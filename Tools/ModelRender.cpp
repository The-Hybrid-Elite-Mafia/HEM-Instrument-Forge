// ModelRender: turns every modeled instrument into library folders (24-bit FLAC + instrument.json).
//   ModelRender --out DIR [--only id,id] [--jobs N]
//   ModelRender --list
#include "Models/ModelCatalog.h"
#include <iostream>

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));

    auto value = [&] (const char* key) { const int i = args.indexOf (key); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };

    if (args.contains ("--list"))
    {
        for (auto& m : hf::models::catalog())
            std::cout << m.id << " | " << m.name << " | " << m.family << " | " << (int) m.arts.size() << " articulation(s)\n";
        return 0;
    }
    const auto outPath = value ("--out");
    if (outPath.isEmpty() || ! juce::File::isAbsolutePath (juce::File::getCurrentWorkingDirectory().getChildFile (outPath).getFullPathName()))
    {
        std::cerr << "usage: ModelRender --out DIR [--only id,id] [--jobs N] | --list\n";
        return 2;
    }
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
    out.createDirectory();

    juce::StringArray only;
    only.addTokens (value ("--only"), ",", {});
    only.removeEmptyStrings();
    for (auto& id : only)
        if (hf::models::find (id) == nullptr) { std::cerr << "unknown model instrument: " << id << "\n"; return 2; }

    const int jobs = juce::jlimit (1, 64, value ("--jobs").isNotEmpty() ? value ("--jobs").getIntValue() : juce::SystemStats::getNumCpus());
    // family folders first (the render threads only make their own instrument folder)
    for (auto& m : hf::models::catalog())
        out.getChildFile (hf::models::folderOf (m)).getParentDirectory().createDirectory();
    juce::ThreadPool pool (juce::ThreadPoolOptions{}.withNumberOfThreads (jobs).withThreadStackSizeBytes (8 * 1024 * 1024));
    std::atomic<int> failed { 0 };
    juce::CriticalSection printLock;   // tool only (not audio): keeps lines whole
    juce::StringArray report;

    for (auto& m : hf::models::catalog())
    {
        if (! only.isEmpty() && ! only.contains (m.id)) continue;
        pool.addJob ([&m, &out, &failed, &printLock, &report]
        {
            const auto r = hf::models::renderInstrument (m, out);
            const juce::ScopedLock sl (printLock);
            std::cout << (r.ok ? "" : "FAILED: ") << r.message << std::endl;
            report.add (r.message);
            if (! r.ok) ++failed;
        });
    }
    while (pool.getNumJobs() > 0) juce::Thread::sleep (200);

    report.sort (true);
    out.getChildFile ("MODEL-REPORT.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
    std::cout << (failed.load() == 0 ? "all modeled instruments rendered" : juce::String (failed.load()) + " instrument(s) failed") << std::endl;
    return failed.load() == 0 ? 0 : 1;
}
