/*
 * nonblocking host sockets for the guest. MIME_HC_NET_POLL checks readiness and arms watches; a kqueue or
 * epoll thread queues ready sockets and raises MIME_IRQ_NET. DNS lookups run on separate threads so slow
 * resolvers never stall vCPUs.
 */
#include "vmm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef __APPLE__
#include <net/if_dl.h>
#include <sys/event.h>
#else
#include <linux/if_packet.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define NET_SLOTS 4096
#define NET_SLOT_MASK (NET_SLOTS - 1)
#define NET_RING 8192
#define NET_MAX_POLL 1024
#define NET_MAX_ANSWERS 64

/* Windows socket constants used by the guest. */
#define WIN_AF_INET 2
#define WIN_AF_INET6 23
#define WIN_SOCK_STREAM 1
#define WIN_SOCK_DGRAM 2
#define WIN_MSG_OOB 1
#define WIN_MSG_PEEK 2
#define WIN_MSG_DONTROUTE 4

typedef struct NetSocket {
    int fd; /* -1 when the slot is free. */
    uint32_t gen;
    bool stream;
    bool listening;
    bool queued; /* in the ring. */
    bool remote;  /* connected, or connecting, to another host than this one. */
    bool severed; /* a remote connection aborted when the machine went offline. */
    uint64_t polled; /* the poll that listed it first, and where. */
    uint32_t polled_at;
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
static int g_kq = -1; /* the kqueue, or the epoll instance on Linux. */
#ifndef __APPLE__
static int g_stop_fd = -1;
#define STOP_WATCH UINT64_MAX
#endif
static pthread_t g_thread;
static NetSocket g_sockets[NET_SLOTS];
static uint32_t g_next_slot;
static uint32_t g_ring[NET_RING];
static uint32_t g_ring_head, g_ring_tail;
static NetResolve *g_resolves;
static uint32_t g_next_resolve;
static bool g_offline;

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

/* ------------------------------------------------------------ addresses. */

static bool to_host(const MimeNetAddr *a, struct sockaddr_storage *ss, socklen_t *len)
{
    memset(ss, 0, sizeof(*ss));
    if (a->family == WIN_AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)ss;
#ifdef __APPLE__
        in->sin_len = sizeof(*in);
#endif
        in->sin_family = AF_INET;
        memcpy(&in->sin_port, a->data, 2);
        memcpy(&in->sin_addr, a->data + 2, 4);
        *len = sizeof(*in);
        return true;
    }
    if (a->family == WIN_AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;
#ifdef __APPLE__
        in6->sin6_len = sizeof(*in6);
#endif
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
    return pa ? mime_vm_phys(vm, pa, sizeof(MimeNetAddr)) : nullptr;
}

static bool loopback(const struct sockaddr_storage *ss)
{
    if (ss->ss_family == AF_INET)
        return ((const uint8_t *)&((const struct sockaddr_in *)ss)->sin_addr)[0] == 127;
    if (ss->ss_family == AF_INET6) {
        const struct in6_addr *a = &((const struct sockaddr_in6 *)ss)->sin6_addr;
        return IN6_IS_ADDR_LOOPBACK(a) || (IN6_IS_ADDR_V4MAPPED(a) && a->s6_addr[12] == 127);
    }
    return false;
}

static bool unspecified(const struct sockaddr_storage *ss)
{
    if (ss->ss_family == AF_INET)
        return ((const struct sockaddr_in *)ss)->sin_addr.s_addr == INADDR_ANY;
    return ss->ss_family == AF_INET6 && IN6_IS_ADDR_UNSPECIFIED(&((const struct sockaddr_in6 *)ss)->sin6_addr);
}

/* offline, only this machine answers. */
static bool reachable(const struct sockaddr_storage *ss)
{
    return !__atomic_load_n(&g_offline, __ATOMIC_ACQUIRE) || loopback(ss);
}

/* ------------------------------------------------------------ the socket table and the ring. */

static NetSocket *lookup(uint64_t id)
{
    if (!id || id > UINT32_MAX)
        return nullptr;
    NetSocket *s = &g_sockets[id & NET_SLOT_MASK];
    if (s->fd < 0 || s->gen != (uint32_t)(id >> 12))
        return nullptr;
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
#ifdef __APPLE__
    struct kevent events[64];
#else
    struct epoll_event events[64];
#endif
    for (;;) {
#ifdef __APPLE__
        int n = kevent(g_kq, nullptr, 0, events, 64, nullptr);
#else
        int n = epoll_wait(g_kq, events, 64, -1);
#endif
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        bool raise = false;
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < n; i++) {
#ifdef __APPLE__
            if (events[i].filter == EVFILT_USER) {
                pthread_mutex_unlock(&g_lock);
                return nullptr;
            }
            NetSocket *s = lookup((uint64_t)(uintptr_t)events[i].udata);
#else
            if (events[i].data.u64 == STOP_WATCH) {
                pthread_mutex_unlock(&g_lock);
                return nullptr;
            }
            NetSocket *s = lookup(events[i].data.u64);
#endif
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
    return nullptr;
}

/* called with the lock held. */
static bool start(MimeVm *vm)
{
    if (g_kq >= 0)
        return true;
    for (uint32_t i = 0; i < NET_SLOTS; i++)
        g_sockets[i].fd = -1;
    g_vm = vm;
#ifdef __APPLE__
    g_kq = kqueue();
    if (g_kq < 0)
        return false;
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(g_kq, &ev, 1, nullptr, 0, nullptr);
#else
    g_kq = epoll_create1(EPOLL_CLOEXEC);
    g_stop_fd = eventfd(0, EFD_CLOEXEC);
    if (g_kq < 0 || g_stop_fd < 0)
        return false;
    struct epoll_event ev = {.events = EPOLLIN, .data.u64 = STOP_WATCH};
    epoll_ctl(g_kq, EPOLL_CTL_ADD, g_stop_fd, &ev);
#endif
    if (pthread_create(&g_thread, nullptr, watch_thread, nullptr)) {
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
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
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
        s->remote = false;
        s->severed = false;
        return s;
    }
    return nullptr;
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

/* ------------------------------------------------------------ operations. */

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
    if (s->severed)
        return fail(value, 10053); /* WSAECONNABORTED */
    ssize_t n;
    if (addr_pa) {
        const MimeNetAddr *a = guest_addr(vm, addr_pa);
        struct sockaddr_storage ss;
        socklen_t sl;
        if (!a || !to_host(a, &ss, &sl))
            return fail(value, 10047);
        if (!reachable(&ss))
            return fail(value, 10051); /* WSAENETUNREACH */
        n = sendto(s->fd, data, len, host_flags(flags), (struct sockaddr *)&ss, sl);
    } else {
        if (s->remote && __atomic_load_n(&g_offline, __ATOMIC_ACQUIRE))
            return fail(value, 10051);
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
    MimeNetAddr *a = nullptr;
    if (addr_pa && !(a = guest_addr(vm, addr_pa)))
        return MIME_HC_STATUS_INVALID;
    if (s->severed)
        return fail(value, 10053);
    struct sockaddr_storage ss;
    struct iovec iov = {data, len};
    struct msghdr msg = {};
    ssize_t n;
    for (;;) {
        msg.msg_name = &ss;
        msg.msg_namelen = sizeof(ss);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        memset(&ss, 0, sizeof(ss));
        n = recvmsg(s->fd, &msg, host_flags(flags));
        /* offline, datagrams from other hosts are dropped. */
        if (n < 0 || s->stream || !msg.msg_namelen || reachable(&ss))
            break;
        if (flags & WIN_MSG_PEEK)
            recv(s->fd, &dummy, 1, 0);
    }
    if (n < 0) {
        /* an ICMP port unreachable of a datagram reports as a reset on windows. */
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
        return fail(value, 10040); /* WSAEMSGSIZE, the buffer is full. */
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
#ifdef __APPLE__
        r = sockopt_int(fd, IPPROTO_TCP, TCP_KEEPALIVE, set, (int)v, value);
#else
        r = sockopt_int(fd, IPPROTO_TCP, TCP_KEEPIDLE, set, (int)v, value);
#endif
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
        /* a lingering close would block the vCPU, so only an abortive one reaches the host. */
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
#ifdef __APPLE__
        if (!s->stream) {
            socklen_t nl = sizeof(n);
            r = getsockopt(fd, SOL_SOCKET, SO_NREAD, &n, &nl);
        } else {
            r = ioctl(fd, FIONREAD, &n);
        }
#else
        /* Linux answers the size of the next datagram for a datagram socket. */
        r = ioctl(fd, FIONREAD, &n);
#endif
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

/* reports readiness and retrieves the pending socket error. */
static uint16_t readiness(NetSocket *s, short revents, int32_t *error)
{
    uint16_t r = 0;
    *error = 0;
    if (revents & POLLNVAL)
        return MIME_NET_INVALID;
    if (s->severed) {
        *error = 10053;
        return MIME_NET_ERROR;
    }
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
    MimeNetPoll *entries = count ? mime_vm_phys(vm, pa, count * sizeof(MimeNetPoll)) : nullptr;
    if (count && !entries)
        return MIME_HC_STATUS_INVALID;
    struct pollfd fds[NET_MAX_POLL];
    NetSocket *socks[NET_MAX_POLL];
    /*
     * select lists a socket once for each of its sets. macos reports a descriptor that is listed twice in one of its
     * entries only, so the others are not polled and take its answer.
     */
    static uint64_t generation;
    generation++;
    for (uint64_t i = 0; i < count; i++) {
        socks[i] = lookup(entries[i].socket);
        fds[i].fd = socks[i] ? socks[i]->fd : -1;
        fds[i].events = POLLIN | POLLOUT;
        fds[i].revents = 0;
        if (socks[i] && socks[i]->polled == generation) {
            fds[i].fd = -1;
        } else if (socks[i]) {
            socks[i]->polled = generation;
            socks[i]->polled_at = (uint32_t)i;
        }
    }
    if (count)
        poll(fds, (nfds_t)count, 0);
    for (uint64_t i = 0; i < count; i++) {
        if (socks[i] && socks[i]->polled_at != i) {
            fds[i].fd = socks[i]->fd;
            fds[i].revents = fds[socks[i]->polled_at].revents;
        }
    }
    uint64_t ready = 0;
#ifdef __APPLE__
    struct kevent changes[2 * NET_MAX_POLL];
    int nchanges = 0;
#endif
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
#ifdef __APPLE__
        void *udata = (void *)(uintptr_t)e->socket;
        if (e->events & MIME_NET_READ)
            EV_SET(&changes[nchanges++], fds[i].fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, udata);
        if (e->events & MIME_NET_WRITE)
            EV_SET(&changes[nchanges++], fds[i].fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, udata);
#else
        /* one watch per descriptor: both directions at once, until it fires. */
        struct epoll_event ev = {.events = EPOLLONESHOT | EPOLLRDHUP, .data.u64 = e->socket};
        if (e->events & MIME_NET_READ)
            ev.events |= EPOLLIN;
        if (e->events & MIME_NET_WRITE)
            ev.events |= EPOLLOUT;
        if ((e->events & (MIME_NET_READ | MIME_NET_WRITE)) &&
            epoll_ctl(g_kq, EPOLL_CTL_MOD, fds[i].fd, &ev) != 0 && errno == ENOENT)
            epoll_ctl(g_kq, EPOLL_CTL_ADD, fds[i].fd, &ev);
#endif
    }
#ifdef __APPLE__
    if (nchanges)
        kevent(g_kq, changes, nchanges, nullptr, 0, nullptr);
#endif
    *value = ready;
    return MIME_HC_STATUS_OK;
}

static uint64_t net_events(MimeVm *vm, uint64_t pa, uint64_t max, uint64_t *value)
{
    uint32_t *out = max ? mime_vm_phys(vm, pa, max * sizeof(uint32_t)) : nullptr;
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

/* ------------------------------------------------------------ names and interfaces. */

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

static bool localhost(const char *name)
{
    size_t n = strlen(name);
    if (n && name[n - 1] == '.')
        n--;
    return (n == 9 && !strncasecmp(name, "localhost", 9)) || (n > 10 && !strncasecmp(name + n - 10, ".localhost", 10));
}

static void *resolve_thread(void *arg)
{
    NetResolve *r = arg;
    struct addrinfo hints = {}, *list = nullptr;
    hints.ai_family = r->family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    /* offline, there is no server to ask: addresses and localhost alone resolve. */
    if (__atomic_load_n(&g_offline, __ATOMIC_ACQUIRE) && !localhost(r->name))
        hints.ai_flags |= AI_NUMERICHOST;
    int e = getaddrinfo(r->name, nullptr, &hints, &list);
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
    return nullptr;
}

static uint64_t net_resolve(MimeVm *vm, uint64_t pa, uint64_t len, uint64_t family, uint64_t *value)
{
    const char *name = len ? mime_vm_phys(vm, pa, len) : nullptr;
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
        MimeNetAddr *out = n ? mime_vm_phys(vm, pa, n * sizeof(MimeNetAddr)) : nullptr;
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
    MimeNetInterface *out = max ? mime_vm_phys(vm, pa, max * sizeof(MimeNetInterface)) : nullptr;
    if (max && !out)
        return MIME_HC_STATUS_INVALID;
    struct ifaddrs *list;
    if (getifaddrs(&list) < 0)
        return fail_errno(value);
    uint64_t n = 0;
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || (ifa->ifa_addr->sa_family != AF_INET && ifa->ifa_addr->sa_family != AF_INET6))
            continue;
        if (g_offline && !(ifa->ifa_flags & IFF_LOOPBACK))
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
            if (ifa->ifa_flags & IFF_RUNNING)
                e->flags |= MIME_NET_IF_RUNNING;
            e->prefix = prefix_bits(ifa->ifa_netmask);
            from_host(ifa->ifa_addr, &e->addr);
            /* the link entry of the same interface has the MTU and the hardware address. */
            for (struct ifaddrs *link = list; link; link = link->ifa_next) {
#ifdef __APPLE__
                if (!link->ifa_addr || link->ifa_addr->sa_family != AF_LINK || strcmp(link->ifa_name, ifa->ifa_name))
                    continue;
                const struct sockaddr_dl *dl = (const struct sockaddr_dl *)link->ifa_addr;
                if (dl->sdl_alen && dl->sdl_alen <= sizeof(e->mac)) {
                    e->mac_length = dl->sdl_alen;
                    memcpy(e->mac, LLADDR(dl), dl->sdl_alen);
                }
                if (link->ifa_data)
                    e->mtu = ((const struct if_data *)link->ifa_data)->ifi_mtu;
#else
                if (!link->ifa_addr || link->ifa_addr->sa_family != AF_PACKET || strcmp(link->ifa_name, ifa->ifa_name))
                    continue;
                const struct sockaddr_ll *ll = (const struct sockaddr_ll *)link->ifa_addr;
                if (ll->sll_halen && ll->sll_halen <= sizeof(e->mac)) {
                    e->mac_length = ll->sll_halen;
                    memcpy(e->mac, ll->sll_addr, ll->sll_halen);
                }
                struct ifreq req;
                memset(&req, 0, sizeof(req));
                mime_strlcpy(req.ifr_name, ifa->ifa_name, sizeof(req.ifr_name));
                int probe = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
                if (probe >= 0) {
                    if (ioctl(probe, SIOCGIFMTU, &req) == 0)
                        e->mtu = (uint32_t)req.ifr_mtu;
                    close(probe);
                }
#endif
                break;
            }
        }
        n++;
    }
    freeifaddrs(list);
    *value = n < max ? n : max;
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------ discord. */

/* where Discord, its Flatpak and Snap packages and the clients built like it serve discord-ipc-N. */
static const char *const g_ipc_dirs[] = {"", "/app/com.discordapp.Discord", "/app/com.discordapp.DiscordCanary",
                                         "/app/com.discordapp.DiscordPTB", "/app/dev.vencord.Vesktop",
                                         "/snap.discord", "/snap.discord-canary"};

static int ipc_connect(unsigned index, bool probe)
{
    char run[256];
    const char *base = getenv("XDG_RUNTIME_DIR");
    if (!base || !*base) {
        snprintf(run, sizeof(run), "/run/user/%u", (unsigned)getuid());
        base = run;
    }
    char darwin[256] = "";
#ifdef __APPLE__
    /* Discord serves its socket in the user's temporary folder, which the machine's environment may not name. */
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, darwin, sizeof(darwin)) > sizeof(darwin))
        darwin[0] = 0;
#endif
    const char *temps[] = {getenv("TMPDIR"), darwin, getenv("TMP"), getenv("TEMP"), "/tmp"};
    size_t runs = sizeof(g_ipc_dirs) / sizeof(g_ipc_dirs[0]), all = runs + sizeof(temps) / sizeof(temps[0]);
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    for (size_t i = 0; i < all; i++) {
        const char *dir = i < runs ? base : temps[i - runs];
        const char *sub = i < runs ? g_ipc_dirs[i] : "";
        if (!dir || !*dir)
            continue;
        if ((size_t)snprintf(sa.sun_path, sizeof(sa.sun_path), "%s%s/discord-ipc-%u", dir, sub, index) >= sizeof(sa.sun_path))
            continue;
        struct stat st;
        if (stat(sa.sun_path, &st) != 0 || !S_ISSOCK(st.st_mode))
            continue;
        if (probe)
            return 0;
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return -1;
        if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0)
            return fd;
        close(fd);
    }
    return -1;
}

