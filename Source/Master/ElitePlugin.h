#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace pg
{
/** Static description of one of the Elite plugin types. */
struct PluginInfo
{
    const char* id;          // stable: saved in sessions and preset folders ("elite-vox")
    const char* name;        // "Elite Vox"
    const char* shortName;   // shown on insert buttons ("VOX")
    const char* blurb;       // one line for menus / tooltips
    juce::uint32 accent;     // accent colour (ARGB) of its faceplate
    bool masterOnly = false; // has latency: only allowed in MASTER slots (no delay compensation needed)
};

/**
    Base class of every Elite plugin. Each plugin is a real juce::AudioProcessor with its own
    AudioProcessorValueTreeState, so every knob is an APVTS parameter and the editors bind with
    Slider/Button/ComboBox attachments.

    THREAD CONTRACT
      prepareToPlay / releaseResources / state: message thread (or the export thread on its own copy).
      processBlock: audio thread. Never allocates, never locks.
      Readouts (meters, gain reduction) go audio -> UI through relaxed atomics.
      The analyzer FIFO (optional) is a single-producer/single-consumer juce::AbstractFifo and is
      only fed while an editor is open.

    Bypass ("bypass" parameter, added to every plugin) and the engine-wide BEFORE/AFTER switch
    crossfade over 5 ms against a latency-aligned dry copy.
*/
class ElitePlugin : public juce::AudioProcessor
{
public:
    static constexpr int kNumReadouts = 12;
    static constexpr int kAnalyzerFifoSize = 1 << 14;

    ElitePlugin (const PluginInfo& info, juce::AudioProcessorValueTreeState::ParameterLayout layout);
    ~ElitePlugin() override = default;

    const PluginInfo& getInfo() const noexcept { return info; }
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) final;
    void releaseResources() final {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    using AudioProcessor::processBlock;
    void reset() final;

    /** Stereo, in place, numSamples <= the block size given to prepareToPlay. Audio thread. */
    void processStereo (float* left, float* right, int numSamples) noexcept;

    const juce::String getName() const final { return info.name; }
    bool acceptsMidi() const final { return false; }
    bool producesMidi() const final { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() final { return 1; }
    int getCurrentProgram() final { return 0; }
    void setCurrentProgram (int) final {}
    const juce::String getProgramName (int) final { return {}; }
    void changeProgramName (int, const juce::String&) final {}
    bool isBusesLayoutSupported (const BusesLayout&) const final;
    bool hasEditor() const final { return editorFactory != nullptr; }
    juce::AudioProcessorEditor* createEditor() final { return editorFactory != nullptr ? editorFactory (*this) : nullptr; }

    /** Set once by the app (the UI module) so the engine and tests do not depend on editor code. */
    using EditorFactory = juce::AudioProcessorEditor* (*) (ElitePlugin&);
    static inline EditorFactory editorFactory = nullptr;

    void getStateInformation (juce::MemoryBlock&) final;
    void setStateInformation (const void*, int) final;
    /** Message thread. Parameters as a ValueTree (what sessions and presets store). */
    juce::ValueTree saveState();
    void loadState (const juce::ValueTree&);
    /** Every parameter back to its default (bypass included). */
    void resetToDefaults();

    //==============================================================================
    /** Engine-wide BEFORE/AFTER switch: while true every plugin is bypassed (with crossfade). */
    void setGlobalBypass (const std::atomic<bool>* flag) noexcept { globalBypass = flag; }
    /** A second switch for a group of plugins (the master inserts, for the CLEAN export). */
    void setGroupBypass (const std::atomic<bool>* flag) noexcept { groupBypass = flag; }
    /** Project tempo, for tempo-synced plugins. */
    void setTempoSource (const std::atomic<float>* bpm) noexcept { tempo = bpm; }
    float getTempo() const noexcept { return tempo != nullptr ? tempo->load (std::memory_order_relaxed) : 120.0f; }

    float getReadout (int index) const noexcept { return readouts[(size_t) index].load (std::memory_order_relaxed); }
    float getInputPeak() const noexcept  { return inPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outPeak.load (std::memory_order_relaxed); }
    bool isBypassedNow() const noexcept;

    /** Analyzer (post-processing, mono sum). Fed only while setAnalyzerActive(true). */
    void setAnalyzerActive (bool on) noexcept { analyzerActive.store (on); }
    int readAnalyzer (float* dest, int maxSamples) noexcept;
    double getPreparedSampleRate() const noexcept { return sampleRateHz; }

    /** Parameter value helpers for subclasses (audio thread safe). */
    float param (std::atomic<float>* p) const noexcept { return p->load (std::memory_order_relaxed); }
    std::atomic<float>* raw (const juce::String& id) const;

protected:
    /** Subclass hooks. prepareDsp allocates everything; processDsp never allocates. */
    virtual void prepareDsp (double sampleRate, int maxBlock) = 0;
    virtual void processDsp (float* left, float* right, int numSamples) noexcept = 0;
    virtual void resetDsp() noexcept = 0;
    /** Latency of processDsp in samples (constant after prepareDsp). */
    virtual int dspLatency() const noexcept { return 0; }

    void setReadout (int index, float value) noexcept { readouts[(size_t) index].store (value, std::memory_order_relaxed); }
    /** True while an editor wants live display data (subclasses may feed their own FIFOs). */
    bool isAnalyzerActive() const noexcept { return analyzerActive.load (std::memory_order_relaxed); }
    void pushAnalyzer (const float* left, const float* right, int numSamples) noexcept;

    const PluginInfo& info;
    juce::AudioProcessorValueTreeState apvts;
    double sampleRateHz = 48000.0;
    int maxBlockSize = 512;

private:
    std::atomic<float>* bypassParam = nullptr;
    const std::atomic<bool>* globalBypass = nullptr;
    const std::atomic<bool>* groupBypass = nullptr;
    const std::atomic<float>* tempo = nullptr;

    juce::AudioBuffer<float> dry;          // 2 x maxBlock
    std::vector<float> dryDelayL, dryDelayR; // latency alignment of the dry path
    int dryDelayPos = 0, latency = 0;
    float wetMix = 1.0f, wetStep = 0.0f;   // 1 = processed, 0 = bypassed
    bool wasFullyBypassed = false;

    std::array<std::atomic<float>, kNumReadouts> readouts {};
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    std::atomic<bool> analyzerActive { false };
    juce::AbstractFifo analyzerFifo { kAnalyzerFifoSize };
    std::vector<float> analyzerData = std::vector<float> ((size_t) kAnalyzerFifoSize, 0.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElitePlugin)
};

//==============================================================================
/** Parameter-layout helpers so every plugin declares parameters the same way. */
namespace params
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    inline constexpr int kVersion = 1;

    inline juce::ParameterID pid (const char* id) { return { id, kVersion }; }

    /** dB parameter, linear range. */
    void addDb (Layout&, const char* id, const char* name, float min, float max, float def, float step = 0.1f);
    /** Frequency (Hz), log-skewed. */
    void addHz (Layout&, const char* id, const char* name, float min, float max, float def);
    /** Milliseconds, log-skewed. */
    void addMs (Layout&, const char* id, const char* name, float min, float max, float def);
    /** Plain linear value with unit label. */
    void addFloat (Layout&, const char* id, const char* name, float min, float max, float def, const char* unit, float step = 0.01f, float centre = 0.0f);
    void addPercent (Layout&, const char* id, const char* name, float def);
    void addBool (Layout&, const char* id, const char* name, bool def);
    void addChoice (Layout&, const char* id, const char* name, const juce::StringArray& choices, int def);

    /** dB -> linear gain. */
    inline float dbToGain (float db) noexcept { return db <= -120.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
}
} // namespace pg
