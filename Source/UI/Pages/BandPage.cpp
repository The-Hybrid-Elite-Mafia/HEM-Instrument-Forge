#include "UI/Pages/BandPage.h"
#include "UI/Pages/TracksPage.h"

namespace hf::ui
{
namespace band = pg::band;
using E = ForgeEngine;

BandPage::BandPage (AppState& s) : state (s)
{
    seed = (juce::uint32) juce::jmax (1, s.settings.getIntValue ("band.seed", (int) (juce::Time::currentTimeMillis() & 0x7fffffff)));
    for (auto* c : { &genre, &key, &tempo, &length })
    {
        styleDarkCombo (*c);
        side.addAndMakeVisible (*c);
    }
    for (int g = 0; g < band::kNumGenres; ++g) genre.addItem (band::genreName ((band::Genre) g), g + 1);
    for (int k = 0; k < 12; ++k) key.addItem (juce::String ("Key of ") + band::theory::keyName (k), k + 1);
    tempo.addItem ("Tempo: the style's own", 1);
    for (int b = 60; b <= 180; b += 4) tempo.addItem (juce::String (b) + " BPM", b);
    length.addItem ("Full song (about 3 minutes)", 1);
    length.addItem ("Short (about 1 minute)", 2);

    genre.setSelectedId (s.settings.getIntValue ("band.genre", 1), juce::dontSendNotification);
    key.setSelectedId (s.settings.getIntValue ("band.key", 8), juce::dontSendNotification);
    tempo.setSelectedId (s.settings.getIntValue ("band.tempo", 1), juce::dontSendNotification);
    length.setSelectedId (s.settings.getIntValue ("band.length", 1), juce::dontSendNotification);
    minor.setToggleState (s.settings.getBoolValue ("band.minor", false), juce::dontSendNotification);
    minor.getProperties().set (props::accent, (juce::int64) colours::glow.getARGB());
    side.addAndMakeVisible (minor);
    genre.onChange = [this] { genreChanged(); saveChoices(); };
    for (auto* c : { &key, &tempo, &length }) c->onChange = [this] { saveChoices(); };
    minor.onClick = [this] { saveChoices(); };

    writeButton.setTooltip ("The band writes a new song in this style and it lands as tracks (Undo brings the old song back)");
    writeButton.onClick = [this] { write (false); };
    againButton.setTooltip ("Same choices, a different song");
    againButton.onClick = [this] { write (true); };
    sunoButton.setTooltip ("Copies a style line for Suno to the clipboard");
    sunoButton.onClick = [this]
    {
        const auto text = bandbridge::sunoStyle (state.getSong(), state.getLibrary());
        juce::SystemClipboard::copyTextToClipboard (text);
        if (state.status) state.status ("Copied: " + text);
    };
    balanceButton.setTooltip ("Set every track's level so the parts sit right in the mix (measured, not guessed)");
    balanceButton.onClick = [this] { state.balanceTracks(); };
    for (auto* b : { &writeButton, &againButton, &balanceButton, &sunoButton }) addAndMakeVisible (*b);
    writeButton.getProperties().set (props::accent, (juce::int64) colours::hemGreen.getARGB());
    state.addListener (this);
    genreChanged();
}

BandPage::~BandPage() { state.removeListener (this); }

void BandPage::genreChanged()
{
    for (auto& p : parts) { removeChildComponent (p.on.get()); removeChildComponent (p.pick.get()); }
    parts.clear();
    const auto g = (band::Genre) juce::jlimit (0, band::kNumGenres - 1, genre.getSelectedId() - 1);
    const auto defaults = band::defaultInstruments (g);
    for (auto role : band::availableInstruments (g))
    {
        PartRow r;
        r.role = role;
        r.on = std::make_unique<juce::ToggleButton> (bandbridge::roleName (role));
        r.on->getProperties().set (props::accent, (juce::int64) colours::hemGreen.getARGB());
        const auto savedOn = state.settings.getValue ("band.on." + juce::String ((int) g) + "." + juce::String ((int) role));
        r.on->setToggleState (savedOn.isNotEmpty() ? savedOn == "1" : std::find (defaults.begin(), defaults.end(), role) != defaults.end(),
                              juce::dontSendNotification);
        r.on->onClick = [this] { saveChoices(); repaint(); };
        const auto saved = state.settings.getValue ("band.inst." + juce::String ((int) role));
        r.instrumentId = saved.isNotEmpty() && state.getLibrary().find (saved) != nullptr ? saved
                       : bandbridge::defaultInstrument (role, g, state.getLibrary());
        r.pick = std::make_unique<juce::TextButton>();
        styleDarkButton (*r.pick);
        r.pick->setTooltip ("Which instrument plays this part");
        const auto roleCopy = role;
        auto* pickPtr = r.pick.get();
        r.pick->onClick = [this, roleCopy, pickPtr]
        {
            for (auto& p : parts)
                if (p.role == roleCopy)
                {
                    juce::PopupMenu m;
                    auto ids = std::make_shared<juce::StringArray>();
                    for (auto* e : bandbridge::candidates (roleCopy, state.getLibrary()))
                    {
                        ids->add (e->id);
                        m.addItem (ids->size(), e->name + "   (" + e->family + ")", true, e->id == p.instrumentId);
                    }
                    m.addSeparator();
                    m.addItem (10000, "Any instrument...");
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pickPtr), [this, roleCopy, ids, pickPtr] (int res)
                    {
                        auto set = [this, roleCopy] (const juce::String& id)
                        {
                            for (auto& q : parts) if (q.role == roleCopy) { q.instrumentId = id; q.pick->setButtonText (state.instrumentName (id)); }
                            state.settings.setValue ("band.inst." + juce::String ((int) roleCopy), id);
                        };
                        if (res == 10000) showInstrumentMenu (state, *pickPtr, {}, bandbridge::isDrumRole (roleCopy), set);
                        else if (res > 0 && res <= ids->size()) set ((*ids)[res - 1]);
                    });
                }
        };
        r.pick->setButtonText (r.instrumentId.isNotEmpty() ? state.instrumentName (r.instrumentId) : juce::String ("(nothing installed)"));
        addAndMakeVisible (*r.on);
        addAndMakeVisible (*r.pick);
        parts.push_back (std::move (r));
    }
    resized();
    repaint();
}

