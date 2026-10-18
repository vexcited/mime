/*
 * the extended resolver GetAddrInfoEx, synchronous or completing in the
 * background, and the name space provider interface: lookups of services,
 * the list of providers and the catalog queries of WSC.
 */
#include "ws2_32_private.h"

int WSAAPI WSAEnumProtocolsW(LPINT protocols, LPWSAPROTOCOL_INFOW buffer, LPDWORD length);

#define NS_ALL 0
#define NS_DNS 12
#define NS_NLA 15

typedef struct addrinfoexW {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    PWSTR ai_canonname;
    struct sockaddr *ai_addr;
    void *ai_blob;
    size_t ai_bloblen;
    GUID * ai_provider;
    struct addrinfoexW *ai_next;
} ADDRINFOEXW, *PADDRINFOEXW;

typedef struct addrinfoexA {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    void *ai_blob;
    size_t ai_bloblen;
    GUID * ai_provider;
    struct addrinfoexA *ai_next;
} ADDRINFOEXA, *PADDRINFOEXA;

typedef void(CALLBACK *LOOKUP_COMPLETION)(DWORD error, DWORD bytes, LPOVERLAPPED overlapped);

/* the provider every answer comes from, the tcpip name space of windows. */
static GUID g_dns_provider = {0x22059d40, 0x7e9e, 0x11cf, {0xae, 0x5a, 0x00, 0xaa, 0x00, 0xa7, 0x11, 0x2b}};
static GUID g_nla_provider = {0x6642243a, 0x3ba8, 0x4aa6, {0xba, 0xa5, 0x2e, 0x0b, 0xd7, 0x1f, 0xdd, 0x83}};

void WSAAPI FreeAddrInfoExW(PADDRINFOEXW info)
{
    while (info) {
        PADDRINFOEXW next = info->ai_next;
        WsFree(info->ai_canonname);
        WsFree(info->ai_addr);
        WsFree(info->ai_blob);
        WsFree(info);
        info = next;
    }
}

void WSAAPI FreeAddrInfoEx(PADDRINFOEXA info)
{
    FreeAddrInfoExW((PADDRINFOEXW)info);
}

/* the answer of GetAddrInfoW, moved into the extended layout. */
static int convert(PADDRINFOW list, PADDRINFOEXW *out)
{
    PADDRINFOEXW *tail = out;
    for (PADDRINFOW ai = list; ai; ai = ai->ai_next) {
        PADDRINFOEXW ex = WsAllocZero(sizeof(*ex));
        if (!ex)
            return WSA_NOT_ENOUGH_MEMORY;
        ex->ai_flags = ai->ai_flags;
        ex->ai_family = ai->ai_family;
        ex->ai_socktype = ai->ai_socktype;
        ex->ai_protocol = ai->ai_protocol;
        ex->ai_addrlen = ai->ai_addrlen;
        ex->ai_canonname = ai->ai_canonname;
        ex->ai_addr = ai->ai_addr;
        ai->ai_canonname = nullptr;
        ai->ai_addr = nullptr;
        *tail = ex;
        tail = &ex->ai_next;
    }
    return 0;
}

static int resolve(PCWSTR name, PCWSTR service, DWORD space, const ADDRINFOEXW *hints, PADDRINFOEXW *result)
{
    if (space != NS_ALL && space != NS_DNS)
        return WSAEINVAL;
    ADDRINFOW plain = {};
    if (hints) {
        if (hints->ai_addrlen || hints->ai_canonname || hints->ai_addr || hints->ai_next || hints->ai_blob ||
            hints->ai_bloblen)
            return WSANO_RECOVERY;
        plain.ai_flags = hints->ai_flags;
        plain.ai_family = hints->ai_family;
        plain.ai_socktype = hints->ai_socktype;
        plain.ai_protocol = hints->ai_protocol;
    }
    PADDRINFOW list = nullptr;
    int error = GetAddrInfoW(name, service, hints ? &plain : nullptr, &list);
    if (!error)
        error = convert(list, result);
    FreeAddrInfoW(list);
    if (error) {
        FreeAddrInfoExW(*result);
        *result = nullptr;
    }
    return error;
}

/* ------------------------------------------------------------------ background lookups */

typedef struct GAI_JOB {
    struct GAI_JOB *Next;
    LONG Done;
    PWSTR Name;
    PWSTR Service;
    DWORD Space;
    ADDRINFOEXW Hints;
    BOOL HasHints;
    PADDRINFOEXW *Result;
    LPOVERLAPPED Overlapped;
    LOOKUP_COMPLETION Completion;
} GAI_JOB;

static SRWLOCK g_jobs_lock = SRWLOCK_INIT;
static GAI_JOB *g_jobs;

