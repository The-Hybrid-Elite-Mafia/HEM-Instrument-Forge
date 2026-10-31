#include "TestLibrary.h"
#include "Library/UserSounds.h"
#include "Sampler/VoicePool.h"
#include <cmath>

namespace
{
using hemtest::TestLibrary;
namespace us = hf::usersounds;

/** A plucked-string-like test tone: three harmonics, decaying. `cents` detunes it from the nominal note. */
juce::AudioBuffer<float> tone (double hz, double rate, double seconds, int channels = 2, double amp = 0.5)
{
    const int n = (int) (seconds * rate);
    juce::AudioBuffer<float> b (channels, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        double v = 0.0;
        const double amps[3] = { 1.0, 0.5, 0.25 };
        for (int h = 0; h < 3; ++h) v += amps[h] * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (h + 1) * t);
        v *= amp / 1.75 * std::exp (-t / 1.5);
        for (int ch = 0; ch < channels; ++ch) b.setSample (ch, i, (float) v);
    }
    return b;
}

juce::AudioBuffer<float> hit (double hz, double rate, double seconds, double amp)
{
    const int n = (int) (seconds * rate);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        const float v = (float) (amp * std::exp (-t / 0.06) * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}


/** A 120 BPM test groove, `bars` bars of 2 s: kick on beats 1 and 3, snare on 2 and 4, a hat on every eighth. Fills `truth` with {time, class 0/1/2}. */
juce::AudioBuffer<float> groove (int bars, double rate, std::vector<std::pair<double, int>>& truth)
{
    const int n = (int) ((bars * 2.0 + 0.6) * rate);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (7);
    float lp = 0.0f;
    auto add = [&] (double t0, int cls)
    {
        truth.push_back ({ t0, cls });
        const int s0 = (int) (t0 * rate);
        const int len = (int) ((cls == 0 ? 0.35 : cls == 1 ? 0.25 : 0.08) * rate);
        for (int i = 0; i < len && s0 + i < n; ++i)
        {
            const double t = i / rate;
            double v;
            if (cls == 0) v = 0.9 * std::exp (-t / 0.09) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-t / 0.02)) * t);
            else if (cls == 1) v = std::exp (-t / 0.07) * (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 190.0 * t) + 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 420.0 * t) + 0.08 * (rng.nextFloat() * 2.0f - 1.0f));
            else { const float nz = rng.nextFloat() * 2.0f - 1.0f; lp += 0.15f * (nz - lp); v = 0.4 * std::exp (-t / 0.02) * (nz - lp); }   // noise with the low end taken off, like a hat
            for (int ch = 0; ch < 2; ++ch) b.setSample (ch, s0 + i, b.getSample (ch, s0 + i) + (float) v);
        }
    };
    for (int bar = 0; bar < bars; ++bar)
        for (int e = 0; e < 8; ++e)
        {
            const double t = bar * 2.0 + e * 0.25 + 0.3;      // 0.3 s of lead-in
            if (e == 0 || e == 4) add (t, 0);
            else if (e == 2 || e == 6) add (t, 1);
            else add (t, 2);
        }
    return b;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate, int bits = 24)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (b.getNumChannels()).withBitsPerSample (bits));
    if (w == nullptr) return false;
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Rig
{
    hf::DiskStreamer disk;
    hf::VoicePool pool;
    hf::TrackPlayState ts;
    juce::AudioBuffer<float> buf { 2, 256 };
    std::vector<float> outL;

    explicit Rig (const hf::LoadedInstrument* inst)
    {
        disk.prepare (false);
        pool.prepare (TestLibrary::kRate, 256, disk);
        ts.instrument = inst;
    }
    ~Rig() { pool.allOff (-1, true); render (256); disk.shutdown(); }
    void play (int key, int vel, juce::uint32 id)
    {
        hf::VoicePool::NoteOn n;
        n.track = 0; n.key = key; n.velocity = vel; n.noteId = id; n.articulation = 0; n.velTrack = 0.0f;
        pool.noteOn (n, ts);
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
        }
    }
};

