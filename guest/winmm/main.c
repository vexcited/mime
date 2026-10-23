#define MIME_BUILD_WINMM
#include "winmm_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        WinmmModule = instance;
    else if (reason == DLL_PROCESS_DETACH && !reserved)
        WinmmJoyDetach();
    return TRUE;
}
