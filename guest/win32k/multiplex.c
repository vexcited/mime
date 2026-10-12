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
    case MIME_TWOPARAM_WINDOWFROMPOINT: {
        POINT pt = {(LONG)param1, (LONG)param2};
        WND *desktop = WndDesktop();
        for (WND *c = desktop ? desktop->FirstChild : NULL; c; c = c->Next) {
            if (!(c->Style & WS_VISIBLE) || (c->Style & WS_MINIMIZE) || (c->ExStyle & WS_EX_TRANSPARENT) ||
                (c->ExStyle & 0x80000000u) || !RectContains(&c->WindowRect, pt.x, pt.y) || !LayerHitTest(c, pt))
                continue;
            r = (ULONG_PTR)WndHandle(WndFromPoint(c, pt));
            break;
        }
        break;
    }
    }
    UserLeave();
    return r;
}

BOOL NTAPI NtUserGetCursorInfo(PCURSORINFO user_info)
{
    CURSORINFO ci;
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&ci.cbSize, &user_info->cbSize, sizeof(DWORD))) ||
        ci.cbSize != sizeof(CURSORINFO)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    ci.flags = t && t->CursorShowCount >= 0 ? CURSOR_SHOWING : 0;
    ci.hCursor = t ? t->Cursor : NULL;
    ci.ptScreenPos = InputCursorPos;
    UserLeave();
    if (!NT_SUCCESS(MimeCopyToUser(user_info, &ci, sizeof(ci)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

/* the queues stay separate: attaching only validates the threads, which is enough for focus tricks. */
BOOL NTAPI NtUserAttachThreadInput(DWORD attach, DWORD to, BOOL do_attach)
{
    UserEnter();
    W32THREAD *a = W32LookupThread((HANDLE)(ULONG_PTR)attach);
    W32THREAD *b = W32LookupThread((HANDLE)(ULONG_PTR)to);
    BOOL ok = a && b && a != b;
    UserLeave();
    if (!ok)
        W32SetLastError(ERROR_INVALID_PARAMETER);
    (void)do_attach;
    return ok;
}
