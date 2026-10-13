/*
 * the socket driver, \Device\Afd. a socket file is bound to a non blocking
 * socket of the host. an operation that would block becomes a request
 * queued on its socket, and the host is asked to watch the socket; once
 * the watch fires, the worker thread retries the requests, records the
 * WSAEventSelect events and wakes the threads in select or WSAPoll.
 *
 * requests complete like any overlapped i/o: the status block, the event,
 * an apc or the completion port of the file. data moves through one
 * bounce buffer, so the driver works under one lock.
 */
#include "../ntos.h"

#include <mime/afd.h>

#define AFD_CHUNK 65536
#define AFD_MAX_BUFFERS 1024
#define AFD_MAX_POLL 1024
#define AFD_EVENTS_BATCH 256
#define AFD_MAX_ANSWERS 64
#define AFD_MAX_INTERFACES 64

#define WSAEWOULDBLOCK 10035
#define WSAEALREADY 10037
#define WSAEINVAL 10022
#define WSAEFAULT 10014
#define WSAENOTSOCK 10038
#define WSAEMSGSIZE 10040
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAESHUTDOWN 10058
#define WSAECONNABORTED 10053
#define WSAECONNREFUSED 10061
#define WSAENOBUFS 10055

#define MSG_OOB 0x1
#define MSG_PEEK 0x2
#define MSG_DONTROUTE 0x4
#define MSG_WAITALL 0x8

#define SOCK_STREAM 1
#define SOCK_DGRAM 2

#define FD_READ 0x01
#define FD_WRITE 0x02
#define FD_OOB 0x04
#define FD_ACCEPT 0x08
#define FD_CONNECT 0x10
#define FD_CLOSE 0x20
#define FD_ALL 0x3FF
#define FD_CONNECT_BIT 4
#define FD_CLOSE_BIT 5

#define FILE_DEVICE_NETWORK 0x12
#define FILE_DEVICE_NAMED_PIPE 0x11

MIME_STATIC_ASSERT(MIME_AFD_OPT_ATMARK == MIME_NET_OPT_ATMARK && MIME_AFD_OPT_ERROR == MIME_NET_OPT_ERROR,
                   "afd options are the host's");
MIME_STATIC_ASSERT(sizeof(MIME_AFD_ADDRESS) == sizeof(MimeNetAddr), "afd addresses are the host's");
MIME_STATIC_ASSERT(sizeof(MIME_AFD_INTERFACE) == sizeof(MimeNetInterface), "afd interfaces are the host's");

enum { AFD_RECV, AFD_SEND, AFD_CONNECT, AFD_ACCEPT };
enum { AFD_BLOCKED, AFD_DONE, AFD_MOVED };

typedef struct AFD_REQUEST {
    LIST_ENTRY Links;
    ULONG Kind;
    PEPROCESS Process;
    PETHREAD Thread;
    PVOID ApcRoutine;
    PFILE_OBJECT File;
    PKEVENT Event;
    PVOID ApcContext;
    PIO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    ULONG_PTR Information;
    BOOLEAN Pending; /* went pending, so even a failure reaches the apc or the port */
    ULONG Flags; /* MSG_* */
    ULONG Total;
    ULONG Transferred;
    PVOID From;        /* receive: the caller's sockaddr and length */
    PVOID FromLength;
    LONG FromCapacity;
    BOOLEAN HasTo;
    MIME_AFD_ADDRESS To; /* send: the destination of a datagram */
    PFILE_OBJECT Accept;  /* accept: the socket that takes the connection */
    PVOID AcceptAddresses;
    ULONG Count;
    MIME_AFD_BUFFER Buffers[];
} AFD_REQUEST;

typedef struct AFD_SOCKET {
    LIST_ENTRY Links;
    ULONG Host; /* the host socket, 0 before IOCTL_AFD_CREATE */
    ULONG Family;
    ULONG Type;
    ULONG Protocol;
    ULONG State;
    BOOLEAN Closed;
    BOOLEAN Bound;
    BOOLEAN NonBlocking;
    BOOLEAN ShutRead;
    BOOLEAN ShutWrite;
    BOOLEAN Hup;    /* the peer ended the stream */
    BOOLEAN Broken; /* the connection broke */
    BOOLEAN ConnectFailed;
    BOOLEAN ConnectReport; /* FD_CONNECT is due */
    BOOLEAN CloseReported;
    LONG BrokenError;
    LONG ConnectError;
    LONG SoError; /* the pending error SO_ERROR reports */
    ULONG64 ConnectTime;
    LIST_ENTRY Receivers;
    LIST_ENTRY Senders;
    LIST_ENTRY Connects;
    LIST_ENTRY Accepters;
    PKEVENT SelectEvent;
    ULONG SelectMask;
    ULONG Enabled;
    ULONG Recorded;
    LONG Errors[10];
} AFD_SOCKET;

/* a thread in select or WSAPoll, woken whenever the worker ran. */
typedef struct AFD_POLLER {
    LIST_ENTRY Links;
    KEVENT Wake;
} AFD_POLLER;

/* a thread waiting for a host name. */
typedef struct AFD_RESOLVER {
    LIST_ENTRY Links;
    ULONG Id;
    BOOLEAN Done;
    KEVENT Wake;
} AFD_RESOLVER;

static PDEVICE_OBJECT g_device;
static KMUTEX g_lock;
static KEVENT g_work;
static LIST_ENTRY g_sockets;
static LIST_ENTRY g_pollers;
static LIST_ENTRY g_resolvers;
static PUCHAR g_bounce;
/* host arguments live in the kernel image, which is physically contiguous. */
static MIME_AFD_ADDRESS g_addr __attribute__((aligned(16)));
static MimeNetPoll g_poll __attribute__((aligned(16)));
static ULONG g_events[AFD_EVENTS_BATCH] __attribute__((aligned(16)));

static void lock(void)
{
    KeWaitForSingleObject(&g_lock, Executive, KernelMode, FALSE, NULL);
}

static void unlock(void)
{
    KeReleaseMutex(&g_lock, FALSE);
}

/* a host call; 0 or the winsock error. */
static LONG host(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 *value)
{
    ULONG64 v = 0;
    ULONG64 hs = HalHypercall(nr, a1, a2, a3, a4, a5, 0, &v);
    if (value)
        *value = v;
    if (hs == MIME_HC_STATUS_OK)
        return 0;
    if (hs == MIME_HC_STATUS_NET_ERROR)
        return (LONG)v;
    return hs == MIME_HC_STATUS_UNSUPPORTED ? 10045 : WSAEINVAL;
}

static ULONG64 pa(const void *p)
{
    return HalPhysicalOf(p);
}

/* ------------------------------------------------------------ requests */

static NTSTATUS attach_copy(PEPROCESS process, PVOID dst, const void *src, SIZE_T length, BOOLEAN to_user)
{
    if (!length)
        return STATUS_SUCCESS;
    if (process == PsCurrentProcess())
        return to_user ? MimeCopyToUser(dst, src, length) : MimeCopyFromUser(dst, src, length);
    PKPROCESS state;
    KeStackAttachProcess(&process->Pcb, &state);
    NTSTATUS status = to_user ? MimeCopyToUser(dst, src, length) : MimeCopyFromUser(dst, src, length);
    KeUnstackDetachProcess(&state);
    return status;
}

static AFD_REQUEST *new_request(PFILE_OBJECT file, ULONG kind, ULONG count, HANDLE event, PVOID apc_routine,
                                PVOID apc_context, PIO_STATUS_BLOCK iosb, NTSTATUS *status)
{
    AFD_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r) + count * sizeof(MIME_AFD_BUFFER),
                                     KI_TAG('A', 'f', 'd', 'R'));
    if (!r) {
        *status = STATUS_INSUFFICIENT_RESOURCES;
        return NULL;
    }
    memset(r, 0, sizeof(*r));
    if (event) {
        *status = ObReferenceHandle(event, ExEventType, KeGetPreviousMode(), (PVOID *)&r->Event, NULL);
        if (!NT_SUCCESS(*status)) {
            ExFreePool(r);
            return NULL;
        }
        KeClearEvent(r->Event);
    }
    r->Kind = kind;
    r->Count = count;
    r->Process = PsCurrentProcess();
    ObReferenceObject(r->Process);
    r->Thread = PsCurrentThread();
    ObReferenceObject(r->Thread);
    r->ApcRoutine = apc_routine;
    r->File = file;
    ObReferenceObject(file);
    r->ApcContext = apc_context;
    r->Iosb = iosb;
    *status = STATUS_SUCCESS;
    return r;
}

