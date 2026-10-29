#include "Stage/Stage.h"
#include <cmath>

namespace hf
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kSpeed = 343.0f;          // speed of sound, m/s
constexpr float kHeadRadius = 0.0875f;    // m
constexpr float kRefDistance = 1.6f;      // a player at the front centre, close mics
constexpr float kDirectExp = 0.7f;        // level vs distance (1 = inverse square law in power)
constexpr float kMaxDirectMs = 150.0f, kMaxEchoMs = 420.0f, kMaxHeadMs = 2.0f;

int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }

struct Vec { float x, y, z; };
float len (Vec a) { return std::sqrt (a.x * a.x + a.y * a.y + a.z * a.z); }
}

const std::vector<RoomPreset>& roomPresets()
{
    static const std::vector<RoomPreset> p {
        //  name                 W     L     H   refl  rtLo  rtHi  pre  diff  tail   walls
        { "Dry Booth",          3.0f, 3.5f, 2.6f, 0.25f, 0.22f, 0.15f, 2.0f, 0.5f, -10.0f, true },
        { "Studio Live Room",   7.0f, 6.0f, 3.6f, 0.45f, 0.65f, 0.45f, 6.0f, 0.65f, -6.0f, true },
        { "Wood Barn",         14.0f, 11.0f, 6.0f, 0.55f, 1.30f, 0.85f, 12.0f, 0.7f, -5.0f, true },
        { "Small Church",      14.0f, 24.0f, 10.0f, 0.62f, 2.60f, 1.60f, 18.0f, 0.75f, -4.0f, true },
        { "Concert Hall",      26.0f, 38.0f, 15.0f, 0.55f, 2.20f, 1.50f, 24.0f, 0.8f, -4.0f, true },
        { "Plate",              4.0f, 4.0f, 3.0f, 0.0f, 2.00f, 1.80f, 8.0f, 0.85f, -5.0f, false },
        { "Open Air",          40.0f, 40.0f, 30.0f, 0.0f, 0.35f, 0.20f, 20.0f, 0.3f, -18.0f, false },
    };
    return p;
}

juce::StringArray roomNames()
{
    juce::StringArray s;
    for (auto& r : roomPresets()) s.add (r.name);
    return s;
}

//==============================================================================
void StageTrack::Delay::prepare (int minLen)
{
    const int n = nextPow2 (juce::jmax (8, minLen));
    buf.assign ((size_t) n, 0.0f);
    mask = n - 1;
    write = 0;
}

float StageTrack::Delay::read (float d) const noexcept
{
    d = juce::jlimit (0.0f, (float) mask - 2.0f, d);
    const int di = (int) d;
    const float fr = d - (float) di;
    const int i0 = (write - 1 - di) & mask;
    const int i1 = (i0 - 1) & mask;
    return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
}

void StageTrack::Ear::set (double fs, float incidenceDeg) noexcept
{
    // spherical-head shadow (single-pole / single-zero head model), bilinear transform
    const float alpha = 1.05f + 0.95f * std::cos (juce::degreesToRadians (incidenceDeg * 180.0f / 150.0f));
    const float w0 = kSpeed / kHeadRadius;
    const float tau = 1.0f / (2.0f * w0);
    const float K = 2.0f * (float) fs;
    const float den = 1.0f + tau * K;
    b0 = (1.0f + alpha * tau * K) / den;
    b1 = (1.0f - alpha * tau * K) / den;
    a1 = (1.0f - tau * K) / den;
}

void StageTrack::prepare (double fs, int)
{
    sr = fs;
    dL.prepare ((int) (kMaxDirectMs * 0.001 * fs) + 4);
    dR.prepare ((int) (kMaxDirectMs * 0.001 * fs) + 4);
    er.prepare ((int) (kMaxEchoMs * 0.001 * fs) + 4);
    hL.prepare ((int) (kMaxHeadMs * 0.001 * fs) + 4);
    hR.prepare ((int) (kMaxHeadMs * 0.001 * fs) + 4);
    reset();
}

void StageTrack::reset()
{
    dL.clear(); dR.clear(); er.clear(); hL.clear(); hR.clear();
    lpL = lpR = 0.0f;
    for (auto& e : ears) { e.x1 = e.y1 = 0.0f; }
    first = true;
    lastAz = 1000.0f;
}

