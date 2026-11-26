/*
 * the wait chain traversal of advapi32 26300, walked the way it walks it: the thread asked about, what it waits on, the
 * thread that owns that, and on, to sixteen nodes, until a thread runs, belongs to another process or was met before,
 * which is a cycle. what a blocked thread waits on comes from the service it waits in, which ThreadLastSystemCall tells
 * with its first argument; the numbers of NtWaitForSingleObject and of the wait of a critical section are learnt when a
 * session first opens, from a thread made to wait in each. a waited mutant, thread or process names its owner, an
 * event may be the semaphore of a critical section, ntdll finds the owner of a critical section from the address it
 * waits on, and a thread that sends a message waits on the thread of the window. com, alpc, sockets and smb are not
 * followed, which advapi32 does.
 */
#include "advapi32_private.h"
#include <mime/win32/winuser.h>

#define WCT_ASYNC_OPEN_FLAG 0x1
#define WCT_OUT_OF_PROC_FLAG 0x1
#define WCT_OUT_OF_PROC_CS_FLAG 0x4
#define WCT_FLAGS 0xF
#define WCT_MAX_NODE_COUNT 16
#define THREAD_LAST_SYSTEM_CALL ((THREADINFOCLASS)21)
#define STATE_READY 1
#define STATE_RUNNING 2
#define STATE_WAITING 5
#define WAIT_USER_REQUEST 6
#define WAIT_ALERT_BY_THREAD_ID 37
/* RtlQueryProcessDebugInformation as x86 advapi32 asks it: the address or semaphore in at 0x50, the owner out at 0x54. */
#define DEBUG_BY_SEMAPHORE 0x400
#define DEBUG_BY_ADDRESS 0x80000800
#define DEBUG_LOCK_IN 0x50
#define DEBUG_OWNER_OUT 0x54
#define NO_SERVICE 0xFFFF

typedef enum {
    WctCriticalSectionType = 1,
    WctSendMessageType,
    WctMutexType,
    WctAlpcType,
    WctComType,
    WctThreadWaitType,
    WctProcessWaitType,
    WctThreadType,
    WctComActivationType,
    WctUnknownType,
    WctSocketIoType,
    WctSmbIoType,
} WCT_OBJECT_TYPE;

typedef enum {
    WctStatusNoAccess = 1,
    WctStatusRunning,
    WctStatusBlocked,
    WctStatusPidOnly,
    WctStatusPidOnlyRpcss,
    WctStatusOwned,
    WctStatusNotOwned,
    WctStatusAbandoned,
} WCT_OBJECT_STATUS;

typedef struct _WAITCHAIN_NODE_INFO {
    WCT_OBJECT_TYPE ObjectType;
    WCT_OBJECT_STATUS ObjectStatus;
    union {
        struct {
            WCHAR ObjectName[128];
            LARGE_INTEGER Timeout;
            BOOL Alertable;
        } LockObject;
        struct {
            DWORD ProcessId;
            DWORD ThreadId;
            DWORD WaitTime;
            DWORD ContextSwitches;
        } ThreadObject;
    };
} WAITCHAIN_NODE_INFO;
MIME_STATIC_ASSERT(sizeof(WAITCHAIN_NODE_INFO) == 280, "wait chain node");

typedef void(CALLBACK *PWAITCHAINCALLBACK)(HANDLE session, DWORD_PTR context, DWORD status, LPDWORD count,
                                           WAITCHAIN_NODE_INFO *nodes, LPBOOL cycle);
typedef HWND(WINAPI *SEND_RECEIVER)(DWORD tid);

typedef struct WCT_SESSION {
    struct WCT_SESSION *Next;
    DWORD Flags;
    PWAITCHAINCALLBACK Callback;
    LONG Refs;
    volatile LONG Closed;
    HANDLE Idle; /* set when the last query of an asynchronous session ends. */
} WCT_SESSION;

typedef struct WCT_QUERY {
    WCT_SESSION *Session;
    DWORD_PTR Context;
    DWORD Flags;
    DWORD Thread;
    LPDWORD Count;
    WAITCHAIN_NODE_INFO *Nodes;
    LPBOOL Cycle;
} WCT_QUERY;

