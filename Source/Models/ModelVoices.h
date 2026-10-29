#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

namespace hf::models
{
/**
    Modeled instruments: original synthesis code (no recordings), used only offline by the
    ModelRender tool to turn every modeled instrument into a multi-sampled library instrument
    (so the real-time engine plays everything the same way).

    Models (all physically-inspired, none is a measured copy of a real instrument):
      plucked strings  two-polarization digital waveguide (tuned delay loop, loss filter,
                       fractional all-pass tuning), pick-position comb, pick noise, body
                       resonances (bank of damped resonators), optional sympathetic / octave /
                       unison courses, magnetic pickup + amp + cabinet for electric
      tine piano       decaying tine partials + bell partial + asymmetric pickup "bark"
      clavinet         very bright waveguide string, pickup comb, fast yarn damping
      tonewheel organ  nine drawbar sine "wheels", key click, percussion, scanner chorus
      free reeds       pulse-shaped reed waveforms, musette detune, bellows envelope, formants
      flutes           jet tone (low partials) + shaped breath noise + chiff
      voices           glottal source through vowel formant filters, ensemble detune, vibrato
      steel drum       inharmonic modal partials
      synths           band-limited saw / square / supersaw, filter, FM bell
    The quality of each is decided by listening; the tests only check pitch, level and safety.
*/
struct NoteSpec
{
    double sampleRate = 48000.0;
    int pitch = 60;
    float velocity = 0.7f;       // 0..1
    double seconds = 4.0;        // length rendered (sustained sounds hold for the whole time)
    juce::uint32 seed = 1;
    int variant = 0;             // model-specific articulation
};

using Renderer = std::function<juce::AudioBuffer<float> (const NoteSpec&)>;

/** Renderers by model name; empty function if unknown. */
Renderer renderer (const juce::String& model);
juce::StringArray modelNames();

double midiToHz (double midi) noexcept;
} // namespace hf::models
