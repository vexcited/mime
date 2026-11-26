/*
 * win32k entry point, the user lock, per process and per thread state,
 * atoms and small helpers.
 */
#include "win32k.h"

#define W32K_TAG W32_TAG('W', '3', '2', 'k')

void Win32kInitializeServices(PKSERVICE_DESCRIPTOR table);
extern ULONG Win32kServiceCount;

static KSERVICE_DESCRIPTOR g_services[2048];

/* x64 code using floating point refers to it, as with msvc. */
#ifdef __x86_64__
int _fltused = 0x9875;
#endif
LIST_ENTRY W32Processes = {&W32Processes, &W32Processes};
static LIST_ENTRY g_threads = {&g_threads, &g_threads};

/* ------------------------------------------------------------------ memory */

void *W32Alloc(SIZE_T size)
{
    void *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, W32K_TAG);
    if (p)
        memset(p, 0, size);
    return p;
}

void W32Free(void *p)
{
    if (p)
        ExFreePool(p);
}

PWSTR W32StrDup(PCWSTR s, SIZE_T chars)
{
    PWSTR copy = W32Alloc((chars + 1) * sizeof(WCHAR));
    if (copy && chars)
        memcpy(copy, s, chars * sizeof(WCHAR));
    return copy;
}

/* rtl hooks used by the shared string code. */
PVOID MimeRtlAllocate(SIZE_T size)
{
    return W32Alloc(size);
}

void MimeRtlFree(PVOID p)
{
    W32Free(p);
}

NTSTATUS W32CaptureString(PUNICODE_STRING user, PWSTR *out, SIZE_T *chars)
{
    UNICODE_STRING s;
    *out = nullptr;
    if (chars)
        *chars = 0;
    if (!user)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    SIZE_T n = s.Length / sizeof(WCHAR);
    PWSTR copy = W32Alloc((n + 1) * sizeof(WCHAR));
    if (!copy)
        return STATUS_NO_MEMORY;
    if (n && !NT_SUCCESS(MimeCopyFromUser(copy, s.Buffer, n * sizeof(WCHAR)))) {
        W32Free(copy);
        return STATUS_ACCESS_VIOLATION;
    }
    *out = copy;
    if (chars)
        *chars = n;
    return STATUS_SUCCESS;
}

void W32SetLastError(ULONG error)
{
    PUCHAR teb = PsGetCurrentThreadTeb();
    if (teb)
        MimeCopyToUser(teb + FIELD_OFFSET(TEB, LastErrorValue), &error, sizeof(error));
}

DWORD W32TickCount(void)
{
    return (DWORD)(KeQueryInterruptTime() / 10000);
}

/* ------------------------------------------------------------------ the user lock */

static KEVENT g_lock_event;
static PKTHREAD volatile g_lock_owner;
static ULONG g_lock_depth;
static volatile LONG g_lock_waiters;

