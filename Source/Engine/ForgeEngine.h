#pragma once

#include "Engine/PlaySnapshot.h"
#include "Sampler/VoicePool.h"
#include "Stage/Stage.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace hf
{
/**
    HEM Instrument Forge audio engine (a juce::AudioProcessor driven by the standalone app's
    AudioProcessorPlayer, or rendered offline by the exporter).

    Signal flow per track:  notes -> sample voices -> track level / mute / solo
                            -> 3D stage (seat, distance, early echoes) -> mix  (+ room send)
    Mix + room tail -> master level -> safety limiter (-0.3 dBFS) -> out.

    THREAD CONTRACT
      Message thread: parameters (APVTS), publishSong(), setTrackInstrument(), transport calls,
        live notes from the on-screen keyboard (ui queue), collectGarbage() from a timer.
      MIDI thread: liveMidi() (midi queue). Only one MIDI producer at a time.
      Audio thread: processBlock(). Reads parameters through getRawParameterValue() pointers,
        the song through a lock-free triple buffer, live notes through two SPSC FIFOs
        (juce::AbstractFifo), instruments through atomic pointers. No locks, no allocation.
      Disk thread: DiskStreamer (offline rendering services the streams in the render thread).
*/
class ForgeEngine final : public juce::AudioProcessor
{
public:
    static constexpr int kChunk = 256;   // internal processing block (events are sample-exact inside it)
    static constexpr float kMixHeadroomDb = -8.0f;   // fixed mix-bus headroom (see processChunk)

    struct Ids
    {
        static inline const juce::String bpm = "bpm", master = "master", room = "room", roomSize = "roomSize",
            reverb = "reverb", mics = "mics", headphones = "hp3d", click = "click";
        static juce::String track (int t, const char* what) { return "t" + juce::String (t) + "_" + what; }
    };
    // per-track parameter names (Ids::track (t, ...))
    static constexpr const char* kLevel = "level"; static constexpr const char* kMute = "mute";
    static constexpr const char* kSolo = "solo";   static constexpr const char* kX = "x";
    static constexpr const char* kDepth = "depth"; static constexpr const char* kHeight = "height";
    static constexpr const char* kWidth = "width"; static constexpr const char* kRoomSend = "room";
    static constexpr const char* kArt = "art";     static constexpr const char* kLegato = "legato";
    static constexpr const char* kMono = "mono";   static constexpr const char* kHuman = "human";
    static constexpr const char* kVelSens = "vel"; static constexpr const char* kTranspose = "trans";
    static constexpr const char* kRelease = "release";

    explicit ForgeEngine (bool offline = false);
    ~ForgeEngine() override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    float param (const juce::String& id) const;
    void setParam (const juce::String& id, float plainValue);

    // ---- song / instruments (message thread) ----------------------------------------
    void publishSong (const Song&);
    /** Sets (or clears with nullptr) a track's instrument. The old one is freed once nothing plays it. */
    void setTrackInstrument (int track, std::shared_ptr<LoadedInstrument>);
    std::shared_ptr<LoadedInstrument> getTrackInstrument (int track) const { return trackInst[(size_t) track]; }
    /** Frees replaced instruments nothing uses any more (call from a timer). */
    void collectGarbage();
    /** Puts the track's seat / mono / legato defaults from its instrument into the parameters. */
    void applyInstrumentDefaults (int track, const InstrumentDef&);

    // ---- transport (message thread) ---------------------------------------------------
    void play();
    void stop();
    bool isPlaying() const noexcept { return playing.load(); }
    void setPositionBeats (double beats);
    double getPositionBeats() const noexcept { return positionBeats.load(); }
    void setLoop (bool on, double startBeat, double endBeat);
    bool isLooping() const noexcept { return loopOn.load(); }
    double getLoopStart() const noexcept { return loopStart.load(); }
    double getLoopEnd() const noexcept { return loopEnd.load(); }

    // ---- live playing ----------------------------------------------------------------
    /** Which track the on-screen keyboard and MIDI input play (and record into). */
    void setLiveTrack (int t) noexcept { liveTrack.store (juce::jlimit (0, kMaxTracks - 1, t)); }
    int getLiveTrack() const noexcept { return liveTrack.load(); }
    /** Message thread (on-screen keyboard). */
    void uiNoteOn (int key, int velocity);
    void uiNoteOff (int key);
    /** MIDI thread (one input device at a time). */
    void liveMidi (const juce::MidiMessage&);
    /** Recording: notes played live while playing are captured; commitRecording() returns them. */
    void setRecording (bool on) noexcept { recording.store (on); }
    bool isRecording() const noexcept { return recording.load(); }
    struct RecordedNote { int track; Note note; };
    /** Message thread: collects finished recorded notes (call often; notes still held stay pending). */
    void collectRecorded (std::vector<RecordedNote>& out);

    // ---- meters ----------------------------------------------------------------------
    float getTrackPeak (int t, int ch) const noexcept { return trackPeak[(size_t) t][(size_t) ch].load (std::memory_order_relaxed); }
    float getMasterPeak (int ch) const noexcept { return masterPeak[(size_t) ch].load (std::memory_order_relaxed); }
    int getActiveVoices() const noexcept { return voices.activeVoices(); }
    int getUnderruns() const noexcept { return streamer.underruns.load(); }
    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }
    double getEngineSampleRate() const noexcept { return sampleRateHz; }

    // ---- offline (exporter) ----------------------------------------------------------
    /** Renders from the current position (offline engines only). */
    void renderOffline (juce::AudioBuffer<float>& out, int start, int numSamples);
    bool isOfflineEngine() const noexcept { return offlineMode; }

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    const juce::String getName() const override { return "HEM Instrument Forge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    struct LiveEvent { juce::uint8 type = 0; juce::uint8 key = 0; juce::uint8 value = 0; juce::int16 value14 = 0; };
    enum LiveType : juce::uint8 { evNoteOn = 1, evNoteOff, evPedal, evBend, evAllOff };
    struct LiveQueue
    {
        static constexpr int kSize = 1024;
        juce::AbstractFifo fifo { kSize };
        std::array<LiveEvent, kSize> data {};
        bool push (const LiveEvent&) noexcept;
        bool pop (LiveEvent&) noexcept;
    };
    struct RecEvent { juce::int16 track = 0; juce::int16 key = 0; juce::int16 velocity = 0; bool on = true; double beat = 0.0; };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void cacheParameters();
    void processChunk (float* outL, float* outR, int n) noexcept;
    void runSequencer (double b0, double b1, int chunkOffset, int n) noexcept;
    void handleLive (LiveQueue&) noexcept;
    void handleLiveEvent (const LiveEvent&) noexcept;
    void startNote (int t, const PlayEvent&, int offset, bool live) noexcept;
    void metronome (float* l, float* r, int n, double b0, double b1) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    const bool offlineMode;
    double sampleRateHz = 48000.0;

    // parameters
    std::atomic<float>* pBpm = nullptr, *pMaster = nullptr, *pRoom = nullptr, *pRoomSize = nullptr, *pReverb = nullptr,
                      *pMics = nullptr, *pHp = nullptr, *pClick = nullptr;
    struct TrackParams { std::atomic<float>* level, *mute, *solo, *x, *depth, *height, *width, *room, *art, *legato, *mono, *human, *vel, *trans, *release; };
    std::array<TrackParams, kMaxTracks> tp {};

    // song
    TripleBuffer<PlaySnapshot> songBuffer;
    const PlaySnapshot* song = nullptr;   // audio thread's current snapshot

    // instruments
    std::array<std::shared_ptr<LoadedInstrument>, kMaxTracks> trackInst;      // message thread owns
    std::array<std::atomic<const LoadedInstrument*>, kMaxTracks> pendingInst {};   // message -> audio
    std::array<std::atomic<const LoadedInstrument*>, kMaxTracks> audioInst {};     // audio -> message (in use)
    std::vector<std::shared_ptr<LoadedInstrument>> retired;

    // playing
    DiskStreamer streamer;
    VoicePool voices;
    std::array<TrackPlayState, kMaxTracks> trackState {};
    std::array<StageTrack, kMaxTracks> stages;
    RoomTail roomTail;
    juce::AudioBuffer<float> trackBuf;    // 2 * kMaxTracks channels x kChunk
    juce::AudioBuffer<float> mixBuf, sendBuf;
    std::array<float*, kMaxTracks> trackL {}, trackR {};
    std::array<float, kMaxTracks> trackGainSmoothed {};

    // transport
    std::atomic<bool> playing { false }, loopOn { false }, recording { false };
    std::atomic<double> positionBeats { 0.0 }, loopStart { 0.0 }, loopEnd { 16.0 };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool> stopRequest { false };
    double playBeats = 0.0;               // audio thread's position
    bool wasPlaying = false;
    double clickPhase = 0.0; float clickEnv = 0.0f; double clickFreq = 1000.0;

    // live
    std::atomic<int> liveTrack { 0 };
    LiveQueue uiQueue, midiQueue;
    std::array<std::array<juce::uint32, 128>, kMaxTracks> liveIds {};
    juce::uint32 liveCounter = 0x80000000u;
    juce::AbstractFifo recFifo { 4096 };
    std::array<RecEvent, 4096> recData {};
    struct PendingRec { int track; int key; int velocity; double beat; };
    std::vector<PendingRec> pendingRec;   // message thread

    // limiter (sample peak, fast attack / 80 ms release, no look-ahead so live playing has no extra latency)
    float limGain = 1.0f;

    std::array<std::array<std::atomic<float>, 2>, kMaxTracks> trackPeak {};
    std::array<std::atomic<float>, 2> masterPeak {};
    juce::AudioProcessLoadMeasurer loadMeasurer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeEngine)
};
} // namespace hf
