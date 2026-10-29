#include "Band/BandComposer.h"
#include <algorithm>
#include <cmath>

namespace pg::band
{
namespace
{
using I = Instrument;

//==============================================================================
// Song form and chords
//==============================================================================
struct FormEntry { Section kind; int bars; float energy; };

std::vector<FormEntry> songForm (Genre g, bool full)
{
    using S = Section;
    if (g == Genre::blues)
        return full ? std::vector<FormEntry> { { S::intro, 4, 0.45f }, { S::verse, 12, 0.6f }, { S::verse, 12, 0.65f },
                                               { S::solo, 12, 0.85f }, { S::verse, 12, 0.7f }, { S::outro, 4, 0.45f } }
                    : std::vector<FormEntry> { { S::intro, 4, 0.45f }, { S::verse, 12, 0.65f }, { S::outro, 4, 0.45f } };
    if (! full)
        return { { S::intro, 4, 0.4f }, { S::verse, 8, 0.6f }, { S::chorus, 8, 0.9f }, { S::outro, 4, 0.45f } };
    if (g == Genre::bluegrass)
        return { { S::intro, 8, 0.7f }, { S::verse, 8, 0.6f }, { S::chorus, 8, 0.85f }, { S::solo, 8, 0.85f },
                 { S::verse, 8, 0.6f }, { S::chorus, 8, 0.9f }, { S::solo, 8, 0.9f }, { S::outro, 4, 0.6f } };
    return { { S::intro, 4, 0.4f }, { S::verse, 8, 0.55f }, { S::chorus, 8, 0.85f }, { S::verse, 8, 0.6f },
             { S::chorus, 8, 0.9f }, { S::bridge, 8, 0.5f }, { S::chorus, 8, 1.0f }, { S::outro, 4, 0.4f } };
}

/** Chord degrees, one per half bar, for `bars` bars. */
std::vector<int> progression (Genre g, bool minor, Section s, int bars, Rng& rng)
{
    // bar-level patterns (scale degrees, 0 = I); each entry lasts one bar unless noted
    std::vector<int> barChords;
    auto pick = [&rng] (const std::vector<std::vector<int>>& options) { return options[(size_t) rng.below ((int) options.size())]; };

    if (g == Genre::blues)
    {
        static const std::vector<int> twelve { 0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4 };
        for (int b = 0; b < bars; ++b) barChords.push_back (bars == 4 ? twelve[(size_t) (8 + b)] : twelve[(size_t) (b % 12)]);
    }
    else if (minor)
    {
        std::vector<int> loop = s == Section::chorus ? pick ({ { 0, 5, 2, 6 }, { 5, 6, 0, 0 }, { 0, 3, 5, 4 } })
                              : s == Section::bridge ? pick ({ { 5, 5, 6, 6 }, { 3, 3, 4, 4 } })
                              : pick ({ { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 0, 5, 6 } });
        const bool slow = g == Genre::cinematic;
        for (int b = 0; b < bars; ++b) barChords.push_back (loop[(size_t) ((slow ? b / 2 : b) % (int) loop.size())]);
    }
    else
    {
        std::vector<int> eight;
        switch (s)
        {
            case Section::chorus: eight = pick ({ { 3, 3, 0, 0, 4, 4, 0, 0 }, { 3, 0, 4, 5, 3, 0, 4, 0 }, { 0, 4, 5, 3, 0, 4, 3, 0 } }); break;
            case Section::bridge: eight = pick ({ { 5, 5, 3, 3, 0, 0, 4, 4 }, { 1, 1, 4, 4, 5, 5, 4, 4 } }); break;
            case Section::intro:
            case Section::outro:  eight = { 0, 3, 4, 0, 0, 3, 4, 0 }; break;
            case Section::verse:
            case Section::solo:
            default:
                eight = g == Genre::folk ? pick ({ { 0, 4, 5, 3, 0, 4, 3, 0 }, { 0, 0, 3, 0, 0, 4, 0, 0 } })
                      : g == Genre::gospel ? pick ({ { 0, 3, 0, 4, 0, 3, 4, 0 }, { 0, 5, 1, 4, 0, 5, 1, 4 } })
                      : pick ({ { 0, 0, 3, 0, 0, 0, 4, 4 }, { 0, 0, 3, 3, 0, 4, 0, 0 }, { 0, 3, 0, 4, 0, 3, 4, 0 } });
                break;
        }
        for (int b = 0; b < bars; ++b) barChords.push_back (eight[(size_t) (b % 8)]);
    }

    std::vector<int> halves;
    for (size_t b = 0; b < barChords.size(); ++b)
    {
        halves.push_back (barChords[b]);
        // gospel walks through a passing chord on the second half of some bars
        if (g == Genre::gospel && ! minor && b + 1 < barChords.size() && barChords[b] == barChords[b + 1] && rng.chance (0.4))
            halves.push_back (5);
        else
            halves.push_back (barChords[b]);
    }
    // the last half bar of a section leads home: V before I where it fits
    if (! minor && g != Genre::blues && halves.size() >= 2 && s != Section::outro && halves[halves.size() - 2] == 0)
        halves.back() = 4;
    return halves;
}

//==============================================================================
// Writing helpers
//==============================================================================
struct Writer
{
    const Song& song;
    Rng rng;
    double humanize = 0.01;   // beats

