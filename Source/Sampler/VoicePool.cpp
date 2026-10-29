#include "Sampler/VoicePool.h"
#include <cmath>

namespace hf
{
namespace
{
inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

constexpr float kMinus60dB = 0.001f;
constexpr float kSilence = 1.0e-4f;

float velocityGain (int velocity, float velTrack) noexcept
{
    const float v = (float) juce::jlimit (1, 127, velocity) / 127.0f;
    return (1.0f - velTrack) + velTrack * v * v;
}
} // namespace

void VoicePool::prepare (double sampleRate, int, DiskStreamer& s)
{
    sr = sampleRate;
    disk = &s;
    for (auto& v : voices) v = Voice {};
    active.store (0);
    clockSeconds = 0.0;
}

int VoicePool::voicesOnTrack (int track) const noexcept
{
    int n = 0;
    for (auto& v : voices) if (v.on && v.track == track) ++n;
    return n;
}

VoicePool::Voice* VoicePool::allocate() noexcept
{
    for (auto& v : voices)
        if (! v.on) return &v;
    // all busy: steal the quietest releasing voice, else the oldest
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.releasing && v.delay == 0 && (best == nullptr || v.env < best->env)) best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age) best = &v;
    steals.fetch_add (1, std::memory_order_relaxed);
    finish (*best);
    return best;
}

void VoicePool::finish (Voice& v) noexcept
{
    if (! v.on) return;
    if (v.slot >= 0 && disk != nullptr) disk->stop (v.slot);
    if (v.inst != nullptr) v.inst->users.fetch_sub (1, std::memory_order_relaxed);
    v.on = false;
    v.slot = -1;
    v.inst = nullptr;
    v.zone = nullptr;
    active.fetch_sub (1, std::memory_order_relaxed);
}

void VoicePool::startZone (Voice& v, const NoteOn& n, const LoadedZone& z, float gain, double startFrame, float attackSeconds) noexcept
{
    const auto& def = *z.def;
    v = Voice {};
    v.on = true;
    v.track = n.track;
    v.key = n.key;
    v.noteId = n.noteId;
    v.age = ++ageCounter;
    v.zone = &z;
    v.gain = gain;
    v.delay = juce::jmax (0, n.offset);
    v.pos = startFrame;
    v.startSeconds = clockSeconds + v.delay / sr;
    v.inst = nullptr;
    const double semis = def.keytrack ? (double) (n.key - def.root) : 0.0;
    v.baseStep = std::pow (2.0, (semis + def.tuneCents / 100.0) / 12.0);   // * library / device rate, set by caller
    v.glide = n.glideSemitones;
    v.glideCoef = std::exp (-1.0f / (float) (0.06 * sr));
    const int atk = juce::jmax (1, (int) (attackSeconds * sr));
    v.attackStep = 1.0f / (float) atk;
    v.env = atk <= 1 ? 1.0f : 0.0f;
    active.fetch_add (1, std::memory_order_relaxed);
}

void VoicePool::noteOn (const NoteOn& n, TrackPlayState& ts) noexcept
{
    const auto* inst = ts.instrument;
    if (inst == nullptr || n.key < 0 || n.key > 127) return;
    const auto& def = inst->getDef();
    const int artIndex = ts.keyswitchArticulation >= 0 ? ts.keyswitchArticulation : n.articulation;
    const auto& art = inst->articulation (artIndex);
    const auto& list = art.byKey[(size_t) n.key];
    if (list.empty()) return;

    const double rateRatio = inst->libraryRate() / sr;
    bool legatoTie = false;

    // mono: the track's sounding note gives way (tied when legato)
    if (n.mono)
        for (auto& v : voices)
            if (v.on && v.track == n.track && ! v.releasing && v.releaseDelay < 0 && ! v.isReleaseSample)
            {
                legatoTie = n.legato;
                v.relSeconds = legatoTie ? 0.06f : 0.04f;
                v.relCoef = std::pow (kMinus60dB, 1.0f / (float) (v.relSeconds * sr));
                v.releaseDelay = juce::jmax (0, n.offset);
                v.pedalHeld = false;
            }

    // the same key struck again: the old note gives way quickly (pitched instruments),
    // a closed / pedal hi-hat chokes the open one (drum kits)
    for (auto& v : voices)
    {
        if (! v.on || v.track != n.track || v.releasing || v.releaseDelay >= 0) continue;
        const bool sameKey = v.key == n.key && def.kind == InstrumentKind::pitched;
        const bool choke = def.kind == InstrumentKind::drums && v.key == 46 && (n.key == 42 || n.key == 44);
        if (sameKey || choke)
        {
            v.relSeconds = choke ? 0.03f : 0.08f;
            v.relCoef = std::pow (kMinus60dB, 1.0f / (float) (v.relSeconds * sr));
            v.releaseDelay = juce::jmax (0, n.offset);
            v.pedalHeld = false;
        }
    }

    const int pass = ts.rrCounter[(size_t) n.key]++;
    const float velGain = velocityGain (n.velocity, n.velTrack);
    for (int zi : list)
    {
        const auto& z = art.zones[(size_t) zi];
        const auto& zd = *z.def;
        if (n.velocity < zd.vlo || n.velocity > zd.vhi) continue;
        if (zd.rrCount > 1 && (pass % zd.rrCount) + 1 != zd.rr) continue;
        if (zd.pedal == 1 && ! ts.pedal) continue;
        if (zd.pedal == 0 && ts.pedal) continue;
        const float xf = zd.crossfadeGain (n.velocity);
        if (xf <= 0.0f) continue;

        auto* v = allocate();
        // legato ties skip the attack: start a little into the sample, fade in over 50 ms
        const double legatoStart = legatoTie ? juce::jmin (0.05 * inst->libraryRate(), (double) z.headFrames - 8.0) : 0.0;
        const float attack = legatoTie ? 0.05f : (def.kind == InstrumentKind::pitched ? 0.0015f : 0.0f);
        startZone (*v, n, z, z.gain * inst->instrumentGain() * velGain * xf, juce::jmax (0.0, legatoStart), attack);
        v->baseStep *= rateRatio;
        v->inst = inst;
        inst->users.fetch_add (1, std::memory_order_relaxed);
        const float rel = zd.release > 0.0f ? zd.release : art.def->release;
        v->relSeconds = juce::jmax (0.005f, rel * n.releaseScale);
        v->relCoef = std::pow (kMinus60dB, 1.0f / (float) (v->relSeconds * sr));
        if (! z.resident && disk != nullptr)
        {
            v->slot = disk->start (&z, inst, z.headFrames);
            // no free stream: the note plays its head and fades out at the end of it
        }
        v->art = artIndex;
        v->velocity = n.velocity;
    }
}

