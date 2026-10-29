#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace hf
{
/**
    One instrument of the HEM Instrument Library, as described by its instrument.json
    (written by Tools/build_library.py or Tools/ModelRender). Plain data, read on a worker thread.

    Zones follow SFZ rules: every zone whose key range, velocity range, round-robin slot and pedal
    condition match a note plays (so velocity cross-fades and layered samples work).
*/
struct Zone
{
    juce::String file;            // relative to the instrument folder
    int frames = 0;               // length at the library rate
    int root = 60, lo = 0, hi = 127;
    int vlo = 0, vhi = 127;
    float gainDb = 0.0f;          // the zone's own level (SFZ volume)
    float tuneCents = 0.0f;       // correction that puts the recording in tune
    int rr = 1, rrCount = 1;      // round robin: plays on pass rr of rrCount
    bool keytrack = true;         // false: plays at its recorded pitch on every key of its range
    int loopStart = -1, loopEnd = -1;   // frames; -1 = no loop
    int xinLo = -1, xinHi = -1;   // velocity fade-in range (SFZ xfin_lovel / xfin_hivel)
    int xoutLo = -1, xoutHi = -1; // velocity fade-out range
    int pedal = -1;               // -1 any, 0 only with the sustain pedal up, 1 only with it down
    float release = -1.0f;        // seconds; -1 = the articulation's release

    bool hasLoop() const noexcept { return loopStart >= 0 && loopEnd > loopStart + 64; }
    /** Gain (0..1) from the velocity cross-fade ranges for velocity 0..127. */
    float crossfadeGain (int velocity) const noexcept;
};

struct Articulation
{
    juce::String name;
    float release = 0.3f;         // seconds the note takes to die away after note-off
    bool loop = false;            // sustained (held notes keep sounding)
    std::vector<Zone> zones;
    std::vector<Zone> releaseZones;   // played at note-off (key-up noises, release tails)
};

struct Seat
{
    float x = 0.0f;               // -1 left .. +1 right
    float depth = 0.5f;           // 0 front of the stage .. 1 back
    float height = 0.25f;         // 0 floor .. 1 high
};

enum class InstrumentKind { pitched, perc, drums };

struct InstrumentDef
{
    juce::String id, name, family, source, licence, credit;
    InstrumentKind kind = InstrumentKind::pitched;
    float gainDb = 0.0f;          // levels the instrument with the rest of the library
    float velTrack = 0.6f;        // 0 = velocity changes only the layer, 1 = full SFZ velocity curve
    bool mono = false;            // plays one note at a time by default (winds, solo strings)
    int rangeLo = 0, rangeHi = 127;
    Seat seat;
    double sampleRate = 48000.0;
    std::vector<Articulation> articulations;
    juce::File folder;

    int numZones() const noexcept;
    bool isDrums() const noexcept { return kind == InstrumentKind::drums; }

    /** Reads and checks <folder>/instrument.json. Returns nullptr and sets error if it is not valid. */
    static std::shared_ptr<InstrumentDef> load (const juce::File& folder, juce::String& error);
};

/** MIDI note -> "C4" (middle C = C4 = 60). */
juce::String noteName (int midiNote);
} // namespace hf
