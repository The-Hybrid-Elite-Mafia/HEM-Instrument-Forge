// Renders the main window (every tab) to PNG without a display, and runs the main flows end to
// end: write a band song, play it, edit notes, move a player, export.
// Usage: ScreenPreview <output folder> <library folder>

#include "UI/MainComponent.h"

using namespace hf;

namespace
{
    void shot (juce::Component& c, const juce::File& f)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("shot: %s\n", f.getFullPathName().toRawUTF8());
    }
    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
    void play (ForgeEngine& e, double seconds)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < (int) (seconds * 48000.0 / 512.0); ++i) { buf.clear(); e.processBlock (buf, midi); }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp/forge-screens"));
    const juce::File lib (argc > 2 ? juce::String (argv[2]) : juce::String());
    out.createDirectory();
    int failures = 0;
    auto check = [&failures] (bool ok, const char* what) { std::printf ("%s: %s\n", ok ? "OK  " : "FAIL", what); if (! ok) ++failures; };

    juce::PropertiesFile::Options o;
    o.applicationName = "ScreenPreview";
    o.folderName = "hf-preview";
    o.filenameSuffix = ".settings";
    juce::PropertiesFile settings (out.getChildFile ("preview.settings"), o);
    settings.setValue ("libraryFolder", lib.getFullPathName());
    settings.setValue ("band.genre", 1);
    settings.setValue ("band.length", 2);   // short song

    auto enginePtr = std::make_unique<ForgeEngine>();   // heap: large fixed arrays inside
    auto& engine = *enginePtr;
    engine.prepareToPlay (48000.0, 512);
    {
        auto mcPtr = std::make_unique<ui::MainComponent> (engine, nullptr, settings);
        auto& mc = *mcPtr;
        auto& st = mc.getState();
        mc.setSize (1600, 900);
        pump (300);
        check (st.getLibrary().isOpen(), "library opens");
        std::printf ("library: %d instruments\n", (int) st.getLibrary().getEntries().size());
        mc.showTab (ui::tabs::band); pump (200); shot (mc, out.getChildFile ("01-band-empty.png"));

        // BAND: write a song, let the instruments load, play a few seconds
        mc.getBandPage().write (false);
        for (int i = 0; i < 600 && (st.isLoading() || st.isBalancing()); ++i) pump (100);
        pump (300);
        check (! st.getSong().tracks.empty(), "band writes tracks");
        check (! st.isBalancing(), "balancing finishes");
        for (int t = 0; t < (int) st.getSong().tracks.size(); ++t)
            std::printf ("  balanced level track %d: %.1f dB\n", t + 1, engine.param (ForgeEngine::Ids::track (t, ForgeEngine::kLevel)));
        bool allLoaded = true;
        for (int t = 0; t < (int) st.getSong().tracks.size(); ++t)
        {
            const auto& tr = st.getSong().tracks[(size_t) t];
            std::printf ("  track %d: %s  [%s]  %d notes  %s\n", t + 1, tr.name.toRawUTF8(), tr.instrumentId.toRawUTF8(), (int) tr.notes.size(),
                         st.instrumentOf (t) != nullptr ? "loaded" : "NOT LOADED");
            allLoaded &= st.instrumentOf (t) != nullptr;
        }
        check (allLoaded, "every band track has its instrument loaded");
        engine.setPositionBeats (4.0);
        engine.play();
        play (engine, 4.0);
        const float peak = juce::jmax (engine.getMasterPeak (0), engine.getMasterPeak (1));
        std::printf ("master peak while playing: %.3f, voices %d\n", peak, engine.getActiveVoices());
        check (peak > 0.01f, "the band makes sound");
        pump (200);
        shot (mc, out.getChildFile ("02-band-written.png"));

        mc.showTab (ui::tabs::tracks); pump (300); shot (mc, out.getChildFile ("03-tracks-song.png"));
        mc.getTracksPage().showNotes (true); pump (300); shot (mc, out.getChildFile ("04-tracks-notes.png"));

        // undo / redo of an edit
        const auto before = (int) st.getSong().tracks[0].notes.size();
        auto s = st.getSong();
        s.tracks[0].notes.clear();
        st.changeSong (s, "Clear notes");
        check (st.getSong().tracks[0].notes.empty(), "edit applies");
        st.undoLast();
        check ((int) st.getSong().tracks[0].notes.size() == before, "undo brings the notes back");
        st.redoLast();
        st.undoLast();

        mc.showTab (ui::tabs::stage);
        engine.setParam (ForgeEngine::Ids::track (0, ForgeEngine::kX), -0.8f);
        pump (300); shot (mc, out.getChildFile ("05-stage.png"));
        mc.showTab (ui::tabs::instruments);
        mc.getInstrumentsPage().showInstrument (st.getSong().tracks[0].instrumentId);
        pump (300); shot (mc, out.getChildFile ("06-instruments.png"));

        // add a track and remove it again (parameters follow the tracks)
        const int n0 = (int) st.getSong().tracks.size();
        engine.setParam (ForgeEngine::Ids::track (n0 - 1, ForgeEngine::kLevel), -7.0f);
        st.addTrack (st.getLibrary().getEntries().front().id, {});
        check ((int) st.getSong().tracks.size() == n0 + 1, "add track");
        st.removeTrack (0);
        check ((int) st.getSong().tracks.size() == n0, "remove track");
        check (std::abs (engine.param (ForgeEngine::Ids::track (n0 - 2, ForgeEngine::kLevel)) + 7.0f) < 0.05f, "track settings move up with their track");
        st.undoLast(); st.undoLast();
        check ((int) st.getSong().tracks.size() == n0 && std::abs (engine.param (ForgeEngine::Ids::track (n0 - 1, ForgeEngine::kLevel)) + 7.0f) < 0.05f,
               "undo restores tracks and their settings");

        // save / reopen
        juce::String err;
        const auto sessionFile = out.getChildFile ("Preview Song.hemforge");
        check (st.saveSession (sessionFile, err), "save session");
        check (! st.isDirty(), "saved song is not dirty");
        st.newSong();
        check (st.getSong().tracks.empty(), "new song is empty");
        check (st.openSession (sessionFile, err), "open session");
        check ((int) st.getSong().tracks.size() == n0, "reopened song has its tracks");
        for (int i = 0; i < 300 && st.isLoading(); ++i) pump (100);

        // export (mastered mix + stems + MIDI) through the panel
        mc.showExport();
        pump (200); shot (mc, out.getChildFile ("07-export.png"));
        auto& ex = mc.getExportPanel();
        ex.start();
        for (int i = 0; i < 1200 && ex.isBusy(); ++i) pump (100);
        pump (200);
        std::printf ("export: %s\n", ex.lastResult.toRawUTF8());
        check (ex.lastResult.startsWith ("Done"), "export finishes");
        shot (mc, out.getChildFile ("08-export-done.png"));

        mc.setSize (1389, 781);
        mc.showTab (ui::tabs::tracks); pump (300); shot (mc, out.getChildFile ("09-tracks-laptop.png"));
        mc.setSize (1920, 1080);
        mc.showTab (ui::tabs::band); pump (300); shot (mc, out.getChildFile ("10-band-1080p.png"));
        engine.stop();
        play (engine, 0.2);
    }
    std::printf (failures == 0 ? "PREVIEW: all checks passed\n" : "PREVIEW: %d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