/* what is kept open of a thread on the chain: the thread, its process and their ids. */
typedef struct CLIENT {
    HANDLE Thread, Process;
    DWORD Pid, Tid;
} CLIENT;

/* ThreadLastSystemCall in the layout of a 32-bit caller. */
typedef struct LAST_CALL {
    ULONG FirstArgument;
    USHORT SystemCallNumber;
    USHORT Pad;
    ULONG64 WaitTime;
} LAST_CALL;

static SRWLOCK g_lock = SRWLOCK_INIT;
static WCT_SESSION *g_sessions;
static USHORT g_wait_service = NO_SERVICE, g_cs_service = NO_SERVICE;

static WCT_SESSION *reference(HANDLE handle)
{
    AcquireSRWLockExclusive(&g_lock);
    WCT_SESSION *s = g_sessions;
    while (s && s != handle)
        s = s->Next;
    if (s)
        InterlockedIncrement(&s->Refs);
    ReleaseSRWLockExclusive(&g_lock);
    return s;
}

static void dereference(WCT_SESSION *s)
{
    if (InterlockedDecrement(&s->Refs))
        return;
    if (s->Idle)
        SetEvent(s->Idle);
}

/* ------------------------------------------------------------------ threads */

static PBYTE snapshot(DWORD *error)
{
    ULONG size = 0x10000;
    for (;;) {
        PBYTE list = HeapAlloc(GetProcessHeap(), 0, size);
        if (!list) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        NTSTATUS status = NtQuerySystemInformation(SystemProcessInformation, list, size, &size);
        if (NT_SUCCESS(status)) {
            *error = 0;
            return list;
        }
        HeapFree(GetProcessHeap(), 0, list);
        if (status != STATUS_INFO_LENGTH_MISMATCH) {
            *error = RtlNtStatusToDosError(status);
            return nullptr;
        }
        size += 0x4000;
    }
}

static SYSTEM_THREAD_INFORMATION *find_thread(PBYTE list, DWORD pid, DWORD tid)
{
    for (PBYTE at = list; list;) {
        SYSTEM_PROCESS_INFORMATION *p = (SYSTEM_PROCESS_INFORMATION *)at;
        if ((DWORD)(ULONG_PTR)p->UniqueProcessId == pid) {
            SYSTEM_THREAD_INFORMATION *t = (SYSTEM_THREAD_INFORMATION *)(p + 1);
            for (ULONG i = 0; i < p->NumberOfThreads; i++, t++)
                if ((DWORD)(ULONG_PTR)t->ClientId.UniqueThread == tid)
                    return t;
            return nullptr;
        }
        if (!p->NextEntryOffset)
            return nullptr;
        at += p->NextEntryOffset;
    }
    return nullptr;
}

static void client_close(CLIENT *c)
{
    if (c->Thread)
        CloseHandle(c->Thread);
    if (c->Process)
        CloseHandle(c->Process);
    memset(c, 0, sizeof(*c));
}

static DWORD client_open(DWORD tid, CLIENT *c)
{
    memset(c, 0, sizeof(*c));
    if (!tid)
        return ERROR_INVALID_PARAMETER;
    c->Tid = tid;
    DWORD error;
    THREAD_BASIC_INFORMATION basic;
    ULONG length = 0;
    if (!(c->Thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, tid))) {
        error = GetLastError();
    } else {
        NTSTATUS status = NtQueryInformationThread(c->Thread, ThreadBasicInformation, &basic, sizeof(basic), &length);
        if (!NT_SUCCESS(status) || length != sizeof(basic)) {
            error = RtlNtStatusToDosError(status);
        } else {
            c->Pid = (DWORD)(ULONG_PTR)basic.ClientId.UniqueProcess;
            c->Process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, c->Pid);
            error = c->Process ? ERROR_SUCCESS : GetLastError();
        }
    }
    if (error)
        client_close(c);
    return error;
}

