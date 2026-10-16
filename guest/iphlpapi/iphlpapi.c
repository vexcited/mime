/*
 * iphlpapi: the ip helper. mime has no network adapter yet, so the machine
 * has exactly one interface, the software loopback, with ::1 and 127.0.0.1
 * and the routes windows creates for it. nothing ever changes, so change
 * notifications only deliver their initial notification.
 */
#define MIME_BUILD_IPHLPAPI
#include <mime/win32/iphlpapi.h>
#include <mime/win32/winnls.h>

#define LOOPBACK_INDEX 1
#define LOOPBACK_METRIC 75
#define LOOPBACK_SPEED 1073741824ull
#define ROUTE_METRIC 256

/* the adapter name is a guid that differs between machines; this one is fixed for mime. */
static constexpr char loopback_guid[] = "{2D6A9B3C-5B0E-11F0-8E5B-806E6F6E6963}";
static constexpr WCHAR loopback_description[] = L"Software Loopback Interface 1";
static constexpr WCHAR loopback_friendly[] = L"Loopback Pseudo-Interface 1";
static constexpr char loopback_name[] = "loopback_0";

static HANDLE heap(void)
{
    return GetProcessHeap();
}

static ULONG64 loopback_luid(void)
{
    NET_LUID luid = {};
    luid.Info.IfType = IF_TYPE_SOFTWARE_LOOPBACK;
    return luid.Value;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ addresses of the loopback */

typedef struct {
    int family;
    BYTE addr[16];
    UINT8 length;
} PREFIX;

static const PREFIX unicast[] = {
    {AF_INET6, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 128},
    {AF_INET, {127, 0, 0, 1}, 8},
};

/* the on link prefixes of the loopback, which are also its routes. */
static const PREFIX prefixes[] = {
    {AF_INET6, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 128},
    {AF_INET6, {0xFF}, 8},
    {AF_INET, {127, 0, 0, 0}, 8},
    {AF_INET, {127, 0, 0, 1}, 32},
    {AF_INET, {127, 255, 255, 255}, 32},
    {AF_INET, {224, 0, 0, 0}, 4},
    {AF_INET, {255, 255, 255, 255}, 32},
};

static BOOL family_wanted(ULONG family, int f)
{
    return family == AF_UNSPEC || (int)family == f;
}

static void fill_sockaddr(SOCKADDR_INET *sa, const PREFIX *p)
{
    memset(sa, 0, sizeof(*sa));
    if (p->family == AF_INET) {
        sa->Ipv4.sin_family = AF_INET;
        memcpy(&sa->Ipv4.sin_addr, p->addr, 4);
    } else {
        sa->Ipv6.sin6_family = AF_INET6;
        memcpy(&sa->Ipv6.sin6_addr, p->addr, 16);
    }
}

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
    if (p)
        memset(p, 0, size);
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

static SOCKADDR *take_sockaddr(LAYOUT *l, const PREFIX *p, INT *length)
{
    SIZE_T size = p->family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6);
    SOCKADDR_INET *sa = take(l, size, 8);
    if (sa) {
        SOCKADDR_INET full;
        fill_sockaddr(&full, p);
        memcpy(sa, &full, size);
    }
    *length = (INT)size;
    return (SOCKADDR *)sa;
}

