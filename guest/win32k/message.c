/*
 * message queues: sent, posted and input messages, timers, kernel to user
 * window procedure callbacks and the default window procedure.
 */
#include "win32k.h"

/* ------------------------------------------------------------------ queues */

void QueueWake(W32THREAD *t, ULONG bits)
{
    if (!t)
        return;
    t->WakeBits |= bits;
    t->ChangeBits |= bits;
    KeSetEvent(&t->Event, 0, FALSE);
}

BOOLEAN QueuePost(W32THREAD *t, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!t || t->Destroying)
        return FALSE;
    QUEUED_MESSAGE *qm = W32Alloc(sizeof(QUEUED_MESSAGE));
    if (!qm)
        return FALSE;
    qm->Kind = QM_POSTED;
    qm->Msg.hwnd = hwnd;
    qm->Msg.message = msg;
    qm->Msg.wParam = wparam;
    qm->Msg.lParam = lparam;
    qm->Msg.time = W32TickCount();
    qm->Msg.pt = InputCursorPos;
    InsertTailList(&t->Posted, &qm->Links);
    QueueWake(t, QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
    return TRUE;
}

void QueuePostInput(W32THREAD *t, const MimeInputEvent *ev)
{
    if (!t || t->Destroying)
        return;
    QUEUED_MESSAGE *qm = W32Alloc(sizeof(QUEUED_MESSAGE));
    if (!qm)
        return;
    qm->Kind = QM_INPUT;
    qm->Input = *ev;
    InsertTailList(&t->Input, &qm->Links);
    ULONG bits = ev->type == MIME_INPUT_MOUSE_MOVE ? QS_MOUSEMOVE
                 : ev->type <= MIME_INPUT_MOUSE_WHEEL ? QS_MOUSEBUTTON
                                                      : QS_KEY;
    QueueWake(t, bits);
}

/* ------------------------------------------------------------------ callbacks */

#define ALIGN16(x) (((x) + 15) & ~(SIZE_T)15)

LRESULT MsgCallWindowProc(WND *w, WNDPROC proc, BOOLEAN ansi, UINT msg, WPARAM wparam, LPARAM lparam,
                          ULONG lparam_kind, ULONG lparam_size)
{
    if (!proc || !w->Thread)
        return DefWindowProcKernel(w, msg, wparam, lparam);
    W32THREAD *t = w->Thread;
    if (t != W32GetThread())
        return 0;
    if (lparam_kind == MIME_LPARAM_VALUE)
        lparam_size = 0;
    SIZE_T size = sizeof(MIME_WINDOWPROC_ARGS) + ALIGN16(lparam_size);
    UCHAR local[sizeof(MIME_WINDOWPROC_ARGS) + 256];
    MIME_WINDOWPROC_ARGS *args = size <= sizeof(local) ? (MIME_WINDOWPROC_ARGS *)local : W32Alloc(size);
    if (!args)
        return 0;
    args->Proc = proc;
    args->Hwnd = WndHandle(w);
    args->Msg = msg;
    args->LparamKind = lparam_kind;
    args->Wparam = wparam;
    args->Lparam = lparam_kind == MIME_LPARAM_VALUE ? lparam : 0;
    args->Ansi = ansi;
    args->DataSize = lparam_size;
    if (lparam_size)
        memcpy(args->Data, (PVOID)lparam, lparam_size);

    t->CallbackDepth++;
    UserReference(&w->Header);
    ULONG depth = UserLeaveAll();
    PVOID out = NULL;
    ULONG out_len = 0;
    NTSTATUS status = KeUserModeCallback(MIME_CB_WINDOWPROC, args, (ULONG)size, &out, &out_len);
    UserReenter(depth);
    t->CallbackDepth--;

    LRESULT result = 0;
    if (NT_SUCCESS(status) && out && out_len >= sizeof(MIME_CALLBACK_RESULT)) {
        MIME_CALLBACK_RESULT hdr;
        if (NT_SUCCESS(MimeCopyFromUser(&hdr, out, sizeof(hdr)))) {
            result = hdr.Result;
            if (lparam_kind == MIME_LPARAM_INOUT && lparam_size) {
                ULONG n = hdr.DataSize < lparam_size ? hdr.DataSize : lparam_size;
                if (n && out_len >= sizeof(hdr) + n)
                    MimeCopyFromUser((PVOID)lparam, (PUCHAR)out + sizeof(hdr), n);
            }
        }
    } else if (!NT_SUCCESS(status)) {
        DbgPrint("win32k: window procedure callback for message %x failed: %08x\n", msg, status);
    }
    UserDereference(&w->Header);
    if ((PUCHAR)args != local)
        W32Free(args);
    return result;
}

