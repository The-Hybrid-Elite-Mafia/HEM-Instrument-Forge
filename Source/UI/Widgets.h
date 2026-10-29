#pragma once

#include "UI/ForgeLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace hf::ui
{
/** Round transport button that sits in one of the 6 chrome sockets of the main faceplate.
    The socket is part of the background art; this draws the cap, icon and glow. */
class TransportButton final : public juce::Button
{
public:
    enum class Icon { toStart, play, stop, loop, record, exportWav };
    TransportButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i) { setTooltip (tip); }

    bool lit = false;
    juce::Colour ledColour { colours::hemGreen };

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const float capR = R * 0.975f;
        juce::ColourGradient cap (juce::Colour (down ? 0xff2a2f36 : 0xff4b525c), c.x - capR * 0.4f, c.y - capR * 0.7f,
                                  juce::Colour (0xff101215), c.x + capR * 0.5f, c.y + capR, true);
        g.setGradientFill (cap);
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.22f : 0.12f));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, juce::jmax (1.0f, R * 0.03f));
        if (lit)
        {
            g.setColour (ledColour.withAlpha (0.18f));
            g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
            g.setColour (ledColour.withAlpha (0.8f));
            g.drawEllipse (c.x - capR * 0.96f, c.y - capR * 0.96f, capR * 1.92f, capR * 1.92f, juce::jmax (1.5f, R * 0.05f));
        }
        const auto ink = lit ? ledColour.brighter (0.6f) : colours::paper.withAlpha (over ? 1.0f : 0.85f);
        g.setColour (icon == Icon::record && ! lit ? juce::Colour (0xffd8423a) : ink);
        const float s = R * 0.336f;
        juce::Path p;
        switch (icon)
        {
            case Icon::toStart:
                p.addRectangle (c.x - s, c.y - s * 0.8f, s * 0.22f, s * 1.6f);
                p.addTriangle (c.x - s * 0.7f, c.y, c.x + s * 0.15f, c.y - s * 0.8f, c.x + s * 0.15f, c.y + s * 0.8f);
                p.addTriangle (c.x + s * 0.05f, c.y, c.x + s * 0.9f, c.y - s * 0.8f, c.x + s * 0.9f, c.y + s * 0.8f);
                break;
            case Icon::play:
                if (lit) { p.addRectangle (c.x - s * 0.6f, c.y - s * 0.8f, s * 0.42f, s * 1.6f); p.addRectangle (c.x + s * 0.18f, c.y - s * 0.8f, s * 0.42f, s * 1.6f); }
                else p.addTriangle (c.x - s * 0.55f, c.y - s * 0.9f, c.x - s * 0.55f, c.y + s * 0.9f, c.x + s * 0.95f, c.y);
                break;
            case Icon::stop:
                p.addRoundedRectangle (c.x - s * 0.7f, c.y - s * 0.7f, s * 1.4f, s * 1.4f, s * 0.12f);
                break;
            case Icon::loop:
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, s * 0.85f, s * 0.85f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
                g.strokePath (arc, juce::PathStrokeType (s * 0.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const float ax = c.x + std::sin (-0.2f) * s * 0.85f, ay = c.y - std::cos (-0.2f) * s * 0.85f;
                p.addTriangle (ax - s * 0.38f, ay - s * 0.3f, ax - s * 0.38f, ay + s * 0.3f, ax + s * 0.22f, ay);
                break;
            }
            case Icon::record:
                p.addEllipse (c.x - s * 0.75f, c.y - s * 0.75f, s * 1.5f, s * 1.5f);
                break;
            case Icon::exportWav:
                p.addRectangle (c.x - s * 0.14f, c.y - s * 0.9f, s * 0.28f, s * 0.9f);
                p.addTriangle (c.x - s * 0.55f, c.y - s * 0.05f, c.x + s * 0.55f, c.y - s * 0.05f, c.x, c.y + s * 0.55f);
                p.addRectangle (c.x - s * 0.85f, c.y + s * 0.7f, s * 1.7f, s * 0.2f);
                break;
        }
        g.fillPath (p);
    }

private:
    Icon icon;
};

/** Stereo peak meter (call update() from a UI timer with linear peaks). */
class StripMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void update (float l, float r)
    {
        auto toDb = [] (float v) { return v > 1.0e-6f ? 20.0f * std::log10 (v) : -120.0f; };
        auto fall = [&] (float& shown, float target) { const float db = toDb (target); shown = db > shown ? db : juce::jmax (db, shown - 1.6f); };
        fall (levL, l); fall (levR, r);
        holdL = juce::jmax (holdL - 0.3f, levL); holdR = juce::jmax (holdR - 0.3f, levR);
        if (l >= 1.0f || r >= 1.0f) clipped = true;
        repaint();
    }
    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }
    bool horizontal = true;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (b, 2.0f);
        if (horizontal)
        {
            auto clipLed = b.removeFromRight (juce::jmin (10.0f, b.getWidth() * 0.04f));
            g.setColour (clipped ? colours::meterRed : juce::Colour (0xff2a1414));
            g.fillRect (clipLed.reduced (1.0f, 2.0f));
            b = b.reduced (2.0f);
            const float h = (b.getHeight() - 2.0f) * 0.5f;
            bar (g, b.withHeight (h), levL, holdL, true);
            bar (g, b.withTrimmedTop (h + 2.0f), levR, holdR, true);
            return;
        }
        auto clipLed = b.removeFromTop (juce::jmin (6.0f, b.getHeight() * 0.05f));
        g.setColour (clipped ? colours::meterRed : juce::Colour (0xff2a1414));
        g.fillRect (clipLed.reduced (1.0f, 0.0f));
        b.removeFromTop (1.0f);
        const float w = (b.getWidth() - 1.0f) * 0.5f;
        bar (g, b.withWidth (w), levL, holdL, false);
        bar (g, b.withTrimmedLeft (w + 1.0f), levR, holdR, false);
    }

