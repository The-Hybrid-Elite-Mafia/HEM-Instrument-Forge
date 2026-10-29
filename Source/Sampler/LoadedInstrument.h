#pragma once

#include "Library/InstrumentDef.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <functional>

namespace hf
{
/**
    An instrument ready to play: the first part of every sample ("head") decoded into memory,
    short samples decoded completely. The rest of long samples is streamed from disk while the
    note plays (DiskStreamer), so a 1 GB piano does not need 1 GB of RAM.

    Built on a worker thread (load()), immutable afterwards. The audio thread only reads it.
    Lifetime: tracks hold a shared_ptr on the message thread; voices and disk streams count
    themselves in `users` so the engine frees an old instrument only when nothing plays it.
*/
struct LoadedZone
{
    const Zone* def = nullptr;
    juce::File file;
    juce::AudioBuffer<float> head;     // 2 channels, headFrames + kGuard frames (zeros after the end)
    int headFrames = 0;                // frames held in `head`
    bool resident = false;             // the whole sample is in `head` (no streaming)
    float gain = 1.0f;                 // zone level (linear)
};

struct LoadedArticulation
{
    const Articulation* def = nullptr;
    std::vector<LoadedZone> zones, releaseZones;
    /** zone indices covering each key (built once, read by the audio thread) */
    std::array<std::vector<int>, 128> byKey, releaseByKey;
};

class LoadedInstrument
{
public:
    static constexpr int kHeadFrames = 24576;   // ~0.5 s at 48 kHz decoded up front
    static constexpr int kGuard = 4;            // frames after the head for interpolation

    /** Decodes heads / short samples. Worker thread. Returns nullptr + error on failure
        (a missing or unreadable sample fails the whole instrument). `cancel` may be null. */
    static std::shared_ptr<LoadedInstrument> load (std::shared_ptr<const InstrumentDef>, juce::String& error,
                                                   const std::atomic<bool>* cancel = nullptr,
                                                   std::function<void (float)> progress = {});

    const InstrumentDef& getDef() const noexcept { return *def; }
    std::shared_ptr<const InstrumentDef> getDefPtr() const noexcept { return def; }
    int numArticulations() const noexcept { return (int) arts.size(); }
    const LoadedArticulation& articulation (int i) const noexcept { return arts[(size_t) juce::jlimit (0, (int) arts.size() - 1, i)]; }
    size_t memoryBytes() const noexcept { return bytes; }
    double libraryRate() const noexcept { return def->sampleRate; }
    float instrumentGain() const noexcept { return gain; }

    /** Voices and disk streams currently using this instrument (audio / disk threads). */
    mutable std::atomic<int> users { 0 };

private:
    std::shared_ptr<const InstrumentDef> def;
    std::vector<LoadedArticulation> arts;
    size_t bytes = 0;
    float gain = 1.0f;
};
} // namespace hf
