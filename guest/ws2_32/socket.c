/*
 * sockets. mime has no network stack yet, so ws2_32 implements tcp and udp
 * over the loopback interface inside the process: a socket is a kernel
 * event handle, which keeps its value unique among the process handles,
 * plus the state below. connections to another process or another host
 * fail like they would on a machine whose only interface is the loopback.
 */
#include "ws2_32_private.h"

/* bytes a connection buffers before send blocks or fails with WSAEWOULDBLOCK. */
#define WS_STREAM_LIMIT (1024 * 1024)
#define WS_DEFAULT_BUFFER 65536
#define WS_MAX_BACKLOG 200
#define WS_EPHEMERAL_FIRST 49152
#define WS_MAX_UDP_V4 65507
#define WS_MAX_UDP_V6 65527
#define WS_EXTRA_OPTIONS 16
#define WS_MAX_BUFFERS 64

enum { WS_OPEN, WS_LISTENING, WS_CONNECTED };

enum { WS_READABLE = 1, WS_WRITABLE = 2, WS_FAILED = 4 };

/* an address and port; ipv4 is kept v4 mapped so both families compare alike. */
typedef struct {
    IN6_ADDR addr;
    USHORT port;
    ULONG scope;
} WS_ENDPOINT;

typedef struct _WS_DATAGRAM {
    struct _WS_DATAGRAM *next;
    WS_ENDPOINT from;
    ULONG size;
    BYTE data[];
} WS_DATAGRAM;

/* an overlapped receive waiting for data. */
typedef struct _WS_REQUEST {
    struct _WS_REQUEST *next;
    LPWSAOVERLAPPED overlapped;
    LPWSAOVERLAPPED_COMPLETION_ROUTINE routine;
    HANDLE thread;
    DWORD flags;
    struct sockaddr *from;
    LPINT fromlen;
    DWORD count;
    WSABUF buffers[];
} WS_REQUEST;

typedef struct _WS_SOCKET {
    struct _WS_SOCKET *next;
    SOCKET handle;
    LONG refs;
    BOOL registered;
    BOOL closed;
    int family, type, protocol;
    BOOL nonblocking;
    int state;
    BOOL bound;
    WS_ENDPOINT local;
    BOOL has_remote;
    WS_ENDPOINT remote;
    /* a stream connection. */
    struct _WS_SOCKET *peer;
    BYTE *rx;
    SIZE_T rx_start, rx_end, rx_size;
    BOOL rx_fin, reset, shut_rd, shut_wr, write_blocked;
    ULONGLONG connect_time;
    /* a datagram socket. */
    WS_DATAGRAM *dg_head, *dg_tail;
    SIZE_T dg_bytes;
    BOOL udp_reset, connreset;
    /* a listening socket and the connections waiting for accept. */
    int backlog;
    struct _WS_SOCKET *queue_head, *queue_tail, *queue_next;
    int queue_count;
    BOOL accepted;
    /* the outcome of the last connect. */
    BOOL connect_failed, connect_report;
    int connect_error;
    int so_error;
    /* options. */
    BOOL reuseaddr, exclusive, keepalive, broadcast, oobinline, nodelay, v6only;
    LINGER linger;
    int sndbuf, rcvbuf;
    DWORD rcvtimeo, sndtimeo;
    struct {
        int level, name, value;
    } extra[WS_EXTRA_OPTIONS];
    int extra_count;
    /* WSAAsyncSelect and WSAEventSelect. */
    HWND hwnd;
    UINT msg;
    HANDLE event;
    long select_events, enabled, recorded;
    int errors[FD_MAX_EVENTS];
    BOOL close_reported;
    WS_REQUEST *requests;
} WS_SOCKET;

static CRITICAL_SECTION lock;
static CONDITION_VARIABLE changed;
static WS_SOCKET *sockets;
static USHORT next_port;

void WsSocketsInitialize(void)
{
    InitializeCriticalSection(&lock);
    InitializeConditionVariable(&changed);
    next_port = (USHORT)(WS_EPHEMERAL_FIRST + GetTickCount() % 16384);
}

/* ------------------------------------------------------------------ endpoints */

static const BYTE v4_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};

static BOOL ep_is_v4(const WS_ENDPOINT *ep)
{
    return !memcmp(ep->addr.u.Byte, v4_prefix, 12);
}

static BOOL ep_is_any(const WS_ENDPOINT *ep)
{
    static const BYTE zero[16];
    if (ep_is_v4(ep))
        return !memcmp(&ep->addr.u.Byte[12], zero, 4);
    return !memcmp(ep->addr.u.Byte, zero, 16);
}

static BOOL ep_is_loopback(const WS_ENDPOINT *ep)
{
    return WsIsLoopbackV6(&ep->addr);
}

static BOOL ep_is_multicast(const WS_ENDPOINT *ep)
{
    if (ep_is_v4(ep))
        return (ep->addr.u.Byte[12] & 0xF0) == 0xE0;
    return ep->addr.u.Byte[0] == 0xFF;
}

static BOOL ep_is_broadcast(const WS_ENDPOINT *ep)
{
    return ep_is_v4(ep) && ep->addr.u.Byte[12] == 0xFF && ep->addr.u.Byte[13] == 0xFF &&
           ep->addr.u.Byte[14] == 0xFF && ep->addr.u.Byte[15] == 0xFF;
}

static BOOL ep_same_address(const WS_ENDPOINT *a, const WS_ENDPOINT *b)
{
    return !memcmp(&a->addr, &b->addr, sizeof(a->addr));
}

static void ep_loopback(WS_ENDPOINT *ep, BOOL v4)
{
    memset(&ep->addr, 0, sizeof(ep->addr));
    if (v4) {
        memcpy(ep->addr.u.Byte, v4_prefix, 12);
        ep->addr.u.Byte[12] = 127;
        ep->addr.u.Byte[15] = 1;
    } else {
        ep->addr.u.Byte[15] = 1;
    }
    ep->scope = 0;
}

static int sockaddr_size(int family)
{
    return family == AF_INET ? (int)sizeof(SOCKADDR_IN) : (int)sizeof(SOCKADDR_IN6);
}

static int to_endpoint(const WS_SOCKET *sock, const SOCKADDR *addr, int len, WS_ENDPOINT *ep)
{
    if (!addr || len < (int)sizeof(SOCKADDR_IN))
        return WSAEFAULT;
    memset(ep, 0, sizeof(*ep));
    if (addr->sa_family == AF_INET) {
        if (sock->family != AF_INET)
            return WSAEAFNOSUPPORT;
        const SOCKADDR_IN *in = (const SOCKADDR_IN *)addr;
        memcpy(ep->addr.u.Byte, v4_prefix, 12);
        memcpy(&ep->addr.u.Byte[12], &in->sin_addr, 4);
        ep->port = in->sin_port;
        return 0;
    }
    if (addr->sa_family == AF_INET6) {
        if (sock->family != AF_INET6)
            return WSAEAFNOSUPPORT;
        if (len < (int)sizeof(SOCKADDR_IN6))
            return WSAEFAULT;
        const SOCKADDR_IN6 *in6 = (const SOCKADDR_IN6 *)addr;
        ep->addr = in6->sin6_addr;
        ep->port = in6->sin6_port;
        ep->scope = in6->sin6_scope_id;
        /* a v6 only socket cannot reach the ipv4 side. */
        if (sock->v6only && ep_is_v4(ep))
            return WSAEAFNOSUPPORT;
        return 0;
    }
    return WSAEAFNOSUPPORT;
}

static int from_endpoint(const WS_SOCKET *sock, const WS_ENDPOINT *ep, SOCKADDR *addr, int *len)
{
    int need = sockaddr_size(sock->family);
    if (!addr || !len || *len < need)
        return WSAEFAULT;
    memset(addr, 0, need);
    if (sock->family == AF_INET) {
        SOCKADDR_IN *in = (SOCKADDR_IN *)addr;
        in->sin_family = AF_INET;
        in->sin_port = ep->port;
        memcpy(&in->sin_addr, &ep->addr.u.Byte[12], 4);
    } else {
        SOCKADDR_IN6 *in6 = (SOCKADDR_IN6 *)addr;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = ep->port;
        in6->sin6_addr = ep->addr;
        in6->sin6_scope_id = ep->scope;
    }
    *len = need;
    return 0;
}

/* whether sock, bound, receives on ep's address. */
static BOOL covers(const WS_SOCKET *sock, const WS_ENDPOINT *ep)
{
    if (!ep_is_any(&sock->local))
        return ep_same_address(&sock->local, ep);
    if (sock->family == AF_INET)
        return ep_is_v4(ep);
    return !ep_is_v4(ep) || !sock->v6only;
}

/* ------------------------------------------------------------------ the socket table */

static WS_SOCKET *find(SOCKET s)
{
    for (WS_SOCKET *sock = sockets; sock; sock = sock->next) {
        if (sock->handle == s)
            return sock;
    }
    return NULL;
}

BOOL WsIsSocket(SOCKET s)
{
    EnterCriticalSection(&lock);
    BOOL found = find(s) != NULL;
    LeaveCriticalSection(&lock);
    return found;
}

/* enters the lock and references the socket; *error is set when it fails. */
static WS_SOCKET *acquire(SOCKET s, int *error)
{
    *error = WsCheckStarted();
    if (*error)
        return NULL;
    EnterCriticalSection(&lock);
    WS_SOCKET *sock = find(s);
    if (!sock) {
        LeaveCriticalSection(&lock);
        *error = WSAENOTSOCK;
        return NULL;
    }
    sock->refs++;
    return sock;
}

static void destroy(WS_SOCKET *sock)
{
    WsFree(sock->rx);
    while (sock->dg_head) {
        WS_DATAGRAM *d = sock->dg_head;
        sock->dg_head = d->next;
        WsFree(d);
    }
    WsFree(sock);
}

static void release(WS_SOCKET *sock)
{
    if (!--sock->refs && sock->closed)
        destroy(sock);
    LeaveCriticalSection(&lock);
}

/* stores error and releases; the usual end of a failing call. */
static int fail(WS_SOCKET *sock, int error)
{
    release(sock);
    return WsFail(error);
}

static void wake(void)
{
    WakeAllConditionVariable(&changed);
}

