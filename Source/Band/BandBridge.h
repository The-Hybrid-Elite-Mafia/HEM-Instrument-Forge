#pragma once

#include "Band/BandComposer.h"
#include "Library/Library.h"
#include "Session/Song.h"

namespace hf::bandbridge
{
namespace band = pg::band;

/** One band part: which role the composer writes and which library instrument plays it. */
struct RoleChoice
{
    band::Instrument role = band::Instrument::acousticGuitar;
    juce::String instrumentId;
    bool on = true;
};

/** Plain name of a part ("Acoustic Guitar part"). */
juce::String roleName (band::Instrument);
/** Drum parts use General MIDI drum notes and need a drum kit. */
bool isDrumRole (band::Instrument);

/** The instrument a role uses by default in a genre (the first one installed from a preference list). */
juce::String defaultInstrument (band::Instrument role, band::Genre, const Library&);
/** Instruments that can play a role, best matches first (drum roles: drum kits only). */
std::vector<const LibraryEntry*> candidates (band::Instrument role, const Library&);

/** Writes the band and turns every chosen part into a track (notes moved into the instrument's range). */
Song makeSong (const band::Request&, const std::vector<RoleChoice>&, const Library&);
/** Writes a different take of one track's part over the same chords (other tracks unchanged). */
bool rerollTrack (Song&, int track, juce::uint32 seed, const Library&);

/** Moves a part by octaves into lo..hi (notes still outside are folded in). Drum parts are left alone. */
void fitToRange (Track&, int lo, int hi);

juce::var requestToVar (const band::Request&);
band::Request requestFromVar (const juce::var&);

/** "Outlaw Country instrumental, 96 BPM, G major, fiddle, banjo, ..., live band feel, warm mix" */
juce::String sunoStyle (const Song&, const Library&);
} // namespace hf::bandbridge
