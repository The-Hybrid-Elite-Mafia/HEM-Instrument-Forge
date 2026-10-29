// HEM Instrument Forge unit test runner + allocation tracker.
//
// Allocation tracking: global operator new/delete are replaced for this test
// executable only. Counting is per-thread (thread_local flag) so allocations by
// unrelated JUCE threads cannot produce false positives. On MSVC Debug builds a
// CRT alloc hook additionally catches raw malloc/realloc from the tracked thread.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cstdlib>
#include <new>

#if defined (_MSC_VER) && defined (_DEBUG)
 #include <crtdbg.h>
#endif

namespace hemtest
{
    thread_local bool trackThisThread = false;
    std::atomic<int> allocationCount { 0 };

    void beginTracking() { allocationCount.store (0); trackThisThread = true; }
    int  endTracking()   { trackThisThread = false; return allocationCount.load(); }
    void addToTracked (int n) { allocationCount.fetch_add (n); }
}

static void* countedAlloc (std::size_t size)
{
    if (hemtest::trackThisThread)
        hemtest::allocationCount.fetch_add (1);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t size)                                   { return countedAlloc (size); }
void* operator new[] (std::size_t size)                                 { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept   { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                                 { std::free (p); }
void operator delete[] (void* p) noexcept                               { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                    { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                  { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept          { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept        { std::free (p); }

#if defined (_MSC_VER) && defined (_DEBUG)
static int crtAllocHook (int type, void*, size_t, int, long, const unsigned char*, int)
{
    if (hemtest::trackThisThread && (type == _HOOK_ALLOC || type == _HOOK_REALLOC))
        hemtest::allocationCount.fetch_add (1);
    return TRUE;
}
#endif

int main (int argc, char** argv)
{
#if defined (_MSC_VER) && defined (_DEBUG)
    _CrtSetAllocHook (crtAllocHook);
#endif

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);

    // HEMTests            -> every HEM test
    // HEMTests <category> -> one category
    // HEMTests name:<a,b> -> only the named tests (e.g. name:Suno Fix,Beat Forge)
    if (argc > 1 && juce::String (argv[1]).startsWith ("name:"))
    {
        const auto wanted = juce::StringArray::fromTokens (juce::String (argv[1]).fromFirstOccurrenceOf ("name:", false, false), ",", "");
        juce::Array<juce::UnitTest*> picked;
        for (auto* t : juce::UnitTest::getAllTests())
            if (wanted.contains (t->getName()))
                picked.add (t);
        runner.runTests (picked);
    }
    else if (argc > 1)
        runner.runTestsInCategory (argv[1]);
    else
        runner.runTestsInCategory ("Forge");

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
        std::printf ("[%s] %-28s / %-40s passes=%d failures=%d\n",
                     r->failures == 0 ? " OK " : "FAIL",
                     r->unitTestName.toRawUTF8(), r->subcategoryName.toRawUTF8(), r->passes, r->failures);
        for (const auto& m : r->messages)
            std::printf ("       %s\n", m.toRawUTF8());
    }

    std::printf ("\nHEM TEST SUMMARY: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