static ULONGLONG deadline_after(DWORD ms)
{
    return ms ? GetTickCount64() + ms : 0;
}

/* waits for any socket change; WSAETIMEDOUT at the deadline, WSAEINTR when the socket was closed. */
static int wait_change(WS_SOCKET *sock, ULONGLONG deadline)
{
    DWORD ms = INFINITE;
    if (deadline) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            return WSAETIMEDOUT;
        ms = (DWORD)min(deadline - now, (ULONGLONG)0xFFFFFFFE);
    }
    SleepConditionVariableCS(&changed, &lock, ms);
    if (sock && sock->closed)
        return WSAEINTR;
    return 0;
}

/* ------------------------------------------------------------------ event selection */

static SIZE_T rx_available(const WS_SOCKET *sock)
{
    return sock->rx_end - sock->rx_start;
}

static BOOL peer_has_room(const WS_SOCKET *sock)
{
    return !sock->peer || rx_available(sock->peer) < WS_STREAM_LIMIT;
}

/* the network events whose condition currently holds, with their errors. */
static long conditions(const WS_SOCKET *sock, int *errors)
{
    long c = 0;
    if (sock->type == SOCK_STREAM) {
        if (sock->state == WS_LISTENING && sock->queue_count)
            c |= FD_ACCEPT;
        if (sock->state == WS_CONNECTED) {
            SIZE_T avail = rx_available(sock);
            if (avail && !sock->shut_rd)
                c |= FD_READ;
            if (!sock->shut_wr && sock->peer && peer_has_room(sock))
                c |= FD_WRITE;
            if (sock->reset || (sock->rx_fin && !avail)) {
                c |= FD_CLOSE;
                errors[FD_CLOSE_BIT] = sock->reset ? WSAECONNRESET : 0;
            }
        }
        if (sock->connect_report) {
            c |= FD_CONNECT;
            errors[FD_CONNECT_BIT] = sock->connect_error;
        }
    } else {
        if (sock->dg_head || (sock->udp_reset && sock->connreset))
            c |= FD_READ;
        c |= FD_WRITE;
    }
    return c;
}

/* records the selected events that became true and tells the window or the event. */
static void signal_events(WS_SOCKET *sock)
{
    static const int order[] = {FD_CONNECT_BIT, FD_ACCEPT_BIT, FD_READ_BIT, FD_WRITE_BIT, FD_CLOSE_BIT};
    if (!sock->select_events || !sock->registered || sock->closed)
        return;
    int errors[FD_MAX_EVENTS] = {0};
    long fire = conditions(sock, errors) & sock->select_events & sock->enabled;
    if (sock->close_reported)
        fire &= ~FD_CLOSE;
    if (!fire)
        return;
    sock->enabled &= ~fire;
    if (fire & FD_CONNECT)
        sock->connect_report = FALSE;
    if (fire & FD_CLOSE)
        sock->close_reported = TRUE;
    sock->recorded |= fire;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(order); i++) {
        int bit = order[i];
        if (!(fire & (1 << bit)))
            continue;
        sock->errors[bit] = errors[bit];
        if (sock->hwnd)
            PostMessageW(sock->hwnd, sock->msg, (WPARAM)sock->handle, WSAMAKESELECTREPLY(1 << bit, errors[bit]));
    }
    if (sock->event)
        SetEvent(sock->event);
}

/* the select view of a socket. */
static int readiness(const WS_SOCKET *sock)
{
    int r = 0;
    if (sock->type == SOCK_STREAM) {
        if (sock->state == WS_LISTENING && sock->queue_count)
            r |= WS_READABLE;
        if (sock->state == WS_CONNECTED) {
            if (rx_available(sock) || sock->rx_fin || sock->reset || sock->shut_rd)
                r |= WS_READABLE;
            if (!sock->shut_wr && peer_has_room(sock))
                r |= WS_WRITABLE;
        }
        if (sock->state == WS_OPEN && sock->connect_failed)
            r |= WS_FAILED;
    } else {
        if (sock->dg_head || (sock->udp_reset && sock->connreset))
            r |= WS_READABLE;
        r |= WS_WRITABLE;
    }
    return r;
}

/* ------------------------------------------------------------------ overlapped requests */

typedef struct {
    LPWSAOVERLAPPED_COMPLETION_ROUTINE routine;
    DWORD error, bytes, flags;
    LPWSAOVERLAPPED overlapped;
} WS_APC;

static void NTAPI completion_apc(ULONG_PTR data)
{
    WS_APC *apc = (WS_APC *)data;
    apc->routine(apc->error, apc->bytes, apc->overlapped, apc->flags);
    WsFree(apc);
}

static DWORD status_error(NTSTATUS status)
{
    switch (status) {
    case STATUS_SUCCESS:
        return 0;
    case STATUS_BUFFER_OVERFLOW:
        return WSAEMSGSIZE;
    case STATUS_CANCELLED:
        return WSA_OPERATION_ABORTED;
    case STATUS_CONNECTION_RESET:
        return WSAECONNRESET;
    case STATUS_CONNECTION_ABORTED:
        return WSAECONNABORTED;
    }
    return WSAEFAULT;
}

static HANDLE overlapped_event(const WS_SOCKET *sock, LPWSAOVERLAPPED ov)
{
    HANDLE event = (HANDLE)((ULONG_PTR)ov->hEvent & ~(ULONG_PTR)1);
    return event ? event : (HANDLE)sock->handle;
}

/* the end of an overlapped operation: the status, the event, the completion routine. */
static void complete(WS_SOCKET *sock, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine,
                     HANDLE thread, NTSTATUS status, DWORD bytes, DWORD flags)
{
    ov->Internal = (ULONG_PTR)status;
    ov->InternalHigh = bytes;
    ov->Offset = flags;
    SetEvent(overlapped_event(sock, ov));
    if (routine) {
        WS_APC *apc = WsAlloc(sizeof(*apc));
        if (apc) {
            apc->routine = routine;
            apc->error = status_error(status);
            apc->bytes = bytes;
            apc->flags = flags;
            apc->overlapped = ov;
            if (!QueueUserAPC(completion_apc, thread ? thread : GetCurrentThread(), (ULONG_PTR)apc))
                WsFree(apc);
        }
    }
}

static void finish_request(WS_SOCKET *sock, WS_REQUEST *r, NTSTATUS status, DWORD bytes, DWORD flags)
{
    complete(sock, r->overlapped, r->routine, r->thread, status, bytes, flags);
    if (r->thread)
        CloseHandle(r->thread);
    WsFree(r);
}

static void cancel_requests(WS_SOCKET *sock)
{
    while (sock->requests) {
        WS_REQUEST *r = sock->requests;
        sock->requests = r->next;
        finish_request(sock, r, STATUS_CANCELLED, 0, 0);
    }
}

/* ------------------------------------------------------------------ receive buffers */

static DWORD buffers_size(const WSABUF *buffers, DWORD count)
{
    ULONGLONG total = 0;
    for (DWORD i = 0; i < count; i++)
        total += buffers[i].len;
    return (DWORD)min(total, (ULONGLONG)0x7FFFFFFF);
}

static DWORD scatter(const WSABUF *buffers, DWORD count, const BYTE *data, DWORD size)
{
    DWORD done = 0;
    for (DWORD i = 0; i < count && done < size; i++) {
        DWORD n = min(buffers[i].len, size - done);
        memcpy(buffers[i].buf, data + done, n);
        done += n;
    }
    return done;
}

/* the part of the buffers left after the first done bytes. */
static DWORD remaining_buffers(const WSABUF *buffers, DWORD count, DWORD done, WSABUF *rest)
{
    DWORD k = 0;
    for (DWORD i = 0; i < count; i++) {
        if (done >= buffers[i].len) {
            done -= buffers[i].len;
            continue;
        }
        rest[k].buf = buffers[i].buf + done;
        rest[k++].len = buffers[i].len - done;
        done = 0;
    }
    return k;
}

/* every buffer with a length needs memory behind it. */
static BOOL buffers_valid(const WSABUF *buffers, DWORD count)
{
    if (!buffers)
        return !count;
    for (DWORD i = 0; i < count; i++) {
        if (buffers[i].len && !buffers[i].buf)
            return FALSE;
    }
    return TRUE;
}

static BOOL rx_append(WS_SOCKET *sock, const WSABUF *buffers, DWORD count, DWORD size)
{
    if (sock->rx_start && sock->rx_start == sock->rx_end)
        sock->rx_start = sock->rx_end = 0;
    if (sock->rx_end + size > sock->rx_size) {
        SIZE_T used = rx_available(sock);
        if (sock->rx_start) {
            memmove(sock->rx, sock->rx + sock->rx_start, used);
            sock->rx_start = 0;
            sock->rx_end = used;
        }
        if (used + size > sock->rx_size) {
            SIZE_T grown = max(sock->rx_size * 2, used + size);
            BYTE *rx = sock->rx ? HeapReAlloc(GetProcessHeap(), 0, sock->rx, grown) : WsAlloc(grown);
            if (!rx)
                return FALSE;
            sock->rx = rx;
            sock->rx_size = grown;
        }
    }
    for (DWORD i = 0; i < count; i++) {
        memcpy(sock->rx + sock->rx_end, buffers[i].buf, buffers[i].len);
        sock->rx_end += buffers[i].len;
    }
    return TRUE;
}

/* data was consumed: a blocked sender may write again. */
static void room_made(WS_SOCKET *sock)
{
    WS_SOCKET *sender = sock->peer;
    if (sender && sender->write_blocked && peer_has_room(sender)) {
        sender->write_blocked = FALSE;
        sender->enabled |= FD_WRITE;
        signal_events(sender);
    }
}

/* reads from a connection without waiting; FALSE when the call would have to wait. */
static BOOL stream_take(WS_SOCKET *sock, const WSABUF *buffers, DWORD count, DWORD flags, DWORD *received,
                        int *error)
{
    SIZE_T avail = rx_available(sock);
    DWORD want = buffers_size(buffers, count);
    *error = 0;
    if (avail) {
        DWORD n = scatter(buffers, count, sock->rx + sock->rx_start, (DWORD)min(avail, (SIZE_T)want));
        if (!(flags & MSG_PEEK)) {
            sock->rx_start += n;
            room_made(sock);
        }
        *received = n;
        return TRUE;
    }
    *received = 0;
    if (sock->reset) {
        *error = WSAECONNRESET;
        return TRUE;
    }
    return sock->rx_fin;
}

