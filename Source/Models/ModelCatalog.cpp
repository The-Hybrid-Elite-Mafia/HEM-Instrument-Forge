#include "Models/ModelCatalog.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace hf::models
{
namespace
{
constexpr double kRate = 48000.0;

ModelArticulation pluckArt (juce::String name, int variant = 0, double seconds = 4.0, float release = 0.18f, int layers = 3, int rr = 2)
{
    ModelArticulation a;
    a.name = std::move (name); a.variant = variant; a.seconds = seconds; a.release = release; a.velLayers = layers; a.roundRobin = rr;
    return a;
}
ModelArticulation heldArt (juce::String name, int variant = 0, double seconds = 6.0, float release = 0.3f, int layers = 1, double period = 0.0)
{
    ModelArticulation a;
    a.name = std::move (name); a.variant = variant; a.sustain = true; a.seconds = seconds; a.release = release;
    a.velLayers = layers; a.roundRobin = 1; a.loopPeriod = period;
    return a;
}

ModelInstrument inst (juce::String id, juce::String name, juce::String family, juce::String model, int lo, int hi,
                      std::vector<ModelArticulation> arts, int step = 3, bool mono = false, float velTrack = 0.35f)
{
    ModelInstrument m;
    m.id = std::move (id); m.name = std::move (name); m.family = std::move (family); m.model = std::move (model);
    m.lo = lo; m.hi = hi; m.arts = std::move (arts); m.step = step; m.mono = mono; m.velTrack = velTrack;
    return m;
}

std::vector<ModelInstrument> makeCatalog()
{
    const juce::String G = "Guitars & Plucked", B = "Bass", K = "Keys", F = "Folk & Free Reeds", W = "World",
                       WW = "Woodwinds", C = "Choir & Pads", S = "Synths", D = "Drum Kits";
    std::vector<ModelInstrument> c;
    c.push_back (inst ("acoustic-guitar", "Acoustic Guitar (Steel)", G, "acoustic-guitar", 40, 88, { pluckArt ("Pluck"), pluckArt ("Palm Muted", 1, 1.2, 0.08f) }));
    c.push_back (inst ("nylon-guitar", "Nylon Guitar", G, "nylon-guitar", 40, 83, { pluckArt ("Pluck"), pluckArt ("Palm Muted", 1, 1.2, 0.08f) }));
    c.push_back (inst ("twelve-string", "12-String Guitar", G, "twelve-string", 40, 84, { pluckArt ("Pluck", 0, 4.5) }));
    c.push_back (inst ("mandolin", "Mandolin", G, "mandolin", 55, 91, { pluckArt ("Pluck", 0, 2.5), pluckArt ("Muted", 1, 1.0, 0.06f) }));
    c.push_back (inst ("banjo", "Banjo", G, "banjo", 48, 86, { pluckArt ("Pluck", 0, 2.5), pluckArt ("Muted", 1, 1.0, 0.06f) }));
    c.push_back (inst ("bouzouki", "Bouzouki", G, "bouzouki", 43, 84, { pluckArt ("Pluck", 0, 3.5) }));
    c.push_back (inst ("resonator-guitar", "Resonator Guitar", G, "resonator-guitar", 40, 84, { pluckArt ("Pluck", 0, 4.0) }));
    c.push_back (inst ("pedal-steel", "Pedal Steel Guitar", G, "pedal-steel", 40, 88, { pluckArt ("Pluck", 0, 6.0, 0.4f, 3, 1) }));
    c.push_back (inst ("lap-steel", "Lap Steel Guitar", G, "lap-steel", 40, 88, { pluckArt ("Pluck", 0, 5.0, 0.3f, 3, 1) }));
    c.push_back (inst ("electric-guitar-clean", "Electric Guitar (Clean)", G, "electric-guitar-clean", 40, 88, { pluckArt ("Pluck"), pluckArt ("Palm Muted", 1, 1.0, 0.06f) }));
    c.push_back (inst ("electric-guitar-crunch", "Electric Guitar (Crunch)", G, "electric-guitar-crunch", 40, 88, { pluckArt ("Pluck", 0, 4.5), pluckArt ("Palm Muted", 1, 1.0, 0.06f) }));
    c.push_back (inst ("koto", "Koto", W, "koto", 43, 86, { pluckArt ("Pluck", 0, 4.0, 0.4f) }));
    c.push_back (inst ("sitar", "Sitar", W, "sitar", 48, 84, { pluckArt ("Pluck", 0, 5.0, 0.4f, 3, 1) }));

    c.push_back (inst ("electric-bass", "Electric Bass", B, "electric-bass", 28, 67,
                       { pluckArt ("Finger", 0, 4.0, 0.08f), pluckArt ("Pick", 1, 4.0, 0.08f), pluckArt ("Slap", 2, 3.0, 0.08f) }));
    c.push_back (inst ("synth-bass", "Synth Bass", B, "synth-bass", 24, 60, { heldArt ("Synth Bass", 0, 4.0, 0.06f, 3) }, 3, true, 0.35f));
    c.push_back (inst ("bass-808", "808 Bass", B, "bass-808", 24, 55, { pluckArt ("808", 0, 4.0, 0.1f, 3, 1) }, 3, true, 0.35f));

    c.push_back (inst ("tine-piano", "Electric Piano (Tine)", K, "tine-piano", 28, 103, { pluckArt ("Normal", 0, 6.0, 0.25f, 4, 1) }));
    c.push_back (inst ("reed-piano", "Electric Piano (Reed)", K, "reed-piano", 33, 96, { pluckArt ("Normal", 0, 5.0, 0.2f, 4, 1) }));
    c.push_back (inst ("clavinet", "Clavinet", K, "clavinet", 29, 89, { pluckArt ("Normal", 0, 2.5, 0.05f, 3, 2), pluckArt ("Muted", 1, 0.8, 0.03f, 3, 1) }));
    {
        // chorus 6.9 Hz; rotary slow: horn 0.8 Hz / drum 0.4 Hz (2.5 s); fast: 6.4 / 5.6 Hz (both repeat every 1.25 s)
        const double chorus = 1.0 / 6.9;
        auto o = inst ("tonewheel-organ", "Tonewheel Organ", K, "tonewheel-organ", 24, 96,
                       { heldArt ("Gospel", 0, 5.0, 0.04f, 1, chorus), heldArt ("Jazz Percussion", 1, 5.0, 0.04f, 1, chorus),
                         heldArt ("Soft Ballad", 2, 5.0, 0.04f, 1, chorus), heldArt ("Full", 3, 5.0, 0.04f, 1, chorus),
                         heldArt ("Rotary Slow", 4, 8.0, 0.04f, 1, 2.5), heldArt ("Rotary Fast", 5, 6.0, 0.04f, 1, 1.25) }, 3, false, 0.0f);
        c.push_back (o);
    }

    c.push_back (inst ("accordion", "Accordion", F, "accordion", 41, 93,
                       { heldArt ("Musette", 0, 5.0, 0.08f, 2), heldArt ("Bassoon", 1, 5.0, 0.08f, 2), heldArt ("Bright", 2, 5.0, 0.08f, 2) }));
    c.push_back (inst ("harmonium", "Harmonium", F, "harmonium", 36, 89, { heldArt ("Normal", 0, 5.0, 0.15f, 2) }));
    c.push_back (inst ("melodica", "Melodica", F, "melodica", 53, 89, { heldArt ("Normal", 0, 5.0, 0.06f, 2) }, 3, true, 0.4f));
    c.push_back (inst ("bagpipe", "Bagpipe Chanter", F, "bagpipe", 67, 81, { heldArt ("Chanter", 0, 5.0, 0.04f, 1) }, 2, true, 0.0f));
    c.push_back (inst ("bagpipe-drones", "Bagpipe Drones", F, "bagpipe-drones", 45, 57, { heldArt ("Drones", 0, 6.0, 0.4f, 1) }, 3, false, 0.0f));
    c.push_back (inst ("pan-flute", "Pan Flute", WW, "pan-flute", 60, 96, { heldArt ("Normal", 0, 5.0, 0.15f, 2) }, 3, true, 0.4f));
    c.push_back (inst ("tin-whistle", "Tin Whistle", WW, "tin-whistle", 74, 98, { heldArt ("Normal", 0, 5.0, 0.08f, 2) }, 3, true, 0.4f));
    c.push_back (inst ("steel-drum", "Steel Drum", W, "steel-drum", 55, 89, { pluckArt ("Mallet", 0, 3.0, 0.5f, 3, 2) }));
    c.push_back (inst ("jaw-harp", "Jaw Harp", W, "jaw-harp", 50, 67, { pluckArt ("Twang", 0, 2.5, 0.2f, 3, 1) }, 3, true));

    c.push_back (inst ("choir", "Choir", C, "choir", 40, 81, { heldArt ("Aah", 0, 6.0, 0.6f, 2), heldArt ("Ooh", 1, 6.0, 0.6f, 2), heldArt ("Hum", 2, 6.0, 0.6f, 2) }));
    c.push_back (inst ("gospel-choir", "Gospel Choir", C, "gospel-choir", 40, 79, { heldArt ("Aah", 0, 6.0, 0.5f, 2), heldArt ("Ooh", 1, 6.0, 0.5f, 2) }));
    c.push_back (inst ("warm-pad", "Warm Pad", C, "warm-pad", 24, 96,
                       { heldArt ("Warm", 0, 6.0, 1.2f, 2), heldArt ("Strings", 1, 6.0, 1.0f, 2), heldArt ("Glass", 2, 6.0, 1.5f, 2) }, 4));

    c.push_back (inst ("synth-lead", "Synth Lead", S, "synth-lead", 36, 96,
                       { heldArt ("Saw", 0, 4.0, 0.12f, 2), heldArt ("Square", 1, 4.0, 0.12f, 2), heldArt ("Super Saw", 2, 4.0, 0.15f, 2) }, 3, true, 0.4f));
    c.push_back (inst ("synth-pluck", "Synth Pluck", S, "synth-pluck", 36, 96, { pluckArt ("Pluck", 0, 2.5, 0.15f, 3, 1) }));
    c.push_back (inst ("synth-bell", "Synth Bell", S, "synth-bell", 48, 100, { pluckArt ("Bell", 0, 5.0, 0.6f, 3, 1) }));

    {
        ModelInstrument k;
        k.id = "kit-trap"; k.name = "Trap Kit"; k.family = D; k.model = "kit-trap"; k.kind = "drums";
        k.lo = 35; k.hi = 81; k.velTrack = 0.3f;
        k.arts = { pluckArt ("Kit", 0, 2.6, 0.05f, 4, 1) };
        k.drumNotes = { 36, 37, 38, 39, 42, 44, 45, 46, 47, 49, 50, 51, 53, 54, 70 };
        c.push_back (k);
    }
    return c;
}

struct SeatDef { float x, depth, height; };
SeatDef seatFor (const juce::String& family)
{
    static const std::map<juce::String, SeatDef> seats {
        { "Keys", { -0.45f, 0.30f, 0.25f } }, { "Strings", { -0.55f, 0.35f, 0.20f } }, { "Brass", { 0.45f, 0.75f, 0.30f } },
        { "Woodwinds", { 0.05f, 0.55f, 0.25f } }, { "Guitars & Plucked", { 0.40f, 0.25f, 0.20f } }, { "Bass", { 0.15f, 0.40f, 0.15f } },
        { "Folk & Free Reeds", { -0.20f, 0.20f, 0.25f } }, { "Mallets & Bells", { 0.30f, 0.65f, 0.30f } },
        { "Percussion", { 0.25f, 0.80f, 0.25f } }, { "Drum Kits", { 0.0f, 0.70f, 0.20f } }, { "World", { -0.30f, 0.45f, 0.20f } },
        { "Synths", { 0.0f, 0.25f, 0.30f } }, { "Choir & Pads", { 0.0f, 0.85f, 0.45f } }, { "Sound Effects", { 0.0f, 0.50f, 0.30f } } };
    auto it = seats.find (family);
    return it == seats.end() ? SeatDef { 0.0f, 0.5f, 0.25f } : it->second;
}

/** Velocity layers: (render velocity 0..1, vlo, vhi). */
struct Layer { float vel; int vlo, vhi; };
std::vector<Layer> layers (int n)
{
    switch (n)
    {
        case 1: return { { 0.8f, 0, 127 } };
        case 2: return { { 0.5f, 0, 79 }, { 0.9f, 80, 127 } };
        case 3: return { { 0.35f, 0, 55 }, { 0.68f, 56, 95 }, { 0.95f, 96, 127 } };
        default: return { { 0.28f, 0, 45 }, { 0.52f, 46, 79 }, { 0.75f, 80, 105 }, { 0.97f, 106, 127 } };
    }
}

bool writeFlac (const juce::File& f, const juce::AudioBuffer<float>& b, juce::int64& bytes)
{
    f.deleteFile();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (kRate)
                                           .withNumChannels (b.getNumChannels()).withBitsPerSample (24).withQualityOptionIndex (5));
    if (w == nullptr) return false;
    const bool ok = w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    w.reset();
    bytes += f.getSize();
    return ok;
}

juce::var zoneVar (const juce::String& file, int frames, int root, int lo, int hi, int vlo, int vhi, double db,
                   int rr, int rrn, bool keytrack, int ls, int le)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("file", file);
    o->setProperty ("frames", frames);
    o->setProperty ("root", root);
    o->setProperty ("lo", lo);
    o->setProperty ("hi", hi);
    o->setProperty ("vlo", vlo);
    o->setProperty ("vhi", vhi);
    o->setProperty ("db", std::round (db * 100.0) / 100.0);
    o->setProperty ("tune", 0.0);
    o->setProperty ("rr", rr);
    o->setProperty ("rrn", rrn);
    o->setProperty ("keytrack", keytrack);
    if (ls >= 0)
    {
        juce::Array<juce::var> l { ls, le };
        o->setProperty ("loop", l);
    }
    return juce::var (o);
}
} // namespace