static void layout_adapter(LAYOUT *l, ULONG family, ULONG flags)
{
    IP_ADAPTER_ADDRESSES *a = take(l, sizeof(*a), 8);
    if (a) {
        a->Length = sizeof(*a);
        a->IfIndex = LOOPBACK_INDEX;
        a->Flags = IP_ADAPTER_IPV4_ENABLED | IP_ADAPTER_IPV6_ENABLED;
        a->Mtu = 0xFFFFFFFF;
        a->IfType = IF_TYPE_SOFTWARE_LOOPBACK;
        a->OperStatus = IfOperStatusUp;
        a->Ipv6IfIndex = LOOPBACK_INDEX;
        /* every scope below global is the loopback itself. */
        for (int i = 1; i < 14; i++)
            a->ZoneIndices[i] = LOOPBACK_INDEX;
        a->TransmitLinkSpeed = LOOPBACK_SPEED;
        a->ReceiveLinkSpeed = LOOPBACK_SPEED;
        a->Ipv4Metric = LOOPBACK_METRIC;
        a->Ipv6Metric = LOOPBACK_METRIC;
        a->Luid.Value = loopback_luid();
        a->CompartmentId = NET_IF_COMPARTMENT_ID_PRIMARY;
        a->ConnectionType = NET_IF_CONNECTION_DEDICATED;
        a->TunnelType = TUNNEL_TYPE_NONE;
    }
    char *name = take(l, sizeof(loopback_guid), 1);
    if (name) {
        memcpy(name, loopback_guid, sizeof(loopback_guid));
        a->AdapterName = name;
    }
    PWSTR dns_suffix = take_string(l, L"");
    PWSTR description = take_string(l, loopback_description);
    PWSTR friendly = take_string(l, (flags & GAA_FLAG_SKIP_FRIENDLY_NAME) ? L"" : loopback_friendly);
    if (a) {
        a->DnsSuffix = dns_suffix;
        a->Description = description;
        a->FriendlyName = friendly;
    }
    if (!(flags & GAA_FLAG_SKIP_UNICAST)) {
        PIP_ADAPTER_UNICAST_ADDRESS_LH *next = a ? &a->FirstUnicastAddress : nullptr;
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(unicast); i++) {
            if (!family_wanted(family, unicast[i].family))
                continue;
            IP_ADAPTER_UNICAST_ADDRESS_LH *u = take(l, sizeof(*u), 8);
            INT length;
            SOCKADDR *sa = take_sockaddr(l, &unicast[i], &length);
            if (!u)
                continue;
            u->Length = sizeof(*u);
            u->Address.lpSockaddr = sa;
            u->Address.iSockaddrLength = length;
            u->PrefixOrigin = IpPrefixOriginWellKnown;
            u->SuffixOrigin = IpSuffixOriginWellKnown;
            u->DadState = IpDadStatePreferred;
            u->ValidLifetime = 0xFFFFFFFF;
            u->PreferredLifetime = 0xFFFFFFFF;
            u->LeaseLifetime = 0xFFFFFFFF;
            u->OnLinkPrefixLength = unicast[i].length;
            *next = u;
            next = &u->Next;
        }
    }
    if (flags & GAA_FLAG_INCLUDE_PREFIX) {
        PIP_ADAPTER_PREFIX_XP *next = a ? &a->FirstPrefix : nullptr;
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(prefixes); i++) {
            if (!family_wanted(family, prefixes[i].family))
                continue;
            IP_ADAPTER_PREFIX_XP *p = take(l, sizeof(*p), 8);
            INT length;
            SOCKADDR *sa = take_sockaddr(l, &prefixes[i], &length);
            if (!p)
                continue;
            p->Length = sizeof(*p);
            p->Address.lpSockaddr = sa;
            p->Address.iSockaddrLength = length;
            p->PrefixLength = prefixes[i].length;
            *next = p;
            next = &p->Next;
        }
    }
}

ULONG WINAPI GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES addresses,
                                  PULONG size)
{
    if (!size || reserved || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    LAYOUT measure = {nullptr, 0};
    layout_adapter(&measure, family, flags);
    if (!addresses || *size < measure.used) {
        *size = (ULONG)measure.used;
        return ERROR_BUFFER_OVERFLOW;
    }
    LAYOUT write = {(BYTE *)addresses, 0};
    layout_adapter(&write, family, flags);
    *size = (ULONG)write.used;
    return NO_ERROR;
}

/* the loopback is not an ipv4 adapter in the sense of GetAdaptersInfo, so there is none. */
ULONG WINAPI GetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NO_DATA;
}

DWORD WINAPI GetNumberOfInterfaces(PDWORD count)
{
    if (!count)
        return ERROR_INVALID_PARAMETER;
    *count = 1;
    return NO_ERROR;
}

/* ------------------------------------------------------------------ routes */

