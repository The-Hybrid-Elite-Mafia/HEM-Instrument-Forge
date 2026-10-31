#include "Library/UserSounds.h"
#include "Core/SincResampler.h"
#include "Library/InstrumentDef.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace hf::usersounds
{
namespace
{
const char* const kFamily = "My Sounds";

juce::String slug (const juce::String& s)
{
    juce::String out;
    for (auto c : s.toLowerCase())
        out << (juce::CharacterFunctions::isLetterOrDigit (c) && c < 128 ? juce::String::charToString (c) : juce::String ("-"));
    while (out.contains ("--")) out = out.replace ("--", "-");
    out = out.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return out.isEmpty() ? juce::String ("sound") : out.substring (0, 40);
}

struct Audio
{
    juce::AudioBuffer<float> data;    // 2 channels at kRate
    juce::String error;
    bool ok = false;
};

/** Reads any file JUCE can decode, converts to stereo at the library rate. */
Audio readAudio (const juce::File& f)
{
    Audio a;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { a.error = f.getFileName() + ": can't read this file (use WAV, AIFF or FLAC)"; return a; }
    if (r->sampleRate < 8000.0 || r->lengthInSamples <= 0) { a.error = f.getFileName() + ": the file is empty"; return a; }
    if ((double) r->lengthInSamples / r->sampleRate > kMaxSeconds)
    {
        a.error = f.getFileName() + ": longer than " + juce::String ((int) kMaxSeconds) + " seconds";
        return a;
    }
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> in (2, n);
    in.clear();
    if (! r->read (&in, 0, n, 0, true, true)) { a.error = f.getFileName() + ": can't decode"; return a; }
    if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, n);

    pg::SincResampler rs ((std::uint32_t) std::lround (r->sampleRate), (std::uint32_t) kRate);
    const int outN = (int) rs.outputLength ((std::uint64_t) n);
    a.data.setSize (2, outN);
    for (int ch = 0; ch < 2; ++ch)
        rs.process (in.getReadPointer (ch), (std::uint64_t) n, a.data.getWritePointer (ch));

    // Remove leading silence (below -70 dBFS) but keep 2 ms, so the hit starts when the key is pressed.
    const float floorGain = juce::Decibels::decibelsToGain (-70.0f);
    int first = 0;
    while (first < outN && std::abs (a.data.getSample (0, first)) < floorGain && std::abs (a.data.getSample (1, first)) < floorGain) ++first;
    if (first >= outN) { a.error = f.getFileName() + ": the file is silent"; return a; }
    first = juce::jmax (0, first - (int) (0.002 * kRate));
    if (first > 0)
    {
        juce::AudioBuffer<float> t (2, outN - first);
        for (int ch = 0; ch < 2; ++ch) t.copyFrom (ch, 0, a.data, ch, first, outN - first);
        a.data = std::move (t);
    }
    // Short fade-out of the last 5 ms so a file cut off mid-sound does not click.
    const int fade = juce::jmin (a.data.getNumSamples(), (int) (0.005 * kRate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < fade; ++i)
            a.data.getWritePointer (ch)[a.data.getNumSamples() - fade + i] *= 1.0f - (float) (i + 1) / (float) fade;
    a.ok = true;
    return a;
}

std::vector<float> mono (const juce::AudioBuffer<float>& b)
{
    std::vector<float> m ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
    return m;
}

bool writeWav24 (const juce::File& f, const juce::AudioBuffer<float>& b, juce::String& error)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) { error = "Can't write " + f.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (kRate).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) { error = "Can't create a WAV writer for " + f.getFileName(); return false; }
    const bool ok = w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    w.reset();
    if (! ok) error = "Writing " + f.getFileName() + " failed";
    return ok;
}

juce::var zoneVar (const juce::String& file, int frames, int root, int lo, int hi, double tune, int rr, int rrn, bool keytrack)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("file", file);
    o->setProperty ("frames", frames);
    o->setProperty ("root", root);
    o->setProperty ("lo", lo);
    o->setProperty ("hi", hi);
    o->setProperty ("tune", tune);
    o->setProperty ("rr", rr);
    o->setProperty ("rrn", rrn);
    o->setProperty ("keytrack", keytrack);
    return juce::var (o);
}

