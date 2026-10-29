#include "UI/Pages/TracksPage.h"
#include "Band/BandBridge.h"

namespace hf::ui
{
using E = ForgeEngine;

juce::Colour trackColour (int t)
{
    static const juce::uint32 c[] { 0xff3fa9ff, 0xff35e06a, 0xffffb02e, 0xffff6b6b, 0xffb388ff, 0xff4dd0e1, 0xfff06292, 0xffcddc39,
                                    0xffff8a65, 0xff81c784, 0xff9fa8da, 0xffffd54f };
    return juce::Colour (c[(size_t) ((t % 12) + 12) % 12]);
}

void showInstrumentMenu (AppState& state, juce::Component& target, const juce::String& current, bool drumsOnly,
                         std::function<void (const juce::String&)> chosen)
{
    const auto& lib = state.getLibrary();
    if (! lib.isOpen())
    {
        if (state.status) state.status (state.getLibraryError());
        return;
    }
    juce::PopupMenu m;
    auto ids = std::make_shared<juce::StringArray>();
    for (auto& fam : lib.getFamilies())
    {
        juce::PopupMenu sub;
        bool any = false;
        for (auto& e : lib.getEntries())
        {
            if (e.family != fam || (drumsOnly && e.kind != "drums")) continue;
            ids->add (e.id);
            sub.addItem (ids->size(), e.name + (e.source == "hem" ? "  (modeled)" : ""), true, e.id == current);
            any = true;
        }
        if (any) m.addSubMenu (fam, sub);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                     [ids, chosen] (int r) { if (r > 0 && r <= ids->size()) chosen ((*ids)[r - 1]); });
}

//==============================================================================
class TrackList::Row final : public juce::Component
{
public:
    Row (TrackList& o, int t) : owner (o), index (t)
    {
        auto& apvts = owner.state.engine.getAPVTS();
        for (auto* b : { &mute, &solo })
        {
            b->getProperties().set (props::style, "ms");
            b->setClickingTogglesState (true);
            addAndMakeVisible (*b);
        }
        mute.getProperties().set (props::accent, (juce::int64) juce::Colour (0xffffb02e).getARGB());
        solo.getProperties().set (props::accent, (juce::int64) colours::hemGreen.getARGB());
        mute.setTooltip ("Mute this track");
        solo.setTooltip ("Solo: hear only the soloed tracks");
        muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, E::Ids::track (t, E::kMute), mute);
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, E::Ids::track (t, E::kSolo), solo);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4);
        const int s = juce::jmin (b.getHeight() / 2 - 1, juce::roundToInt (22.0f * owner.state.uiScale));
        auto right = b.removeFromRight (s * 2 + 10);
        right.removeFromRight (8);   // meter
        solo.setBounds (right.removeFromRight (s).withSizeKeepingCentre (s, s));
        right.removeFromRight (2);
        mute.setBounds (right.removeFromRight (s).withSizeKeepingCentre (s, s));
    }

    void paint (juce::Graphics& g) override
    {
        const float u = owner.state.uiScale;
        const auto& song = owner.state.getSong();
        if (index >= (int) song.tracks.size()) return;
        const auto& t = song.tracks[(size_t) index];
        const bool sel = owner.state.getSelectedTrack() == index;
        auto r = getLocalBounds().toFloat().reduced (2.0f, 1.5f);
        g.setColour (sel ? juce::Colour (0xff2c3440) : juce::Colour (0xff1d2128));
        g.fillRoundedRectangle (r, 4.0f);
        if (sel) { g.setColour (colours::glow.withAlpha (0.8f)); g.drawRoundedRectangle (r, 4.0f, 1.5f); }
        g.setColour (trackColour (index));
        g.fillRoundedRectangle (r.removeFromLeft (5.0f * u), 2.0f);
        r.removeFromLeft (6.0f * u);
        r.removeFromRight ((float) (getWidth() - mute.getX()) + 2.0f);
        auto top = r.removeFromTop (r.getHeight() * 0.55f);
        ForgeLookAndFeel::drawPanelText (g, juce::String (index + 1) + "  " + t.name, top, juce::Justification::bottomLeft, juce::jmin (16.0f * u, top.getHeight() * 0.8f));
        auto inst = owner.state.instrumentOf (index);
        juce::String sub = t.instrumentId.isEmpty() ? juce::String ("no instrument - right-click")
                         : owner.state.getLibrary().find (t.instrumentId) == nullptr ? "not installed: " + t.instrumentId
                         : inst == nullptr ? "loading " + owner.state.instrumentName (t.instrumentId) + "..."
                         : owner.state.instrumentName (t.instrumentId);
        ForgeLookAndFeel::drawPanelText (g, sub + "   " + juce::String ((int) t.notes.size()) + " notes", r, juce::Justification::topLeft,
                                         juce::jmin (12.0f * u, r.getHeight() * 0.8f), inst == nullptr && t.instrumentId.isNotEmpty() ? juce::Colour (0xffffb02e) : colours::paperDim);
        // meter
        const float pk = juce::jmax (owner.state.engine.getTrackPeak (index, 0), owner.state.engine.getTrackPeak (index, 1));
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
        const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto m = getLocalBounds().toFloat().removeFromRight (7.0f).reduced (1.0f, 5.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRect (m);
        g.setColour (db > -1.0f ? colours::meterRed : db > -12.0f ? juce::Colour (0xffffb02e) : colours::hemGreen);
        g.fillRect (m.withTop (m.getBottom() - m.getHeight() * f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.state.selectTrack (index);
        if (e.mods.isPopupMenu()) menu();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { rename(); }

    void rename()
    {
        const auto& song = owner.state.getSong();
        if (index >= (int) song.tracks.size()) return;
        auto* ed = new juce::TextEditor();
        ed->setText (song.tracks[(size_t) index].name, false);
        ed->setFont (ForgeLookAndFeel::font (16.0f, false));
        ed->setBounds (getLocalBounds().reduced (10, getHeight() / 5).withWidth (juce::jmax (100, mute.getX() - 14)));
        addAndMakeVisible (ed);
        ed->grabKeyboardFocus();
        ed->selectAll();
        juce::Component::SafePointer<Row> safe (this);
        auto finish = [safe, ed] (bool apply)
        {
            if (safe == nullptr) return;
            const auto text = ed->getText();
            juce::MessageManager::callAsync ([safe, ed, text, apply]
            {
                if (safe == nullptr) return;
                std::unique_ptr<juce::TextEditor> owned (ed);
                safe->removeChildComponent (ed);
                if (apply) safe->owner.state.renameTrack (safe->index, text);
            });
        };
        ed->onReturnKey = [finish] { finish (true); };
        ed->onEscapeKey = [finish] { finish (false); };
        ed->onFocusLost = [finish] { finish (true); };
    }

    void menu()
    {
        auto& st = owner.state;
        const auto& song = st.getSong();
        if (index >= (int) song.tracks.size()) return;
        const auto& t = song.tracks[(size_t) index];
        juce::PopupMenu m;
        m.addItem (1, "Change instrument...");
        m.addItem (2, "Rename...");
        m.addItem (3, "Duplicate");
        m.addItem (4, "Clear notes", ! t.notes.empty());
        m.addItem (5, "New take of this part (band)", t.bandRole >= 0 && ! song.bandRequest.isVoid());
        m.addSeparator();
        m.addItem (6, "Remove track");
        juce::Component::SafePointer<Row> safe (this);
        const int i = index;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, i] (int r)
        {
            if (safe == nullptr) return;
            auto& s = safe->owner.state;
            switch (r)
            {
                case 1: showInstrumentMenu (s, *safe, s.getSong().tracks[(size_t) i].instrumentId, false,
                                            [&s, i] (const juce::String& id) { s.setTrackInstrument (i, id); }); break;
                case 2: safe->rename(); break;
                case 3: s.duplicateTrack (i); break;
                case 4: { auto song2 = s.getSong(); song2.tracks[(size_t) i].notes.clear(); s.changeSong (song2, "Clear notes"); break; }
                case 5:
                {
                    auto song2 = s.getSong();
                    if (bandbridge::rerollTrack (song2, i, juce::Random::getSystemRandom().nextInt() | 1, s.getLibrary()))
                        s.changeSong (song2, "New take of " + song2.tracks[(size_t) i].name);
                    break;
                }
                case 6: s.removeTrack (i); break;
                default: break;
            }
        });
    }

    TrackList& owner;
    const int index;
    juce::TextButton mute { "M" }, solo { "S" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
};

//==============================================================================
TrackList::TrackList (AppState& s) : state (s)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    for (auto* b : { &addButton, &dupButton, &removeButton }) { styleDarkButton (*b); addAndMakeVisible (*b); }
    addButton.setTooltip ("Add a track and pick its instrument");
    addButton.onClick = [this]
    {
        showInstrumentMenu (state, addButton, {}, false, [this] (const juce::String& id) { state.addTrack (id, {}); });
    };
    dupButton.onClick = [this] { state.duplicateTrack (state.getSelectedTrack()); };
    removeButton.onClick = [this] { state.removeTrack (state.getSelectedTrack()); };
    state.addListener (this);
    rebuild();
    startTimerHz (15);
}

TrackList::~TrackList() { state.removeListener (this); }

void TrackList::rebuild()
{
    const int n = (int) state.getSong().tracks.size();
    if (rowComps.size() != n)
    {
        rowComps.clear();
        for (int t = 0; t < n; ++t) holder.addAndMakeVisible (rowComps.add (new Row (*this, t)));
    }
    removeButton.setEnabled (n > 0);
    dupButton.setEnabled (n > 0 && n < kMaxTracks);
    addButton.setEnabled (n < kMaxTracks);
    layoutRows();
    for (auto* r : rowComps) r->repaint();
}

void TrackList::layoutRows()
{
    const int rowH = juce::roundToInt (50.0f * state.uiScale);
    holder.setSize (viewport.getWidth() - (rowComps.size() * rowH > viewport.getHeight() ? viewport.getScrollBarThickness() : 0), rowComps.size() * rowH);
    for (int i = 0; i < rowComps.size(); ++i) rowComps[i]->setBounds (0, i * rowH, holder.getWidth(), rowH);
}

void TrackList::resized()
{
    auto b = getLocalBounds().reduced (6);
    const int bh = juce::roundToInt (28.0f * state.uiScale);
    auto bottom = b.removeFromBottom (bh);
    b.removeFromBottom (6);
    addButton.setBounds (bottom.removeFromLeft (bottom.getWidth() * 2 / 5).reduced (2, 0));
    dupButton.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (2, 0));
    removeButton.setBounds (bottom.reduced (2, 0));
    b.removeFromTop (juce::roundToInt (26.0f * state.uiScale));
    viewport.setBounds (b);
    layoutRows();
}

