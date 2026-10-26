/*
 * window hooks. thread and process hooks run in the hooked thread, low level hooks in the thread that installed
 * them: like windows, the input thread sends it each event as a message, waits for the verdict and skips a hook
 * that does not answer in time.
 */
#include "win32k.h"

#define HOOK_CHAINS (WH_MAX - WH_MIN + 1)
#define LOW_LEVEL_TIMEOUT_MS 1000
#define ALIGN16(x) (((x) + 15) & ~(SIZE_T)15)

static LIST_ENTRY g_chains[HOOK_CHAINS];
static BOOLEAN g_chains_ready;

static PLIST_ENTRY chain(int id)
{
    if (!g_chains_ready) {
        for (int i = 0; i < HOOK_CHAINS; i++)
            InitializeListHead(&g_chains[i]);
        g_chains_ready = TRUE;
    }
    return &g_chains[id - WH_MIN];
}

static BOOLEAN low_level(int id)
{
    return id == WH_KEYBOARD_LL || id == WH_MOUSE_LL;
}

/* other hooks only run in their own process, the only address space that holds their code. */
static BOOLEAN applies(const HOOK *h, const W32THREAD *t)
{
    if (h->Header.Destroyed || !h->Owner || h->Owner->Destroying)
        return FALSE;
    if (low_level(h->Id))
        return TRUE;
    if (!t || t->Process != h->Process)
        return FALSE;
    return !h->Target || h->Target == t;
}

static HOOK *next_hook(int id, PLIST_ENTRY from, const W32THREAD *t)
{
    PLIST_ENTRY head = chain(id);
    for (PLIST_ENTRY e = from; e != head; e = e->Flink) {
        HOOK *h = CONTAINING_RECORD(e, HOOK, Links);
        if (applies(h, t))
            return h;
    }
    return nullptr;
}

HOOK *HookLookup(HHOOK handle)
{
    return (HOOK *)UserLookup(handle, USER_TYPE_HOOK);
}

BOOLEAN HookActive(int id, W32THREAD *t)
{
    return id >= WH_MIN && id <= WH_MAX && next_hook(id, chain(id)->Flink, t);
}

static ULONG64 now_ms(void)
{
    return KeQueryInterruptTime() / 10000;
}

LRESULT HookInvoke(HOOK *h, int code, WPARAM wparam, LPARAM lparam, ULONG size)
{
    W32THREAD *t = W32GetThread();
    if (!t || t->Destroying || h->Header.Destroyed)
        return 0;
    SIZE_T total = sizeof(MIME_HOOKPROC_ARGS) + ALIGN16(size);
    UCHAR local[sizeof(MIME_HOOKPROC_ARGS) + 128];
    MIME_HOOKPROC_ARGS *args = total <= sizeof(local) ? (MIME_HOOKPROC_ARGS *)local : W32Alloc(total);
    if (!args)
        return 0;
    memset(args, 0, sizeof(*args));
    args->Proc = h->Proc;
    args->Id = h->Id;
    args->Code = code;
    args->Wparam = wparam;
    args->Lparam = size ? 0 : lparam;
    args->Ansi = h->Ansi;
    args->DataSize = size;
    if (size)
        memcpy(args->Data, (PVOID)lparam, size);

    HOOK *prev = t->CurrentHook;
    t->CurrentHook = h;
    t->CallbackDepth++;
    UserReference(&h->Header);
    ULONG depth = UserLeaveAll();
    PVOID out = nullptr;
    ULONG out_len = 0;
    NTSTATUS status = KeUserModeCallback(MIME_CB_HOOKPROC, args, (ULONG)total, &out, &out_len);
    UserReenter(depth);
    t->CallbackDepth--;
    t->CurrentHook = prev;

    LRESULT result = 0;
    if (NT_SUCCESS(status) && out && out_len >= sizeof(MIME_CALLBACK_RESULT)) {
        MIME_CALLBACK_RESULT hdr;
        if (NT_SUCCESS(MimeCopyFromUser(&hdr, out, sizeof(hdr)))) {
            result = hdr.Result;
            ULONG n = hdr.DataSize < size ? hdr.DataSize : size;
            if (n && out_len >= sizeof(hdr) + n)
                MimeCopyFromUser((PVOID)lparam, (PUCHAR)out + sizeof(hdr), n);
        }
    } else if (!NT_SUCCESS(status)) {
        DbgPrint("win32k: hook %d callback failed: %08x\n", h->Id, status);
    }
    UserDereference(&h->Header);
    if ((PUCHAR)args != local)
        W32Free(args);
    return result;
}

