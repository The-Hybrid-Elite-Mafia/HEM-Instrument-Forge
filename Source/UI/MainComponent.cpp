#include "UI/MainComponent.h"
#include "ForgeBinaryData.h"

namespace hf::ui
{
using E = ForgeEngine;

namespace
{
    constexpr float kW = 3840.0f, kH = 2160.0f;
    constexpr float kSocketX[6] { 1400.0f, 1607.0f, 1815.0f, 2024.0f, 2232.0f, 2441.0f };
    constexpr float kSocketY = 347.0f, kSocketR = 84.0f;
    constexpr float kTabX[4] { 108.0f, 1024.0f, 1940.0f, 2857.0f };
    constexpr float kTabY = 503.0f, kTabW = 873.0f, kTabH = 175.0f;

    enum MenuIds
    {
        mNew = 1, mOpen, mSave, mSaveAs, mImportMidi, mExport, mExportMidi, mAudio, mLibrary, mQuit, mClearRecent,
        mUndo = 20, mRedo, mAddTrack, mDupTrack, mRemoveTrack, mClearSolo, mClearMute,
        mTabBase = 40,
        mShortcuts = 90, mAbout, mSongsFolder, mLibraryFolder,
        mRecentBase = 100
    };

    void noKeyboardFocus (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<juce::Slider*> (child) != nullptr
                || dynamic_cast<juce::ComboBox*> (child) != nullptr)
                child->setWantsKeyboardFocus (false);
            noKeyboardFocus (*child);
        }
    }

    juce::String timeText (double s)
    {
        const int total = (int) s;
        return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2) + "." + juce::String ((int) (std::fmod (s, 1.0) * 10.0));
    }
}

