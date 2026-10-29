#include "Sampler/LoadedInstrument.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace hf
{
namespace
{
bool loadZone (juce::AudioFormatManager& fm, const InstrumentDef& def, const Zone& z, LoadedZone& out, juce::String& error)
{
    out.def = &z;
    out.file = def.folder.getChildFile (z.file);
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (out.file));
    if (r == nullptr) { error = "Can't read " + out.file.getFullPathName(); return false; }
    if (std::abs (r->sampleRate - def.sampleRate) > 1.0)
    {
        error = out.file.getFileName() + " is at " + juce::String (r->sampleRate) + " Hz, the instrument says " + juce::String (def.sampleRate);
        return false;
    }
    const int frames = (int) juce::jmin ((juce::int64) z.frames, r->lengthInSamples);
    if (frames <= 0) { error = out.file.getFileName() + " is empty"; return false; }
    // whole sample in memory when it is short, or when its loop starts inside the head
    // (the stream would have to jump back into the head)
    out.resident = frames <= 2 * LoadedInstrument::kHeadFrames || (z.hasLoop() && z.loopStart < LoadedInstrument::kHeadFrames + 1024);
    out.headFrames = out.resident ? frames : LoadedInstrument::kHeadFrames;
    out.head.setSize (2, out.headFrames + LoadedInstrument::kGuard);
    out.head.clear();
    if (! r->read (&out.head, 0, out.headFrames, 0, true, true)) { error = "Can't decode " + out.file.getFullPathName(); return false; }
    if (r->numChannels == 1) out.head.copyFrom (1, 0, out.head, 0, 0, out.headFrames);
    if (out.resident && z.hasLoop())
        for (int g = 0; g < LoadedInstrument::kGuard; ++g)   // guard frames continue the loop
            for (int ch = 0; ch < 2; ++ch)
                out.head.setSample (ch, out.headFrames + g, out.head.getSample (ch, z.loopStart + g));
    out.gain = juce::Decibels::decibelsToGain (z.gainDb);
    return true;
}

void index (std::vector<LoadedZone>& zones, std::array<std::vector<int>, 128>& byKey)
{
    for (int i = 0; i < (int) zones.size(); ++i)
    {
        const auto& z = *zones[(size_t) i].def;
        for (int k = juce::jmax (0, z.lo); k <= juce::jmin (127, z.hi); ++k)
            byKey[(size_t) k].push_back (i);
    }
}
} // namespace

std::shared_ptr<LoadedInstrument> LoadedInstrument::load (std::shared_ptr<const InstrumentDef> d, juce::String& error,
                                                          const std::atomic<bool>* cancel, std::function<void (float)> progress)
{
    if (d == nullptr) { error = "No instrument"; return nullptr; }
    auto li = std::make_shared<LoadedInstrument>();
    li->def = d;
    li->gain = juce::Decibels::decibelsToGain (d->gainDb);
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const int total = juce::jmax (1, d->numZones());
    int done = 0;
    li->arts.resize (d->articulations.size());
    for (size_t a = 0; a < d->articulations.size(); ++a)
    {
        const auto& src = d->articulations[a];
        auto& dst = li->arts[a];
        dst.def = &src;
        dst.zones.resize (src.zones.size());
        dst.releaseZones.resize (src.releaseZones.size());
        for (size_t i = 0; i < src.zones.size(); ++i)
        {
            if (cancel != nullptr && cancel->load()) { error = "cancelled"; return nullptr; }
            if (! loadZone (fm, *d, src.zones[i], dst.zones[i], error)) return nullptr;
            li->bytes += (size_t) dst.zones[i].head.getNumSamples() * 2 * sizeof (float);
            if (progress && (++done % 16) == 0) progress ((float) done / (float) total);
        }
        for (size_t i = 0; i < src.releaseZones.size(); ++i)
        {
            if (cancel != nullptr && cancel->load()) { error = "cancelled"; return nullptr; }
            if (! loadZone (fm, *d, src.releaseZones[i], dst.releaseZones[i], error)) return nullptr;
            li->bytes += (size_t) dst.releaseZones[i].head.getNumSamples() * 2 * sizeof (float);
            ++done;
        }
        index (dst.zones, dst.byKey);
        index (dst.releaseZones, dst.releaseByKey);
    }
    if (progress) progress (1.0f);
    return li;
}
} // namespace hf
