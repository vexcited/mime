/*
 * what the network store tells: the tables of the ndis, ipv4, ipv6, tcp and udp modules, filled from the host's
 * network configuration and the socket driver's sockets, plus the software loopback every windows machine has.
 * interfaces are the host's links, their addresses the host's, routes the ones the stack makes for the addresses and
 * the host's gateways. the values no host can tell are those windows 11 26100 reports on a machine with one ethernet
 * adapter, see docs/nsi.md.
 */
#include "nsi_p.h"

#define IF_TYPE_OTHER 1
#define IF_TYPE_ETHERNET 6
#define IF_TYPE_PPP 23
#define IF_TYPE_LOOPBACK 24
#define IF_TYPE_PROP_VIRTUAL 53
#define IF_TYPE_IEEE80211 71

#define LOOPBACK_INDEX 1
#define LOOPBACK_METRIC 75
#define ROUTE_METRIC 256
#define INFINITE_LIFETIME 0xFFFFFFFFu

#define PROTOCOL_LOCAL 2
#define PROTOCOL_NETMGMT 3
#define PROTOCOL_ICMP 4
#define ORIGIN_MANUAL 0
#define ORIGIN_WELL_KNOWN 1
#define ORIGIN_ROUTER_ADVERTISEMENT 3

#define PREFIX_MANUAL 1
#define PREFIX_WELL_KNOWN 2
#define PREFIX_DHCP 3
#define PREFIX_ROUTER_ADVERTISEMENT 4
#define SUFFIX_MANUAL 1
#define SUFFIX_WELL_KNOWN 2
#define SUFFIX_DHCP 3
#define SUFFIX_LINK_LAYER 4
#define SUFFIX_RANDOM 5
#define DAD_TENTATIVE 1
#define DAD_DEPRECATED 3
#define DAD_PREFERRED 4

/* every interface of a windows machine is on one network until a profile says otherwise; this machine's. */
static const GUID g_network = {0x2D6A9B3B, 0x5B0E, 0x11F0, {0x8E, 0x5B, 0x80, 0x6E, 0x6F, 0x6E, 0x69, 0x63}};
/* on windows the loopback's guid is the network's plus one. */
static const GUID g_loopback = {0x2D6A9B3C, 0x5B0E, 0x11F0, {0x8E, 0x5B, 0x80, 0x6E, 0x6F, 0x6E, 0x69, 0x63}};
static const GUID g_compartment = {0x2D6A9B3D, 0x5B0E, 0x11F0, {0x8E, 0x5B, 0x80, 0x6E, 0x6F, 0x6E, 0x69, 0x63}};

static const UCHAR loopback6[16] = {[15] = 1};
static const UCHAR loopback4[4] = {127, 0, 0, 1};
static const UCHAR zero16[16];

static NSI_NET g_net;
static BOOLEAN g_built;
static ULONG64 g_boot_time;

/* ------------------------------------------------------------------ the network */

static ULONG length_of(ULONG family)
{
    return family == NSI_AF_INET ? 4 : 16;
}

/* the scope level of an address: 2 link, 5 site, 14 global. */
static int address_scope(ULONG family, const UCHAR *bytes)
{
    if (family == NSI_AF_INET)
        return bytes[0] == 127 || (bytes[0] == 169 && bytes[1] == 254) ? 2 : 14;
    if (bytes[0] == 0xFF)
        return bytes[1] & 0xF;
    if (!memcmp(bytes, loopback6, 16) || (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80))
        return 2;
    if (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0xC0)
        return 5;
    return 14;
}

static ULONG zone_of(const NSI_IF *a, ULONG family, const UCHAR *bytes)
{
    if (family != NSI_AF_INET6 || !memcmp(bytes, loopback6, 16))
        return 0;
    return a->Zones[address_scope(family, bytes)];
}

/* windows' automatic interface metric for a link speed. */
static ULONG speed_metric(ULONG64 speed)
{
    static const struct {
        ULONG64 Speed;
        ULONG Metric;
    } table[] = {
        {200000000000ull, 5}, {80000000000ull, 10}, {20000000000ull, 15}, {4000000000ull, 20},
        {500000000ull, 25},   {200000000ull, 30},   {80000000ull, 35},    {20000000ull, 40},
        {4000000ull, 45},     {500000ull, 50},
    };
    if (!speed)
        return 25;
    for (ULONG i = 0; i < RTL_NUMBER_OF(table); i++)
        if (speed >= table[i].Speed)
            return table[i].Metric;
    return 55;
}

/* a guid that is stable for a given host interface name. */
static void name_guid(GUID *guid, const char *kind, const char *name)
{
    ULONG64 h[2] = {0xCBF29CE484222325ull, 0x84222325CBF29CE4ull};
    for (int k = 0; k < 2; k++) {
        for (const char *p = kind; *p; p++)
            h[k] = (h[k] ^ (UCHAR)*p) * 0x100000001B3ull;
        for (const char *p = name; *p; p++)
            h[k] = (h[k] ^ (UCHAR)*p) * (k ? 0x100000001B3ull : 0x1000193ull);
        h[k] ^= h[k] >> 29;
        h[k] *= 0xBF58476D1CE4E5B9ull;
        h[k] ^= h[k] >> 32;
    }
    UCHAR b[16];
    memcpy(b, h, 16);
    b[6] = (UCHAR)((b[6] & 0x0F) | 0x40);
    b[8] = (UCHAR)((b[8] & 0x3F) | 0x80);
    memcpy(guid, b, 16);
}

static void widen(WCHAR *out, ULONG count, const char *s)
{
    ULONG i = 0;
    for (; s[i] && i + 1 < count; i++)
        out[i] = (UCHAR)s[i];
    out[i] = 0;
}

static ULONG64 luid_of(ULONG type, ULONG index)
{
    return ((ULONG64)type << 48) | ((ULONG64)(index & 0xFFFFFF) << 24);
}

static NSI_ADDRESS *new_address(NSI_NET *n, ULONG interface, ULONG family, const UCHAR *bytes, ULONG scope,
                                UCHAR prefix)
{
    NSI_ADDRESS *a = &n->Addresses[n->AddressCount++];
    memset(a, 0, sizeof(*a));
    a->Family = (UCHAR)family;
    memcpy(a->Bytes, bytes, length_of(family));
    a->If = interface;
    a->Scope = scope;
    a->Prefix = prefix;
    a->Valid = a->Preferred = INFINITE_LIFETIME;
    a->Dad = DAD_PREFERRED;
    return a;
}

/* the address origin and state windows shows for a host address. */
static void classify(NSI_ADDRESS *a)
{
    BOOLEAN dynamic = (a->Flags & MIME_AFD_ADDR_DYNAMIC) != 0;
    if (a->Family == NSI_AF_INET) {
        if (a->Bytes[0] == 169 && a->Bytes[1] == 254) {
            a->PrefixOrigin = PREFIX_WELL_KNOWN;
            a->SuffixOrigin = SUFFIX_RANDOM;
        } else {
            a->PrefixOrigin = dynamic ? PREFIX_DHCP : PREFIX_MANUAL;
            a->SuffixOrigin = dynamic ? SUFFIX_DHCP : SUFFIX_MANUAL;
        }
    } else if (a->Bytes[0] == 0xFE && (a->Bytes[1] & 0xC0) == 0x80) {
        a->PrefixOrigin = PREFIX_WELL_KNOWN;
        a->SuffixOrigin = SUFFIX_LINK_LAYER;
    } else if (a->Flags & MIME_AFD_ADDR_TEMPORARY) {
        a->PrefixOrigin = PREFIX_ROUTER_ADVERTISEMENT;
        a->SuffixOrigin = SUFFIX_RANDOM;
    } else if (dynamic && a->Prefix == 128) {
        a->PrefixOrigin = PREFIX_DHCP;
        a->SuffixOrigin = SUFFIX_DHCP;
    } else if (dynamic) {
        a->PrefixOrigin = PREFIX_ROUTER_ADVERTISEMENT;
        a->SuffixOrigin = SUFFIX_LINK_LAYER;
    } else {
        a->PrefixOrigin = PREFIX_MANUAL;
        a->SuffixOrigin = SUFFIX_MANUAL;
    }
    a->Dad = a->Flags & MIME_AFD_ADDR_TENTATIVE    ? DAD_TENTATIVE
             : a->Flags & MIME_AFD_ADDR_DEPRECATED ? DAD_DEPRECATED
                                                   : DAD_PREFERRED;
}

