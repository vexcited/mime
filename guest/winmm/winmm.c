/*
 * winmm: the multimedia timer and sound entry points. timers run on one
 * thread of the process like windows, with its 16 slots and its timer ids.
 * there are no audio devices: sounds fail the way winmm does without an
 * output device.
 */
#define MIME_BUILD_WINMM
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mmsystem.h>

#define MAX_TIMERS 16
#define MIN_PERIOD 1
#define MAX_PERIOD 1000000

typedef struct MM_TIMER {
    UINT Id;
    UINT Delay;
    UINT Flags;
    LPTIMECALLBACK Callback;
    DWORD_PTR User;
    ULONGLONG Due; /* in milliseconds of timeGetTime, 64 bit. */
} MM_TIMER;

static struct {
    RTL_RUN_ONCE Once;
    RTL_CRITICAL_SECTION Lock;
    HANDLE Thread, Wake, Idle;
    DWORD ThreadId;
    MM_TIMER Timers[MAX_TIMERS];
    WORD Counter;
    UINT Running;
    UINT RunningFlags;
} g_mm;

static ULONGLONG now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    if (freq.QuadPart <= 0)
        return GetTickCount64();
    ULONGLONG seconds = (ULONGLONG)(c.QuadPart / freq.QuadPart), rest = (ULONGLONG)(c.QuadPart % freq.QuadPart);
    return seconds * 1000 + rest * 1000 / (ULONGLONG)freq.QuadPart;
}

WINMMAPI DWORD WINAPI timeGetTime(void)
{
    return (DWORD)now_ms();
}

WINMMAPI MMRESULT WINAPI timeGetSystemTime(LPMMTIME time, UINT size)
{
    if (!time || size < sizeof(MMTIME))
        return MMSYSERR_INVALPARAM;
    time->wType = TIME_MS;
    time->u.ms = timeGetTime();
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS caps, UINT size)
{
    if (!caps || size < sizeof(TIMECAPS))
        return TIMERR_NOCANDO;
    caps->wPeriodMin = MIN_PERIOD;
    caps->wPeriodMax = MAX_PERIOD;
    return TIMERR_NOERROR;
}

/* the scheduler ticks every millisecond already, so periods only need validating. */
WINMMAPI MMRESULT WINAPI timeBeginPeriod(UINT period)
{
    return period < MIN_PERIOD || period > MAX_PERIOD ? TIMERR_NOCANDO : TIMERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI timeEndPeriod(UINT period)
{
    return period < MIN_PERIOD || period > MAX_PERIOD ? TIMERR_NOCANDO : TIMERR_NOERROR;
}

static DWORD WINAPI timer_thread(LPVOID param)
{
    for (;;) {
        RtlEnterCriticalSection(&g_mm.Lock);
        ULONGLONG now = now_ms(), next = (ULONGLONG)-1;
        int fire = -1;
        for (int i = 0; i < MAX_TIMERS; i++) {
            MM_TIMER *t = &g_mm.Timers[i];
            if (!t->Id)
                continue;
            if (t->Due <= now && fire < 0)
                fire = i;
            if (t->Due < next)
                next = t->Due;
        }
        if (fire < 0) {
            RtlLeaveCriticalSection(&g_mm.Lock);
            DWORD wait = next == (ULONGLONG)-1 ? INFINITE : (DWORD)(next - now);
            WaitForSingleObject(g_mm.Wake, wait);
            continue;
        }
        MM_TIMER t = g_mm.Timers[fire];
        if (t.Flags & TIME_PERIODIC) {
            MM_TIMER *slot = &g_mm.Timers[fire];
            slot->Due += slot->Delay;
            /* a callback slower than its period skips the missed ticks. */
            if (slot->Due + slot->Delay <= now)
                slot->Due = now + slot->Delay;
        } else {
            g_mm.Timers[fire].Id = 0;
        }
        g_mm.Running = t.Id;
        g_mm.RunningFlags = t.Flags;
        ResetEvent(g_mm.Idle);
        RtlLeaveCriticalSection(&g_mm.Lock);

        if (t.Flags & TIME_CALLBACK_EVENT_SET)
            SetEvent((HANDLE)(void *)t.Callback);
        else if (t.Flags & TIME_CALLBACK_EVENT_PULSE)
            PulseEvent((HANDLE)(void *)t.Callback);
        else if (t.Callback)
            t.Callback(t.Id, 0, t.User, 0, 0);

        RtlEnterCriticalSection(&g_mm.Lock);
        g_mm.Running = 0;
        SetEvent(g_mm.Idle);
        RtlLeaveCriticalSection(&g_mm.Lock);
    }
    return 0;
}

static ULONG NTAPI init_timers(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&g_mm.Lock);
    return TRUE;
}