StageTrack::Geometry StageTrack::computeGeometry (const StageParams& p, const GlobalStage& g) noexcept
{
    const auto& presets = roomPresets();
    const auto& room = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, g.room)];
    const float s = juce::jlimit (0.5f, 1.5f, g.roomSize);
    const float W = room.width * s, L = room.length * s, H = room.height * s;
    const float yFront = -0.45f * L, yBack = 0.55f * L;
    const float stageDepth = juce::jmin (0.4f * L, 8.0f);

    const float mic = juce::jlimit (0.0f, 1.0f, g.micDistance);
    const Vec m { 0.0f, juce::jmax (yFront + 0.6f, -(1.0f + mic * juce::jmax (0.0f, 0.45f * L - 2.0f))), 1.6f };
    const Vec src { juce::jlimit (-1.0f, 1.0f, p.x) * W * 0.42f,
                    0.5f + juce::jlimit (0.0f, 1.0f, p.depth) * stageDepth,
                    juce::jmin (H - 0.3f, 0.8f + juce::jlimit (0.0f, 1.0f, p.height) * 2.2f) };

    Geometry geo;
    const Vec dv { src.x - m.x, src.y - m.y, src.z - m.z };
    geo.distance = juce::jmax (0.3f, len (dv));
    geo.azimuth = std::atan2 (dv.x, dv.y);
    geo.gain = std::pow (kRefDistance / juce::jmax (kRefDistance, geo.distance), kDirectExp);
    geo.delayMs = juce::jlimit (0.0f, kMaxDirectMs - 5.0f, (geo.distance - kRefDistance) / kSpeed * 1000.0f);
    geo.airHz = juce::jlimit (3000.0f, 20000.0f, 20000.0f / (1.0f + juce::jmax (0.0f, geo.distance - kRefDistance) / 18.0f));
    const float far = juce::jlimit (0.0f, 1.0f, (geo.distance - kRefDistance) / (4.0f * kRefDistance));
    geo.sendGain = juce::Decibels::decibelsToGain (p.roomSendDb) * (0.3f + 0.7f * far) * (0.6f + 0.8f * mic);

    // first reflections from the image sources (walls / floor / ceiling)
    const Vec images[6] {
        { -W - src.x, src.y, src.z }, { W - src.x, src.y, src.z },
        { src.x, 2.0f * yBack - src.y, src.z }, { src.x, 2.0f * yFront - src.y, src.z },
        { src.x, src.y, -src.z }, { src.x, src.y, 2.0f * H - src.z } };
    for (int i = 0; i < 6; ++i)
    {
        const bool wallTap = i != 4;
        const Vec iv { images[i].x - m.x, images[i].y - m.y, images[i].z - m.z };
        const float di = juce::jmax (0.3f, len (iv));
        const float refl = wallTap ? room.reflectivity : juce::jmax (0.3f, room.reflectivity);   // a floor always reflects
        geo.tapGain[(size_t) i] = (room.walls || ! wallTap) ? refl * std::pow (kRefDistance / di, 0.85f) : 0.0f;
        geo.tapDelayMs[(size_t) i] = juce::jlimit (0.0f, kMaxEchoMs - 5.0f, (di - kRefDistance) / kSpeed * 1000.0f);
        geo.tapPan[(size_t) i] = juce::jlimit (-1.0f, 1.0f, std::sin (std::atan2 (iv.x, iv.y)) / 0.87f);
    }
    return geo;
}