/* the lock for a call that changes nothing the desktop heap shows, so leaving it does not rewrite the heap. */
void UserEnterRead(void)
{
    PKTHREAD self = KeGetCurrentThread();
    if (g_lock_owner == self) {
        g_lock_depth++;
        return;
    }
    for (;;) {
        PKTHREAD expected = nullptr;
        if (__atomic_compare_exchange_n(&g_lock_owner, &expected, self, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            break;
        __atomic_add_fetch(&g_lock_waiters, 1, __ATOMIC_RELAXED);
        /* recheck after announcing ourselves, then sleep until a release. */
        expected = nullptr;
        if (__atomic_compare_exchange_n(&g_lock_owner, &expected, self, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_sub_fetch(&g_lock_waiters, 1, __ATOMIC_RELAXED);
            break;
        }
        KeWaitForSingleObject(&g_lock_event, Executive, KernelMode, FALSE, nullptr);
        __atomic_sub_fetch(&g_lock_waiters, 1, __ATOMIC_RELAXED);
    }
    g_lock_depth = 1;
}

void UserEnter(void)
{
    UserEnterRead();
    DeskTouch();
}

void UserLeave(void)
{
    if (g_lock_owner != KeGetCurrentThread())
        KeBugCheckEx(0x000000E3, 0x55534552, 0, 0, 0); /* RESOURCE_NOT_OWNED */
    if (g_lock_depth == 1) {
        DcAttrFlush();
        DeskSync();
    }
    if (--g_lock_depth)
        return;
    __atomic_store_n(&g_lock_owner, nullptr, __ATOMIC_RELEASE);
    if (__atomic_load_n(&g_lock_waiters, __ATOMIC_RELAXED))
        KeSetEvent(&g_lock_event, 0, FALSE);
}

ULONG UserLeaveAll(void)
{
    if (g_lock_owner != KeGetCurrentThread())
        return 0;
    ULONG depth = g_lock_depth;
    g_lock_depth = 1;
    UserLeave();
    return depth;
}

void UserReenter(ULONG depth)
{
    if (!depth)
        return;
    UserEnter();
    g_lock_depth = depth;
}

void UserReenterRead(ULONG depth)
{
    if (!depth)
        return;
    UserEnterRead();
    g_lock_depth = depth;
}

BOOLEAN UserIsOwner(void)
{
    return g_lock_owner == KeGetCurrentThread();
}

/* ------------------------------------------------------------------ atoms */

typedef struct ATOM_ENTRY {
    PWSTR Name;
    ULONG Refs;
} ATOM_ENTRY;

#define ATOM_BASE 0xC000
#define MAX_ATOMS 4096
static ATOM_ENTRY g_atoms[MAX_ATOMS];

/*
 * the atoms windows 11 has from its start, 0xc001 on, read with GetClipboardFormatName: user32 compares the atoms of
 * its classes with the ones the server information names, and gives some of them to properties.
 */
static const PCWSTR g_boot_atoms[] = {
    L"USER32", L"ObjectLink", L"OwnerLink", L"Native", L"Binary", L"FileName", L"FileNameW", L"NetworkName",
    L"DataObject", L"Embedded Object", L"Embed Source", L"Custom Link Source", L"Link Source", L"Object Descriptor",
    L"Link Source Descriptor", L"OleDraw", L"PBrush", L"MSDraw", L"Ole Private Data", L"Screen Picture",
    L"OleClipboardPersistOnFlush", L"MoreOlePrivateData", L"AdditionalFGBoostProp", L"Button", L"Edit",
    L"Static", L"ListBox", L"ScrollBar", L"ComboBox", L"MDIClient", L"ComboLBox", L"DDEMLEvent", L"DDEMLMom",
    L"DMGClass", L"DDEMLAnsiClient", L"DDEMLUnicodeClient", L"DDEMLAnsiServer", L"DDEMLUnicodeServer", L"IME",
    L"Ghost", L"SHELLHOOK", L"SysFrostedWindow", L"SysCP", L"SysDT", L"SysQOS", L"SysDDEI", L"SysWNDO", L"SysIMEL",
    L"SysLayer", L"SysDispAffinity", L"SysSwapChain", L"SysDCompHwndTargets", L"SysVisRgnTracker",
    L"SysDCompHwndBitmap", L"SysCompositionInputQueue", L"SysWndMargin", L"ACTIVATESHELLWINDOW",
    L"OTHERWINDOWCREATED", L"OTHERWINDOWDESTROYED", L"Message", L"SysShadow", L"OleMainThreadWndClass",
    L"FlashWState", L"SysTouchFlags", L"MicrosoftWindowsTargetingWindow", L"SysFeedbackSettings",
    L"ShutdownBlockingReason", L"BrokeredForeground", L"MiPFlags", L"CoreWindowProp",
};

static void atoms_initialize(void)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_boot_atoms); i++) {
        g_atoms[i + 1].Name = W32StrDup(g_boot_atoms[i], wcslen(g_boot_atoms[i]));
        g_atoms[i + 1].Refs = 1;
    }
}