MainComponent::MainComponent (ForgeEngine& e, juce::AudioDeviceManager* d, juce::PropertiesFile& s)
    : engine (e), devices (d), settings (s), lnf (colours::glow), state (e, d, s)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    logo = juce::ImageCache::getFromMemory (ForgeBinary::HEMIcon1024_png, ForgeBinary::HEMIcon1024_pngSize);
    state.status = [this] (const juce::String& t) { status (t); };
    state.addListener (this);

    addAndMakeVisible (menuBar);
    using I = TransportButton::Icon;
    const std::pair<I, const char*> tb[6] { { I::toStart, "Back to the start (Enter)" }, { I::play, "Play / pause (Space)" },
                                            { I::stop, "Stop and go back to where play started" },
                                            { I::loop, "Loop on / off (L). Drag on the ruler to set the loop." },
                                            { I::record, "Record what you play on the keyboard / MIDI keyboard into the selected track (R)" },
                                            { I::exportWav, "Export stems, mix and MIDI (Ctrl+E)" } };
    for (int i = 0; i < 6; ++i)
    {
        transport[(size_t) i] = std::make_unique<TransportButton> (tb[i].first, tb[i].second);
        addAndMakeVisible (*transport[(size_t) i]);
    }
    transport[0]->onClick = [this] { engine.setPositionBeats (engine.isLooping() ? engine.getLoopStart() : 0.0); };
    transport[1]->onClick = [this] { togglePlay(); };
    transport[2]->onClick = [this] { engine.stop(); engine.setRecording (false); engine.setPositionBeats (engine.isLooping() ? engine.getLoopStart() : 0.0); };
    transport[3]->onClick = [this] { toggleLoop(); };
    transport[4]->onClick = [this] { toggleRecord(); };
    transport[5]->onClick = [this] { showExport(); };
    transport[3]->ledColour = colours::glow;
    transport[4]->ledColour = colours::meterRed;
    transport[5]->ledColour = juce::Colour (0xffcfa63a);

    const char* names[tabs::count] { "BAND", "TRACKS", "STAGE", "INSTRUMENTS" };
    for (int i = 0; i < tabs::count; ++i)
    {
        auto b = std::make_unique<juce::TextButton> (names[i]);
        b->getProperties().set (props::style, "tab");
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*b);
        tabButtons[(size_t) i] = std::move (b);
    }

    bpmLabel.setEditable (false, true, false);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, colours::glow.brighter (0.6f));
    bpmLabel.setTooltip ("Song tempo: double-click to type it");
    bpmLabel.onTextChange = [this]
    {
        const double v = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue();
        if (v >= 40.0 && v <= 240.0) state.setBpm (v);
    };
    addAndMakeVisible (bpmLabel);

    bandPage = std::make_unique<BandPage> (state);
    tracksPage = std::make_unique<TracksPage> (state);
    stagePage = std::make_unique<StagePage> (state);
    instrumentsPage = std::make_unique<InstrumentsPage> (state);
    exportPanel = std::make_unique<ExportPanel> (state);
    exportPanel->onClose = [this] { exportPanel->setVisible (false); grabKeyboardFocus(); };
    for (juce::Component* c : { (juce::Component*) bandPage.get(), &bandPage->getSidePanel(), (juce::Component*) tracksPage.get(), &tracksPage->getSidePanel(),
                                (juce::Component*) stagePage.get(), &stagePage->getSidePanel(), (juce::Component*) instrumentsPage.get(), &instrumentsPage->getSidePanel() })
        addChildComponent (c);
    addChildComponent (*exportPanel);
    masterMeter.setTooltip ("Master level (click to clear the red clip light)");
    addAndMakeVisible (masterMeter);

    recent.setMaxNumberOfItems (10);
    recent.restoreFromString (settings.getValue ("recentSessions"));

    if (! state.openLibrary())
        statusText = state.getLibraryError();
    else
        statusText = "Library: " + juce::String ((int) state.getLibrary().getEntries().size()) + " instruments. Go to BAND and press WRITE THE SONG.";
    state.markSaved();
    showTab (state.getLibrary().isOpen() ? tabs::band : tabs::instruments);
    noKeyboardFocus (*this);
    setSize (1600, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    state.removeListener (this);
    settings.setValue ("recentSessions", recent.toString());
    exportPanel.reset();
    instrumentsPage.reset(); stagePage.reset(); tracksPage.reset(); bandPage.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::Rectangle<float> MainComponent::fp (float x, float y, float w, float h) const
{
    const float sx = (float) getWidth() / kW, sy = (float) getHeight() / kH;
    return { x * sx, y * sy, w * sx, h * sy };
}

//==============================================================================
void MainComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    state.uiScale = (float) getWidth() / 1600.0f;
    if (FaceplateKit::get().fullImage().isValid())
    {
        float ds = 1.0f;
        if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) ds = (float) disp->scale;
        background = juce::Image (juce::Image::RGB, juce::roundToInt ((float) getWidth() * ds), juce::roundToInt ((float) getHeight() * ds), false);
        juce::Graphics g (background);
        FaceplateKit::get().draw (g, FaceplateKit::Piece { { 0, 0, 3840, 2160 }, {}, 0.0f }, background.getBounds().toFloat(), (float) background.getWidth() / kW);
    }
    const float sx = (float) getWidth() / kW;
    menuBar.setBounds (fp (250.0f, 62.0f, 3450.0f, 125.0f).toNearestInt());
    for (int i = 0; i < 6; ++i)
        transport[(size_t) i]->setBounds (juce::Rectangle<float> (2.0f * kSocketR * sx, 2.0f * kSocketR * sx)
                                              .withCentre (fp (kSocketX[i], kSocketY, 0.0f, 0.0f).getPosition()).toNearestInt());
    for (int i = 0; i < tabs::count; ++i)
        tabButtons[(size_t) i]->setBounds (fp (kTabX[i], kTabY, kTabW, kTabH).toNearestInt());
    bpmLabel.setBounds (fp (3330.0f, 270.0f, 370.0f, 80.0f).toNearestInt());
    bpmLabel.setFont (ForgeLookAndFeel::font (juce::jmax (12.0f, 56.0f * sx)));

    const auto left = fp (92.0f, 708.0f, 596.0f, 1220.0f).toNearestInt();
    const auto main = fp (746.0f, 708.0f, 3004.0f, 1220.0f).toNearestInt();
    for (auto* c : { &bandPage->getSidePanel(), &tracksPage->getSidePanel(), &stagePage->getSidePanel(), &instrumentsPage->getSidePanel() })
        c->setBounds (left);
    for (auto* c : { (juce::Component*) bandPage.get(), (juce::Component*) tracksPage.get(), (juce::Component*) stagePage.get(), (juce::Component*) instrumentsPage.get() })
        c->setBounds (main);
    exportPanel->setBounds (main);
    masterMeter.setBounds (fp (3060.0f, 2000.0f, 650.0f, 70.0f).toNearestInt());
}

