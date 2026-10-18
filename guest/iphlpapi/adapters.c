/*
 * the adapter lists: GetAdaptersAddresses, GetAdaptersInfo, the network
 * and per adapter parameters, and the ipv4 interface list.
 */
#include "private.h"

/* ------------------------------------------------------------------ GetAdaptersAddresses */

/* lays the result out twice: once to measure it, once to write it into the caller's buffer. */
typedef struct {
    BYTE *base;
    SIZE_T used;
} LAYOUT;

static void *take(LAYOUT *l, SIZE_T size, SIZE_T align)
{
    l->used = (l->used + align - 1) & ~(align - 1);
    void *p = l->base ? l->base + l->used : nullptr;
    l->used += size;
    return p;
}

static PWSTR take_string(LAYOUT *l, const WCHAR *s)
{
    SIZE_T n = (wcslen(s) + 1) * sizeof(WCHAR);
    PWSTR p = take(l, n, sizeof(WCHAR));
    if (p)
        memcpy(p, s, n);
    return p;
}

/* an entry of one of the address lists, with its sockaddr right behind it. */
static void *take_entry(LAYOUT *l, SIZE_T size, ADDRESS_FAMILY family, const BYTE *bytes, ULONG scope,
                        SOCKET_ADDRESS **address)
{
    l->used = (l->used + 7) & ~(SIZE_T)7;
    BYTE *entry = take(l, size, 8);
    SIZE_T length = IphSockaddrSize(family);
    SOCKADDR_INET *sa = take(l, (length + 7) & ~(SIZE_T)7, 1);
    if (entry) {
        SOCKADDR_INET full;
        IphSockaddr(&full, family, bytes, scope);
        memcpy(sa, &full, length);
        *(ULONG *)entry = (ULONG)size;
        *address = (SOCKET_ADDRESS *)(entry + 8 + sizeof(void *));
        (*address)->lpSockaddr = (SOCKADDR *)sa;
        (*address)->iSockaddrLength = (INT)length;
    }
    return entry;
}

static ULONG adapter_flags(const IPH_ADAPTER *a, ULONG flags)
{
    if (a->type == IF_TYPE_SOFTWARE_LOOPBACK)
        return IP_ADAPTER_IPV4_ENABLED | IP_ADAPTER_IPV6_ENABLED;
    ULONG f = 0;
    if (!(flags & GAA_FLAG_SKIP_DNS_INFO))
        f |= IP_ADAPTER_DDNS_ENABLED;
    if (a->dhcp)
        f |= IP_ADAPTER_DHCP_ENABLED;
    if (a->ipv4)
        f |= IP_ADAPTER_NETBIOS_OVER_TCPIP_ENABLED | IP_ADAPTER_IPV4_ENABLED;
    if (a->ipv6)
        f |= IP_ADAPTER_IPV6_ENABLED | IP_ADAPTER_IPV6_MANAGE_ADDRESS_CONFIG;
    return f;
}

/* the connection specific suffix: the resolver's domain, or its first search domain. */
static void dns_suffix(const IPH_SNAPSHOT *s, WCHAR *out, SIZE_T count)
{
    const char *from = s->domain[0] ? s->domain : s->search;
    SIZE_T i = 0;
    for (; from[i] && from[i] != ' ' && from[i] != ',' && i + 1 < count; i++)
        out[i] = (BYTE)from[i];
    out[i] = 0;
}

