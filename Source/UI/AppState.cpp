#include "UI/AppState.h"
#include "Engine/Exporter.h"
#include "Band/BandBridge.h"

namespace hf::ui
{
using E = ForgeEngine;

namespace
{
    const char* const kTrackParams[] { E::kLevel, E::kMute, E::kSolo, E::kX, E::kDepth, E::kHeight, E::kWidth, E::kRoomSend,
                                       E::kArt, E::kLegato, E::kMono, E::kHuman, E::kVelSens, E::kTranspose, E::kRelease };
}

//==============================================================================
class AppState::SongAction final : public juce::UndoableAction
{
public:
    SongAction (AppState& s, Song b, Song a, juce::ValueTree pb = {}, juce::ValueTree pa = {})
        : state (s), before (std::move (b)), after (std::move (a)), paramsBefore (std::move (pb)), paramsAfter (std::move (pa)) {}

    bool perform() override { apply (after, paramsAfter); return true; }
    bool undo() override { apply (before, paramsBefore); return true; }
    int getSizeInUnits() override { return 1 + (int) (after.tracks.size() * 8); }

private:
    void apply (const Song& s, const juce::ValueTree& p)
    {
        if (p.isValid()) state.engine.getAPVTS().replaceState (p.createCopy());
        state.setSongInternal (s);
    }
    AppState& state;
    Song before, after;
    juce::ValueTree paramsBefore, paramsAfter;
};

//==============================================================================
class AppState::BalanceJob final : public juce::Thread
{
public:
    BalanceJob (AppState& s, Song sg, juce::ValueTree p, exporter::Instruments i)
        : juce::Thread ("HEM balance", 8 * 1024 * 1024), state (s), song (std::move (sg)), params (std::move (p)), insts (std::move (i)) {}
    ~BalanceJob() override { cancel.store (true); stopThread (30000); }

    void run() override
    {
        // measure at most the first 48 bars (enough to hear every part) at 24 kHz
        const double maxBeats = 48.0 * song.beatsPerBar;
        for (auto& t : song.tracks)
            t.notes.erase (std::remove_if (t.notes.begin(), t.notes.end(), [maxBeats] (const Note& n) { return n.start >= maxBeats; }), t.notes.end());
        std::vector<float> levels (song.tracks.size(), std::numeric_limits<float>::quiet_NaN());
        for (int t = 0; t < (int) song.tracks.size() && ! cancel.load(); ++t)
        {
            if (insts[(size_t) t] == nullptr || song.tracks[(size_t) t].notes.empty()) continue;
            juce::AudioBuffer<float> out;
            if (! exporter::render (song, params, insts, 24000.0, 0.5, t, out, &cancel)) continue;
            // loudness while the part plays: 0.4 s windows within 30 dB of the loudest one
            const int win = 9600;
            std::vector<double> pw;
            for (int i = 0; i + win <= out.getNumSamples(); i += win)
            {
                double e = 0.0;
                for (int ch = 0; ch < 2; ++ch) { auto* d = out.getReadPointer (ch, i); for (int k = 0; k < win; ++k) e += (double) d[k] * d[k]; }
                pw.push_back (e / (2.0 * win));
            }
            if (pw.empty()) continue;
            const double top = *std::max_element (pw.begin(), pw.end());
            if (top < 1.0e-12) continue;
            double sum = 0.0; int n = 0;
            for (double v : pw) if (v > top * 1.0e-3) { sum += v; ++n; }
            const double rmsDb = 10.0 * std::log10 (sum / juce::jmax (1, n));
            const float current = (float) (double) params.getChildWithProperty ("id", ForgeEngine::Ids::track (t, ForgeEngine::kLevel)).getProperty ("value", 0.0);
            const float target = bandbridge::roleTargetRmsDb (song.tracks[(size_t) t].bandRole) + ForgeEngine::kMixHeadroomDb;
            levels[(size_t) t] = juce::jlimit (-30.0f, 12.0f, current + target - (float) rmsDb);
        }
        if (cancel.load()) return;
        auto* st = &state;
        auto flag = state.alive;
        juce::MessageManager::callAsync ([st, flag, levels] { if (flag->load()) st->balanceFinished (levels); });
    }
    std::atomic<bool> cancel { false };

private:
    AppState& state;
    Song song;
    juce::ValueTree params;
    exporter::Instruments insts;
};

void AppState::balanceTracks()
{
    if (song.tracks.empty()) return;
    balancePending = true;
    if (status) status ("Balancing the band...");
}

void AppState::startBalance()
{
    balancePending = false;
    if (balanceJob != nullptr) return;
    exporter::Instruments insts {};
    for (int t = 0; t < (int) song.tracks.size(); ++t) insts[(size_t) t] = engine.getTrackInstrument (t);
    // measure with every track at its current level, nothing muted or soloed
    auto params = engine.getAPVTS().copyState();
    for (int t = 0; t < kMaxTracks; ++t)
        for (auto* w : { E::kMute, E::kSolo })
        {
            auto c = params.getChildWithProperty ("id", E::Ids::track (t, w));
            if (c.isValid()) c.setProperty ("value", 0.0f, nullptr);
        }
    balanceJob = std::make_unique<BalanceJob> (*this, song, params, insts);
    balanceJob->startThread();
}

void AppState::balanceFinished (std::vector<float> levels)
{
    balanceJob.reset();
    const auto before = engine.getAPVTS().copyState();
    int changed = 0;
    for (int t = 0; t < (int) levels.size() && t < kMaxTracks; ++t)
        if (! std::isnan (levels[(size_t) t])) { engine.setParam (E::Ids::track (t, E::kLevel), levels[(size_t) t]); ++changed; }
    const auto after = engine.getAPVTS().copyState();
    engine.getAPVTS().replaceState (before);
    if (changed > 0) changeSongAndParameters (song, after, "Balance the band");
    if (status) status ("Balanced " + juce::String (changed) + " track(s): every part at its place in the mix (Undo puts the old levels back).");
}

//==============================================================================
AppState::AppState (ForgeEngine& e, juce::AudioDeviceManager* d, juce::PropertiesFile& s)
    : engine (e), devices (d), settings (s)
{
    song.bpm = 100.0;
    engine.publishSong (song);
    markSaved();
    startTimerHz (20);
}

AppState::~AppState()
{
    stopTimer();
    alive->store (false);
    balanceJob.reset();
    loader.removeAllJobs (true, 20000);
}

juce::File AppState::songsFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hybrid Elite Mafia")
                 .getChildFile ("Instrument Forge").getChildFile ("Songs");
    f.createDirectory();
    return f;
}

