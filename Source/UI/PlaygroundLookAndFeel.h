#pragma once

#include "FaceplateKit.h"

namespace pg::ui
{
namespace colours
{
    inline const juce::Colour ink       { 0xff15181d };   // dark text on chrome
    inline const juce::Colour inkSoft   { 0xff3b4048 };
    inline const juce::Colour paper     { 0xfff4f6f8 };   // light text on the grey panels
    inline const juce::Colour paperDim  { 0xffc4c9d0 };
    inline const juce::Colour navy      { 0xff05070d };   // gaps between the chrome pieces
    inline const juce::Colour glow      { 0xff2f8bff };   // HEM blue
    inline const juce::Colour hemGreen  { 0xff35e06a };
    inline const juce::Colour meterRed  { 0xffff4a3d };
}

/** Component property keys the look-and-feel reads. */
namespace props
{
    inline const juce::Identifier style  { "pgStyle" };   // "tab", "led", "plate", "glass"
    inline const juce::Identifier accent { "pgAccent" };  // ARGB int, overrides the look-and-feel accent
}

/**
    Rework Playground look: controls are drawn in code over the faceplate pieces
    (knobs sit in the chrome sockets, tabs in the tab slots, readouts behind glass).
*/
class PlaygroundLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit PlaygroundLookAndFeel (juce::Colour accent = colours::glow);

    void setAccent (juce::Colour c) noexcept { accent = c; }
    juce::Colour getAccent() const noexcept { return accent; }
    juce::Colour accentOf (const juce::Component&) const;

    static juce::Font font (float height, bool bold = true);

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawMenuBarBackground (juce::Graphics&, int w, int h, bool isMouseOverBar, juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int w, int h, int itemIndex, const juce::String& text, bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent&) override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    int getMenuBarItemWidth (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;

    /** A knob face (no socket) - also used by the mixer. */
    void drawKnob (juce::Graphics&, juce::Rectangle<float> socketBounds, float pos, float start, float end, bool bipolar, float zeroPos, juce::Colour accent, bool hot) const;
    /** Accent LED with glow. */
    static void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, bool on);
    /** Engraved text on chrome (dark with a light lower edge). */
    static void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float height);
    /** Light text with a soft shadow, for the grey panels. */
    static void drawPanelText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float height, juce::Colour = colours::paper);

private:
    juce::Colour accent;
};
} // namespace pg::ui
