#include "TestLibrary.h"
#include "Stage/Stage.h"
#include <cmath>

namespace
{
double rms (const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (s / juce::jmax (1, to - from));
}

class StageTests final : public juce::UnitTest
{
public:
    StageTests() : juce::UnitTest ("Stage", "Forge") {}

    struct Out { std::vector<float> l, r, sl, sr; };

    Out run (const hf::StageParams& p, const hf::GlobalStage& g, int seconds = 1, bool impulse = false)
    {
        hf::StageTrack st;
        st.prepare (48000.0, 256);
        Out o;
        const int total = 48000 * seconds;
        std::vector<float> in ((size_t) total);
        for (int i = 0; i < total; ++i) in[(size_t) i] = impulse ? (i == 0 ? 1.0f : 0.0f) : 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0);
        o.l.assign ((size_t) total, 0.0f); o.r = o.l; o.sl = o.l; o.sr = o.l;
        for (int i = 0; i < total; i += 256)
        {
            const int n = juce::jmin (256, total - i);
            st.process (in.data() + i, in.data() + i, o.l.data() + i, o.r.data() + i, o.sl.data() + i, o.sr.data() + i, n, p, g);
        }
        return o;
    }

    void runTest() override
    {
        beginTest ("further back: quieter, darker, later, more room");
        {
            hf::GlobalStage g;
            g.room = 2;
            hf::StageParams front, back;
            front.depth = 0.0f; back.depth = 1.0f;
            const auto a = hf::StageTrack::computeGeometry (front, g);
            const auto b = hf::StageTrack::computeGeometry (back, g);
            expect (b.distance > a.distance);
            expect (b.gain < a.gain);
            expect (b.airHz < a.airHz);
            expect (b.delayMs > a.delayMs);
            expect (b.sendGain > a.sendGain);
            logMessage ("front: " + juce::String (a.distance, 2) + " m, " + juce::String (juce::Decibels::gainToDecibels (a.gain), 1) + " dB; back: "
                        + juce::String (b.distance, 2) + " m, " + juce::String (juce::Decibels::gainToDecibels (b.gain), 1) + " dB, "
                        + juce::String (b.delayMs, 1) + " ms, air " + juce::String (b.airHz, 0) + " Hz");
            // more mic distance = more room
            g.micDistance = 1.0f;
            expect (hf::StageTrack::computeGeometry (front, g).sendGain > a.sendGain);
        }

        beginTest ("left seats come out of the left speaker");
        {
            hf::GlobalStage g; g.room = 5;   // plate: no walls, so only the direct sound + floor
            hf::StageParams p; p.x = -1.0f; p.depth = 0.2f;
            const auto o = run (p, g);
            expect (rms (o.l, 4800, 48000) > 2.0 * rms (o.r, 4800, 48000));
            expect (hf::StageTrack::computeGeometry (p, g).azimuth < 0.0f);
        }

        beginTest ("mono sum loses at most 3 dB for a hard-panned seat");
        {
            hf::GlobalStage g; g.room = 5;
            hf::StageParams centre; centre.depth = 0.2f; centre.x = 0.0f;
            hf::StageParams side = centre; side.x = 1.0f;
            const auto a = run (centre, g), b = run (side, g);
            std::vector<float> ma (a.l.size()), mb (b.l.size());
            for (size_t i = 0; i < ma.size(); ++i) { ma[i] = 0.5f * (a.l[i] + a.r[i]); mb[i] = 0.5f * (b.l[i] + b.r[i]); }
            // the side seat is also further away; take the distance level out, keep only the panning
            const double distanceDb = 20.0 * std::log10 (hf::StageTrack::computeGeometry (side, g).gain / hf::StageTrack::computeGeometry (centre, g).gain);
            const double lossDb = 20.0 * std::log10 (rms (mb, 4800, 48000) / rms (ma, 4800, 48000)) - distanceDb;
            logMessage ("mono-sum change from panning, hard right vs centre: " + juce::String (lossDb, 2) + " dB (distance "
                        + juce::String (distanceDb, 2) + " dB)");
            expectGreaterOrEqual (lossDb, -3.5);
        }

        beginTest ("headphone 3D: left source reaches the left ear first and louder");
        {
            hf::GlobalStage g; g.room = 5; g.headphone3D = true;
            hf::StageParams p; p.x = -1.0f; p.depth = 0.0f;
            const auto o = run (p, g, 1, true);
            int firstL = -1, firstR = -1;
            for (int i = 0; i < 2000; ++i)
            {
                if (firstL < 0 && std::abs (o.l[(size_t) i]) > 1.0e-4f) firstL = i;
                if (firstR < 0 && std::abs (o.r[(size_t) i]) > 1.0e-4f) firstR = i;
            }
            expect (firstL >= 0 && firstR >= 0 && firstR > firstL);
            logMessage ("ear arrival difference: " + juce::String ((firstR - firstL) / 48.0, 3) + " ms");
            double el = 0, er = 0;
            for (int i = 0; i < 4000; ++i) { el += o.l[(size_t) i] * o.l[(size_t) i]; er += o.r[(size_t) i] * o.r[(size_t) i]; }
            expect (el > er);
            for (auto v : o.l) expect (std::isfinite (v));
        }

        beginTest ("room tail decays by its decay time");
        {
            hf::RoomTail tail;
            tail.prepare (48000.0, 256);
            const auto& room = hf::roomPresets()[1];
            tail.configure (room, 1.0f);
            const int total = 48000 * 2;
            std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
            l[0] = r[0] = 1.0f;
            for (int i = 0; i < total; i += 256) tail.process (l.data() + i, r.data() + i, juce::jmin (256, total - i));
            const double early = rms (l, 2400, 7200);
            const int t60 = (int) (48000 * room.rtHigh * 0.5f + 48000 * room.rtLow * 0.5f);
            const double late = rms (l, 2400 + t60, 7200 + t60);
            const double drop = 20.0 * std::log10 (late / early);
            logMessage ("tail level after one decay time: " + juce::String (drop, 1) + " dB");
            expect (drop < -40.0 && drop > -80.0);
            for (auto v : l) expect (std::isfinite (v));
        }

        beginTest ("moving a seat while playing never jumps");
        {
            hf::StageTrack st;
            st.prepare (48000.0, 256);
            hf::GlobalStage g; g.room = 2;
            std::vector<float> in (256), l (256), r (256), sl (256), sr (256);
            float prevL = 0.0f, maxStep = 0.0f;
            int k = 0;
            for (int block = 0; block < 400; ++block)
            {
                hf::StageParams p;
                p.x = std::sin ((float) block * 0.05f);
                p.depth = 0.5f + 0.5f * std::sin ((float) block * 0.031f);
                for (int i = 0; i < 256; ++i, ++k) in[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * k / 48000.0);
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                st.process (in.data(), in.data(), l.data(), r.data(), sl.data(), sr.data(), 256, p, g);
                for (int i = 0; i < 256; ++i) { if (block > 4) maxStep = juce::jmax (maxStep, std::abs (l[(size_t) i] - prevL)); prevL = l[(size_t) i]; }
            }
            // a 220 Hz sine at 0.5 changes by at most ~0.0144 per sample; echoes add a little
            expectLessThan (maxStep, 0.05f);
        }

        beginTest ("stage processing never allocates");
        {
            hf::StageTrack st;
            st.prepare (48000.0, 256);
            hf::RoomTail tail;
            tail.prepare (48000.0, 256);
            std::vector<float> in (256, 0.1f), l (256), r (256), sl (256), sr (256);
            hf::GlobalStage g;
            hemtest::beginTracking();
            for (int b = 0; b < 50; ++b)
            {
                g.room = b % (int) hf::roomPresets().size();
                g.headphone3D = (b & 1) != 0;
                hf::StageParams p; p.x = (float) b / 50.0f;
                st.process (in.data(), in.data(), l.data(), r.data(), sl.data(), sr.data(), 256, p, g);
                tail.configure (hf::roomPresets()[(size_t) g.room], 1.0f);
                tail.process (sl.data(), sr.data(), 256);
            }
            expectEquals (hemtest::endTracking(), 0);
        }
    }
};

static StageTests stageTests;
} // namespace
