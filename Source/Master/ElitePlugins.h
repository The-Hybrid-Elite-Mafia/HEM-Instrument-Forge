#pragma once

#include "ElitePlugin.h"
#include "Core/Dynamics.h"
#include "Core/Loudness.h"
#include "Core/Biquad.h"
#include "Core/LookaheadLimiter.h"
#include "Core/Fft.h"
#include <complex>

namespace pg
{
//==============================================================================
/** ELITE VOX - one-move vocal / drum leveller: gate, SQUEEZE compressor with automatic makeup,
    built-in output limiter, output level. Modes set the timing for vocals, drums, bass or a bus. */
class EliteVox final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray modes() { return { "Vocal", "Drums", "Bass", "Bus" }; }
    EliteVox();

    enum Readout { gateGr, compGr, autoGain, limitGr };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateSettings() noexcept;
    std::atomic<float> *pGate, *pSqueeze, *pOutput, *pMode;
    dsp::GateCore gate;
    dsp::CompressorCore comp;
    dsp::SafetyLimiter limiter;
    juce::SmoothedValue<float> makeup, output;
    float lastGate = 1.0e9f, lastSqueeze = 1.0e9f; int lastMode = -1;
};

//==============================================================================
/** ELITE EQ - six-band paragraphic EQ (cuts are 24 dB/oct) with a live spectrum analyzer. */
class EliteEq final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static constexpr int kBands = 6;
    static juce::StringArray typesFor (int band);
    /** Default type/frequency for a band. */
    static float defaultFreq (int band);
    EliteEq();

    /** Message thread (UI curve): the 1-2 biquads of a band for the current parameters. Returns stages (0 = band off / flat). */
    int bandCoeffs (int band, double sampleRate, BiquadCoeffs out[2]) const;
    static juce::String id (int band, const char* what) { return "b" + juce::String (band + 1) + "_" + what; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    struct BandParams { std::atomic<float> *on, *type, *freq, *gain, *q; };
    std::array<BandParams, kBands> bp {};
    std::atomic<float>* pOutput = nullptr;
    std::array<std::array<BiquadCoeffs, 2>, kBands> coeffs {};
    std::array<int, kBands> stages {};
    std::array<std::array<std::array<BiquadState, 2>, 2>, kBands> st {};   // band x channel x stage
    std::array<std::array<float, 5>, kBands> cache {};
    juce::SmoothedValue<float> output;
};

//==============================================================================
/** ELITE COMP - smooth bus / vocal compressor with auto release (ARC), three characters
    (Clean, Electro, Warm) and parallel MIX. */
class EliteComp final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray modes() { return { "Clean", "Electro", "Warm" }; }
    EliteComp();
    enum Readout { gr };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pThr, *pRatio, *pAtk, *pRel, *pArc, *pMode, *pGain, *pMix;
    dsp::CompressorCore comp;
    juce::SmoothedValue<float> makeup, mix;
    std::array<float, 8> cache {};
};

//==============================================================================
/** ELITE BASS - psychoacoustic bass: adds the 2nd-4th harmonics of the bass below FREQ so the low
    end is heard on small speakers, and lets you turn the original sub down (ORIGINAL). */
class EliteBass final : public ElitePlugin
{
public:
    static const PluginInfo info;
    EliteBass();
    enum Readout { originalLevel, harmonicsLevel };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateFilters (float freq) noexcept;
    std::atomic<float> *pFreq, *pIntensity, *pOriginal, *pOutput, *pSolo;
    BiquadCoeffs lp, hp, hHp, hLp;
    std::array<std::array<BiquadState, 2>, 2> lowSt {}, highSt {};   // channel x stage
    std::array<BiquadState, 2> hHpSt {}, hLpSt {};
    float env = 0.0f, envAtt = 0.0f, envRel = 0.0f, lastFreq = -1.0f;
    juce::SmoothedValue<float> intensity, original, output;
};

