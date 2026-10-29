#include "ElitePlugin.h"

namespace pg
{
namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout withBypass (juce::AudioProcessorValueTreeState::ParameterLayout layout)
    {
        params::addBool (layout, "bypass", "Bypass", false);
        return layout;
    }

    constexpr double kCrossfadeMs = 5.0;
}

ElitePlugin::ElitePlugin (const PluginInfo& i, juce::AudioProcessorValueTreeState::ParameterLayout layout)
    : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      info (i),
      apvts (*this, nullptr, juce::Identifier (juce::String (i.id).replaceCharacter ('-', '_').toUpperCase()), withBypass (std::move (layout)))
{
    bypassParam = apvts.getRawParameterValue ("bypass");
    for (auto& r : readouts) r.store (0.0f);
}

std::atomic<float>* ElitePlugin::raw (const juce::String& id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);   // a plugin asked for a parameter it never declared
    return p;
}

bool ElitePlugin::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void ElitePlugin::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize = juce::jmax (1, maximumExpectedSamplesPerBlock);
    dry.setSize (2, maxBlockSize, false, true, false);
    prepareDsp (sampleRateHz, maxBlockSize);
    latency = juce::jmax (0, dspLatency());
    setLatencySamples (latency);
    dryDelayL.assign ((size_t) juce::jmax (1, latency), 0.0f);
    dryDelayR.assign ((size_t) juce::jmax (1, latency), 0.0f);
    dryDelayPos = 0;
    wetStep = (float) (1.0 / juce::jmax (1.0, kCrossfadeMs * 0.001 * sampleRateHz));
    wetMix = isBypassedNow() ? 0.0f : 1.0f;
    wasFullyBypassed = wetMix == 0.0f;
    resetDsp();
}

void ElitePlugin::reset()
{
    resetDsp();
    std::fill (dryDelayL.begin(), dryDelayL.end(), 0.0f);
    std::fill (dryDelayR.begin(), dryDelayR.end(), 0.0f);
    dryDelayPos = 0;
}

bool ElitePlugin::isBypassedNow() const noexcept
{
    const bool own = bypassParam != nullptr && bypassParam->load (std::memory_order_relaxed) > 0.5f;
    const bool global = globalBypass != nullptr && globalBypass->load (std::memory_order_relaxed);
    const bool group = groupBypass != nullptr && groupBypass->load (std::memory_order_relaxed);
    return own || global || group;
}

void ElitePlugin::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() >= 2)
    {
        processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    }
    else if (buffer.getNumChannels() == 1)
    {
        // Mono hosts: process a stereo copy and fold back. dry has 2 channels of maxBlock samples.
        auto* m = buffer.getWritePointer (0);
        for (int start = 0; start < n; start += maxBlockSize)
        {
            const int len = juce::jmin (maxBlockSize, n - start);
            float tmpL[64], tmpR[64];
            for (int s = 0; s < len; s += 64)
            {
                const int k = juce::jmin (64, len - s);
                std::copy (m + start + s, m + start + s + k, tmpL);
                std::copy (m + start + s, m + start + s + k, tmpR);
                processStereo (tmpL, tmpR, k);
                for (int i = 0; i < k; ++i) m[start + s + i] = 0.5f * (tmpL[i] + tmpR[i]);
            }
        }
    }
}

void ElitePlugin::processStereo (float* left, float* right, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    if (numSamples <= 0) return;

    // Hosts may hand us more than they promised; split into prepared-size chunks.
    if (numSamples > maxBlockSize)
    {
        for (int start = 0; start < numSamples; start += maxBlockSize)
            processStereo (left + start, right + start, juce::jmin (maxBlockSize, numSamples - start));
        return;
    }

    float in = 0.0f;
    for (int i = 0; i < numSamples; ++i) in = juce::jmax (in, std::abs (left[i]), std::abs (right[i]));
    inPeak.store (in, std::memory_order_relaxed);

    const float target = isBypassedNow() ? 0.0f : 1.0f;
    const bool steadyBypass = target == 0.0f && wetMix == 0.0f;

    // Dry copy, delayed by our latency so the crossfade is phase-aligned.
    auto* dl = dry.getWritePointer (0);
    auto* dr = dry.getWritePointer (1);
    if (latency == 0)
    {
        std::copy (left, left + numSamples, dl);
        std::copy (right, right + numSamples, dr);
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            dl[i] = dryDelayL[(size_t) dryDelayPos];
            dr[i] = dryDelayR[(size_t) dryDelayPos];
            dryDelayL[(size_t) dryDelayPos] = left[i];
            dryDelayR[(size_t) dryDelayPos] = right[i];
            if (++dryDelayPos >= latency) dryDelayPos = 0;
        }
    }

    if (steadyBypass)
    {
        // Fully bypassed: skip the DSP entirely; a latent plugin still outputs its aligned dry.
        std::copy (dl, dl + numSamples, left);
        std::copy (dr, dr + numSamples, right);
        wasFullyBypassed = true;
    }
    else
    {
        if (wasFullyBypassed)
        {
            resetDsp();     // stale tails must not come back when the plugin is switched on again
            wasFullyBypassed = false;
        }

        processDsp (left, right, numSamples);

        if (wetMix != target || target != 1.0f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                wetMix = target > wetMix ? juce::jmin (target, wetMix + wetStep) : juce::jmax (target, wetMix - wetStep);
                left[i]  = dl[i] + wetMix * (left[i] - dl[i]);
                right[i] = dr[i] + wetMix * (right[i] - dr[i]);
            }
        }
    }

    float out = 0.0f;
    for (int i = 0; i < numSamples; ++i) out = juce::jmax (out, std::abs (left[i]), std::abs (right[i]));
    outPeak.store (out, std::memory_order_relaxed);

    if (analyzerActive.load (std::memory_order_relaxed))
        pushAnalyzer (left, right, numSamples);
}

