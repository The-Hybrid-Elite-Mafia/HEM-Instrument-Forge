#include "TestLibrary.h"
#include "Sampler/VoicePool.h"
#include <cmath>

namespace
{
using hemtest::TestLibrary;

/** One track rendered through a voice pool with the disk serviced every block (deterministic). */
struct Rig
{
    hf::DiskStreamer disk;
    hf::VoicePool pool;
    hf::TrackPlayState ts;
    juce::AudioBuffer<float> buf { 2, 256 };
    std::vector<float> outL, outR;

    explicit Rig (const hf::LoadedInstrument* inst)
    {
        disk.prepare (false);
        pool.prepare (TestLibrary::kRate, 256, disk);
        ts.instrument = inst;
    }
    ~Rig() { pool.allOff (-1, true); render (256); disk.shutdown(); }

    hf::VoicePool::NoteOn note (int key, int vel, juce::uint32 id, int art = 0)
    {
        hf::VoicePool::NoteOn n;
        n.track = 0; n.key = key; n.velocity = vel; n.noteId = id; n.articulation = art; n.velTrack = 0.0f;
        return n;
    }
    void render (int samples)
    {
        for (int done = 0; done < samples; done += 256)
        {
            const int n = juce::jmin (256, samples - done);
            disk.serviceAll();
            buf.clear();
            float* l = buf.getWritePointer (0);
            float* r = buf.getWritePointer (1);
            pool.render (&l, &r, 1, n, &ts);
            outL.insert (outL.end(), l, l + n);
            outR.insert (outR.end(), r, r + n);
        }
    }
    float peak (int from, int to) const
    {
        float p = 0.0f;
        for (int i = juce::jmax (0, from); i < juce::jmin ((int) outL.size(), to); ++i) p = juce::jmax (p, std::abs (outL[(size_t) i]));
        return p;
    }
};

double zeroCrossFreq (const std::vector<float>& x, int from, int to)
{
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f) ++crossings;
    return crossings * TestLibrary::kRate / (to - from);
}

class SamplerTests final : public juce::UnitTest
{
public:
    SamplerTests() : juce::UnitTest ("Sampler", "Forge") {}

