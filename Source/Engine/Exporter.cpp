#include "Engine/Exporter.h"
#include "Master/ElitePlugins.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hf
{
namespace exporter
{
bool render (const Song& song, const juce::ValueTree& parameters, const Instruments& insts, double sr, double tail,
             int soloTrack, juce::AudioBuffer<float>& out, const std::atomic<bool>* cancel, const std::function<void (float)>& progress)
{
    ForgeEngine e (true);
    if (parameters.isValid() && parameters.hasType (e.getAPVTS().state.getType()))
        e.getAPVTS().replaceState (parameters.createCopy());
    for (int t = 0; t < kMaxTracks; ++t)
        e.setParam (ForgeEngine::Ids::track (t, ForgeEngine::kSolo), soloTrack < 0 ? 0.0f : (t == soloTrack ? 1.0f : 0.0f));
    e.setParam (ForgeEngine::Ids::click, 0.0f);
    e.setParam (ForgeEngine::Ids::headphones, 0.0f);   // exports are always speaker stereo
    constexpr int block = 1024;
    e.setPlayConfigDetails (0, 2, sr, block);
    e.prepareToPlay (sr, block);
    for (int t = 0; t < kMaxTracks; ++t) e.setTrackInstrument (t, insts[(size_t) t]);
    e.publishSong (song);
    e.setLoop (false, 0.0, 4.0);
    e.setPositionBeats (0.0);
    e.play();
    const double seconds = song.lengthBeats() * 60.0 / juce::jmax (20.0, song.bpm) + juce::jmax (0.0, tail);
    const int total = (int) std::ceil (seconds * sr);
    out.setSize (2, total, false, false, true);
    out.clear();
    for (int pos = 0; pos < total; pos += block)
    {
        if (cancel != nullptr && cancel->load()) return false;
        e.renderOffline (out, pos, juce::jmin (block, total - pos));
        if (progress && (pos / block) % 64 == 0) progress ((float) pos / (float) total);
    }
    e.releaseResources();
    return true;
}

juce::String master (juce::AudioBuffer<float>& mix, double sr)
{
    const int len = mix.getNumSamples();
    {
        pg::dsp::LoudnessMeter lm;
        lm.prepare (sr);
        lm.process (mix.getReadPointer (0), mix.getReadPointer (1), len);
        if (lm.getIntegrated() > -70.0f) mix.applyGain (juce::Decibels::decibelsToGain (-20.0f - lm.getIntegrated()));
    }
    pg::EliteMaster m;
    auto* input = m.getAPVTS().getParameter ("input");
    constexpr int block = 1024;
    juce::AudioBuffer<float> out;
    juce::MidiBuffer midi;
    int latency = 0;
    float integrated = -100.0f;
    for (int pass = 0; pass < 4; ++pass)
    {
        m.setPlayConfigDetails (2, 2, sr, block);
        m.prepareToPlay (sr, block);
        m.resetMeters();
        latency = m.getLatencySamples();
        out.setSize (2, len + latency, false, false, true);
        out.clear();
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, mix, ch, 0, len);
        for (int st = 0; st < out.getNumSamples(); st += block)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, st, juce::jmin (block, out.getNumSamples() - st));
            m.processBlock (view, midi);
        }
        integrated = m.getReadout (pg::EliteMaster::integrated);
        if (integrated <= -70.0f) break;
        const float now = input->convertFrom0to1 (input->getValue());
        const float next = juce::jlimit (0.0f, 18.0f, now + (pg::EliteMaster::targetLufs (0) - integrated));
        if (std::abs (next - now) < 0.1f || pass == 3) break;
        input->setValueNotifyingHost (input->convertTo0to1 (next));
    }
    for (int ch = 0; ch < 2; ++ch) mix.copyFrom (ch, 0, out, ch, latency, len);
    const float peak = mix.getMagnitude (0, len);
    return "mastered, " + juce::String (integrated, 1) + " LUFS, peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS";
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& b, double sr, int bits, juce::String& error)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (os == nullptr) { error = "Can't write " + file.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2)
                                               .withBitsPerSample (bits == 16 ? 16 : bits == 32 ? 32 : 24));
    if (writer == nullptr) { error = "Can't create a WAV writer for " + file.getFileName(); return false; }
    if (bits == 16)
    {
        // TPDF dither at the 16-bit step
        juce::AudioBuffer<float> d (b);
        juce::Random rnd (0x48454d);
        const float lsb = 1.0f / 32768.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* p = d.getWritePointer (ch);
            for (int i = 0; i < d.getNumSamples(); ++i) p[i] += (rnd.nextFloat() - rnd.nextFloat()) * lsb;
        }
        if (! writer->writeFromAudioSampleBuffer (d, 0, d.getNumSamples())) { error = "Writing " + file.getFileName() + " failed"; return false; }
        return true;
    }
    if (! writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) { error = "Writing " + file.getFileName() + " failed"; return false; }
    return true;
}