void MainComponent::paint (juce::Graphics& g)
{
    if (background.isValid()) g.drawImage (background, getLocalBounds().toFloat());
    else g.fillAll (colours::navy);
    const float sx = (float) getWidth() / kW;
    if (logo.isValid())
    {
        const auto r = juce::Rectangle<float> (2.0f * 56.0f * sx, 2.0f * 56.0f * sx).withCentre (fp (158.0f, 125.0f, 0.0f, 0.0f).getPosition());
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addEllipse (r);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, r, juce::RectanglePlacement::fillDestination);
    }
    auto plate = fp (165.0f, 272.0f, 980.0f, 145.0f);
    ForgeLookAndFeel::drawEngraved (g, state.songName() + (dirtyCached ? " *" : ""), plate.removeFromTop (plate.getHeight() * 0.6f),
                                    juce::Justification::bottomLeft, juce::jmax (12.0f, 64.0f * sx));
    const auto& song = state.getSong();
    ForgeLookAndFeel::drawEngraved (g, juce::String ((int) song.tracks.size()) + " TRACKS  -  "
                                        + (song.bandInfo.isNotEmpty() ? song.bandInfo.toUpperCase() : juce::String ("HEM INSTRUMENT FORGE")),
                                    plate, juce::Justification::centredLeft, juce::jmax (9.0f, 34.0f * sx));

    // counter glass
    auto glass = fp (2700.0f, 262.0f, 640.0f, 165.0f);
    const double beats = engine.getPositionBeats();
    const double bpm = juce::jmax (20.0, (double) engine.param (E::Ids::bpm));
    g.setFont (ForgeLookAndFeel::font (juce::jmax (14.0f, 92.0f * sx)));
    g.setColour (engine.isRecording() ? colours::meterRed.brighter (0.4f) : colours::glow.brighter (0.7f));
    const int bpb = juce::jmax (1, song.beatsPerBar);
    g.drawText (juce::String ((int) (beats / bpb) + 1) + "." + juce::String ((int) std::fmod (beats, (double) bpb) + 1)
                    + "   " + timeText (beats * 60.0 / bpm),
                glass.removeFromTop (glass.getHeight() * 0.62f), juce::Justification::centredLeft, false);
    g.setFont (ForgeLookAndFeel::font (juce::jmax (9.0f, 36.0f * sx)));
    g.setColour (colours::paperDim);
    const int bars = (int) std::ceil (song.lengthBeats() / bpb);
    g.drawText ("BAR . BEAT   OF " + juce::String (bars) + " BARS" + (engine.isLooping() ? "   LOOP " + juce::String ((int) (engine.getLoopStart() / bpb) + 1)
                    + "-" + juce::String ((int) std::ceil (engine.getLoopEnd() / bpb)) : juce::String())
                    + (engine.isRecording() ? "   REC" : juce::String()),
                glass, juce::Justification::centredLeft, false);

    // bottom strip
    ForgeLookAndFeel::drawEngraved (g, statusText, fp (110.0f, 1985.0f, 2600.0f, 100.0f), juce::Justification::centredLeft, juce::jmax (10.0f, 38.0f * sx));
    ForgeLookAndFeel::drawEngraved (g, juce::String (engine.getActiveVoices()) + " VOICES  " + juce::String (juce::roundToInt (engine.getCpuLoad() * 100.0)) + "% CPU",
                                    fp (2700.0f, 1985.0f, 340.0f, 100.0f), juce::Justification::centredRight, juce::jmax (9.0f, 30.0f * sx));
}

//==============================================================================
void MainComponent::showTab (int tab)
{
    currentTab = juce::jlimit (0, tabs::count - 1, tab);
    for (int i = 0; i < tabs::count; ++i) tabButtons[(size_t) i]->setToggleState (i == currentTab, juce::dontSendNotification);
    bandPage->setVisible (currentTab == tabs::band);
    bandPage->getSidePanel().setVisible (currentTab == tabs::band);
    tracksPage->setVisible (currentTab == tabs::tracks);
    tracksPage->getSidePanel().setVisible (currentTab == tabs::tracks);
    stagePage->setVisible (currentTab == tabs::stage);
    stagePage->getSidePanel().setVisible (currentTab == tabs::stage);
    instrumentsPage->setVisible (currentTab == tabs::instruments);
    instrumentsPage->getSidePanel().setVisible (currentTab == tabs::instruments);
    if (exportPanel->isVisible()) exportPanel->toFront (false);
    menuItemsChanged();
    if (currentTab != tabs::tracks || ! tracksPage->isShowingNotes()) grabKeyboardFocus();
}

