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

/* ---------------------------------------------------------------- sha-256 */

typedef struct SHA256 {
    DWORD State[8];
    ULONG64 Length;
    BYTE Block[64];
    DWORD Used;
} SHA256;

static const DWORD g_sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline DWORD ror32(DWORD v, int n)
{
    return v >> n | v << (32 - n);
}

static void sha256_block(SHA256 *c, const BYTE *p)
{
    DWORD w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (DWORD)p[4 * i] << 24 | (DWORD)p[4 * i + 1] << 16 | (DWORD)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        DWORD s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ w[i - 15] >> 3;
        DWORD s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    DWORD a = c->State[0], b = c->State[1], cc = c->State[2], d = c->State[3];
    DWORD e = c->State[4], f = c->State[5], g = c->State[6], h = c->State[7];
    for (int i = 0; i < 64; i++) {
        DWORD t1 = h + (ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25)) + ((e & f) ^ (~e & g)) + g_sha_k[i] + w[i];
        DWORD t2 = (ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = cc;
        cc = b;
        b = a;
        a = t1 + t2;
    }
    c->State[0] += a;
    c->State[1] += b;
    c->State[2] += cc;
    c->State[3] += d;
    c->State[4] += e;
    c->State[5] += f;
    c->State[6] += g;
    c->State[7] += h;
}

static void sha256_init(SHA256 *c)
{
    static const DWORD initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->State, initial, sizeof(initial));
    c->Length = 0;
    c->Used = 0;
}

static void sha256_update(SHA256 *c, const void *data, SIZE_T size)
{
    const BYTE *p = data;
    c->Length += size;
    while (size) {
        DWORD n = 64 - c->Used < size ? 64 - c->Used : (DWORD)size;
        memcpy(c->Block + c->Used, p, n);
        c->Used += n;
        p += n;
        size -= n;
        if (c->Used == 64) {
            sha256_block(c, c->Block);
            c->Used = 0;
        }
    }
}

static void sha256_final(SHA256 *c, BYTE out[32])
{
    ULONG64 bits = c->Length * 8;
    BYTE pad = 0x80;
    sha256_update(c, &pad, 1);
    pad = 0;
    while (c->Used != 56)
        sha256_update(c, &pad, 1);
    BYTE length[8];
    for (int i = 0; i < 8; i++)
        length[i] = (BYTE)(bits >> (56 - 8 * i));
    sha256_update(c, length, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (BYTE)(c->State[i] >> 24);
        out[4 * i + 1] = (BYTE)(c->State[i] >> 16);
        out[4 * i + 2] = (BYTE)(c->State[i] >> 8);
        out[4 * i + 3] = (BYTE)c->State[i];
    }
    SecureZeroMemory(c, sizeof(*c));
}

/* ---------------------------------------------------------------- chacha20 */

static inline DWORD rol32(DWORD v, int n)
{
    return v << n | v >> (32 - n);
}

#define QUARTER(a, b, c, d)                                                                                       \
    a += b, d = rol32(d ^ a, 16), c += d, b = rol32(b ^ c, 12), a += b, d = rol32(d ^ a, 8), c += d,               \
    b = rol32(b ^ c, 7)

static void chacha20_block(const BYTE key[32], DWORD counter, const DWORD nonce[3], BYTE out[64])
{
    DWORD in[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; i++)
        in[4 + i] = (DWORD)key[4 * i] | (DWORD)key[4 * i + 1] << 8 | (DWORD)key[4 * i + 2] << 16 |
                    (DWORD)key[4 * i + 3] << 24;
    in[12] = counter;
    in[13] = nonce[0];
    in[14] = nonce[1];
    in[15] = nonce[2];
    DWORD x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QUARTER(x[0], x[4], x[8], x[12]);
        QUARTER(x[1], x[5], x[9], x[13]);
        QUARTER(x[2], x[6], x[10], x[14]);
        QUARTER(x[3], x[7], x[11], x[15]);
        QUARTER(x[0], x[5], x[10], x[15]);
        QUARTER(x[1], x[6], x[11], x[12]);
        QUARTER(x[2], x[7], x[8], x[13]);
        QUARTER(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        DWORD v = x[i] + in[i];
        out[4 * i] = (BYTE)v;
        out[4 * i + 1] = (BYTE)(v >> 8);
        out[4 * i + 2] = (BYTE)(v >> 16);
        out[4 * i + 3] = (BYTE)(v >> 24);
    }
    SecureZeroMemory(x, sizeof(x));
    SecureZeroMemory(in, sizeof(in));
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
        InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
        NTSTATUS status = NtOpenFile(&g_rng.Device, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
        g_rng.DeviceState = NT_SUCCESS(status) ? 1 : 2;
    }
    if (g_rng.DeviceState != 1)
        return FALSE;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtDeviceIoControlFile(g_rng.Device, NULL, NULL, NULL, &iosb, IOCTL_KSEC_RNG, NULL, 0, out, size);
    if (NT_SUCCESS(status) && iosb.Information == size)
        return TRUE;
    NtClose(g_rng.Device);
    g_rng.Device = NULL;
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
    if (!RegQueryValueExW(key, L"MachineGuid", NULL, NULL, (LPBYTE)guid, &size))
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
