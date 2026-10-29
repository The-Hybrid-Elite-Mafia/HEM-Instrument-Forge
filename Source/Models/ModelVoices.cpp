#include "Models/ModelVoices.h"
#include "Models/BandInstruments.h"
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <vector>

namespace hf::models
{
namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

//==============================================================================
// small building blocks
//==============================================================================
struct Rng
{
    explicit Rng (juce::uint32 s) : state (s != 0 ? s : 0x9E3779B9u) {}
    juce::uint32 next() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    double uni() noexcept { return (next() >> 8) * (1.0 / 16777216.0); }
    double bi() noexcept { return uni() * 2.0 - 1.0; }
    juce::uint32 state;
};

struct Biquad
{
    enum Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (Type t, double sr, double f, double q, double gainDb = 0.0)
    {
        f = juce::jlimit (5.0, sr * 0.45, f);
        q = juce::jmax (0.05, q);
        const double w = kTwoPi * f / sr, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * q), A = std::pow (10.0, gainDb / 40.0);
        double B0, B1, B2, A0, A1, A2;
        switch (t)
        {
            case lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;   // 0 dB peak
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
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Chain
{
    std::vector<Biquad> f;
    Chain& add (Biquad::Type t, double sr, double fr, double q, double g = 0.0) { Biquad b; b.set (t, sr, fr, q, g); f.push_back (b); return *this; }
    double process (double x) noexcept { for (auto& b : f) x = b.process (x); return x; }
    void run (std::vector<double>& x) { for (auto& s : x) s = process (s); }
};

/** A body: the direct signal plus a bank of damped resonances (wood / metal / skin). */
struct Mode { double freq, q, gainDb; };

struct Body
{
    std::vector<Biquad> modes;
    std::vector<double> gains;
    double direct = 1.0;
    void set (double sr, const std::vector<Mode>& ms, double spread, double directGain)
    {
        direct = directGain;
        modes.clear(); gains.clear();
        for (auto& m : ms)
        {
            Biquad b;
            b.set (Biquad::bandpass, sr, m.freq * spread, m.q);
            modes.push_back (b);
            gains.push_back (std::pow (10.0, m.gainDb / 20.0) - 1.0 > -1.0 ? std::pow (10.0, m.gainDb / 20.0) : 0.0);
        }
    }
    double process (double x) noexcept
    {
        double y = x * direct;
        for (size_t i = 0; i < modes.size(); ++i) y += modes[i].process (x) * gains[i];
        return y;
    }
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

juce::AudioBuffer<float> stereo (const std::vector<double>& l, const std::vector<double>& r)
{
    juce::AudioBuffer<float> b (2, (int) l.size());
    for (int i = 0; i < (int) l.size(); ++i) { b.setSample (0, i, (float) l[(size_t) i]); b.setSample (1, i, (float) r[(size_t) i]); }
    return b;
}

double envAR (double t, double attack, double hold) noexcept
{
    juce::ignoreUnused (hold);
    return t < attack ? std::sin (0.5 * std::numbers::pi * t / attack) : 1.0;
}

//==============================================================================
// Digital waveguide string (one polarization)
//==============================================================================
double lpPhaseDelay (double a, double w) { return std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w; }
double apPhaseDelay (double c, double w)
{
    const double num = std::atan2 (-std::sin (w), c + std::cos (w));
    const double den = std::atan2 (-c * std::sin (w), 1.0 + c * std::cos (w));
    double ph = num - den;
    while (ph > 0) ph -= kTwoPi;
    return -ph / w;
}

struct StringParams
{
    double f0 = 110.0;
    double t60 = 3.0;          // seconds to fall 60 dB at f0
    double loss = 0.15;        // loop lowpass (0 bright .. 0.95 dark)
    double pickPos = 0.13;     // fraction of the string
    double exciteSoft = 0.3;   // 0 sharp pick .. 0.97 soft finger
    double velocity = 0.7;
    double gain = 1.0;
};

/** Plucks a string and adds `n` samples of it into dst. */
void pluck (std::vector<double>& dst, double sr, const StringParams& p, Rng& rng)
{
    const double f0 = juce::jlimit (20.0, sr / 6.0, p.f0);
    const double w = kTwoPi * f0 / sr;
    const double a = juce::jlimit (0.0, 0.95, p.loss);
    const double period = sr / f0;
    const double lpd = lpPhaseDelay (a, w);
    int N = juce::jmax (2, (int) std::floor (period - lpd - 0.5));
    const double need = period - N - lpd;
    double lo = -0.99, hi = 0.99, c = 0.0;
    for (int it = 0; it < 50; ++it) { c = 0.5 * (lo + hi); if (apPhaseDelay (c, w) > need) lo = c; else hi = c; }
    const double mag = (1.0 - a) / std::sqrt (1.0 - 2.0 * a * std::cos (w) + a * a);
    const double g = juce::jmin (0.99997, std::pow (10.0, -3.0 / (juce::jmax (0.02, p.t60) * f0)) / mag);

    std::vector<double> line ((size_t) N, 0.0);
    double lp = 0.0;
    const double soft = juce::jlimit (0.0, 0.97, p.exciteSoft);
    for (int i = 0; i < N; ++i) { lp = (1.0 - soft) * rng.bi() + soft * lp; line[(size_t) i] = lp; }
    // pick position: a comb that removes the harmonics with a node at the pick point
    const int M = juce::jlimit (1, N - 1, (int) std::round (p.pickPos * N));
    std::vector<double> combed (line);
    for (int i = 0; i < N; ++i) combed[(size_t) i] = line[(size_t) i] - line[(size_t) ((i - M + N) % N)];
    double mean = 0.0; for (double v : combed) mean += v; mean /= N;
    double peakAbs = 1.0e-9; for (auto& v : combed) { v -= mean; peakAbs = juce::jmax (peakAbs, std::abs (v)); }
    const double amp = p.gain * std::pow (p.velocity, 1.3) / peakAbs;
    for (int i = 0; i < N; ++i) line[(size_t) i] = combed[(size_t) i] * amp;

    double s = 0.0, apx = 0.0, apy = 0.0, env = 0.0;
    int idx = 0;
    for (size_t n = 0; n < dst.size(); ++n)
    {
        const double x = line[(size_t) idx];
        s = (1.0 - a) * x + a * s;
        const double y = c * s + apx - c * apy;
        apx = s; apy = y;
        line[(size_t) idx] = g * y;
        idx = idx + 1 == N ? 0 : idx + 1;
        dst[n] += x;
        env = juce::jmax (std::abs (x), env * 0.9995);
        if (n > (size_t) (sr * 0.2) && env < 1.0e-6) break;
    }
}

//==============================================================================
// Plucked instruments
//==============================================================================
struct Course { double semitones; double cents; double gain; };

struct PluckedSpec
{
    double t60At110 = 3.5, t60Exp = 0.45;   // t60 = t60At110 * (110 / f)^t60Exp
    double loss = 0.15, lossVel = 0.2;       // darker when soft
    double pickPos = 0.13, exciteSoft = 0.25, exciteVel = 0.3;
    std::vector<Course> courses { { 0.0, 0.0, 1.0 } };
    std::vector<Mode> body;
    double bodyDirect = 0.75;
    double pickNoise = 0.03, pickNoiseHz = 3500.0;
    double highpass = 60.0, lowpass = 14000.0;
    // electric
    bool electric = false;
    double pickupHz = 3800.0, pickupQ = 1.4;
    double drive = 0.0;        // 0 clean .. 10 crunch
    double cabHz = 5200.0;
    // effects on the string
    double buzz = 0.0;         // sitar bridge buzz
    std::vector<double> sympathetic;   // ratios of sympathetic strings that ring along
};

juce::AudioBuffer<float> renderPlucked (const NoteSpec& ns, const PluckedSpec& sp, bool muted)
{
    const double sr = ns.sampleRate;
    const int n = (int) (ns.seconds * sr);
    std::vector<double> str ((size_t) n, 0.0);
    Rng rng (ns.seed * 7919u + (juce::uint32) ns.pitch * 31u);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    for (auto& c : sp.courses)
    {
        const double f = midiToHz (ns.pitch + c.semitones + c.cents / 100.0);
        if (f > sr / 6.0) continue;
        StringParams p;
        p.f0 = f;
        p.t60 = sp.t60At110 * std::pow (110.0 / f, sp.t60Exp);
        p.loss = juce::jlimit (0.0, 0.95, sp.loss + sp.lossVel * (1.0 - vel));
        p.pickPos = sp.pickPos;
        p.exciteSoft = juce::jlimit (0.0, 0.97, sp.exciteSoft + sp.exciteVel * (1.0 - vel));
        p.velocity = vel;
        p.gain = c.gain;
        if (muted)
        {
            p.t60 = juce::jmin (p.t60, 0.16);
            p.loss = juce::jmin (0.85, p.loss + 0.3);
            p.exciteSoft = juce::jmin (0.9, p.exciteSoft + 0.25);
        }
        // two polarizations: the second decays faster and is a hair sharper (natural double decay)
        pluck (str, sr, p, rng);
        StringParams q = p;
        q.t60 *= 0.55; q.f0 *= std::pow (2.0, 0.7 / 1200.0); q.pickPos = juce::jmin (0.45, q.pickPos + 0.02); q.gain *= 0.4;
        pluck (str, sr, q, rng);
    }
    // sympathetic strings ring along (a few high-Q resonators at related pitches)
    if (! sp.sympathetic.empty())
    {
        std::vector<Biquad> sym;
        for (double r : sp.sympathetic) { Biquad b; b.set (Biquad::bandpass, sr, midiToHz (ns.pitch) * r, 180.0); sym.push_back (b); }
        for (auto& v : str)
        {
            double add = 0.0;
            for (auto& b : sym) add += b.process (v);
            v += add * 0.35;
        }
    }
    // sitar-style bridge buzz: a curved bridge brightens the string while it is loud
    if (sp.buzz > 0.0)
    {
        Biquad hp; hp.set (Biquad::highpass, sr, 1800.0, 0.7);
        double env = 0.0;
        for (auto& v : str)
        {
            env = juce::jmax (std::abs (v), env * 0.9997);
            const double b = hp.process (std::copysign (v * v, v)) * sp.buzz * 4.0;
            v += b / (1.0 + 2.0 * env);
        }
    }
    // pick noise
    {
        Biquad bp; bp.set (Biquad::bandpass, sr, sp.pickNoiseHz, 1.2);
        const int len = (int) (0.006 * sr);
        for (int i = 0; i < len && i < n; ++i)
            str[(size_t) i] += bp.process (rng.bi()) * sp.pickNoise * vel * (1.0 - (double) i / len);
    }

    std::vector<double> L ((size_t) n), R ((size_t) n);
    if (sp.electric)
    {
        // magnetic pickup -> amp (asymmetric soft clip) -> tone stack -> cabinet
        Chain pick; pick.add (Biquad::highpass, sr, 70.0, 0.7).add (Biquad::lowpass, sr, sp.pickupHz, sp.pickupQ);
        Chain tone; tone.add (Biquad::lowShelf, sr, 120.0, 0.7, 2.0).add (Biquad::peak, sr, 650.0, 0.8, -3.0).add (Biquad::highShelf, sr, 3000.0, 0.7, 2.0);
        Chain cabL; cabL.add (Biquad::highpass, sr, 85.0, 0.8).add (Biquad::peak, sr, 2400.0, 1.2, 3.0).add (Biquad::lowpass, sr, sp.cabHz, 0.9).add (Biquad::lowpass, sr, sp.cabHz * 1.25, 0.7);
        Chain cabR = cabL;
        cabR.f[1].set (Biquad::peak, sr, 2650.0, 1.2, 2.5);
        const double pre = 1.0 + sp.drive * 2.2;
        const double norm = sp.drive > 0.0 ? 1.0 / std::tanh (pre * 0.6) : 1.0;
        for (int i = 0; i < n; ++i)
        {
            double x = pick.process (str[(size_t) i]);
            if (sp.drive > 0.0)
            {
                const double d = x * pre;
                x = (std::tanh (d + 0.15) - std::tanh (0.15)) * norm * 0.6;
            }
            x = tone.process (x);
            L[(size_t) i] = cabL.process (x);
            R[(size_t) i] = cabR.process (x);
        }
    }
    else
    {
        // two "microphones" on the body: the same modes, slightly shifted, for a natural stereo image
        Body bl, br;
        bl.set (sr, sp.body, 1.0, sp.bodyDirect);
        br.set (sr, sp.body, 1.035, sp.bodyDirect);
        Chain fl; fl.add (Biquad::highpass, sr, sp.highpass, 0.7).add (Biquad::lowpass, sr, sp.lowpass, 0.7);
        Chain fr = fl;
        for (int i = 0; i < n; ++i)
        {
            const double x = str[(size_t) i];
            L[(size_t) i] = fl.process (bl.process (x));
            R[(size_t) i] = fr.process (br.process (x));
        }
    }
    return stereo (L, R);
}

PluckedSpec acousticSteel()
{
    PluckedSpec s;
    s.t60At110 = 3.6; s.t60Exp = 0.45; s.loss = 0.14; s.pickPos = 0.12; s.exciteSoft = 0.18;
    s.body = { { 98, 7, 9 }, { 196, 10, 5 }, { 245, 9, 3 }, { 390, 7, 3 }, { 560, 6, 2 }, { 1080, 4, 1.5 }, { 2200, 3, 2 }, { 3400, 3, 1 } };
    s.bodyDirect = 0.7; s.pickNoise = 0.04;
    return s;
}
PluckedSpec nylon()
{
    PluckedSpec s;
    s.t60At110 = 3.2; s.t60Exp = 0.5; s.loss = 0.32; s.lossVel = 0.2; s.pickPos = 0.17; s.exciteSoft = 0.55; s.exciteVel = 0.25;
    s.body = { { 95, 6, 9 }, { 190, 8, 5 }, { 400, 6, 3 }, { 610, 5, 2 }, { 1200, 3, 1 } };
    s.bodyDirect = 0.75; s.pickNoise = 0.015; s.lowpass = 9000.0;
    return s;
}
PluckedSpec twelveString()
{
    auto s = acousticSteel();
    s.courses = { { 0.0, 0.0, 1.0 }, { 0.0, 3.5, 0.75 }, { 12.0, -2.0, 0.55 } };
    return s;
}
PluckedSpec mandolin()
{
    PluckedSpec s;
    s.t60At110 = 1.5; s.t60Exp = 0.35; s.loss = 0.07; s.pickPos = 0.11; s.exciteSoft = 0.1;
    s.courses = { { 0.0, 0.0, 1.0 }, { 0.0, 3.0, 0.9 } };
    s.body = { { 380, 9, 5 }, { 790, 8, 5 }, { 1250, 6, 3 }, { 2600, 4, 3 }, { 4200, 3, 2 } };
    s.bodyDirect = 0.65; s.highpass = 170.0; s.pickNoise = 0.05;
    return s;
}
PluckedSpec banjo()
{
    PluckedSpec s;
    s.t60At110 = 1.15; s.t60Exp = 0.3; s.loss = 0.025; s.lossVel = 0.1; s.pickPos = 0.08; s.exciteSoft = 0.04; s.exciteVel = 0.15;
    s.body = { { 290, 3, 4 }, { 720, 4, 6 }, { 1180, 5, 7 }, { 1900, 5, 4 }, { 2650, 4, 5 }, { 4100, 3, 2 } };
    s.bodyDirect = 0.55; s.highpass = 150.0; s.pickNoise = 0.06; s.pickNoiseHz = 4200.0;
    return s;
}
PluckedSpec bouzouki()
{
    PluckedSpec s = mandolin();
    s.t60At110 = 2.4; s.courses = { { 0.0, 0.0, 1.0 }, { 12.0, 2.0, 0.6 } };
    s.body = { { 210, 8, 6 }, { 470, 7, 4 }, { 900, 6, 3 }, { 2100, 4, 2 } };
    s.highpass = 90.0;
    return s;
}
PluckedSpec resonator()
{
    PluckedSpec s;
    s.t60At110 = 3.2; s.t60Exp = 0.4; s.loss = 0.06; s.pickPos = 0.1; s.exciteSoft = 0.12;
    s.body = { { 680, 22, 7 }, { 1350, 18, 6 }, { 2050, 15, 5 }, { 3400, 12, 4 }, { 190, 6, 3 } };
    s.bodyDirect = 0.5; s.highpass = 110.0; s.pickNoise = 0.03;
    return s;
}
PluckedSpec steelGuitar (bool lap)
{
    PluckedSpec s;
    s.t60At110 = lap ? 4.5 : 6.0; s.t60Exp = 0.35; s.loss = 0.05; s.pickPos = 0.09; s.exciteSoft = 0.2;
    s.electric = true; s.pickupHz = 4300.0; s.pickupQ = 1.1; s.drive = lap ? 1.5 : 0.0; s.cabHz = 6500.0;
    return s;
}
PluckedSpec koto()
{
    PluckedSpec s;
    s.t60At110 = 2.6; s.t60Exp = 0.35; s.loss = 0.05; s.pickPos = 0.24; s.exciteSoft = 0.05;
    s.body = { { 330, 7, 4 }, { 980, 9, 4 }, { 1900, 6, 2 } };
    s.bodyDirect = 0.7; s.highpass = 120.0; s.pickNoise = 0.05;
    return s;
}
PluckedSpec sitar()
{
    PluckedSpec s;
    s.t60At110 = 4.0; s.t60Exp = 0.3; s.loss = 0.04; s.pickPos = 0.1; s.exciteSoft = 0.06;
    s.body = { { 250, 6, 4 }, { 600, 6, 3 }, { 1450, 5, 2 } };
    s.buzz = 0.9; s.sympathetic = { 1.0, 1.5, 2.0, 1.335, 3.0 };
    s.bodyDirect = 0.75; s.highpass = 100.0;
    return s;
}
PluckedSpec electricGuitar (bool crunch)
{
    PluckedSpec s;
    s.t60At110 = crunch ? 4.0 : 3.4; s.t60Exp = 0.4; s.loss = 0.06; s.lossVel = 0.15; s.pickPos = 0.09; s.exciteSoft = 0.1;
    s.electric = true; s.pickupHz = crunch ? 3300.0 : 4200.0; s.pickupQ = 1.5; s.drive = crunch ? 6.0 : 0.0; s.cabHz = crunch ? 4600.0 : 5600.0;
    return s;
}
PluckedSpec electricBass (int variant)
{
    PluckedSpec s;
    s.t60At110 = 4.2; s.t60Exp = 0.25; s.loss = variant == 0 ? 0.33 : variant == 1 ? 0.18 : 0.08; s.lossVel = 0.15;
    s.pickPos = variant == 2 ? 0.07 : 0.2; s.exciteSoft = variant == 0 ? 0.5 : variant == 1 ? 0.12 : 0.0;
    s.electric = true; s.pickupHz = variant == 2 ? 3500.0 : 2400.0; s.pickupQ = 1.0; s.drive = 0.0; s.cabHz = 5000.0;
    return s;
}

//==============================================================================
// Keys
//==============================================================================
juce::AudioBuffer<float> renderTine (const NoteSpec& ns, bool reed)
{
    const double sr = ns.sampleRate, f = midiToHz (ns.pitch);
    const int n = (int) (ns.seconds * sr);
    std::vector<double> m ((size_t) n, 0.0);
    Rng rng (ns.seed * 131u + (juce::uint32) ns.pitch);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    const double t60 = 3.8 * std::pow (261.6 / f, 0.55);
    struct P { double ratio, amp, decayScale; };
    std::vector<P> parts = reed ? std::vector<P> { { 1, 1.0, 1.0 }, { 2, 0.18, 0.5 }, { 3, 0.32, 0.4 }, { 5, 0.12, 0.25 }, { 7, 0.05, 0.2 } }
                                : std::vector<P> { { 1, 1.0, 1.0 }, { 2, 0.22, 0.55 }, { 3, 0.05, 0.35 } };
    for (auto& p : parts)
    {
        const double fr = f * p.ratio;
        if (fr > sr * 0.45) continue;
        const double d = std::exp (-6.9 / (sr * t60 * p.decayScale));
        double a = p.amp * (0.4 + 0.6 * vel), ph = rng.uni() * 0.1;
        for (int i = 0; i < n; ++i) { m[(size_t) i] += a * std::sin (kTwoPi * ph); ph += fr / sr; a *= d; }
    }
    if (! reed)
    {
        // the tine's bell: an inharmonic partial that dies fast, stronger when struck hard
        const double fb = f * 7.1;
        if (fb < sr * 0.45)
        {
            double a = 0.25 * vel * vel; const double d = std::exp (-6.9 / (sr * 0.12));
            for (int i = 0; i < n && a > 1.0e-6; ++i) { m[(size_t) i] += a * std::sin (kTwoPi * fb * i / sr); a *= d; }
        }
    }
    // hammer thump
    Biquad lp; lp.set (Biquad::lowpass, sr, 400.0, 0.7);
    for (int i = 0; i < (int) (0.012 * sr) && i < n; ++i) m[(size_t) i] += lp.process (rng.bi()) * 0.25 * vel * (1.0 - i / (0.012 * sr));
    // pickup "bark": asymmetric drive that grows with how hard the key is hit
    const double bark = (reed ? 0.9 : 0.55) * vel * vel;
    for (auto& v : m) v = v + bark * v * std::abs (v) * 0.6;
    std::vector<double> L ((size_t) n), R ((size_t) n);
    Chain tl; tl.add (Biquad::highpass, sr, 50.0, 0.7).add (Biquad::lowpass, sr, reed ? 5200.0 : 7500.0, 0.7);
    Chain tr = tl;
    const int d = (int) (0.0004 * sr);
    for (int i = 0; i < n; ++i)
    {
        L[(size_t) i] = tl.process (m[(size_t) i]) * 0.5;
        R[(size_t) i] = tr.process (i >= d ? m[(size_t) (i - d)] : 0.0) * 0.5;
    }
    return stereo (L, R);
}

juce::AudioBuffer<float> renderClav (const NoteSpec& ns, bool muted)
{
    const double sr = ns.sampleRate;
    const int n = (int) (ns.seconds * sr);
    std::vector<double> s ((size_t) n, 0.0);
    Rng rng (ns.seed * 17u + (juce::uint32) ns.pitch);
    StringParams p;
    p.f0 = midiToHz (ns.pitch);
    p.t60 = (muted ? 0.18 : 2.2) * std::pow (110.0 / p.f0, 0.25);
    p.loss = muted ? 0.4 : 0.01;
    p.pickPos = 0.05;
    p.exciteSoft = 0.0;
    p.velocity = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    pluck (s, sr, p, rng);
    // two pickups near the end of the string: a comb, then the amp's lowpass
    const int M = juce::jmax (1, (int) (sr / p.f0 * 0.1));
    std::vector<double> L ((size_t) n), R ((size_t) n);
    Chain c; c.add (Biquad::highpass, sr, 90.0, 0.7).add (Biquad::peak, sr, 1500.0, 0.9, 4.0).add (Biquad::lowpass, sr, 6500.0, 0.8);
    for (int i = 0; i < n; ++i)
    {
        const double x = s[(size_t) i] - 0.7 * (i >= M ? s[(size_t) (i - M)] : 0.0);
        const double y = c.process (x);
        L[(size_t) i] = y; R[(size_t) i] = y;
    }
    return stereo (L, R);
}

/** Tonewheel organ. variant: 0 gospel, 1 jazz + percussion, 2 soft ballad, 3 full, 4 rotary slow, 5 rotary fast */
juce::AudioBuffer<float> renderOrgan (const NoteSpec& ns, int variant)
{
    const double sr = ns.sampleRate;
    const int n = (int) (ns.seconds * sr);
    static const double ratio[9] { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
    static const int regs[4][9] { { 8, 8, 8, 8, 0, 0, 0, 0, 0 }, { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 0, 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
    const int reg = variant >= 4 ? 0 : variant;
    const double f = midiToHz (ns.pitch);
    Rng rng (ns.seed * 53u + (juce::uint32) ns.pitch);
    std::vector<double> m ((size_t) n, 0.0);
    for (int k = 0; k < 9; ++k)
    {
        const int v = regs[reg][k];
        if (v == 0) continue;
        double fr = f * ratio[k];
        while (fr > 5900.0) fr *= 0.5;   // the top wheels fold back an octave
        const double amp = std::pow (10.0, -(8 - v) * 3.0 / 20.0) * 0.12;
        double ph = rng.uni();
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (kTwoPi * ph);
            m[(size_t) i] += amp * (s + 0.03 * s * s);   // wheels are not perfectly pure
            ph += fr / sr;
            if (ph >= 1.0) ph -= 1.0;
        }
    }
    // percussion (jazz): second-harmonic chime that dies in ~0.3 s
    if (variant == 1)
    {
        double a = 0.22; const double d = std::exp (-6.9 / (sr * 0.35));
        for (int i = 0; i < n; ++i) { m[(size_t) i] += a * std::sin (kTwoPi * f * 3.0 * i / sr); a *= d; }
    }
    // key click
    Biquad click; click.set (Biquad::bandpass, sr, 2600.0, 0.9);
    for (int i = 0; i < (int) (0.004 * sr) && i < n; ++i) m[(size_t) i] += click.process (rng.bi()) * 0.18 * (1.0 - i / (0.004 * sr));
    // soft attack (contacts close within ~5 ms)
    for (int i = 0; i < (int) (0.005 * sr) && i < n; ++i) m[(size_t) i] *= (double) i / (0.005 * sr);

    std::vector<double> L ((size_t) n), R ((size_t) n);
    const double maxD = 0.003 * sr;
    std::vector<double> dl ((size_t) (maxD + 8), 0.0);
    int w = 0;
    auto readD = [&] (double d) { const double p = w - d; const int i0 = ((int) std::floor (p) % (int) dl.size() + (int) dl.size()) % (int) dl.size(); const int i1 = (i0 + 1) % (int) dl.size(); const double fr = p - std::floor (p); return dl[(size_t) i0] * (1.0 - fr) + dl[(size_t) i1] * fr; };
    Biquad split; split.set (Biquad::lowpass, sr, 800.0, 0.7);
    for (int i = 0; i < n; ++i)
    {
        const double x = m[(size_t) i];
        dl[(size_t) w] = x;
        const double t = i / sr;
        if (variant >= 4)
        {
            // rotary cabinet: horn (highs) and drum (lows) turning; loop-friendly rates (common period 2.5 s)
            const double horn = variant == 4 ? 0.8 : 6.4, drum = variant == 4 ? 0.4 : 5.6;
            const double lo = split.process (x), hi = x - lo;
            const double ph = kTwoPi * horn * t, pd = kTwoPi * drum * t;
            const double hd = 0.0012 * sr * (1.0 + std::sin (ph)) + 1.0;
            const double hiL = readD (hd) * 0.0 + hi * (0.75 + 0.25 * std::sin (ph));
            const double hiR = hi * (0.75 + 0.25 * std::sin (ph + 2.2));
            L[(size_t) i] = lo * (0.85 + 0.15 * std::sin (pd)) + hiL;
            R[(size_t) i] = lo * (0.85 + 0.15 * std::sin (pd + 2.0)) + hiR;
            // doppler on the horn: the delay above shifts pitch slightly
            L[(size_t) i] += (readD (hd) - x) * 0.25;
            R[(size_t) i] += (readD (0.0012 * sr * (1.0 + std::sin (ph + 2.2)) + 1.0) - x) * 0.25;
        }
        else
        {
            // scanner chorus: a delay swept at 6.9 Hz mixed with the dry signal
            const double d1 = 0.0007 * sr * (1.0 + std::sin (kTwoPi * 6.9 * t)) + 1.0;
            const double d2 = 0.0007 * sr * (1.0 + std::sin (kTwoPi * 6.9 * t + 1.7)) + 1.0;
            L[(size_t) i] = 0.6 * x + 0.4 * readD (d1);
            R[(size_t) i] = 0.6 * x + 0.4 * readD (d2);
        }
        w = (w + 1) % (int) dl.size();
    }
    Chain cl; cl.add (Biquad::lowpass, sr, 7000.0, 0.7).add (Biquad::highpass, sr, 40.0, 0.7);
    Chain cr = cl;
    for (int i = 0; i < n; ++i) { L[(size_t) i] = cl.process (L[(size_t) i]); R[(size_t) i] = cr.process (R[(size_t) i]); }
    return stereo (L, R);
}

//==============================================================================
// Free reeds (accordion / harmonium / melodica) and bagpipe
//==============================================================================
struct ReedSpec
{
    std::vector<Course> reeds;
    double width = 0.32;           // pulse width of the reed
    double brightHz = 3500.0, brightVelHz = 2500.0;
    std::vector<Mode> body;
    double attack = 0.04, breath = 0.02, tremble = 0.015;
    bool constantLevel = false;    // bagpipe: velocity does not change the level
};

juce::AudioBuffer<float> renderReed (const NoteSpec& ns, const ReedSpec& sp)
{
    const double sr = ns.sampleRate;
    const int n = (int) (ns.seconds * sr);
    Rng rng (ns.seed * 211u + (juce::uint32) ns.pitch);
    const double vel = sp.constantLevel ? 0.8 : juce::jlimit (0.05, 1.0, (double) ns.velocity);
    std::vector<double> m ((size_t) n, 0.0);
    for (auto& r : sp.reeds)
    {
        const double f = midiToHz (ns.pitch + r.semitones + r.cents / 100.0);
        if (f > sr * 0.4) continue;
        double ph = rng.uni();
        for (int i = 0; i < n; ++i)
        {
            const double dt = f / sr;
            m[(size_t) i] += pulseAt (ph, dt, sp.width) * r.gain;
            ph += dt; if (ph >= 1.0) ph -= 1.0;
        }
    }
    Chain c; c.add (Biquad::lowpass, sr, sp.brightHz + sp.brightVelHz * vel, 0.6).add (Biquad::highpass, sr, 80.0, 0.7);
    Body bl, br;
    bl.set (sr, sp.body, 1.0, 0.8);
    br.set (sr, sp.body, 1.03, 0.8);
    Biquad breathF; breathF.set (Biquad::bandpass, sr, 1800.0, 0.6);
    std::vector<double> L ((size_t) n), R ((size_t) n);
    double tr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        tr += (rng.bi() * 0.5 - tr) * 0.0005;   // slow random bellows pressure
        const double env = envAR (t, sp.attack, 0.0) * (1.0 + sp.tremble * tr) * (0.35 + 0.65 * vel);
        const double x = c.process (m[(size_t) i]) * env * 0.22 + breathF.process (rng.bi()) * sp.breath * env;
        L[(size_t) i] = bl.process (x);
        R[(size_t) i] = br.process (x);
    }
    return stereo (L, R);
}

//==============================================================================
// Flutes (pan flute, tin whistle)
//==============================================================================
juce::AudioBuffer<float> renderFlute (const NoteSpec& ns, bool whistle)
{
    const double sr = ns.sampleRate, f = midiToHz (ns.pitch);
    const int n = (int) (ns.seconds * sr);
    Rng rng (ns.seed * 97u + (juce::uint32) ns.pitch);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    const double h[4] { 1.0, whistle ? 0.28 : 0.12, whistle ? 0.12 : 0.05, whistle ? 0.05 : 0.015 };
    Biquad noiseBp; noiseBp.set (Biquad::bandpass, sr, juce::jmin (sr * 0.4, f * 3.0), 1.2);
    Biquad noiseHi; noiseHi.set (Biquad::bandpass, sr, 5500.0, 0.6);
    std::vector<double> L ((size_t) n), R ((size_t) n);
    double ph = rng.uni(), vibPh = 0.0, drift = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        // the note bends up into pitch at the start, vibrato grows after 0.35 s
        const double bend = -0.25 * std::exp (-t / 0.05);
        const double vib = 0.12 * juce::jlimit (0.0, 1.0, (t - 0.35) / 0.4) * std::sin (vibPh);
        drift += (rng.bi() * 0.02 - drift) * 0.0002;
        const double fr = f * std::pow (2.0, (bend + vib + drift) / 12.0);
        double v = 0.0;
        for (int k = 0; k < 4; ++k) if (fr * (k + 1) < sr * 0.45) v += h[k] * std::sin (kTwoPi * ph * (k + 1));
        ph += fr / sr; if (ph >= 1.0) ph -= 1.0;
        vibPh += kTwoPi * 5.1 / sr;
        const double env = envAR (t, whistle ? 0.03 : 0.06, 0.0);
        const double chiff = (whistle ? 0.4 : 0.25) * std::exp (-t / 0.025);
        const double breath = noiseBp.process (rng.bi()) * (whistle ? 0.12 : 0.18) + noiseHi.process (rng.bi()) * 0.05;
        const double y = (v * 0.35 + breath * (0.6 + chiff * 6.0)) * env * (0.4 + 0.6 * vel);
        L[(size_t) i] = y; R[(size_t) i] = y * 0.97;
    }
    return stereo (L, R);
}

//==============================================================================
// Voices (choir)
//==============================================================================
struct Formants { double f[5]; double bw[5]; double amp[5]; };

Formants vowel (int v, int pitch)
{
    // soprano / alto / tenor / bass by range; v: 0 aah, 1 ooh, 2 hum
    const int type = pitch >= 67 ? 0 : pitch >= 60 ? 1 : pitch >= 52 ? 2 : 3;
    static const Formants aah[4] {
        { { 800, 1150, 2900, 3900, 4950 }, { 80, 90, 120, 130, 140 }, { 1.0, 0.5, 0.025, 0.1, 0.003 } },
        { { 800, 1150, 2800, 3500, 4950 }, { 80, 90, 120, 130, 140 }, { 1.0, 0.63, 0.1, 0.016, 0.001 } },
        { { 650, 1080, 2650, 2900, 3250 }, { 80, 90, 120, 130, 140 }, { 1.0, 0.5, 0.45, 0.4, 0.08 } },
        { { 600, 1040, 2250, 2450, 2750 }, { 60, 70, 110, 120, 130 }, { 1.0, 0.45, 0.35, 0.35, 0.1 } } };
    static const Formants ooh[4] {
        { { 325, 700, 2700, 3800, 4950 }, { 50, 60, 170, 180, 200 }, { 1.0, 0.16, 0.018, 0.01, 0.001 } },
        { { 325, 700, 2530, 3500, 4950 }, { 50, 60, 170, 180, 200 }, { 1.0, 0.25, 0.03, 0.01, 0.001 } },
        { { 350, 600, 2700, 2900, 3300 }, { 40, 60, 100, 120, 120 }, { 1.0, 0.1, 0.14, 0.2, 0.05 } },
        { { 350, 600, 2400, 2675, 2950 }, { 40, 80, 100, 120, 120 }, { 1.0, 0.1, 0.025, 0.04, 0.016 } } };
    Formants f = (v == 1) ? ooh[type] : aah[type];
    if (v == 2)   // hum: closed mouth, nasal
    {
        f = { { 280, 1350, 2300, 3100, 4200 }, { 50, 120, 150, 200, 250 }, { 1.0, 0.06, 0.04, 0.01, 0.002 } };
    }
    return f;
}

juce::AudioBuffer<float> renderChoir (const NoteSpec& ns, int vowelIndex, int voices, double bright)
{
    const double sr = ns.sampleRate, f = midiToHz (ns.pitch);
    const int n = (int) (ns.seconds * sr);
    Rng rng (ns.seed * 401u + (juce::uint32) ns.pitch);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    const auto fm = vowel (vowelIndex, ns.pitch);
    std::vector<double> L ((size_t) n, 0.0), R ((size_t) n, 0.0);
    for (int v = 0; v < voices; ++v)
    {
        const double det = rng.bi() * 11.0;         // cents
        const double vibRate = 4.8 + rng.uni() * 1.2, vibDepth = 18.0 + rng.uni() * 16.0;
        const int onset = (int) (rng.uni() * 0.04 * sr);
        const double pan = voices > 1 ? -0.9 + 1.8 * v / (voices - 1) : 0.0;
        const double gl = std::cos ((pan + 1.0) * 0.25 * std::numbers::pi), gr = std::sin ((pan + 1.0) * 0.25 * std::numbers::pi);
        std::vector<Biquad> form (5);
        for (int k = 0; k < 5; ++k) form[(size_t) k].set (Biquad::bandpass, sr, juce::jmin (sr * 0.45, fm.f[k] * (1.0 + 0.02 * rng.bi())), fm.f[k] / fm.bw[k]);
        Biquad tilt; tilt.set (Biquad::lowpass, sr, juce::jmin (sr * 0.45, 2500.0 + 3000.0 * bright * vel), 0.6);
        double ph = rng.uni(), vibPh = rng.uni() * kTwoPi, wander = 0.0, amp = 0.0;
        for (int i = onset; i < n; ++i)
        {
            const double t = (i - onset) / sr;
            wander += (rng.bi() * 6.0 - wander) * 0.00005;
            const double cents = det + wander + vibDepth * juce::jlimit (0.0, 1.0, (t - 0.25) / 0.5) * std::sin (vibPh);
            const double fr = f * std::pow (2.0, cents / 1200.0);
            const double dt = fr / sr;
            // glottal pulse: a band-limited saw with a softened top (spectral tilt)
            const double src = tilt.process (sawAt (ph, dt)) + 0.04 * rng.bi();
            ph += dt; if (ph >= 1.0) ph -= 1.0;
            vibPh += kTwoPi * vibRate / sr;
            double y = 0.0;
            for (int k = 0; k < 5; ++k) y += form[(size_t) k].process (src) * fm.amp[k];
            amp += ((0.3 + 0.7 * vel) - amp) * (1.0 / (0.12 * sr));   // ~0.25 s swell in
            y *= amp * 0.5 / std::sqrt ((double) voices);
            L[(size_t) i] += y * gl;
            R[(size_t) i] += y * gr;
        }
    }
    return stereo (L, R);
}

//==============================================================================
// Steel drum, jaw harp
//==============================================================================
juce::AudioBuffer<float> renderSteelDrum (const NoteSpec& ns)
{
    const double sr = ns.sampleRate, f = midiToHz (ns.pitch);
    const int n = (int) (ns.seconds * sr);
    Rng rng (ns.seed * 61u + (juce::uint32) ns.pitch);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    struct P { double ratio, amp, t60; };
    const P parts[] { { 1.0, 1.0, 2.0 }, { 2.0, 0.55, 1.4 }, { 3.0, 0.3, 0.9 }, { 4.24, 0.12 * vel, 0.3 }, { 5.1, 0.06 * vel, 0.2 } };
    std::vector<double> m ((size_t) n, 0.0);
    for (auto& p : parts)
    {
        const double fr = f * p.ratio * std::pow (2.0, rng.bi() * 3.0 / 1200.0);
        if (fr > sr * 0.45) continue;
        double a = p.amp * (0.3 + 0.7 * vel);
        const double d = std::exp (-6.9 / (sr * p.t60 * std::pow (440.0 / f, 0.3)));
        const double ph0 = rng.uni();
        for (int i = 0; i < n && a > 1.0e-7; ++i) { m[(size_t) i] += a * std::sin (kTwoPi * (ph0 + fr * i / sr)); a *= d; }
    }
    Biquad mallet; mallet.set (Biquad::bandpass, sr, 1500.0 + 2500.0 * vel, 0.8);
    for (int i = 0; i < (int) (0.003 * sr); ++i) m[(size_t) i] += mallet.process (rng.bi()) * 0.3 * vel;
    std::vector<double> L ((size_t) n), R ((size_t) n);
    const int d = (int) (0.0006 * sr);
    for (int i = 0; i < n; ++i) { L[(size_t) i] = m[(size_t) i] * 0.35; R[(size_t) i] = (i >= d ? m[(size_t) (i - d)] : 0.0) * 0.35; }
    return stereo (L, R);
}

juce::AudioBuffer<float> renderJawHarp (const NoteSpec& ns)
{
    const double sr = ns.sampleRate, f = midiToHz (ns.pitch);
    const int n = (int) (ns.seconds * sr);
    Rng rng (ns.seed * 19u + (juce::uint32) ns.pitch);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    std::vector<double> L ((size_t) n), R ((size_t) n);
    double ph = 0.0;
    Biquad form;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        // the mouth sweeps the resonance up after the twang (the "boing")
        const double fc = 350.0 + 1700.0 * (1.0 - std::exp (-t / 0.18)) * (0.6 + 0.4 * vel);
        if ((i & 31) == 0) form.set (Biquad::bandpass, sr, fc, 6.0);
        const double src = sawAt (ph, f / sr);
        ph += f / sr; if (ph >= 1.0) ph -= 1.0;
        const double env = std::exp (-t / 0.9) * (0.3 + 0.7 * vel) * juce::jmin (1.0, t / 0.002);
        const double y = (form.process (src) * 1.2 + src * 0.08) * env * 0.4;
        L[(size_t) i] = y; R[(size_t) i] = y;
    }
    return stereo (L, R);
}

//==============================================================================
// Synths
//==============================================================================
/** variant: 0 saw lead, 1 square lead, 2 supersaw lead, 3 warm pad, 4 glass pad, 5 pluck, 6 bell, 7 synth bass, 8 808 bass, 9 string pad */
juce::AudioBuffer<float> renderSynth (const NoteSpec& ns, int variant)
{
    const double sr = ns.sampleRate, f = midiToHz (ns.pitch);
    const int n = (int) (ns.seconds * sr);
    Rng rng (ns.seed * 1031u + (juce::uint32) ns.pitch * 7u + (juce::uint32) variant);
    const double vel = juce::jlimit (0.05, 1.0, (double) ns.velocity);
    std::vector<double> L ((size_t) n, 0.0), R ((size_t) n, 0.0);

    if (variant == 6)   // FM bell: carrier with a 3.5x modulator, index falling
    {
        double cp = 0.0, mp = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double idx = (1.5 + 3.0 * vel) * std::exp (-t / 0.6);
            const double y = std::sin (kTwoPi * cp + idx * std::sin (kTwoPi * mp)) * std::exp (-t / 2.2) * (0.3 + 0.7 * vel) * 0.35;
            cp += f / sr; mp += f * 3.5 / sr;
            L[(size_t) i] = y; R[(size_t) i] = y;
        }
        return stereo (L, R);
    }
    if (variant == 8)   // 808 bass: sine with a pitch drop at the start, soft drive, long decay
    {
        double ph = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double fr = f * std::pow (2.0, (7.0 * std::exp (-t / 0.012)) / 12.0);
            ph += fr / sr;
            const double env = std::exp (-t / 1.4) * juce::jmin (1.0, t / 0.0015);
            const double y = std::tanh (std::sin (kTwoPi * ph) * (1.2 + 1.2 * vel)) * env * 0.6;
            L[(size_t) i] = y; R[(size_t) i] = y;
        }
        return stereo (L, R);
    }

