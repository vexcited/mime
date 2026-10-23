/* mfplat.dll, the media foundation platform: startup, time, frame rate conversions and transform enumeration. */
#include "mfplat_private.h"

/* floating point code references it and there is no crt to define it. */
int _fltused = 0x9875;

static LONG g_startups;
static LARGE_INTEGER g_time_base;
static LARGE_INTEGER g_time_frequency;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        QueryPerformanceFrequency(&g_time_frequency);
        QueryPerformanceCounter(&g_time_base);
    }
    return TRUE;
}

void *MfAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *MfAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void MfFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

void *WINAPI MFHeapAlloc(SIZE_T size, ULONG flags, char *file, int line, int type)
{
    return HeapAlloc(GetProcessHeap(), flags, size);
}

void WINAPI MFHeapFree(void *p)
{
    MfFree(p);
}

/* any version up to 2.x starts. MFSTARTUP_LITE only leaves out sockets. */
HRESULT WINAPI MFStartup(ULONG version, DWORD flags)
{
    if ((version & 0xFFFF0000) > 0x00020000)
        return MF_E_BAD_STARTUP_VERSION;
    InterlockedIncrement(&g_startups);
    return S_OK;
}

HRESULT WINAPI MFShutdown(void)
{
    LONG count = g_startups;
    while (count > 0) {
        LONG seen = InterlockedCompareExchange(&g_startups, count - 1, count);
        if (seen == count) {
            if (count == 1)
                MfWorkQueuesShutdown();
            break;
        }
        count = seen;
    }
    return S_OK;
}

BOOL MfStarted(void)
{
    return g_startups > 0;
}

HRESULT WINAPI MFLockPlatform(void)
{
    InterlockedIncrement(&g_startups);
    return S_OK;
}

HRESULT WINAPI MFUnlockPlatform(void)
{
    return MFShutdown();
}

/* (a * b + d) / c with a 128-bit product. */
LONGLONG WINAPI MFllMulDiv(LONGLONG a, LONGLONG b, LONGLONG c, LONGLONG d)
{
    if (!c)
        return (a < 0) != (b < 0) ? (LONGLONG)0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFLL;
    BOOL negative = (a < 0) != (b < 0);
    ULONGLONG ua = a < 0 ? 0 - (ULONGLONG)a : (ULONGLONG)a;
    ULONGLONG ub = b < 0 ? 0 - (ULONGLONG)b : (ULONGLONG)b;
    ULONGLONG a_lo = (DWORD)ua, a_hi = ua >> 32, b_lo = (DWORD)ub, b_hi = ub >> 32;
    ULONGLONG lo_lo = a_lo * b_lo, hi_lo = a_hi * b_lo, lo_hi = a_lo * b_hi, hi_hi = a_hi * b_hi;
    ULONGLONG middle = (lo_lo >> 32) + (DWORD)hi_lo + (DWORD)lo_hi;
    ULONGLONG low = (middle << 32) | (DWORD)lo_lo;
    ULONGLONG high = hi_hi + (hi_lo >> 32) + (lo_hi >> 32) + (middle >> 32);
    /* add d, keeping the result as a magnitude and a sign. */
    if (d) {
        BOOL d_negative = d < 0;
        ULONGLONG ud = d < 0 ? 0 - (ULONGLONG)d : (ULONGLONG)d;
        if (d_negative == negative) {
            ULONGLONG sum = low + ud;
            high += sum < low;
            low = sum;
        } else if (high || low >= ud) {
            high -= low < ud;
            low -= ud;
        } else {
            low = ud - low;
            negative = d_negative;
        }
    }
    BOOL c_negative = c < 0;
    ULONGLONG uc = c < 0 ? 0 - (ULONGLONG)c : (ULONGLONG)c;
    if (high >= uc)
        return negative != c_negative ? (LONGLONG)0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFLL;
    ULONGLONG quotient = 0, remainder = high;
    for (int bit = 63; bit >= 0; bit--) {
        BOOL carry = remainder >> 63;
        remainder = (remainder << 1) | ((low >> bit) & 1);
        quotient <<= 1;
        if (carry || remainder >= uc) {
            remainder -= uc;
            quotient |= 1;
        }
    }
    return negative != c_negative ? (LONGLONG)(0 - quotient) : (LONGLONG)quotient;
}