void VoicePool::triggerReleaseZones (const Voice& src, TrackPlayState& ts) noexcept
{
    const auto* inst = src.inst;
    if (inst == nullptr) return;
    const auto& art = inst->articulation (src.art);
    const auto& list = art.releaseByKey[(size_t) juce::jlimit (0, 127, src.key)];
    if (list.empty()) return;
    const int velocity = juce::jlimit (1, 127, src.velocity);
    const double held = juce::jmax (0.0, clockSeconds - src.startSeconds);
    const float decay = juce::Decibels::decibelsToGain ((float) (-2.0 * held));   // release noise fades the longer the key was held
    const double rateRatio = inst->libraryRate() / sr;
    for (int zi : list)
    {
        const auto& z = art.releaseZones[(size_t) zi];
        const auto& zd = *z.def;
        if (velocity < zd.vlo || velocity > zd.vhi) continue;
        if (zd.rrCount > 1 && ((ts.rrCounter[(size_t) src.key] - 1) % zd.rrCount) + 1 != zd.rr) continue;
        auto* v = allocate();
        NoteOn n;
        n.track = src.track; n.key = src.key; n.velocity = velocity; n.noteId = 0;
        startZone (*v, n, z, z.gain * inst->instrumentGain() * velocityGain (velocity, 0.5f) * decay, 0.0, 0.002f);
        v->baseStep *= rateRatio;
        v->inst = inst;
        v->isReleaseSample = true;
        inst->users.fetch_add (1, std::memory_order_relaxed);
        v->relSeconds = 0.3f;
        v->relCoef = std::pow (kMinus60dB, 1.0f / (float) (v->relSeconds * sr));
        if (! z.resident && disk != nullptr) v->slot = disk->start (&z, inst, z.headFrames);
    }
}

void VoicePool::noteOff (int track, juce::uint32 noteId, int offset, TrackPlayState& ts) noexcept
{
    bool releaseTriggered = false;
    for (auto& v : voices)
    {
        if (! v.on || v.track != track || v.noteId != noteId || v.releasing || v.releaseDelay >= 0 || v.isReleaseSample) continue;
        if (ts.pedal) { v.pedalHeld = true; continue; }
        v.releaseDelay = juce::jmax (0, offset);
        if (! releaseTriggered) { triggerReleaseZones (v, ts); releaseTriggered = true; }
    }
}

void VoicePool::pedalUp (int track) noexcept
{
    for (auto& v : voices)
        if (v.on && v.track == track && v.pedalHeld && ! v.releasing)
        {
            v.pedalHeld = false;
            v.releasing = true;
            v.releaseDelay = 0;
        }
}

void VoicePool::allOff (int track, bool kill) noexcept
{
    for (auto& v : voices)
    {
        if (! v.on || (track >= 0 && v.track != track)) continue;
        if (kill)
        {
            if (v.delay > 0) { finish (v); continue; }
            v.killStep = 1.0f / (float) juce::jmax (1.0, 0.005 * sr);
        }
        else if (! v.releasing)
        {
            v.releasing = true;
            v.releaseDelay = 0;
            v.pedalHeld = false;
        }
    }
}