/* takes one datagram without waiting; FALSE when the queue is empty. */
static BOOL dgram_take(WS_SOCKET *sock, const WSABUF *buffers, DWORD count, DWORD flags, DWORD *received,
                       WS_ENDPOINT *from, int *error)
{
    WS_DATAGRAM *d = sock->dg_head;
    *error = 0;
    *received = 0;
    if (!d) {
        if (sock->udp_reset && sock->connreset) {
            sock->udp_reset = FALSE;
            *error = WSAECONNRESET;
            return TRUE;
        }
        return FALSE;
    }
    DWORD want = buffers_size(buffers, count);
    *received = scatter(buffers, count, d->data, min(d->size, want));
    *from = d->from;
    if (d->size > want)
        *error = WSAEMSGSIZE;
    if (!(flags & MSG_PEEK)) {
        sock->dg_head = d->next;
        if (!sock->dg_head)
            sock->dg_tail = NULL;
        sock->dg_bytes -= d->size;
        WsFree(d);
    }
    return TRUE;
}

/* completes the waiting overlapped receives that data or the end of the connection can satisfy. */
static void serve_requests(WS_SOCKET *sock)
{
    while (sock->requests) {
        WS_REQUEST *r = sock->requests;
        DWORD received;
        int error;
        WS_ENDPOINT from;
        BOOL done;
        if (sock->type == SOCK_STREAM)
            done = stream_take(sock, r->buffers, r->count, r->flags, &received, &error);
        else
            done = dgram_take(sock, r->buffers, r->count, r->flags, &received, &from, &error);
        if (!done)
            return;
        sock->requests = r->next;
        NTSTATUS status = STATUS_SUCCESS;
        DWORD out_flags = 0;
        if (error == WSAECONNRESET)
            status = STATUS_CONNECTION_RESET;
        if (error == WSAEMSGSIZE) {
            status = STATUS_BUFFER_OVERFLOW;
            out_flags = MSG_PARTIAL;
        }
        if (sock->type == SOCK_DGRAM && !error && r->from && r->fromlen)
            from_endpoint(sock, &from, r->from, r->fromlen);
        finish_request(sock, r, status, received, out_flags);
    }
}

/* something arrived on sock: overlapped receives, selected events and blocked threads hear of it. */
static void data_arrived(WS_SOCKET *sock)
{
    serve_requests(sock);
    signal_events(sock);
    wake();
}

/* ------------------------------------------------------------------ binding */

static BOOL port_in_use(const WS_SOCKET *sock, USHORT port)
{
    for (WS_SOCKET *other = sockets; other; other = other->next) {
        if (other != sock && other->bound && !other->accepted && other->type == sock->type &&
            other->local.port == port)
            return TRUE;
    }
    return FALSE;
}

/* the binding rules: two sockets of one type cannot share a port on overlapping addresses unless both allow it. */
static int check_bind(const WS_SOCKET *sock, const WS_ENDPOINT *ep)
{
    for (WS_SOCKET *other = sockets; other; other = other->next) {
        if (other == sock || !other->bound || other->accepted || other->type != sock->type ||
            other->local.port != ep->port)
            continue;
        WS_SOCKET probe = *sock;
        probe.local = *ep;
        if (!covers(other, ep) && !covers(&probe, &other->local))
            continue;
        if (other->exclusive || sock->exclusive)
            return WSAEACCES;
        if (!sock->reuseaddr)
            return WSAEADDRINUSE;
    }
    return 0;
}

static int bind_endpoint(WS_SOCKET *sock, WS_ENDPOINT *ep)
{
    if (!ep_is_any(ep) && !ep_is_loopback(ep))
        return WSAEADDRNOTAVAIL;
    if (!ep->port) {
        for (int tries = 0; tries < 16384; tries++) {
            USHORT candidate = next_port;
            next_port = next_port == 0xFFFF ? WS_EPHEMERAL_FIRST : next_port + 1;
            if (!port_in_use(sock, htons(candidate))) {
                ep->port = htons(candidate);
                break;
            }
        }
        if (!ep->port)
            return WSAENOBUFS;
    } else {
        int error = check_bind(sock, ep);
        if (error)
            return error;
    }
    sock->local = *ep;
    sock->bound = TRUE;
    return 0;
}

/* binds a socket used before bind to the loopback address that reaches dest. */
static int auto_bind(WS_SOCKET *sock, const WS_ENDPOINT *dest)
{
    if (sock->bound)
        return 0;
    WS_ENDPOINT ep = {0};
    if (dest)
        ep_loopback(&ep, ep_is_v4(dest));
    else if (sock->family == AF_INET)
        memcpy(ep.addr.u.Byte, v4_prefix, 12);
    return bind_endpoint(sock, &ep);
}

/* the source address of traffic sent to dest. */
static WS_ENDPOINT source_for(const WS_SOCKET *sock, const WS_ENDPOINT *dest)
{
    WS_ENDPOINT ep = sock->local;
    if (ep_is_any(&ep) || ep_is_v4(&ep) != ep_is_v4(dest))
        ep_loopback(&ep, ep_is_v4(dest));
    ep.port = sock->local.port;
    return ep;
}

/* ------------------------------------------------------------------ socket creation and closing */

static int resolve_kind(int *af, int *type, int *protocol)
{
    if (*af != AF_INET && *af != AF_INET6)
        return WSAEAFNOSUPPORT;
    if (*type == 0)
        *type = *protocol == IPPROTO_UDP ? SOCK_DGRAM : *protocol == IPPROTO_TCP ? SOCK_STREAM : 0;
    switch (*type) {
    case SOCK_STREAM:
        if (*protocol != 0 && *protocol != IPPROTO_TCP)
            return WSAEPROTONOSUPPORT;
        *protocol = IPPROTO_TCP;
        return 0;
    case SOCK_DGRAM:
        if (*protocol != 0 && *protocol != IPPROTO_UDP)
            return WSAEPROTONOSUPPORT;
        *protocol = IPPROTO_UDP;
        return 0;
    }
    /* raw sockets need the network stack mime does not have. */
    return WSAESOCKTNOSUPPORT;
}

static WS_SOCKET *new_socket(int family, int type, int protocol)
{
    WS_SOCKET *sock = WsAllocZero(sizeof(*sock));
    if (!sock)
        return NULL;
    HANDLE handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!handle) {
        WsFree(sock);
        return NULL;
    }
    sock->handle = (SOCKET)handle;
    sock->family = family;
    sock->type = type;
    sock->protocol = protocol;
    sock->connreset = TRUE;
    sock->v6only = TRUE;
    sock->sndbuf = WS_DEFAULT_BUFFER;
    sock->rcvbuf = WS_DEFAULT_BUFFER;
    sock->enabled = FD_ALL_EVENTS;
    return sock;
}

static void register_socket(WS_SOCKET *sock)
{
    sock->registered = TRUE;
    sock->next = sockets;
    sockets = sock;
}

static SOCKET create_socket(int af, int type, int protocol, DWORD flags)
{
    int error = WsCheckStarted();
    if (!error)
        error = resolve_kind(&af, &type, &protocol);
    if (error) {
        SetLastError(error);
        return INVALID_SOCKET;
    }
    WS_SOCKET *sock = new_socket(af, type, protocol);
    if (!sock) {
        SetLastError(WSAENOBUFS);
        return INVALID_SOCKET;
    }
    EnterCriticalSection(&lock);
    register_socket(sock);
    LeaveCriticalSection(&lock);
    return sock->handle;
}

SOCKET WSAAPI socket(int af, int type, int protocol)
{
    return create_socket(af, type, protocol, WSA_FLAG_OVERLAPPED);
}

SOCKET WSAAPI WSASocketW(int af, int type, int protocol, LPWSAPROTOCOL_INFOW info, GROUP g, DWORD flags)
{
    if (info) {
        if (af == FROM_PROTOCOL_INFO)
            af = info->iAddressFamily;
        if (type == FROM_PROTOCOL_INFO)
            type = info->iSocketType;
        if (protocol == FROM_PROTOCOL_INFO)
            protocol = info->iProtocol;
    }
    return create_socket(af, type, protocol, flags);
}

SOCKET WSAAPI WSASocketA(int af, int type, int protocol, LPWSAPROTOCOL_INFOA info, GROUP g, DWORD flags)
{
    if (info) {
        if (af == FROM_PROTOCOL_INFO)
            af = info->iAddressFamily;
        if (type == FROM_PROTOCOL_INFO)
            type = info->iSocketType;
        if (protocol == FROM_PROTOCOL_INFO)
            protocol = info->iProtocol;
    }
    return create_socket(af, type, protocol, flags);
}

static void unlink_socket(WS_SOCKET *sock)
{
    for (WS_SOCKET **p = &sockets; *p; p = &(*p)->next) {
        if (*p == sock) {
            *p = sock->next;
            break;
        }
    }
    sock->registered = FALSE;
}

/* ends the connection of sock; abortive when it drops unread data or lingers for zero seconds. */
static void disconnect(WS_SOCKET *sock)
{
    WS_SOCKET *peer = sock->peer;
    if (!peer)
        return;
    BOOL abortive = rx_available(sock) || (sock->linger.l_onoff && !sock->linger.l_linger);
    peer->peer = NULL;
    if (abortive)
        peer->reset = TRUE;
    else
        peer->rx_fin = TRUE;
    sock->peer = NULL;
    data_arrived(peer);
}

/* frees what nobody references; called with the lock held. */
static void close_socket(WS_SOCKET *sock)
{
    if (sock->registered)
        unlink_socket(sock);
    sock->closed = TRUE;
    sock->select_events = 0;
    cancel_requests(sock);
    disconnect(sock);
    while (sock->queue_head) {
        WS_SOCKET *child = sock->queue_head;
        sock->queue_head = child->queue_next;
        /* a connection nobody accepted is reset. */
        if (child->peer) {
            child->peer->reset = TRUE;
            child->peer->peer = NULL;
            data_arrived(child->peer);
            child->peer = NULL;
        }
        child->closed = TRUE;
        CloseHandle((HANDLE)child->handle);
        if (!child->refs)
            destroy(child);
    }
    sock->queue_count = 0;
    CloseHandle((HANDLE)sock->handle);
    wake();
    if (!sock->refs)
        destroy(sock);
}