/* 100 ns units since the dll loaded. */
MFTIME WINAPI MFGetSystemTime(void)
{
    LARGE_INTEGER now;
    if (!QueryPerformanceCounter(&now) || !g_time_frequency.QuadPart)
        return 0;
    return MFllMulDiv(now.QuadPart - g_time_base.QuadPart, 10000000, g_time_frequency.QuadPart, 0);
}

/* windows rounds the frame times of these rates its own way. */
static const struct {
    UINT64 time;
    UINT32 numerator, denominator;
    double rate;
} COMMON_RATES[] = {
    {166833, 60000, 1001, 59.94}, {333667, 30000, 1001, 29.97}, {417188, 24000, 1001, 23.976},
    {166667, 60, 1, 60.0},        {333333, 30, 1, 30.0},        {200000, 50, 1, 50.0},
    {400000, 25, 1, 25.0},        {416667, 24, 1, 24.0},        {666667, 15, 1, 15.0},
};

HRESULT WINAPI MFFrameRateToAverageTimePerFrame(UINT32 numerator, UINT32 denominator, UINT64 *time)
{
    UINT64 result = 0;
    if (numerator && denominator) {
        double rate = (double)numerator / (double)denominator;
        for (UINT32 i = 0; i < RTL_NUMBER_OF(COMMON_RATES); i++) {
            if (rate >= COMMON_RATES[i].rate - 0.01 && rate <= COMMON_RATES[i].rate + 0.01) {
                result = COMMON_RATES[i].time;
                goto done;
            }
        }
        result = (UINT64)(LONGLONG)(10000000.0 / rate + 0.5);
    }
done:
    *time = result;
    return S_OK;
}

HRESULT WINAPI MFAverageTimePerFrameToFrameRate(UINT64 time, UINT32 *numerator, UINT32 *denominator)
{
    if (!time) {
        *numerator = *denominator = 0;
        return S_OK;
    }
    for (UINT32 i = 0; i < RTL_NUMBER_OF(COMMON_RATES); i++) {
        if (time + 30 >= COMMON_RATES[i].time && time <= COMMON_RATES[i].time + 30) {
            *numerator = COMMON_RATES[i].numerator;
            *denominator = COMMON_RATES[i].denominator;
            return S_OK;
        }
    }
    if (time >= 100000000) {
        *numerator = 1;
        *denominator = 10;
        return S_OK;
    }
    UINT32 a = 10000000, b = (UINT32)time;
    while (b) {
        UINT32 r = a % b;
        a = b;
        b = r;
    }
    *numerator = 10000000 / a;
    *denominator = (UINT32)time / a;
    return S_OK;
}

/* mime registers no transforms, so every enumeration is empty. */
HRESULT WINAPI MFTEnumEx(GUID category, UINT32 flags, const MFT_REGISTER_TYPE_INFO *input,
                         const MFT_REGISTER_TYPE_INFO *output, IMFActivate ***activates, UINT32 *count)
{
    if (!activates || !count)
        return E_POINTER;
    *count = 0;
    *activates = nullptr;
    if (flags & 0xEFFFF800)
        return HRESULT_FROM_WIN32(ERROR_INVALID_FLAGS);
    return S_OK;
}

HRESULT WINAPI MFTEnum2(GUID category, UINT32 flags, const MFT_REGISTER_TYPE_INFO *input,
                        const MFT_REGISTER_TYPE_INFO *output, IMFAttributes *attributes, IMFActivate ***activates,
                        UINT32 *count)
{
    return MFTEnumEx(category, flags, input, output, activates, count);
}

HRESULT WINAPI MFTEnum(GUID category, UINT32 flags, MFT_REGISTER_TYPE_INFO *input, MFT_REGISTER_TYPE_INFO *output,
                       IMFAttributes *attributes, CLSID **clsids, UINT32 *count)
{
    if (!clsids || !count)
        return E_POINTER;
    *count = 0;
    *clsids = nullptr;
    return S_OK;
}
