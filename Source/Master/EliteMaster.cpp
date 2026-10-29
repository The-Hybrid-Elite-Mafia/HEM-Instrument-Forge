#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteMaster::info { "elite-master", "Elite Master", "MSTR", "Final limiter with LUFS + true-peak meter (master only)", 0xffcfa63a, true };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout masterLayout()
    {
        params::Layout l;
        params::addDb (l, "input", "Input", 0.0f, 18.0f, 0.0f);
        params::addDb (l, "ceiling", "Ceiling", -3.0f, 0.0f, -1.0f);
        params::addMs (l, "release", "Release", 10.0f, 1000.0f, 80.0f);
        params::addChoice (l, "character", "Character", EliteMaster::characters(), 0);
        params::addChoice (l, "target", "Target", EliteMaster::targets(), 0);
        return l;
    }
}

float EliteMaster::targetLufs (int index)
{
    static constexpr float t[] { -14.0f, -16.0f, -9.0f, -7.0f };
    return t[juce::jlimit (0, 3, index)];
}

EliteMaster::EliteMaster() : ElitePlugin (info, masterLayout())
{
    pInput = raw ("input"); pCeiling = raw ("ceiling"); pRelease = raw ("release"); pChar = raw ("character"); pTarget = raw ("target");
    limiter.setTruePeakMode (true);   // hold inter-sample peaks under the ceiling too (streaming-safe)
}

void EliteMaster::prepareDsp (double fs, int maxBlock)
{
    limiter.prepare (fs, maxBlock);
    loudness.prepare (fs);
    tpMeter.reset();
    warmLow = BiquadCoeffs::lowShelf (fs, 80.0, 1.0);
    warmHigh = BiquadCoeffs::highShelf (fs, 12000.0, -0.7);
    input.reset (fs, 0.05);
    input.setCurrentAndTargetValue (params::dbToGain (param (pInput)));
    lastCeil = 1.0e9f; lastRel = -1.0f;
}

void EliteMaster::resetDsp() noexcept
{
    limiter.reset();
    for (auto& ch : warmSt) for (auto& s : ch) s.reset();
}

bool EliteMaster::matchTarget()
{
    const float integratedNow = getReadout (integrated);
    if (integratedNow <= -70.0f) return false;
    const float tgt = targetLufs ((int) pTarget->load());
    auto* p = apvts.getParameter ("input");
    const float next = juce::jlimit (0.0f, 18.0f, pInput->load() + (tgt - integratedNow));
    p->setValueNotifyingHost (p->convertTo0to1 (next));
    resetMeters();
    return true;
}

void EliteMaster::processDsp (float* left, float* right, int n) noexcept
{
    if (resetRequest.exchange (false))
    {
        loudness.reset();
        tpMeter.reset();
    }
    const float ceil = param (pCeiling), rel = param (pRelease);
    if (ceil != lastCeil) { limiter.setCeilingDb (ceil); lastCeil = ceil; }
    if (rel != lastRel) { limiter.setRelease (rel); lastRel = rel; }
    input.setTargetValue (params::dbToGain (param (pInput)));
    const int character = juce::jlimit (0, 2, (int) param (pChar));
    const float ceilLin = params::dbToGain (ceil);

    for (int i = 0; i < n; ++i)
    {
        const float g = input.getNextValue();
        float l = left[i] * g, r = right[i] * g;
        if (character == 1)
        {
            // Punch: soft-clip only the tallest transients (above the ceiling + 1.5 dB) before the
            // limiter, so it has less to do and the drums keep their snap.
            const float knee = ceilLin * 1.19f;
            auto soft = [knee] (float x) { const float a = std::abs (x); return a <= knee ? x : std::copysign (knee + (a - knee) / (1.0f + (a - knee) / knee), x); };
            l = soft (l); r = soft (r);
        }
        else if (character == 2)
        {
            // Warm: a touch of low-end weight, a softer top, gentle tape-like saturation
            l = warmSt[0][1].process (warmHigh, warmSt[0][0].process (warmLow, l));
            r = warmSt[1][1].process (warmHigh, warmSt[1][0].process (warmLow, r));
            l = std::tanh (l * 0.9f) / 0.9f; r = std::tanh (r * 0.9f) / 0.9f;
        }
        left[i] = l; right[i] = r;
    }

    const float grDb = limiter.process (left, right, n, false);
    loudness.process (left, right, n);
    tpMeter.process (left, right, n);

    setReadout (EliteMaster::gr, grDb);
    setReadout (momentary, loudness.getMomentary());
    setReadout (shortTerm, loudness.getShortTerm());
    setReadout (integrated, loudness.getIntegrated());
    setReadout (truePeak, params::gainToDb (tpMeter.get()));
    const float integ = loudness.getIntegrated();
    setReadout (suggestedInput, integ > -70.0f ? juce::jlimit (0.0f, 18.0f, param (pInput) + (targetLufs ((int) param (pTarget)) - integ)) : param (pInput));
}
} // namespace pg