const std::vector<ModelInstrument>& catalog()
{
    static const std::vector<ModelInstrument> c = makeCatalog();
    return c;
}

const ModelInstrument* find (const juce::String& id)
{
    for (auto& m : catalog()) if (m.id == id) return &m;
    return nullptr;
}

juce::String folderOf (const ModelInstrument& m)
{
    return m.family.toLowerCase().replace (" & ", "-").replace (" ", "-") + "/" + m.id;
}

double rmsDb (const juce::AudioBuffer<float>& b, double sampleRate, double seconds)
{
    const int n = juce::jmin (b.getNumSamples(), (int) (seconds * sampleRate));
    if (n <= 0) return -120.0;
    double s = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i) { const double v = b.getSample (ch, i); s += v * v; }
    return 10.0 * std::log10 (s / (n * juce::jmax (1, b.getNumChannels())) + 1.0e-20);
}

void trimTail (juce::AudioBuffer<float>& b, double sampleRate)
{
    const float peak = b.getMagnitude (0, b.getNumSamples());
    if (peak <= 0.0f) { b.setSize (b.getNumChannels(), juce::jmin (b.getNumSamples(), 64), true); return; }
    const float thresh = peak * std::pow (10.0f, -70.0f / 20.0f);
    int last = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = b.getNumSamples() - 1; i > last; --i)
            if (std::abs (b.getSample (ch, i)) > thresh) { last = i; break; }
    const int fade = (int) (0.01 * sampleRate);
    const int len = juce::jmin (b.getNumSamples(), last + fade);
    b.setSize (b.getNumChannels(), juce::jmax (64, len), true);
    const int n = b.getNumSamples();
    const int fs = juce::jmax (0, n - fade);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        b.applyGainRamp (ch, fs, n - fs, 1.0f, 0.0f);
}