static PWSTR duplicate(PCWSTR s)
{
    if (!s)
        return nullptr;
    SIZE_T n = (wcslen(s) + 1) * sizeof(WCHAR);
    PWSTR copy = WsAlloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

/* takes a job off the list; whoever takes it delivers its completion. */
static BOOL claim(GAI_JOB *job)
{
    BOOL found = FALSE;
    AcquireSRWLockExclusive(&g_jobs_lock);
    for (GAI_JOB **p = &g_jobs; *p; p = &(*p)->Next) {
        if (*p == job) {
            *p = job->Next;
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_jobs_lock);
    return found;
}

static void deliver(GAI_JOB *job, int error, PADDRINFOEXW result)
{
    *job->Result = result;
    job->Overlapped->Internal = (ULONG_PTR)error;
    if (job->Completion)
        job->Completion((DWORD)error, 0, job->Overlapped);
    else
        SetEvent(job->Overlapped->hEvent);
}

static DWORD WINAPI gai_worker(LPVOID parameter)
{
    GAI_JOB *job = parameter;
    PADDRINFOEXW result = nullptr;
    int error = resolve(job->Name, job->Service, job->Space, job->HasHints ? &job->Hints : nullptr, &result);
    if (claim(job))
        deliver(job, error, result);
    else
        FreeAddrInfoExW(result);
    WsFree(job->Name);
    WsFree(job->Service);
    if (InterlockedIncrement(&job->Done) == 2)
        WsFree(job);
    return 0;
}

INT WSAAPI GetAddrInfoExW(PCWSTR name, PCWSTR service, DWORD space, GUID * provider, const ADDRINFOEXW *hints,
                          PADDRINFOEXW *result, struct timeval *timeout, LPOVERLAPPED overlapped,
                          LOOKUP_COMPLETION completion, LPHANDLE handle)
{
    BOOL asynchronous = overlapped || timeout || completion || handle;
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return error;
    }
    *result = nullptr;
    if (handle)
        *handle = nullptr;
    if (!name && !service) {
        SetLastError(WSAHOST_NOT_FOUND);
        return WSAHOST_NOT_FOUND;
    }
    if (asynchronous) {
        /* a completion needs exactly one of a routine and an event. */
        error = WSAEINVAL;
        if (overlapped) {
            overlapped->Internal = WSAEINPROGRESS;
            if (!completion != !overlapped->hEvent)
                error = 0;
        }
        if (error) {
            SetLastError(error);
            return error;
        }
    }
    if (hints && (hints->ai_addrlen || hints->ai_canonname || hints->ai_addr || hints->ai_next)) {
        SetLastError(WSANO_RECOVERY);
        return WSANO_RECOVERY;
    }
    if (hints && (hints->ai_flags & (AI_CANONNAME | AI_FQDN)) && !name) {
        SetLastError(WSAEINVAL);
        return WSAEINVAL;
    }
    if (!asynchronous) {
        error = resolve(name, service, space, hints, result);
        SetLastError(error);
        return error;
    }
    GAI_JOB *job = WsAllocZero(sizeof(*job));
    if (!job) {
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return WSA_NOT_ENOUGH_MEMORY;
    }
    job->Name = duplicate(name);
    job->Service = duplicate(service);
    job->Space = space;
    if (hints) {
        job->Hints = *hints;
        job->HasHints = TRUE;
    }
    job->Result = result;
    job->Overlapped = overlapped;
    job->Completion = completion;
    AcquireSRWLockExclusive(&g_jobs_lock);
    job->Next = g_jobs;
    g_jobs = job;
    ReleaseSRWLockExclusive(&g_jobs_lock);
    if (handle)
        *handle = job;
    HANDLE thread = CreateThread(nullptr, 0, gai_worker, job, 0, nullptr);
    if (!thread) {
        claim(job);
        WsFree(job->Name);
        WsFree(job->Service);
        WsFree(job);
        if (handle)
            *handle = nullptr;
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return WSA_NOT_ENOUGH_MEMORY;
    }
    CloseHandle(thread);
    SetLastError(WSA_IO_PENDING);
    return WSA_IO_PENDING;
}

/* the lookup completes at once with WSA_E_CANCELLED; its worker's answer is then dropped. */
INT WSAAPI GetAddrInfoExCancel(LPHANDLE handle)
{
    if (!handle || !*handle)
        return WSA_INVALID_HANDLE;
    GAI_JOB *job = *handle;
    if (!claim(job))
        return WSA_INVALID_HANDLE;
    deliver(job, WSA_E_CANCELLED, nullptr);
    if (InterlockedIncrement(&job->Done) == 2)
        WsFree(job);
    return 0;
}

INT WSAAPI GetAddrInfoExOverlappedResult(LPOVERLAPPED overlapped)
{
    return overlapped ? (INT)overlapped->Internal : WSAEINVAL;
}

/* the ansi form converts the strings and the canonical names, and has no background mode. */
INT WSAAPI GetAddrInfoExA(PCSTR name, PCSTR service, DWORD space, GUID * provider, const ADDRINFOEXA *hints,
                          PADDRINFOEXA *result, struct timeval *timeout, LPOVERLAPPED overlapped,
                          LOOKUP_COMPLETION completion, LPHANDLE handle)
{
    if (timeout || overlapped || completion || handle) {
        SetLastError(WSAEINVAL);
        return WSAEINVAL;
    }
    WCHAR wname[1024], wservice[256];
    if (name)
        MultiByteToWideChar(CP_ACP, 0, name, -1, wname, RTL_NUMBER_OF(wname));
    if (service)
        MultiByteToWideChar(CP_ACP, 0, service, -1, wservice, RTL_NUMBER_OF(wservice));
    PADDRINFOEXW wide = nullptr;
    int error = GetAddrInfoExW(name ? wname : nullptr, service ? wservice : nullptr, space, provider,
                               (const ADDRINFOEXW *)hints, &wide, nullptr, nullptr, nullptr, nullptr);
    for (PADDRINFOEXW ai = wide; !error && ai; ai = ai->ai_next) {
        if (!ai->ai_canonname)
            continue;
        int n = WideCharToMultiByte(CP_ACP, 0, ai->ai_canonname, -1, nullptr, 0, nullptr, nullptr);
        char *a = n > 0 ? WsAlloc((SIZE_T)n) : nullptr;
        if (!a) {
            error = WSA_NOT_ENOUGH_MEMORY;
            break;
        }
        WideCharToMultiByte(CP_ACP, 0, ai->ai_canonname, -1, a, n, nullptr, nullptr);
        WsFree(ai->ai_canonname);
        ai->ai_canonname = (PWSTR)a;
    }
    if (error) {
        FreeAddrInfoExW(wide);
        wide = nullptr;
    }
    *result = (PADDRINFOEXA)wide;
    SetLastError(error);
    return error;
}

/* ------------------------------------------------------------------ name space providers */

typedef struct WSANAMESPACE_INFOW {
    GUID NSProviderId;
    DWORD dwNameSpace;
    BOOL fActive;
    DWORD dwVersion;
    LPWSTR lpszIdentifier;
} WSANAMESPACE_INFOW;

typedef struct WSANAMESPACE_INFOA {
    GUID NSProviderId;
    DWORD dwNameSpace;
    BOOL fActive;
    DWORD dwVersion;
    LPSTR lpszIdentifier;
} WSANAMESPACE_INFOA;

static const struct {
    const GUID *Id;
    DWORD Space;
    PCWSTR Name;
} g_providers[] = {
    {&g_nla_provider, NS_NLA, L"Network Location Awareness Legacy"},
    {&g_dns_provider, NS_DNS, L"Tcpip"},
};

static INT enum_providers(LPDWORD length, void *buffer, BOOL wide)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!length)
        return WsFail(WSAEFAULT);
    SIZE_T entry = wide ? sizeof(WSANAMESPACE_INFOW) : sizeof(WSANAMESPACE_INFOA);
    SIZE_T need = entry * RTL_NUMBER_OF(g_providers);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_providers); i++)
        need += (wcslen(g_providers[i].Name) + 1) * (wide ? sizeof(WCHAR) : 1);
    if (!buffer || *length < need) {
        *length = (DWORD)need;
        return WsFail(WSAEFAULT);
    }
    char *strings = (char *)buffer + entry * RTL_NUMBER_OF(g_providers);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_providers); i++) {
        WSANAMESPACE_INFOW *info = (WSANAMESPACE_INFOW *)((char *)buffer + entry * i);
        info->NSProviderId = *g_providers[i].Id;
        info->dwNameSpace = g_providers[i].Space;
        info->fActive = TRUE;
        info->dwVersion = 0;
        info->lpszIdentifier = (LPWSTR)strings;
        SIZE_T n = wcslen(g_providers[i].Name) + 1;
        if (wide) {
            memcpy(strings, g_providers[i].Name, n * sizeof(WCHAR));
            strings += n * sizeof(WCHAR);
        } else {
            WideCharToMultiByte(CP_ACP, 0, g_providers[i].Name, -1, strings, (int)n, nullptr, nullptr);
            strings += n;
        }
    }
    return (INT)RTL_NUMBER_OF(g_providers);
}