void BandPage::saveChoices()
{
    auto& st = state.settings;
    st.setValue ("band.genre", genre.getSelectedId());
    st.setValue ("band.key", key.getSelectedId());
    st.setValue ("band.tempo", tempo.getSelectedId());
    st.setValue ("band.length", length.getSelectedId());
    st.setValue ("band.minor", minor.getToggleState());
    const int g = genre.getSelectedId() - 1;
    for (auto& p : parts)
        st.setValue ("band.on." + juce::String (g) + "." + juce::String ((int) p.role), p.on->getToggleState() ? "1" : "0");
}

band::Request BandPage::makeRequest() const
{
    band::Request r;
    r.genre = (band::Genre) juce::jlimit (0, band::kNumGenres - 1, genre.getSelectedId() - 1);
    r.keyRoot = juce::jlimit (0, 11, key.getSelectedId() - 1);
    r.minor = minor.getToggleState();
    r.bpm = tempo.getSelectedId() > 1 ? (double) tempo.getSelectedId() : 0.0;
    r.fullSong = length.getSelectedId() != 2;
    r.seed = seed;
    for (auto& p : parts) if (p.on->getToggleState()) r.instruments.push_back (p.role);
    return r;
}

void BandPage::write (bool newSeed)
{
    if (! state.getLibrary().isOpen()) { if (state.status) state.status (state.getLibraryError()); return; }
    if (newSeed)
        seed = (juce::uint32) (juce::Random::getSystemRandom().nextInt (0x7ffffffe) + 1);
    state.settings.setValue ("band.seed", (int) seed);
    const auto req = makeRequest();
    if (req.instruments.empty()) { if (state.status) state.status ("Turn on at least one part."); return; }
    std::vector<bandbridge::RoleChoice> roles;
    juce::StringArray missing;
    for (auto& p : parts)
    {
        if (! p.on->getToggleState()) continue;
        if (p.instrumentId.isEmpty()) { missing.add (bandbridge::roleName (p.role)); continue; }
        roles.push_back ({ p.role, p.instrumentId, true });
    }
    if (roles.empty()) { if (state.status) state.status ("No installed instrument can play these parts."); return; }
    state.engine.stop();
    auto song = bandbridge::makeSong (req, roles, state.getLibrary());
    // every track starts from its instrument's seat and default settings
    const auto before = state.engine.getAPVTS().copyState();
    for (int t = 0; t < kMaxTracks; ++t)
        for (auto* w : { E::kLevel, E::kMute, E::kSolo, E::kWidth, E::kRoomSend, E::kArt, E::kHuman, E::kVelSens, E::kTranspose, E::kRelease })
            if (auto* p = state.engine.getAPVTS().getParameter (E::Ids::track (t, w))) p->setValueNotifyingHost (p->getDefaultValue());
    state.setParam (E::Ids::bpm, (float) song.bpm);
    const auto after = state.engine.getAPVTS().copyState();
    state.engine.getAPVTS().replaceState (before);
    for (int t = 0; t < (int) song.tracks.size(); ++t) state.wantDefaults (t);
    state.changeSongAndParameters (song, after, "Write the band");
    state.engine.setPositionBeats (0.0);
    state.selectTrack (0);
    state.balanceTracks();
    if (state.status)
        state.status ("The band wrote: " + song.bandInfo + "  -  " + juce::String ((int) song.tracks.size()) + " tracks, "
                      + juce::String ((int) std::ceil (song.lengthBeats() / song.beatsPerBar)) + " bars. Press play (Space)."
                      + (missing.isEmpty() ? juce::String() : "  Left out (no instrument): " + missing.joinIntoString (", ")));
    repaint();
}

