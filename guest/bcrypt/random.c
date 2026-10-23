/*
 * BCryptGenRandom and the crypto library hooks. bytes come from advapi32's generator, which reads the kernel's
 * \Device\CNG when it exists, the same source windows seeds from.
 */
#include "bcrypt_private.h"

typedef BOOLEAN(WINAPI *RTL_GEN_RANDOM)(PVOID, ULONG);

static INIT_ONCE g_random_once = INIT_ONCE_STATIC_INIT;
static RTL_GEN_RANDOM g_random;

static BOOL WINAPI load_random(INIT_ONCE *once, void *param, void **context)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32)
        g_random = (RTL_GEN_RANDOM)(void *)GetProcAddress(advapi32, "SystemFunction036");
    return TRUE;
}

BOOL BcRandom(void *buffer, SIZE_T size)
{
    InitOnceExecuteOnce(&g_random_once, load_random, nullptr, nullptr);
    UCHAR *p = buffer;
    while (size) {
        ULONG n = size > 0x10000 ? 0x10000 : (ULONG)size;
        if (!g_random || !g_random(p, n))
            return FALSE;
        p += n;
        size -= n;
    }
    return TRUE;
}

void *mime_crypto_alloc(size_t size)
{
    return BcAlloc(size);
}

void mime_crypto_free(void *p)
{
    BcFree(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    return BcRandom(out, size);
}

BCRYPTAPI NTSTATUS WINAPI BCryptGenRandom(BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer,
                                          ULONG dwFlags)
{
    if (dwFlags & BCRYPT_USE_SYSTEM_PREFERRED_RNG) {
        if (hAlgorithm)
            return STATUS_INVALID_PARAMETER;
        dwFlags &= ~BCRYPT_USE_SYSTEM_PREFERRED_RNG;
    } else {
        ALG *alg = BcAlg(hAlgorithm);
        if (!alg || alg->info->iface != BCRYPT_RNG_INTERFACE)
            return STATUS_INVALID_HANDLE;
    }
    /* the entropy a caller offers in the buffer is ignored since windows 8. */
    if (dwFlags & ~BCRYPT_RNG_USE_ENTROPY_IN_BUFFER || !pbBuffer)
        return STATUS_INVALID_PARAMETER;
    return BcRandom(pbBuffer, cbBuffer) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
