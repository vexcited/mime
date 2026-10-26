#include "shell32_private.h"

HINSTANCE g_shell32;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_shell32 = instance;
    return TRUE;
}