    int bpb() const { return song.beatsPerBar; }
    int key() const { return song.request.keyRoot; }
    bool minor() const { return song.request.minor; }
    bool blues() const { return song.request.genre == Genre::blues; }

    const SectionPlan* sectionAtBar (int bar) const
    {
        for (auto& s : song.sections) if (bar >= s.startBar && bar < s.startBar + s.bars) return &s;
        return nullptr;
    }

    int degreeAt (double beat) const
    {
        const int bar = (int) std::floor (beat / bpb());
        const auto* s = sectionAtBar (bar);
        if (s == nullptr || s->chordRoots.empty()) return 0;
        const double inBar = beat - bar * bpb();
        const int idx = (bar - s->startBar) * 2 + (inBar >= bpb() * 0.5 ? 1 : 0);
        return s->chordRoots[(size_t) juce::jlimit (0, (int) s->chordRoots.size() - 1, idx)];
    }

    /** Pitch classes of the chord at `beat` (blues: dominant sevenths). */
    std::vector<int> chordAt (double beat) const
    {
        const int d = degreeAt (beat);
        if (blues())
        {
            const int r = theory::degreePitch (key(), false, d, 4) % 12;
            return { r, (r + 4) % 12, (r + 7) % 12, (r + 10) % 12 };
        }
        const bool seventh = song.request.genre == Genre::gospel;
        return theory::chordPitchClasses (key(), minor(), d, seventh);
    }

    int rootPc (double beat) const { return chordAt (beat)[0]; }

    /** Swing the off-beat 8th notes. */
    double swung (double beat) const
    {
        const double frac = beat - std::floor (beat);
        if (std::abs (frac - 0.5) < 1.0e-6) return beat + song.swing * (1.0 / 6.0);
        return beat;
    }

    void add (std::vector<Note>& out, double start, double length, int pitch, float vel, int articulation = art::normal, bool swing = true)
    {
        Note n;
        n.start = juce::jmax (0.0, (swing ? swung (start) : start) + rng.range (-humanize, humanize));
        n.length = juce::jmax (0.02, length);
        n.pitch = juce::jlimit (0, 127, pitch);
        n.velocity = juce::jlimit (0.05f, 1.0f, vel * (float) rng.range (0.92, 1.06));
        n.articulation = articulation;
        out.push_back (n);
    }

    /** Pentatonic pitches (plus the blue note for blues) between lo and hi. */
    std::vector<int> pentatonic (int lo, int hi) const
    {
        static const int majP[5] = { 0, 2, 4, 7, 9 };
        static const int minP[5] = { 0, 3, 5, 7, 10 };
        const int* p = (minor() || blues()) ? minP : majP;
        std::vector<int> out;
        for (int m = lo; m <= hi; ++m)
        {
            const int rel = ((m - key()) % 12 + 12) % 12;
            bool ok = false;
            for (int i = 0; i < 5; ++i) ok |= rel == p[i];
            if (blues() && rel == 6) ok = true;
            if (ok) out.push_back (m);
        }
        return out;
    }

    static int nearestIn (const std::vector<int>& set, int pitch)
    {
        int best = set.empty() ? pitch : set[0];
        for (int p : set) if (std::abs (p - pitch) < std::abs (best - pitch)) best = p;
        return best;
    }

    int nearestChordTone (double beat, int pitch, int lo, int hi) const
    {
        const auto pcs = chordAt (beat);
        int best = pitch, dist = 99;
        for (int m = lo; m <= hi; ++m)
            for (int pc : pcs)
                if (m % 12 == pc && std::abs (m - pitch) < dist) { dist = std::abs (m - pitch); best = m; }
        return best;
    }