DWORD NETIOAPI_API_ GetIpForwardTable2(ADDRESS_FAMILY family, PMIB_IPFORWARD_TABLE2 *table)
{
    if (!table || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    ULONG n = 0;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(prefixes); i++)
        n += family_wanted(family, prefixes[i].family);
    SIZE_T bytes = offsetof(MIB_IPFORWARD_TABLE2, Table) + (n ? n : 1) * sizeof(MIB_IPFORWARD_ROW2);
    PMIB_IPFORWARD_TABLE2 t = HeapAlloc(heap(), HEAP_ZERO_MEMORY, bytes);
    if (!t)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(prefixes); i++) {
        if (!family_wanted(family, prefixes[i].family))
            continue;
        MIB_IPFORWARD_ROW2 *row = &t->Table[t->NumEntries++];
        row->InterfaceLuid.Value = loopback_luid();
        row->InterfaceIndex = LOOPBACK_INDEX;
        fill_sockaddr(&row->DestinationPrefix.Prefix, &prefixes[i]);
        row->DestinationPrefix.PrefixLength = prefixes[i].length;
        /* on link: the next hop is the unspecified address of the family. */
        row->NextHop.si_family = (ADDRESS_FAMILY)prefixes[i].family;
        row->ValidLifetime = 0xFFFFFFFF;
        row->PreferredLifetime = 0xFFFFFFFF;
        row->Metric = ROUTE_METRIC;
        row->Protocol = MIB_IPPROTO_LOCAL;
        row->Loopback = TRUE;
        row->Immortal = TRUE;
        row->Origin = NlroWellKnown;
    }
    *table = t;
    return NO_ERROR;
}

void NETIOAPI_API_ FreeMibTable(PVOID memory)
{
    if (memory)
        HeapFree(heap(), 0, memory);
}

/* ------------------------------------------------------------------ change notifications */

/* route and interface callbacks differ only in the row they would pass, never passed here. */
typedef void(NETIOAPI_API_ *CHANGE_CALLBACK)(PVOID context, PVOID row, MIB_NOTIFICATION_TYPE type);

typedef struct _NOTIFICATION {
    struct _NOTIFICATION *next;
    CHANGE_CALLBACK callback;
    PVOID context;
    HANDLE thread;
    DWORD thread_id;
} NOTIFICATION;

static SRWLOCK notifications_lock = SRWLOCK_INIT;
static NOTIFICATION *notifications;

static DWORD WINAPI initial_notification(LPVOID parameter)
{
    NOTIFICATION *n = parameter;
    n->callback(n->context, nullptr, MibInitialNotification);
    return 0;
}

static DWORD notify(ADDRESS_FAMILY family, CHANGE_CALLBACK callback, PVOID context, BOOLEAN initial, HANDLE *handle)
{
    if (!callback || !handle || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    NOTIFICATION *n = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*n));
    if (!n)
        return ERROR_NOT_ENOUGH_MEMORY;
    n->callback = callback;
    n->context = context;
    /* like windows, the initial notification arrives on another thread; the lock keeps a cancel
       from that thread waiting until its handle is known. */
    AcquireSRWLockExclusive(&notifications_lock);
    n->next = notifications;
    notifications = n;
    if (initial)
        n->thread = CreateThread(nullptr, 0, initial_notification, n, 0, &n->thread_id);
    ReleaseSRWLockExclusive(&notifications_lock);
    *handle = (HANDLE)n;
    return NO_ERROR;
}

DWORD NETIOAPI_API_ NotifyRouteChange2(ADDRESS_FAMILY family, PIPFORWARD_CHANGE_CALLBACK callback, PVOID context,
                                       BOOLEAN initial, HANDLE *handle)
{
    return notify(family, (CHANGE_CALLBACK)callback, context, initial, handle);
}

DWORD NETIOAPI_API_ NotifyIpInterfaceChange(ADDRESS_FAMILY family, PIPINTERFACE_CHANGE_CALLBACK callback,
                                            PVOID context, BOOLEAN initial, HANDLE *handle)
{
    return notify(family, (CHANGE_CALLBACK)callback, context, initial, handle);
}