int WSAAPI closesocket(SOCKET s)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    EnterCriticalSection(&lock);
    WS_SOCKET *sock = find(s);
    if (!sock) {
        LeaveCriticalSection(&lock);
        return WsFail(WSAENOTSOCK);
    }
    close_socket(sock);
    LeaveCriticalSection(&lock);
    return 0;
}

void WsCloseAllSockets(void)
{
    EnterCriticalSection(&lock);
    while (sockets)
        close_socket(sockets);
    LeaveCriticalSection(&lock);
}

/* ------------------------------------------------------------------ bind, listen, connect, accept */

int WSAAPI bind(SOCKET s, const struct sockaddr *name, int namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (sock->bound)
        return fail(sock, WSAEINVAL);
    WS_ENDPOINT ep;
    error = to_endpoint(sock, name, namelen, &ep);
    if (!error)
        error = bind_endpoint(sock, &ep);
    if (error)
        return fail(sock, error);
    signal_events(sock);
    release(sock);
    return 0;
}

int WSAAPI listen(SOCKET s, int backlog)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (sock->type != SOCK_STREAM)
        return fail(sock, WSAEOPNOTSUPP);
    if (sock->state == WS_CONNECTED)
        return fail(sock, WSAEISCONN);
    if (!sock->bound)
        return fail(sock, WSAEINVAL);
    if (backlog == SOMAXCONN)
        backlog = WS_MAX_BACKLOG;
    else if (backlog < 0)
        backlog = min(max(-backlog, WS_MAX_BACKLOG), 65535);
    else
        backlog = min(max(backlog, 1), WS_MAX_BACKLOG);
    sock->backlog = backlog;
    sock->state = WS_LISTENING;
    signal_events(sock);
    release(sock);
    return 0;
}

/* the listening socket a connection to ep reaches, preferring a specific address over a wildcard. */
static WS_SOCKET *find_listener(const WS_SOCKET *client, const WS_ENDPOINT *ep)
{
    WS_SOCKET *wildcard = NULL;
    for (WS_SOCKET *sock = sockets; sock; sock = sock->next) {
        if (sock->state != WS_LISTENING || sock->type != SOCK_STREAM || sock->local.port != ep->port ||
            !covers(sock, ep))
            continue;
        if (!ep_is_any(&sock->local))
            return sock;
        if (!wildcard)
            wildcard = sock;
    }
    return wildcard;
}

static int stream_connect(WS_SOCKET *sock, const WS_ENDPOINT *ep)
{
    if (sock->state == WS_LISTENING)
        return WSAEINVAL;
    if (sock->state == WS_CONNECTED)
        return WSAEISCONN;
    if (ep_is_any(ep))
        return WSAEADDRNOTAVAIL;
    if (!ep_is_loopback(ep))
        return WSAENETUNREACH;
    int error = auto_bind(sock, ep);
    if (error)
        return error;
    sock->connect_failed = FALSE;
    sock->so_error = 0;
    WS_SOCKET *listener = find_listener(sock, ep);
    WS_SOCKET *child = NULL;
    if (listener && listener->queue_count < listener->backlog)
        child = new_socket(listener->family, SOCK_STREAM, IPPROTO_TCP);
    if (!child) {
        sock->connect_failed = TRUE;
        sock->connect_error = WSAECONNREFUSED;
        sock->so_error = WSAECONNREFUSED;
        sock->connect_report = TRUE;
        signal_events(sock);
        wake();
        return sock->nonblocking ? WSAEWOULDBLOCK : WSAECONNREFUSED;
    }
    WS_ENDPOINT source = source_for(sock, ep);
    if (ep_is_any(&sock->local))
        sock->local.addr = source.addr;
    child->accepted = TRUE;
    child->bound = TRUE;
    child->local = *ep;
    child->has_remote = TRUE;
    child->remote = source;
    child->state = WS_CONNECTED;
    child->connect_time = GetTickCount64();
    child->v6only = listener->v6only;
    child->nonblocking = listener->nonblocking;
    child->keepalive = listener->keepalive;
    child->nodelay = listener->nodelay;
    child->linger = listener->linger;
    child->sndbuf = listener->sndbuf;
    child->rcvbuf = listener->rcvbuf;
    child->oobinline = listener->oobinline;
    child->peer = sock;
    sock->peer = child;
    sock->state = WS_CONNECTED;
    sock->has_remote = TRUE;
    sock->remote = *ep;
    sock->connect_time = GetTickCount64();
    sock->connect_error = 0;
    sock->connect_report = TRUE;
    if (listener->queue_tail)
        listener->queue_tail->queue_next = child;
    else
        listener->queue_head = child;
    listener->queue_tail = child;
    listener->queue_count++;
    signal_events(listener);
    signal_events(sock);
    wake();
    /* like windows, a non blocking connect reports completion through select and the events. */
    return sock->nonblocking ? WSAEWOULDBLOCK : 0;
}

static int dgram_connect(WS_SOCKET *sock, const SOCKADDR *name, int namelen)
{
    if (name && namelen >= (int)sizeof(SOCKADDR) && name->sa_family == AF_UNSPEC) {
        sock->has_remote = FALSE;
        return 0;
    }
    WS_ENDPOINT ep;
    int error = to_endpoint(sock, name, namelen, &ep);
    if (error)
        return error;
    if (!ep_is_loopback(&ep) && !ep_is_any(&ep) && !ep_is_broadcast(&ep) && !ep_is_multicast(&ep))
        return WSAENETUNREACH;
    if ((error = auto_bind(sock, &ep)))
        return error;
    sock->has_remote = TRUE;
    sock->remote = ep;
    return 0;
}

int WSAAPI connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (sock->type == SOCK_DGRAM) {
        error = dgram_connect(sock, name, namelen);
    } else {
        WS_ENDPOINT ep;
        error = to_endpoint(sock, name, namelen, &ep);
        if (!error)
            error = stream_connect(sock, &ep);
    }
    if (error)
        return fail(sock, error);
    release(sock);
    return 0;
}

SOCKET WSAAPI accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock) {
        SetLastError(error);
        return INVALID_SOCKET;
    }
    if (sock->type != SOCK_STREAM)
        error = WSAEOPNOTSUPP;
    else if (sock->state != WS_LISTENING)
        error = WSAEINVAL;
    else if (addr && (!addrlen || *addrlen < sockaddr_size(sock->family)))
        error = WSAEFAULT;
    sock->enabled |= FD_ACCEPT;
    while (!error && !sock->queue_head) {
        if (sock->nonblocking)
            error = WSAEWOULDBLOCK;
        else
            error = wait_change(sock, 0);
        if (!error && sock->state != WS_LISTENING)
            error = WSAEINVAL;
    }
    if (error) {
        signal_events(sock);
        fail(sock, error);
        return INVALID_SOCKET;
    }
    WS_SOCKET *child = sock->queue_head;
    sock->queue_head = child->queue_next;
    if (!sock->queue_head)
        sock->queue_tail = NULL;
    sock->queue_count--;
    child->queue_next = NULL;
    /* the accepted socket takes the selection of the listening one. */
    child->hwnd = sock->hwnd;
    child->msg = sock->msg;
    child->event = sock->event;
    child->select_events = sock->select_events;
    child->enabled = FD_ALL_EVENTS;
    register_socket(child);
    if (addr)
        from_endpoint(child, &child->remote, addr, addrlen);
    signal_events(child);
    signal_events(sock);
    SOCKET result = child->handle;
    release(sock);
    return result;
}

int WSAAPI shutdown(SOCKET s, int how)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (how < SD_RECEIVE || how > SD_BOTH)
        return fail(sock, WSAEINVAL);
    if (sock->type == SOCK_STREAM && sock->state != WS_CONNECTED)
        return fail(sock, WSAENOTCONN);
    if (how != SD_SEND)
        sock->shut_rd = TRUE;
    if (how != SD_RECEIVE && !sock->shut_wr) {
        sock->shut_wr = TRUE;
        if (sock->peer) {
            sock->peer->rx_fin = TRUE;
            data_arrived(sock->peer);
        }
    }
    signal_events(sock);
    wake();
    release(sock);
    return 0;
}

int WSAAPI getsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!name || !namelen)
        return fail(sock, WSAEFAULT);
    if (!sock->bound)
        return fail(sock, WSAEINVAL);
    if ((error = from_endpoint(sock, &sock->local, name, namelen)))
        return fail(sock, error);
    release(sock);
    return 0;
}

int WSAAPI getpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!name || !namelen)
        return fail(sock, WSAEFAULT);
    if (!sock->has_remote || (sock->type == SOCK_STREAM && sock->state != WS_CONNECTED))
        return fail(sock, WSAENOTCONN);
    if ((error = from_endpoint(sock, &sock->remote, name, namelen)))
        return fail(sock, error);
    release(sock);
    return 0;
}

/* ------------------------------------------------------------------ sending */

static int stream_send(WS_SOCKET *sock, const WSABUF *buffers, DWORD count, DWORD *sent, BOOL overlapped)
{
    if (sock->state != WS_CONNECTED)
        return WSAENOTCONN;
    if (sock->shut_wr)
        return WSAESHUTDOWN;
    ULONGLONG deadline = deadline_after(sock->sndtimeo);
    for (;;) {
        if (sock->reset)
            return WSAECONNRESET;
        if (!sock->peer) {
            /* the other side closed: windows answers the first send with a reset. */
            sock->reset = TRUE;
            signal_events(sock);
            return WSAECONNRESET;
        }
        if (overlapped || peer_has_room(sock))
            break;
        if (sock->nonblocking) {
            sock->write_blocked = TRUE;
            return WSAEWOULDBLOCK;
        }
        int error = wait_change(sock, deadline);
        if (error)
            return error;
    }
    DWORD size = buffers_size(buffers, count);
    WS_SOCKET *peer = sock->peer;
    if (!peer->shut_rd) {
        if (!rx_append(peer, buffers, count, size))
            return WSAENOBUFS;
        data_arrived(peer);
    }
    *sent = size;
    return 0;
}