    /** Guitar-style voicing: bass root then stacked chord tones, `count` notes from about E2. */
    std::vector<int> voicing (double beat, int count, int lowest = 40, int highest = 67) const
    {
        const auto pcs = chordAt (beat);
        const int root = pcs[0], third = pcs[1], fifth = pcs[2];
        const int order[6] = { root, fifth, root, third, fifth, root };
        std::vector<int> v;
        int last = lowest - 1;
        for (int i = 0; i < 6 && (int) v.size() < count; ++i)
        {
            int p = theory::atOrAbove (order[i], last + 1);
            if (p > highest) break;
            v.push_back (p);
            last = p;
        }
        return v;
    }
};

bool isDrums (I i) { return i == I::drums || i == I::trapDrums; }

/** Which instrument takes solos and fills: the first of these that is in the band. */
I pickLead (const std::vector<I>& band)
{
    for (I c : { I::fiddle, I::harmonica, I::banjo, I::electricGuitar, I::piano, I::mandolin, I::strings })
        if (std::find (band.begin(), band.end(), c) != band.end()) return c;
    return I::count;
}

//==============================================================================
// Drums
//==============================================================================
void writeDrums (Writer& w, std::vector<Note>& out)
{
    const Genre g = w.song.request.genre;
    w.humanize = 0.006;
    for (auto& s : w.song.sections)
    {
        const bool intro = s.kind == Section::intro || s.kind == Section::outro;
        if (intro && g != Genre::blues && g != Genre::cinematic && s.kind == Section::intro && s.bars <= 4) continue;
        for (int b = 0; b < s.bars; ++b)
        {
            const int bar = s.startBar + b;
            const double t = bar * w.bpb();
            const bool lastBar = b == s.bars - 1;
            const bool firstBar = b == 0;
            const float e = s.energy;
            const bool rideMode = s.kind == Section::chorus || s.kind == Section::solo;

            if (firstBar && (s.kind == Section::chorus || s.kind == Section::solo) && g != Genre::folk)
                w.add (out, t, 2.0, drum::crash, 0.85f);

            if (g == Genre::cinematic)
            {
                w.add (out, t, 1.0, drum::kick, 0.9f);
                w.add (out, t, 1.0, drum::tomLow, 0.75f * e + 0.2f);
                if (e > 0.7f) { w.add (out, t + 2, 1.0, drum::snare, 0.8f); w.add (out, t + 2, 1.0, drum::tomMid, 0.6f); }
                if (e > 0.8f) for (int k = 0; k < 4; ++k) w.add (out, t + k + 0.5, 0.2, drum::tomHigh, 0.35f);
                if (lastBar) for (int k = 0; k < 8; ++k) w.add (out, t + 2 + k * 0.25, 0.2, k < 4 ? drum::tomMid : drum::tomLow, 0.5f + 0.06f * k);
                continue;
            }

            // kick
            if (g == Genre::gospel) { w.add (out, t, 0.5, drum::kick, 0.9f); w.add (out, t + 1.5, 0.5, drum::kick, 0.7f); w.add (out, t + 2, 0.5, drum::kick, 0.85f); }
            else if (g == Genre::folk) { w.add (out, t, 0.5, drum::kick, 0.7f); if (e > 0.7f) w.add (out, t + 2, 0.5, drum::kick, 0.6f); }
            else { w.add (out, t, 0.5, drum::kick, 0.9f); w.add (out, t + 2, 0.5, drum::kick, 0.85f); if (e > 0.8f && w.rng.chance (0.5)) w.add (out, t + 2.5, 0.5, drum::kick, 0.6f); }

            // backbeat
            const int back = g == Genre::folk || s.kind == Section::bridge ? drum::rim : (g == Genre::outlawCountry && s.kind == Section::verse ? drum::brush : drum::snare);
            if (! (lastBar && ! intro))
            {
                w.add (out, t + 1, 0.5, back, 0.85f);
                w.add (out, t + 3, 0.5, back, 0.9f);
            }
            else
            {
                w.add (out, t + 1, 0.5, back, 0.85f);
                // fill on beats 3-4
                const int fill[8] = { drum::snare, drum::snare, drum::tomHigh, drum::tomHigh, drum::tomMid, drum::tomMid, drum::tomLow, drum::tomLow };
                for (int k = 0; k < 8; ++k) w.add (out, t + 2 + k * 0.25, 0.2, fill[k], 0.55f + 0.05f * k, art::normal, false);
            }

            // country train beat: soft brushed 8ths/16ths on the snare between the backbeats
            if (g == Genre::outlawCountry && s.kind == Section::verse)
                for (int k = 0; k < 8; ++k)
                    if (k != 2 && k != 6) w.add (out, t + k * 0.5, 0.25, drum::brush, k % 2 == 0 ? 0.35f : 0.25f);

            // time keeping
            if (g == Genre::folk)
                for (int k = 0; k < 8; ++k) w.add (out, t + k * 0.5, 0.25, drum::shaker, k % 2 == 0 ? 0.5f : 0.35f);
            else
                for (int k = 0; k < 8; ++k)
                {
                    const int sound = rideMode ? drum::ride : drum::hatClosed;
                    const float v = (k % 2 == 0 ? 0.6f : 0.4f) * (0.7f + 0.3f * e);
                    if (! rideMode && k == 7 && e > 0.6f && w.rng.chance (0.3)) w.add (out, t + k * 0.5, 0.5, drum::hatOpen, v);
                    else w.add (out, t + k * 0.5, 0.25, sound, v);
                }
            if (g == Genre::gospel && rideMode)
                for (int k = 1; k < 4; k += 2) w.add (out, t + k, 0.3, drum::tamb, 0.45f);
        }
    }
}

void writeTrapDrums (Writer& w, std::vector<Note>& out, std::vector<double>& kickTimes)
{
    w.humanize = 0.002;
    static const std::vector<std::vector<double>> kickPats {
        { 0.0, 0.75, 2.5 }, { 0.0, 1.75, 2.25, 3.5 }, { 0.0, 0.5, 2.75 }, { 0.0, 2.0, 2.75, 3.25 }
    };
    const auto& kp = kickPats[(size_t) w.rng.below ((int) kickPats.size())];
    const auto& kp2 = kickPats[(size_t) w.rng.below ((int) kickPats.size())];
    for (auto& s : w.song.sections)
    {
        for (int b = 0; b < s.bars; ++b)
        {
            const int bar = s.startBar + b;
            const double t = bar * w.bpb();
            const bool intro = s.kind == Section::intro || s.kind == Section::outro;
            const bool sparse = intro || s.kind == Section::bridge;

            if (! intro)
                for (double k : (b % 2 == 0 ? kp : kp2)) { w.add (out, t + k, 0.5, drum::kick, 0.95f, art::normal, false); kickTimes.push_back (out.back().start); }
            if (! intro) { w.add (out, t + 2, 0.5, drum::clap, 0.9f, art::normal, false); w.add (out, t + 2, 0.5, drum::snare, 0.7f, art::normal, false); }

            // hats: 8ths with rolls
            for (int beat = 0; beat < 4; ++beat)
            {
                const int roll = sparse ? 0 : w.rng.below (8);   // 0-4 plain, 5 16ths, 6 32nd triplets, 7 16th triplets
                const double b0 = t + beat;
                if (roll == 5) for (int k = 0; k < 4; ++k) w.add (out, b0 + k * 0.25, 0.1, drum::hatClosed, 0.45f + 0.1f * (k % 2), art::normal, false);
                else if (roll == 6) for (int k = 0; k < 6; ++k) w.add (out, b0 + k / 6.0, 0.08, drum::hatClosed, 0.3f + 0.08f * k, art::normal, false);
                else if (roll == 7) for (int k = 0; k < 3; ++k) w.add (out, b0 + k / 3.0, 0.1, drum::hatClosed, 0.5f, art::normal, false);
                else { w.add (out, b0, 0.1, drum::hatClosed, 0.6f, art::normal, false); w.add (out, b0 + 0.5, 0.1, drum::hatClosed, 0.42f, art::normal, false); }
            }
            if (! sparse && b % 4 == 3) w.add (out, t + 3.5, 0.5, drum::hatOpen, 0.5f, art::normal, false);
            if (b == 0 && s.kind == Section::chorus) w.add (out, t, 2.0, drum::crash, 0.7f, art::normal, false);
        }
    }
}

//==============================================================================
// Bass
//==============================================================================
void writeBass (Writer& w, I inst, std::vector<Note>& out, const std::vector<double>& kicks)
{
    const Genre g = w.song.request.genre;
    w.humanize = 0.008;
    const int low = inst == I::bass808 ? 31 : 28;   // G1 / E1
    auto bassNote = [&] (double beat, int pcOffset = 0) { return theory::atOrAbove ((w.rootPc (beat) + pcOffset) % 12, low + 4); };

    if (inst == I::bass808)
    {
        w.humanize = 0.0;   // locked to the kick
        for (size_t i = 0; i < kicks.size(); ++i)
        {
            const double st = kicks[i];
            const double next = i + 1 < kicks.size() ? kicks[i + 1] : st + 2.0;
            const int p = bassNote (st);
            const bool slide = w.rng.chance (0.12);
            w.add (out, st, juce::jmin (next - st, 2.5) - 0.05, p, 0.9f, slide ? art::slideUp : art::normal, false);
        }
        return;
    }

    for (auto& s : w.song.sections)
    {
        const bool intro = s.kind == Section::intro;
        if (intro && g != Genre::bluegrass && g != Genre::blues) continue;
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = (s.startBar + b) * w.bpb();
            const float v = 0.75f + 0.2f * s.energy;
            if (g == Genre::blues)
            {
                // walking boogie: root, 3rd, 5th, 6th (and back down on even bars)
                const int r = bassNote (t);
                const int up[4] = { 0, 4, 7, 9 }, down[4] = { 12, 9, 7, 4 };
                for (int k = 0; k < 4; ++k) w.add (out, t + k, 0.9, r + (b % 2 == 0 ? up[k] : down[k]), v);
            }
            else if (g == Genre::cinematic || s.kind == Section::bridge || s.kind == Section::outro)
            {
                w.add (out, t, 1.9, bassNote (t), v);
                w.add (out, t + 2, 1.9, bassNote (t + 2), v * 0.9f);
            }
            else if (g == Genre::gospel)
            {
                const int r = bassNote (t);
                w.add (out, t, 0.9, r, v);
                w.add (out, t + 1.5, 0.45, r + 12, v * 0.8f);
                w.add (out, t + 2, 0.9, bassNote (t + 2), v);
                // chromatic walk into the next chord
                const int next = bassNote (t + w.bpb());
                w.add (out, t + 3, 0.45, next + (next > r ? -2 : 2), v * 0.75f);
                w.add (out, t + 3.5, 0.45, next + (next > r ? -1 : 1), v * 0.75f);
            }
            else
            {
                // root - fifth (country / bluegrass / folk)
                const int r1 = bassNote (t), r2 = bassNote (t + 2);
                w.add (out, t, 0.9, r1, v);
                w.add (out, t + 2, 0.9, r2 == r1 ? r1 + 7 - (r1 + 7 > low + 19 ? 12 : 0) : r2, v * 0.92f);
                if (g == Genre::bluegrass || s.energy > 0.8f)
                {
                    w.add (out, t + 1, 0.45, r1 + 7 - 12 >= low ? r1 + 7 - 12 : r1 + 7, v * 0.7f);
                    const int next = bassNote (t + w.bpb());
                    w.add (out, t + 3, 0.45, next + (next > r2 ? -2 : 2), v * 0.7f);
                }
            }
        }
    }
}

//==============================================================================
// Rhythm parts
//==============================================================================
void writeStrum (Writer& w, std::vector<Note>& out, bool boomChuck, bool fingerpick)
{
    w.humanize = 0.008;
    for (auto& s : w.song.sections)
    {
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = (s.startBar + b) * w.bpb();
            const float e = s.energy;

            if (fingerpick)
            {
                // Travis picking: thumb alternates bass, fingers fill the off beats
                for (int k = 0; k < 4; ++k)
                {
                    const auto v = w.voicing (t + k, 6);
                    if (v.size() < 4) continue;
                    w.add (out, t + k, 1.6, k % 2 == 0 ? v[0] : v[1], 0.7f);
                    const int hi = v[(size_t) (v.size() - 1 - (size_t) w.rng.below (3))];
                    w.add (out, t + k + 0.5, 1.2, hi, 0.5f + 0.1f * e);
                }
                continue;
            }

            if (boomChuck && s.kind != Section::chorus)
            {
                for (int k = 0; k < 4; ++k)
                {
                    const auto v = w.voicing (t + k, 6);
                    if (v.size() < 4) continue;
                    if (k % 2 == 0) w.add (out, t + k, 0.9, k == 0 ? v[0] : v[1], 0.8f);   // boom: single bass string
                    else for (size_t i = 2; i < v.size(); ++i)                             // chuck: short strum of the top strings
                        w.add (out, t + k + (double) (i - 2) * 0.02, 0.35, v[i], 0.62f, art::muted, false);
                }
                continue;
            }

            // full strums: D-DU-UDU
            static const char* pattern = "D-DU-UDU";
            for (int k = 0; k < 8; ++k)
            {
                const char c = pattern[k];
                if (c == '-') continue;
                if (s.energy < 0.5f && k % 2 == 1) continue;   // quiet sections: downstrokes only
                const double st = t + k * 0.5;
                const auto v = w.voicing (st, 6);
                double len = 0.5;
                for (int j = k + 1; j < 8; ++j) { if (pattern[j] != '-') break; len += 0.5; }
                const bool down = c == 'D';
                const float vel = (down ? 0.72f : 0.5f) * (0.75f + 0.3f * e);
                const int n = (int) v.size();
                for (int i = 0; i < n; ++i)
                {
                    const int idx = down ? i : n - 1 - i;
                    if (! down && idx < 2) continue;           // up strokes hit the high strings
                    w.add (out, st + i * 0.018, len + 0.05, v[(size_t) idx], vel * (down ? 1.0f - 0.03f * i : 0.9f), art::normal);
                }
            }
        }
    }
}

