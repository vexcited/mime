/*
 * sockets. a socket is a file of the kernel's \Device\Afd, which binds it
 * to a socket of the host, so the handle works with completion ports,
 * waits and handle duplication like on windows. ws2_32 keeps a record per
 * socket for what only the caller sees: timeouts, linger and the options
 * the host has no notion of, and the window of a WSAAsyncSelect.
 *
 * blocking calls start a request with the thread's event and wait for it;
 * overlapped calls hand the caller's OVERLAPPED to the kernel, which
 * completes it through its event, its completion routine or the port.
 */
#include "ws2_32_private.h"

#define WS_MAX_UDP_V4 65507
#define WS_MAX_UDP_V6 65527
#define WS_EXTRA_OPTIONS 16
#define WS_MAX_POLL 1024

typedef struct _WS_SOCKET {
    struct _WS_SOCKET *next;
    SOCKET handle;
    LONG refs;
    BOOL closed;
    int family, type, protocol;
    BOOL reuseaddr, exclusive, connreset;
    LINGER linger;
    DWORD rcvtimeo, sndtimeo;
    struct {
        int level, name, value;
    } extra[WS_EXTRA_OPTIONS];
    int extra_count;
    /* what an accepted socket inherits. */
    HANDLE select_event;
    long select_events;
    HWND hwnd;
    UINT msg;
} WS_SOCKET;

static CRITICAL_SECTION lock;
static WS_SOCKET *sockets;
static HANDLE helper;

/* WSAAsyncSelect: one thread turns the recorded events of every socket into window messages. */
static HANDLE async_event;
static HANDLE async_thread;

void WsSocketsInitialize(void)
{
    InitializeCriticalSection(&lock);
}

/* ------------------------------------------------------------------ the driver */

static HANDLE afd_open(void)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_AFD_DEVICE);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    HANDLE h;
    if (!NT_SUCCESS(NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, nullptr, 0)))
        return nullptr;
    return h;
}

/* an afd file that is no socket, for select, WSAPoll and names. */
static HANDLE helper_handle(void)
{
    EnterCriticalSection(&lock);
    if (!helper)
        helper = afd_open();
    HANDLE h = helper;
    LeaveCriticalSection(&lock);
    return h;
}

static HANDLE thread_event(void)
{
    WS_THREAD *t = WsThread();
    if (!t)
        return nullptr;
    if (!t->event)
        t->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return t->event;
}

/* a request of a blocking caller: waits for it, cancelling it after timeout milliseconds. */
static NTSTATUS afd_call(HANDLE h, ULONG code, void *in, ULONG in_len, void *out, ULONG out_len, DWORD timeout,
                         ULONG_PTR *information)
{
    HANDLE event = thread_event();
    if (!event)
        return STATUS_INSUFFICIENT_RESOURCES;
    IO_STATUS_BLOCK io;
    io.Status = STATUS_PENDING;
    io.Information = 0;
    NTSTATUS status = NtDeviceIoControlFile(h, event, nullptr, nullptr, &io, code, in, in_len, out, out_len);
    if (status == STATUS_PENDING) {
        if (WaitForSingleObject(event, timeout ? timeout : INFINITE) == WAIT_TIMEOUT) {
            IO_STATUS_BLOCK cancel;
            NtCancelIoFileEx(h, &io, &cancel);
            WaitForSingleObject(event, INFINITE);
            if (io.Status == STATUS_AFD_CANCELLED)
                io.Status = STATUS_AFD_IO_TIMEOUT;
        }
        status = io.Status;
    }
    if (information)
        *information = io.Information;
    return status;
}

typedef struct {
    LPWSAOVERLAPPED_COMPLETION_ROUTINE routine;
    LPWSAOVERLAPPED overlapped;
} WS_COMPLETION;

static void NTAPI completion_apc(PVOID context, PIO_STATUS_BLOCK io, ULONG reserved)
{
    WS_COMPLETION *c = context;
    c->routine((DWORD)MimeAfdError(io->Status), (DWORD)io->Information, c->overlapped, 0);
    WsFree(c);
}

/* an overlapped request: 0 with the bytes done at once, WSA_IO_PENDING, or the error. */
static int afd_overlapped(HANDLE h, ULONG code, void *in, ULONG in_len, LPWSAOVERLAPPED ov,
                          LPWSAOVERLAPPED_COMPLETION_ROUTINE routine, DWORD *transferred)
{
    HANDLE event = (HANDLE)((ULONG_PTR)ov->hEvent & ~(ULONG_PTR)1);
    /* the low bit of the event keeps the completion off the port, like windows. */
    PVOID context = ((ULONG_PTR)ov->hEvent & 1) ? nullptr : ov;
    PIO_APC_ROUTINE apc = nullptr;
    WS_COMPLETION *c = nullptr;
    if (routine) {
        c = WsAlloc(sizeof(*c));
        if (!c)
            return WSAENOBUFS;
        c->routine = routine;
        c->overlapped = ov;
        apc = (PIO_APC_ROUTINE)completion_apc;
        context = c;
    }
    ov->Internal = (ULONG_PTR)STATUS_PENDING;
    ov->InternalHigh = 0;
    NTSTATUS status = NtDeviceIoControlFile(h, event, apc, context, (PIO_STATUS_BLOCK)ov, code, in, in_len, nullptr, 0);
    if (status == STATUS_PENDING)
        return WSA_IO_PENDING;
    if (NT_ERROR(status))
        WsFree(c);
    if (transferred)
        *transferred = (DWORD)ov->InternalHigh;
    return MimeAfdError(status);
}

/* ------------------------------------------------------------------ the socket records */

static WS_SOCKET *find(SOCKET s)
{
    for (WS_SOCKET *sock = sockets; sock; sock = sock->next) {
        if (sock->handle == s)
            return sock;
    }
    return nullptr;
}

static WS_SOCKET *new_record(SOCKET s, int family, int type, int protocol)
{
    WS_SOCKET *sock = WsAllocZero(sizeof(*sock));
    if (!sock)
        return nullptr;
    sock->handle = s;
    sock->family = family;
    sock->type = type;
    sock->protocol = protocol;
    sock->connreset = TRUE;
    return sock;
}

static BOOL afd_state(SOCKET s, MIME_AFD_STATE *st)
{
    IO_STATUS_BLOCK io;
    NTSTATUS status = NtDeviceIoControlFile((HANDLE)s, nullptr, nullptr, nullptr, &io, IOCTL_AFD_STATE, nullptr, 0, st, sizeof(*st));
    return NT_SUCCESS(status);
}

/* references the record of s; a socket from another process or a duplicated handle gets one now. */
static WS_SOCKET *acquire(SOCKET s, int *error)
{
    *error = WsCheckStarted();
    if (*error)
        return nullptr;
    EnterCriticalSection(&lock);
    WS_SOCKET *sock = find(s);
    if (sock && !sock->closed) {
        sock->refs++;
        LeaveCriticalSection(&lock);
        return sock;
    }
    LeaveCriticalSection(&lock);
    MIME_AFD_STATE st;
    if (s == INVALID_SOCKET || !s || !afd_state(s, &st)) {
        *error = WSAENOTSOCK;
        return nullptr;
    }
    WS_SOCKET *fresh = new_record(s, (int)st.Family, (int)st.Type, (int)st.Protocol);
    if (!fresh) {
        *error = WSAENOBUFS;
        return nullptr;
    }
    EnterCriticalSection(&lock);
    sock = find(s);
    if (sock && !sock->closed) {
        WsFree(fresh);
    } else {
        sock = fresh;
        sock->next = sockets;
        sockets = sock;
    }
    sock->refs++;
    LeaveCriticalSection(&lock);
    return sock;
}

static void release(WS_SOCKET *sock)
{
    EnterCriticalSection(&lock);
    BOOL gone = !--sock->refs && sock->closed;
    LeaveCriticalSection(&lock);
    if (gone)
        WsFree(sock);
}

