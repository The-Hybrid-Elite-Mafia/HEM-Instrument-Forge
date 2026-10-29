#pragma once

#include "UI/AppState.h"
#include "UI/Widgets.h"
#include "UI/Pages/BandPage.h"
#include "UI/Pages/TracksPage.h"
#include "UI/Pages/StagePage.h"
#include "UI/Pages/InstrumentsPage.h"
#include "UI/Pages/ExportPanel.h"

namespace hf::ui
{
namespace tabs { enum { band = 0, tracks, stage, instruments, count }; }

/**
    The Instrument Forge main window on Steven's main faceplate (3840 x 2160, shown at 16:9):
    menu bar on the top chrome strip; song plate, 6 transport sockets (to start, play, stop, loop,
    record, export) and the counter glass on the second row; BAND / TRACKS / STAGE / INSTRUMENTS in
    the four tab slots; the page's side panel on the left and the page in the big grey window;
    status, voices / CPU and the master meter on the bottom strip.
*/
class MainComponent final : public juce::Component,
                            public juce::MenuBarModel,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private AppState::Listener
{
public:
    MainComponent (ForgeEngine&, juce::AudioDeviceManager*, juce::PropertiesFile&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void checkSaveThen (std::function<void()> then);
    void openSession (const juce::File&);
    void showTab (int);
    int getTab() const noexcept { return currentTab; }
    AppState& getState() noexcept { return state; }
    BandPage& getBandPage() noexcept { return *bandPage; }
    TracksPage& getTracksPage() noexcept { return *tracksPage; }
    InstrumentsPage& getInstrumentsPage() noexcept { return *instrumentsPage; }
    ExportPanel& getExportPanel() noexcept { return *exportPanel; }
    void showExport();

    /** 4K faceplate rectangle -> window rectangle. */
    juce::Rectangle<float> fp (float x, float y, float w, float h) const;

private:
    void timerCallback() override;
    void songChanged() override;
    void libraryChanged() override { repaint(); }
    void status (const juce::String&);
    void togglePlay();
    void toggleLoop();
    void toggleRecord();
    bool save();
    void saveAs (std::function<void (bool)> done = {});
    void openDialog();
    void importMidiDialog();
    void exportMidiOnly();
    void chooseLibraryFolder();
    void showAudioSettings();
    void showShortcuts();
    void showAbout();
    void updateTitle();

    ForgeEngine& engine;
    juce::AudioDeviceManager* devices;
    juce::PropertiesFile& settings;
    ForgeLookAndFeel lnf;
    AppState state;

    juce::Image background, logo;
    juce::MenuBarComponent menuBar { this };
    std::array<std::unique_ptr<TransportButton>, 6> transport;
    std::array<std::unique_ptr<juce::TextButton>, tabs::count> tabButtons;
    juce::Label bpmLabel;
    std::unique_ptr<BandPage> bandPage;
    std::unique_ptr<TracksPage> tracksPage;
    std::unique_ptr<StagePage> stagePage;
    std::unique_ptr<InstrumentsPage> instrumentsPage;
    std::unique_ptr<ExportPanel> exportPanel;
    StripMeter masterMeter;
    int currentTab = tabs::band;
    juce::RecentlyOpenedFilesList recent;
    juce::String statusText;
    juce::uint32 statusTime = 0;
    bool dirtyCached = false;
    int tick = 0;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace hf::ui