/* ------------------------------------------------------------------ sending */

static void complete_sent(SENT_MESSAGE *sm, LRESULT result)
{
    if (sm->Notify) {
        if (sm->LparamKind != MIME_LPARAM_VALUE)
            W32Free((PVOID)sm->Lparam);
        W32Free(sm);
        return;
    }
    sm->Result = result;
    sm->Completed = TRUE;
    KeSetEvent(&sm->Done, 0, FALSE);
}

void MsgProcessSent(W32THREAD *t)
{
    while (!IsListEmpty(&t->Sent)) {
        SENT_MESSAGE *sm = CONTAINING_RECORD(RemoveHeadList(&t->Sent), SENT_MESSAGE, Links);
        SENT_MESSAGE *prev = t->Receiving;
        t->Receiving = sm;
        WND *w = WndLookup(sm->Hwnd);
        LRESULT r = 0;
        if (w && w->Thread == t)
            r = MsgCallWindowProc(w, w->Proc, w->Ansi, sm->Msg, sm->Wparam, sm->Lparam, sm->LparamKind,
                                  sm->LparamSize);
        t->Receiving = prev;
        complete_sent(sm, r);
    }
}

static LRESULT send_to_thread(W32THREAD *target, WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG kind,
                              ULONG size, BOOLEAN notify)
{
    W32THREAD *self = W32GetThread();
    SENT_MESSAGE *sm = W32Alloc(sizeof(SENT_MESSAGE));
    if (!sm)
        return 0;
    sm->Sender = notify ? NULL : self;
    sm->Hwnd = WndHandle(w);
    sm->Msg = msg;
    sm->Wparam = wparam;
    sm->Lparam = lparam;
    sm->LparamKind = kind;
    sm->LparamSize = size;
    sm->Notify = notify;
    if (notify && kind != MIME_LPARAM_VALUE && size) {
        /* nobody waits, so the receiver gets its own copy. */
        PVOID copy = W32Alloc(size);
        if (!copy) {
            W32Free(sm);
            return 0;
        }
        memcpy(copy, (PVOID)lparam, size);
        sm->Lparam = (LPARAM)copy;
    }
    KeInitializeEvent(&sm->Done, NotificationEvent, FALSE);
    InsertTailList(&target->Sent, &sm->Links);
    QueueWake(target, QS_SENDMESSAGE);
    if (notify)
        return 1;
    /* wait, while still serving messages sent to us so two threads sending to each other cannot deadlock. */
    while (!sm->Completed) {
        if (self && !IsListEmpty(&self->Sent)) {
            MsgProcessSent(self);
            continue;
        }
        PVOID objects[2] = {&sm->Done, self ? (PVOID)&self->Event : (PVOID)&sm->Done};
        ULONG depth = UserLeaveAll();
        NTSTATUS status = KeWaitForMultipleObjects(self ? 2 : 1, objects, WaitAny, UserRequest, UserMode, FALSE,
                                                   NULL, NULL);
        UserReenter(depth);
        if (status == STATUS_THREAD_IS_TERMINATING && !sm->Completed) {
            /* the receiver will complete into a message nobody waits for. */
            sm->Notify = TRUE;
            sm->LparamKind = MIME_LPARAM_VALUE;
            return 0;
        }
    }
    LRESULT r = sm->Result;
    W32Free(sm);
    return r;
}

LRESULT MsgSendKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG lparam_kind, ULONG lparam_size)
{
    if (!w || w->Header.Destroyed)
        return 0;
    W32THREAD *target = w->Thread;
    if (!target)
        return DefWindowProcKernel(w, msg, wparam, lparam);
    if (target == W32GetThread())
        return MsgCallWindowProc(w, w->Proc, w->Ansi, msg, wparam, lparam, lparam_kind, lparam_size);
    if (target->Destroying)
        return 0;
    return send_to_thread(target, w, msg, wparam, lparam, lparam_kind, lparam_size, FALSE);
}

LRESULT MsgSend(WND *w, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return MsgSendKernel(w, msg, wparam, lparam, MIME_LPARAM_VALUE, 0);
}

/* ------------------------------------------------------------------ timers */

typedef struct TIMER {
    LIST_ENTRY Links;
    W32THREAD *Thread;
    HWND Hwnd;
    UINT_PTR Id;
    UINT Elapse;
    TIMERPROC Proc;
    ULONG64 Due; /* milliseconds of interrupt time */
} TIMER;