static int fail(WS_SOCKET *sock, int error)
{
    release(sock);
    return WsFail(error);
}

static int succeed(WS_SOCKET *sock)
{
    release(sock);
    SetLastError(0);
    return 0;
}

BOOL WsIsSocket(SOCKET s)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return FALSE;
    release(sock);
    return TRUE;
}

static NTSTATUS socket_call(WS_SOCKET *sock, ULONG code, void *in, ULONG in_len, void *out, ULONG out_len,
                            ULONG_PTR *information)
{
    return afd_call((HANDLE)sock->handle, code, in, in_len, out, out_len, 0, information);
}

static int host_option(WS_SOCKET *sock, ULONG option, BOOL set, ULONG value, ULONG *result)
{
    MIME_AFD_OPTION o = {option, set != 0, value};
    ULONG out = 0;
    NTSTATUS status = socket_call(sock, IOCTL_AFD_OPTION, &o, sizeof(o), &out, sizeof(out), nullptr);
    if (result)
        *result = out;
    return MimeAfdError(status);
}

/* ------------------------------------------------------------------ addresses */

static int sockaddr_size(int family)
{
    return family == AF_INET ? (int)sizeof(SOCKADDR_IN) : (int)sizeof(SOCKADDR_IN6);
}

/* a caller's sockaddr as the driver takes it. */
static int to_address(const WS_SOCKET *sock, const struct sockaddr *addr, int len, MIME_AFD_ADDRESS *a)
{
    if (!addr || len < (int)sizeof(SOCKADDR_IN))
        return WSAEFAULT;
    if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6)
        return WSAEAFNOSUPPORT;
    if (addr->sa_family != sock->family)
        return WSAEAFNOSUPPORT;
    int need = sockaddr_size(addr->sa_family);
    if (len < need)
        return WSAEFAULT;
    memset(a, 0, sizeof(*a));
    memcpy(a, addr, need);
    return 0;
}

static int from_address(const MIME_AFD_ADDRESS *a, struct sockaddr *addr, int *len)
{
    int need = a->Family == AF_INET6 ? (int)sizeof(SOCKADDR_IN6) : (int)sizeof(SOCKADDR_IN);
    if (!addr || !len || *len < need)
        return WSAEFAULT;
    memcpy(addr, a, need);
    *len = need;
    return 0;
}

static BOOL address_is_any(const MIME_AFD_ADDRESS *a)
{
    const SOCKADDR_IN *in = (const SOCKADDR_IN *)a;
    const SOCKADDR_IN6 *in6 = (const SOCKADDR_IN6 *)a;
    static const BYTE zero[16] = {};
    if (a->Family == AF_INET)
        return in->sin_addr.S_un.S_addr == 0;
    return !memcmp(&in6->sin6_addr, zero, 16);
}

/* ------------------------------------------------------------------ creation and closing */

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
    /* raw sockets would need privileges the host does not give. */
    return WSAESOCKTNOSUPPORT;
}

/* a new afd file with its host socket. */
static int open_socket(int af, int type, int protocol, HANDLE *out)
{
    HANDLE h = afd_open();
    if (!h)
        return WSAENETDOWN;
    MIME_AFD_CREATE c = {(ULONG)af, (ULONG)type, (ULONG)protocol};
    NTSTATUS status = afd_call(h, IOCTL_AFD_CREATE, &c, sizeof(c), nullptr, 0, 0, nullptr);
    if (!NT_SUCCESS(status)) {
        NtClose(h);
        return MimeAfdError(status);
    }
    *out = h;
    return 0;
}

static WS_SOCKET *register_socket(HANDLE h, int af, int type, int protocol)
{
    WS_SOCKET *sock = new_record((SOCKET)h, af, type, protocol);
    if (!sock)
        return nullptr;
    EnterCriticalSection(&lock);
    sock->next = sockets;
    sockets = sock;
    LeaveCriticalSection(&lock);
    return sock;
}

static SOCKET create_socket(int af, int type, int protocol, DWORD flags)
{
    int error = WsCheckStarted();
    if (!error)
        error = resolve_kind(&af, &type, &protocol);
    HANDLE h = nullptr;
    if (!error)
        error = open_socket(af, type, protocol, &h);
    if (error) {
        SetLastError(error);
        return INVALID_SOCKET;
    }
    if (!register_socket(h, af, type, protocol)) {
        NtClose(h);
        SetLastError(WSAENOBUFS);
        return INVALID_SOCKET;
    }
    SetLastError(0);
    return (SOCKET)h;
}

SOCKET WSAAPI socket(int af, int type, int protocol)
{
    return create_socket(af, type, protocol, WSA_FLAG_OVERLAPPED);
}

static void protocol_info_a(const WSAPROTOCOL_INFOW *w, WSAPROTOCOL_INFOA *a);

/* a socket WSADuplicateSocket made for this process: its handle, already here. */
static SOCKET adopt_socket(const WSAPROTOCOL_INFOW *info)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return INVALID_SOCKET;
    }
    HANDLE h = (HANDLE)(ULONG_PTR)info->dwProviderReserved;
    if (!register_socket(h, info->iAddressFamily, info->iSocketType, info->iProtocol)) {
        SetLastError(WSAENOBUFS);
        return INVALID_SOCKET;
    }
    SetLastError(0);
    return (SOCKET)h;
}

SOCKET WSAAPI WSASocketW(int af, int type, int protocol, LPWSAPROTOCOL_INFOW info, GROUP g, DWORD flags)
{
    if (info) {
        if (af == FROM_PROTOCOL_INFO && type == FROM_PROTOCOL_INFO && protocol == FROM_PROTOCOL_INFO &&
            info->dwProviderReserved)
            return adopt_socket(info);
        if (af == FROM_PROTOCOL_INFO)
            af = info->iAddressFamily;
        if (type == FROM_PROTOCOL_INFO)
            type = info->iSocketType;
        if (protocol == FROM_PROTOCOL_INFO)
            protocol = info->iProtocol;
    }
    return create_socket(af, type, protocol, flags);
}

/*
 * the socket's handle duplicated into the target process, carried in the
 * protocol information the way windows' provider carries it; WSASocket
 * there takes it back.
 */
int WSAAPI WSADuplicateSocketW(SOCKET s, DWORD process_id, LPWSAPROTOCOL_INFOW info)
{
    int error;
    if (!info)
        return WsFail(WSAEFAULT);
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, process_id);
    HANDLE copy = nullptr;
    if (!process || !DuplicateHandle(GetCurrentProcess(), (HANDLE)s, process, &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        error = WSAEINVAL;
    else if (!WsProtocolInfo(sock->family, sock->type, info))
        error = WSAEINVAL;
    if (process)
        CloseHandle(process);
    if (!error)
        info->dwProviderReserved = (DWORD)(ULONG_PTR)copy;
    release(sock);
    return error ? WsFail(error) : 0;
}

int WSAAPI WSADuplicateSocketA(SOCKET s, DWORD process_id, LPWSAPROTOCOL_INFOA info)
{
    WSAPROTOCOL_INFOW wide;
    if (!info)
        return WsFail(WSAEFAULT);
    if (WSADuplicateSocketW(s, process_id, &wide))
        return SOCKET_ERROR;
    protocol_info_a(&wide, info);
    return 0;
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

static void unlink_record(WS_SOCKET *sock)
{
    for (WS_SOCKET **p = &sockets; *p; p = &(*p)->next) {
        if (*p == sock) {
            *p = sock->next;
            return;
        }
    }
}

int WSAAPI closesocket(SOCKET s)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    /* SO_LINGER with no time resets the connection instead of closing it gracefully. */
    if (sock->type == SOCK_STREAM && sock->linger.l_onoff && !sock->linger.l_linger)
        host_option(sock, MIME_AFD_OPT_LINGER, TRUE, 1u << 16, nullptr);
    EnterCriticalSection(&lock);
    sock->closed = TRUE;
    unlink_record(sock);
    sock->refs--;
    LeaveCriticalSection(&lock);
    NtClose((HANDLE)s);
    return succeed(sock);
}

void WsCloseAllSockets(void)
{
    EnterCriticalSection(&lock);
    while (sockets) {
        WS_SOCKET *sock = sockets;
        sockets = sock->next;
        sock->closed = TRUE;
        NtClose((HANDLE)sock->handle);
        if (!sock->refs)
            WsFree(sock);
    }
    LeaveCriticalSection(&lock);
}

/* ------------------------------------------------------------------ binding and connecting */

int WSAAPI bind(SOCKET s, const struct sockaddr *name, int namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    MIME_AFD_ADDRESS a;
    if ((error = to_address(sock, name, namelen, &a)))
        return fail(sock, error);
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_BIND, &a, sizeof(a), nullptr, 0, nullptr));
    if (error == WSAEADDRINUSE && sock->exclusive)
        error = WSAEACCES;
    return error ? fail(sock, error) : succeed(sock);
}