void writeBanjo (Writer& w, std::vector<Note>& out)
{
    w.humanize = 0.006;
    for (auto& s : w.song.sections)
    {
        if (s.kind == Section::bridge) continue;
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = (s.startBar + b) * w.bpb();
            // forward roll over 8 eighths: T I M T I M T M
            static const int roll[8] = { 0, 1, 2, 0, 1, 2, 0, 3 };
            for (int k = 0; k < 8; ++k)
            {
                const double st = t + k * 0.5;
                const auto v = w.voicing (st, 5, 50, 79);
                if (v.size() < 3) continue;
                const int drone = theory::atOrAbove (w.key() + 7, 67);   // short 5th string drone
                const int choices[4] = { v[1], v[2], v.back(), drone };
                w.add (out, st, 1.0, choices[roll[k]], k % 3 == 0 ? 0.8f : 0.6f);
            }
        }
    }
}

void writeMandolin (Writer& w, std::vector<Note>& out)
{
    w.humanize = 0.006;
    for (auto& s : w.song.sections)
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = (s.startBar + b) * w.bpb();
            for (int k = 1; k < 4; k += 2)                                     // the chop on 2 and 4
            {
                const auto v = w.voicing (t + k, 4, 55, 76);
                for (size_t i = 0; i < v.size(); ++i) w.add (out, t + k + i * 0.01, 0.18, v[i], 0.75f, art::chop, false);
            }
        }
}