//==============================================================================
bool AppState::openLibrary()
{
    libraryError = {};
    for (auto& f : Library::searchFolders (settings.getValue ("libraryFolder")))
    {
        juce::String err;
        if (f != juce::File() && f.isDirectory() && library.open (f, err))
        {
            failed.clear();
            listeners.call ([] (Listener& l) { l.libraryChanged(); });
            ensureInstruments();
            return true;
        }
        if (err.isNotEmpty() && f.isDirectory() && libraryError.isEmpty()) libraryError = err;
    }
    if (libraryError.isEmpty())
        libraryError = "The HEM Instrument Library was not found. Install it, or choose its folder (File > Instrument Library Folder).";
    listeners.call ([] (Listener& l) { l.libraryChanged(); });
    return false;
}

bool AppState::openLibraryAt (const juce::File& folder)
{
    Library fresh;
    juce::String err;
    if (! fresh.open (folder, err)) { libraryError = err; return false; }
    settings.setValue ("libraryFolder", folder.getFullPathName());
    library = std::move (fresh);
    libraryError = {};
    failed.clear();
    cache.clear();   // a different library: instruments with the same id may differ
    for (int t = 0; t < kMaxTracks; ++t) engine.setTrackInstrument (t, nullptr);
    listeners.call ([] (Listener& l) { l.libraryChanged(); });
    ensureInstruments();
    return true;
}

//==============================================================================
void AppState::notifySong()
{
    listeners.call ([] (Listener& l) { l.songChanged(); });
}

void AppState::setSongInternal (const Song& s, bool republish)
{
    song = s;
    if (republish) engine.publishSong (song);
    if (selected >= (int) song.tracks.size()) selected = juce::jmax (0, (int) song.tracks.size() - 1);
    engine.setLiveTrack (selected);
    ensureInstruments();
    notifySong();
}

void AppState::changeSong (const Song& newSong, const juce::String& what)
{
    undo.beginNewTransaction (what);
    undo.perform (new SongAction (*this, song, newSong));
}

void AppState::changeSongAndParameters (const Song& newSong, const juce::ValueTree& newParams, const juce::String& what)
{
    undo.beginNewTransaction (what);
    undo.perform (new SongAction (*this, song, newSong, engine.getAPVTS().copyState(), newParams));
}

void AppState::commitPreview (const Song& before, const juce::String& what)
{
    undo.beginNewTransaction (what);
    undo.perform (new SongAction (*this, before, song));
}

bool AppState::undoLast()
{
    const auto what = undo.getUndoDescription();
    if (! undo.undo()) return false;
    if (status) status ("Undo: " + what);
    return true;
}

