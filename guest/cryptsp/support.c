/* what the providers of mime's advapi32 take from the rest of it, where advapi32 is microsoft's. */
#ifndef __i386__
#include "../advapi32/advapi32_private.h"

DECLSPEC_IMPORT BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length);

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

/* windows' providers draw from the process generator of advapi32, RtlGenRandom. */
BOOL AdvGenRandom(PVOID buffer, SIZE_T length)
{
    return SystemFunction036(buffer, (ULONG)length);
}
#endif
