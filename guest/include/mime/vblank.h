/*
 * vertical blanks on the performance counter: they come at the panel's refresh rate, counted from the counter's
 * zero, which is all dxgi, dwmapi and dcomp need to agree on when a frame shows.
 */
#ifndef MIME_VBLANK_H
#define MIME_VBLANK_H

#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winuser.h>

typedef struct MIME_VBLANK {
    /* counter ticks per blank. */
    UINT64 period;
    UINT64 frequency;
    UINT rate;
} MIME_VBLANK;

/* in hertz, cached for a few seconds. */
FORCEINLINE UINT MimeRefreshRate(void)
{
    static volatile LONG cached;
    static volatile LONG64 stamp;
    ULONGLONG now = GetTickCount64();
    LONG rate = cached;
    if (!rate || now - (ULONGLONG)stamp > 4000) {
        HDC screen = GetDC(nullptr);
        int hz = screen ? GetDeviceCaps(screen, VREFRESH) : 0;
        if (screen)
            ReleaseDC(nullptr, screen);
        rate = hz > 1 ? hz : 60;
        cached = rate;
        stamp = (LONG64)now;
    }
    return (UINT)rate;
}

FORCEINLINE void MimeVblankInfo(MIME_VBLANK *info)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    info->rate = MimeRefreshRate();
    info->frequency = (UINT64)frequency.QuadPart;
    info->period = info->frequency / info->rate;
}

/* the blank at or before counter. MimeVblankTime gives the counter value of a blank. */
FORCEINLINE UINT64 MimeVblankIndex(const MIME_VBLANK *info, UINT64 counter) { return counter / info->period; }

FORCEINLINE UINT64 MimeVblankTime(const MIME_VBLANK *info, UINT64 index) { return index * info->period; }

/*
 * waits for a value of the counter. a sleep is as coarse as the clock of the system, 15.6 ms unless a program asked
 * for better, and a blank comes every 6.9 ms on a panel of 144 Hz: the wait is on a timer of its own, as fine as
 * the kernel makes them.
 */
FORCEINLINE void MimeSleepUntil(UINT64 counter)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if ((UINT64)now.QuadPart >= counter)
        return;
    UINT64 left = counter - (UINT64)now.QuadPart;
    LARGE_INTEGER due = {.QuadPart = -(LONGLONG)(left * 10000000 / (UINT64)frequency.QuadPart)};
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer && due.QuadPart && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(timer, INFINITE);
    else
        Sleep((DWORD)(left * 1000 / (UINT64)frequency.QuadPart) + 1);
    if (timer)
        CloseHandle(timer);
}

#endif
