/* the interface, address, neighbor and connection tables. */
#include "private.h"

void NETIOAPI_API_ FreeMibTable(PVOID memory)
{
    IphFree(memory);
}

/* NumEntries then the rows at offset 8, the layout FreeMibTable expects. */
static void *new_table(ULONG rows, ULONG row)
{
    return IphAlloc(IphTableSize(rows, row, FALSE));
}

/* ------------------------------------------------------------------ interfaces */

static void device_name(WCHAR *out, SIZE_T count, const IPH_ADAPTER *a)
{
    WCHAR guid[40];
    SIZE_T i = 0;
    for (; a->name[i]; i++)
        guid[i] = (BYTE)a->name[i];
    guid[i] = 0;
    _snwprintf(out, count, L"\\DEVICE\\TCPIP_%s", guid);
}

static void if_row(const IPH_ADAPTER *a, MIB_IFROW *row)
{
    memset(row, 0, sizeof(*row));
    device_name(row->wszName, RTL_NUMBER_OF(row->wszName), a);
    row->dwIndex = a->index;
    row->dwType = a->type;
    row->dwMtu = a->mtu;
    row->dwSpeed = a->speed > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)a->speed;
    row->dwPhysAddrLen = a->mac_length;
    memcpy(row->bPhysAddr, a->mac, a->mac_length);
    row->dwAdminStatus = a->admin_up ? MIB_IF_ADMIN_STATUS_UP : MIB_IF_ADMIN_STATUS_DOWN;
    row->dwOperStatus = a->up ? IF_OPER_STATUS_OPERATIONAL : IF_OPER_STATUS_NON_OPERATIONAL;
    SIZE_T i = 0;
    for (; a->description[i] && i + 1 < MAXLEN_IFDESCR; i++)
        row->bDescr[i] = a->description[i] < 0x80 ? (UCHAR)a->description[i] : '?';
    row->dwDescrLen = (DWORD)i + 1;
}

DWORD WINAPI GetIfTable(PMIB_IFTABLE table, PULONG size, BOOL order)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    DWORD error = IphCheckTable(s->adapter_count, sizeof(MIB_IFROW), table, size);
    if (!error) {
        for (ULONG i = 0; i < s->adapter_count; i++)
            if_row(&s->adapters[i], &table->table[i]);
        table->dwNumEntries = s->adapter_count;
        if (order) {
            for (ULONG i = 1; i < s->adapter_count; i++) {
                MIB_IFROW row = table->table[i];
                ULONG j = i;
                for (; j && table->table[j - 1].dwIndex > row.dwIndex; j--)
                    table->table[j] = table->table[j - 1];
                table->table[j] = row;
            }
        }
        if (!s->adapter_count)
            error = ERROR_NO_DATA;
    }
    IphRelease(s);
    return error;
}

DWORD WINAPI GetIfEntry(PMIB_IFROW row)
{
    if (!row)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByIndex(s, row->dwIndex);
    if (a)
        if_row(a, row);
    IphRelease(s);
    return a ? NO_ERROR : ERROR_FILE_NOT_FOUND;
}

static void if_row2(const IPH_ADAPTER *a, MIB_IF_ROW2 *row)
{
    memset(row, 0, sizeof(*row));
    row->InterfaceLuid = a->luid;
    row->InterfaceIndex = a->index;
    row->InterfaceGuid = a->guid;
    memcpy(row->Alias, a->alias, (wcslen(a->alias) + 1) * sizeof(WCHAR));
    memcpy(row->Description, a->description, (wcslen(a->description) + 1) * sizeof(WCHAR));
    row->PhysicalAddressLength = a->mac_length;
    memcpy(row->PhysicalAddress, a->mac, a->mac_length);
    memcpy(row->PermanentPhysicalAddress, a->mac, a->mac_length);
    row->Mtu = a->mtu;
    row->Type = a->type;
    row->TunnelType = TUNNEL_TYPE_NONE;
    switch (a->type) {
    case IF_TYPE_SOFTWARE_LOOPBACK:
        row->MediaType = NdisMediumLoopback;
        row->AccessType = NET_IF_ACCESS_LOOPBACK;
        break;
    case IF_TYPE_IEEE80211:
        row->MediaType = NdisMedium802_3;
        row->PhysicalMediumType = NdisPhysicalMediumNative802_11;
        row->AccessType = NET_IF_ACCESS_BROADCAST;
        break;
    case IF_TYPE_ETHERNET_CSMACD:
        row->MediaType = NdisMedium802_3;
        row->PhysicalMediumType = a->hardware ? NdisPhysicalMedium802_3 : NdisPhysicalMediumUnspecified;
        row->AccessType = NET_IF_ACCESS_BROADCAST;
        break;
    case IF_TYPE_PPP:
        row->MediaType = NdisMediumWan;
        row->AccessType = NET_IF_ACCESS_POINT_TO_POINT;
        break;
    default:
        row->MediaType = NdisMediumIP;
        row->AccessType = NET_IF_ACCESS_POINT_TO_POINT;
        break;
    }
    row->DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    row->InterfaceAndOperStatusFlags.HardwareInterface = a->hardware;
    row->InterfaceAndOperStatusFlags.ConnectorPresent = a->hardware;
    row->InterfaceAndOperStatusFlags.NotMediaConnected = !a->up;
    row->OperStatus = a->up ? IfOperStatusUp : IfOperStatusDown;
    row->AdminStatus = a->admin_up ? NET_IF_ADMIN_STATUS_UP : NET_IF_ADMIN_STATUS_DOWN;
    row->MediaConnectState = a->up ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    row->NetworkGuid = a->network;
    row->ConnectionType = NET_IF_CONNECTION_DEDICATED;
    row->TransmitLinkSpeed = a->speed;
    row->ReceiveLinkSpeed = a->speed;
}

