#pragma once

#include "UI/AppState.h"
#include "UI/Widgets.h"
#include "Band/BandBridge.h"

namespace hf::ui
{
/**
    BAND tab: pick a style, key, tempo and length on the left, choose which parts play and which
    instrument plays each part, press WRITE THE SONG. The band's parts become tracks (one per part),
    ready to play, edit, re-seat on the STAGE and export. Writing a new song is one undo step.
*/
class BandPage final : public juce::Component, private AppState::Listener
{
public:
    explicit BandPage (AppState&);
    ~BandPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Component& getSidePanel() noexcept { return side; }

    /** Writes the song from the current choices (also used by the preview tool). */
    void write (bool newSeed);

private:
    struct PartRow
    {
        pg::band::Instrument role;
        std::unique_ptr<juce::ToggleButton> on;
        std::unique_ptr<juce::TextButton> pick;
        juce::String instrumentId;
    };
    class Side final : public juce::Component
    {
    public:
        explicit Side (BandPage& p) : page (p) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        BandPage& page;
    };

    void libraryChanged() override { genreChanged(); }
    void songChanged() override { repaint(); }
    void genreChanged();
    pg::band::Request makeRequest() const;
    void saveChoices();

    AppState& state;
    Side side { *this };
    juce::ComboBox genre, key, tempo, length;
    juce::ToggleButton minor { "Minor key" };
    std::vector<PartRow> parts;
    juce::TextButton writeButton { "WRITE THE SONG" }, againButton { "ANOTHER VERSION" }, balanceButton { "BALANCE THE MIX" }, sunoButton { "COPY STYLE FOR SUNO" };
    juce::uint32 seed = 1;
};
} // namespace hf::ui