static void layout_adapter(LAYOUT *l, const IPH_SNAPSHOT *s, const IPH_ADAPTER *a, ULONG family, ULONG flags,
                           IP_ADAPTER_ADDRESSES **link)
{
    IP_ADAPTER_ADDRESSES *out = take(l, sizeof(*out), 8);
    if (out) {
        out->Length = sizeof(*out);
        out->IfIndex = a->index;
        out->Flags = adapter_flags(a, flags);
        out->Mtu = a->mtu;
        out->IfType = a->type;
        out->OperStatus = a->up ? IfOperStatusUp : IfOperStatusDown;
        out->Ipv6IfIndex = a->ipv6 ? a->index : 0;
        memcpy(out->ZoneIndices, a->zones, sizeof(out->ZoneIndices));
        out->TransmitLinkSpeed = a->speed;
        out->ReceiveLinkSpeed = a->speed;
        out->Ipv4Metric = a->ipv4 ? a->metric : 0;
        out->Ipv6Metric = a->ipv6 ? a->metric : 0;
        out->Luid = a->luid;
        out->CompartmentId = NET_IF_COMPARTMENT_ID_PRIMARY;
        out->NetworkGuid = a->network;
        out->ConnectionType = NET_IF_CONNECTION_DEDICATED;
        out->TunnelType = TUNNEL_TYPE_NONE;
        memcpy(out->PhysicalAddress, a->mac, a->mac_length);
        out->PhysicalAddressLength = a->mac_length;
        if (*link)
            (*link)->Next = out;
        *link = out;
    }
    BOOL dns = a->dns && !(flags & GAA_FLAG_SKIP_DNS_INFO);
    WCHAR suffix[MAX_DNS_SUFFIX_STRING_LENGTH] = {};
    if (dns)
        dns_suffix(s, suffix, RTL_NUMBER_OF(suffix));
    /* windows ignores GAA_FLAG_SKIP_FRIENDLY_NAME: the alias is always there. */
    PWSTR dns_text = take_string(l, suffix);
    PWSTR friendly = take_string(l, a->alias);
    PWSTR description = take_string(l, a->description);
    char *name = take(l, strlen(a->name) + 1, 1);
    if (out) {
        out->DnsSuffix = dns_text;
        out->FriendlyName = friendly;
        out->Description = description;
        memcpy(name, a->name, strlen(a->name) + 1);
        out->AdapterName = name;
    }
    if (!(flags & GAA_FLAG_SKIP_UNICAST)) {
        void **next = out ? (void **)&out->FirstUnicastAddress : nullptr;
        for (ULONG i = 0; i < a->address_count; i++) {
            const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
            if (!IphFamilyWanted(family, e->family))
                continue;
            SOCKET_ADDRESS *sa;
            IP_ADAPTER_UNICAST_ADDRESS *u = take_entry(l, sizeof(*u), e->family, e->bytes, e->scope, &sa);
            if (!u)
                continue;
            if (e->eligible)
                u->Flags |= IP_ADAPTER_ADDRESS_DNS_ELIGIBLE;
            u->PrefixOrigin = e->prefix_origin;
            u->SuffixOrigin = e->suffix_origin;
            u->DadState = e->dad;
            u->ValidLifetime = e->valid;
            u->PreferredLifetime = e->preferred;
            u->LeaseLifetime = e->valid;
            u->OnLinkPrefixLength = e->prefix;
            *next = u;
            next = (void **)&u->Next;
        }
    }
    if (!(flags & GAA_FLAG_SKIP_MULTICAST)) {
        void **next = out ? (void **)&out->FirstMulticastAddress : nullptr;
        for (int pass = 0; pass < 2; pass++) {
            ADDRESS_FAMILY f = pass ? AF_INET : AF_INET6;
            for (ULONG i = 0; IphFamilyWanted(family, f) && i < s->multicast_count; i++) {
                const IPH_ADDRESS *m = &s->multicasts[i];
                if (m->index != a->index || m->family != f)
                    continue;
                SOCKET_ADDRESS *sa;
                IP_ADAPTER_MULTICAST_ADDRESS *e = take_entry(l, sizeof(*e), f, m->bytes, m->scope, &sa);
                if (!e)
                    continue;
                *next = e;
                next = (void **)&e->Next;
            }
        }
    }
    if (dns && !(flags & GAA_FLAG_SKIP_DNS_SERVER)) {
        void **next = out ? (void **)&out->FirstDnsServerAddress : nullptr;
        for (ULONG i = 0; i < s->server_count; i++) {
            const SOCKADDR_INET *server = &s->servers[i];
            if (!IphFamilyWanted(family, server->si_family))
                continue;
            const BYTE *bytes = server->si_family == AF_INET ? (const BYTE *)&server->Ipv4.sin_addr
                                                              : (const BYTE *)&server->Ipv6.sin6_addr;
            SOCKET_ADDRESS *sa;
            IP_ADAPTER_DNS_SERVER_ADDRESS *e =
                take_entry(l, sizeof(*e), server->si_family, bytes, server->Ipv6.sin6_scope_id, &sa);
            if (!e)
                continue;
            *next = e;
            next = (void **)&e->Next;
        }
        /* the search domains, split at spaces and commas. */
        IP_ADAPTER_DNS_SUFFIX *last = nullptr;
        for (const char *p = s->search; *p;) {
            while (*p == ' ' || *p == ',')
                p++;
            SIZE_T n = 0;
            while (p[n] && p[n] != ' ' && p[n] != ',')
                n++;
            if (!n)
                break;
            IP_ADAPTER_DNS_SUFFIX *e = take(l, sizeof(*e), 8);
            if (e && n < MAX_DNS_SUFFIX_STRING_LENGTH) {
                memset(e, 0, sizeof(*e));
                for (SIZE_T k = 0; k < n; k++)
                    e->String[k] = (BYTE)p[k];
                if (last)
                    last->Next = e;
                else
                    out->FirstDnsSuffix = e;
                last = e;
            }
            p += n;
        }
    }
    if (flags & GAA_FLAG_INCLUDE_PREFIX) {
        void **next = out ? (void **)&out->FirstPrefix : nullptr;
        for (int pass = 0; pass < 2; pass++) {
            ADDRESS_FAMILY f = pass ? AF_INET : AF_INET6;
            for (ULONG i = 0; IphFamilyWanted(family, f) && i < s->route_count; i++) {
                const IPH_ROUTE *r = &s->routes[i];
                static const BYTE zero[16];
                if (r->index != a->index || r->family != f || memcmp(r->next_hop, zero, 16))
                    continue;
                SOCKET_ADDRESS *sa;
                IP_ADAPTER_PREFIX *e =
                    take_entry(l, sizeof(*e), f, r->destination, IphZone(a, f, r->destination), &sa);
                if (!e)
                    continue;
                e->PrefixLength = r->prefix;
                *next = e;
                next = (void **)&e->Next;
            }
        }
    }
    if (flags & GAA_FLAG_INCLUDE_GATEWAYS) {
        void **next = out ? (void **)&out->FirstGatewayAddress : nullptr;
        for (int pass = 0; pass < 2; pass++) {
            ADDRESS_FAMILY f = pass ? AF_INET : AF_INET6;
            for (ULONG i = 0; IphFamilyWanted(family, f) && i < s->route_count; i++) {
                const IPH_ROUTE *r = &s->routes[i];
                if (r->index != a->index || r->family != f || r->prefix)
                    continue;
                SOCKET_ADDRESS *sa;
                IP_ADAPTER_GATEWAY_ADDRESS *e = take_entry(l, sizeof(*e), f, r->next_hop, r->next_scope, &sa);
                if (!e)
                    continue;
                *next = e;
                next = (void **)&e->Next;
            }
        }
    }
}

