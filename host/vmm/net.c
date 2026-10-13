/*
 * the guest network: sockets of the host, kept non blocking. the guest
 * kernel asks what is ready with MIME_HC_NET_POLL and arms a watch on what
 * would block; a kqueue thread queues the sockets whose watch fired and
 * raises MIME_IRQ_NET. host names resolve on their own threads, so a slow
 * dns server never stalls the vcpu.
 */
#include "vmm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define NET_SLOTS 4096
#define NET_SLOT_MASK (NET_SLOTS - 1)
#define NET_RING 8192
#define NET_MAX_POLL 1024
#define NET_MAX_ANSWERS 64

/* the windows values the guest speaks. */
#define WIN_AF_INET 2
#define WIN_AF_INET6 23
#define WIN_SOCK_STREAM 1
#define WIN_SOCK_DGRAM 2
#define WIN_MSG_OOB 1
#define WIN_MSG_PEEK 2
#define WIN_MSG_DONTROUTE 4

typedef struct NetSocket {
    int fd; /* -1 when the slot is free */
    uint32_t gen;
    bool stream;
    bool listening;
    bool queued; /* in the ring */
} NetSocket;

typedef struct NetResolve {
    struct NetResolve *next;
    uint32_t id;
    int family;
    bool done;
    int32_t error;
    uint32_t count;
    MimeNetAddr addrs[NET_MAX_ANSWERS];
    char name[256];
} NetResolve;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static MimeVm *g_vm;
static int g_kq = -1;
static pthread_t g_thread;
static NetSocket g_sockets[NET_SLOTS];
static uint32_t g_next_slot;
static uint32_t g_ring[NET_RING];
static uint32_t g_ring_head, g_ring_tail;
static NetResolve *g_resolves;
static uint32_t g_next_resolve;

static int32_t wsa_error(int e)
{
    switch (e) {
    case EWOULDBLOCK:
    case EINPROGRESS:
        return 10035; /* WSAEWOULDBLOCK */
    case EALREADY:
        return 10037;
    case ENOTSOCK:
    case EBADF:
        return 10038;
    case EDESTADDRREQ:
        return 10039;
    case EMSGSIZE:
        return 10040;
    case EPROTOTYPE:
        return 10041;
    case ENOPROTOOPT:
        return 10042;
    case EPROTONOSUPPORT:
        return 10043;
    case ESOCKTNOSUPPORT:
        return 10044;
    case EOPNOTSUPP:
        return 10045;
    case EPFNOSUPPORT:
        return 10046;
    case EAFNOSUPPORT:
        return 10047;
    case EADDRINUSE:
        return 10048;
    case EADDRNOTAVAIL:
        return 10049;
    case ENETDOWN:
        return 10050;
    case ENETUNREACH:
        return 10051;
    case ENETRESET:
        return 10052;
    case ECONNABORTED:
        return 10053;
    case ECONNRESET:
    case EPIPE:
        return 10054;
    case ENOBUFS:
    case ENOMEM:
        return 10055;
    case EISCONN:
        return 10056;
    case ENOTCONN:
        return 10057;
    case ESHUTDOWN:
        return 10058;
    case ETIMEDOUT:
        return 10060;
    case ECONNREFUSED:
        return 10061;
    case EHOSTDOWN:
        return 10064;
    case EHOSTUNREACH:
        return 10065;
    case EACCES:
    case EPERM:
        return 10013;
    case EFAULT:
        return 10014;
    case EMFILE:
    case ENFILE:
        return 10024;
    default:
        return 10022; /* WSAEINVAL */
    }
}

static uint64_t fail(uint64_t *value, int32_t error)
{
    *value = (uint64_t)(uint32_t)error;
    return MIME_HC_STATUS_NET_ERROR;
}

static uint64_t fail_errno(uint64_t *value)
{
    return fail(value, wsa_error(errno));
}

/* ------------------------------------------------------------ addresses */