bool writeInstrumentJson (const juce::File& dir, const juce::String& id, const juce::String& name, const char* kind,
                          juce::Array<juce::var> zones, float release, int lo, int hi, bool pitched)
{
    auto* art = new juce::DynamicObject();
    art->setProperty ("name", pitched ? "Normal" : "Kit");
    art->setProperty ("release", release);
    art->setProperty ("loop", false);
    art->setProperty ("zones", zones);
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "hem-instrument");
    o->setProperty ("version", 1);
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("family", kFamily);
    o->setProperty ("kind", kind);
    o->setProperty ("source", "Imported by you");
    o->setProperty ("licence", "Your own recording");
    o->setProperty ("credit", name);
    o->setProperty ("gainDb", 0.0);
    o->setProperty ("velTrack", pitched ? 0.6 : 0.5);
    o->setProperty ("mono", false);
    o->setProperty ("range", juce::Array<juce::var> { lo, hi });
    auto* seat = new juce::DynamicObject();
    seat->setProperty ("x", 0.0);
    seat->setProperty ("depth", 0.5);
    seat->setProperty ("height", 0.25);
    o->setProperty ("seat", juce::var (seat));
    o->setProperty ("sampleRate", kRate);
    o->setProperty ("articulations", juce::Array<juce::var> { juce::var (art) });
    return dir.getChildFile ("instrument.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

/** A free id/folder name for `base` inside myFolder (adds -2, -3 ... rather than overwriting). */
juce::String freshId (const juce::File& myFolder, const juce::String& name)
{
    const auto base = "mine-" + slug (name);
    juce::String id = base;
    for (int i = 2; myFolder.getChildFile (id).exists(); ++i) id = base + "-" + juce::String (i);
    return id;
}

/** Common last step: applies the common gain so the loudest sample peaks at kPeakDb. */
float commonGain (const std::vector<Audio>& all)
{
    float peak = 0.0f;
    for (auto& a : all) peak = juce::jmax (peak, a.data.getMagnitude (0, a.data.getNumSamples()));
    return peak > 1.0e-6f ? juce::Decibels::decibelsToGain (kPeakDb) / peak : 1.0f;
}

void finishFolder (const juce::File& myFolder) { rebuildIndex (myFolder); }
} // namespace

juce::File defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hybrid Elite Mafia")
        .getChildFile ("Instrument Forge").getChildFile ("My Sounds");
}

double detectPitchHz (const float* x, int n, double rate, float& confidence)
{
    confidence = 0.0f;
    const int tauMin = juce::jmax (2, (int) (rate / kMaxHz));
    const int tauMax = (int) (rate / kMinHz);
    const int W = 4096;
    if (n < W + tauMax + 16) return 0.0;

    // Where the sound is loudest, then look after the attack at a few places and keep the most confident answer.
    int peakAt = 0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        if (std::abs (x[i]) > peak) { peak = std::abs (x[i]); peakAt = i; }
    if (peak < 1.0e-5f) return 0.0;

    std::vector<double> d ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    double bestHz = 0.0;
    float bestConf = 0.0f;
    for (double offSec : { 0.02, 0.08, 0.2, 0.4 })
    {
        const int start = peakAt + (int) (offSec * rate);
        if (start + W + tauMax + 2 > n) break;
        // energy gate: skip windows that have already died away
        double e = 0.0;
        for (int j = 0; j < W; ++j) e += (double) x[start + j] * x[start + j];
        if (std::sqrt (e / W) < 0.01 * peak) break;

        d[0] = 0.0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            double s = 0.0;
            for (int j = 0; j < W; ++j) { const double v = (double) x[start + j] - x[start + j + tau]; s += v * v; }
            d[(size_t) tau] = s;
        }
        cm[0] = 1.0;
        double run = 0.0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0;
        }
        // first dip below the threshold, followed down to its minimum
        int found = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (cm[(size_t) tau] < 0.15)
            {
                while (tau + 1 <= tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                found = tau;
                break;
            }
        if (found < 0) continue;
        double tauF = found;
        if (found > 1 && found < tauMax)
        {
            const double a = cm[(size_t) found - 1], b = cm[(size_t) found], c = cm[(size_t) found + 1];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1.0e-12) tauF = found + 0.5 * (a - c) / den;
        }
        const float conf = (float) juce::jlimit (0.0, 1.0, 1.0 - cm[(size_t) found]);
        if (conf > bestConf) { bestConf = conf; bestHz = rate / tauF; }
    }
    confidence = bestConf;
    return bestConf >= kMinConfidence ? bestHz : 0.0;
}

