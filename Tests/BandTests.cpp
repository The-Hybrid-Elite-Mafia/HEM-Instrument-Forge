#include "TestLibrary.h"
#include "Band/BandBridge.h"

namespace
{
namespace bb = hf::bandbridge;
using I = pg::band::Instrument;
using G = pg::band::Genre;

class BandTests final : public juce::UnitTest
{
public:
    BandTests() : juce::UnitTest ("Band", "Forge") {}

    void runTest() override
    {
        hemtest::TestLibrary tl;
        const auto& lib = tl.library();

        beginTest ("every genre writes a song and every part gets a track with an instrument");
        {
            for (int g = 0; g < pg::band::kNumGenres; ++g)
            {
                pg::band::Request req;
                req.genre = (G) g;
                req.keyRoot = 2;
                req.fullSong = false;
                req.seed = 7u + (juce::uint32) g;
                std::vector<bb::RoleChoice> roles;
                for (auto r : pg::band::defaultInstruments (req.genre))
                    roles.push_back ({ r, bb::defaultInstrument (r, req.genre, lib), true });
                const auto s = bb::makeSong (req, roles, lib);
                expectEquals ((int) s.tracks.size(), (int) roles.size());
                for (auto& t : s.tracks)
                {
                    expect (! t.notes.empty(), t.name);
                    expect (t.instrumentId.isNotEmpty());
                    const bool drums = bb::isDrumRole ((I) t.bandRole);
                    expect (drums ? t.instrumentId == "test-kit" : t.instrumentId != "test-kit");
                }
                expect (s.bandInfo.isNotEmpty());
            }
        }

        beginTest ("same request, same song");
        {
            pg::band::Request req;
            req.genre = G::bluegrass;
            req.seed = 99;
            std::vector<bb::RoleChoice> roles { { I::banjo, "test-sine", true }, { I::fiddle, "test-sine", true }, { I::uprightBass, "test-sine", true } };
            const auto a = bb::makeSong (req, roles, lib), b = bb::makeSong (req, roles, lib);
            bool same = a.tracks.size() == b.tracks.size();
            for (size_t t = 0; same && t < a.tracks.size(); ++t)
            {
                same &= a.tracks[t].notes.size() == b.tracks[t].notes.size();
                for (size_t k = 0; same && k < a.tracks[t].notes.size(); ++k)
                    same &= a.tracks[t].notes[k].start == b.tracks[t].notes[k].start && a.tracks[t].notes[k].pitch == b.tracks[t].notes[k].pitch;
            }
            expect (same);
        }

        beginTest ("re-rolling one part leaves the others alone");
        {
            pg::band::Request req;
            req.genre = G::outlawCountry;
            req.seed = 5;
            std::vector<bb::RoleChoice> roles { { I::drums, "test-kit", true }, { I::bass, "test-sine", true }, { I::fiddle, "test-sine", true } };
            auto s = bb::makeSong (req, roles, lib);
            const auto before = s;
            int fiddle = -1;
            for (int t = 0; t < (int) s.tracks.size(); ++t) if (s.tracks[(size_t) t].bandRole == (int) I::fiddle) fiddle = t;
            expect (fiddle >= 0);
            expect (bb::rerollTrack (s, fiddle, 12345u, lib));
            bool othersSame = true, fiddleChanged = false;
            for (int t = 0; t < (int) s.tracks.size(); ++t)
            {
                const auto& a = before.tracks[(size_t) t].notes;
                const auto& b = s.tracks[(size_t) t].notes;
                bool same = a.size() == b.size();
                for (size_t k = 0; same && k < a.size(); ++k) same &= a[k].start == b[k].start && a[k].pitch == b[k].pitch;
                if (t == fiddle) fiddleChanged = ! same; else othersSame &= same;
            }
            expect (othersSame);
            expect (fiddleChanged);
        }

        beginTest ("parts are moved into the instrument's range by octaves");
        {
            hf::Track t;
            for (int p : { 30, 32, 34, 35, 90 }) { hf::Note n; n.pitch = p; t.notes.push_back (n); }
            bb::fitToRange (t, 48, 72);
            for (auto& n : t.notes) expect (n.pitch >= 48 && n.pitch <= 72);
            expectEquals (t.notes[0].pitch % 12, 30 % 12);
            hf::Track inside;
            for (int p : { 50, 60, 70 }) { hf::Note n; n.pitch = p; inside.notes.push_back (n); }
            bb::fitToRange (inside, 48, 72);
            expectEquals (inside.notes[1].pitch, 60);
        }

        beginTest ("candidates: drum parts get kits, melodic parts get pitched instruments");
        {
            for (auto* e : bb::candidates (I::drums, lib)) expectEquals (e->kind, juce::String ("drums"));
            for (auto* e : bb::candidates (I::piano, lib)) expectEquals (e->kind, juce::String ("pitched"));
            expect (! bb::candidates (I::fiddle, lib).empty());
        }

        beginTest ("request survives the session file format");
        {
            pg::band::Request req;
            req.genre = G::gospel; req.keyRoot = 5; req.minor = true; req.bpm = 77; req.fullSong = false; req.seed = 4242;
            req.instruments = { I::organ, I::piano };
            req.partSeeds[(size_t) I::piano] = 31337u;
            const auto back = bb::requestFromVar (juce::JSON::parse (juce::JSON::toString (bb::requestToVar (req))));
            expect (back.genre == req.genre && back.keyRoot == 5 && back.minor && back.bpm == 77 && ! back.fullSong && back.seed == 4242);
            expectEquals ((int) back.instruments.size(), 2);
            expectEquals ((int) back.partSeeds[(size_t) I::piano], 31337);
        }

        beginTest ("style text for Suno");
        {
            pg::band::Request req;
            req.genre = G::folk; req.keyRoot = 2; req.seed = 3; req.fullSong = false;
            const auto s = bb::makeSong (req, { { I::acousticGuitar, "test-sine", true } }, lib);
            const auto t = bb::sunoStyle (s, lib);
            expect (t.startsWith ("Folk instrumental"));
            expect (t.contains ("D major"));
        }
    }
};

static BandTests bandTests;
} // namespace
