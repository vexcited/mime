#include <mime/win32/ws2tcpip.h>
#include <mime/win32/winnls.h>
#include <mime/win32/iphlpapi.h>

#define NS_NLA 15
#define LUP_CONTAINERS 0x0002
#define LUP_RETURN_NAME 0x0010
#define LUP_RETURN_COMMENT 0x0080
#define LUP_RETURN_BLOB 0x0200
#define LUP_FLUSHPREVIOUS 0x2000
#define RESULT_IS_ADDED 0x0010
#define SIO_NSP_NOTIFY_CHANGE 0x88000019
#define NLA_RECORD 1048
#define NLA_CONNECTIVITY 3
#define NLA_INTERFACE 1
#define NLA_NETWORK_UNMANAGED 2
#define NLA_INTERNET_UNKNOWN 0
#define NLA_INTERNET_YES 2
#define LOOKUP_MAGIC 0x4B4F4F4C

typedef struct WSAQUERYSETW_ {
    DWORD dwSize;
    LPWSTR lpszServiceInstanceName;
    GUID *lpServiceClassId;
    DWORD dwVersion;
    DWORD ecHow;
    LPWSTR lpszComment;
    DWORD dwNameSpace;
    GUID *lpNSProviderId;
    LPWSTR lpszContext;
    DWORD dwNumberOfProtocols;
    struct {
        INT iAddressFamily;
        INT iProtocol;
    } *lpafpProtocols;
    LPWSTR lpszQueryString;
    DWORD dwNumberOfCsAddrs;
    void *lpcsaBuffer;
    DWORD dwOutputFlags;
    struct {
        ULONG cbSize;
        BYTE *pBlobData;
    } *lpBlob;
} WSAQUERYSETW_;

/* WSACOMPLETION: only the event of an overlapped wait is kept. */
typedef struct WSACOMPLETION_ {
    INT Type;
    LPWSAOVERLAPPED Overlapped;
} WSACOMPLETION_;
#define NSP_NOTIFY_EVENT 1

typedef struct NSP_ROUTINE_ {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    void *NSPCleanup, *NSPLookupServiceBegin, *NSPLookupServiceNext, *NSPLookupServiceEnd, *NSPSetService,
        *NSPInstallServiceClass, *NSPRemoveServiceClass, *NSPGetServiceClassInfo, *NSPIoctl;
} NSP_ROUTINE_;

typedef struct NLA_NETWORK {
    WCHAR Name[256];
    WCHAR Comment[256];
    DWORD Internet;
    DWORD InterfaceType;
    DWORD Speed;
    char Adapter[64];
} NLA_NETWORK;

typedef struct LOOKUP {
    DWORD Magic;
    DWORD Flags;
    WCHAR Filter[256];
    BOOL Filtered;
    BOOL Ended;
    NLA_NETWORK *Networks;
    DWORD Count;
    DWORD Next;
    HANDLE Waiting; /* the event of a wait for a change, set as the lookup ends. */
} LOOKUP;

static const GUID g_nla_class = {0x37e515, 0xb5c9, 0x4a43, {0xba, 0xda, 0x8b, 0x48, 0xa8, 0x7a, 0xd2, 0x39}};

static void *alloc_zero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void release(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static INT fail(int error)
{
    WSASetLastError(error);
    return SOCKET_ERROR;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/*
 * on windows the networks come from the NlaSvc service. here they are the adapters that are up with an address, one
 * network each, with the two records of 1048 bytes measured on windows 11 for mime's own ws2_32, see
 * guest/ws2_32/nsp.c.
 */
static void networks(LOOKUP *l)
{
    ULONG size = 16384;
    PIP_ADAPTER_ADDRESSES list = nullptr;
    ULONG error = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 4 && error == ERROR_BUFFER_OVERFLOW; tries++) {
        release(list);
        list = alloc_zero(size);
        if (!list)
            return;
        error = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                                                    GAA_FLAG_INCLUDE_GATEWAYS,
                                     nullptr, list, &size);
    }
    DWORD count = 0;
    if (!error) {
        for (PIP_ADAPTER_ADDRESSES a = list; a; a = a->Next)
            count += a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK && a->FirstUnicastAddress;
        l->Networks = count ? alloc_zero(count * sizeof(NLA_NETWORK)) : nullptr;
    }
    for (PIP_ADAPTER_ADDRESSES a = list; !error && l->Networks && a && l->Count < count; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !a->FirstUnicastAddress)
            continue;
        NLA_NETWORK *n = &l->Networks[l->Count++];
        lstrcpynW(n->Name, a->FriendlyName ? a->FriendlyName : L"Network", RTL_NUMBER_OF(n->Name));
        lstrcpynW(n->Comment, a->Description ? a->Description : L"", RTL_NUMBER_OF(n->Comment));
        n->Internet = a->FirstGatewayAddress ? NLA_INTERNET_YES : NLA_INTERNET_UNKNOWN;
        n->InterfaceType = a->IfType;
        n->Speed = a->TransmitLinkSpeed > 0xFFFFFFFFull ? 0xFFFFFFFF : (DWORD)a->TransmitLinkSpeed;
        if (a->AdapterName)
            lstrcpynA(n->Adapter, a->AdapterName, sizeof(n->Adapter));
    }
    release(list);
}

