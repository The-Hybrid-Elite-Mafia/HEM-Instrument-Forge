#include "PlaygroundLookAndFeel.h"
#include "PlaygroundBinaryData.h"

namespace pg::ui
{
namespace
{
    juce::Typeface::Ptr boldFace()
    {
        static auto t = juce::Typeface::createSystemTypefaceFor (PlaygroundBinary::PoppinsBold_ttf, PlaygroundBinary::PoppinsBold_ttfSize);
        return t;
    }
    juce::Typeface::Ptr mediumFace()
    {
        static auto t = juce::Typeface::createSystemTypefaceFor (PlaygroundBinary::PoppinsMedium_ttf, PlaygroundBinary::PoppinsMedium_ttfSize);
        return t;
    }
    juce::String styleOf (const juce::Component& c) { return c.getProperties()[props::style].toString(); }
}

PlaygroundLookAndFeel::PlaygroundLookAndFeel (juce::Colour a) : accent (a)
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1f26));
    setColour (juce::PopupMenu::textColourId, colours::paper);
    setColour (juce::PopupMenu::headerTextColourId, colours::paperDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, colours::ink);
    setColour (juce::ComboBox::arrowColourId, colours::ink);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::TextButton::textColourOnId, colours::ink);
    setColour (juce::Label::textColourId, colours::paper);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1b1f26));
    setColour (juce::AlertWindow::textColourId, colours::paper);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1015));
    setColour (juce::TextEditor::textColourId, colours::paper);
    setColour (juce::TextEditor::outlineColourId, accent.withAlpha (0.6f));
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::ResizableWindow::backgroundColourId, colours::navy);
    setColour (juce::DocumentWindow::textColourId, colours::paper);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1b1f26));
    setColour (juce::TooltipWindow::textColourId, colours::paper);
    setColour (juce::TooltipWindow::outlineColourId, accent.withAlpha (0.5f));
}

juce::Colour PlaygroundLookAndFeel::accentOf (const juce::Component& c) const
{
    const auto v = c.getProperties()[props::accent];
    return v.isVoid() ? accent : juce::Colour ((juce::uint32) (juce::int64) v);
}

juce::Font PlaygroundLookAndFeel::font (float height, bool bold)
{
    auto f = juce::Font (juce::FontOptions (bold ? boldFace() : mediumFace()).withHeight (height));
    return f;
}

juce::Typeface::Ptr PlaygroundLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return f.isBold() ? boldFace() : mediumFace();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font PlaygroundLookAndFeel::getComboBoxFont (juce::ComboBox& b)   { return font (juce::jmin (styleOf (b) == "dark" ? 14.0f : 18.0f, (float) b.getHeight() * 0.5f)); }
juce::Font PlaygroundLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (17.0f, (float) h * 0.42f)); }
juce::Font PlaygroundLookAndFeel::getPopupMenuFont()                     { return font (16.0f, false); }

void PlaygroundLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const int inset = styleOf (box) == "dark" ? 5 : juce::roundToInt ((float) box.getHeight() * 0.3f);
    label.setBounds (inset, 1, box.getWidth() - box.getHeight() - inset, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

//==============================================================================
void PlaygroundLookAndFeel::drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
{
    if (on)
    {
        juce::ColourGradient glow (col.withAlpha (0.55f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 3.2f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - r * 3.2f, c.y - r * 3.2f, r * 6.4f, r * 6.4f);
    }
    juce::ColourGradient body (on ? col.brighter (0.9f) : col.darker (2.5f).withSaturation (0.3f), c.x - r * 0.3f, c.y - r * 0.4f,
                               on ? col : juce::Colour (0xff101214), c.x + r, c.y + r, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, juce::jmax (1.0f, r * 0.15f));
}

void PlaygroundLookAndFeel::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float h)
{
    g.setFont (font (h));
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.drawText (text, r.translated (0.0f, 1.0f), j, false);
    g.setColour (colours::ink);
    g.drawText (text, r, j, false);
}

void PlaygroundLookAndFeel::drawPanelText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float h, juce::Colour c)
{
    g.setFont (font (h));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText (text, r.translated (0.0f, 1.5f), j, false);
    g.setColour (c);
    g.drawText (text, r, j, false);
}