bool AppState::redoLast()
{
    const auto what = undo.getRedoDescription();
    if (! undo.redo()) return false;
    if (status) status ("Redo: " + what);
    return true;
}

void AppState::shiftTrackParameters (int from, int dir)
{
    // dir < 0: track `from` is removed (later tracks move up); dir > 0: a gap opens at `from`
    auto copyTrack = [this] (int src, int dst)
    {
        for (auto* w : kTrackParams) engine.setParam (E::Ids::track (dst, w), engine.param (E::Ids::track (src, w)));
    };
    auto resetTrack = [this] (int t)
    {
        for (auto* w : kTrackParams)
            if (auto* p = engine.getAPVTS().getParameter (E::Ids::track (t, w))) p->setValueNotifyingHost (p->getDefaultValue());
    };
    if (dir < 0)
    {
        for (int t = from; t < kMaxTracks - 1; ++t) copyTrack (t + 1, t);
        resetTrack (kMaxTracks - 1);
    }
    else
    {
        for (int t = kMaxTracks - 1; t > from; --t) copyTrack (t - 1, t);
        resetTrack (from);
    }
}

int AppState::addTrack (const juce::String& id, const juce::String& name)
{
    if ((int) song.tracks.size() >= kMaxTracks) { if (status) status ("All 32 tracks are in use."); return -1; }
    auto s = song;
    Track t;
    t.instrumentId = id;
    t.name = name.isNotEmpty() ? name : (id.isNotEmpty() ? instrumentName (id) : juce::String ("Track ") + juce::String (s.tracks.size() + 1));
    s.tracks.push_back (t);
    const int index = (int) s.tracks.size() - 1;
    const auto before = engine.getAPVTS().copyState();
    // the new track starts from default settings (a removed track may have left values there)
    for (auto* w : kTrackParams)
        if (auto* p = engine.getAPVTS().getParameter (E::Ids::track (index, w))) p->setValueNotifyingHost (p->getDefaultValue());
    const auto after = engine.getAPVTS().copyState();
    engine.getAPVTS().replaceState (before);
    wantDefaults (index);
    changeSongAndParameters (s, after, "Add track");
    selectTrack (index);
    return index;
}

void AppState::removeTrack (int t)
{
    if (t < 0 || t >= (int) song.tracks.size()) return;
    auto s = song;
    s.tracks.erase (s.tracks.begin() + t);
    const auto before = engine.getAPVTS().copyState();
    shiftTrackParameters (t, -1);
    const auto after = engine.getAPVTS().copyState();
    engine.getAPVTS().replaceState (before);
    changeSongAndParameters (s, after, "Remove track");
}

void AppState::duplicateTrack (int t)
{
    if (t < 0 || t >= (int) song.tracks.size() || (int) song.tracks.size() >= kMaxTracks) return;
    auto s = song;
    auto copy = s.tracks[(size_t) t];
    copy.name << " 2";
    s.tracks.insert (s.tracks.begin() + t + 1, copy);
    s.renumber();
    const auto before = engine.getAPVTS().copyState();
    shiftTrackParameters (t + 1, +1);
    for (auto* w : kTrackParams) engine.setParam (E::Ids::track (t + 1, w), engine.param (E::Ids::track (t, w)));
    const auto after = engine.getAPVTS().copyState();
    engine.getAPVTS().replaceState (before);
    changeSongAndParameters (s, after, "Duplicate track");
    selectTrack (t + 1);
}

void AppState::setTrackInstrument (int t, const juce::String& id)
{
    if (t < 0 || t >= (int) song.tracks.size()) return;
    auto s = song;
    auto& tr = s.tracks[(size_t) t];
    const auto oldName = instrumentName (tr.instrumentId);
    if (tr.name.isEmpty() || tr.name == oldName || tr.name.startsWith ("Track ")) tr.name = instrumentName (id);
    tr.instrumentId = id;
    wantDefaults (t);
    changeSong (s, "Change instrument");
}

void AppState::renameTrack (int t, const juce::String& name)
{
    if (t < 0 || t >= (int) song.tracks.size() || name.trim().isEmpty()) return;
    auto s = song;
    s.tracks[(size_t) t].name = name.trim();
    changeSong (s, "Rename track");
}

void AppState::setBpm (double bpm)
{
    bpm = juce::jlimit (40.0, 240.0, bpm);
    if (std::abs (bpm - song.bpm) < 0.01) return;
    auto s = song;
    s.bpm = bpm;
    changeSong (s, "Tempo");
}