static INT WSAAPI cleanup(GUID *provider)
{
    return 0;
}

static INT WSAAPI lookup_begin(GUID *provider, WSAQUERYSETW_ *q, void *class_info, DWORD flags, LPHANDLE lookup)
{
    if (!q || !lookup)
        return fail(WSAEFAULT);
    if (q->dwNameSpace == NS_NLA && q->lpServiceClassId && memcmp(q->lpServiceClassId, &g_nla_class, sizeof(GUID)))
        return fail(WSASERVICE_NOT_FOUND);
    DWORD f = flags & 0xFFFFEFF3;
    if (f & LUP_CONTAINERS)
        return fail(WSANO_DATA);
    if (f & 0xA000)
        return fail(WSAEINVAL);
    if ((f & 0xFF0) == 0xFF0 || (f & 0xFF0) == 0)
        f = (f & 0xFFFFF00F) | (LUP_RETURN_NAME | LUP_RETURN_COMMENT | LUP_RETURN_BLOB);
    else if (f & 0xD60)
        return fail(WSAEINVAL);
    if (!(f & (LUP_RETURN_NAME | LUP_RETURN_COMMENT | LUP_RETURN_BLOB)))
        return fail(WSAEINVAL);
    if (q->lpszServiceInstanceName && !(f & LUP_RETURN_NAME))
        return fail(WSAEINVAL);
    for (DWORD i = 0; q->lpafpProtocols && i < q->dwNumberOfProtocols; i++) {
        if ((q->lpafpProtocols[i].iAddressFamily != AF_INET && q->lpafpProtocols[i].iAddressFamily != AF_UNSPEC) ||
            (q->lpafpProtocols[i].iProtocol != IPPROTO_TCP && q->lpafpProtocols[i].iProtocol != IPPROTO_UDP))
            return fail(WSAEINVAL);
    }
    if (q->lpafpProtocols && !q->dwNumberOfProtocols)
        return fail(WSAEINVAL);
    LOOKUP *l = alloc_zero(sizeof(*l));
    if (!l)
        return fail(WSA_NOT_ENOUGH_MEMORY);
    l->Magic = LOOKUP_MAGIC;
    l->Flags = f;
    if (q->lpszServiceInstanceName) {
        lstrcpynW(l->Filter, q->lpszServiceInstanceName, RTL_NUMBER_OF(l->Filter));
        l->Filtered = TRUE;
    }
    networks(l);
    *lookup = l;
    return 0;
}

static BOOL valid(HANDLE lookup)
{
    return lookup && ((LOOKUP *)lookup)->Magic == LOOKUP_MAGIC;
}

static SIZE_T round8(SIZE_T n)
{
    return (n + 7) & ~(SIZE_T)7;
}

