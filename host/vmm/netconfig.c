/*
 * host network configuration for guest IP Helper: links, addresses and lifetimes, gateway routes, DNS servers
 * and domains. Linux uses netlink, sysfs and resolv.conf; macOS uses routing sysctl, interface ioctls and
 * resolv.conf.
 */
#include "vmm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __APPLE__
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/route.h>
#include <sys/sysctl.h>
#else
#include <linux/if_arp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#endif

#define WIN_AF_INET 2
#define WIN_AF_INET6 23

#define CONFIG_LINKS 64
#define CONFIG_ADDRESSES 512
#define CONFIG_ROUTES 256

typedef struct {
    MimeNetConfig head;
    MimeNetLink links[CONFIG_LINKS];
    MimeNetAddress addresses[CONFIG_ADDRESSES];
    MimeNetRoute routes[CONFIG_ROUTES];
} Config;

static void set_addr(MimeNetAddr *a, int family, const void *bytes, uint32_t scope)
{
    memset(a, 0, sizeof(*a));
    if (family == AF_INET) {
        a->family = WIN_AF_INET;
        memcpy(a->data + 2, bytes, 4);
    } else {
        a->family = WIN_AF_INET6;
        memcpy(a->data + 6, bytes, 16);
        memcpy(a->data + 22, &scope, 4);
    }
}

static bool link_local6(const uint8_t *b)
{
    return b[0] == 0xFE && (b[1] & 0xC0) == 0x80;
}

static MimeNetLink *find_link(Config *c, uint32_t index)
{
    for (uint32_t i = 0; i < c->head.links; i++)
        if (c->links[i].index == index)
            return &c->links[i];
    return nullptr;
}

static void add_route(Config *c, uint32_t index, int family, const void *destination, uint8_t prefix,
                      const void *gateway, uint32_t metric, uint8_t origin)
{
    if (c->head.routes >= CONFIG_ROUTES)
        return;
    MimeNetRoute *r = &c->routes[c->head.routes++];
    memset(r, 0, sizeof(*r));
    r->index = index;
    r->metric = metric;
    r->prefix = prefix;
    r->origin = origin;
    static const uint8_t zero[16];
    set_addr(&r->destination, family, destination ? destination : zero, 0);
    set_addr(&r->gateway, family, gateway, family == AF_INET6 && link_local6(gateway) ? index : 0);
}

/* ------------------------------------------------------------ the resolver. */

static void read_resolver(Config *c)
{
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *save = nullptr;
        char *key = strtok_r(line, " \t\r\n", &save);
        if (!key || key[0] == '#' || key[0] == ';')
            continue;
        if (!strcmp(key, "nameserver")) {
            char *value = strtok_r(nullptr, " \t\r\n", &save);
            if (!value || c->head.servers >= MIME_NET_MAX_DNS)
                continue;
            uint32_t scope = 0;
            char *percent = strchr(value, '%');
            if (percent) {
                *percent = 0;
                scope = if_nametoindex(percent + 1);
                if (!scope)
                    scope = (uint32_t)strtoul(percent + 1, nullptr, 10);
            }
            uint8_t bytes[16];
            if (inet_pton(AF_INET, value, bytes) == 1)
                set_addr(&c->head.server[c->head.servers++], AF_INET, bytes, 0);
            else if (inet_pton(AF_INET6, value, bytes) == 1)
                set_addr(&c->head.server[c->head.servers++], AF_INET6, bytes, scope);
        } else if (!strcmp(key, "domain")) {
            char *value = strtok_r(nullptr, " \t\r\n", &save);
            if (value)
                mime_strlcpy(c->head.domain, value, sizeof(c->head.domain));
        } else if (!strcmp(key, "search")) {
            /* the last search line wins, as in the host's resolver. */
            c->head.search[0] = 0;
            for (char *value; (value = strtok_r(nullptr, " \t\r\n", &save));) {
                size_t used = strlen(c->head.search);
                if (used + strlen(value) + 2 > sizeof(c->head.search))
                    break;
                if (used)
                    c->head.search[used++] = ' ';
                mime_strlcpy(c->head.search + used, value, sizeof(c->head.search) - used);
            }
        }
    }
    fclose(f);
}