void MainComponent::showExport()
{
    exportPanel->open();
}

void MainComponent::status (const juce::String& t)
{
    statusText = t;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (fp (60.0f, 1966.0f, 3060.0f, 140.0f).toNearestInt());
}

void MainComponent::songChanged()
{
    menuItemsChanged();
    repaint (fp (100.0f, 250.0f, 1100.0f, 190.0f).toNearestInt());
}

void MainComponent::updateTitle()
{
    if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        w->setName ("HEM Instrument Forge  -  " + state.songName() + (dirtyCached ? " *" : ""));
}

void MainComponent::timerCallback()
{
    ++tick;
    transport[1]->lit = engine.isPlaying();
    transport[3]->lit = engine.isLooping();
    transport[4]->lit = engine.isRecording();
    transport[5]->lit = exportPanel->isBusy();
    for (auto& t : transport) t->repaint();
    masterMeter.update (engine.getMasterPeak (0), engine.getMasterPeak (1));
    repaint (fp (2640.0f, 250.0f, 1090.0f, 186.0f).toNearestInt());
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (engine.param (E::Ids::bpm), 1) + " BPM", juce::dontSendNotification);
    if (tick % 15 == 0) repaint (fp (2700.0f, 1985.0f, 340.0f, 100.0f).toNearestInt());
    if (tick % 30 == 0)
    {
        const bool d = state.isDirty();
        if (d != dirtyCached || tick == 30)
        {
            dirtyCached = d;
            repaint (fp (100.0f, 250.0f, 1100.0f, 190.0f).toNearestInt());
            updateTitle();
        }
    }
    if (state.isLoading() && tick % 10 == 0 && statusTime == 0) { statusText = state.loadingText(); repaint (fp (60.0f, 1966.0f, 3060.0f, 140.0f).toNearestInt()); }
    if (statusTime != 0 && juce::Time::getMillisecondCounter() - statusTime > 15000)
    {
        statusTime = 0;
        statusText = engine.isPlaying() ? juce::String ("Playing") : juce::String ("Ready");
        repaint (fp (60.0f, 1966.0f, 3060.0f, 140.0f).toNearestInt());
    }
}

//==============================================================================
void MainComponent::togglePlay()
{
    if (engine.isPlaying()) { engine.stop(); engine.setRecording (false); return; }
    if (! engine.isLooping() && engine.getPositionBeats() >= state.getSong().lengthBeats() - 0.01) engine.setPositionBeats (0.0);
    engine.play();
}

void MainComponent::toggleLoop()
{
    const auto& s = state.getSong();
    if (! engine.isLooping() && engine.getLoopEnd() - engine.getLoopStart() < 0.5)
        engine.setLoop (true, 0.0, juce::jmax ((double) s.beatsPerBar, s.lengthBeats()));
    else if (! engine.isLooping() && engine.getLoopStart() >= s.lengthBeats())
        engine.setLoop (true, 0.0, juce::jmax ((double) s.beatsPerBar, s.lengthBeats()));
    else
        engine.setLoop (! engine.isLooping(), engine.getLoopStart(), engine.getLoopEnd());
    repaint();
}