void AppState::selectTrack (int t)
{
    t = juce::jlimit (0, juce::jmax (0, (int) song.tracks.size() - 1), t);
    if (t == selected) return;
    selected = t;
    engine.setLiveTrack (t);
    listeners.call ([] (Listener& l) { l.selectionChanged(); });
}

//==============================================================================
juce::String AppState::instrumentName (const juce::String& id) const
{
    if (auto* e = library.find (id)) return e->name;
    return id;
}

bool AppState::isDrumKit (const juce::String& id) const
{
    if (auto* e = library.find (id)) return e->kind == "drums";
    return false;
}

std::shared_ptr<LoadedInstrument> AppState::instrumentOf (int t) const
{
    return t >= 0 && t < kMaxTracks ? engine.getTrackInstrument (t) : nullptr;
}

std::shared_ptr<LoadedInstrument> AppState::cachedInstrument (const juce::String& id)
{
    auto it = cache.find (id);
    if (it != cache.end()) return it->second;
    requestInstrument (id);
    return nullptr;
}

void AppState::requestInstrument (const juce::String& id)
{
    if (id.isEmpty() || cache.count (id) > 0 || inFlight.count (id) > 0 || failed.count (id) > 0) return;
    if (library.find (id) == nullptr) return;
    startLoad (id);
}

juce::String AppState::loadingText() const
{
    if (inFlight.empty()) return {};
    juce::StringArray names;
    for (auto& id : inFlight) names.add (instrumentName (id));
    return "Loading " + names.joinIntoString (", ") + "...";
}

void AppState::startLoad (const juce::String& id)
{
    const auto* entry = library.find (id);
    if (entry == nullptr) return;
    inFlight.insert (id);
    const auto folder = library.folderOf (*entry);
    auto flag = alive;
    loader.addJob ([this, id, folder, flag]
    {
        juce::String err;
        std::shared_ptr<LoadedInstrument> inst;
        if (auto def = InstrumentDef::load (folder, err))
            inst = LoadedInstrument::load (def, err);
        juce::MessageManager::callAsync ([this, id, inst, err, flag]
        {
            if (! flag->load()) return;
            inFlight.erase (id);
            if (inst == nullptr)
            {
                failed[id] = err;
                if (status) status ("Could not load " + instrumentName (id) + ": " + err);
            }
            else cache[id] = inst;
            ensureInstruments();
            listeners.call ([] (Listener& l) { l.instrumentsChanged(); });
        });
    });
    listeners.call ([] (Listener& l) { l.instrumentsChanged(); });
}

void AppState::ensureInstruments()
{
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const juce::String want = t < (int) song.tracks.size() ? song.tracks[(size_t) t].instrumentId : juce::String();
        auto current = engine.getTrackInstrument (t);
        const juce::String have = current != nullptr ? current->getDef().id : juce::String();
        if (want == have)
        {
            if (current != nullptr && defaultsWanted.erase (t) > 0) { engine.applyInstrumentDefaults (t, current->getDef()); spreadSeat (t); }
            continue;
        }
        if (want.isEmpty()) { engine.setTrackInstrument (t, nullptr); continue; }
        auto it = cache.find (want);
        if (it != cache.end())
        {
            engine.setTrackInstrument (t, it->second);
            if (defaultsWanted.erase (t) > 0) { engine.applyInstrumentDefaults (t, it->second->getDef()); spreadSeat (t); }
        }
        else
        {
            if (current != nullptr) engine.setTrackInstrument (t, nullptr);   // silence the old sound until the new one is ready
            requestInstrument (want);
        }
    }
}

void AppState::spreadSeat (int t)
{
    // two players of the same family get the same default seat: move this one sideways until
    // nobody sits within reach (alternating right / left of the family's seat)
    const float x0 = engine.param (E::Ids::track (t, E::kX));
    const float d = engine.param (E::Ids::track (t, E::kDepth));
    auto taken = [this, t, d] (float x)
    {
        for (int u = 0; u < (int) song.tracks.size(); ++u)
            if (u != t && engine.getTrackInstrument (u) != nullptr
                && std::abs (engine.param (E::Ids::track (u, E::kX)) - x) < 0.25f && std::abs (engine.param (E::Ids::track (u, E::kDepth)) - d) < 0.15f)
                return true;
        return false;
    };
    for (int step = 0; step <= 8; ++step)
    {
        const float off = (float) ((step + 1) / 2) * 0.3f * (step % 2 == 1 ? 1.0f : -1.0f);
        const float x = juce::jlimit (-0.95f, 0.95f, x0 + off);
        if (! taken (x)) { engine.setParam (E::Ids::track (t, E::kX), x); return; }
    }
}

