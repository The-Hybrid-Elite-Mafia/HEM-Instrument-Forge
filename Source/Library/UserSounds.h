#pragma once

#include <juce_core/juce_core.h>

namespace hf
{
/**
    My Sounds: turns the user's own WAV recordings (guitar notes, drum hits) into instruments of the
    Forge, played back as recorded. This is sampling. Nothing is learned or generated from the audio.

    Every import converts the audio to the library rate (48 kHz, 24-bit WAV), writes
    <myFolder>/<id>/instrument.json plus the samples, and rewrites <myFolder>/index.json.
    Library::addFolder(myFolder) then lists them. Worker thread or message thread (it reads and writes files).
*/
namespace usersounds
{
// ---- limits and working guesses (ASSUMED: set by listening tests on the maker's side, not measured against other tools)
constexpr double kRate = 48000.0;             // the library rate; every instrument in the library is 48 kHz
constexpr double kMaxSeconds = 120.0;         // longest file accepted
constexpr int    kSpreadUp = 12;              // ASSUMED: a pitched sample is stretched this many semitones up from its root
constexpr int    kSpreadDown = 12;            // ASSUMED: ... and this many down (more sounds thin or chipmunk-like)
constexpr float  kPeakDb = -3.0f;             // loudest sample of an instrument is stored at this peak
constexpr double kMinHz = 40.0, kMaxHz = 2000.0;   // pitch search range
constexpr float  kMinConfidence = 0.80f;      // below this the pitch is "not found" and the user must set the note

struct Result
{
    bool ok = false;
    juce::String error;
    juce::String id, name;
    int samples = 0;
    juce::String report;          // plain-words summary, one line per file
};

enum class Kind { pitched, drums };

juce::File defaultFolder();       // Documents/Hybrid Elite Mafia/Instrument Forge/My Sounds

/** Pitch of a mono signal in Hz (YIN with parabolic interpolation). Returns 0 when no steady pitch is found.
    `confidence` is 0..1 (1 - the YIN dip). */
double detectPitchHz (const float* mono, int numFrames, double rate, float& confidence);

/** GM drum note for a file name ("Kick 01.wav" -> 36, "hat open" -> 46 ...). -1 when the name says nothing. */
int drumNoteForName (const juce::String& fileName);

/** "E2", "c#3", "Bb1" or "40" -> MIDI note (middle C = C4 = 60). -1 when it is not a note. */
int parseNoteName (const juce::String&);

/** One or more recordings of the same instrument (several notes of a guitar, say). Each file's pitch is
    found by analysis unless `forcedNote` >= 0 (then every file uses it, which is only sensible for one file).
    Files with the same note become round-robin takes. */
Result importPitched (const juce::Array<juce::File>& wavs, const juce::String& name, int forcedNote, const juce::File& myFolder);

/** A drum kit from hit recordings. Notes come from the file names, the rest fill free GM notes from 36 up. */
Result importKit (const juce::Array<juce::File>& wavs, const juce::String& name, const juce::File& myFolder);

/** Deletes one imported instrument and rewrites index.json. */
bool remove (const juce::String& id, const juce::File& myFolder);

/** Rewrites <myFolder>/index.json from the instrument folders that exist. */
void rebuildIndex (const juce::File& myFolder);
} // namespace usersounds
} // namespace hf