static uint64_t net_ipc(MimeVm *vm, uint64_t index, bool probe, uint64_t *value)
{
    if (index > 9)
        return fail(value, 10022);
    if (!probe && !start(vm))
        return fail(value, 10050);
    int fd = ipc_connect((unsigned)index, probe);
    if (fd < 0)
        return fail(value, 10061); /* WSAECONNREFUSED */
    if (probe)
        return MIME_HC_STATUS_OK;
    prepare_fd(fd, false);
    NetSocket *s = add_socket(fd, true);
    if (!s) {
        close(fd);
        return fail(value, 10024);
    }
    *value = socket_id(s);
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------ dispatch. */

uint64_t mime_net_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8])
{
    uint64_t value = 0, status = MIME_HC_STATUS_OK;
    /* reading the configuration may take a while and touches no socket, so it runs without the lock. */
    if (nr == MIME_HC_NET_CONFIG) {
        status = mime_net_config(vm, regs[1], regs[2], &value);
        regs[1] = value;
        return status;
    }
    pthread_mutex_lock(&g_lock);
    NetSocket *s = nullptr;
    if (nr >= MIME_HC_NET_CLOSE && nr <= MIME_HC_NET_OPTION) {
        s = g_kq >= 0 ? lookup(regs[1]) : nullptr;
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
        else if (!unspecified(&ss) && !reachable(&ss))
            status = fail(&value, 10049); /* WSAEADDRNOTAVAIL, the addresses of the network went away. */
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
        if (!a) {
            status = MIME_HC_STATUS_INVALID;
        } else if (!to_host(a, &ss, &sl)) {
            status = fail(&value, 10047);
        } else if (s->severed) {
            status = fail(&value, 10053);
        } else if (!reachable(&ss)) {
            status = fail(&value, 10051);
        } else {
            int r = connect(s->fd, (struct sockaddr *)&ss, sl);
            if (r == 0 || errno == EINPROGRESS)
                s->remote = !loopback(&ss);
            if (r < 0)
                status = fail_errno(&value);
        }
        break;
    }
    case MIME_HC_NET_ACCEPT: {
        MimeNetAddr *a = guest_addr(vm, regs[2]);
        struct sockaddr_storage ss;
        int fd;
        for (;;) {
            socklen_t sl = sizeof(ss);
            fd = accept(s->fd, (struct sockaddr *)&ss, &sl);
            /* offline, connections from other hosts are refused. */
            if (fd < 0 || reachable(&ss))
                break;
            close(fd);
        }
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
        c->remote = !loopback(&ss);
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
    case MIME_HC_NET_IPC:
        status = net_ipc(vm, regs[1], regs[2] != 0, &value);
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

bool mime_net_offline(void)
{
    return __atomic_load_n(&g_offline, __ATOMIC_ACQUIRE);
}

/* going offline aborts the connections to other hosts, as a network that goes away does. */
void mime_net_set_offline(bool offline)
{
    pthread_mutex_lock(&g_lock);
    __atomic_store_n(&g_offline, offline, __ATOMIC_RELEASE);
    bool raise = false;
    for (uint32_t i = 0; offline && g_kq >= 0 && i < NET_SLOTS; i++) {
        NetSocket *s = &g_sockets[i];
        if (s->fd < 0 || !s->stream || !s->remote || s->severed)
            continue;
        s->severed = true;
        shutdown(s->fd, SHUT_RDWR);
        if (!s->queued) {
            s->queued = true;
            push_ring(socket_id(s));
            raise = true;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (raise)
        mime_vm_raise_irq(g_vm, MIME_IRQ_NET);
    mime_info("the machine is %s", offline ? "offline" : "online");
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
#ifdef __APPLE__
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    kevent(g_kq, &ev, 1, nullptr, 0, nullptr);
#else
    uint64_t one = 1;
    if (write(g_stop_fd, &one, sizeof(one)) < 0)
        mime_warn("cannot stop the network watch");
#endif
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_thread, nullptr);
    close(g_kq);
    g_kq = -1;
#ifndef __APPLE__
    close(g_stop_fd);
    g_stop_fd = -1;
#endif
}
