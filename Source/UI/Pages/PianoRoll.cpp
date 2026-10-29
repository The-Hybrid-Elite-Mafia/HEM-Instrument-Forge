#include "UI/Pages/PianoRoll.h"

namespace hf::ui
{
juce::String drumNoteName (int n)
{
    switch (n)
    {
        case 35: return "Kick 2";       case 36: return "Kick";          case 37: return "Rim / Side Stick";
        case 38: return "Snare";        case 39: return "Clap";          case 40: return "Snare 2";
        case 41: return "Floor Tom 2";  case 42: return "Hi-Hat Closed"; case 43: return "Floor Tom";
        case 44: return "Hi-Hat Pedal"; case 45: return "Low Tom";       case 46: return "Hi-Hat Open";
        case 47: return "Mid Tom";      case 48: return "High Tom 2";    case 49: return "Crash";
        case 50: return "High Tom";     case 51: return "Ride";          case 52: return "China";
        case 53: return "Ride Bell";    case 54: return "Tambourine";    case 55: return "Splash";
        case 56: return "Cowbell";      case 57: return "Crash 2";       case 58: return "Vibraslap";
        case 59: return "Ride 2";       case 60: return "Bongo High";    case 61: return "Bongo Low";
        case 62: return "Conga Mute";   case 63: return "Conga High";    case 64: return "Conga Low";
        case 67: return "Agogo High";   case 68: return "Agogo Low";     case 69: return "Cabasa";
        case 70: return "Shaker";       case 73: return "Guiro";         case 75: return "Claves";
        case 76: return "Woodblock";    case 80: return "Triangle Mute"; case 81: return "Triangle";
        default: return {};
    }
}

namespace
{
    bool isBlack (int p) { const int k = ((p % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    const juce::Colour kNote { 0xff3fa9ff };
    const juce::Colour kGridBg { 0xff1d2128 };

    Track* trackIn (Song& s, int t) { return t >= 0 && t < (int) s.tracks.size() ? &s.tracks[(size_t) t] : nullptr; }
    Note* noteIn (Track& t, juce::uint32 id)
    {
        for (auto& n : t.notes) if (n.id == id) return &n;
        return nullptr;
    }
}

PianoRoll::PianoRoll (AppState& s) : state (s)
{
    setWantsKeyboardFocus (true);
    state.addListener (this);
    startTimerHz (30);
}

PianoRoll::~PianoRoll()
{
    stopAudition();
    state.removeListener (this);
}

const Track* PianoRoll::track() const
{
    const auto& s = state.getSong();
    const int t = state.getSelectedTrack();
    return t >= 0 && t < (int) s.tracks.size() ? &s.tracks[(size_t) t] : nullptr;
}

PianoRoll::Layout PianoRoll::layout() const
{
    const float u = state.uiScale;
    auto b = getLocalBounds();
    Layout l;
    const int kw = juce::roundToInt (64.0f * u);
    l.ruler = b.removeFromTop (juce::roundToInt (22.0f * u)).withTrimmedLeft (kw);
    l.velLane = b.removeFromBottom (juce::roundToInt (64.0f * u));
    l.velKeys = l.velLane.removeFromLeft (kw);
    b.removeFromBottom (2);
    l.keys = b.removeFromLeft (kw);
    l.grid = b;
    return l;
}

float PianoRoll::keyH() const { return keyHeight * state.uiScale; }
double PianoRoll::beatAt (float x) const { return scrollBeats + (x - (float) layout().grid.getX()) / (pxPerBeat * state.uiScale); }
float PianoRoll::xOf (double beat) const { return (float) layout().grid.getX() + (float) ((beat - scrollBeats) * pxPerBeat * state.uiScale); }
int PianoRoll::pitchAt (float y) const { return juce::jlimit (0, 127, topPitch - (int) std::floor ((y - (float) layout().grid.getY()) / keyH())); }
float PianoRoll::yOf (int p) const { return (float) layout().grid.getY() + (float) (topPitch - p) * keyH(); }

double PianoRoll::snapBeat (double b) const
{
    return snap > 0.0 ? std::round (b / snap) * snap : b;
}

int PianoRoll::noteAt (juce::Point<float> p, bool& edge) const
{
    edge = false;
    const auto* t = track();
    if (t == nullptr) return -1;
    for (int i = (int) t->notes.size() - 1; i >= 0; --i)
    {
        const auto& n = t->notes[(size_t) i];
        const juce::Rectangle<float> r (xOf (n.start), yOf (n.pitch), juce::jmax (3.0f, xOf (n.end()) - xOf (n.start)), keyH());
        if (r.contains (p))
        {
            edge = p.x > r.getRight() - juce::jmin (8.0f * state.uiScale, r.getWidth() * 0.35f);
            return i;
        }
    }
    return -1;
}

void PianoRoll::audition (int pitch, int velocity)
{
    if (pitch == auditionKey) return;
    stopAudition();
    state.engine.uiNoteOn (pitch, velocity);
    auditionKey = pitch;
}

void PianoRoll::stopAudition()
{
    if (auditionKey >= 0) state.engine.uiNoteOff (auditionKey);
    auditionKey = -1;
}

//==============================================================================
void PianoRoll::resized()
{
    if (topPitch > 127) topPitch = 127;
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto l = layout();
    g.setColour (kGridBg);
    g.fillRect (getLocalBounds());
    paintGrid (g, l);
    paintNotes (g, l);
    paintKeys (g, l);
    paintRuler (g, l);
    paintVelocity (g, l);

    // play head
    const double pos = state.engine.getPositionBeats();
    const float px = xOf (pos);
    if (px >= (float) l.grid.getX() && px <= (float) l.grid.getRight())
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (px, (float) l.ruler.getY(), 1.5f, (float) (l.velLane.getBottom() - l.ruler.getY()));
    }
    if (drag == Drag::box)
    {
        g.setColour (colours::glow.withAlpha (0.15f));
        g.fillRect (box);
        g.setColour (colours::glow);
        g.drawRect (box, 1.0f);
    }
    if (track() == nullptr)
        ForgeLookAndFeel::drawPanelText (g, "No track yet: add one on the left, or write a song on the BAND tab.", l.grid.toFloat(),
                                         juce::Justification::centred, 18.0f * state.uiScale);
}

void PianoRoll::paintGrid (juce::Graphics& g, const Layout& l)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (l.grid);
    const float kh = keyH();
    for (int p = topPitch; p >= 0; --p)
    {
        const float y = yOf (p);
        if (y > (float) l.grid.getBottom()) break;
        g.setColour (isBlack (p) ? juce::Colour (0xff181b21) : juce::Colour (0xff22262e));
        g.fillRect ((float) l.grid.getX(), y, (float) l.grid.getWidth(), kh);
        if (p % 12 == 0) { g.setColour (juce::Colours::white.withAlpha (0.10f)); g.fillRect ((float) l.grid.getX(), y + kh - 1.0f, (float) l.grid.getWidth(), 1.0f); }
    }
    const auto& s = state.getSong();
    const double b0 = std::floor (beatAt ((float) l.grid.getX())), b1 = beatAt ((float) l.grid.getRight()) + 1.0;
    const double sub = snap > 0.0 && snap * pxPerBeat * state.uiScale >= 6.0 ? snap : 1.0;
    for (double b = b0; b <= b1; b += sub)
    {
        const float x = xOf (b);
        const bool bar = std::fmod (b, (double) s.beatsPerBar) == 0.0;
        const bool beat = std::fmod (b, 1.0) == 0.0;
        g.setColour (juce::Colours::white.withAlpha (bar ? 0.22f : beat ? 0.09f : 0.04f));
        g.fillRect (x, (float) l.grid.getY(), 1.0f, (float) l.grid.getHeight());
    }
    // song end and loop
    if (state.engine.isLooping())
    {
        const float a = xOf (state.engine.getLoopStart()), e = xOf (state.engine.getLoopEnd());
        g.setColour (colours::glow.withAlpha (0.06f));
        g.fillRect (a, (float) l.grid.getY(), e - a, (float) l.grid.getHeight());
    }
}

void PianoRoll::paintNotes (juce::Graphics& g, const Layout& l)
{
    const auto* t = track();
    if (t == nullptr) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (l.grid);
    const float kh = keyH();
    for (auto& n : t->notes)
    {
        const float x0 = xOf (n.start), x1 = xOf (n.end());
        if (x1 < (float) l.grid.getX() || x0 > (float) l.grid.getRight()) continue;
        const float y = yOf (n.pitch);
        if (y + kh < (float) l.grid.getY() || y > (float) l.grid.getBottom()) continue;
        const auto r = juce::Rectangle<float> (x0, y, juce::jmax (3.0f, x1 - x0), kh).reduced (0.5f, 1.0f);
        const bool sel = selection.count (n.id) > 0;
        const float v = (float) n.velocity / 127.0f;
        g.setColour ((sel ? juce::Colour (0xffffc34d) : kNote).withMultipliedBrightness (0.55f + 0.45f * v));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (sel ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 2.5f, 1.0f);
        if (kh >= 12.0f && r.getWidth() > 26.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (ForgeLookAndFeel::font (juce::jmin (12.0f, kh * 0.8f), false));
            g.drawText (noteName (n.pitch), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

void PianoRoll::paintKeys (juce::Graphics& g, const Layout& l)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (l.keys);
    const bool drums = track() != nullptr && state.isDrumKit (track()->instrumentId);
    const float kh = keyH();
    for (int p = topPitch; p >= 0; --p)
    {
        const float y = yOf (p);
        if (y > (float) l.keys.getBottom()) break;
        const auto r = juce::Rectangle<float> ((float) l.keys.getX(), y, (float) l.keys.getWidth(), kh);
        g.setColour (p == auditionKey ? colours::glow : isBlack (p) ? juce::Colour (0xff15171b) : juce::Colour (0xffe6e9ee));
        g.fillRect (r.reduced (0.0f, 0.5f));
        const auto text = drums ? drumNoteName (p) : (p % 12 == 0 ? noteName (p) : juce::String());
        if (text.isNotEmpty() && kh >= 9.0f)
        {
            g.setColour (isBlack (p) && p != auditionKey ? colours::paper : colours::ink);
            g.setFont (ForgeLookAndFeel::font (juce::jmin (11.0f * state.uiScale, kh * 0.85f), false));
            g.drawText (text, r.reduced (3.0f, 0.0f), juce::Justification::centredRight, true);
        }
    }
}

void PianoRoll::paintRuler (juce::Graphics& g, const Layout& l)
{
    g.setColour (juce::Colour (0xff111318));
    g.fillRect (l.ruler.withLeft (0));
    const auto& s = state.getSong();
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (l.ruler);
    if (state.engine.isLooping())
    {
        const float a = xOf (state.engine.getLoopStart()), e = xOf (state.engine.getLoopEnd());
        g.setColour (colours::glow.withAlpha (0.45f));
        g.fillRect (a, (float) l.ruler.getY() + 2.0f, e - a, (float) l.ruler.getHeight() - 4.0f);
    }
    g.setFont (ForgeLookAndFeel::font (12.0f * state.uiScale, false));
    const int barStep = pxPerBeat * state.uiScale * s.beatsPerBar < 40.0 ? 4 : 1;
    const int firstBar = juce::jmax (0, (int) std::floor (beatAt ((float) l.ruler.getX()) / s.beatsPerBar));
    for (int bar = firstBar; ; ++bar)
    {
        const float x = xOf ((double) bar * s.beatsPerBar);
        if (x > (float) l.ruler.getRight()) break;
        if (bar % barStep != 0) continue;
        g.setColour (colours::paperDim);
        g.fillRect (x, (float) l.ruler.getBottom() - 6.0f, 1.0f, 6.0f);
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, (float) l.ruler.getY(), 40.0f, (float) l.ruler.getHeight()), juce::Justification::centredLeft, false);
    }
}

void PianoRoll::paintVelocity (juce::Graphics& g, const Layout& l)
{
    g.setColour (juce::Colour (0xff15181d));
    g.fillRect (l.velLane);
    g.setColour (juce::Colour (0xff111318));
    g.fillRect (l.velKeys);
    ForgeLookAndFeel::drawPanelText (g, "VELOCITY", l.velKeys.toFloat(), juce::Justification::centred, 10.0f * state.uiScale, colours::paperDim);
    const auto* t = track();
    if (t == nullptr) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (l.velLane);
    const auto inner = l.velLane.reduced (0, 4).toFloat();
    for (auto& n : t->notes)
    {
        const float x = xOf (n.start);
        if (x < (float) l.velLane.getX() - 4.0f || x > (float) l.velLane.getRight()) continue;
        const float h = inner.getHeight() * (float) n.velocity / 127.0f;
        const bool sel = selection.count (n.id) > 0;
        g.setColour (sel ? juce::Colour (0xffffc34d) : kNote);
        g.fillRect (x, inner.getBottom() - h, 3.0f, h);
        g.fillEllipse (x - 2.0f, inner.getBottom() - h - 3.0f, 7.0f, 7.0f);
    }
}

//==============================================================================
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto l = layout();
    const auto p = e.position;
    const int t = state.getSelectedTrack();
    if (track() == nullptr) return;
    dragBefore = state.getSong();

    if (l.ruler.contains (p.toInt()))
    {
        drag = Drag::ruler;
        rulerStart = juce::jmax (0.0, snapBeat (beatAt (p.x)));
        return;
    }
    if (l.keys.contains (p.toInt())) { audition (pitchAt (p.y), newVelocity); drag = Drag::scrollKeys; return; }
    if (l.velLane.contains (p.toInt()) && p.x >= (float) l.velLane.getX())
    {
        drag = Drag::velocity;
        mouseDrag (e);
        return;
    }
    if (! l.grid.contains (p.toInt())) return;

    bool edge = false;
    const int hit = noteAt (p, edge);
    if (hit >= 0)
    {
        const auto n = track()->notes[(size_t) hit];
        if (e.mods.isPopupMenu())
        {
            auto s = state.getSong();
            auto& tr = s.tracks[(size_t) t];
            tr.notes.erase (std::remove_if (tr.notes.begin(), tr.notes.end(), [&] (const Note& x) { return x.id == n.id; }), tr.notes.end());
            selection.erase (n.id);
            state.changeSong (s, "Delete note");
            return;
        }
        if (e.mods.isShiftDown()) { if (selection.count (n.id)) selection.erase (n.id); else selection.insert (n.id); }
        else if (selection.count (n.id) == 0) selection = { n.id };
        dragNote = n.id;
        drag = edge ? Drag::resize : Drag::move;
        dragStartBeat = beatAt (p.x);
        dragStartPitch = n.pitch;
        grabOffset = dragStartBeat - n.start;
        dragOriginals.clear();
        for (auto& x : track()->notes) if (selection.count (x.id)) dragOriginals[x.id] = x;
        lastLength = n.length;
        audition (n.pitch, n.velocity);
        repaint();
        return;
    }
    if (e.mods.isPopupMenu()) return;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        drag = Drag::box;
        box = { p, p };
        if (! e.mods.isShiftDown()) selection.clear();
        return;
    }
    // draw a new note
    auto s = state.getSong();
    auto* tr = trackIn (s, t);
    Note n;
    n.start = juce::jmax (0.0, snap > 0.0 ? std::floor (beatAt (p.x) / snap) * snap : beatAt (p.x));
    n.length = lastLength;
    n.pitch = pitchAt (p.y);
    n.velocity = newVelocity;
    n.id = s.newId();
    tr->notes.push_back (n);
    tr->sortNotes();
    selection = { n.id };
    dragNote = n.id;
    dragStartBeat = n.start;
    drag = Drag::create;
    state.previewSong (s);
    audition (n.pitch, n.velocity);
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const int t = state.getSelectedTrack();
    const double minLen = snap > 0.0 ? snap : 1.0 / 32.0;
    switch (drag)
    {
        case Drag::ruler:
        {
            const double b = juce::jmax (0.0, snapBeat (beatAt (p.x)));
            if (std::abs (e.getDistanceFromDragStartX()) > 4 && std::abs (b - rulerStart) > 1.0e-6)
                state.engine.setLoop (true, juce::jmin (rulerStart, b), juce::jmax (rulerStart, b));
            repaint();
            break;
        }
        case Drag::scrollKeys:
            audition (pitchAt (p.y), newVelocity);
            repaint (layout().keys);
            break;
        case Drag::create:
        {
            auto s = state.getSong();
            if (auto* tr = trackIn (s, t))
                if (auto* n = noteIn (*tr, dragNote))
                {
                    const double end = beatAt (p.x);
                    n->length = juce::jmax (minLen, snap > 0.0 ? std::ceil ((end - n->start) / snap - 1.0e-9) * snap : end - n->start);
                    lastLength = n->length;
                    state.previewSong (s);
                }
            break;
        }
        case Drag::move:
        case Drag::resize:
        {
            auto s = state.getSong();
            auto* tr = trackIn (s, t);
            if (tr == nullptr) break;
            double dBeat = beatAt (p.x) - dragStartBeat;
            if (snap > 0.0) dBeat = std::round (dBeat / snap) * snap;
            const int dPitch = drag == Drag::move ? pitchAt (p.y) - dragStartPitch : 0;
            if (drag == Drag::move && e.mods.isAltDown()) dBeat = 0.0;   // Alt: change pitch only
            for (auto& [id, orig] : dragOriginals)
                if (auto* n = noteIn (*tr, id))
                {
                    if (drag == Drag::move)
                    {
                        n->start = juce::jmax (0.0, orig.start + dBeat);
                        n->pitch = juce::jlimit (0, 127, orig.pitch + dPitch);
                    }
                    else
                    {
                        n->length = juce::jmax (minLen, orig.length + dBeat);
                        if (id == dragNote) lastLength = n->length;
                    }
                }
            tr->sortNotes();
            state.previewSong (s);
            if (drag == Drag::move && dragOriginals.count (dragNote))
                audition (juce::jlimit (0, 127, dragOriginals[dragNote].pitch + dPitch), dragOriginals[dragNote].velocity);
            break;
        }
        case Drag::box:
        {
            box = juce::Rectangle<float> (e.mouseDownPosition, p);
            if (const auto* tr = track())
                for (auto& n : tr->notes)
                {
                    const juce::Rectangle<float> r (xOf (n.start), yOf (n.pitch), juce::jmax (3.0f, xOf (n.end()) - xOf (n.start)), keyH());
                    if (r.intersects (box)) selection.insert (n.id);
                }
            repaint();
            break;
        }
        case Drag::velocity:
        {
            const auto l = layout();
            const auto inner = l.velLane.reduced (0, 4).toFloat();
            const int vel = juce::jlimit (1, 127, juce::roundToInt (127.0f * (inner.getBottom() - p.y) / inner.getHeight()));
            auto s = state.getSong();
            auto* tr = trackIn (s, t);
            if (tr == nullptr) break;
            bool changed = false;
            const float tol = 5.0f * state.uiScale;
            for (auto& n : tr->notes)
            {
                const float x = xOf (n.start);
                const bool inSel = selection.empty() || selection.count (n.id) > 0;
                if (std::abs (x - p.x) <= tol && inSel) { n.velocity = vel; changed = true; }
            }
            if (changed) state.previewSong (s);
            break;
        }
        case Drag::none:
        default: break;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    stopAudition();
    const auto d = drag;
    drag = Drag::none;
    if (d == Drag::ruler && std::abs (e.getDistanceFromDragStartX()) <= 4)
        state.engine.setPositionBeats (rulerStart);
    const char* what = d == Drag::create ? "Draw note" : d == Drag::move ? "Move notes" : d == Drag::resize ? "Change note length"
                     : d == Drag::velocity ? "Change velocity" : nullptr;
    if (what != nullptr)
    {
        const bool same = dragBefore.toTree().isEquivalentTo (state.getSong().toTree());
        if (! same) state.commitPreview (dragBefore, what);
    }
    repaint();
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const auto l = layout();
    if (l.grid.contains (e.position.toInt()) && noteAt (e.position, edge) >= 0)
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (l.grid.contains (e.position.toInt()) ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (layout().ruler.contains (e.position.toInt())) state.engine.setLoop (false, state.engine.getLoopStart(), state.engine.getLoopEnd());
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float dy = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = beatAt (e.position.x);
        pxPerBeat = juce::jlimit (6.0, 480.0, pxPerBeat * (dy > 0 ? 1.2 : 1.0 / 1.2));
        scrollBeats = juce::jmax (0.0, anchor - (e.position.x - (float) layout().grid.getX()) / (pxPerBeat * state.uiScale));
    }
    else if (e.mods.isShiftDown() || w.deltaX != 0.0f)
        scrollBeats = juce::jmax (0.0, scrollBeats - (double) (w.deltaX != 0.0f ? w.deltaX : dy) * 8.0 * 60.0 / pxPerBeat);
    else
        topPitch = juce::jlimit (12, 127, topPitch + (dy > 0 ? 3 : -3));
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (k == juce::KeyPress ('a', cmd, 0)) { selectAll(); return true; }
    if (k == juce::KeyPress ('d', cmd, 0)) { duplicateSelection(); return true; }
    if (k == juce::KeyPress ('q')) { quantizeSelection(); return true; }
    const bool up = k.getKeyCode() == juce::KeyPress::upKey, down = k.getKeyCode() == juce::KeyPress::downKey;
    const bool left = k.getKeyCode() == juce::KeyPress::leftKey, right = k.getKeyCode() == juce::KeyPress::rightKey;
    if ((up || down || left || right) && ! selection.empty())
    {
        auto s = state.getSong();
        auto* tr = trackIn (s, state.getSelectedTrack());
        if (tr == nullptr) return false;
        const int dp = (up ? 1 : down ? -1 : 0) * (k.getModifiers().isShiftDown() ? 12 : 1);
        const double db = (right ? 1.0 : left ? -1.0 : 0.0) * (snap > 0.0 ? snap : 0.25);
        for (auto& n : tr->notes)
            if (selection.count (n.id))
            {
                n.pitch = juce::jlimit (0, 127, n.pitch + dp);
                n.start = juce::jmax (0.0, n.start + db);
            }
        tr->sortNotes();
        state.changeSong (s, dp != 0 ? "Transpose notes" : "Move notes");
        return true;
    }
    return false;
}

//==============================================================================
void PianoRoll::deleteSelection()
{
    if (selection.empty()) return;
    auto s = state.getSong();
    auto* tr = trackIn (s, state.getSelectedTrack());
    if (tr == nullptr) return;
    tr->notes.erase (std::remove_if (tr->notes.begin(), tr->notes.end(), [this] (const Note& n) { return selection.count (n.id) > 0; }), tr->notes.end());
    selection.clear();
    state.changeSong (s, "Delete notes");
}

void PianoRoll::selectAll()
{
    selection.clear();
    if (auto* t = track()) for (auto& n : t->notes) selection.insert (n.id);
    repaint();
}

void PianoRoll::duplicateSelection()
{
    auto s = state.getSong();
    auto* tr = trackIn (s, state.getSelectedTrack());
    if (tr == nullptr || selection.empty()) return;
    double first = 1.0e9, last = 0.0;
    for (auto& n : tr->notes) if (selection.count (n.id)) { first = juce::jmin (first, n.start); last = juce::jmax (last, n.end()); }
    // repeat after the selection, rounded up to a whole bar
    const double bar = s.beatsPerBar;
    const double shift = juce::jmax (bar, std::ceil ((last - first) / bar - 1.0e-9) * bar);
    std::vector<Note> add;
    std::set<juce::uint32> fresh;
    for (auto& n : tr->notes)
        if (selection.count (n.id)) { auto c = n; c.start += shift; c.id = s.newId(); fresh.insert (c.id); add.push_back (c); }
    tr->notes.insert (tr->notes.end(), add.begin(), add.end());
    tr->sortNotes();
    selection = fresh;
    state.changeSong (s, "Duplicate notes");
}

void PianoRoll::quantizeSelection()
{
    const double q = snap > 0.0 ? snap : 0.25;
    auto s = state.getSong();
    auto* tr = trackIn (s, state.getSelectedTrack());
    if (tr == nullptr) return;
    for (auto& n : tr->notes)
        if (selection.empty() || selection.count (n.id))
        {
            const double end = n.end();
            n.start = std::round (n.start / q) * q;
            n.length = juce::jmax (q, std::round ((end - n.start) / q) * q);
        }
    tr->sortNotes();
    state.changeSong (s, "Quantize");
    if (state.status) state.status (juce::String ("Quantized ") + (selection.empty() ? "the whole track" : "the selected notes"));
}

void PianoRoll::fitToNotes()
{
    const auto l = layout();
    int lo = 128, hi = -1;
    if (auto* t = track()) for (auto& n : t->notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    if (hi < 0) { lo = 48; hi = 72; }
    lo = juce::jmax (0, lo - 3); hi = juce::jmin (127, hi + 3);
    keyHeight = juce::jlimit (6.0f, 22.0f, (float) l.grid.getHeight() / (float) (hi - lo + 1) / state.uiScale);
    topPitch = juce::jmin (127, hi + juce::jmax (0, (int) ((float) l.grid.getHeight() / keyH()) - (hi - lo + 1)) / 2);
    const double len = juce::jmax ((double) state.getSong().beatsPerBar * 4, state.getSong().lengthBeats());
    pxPerBeat = juce::jlimit (6.0, 480.0, (double) l.grid.getWidth() / len / state.uiScale);
    scrollBeats = 0.0;
    repaint();
}

void PianoRoll::songChanged()
{
    // drop selected ids that no longer exist (undo / another page changed the notes)
    if (auto* t = track())
    {
        std::set<juce::uint32> keep;
        for (auto& n : t->notes) if (selection.count (n.id)) keep.insert (n.id);
        selection = keep;
    }
    else selection.clear();
    repaint();
}

void PianoRoll::selectionChanged()
{
    selection.clear();
    fitToNotes();
}

void PianoRoll::timerCallback()
{
    const double pos = state.engine.getPositionBeats();
    if (std::abs (pos - lastPlayhead) > 1.0e-6 && isShowing())
    {
        // follow the play head while playing
        if (state.engine.isPlaying() && drag == Drag::none)
        {
            const auto l = layout();
            const float x = xOf (pos);
            if (x > (float) l.grid.getRight() - 20.0f || x < (float) l.grid.getX())
                scrollBeats = juce::jmax (0.0, pos - 1.0);
        }
        lastPlayhead = pos;
        repaint();
    }
}
} // namespace hf::ui
