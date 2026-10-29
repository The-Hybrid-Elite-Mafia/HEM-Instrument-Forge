#pragma once

#include "Core/Biquad.h"
#include <array>
#include <atomic>
#include <cmath>
#include <numbers>
#include <vector>

namespace pg::dsp
{
/**
    Real-time ITU-R BS.1770-4 loudness meter (stereo): K-weighting, momentary (400 ms),
    short-term (3 s) and gated integrated loudness, all in LUFS.

    The integrated value uses a histogram of 100 ms-hop momentary blocks (0.1 LU bins from -70 to
    +10 LUFS), so it runs for any song length without growing memory and without allocation in
    process(). prepare() allocates; process() never does.
*/
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // K-weighting stage 1 (high shelf, +4 dB) and stage 2 (high pass), as in BS.1770 for 48 kHz,
        // re-derived for any rate with the same analog prototypes.
        preFilter = BiquadCoeffs::highShelfQ (fs, 1681.97, 3.99984, 0.7071752);
        rlbFilter = BiquadCoeffs::highPass (fs, 38.1355, 0.5003270);
        hop = std::max (1, (int) std::lround (fs * 0.1));
        hops.assign (30, 0.0);   // 30 x 100 ms = 3 s
        reset();
    }

    void reset() noexcept
    {
        for (auto& st : state) st = {};
        std::fill (hops.begin(), hops.end(), 0.0);
        histogram.fill (0);
        hopSum = 0.0; hopFill = 0; hopIndex = 0; hopsSeen = 0;
        momentary.store (-120.0f); shortTerm.store (-120.0f); integrated.store (-120.0f);
    }

    void process (const float* left, const float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = rlb (1, pre (0, left[i]));
            const float r = rlb (3, pre (2, right[i]));
            hopSum += (double) l * l + (double) r * r;
            if (++hopFill >= hop)
                finishHop();
        }
    }

    float getMomentary() const noexcept  { return momentary.load (std::memory_order_relaxed); }
    float getShortTerm() const noexcept  { return shortTerm.load (std::memory_order_relaxed); }
    float getIntegrated() const noexcept { return integrated.load (std::memory_order_relaxed); }

    static float powerToLufs (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -120.0f;
    }

private:
    static constexpr int kBins = 800;          // -70 .. +10 LUFS in 0.1 LU steps
    static constexpr float kBinLow = -70.0f;

    float pre (int s, float x) noexcept { return state[(size_t) s].process (preFilter, x); }
    float rlb (int s, float x) noexcept { return state[(size_t) s].process (rlbFilter, x); }

    void finishHop() noexcept
    {
        hops[(size_t) hopIndex] = hopSum / (double) hop;
        hopIndex = (hopIndex + 1) % (int) hops.size();
        hopSum = 0.0;
        hopFill = 0;
        ++hopsSeen;

        // momentary = last 4 hops, short-term = last 30 hops
        double m = 0.0, st = 0.0;
        const int n = (int) hops.size();
        for (int k = 0; k < n; ++k)
        {
            const double v = hops[(size_t) ((hopIndex - 1 - k + 2 * n) % n)];
            if (k < 4) m += v;
            st += v;
        }
        const float mLufs = hopsSeen >= 4 ? powerToLufs (m / 4.0) : -120.0f;
        momentary.store (mLufs, std::memory_order_relaxed);
        shortTerm.store (hopsSeen >= 30 ? powerToLufs (st / 30.0) : -120.0f, std::memory_order_relaxed);

        if (hopsSeen >= 4 && mLufs > kBinLow)
        {
            const int bin = std::clamp ((int) ((mLufs - kBinLow) * 10.0f), 0, kBins - 1);
            ++histogram[(size_t) bin];
            updateIntegrated();
        }
    }

    void updateIntegrated() noexcept
    {
        // absolute gate -70 LUFS (bins start there), then relative gate -10 LU below the ungated mean
        double sum = 0.0; long count = 0;
        for (int b = 0; b < kBins; ++b)
            if (histogram[(size_t) b] > 0)
            {
                sum += histogram[(size_t) b] * std::pow (10.0, (binLufs (b) + 0.691) / 10.0);
                count += histogram[(size_t) b];
            }
        if (count == 0) return;
        const float relGate = powerToLufs (sum / (double) count) - 10.0f;

        sum = 0.0; count = 0;
        for (int b = 0; b < kBins; ++b)
            if (histogram[(size_t) b] > 0 && binLufs (b) >= relGate)
            {
                sum += histogram[(size_t) b] * std::pow (10.0, (binLufs (b) + 0.691) / 10.0);
                count += histogram[(size_t) b];
            }
        if (count > 0) integrated.store (powerToLufs (sum / (double) count), std::memory_order_relaxed);
    }

    static double binLufs (int b) noexcept { return kBinLow + (b + 0.5) * 0.1; }

    double fs = 48000.0;
    BiquadCoeffs preFilter, rlbFilter;
    std::array<BiquadState, 4> state {};
    std::vector<double> hops;
    std::array<int, kBins> histogram {};
    double hopSum = 0.0;
    int hop = 4800, hopFill = 0, hopIndex = 0;
    long hopsSeen = 0;
    std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f }, integrated { -120.0f };
};

/**
    True-peak estimate per BS.1770 annex 2: 4x oversampling with a 48-tap windowed-sinc
    interpolator (12 taps per phase), largest absolute value. Holds the max until read & reset.
*/
class TruePeakMeter
{
public:
    TruePeakMeter()
    {
        constexpr int taps = 48;
        for (int phase = 0; phase < 4; ++phase)
            for (int k = 0; k < 12; ++k)
            {
                const int n = k * 4 + phase;
                const double t = (n - (taps - 1) * 0.5) / 4.0;
                const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (std::numbers::pi * t) / (std::numbers::pi * t);
                const double w = 0.5 * (1.0 - std::cos (2.0 * std::numbers::pi * (n + 0.5) / taps));   // Hann
                coeffs[(size_t) phase][(size_t) k] = (float) (sinc * w);
            }
    }

    void reset() noexcept { histL.fill (0.0f); histR.fill (0.0f); pos = 0; held.store (0.0f); }

    void process (const float* left, const float* right, int numSamples) noexcept
    {
        float mx = held.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            histL[(size_t) pos] = left[i];
            histR[(size_t) pos] = right[i];
            pos = (pos + 1) % 12;
            for (int phase = 0; phase < 4; ++phase)
            {
                float yl = 0.0f, yr = 0.0f;
                for (int k = 0; k < 12; ++k)
                {
                    const int idx = (pos - 1 - k + 24) % 12;
                    yl += coeffs[(size_t) phase][(size_t) k] * histL[(size_t) idx];
                    yr += coeffs[(size_t) phase][(size_t) k] * histR[(size_t) idx];
                }
                mx = std::max (mx, std::max (std::abs (yl), std::abs (yr)));
            }
        }
        held.store (mx, std::memory_order_relaxed);
    }

    /** Linear true peak since the last reset. */
    float get() const noexcept { return held.load (std::memory_order_relaxed); }
    void clearHold() noexcept { held.store (0.0f); }

private:
    std::array<std::array<float, 12>, 4> coeffs {};
    std::array<float, 12> histL {}, histR {};
    int pos = 0;
    std::atomic<float> held { 0.0f };
};
} // namespace pg::dsp
