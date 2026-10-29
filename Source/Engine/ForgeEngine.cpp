#include "Engine/ForgeEngine.h"
#include <algorithm>
#include <cmath>

namespace hf
{
//==============================================================================
void PlaySnapshot::build (const Song& s)
{
    bpm = juce::jlimit (20.0, 300.0, s.bpm);
    lengthBeats = s.lengthBeats();
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& ev = events[(size_t) t];
        ev.clear();
        used[(size_t) t] = t < (int) s.tracks.size();
        if (! used[(size_t) t]) continue;
        const auto& tr = s.tracks[(size_t) t];
        ev.reserve (tr.notes.size() * 2);
        for (auto& n : tr.notes)
        {
            PlayEvent on;
            on.beat = juce::jmax (0.0, n.start);
            on.id = n.id;
            on.pitch = (juce::int16) n.pitch;
            on.velocity = (juce::uint8) juce::jlimit (1, 127, n.velocity);
            on.articulation = (juce::int8) juce::jlimit (-1, 63, n.articulation);
            on.glide = n.glide;
            on.on = true;
            PlayEvent off = on;
            off.on = false;
            off.beat = juce::jmax (on.beat + 1.0e-4, n.end());
            ev.push_back (on);
            ev.push_back (off);
        }
        // by time; at the same time note-offs go first (a re-struck key releases, then strikes)
        std::stable_sort (ev.begin(), ev.end(), [] (const PlayEvent& a, const PlayEvent& b)
                          { return a.beat < b.beat || (a.beat == b.beat && ! a.on && b.on); });
    }
    ++version;
}

//==============================================================================
bool ForgeEngine::LiveQueue::push (const LiveEvent& e) noexcept
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return false;
    data[(size_t) (n1 > 0 ? s1 : s2)] = e;
    fifo.finishedWrite (1);
    return true;
}

