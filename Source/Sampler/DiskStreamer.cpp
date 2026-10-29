#include "Sampler/DiskStreamer.h"

namespace hf
{
DiskStreamer::DiskStreamer() : juce::Thread ("HEM disk streamer")
{
    formats.registerBasicFormats();
}

DiskStreamer::~DiskStreamer() { shutdown(); }

void DiskStreamer::prepare (bool ownThread)
{
    shutdown();
    if (slots.empty())
    {
        slots.reserve (kSlots);
        for (int i = 0; i < kSlots; ++i)
        {
            auto s = std::make_unique<Slot>();
            s->ring.setSize (2, kRingFrames);
            s->ring.clear();
            slots.push_back (std::move (s));
        }
    }
    scratch.setSize (2, kChunk);
    threaded = ownThread;
    if (threaded) startThread (juce::Thread::Priority::high);
}

void DiskStreamer::shutdown()
{
    if (isThreadRunning()) stopThread (4000);
    for (auto& s : slots)
        if (s->state.load() != idle) closeSlot (*s);
    busySlots.store (0);
}

int DiskStreamer::start (const LoadedZone* z, const LoadedInstrument* inst, juce::int64 first) noexcept
{
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto& s = *slots[(size_t) i];
        int expected = idle;
        // claim with a CAS so a slot is never handed out twice; the disk thread ignores idle slots
        if (s.state.load (std::memory_order_acquire) == idle && s.state.compare_exchange_strong (expected, stopping + 1))
        {
            s.zone = z;
            s.instrument = inst;
            s.startFrame = first;
            s.ringFirst = first;
            s.fifo.reset();
            inst->users.fetch_add (1, std::memory_order_relaxed);
            busySlots.fetch_add (1, std::memory_order_relaxed);
            s.state.store (starting, std::memory_order_release);
            return i;
        }
    }
    return -1;
}

void DiskStreamer::stop (int slot) noexcept
{
    if (slot < 0 || slot >= (int) slots.size()) return;
    auto& s = *slots[(size_t) slot];
    const int st = s.state.load (std::memory_order_acquire);
    if (st == starting || st == streaming)
        s.state.store (stopping, std::memory_order_release);
}

juce::int64 DiskStreamer::firstFrame (int slot) const noexcept { return slots[(size_t) slot]->ringFirst; }

int DiskStreamer::numReady (int slot) const noexcept
{
    auto& s = *slots[(size_t) slot];
    return s.state.load (std::memory_order_acquire) == streaming ? s.fifo.getNumReady() : 0;
}

void DiskStreamer::frameAt (int slot, juce::int64 f, float& l, float& r) const noexcept
{
    auto& s = *slots[(size_t) slot];
    int s1, n1, s2, n2;
    s.fifo.prepareToRead (1, s1, n1, s2, n2);
    const int idx = (int) ((s1 + (f - s.ringFirst)) % kRingFrames);
    l = s.ring.getSample (0, idx);
    r = s.ring.getSample (1, idx);
}

DiskStreamer::View DiskStreamer::view (int slot) const noexcept
{
    View v;
    auto& s = *slots[(size_t) slot];
    v.l = s.ring.getReadPointer (0);
    v.r = s.ring.getReadPointer (1);
    v.first = s.ringFirst;
    if (s.state.load (std::memory_order_acquire) != streaming) return v;
    int s1, n1, s2, n2;
    v.ready = s.fifo.getNumReady();
    s.fifo.prepareToRead (v.ready, s1, n1, s2, n2);
    v.readPos = s1;
    v.endReached = s.endFlag.load (std::memory_order_acquire);
    return v;
}

void DiskStreamer::consumeBefore (int slot, juce::int64 f) noexcept
{
    auto& s = *slots[(size_t) slot];
    const juce::int64 n = f - s.ringFirst;
    if (n <= 0) return;
    const int ready = s.fifo.getNumReady();
    const int k = (int) juce::jmin ((juce::int64) ready, n);
    if (k > 0)
    {
        s.fifo.finishedRead (k);
        s.ringFirst += k;
    }
}

//==============================================================================
void DiskStreamer::openSlot (Slot& s)
{
    s.reader.reset (formats.createReaderFor (s.zone->file));
    s.nextWrite.store (s.startFrame);
    s.endReached = s.reader == nullptr;
    s.endFlag.store (s.endReached, std::memory_order_release);
    s.state.store (streaming, std::memory_order_release);
}

void DiskStreamer::closeSlot (Slot& s)
{
    s.reader.reset();
    if (s.instrument != nullptr) s.instrument->users.fetch_sub (1, std::memory_order_relaxed);
    s.instrument = nullptr;
    s.zone = nullptr;
    busySlots.fetch_sub (1, std::memory_order_relaxed);
    s.state.store (idle, std::memory_order_release);
}

bool DiskStreamer::serviceSlot (Slot& s, bool fillCompletely)
{
    const int st = s.state.load (std::memory_order_acquire);
    if (st == stopping) { closeSlot (s); return true; }
    if (st == starting) { openSlot (s); return true; }
    if (st != streaming || s.endReached || s.reader == nullptr) return false;

    const auto& z = *s.zone->def;
    const juce::int64 total = juce::jmin ((juce::int64) z.frames, s.reader->lengthInSamples);
    bool did = false;
    for (;;)
    {
        const int free = s.fifo.getFreeSpace();
        if (free <= 0 || (! fillCompletely && free < kChunk && did)) break;
        if (! fillCompletely && free < kChunk / 2) break;
        const juce::int64 w = s.nextWrite.load (std::memory_order_relaxed);
        const juce::int64 src = sourceFrame (z, w);
        juce::int64 seg = juce::jmin ((juce::int64) free, (juce::int64) kChunk);
        if (z.hasLoop()) seg = juce::jmin (seg, (juce::int64) z.loopEnd - src);
        else seg = juce::jmin (seg, total - src);
        if (seg <= 0) { s.endReached = true; s.endFlag.store (true, std::memory_order_release); break; }
        const int n = (int) seg;
        scratch.clear (0, n);
        s.reader->read (&scratch, 0, n, src, true, true);
        if (s.reader->numChannels == 1) scratch.copyFrom (1, 0, scratch, 0, 0, n);
        int s1, n1, s2, n2;
        s.fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (n1 > 0) s.ring.copyFrom (ch, s1, scratch, ch, 0, n1);
            if (n2 > 0) s.ring.copyFrom (ch, s2, scratch, ch, n1, n2);
        }
        s.fifo.finishedWrite (n1 + n2);
        s.nextWrite.store (w + n1 + n2, std::memory_order_relaxed);
        did = true;
        if (s.state.load (std::memory_order_acquire) != streaming) break;
    }
    return did;
}

void DiskStreamer::run()
{
    while (! threadShouldExit())
    {
        bool any = false;
        for (auto& s : slots)
            any |= serviceSlot (*s, false);
        if (! any) wait (2);
    }
}

void DiskStreamer::serviceAll()
{
    for (int pass = 0; pass < 2; ++pass)   // pass 1 opens new streams, pass 2 fills them
        for (auto& s : slots)
            serviceSlot (*s, true);
}
} // namespace hf
