#ifndef __i386__
#include "mswsock_private.h"

#define NS_ALL 0
#define NS_DNS 12

#define LUP_RETURN_NAME 0x0010
#define LUP_RETURN_TYPE 0x0020
#define LUP_RETURN_ADDR 0x0100
#define LUP_RETURN_BLOB 0x0200

#define WSA_E_NO_MORE 10110L
#define WSASERVICE_NOT_FOUND 10108L

#define DNS_TYPE_A 0x0001
#define DNS_TYPE_PTR 0x000c
#define DNS_TYPE_AAAA 0x001c

#define NS_MAX_ADDRESSES 32

typedef struct {
    INT iAddressFamily;
    INT iProtocol;
} NS_AFPROTOCOLS;

typedef struct {
    SOCKET_ADDRESS LocalAddr;
    SOCKET_ADDRESS RemoteAddr;
    INT iSocketType;
    INT iProtocol;
} NS_CSADDR_INFO;

typedef struct {
    ULONG cbSize;
    BYTE *pBlobData;
} NS_BLOB;

typedef struct {
    DWORD dwSize;
    LPWSTR lpszServiceInstanceName;
    PGUID lpServiceClassId;
    PVOID lpVersion;
    LPWSTR lpszComment;
    DWORD dwNameSpace;
    PGUID lpNSProviderId;
    LPWSTR lpszContext;
    DWORD dwNumberOfProtocols;
    NS_AFPROTOCOLS *lpafpProtocols;
    LPWSTR lpszQueryString;
    DWORD dwNumberOfCsAddrs;
    NS_CSADDR_INFO *lpcsaBuffer;
    DWORD dwOutputFlags;
    NS_BLOB *lpBlob;
} NS_QUERYSET;

typedef struct {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    PVOID NSPCleanup;
    PVOID NSPLookupServiceBegin;
    PVOID NSPLookupServiceNext;
    PVOID NSPLookupServiceEnd;
    PVOID NSPSetService;
    PVOID NSPInstallServiceClass;
    PVOID NSPRemoveServiceClass;
    PVOID NSPGetServiceClassInfo;
    PVOID NSPIoctl;
} NS_ROUTINE;

/* the service classes ws2_32 asks of the dns namespace, see svcguid.h. */
#define SVCID_TCPIP(n) {(n), 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}
#define SVCID_DNS(type) {0x00090035, 0, (type), {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}
static const GUID svcid_hostname = SVCID_TCPIP(0x0002A800);
static const GUID svcid_addr_by_string = SVCID_TCPIP(0x0002A801);
static const GUID svcid_service_by_name = SVCID_TCPIP(0x0002A802);
static const GUID svcid_addr_by_name = SVCID_TCPIP(0x0002A803);
static const GUID svcid_dns_a = SVCID_DNS(DNS_TYPE_A);
static const GUID svcid_dns_ptr = SVCID_DNS(DNS_TYPE_PTR);
static const GUID svcid_dns_aaaa = SVCID_DNS(DNS_TYPE_AAAA);

typedef struct {
    GUID klass;
    char name[256];
    int family;
    int count;
    MIME_AFD_ADDRESS addresses[NS_MAX_ADDRESSES];
    DWORD flags;
    int error;
    int socket_type, protocol;
    USHORT port;
    BOOL service;
    WS_SERVICE_ENTRY entry;
    BOOL done;
} NS_LOOKUP;

static int fail(int error)
{
    SetLastError((DWORD)error);
    return SOCKET_ERROR;
}

