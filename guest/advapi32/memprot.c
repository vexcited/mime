/*
 * RtlEncryptMemory and RtlDecryptMemory under their old names SystemFunction040 and 041, which SecureString uses.
 * windows reaches them through cryptbase; here CryptProtectMemory of crypt32 does the work, which needs blocks of 16
 * bytes. The memory of these functions needs blocks of 8, so a length that is 8 more than a multiple of 16 is
 * done as two overlapping runs, and 8 bytes alone are masked with a block made from the same key.
 */
#include "advapi32_private.h"

typedef BOOL(WINAPI *PROTECT_MEMORY)(LPVOID data, DWORD size, DWORD flags);

#define MEMORY_BLOCK 16
#define MEMORY_SAME_LOGON 2

static NTSTATUS protect_memory(PBYTE memory, ULONG length, ULONG flags, BOOL encrypt)
{
    if (flags > MEMORY_SAME_LOGON || length % 8)
        return STATUS_INVALID_PARAMETER;
    if (!length)
        return STATUS_SUCCESS;
    if (!memory)
        return STATUS_ACCESS_VIOLATION;
    HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
    PROTECT_MEMORY function = crypt32 ? (PROTECT_MEMORY)(void *)GetProcAddress(crypt32, encrypt ? "CryptProtectMemory" : "CryptUnprotectMemory") : nullptr;
    PROTECT_MEMORY forward = crypt32 ? (PROTECT_MEMORY)(void *)GetProcAddress(crypt32, "CryptProtectMemory") : nullptr;
    if (!function || !forward)
        return STATUS_UNSUCCESSFUL;
    BOOL ok;
    if (length % MEMORY_BLOCK == 0) {
        ok = function(memory, length, flags);
    } else if (length == 8) {
        /* the same key twice gives the same block, and a mask is its own inverse. */
        BYTE mask[MEMORY_BLOCK] = {};
        ok = forward(mask, sizeof(mask), flags);
        for (ULONG i = 0; i < 8; i++)
            memory[i] ^= mask[i];
    } else if (encrypt) {
        ok = function(memory, length - 8, flags) && function(memory + length - MEMORY_BLOCK, MEMORY_BLOCK, flags);
    } else {
        ok = function(memory + length - MEMORY_BLOCK, MEMORY_BLOCK, flags) && function(memory, length - 8, flags);
    }
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

ADVAPI NTSTATUS WINAPI SystemFunction040(PVOID memory, ULONG length, ULONG flags)
{
    return protect_memory(memory, length, flags, TRUE);
}

ADVAPI NTSTATUS WINAPI SystemFunction041(PVOID memory, ULONG length, ULONG flags)
{
    return protect_memory(memory, length, flags, FALSE);
}
