#include "TestLibrary.h"

namespace
{
class LibraryTests final : public juce::UnitTest
{
public:
    LibraryTests() : juce::UnitTest ("Library", "Forge") {}

    void runTest() override
    {
        hemtest::TestLibrary tl;

        beginTest ("index lists installed instruments and families");
        {
            const auto& lib = tl.library();
            expect (lib.isOpen());
            expectEquals ((int) lib.getEntries().size(), 4);
            expect (lib.getFamilies().contains ("Keys") && lib.getFamilies().contains ("Drum Kits"));
            expect (lib.find ("test-sine") != nullptr);
            expect (lib.find ("nope") == nullptr);
        }

        beginTest ("an instrument whose folder is missing is not listed");
        {
            auto copy = tl.folder().getSiblingFile (tl.folder().getFileName() + "-partial");
            copy.deleteRecursively();
            tl.folder().copyDirectoryTo (copy);
            copy.getChildFile ("percussion").deleteRecursively();
            hf::Library lib;
            juce::String err;
            expect (lib.open (copy, err));
            expectEquals ((int) lib.getEntries().size(), 3);
            expect (! lib.getFamilies().contains ("Percussion"));
            copy.deleteRecursively();
        }

        beginTest ("instrument.json is read");
        {
            auto d = tl.def ("test-sine");
            expect (d != nullptr);
            expectEquals ((int) d->articulations.size(), 4);
            expectEquals ((int) d->articulations[0].zones.size(), 6);
            expect (d->articulations[2].zones[0].hasLoop());
            expectEquals (d->articulations[3].zones[1].pedal, 1);
            expect (d->kind == hf::InstrumentKind::pitched);
            expect (tl.def ("test-kit")->isDrums());
        }

        beginTest ("bad instrument files are refused with a reason");
        {
            auto dir = tl.folder().getChildFile ("bad");
            dir.createDirectory();
            juce::String err;
            expect (hf::InstrumentDef::load (dir, err) == nullptr);
            expect (err.isNotEmpty());
            dir.getChildFile ("instrument.json").replaceWithText ("{\"format\":\"hem-instrument\",\"id\":\"x\",\"name\":\"X\",\"sampleRate\":48000,"
                                                                  "\"articulations\":[{\"name\":\"A\",\"zones\":[{\"file\":\"../evil.flac\",\"frames\":10,\"root\":60,\"lo\":0,\"hi\":127}]}]}");
            err.clear();
            expect (hf::InstrumentDef::load (dir, err) == nullptr);
            expect (err.contains ("file"));
            dir.getChildFile ("instrument.json").replaceWithText ("{\"format\":\"hem-instrument\",\"id\":\"x\",\"name\":\"X\",\"sampleRate\":48000,"
                                                                  "\"articulations\":[{\"name\":\"A\",\"zones\":[{\"file\":\"a.flac\",\"frames\":10,\"root\":60,\"lo\":70,\"hi\":20}]}]}");
            err.clear();
            expect (hf::InstrumentDef::load (dir, err) == nullptr);
            dir.deleteRecursively();
        }

        beginTest ("velocity cross-fade gains");
        {
            hf::Zone z;
            z.xinLo = 20; z.xinHi = 60; z.xoutLo = 90; z.xoutHi = 120;
            expectEquals (z.crossfadeGain (10), 0.0f);
            expectWithinAbsoluteError (z.crossfadeGain (40), std::sin (juce::MathConstants<float>::halfPi * 0.5f), 1.0e-5f);
            expectEquals (z.crossfadeGain (75), 1.0f);
            expectEquals (z.crossfadeGain (120), 0.0f);
            hf::Zone plain;
            expectEquals (plain.crossfadeGain (1), 1.0f);
        }

        beginTest ("loading decodes heads; short samples stay resident, long ones stream");
        {
            auto li = tl.load ("test-sine");
            expect (li != nullptr);
            const auto& a = li->articulation (0);
            expect (! a.zones[0].resident);
            expectEquals (a.zones[0].headFrames, hf::LoadedInstrument::kHeadFrames);
            // the head holds the exact sample values
            float maxErr = 0.0f;
            for (int i = 0; i < 5000; ++i)
                maxErr = juce::jmax (maxErr, std::abs (a.zones[0].head.getSample (0, i) - hemtest::TestLibrary::sineSample (48, 0, i)));
            expectLessThan (maxErr, 1.0e-5f);
            auto perc = tl.load ("test-perc");
            expect (perc->articulation (0).zones[0].resident);
            // key index: key 61 has only the second hit
            expectEquals ((int) perc->articulation (0).byKey[61].size(), 1);
            expect (perc->articulation (0).byKey[59].empty());
            expect (li->memoryBytes() > 0);
        }
    }
};

static LibraryTests libraryTests;
} // namespace