/* starts the timer thread on first use, with the lock held. */
static BOOL start_thread(void)
{
    if (g_mm.Thread)
        return TRUE;
    if (!g_mm.Wake)
        g_mm.Wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_mm.Idle)
        g_mm.Idle = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!g_mm.Wake || !g_mm.Idle)
        return FALSE;
    g_mm.Thread = CreateThread(NULL, 0, timer_thread, NULL, 0, &g_mm.ThreadId);
    if (!g_mm.Thread)
        return FALSE;
    SetThreadPriority(g_mm.Thread, THREAD_PRIORITY_TIME_CRITICAL);
    return TRUE;
}

WINMMAPI MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user,
                                      UINT flags)
{
    if (flags & ~(UINT)0x1F1)
        return 0;
    if (delay > MAX_PERIOD || delay < MIN_PERIOD)
        return 0;
    RtlRunOnceExecuteOnce(&g_mm.Once, init_timers, NULL, NULL);
    RtlEnterCriticalSection(&g_mm.Lock);
    UINT id = 0;
    if (start_thread()) {
        for (int i = 0; i < MAX_TIMERS; i++) {
            MM_TIMER *t = &g_mm.Timers[i];
            if (t->Id)
                continue;
            do
                g_mm.Counter += MAX_TIMERS;
            while (!g_mm.Counter);
            id = (UINT)i + g_mm.Counter;
            t->Id = id;
            t->Delay = delay;
            t->Flags = flags;
            t->Callback = callback;
            t->User = user;
            t->Due = now_ms() + delay;
            SetEvent(g_mm.Wake);
            break;
        }
    }
    RtlLeaveCriticalSection(&g_mm.Lock);
    return id;
}

WINMMAPI MMRESULT WINAPI timeKillEvent(UINT id)
{
    RtlRunOnceExecuteOnce(&g_mm.Once, init_timers, NULL, NULL);
    RtlEnterCriticalSection(&g_mm.Lock);
    MM_TIMER *t = &g_mm.Timers[id & (MAX_TIMERS - 1)];
    if (!g_mm.Thread || !id || t->Id != id) {
        RtlLeaveCriticalSection(&g_mm.Lock);
        return TIMERR_NOCANDO;
    }
    UINT flags = t->Flags;
    t->Id = 0;
    SetEvent(g_mm.Wake);
    BOOL wait = g_mm.Running == id && GetCurrentThreadId() != g_mm.ThreadId && (flags & TIME_KILL_SYNCHRONOUS);
    RtlLeaveCriticalSection(&g_mm.Lock);
    if (wait)
        WaitForSingleObject(g_mm.Idle, INFINITE);
    return TIMERR_NOERROR;
}

/* ------------------------------------------------------------------ sounds */

#define SND_INVALID_FLAGS 0xFF80DF20u

/*
 * no wave output device exists. stopping always succeeds; an asynchronous
 * sound is accepted and fails later on the sound thread as on windows, a
 * synchronous one fails at once.
 */
static BOOL play_sound(BOOL has_sound, DWORD flags)
{
    if (flags & SND_INVALID_FLAGS)
        return FALSE;
    if (!has_sound || (flags & SND_PURGE))
        return TRUE;
    if (flags & SND_ASYNC)
        return TRUE;
    return FALSE;
}

WINMMAPI BOOL WINAPI PlaySoundW(LPCWSTR sound, HMODULE module, DWORD flags)
{
    return play_sound(sound != NULL, flags);
}

WINMMAPI BOOL WINAPI PlaySoundA(LPCSTR sound, HMODULE module, DWORD flags)
{
    return play_sound(sound != NULL, flags);
}

WINMMAPI BOOL WINAPI sndPlaySoundW(LPCWSTR sound, UINT flags)
{
    return play_sound(sound != NULL, flags & (SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP));
}

WINMMAPI BOOL WINAPI sndPlaySoundA(LPCSTR sound, UINT flags)
{
    return play_sound(sound != NULL, flags & (SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP));
}

WINMMAPI UINT WINAPI waveOutGetNumDevs(void)
{
    return 0;
}

WINMMAPI UINT WINAPI waveInGetNumDevs(void)
{
    return 0;
}

WINMMAPI UINT WINAPI midiOutGetNumDevs(void)
{
    return 0;
}

WINMMAPI UINT WINAPI midiInGetNumDevs(void)
{
    return 0;
}

WINMMAPI UINT WINAPI mixerGetNumDevs(void)
{
    return 0;
}

WINMMAPI UINT WINAPI auxGetNumDevs(void)
{
    return 0;
}
