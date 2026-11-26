/*
 * the wait chain traversal of advapi32 26300 as far as the threads go: a chain is the thread asked about, running or
 * blocked as the system's process list tells. advapi32 then reads the lock a blocked thread waits on out of its
 * process, and drops that node when it cannot; here it never can, so a chain is always the one thread.
 */
#include "advapi32_private.h"

#define WCT_ASYNC_OPEN_FLAG 0x1
#define WCT_FLAGS 0xF
#define WCT_MAX_NODE_COUNT 16

typedef enum { WctThreadType = 8 } WCT_OBJECT_TYPE;
typedef enum { WctStatusRunning = 2, WctStatusBlocked = 3 } WCT_OBJECT_STATUS;

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
    DWORD Thread;
    LPDWORD Count;
    WAITCHAIN_NODE_INFO *Nodes;
    LPBOOL Cycle;
} WCT_QUERY;

static SRWLOCK g_lock = SRWLOCK_INIT;
static WCT_SESSION *g_sessions;

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

/* KWAIT_REASON values advapi32 takes for a thread that waits on something, not one that only waits for its turn. */
static BOOL blocked(ULONG reason)
{
    return reason == 0 || reason == 4 || reason == 5 || reason == 6 || reason == 13 || reason == 16 || reason == 17 ||
           reason == 37 || reason == 40 || reason == 41;
}

static DWORD thread_node(DWORD tid, WAITCHAIN_NODE_INFO *node)
{
    ULONG size = 0x10000;
    for (;;) {
        PBYTE list = HeapAlloc(GetProcessHeap(), 0, size);
        if (!list)
            return ERROR_NOT_ENOUGH_MEMORY;
        NTSTATUS status = NtQuerySystemInformation(SystemProcessInformation, list, size, &size);
        if (status == STATUS_INFO_LENGTH_MISMATCH) {
            HeapFree(GetProcessHeap(), 0, list);
            size += 0x4000;
            continue;
        }
        DWORD error = NT_SUCCESS(status) ? ERROR_INVALID_THREAD_ID : RtlNtStatusToDosError(status);
        for (PBYTE at = list; NT_SUCCESS(status);) {
            SYSTEM_PROCESS_INFORMATION *p = (SYSTEM_PROCESS_INFORMATION *)at;
            SYSTEM_THREAD_INFORMATION *t = (SYSTEM_THREAD_INFORMATION *)(p + 1);
            for (ULONG i = 0; i < p->NumberOfThreads && error; i++, t++) {
                if ((DWORD)(ULONG_PTR)t->ClientId.UniqueThread != tid)
                    continue;
                memset(node, 0, sizeof(*node));
                node->ObjectType = WctThreadType;
                node->ObjectStatus = t->ThreadState == 5 && blocked(t->WaitReason) ? WctStatusBlocked : WctStatusRunning;
                node->ThreadObject.ProcessId = (DWORD)(ULONG_PTR)t->ClientId.UniqueProcess;
                node->ThreadObject.ThreadId = tid;
                node->ThreadObject.WaitTime = t->WaitTime;
                node->ThreadObject.ContextSwitches = t->ContextSwitches;
                error = ERROR_SUCCESS;
            }
            if (!error || !p->NextEntryOffset)
                break;
            at += p->NextEntryOffset;
        }
        HeapFree(GetProcessHeap(), 0, list);
        return error;
    }
}

static DWORD wait_chain(DWORD tid, LPDWORD count, WAITCHAIN_NODE_INFO *nodes, LPBOOL cycle)
{
    *cycle = FALSE;
    memset(nodes, 0, *count * sizeof(*nodes));
    HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, tid);
    if (!thread)
        return GetLastError();
    CloseHandle(thread);
    DWORD error = thread_node(tid, &nodes[0]);
    if (!error)
        *count = 1;
    return error;
}

static DWORD CALLBACK query_worker(PVOID param)
{
    WCT_QUERY *q = param;
    WCT_SESSION *s = q->Session;
    DWORD error = s->Closed ? ERROR_CANCELLED : wait_chain(q->Thread, q->Count, q->Nodes, q->Cycle);
    if (!s->Closed)
        s->Callback(s, q->Context, error, q->Count, q->Nodes, q->Cycle);
    HeapFree(GetProcessHeap(), 0, q);
    dereference(s);
    return 0;
}

/* measured on windows 11 26100 with api.c of scratch/pr17 in the research repository, in both widths. */
ADVAPI HANDLE WINAPI OpenThreadWaitChainSession(DWORD flags, PWAITCHAINCALLBACK callback)
{
    if ((flags & ~WCT_ASYNC_OPEN_FLAG) || ((flags & WCT_ASYNC_OPEN_FLAG) && !callback)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
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
            *q = (WCT_QUERY){s, context, tid, count, nodes, cycle};
            HANDLE worker = CreateThread(nullptr, 0, query_worker, q, 0, nullptr);
            if (worker) {
                CloseHandle(worker);
                AdvSetError(ERROR_IO_PENDING);
                return FALSE;
            }
            HeapFree(GetProcessHeap(), 0, q);
        }
    } else {
        error = wait_chain(tid, count, nodes, cycle);
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