static BOOL listed(const IPH_ADAPTER *a, ULONG family, ULONG flags)
{
    if (flags & GAA_FLAG_INCLUDE_ALL_INTERFACES)
        return TRUE;
    if (family == AF_INET)
        return a->ipv4;
    if (family == AF_INET6)
        return a->ipv6;
    return a->ipv4 || a->ipv6;
}

/* windows lists the adapters by their lowest metric, then by luid. */
static ULONG sort_key(const IPH_ADAPTER *a)
{
    ULONG v4 = a->ipv4 ? a->metric : 0xFFFFFFFF, v6 = a->ipv6 ? a->metric : 0xFFFFFFFF;
    return v4 < v6 ? v4 : v6;
}

static void order_adapters(const IPH_SNAPSHOT *s, const IPH_ADAPTER **order)
{
    for (ULONG i = 0; i < s->adapter_count; i++) {
        const IPH_ADAPTER *a = &s->adapters[i];
        ULONG j = i;
        for (; j && (sort_key(order[j - 1]) > sort_key(a) ||
                     (sort_key(order[j - 1]) == sort_key(a) && order[j - 1]->luid.Value > a->luid.Value));
             j--)
            order[j] = order[j - 1];
        order[j] = a;
    }
}

static SIZE_T layout_all(LAYOUT *l, const IPH_SNAPSHOT *s, const IPH_ADAPTER **order, ULONG family, ULONG flags)
{
    IP_ADAPTER_ADDRESSES *link = nullptr;
    SIZE_T count = 0;
    for (ULONG i = 0; i < s->adapter_count; i++) {
        if (!listed(order[i], family, flags))
            continue;
        l->used = (l->used + 7) & ~(SIZE_T)7;
        layout_adapter(l, s, order[i], family, flags, &link);
        count++;
    }
    return count;
}

