#include "Band/BandBridge.h"
#include <algorithm>
#include <cmath>

namespace hf::bandbridge
{
using I = band::Instrument;
using G = band::Genre;

juce::String roleName (I r)
{
    switch (r)
    {
        case I::acousticGuitar: return "Rhythm Guitar";
        case I::electricGuitar: return "Lead Guitar";
        case I::banjo:          return "Banjo Part";
        case I::mandolin:       return "Mandolin Part";
        case I::bass:           return "Bass Line";
        case I::uprightBass:    return "Walking Bass";
        case I::bass808:        return "808 Line";
        case I::fiddle:         return "Fiddle Line";
        case I::harmonica:      return "Harp Line";
        case I::piano:          return "Piano Part";
        case I::organ:          return "Organ Part";
        case I::strings:        return "Pad / Strings";
        case I::drums:          return "Drums";
        case I::trapDrums:      return "Trap Drums";
        case I::count:          break;
    }
    return "Part";
}

bool isDrumRole (I r) { return r == I::drums || r == I::trapDrums; }

float roleTargetRmsDb (int role)
{
    // the band's balance (same table the Band Forge renderer levels its stems to)
    switch (role < 0 || role >= (int) I::count ? I::count : (I) role)
    {
        case I::drums: return -17.0f;          case I::trapDrums: return -16.0f;
        case I::bass: return -19.0f;           case I::uprightBass: return -20.0f;  case I::bass808: return -17.0f;
        case I::acousticGuitar: return -21.0f; case I::electricGuitar: return -22.5f;
        case I::banjo: return -23.0f;          case I::mandolin: return -24.0f;
        case I::fiddle: return -22.0f;         case I::harmonica: return -23.0f;
        case I::piano: return -21.5f;          case I::organ: return -26.0f;        case I::strings: return -24.5f;
        case I::count: default: break;
    }
    return -22.0f;
}

namespace
{
std::vector<juce::String> preferences (I r, G g)
{
    switch (r)
    {
        case I::acousticGuitar: return { "acoustic-guitar", "nylon-guitar", "twelve-string", "strumstick" };
        case I::electricGuitar: return g == G::blues || g == G::outlawCountry ? std::vector<juce::String> { "electric-guitar-crunch", "electric-guitar-clean" }
                                                                              : std::vector<juce::String> { "electric-guitar-clean", "electric-guitar-crunch" };
        case I::banjo:          return { "banjo", "strumstick" };
        case I::mandolin:       return { "mandolin", "bouzouki" };
        case I::bass:           return { "electric-bass", "double-bass" };
        case I::uprightBass:    return { "double-bass", "electric-bass" };
        case I::bass808:        return { "bass-808", "synth-bass" };
        case I::fiddle:         return { "fiddle", "solo-violin" };
        case I::harmonica:      return { "harmonica-c", "harmonica-f", "harmonica-chromatic" };
        case I::piano:          return g == G::gospel || g == G::blues ? std::vector<juce::String> { "upright-piano-b", "grand-piano-b", "grand-piano-a" }
                                                                       : std::vector<juce::String> { "grand-piano-b", "grand-piano-a", "upright-piano-b" };
        case I::organ:          return g == G::cinematic ? std::vector<juce::String> { "pipe-organ", "tonewheel-organ" }
                                                         : std::vector<juce::String> { "tonewheel-organ", "chamber-organ", "pipe-organ" };
        case I::strings:        return { "string-ensemble", "violin-section", "warm-pad" };
        case I::drums:          return g == G::cinematic ? std::vector<juce::String> { "kit-orchestral", "kit-jazz" }
                                                         : std::vector<juce::String> { "kit-jazz", "kit-orchestral" };
        case I::trapDrums:      return { "kit-trap", "kit-jazz" };
        case I::count:          break;
    }
    return {};
}

juce::StringArray familiesFor (I r)
{
    switch (r)
    {
        case I::acousticGuitar: case I::electricGuitar: case I::banjo: case I::mandolin:
            return { "Guitars & Plucked", "World", "Keys", "Mallets & Bells" };
        case I::bass: case I::uprightBass: case I::bass808:
            return { "Bass", "Synths", "Brass" };
        case I::fiddle: case I::harmonica:
            return { "Strings", "Folk & Free Reeds", "Woodwinds", "Brass", "Synths", "World", "Guitars & Plucked", "Mallets & Bells" };
        case I::piano: case I::organ:
            return { "Keys", "Mallets & Bells", "Guitars & Plucked", "Synths" };
        case I::strings:
            return { "Strings", "Choir & Pads", "Brass", "Woodwinds", "Keys", "Synths" };
        case I::drums: case I::trapDrums:
            return { "Drum Kits" };
        case I::count: break;
    }
    return {};
}
} // namespace

juce::String defaultInstrument (I role, G genre, const Library& lib)
{
    for (auto& id : preferences (role, genre))
        if (lib.find (id) != nullptr) return id;
    const auto c = candidates (role, lib);
    return c.empty() ? juce::String() : c.front()->id;
}

std::vector<const LibraryEntry*> candidates (I role, const Library& lib)
{
    std::vector<const LibraryEntry*> out;
    const auto fams = familiesFor (role);
    const bool drums = isDrumRole (role);
    for (auto& f : fams)
        for (auto& e : lib.getEntries())
            if (e.family == f && (drums ? e.kind == "drums" : e.kind == "pitched")) out.push_back (&e);
    if (! drums)   // anything else pitched comes after the best matches
        for (auto& e : lib.getEntries())
            if (e.kind == "pitched" && std::find (out.begin(), out.end(), &e) == out.end()) out.push_back (&e);
    return out;
}

void fitToRange (Track& t, int lo, int hi)
{
    if (t.notes.empty() || hi - lo < 11) return;
    std::vector<int> p;
    for (auto& n : t.notes) p.push_back (n.pitch);
    std::nth_element (p.begin(), p.begin() + (long) p.size() / 2, p.end());
    const int median = p[p.size() / 2];
    const int centre = (lo + hi) / 2;
    int shift = (int) std::lround ((centre - median) / 12.0) * 12;
    // only move when the part is actually out of range
    int outside = 0;
    for (auto& n : t.notes) outside += (n.pitch < lo || n.pitch > hi) ? 1 : 0;
    if (outside == 0) shift = 0;
    for (auto& n : t.notes)
    {
        int q = n.pitch + shift;
        while (q < lo) q += 12;
        while (q > hi) q -= 12;
        n.pitch = juce::jlimit (0, 127, q);
    }
}

static Track trackFromPart (const band::Part& part, const juce::String& instrumentId, const Library& lib, Song& song)
{
    Track t;
    t.bandRole = (int) part.instrument;
    t.instrumentId = instrumentId;
    const auto* e = lib.find (instrumentId);
    t.name = e != nullptr ? e->name : roleName (part.instrument);
    for (auto& bn : part.notes)
    {
        Note n;
        n.start = juce::jmax (0.0, bn.start);
        n.length = juce::jmax (1.0 / 64.0, bn.length);
        n.pitch = juce::jlimit (0, 127, bn.pitch);
        n.velocity = juce::jlimit (1, 127, (int) std::lround (bn.velocity * 127.0f));
        // composer articulations: muted / chop become short notes; slides and bends glide in
        if (bn.articulation == band::art::slideUp) n.glide = -2.0f;
        else if (bn.articulation == band::art::bend && ! isDrumRole (part.instrument)) n.glide = -1.0f;
        if ((bn.articulation == band::art::muted || bn.articulation == band::art::chop) && ! isDrumRole (part.instrument))
            n.length = juce::jmin (n.length, 0.2);
        n.id = song.newId();
        t.notes.push_back (n);
    }
    t.sortNotes();
    if (e != nullptr && e->kind == "pitched") fitToRange (t, e->rangeLo, e->rangeHi);
    return t;
}

Song makeSong (const band::Request& reqIn, const std::vector<RoleChoice>& roles, const Library& lib)
{
    band::Request req = reqIn;
    req.instruments.clear();
    for (auto& r : roles)
        if (r.on) req.instruments.push_back (r.role);
    const auto bs = band::compose (req);
    Song s;
    s.bpm = bs.bpm;
    s.beatsPerBar = bs.beatsPerBar;
    s.bandInfo = bs.describe();
    s.bandRequest = requestToVar (bs.request);
    for (auto& part : bs.parts)
    {
        juce::String id;
        for (auto& r : roles)
            if (r.role == part.instrument) { id = r.instrumentId; break; }
        if (id.isEmpty()) id = defaultInstrument (part.instrument, req.genre, lib);
        if ((int) s.tracks.size() < kMaxTracks) s.tracks.push_back (trackFromPart (part, id, lib, s));
    }
    return s;
}

bool rerollTrack (Song& s, int track, juce::uint32 seed, const Library& lib)
{
    if (track < 0 || track >= (int) s.tracks.size() || s.bandRequest.isVoid()) return false;
    auto& tr = s.tracks[(size_t) track];
    if (tr.bandRole < 0 || tr.bandRole >= band::kNumInstruments) return false;
    auto req = requestFromVar (s.bandRequest);
    const auto role = (I) tr.bandRole;
    req.partSeeds[(size_t) role] = seed == 0 ? 1u : seed;
    if (std::find (req.instruments.begin(), req.instruments.end(), role) == req.instruments.end()) req.instruments.push_back (role);
    const auto bs = band::compose (req);
    for (auto& part : bs.parts)
        if (part.instrument == role)
        {
            auto t = trackFromPart (part, tr.instrumentId, lib, s);
            tr.notes = std::move (t.notes);
            s.bandRequest = requestToVar (req);
            return true;
        }
    return false;
}

juce::var requestToVar (const band::Request& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("genre", (int) r.genre);
    o->setProperty ("key", r.keyRoot);
    o->setProperty ("minor", r.minor);
    o->setProperty ("bpm", r.bpm);
    o->setProperty ("full", r.fullSong);
    o->setProperty ("seed", (juce::int64) r.seed);
    juce::Array<juce::var> inst, seeds;
    for (auto i : r.instruments) inst.add ((int) i);
    for (auto s : r.partSeeds) seeds.add ((juce::int64) s);
    o->setProperty ("instruments", inst);
    o->setProperty ("partSeeds", seeds);
    return juce::var (o);
}

band::Request requestFromVar (const juce::var& v)
{
    band::Request r;
    r.genre = (G) juce::jlimit (0, band::kNumGenres - 1, (int) v.getProperty ("genre", 0));
    r.keyRoot = juce::jlimit (0, 11, (int) v.getProperty ("key", 7));
    r.minor = (bool) v.getProperty ("minor", false);
    r.bpm = (double) v.getProperty ("bpm", 0.0);
    r.fullSong = (bool) v.getProperty ("full", true);
    r.seed = (uint32_t) (juce::int64) v.getProperty ("seed", 1);
    if (auto* a = v["instruments"].getArray())
        for (auto& i : *a) r.instruments.push_back ((I) juce::jlimit (0, band::kNumInstruments - 1, (int) i));
    if (auto* a = v["partSeeds"].getArray())
        for (int i = 0; i < juce::jmin (a->size(), band::kNumInstruments); ++i) r.partSeeds[(size_t) i] = (uint32_t) (juce::int64) (*a)[i];
    return r;
}

juce::String sunoStyle (const Song& s, const Library& lib)
{
    juce::StringArray parts;
    juce::String genre = "instrumental";
    if (! s.bandRequest.isVoid())
    {
        const auto r = requestFromVar (s.bandRequest);
        genre = juce::String (band::genreName (r.genre)) + " instrumental";
        parts.add (juce::String (juce::roundToInt (s.bpm)) + " BPM");
        parts.add (juce::String (band::theory::keyName (r.keyRoot)) + (r.minor ? " minor" : " major"));
    }
    else
        parts.add (juce::String (juce::roundToInt (s.bpm)) + " BPM");
    juce::StringArray names;
    for (auto& t : s.tracks)
        if (const auto* e = lib.find (t.instrumentId)) names.addIfNotAlreadyThere (e->name.toLowerCase());
    return genre + ", " + parts.joinIntoString (", ") + (names.isEmpty() ? juce::String() : ", " + names.joinIntoString (", "))
           + ", live band feel, natural room, warm mix";
}
} // namespace hf::bandbridge
