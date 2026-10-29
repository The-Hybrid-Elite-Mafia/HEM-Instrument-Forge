#include "UI/Pages/InstrumentsPage.h"
#include "UI/Pages/TracksPage.h"

namespace hf::ui
{
using E = ForgeEngine;

InstrumentsPage::Side::Side (InstrumentsPage& p) : page (p)
{
    styleDarkCombo (family);
    family.onChange = [this] { refresh(); };
    addAndMakeVisible (family);
    search.setTextToShowWhenEmpty ("Search...", colours::paperDim);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);
    list.setModel (this);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff15181d));
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
}

void InstrumentsPage::Side::refresh()
{
    const auto& lib = page.state.getLibrary();
    const auto fam = family.getText();
    if (family.getNumItems() != lib.getFamilies().size() + 1)
    {
        family.clear (juce::dontSendNotification);
        family.addItem ("All families", 1);
        for (int i = 0; i < lib.getFamilies().size(); ++i) family.addItem (lib.getFamilies()[i], i + 2);
        family.setSelectedId (1, juce::dontSendNotification);
    }
    const auto chosen = family.getSelectedId() > 1 ? family.getText() : juce::String();
    const auto q = search.getText().trim();
    rows.clear();
    for (auto& e : lib.getEntries())
    {
        if (chosen.isNotEmpty() && e.family != chosen) continue;
        if (q.isNotEmpty() && ! e.name.containsIgnoreCase (q) && ! e.family.containsIgnoreCase (q)) continue;
        rows.add (e.id);
    }
    list.updateContent();
    list.repaint();
    juce::ignoreUnused (fam);
}

void InstrumentsPage::Side::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto* e = page.state.getLibrary().find (rows[row]);
    if (e == nullptr) return;
    const float u = page.state.uiScale;
    if (selected) { g.setColour (colours::glow.withAlpha (0.3f)); g.fillRect (0, 0, w, h); }
    g.setColour (colours::paper);
    g.setFont (ForgeLookAndFeel::font (14.0f * u, false));
    g.drawText (e->name, 8, 0, w - 16, h * 6 / 10, juce::Justification::bottomLeft, true);
    g.setColour (colours::paperDim);
    g.setFont (ForgeLookAndFeel::font (11.0f * u, false));
    g.drawText (e->family + (e->source == "hem" ? "  -  modeled" : "  -  recorded"), 8, h * 6 / 10, w - 16, h * 4 / 10, juce::Justification::topLeft, true);
}

void InstrumentsPage::Side::selectedRowsChanged (int row)
{
    if (row >= 0 && row < rows.size()) page.showInstrument (rows[row]);
}

void InstrumentsPage::Side::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < rows.size()) { page.showInstrument (rows[row]); page.putOnTrack (page.state.getSong().tracks.empty()); }
}

void InstrumentsPage::Side::paint (juce::Graphics& g)
{
    const float u = page.state.uiScale;
    heading (g, "INSTRUMENTS  (" + juce::String ((int) page.state.getLibrary().getEntries().size()) + ")",
             getLocalBounds().reduced (juce::roundToInt (12.0f * u)).removeFromTop (juce::roundToInt (26.0f * u)).toFloat(), 18.0f * u);
}

void InstrumentsPage::Side::resized()
{
    const float u = page.state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (12.0f * u));
    b.removeFromTop (juce::roundToInt (32.0f * u));
    family.setBounds (b.removeFromTop (juce::roundToInt (30.0f * u)));
    b.removeFromTop (juce::roundToInt (6.0f * u));
    search.setBounds (b.removeFromTop (juce::roundToInt (28.0f * u)));
    search.setFont (ForgeLookAndFeel::font (14.0f * u, false));
    b.removeFromTop (juce::roundToInt (6.0f * u));
    list.setRowHeight (juce::roundToInt (40.0f * u));
    list.setBounds (b);
}

