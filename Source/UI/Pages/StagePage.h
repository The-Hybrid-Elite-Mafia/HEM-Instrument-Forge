#pragma once

#include "UI/AppState.h"
#include "UI/Widgets.h"

namespace hf::ui
{
/**
    STAGE tab: the room seen from above with every player in it. Drag a player left / right and
    front / back; the sound moves with them (level, darkness, delay, early echoes and room all follow
    the distance). The selected player's height, width, level and room send are the knobs below.
    The left panel picks the room and how far away the microphones stand.
*/
class StagePage final : public juce::Component, private AppState::Listener, private juce::Timer
{
public:
    explicit StagePage (AppState&);
    ~StagePage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Component& getSidePanel() noexcept { return side; }

private:
    class Side final : public juce::Component
    {
    public:
        explicit Side (StagePage&);
        void paint (juce::Graphics&) override;
        void resized() override;
        StagePage& page;
        juce::ComboBox room;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roomAtt;
        ParamKnob size, level, mics, master;
        ParamToggle phones, click;
    };

    void selectionChanged() override { bindTrack(); repaint(); }
    void songChanged() override { bindTrack(); repaint(); }
    void timerCallback() override;
    void bindTrack();
    juce::Rectangle<float> stageArea() const;
    juce::Point<float> seatPos (int track) const;
    int playerAt (juce::Point<float>) const;

    AppState& state;
    Side side { *this };
    ParamKnob trackLevel, trackHeight, trackWidth, trackRoom;
    int dragging = -1;
    int boundTrack = -1;
};
} // namespace hf::ui