void MainComponent::toggleRecord()
{
    if (state.getSong().tracks.empty()) { status ("Add a track first (INSTRUMENTS tab), then record into it."); return; }
    const bool on = ! engine.isRecording();
    engine.setRecording (on);
    if (on)
    {
        if (! engine.isPlaying()) engine.play();
        status ("Recording into track " + juce::String (state.getSelectedTrack() + 1) + ": play the keyboard (or your MIDI keyboard). Press R or stop to finish. (Click: STAGE tab.)");
    }
    else status ("Recording stopped.");
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (k == juce::KeyPress::returnKey) { transport[0]->triggerClick(); return true; }
    if (k == juce::KeyPress ('l')) { toggleLoop(); return true; }
    if (k == juce::KeyPress ('r')) { toggleRecord(); return true; }
    if (k == juce::KeyPress ('z', cmd, 0)) { state.undoLast(); return true; }
    if (k == juce::KeyPress ('y', cmd, 0) || k == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)) { state.redoLast(); return true; }
    if (k == juce::KeyPress ('s', cmd, 0)) { save(); return true; }
    if (k == juce::KeyPress ('s', cmd | juce::ModifierKeys::shiftModifier, 0)) { saveAs(); return true; }
    if (k == juce::KeyPress ('o', cmd, 0)) { checkSaveThen ([this] { openDialog(); }); return true; }
    if (k == juce::KeyPress ('n', cmd, 0)) { checkSaveThen ([this] { state.newSong(); status ("New song."); }); return true; }
    if (k == juce::KeyPress ('e', cmd, 0)) { showExport(); return true; }
    if (k == juce::KeyPress ('m', cmd, 0)) { importMidiDialog(); return true; }
    for (int i = 0; i < tabs::count; ++i)
        if (k == juce::KeyPress ('1' + i, cmd, 0)) { showTab (i); return true; }
    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "View", "Help" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    const bool hasTrack = state.getSelectedTrack() < (int) state.getSong().tracks.size();
    switch (index)
    {
        case 0:
        {
            m.addItem (mNew, "New Song        Ctrl+N");
            m.addItem (mOpen, "Open Song...    Ctrl+O");
            juce::PopupMenu rec;
            recent.createPopupMenuItems (rec, mRecentBase, false, true);
            if (rec.getNumItems() > 0) { rec.addSeparator(); rec.addItem (mClearRecent, "Clear list"); }
            m.addSubMenu ("Open Recent", rec, rec.getNumItems() > 0);
            m.addItem (mSave, "Save        Ctrl+S");
            m.addItem (mSaveAs, "Save As...  Ctrl+Shift+S");
            m.addSeparator();
            m.addItem (mImportMidi, "Import MIDI File...   Ctrl+M");
            m.addItem (mExport, "Export Stems / Mix / MIDI...   Ctrl+E");
            m.addItem (mExportMidi, "Export MIDI File Only...", ! state.getSong().tracks.empty());
            m.addSeparator();
            m.addItem (mAudio, "Audio and MIDI Settings (sound card, MIDI keyboard)...", devices != nullptr);
            m.addItem (mLibrary, "Instrument Library Folder...");
            m.addSeparator();
            m.addItem (mQuit, "Quit");
            break;
        }
        case 1:
            m.addItem (mUndo, "Undo " + state.getUndo().getUndoDescription() + "   Ctrl+Z", state.getUndo().canUndo());
            m.addItem (mRedo, "Redo " + state.getUndo().getRedoDescription() + "   Ctrl+Y", state.getUndo().canRedo());
            m.addSeparator();
            m.addItem (mAddTrack, "Add Track...", (int) state.getSong().tracks.size() < kMaxTracks);
            m.addItem (mDupTrack, "Duplicate Selected Track", hasTrack);
            m.addItem (mRemoveTrack, "Remove Selected Track", hasTrack);
            m.addSeparator();
            m.addItem (mClearSolo, "Clear All Solos");
            m.addItem (mClearMute, "Clear All Mutes");
            break;
        case 2:
        {
            const char* names[tabs::count] { "Band       Ctrl+1", "Tracks     Ctrl+2", "Stage      Ctrl+3", "Instruments   Ctrl+4" };
            for (int i = 0; i < tabs::count; ++i) m.addItem (mTabBase + i, names[i], true, currentTab == i);
            break;
        }
        case 3:
            m.addItem (mShortcuts, "Keyboard Shortcuts");
            m.addItem (mSongsFolder, "Open My Songs Folder");
            m.addItem (mLibraryFolder, "Open the Instrument Library Folder", state.getLibrary().isOpen());
            m.addItem (mAbout, "About and Credits");
            break;
        default: break;
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    if (id >= mRecentBase && id < mRecentBase + 20)
    {
        const auto f = recent.getFile (id - mRecentBase);
        checkSaveThen ([this, f] { openSession (f); });
        return;
    }
    if (id >= mTabBase && id < (int) mTabBase + (int) tabs::count) { showTab (id - (int) mTabBase); return; }
    switch (id)
    {
        case mNew: checkSaveThen ([this] { state.newSong(); status ("New song."); }); break;
        case mOpen: checkSaveThen ([this] { openDialog(); }); break;
        case mClearRecent: recent.clear(); settings.setValue ("recentSessions", {}); break;
        case mSave: save(); break;
        case mSaveAs: saveAs(); break;
        case mImportMidi: importMidiDialog(); break;
        case mExport: showExport(); break;
        case mExportMidi: exportMidiOnly(); break;
        case mAudio: showAudioSettings(); break;
        case mLibrary: chooseLibraryFolder(); break;
        case mQuit: juce::JUCEApplicationBase::getInstance()->systemRequestedQuit(); break;
        case mUndo: state.undoLast(); break;
        case mRedo: state.redoLast(); break;
        case mAddTrack: showTab (tabs::instruments); status ("Pick an instrument on the left, then ADD AS A NEW TRACK."); break;
        case mDupTrack: state.duplicateTrack (state.getSelectedTrack()); break;
        case mRemoveTrack: state.removeTrack (state.getSelectedTrack()); break;
        case mClearSolo: case mClearMute:
            for (int t = 0; t < kMaxTracks; ++t) engine.setParam (E::Ids::track (t, id == mClearSolo ? E::kSolo : E::kMute), 0.0f);
            break;
        case mShortcuts: showShortcuts(); break;
        case mSongsFolder: AppState::songsFolder().revealToUser(); break;
        case mLibraryFolder: state.getLibrary().getFolder().revealToUser(); break;
        case mAbout: showAbout(); break;
        default: break;
    }
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension (session::kExtension) || file.hasFileExtension ("mid;midi")) return true;
    }
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension (session::kExtension)) { checkSaveThen ([this, file] { openSession (file); }); return; }
    }
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension ("mid;midi"))
        {
            juce::String err;
            if (state.importMidi (file, err)) { status ("Imported " + file.getFileName()); showTab (tabs::tracks); }
            else status (err);
        }
    }
}