DWORD NETIOAPI_API_ GetIfTable2Ex(MIB_IF_TABLE_LEVEL level, PMIB_IF_TABLE2 *table)
{
    if ((ULONG)level > MibIfTableNormalWithoutStatistics || !table)
        return ERROR_INVALID_PARAMETER;
    *table = nullptr;
    IPH_SNAPSHOT *s = IphAcquire();
    PMIB_IF_TABLE2 t = new_table(s->adapter_count, sizeof(MIB_IF_ROW2));
    if (t) {
        for (ULONG i = 0; i < s->adapter_count; i++)
            if_row2(&s->adapters[i], &t->Table[i]);
        t->NumEntries = s->adapter_count;
    }
    IphRelease(s);
    *table = t;
    return t ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

DWORD NETIOAPI_API_ GetIfTable2(PMIB_IF_TABLE2 *table)
{
    return GetIfTable2Ex(MibIfTableNormal, table);
}

static GUID zero_guid;

DWORD NETIOAPI_API_ GetIfEntry2Ex(MIB_IF_ENTRY_LEVEL level, PMIB_IF_ROW2 row)
{
    if ((level != MibIfEntryNormal && level != MibIfEntryNormalWithoutStatistics) || !row)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = nullptr;
    DWORD error = ERROR_FILE_NOT_FOUND;
    if (row->InterfaceLuid.Value) {
        a = IphAdapterByLuid(s, &row->InterfaceLuid);
    } else if (row->InterfaceIndex) {
        a = IphAdapterByIndex(s, row->InterfaceIndex);
    } else {
        for (ULONG i = 0; i < s->adapter_count && memcmp(&row->InterfaceGuid, &zero_guid, sizeof(GUID)); i++)
            if (!memcmp(&s->adapters[i].guid, &row->InterfaceGuid, sizeof(GUID)))
                a = &s->adapters[i];
        error = ERROR_INVALID_PARAMETER;
    }
    if (a) {
        if_row2(a, row);
        error = NO_ERROR;
    }
    IphRelease(s);
    return error;
}

DWORD NETIOAPI_API_ GetIfEntry2(PMIB_IF_ROW2 row)
{
    return GetIfEntry2Ex(MibIfEntryNormal, row);
}

/* ------------------------------------------------------------------ ip interfaces */

void IphInterfaceRow(const IPH_ADAPTER *a, ADDRESS_FAMILY family, MIB_IPINTERFACE_ROW *row)
{
    memset(row, 0, sizeof(*row));
    BOOL loopback = a->type == IF_TYPE_SOFTWARE_LOOPBACK;
    BOOL discovery = !loopback && a->type != IF_TYPE_PROP_VIRTUAL && a->type != IF_TYPE_PPP;
    row->Family = family;
    row->InterfaceLuid = a->luid;
    row->InterfaceIndex = a->index;
    row->MinRouterAdvertisementInterval = 200;
    row->MaxRouterAdvertisementInterval = 600;
    row->UseAutomaticMetric = TRUE;
    row->UseNeighborUnreachabilityDetection = !loopback;
    row->ManagedAddressConfigurationSupported = family == AF_INET6 && !loopback;
    row->RouterDiscoveryBehavior = family == AF_INET ? RouterDiscoveryDhcp : RouterDiscoveryEnabled;
    row->DadTransmits = loopback ? 0 : family == AF_INET ? 3 : 1;
    row->BaseReachableTime = 30000;
    row->RetransmitTime = 1000;
    row->PathMtuDiscoveryTimeout = 600000;
    row->LinkLocalAddressBehavior = family == AF_INET ? LinkLocalDelayed : LinkLocalAlwaysOn;
    row->LinkLocalAddressTimeout = 6000;
    memcpy(row->ZoneIndices, a->zones, sizeof(row->ZoneIndices));
    row->SitePrefixLength = family == AF_INET6 ? 64 : 0;
    row->Metric = a->metric;
    row->NlMtu = a->mtu;
    row->Connected = (BOOLEAN)a->up;
    row->SupportsWakeUpPatterns = (BOOLEAN)a->hardware;
    row->SupportsNeighborDiscovery = (BOOLEAN)discovery;
    row->SupportsRouterDiscovery = (BOOLEAN)discovery;
    row->ReachableTime = 30000;
}

DWORD NETIOAPI_API_ GetIpInterfaceTable(ADDRESS_FAMILY family, PMIB_IPINTERFACE_TABLE *table)
{
    if (!table || !IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->adapter_count; i++)
        n += (IphFamilyWanted(family, AF_INET) && s->adapters[i].ipv4) +
             (IphFamilyWanted(family, AF_INET6) && s->adapters[i].ipv6);
    PMIB_IPINTERFACE_TABLE t = new_table(n, sizeof(MIB_IPINTERFACE_ROW));
    if (t) {
        for (int pass = 0; pass < 2; pass++) {
            ADDRESS_FAMILY f = pass ? AF_INET6 : AF_INET;
            for (ULONG i = 0; IphFamilyWanted(family, f) && i < s->adapter_count; i++)
                if (IphFamilyEnabled(&s->adapters[i], f))
                    IphInterfaceRow(&s->adapters[i], f, &t->Table[t->NumEntries++]);
        }
    }
    IphRelease(s);
    *table = t;
    return t ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

/* the adapter a row names by luid, else by index. */
static DWORD row_adapter(const IPH_SNAPSHOT *s, const NET_LUID *luid, ULONG index, const IPH_ADAPTER **a)
{
    if (luid->Value) {
        *a = IphAdapterByLuid(s, luid);
        return *a ? NO_ERROR : ERROR_NOT_FOUND;
    }
    if (!index)
        return ERROR_INVALID_PARAMETER;
    *a = IphAdapterByIndex(s, index);
    return *a ? NO_ERROR : ERROR_FILE_NOT_FOUND;
}

DWORD NETIOAPI_API_ GetIpInterfaceEntry(PMIB_IPINTERFACE_ROW row)
{
    if (!row || (row->Family != AF_INET && row->Family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a;
    DWORD error = row_adapter(s, &row->InterfaceLuid, row->InterfaceIndex, &a);
    if (!error && !IphFamilyEnabled(a, row->Family))
        error = ERROR_NOT_FOUND;
    if (!error)
        IphInterfaceRow(a, row->Family, row);
    IphRelease(s);
    return error;
}

void NETIOAPI_API_ InitializeIpInterfaceEntry(PMIB_IPINTERFACE_ROW row)
{
    memset(row, 0xFF, sizeof(*row));
    row->Family = AF_UNSPEC;
    row->InterfaceIndex = 0;
    row->InterfaceLuid.Value = 0;
    row->BaseReachableTime = 0;
    row->RetransmitTime = 0;
    row->PathMtuDiscoveryTimeout = 0;
    row->NlMtu = 0;
}

/* ------------------------------------------------------------------ addresses */

void IphUnicastRow(const IPH_SNAPSHOT *s, const IPH_ADDRESS *e, MIB_UNICASTIPADDRESS_ROW *row)
{
    const IPH_ADAPTER *a = IphAdapterByIndex(s, e->index);
    memset(row, 0, sizeof(*row));
    IphSockaddr(&row->Address, e->family, e->bytes, e->scope);
    if (a)
        row->InterfaceLuid = a->luid;
    row->InterfaceIndex = e->index;
    row->PrefixOrigin = e->prefix_origin;
    row->SuffixOrigin = e->suffix_origin;
    row->ValidLifetime = e->valid;
    row->PreferredLifetime = e->preferred;
    row->OnLinkPrefixLength = e->prefix;
    row->DadState = e->dad;
    row->ScopeId.Value = a ? IphZone(a, e->family, e->bytes) : 0;
    row->CreationTimeStamp = s->created;
}

DWORD NETIOAPI_API_ GetUnicastIpAddressTable(ADDRESS_FAMILY family, PMIB_UNICASTIPADDRESS_TABLE *table)
{
    if (!table || !IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->address_count; i++)
        n += IphFamilyWanted(family, s->addresses[i].family);
    PMIB_UNICASTIPADDRESS_TABLE t = new_table(n, sizeof(MIB_UNICASTIPADDRESS_ROW));
    if (t) {
        for (int pass = 0; pass < 2; pass++) {
            ADDRESS_FAMILY f = pass ? AF_INET6 : AF_INET;
            for (ULONG i = 0; IphFamilyWanted(family, f) && i < s->address_count; i++)
                if (s->addresses[i].family == f)
                    IphUnicastRow(s, &s->addresses[i], &t->Table[t->NumEntries++]);
        }
    }
    IphRelease(s);
    *table = t;
    return t ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

static BOOL same_address(const IPH_ADDRESS *e, const SOCKADDR_INET *sa)
{
    if (e->family != sa->si_family)
        return FALSE;
    return e->family == AF_INET ? !memcmp(e->bytes, &sa->Ipv4.sin_addr, 4) : !memcmp(e->bytes, &sa->Ipv6.sin6_addr, 16);
}

DWORD NETIOAPI_API_ GetUnicastIpAddressEntry(PMIB_UNICASTIPADDRESS_ROW row)
{
    if (!row || (row->Address.si_family != AF_INET && row->Address.si_family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a;
    DWORD error = row_adapter(s, &row->InterfaceLuid, row->InterfaceIndex, &a);
    if (!error) {
        error = ERROR_NOT_FOUND;
        for (ULONG i = 0; i < a->address_count; i++) {
            const IPH_ADDRESS *e = &s->addresses[a->first_address + i];
            if (same_address(e, &row->Address)) {
                IphUnicastRow(s, e, row);
                error = NO_ERROR;
                break;
            }
        }
    }
    IphRelease(s);
    return error;
}

void NETIOAPI_API_ InitializeUnicastIpAddressEntry(PMIB_UNICASTIPADDRESS_ROW row)
{
    memset(row, 0, sizeof(*row));
    row->OnLinkPrefixLength = 0xFF;
    row->PreferredLifetime = INFINITE_LIFETIME;
    row->ValidLifetime = INFINITE_LIFETIME;
    row->PrefixOrigin = IpPrefixOriginUnchanged;
    row->SuffixOrigin = IpSuffixOriginUnchanged;
}

static void group_row(const IPH_SNAPSHOT *s, const IPH_ADDRESS *m, MIB_MULTICASTIPADDRESS_ROW *row)
{
    const IPH_ADAPTER *a = IphAdapterByIndex(s, m->index);
    memset(row, 0, sizeof(*row));
    IphSockaddr(&row->Address, m->family, m->bytes, m->scope);
    if (a)
        row->InterfaceLuid = a->luid;
    row->InterfaceIndex = m->index;
    row->ScopeId.Value = m->scope;
}

DWORD NETIOAPI_API_ GetMulticastIpAddressTable(ADDRESS_FAMILY family, PMIB_MULTICASTIPADDRESS_TABLE *table)
{
    if (!table || !IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->multicast_count; i++)
        n += IphFamilyWanted(family, s->multicasts[i].family);
    PMIB_MULTICASTIPADDRESS_TABLE t = new_table(n, sizeof(MIB_MULTICASTIPADDRESS_ROW));
    if (t) {
        for (int pass = 0; pass < 2; pass++) {
            ADDRESS_FAMILY f = pass ? AF_INET6 : AF_INET;
            for (ULONG i = 0; IphFamilyWanted(family, f) && i < s->multicast_count; i++)
                if (s->multicasts[i].family == f)
                    group_row(s, &s->multicasts[i], &t->Table[t->NumEntries++]);
        }
    }
    IphRelease(s);
    *table = t;
    return t ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

DWORD NETIOAPI_API_ GetMulticastIpAddressEntry(PMIB_MULTICASTIPADDRESS_ROW row)
{
    if (!row || (row->Address.si_family != AF_INET && row->Address.si_family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a;
    DWORD error = row_adapter(s, &row->InterfaceLuid, row->InterfaceIndex, &a);
    if (!error) {
        error = ERROR_NOT_FOUND;
        for (ULONG i = 0; i < s->multicast_count; i++) {
            const IPH_ADDRESS *m = &s->multicasts[i];
            if (m->index == a->index && same_address(m, &row->Address)) {
                group_row(s, m, row);
                error = NO_ERROR;
                break;
            }
        }
    }
    IphRelease(s);
    return error;
}

/* no adapter has an anycast address. */
DWORD NETIOAPI_API_ GetAnycastIpAddressTable(ADDRESS_FAMILY family, PMIB_ANYCASTIPADDRESS_TABLE *table)
{
    if (!table || !IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    *table = new_table(0, sizeof(MIB_ANYCASTIPADDRESS_ROW));
    return *table ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

DWORD NETIOAPI_API_ GetAnycastIpAddressEntry(PMIB_ANYCASTIPADDRESS_ROW row)
{
    if (!row || (row->Address.si_family != AF_INET && row->Address.si_family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a;
    DWORD error = row_adapter(s, &row->InterfaceLuid, row->InterfaceIndex, &a);
    IphRelease(s);
    return error ? error : ERROR_NOT_FOUND;
}

/* ------------------------------------------------------------------ GetIpAddrTable */

static BOOL in_addr_table(const IPH_ADAPTER *a, const IPH_ADDRESS *e)
{
    return a->up && e->family == AF_INET && e->dad >= IpDadStateDeprecated;
}

DWORD WINAPI GetIpAddrTable(PMIB_IPADDRTABLE table, PULONG size, BOOL order)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    ULONG n = 0;
    for (ULONG i = 0; i < s->adapter_count; i++) {
        const IPH_ADAPTER *a = &s->adapters[i];
        for (ULONG k = 0; k < a->address_count; k++)
            n += in_addr_table(a, &s->addresses[a->first_address + k]);
    }
    DWORD error = IphCheckTable(n, sizeof(MIB_IPADDRROW), table, size);
    if (!error) {
        ULONG count = 0;
        for (ULONG i = 0; i < s->adapter_count; i++) {
            const IPH_ADAPTER *a = &s->adapters[i];
            BOOL first = TRUE;
            for (ULONG k = 0; k < a->address_count; k++) {
                const IPH_ADDRESS *e = &s->addresses[a->first_address + k];
                if (e->family != AF_INET)
                    continue;
                BOOL primary = first;
                first = FALSE;
                if (!in_addr_table(a, e))
                    continue;
                MIB_IPADDRROW *row = &table->table[count++];
                memset(row, 0, sizeof(*row));
                memcpy(&row->dwAddr, e->bytes, 4);
                row->dwIndex = a->index;
                row->dwMask = IphIpv4Mask(e->prefix);
                /* windows writes 1 here, not a broadcast address. */
                row->dwBCastAddr = 1;
                row->dwReasmSize = 0xFFFF;
                if (e->suffix_origin == IpSuffixOriginDhcp || e->suffix_origin == IpSuffixOriginRandom)
                    row->wType |= MIB_IPADDR_DYNAMIC;
                if (primary)
                    row->wType |= MIB_IPADDR_PRIMARY;
                if (e->dad == IpDadStateDeprecated)
                    row->wType |= MIB_IPADDR_DELETED;
            }
        }
        table->dwNumEntries = count;
        if (order) {
            for (ULONG i = 1; i < count; i++) {
                MIB_IPADDRROW row = table->table[i];
                ULONG j = i;
                for (; j && memcmp(&table->table[j - 1].dwAddr, &row.dwAddr, 4) > 0; j--)
                    table->table[j] = table->table[j - 1];
                table->table[j] = row;
            }
        }
        if (!count)
            error = ERROR_NO_DATA;
    }
    IphRelease(s);
    return error;
}

/* ------------------------------------------------------------------ neighbors */

/* the neighbor caches are the host's and out of reach, so the tables are empty. */
DWORD NETIOAPI_API_ GetIpNetTable2(ADDRESS_FAMILY family, PMIB_IPNET_TABLE2 *table)
{
    if (!table)
        return ERROR_INVALID_PARAMETER;
    *table = nullptr;
    if (!IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    *table = new_table(0, sizeof(MIB_IPNET_ROW2));
    return *table ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

ULONG WINAPI GetIpNetTable(PMIB_IPNETTABLE table, PULONG size, BOOL order)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD error = IphCheckTable(0, sizeof(MIB_IPNETROW), table, size);
    if (error)
        return error;
    table->dwNumEntries = 0;
    return ERROR_NO_DATA;
}

/*
 * there is no link layer to ask, so only the machine's own addresses resolve, to the hardware address of the adapter
 * that holds them. any other address is as unreachable as it would be after the requests went unanswered.
 */
DWORD NETIOAPI_API_ ResolveIpNetEntry2(PMIB_IPNET_ROW2 row, const SOCKADDR_INET *source)
{
    if (!row || !IphValidFamily(row->Address.si_family))
        return ERROR_INVALID_PARAMETER;
    const BYTE *wanted = row->Address.si_family == AF_INET ? (const BYTE *)&row->Address.Ipv4.sin_addr :
                                                              (const BYTE *)&row->Address.Ipv6.sin6_addr;
    SIZE_T length = row->Address.si_family == AF_INET ? 4 : 16;
    DWORD error = ERROR_BAD_NET_NAME;
    IPH_SNAPSHOT *s = IphAcquire();
    for (ULONG i = 0; i < s->address_count; i++) {
        const IPH_ADDRESS *address = &s->addresses[i];
        if (address->family != row->Address.si_family || memcmp(address->bytes, wanted, length))
            continue;
        const IPH_ADAPTER *adapter = IphAdapterByIndex(s, address->index);
        if (!adapter || !adapter->mac_length)
            break;
        row->InterfaceIndex = adapter->index;
        row->InterfaceLuid = adapter->luid;
        memcpy(row->PhysicalAddress, adapter->mac, adapter->mac_length);
        row->PhysicalAddressLength = adapter->mac_length;
        row->State = NlnsPermanent;
        row->Flags = 0;
        error = NO_ERROR;
        break;
    }
    IphRelease(s);
    return error;
}

DWORD WINAPI SendARP(ULONG destination, ULONG source, PVOID address, PULONG length)
{
    MIB_IPNET_ROW2 row = {};
    row.Address.Ipv4.sin_family = AF_INET;
    row.Address.Ipv4.sin_addr.S_un.S_addr = destination;
    DWORD error = ResolveIpNetEntry2(&row, nullptr);
    if (!error && row.PhysicalAddressLength > *length)
        error = ERROR_BUFFER_OVERFLOW;
    if (error) {
        *length = 0;
        return error;
    }
    memcpy(address, row.PhysicalAddress, row.PhysicalAddressLength);
    *length = row.PhysicalAddressLength;
    return NO_ERROR;
}

/*
 * the neighbor cache is the host's, so there is nothing of it to empty: an adapter that exists has an empty one.
 */
DWORD WINAPI FlushIpNetTable(DWORD index)
{
    IPH_SNAPSHOT *s = IphAcquire();
    BOOL exists = IphAdapterByIndex(s, index) != nullptr;
    IphRelease(s);
    return exists ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

/* ------------------------------------------------------------------ tcp and udp */

typedef struct {
    MIME_AFD_SOCKET_INFO *list;
    ULONG count;
} SOCKETS;

static void read_sockets(SOCKETS *out)
{
    out->list = nullptr;
    out->count = 0;
    for (ULONG max = 256; max <= 65536; max *= 4) {
        MIME_AFD_SOCKET_INFO *list = IphAlloc(max * sizeof(*list));
        if (!list)
            return;
        ULONG_PTR bytes = 0;
        NTSTATUS status = IphDeviceControl(IOCTL_AFD_SOCKETS, list, max * sizeof(*list), &bytes);
        if (NT_SUCCESS(status)) {
            out->list = list;
            out->count = (ULONG)(bytes / sizeof(*list));
            return;
        }
        IphFree(list);
        if (status != (NTSTATUS)0x80000005L)
            return;
    }
}

static DWORD tcp_state(const MIME_AFD_SOCKET_INFO *k)
{
    switch (k->State) {
    case MIME_AFD_STATE_LISTENING:
        return MIB_TCP_STATE_LISTEN;
    case MIME_AFD_STATE_CONNECTING:
        return MIB_TCP_STATE_SYN_SENT;
    case MIME_AFD_STATE_CONNECTED:
        if (k->Flags & 1)
            return MIB_TCP_STATE_CLOSE_WAIT;
        return (k->Flags & 2) ? MIB_TCP_STATE_FIN_WAIT2 : MIB_TCP_STATE_ESTAB;
    }
    return 0;
}

enum { ROW_BASIC, ROW_TCP2, ROW_PID, ROW_MODULE };

typedef struct {
    ULONG size;
    ULONG offset; /* offset of the first row. */
} ROW_SHAPE;

static ROW_SHAPE tcp_shape(ADDRESS_FAMILY family, int kind)
{
    if (family == AF_INET6)
        return kind == ROW_MODULE ? (ROW_SHAPE){sizeof(MIB_TCP6ROW_OWNER_MODULE), 8}
                                  : (ROW_SHAPE){sizeof(MIB_TCP6ROW_OWNER_PID), 4};
    switch (kind) {
    case ROW_TCP2:
        return (ROW_SHAPE){sizeof(MIB_TCPROW2), 4};
    case ROW_PID:
        return (ROW_SHAPE){sizeof(MIB_TCPROW_OWNER_PID), 4};
    case ROW_MODULE:
        return (ROW_SHAPE){sizeof(MIB_TCPROW_OWNER_MODULE), 8};
    }
    return (ROW_SHAPE){sizeof(MIB_TCPROW), 4};
}

static ROW_SHAPE udp_shape(ADDRESS_FAMILY family, int kind)
{
    if (family == AF_INET6)
        return kind == ROW_MODULE ? (ROW_SHAPE){sizeof(MIB_UDP6ROW_OWNER_MODULE), 8}
               : kind == ROW_PID  ? (ROW_SHAPE){sizeof(MIB_UDP6ROW_OWNER_PID), 4}
                                  : (ROW_SHAPE){sizeof(MIB_UDP6ROW), 4};
    return kind == ROW_MODULE ? (ROW_SHAPE){sizeof(MIB_UDPROW_OWNER_MODULE), 8}
           : kind == ROW_PID  ? (ROW_SHAPE){sizeof(MIB_UDPROW_OWNER_PID), 4}
                              : (ROW_SHAPE){sizeof(MIB_UDPROW), 4};
}

/* splits a socket address into the bytes, port and scope the tables store. */
static void endpoint(const MIME_AFD_ADDRESS *a, BYTE *bytes, DWORD *port, DWORD *scope)
{
    *port = (DWORD)a->Data[0] | (DWORD)a->Data[1] << 8;
    if (a->Family == AF_INET6) {
        memcpy(bytes, a->Data + 6, 16);
        if (scope)
            memcpy(scope, a->Data + 22, 4);
    } else {
        memcpy(bytes, a->Data + 2, 4);
        if (scope)
            *scope = 0;
    }
}

static void tcp_row(BYTE *row, ADDRESS_FAMILY family, int kind, const MIME_AFD_SOCKET_INFO *k)
{
    DWORD state = tcp_state(k);
    if (family == AF_INET6) {
        MIB_TCP6ROW_OWNER_PID *r = (MIB_TCP6ROW_OWNER_PID *)row;
        endpoint(&k->Local, r->ucLocalAddr, &r->dwLocalPort, &r->dwLocalScopeId);
        if (k->Remote.Family)
            endpoint(&k->Remote, r->ucRemoteAddr, &r->dwRemotePort, &r->dwRemoteScopeId);
        r->dwState = state;
        r->dwOwningPid = k->ProcessId;
        return;
    }
    MIB_TCPROW_OWNER_PID *r = (MIB_TCPROW_OWNER_PID *)row;
    r->dwState = state;
    endpoint(&k->Local, (BYTE *)&r->dwLocalAddr, &r->dwLocalPort, nullptr);
    if (k->Remote.Family)
        endpoint(&k->Remote, (BYTE *)&r->dwRemoteAddr, &r->dwRemotePort, nullptr);
    if (kind != ROW_BASIC)
        r->dwOwningPid = k->ProcessId;
}

static void udp_row(BYTE *row, ADDRESS_FAMILY family, int kind, const MIME_AFD_SOCKET_INFO *k)
{
    if (family == AF_INET6) {
        MIB_UDP6ROW_OWNER_PID *r = (MIB_UDP6ROW_OWNER_PID *)row;
        endpoint(&k->Local, r->ucLocalAddr, &r->dwLocalPort, &r->dwLocalScopeId);
        if (kind != ROW_BASIC)
            r->dwOwningPid = k->ProcessId;
        return;
    }
    MIB_UDPROW_OWNER_PID *r = (MIB_UDPROW_OWNER_PID *)row;
    endpoint(&k->Local, (BYTE *)&r->dwLocalAddr, &r->dwLocalPort, nullptr);
    if (kind != ROW_BASIC)
        r->dwOwningPid = k->ProcessId;
}

/* which: listeners 1, connections 2, both 3. */
static BOOL tcp_wanted(const MIME_AFD_SOCKET_INFO *k, ADDRESS_FAMILY family, int which)
{
    if (k->Type != 1 || k->Family != family)
        return FALSE;
    DWORD state = tcp_state(k);
    if (!state)
        return FALSE;
    return state == MIB_TCP_STATE_LISTEN ? (which & 1) != 0 : (which & 2) != 0;
}

static BOOL udp_wanted(const MIME_AFD_SOCKET_INFO *k, ADDRESS_FAMILY family)
{
    return k->Type == 2 && k->Family == family && (k->Bound || k->State == MIME_AFD_STATE_CONNECTED);
}

static int compare_rows(const BYTE *a, const BYTE *b, SIZE_T key)
{
    return memcmp(a, b, key);
}

/* fills a connection table sized as windows does, tcp when which is nonzero. */
static DWORD socket_table(void *table, PULONG size, BOOL order, ADDRESS_FAMILY family, int kind, int which)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    ROW_SHAPE shape = which ? tcp_shape(family, kind) : udp_shape(family, kind);
    SOCKETS sockets;
    read_sockets(&sockets);
    ULONG n = 0;
    for (ULONG i = 0; i < sockets.count; i++)
        n += which ? tcp_wanted(&sockets.list[i], family, which) : udp_wanted(&sockets.list[i], family);
    DWORD error;
    if (!table || *size < IphTableSize(n, shape.size, FALSE)) {
        *size = IphTableSize(n, shape.size, TRUE);
        error = ERROR_INSUFFICIENT_BUFFER;
    } else {
        BYTE *rows = (BYTE *)table + shape.offset;
        memset(rows, 0, n * shape.size);
        ULONG count = 0;
        for (ULONG i = 0; i < sockets.count; i++) {
            const MIME_AFD_SOCKET_INFO *k = &sockets.list[i];
            if (which ? !tcp_wanted(k, family, which) : !udp_wanted(k, family))
                continue;
            if (which)
                tcp_row(rows + count * shape.size, family, kind, k);
            else
                udp_row(rows + count * shape.size, family, kind, k);
            count++;
        }
        *(DWORD *)table = count;
        *size = IphTableSize(count, shape.size, FALSE);
        if (order) {
            /* sort by local then remote endpoint, skipping the state that leads a v4 tcp row. */
            SIZE_T skip = which && family == AF_INET ? 4 : 0;
            SIZE_T key = family == AF_INET6 ? (which ? 48 : 24) : (which ? 16 : 8);
            BYTE *tmp = IphAlloc(shape.size);
            for (ULONG i = 1; tmp && i < count; i++) {
                memcpy(tmp, rows + i * shape.size, shape.size);
                ULONG j = i;
                for (; j && compare_rows(rows + (j - 1) * shape.size + skip, tmp + skip, key) > 0; j--)
                    memcpy(rows + j * shape.size, rows + (j - 1) * shape.size, shape.size);
                memcpy(rows + j * shape.size, tmp, shape.size);
            }
            IphFree(tmp);
        }
        error = NO_ERROR;
    }
    IphFree(sockets.list);
    return error;
}

/*
 * the only change a row takes is to delete the connection. the connections are the host's sockets, out of reach of
 * the guest, so none can be taken down.
 */
DWORD WINAPI SetTcpEntry(PMIB_TCPROW row)
{
    if (!row || row->dwState != MIB_TCP_STATE_DELETE_TCB)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI GetTcpTable(PMIB_TCPTABLE table, PULONG size, BOOL order)
{
    return socket_table(table, size, order, AF_INET, ROW_BASIC, 3);
}

ULONG WINAPI GetTcpTable2(PMIB_TCPTABLE2 table, PULONG size, BOOL order)
{
    return socket_table(table, size, order, AF_INET, ROW_TCP2, 3);
}

DWORD WINAPI GetExtendedTcpTable(PVOID table, PDWORD size, BOOL order, ULONG family, TCP_TABLE_CLASS table_class,
                                 ULONG reserved)
{
    if ((family != AF_INET && family != AF_INET6) || (ULONG)table_class > TCP_TABLE_OWNER_MODULE_ALL)
        return ERROR_INVALID_PARAMETER;
    static const int kinds[] = {ROW_BASIC, ROW_BASIC, ROW_BASIC, ROW_PID, ROW_PID, ROW_PID,
                                ROW_MODULE, ROW_MODULE, ROW_MODULE};
    static const int which[] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
    if (family == AF_INET6 && kinds[table_class] == ROW_BASIC)
        return ERROR_INVALID_PARAMETER;
    return socket_table(table, size, order, (ADDRESS_FAMILY)family, kinds[table_class], which[table_class]);
}

ULONG WINAPI GetUdpTable(PMIB_UDPTABLE table, PULONG size, BOOL order)
{
    return socket_table(table, size, order, AF_INET, ROW_BASIC, 0);
}

DWORD WINAPI GetExtendedUdpTable(PVOID table, PDWORD size, BOOL order, ULONG family, UDP_TABLE_CLASS table_class,
                                 ULONG reserved)
{
    if ((family != AF_INET && family != AF_INET6) || (ULONG)table_class > UDP_TABLE_OWNER_MODULE)
        return ERROR_INVALID_PARAMETER;
    int kind = table_class == UDP_TABLE_OWNER_PID ? ROW_PID : table_class == UDP_TABLE_OWNER_MODULE ? ROW_MODULE
                                                                                                   : ROW_BASIC;
    return socket_table(table, size, order, (ADDRESS_FAMILY)family, kind, 0);
}