/* KWAIT_REASON values advapi32 takes for a thread that waits on something, not one that only waits for its turn. */
static BOOL blocked(ULONG reason)
{
    return reason == 0 || reason == 4 || reason == 5 || reason == 6 || reason == 13 || reason == 16 || reason == 17 ||
           reason == 37 || reason == 40 || reason == 41;
}

/* the node of a thread; the client stays open for the caller when it asks for it. */
static DWORD thread_node(DWORD tid, PBYTE list, WAITCHAIN_NODE_INFO *node, CLIENT *kept)
{
    CLIENT c;
    DWORD error = client_open(tid, &c);
    SYSTEM_THREAD_INFORMATION *t = error ? nullptr : find_thread(list, c.Pid, tid);
    if (!error && !t)
        error = ERROR_INVALID_THREAD_ID;
    if (!error) {
        node->ObjectType = WctThreadType;
        node->ObjectStatus =
            t->ThreadState == STATE_WAITING && blocked(t->WaitReason) ? WctStatusBlocked : WctStatusRunning;
        node->ThreadObject.ProcessId = c.Pid;
        node->ThreadObject.ThreadId = tid;
        node->ThreadObject.WaitTime = t->WaitTime;
        node->ThreadObject.ContextSwitches = t->ContextSwitches;
    }
    if (!error && kept)
        *kept = c;
    else
        client_close(&c);
    return error;
}

static BOOL same_thread_node(const WAITCHAIN_NODE_INFO *a, const WAITCHAIN_NODE_INFO *b)
{
    return a->ObjectType == WctThreadType && b->ObjectType == WctThreadType && a->ObjectStatus == b->ObjectStatus &&
           a->ThreadObject.ProcessId == b->ThreadObject.ProcessId &&
           a->ThreadObject.ThreadId == b->ThreadObject.ThreadId &&
           a->ThreadObject.WaitTime == b->ThreadObject.WaitTime &&
           a->ThreadObject.ContextSwitches == b->ThreadObject.ContextSwitches;
}

/*
 * like WctCheckChain, a second look: a blocked thread that ran in between, or a node of a kind it does not know, ends
 * the chain there. FALSE when the chain was cut.
 */
