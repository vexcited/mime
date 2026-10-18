/*
 * iphlpapi: the ip helper. the machine's adapters are the host's network
 * interfaces, read from the socket driver, beside the software loopback
 * windows always has. every call works on a snapshot of them, taken at
 * most every half second.
 */
#include "private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ the socket driver */

static SRWLOCK device_lock = SRWLOCK_INIT;
static HANDLE device;

HANDLE IphDevice(void)
{
    AcquireSRWLockExclusive(&device_lock);
    if (!device) {
        UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_AFD_DEVICE);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
        IO_STATUS_BLOCK io;
        HANDLE h;
        if (NT_SUCCESS(NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, nullptr, 0)))
            device = h;
    }
    HANDLE h = device;
    ReleaseSRWLockExclusive(&device_lock);
    return h;
}

NTSTATUS IphDeviceControl(ULONG code, void *output, ULONG out_len, ULONG_PTR *information)
{
    HANDLE h = IphDevice();
    if (!h)
        return STATUS_UNSUCCESSFUL;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event)
        return STATUS_INSUFFICIENT_RESOURCES;
    IO_STATUS_BLOCK io;
    io.Status = STATUS_PENDING;
    io.Information = 0;
    NTSTATUS status = NtDeviceIoControlFile(h, event, nullptr, nullptr, &io, code, nullptr, 0, output, out_len);
    if (status == STATUS_PENDING) {
        WaitForSingleObject(event, INFINITE);
        status = io.Status;
    }
    CloseHandle(event);
    *information = io.Information;
    return status;
}

/* ------------------------------------------------------------------ helpers */

BOOL IphValidFamily(ADDRESS_FAMILY family)
{
    return family == AF_UNSPEC || family == AF_INET || family == AF_INET6;
}

BOOL IphFamilyWanted(ULONG family, ADDRESS_FAMILY f)
{
    return family == AF_UNSPEC || family == f;
}

SIZE_T IphSockaddrSize(ADDRESS_FAMILY family)
{
    return family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6);
}

void IphSockaddr(SOCKADDR_INET *sa, ADDRESS_FAMILY family, const BYTE *bytes, ULONG scope)
{
    memset(sa, 0, sizeof(*sa));
    if (family == AF_INET) {
        sa->Ipv4.sin_family = AF_INET;
        memcpy(&sa->Ipv4.sin_addr, bytes, 4);
    } else {
        sa->Ipv6.sin6_family = AF_INET6;
        memcpy(&sa->Ipv6.sin6_addr, bytes, 16);
        sa->Ipv6.sin6_scope_id = scope;
    }
}

/* the scope level of an address: 1 interface, 2 link, 5 site, 14 global. */
int IphAddressScope(ADDRESS_FAMILY family, const BYTE *bytes)
{
    if (family == AF_INET) {
        if (bytes[0] == 127 || (bytes[0] == 169 && bytes[1] == 254))
            return 2;
        return 14;
    }
    if (bytes[0] == 0xFF)
        return bytes[1] & 0xF;
    static const BYTE loopback[16] = {[15] = 1};
    if (!memcmp(bytes, loopback, 16) || (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80))
        return 2;
    if (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0xC0)
        return 5;
    return 14;
}

ULONG IphZone(const IPH_ADAPTER *a, ADDRESS_FAMILY family, const BYTE *bytes)
{
    static const BYTE loopback[16] = {[15] = 1};
    if (family != AF_INET6 || !memcmp(bytes, loopback, 16))
        return 0;
    return a->zones[IphAddressScope(family, bytes)];
}