static void free_request(AFD_REQUEST *r)
{
    if (r->Event)
        ObDereferenceObject(r->Event);
    if (r->Accept)
        ObDereferenceObject(r->Accept);
    ObDereferenceObject(r->File);
    ObDereferenceObject(r->Thread);
    ObDereferenceObject(r->Process);
    ExFreePool(r);
}

/* reports the result: status block, event, then the apc or the completion port, which an error that
   ended the request before it went pending skips, like windows. */
static void finish_request(AFD_REQUEST *r)
{
    IO_STATUS_BLOCK io;
    io.Status = r->Status;
    io.Information = r->Information;
    if (r->Iosb)
        attach_copy(r->Process, r->Iosb, &io, sizeof(io), TRUE);
    if (r->Event)
        KeSetEvent(r->Event, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&r->File->Event, IO_NO_INCREMENT, FALSE);
    if (r->Pending || !NT_ERROR(r->Status)) {
        if (r->ApcRoutine)
            IopQueueCompletionApc(r->Thread, r->ApcRoutine, r->ApcContext, r->Iosb);
        else
            IopPostCompletion(r->File, r->ApcContext, r->Status, r->Information);
    }
}

static void complete_request(AFD_REQUEST *r)
{
    finish_request(r);
    free_request(r);
}

static NTSTATUS immediate(AFD_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Status = status;
    r->Information = information;
    complete_request(r);
    return status;
}

static ULONG done(AFD_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Status = status;
    r->Information = information;
    return AFD_DONE;
}

static void fail_list(LIST_ENTRY *list, NTSTATUS status)
{
    while (!IsListEmpty(list)) {
        AFD_REQUEST *r = CONTAINING_RECORD(RemoveHeadList(list), AFD_REQUEST, Links);
        r->Status = status;
        r->Information = 0;
        complete_request(r);
    }
}

/* copies between the bounce buffer and the request's buffers, from a byte offset. */
static NTSTATUS move_data(AFD_REQUEST *r, ULONG offset, PUCHAR data, ULONG length, BOOLEAN to_user)
{
    for (ULONG i = 0; i < r->Count && length; i++) {
        ULONG len = r->Buffers[i].Length;
        if (offset >= len) {
            offset -= len;
            continue;
        }
        ULONG n = len - offset < length ? len - offset : length;
        PUCHAR user = (PUCHAR)(ULONG_PTR)r->Buffers[i].Data + offset;
        NTSTATUS status = to_user ? attach_copy(r->Process, user, data, n, TRUE)
                                  : attach_copy(r->Process, data, user, n, FALSE);
        if (!NT_SUCCESS(status))
            return status;
        data += n;
        length -= n;
        offset = 0;
    }
    return STATUS_SUCCESS;
}

static ULONG address_size(const MIME_AFD_ADDRESS *a)
{
    return a->Family == 23 ? 28 : 16;
}

/* ------------------------------------------------------------ the socket state */

static AFD_SOCKET *find_host(ULONG id)
{
    for (PLIST_ENTRY e = g_sockets.Flink; e != &g_sockets; e = e->Flink) {
        AFD_SOCKET *s = CONTAINING_RECORD(e, AFD_SOCKET, Links);
        if (s->Host == id)
            return s;
    }
    return NULL;
}

static ULONG64 now(void)
{
    return KeQueryInterruptTime();
}

static void connect_done(AFD_SOCKET *s, LONG error)
{
    s->ConnectReport = TRUE;
    s->ConnectError = error;
    if (error) {
        s->State = MIME_AFD_STATE_OPEN;
        s->ConnectFailed = TRUE;
        s->SoError = error;
        fail_list(&s->Connects, MimeAfdStatus(error));
        return;
    }
    s->State = MIME_AFD_STATE_CONNECTED;
    s->ConnectTime = now();
    /* ConnectEx data goes out first, as a send that reports the connect. */
    while (!IsListEmpty(&s->Connects)) {
        AFD_REQUEST *r = CONTAINING_RECORD(RemoveTailList(&s->Connects), AFD_REQUEST, Links);
        if (r->Total) {
            r->Kind = AFD_SEND;
            InsertHeadList(&s->Senders, &r->Links);
        } else {
            r->Status = STATUS_SUCCESS;
            r->Information = 0;
            complete_request(r);
        }
    }
}

/* takes in what the host saw; its error is gone from the host socket once read. */
static void observe(AFD_SOCKET *s, ULONG revents, LONG error)
{
    if (revents & MIME_NET_INVALID)
        return;
    if (s->State == MIME_AFD_STATE_CONNECTING) {
        if (revents & MIME_NET_ERROR)
            connect_done(s, error);
        else if (revents & MIME_NET_HUP)
            connect_done(s, WSAECONNREFUSED);
        else if (revents & MIME_NET_WRITE)
            connect_done(s, 0);
        return;
    }
    if (s->Type == SOCK_STREAM && s->State == MIME_AFD_STATE_CONNECTED) {
        if ((revents & MIME_NET_ERROR) && !s->Broken) {
            s->Broken = TRUE;
            s->BrokenError = error;
            s->SoError = error;
        }
        if (revents & MIME_NET_HUP)
            s->Hup = TRUE;
    } else if (revents & MIME_NET_ERROR) {
        s->SoError = error;
    }
}

/* the WSAEventSelect conditions that hold, with their errors. */
static ULONG conditions(AFD_SOCKET *s, ULONG revents, LONG *errors)
{
    ULONG c = 0;
    if (s->Type == SOCK_STREAM) {
        if (s->State == MIME_AFD_STATE_LISTENING && (revents & MIME_NET_READ))
            c |= FD_ACCEPT;
        if (s->State == MIME_AFD_STATE_CONNECTED) {
            if (revents & MIME_NET_READ)
                c |= FD_READ;
            if ((revents & MIME_NET_WRITE) && !s->ShutWrite && !s->Broken)
                c |= FD_WRITE;
            if ((s->Hup || s->Broken) && !(revents & MIME_NET_READ)) {
                c |= FD_CLOSE;
                errors[FD_CLOSE_BIT] = s->Broken ? s->BrokenError : 0;
            }
        }
        if (s->ConnectReport) {
            c |= FD_CONNECT;
            errors[FD_CONNECT_BIT] = s->ConnectError;
        }
    } else {
        if (revents & MIME_NET_READ)
            c |= FD_READ;
        if (revents & MIME_NET_WRITE)
            c |= FD_WRITE;
    }
    return c;
}

static void signal_select(AFD_SOCKET *s, ULONG revents)
{
    if (!s->SelectMask || !s->SelectEvent)
        return;
    LONG errors[10] = {0};
    ULONG fire = conditions(s, revents, errors) & s->SelectMask & s->Enabled;
    if (s->CloseReported)
        fire &= ~FD_CLOSE;
    if (!fire)
        return;
    s->Enabled &= ~fire;
    if (fire & FD_CONNECT)
        s->ConnectReport = FALSE;
    if (fire & FD_CLOSE)
        s->CloseReported = TRUE;
    s->Recorded |= fire;
    for (int i = 0; i < 10; i++) {
        if (fire & (1u << i))
            s->Errors[i] = errors[i];
    }
    KeSetEvent(s->SelectEvent, IO_NO_INCREMENT, FALSE);
}

