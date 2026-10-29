#include "UI/Pages/ExportPanel.h"

namespace hf::ui
{
class ExportPanel::Job final : public juce::Thread
{
public:
    Job (ExportPanel& p, Song s, juce::ValueTree params, exporter::Instruments insts, ExportOptions o)
        : juce::Thread ("HEM export", 8 * 1024 * 1024), panel (p), song (std::move (s)), parameters (std::move (params)), instruments (std::move (insts)), options (std::move (o)) {}
    ~Job() override { cancelFlag.store (true); stopThread (60000); }

    void run() override
    {
        ExportResult result;
        juce::String error;
        auto* progress = &panel.progress;
        auto* lock = &panel.textLock;
        auto* text = &panel.progressText;
        const bool ok = exporter::run (song, parameters, instruments, options, result, error,
                                       [progress, lock, text] (float f, const juce::String& what)
                                       {
                                           progress->store (f);
                                           const juce::ScopedLock sl (*lock);
                                           *text = what;
                                       },
                                       &cancelFlag);
        juce::Component::SafePointer<ExportPanel> safe (&panel);
        const auto folder = options.folder;
        const bool cancelled = cancelFlag.load();
        juce::MessageManager::callAsync ([safe, ok, error, result, folder, cancelled]
        {
            if (safe != nullptr) safe->finished (ok && ! cancelled, cancelled ? juce::String ("Export cancelled") : error, result, folder);
        });
    }
    std::atomic<bool> cancelFlag { false };

private:
    ExportPanel& panel;
    Song song;
    juce::ValueTree parameters;
    exporter::Instruments instruments;
    ExportOptions options;
};

ExportPanel::ExportPanel (AppState& s) : state (s)
{
    for (auto* t : { &stems, &midiFile })
    {
        t->setToggleState (true, juce::dontSendNotification);
        t->getProperties().set (props::accent, (juce::int64) colours::hemGreen.getARGB());
        addAndMakeVisible (*t);
    }
    for (auto* c : { &mix, &rate, &bits }) { styleDarkCombo (*c); addAndMakeVisible (*c); }
    mix.addItem ("Mix: none", 1);
    mix.addItem ("Mix: clean (no mastering, peaks under -1 dB)", 2);
    mix.addItem ("Mix: mastered (Elite Master, -14 LUFS, ready to share)", 3);
    mix.setSelectedId (s.settings.getIntValue ("export.mix", 3), juce::dontSendNotification);
    rate.addItem ("44.1 kHz", 44100);
    rate.addItem ("48 kHz", 48000);
    rate.addItem ("96 kHz", 96000);
    rate.setSelectedId (s.settings.getIntValue ("export.rate", 48000), juce::dontSendNotification);
    bits.addItem ("16-bit (dithered, CD)", 16);
    bits.addItem ("24-bit", 24);
    bits.addItem ("32-bit float", 32);
    bits.setSelectedId (s.settings.getIntValue ("export.bits", 24), juce::dontSendNotification);
    name.setFont (ForgeLookAndFeel::font (16.0f, false));
    addAndMakeVisible (name);
    for (auto* b : { &folderButton, &goButton, &closeButton, &openButton }) { styleDarkButton (*b); addAndMakeVisible (*b); }
    goButton.getProperties().set (props::accent, (juce::int64) colours::hemGreen.getARGB());
    folderButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Export into which folder?", folder.getParentDirectory());
        juce::Component::SafePointer<ExportPanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe, chooser] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult() != juce::File()) { safe->folder = fc.getResult(); safe->repaint(); }
                              });
    };
    goButton.onClick = [this] { if (isBusy()) cancel(); else start(); };
    closeButton.onClick = [this] { if (! isBusy() && onClose) onClose(); };
    openButton.onClick = [this] { if (lastFolder.isDirectory()) lastFolder.revealToUser(); };
    openButton.setVisible (false);
}

ExportPanel::~ExportPanel() { job.reset(); }

void ExportPanel::open()
{
    if (! isBusy())
    {
        name.setText (state.songName(), false);
        folder = exporter::defaultFolder (state.songName());
        openButton.setVisible (lastFolder.isDirectory());
    }
    setVisible (true);
    toFront (true);
    repaint();
}