static bool to_host(const MimeNetAddr *a, struct sockaddr_storage *ss, socklen_t *len)
{
    memset(ss, 0, sizeof(*ss));
    if (a->family == WIN_AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)ss;
        in->sin_len = sizeof(*in);
        in->sin_family = AF_INET;
        memcpy(&in->sin_port, a->data, 2);
        memcpy(&in->sin_addr, a->data + 2, 4);
        *len = sizeof(*in);
        return true;
    }
    if (a->family == WIN_AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;
        in6->sin6_len = sizeof(*in6);
        in6->sin6_family = AF_INET6;
        memcpy(&in6->sin6_port, a->data, 2);
        memcpy(&in6->sin6_flowinfo, a->data + 2, 4);
        memcpy(&in6->sin6_addr, a->data + 6, 16);
        memcpy(&in6->sin6_scope_id, a->data + 22, 4);
        *len = sizeof(*in6);
        return true;
    }
    return false;
}

static void from_host(const struct sockaddr *sa, MimeNetAddr *a)
{
    memset(a, 0, sizeof(*a));
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        a->family = WIN_AF_INET;
        memcpy(a->data, &in->sin_port, 2);
        memcpy(a->data + 2, &in->sin_addr, 4);
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        a->family = WIN_AF_INET6;
        memcpy(a->data, &in6->sin6_port, 2);
        memcpy(a->data + 2, &in6->sin6_flowinfo, 4);
        memcpy(a->data + 6, &in6->sin6_addr, 16);
        memcpy(a->data + 22, &in6->sin6_scope_id, 4);
    }
}

static MimeNetAddr *guest_addr(MimeVm *vm, uint64_t pa)
{
    return pa ? mime_vm_phys(vm, pa, sizeof(MimeNetAddr)) : NULL;
}

/* ------------------------------------------------------------ the socket table and the ring */

static NetSocket *lookup(uint64_t id)
{
    if (!id || id > UINT32_MAX)
        return NULL;
    NetSocket *s = &g_sockets[id & NET_SLOT_MASK];
    if (s->fd < 0 || s->gen != (uint32_t)(id >> 12))
        return NULL;
    return s;
}

static uint32_t socket_id(const NetSocket *s)
{
    return (s->gen << 12) | (uint32_t)(s - g_sockets);
}

static void push_ring(uint32_t id)
{
    uint32_t next = (g_ring_head + 1) % NET_RING;
    if (next == g_ring_tail)
        return;
    g_ring[g_ring_head] = id;
    g_ring_head = next;
}

static void *watch_thread(void *arg)
{
    (void)arg;
    struct kevent events[64];
    for (;;) {
        int n = kevent(g_kq, NULL, 0, events, 64, NULL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        bool raise = false;
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < n; i++) {
            if (events[i].filter == EVFILT_USER) {
                pthread_mutex_unlock(&g_lock);
                return NULL;
            }
            NetSocket *s = lookup((uint64_t)(uintptr_t)events[i].udata);
            if (s && !s->queued) {
                s->queued = true;
                push_ring(socket_id(s));
                raise = true;
            }
        }
        pthread_mutex_unlock(&g_lock);
        if (raise)
            mime_vm_raise_irq(g_vm, MIME_IRQ_NET);
    }
    return NULL;
}

/* called with the lock held. */
static bool start(MimeVm *vm)
{
    if (g_kq >= 0)
        return true;
    for (uint32_t i = 0; i < NET_SLOTS; i++)
        g_sockets[i].fd = -1;
    g_vm = vm;
    g_kq = kqueue();
    if (g_kq < 0)
        return false;
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    kevent(g_kq, &ev, 1, NULL, 0, NULL);
    if (pthread_create(&g_thread, NULL, watch_thread, NULL)) {
        close(g_kq);
        g_kq = -1;
        return false;
    }
    return true;
}

