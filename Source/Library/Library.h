#pragma once

#include "Library/InstrumentDef.h"

namespace hf
{
/** One row of the library's index.json (enough to list and pick an instrument without loading it). */
struct LibraryEntry
{
    juce::String id, name, family, kind, folder, source, licence;
    int rangeLo = 0, rangeHi = 127;
    juce::StringArray articulations;
    int zones = 0;
    juce::int64 bytes = 0;
    bool mine = false;            // one of the user's own sounds (My Sounds)
};

/**
    The installed HEM Instrument Library: a folder with index.json and one sub-folder per instrument.
    Message thread (or a worker thread for open()); the object is immutable after open().
*/
class Library
{
public:
    /** Reads index.json. False + error if the folder is not a library. */
    bool open (const juce::File& folder, juce::String& error);
    /** Adds the instruments of a second folder with its own index.json (My Sounds). Ids already in the
        library are skipped. Returns how many were added. Call after open(). */
    int addFolder (const juce::File&);
    bool isOpen() const noexcept { return ! entries.empty(); }
    juce::File getFolder() const { return folder; }

    const std::vector<LibraryEntry>& getEntries() const noexcept { return entries; }
    const juce::StringArray& getFamilies() const noexcept { return families; }
    const LibraryEntry* find (const juce::String& id) const;
    juce::File folderOf (const LibraryEntry&) const;
    juce::int64 totalBytes() const;

    /** Where the app looks, in order: the folder chosen in settings, <app>/Library,
        Documents/Hybrid Elite Mafia/Instrument Forge/Library, and the HEM_LIBRARY environment variable. */
    static juce::Array<juce::File> searchFolders (const juce::String& chosenFolder);

private:
    juce::File folder;
    std::vector<LibraryEntry> entries;
    juce::StringArray families;
};
} // namespace hf