static void add_route(NSI_NET *n, ULONG interface, ULONG family, const UCHAR *destination, UCHAR prefix,
                      const UCHAR *next_hop, ULONG metric, ULONG protocol, ULONG origin, BOOLEAN loopback)
{
    ULONG length = length_of(family);
    UCHAR masked[16] = {};
    memcpy(masked, destination, length);
    for (ULONG i = 0; i < length; i++) {
        int bits = prefix - (int)i * 8;
        masked[i] &= bits >= 8 ? 0xFF : bits <= 0 ? 0 : (UCHAR)(0xFF << (8 - bits));
    }
    if (!next_hop)
        next_hop = zero16;
    for (ULONG i = 0; i < n->RouteCount; i++) {
        NSI_ROUTE *r = &n->Routes[i];
        if (r->Family == family && r->If == interface && r->Prefix == prefix && !memcmp(r->Destination, masked, 16) &&
            !memcmp(r->NextHop, next_hop, length))
            return;
    }
    NSI_ROUTE *r = &n->Routes[n->RouteCount++];
    memset(r, 0, sizeof(*r));
    r->Family = (UCHAR)family;
    memcpy(r->Destination, masked, 16);
    r->Prefix = prefix;
    memcpy(r->NextHop, next_hop, length);
    r->If = interface;
    r->Metric = metric;
    r->Protocol = protocol;
    r->Origin = origin;
    r->Loopback = loopback;
    /* a gateway that was configured or leased stays until it is taken away, the stack's own routes do not. */
    r->Immortal = protocol == PROTOCOL_NETMGMT;
}

static void add_group(NSI_NET *n, ULONG interface, ULONG family, const UCHAR *bytes)
{
    for (ULONG i = 0; i < n->GroupCount; i++) {
        const NSI_GROUP *g = &n->Groups[i];
        if (g->If == interface && g->Family == family && !memcmp(g->Bytes, bytes, length_of(family)))
            return;
    }
    NSI_GROUP *g = &n->Groups[n->GroupCount++];
    memset(g, 0, sizeof(*g));
    g->Family = (UCHAR)family;
    memcpy(g->Bytes, bytes, length_of(family));
    g->If = interface;
}

