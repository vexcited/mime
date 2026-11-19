/* kernelbase entry point. */
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
    }
    return TRUE;
}

/*
 * the loader of windows calls this once kernel32 and kernelbase are loaded and the process is set up, and ends the
 * process when it fails. measured on windows 11 26300: 0. what windows does here mime did when the dll attached.
 */
__declspec(dllexport) NTSTATUS WINAPI KernelbasePostInit(void)
{
    return STATUS_SUCCESS;
}