void StageTrack::process (const float* inL, const float* inR, float* outL, float* outR, float* sendL, float* sendR,
                          int n, const StageParams& p, const GlobalStage& g) noexcept
{
    target = computeGeometry (p, g);
    const float pan = juce::jlimit (-1.0f, 1.0f, std::sin (target.azimuth) / 0.87f);
    const float pa = (pan + 1.0f) * 0.25f * kPi;
    const float tPanL = std::cos (pa), tPanR = std::sin (pa);
    const float closeness = juce::jlimit (0.35f, 1.0f, 2.0f * kRefDistance / target.distance);
    const float tWidth = juce::jlimit (0.0f, 1.5f, p.width) * closeness;
    const float tAir = std::exp (-2.0f * kPi * target.airHz / (float) sr);
    const float delaySamples = target.delayMs * 0.001f * (float) sr;
    std::array<float, 6> tTapD {}, tTapL {}, tTapR {};
    for (int i = 0; i < 6; ++i)
    {
        tTapD[(size_t) i] = target.tapDelayMs[(size_t) i] * 0.001f * (float) sr;
        const float a = (target.tapPan[(size_t) i] + 1.0f) * 0.25f * kPi;
        tTapL[(size_t) i] = std::cos (a) * target.tapGain[(size_t) i];
        tTapR[(size_t) i] = std::sin (a) * target.tapGain[(size_t) i];
    }

    // headphone 3D: ear filters for the instrument's two channels (+-spread around the seat)
    if (g.headphone3D && std::abs (target.azimuth - lastAz) > 0.002f)
    {
        lastAz = target.azimuth;
        const float spread = juce::degreesToRadians (12.0f) * tWidth;
        const float az[2] { target.azimuth - spread, target.azimuth + spread };
        for (int s = 0; s < 2; ++s)
        {
            const float a = juce::jlimit (-kPi * 0.5f, kPi * 0.5f, az[s]);   // front hemisphere (stage is in front)
            const float degs = juce::radiansToDegrees (a);
            ears[(size_t) s * 2 + 0].set (sr, std::abs (degs + 90.0f));   // left ear at -90
            ears[(size_t) s * 2 + 1].set (sr, std::abs (degs - 90.0f));   // right ear at +90
            const float itd = kHeadRadius / kSpeed * (std::abs (a) + std::sin (std::abs (a)));   // Woodworth
            // the far ear hears it later
            sItd[s * 2 + 0] = a > 0.0f ? itd * (float) sr : 0.0f;
            sItd[s * 2 + 1] = a < 0.0f ? itd * (float) sr : 0.0f;
        }
    }

    if (first)
    {
        first = false;
        sGain = target.gain; sDelay = delaySamples; sPanL = tPanL; sPanR = tPanR; sWidth = tWidth;
        sSend = target.sendGain; sAirCoef = tAir;
        sTapDelay = tTapD; sTapL = tTapL; sTapR = tTapR;
    }
    geo = target;

    // ~20 ms smoothing so moving a seat while playing glides instead of clicking
    const float k = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        sGain += k * (target.gain - sGain);
        sDelay += k * (delaySamples - sDelay);
        sPanL += k * (tPanL - sPanL);
        sPanR += k * (tPanR - sPanR);
        sWidth += k * (tWidth - sWidth);
        sSend += k * (target.sendGain - sSend);
        sAirCoef += k * (tAir - sAirCoef);

        // width (mid/side) then distance level and air absorption
        const float mid = 0.5f * (inL[i] + inR[i]), side = 0.5f * (inL[i] - inR[i]) * sWidth;
        float l = (mid + side) * sGain, r = (mid - side) * sGain;
        lpL = l + sAirCoef * (lpL - l);
        lpR = r + sAirCoef * (lpR - r);
        dL.push (lpL);
        dR.push (lpR);
        const float xl = dL.read (sDelay), xr = dR.read (sDelay);

        float oL, oR;
        if (g.headphone3D)
        {
            hL.push (xl);
            hR.push (xr);
            const float sLl = hL.read (sItd[0]), sLr = hL.read (sItd[1]);
            const float sRl = hR.read (sItd[2]), sRr = hR.read (sItd[3]);
            oL = ears[0].process (sLl) + ears[2].process (sRl);
            oR = ears[1].process (sLr) + ears[3].process (sRr);
            oL *= 0.6f; oR *= 0.6f;
        }
        else
        {
            // stereo: the instrument's own image, steered toward its seat
            const float c = 0.5f * (xl + xr), sd = 0.5f * (xl - xr);
            // constant-power pan of the centre, unity at centre (cos(pi/4) * sqrt2 = 1)
            oL = c * sPanL * 1.41421356f + sd;
            oR = c * sPanR * 1.41421356f - sd;
        }

        // early echoes from the mono source (before the distance level: the taps carry their own)
        er.push (0.5f * (lpL + lpR) / juce::jmax (1.0e-3f, sGain));
        float eL = 0.0f, eR = 0.0f;
        for (int t = 0; t < 6; ++t)
        {
            sTapDelay[(size_t) t] += k * (tTapD[(size_t) t] - sTapDelay[(size_t) t]);
            sTapL[(size_t) t] += k * (tTapL[(size_t) t] - sTapL[(size_t) t]);
            sTapR[(size_t) t] += k * (tTapR[(size_t) t] - sTapR[(size_t) t]);
            const float e = er.read (sTapDelay[(size_t) t]);
            eL += e * sTapL[(size_t) t];
            eR += e * sTapR[(size_t) t];
        }
        outL[i] += oL + eL * 0.6f;
        outR[i] += oR + eR * 0.6f;
        sendL[i] += (xl + eL * 0.5f) * sSend;
        sendR[i] += (xr + eR * 0.5f) * sSend;
    }
}

//==============================================================================
float RoomTail::Allpass::process (float x) noexcept
{
    const float d = buf[(size_t) pos];
    const float y = d - g * x;
    buf[(size_t) pos] = x + g * y;
    if (++pos >= len) pos = 0;
    return y;
}

void RoomTail::prepare (double fs, int)
{
    sr = fs;
    const int lineMax = nextPow2 ((int) (0.34 * fs) + 8);
    for (auto& l : lines) { l.buf.assign ((size_t) lineMax, 0.0f); l.mask = lineMax - 1; l.write = 0; }
    static const float diffMs[4] { 4.77f, 3.59f, 12.73f, 9.30f };
    for (int i = 0; i < 4; ++i)
    {
        diff[(size_t) i].len = juce::jmax (1, (int) std::lround (diffMs[i] * 0.001 * fs));
        diff[(size_t) i].buf.assign ((size_t) diff[(size_t) i].len, 0.0f);
        diff[(size_t) i].pos = 0;
    }
    pre.assign ((size_t) (0.06 * fs) + 4, 0.0f);
    preWrite = 0;
    lfoInc = (float) (2.0 * kPi * 0.37 / fs);
    configuredRoom = -1;
    configuredSize = -1.0f;
    dampCoef = std::exp (-2.0f * kPi * 1000.0f / (float) fs);
    configure (roomPresets()[1], 1.0f);
}

