/*
 * kernel32 forwards almost everything to kernelbase; it keeps the thread start thunk.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* ntdll starts every user thread here. */
__declspec(dllexport) void WINAPI BaseThreadInitThunk(ULONG flags, LPTHREAD_START_ROUTINE entry, PVOID parameter)
{
    ExitThread(entry(parameter));
}
