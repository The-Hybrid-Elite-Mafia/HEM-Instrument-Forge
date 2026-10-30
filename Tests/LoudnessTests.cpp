// Loudness meter accuracy (the meter the exporter uses to master): K-weighting against the ITU-R
// BS.1770 published table, and the EBU Tech 3341 reference case (1 kHz stereo sine at -23 dBFS must
// read -23.0 +/- 0.1 LUFS) for momentary, short-term and integrated at 44.1 / 48 / 96 kHz.

#include "Core/Loudness.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <numbers>
#include <vector>

namespace hemtest
{
void beginTracking();
int endTracking();
}

namespace
{
struct Ref { double b0, b1, b2, a1, a2; };

// ITU-R BS.1770-4, tables 1 and 2 (48 kHz).
constexpr Ref kStage1At48k { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
constexpr Ref kStage2At48k { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

// Independent reference: the analog-prototype design used by libebur128 / pyloudnorm.
Ref refStage1 (double fs)
{
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan (std::numbers::pi * f0 / fs);
    const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    return { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
             2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
}

Ref refStage2 (double fs)
{
    const double f0 = 38.13547087602444, Q = 0.5003270373238773;
    const double K = std::tan (std::numbers::pi * f0 / fs);
    const double a0 = 1.0 + K / Q + K * K;
    return { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
}

class LoudnessTests final : public juce::UnitTest
{
public:
    LoudnessTests() : juce::UnitTest ("Loudness Meter", "Forge") {}

    void checkCoeffs (const pg::BiquadCoeffs& got, const Ref& want, const juce::String& what)
    {
        constexpr double tol = 1.0e-8;
        expectWithinAbsoluteError (got.b0, want.b0, tol, what + " b0");
        expectWithinAbsoluteError (got.b1, want.b1, tol, what + " b1");
        expectWithinAbsoluteError (got.b2, want.b2, tol, what + " b2");
        expectWithinAbsoluteError (got.a1, want.a1, tol, what + " a1");
        expectWithinAbsoluteError (got.a2, want.a2, tol, what + " a2");
    }

    void runTest() override
    {
        using pg::dsp::LoudnessMeter;

        beginTest ("K-weighting at 48 kHz matches the BS.1770 table (1e-8)");
        checkCoeffs (LoudnessMeter::kWeightingStage1 (48000.0), kStage1At48k, "stage 1");
        checkCoeffs (LoudnessMeter::kWeightingStage2 (48000.0), kStage2At48k, "stage 2");

        beginTest ("K-weighting at 44.1 / 88.2 / 96 / 192 kHz matches the analog-prototype design (1e-8)");
        for (double fs : { 44100.0, 88200.0, 96000.0, 192000.0 })
        {
            checkCoeffs (LoudnessMeter::kWeightingStage1 (fs), refStage1 (fs), "stage 1 @ " + juce::String (fs));
            checkCoeffs (LoudnessMeter::kWeightingStage2 (fs), refStage2 (fs), "stage 2 @ " + juce::String (fs));
        }

        beginTest ("EBU Tech 3341 case 1: 1 kHz stereo sine at -23 dBFS reads -23.0 +/- 0.1 LUFS (M, S, I)");
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            LoudnessMeter lm;
            lm.prepare (fs);
            const int n = (int) (fs * 10.0);
            const float amp = (float) std::pow (10.0, -23.0 / 20.0);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
                l[(size_t) i] = r[(size_t) i] = amp * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * i / fs);

            hemtest::beginTracking();
            for (int start = 0; start < n; start += 512)
                lm.process (l.data() + start, r.data() + start, juce::jmin (512, n - start));
            const int allocs = hemtest::endTracking();
            expectEquals (allocs, 0, "allocations in process()");

            const juce::String at = " @ " + juce::String (fs) + " Hz";
            logMessage ("M " + juce::String (lm.getMomentary(), 3) + "  S " + juce::String (lm.getShortTerm(), 3)
                        + "  I " + juce::String (lm.getIntegrated(), 3) + " LUFS" + at);
            expectWithinAbsoluteError (lm.getMomentary(), -23.0f, 0.1f, "momentary" + at);
            expectWithinAbsoluteError (lm.getShortTerm(), -23.0f, 0.1f, "short-term" + at);
            expectWithinAbsoluteError (lm.getIntegrated(), -23.0f, 0.1f, "integrated" + at);
        }
    }
};

static LoudnessTests loudnessTests;
} // namespace