static void prepare_fd(int fd, bool stream)
{
    int on = 1;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    /* windows rebinds a port whose old connections linger in time wait. */
    if (stream)
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

static NetSocket *add_socket(int fd, bool stream)
{
    for (uint32_t i = 0; i < NET_SLOTS; i++) {
        uint32_t slot = (g_next_slot + i) & NET_SLOT_MASK;
        NetSocket *s = &g_sockets[slot];
        if (s->fd >= 0)
            continue;
        g_next_slot = slot + 1;
        s->fd = fd;
        s->gen = (s->gen + 1) & 0xFFFFF;
        if (!s->gen)
            s->gen = 1;
        s->stream = stream;
        s->listening = false;
        s->queued = false;
        return s;
    }
    return NULL;
}

static uint64_t net_socket(MimeVm *vm, uint64_t family, uint64_t type, uint64_t protocol, uint64_t *value)
{
    int af = family == WIN_AF_INET ? AF_INET : family == WIN_AF_INET6 ? AF_INET6 : -1;
    int st = type == WIN_SOCK_STREAM ? SOCK_STREAM : type == WIN_SOCK_DGRAM ? SOCK_DGRAM : -1;
    if (af < 0)
        return fail(value, 10047);
    if (st < 0)
        return fail(value, 10044);
    if (!start(vm))
        return fail(value, 10050);
    int fd = socket(af, st, (int)protocol);
    if (fd < 0)
        return fail_errno(value);
    prepare_fd(fd, st == SOCK_STREAM);
    if (af == AF_INET6) {
        int on = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    }
    NetSocket *s = add_socket(fd, st == SOCK_STREAM);
    if (!s) {
        close(fd);
        return fail(value, 10024);
    }
    *value = socket_id(s);
    return MIME_HC_STATUS_OK;
}

static void close_socket(NetSocket *s)
{
    close(s->fd);
    s->fd = -1;
    s->queued = false;
}

/* ------------------------------------------------------------ operations */

static int host_flags(uint64_t flags)
{
    int f = 0;
    if (flags & WIN_MSG_OOB)
        f |= MSG_OOB;
    if (flags & WIN_MSG_PEEK)
        f |= MSG_PEEK;
    if (flags & WIN_MSG_DONTROUTE)
        f |= MSG_DONTROUTE;
    return f;
}

static uint64_t net_send(MimeVm *vm, NetSocket *s, uint64_t pa, uint64_t len, uint64_t flags, uint64_t addr_pa,
                         uint64_t *value)
{
    const void *data = len ? mime_vm_phys(vm, pa, len) : "";
    if (!data)
        return MIME_HC_STATUS_INVALID;
    ssize_t n;
    if (addr_pa) {
        const MimeNetAddr *a = guest_addr(vm, addr_pa);
        struct sockaddr_storage ss;
        socklen_t sl;
        if (!a || !to_host(a, &ss, &sl))
            return fail(value, 10047);
        n = sendto(s->fd, data, len, host_flags(flags), (struct sockaddr *)&ss, sl);
    } else {
        n = send(s->fd, data, len, host_flags(flags));
    }
    if (n < 0)
        return fail_errno(value);
    *value = (uint64_t)n;
    return MIME_HC_STATUS_OK;
}

static uint64_t net_recv(MimeVm *vm, NetSocket *s, uint64_t pa, uint64_t len, uint64_t flags, uint64_t addr_pa,
                         uint64_t *value)
{
    char dummy;
    void *data = len ? mime_vm_phys(vm, pa, len) : &dummy;
    if (!data)
        return MIME_HC_STATUS_INVALID;
    MimeNetAddr *a = NULL;
    if (addr_pa && !(a = guest_addr(vm, addr_pa)))
        return MIME_HC_STATUS_INVALID;
    struct sockaddr_storage ss;
    struct iovec iov = {data, len};
    struct msghdr msg = {0};
    msg.msg_name = &ss;
    msg.msg_namelen = sizeof(ss);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    memset(&ss, 0, sizeof(ss));
    ssize_t n = recvmsg(s->fd, &msg, host_flags(flags));
    if (n < 0) {
        /* an icmp port unreachable of a datagram reports as a reset on windows. */
        if (!s->stream && errno == ECONNREFUSED)
            return fail(value, 10054);
        return fail_errno(value);
    }
    if (a) {
        if (msg.msg_namelen)
            from_host((struct sockaddr *)&ss, a);
        else
            memset(a, 0, sizeof(*a));
    }
    if (!s->stream && (msg.msg_flags & MSG_TRUNC))
        return fail(value, 10040); /* WSAEMSGSIZE, the buffer is full */
    *value = (uint64_t)n;
    return MIME_HC_STATUS_OK;
}

static int sockopt_int(int fd, int level, int name, bool set, int v, uint64_t *value)
{
    if (set)
        return setsockopt(fd, level, name, &v, sizeof(v));
    socklen_t l = sizeof(v);
    int r = getsockopt(fd, level, name, &v, &l);
    *value = (uint64_t)(uint32_t)v;
    return r;
}

static uint64_t net_option(NetSocket *s, uint64_t option, bool set, uint64_t v, uint64_t *value)
{
    int fd = s->fd, r = -1;
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    *value = 0;
    switch (option) {
    case MIME_NET_OPT_REUSEADDR:
        r = sockopt_int(fd, SOL_SOCKET, SO_REUSEADDR, set, (int)v, value);
        if (set && r == 0)
            r = sockopt_int(fd, SOL_SOCKET, SO_REUSEPORT, set, (int)v, value);
        break;
    case MIME_NET_OPT_KEEPALIVE:
        r = sockopt_int(fd, SOL_SOCKET, SO_KEEPALIVE, set, (int)v, value);
        break;
    case MIME_NET_OPT_NODELAY:
        r = sockopt_int(fd, IPPROTO_TCP, TCP_NODELAY, set, (int)v, value);
        break;
    case MIME_NET_OPT_SNDBUF:
        r = sockopt_int(fd, SOL_SOCKET, SO_SNDBUF, set, (int)v, value);
        break;
    case MIME_NET_OPT_RCVBUF:
        r = sockopt_int(fd, SOL_SOCKET, SO_RCVBUF, set, (int)v, value);
        break;
    case MIME_NET_OPT_BROADCAST:
        r = sockopt_int(fd, SOL_SOCKET, SO_BROADCAST, set, (int)v, value);
        break;
    case MIME_NET_OPT_V6ONLY:
        r = sockopt_int(fd, IPPROTO_IPV6, IPV6_V6ONLY, set, (int)v, value);
        break;
    case MIME_NET_OPT_OOBINLINE:
        r = sockopt_int(fd, SOL_SOCKET, SO_OOBINLINE, set, (int)v, value);
        break;
    case MIME_NET_OPT_KEEPIDLE:
        r = sockopt_int(fd, IPPROTO_TCP, TCP_KEEPALIVE, set, (int)v, value);
        break;
    case MIME_NET_OPT_KEEPINTVL:
        r = sockopt_int(fd, IPPROTO_TCP, TCP_KEEPINTVL, set, (int)v, value);
        break;
    case MIME_NET_OPT_KEEPCNT:
        r = sockopt_int(fd, IPPROTO_TCP, TCP_KEEPCNT, set, (int)v, value);
        break;
    case MIME_NET_OPT_TTL:
        if (getsockname(fd, (struct sockaddr *)&ss, &sl) == 0 && ss.ss_family == AF_INET6)
            r = sockopt_int(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, set, (int)v, value);
        else
            r = sockopt_int(fd, IPPROTO_IP, IP_TTL, set, (int)v, value);
        break;
    case MIME_NET_OPT_LINGER: {
        struct linger l = {(int)((v >> 16) & 1), (int)(v & 0xFFFF)};
        /* a lingering close would block the vcpu, so only an abortive one reaches the host. */
        if (set && l.l_linger)
            l.l_onoff = 0;
        if (set) {
            r = setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
        } else {
            socklen_t ll = sizeof(l);
            r = getsockopt(fd, SOL_SOCKET, SO_LINGER, &l, &ll);
            *value = ((uint64_t)(l.l_onoff != 0) << 16) | (uint64_t)(l.l_linger & 0xFFFF);
        }
        break;
    }
    case MIME_NET_OPT_ERROR: {
        int e = 0;
        socklen_t el = sizeof(e);
        r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el);
        *value = e ? (uint64_t)(uint32_t)wsa_error(e) : 0;
        break;
    }
    case MIME_NET_OPT_NREAD: {
        int n = 0;
        if (!s->stream) {
            socklen_t nl = sizeof(n);
            r = getsockopt(fd, SOL_SOCKET, SO_NREAD, &n, &nl);
        } else {
            r = ioctl(fd, FIONREAD, &n);
        }
        *value = (uint64_t)(uint32_t)n;
        break;
    }
    case MIME_NET_OPT_ATMARK: {
        int at = 0;
        r = ioctl(fd, SIOCATMARK, &at);
        *value = at != 0;
        break;
    }
    default:
        return fail(value, 10042);
    }
    if (r < 0)
        return fail_errno(value);
    return MIME_HC_STATUS_OK;
}