int WSAAPI listen(SOCKET s, int backlog)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (backlog < 0)
        backlog = -backlog; /* SOMAXCONN_HINT */
    if (backlog == SOMAXCONN || backlog > 4096)
        backlog = 4096;
    ULONG b = (ULONG)backlog;
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_LISTEN, &b, sizeof(b), nullptr, 0, nullptr));
    return error ? fail(sock, error) : succeed(sock);
}

int WSAAPI connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    MIME_AFD_CONNECT c;
    memset(&c, 0, sizeof(c));
    if ((error = to_address(sock, name, namelen, &c.Address)))
        return fail(sock, error);
    if (sock->type == SOCK_STREAM && address_is_any(&c.Address))
        return fail(sock, WSAEADDRNOTAVAIL);
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_CONNECT, &c, sizeof(c), nullptr, 0, nullptr));
    return error ? fail(sock, error) : succeed(sock);
}

/* connect data and quality of service need a transport that carries them, which tcp does not. */
int WSAAPI WSAConnect(SOCKET s, const struct sockaddr *name, int namelen, LPWSABUF caller_data, LPWSABUF callee_data,
                      void *sqos, void *gqos)
{
    if (callee_data)
        callee_data->len = 0;
    return connect(s, name, namelen);
}

/* an accepted socket takes over the listener's selection, like windows. */
static void inherit(WS_SOCKET *listener, SOCKET s)
{
    if (listener->select_event)
        WSAEventSelect(s, listener->select_event, listener->select_events);
    else if (listener->hwnd)
        WSAAsyncSelect(s, listener->hwnd, listener->msg, listener->select_events);
}

SOCKET WSAAPI accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock) {
        SetLastError(error);
        return INVALID_SOCKET;
    }
    if (addr && (!addrlen || *addrlen < sockaddr_size(sock->family))) {
        fail(sock, WSAEFAULT);
        return INVALID_SOCKET;
    }
    HANDLE h;
    if ((error = open_socket(sock->family, sock->type, sock->protocol, &h))) {
        fail(sock, error);
        return INVALID_SOCKET;
    }
    MIME_AFD_ADDRESS both[2];
    memset(both, 0, sizeof(both));
    MIME_AFD_ACCEPT a = {(ULONG64)(ULONG_PTR)h, 0, 0, 0, (ULONG64)(ULONG_PTR)both};
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_ACCEPT, &a, sizeof(a), nullptr, 0, nullptr));
    if (!error && !register_socket(h, sock->family, sock->type, sock->protocol))
        error = WSAENOBUFS;
    if (error) {
        NtClose(h);
        fail(sock, error);
        return INVALID_SOCKET;
    }
    if (addr)
        from_address(&both[1], addr, addrlen);
    inherit(sock, (SOCKET)h);
    release(sock);
    SetLastError(0);
    return (SOCKET)h;
}

int WSAAPI shutdown(SOCKET s, int how)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    ULONG h = (ULONG)how;
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_SHUTDOWN, &h, sizeof(h), nullptr, 0, nullptr));
    return error ? fail(sock, error) : succeed(sock);
}

static int get_name(SOCKET s, BOOL peer, struct sockaddr *name, int *namelen)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!name || !namelen)
        return fail(sock, WSAEFAULT);
    ULONG p = peer;
    MIME_AFD_ADDRESS a;
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_NAME, &p, sizeof(p), &a, sizeof(a), nullptr));
    if (!error)
        error = from_address(&a, name, namelen);
    return error ? fail(sock, error) : succeed(sock);
}

int WSAAPI getsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
    return get_name(s, FALSE, name, namelen);
}

int WSAAPI getpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
    return get_name(s, TRUE, name, namelen);
}

/* ------------------------------------------------------------------ sending and receiving */

static int transfer(SOCKET s, BOOL send, LPWSABUF buffers, DWORD count, LPDWORD done, LPDWORD flags,
                    const struct sockaddr *to, int tolen, struct sockaddr *from, LPINT fromlen,
                    LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if ((!buffers && count) || (!send && !flags))
        return fail(sock, WSAEFAULT);
    DWORD f = send ? (flags ? *flags : 0) : *flags;
    DWORD allowed = send ? (MSG_OOB | MSG_DONTROUTE | MSG_PARTIAL) : (MSG_OOB | MSG_PEEK | MSG_WAITALL | MSG_PARTIAL);
    if (f & ~allowed)
        return fail(sock, WSAEOPNOTSUPP);
    if ((f & MSG_OOB) && sock->type != SOCK_STREAM)
        return fail(sock, WSAEOPNOTSUPP);
    if ((f & MSG_WAITALL) && (sock->type != SOCK_STREAM || overlapped))
        return fail(sock, WSAEOPNOTSUPP);
    MIME_AFD_TRANSFER t;
    memset(&t, 0, sizeof(t));
    t.Buffers = (ULONG64)(ULONG_PTR)buffers;
    t.BufferCount = count;
    t.Flags = f & ~MSG_PARTIAL;
    t.AfdFlags = overlapped ? MIME_AFD_OVERLAPPED : 0;
    MIME_AFD_ADDRESS dest;
    if (send && to && sock->type == SOCK_DGRAM) {
        if ((error = to_address(sock, to, tolen, &dest)))
            return fail(sock, error);
        t.Address = (ULONG64)(ULONG_PTR)&dest;
    }
    if (!send && from) {
        if (!fromlen || *fromlen < sockaddr_size(sock->family))
            return fail(sock, WSAEFAULT);
        t.Address = (ULONG64)(ULONG_PTR)from;
        t.AddressLength = (ULONG64)(ULONG_PTR)fromlen;
    }
    ULONG code = send ? IOCTL_AFD_SEND : IOCTL_AFD_RECV;
    if (!send)
        *flags = 0;
    if (overlapped) {
        DWORD bytes = 0;
        error = afd_overlapped((HANDLE)s, code, &t, sizeof(t), overlapped, routine, &bytes);
        if (!error && done)
            *done = bytes;
        return error ? fail(sock, error) : succeed(sock);
    }
    ULONG_PTR bytes = 0;
    NTSTATUS status = afd_call((HANDLE)s, code, &t, sizeof(t), nullptr, 0, send ? sock->sndtimeo : sock->rcvtimeo, &bytes);
    if (done)
        *done = (DWORD)bytes;
    error = MimeAfdError(status);
    /* a socket closed under a blocking call interrupts it. */
    if (error == WSA_OPERATION_ABORTED)
        error = WSAEINTR;
    return error ? fail(sock, error) : succeed(sock);
}