void writePads (Writer& w, I inst, std::vector<Note>& out)
{
    w.humanize = 0.01;
    const bool organ = inst == I::organ;
    const int lo = organ ? 52 : 48, hi = organ ? 76 : 81;
    for (auto& s : w.song.sections)
    {
        if (organ && s.kind == Section::intro && w.song.request.genre != Genre::gospel) continue;
        for (size_t h = 0; h < s.chordRoots.size(); ++h)
        {
            const double st = s.startBar * w.bpb() + h * w.bpb() * 0.5;
            if (h > 0 && s.chordRoots[h] == s.chordRoots[h - 1]) continue;     // tie repeated chords
            double len = w.bpb() * 0.5;
            for (size_t j = h + 1; j < s.chordRoots.size() && s.chordRoots[j] == s.chordRoots[h]; ++j) len += w.bpb() * 0.5;
            const auto pcs = w.chordAt (st);
            const float v = 0.45f + 0.35f * s.energy;
            int last = lo - 1;
            for (int pc : pcs) { const int p = theory::atOrAbove (pc, last + 1); if (p <= hi) { w.add (out, st, len, p, v, art::normal, false); last = p; } }
            if (! organ && s.energy > 0.8f) w.add (out, st, len, theory::atOrAbove (pcs[0], last + 1), v * 0.8f, art::normal, false);
        }
    }
}