#ifdef __APPLE__
/* ------------------------------------------------------------ macOS. */

static void read_links(Config *c)
{
    struct ifaddrs *list;
    if (getifaddrs(&list) < 0)
        return;
    int probe = socket(AF_INET, SOCK_DGRAM, 0);
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK || c->head.links >= CONFIG_LINKS)
            continue;
        const struct sockaddr_dl *dl = (const struct sockaddr_dl *)ifa->ifa_addr;
        MimeNetLink *l = &c->links[c->head.links++];
        memset(l, 0, sizeof(*l));
        mime_strlcpy(l->name, ifa->ifa_name, sizeof(l->name));
        l->index = dl->sdl_index;
        if (ifa->ifa_flags & IFF_UP)
            l->flags |= MIME_NET_IF_UP;
        if (ifa->ifa_flags & IFF_RUNNING)
            l->flags |= MIME_NET_IF_RUNNING;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            l->flags |= MIME_NET_IF_LOOPBACK;
        if (ifa->ifa_flags & IFF_BROADCAST)
            l->flags |= MIME_NET_IF_BROADCAST;
        if (ifa->ifa_flags & IFF_MULTICAST)
            l->flags |= MIME_NET_IF_MULTICAST;
        if (ifa->ifa_flags & IFF_POINTOPOINT)
            l->flags |= MIME_NET_IF_POINTTOPOINT;
        if (dl->sdl_alen && dl->sdl_alen <= sizeof(l->mac)) {
            l->mac_length = dl->sdl_alen;
            memcpy(l->mac, LLADDR(dl), dl->sdl_alen);
        }
        const struct if_data *data = ifa->ifa_data;
        if (data) {
            l->mtu = data->ifi_mtu;
            l->speed = data->ifi_baudrate;
        }
        if (ifa->ifa_flags & IFF_LOOPBACK)
            l->type = MIME_NET_LINK_LOOPBACK;
        else if (dl->sdl_type == 0x06) /* IFT_ETHER */
            l->type = MIME_NET_LINK_ETHERNET;
        else if (dl->sdl_type == 0x17) /* IFT_PPP */
            l->type = MIME_NET_LINK_PPP;
        else if (ifa->ifa_flags & IFF_POINTOPOINT)
            l->type = MIME_NET_LINK_TUNNEL;
        else
            l->type = MIME_NET_LINK_OTHER;
        if (probe >= 0 && l->type == MIME_NET_LINK_ETHERNET) {
            struct ifmediareq media;
            memset(&media, 0, sizeof(media));
            mime_strlcpy(media.ifm_name, ifa->ifa_name, sizeof(media.ifm_name));
            if (ioctl(probe, SIOCGIFMEDIA, &media) == 0) {
                if (IFM_TYPE(media.ifm_current) == IFM_IEEE80211)
                    l->type = MIME_NET_LINK_WIRELESS;
            } else {
                l->flags |= MIME_NET_IF_VIRTUAL;
            }
        }
        if (!strncmp(l->name, "bridge", 6) || !strncmp(l->name, "utun", 4) || !strncmp(l->name, "awdl", 4) ||
            !strncmp(l->name, "llw", 3) || !strncmp(l->name, "vmenet", 6))
            l->flags |= MIME_NET_IF_VIRTUAL;
    }
    if (probe >= 0)
        close(probe);
    freeifaddrs(list);
}

static uint8_t mask_bits(const struct sockaddr *mask, size_t full)
{
    if (!mask)
        return 0;
    const uint8_t *b = mask->sa_family == AF_INET6 ? (const uint8_t *)&((const struct sockaddr_in6 *)mask)->sin6_addr
                                                   : (const uint8_t *)&((const struct sockaddr_in *)mask)->sin_addr;
    uint8_t bits = 0;
    for (size_t i = 0; i < full; i++)
        bits += (uint8_t)__builtin_popcount(b[i]);
    return bits;
}