void IphIpv4String(char *out, const BYTE *b)
{
    _snprintf(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

ULONG IphIpv4Mask(UINT8 prefix)
{
    ULONG host = prefix >= 32 ? 0xFFFFFFFFu : prefix ? ~((1u << (32 - prefix)) - 1) : 0;
    return ((host >> 24) & 0xFF) | ((host >> 8) & 0xFF00) | ((host << 8) & 0xFF0000) | (host << 24);
}

ULONG IphTableSize(ULONG rows, ULONG row, BOOL grow)
{
    if (grow)
        rows += rows / 8 < 10 ? 10 : rows / 8;
    return rows * row + 12;
}

DWORD IphCheckTable(ULONG rows, ULONG row, const void *table, PULONG size)
{
    if (!table || *size < IphTableSize(rows, row, FALSE)) {
        *size = IphTableSize(rows, row, TRUE);
        return ERROR_INSUFFICIENT_BUFFER;
    }
    return NO_ERROR;
}

const IPH_ADAPTER *IphAdapterByIndex(const IPH_SNAPSHOT *s, ULONG index)
{
    for (ULONG i = 0; i < s->adapter_count; i++)
        if (s->adapters[i].index == index)
            return &s->adapters[i];
    return nullptr;
}

const IPH_ADAPTER *IphAdapterByLuid(const IPH_SNAPSHOT *s, const NET_LUID *luid)
{
    for (ULONG i = 0; i < s->adapter_count; i++)
        if (s->adapters[i].luid.Value == luid->Value)
            return &s->adapters[i];
    return nullptr;
}

BOOL IphFamilyEnabled(const IPH_ADAPTER *a, ADDRESS_FAMILY family)
{
    return family == AF_INET ? a->ipv4 : a->ipv6;
}

/* windows' automatic interface metric for a link speed. */
static ULONG speed_metric(ULONG64 speed)
{
    static const struct {
        ULONG64 speed;
        ULONG metric;
    } table[] = {
        {200000000000ull, 5}, {80000000000ull, 10}, {20000000000ull, 15}, {4000000000ull, 20},
        {500000000ull, 25},   {200000000ull, 30},   {80000000ull, 35},    {20000000ull, 40},
        {4000000ull, 45},     {500000ull, 50},
    };
    if (!speed)
        return 25;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(table); i++)
        if (speed >= table[i].speed)
            return table[i].metric;
    return 55;
}

/* a guid that stays the same for a host interface name. */
static void name_guid(GUID *guid, const char *kind, const char *name)
{
    ULONG64 h[2] = {0xCBF29CE484222325ull, 0x84222325CBF29CE4ull};
    for (int k = 0; k < 2; k++) {
        for (const char *p = kind; *p; p++)
            h[k] = (h[k] ^ (BYTE)*p) * 0x100000001B3ull;
        for (const char *p = name; *p; p++)
            h[k] = (h[k] ^ (BYTE)*p) * (k ? 0x100000001B3ull : 0x1000193ull);
        h[k] ^= h[k] >> 29;
        h[k] *= 0xBF58476D1CE4E5B9ull;
        h[k] ^= h[k] >> 32;
    }
    BYTE b[16];
    memcpy(b, h, 16);
    b[6] = (BYTE)((b[6] & 0x0F) | 0x40);
    b[8] = (BYTE)((b[8] & 0x3F) | 0x80);
    memcpy(guid, b, 16);
}

static void guid_string(char *out, const GUID *g)
{
    _snprintf(out, 39, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2,
              g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
              g->Data4[7]);
}

static void widen(WCHAR *out, SIZE_T count, const char *s)
{
    SIZE_T i = 0;
    for (; s[i] && i + 1 < count; i++)
        out[i] = (BYTE)s[i];
    out[i] = 0;
}

static void append_number(WCHAR *out, SIZE_T count, const WCHAR *prefix, ULONG n)
{
    SIZE_T i = 0;
    for (; prefix[i] && i + 1 < count; i++)
        out[i] = prefix[i];
    if (n > 1 && i + 4 < count) {
        WCHAR digits[12];
        int d = 0;
        for (ULONG v = n; v; v /= 10)
            digits[d++] = (WCHAR)(L'0' + v % 10);
        while (d && i + 1 < count)
            out[i++] = digits[--d];
    }
    out[i] = 0;
}

/* ------------------------------------------------------------------ the snapshot */

static const BYTE loopback6[16] = {[15] = 1};
static const BYTE loopback4[4] = {127, 0, 0, 1};

static ULONG64 now_filetime(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONG64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static LARGE_INTEGER module_time;

static void add_loopback(IPH_SNAPSHOT *s)
{
    IPH_ADAPTER *a = &s->adapters[s->adapter_count++];
    a->index = LOOPBACK_INDEX;
    a->luid.Info.IfType = IF_TYPE_SOFTWARE_LOOPBACK;
    /* every windows machine has its own loopback guid; this one is mime's. */
    static const GUID guid = {0x2D6A9B3C, 0x5B0E, 0x11F0, {0x8E, 0x5B, 0x80, 0x6E, 0x6F, 0x6E, 0x69, 0x63}};
    a->guid = guid;
    guid_string(a->name, &a->guid);
    widen(a->alias, RTL_NUMBER_OF(a->alias), "Loopback Pseudo-Interface 1");
    widen(a->description, RTL_NUMBER_OF(a->description), "Software Loopback Interface 1");
    strcpy(a->host, "lo");
    a->type = IF_TYPE_SOFTWARE_LOOPBACK;
    a->mtu = 0xFFFFFFFF;
    a->speed = 1073741824ull;
    a->admin_up = a->up = TRUE;
    a->ipv4 = a->ipv6 = TRUE;
    a->metric = LOOPBACK_METRIC;
    for (int i = 1; i < 14; i++)
        a->zones[i] = LOOPBACK_INDEX;
}

static IPH_ADDRESS *new_address(IPH_SNAPSHOT *s, ADDRESS_FAMILY family, const BYTE *bytes, ULONG scope, UINT8 prefix)
{
    IPH_ADDRESS *a = &s->addresses[s->address_count++];
    memset(a, 0, sizeof(*a));
    a->family = family;
    memcpy(a->bytes, bytes, family == AF_INET ? 4 : 16);
    a->scope = scope;
    a->prefix = prefix;
    a->valid = a->preferred = INFINITE_LIFETIME;
    a->dad = IpDadStatePreferred;
    return a;
}

static void add_route(IPH_SNAPSHOT *s, ULONG index, ADDRESS_FAMILY family, const BYTE *destination, UINT8 prefix,
                      const BYTE *next_hop, ULONG next_scope, ULONG metric, NL_ROUTE_PROTOCOL protocol,
                      NL_ROUTE_ORIGIN origin, BOOLEAN loopback)
{
    SIZE_T length = family == AF_INET ? 4 : 16;
    BYTE masked[16] = {};
    memcpy(masked, destination, length);
    for (SIZE_T i = 0; i < length; i++) {
        int bits = prefix - (int)i * 8;
        masked[i] &= bits >= 8 ? 0xFF : bits <= 0 ? 0 : (BYTE)(0xFF << (8 - bits));
    }
    static const BYTE zero[16];
    if (!next_hop)
        next_hop = zero;
    for (ULONG i = 0; i < s->route_count; i++) {
        IPH_ROUTE *r = &s->routes[i];
        if (r->family == family && r->index == index && r->prefix == prefix && !memcmp(r->destination, masked, 16) &&
            !memcmp(r->next_hop, next_hop, length))
            return;
    }
    IPH_ROUTE *r = &s->routes[s->route_count++];
    memset(r, 0, sizeof(*r));
    r->family = family;
    memcpy(r->destination, masked, 16);
    r->prefix = prefix;
    memcpy(r->next_hop, next_hop, length);
    r->next_scope = next_scope;
    r->index = index;
    r->metric = metric;
    r->protocol = protocol;
    r->origin = origin;
    r->loopback = loopback;
    r->immortal = protocol == MIB_IPPROTO_LOCAL || origin == NlroManual;
}

static void add_multicast(IPH_SNAPSHOT *s, const IPH_ADAPTER *a, ADDRESS_FAMILY family, const BYTE *bytes)
{
    for (ULONG i = 0; i < s->multicast_count; i++) {
        const IPH_ADDRESS *m = &s->multicasts[i];
        if (m->index == a->index && m->family == family && !memcmp(m->bytes, bytes, family == AF_INET ? 4 : 16))
            return;
    }
    IPH_ADDRESS *m = &s->multicasts[s->multicast_count++];
    memset(m, 0, sizeof(*m));
    m->family = family;
    memcpy(m->bytes, bytes, family == AF_INET ? 4 : 16);
    m->index = a->index;
    m->scope = IphZone(a, family, bytes);
}

/* the address origins and states windows would show for a host address. */
static void classify(IPH_ADDRESS *a)
{
    BOOL dynamic = a->flags & MIME_AFD_ADDR_DYNAMIC;
    if (a->family == AF_INET) {
        if (a->bytes[0] == 169 && a->bytes[1] == 254) {
            a->prefix_origin = IpPrefixOriginWellKnown;
            a->suffix_origin = IpSuffixOriginRandom;
        } else {
            a->prefix_origin = dynamic ? IpPrefixOriginDhcp : IpPrefixOriginManual;
            a->suffix_origin = dynamic ? IpSuffixOriginDhcp : IpSuffixOriginManual;
        }
    } else if (a->bytes[0] == 0xFE && (a->bytes[1] & 0xC0) == 0x80) {
        a->prefix_origin = IpPrefixOriginWellKnown;
        a->suffix_origin = IpSuffixOriginLinkLayerAddress;
    } else if (a->flags & MIME_AFD_ADDR_TEMPORARY) {
        a->prefix_origin = IpPrefixOriginRouterAdvertisement;
        a->suffix_origin = IpSuffixOriginRandom;
    } else if (dynamic && a->prefix == 128) {
        a->prefix_origin = IpPrefixOriginDhcp;
        a->suffix_origin = IpSuffixOriginDhcp;
    } else if (dynamic) {
        a->prefix_origin = IpPrefixOriginRouterAdvertisement;
        a->suffix_origin = IpSuffixOriginLinkLayerAddress;
    } else {
        a->prefix_origin = IpPrefixOriginManual;
        a->suffix_origin = IpSuffixOriginManual;
    }
    a->dad = a->flags & MIME_AFD_ADDR_TENTATIVE    ? IpDadStateTentative
             : a->flags & MIME_AFD_ADDR_DEPRECATED ? IpDadStateDeprecated
                                                   : IpDadStatePreferred;
    a->eligible = a->dad == IpDadStatePreferred && a->suffix_origin != IpSuffixOriginRandom &&
                  IphAddressScope(a->family, a->bytes) > 2;
}

static IPH_SNAPSHOT *allocate(ULONG links, ULONG addresses, ULONG routes)
{
    IPH_SNAPSHOT *s = IphAlloc(sizeof(*s));
    if (!s)
        return nullptr;
    s->refs = 1;
    s->adapters = IphAlloc((links + 1) * sizeof(IPH_ADAPTER));
    s->addresses = IphAlloc((addresses + 2) * sizeof(IPH_ADDRESS));
    /* each ipv4 address makes 3 routes and each ipv6 one 2, each adapter 4 more. */
    s->routes = IphAlloc((routes + addresses * 3 + (links + 1) * 4 + 8) * sizeof(IPH_ROUTE));
    s->multicasts = IphAlloc((addresses + (links + 1) * 10) * sizeof(IPH_ADDRESS));
    if (!s->adapters || !s->addresses || !s->routes || !s->multicasts) {
        IphRelease(s);
        return nullptr;
    }
    return s;
}

static const MIME_AFD_CONFIG *read_config(void **block)
{
    *block = nullptr;
    for (ULONG size = 65536; size <= 1u << 20; size *= 2) {
        MIME_AFD_CONFIG *c = IphAlloc(size);
        if (!c)
            return nullptr;
        ULONG_PTR got = 0;
        NTSTATUS status = IphDeviceControl(IOCTL_AFD_CONFIG, c, size, &got);
        if (NT_SUCCESS(status) && got >= sizeof(*c) && c->Size <= got) {
            *block = c;
            return c;
        }
        IphFree(c);
        if (status != (NTSTATUS)0x80000005L)
            return nullptr;
    }
    return nullptr;
}

static int compare_routes(const IPH_ROUTE *a, const IPH_ROUTE *b)
{
    if (a->family != b->family)
        return a->family == AF_INET ? -1 : 1;
    int c = memcmp(a->destination, b->destination, 16);
    if (c)
        return c;
    if (a->prefix != b->prefix)
        return a->prefix < b->prefix ? -1 : 1;
    if (a->index != b->index)
        return a->index < b->index ? -1 : 1;
    return memcmp(a->next_hop, b->next_hop, 16);
}

static void sort_routes(IPH_SNAPSHOT *s)
{
    for (ULONG i = 1; i < s->route_count; i++) {
        IPH_ROUTE r = s->routes[i];
        ULONG j = i;
        for (; j && compare_routes(&s->routes[j - 1], &r) > 0; j--)
            s->routes[j] = s->routes[j - 1];
        s->routes[j] = r;
    }
}

static void add_adapter_addresses(IPH_SNAPSHOT *s, IPH_ADAPTER *a, const MIME_AFD_CONFIG *c,
                                  const MIME_AFD_LINK_ADDRESS *list)
{
    a->first_address = s->address_count;
    /* ipv6 then ipv4, the addresses registered in dns first in each. */
    for (int pass = 0; pass < 4; pass++) {
        ADDRESS_FAMILY family = pass < 2 ? AF_INET6 : AF_INET;
        for (ULONG i = 0; c && i < c->Addresses; i++) {
            const MIME_AFD_LINK_ADDRESS *h = &list[i];
            if (h->Index != a->index || h->Address.Family != family)
                continue;
            const BYTE *bytes = family == AF_INET ? h->Address.Data + 2 : h->Address.Data + 6;
            IPH_ADDRESS probe = {};
            probe.family = family;
            memcpy(probe.bytes, bytes, family == AF_INET ? 4 : 16);
            probe.flags = h->Flags;
            probe.prefix = h->Prefix;
            classify(&probe);
            if (probe.eligible != (pass % 2 == 0))
                continue;
            ULONG scope = 0;
            if (family == AF_INET6)
                memcpy(&scope, h->Address.Data + 22, 4);
            IPH_ADDRESS *e = new_address(s, family, bytes, scope, h->Prefix);
            e->index = a->index;
            e->flags = h->Flags;
            e->valid = h->Valid;
            e->preferred = h->Preferred;
            classify(e);
        }
    }
    a->address_count = s->address_count - a->first_address;
}

static void loopback_routes(IPH_SNAPSHOT *s, const IPH_ADAPTER *a)
{
    static const BYTE multicast6[16] = {0xFF};
    static const BYTE net127[4] = {127}, bcast127[4] = {127, 255, 255, 255};
    static const BYTE multicast4[4] = {224}, broadcast[4] = {255, 255, 255, 255};
    add_route(s, a->index, AF_INET6, loopback6, 128, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
    add_route(s, a->index, AF_INET6, multicast6, 8, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
    add_route(s, a->index, AF_INET, net127, 8, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
    add_route(s, a->index, AF_INET, loopback4, 32, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
    add_route(s, a->index, AF_INET, bcast127, 32, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
    add_route(s, a->index, AF_INET, multicast4, 4, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
    add_route(s, a->index, AF_INET, broadcast, 32, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, TRUE);
}

/* the on link routes the stack makes for an adapter's addresses. */
static void adapter_routes(IPH_SNAPSHOT *s, const IPH_ADAPTER *a)
{
    static const BYTE multicast6[16] = {0xFF}, link6[16] = {0xFE, 0x80};
    static const BYTE multicast4[4] = {224}, broadcast[4] = {255, 255, 255, 255};
    BOOL v4 = FALSE, v6 = FALSE;
    for (ULONG i = 0; i < a->address_count; i++) {
        const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
        if (e->family == AF_INET) {
            v4 = TRUE;
            if (e->prefix < 32)
                add_route(s, a->index, AF_INET, e->bytes, e->prefix, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL,
                          NlroWellKnown, FALSE);
            add_route(s, a->index, AF_INET, e->bytes, 32, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown,
                      TRUE);
            if (e->prefix < 31) {
                BYTE b[4];
                ULONG mask = IphIpv4Mask(e->prefix);
                for (int k = 0; k < 4; k++)
                    b[k] = (BYTE)(e->bytes[k] | ~((const BYTE *)&mask)[k]);
                add_route(s, a->index, AF_INET, b, 32, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown,
                          FALSE);
            }
        } else {
            v6 = TRUE;
            if (e->prefix < 128 && !(e->bytes[0] == 0xFE && (e->bytes[1] & 0xC0) == 0x80))
                add_route(s, a->index, AF_INET6, e->bytes, e->prefix, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL,
                          NlroWellKnown, FALSE);
            add_route(s, a->index, AF_INET6, e->bytes, 128, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL,
                      NlroWellKnown, TRUE);
        }
    }
    if (v4) {
        add_route(s, a->index, AF_INET, multicast4, 4, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown,
                  FALSE);
        add_route(s, a->index, AF_INET, broadcast, 32, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown,
                  FALSE);
    }
    if (v6) {
        add_route(s, a->index, AF_INET6, link6, 64, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown, FALSE);
        add_route(s, a->index, AF_INET6, multicast6, 8, nullptr, 0, ROUTE_METRIC, MIB_IPPROTO_LOCAL, NlroWellKnown,
                  FALSE);
    }
}

/* the groups windows joins on an adapter: all nodes, the solicited node of each address, mdns, llmnr and ssdp. */
static void adapter_multicasts(IPH_SNAPSHOT *s, const IPH_ADAPTER *a)
{
    if (a->type == IF_TYPE_SOFTWARE_LOOPBACK) {
        static const BYTE ssdp6[16] = {0xFF, 0x02, [15] = 0x0C}, ssdp4[4] = {239, 255, 255, 250};
        add_multicast(s, a, AF_INET6, ssdp6);
        add_multicast(s, a, AF_INET, ssdp4);
        return;
    }
    if (!a->up)
        return;
    if (a->ipv6) {
        static const BYTE groups[][16] = {
            {0xFF, 0x01, [15] = 1},
            {0xFF, 0x02, [15] = 1},
            {0xFF, 0x02, [15] = 0x0C},
            {0xFF, 0x02, [15] = 0xFB},
            {0xFF, 0x02, [13] = 1, [15] = 3},
        };
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(groups); i++)
            add_multicast(s, a, AF_INET6, groups[i]);
        for (ULONG i = 0; i < a->address_count; i++) {
            const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
            if (e->family != AF_INET6)
                continue;
            BYTE node[16] = {0xFF, 0x02, [11] = 1, [12] = 0xFF};
            memcpy(node + 13, e->bytes + 13, 3);
            add_multicast(s, a, AF_INET6, node);
        }
    }
    if (a->ipv4) {
        static const BYTE groups[][4] = {{224, 0, 0, 1}, {224, 0, 0, 251}, {224, 0, 0, 252}, {239, 255, 255, 250}};
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(groups); i++)
            add_multicast(s, a, AF_INET, groups[i]);
    }
}

static IPH_SNAPSHOT *build(void)
{
    void *block;
    const MIME_AFD_CONFIG *c = read_config(&block);
    const MIME_AFD_LINK *links = c ? (const MIME_AFD_LINK *)(c + 1) : nullptr;
    const MIME_AFD_LINK_ADDRESS *addresses = c ? (const MIME_AFD_LINK_ADDRESS *)(links + c->Links) : nullptr;
    const MIME_AFD_ROUTE *routes = c ? (const MIME_AFD_ROUTE *)(addresses + c->Addresses) : nullptr;
    IPH_SNAPSHOT *s = allocate(c ? c->Links : 0, c ? c->Addresses : 0, c ? c->Routes : 0);
    if (!s) {
        IphFree(block);
        return nullptr;
    }
    s->from_host = c != nullptr;
    s->created = module_time;
    add_loopback(s);
    IPH_ADAPTER *loopback = &s->adapters[0];
    loopback->first_address = s->address_count;
    IPH_ADDRESS *e = new_address(s, AF_INET6, loopback6, 0, 128);
    e->index = LOOPBACK_INDEX;
    e->prefix_origin = IpPrefixOriginWellKnown;
    e->suffix_origin = IpSuffixOriginWellKnown;
    e = new_address(s, AF_INET, loopback4, 0, 8);
    e->index = LOOPBACK_INDEX;
    e->prefix_origin = IpPrefixOriginWellKnown;
    e->suffix_origin = IpSuffixOriginWellKnown;
    loopback->address_count = 2;
    loopback_routes(s, loopback);
    ULONG ethernet = 0, wireless = 0;
    for (ULONG i = 0; c && i < c->Links; i++) {
        const MIME_AFD_LINK *l = &links[i];
        if ((l->Flags & MIME_AFD_IF_LOOPBACK) || l->Type == MIME_AFD_LINK_LOOPBACK || !l->Index ||
            IphAdapterByIndex(s, l->Index))
            continue;
        IPH_ADAPTER *a = &s->adapters[s->adapter_count++];
        a->index = l->Index;
        memcpy(a->host, l->Name, sizeof(a->host) - 1);
        a->hardware = !(l->Flags & MIME_AFD_IF_VIRTUAL);
        a->admin_up = (l->Flags & MIME_AFD_IF_UP) != 0;
        a->up = a->admin_up && (l->Flags & MIME_AFD_IF_RUNNING);
        a->mtu = l->Mtu ? l->Mtu : 1500;
        a->speed = a->up ? l->Speed : 0;
        a->mac_length = l->MacLength <= 8 ? l->MacLength : 8;
        memcpy(a->mac, l->Mac, a->mac_length);
        const char *kind;
        switch (l->Type) {
        case MIME_AFD_LINK_ETHERNET:
            a->type = IF_TYPE_ETHERNET_CSMACD;
            kind = a->hardware ? "Mime Ethernet Adapter" : "Mime Virtual Ethernet Adapter";
            if (a->hardware) {
                append_number(a->alias, RTL_NUMBER_OF(a->alias), L"Ethernet ", ++ethernet);
                if (ethernet == 1)
                    a->alias[8] = 0;
            } else {
                WCHAR host[20];
                widen(host, RTL_NUMBER_OF(host), a->host);
                _snwprintf(a->alias, RTL_NUMBER_OF(a->alias), L"vEthernet (%s)", host);
            }
            break;
        case MIME_AFD_LINK_WIRELESS:
            a->type = IF_TYPE_IEEE80211;
            kind = "Mime Wi-Fi Adapter";
            append_number(a->alias, RTL_NUMBER_OF(a->alias), L"Wi-Fi ", ++wireless);
            if (wireless == 1)
                a->alias[5] = 0;
            break;
        case MIME_AFD_LINK_TUNNEL:
            a->type = IF_TYPE_PROP_VIRTUAL;
            kind = "Mime Tunnel Adapter";
            break;
        case MIME_AFD_LINK_PPP:
            a->type = IF_TYPE_PPP;
            kind = "Mime PPP Adapter";
            break;
        default:
            a->type = IF_TYPE_OTHER;
            kind = "Mime Network Adapter";
            break;
        }
        if (!a->alias[0])
            widen(a->alias, RTL_NUMBER_OF(a->alias), a->host);
        /* windows numbers adapters of the same description: "#2", "#3"... */
        ULONG same = 1;
        WCHAR wide[64];
        widen(wide, RTL_NUMBER_OF(wide), kind);
        for (ULONG k = 1; k + 1 < s->adapter_count; k++)
            if (!wcsncmp(s->adapters[k].description, wide, wcslen(wide)))
                same++;
        if (same > 1)
            _snwprintf(a->description, RTL_NUMBER_OF(a->description), L"%s #%lu", wide, (unsigned long)same);
        else
            memcpy(a->description, wide, (wcslen(wide) + 1) * sizeof(WCHAR));
        a->luid.Info.IfType = a->type;
        a->luid.Info.NetLuidIndex = 32768 + a->index;
        name_guid(&a->guid, "mime adapter ", a->host);
        name_guid(&a->network, "mime network ", a->host);
        guid_string(a->name, &a->guid);
        a->zones[1] = a->zones[2] = a->zones[3] = a->index;
        for (int k = 4; k < 14; k++)
            a->zones[k] = 1;
        a->metric = speed_metric(a->speed);
        a->ipv4 = TRUE;
        if (a->up)
            add_adapter_addresses(s, a, c, addresses);
        a->ipv6 = !a->up;
        BOOL v4 = FALSE, dynamic = FALSE;
        for (ULONG k = 0; k < a->address_count; k++) {
            const IPH_ADDRESS *x = &s->addresses[a->first_address + k];
            if (x->family == AF_INET6)
                a->ipv6 = TRUE;
            if (x->family == AF_INET) {
                v4 = TRUE;
                dynamic |= (x->flags & MIME_AFD_ADDR_DYNAMIC) != 0;
            }
        }
        BOOL wired_or_wireless = a->type == IF_TYPE_ETHERNET_CSMACD || a->type == IF_TYPE_IEEE80211;
        a->dhcp = dynamic || (!v4 && a->hardware && wired_or_wireless);
        if (a->up)
            adapter_routes(s, a);
    }
    /* the host's routes through a gateway, of adapters that are up. */
    for (ULONG i = 0; c && i < c->Routes; i++) {
        const MIME_AFD_ROUTE *r = &routes[i];
        const IPH_ADAPTER *a = IphAdapterByIndex(s, r->Index);
        if (!a || !a->up || a->type == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        ADDRESS_FAMILY family = r->Destination.Family;
        const BYTE *destination = family == AF_INET ? r->Destination.Data + 2 : r->Destination.Data + 6;
        const BYTE *gateway = family == AF_INET ? r->Gateway.Data + 2 : r->Gateway.Data + 6;
        ULONG scope = 0;
        if (family == AF_INET6)
            memcpy(&scope, r->Gateway.Data + 22, 4);
        NL_ROUTE_PROTOCOL protocol = r->Origin == MIME_AFD_ROUTE_RA ? RouteProtocolIcmp : MIB_IPPROTO_NETMGMT;
        NL_ROUTE_ORIGIN origin = r->Origin == MIME_AFD_ROUTE_DHCP ? NlroDHCP
                                 : r->Origin == MIME_AFD_ROUTE_RA ? NlroRouterAdvertisement
                                                                  : NlroManual;
        add_route(s, a->index, family, destination, r->Prefix, gateway, scope, r->Metric, protocol, origin, FALSE);
    }
    sort_routes(s);
    for (ULONG i = 0; i < s->adapter_count; i++)
        adapter_multicasts(s, &s->adapters[i]);
    /* the resolver: the host's servers belong to the adapters with a default gateway, or else to those with an
       address wider than the link. */
    if (c) {
        for (ULONG i = 0; i < c->Servers && i < MIME_AFD_MAX_DNS; i++) {
            const MIME_AFD_ADDRESS *h = &c->Server[i];
            ULONG scope = 0;
            if (h->Family == AF_INET6)
                memcpy(&scope, h->Data + 22, 4);
            IphSockaddr(&s->servers[s->server_count++], h->Family,
                        h->Family == AF_INET ? h->Data + 2 : h->Data + 6, scope);
        }
        memcpy(s->domain, c->Domain, sizeof(s->domain) - 1);
        memcpy(s->search, c->Search, sizeof(s->search) - 1);
    }
    BOOL any = FALSE;
    for (ULONG i = 1; i < s->adapter_count; i++) {
        IPH_ADAPTER *a = &s->adapters[i];
        for (ULONG k = 0; a->up && k < s->route_count; k++)
            if (s->routes[k].index == a->index && !s->routes[k].prefix)
                a->dns = any = TRUE;
    }
    for (ULONG i = 1; !any && i < s->adapter_count; i++) {
        IPH_ADAPTER *a = &s->adapters[i];
        for (ULONG k = 0; a->up && k < a->address_count; k++) {
            const IPH_ADDRESS *x = &s->addresses[a->first_address + k];
            if (IphAddressScope(x->family, x->bytes) > 2)
                a->dns = TRUE;
        }
    }
    IphFree(block);
    return s;
}

/* when even memory is missing, the loopback alone, from storage of our own. */
static IPH_SNAPSHOT *fallback(void)
{
    static IPH_ADAPTER adapters[1];
    static IPH_ADDRESS addresses[1];
    static IPH_ROUTE routes[1];
    static IPH_SNAPSHOT empty = {.refs = 1 << 30, .adapters = adapters, .addresses = addresses, .routes = routes,
                                 .multicasts = addresses};
    return &empty;
}

void IphRelease(IPH_SNAPSHOT *s)
{
    if (!s || s == fallback())
        return;
    if (InterlockedDecrement(&s->refs))
        return;
    IphFree(s->adapters);
    IphFree(s->addresses);
    IphFree(s->routes);
    IphFree(s->multicasts);
    IphFree(s);
}

IPH_SNAPSHOT *IphFresh(void)
{
    if (!module_time.QuadPart)
        module_time.QuadPart = (LONGLONG)now_filetime();
    IPH_SNAPSHOT *s = build();
    if (!s)
        return fallback();
    s->taken = GetTickCount64();
    return s;
}

static SRWLOCK cache_lock = SRWLOCK_INIT;
static IPH_SNAPSHOT *cache;

IPH_SNAPSHOT *IphAcquire(void)
{
    AcquireSRWLockExclusive(&cache_lock);
    if (!cache || GetTickCount64() - cache->taken > 500) {
        IphRelease(cache);
        cache = IphFresh();
    }
    IPH_SNAPSHOT *s = cache;
    if (s != fallback())
        InterlockedIncrement(&s->refs);
    ReleaseSRWLockExclusive(&cache_lock);
    return s;
}

/* ------------------------------------------------------------------ connectivity */

void IphHint(const IPH_SNAPSHOT *s, const IPH_ADAPTER *only, NL_NETWORK_CONNECTIVITY_HINT *hint)
{
    memset(hint, 0, sizeof(*hint));
    NL_NETWORK_CONNECTIVITY_LEVEL_HINT level = NetworkConnectivityLevelHintNone;
    for (ULONG i = 1; i < s->adapter_count; i++) {
        const IPH_ADAPTER *a = &s->adapters[i];
        if ((only && a != only) || !a->up)
            continue;
        for (ULONG k = 0; k < s->route_count; k++)
            if (s->routes[k].index == a->index && !s->routes[k].prefix)
                level = NetworkConnectivityLevelHintInternetAccess;
        if (level < NetworkConnectivityLevelHintLocalAccess && a->address_count)
            level = NetworkConnectivityLevelHintLocalAccess;
    }
    hint->ConnectivityLevel = level;
    hint->ConnectivityCost =
        level >= NetworkConnectivityLevelHintLocalAccess ? NetworkConnectivityCostHintUnrestricted
                                                         : NetworkConnectivityCostHintUnknown;
}

DWORD NETIOAPI_API_ GetNetworkConnectivityHint(NL_NETWORK_CONNECTIVITY_HINT *hint)
{
    IPH_SNAPSHOT *s = IphAcquire();
    IphHint(s, nullptr, hint);
    IphRelease(s);
    return NO_ERROR;
}

DWORD NETIOAPI_API_ GetNetworkConnectivityHintForInterface(NET_IFINDEX index, NL_NETWORK_CONNECTIVITY_HINT *hint)
{
    if (!hint)
        return ERROR_INVALID_PARAMETER;
    memset(hint, 0, sizeof(*hint));
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByIndex(s, index);
    DWORD error = a ? NO_ERROR : ERROR_FILE_NOT_FOUND;
    if (a)
        IphHint(s, a, hint);
    IphRelease(s);
    return error;
}
