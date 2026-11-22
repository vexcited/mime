/*
 * the proxy objects winhttp gives wininet: the list of the proxies that failed, the manager that answers which proxy a
 * url takes, and its answers. wininet reaches them through their virtual tables alone, so the slots and their order are
 * those of Windows. the manager of Windows asks the proxy discovery service first and its own static settings second;
 * here the first answer is worked out in the caller from the same settings the service reads.
 */
#include "winhttp_private.h"
#include <mime/ntrtl.h>

#ifndef ERROR_UNIDENTIFIED_ERROR
#define ERROR_UNIDENTIFIED_ERROR 317L
#endif
#ifndef ARRAYSIZE
#define ARRAYSIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#define HR_NOT_FOUND ((HRESULT)0x80070490L)
#define HR_PENDING ((HRESULT)0x000C03E5L)
#define HR_IO_PENDING ((HRESULT)0x800703E5L)
#define HR_CANCELLED ((HRESULT)0x800C2EF1L)

/* the schemes of a url and of a proxy, by the numbers the results carry. */
enum { SCHEME_NONE, SCHEME_FTP, SCHEME_GOPHER, SCHEME_HTTP, SCHEME_HTTPS, SCHEME_SOCKS = 8 };

/* what a manager was made for and what a request allows: detection and scripts, static settings. */
#define RESOLVE_AUTO 0x2
#define RESOLVE_STATIC 0x4
#define RESOLVE_NO_CLIENT 0x400

#define CONFIG_TYPE_STATIC 16
#define CONFIG_TYPE_STATIC_POLICY 32
#define SETTINGS_FROM_POLICY 0x10000

/* answers are kept an hour, as the service's are. */
#define CACHE_LIFETIME 3600000
#define CACHE_LIMIT 128

typedef struct {
    ULONGLONG Low, High;
} WH_IID;

static const WH_IID IidUnknown = {0, 0x46000000000000C0ULL};
static const WH_IID IidProxyList = {0x11E22EE17C3BCF70ULL, 0x669A0C200008C181ULL};
static const WH_IID IidCmProxyResolver = {0x4001D20FCAC9D2D8ULL, 0x281984BE010470A9ULL};
static const WH_IID IidProxyResolverEx = {0x4001D20FCAC9D2D9ULL, 0x281984BE010470A9ULL};
static const WH_IID IidProxyOperation = {0x4001D20FCAC9D2DAULL, 0x281984BE010470A9ULL};
static const WH_IID IidProxyResolver = {0x4B8428E9542EFEF0ULL, 0xBC0ABED208A9DF9AULL};
static const WH_IID IidClientProxyResolverEx = {0x4F2FFCB0D6A3127CULL, 0x76390627BACED59EULL};
static const WH_IID IidProxyResult = {0x41835301B62D5224ULL, 0x8F389C1A05B3DC91ULL};
static const WH_IID IidCmProxyResult = {0x4DDA31A0D8109AD1ULL, 0x4696656A10B31D83ULL};
static const WH_IID IidHostProxyResult = {0x4D17538987A32A4DULL, 0x07A5771883185881ULL};
static const WH_IID IidGetProxyForUrlCompletion = {0x48B7D1A3EAF85B1BULL, 0xAB9B9CF0F952969DULL};
static const WH_IID IidRefreshCompletion = {0x464B9C64FFECD0A8ULL, 0x2B811CA713662BAEULL};

static BOOL IsIid(const GUID *iid, const WH_IID *known)
{
    return !memcmp(iid, known, sizeof(*known));
}

/* an object seen through its virtual table. */
typedef struct {
    const void *const *Vtbl;
} WH_INTERFACE;

/* a url as wininet describes it. */
typedef struct {
    PCWSTR Url;
    PCWSTR Host;
    PCWSTR Path;
    DWORD Scheme;
    WORD Port;
} WH_RESOLVE_URL;

typedef struct {
    DWORD Values[12];
    WH_INTERFACE *Interface;
} WH_PROXY_OPTIONS;

typedef struct {
    DWORD Values[5];
} WH_SESSION_OPTIONS;

#define SLOT(object, index, type) ((type)((WH_INTERFACE *)(object))->Vtbl[index])

static ULONG InterfaceAddRef(void *object)
{
    return SLOT(object, 1, ULONG(WINAPI *)(void *))(object);
}

static ULONG InterfaceRelease(void *object)
{
    return SLOT(object, 2, ULONG(WINAPI *)(void *))(object);
}

static LONG volatile ProxyVersion;

/* a version is never 0. */
static DWORD NextVersion(void)
{
    LONG version;
    do
        version = InterlockedIncrement(&ProxyVersion);
    while (!version);
    return (DWORD)version;
}

/* the error of an export for the result of a method. */
static DWORD Win32FromResult(HRESULT hr)
{
    if ((hr & 0x1FFF0000) == 0xC0000)
        return (WORD)hr;
    if (hr >= 0)
        return ERROR_SUCCESS;
    if ((hr & 0x1FFF0000) == 0x70000)
        return (WORD)hr ? (WORD)hr : ERROR_UNIDENTIFIED_ERROR;
    if (!(hr & 0x10000000))
        return (DWORD)hr;
    DWORD error = RtlNtStatusToDosError(hr & 0xEFFFFFFF);
    return !error || error == ERROR_UNIDENTIFIED_ERROR ? (DWORD)hr : error;
}

static HRESULT ResultFromWin32(DWORD error)
{
    return (LONG)error > 0 ? (HRESULT)((error & 0xFFFF) | 0x80070000) : (HRESULT)error;
}

/* the proxies that failed, each until a time. */

typedef struct WH_LIST_ENTRY {
    struct WH_LIST_ENTRY *Next;
    PWSTR Name;
    WORD Port;
    BOOL Available;
    ULONGLONG Expires;
} WH_LIST_ENTRY;

typedef struct {
    const void *const *Vtbl;
    LONG volatile Refs;
    CRITICAL_SECTION Lock;
    WH_LIST_ENTRY *Entries;
    ULONGLONG GoodTime;
    ULONGLONG BadTime;
} WH_LIST;