static LIST_ENTRY g_timers = {&g_timers, &g_timers};

static ULONG64 now_ms(void)
{
    return KeQueryInterruptTime() / 10000;
}

static TIMER *find_timer(W32THREAD *t, HWND hwnd, UINT_PTR id)
{
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers; e = e->Flink) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        if (tm->Hwnd == hwnd && tm->Id == id && (hwnd || tm->Thread == t))
            return tm;
    }
    return NULL;
}

UINT_PTR NTAPI NtUserSetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc, ULONG tolerance)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    UINT_PTR result = 0;
    WND *w = hwnd ? WndLookup(hwnd) : NULL;
    if ((hwnd && (!w || w->Thread != t)) || !t)
        goto out;
    if (elapse < USER_TIMER_MINIMUM)
        elapse = USER_TIMER_MINIMUM;
    if (elapse > USER_TIMER_MAXIMUM)
        elapse = USER_TIMER_MAXIMUM;
    if (!hwnd && !id) {
        static UINT_PTR next_id = 0x7FFE;
        do
            id = ++next_id;
        while (find_timer(t, NULL, id));
    }
    TIMER *tm = find_timer(t, hwnd, id);
    if (!tm) {
        tm = W32Alloc(sizeof(TIMER));
        if (!tm)
            goto out;
        InsertTailList(&g_timers, &tm->Links);
    }
    tm->Thread = t;
    tm->Hwnd = hwnd;
    tm->Id = id;
    tm->Elapse = elapse;
    tm->Proc = proc;
    tm->Due = now_ms() + elapse;
    result = hwnd ? (id ? id : 1) : id;
    QueueWake(t, 0);
out:
    UserLeave();
    (void)tolerance;
    return result;
}

BOOL NTAPI NtUserKillTimer(HWND hwnd, UINT_PTR id)
{
    UserEnter();
    TIMER *tm = find_timer(W32GetThread(), hwnd, id);
    if (tm) {
        RemoveEntryList(&tm->Links);
        W32Free(tm);
    }
    UserLeave();
    return tm != NULL;
}

void TimerRemoveWindow(WND *w)
{
    HWND h = WndHandle(w);
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers;) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        e = e->Flink;
        if (tm->Hwnd == h) {
            RemoveEntryList(&tm->Links);
            W32Free(tm);
        }
    }
}

void TimerRemoveThread(W32THREAD *t)
{
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers;) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        e = e->Flink;
        if (tm->Thread == t) {
            RemoveEntryList(&tm->Links);
            W32Free(tm);
        }
    }
}

LONG64 TimerNextDue(W32THREAD *t)
{
    LONG64 best = -1;
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers; e = e->Flink) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        if (tm->Thread == t && (best < 0 || (LONG64)tm->Due < best))
            best = (LONG64)tm->Due;
    }
    return best;
}

void TimerProcessExpired(W32THREAD *t, LONG64 *next_due)
{
    *next_due = TimerNextDue(t);
}

static BOOLEAN hwnd_matches(HWND msg_hwnd, HWND filter)
{
    if (!filter)
        return TRUE;
    if (filter == (HWND)(LONG_PTR)-1)
        return msg_hwnd == NULL;
    if (msg_hwnd == filter)
        return TRUE;
    WND *w = WndLookup(msg_hwnd), *f = WndLookup(filter);
    return w && f && WndIsChildOf(w, f);
}

static BOOLEAN range_matches(UINT msg, UINT min, UINT max)
{
    if (!min && !max)
        return TRUE;
    if (max < min)
        return msg >= min || msg <= max;
    return msg >= min && msg <= max;
}