ATOM AtomFind(PCWSTR name)
{
    for (ULONG i = 0; i < MAX_ATOMS; i++)
        if (g_atoms[i].Name && !_wcsicmp(g_atoms[i].Name, name))
            return (ATOM)(ATOM_BASE + i);
    return 0;
}

ATOM AtomAdd(PCWSTR name)
{
    ATOM a = AtomFind(name);
    if (a) {
        g_atoms[a - ATOM_BASE].Refs++;
        return a;
    }
    /* windows never gives 0xc000. */
    for (ULONG i = 1; i < MAX_ATOMS; i++) {
        if (!g_atoms[i].Name) {
            g_atoms[i].Name = W32StrDup(name, wcslen(name));
            if (!g_atoms[i].Name)
                return 0;
            g_atoms[i].Refs = 1;
            return (ATOM)(ATOM_BASE + i);
        }
    }
    return 0;
}

void AtomDelete(ATOM atom)
{
    if (atom < ATOM_BASE || atom >= ATOM_BASE + MAX_ATOMS || !g_atoms[atom - ATOM_BASE].Name)
        return;
    if (--g_atoms[atom - ATOM_BASE].Refs == 0) {
        W32Free(g_atoms[atom - ATOM_BASE].Name);
        g_atoms[atom - ATOM_BASE].Name = nullptr;
    }
}

ULONG AtomGetName(ATOM atom, PWSTR buffer, ULONG chars)
{
    if (atom < ATOM_BASE || atom >= ATOM_BASE + MAX_ATOMS || !g_atoms[atom - ATOM_BASE].Name || !chars)
        return 0;
    PCWSTR name = g_atoms[atom - ATOM_BASE].Name;
    ULONG n = (ULONG)wcslen(name);
    if (n >= chars)
        n = chars - 1;
    memcpy(buffer, name, n * sizeof(WCHAR));
    buffer[n] = 0;
    return n;
}

/*
 * the name of an atom of the window manager into the buffer of a UNICODE_STRING, the characters written are the
 * answer. an integer atom, below 0xc000, is named by its number after a #, as the classes #32770 and the like.
 */
ULONG NTAPI NtUserGetAtomName(ATOM atom, PUNICODE_STRING user_name)
{
    UNICODE_STRING s;
    if (!user_name || !NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s))) || s.MaximumLength < sizeof(WCHAR))
        return 0;
    WCHAR name[256];
    ULONG n = 0;
    if (atom && atom < ATOM_BASE) {
        char digits[8];
        int k = 0;
        for (ULONG v = atom; v; v /= 10)
            digits[k++] = (char)('0' + v % 10);
        name[n++] = L'#';
        while (k)
            name[n++] = (WCHAR)digits[--k];
        name[n] = 0;
    } else {
        UserEnter();
        n = AtomGetName(atom, name, RTL_NUMBER_OF(name));
        UserLeave();
    }
    if (!n) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    ULONG room = s.MaximumLength / sizeof(WCHAR) - 1;
    if (n > room)
        n = room;
    name[n] = 0;
    if (!NT_SUCCESS(MimeCopyToUser(s.Buffer, name, (n + 1) * sizeof(WCHAR))))
        return 0;
    s.Length = (USHORT)(n * sizeof(WCHAR));
    MimeCopyToUser(&user_name->Length, &s.Length, sizeof(s.Length));
    return n;
}

UINT NTAPI NtUserRegisterWindowMessage(PUNICODE_STRING user_name)
{
    PWSTR name;
    if (!NT_SUCCESS(W32CaptureString(user_name, &name, nullptr)) || !name)
        return 0;
    UserEnter();
    ATOM a = *name ? AtomAdd(name) : 0;
    UserLeave();
    W32Free(name);
    return a;
}

/* ------------------------------------------------------------------ processes and threads */

