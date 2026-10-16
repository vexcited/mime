/*
 * uuids: creation, string conversion and comparison.
 *
 * mime has no cryptographic random source yet, so random uuids come from a
 * xoshiro256** generator seeded from timers, ids and addresses, with the
 * performance counter mixed in at every call.
 */
#include "rpcrt4_private.h"

static SRWLOCK g_random_lock = SRWLOCK_INIT;
static ULONG64 g_state[4];
static BOOL g_seeded;
static ULONG64 g_last_time;
static USHORT g_clock_sequence;
static BYTE g_node[6];

static ULONG64 splitmix(ULONG64 *x)
{
    ULONG64 z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static ULONG64 rotl(ULONG64 x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static ULONG64 next_random(void)
{
    ULONG64 result = rotl(g_state[1] * 5, 7) * 9;
    ULONG64 t = g_state[1] << 17;
    g_state[2] ^= g_state[0];
    g_state[3] ^= g_state[1];
    g_state[1] ^= g_state[2];
    g_state[0] ^= g_state[3];
    g_state[2] ^= t;
    g_state[3] = rotl(g_state[3], 45);
    return result;
}

static ULONG64 cycle_counter(void)
{
#ifdef __aarch64__
    ULONG64 value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
    return 0;
#endif
}

static void seed(void)
{
    LARGE_INTEGER counter;
    FILETIME now;
    QueryPerformanceCounter(&counter);
    GetSystemTimeAsFileTime(&now);
    ULONG64 x = (ULONG64)counter.QuadPart;
    x ^= ((ULONG64)now.dwHighDateTime << 32 | now.dwLowDateTime) * 0x2545F4914F6CDD1Dull;
    x ^= (ULONG64)GetCurrentProcessId() << 48 ^ (ULONG64)GetCurrentThreadId() << 32;
    x ^= (ULONG64)(ULONG_PTR)&counter ^ rotl((ULONG64)(ULONG_PTR)GetProcessHeap(), 17);
    x ^= rotl(GetTickCount64(), 29) ^ rotl(cycle_counter(), 41);
    for (int i = 0; i < 4; i++)
        g_state[i] ^= splitmix(&x);
    g_seeded = TRUE;
}

static void random_bytes(BYTE *out, SIZE_T size)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    AcquireSRWLockExclusive(&g_random_lock);
    if (!g_seeded)
        seed();
    g_state[0] ^= (ULONG64)counter.QuadPart ^ cycle_counter();
    while (size) {
        ULONG64 r = next_random();
        SIZE_T n = size < 8 ? size : 8;
        memcpy(out, &r, n);
        out += n;
        size -= n;
    }
    ReleaseSRWLockExclusive(&g_random_lock);
}

RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreate(UUID *uuid)
{
    if (!uuid)
        return RPC_S_INVALID_ARG;
    random_bytes((BYTE *)uuid, sizeof(*uuid));
    uuid->Data3 = (USHORT)((uuid->Data3 & 0x0FFF) | 0x4000);
    uuid->Data4[0] = (BYTE)((uuid->Data4[0] & 0x3F) | 0x80);
    return RPC_S_OK;
}

/*
 * a version 1 uuid. there is no network adapter to take the node from, so
 * the node is random with the multicast bit set and the status says the
 * uuid is only unique on this machine, as windows does without an adapter.
 */
RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreateSequential(UUID *uuid)
{
    if (!uuid)
        return RPC_S_INVALID_ARG;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    /* 100 ns intervals since the gregorian reform, 1582-10-15. */
    ULONG64 time = ((ULONG64)now.dwHighDateTime << 32 | now.dwLowDateTime) + 0x01B21DD213814000ull;
    BYTE fresh[8];
    random_bytes(fresh, sizeof(fresh));
    AcquireSRWLockExclusive(&g_random_lock);
    if (!g_last_time) {
        g_clock_sequence = (USHORT)(fresh[0] | fresh[1] << 8);
        memcpy(g_node, fresh + 2, 6);
        g_node[0] |= 0x01;
    }
    if (time <= g_last_time)
        time = g_last_time + 1;
    g_last_time = time;
    uuid->Data1 = (ULONG)time;
    uuid->Data2 = (USHORT)(time >> 32);
    uuid->Data3 = (USHORT)(((time >> 48) & 0x0FFF) | 0x1000);
    uuid->Data4[0] = (BYTE)(((g_clock_sequence >> 8) & 0x3F) | 0x80);
    uuid->Data4[1] = (BYTE)g_clock_sequence;
    memcpy(uuid->Data4 + 2, g_node, 6);
    ReleaseSRWLockExclusive(&g_random_lock);
    return RPC_S_UUID_LOCAL_ONLY;
}

RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreateNil(UUID *uuid)
{
    if (!uuid)
        return RPC_S_INVALID_ARG;
    memset(uuid, 0, sizeof(*uuid));
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ strings */

static void format_uuid(const UUID *uuid, char *out)
{
    static const char hex[] = "0123456789abcdef";
    BYTE bytes[16];
    bytes[0] = (BYTE)(uuid->Data1 >> 24);
    bytes[1] = (BYTE)(uuid->Data1 >> 16);
    bytes[2] = (BYTE)(uuid->Data1 >> 8);
    bytes[3] = (BYTE)uuid->Data1;
    bytes[4] = (BYTE)(uuid->Data2 >> 8);
    bytes[5] = (BYTE)uuid->Data2;
    bytes[6] = (BYTE)(uuid->Data3 >> 8);
    bytes[7] = (BYTE)uuid->Data3;
    memcpy(bytes + 8, uuid->Data4, 8);
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            *out++ = '-';
        *out++ = hex[bytes[i] >> 4];
        *out++ = hex[bytes[i] & 15];
    }
    *out = 0;
}

static int hex_digit(unsigned c)
{
    if (c >= '0' && c <= '9')
        return (int)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (int)(c - 'A' + 10);
    return -1;
}

/* parses "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" from characters of either width. */
static RPC_STATUS parse_uuid(const void *string, SIZE_T width, UUID *uuid)
{
    if (!uuid)
        return RPC_S_INVALID_ARG;
    if (!string) {
        memset(uuid, 0, sizeof(*uuid));
        return RPC_S_OK;
    }
    unsigned chars[37];
    for (int i = 0; i < 37; i++) {
        chars[i] = width == 1 ? ((const BYTE *)string)[i] : ((const USHORT *)string)[i];
        if (!chars[i] && i < 36)
            return RPC_S_INVALID_STRING_UUID;
    }
    if (chars[36])
        return RPC_S_INVALID_STRING_UUID;
    BYTE bytes[16];
    int c = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (chars[c++] != '-')
                return RPC_S_INVALID_STRING_UUID;
        }
        int hi = hex_digit(chars[c]), lo = hex_digit(chars[c + 1]);
        if (hi < 0 || lo < 0)
            return RPC_S_INVALID_STRING_UUID;
        bytes[i] = (BYTE)(hi << 4 | lo);
        c += 2;
    }
    uuid->Data1 = (ULONG)bytes[0] << 24 | (ULONG)bytes[1] << 16 | (ULONG)bytes[2] << 8 | bytes[3];
    uuid->Data2 = (USHORT)(bytes[4] << 8 | bytes[5]);
    uuid->Data3 = (USHORT)(bytes[6] << 8 | bytes[7]);
    memcpy(uuid->Data4, bytes + 8, 8);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY UuidToStringA(const UUID *uuid, RPC_CSTR *string)
{
    static const UUID nil;
    if (!string)
        return RPC_S_INVALID_ARG;
    char *text = HeapAlloc(GetProcessHeap(), 0, 37);
    *string = (RPC_CSTR)text;
    if (!text)
        return RPC_S_OUT_OF_MEMORY;
    format_uuid(uuid ? uuid : &nil, text);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY UuidToStringW(const UUID *uuid, RPC_WSTR *string)
{
    static const UUID nil;
    if (!string)
        return RPC_S_INVALID_ARG;
    char text[37];
    format_uuid(uuid ? uuid : &nil, text);
    USHORT *wide = HeapAlloc(GetProcessHeap(), 0, sizeof(USHORT) * 37);
    *string = wide;
    if (!wide)
        return RPC_S_OUT_OF_MEMORY;
    for (int i = 0; i < 37; i++)
        wide[i] = (BYTE)text[i];
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY UuidFromStringA(RPC_CSTR string, UUID *uuid)
{
    return parse_uuid(string, 1, uuid);
}

RPCRTAPI RPC_STATUS RPC_ENTRY UuidFromStringW(RPC_WSTR string, UUID *uuid)
{
    return parse_uuid(string, 2, uuid);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringFreeA(RPC_CSTR *string)
{
    if (!string)
        return RPC_S_INVALID_ARG;
    if (*string)
        HeapFree(GetProcessHeap(), 0, *string);
    *string = nullptr;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringFreeW(RPC_WSTR *string)
{
    return RpcStringFreeA((RPC_CSTR *)string);
}

/* ------------------------------------------------------------------ comparison */

RPCRTAPI signed int RPC_ENTRY UuidCompare(UUID *a, UUID *b, RPC_STATUS *status)
{
    static const UUID nil;
    if (status)
        *status = RPC_S_OK;
    const UUID *x = a ? a : &nil, *y = b ? b : &nil;
    if (x->Data1 != y->Data1)
        return x->Data1 < y->Data1 ? -1 : 1;
    if (x->Data2 != y->Data2)
        return x->Data2 < y->Data2 ? -1 : 1;
    if (x->Data3 != y->Data3)
        return x->Data3 < y->Data3 ? -1 : 1;
    for (int i = 0; i < 8; i++)
        if (x->Data4[i] != y->Data4[i])
            return x->Data4[i] < y->Data4[i] ? -1 : 1;
    return 0;
}

RPCRTAPI int RPC_ENTRY UuidEqual(UUID *a, UUID *b, RPC_STATUS *status)
{
    return UuidCompare(a, b, status) == 0;
}

RPCRTAPI int RPC_ENTRY UuidIsNil(UUID *uuid, RPC_STATUS *status)
{
    return UuidCompare(uuid, nullptr, status) == 0;
}

/* a 16 bit fold of the uuid; the windows hash function is not reproduced. */
RPCRTAPI unsigned short RPC_ENTRY UuidHash(UUID *uuid, RPC_STATUS *status)
{
    if (status)
        *status = RPC_S_OK;
    if (!uuid)
        return 0;
    const BYTE *p = (const BYTE *)uuid;
    unsigned c0 = 0, c1 = 0;
    for (int i = 0; i < 16; i++) {
        c0 += p[i];
        c1 += c0;
    }
    return (unsigned short)((c1 & 0xFF) << 8 | (c0 & 0xFF));
}