static INT WSAAPI lookup_next(HANDLE lookup, DWORD flags, LPDWORD length, WSAQUERYSETW_ *q)
{
    if (!valid(lookup))
        return fail(WSA_INVALID_HANDLE);
    if (!length)
        return fail(WSAEFAULT);
    LOOKUP *l = lookup;
    BOOL flush = (flags & LUP_FLUSHPREVIOUS) != 0;
    DWORD f = flags & ~LUP_FLUSHPREVIOUS;
    if ((f & 0xFF0) == 0xFF0)
        f = (f & 0xFFFFF00F) | (LUP_RETURN_NAME | LUP_RETURN_COMMENT | LUP_RETURN_BLOB);
    if (f & 0xFFFFFD6F)
        return fail(WSAEINVAL);
    if (!f)
        f = l->Flags;
    /* the networks as the lookup began, past the flushed one and those of other names. */
    NLA_NETWORK *n = nullptr;
    while (!l->Ended && l->Next < l->Count) {
        NLA_NETWORK *c = &l->Networks[l->Next];
        if (flush) {
            flush = FALSE;
        } else if (!l->Filtered || !_wcsicmp(l->Filter, c->Name)) {
            n = c;
            break;
        }
        l->Next++;
    }
    if (!n) {
        l->Ended = TRUE;
        return fail(WSA_E_NO_MORE);
    }
    SIZE_T name = (f & LUP_RETURN_NAME) ? round8((wcslen(n->Name) + 1) * sizeof(WCHAR)) : 0;
    SIZE_T comment = (f & LUP_RETURN_COMMENT) ? round8((wcslen(n->Comment) + 1) * sizeof(WCHAR)) : 0;
    SIZE_T blob = (f & LUP_RETURN_BLOB) ? round8(sizeof(*q->lpBlob)) + 2 * NLA_RECORD : 0;
    SIZE_T need = sizeof(WSAQUERYSETW_) + name + comment + blob;
    BOOL fits = q && *length >= need;
    *length = (DWORD)need;
    if (!fits)
        return fail(WSAEFAULT);
    memset(q, 0, need);
    q->dwSize = (DWORD)need;
    BYTE *at = (BYTE *)(q + 1);
    if (name) {
        q->lpszServiceInstanceName = (LPWSTR)at;
        wcscpy(q->lpszServiceInstanceName, n->Name);
        at += name;
    }
    if (comment) {
        q->lpszComment = (LPWSTR)at;
        wcscpy(q->lpszComment, n->Comment);
        at += comment;
    }
    if (blob) {
        q->lpBlob = (void *)at;
        q->lpBlob->cbSize = 2 * NLA_RECORD;
        BYTE *data = at + round8(sizeof(*q->lpBlob));
        q->lpBlob->pBlobData = data;
        DWORD *connectivity = (DWORD *)data;
        connectivity[0] = NLA_CONNECTIVITY;
        connectivity[1] = NLA_RECORD;
        connectivity[2] = NLA_RECORD;
        connectivity[3] = NLA_NETWORK_UNMANAGED;
        connectivity[4] = n->Internet;
        DWORD *iface = (DWORD *)(data + NLA_RECORD);
        iface[0] = NLA_INTERFACE;
        iface[1] = NLA_RECORD;
        iface[3] = n->InterfaceType;
        iface[4] = n->Speed;
        strcpy((char *)&iface[5], n->Adapter);
    }
    q->dwOutputFlags = RESULT_IS_ADDED;
    l->Next++;
    return 0;
}

static INT WSAAPI lookup_end(HANDLE lookup)
{
    if (!valid(lookup))
        return fail(WSA_INVALID_HANDLE);
    LOOKUP *l = lookup;
    l->Magic = 0;
    if (l->Waiting)
        SetEvent(l->Waiting);
    release(l->Networks);
    release(l);
    return 0;
}

/* no service is registered or classed here. */
static INT WSAAPI set_service(GUID *provider, void *class_info, WSAQUERYSETW_ *q, int operation, DWORD flags)
{
    return fail(WSAEOPNOTSUPP);
}

static INT WSAAPI install_class(GUID *provider, void *class_info)
{
    return fail(WSAEOPNOTSUPP);
}

static INT WSAAPI remove_class(GUID *provider, GUID *class_id)
{
    return fail(WSAEOPNOTSUPP);
}

static INT WSAAPI class_info(GUID *provider, LPDWORD size, void *info)
{
    return fail(WSAEOPNOTSUPP);
}

/* a change of the networks is never told: an overlapped wait stays pending until its lookup ends. */
static INT WSAAPI ioctl(HANDLE lookup, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, LPDWORD returned,
                        WSACOMPLETION_ *completion, void *thread)
{
    if (!valid(lookup))
        return fail(WSA_INVALID_HANDLE);
    if (code != SIO_NSP_NOTIFY_CHANGE)
        return fail(WSAEOPNOTSUPP);
    if (!completion)
        return fail(WSAEWOULDBLOCK);
    if (completion->Type == NSP_NOTIFY_EVENT && completion->Overlapped)
        ((LOOKUP *)lookup)->Waiting = completion->Overlapped->hEvent;
    return fail(WSA_IO_PENDING);
}

INT WSAAPI NSPStartup(GUID *provider, NSP_ROUTINE_ *routines)
{
    if (!routines)
        return fail(WSAEFAULT);
    routines->cbSize = sizeof(*routines);
    routines->dwMajorVersion = 4;
    routines->dwMinorVersion = 4;
    routines->NSPCleanup = (void *)cleanup;
    routines->NSPLookupServiceBegin = (void *)lookup_begin;
    routines->NSPLookupServiceNext = (void *)lookup_next;
    routines->NSPLookupServiceEnd = (void *)lookup_end;
    routines->NSPSetService = (void *)set_service;
    routines->NSPInstallServiceClass = (void *)install_class;
    routines->NSPRemoveServiceClass = (void *)remove_class;
    routines->NSPGetServiceClassInfo = (void *)class_info;
    routines->NSPIoctl = (void *)ioctl;
    return 0;
}
