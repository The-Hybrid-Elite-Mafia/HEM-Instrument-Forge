#include "Session/Song.h"
#include <algorithm>
#include <cmath>

namespace hf
{
void Track::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                      { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
}

double Song::lengthBeats() const
{
    double end = 0.0;
    for (auto& t : tracks)
        for (auto& n : t.notes) end = juce::jmax (end, n.end());
    const double bar = (double) juce::jmax (1, beatsPerBar);
    return juce::jmax (bar, std::ceil (end / bar - 1.0e-9) * bar);
}

void Song::renumber()
{
    nextId = 1;
    for (auto& t : tracks)
        for (auto& n : t.notes) n.id = nextId++;
}

namespace ids
{
    static const juce::Identifier song ("SONG"), track ("TRACK"), note ("N"), bpm ("bpm"), bpb ("beatsPerBar"),
        name ("name"), instrument ("instrument"), role ("role"), band ("band"), request ("request"),
        s ("s"), l ("l"), p ("p"), v ("v"), a ("a"), g ("g");
}

juce::ValueTree Song::toTree() const
{
    juce::ValueTree t (ids::song);
    t.setProperty (ids::bpm, bpm, nullptr);
    t.setProperty (ids::bpb, beatsPerBar, nullptr);
    t.setProperty (ids::band, bandInfo, nullptr);
    if (! bandRequest.isVoid()) t.setProperty (ids::request, juce::JSON::toString (bandRequest, true), nullptr);
    for (auto& tr : tracks)
    {
        juce::ValueTree tt (ids::track);
        tt.setProperty (ids::name, tr.name, nullptr);
        tt.setProperty (ids::instrument, tr.instrumentId, nullptr);
        tt.setProperty (ids::role, tr.bandRole, nullptr);
        for (auto& n : tr.notes)
        {
            juce::ValueTree nt (ids::note);
            nt.setProperty (ids::s, n.start, nullptr);
            nt.setProperty (ids::l, n.length, nullptr);
            nt.setProperty (ids::p, n.pitch, nullptr);
            nt.setProperty (ids::v, n.velocity, nullptr);
            if (n.articulation >= 0) nt.setProperty (ids::a, n.articulation, nullptr);
            if (n.glide != 0.0f) nt.setProperty (ids::g, n.glide, nullptr);
            tt.appendChild (nt, nullptr);
        }
        t.appendChild (tt, nullptr);
    }
    return t;
}

Song Song::fromTree (const juce::ValueTree& t)
{
    Song s;
    if (! t.hasType (ids::song)) return s;
    s.bpm = juce::jlimit (20.0, 300.0, (double) t.getProperty (ids::bpm, 100.0));
    s.beatsPerBar = juce::jlimit (1, 16, (int) t.getProperty (ids::bpb, 4));
    s.bandInfo = t.getProperty (ids::band).toString();
    if (t.hasProperty (ids::request)) s.bandRequest = juce::JSON::parse (t.getProperty (ids::request).toString());
    for (auto tt : t)
    {
        if (! tt.hasType (ids::track) || (int) s.tracks.size() >= kMaxTracks) continue;
        Track tr;
        tr.name = tt.getProperty (ids::name).toString();
        tr.instrumentId = tt.getProperty (ids::instrument).toString();
        tr.bandRole = (int) tt.getProperty (ids::role, -1);
        for (auto nt : tt)
        {
            if (! nt.hasType (ids::note)) continue;
            Note n;
            n.start = juce::jmax (0.0, (double) nt.getProperty (ids::s, 0.0));
            n.length = juce::jmax (1.0 / 64.0, (double) nt.getProperty (ids::l, 1.0));
            n.pitch = juce::jlimit (0, 127, (int) nt.getProperty (ids::p, 60));
            n.velocity = juce::jlimit (1, 127, (int) nt.getProperty (ids::v, 100));
            n.articulation = juce::jlimit (-1, 63, (int) nt.getProperty (ids::a, -1));
            n.glide = juce::jlimit (-24.0f, 24.0f, (float) (double) nt.getProperty (ids::g, 0.0));
            tr.notes.push_back (n);
        }
        tr.sortNotes();
        s.tracks.push_back (std::move (tr));
    }
    s.renumber();
    return s;
}

//==============================================================================
namespace session
{
bool save (const juce::File& file, const Song& song, const juce::ValueTree& parameters, juce::String& error)
{
    juce::ValueTree root ("HEMFORGE");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (song.toTree(), nullptr);
    if (parameters.isValid()) root.appendChild (parameters.createCopy(), nullptr);
    auto xml = root.createXml();
    if (xml == nullptr) { error = "Could not build the session"; return false; }
    // write to a temporary file first so a crash never leaves half a session
    const auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (! xml->writeTo (tmp)) { error = "Can't write " + tmp.getFullPathName(); return false; }
    if (! tmp.moveFileTo (file)) { tmp.deleteFile(); error = "Can't replace " + file.getFullPathName(); return false; }
    return true;
}

bool load (const juce::File& file, Song& song, juce::ValueTree& parameters, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) { error = file.getFileName() + " is not a session file"; return false; }
    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("HEMFORGE")) { error = file.getFileName() + " is not a HEM Instrument Forge session"; return false; }
    song = Song::fromTree (root.getChildWithName ("SONG"));
    parameters = juce::ValueTree();
    for (auto c : root)
        if (! c.hasType ("SONG")) { parameters = c.createCopy(); break; }
    return true;
}
} // namespace session