/* asks the host what the socket is ready for; arm also watches the events asked. */
static ULONG poll_socket(AFD_SOCKET *s, ULONG events, BOOLEAN arm)
{
    g_poll.socket = s->Host;
    g_poll.events = (uint16_t)events;
    g_poll.revents = 0;
    g_poll.error = 0;
    ULONG64 ready;
    if (host(MIME_HC_NET_POLL, pa(&g_poll), 1, arm, 0, 0, &ready))
        return MIME_NET_INVALID;
    ULONG revents = g_poll.revents;
    observe(s, revents, g_poll.error);
    signal_select(s, revents);
    return revents;
}

/* ------------------------------------------------------------ operations */

static ULONG try_recv(AFD_SOCKET *s, AFD_REQUEST *r)
{
    BOOLEAN stream = s->Type == SOCK_STREAM;
    for (;;) {
        if (stream && s->Broken)
            return done(r, MimeAfdStatus(s->BrokenError), 0);
        if (!r->Total) {
            /* a zero byte receive completes once the socket is readable. */
            ULONG rev = poll_socket(s, 0, FALSE);
            if (stream && s->Broken)
                return done(r, MimeAfdStatus(s->BrokenError), 0);
            if (!(rev & (MIME_NET_READ | MIME_NET_HUP | MIME_NET_ERROR)) && !s->Hup)
                return AFD_BLOCKED;
            return done(r, STATUS_SUCCESS, 0);
        }
        ULONG left = stream ? r->Total - r->Transferred : r->Total;
        ULONG chunk = left < AFD_CHUNK ? left : AFD_CHUNK;
        BOOLEAN want_from = r->From || !stream;
        ULONG64 got = 0;
        LONG e = host(MIME_HC_NET_RECV, s->Host, pa(g_bounce), chunk, r->Flags & (MSG_OOB | MSG_PEEK),
                      want_from ? pa(&g_addr) : 0, &got);
        NTSTATUS status = STATUS_SUCCESS;
        if (e == WSAEWOULDBLOCK)
            return AFD_BLOCKED;
        if (e == WSAEMSGSIZE && !stream) {
            got = chunk;
            status = STATUS_BUFFER_OVERFLOW;
        } else if (e) {
            if (stream && !s->Broken) {
                s->Broken = TRUE;
                s->BrokenError = e;
            }
            return done(r, MimeAfdStatus(e), r->Transferred);
        }
        NTSTATUS copied = move_data(r, stream ? r->Transferred : 0, g_bounce, (ULONG)got, TRUE);
        if (!NT_SUCCESS(copied))
            return done(r, copied, 0);
        if (stream) {
            r->Transferred += (ULONG)got;
            if (!got)
                s->Hup = TRUE;
            if ((r->Flags & MSG_WAITALL) && !(r->Flags & MSG_PEEK) && got && r->Transferred < r->Total)
                continue;
        } else {
            r->Transferred = (ULONG)got;
        }
        if (r->From && r->FromLength) {
            LONG size = (LONG)address_size(&g_addr);
            if (size <= r->FromCapacity) {
                attach_copy(r->Process, r->From, &g_addr, size, TRUE);
                attach_copy(r->Process, r->FromLength, &size, sizeof(size), TRUE);
            }
        }
        return done(r, status, r->Transferred);
    }
}

static ULONG try_send(AFD_SOCKET *s, AFD_REQUEST *r)
{
    BOOLEAN stream = s->Type == SOCK_STREAM;
    if (stream && s->Broken)
        return done(r, MimeAfdStatus(s->BrokenError), r->Transferred);
    if (!stream && r->Total > AFD_CHUNK)
        return done(r, MimeAfdStatus(WSAEMSGSIZE), 0);
    do {
        ULONG left = r->Total - r->Transferred;
        ULONG chunk = left < AFD_CHUNK ? left : AFD_CHUNK;
        NTSTATUS copied = move_data(r, r->Transferred, g_bounce, chunk, FALSE);
        if (!NT_SUCCESS(copied))
            return done(r, copied, r->Transferred);
        if (r->HasTo)
            g_addr = r->To;
        ULONG64 sent = 0;
        LONG e = host(MIME_HC_NET_SEND, s->Host, pa(g_bounce), chunk, r->Flags & (MSG_OOB | MSG_DONTROUTE),
                      r->HasTo ? pa(&g_addr) : 0, &sent);
        if (e == WSAEWOULDBLOCK) {
            s->Enabled |= FD_WRITE;
            return AFD_BLOCKED;
        }
        if (e)
            return done(r, MimeAfdStatus(e), r->Transferred);
        s->Bound = TRUE;
        if (!stream)
            return done(r, STATUS_SUCCESS, sent);
        r->Transferred += (ULONG)sent;
    } while (r->Transferred < r->Total);
    return done(r, STATUS_SUCCESS, r->Transferred);
}

static ULONG try_accept(AFD_SOCKET *s, AFD_REQUEST *r)
{
    AFD_SOCKET *t = r->Accept->FsContext;
    if (!t || t->Closed)
        return done(r, STATUS_AFD_CANCELLED, 0);
    for (;;) {
        ULONG64 id = 0;
        LONG e = host(MIME_HC_NET_ACCEPT, s->Host, pa(&g_addr), 0, 0, 0, &id);
        if (e == WSAEWOULDBLOCK)
            return AFD_BLOCKED;
        /* a connection reset before it was accepted is skipped, like windows does. */
        if (e == WSAECONNABORTED || e == 10054)
            continue;
        if (e)
            return done(r, MimeAfdStatus(e), 0);
        MIME_AFD_ADDRESS remote = g_addr;
        if (t->Host)
            host(MIME_HC_NET_CLOSE, t->Host, 0, 0, 0, 0, NULL);
        t->Host = (ULONG)id;
        t->Family = s->Family;
        t->Type = s->Type;
        t->Protocol = s->Protocol;
        t->State = MIME_AFD_STATE_CONNECTED;
        t->Bound = TRUE;
        t->ConnectTime = now();
        if (r->AcceptAddresses) {
            MIME_AFD_ADDRESS both[2];
            memset(both, 0, sizeof(both));
            if (!host(MIME_HC_NET_NAME, t->Host, 0, pa(&g_addr), 0, 0, NULL))
                both[0] = g_addr;
            both[1] = remote;
            attach_copy(r->Process, r->AcceptAddresses, both, sizeof(both), TRUE);
        }
        if (!r->Total)
            return done(r, STATUS_SUCCESS, 0);
        /* AcceptEx data: the request waits on the new connection for its first bytes. */
        r->Kind = AFD_RECV;
        InsertTailList(&t->Receivers, &r->Links);
        return AFD_MOVED;
    }
}

static ULONG try_request(AFD_SOCKET *s, AFD_REQUEST *r)
{
    switch (r->Kind) {
    case AFD_RECV:
        return try_recv(s, r);
    case AFD_SEND:
        return try_send(s, r);
    case AFD_ACCEPT:
        return try_accept(s, r);
    }
    return AFD_BLOCKED;
}

static BOOLEAN run_list(AFD_SOCKET *s, LIST_ENTRY *list, AFD_SOCKET **moved)
{
    BOOLEAN progress = FALSE;
    while (!IsListEmpty(list)) {
        AFD_REQUEST *r = CONTAINING_RECORD(RemoveHeadList(list), AFD_REQUEST, Links);
        ULONG result = try_request(s, r);
        if (result == AFD_BLOCKED) {
            InsertHeadList(list, &r->Links);
            break;
        }
        progress = TRUE;
        if (result == AFD_DONE)
            complete_request(r);
        else if (moved)
            *moved = r->Accept->FsContext;
    }
    return progress;
}

