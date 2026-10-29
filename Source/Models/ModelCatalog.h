#pragma once

#include "Models/ModelVoices.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace hf::models
{
/** One playing style of a modeled instrument (becomes one articulation of the library instrument). */
struct ModelArticulation
{
    juce::String name;
    int variant = 0;           // passed to the model as NoteSpec::variant
    bool sustain = false;      // held notes keep sounding (the sample gets a crossfade loop)
    float release = 0.25f;     // seconds the note takes to die after note-off
    double seconds = 4.0;      // length rendered per sample
    int velLayers = 3;         // recorded velocity layers
    int roundRobin = 1;        // alternate takes per note (different random pluck / breath)
    double loopPeriod = 0.0;   // seconds; loop length is made a whole number of these (chorus / rotary)
};

/** A modeled instrument: rendered offline by Tools/ModelRender into the same format as the
    recorded library, so the engine plays it exactly like a recorded instrument. */
struct ModelInstrument
{
    juce::String id, name, family, model;
    juce::String kind = "pitched";     // pitched | drums
    int lo = 36, hi = 96;              // playable range
    int step = 3;                      // semitones between sampled notes
    bool mono = false;
    float velTrack = 0.35f;
    std::vector<ModelArticulation> arts;
    std::vector<int> drumNotes;        // kind == drums: the GM notes rendered (each its own zone)
};

const std::vector<ModelInstrument>& catalog();
const ModelInstrument* find (const juce::String& id);

/** "<family slug>/<id>", the same folder rule Tools/build_library.py uses. */
juce::String folderOf (const ModelInstrument&);

struct RenderResult
{
    bool ok = false;
    juce::String message;
    int files = 0;
    juce::int64 bytes = 0;
};

/** Renders every sample of the instrument, writes 24-bit FLACs and instrument.json into
    libraryRoot/folderOf(m). Safe to call for different instruments on different threads. */
RenderResult renderInstrument (const ModelInstrument& m, const juce::File& libraryRoot,
                               const std::atomic<bool>* cancel = nullptr);

/** Bakes a seamless crossfade loop near the end of a held sample. Cuts the buffer at the loop
    end. Returns false (and leaves the buffer alone) if the sample is too short. */
bool makeLoop (juce::AudioBuffer<float>& b, double sampleRate, double period, int& loopStart, int& loopEnd);

/** Cuts the silent tail of a one-shot sample (below -70 dB of its peak) with a short fade. */
void trimTail (juce::AudioBuffer<float>& b, double sampleRate);

/** RMS level (dBFS) of the first `seconds` of the buffer, both channels. */
double rmsDb (const juce::AudioBuffer<float>& b, double sampleRate, double seconds);
} // namespace hf::models