ULONG WINAPI GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES addresses,
                                  PULONG size)
{
    if (!size || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER **order = IphAlloc((s->adapter_count + 1) * sizeof(*order));
    if (!order) {
        IphRelease(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    order_adapters(s, order);
    LAYOUT measure = {nullptr, 0};
    ULONG error = NO_ERROR;
    if (!layout_all(&measure, s, order, family, flags)) {
        error = ERROR_NO_DATA;
    } else if (measure.used > 0xFFFFFFFF) {
        error = ERROR_INVALID_DATA;
    } else if (!addresses || *size < measure.used) {
        /* the size asked for is twice what this list needs, at least 16 KiB less one. */
        SIZE_T twice = measure.used * 2;
        *size = measure.used >= 0x7FFFFFFF ? 0xFFFFFFFE : twice <= 0x3FFF ? 0x3FFF : (ULONG)twice;
        error = ERROR_BUFFER_OVERFLOW;
    } else {
        memset(addresses, 0, *size);
        LAYOUT write = {(BYTE *)addresses, 0};
        layout_all(&write, s, order, family, flags);
    }
    IphFree(order);
    IphRelease(s);
    return error;
}

/* ------------------------------------------------------------------ GetAdaptersInfo */

static void address_string(IP_ADDR_STRING *e, const BYTE *address, const BYTE *mask)
{
    IphIpv4String(e->IpAddress.String, address);
    IphIpv4String(e->IpMask.String, mask);
    memcpy(&e->Context, address, 4);
}

static const BYTE any4[4], broadcast4[4] = {255, 255, 255, 255};

static ULONG v4_addresses(const IPH_SNAPSHOT *s, const IPH_ADAPTER *a)
{
    ULONG n = 0;
    for (ULONG i = 0; a->up && i < a->address_count; i++)
        n += s->addresses[a->first_address + i].family == AF_INET;
    return n;
}

static ULONG v4_gateways(const IPH_SNAPSHOT *s, const IPH_ADAPTER *a)
{
    ULONG n = 0;
    for (ULONG i = 0; i < s->route_count; i++) {
        const IPH_ROUTE *r = &s->routes[i];
        n += r->index == a->index && r->family == AF_INET && !r->prefix && memcmp(r->next_hop, any4, 4);
    }
    return n;
}

static BOOL info_listed(const IPH_ADAPTER *a)
{
    return a->type != IF_TYPE_SOFTWARE_LOOPBACK && a->ipv4;
}

ULONG WINAPI GetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    SIZE_T need = 0;
    ULONG count = 0;
    for (ULONG i = 0; i < s->adapter_count; i++) {
        const IPH_ADAPTER *a = &s->adapters[i];
        if (!info_listed(a))
            continue;
        count++;
        ULONG addresses = v4_addresses(s, a), gateways = v4_gateways(s, a);
        need += sizeof(IP_ADAPTER_INFO) + ((addresses ? addresses - 1 : 0) + (gateways ? gateways - 1 : 0)) *
                                              sizeof(IP_ADDR_STRING);
    }
    ULONG error = NO_ERROR;
    if (!count) {
        error = ERROR_NO_DATA;
    } else if (!info || *size < need) {
        *size = (ULONG)need;
        error = ERROR_BUFFER_OVERFLOW;
    } else {
        memset(info, 0, *size);
        BYTE *p = (BYTE *)info;
        IP_ADAPTER_INFO *previous = nullptr;
        for (ULONG i = 0; i < s->adapter_count; i++) {
            const IPH_ADAPTER *a = &s->adapters[i];
            if (!info_listed(a))
                continue;
            IP_ADAPTER_INFO *e = (IP_ADAPTER_INFO *)p;
            p += sizeof(*e);
            if (previous)
                previous->Next = e;
            previous = e;
            e->ComboIndex = e->Index = a->index;
            strcpy(e->AdapterName, a->name);
            SIZE_T k = 0;
            for (; a->description[k] && k < MAX_ADAPTER_DESCRIPTION_LENGTH; k++)
                e->Description[k] = a->description[k] < 0x80 ? (char)a->description[k] : '?';
            e->AddressLength = a->mac_length;
            memcpy(e->Address, a->mac, a->mac_length);
            e->Type = a->type;
            e->DhcpEnabled = a->dhcp;
            IP_ADDR_STRING *last = nullptr;
            for (ULONG j = 0; a->up && j < a->address_count; j++) {
                const IPH_ADDRESS *x = &s->addresses[a->first_address + j];
                if (x->family != AF_INET)
                    continue;
                ULONG mask = IphIpv4Mask(x->prefix);
                IP_ADDR_STRING *slot = last ? (IP_ADDR_STRING *)p : &e->IpAddressList;
                if (last) {
                    p += sizeof(*slot);
                    last->Next = slot;
                }
                address_string(slot, x->bytes, (const BYTE *)&mask);
                last = slot;
            }
            if (!last) {
                address_string(&e->IpAddressList, any4, any4);
                e->IpAddressList.Context = 0;
            }
            last = nullptr;
            for (ULONG j = 0; j < s->route_count; j++) {
                const IPH_ROUTE *r = &s->routes[j];
                if (r->index != a->index || r->family != AF_INET || r->prefix || !memcmp(r->next_hop, any4, 4))
                    continue;
                IP_ADDR_STRING *slot = last ? (IP_ADDR_STRING *)p : &e->GatewayList;
                if (last) {
                    p += sizeof(*slot);
                    last->Next = slot;
                }
                address_string(slot, r->next_hop, broadcast4);
                last = slot;
            }
            if (!last) {
                address_string(&e->GatewayList, any4, broadcast4);
                e->GatewayList.Context = 0;
            }
        }
    }
    IphRelease(s);
    return error;
}

/* ------------------------------------------------------------------ network and adapter parameters */

static ULONG dns4(const IPH_SNAPSHOT *s, const IPH_ADAPTER *only, const BYTE **list, ULONG max)
{
    BOOL any = FALSE;
    for (ULONG i = 1; i < s->adapter_count; i++)
        any |= s->adapters[i].dns && (!only || only == &s->adapters[i]);
    ULONG n = 0;
    for (ULONG i = 0; any && i < s->server_count && n < max; i++)
        if (s->servers[i].si_family == AF_INET)
            list[n++] = (const BYTE *)&s->servers[i].Ipv4.sin_addr;
    return n;
}

static void server_list(IP_ADDR_STRING *first, BYTE *extra, const BYTE **list, ULONG n)
{
    IP_ADDR_STRING *last = nullptr;
    for (ULONG i = 0; i < n; i++) {
        IP_ADDR_STRING *slot = last ? (IP_ADDR_STRING *)(extra + (i - 1) * sizeof(IP_ADDR_STRING)) : first;
        if (last)
            last->Next = slot;
        address_string(slot, list[i], broadcast4);
        last = slot;
    }
}

DWORD WINAPI GetNetworkParams(PFIXED_INFO info, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const BYTE *list[MIME_AFD_MAX_DNS];
    ULONG n = dns4(s, nullptr, list, MIME_AFD_MAX_DNS);
    SIZE_T need = sizeof(FIXED_INFO) + (n ? n - 1 : 0) * sizeof(IP_ADDR_STRING);
    DWORD error = NO_ERROR;
    if (!info || *size < need) {
        *size = (ULONG)need;
        error = ERROR_BUFFER_OVERFLOW;
    } else {
        memset(info, 0, *size);
        DWORD length = sizeof(info->HostName);
        if (!GetComputerNameExA(ComputerNameDnsHostname, info->HostName, &length))
            info->HostName[0] = 0;
        length = sizeof(info->DomainName);
        if (!GetComputerNameExA(ComputerNameDnsDomain, info->DomainName, &length))
            info->DomainName[0] = 0;
        server_list(&info->DnsServerList, (BYTE *)(info + 1), list, n);
        /* netbt's NodeType when its registry has none: hybrid. */
        info->NodeType = HYBRID_NODETYPE;
    }
    IphRelease(s);
    return error;
}

DWORD WINAPI GetPerAdapterInfo(ULONG index, PIP_PER_ADAPTER_INFO info, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByIndex(s, index);
    if (!a) {
        IphRelease(s);
        return ERROR_NO_DATA;
    }
    const BYTE *list[MIME_AFD_MAX_DNS];
    ULONG n = dns4(s, a, list, MIME_AFD_MAX_DNS);
    SIZE_T need = sizeof(IP_PER_ADAPTER_INFO) + (n ? n - 1 : 0) * sizeof(IP_ADDR_STRING);
    DWORD error = NO_ERROR;
    if (!info || *size < need) {
        *size = (ULONG)need;
        error = ERROR_BUFFER_OVERFLOW;
    } else {
        memset(info, 0, *size);
        info->AutoconfigEnabled = a->ipv4 && a->dhcp;
        for (ULONG i = 0; info->AutoconfigEnabled && i < a->address_count; i++) {
            const IPH_ADDRESS *x = &s->addresses[a->first_address + i];
            if (x->family == AF_INET && x->bytes[0] == 169 && x->bytes[1] == 254 && x->dad == IpDadStatePreferred)
                info->AutoconfigActive = TRUE;
        }
        server_list(&info->DnsServerList, (BYTE *)(info + 1), list, n);
    }
    IphRelease(s);
    return error;
}

/* ------------------------------------------------------------------ the ipv4 interface list */

static void device_name(WCHAR *out, const IPH_ADAPTER *a)
{
    WCHAR guid[40];
    SIZE_T i = 0;
    for (; a->name[i]; i++)
        guid[i] = (BYTE)a->name[i];
    guid[i] = 0;
    _snwprintf(out, MAX_ADAPTER_NAME, L"\\DEVICE\\TCPIP_%s", guid);
}

DWORD WINAPI GetInterfaceInfo(PIP_INTERFACE_INFO table, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->adapter_count; i++) {
        const IPH_ADAPTER *a = &s->adapters[i];
        if (!info_listed(a))
            continue;
        if (table && IphTableSize(n + 1, sizeof(IP_ADAPTER_INDEX_MAP), FALSE) <= *size) {
            table->Adapter[n].Index = a->index;
            device_name(table->Adapter[n].Name, a);
        }
        n++;
    }
    if (table && *size >= offsetof(IP_INTERFACE_INFO, Adapter))
        table->NumAdapters = (LONG)n;
    DWORD error = IphCheckTable(n, sizeof(IP_ADAPTER_INDEX_MAP), table, size);
    if (!error && !n)
        error = ERROR_NO_DATA;
    IphRelease(s);
    return error;
}

DWORD WINAPI GetNumberOfInterfaces(PDWORD count)
{
    if (!count)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    *count = s->adapter_count;
    IphRelease(s);
    return NO_ERROR;
}

DWORD WINAPI GetAdapterIndex(LPWSTR name, PULONG index)
{
    if (!name || !index || wcslen(name) <= 14)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    DWORD error = ERROR_FILE_NOT_FOUND;
    *index = 0;
    for (ULONG i = 0; i < s->adapter_count; i++) {
        WCHAR full[MAX_ADAPTER_NAME];
        device_name(full, &s->adapters[i]);
        if (!_wcsicmp(full + 14, name + 14)) {
            *index = s->adapters[i].index;
            error = NO_ERROR;
            break;
        }
    }
    IphRelease(s);
    return error;
}

DWORD WINAPI GetFriendlyIfIndex(DWORD index)
{
    return index & 0xFFFFFF;
}

/* mime has no dhcp client service, so its rpc server is not there to release or renew a lease. */
DWORD WINAPI IpReleaseAddress(PIP_ADAPTER_INDEX_MAP adapter)
{
    if (!adapter || wcsnlen(adapter->Name, MAX_ADAPTER_NAME) <= 14)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI IpRenewAddress(PIP_ADAPTER_INDEX_MAP adapter)
{
    if (!adapter || wcsnlen(adapter->Name, MAX_ADAPTER_NAME) <= 14)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}