void writePiano (Writer& w, std::vector<Note>& out, bool lead)
{
    const Genre g = w.song.request.genre;
    w.humanize = 0.01;
    for (auto& s : w.song.sections)
    {
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = (s.startBar + b) * w.bpb();
            const auto pcs = w.chordAt (t);
            const float e = s.energy;
            if (g == Genre::trap || g == Genre::popTrap)
            {
                if (lead) continue;   // the melody is written by the lead pass
                // dark chord stabs on the first beat of each bar
                const auto v = w.voicing (t, 4, 55, 79);
                for (int p : v) w.add (out, t, 1.5, p, 0.5f, art::normal, false);
                continue;
            }
            if (g == Genre::cinematic)
            {
                // flowing arpeggio over two octaves
                const auto v = w.voicing (t, 6, 48, 84);
                for (int k = 0; k < 8; ++k) w.add (out, t + k * 0.5, 1.5, v[(size_t) (k < 4 ? k : 7 - k) % v.size()], 0.45f + 0.25f * e);
                continue;
            }
            // left hand octave root, right hand chord on the beats / gospel syncopation
            const int bassP = theory::atOrAbove (pcs[0], 36);
            w.add (out, t, 1.8, bassP, 0.7f);
            w.add (out, t, 1.8, bassP + 12, 0.55f);
            const double hits[4] = { 0.0, 1.5, 2.0, 3.0 };
            for (double h : hits)
            {
                if (g != Genre::gospel && h == 1.5) continue;
                int last = 59;
                for (int pc : w.chordAt (t + h)) { const int p = theory::atOrAbove (pc, last + 1); w.add (out, t + h, 0.9, p, 0.5f + 0.25f * e); last = p; }
            }
            if (b == s.bars - 1 && g == Genre::gospel)   // run up into the next section
                for (int k = 0; k < 6; ++k) w.add (out, t + 3.0 + k / 6.0, 0.2, w.nearestChordTone (t + 3, 72 + k * 2, 70, 90), 0.5f, art::normal, false);
        }
    }
}

//==============================================================================
// Leads: fills, melodies and solos (fiddle, harmonica, electric guitar, banjo, piano melody)
//==============================================================================
struct Range { int lo, hi; };

Range leadRange (I inst)
{
    switch (inst)
    {
        case I::fiddle:         return { 62, 86 };
        case I::harmonica:      return { 60, 84 };
        case I::electricGuitar: return { 52, 79 };
        case I::banjo:          return { 55, 79 };
        case I::mandolin:       return { 62, 86 };
        case I::piano:          return { 64, 88 };
        case I::strings:        return { 60, 84 };
        default:                return { 60, 84 };
    }
}

/** A phrase of `beats` starting at `start`: a random walk on the pentatonic that lands on chord tones. */
void phrase (Writer& w, std::vector<Note>& out, I inst, double start, double beats, double density, float vel, int& cursor)
{
    const auto rng = leadRange (inst);
    const auto pent = w.pentatonic (rng.lo, rng.hi);
    if (pent.empty()) return;
    double t = start;
    const double end = start + beats;
    // rhythm cells (lengths in beats)
    static const std::vector<std::vector<double>> busy { { 0.5, 0.5 }, { 0.25, 0.25, 0.5 }, { 0.5, 0.25, 0.25 }, { 1.0 / 3, 1.0 / 3, 1.0 / 3 } };
    static const std::vector<std::vector<double>> calm { { 1.0 }, { 1.5, 0.5 }, { 2.0 }, { 0.5, 1.5 } };
    while (t < end - 1.0e-6)
    {
        const auto& cell = w.rng.chance (density) ? busy[(size_t) w.rng.below ((int) busy.size())] : calm[(size_t) w.rng.below ((int) calm.size())];
        for (double len : cell)
        {
            if (t >= end - 1.0e-6) break;
            len = juce::jmin (len, end - t);
            // step on the scale, mostly by small moves
            int idx = 0;
            for (size_t i = 0; i < pent.size(); ++i) if (pent[i] == Writer::nearestIn (pent, cursor)) idx = (int) i;
            const int step = w.rng.below (5) - 2;
            idx = juce::jlimit (0, (int) pent.size() - 1, idx + step);
            int p = pent[(size_t) idx];
            const bool strong = std::abs (t - std::round (t)) < 1.0e-6;
            if (strong) p = w.nearestChordTone (t, p, rng.lo, rng.hi);
            cursor = p;
            int a = art::normal;
            if ((inst == I::harmonica || inst == I::electricGuitar || inst == I::fiddle) && strong && len >= 0.75 && w.rng.chance (0.35)) a = art::bend;
            w.add (out, t, len * (inst == I::fiddle || inst == I::strings ? 1.0 : 0.92), p, vel * (strong ? 1.0f : 0.85f), a);
            t += len;
        }
    }
}

