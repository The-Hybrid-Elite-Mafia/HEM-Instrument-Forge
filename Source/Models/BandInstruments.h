#pragma once

#include "Band/BandTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace pg::band
{
/**
    Modeled instruments, rendered offline (this is not real-time code).
    Plucked strings (guitar, banjo, mandolin, basses, piano strings) are waveguides: a tuned delay
    line with a loss filter, excited by a shaped noise burst and coloured by body resonances.
    Fiddle, harmonica, organ and strings are band-limited oscillators shaped by bow / breath /
    formant models. Drums are synthesized hits. All original code, no samples.
*/

/** Renders one part into `out` (2 channels, already sized for the song, added in place).
    Returns false if cancelled. */
bool renderPart (const Song& song, const Part& part, double sampleRate, juce::AudioBuffer<float>& out,
                 const std::atomic<bool>* cancel = nullptr);

/** Renders a single note of an instrument at `sampleRate` into a fresh stereo buffer
    (used by the tests and for auditioning). */
juce::AudioBuffer<float> renderSingleNote (Instrument inst, int pitch, float velocity, double seconds,
                                           double sampleRate, int articulation = art::normal);

/** Loudness the stem is levelled to (RMS of the playing parts, dBFS), so the band is balanced
    with every fader at 0 dB. */
float stemTargetRmsDb (Instrument);

double midiToHz (double midi);
} // namespace pg::band
