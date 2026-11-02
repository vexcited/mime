/*
 * message queues: sent, posted and input messages, timers, kernel to user
 * window procedure callbacks and the default window procedure.
 */
#include "win32k.h"
#include <mime/msgtable.h>

/* queues. */

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
    QueueWake(t, msg == WM_INPUT ? QS_RAWINPUT : QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
    return TRUE;
}

BOOLEAN QueueWindowOperation(W32THREAD *t, HWND hwnd, UINT command, const WINDOWPLACEMENT *placement)
{
    if (!t || t->Destroying)
        return FALSE;
    QUEUED_MESSAGE *qm = W32Alloc(sizeof(*qm));
    if (!qm)
        return FALSE;
    qm->Kind = placement ? QM_WINDOW_PLACEMENT : QM_WINDOW_SHOW;
    qm->Msg.hwnd = hwnd;
    qm->Msg.wParam = command;
    if (placement)
        qm->Placement = *placement;
    InsertTailList(&t->Posted, &qm->Links);
    QueueWake(t, QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
    return TRUE;
}

void QueuePostInput(W32THREAD *t, const MimeInputEvent *ev, WPARAM mouse_state)
{
    if (!t || t->Destroying)
        return;
    QUEUED_MESSAGE *qm = W32Alloc(sizeof(QUEUED_MESSAGE));
    if (!qm)
        return;
    qm->Kind = QM_INPUT;
    qm->Input = *ev;
    /* raw input keeps mouse flags in the otherwise unused message payload. */
    qm->Msg.wParam = mouse_state;
    InsertTailList(&t->Input, &qm->Links);
    ULONG bits = ev->type == MIME_INPUT_MOUSE_MOVE ? QS_MOUSEMOVE
                 : ev->type <= MIME_INPUT_MOUSE_WHEEL ? QS_MOUSEBUTTON
                                                      : QS_KEY;
    QueueWake(t, bits);
}

/* callbacks. */

#define ALIGN16(x) (((x) + 15) & ~(SIZE_T)15)

/* larger blocks need a separate allocation in the receiver's address space. */
#define MSG_INLINE_MAX 0x8000

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
    /* allocate below 4 GiB for a 32-bit receiver. */
    PVOID external = nullptr;
    SIZE_T external_size = 0;
    if (lparam_size > MSG_INLINE_MAX) {
        external_size = lparam_size;
        NTSTATUS alloc = ZwAllocateVirtualMemory((HANDLE)-1, &external, 0, &external_size, MEM_COMMIT | MEM_RESERVE,
                                                 PAGE_READWRITE);
        if (!NT_SUCCESS(alloc) || (t->Process && t->Process->Wow && (ULONG_PTR)external + external_size > 0xFFFFFFFFull) ||
            !NT_SUCCESS(MimeCopyToUser(external, (PVOID)lparam, lparam_size))) {
            if (NT_SUCCESS(alloc)) {
                external_size = 0;
                ZwFreeVirtualMemory((HANDLE)-1, &external, &external_size, MEM_RELEASE);
            }
            return 0;
        }
    }
    SIZE_T size = sizeof(MIME_WINDOWPROC_ARGS) + (external ? 0 : ALIGN16(lparam_size));
    UCHAR local[sizeof(MIME_WINDOWPROC_ARGS) + 256];
    MIME_WINDOWPROC_ARGS *args = size <= sizeof(local) ? (MIME_WINDOWPROC_ARGS *)local : W32Alloc(size);
    if (!args) {
        if (external) {
            external_size = 0;
            ZwFreeVirtualMemory((HANDLE)-1, &external, &external_size, MEM_RELEASE);
        }
        return 0;
    }
    args->Proc = proc;
    args->Hwnd = WndHandle(w);
    args->Msg = msg;
    args->LparamKind = lparam_kind | (external ? MIME_LPARAM_EXTERNAL : 0);
    args->Wparam = wparam;
    args->Lparam = lparam_kind == MIME_LPARAM_VALUE ? lparam : 0;
    if (lparam_kind == MIME_LPARAM_WPTR)
        args->Lparam = *(LPARAM *)((PUCHAR)lparam + 8);
    if (external)
        args->Lparam = (LPARAM)external;
    args->Ansi = ansi;
    args->DataSize = lparam_size;
    if (lparam_size && !external)
        memcpy(args->Data, (PVOID)lparam, lparam_size);

    t->CallbackDepth++;
    UserReference(&w->Header);
    ULONG depth = UserLeaveAll();
    PVOID out = nullptr;
    ULONG out_len = 0;
    NTSTATUS status = KeUserModeCallback(MIME_CB_WINDOWPROC, args, (ULONG)size, &out, &out_len);
    UserReenter(depth);
    t->CallbackDepth--;

    BOOLEAN writes = lparam_kind == MIME_LPARAM_INOUT || lparam_kind == MIME_LPARAM_PTR2 ||
                     lparam_kind == MIME_LPARAM_WPTR;
    LRESULT result = 0;
    if (NT_SUCCESS(status) && out && out_len >= sizeof(MIME_CALLBACK_RESULT)) {
        MIME_CALLBACK_RESULT hdr;
        if (NT_SUCCESS(MimeCopyFromUser(&hdr, out, sizeof(hdr)))) {
            result = hdr.Result;
            if (writes && lparam_size && external) {
                MimeCopyFromUser((PVOID)lparam, external, lparam_size);
            } else if (writes && lparam_size) {
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
    if (external) {
        external_size = 0;
        ZwFreeVirtualMemory((HANDLE)-1, &external, &external_size, MEM_RELEASE);
    }
    return result;
}

/* sending. */

static ULONG64 now_ms(void);

typedef struct SEND_OPTS {
    ULONG Flags;            /* SMTO_* */
    ULONG Timeout;          /* milliseconds, none when 0. */
    BOOLEAN Owned;          /* pool memory; ownership passes to the message when the sender stops waiting. */
    SENDASYNCPROC AsyncProc; /* called in the sender's thread without blocking the sender. */
    ULONG_PTR AsyncData;
    BOOLEAN TimedOut;
    BOOLEAN Aborted;        /* the window or its thread exited. */
    BOOLEAN Refused;        /* not sent because the receiving thread is hung. */
    BOOLEAN Taken;          /* the message owns the block. */
} SEND_OPTS;

/* a thread is considered hung after this long without checking its queue. */
#define HUNG_MS 5000

static BOOLEAN thread_hung(W32THREAD *t)
{
    return !t->InWait && (DWORD)(W32TickCount() - t->LastRead) > HUNG_MS;
}

BOOLEAN MsgThreadHung(W32THREAD *t)
{
    return thread_hung(t);
}

static void free_sent(SENT_MESSAGE *sm)
{
    if (sm->Owned)
        W32Free((PVOID)sm->Lparam);
    W32Free(sm);
}

/* deliver the callback result when the sender next processes its queue. */
static void queue_result_call(SENT_MESSAGE *sm, LRESULT result)
{
    W32THREAD *sender = W32LookupThread(sm->SenderTid);
    if (!sender)
        return;
    SENT_MESSAGE *call = W32Alloc(sizeof(SENT_MESSAGE));
    if (!call)
        return;
    call->ResultCall = TRUE;
    call->Notify = TRUE;
    call->Hwnd = sm->Hwnd;
    call->Msg = sm->Msg;
    call->AsyncProc = sm->AsyncProc;
    call->AsyncData = sm->AsyncData;
    call->Result = result;
    InsertTailList(&sender->Sent, &call->Links);
    QueueWake(sender, QS_SENDMESSAGE);
}

static void complete_sent(SENT_MESSAGE *sm, LRESULT result)
{
    if (sm->Notify) {
        if (sm->Callback && !sm->Aborted)
            queue_result_call(sm, result);
        free_sent(sm);
        return;
    }
    sm->Result = result;
    sm->Completed = TRUE;
    KeSetEvent(&sm->Done, 0, FALSE);
}

/* SendMessageCallback runs its callback in the sending thread. */
static void call_async(W32THREAD *t, SENDASYNCPROC proc, HWND hwnd, UINT msg, ULONG_PTR data, LRESULT result)
{
    MIME_SENDASYNC_ARGS args = {proc, hwnd, msg, data, result};
    t->CallbackDepth++;
    ULONG depth = UserLeaveAll();
    PVOID out = nullptr;
    ULONG out_len = 0;
    NTSTATUS status = KeUserModeCallback(MIME_CB_SENDASYNC, &args, sizeof(args), &out, &out_len);
    UserReenter(depth);
    t->CallbackDepth--;
    if (!NT_SUCCESS(status))
        DbgPrint("win32k: send callback for message %x failed: %08x\n", msg, status);
}

/*
 * a message hook sees the message as it is, the data its lparam points at included: a copy follows the message
 * in the call, and the hook's copy is dropped afterwards. other pointer kinds are seen as 0.
 */
static void call_wnd_hook(int id, W32THREAD *t, BOOLEAN own, LRESULT result, LPARAM shown, WPARAM wparam, UINT msg,
                          HWND hwnd, ULONG kind, LPARAM lparam, ULONG size)
{
    BOOLEAN data = size && (kind == MIME_LPARAM_IN || kind == MIME_LPARAM_INOUT);
    ULONG head = id == WH_CALLWNDPROCRET ? sizeof(CWPRETSTRUCT) : sizeof(CWPSTRUCT);
    ULONG total = head + (data ? size : 0);
    UCHAR local[sizeof(CWPRETSTRUCT) + 64];
    UCHAR *buffer = total <= sizeof(local) ? local : W32Alloc(total);
    if (!buffer)
        return;
    if (id == WH_CALLWNDPROCRET) {
        CWPRETSTRUCT ret = {result, shown, wparam, msg, hwnd};
        memcpy(buffer, &ret, head);
    } else {
        CWPSTRUCT cwp = {shown, wparam, msg, hwnd};
        memcpy(buffer, &cwp, head);
    }
    if (data)
        memcpy(buffer + head, (PVOID)lparam, size);
    HookCall(id, t, HC_ACTION, own, (LPARAM)buffer, total, nullptr);
    if (buffer != local)
        W32Free(buffer);
}

/* run the window procedure between WH_CALLWNDPROC and WH_CALLWNDPROCRET. */
static LRESULT call_sent(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG kind, ULONG size, BOOLEAN own)
{
    W32THREAD *t = w->Thread;
    HWND hwnd = WndHandle(w);
    LPARAM shown = kind == MIME_LPARAM_VALUE ? lparam : 0;
    if (HookActive(WH_CALLWNDPROC, t)) {
        UserReference(&w->Header);
        call_wnd_hook(WH_CALLWNDPROC, t, own, 0, shown, wparam, msg, hwnd, kind, lparam, size);
        BOOLEAN gone = w->Header.Destroyed;
        UserDereference(&w->Header);
        if (gone)
            return 0;
    }
    LRESULT r = MsgCallWindowProc(w, w->Proc, w->Ansi, msg, wparam, lparam, kind, size);
    if (HookActive(WH_CALLWNDPROCRET, t))
        call_wnd_hook(WH_CALLWNDPROCRET, t, own, r, shown, wparam, msg, hwnd, kind, lparam, size);
    return r;
}

void MsgProcessSent(W32THREAD *t)
{
    while (!IsListEmpty(&t->Sent)) {
        SENT_MESSAGE *sm = CONTAINING_RECORD(RemoveHeadList(&t->Sent), SENT_MESSAGE, Links);
        if (sm->ResultCall) {
            call_async(t, sm->AsyncProc, sm->Hwnd, sm->Msg, sm->AsyncData, sm->Result);
            W32Free(sm);
            continue;
        }
        SENT_MESSAGE *prev = t->Receiving;
        BOOLEAN prev_replied = t->Replied;
        ULONG prev_kind = t->ReceivingKind;
        sm->Outer = prev;
        t->Receiving = sm;
        t->Replied = FALSE;
        t->ReceivingKind = sm->Callback ? 4 : sm->Notify ? 2 : 1;
        LRESULT r = 0;
        if (sm->Hook) {
            HOOK *h = HookLookup(sm->Hook);
            if (h && h->Owner == t)
                r = HookInvoke(h, (int)sm->Msg, sm->Wparam, sm->Lparam, sm->LparamSize);
        } else {
            WND *w = WndLookup(sm->Hwnd);
            if (w && w->Thread == t)
                r = call_sent(w, sm->Msg, sm->Wparam, sm->Lparam, sm->LparamKind, sm->LparamSize, FALSE);
            else
                sm->Aborted = TRUE;
        }
        t->Receiving = prev;
        /* the sender now owns an answered message. */
        BOOLEAN replied = t->Replied;
        t->Replied = prev_replied;
        t->ReceivingKind = prev_kind;
        if (!replied)
            complete_sent(sm, r);
    }
}

static LRESULT send_to_thread(W32THREAD *target, WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG kind,
                              ULONG size, BOOLEAN notify, SEND_OPTS *opts)
{
    W32THREAD *self = W32GetThread();
    if (opts && (opts->Flags & SMTO_ABORTIFHUNG) && thread_hung(target)) {
        opts->Refused = TRUE;
        return 0;
    }
    SENT_MESSAGE *sm = W32Alloc(sizeof(SENT_MESSAGE));
    if (!sm)
        return 0;
    sm->Sender = notify ? nullptr : self;
    sm->Hwnd = WndHandle(w);
    sm->Msg = msg;
    sm->Wparam = wparam;
    sm->Lparam = lparam;
    sm->LparamKind = kind;
    sm->LparamSize = size;
    sm->Notify = notify;
    sm->Owned = opts && opts->Owned && kind != MIME_LPARAM_VALUE;
    if (opts && opts->AsyncProc && self) {
        sm->Callback = TRUE;
        sm->SenderTid = self->Tid;
        sm->AsyncProc = opts->AsyncProc;
        sm->AsyncData = opts->AsyncData;
    }
    if (notify && kind != MIME_LPARAM_VALUE && size && !sm->Owned) {
        /* nobody waits, so the receiver gets its own copy. */
        PVOID copy = W32Alloc(size);
        if (!copy) {
            W32Free(sm);
            return 0;
        }
        memcpy(copy, (PVOID)lparam, size);
        sm->Lparam = (LPARAM)copy;
        sm->Owned = TRUE;
    }
    KeInitializeEvent(&sm->Done, NotificationEvent, FALSE);
    InsertTailList(&target->Sent, &sm->Links);
    QueueWake(target, QS_SENDMESSAGE);
    if (notify) {
        if (opts)
            opts->Taken = opts->Owned;
        return 1;
    }
    /* serve incoming messages while waiting to avoid deadlock between threads sending to each other. */
    BOOLEAN block = opts && (opts->Flags & SMTO_BLOCK);
    ULONG64 deadline = opts && opts->Timeout ? now_ms() + opts->Timeout : 0;
    BOOLEAN timed_out = FALSE;
    if (self)
        self->InWait = TRUE;
    while (!sm->Completed) {
        if (self && !block && !IsListEmpty(&self->Sent)) {
            MsgProcessSent(self);
            continue;
        }
        LARGE_INTEGER timeout, *pt = nullptr;
        if (deadline) {
            LONG64 left = (LONG64)(deadline - now_ms());
            if (left <= 0) {
                if (opts->Flags & SMTO_NOTIMEOUTIFNOTHUNG && !thread_hung(target)) {
                    deadline = now_ms() + opts->Timeout;
                    continue;
                }
                timed_out = TRUE;
                break;
            }
            timeout.QuadPart = -left * 10000;
            pt = &timeout;
        }
        BOOLEAN serve = self && !block;
        PVOID objects[2] = {&sm->Done, serve ? (PVOID)&self->Event : (PVOID)&sm->Done};
        ULONG depth = UserLeaveAll();
        NTSTATUS status = KeWaitForMultipleObjects(serve ? 2 : 1, objects, WaitAny, UserRequest, UserMode, FALSE, pt,
                                                   nullptr);
        UserReenter(depth);
        if (status == STATUS_THREAD_IS_TERMINATING && !sm->Completed)
            break;
    }
    if (self)
        self->InWait = FALSE;
    if (!sm->Completed) {
        /* the receiver will complete the message after the sender has stopped waiting. */
        sm->Notify = TRUE;
        sm->Sender = nullptr;
        sm->Callback = FALSE;
        if (!sm->Owned)
            sm->LparamKind = MIME_LPARAM_VALUE;
        if (opts) {
            opts->TimedOut = timed_out;
            opts->Taken = sm->Owned;
        }
        if (timed_out)
            W32SetLastError(ERROR_TIMEOUT);
        return 0;
    }
    LRESULT r = sm->Result;
    if (opts)
        opts->Aborted = sm->Aborted;
    W32Free(sm);
    return r;
}

static LRESULT send_to_window(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG kind, ULONG size, SEND_OPTS *opts)
{
    if (!w || w->Header.Destroyed)
        return 0;
    W32THREAD *target = w->Thread;
    if (!target)
        return DefWindowProcKernel(w, msg, wparam, lparam);
    if (target == W32GetThread())
        return call_sent(w, msg, wparam, lparam, kind, size, TRUE);
    if (target->Destroying)
        return 0;
    return send_to_thread(target, w, msg, wparam, lparam, kind, size, FALSE, opts);
}

/*
 * send from a kernel thread without a queue, used during shutdown. wait without the lock until an answer, timeout or
 * event so the receiving thread can respond. the receiver frees an unanswered message.
 */
ULONG MsgSendSystem(HANDLE tid, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG64 timeout_ms,
                    PKEVENT *events, ULONG event_count, LRESULT *result)
{
    UserEnter();
    W32THREAD *t = W32LookupThread(tid);
    WND *w = WndLookup(hwnd);
    if (!t || !w || w->Thread != t) {
        UserLeave();
        return MSGSYS_GONE;
    }
    SENT_MESSAGE *sm = W32Alloc(sizeof(SENT_MESSAGE));
    if (!sm) {
        UserLeave();
        return MSGSYS_GONE;
    }
    sm->Hwnd = hwnd;
    sm->Msg = msg;
    sm->Wparam = wparam;
    sm->Lparam = lparam;
    sm->LparamKind = MIME_LPARAM_VALUE;
    KeInitializeEvent(&sm->Done, NotificationEvent, FALSE);
    InsertTailList(&t->Sent, &sm->Links);
    QueueWake(t, QS_SENDMESSAGE);
    UserLeave();

    PVOID objects[5] = {&sm->Done};
    ULONG n = 1;
    for (ULONG i = 0; i < event_count && i < 4; i++)
        objects[n++] = events[i];
    LARGE_INTEGER timeout = {.QuadPart = -(LONGLONG)(timeout_ms * 10000)};
    NTSTATUS status = KeWaitForMultipleObjects(n, objects, WaitAny, Executive, KernelMode, FALSE, &timeout, nullptr);
    UserEnter();
    ULONG answer;
    if (sm->Completed) {
        *result = sm->Result;
        answer = sm->Aborted ? MSGSYS_GONE : MSGSYS_OK;
        W32Free(sm);
    } else {
        sm->Notify = TRUE;
        answer = status == STATUS_TIMEOUT ? MSGSYS_TIMEOUT : MSGSYS_EVENT0 + (ULONG)(status - STATUS_WAIT_1);
    }
    UserLeave();
    return answer;
}

LRESULT MsgSendKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG lparam_kind, ULONG lparam_size)
{
    return send_to_window(w, msg, wparam, lparam, lparam_kind, lparam_size, nullptr);
}

LRESULT MsgSend(WND *w, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return MsgSendKernel(w, msg, wparam, lparam, MIME_LPARAM_VALUE, 0);
}

/* timers. */

typedef struct TIMER {
    LIST_ENTRY Links;
    W32THREAD *Thread;
    HWND Hwnd;
    UINT_PTR Id;
    UINT Elapse;
    TIMERPROC Proc;
    ULONG64 Due; /* milliseconds of interrupt time. */
    /* WM_SYSTIMER timers, such as caret blinking, are separate from application timers with the same id. */
    BOOLEAN System;
} TIMER;

static LIST_ENTRY g_timers = {&g_timers, &g_timers};

static ULONG64 now_ms(void)
{
    return KeQueryInterruptTime() / 10000;
}

static TIMER *find_timer(W32THREAD *t, HWND hwnd, UINT_PTR id, BOOLEAN system)
{
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers; e = e->Flink) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        if (tm->Hwnd == hwnd && tm->Id == id && tm->System == system && (hwnd || tm->Thread == t))
            return tm;
    }
    return nullptr;
}

