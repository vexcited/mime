/*
 * the display's vertical blanks on the performance counter: they come at the
 * panel's refresh rate and are counted from the counter's zero, which is all
 * dxgi, dwmapi and dcomp need to agree on when a frame shows.
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

/* the panel's rate in hertz, read again at most every few seconds. */
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

/* the index of the blank at or before the counter value, and the counter value the next one comes at. */
FORCEINLINE UINT64 MimeVblankIndex(const MIME_VBLANK *info, UINT64 counter) { return counter / info->period; }

FORCEINLINE UINT64 MimeVblankTime(const MIME_VBLANK *info, UINT64 index) { return index * info->period; }

#endif