//==============================================================================
InstrumentsPage::InstrumentsPage (AppState& s)
    : state (s),
      human (s.engine.getAPVTS(), "HUMANIZE"), velFeel (s.engine.getAPVTS(), "VELOCITY FEEL"),
      release (s.engine.getAPVTS(), "RELEASE"), transpose (s.engine.getAPVTS(), "TRANSPOSE"),
      mono (s.engine.getAPVTS(), "One note at a time", colours::glow), legato (s.engine.getAPVTS(), "Legato (slur)", colours::glow)
{
    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyPressBaseOctave (keyOctave);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::glow.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::glow.withAlpha (0.25f));
    keyboard.setWantsKeyboardFocus (true);
    keyState.addListener (this);
    addAndMakeVisible (keyboard);
    for (auto* b : { &putButton, &newButton, &octDown, &octUp }) { styleDarkButton (*b); addAndMakeVisible (*b); }
    putButton.getProperties().set (props::accent, (juce::int64) colours::hemGreen.getARGB());
    putButton.onClick = [this] { putOnTrack (false); };
    newButton.onClick = [this] { putOnTrack (true); };
    octDown.onClick = [this] { keyOctave = juce::jmax (1, keyOctave - 1); keyboard.setKeyPressBaseOctave (keyOctave); keyboard.setLowestVisibleKey (juce::jmax (21, keyboard.getLowestVisibleKey() - 12)); };
    octUp.onClick = [this] { keyOctave = juce::jmin (8, keyOctave + 1); keyboard.setKeyPressBaseOctave (keyOctave); keyboard.setLowestVisibleKey (juce::jmin (84, keyboard.getLowestVisibleKey() + 12)); };
    octDown.setTooltip ("Keyboard an octave down");
    octUp.setTooltip ("Keyboard an octave up");

    styleDarkCombo (articulation);
    articulation.setTooltip ("How the selected track plays (also switchable by notes C0 and up)");
    articulation.onChange = [this]
    {
        const int t = state.getSelectedTrack();
        if (t < (int) state.getSong().tracks.size() && articulation.getSelectedId() > 0)
            state.setParam (E::Ids::track (t, E::kArt), (float) (articulation.getSelectedId() - 1));
    };
    addAndMakeVisible (articulation);
    human.valueText = [] (double v) { return v < 0.01 ? juce::String ("EXACT") : juce::String (juce::roundToInt (v * 100)) + "% LOOSE"; };
    velFeel.valueText = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "% DYNAMICS"; };
    release.valueText = [] (double v) { return "x" + juce::String (v, 2) + " RING"; };
    transpose.valueText = [] (double v) { const int n = juce::roundToInt (v); return n == 0 ? juce::String ("NO SHIFT") : (n > 0 ? "+" : "") + juce::String (n) + " SEMI"; };
    for (auto* c : std::initializer_list<juce::Component*> { &human, &velFeel, &release, &transpose, &mono, &legato }) addAndMakeVisible (c);
    state.addListener (this);
    side.refresh();
    bindTrack();
    startTimerHz (10);
}

InstrumentsPage::~InstrumentsPage()
{
    keyState.removeListener (this);
    state.removeListener (this);
}

void InstrumentsPage::showInstrument (const juce::String& id)
{
    shownId = id;
    const int row = side.rows.indexOf (id);
    if (row >= 0 && side.list.getSelectedRow() != row) side.list.selectRow (row, true, true);
    repaint();
}

void InstrumentsPage::putOnTrack (bool asNew)
{
    if (shownId.isEmpty()) { if (state.status) state.status ("Pick an instrument on the left first."); return; }
    const int t = state.getSelectedTrack();
    if (asNew || t >= (int) state.getSong().tracks.size()) state.addTrack (shownId, {});
    else state.setTrackInstrument (t, shownId);
    if (state.status) state.status (state.instrumentName (shownId) + " is on track " + juce::String (state.getSelectedTrack() + 1) + ". Play it on the keyboard below.");
    keyboard.grabKeyboardFocus();
}

