/*
 * advapi32 entry point and the ansi string helpers.
 *
 * advapi32.def forwards the registry functions to kernelbase like windows does; security, cryptography, event
 * logging and the service control manager live in the other files.
 */
#include "advapi32_private.h"

PWSTR AdvAnsiToWide(LPCSTR s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    PWSTR w = n > 0 ? AdvAlloc(n * sizeof(WCHAR)) : nullptr;
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    else
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
    return w;
}

PSTR AdvWideToAnsi(LPCWSTR s)
{
    if (!s)
        return nullptr;
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
    PSTR a = n > 0 ? AdvAlloc(n) : nullptr;
    if (a)
        WideCharToMultiByte(CP_ACP, 0, s, -1, a, n, nullptr, nullptr);
    else
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
    return a;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
