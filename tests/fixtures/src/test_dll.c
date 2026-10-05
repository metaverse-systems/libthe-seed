/* A DLL with one import from kernel32.dll and one from msvcrt.dll, so that
 * the dependency lister has a known set of imports to find. */
#include <windows.h>
#include <string.h>

__declspec(dllexport) unsigned long test_tick_and_length(const char *text)
{
    return GetTickCount() + (unsigned long)strlen(text);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
