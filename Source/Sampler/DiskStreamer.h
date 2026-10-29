#pragma once

#include "Sampler/LoadedInstrument.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hf
{
/**
    Streams the part of long samples that is not in memory.

    Each playing voice of a long sample owns one stream slot: a juce::AbstractFifo ring of
    kRingFrames stereo frames, filled by the disk thread and read by the audio thread
    (single producer / single consumer, lock-free). A note starts from the in-memory head, so
    the disk has kHeadFrames (~0.5 s) to deliver the rest.

    THREAD CONTRACT
      prepare() / release(): message thread, with the audio stopped.
      start() / stop() / ready() / read pointers: audio thread only. Never blocks or allocates.
      The disk thread (or serviceAll() when rendering offline) opens files and fills rings.
      If the disk is late the voice sees fewer frames than it needs and fades out (underrun
      counter), it never waits.

    Slot states: idle -> starting (audio set the zone) -> streaming (disk opened the file)
                 -> stopping (audio is done) -> idle (disk closed the file).
*/
class DiskStreamer final : private juce::Thread
{
public:
    static constexpr int kSlots = 256;
    static constexpr int kRingFrames = 32768;
    static constexpr int kChunk = 4096;

    DiskStreamer();
    ~DiskStreamer() override;

    /** Allocates every ring. With `ownThread` the disk thread runs; without it the caller
        drives serviceAll() (offline rendering). */
    void prepare (bool ownThread);
    void shutdown();

    // ---- audio thread --------------------------------------------------------------
    /** Starts streaming a zone from stream frame `firstFrame` (normally the end of its head).
        Returns the slot index, or -1 if every slot is busy. */
    int start (const LoadedZone*, const LoadedInstrument*, juce::int64 firstFrame) noexcept;
    void stop (int slot) noexcept;
    /** First stream frame held in the ring and how many frames are ready from it. */
    juce::int64 firstFrame (int slot) const noexcept;
    int numReady (int slot) const noexcept;
    /** Frame `streamFrame` (must be inside firstFrame .. firstFrame + numReady). */
    void frameAt (int slot, juce::int64 streamFrame, float& l, float& r) const noexcept;
    /** Snapshot of a slot for one audio block: frame f (first <= f < first + ready) is at
        ring index (readPos + (f - first)) % kRingFrames. */
    struct View
    {
        const float* l = nullptr;
        const float* r = nullptr;
        int readPos = 0, ready = 0;
        juce::int64 first = 0;
        bool endReached = false;    // the disk has written the last frame of the sample
        bool get (juce::int64 f, float& outL, float& outR) const noexcept
        {
            const juce::int64 o = f - first;
            if (o < 0 || o >= ready) return false;
            const int idx = (int) ((readPos + o) % kRingFrames);
            outL = l[idx]; outR = r[idx];
            return true;
        }
    };
    View view (int slot) const noexcept;
    /** Frees ring frames before `streamFrame` (the voice no longer needs them). */
    void consumeBefore (int slot, juce::int64 streamFrame) noexcept;

    /** Count of voices that ran out of streamed audio (audio thread increments). */
    std::atomic<int> underruns { 0 };
    std::atomic<int> busySlots { 0 };

    // ---- offline -------------------------------------------------------------------
    /** Opens pending streams and fills every ring completely (call between offline blocks). */
    void serviceAll();

    /** Maps a stream frame to the sample frame (loop wrap). */
    static juce::int64 sourceFrame (const Zone& z, juce::int64 streamFrame) noexcept
    {
        if (! z.hasLoop() || streamFrame < z.loopEnd) return streamFrame;
        const juce::int64 len = z.loopEnd - z.loopStart;
        return z.loopStart + (streamFrame - z.loopStart) % len;
    }

private:
    enum State { idle = 0, starting, streaming, stopping };

    struct Slot
    {
        std::atomic<int> state { idle };
        const LoadedZone* zone = nullptr;
        const LoadedInstrument* instrument = nullptr;
        juce::int64 startFrame = 0;          // written by audio before `starting`
        juce::int64 ringFirst = 0;           // stream frame at the ring's read position (audio side)
        std::atomic<juce::int64> nextWrite { 0 };   // next stream frame the disk writes
        bool endReached = false;             // disk: sample end written (one-shot)
        std::atomic<bool> endFlag { false };  // same, for the audio thread
        juce::AbstractFifo fifo { kRingFrames };
        juce::AudioBuffer<float> ring;
        std::unique_ptr<juce::AudioFormatReader> reader;   // disk thread only
    };

    void run() override;
    bool serviceSlot (Slot&, bool fillCompletely);
    void openSlot (Slot&);
    void closeSlot (Slot&);

    std::vector<std::unique_ptr<Slot>> slots;
    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> scratch;    // disk thread read buffer
    bool threaded = false;
};
} // namespace hf
