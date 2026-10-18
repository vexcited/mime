/*
 * ProcessPrng, the random source of the process. windows keeps an aes
 * counter generator per processor seeded by the kernel; mime takes the
 * processor's own generator when it has one and otherwise the process
 * generator of advapi32, which asks the kernel first.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

#if defined(__x86_64__) || defined(__i386__)
static BOOL has_rdrand(void)
{
    static LONG known = -1;
    if (known < 0) {
        unsigned a = 1, b, c, d;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d) : "c"(0));
        known = (c >> 30) & 1;
    }
    return known;
}

/* rdrand may come back empty for a moment, a few retries cover it. */
__attribute__((target("rdrnd"))) static BOOL rdrand_fill(BYTE *buffer, SIZE_T size)
{
    while (size) {
        unsigned long long value = 0;
        int ok = 0;
        for (int tries = 0; tries < 16 && !ok; tries++) {
#ifdef __x86_64__
            ok = __builtin_ia32_rdrand64_step(&value);
#else
            unsigned int low = 0, high = 0;
            ok = __builtin_ia32_rdrand32_step(&low) && __builtin_ia32_rdrand32_step(&high);
            value = (unsigned long long)high << 32 | low;
#endif
        }
        if (!ok)
            return FALSE;
        SIZE_T n = size < 8 ? size : 8;
        memcpy(buffer, &value, n);
        buffer += n;
        size -= n;
    }
    return TRUE;
}
#endif

static BOOL process_fill(BYTE *buffer, SIZE_T size)
{
    typedef BOOLEAN(WINAPI * RANDOM)(void *, ULONG);
    static RANDOM random;
    if (!random) {
        HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
        random = advapi32 ? (RANDOM)GetProcAddress(advapi32, "SystemFunction036") : nullptr;
        if (!random)
            return FALSE;
    }
    while (size) {
        ULONG n = size > 0x10000 ? 0x10000 : (ULONG)size;
        if (!random(buffer, n))
            return FALSE;
        buffer += n;
        size -= n;
    }
    return TRUE;
}

BOOL WINAPI ProcessPrng(BYTE *buffer, SIZE_T size)
{
#if defined(__x86_64__) || defined(__i386__)
    if (has_rdrand() && rdrand_fill(buffer, size))
        return TRUE;
#endif
    return process_fill(buffer, size);
}

/* a random guid of version 4. */
BOOL WINAPI ProcessPrngGuid(GUID *guid)
{
    if (!ProcessPrng((BYTE *)guid, sizeof(*guid)))
        return FALSE;
    guid->Data3 = (WORD)((guid->Data3 & 0x0FFF) | 0x4000);
    guid->Data4[0] = (BYTE)((guid->Data4[0] & 0x3F) | 0x80);
    return TRUE;
}
