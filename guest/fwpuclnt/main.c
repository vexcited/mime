#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/*
 * ws2_32 hands every dns answer to the NameSpace_Callout of the winsock catalog. on windows it tells the filtering
 * engine about the addresses when the engine's name cache option is on, which it is not by default, and answers 0.
 * mime has no filtering engine, so the option is never on.
 */
DWORD WINAPI NamespaceCallout(PCWSTR name, DWORD count, PVOID addresses)
{
    return 0;
}