BOOLEAN TimerGetMessage(W32THREAD *t, HWND hwnd, UINT min, UINT max, BOOLEAN remove, MSG *out)
{
    if (!range_matches(WM_TIMER, min, max))
        return FALSE;
    ULONG64 now = now_ms();
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers; e = e->Flink) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        if (tm->Thread != t || tm->Due > now || !hwnd_matches(tm->Hwnd, hwnd))
            continue;
        out->hwnd = tm->Hwnd;
        out->message = WM_TIMER;
        out->wParam = tm->Id;
        out->lParam = (LPARAM)tm->Proc;
        out->time = (DWORD)now;
        out->pt = InputCursorPos;
        if (remove) {
            tm->Due = now + tm->Elapse;
            /* rotate so timers with equal periods take turns. */
            RemoveEntryList(&tm->Links);
            InsertTailList(&g_timers, &tm->Links);
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN MsgFilterMatches(HWND hwnd, UINT msg, HWND filter, UINT min, UINT max)
{
    return hwnd_matches(hwnd, filter) && range_matches(msg, min, max);
}

/* ------------------------------------------------------------------ retrieval */

static void set_message_state(W32THREAD *t, const MSG *m)
{
    t->MessageTime = m->time;
    t->MessagePos = m->pt;
}

BOOLEAN MsgPeek(W32THREAD *t, MSG *out, HWND hwnd, UINT min, UINT max, UINT flags, BOOLEAN wait)
{
    BOOLEAN remove = (flags & PM_REMOVE) != 0;
    for (;;) {
        MsgProcessSent(t);
        t->WakeBits = 0;

        for (PLIST_ENTRY e = t->Posted.Flink; e != &t->Posted; e = e->Flink) {
            QUEUED_MESSAGE *qm = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
            if (!hwnd_matches(qm->Msg.hwnd, hwnd) || !range_matches(qm->Msg.message, min, max))
                continue;
            *out = qm->Msg;
            t->ExtraInfo = qm->ExtraInfo;
            set_message_state(t, out);
            if (remove) {
                RemoveEntryList(&qm->Links);
                W32Free(qm);
            }
            return TRUE;
        }

        for (PLIST_ENTRY e = t->Input.Flink; e != &t->Input;) {
            QUEUED_MESSAGE *qm = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
            e = e->Flink;
            BOOLEAN consumed = FALSE;
            BOOLEAN got = InputTranslate(t, qm, out, hwnd, min, max, remove, &consumed);
            if (consumed || (got && remove)) {
                RemoveEntryList(&qm->Links);
                W32Free(qm);
            }
            if (got) {
                set_message_state(t, out);
                return TRUE;
            }
        }

        if (t->QuitPosted && range_matches(WM_QUIT, min, max)) {
            out->hwnd = NULL;
            out->message = WM_QUIT;
            out->wParam = (WPARAM)t->QuitCode;
            out->lParam = 0;
            out->time = W32TickCount();
            out->pt = InputCursorPos;
            if (remove)
                t->QuitPosted = FALSE;
            return TRUE;
        }

        if (range_matches(WM_PAINT, min, max)) {
            WND *w = WndFindNeedsPaint(t, hwnd);
            if (w) {
                out->hwnd = WndHandle(w);
                out->message = WM_PAINT;
                out->wParam = 0;
                out->lParam = 0;
                out->time = W32TickCount();
                out->pt = InputCursorPos;
                if (remove)
                    w->Flags &= ~WNDF_INTERNAL_PAINT;
                return TRUE;
            }
        }

        if (TimerGetMessage(t, hwnd, min, max, remove, out)) {
            set_message_state(t, out);
            return TRUE;
        }

        PaintFlushThread(t);
        if (!wait)
            return FALSE;

        LONG64 due = TimerNextDue(t);
        LARGE_INTEGER timeout, *pt = NULL;
        if (due >= 0) {
            LONG64 delta = due - (LONG64)now_ms();
            if (delta < 0)
                delta = 0;
            timeout.QuadPart = -delta * 10000;
            pt = &timeout;
        }
        ULONG depth = UserLeaveAll();
        NTSTATUS status = KeWaitForSingleObject(&t->Event, UserRequest, UserMode, FALSE, pt);
        UserReenter(depth);
        if (status == STATUS_THREAD_IS_TERMINATING) {
            out->hwnd = NULL;
            out->message = WM_QUIT;
            out->wParam = 0;
            out->lParam = 0;
            return TRUE;
        }
    }
}

/* ------------------------------------------------------------------ message services */

static BOOL copy_msg_out(MSG *user, const MSG *m)
{
    return NT_SUCCESS(MimeCopyToUser(user, m, sizeof(*m)));
}

BOOL NTAPI NtUserGetMessage(MSG *user_msg, HWND hwnd, UINT min, UINT max)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    MSG m;
    memset(&m, 0, sizeof(m));
    if (t)
        MsgPeek(t, &m, hwnd, min, max, PM_REMOVE, TRUE);
    UserLeave();
    if (!copy_msg_out(user_msg, &m))
        return -1;
    return m.message != WM_QUIT;
}