/* what a socket is ready for; the error is taken from the socket. */
static uint16_t readiness(NetSocket *s, short revents, int32_t *error)
{
    uint16_t r = 0;
    *error = 0;
    if (revents & POLLNVAL)
        return MIME_NET_INVALID;
    if (revents & POLLIN) {
        if (!s->stream || s->listening) {
            r |= MIME_NET_READ;
        } else {
            int n = 0;
            if (ioctl(s->fd, FIONREAD, &n) == 0 && n > 0) {
                r |= MIME_NET_READ;
            } else {
                char c;
                ssize_t got = recv(s->fd, &c, 1, MSG_PEEK);
                if (got > 0)
                    r |= MIME_NET_READ;
                else if (got == 0)
                    r |= MIME_NET_HUP;
                else if (errno != EWOULDBLOCK && errno != EINTR)
                    revents |= POLLERR;
            }
        }
    }
    if (revents & POLLOUT)
        r |= MIME_NET_WRITE;
    if (revents & (POLLERR | POLLHUP)) {
        int e = 0;
        socklen_t el = sizeof(e);
        if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &e, &el) == 0 && e) {
            r |= MIME_NET_ERROR;
            *error = wsa_error(e);
        } else if ((revents & POLLHUP) && s->stream && !s->listening && !(r & MIME_NET_READ)) {
            r |= MIME_NET_HUP;
        }
    }
    return r;
}

