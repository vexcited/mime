/*
 * sechost.dll forwards to advapi32.dll, which holds what windows keeps here (see sechost.def and docs/sechost.md).
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