/* the host events the requests and the event selection wait for. */
static ULONG wants(AFD_SOCKET *s)
{
    ULONG w = 0;
    if (!IsListEmpty(&s->Receivers) || !IsListEmpty(&s->Accepters))
        w |= MIME_NET_READ;
    if (!IsListEmpty(&s->Senders) || s->State == MIME_AFD_STATE_CONNECTING)
        w |= MIME_NET_WRITE;
    ULONG sel = s->SelectEvent ? s->SelectMask & s->Enabled : 0;
    BOOLEAN ended = s->Hup || s->Broken;
    if ((sel & (FD_READ | FD_OOB | FD_ACCEPT)) && !ended)
        w |= MIME_NET_READ;
    if ((sel & FD_CLOSE) && !s->CloseReported && s->Type == SOCK_STREAM && s->State == MIME_AFD_STATE_CONNECTED &&
        !ended)
        w |= MIME_NET_READ;
    if ((sel & FD_WRITE) && (s->State == MIME_AFD_STATE_CONNECTED || s->Type == SOCK_DGRAM))
        w |= MIME_NET_WRITE;
    return w;
}

/* retries what waits on a socket until nothing moves, then arms the host watches. */
static void service(AFD_SOCKET *s)
{
    for (int round = 0; round < 16 && s->Host && !s->Closed; round++) {
        AFD_SOCKET *moved = NULL;
        BOOLEAN progress = run_list(s, &s->Accepters, &moved);
        progress |= run_list(s, &s->Receivers, NULL);
        progress |= run_list(s, &s->Senders, NULL);
        if (moved && moved != s)
            service(moved);
        BOOLEAN connecting = s->State == MIME_AFD_STATE_CONNECTING;
        ULONG want = wants(s);
        if (!want && !(s->SelectEvent && s->SelectMask))
            return;
        ULONG rev = poll_socket(s, want, FALSE);
        if (connecting && s->State != MIME_AFD_STATE_CONNECTING)
            progress = TRUE;
        if (!progress && round)
            break;
        BOOLEAN ready = ((want & MIME_NET_READ) && (rev & (MIME_NET_READ | MIME_NET_HUP | MIME_NET_ERROR))) ||
                        ((want & MIME_NET_WRITE) && (rev & (MIME_NET_WRITE | MIME_NET_ERROR)));
        if (!ready && !progress)
            break;
    }
    ULONG want = wants(s);
    if (want && s->Host && !s->Closed)
        poll_socket(s, want, TRUE);
}

static void wake_pollers(void)
{
    for (PLIST_ENTRY e = g_pollers.Flink; e != &g_pollers; e = e->Flink)
        KeSetEvent(&CONTAINING_RECORD(e, AFD_POLLER, Links)->Wake, IO_NO_INCREMENT, FALSE);
}

static void NTAPI worker(PVOID context)
{
    for (;;) {
        KeWaitForSingleObject(&g_work, Executive, KernelMode, FALSE, NULL);
        lock();
        for (;;) {
            ULONG64 n = 0;
            if (host(MIME_HC_NET_EVENTS, pa(g_events), AFD_EVENTS_BATCH, 0, 0, 0, &n) || !n)
                break;
            for (ULONG i = 0; i < n; i++) {
                ULONG id = g_events[i];
                if (id & MIME_NET_RESOLVED) {
                    for (PLIST_ENTRY e = g_resolvers.Flink; e != &g_resolvers; e = e->Flink) {
                        AFD_RESOLVER *w = CONTAINING_RECORD(e, AFD_RESOLVER, Links);
                        if (w->Id == (id & ~MIME_NET_RESOLVED)) {
                            w->Done = TRUE;
                            KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
                        }
                    }
                    continue;
                }
                AFD_SOCKET *s = find_host(id);
                if (s)
                    service(s);
            }
            if (n < AFD_EVENTS_BATCH)
                break;
        }
        wake_pollers();
        unlock();
    }
}

/* interrupt level, from the session dpc. */
void AfdInterrupt(void)
{
    KeSetEvent(&g_work, IO_NO_INCREMENT, FALSE);
}

/* ------------------------------------------------------------ device controls */

static NTSTATUS capture(PVOID dst, PVOID src, ULONG length, ULONG in_len)
{
    if (in_len < length || !src)
        return STATUS_INVALID_PARAMETER;
    return KiCopyFromUser(dst, src, length);
}

static NTSTATUS check_address(const MIME_AFD_ADDRESS *a, const AFD_SOCKET *s)
{
    if (a->Family != 2 && a->Family != 23)
        return MimeAfdStatus(10047); /* WSAEAFNOSUPPORT */
    if (a->Family != (USHORT)s->Family)
        return MimeAfdStatus(10047);
    return STATUS_SUCCESS;
}

/* starts a request: completes it now when it can, otherwise queues it or fails a non blocking one. */
static NTSTATUS start_request(AFD_SOCKET *s, AFD_REQUEST *r, LIST_ENTRY *list, BOOLEAN may_pend)
{
    ULONG result = IsListEmpty(list) ? try_request(s, r) : AFD_BLOCKED;
    if (result == AFD_DONE) {
        if (s->SelectMask)
            service(s);
        unlock();
        return immediate(r, r->Status, r->Information);
    }
    if (result == AFD_MOVED) {
        AFD_SOCKET *t = r->Accept->FsContext;
        r->Pending = TRUE;
        service(s);
        service(t);
        unlock();
        return STATUS_PENDING;
    }
    if (!may_pend) {
        service(s);
        unlock();
        return immediate(r, STATUS_AFD_WOULD_BLOCK, 0);
    }
    r->Pending = TRUE;
    InsertTailList(list, &r->Links);
    service(s);
    unlock();
    return STATUS_PENDING;
}

static NTSTATUS capture_buffers(AFD_REQUEST *r, ULONG64 buffers)
{
    if (r->Count && !buffers)
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status = KiCopyFromUser(r->Buffers, (PVOID)(ULONG_PTR)buffers, r->Count * sizeof(MIME_AFD_BUFFER));
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 total = 0;
    for (ULONG i = 0; i < r->Count; i++)
        total += r->Buffers[i].Length;
    if (total > 0x7FFFFFFF)
        return STATUS_INVALID_PARAMETER;
    r->Total = (ULONG)total;
    return STATUS_SUCCESS;
}

static NTSTATUS transfer(PFILE_OBJECT file, AFD_SOCKET *s, BOOLEAN send, HANDLE event, PVOID apc, PVOID context,
                         PIO_STATUS_BLOCK iosb, PVOID input, ULONG in_len)
{
    MIME_AFD_TRANSFER t;
    NTSTATUS status = capture(&t, input, sizeof(t), in_len);
    if (!NT_SUCCESS(status))
        return status;
    if (t.BufferCount > AFD_MAX_BUFFERS)
        return MimeAfdStatus(WSAENOBUFS);
    AFD_REQUEST *r = new_request(file, send ? AFD_SEND : AFD_RECV, t.BufferCount, event, apc, context, iosb, &status);
    if (!r)
        return status;
    status = capture_buffers(r, t.Buffers);
    if (!NT_SUCCESS(status))
        return immediate(r, status, 0);
    r->Flags = t.Flags;
    /* a connected stream ignores the destination, like windows. */
    if (send && t.Address && s->Type == SOCK_DGRAM) {
        status = KiCopyFromUser(&r->To, (PVOID)(ULONG_PTR)t.Address, sizeof(r->To));
        if (!NT_SUCCESS(status))
            return immediate(r, status, 0);
        r->HasTo = TRUE;
    }
    if (!send && t.Address && t.AddressLength) {
        LONG capacity;
        status = KiCopyFromUser(&capacity, (PVOID)(ULONG_PTR)t.AddressLength, sizeof(capacity));
        if (!NT_SUCCESS(status))
            return immediate(r, status, 0);
        r->From = (PVOID)(ULONG_PTR)t.Address;
        r->FromLength = (PVOID)(ULONG_PTR)t.AddressLength;
        r->FromCapacity = capacity;
    }
    lock();
    if (!s->Host || s->Closed) {
        unlock();
        return immediate(r, MimeAfdStatus(WSAENOTSOCK), 0);
    }
    if (r->HasTo && s->Type == SOCK_DGRAM && !NT_SUCCESS(status = check_address(&r->To, s))) {
        unlock();
        return immediate(r, status, 0);
    }
    if (s->Type == SOCK_STREAM && s->State != MIME_AFD_STATE_CONNECTED) {
        unlock();
        return immediate(r, MimeAfdStatus(WSAENOTCONN), 0);
    }
    if (send ? s->ShutWrite : s->ShutRead) {
        unlock();
        return immediate(r, MimeAfdStatus(WSAESHUTDOWN), 0);
    }
    if (!send && s->Type == SOCK_DGRAM && !s->Bound) {
        unlock();
        return immediate(r, MimeAfdStatus(WSAEINVAL), 0);
    }
    if (!send)
        s->Enabled |= FD_READ | FD_OOB;
    BOOLEAN may_pend = (t.AfdFlags & MIME_AFD_OVERLAPPED) || !s->NonBlocking;
    return start_request(s, r, send ? &s->Senders : &s->Receivers, may_pend);
}