void MainComponent::checkSaveThen (std::function<void()> then)
{
    if (! state.isDirty()) { then(); return; }
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Save changes?")
                                      .withMessage ("\"" + state.songName() + "\" has changes that are not saved.")
                                      .withButton ("SAVE").withButton ("DON'T SAVE").withButton ("CANCEL"),
                                  [safe, then] (int r)
                                  {
                                      if (safe == nullptr) return;
                                      if (r == 1) { if (safe->state.getSessionFile() != juce::File()) { if (safe->save()) then(); }
                                                    else safe->saveAs ([then] (bool ok) { if (ok) then(); }); }
                                      else if (r == 2) then();
                                  });
}

void MainComponent::openSession (const juce::File& f)
{
    juce::String err;
    if (! state.openSession (f, err)) { status ("Could not open: " + err); return; }
    recent.addFile (f);
    settings.setValue ("recentSessions", recent.toString());
    dirtyCached = false;
    updateTitle();
    status ("Opened " + f.getFileName());
    showTab (tabs::tracks);
}

void MainComponent::openDialog()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open song", AppState::songsFolder(), "*" + juce::String (session::kExtension));
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& fc) { if (safe != nullptr && fc.getResult() != juce::File()) safe->openSession (fc.getResult()); });
}

bool MainComponent::save()
{
    if (state.getSessionFile() == juce::File()) { saveAs(); return false; }
    juce::String err;
    if (! state.saveSession (state.getSessionFile(), err)) { status ("Could not save: " + err); return false; }
    dirtyCached = false;
    updateTitle();
    status ("Saved " + state.getSessionFile().getFullPathName());
    repaint();
    return true;
}

void MainComponent::saveAs (std::function<void (bool)> done)
{
    auto start = state.getSessionFile() != juce::File() ? state.getSessionFile()
                                                        : AppState::songsFolder().getChildFile (state.songName() + session::kExtension);
    auto chooser = std::make_shared<juce::FileChooser> ("Save song as", start, "*" + juce::String (session::kExtension));
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, chooser, done] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              bool ok = false;
                              if (fc.getResult() != juce::File())
                              {
                                  juce::String err;
                                  ok = safe->state.saveSession (fc.getResult(), err);
                                  if (ok)
                                  {
                                      safe->recent.addFile (safe->state.getSessionFile());
                                      safe->settings.setValue ("recentSessions", safe->recent.toString());
                                      safe->dirtyCached = false;
                                      safe->updateTitle();
                                      safe->status ("Saved " + safe->state.getSessionFile().getFullPathName());
                                      safe->repaint();
                                  }
                                  else safe->status ("Could not save: " + err);
                              }
                              if (done) done (ok);
                          });
}