bool makeLoop (juce::AudioBuffer<float>& b, double sampleRate, double period, int& loopStart, int& loopEnd)
{
    const int n = b.getNumSamples();
    loopStart = loopEnd = -1;
    if (n < (int) (1.5 * sampleRate)) return false;
    const int ls = juce::jmax ((int) (n * 0.35), (int) (0.5 * sampleRate));
    int le = (int) (n * 0.88);
    if (period > 0.0)
    {
        const double p = period * sampleRate;
        const int whole = (int) std::floor ((le - ls) / p);
        if (whole < 1) return false;
        le = ls + (int) std::round (whole * p);
    }
    if (le - ls < (int) (0.6 * sampleRate)) return false;
    const int xf = juce::jmin ((int) (0.12 * sampleRate), (le - ls) / 3);
    // the end of the loop fades into the audio just before the loop start, so the jump is seamless
    juce::AudioBuffer<float> y (b.getNumChannels(), le);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        y.copyFrom (ch, 0, b, ch, 0, le);
        for (int i = 0; i < xf; ++i)
        {
            const double t = (double) i / (xf - 1);
            const float a = (float) std::sqrt (1.0 - t), c = (float) std::sqrt (t);
            y.setSample (ch, le - xf + i, b.getSample (ch, le - xf + i) * a + b.getSample (ch, ls - xf + i) * c);
        }
    }
    b = std::move (y);
    loopStart = ls;
    loopEnd = le;
    return true;
}

