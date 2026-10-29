#pragma once

#include "Library/Library.h"
#include "Sampler/LoadedInstrument.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hemtest
{
void beginTracking();
int endTracking();
void addToTracked (int);

/**
    A small instrument library written to a temporary folder for the tests:
      test-sine   pitched, roots 48 / 60 / 72, two velocity layers (sine / brighter), 3 s notes
                  (longer than two heads, so they stream from disk); articulation 2 "Held" has
                  2.5 s looped notes; articulation 3 "Pedal" has pedal-up / pedal-down zones
      test-rr     pitched, one root (60), round robin of 3 (different amplitudes 0.2 / 0.4 / 0.6)
      test-perc   unpitched, keys 60 / 61, short hits (resident)
      test-kit    drum kit on GM 36 / 38 / 42 / 46
    Every sample is a known signal so tests can check pitch, level and continuity exactly.
*/
class TestLibrary
{
public:
    TestLibrary();
    ~TestLibrary();
    const juce::File& folder() const noexcept { return root; }
    const hf::Library& library() const noexcept { return lib; }
    std::shared_ptr<const hf::InstrumentDef> def (const juce::String& id) const;
    std::shared_ptr<hf::LoadedInstrument> load (const juce::String& id) const;

    static constexpr double kRate = 48000.0;
    static float sineFreq (int root) { return 440.0f * std::pow (2.0f, (float) (root - 69) / 12.0f); }
    /** The exact sample value written for the test-sine zone (root, layer) at frame i. */
    static float sineSample (int root, int layer, int i);

private:
    juce::File root;
    hf::Library lib;
};
} // namespace hemtest
