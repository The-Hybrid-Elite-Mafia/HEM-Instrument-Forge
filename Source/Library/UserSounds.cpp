#include "Library/UserSounds.h"
#include "Core/SincResampler.h"
#include "Core/Fft.h"
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
Audio readAudio (const juce::File& f, double maxSeconds = kMaxSeconds)
{
    Audio a;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { a.error = f.getFileName() + ": can't read this file (use WAV, AIFF or FLAC)"; return a; }
    if (r->sampleRate < 8000.0 || r->lengthInSamples <= 0) { a.error = f.getFileName() + ": the file is empty"; return a; }
    if ((double) r->lengthInSamples / r->sampleRate > maxSeconds)
    {
        a.error = f.getFileName() + ": longer than " + juce::String ((int) maxSeconds) + " seconds"
                + (maxSeconds <= kMaxSeconds ? ". Use the cut options (drum hits, notes or loops) for long files" : "");
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

juce::var zoneVar (const juce::String& file, int frames, int root, int lo, int hi, double tune, int rr, int rrn, bool keytrack, bool oneShot = false)
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
    if (oneShot) o->setProperty ("oneshot", true);
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

namespace
{
Result buildPitched (std::vector<Audio>& audio, const std::vector<int>& roots, const std::vector<double>& tunes,
                     const juce::String& name, const juce::File& myFolder, Result res)
{
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

/** labels[i] names piece i in the report; named[i] false = the key was a free one. */
Result buildKit (std::vector<Audio>& audio, const std::vector<int>& notes, const std::vector<juce::String>& labels,
                 const std::vector<bool>& named, const juce::String& name, const juce::File& myFolder, Result res)
{
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
            zones.add (zoneVar (file, a.data.getNumSamples(), note, note, note, 0.0, (int) t + 1, (int) takes.size(), false, true));
            res.report << labels[(size_t) takes[t]] << ": key " << noteName (note) << " (GM " << note << ")"
                       << (named[(size_t) takes[t]] ? "" : ", name unclear so I picked a free key") << "\n";
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
} // namespace

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
    return buildPitched (audio, roots, tunes, name, myFolder, std::move (res));
}

Result importKit (const juce::Array<juce::File>& wavs, const juce::String& name, const juce::File& myFolder)
{
    Result res;
    if (wavs.isEmpty()) { res.error = "No files chosen"; return res; }
    if (name.trim().isEmpty()) { res.error = "Give the kit a name"; return res; }

    std::vector<Audio> audio;
    std::vector<int> notes;
    std::vector<bool> named;
    std::vector<juce::String> labels;
    for (auto& f : wavs)
    {
        auto a = readAudio (f);
        if (! a.ok) { res.error = a.error; return res; }
        audio.push_back (std::move (a));
        const int n = drumNoteForName (f.getFileName());
        notes.push_back (n);
        named.push_back (n >= 0);
        labels.push_back (f.getFileName());
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
    return buildKit (audio, notes, labels, named, name, myFolder, std::move (res));
}

namespace
{
constexpr int kHop = 256, kWin = 512;

/** Level in dB of each 512-frame window, every 256 frames. */
std::vector<float> levelDb (const float* x, int n)
{
    const int count = n > kWin ? (n - kWin) / kHop + 1 : 0;
    std::vector<float> env ((size_t) count);
    for (int k = 0; k < count; ++k)
    {
        double s = 0.0;
        const float* p = x + (size_t) k * kHop;
        for (int i = 0; i < kWin; ++i) s += (double) p[i] * p[i];
        env[(size_t) k] = (float) (10.0 * std::log10 (s / kWin + 1.0e-12));
    }
    return env;
}

Audio cut (const Audio& src, int start, int len)
{
    Audio a;
    start = juce::jlimit (0, juce::jmax (0, src.data.getNumSamples() - 1), start);
    len = juce::jmin (len, src.data.getNumSamples() - start);
    if (len <= 0) return a;
    a.data.setSize (2, len);
    for (int ch = 0; ch < 2; ++ch) a.data.copyFrom (ch, 0, src.data, ch, start, len);
    const int fin = juce::jmin (len / 2, (int) (0.001 * kRate));
    const int fout = juce::jmin (len / 2, (int) (0.004 * kRate));
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = a.data.getWritePointer (ch);
        for (int i = 0; i < fin; ++i) d[i] *= (float) (i + 1) / (float) fin;
        for (int i = 0; i < fout; ++i) d[len - fout + i] *= 1.0f - (float) (i + 1) / (float) fout;
    }
    a.ok = true;
    return a;
}

} // namespace

std::vector<int> findOnsets (const float* x, int n, double rate)
{
    std::vector<int> out;
    auto lv = levelDb (x, n);
    if (lv.size() < 8) return out;
    // three silent frames in front, so a hit that starts at the very first sample still shows a rise
    std::vector<float> env (3, -120.0f);
    env.insert (env.end(), lv.begin(), lv.end());
    const int m = (int) env.size();
    float top = -200.0f;
    for (auto v : env) top = juce::jmax (top, v);
    const int minGap = juce::jmax (1, (int) (kOnsetGapSeconds * rate / kHop));
    std::vector<float> rise ((size_t) m, 0.0f);
    for (int k = 3; k < m; ++k) rise[(size_t) k] = env[(size_t) k] - env[(size_t) k - 3];

    int lastHop = -100000;
    float lastRise = 0.0f;
    for (int k = 3; k < m; ++k)
    {
        const float r = rise[(size_t) k];
        if (r < (float) kOnsetRiseDb || env[(size_t) k] < top - (float) kOnsetFloorDb) continue;
        bool isMax = true;                       // the strongest rise within +-3 hops
        for (int j = juce::jmax (3, k - 3); j <= juce::jmin (m - 1, k + 3); ++j)
            if (rise[(size_t) j] > r) { isMax = false; break; }
        if (! isMax) continue;
        if (k - lastHop < minGap)
        {
            if (r > lastRise && ! out.empty()) { out.pop_back(); lastHop = -100000; }
            else continue;
        }
        // Where the sound really starts: the rise was seen between the start of the comparison window and the end of this one.
        // Take the first sample that is clearly above what was playing before (10% of the way from that level up to the new peak).
        const int w = k - 3;
        const int a = juce::jmax (0, (w - 3) * kHop);
        const int bEnd = juce::jmin (n, w * kHop + kWin);
        float before = 0.0f, pk = 0.0f;
        for (int i = a; i < juce::jmin (bEnd, a + 256); ++i) before = juce::jmax (before, std::abs (x[i]));
        for (int i = a; i < bEnd; ++i) pk = juce::jmax (pk, std::abs (x[i]));
        const float thr = before + 0.1f * juce::jmax (0.0f, pk - before);
        int pos = a;
        for (int i = a; i < bEnd; ++i)
            if (std::abs (x[i]) >= thr && std::abs (x[i]) > 0.0f) { pos = i; break; }
        const int back = (int) (0.001 * rate);
        out.push_back (juce::jmax (0, pos - back));
        lastHop = k;
        lastRise = r;
    }
    return out;
}

/** How well the hit times sit on an even grid: the best of the beat, the eighth-note and the sixteenth-note grids of period T (0..1).
    A steady run of eighths puts half its hits at the opposite phase of the beat grid, so the beat grid alone would call it random. */
double gridScore (const std::vector<int>& on, double rate, double T)
{
    double best = 0.0;
    for (int div : { 1, 2, 4 })
    {
        double cs = 0.0, sn = 0.0;
        for (int o : on)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * (o / rate) * div / T;
            cs += std::cos (ph); sn += std::sin (ph);
        }
        best = juce::jmax (best, std::sqrt (cs * cs + sn * sn) / (double) on.size());
    }
    return best;
}

double detectBpm (const float* x, int n, double rate, float& confidence)
{
    confidence = 0.0f;
    const auto env = levelDb (x, n);
    const int m = (int) env.size();
    if (m < 400) return 0.0;
    std::vector<double> flux ((size_t) m, 0.0);
    double mean = 0.0;
    for (int k = 1; k < m; ++k) { flux[(size_t) k] = juce::jmax (0.0f, env[(size_t) k] - env[(size_t) k - 1]); mean += flux[(size_t) k]; }
    mean /= m;
    for (auto& f : flux) f = juce::jmax (0.0, f - mean);
    const double hopsPerSec = rate / kHop;
    const int lagLo = (int) (hopsPerSec * 60.0 / 200.0), lagHi = juce::jmin (m / 2, (int) (hopsPerSec * 60.0 / 60.0));
    std::vector<double> ac ((size_t) lagHi + 2, 0.0);
    double zero = 0.0;
    for (int k = 0; k < m; ++k) zero += flux[(size_t) k] * flux[(size_t) k];
    if (zero <= 0.0) return 0.0;
    for (int lag = lagLo - 1; lag <= lagHi + 1; ++lag)
    {
        double s = 0.0;
        for (int k = lag; k < m; ++k) s += flux[(size_t) k] * flux[(size_t) k - (size_t) lag];
        ac[(size_t) lag] = s * (double) m / (double) (m - lag) / zero;
    }
    // best peak with the tempo between 75 and 160 BPM
    const int a = (int) (hopsPerSec * 60.0 / 160.0), b = (int) (hopsPerSec * 60.0 / 75.0);
    int best = -1;
    double bestV = 0.0;
    for (int lag = juce::jmax (a, lagLo); lag <= juce::jmin (b, lagHi); ++lag)
        if (ac[(size_t) lag] > ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1] && ac[(size_t) lag] > bestV) { bestV = ac[(size_t) lag]; best = lag; }
    if (best < 0) return 0.0;
    double lagF = best;
    const double y0 = ac[(size_t) best - 1], y1 = ac[(size_t) best], y2 = ac[(size_t) best + 1];
    if (std::abs (y0 - 2.0 * y1 + y2) > 1.0e-12) lagF += 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
    double period = lagF / hopsPerSec;    // seconds per beat, rough

    // refine against the onsets: the period whose grid the onsets sit closest to
    const auto on = findOnsets (x, n, rate);
    if (on.size() >= 8)
    {
        double bestScore = -1.0, bestPeriod = period;
        for (int step = -150; step <= 150; ++step)
        {
            const double T = period * (1.0 + step * 0.0001);
            const double score = gridScore (on, rate, T);
            if (score > bestScore) { bestScore = score; bestPeriod = T; }
        }
        period = bestPeriod;
        confidence = (float) bestScore;
    }
    else confidence = (float) juce::jlimit (0.0, 1.0, bestV);
    return 60.0 / period;
}

DrumClass classifyHit (const float* x, int n, double rate)
{
    const int N = 8192;
    pg::dsp::Fft fft;
    fft.prepare (N);
    std::vector<std::complex<float>> buf ((size_t) N);
    const int len = juce::jmin (n, (int) (0.1 * rate), N);
    for (int i = 0; i < N; ++i)
    {
        const float w = i < len ? 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) juce::jmax (1, len - 1)) : 0.0f;
        buf[(size_t) i] = { i < len ? x[i] * w : 0.0f, 0.0f };
    }
    fft.perform (buf.data(), false);
    double total = 0.0, low = 0.0, mid = 0.0, under250 = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double hz = k * rate / N;
        const double p = std::norm (buf[(size_t) k]);
        total += p;
        if (hz < 100.0) low += p;
        if (hz < 250.0) under250 += p;
        else if (hz >= 1500.0 && hz < 5000.0) mid += p;
    }
    if (total <= 0.0) return DrumClass::snare;
    if (low / total > kKickLowShare) return DrumClass::kick;
    // the hat test ignores the low end, so the leftover thump of the hit before does not count against it
    const double upper = total - under250;
    if (upper > 0.0 && mid / upper >= kHatMidShare) return DrumClass::hat;
    return DrumClass::snare;
}