int WSAAPI send(SOCKET s, const char *buf, int len, int flags)
{
    if (len < 0)
        return WsFail(WSAEINVAL);
    WSABUF b = {(ULONG)len, (char *)buf};
    DWORD sent = 0, f = (DWORD)flags;
    int r = transfer(s, TRUE, &b, 1, &sent, &f, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
    return r ? r : (int)sent;
}

int WSAAPI sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    if (len < 0)
        return WsFail(WSAEINVAL);
    WSABUF b = {(ULONG)len, (char *)buf};
    DWORD sent = 0, f = (DWORD)flags;
    int r = transfer(s, TRUE, &b, 1, &sent, &f, to, tolen, nullptr, nullptr, nullptr, nullptr);
    return r ? r : (int)sent;
}

int WSAAPI WSASend(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags, LPWSAOVERLAPPED overlapped,
                   LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return transfer(s, TRUE, buffers, count, sent, &flags, nullptr, 0, nullptr, nullptr, overlapped, routine);
}

int WSAAPI WSASendTo(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags, const struct sockaddr *to,
                     int tolen, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return transfer(s, TRUE, buffers, count, sent, &flags, to, tolen, nullptr, nullptr, overlapped, routine);
}

int WSAAPI recv(SOCKET s, char *buf, int len, int flags)
{
    if (len < 0)
        return WsFail(WSAEINVAL);
    WSABUF b = {(ULONG)len, buf};
    DWORD got = 0, f = (DWORD)flags;
    int r = transfer(s, FALSE, &b, 1, &got, &f, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
    return r ? r : (int)got;
}

int WSAAPI recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    if (len < 0)
        return WsFail(WSAEINVAL);
    WSABUF b = {(ULONG)len, buf};
    DWORD got = 0, f = (DWORD)flags;
    int r = transfer(s, FALSE, &b, 1, &got, &f, nullptr, 0, from, fromlen, nullptr, nullptr);
    return r ? r : (int)got;
}

int WSAAPI WSARecv(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                   LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return transfer(s, FALSE, buffers, count, received, flags, nullptr, 0, nullptr, nullptr, overlapped, routine);
}

int WSAAPI WSARecvFrom(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                       struct sockaddr *from, LPINT fromlen, LPWSAOVERLAPPED overlapped,
                       LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    return transfer(s, FALSE, buffers, count, received, flags, nullptr, 0, from, fromlen, overlapped, routine);
}

BOOL WSAAPI WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED overlapped, LPDWORD transferred, BOOL wait,
                                   LPDWORD flags)
{
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
    *flags = 0;
    int error = MimeAfdError((NTSTATUS)overlapped->Internal);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the mswsock extensions */

static BOOL WSAAPI connect_ex(SOCKET s, const struct sockaddr *name, int namelen, PVOID send_buffer, DWORD send_length,
                              LPDWORD sent, LPOVERLAPPED overlapped)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock) {
        SetLastError(error);
        return FALSE;
    }
    MIME_AFD_CONNECT c;
    memset(&c, 0, sizeof(c));
    if (!overlapped)
        error = WSAEINVAL;
    else if (sock->type != SOCK_STREAM)
        error = WSAEOPNOTSUPP;
    else
        error = to_address(sock, name, namelen, &c.Address);
    if (!error && address_is_any(&c.Address))
        error = WSAEADDRNOTAVAIL;
    if (!error) {
        c.AfdFlags = MIME_AFD_OVERLAPPED;
        c.Length = send_buffer ? send_length : 0;
        c.Buffer = (ULONG64)(ULONG_PTR)send_buffer;
        DWORD bytes = 0;
        error = afd_overlapped((HANDLE)s, IOCTL_AFD_CONNECT, &c, sizeof(c), (LPWSAOVERLAPPED)overlapped, nullptr, &bytes);
        if (!error && sent)
            *sent = bytes;
    }
    release(sock);
    SetLastError(error);
    return !error;
}

static BOOL WSAAPI accept_ex(SOCKET listener, SOCKET accepted, PVOID buffer, DWORD receive_length,
                             DWORD local_length, DWORD remote_length, LPDWORD received, LPOVERLAPPED overlapped)
{
    int error;
    WS_SOCKET *sock = acquire(listener, &error);
    if (!sock) {
        SetLastError(error);
        return FALSE;
    }
    DWORD need = (DWORD)sockaddr_size(sock->family) + 16;
    if (!overlapped || !buffer)
        error = WSAEFAULT;
    else if (local_length < need || remote_length < need)
        error = WSAEINVAL;
    if (!error) {
        /* the two address areas hold the local and remote sockaddr, read by GetAcceptExSockaddrs. */
        MIME_AFD_ACCEPT a = {(ULONG64)accepted, MIME_AFD_OVERLAPPED, receive_length, (ULONG64)(ULONG_PTR)buffer,
                             (ULONG64)(ULONG_PTR)((PUCHAR)buffer + receive_length)};
        DWORD bytes = 0;
        error = afd_overlapped((HANDLE)listener, IOCTL_AFD_ACCEPT, &a, sizeof(a), (LPWSAOVERLAPPED)overlapped, nullptr,
                               &bytes);
        if (!error && received)
            *received = bytes;
    }
    release(sock);
    SetLastError(error);
    return !error;
}

static void WSAAPI get_accept_ex_sockaddrs(PVOID buffer, DWORD receive_length, DWORD local_length,
                                           DWORD remote_length, struct sockaddr **local, LPINT local_size,
                                           struct sockaddr **remote, LPINT remote_size)
{
    MIME_AFD_ADDRESS *both = (MIME_AFD_ADDRESS *)((PUCHAR)buffer + receive_length);
    if (local) {
        *local = (struct sockaddr *)&both[0];
        if (local_size)
            *local_size = both[0].Family == AF_INET6 ? (int)sizeof(SOCKADDR_IN6) : (int)sizeof(SOCKADDR_IN);
    }
    if (remote) {
        *remote = (struct sockaddr *)&both[1];
        if (remote_size)
            *remote_size = both[1].Family == AF_INET6 ? (int)sizeof(SOCKADDR_IN6) : (int)sizeof(SOCKADDR_IN);
    }
}

/* the connection ends gracefully; the socket cannot be reused, which TF_REUSE_SOCKET would ask for. */
static BOOL WSAAPI disconnect_ex(SOCKET s, LPOVERLAPPED overlapped, DWORD flags, DWORD reserved)
{
    if (shutdown(s, SD_SEND))
        return FALSE;
    if (overlapped) {
        ULONG64 zero = 0;
        int error = afd_overlapped((HANDLE)s, IOCTL_AFD_COMPLETE, &zero, sizeof(zero), (LPWSAOVERLAPPED)overlapped,
                                   nullptr, nullptr);
        if (error && error != WSA_IO_PENDING) {
            SetLastError(error);
            return FALSE;
        }
    }
    return TRUE;
}