void TrackList::paint (juce::Graphics& g)
{
    heading (g, "TRACKS  (" + juce::String ((int) state.getSong().tracks.size()) + " / 32)",
             getLocalBounds().reduced (10, 6).removeFromTop (juce::roundToInt (24.0f * state.uiScale)).toFloat(), 17.0f * state.uiScale);
    if (state.getSong().tracks.empty())
        ForgeLookAndFeel::drawPanelText (g, "No tracks yet.\nWrite a song on the BAND tab,\nor press + ADD TRACK.", getLocalBounds().toFloat(),
                                         juce::Justification::centred, 14.0f * state.uiScale, colours::paperDim);
}

void TrackList::selectionChanged() { repaint(); for (auto* r : rowComps) r->repaint(); }
void TrackList::instrumentsChanged() { for (auto* r : rowComps) r->repaint(); }

void TrackList::timerCallback()
{
    if (! isShowing()) return;
    for (auto* r : rowComps) r->repaint (r->getLocalBounds().removeFromRight (10));
}

//==============================================================================
SongView::SongView (AppState& s) : state (s)
{
    state.addListener (this);
    startTimerHz (30);
}

SongView::~SongView() { state.removeListener (this); }

juce::Rectangle<int> SongView::ruler() const { return getLocalBounds().removeFromTop (juce::roundToInt (22.0f * state.uiScale)); }

