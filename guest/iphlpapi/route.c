/*
 * routing table and route selection. routes are the ones the stack makes for the adapters' addresses and the
 * loopback, plus the host's gateway routes. a destination takes the longest matching prefix, then the lowest route
 * and interface metric.
 */
#include "private.h"

static BOOL prefix_matches(const BYTE *a, const BYTE *b, UINT8 bits)
{
    for (int i = 0; bits; i++) {
        BYTE mask = bits >= 8 ? 0xFF : (BYTE)(0xFF << (8 - bits));
        if ((a[i] ^ b[i]) & mask)
            return FALSE;
        bits = bits >= 8 ? bits - 8 : 0;
    }
    return TRUE;
}

static int common_prefix(const BYTE *a, const BYTE *b, int length)
{
    int bits = 0;
    for (int i = 0; i < length; i++) {
        BYTE x = a[i] ^ b[i];
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
static const IPH_ADDRESS *pick_source(const IPH_SNAPSHOT *s, const IPH_ADAPTER *a, ADDRESS_FAMILY family,
                                      const BYTE *destination)
{
    const IPH_ADDRESS *best = nullptr;
    int best_score = -1;
    int length = family == AF_INET ? 4 : 16;
    int scope = IphAddressScope(family, destination);
    for (ULONG i = 0; i < a->address_count; i++) {
        const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
        if (e->family != family || e->dad == IpDadStateTentative || e->dad == IpDadStateDuplicate)
            continue;
        if (!memcmp(e->bytes, destination, length))
            return e;
        int score = common_prefix(e->bytes, destination, length);
        if ((IphAddressScope(family, e->bytes) <= 2) == (scope <= 2))
            score += 1 << 12;
        if (e->dad == IpDadStatePreferred)
            score += 1 << 11;
        if (family == AF_INET6 && (e->flags & MIME_AFD_ADDR_TEMPORARY))
            score += 1 << 10;
        if (score > best_score) {
            best = e;
            best_score = score;
        }
    }
    return best;
}

DWORD IphBestRoute(const IPH_SNAPSHOT *s, const NET_LUID *luid, ULONG index, const SOCKADDR_INET *source,
                   const SOCKADDR_INET *destination, const IPH_ROUTE **route, const IPH_ADDRESS **from)
{
    ADDRESS_FAMILY family = destination->si_family;
    const BYTE *bytes =
        family == AF_INET ? (const BYTE *)&destination->Ipv4.sin_addr : (const BYTE *)&destination->Ipv6.sin6_addr;
    const IPH_ADAPTER *only = nullptr;
    if (luid && luid->Value) {
        if (!(only = IphAdapterByLuid(s, luid)))
            return ERROR_NOT_FOUND;
    } else if (index) {
        if (!(only = IphAdapterByIndex(s, index)))
            return ERROR_FILE_NOT_FOUND;
    }
    /* a link scope destination with a zone goes out that zone's interface. */
    ULONG zone = family == AF_INET6 && IphAddressScope(family, bytes) <= 2 ? destination->Ipv6.sin6_scope_id : 0;
    const IPH_ROUTE *best = nullptr;
    ULONG best_metric = 0;
    for (ULONG i = 0; i < s->route_count; i++) {
        const IPH_ROUTE *r = &s->routes[i];
        if (r->family != family || !prefix_matches(r->destination, bytes, r->prefix))
            continue;
        const IPH_ADAPTER *a = IphAdapterByIndex(s, r->index);
        if (!a || !a->up || (only && a != only) || (zone && a->index != zone && r->prefix))
            continue;
        ULONG metric = r->metric + a->metric;
        if (!best || r->prefix > best->prefix || (r->prefix == best->prefix && metric < best_metric)) {
            best = r;
            best_metric = metric;
        }
    }
    if (!best)
        return ERROR_NETWORK_UNREACHABLE;
    const IPH_ADAPTER *a = IphAdapterByIndex(s, best->index);
    const IPH_ADDRESS *chosen = nullptr;
    if (source && source->si_family == family) {
        const BYTE *wanted =
            family == AF_INET ? (const BYTE *)&source->Ipv4.sin_addr : (const BYTE *)&source->Ipv6.sin6_addr;
        static const BYTE zero[16];
        if (memcmp(wanted, zero, family == AF_INET ? 4 : 16)) {
            for (ULONG i = 0; i < a->address_count && !chosen; i++) {
                const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
                if (e->family == family && !memcmp(e->bytes, wanted, family == AF_INET ? 4 : 16))
                    chosen = e;
            }
            if (!chosen)
                return ERROR_INVALID_PARAMETER;
        }
    }
    if (!chosen)
        chosen = pick_source(s, a, family, bytes);
    if (!chosen)
        return ERROR_NETWORK_UNREACHABLE;
    *route = best;
    *from = chosen;
    return NO_ERROR;
}

void IphRouteRow(const IPH_SNAPSHOT *s, const IPH_ROUTE *r, MIB_IPFORWARD_ROW2 *row)
{
    const IPH_ADAPTER *a = IphAdapterByIndex(s, r->index);
    memset(row, 0, sizeof(*row));
    if (a)
        row->InterfaceLuid = a->luid;
    row->InterfaceIndex = r->index;
    IphSockaddr(&row->DestinationPrefix.Prefix, r->family, r->destination, 0);
    row->DestinationPrefix.PrefixLength = r->prefix;
    IphSockaddr(&row->NextHop, r->family, r->next_hop, r->next_scope);
    row->ValidLifetime = INFINITE_LIFETIME;
    row->PreferredLifetime = INFINITE_LIFETIME;
    row->Metric = r->metric;
    row->Protocol = r->protocol;
    row->Loopback = r->loopback;
    row->Immortal = r->immortal;
    row->Origin = r->origin;
}

/* ------------------------------------------------------------------ tables */

DWORD NETIOAPI_API_ GetIpForwardTable2(ADDRESS_FAMILY family, PMIB_IPFORWARD_TABLE2 *table)
{
    if (!table || !IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->route_count; i++)
        n += IphFamilyWanted(family, s->routes[i].family);
    PMIB_IPFORWARD_TABLE2 t = IphAlloc(IphTableSize(n, sizeof(MIB_IPFORWARD_ROW2), FALSE));
    if (t) {
        for (ULONG i = 0; i < s->route_count; i++)
            if (IphFamilyWanted(family, s->routes[i].family))
                IphRouteRow(s, &s->routes[i], &t->Table[t->NumEntries++]);
    }
    IphRelease(s);
    *table = t;
    return t ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

/* an adapter's first preferred ipv4 address, which the legacy table shows as an on link route's next hop. */
static const IPH_ADDRESS *local_address(const IPH_SNAPSHOT *s, const IPH_ADAPTER *a)
{
    for (ULONG i = 0; a && i < a->address_count; i++) {
        const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
        if (e->family == AF_INET && e->dad == IpDadStatePreferred)
            return e;
    }
    return nullptr;
}

DWORD WINAPI GetIpForwardTable(PMIB_IPFORWARDTABLE table, PULONG size, BOOL order)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->route_count; i++)
        n += s->routes[i].family == AF_INET;
    DWORD error = IphCheckTable(n, sizeof(MIB_IPFORWARDROW), table, size);
    if (!error) {
        ULONG count = 0;
        static const BYTE zero[4];
        for (ULONG i = 0; i < s->route_count; i++) {
            const IPH_ROUTE *r = &s->routes[i];
            if (r->family != AF_INET)
                continue;
            const IPH_ADAPTER *a = IphAdapterByIndex(s, r->index);
            MIB_IPFORWARDROW *row = &table->table[count++];
            memset(row, 0, sizeof(*row));
            memcpy(&row->dwForwardDest, r->destination, 4);
            row->dwForwardMask = IphIpv4Mask(r->prefix);
            BOOL direct = !memcmp(r->next_hop, zero, 4);
            const IPH_ADDRESS *local = direct ? local_address(s, a) : nullptr;
            memcpy(&row->dwForwardNextHop, direct ? (local ? local->bytes : zero) : r->next_hop, 4);
            row->dwForwardIfIndex = r->index;
            row->dwForwardType = direct ? MIB_IPROUTE_TYPE_DIRECT : MIB_IPROUTE_TYPE_INDIRECT;
            row->dwForwardProto = r->protocol;
            row->dwForwardMetric1 = r->metric + (a ? a->metric : 0);
        }
        table->dwNumEntries = count;
        if (!count)
            error = ERROR_NO_DATA;
    }
    IphRelease(s);
    return error;
}

/* ------------------------------------------------------------------ route selection */

static void map_v4(SOCKADDR_INET *sa)
{
    if (sa->si_family != AF_INET)
        return;
    BYTE b[4];
    memcpy(b, &sa->Ipv4.sin_addr, 4);
    memset(sa, 0, sizeof(*sa));
    sa->Ipv6.sin6_family = AF_INET6;
    BYTE *p = (BYTE *)&sa->Ipv6.sin6_addr;
    p[10] = p[11] = 0xFF;
    memcpy(p + 12, b, 4);
}

static BOOL is_v4_mapped(const SOCKADDR_INET *sa)
{
    static const BYTE prefix[12] = {[10] = 0xFF, [11] = 0xFF};
    return sa->si_family == AF_INET6 && !memcmp(&sa->Ipv6.sin6_addr, prefix, 12);
}

static void unmap(const SOCKADDR_INET *in, SOCKADDR_INET *out)
{
    memset(out, 0, sizeof(*out));
    out->Ipv4.sin_family = AF_INET;
    memcpy(&out->Ipv4.sin_addr, (const BYTE *)&in->Ipv6.sin6_addr + 12, 4);
}

DWORD NETIOAPI_API_ GetBestRoute2(NET_LUID *luid, NET_IFINDEX index, const SOCKADDR_INET *source,
                                  const SOCKADDR_INET *destination, ULONG options, PMIB_IPFORWARD_ROW2 best,
                                  SOCKADDR_INET *best_source)
{
    if (!best || !best_source || !destination ||
        (destination->si_family != AF_INET && destination->si_family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    memset(best, 0, sizeof(*best));
    memset(best_source, 0, sizeof(*best_source));
    SOCKADDR_INET target = *destination, from = {};
    const SOCKADDR_INET *wanted = source && source->si_family == destination->si_family ? source : nullptr;
    BOOL mapped = is_v4_mapped(destination);
    if (mapped) {
        unmap(destination, &target);
        if (wanted) {
            if (!is_v4_mapped(wanted))
                return ERROR_INVALID_PARAMETER;
            unmap(wanted, &from);
            wanted = &from;
        }
    }
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ROUTE *route;
    const IPH_ADDRESS *chosen;
    DWORD error = IphBestRoute(s, luid, index, wanted, &target, &route, &chosen);
    if (!error) {
        IphRouteRow(s, route, best);
        IphSockaddr(best_source, chosen->family, chosen->bytes, chosen->scope);
        if (mapped) {
            map_v4(&best->DestinationPrefix.Prefix);
            best->DestinationPrefix.PrefixLength += 96;
            map_v4(&best->NextHop);
            map_v4(best_source);
        }
    }
    IphRelease(s);
    return error;
}

DWORD WINAPI GetBestRoute(DWORD destination, DWORD source, PMIB_IPFORWARDROW best)
{
    if (!best)
        return ERROR_INVALID_PARAMETER;
    SOCKADDR_INET target = {}, from = {};
    target.Ipv4.sin_family = from.Ipv4.sin_family = AF_INET;
    target.Ipv4.sin_addr.S_un.S_addr = destination;
    from.Ipv4.sin_addr.S_un.S_addr = source;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ROUTE *route;
    const IPH_ADDRESS *chosen;
    DWORD error = IphBestRoute(s, nullptr, 0, &from, &target, &route, &chosen);
    if (error == ERROR_INVALID_PARAMETER)
        error = IphBestRoute(s, nullptr, 0, nullptr, &target, &route, &chosen);
    if (!error) {
        const IPH_ADAPTER *a = IphAdapterByIndex(s, route->index);
        static const BYTE zero[4];
        memset(best, 0, sizeof(*best));
        memcpy(&best->dwForwardDest, route->destination, 4);
        best->dwForwardMask = IphIpv4Mask(route->prefix);
        memcpy(&best->dwForwardNextHop, route->next_hop, 4);
        best->dwForwardIfIndex = route->index;
        best->dwForwardType =
            memcmp(route->next_hop, zero, 4) ? MIB_IPROUTE_TYPE_INDIRECT : MIB_IPROUTE_TYPE_DIRECT;
        best->dwForwardProto = route->protocol;
        best->dwForwardMetric1 = route->metric + (a ? a->metric : 0);
        best->dwForwardMetric2 = best->dwForwardMetric3 = best->dwForwardMetric4 = best->dwForwardMetric5 =
            0xFFFFFFFF;
    }
    IphRelease(s);
    return error;
}

DWORD WINAPI GetBestInterfaceEx(struct sockaddr *destination, PDWORD index)
{
    if (!destination || !index || (destination->sa_family != AF_INET && destination->sa_family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    SOCKADDR_INET target = {};
    memcpy(&target, destination, destination->sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
    /* an interface level scope id names the interface itself. */
    if (target.si_family == AF_INET6 && (target.Ipv6.sin6_scope_id & 0xF0000000) == 0x10000000) {
        *index = target.Ipv6.sin6_scope_id & 0x0FFFFFFF;
        return NO_ERROR;
    }
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ROUTE *route;
    const IPH_ADDRESS *chosen;
    DWORD error = IphBestRoute(s, nullptr, 0, nullptr, &target, &route, &chosen);
    if (!error)
        *index = route->index;
    IphRelease(s);
    return error;
}

DWORD WINAPI GetBestInterface(DWORD destination, PDWORD index)
{
    if (!index)
        return ERROR_INVALID_PARAMETER;
    SOCKADDR_IN target = {};
    target.sin_family = AF_INET;
    target.sin_addr.S_un.S_addr = destination;
    return GetBestInterfaceEx((struct sockaddr *)&target, index);
}
