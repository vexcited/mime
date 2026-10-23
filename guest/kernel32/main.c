/* kernel32 forwards almost everything to kernelbase. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* ntdll starts every user thread here. */
__declspec(dllexport) void WINAPI BaseThreadInitThunk(ULONG flags, LPTHREAD_START_ROUTINE entry, PVOID parameter)
{
    ExitThread(entry(parameter));
}

/*
 * the multimedia time api set, hosted by kernel32. while any timeBeginPeriod is not yet ended by timeEndPeriod, the
 * process clock resolution is raised with NtSetTimerResolution.
 */
#define TIMERR_NOERROR 0
#define TIMERR_NOCANDO 97
#define TIME_PERIOD_MIN 1
#define TIME_PERIOD_MAX 1000000
#define MMSYSERR_INVALPARAM 11

#pragma pack(push, 1)
typedef struct {
    UINT wType;
    union {
        DWORD ms;
        DWORD sample;
        DWORD cb;
        DWORD ticks;
        struct {
            BYTE hour, min, sec, frame, fps, dummy, pad[2];
        } smpte;
        struct {
            DWORD songptrpos;
        } midi;
    } u;
} KMMTIME;
#pragma pack(pop)

typedef struct {
    UINT wPeriodMin, wPeriodMax;
} KTIMECAPS;

static RTL_SRWLOCK g_time_lock;
static struct {
    UINT Period;
    LONG Count;
} g_periods[64];

DECLSPEC_EXPORT DWORD WINAPI timeGetTime(void)
{
    return GetTickCount();
}

DECLSPEC_EXPORT UINT WINAPI timeGetSystemTime(KMMTIME *time, UINT size)
{
    if (!time || size < sizeof(*time))
        return MMSYSERR_INVALPARAM;
    time->wType = 1;
    time->u.ms = GetTickCount();
    return TIMERR_NOERROR;
}

DECLSPEC_EXPORT UINT WINAPI timeGetDevCaps(KTIMECAPS *caps, UINT size)
{
    if (!caps || size < sizeof(*caps))
        return TIMERR_NOCANDO;
    caps->wPeriodMin = TIME_PERIOD_MIN;
    caps->wPeriodMax = TIME_PERIOD_MAX;
    return TIMERR_NOERROR;
}

/* the clock follows the smallest active period. */
static void apply_periods(void)
{
    UINT finest = 0;
    for (int i = 0; i < 64; i++)
        if (g_periods[i].Count && (!finest || g_periods[i].Period < finest))
            finest = g_periods[i].Period;
    ULONG current;
    if (finest)
        NtSetTimerResolution(finest * 10000, TRUE, &current);
    else
        NtSetTimerResolution(0, FALSE, &current);
}

DECLSPEC_EXPORT UINT WINAPI timeBeginPeriod(UINT period)
{
    if (period < TIME_PERIOD_MIN || period > TIME_PERIOD_MAX)
        return TIMERR_NOCANDO;
    RtlAcquireSRWLockExclusive(&g_time_lock);
    int slot = -1;
    for (int i = 0; i < 64; i++) {
        if (g_periods[i].Count && g_periods[i].Period == period) {
            slot = i;
            break;
        }
        if (!g_periods[i].Count && slot < 0)
            slot = i;
    }
    UINT result = TIMERR_NOCANDO;
    if (slot >= 0) {
        g_periods[slot].Period = period;
        g_periods[slot].Count++;
        apply_periods();
        result = TIMERR_NOERROR;
    }
    RtlReleaseSRWLockExclusive(&g_time_lock);
    return result;
}

DECLSPEC_EXPORT UINT WINAPI timeEndPeriod(UINT period)
{
    if (period < TIME_PERIOD_MIN || period > TIME_PERIOD_MAX)
        return TIMERR_NOCANDO;
    RtlAcquireSRWLockExclusive(&g_time_lock);
    UINT result = TIMERR_NOCANDO;
    for (int i = 0; i < 64; i++) {
        if (g_periods[i].Count && g_periods[i].Period == period) {
            g_periods[i].Count--;
            apply_periods();
            result = TIMERR_NOERROR;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_time_lock);
    return result;
}

/* older name of DnsHostnameToComputerNameExW, only kernel32 exports it. */
DECLSPEC_EXPORT BOOL WINAPI DnsHostnameToComputerNameW(LPCWSTR host, LPWSTR name, LPDWORD size)
{
    return DnsHostnameToComputerNameExW(host, name, size);
}