//==============================================================================
/** ELITE CHANNEL - gate + 4-band EQ + compressor channel strip, order switchable. */
class EliteChannel final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray orders() { return { "EQ > Dyn", "Dyn > EQ" }; }
    static juce::StringArray compModes() { return { "Clean", "Vox" }; }
    static constexpr int kEqBands = 4;
    static juce::String eqId (int band, const char* what) { return "eq" + juce::String (band + 1) + "_" + what; }
    EliteChannel();
    enum Readout { gateGr, compGr };

    int bandCoeffs (int band, double sampleRate, BiquadCoeffs& out) const;

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateSettings() noexcept;
    std::atomic<float> *pGateOn, *pGateThr, *pGateFloor, *pGateRel;
    std::atomic<float> *pEqOn;
    std::array<std::array<std::atomic<float>*, 3>, kEqBands> pEq {};   // freq, gain, q
    std::atomic<float> *pCompOn, *pCompThr, *pCompRatio, *pCompAtk, *pCompRel, *pCompMode;
    std::atomic<float> *pOutput, *pPhase, *pOrder;
    dsp::GateCore gate;
    dsp::CompressorCore comp;
    std::array<BiquadCoeffs, kEqBands> eq {};
    std::array<std::array<BiquadState, 2>, kEqBands> eqSt {};
    std::array<float, 32> cache {};
    juce::SmoothedValue<float> output, makeup;
};

//==============================================================================
/** ELITE VERB - 8-line feedback-delay-network reverb (Room / Plate / Hall). */
class EliteVerb final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray types() { return { "Room", "Plate", "Hall" }; }
    EliteVerb();
    double getTailLengthSeconds() const override { return 12.0; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    static constexpr int kLines = 8, kDiffusers = 4;
    struct Line { std::vector<float> buf; int write = 0; float length = 1000.0f, target = 1000.0f; float lp = 0.0f; float gain = 0.5f; };
    struct Allpass { std::vector<float> buf; int pos = 0; int length = 100; };
    void updateTargets() noexcept;
    float readFrac (const Line&, float delay) const noexcept;

    std::atomic<float> *pPre, *pSize, *pDecay, *pDamp, *pLowCut, *pMix, *pWidth, *pType;
    std::array<Line, kLines> lines;
    std::array<Allpass, kDiffusers> diffusers;
    std::vector<float> pre; int preWrite = 0;
    BiquadCoeffs lowCut; std::array<BiquadState, 2> lowCutSt {};
    float dampCoeff = 0.3f, lfoPhase = 0.0f, lfoInc = 0.0f, diffusion = 0.7f;
    std::array<float, 8> cache {};
    juce::SmoothedValue<float> mix, width;
};

//==============================================================================
/** ELITE DELAY - stereo / ping-pong delay with tempo sync, filtered and saturated feedback and ducking. */
class EliteDelay final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray divisions() { return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" }; }
    static double divisionBeats (int index);
    EliteDelay();
    double getTailLengthSeconds() const override { return 8.0; }
    enum Readout { delayMs };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pTime, *pSync, *pDiv, *pFeedback, *pLowCut, *pHighCut, *pPing, *pMix, *pDuck;
    std::vector<float> bufL, bufR;
    int write = 0;
    float current = 0.0f, duckEnv = 0.0f, duckRel = 0.0f;
    BiquadCoeffs hp, lp; std::array<BiquadState, 2> hpSt {}, lpSt {};
    float lastLow = -1.0f, lastHigh = -1.0f;
    juce::SmoothedValue<float> feedback, mix, ping;
};

//==============================================================================
/** ELITE MASTER - final look-ahead limiter with character, true-peak and LUFS metering and
    "match target" for streaming loudness. MASTER slots only (it has 1.5 ms of look-ahead). */