/* MsgProcessSent serves it in the owner thread. */
static LRESULT send_hook(HOOK *h, int code, WPARAM wparam, LPARAM lparam, ULONG size)
{
    W32THREAD *owner = h->Owner;
    W32THREAD *self = PsGetCurrentThreadWin32Thread();
    SENT_MESSAGE *sm = W32Alloc(sizeof(SENT_MESSAGE));
    PVOID copy = size ? W32Alloc(size) : nullptr;
    if (!sm || (size && !copy)) {
        W32Free(sm);
        W32Free(copy);
        return 0;
    }
    if (size)
        memcpy(copy, (PVOID)lparam, size);
    sm->Sender = self;
    sm->Hook = h->Header.Handle;
    sm->Msg = (UINT)code;
    sm->Wparam = wparam;
    sm->Lparam = size ? (LPARAM)copy : lparam;
    sm->LparamKind = size ? MIME_LPARAM_INOUT : MIME_LPARAM_VALUE;
    sm->LparamSize = size;
    sm->Owned = size != 0;
    KeInitializeEvent(&sm->Done, NotificationEvent, FALSE);
    InsertTailList(&owner->Sent, &sm->Links);
    QueueWake(owner, QS_SENDMESSAGE);

    ULONG64 deadline = now_ms() + LOW_LEVEL_TIMEOUT_MS;
    while (!sm->Completed) {
        if (self && !IsListEmpty(&self->Sent)) {
            MsgProcessSent(self);
            continue;
        }
        LONG64 left = (LONG64)(deadline - now_ms());
        if (left <= 0)
            break;
        LARGE_INTEGER timeout = {.QuadPart = -left * 10000};
        PVOID objects[2] = {&sm->Done, self ? (PVOID)&self->Event : (PVOID)&sm->Done};
        ULONG depth = UserLeaveAll();
        NTSTATUS status = KeWaitForMultipleObjects(self ? 2 : 1, objects, WaitAny, UserRequest,
                                                   self ? UserMode : KernelMode, FALSE, &timeout, nullptr);
        UserReenter(depth);
        if (status == STATUS_THREAD_IS_TERMINATING)
            break;
    }
    if (!sm->Completed) {
        /* the owner will answer late into a call nobody waits for, and free it. */
        sm->Sender = nullptr;
        sm->Notify = TRUE;
        return 0;
    }
    LRESULT r = sm->Result;
    if (size) {
        memcpy((PVOID)lparam, copy, size);
        W32Free(copy);
    }
    W32Free(sm);
    return r;
}

static LRESULT call_hook(HOOK *h, int code, WPARAM wparam, LPARAM lparam, ULONG size)
{
    if (low_level(h->Id) && h->Owner != PsGetCurrentThreadWin32Thread())
        return send_hook(h, code, wparam, lparam, size);
    return HookInvoke(h, code, wparam, lparam, size);
}

LRESULT HookCall(int id, W32THREAD *t, int code, WPARAM wparam, LPARAM lparam, ULONG size, BOOLEAN *called)
{
    HOOK *h = id >= WH_MIN && id <= WH_MAX ? next_hook(id, chain(id)->Flink, t) : nullptr;
    if (called)
        *called = h != nullptr;
    return h ? call_hook(h, code, wparam, lparam, size) : 0;
}

/* WH_CBT runs in the thread acting on its own window, a nonzero answer prevents the action. */
BOOLEAN HookCbt(WND *w, int code, WPARAM wparam, LPARAM lparam, ULONG size)
{
    W32THREAD *t = W32GetThread();
    if (!t || (w && w->Thread != t) || !HookActive(WH_CBT, t))
        return FALSE;
    return HookCall(WH_CBT, t, code, wparam, lparam, size, nullptr) != 0;
}

