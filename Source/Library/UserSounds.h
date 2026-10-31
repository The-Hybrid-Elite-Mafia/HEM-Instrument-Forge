#pragma once

#include <juce_core/juce_core.h>
#include <vector>

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
constexpr double kMaxSeconds = 120.0;         // longest file accepted when it is used as it is
constexpr double kMaxSliceSeconds = 600.0;    // longest file accepted for cutting (10 minutes)
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

// ---- cutting a long file (a whole drum stem, a guitar stem) into usable pieces -------------------
// ASSUMED (working guesses, tested on made-up signals and on one real stem, not on a wide range of music):
constexpr double kOnsetRiseDb = 9.0;          // a hit starts when the level jumps this many dB in ~16 ms
constexpr double kOnsetFloorDb = 40.0;        // ... and is within this many dB of the loudest part
constexpr double kOnsetGapSeconds = 0.06;     // two onsets closer than this count as one
constexpr double kKickLowShare = 0.5;         // ASSUMED: share of energy under 100 Hz above which a hit is a kick (measured on one Suno stem: kicks 0.66-0.91, most other hits 0.0)
constexpr double kHatMidShare = 0.085;        // ASSUMED: share of the first 100 ms between 1.5 and 5 kHz at or above which a non-kick hit is a hat (measured on one Suno stem: snares 0-0.08, hats 0.09-0.28)
constexpr int    kTakesPerDrum = 3;           // round-robin takes kept per drum
constexpr double kMaxHitSeconds = 1.5;        // a hit is cut at the next onset or this long

enum class Mode { notes, drumKit, sliceHits, sliceNotes, sliceLoops };

struct SliceOptions
{
    double bpm = 0.0;             // loops: 0 = find the tempo
    int bars = 1;                 // loops: bars per loop (1, 2 or 4)
    int maxSlices = 8;            // notes: most notes kept; loops: most loops made
};

/** Frame positions where a new sound starts (drum hits, plucked notes). */
std::vector<int> findOnsets (const float* mono, int numFrames, double rate);

/** Tempo (60..200 range folded into 75..160) from the rhythm of the onsets, refined against them. 0 if none. */
double detectBpm (const float* mono, int numFrames, double rate, float& confidence);

enum class DrumClass { kick, snare, hat };
DrumClass classifyHit (const float* mono, int numFrames, double rate);

/** Cuts one long recording and builds an instrument from the pieces:
      sliceHits   best isolated kick / snare / hat hits become a drum kit (up to kTakesPerDrum takes each)
      sliceNotes  single notes with a steady pitch become a pitched instrument (up to opt.maxSlices notes)
      sliceLoops  consecutive loops of opt.bars bars, one per key (C2 up) of a kit, each plays through */
Result importSliced (const juce::File& wav, const juce::String& name, Mode mode, const SliceOptions& opt, const juce::File& myFolder);

/** Deletes one imported instrument and rewrites index.json. */
bool remove (const juce::String& id, const juce::File& myFolder);

/** Rewrites <myFolder>/index.json from the instrument folders that exist. */
void rebuildIndex (const juce::File& myFolder);
} // namespace usersounds
} // namespace hf