static BOOL chain_holds(PBYTE list, LPDWORD count, WAITCHAIN_NODE_INFO *nodes)
{
    for (DWORD i = 0; i < *count; i++) {
        WAITCHAIN_NODE_INFO *n = &nodes[i], now = {};
        if (n->ObjectType == WctUnknownType || !n->ObjectType || n->ObjectType > WctSmbIoType)
            goto cut;
        if (n->ObjectType != WctThreadType || n->ObjectStatus != WctStatusBlocked)
            continue;
        if (!thread_node(n->ThreadObject.ThreadId, list, &now, nullptr) && same_thread_node(n, &now))
            continue;
        n->ObjectStatus = WctStatusRunning;
    cut:
        *count = i + 1;
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ calibration */

typedef struct CALIBRATION {
    HANDLE Event;         /* the thread waits on it in NtWaitForSingleObject, or */
    CRITICAL_SECTION *Cs; /* it waits to enter it, then for Release. */
    HANDLE Release;
} CALIBRATION;

static DWORD CALLBACK calibrator(PVOID param)
{
    CALIBRATION *c = param;
    if (c->Event) {
        LARGE_INTEGER timeout = {.QuadPart = -300000000};
        NtWaitForSingleObject(c->Event, FALSE, &timeout);
    } else {
        EnterCriticalSection(c->Cs);
        WaitForSingleObject(c->Release, INFINITE);
        LeaveCriticalSection(c->Cs);
    }
    return 0;
}

/* WctWaitUntilThreadIsBlocked: a tenth of a second, then a look every half second until it waits for that reason. */
static BOOL waits_for(DWORD tid, ULONG reason)
{
    Sleep(100);
    for (int tries = 0; tries < 10; tries++) {
        DWORD error;
        PBYTE list = snapshot(&error);
        if (!list)
            return FALSE;
        SYSTEM_THREAD_INFORMATION *t = find_thread(list, GetCurrentProcessId(), tid);
        ULONG state = t ? t->ThreadState : 0, why = t ? t->WaitReason : 0;
        HeapFree(GetProcessHeap(), 0, list);
        if (!t)
            return FALSE;
        if (state == STATE_WAITING && why == reason)
            return TRUE;
        if (state != STATE_READY && state != STATE_RUNNING && (state != STATE_WAITING || why))
            return FALSE;
        Sleep(500);
    }
    return FALSE;
}

/* the number of the service the helper thread waits in, NO_SERVICE when it cannot be told. */
static USHORT calibrate(BOOL critical_section)
{
    CRITICAL_SECTION cs;
    CALIBRATION c = {};
    USHORT number = NO_SERVICE;
    if (critical_section) {
        if (!(c.Release = CreateEventW(nullptr, FALSE, FALSE, nullptr)))
            return number;
        InitializeCriticalSection(&cs);
        EnterCriticalSection(&cs);
        c.Cs = &cs;
    } else if (!(c.Event = CreateEventW(nullptr, TRUE, FALSE, nullptr))) {
        return number;
    }
    DWORD tid;
    HANDLE thread = CreateThread(nullptr, 0, calibrator, &c, 0, &tid);
    if (thread) {
        LAST_CALL last;
        ULONG length = 0;
        if (waits_for(tid, critical_section ? WAIT_ALERT_BY_THREAD_ID : WAIT_USER_REQUEST) &&
            NT_SUCCESS(NtQueryInformationThread(thread, THREAD_LAST_SYSTEM_CALL, &last, sizeof(last), &length)) &&
            length == sizeof(last) && last.SystemCallNumber)
            number = last.SystemCallNumber;
    }
    if (critical_section)
        LeaveCriticalSection(&cs);
    SetEvent(critical_section ? c.Release : c.Event);
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    if (critical_section)
        DeleteCriticalSection(&cs);
    CloseHandle(critical_section ? c.Release : c.Event);
    return number;
}

/* WctInitPackage: once per process, again after a failure. */
static BOOL calibrated(void)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    if (g_wait_service == NO_SERVICE)
        g_wait_service = calibrate(FALSE);
    if (g_cs_service == NO_SERVICE)
        g_cs_service = calibrate(TRUE);
    BOOL ok = g_wait_service != NO_SERVICE && g_cs_service != NO_SERVICE;
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

/* ------------------------------------------------------------------ locks */

/* the owner of a critical section, as ntdll of the waiting process finds it from its semaphore or the waited address. */
static DWORD critical_section(const CLIENT *c, ULONG how, ULONG lock, WAITCHAIN_NODE_INFO *node, DWORD *tid, DWORD *pid)
{
    PBYTE buffer = RtlCreateQueryDebugBuffer(0, FALSE);
    if (!buffer)
        return ERROR_NOT_ENOUGH_MEMORY;
    *(ULONG *)(buffer + DEBUG_LOCK_IN) = lock;
    DWORD error = ERROR_SUCCESS;
    NTSTATUS status = RtlQueryProcessDebugInformation((HANDLE)(ULONG_PTR)c->Pid, how, buffer);
    DWORD owner = *(ULONG *)(buffer + DEBUG_OWNER_OUT);
    if (!NT_SUCCESS(status)) {
        error = RtlNtStatusToDosError(status);
    } else if (!owner) {
        error = ERROR_NOT_FOUND;
    } else {
        CLIENT o;
        node->ObjectType = WctCriticalSectionType;
        if (client_open(owner, &o)) {
            node->ObjectStatus = WctStatusAbandoned;
        } else if (o.Pid != c->Pid) {
            error = ERROR_NOT_FOUND;
        } else {
            node->ObjectStatus = WctStatusOwned;
            *tid = owner;
            *pid = o.Pid;
        }
        client_close(&o);
    }
    RtlDestroyQueryDebugBuffer(buffer);
    return error;
}

static DWORD mutant(HANDLE h, WAITCHAIN_NODE_INFO *node, DWORD *tid, DWORD *pid)
{
    struct {
        ULONG Process, Thread;
    } owner;
    ULONG length = 0;
    if (!NT_SUCCESS(NtQueryMutant(h, 1, &owner, sizeof(owner), &length)))
        return ERROR_NOT_FOUND;
    if (length != sizeof(owner))
        return ERROR_BAD_LENGTH;
    if (NtQueryObject(h, ObjectNameInformation, nullptr, 0, &length) == STATUS_INFO_LENGTH_MISMATCH) {
        OBJECT_NAME_INFORMATION *name = HeapAlloc(GetProcessHeap(), 0, length);
        if (name && NT_SUCCESS(NtQueryObject(h, ObjectNameInformation, name, length, nullptr))) {
            ULONG chars = name->Name.Length / sizeof(WCHAR);
            if (chars >= RTL_NUMBER_OF(node->LockObject.ObjectName))
                chars = RTL_NUMBER_OF(node->LockObject.ObjectName) - 1;
            memcpy(node->LockObject.ObjectName, name->Name.Buffer, chars * sizeof(WCHAR));
            node->LockObject.ObjectName[chars] = 0;
        }
        HeapFree(GetProcessHeap(), 0, name);
    }
    node->ObjectStatus = WctStatusOwned;
    *tid = owner.Thread;
    *pid = owner.Process;
    if (!owner.Thread) {
        struct {
            LONG Count;
            BOOLEAN Mine, Abandoned;
        } basic;
        NTSTATUS status = NtQueryMutant(h, 0, &basic, sizeof(basic), &length);
        if (!NT_SUCCESS(status))
            return RtlNtStatusToDosError(status);
        if (length != sizeof(basic))
            return ERROR_BAD_LENGTH;
        if (basic.Abandoned)
            node->ObjectStatus = WctStatusAbandoned;
    }
    node->ObjectType = WctMutexType;
    return ERROR_SUCCESS;
}

/* WctGetObjectInfo: the waited handle in this process, with the type that tells how to follow it. */
static DWORD waited_object(const CLIENT *c, ULONG value, HANDLE *out, WCT_OBJECT_TYPE *type)
{
    static const struct {
        PCWSTR Name;
        WCT_OBJECT_TYPE Type;
        ACCESS_MASK Access;
    } kinds[] = {
        {L"Event", WctCriticalSectionType, 0},
        {L"Mutant", WctMutexType, 0x1F0001},
        {L"Thread", WctThreadWaitType, 0x1FFFFF},
        {L"Process", WctProcessWaitType, 0x1FFFFF},
    };
    HANDLE h = nullptr, source = (HANDLE)(ULONG_PTR)value, from = c->Process;
    /* the thread waits on itself: its handle here stands for it. */
    if (source == GetCurrentThread()) {
        source = c->Thread;
        from = GetCurrentProcess();
    }
    *out = nullptr;
    *type = WctUnknownType;
    if (!DuplicateHandle(from, source, GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return GetLastError();
    if (!h || h == INVALID_HANDLE_VALUE)
        return ERROR_INVALID_PARAMETER;
    ULONG length = 0;
    NTSTATUS status = NtQueryObject(h, ObjectTypeInformation, nullptr, 0, &length);
    UNICODE_STRING *name = status == STATUS_INFO_LENGTH_MISMATCH ? HeapAlloc(GetProcessHeap(), 0, length) : nullptr;
    DWORD error = ERROR_SUCCESS;
    if (status != STATUS_INFO_LENGTH_MISMATCH)
        error = RtlNtStatusToDosError(status);
    else if (!name)
        error = ERROR_NOT_ENOUGH_MEMORY;
    else if (!NT_SUCCESS(status = NtQueryObject(h, ObjectTypeInformation, name, length, &length)))
        error = RtlNtStatusToDosError(status);
    for (ULONG i = 0; !error && i < RTL_NUMBER_OF(kinds); i++) {
        UNICODE_STRING kind;
        RtlInitUnicodeString(&kind, kinds[i].Name);
        if (!RtlEqualUnicodeString(&kind, name, FALSE))
            continue;
        *type = kinds[i].Type;
        if (kinds[i].Access) {
            CloseHandle(h);
            h = nullptr;
            if (!DuplicateHandle(from, source, GetCurrentProcess(), &h, kinds[i].Access, FALSE, 0))
                error = GetLastError();
        }
        break;
    }
    HeapFree(GetProcessHeap(), 0, name);
    if (error) {
        if (h)
            CloseHandle(h);
        return error;
    }
    *out = h;
    return ERROR_SUCCESS;
}

static DWORD thread_or_process(HANDLE h, WCT_OBJECT_TYPE type, WAITCHAIN_NODE_INFO *node, DWORD *tid, DWORD *pid)
{
    if (type == WctThreadWaitType) {
        THREAD_BASIC_INFORMATION basic;
        NTSTATUS status = NtQueryInformationThread(h, ThreadBasicInformation, &basic, sizeof(basic), nullptr);
        if (!NT_SUCCESS(status))
            return RtlNtStatusToDosError(status);
        *tid = (DWORD)(ULONG_PTR)basic.ClientId.UniqueThread;
        *pid = (DWORD)(ULONG_PTR)basic.ClientId.UniqueProcess;
    } else {
        *tid = 0;
        if (!(*pid = GetProcessId(h)))
            return GetLastError();
    }
    node->ObjectType = type;
    node->ObjectStatus = WctStatusOwned;
    return ERROR_SUCCESS;
}

/* WctGetLockInfo: the node of what the client waits on, and the thread or process that holds it. */
static DWORD lock_node(const CLIENT *c, DWORD flags, DWORD first_pid, SEND_RECEIVER receiver,
                       WAITCHAIN_NODE_INFO *node, DWORD *tid, DWORD *pid)
{
    *tid = *pid = 0;
    LAST_CALL last;
    ULONG length = 0;
    if (!NT_SUCCESS(NtQueryInformationThread(c->Thread, THREAD_LAST_SYSTEM_CALL, &last, 16, &length)) || length != 16)
        return ERROR_INVALID_PARAMETER;
    BOOL here = (flags & WCT_OUT_OF_PROC_CS_FLAG) || c->Pid == first_pid;
    if (last.SystemCallNumber == g_wait_service) {
        HANDLE h;
        WCT_OBJECT_TYPE type;
        DWORD error = waited_object(c, last.FirstArgument, &h, &type);
        if (error)
            return error;
        if (type == WctThreadWaitType || type == WctProcessWaitType) {
            error = thread_or_process(h, type, node, tid, pid);
            CloseHandle(h);
            return error;
        }
        if (type == WctCriticalSectionType && here)
            error = critical_section(c, DEBUG_BY_SEMAPHORE, last.FirstArgument, node, tid, pid);
        else if (type == WctMutexType)
            error = mutant(h, node, tid, pid);
        else
            error = ERROR_NOT_FOUND;
        CloseHandle(h);
        if (!error)
            return error;
        memset(node, 0, sizeof(*node));
    } else if (last.SystemCallNumber == g_cs_service && here) {
        /* the address is taken four bytes on first, as when advapi32 has its fix for the waits of critical sections. */
        if (!critical_section(c, DEBUG_BY_ADDRESS, last.FirstArgument + 4, node, tid, pid) ||
            !critical_section(c, DEBUG_BY_ADDRESS, last.FirstArgument, node, tid, pid))
            return ERROR_SUCCESS;
        memset(node, 0, sizeof(*node));
    }
    HWND window = receiver ? receiver(c->Tid) : nullptr;
    if (window) {
        DWORD owner_pid = 0, owner = GetWindowThreadProcessId(window, &owner_pid);
        if (owner != c->Tid) {
            node->ObjectType = WctSendMessageType;
            node->ObjectStatus = owner ? WctStatusOwned : WctStatusNotOwned;
            *tid = owner;
            *pid = owner_pid;
            return ERROR_SUCCESS;
        }
    }
    return ERROR_NOT_FOUND;
}

/* ------------------------------------------------------------------ chains */

/* where the next node goes: in the caller's array, or past its end in a spare one, which only counts. */
static WAITCHAIN_NODE_INFO *next_node(WAITCHAIN_NODE_INFO *nodes, DWORD capacity, DWORD *used, BOOL *overflow,
                                      WAITCHAIN_NODE_INFO *spare)
{
    if (*overflow || *used >= capacity) {
        *overflow = TRUE;
        memset(spare, 0, sizeof(*spare));
        (*used)++;
        return spare;
    }
    return &nodes[(*used)++];
}

static DWORD wait_chain(WCT_SESSION *s, DWORD flags, DWORD tid, LPDWORD count, WAITCHAIN_NODE_INFO *nodes,
                        LPBOOL cycle)
{
    DWORD capacity = *count, used = 0, step = 0, error;
    BOOL overflow = FALSE;
    WAITCHAIN_NODE_INFO spare;
    *cycle = FALSE;
    memset(nodes, 0, capacity * sizeof(*nodes));
    HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, tid);
    if (!thread)
        return GetLastError();
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    SEND_RECEIVER receiver = user32 ? (SEND_RECEIVER)GetProcAddress(user32, "GetSendMessageReceiver") : nullptr;
    PBYTE list = snapshot(&error);
    CLIENT c = {};
    DWORD first_pid = 0, owner_pid = 0, next = tid;
    while (list) {
        if (s->Closed) {
            error = ERROR_CANCELLED;
            goto done;
        }
        if (used == WCT_MAX_NODE_COUNT)
            break;
        DWORD index = used;
        WAITCHAIN_NODE_INFO *node = next_node(nodes, capacity, &used, &overflow, &spare);
        if ((step = thread_node(next, list, node, &c))) {
            if (used <= 2)
                break;
            if (step == ERROR_ACCESS_DENIED) {
                node->ObjectType = WctThreadType;
                node->ObjectStatus = WctStatusNoAccess;
                node->ThreadObject.ProcessId = owner_pid;
                node->ThreadObject.ThreadId = next;
                step = 0;
            } else if (!overflow) {
                nodes[used - 2].ObjectStatus = WctStatusAbandoned;
            }
            break;
        }
        if (!first_pid)
            first_pid = c.Pid;
        if ((node->ObjectStatus != WctStatusBlocked && node->ObjectStatus != WctStatusPidOnly) ||
            (!(flags & WCT_OUT_OF_PROC_FLAG) && c.Pid != first_pid))
            break;
        /* a thread met before closes a cycle when it waits as it did then. */
        BOOL met = FALSE;
        for (DWORD j = 0; !overflow && j < index && !met; j++)
            if (nodes[j].ObjectType == WctThreadType && nodes[j].ThreadObject.ThreadId == next) {
                met = TRUE;
                if (same_thread_node(&nodes[j], node))
                    *cycle = TRUE;
            }
        if (met || used == WCT_MAX_NODE_COUNT)
            break;
        WAITCHAIN_NODE_INFO *lock = next_node(nodes, capacity, &used, &overflow, &spare);
        if ((step = lock_node(&c, flags, first_pid, receiver, lock, &next, &owner_pid)) ||
            lock->ObjectStatus != WctStatusOwned)
            break;
        if (!next && owner_pid) {
            /* a process holds it: its node has no thread. */
            if (used == WCT_MAX_NODE_COUNT)
                break;
            WAITCHAIN_NODE_INFO *holder = next_node(nodes, capacity, &used, &overflow, &spare);
            holder->ObjectType = WctThreadType;
            holder->ObjectStatus = WctStatusPidOnly;
            holder->ThreadObject.ProcessId = owner_pid;
            break;
        }
        client_close(&c);
    }
    client_close(&c);
    /* a step that failed takes back its node, and with it the overflow when the chain fits again. */
    if (step && used && --used <= capacity)
        overflow = FALSE;
    if (list) {
        HeapFree(GetProcessHeap(), 0, list);
        list = snapshot(&error);
    }
    if (list) {
        if (!overflow)
            *count = used;
        if (!chain_holds(list, count, nodes)) {
            *cycle = FALSE;
        } else if (overflow) {
            *count = used;
            error = ERROR_MORE_DATA;
        } else if (used == WCT_MAX_NODE_COUNT) {
            error = ERROR_TOO_MANY_THREADS;
        }
        if (!error && !used)
            error = ERROR_INVALID_THREAD_ID;
    }
done:
    if (list)
        HeapFree(GetProcessHeap(), 0, list);
    if (user32)
        FreeLibrary(user32);
    CloseHandle(thread);
    return error;
}

static DWORD CALLBACK query_worker(PVOID param)
{
    WCT_QUERY *q = param;
    WCT_SESSION *s = q->Session;
    DWORD error = s->Closed ? ERROR_CANCELLED : wait_chain(s, q->Flags, q->Thread, q->Count, q->Nodes, q->Cycle);
    if (!s->Closed)
        s->Callback(s, q->Context, error, q->Count, q->Nodes, q->Cycle);
    HeapFree(GetProcessHeap(), 0, q);
    dereference(s);
    return 0;
}

/* measured on windows 11 26100 with api.c and wct.c of scratch/pr17 in the research repository, in both widths. */
ADVAPI HANDLE WINAPI OpenThreadWaitChainSession(DWORD flags, PWAITCHAINCALLBACK callback)
{
    if ((flags & ~WCT_ASYNC_OPEN_FLAG) || ((flags & WCT_ASYNC_OPEN_FLAG) && !callback)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!calibrated()) {
        AdvSetError(ERROR_INTERNAL_ERROR);
        return nullptr;
    }
    WCT_SESSION *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    HANDLE idle = s ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    if (!idle) {
        HeapFree(GetProcessHeap(), 0, s);
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    s->Flags = flags;
    s->Callback = callback;
    s->Refs = 1;
    s->Idle = idle;
    AcquireSRWLockExclusive(&g_lock);
    s->Next = g_sessions;
    g_sessions = s;
    ReleaseSRWLockExclusive(&g_lock);
    return s;
}

/* like advapi32, waits for the queries still running and calls none of their callbacks. */
ADVAPI void WINAPI CloseThreadWaitChainSession(HANDLE handle)
{
    AcquireSRWLockExclusive(&g_lock);
    WCT_SESSION **link = &g_sessions;
    while (*link && *link != handle)
        link = &(*link)->Next;
    WCT_SESSION *s = *link;
    if (s)
        *link = s->Next;
    ReleaseSRWLockExclusive(&g_lock);
    if (!s)
        return;
    InterlockedExchange(&s->Closed, 1);
    if (InterlockedDecrement(&s->Refs))
        WaitForSingleObject(s->Idle, INFINITE);
    CloseHandle(s->Idle);
    HeapFree(GetProcessHeap(), 0, s);
}

ADVAPI BOOL WINAPI GetThreadWaitChain(HANDLE handle, DWORD_PTR context, DWORD flags, DWORD tid, LPDWORD count,
                                     WAITCHAIN_NODE_INFO *nodes, LPBOOL cycle)
{
    WCT_SESSION *s = nullptr;
    if (!handle || (flags & ~WCT_FLAGS) || !count || *count - 1 >= WCT_MAX_NODE_COUNT || !nodes || !cycle ||
        !(s = reference(handle))) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD error;
    if (s->Flags & WCT_ASYNC_OPEN_FLAG) {
        WCT_QUERY *q = HeapAlloc(GetProcessHeap(), 0, sizeof(*q));
        error = ERROR_NOT_ENOUGH_MEMORY;
        if (q) {
            *q = (WCT_QUERY){s, context, flags, tid, count, nodes, cycle};
            HANDLE worker = CreateThread(nullptr, 0, query_worker, q, 0, nullptr);
            if (worker) {
                CloseHandle(worker);
                AdvSetError(ERROR_IO_PENDING);
                return FALSE;
            }
            HeapFree(GetProcessHeap(), 0, q);
        }
    } else {
        error = wait_chain(s, flags, tid, count, nodes, cycle);
    }
    dereference(s);
    if (error) {
        AdvSetError(error);
        return FALSE;
    }
    return TRUE;
}

/* the hooks that let the traversal follow com calls, which it does not follow here. */
ADVAPI void WINAPI RegisterWaitChainCOMCallback(PVOID call_state, PVOID activation_state)
{
}