static void read_addresses(Config *c)
{
    struct ifaddrs *list;
    if (getifaddrs(&list) < 0)
        return;
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || c->head.addresses >= CONFIG_ADDRESSES)
            continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;
        MimeNetAddress *a = &c->addresses[c->head.addresses++];
        memset(a, 0, sizeof(*a));
        a->index = if_nametoindex(ifa->ifa_name);
        a->valid = a->preferred = UINT32_MAX;
        if (family == AF_INET) {
            a->prefix = mask_bits(ifa->ifa_netmask, 4);
            set_addr(&a->addr, AF_INET, &((const struct sockaddr_in *)ifa->ifa_addr)->sin_addr, 0);
        } else {
            const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)ifa->ifa_addr;
            struct in6_addr bytes = in6->sin6_addr;
            uint32_t scope = in6->sin6_scope_id;
            /* the kernel's embedded scope of link local addresses. */
            if (link_local6(bytes.s6_addr)) {
                bytes.s6_addr[2] = bytes.s6_addr[3] = 0;
                scope = a->index;
            }
            a->prefix = mask_bits(ifa->ifa_netmask, 16);
            set_addr(&a->addr, AF_INET6, &bytes, scope);
        }
    }
    freeifaddrs(list);
}

#define SA_ROUND(n) ((n) > 0 ? (1 + (((n) - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t))

static void read_routes_family(Config *c, int family)
{
    int mib[6] = {CTL_NET, PF_ROUTE, 0, family, NET_RT_FLAGS, RTF_GATEWAY};
    size_t size = 0;
    if (sysctl(mib, 6, nullptr, &size, nullptr, 0) < 0 || !size)
        return;
    char *buffer = malloc(size);
    if (!buffer)
        return;
    if (sysctl(mib, 6, buffer, &size, nullptr, 0) < 0) {
        free(buffer);
        return;
    }
    for (char *p = buffer; p + sizeof(struct rt_msghdr) <= buffer + size;) {
        const struct rt_msghdr *m = (const struct rt_msghdr *)p;
        if (!m->rtm_msglen)
            break;
        p += m->rtm_msglen;
        if (!(m->rtm_flags & RTF_GATEWAY) || (m->rtm_flags & (RTF_REJECT | RTF_BLACKHOLE)))
            continue;
        const struct sockaddr *sa[RTAX_MAX] = {};
        const char *at = (const char *)(m + 1);
        for (int i = 0; i < RTAX_MAX; i++) {
            if (!(m->rtm_addrs & (1 << i)))
                continue;
            sa[i] = (const struct sockaddr *)at;
            at += SA_ROUND(sa[i]->sa_len);
        }
        const struct sockaddr *dst = sa[RTAX_DST], *gw = sa[RTAX_GATEWAY], *mask = sa[RTAX_NETMASK];
        if (!dst || !gw || dst->sa_family != family || gw->sa_family != family)
            continue;
        size_t full = family == AF_INET ? 4 : 16;
        uint8_t prefix = (uint8_t)(full * 8);
        if (!(m->rtm_flags & RTF_HOST)) {
            /* the mask may be cut after its last nonzero byte, or missing for a default route. */
            size_t offset = family == AF_INET ? 4 : 8;
            size_t have = mask && mask->sa_len > offset ? mask->sa_len - offset : 0;
            prefix = 0;
            for (size_t i = 0; i < have && i < full; i++)
                prefix += (uint8_t)__builtin_popcount(((const uint8_t *)mask)[offset + i]);
        }
        if (family == AF_INET) {
            add_route(c, m->rtm_index, AF_INET, &((const struct sockaddr_in *)dst)->sin_addr, prefix,
                      &((const struct sockaddr_in *)gw)->sin_addr, 0, MIME_NET_ROUTE_STATIC);
        } else {
            struct in6_addr d = ((const struct sockaddr_in6 *)dst)->sin6_addr;
            struct in6_addr g = ((const struct sockaddr_in6 *)gw)->sin6_addr;
            if (link_local6(g.s6_addr))
                g.s6_addr[2] = g.s6_addr[3] = 0;
            add_route(c, m->rtm_index, AF_INET6, &d, prefix, &g, 0, MIME_NET_ROUTE_STATIC);
        }
    }
    free(buffer);
}

static void read_routes(Config *c)
{
    read_routes_family(c, AF_INET);
    read_routes_family(c, AF_INET6);
}

#else
/* ------------------------------------------------------------ Linux. */

static bool sys_read(const char *name, const char *file, char *out, size_t size)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/%s", name, file);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(out, (int)size, f) != nullptr;
    fclose(f);
    return ok;
}