int drumNoteForName (const juce::String& fileName)
{
    const auto n = " " + fileName.upToLastOccurrenceOf (".", false, false).toLowerCase().replaceCharacters ("_-.()", "     ") + " ";
    auto has = [&n] (const char* w) { return n.contains (w); };
    if (has ("kick") || has (" bd ") || has ("bassdrum") || has ("bass drum")) return 36;
    if (has ("rimshot") || has ("side stick") || has ("sidestick") || has (" rim ")) return 37;
    if (has ("snare") || has (" sd ") || has ("snr")) return 38;
    if (has ("clap")) return 39;
    if (has ("hat") || has (" hh ") || has ("hihat"))
    {
        if (has ("open") || has (" oh ")) return 46;
        if (has ("pedal")) return 44;
        return 42;
    }
    if (has ("tom"))
    {
        if (has ("floor") || has ("low")) return 43;
        if (has ("high") || has (" hi ")) return 50;
        if (has ("mid")) return 47;
        return 45;
    }
    if (has ("crash")) return 49;
    if (has ("ride")) return 51;
    if (has ("cowbell")) return 56;
    if (has ("tamb")) return 54;
    return -1;
}

int parseNoteName (const juce::String& text)
{
    auto t = text.trim();
    if (t.isEmpty()) return -1;
    if (t.containsOnly ("0123456789"))
    {
        const int v = t.getIntValue();
        return v >= 0 && v <= 127 ? v : -1;
    }
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);
    if (letter < 'A' || letter > 'G') return -1;
    int semi = semis[letter - 'A'];
    int i = 1;
    if (t[i] == '#') { ++semi; ++i; }
    else if (t[i] == 'b' || t[i] == 'B') { --semi; ++i; }
    const auto oct = t.substring (i);
    if (oct.isEmpty() || ! oct.trimCharactersAtStart ("-").containsOnly ("0123456789") || oct.trimCharactersAtStart ("-").isEmpty()) return -1;
    const int midi = (oct.getIntValue() + 1) * 12 + semi;
    return midi >= 0 && midi <= 127 ? midi : -1;
}

