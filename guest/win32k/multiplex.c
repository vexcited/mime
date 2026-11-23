/*
 * the NtUserCall* multiplexers: small services selected by a routine number.
 */
#include "win32k.h"

/* what GetQueueStatus answers: the kinds that arrived since the last call in the high word, those waiting in the low. */
static ULONG queue_status(W32THREAD *t, ULONG flags, BOOL clear)
{
    t->LastRead = W32TickCount();
    MsgProcessSent(t);
    ULONG pending = 0;
    for (PLIST_ENTRY e = t->Posted.Flink; e != &t->Posted; e = e->Flink)
        pending |= MsgPostedBits(CONTAINING_RECORD(e, QUEUED_MESSAGE, Links));
    if (t->QuitPosted)
        pending |= QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;
    if (!IsListEmpty(&t->Input))
        pending |= QS_MOUSE | QS_KEY;
    if (WndFindNeedsPaint(t, nullptr))
        pending |= QS_PAINT;
    if (TimerNextDue(t) >= 0 && (LONG64)(KeQueryInterruptTime() / 10000) >= TimerNextDue(t))
        pending |= QS_TIMER;
    ULONG r = MAKELONG(t->ChangeBits & flags, pending & flags);
    if (clear)
        t->ChangeBits &= ~flags;
    return r;
}

/* the kinds GetQueueStatus takes, measured on windows 11: bits 0 to 8, 10 to 12 and 14; the others are
 * ERROR_INVALID_FLAGS. */
#define QS_VALID 0x5DFF

/* the service genuine user32 calls for GetQueueStatus; the readonly one leaves the change bits as they are. */
DWORD NTAPI NtUserGetQueueStatus(UINT flags)
{
    if (flags & ~QS_VALID) {
        W32SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    UserEnterRead();
    W32THREAD *t = W32GetThread();
    DWORD r = t ? queue_status(t, flags, TRUE) : 0;
    UserLeave();
    return r;
}

DWORD NTAPI NtUserGetQueueStatusReadonly(UINT flags)
{
    if (flags & ~QS_VALID) {
        W32SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    UserEnterRead();
    W32THREAD *t = W32GetThread();
    DWORD r = t ? queue_status(t, flags, FALSE) : 0;
    UserLeave();
    return r;
}

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
        r = t ? (ULONG_PTR)W32SharedWindow(t, offsetof(W32THREAD, Focus)) : 0;
        break;
    case MIME_NOPARAM_GETACTIVEWINDOW:
        r = t ? (ULONG_PTR)W32SharedWindow(t, offsetof(W32THREAD, Active)) : 0;
        break;
    case MIME_NOPARAM_GETCAPTURE:
        r = t ? (ULONG_PTR)W32SharedWindow(t, offsetof(W32THREAD, Capture)) : 0;
        break;
    case MIME_NOPARAM_RELEASECAPTURE:
        if (t)
            InputSetCapture(t, nullptr, 0);
        r = 1;
        break;
    case MIME_NOPARAM_GETMESSAGETIME:
        r = t ? t->MessageTime : 0;
        break;
    case MIME_NOPARAM_GETMESSAGEEXTRAINFO:
        r = t ? t->ExtraInfo : 0;
        break;
    case MIME_NOPARAM_GETINPUTSOURCE:
        r = t ? (ULONG_PTR)t->InputDevice | (ULONG_PTR)t->InputOrigin << 8 : 0;
        break;
    case MIME_NOPARAM_GETDPIFORSYSTEM:
        r = DpiScaleOf(DpiThreadAwareness(t));
        break;
    case MIME_NOPARAM_GETSYSTEMDPI:
        r = W32SystemDpi();
        break;
    case MIME_NOPARAM_INSENDMESSAGE:
        r = MsgInSendFlags(t);
        break;
    case MIME_NOPARAM_GETDIALOGBASEUNITS: {
        /* averages of the system font, 8 by 16 at 96 dpi. */
        UINT dpi = DpiScaleOf(DpiThreadAwareness(t));
        r = (ULONG_PTR)MAKELONG(W32MulDiv(8, (LONG)dpi, 96), W32MulDiv(16, (LONG)dpi, 96));
        break;
    }
    case MIME_NOPARAM_GETLASTINPUTTIME:
        r = InputLastTime;
        break;
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
        if (t)
            r = queue_status(t, (ULONG)param, TRUE);
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
        /* a call is refused once the awareness was chosen, what the manifest says is forced. */
        r = t ? DpiProcessSetAwareness(t->Process, (ULONG)param & 0xFF, (param & 0x100) != 0) : 0;
        break;
    case MIME_ONEPARAM_GETPROCESSDPIAWARENESS:
        r = t ? t->Process->DpiAwareness : 0;
        break;
    case MIME_ONEPARAM_GETPROCESSDPISET:
        r = t ? t->Process->DpiSet : 0;
        break;
    case MIME_ONEPARAM_SETTHREADDPICONTEXT:
        r = t ? DpiThreadSetContext(t, (ULONG)param) : 0;
        break;
    case MIME_ONEPARAM_GETTHREADDPICONTEXT:
        r = t ? (t->DpiOverride ? t->DpiOverride - 1 : t->Process->DpiAwareness) : 0;
        break;
    case MIME_ONEPARAM_SETTHREADDPIHOSTING:
        /* only the two behaviors there are. */
        if (!t || (ULONG)param > 1) {
            r = (ULONG)-1;
        } else {
            r = t->DpiHosting;
            t->DpiHosting = (ULONG)param;
        }
        break;
    case MIME_ONEPARAM_GETKEYBOARDLAYOUT: {
        /* the host may be writing a new layout: a copy counts once the generation held still across it. */
        const volatile MimeBootInfo *boot = HalMimeGetBootInfo();
        MimeKeyboardLayout layout;
        for (int tries = 0; tries < 4; tries++) {
            memcpy(&layout, (const void *)&boot->keyboard, sizeof(layout));
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (layout.generation == boot->keyboard.generation)
                break;
        }
        r = layout.generation;
        if (r && !NT_SUCCESS(MimeCopyToUser((PVOID)param, &layout, sizeof(layout))))
            r = 0;
        break;
    }
    case MIME_ONEPARAM_GETTHREADDPIHOSTING:
        r = t ? t->DpiHosting : 0;
        break;
    case MIME_ONEPARAM_REPLYMESSAGE:
        r = MsgReply(t, (LRESULT)param);
        break;
    case MIME_ONEPARAM_SETMESSAGEEXTRAINFO:
        if (t) {
            r = t->ExtraInfo;
            t->ExtraInfo = param;
        }
        break;
    }
    UserLeave();
    return r;
}

