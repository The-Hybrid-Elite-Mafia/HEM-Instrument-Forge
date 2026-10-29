#include "TestLibrary.h"
#include "Engine/ForgeEngine.h"

namespace
{
class SessionTests final : public juce::UnitTest
{
public:
    SessionTests() : juce::UnitTest ("Session", "Forge") {}

    void runTest() override
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HEMForgeSessionTest");
        tmp.createDirectory();

        hf::Song s;
        s.bpm = 93.5;
        s.bandInfo = "Folk - D major - 93 BPM";
        for (int t = 0; t < 3; ++t)
        {
            hf::Track tr;
            tr.name = "Track " + juce::String (t);
            tr.instrumentId = t == 2 ? "test-kit" : "test-sine";
            tr.bandRole = t;
            for (int k = 0; k < 10; ++k)
            {
                hf::Note n;
                n.start = k * 0.5 + t * 0.125; n.length = 0.375; n.pitch = 48 + k + t; n.velocity = 30 + k * 9;
                n.articulation = k % 2 == 0 ? -1 : 1;
                n.glide = k == 3 ? -2.0f : 0.0f;
                tr.notes.push_back (n);
            }
            s.tracks.push_back (tr);
        }
        s.renumber();

        beginTest ("session save / load round trip (song + parameters)");
        {
            hf::ForgeEngine e (true);
            e.setParam (hf::ForgeEngine::Ids::track (1, hf::ForgeEngine::kX), -0.7f);
            e.setParam (hf::ForgeEngine::Ids::room, 3.0f);
            const auto f = tmp.getChildFile ("a.hemforge");
            juce::String err;
            expect (hf::session::save (f, s, e.getAPVTS().copyState(), err), err);
            hf::Song back;
            juce::ValueTree params;
            expect (hf::session::load (f, back, params, err), err);
            expectEquals ((int) back.tracks.size(), 3);
            expectEquals (back.bpm, 93.5);
            expectEquals (back.bandInfo, s.bandInfo);
            expectEquals (back.tracks[2].instrumentId, juce::String ("test-kit"));
            bool same = true;
            for (size_t t = 0; t < 3; ++t)
                for (size_t k = 0; k < 10; ++k)
                {
                    const auto& a = s.tracks[t].notes[k];
                    const auto& b = back.tracks[t].notes[k];
                    same &= a.start == b.start && a.length == b.length && a.pitch == b.pitch && a.velocity == b.velocity
                            && a.articulation == b.articulation && a.glide == b.glide;
                }
            expect (same);
            hf::ForgeEngine e2 (true);
            e2.getAPVTS().replaceState (params);
            expectWithinAbsoluteError (e2.param (hf::ForgeEngine::Ids::track (1, hf::ForgeEngine::kX)), -0.7f, 0.002f);
            expectEquals ((int) e2.param (hf::ForgeEngine::Ids::room), 3);
            expect (! tmp.getChildFile ("a.hemforge.tmp").exists());
        }

        beginTest ("not-a-session files are refused");
        {
            const auto f = tmp.getChildFile ("junk.hemforge");
            f.replaceWithText ("hello");
            hf::Song back;
            juce::ValueTree params;
            juce::String err;
            expect (! hf::session::load (f, back, params, err));
            expect (err.isNotEmpty());
        }

        beginTest ("MIDI file round trip");
        {
            const auto f = tmp.getChildFile ("a.mid");
            expect (hf::midi::write (s, f, [] (int t) { return t == 2; }));
            hf::Song back;
            juce::String err;
            expect (hf::midi::read (f, back, err), err);
            expectEquals ((int) back.tracks.size(), 3);
            expectWithinAbsoluteError (back.bpm, 93.5, 0.01);
            expectEquals ((int) back.tracks[0].notes.size(), 10);
            expectWithinAbsoluteError (back.tracks[1].notes[3].start, s.tracks[1].notes[3].start, 1.0e-3);
            expectWithinAbsoluteError (back.tracks[1].notes[3].length, s.tracks[1].notes[3].length, 1.0e-3);
            expectEquals (back.tracks[1].notes[3].pitch, s.tracks[1].notes[3].pitch);
            expectEquals (back.tracks[0].name, juce::String ("Track 0"));
        }

        beginTest ("song length rounds up to whole bars");
        {
            hf::Song e;
            expectEquals (e.lengthBeats(), 4.0);
            expectEquals (s.lengthBeats(), 8.0);
        }
        tmp.deleteRecursively();
    }
};

static SessionTests sessionTests;
} // namespace