//==============================================================================
void AppState::timerCallback()
{
    ++tick;
    if (balancePending && ! isLoading() && balanceJob == nullptr) startBalance();
    std::vector<ForgeEngine::RecordedNote> rec;
    engine.collectRecorded (rec);
    if (! rec.empty())
    {
        auto s = song;
        for (auto& r : rec)
        {
            if (r.track < 0 || r.track >= (int) s.tracks.size()) continue;
            auto n = r.note;
            n.id = s.newId();
            s.tracks[(size_t) r.track].notes.push_back (n);
            s.tracks[(size_t) r.track].sortNotes();
        }
        changeSong (s, "Record");
        if (status) status ("Recorded " + juce::String ((int) rec.size()) + " note(s)");
    }
    if (tick % 10 == 0)
    {
        engine.collectGarbage();
        // forget cached instruments no track and no audition holds any more
        for (auto it = cache.begin(); it != cache.end();)
        {
            bool used = false;
            for (auto& tr : song.tracks) used |= tr.instrumentId == it->first;
            if (! used && it->second.use_count() == 1 && it->second->users.load() == 0) it = cache.erase (it);
            else ++it;
        }
    }
}

//==============================================================================
void AppState::newSong()
{
    engine.stop();
    engine.setPositionBeats (0.0);
    for (auto* p : engine.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) rp->setValueNotifyingHost (rp->getDefaultValue());
    Song s;
    s.bpm = 100.0;
    sessionFile = juce::File();
    undo.clearUndoHistory();
    selected = 0;
    setSongInternal (s);
    markSaved();
    listeners.call ([] (Listener& l) { l.selectionChanged(); });
}

bool AppState::openSession (const juce::File& f, juce::String& error)
{
    Song s;
    juce::ValueTree params;
    if (! session::load (f, s, params, error)) return false;
    engine.stop();
    engine.setPositionBeats (0.0);
    if (params.isValid()) engine.getAPVTS().replaceState (params);
    sessionFile = f;
    undo.clearUndoHistory();
    selected = 0;
    defaultsWanted.clear();
    setSongInternal (s);
    markSaved();
    listeners.call ([] (Listener& l) { l.selectionChanged(); });
    juce::StringArray missing;
    for (auto& t : song.tracks)
        if (t.instrumentId.isNotEmpty() && library.find (t.instrumentId) == nullptr) missing.addIfNotAlreadyThere (t.instrumentId);
    if (! missing.isEmpty() && status)
        status ("Opened. Not installed: " + missing.joinIntoString (", ") + " (pick another instrument for those tracks)");
    return true;
}

bool AppState::saveSession (const juce::File& f, juce::String& error)
{
    auto target = f.withFileExtension (session::kExtension);
    if (! session::save (target, song, engine.getAPVTS().copyState(), error)) return false;
    sessionFile = target;
    markSaved();
    return true;
}

juce::String AppState::songName() const
{
    return sessionFile == juce::File() ? juce::String ("Untitled Song") : sessionFile.getFileNameWithoutExtension();
}

juce::String AppState::stateText() const
{
    return song.toTree().toXmlString() + engine.getAPVTS().copyState().toXmlString();
}

bool AppState::isDirty() const { return stateText() != savedText; }
void AppState::markSaved() { savedText = stateText(); }

bool AppState::importMidi (const juce::File& f, juce::String& error)
{
    Song in;
    if (! midi::read (f, in, error)) return false;
    auto s = song;
    if (s.tracks.empty()) s.bpm = in.bpm;
    int added = 0;
    for (auto& t : in.tracks)
    {
        if ((int) s.tracks.size() >= kMaxTracks) break;
        // guess: channel-10 style drum names get the first drum kit, everything else the first piano
        juce::String pick;
        const bool drums = t.name.containsIgnoreCase ("drum") || t.name.containsIgnoreCase ("kit") || t.name.containsIgnoreCase ("perc");
        for (auto& e : library.getEntries())
            if ((drums && e.kind == "drums") || (! drums && e.kind == "pitched" && e.family == "Keys")) { pick = e.id; break; }
        t.instrumentId = pick;
        if (t.name.isEmpty()) t.name = f.getFileNameWithoutExtension() + " " + juce::String (added + 1);
        s.tracks.push_back (t);
        wantDefaults ((int) s.tracks.size() - 1);
        ++added;
    }
    if (added == 0) { error = f.getFileName() + " has no notes"; return false; }
    s.renumber();
    changeSong (s, "Import MIDI");
    return true;
}
} // namespace hf::ui