ULONG_PTR NTAPI NtUserCallTwoParam(ULONG_PTR param1, ULONG_PTR param2, ULONG routine)
{
    ULONG_PTR r = 0;
    UserEnter();
    switch (routine) {
    case MIME_TWOPARAM_GETSYSTEMMETRICS:
        r = (ULONG_PTR)(LONG_PTR)MetricsGet((int)param1, (UINT)param2);
        break;
    case MIME_TWOPARAM_SETCURSORPOS:
    {
        POINT pt = {(LONG)param1, (LONG)param2};
        DpiPointConvert(&pt, DpiCurrentAwareness(), DPI_PHYSICAL);
        InputSetCursorPos(pt);
        r = 1;
    }
        break;
    case MIME_TWOPARAM_HOSTMOVESIZE: {
        WND *w = WndLookup((HWND)param1);
        r = w && WndIsTopLevel(w) && HostMoveSize(w, (ULONG)param2, FALSE);
        break;
    }
    case MIME_TWOPARAM_CBTSYSCOMMAND:
        r = HookCbt(nullptr, HCBT_SYSCOMMAND, param1, (LPARAM)param2, 0);
        break;
    case MIME_TWOPARAM_DISPLAYUNIQUENESS:
        r = MetricsDisplayUniqueness();
        break;
    case MIME_TWOPARAM_REGISTERHOTKEY: {
        ULONG key[3];
        if (NT_SUCCESS(MimeCopyFromUser(key, (PVOID)param2, sizeof(key))))
            r = HotkeyRegister(W32GetThread(), (HWND)param1, (int)key[0], key[1], key[2]);
        else
            W32SetLastError(ERROR_NOACCESS);
        break;
    }
    case MIME_TWOPARAM_UNREGISTERHOTKEY:
        r = HotkeyUnregister(W32GetThread(), (HWND)param1, (int)param2);
        break;
    case MIME_TWOPARAM_THREADWINDOWS: {
        W32THREAD *t = nullptr;
        if (!param1) {
            t = InputForeground ? InputForeground->Thread : nullptr;
        } else {
            t = W32LookupThread((HANDLE)param1);
            if (!t)
                W32SetLastError(ERROR_INVALID_PARAMETER);
        }
        if (t) {
            ULONG windows[3] = {(ULONG)(ULONG_PTR)t->Active, (ULONG)(ULONG_PTR)t->Focus, (ULONG)(ULONG_PTR)t->Capture};
            r = NT_SUCCESS(MimeCopyToUser((PVOID)param2, windows, sizeof(windows))) ? (t == W32GetThread() ? 2 : 1) : 0;
        }
        break;
    }
    case MIME_TWOPARAM_ALTALONE: {
        W32THREAD *t = W32GetThread();
        r = t && t->AltAlone;
        if (t)
            t->AltAlone = FALSE;
        break;
    }
    case MIME_TWOPARAM_CBTMOVESIZE: {
        WND *w = WndLookup((HWND)param1);
        RECT rect;
        if (w && NT_SUCCESS(MimeCopyFromUser(&rect, (PVOID)param2, sizeof(rect)))) {
            HookCbt(w, HCBT_MOVESIZE, param1, (LPARAM)&rect, sizeof(rect));
            MimeCopyToUser((PVOID)param2, &rect, sizeof(rect));
        }
        break;
    }
    case MIME_TWOPARAM_GETMONITORINFO:
    case MIME_TWOPARAM_GETMONITORS:
        r = MonitorCall(param1, param2, routine);
        break;
    case MIME_TWOPARAM_WINDOWFROMPOINT: {
        POINT pt = {(LONG)param1, (LONG)param2};
        DpiPixelConvert(&pt, DpiCurrentAwareness(), DPI_PHYSICAL);
        WND *desktop = WndDesktop();
        for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next) {
            POINT at = pt;
            DpiPixelConvert(&at, DPI_PHYSICAL, c->Awareness);
            if (!(c->Style & WS_VISIBLE) || (c->Style & WS_MINIMIZE) || (c->ExStyle & WS_EX_TRANSPARENT) ||
                (c->ExStyle & 0x80000000u) || !RectContains(&c->WindowRect, at.x, at.y) || !LayerHitTest(c, at))
                continue;
            r = (ULONG_PTR)WndHandle(WndFromPoint(c, at));
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
    ci.hCursor = t ? t->Cursor : nullptr;
    ci.ptScreenPos = InputCursorPos;
    DpiPixelConvert(&ci.ptScreenPos, DPI_PHYSICAL, DpiThreadAwareness(t));
    UserLeave();
    if (!NT_SUCCESS(MimeCopyToUser(user_info, &ci, sizeof(ci)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

BOOL NTAPI NtUserAttachThreadInput(DWORD attach, DWORD to, BOOL do_attach)
{
    UserEnter();
    /* like any call of the window manager, this one makes its caller a gui thread, so it can attach itself. */
    W32GetThread();
    W32THREAD *a = W32LookupThread((HANDLE)(ULONG_PTR)attach);
    W32THREAD *b = W32LookupThread((HANDLE)(ULONG_PTR)to);
    BOOL ok = a && b && a != b && W32AttachInput(a, b, do_attach);
    UserLeave();
    if (!ok)
        W32SetLastError(ERROR_INVALID_PARAMETER);
    return ok;
}

/*
 * IsThreadMessageQueueAttached, as win32kfull 26300 answers it: whether the thread shares the caller's input, or with
 * no thread whether any other thread shares it. a thread that is not a gui thread is a wrong parameter.
 */
BOOL NTAPI NtUserThreadMessageQueueAttached(DWORD tid)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = FALSE;
    ULONG error = 0;
    if (t && tid) {
        W32THREAD *other = W32LookupThread((HANDLE)(ULONG_PTR)tid);
        if (other)
            ok = W32InputOwner(other) == W32InputOwner(t);
        else
            error = ERROR_INVALID_PARAMETER;
    } else if (t) {
        ULONG count = 0;
        for (W32THREAD *x = W32NextThread(nullptr); x && count < 2; x = W32NextThread(x))
            if (!x->Destroying && W32InputOwner(x) == W32InputOwner(t))
                count++;
        ok = count > 1;
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return ok;
}

/*
 * PostQuitMessage, as win32kfull 26300 does it: no message is queued, the thread keeps a quit and its code, a second
 * call only changes the code. the answer is always TRUE.
 */
BOOL NTAPI NtUserPostQuitMessage(int code)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    if (t) {
        t->QuitPosted = TRUE;
        t->QuitCode = code;
        QueueWake(t, QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
    }
    UserLeave();
    return TRUE;
}