static bool sys_exists(const char *name, const char *file)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/%s", name, file);
    return access(path, F_OK) == 0;
}

/* calls each for every message in a netlink dump. */
static void netlink_dump(Config *c, int type, int family, void (*each)(Config *, const struct nlmsghdr *))
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0)
        return;
    struct {
        struct nlmsghdr header;
        struct rtgenmsg message;
    } request = {};
    request.header.nlmsg_len = sizeof(request);
    request.header.nlmsg_type = (uint16_t)type;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = 1;
    request.message.rtgen_family = (uint8_t)family;
    struct sockaddr_nl kernel = {.nl_family = AF_NETLINK};
    if (sendto(fd, &request, sizeof(request), 0, (struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
        close(fd);
        return;
    }
    static char buffer[65536];
    for (bool done = false; !done;) {
        struct timeval timeout = {.tv_sec = 1};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        for (const struct nlmsghdr *h = (const struct nlmsghdr *)buffer; NLMSG_OK(h, (size_t)n);
             h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR) {
                done = true;
                break;
            }
            each(c, h);
        }
    }
    close(fd);
}

static void each_address(Config *c, const struct nlmsghdr *h)
{
    if (h->nlmsg_type != RTM_NEWADDR || c->head.addresses >= CONFIG_ADDRESSES)
        return;
    const struct ifaddrmsg *m = NLMSG_DATA(h);
    if (m->ifa_family != AF_INET && m->ifa_family != AF_INET6)
        return;
    const void *local = nullptr, *address = nullptr;
    const struct ifa_cacheinfo *cache = nullptr;
    uint32_t flags = m->ifa_flags;
    int length = (int)IFA_PAYLOAD(h);
    for (const struct rtattr *a = IFA_RTA(m); RTA_OK(a, length); a = RTA_NEXT(a, length)) {
        if (a->rta_type == IFA_LOCAL)
            local = RTA_DATA(a);
        else if (a->rta_type == IFA_ADDRESS)
            address = RTA_DATA(a);
        else if (a->rta_type == IFA_CACHEINFO)
            cache = RTA_DATA(a);
        else if (a->rta_type == IFA_FLAGS)
            memcpy(&flags, RTA_DATA(a), 4);
    }
    /* IPv4 local address identifies this host; the other address identifies a point-to-point peer. */
    const void *bytes = local ? local : address;
    if (!bytes)
        return;
    MimeNetAddress *e = &c->addresses[c->head.addresses++];
    memset(e, 0, sizeof(*e));
    e->index = m->ifa_index;
    e->prefix = m->ifa_prefixlen;
    e->valid = cache ? cache->ifa_valid : UINT32_MAX;
    e->preferred = cache ? cache->ifa_prefered : UINT32_MAX;
    if (!(flags & IFA_F_PERMANENT) && e->valid != UINT32_MAX)
        e->flags |= MIME_NET_ADDR_DYNAMIC;
    if (flags & IFA_F_TEMPORARY)
        e->flags |= MIME_NET_ADDR_TEMPORARY;
    if (flags & IFA_F_DEPRECATED)
        e->flags |= MIME_NET_ADDR_DEPRECATED;
    if (flags & (IFA_F_TENTATIVE | IFA_F_DADFAILED))
        e->flags |= MIME_NET_ADDR_TENTATIVE;
    uint32_t scope = m->ifa_family == AF_INET6 && link_local6(bytes) ? m->ifa_index : 0;
    set_addr(&e->addr, m->ifa_family, bytes, scope);
}