static NTSTATUS do_connect(PFILE_OBJECT file, AFD_SOCKET *s, HANDLE event, PVOID apc, PVOID context,
                           PIO_STATUS_BLOCK iosb, PVOID input, ULONG in_len)
{
    MIME_AFD_CONNECT c;
    NTSTATUS status = capture(&c, input, sizeof(c), in_len);
    if (!NT_SUCCESS(status))
        return status;
    AFD_REQUEST *r = new_request(file, AFD_CONNECT, c.Length ? 1 : 0, event, apc, context, iosb, &status);
    if (!r)
        return status;
    if (c.Length) {
        r->Buffers[0].Length = c.Length;
        r->Buffers[0].Data = c.Buffer;
        r->Total = c.Length;
    }
    lock();
    if (!s->Host || s->Closed)
        status = MimeAfdStatus(WSAENOTSOCK);
    else
        status = check_address(&c.Address, s);
    if (!NT_SUCCESS(status))
        ;
    else if (s->State == MIME_AFD_STATE_CONNECTED)
        status = MimeAfdStatus(WSAEISCONN);
    else if (s->State == MIME_AFD_STATE_CONNECTING)
        status = MimeAfdStatus((c.AfdFlags & MIME_AFD_OVERLAPPED) ? WSAEINVAL : WSAEALREADY);
    else if (s->State == MIME_AFD_STATE_LISTENING)
        status = MimeAfdStatus(WSAEINVAL);
    if (!NT_SUCCESS(status)) {
        unlock();
        return immediate(r, status, 0);
    }
    g_addr = c.Address;
    LONG e = host(MIME_HC_NET_CONNECT, s->Host, pa(&g_addr), 0, 0, 0, NULL);
    if (e && e != WSAEWOULDBLOCK) {
        unlock();
        return immediate(r, MimeAfdStatus(e), 0);
    }
    s->Bound = TRUE;
    s->ConnectFailed = FALSE;
    s->ConnectReport = FALSE;
    s->Hup = s->Broken = FALSE;
    s->Enabled |= FD_CONNECT | FD_WRITE;
    if (!e) {
        connect_done(s, 0);
        if (!r->Total) {
            service(s);
            unlock();
            return immediate(r, STATUS_SUCCESS, 0);
        }
        r->Kind = AFD_SEND;
        return start_request(s, r, &s->Senders, TRUE);
    }
    s->State = MIME_AFD_STATE_CONNECTING;
    if (!(c.AfdFlags & MIME_AFD_OVERLAPPED) && s->NonBlocking) {
        service(s);
        unlock();
        return immediate(r, STATUS_AFD_WOULD_BLOCK, 0);
    }
    r->Pending = TRUE;
    InsertTailList(&s->Connects, &r->Links);
    service(s);
    unlock();
    return STATUS_PENDING;
}

static NTSTATUS do_accept(PFILE_OBJECT file, AFD_SOCKET *s, HANDLE event, PVOID apc, PVOID context,
                          PIO_STATUS_BLOCK iosb, PVOID input, ULONG in_len)
{
    MIME_AFD_ACCEPT a;
    NTSTATUS status = capture(&a, input, sizeof(a), in_len);
    if (!NT_SUCCESS(status))
        return status;
    PFILE_OBJECT target;
    status = ObReferenceHandle((HANDLE)(ULONG_PTR)a.Socket, IoFileObjectTypeObject, KeGetPreviousMode(),
                               (PVOID *)&target, NULL);
    if (!NT_SUCCESS(status))
        return status;
    if (target->DeviceObject != g_device || !target->FsContext) {
        ObDereferenceObject(target);
        return MimeAfdStatus(WSAENOTSOCK);
    }
    AFD_REQUEST *r = new_request(file, AFD_ACCEPT, a.Length ? 1 : 0, event, apc, context, iosb, &status);
    if (!r) {
        ObDereferenceObject(target);
        return status;
    }
    r->Accept = target;
    r->AcceptAddresses = (PVOID)(ULONG_PTR)a.Addresses;
    if (a.Length) {
        r->Buffers[0].Length = a.Length;
        r->Buffers[0].Data = a.Buffer;
        r->Total = a.Length;
    }
    lock();
    AFD_SOCKET *t = target->FsContext;
    if (!s->Host || s->Closed)
        status = MimeAfdStatus(WSAENOTSOCK);
    else if (s->State != MIME_AFD_STATE_LISTENING)
        status = MimeAfdStatus(WSAEINVAL);
    else if (t == s || t->Closed || t->State != MIME_AFD_STATE_OPEN || t->Bound)
        status = MimeAfdStatus(WSAEINVAL);
    if (!NT_SUCCESS(status)) {
        unlock();
        return immediate(r, status, 0);
    }
    s->Enabled |= FD_ACCEPT;
    BOOLEAN may_pend = (a.AfdFlags & MIME_AFD_OVERLAPPED) || !s->NonBlocking;
    return start_request(s, r, &s->Accepters, may_pend);
}

/* select and WSAPoll: what each socket is ready for, waiting until one is or the timeout ends. */
static ULONG poll_result(AFD_SOCKET *s, ULONG revents)
{
    ULONG r = 0;
    if (revents & MIME_NET_INVALID)
        return MIME_AFD_POLL_INVALID;
    if (s->Type == SOCK_DGRAM) {
        if (revents & MIME_NET_READ)
            r |= MIME_AFD_POLL_READ | MIME_AFD_POLL_DATA;
        if (revents & MIME_NET_WRITE)
            r |= MIME_AFD_POLL_WRITE;
        return r;
    }
    switch (s->State) {
    case MIME_AFD_STATE_LISTENING:
        if (revents & MIME_NET_READ)
            r |= MIME_AFD_POLL_READ | MIME_AFD_POLL_DATA;
        break;
    case MIME_AFD_STATE_CONNECTED:
        if (revents & MIME_NET_READ)
            r |= MIME_AFD_POLL_READ | MIME_AFD_POLL_DATA;
        if (s->Hup || s->Broken)
            r |= MIME_AFD_POLL_READ | MIME_AFD_POLL_HUP;
        if (s->Broken)
            r |= MIME_AFD_POLL_ERROR;
        else if ((revents & MIME_NET_WRITE) && !s->ShutWrite)
            r |= MIME_AFD_POLL_WRITE;
        break;
    case MIME_AFD_STATE_OPEN:
        if (s->ConnectFailed)
            r |= MIME_AFD_POLL_EXCEPT | MIME_AFD_POLL_ERROR | MIME_AFD_POLL_HUP;
        break;
    }
    return r;
}