UINT_PTR TimerSet(W32THREAD *t, HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc, BOOLEAN system)
{
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    if ((hwnd && (!w || w->Thread != t)) || !t)
        return 0;
    if (elapse < USER_TIMER_MINIMUM)
        elapse = USER_TIMER_MINIMUM;
    if (elapse > USER_TIMER_MAXIMUM)
        elapse = USER_TIMER_MAXIMUM;
    if (!hwnd && !id) {
        static UINT_PTR next_id = 0x7FFE;
        do
            id = ++next_id;
        while (find_timer(t, nullptr, id, system));
    }
    TIMER *tm = find_timer(t, hwnd, id, system);
    if (!tm) {
        tm = W32Alloc(sizeof(TIMER));
        if (!tm)
            return 0;
        InsertTailList(&g_timers, &tm->Links);
    }
    tm->Thread = t;
    tm->Hwnd = hwnd;
    tm->Id = id;
    tm->Elapse = elapse;
    tm->Proc = proc;
    tm->System = system;
    tm->Due = now_ms() + elapse;
    QueueWake(t, 0);
    return hwnd ? (id ? id : 1) : id;
}

BOOLEAN TimerKill(W32THREAD *t, HWND hwnd, UINT_PTR id, BOOLEAN system)
{
    TIMER *tm = find_timer(t, hwnd, id, system);
    if (tm) {
        RemoveEntryList(&tm->Links);
        W32Free(tm);
    }
    return tm != nullptr;
}