static ULONGLONG Now(void)
{
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return ((ULONGLONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
}

static WH_LIST_ENTRY **ListFind(WH_LIST *list, PCWSTR name, WORD port)
{
    WH_LIST_ENTRY **link = &list->Entries;
    while (*link && ((*link)->Port != port || _wcsicmp((*link)->Name, name)))
        link = &(*link)->Next;
    return link;
}

static void ListRemove(WH_LIST_ENTRY **link)
{
    WH_LIST_ENTRY *entry = *link;
    *link = entry->Next;
    WhFree(entry->Name);
    WhFree(entry);
}

static HRESULT WINAPI ListQueryInterface(WH_LIST *list, const GUID *iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsIid(iid, &IidUnknown) && !IsIid(iid, &IidProxyList))
        return E_NOINTERFACE;
    InterlockedIncrement(&list->Refs);
    *out = list;
    return S_OK;
}

static ULONG WINAPI ListAddRef(WH_LIST *list)
{
    return (ULONG)InterlockedIncrement(&list->Refs);
}

static ULONG WINAPI ListRelease(WH_LIST *list)
{
    LONG refs = InterlockedDecrement(&list->Refs);
    if (!refs) {
        while (list->Entries)
            ListRemove(&list->Entries);
        DeleteCriticalSection(&list->Lock);
        WhFree(list);
    }
    return (ULONG)refs;
}

/* a proxy nobody reported is available, which the result tells apart from one that was reported so. */
static HRESULT WINAPI ListIsProxyAvailable(WH_LIST *list, PCWSTR name, WORD port, BOOL *available)
{
    HRESULT hr = S_FALSE;
    *available = TRUE;
    ULONGLONG now = Now();
    EnterCriticalSection(&list->Lock);
    WH_LIST_ENTRY **link = ListFind(list, name, port);
    if (*link) {
        hr = S_OK;
        if ((*link)->Expires > now)
            *available = (*link)->Available;
        else
            ListRemove(link);
    }
    LeaveCriticalSection(&list->Lock);
    return hr;
}

static HRESULT WINAPI ListReportProxyAvailability(WH_LIST *list, PCWSTR name, WORD port, BOOL available)
{
    HRESULT hr = S_OK;
    ULONGLONG expires = Now() + (available ? list->GoodTime : list->BadTime);
    EnterCriticalSection(&list->Lock);
    WH_LIST_ENTRY **link = ListFind(list, name, port);
    if (*link) {
        /* a list that keeps no good proxy forgets the one that works again. */
        if (available && !list->GoodTime) {
            ListRemove(link);
        } else {
            (*link)->Available = available;
            (*link)->Expires = expires;
        }
    } else if (!available || list->GoodTime) {
        WH_LIST_ENTRY *entry = WhAllocZero(sizeof(*entry));
        if (entry && (entry->Name = WhDupW(name))) {
            entry->Port = port;
            entry->Available = available;
            entry->Expires = expires;
            entry->Next = list->Entries;
            list->Entries = entry;
        } else {
            WhFree(entry);
            hr = E_OUTOFMEMORY;
        }
    }
    LeaveCriticalSection(&list->Lock);
    return hr;
}

static HRESULT WINAPI ListFlushProxyList(WH_LIST *list)
{
    EnterCriticalSection(&list->Lock);
    while (list->Entries)
        ListRemove(&list->Entries);
    LeaveCriticalSection(&list->Lock);
    return S_OK;
}

static const void *const ListVtbl[] = {
    ListQueryInterface, ListAddRef, ListRelease, ListIsProxyAvailable, ListReportProxyAvailability, ListFlushProxyList,
};

DWORD WINAPI WinHttpCreateProxyList(ULONGLONG good_time, ULONGLONG bad_time, void **out)
{
    if (!out)
        return ERROR_INVALID_PARAMETER;
    WH_LIST *list = WhAllocZero(sizeof(*list));
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;
    list->Vtbl = ListVtbl;
    list->Refs = 1;
    list->GoodTime = good_time;
    list->BadTime = bad_time;
    InitializeCriticalSection(&list->Lock);
    *out = list;
    return ERROR_SUCCESS;
}

/* static proxies: "http=one:80;https=https://two;three", and the hosts that go around them. */

typedef struct WH_SERVER {
    struct WH_SERVER *Next;
    DWORD Protocol;
    DWORD Scheme;
    PWSTR Host;
    WORD Port;
} WH_SERVER;

typedef struct {
    WH_SERVER *First;
    WH_SERVER *Last;
    PWSTR Raw;
} WH_SERVERS;

typedef struct WH_BYPASS {
    struct WH_BYPASS *Next;
    DWORD Scheme;
    PWSTR Name;
    DWORD Length;
    WORD Port;
    BOOL Local;
    BOOL Loopback;
} WH_BYPASS;

typedef struct {
    WH_BYPASS *First;
    PWSTR Raw;
} WH_BYPASSES;

static const struct {
    PCWSTR Name;
    DWORD Scheme;
} SchemeNames[] = {
    {L"ftp", SCHEME_FTP}, {L"gopher", SCHEME_GOPHER}, {L"http", SCHEME_HTTP}, {L"https", SCHEME_HTTPS}, {L"file", 5},
    {L"news", 6},         {L"mailto", 7},             {L"socks", SCHEME_SOCKS}, {L"javascript", 9},      {L"vbscript", 10},
};

/* the first name that begins with the text, so a prefix of a name is that name. */
static DWORD SchemeFromName(PCWSTR name, DWORD length)
{
    for (DWORD i = 0; i < ARRAYSIZE(SchemeNames); i++)
        if (!_wcsnicmp(name, SchemeNames[i].Name, length))
            return SchemeNames[i].Scheme;
    return (DWORD)-1;
}

static PCWSTR SchemeName(DWORD scheme)
{
    for (DWORD i = 0; i < ARRAYSIZE(SchemeNames); i++)
        if (SchemeNames[i].Scheme == scheme)
            return SchemeNames[i].Name;
    return nullptr;
}

static BOOL IsSpace(WCHAR c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

static void ServersFree(WH_SERVERS *servers)
{
    if (!servers)
        return;
    while (servers->First) {
        WH_SERVER *server = servers->First;
        servers->First = server->Next;
        WhFree(server->Host);
        WhFree(server);
    }
    WhFree(servers->Raw);
    WhFree(servers);
}

static BOOL ServersHave(const WH_SERVERS *servers, DWORD protocol)
{
    for (const WH_SERVER *server = servers->First; server; server = server->Next)
        if (server->Protocol == protocol)
            return TRUE;
    return FALSE;
}

static void ServersAdd(WH_SERVERS *servers, DWORD protocol, DWORD scheme, PCWSTR host, DWORD length, WORD port)
{
    WH_SERVER *server = WhAllocZero(sizeof(*server));
    if (!server || !(server->Host = WhDupWn(host, length))) {
        WhFree(server);
        return;
    }
    server->Protocol = protocol;
    server->Scheme = scheme;
    server->Port = port;
    if (servers->Last)
        servers->Last->Next = server;
    else
        servers->First = server;
    servers->Last = server;
}

/*
 * the list of proxies, read as Windows reads it: entries end at a space, a comma or a semicolon, a name before "=" is
 * the protocol the proxy serves, a name before "://" the scheme of the proxy, and an address in brackets keeps its
 * colons. a protocol that is already served, a protocol that is no scheme and a proxy that is neither http, https nor
 * ftp are skipped, while a port that is no number fails the whole list.
 */
static DWORD ServersParse(WH_SERVERS *servers, PCWSTR text)
{
    enum { NAME = 1, AFTER_NAME, HOST, PORT, FAILED = 6 };
    PCWSTR p = text, start = text, scheme = nullptr, host = nullptr;
    DWORD length = 0, protocol_length = 0, scheme_length = 0, host_length = 0, slashes = 0, state = NAME;
    WORD port = 0;
    BOOL bracket = FALSE, end = FALSE;
    for (;;) {
        WCHAR c = *p;
        PCWSTR at = p++;
        if (slashes == 1 && c != '/')
            return ERROR_INVALID_PARAMETER;
        if (c == 0 || c == ',' || c == ';' || IsSpace(c)) {
            if (c == 0)
                end = TRUE;
            if (host_length)
                length = host_length;
            if (bracket)
                return ERROR_INVALID_PARAMETER;
            if (length) {
                if (!host)
                    host = scheme ? scheme : start;
                DWORD protocol = protocol_length ? SchemeFromName(start, protocol_length) : SCHEME_NONE;
                DWORD proxy = scheme_length ? SchemeFromName(scheme, scheme_length) : SCHEME_NONE;
                if (protocol != (DWORD)-1 && (proxy <= SCHEME_FTP || proxy == SCHEME_HTTP || proxy == SCHEME_HTTPS) &&
                    !ServersHave(servers, protocol))
                    ServersAdd(servers, protocol, proxy, host, length, port);
            }
            if (end)
                return ERROR_SUCCESS;
            length = protocol_length = scheme_length = host_length = slashes = 0;
            start = p;
            scheme = host = nullptr;
            port = 0;
            state = NAME;
            bracket = FALSE;
            continue;
        }
        switch (c) {
        case '/':
            if (state != AFTER_NAME || slashes >= 2 || length)
                return ERROR_INVALID_PARAMETER;
            if (++slashes == 2) {
                state = HOST;
                host = p;
            }
            break;
        case ':':
            if (state == NAME) {
                if (*p == '/') {
                    scheme = start;
                    start = nullptr;
                    scheme_length = length;
                    protocol_length = 0;
                    state = AFTER_NAME;
                } else if (*p) {
                    host = start;
                    host_length = length;
                    state = PORT;
                } else {
                    return ERROR_INVALID_PARAMETER;
                }
                length = 0;
            } else if (state == AFTER_NAME) {
                if (*p == '/') {
                    scheme_length = length;
                } else if (*p) {
                    host = scheme;
                    host_length = length;
                    state = PORT;
                } else {
                    return ERROR_INVALID_PARAMETER;
                }
                length = 0;
            } else if (state == HOST) {
                if (bracket) {
                    length++;
                } else {
                    state = PORT;
                    host_length = length;
                    length = 0;
                }
            } else {
                return ERROR_INVALID_PARAMETER;
            }
            break;
        case '=':
            if (state != NAME || !length)
                return ERROR_INVALID_PARAMETER;
            protocol_length = length;
            length = 0;
            state = AFTER_NAME;
            scheme = p;
            break;
        case '[':
            if (state == NAME || state == AFTER_NAME) {
                state = HOST;
                host = at;
            } else if (state != HOST || length) {
                return ERROR_INVALID_PARAMETER;
            }
            length = 1;
            bracket = TRUE;
            break;
        case ']':
            if (state != HOST || !length)
                return ERROR_INVALID_PARAMETER;
            length++;
            bracket = FALSE;
            break;
        default:
            if (state != PORT)
                length++;
            else if (c >= '0' && c <= '9')
                port = (WORD)(port * 10 + (c - '0'));
            else
                return ERROR_INVALID_PARAMETER;
            break;
        }
    }
}

static DWORD ServersCreate(PCWSTR text, WH_SERVERS **out)
{
    *out = nullptr;
    WH_SERVERS *servers = WhAllocZero(sizeof(*servers));
    if (!servers || !(servers->Raw = WhDupW(text))) {
        WhFree(servers);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD error = ServersParse(servers, text);
    if (error) {
        ServersFree(servers);
        return error;
    }
    *out = servers;
    return ERROR_SUCCESS;
}

/*
 * the proxy of a scheme: the one named for it, or the one without a name for every scheme but socks, and failing both
 * the socks proxy. a proxy without a scheme is an http one, and one without a port takes the port of its scheme.
 */
static const WH_SERVER *ServersPick(const WH_SERVERS *servers, DWORD scheme, DWORD *proxy_scheme, WORD *port)
{
    const WH_SERVER *socks = nullptr, *found = nullptr;
    *proxy_scheme = SCHEME_SOCKS;
    for (const WH_SERVER *server = servers->First; server; server = server->Next) {
        if (server->Protocol == SCHEME_SOCKS)
            socks = server;
        if (server->Protocol == scheme || (server->Protocol == SCHEME_NONE && scheme != SCHEME_SOCKS)) {
            found = server;
            *proxy_scheme = server->Scheme;
            break;
        }
    }
    if (!found && !(found = socks))
        return nullptr;
    if (*proxy_scheme == SCHEME_NONE)
        *proxy_scheme = SCHEME_HTTP;
    *port = found->Port;
    if (!*port)
        *port = *proxy_scheme == SCHEME_FTP     ? 21
                : *proxy_scheme == SCHEME_HTTP  ? 80
                : *proxy_scheme == SCHEME_HTTPS ? 443
                : *proxy_scheme == SCHEME_SOCKS ? 1080
                                                : 0;
    return found;
}

static void BypassesFree(WH_BYPASSES *bypasses)
{
    if (!bypasses)
        return;
    while (bypasses->First) {
        WH_BYPASS *bypass = bypasses->First;
        bypasses->First = bypass->Next;
        WhFree(bypass->Name);
        WhFree(bypass);
    }
    WhFree(bypasses->Raw);
    WhFree(bypasses);
}

static BOOL BypassesHave(const WH_BYPASSES *bypasses, DWORD scheme, PCWSTR name, DWORD length, WORD port)
{
    BOOL local = !_wcsnicmp(L"<local>", name, length), loopback = !_wcsnicmp(L"<-loopback>", name, length);
    for (const WH_BYPASS *bypass = bypasses->First; bypass; bypass = bypass->Next) {
        if ((bypass->Scheme != scheme && bypass->Scheme) || (bypass->Port != port && bypass->Port))
            continue;
        if (bypass->Local ? local
                          : bypass->Loopback ? loopback : bypass->Length == length && !_wcsnicmp(bypass->Name, name, length))
            return TRUE;
    }
    return FALSE;
}

/* a name goes before the others and "<local>" after them all, which is the order they are tried in. */
static void BypassesAdd(WH_BYPASSES *bypasses, DWORD scheme, PCWSTR name, DWORD length, WORD port)
{
    if (BypassesHave(bypasses, scheme, name, length, port))
        return;
    WH_BYPASS *bypass = WhAllocZero(sizeof(*bypass));
    if (!bypass || !(bypass->Name = WhDupWn(name, length))) {
        WhFree(bypass);
        return;
    }
    CharLowerW(bypass->Name);
    bypass->Scheme = scheme;
    bypass->Length = length;
    bypass->Port = port;
    bypass->Local = !_wcsnicmp(L"<local>", name, length);
    bypass->Loopback = !bypass->Local && !_wcsnicmp(L"<-loopback>", name, length);
    if (bypass->Local) {
        WH_BYPASS **link = &bypasses->First;
        while (*link)
            link = &(*link)->Next;
        *link = bypass;
    } else {
        bypass->Next = bypasses->First;
        bypasses->First = bypass;
    }
}

/* the list of hosts, with the rules of the list of proxies but a scheme before "://" and a "*" for a host left out. */
static DWORD BypassesParse(WH_BYPASSES *bypasses, PCWSTR text)
{
    enum { NAME = 2, HOST, PORT, FAILED = 6 };
    PCWSTR p = text, start = text, host = nullptr;
    DWORD length = 0, scheme_length = 0, host_length = 0, slashes = 0, state = NAME;
    WORD port = 0;
    BOOL bracket = FALSE, end = FALSE;
    for (;;) {
        WCHAR c = *p;
        PCWSTR at = p++;
        if (slashes == 1 && c != '/')
            return ERROR_INVALID_PARAMETER;
        if (c == 0 || c == ',' || c == ';' || IsSpace(c)) {
            BOOL entry = TRUE;
            if (c == 0)
                end = TRUE;
            if (!host_length)
                host_length = length;
            if (host_length) {
                if (!host)
                    host = start;
            } else if (state == HOST || state == PORT) {
                host_length = 1;
                host = L"*";
            } else {
                entry = FALSE;
            }
            DWORD scheme = entry && scheme_length ? SchemeFromName(start, scheme_length) : SCHEME_NONE;
            if (entry && scheme != (DWORD)-1)
                BypassesAdd(bypasses, scheme, host, host_length, port);
            if (end)
                return ERROR_SUCCESS;
            length = scheme_length = host_length = slashes = 0;
            start = p;
            host = nullptr;
            port = 0;
            state = NAME;
            bracket = FALSE;
            continue;
        }
        switch (c) {
        case '/':
            if (state != NAME || slashes >= 2 || length)
                return ERROR_INVALID_PARAMETER;
            if (++slashes == 2) {
                state = HOST;
                host = p;
            }
            break;
        case ':':
            if (state == NAME) {
                if (*p == '/') {
                    scheme_length = length;
                } else if (*p) {
                    host = start;
                    host_length = length;
                    if (!length) {
                        host_length = 1;
                        host = L"*";
                    }
                    state = PORT;
                } else {
                    return ERROR_INVALID_PARAMETER;
                }
                length = 0;
            } else if (state == HOST) {
                if (bracket) {
                    length++;
                } else {
                    host_length = length;
                    state = PORT;
                    length = 0;
                }
            } else {
                return ERROR_INVALID_PARAMETER;
            }
            break;
        case '[':
            if (state == NAME) {
                state = HOST;
                host = at;
            } else if (state != HOST || length) {
                return ERROR_INVALID_PARAMETER;
            }
            length = 1;
            bracket = TRUE;
            break;
        case ']':
            if (state != HOST || !length)
                return ERROR_INVALID_PARAMETER;
            length++;
            bracket = FALSE;
            break;
        default:
            if (state != PORT)
                length++;
            else if (c >= '0' && c <= '9')
                port = (WORD)(port * 10 + (c - '0'));
            else
                return ERROR_INVALID_PARAMETER;
            break;
        }
    }
}

/* the proxies themselves are reached without a proxy. */
static DWORD BypassesCreate(PCWSTR text, const WH_SERVERS *servers, WH_BYPASSES **out)
{
    *out = nullptr;
    WH_BYPASSES *bypasses = WhAllocZero(sizeof(*bypasses));
    if (!bypasses || !(bypasses->Raw = WhDupW(text))) {
        WhFree(bypasses);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD error = BypassesParse(bypasses, text);
    if (error) {
        BypassesFree(bypasses);
        return error;
    }
    for (const WH_SERVER *server = servers->First; server; server = server->Next)
        BypassesAdd(bypasses, server->Scheme, server->Host, (DWORD)wcslen(server->Host), server->Port);
    *out = bypasses;
    return ERROR_SUCCESS;
}

static BOOL Glob(PCWSTR pattern, PCWSTR text, DWORD length)
{
    PCWSTR star = nullptr;
    DWORD resume = 0, i = 0;
    while (i < length) {
        WCHAR c = text[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (*pattern == '*') {
            star = ++pattern;
            resume = i;
        } else if (*pattern && *pattern == c) {
            pattern++;
            i++;
        } else if (star) {
            pattern = star;
            i = ++resume;
        } else {
            return FALSE;
        }
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern;
}

NTSYSAPI NTSTATUS NTAPI RtlIpv4StringToAddressExW(PCWSTR text, BOOLEAN strict, struct in_addr *address, PUSHORT port);
NTSYSAPI NTSTATUS NTAPI RtlIpv6StringToAddressExW(PCWSTR text, struct in6_addr *address, PULONG scope, PUSHORT port);

/* whether a host is an address, and whether it is this machine or a neighbour on the link. */
static void HostKind(PCWSTR host, BOOL *address, BOOL *loopback)
{
    *address = *loopback = FALSE;
    if (!_wcsicmp(host, L"localhost") || !_wcsicmp(host, L"loopback")) {
        *loopback = TRUE;
        return;
    }
    struct in_addr v4;
    struct in6_addr v6;
    USHORT port = 0;
    ULONG scope = 0;
    if (RtlIpv4StringToAddressExW(host, TRUE, &v4, &port) >= 0) {
        const BYTE *b = (const BYTE *)&v4;
        *address = TRUE;
        *loopback = (b[0] == 169 && b[1] == 254) || (b[0] == 127 && b[1] == 0 && b[2] == 0 && b[3] == 1);
    } else if (RtlIpv6StringToAddressExW(host, &v6, &scope, &port) >= 0) {
        static const BYTE one[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        const BYTE *b = (const BYTE *)&v6;
        *address = TRUE;
        *loopback = !memcmp(b, one, sizeof(one)) || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80);
    }
}

/*
 * whether a host goes around the proxies, and whether the answer should say it was bypassed. this machine goes around
 * them unless "<-loopback>" is listed, a name without a dot does when "<local>" is, and an address of this machine is
 * only said to be bypassed when a pattern names it.
 */
static BOOL HostBypassed(const WH_BYPASSES *bypasses, DWORD scheme, PCWSTR host, WORD port, BOOL *reported)
{
    BOOL address, loopback, listed = FALSE, named = FALSE, dot = FALSE;
    DWORD length = (DWORD)wcslen(host);
    HostKind(host, &address, &loopback);
    *reported = FALSE;
    listed = loopback;
    for (DWORD i = 0; i < length; i++) {
        if (host[i] == '.')
            dot = TRUE;
        if (host[i] < 0x20)
            return FALSE;
    }
    if (address)
        dot = TRUE;
    for (const WH_BYPASS *bypass = bypasses->First; bypass; bypass = bypass->Next) {
        if (bypass->Loopback) {
            if (loopback) {
                listed = FALSE;
                break;
            }
            continue;
        }
        if ((bypass->Scheme && bypass->Scheme != scheme) || (bypass->Port && bypass->Port != port))
            continue;
        if (!bypass->Local || loopback) {
            if (Glob(bypass->Name, host, length)) {
                listed = named = TRUE;
                break;
            }
        } else if (!dot) {
            listed = TRUE;
            break;
        }
    }
    *reported = listed && (!(loopback && address) || named);
    return listed;
}

/* "http=one;https=two" out of a list, with a proxy for every scheme given to http, https and ftp where they have none. */
static DWORD ServersUiString(const WH_SERVERS *servers, PWSTR *out)
{
    const WH_SERVER *http = nullptr, *https = nullptr, *ftp = nullptr, *socks = nullptr, *all = nullptr;
    for (const WH_SERVER *server = servers->First; server; server = server->Next) {
        if (server->Protocol == SCHEME_NONE)
            all = server;
        else if (server->Protocol == SCHEME_FTP)
            ftp = server;
        else if (server->Protocol == SCHEME_HTTP)
            http = server;
        else if (server->Protocol == SCHEME_HTTPS)
            https = server;
        else if (server->Protocol == SCHEME_SOCKS)
            socks = server;
    }
    WH_SERVER filled[3];
    if (all && (http || https || ftp || socks)) {
        filled[0] = filled[1] = filled[2] = *all;
        filled[0].Protocol = SCHEME_HTTP;
        filled[1].Protocol = SCHEME_HTTPS;
        filled[2].Protocol = SCHEME_FTP;
        if (!http)
            http = &filled[0];
        if (!https)
            https = &filled[1];
        if (!ftp)
            ftp = &filled[2];
        all = nullptr;
    }
    const WH_SERVER *order[] = {http, https, ftp, socks, all};
    SIZE_T capacity = 1;
    for (DWORD i = 0; i < ARRAYSIZE(order); i++)
        if (order[i])
            capacity += wcslen(order[i]->Host) + 32;
    PWSTR text = WhTaskAlloc(capacity * sizeof(WCHAR));
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR at = text;
    for (DWORD i = 0; i < ARRAYSIZE(order); i++) {
        const WH_SERVER *server = order[i];
        if (!server)
            continue;
        PCWSTR protocol = SchemeName(server->Protocol), scheme = SchemeName(server->Scheme);
        if (at != text)
            *at++ = ';';
        if (protocol)
            at += wsprintfW(at, L"%s=", protocol);
        if (scheme)
            at += wsprintfW(at, L"%s://", scheme);
        SIZE_T host = wcslen(server->Host);
        memcpy(at, server->Host, host * sizeof(WCHAR));
        at += host;
        if (server->Port)
            at += wsprintfW(at, L":%u", server->Port);
    }
    *at = 0;
    *out = text;
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpCreateUiCompatibleProxyString(PCWSTR proxy, PWSTR *text)
{
    if (!proxy || !text)
        return ERROR_INVALID_PARAMETER;
    *text = nullptr;
    WH_SERVERS *servers;
    DWORD error = ServersCreate(proxy, &servers);
    if (error)
        return error;
    error = ServersUiString(servers, text);
    ServersFree(servers);
    return error;
}

/*
 * the answer for a url: direct access or a proxy. the static settings of a manager answer with one of either. what
 * Windows gets from the service is a list to walk when a proxy fails, which also answers for the interface the
 * connection was chosen on.
 */

typedef struct {
    BOOL Proxy;
    BOOL Bypass;
    DWORD Scheme;
    PWSTR Host;
    WORD Port;
} WH_RESULT_ENTRY;

typedef struct {
    const void *const *Vtbl;
    LONG volatile Refs;
    BOOL Walked;
    DWORD Version;
    DWORD Type;
    DWORD Count;
    DWORD Current;
    WH_RESULT_ENTRY *Entries;
} WH_RESULT;

static const void *const *ResultVtbl(BOOL walked);

static void EntriesFree(WH_RESULT_ENTRY *entries, DWORD count)
{
    for (DWORD i = 0; entries && i < count; i++)
        WhFree(entries[i].Host);
    WhFree(entries);
}

static WH_RESULT_ENTRY *EntriesCopy(const WH_RESULT_ENTRY *entries, DWORD count)
{
    WH_RESULT_ENTRY *copy = WhAllocZero(count * sizeof(*copy));
    for (DWORD i = 0; copy && i < count; i++) {
        copy[i] = entries[i];
        if (entries[i].Host && !(copy[i].Host = WhDupW(entries[i].Host))) {
            EntriesFree(copy, i);
            return nullptr;
        }
    }
    return copy;
}

static HRESULT ResultCreate(BOOL walked, DWORD version, DWORD type, const WH_RESULT_ENTRY *entries, DWORD count,
                            WH_RESULT **out)
{
    *out = nullptr;
    WH_RESULT *result = WhAllocZero(sizeof(*result));
    if (!result || !(result->Entries = EntriesCopy(entries, count))) {
        WhFree(result);
        return E_OUTOFMEMORY;
    }
    result->Vtbl = ResultVtbl(walked);
    result->Refs = 1;
    result->Walked = walked;
    result->Version = version;
    result->Type = type;
    result->Count = count;
    *out = result;
    return S_OK;
}

static const WH_RESULT_ENTRY *ResultEntry(const WH_RESULT *result)
{
    return result->Current < result->Count ? &result->Entries[result->Current] : nullptr;
}

static HRESULT WINAPI ResultQueryInterface(WH_RESULT *result, const GUID *iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsIid(iid, &IidUnknown) && !IsIid(iid, &IidProxyResult) &&
        !(result->Walked && (IsIid(iid, &IidCmProxyResult) || IsIid(iid, &IidHostProxyResult))))
        return E_NOINTERFACE;
    InterlockedIncrement(&result->Refs);
    *out = result;
    return S_OK;
}

static ULONG WINAPI ResultAddRef(WH_RESULT *result)
{
    return (ULONG)InterlockedIncrement(&result->Refs);
}

static ULONG WINAPI ResultRelease(WH_RESULT *result)
{
    LONG refs = InterlockedDecrement(&result->Refs);
    if (!refs) {
        EntriesFree(result->Entries, result->Count);
        WhFree(result);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI ResultIsUseProxy(WH_RESULT *result, BOOL *use)
{
    const WH_RESULT_ENTRY *entry = ResultEntry(result);
    *use = FALSE;
    if (!entry)
        return HR_NOT_FOUND;
    *use = entry->Proxy;
    return S_OK;
}

static HRESULT WINAPI ResultIsDirect(WH_RESULT *result, BOOL *direct, BOOL *bypass)
{
    const WH_RESULT_ENTRY *entry = ResultEntry(result);
    *direct = *bypass = FALSE;
    if (!entry)
        return HR_NOT_FOUND;
    if (!entry->Proxy) {
        *direct = TRUE;
        *bypass = entry->Bypass;
    }
    return S_OK;
}

/* a static answer has nothing after it. */
static HRESULT WINAPI ResultSkipCurrentResult(WH_RESULT *result)
{
    if (!result->Walked || result->Current >= result->Count || ++result->Current >= result->Count)
        return HR_NOT_FOUND;
    return S_OK;
}

static HRESULT WINAPI ResultGetProxy(WH_RESULT *result, DWORD *scheme, PWSTR *host, WORD *port)
{
    const WH_RESULT_ENTRY *entry = ResultEntry(result);
    *scheme = 0;
    *host = nullptr;
    *port = 0;
    if (!entry || !entry->Proxy || !entry->Host)
        return HR_NOT_FOUND;
    DWORD error = WhTaskDupW(entry->Host, host);
    if (error)
        return ResultFromWin32(error);
    *scheme = entry->Scheme;
    *port = entry->Port;
    return S_OK;
}

/* only an answer of the service may be kept for the next request to the host. */
static HRESULT WINAPI ResultCanCacheResult(WH_RESULT *result, BOOL *cache)
{
    *cache = result->Walked;
    return S_OK;
}

static HRESULT WINAPI ResultGetVersion(WH_RESULT *result, DWORD *version)
{
    *version = result->Version;
    return S_OK;
}

/* a static answer is the name of its proxy, a walked one the text of a proxy script. */
static HRESULT WINAPI ResultGetRawProxyString(WH_RESULT *result, PWSTR *text)
{
    if (!text)
        return E_INVALIDARG;
    *text = nullptr;
    if (!result->Walked) {
        const WH_RESULT_ENTRY *entry = &result->Entries[0];
        return ResultFromWin32(WhTaskDupW(entry->Proxy && !entry->Bypass ? entry->Host : L"DIRECT", text));
    }
    SIZE_T capacity = 1;
    for (DWORD i = 0; i < result->Count; i++)
        capacity += (result->Entries[i].Host ? wcslen(result->Entries[i].Host) : 0) + 16;
    PWSTR raw = WhTaskAlloc(capacity * sizeof(WCHAR));
    if (!raw)
        return E_OUTOFMEMORY;
    PWSTR at = raw;
    for (DWORD i = 0; i < result->Count; i++) {
        const WH_RESULT_ENTRY *entry = &result->Entries[i];
        if (!entry->Proxy) {
            at += wsprintfW(at, L"DIRECT;");
            continue;
        }
        PCWSTR kind = entry->Scheme == SCHEME_HTTPS ? L"HTTPS " : entry->Scheme == SCHEME_SOCKS ? L"SOCKS " : L"PROXY ";
        SIZE_T host = wcslen(entry->Host);
        at += wsprintfW(at, L"%s", kind);
        memcpy(at, entry->Host, host * sizeof(WCHAR));
        at += host;
        at += entry->Port ? wsprintfW(at, L":%d;", entry->Port) : wsprintfW(at, L";");
    }
    *at = 0;
    *text = raw;
    return S_OK;
}

static HRESULT WINAPI ResultGetResultConfigType(WH_RESULT *result, DWORD *type)
{
    *type = result->Type;
    return S_OK;
}

/*
 * the answer as the entries WinHttpFreeProxyResult frees, from the entry it stands on to its last, which leaves it
 * past the last.
 */
static HRESULT WINAPI ResultConvertToProxyResult(WH_RESULT *result, WINHTTP_PROXY_RESULT_EX *out)
{
    memset(out, 0, sizeof(*out));
    DWORD count = result->Current < result->Count ? result->Count - result->Current : 0;
    if (!count)
        return HR_NOT_FOUND;
    WINHTTP_PROXY_RESULT_ENTRY *entries = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, count * sizeof(*entries));
    if (!entries)
        return E_OUTOFMEMORY;
    for (DWORD i = 0; i < count; i++) {
        const WH_RESULT_ENTRY *entry = &result->Entries[result->Current + i];
        entries[i].fProxy = entry->Proxy;
        entries[i].fBypass = entry->Proxy ? FALSE : entry->Bypass;
        if (!entry->Proxy)
            continue;
        SIZE_T size = (wcslen(entry->Host) + 1) * sizeof(WCHAR);
        if (!(entries[i].pwszProxy = GlobalAlloc(GMEM_FIXED, size))) {
            WINHTTP_PROXY_RESULT partial = {i, entries};
            WinHttpFreeProxyResult(&partial);
            return E_OUTOFMEMORY;
        }
        memcpy(entries[i].pwszProxy, entry->Host, size);
        entries[i].ProxyScheme = (INTERNET_SCHEME)entry->Scheme;
        entries[i].ProxyPort = entry->Port;
    }
    if (result->Walked)
        result->Current = result->Count;
    out->cEntries = count;
    out->pEntries = entries;
    return S_OK;
}

/* no answer here was chosen for an interface or holds a detection of the service. */

static HRESULT WINAPI ResultGetInterfaceIndexAffinity(WH_RESULT *result, DWORD *index)
{
    if (!index)
        return E_INVALIDARG;
    *index = 0;
    return S_OK;
}

static HRESULT WINAPI ResultGetProxyDetectionHandleInterface(WH_RESULT *result, void **handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    return S_OK;
}

static HRESULT WINAPI ResultSetProxyDetectionHandle(WH_RESULT *result, HANDLE handle)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI ResultGetSelectableConnectionInterfaceHandle(WH_RESULT *result, HANDLE *handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    return S_FALSE;
}

static HRESULT WINAPI ResultGetConnectionName(WH_RESULT *result, PWSTR *name)
{
    if (name)
        *name = nullptr;
    return S_FALSE;
}

static HRESULT WINAPI ResultGetAggregatedProxyConfigInfo(WH_RESULT *result, DWORD *type, DWORD *last, DWORD *flags,
                                                         BOOL *direct)
{
    if (last)
        *last = 0;
    if (flags)
        *flags = 0;
    if (direct)
        *direct = FALSE;
    if (!type)
        return E_INVALIDARG;
    *type = result->Type;
    if (last && result->Current >= result->Count)
        *last = result->Type;
    return S_OK;
}

static const void *const StaticResultVtbl[] = {
    ResultQueryInterface,    ResultAddRef,   ResultRelease,        ResultIsUseProxy, ResultIsDirect,
    ResultSkipCurrentResult, ResultGetProxy, ResultCanCacheResult, ResultGetVersion, ResultGetRawProxyString,
    ResultGetResultConfigType, ResultConvertToProxyResult,
};

static const void *const WalkedResultVtbl[] = {
    ResultQueryInterface,
    ResultAddRef,
    ResultRelease,
    ResultIsUseProxy,
    ResultIsDirect,
    ResultSkipCurrentResult,
    ResultGetProxy,
    ResultCanCacheResult,
    ResultGetVersion,
    ResultGetRawProxyString,
    ResultGetResultConfigType,
    ResultConvertToProxyResult,
    ResultGetInterfaceIndexAffinity,
    ResultGetProxyDetectionHandleInterface,
    ResultSetProxyDetectionHandle,
    ResultGetSelectableConnectionInterfaceHandle,
    ResultGetConnectionName,
    ResultGetAggregatedProxyConfigInfo,
};

static const void *const *ResultVtbl(BOOL walked)
{
    return walked ? WalkedResultVtbl : StaticResultVtbl;
}

DWORD WINAPI WinHttpCreateProxyResult(ULONG_PTR reserved, DWORD scheme, WORD port, PCWSTR proxy, BOOL bypassed,
                                      void **out)
{
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = nullptr;
    WH_RESULT_ENTRY entry = {0};
    if (proxy) {
        entry.Proxy = TRUE;
        entry.Scheme = scheme;
        entry.Host = (PWSTR)proxy;
        entry.Port = port;
    } else {
        entry.Bypass = bypassed;
    }
    WH_RESULT *result;
    HRESULT hr = ResultCreate(FALSE, 0, CONFIG_TYPE_STATIC, &entry, 1, &result);
    if (hr < 0)
        return Win32FromResult(hr);
    *out = result;
    return ERROR_SUCCESS;
}

/* which proxy a url takes among static settings: none for a host that goes around them. */
static void StaticAnswer(const WH_SERVERS *servers, const WH_BYPASSES *bypasses, const WH_RESOLVE_URL *url,
                         WH_RESULT_ENTRY *entry)
{
    memset(entry, 0, sizeof(*entry));
    if (!servers)
        return;
    if (bypasses && HostBypassed(bypasses, url->Scheme, url->Host, url->Port, &entry->Bypass))
        return;
    const WH_SERVER *server = ServersPick(servers, url->Scheme, &entry->Scheme, &entry->Port);
    if (!server) {
        entry->Scheme = 0;
        entry->Port = 0;
        return;
    }
    entry->Proxy = TRUE;
    entry->Host = server->Host;
}

/* the manager. */

typedef struct WH_CACHED {
    struct WH_CACHED *Next;
    PWSTR Host;
    DWORD Scheme;
    WORD Port;
    DWORD Count;
    WH_RESULT_ENTRY *Entries;
} WH_CACHED;

typedef struct {
    const void *const *Resolver;
    const void *const *Operations;
    const void *const *Client;
    LONG volatile Refs;
    CRITICAL_SECTION Lock;
    DWORD Flags;
    DWORD SessionFlags;
    WH_INTERFACE *List;
    /* what the last settings were, for the questions about them. */
    BOOL SettingsSet;
    BOOL Explicit;
    DWORD ExplicitVersion;
    DWORD SettingsFlags;
    PWSTR Connection;
    /* the part that asks the service on Windows. */
    BOOL OptionsSet;
    WH_PROXY_OPTIONS Options;
    WH_SESSION_OPTIONS SessionOptions;
    WH_PROXY_SETTINGS Pushed;
    BOOL PushedExplicit;
    DWORD AnswerVersion;
    DWORD Affinity;
    BOOL Aggregate;
    WH_CACHED *Cache;
    DWORD CacheCount;
    ULONGLONG CacheTick;
    BOOL DetectFailed;
    /* the static part. */
    WH_SERVERS *Servers;
    WH_BYPASSES *Bypasses;
    BOOL NoDirect;
    BOOL StaticExplicit;
    DWORD StaticVersion;
    DWORD StaticType;
    DWORD StaticOption;
} WH_MANAGER;

static WH_MANAGER *ManagerOf(void *self, DWORD index)
{
    return (WH_MANAGER *)((void **)self - index);
}

static void CacheFlush(WH_MANAGER *manager)
{
    while (manager->Cache) {
        WH_CACHED *cached = manager->Cache;
        manager->Cache = cached->Next;
        EntriesFree(cached->Entries, cached->Count);
        WhFree(cached->Host);
        WhFree(cached);
    }
    manager->CacheCount = 0;
    manager->CacheTick = GetTickCount64();
    manager->DetectFailed = FALSE;
}

static HRESULT WINAPI ManagerQueryInterface(WH_MANAGER *manager, const GUID *iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsIid(iid, &IidUnknown) || IsIid(iid, &IidCmProxyResolver) || IsIid(iid, &IidProxyResolver))
        *out = &manager->Resolver;
    else if (IsIid(iid, &IidProxyResolverEx))
        *out = &manager->Operations;
    else if (IsIid(iid, &IidClientProxyResolverEx))
        *out = &manager->Client;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&manager->Refs);
    return S_OK;
}

static ULONG WINAPI ManagerAddRef(WH_MANAGER *manager)
{
    return (ULONG)InterlockedIncrement(&manager->Refs);
}

static ULONG WINAPI ManagerRelease(WH_MANAGER *manager)
{
    LONG refs = InterlockedDecrement(&manager->Refs);
    if (!refs) {
        CacheFlush(manager);
        ServersFree(manager->Servers);
        BypassesFree(manager->Bypasses);
        WhFreeProxySettings(&manager->Pushed);
        WhTaskFree(manager->Connection);
        if (manager->Options.Interface)
            InterfaceRelease(manager->Options.Interface);
        if (manager->List)
            InterfaceRelease(manager->List);
        DeleteCriticalSection(&manager->Lock);
        WhFree(manager);
    }
    return (ULONG)refs;
}

/* the answer the service gives, from the entries a proxy script returned. */
static HRESULT AutoAnswer(const WH_RESOLVE_URL *url, BOOL detect, PCWSTR script, WH_RESULT_ENTRY **entries, DWORD *count)
{
    WINHTTP_AUTOPROXY_OPTIONS options = {0};
    WINHTTP_PROXY_RESULT found;
    if (detect) {
        options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
        options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    }
    if (script) {
        options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
        options.lpszAutoConfigUrl = script;
    }
    DWORD error = WhAutoProxy(url->Url, &options, &found);
    if (error)
        return ResultFromWin32(error);
    WH_RESULT_ENTRY *list = WhAllocZero((found.cEntries ? found.cEntries : 1) * sizeof(*list));
    HRESULT hr = list ? S_OK : E_OUTOFMEMORY;
    for (DWORD i = 0; hr >= 0 && i < found.cEntries; i++) {
        const WINHTTP_PROXY_RESULT_ENTRY *entry = &found.pEntries[i];
        if (!entry->fProxy || !entry->pwszProxy)
            continue;
        list[i].Proxy = TRUE;
        list[i].Scheme = entry->ProxyScheme == INTERNET_SCHEME_HTTPS   ? SCHEME_HTTPS
                         : entry->ProxyScheme == INTERNET_SCHEME_SOCKS ? SCHEME_SOCKS
                                                                       : SCHEME_HTTP;
        list[i].Port = entry->ProxyPort;
        if (!(list[i].Host = WhDupW(entry->pwszProxy)))
            hr = E_OUTOFMEMORY;
    }
    *count = found.cEntries ? found.cEntries : 1;
    WinHttpFreeProxyResult(&found);
    if (hr < 0) {
        EntriesFree(list, *count);
        return hr;
    }
    *entries = list;
    return S_OK;
}

/*
 * what the service answers for the settings of the user: the proxy of the registry, measured on Windows to win over
 * the proxy a caller pushed without making it explicit, around the hosts of its bypass list, and direct access
 * without one.
 */
static HRESULT RegistryAnswer(const WH_RESOLVE_URL *url, WH_RESULT_ENTRY **entries, DWORD *count)
{
    WH_PROXY_SETTINGS settings;
    WH_SERVERS *servers = nullptr;
    WH_BYPASSES *bypasses = nullptr;
    WH_RESULT_ENTRY entry = {0};
    if (!WhReadProxySettings(nullptr, FALSE, FALSE, nullptr, nullptr, &settings)) {
        if ((settings.Flags & WH_SETTINGS_PROXY) && settings.Proxy && *settings.Proxy &&
            !ServersCreate(settings.Proxy, &servers))
            BypassesCreate(settings.ProxyBypass ? settings.ProxyBypass : L"", servers, &bypasses);
        WhFreeProxySettings(&settings);
    }
    StaticAnswer(servers, bypasses, url, &entry);
    *entries = EntriesCopy(&entry, 1);
    *count = 1;
    ServersFree(servers);
    BypassesFree(bypasses);
    return *entries ? S_OK : E_OUTOFMEMORY;
}

static BOOL CacheLookup(WH_MANAGER *manager, const WH_RESOLVE_URL *url, WH_RESULT **result)
{
    if (GetTickCount64() - manager->CacheTick >= CACHE_LIFETIME)
        CacheFlush(manager);
    for (WH_CACHED *cached = manager->Cache; cached; cached = cached->Next)
        if (cached->Scheme == url->Scheme && cached->Port == url->Port && !_wcsicmp(cached->Host, url->Host))
            return ResultCreate(TRUE, manager->AnswerVersion, 0, cached->Entries, cached->Count, result) >= 0;
    return FALSE;
}

static void CacheStore(WH_MANAGER *manager, const WH_RESOLVE_URL *url, DWORD version, const WH_RESULT_ENTRY *entries,
                       DWORD count)
{
    if (version != manager->AnswerVersion || manager->CacheCount >= CACHE_LIMIT || !manager->Options.Values[1])
        return;
    WH_CACHED *cached = WhAllocZero(sizeof(*cached));
    if (!cached || !(cached->Host = WhDupW(url->Host)) || !(cached->Entries = EntriesCopy(entries, count))) {
        if (cached)
            WhFree(cached->Host);
        WhFree(cached);
        return;
    }
    cached->Scheme = url->Scheme;
    cached->Port = url->Port;
    cached->Count = count;
    cached->Next = manager->Cache;
    manager->Cache = cached;
    manager->CacheCount++;
}

typedef struct {
    WH_MANAGER *Manager;
    DWORD Flags;
    DWORD Low;
    DWORD Version;
    BOOL Detect;
    PWSTR Script;
    WH_RESOLVE_URL Url;
    WH_INTERFACE *Completion;
} WH_ANSWER_JOB;

static void JobFree(WH_ANSWER_JOB *job)
{
    WhFree((void *)job->Url.Url);
    WhFree((void *)job->Url.Host);
    WhFree((void *)job->Url.Path);
    WhFree(job->Script);
    if (job->Completion)
        InterfaceRelease(job->Completion);
    if (job->Manager)
        ManagerRelease(job->Manager);
    WhFree(job);
}

static HRESULT StaticStage(WH_MANAGER *manager, DWORD flags, const WH_RESOLVE_URL *url, void **out);

/* detection and the script first, then the settings of the user. a detection that found nothing is not tried again
   until the answers are flushed. */
static HRESULT ServiceAnswer(WH_ANSWER_JOB *job, void **out)
{
    WH_MANAGER *manager = job->Manager;
    WH_RESULT_ENTRY *entries = nullptr;
    DWORD count = 0;
    HRESULT hr = HR_NOT_FOUND;
    if (job->Detect || job->Script) {
        hr = AutoAnswer(&job->Url, job->Detect, job->Script, &entries, &count);
        if (hr < 0 && job->Detect && !job->Script) {
            EnterCriticalSection(&manager->Lock);
            if (job->Version == manager->AnswerVersion)
                manager->DetectFailed = TRUE;
            LeaveCriticalSection(&manager->Lock);
        }
    }
    if (hr < 0 && (job->Low & RESOLVE_STATIC))
        hr = RegistryAnswer(&job->Url, &entries, &count);
    if (hr < 0)
        return HR_NOT_FOUND;
    WH_RESULT *result;
    hr = ResultCreate(TRUE, job->Version, 0, entries, count, &result);
    if (hr >= 0) {
        EnterCriticalSection(&manager->Lock);
        CacheStore(manager, &job->Url, job->Version, entries, count);
        LeaveCriticalSection(&manager->Lock);
        *out = result;
    }
    EntriesFree(entries, count);
    return hr;
}

static DWORD WINAPI AnswerWorker(LPVOID parameter)
{
    WH_ANSWER_JOB *job = parameter;
    void *result = nullptr;
    HRESULT hr = ServiceAnswer(job, &result);
    if (hr < 0)
        hr = StaticStage(job->Manager, job->Flags, &job->Url, &result);
    SLOT(job->Completion, 3, HRESULT(WINAPI *)(void *, HRESULT, void *))(job->Completion, hr, result);
    if (result)
        InterfaceRelease(result);
    JobFree(job);
    return 0;
}

/*
 * the first answer, the service's on Windows. it serves detection and scripts for settings that ask for them, and the
 * settings of the user unless the settings of the manager were made explicit. with a completion the work that waits
 * for the network runs behind the caller, who is told the answer is pending.
 */
static HRESULT ClientStage(WH_MANAGER *manager, DWORD flags, const WH_RESOLVE_URL *url, WH_INTERFACE *completion,
                           void **out)
{
    if (flags & RESOLVE_NO_CLIENT)
        return HR_NOT_FOUND;
    EnterCriticalSection(&manager->Lock);
    DWORD low = flags & manager->Flags & 0xFF;
    if (manager->PushedExplicit)
        low &= ~RESOLVE_STATIC;
    BOOL detect = (low & RESOLVE_AUTO) && (manager->Pushed.Flags & WH_SETTINGS_AUTO_DETECT) && !manager->DetectFailed;
    PCWSTR script = (low & RESOLVE_AUTO) && (manager->Pushed.Flags & WH_SETTINGS_CONFIG_URL) &&
                            manager->Pushed.AutoconfigUrl && *manager->Pushed.AutoconfigUrl
                        ? manager->Pushed.AutoconfigUrl
                        : nullptr;
    if (!manager->OptionsSet || (!detect && !script && !(low & RESOLVE_STATIC))) {
        LeaveCriticalSection(&manager->Lock);
        return HR_NOT_FOUND;
    }
    WH_RESULT *cached;
    if (CacheLookup(manager, url, &cached)) {
        LeaveCriticalSection(&manager->Lock);
        *out = cached;
        return S_OK;
    }
    WH_ANSWER_JOB *job = WhAllocZero(sizeof(*job));
    if (job) {
        job->Flags = flags;
        job->Low = low;
        job->Version = manager->AnswerVersion;
        job->Detect = detect;
        job->Script = script ? WhDupW(script) : nullptr;
        job->Url.Url = WhDupW(url->Url);
        job->Url.Host = WhDupW(url->Host);
        job->Url.Path = url->Path ? WhDupW(url->Path) : nullptr;
        job->Url.Scheme = url->Scheme;
        job->Url.Port = url->Port;
    }
    LeaveCriticalSection(&manager->Lock);
    if (!job || !job->Url.Url || !job->Url.Host || (script && !job->Script)) {
        if (job)
            JobFree(job);
        return E_OUTOFMEMORY;
    }
    InterlockedIncrement(&manager->Refs);
    job->Manager = manager;
    if (completion && (detect || script)) {
        InterfaceAddRef(completion);
        job->Completion = completion;
        if (QueueUserWorkItem(AnswerWorker, job, WT_EXECUTELONGFUNCTION))
            return HR_PENDING;
        JobFree(job);
        return E_OUTOFMEMORY;
    }
    HRESULT hr = ServiceAnswer(job, out);
    JobFree(job);
    return hr;
}

/* the second answer: the settings made explicit on the manager, or direct access where they allow it. */
static HRESULT StaticStage(WH_MANAGER *manager, DWORD flags, const WH_RESOLVE_URL *url, void **out)
{
    if (!(flags & RESOLVE_STATIC))
        return HR_NOT_FOUND;
    WH_RESULT_ENTRY entry = {0};
    PWSTR host = nullptr;
    EnterCriticalSection(&manager->Lock);
    if (manager->StaticExplicit) {
        StaticAnswer(manager->Servers, manager->Bypasses, url, &entry);
        if (entry.Proxy && !(entry.Host = host = WhDupW(entry.Host))) {
            LeaveCriticalSection(&manager->Lock);
            return E_OUTOFMEMORY;
        }
    }
    BOOL none = manager->NoDirect && !entry.Proxy;
    DWORD version = manager->StaticVersion, type = manager->StaticType;
    LeaveCriticalSection(&manager->Lock);
    if (none)
        return HR_NOT_FOUND;
    WH_RESULT *result;
    HRESULT hr = ResultCreate(FALSE, version, type, &entry, 1, &result);
    WhFree(host);
    if (hr >= 0)
        *out = result;
    return hr;
}

static HRESULT WINAPI ManagerGetProxyForUrl(WH_MANAGER *manager, DWORD flags, const WH_RESOLVE_URL *url, DWORD *unused,
                                            void *context, WH_INTERFACE *completion, void **out)
{
    if (!url || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!url->Url || !url->Host)
        return E_INVALIDARG;
    HRESULT hr = ClientStage(manager, flags, url, completion, out);
    if (hr < 0)
        hr = StaticStage(manager, flags, url, out);
    return hr;
}

static BOOL SameString(PCWSTR a, PCWSTR b)
{
    if (!a || !b)
        return !a && !b;
    return !wcscmp(a, b);
}

/* the static part compares the proxy and its bypass list as they were written. */
static BOOL StaticChanged(const WH_MANAGER *manager, const WH_PROXY_SETTINGS *settings)
{
    if (!(settings->Flags & WH_SETTINGS_PROXY) || !settings->Proxy || !*settings->Proxy)
        return manager->Servers != nullptr;
    if (!manager->Servers || wcscmp(manager->Servers->Raw, settings->Proxy))
        return TRUE;
    if (!manager->Bypasses != !settings->ProxyBypass)
        return TRUE;
    return settings->ProxyBypass && wcscmp(manager->Bypasses->Raw, settings->ProxyBypass);
}

static BOOL PushedChanged(const WH_MANAGER *manager, const WH_PROXY_SETTINGS *settings)
{
    const WH_PROXY_SETTINGS *pushed = &manager->Pushed;
    if (pushed->Flags != settings->Flags || pushed->AutoDiscoveryFlags != settings->AutoDiscoveryFlags)
        return TRUE;
    if ((settings->Flags & WH_SETTINGS_CONFIG_URL) &&
        (!SameString(pushed->AutoconfigUrl, settings->AutoconfigUrl) ||
         !SameString(pushed->AutoconfigSecondaryUrl, settings->AutoconfigSecondaryUrl)))
        return TRUE;
    if ((settings->Flags & WH_SETTINGS_AUTO_DETECT) &&
        !SameString(pushed->LastKnownGoodAutoConfigUrl, settings->LastKnownGoodAutoConfigUrl))
        return TRUE;
    if (!(settings->Flags & WH_SETTINGS_PROXY) || !settings->Proxy || !*settings->Proxy)
        return (pushed->Flags & WH_SETTINGS_PROXY) && pushed->Proxy && *pushed->Proxy;
    return !(pushed->Flags & WH_SETTINGS_PROXY) || !SameString(pushed->Proxy, settings->Proxy) ||
           !SameString(pushed->ProxyBypass, settings->ProxyBypass);
}

static HRESULT WINAPI ManagerGetProxySettings(WH_MANAGER *manager, WH_PROXY_SETTINGS *out)
{
    WH_PROXY_SETTINGS settings = {0};
    EnterCriticalSection(&manager->Lock);
    HRESULT hr = manager->OptionsSet ? S_OK : E_INVALIDARG;
    DWORD error = hr < 0 ? 0 : WhTaskDupW(manager->Connection, &settings.ConnectionName);
    const WH_PROXY_SETTINGS *pushed = &manager->Pushed;
    if (hr >= 0 && !error) {
        WH_PROXY_SETTINGS copy;
        if (!(error = WhCopyProxySettings(pushed, &copy))) {
            settings.AutoconfigUrl = copy.AutoconfigUrl;
            settings.AutoconfigSecondaryUrl = copy.AutoconfigSecondaryUrl;
            settings.LastKnownGoodAutoConfigUrl = copy.LastKnownGoodAutoConfigUrl;
            settings.DetectedInterfaceIp = copy.DetectedInterfaceIp;
            settings.NetworkKeys = copy.NetworkKeys;
            copy.AutoconfigUrl = copy.AutoconfigSecondaryUrl = copy.LastKnownGoodAutoConfigUrl = nullptr;
            copy.DetectedInterfaceIp = nullptr;
            copy.NetworkKeys = nullptr;
            WhFreeProxySettings(&copy);
            settings.Flags = (manager->SettingsFlags & ~0xFu) | (pushed->Flags & 0xC);
            settings.AutoDiscoveryFlags = pushed->AutoDiscoveryFlags;
            settings.AutoconfigReloadDelayMins = pushed->AutoconfigReloadDelayMins;
            settings.LastKnownDetectTime = pushed->LastKnownDetectTime;
            settings.DetectedInterfaceIpCount = pushed->DetectedInterfaceIp ? pushed->DetectedInterfaceIpCount : 0;
            settings.NetworkKeyCount = pushed->NetworkKeys ? pushed->NetworkKeyCount : 0;
        }
    }
    if (hr >= 0 && !error) {
        if (!manager->NoDirect)
            settings.Flags |= WH_SETTINGS_DIRECT;
        if (manager->Servers) {
            settings.Flags |= WH_SETTINGS_PROXY;
            if (!(error = WhTaskDupW(manager->Servers->Raw, &settings.Proxy)) && manager->Bypasses)
                error = WhTaskDupW(manager->Bypasses->Raw, &settings.ProxyBypass);
        }
    }
    LeaveCriticalSection(&manager->Lock);
    if (hr >= 0 && error)
        hr = ResultFromWin32(error);
    if (hr < 0) {
        WhFreeProxySettings(&settings);
        return hr;
    }
    *out = settings;
    return S_OK;
}

/* a proxy list that does not parse fails the settings before anything of them is kept. */
static HRESULT PushSettings(WH_MANAGER *manager, const WH_PROXY_SETTINGS *settings, BOOL explicit, BOOL force,
                            BOOL *changed)
{
    if (!manager->OptionsSet)
        return E_INVALIDARG;
    if ((!force && settings->CurrentSettingsVersion != manager->Pushed.CurrentSettingsVersion) ||
        (manager->PushedExplicit && !explicit))
        return S_OK;
    manager->PushedExplicit = explicit;
    BOOL differs = TRUE;
    PCWSTR old_name = manager->Pushed.ConnectionName, new_name = settings->ConnectionName;
    if (old_name ? !new_name || _wcsicmp(old_name, new_name) : new_name != nullptr)
        differs = PushedChanged(manager, settings);
    if (differs)
        *changed = TRUE;
    if ((settings->Flags & WH_SETTINGS_PROXY) && settings->Proxy && *settings->Proxy) {
        WH_SERVERS *servers;
        DWORD error = ServersCreate(settings->Proxy, &servers);
        if (error)
            return ResultFromWin32(error);
        ServersFree(servers);
    }
    WH_PROXY_SETTINGS copy;
    DWORD error = WhCopyProxySettings(settings, &copy);
    if (error)
        return ResultFromWin32(error);
    WhFreeProxySettings(&manager->Pushed);
    manager->Pushed = copy;
    if (differs || explicit) {
        manager->AnswerVersion = NextVersion();
        CacheFlush(manager);
    }
    return S_OK;
}

static HRESULT StaticSettings(WH_MANAGER *manager, const WH_PROXY_SETTINGS *settings, BOOL explicit, BOOL *changed)
{
    if (manager->StaticExplicit && !explicit)
        return S_OK;
    BOOL proxy = (settings->Flags & WH_SETTINGS_PROXY) && settings->Proxy && *settings->Proxy;
    manager->NoDirect = !(settings->Flags & WH_SETTINGS_DIRECT);
    if (!StaticChanged(manager, settings))
        return S_OK;
    if (explicit)
        manager->StaticExplicit = TRUE;
    *changed = TRUE;
    manager->StaticVersion = NextVersion();
    if (settings->Flags & SETTINGS_FROM_POLICY)
        manager->StaticType = CONFIG_TYPE_STATIC_POLICY;
    ServersFree(manager->Servers);
    BypassesFree(manager->Bypasses);
    manager->Servers = nullptr;
    manager->Bypasses = nullptr;
    if (!proxy)
        return S_OK;
    WH_SERVERS *servers;
    WH_BYPASSES *bypasses;
    DWORD error = ServersCreate(settings->Proxy, &servers);
    if (error)
        return ResultFromWin32(error);
    if ((error = BypassesCreate(settings->ProxyBypass ? settings->ProxyBypass : L"", servers, &bypasses))) {
        ServersFree(servers);
        return ResultFromWin32(error);
    }
    manager->Servers = servers;
    manager->Bypasses = bypasses;
    return S_OK;
}

/*
 * settings a caller makes explicit are its own and are numbered by the manager. those it only passes on are the
 * settings of the user, which later ones replace until explicit ones were given.
 */
static HRESULT WINAPI ManagerSetProxySettings(WH_MANAGER *manager, const WH_PROXY_SETTINGS *settings, BOOL explicit,
                                              BOOL force, BOOL *changed)
{
    *changed = FALSE;
    WH_PROXY_SETTINGS local = *settings;
    BOOL any = FALSE;
    PWSTR name = nullptr;
    EnterCriticalSection(&manager->Lock);
    if (manager->Explicit && !explicit) {
        LeaveCriticalSection(&manager->Lock);
        return S_OK;
    }
    if (explicit) {
        if (!manager->Explicit)
            manager->ExplicitVersion = 0;
        local.CurrentSettingsVersion = ++manager->ExplicitVersion;
    }
    WhTaskFree(manager->Connection);
    manager->Connection = nullptr;
    HRESULT hr = ResultFromWin32(WhTaskDupW(local.ConnectionName, &name));
    if (hr >= 0)
        hr = PushSettings(manager, &local, explicit, force, &any);
    if (hr >= 0)
        hr = StaticSettings(manager, &local, explicit, &any);
    if (hr >= 0) {
        *changed = any;
        manager->SettingsFlags = local.Flags;
        manager->Connection = name;
        name = nullptr;
        manager->SettingsSet = TRUE;
        manager->Explicit = explicit;
    }
    LeaveCriticalSection(&manager->Lock);
    WhTaskFree(name);
    return hr;
}

/* nothing here waits for the service, so a refresh is done when it returns and its completion is never called. */
static HRESULT WINAPI ManagerRefreshProxySettings(WH_MANAGER *manager, BOOL flush, WH_INTERFACE *completion)
{
    EnterCriticalSection(&manager->Lock);
    if (flush) {
        manager->AnswerVersion = NextVersion();
        CacheFlush(manager);
    }
    LeaveCriticalSection(&manager->Lock);
    return S_OK;
}

static HRESULT WINAPI ManagerIsReadyForRequests(WH_MANAGER *manager, BOOL *ready)
{
    EnterCriticalSection(&manager->Lock);
    *ready = manager->SettingsSet;
    LeaveCriticalSection(&manager->Lock);
    return S_OK;
}

static HRESULT WINAPI ManagerIsSettingsChanged(WH_MANAGER *manager, const WH_PROXY_SETTINGS *settings, BOOL *changed)
{
    HRESULT hr = S_OK;
    *changed = FALSE;
    EnterCriticalSection(&manager->Lock);
    if (manager->SettingsSet && !manager->Explicit) {
        if (!SameString(manager->Connection, settings->ConnectionName))
            *changed = TRUE;
        else if (settings->CurrentSettingsVersion == manager->ExplicitVersion)
            *changed = FALSE;
        else if (settings->Flags != manager->SettingsFlags)
            *changed = TRUE;
        else if (!manager->OptionsSet)
            hr = E_UNEXPECTED;
        else
            *changed = PushedChanged(manager, settings) || StaticChanged(manager, settings);
    }
    LeaveCriticalSection(&manager->Lock);
    return hr;
}

static HRESULT WINAPI ManagerOnNetworkChange(WH_MANAGER *manager)
{
    if (manager->List)
        SLOT(manager->List, 5, HRESULT(WINAPI *)(void *))(manager->List);
    EnterCriticalSection(&manager->Lock);
    CacheFlush(manager);
    LeaveCriticalSection(&manager->Lock);
    return S_OK;
}

/* the proxy of an answer, for the list of the proxies that failed. socks proxies are not kept there. */
static HRESULT ResultProxy(void *result, PWSTR *host, WORD *port)
{
    BOOL use = FALSE;
    DWORD scheme = 0;
    *host = nullptr;
    HRESULT hr = SLOT(result, 3, HRESULT(WINAPI *)(void *, BOOL *))(result, &use);
    if (hr < 0 || !use)
        return hr < 0 ? hr : S_FALSE;
    hr = SLOT(result, 6, HRESULT(WINAPI *)(void *, DWORD *, PWSTR *, WORD *))(result, &scheme, host, port);
    if (hr >= 0 && scheme == SCHEME_SOCKS) {
        WhTaskFree(*host);
        *host = nullptr;
    }
    return hr;
}

static HRESULT ManagerReport(WH_MANAGER *manager, void *result, BOOL available)
{
    PWSTR host;
    WORD port = 0;
    HRESULT hr = ResultProxy(result, &host, &port);
    if (hr >= 0 && host && manager->List)
        hr = SLOT(manager->List, 4, HRESULT(WINAPI *)(void *, PCWSTR, WORD, BOOL))(manager->List, host, port, available);
    WhTaskFree(host);
    return hr < 0 ? hr : S_OK;
}

static HRESULT WINAPI ManagerOnSendRequestComplete(WH_MANAGER *manager, BOOL succeeded, const WH_RESOLVE_URL *url,
                                                   void *result)
{
    if (!result)
        return E_INVALIDARG;
    return succeeded ? ManagerReport(manager, result, TRUE) : S_OK;
}

static HRESULT WINAPI ManagerOnProxyFailure(WH_MANAGER *manager, void *result)
{
    return ManagerReport(manager, result, FALSE);
}

static HRESULT WINAPI ManagerIsProxyFiltered(WH_MANAGER *manager, void *result, BOOL *filtered)
{
    PWSTR host;
    WORD port = 0;
    BOOL available = TRUE;
    *filtered = FALSE;
    HRESULT hr = ResultProxy(result, &host, &port);
    if (hr >= 0 && host && manager->List)
        hr = SLOT(manager->List, 3, HRESULT(WINAPI *)(void *, PCWSTR, WORD, BOOL *))(manager->List, host, port, &available);
    WhTaskFree(host);
    if (hr < 0)
        return hr;
    *filtered = !available;
    return S_OK;
}

/* options are given once. */
static HRESULT WINAPI ManagerSetOptions(WH_MANAGER *manager, const WH_PROXY_OPTIONS *options)
{
    if (!options)
        return E_INVALIDARG;
    HRESULT hr = E_INVALIDARG;
    EnterCriticalSection(&manager->Lock);
    if (!manager->OptionsSet) {
        manager->Options = *options;
        if (options->Interface)
            InterfaceAddRef(options->Interface);
        manager->StaticOption = options->Values[11];
        manager->OptionsSet = TRUE;
        hr = S_OK;
    }
    LeaveCriticalSection(&manager->Lock);
    return hr;
}

static HRESULT WINAPI ManagerFlushCaches(WH_MANAGER *manager, DWORD which)
{
    EnterCriticalSection(&manager->Lock);
    if (manager->OptionsSet)
        CacheFlush(manager);
    LeaveCriticalSection(&manager->Lock);
    return S_OK;
}

static HRESULT WINAPI ManagerCancelAll(WH_MANAGER *manager)
{
    return S_OK;
}

static HRESULT WINAPI ManagerSetInterfaceIndexAffinity(WH_MANAGER *manager, DWORD index)
{
    EnterCriticalSection(&manager->Lock);
    if (manager->Affinity != index) {
        manager->Affinity = index;
        CacheFlush(manager);
    }
    LeaveCriticalSection(&manager->Lock);
    return S_OK;
}

static const void *const ManagerVtbl[] = {
    ManagerQueryInterface,
    ManagerAddRef,
    ManagerRelease,
    ManagerGetProxyForUrl,
    ManagerGetProxySettings,
    ManagerSetProxySettings,
    ManagerRefreshProxySettings,
    ManagerIsReadyForRequests,
    ManagerIsSettingsChanged,
    ManagerOnNetworkChange,
    ManagerOnSendRequestComplete,
    ManagerOnProxyFailure,
    ManagerIsProxyFiltered,
    ManagerSetOptions,
    ManagerFlushCaches,
    ManagerCancelAll,
    ManagerSetInterfaceIndexAffinity,
};

/* one resolution that can be cancelled before it starts. */

typedef struct {
    const void *const *Vtbl;
    LONG volatile Refs;
    WH_MANAGER *Manager;
    BOOL Cancelled;
} WH_OPERATION;

static HRESULT WINAPI OperationQueryInterface(WH_OPERATION *operation, const GUID *iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsIid(iid, &IidUnknown) && !IsIid(iid, &IidProxyOperation))
        return E_NOINTERFACE;
    InterlockedIncrement(&operation->Refs);
    *out = operation;
    return S_OK;
}

static ULONG WINAPI OperationAddRef(WH_OPERATION *operation)
{
    return (ULONG)InterlockedIncrement(&operation->Refs);
}

static ULONG WINAPI OperationRelease(WH_OPERATION *operation)
{
    LONG refs = InterlockedDecrement(&operation->Refs);
    if (!refs) {
        ManagerRelease(operation->Manager);
        WhFree(operation);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI OperationStartProxyResolve(WH_OPERATION *operation, DWORD flags, const WH_RESOLVE_URL *url,
                                                 void *context, WH_INTERFACE *completion, void **out)
{
    if (operation->Cancelled)
        return HR_CANCELLED;
    return ManagerGetProxyForUrl(operation->Manager, flags, url, nullptr, context, completion, out);
}

static HRESULT WINAPI OperationCancelProxyResolve(WH_OPERATION *operation)
{
    operation->Cancelled = TRUE;
    return S_OK;
}

static const void *const OperationVtbl[] = {
    OperationQueryInterface, OperationAddRef, OperationRelease, OperationStartProxyResolve, OperationCancelProxyResolve,
};

static HRESULT WINAPI OperationsQueryInterface(void *self, const GUID *iid, void **out)
{
    return ManagerQueryInterface(ManagerOf(self, 1), iid, out);
}

static ULONG WINAPI OperationsAddRef(void *self)
{
    return ManagerAddRef(ManagerOf(self, 1));
}

static ULONG WINAPI OperationsRelease(void *self)
{
    return ManagerRelease(ManagerOf(self, 1));
}

static HRESULT WINAPI OperationsCreateProxyOperation(void *self, void **out)
{
    WH_MANAGER *manager = ManagerOf(self, 1);
    WH_OPERATION *operation = WhAllocZero(sizeof(*operation));
    if (!operation)
        return E_OUTOFMEMORY;
    operation->Vtbl = OperationVtbl;
    operation->Refs = 1;
    operation->Manager = manager;
    InterlockedIncrement(&manager->Refs);
    *out = operation;
    return S_OK;
}

static const void *const OperationsVtbl[] = {
    OperationsQueryInterface, OperationsAddRef, OperationsRelease, OperationsCreateProxyOperation,
};

static HRESULT WINAPI ClientQueryInterface(void *self, const GUID *iid, void **out)
{
    return ManagerQueryInterface(ManagerOf(self, 2), iid, out);
}

static ULONG WINAPI ClientAddRef(void *self)
{
    return ManagerAddRef(ManagerOf(self, 2));
}

static ULONG WINAPI ClientRelease(void *self)
{
    return ManagerRelease(ManagerOf(self, 2));
}

/* the timeouts of the session Windows asks the service through. */
static HRESULT WINAPI ClientSetSessionOptions(void *self, const WH_SESSION_OPTIONS *options)
{
    WH_MANAGER *manager = ManagerOf(self, 2);
    if (!options)
        return E_INVALIDARG;
    EnterCriticalSection(&manager->Lock);
    manager->SessionOptions = *options;
    LeaveCriticalSection(&manager->Lock);
    return S_OK;
}

static HRESULT WINAPI ClientAggregateAllProxyConfigurations(void *self)
{
    ManagerOf(self, 2)->Aggregate = TRUE;
    return S_OK;
}

static const void *const ClientVtbl[] = {
    ClientQueryInterface, ClientAddRef, ClientRelease, ClientSetSessionOptions, ClientAggregateAllProxyConfigurations,
};

/* a manager serves the kinds of answers its low byte names, and one that names none is no manager. */
DWORD WINAPI WinHttpCreateProxyManager(DWORD flags, DWORD session_flags, void *list, void **out)
{
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = nullptr;
    if (!(flags & 0xFF))
        return ERROR_INVALID_PARAMETER;
    WH_MANAGER *manager = WhAllocZero(sizeof(*manager));
    if (!manager)
        return ERROR_NOT_ENOUGH_MEMORY;
    manager->Resolver = ManagerVtbl;
    manager->Operations = OperationsVtbl;
    manager->Client = ClientVtbl;
    manager->Refs = 1;
    manager->Flags = flags & 0xFF;
    manager->SessionFlags = session_flags;
    manager->AnswerVersion = NextVersion();
    manager->StaticVersion = NextVersion();
    manager->StaticType = CONFIG_TYPE_STATIC;
    manager->CacheTick = GetTickCount64();
    InitializeCriticalSection(&manager->Lock);
    if (list) {
        InterfaceAddRef(list);
        manager->List = list;
    }
    *out = manager;
    return ERROR_SUCCESS;
}

/* the completions the exports make for callers that give a function, or nothing and wait. */

typedef LONG(WINAPI *WH_PROXY_CALLBACK)(void *context, LONG hr, void *result);
typedef LONG(WINAPI *WH_REFRESH_CALLBACK)(void *context);

typedef struct {
    const void *const *Vtbl;
    LONG volatile Refs;
    const WH_IID *Iid;
    WH_PROXY_CALLBACK Resolved;
    WH_REFRESH_CALLBACK Refreshed;
    void *Context;
    HANDLE Event;
    HRESULT Result;
    void *Answer;
} WH_COMPLETION;

static HRESULT WINAPI CompletionQueryInterface(WH_COMPLETION *completion, const GUID *iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsIid(iid, &IidUnknown) && !IsIid(iid, completion->Iid))
        return E_NOINTERFACE;
    InterlockedIncrement(&completion->Refs);
    *out = completion;
    return S_OK;
}

static ULONG WINAPI CompletionAddRef(WH_COMPLETION *completion)
{
    return (ULONG)InterlockedIncrement(&completion->Refs);
}

static ULONG WINAPI CompletionRelease(WH_COMPLETION *completion)
{
    LONG refs = InterlockedDecrement(&completion->Refs);
    if (!refs) {
        if (completion->Answer)
            InterfaceRelease(completion->Answer);
        if (completion->Event)
            CloseHandle(completion->Event);
        WhFree(completion);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI CompletionOnProxyResolved(WH_COMPLETION *completion, HRESULT hr, void *result)
{
    if (completion->Resolved) {
        completion->Resolved(completion->Context, hr, result);
        return S_OK;
    }
    completion->Result = hr;
    if (result)
        InterfaceAddRef(result);
    completion->Answer = result;
    SetEvent(completion->Event);
    return S_OK;
}

static HRESULT WINAPI CompletionOnRefreshed(WH_COMPLETION *completion)
{
    if (completion->Refreshed)
        completion->Refreshed(completion->Context);
    else
        SetEvent(completion->Event);
    return S_OK;
}

static const void *const ProxyCompletionVtbl[] = {
    CompletionQueryInterface, CompletionAddRef, CompletionRelease, CompletionOnProxyResolved,
};

static const void *const RefreshCompletionVtbl[] = {
    CompletionQueryInterface, CompletionAddRef, CompletionRelease, CompletionOnRefreshed,
};

static WH_COMPLETION *CompletionCreate(const void *const *vtbl, const WH_IID *iid, BOOL wait)
{
    WH_COMPLETION *completion = WhAllocZero(sizeof(*completion));
    if (!completion)
        return nullptr;
    completion->Vtbl = vtbl;
    completion->Refs = 1;
    completion->Iid = iid;
    if (wait && !(completion->Event = CreateEventW(nullptr, FALSE, FALSE, nullptr))) {
        WhFree(completion);
        return nullptr;
    }
    return completion;
}

/*
 * the proxy of a url from a resolver, which wininet may implement itself. with a function the answer may come later
 * through it, announced by ERROR_IO_PENDING; without one the call waits for it.
 */
DWORD WINAPI WinHttpResolverGetProxyForUrl(DWORD flags, void *resolver, WH_RESOLVE_URL *url, void *context,
                                           WH_PROXY_CALLBACK callback, void *callback_context, void **out)
{
    if (!out || !resolver || !url)
        return ERROR_INVALID_PARAMETER;
    *out = nullptr;
    if (!callback && callback_context)
        return ERROR_INVALID_PARAMETER;
    WH_COMPLETION *completion = CompletionCreate(ProxyCompletionVtbl, &IidGetProxyForUrlCompletion, !callback);
    if (!completion)
        return ERROR_NOT_ENOUGH_MEMORY;
    completion->Resolved = callback;
    completion->Context = callback_context;
    void *result = nullptr;
    typedef HRESULT(WINAPI * GET)(void *, DWORD, const WH_RESOLVE_URL *, DWORD *, void *, void *, void **);
    HRESULT hr = SLOT(resolver, 3, GET)(resolver, flags, url, nullptr, context, completion, &result);
    if (hr == HR_PENDING) {
        if (callback) {
            hr = HR_IO_PENDING;
        } else if (WaitForSingleObject(completion->Event, INFINITE) != WAIT_OBJECT_0) {
            hr = E_UNEXPECTED;
        } else if ((hr = completion->Result) >= 0) {
            *out = completion->Answer;
            completion->Answer = nullptr;
        }
    } else if (hr >= 0) {
        *out = result;
        result = nullptr;
    }
    if (result)
        InterfaceRelease(result);
    CompletionRelease(completion);
    return Win32FromResult(hr);
}

DWORD WINAPI WinHttpRefreshProxySettings(void *resolver, BOOL flush, WH_REFRESH_CALLBACK callback, void *context)
{
    if (!resolver)
        return ERROR_INVALID_PARAMETER;
    WH_COMPLETION *completion = CompletionCreate(RefreshCompletionVtbl, &IidRefreshCompletion, !callback);
    if (!completion)
        return ERROR_NOT_ENOUGH_MEMORY;
    completion->Refreshed = callback;
    completion->Context = context;
    HRESULT hr = SLOT(resolver, 6, HRESULT(WINAPI *)(void *, BOOL, void *))(resolver, flush, completion);
    if (hr == HR_PENDING) {
        if (callback)
            hr = HR_IO_PENDING;
        else
            hr = WaitForSingleObject(completion->Event, INFINITE) == WAIT_OBJECT_0 ? S_OK : E_UNEXPECTED;
    }
    CompletionRelease(completion);
    return Win32FromResult(hr);
}