void writeLead (Writer& w, I inst, std::vector<Note>& out, bool isMainLead)
{
    const Genre g = w.song.request.genre;
    w.humanize = 0.012;
    const auto rng = leadRange (inst);
    int cursor = (rng.lo + rng.hi) / 2;
    for (auto& s : w.song.sections)
    {
        const double secStart = s.startBar * w.bpb();
        const float vel = 0.55f + 0.35f * s.energy;
        const bool solo = s.kind == Section::solo || (s.kind == Section::intro && isMainLead && g != Genre::trap);
        if (solo)
        {
            if (! isMainLead && s.kind == Section::solo) continue;
            // continuous lines; bluegrass is busy, others breathe every 2 bars
            for (int b = 0; b < s.bars; b += 2)
            {
                const double busy = g == Genre::bluegrass ? 0.8 : 0.5;
                phrase (w, out, inst, secStart + b * w.bpb(), w.bpb() * 2 - (g == Genre::bluegrass ? 0.0 : 0.5), busy, vel, cursor);
            }
            continue;
        }
        if (s.kind == Section::chorus && (inst == I::fiddle || inst == I::strings || inst == I::harmonica || inst == I::organ))
        {
            // long notes under the chorus
            for (int b = 0; b < s.bars; b += 1)
            {
                const double st = secStart + b * w.bpb();
                const int p = w.nearestChordTone (st, cursor, rng.lo, rng.hi);
                cursor = p;
                w.add (out, st, w.bpb() - 0.1, p, vel * 0.8f);
                if (inst == I::fiddle && w.rng.chance (0.5))   // double stop: add the third below
                    w.add (out, st, w.bpb() - 0.1, w.nearestChordTone (st, p - 4, rng.lo - 7, p - 1), vel * 0.6f, art::doubleStop);
            }
            continue;
        }
        if ((g == Genre::trap || g == Genre::popTrap) && inst == I::piano)
        {
            // a 2-bar melody loop, repeated through the section
            std::vector<Note> loop;
            Writer w2 = w;
            int c2 = cursor;
            phrase (w2, loop, inst, 0.0, w.bpb() * 2.0 - 0.5, 0.35, 0.6f, c2);
            for (int b = 0; b + 1 < s.bars + 1; b += 2)
                for (auto n : loop) { n.start += secStart + b * w.bpb(); if (n.start < secStart + s.bars * w.bpb()) out.push_back (n); }
            continue;
        }
        if (s.kind == Section::outro && ! isMainLead) continue;
        // verses / bridge: answer the singer in the gaps (last 1.5 beats of every second bar)
        for (int b = 1; b < s.bars; b += 2)
        {
            if (! isMainLead && (b / 2) % 2 == 0) continue;   // second lead plays every other gap
            const double st = secStart + b * w.bpb() + 2.0;
            phrase (w, out, inst, st, w.bpb() - 2.0, 0.6, vel * 0.9f, cursor);
        }
    }
}

void writeHarmonicaChords (Writer& w, std::vector<Note>& out)
{
    // the "train" chug: two-note chords on the 8ths, breathing in and out
    for (auto& s : w.song.sections)
    {
        if (s.kind != Section::intro && s.kind != Section::solo) continue;
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = (s.startBar + b) * w.bpb();
            for (int k = 0; k < 8; ++k)
            {
                const double st = t + k * 0.5;
                const auto pcs = w.chordAt (st);
                const int a = theory::atOrAbove (pcs[0], 64), c = theory::atOrAbove (pcs[1], a + 1);
                const float v = k % 2 == 0 ? 0.75f : 0.55f;
                w.add (out, st, 0.42, a, v);
                w.add (out, st, 0.42, c, v * 0.9f);
            }
        }
    }
}

float panFor (I inst, int indexAmongRhythm)
{
    switch (inst)
    {
        case I::drums: case I::trapDrums: case I::bass: case I::uprightBass: case I::bass808: return 0.0f;
        case I::acousticGuitar: return -0.35f;
        case I::electricGuitar: return 0.4f;
        case I::banjo: return 0.3f;
        case I::mandolin: return -0.45f;
        case I::fiddle: return 0.2f;
        case I::harmonica: return -0.15f;
        case I::piano: return indexAmongRhythm % 2 == 0 ? -0.1f : 0.1f;
        case I::organ: return 0.25f;
        case I::strings: return 0.0f;
        case I::count: break;
    }
    return 0.0f;
}
} // namespace

//==============================================================================
double defaultBpm (Genre g)
{
    switch (g)
    {
        case Genre::outlawCountry: return 96.0;
        case Genre::bluegrass:     return 132.0;
        case Genre::folk:          return 96.0;
        case Genre::gospel:        return 74.0;
        case Genre::blues:         return 84.0;
        case Genre::trap:          return 140.0;
        case Genre::popTrap:       return 150.0;
        case Genre::cinematic:     return 72.0;
        case Genre::count:         break;
    }
    return 100.0;
}

