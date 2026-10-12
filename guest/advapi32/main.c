/*
 * advapi32 entry point and the string helpers of the ansi functions.
 *
 * the registry functions are forwarded to kernelbase by advapi32.def, like
 * windows does; security, cryptography, event logging and the service
 * control manager live in the other files.
 */
#include "advapi32_private.h"

PWSTR AdvAnsiToWide(LPCSTR s)
{
    if (!s)
        return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    PWSTR w = n > 0 ? AdvAlloc(n * sizeof(WCHAR)) : NULL;
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    else
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
    return w;
}

PSTR AdvWideToAnsi(LPCWSTR s)
{
    if (!s)
        return NULL;
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL);
    PSTR a = n > 0 ? AdvAlloc(n) : NULL;
    if (a)
        WideCharToMultiByte(CP_ACP, 0, s, -1, a, n, NULL, NULL);
    else
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
    return a;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