void MainComponent::importMidiDialog()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Import a MIDI file", AppState::songsFolder(), "*.mid;*.midi");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              juce::String err;
                              if (safe->state.importMidi (fc.getResult(), err)) { safe->status ("Imported " + fc.getResult().getFileName()); safe->showTab (tabs::tracks); }
                              else safe->status (err);
                          });
}

void MainComponent::exportMidiOnly()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Export MIDI file", AppState::songsFolder().getChildFile (state.songName() + ".mid"), "*.mid");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, chooser] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              auto& st = safe->state;
                              const auto song = st.getSong();
                              const bool ok = midi::write (song, fc.getResult().withFileExtension (".mid"),
                                                           [&st, &song] (int t) { return t < (int) song.tracks.size() && st.isDrumKit (song.tracks[(size_t) t].instrumentId); });
                              safe->status (ok ? "Wrote " + fc.getResult().withFileExtension (".mid").getFullPathName() : juce::String ("Could not write the MIDI file"));
                          });
}

void MainComponent::chooseLibraryFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Where is the HEM Instrument Library? (the folder with index.json)",
                                                        state.getLibrary().isOpen() ? state.getLibrary().getFolder() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe, chooser] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              if (safe->state.openLibraryAt (fc.getResult()))
                                  safe->status ("Library: " + juce::String ((int) safe->state.getLibrary().getEntries().size()) + " instruments in " + fc.getResult().getFullPathName());
                              else safe->status (safe->state.getLibraryError());
                          });
}

void MainComponent::showAudioSettings()
{
    if (devices == nullptr) return;
    auto* selector = new juce::AudioDeviceSelectorComponent (*devices, 0, 0, 2, 2, true, false, true, false);
    selector->setSize (600, 520);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio and MIDI Settings";
    o.dialogBackgroundColour = juce::Colour (0xff1b1f26);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showShortcuts()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Keyboard shortcuts").withMessage (
        "Space  -  play / pause\n"
        "Enter  -  back to the start (of the loop when looping)\n"
        "L  -  loop on / off (drag on a ruler to set the loop)\n"
        "R  -  record into the selected track\n"
        "Ctrl+Z / Ctrl+Y  -  undo / redo\n"
        "Ctrl+1 / 2 / 3 / 4  -  Band / Tracks / Stage / Instruments\n"
        "Ctrl+S save,  Ctrl+Shift+S save as,  Ctrl+O open,  Ctrl+N new\n"
        "Ctrl+E export,  Ctrl+M import MIDI\n\n"
        "Note editor: click to draw, drag to move, drag the right edge for length,\n"
        "right-click deletes, Ctrl+drag selects, Delete, Ctrl+A, Ctrl+D duplicate, Q quantize,\n"
        "arrow keys move (Shift+up/down = octave), Ctrl + wheel zooms.\n\n"
        "Instruments keyboard: keys A W S E D F T G Y H U J K play notes.").withButton ("OK"), nullptr);
}

void MainComponent::showAbout()
{
    juce::String credits = "Recorded instruments (all public domain, CC0):\n"
                           " - Versilian Community Sample Library (Versilian Studios and contributors)\n"
                           " - VS Chamber Orchestra 2: Community Edition (Sam Gossner, Simon Dalzell, Versilian Studios)\n"
                           " - Virtuosity Drums (performed by Austin McMahon)\n"
                           "Modeled instruments: original HEM code (no recordings).\n";
    if (state.getLibrary().isOpen())
    {
        const auto f = state.getLibrary().getFolder().getChildFile ("CREDITS.txt");
        if (f.existsAsFile()) credits << "\nFull credits: " << f.getFullPathName();
    }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("HEM Instrument Forge")
                                      .withMessage ("Version " HEM_VERSION_STRING "\n\nThe Hybrid Elite Mafia Instrument Forge.\n"
                                                    "Write songs with a band of recorded and modeled instruments, seat every player on a 3D stage, "
                                                    "export stems, a mastered mix and MIDI.\n\n" + credits + "\nBuilt with JUCE 8.")
                                      .withButton ("OK"), nullptr);
}
} // namespace hf::ui