static BOOL same_guid(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

static void copy_text(char *dst, SIZE_T size, const char *src)
{
    SIZE_T n = strlen(src);
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void add_v4(NS_LOOKUP *l, IN_ADDR a)
{
    if (l->count >= NS_MAX_ADDRESSES)
        return;
    SOCKADDR_IN *in = (SOCKADDR_IN *)&l->addresses[l->count++];
    memset(in, 0, sizeof(MIME_AFD_ADDRESS));
    in->sin_family = AF_INET;
    in->sin_addr = a;
}

static void add_v6(NS_LOOKUP *l, const IN6_ADDR *a, ULONG scope)
{
    if (l->count >= NS_MAX_ADDRESSES)
        return;
    SOCKADDR_IN6 *in6 = (SOCKADDR_IN6 *)&l->addresses[l->count++];
    memset(in6, 0, sizeof(MIME_AFD_ADDRESS));
    in6->sin6_family = AF_INET6;
    in6->sin6_addr = *a;
    in6->sin6_scope_id = scope;
}

static void add_loopback(NS_LOOKUP *l)
{
    if (l->family == AF_INET6) {
        IN6_ADDR a = {};
        a.u.Byte[15] = 1;
        add_v6(l, &a, 0);
    } else {
        IN_ADDR a = {.S_un.S_addr = htonl(INADDR_LOOPBACK)};
        add_v4(l, a);
    }
}

/* the addresses of the interfaces that are up, outside the loopback. */
static void add_interfaces(NS_LOOKUP *l)
{
    MIME_AFD_INTERFACE list[64];
    int n = WsInterfaces(list, 64);
    for (int i = 0; i < n; i++) {
        const MIME_AFD_INTERFACE *e = &list[i];
        if (e->Address.Family != l->family || (e->Flags & MIME_AFD_IF_LOOPBACK) ||
            (e->Flags & (MIME_AFD_IF_UP | MIME_AFD_IF_RUNNING)) != (MIME_AFD_IF_UP | MIME_AFD_IF_RUNNING))
            continue;
        if (l->family == AF_INET)
            add_v4(l, ((const SOCKADDR_IN *)&e->Address)->sin_addr);
        else
            add_v6(l, &((const SOCKADDR_IN6 *)&e->Address)->sin6_addr, ((const SOCKADDR_IN6 *)&e->Address)->sin6_scope_id);
    }
}

/* the addresses of a name: numeric, this machine's names, or the host's resolver. */
static int forward(NS_LOOKUP *l, const char *name)
{
    IN_ADDR a4;
    IN6_ADDR a6;
    ULONG scope;
    USHORT port;
    const char *end;
    copy_text(l->name, sizeof(l->name), name);
    if (l->family == AF_INET && WsParseIpv4(name, TRUE, &a4, &end) && !*end) {
        add_v4(l, a4);
        return 0;
    }
    if (l->family == AF_INET6 && WsParseIpv6Ex(name, &a6, &scope, &port)) {
        add_v6(l, &a6, scope);
        return 0;
    }
    /* windows answers this machine's name for localhost too, and the addresses of its interfaces for its names. */
    if (WsIsLocalName(name)) {
        WsFullHostName(l->name, sizeof(l->name));
        if (_stricmp(name, "localhost") && _stricmp(name, "localhost."))
            add_interfaces(l);
        if (!l->count)
            add_loopback(l);
        return 0;
    }
    MIME_AFD_ADDRESS found[NS_MAX_ADDRESSES];
    int n = WsResolve(name, l->family, found, NS_MAX_ADDRESSES);
    if (n < 0)
        return -n;
    for (int i = 0; i < n; i++) {
        if (found[i].Family != l->family)
            continue;
        if (l->family == AF_INET)
            add_v4(l, ((SOCKADDR_IN *)&found[i])->sin_addr);
        else
            add_v6(l, &((SOCKADDR_IN6 *)&found[i])->sin6_addr, ((SOCKADDR_IN6 *)&found[i])->sin6_scope_id);
    }
    return l->count ? 0 : WSANO_DATA;
}

/* the address of a reverse name, d.c.b.a.in-addr.arpa or the 32 nibbles of ip6.arpa. */
static BOOL parse_reverse(const char *name, NS_LOOKUP *l)
{
    SIZE_T n = strlen(name);
    if (n && name[n - 1] == '.')
        n--;
    static const char v4[] = ".in-addr.arpa", v6[] = ".ip6.arpa";
    if (n > sizeof(v4) - 1 && !_strnicmp(name + n - (sizeof(v4) - 1), v4, sizeof(v4) - 1)) {
        char text[32];
        SIZE_T len = n - (sizeof(v4) - 1);
        if (len >= sizeof(text))
            return FALSE;
        memcpy(text, name, len);
        text[len] = 0;
        IN_ADDR reversed;
        const char *end;
        if (!WsParseIpv4(text, TRUE, &reversed, &end) || *end)
            return FALSE;
        IN_ADDR a;
        for (int i = 0; i < 4; i++)
            ((BYTE *)&a)[i] = ((const BYTE *)&reversed)[3 - i];
        l->family = AF_INET;
        add_v4(l, a);
        return TRUE;
    }
    if (n == 64 + sizeof(v6) - 1 && !_strnicmp(name + 64, v6, sizeof(v6) - 1)) {
        IN6_ADDR a = {};
        for (int i = 0; i < 32; i++) {
            char c = name[2 * i];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0 || name[2 * i + 1] != '.')
                return FALSE;
            int nibble = 31 - i;
            a.u.Byte[nibble / 2] |= (BYTE)(nibble & 1 ? d : d << 4);
        }
        l->family = AF_INET6;
        add_v6(l, &a, 0);
        return TRUE;
    }
    return FALSE;
}

static BOOL is_loopback(const MIME_AFD_ADDRESS *a)
{
    if (a->Family == AF_INET)
        return WsIsLoopbackV4(&((const SOCKADDR_IN *)a)->sin_addr);
    return WsIsLoopbackV6(&((const SOCKADDR_IN6 *)a)->sin6_addr);
}

/* the host's resolver answers names only, so only the addresses of this machine have a name. */
static int reverse(NS_LOOKUP *l)
{
    if (!l->count || !is_loopback(&l->addresses[0]))
        return WSAHOST_NOT_FOUND;
    WsFullHostName(l->name, sizeof(l->name));
    return 0;
}

/* getservbyname and getservbyport ask for "name/protocol" and "port/protocol", the protocol may be empty. */
static int service(NS_LOOKUP *l, char *query)
{
    char *slash = strchr(query, '/');
    const char *proto = slash ? slash + 1 : "";
    if (slash)
        *slash = 0;
    BOOL number = *query != 0;
    ULONG port = 0;
    for (const char *c = query; *c; c++) {
        if (*c < '0' || *c > '9' || port > 0xFFFF)
            number = FALSE;
        else
            port = port * 10 + (ULONG)(*c - '0');
    }
    /* getservbyport writes the port as it was given, in network order. */
    if (!WsServiceEntry(number ? nullptr : query, ntohs((USHORT)port), proto, &l->entry))
        return WSATYPE_NOT_FOUND;
    copy_text(l->name, sizeof(l->name), l->entry.name);
    return 0;
}

/* a servent whose pointers are offsets from its start, like the hostent. */
static SIZE_T servent_size(const NS_LOOKUP *l)
{
    SIZE_T n = sizeof(struct servent) + sizeof(char *) + strlen(l->entry.name) + strlen(l->entry.proto) + 2;
    for (int i = 0; i < 3 && l->entry.aliases[i]; i++)
        n += sizeof(char *) + strlen(l->entry.aliases[i]) + 1;
    return n;
}

static void pack_servent(const NS_LOOKUP *l, BYTE *out)
{
    struct servent *s = (struct servent *)out;
    int count = 0;
    while (count < 3 && l->entry.aliases[count])
        count++;
    SIZE_T list = sizeof(*s), text = list + (count + 1) * sizeof(char *);
    s->s_aliases = (char **)list;
    s->s_port = (short)htons(l->entry.port);
    s->s_name = (char *)text;
    memcpy(out + text, l->entry.name, strlen(l->entry.name) + 1);
    text += strlen(l->entry.name) + 1;
    for (int i = 0; i < count; i++) {
        ((char **)(out + list))[i] = (char *)text;
        memcpy(out + text, l->entry.aliases[i], strlen(l->entry.aliases[i]) + 1);
        text += strlen(l->entry.aliases[i]) + 1;
    }
    ((char **)(out + list))[count] = nullptr;
    s->s_proto = (char *)text;
    memcpy(out + text, l->entry.proto, strlen(l->entry.proto) + 1);
}

static INT WSAAPI nsp_cleanup(PGUID provider)
{
    return 0;
}

static INT WSAAPI nsp_lookup_begin(PGUID provider, NS_QUERYSET *q, PVOID class_info, DWORD flags, LPHANDLE handle)
{
    if (!q || !handle || q->dwSize < sizeof(NS_QUERYSET) || !q->lpServiceClassId)
        return fail(WSAEINVAL);
    if (q->dwNameSpace != NS_ALL && q->dwNameSpace != NS_DNS)
        return fail(WSAEINVAL);
    NS_LOOKUP *l = WsAllocZero(sizeof(*l));
    if (!l)
        return fail(WSA_NOT_ENOUGH_MEMORY);
    l->klass = *q->lpServiceClassId;
    l->flags = flags;
    l->socket_type = SOCK_STREAM;
    l->protocol = IPPROTO_TCP;
    char name[256] = "";
    BOOL narrow = !q->lpszServiceInstanceName || WsNarrow(q->lpszServiceInstanceName, name, sizeof(name));
    /* the answers of windows: what a failed name gives shows at the first LookupServiceNext. */
    if (same_guid(&l->klass, &svcid_hostname)) {
        WsHostName(l->name, sizeof(l->name));
    } else if (same_guid(&l->klass, &svcid_addr_by_name) || same_guid(&l->klass, &svcid_dns_a) ||
               same_guid(&l->klass, &svcid_dns_aaaa)) {
        l->family = same_guid(&l->klass, &svcid_dns_aaaa) ? AF_INET6 : AF_INET;
        if (same_guid(&l->klass, &svcid_addr_by_name)) {
            l->socket_type = SOCK_DGRAM;
            l->protocol = IPPROTO_UDP;
        } else {
            l->port = 53;
            if (l->family == AF_INET6) {
                l->socket_type = SOCK_RAW;
                l->protocol = AF_INET6;
            }
        }
        l->error = !narrow ? WSAHOST_NOT_FOUND : *name ? forward(l, name) : WSAEINVAL;
    } else if (same_guid(&l->klass, &svcid_addr_by_string)) {
        IN_ADDR a;
        const char *end;
        l->family = AF_INET;
        if (WsParseIpv4(name, TRUE, &a, &end) && !*end) {
            add_v4(l, a);
            l->error = reverse(l);
        } else {
            l->error = WSAHOST_NOT_FOUND;
        }
    } else if (same_guid(&l->klass, &svcid_service_by_name)) {
        l->service = TRUE;
        l->error = service(l, name);
    } else if (same_guid(&l->klass, &svcid_dns_ptr)) {
        l->error = parse_reverse(name, l) ? reverse(l) : WSAHOST_NOT_FOUND;
        l->count = 0;
    } else {
        WsFree(l);
        return fail(WSASERVICE_NOT_FOUND);
    }
    *handle = l;
    return 0;
}

static SIZE_T align8(SIZE_T n)
{
    return (n + 7) & ~(SIZE_T)7;
}

static int address_length(int family)
{
    return family == AF_INET6 ? (int)sizeof(SOCKADDR_IN6) : (int)sizeof(SOCKADDR_IN);
}

static int host_address_length(const NS_LOOKUP *l)
{
    return !l->family ? 0 : l->family == AF_INET6 ? 16 : 4;
}

/* a hostent whose pointers are offsets from its start, which ws2_32 unpacks into its thread's copy. */
static SIZE_T hostent_size(const NS_LOOKUP *l)
{
    return sizeof(struct hostent) + sizeof(char *) + (l->count + 1) * sizeof(char *) +
           l->count * host_address_length(l) + strlen(l->name) + 1;
}

static void pack_hostent(const NS_LOOKUP *l, BYTE *out)
{
    int length = host_address_length(l);
    struct hostent *h = (struct hostent *)out;
    SIZE_T aliases = sizeof(*h), list = aliases + sizeof(char *), data = list + (l->count + 1) * sizeof(char *);
    SIZE_T name = data + l->count * length;
    h->h_addrtype = (short)l->family;
    h->h_length = (short)length;
    h->h_aliases = (char **)aliases;
    *(char **)(out + aliases) = nullptr;
    h->h_addr_list = (char **)list;
    for (int i = 0; i < l->count; i++) {
        const void *a = l->family == AF_INET6 ? (const void *)&((const SOCKADDR_IN6 *)&l->addresses[i])->sin6_addr
                                              : (const void *)&((const SOCKADDR_IN *)&l->addresses[i])->sin_addr;
        memcpy(out + data + i * length, a, length);
        ((char **)(out + list))[i] = (char *)(data + i * length);
    }
    ((char **)(out + list))[l->count] = nullptr;
    h->h_name = (char *)name;
    memcpy(out + name, l->name, strlen(l->name) + 1);
}

/* the LUP_RETURN flags of LookupServiceBegin hold for every LookupServiceNext, whose own flags add to them. */
static INT WSAAPI nsp_lookup_next(HANDLE handle, DWORD flags, LPDWORD length, NS_QUERYSET *results)
{
    NS_LOOKUP *l = handle;
    if (!l || !length)
        return fail(WSAEINVAL);
    if (l->error) {
        /* windows asks for room even for a name that failed, and tells the failure once it has it. */
        if (!results || *length < sizeof(NS_QUERYSET)) {
            *length = (DWORD)align8(sizeof(NS_QUERYSET));
            return fail(WSAEFAULT);
        }
        return fail(l->error);
    }
    if (l->done)
        return fail(WSA_E_NO_MORE);
    flags |= l->flags;
    int alen = address_length(l->family);
    SIZE_T text_size = align8((strlen(l->name) + 1) * sizeof(WCHAR));
    SIZE_T name_size = (flags & LUP_RETURN_NAME) ? text_size : 0;
    SIZE_T type_size = (flags & LUP_RETURN_TYPE) ? sizeof(GUID) : 0;
    SIZE_T addr_size = (flags & LUP_RETURN_ADDR) ? l->count * (sizeof(NS_CSADDR_INFO) + align8(2 * alen)) : 0;
    SIZE_T blob_data = l->service ? servent_size(l) : hostent_size(l);
    SIZE_T blob_size = (flags & LUP_RETURN_BLOB) ? sizeof(NS_BLOB) + align8(blob_data) : 0;
    SIZE_T need = align8(sizeof(NS_QUERYSET)) + name_size + text_size + type_size + addr_size + blob_size;
    if (!results || *length < need) {
        *length = (DWORD)need;
        return fail(WSAEFAULT);
    }
    memset(results, 0, need);
    BYTE *p = (BYTE *)results + align8(sizeof(NS_QUERYSET));
    results->dwSize = sizeof(NS_QUERYSET);
    results->dwNameSpace = NS_DNS;
    if (name_size) {
        results->lpszServiceInstanceName = (LPWSTR)p;
        WsWiden(l->name, results->lpszServiceInstanceName, name_size / sizeof(WCHAR));
        p += name_size;
    }
    results->lpszQueryString = (LPWSTR)p;
    WsWiden(l->name, results->lpszQueryString, text_size / sizeof(WCHAR));
    p += text_size;
    if (type_size) {
        memcpy(p, &l->klass, sizeof(GUID));
        results->lpServiceClassId = (PGUID)p;
        p += type_size;
    }
    if (addr_size) {
        NS_CSADDR_INFO *cs = (NS_CSADDR_INFO *)p;
        BYTE *addresses = p + l->count * sizeof(NS_CSADDR_INFO);
        for (int i = 0; i < l->count; i++) {
            BYTE *local = addresses + i * align8(2 * alen), *remote = local + alen;
            ((SOCKADDR *)local)->sa_family = (ADDRESS_FAMILY)l->family;
            memcpy(remote, &l->addresses[i], alen);
            ((SOCKADDR_IN *)remote)->sin_port = htons(l->port);
            cs[i].LocalAddr.lpSockaddr = (LPSOCKADDR)local;
            cs[i].LocalAddr.iSockaddrLength = alen;
            cs[i].RemoteAddr.lpSockaddr = (LPSOCKADDR)remote;
            cs[i].RemoteAddr.iSockaddrLength = alen;
            cs[i].iSocketType = l->socket_type;
            cs[i].iProtocol = l->protocol;
        }
        results->dwNumberOfCsAddrs = (DWORD)l->count;
        results->lpcsaBuffer = cs;
        p += addr_size;
    }
    if (blob_size) {
        NS_BLOB *blob = (NS_BLOB *)p;
        blob->cbSize = (ULONG)blob_data;
        blob->pBlobData = p + sizeof(NS_BLOB);
        if (l->service)
            pack_servent(l, blob->pBlobData);
        else
            pack_hostent(l, blob->pBlobData);
        results->lpBlob = blob;
    }
    l->done = TRUE;
    return 0;
}

static INT WSAAPI nsp_lookup_end(HANDLE handle)
{
    WsFree(handle);
    return 0;
}

static INT WSAAPI nsp_unsupported(void)
{
    return fail(WSAEOPNOTSUPP);
}

INT WSAAPI NSPStartup(PGUID provider, NS_ROUTINE *routines)
{
    if (!routines)
        return fail(WSAEFAULT);
    routines->cbSize = sizeof(*routines);
    routines->dwMajorVersion = 4;
    routines->dwMinorVersion = 4;
    routines->NSPCleanup = (PVOID)nsp_cleanup;
    routines->NSPLookupServiceBegin = (PVOID)nsp_lookup_begin;
    routines->NSPLookupServiceNext = (PVOID)nsp_lookup_next;
    routines->NSPLookupServiceEnd = (PVOID)nsp_lookup_end;
    routines->NSPSetService = (PVOID)nsp_unsupported;
    routines->NSPInstallServiceClass = (PVOID)nsp_unsupported;
    routines->NSPRemoveServiceClass = (PVOID)nsp_unsupported;
    routines->NSPGetServiceClassInfo = (PVOID)nsp_unsupported;
    routines->NSPIoctl = (PVOID)nsp_unsupported;
    return 0;
}
#endif