W32PROCESS *W32GetProcess(void)
{
    PEPROCESS process = PsGetCurrentProcess();
    W32PROCESS *p = PsGetProcessWin32Process(process);
    if (p)
        return p;
    p = W32Alloc(sizeof(W32PROCESS));
    if (!p)
        return nullptr;
    p->Process = process;
    p->Pid = PsGetCurrentProcessId();
    PPEB peb = PsGetProcessPeb(process);
    PRTL_USER_PROCESS_PARAMETERS params = nullptr;
    UNICODE_STRING image;
    if (peb && NT_SUCCESS(MimeCopyFromUser(&params, &peb->ProcessParameters, sizeof(params))) && params &&
        NT_SUCCESS(MimeCopyFromUser(&image, &params->ImagePathName, sizeof(image)))) {
        ULONG chars = image.Length / sizeof(WCHAR);
        if (chars >= RTL_NUMBER_OF(p->ImagePath))
            chars = RTL_NUMBER_OF(p->ImagePath) - 1;
        if (NT_SUCCESS(MimeCopyFromUser(p->ImagePath, image.Buffer, chars * sizeof(WCHAR))))
            p->ImageChars = chars;
        UNICODE_STRING desktop;
        if (NT_SUCCESS(MimeCopyFromUser(&desktop, &params->DesktopInfo, sizeof(desktop)))) {
            chars = desktop.Length / sizeof(WCHAR);
            if (chars >= RTL_NUMBER_OF(p->DesktopInfo))
                chars = 0;
            if (chars && !NT_SUCCESS(MimeCopyFromUser(p->DesktopInfo, desktop.Buffer, chars * sizeof(WCHAR))))
                chars = 0;
            p->DesktopInfo[chars] = 0;
        }
    }
    InitializeListHead(&p->Classes);
    p->Wow = PsGetProcessWow64Process(process) != nullptr;
    /* a program is dpi unaware until its manifest or a call says otherwise. */
    p->DpiAwareness = DPI_AWARE_UNAWARE;
    InsertTailList(&W32Processes, &p->Links);
    PsSetProcessWin32Process(process, p, nullptr);
    return p;
}

W32THREAD *W32GetThread(void)
{
    W32THREAD *t = PsGetCurrentThreadWin32Thread();
    if (t)
        return t;
    W32PROCESS *p = W32GetProcess();
    if (!p)
        return nullptr;
    t = W32Alloc(sizeof(W32THREAD));
    if (!t)
        return nullptr;
    t->Thread = PsGetCurrentThread();
    t->Tid = PsGetCurrentThreadId();
    t->Process = p;
    KeInitializeEvent(&t->Event, SynchronizationEvent, FALSE);
    InitializeListHead(&t->Posted);
    InitializeListHead(&t->Input);
    InitializeListHead(&t->Sent);
    t->CursorShowCount = 0;
    t->LastRead = W32TickCount();
    InsertTailList(&g_threads, &t->Links);
    PsSetThreadWin32Thread(PsGetCurrentThread(), t, nullptr);
    DeskAttachThread(p);
    return t;
}

/* the threads of the window manager one after another, from null. the lock is held. */
W32THREAD *W32NextThread(W32THREAD *previous)
{
    PLIST_ENTRY e = previous ? previous->Links.Flink : g_threads.Flink;
    return e == &g_threads ? nullptr : CONTAINING_RECORD(e, W32THREAD, Links);
}

W32THREAD *W32LookupThread(HANDLE tid)
{
    for (PLIST_ENTRY e = g_threads.Flink; e != &g_threads; e = e->Flink) {
        W32THREAD *t = CONTAINING_RECORD(e, W32THREAD, Links);
        if (t->Tid == tid && !t->Destroying)
            return t;
    }
    return nullptr;
}

/* threads attached by AttachThreadInput, so that lookups of shared state skip the walk while there are none. */
static ULONG g_attached;

/* gives every thread that reads the state of from the state of to instead. */
static void move_sharers(W32THREAD *from, W32THREAD *to)
{
    InputKeysChanged();
    for (PLIST_ENTRY e = g_threads.Flink; e != &g_threads; e = e->Flink) {
        W32THREAD *x = CONTAINING_RECORD(e, W32THREAD, Links);
        if (x != to && (x == from || x->InputShare == from)) {
            if (!x->InputShare)
                g_attached++;
            x->InputShare = to;
        }
        if (x == to && x->InputShare) {
            x->InputShare = nullptr;
            g_attached--;
        }
    }
}