/* delivers one datagram to the sockets bound on dest. */
static void deliver(WS_SOCKET *sender, const WS_ENDPOINT *dest, const WSABUF *buffers, DWORD count, DWORD size)
{
    WS_ENDPOINT from = source_for(sender, dest);
    BOOL broadcast = ep_is_broadcast(dest);
    BOOL delivered = FALSE;
    for (WS_SOCKET *sock = sockets; sock; sock = sock->next) {
        if (sock->type != SOCK_DGRAM || !sock->bound || sock->local.port != dest->port)
            continue;
        if (broadcast ? !(ep_is_any(&sock->local) && covers(sock, dest)) : !covers(sock, dest))
            continue;
        if (sock->has_remote && (!ep_same_address(&sock->remote, &from) || sock->remote.port != from.port))
            continue;
        delivered = TRUE;
        if (!sock->shut_rd && sock->dg_bytes + size <= (SIZE_T)max(sock->rcvbuf, WS_DEFAULT_BUFFER)) {
            WS_DATAGRAM *d = WsAlloc(sizeof(*d) + size);
            if (d) {
                d->next = NULL;
                d->from = from;
                d->size = size;
                DWORD done = 0;
                for (DWORD i = 0; i < count; i++) {
                    memcpy(d->data + done, buffers[i].buf, buffers[i].len);
                    done += buffers[i].len;
                }
                if (sock->dg_tail)
                    sock->dg_tail->next = d;
                else
                    sock->dg_head = d;
                sock->dg_tail = d;
                sock->dg_bytes += size;
                data_arrived(sock);
            }
        }
        if (!broadcast)
            break;
    }
    /* nobody listens: the port unreachable answer resets the sender's next receive. */
    if (!delivered && !broadcast) {
        sender->udp_reset = TRUE;
        data_arrived(sender);
    }
}

static int dgram_send(WS_SOCKET *sock, const WSABUF *buffers, DWORD count, DWORD *sent, const SOCKADDR *to,
                      int tolen)
{
    WS_ENDPOINT dest;
    if (to) {
        int error = to_endpoint(sock, to, tolen, &dest);
        if (error)
            return error;
    } else if (sock->has_remote) {
        dest = sock->remote;
    } else {
        return WSAENOTCONN;
    }
    if (sock->shut_wr)
        return WSAESHUTDOWN;
    DWORD size = buffers_size(buffers, count);
    if (size > (DWORD)(ep_is_v4(&dest) ? WS_MAX_UDP_V4 : WS_MAX_UDP_V6))
        return WSAEMSGSIZE;
    if (ep_is_any(&dest) || !dest.port)
        return WSAEADDRNOTAVAIL;
    if (ep_is_broadcast(&dest) && !sock->broadcast)
        return WSAEACCES;
    if (!ep_is_loopback(&dest) && !ep_is_broadcast(&dest) && !ep_is_multicast(&dest))
        return WSAENETUNREACH;
    int error = auto_bind(sock, &dest);
    if (error)
        return error;
    /* no socket joins a group without a network, so multicast goes nowhere. */
    if (!ep_is_multicast(&dest))
        deliver(sock, &dest, buffers, count, size);
    *sent = size;
    return 0;
}

static int do_send(SOCKET s, const WSABUF *buffers, DWORD count, LPDWORD sent, DWORD flags, const SOCKADDR *to,
                   int tolen, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!buffers_valid(buffers, count))
        return fail(sock, WSAEFAULT);
    if (flags & MSG_OOB)
        return fail(sock, WSAEOPNOTSUPP);
    DWORD n = 0;
    if (sock->type == SOCK_STREAM)
        error = stream_send(sock, buffers, count, &n, overlapped != NULL);
    else
        error = dgram_send(sock, buffers, count, &n, to, tolen);
    if (error)
        return fail(sock, error);
    if (sent)
        *sent = n;
    if (overlapped)
        complete(sock, overlapped, routine, NULL, STATUS_SUCCESS, n, 0);
    release(sock);
    return 0;
}

int WSAAPI send(SOCKET s, const char *buf, int len, int flags)
{
    if (len < 0)
        return WsFail(WSAEFAULT);
    WSABUF b = {(ULONG)len, (CHAR *)buf};
    DWORD sent = 0;
    if (do_send(s, &b, 1, &sent, (DWORD)flags, NULL, 0, NULL, NULL))
        return SOCKET_ERROR;
    return (int)sent;
}

int WSAAPI sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    if (len < 0)
        return WsFail(WSAEFAULT);
    WSABUF b = {(ULONG)len, (CHAR *)buf};
    DWORD sent = 0;
    if (do_send(s, &b, 1, &sent, (DWORD)flags, to, tolen, NULL, NULL))
        return SOCKET_ERROR;
    return (int)sent;
}

int WSAAPI WSASend(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags, LPWSAOVERLAPPED overlapped,
                   LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return do_send(s, buffers, count, sent, flags, NULL, 0, overlapped, routine);
}

int WSAAPI WSASendTo(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags, const struct sockaddr *to,
                     int tolen, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return do_send(s, buffers, count, sent, flags, to, tolen, overlapped, routine);
}

/* ------------------------------------------------------------------ receiving */

static int queue_request(WS_SOCKET *sock, const WSABUF *buffers, DWORD count, DWORD flags, struct sockaddr *from,
                         LPINT fromlen, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    WS_REQUEST *r = WsAllocZero(sizeof(*r) + count * sizeof(WSABUF));
    if (!r)
        return WSAENOBUFS;
    /* the completion routine runs on the calling thread, which needs a real handle. */
    if (routine && !NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), NtCurrentThread(), NtCurrentProcess(),
                                                 &r->thread, 0, 0, DUPLICATE_SAME_ACCESS))) {
        WsFree(r);
        return WSAENOBUFS;
    }
    r->overlapped = overlapped;
    r->routine = routine;
    r->flags = flags;
    r->from = from;
    r->fromlen = fromlen;
    r->count = count;
    memcpy(r->buffers, buffers, count * sizeof(WSABUF));
    overlapped->Internal = (ULONG_PTR)STATUS_PENDING;
    overlapped->InternalHigh = 0;
    ResetEvent(overlapped_event(sock, overlapped));
    WS_REQUEST **tail = &sock->requests;
    while (*tail)
        tail = &(*tail)->next;
    *tail = r;
    return WSA_IO_PENDING;
}

static int do_recv(SOCKET s, const WSABUF *buffers, DWORD count, LPDWORD received, LPDWORD flags,
                   struct sockaddr *from, LPINT fromlen, LPWSAOVERLAPPED overlapped,
                   LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    DWORD in_flags = flags ? *flags : 0;
    if (!buffers_valid(buffers, count) || !flags)
        return fail(sock, WSAEFAULT);
    if (from && (!fromlen || *fromlen < sockaddr_size(sock->family)))
        return fail(sock, WSAEFAULT);
    sock->enabled |= FD_READ;
    if (in_flags & MSG_OOB) {
        signal_events(sock);
        return fail(sock, WSAEINVAL);
    }
    if (sock->type == SOCK_STREAM) {
        if (sock->state == WS_LISTENING || sock->state == WS_OPEN)
            error = WSAENOTCONN;
        else if (sock->shut_rd)
            error = WSAESHUTDOWN;
    } else if (!sock->bound) {
        error = WSAEINVAL;
    }
    if (error) {
        signal_events(sock);
        return fail(sock, error);
    }
    ULONGLONG deadline = deadline_after(sock->rcvtimeo);
    DWORD n = 0, out_flags = 0;
    WS_ENDPOINT source;
    for (;;) {
        BOOL done;
        if (sock->type == SOCK_STREAM) {
            done = stream_take(sock, buffers, count, in_flags, &n, &error);
            /* MSG_WAITALL keeps a blocking call waiting for the whole buffer or the end of the stream. */
            if (done && !error && n && (in_flags & MSG_WAITALL) && !(in_flags & MSG_PEEK) && !overlapped &&
                !sock->nonblocking && count <= WS_MAX_BUFFERS) {
                DWORD want = buffers_size(buffers, count);
                while (n < want && !sock->rx_fin && !sock->reset) {
                    if (wait_change(sock, deadline))
                        break;
                    WSABUF rest[WS_MAX_BUFFERS];
                    DWORD more = 0, k = remaining_buffers(buffers, count, n, rest);
                    int ignored;
                    stream_take(sock, rest, k, in_flags, &more, &ignored);
                    n += more;
                }
            }
        } else {
            done = dgram_take(sock, buffers, count, in_flags, &n, &source, &error);
            if (done && from && error != WSAECONNRESET)
                from_endpoint(sock, &source, from, fromlen);
        }
        if (done)
            break;
        if (overlapped) {
            error = queue_request(sock, buffers, count, in_flags, from, fromlen, overlapped, routine);
            signal_events(sock);
            return fail(sock, error);
        }
        if (sock->nonblocking) {
            error = WSAEWOULDBLOCK;
            break;
        }
        if ((error = wait_change(sock, deadline)))
            break;
        if (sock->type == SOCK_STREAM && sock->shut_rd) {
            error = WSAESHUTDOWN;
            break;
        }
    }
    if (error == WSAEMSGSIZE)
        out_flags = MSG_PARTIAL;
    signal_events(sock);
    if (error && error != WSAEMSGSIZE)
        return fail(sock, error);
    *flags = out_flags;
    if (received)
        *received = n;
    if (overlapped)
        complete(sock, overlapped, routine, NULL, error ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS, n, out_flags);
    if (error)
        return fail(sock, error);
    release(sock);
    return 0;
}

int WSAAPI recv(SOCKET s, char *buf, int len, int flags)
{
    if (len < 0)
        return WsFail(WSAEFAULT);
    WSABUF b = {(ULONG)len, buf};
    DWORD received = 0, f = (DWORD)flags;
    if (do_recv(s, &b, 1, &received, &f, NULL, NULL, NULL, NULL))
        return SOCKET_ERROR;
    return (int)received;
}

int WSAAPI recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    if (len < 0)
        return WsFail(WSAEFAULT);
    WSABUF b = {(ULONG)len, buf};
    DWORD received = 0, f = (DWORD)flags;
    if (do_recv(s, &b, 1, &received, &f, from, fromlen, NULL, NULL))
        return SOCKET_ERROR;
    return (int)received;
}