INT WSAAPI WSAEnumNameSpaceProvidersW(LPDWORD length, WSANAMESPACE_INFOW *buffer)
{
    return enum_providers(length, buffer, TRUE);
}

INT WSAAPI WSAEnumNameSpaceProvidersA(LPDWORD length, WSANAMESPACE_INFOA *buffer)
{
    return enum_providers(length, buffer, FALSE);
}

/*
 * lookups of services: a query is accepted and then has nothing to return,
 * as the network location provider answers for a machine on no network
 * it knows and no service is registered.
 */
typedef struct WS_LOOKUP {
    DWORD Magic;
} WS_LOOKUP;

#define LOOKUP_MAGIC 0x4B4F4F4C

INT WSAAPI WSALookupServiceBeginW(void *restrictions, DWORD flags, LPHANDLE lookup)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!restrictions || !lookup)
        return WsFail(WSAEFAULT);
    if (*(DWORD *)restrictions < 0x3C)
        return WsFail(WSAEFAULT);
    WS_LOOKUP *l = WsAllocZero(sizeof(*l));
    if (!l)
        return WsFail(WSA_NOT_ENOUGH_MEMORY);
    l->Magic = LOOKUP_MAGIC;
    *lookup = l;
    return 0;
}

INT WSAAPI WSALookupServiceBeginA(void *restrictions, DWORD flags, LPHANDLE lookup)
{
    return WSALookupServiceBeginW(restrictions, flags, lookup);
}

