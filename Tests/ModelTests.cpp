#include "TestLibrary.h"
#include "Models/ModelCatalog.h"
#include "Library/InstrumentDef.h"
#include <cmath>

namespace
{
using namespace hf::models;

/** YIN pitch of the mono sum over [from, from + 4096). Returns Hz or 0. */
double yinHz (const juce::AudioBuffer<float>& b, int from, double sr, double minHz = 25.0, double maxHz = 2500.0)
{
    const int W = 4096;
    const int maxLag = juce::jmin (W - 1, (int) (sr / minHz));
    const int minLag = juce::jmax (2, (int) (sr / maxHz));
    if (from + W + maxLag > b.getNumSamples()) return 0.0;
    std::vector<double> x ((size_t) (W + maxLag));
    for (int i = 0; i < W + maxLag; ++i) x[(size_t) i] = b.getSample (0, from + i) + b.getSample (1, from + i);
    std::vector<double> d ((size_t) maxLag + 1, 0.0), cm ((size_t) maxLag + 1, 1.0);
    double run = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i < W; ++i) { const double e = x[(size_t) i] - x[(size_t) (i + tau)]; s += e * e; }
        d[(size_t) tau] = s;
        run += s;
        cm[(size_t) tau] = run > 0.0 ? s * tau / run : 1.0;
    }
    int best = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
        if (cm[(size_t) tau] < 0.15)
        {
            while (tau + 1 < maxLag && cm[(size_t) (tau + 1)] < cm[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        double m = 1.0e9;
        for (int tau = minLag; tau < maxLag; ++tau) if (cm[(size_t) tau] < m) { m = cm[(size_t) tau]; best = tau; }
    }
    if (best <= 0) return 0.0;
    const double a = cm[(size_t) best - 1], c = cm[(size_t) best], e = cm[(size_t) best + 1];
    const double den = a - 2 * c + e;
    const double t = best + (std::abs (den) > 1e-12 ? 0.5 * (a - e) / den : 0.0);
    return sr / t;
}

double centsOff (double hz, int midi) { return 1200.0 * std::log2 (hz / midiToHz (midi)); }

class ModelTests final : public juce::UnitTest
{
public:
    ModelTests() : juce::UnitTest ("Models", "Forge") {}

    void runTest() override
    {
        beginTest ("every modeled instrument makes clean, non-silent sound in every articulation");
        {
            for (auto& m : catalog())
            {
                auto r = renderer (m.model);
                expect ((bool) r, m.id);
                if (! r) continue;
                for (auto& a : m.arts)
                {
                    NoteSpec ns;
                    ns.pitch = m.kind == "drums" ? 38 : (m.lo + m.hi) / 2;
                    ns.velocity = 0.8f;
                    ns.seconds = 1.5;
                    ns.variant = a.variant;
                    const auto b = r (ns);
                    expectEquals (b.getNumChannels(), 2, m.id);
                    bool finite = true;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < b.getNumSamples(); ++i) finite &= std::isfinite (b.getSample (ch, i));
                    expect (finite, m.id + " " + a.name + " has bad samples");
                    const float peak = b.getMagnitude (0, b.getNumSamples());
                    expect (peak > 1.0e-3f, m.id + " " + a.name + " is silent");
                    expect (peak < 8.0f, m.id + " " + a.name + " is far too loud: " + juce::String (peak));
                }
            }
        }

        beginTest ("modeled instruments play in tune");
        {
            // the bell (FM, 3.5:1) has no single period; drums are unpitched
            const juce::StringArray skip { "synth-bell", "kit-trap" };
            int checked = 0;
            double worst = 0.0;
            juce::String worstName;
            for (auto& m : catalog())
            {
                if (skip.contains (m.id)) continue;
                const auto r = renderer (m.model);
                const double tol = m.id.contains ("choir") ? 30.0 : m.id == "jaw-harp" || m.id == "sitar" ? 20.0 : 12.0;
                for (int pitch : { m.lo + (m.hi - m.lo) / 4, m.lo + 3 * (m.hi - m.lo) / 4 })
                {
                    NoteSpec ns;
                    ns.pitch = pitch;
                    ns.velocity = 0.7f;
                    ns.seconds = 1.6;
                    ns.variant = m.arts[0].variant;
                    const auto b = r (ns);
                    const double hz = yinHz (b, (int) (0.45 * ns.sampleRate), ns.sampleRate);
                    expect (hz > 0.0, m.id + " has no pitch");
                    if (hz <= 0.0) continue;
                    // octave-equivalent: a few instruments (12-string octave course, drones) are strong an octave up
                    double c = centsOff (hz, pitch);
                    c -= 1200.0 * std::round (c / 1200.0);
                    expectLessOrEqual (std::abs (c), tol, m.id + " at " + hf::noteName (pitch) + ": " + juce::String (c, 1) + " cents");
                    if (std::abs (c) > worst) { worst = std::abs (c); worstName = m.id + " " + hf::noteName (pitch); }
                    ++checked;
                }
            }
            logMessage ("pitch checked on " + juce::String (checked) + " notes; worst " + juce::String (worst, 1) + " cents (" + worstName + ")");
        }

        beginTest ("the trap kit has a sound on every note it lists");
        {
            const auto* k = find ("kit-trap");
            expect (k != nullptr);
            if (k != nullptr)
                for (int note : k->drumNotes)
                {
                    NoteSpec ns; ns.pitch = note; ns.velocity = 0.9f; ns.seconds = 1.0;
                    const auto b = renderer (k->model) (ns);
                    expect (b.getMagnitude (0, b.getNumSamples()) > 1.0e-3f, "note " + juce::String (note));
                }
        }

        beginTest ("loops are seamless");
        {
            juce::AudioBuffer<float> b (2, 48000 * 4);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 233.0 * i / 48000.0);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            int ls = -1, le = -1;
            expect (makeLoop (b, 48000.0, 0.0, ls, le));
            expectEquals (b.getNumSamples(), le);
            // jumping from the last sample to the loop start looks like one ordinary sample step
            const float jump = std::abs (b.getSample (0, ls) - b.getSample (0, le - 1));
            const float slope = 0.5f * (float) (2.0 * juce::MathConstants<double>::pi * 233.0 / 48000.0);
            logMessage ("loop jump " + juce::String (jump, 5) + " (one sample of the sine moves up to " + juce::String (slope, 5) + ")");
            expectLessThan (jump, slope * 1.35f);

            juce::AudioBuffer<float> c (2, 48000 * 8);
            c.clear();
            expect (makeLoop (c, 48000.0, 2.5, ls, le));
            expectWithinAbsoluteError ((le - ls) / 48000.0, 2.5, 1.0e-4);
            juce::AudioBuffer<float> tooShort (2, 48000);
            expect (! makeLoop (tooShort, 48000.0, 0.0, ls, le));
        }

        beginTest ("a rendered model instrument loads like a recorded one");
        {
            ModelInstrument m;
            m.id = "test-model-banjo"; m.name = "Test Banjo"; m.family = "Guitars & Plucked"; m.model = "banjo";
            m.lo = 60; m.hi = 66; m.step = 3;
            ModelArticulation pl; pl.name = "Pluck"; pl.seconds = 1.0; pl.velLayers = 2; pl.roundRobin = 2;
            ModelArticulation held; held.name = "Held"; held.sustain = true; held.seconds = 2.0; held.velLayers = 1;
            m.arts = { pl, held };
            m.model = "banjo";
            const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HEMForgeModelTest");
            root.deleteRecursively();
            const auto res = renderInstrument (m, root);
            expect (res.ok, res.message);
            logMessage (res.message);
            juce::String err;
            const auto def = hf::InstrumentDef::load (root.getChildFile ("guitars-plucked/test-model-banjo"), err);
            expect (def != nullptr, err);
            if (def != nullptr)
            {
                expectEquals ((int) def->articulations.size(), 2);
                // roots 61, 64, 66 (last one covers the top), 2 layers x 2 takes
                expectEquals ((int) def->articulations[0].zones.size(), 3 * 2 * 2);
                expect (def->articulations[1].loop);
                for (auto& z : def->articulations[1].zones) expect (z.hasLoop());
                bool covered = true;
                for (int note = 60; note <= 66; ++note)
                    for (int v : { 1, 64, 127 })
                    {
                        int hits = 0;
                        for (auto& z : def->articulations[0].zones)
                            if (note >= z.lo && note <= z.hi && v >= z.vlo && v <= z.vhi && z.rr == 1) ++hits;
                        covered &= hits == 1;
                    }
                expect (covered, "every note and velocity has exactly one zone per take");
                for (auto& a : def->articulations)
                    for (auto& z : a.zones) expect (def->folder.getChildFile (z.file).existsAsFile(), z.file);
                expect (std::abs (def->gainDb) < 24.0f);
                expectEquals (def->source, juce::String ("hem"));
            }
            root.deleteRecursively();
        }
    }
};

static ModelTests modelTests;
} // namespace
