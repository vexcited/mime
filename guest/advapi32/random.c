/*
 * the process random generator behind SystemFunction036 (RtlGenRandom) and
 * CryptGenRandom.
 *
 * windows draws these bytes from bcryptprimitives, which the kernel seeds
 * through \Device\CNG. the generator here asks that device first, so it
 * uses the kernel's randomness as soon as mime's kernel provides one. mime's
 * kernel has no entropy source yet, so until then the seed is the best the
 * guest offers: the jitter of the performance counter around system calls,
 * the time, the identity of the process and thread, addresses, the system
 * counters and the machine guid the host drew at installation. the bytes are
 * then produced by chacha20 with fast key erasure, keyed by sha-256 of that
 * pool, and reseeded with fresh timings as the process runs.
 */
#include "advapi32_private.h"
#include <crypto/crypto.h>

/* ---------------------------------------------------------------- sha-256 and chacha20 */

typedef MimeHashCtx SHA256;

static void sha256_init(SHA256 *c)
{
    mime_sha256.init(c);
}

static void sha256_update(SHA256 *c, const void *data, SIZE_T size)
{
    mime_sha256.update(c, data, size);
}

static void sha256_final(SHA256 *c, BYTE out[32])
{
    mime_sha256.final(c, out);
    SecureZeroMemory(c, sizeof(*c));
}

static void chacha20_block(const BYTE key[32], DWORD counter, const DWORD nonce[3], BYTE out[64])
{
    mime_chacha20_block(key, counter, (const uint8_t *)nonce, out);
}

/* ---------------------------------------------------------------- entropy */

/* IOCTL_KSEC_RNG of \Device\CNG, what bcryptprimitives seeds from. */
#define IOCTL_KSEC_RNG 0x00390004
#define RESEED_INTERVAL 64

static struct {
    RTL_SRWLOCK Lock;
    BOOL Seeded;
    BYTE Key[32];
    ULONG64 Requests;
    HANDLE Device;
    LONG DeviceState; /* 0 untried, 1 present, 2 absent */
} g_rng;

static ULONG64 counter(void)
{
    LARGE_INTEGER c;
    RtlQueryPerformanceCounter(&c);
    return (ULONG64)c.QuadPart;
}

/* random bytes from the kernel, when it offers them. */
static BOOL kernel_random(BYTE *out, ULONG size)
{
    if (g_rng.DeviceState == 0) {
        UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\CNG");
        OBJECT_ATTRIBUTES oa;
        IO_STATUS_BLOCK iosb;
        InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        NTSTATUS status = NtOpenFile(&g_rng.Device, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
        g_rng.DeviceState = NT_SUCCESS(status) ? 1 : 2;
    }
    if (g_rng.DeviceState != 1)
        return FALSE;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtDeviceIoControlFile(g_rng.Device, nullptr, nullptr, nullptr, &iosb, IOCTL_KSEC_RNG, nullptr, 0, out, size);
    if (NT_SUCCESS(status) && iosb.Information == size)
        return TRUE;
    NtClose(g_rng.Device);
    g_rng.Device = nullptr;
    g_rng.DeviceState = 2;
    return FALSE;
}

/* the timing of system calls: the host schedules the virtual processors, so the low bits wander. */
static void add_jitter(SHA256 *pool, int rounds)
{
    for (int i = 0; i < rounds; i++) {
        ULONG64 before = counter();
        LARGE_INTEGER t;
        if (i & 1)
            NtYieldExecution();
        else
            NtQuerySystemTime(&t);
        ULONG64 after = counter();
        ULONG64 sample[2] = {after - before, after};
        sha256_update(pool, sample, sizeof(sample));
    }
}

static void add_system_state(SHA256 *pool)
{
    BYTE buffer[0x158];
    ULONG n = 0;
    if (NT_SUCCESS(NtQuerySystemInformation(SystemPerformanceInformation, buffer, sizeof(buffer), &n)))
        sha256_update(pool, buffer, n);
    if (NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, buffer, sizeof(buffer), &n)))
        sha256_update(pool, buffer, n);
    if (NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, buffer, sizeof(buffer), &n)))
        sha256_update(pool, buffer, n);
    LONGLONG precise = RtlGetSystemTimePrecise();
    sha256_update(pool, &precise, sizeof(precise));
    PTEB teb = NtCurrentTeb();
    const void *places[] = {teb, teb->ProcessEnvironmentBlock, &places, pool, &g_rng,
                            teb->ProcessEnvironmentBlock->ProcessParameters};
    sha256_update(pool, places, sizeof(places));
    sha256_update(pool, &teb->ClientId, sizeof(teb->ClientId));
}

/* the guid the host drew for this installation: secret to nobody, but different on every machine. */
static void add_machine_guid(SHA256 *pool)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE, &key))
        return;
    WCHAR guid[64];
    DWORD size = sizeof(guid);
    if (!RegQueryValueExW(key, L"MachineGuid", nullptr, nullptr, (LPBYTE)guid, &size))
        sha256_update(pool, guid, size);
    RegCloseKey(key);
}

static void seed(BOOL first)
{
    SHA256 pool;
    sha256_init(&pool);
    sha256_update(&pool, g_rng.Key, sizeof(g_rng.Key));
    BYTE kernel[32];
    if (kernel_random(kernel, sizeof(kernel))) {
        sha256_update(&pool, kernel, sizeof(kernel));
        SecureZeroMemory(kernel, sizeof(kernel));
    } else {
        add_system_state(&pool);
        if (first)
            add_machine_guid(&pool);
        add_jitter(&pool, first ? 512 : 32);
    }
    sha256_final(&pool, g_rng.Key);
    g_rng.Seeded = TRUE;
}

BOOL AdvGenRandom(PVOID buffer, SIZE_T length)
{
    RtlAcquireSRWLockExclusive(&g_rng.Lock);
    if (!g_rng.Seeded || g_rng.Requests % RESEED_INTERVAL == 0)
        seed(!g_rng.Seeded);
    ULONG64 request = ++g_rng.Requests, now = counter();
    DWORD nonce[3] = {(DWORD)request, (DWORD)(request >> 32), (DWORD)now};
    BYTE block[64], next[32];
    BYTE *out = buffer;
    /* the first half of block 0 becomes the next key, so earlier outputs cannot be recovered. */
    chacha20_block(g_rng.Key, 0, nonce, block);
    memcpy(next, block, 32);
    SIZE_T n = length < 32 ? length : 32;
    memcpy(out, block + 32, n);
    out += n;
    length -= n;
    for (DWORD i = 1; length; i++) {
        chacha20_block(g_rng.Key, i, nonce, block);
        n = length < 64 ? length : 64;
        memcpy(out, block, n);
        out += n;
        length -= n;
    }
    memcpy(g_rng.Key, next, 32);
    RtlReleaseSRWLockExclusive(&g_rng.Lock);
    SecureZeroMemory(block, sizeof(block));
    SecureZeroMemory(next, sizeof(next));
    return TRUE;
}

/* RtlGenRandom. */
ADVAPI BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length)
{
    if (!buffer && length)
        return FALSE;
    return (BOOLEAN)AdvGenRandom(buffer, length);
}