static uint64_t net_poll(MimeVm *vm, uint64_t pa, uint64_t count, bool arm, uint64_t *value)
{
    if (count > NET_MAX_POLL)
        return MIME_HC_STATUS_INVALID;
    MimeNetPoll *entries = count ? mime_vm_phys(vm, pa, count * sizeof(MimeNetPoll)) : NULL;
    if (count && !entries)
        return MIME_HC_STATUS_INVALID;
    struct pollfd fds[NET_MAX_POLL];
    NetSocket *socks[NET_MAX_POLL];
    for (uint64_t i = 0; i < count; i++) {
        socks[i] = lookup(entries[i].socket);
        fds[i].fd = socks[i] ? socks[i]->fd : -1;
        fds[i].events = POLLIN | POLLOUT;
        fds[i].revents = 0;
    }
    if (count)
        poll(fds, (nfds_t)count, 0);
    uint64_t ready = 0;
    struct kevent changes[2 * NET_MAX_POLL];
    int nchanges = 0;
    for (uint64_t i = 0; i < count; i++) {
        MimeNetPoll *e = &entries[i];
        int32_t error = 0;
        e->revents = socks[i] ? readiness(socks[i], fds[i].revents, &error) : MIME_NET_INVALID;
        e->error = error;
        bool read_done = e->revents & (MIME_NET_READ | MIME_NET_HUP | MIME_NET_ERROR | MIME_NET_INVALID);
        bool write_done = e->revents & (MIME_NET_WRITE | MIME_NET_ERROR | MIME_NET_INVALID);
        if (((e->events & MIME_NET_READ) && read_done) || ((e->events & MIME_NET_WRITE) && write_done))
            ready++;
        if (!arm || !socks[i])
            continue;
        /* armed even when ready: a watch that fires at once costs a wakeup, a missed one a stall. */
        void *udata = (void *)(uintptr_t)e->socket;
        if (e->events & MIME_NET_READ)
            EV_SET(&changes[nchanges++], fds[i].fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, udata);
        if (e->events & MIME_NET_WRITE)
            EV_SET(&changes[nchanges++], fds[i].fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, udata);
    }
    if (nchanges)
        kevent(g_kq, changes, nchanges, NULL, 0, NULL);
    *value = ready;
    return MIME_HC_STATUS_OK;
}