static NTSTATUS do_poll(PVOID input, ULONG in_len, PVOID output, ULONG out_len, ULONG_PTR *information)
{
    MIME_AFD_POLL head;
    NTSTATUS status = capture(&head, input, sizeof(head), in_len);
    if (!NT_SUCCESS(status))
        return status;
    if (head.Count > AFD_MAX_POLL)
        return STATUS_INVALID_PARAMETER;
    ULONG size = sizeof(head) + head.Count * sizeof(MIME_AFD_POLL_ENTRY);
    if (in_len < size || out_len < size)
        return STATUS_INVALID_PARAMETER;
    MIME_AFD_POLL *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('A', 'f', 'd', 'P'));
    MimeNetPoll *hp = ExAllocatePool2(POOL_FLAG_NON_PAGED, (head.Count ? head.Count : 1) * sizeof(MimeNetPoll),
                                      KI_TAG('A', 'f', 'd', 'H'));
    PFILE_OBJECT *files = ExAllocatePool2(POOL_FLAG_NON_PAGED, (head.Count ? head.Count : 1) * sizeof(PFILE_OBJECT),
                                          KI_TAG('A', 'f', 'd', 'F'));
    if (!p || !hp || !files) {
        if (p)
            ExFreePool(p);
        if (hp)
            ExFreePool(hp);
        if (files)
            ExFreePool(files);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = KiCopyFromUser(p, input, size);
    for (ULONG i = 0; i < head.Count; i++) {
        files[i] = NULL;
        PFILE_OBJECT f;
        if (NT_SUCCESS(status) && NT_SUCCESS(ObReferenceHandle((HANDLE)(ULONG_PTR)p->Entries[i].Socket,
                                                               IoFileObjectTypeObject, UserMode, (PVOID *)&f, NULL))) {
            if (f->DeviceObject == g_device && f->FsContext)
                files[i] = f;
            else
                ObDereferenceObject(f);
        }
    }
    AFD_POLLER waiter;
    KeInitializeEvent(&waiter.Wake, NotificationEvent, FALSE);
    LONGLONG deadline = head.Timeout >= 0 ? (LONGLONG)now() + head.Timeout * 10000 : -1;
    BOOLEAN registered = FALSE;
    while (NT_SUCCESS(status)) {
        KeClearEvent(&waiter.Wake);
        lock();
        if (!registered) {
            InsertTailList(&g_pollers, &waiter.Links);
            registered = TRUE;
        }
        ULONG n = 0;
        for (ULONG i = 0; i < head.Count; i++) {
            AFD_SOCKET *s = files[i] ? files[i]->FsContext : NULL;
            hp[n].socket = s && !s->Closed ? s->Host : 0;
            hp[n].events = 0;
            hp[n].revents = 0;
            hp[n].error = 0;
            n++;
        }
        ULONG64 ignored;
        if (n)
            host(MIME_HC_NET_POLL, pa(hp), n, FALSE, 0, 0, &ignored);
        ULONG ready = 0;
        for (ULONG i = 0; i < head.Count; i++) {
            MIME_AFD_POLL_ENTRY *e = &p->Entries[i];
            AFD_SOCKET *s = files[i] ? files[i]->FsContext : NULL;
            if (!s || s->Closed) {
                e->Results = MIME_AFD_POLL_INVALID;
            } else if (!s->Host) {
                e->Results = 0;
            } else {
                observe(s, hp[i].revents, hp[i].error);
                signal_select(s, hp[i].revents);
                e->Results = poll_result(s, hp[i].revents);
            }
            if (e->Results & (e->Events | MIME_AFD_POLL_INVALID))
                ready++;
        }
        p->Ready = ready;
        LONGLONG left = deadline < 0 ? -1 : deadline - (LONGLONG)now();
        if (ready || (deadline >= 0 && left <= 0)) {
            unlock();
            break;
        }
        /* watch every socket for what its caller asked, then sleep until the worker ran. */
        for (ULONG i = 0; i < head.Count; i++) {
            ULONG asked = p->Entries[i].Events;
            hp[i].events = 0;
            if (asked & (MIME_AFD_POLL_READ | MIME_AFD_POLL_HUP | MIME_AFD_POLL_ERROR))
                hp[i].events |= MIME_NET_READ;
            if (asked & (MIME_AFD_POLL_WRITE | MIME_AFD_POLL_EXCEPT | MIME_AFD_POLL_ERROR))
                hp[i].events |= MIME_NET_WRITE;
        }
        if (n)
            host(MIME_HC_NET_POLL, pa(hp), n, TRUE, 0, 0, &ignored);
        unlock();
        LARGE_INTEGER timeout = {.QuadPart = -left};
        NTSTATUS wait = KeWaitForSingleObject(&waiter.Wake, UserRequest, UserMode, FALSE, left < 0 ? NULL : &timeout);
        if (wait != STATUS_SUCCESS && wait != STATUS_TIMEOUT)
            status = wait;
    }
    if (registered) {
        lock();
        RemoveEntryList(&waiter.Links);
        unlock();
    }
    for (ULONG i = 0; i < head.Count; i++) {
        if (files[i])
            ObDereferenceObject(files[i]);
    }
    if (NT_SUCCESS(status)) {
        status = KiCopyToUser(output, p, size);
        *information = size;
    }
    ExFreePool(files);
    ExFreePool(hp);
    ExFreePool(p);
    return status;
}

static NTSTATUS do_resolve(PVOID input, ULONG in_len, PVOID output, ULONG out_len, ULONG_PTR *information)
{
    MIME_AFD_RESOLVE q;
    NTSTATUS status = capture(&q, input, sizeof(q), in_len);
    if (!NT_SUCCESS(status))
        return status;
    if (!q.Length || q.Length >= sizeof(q.Name))
        return STATUS_INVALID_PARAMETER;
    PUCHAR name = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(q.Name), KI_TAG('A', 'f', 'd', 'N'));
    MIME_AFD_ADDRESS *answers =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, AFD_MAX_ANSWERS * sizeof(MIME_AFD_ADDRESS), KI_TAG('A', 'f', 'd', 'A'));
    if (!name || !answers) {
        if (name)
            ExFreePool(name);
        if (answers)
            ExFreePool(answers);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memcpy(name, q.Name, q.Length);
    AFD_RESOLVER waiter;
    KeInitializeEvent(&waiter.Wake, NotificationEvent, FALSE);
    waiter.Done = FALSE;
    lock();
    ULONG64 id = 0;
    LONG e = host(MIME_HC_NET_RESOLVE, pa(name), q.Length, q.Family, 0, 0, &id);
    if (!e) {
        waiter.Id = (ULONG)id;
        InsertTailList(&g_resolvers, &waiter.Links);
    }
    unlock();
    ULONG64 count = 0;
    if (!e) {
        NTSTATUS wait = KeWaitForSingleObject(&waiter.Wake, UserRequest, UserMode, FALSE, NULL);
        lock();
        RemoveEntryList(&waiter.Links);
        if (waiter.Done)
            e = host(MIME_HC_NET_ANSWER, waiter.Id, pa(answers), AFD_MAX_ANSWERS, 0, 0, &count);
        unlock();
        if (!waiter.Done)
            status = wait;
    }
    if (NT_SUCCESS(status)) {
        if (e) {
            status = MimeAfdStatus(e);
        } else {
            ULONG bytes = (ULONG)count * sizeof(MIME_AFD_ADDRESS);
            if (bytes > out_len)
                bytes = out_len / sizeof(MIME_AFD_ADDRESS) * sizeof(MIME_AFD_ADDRESS);
            status = KiCopyToUser(output, answers, bytes);
            *information = bytes;
        }
    }
    ExFreePool(answers);
    ExFreePool(name);
    return status;
}

static NTSTATUS do_interfaces(PVOID output, ULONG out_len, ULONG_PTR *information)
{
    MIME_AFD_INTERFACE *list =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, AFD_MAX_INTERFACES * sizeof(MIME_AFD_INTERFACE), KI_TAG('A', 'f', 'd', 'I'));
    if (!list)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG64 count = 0;
    lock();
    LONG e = host(MIME_HC_NET_INTERFACES, pa(list), AFD_MAX_INTERFACES, 0, 0, 0, &count);
    unlock();
    NTSTATUS status = MimeAfdStatus(e);
    if (!e) {
        ULONG bytes = (ULONG)count * sizeof(MIME_AFD_INTERFACE);
        if (bytes > out_len)
            bytes = out_len / sizeof(MIME_AFD_INTERFACE) * sizeof(MIME_AFD_INTERFACE);
        status = KiCopyToUser(output, list, bytes);
        *information = bytes;
    }
    ExFreePool(list);
    return status;
}

