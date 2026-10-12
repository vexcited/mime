/*
 * the NtUserCall* multiplexers: small services selected by a routine number.
 */
#include "win32k.h"

ULONG_PTR NTAPI NtUserCallNoParam(ULONG routine)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    ULONG_PTR r = 0;
    switch (routine) {
    case MIME_NOPARAM_GETDESKTOPWINDOW:
        r = (ULONG_PTR)WndHandle(WndDesktop());
        break;
    case MIME_NOPARAM_GETFOCUS:
        r = t ? (ULONG_PTR)t->Focus : 0;
        break;
    case MIME_NOPARAM_GETACTIVEWINDOW:
        r = t ? (ULONG_PTR)t->Active : 0;
        break;
    case MIME_NOPARAM_GETCAPTURE:
        r = t ? (ULONG_PTR)t->Capture : 0;
        break;
    case MIME_NOPARAM_RELEASECAPTURE:
        if (t)
            InputSetCapture(t, NULL, 0);
        r = 1;
        break;
    case MIME_NOPARAM_GETMESSAGETIME:
        r = t ? t->MessageTime : 0;
        break;
    case MIME_NOPARAM_GETMESSAGEEXTRAINFO:
        r = t ? t->ExtraInfo : 0;
        break;
    case MIME_NOPARAM_GETDPIFORSYSTEM:
        r = W32SystemDpi();
        break;
    case MIME_NOPARAM_GETDIALOGBASEUNITS: {
        /* averages of the system font, 8 by 16 at 96 dpi. */
        UINT dpi = W32SystemDpi();
        r = (ULONG_PTR)MAKELONG(W32MulDiv(8, (LONG)dpi, 96), W32MulDiv(16, (LONG)dpi, 96));
        break;
    }
    }
    UserLeave();
    return r;
}

ULONG_PTR NTAPI NtUserCallOneParam(ULONG_PTR param, ULONG routine)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    ULONG_PTR r = 0;
    switch (routine) {
    case MIME_ONEPARAM_GETSYSCOLOR:
        r = MetricsSysColor((int)param);
        break;
    case MIME_ONEPARAM_GETSYSCOLORBRUSH:
        r = (ULONG_PTR)MetricsSysColorBrush((int)param);
        break;
    case MIME_ONEPARAM_MESSAGEBEEP:
        HostBeep((UINT)param);
        r = 1;
        break;
    case MIME_ONEPARAM_POSTQUITMESSAGE:
        if (t) {
            t->QuitPosted = TRUE;
            t->QuitCode = (int)param;
            QueueWake(t, QS_POSTMESSAGE);
        }
        break;
    case MIME_ONEPARAM_GETQUEUESTATUS:
        if (t) {
            MsgProcessSent(t);
            ULONG flags = (ULONG)param;
            ULONG pending = 0;
            if (!IsListEmpty(&t->Posted) || t->QuitPosted)
                pending |= QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;
            if (!IsListEmpty(&t->Input))
                pending |= QS_INPUT;
            if (WndFindNeedsPaint(t, NULL))
                pending |= QS_PAINT;
            if (TimerNextDue(t) >= 0 && (LONG64)(KeQueryInterruptTime() / 10000) >= TimerNextDue(t))
                pending |= QS_TIMER;
            r = MAKELONG(t->ChangeBits & flags, pending & flags);
            t->ChangeBits &= ~flags;
        }
        break;
    case MIME_ONEPARAM_SHOWCURSOR:
        if (t) {
            t->CursorShowCount += param ? 1 : -1;
            r = (ULONG_PTR)(LONG_PTR)t->CursorShowCount;
            if (t->CursorShowCount < 0)
                HostSetCursor(MIME_CURSOR_HIDDEN);
            else
                CursorSet(t->Cursor);
        }
        break;
    case MIME_ONEPARAM_SETPROCESSDPIAWARENESS:
        if (t) {
            t->Process->DpiAwareness = (ULONG)param;
            r = 1;
        }
        break;
    case MIME_ONEPARAM_GETPROCESSDPIAWARENESS:
        r = t ? t->Process->DpiAwareness : 0;
        break;
    }
    UserLeave();
    return r;
}

#define PRIMARY_MONITOR ((HMONITOR)(ULONG_PTR)0x00010001)

ULONG_PTR NTAPI NtUserCallTwoParam(ULONG_PTR param1, ULONG_PTR param2, ULONG routine)
{
    ULONG_PTR r = 0;
    UserEnter();
    switch (routine) {
    case MIME_TWOPARAM_GETSYSTEMMETRICS:
        r = (ULONG_PTR)(LONG_PTR)MetricsGet((int)param1, (UINT)param2);
        break;
    case MIME_TWOPARAM_SETCURSORPOS:
        InputCursorPos = (POINT){(LONG)param1, (LONG)param2};
        r = 1;
        break;
    case MIME_TWOPARAM_MONITORFROMPOINT:
        r = (ULONG_PTR)PRIMARY_MONITOR;
        break;
    case MIME_TWOPARAM_GETMONITORINFO: {
        if ((HMONITOR)param1 != PRIMARY_MONITOR)
            break;
        MONITORINFOEXW mi;
        if (!NT_SUCCESS(MimeCopyFromUser(&mi.cbSize, (PVOID)param2, sizeof(DWORD))))
            break;
        if (mi.cbSize != sizeof(MONITORINFO) && mi.cbSize != sizeof(MONITORINFOEXW))
            break;
        mi.rcMonitor = (RECT){0, 0, W32Display.Width, W32Display.Height};
        mi.rcWork = W32Display.Work;
        mi.dwFlags = MONITORINFOF_PRIMARY;
        memcpy(mi.szDevice, L"\\\\.\\DISPLAY1", sizeof(L"\\\\.\\DISPLAY1"));
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param2, &mi, mi.cbSize));
        break;
    }
    }
    UserLeave();
    return r;
}
