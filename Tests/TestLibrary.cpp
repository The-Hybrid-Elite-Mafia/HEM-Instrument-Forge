#include "TestLibrary.h"
#include <cmath>

namespace hemtest
{
namespace
{
void writeFlac (const juce::File& f, const std::function<float (int)>& gen, int frames)
{
    juce::AudioBuffer<float> b (2, frames);
    for (int i = 0; i < frames; ++i)
    {
        const float v = gen (i);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    juce::FlacAudioFormat flac;
    auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (TestLibrary::kRate).withNumChannels (2).withBitsPerSample (24));
    jassert (w != nullptr);
    w->writeFromAudioSampleBuffer (b, 0, frames);
}

juce::var zone (const juce::String& file, int frames, int root, int lo, int hi, int vlo, int vhi, int rr = 1, int rrn = 1,
                bool keytrack = true, int loopStart = -1, int loopEnd = -1, int pedal = -1)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("file", file);
    o->setProperty ("frames", frames);
    o->setProperty ("root", root); o->setProperty ("lo", lo); o->setProperty ("hi", hi);
    o->setProperty ("vlo", vlo); o->setProperty ("vhi", vhi);
    o->setProperty ("db", 0.0); o->setProperty ("tune", 0.0);
    o->setProperty ("rr", rr); o->setProperty ("rrn", rrn);
    o->setProperty ("keytrack", keytrack);
    if (loopStart >= 0) o->setProperty ("loop", juce::Array<juce::var> { loopStart, loopEnd });
    if (pedal >= 0) o->setProperty ("pedal", pedal);
    return juce::var (o);
}

juce::var art (const juce::String& name, float release, bool loop, juce::Array<juce::var> zones)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("release", release);
    o->setProperty ("loop", loop);
    o->setProperty ("zones", zones);
    return juce::var (o);
}

void writeInstrument (const juce::File& dir, const juce::String& id, const juce::String& name, const juce::String& family,
                      const juce::String& kind, juce::Array<juce::var> arts, int lo, int hi, bool mono = false)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "hem-instrument");
    o->setProperty ("version", 1);
    o->setProperty ("id", id); o->setProperty ("name", name); o->setProperty ("family", family);
    o->setProperty ("kind", kind); o->setProperty ("source", "test"); o->setProperty ("licence", "CC0 1.0");
    o->setProperty ("credit", "test signal");
    o->setProperty ("gainDb", 0.0); o->setProperty ("velTrack", 0.0); o->setProperty ("mono", mono);
    o->setProperty ("range", juce::Array<juce::var> { lo, hi });
    auto* seat = new juce::DynamicObject();
    seat->setProperty ("x", 0.0); seat->setProperty ("depth", 0.0); seat->setProperty ("height", 0.25);
    o->setProperty ("seat", juce::var (seat));
    o->setProperty ("sampleRate", TestLibrary::kRate);
    o->setProperty ("articulations", arts);
    dir.getChildFile ("instrument.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}
} // namespace

float TestLibrary::sineSample (int root, int layer, int i)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * sineFreq (root) / kRate;
    if (layer == 0) return (float) (0.5 * std::sin (w * i));
    return (float) (0.5 * (0.8 * std::sin (w * i) + 0.2 * std::sin (2.0 * w * i)));
}

