#pragma once

#include "Engine/ForgeEngine.h"
#include <functional>

namespace hf
{
/**
    Offline export: renders the song through a private copy of the engine (same parameters,
    same instruments, disk streams serviced in the render thread so nothing can drop out).

      stems      one WAV per track, rendered solo with its own stage and room (stems add up
                 to the mix)
      mix        CLEAN: the full mix, turned down only if its peaks go over -1 dBFS;
                 MASTERED: through Elite Master with its input matched to -14 LUFS integrated,
                 true peaks held under -1 dBTP
      MIDI       every track's notes (drum kits on channel 10)

    Runs on a worker thread. Never touches the live engine.
*/
struct ExportOptions
{
    enum class Mix { none, clean, mastered };
    bool stems = true;
    Mix mix = Mix::mastered;
    bool midi = true;
    double sampleRate = 48000.0;
    int bitDepth = 24;                 // 16 (dithered), 24, 32 (float)
    double tailSeconds = 4.0;          // after the last note
    juce::File folder;                 // created if needed
    juce::String baseName = "Song";
};

struct ExportResult
{
    juce::Array<juce::File> files;
    juce::String mixInfo;              // "mastered, -14.0 LUFS, peak -1.1 dBFS"
};

namespace exporter
{
    using Progress = std::function<void (float fraction, const juce::String& what)>;
    using Instruments = std::array<std::shared_ptr<LoadedInstrument>, kMaxTracks>;

    bool run (const Song&, const juce::ValueTree& parameters, const Instruments&, const ExportOptions&,
              ExportResult&, juce::String& error, const Progress& = {}, const std::atomic<bool>* cancel = nullptr);

    /** Renders the song (all tracks, or only `soloTrack`) into a stereo buffer. */
    bool render (const Song&, const juce::ValueTree& parameters, const Instruments&, double sampleRate, double tailSeconds,
                 int soloTrack, juce::AudioBuffer<float>& out, const std::atomic<bool>* cancel = nullptr,
                 const std::function<void (float)>& progress = {});

    /** Elite Master to -14 LUFS (returns "mastered, x LUFS, peak y dBFS"). */
    juce::String master (juce::AudioBuffer<float>& mix, double sampleRate);

    bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, int bitDepth, juce::String& error);

    /** Default export folder: Documents/Hybrid Elite Mafia/Instrument Forge/Exports/<name> <date time>. */
    juce::File defaultFolder (const juce::String& songName);
}
} // namespace hf
