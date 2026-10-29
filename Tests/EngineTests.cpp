#include "TestLibrary.h"
#include "Engine/ForgeEngine.h"
#include <cmath>

namespace
{
using E = hf::ForgeEngine;

struct EngineRig
{
    std::unique_ptr<E> e;
    juce::AudioBuffer<float> out;
    juce::MidiBuffer midi;

    explicit EngineRig (bool offline = true, int block = 512)
    {
        e = std::make_unique<E> (offline);
        e->setPlayConfigDetails (0, 2, 48000.0, block);
        e->prepareToPlay (48000.0, block);
        // quiet, dry, close: the tests look at the direct sound
        e->setParam (E::Ids::room, 0.0f);
        e->setParam (E::Ids::reverb, -40.0f);
        e->setParam (E::Ids::mics, 0.0f);
        for (int t = 0; t < hf::kMaxTracks; ++t) { e->setParam (E::Ids::track (t, E::kDepth), 0.0f); e->setParam (E::Ids::track (t, E::kX), 0.0f); }
    }
    ~EngineRig() { e->releaseResources(); }

    /** Renders `samples` and returns the left channel. */
    std::vector<float> render (int samples, int block = 512)
    {
        std::vector<float> l;
        l.reserve ((size_t) samples);
        out.setSize (2, block, false, false, true);
        for (int done = 0; done < samples; done += block)
        {
            const int n = juce::jmin (block, samples - done);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, 0, n);
            view.clear();
            e->processBlock (view, midi);
            l.insert (l.end(), view.getReadPointer (0), view.getReadPointer (0) + n);
        }
        return l;
    }
};

int firstAbove (const std::vector<float>& x, float thr, int from = 0)
{
    for (int i = from; i < (int) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > thr) return i;
    return -1;
}

