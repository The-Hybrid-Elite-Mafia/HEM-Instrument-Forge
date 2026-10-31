#include "Library/Library.h"

namespace hf
{
namespace
{
/** Reads <root>/index.json. `absolute` makes each entry's folder a full path (for My Sounds, which lives elsewhere). */
bool readIndex (const juce::File& f, bool absolute, std::vector<LibraryEntry>& entries, juce::StringArray& families, juce::String& error)
{
    const auto json = juce::JSON::parse (f.getChildFile ("index.json"));
    if (! json.isObject() || json["format"].toString() != "hem-instrument-library")
    {
        error = "No HEM Instrument Library in " + f.getFullPathName();
        return false;
    }
    if (auto* fams = json["families"].getArray())
        for (auto& v : *fams) families.addIfNotAlreadyThere (v.toString());
    if (auto* items = json["instruments"].getArray())
        for (auto& v : *items)
        {
            LibraryEntry e;
            e.id = v["id"].toString();
            e.name = v["name"].toString();
            e.family = v["family"].toString();
            e.kind = v["kind"].toString();
            e.folder = v["folder"].toString();
            e.source = v["source"].toString();
            e.licence = v["licence"].toString();
            if (auto* r = v["range"].getArray(); r != nullptr && r->size() == 2) { e.rangeLo = (int) (*r)[0]; e.rangeHi = (int) (*r)[1]; }
            if (auto* a = v["articulations"].getArray())
                for (auto& n : *a) e.articulations.add (n.toString());
            e.zones = (int) v["zones"];
            e.bytes = (juce::int64) v["bytes"];
            if (e.id.isEmpty() || e.folder.isEmpty() || e.folder.contains ("..") || juce::File::isAbsolutePath (e.folder)) continue;
            if (! f.getChildFile (e.folder).getChildFile ("instrument.json").existsAsFile()) continue;   // family not installed
            if (absolute) e.folder = f.getChildFile (e.folder).getFullPathName();
            e.mine = absolute;
            if (! families.contains (e.family)) families.add (e.family);
            entries.push_back (std::move (e));
        }
    return true;
}
} // namespace

bool Library::open (const juce::File& f, juce::String& error)
{
    entries.clear();
    families.clear();
    folder = f;
    if (! readIndex (f, false, entries, families, error)) return false;
    if (entries.empty())
    {
        error = "The library in " + f.getFullPathName() + " lists no installed instruments";
        return false;
    }
    // keep only families that have something installed
    juce::StringArray used;
    for (auto& fam : families)
        for (auto& e : entries)
            if (e.family == fam) { used.add (fam); break; }
    families = used;
    return true;
}

int Library::addFolder (const juce::File& mine)
{
    if (! mine.getChildFile ("index.json").existsAsFile()) return 0;
    std::vector<LibraryEntry> extra;
    juce::StringArray fams;
    juce::String err;
    if (! readIndex (mine, true, extra, fams, err)) return 0;
    int added = 0;
    for (auto& e : extra)
    {
        if (find (e.id) != nullptr) continue;   // never hide or replace a library instrument
        if (! families.contains (e.family)) families.add (e.family);
        entries.push_back (std::move (e));
        ++added;
    }
    return added;
}

const LibraryEntry* Library::find (const juce::String& id) const
{
    for (auto& e : entries)
        if (e.id == id) return &e;
    return nullptr;
}

juce::File Library::folderOf (const LibraryEntry& e) const { return folder.getChildFile (e.folder); }

juce::int64 Library::totalBytes() const
{
    juce::int64 t = 0;
    for (auto& e : entries) t += e.bytes;
    return t;
}

juce::Array<juce::File> Library::searchFolders (const juce::String& chosen)
{
    juce::Array<juce::File> list;
    if (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen)) list.add (juce::File (chosen));
    list.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("Library"));
    list.add (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hybrid Elite Mafia")
                  .getChildFile ("Instrument Forge").getChildFile ("Library"));
    const auto env = juce::SystemStats::getEnvironmentVariable ("HEM_LIBRARY", {});
    if (env.isNotEmpty() && juce::File::isAbsolutePath (env)) list.add (juce::File (env));
    return list;
}
} // namespace hf
