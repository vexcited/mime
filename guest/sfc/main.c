/*
 * sfc.dll, the entry points of Windows Resource Protection. Everything forwards to sfc_os.dll, which holds the code, as
 * the dll of windows does (see sfc.def and docs/sfc.md).
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