static void each_route(Config *c, const struct nlmsghdr *h)
{
    if (h->nlmsg_type != RTM_NEWROUTE)
        return;
    const struct rtmsg *m = NLMSG_DATA(h);
    if ((m->rtm_family != AF_INET && m->rtm_family != AF_INET6) || m->rtm_type != RTN_UNICAST)
        return;
    uint32_t table = m->rtm_table, index = 0, metric = 0;
    const void *destination = nullptr, *gateway = nullptr;
    int length = (int)RTM_PAYLOAD(h);
    for (const struct rtattr *a = RTM_RTA(m); RTA_OK(a, length); a = RTA_NEXT(a, length)) {
        if (a->rta_type == RTA_TABLE)
            memcpy(&table, RTA_DATA(a), 4);
        else if (a->rta_type == RTA_DST)
            destination = RTA_DATA(a);
        else if (a->rta_type == RTA_GATEWAY)
            gateway = RTA_DATA(a);
        else if (a->rta_type == RTA_OIF)
            memcpy(&index, RTA_DATA(a), 4);
        else if (a->rta_type == RTA_PRIORITY)
            memcpy(&metric, RTA_DATA(a), 4);
    }
    if (table != RT_TABLE_MAIN || !gateway || !index)
        return;
    uint8_t origin = m->rtm_protocol == RTPROT_DHCP ? MIME_NET_ROUTE_DHCP
                     : m->rtm_protocol == RTPROT_RA  ? MIME_NET_ROUTE_RA
                                                     : MIME_NET_ROUTE_STATIC;
    add_route(c, index, m->rtm_family, destination, m->rtm_dst_len, gateway, metric, origin);
}

static void each_link(Config *c, const struct nlmsghdr *h)
{
    if (h->nlmsg_type != RTM_NEWLINK || c->head.links >= CONFIG_LINKS)
        return;
    const struct ifinfomsg *m = NLMSG_DATA(h);
    MimeNetLink *l = &c->links[c->head.links];
    memset(l, 0, sizeof(*l));
    int length = (int)IFLA_PAYLOAD(h);
    for (const struct rtattr *a = IFLA_RTA(m); RTA_OK(a, length); a = RTA_NEXT(a, length)) {
        size_t size = RTA_PAYLOAD(a);
        if (a->rta_type == IFLA_IFNAME)
            mime_strlcpy(l->name, RTA_DATA(a), size < sizeof(l->name) ? size : sizeof(l->name));
        else if (a->rta_type == IFLA_MTU && size >= 4)
            memcpy(&l->mtu, RTA_DATA(a), 4);
        else if (a->rta_type == IFLA_ADDRESS && size && size <= sizeof(l->mac) && m->ifi_type != ARPHRD_NONE &&
                 m->ifi_type != ARPHRD_LOOPBACK) {
            l->mac_length = (uint8_t)size;
            memcpy(l->mac, RTA_DATA(a), size);
        }
    }
    if (!l->name[0])
        return;
    c->head.links++;
    l->index = (uint32_t)m->ifi_index;
    if (m->ifi_flags & IFF_UP)
        l->flags |= MIME_NET_IF_UP;
    if (m->ifi_flags & IFF_RUNNING)
        l->flags |= MIME_NET_IF_RUNNING;
    if (m->ifi_flags & IFF_LOOPBACK)
        l->flags |= MIME_NET_IF_LOOPBACK;
    if (m->ifi_flags & IFF_BROADCAST)
        l->flags |= MIME_NET_IF_BROADCAST;
    if (m->ifi_flags & IFF_MULTICAST)
        l->flags |= MIME_NET_IF_MULTICAST;
    if (m->ifi_flags & IFF_POINTOPOINT)
        l->flags |= MIME_NET_IF_POINTTOPOINT;
    switch (m->ifi_type) {
    case ARPHRD_ETHER:
        l->type = sys_exists(l->name, "wireless") || sys_exists(l->name, "phy80211") ? MIME_NET_LINK_WIRELESS
                                                                                     : MIME_NET_LINK_ETHERNET;
        break;
    case ARPHRD_LOOPBACK:
        l->type = MIME_NET_LINK_LOOPBACK;
        break;
    case ARPHRD_PPP:
        l->type = MIME_NET_LINK_PPP;
        break;
    case ARPHRD_NONE:
    case ARPHRD_TUNNEL:
    case ARPHRD_TUNNEL6:
    case ARPHRD_SIT:
    case ARPHRD_IPGRE:
        l->type = MIME_NET_LINK_TUNNEL;
        break;
    default:
        l->type = MIME_NET_LINK_OTHER;
        break;
    }
    /* a device link means hardware, or at least a bus, behind the interface. */
    if (!sys_exists(l->name, "device") && l->type != MIME_NET_LINK_LOOPBACK)
        l->flags |= MIME_NET_IF_VIRTUAL;
    if (!(l->flags & MIME_NET_IF_RUNNING))
        return;
    char text[64];
    if (sys_read(l->name, "speed", text, sizeof(text))) {
        long mbps = strtol(text, nullptr, 10);
        if (mbps > 0)
            l->speed = (uint64_t)mbps * 1000000;
    }
    if (l->type == MIME_NET_LINK_WIRELESS && !l->speed) {
        int probe = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        struct iwreq wrq;
        memset(&wrq, 0, sizeof(wrq));
        mime_strlcpy(wrq.ifr_name, l->name, sizeof(wrq.ifr_name));
        if (probe >= 0 && ioctl(probe, SIOCGIWRATE, &wrq) == 0 && wrq.u.bitrate.value > 0)
            l->speed = (uint64_t)wrq.u.bitrate.value;
        if (probe >= 0)
            close(probe);
    }
}