int WSAAPI WSARecv(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                   LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return do_recv(s, buffers, count, received, flags, NULL, NULL, overlapped, routine);
}

int WSAAPI WSARecvFrom(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                       struct sockaddr *from, LPINT fromlen, LPWSAOVERLAPPED overlapped,
                       LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return do_recv(s, buffers, count, received, flags, from, fromlen, overlapped, routine);
}

BOOL WSAAPI WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED overlapped, LPDWORD transferred, BOOL wait,
                                   LPDWORD flags)
{
    if (!WsIsSocket(s)) {
        SetLastError(WSAENOTSOCK);
        return FALSE;
    }
    if (!overlapped || !transferred || !flags) {
        SetLastError(WSAEFAULT);
        return FALSE;
    }
    HANDLE event = (HANDLE)((ULONG_PTR)overlapped->hEvent & ~(ULONG_PTR)1);
    if (!event)
        event = (HANDLE)s;
    while ((NTSTATUS)overlapped->Internal == STATUS_PENDING) {
        if (!wait) {
            SetLastError(WSA_IO_INCOMPLETE);
            return FALSE;
        }
        if (WaitForSingleObject(event, INFINITE) == WAIT_FAILED)
            return FALSE;
    }
    *transferred = (DWORD)overlapped->InternalHigh;
    *flags = overlapped->Offset;
    DWORD error = status_error((NTSTATUS)overlapped->Internal);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ select and poll */

static ULONGLONG timeval_deadline(const struct timeval *timeout, BOOL *poll_only)
{
    *poll_only = FALSE;
    if (!timeout)
        return 0;
    ULONGLONG ms = (ULONGLONG)(timeout->tv_sec > 0 ? timeout->tv_sec : 0) * 1000 +
                   (ULONGLONG)(timeout->tv_usec > 0 ? (timeout->tv_usec + 999) / 1000 : 0);
    if (!ms) {
        *poll_only = TRUE;
        return 0;
    }
    return GetTickCount64() + ms;
}

static int set_count(const fd_set *set)
{
    return set ? (int)set->fd_count : 0;
}

int WSAAPI select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!set_count(readfds) && !set_count(writefds) && !set_count(exceptfds))
        return WsFail(WSAEINVAL);
    BOOL poll_only;
    ULONGLONG deadline = timeval_deadline(timeout, &poll_only);
    fd_set *sets[3] = {readfds, writefds, exceptfds};
    static const int wanted[3] = {WS_READABLE, WS_WRITABLE, WS_FAILED};
    EnterCriticalSection(&lock);
    for (;;) {
        int total = 0;
        u_int kept[3] = {0, 0, 0};
        for (int k = 0; k < 3; k++) {
            for (u_int i = 0; sets[k] && i < sets[k]->fd_count; i++) {
                WS_SOCKET *sock = find(sets[k]->fd_array[i]);
                if (!sock) {
                    LeaveCriticalSection(&lock);
                    return WsFail(WSAENOTSOCK);
                }
                if (readiness(sock) & wanted[k])
                    kept[k]++;
            }
            total += kept[k];
        }
        if (total || poll_only) {
            for (int k = 0; k < 3; k++) {
                if (!sets[k])
                    continue;
                u_int out = 0;
                for (u_int i = 0; i < sets[k]->fd_count; i++) {
                    WS_SOCKET *sock = find(sets[k]->fd_array[i]);
                    if (readiness(sock) & wanted[k])
                        sets[k]->fd_array[out++] = sets[k]->fd_array[i];
                }
                sets[k]->fd_count = out;
            }
            LeaveCriticalSection(&lock);
            return total;
        }
        if ((error = wait_change(NULL, deadline))) {
            for (int k = 0; k < 3; k++) {
                if (sets[k])
                    sets[k]->fd_count = 0;
            }
            LeaveCriticalSection(&lock);
            return 0;
        }
    }
}

static SHORT poll_events(const WS_SOCKET *sock, SHORT events)
{
    int r = readiness(sock);
    SHORT revents = 0;
    if ((r & WS_READABLE) && (events & POLLRDNORM))
        revents |= POLLRDNORM;
    if ((r & WS_WRITABLE) && (events & POLLWRNORM))
        revents |= POLLWRNORM;
    if (sock->type == SOCK_STREAM && sock->state == WS_CONNECTED && (sock->rx_fin || sock->reset) &&
        !rx_available(sock))
        revents |= POLLHUP;
    if (sock->reset || (sock->state == WS_OPEN && sock->connect_failed))
        revents |= POLLERR;
    if (sock->state == WS_OPEN && sock->connect_failed)
        revents |= POLLHUP;
    return revents;
}

int WSAAPI WSAPoll(LPWSAPOLLFD fds, ULONG count, INT timeout)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!fds)
        return WsFail(WSAEFAULT);
    for (ULONG i = 0; i < count; i++) {
        if (fds[i].events & ~(POLLRDNORM | POLLRDBAND | POLLPRI | POLLWRNORM | POLLWRBAND))
            return WsFail(WSAEINVAL);
    }
    ULONGLONG deadline = timeout > 0 ? GetTickCount64() + (ULONGLONG)timeout : 0;
    EnterCriticalSection(&lock);
    for (;;) {
        int total = 0;
        for (ULONG i = 0; i < count; i++) {
            fds[i].revents = 0;
            if (fds[i].fd == INVALID_SOCKET)
                continue;
            WS_SOCKET *sock = find(fds[i].fd);
            fds[i].revents = sock ? poll_events(sock, fds[i].events) : POLLNVAL;
            if (fds[i].revents)
                total++;
        }
        if (total || !timeout || wait_change(NULL, deadline)) {
            LeaveCriticalSection(&lock);
            return total;
        }
    }
}

/* ------------------------------------------------------------------ WSAEventSelect and WSAAsyncSelect */

int WSAAPI WSAEventSelect(SOCKET s, WSAEVENT event, long events)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (events && !event)
        return fail(sock, WSAEINVAL);
    sock->hwnd = NULL;
    sock->msg = 0;
    sock->event = events ? event : NULL;
    sock->select_events = events;
    sock->enabled = FD_ALL_EVENTS;
    if (events)
        sock->nonblocking = TRUE;
    signal_events(sock);
    release(sock);
    return 0;
}

int WSAAPI WSAAsyncSelect(SOCKET s, HWND hwnd, u_int msg, long events)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (events && !IsWindow(hwnd))
        return fail(sock, WSAEINVAL);
    sock->event = NULL;
    sock->hwnd = events ? hwnd : NULL;
    sock->msg = msg;
    sock->select_events = events;
    sock->enabled = FD_ALL_EVENTS;
    sock->nonblocking = TRUE;
    signal_events(sock);
    release(sock);
    return 0;
}

int WSAAPI WSAEnumNetworkEvents(SOCKET s, WSAEVENT event, LPWSANETWORKEVENTS events)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!events)
        return fail(sock, WSAEFAULT);
    events->lNetworkEvents = sock->recorded;
    for (int i = 0; i < FD_MAX_EVENTS; i++)
        events->iErrorCode[i] = (sock->recorded & (1 << i)) ? sock->errors[i] : 0;
    sock->recorded = 0;
    if (event)
        ResetEvent(event);
    release(sock);
    return 0;
}

/* ------------------------------------------------------------------ ioctls */

int WSAAPI ioctlsocket(SOCKET s, long cmd, u_long *argp)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!argp)
        return fail(sock, WSAEFAULT);
    switch ((u_long)cmd) {
    case FIONBIO:
        /* a socket under WSAAsyncSelect or WSAEventSelect stays non blocking. */
        if (!*argp && sock->select_events)
            return fail(sock, WSAEINVAL);
        sock->nonblocking = *argp != 0;
        break;
    case FIONREAD:
        if (sock->type == SOCK_STREAM)
            *argp = sock->state == WS_CONNECTED ? (u_long)min(rx_available(sock), (SIZE_T)0xFFFFFFFF) : 0;
        else
            *argp = (u_long)sock->dg_bytes;
        break;
    case SIOCATMARK:
        *argp = TRUE;
        break;
    default:
        return fail(sock, WSAEINVAL);
    }
    release(sock);
    return 0;
}

/* SIO_GET_INTERFACE_LIST: the loopback interface of the socket's family. */
static int interface_list(const WS_SOCKET *sock, LPVOID out, DWORD out_size, LPDWORD returned)
{
    if (out_size < sizeof(INTERFACE_INFO)) {
        *returned = sizeof(INTERFACE_INFO);
        return WSAEFAULT;
    }
    INTERFACE_INFO *info = out;
    memset(info, 0, sizeof(*info));
    info->iiFlags = IFF_UP | IFF_LOOPBACK | IFF_MULTICAST;
    if (sock->family == AF_INET) {
        info->iiAddress.AddressIn.sin_family = AF_INET;
        info->iiAddress.AddressIn.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
        info->iiBroadcastAddress.AddressIn.sin_family = AF_INET;
        info->iiBroadcastAddress.AddressIn.sin_addr.S_un.S_addr = INADDR_BROADCAST;
        info->iiNetmask.AddressIn.sin_family = AF_INET;
        info->iiNetmask.AddressIn.sin_addr.S_un.S_addr = htonl(0xFF000000);
    } else {
        info->iiAddress.AddressIn6.sin6_family = AF_INET6;
        info->iiAddress.AddressIn6.sin6_addr.u.Byte[15] = 1;
        info->iiNetmask.AddressIn6.sin6_family = AF_INET6;
        memset(info->iiNetmask.AddressIn6.sin6_addr.u.Byte, 0xFF, 16);
    }
    *returned = sizeof(INTERFACE_INFO);
    return 0;
}

static int routing_query(const WS_SOCKET *sock, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                         LPDWORD returned)
{
    WS_ENDPOINT dest;
    int error = to_endpoint(sock, in, (int)in_size, &dest);
    if (error)
        return error;
    if (!ep_is_loopback(&dest))
        return WSAENETUNREACH;
    WS_ENDPOINT source = dest;
    ep_loopback(&source, ep_is_v4(&dest));
    source.port = 0;
    int len = (int)out_size;
    if ((error = from_endpoint(sock, &source, out, &len))) {
        *returned = (DWORD)sockaddr_size(sock->family);
        return error;
    }
    *returned = (DWORD)len;
    return 0;
}

