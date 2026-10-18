/*
 * kernelbase entry point.
 */
#include "kernelbase_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        KbInitializeNls();
        KbInitializeCommandLine();
        KbInitializeConsole();
        KbInitializeDebug();
        KbThreadAttach();
        KbInitializeApc();
        KbInitializeLongPaths();
        break;
    case DLL_THREAD_ATTACH:
        KbThreadAttach();
        break;
    case DLL_THREAD_DETACH:
        KbThreadDetach();
        break;
    case DLL_PROCESS_DETACH:
        KbProcessDetach();
        break;
    }
    return TRUE;
}