static void detach(W32THREAD *t)
{
    memcpy(t->KeyState, t->InputShare->KeyState, sizeof(t->KeyState));
    t->InputShare = nullptr;
    g_attached--;
    InputKeysChanged();
}

/*
 * attached threads share one input state, as windows shares their queue: the key state lives in the thread the group is
 * attached to, and the focus, active window and capture read as the group's.
 */
BOOL W32AttachInput(W32THREAD *a, W32THREAD *b, BOOL attach)
{
    W32THREAD *ra = W32InputOwner(a), *rb = W32InputOwner(b);
    if (attach) {
        if (ra != rb)
            move_sharers(ra, rb);
        return TRUE;
    }
    if (ra != rb)
        return FALSE;
    detach(a->InputShare ? a : b);
    return TRUE;
}

/* a window handle field of the thread, or of the first thread of its attached group that has one. */
HWND W32SharedWindow(W32THREAD *t, SIZE_T offset)
{
    HWND own = *(HWND *)((char *)t + offset);
    if (own || !g_attached)
        return own;
    W32THREAD *owner = W32InputOwner(t);
    for (PLIST_ENTRY e = g_threads.Flink; e != &g_threads; e = e->Flink) {
        W32THREAD *x = CONTAINING_RECORD(e, W32THREAD, Links);
        HWND h = *(HWND *)((char *)x + offset);
        if (h && !x->Destroying && W32InputOwner(x) == owner)
            return h;
    }
    return nullptr;
}

/* an ending thread leaves its group: its sharers take the first of them as the holder of their state. */
static void leave_group(W32THREAD *t)
{
    InputKeysChanged();
    if (t->InputShare) {
        t->InputShare = nullptr;
        g_attached--;
        return;
    }
    W32THREAD *heir = nullptr;
    for (PLIST_ENTRY e = g_threads.Flink; e != &g_threads && !heir; e = e->Flink) {
        W32THREAD *x = CONTAINING_RECORD(e, W32THREAD, Links);
        if (x->InputShare == t)
            heir = x;
    }
    if (!heir)
        return;
    memcpy(heir->KeyState, t->KeyState, sizeof(t->KeyState));
    move_sharers(t, heir);
    /* move_sharers counted t as a sharer it is not. */
    t->InputShare = nullptr;
    g_attached--;
}

/* the live threads of a process, in creation order. */
ULONG W32ProcessThreads(W32PROCESS *p, HANDLE *tids, ULONG max)
{
    ULONG n = 0;
    for (PLIST_ENTRY e = g_threads.Flink; e != &g_threads && n < max; e = e->Flink) {
        W32THREAD *t = CONTAINING_RECORD(e, W32THREAD, Links);
        if (t->Process == p && !t->Destroying)
            tids[n++] = t->Tid;
    }
    return n;
}

static void free_message_list(PLIST_ENTRY list)
{
    while (!IsListEmpty(list))
        W32Free(CONTAINING_RECORD(RemoveHeadList(list), QUEUED_MESSAGE, Links));
}