void InstrumentsPage::bindTrack()
{
    const int t = state.getSelectedTrack();
    const bool has = t < (int) state.getSong().tracks.size();
    for (auto* c : std::initializer_list<juce::Component*> { &human, &velFeel, &release, &transpose, &mono, &legato, &articulation, &putButton })
        c->setEnabled (has);
    if (! has) { boundTrack = -1; return; }
    if (t != boundTrack)
    {
        boundTrack = t;
        human.bind (E::Ids::track (t, E::kHuman));
        velFeel.bind (E::Ids::track (t, E::kVelSens));
        release.bind (E::Ids::track (t, E::kRelease));
        transpose.bind (E::Ids::track (t, E::kTranspose));
        mono.bind (E::Ids::track (t, E::kMono));
        legato.bind (E::Ids::track (t, E::kLegato));
    }
    auto inst = state.instrumentOf (t);
    const auto id = inst != nullptr ? inst->getDef().id : juce::String();
    if (id != boundInstrument || articulation.getNumItems() == 0)
    {
        boundInstrument = id;
        articulation.clear (juce::dontSendNotification);
        if (inst != nullptr)
            for (int a = 0; a < inst->numArticulations() && a < 16; ++a)
                articulation.addItem ("Plays: " + inst->getDef().articulations[(size_t) a].name, a + 1);
        else
            articulation.addItem ("Plays: (instrument loading)", 1);
        articulation.setSelectedId ((int) state.param (E::Ids::track (t, E::kArt)) + 1, juce::dontSendNotification);
        if (shownId.isEmpty() && id.isNotEmpty()) showInstrument (id);
    }
    repaint();
}

void InstrumentsPage::timerCallback()
{
    const int t = state.getSelectedTrack();
    if (t < (int) state.getSong().tracks.size())
    {
        const int a = (int) state.param (E::Ids::track (t, E::kArt)) + 1;
        if (a != articulation.getSelectedId() && a <= articulation.getNumItems()) articulation.setSelectedId (a, juce::dontSendNotification);
    }
}

void InstrumentsPage::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    // message thread (mouse / computer keys on the on-screen keyboard)
    if (state.getSong().tracks.empty()) return;
    state.engine.uiNoteOn (note, juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f)));
}

void InstrumentsPage::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    state.engine.uiNoteOff (note);
}

void InstrumentsPage::resized()
{
    const float u = state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    auto keys = b.removeFromBottom (juce::roundToInt (110.0f * u));
    octDown.setBounds (keys.removeFromLeft (juce::roundToInt (30.0f * u)).withSizeKeepingCentre (juce::roundToInt (30.0f * u), juce::roundToInt (40.0f * u)));
    octUp.setBounds (keys.removeFromRight (juce::roundToInt (30.0f * u)).withSizeKeepingCentre (juce::roundToInt (30.0f * u), juce::roundToInt (40.0f * u)));
    keyboard.setBounds (keys.reduced (juce::roundToInt (6.0f * u), 0));
    keyboard.setKeyWidth (juce::jmax (12.0f, (float) keyboard.getWidth() / 36.0f));
    b.removeFromBottom (juce::roundToInt (10.0f * u));
    auto knobs = b.removeFromBottom (juce::roundToInt (118.0f * u));
    auto toggles = knobs.removeFromRight (juce::roundToInt (230.0f * u));
    const int kw = juce::roundToInt (118.0f * u);
    for (auto* k : { &human, &velFeel, &release, &transpose }) { k->setBounds (knobs.removeFromLeft (kw)); knobs.removeFromLeft (juce::roundToInt (8.0f * u)); }
    toggles = toggles.withSizeKeepingCentre (toggles.getWidth(), juce::roundToInt (100.0f * u));
    articulation.setBounds (toggles.removeFromTop (juce::roundToInt (30.0f * u)));
    toggles.removeFromTop (juce::roundToInt (6.0f * u));
    mono.setBounds (toggles.removeFromTop (juce::roundToInt (28.0f * u)));
    toggles.removeFromTop (juce::roundToInt (4.0f * u));
    legato.setBounds (toggles.removeFromTop (juce::roundToInt (28.0f * u)));
    // buttons under the details
    auto top = b.removeFromTop (juce::roundToInt (210.0f * u));
    auto buttons = top.removeFromBottom (juce::roundToInt (36.0f * u));
    putButton.setBounds (buttons.removeFromLeft (juce::roundToInt (240.0f * u)));
    buttons.removeFromLeft (juce::roundToInt (10.0f * u));
    newButton.setBounds (buttons.removeFromLeft (juce::roundToInt (200.0f * u)));
}