Result importPitched (const juce::Array<juce::File>& wavs, const juce::String& name, int forcedNote, const juce::File& myFolder)
{
    Result res;
    if (wavs.isEmpty()) { res.error = "No files chosen"; return res; }
    if (name.trim().isEmpty()) { res.error = "Give the instrument a name"; return res; }
    if (forcedNote >= 0 && wavs.size() > 1) { res.error = "Setting one note for several files would stack them on the same key"; return res; }

    std::vector<Audio> audio;
    std::vector<int> roots;
    std::vector<double> tunes;
    for (auto& f : wavs)
    {
        auto a = readAudio (f);
        if (! a.ok) { res.error = a.error; return res; }
        int root;
        double tune = 0.0;
        if (forcedNote >= 0)
        {
            root = forcedNote;
            res.report << f.getFileName() << ": note " << noteName (root) << " (set by you)\n";
        }
        else
        {
            const auto m = mono (a.data);
            float conf = 0.0f;
            const double hz = detectPitchHz (m.data(), (int) m.size(), kRate, conf);
            if (hz <= 0.0)
            {
                res.error = f.getFileName() + ": can't find a steady pitch (confidence " + juce::String (conf, 2)
                          + "). Is it a drum, a chord or a noisy take? Pick the note yourself.";
                return res;
            }
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            root = (int) std::lround (midi);
            tune = -(midi - root) * 100.0;   // the correction that puts the recording on the note
            res.report << f.getFileName() << ": " << noteName (root) << ", " << juce::String (hz, 1) << " Hz, "
                       << juce::String ((int) std::lround (std::abs (tune))) << " cents " << (tune > 0 ? "flat" : "sharp")
                       << " (found with confidence " << juce::String (conf, 2) << ")\n";
        }
        if (root < 0 || root > 127) { res.error = f.getFileName() + ": the note is outside the keyboard"; return res; }
        audio.push_back (std::move (a));
        roots.push_back (root);
        tunes.push_back (tune);
    }

    const float g = commonGain (audio);
    myFolder.createDirectory();
    const auto id = freshId (myFolder, name);
    const auto dir = myFolder.getChildFile (id);
    if (! dir.createDirectory()) { res.error = "Can't create " + dir.getFullPathName(); return res; }

    // distinct roots, lowest first; each covers up to the halfway point to its neighbours
    std::map<int, std::vector<int>> byRoot;
    for (size_t i = 0; i < roots.size(); ++i) byRoot[roots[i]].push_back ((int) i);
    std::vector<int> distinct;
    for (auto& kv : byRoot) distinct.push_back (kv.first);

    juce::Array<juce::var> zones;
    int lowest = 127, highest = 0;
    for (size_t k = 0; k < distinct.size(); ++k)
    {
        const int root = distinct[k];
        const int lo = k == 0 ? juce::jmax (0, root - kSpreadDown) : (distinct[k - 1] + root) / 2 + 1;
        const int hi = k + 1 == distinct.size() ? juce::jmin (127, root + kSpreadUp) : (root + distinct[k + 1]) / 2;
        const auto& takes = byRoot[root];
        for (size_t t = 0; t < takes.size(); ++t)
        {
            auto& a = audio[(size_t) takes[t]];
            a.data.applyGain (g);
            const auto file = "n" + juce::String (root) + (takes.size() > 1 ? "_" + juce::String ((int) t + 1) : juce::String()) + ".wav";
            if (! writeWav24 (dir.getChildFile (file), a.data, res.error)) { dir.deleteRecursively(); return res; }
            zones.add (zoneVar (file, a.data.getNumSamples(), root, lo, hi, tunes[(size_t) takes[t]], (int) t + 1, (int) takes.size(), true));
        }
        lowest = juce::jmin (lowest, lo);
        highest = juce::jmax (highest, hi);
    }
    if (! writeInstrumentJson (dir, id, name.trim(), "pitched", zones, 0.35f, lowest, highest, true))
    {
        res.error = "Can't write instrument.json";
        dir.deleteRecursively();
        return res;
    }
    finishFolder (myFolder);
    res.ok = true;
    res.id = id;
    res.name = name.trim();
    res.samples = (int) audio.size();
    return res;
}