int WSAAPI WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size, LPDWORD returned,
                    LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!returned)
        return fail(sock, WSAEFAULT);
    DWORD size = 0;
    switch (code) {
    case FIONBIO:
        if (!in || in_size < sizeof(u_long))
            error = WSAEFAULT;
        else if (!*(u_long *)in && sock->select_events)
            error = WSAEINVAL;
        else
            sock->nonblocking = *(u_long *)in != 0;
        break;
    case FIONREAD:
    case SIOCATMARK:
        if (!out || out_size < sizeof(u_long)) {
            error = WSAEFAULT;
            break;
        }
        if (code == SIOCATMARK)
            *(u_long *)out = TRUE;
        else if (sock->type == SOCK_STREAM)
            *(u_long *)out = sock->state == WS_CONNECTED ? (u_long)min(rx_available(sock), (SIZE_T)0xFFFFFFFF) : 0;
        else
            *(u_long *)out = (u_long)sock->dg_bytes;
        size = sizeof(u_long);
        break;
    case SIO_UDP_CONNRESET:
        if (!in || in_size < sizeof(BOOL))
            error = WSAEFAULT;
        else if (sock->type != SOCK_DGRAM)
            error = WSAEINVAL;
        else
            sock->connreset = *(BOOL *)in != 0;
        break;
    case SIO_KEEPALIVE_VALS:
        if (!in || in_size < sizeof(struct tcp_keepalive))
            error = WSAEFAULT;
        else
            sock->keepalive = ((struct tcp_keepalive *)in)->onoff != 0;
        break;
    case SIO_UDP_NETRESET:
    case SIO_LOOPBACK_FAST_PATH:
        if (!in || in_size < sizeof(BOOL))
            error = WSAEFAULT;
        break;
    case SIO_BASE_HANDLE:
    case SIO_BSP_HANDLE:
    case SIO_BSP_HANDLE_SELECT:
    case SIO_BSP_HANDLE_POLL:
        if (!out || out_size < sizeof(SOCKET)) {
            error = WSAEFAULT;
            break;
        }
        *(SOCKET *)out = sock->handle;
        size = sizeof(SOCKET);
        break;
    case SIO_GET_INTERFACE_LIST:
        error = out ? interface_list(sock, out, out_size, &size) : WSAEFAULT;
        break;
    case SIO_ADDRESS_LIST_QUERY:
        /* the list holds the non loopback addresses, of which there are none. */
        size = offsetof(SOCKET_ADDRESS_LIST, Address);
        if (!out || out_size < size)
            error = WSAEFAULT;
        else
            ((SOCKET_ADDRESS_LIST *)out)->iAddressCount = 0;
        break;
    case SIO_ROUTING_INTERFACE_QUERY:
        error = in && out ? routing_query(sock, in, in_size, out, out_size, &size) : WSAEFAULT;
        break;
    case SIO_GET_EXTENSION_FUNCTION_POINTER:
        /* the mswsock extensions are not provided; callers fall back to the plain functions. */
        error = WSAEINVAL;
        break;
    default:
        error = WSAEOPNOTSUPP;
        break;
    }
    *returned = size;
    if (error)
        return fail(sock, error);
    if (overlapped)
        complete(sock, overlapped, routine, NULL, STATUS_SUCCESS, size, 0);
    release(sock);
    return 0;
}

/* ------------------------------------------------------------------ protocol information */

static const GUID provider_v4 = {0xE70F1AA0, 0xAB8B, 0x11CF, {0x8C, 0xA3, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};
static const GUID provider_v6 = {0xF9EAB0C0, 0x26D4, 0x11D0, {0xBB, 0xBF, 0x00, 0xAA, 0x00, 0x6C, 0x34, 0xE4}};

BOOL WsProtocolInfo(int family, int type, WSAPROTOCOL_INFOW *info)
{
    if ((family != AF_INET && family != AF_INET6) || (type != SOCK_STREAM && type != SOCK_DGRAM))
        return FALSE;
    memset(info, 0, sizeof(*info));
    BOOL stream = type == SOCK_STREAM, v4 = family == AF_INET;
    if (stream) {
        info->dwServiceFlags1 = XP1_GUARANTEED_DELIVERY | XP1_GUARANTEED_ORDER | XP1_GRACEFUL_CLOSE |
                                XP1_EXPEDITED_DATA | XP1_IFS_HANDLES;
    } else {
        info->dwServiceFlags1 = XP1_CONNECTIONLESS | XP1_MESSAGE_ORIENTED | XP1_SUPPORT_BROADCAST |
                                XP1_SUPPORT_MULTIPOINT | XP1_IFS_HANDLES;
        info->dwMessageSize = v4 ? WS_MAX_UDP_V4 : WS_MAX_UDP_V6;
    }
    info->dwProviderFlags = PFL_MATCHES_PROTOCOL_ZERO;
    info->ProviderId = v4 ? provider_v4 : provider_v6;
    info->dwCatalogEntryId = 1001 + (v4 ? 0 : 3) + (stream ? 0 : 1);
    info->ProtocolChain.ChainLen = BASE_PROTOCOL;
    info->iVersion = 2;
    info->iAddressFamily = family;
    info->iMaxSockAddr = sockaddr_size(family);
    info->iMinSockAddr = sockaddr_size(family);
    info->iSocketType = type;
    info->iProtocol = stream ? IPPROTO_TCP : IPPROTO_UDP;
    info->iNetworkByteOrder = BIGENDIAN;
    info->iSecurityScheme = SECURITY_PROTOCOL_NONE;
    const char *name = stream ? (v4 ? "MSAFD Tcpip [TCP/IP]" : "MSAFD Tcpip [TCP/IPv6]")
                              : (v4 ? "MSAFD Tcpip [UDP/IP]" : "MSAFD Tcpip [UDP/IPv6]");
    WsWiden(name, info->szProtocol, RTL_NUMBER_OF(info->szProtocol));
    return TRUE;
}

static void protocol_info_a(const WSAPROTOCOL_INFOW *w, WSAPROTOCOL_INFOA *a)
{
    memcpy(a, w, offsetof(WSAPROTOCOL_INFOA, szProtocol));
    WsNarrow(w->szProtocol, a->szProtocol, sizeof(a->szProtocol));
}

static const int protocol_order[][2] = {
    {AF_INET, SOCK_STREAM},
    {AF_INET, SOCK_DGRAM},
    {AF_INET6, SOCK_STREAM},
    {AF_INET6, SOCK_DGRAM},
};

static BOOL protocol_wanted(const INT *protocols, int protocol)
{
    if (!protocols)
        return TRUE;
    for (; *protocols; protocols++) {
        if (*protocols == protocol)
            return TRUE;
    }
    return FALSE;
}

static int enum_protocols(LPINT protocols, void *buffer, LPDWORD length, BOOL wide)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!length)
        return WsFail(WSAEFAULT);
    WSAPROTOCOL_INFOW list[RTL_NUMBER_OF(protocol_order)];
    int n = 0;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(protocol_order); i++) {
        WsProtocolInfo(protocol_order[i][0], protocol_order[i][1], &list[n]);
        if (protocol_wanted(protocols, list[n].iProtocol))
            n++;
    }
    DWORD each = wide ? sizeof(WSAPROTOCOL_INFOW) : sizeof(WSAPROTOCOL_INFOA);
    if (!buffer || *length < each * n) {
        *length = each * n;
        return WsFail(WSAENOBUFS);
    }
    for (int i = 0; i < n; i++) {
        if (wide)
            ((WSAPROTOCOL_INFOW *)buffer)[i] = list[i];
        else
            protocol_info_a(&list[i], &((WSAPROTOCOL_INFOA *)buffer)[i]);
    }
    return n;
}

int WSAAPI WSAEnumProtocolsW(LPINT protocols, LPWSAPROTOCOL_INFOW buffer, LPDWORD length)
{
    return enum_protocols(protocols, buffer, length, TRUE);
}

int WSAAPI WSAEnumProtocolsA(LPINT protocols, LPWSAPROTOCOL_INFOA buffer, LPDWORD length)
{
    return enum_protocols(protocols, buffer, length, FALSE);
}

/* ------------------------------------------------------------------ options */

static int read_int(const char *value, int length, int *out)
{
    if (!value || length < 1)
        return WSAEFAULT;
    if (length >= (int)sizeof(int))
        memcpy(out, value, sizeof(int));
    else
        *out = (UCHAR)value[0];
    return 0;
}

static int *extra_option(WS_SOCKET *sock, int level, int name, BOOL create)
{
    for (int i = 0; i < sock->extra_count; i++) {
        if (sock->extra[i].level == level && sock->extra[i].name == name)
            return &sock->extra[i].value;
    }
    if (!create || sock->extra_count == WS_EXTRA_OPTIONS)
        return NULL;
    sock->extra[sock->extra_count].level = level;
    sock->extra[sock->extra_count].name = name;
    sock->extra[sock->extra_count].value = 0;
    return &sock->extra[sock->extra_count++].value;
}

