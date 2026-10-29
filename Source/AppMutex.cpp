// Named mutex the installer checks (Inno Setup AppMutex) so it never overwrites the app while it runs.
#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace hf
{
void createAppMutex()
{
   #if defined (_WIN32)
    // kept for the life of the process (closed by Windows when the app exits)
    static HANDLE h = CreateMutexW (nullptr, FALSE, L"HEMInstrumentForgeRunning");
    (void) h;
   #endif
}
} // namespace hf