BOOL NTAPI NtUserPeekMessage(MSG *user_msg, HWND hwnd, UINT min, UINT max, UINT flags)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    MSG m;
    memset(&m, 0, sizeof(m));
    BOOLEAN got = t && MsgPeek(t, &m, hwnd, min, max, flags, FALSE);
    UserLeave();
    if (got && !copy_msg_out(user_msg, &m))
        return FALSE;
    return got;
}

BOOL NTAPI NtUserWaitMessage(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    if (t) {
        MsgProcessSent(t);
        if (!(t->ChangeBits & (QS_ALLINPUT))) {
            PaintFlushThread(t);
            LONG64 due = TimerNextDue(t);
            LARGE_INTEGER timeout, *pt = NULL;
            if (due >= 0) {
                LONG64 delta = due - (LONG64)now_ms();
                timeout.QuadPart = -(delta < 0 ? 0 : delta) * 10000;
                pt = &timeout;
            }
            ULONG depth = UserLeaveAll();
            KeWaitForSingleObject(&t->Event, UserRequest, UserMode, FALSE, pt);
            UserReenter(depth);
            MsgProcessSent(t);
        }
        t->ChangeBits = 0;
    }
    UserLeave();
    return TRUE;
}

LRESULT NTAPI NtUserDispatchMessage(const MSG *user_msg)
{
    MSG m;
    if (!NT_SUCCESS(MimeCopyFromUser(&m, user_msg, sizeof(m))))
        return 0;
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *w = WndLookup(m.hwnd);
    LRESULT r = 0;
    if (w && w->Thread == t) {
        r = MsgCallWindowProc(w, w->Proc, w->Ansi, m.message, m.wParam, m.lParam, MIME_LPARAM_VALUE, 0);
        if (m.message == WM_PAINT && !w->Header.Destroyed && (w->Flags & WNDF_FRAME_DIRTY) &&
            RgnIsEmpty(&w->Update)) {
            /* the application painted without BeginPaint; draw the frame ourselves so it does not spin. */
            w->Flags &= ~WNDF_FRAME_DIRTY;
            FramePaint(w);
        }
    }
    UserLeave();
    return r;
}

BOOL NTAPI NtUserTranslateMessage(const MSG *user_msg, UINT flags)
{
    /* the host delivers translated characters as their own events. */
    MSG m;
    if (!NT_SUCCESS(MimeCopyFromUser(&m, user_msg, sizeof(m))))
        return FALSE;
    (void)flags;
    return m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN;
}

BOOL NTAPI NtUserGetMessagePos(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    DWORD pos = t ? (DWORD)MAKELONG((SHORT)t->MessagePos.x, (SHORT)t->MessagePos.y) : 0;
    UserLeave();
    return (BOOL)pos;
}

static void broadcast(UINT msg, WPARAM wparam, LPARAM lparam, BOOLEAN post)
{
    WND *d = WndDesktop();
    if (!d)
        return;
    for (WND *c = d->FirstChild; c; c = c->Next) {
        if (c->Flags & WNDF_DESTROYING)
            continue;
        if (post)
            QueuePost(c->Thread, WndHandle(c), msg, wparam, lparam);
        else
            MsgSend(c, msg, wparam, lparam);
    }
}

LRESULT NTAPI NtUserMessageCall(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG_PTR result_info, DWORD type,
                                BOOL ansi)
{
    UserEnter();
    LRESULT r = 0;
    if (hwnd == HWND_BROADCAST && type != MIME_MSGCALL_DEFWINDOWPROC) {
        broadcast(msg, wparam, lparam, FALSE);
        UserLeave();
        return 0;
    }
    WND *w = WndLookup(hwnd);
    if (!w) {
        UserLeave();
        return 0;
    }
    switch (type) {
    case MIME_MSGCALL_DEFWINDOWPROC:
        r = DefWindowProcUser(w, msg, wparam, lparam);
        break;
    case MIME_MSGCALL_SEND:
        r = MsgSend(w, msg, wparam, lparam);
        break;
    case MIME_MSGCALL_SENDNOTIFY:
        if (w->Thread && w->Thread != W32GetThread())
            r = send_to_thread(w->Thread, w, msg, wparam, lparam, MIME_LPARAM_VALUE, 0, TRUE);
        else
            r = MsgSend(w, msg, wparam, lparam);
        break;
    case MIME_MSGCALL_SENDTIMEOUT: {
        r = MsgSend(w, msg, wparam, lparam);
        if (result_info) {
            MIME_SEND_TIMEOUT st;
            if (NT_SUCCESS(MimeCopyFromUser(&st, (PVOID)result_info, sizeof(st)))) {
                st.Result = (ULONG_PTR)r;
                MimeCopyToUser((PVOID)result_info, &st, sizeof(st));
            }
        }
        r = 1;
        break;
    }
    }
    UserLeave();
    (void)ansi;
    return r;
}

