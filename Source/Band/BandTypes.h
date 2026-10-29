#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <vector>

/**
    BAND FORGE - auto-written instrumentals.

    BandTypes.h holds the plain data: genres, instruments, notes, the song plan and the music
    theory helpers. The composer (BandComposer) turns a Request into a Song (notes per part);
    the renderer (BandRender) plays every part through the modeled instruments (BandInstruments)
    into one stereo stem per part. Everything here is offline (not on the audio thread).
*/
namespace pg::band
{
//==============================================================================
enum class Genre
{
    outlawCountry = 0, bluegrass, folk, gospel, blues, trap, popTrap, cinematic, count
};

enum class Instrument
{
    acousticGuitar = 0, electricGuitar, banjo, mandolin, bass, uprightBass, bass808,
    fiddle, harmonica, piano, organ, strings, drums, trapDrums, count
};

inline constexpr int kNumGenres = (int) Genre::count;
inline constexpr int kNumInstruments = (int) Instrument::count;

const char* genreName (Genre);
const char* instrumentName (Instrument);

/** Instruments a genre uses by default, in mixer order. */
std::vector<Instrument> defaultInstruments (Genre);
/** Every instrument that makes sense for a genre (the check boxes shown in BAND FORGE). */
std::vector<Instrument> availableInstruments (Genre);

//==============================================================================
/** One note. Times are in beats (quarter notes) from the start of the song. */
struct Note
{
    double start = 0.0;
    double length = 1.0;
    int pitch = 60;            // MIDI note; drums use the General MIDI drum numbers
    float velocity = 0.8f;     // 0..1
    int articulation = 0;      // instrument specific (see Art)
};

namespace art
{
    // strings / guitars
    inline constexpr int normal = 0, muted = 1, slideUp = 2, accent = 3, chop = 4;
    // harmonica / fiddle
    inline constexpr int bend = 2, doubleStop = 5;
}

namespace drum
{
    inline constexpr int kick = 36, rim = 37, snare = 38, clap = 39, brush = 40,
                         hatClosed = 42, hatPedal = 44, hatOpen = 46,
                         tomLow = 45, tomMid = 47, tomHigh = 50, crash = 49, ride = 51, rideBell = 53,
                         tamb = 54, shaker = 70;
}

struct Part
{
    Instrument instrument = Instrument::acousticGuitar;
    float pan = 0.0f;          // -1..1
    std::vector<Note> notes;
};

//==============================================================================
enum class Section { intro, verse, chorus, bridge, solo, outro };
const char* sectionName (Section);

struct SectionPlan
{
    Section kind = Section::verse;
    int startBar = 0;
    int bars = 8;
    float energy = 0.6f;               // 0..1, drives density and loudness
    std::vector<int> chordRoots;       // one entry per half bar: scale degree 0..6
};

/** What the user asks for. */
struct Request
{
    Genre genre = Genre::outlawCountry;
    int keyRoot = 7;                   // pitch class, 0 = C (7 = G)
    bool minor = false;
    double bpm = 0.0;                  // 0 = genre default
    bool fullSong = true;              // false = about one minute
    std::vector<Instrument> instruments;   // empty = genre default
    uint32_t seed = 1;
    /** Per-part seed (0 = derived from `seed`): re-rolls one part over the same chords. */
    std::array<uint32_t, (size_t) Instrument::count> partSeeds {};
};

/** The finished plan: tempo, sections and the notes for every part. */
struct Song
{
    Request request;
    double bpm = 100.0;
    int beatsPerBar = 4;
    double swing = 0.0;                // 0 = straight, 1 = full triplet swing on 8ths
    int totalBars = 0;
    std::vector<SectionPlan> sections;
    std::vector<Part> parts;

    double secondsPerBeat() const noexcept { return 60.0 / bpm; }
    double lengthBeats() const noexcept { return (double) totalBars * beatsPerBar; }
    /** Song length in seconds, plus `tail` for ringing notes. */
    double lengthSeconds (double tail = 3.0) const noexcept { return lengthBeats() * secondsPerBeat() + tail; }
    /** "Outlaw Country - G major - 96 BPM" */
    juce::String describe() const;
};

//==============================================================================
/** Music theory helpers. */
namespace theory
{
    const std::array<int, 7>& scale (bool minor);
    /** MIDI pitch of scale degree `degree` (any integer, wraps into octaves) in the key, octave of `baseOctave` (4 = middle C octave). */
    int degreePitch (int keyRoot, bool minor, int degree, int baseOctave);
    /** Triad (or seventh) chord tones as pitch classes for the chord built on `degree`. */
    std::vector<int> chordPitchClasses (int keyRoot, bool minor, int degree, bool seventh = false);
    /** Is pitch class `pc` in the key's scale? (blues notes are not) */
    bool inScale (int keyRoot, bool minor, int pc);
    const char* keyName (int keyRoot);
    /** Lowest pitch >= lowest whose pitch class is pc. */
    int atOrAbove (int pc, int lowest);
}

//==============================================================================
/** Small deterministic random generator (xorshift), so the same seed gives the same song. */
struct Rng
{
    explicit Rng (uint32_t s) : state (s != 0 ? s : 0x9E3779B9u) {}
    uint32_t next() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    double uniform() noexcept { return (next() >> 8) * (1.0 / 16777216.0); }           // [0,1)
    double range (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }
    int below (int n) noexcept { return n <= 0 ? 0 : (int) (next() % (uint32_t) n); }
    bool chance (double p) noexcept { return uniform() < p; }
    uint32_t state;
};
} // namespace pg::band