double SongView::beatsShown() const
{
    const auto& s = state.getSong();
    return juce::jmax ((double) s.beatsPerBar * 8.0, s.lengthBeats());
}

float SongView::xOf (double beat) const { return (float) (beat / beatsShown() * getWidth()); }
double SongView::beatAt (float x) const { return juce::jlimit (0.0, beatsShown(), (double) x / juce::jmax (1, getWidth()) * beatsShown()); }

void SongView::paint (juce::Graphics& g)
{
    const float u = state.uiScale;
    const auto& s = state.getSong();
    g.fillAll (juce::Colour (0xff1d2128));
    const auto rl = ruler();
    g.setColour (juce::Colour (0xff111318));
    g.fillRect (rl);
    const int bars = (int) std::ceil (beatsShown() / s.beatsPerBar);
    const int step = bars > 64 ? 8 : bars > 24 ? 4 : 1;
    g.setFont (ForgeLookAndFeel::font (12.0f * u, false));
    for (int b = 0; b <= bars; ++b)
    {
        const float x = xOf ((double) b * s.beatsPerBar);
        g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.16f : 0.06f));
        g.fillRect (x, (float) rl.getBottom(), 1.0f, (float) (getHeight() - rl.getBottom()));
        if (b % step == 0 && b < bars)
        {
            g.setColour (colours::paperDim);
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 3.0f, 0.0f, 40.0f, (float) rl.getHeight()), juce::Justification::centredLeft, false);
        }
    }
    if (state.engine.isLooping())
    {
        const float a = xOf (state.engine.getLoopStart()), e = xOf (state.engine.getLoopEnd());
        g.setColour (colours::glow.withAlpha (0.45f));
        g.fillRect (a, 2.0f, e - a, (float) rl.getHeight() - 4.0f);
        g.setColour (colours::glow.withAlpha (0.05f));
        g.fillRect (a, (float) rl.getBottom(), e - a, (float) (getHeight() - rl.getBottom()));
    }
    const int n = (int) s.tracks.size();
    if (n == 0)
    {
        ForgeLookAndFeel::drawPanelText (g, "Empty song. Go to BAND and press WRITE THE SONG, or add a track on the left.",
                                         getLocalBounds().toFloat(), juce::Justification::centred, 18.0f * u);
        return;
    }
    const float laneH = juce::jmin (60.0f * u, (float) (getHeight() - rl.getBottom()) / (float) n);
    for (int t = 0; t < n; ++t)
    {
        const auto& tr = s.tracks[(size_t) t];
        const juce::Rectangle<float> lane (0.0f, (float) rl.getBottom() + (float) t * laneH, (float) getWidth(), laneH);
        if (t == state.getSelectedTrack()) { g.setColour (colours::glow.withAlpha (0.10f)); g.fillRect (lane); }
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (lane.getX(), lane.getBottom() - 1.0f, lane.getWidth(), 1.0f);
        int lo = 127, hi = 0;
        for (auto& no : tr.notes) { lo = juce::jmin (lo, no.pitch); hi = juce::jmax (hi, no.pitch); }
        const int span = juce::jmax (12, hi - lo + 1);
        const auto col = trackColour (t);
        const auto body = lane.reduced (0.0f, 3.0f);
        for (auto& no : tr.notes)
        {
            const float x0 = xOf (no.start), x1 = juce::jmax (x0 + 1.5f, xOf (no.end()));
            const float y = body.getBottom() - body.getHeight() * (float) (no.pitch - lo + 1) / (float) (span + 1);
            g.setColour (col.withAlpha (0.4f + 0.6f * (float) no.velocity / 127.0f));
            g.fillRect (x0, y, x1 - x0, juce::jmax (1.5f, body.getHeight() / (float) (span + 1)));
        }
        g.setColour (colours::paper.withAlpha (0.8f));
        g.setFont (ForgeLookAndFeel::font (juce::jmin (13.0f * u, laneH * 0.4f), false));
        g.drawText (tr.name, lane.reduced (6.0f, 2.0f), juce::Justification::topLeft, true);
    }
    const float px = xOf (state.engine.getPositionBeats());
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.fillRect (px, 0.0f, 1.5f, (float) getHeight());
}

