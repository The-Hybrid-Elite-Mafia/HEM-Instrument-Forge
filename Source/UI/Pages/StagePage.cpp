#include "UI/Pages/StagePage.h"
#include "UI/Pages/TracksPage.h"
#include "Stage/Stage.h"

namespace hf::ui
{
using E = ForgeEngine;

StagePage::Side::Side (StagePage& p)
    : page (p),
      size (p.state.engine.getAPVTS(), "ROOM SIZE"), level (p.state.engine.getAPVTS(), "ROOM LEVEL"),
      mics (p.state.engine.getAPVTS(), "MIC DISTANCE"), master (p.state.engine.getAPVTS(), "MASTER", colours::hemGreen),
      phones (p.state.engine.getAPVTS(), "Headphone 3D", colours::glow), click (p.state.engine.getAPVTS(), "Click", juce::Colour (0xffffb02e))
{
    styleDarkCombo (room);
    room.addItemList (roomNames(), 1);
    roomAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (page.state.engine.getAPVTS(), E::Ids::room, room);
    room.setTooltip ("The room everyone plays in (simulated: walls, echoes and decay are computed, not recorded)");
    addAndMakeVisible (room);
    size.bind (E::Ids::roomSize);
    level.bind (E::Ids::reverb);
    mics.bind (E::Ids::mics);
    master.bind (E::Ids::master);
    mics.valueText = [] (double v) { return v < 0.2 ? juce::String ("CLOSE MICS") : v > 0.75 ? juce::String ("FAR (ROOM) MICS") : juce::String (juce::roundToInt (v * 100)) + "% AWAY"; };
    size.valueText = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "% SIZE"; };
    phones.bind (E::Ids::headphones);
    click.bind (E::Ids::click);
    phones.setTooltip ("For headphones: each player is placed around your head (ear timing and head shadow). Turn off for speakers.");
    click.setTooltip ("Metronome while playing and recording");
    for (auto* c : std::initializer_list<juce::Component*> { &size, &level, &mics, &master, &phones, &click }) addAndMakeVisible (c);
}

void StagePage::Side::paint (juce::Graphics& g)
{
    const float u = page.state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (12.0f * u));
    heading (g, "ROOM", b.removeFromTop (juce::roundToInt (28.0f * u)).toFloat(), 18.0f * u);
}

void StagePage::Side::resized()
{
    const float u = page.state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (12.0f * u));
    b.removeFromTop (juce::roundToInt (32.0f * u));
    room.setBounds (b.removeFromTop (juce::roundToInt (32.0f * u)));
    b.removeFromTop (juce::roundToInt (12.0f * u));
    const int kh = juce::jmin (b.getWidth() / 2, juce::roundToInt (118.0f * u));
    auto row1 = b.removeFromTop (kh);
    size.setBounds (row1.removeFromLeft (row1.getWidth() / 2));
    level.setBounds (row1);
    b.removeFromTop (juce::roundToInt (6.0f * u));
    auto row2 = b.removeFromTop (kh);
    mics.setBounds (row2.removeFromLeft (row2.getWidth() / 2));
    master.setBounds (row2);
    b.removeFromTop (juce::roundToInt (12.0f * u));
    phones.setBounds (b.removeFromTop (juce::roundToInt (30.0f * u)));
    b.removeFromTop (juce::roundToInt (6.0f * u));
    click.setBounds (b.removeFromTop (juce::roundToInt (30.0f * u)));
}

//==============================================================================
StagePage::StagePage (AppState& s)
    : state (s),
      trackLevel (s.engine.getAPVTS(), "LEVEL", colours::hemGreen), trackHeight (s.engine.getAPVTS(), "HEIGHT"),
      trackWidth (s.engine.getAPVTS(), "WIDTH"), trackRoom (s.engine.getAPVTS(), "ROOM SEND")
{
    for (auto* k : { &trackLevel, &trackHeight, &trackWidth, &trackRoom }) addAndMakeVisible (k);
    trackHeight.valueText = [] (double v) { return v < 0.15 ? juce::String ("ON THE FLOOR") : v > 0.7 ? juce::String ("UP HIGH") : juce::String (juce::roundToInt (v * 100)) + "% UP"; };
    trackWidth.valueText = [] (double v) { return v < 0.05 ? juce::String ("POINT (MONO)") : juce::String (juce::roundToInt (v * 100)) + "% WIDE"; };
    state.addListener (this);
    bindTrack();
    startTimerHz (20);
}

StagePage::~StagePage() { state.removeListener (this); }

void StagePage::bindTrack()
{
    const int t = state.getSelectedTrack();
    const bool has = t < (int) state.getSong().tracks.size();
    for (auto* k : { &trackLevel, &trackHeight, &trackWidth, &trackRoom }) k->setEnabled (has);
    if (! has || t == boundTrack) return;
    boundTrack = t;
    trackLevel.bind (E::Ids::track (t, E::kLevel));
    trackHeight.bind (E::Ids::track (t, E::kHeight));
    trackWidth.bind (E::Ids::track (t, E::kWidth));
    trackRoom.bind (E::Ids::track (t, E::kRoomSend));
}

