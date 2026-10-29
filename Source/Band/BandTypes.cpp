#include "Band/BandTypes.h"

namespace pg::band
{
const char* genreName (Genre g)
{
    switch (g)
    {
        case Genre::outlawCountry: return "Outlaw Country";
        case Genre::bluegrass:     return "Bluegrass";
        case Genre::folk:          return "Folk";
        case Genre::gospel:        return "Gospel";
        case Genre::blues:         return "Blues";
        case Genre::trap:          return "Trap";
        case Genre::popTrap:       return "Pop Trap";
        case Genre::cinematic:     return "Cinematic";
        case Genre::count:         break;
    }
    return "?";
}

const char* instrumentName (Instrument i)
{
    switch (i)
    {
        case Instrument::acousticGuitar: return "Acoustic Guitar";
        case Instrument::electricGuitar: return "Electric Guitar";
        case Instrument::banjo:          return "Banjo";
        case Instrument::mandolin:       return "Mandolin";
        case Instrument::bass:           return "Bass";
        case Instrument::uprightBass:    return "Upright Bass";
        case Instrument::bass808:        return "808";
        case Instrument::fiddle:         return "Fiddle";
        case Instrument::harmonica:      return "Harmonica";
        case Instrument::piano:          return "Piano";
        case Instrument::organ:          return "Organ";
        case Instrument::strings:        return "Strings";
        case Instrument::drums:          return "Drums";
        case Instrument::trapDrums:      return "Trap Drums";
        case Instrument::count:          break;
    }
    return "?";
}

const char* sectionName (Section s)
{
    switch (s)
    {
        case Section::intro:  return "Intro";
        case Section::verse:  return "Verse";
        case Section::chorus: return "Chorus";
        case Section::bridge: return "Bridge";
        case Section::solo:   return "Solo";
        case Section::outro:  return "Outro";
    }
    return "?";
}

std::vector<Instrument> defaultInstruments (Genre g)
{
    using I = Instrument;
    switch (g)
    {
        case Genre::outlawCountry: return { I::drums, I::bass, I::acousticGuitar, I::electricGuitar, I::fiddle, I::harmonica };
        case Genre::bluegrass:     return { I::uprightBass, I::acousticGuitar, I::banjo, I::mandolin, I::fiddle };
        case Genre::folk:          return { I::uprightBass, I::acousticGuitar, I::harmonica, I::fiddle };
        case Genre::gospel:        return { I::drums, I::bass, I::piano, I::organ };
        case Genre::blues:         return { I::drums, I::bass, I::electricGuitar, I::harmonica, I::piano };
        case Genre::trap:          return { I::trapDrums, I::bass808, I::piano, I::strings };
        case Genre::popTrap:       return { I::trapDrums, I::bass808, I::electricGuitar, I::piano };
        case Genre::cinematic:     return { I::drums, I::piano, I::strings, I::bass };
        case Genre::count:         break;
    }
    return {};
}

std::vector<Instrument> availableInstruments (Genre g)
{
    using I = Instrument;
    switch (g)
    {
        case Genre::outlawCountry: return { I::drums, I::bass, I::acousticGuitar, I::electricGuitar, I::banjo, I::mandolin, I::fiddle, I::harmonica, I::piano, I::organ };
        case Genre::bluegrass:     return { I::uprightBass, I::acousticGuitar, I::banjo, I::mandolin, I::fiddle, I::harmonica };
        case Genre::folk:          return { I::drums, I::uprightBass, I::acousticGuitar, I::banjo, I::mandolin, I::fiddle, I::harmonica, I::piano, I::strings };
        case Genre::gospel:        return { I::drums, I::bass, I::piano, I::organ, I::strings, I::electricGuitar };
        case Genre::blues:         return { I::drums, I::bass, I::uprightBass, I::electricGuitar, I::acousticGuitar, I::harmonica, I::piano, I::organ };
        case Genre::trap:          return { I::trapDrums, I::bass808, I::piano, I::strings, I::electricGuitar, I::organ };
        case Genre::popTrap:       return { I::trapDrums, I::bass808, I::electricGuitar, I::acousticGuitar, I::piano, I::strings };
        case Genre::cinematic:     return { I::drums, I::bass, I::piano, I::strings, I::organ, I::fiddle, I::acousticGuitar };
        case Genre::count:         break;
    }
    return {};
}

juce::String Song::describe() const
{
    return juce::String (genreName (request.genre)) + " - " + theory::keyName (request.keyRoot)
           + (request.minor ? " minor" : " major") + " - " + juce::String (juce::roundToInt (bpm)) + " BPM";
}

//==============================================================================
namespace theory
{
const std::array<int, 7>& scale (bool minor)
{
    static const std::array<int, 7> maj { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> min { 0, 2, 3, 5, 7, 8, 10 };
    return minor ? min : maj;
}

int degreePitch (int keyRoot, bool minor, int degree, int baseOctave)
{
    const auto& s = scale (minor);
    const int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    const int d = degree - oct * 7;
    return 12 * (baseOctave + 1) + keyRoot + s[(size_t) d] + 12 * oct;
}

std::vector<int> chordPitchClasses (int keyRoot, bool minor, int degree, bool seventh)
{
    std::vector<int> pcs;
    const int n = seventh ? 4 : 3;
    for (int i = 0; i < n; ++i)
        pcs.push_back (degreePitch (keyRoot, minor, degree + 2 * i, 4) % 12);
    return pcs;
}

bool inScale (int keyRoot, bool minor, int pc)
{
    const int rel = ((pc - keyRoot) % 12 + 12) % 12;
    for (int s : scale (minor)) if (s == rel) return true;
    return false;
}

const char* keyName (int keyRoot)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((keyRoot % 12) + 12) % 12];
}

int atOrAbove (int pc, int lowest)
{
    int p = lowest + (((pc - lowest) % 12) + 12) % 12;
    return p;
}
} // namespace theory
} // namespace pg::band
