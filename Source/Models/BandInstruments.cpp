#include "Models/BandInstruments.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace pg::band
{
namespace
{
using I = Instrument;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

//==============================================================================
// Small DSP blocks
//==============================================================================
struct Biquad
{
    enum Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (Type t, double sr, double f, double q, double gainDb = 0.0)
    {
        f = juce::jlimit (5.0, sr * 0.45, f);
        const double w = kTwoPi * f / sr, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * q), A = std::pow (10.0, gainDb / 40.0);
        double B0, B1, B2, A0, A1, A2;
        switch (t)
        {
            case lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case lowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case highShelf:
            default:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s; break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

struct Chain
{
    std::vector<Biquad> f;
    void add (Biquad::Type t, double sr, double fr, double q, double g = 0.0) { Biquad b; b.set (t, sr, fr, q, g); f.push_back (b); }
    void run (std::vector<float>& x) { for (auto& b : f) for (auto& s : x) s = b.process (s); }
};

double polyBlep (double t, double dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

double sawAt (double ph, double dt) noexcept { return 2.0 * ph - 1.0 - polyBlep (ph, dt); }

double pulseAt (double ph, double dt, double width) noexcept
{
    double v = ph < width ? 1.0 : -1.0;
    v += polyBlep (ph, dt);
    double ph2 = ph + (1.0 - width); ph2 -= std::floor (ph2);
    v -= polyBlep (ph2, dt);
    return v;
}

float noise (Rng& r) noexcept { return (float) (r.uniform() * 2.0 - 1.0); }

void addPanned (juce::AudioBuffer<float>& out, const std::vector<float>& mono, int start, float pan, float gain)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    const float gl = std::cos (a) * gain * 1.41421356f, gr = std::sin (a) * gain * 1.41421356f;
    const int n = juce::jmin ((int) mono.size(), out.getNumSamples() - start);
    if (n <= 0 || start < 0) return;
    auto* l = out.getWritePointer (0) + start;
    auto* r = out.getWritePointer (1) + start;
    for (int i = 0; i < n; ++i) { l[i] += mono[(size_t) i] * gl; r[i] += mono[(size_t) i] * gr; }
}

//==============================================================================
// Plucked / struck string waveguide
//==============================================================================
struct StringSpec
{
    double t60At110 = 3.0;     // seconds to fall 60 dB at 110 Hz
    double t60Exponent = 0.5;  // higher notes die faster: t60 * (110/f)^exp
    double loss = 0.2;         // loop lowpass coefficient (0 = brightest)
    double lossVelocity = 0.2; // softer playing = darker
    double pickPos = 0.13;     // fraction of the string from the bridge
    double exciteSoft = 0.3;   // excitation lowpass (0 = sharp pick, 0.9 = finger/hammer)
    double dampT60 = 0.07;     // when the note is released / muted
    double maxRing = 6.0;      // seconds
    double detuneCents = 0.0;  // second string (unison courses, piano)
    double subAmount = 0.0;    // sine reinforcement of the fundamental (bass)
};

StringSpec specFor (I inst, int articulation)
{
    StringSpec s;
    switch (inst)
    {
        case I::acousticGuitar: s = { 3.6, 0.45, 0.16, 0.25, 0.13, 0.25, 0.07, 6.0 }; break;
        case I::electricGuitar: s = { 2.8, 0.40, 0.06, 0.15, 0.08, 0.1, 0.06, 5.0 }; break;
        case I::banjo:          s = { 1.1, 0.35, 0.03, 0.10, 0.10, 0.05, 0.05, 2.5 }; break;
        case I::mandolin:       s = { 1.4, 0.35, 0.06, 0.15, 0.12, 0.15, 0.05, 3.0 }; s.detuneCents = 3.0; break;
        case I::bass:           s = { 3.0, 0.30, 0.30, 0.20, 0.20, 0.45, 0.08, 5.0 }; s.subAmount = 0.25; break;
        case I::uprightBass:    s = { 1.3, 0.25, 0.45, 0.20, 0.22, 0.75, 0.09, 3.0 }; s.subAmount = 0.2; break;
        case I::piano:          s = { 9.0, 0.55, 0.10, 0.35, 0.11, 0.7, 0.25, 9.0 }; s.detuneCents = 1.3; break;
        default: break;
    }
    if (articulation == art::muted) { s.t60At110 = 0.18; s.t60Exponent = 0.0; s.loss = juce::jmin (0.6, s.loss + 0.25); s.maxRing = 0.5; }
    if (articulation == art::chop)  { s.t60At110 = 0.07; s.t60Exponent = 0.0; s.loss = juce::jmin (0.7, s.loss + 0.3); s.maxRing = 0.25; }
    return s;
}

/** Phase delay (samples) of the loop lowpass y = (1-a)x + a y[-1] at angular frequency w. */
double lpPhaseDelay (double a, double w) { return std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w; }

/** Phase delay (samples) of the first-order allpass (c + z^-1)/(1 + c z^-1). */
double apPhaseDelay (double c, double w)
{
    const double num = std::atan2 (-std::sin (w), c + std::cos (w));
    const double den = std::atan2 (-c * std::sin (w), 1.0 + c * std::cos (w));
    double ph = num - den;
    while (ph > 0) ph -= kTwoPi;
    return -ph / w;
}

/** One vibrating string. Writes `samples` into dst (added). Returns the number of samples written. */
int renderString (std::vector<float>& dst, double sr, double f0, float vel, int noteSamples, const StringSpec& sp, Rng& rng, float gain)
{
    f0 = juce::jlimit (20.0, sr / 6.0, f0);
    const double w = kTwoPi * f0 / sr;
    const double a = juce::jlimit (0.0, 0.95, sp.loss + sp.lossVelocity * (1.0 - vel));
    const double period = sr / f0;
    const double lpd = lpPhaseDelay (a, w);
    int N = (int) std::floor (period - lpd - 0.5);
    N = juce::jmax (2, N);
    const double need = period - N - lpd;
    // solve allpass coefficient for the fractional remainder
    double lo = -0.99, hi = 0.99, c = 0.0;
    for (int it = 0; it < 50; ++it) { c = 0.5 * (lo + hi); if (apPhaseDelay (c, w) > need) lo = c; else hi = c; }

    const double mag = (1.0 - a) / std::sqrt (1.0 - 2.0 * a * std::cos (w) + a * a);
    const double t60 = juce::jmax (0.05, sp.t60At110 * std::pow (110.0 / f0, sp.t60Exponent));
    const double gRing = juce::jmin (0.99995, std::pow (10.0, -3.0 / (t60 * f0)) / mag);
    const double gDamp = juce::jmin (gRing, std::pow (10.0, -3.0 / (sp.dampT60 * f0)) / mag);

    // excitation: shaped noise burst, one period long, with the pick-position comb
    std::vector<float> line ((size_t) N, 0.0f);
    double lp = 0.0;
    const double soft = juce::jlimit (0.0, 0.97, sp.exciteSoft + 0.25 * (1.0 - vel));
    for (int i = 0; i < N; ++i) { lp = (1.0 - soft) * noise (rng) + soft * lp; line[(size_t) i] = (float) lp; }
    const int M = juce::jlimit (1, N - 1, (int) std::round (sp.pickPos * N));
    std::vector<float> combed (line);
    for (int i = 0; i < N; ++i) combed[(size_t) i] = line[(size_t) i] - line[(size_t) ((i - M + N) % N)];
    double mean = 0.0; for (float v : combed) mean += v; mean /= N;
    double peakAbs = 1.0e-9; for (auto& v : combed) { v = (float) (v - mean); peakAbs = juce::jmax (peakAbs, (double) std::abs (v)); }
    const float amp = gain * std::pow (vel, 1.4f) / (float) peakAbs;
    for (int i = 0; i < N; ++i) line[(size_t) i] = combed[(size_t) i] * amp;

    const int maxLen = juce::jmin ((int) dst.size(), noteSamples + (int) (sr * sp.maxRing));
    double s = 0.0, apx = 0.0, apy = 0.0, env = 0.0;
    int idx = 0, written = 0;
    for (int n = 0; n < maxLen; ++n)
    {
        const double g = n < noteSamples ? gRing : gDamp;
        const double x = line[(size_t) idx];
        s = (1.0 - a) * x + a * s;
        const double y = c * s + apx - c * apy;
        apx = s; apy = y;
        line[(size_t) idx] = (float) (g * y);
        idx = idx + 1 == N ? 0 : idx + 1;
        dst[(size_t) n] += (float) x;
        env = juce::jmax (std::abs (x), env * 0.9995);
        written = n + 1;
        if (n > noteSamples && env < 2.0e-5) break;
    }
    // sine reinforcement of the fundamental (basses)
    if (sp.subAmount > 0.0)
    {
        const double dec = std::exp (-1.0 / (sr * juce::jmax (0.2, t60 * 0.4)));
        const double rel = std::exp (-1.0 / (sr * sp.dampT60));
        double e = sp.subAmount * std::pow (vel, 1.4f) * gain, ph = 0.0;
        for (int n = 0; n < written; ++n)
        {
            dst[(size_t) n] += (float) (e * std::sin (ph) * juce::jmin (1.0, n / (sr * 0.004)));
            ph += w; e *= n < noteSamples ? dec : rel;
        }
    }
    return written;
}

//==============================================================================
// Oscillator instruments
//==============================================================================
/** Attack / release envelope (linear attack, exponential release). */
double envAt (int n, int noteSamples, double sr, double attack, double release)
{
    const double t = n / sr;
    const double a = attack > 0.0 ? juce::jmin (1.0, t / attack) : 1.0;
    if (n < noteSamples) return a;
    return a * std::exp (-(n - noteSamples) / (sr * release));
}

int renderFiddle (std::vector<float>& dst, double sr, const Note& note, double prevPitch, bool legato, int noteSamples, Rng& rng)
{
    const double attack = legato ? 0.02 : 0.07, release = 0.12;
    const int total = juce::jmin ((int) dst.size(), noteSamples + (int) (sr * release * 6));
    double ph = rng.uniform();
    Biquad bowNoise; bowNoise.set (Biquad::bandpass, sr, 2600.0, 0.8);
    const double target = note.pitch, start = legato ? prevPitch : note.pitch - (note.articulation == art::bend ? 1.0 : 0.15);
    const double glide = legato ? 0.06 : (note.articulation == art::bend ? 0.12 : 0.04);
    const float vel = std::pow (note.velocity, 1.2f);
    for (int n = 0; n < total; ++n)
    {
        const double t = n / sr;
        double m = target + (start - target) * std::exp (-t / glide);
        const double vibDepth = juce::jlimit (0.0, 1.0, (t - 0.22) / 0.3) * 0.17;   // semitones
        m += vibDepth * std::sin (kTwoPi * 5.6 * t);
        const double f = midiToHz (m);
        const double dt = f / sr;
        const double e = envAt (n, noteSamples, sr, attack, release);
        double v = sawAt (ph, dt) * 0.8 + pulseAt (ph, dt, 0.42) * 0.2;
        const double scrape = (t < 0.05 && ! legato) ? (1.0 - t / 0.05) * 0.35 : 0.0;
        v += bowNoise.process (noise (rng)) * (0.08 + scrape);
        dst[(size_t) n] += (float) (v * e * vel * 0.5);
        ph += dt; ph -= std::floor (ph);
    }
    return total;
}

int renderHarmonica (std::vector<float>& dst, double sr, const Note& note, int noteSamples, Rng& rng)
{
    const double attack = 0.025, release = 0.06;
    const int total = juce::jmin ((int) dst.size(), noteSamples + (int) (sr * release * 6));
    double p1 = rng.uniform(), p2 = rng.uniform();
    Biquad breath; breath.set (Biquad::bandpass, sr, 1800.0, 0.7);
    const bool bend = note.articulation == art::bend;
    const float vel = std::pow (note.velocity, 1.1f);
    for (int n = 0; n < total; ++n)
    {
        const double t = n / sr;
        const double m = note.pitch - (bend ? 1.0 * std::exp (-t / 0.11) : 0.0);
        const double f1 = midiToHz (m), f2 = midiToHz (m + 0.04);
        const double e = envAt (n, noteSamples, sr, attack, release) * (1.0 + 0.06 * std::sin (kTwoPi * 4.8 * t));
        double v = pulseAt (p1, f1 / sr, 0.32) * 0.5 + pulseAt (p2, f2 / sr, 0.3) * 0.5;
        v += breath.process (noise (rng)) * 0.12;
        dst[(size_t) n] += (float) (v * e * vel * 0.45);
        p1 += f1 / sr; p1 -= std::floor (p1);
        p2 += f2 / sr; p2 -= std::floor (p2);
    }
    return total;
}

int renderOrgan (std::vector<float>& L, std::vector<float>& R, double sr, const Note& note, int noteSamples, Rng& rng)
{
    static const double ratio[7] = { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0 };
    static const double level[7] = { 0.55, 1.0, 0.55, 0.45, 0.25, 0.22, 0.1 };
    const double release = 0.07;
    const int total = juce::jmin ((int) L.size(), noteSamples + (int) (sr * release * 6));
    const double f = midiToHz (note.pitch);
    double ph[7] {};
    for (auto& p : ph) p = rng.uniform();
    Biquad click; click.set (Biquad::bandpass, sr, 2400.0, 1.0);
    const float vel = 0.6f + 0.4f * note.velocity;
    for (int n = 0; n < total; ++n)
    {
        double v = 0.0;
        for (int k = 0; k < 7; ++k)
        {
            if (f * ratio[k] > sr * 0.45) continue;
            v += level[k] * std::sin (kTwoPi * ph[k]);
            ph[k] += f * ratio[k] / sr; ph[k] -= std::floor (ph[k]);
        }
        const double e = envAt (n, noteSamples, sr, 0.006, release);
        const double c = n < (int) (sr * 0.006) ? click.process (noise (rng)) * 0.4 : 0.0;
        const float s = (float) ((v * 0.22 + c) * e * vel);
        L[(size_t) n] += s; R[(size_t) n] += s;
    }
    return total;
}

int renderStrings (std::vector<float>& L, std::vector<float>& R, double sr, const Note& note, int noteSamples, Rng& rng)
{
    const double attack = 0.28, release = 0.5;
    const int total = juce::jmin ((int) L.size(), noteSamples + (int) (sr * release * 5));
    const double det[3] = { -0.07, 0.0, 0.07 };   // semitones
    double ph[3] { rng.uniform(), rng.uniform(), rng.uniform() };
    const double vibRate = 4.8 + rng.range (-0.3, 0.3);
    const float vel = 0.5f + 0.5f * note.velocity;
    for (int n = 0; n < total; ++n)
    {
        const double t = n / sr;
        const double vib = 0.06 * std::sin (kTwoPi * vibRate * t);
        double v[3];
        for (int k = 0; k < 3; ++k)
        {
            const double f = midiToHz (note.pitch + det[k] + vib);
            v[k] = sawAt (ph[k], f / sr);
            ph[k] += f / sr; ph[k] -= std::floor (ph[k]);
        }
        const double e = envAt (n, noteSamples, sr, attack, release) * vel * 0.18;
        L[(size_t) n] += (float) ((v[0] + 0.6 * v[1]) * e);
        R[(size_t) n] += (float) ((v[2] + 0.6 * v[1]) * e);
    }
    return total;
}

int render808 (std::vector<float>& dst, double sr, const Note& note, double prevPitch, int noteSamples)
{
    const int total = juce::jmin ((int) dst.size(), noteSamples + (int) (sr * 0.08));
    const bool slide = note.articulation == art::slideUp && prevPitch > 0.0;
    double ph = 0.0;
    for (int n = 0; n < total; ++n)
    {
        const double t = n / sr;
        double m = note.pitch + 12.0 * std::exp (-t / 0.012);                 // punchy pitch drop
        if (slide) m = prevPitch + (note.pitch - prevPitch) * juce::jmin (1.0, t / 0.09);
        const double f = midiToHz (m);
        const double e = (n < noteSamples ? std::exp (-t / 1.6) : std::exp (-t / 1.6) * std::exp (-(n - noteSamples) / (sr * 0.02)));
        const double x = std::sin (kTwoPi * ph) * e * note.velocity;
        dst[(size_t) n] += (float) (std::tanh (2.2 * x) / std::tanh (2.2) * 0.9);
        ph += f / sr; ph -= std::floor (ph);
    }
    return total;
}

//==============================================================================
// Drums (synthesized)
//==============================================================================
struct DrumHit { std::vector<float> l, r; };

void metalBank (std::vector<float>& x, double sr, double baseScale, double decay, float amp, Rng& rng, double hpFreq)
{
    static const double ratios[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    double ph[6] {};
    for (auto& p : ph) p = rng.uniform();
    Biquad hp1, hp2, bp;
    hp1.set (Biquad::highpass, sr, hpFreq, 0.7); hp2.set (Biquad::highpass, sr, hpFreq, 0.7);
    bp.set (Biquad::peak, sr, hpFreq * 1.3, 1.0, 6.0);
    for (size_t n = 0; n < x.size(); ++n)
    {
        double v = 0.0;
        for (int k = 0; k < 6; ++k)
        {
            v += ph[k] < 0.5 ? 1.0 : -1.0;
            ph[k] += ratios[k] * baseScale / sr; ph[k] -= std::floor (ph[k]);
        }
        v = v / 6.0 + noise (rng) * 0.3;
        const double e = std::exp (-(double) n / (sr * decay));
        x[n] += amp * (float) (bp.process (hp2.process (hp1.process ((float) v))) * e);
    }
}

void drumHit (int pitch, float vel, double sr, bool trap, Rng& rng, std::vector<float>& mono, float& pan)
{
    pan = 0.0f;
    auto make = [&] (double seconds) { mono.assign ((size_t) (seconds * sr), 0.0f); };
    const float v = std::pow (vel, 1.3f);
    switch (pitch)
    {
        case drum::kick:
        {
            make (trap ? 0.6 : 0.5);
            double ph = 0.0;
            Biquad click; click.set (Biquad::bandpass, sr, 3500.0, 1.2);
            for (size_t n = 0; n < mono.size(); ++n)
            {
                const double t = n / sr;
                const double f = (trap ? 48.0 : 54.0) + (trap ? 140.0 : 95.0) * std::exp (-t / (trap ? 0.03 : 0.045));
                const double body = std::sin (kTwoPi * ph) * std::exp (-t / (trap ? 0.25 : 0.22));
                const double beat = t < 0.006 ? click.process (noise (rng)) * (1.0 - t / 0.006) * 0.9 : 0.0;
                mono[n] = (float) (std::tanh ((body + beat) * 1.6) * v);
                ph += f / sr; ph -= std::floor (ph);
            }
            break;
        }
        case drum::snare: case drum::clap:
        {
            make (trap ? 0.35 : 0.4);
            Biquad hp; hp.set (Biquad::highpass, sr, trap ? 1800.0 : 1300.0, 0.7);
            Biquad pk; pk.set (Biquad::peak, sr, 5200.0, 1.0, 5.0);
            double p1 = 0, p2 = 0;
            const bool clap = pitch == drum::clap;
            for (size_t n = 0; n < mono.size(); ++n)
            {
                const double t = n / sr;
                double tone = clap ? 0.0 : (std::sin (kTwoPi * p1) * 0.6 + std::sin (kTwoPi * p2) * 0.35) * std::exp (-t / 0.07);
                double envN;
                if (clap) { const double bursts = (t < 0.011 ? 1.0 : t < 0.022 ? std::exp (-(t - 0.011) / 0.004) + (t > 0.011 ? 1.0 : 0.0) : 0.0);
                            envN = (t < 0.03 ? 0.7 * (1.0 + std::sin (t / 0.011 * kTwoPi) * 0.3) * (bursts > 0 ? 1.0 : 0.4) : 0.0) + std::exp (-t / 0.11) * 0.6; }
                else envN = std::exp (-t / (trap ? 0.09 : 0.14));
                const double wires = pk.process (hp.process (noise (rng))) * envN;
                mono[n] = (float) ((tone + wires * (clap ? 0.9 : 0.75)) * v);
                p1 += 185.0 / sr; p2 += 330.0 / sr; p1 -= std::floor (p1); p2 -= std::floor (p2);
            }
            break;
        }
        case drum::rim:
        {
            make (0.08);
            double ph = 0;
            for (size_t n = 0; n < mono.size(); ++n)
            {
                const double t = n / sr;
                mono[n] = (float) ((std::sin (kTwoPi * ph) * 0.8 + noise (rng) * 0.3) * std::exp (-t / 0.012) * v);
                ph += 1750.0 / sr; ph -= std::floor (ph);
            }
            break;
        }
        case drum::brush:
        {
            make (0.25);
            Biquad bp; bp.set (Biquad::bandpass, sr, 4200.0, 0.6);
            for (size_t n = 0; n < mono.size(); ++n)
            {
                const double t = n / sr;
                const double e = juce::jmin (1.0, t / 0.012) * std::exp (-t / 0.08);
                mono[n] = (float) (bp.process (noise (rng)) * e * v * 1.4);
            }
            break;
        }
        case drum::hatClosed: case drum::hatPedal: case drum::hatOpen:
        {
            const double decay = pitch == drum::hatOpen ? 0.32 : pitch == drum::hatPedal ? 0.03 : (trap ? 0.035 : 0.05);
            make (decay * 6.0);
            metalBank (mono, sr, 1.0, decay, v * 0.9f, rng, 7000.0);
            pan = 0.3f;
            break;
        }
        case drum::ride: case drum::rideBell:
        {
            make (1.8);
            metalBank (mono, sr, 0.62, pitch == drum::rideBell ? 0.6 : 0.9, v * 0.55f, rng, 3200.0);
            pan = -0.35f;
            break;
        }
        case drum::crash:
        {
            make (2.5);
            metalBank (mono, sr, 0.85, 1.4, v * 0.6f, rng, 2600.0);
            Biquad hp; hp.set (Biquad::highpass, sr, 3000.0, 0.7);
            for (size_t n = 0; n < mono.size(); ++n) mono[n] += (float) (hp.process (noise (rng)) * std::exp (-(double) n / (sr * 1.1)) * v * 0.35);
            pan = -0.25f;
            break;
        }
        case drum::tomLow: case drum::tomMid: case drum::tomHigh:
        {
            const double f0 = pitch == drum::tomLow ? 88.0 : pitch == drum::tomMid ? 120.0 : 165.0;
            pan = pitch == drum::tomLow ? 0.35f : pitch == drum::tomMid ? 0.0f : -0.3f;
            make (0.6);
            double ph = 0;
            for (size_t n = 0; n < mono.size(); ++n)
            {
                const double t = n / sr;
                const double f = f0 * (1.0 + 0.6 * std::exp (-t / 0.03));
                mono[n] = (float) ((std::sin (kTwoPi * ph) * std::exp (-t / 0.3) + noise (rng) * std::exp (-t / 0.01) * 0.3) * v);
                ph += f / sr; ph -= std::floor (ph);
            }
            break;
        }
        case drum::tamb: case drum::shaker:
        {
            make (pitch == drum::tamb ? 0.25 : 0.12);
            Biquad hp; hp.set (Biquad::highpass, sr, pitch == drum::tamb ? 6500.0 : 4500.0, 0.8);
            for (size_t n = 0; n < mono.size(); ++n)
            {
                const double t = n / sr;
                const double e = juce::jmin (1.0, t / (pitch == drum::shaker ? 0.02 : 0.002)) * std::exp (-t / (pitch == drum::tamb ? 0.07 : 0.035));
                mono[n] = (float) (hp.process (noise (rng)) * e * v * 0.8);
            }
            pan = 0.45f;
            break;
        }
        default: make (0.01); break;
    }
}

//==============================================================================
void bodyFilters (I inst, double sr, Chain& c)
{
    switch (inst)
    {
        case I::acousticGuitar: c.add (Biquad::highpass, sr, 75, 0.7); c.add (Biquad::peak, sr, 105, 1.4, 5); c.add (Biquad::peak, sr, 220, 1.2, 2.5);
                                c.add (Biquad::peak, sr, 2600, 1.0, 2.0); c.add (Biquad::lowpass, sr, 9000, 0.7); break;
        case I::electricGuitar: c.add (Biquad::highpass, sr, 90, 0.7); c.add (Biquad::peak, sr, 1800, 1.0, 3.5); c.add (Biquad::lowpass, sr, 5200, 0.8); break;
        case I::banjo:          c.add (Biquad::highpass, sr, 160, 0.7); c.add (Biquad::peak, sr, 1150, 2.0, 7); c.add (Biquad::peak, sr, 2600, 1.5, 3);
                                c.add (Biquad::peak, sr, 400, 1.0, -3); c.add (Biquad::lowpass, sr, 9500, 0.7); break;
        case I::mandolin:       c.add (Biquad::highpass, sr, 180, 0.7); c.add (Biquad::peak, sr, 850, 1.5, 4); c.add (Biquad::peak, sr, 3100, 1.5, 3); c.add (Biquad::lowpass, sr, 9000, 0.7); break;
        case I::bass:           c.add (Biquad::peak, sr, 100, 1.0, 3); c.add (Biquad::lowpass, sr, 3000, 0.7); break;
        case I::uprightBass:    c.add (Biquad::peak, sr, 90, 1.0, 4); c.add (Biquad::peak, sr, 700, 1.5, 3); c.add (Biquad::lowpass, sr, 2300, 0.7); break;
        case I::bass808:        c.add (Biquad::highpass, sr, 28, 0.7); break;
        case I::fiddle:         c.add (Biquad::highpass, sr, 190, 0.7); c.add (Biquad::peak, sr, 280, 3.0, 6); c.add (Biquad::peak, sr, 460, 4.0, 5);
                                c.add (Biquad::peak, sr, 1500, 1.0, -3); c.add (Biquad::peak, sr, 2800, 1.2, 6); c.add (Biquad::lowpass, sr, 7000, 0.7); break;
        case I::harmonica:      c.add (Biquad::highpass, sr, 220, 0.7); c.add (Biquad::peak, sr, 800, 2.0, 6); c.add (Biquad::peak, sr, 1600, 2.5, 4);
                                c.add (Biquad::peak, sr, 3000, 3.0, 3); c.add (Biquad::lowpass, sr, 6000, 0.7); break;
        case I::piano:          c.add (Biquad::highpass, sr, 35, 0.7); c.add (Biquad::peak, sr, 250, 1.0, 2); c.add (Biquad::peak, sr, 2500, 1.0, 2); break;
        case I::organ:          c.add (Biquad::lowpass, sr, 6500, 0.7); break;
        case I::strings:        c.add (Biquad::highpass, sr, 90, 0.7); c.add (Biquad::peak, sr, 320, 1.0, 2); c.add (Biquad::lowpass, sr, 4200, 0.7); break;
        default: break;
    }
}

void electricAmp (std::vector<float>& x)
{
    const float drive = 2.2f, norm = 1.0f / std::tanh (drive);
    float peakAbs = 1.0e-9f;
    for (float v : x) peakAbs = juce::jmax (peakAbs, std::abs (v));
    const float in = 0.8f / peakAbs;
    for (auto& v : x) v = std::tanh (v * in * drive) * norm * 0.8f;
}
} // namespace

//==============================================================================
double midiToHz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

float stemTargetRmsDb (Instrument i)
{
    switch (i)
    {
        case I::drums: return -17.0f;          case I::trapDrums: return -16.0f;
        case I::bass: return -19.0f;           case I::uprightBass: return -20.0f;  case I::bass808: return -17.0f;
        case I::acousticGuitar: return -21.0f; case I::electricGuitar: return -22.5f;
        case I::banjo: return -23.0f;          case I::mandolin: return -24.0f;
        case I::fiddle: return -22.0f;         case I::harmonica: return -23.0f;
        case I::piano: return -21.5f;          case I::organ: return -26.0f;        case I::strings: return -24.5f;
        case I::count: break;
    }
    return -22.0f;
}

bool renderPart (const Song& song, const Part& part, double sr, juce::AudioBuffer<float>& out, const std::atomic<bool>* cancel)
{
    const int total = out.getNumSamples();
    const double spb = song.secondsPerBeat();
    const I inst = part.instrument;
    Rng rng (song.request.seed * 7919u + (uint32_t) inst * 131u + 17u);
    auto startSample = [&] (const Note& n) { return (int) std::llround (n.start * spb * sr); };
    auto lengthSamples = [&] (const Note& n) { return juce::jmax (1, (int) std::llround (n.length * spb * sr)); };
    auto cancelled = [&] { return cancel != nullptr && cancel->load(); };

    if (inst == I::drums || inst == I::trapDrums)
    {
        std::vector<float> hit;
        for (size_t i = 0; i < part.notes.size(); ++i)
        {
            if ((i & 63) == 0 && cancelled()) return false;
            const auto& n = part.notes[i];
            float pan = 0.0f;
            drumHit (n.pitch, n.velocity, sr, inst == I::trapDrums, rng, hit, pan);
            addPanned (out, hit, startSample (n), pan, 0.7f);
        }
        return true;
    }

    const bool stereoVoice = inst == I::organ || inst == I::strings;
    std::vector<float> mono ((size_t) total, 0.0f), monoR;
    if (stereoVoice) monoR.assign ((size_t) total, 0.0f);
    std::vector<float> scratch, scratchR;
    double prevPitch = -1.0;
    double prevEnd = -1.0e9;

    for (size_t i = 0; i < part.notes.size(); ++i)
    {
        if ((i & 31) == 0 && cancelled()) return false;
        const auto& n = part.notes[i];
        const int st = startSample (n);
        if (st >= total) break;
        const int len = lengthSamples (n);
        const int room = total - st;
        scratch.assign ((size_t) room, 0.0f);
        int written = 0;
        switch (inst)
        {
            case I::acousticGuitar: case I::electricGuitar: case I::banjo: case I::mandolin:
            case I::bass: case I::uprightBass: case I::piano:
            {
                const auto spec = specFor (inst, n.articulation);
                const double f = midiToHz (n.pitch);
                written = renderString (scratch, sr, f, n.velocity, len, spec, rng, 0.5f);
                if (spec.detuneCents > 0.0)
                    written = juce::jmax (written, renderString (scratch, sr, f * std::pow (2.0, spec.detuneCents / 1200.0), n.velocity, len, spec, rng, 0.45f));
                break;
            }
            case I::bass808: written = render808 (scratch, sr, n, prevPitch, len); break;
            case I::fiddle:
            {
                const bool legato = n.articulation != art::doubleStop && prevPitch > 0.0 && n.start - prevEnd < 0.06;
                written = renderFiddle (scratch, sr, n, prevPitch, legato, len, rng);
                break;
            }
            case I::harmonica: written = renderHarmonica (scratch, sr, n, len, rng); break;
            case I::organ: case I::strings:
                scratchR.assign ((size_t) room, 0.0f);
                written = inst == I::organ ? renderOrgan (scratch, scratchR, sr, n, len, rng) : renderStrings (scratch, scratchR, sr, n, len, rng);
                for (int k = 0; k < written; ++k) monoR[(size_t) (st + k)] += scratchR[(size_t) k];
                break;
            case I::drums: case I::trapDrums: case I::count: break;
        }
        for (int k = 0; k < written; ++k) mono[(size_t) (st + k)] += scratch[(size_t) k];
        if (n.articulation != art::doubleStop) { prevPitch = n.pitch; prevEnd = n.start + n.length; }
    }

    Chain body;
    bodyFilters (inst, sr, body);
    body.run (mono);
    if (inst == I::electricGuitar) electricAmp (mono);
    if (stereoVoice)
    {
        Chain bodyR; bodyFilters (inst, sr, bodyR); bodyR.run (monoR);
        auto* l = out.getWritePointer (0); auto* r = out.getWritePointer (1);
        const float pl = 1.0f - juce::jmax (0.0f, part.pan), pr = 1.0f + juce::jmin (0.0f, part.pan);
        for (int k = 0; k < total; ++k)
        {
            float a = 1.0f, b = 1.0f;
            if (inst == I::organ)   // slow rotating-speaker motion
            {
                const float s = std::sin ((float) (kTwoPi * 0.75 * k / sr));
                a = 1.0f + 0.22f * s; b = 1.0f - 0.22f * s;
            }
            l[k] += mono[(size_t) k] * a * pl;
            r[k] += monoR[(size_t) k] * b * pr;
        }
    }
    else
        addPanned (out, mono, 0, part.pan, 1.0f);
    return true;
}

juce::AudioBuffer<float> renderSingleNote (Instrument inst, int pitch, float velocity, double seconds, double sr, int articulation)
{
    Song s;
    s.bpm = 60.0;
    s.request.seed = 5;
    Part p;
    p.instrument = inst;
    Note n;
    n.start = 0.0; n.length = seconds; n.pitch = pitch; n.velocity = velocity; n.articulation = articulation;
    p.notes.push_back (n);
    juce::AudioBuffer<float> buf (2, (int) ((seconds + 2.0) * sr));
    buf.clear();
    renderPart (s, p, sr, buf);
    return buf;
}
} // namespace pg::band
