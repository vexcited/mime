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

void UserEnter(void)
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

void UserLeave(void)
{
    if (g_lock_owner != KeGetCurrentThread())
        KeBugCheckEx(0x000000E3, 0x55534552, 0, 0, 0); /* RESOURCE_NOT_OWNED */
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
    for (ULONG i = 0; i < MAX_ATOMS; i++) {
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
    /* every process is treated as system dpi aware until dpi virtualization exists. */
    p->DpiAwareness = 1;
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
    return t;
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

/* the threads of a process that are alive, in the order they were made. */
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
        W32GetThread();
        UserLeave();
        return STATUS_SUCCESS;
    }
    W32THREAD *t = PsGetThreadWin32Thread(thread);
    if (!t)
        return STATUS_SUCCESS;
    UserEnter();
    t->Destroying = TRUE;
    WndDestroyThreadWindows(t);
    ClipboardThreadDestroyed(t);
    DndThreadDestroyed(t);
    TimerRemoveThread(t);
    HookRemoveThread(t);
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
    if (InputForeground && InputForeground->Thread == t)
        InputForeground = nullptr;
    WinStaFreeThread(t);
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
        UserLeave();
        return STATUS_SUCCESS;
    }
    W32PROCESS *p = PsGetProcessWin32Process(process);
    UserEnter();
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
        MenuFreeProcess(p);
        ClassFreeProcess(p);
        WinStaFreeProcess(p);
        RemoveEntryList(&p->Links);
        PsSetProcessWin32Process(process, nullptr, p);
    }
    UserLeave();
    W32Free(p);
    return STATUS_SUCCESS;
}


/* ------------------------------------------------------------------ client registration */

BOOL NTAPI NtUserInitializeClientPfnArrays(const MIME_CLIENT_INFO *user_info)
{
    MIME_CLIENT_INFO info;
    if (!NT_SUCCESS(MimeCopyFromUser(&info, user_info, sizeof(info))) || info.Size != sizeof(info))
        return FALSE;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    if (p) {
        p->Client = info;
        p->ClientValid = TRUE;
        p->Wow = (info.Flags & MIME_CLIENT_WOW) != 0;
    }
    UserLeave();
    return p != nullptr;
}

WNDPROC W32ClientProc(W32PROCESS *p, ULONG fnid)
{
    if (!p || !p->ClientValid || fnid >= MIME_FNID_COUNT)
        return nullptr;
    return p->Client.Procs[fnid];
}

ULONG_PTR NTAPI NtUserGetThreadState(ULONG routine)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    ULONG_PTR v = 0;
    if (t) {
        switch (routine) {
        case 0: /* focus */
            v = (ULONG_PTR)t->Focus;
            break;
        case 1: /* active */
            v = (ULONG_PTR)t->Active;
            break;
        case 2: /* capture */
            v = (ULONG_PTR)t->Capture;
            break;
        case 14: /* thread has a queue */
            v = 1;
            break;
        }
    }
    UserLeave();
    return v;
}

/* ------------------------------------------------------------------ driver entry */

ULONG Win32kServiceCount = 2048;

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    KeInitializeEvent(&g_lock_event, SynchronizationEvent, FALSE);
    Win32kInitializeServices(g_services);
    if (!KeAddSystemServiceTable(g_services, nullptr, Win32kServiceCount, nullptr, 1)) {
        DbgPrint("win32k: cannot register the system service table\n");
        return STATUS_UNSUCCESSFUL;
    }
    static WIN32_CALLOUTS callouts = {process_callout, thread_callout, W32ShutdownProcess, W32TimeChange};
    PsEstablishWin32Callouts(&callouts);

    UserEnter();
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