void VoicePool::killInstrument (const LoadedInstrument* inst) noexcept
{
    for (auto& v : voices)
        if (v.on && v.inst == inst)
        {
            if (v.delay > 0) finish (v);
            else v.killStep = 1.0f / (float) juce::jmax (1.0, 0.005 * sr);
        }
}

//==============================================================================
bool VoicePool::renderVoice (Voice& v, float* outL, float* outR, int numSamples, float bendSemis) noexcept
{
    const auto& z = *v.zone;
    const auto& zd = *z.def;
    const float* hl = z.head.getReadPointer (0);
    const float* hr = z.head.getReadPointer (1);
    const int headFrames = z.headFrames;
    const bool loop = zd.hasLoop();
    const juce::int64 total = zd.frames;
    DiskStreamer::View view;
    if (v.slot >= 0) view = disk->view (v.slot);

    int k = 0;
    if (v.delay > 0)
    {
        k = juce::jmin (v.delay, numSamples);
        v.delay -= k;
        if (v.releaseDelay > 0) v.releaseDelay = juce::jmax (0, v.releaseDelay - k);
        if (k == numSamples) return true;
    }

    // pitch for this block (bend + glide are per block: a 10 ms step is inaudible on these)
    const double step = v.baseStep * std::pow (2.0, (double) (bendSemis + v.glide) / 12.0);
    if (v.glide != 0.0f)
    {
        v.glide *= std::pow (v.glideCoef, (float) (numSamples - k));
        if (std::abs (v.glide) < 0.002f) v.glide = 0.0f;
    }

    auto fetch = [&] (juce::int64 f, float& l, float& r) noexcept -> bool
    {
        if (f < 0) { l = r = 0.0f; return true; }
        if (z.resident)
        {
            if (f >= headFrames + LoadedInstrument::kGuard) { l = r = 0.0f; return true; }
            l = hl[f]; r = hr[f];
            return true;
        }
        if (f < headFrames) { l = hl[f]; r = hr[f]; return true; }
        if (! loop && f >= total) { l = r = 0.0f; return true; }
        if (v.slot < 0) return false;
        return view.get (f, l, r);
    };

    bool alive = true;
    for (; k < numSamples; ++k)
    {
        if (v.releaseDelay == 0) { v.releaseDelay = -1; v.releasing = true; }
        else if (v.releaseDelay > 0) --v.releaseDelay;

        // resident loop: wrap the read position (the guard frames continue the loop)
        if (z.resident && loop && v.pos >= zd.loopEnd) v.pos -= (double) (zd.loopEnd - zd.loopStart);
        const juce::int64 i = (juce::int64) v.pos;
        if (! loop && i >= (z.resident ? (juce::int64) headFrames : total)) { alive = false; break; }

        float a0, a1, a2, a3, b0, b1, b2, b3;
        const bool ok = fetch (i - 1, a0, b0) && fetch (i, a1, b1) && fetch (i + 1, a2, b2) && fetch (i + 2, a3, b3);
        if (! ok)
        {
            // the disk is late (or no stream slot was free): fade out from the last sample
            disk->underruns.fetch_add (v.slot >= 0 ? 1 : 0, std::memory_order_relaxed);
            const int fade = juce::jmin (64, numSamples - k);
            for (int j = 0; j < fade; ++j)
            {
                const float g = v.env * v.gain * (1.0f - (float) (j + 1) / (float) fade);
                outL[k + j] += v.lastL * g;
                outR[k + j] += v.lastR * g;
            }
            alive = false;
            break;
        }
        const float t = (float) (v.pos - (double) i);
        const float sl = hermite (a0, a1, a2, a3, t);
        const float sr2 = hermite (b0, b1, b2, b3, t);

        // envelope
        if (v.killStep > 0.0f)
        {
            v.env -= v.killStep;
            if (v.env <= 0.0f) { alive = false; break; }
        }
        else if (v.releasing)
        {
            v.env *= v.relCoef;
            if (v.env < kSilence) { alive = false; break; }
        }
        else if (v.env < 1.0f)
            v.env = juce::jmin (1.0f, v.env + v.attackStep);

        const float g = v.env * v.gain;
        outL[k] += sl * g;
        outR[k] += sr2 * g;
        v.lastL = sl; v.lastR = sr2;
        v.pos += step;
    }

    if (alive && v.slot >= 0)
        disk->consumeBefore (v.slot, (juce::int64) v.pos - 2);
    return alive;
}

void VoicePool::render (float* const* trackL, float* const* trackR, int numTracks, int numSamples, const TrackPlayState* states) noexcept
{
    for (auto& v : voices)
    {
        if (! v.on) continue;
        if (v.track < 0 || v.track >= numTracks) { finish (v); continue; }
        const bool keep = renderVoice (v, trackL[v.track], trackR[v.track], numSamples, states[v.track].bendSemitones);
        if (! keep) finish (v);
    }
    clockSeconds += numSamples / sr;
}
} // namespace hf