//==============================================================================
void PlaygroundLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> b, float pos, float start, float end,
                                      bool bipolar, float zeroPos, juce::Colour acc, bool hot) const
{
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float ringR = R * 0.735f, ringW = R * 0.085f;
    const float angle = start + pos * (end - start);

    // value ring in the socket's recess
    juce::Path track;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff1a1d22).withAlpha (0.85f));
    g.strokePath (track, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float zeroAngle = bipolar ? start + zeroPos * (end - start) : start;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
    g.setColour (acc.withAlpha (hot ? 0.45f : 0.3f));
    g.strokePath (arc, juce::PathStrokeType (ringW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (acc.brighter (0.25f));
    g.strokePath (arc, juce::PathStrokeType (ringW * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body: dark gunmetal cap with a chrome edge
    const float kR = R * 0.58f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - kR, c.y - kR + R * 0.05f, kR * 2.0f, kR * 2.0f);
    juce::ColourGradient edge (juce::Colour (0xffe9edf2), c.x - kR, c.y - kR, juce::Colour (0xff4a5058), c.x + kR, c.y + kR, false);
    g.setGradientFill (edge);
    g.fillEllipse (c.x - kR, c.y - kR, kR * 2.0f, kR * 2.0f);
    const float capR = kR * 0.88f;
    juce::ColourGradient cap (juce::Colour (0xff4b525c), c.x - capR * 0.5f, c.y - capR * 0.6f,
                              juce::Colour (0xff121418), c.x + capR * 0.6f, c.y + capR, true);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    // fine knurl ring
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
        g.drawLine (c.x + std::sin (a) * capR * 0.86f, c.y - std::cos (a) * capR * 0.86f,
                    c.x + std::sin (a) * capR * 0.98f, c.y - std::cos (a) * capR * 0.98f, juce::jmax (0.6f, R * 0.012f));
    }
    // sheen
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.16f), c.x, c.y - capR, juce::Colours::white.withAlpha (0.0f), c.x, c.y + capR * 0.2f, false);
    g.setGradientFill (sheen);
    g.fillEllipse (c.x - capR * 0.8f, c.y - capR * 0.95f, capR * 1.6f, capR * 1.1f);

    // pointer
    const juce::Point<float> p0 (c.x + std::sin (angle) * capR * 0.25f, c.y - std::cos (angle) * capR * 0.25f);
    const juce::Point<float> p1 (c.x + std::sin (angle) * capR * 0.86f, c.y - std::cos (angle) * capR * 0.86f);
    g.setColour (acc.withAlpha (0.35f));
    g.drawLine ({ p0, p1 }, R * 0.09f);
    g.setColour (acc.brighter (0.6f));
    g.drawLine ({ p0, p1 }, R * 0.035f);
}

void PlaygroundLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const float size = (float) juce::jmin (w, h);
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre (size, size);
    FaceplateKit::get().drawSocket (g, b);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    drawKnob (g, b, pos, start, end, bipolar, bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f, accentOf (s), s.isMouseOverOrDragging());
}

