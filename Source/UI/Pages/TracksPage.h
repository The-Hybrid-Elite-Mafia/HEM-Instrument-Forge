#pragma once

#include "UI/Pages/PianoRoll.h"

namespace hf::ui
{
juce::Colour trackColour (int track);

/** Pop-up menu of every installed instrument by family (ids returned through `chosen`). */
void showInstrumentMenu (AppState&, juce::Component& target, const juce::String& current, bool drumsOnly,
                         std::function<void (const juce::String&)> chosen);

/** Left panel of TRACKS: one row per track (name, instrument, mute, solo, meter). */
class TrackList final : public juce::Component, private AppState::Listener, private juce::Timer
{
public:
    explicit TrackList (AppState&);
    ~TrackList() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void rebuild();

private:
    class Row;
    void songChanged() override { rebuild(); }
    void selectionChanged() override;
    void libraryChanged() override { rebuild(); }
    void instrumentsChanged() override;
    void timerCallback() override;
    void layoutRows();

    AppState& state;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Row> rowComps;
    juce::TextButton addButton { "+ ADD TRACK" }, dupButton { "DUPLICATE" }, removeButton { "REMOVE" };
};

/** All tracks as lanes over the bars (click a lane to pick the track, double-click to edit its notes). */
class SongView final : public juce::Component, private AppState::Listener, private juce::Timer
{
public:
    explicit SongView (AppState&);
    ~SongView() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::function<void()> onEditNotes;

private:
    void songChanged() override { repaint(); }
    void selectionChanged() override { repaint(); }
    void timerCallback() override;
    juce::Rectangle<int> ruler() const;
    double beatsShown() const;
    float xOf (double beat) const;
    double beatAt (float x) const;
    AppState& state;
    double rulerStart = 0.0;
    bool rulerDrag = false;
    double lastPos = -1.0;
};

/** TRACKS tab: song overview or the piano roll of the selected track, with the editing tools. */
class TracksPage final : public juce::Component, private AppState::Listener
{
public:
    explicit TracksPage (AppState&);
    ~TracksPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Component& getSidePanel() noexcept { return trackList; }
    void showNotes (bool notes);
    bool isShowingNotes() const noexcept { return notesMode; }
    PianoRoll& getPianoRoll() noexcept { return roll; }

private:
    void selectionChanged() override { updateButtons(); }
    void songChanged() override { updateButtons(); }
    void updateButtons();

    AppState& state;
    TrackList trackList;
    SongView songView;
    PianoRoll roll;
    bool notesMode = false;
    juce::TextButton songButton { "SONG" }, notesButton { "NOTES" }, quantizeButton { "QUANTIZE" }, fitButton { "FIT" },
                     undoButton { "UNDO" }, redoButton { "REDO" }, rerollButton { "NEW TAKE OF THIS PART" }, clearButton { "CLEAR NOTES" },
                     instButton { "INSTRUMENT" };
    juce::ComboBox snapBox, velBox;
};
} // namespace hf::ui