TestLibrary::TestLibrary()
{
    root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HEMForgeTestLibrary-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    root.createDirectory();
    const int noteFrames = (int) (3.0 * kRate), heldFrames = (int) (2.5 * kRate);

    // test-sine
    {
        auto dir = root.getChildFile ("keys/test-sine");
        dir.createDirectory();
        juce::Array<juce::var> zs, held, pedal;
        const int roots[3] { 48, 60, 72 };
        for (int ri = 0; ri < 3; ++ri)
        {
            const int r = roots[ri];
            const int lo = ri == 0 ? 36 : r - 5, hi = ri == 2 ? 84 : r + 6;
            for (int layer = 0; layer < 2; ++layer)
            {
                const auto f = "n" + juce::String (r) + "_v" + juce::String (layer) + ".flac";
                writeFlac (dir.getChildFile (f), [r, layer] (int i) { return sineSample (r, layer, i); }, noteFrames);
                zs.add (zone (f, noteFrames, r, lo, hi, layer == 0 ? 0 : 64, layer == 0 ? 63 : 127));
            }
            // held: whole number of cycles in the loop so the loop is seamless
            const double period = kRate / sineFreq (r);
            const int loopStart = (int) std::round (period * std::ceil (1.2 * kRate / period));
            const int cycles = (int) std::floor ((heldFrames - loopStart) / period);
            const int loopEnd = loopStart + (int) std::round (cycles * period);
            const auto hf = "h" + juce::String (r) + ".flac";
            writeFlac (dir.getChildFile (hf), [r] (int i) { return sineSample (r, 0, i); }, loopEnd);
            held.add (zone (hf, loopEnd, r, lo, hi, 0, 127, 1, 1, true, loopStart, loopEnd));
        }
        // pedal: two zones on key 60; pedal up = layer 0, pedal down = layer 1
        pedal.add (zone ("n60_v0.flac", noteFrames, 60, 54, 66, 0, 127, 1, 1, true, -1, -1, 0));
        pedal.add (zone ("n60_v1.flac", noteFrames, 60, 54, 66, 0, 127, 1, 1, true, -1, -1, 1));
        writeInstrument (dir, "test-sine", "Test Sine", "Keys", "pitched",
                         { art ("Normal", 0.1f, false, zs), art ("Short", 0.02f, false, zs), art ("Held", 0.2f, true, held),
                           art ("Pedal", 0.1f, false, pedal) }, 36, 84);
    }
    // test-rr
    {
        auto dir = root.getChildFile ("keys/test-rr");
        dir.createDirectory();
        juce::Array<juce::var> zs;
        for (int k = 1; k <= 3; ++k)
        {
            const auto f = "rr" + juce::String (k) + ".flac";
            const float amp = 0.2f * (float) k;
            writeFlac (dir.getChildFile (f), [amp] (int i) { return amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.6255653 * i / kRate); }, 20000);
            zs.add (zone (f, 20000, 60, 0, 127, 0, 127, k, 3));
        }
        writeInstrument (dir, "test-rr", "Test Round Robin", "Keys", "pitched", { art ("Normal", 0.05f, false, zs) }, 0, 127);
    }
    // test-perc
    {
        auto dir = root.getChildFile ("percussion/test-perc");
        dir.createDirectory();
        juce::Array<juce::var> zs;
        for (int k = 0; k < 2; ++k)
        {
            const auto f = "hit" + juce::String (k) + ".flac";
            const double fr = k == 0 ? 200.0 : 400.0;
            writeFlac (dir.getChildFile (f), [fr] (int i) { return (float) (0.6 * std::exp (-i / 4000.0) * std::sin (2.0 * juce::MathConstants<double>::pi * fr * i / kRate)); }, 12000);
            zs.add (zone (f, 12000, 60 + k, 60 + k, 60 + k, 0, 127, 1, 1, false));
        }
        writeInstrument (dir, "test-perc", "Test Percussion", "Percussion", "perc", { art ("Hits", 0.05f, false, zs) }, 60, 61);
    }
    // test-kit
    {
        auto dir = root.getChildFile ("drum-kits/test-kit");
        dir.createDirectory();
        juce::Array<juce::var> zs;
        for (int note : { 36, 38, 42, 46 })
        {
            const auto f = "gm" + juce::String (note) + ".flac";
            const double fr = 60.0 + note * 5.0;
            writeFlac (dir.getChildFile (f), [fr] (int i) { return (float) (0.5 * std::exp (-i / 9000.0) * std::sin (2.0 * juce::MathConstants<double>::pi * fr * i / kRate)); }, 30000);
            zs.add (zone (f, 30000, note, note, note, 0, 127, 1, 1, false));
        }
        writeInstrument (dir, "test-kit", "Test Kit", "Drum Kits", "drums", { art ("Kit", 0.1f, false, zs) }, 35, 81);
    }

    // index.json
    juce::Array<juce::var> items;
    for (auto [id, folderName, fam, kind] : { std::tuple<const char*, const char*, const char*, const char*> { "test-sine", "keys/test-sine", "Keys", "pitched" },
                                              { "test-rr", "keys/test-rr", "Keys", "pitched" },
                                              { "test-perc", "percussion/test-perc", "Percussion", "perc" },
                                              { "test-kit", "drum-kits/test-kit", "Drum Kits", "drums" } })
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id); o->setProperty ("name", id); o->setProperty ("family", fam); o->setProperty ("kind", kind);
        o->setProperty ("folder", folderName); o->setProperty ("source", "test"); o->setProperty ("licence", "CC0 1.0");
        o->setProperty ("range", juce::Array<juce::var> { 36, 84 });
        o->setProperty ("articulations", juce::Array<juce::var> { "Normal" });
        o->setProperty ("zones", 6); o->setProperty ("bytes", 1000);
        items.add (juce::var (o));
    }
    auto* idx = new juce::DynamicObject();
    idx->setProperty ("format", "hem-instrument-library");
    idx->setProperty ("version", 1);
    idx->setProperty ("families", juce::Array<juce::var> { "Keys", "Percussion", "Drum Kits" });
    idx->setProperty ("instruments", items);
    root.getChildFile ("index.json").replaceWithText (juce::JSON::toString (juce::var (idx)));
    juce::String err;
    lib.open (root, err);
    jassert (lib.isOpen());
}

TestLibrary::~TestLibrary() { root.deleteRecursively(); }

std::shared_ptr<const hf::InstrumentDef> TestLibrary::def (const juce::String& id) const
{
    const auto* e = lib.find (id);
    if (e == nullptr) return nullptr;
    juce::String err;
    return hf::InstrumentDef::load (lib.folderOf (*e), err);
}

std::shared_ptr<hf::LoadedInstrument> TestLibrary::load (const juce::String& id) const
{
    juce::String err;
    auto d = def (id);
    auto li = hf::LoadedInstrument::load (d, err);
    jassert (li != nullptr);
    return li;
}
} // namespace hemtest