juce::Rectangle<float> StagePage::stageArea() const
{
    const float u = state.uiScale;
    auto b = getLocalBounds().toFloat().reduced (16.0f * u);
    b.removeFromTop (30.0f * u);
    b.removeFromBottom (128.0f * u);
    // keep the room's shape (width x length of the preset, scaled by room size)
    const auto& rooms = roomPresets();
    const int ri = juce::jlimit (0, (int) rooms.size() - 1, (int) state.param (E::Ids::room));
    const float w = rooms[(size_t) ri].width, l = rooms[(size_t) ri].length;
    const float aspect = juce::jlimit (0.6f, 2.4f, w / juce::jmax (1.0f, l * 0.6f));
    float rw = b.getWidth(), rh = rw / aspect;
    if (rh > b.getHeight()) { rh = b.getHeight(); rw = rh * aspect; }
    return b.withSizeKeepingCentre (rw, rh);
}

juce::Point<float> StagePage::seatPos (int t) const
{
    const auto a = stageArea().reduced (28.0f * state.uiScale);
    const float x = state.param (E::Ids::track (t, E::kX));
    const float d = state.param (E::Ids::track (t, E::kDepth));
    // front of the stage at the bottom, near the listener
    return { a.getCentreX() + x * a.getWidth() * 0.5f, a.getBottom() - d * a.getHeight() };
}

int StagePage::playerAt (juce::Point<float> p) const
{
    const float r = 20.0f * state.uiScale;
    const int n = (int) state.getSong().tracks.size();
    for (int t = n - 1; t >= 0; --t)
        if (seatPos (t).getDistanceFrom (p) <= r) return t;
    return -1;
}

void StagePage::resized()
{
    const float u = state.uiScale;
    auto b = getLocalBounds().reduced (juce::roundToInt (16.0f * u));
    auto knobs = b.removeFromBottom (juce::roundToInt (118.0f * u));
    const int kw = juce::roundToInt (118.0f * u);
    knobs = knobs.withSizeKeepingCentre (kw * 4 + juce::roundToInt (30.0f * u), knobs.getHeight());
    for (auto* k : { &trackLevel, &trackHeight, &trackWidth, &trackRoom })
    {
        k->setBounds (knobs.removeFromLeft (kw));
        knobs.removeFromLeft (juce::roundToInt (10.0f * u));
    }
}