static void add_loopback(NSI_NET *n)
{
    static const UCHAR multicast6[16] = {0xFF};
    static const UCHAR net127[4] = {127}, bcast127[4] = {127, 255, 255, 255};
    static const UCHAR multicast4[4] = {224}, broadcast[4] = {255, 255, 255, 255};
    static const UCHAR ssdp6[16] = {0xFF, 0x02, [15] = 0x0C}, ssdp4[4] = {239, 255, 255, 250};
    ULONG at = n->IfCount++;
    NSI_IF *a = &n->Ifs[at];
    memset(a, 0, sizeof(*a));
    a->Index = LOOPBACK_INDEX;
    a->Luid = luid_of(IF_TYPE_LOOPBACK, 0);
    a->Guid = g_loopback;
    widen(a->Alias, RTL_NUMBER_OF(a->Alias), "Loopback Pseudo-Interface 1");
    widen(a->Description, RTL_NUMBER_OF(a->Description), "Software Loopback Interface 1");
    strcpy(a->Host, "lo");
    a->Type = IF_TYPE_LOOPBACK;
    a->Loopback = TRUE;
    a->Mtu = 0xFFFFFFFF;
    a->Speed = 1073741824ull;
    a->AdminUp = a->Up = TRUE;
    a->Ipv4 = a->Ipv6 = TRUE;
    a->Metric = LOOPBACK_METRIC;
    for (int i = 0; i < 14; i++)
        a->Zones[i] = LOOPBACK_INDEX;
    a->Zones[15] = LOOPBACK_INDEX;
    NSI_ADDRESS *e = new_address(n, at, NSI_AF_INET6, loopback6, 0, 128);
    e->PrefixOrigin = PREFIX_WELL_KNOWN;
    e->SuffixOrigin = SUFFIX_WELL_KNOWN;
    e = new_address(n, at, NSI_AF_INET, loopback4, 0, 8);
    e->PrefixOrigin = PREFIX_WELL_KNOWN;
    e->SuffixOrigin = SUFFIX_WELL_KNOWN;
    /* the stack's routes have the origin windows gives them: well known for the multicast prefixes, manual, which is
       0, for the others. */
    add_route(n, at, NSI_AF_INET6, loopback6, 128, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
    add_route(n, at, NSI_AF_INET6, multicast6, 8, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_WELL_KNOWN, TRUE);
    add_route(n, at, NSI_AF_INET, net127, 8, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
    add_route(n, at, NSI_AF_INET, loopback4, 32, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
    add_route(n, at, NSI_AF_INET, bcast127, 32, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
    add_route(n, at, NSI_AF_INET, multicast4, 4, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_WELL_KNOWN, TRUE);
    add_route(n, at, NSI_AF_INET, broadcast, 32, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
    add_group(n, at, NSI_AF_INET6, ssdp6);
    add_group(n, at, NSI_AF_INET, ssdp4);
}

static NSI_IF *if_by_index(NSI_NET *n, ULONG index, ULONG *at)
{
    for (ULONG i = 0; i < n->IfCount; i++) {
        if (n->Ifs[i].Index == index) {
            if (at)
                *at = i;
            return &n->Ifs[i];
        }
    }
    return nullptr;
}

/* the addresses of an interface that is up, ipv6 then ipv4, and the on link routes the stack makes for them. */
static void add_addresses(NSI_NET *n, ULONG at, const MIME_AFD_CONFIG *c, const MIME_AFD_LINK_ADDRESS *list)
{
    static const UCHAR multicast6[16] = {0xFF}, link6[16] = {0xFE, 0x80};
    static const UCHAR multicast4[4] = {224}, broadcast[4] = {255, 255, 255, 255};
    NSI_IF *a = &n->Ifs[at];
    BOOLEAN v4 = FALSE, v6 = FALSE;
    for (int pass = 0; pass < 2; pass++) {
        ULONG family = pass ? NSI_AF_INET : NSI_AF_INET6;
        for (ULONG i = 0; i < c->Addresses; i++) {
            const MIME_AFD_LINK_ADDRESS *h = &list[i];
            if (h->Index != a->Index || h->Address.Family != family)
                continue;
            const UCHAR *bytes = family == NSI_AF_INET ? h->Address.Data + 2 : h->Address.Data + 6;
            NSI_ADDRESS *e = new_address(n, at, family, bytes, 0, h->Prefix);
            e->Flags = h->Flags;
            e->Valid = h->Valid;
            e->Preferred = h->Preferred;
            classify(e);
            if (family == NSI_AF_INET) {
                v4 = TRUE;
                if (e->Prefix < 32)
                    add_route(n, at, family, e->Bytes, e->Prefix, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL,
                              FALSE);
                add_route(n, at, family, e->Bytes, 32, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
                if (e->Prefix < 31) {
                    UCHAR b[4];
                    for (int k = 0; k < 4; k++) {
                        int bits = e->Prefix - k * 8;
                        UCHAR mask = bits >= 8 ? 0xFF : bits <= 0 ? 0 : (UCHAR)(0xFF << (8 - bits));
                        b[k] = (UCHAR)(e->Bytes[k] | (UCHAR)~mask);
                    }
                    /* the broadcast address of the subnet is delivered here like the address itself. */
                    add_route(n, at, family, b, 32, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
                }
            } else {
                v6 = TRUE;
                BOOLEAN link = e->Bytes[0] == 0xFE && (e->Bytes[1] & 0xC0) == 0x80;
                if (link && !a->HasLinkLocal) {
                    a->HasLinkLocal = TRUE;
                    memcpy(a->LinkLocal, e->Bytes, 16);
                }
                if (e->Prefix < 128 && !link)
                    add_route(n, at, family, e->Bytes, e->Prefix, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL,
                              FALSE);
                add_route(n, at, family, e->Bytes, 128, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
                UCHAR node[16] = {0xFF, 0x02, [11] = 1, [12] = 0xFF};
                memcpy(node + 13, e->Bytes + 13, 3);
                add_group(n, at, family, node);
            }
        }
    }
    if (v4) {
        add_route(n, at, NSI_AF_INET, multicast4, 4, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_WELL_KNOWN, FALSE);
        add_route(n, at, NSI_AF_INET, broadcast, 32, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, TRUE);
    }
    if (v6) {
        a->Ipv6 = TRUE;
        add_route(n, at, NSI_AF_INET6, link6, 64, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_MANUAL, FALSE);
        add_route(n, at, NSI_AF_INET6, multicast6, 8, nullptr, ROUTE_METRIC, PROTOCOL_LOCAL, ORIGIN_WELL_KNOWN, FALSE);
    }
    /* windows joins all nodes, each address's solicited node group, mdns, llmnr and ssdp on an adapter. */
    if (a->Ipv6) {
        static const UCHAR groups[][16] = {
            {0xFF, 0x01, [15] = 1},    {0xFF, 0x02, [15] = 1},           {0xFF, 0x02, [15] = 0x0C},
            {0xFF, 0x02, [15] = 0xFB}, {0xFF, 0x02, [13] = 1, [15] = 3},
        };
        for (ULONG i = 0; i < RTL_NUMBER_OF(groups); i++)
            add_group(n, at, NSI_AF_INET6, groups[i]);
    }
    static const UCHAR groups[][4] = {{224, 0, 0, 1}, {224, 0, 0, 251}, {224, 0, 0, 252}, {239, 255, 255, 250}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(groups); i++)
        add_group(n, at, NSI_AF_INET, groups[i]);
}

static void add_link(NSI_NET *n, const MIME_AFD_CONFIG *c, const MIME_AFD_LINK *l,
                     const MIME_AFD_LINK_ADDRESS *addresses, ULONG *ethernet, ULONG *wireless)
{
    ULONG at = n->IfCount++;
    NSI_IF *a = &n->Ifs[at];
    memset(a, 0, sizeof(*a));
    a->Index = l->Index;
    memcpy(a->Host, l->Name, sizeof(a->Host) - 1);
    a->Hardware = !(l->Flags & MIME_AFD_IF_VIRTUAL);
    a->AdminUp = (l->Flags & MIME_AFD_IF_UP) != 0;
    a->Up = a->AdminUp && (l->Flags & MIME_AFD_IF_RUNNING);
    a->Mtu = l->Mtu ? l->Mtu : 1500;
    a->Speed = a->Up ? l->Speed : 0;
    a->RxBytes = l->RxBytes;
    a->RxPackets = l->RxPackets;
    a->RxMulticast = l->RxMulticast <= l->RxPackets ? l->RxMulticast : l->RxPackets;
    a->RxErrors = l->RxErrors;
    a->RxDropped = l->RxDropped;
    a->TxBytes = l->TxBytes;
    a->TxPackets = l->TxPackets;
    a->TxErrors = l->TxErrors;
    a->TxDropped = l->TxDropped;
    a->MacLength = l->MacLength <= 8 ? l->MacLength : 8;
    memcpy(a->Mac, l->Mac, a->MacLength);
    const char *kind;
    switch (l->Type) {
    case MIME_AFD_LINK_ETHERNET:
        a->Type = IF_TYPE_ETHERNET;
#if defined(__aarch64__)
        kind = a->Hardware ? "Realtek USB GbE Family Controller" : "Virtual Ethernet Adapter";
#else
        kind = a->Hardware ? "Intel(R) Ethernet Connection I219-V" : "Virtual Ethernet Adapter";
#endif
        if (!a->Hardware) {
            WCHAR host[20];
            widen(host, RTL_NUMBER_OF(host), a->Host);
            _snwprintf(a->Alias, RTL_NUMBER_OF(a->Alias), L"vEthernet (%s)", host);
        } else if (++*ethernet == 1) {
            widen(a->Alias, RTL_NUMBER_OF(a->Alias), "Ethernet");
        } else {
            _snwprintf(a->Alias, RTL_NUMBER_OF(a->Alias), L"Ethernet %u", *ethernet);
        }
        break;
    case MIME_AFD_LINK_WIRELESS:
        a->Type = IF_TYPE_IEEE80211;
#if defined(__aarch64__)
        kind = "Qualcomm(R) FastConnect 7800";
#else
        kind = "Intel(R) Wi-Fi 6E AX211 160MHz";
#endif
        if (++*wireless == 1)
            widen(a->Alias, RTL_NUMBER_OF(a->Alias), "Wi-Fi");
        else
            _snwprintf(a->Alias, RTL_NUMBER_OF(a->Alias), L"Wi-Fi %u", *wireless);
        break;
    case MIME_AFD_LINK_TUNNEL:
        a->Type = IF_TYPE_PROP_VIRTUAL;
        kind = "Tunnel Adapter";
        break;
    case MIME_AFD_LINK_PPP:
        a->Type = IF_TYPE_PPP;
        kind = "PPP Adapter";
        break;
    default:
        a->Type = IF_TYPE_OTHER;
        kind = "Network Adapter";
        break;
    }
    if (!a->Alias[0])
        widen(a->Alias, RTL_NUMBER_OF(a->Alias), a->Host);
    /* windows numbers adapters of the same description: "#2", "#3"... */
    ULONG same = 1;
    WCHAR wide[64];
    widen(wide, RTL_NUMBER_OF(wide), kind);
    for (ULONG k = 0; k < at; k++)
        if (!wcsncmp(n->Ifs[k].Description, wide, wcslen(wide)))
            same++;
    if (same > 1)
        _snwprintf(a->Description, RTL_NUMBER_OF(a->Description), L"%s #%u", wide, same);
    else
        memcpy(a->Description, wide, (wcslen(wide) + 1) * sizeof(WCHAR));
    a->Luid = luid_of(a->Type, 32768 + a->Index);
    name_guid(&a->Guid, "adapter ", a->Host);
    a->Zones[0] = a->Zones[1] = a->Zones[2] = a->Zones[3] = a->Index;
    for (int k = 4; k < 14; k++)
        a->Zones[k] = 1;
    a->Zones[15] = 1;
    a->Metric = speed_metric(a->Speed);
    a->Ipv4 = TRUE;
    /* an interface with no link keeps both stacks bound, one that is up has ipv6 when the host gave it an address. */
    a->Ipv6 = !a->Up;
    if (a->Up)
        add_addresses(n, at, c, addresses);
}

static int compare_ifs(const NSI_IF *a, const NSI_IF *b)
{
    return a->Luid < b->Luid ? -1 : a->Luid > b->Luid;
}

static void release(NSI_NET *n)
{
    if (n->Ifs)
        ExFreePool(n->Ifs);
    if (n->Addresses)
        ExFreePool(n->Addresses);
    if (n->Routes)
        ExFreePool(n->Routes);
    if (n->Groups)
        ExFreePool(n->Groups);
    if (n->Sockets)
        ExFreePool(n->Sockets);
    memset(n, 0, sizeof(*n));
}

static void *zeroed(SIZE_T size)
{
    void *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('N', 's', 'i', 'N'));
    if (p)
        memset(p, 0, size);
    return p;
}

static BOOLEAN build(NSI_NET *n)
{
    MIME_AFD_CONFIG *c = AfdCopyConfig();
    const MIME_AFD_LINK *links = c ? (const MIME_AFD_LINK *)(c + 1) : nullptr;
    ULONG link_count = c ? c->Links : 0, address_count = c ? c->Addresses : 0, route_count = c ? c->Routes : 0;
    /* what did not fit the host's answer is not there. */
    if (c) {
        SIZE_T room = c->Size > sizeof(*c) ? c->Size - sizeof(*c) : 0;
        if (link_count > room / sizeof(MIME_AFD_LINK))
            link_count = (ULONG)(room / sizeof(MIME_AFD_LINK));
        room -= link_count * sizeof(MIME_AFD_LINK);
        if (address_count > room / sizeof(MIME_AFD_LINK_ADDRESS))
            address_count = (ULONG)(room / sizeof(MIME_AFD_LINK_ADDRESS));
        room -= address_count * sizeof(MIME_AFD_LINK_ADDRESS);
        if (route_count > room / sizeof(MIME_AFD_ROUTE))
            route_count = (ULONG)(room / sizeof(MIME_AFD_ROUTE));
        c->Links = link_count;
        c->Addresses = address_count;
        c->Routes = route_count;
    }
    const MIME_AFD_LINK_ADDRESS *addresses = c ? (const MIME_AFD_LINK_ADDRESS *)(links + link_count) : nullptr;
    const MIME_AFD_ROUTE *routes = c ? (const MIME_AFD_ROUTE *)(addresses + address_count) : nullptr;
    memset(n, 0, sizeof(*n));
    n->Ifs = zeroed((link_count + 1) * sizeof(NSI_IF));
    n->Addresses = zeroed((address_count + 2) * sizeof(NSI_ADDRESS));
    /* 3 routes for an ipv4 address, 2 for an ipv6 one, 4 for an interface and the loopback's 7. */
    n->Routes = zeroed((route_count + address_count * 3 + (link_count + 1) * 4 + 8) * sizeof(NSI_ROUTE));
    n->Groups = zeroed((address_count + (link_count + 1) * 10) * sizeof(NSI_GROUP));
    if (!n->Ifs || !n->Addresses || !n->Routes || !n->Groups) {
        release(n);
        if (c)
            ExFreePool(c);
        return FALSE;
    }
    add_loopback(n);
    ULONG ethernet = 0, wireless = 0;
    for (ULONG i = 0; i < link_count; i++) {
        const MIME_AFD_LINK *l = &links[i];
        if ((l->Flags & MIME_AFD_IF_LOOPBACK) || l->Type == MIME_AFD_LINK_LOOPBACK || l->Index <= LOOPBACK_INDEX ||
            if_by_index(n, l->Index, nullptr))
            continue;
        add_link(n, c, l, addresses, &ethernet, &wireless);
    }
    /* the host's gateway routes, for interfaces that are up. */
    for (ULONG i = 0; i < route_count; i++) {
        const MIME_AFD_ROUTE *r = &routes[i];
        ULONG at;
        const NSI_IF *a = if_by_index(n, r->Index, &at);
        if (!a || !a->Up || a->Loopback)
            continue;
        ULONG family = r->Destination.Family;
        if (family != NSI_AF_INET && family != NSI_AF_INET6)
            continue;
        const UCHAR *destination = family == NSI_AF_INET ? r->Destination.Data + 2 : r->Destination.Data + 6;
        const UCHAR *gateway = family == NSI_AF_INET ? r->Gateway.Data + 2 : r->Gateway.Data + 6;
        ULONG protocol = r->Origin == MIME_AFD_ROUTE_RA ? PROTOCOL_ICMP : PROTOCOL_NETMGMT;
        /* windows tells a leased gateway as a manual one, 0, and only a router's advertisement apart. */
        ULONG origin = r->Origin == MIME_AFD_ROUTE_RA ? ORIGIN_ROUTER_ADVERTISEMENT : ORIGIN_MANUAL;
        add_route(n, at, family, destination, r->Prefix, gateway, r->Metric, protocol, origin, FALSE);
    }
    if (c)
        ExFreePool(c);
    /* ndis lists interfaces by luid. positions change, so the references to them follow. */
    for (ULONG i = 1; i < n->IfCount; i++) {
        for (ULONG j = i; j && compare_ifs(&n->Ifs[j - 1], &n->Ifs[j]) > 0; j--) {
            NSI_IF t = n->Ifs[j];
            n->Ifs[j] = n->Ifs[j - 1];
            n->Ifs[j - 1] = t;
            for (ULONG k = 0; k < n->AddressCount; k++)
                n->Addresses[k].If = n->Addresses[k].If == j ? j - 1 : n->Addresses[k].If == j - 1 ? j : n->Addresses[k].If;
            for (ULONG k = 0; k < n->RouteCount; k++)
                n->Routes[k].If = n->Routes[k].If == j ? j - 1 : n->Routes[k].If == j - 1 ? j : n->Routes[k].If;
            for (ULONG k = 0; k < n->GroupCount; k++)
                n->Groups[k].If = n->Groups[k].If == j ? j - 1 : n->Groups[k].If == j - 1 ? j : n->Groups[k].If;
        }
    }
    for (ULONG i = 0; i < n->AddressCount; i++)
        n->Addresses[i].Scope = zone_of(&n->Ifs[n->Addresses[i].If], n->Addresses[i].Family, n->Addresses[i].Bytes);
    n->Taken = KeQueryInterruptTime();
    return TRUE;
}

const NSI_NET *NsiNetCurrent(BOOLEAN fresh, ULONG module)
{
    if (!g_built || fresh || KeQueryInterruptTime() - g_net.Taken > 5000000) {
        NSI_NET next;
        if (build(&next)) {
            if (g_built)
                release(&g_net);
            g_net = next;
            g_built = TRUE;
            NetbtSync(&g_net);
        }
    }
    /* the sockets are read for the modules that tell them, and at each request: a program looks for the socket it
       has just bound. */
    if (g_built && (module == NSI_MODULE_TCP || module == NSI_MODULE_UDP)) {
        if (g_net.Sockets)
            ExFreePool(g_net.Sockets);
        g_net.SocketCount = AfdCopySockets(&g_net.Sockets);
    }
    return g_built ? &g_net : nullptr;
}

/*
 * the connectivity the network list manager publishes, as iphlpapi reads it: bit 3 internet over ipv4 and bit 5 over
 * ipv6, bits 2 and 4 a local network, the second dword the cost with bit 0 unrestricted. a windows machine with a
 * routed ipv4 address and a link local ipv6 one has 0x19 and 1.
 */
ULONG64 NsiNetConnectivity(const NSI_NET *net)
{
    ULONG state = 0;
    for (ULONG i = 0; i < net->RouteCount; i++) {
        const NSI_ROUTE *r = &net->Routes[i];
        if (!r->Prefix && !net->Ifs[r->If].Loopback)
            state |= r->Family == NSI_AF_INET ? 0x08 : 0x20;
    }
    for (ULONG i = 0; i < net->AddressCount; i++) {
        const NSI_ADDRESS *a = &net->Addresses[i];
        if (net->Ifs[a->If].Loopback)
            continue;
        if (a->Family == NSI_AF_INET && !(state & 0x08))
            state |= 0x04;
        if (a->Family == NSI_AF_INET6 && !(state & 0x20))
            state |= 0x10;
    }
    if (!state)
        return 0;
    return (state | 0x01) | (1ull << 32);
}

/* ------------------------------------------------------------------ helpers of the tables */

static void counted(NSI_COUNTED_STRING *out, const WCHAR *text)
{
    ULONG length = (ULONG)wcslen(text);
    if (length > 256)
        length = 256;
    out->Length = (USHORT)(length * sizeof(WCHAR));
    memcpy(out->String, text, length * sizeof(WCHAR));
}

static void physical(NSI_PHYSICAL_ADDRESS *out, const NSI_IF *a)
{
    out->Length = (USHORT)a->MacLength;
    memcpy(out->Address, a->Mac, a->MacLength);
}

static void put32(PUCHAR base, ULONG offset, ULONG value)
{
    memcpy(base + offset, &value, sizeof(value));
}

static void put64(PUCHAR base, ULONG offset, ULONG64 value)
{
    memcpy(base + offset, &value, sizeof(value));
}

static const NSI_IF *if_by_luid(const NSI_NET *net, ULONG64 luid)
{
    for (ULONG i = 0; i < net->IfCount; i++)
        if (net->Ifs[i].Luid == luid)
            return &net->Ifs[i];
    return nullptr;
}

/* ------------------------------------------------------------------ ndis */

static ULONG ndis_count(const NSI_NET *net, ULONG module, ULONG table)
{
    return net->IfCount;
}

static ULONG ndis_access(const NSI_IF *a)
{
    if (a->Loopback)
        return 1;
    return a->Type == IF_TYPE_PPP || a->Type == IF_TYPE_PROP_VIRTUAL ? 3 : 2;
}

static ULONG ndis_medium(const NSI_IF *a)
{
    if (a->Loopback)
        return 17;
    return a->Type == IF_TYPE_PROP_VIRTUAL ? 15 : a->Type == IF_TYPE_PPP ? 3 : 0;
}

static ULONG ndis_physical_medium(const NSI_IF *a)
{
    return a->Type == IF_TYPE_ETHERNET ? 14 : a->Type == IF_TYPE_IEEE80211 ? 9 : 0;
}

/* the mtu of the link: windows' loopback has 1500 here and no limit in the ip interface. */
static ULONG ndis_mtu(const NSI_IF *a)
{
    return a->Loopback ? 1500 : a->Mtu;
}

static void ndis_ifinfo(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw_bytes,
                        PUCHAR dynamic_bytes, PUCHAR fixed_bytes)
{
    const NSI_IF *a = &net->Ifs[i];
    memcpy(key, &a->Luid, 8);
    NSI_NDIS_IFINFO_RW *rw = (NSI_NDIS_IFINFO_RW *)rw_bytes;
    rw->NetworkGuid = g_network;
    rw->AdminStatus = a->AdminUp ? 1 : 2;
    counted(&rw->Alias, a->Alias);
    physical(&rw->PhysicalAddress, a);
    NSI_NDIS_IFINFO_DYNAMIC *d = (NSI_NDIS_IFINFO_DYNAMIC *)dynamic_bytes;
    d->OperStatus = a->Up ? 1 : 2;
    d->Flags = a->Up ? 0 : 2;
    d->MediaConnectState = a->Up ? 1 : 2;
    d->MediaDuplexState = a->Up ? 2 : 0;
    d->Mtu = ndis_mtu(a);
    d->TransmitSpeed = d->ReceiveSpeed = a->Speed;
    d->InErrors = a->RxErrors;
    d->InDiscards = a->RxDropped;
    d->OutErrors = a->TxErrors;
    d->OutDiscards = a->TxDropped;
    d->InOctets = a->RxBytes;
    d->InUcastPkts = a->RxPackets - a->RxMulticast;
    d->InMulticastPkts = a->RxMulticast;
    d->OutOctets = a->TxBytes;
    d->OutUcastPkts = a->TxPackets;
    d->InUcastOctets = a->RxBytes;
    d->OutUcastOctets = a->TxBytes;
    d->CompartmentId = 1;
    NSI_NDIS_IFINFO_STATIC *s = (NSI_NDIS_IFINFO_STATIC *)fixed_bytes;
    s->Index = a->Index;
    counted(&s->Description, a->Description);
    s->Type = a->Type;
    s->AccessType = ndis_access(a);
    s->ConnectionType = 1;
    s->Guid = a->Guid;
    s->ConnectorPresent = a->Hardware;
    physical(&s->PermanentPhysicalAddress, a);
    s->Flags = a->Hardware ? 1 : 0;
    s->MediaType = ndis_medium(a);
    s->PhysicalMediumType = ndis_physical_medium(a);
}

static void ndis_interface(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw_bytes,
                           PUCHAR dynamic_bytes, PUCHAR fixed_bytes)
{
    const NSI_IF *a = &net->Ifs[i];
    memcpy(key, &a->Luid, 8);
    NSI_NDIS_INTERFACE_DYNAMIC *d = (NSI_NDIS_INTERFACE_DYNAMIC *)dynamic_bytes;
    d->CompartmentId = 1;
    d->NetworkGuid = g_network;
    counted(&d->Alias, a->Alias);
    d->OperStatus = a->Up ? 1 : 2;
    d->Mtu = ndis_mtu(a);
    physical(&d->PhysicalAddress, a);
    physical(&d->PermanentPhysicalAddress, a);
    d->MediaConnectState = a->Up ? 1 : 2;
    d->TransmitSpeed = d->ReceiveSpeed = a->Speed;
    d->AdminStatus = a->AdminUp ? 1 : 2;
    d->MediaDuplexState = a->Up ? 2 : 0;
    NSI_NDIS_INTERFACE_STATIC *s = (NSI_NDIS_INTERFACE_STATIC *)fixed_bytes;
    s->Index = a->Index;
    counted(&s->Description, a->Description);
    s->Type = a->Type;
    s->AccessType = ndis_access(a);
    s->ConnectionType = 1;
    s->Guid = a->Guid;
    s->ConnectorPresent = a->Hardware;
    s->Flags = a->Hardware ? 1 : 0;
    s->MediaType = ndis_medium(a);
    s->PhysicalMediumType = ndis_physical_medium(a);
}

/* the luid of an interface index. */
static NTSTATUS ndis_index(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                           PUCHAR fixed)
{
    ULONG index;
    memcpy(&index, key, 4);
    for (ULONG i = 0; i < net->IfCount; i++) {
        if (net->Ifs[i].Index == index) {
            memcpy(fixed, &net->Ifs[i].Luid, 8);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NSI_NO_OBJECT;
}

static ULONG one(const NSI_NET *net, ULONG module, ULONG table)
{
    return 1;
}

static ULONG none(const NSI_NET *net, ULONG module, ULONG table)
{
    return 0;
}

static void no_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw, PUCHAR dynamic,
                      PUCHAR fixed)
{
}

/* the one network of the machine. the bytes after its compartment are the ones windows has for a network with no name. */
static void ndis_network(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw, PUCHAR dynamic,
                         PUCHAR fixed)
{
    memcpy(key, &g_network, 16);
    put32(rw, 4, 1);
    rw[11] = 8;
}

/* the default compartment: its loopback, its network and its guid. */
static void ndis_compartment(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw,
                             PUCHAR dynamic, PUCHAR fixed)
{
    put32(key, 0, 1);
    const NSI_IF *loopback = if_by_luid(net, luid_of(IF_TYPE_LOOPBACK, 0));
    put64(rw, 8, luid_of(IF_TYPE_LOOPBACK, 0));
    memcpy(rw + 16, &g_network, 16);
    memcpy(rw + 32, &g_loopback, 16);
    if (loopback)
        counted((NSI_COUNTED_STRING *)(rw + 48), loopback->Description);
    memcpy(rw + 1620, &g_compartment, 16);
}

/* every thread is in the default compartment, with no scope. */
static NTSTATUS ndis_thread(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                            PUCHAR fixed)
{
    put32(rw, 0, 1);
    return STATUS_SUCCESS;
}

/* a thread may name the only compartment there is, and any scope. */
static NTSTATUS ndis_thread_set(const NSI_NET *net, ULONG module, ULONG table, ULONG action, const UCHAR *key,
                                ULONG key_size, ULONG type, const UCHAR *data, ULONG size, ULONG offset)
{
    if (type != NSI_PARAM_RW || size != 4 || (offset != 0 && offset != 4))
        return STATUS_INVALID_PARAMETER;
    ULONG value;
    memcpy(&value, data, 4);
    return offset == 0 && value != 1 ? STATUS_NSI_NOT_FOUND : STATUS_SUCCESS;
}

static NTSTATUS ndis_session(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                             PUCHAR fixed)
{
    put32(rw, 0, 1);
    return STATUS_SUCCESS;
}

static NTSTATUS ndis_compartment_guid(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw,
                                      PUCHAR dynamic, PUCHAR fixed)
{
    if (memcmp(key, &g_compartment, 16))
        return STATUS_NSI_NO_OBJECT;
    put32(fixed, 0, 1);
    return STATUS_SUCCESS;
}

/* the key is a job handle, and no job has a compartment of its own. */
static NTSTATUS ndis_job(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                         PUCHAR fixed)
{
    return STATUS_INVALID_HANDLE;
}

static const NSI_TABLE g_ndis[NSI_NDIS_TABLES] = {
    [NSI_NDIS_IFINFO] = {8, 1092, 216, 600, NSI_T_COUNTERS, ndis_count, ndis_ifinfo},
    [NSI_NDIS_INTERFACE] = {8, 0, 656, 568, NSI_T_COUNT_MORE, ndis_count, ndis_interface},
    [NSI_NDIS_INDEX] = {4, 0, 0, 8, 0, nullptr, nullptr, ndis_index},
    [3] = {40, 0, 8, 0, 0, none, no_object},
    [4] = {8, 0, 0, 0, 0, none, no_object},
    [5] = {8, 0, 0, 0, 0, none, no_object},
    [NSI_NDIS_NETWORK] = {16, 528, 0, 0, NSI_T_COUNT_MORE, one, ndis_network},
    [NSI_NDIS_COMPARTMENT] = {4, 1640, 0, 0, NSI_T_COUNT_MORE, one, ndis_compartment},
    [NSI_NDIS_THREAD] = {0, 8, 0, 0, 0, nullptr, nullptr, ndis_thread, ndis_thread_set},
    [NSI_NDIS_SESSION] = {4, 4, 0, 0, 0, none, no_object, ndis_session},
    [NSI_NDIS_COMPARTMENT_GUID] = {16, 0, 0, 4, 0, nullptr, nullptr, ndis_compartment_guid},
    [NSI_NDIS_IFINFO_ALL] = {8, 1092, 216, 600, NSI_T_COUNTERS, ndis_count, ndis_ifinfo},
    [NSI_NDIS_INTERFACE_ALL] = {8, 0, 656, 568, NSI_T_COUNT_MORE, ndis_count, ndis_interface},
    [NSI_NDIS_STACK] = {8, 0, 0, 0, NSI_T_COUNT_MORE, none, no_object},
    [NSI_NDIS_JOB] = {8, 4, 0, 0, 0, nullptr, nullptr, ndis_job},
};

/* ------------------------------------------------------------------ ip interfaces */

static ULONG family_of(ULONG module)
{
    return module == NSI_MODULE_IPV4 ? NSI_AF_INET : NSI_AF_INET6;
}

static BOOLEAN bound(const NSI_IF *a, ULONG family)
{
    return family == NSI_AF_INET ? a->Ipv4 : a->Ipv6;
}

/* the i-th interface the family is bound to. */
static const NSI_IF *ip_if(const NSI_NET *net, ULONG family, ULONG i)
{
    for (ULONG k = 0; k < net->IfCount; k++)
        if (bound(&net->Ifs[k], family) && !i--)
            return &net->Ifs[k];
    return nullptr;
}

static ULONG ip_if_count(const NSI_NET *net, ULONG module, ULONG table)
{
    ULONG count = 0;
    for (ULONG k = 0; k < net->IfCount; k++)
        count += bound(&net->Ifs[k], family_of(module));
    return count;
}

/*
 * an ip interface. the fields are those of MIB_IPINTERFACE_ROW, at the offsets iphlpapi reads them; the others hold
 * what windows has on a loopback and on an ethernet interface, by offset.
 */
static void ip_if_fill(const NSI_IF *a, ULONG family, PUCHAR rw, PUCHAR dynamic, PUCHAR fixed)
{
    BOOLEAN v4 = family == NSI_AF_INET;
    BOOLEAN discovery = !a->Loopback && a->Type != IF_TYPE_PROP_VIRTUAL && a->Type != IF_TYPE_PPP;
    rw[5] = !a->Loopback;             /* neighbor unreachability detection */
    rw[6] = 1;                        /* automatic metric */
    rw[10] = rw[11] = v4;             /* managed and other stateful configuration */
    if (a->Loopback)
        put32(rw, 16, 0xFFFFFFFF);
    put32(rw, 20, v4 ? 2 : 1);        /* router discovery: dhcp, enabled */
    put32(rw, 28, a->Metric);
    put32(rw, 32, 30000);             /* base reachable time */
    put32(rw, 36, 1000);              /* retransmit time */
    put32(rw, 40, 600000);            /* path mtu discovery timeout */
    put32(rw, 44, a->Loopback ? 0 : v4 ? 3 : 1); /* duplicate address detection transmits */
    put32(rw, 48, v4 ? 1 : 2);        /* link local address behavior: delayed, always on */
    put32(rw, 52, v4 ? 6500 : 0);     /* link local address timeout */
    memcpy(rw + 56, a->Zones, sizeof(a->Zones));
    put32(rw, 120, a->Mtu);
    put32(rw, 124, 64);               /* site prefix length */
    if (!v4 && a->HasLinkLocal) {
        put32(rw, 132, 64);
        memcpy(rw + 136, a->LinkLocal, 16);
    }
    put32(rw, 156, 1800);
    put32(rw, 160, 1);
    if (!a->Loopback)
        put32(rw, 176, v4 ? 10000002 : 10000001);
    put32(rw, 184, 3);
    if (a->Loopback)
        put32(rw, 208, 0xFFFFFFFF);
    else if (v4)
        put32(rw, 208, 3);
    if (!a->Loopback)
        put32(rw, 212, 1);
    put32(rw, 216, 1000);
    put32(rw, 224, 1);
    if (!v4)
        put32(rw, 228, 0x01010000);
    put32(rw, 232, 1);
    put32(dynamic, 0, a->Index);
    put32(dynamic, 4, 1);
    dynamic[8] = a->Hardware;         /* wake up patterns */
    put32(dynamic, 12, 30000);        /* reachable time */
    put32(dynamic, 16, a->Up);        /* connected */
    if (a->Loopback) {
        dynamic[32] = v4 ? 0x1C : 0x3C; /* the offloads of windows' loopback */
        dynamic[33] = 0x1C;
    }
    fixed[0] = fixed[1] = discovery;  /* neighbor and router discovery */
    fixed[2] = 1;
    put32(fixed, 4, a->Type);
    if (!a->Loopback)
        put64(fixed, 16, a->Luid);
}

static void ip_if_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw, PUCHAR dynamic,
                         PUCHAR fixed)
{
    const NSI_IF *a = ip_if(net, family_of(module), i);
    memcpy(key, &a->Luid, 8);
    if (table == NSI_IP_INTERFACE)
        ip_if_fill(a, family_of(module), rw, dynamic, fixed);
}

/* the sub interface of each interface: its mtu and counters the host does not split by family. */
static void ip_subif_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw,
                            PUCHAR dynamic, PUCHAR fixed)
{
    const NSI_IF *a = ip_if(net, family_of(module), i);
    memcpy(key, &a->Luid, 8);
    memcpy(key + 8, &a->Luid, 8);
    put32(rw, 0, a->Mtu);
    put32(dynamic, 0, 1);
}

/* ------------------------------------------------------------------ ip addresses */

static ULONG address_count(const NSI_NET *net, ULONG module, ULONG table)
{
    ULONG count = 0;
    for (ULONG i = 0; i < net->AddressCount; i++)
        count += net->Addresses[i].Family == family_of(module);
    return count;
}

static void address_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw_bytes,
                           PUCHAR dynamic_bytes, PUCHAR fixed_bytes)
{
    ULONG family = family_of(module);
    const NSI_ADDRESS *a = nullptr;
    for (ULONG k = 0; k < net->AddressCount; k++) {
        if (net->Addresses[k].Family == family && !i--) {
            a = &net->Addresses[k];
            break;
        }
    }
    memcpy(key, &net->Ifs[a->If].Luid, 8);
    memcpy(key + 8, a->Bytes, length_of(family));
    NSI_IP_UNICAST_RW *rw = (NSI_IP_UNICAST_RW *)rw_bytes;
    rw->PreferredLifetime = a->Preferred;
    rw->ValidLifetime = a->Valid;
    rw->PrefixOrigin = a->PrefixOrigin;
    rw->SuffixOrigin = a->SuffixOrigin;
    rw->OnLinkPrefixLength = a->Prefix;
    NSI_IP_UNICAST_DYNAMIC *d = (NSI_IP_UNICAST_DYNAMIC *)dynamic_bytes;
    d->ScopeId = a->Scope;
    d->DadState = a->Dad;
    ((NSI_IP_UNICAST_STATIC *)fixed_bytes)->CreationTime = g_boot_time;
}

static ULONG group_count(const NSI_NET *net, ULONG module, ULONG table)
{
    ULONG count = 0;
    for (ULONG i = 0; i < net->GroupCount; i++)
        count += net->Groups[i].Family == family_of(module);
    return count;
}

static void group_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw, PUCHAR dynamic,
                         PUCHAR fixed)
{
    ULONG family = family_of(module);
    const NSI_GROUP *g = nullptr;
    for (ULONG k = 0; k < net->GroupCount; k++) {
        if (net->Groups[k].Family == family && !i--) {
            g = &net->Groups[k];
            break;
        }
    }
    const NSI_IF *a = &net->Ifs[g->If];
    memcpy(key, &a->Luid, 8);
    memcpy(key + 8, g->Bytes, length_of(family));
    put32(dynamic, 0, zone_of(a, family, g->Bytes));
    put32(dynamic, 8, 1);
    put32(fixed, 0, a->Index);
}

/* ------------------------------------------------------------------ routes */

static ULONG route_count(const NSI_NET *net, ULONG module, ULONG table)
{
    ULONG count = 0;
    for (ULONG i = 0; i < net->RouteCount; i++)
        count += net->Routes[i].Family == family_of(module);
    return count;
}

/* on windows a route of the stack's own has its age since the address came, a gateway since the lease. */
static ULONG route_age(void)
{
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    return (ULONG)(((ULONG64)now.QuadPart - g_boot_time) / 10000000);
}

static void route_fill(const NSI_NET *net, const NSI_ROUTE *r, PUCHAR key, PUCHAR rw_bytes, PUCHAR dynamic,
                       PUCHAR fixed_bytes)
{
    const NSI_IF *a = &net->Ifs[r->If];
    if (r->Family == NSI_AF_INET) {
        NSI_IPV4_FORWARD_KEY *k = (NSI_IPV4_FORWARD_KEY *)key;
        k->CompartmentId = 1;
        memcpy(k->Prefix, r->Destination, 4);
        k->PrefixLength = r->Prefix;
        k->Luid = k->Luid2 = a->Luid;
        memcpy(k->NextHop, r->NextHop, 4);
        if (dynamic) {
            NSI_IPV4_FORWARD_DYNAMIC *d = (NSI_IPV4_FORWARD_DYNAMIC *)dynamic;
            d->Age = route_age();
            memcpy(d->Address, r->NextHop, 4);
        }
    } else {
        NSI_IPV6_FORWARD_KEY *k = (NSI_IPV6_FORWARD_KEY *)key;
        k->CompartmentId = 1;
        memcpy(k->Prefix, r->Destination, 16);
        k->PrefixLength = r->Prefix;
        k->Luid = k->Luid2 = a->Luid;
        memcpy(k->NextHop, r->NextHop, 16);
        if (dynamic) {
            NSI_IPV6_FORWARD_DYNAMIC *d = (NSI_IPV6_FORWARD_DYNAMIC *)dynamic;
            d->Age = route_age();
            memcpy(d->Address, r->NextHop, 16);
        }
    }
    NSI_IP_FORWARD_RW *rw = (NSI_IP_FORWARD_RW *)rw_bytes;
    rw->ValidLifetime = rw->PreferredLifetime = INFINITE_LIFETIME;
    rw->Metric = r->Metric;
    rw->Protocol = r->Protocol;
    rw->Loopback = r->Loopback;
    rw->AutoconfigureAddress = 1;
    rw->Immortal = r->Immortal;
    rw->Unknown[0] = rw->Unknown[1] = 1;
    rw->Unknown[2] = r->Protocol == PROTOCOL_LOCAL;
    NSI_IP_FORWARD_STATIC *s = (NSI_IP_FORWARD_STATIC *)fixed_bytes;
    s->Origin = r->Origin;
    s->Index = a->Index;
}

static void route_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw, PUCHAR dynamic,
                         PUCHAR fixed)
{
    ULONG family = family_of(module);
    for (ULONG k = 0; k < net->RouteCount; k++) {
        if (net->Routes[k].Family == family && !i--) {
            route_fill(net, &net->Routes[k], key, rw, dynamic, fixed);
            return;
        }
    }
}

static BOOLEAN prefix_matches(const UCHAR *a, const UCHAR *b, UCHAR bits)
{
    for (int i = 0; bits; i++) {
        UCHAR mask = bits >= 8 ? 0xFF : (UCHAR)(0xFF << (8 - bits));
        if ((a[i] ^ b[i]) & mask)
            return FALSE;
        bits = bits >= 8 ? bits - 8 : 0;
    }
    return TRUE;
}

static int common_prefix(const UCHAR *a, const UCHAR *b, ULONG length)
{
    int bits = 0;
    for (ULONG i = 0; i < length; i++) {
        UCHAR x = a[i] ^ b[i];
        if (!x) {
            bits += 8;
            continue;
        }
        while (!(x & 0x80)) {
            bits++;
            x <<= 1;
        }
        break;
    }
    return bits;
}

/* a short version of windows' source selection: the destination itself, then same scope, not deprecated, temporary
   for ipv6, and the longest common prefix. */
static const NSI_ADDRESS *pick_source(const NSI_NET *net, ULONG interface, ULONG family, const UCHAR *destination)
{
    const NSI_ADDRESS *best = nullptr;
    int best_score = -1;
    ULONG length = length_of(family);
    int scope = address_scope(family, destination);
    for (ULONG i = 0; i < net->AddressCount; i++) {
        const NSI_ADDRESS *e = &net->Addresses[i];
        if (e->If != interface || e->Family != family || e->Dad == DAD_TENTATIVE)
            continue;
        if (!memcmp(e->Bytes, destination, length))
            return e;
        int score = common_prefix(e->Bytes, destination, length);
        if ((address_scope(family, e->Bytes) <= 2) == (scope <= 2))
            score += 1 << 12;
        if (e->Dad == DAD_PREFERRED)
            score += 1 << 11;
        if (family == NSI_AF_INET6 && (e->Flags & MIME_AFD_ADDR_TEMPORARY))
            score += 1 << 10;
        if (score > best_score) {
            best = e;
            best_score = score;
        }
    }
    return best;
}

/*
 * the route to a destination and the source address the stack would use: the longest matching prefix, then the lowest
 * route and interface metric. the answer is the route's key, the start of its parameters, its age and next hop, its
 * static structure, then the source's zone and address.
 */
static NTSTATUS best_route(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                           PUCHAR fixed)
{
    ULONG family = family_of(module);
    ULONG length = length_of(family);
    const UCHAR *destination = key + 4;
    const UCHAR *source = key + 4 + length + 4;
    ULONG zone;
    ULONG64 luid;
    memcpy(&zone, key + 4 + length, 4);
    memcpy(&luid, key + (family == NSI_AF_INET ? 24 : 48), 8);
    const NSI_IF *only = nullptr;
    if (luid && !(only = if_by_luid(net, luid)))
        return STATUS_NSI_NOT_FOUND;
    /* a link scope destination with a zone goes out that zone's interface. */
    if (family != NSI_AF_INET6 || address_scope(family, destination) > 2)
        zone = 0;
    const NSI_ROUTE *best = nullptr;
    ULONG best_metric = 0;
    for (ULONG i = 0; i < net->RouteCount; i++) {
        const NSI_ROUTE *r = &net->Routes[i];
        if (r->Family != family || !prefix_matches(r->Destination, destination, r->Prefix))
            continue;
        const NSI_IF *a = &net->Ifs[r->If];
        if (!a->Up || (only && a != only) || (zone && a->Index != zone && r->Prefix))
            continue;
        ULONG metric = r->Metric + a->Metric;
        if (!best || r->Prefix > best->Prefix || (r->Prefix == best->Prefix && metric < best_metric)) {
            best = r;
            best_metric = metric;
        }
    }
    if (!best)
        return STATUS_NSI_NOT_FOUND;
    const NSI_ADDRESS *chosen = nullptr;
    if (memcmp(source, zero16, length)) {
        for (ULONG i = 0; i < net->AddressCount && !chosen; i++) {
            const NSI_ADDRESS *e = &net->Addresses[i];
            if (e->If == best->If && e->Family == family && !memcmp(e->Bytes, source, length))
                chosen = e;
        }
        if (!chosen)
            return STATUS_NSI_NOT_FOUND;
    } else {
        chosen = pick_source(net, best->If, family, destination);
    }
    if (!chosen)
        return STATUS_NSI_NOT_FOUND;
    /* a multicast destination has an interface and a source but no route to tell: windows leaves it empty. */
    BOOLEAN multicast = family == NSI_AF_INET ? (destination[0] & 0xF0) == 0xE0 : destination[0] == 0xFF;
    UCHAR route_rw[sizeof(NSI_IP_FORWARD_RW)] = {};
    if (family == NSI_AF_INET) {
        NSI_IPV4_BEST_ROUTE *b = (NSI_IPV4_BEST_ROUTE *)dynamic;
        if (multicast) {
            b->Key.Luid = b->Key.Luid2 = net->Ifs[best->If].Luid;
            b->Static.Index = net->Ifs[best->If].Index;
        } else {
            route_fill(net, best, (PUCHAR)&b->Key, route_rw, nullptr, (PUCHAR)&b->Static);
            memcpy(b->Rw, route_rw, sizeof(b->Rw));
            b->Age = route_age();
            memcpy(b->NextHop, best->NextHop, 4);
        }
        b->SourceScope = chosen->Scope;
        memcpy(b->Source, chosen->Bytes, 4);
    } else {
        NSI_IPV6_BEST_ROUTE *b = (NSI_IPV6_BEST_ROUTE *)dynamic;
        if (multicast) {
            b->Key.Luid = b->Key.Luid2 = net->Ifs[best->If].Luid;
            b->Static.Index = net->Ifs[best->If].Index;
        } else {
            route_fill(net, best, (PUCHAR)&b->Key, route_rw, nullptr, (PUCHAR)&b->Static);
            memcpy(b->Rw, route_rw, sizeof(b->Rw));
            b->Age = route_age();
            memcpy(b->NextHop, best->NextHop, 16);
        }
        b->SourceScope = chosen->Scope;
        memcpy(b->Source, chosen->Bytes, 16);
    }
    return STATUS_SUCCESS;
}

/*
 * a neighbor asked for by address, which windows resolves on the link. the link is the host's and its neighbors out of
 * reach: an address of the interface itself is permanent with the interface's hardware address, any other is as
 * unreachable as after requests nobody answered.
 */
static NTSTATUS resolve_neighbor(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw,
                                 PUCHAR dynamic, PUCHAR fixed)
{
    ULONG family = family_of(module);
    ULONG64 luid;
    memcpy(&luid, key, 8);
    const NSI_IF *a = if_by_luid(net, luid);
    if (!a)
        return STATUS_NSI_NOT_FOUND;
    put32(fixed, 0, a->Index);
    for (ULONG i = 0; i < net->AddressCount; i++) {
        const NSI_ADDRESS *e = &net->Addresses[i];
        if (&net->Ifs[e->If] != a || e->Family != family || memcmp(e->Bytes, key + 16, length_of(family)))
            continue;
        memcpy(rw, a->Mac, a->MacLength);
        put32(dynamic, 0, 6);
        dynamic[10] = (UCHAR)a->MacLength;
        return STATUS_SUCCESS;
    }
    dynamic[9] = 1;
    return STATUS_SUCCESS;
}

/*
 * the neighbors of an interface reset, which FlushIpNetTable asks with the luid alone for a key: there is none to
 * forget, so the cache of an interface that exists is empty before and after. an entry cannot be made, the link is
 * the host's.
 */
static NTSTATUS neighbor_set(const NSI_NET *net, ULONG module, ULONG table, ULONG action, const UCHAR *key,
                             ULONG key_size, ULONG type, const UCHAR *data, ULONG size, ULONG offset)
{
    ULONG64 luid;
    if (key_size < sizeof(luid))
        return STATUS_INVALID_PARAMETER;
    memcpy(&luid, key, sizeof(luid));
    if (!if_by_luid(net, luid))
        return STATUS_NSI_NOT_FOUND;
    return action == 3 || action == 4 || action == 5 ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}

/* the compartment of a request, where 0 stands for the caller's: there is one. */
static NTSTATUS ip_compartment_get(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw,
                                   PUCHAR dynamic, PUCHAR fixed);

/* ------------------------------------------------------------------ ip globals */

/* forwarding is off and the hop limit 128; then the counts of interfaces, addresses and routes. */
static void ip_compartment(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw,
                           PUCHAR dynamic, PUCHAR fixed)
{
    if (key)
        put32(key, 0, 1);
    put32(rw, 0, 1);
    put32(rw, 8, 128);
    put32(dynamic, 0, ip_if_count(net, module, table));
    put32(dynamic, 4, address_count(net, module, table) + group_count(net, module, table));
    put32(dynamic, 12, route_count(net, module, table));
}

static NTSTATUS ip_compartment_get(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw,
                                   PUCHAR dynamic, PUCHAR fixed)
{
    ULONG id;
    memcpy(&id, key, sizeof(id));
    if (id > 1)
        return STATUS_NSI_NOT_FOUND;
    ip_compartment(net, module, table, 0, nullptr, rw, dynamic, fixed);
    return STATUS_SUCCESS;
}

/* the statistics of the family: the host keeps its counters, the reassembly timeout is windows' 60 seconds. */
static NTSTATUS ip_stats(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                         PUCHAR fixed)
{
    if (table == NSI_IP_STATS)
        put32(fixed, 0, 60);
    return STATUS_SUCCESS;
}

#define IP_TABLES(v4, key_address, key_route, best_key, best_size, path_key, path_dynamic)                           \
    [NSI_IP_BEST_ROUTE] = {best_key, 0, best_size, 0, 0, nullptr, nullptr, best_route},                              \
    [NSI_IP_COMPARTMENT] = {4, 16, 16, 0, 0, one, ip_compartment, ip_compartment_get},                               \
    [NSI_IP_ICMP_STATS] = {0, 0, 2064, 0, 0, nullptr, nullptr, ip_stats},                                            \
    [4] = {32, v4 ? 72 : 88, 28, 0, NSI_T_UNSUPPORTED},                                                              \
    [5] = {0, 0, 4, 0, NSI_T_UNSUPPORTED, nullptr, nullptr, ip_stats},                                               \
    [NSI_IP_STATS] = {0, 156, 208, 4, 0, nullptr, nullptr, ip_stats},                                                \
    [NSI_IP_INTERFACE] = {8, 240, 88, 32, NSI_T_SORT, ip_if_count, ip_if_object},                                    \
    [NSI_IP_ANYCAST] = {key_address, 0, 0, 0, NSI_T_SORT, none, no_object},                                          \
    [NSI_IP_MULTICAST] = {key_address, 0, 12, 4, NSI_T_SORT, group_count, group_object},                             \
    [NSI_IP_UNICAST] = {key_address, 28, 8, 8, NSI_T_SORT, address_count, address_object},                           \
    [NSI_IP_NEIGHBOR] = {key_address + 8, 32, 16, 4, NSI_T_SORT, none, no_object, nullptr, neighbor_set},            \
    [NSI_IP_PATH] = {path_key, 8, path_dynamic, 0, NSI_T_SORT, none, no_object},                                     \
    [15] = {key_address, 0, 0, 0, 0, none, no_object},                                                               \
    [NSI_IP_FORWARD] = {key_route, 32, v4 ? 20 : 32, 8, NSI_T_SORT, route_count, route_object},                      \
    [18] = {16, 4, 96, 0, NSI_T_SORT, ip_if_count, ip_subif_object},                                                 \
    [NSI_IP_RESOLVE_NEIGHBOR] = {key_address + 8, 32, 16, 4, NSI_T_UNSUPPORTED, nullptr, nullptr, resolve_neighbor},  \
    [24] = {8, 0, 0, 0, NSI_T_SORT, ip_if_count, ip_if_object}

static const NSI_TABLE g_ipv4[NSI_IP_TABLES] = {IP_TABLES(TRUE, 16, 48, 32, 104, 24, 104)};
static const NSI_TABLE g_ipv6[NSI_IP_TABLES] = {
    IP_TABLES(FALSE, 24, 72, 56, 152, 48, 112),
    [13] = {24, 0, 4, 0, 0, none, no_object},
    [14] = {20, 8, 0, 0, 0, none, no_object},
    [17] = {40, 0, 8, 0, 0, none, no_object},
};

/* ------------------------------------------------------------------ tcp and udp */

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define TCP_STATE_LISTEN 2
#define TCP_STATE_SYN_SENT 3
#define TCP_STATE_ESTABLISHED 5
#define TCP_STATE_FIN_WAIT2 7
#define TCP_STATE_CLOSE_WAIT 8

static ULONG tcp_state(const MIME_AFD_SOCKET_INFO *k)
{
    if (k->Type != SOCK_STREAM || (k->Family != NSI_AF_INET && k->Family != NSI_AF_INET6))
        return 0;
    switch (k->State) {
    case MIME_AFD_STATE_LISTENING:
        return TCP_STATE_LISTEN;
    case MIME_AFD_STATE_CONNECTING:
        return TCP_STATE_SYN_SENT;
    case MIME_AFD_STATE_CONNECTED:
        if (k->Flags & 1)
            return TCP_STATE_CLOSE_WAIT;
        return (k->Flags & 2) ? TCP_STATE_FIN_WAIT2 : TCP_STATE_ESTABLISHED;
    }
    return 0;
}

static BOOLEAN tcp_wanted(const MIME_AFD_SOCKET_INFO *k, ULONG table)
{
    ULONG state = tcp_state(k);
    if (!state)
        return FALSE;
    if (table == NSI_TCP_LISTENERS)
        return state == TCP_STATE_LISTEN;
    if (table == NSI_TCP_CONNECTIONS)
        return state != TCP_STATE_LISTEN;
    return TRUE;
}

static BOOLEAN udp_wanted(const MIME_AFD_SOCKET_INFO *k)
{
    return k->Type == SOCK_DGRAM && (k->Family == NSI_AF_INET || k->Family == NSI_AF_INET6) &&
           (k->Bound || k->State == MIME_AFD_STATE_CONNECTED);
}

static ULONG socket_count(const NSI_NET *net, ULONG module, ULONG table)
{
    ULONG count = 0;
    for (ULONG i = 0; i < net->SocketCount; i++)
        count += module == NSI_MODULE_TCP ? tcp_wanted(&net->Sockets[i], table) : udp_wanted(&net->Sockets[i]);
    return count;
}

/* the host's address is a windows sockaddr; one of no family yet takes the socket's. */
static void endpoint(PUCHAR out, const MIME_AFD_ADDRESS *a, ULONG family)
{
    memcpy(out, a, sizeof(*a));
    if (!a->Family)
        put32(out, 0, family);
}

static void socket_object(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw,
                          PUCHAR dynamic_bytes, PUCHAR fixed_bytes)
{
    const MIME_AFD_SOCKET_INFO *k = nullptr;
    for (ULONG at = 0; at < net->SocketCount; at++) {
        const MIME_AFD_SOCKET_INFO *s = &net->Sockets[at];
        if ((module == NSI_MODULE_TCP ? tcp_wanted(s, table) : udp_wanted(s)) && !i--) {
            k = s;
            break;
        }
    }
    endpoint(key, &k->Local, k->Family);
    /* windows tells the creation of the endpoint; the connection's start is the nearest the socket driver keeps. */
    ULONG64 created = g_boot_time + k->ConnectTime;
    if (module == NSI_MODULE_UDP) {
        NSI_UDP_STATIC *s = (NSI_UDP_STATIC *)fixed_bytes;
        s->ProcessId = k->ProcessId;
        s->CreateTime = created;
        s->Flags = 1;
        return;
    }
    endpoint(key + 28, &k->Remote, k->Family);
    NSI_TCP_DYNAMIC *d = (NSI_TCP_DYNAMIC *)dynamic_bytes;
    d->State = tcp_state(k);
    if (d->State == TCP_STATE_LISTEN) {
        d->Unknown[0] = 1;
        d->Unknown[2] = 6;
    }
    NSI_TCP_STATIC *s = (NSI_TCP_STATIC *)fixed_bytes;
    s->ProcessId = k->ProcessId;
    s->CreateTime = created;
}

/* the retransmission algorithm and its bounds as windows tells them, then the open connections. */
static NTSTATUS tcp_stats(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                          PUCHAR fixed)
{
    USHORT family;
    memcpy(&family, key, 2);
    if (family != NSI_AF_INET && family != NSI_AF_INET6)
        return STATUS_INVALID_PARAMETER;
    if (module == NSI_MODULE_UDP)
        return STATUS_SUCCESS;
    ULONG established = 0;
    for (ULONG i = 0; i < net->SocketCount; i++) {
        const MIME_AFD_SOCKET_INFO *k = &net->Sockets[i];
        established += k->Family == family && tcp_state(k) == TCP_STATE_ESTABLISHED;
    }
    put32(dynamic, 16, established);
    put32(fixed, 0, 4);
    put32(fixed, 4, 5);
    put32(fixed, 8, 0xFFFFFFFF);
    put32(fixed, 12, 0xFFFFFFFF);
    return STATUS_SUCCESS;
}

static const NSI_TABLE g_tcp[NSI_TCP_TABLES] = {
    [NSI_TCP_STATS] = {2, 0, 104, 20, 0, nullptr, nullptr, tcp_stats},
    [NSI_TCP_ALL] = {56, 0, 24, 32, NSI_T_COUNTERS, socket_count, socket_object},
    [NSI_TCP_CONNECTIONS] = {56, 0, 24, 32, NSI_T_COUNTERS, socket_count, socket_object},
    [NSI_TCP_LISTENERS] = {56, 0, 24, 32, NSI_T_COUNTERS, socket_count, socket_object},
    [NSI_TCP_BOUND] = {56, 0, 24, 32, NSI_T_COUNTERS, socket_count, socket_object},
};

static const NSI_TABLE g_udp[NSI_UDP_TABLES] = {
    [NSI_UDP_STATS] = {2, 2, 48, 0, 0, none, no_object, tcp_stats},
    [NSI_UDP_ENDPOINT] = {28, 0, 32, 32, NSI_T_COUNTERS, socket_count, socket_object},
};

/* ------------------------------------------------------------------ the modules */

const NSI_TABLE *NsiNetTable(ULONG module, ULONG table, ULONG *tables)
{
    const NSI_TABLE *list;
    switch (module) {
    case NSI_MODULE_NDIS:
        list = g_ndis;
        *tables = RTL_NUMBER_OF(g_ndis);
        break;
    case NSI_MODULE_IPV4:
        list = g_ipv4;
        *tables = RTL_NUMBER_OF(g_ipv4);
        break;
    case NSI_MODULE_IPV6:
        list = g_ipv6;
        *tables = RTL_NUMBER_OF(g_ipv6);
        break;
    case NSI_MODULE_TCP:
        list = g_tcp;
        *tables = RTL_NUMBER_OF(g_tcp);
        break;
    case NSI_MODULE_UDP:
        list = g_udp;
        *tables = RTL_NUMBER_OF(g_udp);
        break;
    default:
        *tables = 0;
        return nullptr;
    }
    return table < *tables ? &list[table] : nullptr;
}

void NsiNetInitialize(void)
{
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    g_boot_time = (ULONG64)now.QuadPart;
}
