#pragma once

#include "Sampler/DiskStreamer.h"
#include <array>

namespace hf
{
/** Per-track playing state, owned by the audio thread. */
struct TrackPlayState
{
    const LoadedInstrument* instrument = nullptr;
    int keyswitchArticulation = -1;      // set by keyswitch notes while playing live (-1 = none)
    bool pedal = false;                  // sustain pedal (live MIDI CC64)
    float bendSemitones = 0.0f;          // live pitch bend
    std::array<juce::uint16, 128> rrCounter {};
};

/**
    The sample player: a fixed pool of voices shared by every track.

    A note plays every zone of the chosen articulation that matches its key, velocity, round-robin
    pass and pedal state (velocity cross-fades and layers work like SFZ). Each zone voice reads the
    in-memory head first and continues from its disk stream. Pitch: cubic Hermite interpolation.
    Envelopes: short attack ramp, release by exponential decay to -60 dB over the release time.

    Everything is allocated in prepare(); noteOn / noteOff / render never allocate or lock.
*/
class VoicePool
{
public:
    static constexpr int kVoices = 256;

    struct NoteOn
    {
        int track = 0;
        int key = 60;                 // played key (transposed already)
        int velocity = 100;           // 1..127 (humanized already)
        int articulation = 0;         // index into the instrument's articulations
        juce::uint32 noteId = 0;      // matches the noteOff
        int offset = 0;               // samples into the current block
        bool legato = false;          // mono legato: ties into the previous note of the track
        bool mono = false;            // one note at a time on this track
        float velTrack = 0.6f;        // 0 = velocity picks layers only, 1 = full SFZ curve
        float glideSemitones = 0.0f;  // starts this many semitones off and slides to the note (slides / bends)
        float releaseScale = 1.0f;    // track "release" control
    };

    void prepare (double sampleRate, int maxBlock, DiskStreamer& streamer);

    void noteOn (const NoteOn&, TrackPlayState&) noexcept;
    /** Releases the voices started by noteId on that track (pedal-held if the pedal is down). */
    void noteOff (int track, juce::uint32 noteId, int offset, TrackPlayState&) noexcept;
    /** Pedal up: releases every pedal-held voice of the track. */
    void pedalUp (int track) noexcept;
    /** Releases (kill = 5 ms fade) every voice of a track (-1 = all tracks). */
    void allOff (int track, bool kill) noexcept;
    /** Fast-fades every voice playing this instrument (it is being replaced). */
    void killInstrument (const LoadedInstrument*) noexcept;

    /** Renders every active voice, added into its track's buffers (trackL[t] / trackR[t]). */
    void render (float* const* trackL, float* const* trackR, int numTracks, int numSamples, const TrackPlayState* states) noexcept;

    int activeVoices() const noexcept { return active.load (std::memory_order_relaxed); }
    int voicesOnTrack (int track) const noexcept;
    /** Notes refused because all voices were busy (stolen voices are not counted here). */
    std::atomic<int> steals { 0 };

private:
    struct Voice
    {
        bool on = false;
        int track = 0, key = 0, art = 0, velocity = 100;
        juce::uint32 noteId = 0;
        juce::uint32 age = 0;
        const LoadedInstrument* inst = nullptr;
        const LoadedZone* zone = nullptr;
        int slot = -1;                 // disk stream, -1 = resident
        double pos = 0.0;              // stream frame (fractional)
        double baseStep = 1.0;         // frames per output sample at the note's pitch
        float glide = 0.0f, glideCoef = 0.0f;
        float gain = 1.0f;
        float env = 0.0f, attackStep = 1.0f;
        bool releasing = false, pedalHeld = false, isReleaseSample = false;
        float relCoef = 0.999f, relSeconds = 0.3f;
        float killStep = 0.0f;         // > 0: linear fade-out per sample
        int delay = 0;                 // samples to wait before starting (in-block offset)
        int releaseDelay = -1;         // samples until the release starts (in-block note-off)
        float lastL = 0.0f, lastR = 0.0f;
        double startSeconds = 0.0;     // engine time at note start (release-sample level)
    };

    Voice* allocate() noexcept;
    void startZone (Voice&, const NoteOn&, const LoadedZone&, float gain, double startFrame, float attackSeconds) noexcept;
    void beginRelease (Voice&) noexcept;
    void finish (Voice&) noexcept;
    bool renderVoice (Voice&, float* l, float* r, int numSamples, float bendSemis) noexcept;   // false = voice ended
    void triggerReleaseZones (const Voice& v, TrackPlayState&) noexcept;

    std::array<Voice, kVoices> voices;
    DiskStreamer* disk = nullptr;
    double sr = 48000.0;
    juce::uint32 ageCounter = 0;
    double clockSeconds = 0.0;
    std::atomic<int> active { 0 };
};
} // namespace hf
