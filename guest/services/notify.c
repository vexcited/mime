/*
 * services.exe: NotifyServiceStatusChange. a client asks to be told when a service enters a state, or when
 * services are created or deleted; the manager completes the request once, as windows does, and writes the
 * completion down a pipe the client keeps open for them.
 */
#include "services.h"

static NOTIFY *g_manager_notifies;
static NOTIFY *g_queue_head, *g_queue_tail;
static HANDLE g_queue_event;
static BOOL g_notifier_started;

static void queue_completion(NOTIFY *n)
{
    if (n->Queued)
        return;
    n->Queued = TRUE;
    n->NextQueued = nullptr;
    if (g_queue_tail)
        g_queue_tail->NextQueued = n;
    else
        g_queue_head = n;
    g_queue_tail = n;
    if (g_queue_event)
        SetEvent(g_queue_event);
}

static void unlink_service(NOTIFY *n)
{
    NOTIFY **link = n->Service ? &n->Service->Notify : &g_manager_notifies;
    for (; *link; link = &(*link)->Next) {
        if (*link == n) {
            *link = n->Next;
            break;
        }
    }
}

static void unlink_client(NOTIFY *n)
{
    for (NOTIFY **link = &n->Client->Notifies; *link; link = &(*link)->NextOfClient) {
        if (*link == n) {
            *link = n->NextOfClient;
            break;
        }
    }
}

/* a request is done: it leaves its service and waits in the queue to be written. */
static void complete(NOTIFY *n, DWORD status, DWORD triggered, SERVICE *s, const WCHAR *names, DWORD names_chars)
{
    unlink_service(n);
    n->Status = status;
    n->Triggered = triggered;
    if (s)
        RunStatusSnapshot(s, &n->Process);
    if (names && names_chars) {
        n->Names = SvcAlloc(names_chars * sizeof(WCHAR));
        if (n->Names) {
            memcpy(n->Names, names, names_chars * sizeof(WCHAR));
            n->NamesChars = names_chars;
        }
    }
    queue_completion(n);
}

void NotifyServiceChanged(SERVICE *s, DWORD old_state)
{
    (void)old_state;
    DWORD state = s->Status.dwCurrentState;
    if (state < 1 || state > 7)
        return;
    DWORD bit = 1u << (state - 1);
    for (NOTIFY *n = s->Notify, *next; n; n = next) {
        next = n->Next;
        if (n->Mask & bit)
            complete(n, ERROR_SUCCESS, bit, s, nullptr, 0);
    }
}

/* the service is marked for deletion, or gone: the requests for a state end with an error. */
void NotifyServiceDeleting(SERVICE *s, BOOL gone)
{
    for (NOTIFY *n = s->Notify, *next; n; n = next) {
        next = n->Next;
        if (n->Mask & SERVICE_NOTIFY_DELETE_PENDING)
            complete(n, ERROR_SUCCESS, SERVICE_NOTIFY_DELETE_PENDING, s, nullptr, 0);
        else if (gone)
            complete(n, ERROR_SERVICE_MARKED_FOR_DELETE, 0, s, nullptr, 0);
    }
}

void NotifyCreatedDeleted(const WCHAR *name, BOOL created)
{
    DWORD bit = created ? SERVICE_NOTIFY_CREATED : SERVICE_NOTIFY_DELETED;
    DWORD chars = (DWORD)wcslen(name) + 2;
    WCHAR *names = SvcAllocZero(chars * sizeof(WCHAR));
    if (!names)
        return;
    memcpy(names, name, wcslen(name) * sizeof(WCHAR));
    for (NOTIFY *n = g_manager_notifies, *next; n; n = next) {
        next = n->Next;
        if (n->Mask & bit)
            complete(n, ERROR_SUCCESS, bit, nullptr, names, chars);
    }
    SvcFree(names);
}

