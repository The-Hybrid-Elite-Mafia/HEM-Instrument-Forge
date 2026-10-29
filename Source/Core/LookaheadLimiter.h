#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <numbers>

namespace pg
{
/** Custom lookahead brickwall limiter (sample-peak).

    Gain path, per sample n:
        r[n]  = required gain so that max(|L|,|R|) * r <= ceiling       (<= 1)
        h[n]  = min(r[n-L+1 .. n])            sliding minimum, O(1) amortised
        hr[n] = h[n] if falling, else one-pole release toward h[n]  (hr <= h always)
        s[n]  = mean(hr[n-L+1 .. n])          box filter = smooth attack ramp
        y[n]  = x[n-(L-1)] * s[n]

    Proof of the brickwall: every hr[m] in the box window m in [n-L+1, n] is a
    minimum over a window r[m-L+1 .. m] that contains index n-(L-1). Therefore
    each term is <= r[n-(L-1)], so their mean is too, and |y[n]| <= ceiling.
    A final clamp at the ceiling absorbs float rounding only.

    Latency = L-1 samples, constant (also while bypassed).
    All buffers are allocated in prepare(); process() never allocates. */
class LookaheadLimiter
{
public:
    static constexpr double kLookaheadMs = 1.5;
    /** True-peak mode: the detector also looks at 4x-interpolated points between samples
        (same 48-tap windowed-sinc as the true-peak meter), so inter-sample peaks are held
        under the ceiling too. Costs kTpDelay extra samples of latency. Set before prepare(). */
    static constexpr int kTpDelay = 6;

    void setTruePeakMode (bool on) noexcept
    {
        truePeak = on;
        if (on)
            for (int phase = 0; phase < 4; ++phase)
                for (int k = 0; k < 12; ++k)
                {
                    const int n = k * 4 + phase;
                    const double t = (n - 23.5) / 4.0;
                    const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (std::numbers::pi * t) / (std::numbers::pi * t);
                    const double w = 0.5 * (1.0 - std::cos (2.0 * std::numbers::pi * (n + 0.5) / 48.0));
                    tpCoeffs[(size_t) phase][(size_t) k] = (float) (sinc * w);
                }
    }

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate;
        windowLength = std::max (2, (int) std::lround (kLookaheadMs * 0.001 * fs) + 1);

        delayL.assign ((size_t) windowLength, 0.0f);
        delayR.assign ((size_t) windowLength, 0.0f);
        boxBuffer.assign ((size_t) windowLength, 1.0f);
        dequeValue.assign ((size_t) windowLength, 1.0f);
        dequeIndex.assign ((size_t) windowLength, 0);
        reset();
        setRelease (releaseMs);
    }