//==============================================================================
namespace midi
{
bool write (const Song& song, const juce::File& file, const std::function<bool (int)>& isDrums)
{
    constexpr int ppq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / song.bpm)), 0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (song.beatsPerBar, 4), 0);
    mf.addTrack (tempo);
    int channel = 1;
    for (int t = 0; t < (int) song.tracks.size(); ++t)
    {
        const auto& tr = song.tracks[(size_t) t];
        const bool drums = isDrums && isDrums (t);
        const int ch = drums ? 10 : channel;
        if (! drums) channel = channel == 9 ? 11 : (channel >= 16 ? 1 : channel + 1);
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, tr.name), 0);
        for (auto& n : tr.notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), n.start * ppq);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), n.end() * ppq);
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && mf.writeTo (os);
}

bool read (const juce::File& file, Song& song, juce::String& error)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) { error = file.getFileName() + " is not a MIDI file"; return false; }
    const int ppq = mf.getTimeFormat();
    if (ppq <= 0) { error = "SMPTE-timed MIDI files are not supported"; return false; }
    double bpm = 120.0;
    {
        juce::MidiMessageSequence tempos;
        mf.findAllTempoEvents (tempos);
        if (tempos.getNumEvents() > 0)
            bpm = 60.0 / juce::jmax (1.0e-6, tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote());
    }
    Song s;
    s.bpm = juce::jlimit (20.0, 300.0, bpm);
    for (int t = 0; t < mf.getNumTracks() && (int) s.tracks.size() < kMaxTracks; ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        Track tr;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* e = seq.getEventPointer (i);
            const auto& m = e->message;
            if (m.isTrackNameEvent()) tr.name = m.getTextFromTextMetaEvent();
            if (! m.isNoteOn()) continue;
            Note n;
            n.start = m.getTimeStamp() / ppq;
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / ppq : n.start + 0.25;
            n.length = juce::jmax (1.0 / 64.0, off - n.start);
            n.pitch = m.getNoteNumber();
            n.velocity = juce::jlimit (1, 127, (int) m.getVelocity());
            tr.notes.push_back (n);
        }
        if (tr.notes.empty()) continue;
        if (tr.name.isEmpty()) tr.name = "MIDI " + juce::String (s.tracks.size() + 1);
        tr.sortNotes();
        s.tracks.push_back (std::move (tr));
    }
    if (s.tracks.empty()) { error = file.getFileName() + " has no notes"; return false; }
    s.renumber();
    song = std::move (s);
    return true;
}
} // namespace midi
} // namespace hf