static const GUID guid_accept_ex = {0xB5367DF1, 0xCBAC, 0x11CF, {0x95, 0xCA, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};
static const GUID guid_connect_ex = {0x25A207B9, 0xDDF3, 0x4660, {0x8E, 0xE9, 0x76, 0xE5, 0x8C, 0x74, 0x06, 0x3E}};
static const GUID guid_get_accept_ex_sockaddrs = {0xB5367DF2, 0xCBAC, 0x11CF,
                                                  {0x95, 0xCA, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};
static const GUID guid_wsa_recv_msg = {0xF689D7C8, 0x6F1F, 0x436B, {0x8A, 0x53, 0xE5, 0x4F, 0xE3, 0x51, 0xC3, 0x22}};
static const GUID guid_wsa_send_msg = {0xA441E712, 0x754F, 0x43CA, {0x84, 0xA7, 0x0D, 0xEE, 0x44, 0xCF, 0x60, 0x6D}};
static const GUID guid_disconnect_ex = {0x7FDA2E11, 0x8630, 0x436F, {0xA0, 0x31, 0xF5, 0x36, 0xA6, 0xEE, 0xC1, 0x57}};

/*
 * WSARecvMsg and WSASendMsg of datagram sockets: the receive or send with an address, the control data besides. there
 * is none to give or take here: no control message comes back, as from a socket that asked for none, and the ones a
 * send carries (the type of service) are ignored.
 */
static int WSAAPI wsa_recv_msg(SOCKET s, LPWSAMSG msg, LPDWORD received, LPWSAOVERLAPPED overlapped,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    if (!msg) {
        SetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    BOOL stream = sock->type == SOCK_STREAM;
    release(sock);
    if (stream)
        return WsFail(WSAEOPNOTSUPP);
    DWORD flags = msg->dwFlags;
    int r = transfer(s, FALSE, msg->lpBuffers, msg->dwBufferCount, received, &flags, nullptr, 0, msg->name,
                     msg->name ? &msg->namelen : nullptr, overlapped, routine);
    msg->Control.len = 0;
    msg->dwFlags = 0;
    return r;
}

static int WSAAPI wsa_send_msg(SOCKET s, LPWSAMSG msg, DWORD flags, LPDWORD sent, LPWSAOVERLAPPED overlapped,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    if (!msg) {
        SetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    BOOL stream = sock->type == SOCK_STREAM;
    release(sock);
    if (stream)
        return WsFail(WSAEOPNOTSUPP);
    return transfer(s, TRUE, msg->lpBuffers, msg->dwBufferCount, sent, &flags, msg->name, msg->namelen, nullptr, nullptr,
                    overlapped, routine);
}

static PVOID extension(const GUID *id)
{
    if (!memcmp(id, &guid_accept_ex, sizeof(GUID)))
        return (PVOID)accept_ex;
    if (!memcmp(id, &guid_connect_ex, sizeof(GUID)))
        return (PVOID)connect_ex;
    if (!memcmp(id, &guid_get_accept_ex_sockaddrs, sizeof(GUID)))
        return (PVOID)get_accept_ex_sockaddrs;
    if (!memcmp(id, &guid_disconnect_ex, sizeof(GUID)))
        return (PVOID)disconnect_ex;
    if (!memcmp(id, &guid_wsa_recv_msg, sizeof(GUID)))
        return (PVOID)wsa_recv_msg;
    if (!memcmp(id, &guid_wsa_send_msg, sizeof(GUID)))
        return (PVOID)wsa_send_msg;
    /* TransmitFile and the others are absent; callers fall back to the plain functions. */
    return nullptr;
}

/* ------------------------------------------------------------------ select and poll */

static int run_poll(MIME_AFD_POLL *p, ULONG count)
{
    HANDLE h = helper_handle();
    if (!h)
        return WSAENETDOWN;
    ULONG size = sizeof(*p) + count * sizeof(MIME_AFD_POLL_ENTRY);
    return MimeAfdError(afd_call(h, IOCTL_AFD_POLL, p, size, p, size, 0, nullptr));
}

int WSAAPI select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    fd_set *sets[3] = {readfds, writefds, exceptfds};
    static const ULONG wanted[3] = {MIME_AFD_POLL_READ, MIME_AFD_POLL_WRITE, MIME_AFD_POLL_EXCEPT};
    ULONG count = 0;
    for (int k = 0; k < 3; k++)
        count += sets[k] ? sets[k]->fd_count : 0;
    if (!count)
        return WsFail(WSAEINVAL);
    if (count > WS_MAX_POLL)
        return WsFail(WSAENOBUFS);
    MIME_AFD_POLL *p = WsAllocZero(sizeof(*p) + count * sizeof(MIME_AFD_POLL_ENTRY));
    if (!p)
        return WsFail(WSAENOBUFS);
    p->Timeout = -1;
    if (timeout)
        p->Timeout = (LONGLONG)(timeout->tv_sec > 0 ? timeout->tv_sec : 0) * 1000 +
                     (timeout->tv_usec > 0 ? (timeout->tv_usec + 999) / 1000 : 0);
    p->Count = count;
    ULONG n = 0;
    for (int k = 0; k < 3; k++) {
        for (u_int i = 0; sets[k] && i < sets[k]->fd_count; i++) {
            p->Entries[n].Socket = (ULONG64)sets[k]->fd_array[i];
            p->Entries[n].Events = wanted[k];
            n++;
        }
    }
    error = run_poll(p, count);
    if (!error) {
        for (ULONG i = 0; i < count; i++) {
            if (p->Entries[i].Results & MIME_AFD_POLL_INVALID)
                error = WSAENOTSOCK;
        }
    }
    if (error) {
        WsFree(p);
        return WsFail(error);
    }
    int total = 0;
    n = 0;
    for (int k = 0; k < 3; k++) {
        if (!sets[k])
            continue;
        u_int out = 0, in = sets[k]->fd_count;
        for (u_int i = 0; i < in; i++, n++) {
            if (p->Entries[n].Results & wanted[k])
                sets[k]->fd_array[out++] = sets[k]->fd_array[i];
        }
        sets[k]->fd_count = out;
        total += (int)out;
    }
    WsFree(p);
    SetLastError(0);
    return total;
}

int WSAAPI WSAPoll(LPWSAPOLLFD fds, ULONG count, INT timeout)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!fds)
        return WsFail(WSAEFAULT);
    if (count > WS_MAX_POLL)
        return WsFail(WSAENOBUFS);
    for (ULONG i = 0; i < count; i++) {
        if (fds[i].events & ~(POLLRDNORM | POLLRDBAND | POLLPRI | POLLWRNORM | POLLWRBAND))
            return WsFail(WSAEINVAL);
    }
    MIME_AFD_POLL *p = WsAllocZero(sizeof(*p) + (count ? count : 1) * sizeof(MIME_AFD_POLL_ENTRY));
    if (!p)
        return WsFail(WSAENOBUFS);
    p->Timeout = timeout < 0 ? -1 : timeout;
    ULONG n = 0;
    for (ULONG i = 0; i < count; i++) {
        fds[i].revents = 0;
        if (fds[i].fd == INVALID_SOCKET)
            continue;
        ULONG events = MIME_AFD_POLL_HUP | MIME_AFD_POLL_ERROR;
        if (fds[i].events & (POLLRDNORM | POLLRDBAND))
            events |= MIME_AFD_POLL_READ;
        if (fds[i].events & POLLWRNORM)
            events |= MIME_AFD_POLL_WRITE;
        p->Entries[n].Socket = (ULONG64)fds[i].fd;
        p->Entries[n].Events = events;
        n++;
    }
    p->Count = n;
    if (n && (error = run_poll(p, n))) {
        WsFree(p);
        return WsFail(error);
    }
    if (!n && timeout)
        Sleep(timeout < 0 ? INFINITE : (DWORD)timeout);
    int total = 0;
    n = 0;
    for (ULONG i = 0; i < count; i++) {
        if (fds[i].fd == INVALID_SOCKET)
            continue;
        ULONG r = p->Entries[n++].Results;
        SHORT revents = 0;
        if (r & MIME_AFD_POLL_INVALID)
            revents = POLLNVAL;
        if ((r & MIME_AFD_POLL_READ) && (fds[i].events & POLLRDNORM))
            revents |= POLLRDNORM;
        if ((r & MIME_AFD_POLL_WRITE) && (fds[i].events & POLLWRNORM))
            revents |= POLLWRNORM;
        if (r & MIME_AFD_POLL_HUP)
            revents |= POLLHUP;
        if (r & MIME_AFD_POLL_ERROR)
            revents |= POLLERR;
        fds[i].revents = revents;
        if (revents)
            total++;
    }
    WsFree(p);
    SetLastError(0);
    return total;
}

/* ------------------------------------------------------------------ WSAEventSelect and WSAAsyncSelect */

static int event_select(WS_SOCKET *sock, HANDLE event, long events)
{
    MIME_AFD_EVENT_SELECT sel = {(ULONG64)(ULONG_PTR)event, (ULONG)events, 0};
    return MimeAfdError(socket_call(sock, IOCTL_AFD_EVENT_SELECT, &sel, sizeof(sel), nullptr, 0, nullptr));
}

int WSAAPI WSAEventSelect(SOCKET s, WSAEVENT event, long events)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (events && !event)
        return fail(sock, WSAEINVAL);
    if ((error = event_select(sock, events ? event : nullptr, events)))
        return fail(sock, error);
    EnterCriticalSection(&lock);
    sock->hwnd = nullptr;
    sock->msg = 0;
    sock->select_event = events ? event : nullptr;
    sock->select_events = events;
    LeaveCriticalSection(&lock);
    return succeed(sock);
}