UINT_PTR NTAPI NtUserSetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc, ULONG tolerance)
{
    UserEnter();
    UINT_PTR result = TimerSet(W32GetThread(), hwnd, id, elapse, proc, FALSE);
    UserLeave();
    (void)tolerance;
    return result;
}

BOOL NTAPI NtUserKillTimer(HWND hwnd, UINT_PTR id)
{
    UserEnter();
    BOOLEAN killed = TimerKill(W32GetThread(), hwnd, id, FALSE);
    UserLeave();
    return killed;
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
        return msg_hwnd == nullptr;
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
    BOOLEAN user = range_matches(WM_TIMER, min, max), system = range_matches(WM_SYSTIMER, min, max);
    if (!user && !system)
        return FALSE;
    ULONG64 now = now_ms();
    for (PLIST_ENTRY e = g_timers.Flink; e != &g_timers; e = e->Flink) {
        TIMER *tm = CONTAINING_RECORD(e, TIMER, Links);
        if (tm->Thread != t || tm->Due > now || !hwnd_matches(tm->Hwnd, hwnd) || !(tm->System ? system : user))
            continue;
        out->hwnd = tm->Hwnd;
        out->message = tm->System ? WM_SYSTIMER : WM_TIMER;
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

/* retrieval. */

static void set_message_state(W32THREAD *t, const MSG *m)
{
    t->MessageTime = m->time;
    t->MessagePos = m->pt;
}

/* WH_KEYBOARD and WH_MOUSE see input messages as they leave the queue, a nonzero answer discards them. */
static BOOLEAN input_hook_discards(W32THREAD *t, const MSG *m, BOOLEAN remove)
{
    int code = remove ? HC_ACTION : HC_NOREMOVE;
    switch (m->message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        return HookActive(WH_KEYBOARD, t) && HookCall(WH_KEYBOARD, t, code, m->wParam, m->lParam, 0, nullptr);
    }
    BOOLEAN nc = m->message >= WM_NCMOUSEMOVE && m->message <= WM_NCXBUTTONDBLCLK;
    if ((!nc && (m->message < WM_MOUSEFIRST || m->message > WM_MOUSELAST)) || !HookActive(WH_MOUSE, t))
        return FALSE;
    MOUSEHOOKSTRUCTEX mh = {};
    mh.pt = m->pt;
    DpiPixelConvert(&mh.pt, DPI_PHYSICAL, DpiThreadAwareness(t));
    mh.hwnd = m->hwnd;
    mh.wHitTestCode = nc ? LOWORD(m->wParam) : HTCLIENT;
    mh.dwExtraInfo = t->ExtraInfo;
    if (m->message == WM_MOUSEWHEEL || m->message == WM_MOUSEHWHEEL ||
        (m->message >= WM_XBUTTONDOWN && m->message <= WM_XBUTTONDBLCLK) ||
        (m->message >= WM_NCXBUTTONDOWN && m->message <= WM_NCXBUTTONDBLCLK))
        mh.mouseData = (DWORD)(m->wParam & 0xFFFF0000);
    return HookCall(WH_MOUSE, t, code, m->message, (LPARAM)&mh, sizeof(mh), nullptr) != 0;
}

static void free_input(W32THREAD *t, QUEUED_MESSAGE *qm)
{
    for (PLIST_ENTRY e = t->Input.Flink; e != &t->Input; e = e->Flink)
        if (e == &qm->Links) {
            RemoveEntryList(e);
            W32Free(qm);
            return;
        }
}

BOOLEAN MsgPeek(W32THREAD *t, MSG *out, HWND hwnd, UINT min, UINT max, UINT flags, BOOLEAN wait)
{
    BOOLEAN remove = (flags & PM_REMOVE) != 0;
    for (;;) {
        t->LastRead = W32TickCount();
        MsgProcessSent(t);
        t->WakeBits = 0;

scan_posted:
        for (PLIST_ENTRY e = t->Posted.Flink; e != &t->Posted; e = e->Flink) {
            QUEUED_MESSAGE *qm = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
            if (qm->Kind == QM_WINDOW_SHOW || qm->Kind == QM_WINDOW_PLACEMENT) {
                RemoveEntryList(e);
                WND *w = WndLookup(qm->Msg.hwnd);
                if (w && w->Thread == t) {
                    if (qm->Kind == QM_WINDOW_SHOW)
                        WndShow(w, (int)qm->Msg.wParam);
                    else
                        WndSetPlacement(w, &qm->Placement);
                }
                W32Free(qm);
                goto scan_posted;
            }
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

        BOOLEAN rescan = FALSE;
        for (PLIST_ENTRY e = t->Input.Flink; e != &t->Input;) {
            QUEUED_MESSAGE *qm = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
            e = e->Flink;
            BOOLEAN consumed = FALSE;
            if (qm->Input.type == MIME_INPUT_WINDOW_STATE) {
                /* state commands may enter a nested message loop. */
                RemoveEntryList(&qm->Links);
                InputTranslate(t, qm, out, hwnd, min, max, remove, &consumed);
                W32Free(qm);
                rescan = TRUE;
                break;
            }
            BOOLEAN got = InputTranslate(t, qm, out, hwnd, min, max, remove, &consumed);
            if (consumed || (got && remove)) {
                RemoveEntryList(&qm->Links);
                W32Free(qm);
                qm = nullptr;
            }
            if (got) {
                set_message_state(t, out);
                if (!input_hook_discards(t, out, remove))
                    return TRUE;
                /* discard regardless of flags. the hook ran unlocked, so find the message again. */
                if (qm)
                    free_input(t, qm);
                rescan = TRUE;
                break;
            }
        }
        if (rescan)
            continue;

        if (t->QuitPosted && range_matches(WM_QUIT, min, max)) {
            out->hwnd = nullptr;
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
        LARGE_INTEGER timeout, *pt = nullptr;
        if (due >= 0) {
            LONG64 delta = due - (LONG64)now_ms();
            if (delta < 0)
                delta = 0;
            timeout.QuadPart = -delta * 10000;
            pt = &timeout;
        }
        t->InWait = TRUE;
        ULONG depth = UserLeaveAll();
        NTSTATUS status = KeWaitForSingleObject(&t->Event, UserRequest, UserMode, FALSE, pt);
        UserReenter(depth);
        t->InWait = FALSE;
        t->LastRead = W32TickCount();
        if (status == STATUS_THREAD_IS_TERMINATING) {
            out->hwnd = nullptr;
            out->message = WM_QUIT;
            out->wParam = 0;
            out->lParam = 0;
            return TRUE;
        }
    }
}

ULONG MsgInSendFlags(W32THREAD *t)
{
    if (!t || !t->ReceivingKind)
        return 0;
    return t->ReceivingKind | (t->Replied ? 8 : 0);
}

/*
 * ReplyMessage releases the sender while the procedure continues. only plain parameters can be answered early;
 * messages with data must wait for copyback when the procedure returns, but the call still succeeds.
 */
BOOLEAN MsgReply(W32THREAD *t, LRESULT result)
{
    SENT_MESSAGE *sm = t ? t->Receiving : nullptr;
    if (!sm || t->ReceivingKind != 1 || t->Replied)
        return FALSE;
    if (sm->LparamKind == MIME_LPARAM_VALUE && !sm->Hook) {
        t->Replied = TRUE;
        complete_sent(sm, result);
    }
    return TRUE;
}

/* message services. */

static BOOL copy_msg_out(MSG *user, const MSG *m)
{
    return NT_SUCCESS(MimeCopyToUser(user, m, sizeof(*m)));
}

/* WH_GETMESSAGE sees every retrieved message and may change it. */
static void get_message_hook(W32THREAD *t, MSG *m, UINT flags)
{
    if (HookActive(WH_GETMESSAGE, t))
        HookCall(WH_GETMESSAGE, t, HC_ACTION, flags & (PM_REMOVE | PM_NOYIELD), (LPARAM)m, sizeof(*m), nullptr);
}

BOOL NTAPI NtUserGetMessage(MSG *user_msg, HWND hwnd, UINT min, UINT max)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    MSG m;
    memset(&m, 0, sizeof(m));
    /* existing queue contents no longer count as new input for MsgWaitForMultipleObjects. */
    if (t)
        t->ChangeBits = 0;
    if (t && MsgPeek(t, &m, hwnd, min, max, PM_REMOVE, TRUE)) {
        DpiPixelConvert(&m.pt, DPI_PHYSICAL, DpiThreadAwareness(t));
        get_message_hook(t, &m, PM_REMOVE);
    }
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
    if (t)
        t->ChangeBits = 0;
    BOOLEAN got = t && MsgPeek(t, &m, hwnd, min, max, flags, FALSE);
    if (got) {
        DpiPixelConvert(&m.pt, DPI_PHYSICAL, DpiThreadAwareness(t));
        get_message_hook(t, &m, flags);
    }
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
        t->LastRead = W32TickCount();
        MsgProcessSent(t);
        if (!(t->ChangeBits & (QS_ALLINPUT))) {
            PaintFlushThread(t);
            LONG64 due = TimerNextDue(t);
            LARGE_INTEGER timeout, *pt = nullptr;
            if (due >= 0) {
                LONG64 delta = due - (LONG64)now_ms();
                timeout.QuadPart = -(delta < 0 ? 0 : delta) * 10000;
                pt = &timeout;
            }
            t->InWait = TRUE;
            ULONG depth = UserLeaveAll();
            KeWaitForSingleObject(&t->Event, UserRequest, UserMode, FALSE, pt);
            UserReenter(depth);
            t->InWait = FALSE;
            t->LastRead = W32TickCount();
            MsgProcessSent(t);
        }
        t->ChangeBits = 0;
    }
    UserLeave();
    return TRUE;
}

static ULONG pending_bits(W32THREAD *t)
{
    ULONG pending = 0;
    for (PLIST_ENTRY e = t->Posted.Flink; e != &t->Posted; e = e->Flink) {
        QUEUED_MESSAGE *qm = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
        pending |= qm->Msg.message == WM_INPUT ? QS_RAWINPUT : QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;
    }
    if (t->QuitPosted)
        pending |= QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;
    if (!IsListEmpty(&t->Input))
        pending |= QS_MOUSE | QS_KEY;
    if (!IsListEmpty(&t->Sent))
        pending |= QS_SENDMESSAGE;
    return pending;
}

#define WAIT_HANDLES 64
#define WAIT_FAILED_VALUE 0xFFFFFFFFu
#define WAIT_FOREVER 0xFFFFFFFFu
#define WAIT_OBJECT 0u
#define WAIT_ABANDONED 0x80u
#define WAIT_TIMED_OUT 0x102u
#define WAIT_IO 0xC0u

/*
 * wake for matching input received since the last queue check, or any pending matching input with
 * MWMO_INPUTAVAILABLE. user32 handles waiting for all handles.
 */
DWORD NTAPI NtUserMsgWaitForMultipleObjectsEx(DWORD count, const HANDLE *user_handles, DWORD timeout, DWORD mask,
                                              DWORD flags)
{
    HANDLE handles[WAIT_HANDLES];
    if (count >= WAIT_HANDLES || (flags & MWMO_WAITALL) ||
        (count && !NT_SUCCESS(MimeCopyFromUser(handles, user_handles, count * sizeof(HANDLE))))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED_VALUE;
    }
    PVOID objects[WAIT_HANDLES];
    for (DWORD i = 0; i < count; i++) {
        if (!NT_SUCCESS(ObReferenceObjectByHandle(handles[i], SYNCHRONIZE, nullptr, UserMode, &objects[i], nullptr))) {
            for (DWORD j = 0; j < i; j++)
                ObDereferenceObject(objects[j]);
            W32SetLastError(ERROR_INVALID_HANDLE);
            return WAIT_FAILED_VALUE;
        }
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    objects[count] = t ? &t->Event : nullptr;
    void *blocks = count + 1 > 3 ? W32Alloc((count + 1) * 64) : nullptr;
    LONG64 deadline = timeout == WAIT_FOREVER ? -1 : (LONG64)now_ms() + timeout;
    DWORD result = WAIT_FAILED_VALUE;
    while (t) {
        t->LastRead = W32TickCount();
        MsgProcessSent(t);
        ULONG ready = (flags & MWMO_INPUTAVAILABLE) ? pending_bits(t) | t->ChangeBits : t->ChangeBits;
        if (ready & mask) {
            t->ChangeBits &= ~mask;
            result = WAIT_OBJECT + count;
            break;
        }
        if ((mask & QS_PAINT) && WndFindNeedsPaint(t, nullptr)) {
            result = WAIT_OBJECT + count;
            break;
        }
        LONG64 due = TimerNextDue(t);
        if ((mask & QS_TIMER) && due >= 0 && due <= (LONG64)now_ms()) {
            result = WAIT_OBJECT + count;
            break;
        }
        LARGE_INTEGER wait, *pw = nullptr;
        LONG64 until = deadline;
        if ((mask & QS_TIMER) && due >= 0 && (until < 0 || due < until))
            until = due;
        if (until >= 0) {
            LONG64 delta = until - (LONG64)now_ms();
            wait.QuadPart = -(delta < 0 ? 0 : delta) * 10000;
            pw = &wait;
        }
        if (count + 1 > 3 && !blocks)
            break;
        PaintFlushThread(t);
        t->InWait = TRUE;
        ULONG depth = UserLeaveAll();
        NTSTATUS status = KeWaitForMultipleObjects(count + 1, objects, WaitAny, UserRequest, UserMode,
                                                   (flags & MWMO_ALERTABLE) != 0, pw, blocks);
        UserReenter(depth);
        t->InWait = FALSE;
        t->LastRead = W32TickCount();
        if (status == STATUS_USER_APC || status == STATUS_ALERTED) {
            result = WAIT_IO;
            break;
        }
        if (status == STATUS_THREAD_IS_TERMINATING)
            break;
        if ((ULONG)status < count) {
            result = WAIT_OBJECT + (ULONG)status;
            break;
        }
        if ((ULONG)status >= 0x80 && (ULONG)status < 0x80 + count) {
            result = WAIT_ABANDONED + ((ULONG)status - 0x80);
            break;
        }
        if (status == STATUS_TIMEOUT && deadline >= 0 && (LONG64)now_ms() >= deadline) {
            result = WAIT_TIMED_OUT;
            break;
        }
    }
    UserLeave();
    W32Free(blocks);
    for (DWORD i = 0; i < count; i++)
        ObDereferenceObject(objects[i]);
    return result;
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
    POINT at = t ? t->MessagePos : (POINT){0, 0};
    DpiPixelConvert(&at, DPI_PHYSICAL, DpiThreadAwareness(t));
    DWORD pos = t ? (DWORD)MAKELONG((SHORT)at.x, (SHORT)at.y) : 0;
    UserLeave();
    return (BOOL)pos;
}

static BOOLEAN is_broadcast(HWND hwnd)
{
    return hwnd == HWND_BROADCAST || hwnd == (HWND)(LONG_PTR)-1 || hwnd == (HWND)0xFFFFFFFFull;
}

/* snapshot broadcast recipients because windows may be created or destroyed during delivery. */
static HWND *top_level_windows(ULONG *count)
{
    WND *d = WndDesktop();
    ULONG n = 0;
    for (WND *c = d ? d->FirstChild : nullptr; c; c = c->Next)
        n++;
    HWND *list = W32Alloc((n + 1) * sizeof(HWND));
    *count = 0;
    if (!list)
        return nullptr;
    for (WND *c = d ? d->FirstChild : nullptr; c && *count < n; c = c->Next) {
        /* message only windows hear no broadcast. */
        if ((c->Flags & WNDF_DESTROYING) || (c->ExStyle & 0x80000000u) || !c->Thread)
            continue;
        list[(*count)++] = WndHandle(c);
    }
    return list;
}

/* these messages never cross process boundaries. */
#define MSG_INITDIALOG 0x0110
#define MSG_NOTIFY 0x004E

/* dialog initialization, notifications and password edit contents stay within their process. */
static BOOLEAN stays_in_process(const WND *w, UINT msg)
{
    if (msg == MSG_INITDIALOG || msg == MSG_NOTIFY)
        return TRUE;
    BOOLEAN reveals_password = msg == WM_GETTEXT || msg == EM_GETLINE || msg == EM_SETPASSWORDCHAR;
    BOOLEAN password_edit = w->Class && w->Class->Fnid == MIME_FNID_EDIT && (w->Style & ES_PASSWORD);
    return reveals_password && password_edit;
}

/* probe both ends before attempting to write into sender memory. */
static BOOLEAN ends_readable(LPARAM user, SIZE_T size)
{
    UCHAR byte;
    if (!size)
        return TRUE;
    return NT_SUCCESS(MimeCopyFromUser(&byte, (PVOID)user, 1)) &&
           NT_SUCCESS(MimeCopyFromUser(&byte, (PVOID)(user + size - 1), 1));
}

/* the supplied structure size must match the message. */
static BOOLEAN sized_struct_usable(LPARAM lparam, ULONG expected)
{
    DWORD claimed;
    if (!lparam || !NT_SUCCESS(MimeCopyFromUser(&claimed, (PVOID)lparam, sizeof(claimed))))
        return FALSE;
    if (claimed != expected) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return ends_readable(lparam, expected);
}

/* these device changes require lparam data. */
static BOOLEAN device_change_needs_data(WPARAM event)
{
    return event == 0x8000 || event == 0x8004 || (event >= 0x8007 && event <= 0x800A);
}

/*
 * threads in one process share the sender's pointers, but still reject parameters that would fail cross-process
 * copying.
 */
static BOOLEAN local_pointers_usable(MSG_RULE rule, WPARAM wparam, LPARAM lparam)
{
    switch (rule.Class) {
    case MC_STRUCT_IN:
    case MC_STRUCT_INOUT:
    case MC_STRUCT_OUT:
        return lparam && ends_readable(lparam, rule.Size);
    case MC_TITLEBAR:
    case MC_HELP:
        return sized_struct_usable(lparam, rule.Size);
    case MC_COPYDATA: {
        COPYDATASTRUCT data;
        if (!lparam || !NT_SUCCESS(MimeCopyFromUser(&data, (PVOID)lparam, sizeof(data))))
            return FALSE;
        return !data.lpData || ends_readable((LPARAM)data.lpData, data.cbData);
    }
    case MC_NCCALCSIZE:
    case MC_MDICREATE:
    case MC_LINE_OUT:
    case MC_WINHELP:
        return lparam != 0;
    case MC_STRING_OUT:
        return wparam <= 0x3FFFFFFF && (lparam || !wparam);
    case MC_DEVCHANGE:
        if (!lparam && device_change_needs_data(wparam)) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return TRUE;
    default:
        return TRUE;
    }
}

static LRESULT deliver(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG kind, ULONG size, DWORD type,
                       SEND_OPTS *opts)
{
    W32THREAD *self = W32GetThread();
    W32THREAD *target = w->Thread;
    BOOLEAN remote = target && target != self && !target->Destroying && !w->Header.Destroyed;
    if (type == MIME_MSGCALL_SENDNOTIFY) {
        if (remote)
            return send_to_thread(target, w, msg, wparam, lparam, kind, size, TRUE, opts);
        return send_to_window(w, msg, wparam, lparam, kind, size, nullptr);
    }
    if (type == MIME_MSGCALL_SENDCALLBACK) {
        if (remote)
            return send_to_thread(target, w, msg, wparam, lparam, kind, size, TRUE, opts);
        LRESULT r = send_to_window(w, msg, wparam, lparam, kind, size, nullptr);
        if (self && opts->AsyncProc)
            call_async(self, opts->AsyncProc, WndHandle(w), msg, opts->AsyncData, r);
        return 1;
    }
    return send_to_window(w, msg, wparam, lparam, kind, size, opts);
}

/* returns the window procedure result for a synchronous send; *ok supplies the SendMessageTimeout success flag. */
static LRESULT send_one(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, DWORD type, SEND_OPTS *opts, BOOLEAN *ok)
{
    W32THREAD *self = W32GetThread();
    W32THREAD *target = w->Thread;
    *ok = FALSE;
    MSG_RULE rule = MsgRuleFor(msg);
    if (rule.Class == MC_KERNEL) {
        *ok = TRUE;
        return 0;
    }
    BOOLEAN cross_thread = target && target != self;
    BOOLEAN cross_process = cross_thread && (!self || target->Process != self->Process);
    if (cross_thread && (rule.Class == MC_CREATE || (cross_process && stays_in_process(w, msg)))) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
    BOOLEAN wow_layout = target && target->Process && target->Process->Wow && rule.Layout;
    BOOLEAN notifies = type == MIME_MSGCALL_SENDNOTIFY || type == MIME_MSGCALL_SENDCALLBACK;
    BOOLEAN marshal = rule.Class != MC_VALUE && rule.Class != MC_CREATE &&
                      (cross_process || wow_layout || (notifies && cross_thread));
    SEND_OPTS local = {};
    if (opts)
        local = *opts;
    local.TimedOut = local.Aborted = local.Refused = local.Taken = FALSE;
    LRESULT r;
    if (!marshal) {
        if (cross_thread && !local_pointers_usable(rule, wparam, lparam))
            return 0;
        r = deliver(w, msg, wparam, lparam, MIME_LPARAM_VALUE, 0, type, &local);
    } else {
        LONG hint = -1;
        if (rule.Class == MC_ITEM_OUT) {
            /* querying text length may destroy the control window. */
            HWND handle = WndHandle(w);
            hint = (LONG)send_to_window(w, msg == LB_GETTEXT ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, wparam, 0,
                                        MIME_LPARAM_VALUE, 0, nullptr);
            w = WndLookup(handle);
            if (!w)
                return 0;
        }
        MSG_CAPTURE c;
        if (!MsgCapture(&c, w, msg, wparam, lparam, rule, hint)) {
            LRESULT refused = c.Refused;
            MsgCaptureFree(&c);
            return refused;
        }
        local.Owned = c.Buffer != nullptr;
        ULONG kind = c.Buffer ? c.Kind : MIME_LPARAM_VALUE;
        LPARAM lp = c.Buffer ? (LPARAM)c.Buffer : c.Lparam;
        r = deliver(w, msg, c.Wparam, lp, kind, c.Size, type, &local);
        BOOLEAN done = !notifies && !local.TimedOut && !local.Refused && !local.Aborted;
        MsgCaptureFinish(&c, &r, done);
        if (!local.Taken)
            MsgCaptureFree(&c);
    }
    BOOLEAN failed = local.TimedOut || local.Refused;
    if (local.Aborted && (local.Flags & SMTO_ERRORONEXIT)) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        failed = TRUE;
    }
    *ok = !failed;
    if (opts) {
        opts->TimedOut = local.TimedOut;
        opts->Aborted = local.Aborted;
        opts->Refused = local.Refused;
    }
    return failed ? 0 : r;
}

/* application-defined messages have no recipient in a broadcast. */
#define MSG_PROGRAM_FIRST 0x0400
#define MSG_PROGRAM_LAST 0xBFFF

/*
 * broadcast to top level windows using single-send validation. application-defined messages reach nobody and set an
 * invalid-parameter error, but the call succeeds.
 */
static LRESULT broadcast_send(UINT msg, WPARAM wparam, LPARAM lparam, DWORD type, SEND_OPTS *opts)
{
    if (msg >= MSG_PROGRAM_FIRST && msg <= MSG_PROGRAM_LAST) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 1;
    }
    ULONG count;
    HWND *windows = top_level_windows(&count);
    for (ULONG i = 0; i < count; i++) {
        WND *w = WndLookup(windows[i]);
        BOOLEAN delivered;
        if (w && !(w->Flags & WNDF_DESTROYING) && w->Thread)
            send_one(w, msg, wparam, lparam, type, opts, &delivered);
    }
    W32Free(windows);
    return 1;
}

/* the daylight time offset changed. Windows sends WM_TIMECHANGE; here it is posted. */
void NTAPI W32TimeChange(void)
{
    UserEnter();
    ULONG count;
    HWND *list = top_level_windows(&count);
    for (ULONG i = 0; i < count; i++) {
        WND *w = WndLookup(list[i]);
        if (w && !(w->Flags & WNDF_DESTROYING) && w->Thread)
            QueuePost(w->Thread, list[i], WM_TIMECHANGE, 0, 0);
    }
    W32Free(list);
    UserLeave();
}

static LRESULT send_user(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, DWORD type, ULONG_PTR info)
{
    SEND_OPTS opts = {};
    MIME_SEND_TIMEOUT st = {};
    if (type == MIME_MSGCALL_SENDTIMEOUT) {
        if (!info || !NT_SUCCESS(MimeCopyFromUser(&st, (PVOID)info, sizeof(st))))
            return 0;
        opts.Flags = st.Flags;
        opts.Timeout = st.Timeout;
    } else if (type == MIME_MSGCALL_SENDCALLBACK) {
        MIME_SEND_CALLBACK cb;
        if (!info || !NT_SUCCESS(MimeCopyFromUser(&cb, (PVOID)info, sizeof(cb))))
            return 0;
        opts.AsyncProc = cb.Proc;
        opts.AsyncData = cb.Data;
    }
    /* pointer parameters require a synchronous send. */
    if ((type == MIME_MSGCALL_SENDNOTIFY || type == MIME_MSGCALL_SENDCALLBACK) && MimeMsgIsSyncOnly(msg, wparam, FALSE)) {
        W32SetLastError(ERROR_MESSAGE_SYNC_ONLY);
        return 0;
    }
    LRESULT r = 0;
    BOOLEAN ok = TRUE;
    if (is_broadcast(hwnd)) {
        r = broadcast_send(msg, wparam, lparam, type, &opts);
        ok = TRUE;
    } else {
        WND *w = WndLookup(hwnd);
        if (!w) {
            W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            ok = FALSE;
        } else {
            r = send_one(w, msg, wparam, lparam, type, &opts, &ok);
        }
    }
    if (type == MIME_MSGCALL_SENDTIMEOUT) {
        st.Result = ok ? (ULONG_PTR)r : 0;
        MimeCopyToUser((PVOID)info, &st, sizeof(st));
        return ok;
    }
    return r;
}

#define BROADCAST_POST 100

/* BroadcastSystemMessage: the recipients and the way to send were checked by user32. */
static LRESULT broadcast_system(UINT msg, WPARAM wparam, LPARAM lparam, ULONG_PTR info)
{
    MIME_BROADCAST b;
    if (!info || !NT_SUCCESS(MimeCopyFromUser(&b, (PVOID)info, sizeof(b))))
        return 0;
    DWORD type = MIME_MSGCALL_SEND;
    if (b.Flags & BSF_POSTMESSAGE)
        type = BROADCAST_POST;
    else if (b.Flags & BSF_SENDNOTIFYMESSAGE)
        type = MIME_MSGCALL_SENDNOTIFY;
    SEND_OPTS opts = {};
    if (b.Flags & (BSF_NOHANG | BSF_FORCEIFHUNG))
        opts.Flags |= SMTO_ABORTIFHUNG;
    if (b.Flags & BSF_NOTIMEOUTIFNOTHUNG)
        opts.Flags |= SMTO_NOTIMEOUTIFNOTHUNG | SMTO_ABORTIFHUNG;
    if (opts.Flags)
        opts.Timeout = 5000;
    LRESULT result = 1;
    ULONG count;
    HWND *list = top_level_windows(&count);
    W32THREAD *self = W32GetThread();
    for (ULONG i = 0; i < count; i++) {
        WND *w = WndLookup(list[i]);
        if (!w || (w->Flags & WNDF_DESTROYING) || !w->Thread)
            continue;
        if ((b.Flags & BSF_IGNORECURRENTTASK) && w->Thread->Process == self->Process)
            continue;
        if (type == BROADCAST_POST) {
            QueuePost(w->Thread, list[i], msg, wparam, lparam);
            continue;
        }
        BOOLEAN ok;
        SEND_OPTS each = opts;
        LRESULT r = send_one(w, msg, wparam, lparam, type, &each, &ok);
        if ((b.Flags & BSF_QUERY) && ok && r == BROADCAST_QUERY_DENY) {
            b.Hwnd = list[i];
            result = 0;
            break;
        }
    }
    W32Free(list);
    MimeCopyToUser((PVOID)info, &b, sizeof(b));
    return result;
}

LRESULT NTAPI NtUserMessageCall(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG_PTR result_info, DWORD type,
                                BOOL ansi)
{
    if (msg & 0xFFFE0000u) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UserEnter();
    LRESULT r = 0;
    switch (type) {
    case MIME_MSGCALL_DEFWINDOWPROC: {
        WND *w = WndLookup(hwnd);
        r = w ? DefWindowProcUser(w, msg, wparam, lparam) : 0;
        break;
    }
    case MIME_MSGCALL_BROADCAST:
        r = broadcast_system(msg, wparam, lparam, result_info);
        break;
    case MIME_MSGCALL_SEND:
    case MIME_MSGCALL_SENDNOTIFY:
    case MIME_MSGCALL_SENDTIMEOUT:
    case MIME_MSGCALL_SENDCALLBACK:
        r = send_user(hwnd, msg, wparam, lparam, type, result_info);
        break;
    }
    UserLeave();
    (void)ansi;
    return r;
}

/* reject reserved messages and messages with sender-owned data, which cannot be posted. */
static BOOLEAN post_allowed(UINT msg, WPARAM wparam)
{
    if (msg < MSG_PROGRAM_FIRST && MimeMsgIsPrivate(msg)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (MimeMsgIsSyncOnly(msg, wparam, TRUE)) {
        W32SetLastError(ERROR_MESSAGE_SYNC_ONLY);
        return FALSE;
    }
    return TRUE;
}

/* only the input stack may post touch and gesture messages. */
static BOOLEAN input_signal(UINT msg)
{
    return msg == 0x0119 || msg == 0x011B || (msg >= 0x0241 && msg <= 0x0244);
}

/* post to every top level window with a queue. */
static BOOL post_to_all(UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (MimeMsgIsSyncOnly(msg, wparam, TRUE)) {
        W32SetLastError(ERROR_MESSAGE_SYNC_ONLY);
        return FALSE;
    }
    if (msg >= MSG_PROGRAM_FIRST && msg <= MSG_PROGRAM_LAST) {
        /* application-defined broadcasts have no recipients but still succeed. */
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return TRUE;
    }
    ULONG count;
    HWND *windows = top_level_windows(&count);
    for (ULONG i = 0; i < count; i++) {
        WND *w = WndLookup(windows[i]);
        if (w && w->Thread)
            QueuePost(w->Thread, windows[i], msg, wparam, lparam);
    }
    W32Free(windows);
    return TRUE;
}

/*
 * PostMessage checks the 17-bit limit, reserved message numbers, then the window. cross-process posts also enforce
 * message restrictions.
 */
BOOL NTAPI NtUserPostMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg & 0xFFFE0000u) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (msg < MSG_PROGRAM_FIRST && MimeMsgIsPrivate(msg)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    UserEnter();
    BOOL ok = FALSE;
    if (is_broadcast(hwnd)) {
        ok = post_to_all(msg, wparam, lparam);
    } else if (!hwnd) {
        W32THREAD *self = W32GetThread();
        if (MimeMsgIsSyncOnly(msg, wparam, TRUE))
            W32SetLastError(ERROR_MESSAGE_SYNC_ONLY);
        else if (self)
            ok = QueuePost(self, nullptr, msg, wparam, lparam);
    } else {
        WND *w = WndLookup(hwnd);
        W32THREAD *self = W32GetThread();
        if (!w || !w->Thread)
            W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        else if (input_signal(msg))
            W32SetLastError(ERROR_SIGNAL_REFUSED);
        else if (MimeMsgIsSyncOnly(msg, wparam, TRUE))
            W32SetLastError(ERROR_MESSAGE_SYNC_ONLY);
        else if (w->Thread->Process != (self ? self->Process : nullptr) && stays_in_process(w, msg))
            W32SetLastError(ERROR_ACCESS_DENIED);
        else
            ok = QueuePost(w->Thread, hwnd, msg, wparam, lparam);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserPostThreadMessage(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg & 0xFFFE0000u) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    BOOL ok = FALSE;
    if (post_allowed(msg, wparam)) {
        W32THREAD *t = W32LookupThread((HANDLE)(ULONG_PTR)thread);
        if (!t)
            W32SetLastError(ERROR_INVALID_THREAD_ID);
        else
            ok = QueuePost(t, nullptr, msg, wparam, lparam);
    }
    UserLeave();
    return ok;
}

/* default window procedure. */

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

static PWSTR capture_sz(LPARAM lparam, BOOLEAN user, ULONG *chars)
{
    *chars = 0;
    if (!lparam)
        return nullptr;
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
    HBRUSH b = w->Class ? w->Class->Background : nullptr;
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
        /* paint the client area in another window's DC, usually the parent's. */
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
    UINT dpi = WndFrameDpi(w);
    switch (msg) {
    case WM_NCCREATE:
        return TRUE;

    case WM_NCCALCSIZE: {
        RECT r;
        if (!read_lparam(&r, lparam, sizeof(r), user))
            return 0;
        /* the menu bar sits under the caption, as wide as the window inside its borders and before the scroll bars. */
        RECT inner = r;
        FrameClientFromWindow(&inner, w->Style & ~(WS_VSCROLL | WS_HSCROLL), w->ExStyle, dpi);
        LONG bar = 0;
        if (!(w->Style & WS_CHILD) && !(w->Style & WS_MINIMIZE))
            bar = MenuBarCompute(w, inner.left - r.left, inner.top - r.top, inner.right - inner.left);
        FrameClientFromWindow(&r, w->Style, w->ExStyle, dpi);
        if (bar) {
            r.top += bar;
            if (r.bottom < r.top)
                r.bottom = r.top;
        }
        if (MenuBarCalcWindow == w)
            w->MenuBarHeight = bar;
        write_lparam(lparam, &r, sizeof(r), user);
        return 0;
    }

    case WM_CHANGEUISTATE: {
        /* the state lives in the top level window, so a child hands the change up. */
        UINT action = LOWORD(wparam), bits = HIWORD(wparam) & (UISF_HIDEFOCUS | UISF_HIDEACCEL);
        if ((w->Style & WS_CHILD) && w->Parent && w->Parent != WndDesktop())
            return MsgSend(w->Parent, msg, wparam, lparam);
        UCHAR now = action == UIS_SET ? (w->UiState | bits) : action == UIS_CLEAR ? (w->UiState & ~bits) : w->UiState;
        if (now != w->UiState)
            MsgSend(w, WM_UPDATEUISTATE, wparam, lparam);
        return 0;
    }

    case WM_UPDATEUISTATE: {
        UINT action = LOWORD(wparam), bits = HIWORD(wparam) & (UISF_HIDEFOCUS | UISF_HIDEACCEL);
        UCHAR now = action == UIS_SET ? (w->UiState | bits) : action == UIS_CLEAR ? (w->UiState & ~bits) : w->UiState;
        if (now != w->UiState) {
            w->UiState = now;
            if (w->MenuBarHeight)
                MenuBarRedraw(w);
        }
        return 0;
    }

    case WM_QUERYUISTATE:
        return w->UiState;

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
            WndShow(w, SW_MINIMIZE);
            break;
        case SC_MAXIMIZE:
            WndShow(w, SW_MAXIMIZE);
            break;
        case SC_RESTORE:
            WndShow(w, SW_RESTORE);
            break;
        }
        return 0;

    case WM_CLOSE:
        WndDestroy(w);
        return 0;

    case WM_SETCURSOR: {
        LONG hit = (SHORT)LOWORD(lparam);
        if (HIWORD(lparam) && hit >= HTLEFT && hit <= HTBOTTOMRIGHT) {
            HostSetCursor(MenuCommandEnabled(w, SC_SIZE) ? system_cursor_for_hit(hit) : MIME_CURSOR_ARROW);
            return TRUE;
        }
        if ((w->Style & WS_CHILD) && w->Parent && WndIsChildOf(w, WndDesktop()) && w->Parent->Thread == w->Thread) {
            if (MsgSend(w->Parent, WM_SETCURSOR, wparam, lparam))
                return TRUE;
        }
        if (!HIWORD(lparam)) {
            HostSetCursor(MIME_CURSOR_ARROW);
            return FALSE;
        }
        if (hit == HTCLIENT) {
            HCURSOR c = w->Class ? w->Class->Cursor : nullptr;
            if (c)
                CursorSet(c);
            return FALSE;
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
            WND *focus = t ? WndLookup(t->Focus) : nullptr;
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

    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS pos;
        if (!read_lparam(&pos, lparam, sizeof(pos), user) || (pos.flags & SWP_NOSIZE) ||
            (w->Style & WS_MINIMIZE) || (w->Flags & WNDF_FULLSCREEN) ||
            !(w->Style & WS_THICKFRAME))
            return 0;
        MINMAXINFO limits;
        WndGetMinMaxInfo(w, &limits);
        pos.cx = W32Max(limits.ptMinTrackSize.x, W32Min(limits.ptMaxTrackSize.x, pos.cx));
        pos.cy = W32Max(limits.ptMinTrackSize.y, W32Min(limits.ptMaxTrackSize.y, pos.cy));
        write_lparam(lparam, &pos, sizeof(pos), user);
        return 0;
    }

    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS pos;
        if (!read_lparam(&pos, lparam, sizeof(pos), user))
            return 0;
        if (!(pos.flags & SWP_NOCLIENTMOVE)) {
            RECT c = w->ClientRect;
            if (w->Parent && w->Parent != WndDesktop())
                RectOffset(&c, -w->Parent->ClientRect.left, -w->Parent->ClientRect.top);
            MsgSend(w, WM_MOVE, 0, MAKELPARAM(c.left, c.top));
        }
        if (!(pos.flags & SWP_NOCLIENTSIZE) || (pos.flags & SWP_STATECHANGED)) {
            WPARAM kind = (w->Style & WS_MINIMIZE) ? SIZE_MINIMIZED : (w->Style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : SIZE_RESTORED;
            MsgSend(w, WM_SIZE, kind, kind == SIZE_MINIMIZED ? 0 :
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
        /* the size is unsigned, so a negative count of GetWindowText takes the whole text. */
        ULONG n = (ULONG)wparam - 1 < w->TextLength ? (ULONG)wparam - 1 : w->TextLength;
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
        WndUpdateHostConfig(w, FALSE);
        return (LRESULT)old;
    }

    /* deliver committed input method text as characters when the window did not handle it. */
    case WM_IME_COMPOSITION:
        if (lparam & 0x0800) /* GCS_RESULTSTR */
            return ImeDefaultCompositionChars(w);
        return 0;

    case WM_IME_CHAR:
        if (w->Thread)
            QueuePost(w->Thread, WndHandle(w), WM_CHAR, wparam, lparam);
        return 0;

    case WM_QUERYENDSESSION:
    case WM_QUERYOPEN:
        return TRUE;

    case MX_WM_CLIENTSHUTDOWN:
        return ClientShutdown(w, (ULONG)wparam);

    case WM_CANCELMODE:
        if (w->Thread && w->Thread->Capture == WndHandle(w))
            InputSetCapture(w->Thread, nullptr, 0);
        return 0;

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