void SongView::mouseDown (const juce::MouseEvent& e)
{
    if (ruler().contains (e.getPosition()))
    {
        rulerDrag = true;
        rulerStart = std::round (beatAt (e.position.x));
        return;
    }
    const auto& s = state.getSong();
    const int n = (int) s.tracks.size();
    if (n == 0) return;
    const float laneH = juce::jmin (60.0f * state.uiScale, (float) (getHeight() - ruler().getBottom()) / (float) n);
    const int t = (int) ((e.position.y - (float) ruler().getBottom()) / laneH);
    if (t >= 0 && t < n) state.selectTrack (t);
}

void SongView::mouseDrag (const juce::MouseEvent& e)
{
    if (! rulerDrag || std::abs (e.getDistanceFromDragStartX()) < 5) return;
    const double bar = state.getSong().beatsPerBar;
    const double b = std::round (beatAt (e.position.x) / bar) * bar, a = std::round (rulerStart / bar) * bar;
    if (std::abs (b - a) >= bar) state.engine.setLoop (true, juce::jmin (a, b), juce::jmax (a, b));
    repaint();
}

void SongView::mouseUp (const juce::MouseEvent& e)
{
    if (rulerDrag && std::abs (e.getDistanceFromDragStartX()) < 5) state.engine.setPositionBeats (rulerStart);
    rulerDrag = false;
}

