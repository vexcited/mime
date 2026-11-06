#include "oleaut32_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) AutErrorAttach();
    else if (reason == DLL_THREAD_DETACH || reason == DLL_PROCESS_DETACH) AutErrorDetach();
    if (reason == DLL_PROCESS_DETACH && !reserved) AutErrorFree();
    return TRUE;
}