static uint64_t net_events(MimeVm *vm, uint64_t pa, uint64_t max, uint64_t *value)
{
    uint32_t *out = max ? mime_vm_phys(vm, pa, max * sizeof(uint32_t)) : NULL;
    if (max && !out)
        return MIME_HC_STATUS_INVALID;
    uint64_t n = 0;
    while (n < max && g_ring_tail != g_ring_head) {
        uint32_t id = g_ring[g_ring_tail];
        g_ring_tail = (g_ring_tail + 1) % NET_RING;
        if (!(id & MIME_NET_RESOLVED)) {
            NetSocket *s = lookup(id);
            if (!s)
                continue;
            s->queued = false;
        }
        out[n++] = id;
    }
    if (g_ring_tail != g_ring_head)
        __atomic_fetch_or(&vm->shared->irq_pending, MIME_IRQ_NET, __ATOMIC_SEQ_CST);
    *value = n;
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------ names and interfaces */

static int32_t resolve_error(int e)
{
    switch (e) {
    case EAI_NONAME:
        return 11001; /* WSAHOST_NOT_FOUND */
    case EAI_AGAIN:
        return 11002; /* WSATRY_AGAIN */
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
    case EAI_NODATA:
        return 11004; /* WSANO_DATA */
#endif
    case EAI_MEMORY:
        return 10055;
    default:
        return 11003; /* WSANO_RECOVERY */
    }
}

static void *resolve_thread(void *arg)
{
    NetResolve *r = arg;
    struct addrinfo hints = {0}, *list = NULL;
    hints.ai_family = r->family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    int e = getaddrinfo(r->name, NULL, &hints, &list);
    pthread_mutex_lock(&g_lock);
    if (e) {
        r->error = resolve_error(e);
    } else {
        for (struct addrinfo *ai = list; ai && r->count < NET_MAX_ANSWERS; ai = ai->ai_next) {
            if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
                continue;
            MimeNetAddr a;
            from_host(ai->ai_addr, &a);
            bool seen = false;
            for (uint32_t i = 0; i < r->count && !seen; i++)
                seen = !memcmp(&r->addrs[i], &a, sizeof(a));
            if (!seen)
                r->addrs[r->count++] = a;
        }
        if (!r->count)
            r->error = 11004;
        freeaddrinfo(list);
    }
    r->done = true;
    push_ring(MIME_NET_RESOLVED | r->id);
    pthread_mutex_unlock(&g_lock);
    mime_vm_raise_irq(g_vm, MIME_IRQ_NET);
    return NULL;
}

static uint64_t net_resolve(MimeVm *vm, uint64_t pa, uint64_t len, uint64_t family, uint64_t *value)
{
    const char *name = len ? mime_vm_phys(vm, pa, len) : NULL;
    if (!name || len >= sizeof(((NetResolve *)0)->name))
        return MIME_HC_STATUS_INVALID;
    if (!start(vm))
        return fail(value, 10050);
    NetResolve *r = calloc(1, sizeof(*r));
    if (!r)
        return fail(value, 10055);
    memcpy(r->name, name, len);
    r->family = family == WIN_AF_INET ? AF_INET : family == WIN_AF_INET6 ? AF_INET6 : AF_UNSPEC;
    g_next_resolve = (g_next_resolve + 1) & ~MIME_NET_RESOLVED;
    if (!g_next_resolve)
        g_next_resolve = 1;
    r->id = g_next_resolve;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    r->next = g_resolves;
    g_resolves = r;
    if (pthread_create(&thread, &attr, resolve_thread, r)) {
        g_resolves = r->next;
        free(r);
        pthread_attr_destroy(&attr);
        return fail(value, 10055);
    }
    pthread_attr_destroy(&attr);
    *value = r->id;
    return MIME_HC_STATUS_OK;
}

static uint64_t net_answer(MimeVm *vm, uint64_t id, uint64_t pa, uint64_t max, uint64_t *value)
{
    NetResolve **p = &g_resolves;
    while (*p && (*p)->id != id)
        p = &(*p)->next;
    NetResolve *r = *p;
    if (!r)
        return MIME_HC_STATUS_NOT_FOUND;
    if (!r->done)
        return fail(value, 10035);
    *p = r->next;
    uint64_t status = MIME_HC_STATUS_OK;
    if (r->error) {
        status = fail(value, r->error);
    } else {
        uint64_t n = r->count < max ? r->count : max;
        MimeNetAddr *out = n ? mime_vm_phys(vm, pa, n * sizeof(MimeNetAddr)) : NULL;
        if (n && !out) {
            status = MIME_HC_STATUS_INVALID;
        } else {
            if (n)
                memcpy(out, r->addrs, n * sizeof(MimeNetAddr));
            *value = n;
        }
    }
    free(r);
    return status;
}

static uint8_t prefix_bits(const struct sockaddr *mask)
{
    if (!mask)
        return 0;
    const uint8_t *b;
    size_t len;
    if (mask->sa_family == AF_INET6) {
        b = (const uint8_t *)&((const struct sockaddr_in6 *)mask)->sin6_addr;
        len = 16;
    } else {
        b = (const uint8_t *)&((const struct sockaddr_in *)mask)->sin_addr;
        len = 4;
    }
    uint8_t bits = 0;
    for (size_t i = 0; i < len; i++)
        bits += (uint8_t)__builtin_popcount(b[i]);
    return bits;
}

static uint64_t net_interfaces(MimeVm *vm, uint64_t pa, uint64_t max, uint64_t *value)
{
    MimeNetInterface *out = max ? mime_vm_phys(vm, pa, max * sizeof(MimeNetInterface)) : NULL;
    if (max && !out)
        return MIME_HC_STATUS_INVALID;
    struct ifaddrs *list;
    if (getifaddrs(&list) < 0)
        return fail_errno(value);
    uint64_t n = 0;
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || (ifa->ifa_addr->sa_family != AF_INET && ifa->ifa_addr->sa_family != AF_INET6))
            continue;
        if (n < max) {
            MimeNetInterface *e = &out[n];
            memset(e, 0, sizeof(*e));
            strlcpy(e->name, ifa->ifa_name, sizeof(e->name));
            e->index = if_nametoindex(ifa->ifa_name);
            if (ifa->ifa_flags & IFF_UP)
                e->flags |= MIME_NET_IF_UP;
            if (ifa->ifa_flags & IFF_LOOPBACK)
                e->flags |= MIME_NET_IF_LOOPBACK;
            if (ifa->ifa_flags & IFF_BROADCAST)
                e->flags |= MIME_NET_IF_BROADCAST;
            if (ifa->ifa_flags & IFF_MULTICAST)
                e->flags |= MIME_NET_IF_MULTICAST;
            if (ifa->ifa_flags & IFF_POINTOPOINT)
                e->flags |= MIME_NET_IF_POINTTOPOINT;
            e->prefix = prefix_bits(ifa->ifa_netmask);
            from_host(ifa->ifa_addr, &e->addr);
            /* the link entry of the same interface has the mtu and the hardware address. */
            for (struct ifaddrs *link = list; link; link = link->ifa_next) {
                if (!link->ifa_addr || link->ifa_addr->sa_family != AF_LINK || strcmp(link->ifa_name, ifa->ifa_name))
                    continue;
                const struct sockaddr_dl *dl = (const struct sockaddr_dl *)link->ifa_addr;
                if (dl->sdl_alen && dl->sdl_alen <= sizeof(e->mac)) {
                    e->mac_length = dl->sdl_alen;
                    memcpy(e->mac, LLADDR(dl), dl->sdl_alen);
                }
                if (link->ifa_data)
                    e->mtu = ((const struct if_data *)link->ifa_data)->ifi_mtu;
                break;
            }
        }
        n++;
    }
    freeifaddrs(list);
    *value = n < max ? n : max;
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------ dispatch */

