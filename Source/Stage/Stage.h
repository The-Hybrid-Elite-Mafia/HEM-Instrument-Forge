#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace hf
{
/**
    THE 3D STAGE

    Every track is a player sitting on a stage inside a room. What you hear is built from:
      direct sound   - level falls with distance (inverse distance), air takes away the highs,
                       sound arrives later from further back, the image narrows with distance;
      early echoes   - six first reflections (side walls, back wall, front wall, floor, ceiling)
                       worked out from the room's size and the player's seat (image sources);
      room tail      - a shared 8-line feedback-delay-network reverb tuned per room (decay times
                       for low and high frequencies, size, diffusion). More of a player goes to it
                       the further back they sit and the further the "mics" are.
    Output: normal stereo (panning by the seat's direction), or HEADPHONE 3D: a spherical-head
    model (arrival-time difference between the ears + head-shadow filter for each ear). This is a
    model, not measured head responses; it works on headphones only.

    The rooms are simulated by code. There are no recorded room responses in this app.

    All memory is allocated in prepare(); process() never allocates.
*/
struct RoomPreset
{
    const char* name;
    float width, length, height;   // metres
    float reflectivity;            // 0..1 wall reflection gain for the early echoes
    float rtLow, rtHigh;           // tail decay (s) below / above ~1 kHz
    float predelayMs;              // gap before the tail starts
    float diffusion;               // 0..1
    float tailLevelDb;             // how loud the room is at the default send
    bool walls;                    // false: only the floor reflects (open air, plate)
};

const std::vector<RoomPreset>& roomPresets();
juce::StringArray roomNames();

/** Seat and room values for one track (read from the APVTS by the engine each block). */
struct StageParams
{
    float x = 0.0f, depth = 0.4f, height = 0.25f;   // seat
    float width = 1.0f;                               // stereo width of the instrument itself
    float roomSendDb = 0.0f;                          // track's extra room send
};

struct GlobalStage
{
    int room = 1;
    float roomSize = 1.0f;      // 0.5 .. 1.5 scales the room
    float micDistance = 0.3f;   // 0 close .. 1 far back
    bool headphone3D = false;
};

/** Spatial processor for one track. */
class StageTrack
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    /** in: the track's stereo signal. Adds the direct sound + early echoes into outL/outR and the
        room-tail feed into sendL/sendR. */
    void process (const float* inL, const float* inR, float* outL, float* outR, float* sendL, float* sendR,
                  int numSamples, const StageParams&, const GlobalStage&) noexcept;

    /** Geometry results (UI shows them; tests check them). Updated by process(). */
    struct Geometry
    {
        float distance = 1.5f, azimuth = 0.0f, gain = 1.0f, delayMs = 0.0f, airHz = 20000.0f, sendGain = 0.5f;
        std::array<float, 6> tapDelayMs {}, tapGain {}, tapPan {};
    };
    static Geometry computeGeometry (const StageParams&, const GlobalStage&) noexcept;
    const Geometry& lastGeometry() const noexcept { return geo; }

private:
    struct Delay
    {
        std::vector<float> buf;
        int write = 0, mask = 0;
        void prepare (int minLen);
        void push (float x) noexcept { buf[(size_t) write] = x; write = (write + 1) & mask; }
        float read (float delaySamples) const noexcept;   // fractional, linear
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    };
    /** Spherical-head ear path: arrival delay + one-pole/one-zero head-shadow filter. */
    struct Ear
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        void set (double sr, float incidenceDeg) noexcept;
        float process (float x) noexcept { const float y = b0 * x + b1 * x1 - a1 * y1; x1 = x; y1 = y; return y; }
    };

    double sr = 48000.0;
    Geometry geo, target;
    // smoothed per-sample values
    float sGain = 1.0f, sDelay = 0.0f, sPanL = 0.707f, sPanR = 0.707f, sWidth = 1.0f, sSend = 0.5f, sAirCoef = 0.0f;
    std::array<float, 6> sTapDelay {}, sTapGain {}, sTapL {}, sTapR {};
    float lpL = 0.0f, lpR = 0.0f;
    Delay dL, dR, er;
    // headphone 3D: two point sources (the instrument's left and right) x two ears
    Delay hL, hR;
    std::array<Ear, 4> ears;
    float itdL = 0.0f, itdR = 0.0f, sItd[4] {};
    float lastAz = 1000.0f;
    bool first = true;
};

/** The shared room tail: 8-line feedback delay network with Hadamard mixing. */
class RoomTail
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void configure (const RoomPreset&, float sizeScale) noexcept;   // audio thread ok (no allocation)
    /** Processes the send bus in place into the wet tail (stereo). */
    void process (float* l, float* r, int numSamples) noexcept;

private:
    static constexpr int kLines = 8;
    struct Line { std::vector<float> buf; int write = 0; int mask = 0; float delay = 1000.0f; float gLow = 0.5f, gHigh = 0.5f; float lp = 0.0f; };
    struct Allpass { std::vector<float> buf; int pos = 0; int len = 100; float g = 0.6f; float process (float x) noexcept; };
    double sr = 48000.0;
    std::array<Line, kLines> lines;
    std::array<Allpass, 4> diff;
    std::vector<float> pre;
    int preWrite = 0, preLen = 1;
    float dampCoef = 0.3f, lfo = 0.0f, lfoInc = 0.0f, level = 1.0f;
    int configuredRoom = -1;
    float configuredSize = -1.0f;
    const RoomPreset* currentPreset = nullptr;
};
} // namespace hf