bool ForgeEngine::LiveQueue::pop (LiveEvent& e) noexcept
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return false;
    e = data[(size_t) (n1 > 0 ? s1 : s2)];
    fifo.finishedRead (1);
    return true;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ForgeEngine::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB");
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { Ids::bpm, 1 }, "Tempo", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 100.0f, AudioParameterFloatAttributes().withLabel ("BPM")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { Ids::master, 1 }, "Master", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f, dbAttr));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { Ids::room, 1 }, "Room", roomNames(), 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { Ids::roomSize, 1 }, "Room Size", NormalisableRange<float> (0.5f, 1.5f, 0.01f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { Ids::reverb, 1 }, "Room Level", NormalisableRange<float> (-40.0f, 6.0f, 0.1f), -3.0f, dbAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { Ids::mics, 1 }, "Mic Distance", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { Ids::headphones, 1 }, "Headphone 3D", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { Ids::click, 1 }, "Click", false));
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto n = "T" + String (t + 1) + " ";
        auto id = [t] (const char* w) { return ParameterID { Ids::track (t, w), 1 }; };
        l.add (std::make_unique<AudioParameterFloat> (id (kLevel), n + "Level", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.5f), 0.0f, dbAttr));
        l.add (std::make_unique<AudioParameterBool> (id (kMute), n + "Mute", false));
        l.add (std::make_unique<AudioParameterBool> (id (kSolo), n + "Solo", false));
        l.add (std::make_unique<AudioParameterFloat> (id (kX), n + "Seat Left/Right", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        l.add (std::make_unique<AudioParameterFloat> (id (kDepth), n + "Seat Front/Back", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
        l.add (std::make_unique<AudioParameterFloat> (id (kHeight), n + "Seat Height", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));
        l.add (std::make_unique<AudioParameterFloat> (id (kWidth), n + "Width", NormalisableRange<float> (0.0f, 1.5f, 0.01f), 1.0f));
        l.add (std::make_unique<AudioParameterFloat> (id (kRoomSend), n + "Room Send", NormalisableRange<float> (-30.0f, 12.0f, 0.1f), 0.0f, dbAttr));
        l.add (std::make_unique<AudioParameterInt> (id (kArt), n + "Articulation", 0, 15, 0));
        l.add (std::make_unique<AudioParameterBool> (id (kLegato), n + "Legato", false));
        l.add (std::make_unique<AudioParameterBool> (id (kMono), n + "One Note At A Time", false));
        l.add (std::make_unique<AudioParameterFloat> (id (kHuman), n + "Humanize", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        l.add (std::make_unique<AudioParameterFloat> (id (kVelSens), n + "Velocity Feel", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        l.add (std::make_unique<AudioParameterInt> (id (kTranspose), n + "Transpose", -24, 24, 0));
        l.add (std::make_unique<AudioParameterFloat> (id (kRelease), n + "Release", NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f));
    }
    return l;
}

ForgeEngine::ForgeEngine (bool offline)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()), offlineMode (offline)
{
    cacheParameters();
    for (auto& a : pendingInst) a.store (nullptr);
    for (auto& a : audioInst) a.store (nullptr);
    for (auto& t : liveIds) t.fill (0);
    song = &songBuffer.front();
    pendingRec.reserve (64);
}

ForgeEngine::~ForgeEngine()
{
    streamer.shutdown();
}

void ForgeEngine::cacheParameters()
{
    auto raw = [this] (const juce::String& id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    pBpm = raw (Ids::bpm); pMaster = raw (Ids::master); pRoom = raw (Ids::room); pRoomSize = raw (Ids::roomSize);
    pReverb = raw (Ids::reverb); pMics = raw (Ids::mics); pHp = raw (Ids::headphones); pClick = raw (Ids::click);
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto r = [&] (const char* w) { return raw (Ids::track (t, w)); };
        tp[(size_t) t] = { r (kLevel), r (kMute), r (kSolo), r (kX), r (kDepth), r (kHeight), r (kWidth), r (kRoomSend),
                           r (kArt), r (kLegato), r (kMono), r (kHuman), r (kVelSens), r (kTranspose), r (kRelease) };
    }
}

float ForgeEngine::param (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

void ForgeEngine::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

bool ForgeEngine::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ForgeEngine::getStateInformation (juce::MemoryBlock& mb)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, mb);
}

void ForgeEngine::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
void ForgeEngine::publishSong (const Song& s)
{
    auto& b = songBuffer.back();
    b.build (s);
    songBuffer.publish();
    setParam (Ids::bpm, (float) s.bpm);
}

void ForgeEngine::setTrackInstrument (int t, std::shared_ptr<LoadedInstrument> inst)
{
    if (t < 0 || t >= kMaxTracks) return;
    auto old = trackInst[(size_t) t];
    trackInst[(size_t) t] = inst;
    pendingInst[(size_t) t].store (inst.get(), std::memory_order_release);
    if (old != nullptr && old != inst) retired.push_back (std::move (old));
}

void ForgeEngine::collectGarbage()
{
    retired.erase (std::remove_if (retired.begin(), retired.end(), [this] (const std::shared_ptr<LoadedInstrument>& p)
    {
        if (p->users.load() != 0) return false;
        for (int t = 0; t < kMaxTracks; ++t)
            if (audioInst[(size_t) t].load() == p.get() || pendingInst[(size_t) t].load() == p.get()) return false;
        return true;
    }), retired.end());
}

void ForgeEngine::applyInstrumentDefaults (int t, const InstrumentDef& d)
{
    setParam (Ids::track (t, kX), d.seat.x);
    setParam (Ids::track (t, kDepth), d.seat.depth);
    setParam (Ids::track (t, kHeight), d.seat.height);
    setParam (Ids::track (t, kMono), d.mono ? 1.0f : 0.0f);
    setParam (Ids::track (t, kLegato), d.mono ? 1.0f : 0.0f);
    setParam (Ids::track (t, kArt), 0.0f);
    setParam (Ids::track (t, kTranspose), 0.0f);
}

//==============================================================================
void ForgeEngine::play() { stopRequest.store (false); playing.store (true); }
void ForgeEngine::stop() { stopRequest.store (true); }

void ForgeEngine::setPositionBeats (double b)
{
    b = juce::jmax (0.0, b);
    positionBeats.store (b);
    seekRequest.store (b);
}

void ForgeEngine::setLoop (bool on, double a, double b)
{
    if (b < a) std::swap (a, b);
    loopStart.store (juce::jmax (0.0, a));
    loopEnd.store (juce::jmax (a + 0.25, b));
    loopOn.store (on);
}

void ForgeEngine::uiNoteOn (int key, int velocity)
{
    LiveEvent e; e.type = evNoteOn; e.key = (juce::uint8) juce::jlimit (0, 127, key); e.value = (juce::uint8) juce::jlimit (1, 127, velocity);
    uiQueue.push (e);
}

void ForgeEngine::uiNoteOff (int key)
{
    LiveEvent e; e.type = evNoteOff; e.key = (juce::uint8) juce::jlimit (0, 127, key);
    uiQueue.push (e);
}

void ForgeEngine::liveMidi (const juce::MidiMessage& m)
{
    LiveEvent e;
    if (m.isNoteOn()) { e.type = evNoteOn; e.key = (juce::uint8) m.getNoteNumber(); e.value = m.getVelocity(); }
    else if (m.isNoteOff()) { e.type = evNoteOff; e.key = (juce::uint8) m.getNoteNumber(); }
    else if (m.isSustainPedalOn() || m.isSustainPedalOff()) { e.type = evPedal; e.value = (juce::uint8) m.getControllerValue(); }
    else if (m.isPitchWheel()) { e.type = evBend; e.value14 = (juce::int16) m.getPitchWheelValue(); }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.type = evAllOff; }
    else return;
    midiQueue.push (e);
}

void ForgeEngine::collectRecorded (std::vector<RecordedNote>& out)
{
    int s1, n1, s2, n2;
    const int ready = recFifo.getNumReady();
    if (ready <= 0) return;
    recFifo.prepareToRead (ready, s1, n1, s2, n2);
    auto handle = [&] (const RecEvent& e)
    {
        if (e.on) { pendingRec.push_back ({ e.track, e.key, e.velocity, e.beat }); return; }
        for (size_t i = 0; i < pendingRec.size(); ++i)
            if (pendingRec[i].track == e.track && pendingRec[i].key == e.key)
            {
                Note n;
                n.start = pendingRec[i].beat;
                n.length = juce::jmax (1.0 / 64.0, e.beat - pendingRec[i].beat);
                n.pitch = e.key;
                n.velocity = pendingRec[i].velocity;
                out.push_back ({ e.track, n });
                pendingRec.erase (pendingRec.begin() + (long) i);
                break;
            }
    };
    for (int i = 0; i < n1; ++i) handle (recData[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) handle (recData[(size_t) (s2 + i)]);
    recFifo.finishedRead (n1 + n2);
}

//==============================================================================
void ForgeEngine::prepareToPlay (double sr, int maxBlock)
{
    sampleRateHz = sr;
    streamer.prepare (! offlineMode);
    voices.prepare (sr, kChunk, streamer);
    for (auto& s : stages) s.prepare (sr, kChunk);
    roomTail.prepare (sr, kChunk);
    trackBuf.setSize (2 * kMaxTracks, kChunk);
    trackBuf.clear();
    mixBuf.setSize (2, kChunk);
    sendBuf.setSize (2, kChunk);
    for (int t = 0; t < kMaxTracks; ++t)
    {
        trackL[(size_t) t] = trackBuf.getWritePointer (2 * t);
        trackR[(size_t) t] = trackBuf.getWritePointer (2 * t + 1);
        trackGainSmoothed[(size_t) t] = -1.0f;
        trackState[(size_t) t].instrument = nullptr;
        audioInst[(size_t) t].store (nullptr);
    }
    loadMeasurer.reset (sr, juce::jmax (1, maxBlock));
    limGain = 1.0f;
    playBeats = positionBeats.load();
    setLatencySamples (0);
}

void ForgeEngine::releaseResources()
{
    streamer.shutdown();
}

void ForgeEngine::startNote (int t, const PlayEvent& ev, int offset, bool live) noexcept
{
    auto& ts = trackState[(size_t) t];
    const auto* inst = ts.instrument;
    if (inst == nullptr) return;
    const auto& d = inst->getDef();
    const auto& p = tp[(size_t) t];
    const float human = p.human->load (std::memory_order_relaxed);
    VoicePool::NoteOn n;
    n.track = t;
    const int trans = d.kind == InstrumentKind::pitched ? (int) p.trans->load (std::memory_order_relaxed) : 0;
    n.key = juce::jlimit (0, 127, (int) ev.pitch + trans);
    int vel = ev.velocity;
    if (human > 0.0f && ! live)
    {
        // deterministic per note, so a song plays the same way every time
        const juce::uint32 h = (ev.id * 2654435761u) >> 16;
        vel = juce::jlimit (1, 127, (int) std::lround (vel * (1.0f + human * 0.16f * ((float) (h & 255) / 127.5f - 1.0f))));
    }
    n.velocity = vel;
    n.articulation = ev.articulation >= 0 ? (int) ev.articulation : (int) p.art->load (std::memory_order_relaxed);
    n.noteId = ev.id;
    n.offset = offset;
    n.legato = p.legato->load (std::memory_order_relaxed) > 0.5f;
    n.mono = n.legato || p.mono->load (std::memory_order_relaxed) > 0.5f;
    n.velTrack = juce::jlimit (0.0f, 1.0f, d.velTrack * 2.0f * p.vel->load (std::memory_order_relaxed));
    n.glideSemitones = ev.glide;
    n.releaseScale = p.release->load (std::memory_order_relaxed);
    voices.noteOn (n, ts);
}

void ForgeEngine::handleLive (LiveQueue& q) noexcept
{
    LiveEvent e;
    while (q.pop (e)) handleLiveEvent (e);
}

void ForgeEngine::handleLiveEvent (const LiveEvent& e) noexcept
{
    {
        const int t = liveTrack.load (std::memory_order_relaxed);
        auto& ts = trackState[(size_t) t];
        switch (e.type)
        {
            case evNoteOn:
            {
                if (e.value == 0) { LiveEvent off = e; off.type = evNoteOff; handleLiveEvent (off); return; }
                // keyswitches: C0 .. (C0 + articulations - 1), below the instrument's playing range
                if (const auto* inst = ts.instrument)
                {
                    const int na = inst->numArticulations();
                    if (na > 1 && e.key >= 12 && e.key < 12 + na && e.key < inst->getDef().rangeLo
                        && inst->getDef().kind == InstrumentKind::pitched)
                    {
                        ts.keyswitchArticulation = e.key - 12;
                        return;
                    }
                }
                {
                    // a key already down (another track, or a doubled message) is released first
                    auto& slot = liveIds[(size_t) t][e.key];
                    if (slot != 0) voices.noteOff (t, slot, 0, ts);
                    slot = ++liveCounter | 0x80000000u;
                    PlayEvent ev;
                    ev.pitch = e.key; ev.velocity = e.value; ev.id = slot; ev.articulation = -1;
                    startNote (t, ev, 0, true);
                    if (recording.load (std::memory_order_relaxed) && playing.load (std::memory_order_relaxed))
                    {
                        int s1, n1, s2, n2;
                        recFifo.prepareToWrite (1, s1, n1, s2, n2);
                        if (n1 + n2 > 0) { recData[(size_t) (n1 > 0 ? s1 : s2)] = { (juce::int16) t, (juce::int16) e.key, (juce::int16) e.value, true, playBeats }; recFifo.finishedWrite (1); }
                    }
                }
                break;
            }
            case evNoteOff:
            {
                // the note may have started on another track before the live track changed
                for (int tt = 0; tt < kMaxTracks; ++tt)
                {
                    auto& slot = liveIds[(size_t) tt][e.key];
                    if (slot == 0) continue;
                    voices.noteOff (tt, slot, 0, trackState[(size_t) tt]);
                    slot = 0;
                    if (recording.load (std::memory_order_relaxed))
                    {
                        int s1, n1, s2, n2;
                        recFifo.prepareToWrite (1, s1, n1, s2, n2);
                        if (n1 + n2 > 0) { recData[(size_t) (n1 > 0 ? s1 : s2)] = { (juce::int16) tt, (juce::int16) e.key, 0, false, playBeats }; recFifo.finishedWrite (1); }
                    }
                }
                break;
            }
            case evPedal:
            {
                const bool down = e.value >= 64;
                if (ts.pedal && ! down) { ts.pedal = false; voices.pedalUp (t); }
                ts.pedal = down;
                break;
            }
            case evBend: ts.bendSemitones = (float) (e.value14 - 8192) / 8192.0f * 2.0f; break;
            case evAllOff: voices.allOff (-1, false); for (auto& tl : liveIds) tl.fill (0); break;
            default: break;
        }
    }
}

void ForgeEngine::runSequencer (double b0, double b1, int chunkOffset, int n) noexcept
{
    if (song == nullptr || b1 <= b0) return;
    const double bpm = juce::jmax (20.0, (double) pBpm->load (std::memory_order_relaxed));
    const double samplesPerBeat = sampleRateHz * 60.0 / bpm;
    const double maxDelayBeats = 0.025 * bpm / 60.0;   // humanize pushes notes up to 25 ms late
    for (int t = 0; t < kMaxTracks; ++t)
    {
        if (! song->used[(size_t) t]) continue;
        const auto& ev = song->events[(size_t) t];
        if (ev.empty()) continue;
        const float human = tp[(size_t) t].human->load (std::memory_order_relaxed);
        const double lookback = human > 0.0f ? maxDelayBeats : 0.0;
        auto it = std::lower_bound (ev.begin(), ev.end(), b0 - lookback - 1.0e-9,
                                    [] (const PlayEvent& e, double b) { return e.beat < b; });
        for (; it != ev.end() && it->beat < b1; ++it)
        {
            double te = it->beat;
            if (human > 0.0f)
            {
                const juce::uint32 h = (it->id * 2246822519u) >> 20;
                te += maxDelayBeats * human * (double) (h & 1023) / 1023.0;
            }
            if (te < b0 || te >= b1) continue;
            const int off = chunkOffset + juce::jlimit (0, n - 1, (int) ((te - b0) * samplesPerBeat));
            if (it->on) startNote (t, *it, off, false);
            else voices.noteOff (t, it->id, off, trackState[(size_t) t]);
        }
    }
}

void ForgeEngine::metronome (float* l, float* r, int n, double b0, double b1) noexcept
{
    const double spb = sampleRateHz * 60.0 / juce::jmax (20.0, (double) pBpm->load());
    const double firstBeat = std::ceil (b0 - 1.0e-9);
    int trig = -1;
    if (firstBeat < b1)
    {
        trig = (int) ((firstBeat - b0) * spb);
        clickFreq = ((long long) firstBeat % 4 == 0) ? 1600.0 : 1050.0;
    }
    const float decay = std::exp (-1.0f / (0.025f * (float) sampleRateHz));
    for (int i = 0; i < n; ++i)
    {
        if (i == trig) { clickEnv = 0.35f; clickPhase = 0.0; }
        if (clickEnv < 1.0e-4f) continue;
        const float s = (float) std::sin (clickPhase) * clickEnv;
        clickPhase += 2.0 * juce::MathConstants<double>::pi * clickFreq / sampleRateHz;
        clickEnv *= decay;
        l[i] += s; r[i] += s;
    }
}

void ForgeEngine::processChunk (float* outL, float* outR, int n) noexcept
{
    if (offlineMode) streamer.serviceAll();

    for (int t = 0; t < kMaxTracks; ++t)
    {
        juce::FloatVectorOperations::clear (trackL[(size_t) t], n);
        juce::FloatVectorOperations::clear (trackR[(size_t) t], n);
    }

    const bool isPlay = playing.load (std::memory_order_relaxed);
    const double bpm = juce::jmax (20.0, (double) pBpm->load (std::memory_order_relaxed));
    const double beatsPerSample = bpm / (60.0 * sampleRateHz);
    double b0 = playBeats;
    if (isPlay)
    {
        const double b1 = b0 + n * beatsPerSample;
        juce::ignoreUnused (b1);
        const bool loop = loopOn.load (std::memory_order_relaxed);
        const double ls = loopStart.load (std::memory_order_relaxed), le = loopEnd.load (std::memory_order_relaxed);
        if (loop && le > ls && b0 < le && b1 >= le)
        {
            const int n1 = juce::jlimit (0, n, (int) ((le - b0) / beatsPerSample));
            runSequencer (b0, le, 0, juce::jmax (1, n1));
            // notes still sounding at the loop end are released (the loop starts clean)
            for (int t = 0; t < kMaxTracks; ++t) voices.allOff (t, false);
            const double nb = ls + (n - n1) * beatsPerSample;
            if (n - n1 > 0) runSequencer (ls, nb, n1, n - n1);
            playBeats = nb;
        }
        else
        {
            if (loop && b0 >= le && le > ls) b0 = ls;
            const double e1 = b0 + n * beatsPerSample;
            runSequencer (b0, e1, 0, n);
            playBeats = e1;
        }
        // stop at the end of the song (plus a bar for the tails)
        if (! loop && song != nullptr && playBeats > song->lengthBeats + 4.0)
        {
            playing.store (false);
            voices.allOff (-1, false);
        }
        positionBeats.store (playBeats, std::memory_order_relaxed);
    }

    voices.render (trackL.data(), trackR.data(), kMaxTracks, n, trackState.data());

    // mixer + stage
    juce::FloatVectorOperations::clear (mixBuf.getWritePointer (0), n);
    juce::FloatVectorOperations::clear (mixBuf.getWritePointer (1), n);
    juce::FloatVectorOperations::clear (sendBuf.getWritePointer (0), n);
    juce::FloatVectorOperations::clear (sendBuf.getWritePointer (1), n);
    bool anySolo = false;
    for (int t = 0; t < kMaxTracks; ++t) anySolo |= tp[(size_t) t].solo->load (std::memory_order_relaxed) > 0.5f;

    GlobalStage gs;
    gs.room = (int) pRoom->load (std::memory_order_relaxed);
    gs.roomSize = pRoomSize->load (std::memory_order_relaxed);
    gs.micDistance = pMics->load (std::memory_order_relaxed);
    gs.headphone3D = pHp->load (std::memory_order_relaxed) > 0.5f;

    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto& p = tp[(size_t) t];
        const bool audible = p.mute->load (std::memory_order_relaxed) < 0.5f && (! anySolo || p.solo->load (std::memory_order_relaxed) > 0.5f);
        const float target = audible ? juce::Decibels::decibelsToGain (p.level->load (std::memory_order_relaxed), -59.9f) : 0.0f;
        float& g = trackGainSmoothed[(size_t) t];
        if (g < 0.0f) g = target;
        float* l = trackL[(size_t) t];
        float* r = trackR[(size_t) t];
        float pk0 = 0.0f, pk1 = 0.0f;
        const float step = (target - g) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            l[i] *= g; r[i] *= g;
            pk0 = juce::jmax (pk0, std::abs (l[i]));
            pk1 = juce::jmax (pk1, std::abs (r[i]));
        }
        g = target;
        auto& m0 = trackPeak[(size_t) t][0];
        auto& m1 = trackPeak[(size_t) t][1];
        m0.store (juce::jmax (m0.load (std::memory_order_relaxed), pk0), std::memory_order_relaxed);
        m1.store (juce::jmax (m1.load (std::memory_order_relaxed), pk1), std::memory_order_relaxed);

        StageParams sp;
        sp.x = p.x->load (std::memory_order_relaxed);
        sp.depth = p.depth->load (std::memory_order_relaxed);
        sp.height = p.height->load (std::memory_order_relaxed);
        sp.width = p.width->load (std::memory_order_relaxed);
        sp.roomSendDb = p.room->load (std::memory_order_relaxed);
        stages[(size_t) t].process (l, r, mixBuf.getWritePointer (0), mixBuf.getWritePointer (1),
                                    sendBuf.getWritePointer (0), sendBuf.getWritePointer (1), n, sp, gs);
    }

    const auto& presets = roomPresets();
    roomTail.configure (presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, gs.room)], gs.roomSize);
    roomTail.process (sendBuf.getWritePointer (0), sendBuf.getWritePointer (1), n);
    const float rv = juce::Decibels::decibelsToGain (pReverb->load (std::memory_order_relaxed), -39.9f);
    juce::FloatVectorOperations::addWithMultiply (mixBuf.getWritePointer (0), sendBuf.getReadPointer (0), rv, n);
    juce::FloatVectorOperations::addWithMultiply (mixBuf.getWritePointer (1), sendBuf.getReadPointer (1), rv, n);

    if (isPlay && pClick->load (std::memory_order_relaxed) > 0.5f)
        metronome (mixBuf.getWritePointer (0), mixBuf.getWritePointer (1), n, b0, b0 + n * beatsPerSample);

    // master + safety limiter (-0.3 dBFS, instant attack, 80 ms release)
    const float master = juce::Decibels::decibelsToGain (pMaster->load (std::memory_order_relaxed), -59.9f);
    const float ceiling = juce::Decibels::decibelsToGain (-0.3f);
    const float rel = 1.0f - std::exp (-1.0f / (0.08f * (float) sampleRateHz));
    const float* ml = mixBuf.getReadPointer (0);
    const float* mr = mixBuf.getReadPointer (1);
    float pk0 = 0.0f, pk1 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float l = ml[i] * master, r = mr[i] * master;
        const float peak = juce::jmax (std::abs (l), std::abs (r));
        limGain += rel * (1.0f - limGain);                       // release toward unity
        if (peak * limGain > ceiling) limGain = ceiling / peak;  // instant attack: never over the ceiling
        l *= limGain; r *= limGain;
        outL[i] = l; outR[i] = r;
        pk0 = juce::jmax (pk0, std::abs (l));
        pk1 = juce::jmax (pk1, std::abs (r));
    }
    masterPeak[0].store (juce::jmax (masterPeak[0].load (std::memory_order_relaxed), pk0), std::memory_order_relaxed);
    masterPeak[1].store (juce::jmax (masterPeak[1].load (std::memory_order_relaxed), pk1), std::memory_order_relaxed);
}