static void destroy_hook(HOOK *h)
{
    h->Header.Destroyed = TRUE;
    RemoveEntryList(&h->Links);
    InitializeListHead(&h->Links);
    UserDereference(&h->Header);
}

void HookRemoveThread(W32THREAD *t)
{
    for (int id = WH_MIN; id <= WH_MAX; id++) {
        PLIST_ENTRY head = chain(id);
        for (PLIST_ENTRY e = head->Flink; e != head;) {
            HOOK *h = CONTAINING_RECORD(e, HOOK, Links);
            e = e->Flink;
            if (h->Owner == t || h->Target == t)
                destroy_hook(h);
        }
    }
}

/* ------------------------------------------------------------------ services */

HHOOK NTAPI NtUserSetWindowsHookEx(HINSTANCE module, PUNICODE_STRING module_name, DWORD thread, int id,
                                   HOOKPROC proc, BOOL ansi)
{
    (void)module_name;
    if (id < WH_MIN || id > WH_MAX || id == WH_HARDWARE) {
        W32SetLastError(ERROR_INVALID_HOOK_FILTER);
        return nullptr;
    }
    if (!proc) {
        W32SetLastError(ERROR_INVALID_FILTER_PROC);
        return nullptr;
    }
    if (id == WH_JOURNALRECORD || id == WH_JOURNALPLAYBACK) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    HHOOK result = nullptr;
    UserEnter();
    W32THREAD *self = W32GetThread();
    W32THREAD *target = nullptr;
    if (!self)
        goto out;
    if (thread) {
        target = W32LookupThread((HANDLE)(ULONG_PTR)thread);
        if (!target) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            goto out;
        }
        if (low_level(id)) {
            W32SetLastError(ERROR_GLOBAL_ONLY_HOOK);
            goto out;
        }
    } else if (!low_level(id) && !module) {
        W32SetLastError(ERROR_HOOK_NEEDS_HMOD);
        goto out;
    }
    HOOK *h = W32Alloc(sizeof(HOOK));
    if (!h || !UserCreateHandle(&h->Header, USER_TYPE_HOOK)) {
        W32Free(h);
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto out;
    }
    h->Id = id;
    h->Proc = proc;
    h->Ansi = ansi != FALSE;
    h->Owner = self;
    h->Target = target;
    h->Process = self->Process;
    /* the newest hook is called first. */
    InsertHeadList(chain(id), &h->Links);
    result = (HHOOK)h->Header.Handle;
out:
    UserLeave();
    return result;
}

BOOL NTAPI NtUserUnhookWindowsHookEx(HHOOK hook)
{
    UserEnter();
    HOOK *h = HookLookup(hook);
    if (h)
        destroy_hook(h);
    else
        W32SetLastError(ERROR_INVALID_HOOK_HANDLE);
    UserLeave();
    return h != nullptr;
}

/* low level hook structures cross threads and processes by copy, other lparams are pointers in this process. */
static ULONG hook_data_size(int id)
{
    switch (id) {
    case WH_KEYBOARD_LL: return sizeof(KBDLLHOOKSTRUCT);
    case WH_MOUSE_LL: return sizeof(MSLLHOOKSTRUCT);
    }
    return 0;
}

LRESULT NTAPI NtUserCallNextHookEx(int code, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    (void)ansi;
    LRESULT r = 0;
    UserEnter();
    W32THREAD *t = W32GetThread();
    HOOK *cur = t ? t->CurrentHook : nullptr;
    HOOK *next = cur && !cur->Header.Destroyed ? next_hook(cur->Id, cur->Links.Flink, t) : nullptr;
    if (next) {
        ULONG size = hook_data_size(cur->Id);
        UCHAR data[32];
        if (size && !NT_SUCCESS(MimeCopyFromUser(data, (PVOID)lparam, size))) {
            W32SetLastError(ERROR_NOACCESS);
        } else {
            r = call_hook(next, code, wparam, size ? (LPARAM)data : lparam, size);
            if (size)
                MimeCopyToUser((PVOID)lparam, data, size);
        }
    }
    UserLeave();
    return r;
}
