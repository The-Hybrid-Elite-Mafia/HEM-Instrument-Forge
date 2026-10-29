#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pg
{
/** RBJ-cookbook biquad. Coefficient math is pure arithmetic (no allocation),
    so it is safe to recompute on the audio thread when a parameter moves.
    juce::dsp::IIR::Coefficients::make* is NOT used because it heap-allocates. */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs identity() noexcept { return {}; }

    static BiquadCoeffs peak (double fs, double f0, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c = std::cos (w0);
        return normalise (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A,
                          1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f0, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0); // S = 1
        const double sA = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) - (A - 1) * c + sA),
                          2 * A * ((A - 1) - (A + 1) * c),
                          A * ((A + 1) - (A - 1) * c - sA),
                          (A + 1) + (A - 1) * c + sA,
                          -2 * ((A - 1) + (A + 1) * c),
                          (A + 1) + (A - 1) * c - sA);
    }

    static BiquadCoeffs highShelf (double fs, double f0, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
        const double sA = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) + (A - 1) * c + sA),
                          -2 * A * ((A - 1) + (A + 1) * c),
                          A * ((A + 1) + (A - 1) * c - sA),
                          (A + 1) - (A - 1) * c + sA,
                          2 * ((A - 1) - (A + 1) * c),
                          (A + 1) - (A - 1) * c - sA);
    }

    static BiquadCoeffs lowPass (double fs, double f0, double q) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalise ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    static BiquadCoeffs highPass (double fs, double f0, double q) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    /** Shelves with an adjustable slope expressed as Q (0.707 = the classic S = 1 shape). */
    static BiquadCoeffs lowShelfQ (double fs, double f0, double gainDb, double q) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double sA = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                          (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }

    static BiquadCoeffs highShelfQ (double fs, double f0, double gainDb, double q) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * clampFreq (f0, fs) / fs;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double sA = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                          (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }

    /** First-order low/high pass (6 dB/oct), as a biquad with b2 = a2 = 0. */
    static BiquadCoeffs lowPass1 (double fs, double f0) noexcept
    {
        const double k = std::tan (std::numbers::pi * clampFreq (f0, fs) / fs);
        return normalise (k, k, 0.0, k + 1.0, k - 1.0, 0.0);
    }

    static BiquadCoeffs highPass1 (double fs, double f0) noexcept
    {
        const double k = std::tan (std::numbers::pi * clampFreq (f0, fs) / fs);
        return normalise (1.0, -1.0, 0.0, k + 1.0, k - 1.0, 0.0);
    }

    bool isIdentity() const noexcept { return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0; }

    /** Magnitude response in dB at frequency f (used by the UI curve, message thread). */
    double magnitudeDb (double fs, double f) const noexcept
    {
        const double w = 2.0 * std::numbers::pi * f / fs;
        const double cr = std::cos (w), ci = -std::sin (w);
        const double c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        const double mag = std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
        return 20.0 * std::log10 (std::max (mag, 1.0e-12));
    }

    static double clampFreq (double f, double fs) noexcept { return std::fmin (std::fmax (f, 10.0), fs * 0.49); }

private:
    static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
};

/** Transposed direct form II, double-precision state. */
struct BiquadState
{
    double z1 = 0.0, z2 = 0.0;

    inline float process (const BiquadCoeffs& c, float xIn) noexcept
    {
        const double x = xIn;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};
} // namespace pg
