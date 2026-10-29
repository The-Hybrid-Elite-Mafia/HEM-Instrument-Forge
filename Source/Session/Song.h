#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

namespace hf
{
inline constexpr int kMaxTracks = 32;

/** One note. Times in beats (quarter notes) from the start of the song. */
struct Note
{
    double start = 0.0;
    double length = 1.0;
    int pitch = 60;
    int velocity = 100;           // 1..127
    int articulation = -1;        // -1 = the track's articulation
    float glide = 0.0f;           // slides in from this many semitones (0 = none)
    juce::uint32 id = 0;          // unique in the song (playback matches note-offs with it)

    double end() const noexcept { return start + length; }
};

struct Track
{
    juce::String name;
    juce::String instrumentId;    // library instrument id ("" = none)
    int bandRole = -1;            // band::Instrument the composer wrote this part for (-1 = drawn / imported)
    std::vector<Note> notes;      // kept sorted by start
    void sortNotes();
};

/**
    The song: tempo + up to 32 tracks of notes. Message thread only. Mixer, stage and track
    settings live in the engine's APVTS (saved next to the song in the session file).
*/
struct Song
{
    double bpm = 100.0;
    int beatsPerBar = 4;
    std::vector<Track> tracks;
    juce::String bandInfo;        // "Outlaw Country - G major - 96 BPM" when BAND wrote it
    juce::var bandRequest;        // the request the composer used (re-roll uses it)
    juce::uint32 nextId = 1;

    /** Last note end, rounded up to a whole bar (at least one bar). */
    double lengthBeats() const;
    juce::uint32 newId() noexcept { return nextId++; }
    /** Gives every note a fresh unique id (after imports / pastes). */
    void renumber();

    juce::ValueTree toTree() const;
    static Song fromTree (const juce::ValueTree&);
};

/** Session file (.hemforge): the song + the engine's parameter state, as XML. */
namespace session
{
    inline constexpr const char* kExtension = ".hemforge";
    bool save (const juce::File&, const Song&, const juce::ValueTree& parameters, juce::String& error);
    bool load (const juce::File&, Song&, juce::ValueTree& parameters, juce::String& error);
}

namespace midi
{
    /** Type-1 MIDI file: tempo track + one track per song track (drum kits on channel 10). */
    bool write (const Song&, const juce::File&, const std::function<bool (int track)>& isDrums);
    /** Reads every track with notes (tempo from the first tempo event). */
    bool read (const juce::File&, Song&, juce::String& error);
}
} // namespace hf