juce::File defaultFolder (const juce::String& songName)
{
    const auto when = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M");
    auto safe = juce::File::createLegalFileName (songName.isEmpty() ? juce::String ("Song") : songName);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hybrid Elite Mafia")
        .getChildFile ("Instrument Forge").getChildFile ("Exports").getChildFile (safe + " " + when);
}

bool run (const Song& song, const juce::ValueTree& params, const Instruments& insts, const ExportOptions& o,
          ExportResult& result, juce::String& error, const Progress& progress, const std::atomic<bool>* cancel)
{
    if (song.tracks.empty()) { error = "The song has no tracks"; return false; }
    if (! o.folder.createDirectory()) { error = "Can't create " + o.folder.getFullPathName(); return false; }

    // which tracks play: notes, an instrument, not muted
    std::vector<int> playing;
    for (int t = 0; t < (int) song.tracks.size() && t < kMaxTracks; ++t)
    {
        const auto c = params.getChildWithProperty ("id", ForgeEngine::Ids::track (t, ForgeEngine::kMute));
        const bool muted = c.isValid() && (double) c.getProperty ("value", 0.0) > 0.5;
        if (! song.tracks[(size_t) t].notes.empty() && insts[(size_t) t] != nullptr && ! muted) playing.push_back (t);
    }
    if (playing.empty()) { error = "No track has notes and an instrument"; return false; }

    juce::AudioBuffer<float> mix;
    const int steps = (o.stems ? (int) playing.size() : 1) + 1;
    int step = 0;
    auto report = [&] (float f, const juce::String& what) { if (progress) progress (((float) step + f) / (float) steps, what); };

    if (o.stems)
    {
        for (size_t i = 0; i < playing.size(); ++i)
        {
            const int t = playing[i];
            const auto& tr = song.tracks[(size_t) t];
            juce::AudioBuffer<float> stem;
            report (0.0f, "Rendering " + tr.name);
            if (! render (song, params, insts, o.sampleRate, o.tailSeconds, t, stem, cancel, [&] (float f) { report (f, "Rendering " + tr.name); }))
            {
                error = "Cancelled";
                return false;
            }
            const auto name = juce::String (i + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (tr.name.isEmpty() ? "Track" : tr.name) + ".wav";
            const auto f = o.folder.getChildFile (name);
            if (! writeWav (f, stem, o.sampleRate, o.bitDepth, error)) return false;
            result.files.add (f);
            if (o.mix != ExportOptions::Mix::none)
            {
                if (mix.getNumSamples() == 0) { mix.setSize (2, stem.getNumSamples()); mix.clear(); }
                for (int ch = 0; ch < 2; ++ch) mix.addFrom (ch, 0, stem, ch, 0, juce::jmin (mix.getNumSamples(), stem.getNumSamples()));
            }
            ++step;
        }
    }
    else if (o.mix != ExportOptions::Mix::none)
    {
        report (0.0f, "Rendering the mix");
        if (! render (song, params, insts, o.sampleRate, o.tailSeconds, -1, mix, cancel, [&] (float f) { report (f, "Rendering the mix"); }))
        {
            error = "Cancelled";
            return false;
        }
        ++step;
    }

    if (o.mix != ExportOptions::Mix::none && mix.getNumSamples() > 0)
    {
        report (0.0f, o.mix == ExportOptions::Mix::mastered ? "Mastering" : "Writing the mix");
        if (o.mix == ExportOptions::Mix::mastered)
            result.mixInfo = master (mix, o.sampleRate);
        else
        {
            const float peak = mix.getMagnitude (0, mix.getNumSamples()), limit = juce::Decibels::decibelsToGain (-1.0f);
            if (peak > limit) mix.applyGain (limit / peak);
            result.mixInfo = "clean, peak " + juce::String (juce::Decibels::gainToDecibels (juce::jmin (peak, limit)), 1) + " dBFS";
        }
        const auto f = o.folder.getChildFile (juce::File::createLegalFileName (o.baseName) + (o.mix == ExportOptions::Mix::mastered ? " - Mastered Mix.wav" : " - Mix.wav"));
        if (! writeWav (f, mix, o.sampleRate, o.bitDepth, error)) return false;
        result.files.add (f);
    }

    if (o.midi)
    {
        const auto f = o.folder.getChildFile (juce::File::createLegalFileName (o.baseName) + ".mid");
        midi::write (song, f, [&] (int t) { return insts[(size_t) t] != nullptr && insts[(size_t) t]->getDef().isDrums(); });
        result.files.add (f);
    }
    if (progress) progress (1.0f, "Done");
    return true;
}
} // namespace exporter
} // namespace hf