RenderResult renderInstrument (const ModelInstrument& m, const juce::File& libraryRoot, const std::atomic<bool>* cancel)
{
    RenderResult res;
    const auto render = renderer (m.model);
    if (! render) { res.message = m.id + ": unknown model " + m.model; return res; }
    const auto dir = libraryRoot.getChildFile (folderOf (m));
    dir.deleteRecursively();
    // parallel renders may race to make the same family folder: retry a few times
    for (int attempt = 0; attempt < 20 && ! dir.isDirectory(); ++attempt)
    {
        dir.getParentDirectory().createDirectory();
        dir.createDirectory();
        if (! dir.isDirectory()) juce::Thread::sleep (25);
    }
    if (! dir.isDirectory()) { res.message = m.id + ": cannot create " + dir.getFullPathName(); return res; }

    const bool drums = m.kind == "drums";
    juce::Array<juce::var> arts;
    std::vector<double> calib;    // levels of the zones used to level the instrument
    int count = 0;

    for (size_t ai = 0; ai < m.arts.size(); ++ai)
    {
        const auto& a = m.arts[ai];
        juce::Array<juce::var> zones;
        std::vector<int> roots;
        if (drums) roots = m.drumNotes;
        else for (int r = m.lo + 1; r <= m.hi + m.step - 1; r += m.step) roots.push_back (juce::jmin (r, m.hi));
        roots.erase (std::unique (roots.begin(), roots.end()), roots.end());
        const auto ls = layers (a.velLayers);
        const int mid = drums ? 0 : roots[roots.size() / 2];

        for (size_t ri = 0; ri < roots.size(); ++ri)
        {
            const int root = roots[ri];
            int lo, hi;
            if (drums) { lo = hi = root; }
            else
            {
                lo = ri == 0 ? m.lo : (roots[ri - 1] + root) / 2 + 1;
                hi = ri + 1 == roots.size() ? m.hi : (root + roots[ri + 1]) / 2;
            }
            for (auto& L : ls)
                for (int rr = 1; rr <= a.roundRobin; ++rr)
                {
                    if (cancel != nullptr && cancel->load()) { res.message = m.id + ": cancelled"; return res; }
                    NoteSpec ns;
                    ns.sampleRate = kRate;
                    ns.pitch = root;
                    ns.velocity = L.vel;
                    ns.seconds = a.seconds;
                    ns.variant = a.variant;
                    ns.seed = (juce::uint32) (root * 1009 + rr * 7919 + (int) ai * 104729 + L.vlo * 31 + 17);
                    auto buf = render (ns);
                    if (buf.getNumSamples() < 64 || buf.getNumChannels() != 2) { res.message = m.id + ": model returned no audio"; return res; }
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < buf.getNumSamples(); ++i)
                            if (! std::isfinite (buf.getSample (ch, i))) { res.message = m.id + ": model produced a bad sample"; return res; }
                    int lS = -1, lE = -1;
                    if (a.sustain) makeLoop (buf, kRate, a.loopPeriod, lS, lE);
                    else trimTail (buf, kRate);
                    // keep the file below full scale; the zone level puts the gain back
                    double db = 0.0;
                    const float peak = buf.getMagnitude (0, buf.getNumSamples());
                    if (peak > 0.89f) { const float g = 0.89f / peak; buf.applyGain (g); db = -20.0 * std::log10 (g); }
                    const double level = rmsDb (buf, kRate, drums ? 0.3 : 0.6) + db;
                    const bool loud = L.vlo <= 100 && 100 <= L.vhi;
                    if (ai == 0 && loud && (drums ? (root == 36 || root == 38) : std::abs (root - mid) <= 7)) calib.push_back (level);
                    ++count;
                    const auto name = "m" + juce::String (count).paddedLeft ('0', 4) + ".flac";
                    if (! writeFlac (dir.getChildFile (name), buf, res.bytes)) { res.message = m.id + ": cannot write " + name; return res; }
                    zones.add (zoneVar (name, buf.getNumSamples(), root, lo, hi, L.vlo, L.vhi, db, rr, a.roundRobin, ! drums, lS, lE));
                }
        }
        auto* ao = new juce::DynamicObject();
        ao->setProperty ("name", a.name);
        ao->setProperty ("release", a.release);
        ao->setProperty ("loop", a.sustain);
        ao->setProperty ("zones", zones);
        ao->setProperty ("releaseZones", juce::Array<juce::var>());
        arts.add (juce::var (ao));
    }

    double gain = 0.0;
    if (! calib.empty())
    {
        std::sort (calib.begin(), calib.end());
        const double target = drums ? -16.0 : -20.0;
        gain = juce::jlimit (-24.0, 24.0, target - calib[calib.size() / 2]);
        gain = std::round (gain * 100.0) / 100.0;
    }

    const auto seat = seatFor (m.family);
    auto* so = new juce::DynamicObject();
    so->setProperty ("x", seat.x);
    so->setProperty ("depth", seat.depth);
    so->setProperty ("height", seat.height);
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "hem-instrument");
    o->setProperty ("version", 1);
    o->setProperty ("id", m.id);
    o->setProperty ("name", m.name);
    o->setProperty ("family", m.family);
    o->setProperty ("kind", m.kind);
    o->setProperty ("source", "hem");
    o->setProperty ("licence", "Original (HEM)");
    o->setProperty ("credit", "Modeled by HEM Instrument Forge (original code, no recordings).");
    o->setProperty ("gainDb", gain);
    o->setProperty ("velTrack", m.velTrack);
    o->setProperty ("mono", m.mono);
    juce::Array<juce::var> range { drums ? 35 : m.lo, drums ? 81 : m.hi };
    o->setProperty ("range", range);
    o->setProperty ("seat", juce::var (so));
    o->setProperty ("sampleRate", kRate);
    o->setProperty ("articulations", arts);
    if (! dir.getChildFile ("instrument.json").replaceWithText (juce::JSON::toString (juce::var (o), false)))
    {
        res.message = m.id + ": cannot write instrument.json";
        return res;
    }
    res.ok = true;
    res.files = count;
    res.message = m.id + ": " + juce::String ((int) m.arts.size()) + " articulation(s), " + juce::String (count) + " samples, "
                + juce::String (res.bytes / 1.0e6, 1) + " MB, gain " + juce::String (gain, 1) + " dB";
    return res;
}
} // namespace hf::models
