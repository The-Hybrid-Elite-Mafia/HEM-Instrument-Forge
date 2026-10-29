#pragma once

#include "Band/BandTypes.h"

namespace pg::band
{
/**
    Writes a whole instrumental from a Request: tempo, song form, chord progression per section and
    the notes for every chosen instrument in the genre's style (boom-chuck guitar, banjo rolls,
    fiddle fills, harmonica train chords, shuffles, trap hats...). Deterministic for a given seed.
*/
Song compose (const Request&);

/** Genre default tempo. */
double defaultBpm (Genre);
} // namespace pg::band
