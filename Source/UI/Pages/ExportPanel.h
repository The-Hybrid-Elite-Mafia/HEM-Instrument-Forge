#pragma once

#include "UI/AppState.h"
#include "UI/Widgets.h"
#include "Engine/Exporter.h"

namespace hf::ui
{
/**
    Export window (over the main panel): stems (one WAV per track), a clean or mastered mix, and the
    MIDI file, rendered offline on a background thread through a private copy of the engine (the
    live engine keeps playing). Message thread only; the render thread reports through callAsync.
*/
class ExportPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ExportPanel (AppState&);
    ~ExportPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void open();
    void start();
    void cancel();
    bool isBusy() const noexcept { return job != nullptr; }
    std::function<void()> onClose;
    /** Folder and files of the last finished export (preview tool / status). */
    juce::File lastFolder;
    juce::String lastResult;

private:
    class Job;
    void timerCallback() override;
    void finished (bool ok, const juce::String& error, const ExportResult& result, const juce::File& folder);

    AppState& state;
    juce::ToggleButton stems { "Stems: one WAV per track" }, midiFile { "MIDI file of the notes" };
    juce::ComboBox mix, rate, bits;
    juce::TextEditor name;
    juce::TextButton folderButton { "CHANGE FOLDER" }, goButton { "EXPORT" }, closeButton { "CLOSE" }, openButton { "OPEN FOLDER" };
    juce::File folder;
    std::unique_ptr<Job> job;
    std::atomic<float> progress { 0.0f };
    juce::String progressText;
    juce::CriticalSection textLock;   // message thread <-> render thread status text (never the audio thread)
};
} // namespace hf::ui