/* NotifyServiceStatusChange: the request is added, and met at once when the state is already one it asks for. */
DWORD NotifyRegister(CLIENT *client, SCHANDLE *handle, UINT64 block, DWORD mask, DWORD version, DWORD *status_out)
{
    (void)status_out;
    SERVICE *s = handle->Kind == SCH_SERVICE ? handle->Service : nullptr;
    if (s) {
        if (s->Type & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER | SERVICE_RECOGNIZER_DRIVER | SERVICE_ADAPTER))
            return ERROR_INVALID_PARAMETER;
        if (s->Flags & SVF_DELETE_PENDING)
            return ERROR_SERVICE_MARKED_FOR_DELETE;
    }
    if (!client->NotifyPipe)
        return ERROR_NOT_FOUND;
    NOTIFY *n = SvcAllocZero(sizeof(*n));
    if (!n)
        return ERROR_NOT_ENOUGH_MEMORY;
    n->Client = client;
    n->Block = block;
    n->Service = s;
    n->Mask = mask;
    n->Version = version;
    n->Base = g_stamp;
    n->Next = s ? s->Notify : g_manager_notifies;
    if (s)
        s->Notify = n;
    else
        g_manager_notifies = n;
    n->NextOfClient = client->Notifies;
    client->Notifies = n;
    if (s) {
        DWORD state = s->Status.dwCurrentState;
        SERVICE_STATUS_PROCESS p;
        RunStatusSnapshot(s, &p);
        state = p.dwCurrentState;
        DWORD bit = state >= 1 && state <= 7 ? 1u << (state - 1) : 0;
        if (mask & bit)
            complete(n, ERROR_SUCCESS, bit, s, nullptr, 0);
    }
    return ERROR_SUCCESS;
}

/* a client is gone: its requests, and what is still queued for it. */
void NotifyClientGone(CLIENT *client)
{
    for (NOTIFY *n = client->Notifies, *next; n; n = next) {
        next = n->NextOfClient;
        if (!n->Queued)
            unlink_service(n);
        else {
            /* it is in the queue: the writer drops it. */
            n->Client = nullptr;
            continue;
        }
        SvcFree(n->Names);
        SvcFree(n);
    }
    client->Notifies = nullptr;
}

/* writes one completion; the lock is not held. */
static void write_one(NOTIFY *n, CLIENT *client, HANDLE pipe, HANDLE event)
{
    SCM_WRITER w;
    ScmWriterInit(&w, 0);
    ScmPutU64(&w, n->Block);
    ScmPutU32(&w, n->Status);
    ScmPutU32(&w, n->Triggered);
    const DWORD *fields = (const DWORD *)&n->Process;
    for (int i = 0; i < 9; i++)
        ScmPutU32(&w, fields[i]);
    ScmPutChars(&w, n->Names, n->NamesChars);
    UINT32 size;
    UCHAR *message = ScmWriterFinish(&w, &size);
    if (message) {
        OVERLAPPED ov = {};
        ov.hEvent = event;
        ResetEvent(event);
        DWORD done = 0;
        BOOL ok = WriteFile(pipe, message, size, &done, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(event, 5000) == WAIT_TIMEOUT) {
                CancelIoEx(pipe, &ov);
                GetOverlappedResult(pipe, &ov, &done, TRUE);
                ok = FALSE;
            } else {
                ok = GetOverlappedResult(pipe, &ov, &done, FALSE);
            }
        }
        if (!ok) {
            EnterCriticalSection(&g_lock);
            if (client->NotifyPipe == pipe) {
                client->NotifyPipe = nullptr;
                CloseHandle(pipe);
            }
            LeaveCriticalSection(&g_lock);
        }
    }
    SvcFree(w.Data);
}

static DWORD WINAPI notifier(LPVOID unused)
{
    (void)unused;
    HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    for (;;) {
        WaitForSingleObject(g_queue_event, INFINITE);
        for (;;) {
            EnterCriticalSection(&g_lock);
            NOTIFY *n = g_queue_head;
            if (n) {
                g_queue_head = n->NextQueued;
                if (!g_queue_head)
                    g_queue_tail = nullptr;
            }
            CLIENT *client = n ? n->Client : nullptr;
            HANDLE pipe = client ? client->NotifyPipe : nullptr;
            if (n && client)
                unlink_client(n);
            LeaveCriticalSection(&g_lock);
            if (!n)
                break;
            if (client && pipe)
                write_one(n, client, pipe, io_event);
            SvcFree(n->Names);
            SvcFree(n);
        }
    }
}

/* a client opens its pipe for completions: kept until the client goes. */
DWORD NotifyAttach(CLIENT *client, HANDLE pipe)
{
    if (client->NotifyPipe)
        return ERROR_ALREADY_EXISTS;
    if (!g_notifier_started) {
        g_notifier_started = TRUE;
        g_queue_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = CreateThread(nullptr, 0, notifier, nullptr, 0, nullptr);
        if (t)
            CloseHandle(t);
    }
    client->NotifyPipe = pipe;
    return ERROR_SUCCESS;
}

void NotifyFlush(CLIENT *client)
{
    (void)client;
}