Song compose (const Request& reqIn)
{
    Song song;
    song.request = reqIn;
    auto& req = song.request;
    if (req.instruments.empty()) req.instruments = defaultInstruments (req.genre);
    song.bpm = req.bpm > 20.0 ? juce::jlimit (40.0, 220.0, req.bpm) : defaultBpm (req.genre);
    song.beatsPerBar = 4;
    switch (req.genre)
    {
        case Genre::outlawCountry: song.swing = 0.15; break;
        case Genre::bluegrass:     song.swing = 0.12; break;
        case Genre::gospel:        song.swing = 0.6; break;
        case Genre::blues:         song.swing = 1.0; break;
        default:                   song.swing = 0.0; break;
    }

    Rng rng (req.seed);
    int bar = 0;
    for (auto& f : songForm (req.genre, req.fullSong))
    {
        SectionPlan s;
        s.kind = f.kind;
        s.startBar = bar;
        s.bars = f.bars;
        s.energy = f.energy;
        s.chordRoots = progression (req.genre, req.minor, f.kind, f.bars, rng);
        song.sections.push_back (s);
        bar += f.bars;
    }
    song.totalBars = bar;

    // repeated sections share their chords (verse 2 = verse 1) so the song holds together
    for (size_t i = 0; i < song.sections.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (song.sections[j].kind == song.sections[i].kind && song.sections[j].bars == song.sections[i].bars)
            { song.sections[i].chordRoots = song.sections[j].chordRoots; break; }

    const I mainLead = pickLead (req.instruments);
    std::vector<double> kicks;
    int rhythmIndex = 0;
    // drums first so the 808 can follow the kick
    std::vector<I> order = req.instruments;
    std::stable_sort (order.begin(), order.end(), [] (I a, I b) { return isDrums (a) && ! isDrums (b); });

    for (I inst : order)
    {
        Part part;
        part.instrument = inst;
        part.pan = panFor (inst, rhythmIndex++);
        Writer w { song, Rng (req.partSeeds[(size_t) inst] != 0 ? req.partSeeds[(size_t) inst] : req.seed * 2654435761u + (uint32_t) inst * 97u + 1u) };
        const Genre g = req.genre;
        switch (inst)
        {
            case I::drums:          writeDrums (w, part.notes); break;
            case I::trapDrums:      writeTrapDrums (w, part.notes, kicks); break;
            case I::bass: case I::uprightBass: case I::bass808: writeBass (w, inst, part.notes, kicks); break;
            case I::acousticGuitar:
                writeStrum (w, part.notes, g == Genre::bluegrass || g == Genre::outlawCountry, g == Genre::folk || g == Genre::cinematic);
                break;
            case I::banjo:          if (g == Genre::bluegrass || g == Genre::outlawCountry || g == Genre::folk) writeBanjo (w, part.notes);
                                    if (inst == mainLead) writeLead (w, inst, part.notes, true);
                                    break;
            case I::mandolin:       writeMandolin (w, part.notes); if (inst == mainLead) writeLead (w, inst, part.notes, true); break;
            case I::organ:          writePads (w, inst, part.notes); break;
            case I::strings:        if (inst == mainLead) writeLead (w, inst, part.notes, true); else writePads (w, inst, part.notes); break;
            case I::piano:          writePiano (w, part.notes, inst == mainLead);
                                    if (inst == mainLead || g == Genre::trap || g == Genre::popTrap) writeLead (w, inst, part.notes, inst == mainLead);
                                    break;
            case I::harmonica:      if (g == Genre::outlawCountry || g == Genre::folk || g == Genre::bluegrass) writeHarmonicaChords (w, part.notes);
                                    writeLead (w, inst, part.notes, inst == mainLead);
                                    break;
            case I::fiddle:         writeLead (w, inst, part.notes, inst == mainLead); break;
            case I::electricGuitar:
                if (g == Genre::popTrap || g == Genre::trap)
                {
                    // clean arpeggio loop
                    w.humanize = 0.004;
                    for (auto& s : song.sections)
                        for (int b = 0; b < s.bars; ++b)
                        {
                            const double t = (s.startBar + b) * song.beatsPerBar;
                            const auto v = w.voicing (t, 5, 52, 79);
                            static const int arp[8] = { 0, 2, 3, 4, 3, 2, 1, 2 };
                            for (int k = 0; k < 8; ++k) w.add (part.notes, t + k * 0.5, 0.9, v[(size_t) arp[k] % v.size()], 0.55f);
                        }
                }
                else if (g == Genre::blues)
                {
                    // shuffle boogie: root + 5th / 6th dyads
                    for (auto& s : song.sections)
                        for (int b = 0; b < s.bars; ++b)
                        {
                            const double t = (s.startBar + b) * song.beatsPerBar;
                            const int r = theory::atOrAbove (w.rootPc (t), 40);
                            for (int k = 0; k < 8; ++k)
                            {
                                const int top = r + ((k / 2) % 2 == 0 ? 7 : 9);
                                w.add (part.notes, t + k * 0.5, 0.45, r, 0.6f, art::muted);
                                w.add (part.notes, t + k * 0.5, 0.45, top, 0.55f, art::muted);
                            }
                        }
                    writeLead (w, inst, part.notes, inst == mainLead);
                }
                else
                {
                    writeLead (w, inst, part.notes, inst == mainLead);
                    if (inst != mainLead)
                        for (auto& s : song.sections)   // country: twangy off-beat double stops in choruses
                            if (s.kind == Section::chorus)
                                for (int b = 0; b < s.bars; ++b)
                                    for (int k = 1; k < 8; k += 2)
                                    {
                                        const double st = (s.startBar + b) * song.beatsPerBar + k * 0.5;
                                        const auto v = w.voicing (st, 6, 52, 76);
                                        if (v.size() >= 5) { w.add (part.notes, st, 0.3, v[3], 0.5f, art::muted); w.add (part.notes, st, 0.3, v[4], 0.45f, art::muted); }
                                    }
                }
                break;
            case I::count: break;
        }
        std::stable_sort (part.notes.begin(), part.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        const double endBeat = song.lengthBeats() + 1.0;
        part.notes.erase (std::remove_if (part.notes.begin(), part.notes.end(), [endBeat] (const Note& n) { return n.start >= endBeat; }), part.notes.end());
        song.parts.push_back (std::move (part));
    }
    return song;
}
} // namespace pg::band
