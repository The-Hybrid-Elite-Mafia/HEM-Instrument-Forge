#pragma once

#include "UI/AppState.h"
#include "UI/Widgets.h"

namespace hf::ui
{
/**
    INSTRUMENTS tab: every installed instrument, by family (left). The selected track can take any
    of them; the keyboard plays the selected track (mouse, computer keys A..K, or a MIDI keyboard),
    and the knobs set how that track plays: articulation, one-note-at-a-time, legato, humanize,
    velocity feel, release and transpose.
*/
class InstrumentsPage final : public juce::Component, private AppState::Listener, private juce::Timer,
                              private juce::MidiKeyboardState::Listener
{
public:
    explicit InstrumentsPage (AppState&);
    ~InstrumentsPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Component& getSidePanel() noexcept { return side; }
    /** Shows an instrument's details (also used by the preview tool). */
    void showInstrument (const juce::String& id);

private:
    class Side final : public juce::Component, private juce::ListBoxModel
    {
    public:
        explicit Side (InstrumentsPage&);
        ~Side() override { list.setModel (nullptr); }
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        int getNumRows() override { return rows.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int row) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        InstrumentsPage& page;
        juce::ComboBox family;
        juce::TextEditor search;
        juce::ListBox list;
        juce::StringArray rows;   // instrument ids
    };

    void libraryChanged() override { side.refresh(); repaint(); }
    void selectionChanged() override { bindTrack(); }
    void songChanged() override { bindTrack(); }
    void instrumentsChanged() override { bindTrack(); repaint(); }
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override;
    void bindTrack();
    void putOnTrack (bool asNewTrack);

    AppState& state;
    Side side { *this };
    juce::String shownId;
    juce::MidiKeyboardState keyState;
    juce::MidiKeyboardComponent keyboard { keyState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton putButton { "PUT ON SELECTED TRACK" }, newButton { "ADD AS A NEW TRACK" }, octDown { "<" }, octUp { ">" };
    juce::ComboBox articulation;
    ParamKnob human, velFeel, release, transpose;
    ParamToggle mono, legato;
    int boundTrack = -1;
    int keyOctave = 4;
    juce::String boundInstrument;
};
} // namespace hf::ui