static NTSTATUS NTAPI thread_callout(PETHREAD thread, BOOLEAN initialize)
{
    if (initialize) {
        UserEnter();
        W32THREAD *made = W32GetThread();
        UserLeave();
        if (made && !made->ClientSetup) {
            made->ClientSetup = TRUE;
            ClientThreadSetup();
        }
        return STATUS_SUCCESS;
    }
    W32THREAD *t = PsGetThreadWin32Thread(thread);
    if (!t)
        return STATUS_SUCCESS;
    if (thread == PsGetCurrentThread())
        GdiThreadExitBatch();
    UserEnter();
    t->Destroying = TRUE;
    WndDestroyThreadWindows(t);
    ClipboardThreadDestroyed(t);
    DndThreadDestroyed(t);
    CaretDestroyThread(t);
    TimerRemoveThread(t);
    HotkeyRemoveThread(t);
    HookRemoveThread(t);
    WndPosThreadGone(t);
    leave_group(t);
    free_message_list(&t->Posted);
    free_message_list(&t->Input);
    /* a thread that ends inside a procedure called for a sent message never answers it. */
    for (SENT_MESSAGE *sm = t->Receiving; sm;) {
        SENT_MESSAGE *outer = sm->Outer;
        if (sm->Notify) {
            if (sm->Owned)
                W32Free((PVOID)sm->Lparam);
            W32Free(sm);
        } else if (!sm->Completed) {
            sm->Completed = TRUE;
            sm->Aborted = TRUE;
            sm->Result = 0;
            KeSetEvent(&sm->Done, 0, FALSE);
        }
        sm = outer;
    }
    t->Receiving = nullptr;
    /* release senders waiting on this thread. */
    while (!IsListEmpty(&t->Sent)) {
        SENT_MESSAGE *sm = CONTAINING_RECORD(RemoveHeadList(&t->Sent), SENT_MESSAGE, Links);
        if (sm->Notify) {
            if (sm->Owned)
                W32Free((PVOID)sm->Lparam);
            W32Free(sm);
            continue;
        }
        sm->Completed = TRUE;
        sm->Aborted = TRUE;
        sm->Result = 0;
        KeSetEvent(&sm->Done, 0, FALSE);
    }
    if (InputForeground && InputForeground->Thread == t) {
        InputForeground = nullptr;
        UserSharedForeground(nullptr);
    }
    InputKeysChanged();
    HostReleaseCursor(thread);
    CursorFreeThread(t);
    /* otherwise a thread that hid the pointer leaves it hidden for the next program. */
    if (InputCursorHidden && (t->CursorShowCount < 0 || !t->Cursor))
        HostSetCursor(MIME_CURSOR_ARROW);
    WinStaFreeThread(t);
    DeskThreadFreed(t);
    RemoveEntryList(&t->Links);
    PsSetThreadWin32Thread(thread, nullptr, t);
    UserLeave();
    W32Free(t);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI process_callout(PEPROCESS process, BOOLEAN initialize)
{
    if (initialize) {
        UserEnter();
        W32GetProcess();
        WndLaunchPolicyDone(process, FALSE);
        UserLeave();
        return STATUS_SUCCESS;
    }
    W32PROCESS *p = PsGetProcessWin32Process(process);
    UserEnter();
    WndLaunchPolicyDone(process, TRUE);
    GdiReleaseProcessObjects(PsGetProcessId(process));
    FontReleaseProcess(PsGetProcessId(process));
    DisplayProcessExited(PsGetProcessId(process));
    if (p) {
        GpuFreeProcess(p);
        VkFreeProcess(p);
        CaptureFreeProcess(p);
        AudioFreeProcess(p);
        GfxShareFreeProcess(p);
        RawFreeProcess(p);
        ShellFreeProcess(p);
        TrayFreeProcess(p);
        MenuFreeProcess(p);
        ClassFreeProcess(p);
        CursorFreeProcess(p);
        DeskProcessExited(p);
        WinStaFreeProcess(p);
        RemoveEntryList(&p->Links);
        PsSetProcessWin32Process(process, nullptr, p);
    }
    UserLeave();
    W32Free(p);
    return STATUS_SUCCESS;
}


/* ------------------------------------------------------------------ client registration */

/*
 * ClientThreadSetup of user32 calls it in a server process only, with the tables RtlRetrieveNtUserPfn gives and
 * the module of user32. win32k takes the procedures from the tables of ntdll instead, which are the same in every
 * process, see ClientPfn.
 */
NTSTATUS NTAPI NtUserInitializeClientPfnArrays(PVOID ansi, PVOID unicode, PVOID workers, HINSTANCE user32)
{
    (void)ansi, (void)unicode, (void)workers, (void)user32;
    return STATUS_SUCCESS;
}

/*
 * ClientThreadSetup asks it once per thread and only tells 1, a session without its display, from the others. the
 * console session of the reference machine answers 3.
 */
ULONG NTAPI NtUserRemoteConnectState(void)
{
    return 3;
}

/* the process uses the library of complex scripts, which win32k keeps for its text calls. */
BOOL NTAPI NtUserRegisterLPK(DWORD flags)
{
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    if (p)
        p->LpkFlags = flags;
    UserLeave();
    return TRUE;
}

/*
 * the kind of user interface a process has, PROCESS_UICONTEXT_DESKTOP for every program of mime, and its flags.
 * the reference machine answers 1 and two zeros for a program of its desktop.
 */
BOOL NTAPI NtUserGetProcessUIContextInformation(HANDLE process, PULONG info)
{
    if (process != (HANDLE)-1) {
        PEPROCESS object;
        if (!NT_SUCCESS(ObReferenceObjectByHandle(process, 0x1000, PsProcessType, UserMode, (PVOID *)&object, nullptr))) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        ObDereferenceObject(object);
    }
    ULONG none[2] = {0, 0};
    return NT_SUCCESS(MimeCopyToUser(info, none, sizeof(none)));
}

/* the flags of the queue of the thread user32 sets, as QF_DIALOGACTIVE around a dialog loop: those of mask take
 * the values of flags, and the old ones come back. */
DWORD NTAPI NtUserSetThreadState(DWORD flags, DWORD mask)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    DWORD old = t ? t->StateFlags : 0;
    if (t)
        t->StateFlags = (old & ~mask) | (flags & mask);
    UserLeave();
    return old;
}