//==============================================================================
void BandPage::resized()
{
    const float u = state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    b.removeFromTop (juce::roundToInt (34.0f * u));
    auto buttons = b.removeFromBottom (juce::roundToInt (40.0f * u));
    writeButton.setBounds (buttons.removeFromLeft (juce::roundToInt (230.0f * u)));
    buttons.removeFromLeft (juce::roundToInt (10.0f * u));
    againButton.setBounds (buttons.removeFromLeft (juce::roundToInt (190.0f * u)));
    buttons.removeFromLeft (juce::roundToInt (10.0f * u));
    balanceButton.setBounds (buttons.removeFromLeft (juce::roundToInt (190.0f * u)));
    sunoButton.setBounds (buttons.removeFromRight (juce::roundToInt (230.0f * u)));
    b.removeFromBottom (juce::roundToInt (70.0f * u));   // result text
    // parts in two columns
    const int rowH = juce::roundToInt (36.0f * u);
    const int perCol = juce::jmax (1, (int) std::ceil ((double) parts.size() / 2.0));
    const int colW = b.getWidth() / 2;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const int col = (int) i / perCol, row = (int) i % perCol;
        auto r = juce::Rectangle<int> (b.getX() + col * colW, b.getY() + row * (rowH + juce::roundToInt (6.0f * u)), colW - juce::roundToInt (20.0f * u), rowH);
        parts[i].on->setBounds (r.removeFromLeft (r.getWidth() * 45 / 100));
        parts[i].pick->setBounds (r.reduced (0, 2));
    }
}

void BandPage::paint (juce::Graphics& g)
{
    const float u = state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    heading (g, "THE BAND  -  pick the parts and who plays them", b.removeFromTop (juce::roundToInt (30.0f * u)).toFloat(), 20.0f * u);
    auto result = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    result.removeFromBottom (juce::roundToInt (48.0f * u));
    result = result.removeFromBottom (juce::roundToInt (64.0f * u));
    const auto& s = state.getSong();
    juce::String text;
    if (! state.getLibrary().isOpen())
        text = state.getLibraryError();
    else if (s.bandInfo.isNotEmpty())
        text = "Now loaded: " + s.bandInfo + "  -  " + juce::String ((int) s.tracks.size()) + " tracks.  Edit any part on TRACKS, move the players on STAGE.";
    else
        text = "Press WRITE THE SONG: every part becomes its own track, played by the instruments you picked.";
    g.setColour (juce::Colour (0xff15181d).withAlpha (0.8f));
    g.fillRoundedRectangle (result.toFloat(), 6.0f);
    ForgeLookAndFeel::drawPanelText (g, text, result.toFloat().reduced (12.0f * u, 4.0f), juce::Justification::centredLeft, 15.0f * u, colours::paper);
}

void BandPage::Side::paint (juce::Graphics& g)
{
    const float u = page.state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (12.0f * u));
    heading (g, "SONG", b.removeFromTop (juce::roundToInt (28.0f * u)).toFloat(), 18.0f * u);
    auto foot = getLocalBounds().reduced (juce::roundToInt (12.0f * u)).removeFromBottom (juce::roundToInt (120.0f * u)).toFloat();
    ForgeLookAndFeel::drawPanelText (g, "Every part is played by real recorded or modeled instruments from your HEM library. "
                                        "Nothing is a loop: the notes are written fresh each time.",
                                     foot, juce::Justification::bottomLeft, 12.5f * u, colours::paperDim);
}

void BandPage::Side::resized()
{
    const float u = page.state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (12.0f * u));
    b.removeFromTop (juce::roundToInt (34.0f * u));
    const int h = juce::roundToInt (32.0f * u), gap = juce::roundToInt (10.0f * u);
    page.genre.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    page.key.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    page.minor.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    page.tempo.setBounds (b.removeFromTop (h)); b.removeFromTop (gap);
    page.length.setBounds (b.removeFromTop (h));
}
} // namespace hf::ui