    void reset() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (boxBuffer.begin(), boxBuffer.end(), 1.0f);
        boxSum = (double) windowLength;
        writePos = 0;
        sampleIndex = 0;
        dqHead = dqTail = dqCount = 0;
        released = 1.0f;
        lastGrDb = 0.0f;
        tpHistL.fill (0.0f); tpHistR.fill (0.0f); tpPos = 0;
    }

    int getLatencySamples() const noexcept { return windowLength - 1 + (truePeak ? kTpDelay : 0); }

    void setCeilingDb (float db) noexcept { ceiling = std::pow (10.0f, db / 20.0f); }

    void setRelease (float ms) noexcept
    {
        releaseMs = ms;
        releaseCoeff = (float) (1.0 - std::exp (-1.0 / std::max (1.0, (double) ms * 0.001 * fs)));
    }

    /** In place. bypass keeps the delay so latency is constant. Returns max GR (dB, positive). */
    float process (float* left, float* right, int numSamples, bool bypass) noexcept
    {
        float minGain = 1.0f;
        const auto L = windowLength;

        for (int i = 0; i < numSamples; ++i)
        {
            float xl = left[i], xr = right[i];
            float peak;
            if (truePeak)
            {
                // history: h[k] = x[n-k]. The interpolated points fall between x[n-6] and x[n-5];
                // the audio path continues with x[n-6], judged by both neighbours + the 4 points.
                tpHistL[(size_t) tpPos] = xl;
                tpHistR[(size_t) tpPos] = xr;
                auto h = [this] (const std::array<float, 12>& a, int k) { return a[(size_t) ((tpPos - k + 12) % 12)]; };
                peak = std::max ({ std::abs (h (tpHistL, 5)), std::abs (h (tpHistR, 5)),
                                   std::abs (h (tpHistL, 6)), std::abs (h (tpHistR, 6)) });
                for (int phase = 0; phase < 4; ++phase)
                {
                    float yl = 0.0f, yr = 0.0f;
                    for (int k = 0; k < 12; ++k)
                    {
                        yl += tpCoeffs[(size_t) phase][(size_t) k] * h (tpHistL, k);
                        yr += tpCoeffs[(size_t) phase][(size_t) k] * h (tpHistR, k);
                    }
                    peak = std::max (peak, std::max (std::abs (yl), std::abs (yr)));
                }
                xl = h (tpHistL, kTpDelay);
                xr = h (tpHistR, kTpDelay);
                tpPos = (tpPos + 1) % 12;
            }
            else
            {
                peak = std::max (std::abs (xl), std::abs (xr));
            }
            const float required = (! bypass && peak > ceiling) ? ceiling / peak : 1.0f;

            // --- sliding minimum (monotonic deque in a fixed ring) ---
            // expire first so the ring never holds more than L entries
            if (dqCount > 0 && dequeIndex[(size_t) dqHead] <= sampleIndex - L)
                popFront();
            while (dqCount > 0 && dequeValue[(size_t) back()] >= required)
                popBack();
            pushBack (required, sampleIndex);
            const float held = dequeValue[(size_t) dqHead];

            // --- release (upward only) ---
            released = held < released ? held : released + (held - released) * releaseCoeff;

            // --- box filter ---
            const auto slot = (size_t) writePos;
            boxSum += (double) released - (double) boxBuffer[slot];
            boxBuffer[slot] = released;
            const float gain = (float) (boxSum / (double) L);

            // --- delay line: read the sample written L-1 samples ago ---
            const auto readSlot = (size_t) ((writePos + 1) % L);
            delayL[slot] = xl;
            delayR[slot] = xr;
            const float dl = delayL[readSlot];
            const float dr = delayR[readSlot];

            writePos = (writePos + 1) % L;
            ++sampleIndex;

            if (bypass)
            {
                left[i] = dl;
                right[i] = dr;
            }
            else
            {
                left[i]  = std::clamp (dl * gain, -ceiling, ceiling);
                right[i] = std::clamp (dr * gain, -ceiling, ceiling);
                minGain = std::min (minGain, gain);
            }
        }

        lastGrDb = minGain < 1.0f ? -20.0f * std::log10 (std::max (minGain, 1.0e-9f)) : 0.0f;
        return lastGrDb;
    }

    float getLastGainReductionDb() const noexcept { return lastGrDb; }
    float getCeilingLinear() const noexcept { return ceiling; }

private:
    int back() const noexcept { return (dqTail + windowLength - 1) % windowLength; }
    void popBack() noexcept { dqTail = back(); --dqCount; }
    void popFront() noexcept { dqHead = (dqHead + 1) % windowLength; --dqCount; }
    void pushBack (float v, juce::int64 idx) noexcept
    {
        dequeValue[(size_t) dqTail] = v;
        dequeIndex[(size_t) dqTail] = idx;
        dqTail = (dqTail + 1) % windowLength;
        ++dqCount;
    }

    double fs = 44100.0;
    int windowLength = 2;
    float ceiling = 1.0f, releaseMs = 80.0f, releaseCoeff = 0.0f, released = 1.0f, lastGrDb = 0.0f;

    std::vector<float> delayL, delayR, boxBuffer, dequeValue;
    std::vector<juce::int64> dequeIndex;
    double boxSum = 0.0;
    int writePos = 0;
    juce::int64 sampleIndex = 0;
    int dqHead = 0, dqTail = 0, dqCount = 0;

    bool truePeak = false;
    std::array<std::array<float, 12>, 4> tpCoeffs {};
    std::array<float, 12> tpHistL {}, tpHistR {};
    int tpPos = 0;
};
} // namespace pg