BOOL NTAPI NtUserPostMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    UserEnter();
    BOOL ok = FALSE;
    if (hwnd == HWND_BROADCAST) {
        broadcast(msg, wparam, lparam, TRUE);
        ok = TRUE;
    } else if (!hwnd) {
        ok = QueuePost(W32GetThread(), NULL, msg, wparam, lparam);
    } else {
        WND *w = WndLookup(hwnd);
        if (w && w->Thread)
            ok = QueuePost(w->Thread, hwnd, msg, wparam, lparam);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserPostThreadMessage(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam)
{
    UserEnter();
    W32THREAD *t = W32LookupThread((HANDLE)(ULONG_PTR)thread);
    BOOL ok = t && QueuePost(t, NULL, msg, wparam, lparam);
    UserLeave();
    return ok;
}

/* ------------------------------------------------------------------ default window procedure */

/* lparam access for the default procedure: user pointers when called by user32, kernel pointers otherwise. */
static BOOLEAN read_lparam(PVOID dst, LPARAM lparam, SIZE_T size, BOOLEAN user)
{
    if (!lparam)
        return FALSE;
    if (!user) {
        memcpy(dst, (PVOID)lparam, size);
        return TRUE;
    }
    return NT_SUCCESS(MimeCopyFromUser(dst, (PVOID)lparam, size));
}

static BOOLEAN write_lparam(LPARAM lparam, const void *src, SIZE_T size, BOOLEAN user)
{
    if (!lparam)
        return FALSE;
    if (!user) {
        memcpy((PVOID)lparam, src, size);
        return TRUE;
    }
    return NT_SUCCESS(MimeCopyToUser((PVOID)lparam, src, size));
}

/* copies a NUL terminated string. */
static PWSTR capture_sz(LPARAM lparam, BOOLEAN user, ULONG *chars)
{
    *chars = 0;
    if (!lparam)
        return NULL;
    ULONG cap = 64, n = 0;
    PWSTR buf = W32Alloc(cap * sizeof(WCHAR));
    while (buf) {
        WCHAR c;
        if (!read_lparam(&c, lparam + (LPARAM)n * (LPARAM)sizeof(WCHAR), sizeof(c), user) || !c)
            break;
        if (n + 1 >= cap) {
            if (cap >= 65536)
                break;
            PWSTR nb = W32Alloc(cap * 2 * sizeof(WCHAR));
            if (!nb)
                break;
            memcpy(nb, buf, n * sizeof(WCHAR));
            W32Free(buf);
            buf = nb;
            cap *= 2;
        }
        buf[n++] = c;
    }
    if (buf)
        buf[n] = 0;
    *chars = n;
    return buf;
}

static HBRUSH class_background(WND *w)
{
    HBRUSH b = w->Class ? w->Class->Background : NULL;
    if ((ULONG_PTR)b && (ULONG_PTR)b <= COLOR_MENUBAR + 1)
        b = MetricsSysColorBrush((int)(ULONG_PTR)b - 1);
    return b;
}

static void fill_client(WND *w, HDC hdc, HBRUSH hbrush)
{
    DC *dc = DcLookup(hdc);
    BRUSH *brush = (BRUSH *)GdiLookup(hbrush, GDI_TYPE_BRUSH);
    if (!dc || !brush || !dc->Surface)
        return;
    RECT r = {0, 0, w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top};
    if (dc->Window != w) {
        /* a dc for another window, typically the parent: paint the window's client area in its coordinates. */
        RECT c = w->ClientRect;
        WND *dw = dc->Window;
        if (dw)
            RectOffset(&c, -dw->ClientRect.left, -dw->ClientRect.top);
        r = c;
    }
    REGION clip;
    RgnInit(&clip);
    if (DcGetClip(dc, &clip)) {
        LONG l = r.left, t = r.top, rr = r.right, b = r.bottom;
        DcLogicalToSurface(dc, &l, &t);
        DcLogicalToSurface(dc, &rr, &b);
        RECT sr = {l, t, rr, b};
        DrawBrushRect(dc, &clip, &sr, brush, PATCOPY);
    }
    RgnFree(&clip);
}

static ULONG system_cursor_for_hit(LONG hit)
{
    switch (hit) {
    case HTLEFT:
    case HTRIGHT:
        return MIME_CURSOR_SIZE_WE;
    case HTTOP:
    case HTBOTTOM:
        return MIME_CURSOR_SIZE_NS;
    case HTTOPLEFT:
    case HTBOTTOMRIGHT:
        return MIME_CURSOR_SIZE_NWSE;
    case HTTOPRIGHT:
    case HTBOTTOMLEFT:
        return MIME_CURSOR_SIZE_NESW;
    default:
        return MIME_CURSOR_ARROW;
    }
}

static LRESULT def_proc(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, BOOLEAN user)
{
    UINT dpi = WndGetDpi(w);
    switch (msg) {
    case WM_NCCREATE:
        return TRUE;

    case WM_NCCALCSIZE: {
        RECT r;
        if (!read_lparam(&r, lparam, sizeof(r), user))
            return 0;
        FrameClientFromWindow(&r, w->Style, w->ExStyle, dpi);
        write_lparam(lparam, &r, sizeof(r), user);
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = {(SHORT)LOWORD(lparam), (SHORT)HIWORD(lparam)};
        return FrameHitTest(w, pt);
    }

    case WM_NCPAINT:
        FramePaint(w);
        return 0;

    case WM_NCACTIVATE:
        if (wparam)
            w->Flags |= WNDF_ACTIVE_FRAME;
        else
            w->Flags &= ~WNDF_ACTIVE_FRAME;
        /* the host draws the border, whose color follows the activation. */
        if (WndIsTopLevel(w))
            WndUpdateHostConfig(w, FALSE);
        if (FrameHasCaption(w->Style, w->ExStyle) && WndIsVisible(w) && lparam != -1)
            FramePaint(w);
        return TRUE;

    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCMOUSELEAVE:
    case WM_NCRBUTTONUP: {
        POINT pt = {(SHORT)LOWORD(lparam), (SHORT)HIWORD(lparam)};
        return FrameMouse(w, msg, (LONG)wparam, pt);
    }

    case WM_SYSCOMMAND:
        switch (wparam & 0xFFF0) {
        case SC_CLOSE:
            if (!(WndTopLevel(w)->Flags & WNDF_NOCLOSE))
                MsgSend(w, WM_CLOSE, 0, 0);
            break;
        case SC_MINIMIZE:
            WndShow(WndTopLevel(w), SW_MINIMIZE);
            break;
        case SC_MAXIMIZE:
            WndShow(WndTopLevel(w), SW_MAXIMIZE);
            break;
        case SC_RESTORE:
            WndShow(WndTopLevel(w), SW_RESTORE);
            break;
        }
        return 0;

    case WM_CLOSE:
        WndDestroy(w);
        return 0;

    case WM_SETCURSOR: {
        LONG hit = (SHORT)LOWORD(lparam);
        if ((w->Style & WS_CHILD) && w->Parent && WndIsChildOf(w, WndDesktop()) && w->Parent->Thread == w->Thread) {
            if (MsgSend(w->Parent, WM_SETCURSOR, wparam, lparam))
                return TRUE;
        }
        if (hit == HTCLIENT) {
            HCURSOR c = w->Class ? w->Class->Cursor : NULL;
            if (c)
                CursorSet(c);
            return TRUE;
        }
        if (hit == HTERROR) {
            UINT mouse_msg = HIWORD(lparam);
            if (mouse_msg == WM_LBUTTONDOWN || mouse_msg == WM_RBUTTONDOWN)
                HostBeep(MB_OK);
            return TRUE;
        }
        HostSetCursor(system_cursor_for_hit(hit));
        return TRUE;
    }

    case WM_MOUSEACTIVATE:
        if ((w->Style & WS_CHILD) && w->Parent && w->Parent != WndDesktop()) {
            LRESULT r = MsgSend(w->Parent, WM_MOUSEACTIVATE, wparam, lparam);
            if (r)
                return r;
        }
        return MA_ACTIVATE;

    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE && !(w->Style & WS_MINIMIZE)) {
            W32THREAD *t = w->Thread;
            WND *focus = t ? WndLookup(t->Focus) : NULL;
            if (!focus || (focus != w && !WndIsChildOf(focus, w)))
                WndSetFocus(w);
        }
        return 0;

    case WM_ERASEBKGND: {
        HBRUSH b = class_background(w);
        if (!b)
            return 0;
        fill_client(w, (HDC)wparam, b);
        return 1;
    }

    case WM_ICONERASEBKGND:
        return 1;

    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC: {
        DC *dc = DcLookup((HDC)wparam);
        int bk = (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) ? COLOR_WINDOW : COLOR_3DFACE;
        if (msg == WM_CTLCOLORSCROLLBAR)
            bk = COLOR_SCROLLBAR;
        if (dc) {
            dc->State.TextColor = MetricsSysColor(COLOR_WINDOWTEXT);
            dc->State.BkColor = MetricsSysColor(bk == COLOR_SCROLLBAR ? COLOR_3DHILIGHT : bk);
        }
        return (LRESULT)MetricsSysColorBrush(bk);
    }

    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS pos;
        if (!read_lparam(&pos, lparam, sizeof(pos), user))
            return 0;
        if (!(pos.flags & SWP_NOMOVE) || (pos.flags & SWP_FRAMECHANGED)) {
            RECT c = w->ClientRect;
            if (w->Parent && w->Parent != WndDesktop())
                RectOffset(&c, -w->Parent->ClientRect.left, -w->Parent->ClientRect.top);
            MsgSend(w, WM_MOVE, 0, MAKELPARAM(c.left, c.top));
        }
        if (!(pos.flags & SWP_NOSIZE) || (pos.flags & SWP_FRAMECHANGED)) {
            WPARAM kind = (w->Style & WS_MINIMIZE) ? SIZE_MINIMIZED : (w->Style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : SIZE_RESTORED;
            MsgSend(w, WM_SIZE, kind,
                    MAKELPARAM(w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top));
        }
        return 0;
    }

    case WM_SETTEXT: {
        ULONG chars;
        PWSTR text = capture_sz(lparam, user, &chars);
        WndSetText(w, text, chars);
        W32Free(text);
        return TRUE;
    }

    case WM_GETTEXT: {
        if (!wparam || !lparam)
            return 0;
        ULONG n = W32Min((LONG)w->TextLength, (LONG)wparam - 1);
        WCHAR zero = 0;
        if (n && !write_lparam(lparam, w->Text, n * sizeof(WCHAR), user))
            return 0;
        write_lparam(lparam + (LPARAM)n * (LPARAM)sizeof(WCHAR), &zero, sizeof(zero), user);
        return n;
    }

    case WM_GETTEXTLENGTH:
        return w->TextLength;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        PaintBegin(w, &ps);
        PaintEnd(w, &ps);
        return 0;
    }

    case WM_SYSKEYDOWN:
        if (wparam == VK_F4 && (HIWORD(lparam) & KF_ALTDOWN)) {
            WND *top = WndTopLevel(w);
            if (top)
                MsgSend(top, WM_SYSCOMMAND, SC_CLOSE, 0);
        }
        return 0;

    case WM_GETICON:
        return (LRESULT)(wparam == ICON_BIG ? w->Icon : wparam == ICON_SMALL ? w->IconSmall
                                                                              : (w->IconSmall ? w->IconSmall : w->Icon));

    case WM_SETICON: {
        HICON old;
        if (wparam == ICON_BIG) {
            old = w->Icon;
            w->Icon = (HICON)lparam;
        } else {
            old = w->IconSmall;
            w->IconSmall = (HICON)lparam;
        }
        if (FrameHasCaption(w->Style, w->ExStyle)) {
            w->Flags |= WNDF_FRAME_DIRTY;
            if (w->Thread)
                QueueWake(w->Thread, QS_PAINT);
        }
        return (LRESULT)old;
    }

    case WM_QUERYENDSESSION:
    case WM_QUERYOPEN:
        return TRUE;

    case WM_CANCELMODE:
        if (w->Thread && w->Thread->Capture == WndHandle(w))
            InputSetCapture(w->Thread, NULL, 0);
        return 0;

    case WM_DPICHANGED: {
        RECT r;
        if (read_lparam(&r, lparam, sizeof(r), user))
            WndSetPos(w, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_CONTEXTMENU:
    case WM_APPCOMMAND:
    case WM_HELP:
        if ((w->Style & WS_CHILD) && w->Parent && w->Parent != WndDesktop())
            return MsgSend(w->Parent, msg, msg == WM_CONTEXTMENU ? (WPARAM)WndHandle(w) : wparam, lparam);
        return 0;
    }
    return 0;
}

LRESULT DefWindowProcKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return def_proc(w, msg, wparam, lparam, FALSE);
}

LRESULT DefWindowProcUser(WND *w, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return def_proc(w, msg, wparam, lparam, TRUE);
}