Result importKit (const juce::Array<juce::File>& wavs, const juce::String& name, const juce::File& myFolder)
{
    Result res;
    if (wavs.isEmpty()) { res.error = "No files chosen"; return res; }
    if (name.trim().isEmpty()) { res.error = "Give the kit a name"; return res; }

    std::vector<Audio> audio;
    std::vector<int> notes;
    std::vector<bool> named;
    for (auto& f : wavs)
    {
        auto a = readAudio (f);
        if (! a.ok) { res.error = a.error; return res; }
        audio.push_back (std::move (a));
        const int n = drumNoteForName (f.getFileName());
        notes.push_back (n);
        named.push_back (n >= 0);
    }
    // files whose name says nothing take the next free GM note from 36 up
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (notes[i] >= 0) continue;
        int n = 36;
        while (n < 128 && std::find (notes.begin(), notes.end(), n) != notes.end()) ++n;
        if (n > 127) { res.error = "Too many files for one kit"; return res; }
        notes[i] = n;
    }

    const float g = commonGain (audio);
    myFolder.createDirectory();
    const auto id = freshId (myFolder, name);
    const auto dir = myFolder.getChildFile (id);
    if (! dir.createDirectory()) { res.error = "Can't create " + dir.getFullPathName(); return res; }

    std::map<int, std::vector<int>> byNote;
    for (size_t i = 0; i < notes.size(); ++i) byNote[notes[i]].push_back ((int) i);
    juce::Array<juce::var> zones;
    int lo = 127, hi = 0;
    for (auto& [note, takes] : byNote)
        for (size_t t = 0; t < takes.size(); ++t)
        {
            auto& a = audio[(size_t) takes[t]];
            a.data.applyGain (g);
            const auto file = "gm" + juce::String (note) + (takes.size() > 1 ? "_" + juce::String ((int) t + 1) : juce::String()) + ".wav";
            if (! writeWav24 (dir.getChildFile (file), a.data, res.error)) { dir.deleteRecursively(); return res; }
            zones.add (zoneVar (file, a.data.getNumSamples(), note, note, note, 0.0, (int) t + 1, (int) takes.size(), false));
            const auto& src = wavs[takes[t]];
            res.report << src.getFileName() << ": key " << noteName (note) << " (GM " << note << ")" << (named[(size_t) takes[t]] ? "" : ", name unclear so I picked a free key") << "\n";
        }
    for (auto& kv : byNote) { lo = juce::jmin (lo, kv.first); hi = juce::jmax (hi, kv.first); }
    if (! writeInstrumentJson (dir, id, name.trim(), "drums", zones, 0.1f, lo, hi, false))
    {
        res.error = "Can't write instrument.json";
        dir.deleteRecursively();
        return res;
    }
    finishFolder (myFolder);
    res.ok = true;
    res.id = id;
    res.name = name.trim();
    res.samples = (int) audio.size();
    return res;
}

bool remove (const juce::String& id, const juce::File& myFolder)
{
    if (! id.startsWith ("mine-") || id.contains ("/") || id.contains ("\\") || id.contains ("..")) return false;
    const auto dir = myFolder.getChildFile (id);
    if (! dir.getChildFile ("instrument.json").existsAsFile()) return false;
    const bool ok = dir.deleteRecursively();
    rebuildIndex (myFolder);
    return ok;
}

void rebuildIndex (const juce::File& myFolder)
{
    juce::Array<juce::var> items;
    for (auto& dir : myFolder.findChildFiles (juce::File::findDirectories, false))
    {
        juce::String err;
        const auto json = juce::JSON::parse (dir.getChildFile ("instrument.json"));
        if (! json.isObject() || json["format"].toString() != "hem-instrument") continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", json["id"]);
        o->setProperty ("name", json["name"]);
        o->setProperty ("family", kFamily);
        o->setProperty ("kind", json["kind"]);
        o->setProperty ("folder", dir.getFileName());
        o->setProperty ("source", json["source"]);
        o->setProperty ("licence", json["licence"]);
        o->setProperty ("range", json["range"]);
        juce::Array<juce::var> arts;
        int zones = 0;
        if (auto* a = json["articulations"].getArray())
            for (auto& av : *a)
            {
                arts.add (av["name"]);
                if (auto* z = av["zones"].getArray()) zones += z->size();
            }
        o->setProperty ("articulations", arts);
        o->setProperty ("zones", zones);
        juce::int64 bytes = 0;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false)) bytes += f.getSize();
        o->setProperty ("bytes", bytes);
        items.add (juce::var (o));
    }
    auto* idx = new juce::DynamicObject();
    idx->setProperty ("format", "hem-instrument-library");
    idx->setProperty ("version", 1);
    idx->setProperty ("families", juce::Array<juce::var> { juce::var (kFamily) });
    idx->setProperty ("instruments", items);
    myFolder.getChildFile ("index.json").replaceWithText (juce::JSON::toString (juce::var (idx)));
}
} // namespace hf::usersounds
