#pragma once

#include "Session/Song.h"
#include <array>
#include <atomic>

namespace hf
{
/** Notes as the audio thread plays them: on / off events per track, sorted by beat. */
struct PlayEvent
{
    double beat = 0.0;
    juce::uint32 id = 0;
    juce::int16 pitch = 60;
    juce::uint8 velocity = 100;
    juce::int8 articulation = -1;
    bool on = true;
    float glide = 0.0f;
};

struct PlaySnapshot
{
    double bpm = 100.0;
    double lengthBeats = 4.0;
    std::array<std::vector<PlayEvent>, kMaxTracks> events;
    std::array<bool, kMaxTracks> used {};
    juce::uint32 version = 0;

    /** Message thread: rebuilds from the song (reuses the vectors' memory). */
    void build (const Song&);
};

/**
    Lock-free triple buffer: the message thread writes the back copy and publishes it; the
    audio thread picks up the newest published copy at the start of a block. Neither side ever
    waits, and the audio thread never allocates or frees (all three copies live here).
*/
template <typename T>
class TripleBuffer
{
public:
    /** Writer: the copy to fill. */
    T& back() noexcept { return items[(size_t) backIndex]; }
    /** Writer: makes back() the newest copy. */
    void publish() noexcept
    {
        const int prev = middle.exchange (backIndex | kFresh, std::memory_order_acq_rel);
        backIndex = prev & kIndexMask;
    }
    /** Reader: switches to the newest copy if there is one. Returns true if it changed. */
    bool fetch() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kFresh) == 0) return false;
        const int prev = middle.exchange (frontIndex, std::memory_order_acq_rel);
        frontIndex = prev & kIndexMask;
        return true;
    }
    const T& front() const noexcept { return items[(size_t) frontIndex]; }

private:
    static constexpr int kFresh = 4, kIndexMask = 3;
    std::array<T, 3> items {};
    int backIndex = 0, frontIndex = 1;
    std::atomic<int> middle { 2 };
};
} // namespace hf