    void runTest() override
    {
        TestLibrary tl;
        auto sine = tl.load ("test-sine");

        beginTest ("a note plays its sample exactly, across the head / disk-stream join");
        {
            Rig rig (sine.get());
            rig.pool.noteOn (rig.note (60, 40, 1), rig.ts);
            rig.render ((int) (2.9 * TestLibrary::kRate));
            float maxErr = 0.0f;
            for (int i = 200; i < (int) rig.outL.size(); ++i)
                maxErr = juce::jmax (maxErr, std::abs (rig.outL[(size_t) i] - TestLibrary::sineSample (60, 0, i)));
            expectLessThan (maxErr, 2.0e-4f);
            expectEquals (rig.disk.underruns.load(), 0);
            logMessage ("max error over 2.9 s (head is " + juce::String (hf::LoadedInstrument::kHeadFrames) + " frames): " + juce::String (maxErr, 7));
        }

        beginTest ("other keys are pitched from the nearest zone");
        {
            Rig rig (sine.get());
            rig.pool.noteOn (rig.note (62, 40, 1), rig.ts);
            rig.render (48000);
            const double f = zeroCrossFreq (rig.outL, 2000, 46000);
            const double want = TestLibrary::sineFreq (62);
            expectWithinAbsoluteError (f, want, want * 0.004);
        }

        beginTest ("velocity picks the layer");
        {
            for (int vel : { 30, 110 })
            {
                Rig rig (sine.get());
                rig.pool.noteOn (rig.note (60, vel, 1), rig.ts);
                rig.render (4000);
                float err0 = 0.0f, err1 = 0.0f;
                for (int i = 200; i < 4000; ++i)
                {
                    err0 = juce::jmax (err0, std::abs (rig.outL[(size_t) i] - TestLibrary::sineSample (60, 0, i)));
                    err1 = juce::jmax (err1, std::abs (rig.outL[(size_t) i] - TestLibrary::sineSample (60, 1, i)));
                }
                if (vel < 64) expectLessThan (err0, 1.0e-3f); else expectLessThan (err1, 1.0e-3f);
            }
        }

        beginTest ("held notes loop seamlessly past the end of the sample");
        {
            Rig rig (sine.get());
            rig.pool.noteOn (rig.note (60, 100, 1, 2), rig.ts);
            rig.render ((int) (6.0 * TestLibrary::kRate));
            const double w = 2.0 * juce::MathConstants<double>::pi * TestLibrary::sineFreq (60) / TestLibrary::kRate;
            float maxStep = 0.0f;
            for (int i = 1000; i < (int) rig.outL.size(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (rig.outL[(size_t) i] - rig.outL[(size_t) i - 1]));
            // the test loop is cut at a whole number of samples, so the join may be up to half a
            // sample out of phase (the library builder cross-fades its loops; this file has none)
            expectLessThan ((double) maxStep, 0.5 * w * 1.35);
            expectGreaterThan (rig.peak ((int) (5.5 * TestLibrary::kRate), (int) (6.0 * TestLibrary::kRate)), 0.45f);
            expectEquals (rig.disk.underruns.load(), 0);
        }

        beginTest ("release falls 60 dB in the release time");
        {
            Rig rig (sine.get());
            rig.pool.noteOn (rig.note (60, 40, 1), rig.ts);   // articulation 0: release 0.1 s
            rig.render (9600);
            rig.pool.noteOff (0, 1, 0, rig.ts);
            rig.render (9600);
            const float before = rig.peak (8000, 9600);
            const float after = rig.peak (9600 + 4700, 9600 + 5000);   // just under 0.1 s later
            expect (after < before * 0.003f && after > before * 0.0003f);
            expectEquals (rig.pool.activeVoices(), 0);
        }

        beginTest ("round robin cycles through its samples");
        {
            auto rr = tl.load ("test-rr");
            Rig rig (rr.get());
            float peaks[6];
            for (int k = 0; k < 6; ++k)
            {
                rig.outL.clear();
                rig.pool.noteOn (rig.note (60, 100, (juce::uint32) k + 1), rig.ts);
                rig.render (4000);
                peaks[k] = rig.peak (500, 4000);
                rig.pool.allOff (0, true);
                rig.render (512);
            }
            expectWithinAbsoluteError (peaks[0], 0.2f, 0.01f);
            expectWithinAbsoluteError (peaks[1], 0.4f, 0.01f);
            expectWithinAbsoluteError (peaks[2], 0.6f, 0.01f);
            expectWithinAbsoluteError (peaks[3], 0.2f, 0.01f);
        }

        beginTest ("sustain pedal: pedal zones and held notes");
        {
            Rig rig (sine.get());
            rig.ts.pedal = true;
            rig.pool.noteOn (rig.note (60, 100, 1, 3), rig.ts);
            rig.render (3000);
            float err1 = 0.0f;
            for (int i = 200; i < 3000; ++i) err1 = juce::jmax (err1, std::abs (rig.outL[(size_t) i] - TestLibrary::sineSample (60, 1, i)));
            expectLessThan (err1, 1.0e-3f);
            rig.pool.noteOff (0, 1, 0, rig.ts);
            rig.render (9600);
            expectGreaterThan (rig.peak (3000 + 8000, 3000 + 9600), 0.3f);   // still held by the pedal
            rig.ts.pedal = false;
            rig.pool.pedalUp (0);
            rig.render (9600);
            expectEquals (rig.pool.activeVoices(), 0);
        }

        beginTest ("no free disk stream: the note plays its head and fades out cleanly");
        {
            Rig rig (sine.get());
            const auto& z = sine->articulation (0).zones[0];
            std::vector<int> taken;
            for (int i = 0; i < hf::DiskStreamer::kSlots; ++i) taken.push_back (rig.disk.start (&z, sine.get(), z.headFrames));
            expectEquals (rig.disk.start (&z, sine.get(), z.headFrames), -1);
            rig.pool.noteOn (rig.note (48, 40, 1), rig.ts);
            rig.render (hf::LoadedInstrument::kHeadFrames + 4000);
            float maxStep = 0.0f;
            for (int i = 1; i < (int) rig.outL.size(); ++i) maxStep = juce::jmax (maxStep, std::abs (rig.outL[(size_t) i] - rig.outL[(size_t) i - 1]));
            expectLessThan (maxStep, 0.05f);
            expectEquals (rig.pool.activeVoices(), 0);
            for (int s : taken) rig.disk.stop (s);
            rig.disk.serviceAll();
        }

        beginTest ("re-striking a key releases the old note; a closed hi-hat chokes the open one");
        {
            Rig rig (sine.get());
            rig.pool.noteOn (rig.note (60, 40, 1), rig.ts);
            rig.render (2000);
            rig.pool.noteOn (rig.note (60, 40, 2), rig.ts);
            rig.render (9600);
            expectEquals (rig.pool.activeVoices(), 1);

            auto kit = tl.load ("test-kit");
            Rig k (kit.get());
            k.pool.noteOn (k.note (46, 100, 1), k.ts);
            k.render (2000);
            k.pool.noteOn (k.note (42, 100, 2), k.ts);
            k.render (4800);
            expectEquals (k.pool.activeVoices(), 1);
        }

        beginTest ("mono legato: the new note takes over without a fresh attack");
        {
            Rig rig (sine.get());
            auto n1 = rig.note (60, 40, 1);
            n1.mono = true; n1.legato = true;
            rig.pool.noteOn (n1, rig.ts);
            rig.render (4800);
            auto n2 = rig.note (64, 40, 2);
            n2.mono = true; n2.legato = true;
            rig.pool.noteOn (n2, rig.ts);
            rig.render (9600);
            expectEquals (rig.pool.activeVoices(), 1);
            // the tie fades over 50 ms: no sample-to-sample jump bigger than the sine's own slope
            float maxStep = 0.0f;
            for (int i = 4790; i < 4800 + 4800; ++i) maxStep = juce::jmax (maxStep, std::abs (rig.outL[(size_t) i] - rig.outL[(size_t) i - 1]));
            expectLessThan (maxStep, 0.04f);
        }

        beginTest ("notes start at their sample offset inside a block");
        {
            Rig rig (sine.get());
            auto n = rig.note (60, 40, 1);
            n.offset = 100;
            rig.pool.noteOn (n, rig.ts);
            rig.render (256);
            expectEquals (rig.peak (0, 100), 0.0f);
            expectGreaterThan (rig.peak (100, 256), 0.0f);
        }

        beginTest ("note on / off and rendering never allocate");
        {
            Rig rig (sine.get());
            rig.outL.reserve (200000); rig.outR.reserve (200000);
            juce::AudioBuffer<float> b (2, 256);
            float* l = b.getWritePointer (0);
            float* r = b.getWritePointer (1);
            rig.disk.serviceAll();
            hemtest::beginTracking();
            for (int k = 0; k < 40; ++k)
            {
                rig.pool.noteOn (rig.note (40 + k, 30 + k * 2, (juce::uint32) k + 1, k % 3), rig.ts);
                rig.pool.render (&l, &r, 1, 256, &rig.ts);
                if (k > 5) rig.pool.noteOff (0, (juce::uint32) k - 5, 10, rig.ts);
            }
            rig.pool.allOff (-1, false);
            rig.pool.render (&l, &r, 1, 256, &rig.ts);
            const int allocs = hemtest::endTracking();
            expectEquals (allocs, 0);
        }
    }
};

static SamplerTests samplerTests;
} // namespace