uint64_t mime_net_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8])
{
    uint64_t value = 0, status = MIME_HC_STATUS_OK;
    pthread_mutex_lock(&g_lock);
    NetSocket *s = NULL;
    if (nr >= MIME_HC_NET_CLOSE && nr <= MIME_HC_NET_OPTION) {
        s = g_kq >= 0 ? lookup(regs[1]) : NULL;
        if (!s) {
            status = fail(&value, 10038); /* WSAENOTSOCK */
            goto done;
        }
    }
    switch (nr) {
    case MIME_HC_NET_SOCKET:
        status = net_socket(vm, regs[1], regs[2], regs[3], &value);
        break;
    case MIME_HC_NET_CLOSE:
        close_socket(s);
        break;
    case MIME_HC_NET_BIND: {
        const MimeNetAddr *a = guest_addr(vm, regs[2]);
        struct sockaddr_storage ss;
        socklen_t sl;
        if (!a)
            status = MIME_HC_STATUS_INVALID;
        else if (!to_host(a, &ss, &sl))
            status = fail(&value, 10047);
        else if (bind(s->fd, (struct sockaddr *)&ss, sl) < 0)
            status = fail_errno(&value);
        break;
    }
    case MIME_HC_NET_LISTEN:
        if (listen(s->fd, (int)(regs[2] > 4096 ? 4096 : regs[2])) < 0)
            status = fail_errno(&value);
        else
            s->listening = true;
        break;
    case MIME_HC_NET_CONNECT: {
        const MimeNetAddr *a = guest_addr(vm, regs[2]);
        struct sockaddr_storage ss;
        socklen_t sl;
        if (!a)
            status = MIME_HC_STATUS_INVALID;
        else if (!to_host(a, &ss, &sl))
            status = fail(&value, 10047);
        else if (connect(s->fd, (struct sockaddr *)&ss, sl) < 0)
            status = fail_errno(&value);
        break;
    }
    case MIME_HC_NET_ACCEPT: {
        MimeNetAddr *a = guest_addr(vm, regs[2]);
        struct sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        int fd = accept(s->fd, (struct sockaddr *)&ss, &sl);
        if (fd < 0) {
            status = fail_errno(&value);
            break;
        }
        prepare_fd(fd, true);
        NetSocket *c = add_socket(fd, true);
        if (!c) {
            close(fd);
            status = fail(&value, 10024);
            break;
        }
        if (a)
            from_host((struct sockaddr *)&ss, a);
        value = socket_id(c);
        break;
    }
    case MIME_HC_NET_SEND:
        status = net_send(vm, s, regs[2], regs[3], regs[4], regs[5], &value);
        break;
    case MIME_HC_NET_RECV:
        status = net_recv(vm, s, regs[2], regs[3], regs[4], regs[5], &value);
        break;
    case MIME_HC_NET_SHUTDOWN: {
        int how = regs[2] == 0 ? SHUT_RD : regs[2] == 1 ? SHUT_WR : SHUT_RDWR;
        if (shutdown(s->fd, how) < 0)
            status = fail_errno(&value);
        break;
    }
    case MIME_HC_NET_NAME: {
        MimeNetAddr *a = guest_addr(vm, regs[3]);
        struct sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        memset(&ss, 0, sizeof(ss));
        int r = regs[2] ? getpeername(s->fd, (struct sockaddr *)&ss, &sl) : getsockname(s->fd, (struct sockaddr *)&ss, &sl);
        if (!a)
            status = MIME_HC_STATUS_INVALID;
        else if (r < 0)
            status = fail_errno(&value);
        else
            from_host((struct sockaddr *)&ss, a);
        break;
    }
    case MIME_HC_NET_OPTION:
        status = net_option(s, regs[2], regs[3] != 0, regs[4], &value);
        break;
    case MIME_HC_NET_POLL:
        status = g_kq >= 0 ? net_poll(vm, regs[1], regs[2], regs[3] != 0, &value) : MIME_HC_STATUS_OK;
        break;
    case MIME_HC_NET_EVENTS:
        status = net_events(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_NET_RESOLVE:
        status = net_resolve(vm, regs[1], regs[2], regs[3], &value);
        break;
    case MIME_HC_NET_ANSWER:
        status = net_answer(vm, regs[1], regs[2], regs[3], &value);
        break;
    case MIME_HC_NET_INTERFACES:
        status = net_interfaces(vm, regs[1], regs[2], &value);
        break;
    default:
        status = MIME_HC_STATUS_UNSUPPORTED;
        break;
    }
done:
    pthread_mutex_unlock(&g_lock);
    regs[1] = value;
    return status;
}

void mime_net_close_all(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_kq < 0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    for (uint32_t i = 0; i < NET_SLOTS; i++) {
        if (g_sockets[i].fd >= 0)
            close_socket(&g_sockets[i]);
    }
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(g_kq, &ev, 1, NULL, 0, NULL);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_thread, NULL);
    close(g_kq);
    g_kq = -1;
}