Result importSliced (const juce::File& wav, const juce::String& name, Mode mode, const SliceOptions& opt, const juce::File& myFolder)
{
    Result res;
    if (name.trim().isEmpty()) { res.error = "Give the instrument a name"; return res; }
    auto src = readAudio (wav, kMaxSliceSeconds);
    if (! src.ok) { res.error = src.error; return res; }
    const auto m = mono (src.data);
    const int total = (int) m.size();
    const auto onsets = findOnsets (m.data(), total, kRate);
    if (onsets.empty()) { res.error = wav.getFileName() + ": no hits or notes stand out in this file"; return res; }
    res.report << wav.getFileName() << ": " << (int) onsets.size() << " starts found\n";

    auto endOf = [&] (size_t i, double maxSec)
    {
        const int next = i + 1 < onsets.size() ? onsets[i + 1] : total;
        return juce::jmin (next, onsets[i] + (int) (maxSec * kRate));
    };

    if (mode == Mode::sliceHits)
    {
        struct Hit { int idx; DrumClass cls; double score; float peak; };
        std::vector<Hit> hits;
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const int s = onsets[i], e = endOf (i, kMaxHitSeconds);
            if (e - s < (int) (0.08 * kRate)) continue;                 // too short to be a clean hit
            const double pre = i == 0 ? 1.0 : (s - onsets[i - 1]) / kRate;
            const double len = (e - s) / kRate;
            Hit h;
            h.idx = (int) i;
            h.cls = classifyHit (m.data() + s, e - s, kRate);
            h.score = juce::jmin (pre, 0.5) + juce::jmin (len, 0.5);   // alone before it, room to ring after it
            float pk = 0.0f;
            for (int j = s; j < e; ++j) pk = juce::jmax (pk, std::abs (m[(size_t) j]));
            h.peak = pk;
            hits.push_back (h);
        }
        std::vector<Audio> audio;
        std::vector<int> notes;
        std::vector<juce::String> labels;
        std::vector<bool> named;
        const struct { DrumClass c; int note; const char* nm; } classes[] = { { DrumClass::kick, 36, "kick" }, { DrumClass::snare, 38, "snare" }, { DrumClass::hat, 42, "hat" } };
        for (auto& cl : classes)
        {
            std::vector<Hit> mine;
            int count = 0;
            for (auto& h : hits) if (h.cls == cl.c) { mine.push_back (h); ++count; }
            std::sort (mine.begin(), mine.end(), [] (const Hit& a, const Hit& b) { return a.score > b.score || (! (b.score > a.score) && a.peak > b.peak); });
            int kept = 0;
            for (auto& h : mine)
            {
                if (kept >= kTakesPerDrum) break;
                auto piece = cut (src, onsets[(size_t) h.idx], endOf ((size_t) h.idx, kMaxHitSeconds) - onsets[(size_t) h.idx]);
                if (! piece.ok) continue;
                audio.push_back (std::move (piece));
                notes.push_back (cl.note);
                labels.push_back (juce::String (cl.nm) + " at " + juce::String ((double) onsets[(size_t) h.idx] / kRate, 2) + " s");
                named.push_back (true);
                ++kept;
            }
            res.report << count << " " << cl.nm << " hits found, kept " << kept << "\n";
        }
        if (audio.empty()) { res.error = wav.getFileName() + ": no clean hits to keep"; return res; }
        return buildKit (audio, notes, labels, named, name, myFolder, std::move (res));
    }

    if (mode == Mode::sliceNotes)
    {
        struct Cand { int root; double tune; float conf; float level; int idx; };
        std::vector<Cand> cands;
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const int s = onsets[i], e = endOf (i, 3.0);
            if (e - s < 6000) continue;
            float conf = 0.0f;
            const double hz = detectPitchHz (m.data() + s, e - s, kRate, conf);
            if (hz <= 0.0) continue;
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int root = (int) std::lround (midi);
            if (root < 0 || root > 127) continue;
            float pk = 0.0f;
            for (int j = s; j < e; ++j) pk = juce::jmax (pk, std::abs (m[(size_t) j]));
            cands.push_back ({ root, -(midi - root) * 100.0, conf, pk, (int) i });
        }
        res.report << (int) cands.size() << " of them have a steady pitch\n";
        if (cands.empty())
        {
            res.error = wav.getFileName() + ": no single notes with a steady pitch found. This looks like chords or a full band. "
                        "Try cutting it into loops, or record single notes.";
            return res;
        }
        std::map<int, Cand> bestPerRoot;
        for (auto& c : cands)
        {
            auto it = bestPerRoot.find (c.root);
            if (it == bestPerRoot.end() || c.conf > it->second.conf + 0.02f || (std::abs (c.conf - it->second.conf) <= 0.02f && c.level > it->second.level))
                bestPerRoot[c.root] = c;
        }
        std::vector<Cand> chosen;
        for (auto& kv : bestPerRoot) chosen.push_back (kv.second);
        // too many: keep notes spread evenly from the lowest to the highest, so the keyboard has no big holes
        const int keep = juce::jmax (1, opt.maxSlices);
        if ((int) chosen.size() > keep)
        {
            std::vector<Cand> spread;
            for (int q = 0; q < keep; ++q)
                spread.push_back (chosen[(size_t) (keep == 1 ? 0 : (long long) q * ((int) chosen.size() - 1) / (keep - 1))]);
            chosen = std::move (spread);
        }
        std::vector<Audio> audio;
        std::vector<int> roots;
        std::vector<double> tunes;
        for (auto& c : chosen)
        {
            auto piece = cut (src, onsets[(size_t) c.idx], endOf ((size_t) c.idx, 3.0) - onsets[(size_t) c.idx]);
            if (! piece.ok) continue;
            audio.push_back (std::move (piece));
            roots.push_back (c.root);
            tunes.push_back (c.tune);
            res.report << noteName (c.root) << " at " << juce::String ((double) onsets[(size_t) c.idx] / kRate, 2) << " s (confidence "
                       << juce::String (c.conf, 2) << ")\n";
        }
        if (audio.empty()) { res.error = "Nothing left to keep"; return res; }
        return buildPitched (audio, roots, tunes, name, myFolder, std::move (res));
    }

    // loops
    float conf = 0.0f;
    double bpm = opt.bpm > 0.0 ? opt.bpm : detectBpm (m.data(), total, kRate, conf);
    if (bpm <= 0.0) { res.error = wav.getFileName() + ": can't find the tempo. Type the BPM yourself."; return res; }
    {
        // snap a typed tempo to the onsets too (a rounded 120 may really be 119.7)
        double bestScore = -1.0, bestT = 60.0 / bpm;
        const double T0 = 60.0 / bpm;
        if (opt.bpm > 0.0 && onsets.size() >= 8)
            for (int step = -150; step <= 150; ++step)
            {
                const double T = T0 * (1.0 + step * 0.0001);
                const double sc = gridScore (onsets, kRate, T);
                if (sc > bestScore) { bestScore = sc; bestT = T; }
            }
        bpm = 60.0 / bestT;
    }
    const int bars = opt.bars == 2 || opt.bars == 4 ? opt.bars : 1;
    const double loopSec = bars * 4.0 * 60.0 / bpm;
    const int loopLen = (int) std::lround (loopSec * kRate);
    // the first loop starts at the first sound in the file (assumed to be the first bar's downbeat)
    const double startSec = onsets[0] / kRate;
    res.report << "Tempo " << juce::String (bpm, 1) << " BPM" << (opt.bpm > 0.0 ? " (from you, snapped to the playing)" : " (found; confidence " + juce::String (conf, 2) + ")")
               << ", " << bars << " bar loops of " << juce::String (loopSec, 2) << " s\n";
    if (opt.bpm <= 0.0 && conf < 0.3f)
        res.report << "The tempo is not certain (confidence " << juce::String (conf, 2) << "). If you know the BPM, type it in and cut again for exact loops.\n";
    std::vector<Audio> audio;
    std::vector<int> notes;
    std::vector<juce::String> labels;
    std::vector<bool> named;
    const int firstStart = (int) std::lround (startSec * kRate);
    for (int k = 0; k < opt.maxSlices; ++k)
    {
        const int s = firstStart + k * loopLen;
        if (s + loopLen > total) break;
        auto piece = cut (src, s, loopLen);
        if (! piece.ok) break;
        audio.push_back (std::move (piece));
        notes.push_back (36 + k);
        labels.push_back ("loop " + juce::String (k + 1) + " from " + juce::String ((double) s / kRate, 2) + " s");
        named.push_back (true);
    }
    if (audio.empty()) { res.error = wav.getFileName() + ": shorter than one loop at " + juce::String (bpm, 1) + " BPM"; return res; }
    return buildKit (audio, notes, labels, named, name, myFolder, std::move (res));
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