    const int voices = variant == 2 ? 7 : variant == 3 ? 5 : variant == 9 ? 6 : variant == 4 ? 3 : 1;
    const double detune = variant == 2 ? 18.0 : variant == 3 ? 9.0 : variant == 9 ? 12.0 : variant == 4 ? 4.0 : 0.0;
    std::vector<double> ph ((size_t) voices), panv ((size_t) voices);
    for (int v = 0; v < voices; ++v) { ph[(size_t) v] = rng.uni(); panv[(size_t) v] = voices > 1 ? -0.8 + 1.6 * v / (voices - 1) : 0.0; }
    Biquad fl, fr2;
    double vibPh = 0.0;
    const double attack = variant == 3 || variant == 9 ? 0.45 : variant == 4 ? 0.25 : variant == 5 || variant == 7 ? 0.002 : 0.01;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double vib = (variant <= 2 ? 0.12 : 0.0) * juce::jlimit (0.0, 1.0, (t - 0.4) / 0.4) * std::sin (vibPh);
        vibPh += kTwoPi * 5.4 / sr;
        double l = 0.0, r = 0.0;
        for (int v = 0; v < voices; ++v)
        {
            const double cents = voices > 1 ? detune * (2.0 * v / (voices - 1) - 1.0) : 0.0;
            const double fq = f * std::pow (2.0, (cents / 100.0 + vib) / 12.0);
            const double dt = fq / sr;
            double s;
            if (variant == 1) s = pulseAt (ph[(size_t) v], dt, 0.5);
            else if (variant == 4) s = std::sin (kTwoPi * ph[(size_t) v]) * 0.7 + std::sin (kTwoPi * ph[(size_t) v] * 2.0) * 0.3;
            else s = sawAt (ph[(size_t) v], dt);
            if (variant == 7) s = s * 0.7 + pulseAt (std::fmod (ph[(size_t) v] * 0.5, 1.0), dt * 0.5, 0.5) * 0.5;   // sub
            ph[(size_t) v] += dt; if (ph[(size_t) v] >= 1.0) ph[(size_t) v] -= 1.0;
            const double a = (panv[(size_t) v] + 1.0) * 0.25 * std::numbers::pi;
            l += s * std::cos (a); r += s * std::sin (a);
        }
        l /= std::sqrt ((double) voices); r /= std::sqrt ((double) voices);
        // filter: plucks and basses close fast; pads stay soft; leads open with velocity
        double cutoff;
        switch (variant)
        {
            case 5: cutoff = 300.0 + (5000.0 * vel) * std::exp (-t / 0.18); break;
            case 7: cutoff = 180.0 + (2200.0 * vel) * std::exp (-t / 0.12); break;
            case 3: case 9: cutoff = 1400.0 + 1600.0 * vel; break;
            case 4: cutoff = 6000.0; break;
            default: cutoff = 2500.0 + 5000.0 * vel; break;
        }
        if ((i & 15) == 0) { fl.set (Biquad::lowpass, sr, cutoff, variant == 7 ? 2.2 : 0.9); fr2 = fl; }
        double env = juce::jmin (1.0, t / attack);
        if (variant == 5) env *= std::exp (-t / 0.9);
        if (variant == 7) env *= 0.55 + 0.45 * std::exp (-t / 0.6);
        L[(size_t) i] = fl.process (l) * env * (0.35 + 0.65 * vel) * 0.3;
        R[(size_t) i] = fr2.process (r) * env * (0.35 + 0.65 * vel) * 0.3;
    }
    return stereo (L, R);
}