class EliteMaster final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray characters() { return { "Clean", "Punch", "Warm" }; }
    static juce::StringArray targets() { return { "Streaming -14 LUFS", "Apple -16 LUFS", "Loud -9 LUFS", "Club -7 LUFS" }; }
    static float targetLufs (int index);
    EliteMaster();
    enum Readout { gr, momentary, shortTerm, integrated, truePeak, suggestedInput };

    /** Message thread: set INPUT so the integrated loudness lands on the chosen target. False if nothing measured yet. */
    bool matchTarget();
    /** Message thread: restart the loudness / true-peak measurement. */
    void resetMeters() noexcept { resetRequest.store (true); }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;
    int dspLatency() const noexcept override { return limiter.getLatencySamples(); }

private:
    std::atomic<float> *pInput, *pCeiling, *pRelease, *pChar, *pTarget;
    LookaheadLimiter limiter;
    dsp::LoudnessMeter loudness;
    dsp::TruePeakMeter tpMeter;
    BiquadCoeffs warmLow, warmHigh; std::array<std::array<BiquadState, 2>, 2> warmSt {};
    juce::SmoothedValue<float> input;
    float lastCeil = 1.0e9f, lastRel = -1.0f;
    std::atomic<bool> resetRequest { false };
};
//==============================================================================
/** ELITE SWIRL KILLER - spectral Mid/Side de-swirl for AI-generated audio.

    Every STFT bin is split into Mid (what L and R share) and Side (the difference). Real panning
    and width move together with the Mid, so over ~130 ms the Side of such a bin is coherent with
    its Mid. Codec swirl / shimmer is Side that has no steady relation to the Mid (low Mid/Side
    coherence) and flickers. Above FROM, those bins are treated; the Mid is never touched, so a
    centred vocal breath at 12 kHz survives while the swirl around it goes.
      REMOVE - turns the flagged Side down (to -26 dB at most).
      REPAIR - keeps the part of the Side that follows the Mid (stable panning / width) and drops
               only the erratic rest.
    PROTECT leaves very wide bins alone (deliberate stereo: wide pads, stereo reverbs).
    LISTEN plays only what is being taken out.

    Latency: one FFT frame (2048 samples up to 64 kHz, 4096 above); the engine compensates it.
    The editor reads a lock-free FIFO of per-band frames (level, swirl, stereo spread) for the 3D view. */
class EliteSwirl final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray modes() { return { "Remove", "Repair" }; }
    static constexpr int kBands = 48;
    static constexpr int kFrameFifo = 256;
    struct Frame { std::array<float, kBands> level {}, swirl {}, spread {}, pan {}; };

    EliteSwirl();
    double getTailLengthSeconds() const override { return 0.0; }
    enum Readout { removedDb, swirlPercent, coherence, whistleHz, shieldActive };

    /** Message thread (UI): pops up to `max` frames, oldest first. */
    int readFrames (Frame* dest, int max) noexcept;
    /** Centre frequency of display band b. */
    static float bandHz (int b) noexcept;

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;
    int dspLatency() const noexcept override { return fftSize; }

private:
    void processFrame() noexcept;
    std::atomic<float> *pAmount, *pFrom, *pProtect, *pMode, *pListen, *pOutput, *pWhistle, *pShield;
    int fftSize = 2048, hop = 512, pos = 0, hopCount = 0;
    dsp::Fft fft;
    std::vector<float> window, inL, inR, outL, outR;
    std::vector<std::complex<float>> bufL, bufR;
    // per-bin state
    std::vector<std::complex<float>> smCross;
    std::vector<float> sPowAvg;
    std::vector<float> mPowAvg, gain, gainTmp, sideSlow, whistleGain, whistleTmp;
    float hfAvg = 0.0f, corrAvg = 1.0f;
    int shieldHold = 0;
    std::vector<std::array<float, 5>> sideHist;
    std::vector<int> bandOf;
    std::vector<float> bandCount;
    int histPos = 0;
    juce::SmoothedValue<float> output;
    juce::AbstractFifo frameFifo { kFrameFifo };
    std::vector<Frame> frames = std::vector<Frame> ((size_t) kFrameFifo);
    Frame work;
};
} // namespace pg
