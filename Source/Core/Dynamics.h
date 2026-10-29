#pragma once

#include <algorithm>
#include <cmath>

namespace pg::dsp
{
inline float timeCoeff (double ms, double fs) noexcept
{
    const double samples = std::max (1.0e-3, ms * 0.001 * fs);
    return (float) std::exp (-1.0 / samples);
}

inline float linToDb (float x) noexcept { return x > 1.0e-9f ? 20.0f * std::log10 (x) : -180.0f; }
inline float dbToLin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

/** Soft-knee downward compressor static curve: gain reduction (positive dB) for a level. */
inline float compressorGr (float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
    const float over = levelDb - thresholdDb;
    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
        return slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);
    return over > 0.0f ? slope * over : 0.0f;
}

/**
    Stereo-linked compressor core: detector (peak or RMS) -> soft-knee curve -> attack/release
    in the dB domain. Optional program-dependent ("auto") release: a fast and a slow release run in
    parallel and the gain follows the one that releases less, so short peaks recover quickly while
    sustained compression recovers slowly without pumping. No allocation.
*/
class CompressorCore
{
public:
    enum class Detector { peak, rms };

    struct Settings
    {
        float thresholdDb = -18.0f, ratio = 3.0f, kneeDb = 6.0f;
        float attackMs = 10.0f, releaseMs = 120.0f;
        bool autoRelease = false;
        Detector detector = Detector::peak;
        float rmsMs = 8.0f;
    };

    void prepare (double sampleRate) noexcept { fs = sampleRate; setSettings (s); reset(); }
    void reset() noexcept { env = 0.0f; envFast = 0.0f; envSlow = 0.0f; rms = 0.0f; }

    void setSettings (const Settings& next) noexcept
    {
        s = next;
        attack = timeCoeff (s.attackMs, fs);
        release = timeCoeff (s.releaseMs, fs);
        releaseFast = timeCoeff (std::max (5.0f, s.releaseMs * 0.25f), fs);
        releaseSlow = timeCoeff (s.releaseMs * 4.0f, fs);
        rmsCoeff = timeCoeff (s.rmsMs, fs);
    }

    /** Gain reduction (positive dB) for this stereo sample. */
    inline float process (float l, float r) noexcept
    {
        float levelDb;
        if (s.detector == Detector::rms)
        {
            const float p = 0.5f * (l * l + r * r);
            rms = p + rmsCoeff * (rms - p);
            levelDb = rms > 1.0e-18f ? 10.0f * std::log10 (rms) + 3.0103f : -180.0f;   // +3 dB: sine RMS reads like its peak
        }
        else
        {
            levelDb = linToDb (std::max (std::abs (l), std::abs (r)));
        }

        const float target = compressorGr (levelDb, s.thresholdDb, s.ratio, s.kneeDb);
        if (! s.autoRelease)
        {
            env = target + (target > env ? attack : release) * (env - target);
            return env;
        }

        envFast = target + (target > envFast ? attack : releaseFast) * (envFast - target);
        envSlow = target + (target > envSlow ? attack : releaseSlow) * (envSlow - target);
        // follow the fast envelope, but never let it release below what the slow (sustained) one holds,
        // scaled down so the slow path only takes over when compression has been going on for a while
        env = std::max (envFast, 0.7f * envSlow);
        return env;
    }

    float current() const noexcept { return env; }
    const Settings& settings() const noexcept { return s; }

private:
    double fs = 48000.0;
    Settings s;
    float attack = 0, release = 0, releaseFast = 0, releaseSlow = 0, rmsCoeff = 0;
    float env = 0, envFast = 0, envSlow = 0, rms = 0;
};

/**
    Downward expander / gate with hysteresis-free soft action: below the threshold the signal is
    pulled down at `ratio` (1:ratio) until `rangeDb` of reduction. Fast attack (opening), adjustable
    release (closing), and a hold time so drum tails are not chopped mid-ring. No allocation.
*/
class GateCore
{
public:
    struct Settings
    {
        float thresholdDb = -60.0f;
        float rangeDb = 60.0f;     // maximum reduction
        float ratio = 4.0f;        // expansion ratio below the threshold
        float attackMs = 0.5f, holdMs = 20.0f, releaseMs = 120.0f;
        bool enabled = true;
    };

    void prepare (double sampleRate) noexcept { fs = sampleRate; setSettings (s); reset(); }
    void reset() noexcept { grDb = 0.0f; holdLeft = 0; level = 0.0f; }

    void setSettings (const Settings& next) noexcept
    {
        s = next;
        attack = timeCoeff (s.attackMs, fs);
        release = timeCoeff (s.releaseMs, fs);
        holdSamples = (int) std::lround (s.holdMs * 0.001 * fs);
        detect = timeCoeff (1.0, fs);
    }

    /** Gain reduction (positive dB). */
    inline float process (float l, float r) noexcept
    {
        if (! s.enabled) { grDb = 0.0f; return 0.0f; }
        const float peak = std::max (std::abs (l), std::abs (r));
        level = peak > level ? peak : peak + detect * (level - peak);   // instant up, 1 ms down
        const float levelDb = linToDb (level);

        float target = 0.0f;
        if (levelDb < s.thresholdDb)
            target = std::min (s.rangeDb, (s.thresholdDb - levelDb) * (s.ratio - 1.0f));

        if (target < grDb)                                   // opening: fast
        {
            grDb = target + attack * (grDb - target);
            holdLeft = holdSamples;
        }
        else if (holdLeft > 0)                               // holding open
        {
            --holdLeft;
        }
        else                                                 // closing: release
        {
            grDb = target + release * (grDb - target);
        }
        return grDb;
    }

    float current() const noexcept { return grDb; }

private:
    double fs = 48000.0;
    Settings s;
    float attack = 0, release = 0, detect = 0, grDb = 0, level = 0;
    int holdSamples = 0, holdLeft = 0;
};

/** Fast peak limiter without look-ahead (for plugin output safety). Instant attack, smooth release. */
class SafetyLimiter
{
public:
    void prepare (double fs, float releaseMs = 60.0f) noexcept { rel = timeCoeff (releaseMs, fs); g = 1.0f; }
    void reset() noexcept { g = 1.0f; }

    inline void process (float& l, float& r, float ceiling) noexcept
    {
        const float peak = std::max (std::abs (l), std::abs (r));
        const float need = peak > ceiling ? ceiling / peak : 1.0f;
        g = need < g ? need : need + rel * (g - need);
        l = std::clamp (l * g, -ceiling, ceiling);
        r = std::clamp (r * g, -ceiling, ceiling);
    }

    float gain() const noexcept { return g; }

private:
    float rel = 0.999f, g = 1.0f;
};
} // namespace pg::dsp
