#include "TestLibrary.h"
#include "Engine/Exporter.h"
#include "Core/Loudness.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
using E = hf::ForgeEngine;

juce::AudioBuffer<float> readWav (const juce::File& f, double* rate = nullptr, int* bits = nullptr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    juce::AudioBuffer<float> b;
    if (r == nullptr) return b;
    b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    if (rate) *rate = r->sampleRate;
    if (bits) *bits = (int) r->bitsPerSample;
    return b;
}

class ExportTests final : public juce::UnitTest
{
public:
    ExportTests() : juce::UnitTest ("Export", "Forge") {}

    void runTest() override
    {
        hemtest::TestLibrary tl;
        hf::exporter::Instruments insts {};
        insts[0] = tl.load ("test-sine");
        insts[1] = tl.load ("test-kit");

        hf::Song s;
        s.bpm = 120.0;
        hf::Track a; a.name = "Keys"; a.instrumentId = "test-sine";
        hf::Track b; b.name = "Kit"; b.instrumentId = "test-kit";
        for (int k = 0; k < 8; ++k)
        {
            hf::Note n; n.start = k * 0.5; n.length = 0.4; n.pitch = 55 + (k * 3) % 12; n.velocity = 60 + k * 5;
            a.notes.push_back (n);
            hf::Note d; d.start = k * 0.5; d.length = 0.1; d.pitch = k % 2 == 0 ? 36 : 38; d.velocity = 100;
            b.notes.push_back (d);
        }
        s.tracks = { a, b };
        s.renumber();

        hf::ForgeEngine params (true);
        params.setParam (E::Ids::track (1, E::kX), 0.6f);
        const auto state = params.getAPVTS().copyState();
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HEMForgeExportTest");
        folder.deleteRecursively();

        beginTest ("offline rendering is repeatable");
        {
            juce::AudioBuffer<float> x, y;
            expect (hf::exporter::render (s, state, insts, 48000.0, 2.0, -1, x));
            expect (hf::exporter::render (s, state, insts, 48000.0, 2.0, -1, y));
            expectEquals (x.getNumSamples(), y.getNumSamples());
            float diff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < x.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (x.getSample (ch, i) - y.getSample (ch, i)));
            expectEquals (diff, 0.0f);
            expectGreaterThan (x.getMagnitude (0, x.getNumSamples()), 0.01f);
            // the length is the song (1 bar at 120 BPM = 2 s) plus the tail
            expectEquals (x.getNumSamples(), (int) std::ceil ((2.0 + 2.0) * 48000.0));
        }

        beginTest ("stems add up to the mix");
        {
            juce::AudioBuffer<float> mix, s0, s1;
            hf::exporter::render (s, state, insts, 48000.0, 2.0, -1, mix);
            hf::exporter::render (s, state, insts, 48000.0, 2.0, 0, s0);
            hf::exporter::render (s, state, insts, 48000.0, 2.0, 1, s1);
            float diff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < mix.getNumSamples(); ++i)
                    diff = juce::jmax (diff, std::abs (mix.getSample (ch, i) - s0.getSample (ch, i) - s1.getSample (ch, i)));
            logMessage ("largest stem-sum difference: " + juce::String (diff, 7));
            expectLessThan (diff, 1.0e-4f);
        }

        beginTest ("export writes stems, a mastered mix and MIDI");
        {
            hf::ExportOptions o;
            o.folder = folder;
            o.baseName = "Test Song";
            o.mix = hf::ExportOptions::Mix::mastered;
            hf::ExportResult res;
            juce::String err;
            expect (hf::exporter::run (s, state, insts, o, res, err), err);
            expectEquals (res.files.size(), 4);
            expect (folder.getChildFile ("01 Keys.wav").existsAsFile());
            expect (folder.getChildFile ("02 Kit.wav").existsAsFile());
            expect (folder.getChildFile ("Test Song - Mastered Mix.wav").existsAsFile());
            expect (folder.getChildFile ("Test Song.mid").existsAsFile());
            double rate = 0; int bits = 0;
            const auto m = readWav (folder.getChildFile ("Test Song - Mastered Mix.wav"), &rate, &bits);
            expectEquals (rate, 48000.0);
            expectEquals (bits, 24);
            pg::dsp::LoudnessMeter lm;
            lm.prepare (48000.0);
            lm.process (m.getReadPointer (0), m.getReadPointer (1), m.getNumSamples());
            logMessage ("mastered: " + res.mixInfo);
            expectWithinAbsoluteError (lm.getIntegrated(), -14.0f, 1.5f);
        }

        beginTest ("16-bit / 44.1 kHz export");
        {
            hf::ExportOptions o;
            o.folder = folder.getChildFile ("cd");
            o.stems = false;
            o.midi = false;
            o.mix = hf::ExportOptions::Mix::clean;
            o.sampleRate = 44100.0;
            o.bitDepth = 16;
            hf::ExportResult res;
            juce::String err;
            expect (hf::exporter::run (s, state, insts, o, res, err), err);
            expectEquals (res.files.size(), 1);
            double rate = 0; int bits = 0;
            const auto m = readWav (res.files[0], &rate, &bits);
            expectEquals (rate, 44100.0);
            expectEquals (bits, 16);
            expectEquals (m.getNumSamples(), (int) std::ceil ((2.0 + 4.0) * 44100.0));
            expectLessOrEqual (m.getMagnitude (0, m.getNumSamples()), juce::Decibels::decibelsToGain (-1.0f) + 1.0e-3f);
        }

        beginTest ("a muted track is left out of the export");
        {
            hf::ForgeEngine p2 (true);
            p2.setParam (E::Ids::track (1, E::kMute), 1.0f);
            hf::ExportOptions o;
            o.folder = folder.getChildFile ("muted");
            o.mix = hf::ExportOptions::Mix::none;
            o.midi = false;
            hf::ExportResult res;
            juce::String err;
            expect (hf::exporter::run (s, p2.getAPVTS().copyState(), insts, o, res, err), err);
            expectEquals (res.files.size(), 1);
        }
        folder.deleteRecursively();
    }
};

static ExportTests exportTests;
} // namespace