/* the requests that answer at once, under the lock. */
static NTSTATUS control(AFD_SOCKET *s, ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len,
                        ULONG_PTR *information)
{
    UCHAR in[64];
    NTSTATUS status = STATUS_SUCCESS;
    if (in_len > sizeof(in))
        in_len = sizeof(in);
    if (in_len && !NT_SUCCESS(status = KiCopyFromUser(in, input, in_len)))
        return status;
    LONG e = 0;
    lock();
    if (code == IOCTL_AFD_CREATE) {
        MIME_AFD_CREATE *c = (MIME_AFD_CREATE *)in;
        if (in_len < sizeof(*c)) {
            status = STATUS_INVALID_PARAMETER;
        } else if (s->Host) {
            e = WSAEINVAL;
        } else {
            ULONG64 id;
            e = host(MIME_HC_NET_SOCKET, c->Family, c->Type, c->Protocol, 0, 0, &id);
            if (!e) {
                s->Host = (ULONG)id;
                s->Family = c->Family;
                s->Type = c->Type;
                s->Protocol = c->Protocol;
            }
        }
        goto out;
    }
    if (!s->Host || s->Closed) {
        e = WSAENOTSOCK;
        goto out;
    }
    switch (code) {
    case IOCTL_AFD_BIND: {
        MIME_AFD_ADDRESS *a = (MIME_AFD_ADDRESS *)in;
        if (in_len < sizeof(*a)) {
            e = WSAEFAULT;
        } else if (s->Bound) {
            e = WSAEINVAL;
        } else if (!NT_SUCCESS(status = check_address(a, s))) {
            break;
        } else {
            g_addr = *a;
            e = host(MIME_HC_NET_BIND, s->Host, pa(&g_addr), 0, 0, 0, NULL);
            if (!e)
                s->Bound = TRUE;
        }
        break;
    }
    case IOCTL_AFD_LISTEN:
        if (in_len < sizeof(ULONG))
            e = WSAEFAULT;
        else if (s->Type != SOCK_STREAM)
            e = 10045; /* WSAEOPNOTSUPP */
        else if (!s->Bound)
            e = WSAEINVAL;
        else if (s->State == MIME_AFD_STATE_CONNECTED || s->State == MIME_AFD_STATE_CONNECTING)
            e = WSAEISCONN;
        else if (!(e = host(MIME_HC_NET_LISTEN, s->Host, *(PULONG)in, 0, 0, 0, NULL)))
            s->State = MIME_AFD_STATE_LISTENING;
        break;
    case IOCTL_AFD_SHUTDOWN: {
        ULONG how = in_len >= sizeof(ULONG) ? *(PULONG)in : 3;
        if (how > 2) {
            e = WSAEINVAL;
        } else if (s->Type == SOCK_STREAM && s->State != MIME_AFD_STATE_CONNECTED) {
            e = WSAENOTCONN;
        } else {
            e = host(MIME_HC_NET_SHUTDOWN, s->Host, how, 0, 0, 0, NULL);
            if (e == WSAENOTCONN && s->Type == SOCK_DGRAM)
                e = 0;
            if (!e) {
                if (how != 1)
                    s->ShutRead = TRUE;
                if (how != 0)
                    s->ShutWrite = TRUE;
            }
        }
        break;
    }
    case IOCTL_AFD_NAME: {
        BOOLEAN peer = in_len >= sizeof(ULONG) && *(PULONG)in;
        if (out_len < sizeof(MIME_AFD_ADDRESS)) {
            e = WSAEFAULT;
        } else if (peer && s->Type == SOCK_STREAM && s->State != MIME_AFD_STATE_CONNECTED) {
            e = WSAENOTCONN;
        } else if (!peer && !s->Bound) {
            e = WSAEINVAL;
        } else if (!(e = host(MIME_HC_NET_NAME, s->Host, peer, pa(&g_addr), 0, 0, NULL))) {
            MIME_AFD_ADDRESS a = g_addr;
            unlock();
            status = KiCopyToUser(output, &a, sizeof(a));
            *information = sizeof(a);
            return status;
        }
        break;
    }
    case IOCTL_AFD_OPTION: {
        MIME_AFD_OPTION *o = (MIME_AFD_OPTION *)in;
        ULONG value = 0;
        if (in_len < sizeof(*o)) {
            e = WSAEFAULT;
            break;
        }
        if (o->Option == MIME_AFD_OPTION_NONBLOCKING) {
            if (o->Set && !o->Value && s->SelectMask)
                e = WSAEINVAL;
            else if (o->Set)
                s->NonBlocking = o->Value != 0;
            value = s->NonBlocking;
        } else if (o->Option == MIME_NET_OPT_ERROR && !o->Set && s->SoError) {
            value = (ULONG)s->SoError;
            s->SoError = 0;
        } else {
            ULONG64 v = 0;
            e = host(MIME_HC_NET_OPTION, s->Host, o->Option, o->Set, o->Value, 0, &v);
            value = (ULONG)v;
        }
        if (!e && out_len >= sizeof(ULONG)) {
            unlock();
            status = KiCopyToUser(output, &value, sizeof(value));
            *information = sizeof(value);
            return status;
        }
        break;
    }
    case IOCTL_AFD_EVENT_SELECT: {
        MIME_AFD_EVENT_SELECT *sel = (MIME_AFD_EVENT_SELECT *)in;
        if (in_len < sizeof(*sel)) {
            e = WSAEFAULT;
            break;
        }
        PKEVENT event = NULL;
        if (sel->Mask && sel->Event) {
            status = ObReferenceHandle((HANDLE)(ULONG_PTR)sel->Event, ExEventType, UserMode, (PVOID *)&event, NULL);
            if (!NT_SUCCESS(status))
                break;
        }
        if (s->SelectEvent)
            ObDereferenceObject(s->SelectEvent);
        s->SelectEvent = event;
        s->SelectMask = event ? sel->Mask : 0;
        s->Enabled = FD_ALL;
        s->Recorded = 0;
        if (event)
            s->NonBlocking = TRUE;
        service(s);
        break;
    }
    case IOCTL_AFD_ENUM_EVENTS: {
        if (out_len < sizeof(MIME_AFD_EVENTS)) {
            e = WSAEFAULT;
            break;
        }
        MIME_AFD_EVENTS ev;
        memset(&ev, 0, sizeof(ev));
        ev.Events = s->Recorded;
        for (int i = 0; i < 10; i++)
            ev.Errors[i] = (s->Recorded & (1u << i)) ? s->Errors[i] : 0;
        s->Recorded = 0;
        unlock();
        if (in_len >= sizeof(ULONG64) && *(ULONG64 *)in) {
            PKEVENT event;
            if (NT_SUCCESS(ObReferenceHandle((HANDLE)(ULONG_PTR) * (ULONG64 *)in, ExEventType, UserMode,
                                             (PVOID *)&event, NULL))) {
                KeClearEvent(event);
                ObDereferenceObject(event);
            }
        }
        status = KiCopyToUser(output, &ev, sizeof(ev));
        *information = sizeof(ev);
        return status;
    }
    case IOCTL_AFD_STATE: {
        if (out_len < sizeof(MIME_AFD_STATE)) {
            e = WSAEFAULT;
            break;
        }
        MIME_AFD_STATE st;
        memset(&st, 0, sizeof(st));
        st.Family = s->Family;
        st.Type = s->Type;
        st.Protocol = s->Protocol;
        st.State = s->State;
        st.Bound = s->Bound;
        st.NonBlocking = s->NonBlocking;
        st.SelectMask = s->SelectMask;
        st.ShutFlags = (s->ShutRead ? 1 : 0) | (s->ShutWrite ? 2 : 0);
        st.ConnectTime = s->ConnectTime;
        unlock();
        status = KiCopyToUser(output, &st, sizeof(st));
        *information = sizeof(st);
        return status;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
out:
    unlock();
    if (e)
        return MimeAfdStatus(e);
    return status;
}

/* NtDeviceIoControlFile of a socket file. */
NTSTATUS AfdDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                          ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    AFD_SOCKET *s = file->FsContext;
    if (!s)
        return STATUS_INVALID_HANDLE;
    switch (code) {
    case IOCTL_AFD_SEND:
    case IOCTL_AFD_RECV:
        return transfer(file, s, code == IOCTL_AFD_SEND, event, apc, context, iosb, input, in_len);
    case IOCTL_AFD_CONNECT:
        return do_connect(file, s, event, apc, context, iosb, input, in_len);
    case IOCTL_AFD_ACCEPT:
        return do_accept(file, s, event, apc, context, iosb, input, in_len);
    }
    /* the rest answers at once and completes like a synchronous request. */
    NTSTATUS status;
    AFD_REQUEST *r = new_request(file, AFD_RECV, 0, event, apc, context, iosb, &status);
    if (!r)
        return status;
    ULONG_PTR information = 0;
    switch (code) {
    case IOCTL_AFD_POLL:
        status = do_poll(input, in_len, output, out_len, &information);
        break;
    case IOCTL_AFD_RESOLVE:
        status = do_resolve(input, in_len, output, out_len, &information);
        break;
    case IOCTL_AFD_INTERFACES:
        status = do_interfaces(output, out_len, &information);
        break;
    case IOCTL_AFD_COMPLETE: {
        ULONG64 value = 0;
        status = capture(&value, input, sizeof(value), in_len);
        information = (ULONG_PTR)value;
        break;
    }
    default:
        status = control(s, code, input, in_len, output, out_len, &information);
        break;
    }
    return immediate(r, status, information);
}

/* NtReadFile and NtWriteFile of a socket: a receive or a send without flags. */
NTSTATUS AfdReadWrite(PFILE_OBJECT file, BOOLEAN write, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                      PVOID buffer, ULONG length)
{
    AFD_SOCKET *s = file->FsContext;
    if (!s)
        return STATUS_INVALID_HANDLE;
    MIME_AFD_BUFFER b = {length, 0, (ULONG64)(ULONG_PTR)buffer};
    MIME_AFD_TRANSFER t;
    memset(&t, 0, sizeof(t));
    t.BufferCount = 1;
    t.AfdFlags = MIME_AFD_OVERLAPPED;
    NTSTATUS status;
    AFD_REQUEST *r = new_request(file, write ? AFD_SEND : AFD_RECV, 1, event, apc, context, iosb, &status);
    if (!r)
        return status;
    r->Buffers[0] = b;
    r->Total = length;
    lock();
    if (!s->Host || s->Closed || (s->Type == SOCK_STREAM && s->State != MIME_AFD_STATE_CONNECTED)) {
        unlock();
        return immediate(r, MimeAfdStatus(s->Host ? WSAENOTCONN : WSAENOTSOCK), 0);
    }
    if (!write)
        s->Enabled |= FD_READ | FD_OOB;
    return start_request(s, r, write ? &s->Senders : &s->Receivers, TRUE);
}

static BOOLEAN cancel_list(LIST_ENTRY *list, PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    BOOLEAN found = FALSE;
    PLIST_ENTRY e = list->Flink;
    while (e != list) {
        AFD_REQUEST *r = CONTAINING_RECORD(e, AFD_REQUEST, Links);
        e = e->Flink;
        if (r->File != file || r->Process != PsCurrentProcess() || (thread && r->Thread != thread) ||
            (iosb && r->Iosb != iosb))
            continue;
        RemoveEntryList(&r->Links);
        r->Status = STATUS_AFD_CANCELLED;
        r->Information = 0;
        complete_request(r);
        found = TRUE;
    }
    return found;
}

/* CancelIo with the thread, CancelIoEx with the status block of one request or neither. */
NTSTATUS AfdCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    AFD_SOCKET *s = file->FsContext;
    BOOLEAN found = FALSE;
    lock();
    if (s) {
        found |= cancel_list(&s->Receivers, file, thread, iosb);
        found |= cancel_list(&s->Senders, file, thread, iosb);
        found |= cancel_list(&s->Accepters, file, thread, iosb);
        if (cancel_list(&s->Connects, file, thread, iosb)) {
            found = TRUE;
            if (IsListEmpty(&s->Connects) && s->State == MIME_AFD_STATE_CONNECTING) {
                /* a cancelled ConnectEx leaves a socket that can only be closed. */
                host(MIME_HC_NET_SHUTDOWN, s->Host, 2, 0, 0, 0, NULL);
                s->State = MIME_AFD_STATE_OPEN;
                s->ConnectFailed = TRUE;
            }
        }
    }
    unlock();
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

BOOLEAN IopIsAfd(PFILE_OBJECT file)
{
    return g_device && file->DeviceObject == g_device;
}

/* ------------------------------------------------------------ the driver */

static NTSTATUS complete_irp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI afd_create(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    AFD_SOCKET *s = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*s), KI_TAG('A', 'f', 'd', 'S'));
    if (!s)
        return complete_irp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    memset(s, 0, sizeof(*s));
    InitializeListHead(&s->Receivers);
    InitializeListHead(&s->Senders);
    InitializeListHead(&s->Connects);
    InitializeListHead(&s->Accepters);
    s->Enabled = FD_ALL;
    lock();
    InsertTailList(&g_sockets, &s->Links);
    unlock();
    file->FsContext = s;
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