static DWORD WINAPI async_main(LPVOID parameter)
{
    static const int order[] = {FD_CONNECT_BIT, FD_ACCEPT_BIT, FD_READ_BIT, FD_OOB_BIT, FD_WRITE_BIT, FD_CLOSE_BIT};
    for (;;) {
        WaitForSingleObject(async_event, INFINITE);
        /* the windows to tell, gathered under the lock and told outside it. */
        for (;;) {
            SOCKET s = INVALID_SOCKET;
            HWND hwnd = nullptr;
            UINT msg = 0;
            MIME_AFD_EVENTS ev = {};
            EnterCriticalSection(&lock);
            for (WS_SOCKET *sock = sockets; sock; sock = sock->next) {
                if (sock->closed || !sock->hwnd)
                    continue;
                IO_STATUS_BLOCK io;
                ULONG64 none = 0;
                if (!NT_SUCCESS(NtDeviceIoControlFile((HANDLE)sock->handle, nullptr, nullptr, nullptr, &io,
                                                      IOCTL_AFD_ENUM_EVENTS, &none, sizeof(none), &ev, sizeof(ev))) ||
                    !ev.Events)
                    continue;
                s = sock->handle;
                hwnd = sock->hwnd;
                msg = sock->msg;
                break;
            }
            LeaveCriticalSection(&lock);
            if (s == INVALID_SOCKET)
                break;
            for (SIZE_T i = 0; i < RTL_NUMBER_OF(order); i++) {
                if (ev.Events & (1u << order[i]))
                    PostMessageW(hwnd, msg, (WPARAM)s, WSAMAKESELECTREPLY(1 << order[i], ev.Errors[order[i]]));
            }
        }
    }
    return 0;
}

static BOOL start_async(void)
{
    EnterCriticalSection(&lock);
    if (!async_event)
        async_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (async_event && !async_thread)
        async_thread = CreateThread(nullptr, 0, async_main, nullptr, 0, nullptr);
    BOOL ok = async_event && async_thread;
    LeaveCriticalSection(&lock);
    return ok;
}

int WSAAPI WSAAsyncSelect(SOCKET s, HWND hwnd, u_int msg, long events)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (events && !IsWindow(hwnd))
        return fail(sock, WSAEINVAL);
    if (events && !start_async())
        return fail(sock, WSAENOBUFS);
    EnterCriticalSection(&lock);
    sock->select_event = nullptr;
    sock->hwnd = events ? hwnd : nullptr;
    sock->msg = msg;
    sock->select_events = events;
    LeaveCriticalSection(&lock);
    if ((error = event_select(sock, events ? async_event : nullptr, events)))
        return fail(sock, error);
    /* a socket under WSAAsyncSelect stays non blocking even when nothing is selected. */
    host_option(sock, MIME_AFD_OPTION_NONBLOCKING, TRUE, 1, nullptr);
    return succeed(sock);
}

int WSAAPI WSAEnumNetworkEvents(SOCKET s, WSAEVENT event, LPWSANETWORKEVENTS events)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!events)
        return fail(sock, WSAEFAULT);
    ULONG64 e = (ULONG64)(ULONG_PTR)event;
    MIME_AFD_EVENTS ev;
    error = MimeAfdError(socket_call(sock, IOCTL_AFD_ENUM_EVENTS, &e, sizeof(e), &ev, sizeof(ev), nullptr));
    if (error)
        return fail(sock, error);
    events->lNetworkEvents = (long)ev.Events;
    for (int i = 0; i < FD_MAX_EVENTS; i++)
        events->iErrorCode[i] = ev.Errors[i];
    return succeed(sock);
}

/* ------------------------------------------------------------------ interfaces and names */

/* the addresses of the host, one entry per address; returns the count or a negative winsock error. */
int WsInterfaces(MIME_AFD_INTERFACE *list, int max)
{
    HANDLE h = helper_handle();
    if (!h)
        return -WSAENETDOWN;
    ULONG_PTR bytes = 0;
    NTSTATUS status = afd_call(h, IOCTL_AFD_INTERFACES, nullptr, 0, list, (ULONG)max * sizeof(*list), 0, &bytes);
    if (!NT_SUCCESS(status))
        return -MimeAfdError(status);
    return (int)(bytes / sizeof(*list));
}

int WsResolve(const char *name, int family, MIME_AFD_ADDRESS *out, int max)
{
    HANDLE h = helper_handle();
    if (!h)
        return -WSANO_RECOVERY;
    MIME_AFD_RESOLVE q;
    memset(&q, 0, sizeof(q));
    SIZE_T n = strlen(name);
    if (!n || n >= sizeof(q.Name))
        return -WSAHOST_NOT_FOUND;
    q.Family = (ULONG)family;
    q.Length = (ULONG)n;
    memcpy(q.Name, name, n);
    ULONG_PTR bytes = 0;
    NTSTATUS status = afd_call(h, IOCTL_AFD_RESOLVE, &q, sizeof(q), out, (ULONG)max * sizeof(*out), 0, &bytes);
    if (!NT_SUCCESS(status))
        return -MimeAfdError(status);
    return (int)(bytes / sizeof(*out));
}

static void prefix_mask(int family, int bits, BYTE *mask)
{
    int len = family == AF_INET ? 4 : 16;
    memset(mask, 0, len);
    for (int i = 0; i < len && bits > 0; i++, bits -= 8)
        mask[i] = bits >= 8 ? 0xFF : (BYTE)(0xFF << (8 - bits));
}

/* an address of an interface that is up with its link up, as the ip helper lists them. */
static BOOL interface_ready(const MIME_AFD_INTERFACE *e)
{
    return (e->Flags & (MIME_AFD_IF_UP | MIME_AFD_IF_RUNNING)) == (MIME_AFD_IF_UP | MIME_AFD_IF_RUNNING);
}

/* SIO_GET_INTERFACE_LIST: the ipv4 interfaces of the host. */
static int interface_list(LPVOID out, DWORD out_size, LPDWORD returned)
{
    MIME_AFD_INTERFACE list[64];
    int n = WsInterfaces(list, 64);
    if (n < 0)
        return -n;
    DWORD count = 0;
    for (int i = 0; i < n; i++)
        count += list[i].Address.Family == AF_INET && interface_ready(&list[i]);
    *returned = count * sizeof(INTERFACE_INFO);
    if (out_size < *returned || !out)
        return WSAEFAULT;
    INTERFACE_INFO *info = out;
    for (int i = 0; i < n; i++) {
        const MIME_AFD_INTERFACE *e = &list[i];
        if (e->Address.Family != AF_INET || !interface_ready(e))
            continue;
        memset(info, 0, sizeof(*info));
        info->iiFlags = IFF_UP;
        if (e->Flags & MIME_AFD_IF_LOOPBACK)
            info->iiFlags |= IFF_LOOPBACK;
        if (e->Flags & MIME_AFD_IF_BROADCAST)
            info->iiFlags |= IFF_BROADCAST;
        if (e->Flags & MIME_AFD_IF_MULTICAST)
            info->iiFlags |= IFF_MULTICAST;
        if (e->Flags & MIME_AFD_IF_POINTTOPOINT)
            info->iiFlags |= IFF_POINTTOPOINT;
        memcpy(&info->iiAddress.AddressIn, &e->Address, sizeof(SOCKADDR_IN));
        info->iiNetmask.AddressIn.sin_family = AF_INET;
        prefix_mask(AF_INET, e->Prefix, (BYTE *)&info->iiNetmask.AddressIn.sin_addr);
        info->iiBroadcastAddress.AddressIn.sin_family = AF_INET;
        info->iiBroadcastAddress.AddressIn.sin_addr.S_un.S_addr =
            info->iiAddress.AddressIn.sin_addr.S_un.S_addr | ~info->iiNetmask.AddressIn.sin_addr.S_un.S_addr;
        info++;
    }
    return 0;
}