static BOOL valid_lookup(HANDLE lookup)
{
    return lookup && ((WS_LOOKUP *)lookup)->Magic == LOOKUP_MAGIC;
}

INT WSAAPI WSALookupServiceNextW(HANDLE lookup, DWORD flags, LPDWORD length, void *results)
{
    if (!valid_lookup(lookup))
        return WsFail(WSA_INVALID_HANDLE);
    if (!length)
        return WsFail(WSAEFAULT);
    return WsFail(WSA_E_NO_MORE);
}

INT WSAAPI WSALookupServiceNextA(HANDLE lookup, DWORD flags, LPDWORD length, void *results)
{
    return WSALookupServiceNextW(lookup, flags, length, results);
}

INT WSAAPI WSALookupServiceEnd(HANDLE lookup)
{
    if (!valid_lookup(lookup))
        return WsFail(WSA_INVALID_HANDLE);
    ((WS_LOOKUP *)lookup)->Magic = 0;
    WsFree(lookup);
    return 0;
}

/* no provider here registers services. */
INT WSAAPI WSASetServiceW(void *info, int operation, DWORD flags)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    return WsFail(info ? WSAEINVAL : WSAEFAULT);
}

INT WSAAPI WSASetServiceA(void *info, int operation, DWORD flags)
{
    return WSASetServiceW(info, operation, flags);
}

/* ------------------------------------------------------------------ the catalog */

/* WSAEnumProtocolsW with the error in the caller's variable, as WSC functions report. */
int WSAAPI WSCEnumProtocols(LPINT protocols, LPWSAPROTOCOL_INFOW buffer, LPDWORD length, LPINT error)
{
    DWORD saved = GetLastError();
    BOOL started = !WsCheckStarted();
    if (!started) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    int n = WSAEnumProtocolsW(protocols, buffer, length);
    if (error)
        *error = n == SOCKET_ERROR ? (int)GetLastError() : 0;
    if (!started)
        WSACleanup();
    SetLastError(saved);
    return n;
}

/* the base providers of the catalog live in mswsock.dll, under the unexpanded system root. */
int WSAAPI WSCGetProviderPath(GUID * provider, WCHAR *path, LPINT length, LPINT error)
{
    static const WCHAR mswsock[] = L"%SystemRoot%\\system32\\mswsock.dll";
    if (!provider || !path || !length) {
        if (error)
            *error = WSAEFAULT;
        return SOCKET_ERROR;
    }
    WSAPROTOCOL_INFOW info[16];
    DWORD size = sizeof(info);
    int found = 0, err = 0;
    int n = WSCEnumProtocols(nullptr, info, &size, &err);
    for (int i = 0; i < n; i++)
        if (!memcmp(&info[i].ProviderId, provider, sizeof(GUID)))
            found = 1;
    if (!found) {
        if (error)
            *error = WSAEINVAL;
        return SOCKET_ERROR;
    }
    if (*length < (int)RTL_NUMBER_OF(mswsock)) {
        *length = RTL_NUMBER_OF(mswsock);
        if (error)
            *error = WSAEFAULT;
        return SOCKET_ERROR;
    }
    memcpy(path, mswsock, sizeof(mswsock));
    *length = RTL_NUMBER_OF(mswsock);
    if (error)
        *error = 0;
    return 0;
}
