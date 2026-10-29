#pragma once

#include "UI/AppState.h"
#include "UI/Widgets.h"

namespace hf::ui
{
/** "Kick", "Snare", ... for General MIDI drum notes (empty if the note has no standard name). */
juce::String drumNoteName (int note);

/**
    Note editor for the selected track.
      click on empty space      draw a note (drag to set its length); you hear it
      drag a note               move it (time snaps to the grid, up / down changes the pitch)
      drag a note's right edge  make it longer / shorter
      right-click a note        delete it
      Ctrl + drag               select with a box;  Shift + click adds to the selection
      Delete                    delete the selected notes;  Ctrl+A select all;  Ctrl+D duplicate
      velocity lane (bottom)    drag the bars to make notes softer / harder
      ruler (top)               click to move the play position, drag to set the loop
      mouse wheel               scroll up / down;  Shift + wheel sideways;  Ctrl + wheel zoom
    Every drag is one undo step. Message thread only.
*/
class PianoRoll final : public juce::Component, private AppState::Listener, private juce::Timer
{
public:
    explicit PianoRoll (AppState&);
    ~PianoRoll() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    double snap = 0.25;            // beats (0 = off)
    int newVelocity = 100;
    void quantizeSelection();
    void deleteSelection();
    void selectAll();
    void duplicateSelection();
    void fitToNotes();

private:
    void songChanged() override;
    void selectionChanged() override;
    void timerCallback() override;

    struct Layout { juce::Rectangle<int> ruler, keys, grid, velLane, velKeys; };
    Layout layout() const;
    double beatAt (float x) const;
    float xOf (double beat) const;
    int pitchAt (float y) const;
    float yOf (int pitch) const;
    float keyH() const;
    double snapBeat (double b) const;
    const Track* track() const;
    int noteAt (juce::Point<float>, bool& onRightEdge) const;
    void audition (int pitch, int velocity);
    void stopAudition();
    void paintKeys (juce::Graphics&, const Layout&);
    void paintGrid (juce::Graphics&, const Layout&);
    void paintNotes (juce::Graphics&, const Layout&);
    void paintVelocity (juce::Graphics&, const Layout&);
    void paintRuler (juce::Graphics&, const Layout&);

    AppState& state;
    double pxPerBeat = 60.0, scrollBeats = 0.0;
    int topPitch = 84;             // highest key shown
    float keyHeight = 14.0f;
    std::set<juce::uint32> selection;   // note ids

    enum class Drag { none, create, move, resize, box, velocity, ruler, scrollKeys };
    Drag drag = Drag::none;
    Song dragBefore;
    juce::uint32 dragNote = 0;
    double dragStartBeat = 0.0, grabOffset = 0.0;
    int dragStartPitch = 60;
    std::map<juce::uint32, Note> dragOriginals;
    juce::Rectangle<float> box;
    double lastLength = 1.0;
    int auditionKey = -1;
    double rulerStart = 0.0;
    double lastPlayhead = -1.0;
};
} // namespace hf::ui