void RoomTail::reset()
{
    for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.lp = 0.0f; }
    for (auto& d : diff) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
    std::fill (pre.begin(), pre.end(), 0.0f);
}

void RoomTail::configure (const RoomPreset& r, float sizeScale) noexcept
{
    const int idx = (int) (&r - roomPresets().data());
    if (idx == configuredRoom && std::abs (sizeScale - configuredSize) < 1.0e-4f) return;
    configuredRoom = idx;
    configuredSize = sizeScale;
    currentPreset = &r;
    static const float base[kLines] { 29.7f, 37.1f, 41.1f, 43.7f, 47.3f, 53.9f, 59.3f, 67.1f };
    const float vol = std::cbrt (r.width * r.length * r.height) / std::cbrt (7.0f * 6.0f * 3.6f);
    const float scale = juce::jlimit (0.25f, 2.6f, 0.55f * vol) * juce::jlimit (0.5f, 1.5f, sizeScale);
    const float rtScale = juce::jlimit (0.5f, 1.5f, sizeScale);
    for (int i = 0; i < kLines; ++i)
    {
        auto& l = lines[(size_t) i];
        l.delay = juce::jlimit (16.0f, (float) l.mask - 64.0f, base[i] * scale * 0.001f * (float) sr);
        const float dSec = l.delay / (float) sr;
        l.gLow = std::pow (10.0f, -3.0f * dSec / juce::jmax (0.05f, r.rtLow * rtScale));
        l.gHigh = std::pow (10.0f, -3.0f * dSec / juce::jmax (0.05f, r.rtHigh * rtScale));
    }
    for (auto& d : diff) d.g = 0.45f + 0.3f * r.diffusion;
    preLen = juce::jlimit (1, (int) pre.size() - 1, (int) (r.predelayMs * 0.001f * (float) sr));
    level = juce::Decibels::decibelsToGain (r.tailLevelDb);
}

void RoomTail::process (float* l, float* r, int n) noexcept
{
    constexpr float h = 0.35355339f;   // 1/sqrt(8)
    for (int i = 0; i < n; ++i)
    {
        // predelay + diffusion
        const float in = 0.5f * (l[i] + r[i]);
        pre[(size_t) preWrite] = in;
        int rp = preWrite - preLen; if (rp < 0) rp += (int) pre.size();
        preWrite = (preWrite + 1) % (int) pre.size();
        float x = pre[(size_t) rp];
        for (auto& d : diff) x = d.process (x);

        lfo += lfoInc; if (lfo > 2.0f * kPi) lfo -= 2.0f * kPi;
        const float mod = std::sin (lfo) * 0.0003f * (float) sr;

        float y[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            auto& ln = lines[(size_t) k];
            float d = ln.delay + ((k == 1 || k == 6) ? mod : (k == 3 ? -mod : 0.0f));
            const int di = (int) d;
            const float fr = d - (float) di;
            const int i0 = (ln.write - di) & ln.mask, i1 = (i0 - 1) & ln.mask;
            const float v = ln.buf[(size_t) i0] + fr * (ln.buf[(size_t) i1] - ln.buf[(size_t) i0]);
            // two-band decay: low part decays with rtLow, high part with rtHigh
            ln.lp = v + dampCoef * (ln.lp - v);
            y[k] = ln.lp * ln.gLow + (v - ln.lp) * ln.gHigh;
        }
        // Hadamard 8x8 (fast butterfly)
        float a[kLines];
        for (int k = 0; k < kLines; ++k) a[k] = y[k];
        for (int len = 1; len < kLines; len <<= 1)
            for (int s = 0; s < kLines; s += 2 * len)
                for (int j = s; j < s + len; ++j) { const float u = a[j], v = a[j + len]; a[j] = u + v; a[j + len] = u - v; }
        for (int k = 0; k < kLines; ++k)
        {
            auto& ln = lines[(size_t) k];
            ln.buf[(size_t) ln.write] = a[k] * h + x * ((k & 1) ? -0.5f : 0.5f);
            ln.write = (ln.write + 1) & ln.mask;
        }
        l[i] = (y[0] - y[2] + y[4] - y[6]) * 0.5f * level;
        r[i] = (y[1] - y[3] + y[5] - y[7]) * 0.5f * level;
    }
}
} // namespace hf