void SongView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (ruler().contains (e.getPosition())) { state.engine.setLoop (false, state.engine.getLoopStart(), state.engine.getLoopEnd()); return; }
    if (onEditNotes) onEditNotes();
}

void SongView::timerCallback()
{
    const double p = state.engine.getPositionBeats();
    if (std::abs (p - lastPos) > 1.0e-6 && isShowing()) { lastPos = p; repaint(); }
}

//==============================================================================
TracksPage::TracksPage (AppState& s) : state (s), trackList (s), songView (s), roll (s)
{
    addAndMakeVisible (songView);
    addChildComponent (roll);
    songView.onEditNotes = [this] { showNotes (true); };
    for (auto* b : { &songButton, &notesButton, &quantizeButton, &fitButton, &undoButton, &redoButton, &rerollButton, &clearButton, &instButton })
    {
        styleDarkButton (*b);
        addAndMakeVisible (*b);
    }
    songButton.setClickingTogglesState (false);
    songButton.onClick = [this] { showNotes (false); };
    notesButton.onClick = [this] { showNotes (true); };
    songButton.setTooltip ("All tracks over the bars (double-click a lane to edit its notes)");
    notesButton.setTooltip ("Edit the notes of the selected track");
    quantizeButton.setTooltip ("Move the selected notes (or all) onto the grid (Q)");
    quantizeButton.onClick = [this] { showNotes (true); roll.quantizeSelection(); };
    fitButton.onClick = [this] { roll.fitToNotes(); };
    undoButton.onClick = [this] { state.undoLast(); };
    redoButton.onClick = [this] { state.redoLast(); };
    rerollButton.setTooltip ("Let the band play this part again, differently (same chords)");
    rerollButton.onClick = [this]
    {
        auto song = state.getSong();
        const int t = state.getSelectedTrack();
        if (t < (int) song.tracks.size() && bandbridge::rerollTrack (song, t, juce::Random::getSystemRandom().nextInt() | 1, state.getLibrary()))
            state.changeSong (song, "New take of " + song.tracks[(size_t) t].name);
    };
    clearButton.onClick = [this]
    {
        auto song = state.getSong();
        const int t = state.getSelectedTrack();
        if (t < (int) song.tracks.size()) { song.tracks[(size_t) t].notes.clear(); state.changeSong (song, "Clear notes"); }
    };
    instButton.setTooltip ("Pick the instrument of the selected track");
    instButton.onClick = [this]
    {
        const int t = state.getSelectedTrack();
        if (t >= (int) state.getSong().tracks.size()) return;
        showInstrumentMenu (state, instButton, state.getSong().tracks[(size_t) t].instrumentId, false,
                            [this, t] (const juce::String& id) { state.setTrackInstrument (t, id); });
    };

    styleDarkCombo (snapBox);
    snapBox.addItemList ({ "Grid: 1 bar", "Grid: 1/2", "Grid: 1/4", "Grid: 1/8", "Grid: 1/16", "Grid: 1/32", "Grid: 1/8 triplet", "Grid: 1/16 triplet", "Grid: off" }, 1);
    snapBox.setSelectedId (4, juce::dontSendNotification);   // 1/8 note = half a beat... see table below
    snapBox.onChange = [this]
    {
        static const double v[] { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0 };
        roll.snap = v[juce::jlimit (0, 8, snapBox.getSelectedId() - 1)];
        roll.repaint();
    };
    snapBox.setSelectedId (5, juce::sendNotificationSync);   // 1/16
    snapBox.setTooltip ("Where new and moved notes land");
    styleDarkCombo (velBox);
    for (int v : { 40, 64, 80, 100, 115, 127 }) velBox.addItem ("New notes: velocity " + juce::String (v), v);
    velBox.setSelectedId (100, juce::dontSendNotification);
    velBox.onChange = [this] { roll.newVelocity = velBox.getSelectedId(); };
    addAndMakeVisible (snapBox);
    addAndMakeVisible (velBox);
    state.addListener (this);
    showNotes (false);
}

