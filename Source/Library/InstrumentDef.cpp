#include "Library/InstrumentDef.h"
#include <cmath>

namespace hf
{
float Zone::crossfadeGain (int v) const noexcept
{
    float g = 1.0f;
    constexpr float halfPi = 1.57079632679f;
    if (xinLo >= 0 && xinHi > xinLo)
    {
        if (v <= xinLo) return 0.0f;
        if (v < xinHi) g *= std::sin (halfPi * (float) (v - xinLo) / (float) (xinHi - xinLo));
    }
    if (xoutLo >= 0 && xoutHi > xoutLo)
    {
        if (v >= xoutHi) return 0.0f;
        if (v > xoutLo) g *= std::cos (halfPi * (float) (v - xoutLo) / (float) (xoutHi - xoutLo));
    }
    return g;
}

int InstrumentDef::numZones() const noexcept
{
    int n = 0;
    for (auto& a : articulations) n += (int) (a.zones.size() + a.releaseZones.size());
    return n;
}

namespace
{
bool readZone (const juce::var& v, Zone& z, juce::String& error)
{
    if (! v.isObject()) { error = "zone is not an object"; return false; }
    z.file = v["file"].toString();
    if (z.file.isEmpty() || z.file.contains ("..") || juce::File::isAbsolutePath (z.file)) { error = "bad zone file name"; return false; }
    z.frames = (int) v["frames"];
    z.root = (int) v["root"];
    z.lo = (int) v["lo"];
    z.hi = (int) v["hi"];
    z.vlo = v.hasProperty ("vlo") ? (int) v["vlo"] : 0;
    z.vhi = v.hasProperty ("vhi") ? (int) v["vhi"] : 127;
    z.gainDb = (float) (double) v.getProperty ("db", 0.0);
    z.tuneCents = juce::jlimit (-1200.0f, 1200.0f, (float) (double) v.getProperty ("tune", 0.0));
    z.rr = juce::jmax (1, (int) v.getProperty ("rr", 1));
    z.rrCount = juce::jmax (1, (int) v.getProperty ("rrn", 1));
    z.keytrack = (bool) v.getProperty ("keytrack", true);
    if (auto* l = v["loop"].getArray(); l != nullptr && l->size() == 2)
    {
        z.loopStart = (int) (*l)[0];
        z.loopEnd = (int) (*l)[1];
    }
    if (auto* x = v["xin"].getArray(); x != nullptr && x->size() == 2) { z.xinLo = (int) (*x)[0]; z.xinHi = (int) (*x)[1]; }
    if (auto* x = v["xout"].getArray(); x != nullptr && x->size() == 2) { z.xoutLo = (int) (*x)[0]; z.xoutHi = (int) (*x)[1]; }
    z.pedal = v.hasProperty ("pedal") ? (int) v["pedal"] : -1;
    z.release = v.hasProperty ("release") ? (float) (double) v["release"] : -1.0f;
    if (z.frames <= 0 || z.root < -24 || z.root > 151 || z.lo < 0 || z.hi > 127 || z.lo > z.hi
        || z.vlo < 0 || z.vhi > 127 || z.vlo > z.vhi || z.rr > z.rrCount)
    {
        error = "zone values out of range (" + z.file + ")";
        return false;
    }
    if (z.loopStart >= 0 && (z.loopEnd > z.frames || z.loopEnd <= z.loopStart))
    {
        z.loopStart = z.loopEnd = -1;   // a broken loop plays as a one-shot
    }
    return true;
}
} // namespace

std::shared_ptr<InstrumentDef> InstrumentDef::load (const juce::File& folder, juce::String& error)
{
    const auto file = folder.getChildFile ("instrument.json");
    if (! file.existsAsFile()) { error = "No instrument.json in " + folder.getFullPathName(); return nullptr; }
    const auto json = juce::JSON::parse (file);
    if (! json.isObject() || json["format"].toString() != "hem-instrument") { error = file.getFullPathName() + " is not a HEM instrument"; return nullptr; }

    auto d = std::make_shared<InstrumentDef>();
    d->folder = folder;
    d->id = json["id"].toString();
    d->name = json["name"].toString();
    d->family = json["family"].toString();
    d->source = json["source"].toString();
    d->licence = json["licence"].toString();
    d->credit = json["credit"].toString();
    const auto kind = json["kind"].toString();
    d->kind = kind == "drums" ? InstrumentKind::drums : kind == "perc" ? InstrumentKind::perc : InstrumentKind::pitched;
    d->gainDb = juce::jlimit (-36.0f, 36.0f, (float) (double) json.getProperty ("gainDb", 0.0));
    d->velTrack = juce::jlimit (0.0f, 1.0f, (float) (double) json.getProperty ("velTrack", 0.6));
    d->mono = (bool) json.getProperty ("mono", false);
    if (auto* r = json["range"].getArray(); r != nullptr && r->size() == 2)
    {
        d->rangeLo = juce::jlimit (0, 127, (int) (*r)[0]);
        d->rangeHi = juce::jlimit (0, 127, (int) (*r)[1]);
    }
    const auto seat = json["seat"];
    if (seat.isObject())
    {
        d->seat.x = juce::jlimit (-1.0f, 1.0f, (float) (double) seat.getProperty ("x", 0.0));
        d->seat.depth = juce::jlimit (0.0f, 1.0f, (float) (double) seat.getProperty ("depth", 0.5));
        d->seat.height = juce::jlimit (0.0f, 1.0f, (float) (double) seat.getProperty ("height", 0.25));
    }
    d->sampleRate = (double) json.getProperty ("sampleRate", 48000.0);
    if (d->id.isEmpty() || d->name.isEmpty() || d->sampleRate < 8000.0) { error = file.getFullPathName() + ": missing id, name or sample rate"; return nullptr; }

    auto* arts = json["articulations"].getArray();
    if (arts == nullptr || arts->isEmpty()) { error = d->name + ": no articulations"; return nullptr; }
    for (auto& av : *arts)
    {
        Articulation a;
        a.name = av["name"].toString();
        a.release = juce::jlimit (0.005f, 20.0f, (float) (double) av.getProperty ("release", 0.3));
        a.loop = (bool) av.getProperty ("loop", false);
        if (auto* zs = av["zones"].getArray())
            for (auto& zv : *zs)
            {
                Zone z;
                if (! readZone (zv, z, error)) { error = d->name + ": " + error; return nullptr; }
                a.zones.push_back (std::move (z));
            }
        if (auto* zs = av["releaseZones"].getArray())
            for (auto& zv : *zs)
            {
                Zone z;
                if (! readZone (zv, z, error)) { error = d->name + ": " + error; return nullptr; }
                a.releaseZones.push_back (std::move (z));
            }
        if (a.zones.empty()) { error = d->name + ": articulation " + a.name + " has no zones"; return nullptr; }
        d->articulations.push_back (std::move (a));
    }
    return d;
}

juce::String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}
} // namespace hf