hf::Song oneNoteSong (double beat, double length = 0.5, int pitch = 60)
{
    hf::Song s;
    s.bpm = 120.0;
    hf::Track t;
    t.name = "Test";
    t.instrumentId = "test-sine";
    hf::Note n;
    n.start = beat; n.length = length; n.pitch = pitch; n.velocity = 40;
    t.notes.push_back (n);
    s.tracks.push_back (t);
    s.renumber();
    return s;
}

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "Forge") {}

    void runTest() override
    {
        hemtest::TestLibrary tl;
        auto sine = tl.load ("test-sine");

        beginTest ("a note starts on its exact sample");
        {
            EngineRig r;
            r.e->setTrackInstrument (0, sine);
            r.e->publishSong (oneNoteSong (1.0));
            r.e->setPositionBeats (0.0);
            r.e->play();
            const auto l = r.render (48000);
            const int onset = firstAbove (l, 1.0e-4f);
            logMessage ("onset at sample " + juce::String (onset) + " (beat 1 at 120 BPM = 24000)");
            expect (onset >= 24000 && onset <= 24004);
        }

        beginTest ("the loop plays the bar again");
        {
            EngineRig r;
            r.e->setTrackInstrument (0, sine);
            r.e->publishSong (oneNoteSong (1.0, 0.25));
            r.e->setLoop (true, 0.0, 2.0);
            r.e->setPositionBeats (0.0);
            r.e->play();
            const auto l = r.render (48000 * 3);
            const int first = firstAbove (l, 1.0e-4f);
            int second = -1;
            for (int i = first + 30000; i < (int) l.size(); ++i)
                if (std::abs (l[(size_t) i]) > 1.0e-4f && rmsBefore (l, i) < 1.0e-5) { second = i; break; }
            logMessage ("loop onsets " + juce::String (first) + " and " + juce::String (second));
            expect (std::abs (second - (first + 48000)) <= 4);
        }

        beginTest ("live notes from the keyboard sound at once and stop on release");
        {
            EngineRig r;
            r.e->setTrackInstrument (0, sine);
            r.e->publishSong (oneNoteSong (100.0));
            r.e->setLiveTrack (0);
            r.e->uiNoteOn (60, 100);
            auto l = r.render (4800);
            expect (firstAbove (l, 1.0e-3f) >= 0 && firstAbove (l, 1.0e-3f) < 600);
            r.e->uiNoteOff (60);
            r.render (48000);
            expectEquals (r.e->getActiveVoices(), 0);
        }

        beginTest ("mute and solo");
        {
            EngineRig r;
            r.e->setTrackInstrument (0, sine);
            r.e->setTrackInstrument (1, sine);
            auto s = oneNoteSong (0.0, 1.0);
            s.tracks.push_back (s.tracks[0]);
            s.renumber();
            r.e->publishSong (s);
            r.e->setParam (E::Ids::track (1, E::kSolo), 1.0f);
            r.e->setParam (E::Ids::track (1, E::kMute), 1.0f);
            r.e->setPositionBeats (0.0);
            r.e->play();
            const auto l = r.render (12000);
            expectEquals (firstAbove (l, 1.0e-5f), -1);   // the soloed track is muted, the other is not soloed
        }

        beginTest ("recording captures what is played live");
        {
            EngineRig r;
            r.e->setTrackInstrument (0, sine);
            r.e->publishSong (oneNoteSong (100.0));
            r.e->setLiveTrack (0);
            r.e->setRecording (true);
            r.e->setPositionBeats (0.0);
            r.e->play();
            r.render (24000);            // beat 1
            r.e->uiNoteOn (64, 90);
            r.render (12000);            // half a beat
            r.e->uiNoteOff (64);
            r.render (2048);
            std::vector<E::RecordedNote> rec;
            r.e->collectRecorded (rec);
            expectEquals ((int) rec.size(), 1);
            if (! rec.empty())
            {
                expectEquals (rec[0].note.pitch, 64);
                expectWithinAbsoluteError (rec[0].note.start, 1.0, 0.03);
                expectWithinAbsoluteError (rec[0].note.length, 0.5, 0.03);
            }
        }

        beginTest ("changing a track's instrument frees the old one once it is silent");
        {
            EngineRig r;
            auto a = tl.load ("test-sine");
            std::weak_ptr<hf::LoadedInstrument> weakA = a;
            r.e->setTrackInstrument (0, a);
            a.reset();
            r.e->publishSong (oneNoteSong (0.0, 4.0));
            r.e->setPositionBeats (0.0);
            r.e->play();
            r.render (4800);
            r.e->setTrackInstrument (0, tl.load ("test-rr"));
            r.e->collectGarbage();
            expect (! weakA.expired());   // the audio thread may still be using it
            r.render (4800);
            r.e->collectGarbage();
            expect (weakA.expired());
        }

        beginTest ("32 busy tracks: no allocation on the audio thread, and the CPU cost");
        {
            EngineRig r (false, 512);   // real-time engine with its own disk thread
            hf::Song s;
            s.bpm = 140.0;
            for (int t = 0; t < hf::kMaxTracks; ++t)
            {
                hf::Track tr;
                tr.name = "T" + juce::String (t);
                for (int k = 0; k < 64; ++k)
                {
                    hf::Note n;
                    n.start = k * 0.25 + t * 0.01; n.length = 0.6; n.pitch = 40 + (t + k * 5) % 40; n.velocity = 20 + (k * 7) % 100;
                    n.articulation = k % 3;
                    tr.notes.push_back (n);
                }
                s.tracks.push_back (tr);
                r.e->setTrackInstrument (t, sine);
                r.e->setParam (E::Ids::track (t, E::kX), (float) t / 16.0f - 1.0f);
                r.e->setParam (E::Ids::track (t, E::kDepth), (float) (t % 5) / 4.0f);
                r.e->setParam (E::Ids::track (t, E::kHuman), 0.5f);
            }
            s.renumber();
            r.e->publishSong (s);
            r.e->setParam (E::Ids::room, 4.0f);
            r.e->setParam (E::Ids::reverb, -6.0f);
            r.e->setPositionBeats (0.0);
            r.e->play();
            r.render (2048);   // first blocks pick up the song
            juce::AudioBuffer<float> b (2, 512);
            hemtest::beginTracking();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            int rendered = 0;
            for (int k = 0; k < 400; ++k)
            {
                b.clear();
                r.e->processBlock (b, r.midi);
                rendered += 512;
                if (k == 200)
                {
                    // switching to headphone 3D is a message-thread action: not counted
                    const int before = hemtest::endTracking();
                    r.e->setParam (E::Ids::headphones, 1.0f);
                    hemtest::beginTracking();
                    hemtest::addToTracked (before);
                }
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const int allocs = hemtest::endTracking();
            expectEquals (allocs, 0);
            const double realtime = (rendered / 48.0) / juce::jmax (0.001, ms);
            logMessage ("32 tracks, " + juce::String (r.e->getActiveVoices()) + " voices sounding at the end: "
                        + juce::String (realtime, 1) + "x real time on one core; underruns " + juce::String (r.e->getUnderruns()));
            expect (realtime > 1.0);
            for (int i = 0; i < 512; ++i) expect (std::isfinite (b.getSample (0, i)));
        }

        beginTest ("the safety limiter holds the output under -0.3 dBFS");
        {
            EngineRig r;
            for (int t = 0; t < 8; ++t) { r.e->setTrackInstrument (t, sine); r.e->setParam (E::Ids::track (t, E::kLevel), 12.0f); }
            hf::Song s;
            s.bpm = 120.0;
            for (int t = 0; t < 8; ++t)
            {
                hf::Track tr;
                for (int k = 0; k < 8; ++k) { hf::Note n; n.start = k * 0.5; n.length = 2.0; n.pitch = 60; n.velocity = 120; tr.notes.push_back (n); }
                s.tracks.push_back (tr);
            }
            s.renumber();
            r.e->publishSong (s);
            r.e->setPositionBeats (0.0);
            r.e->play();
            const auto l = r.render (96000);
            float peak = 0.0f;
            for (auto v : l) peak = juce::jmax (peak, std::abs (v));
            expectLessOrEqual (peak, juce::Decibels::decibelsToGain (-0.3f) + 1.0e-5f);
        }
    }

    static double rmsBefore (const std::vector<float>& x, int i)
    {
        double s = 0.0;
        for (int k = juce::jmax (0, i - 2000); k < i; ++k) s += (double) x[(size_t) k] * x[(size_t) k];
        return std::sqrt (s / 2000.0);
    }
};

static EngineTests engineTests;
} // namespace