DWORD NETIOAPI_API_ CancelMibChangeNotify2(HANDLE handle)
{
    NOTIFICATION *found = nullptr;
    AcquireSRWLockExclusive(&notifications_lock);
    for (NOTIFICATION **p = &notifications; *p; p = &(*p)->next) {
        if (*p == (NOTIFICATION *)handle) {
            found = *p;
            *p = found->next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&notifications_lock);
    if (!found)
        return ERROR_INVALID_HANDLE;
    /* waits for a running callback, unless it is the callback itself that cancels. */
    if (found->thread) {
        if (found->thread_id != GetCurrentThreadId())
            WaitForSingleObject(found->thread, INFINITE);
        CloseHandle(found->thread);
    }
    HeapFree(heap(), 0, found);
    return NO_ERROR;
}

/* ------------------------------------------------------------------ interface names and indexes */

static BOOL is_loopback_luid(const NET_LUID *luid)
{
    return luid && luid->Value == loopback_luid();
}

NET_IFINDEX NETIOAPI_API_ if_nametoindex(PCSTR name)
{
    NET_LUID luid;
    NET_IFINDEX index;
    if (!name || ConvertInterfaceNameToLuidA(name, &luid) || ConvertInterfaceLuidToIndex(&luid, &index))
        return 0;
    return index;
}

PCHAR NETIOAPI_API_ if_indextoname(NET_IFINDEX index, PCHAR name)
{
    NET_LUID luid;
    if (!name || ConvertInterfaceIndexToLuid(index, &luid) ||
        ConvertInterfaceLuidToNameA(&luid, name, IF_NAMESIZE))
        return nullptr;
    return name;
}

DWORD NETIOAPI_API_ ConvertInterfaceIndexToLuid(NET_IFINDEX index, PNET_LUID luid)
{
    if (!luid)
        return ERROR_INVALID_PARAMETER;
    if (index != LOOPBACK_INDEX) {
        luid->Value = 0;
        return ERROR_FILE_NOT_FOUND;
    }
    luid->Value = loopback_luid();
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToIndex(const NET_LUID *luid, PNET_IFINDEX index)
{
    if (!luid || !index)
        return ERROR_INVALID_PARAMETER;
    if (!is_loopback_luid(luid)) {
        *index = 0;
        return ERROR_FILE_NOT_FOUND;
    }
    *index = LOOPBACK_INDEX;
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameA(const NET_LUID *luid, PSTR name, SIZE_T length)
{
    if (!luid || !name || !is_loopback_luid(luid))
        return ERROR_INVALID_PARAMETER;
    if (length < sizeof(loopback_name))
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(name, loopback_name, sizeof(loopback_name));
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameW(const NET_LUID *luid, PWSTR name, SIZE_T length)
{
    if (!luid || !name || !is_loopback_luid(luid))
        return ERROR_INVALID_PARAMETER;
    if (length < sizeof(loopback_name))
        return ERROR_NOT_ENOUGH_MEMORY;
    for (SIZE_T i = 0; i < sizeof(loopback_name); i++)
        name[i] = (WCHAR)loopback_name[i];
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidA(const CHAR *name, PNET_LUID luid)
{
    if (!name || !luid)
        return ERROR_INVALID_PARAMETER;
    if (_stricmp(name, loopback_name)) {
        luid->Value = 0;
        return ERROR_INVALID_NAME;
    }
    luid->Value = loopback_luid();
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidW(const WCHAR *name, PNET_LUID luid)
{
    if (!name || !luid)
        return ERROR_INVALID_PARAMETER;
    char narrow[IF_NAMESIZE];
    SIZE_T i = 0;
    for (; name[i] && i + 1 < sizeof(narrow); i++)
        narrow[i] = name[i] < 0x80 ? (char)name[i] : '?';
    narrow[i] = 0;
    if (name[i])
        narrow[0] = 0;
    return ConvertInterfaceNameToLuidA(narrow, luid);
}