//==============================================================================
void PlaygroundLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto style = styleOf (b);
    auto r = b.getLocalBounds().toFloat();
    auto& kit = FaceplateKit::get();
    const auto acc = accentOf (b);

    if (style == "ms" || style == "dark")
    {
        const bool on = b.getToggleState();
        g.setColour (on ? acc : juce::Colour (0xff1b1f26));
        g.fillRoundedRectangle (r.reduced (0.5f), 3.0f);
        g.setColour (on ? acc.brighter (0.4f) : juce::Colours::white.withAlpha (over ? 0.35f : 0.15f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, 3.0f); }
        return;
    }
    if (style == "tab")
    {
        const float scale = r.getHeight() / (float) FaceplateKit::tabSlot.src.getHeight();
        kit.draw (g, FaceplateKit::tabSlot, r, scale);
        const auto inner = r.reduced (r.getHeight() * 0.2f, r.getHeight() * 0.2f);
        if (b.getToggleState())
        {
            g.setColour (acc.withAlpha (0.22f));
            g.fillRoundedRectangle (inner, inner.getHeight() * 0.3f);
            g.setColour (acc.withAlpha (0.9f));
            g.drawRoundedRectangle (inner, inner.getHeight() * 0.3f, juce::jmax (1.5f, r.getHeight() * 0.04f));
        }
        else if (over || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.07f));
            g.fillRoundedRectangle (inner, inner.getHeight() * 0.3f);
        }
        return;
    }

    // "plate" (default): small brushed plate in a bezel
    const float scale = r.getHeight() / (float) FaceplateKit::plate.src.getHeight();
    kit.draw (g, FaceplateKit::plate, r, scale);
    if (over || down)
    {
        g.setColour ((down ? acc : juce::Colours::white).withAlpha (down ? 0.25f : 0.15f));
        g.fillRoundedRectangle (r.reduced (r.getHeight() * 0.16f), r.getHeight() * 0.12f);
    }
}

void PlaygroundLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float h = getTextButtonFont (b, b.getHeight()).getHeight();
    if (styleOf (b) == "tab")
    {
        const auto acc = accentOf (b);
        drawPanelText (g, b.getButtonText(), r, juce::Justification::centred, h, b.getToggleState() ? juce::Colours::white : colours::paper);
        return;
    }
    if (styleOf (b) == "ms" || styleOf (b) == "dark")
    {
        g.setFont (font (juce::jmin (14.0f, r.getHeight() * 0.6f)));
        g.setColour (b.getToggleState() && styleOf (b) == "ms" ? colours::ink : colours::paper);
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
        return;
    }
    if (b.getButtonText() == "<" || b.getButtonText() == ">")
    {
        // arrow buttons: engraved triangles
        const auto c = r.getCentre();
        const float a = r.getHeight() * 0.13f, dir = b.getButtonText() == "<" ? -1.0f : 1.0f;
        juce::Path tri;
        tri.addTriangle (c.x + dir * a, c.y, c.x - dir * a * 0.8f, c.y - a, c.x - dir * a * 0.8f, c.y + a);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillPath (tri, juce::AffineTransform::translation (0.0f, 1.0f));
        g.setColour (colours::ink);
        g.fillPath (tri);
        return;
    }
    drawEngraved (g, b.getButtonText(), r, juce::Justification::centred, h);
}

void PlaygroundLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // "led": round socket with an LED, label to the right in the button's text colour
    auto r = b.getLocalBounds().toFloat();
    const float d = r.getHeight();
    const auto sock = r.removeFromLeft (d);
    FaceplateKit::get().drawSocket (g, sock);
    const auto acc = accentOf (b);
    drawLed (g, sock.getCentre(), d * 0.2f, acc, b.getToggleState());
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillEllipse (sock.reduced (d * 0.12f)); }
    if (b.getButtonText().isNotEmpty())
    {
        const bool onChrome = b.getProperties()["onChrome"];
        r.removeFromLeft (d * 0.15f);
        if (onChrome) drawEngraved (g, b.getButtonText(), r, juce::Justification::centredLeft, juce::jmin (16.0f, d * 0.45f));
        else drawPanelText (g, b.getButtonText(), r, juce::Justification::centredLeft, juce::jmin (16.0f, d * 0.45f));
    }
}

void PlaygroundLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                              juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto acc = accentOf (s);
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    if (style == juce::Slider::LinearVertical)
    {
        // fader: dark slot, accent fill below the cap, chrome cap with a centre line
        const float slotW = juce::jmax (4.0f, b.getWidth() * 0.12f);
        const auto slot = juce::Rectangle<float> (b.getCentreX() - slotW * 0.5f, b.getY(), slotW, b.getHeight());
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (slot, slotW * 0.5f);
        g.setColour (acc.withAlpha (0.55f));
        g.fillRoundedRectangle (slot.withTop (pos), slotW * 0.5f);
        // dB ticks
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f })
        {
            if (db < s.getMinimum() || db > s.getMaximum()) continue;
            const float ty = (float) s.getPositionOfValue (db);
            const float tw = db == 0.0f ? b.getWidth() * 0.42f : b.getWidth() * 0.26f;
            g.fillRect (b.getCentreX() - tw, ty, tw * 2.0f, db == 0.0f ? 2.0f : 1.0f);
        }
        const float capH = juce::jlimit (16.0f, 34.0f, b.getWidth() * 0.62f), capW = b.getWidth() * 0.86f;
        const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ b.getCentreX(), pos });
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
        juce::ColourGradient chrome (juce::Colour (0xfff3f5f8), cap.getX(), cap.getY(), juce::Colour (0xff7d848e), cap.getX(), cap.getBottom(), false);
        chrome.addColour (0.48, juce::Colour (0xffd5d9df));
        chrome.addColour (0.52, juce::Colour (0xff9aa1aa));
        g.setGradientFill (chrome);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (colours::ink);
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        g.setColour (acc);
        g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 6.0f, 2.0f);
        return;
    }
    // horizontal: thin dark track + accent fill + round thumb
    const auto track = b.withSizeKeepingCentre (b.getWidth(), juce::jmax (3.0f, b.getHeight() * 0.18f));
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    g.setColour (acc);
    g.fillRoundedRectangle (track.withRight (pos), track.getHeight() * 0.5f);
    const float r = juce::jmin (b.getHeight() * 0.42f, 9.0f);
    g.setColour (colours::paper);
    g.fillEllipse (pos - r, b.getCentreY() - r, r * 2.0f, r * 2.0f);
    g.setColour (colours::ink);
    g.drawEllipse (pos - r, b.getCentreY() - r, r * 2.0f, r * 2.0f, 1.0f);
}

void PlaygroundLookAndFeel::drawMenuBarBackground (juce::Graphics&, int, int, bool, juce::MenuBarComponent&)
{
    // the menu sits on the faceplate's chrome strip: nothing to paint
}

juce::Font PlaygroundLookAndFeel::getMenuBarFont (juce::MenuBarComponent& bar, int, const juce::String&)
{
    return font (juce::jlimit (12.0f, 20.0f, (float) bar.getHeight() * 0.42f));
}

int PlaygroundLookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent& bar, int index, const juce::String& text)
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (getMenuBarFont (bar, index, text), text)) + bar.getHeight();
}

void PlaygroundLookAndFeel::drawMenuBarItem (juce::Graphics& g, int w, int h, int, const juce::String& text, bool over, bool open, bool, juce::MenuBarComponent& bar)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    if (open || over)
    {
        g.setColour ((open ? accent : colours::ink).withAlpha (open ? 0.35f : 0.12f));
        g.fillRoundedRectangle (r.reduced (2.0f, (float) h * 0.14f), 5.0f);
    }
    drawEngraved (g, text, r, juce::Justification::centred, getMenuBarFont (bar, 0, text).getHeight());
}

void PlaygroundLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    // "plate": draws its own brushed plate; otherwise it sits on a plate the editor drew
    if (styleOf (box) == "plate")
        FaceplateKit::get().draw (g, FaceplateKit::plate, { 0.0f, 0.0f, (float) w, (float) h }, (float) h / (float) FaceplateKit::plate.src.getHeight());
    else if (styleOf (box) == "dark")
    {
        g.setColour (juce::Colour (0xff0d1015));
        g.fillRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 3.0f);
        g.setColour (accentOf (box).withAlpha (box.isMouseOver (true) ? 0.8f : 0.35f));
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 3.0f, 1.0f);
    }
    const float a = (float) h * 0.16f;
    const float cx = (float) w - (float) h * 0.5f, cy = (float) h * 0.5f;
    juce::Path tri;
    tri.addTriangle (cx - a, cy - a * 0.5f, cx + a, cy - a * 0.5f, cx, cy + a * 0.7f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.85f : 0.3f));
    g.fillPath (tri);
}
} // namespace pg::ui