private:
    static float frac (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }
    static void bar (juce::Graphics& g, juce::Rectangle<float> r, float db, float hold, bool horiz)
    {
        const float f = frac (db);
        if (f > 0.0f)
        {
            if (horiz)
            {
                juce::ColourGradient grad (colours::hemGreen.darker (0.3f), r.getX(), 0.0f, colours::meterRed, r.getRight(), 0.0f, false);
                grad.addColour (frac (-12.0f), colours::hemGreen);
                grad.addColour (frac (0.0f), juce::Colour (0xffffb02e));
                g.setGradientFill (grad);
                g.fillRect (r.withWidth (r.getWidth() * f));
            }
            else
            {
                juce::ColourGradient grad (colours::meterRed, 0.0f, r.getY(), colours::hemGreen.darker (0.3f), 0.0f, r.getBottom(), false);
                grad.addColour (1.0 - frac (0.0f), juce::Colour (0xffffb02e));
                grad.addColour (1.0 - frac (-12.0f), colours::hemGreen);
                g.setGradientFill (grad);
                g.fillRect (r.withTop (r.getBottom() - r.getHeight() * f));
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        if (horiz) g.fillRect (r.getX() + r.getWidth() * frac (hold), r.getY(), 1.0f, r.getHeight());
        else g.fillRect (r.getX(), r.getBottom() - r.getHeight() * frac (hold), r.getWidth(), 1.0f);
    }
    float levL = -100.0f, levR = -100.0f, holdL = -100.0f, holdR = -100.0f;
    bool clipped = false;
};

/** A knob in a chrome socket with its name under it and its value shown while it is touched,
    bound to an APVTS parameter (SliderAttachment). Double-click returns to the default. */
class ParamKnob final : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& s, const juce::String& title, juce::Colour accent = colours::glow)
        : state (s), name (title)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.getProperties().set (props::accent, (juce::int64) accent.getARGB());
        slider.onValueChange = [this] { repaint(); };
        slider.onDragStart = [this] { showValue = true; repaint(); };
        slider.onDragEnd = [this] { showValue = false; repaint(); };
        addAndMakeVisible (slider);
    }

    /** Binds to a parameter id (re-binding is fine: tracks change which parameters a knob shows). */
    void bind (const juce::String& paramId)
    {
        attachment.reset();
        id = paramId;
        if (auto* p = state.getParameter (id))
        {
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            slider.setTooltip (p->getName (64) + ": drag up / down, double-click for the default");
        }
        repaint();
    }
    juce::Slider& getSlider() noexcept { return slider; }
    std::function<juce::String (double)> valueText;

    void resized() override
    {
        auto b = getLocalBounds();
        const int label = juce::jmax (14, getHeight() / 5);
        b.removeFromBottom (label);
        slider.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth(), b.getHeight()), juce::jmin (b.getWidth(), b.getHeight())));
    }
    void paint (juce::Graphics& g) override
    {
        const int label = juce::jmax (14, getHeight() / 5);
        const auto r = getLocalBounds().removeFromBottom (label).toFloat();
        juce::String text = name;
        if (showValue || slider.isMouseOver (true))
        {
            if (valueText) text = valueText (slider.getValue());
            else if (auto* p = state.getParameter (id)) text = p->getCurrentValueAsText() + (p->getLabel().isNotEmpty() ? " " + p->getLabel() : juce::String());
        }
        ForgeLookAndFeel::drawPanelText (g, text, r, juce::Justification::centred, juce::jmin (15.0f, (float) label * 0.8f));
    }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String name, id;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool showValue = false;
};

/** An LED toggle bound to a boolean parameter. */
class ParamToggle final : public juce::ToggleButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& s, const juce::String& text, juce::Colour accent = colours::hemGreen)
        : juce::ToggleButton (text), state (s)
    {
        getProperties().set (props::accent, (juce::int64) accent.getARGB());
    }
    void bind (const juce::String& paramId)
    {
        attachment.reset();
        if (state.getParameter (paramId) != nullptr)
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
    }
private:
    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Dark combo box with light text (for the grey panels). */
inline void styleDarkCombo (juce::ComboBox& c)
{
    c.getProperties().set (props::style, "dark");
    c.setColour (juce::ComboBox::textColourId, colours::paper);
    c.setColour (juce::ComboBox::arrowColourId, colours::paper);
}

/** Small dark button (lit = toggled) for the grey panels. */
inline void styleDarkButton (juce::Button& b, juce::Colour accent = colours::glow)
{
    b.getProperties().set (props::style, "dark");
    b.getProperties().set (props::accent, (juce::int64) accent.getARGB());
}

/** Light panel heading. */
inline void heading (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float h)
{
    ForgeLookAndFeel::drawPanelText (g, text, r, juce::Justification::centredLeft, h, colours::paper);
}
} // namespace hf::ui