static void read_links(Config *c)
{
    netlink_dump(c, RTM_GETLINK, AF_UNSPEC, each_link);
}

static void read_addresses(Config *c)
{
    netlink_dump(c, RTM_GETADDR, AF_UNSPEC, each_address);
}

static void read_routes(Config *c)
{
    netlink_dump(c, RTM_GETROUTE, AF_INET, each_route);
    netlink_dump(c, RTM_GETROUTE, AF_INET6, each_route);
}
#endif

/* ------------------------------------------------------------ the hypercall. */

static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
static Config g_config;

uint64_t mime_net_config(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *value)
{
    if (size < sizeof(MimeNetConfig))
        return MIME_HC_STATUS_INVALID;
    uint8_t *out = mime_vm_phys(vm, pa, size);
    if (!out)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_config_lock);
    Config *c = &g_config;
    memset(&c->head, 0, sizeof(c->head));
    read_links(c);
    read_addresses(c);
    read_routes(c);
    read_resolver(c);
    /* offline, the loopback link is all that is left, without a resolver. */
    if (mime_net_offline()) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < c->head.links; i++)
            if (c->links[i].flags & MIME_NET_IF_LOOPBACK)
                c->links[kept++] = c->links[i];
        c->head.links = kept;
        c->head.servers = 0;
        c->head.domain[0] = 0;
        c->head.search[0] = 0;
    }
    /* drop addresses and routes whose links disappeared during the query. */
    uint32_t kept = 0;
    for (uint32_t i = 0; i < c->head.addresses; i++)
        if (find_link(c, c->addresses[i].index))
            c->addresses[kept++] = c->addresses[i];
    c->head.addresses = kept;
    kept = 0;
    for (uint32_t i = 0; i < c->head.routes; i++)
        if (find_link(c, c->routes[i].index))
            c->routes[kept++] = c->routes[i];
    c->head.routes = kept;
    /* as many entries as fit, links first. */
    uint64_t room = size - sizeof(MimeNetConfig);
    uint32_t links = c->head.links, addresses = c->head.addresses, routes = c->head.routes;
    if (links * sizeof(MimeNetLink) > room)
        links = (uint32_t)(room / sizeof(MimeNetLink));
    room -= links * sizeof(MimeNetLink);
    if (addresses * sizeof(MimeNetAddress) > room)
        addresses = (uint32_t)(room / sizeof(MimeNetAddress));
    room -= addresses * sizeof(MimeNetAddress);
    if (routes * sizeof(MimeNetRoute) > room)
        routes = (uint32_t)(room / sizeof(MimeNetRoute));
    MimeNetConfig head = c->head;
    head.links = links;
    head.addresses = addresses;
    head.routes = routes;
    uint8_t *p = out + sizeof(head);
    memcpy(p, c->links, links * sizeof(MimeNetLink));
    p += links * sizeof(MimeNetLink);
    memcpy(p, c->addresses, addresses * sizeof(MimeNetAddress));
    p += addresses * sizeof(MimeNetAddress);
    memcpy(p, c->routes, routes * sizeof(MimeNetRoute));
    p += routes * sizeof(MimeNetRoute);
    head.size = (uint32_t)(p - out);
    memcpy(out, &head, sizeof(head));
    pthread_mutex_unlock(&g_config_lock);
    *value = head.size;
    return MIME_HC_STATUS_OK;
}