/* the last handle went: pending requests end and the host socket closes. */
static NTSTATUS NTAPI afd_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    AFD_SOCKET *s = file->FsContext;
    if (s) {
        lock();
        s->Closed = TRUE;
        fail_list(&s->Receivers, STATUS_AFD_CANCELLED);
        fail_list(&s->Senders, STATUS_AFD_CANCELLED);
        fail_list(&s->Connects, STATUS_AFD_CANCELLED);
        fail_list(&s->Accepters, STATUS_AFD_CANCELLED);
        if (s->Host)
            host(MIME_HC_NET_CLOSE, s->Host, 0, 0, 0, 0, NULL);
        s->Host = 0;
        if (s->SelectEvent)
            ObDereferenceObject(s->SelectEvent);
        s->SelectEvent = NULL;
        RemoveEntryList(&s->Links);
        InitializeListHead(&s->Links);
        wake_pollers();
        unlock();
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI afd_close(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    if (file->FsContext) {
        ExFreePool(file->FsContext);
        file->FsContext = NULL;
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI afd_volume(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.QueryVolume.FsInformationClass != FileFsDeviceInformation)
        return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
    if (stack->Parameters.QueryVolume.Length < sizeof(FILE_FS_DEVICE_INFORMATION))
        return complete_irp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    /* GetFileType calls a socket a pipe, like windows does. */
    FILE_FS_DEVICE_INFORMATION *info = irp->AssociatedIrp.SystemBuffer;
    info->DeviceType = FILE_DEVICE_NAMED_PIPE;
    info->Characteristics = 0;
    return complete_irp(irp, STATUS_SUCCESS, sizeof(*info));
}

static NTSTATUS NTAPI AfdDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = afd_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = afd_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = afd_close;
    driver->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = afd_volume;
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_AFD_DEVICE);
    return IoCreateDevice(driver, 0, &name, FILE_DEVICE_NETWORK, 0, FALSE, &g_device);
}

void IopInitializeAfd(void)
{
    KeInitializeMutex(&g_lock, 0);
    KeInitializeEvent(&g_work, SynchronizationEvent, FALSE);
    InitializeListHead(&g_sockets);
    InitializeListHead(&g_pollers);
    InitializeListHead(&g_resolvers);
    g_bounce = ExAllocatePool2(POOL_FLAG_NON_PAGED, AFD_CHUNK, KI_TAG('A', 'f', 'd', 'B'));
    if (!g_bounce) {
        KiLogPrintf("afd has no bounce buffer");
        return;
    }
    NTSTATUS status = IoCreateDriver(L"\\Driver\\AFD", AfdDriverEntry, NULL);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("afd failed to start: %08x", status);
        return;
    }
    status = PsCreateSystemThread(NULL, 0, NULL, NULL, NULL, worker, NULL);
    if (!NT_SUCCESS(status))
        KiLogPrintf("afd has no worker: %08x", status);
}