void ForgeEngine::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiIn)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    if (songBuffer.fetch()) song = &songBuffer.front();

    // instrument changes: the track's old voices fade out in 5 ms
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto* p = pendingInst[(size_t) t].load (std::memory_order_acquire);
        auto& ts = trackState[(size_t) t];
        if (p != ts.instrument)
        {
            voices.allOff (t, true);
            ts.instrument = p;
            ts.keyswitchArticulation = -1;
            ts.rrCounter.fill (0);
        }
        audioInst[(size_t) t].store (p, std::memory_order_release);
    }

    // transport requests
    const double seek = seekRequest.exchange (-1.0, std::memory_order_acq_rel);
    if (seek >= 0.0)
    {
        playBeats = seek;
        voices.allOff (-1, true);
    }
    if (stopRequest.exchange (false, std::memory_order_acq_rel))
    {
        playing.store (false);
        voices.allOff (-1, false);
    }
    const bool nowPlaying = playing.load (std::memory_order_relaxed);
    if (wasPlaying && ! nowPlaying) voices.allOff (-1, false);
    wasPlaying = nowPlaying;

    handleLive (uiQueue);
    handleLive (midiQueue);

    // MIDI arriving through the processor (hosted use) plays the live track too
    for (const auto meta : midiIn)
    {
        const auto m = meta.getMessage();
        LiveEvent e;
        if (m.isNoteOn()) { e.type = evNoteOn; e.key = (juce::uint8) m.getNoteNumber(); e.value = m.getVelocity(); }
        else if (m.isNoteOff()) { e.type = evNoteOff; e.key = (juce::uint8) m.getNoteNumber(); }
        else continue;
        handleLiveEvent (e);
    }
    midiIn.clear();

    for (int t = 0; t < kMaxTracks; ++t) { trackPeak[(size_t) t][0].store (0.0f, std::memory_order_relaxed); trackPeak[(size_t) t][1].store (0.0f, std::memory_order_relaxed); }
    masterPeak[0].store (0.0f, std::memory_order_relaxed);
    masterPeak[1].store (0.0f, std::memory_order_relaxed);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = juce::jmin (kChunk, numSamples - start);
        float tmpR[kChunk];
        processChunk (outL + start, outR != nullptr ? outR + start : tmpR, n);
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, numSamples);
}

void ForgeEngine::renderOffline (juce::AudioBuffer<float>& out, int start, int numSamples)
{
    jassert (offlineMode);
    juce::MidiBuffer none;
    juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, numSamples);
    processBlock (view, none);
}
} // namespace hf