/** Pitch in Hz from rising zero crossings, using the span between the first and last crossing (sub-sample accurate). */
double crossFreq (const std::vector<float>& x, int from, int to)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
        {
            const double frac = -x[(size_t) i - 1] / (double) (x[(size_t) i] - x[(size_t) i - 1]);
            const double pos = (i - 1) + frac;
            if (first < 0.0) first = pos;
            last = pos;
            ++count;
        }
    return count < 3 ? 0.0 : (count - 1) * TestLibrary::kRate / (last - first);
}

double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }

class UserSoundsTests final : public juce::UnitTest
{
public:
    UserSoundsTests() : juce::UnitTest ("My Sounds", "Forge") {}

    void runTest() override
    {
        auto work = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("hem-mysounds-test", {}, false);
        work.createDirectory();
        const auto mine = work.getChildFile ("My Sounds");
        const auto src = work.getChildFile ("src");
        src.createDirectory();

        beginTest ("pitch finder: within 3 cents from 82 Hz to 1 kHz");
        {
            double worst = 0.0;
            for (double hz : { 82.41, 110.0, 196.0, 261.63, 440.0, 659.26, 1000.0 })
            {
                auto b = tone (hz, 48000.0, 2.5, 1);
                float conf = 0.0f;
                const double found = us::detectPitchHz (b.getReadPointer (0), b.getNumSamples(), 48000.0, conf);
                const double err = found > 0.0 ? std::abs (centsBetween (found, hz)) : 9999.0;
                worst = juce::jmax (worst, err);
                logMessage (juce::String (hz, 2) + " Hz -> " + juce::String (found, 2) + " Hz, " + juce::String (err, 2) + " cents, confidence " + juce::String (conf, 3));
            }
            expectLessThan (worst, 3.0);
        }

        beginTest ("note names are read");
        {
            expectEquals (us::parseNoteName ("C4"), 60);
            expectEquals (us::parseNoteName ("e2"), 40);
            expectEquals (us::parseNoteName ("A#3"), 58);
            expectEquals (us::parseNoteName ("Bb3"), 58);
            expectEquals (us::parseNoteName ("C-1"), 0);
            expectEquals (us::parseNoteName ("G9"), 127);
            expectEquals (us::parseNoteName ("52"), 52);
            expectEquals (us::parseNoteName ("H2"), -1);
            expectEquals (us::parseNoteName ("G#9"), -1);
            expectEquals (us::parseNoteName (""), -1);
            expectEquals (us::parseNoteName ("200"), -1);
            for (int n = 0; n < 128; ++n) expectEquals (us::parseNoteName (hf::noteName (n)), n);   // round trip with the app's own names
        }

        beginTest ("pitch finder: noise and silence give no pitch");
        {
            juce::Random rnd (7);
            std::vector<float> noise (120000);
            for (auto& v : noise) v = rnd.nextFloat() * 0.6f - 0.3f;
            float conf = 0.0f;
            expectEquals (us::detectPitchHz (noise.data(), (int) noise.size(), 48000.0, conf), 0.0);
            std::vector<float> quiet (120000, 0.0f);
            expectEquals (us::detectPitchHz (quiet.data(), (int) quiet.size(), 48000.0, conf), 0.0);
        }

        beginTest ("a 44.1 kHz guitar-like note becomes an instrument that plays in tune");
        {
            // A3 = 220 Hz, recorded 30 cents sharp, at 44.1 kHz, mono 16-bit
            const double sharp = 220.0 * std::pow (2.0, 0.30 / 12.0);
            const auto f = src.getChildFile ("my guitar A3.wav");
            expect (writeWav (f, tone (sharp, 44100.0, 2.5, 1), 44100.0, 16));
            auto r = us::importPitched ({ f }, "My Guitar", -1, mine);
            expect (r.ok, r.error);
            logMessage (r.report);
            expectEquals (r.id, juce::String ("mine-my-guitar"));

            TestLibrary tl;
            hf::Library lib = tl.library();
            expectEquals (lib.addFolder (mine), 1);
            const auto* e = lib.find ("mine-my-guitar");
            expect (e != nullptr && e->mine && e->family == "My Sounds" && e->kind == "pitched");
            expect (lib.getFamilies().contains ("My Sounds"));
            if (e != nullptr)
            {
                juce::String err;
                auto def = hf::InstrumentDef::load (lib.folderOf (*e), err);
                expect (def != nullptr, err);
                auto li = hf::LoadedInstrument::load (def, err);
                expect (li != nullptr, err);
                if (li != nullptr)
                {
                    expectEquals (def->articulations[0].zones[0].root, 57);
                    {
                        Rig rig (li.get());
                        rig.play (57, 100, 1);
                        rig.render (48000);
                        const double hz = crossFreq (rig.outL, 2000, 40000);
                        const double err57 = centsBetween (hz, 220.0);
                        logMessage ("key A3 plays " + juce::String (hz, 3) + " Hz, " + juce::String (err57, 2) + " cents from 220.000 Hz");
                        expectLessThan (std::abs (err57), 5.0);   // source was 30 cents out; corrected to well inside 5
                    }
                    {
                        Rig rig (li.get());
                        rig.play (60, 100, 2);   // 3 semitones up
                        rig.render (48000);
                        const double hz = crossFreq (rig.outL, 2000, 40000);
                        const double want = 220.0 * std::pow (2.0, 3.0 / 12.0);
                        const double c = centsBetween (hz, want);
                        logMessage ("key C4 plays " + juce::String (hz, 3) + " Hz, " + juce::String (c, 2) + " cents from " + juce::String (want, 3) + " Hz");
                        expectLessThan (std::abs (c), 5.0);
                    }
                }
            }
        }

        beginTest ("the stored sample is 48 kHz, 24-bit and its loudest peak is -3 dBFS");
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (mine.getChildFile ("mine-my-guitar").getChildFile ("n57.wav")));
            expect (r != nullptr);
            if (r != nullptr)
            {
                expectEquals (r->sampleRate, 48000.0);
                expectEquals ((int) r->bitsPerSample, 24);
                expectEquals ((int) r->numChannels, 2);
                juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                const double db = juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()));
                logMessage ("stored peak " + juce::String (db, 3) + " dBFS, " + juce::String ((int) r->lengthInSamples) + " frames");
                expectWithinAbsoluteError (db, -3.0, 0.05);
                // 2.5 s at 44.1 kHz -> 2.5 s at 48 kHz (within the 2 ms lead and 1 frame)
                expectWithinAbsoluteError ((double) r->lengthInSamples / 48000.0, 2.5, 0.01);
            }
        }

        beginTest ("several notes: ranges meet halfway, same note makes round-robin takes");
        {
            const auto a = src.getChildFile ("e2.wav"), b = src.getChildFile ("a2.wav"), c = src.getChildFile ("a2 again.wav");
            expect (writeWav (a, tone (82.407, 48000.0, 2.5, 2), 48000.0));
            expect (writeWav (b, tone (110.0, 48000.0, 2.5, 2), 48000.0));
            expect (writeWav (c, tone (110.0, 48000.0, 2.5, 2, 0.4), 48000.0));
            auto r = us::importPitched ({ a, b, c }, "Bass Takes", -1, mine);
            expect (r.ok, r.error);
            logMessage (r.report);
            juce::String err;
            auto def = hf::InstrumentDef::load (mine.getChildFile (r.id), err);
            expect (def != nullptr, err);
            if (def != nullptr)
            {
                auto& z = def->articulations[0].zones;
                expectEquals ((int) z.size(), 3);
                // E2 = 40, A2 = 45. E2 covers 28..42, A2 covers 43..57
                expectEquals (z[0].root, 40); expectEquals (z[0].lo, 28); expectEquals (z[0].hi, 42);
                expectEquals (z[1].root, 45); expectEquals (z[1].lo, 43); expectEquals (z[1].hi, 57);
                expectEquals (z[1].rrCount, 2); expectEquals (z[1].rr, 1); expectEquals (z[2].rr, 2);
                expectEquals (def->rangeLo, 28); expectEquals (def->rangeHi, 57);
            }
        }

        beginTest ("drum kit: names decide the keys, unnamed files take a free key, hits play back exactly");
        {
            const auto kick = src.getChildFile ("Kick 01.wav"), snare = src.getChildFile ("my_snare.wav"),
                       hat = src.getChildFile ("Hat Open.wav"), odd = src.getChildFile ("take 7.wav");
            auto kb = hit (60.0, 48000.0, 0.4, 0.8), sb = hit (190.0, 48000.0, 0.3, 0.4);
            expect (writeWav (kick, kb, 48000.0)); expect (writeWav (snare, sb, 48000.0));
            expect (writeWav (hat, hit (7000.0, 48000.0, 0.2, 0.2), 48000.0)); expect (writeWav (odd, hit (500.0, 48000.0, 0.2, 0.3), 48000.0));
            expectEquals (us::drumNoteForName ("Kick 01.wav"), 36);
            expectEquals (us::drumNoteForName ("my_snare.wav"), 38);
            expectEquals (us::drumNoteForName ("Hat Open.wav"), 46);
            expectEquals (us::drumNoteForName ("Hat closed.wav"), 42);
            expectEquals (us::drumNoteForName ("take 7.wav"), -1);
            auto r = us::importKit ({ kick, snare, hat, odd }, "My Drums", mine);
            expect (r.ok, r.error);
            logMessage (r.report);
            juce::String err;
            auto def = hf::InstrumentDef::load (mine.getChildFile (r.id), err);
            expect (def != nullptr, err);
            auto li = hf::LoadedInstrument::load (def, err);
            expect (li != nullptr, err);
            if (li != nullptr)
            {
                expect (def->isDrums());
                expectEquals (def->rangeLo, 36); expectEquals (def->rangeHi, 46);
                // loudest file (the kick, peak 0.8) is scaled to -3 dBFS; the snare keeps its 0.5 ratio to it
                const float g = juce::Decibels::decibelsToGain (-3.0f) / 0.8f;
                Rig rig (li.get());
                rig.play (38, 127, 1);
                rig.render (14400);
                double dot = 0.0, e1 = 0.0, e2 = 0.0, maxErr = 0.0;
                for (int i = 100; i < 14000; ++i)
                {
                    const double want = sb.getSample (0, i) * g;
                    dot += want * rig.outL[(size_t) i]; e1 += want * want; e2 += (double) rig.outL[(size_t) i] * rig.outL[(size_t) i];
                }
                const double corr = dot / std::sqrt (e1 * e2 + 1e-30);
                const double ratio = std::sqrt (e2 / e1);
                logMessage ("snare playback: correlation " + juce::String (corr, 6) + ", level ratio to the file " + juce::String (ratio, 4));
                expectGreaterThan (corr, 0.9999);
                juce::ignoreUnused (maxErr);
                // level: velocity 127 through the engine's own curve; the ratio must be a fixed number close to 1 (reported, not assumed)
                expectGreaterThan (ratio, 0.5); expectLessThan (ratio, 1.5);
                // unnamed file went to the first free key after 36 (kick) = 37
                bool has37 = false;
                for (auto& z : def->articulations[0].zones) if (z.lo == 37) has37 = true;
                expect (has37);
            }
        }

        beginTest ("bad input is refused with a reason and leaves nothing behind");
        {
            const auto before = mine.findChildFiles (juce::File::findDirectories, false).size();
            const auto silent = src.getChildFile ("silent.wav");
            juce::AudioBuffer<float> z (2, 48000);
            z.clear();
            expect (writeWav (silent, z, 48000.0));
            auto r = us::importPitched ({ silent }, "Silent", -1, mine);
            expect (! r.ok && r.error.contains ("silent"), r.error);
            expect (! us::importKit ({}, "Nothing", mine).ok);
            expect (! us::importPitched ({ src.getChildFile ("missing.wav") }, "Missing", -1, mine).ok);
            const auto txt = src.getChildFile ("notes.wav");
            txt.replaceWithText ("this is not audio");
            expect (! us::importKit ({ txt }, "Text", mine).ok);
            const auto a = src.getChildFile ("e2.wav"), b = src.getChildFile ("a2.wav");
            expect (! us::importPitched ({ a, b }, "Two on one key", 40, mine).ok);
            // a noisy take: pitch not found -> asks for the note, and works once the note is set
            juce::AudioBuffer<float> nz (2, 96000);
            juce::Random rnd (3);
            for (int i = 0; i < 96000; ++i) { const float v = (rnd.nextFloat() - 0.5f) * 0.5f; nz.setSample (0, i, v); nz.setSample (1, i, v); }
            const auto noisy = src.getChildFile ("noise.wav");
            expect (writeWav (noisy, nz, 48000.0));
            auto rn = us::importPitched ({ noisy }, "Noisy", -1, mine);
            expect (! rn.ok && rn.error.contains ("Pick the note"), rn.error);
            expectEquals (mine.findChildFiles (juce::File::findDirectories, false).size(), before);
            auto rs = us::importPitched ({ noisy }, "Noisy", 52, mine);
            expect (rs.ok, rs.error);
        }

        beginTest ("same name twice does not overwrite; remove deletes only My Sounds instruments");
        {
            const auto f = src.getChildFile ("e2.wav");
            auto r1 = us::importPitched ({ f }, "Twin", -1, mine);
            auto r2 = us::importPitched ({ f }, "Twin", -1, mine);
            expect (r1.ok && r2.ok);
            expect (r1.id != r2.id);
            TestLibrary tl;
            hf::Library lib = tl.library();
            lib.addFolder (mine);
            expect (lib.find (r1.id) != nullptr && lib.find (r2.id) != nullptr);
            expect (us::remove (r1.id, mine));
            expect (! mine.getChildFile (r1.id).exists());
            hf::Library lib2 = tl.library();
            lib2.addFolder (mine);
            expect (lib2.find (r1.id) == nullptr && lib2.find (r2.id) != nullptr);
            expect (! us::remove ("test-sine", mine));       // not one of mine
            expect (! us::remove ("mine-../x", mine));
            expectEquals ((int) lib2.getEntries().size(), (int) tl.library().getEntries().size() + (int) mine.findChildFiles (juce::File::findDirectories, false).size());
        }

        beginTest ("a My Sounds instrument can not hide a library instrument with the same id");
        {
            const auto dir = mine.getChildFile ("clash");
            dir.createDirectory();
            const auto f = src.getChildFile ("e2.wav");
            auto r = us::importPitched ({ f }, "Clash", -1, mine);
            expect (r.ok);
            auto idx = juce::JSON::parse (mine.getChildFile ("index.json"));
            auto text = juce::JSON::toString (idx).replace ("\"" + r.id + "\"", "\"test-sine\"");
            const auto copy = work.getChildFile ("clashmine");
            mine.copyDirectoryTo (copy);
            copy.getChildFile ("index.json").replaceWithText (text);
            TestLibrary tl;
            hf::Library lib = tl.library();
            lib.addFolder (copy);
            const auto* e = lib.find ("test-sine");
            expect (e != nullptr && ! e->mine);
        }


        beginTest ("cutter: finds the hits of a 120 BPM groove, its tempo and the drum types");
        {
            std::vector<std::pair<double, int>> truth;
            auto g = groove (8, 48000.0, truth);
            std::vector<float> m ((size_t) g.getNumSamples());
            for (int i = 0; i < g.getNumSamples(); ++i) m[(size_t) i] = g.getSample (0, i);
            const auto on = us::findOnsets (m.data(), (int) m.size(), 48000.0);
            // the hat that shares an eighth with nothing else: all 8 per bar are separate hits at 250 ms spacing, so every one must be found
            expectEquals ((int) on.size(), (int) truth.size());
            double worst = 0.0;
            for (size_t i = 0; i < juce::jmin (on.size(), truth.size()); ++i) worst = juce::jmax (worst, std::abs (on[i] / 48000.0 - truth[i].first));
            { juce::String miss; for (auto& tr : truth) { bool f = false; for (int o : on) if (std::abs (o / 48000.0 - tr.first) < 0.02) f = true; if (! f) miss << juce::String (tr.first, 2) << "(" << tr.second << ") "; } logMessage ("missed: " + miss); }
            logMessage ("hit count " + juce::String ((int) on.size()) + " of " + juce::String ((int) truth.size()) + ", worst timing error " + juce::String (worst * 1000.0, 2) + " ms");
            expectLessThan (worst, 0.005);
            float conf = 0.0f;
            const double bpm = us::detectBpm (m.data(), (int) m.size(), 48000.0, conf);
            logMessage ("tempo found " + juce::String (bpm, 3) + " BPM, confidence " + juce::String (conf, 2));
            expectWithinAbsoluteError (bpm, 120.0, 1.0);
            int right = 0, cm[3][3] = {};
            for (size_t i = 0; i < juce::jmin (on.size(), truth.size()); ++i)
            {
                const int nextS = i + 1 < on.size() ? on[i + 1] : (int) m.size();
                const auto c = us::classifyHit (m.data() + on[i], juce::jmin (nextS - on[i], 72000), 48000.0);
                if ((int) c == truth[i].second) ++right;
                ++cm[truth[i].second][(int) c];
            }
            logMessage ("test groove, rows = kick/snare/hat, columns = found kick/snare/hat: " + juce::String (cm[0][0]) + " " + juce::String (cm[0][1]) + " " + juce::String (cm[0][2]) + " | " + juce::String (cm[1][0]) + " " + juce::String (cm[1][1]) + " " + juce::String (cm[1][2]) + " | " + juce::String (cm[2][0]) + " " + juce::String (cm[2][1]) + " " + juce::String (cm[2][2]));
            logMessage ("drum types right: " + juce::String (right) + " of " + juce::String ((int) truth.size()));
            expectEquals (right, (int) truth.size());
        }

        beginTest ("cutter: kit from hits, loops by bar length, notes by pitch");
        {
            std::vector<std::pair<double, int>> truth;
            auto g = groove (8, 48000.0, truth);
            const auto song = src.getChildFile ("groove.wav");
            expect (writeWav (song, g, 48000.0));
            auto rk = us::importSliced (song, "Cut Kit", us::Mode::sliceHits, {}, mine);
            expect (rk.ok, rk.error);
            logMessage (rk.report);
            if (rk.ok)
            {
                juce::String err;
                auto def = hf::InstrumentDef::load (mine.getChildFile (rk.id), err);
                expect (def != nullptr, err);
                if (def != nullptr)
                {
                    bool k36 = false, k38 = false, k42 = false, allOne = true;
                    for (auto& z : def->articulations[0].zones) { if (z.lo <= 36 && z.hi >= 36) k36 = true; if (z.lo <= 38 && z.hi >= 38) k38 = true; if (z.lo <= 42 && z.hi >= 42) k42 = true; allOne = allOne && z.oneShot; }
                    for (auto& z : def->articulations[0].zones) logMessage ("zone " + juce::String (z.lo) + "-" + juce::String (z.hi) + " oneShot " + juce::String ((int) z.oneShot));
                    expect (k36 && k38 && k42);
                    expect (allOne);
                    // a kept kick is exactly one of the kick hits: it plays through when the key is let go after 50 ms
                    auto li = hf::LoadedInstrument::load (def, err);
                    expect (li != nullptr, err);
                    if (li != nullptr)
                    {
                        Rig rig (li.get());
                        rig.play (36, 127, 1);
                        rig.render (2400);                       // 50 ms
                        rig.pool.noteOff (0, 1, 0, rig.ts);
                        rig.render (12000);
                        double early = 0.0, late = 0.0;
                        for (int i = 100; i < 1100; ++i) early += (double) rig.outL[(size_t) i] * rig.outL[(size_t) i];
                        for (int i = 3000; i < 4000; ++i) late += (double) rig.outL[(size_t) i] * rig.outL[(size_t) i];
                        logMessage ("kick after key-up at 50 ms: energy 62-83 ms is " + juce::String (10.0 * std::log10 (late / (early + 1e-30)), 1) + " dB against the first 20 ms");
                        // the kick body decays with 90 ms: at 62-83 ms it must still be well above silence (a normal note would be fading out on a 0.1 s release)
                        expectGreaterThan (late, early * 0.005);
                        expectGreaterThan (late, 1.0e-6);
                    }
                }
            }
            // loops: 1 bar at 120 BPM is exactly 2.000 s
            us::SliceOptions lo; lo.bpm = 120.0; lo.bars = 1; lo.maxSlices = 4;
            auto rl = us::importSliced (song, "Cut Loops", us::Mode::sliceLoops, lo, mine);
            expect (rl.ok, rl.error);
            logMessage (rl.report);
            if (rl.ok)
            {
                juce::String err;
                auto def = hf::InstrumentDef::load (mine.getChildFile (rl.id), err);
                expect (def != nullptr, err);
                if (def != nullptr)
                {
                    expectEquals ((int) def->articulations[0].zones.size(), 4);
                    int worstOff = 0;
                    for (auto& z : def->articulations[0].zones) worstOff = juce::jmax (worstOff, std::abs (z.frames - 96000));
                    logMessage ("loop length error: " + juce::String (worstOff) + " frames of 96000");
                    expectLessThan (worstOff, 200);        // the fade at the end trims nothing; allow 4 ms
                }
            }
            // notes: four separate plucked notes a second apart, found by pitch
            juce::AudioBuffer<float> seq (2, 48000 * 5);
            seq.clear();
            const double hzs[4] = { 110.0, 146.83, 196.0, 261.63 };
            for (int k = 0; k < 4; ++k)
            {
                auto tn = tone (hzs[k], 48000.0, 0.7, 2, 0.5);
                for (int i = 0; i < tn.getNumSamples(); ++i) for (int ch = 0; ch < 2; ++ch) seq.setSample (ch, 24000 + k * 48000 + i, tn.getSample (ch, i));
            }
            const auto seqFile = src.getChildFile ("plucks.wav");
            expect (writeWav (seqFile, seq, 48000.0));
            us::SliceOptions no; no.maxSlices = 8;
            auto rn = us::importSliced (seqFile, "Cut Notes", us::Mode::sliceNotes, no, mine);
            expect (rn.ok, rn.error);
            logMessage (rn.report);
            if (rn.ok)
            {
                juce::String err;
                auto def = hf::InstrumentDef::load (mine.getChildFile (rn.id), err);
                expect (def != nullptr, err);
                if (def != nullptr)
                {
                    std::vector<int> roots;
                    for (auto& z : def->articulations[0].zones) roots.push_back (z.root);
                    std::sort (roots.begin(), roots.end());
                    expectEquals ((int) roots.size(), 4);
                    if (roots.size() == 4) { expectEquals (roots[0], 45); expectEquals (roots[1], 50); expectEquals (roots[2], 55); expectEquals (roots[3], 60); }
                }
            }
        }

        // Real-music check, only when HEM_STEM_WAV and HEM_STEM_TRUTH are set (a Suno stem and the hit list taken from its MIDI file).
        if (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_WAV", {}).isNotEmpty())
        {
            beginTest ("real drum stem: onsets, tempo and drum types against the MIDI that came with it");
            const juce::File wavFile (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_WAV", {}));
            const auto truth = juce::JSON::parse (juce::File (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_TRUTH", {})));
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wavFile));
            expect (r != nullptr);
            if (r != nullptr && truth.isObject())
            {
                juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                std::vector<float> mono (b.getNumSamples());
                for (int i = 0; i < b.getNumSamples(); ++i) mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
                const double rate = r->sampleRate;
                const auto on = us::findOnsets (mono.data(), (int) mono.size(), rate);
                { juce::String dump; for (int s : on) dump << juce::String (s / rate, 4) << "\n";
                  juce::File (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_DUMP", "/tmp/onsets.txt")).replaceWithText (dump); }
                int matched = 0, total = 0, classOk = 0, classN = 0; int conf3[3][3] = {};
                std::vector<bool> used (on.size(), false);
                auto& events = *truth["events"].getArray();
                for (auto& ev : events)
                {
                    const double t = (double) (*ev.getArray())[0] + juce::SystemStats::getEnvironmentVariable ("HEM_STEM_OFFSET", "0").getDoubleValue();
                    const int note = (int) (*ev.getArray())[1];
                    if (note != 36 && note != 38 && note != 42) continue;   // kick, snare, hat (the shaker, 70, is not one of ours)
                    ++total;
                    int bestIdx = -1; double bestD = 0.045;
                    for (size_t i = 0; i < on.size(); ++i)
                    {
                        const double d = std::abs (on[i] / rate - t);
                        if (d < bestD) { bestD = d; bestIdx = (int) i; }
                    }
                    if (bestIdx < 0) continue;
                    ++matched;
                    // classify the way the kit builder does: from the hit to the next found hit (at most 1.5 s); only hits with 150 ms or more of room
                    const int s = on[(size_t) bestIdx];
                    const int nextS = (size_t) bestIdx + 1 < on.size() ? on[(size_t) bestIdx + 1] : (int) mono.size();
                    const int win = juce::jmin (nextS - s, (int) (1.5 * rate));
                    if (win >= (int) (0.15 * rate))
                    {
                        const auto c = us::classifyHit (mono.data() + s, win, rate);
                        const int want = note == 36 ? 0 : note == 38 ? 1 : 2;
                        ++classN;
                        if ((int) c == want) ++classOk;
                        ++conf3[want][(int) c];
                    }
                }
                logMessage ("rows = MIDI kick/snare/hat, columns = found kick/snare/hat: " + juce::String (conf3[0][0]) + " " + juce::String (conf3[0][1]) + " " + juce::String (conf3[0][2]) + " | " + juce::String (conf3[1][0]) + " " + juce::String (conf3[1][1]) + " " + juce::String (conf3[1][2]) + " | " + juce::String (conf3[2][0]) + " " + juce::String (conf3[2][1]) + " " + juce::String (conf3[2][2]));
                float conf = 0.0f;
                const double bpm = us::detectBpm (mono.data(), (int) mono.size(), rate, conf);
                logMessage ("onsets found " + juce::String ((int) on.size()) + "; MIDI kick/snare/hat " + juce::String (total) + ", matched within 45 ms: " + juce::String (matched)
                            + "; alone hits typed right: " + juce::String (classOk) + " of " + juce::String (classN)
                            + "; tempo found " + juce::String (bpm, 2) + " BPM (MIDI says " + juce::String ((double) (*truth["bpm"].getArray())[0], 2) + "), confidence " + juce::String (conf, 2));
                // the kit it builds
                auto r2 = us::importSliced (wavFile, "Real Drums", us::Mode::sliceHits, {}, mine);
                expect (r2.ok, r2.error);
                logMessage (r2.report);
                auto r3 = us::importSliced (wavFile, "Real Loops", us::Mode::sliceLoops, { 0.0, 1, 8 }, mine);
                expect (r3.ok, r3.error);
                logMessage (r3.report);
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_GUITAR", {}).isNotEmpty())
        {
            beginTest ("real guitar stem: cut into notes");
            us::SliceOptions go; go.maxSlices = 12;
            auto rg = us::importSliced (juce::File (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_GUITAR", {})), "Real Guitar", us::Mode::sliceNotes, go, mine);
            logMessage (rg.ok ? rg.report : rg.error);
            auto rg2 = us::importSliced (juce::File (juce::SystemStats::getEnvironmentVariable ("HEM_STEM_GUITAR", {})), "Real Guitar Loops", us::Mode::sliceLoops, { 0.0, 2, 6 }, mine);
            logMessage (rg2.ok ? rg2.report : rg2.error);
        }

        work.deleteRecursively();
    }
};
static UserSoundsTests userSoundsTests;
} // namespace