TracksPage::~TracksPage() { state.removeListener (this); }

void TracksPage::showNotes (bool notes)
{
    notesMode = notes;
    songView.setVisible (! notes);
    roll.setVisible (notes);
    if (notes) { roll.fitToNotes(); roll.grabKeyboardFocus(); }
    songButton.setToggleState (! notes, juce::dontSendNotification);
    notesButton.setToggleState (notes, juce::dontSendNotification);
    updateButtons();
    resized();
}

void TracksPage::updateButtons()
{
    const auto& s = state.getSong();
    const int t = state.getSelectedTrack();
    const bool has = t < (int) s.tracks.size();
    rerollButton.setEnabled (has && s.tracks[(size_t) t].bandRole >= 0 && ! s.bandRequest.isVoid());
    clearButton.setEnabled (has && ! s.tracks[(size_t) t].notes.empty());
    instButton.setEnabled (has);
    notesButton.setEnabled (has);
    quantizeButton.setEnabled (has);
    for (auto* c : { (juce::Component*) &snapBox, (juce::Component*) &velBox, (juce::Component*) &quantizeButton, (juce::Component*) &fitButton })
        c->setVisible (notesMode);
    undoButton.setEnabled (state.getUndo().canUndo());
    redoButton.setEnabled (state.getUndo().canRedo());
    undoButton.setTooltip (state.getUndo().canUndo() ? "Undo: " + state.getUndo().getUndoDescription() + "  (Ctrl+Z)" : juce::String ("Nothing to undo"));
    redoButton.setTooltip (state.getUndo().canRedo() ? "Redo: " + state.getUndo().getRedoDescription() + "  (Ctrl+Y)" : juce::String ("Nothing to redo"));
}

void TracksPage::resized()
{
    const float u = state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (8.0f * u));
    auto bar = b.removeFromTop (juce::roundToInt (30.0f * u));
    b.removeFromTop (juce::roundToInt (6.0f * u));
    auto w = [u] (float px) { return juce::roundToInt (px * u); };
    songButton.setBounds (bar.removeFromLeft (w (70)));
    bar.removeFromLeft (w (3));
    notesButton.setBounds (bar.removeFromLeft (w (70)));
    bar.removeFromLeft (w (12));
    instButton.setBounds (bar.removeFromLeft (w (100)));
    bar.removeFromLeft (w (4));
    rerollButton.setBounds (bar.removeFromLeft (w (180)));
    bar.removeFromLeft (w (4));
    clearButton.setBounds (bar.removeFromLeft (w (100)));
    redoButton.setBounds (bar.removeFromRight (w (62)));
    bar.removeFromRight (w (3));
    undoButton.setBounds (bar.removeFromRight (w (62)));
    bar.removeFromRight (w (12));
    if (notesMode)
    {
        fitButton.setBounds (bar.removeFromRight (w (46)));
        bar.removeFromRight (w (4));
        quantizeButton.setBounds (bar.removeFromRight (w (86)));
        bar.removeFromRight (w (4));
        velBox.setBounds (bar.removeFromRight (w (178)));
        bar.removeFromRight (w (4));
        snapBox.setBounds (bar.removeFromRight (w (130)));
    }
    songView.setBounds (b);
    roll.setBounds (b);
}

void TracksPage::paint (juce::Graphics&) {}
} // namespace hf::ui