/* SIO_ADDRESS_LIST_QUERY: the addresses of the socket's family outside the loopback. */
static int address_list(const WS_SOCKET *sock, LPVOID out, DWORD out_size, LPDWORD returned)
{
    MIME_AFD_INTERFACE list[64];
    int n = WsInterfaces(list, 64);
    if (n < 0)
        return -n;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].Address.Family == sock->family && interface_ready(&list[i]) &&
            !(list[i].Flags & MIME_AFD_IF_LOOPBACK))
            count++;
    }
    int size = sockaddr_size(sock->family);
    DWORD need = (DWORD)(offsetof(SOCKET_ADDRESS_LIST, Address) + count * (sizeof(SOCKET_ADDRESS) + size));
    *returned = need;
    if (!out || out_size < need)
        return WSAEFAULT;
    SOCKET_ADDRESS_LIST *l = out;
    PUCHAR data = (PUCHAR)&l->Address[count];
    l->iAddressCount = count;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].Address.Family != sock->family || !interface_ready(&list[i]) ||
            (list[i].Flags & MIME_AFD_IF_LOOPBACK))
            continue;
        memcpy(data, &list[i].Address, size);
        l->Address[k].lpSockaddr = (LPSOCKADDR)data;
        l->Address[k].iSockaddrLength = size;
        data += size;
        k++;
    }
    return 0;
}

/* SIO_ROUTING_INTERFACE_QUERY: the local address the host would send from, found by a connected datagram socket. */
static int routing_query(const WS_SOCKET *sock, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                         LPDWORD returned)
{
    MIME_AFD_ADDRESS dest;
    int error = to_address(sock, in, (int)in_size, &dest);
    if (error)
        return error;
    HANDLE h;
    if ((error = open_socket(sock->family, SOCK_DGRAM, IPPROTO_UDP, &h)))
        return error;
    MIME_AFD_CONNECT c;
    memset(&c, 0, sizeof(c));
    c.Address = dest;
    error = MimeAfdError(afd_call(h, IOCTL_AFD_CONNECT, &c, sizeof(c), nullptr, 0, 0, nullptr));
    MIME_AFD_ADDRESS local;
    ULONG peer = 0;
    if (!error)
        error = MimeAfdError(afd_call(h, IOCTL_AFD_NAME, &peer, sizeof(peer), &local, sizeof(local), 0, nullptr));
    NtClose(h);
    if (error)
        return error;
    ((SOCKADDR_IN *)&local)->sin_port = 0;
    int len = (int)out_size;
    *returned = (DWORD)sockaddr_size(sock->family);
    if ((error = from_address(&local, out, &len)))
        return error;
    *returned = (DWORD)len;
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
    ULONG v = 0;
    switch ((u_long)cmd) {
    case FIONBIO:
        error = host_option(sock, MIME_AFD_OPTION_NONBLOCKING, TRUE, *argp != 0, nullptr);
        break;
    case FIONREAD:
        error = host_option(sock, MIME_AFD_OPT_NREAD, FALSE, 0, &v);
        if (error == WSAENOTCONN || error == WSAEINVAL)
            error = 0, v = 0;
        *argp = v;
        break;
    case SIOCATMARK:
        *argp = TRUE;
        break;
    default:
        error = WSAEINVAL;
        break;
    }
    return error ? fail(sock, error) : succeed(sock);
}

int WSAAPI WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size, LPDWORD returned,
                    LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    int error;
    WS_SOCKET *sock = acquire(s, &error);
    if (!sock)
        return WsFail(error);
    if (!returned && !overlapped)
        return fail(sock, WSAEFAULT);
    DWORD size = 0;
    ULONG v = 0;
    switch (code) {
    case FIONBIO:
        if (!in || in_size < sizeof(u_long))
            error = WSAEFAULT;
        else
            error = host_option(sock, MIME_AFD_OPTION_NONBLOCKING, TRUE, *(u_long *)in != 0, nullptr);
        break;
    case FIONREAD:
    case SIOCATMARK:
        if (!out || out_size < sizeof(u_long)) {
            error = WSAEFAULT;
            break;
        }
        if (code == FIONREAD) {
            error = host_option(sock, MIME_AFD_OPT_NREAD, FALSE, 0, &v);
            if (error == WSAENOTCONN || error == WSAEINVAL)
                error = 0, v = 0;
        } else {
            v = TRUE;
        }
        *(u_long *)out = v;
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
    case SIO_KEEPALIVE_VALS: {
        if (!in || in_size < sizeof(struct tcp_keepalive)) {
            error = WSAEFAULT;
            break;
        }
        const struct tcp_keepalive *k = in;
        error = host_option(sock, MIME_AFD_OPT_KEEPALIVE, TRUE, k->onoff != 0, nullptr);
        if (!error && k->onoff) {
            host_option(sock, MIME_AFD_OPT_KEEPIDLE, TRUE, max(k->keepalivetime / 1000, 1u), nullptr);
            host_option(sock, MIME_AFD_OPT_KEEPINTVL, TRUE, max(k->keepaliveinterval / 1000, 1u), nullptr);
        }
        break;
    }
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
        error = interface_list(out, out_size, &size);
        break;
    case SIO_ADDRESS_LIST_QUERY:
        error = address_list(sock, out, out_size, &size);
        break;
    case SIO_ADDRESS_LIST_CHANGE:
    case SIO_ROUTING_INTERFACE_CHANGE:
        /* winsock does not watch the host's addresses yet, iphlpapi does: an overlapped watch never completes. */
        if (overlapped) {
            overlapped->Internal = (ULONG_PTR)STATUS_PENDING;
            release(sock);
            return WsFail(WSA_IO_PENDING);
        }
        error = WSAEWOULDBLOCK;
        break;
    case SIO_ROUTING_INTERFACE_QUERY:
        error = in && out ? routing_query(sock, in, in_size, out, out_size, &size) : WSAEFAULT;
        break;
    case SIO_GET_EXTENSION_FUNCTION_POINTER: {
        PVOID fn = in && in_size >= sizeof(GUID) ? extension(in) : nullptr;
        if (!in || in_size < sizeof(GUID) || !out || out_size < sizeof(PVOID))
            error = WSAEFAULT;
        else if (!fn)
            error = WSAEINVAL;
        else
            *(PVOID *)out = fn;
        size = sizeof(PVOID);
        break;
    }
    default:
        error = WSAEOPNOTSUPP;
        break;
    }
    if (returned)
        *returned = size;
    if (error)
        return fail(sock, error);
    if (overlapped) {
        ULONG64 info = size;
        error = afd_overlapped((HANDLE)s, IOCTL_AFD_COMPLETE, &info, sizeof(info), overlapped, routine, nullptr);
        if (error)
            return fail(sock, error);
    }
    return succeed(sock);
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

static constexpr int protocol_order[][2] = {
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
        return nullptr;
    sock->extra[sock->extra_count].level = level;
    sock->extra[sock->extra_count].name = name;
    sock->extra[sock->extra_count].value = 0;
    return &sock->extra[sock->extra_count++].value;
}