ULONG_PTR NTAPI NtUserGetThreadState(ULONG routine)
{
    UserEnter();
    /* routine 15 asks whether the thread has a queue yet, without giving it one. */
    W32THREAD *t = routine == 15 ? PsGetCurrentThreadWin32Thread() : W32GetThread();
    ULONG_PTR v = 0;
    if (t) {
        switch (routine) {
        case 0: /* focus */
            v = (ULONG_PTR)W32SharedWindow(t, offsetof(W32THREAD, Focus));
            break;
        case 1: /* active */
            v = (ULONG_PTR)W32SharedWindow(t, offsetof(W32THREAD, Active));
            break;
        case 2: /* capture */
            v = (ULONG_PTR)W32SharedWindow(t, offsetof(W32THREAD, Capture));
            break;
        case 14: /* thread has a queue */
        case 15:
            v = 1;
            break;
        }
    }
    UserLeave();
    return v;
}

/* ------------------------------------------------------------------ waiting for a process to be idle */

#define WIDLE_TIMEOUT 0x102u
#define WIDLE_FAILED 0xFFFFFFFFu
#define WIDLE_POLL_MS 10

/* a thread that sleeps in a message wait with nothing to read has nothing to do. */
static BOOLEAN thread_idle(W32THREAD *t)
{
    return t->InWait && IsListEmpty(&t->Input) && IsListEmpty(&t->Posted) && !t->QuitPosted;
}

/* the subsystem from the process image, 0 when it cannot be read. */
static ULONG image_subsystem(HANDLE pid)
{
    PEPROCESS process;
    if (!NT_SUCCESS(PsLookupProcessByProcessId(pid, &process)))
        return 0;
    ULONG subsystem = 0;
    PPEB peb = PsGetProcessPeb(process);
    if (peb) {
        PKPROCESS state;
        KeStackAttachProcess((PRKPROCESS)process, &state);
        MimeCopyFromUser(&subsystem, &peb->ImageSubsystem, sizeof(subsystem));
        KeUnstackDetachProcess(&state);
    }
    ObDereferenceObject(process);
    return subsystem;
}

/*
 * WaitForInputIdle: waits until a thread of the process waits for input with nothing to read, which only happens
 * once. a process outside the graphical subsystem, or without a message queue, has nothing to wait for, but a
 * graphical one that has not made its queue yet is still waited for, and fails the wait if it ends.
 */