void StagePage::paint (juce::Graphics& g)
{
    const float u = state.uiScale;
    auto top = getLocalBounds().reduced (juce::roundToInt (16.0f * u)).removeFromTop (juce::roundToInt (28.0f * u)).toFloat();
    const auto& rooms = roomPresets();
    const int ri = juce::jlimit (0, (int) rooms.size() - 1, (int) state.param (E::Ids::room));
    const float sz = state.param (E::Ids::roomSize);
    heading (g, "STAGE  -  " + juce::String (rooms[(size_t) ri].name).toUpperCase() + "  ("
                    + juce::String (rooms[(size_t) ri].width * sz, 1) + " x " + juce::String (rooms[(size_t) ri].length * sz, 1) + " m)   drag the players",
             top, 18.0f * u);

    const auto area = stageArea();
    // floor
    juce::ColourGradient floor (juce::Colour (0xff3a2f25), area.getCentreX(), area.getY(), juce::Colour (0xff221b15), area.getCentreX(), area.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRoundedRectangle (area, 10.0f * u);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    for (float y = area.getY() + 14.0f * u; y < area.getBottom(); y += 14.0f * u) g.fillRect (area.getX() + 4.0f, y, area.getWidth() - 8.0f, 1.0f);
    g.setColour (colours::paper.withAlpha (0.35f));
    g.drawRoundedRectangle (area, 10.0f * u, 2.0f);
    // depth guides
    const auto inner = area.reduced (28.0f * u);
    g.setFont (ForgeLookAndFeel::font (11.0f * u, false));
    for (int i = 0; i <= 4; ++i)
    {
        const float y = inner.getBottom() - inner.getHeight() * (float) i / 4.0f;
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (inner.getX(), y, inner.getWidth(), 1.0f);
    }
    g.setColour (colours::paperDim);
    g.drawText ("BACK", inner.withHeight (16.0f * u).translated (0.0f, -22.0f * u), juce::Justification::centred, false);
    // listener / microphones at the front
    const float micY = area.getBottom() + 0.0f;
    const float micSpread = (10.0f + 40.0f * state.param (E::Ids::mics)) * u;
    g.setColour (colours::glow);
    for (float dx : { -micSpread, micSpread })
    {
        g.fillEllipse (area.getCentreX() + dx - 5.0f * u, micY - 5.0f * u, 10.0f * u, 10.0f * u);
    }
    ForgeLookAndFeel::drawPanelText (g, state.param (E::Ids::headphones) > 0.5f ? "YOU (HEADPHONES)" : "MICS / YOU",
                                     juce::Rectangle<float> (area.getCentreX() - 100.0f * u, micY + 6.0f * u, 200.0f * u, 18.0f * u),
                                     juce::Justification::centred, 12.0f * u, colours::glow.brighter (0.5f));

    // players
    const auto& song = state.getSong();
    const float r = 18.0f * u;
    for (int t = 0; t < (int) song.tracks.size(); ++t)
    {
        const auto p = seatPos (t);
        const bool sel = t == state.getSelectedTrack();
        const bool muted = state.param (E::Ids::track (t, E::kMute)) > 0.5f;
        const float h = state.param (E::Ids::track (t, E::kHeight));
        const float pk = juce::jmax (state.engine.getTrackPeak (t, 0), state.engine.getTrackPeak (t, 1));
        const auto col = trackColour (t).withMultipliedAlpha (muted ? 0.3f : 1.0f);
        // shadow grows with height
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (p.x - r + h * 6.0f * u, p.y - r * 0.5f + h * 10.0f * u, r * 2.0f, r);
        if (pk > 0.003f && ! muted)
        {
            const float glowR = r * (1.2f + juce::jmin (1.0f, pk * 2.0f));
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (p.x - glowR, p.y - glowR, glowR * 2.0f, glowR * 2.0f);
        }
        g.setColour (col);
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        g.setColour (sel ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, sel ? 2.5f : 1.0f);
        g.setColour (colours::ink);
        g.setFont (ForgeLookAndFeel::font (13.0f * u));
        g.drawText (juce::String (t + 1), juce::Rectangle<float> (p.x - r, p.y - r, r * 2.0f, r * 2.0f), juce::Justification::centred, false);
        // name under the player, or above it when a neighbour's name would be in the way
        bool above = false;
        for (int o = 0; o < t; ++o)
        {
            const auto q = seatPos (o);
            if (std::abs (q.x - p.x) < 170.0f * u && std::abs (q.y - p.y) < 2.0f * r) above = ! above;
        }
        const float ly = above ? p.y - r - 17.0f * u : p.y + r + 1.0f;
        ForgeLookAndFeel::drawPanelText (g, song.tracks[(size_t) t].name, juce::Rectangle<float> (p.x - 90.0f * u, ly, 180.0f * u, 16.0f * u),
                                         juce::Justification::centredTop, 12.0f * u, sel ? juce::Colours::white : colours::paperDim);
    }
    if (song.tracks.empty())
        ForgeLookAndFeel::drawPanelText (g, "No players yet: write a song on BAND or add tracks.", area, juce::Justification::centred, 16.0f * u);
    else
    {
        const int t = state.getSelectedTrack();
        // computed here from the parameters (the audio thread keeps its own copy)
        StageParams sp;
        sp.x = state.param (E::Ids::track (t, E::kX));
        sp.depth = state.param (E::Ids::track (t, E::kDepth));
        sp.height = state.param (E::Ids::track (t, E::kHeight));
        sp.width = state.param (E::Ids::track (t, E::kWidth));
        sp.roomSendDb = state.param (E::Ids::track (t, E::kRoomSend));
        GlobalStage gs;
        gs.room = (int) state.param (E::Ids::room);
        gs.roomSize = state.param (E::Ids::roomSize);
        gs.micDistance = state.param (E::Ids::mics);
        gs.headphone3D = state.param (E::Ids::headphones) > 0.5f;
        const auto geo = StageTrack::computeGeometry (sp, gs);
        ForgeLookAndFeel::drawPanelText (g, song.tracks[(size_t) juce::jmin (t, (int) song.tracks.size() - 1)].name + ":  "
                                             + juce::String (geo.distance, 1) + " m away,  " + juce::String (geo.delayMs, 1) + " ms later",
                                         juce::Rectangle<float> ((float) getWidth() - 360.0f * u, (float) getHeight() - 30.0f * u, 344.0f * u, 18.0f * u),
                                         juce::Justification::centredRight, 12.0f * u, colours::paperDim);
    }
}

void StagePage::mouseDown (const juce::MouseEvent& e)
{
    dragging = playerAt (e.position);
    if (dragging < 0) return;
    state.selectTrack (dragging);
    for (auto* w : { E::kX, E::kDepth })
        if (auto* p = state.engine.getAPVTS().getParameter (E::Ids::track (dragging, w))) p->beginChangeGesture();
}

void StagePage::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto a = stageArea().reduced (28.0f * state.uiScale);
    const float x = juce::jlimit (-1.0f, 1.0f, (e.position.x - a.getCentreX()) / (a.getWidth() * 0.5f));
    const float d = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    state.setParam (E::Ids::track (dragging, E::kX), x);
    state.setParam (E::Ids::track (dragging, E::kDepth), d);
    repaint();
}

void StagePage::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;
    for (auto* w : { E::kX, E::kDepth })
        if (auto* p = state.engine.getAPVTS().getParameter (E::Ids::track (dragging, w))) p->endChangeGesture();
    dragging = -1;
}

void StagePage::timerCallback()
{
    if (isShowing()) repaint();
}
} // namespace hf::ui