//==============================================================================
// Trap kit (synthesized drum hits from the Band Forge drum models)
//==============================================================================
juce::AudioBuffer<float> renderTrapHit (const NoteSpec& ns)
{
    int note = ns.pitch;
    if (note == 35) note = 36;
    return pg::band::renderSingleNote (pg::band::Instrument::trapDrums, note, ns.velocity, ns.seconds, ns.sampleRate);
}

std::map<juce::String, Renderer>& registry()
{
    static std::map<juce::String, Renderer> r {
        { "acoustic-guitar",        [] (const NoteSpec& n) { return renderPlucked (n, acousticSteel(), n.variant == 1); } },
        { "nylon-guitar",           [] (const NoteSpec& n) { return renderPlucked (n, nylon(), n.variant == 1); } },
        { "twelve-string",          [] (const NoteSpec& n) { return renderPlucked (n, twelveString(), n.variant == 1); } },
        { "mandolin",               [] (const NoteSpec& n) { return renderPlucked (n, mandolin(), n.variant == 1); } },
        { "banjo",                  [] (const NoteSpec& n) { return renderPlucked (n, banjo(), n.variant == 1); } },
        { "bouzouki",               [] (const NoteSpec& n) { return renderPlucked (n, bouzouki(), n.variant == 1); } },
        { "resonator-guitar",       [] (const NoteSpec& n) { return renderPlucked (n, resonator(), n.variant == 1); } },
        { "pedal-steel",            [] (const NoteSpec& n) { return renderPlucked (n, steelGuitar (false), false); } },
        { "lap-steel",              [] (const NoteSpec& n) { return renderPlucked (n, steelGuitar (true), false); } },
        { "koto",                   [] (const NoteSpec& n) { return renderPlucked (n, koto(), false); } },
        { "sitar",                  [] (const NoteSpec& n) { return renderPlucked (n, sitar(), false); } },
        { "electric-guitar-clean",  [] (const NoteSpec& n) { return renderPlucked (n, electricGuitar (false), n.variant == 1); } },
        { "electric-guitar-crunch", [] (const NoteSpec& n) { return renderPlucked (n, electricGuitar (true), n.variant == 1); } },
        { "electric-bass",          [] (const NoteSpec& n) { return renderPlucked (n, electricBass (n.variant), false); } },
        { "tine-piano",             [] (const NoteSpec& n) { return renderTine (n, false); } },
        { "reed-piano",             [] (const NoteSpec& n) { return renderTine (n, true); } },
        { "clavinet",               [] (const NoteSpec& n) { return renderClav (n, n.variant == 1); } },
        { "tonewheel-organ",        [] (const NoteSpec& n) { return renderOrgan (n, n.variant); } },
        { "accordion",              [] (const NoteSpec& n) {
                                         ReedSpec s;
                                         s.reeds = n.variant == 0 ? std::vector<Course> { { 0, 0, 1.0 }, { 0, 11, 0.8 }, { 0, -11, 0.8 } }
                                                 : n.variant == 1 ? std::vector<Course> { { 0, 0, 1.0 }, { -12, 0, 0.7 } }
                                                                  : std::vector<Course> { { 0, 0, 1.0 }, { 12, 2, 0.5 } };
                                         s.body = { { 820, 3, 4 }, { 2200, 3, 3 } };
                                         return renderReed (n, s); } },
        { "harmonium",              [] (const NoteSpec& n) {
                                         ReedSpec s; s.reeds = { { 0, 0, 1.0 }, { -12, 1, 0.6 } }; s.width = 0.42; s.brightHz = 1800.0; s.brightVelHz = 1500.0;
                                         s.body = { { 500, 2, 3 }, { 1300, 3, 2 } }; s.attack = 0.08; s.breath = 0.01;
                                         return renderReed (n, s); } },
        { "melodica",               [] (const NoteSpec& n) {
                                         ReedSpec s; s.reeds = { { 0, 0, 1.0 } }; s.width = 0.25; s.brightHz = 3000.0; s.brightVelHz = 3500.0;
                                         s.body = { { 1100, 3, 4 }, { 2600, 4, 3 } }; s.attack = 0.03; s.breath = 0.05;
                                         return renderReed (n, s); } },
        { "bagpipe",                [] (const NoteSpec& n) {
                                         ReedSpec s; s.reeds = { { 0, 0, 1.0 } }; s.width = 0.12; s.brightHz = 5200.0; s.brightVelHz = 0.0;
                                         s.body = { { 1250, 4, 6 }, { 2600, 5, 5 }, { 3800, 5, 2 } }; s.attack = 0.02; s.breath = 0.01; s.constantLevel = true;
                                         return renderReed (n, s); } },
        { "bagpipe-drones",         [] (const NoteSpec& n) {
                                         ReedSpec s; s.reeds = { { 0, 0, 1.0 }, { -12, 0, 1.0 }, { 12, 0, 0.5 } }; s.width = 0.2; s.brightHz = 3000.0; s.brightVelHz = 0.0;
                                         s.body = { { 900, 3, 4 } }; s.attack = 0.15; s.breath = 0.005; s.constantLevel = true;
                                         return renderReed (n, s); } },
        { "pan-flute",              [] (const NoteSpec& n) { return renderFlute (n, false); } },
        { "tin-whistle",            [] (const NoteSpec& n) { return renderFlute (n, true); } },
        { "choir",                  [] (const NoteSpec& n) { return renderChoir (n, n.variant, 6, 0.4); } },
        { "gospel-choir",           [] (const NoteSpec& n) { return renderChoir (n, n.variant, 10, 0.9); } },
        { "steel-drum",             [] (const NoteSpec& n) { return renderSteelDrum (n); } },
        { "jaw-harp",               [] (const NoteSpec& n) { return renderJawHarp (n); } },
        { "synth-lead",             [] (const NoteSpec& n) { return renderSynth (n, n.variant); } },
        { "warm-pad",               [] (const NoteSpec& n) { return renderSynth (n, n.variant == 0 ? 3 : n.variant == 1 ? 9 : 4); } },
        { "synth-pluck",            [] (const NoteSpec& n) { return renderSynth (n, 5); } },
        { "synth-bell",             [] (const NoteSpec& n) { return renderSynth (n, 6); } },
        { "synth-bass",             [] (const NoteSpec& n) { return renderSynth (n, 7); } },
        { "bass-808",               [] (const NoteSpec& n) { return renderSynth (n, 8); } },
        { "kit-trap",               [] (const NoteSpec& n) { return renderTrapHit (n); } },
    };
    return r;
}
} // namespace

double midiToHz (double midi) noexcept { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

Renderer renderer (const juce::String& model)
{
    auto& r = registry();
    auto it = r.find (model);
    return it == r.end() ? Renderer() : it->second;
}

juce::StringArray modelNames()
{
    juce::StringArray s;
    for (auto& [k, v] : registry()) s.add (k);
    return s;
}
} // namespace hf::models
