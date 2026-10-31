#pragma once

#include "Engine/ForgeEngine.h"
#include "Library/Library.h"
#include "Sampler/LoadedInstrument.h"
#include "Session/Song.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>

namespace hf::ui
{
/**
    Everything the pages share: the engine, the instrument library, the song (with undo), which
    track is selected, and the instruments loaded for the tracks.

    Message thread only. Instruments are decoded on a worker pool and handed to the engine with
    ForgeEngine::setTrackInstrument (an atomic swap, the audio thread never waits).
*/
class AppState final : private juce::Timer
{
public:
    AppState (ForgeEngine&, juce::AudioDeviceManager*, juce::PropertiesFile&);
    ~AppState() override;

    ForgeEngine& engine;
    juce::AudioDeviceManager* devices;   // null in previews / tests
    juce::PropertiesFile& settings;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void songChanged() {}
        virtual void selectionChanged() {}
        virtual void libraryChanged() {}
        virtual void instrumentsChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // ---- library -------------------------------------------------------------------
    /** Looks in the usual places (and the folder chosen in settings). */
    bool openLibrary();
    bool openLibraryAt (const juce::File&);
    const Library& getLibrary() const noexcept { return library; }

    // ---- My Sounds (the user's own WAV recordings as instruments) ----------------------
    /** Reads the files, builds the instrument on a worker thread, then lists it. `done(ok, text)` runs on the
        message thread: `text` is the instrument id on success, else the reason. */
    void importMySounds (const juce::Array<juce::File>& files, const juce::String& name, bool drumKit, int forcedNote,
                         std::function<void (bool, const juce::String&, const juce::String&)> done);
    /** Deletes one of the user's own instruments. False + reason if a track still uses it or it is not theirs. */
    bool removeMySound (const juce::String& id, juce::String& reason);
    bool isMySound (const juce::String& id) const;
    juce::String getLibraryError() const { return libraryError; }

    // ---- song ------------------------------------------------------------------------
    const Song& getSong() const noexcept { return song; }
    /** Replaces the song as one undo step (`what` names it: "Draw notes", "Write the band"). */
    void changeSong (const Song& newSong, const juce::String& what);
    /** Like changeSong, also restoring the parameter state on undo (adding / removing tracks). */
    void changeSongAndParameters (const Song& newSong, const juce::ValueTree& newParams, const juce::String& what);
    /** Live preview while dragging (no undo step); commitPreview() makes the whole drag one step. */
    void previewSong (const Song& s) { setSongInternal (s); }
    void commitPreview (const Song& before, const juce::String& what);
    juce::UndoManager& getUndo() noexcept { return undo; }
    bool undoLast();
    bool redoLast();

    int addTrack (const juce::String& instrumentId, const juce::String& name);
    void removeTrack (int track);
    void duplicateTrack (int track);
    /** Gives a track a new instrument (and its seat / mono defaults). */
    void setTrackInstrument (int track, const juce::String& instrumentId);
    void renameTrack (int track, const juce::String& name);
    void setBpm (double bpm);

    int getSelectedTrack() const noexcept { return selected; }
    void selectTrack (int t);

    /** Marks tracks whose seat / mono / legato should come from their instrument when it loads. */
    void wantDefaults (int track) { defaultsWanted.insert (track); }

    // ---- instruments -------------------------------------------------------------
    bool isLoading() const noexcept { return ! inFlight.empty(); }
    juce::String loadingText() const;
    std::shared_ptr<LoadedInstrument> instrumentOf (int track) const;
    /** Loads (if needed) and returns an instrument for auditioning; null while it loads. */
    std::shared_ptr<LoadedInstrument> cachedInstrument (const juce::String& id);
    void requestInstrument (const juce::String& id);
    juce::String instrumentName (const juce::String& id) const;
    bool isDrumKit (const juce::String& id) const;

    /** Sets every track's level so the parts sit at the band's balance (measured by rendering each
        part offline, on a background thread, once the instruments are loaded). */
    void balanceTracks();
    bool isBalancing() const noexcept { return balanceJob != nullptr || balancePending; }

    // ---- parameters ------------------------------------------------------------------
    float param (const juce::String& id) const { return engine.param (id); }
    void setParam (const juce::String& id, float v) { engine.setParam (id, v); }
    juce::String trackParam (int t, const char* what) const { return ForgeEngine::Ids::track (t, what); }

    // ---- session ------------------------------------------------------------------------
    void newSong();
    bool openSession (const juce::File&, juce::String& error);
    bool saveSession (const juce::File&, juce::String& error);
    juce::File getSessionFile() const { return sessionFile; }
    juce::String songName() const;
    bool isDirty() const;
    void markSaved();
    bool importMidi (const juce::File&, juce::String& error);

    static juce::File songsFolder();

    std::function<void (const juce::String&)> status;   // set by the main window
    float uiScale = 1.0f;                                // window width / 1600 (set by the main window)

private:
    class SongAction;
    class BalanceJob;
    void reloadLibrary();
    void startBalance();
    void balanceFinished (std::vector<float> levels);
    void timerCallback() override;
    void setSongInternal (const Song&, bool republish = true);
    void ensureInstruments();
    void startLoad (const juce::String& id);
    void shiftTrackParameters (int fromTrack, int direction);
    void spreadSeat (int track);
    juce::String stateText() const;
    void notifySong();

    Library library;
    juce::String libraryError;
    Song song;
    juce::UndoManager undo { 0, 200 };
    int selected = 0;
    juce::File sessionFile;
    juce::String savedText;

    std::map<juce::String, std::shared_ptr<LoadedInstrument>> cache;
    std::set<juce::String> inFlight;
    std::map<juce::String, juce::String> failed;   // id -> error (not retried until the library changes)
    std::set<int> defaultsWanted;
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (2).withThreadName ("Instrument loader").withThreadStackSizeBytes (4 * 1024 * 1024) };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::ListenerList<Listener> listeners;
    int tick = 0;
    bool balancePending = false;
    std::unique_ptr<juce::Thread> balanceJob;

    JUCE_DECLARE_NON_COPYABLE (AppState)
};
} // namespace hf::ui