void ExportPanel::start()
{
    if (isBusy()) return;
    const auto& song = state.getSong();
    if (song.tracks.empty()) { lastResult = "Nothing to export yet: the song has no tracks."; repaint(); return; }
    if (state.isLoading()) { lastResult = "Wait a moment: " + state.loadingText(); repaint(); return; }
    exporter::Instruments insts {};
    juce::StringArray none;
    for (int t = 0; t < (int) song.tracks.size(); ++t)
    {
        insts[(size_t) t] = state.instrumentOf (t);
        if (insts[(size_t) t] == nullptr && ! song.tracks[(size_t) t].notes.empty()) none.add (song.tracks[(size_t) t].name);
    }
    ExportOptions o;
    o.stems = stems.getToggleState();
    o.midi = midiFile.getToggleState();
    o.mix = mix.getSelectedId() == 1 ? ExportOptions::Mix::none : mix.getSelectedId() == 2 ? ExportOptions::Mix::clean : ExportOptions::Mix::mastered;
    o.sampleRate = (double) rate.getSelectedId();
    o.bitDepth = bits.getSelectedId();
    o.baseName = juce::File::createLegalFileName (name.getText().trim().isEmpty() ? state.songName() : name.getText().trim());
    o.folder = folder;
    if (! o.stems && ! o.midi && o.mix == ExportOptions::Mix::none) { lastResult = "Pick at least one thing to export."; repaint(); return; }
    state.settings.setValue ("export.mix", mix.getSelectedId());
    state.settings.setValue ("export.rate", rate.getSelectedId());
    state.settings.setValue ("export.bits", bits.getSelectedId());
    progress.store (0.0f);
    {
        const juce::ScopedLock sl (textLock);
        progressText = "Starting";
    }
    lastResult = none.isEmpty() ? juce::String() : "No instrument (silent): " + none.joinIntoString (", ");
    job = std::make_unique<Job> (*this, song, state.engine.getAPVTS().copyState(), insts, o);
    job->startThread (juce::Thread::Priority::normal);
    goButton.setButtonText ("CANCEL");
    closeButton.setEnabled (false);
    openButton.setVisible (false);
    startTimerHz (10);
}

void ExportPanel::cancel()
{
    if (job != nullptr) job->cancelFlag.store (true);
}

void ExportPanel::finished (bool ok, const juce::String& error, const ExportResult& result, const juce::File& f)
{
    stopTimer();
    job.reset();
    goButton.setButtonText ("EXPORT");
    closeButton.setEnabled (true);
    if (ok)
    {
        lastFolder = f;
        lastResult = "Done: " + juce::String (result.files.size()) + " file(s) in " + f.getFullPathName()
                     + (result.mixInfo.isNotEmpty() ? "   (" + result.mixInfo + ")" : juce::String());
        openButton.setVisible (true);
        if (state.status) state.status (lastResult);
    }
    else
    {
        lastResult = error;
        if (state.status) state.status (error);
    }
    repaint();
}

void ExportPanel::timerCallback() { repaint(); }

void ExportPanel::resized()
{
    const float u = state.uiScale;
    auto b = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, juce::roundToInt (720.0f * u)), juce::jmin (getHeight() - 20, juce::roundToInt (460.0f * u)));
    b = b.reduced (juce::roundToInt (24.0f * u));
    b.removeFromTop (juce::roundToInt (40.0f * u));
    const int h = juce::roundToInt (30.0f * u), gap = juce::roundToInt (8.0f * u);
    name.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    auto fr = b.removeFromTop (h);
    folderButton.setBounds (fr.removeFromRight (juce::roundToInt (150.0f * u)));
    b.removeFromTop (gap * 2);
    stems.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    mix.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    midiFile.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    auto fmt = b.removeFromTop (h);
    rate.setBounds (fmt.removeFromLeft (fmt.getWidth() / 2 - gap));
    fmt.removeFromLeft (gap * 2);
    bits.setBounds (fmt);
    auto buttons = b.removeFromBottom (juce::roundToInt (36.0f * u));
    goButton.setBounds (buttons.removeFromLeft (juce::roundToInt (160.0f * u)));
    buttons.removeFromLeft (gap);
    openButton.setBounds (buttons.removeFromLeft (juce::roundToInt (160.0f * u)));
    closeButton.setBounds (buttons.removeFromRight (juce::roundToInt (120.0f * u)));
}

void ExportPanel::paint (juce::Graphics& g)
{
    const float u = state.uiScale;
    g.fillAll (colours::navy.withAlpha (0.6f));
    auto box = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, juce::roundToInt (720.0f * u)), juce::jmin (getHeight() - 20, juce::roundToInt (460.0f * u))).toFloat();
    g.setColour (juce::Colour (0xff1b1f26));
    g.fillRoundedRectangle (box, 10.0f);
    g.setColour (colours::glow.withAlpha (0.6f));
    g.drawRoundedRectangle (box, 10.0f, 1.5f);
    auto b = box.reduced (24.0f * u);
    heading (g, "EXPORT", b.removeFromTop (34.0f * u), 22.0f * u);
    b.removeFromTop ((30.0f + 8.0f) * u);
    ForgeLookAndFeel::drawPanelText (g, folder.getFullPathName(), b.removeFromTop (30.0f * u).withTrimmedRight (160.0f * u),
                                     juce::Justification::centredLeft, 12.0f * u, colours::paperDim);
    auto status = box.reduced (24.0f * u).removeFromBottom (36.0f * u + 64.0f * u).removeFromTop (56.0f * u);
    if (isBusy())
    {
        juce::String what;
        {
            const juce::ScopedLock sl (textLock);
            what = progressText;
        }
        const auto bar = status.removeFromTop (14.0f * u);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (colours::hemGreen);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress.load())), 4.0f);
        ForgeLookAndFeel::drawPanelText (g, what, status, juce::Justification::centredLeft, 13.0f * u);
    }
    else if (lastResult.isNotEmpty())
        ForgeLookAndFeel::drawPanelText (g, lastResult, status, juce::Justification::centredLeft, 13.0f * u,
                                         lastResult.startsWith ("Done") ? colours::hemGreen : juce::Colour (0xffffb02e));
}
} // namespace hf::ui