void ElitePlugin::pushAnalyzer (const float* left, const float* right, int numSamples) noexcept
{
    int start1, size1, start2, size2;
    analyzerFifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) analyzerData[(size_t) (start1 + i)] = 0.5f * (left[i] + right[i]);
    for (int i = 0; i < size2; ++i) analyzerData[(size_t) (start2 + i)] = 0.5f * (left[size1 + i] + right[size1 + i]);
    analyzerFifo.finishedWrite (size1 + size2);   // when full, the newest samples are dropped
}

int ElitePlugin::readAnalyzer (float* dest, int maxSamples) noexcept
{
    int start1, size1, start2, size2;
    analyzerFifo.prepareToRead (maxSamples, start1, size1, start2, size2);
    std::copy (analyzerData.begin() + start1, analyzerData.begin() + start1 + size1, dest);
    std::copy (analyzerData.begin() + start2, analyzerData.begin() + start2 + size2, dest + size1);
    analyzerFifo.finishedRead (size1 + size2);
    return size1 + size2;
}

//==============================================================================
juce::ValueTree ElitePlugin::saveState()
{
    auto state = apvts.copyState();
    state.setProperty ("pluginId", info.id, nullptr);
    return state;
}

void ElitePlugin::loadState (const juce::ValueTree& tree)
{
    if (! tree.isValid()) return;
    if (tree.hasType (apvts.state.getType()))
    {
        // Only parameters we know; a preset from a newer version may carry extra ones.
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto child = tree.getChildWithProperty ("id", rp->getParameterID());
                if (child.isValid() && child.hasProperty ("value"))
                    rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                else
                    rp->setValueNotifyingHost (rp->getDefaultValue());
            }
    }
}

void ElitePlugin::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void ElitePlugin::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = saveState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ElitePlugin::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        loadState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
namespace params
{
    void addDb (Layout& l, const char* id, const char* name, float min, float max, float def, float step)
    {
        juce::NormalisableRange<float> range (min, max, step);
        if (min < 0.0f && max > 0.0f && std::abs (min + max) > 1.0e-3f)
            range.setSkewForCentre (0.0f);   // boost / cut knobs: 0 dB always sits straight up
        l.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, range, def,
                                                            juce::AudioParameterFloatAttributes().withLabel ("dB")
                                                                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    }

    void addHz (Layout& l, const char* id, const char* name, float min, float max, float def)
    {
        juce::NormalisableRange<float> r (min, max, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                                          [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                                          [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
        l.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, r, def,
                                                            juce::AudioParameterFloatAttributes().withLabel ("Hz")
                                                                .withStringFromValueFunction ([] (float v, int)
                                                                {
                                                                    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                                                        : juce::String ((int) std::round (v)) + " Hz";
                                                                })));
    }

    void addMs (Layout& l, const char* id, const char* name, float min, float max, float def)
    {
        juce::NormalisableRange<float> r (min, max, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                                          [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                                          [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
        l.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, r, def,
                                                            juce::AudioParameterFloatAttributes().withLabel ("ms")
                                                                .withStringFromValueFunction ([] (float v, int)
                                                                {
                                                                    return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                                                                                        : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
                                                                })));
    }

    void addFloat (Layout& l, const char* id, const char* name, float min, float max, float def, const char* unit, float step, float centre)
    {
        juce::NormalisableRange<float> r (min, max, step);
        if (centre > min && centre < max) r.setSkewForCentre (centre);
        const juce::String u (unit);
        l.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, r, def,
                                                            juce::AudioParameterFloatAttributes().withLabel (u)
                                                                .withStringFromValueFunction ([u] (float v, int)
                                                                { return juce::String (v, std::abs (v) < 10.0f ? 2 : 1) + (u.isEmpty() ? "" : " " + u); })));
    }

    void addPercent (Layout& l, const char* id, const char* name, float def)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")
                                                                .withStringFromValueFunction ([] (float v, int) { return juce::String ((int) std::round (v)) + " %"; })));
    }

    void addBool (Layout& l, const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (pid (id), name, def));
    }

    void addChoice (Layout& l, const char* id, const char* name, const juce::StringArray& choices, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (pid (id), name, choices, def));
    }
}
} // namespace pg