ULONG NTAPI NtUserWaitForInputIdle(ULONG_PTR process_id, DWORD timeout, BOOL remote_handle)
{
    HANDLE pid = (HANDLE)process_id;
    LONG64 deadline = timeout == 0xFFFFFFFFu ? -1 : (LONG64)W32TickCount() + timeout;
    PEPROCESS target = nullptr;
    if (!NT_SUCCESS(PsLookupProcessByProcessId(pid, &target))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return WIDLE_FAILED;
    }
    /* a console program does not wait, even once it has loaded user32 and has a queue. */
    BOOLEAN graphical = image_subsystem(pid) == 2; /* IMAGE_SUBSYSTEM_WINDOWS_GUI */
    ULONG result = 0;
    for (;;) {
        if (PsGetProcessExitStatus(target) != STATUS_PENDING) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            result = WIDLE_FAILED;
            break;
        }
        if (!graphical)
            break;
        UserEnter();
        W32PROCESS *wp = nullptr;
        for (PLIST_ENTRY e = W32Processes.Flink; e != &W32Processes; e = e->Flink) {
            W32PROCESS *candidate = CONTAINING_RECORD(e, W32PROCESS, Links);
            if (candidate->Pid == pid) {
                wp = candidate;
                break;
            }
        }
        BOOLEAN idle = FALSE;
        if (wp) {
            idle = wp->InputIdle;
            for (PLIST_ENTRY e = g_threads.Flink; e != &g_threads && !idle; e = e->Flink) {
                W32THREAD *t = CONTAINING_RECORD(e, W32THREAD, Links);
                if (t->Process == wp && !t->Destroying && thread_idle(t))
                    idle = TRUE;
            }
            if (idle)
                wp->InputIdle = TRUE;
        }
        UserLeave();
        if (idle) {
            result = 0;
            break;
        }
        LARGE_INTEGER step;
        step.QuadPart = -(LONGLONG)WIDLE_POLL_MS * 10000;
        if (deadline >= 0) {
            LONG64 left = deadline - (LONG64)W32TickCount();
            if (left <= 0) {
                result = WIDLE_TIMEOUT;
                break;
            }
            if (left < WIDLE_POLL_MS)
                step.QuadPart = -left * 10000;
        }
        KeDelayExecutionThread(UserMode, FALSE, &step);
    }
    ObDereferenceObject(target);
    return result;
}

/* ------------------------------------------------------------------ driver entry */

ULONG Win32kServiceCount = 2048;

/*
 * runs before every service of a thread of the window manager. a thread that connected through csr is one before its
 * first service, and windows converts it at that service: user32 has set its table of callbacks by then, which it
 * has not when it connects.
 */
static void NTAPI service_enter(void)
{
    W32THREAD *t = PsGetCurrentThreadWin32Thread();
    if (t && !t->ClientSetup) {
        t->ClientSetup = TRUE;
        ClientThreadSetup();
    }
    GdiFlushUserBatch();
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    KeInitializeEvent(&g_lock_event, SynchronizationEvent, FALSE);
    ShellInitialize();
    Win32kInitializeServices(g_services);
    if (!KeAddSystemServiceTable(g_services, nullptr, Win32kServiceCount, nullptr, 1)) {
        DbgPrint("win32k: cannot register the system service table\n");
        return STATUS_UNSUCCESSFUL;
    }
    static WIN32_CALLOUTS callouts = {process_callout, thread_callout, W32ShutdownProcess, W32TimeChange, W32FastService,
                                      service_enter};
    PsEstablishWin32Callouts(&callouts);
    UserSharedInitialize();

    UserEnter();
    atoms_initialize();
    MetricsInitialize();
    GdiInitializeStockObjects();
    FontInitialize();
    ClassInitialize();
    CursorInitialize();
    UserLeave();
    InputInitialize();
    DbgPrint("win32k: window manager ready, display %dx%d at %u dpi\n", W32Display.Width, W32Display.Height,
             W32Display.Dpi);
    return STATUS_SUCCESS;
}