/* the value of options that are accepted and remembered without effect on the loopback. */
static BOOL inert_option(int level, int name, int *initial)
{
    switch (level) {
    case SOL_SOCKET:
        switch (name) {
        case SO_DEBUG:
        case SO_DONTROUTE:
        case SO_USELOOPBACK:
        case SO_CONDITIONAL_ACCEPT:
        case SO_RANDOMIZE_PORT:
        case SO_PORT_SCALABILITY:
        case SO_REUSE_UNICASTPORT:
        case SO_REUSE_MULTICASTPORT:
        case SO_SNDLOWAT:
            *initial = 0;
            return TRUE;
        case SO_RCVLOWAT:
            *initial = 1;
            return TRUE;
        }
        return FALSE;
    case IPPROTO_TCP:
        switch (name) {
        case TCP_KEEPALIVE:
            *initial = 7200;
            return TRUE;
        case TCP_KEEPINTVL:
            *initial = 1;
            return TRUE;
        case TCP_KEEPCNT:
            *initial = 10;
            return TRUE;
        case TCP_MAXSEG:
            *initial = 65495;
            return TRUE;
        case TCP_EXPEDITED_1122:
        case TCP_TIMESTAMPS:
        case TCP_FASTOPEN:
        case TCP_MAXRT:
        case TCP_NOSYNRETRIES:
        case TCP_CONGESTION_ALGORITHM:
        case TCP_DELAY_FIN_ACK:
        case TCP_FAIL_CONNECT_ON_ICMP_ERROR:
            *initial = 0;
            return TRUE;
        }
        return FALSE;
    case IPPROTO_IP:
    case IPPROTO_IPV6:
        switch (name) {
        case IP_TTL:
            *initial = 128;
            return TRUE;
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
            *initial = 1;
            return TRUE;
        case IP_TOS:
        case IP_MULTICAST_IF:
        case IP_DONTFRAGMENT:
        case IP_PKTINFO:
        case IP_RECVTTL:
        case IP_RECEIVE_BROADCAST:
        case IP_RECVIF:
        case IP_RECVDSTADDR:
        case IP_UNICAST_IF:
        case IP_RECVTCLASS:
        case IP_ECN:
        case IP_MTU_DISCOVER:
        case IP_RECVERR:
            *initial = 0;
            return TRUE;
        }
        return FALSE;
    case IPPROTO_UDP:
        *initial = 0;
        return name == 1 || name == 2;
    }
    return FALSE;
}

static BOOL membership_option(int level, int name)
{
    if (level == IPPROTO_IP)
        return name == IP_ADD_MEMBERSHIP || name == IP_DROP_MEMBERSHIP || name == IP_ADD_SOURCE_MEMBERSHIP ||
               name == IP_DROP_SOURCE_MEMBERSHIP || name == IP_BLOCK_SOURCE || name == IP_UNBLOCK_SOURCE;
    return level == IPPROTO_IPV6 && (name == IPV6_ADD_MEMBERSHIP || name == IPV6_DROP_MEMBERSHIP);
}

int WSAAPI setsockopt(SOCKET s, int level, int name, const char *value, int length)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    int v = 0, initial;
    if (level != SOL_SOCKET && level != IPPROTO_TCP && level != IPPROTO_IP && level != IPPROTO_IPV6 &&
        level != IPPROTO_UDP)
        return fail(sock, WSAEINVAL);
    if (membership_option(level, name)) {
        /* joining a group is accepted; with no network nothing is ever sent to it. */
        if (sock->type != SOCK_DGRAM)
            error = WSAEINVAL;
        else if (!value || length < (int)(level == IPPROTO_IP ? sizeof(IP_MREQ) : sizeof(IPV6_MREQ)))
            error = WSAEFAULT;
        if (error)
            return fail(sock, error);
        release(sock);
        return 0;
    }
    if (level == SOL_SOCKET && name == SO_LINGER) {
        if (!value || length < (int)sizeof(LINGER))
            return fail(sock, WSAEFAULT);
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        memcpy(&sock->linger, value, sizeof(LINGER));
        release(sock);
        return 0;
    }
    if ((error = read_int(value, length, &v)))
        return fail(sock, error);
    switch (level == SOL_SOCKET ? name : -1) {
    case SO_REUSEADDR:
        if (sock->exclusive && v)
            return fail(sock, WSAEINVAL);
        sock->reuseaddr = v != 0;
        break;
    case SO_EXCLUSIVEADDRUSE:
        if (sock->bound)
            return fail(sock, WSAEINVAL);
        if (sock->reuseaddr && v)
            return fail(sock, WSAEINVAL);
        sock->exclusive = v != 0;
        break;
    case SO_KEEPALIVE:
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        sock->keepalive = v != 0;
        break;
    case SO_BROADCAST:
        if (sock->type != SOCK_DGRAM)
            return fail(sock, WSAENOPROTOOPT);
        sock->broadcast = v != 0;
        break;
    case SO_OOBINLINE:
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        sock->oobinline = v != 0;
        break;
    case SO_DONTLINGER:
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        sock->linger.l_onoff = v == 0;
        break;
    case SO_SNDBUF:
        sock->sndbuf = v;
        break;
    case SO_RCVBUF:
        sock->rcvbuf = v;
        break;
    case SO_SNDTIMEO:
        sock->sndtimeo = (DWORD)v;
        break;
    case SO_RCVTIMEO:
        sock->rcvtimeo = (DWORD)v;
        break;
    case SO_ERROR:
        sock->so_error = v;
        break;
    case SO_UPDATE_ACCEPT_CONTEXT:
    case SO_UPDATE_CONNECT_CONTEXT:
        break;
    case SO_TYPE:
    case SO_ACCEPTCONN:
    case SO_MAX_MSG_SIZE:
    case SO_PROTOCOL_INFOA:
    case SO_PROTOCOL_INFOW:
    case SO_CONNECT_TIME:
        return fail(sock, WSAENOPROTOOPT);
    default:
        if (level == IPPROTO_TCP && name == TCP_NODELAY) {
            if (sock->type != SOCK_STREAM)
                return fail(sock, WSAENOPROTOOPT);
            sock->nodelay = v != 0;
        } else if (level == IPPROTO_IPV6 && name == IPV6_V6ONLY) {
            if (sock->family != AF_INET6)
                return fail(sock, WSAEINVAL);
            if (sock->bound)
                return fail(sock, WSAEINVAL);
            sock->v6only = v != 0;
        } else if (inert_option(level, name, &initial)) {
            if ((level == IPPROTO_TCP) != (sock->type == SOCK_STREAM) && level != SOL_SOCKET &&
                level != IPPROTO_IP && level != IPPROTO_IPV6)
                return fail(sock, WSAENOPROTOOPT);
            int *slot = extra_option(sock, level, name, TRUE);
            if (!slot)
                return fail(sock, WSAENOBUFS);
            *slot = v;
        } else {
            return fail(sock, WSAENOPROTOOPT);
        }
        break;
    }
    release(sock);
    return 0;
}

static int write_int(char *value, int *length, int v)
{
    if (!value || !length || *length < (int)sizeof(int))
        return WSAEFAULT;
    memcpy(value, &v, sizeof(int));
    *length = sizeof(int);
    return 0;
}

int WSAAPI getsockopt(SOCKET s, int level, int name, char *value, int *length)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!value || !length)
        return fail(sock, WSAEFAULT);
    int v = 0, initial;
    if (level == SOL_SOCKET) {
        switch (name) {
        case SO_LINGER:
            if (sock->type != SOCK_STREAM)
                return fail(sock, WSAENOPROTOOPT);
            if (*length < (int)sizeof(LINGER))
                return fail(sock, WSAEFAULT);
            memcpy(value, &sock->linger, sizeof(LINGER));
            *length = sizeof(LINGER);
            release(sock);
            return 0;
        case SO_PROTOCOL_INFOW:
        case SO_PROTOCOL_INFOA: {
            WSAPROTOCOL_INFOW info;
            WsProtocolInfo(sock->family, sock->type, &info);
            int need = name == SO_PROTOCOL_INFOW ? (int)sizeof(WSAPROTOCOL_INFOW) : (int)sizeof(WSAPROTOCOL_INFOA);
            if (*length < need) {
                *length = need;
                return fail(sock, WSAEFAULT);
            }
            if (name == SO_PROTOCOL_INFOW)
                memcpy(value, &info, sizeof(info));
            else
                protocol_info_a(&info, (WSAPROTOCOL_INFOA *)value);
            *length = need;
            release(sock);
            return 0;
        }
        case SO_TYPE:
            v = sock->type;
            break;
        case SO_ERROR:
            v = sock->so_error;
            break;
        case SO_ACCEPTCONN:
            v = sock->state == WS_LISTENING;
            break;
        case SO_REUSEADDR:
            v = sock->reuseaddr;
            break;
        case SO_EXCLUSIVEADDRUSE:
            v = sock->exclusive;
            break;
        case SO_KEEPALIVE:
            v = sock->keepalive;
            break;
        case SO_BROADCAST:
            v = sock->broadcast;
            break;
        case SO_OOBINLINE:
            v = sock->oobinline;
            break;
        case SO_DONTLINGER:
            v = !sock->linger.l_onoff;
            break;
        case SO_SNDBUF:
            v = sock->sndbuf;
            break;
        case SO_RCVBUF:
            v = sock->rcvbuf;
            break;
        case SO_SNDTIMEO:
            v = (int)sock->sndtimeo;
            break;
        case SO_RCVTIMEO:
            v = (int)sock->rcvtimeo;
            break;
        case SO_MAX_MSG_SIZE:
            if (sock->type != SOCK_DGRAM)
                return fail(sock, WSAENOPROTOOPT);
            v = sock->family == AF_INET ? WS_MAX_UDP_V4 : WS_MAX_UDP_V6;
            break;
        case SO_CONNECT_TIME:
            v = sock->state == WS_CONNECTED ? (int)((GetTickCount64() - sock->connect_time) / 1000) : -1;
            break;
        default:
            if (!inert_option(level, name, &initial))
                return fail(sock, WSAENOPROTOOPT);
            int *slot = extra_option(sock, level, name, FALSE);
            v = slot ? *slot : initial;
            break;
        }
    } else if (level == IPPROTO_TCP && name == TCP_NODELAY) {
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        v = sock->nodelay;
    } else if (level == IPPROTO_IPV6 && name == IPV6_V6ONLY) {
        if (sock->family != AF_INET6)
            return fail(sock, WSAEINVAL);
        v = sock->v6only;
    } else if (inert_option(level, name, &initial)) {
        int *slot = extra_option(sock, level, name, FALSE);
        v = slot ? *slot : initial;
    } else {
        return fail(sock, level == IPPROTO_TCP || level == IPPROTO_IP || level == IPPROTO_IPV6 ||
                                  level == IPPROTO_UDP
                              ? WSAENOPROTOOPT
                              : WSAEINVAL);
    }
    if ((error = write_int(value, length, v)))
        return fail(sock, error);
    release(sock);
    return 0;
}

/* ------------------------------------------------------------------ WSAConnect */

/* connect data and quality of service need a transport that carries them, which tcp does not. */
int WSAAPI WSAConnect(SOCKET s, const struct sockaddr *name, int namelen, LPWSABUF caller_data, LPWSABUF callee_data,
                      void *sqos, void *gqos)
{
    if (callee_data)
        callee_data->len = 0;
    return connect(s, name, namelen);
}