void InstrumentsPage::paint (juce::Graphics& g)
{
    const float u = state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    const auto* e = state.getLibrary().find (shownId);
    if (! state.getLibrary().isOpen())
    {
        ForgeLookAndFeel::drawPanelText (g, state.getLibraryError(), b.toFloat(), juce::Justification::centred, 16.0f * u);
        return;
    }
    auto top = b.removeFromTop (juce::roundToInt (170.0f * u)).toFloat();
    if (e == nullptr)
    {
        ForgeLookAndFeel::drawPanelText (g, "Pick an instrument on the left. Double-click puts it on the selected track.", top, juce::Justification::centredLeft, 17.0f * u);
    }
    else
    {
        ForgeLookAndFeel::drawPanelText (g, e->name, top.removeFromTop (40.0f * u), juce::Justification::centredLeft, 30.0f * u, juce::Colours::white);
        juce::String sizeText = e->bytes > 0 ? juce::String ((double) e->bytes / 1.0e6, 0) + " MB" : juce::String();
        ForgeLookAndFeel::drawPanelText (g, e->family + "   -   " + (e->source == "hem" ? "modeled by HEM (no recordings)" : "recorded instrument")
                                             + "   -   range " + noteName (e->rangeLo) + " to " + noteName (e->rangeHi)
                                             + "   -   " + juce::String (e->zones) + " samples   " + sizeText,
                                         top.removeFromTop (26.0f * u), juce::Justification::centredLeft, 14.0f * u, colours::paperDim);
        ForgeLookAndFeel::drawPanelText (g, "Plays: " + e->articulations.joinIntoString (",  "), top.removeFromTop (26.0f * u),
                                         juce::Justification::centredLeft, 14.0f * u, colours::paper);
        const juce::String credit = e->source == "vcsl" ? "Recorded by Versilian Studios and contributors (Versilian Community Sample Library, public domain)."
                                  : e->source == "vsco" ? "Recorded by Sam Gossner and Simon Dalzell (VS Chamber Orchestra 2 Community Edition, public domain)."
                                  : e->source == "vd"   ? "Played by Austin McMahon (Virtuosity Drums, public domain)."
                                  : e->source == "hem"  ? "Original HEM model: synthesized by code, then sampled into the library."
                                                        : juce::String();
        ForgeLookAndFeel::drawPanelText (g, (e->licence.isNotEmpty() ? "Licence: " + e->licence + ".  " : juce::String()) + credit,
                                         top.removeFromTop (40.0f * u), juce::Justification::topLeft, 12.0f * u, colours::paperDim);
    }
    // the selected track
    const int t = state.getSelectedTrack();
    const auto& song = state.getSong();
    auto line = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    line.removeFromBottom (juce::roundToInt (110.0f * u + 10.0f * u + 118.0f * u));
    line = line.removeFromBottom (juce::roundToInt (26.0f * u));
    juce::String trackText = t < (int) song.tracks.size()
        ? "SELECTED TRACK " + juce::String (t + 1) + ":  " + song.tracks[(size_t) t].name + "   (" + (state.instrumentOf (t) != nullptr
              ? state.instrumentName (song.tracks[(size_t) t].instrumentId) : state.loadingText().isNotEmpty() ? state.loadingText() : juce::String ("no instrument")) + ")   -   the keyboard plays this track"
        : juce::String ("No track yet: ADD AS A NEW TRACK, then play the keyboard (mouse, or keys A W S E D F T G Y H U J K)");
    heading (g, trackText, line.toFloat(), 15.0f * u);
}
} // namespace hf::ui