/* the value of options that are accepted and remembered without reaching the host. */
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
        case TCP_MAXSEG:
            *initial = 1460;
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

/* the host option behind a windows one, 0 when the host has none. */
static ULONG host_option_id(const WS_SOCKET *sock, int level, int name, int *error)
{
    *error = 0;
    if (level == SOL_SOCKET) {
        switch (name) {
        case SO_KEEPALIVE:
        case SO_OOBINLINE:
            if (sock->type != SOCK_STREAM)
                *error = WSAENOPROTOOPT;
            return name == SO_KEEPALIVE ? MIME_AFD_OPT_KEEPALIVE : MIME_AFD_OPT_OOBINLINE;
        case SO_BROADCAST:
            if (sock->type != SOCK_DGRAM)
                *error = WSAENOPROTOOPT;
            return MIME_AFD_OPT_BROADCAST;
        case SO_SNDBUF:
            return MIME_AFD_OPT_SNDBUF;
        case SO_RCVBUF:
            return MIME_AFD_OPT_RCVBUF;
        }
        return 0;
    }
    if (level == IPPROTO_TCP) {
        if (sock->type != SOCK_STREAM && (name == TCP_NODELAY || name == TCP_KEEPALIVE || name == TCP_KEEPINTVL ||
                                          name == TCP_KEEPCNT)) {
            *error = WSAENOPROTOOPT;
            return 0;
        }
        switch (name) {
        case TCP_NODELAY:
            return MIME_AFD_OPT_NODELAY;
        case TCP_KEEPALIVE:
            return MIME_AFD_OPT_KEEPIDLE;
        case TCP_KEEPINTVL:
            return MIME_AFD_OPT_KEEPINTVL;
        case TCP_KEEPCNT:
            return MIME_AFD_OPT_KEEPCNT;
        }
        return 0;
    }
    if ((level == IPPROTO_IP || level == IPPROTO_IPV6) && name == IP_TTL)
        return MIME_AFD_OPT_TTL;
    if (level == IPPROTO_IPV6 && name == IPV6_V6ONLY) {
        if (sock->family != AF_INET6)
            *error = WSAEINVAL;
        return MIME_AFD_OPT_V6ONLY;
    }
    return 0;
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
        /* joining a group is accepted; the host does not carry multicast for the guest. */
        if (sock->type != SOCK_DGRAM)
            error = WSAEINVAL;
        else if (!value || length < (int)(level == IPPROTO_IP ? sizeof(IP_MREQ) : sizeof(IPV6_MREQ)))
            error = WSAEFAULT;
        return error ? fail(sock, error) : succeed(sock);
    }
    if (level == SOL_SOCKET && name == SO_LINGER) {
        if (!value || length < (int)sizeof(LINGER))
            return fail(sock, WSAEFAULT);
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        memcpy(&sock->linger, value, sizeof(LINGER));
        return succeed(sock);
    }
    /* the context updates of AcceptEx and ConnectEx take no value. */
    if (level == SOL_SOCKET && name == SO_UPDATE_CONNECT_CONTEXT)
        return succeed(sock);
    if (level == SOL_SOCKET && name == SO_UPDATE_ACCEPT_CONTEXT)
        return value && length >= (int)sizeof(SOCKET) ? succeed(sock) : fail(sock, WSAEFAULT);
    if ((error = read_int(value, length, &v)))
        return fail(sock, error);
    ULONG option = host_option_id(sock, level, name, &error);
    if (error)
        return fail(sock, error);
    if (option) {
        error = host_option(sock, option, TRUE, (ULONG)v, nullptr);
        /* windows takes buffer sizes the host refuses, such as 0. */
        if (option == MIME_AFD_OPT_SNDBUF || option == MIME_AFD_OPT_RCVBUF)
            error = 0;
        return error ? fail(sock, error) : succeed(sock);
    }
    switch (level == SOL_SOCKET ? name : -1) {
    case SO_REUSEADDR:
        if (sock->exclusive && v)
            return fail(sock, WSAEINVAL);
        if ((error = host_option(sock, MIME_AFD_OPT_REUSEADDR, TRUE, v != 0, nullptr)))
            return fail(sock, error);
        sock->reuseaddr = v != 0;
        break;
    case SO_EXCLUSIVEADDRUSE:
        if (sock->reuseaddr && v)
            return fail(sock, WSAEINVAL);
        sock->exclusive = v != 0;
        break;
    case SO_DONTLINGER:
        if (sock->type != SOCK_STREAM)
            return fail(sock, WSAENOPROTOOPT);
        sock->linger.l_onoff = v == 0;
        break;
    case SO_SNDTIMEO:
        sock->sndtimeo = (DWORD)v;
        break;
    case SO_RCVTIMEO:
        sock->rcvtimeo = (DWORD)v;
        break;
    case SO_ERROR:
        break;
    case SO_TYPE:
    case SO_ACCEPTCONN:
    case SO_MAX_MSG_SIZE:
    case SO_PROTOCOL_INFOA:
    case SO_PROTOCOL_INFOW:
    case SO_CONNECT_TIME:
        return fail(sock, WSAENOPROTOOPT);
    default:
        if (!inert_option(level, name, &initial))
            return fail(sock, WSAENOPROTOOPT);
        int *slot = extra_option(sock, level, name, TRUE);
        if (!slot)
            return fail(sock, WSAENOBUFS);
        *slot = v;
        break;
    }
    return succeed(sock);
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
    ULONG option = host_option_id(sock, level, name, &error);
    if (error)
        return fail(sock, error);
    if (option) {
        ULONG got = 0;
        if ((error = host_option(sock, option, FALSE, 0, &got)))
            return fail(sock, error);
        v = (int)got;
        if (option == MIME_AFD_OPT_KEEPALIVE || option == MIME_AFD_OPT_NODELAY || option == MIME_AFD_OPT_BROADCAST ||
            option == MIME_AFD_OPT_OOBINLINE || option == MIME_AFD_OPT_V6ONLY)
            v = v != 0;
    } else if (level == SOL_SOCKET) {
        MIME_AFD_STATE st;
        switch (name) {
        case SO_LINGER:
            if (sock->type != SOCK_STREAM)
                return fail(sock, WSAENOPROTOOPT);
            if (*length < (int)sizeof(LINGER))
                return fail(sock, WSAEFAULT);
            memcpy(value, &sock->linger, sizeof(LINGER));
            *length = sizeof(LINGER);
            return succeed(sock);
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
            return succeed(sock);
        }
        case SO_TYPE:
            v = sock->type;
            break;
        case SO_ERROR: {
            ULONG got = 0;
            host_option(sock, MIME_AFD_OPT_ERROR, FALSE, 0, &got);
            v = (int)got;
            break;
        }
        case SO_ACCEPTCONN:
            v = afd_state(s, &st) && st.State == MIME_AFD_STATE_LISTENING;
            break;
        case SO_REUSEADDR:
            v = sock->reuseaddr;
            break;
        case SO_EXCLUSIVEADDRUSE:
            v = sock->exclusive;
            break;
        case SO_DONTLINGER:
            v = !sock->linger.l_onoff;
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
            /* the connect time is interrupt time, which the tick count follows. */
            if (afd_state(s, &st) && st.ConnectTime) {
                ULONGLONG now = GetTickCount64() * 10000;
                v = now > st.ConnectTime ? (int)((now - st.ConnectTime) / 10000000) : 0;
            } else {
                v = -1;
            }
            break;
        default:
            if (!inert_option(level, name, &initial))
                return fail(sock, WSAENOPROTOOPT);
            int *slot = extra_option(sock, level, name, FALSE);
            v = slot ? *slot : initial;
            break;
        }
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
    return succeed(sock);
}
