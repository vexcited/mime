/*
 * dnsrslvr.dll: the dns client service, Dnscache. it makes the lookups of every process with dnsapi's code and keeps
 * their answers in one cache for the machine, on ncalrpc:[DNSResolver]: windows' resolver interface reads and flushes
 * the cache, mime's own makes the lookups. the service cannot be stopped, as on windows.
 */
#include "../dnsapi/dns_private.h"
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>
#include "dnsrslvr/dnsrslvr.h"

WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINBASEAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI BOOL WINAPI AccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, PVOID mapping, PVOID privileges,
                                   LPDWORD privileges_length, LPDWORD granted, LPBOOL status);

#define TOKEN_QUERY 0x0008

typedef DWORD(WINAPI *REGISTER_STOP)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback, void *context,
                                     DWORD flags);

/* the block svchost pushes, from the first sid on, and the place of its stop callback in it. */
#define GLOBALS_REGISTER_STOP 24
static void **g_globals;

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static PSECURITY_DESCRIPTOR g_access;
static HANDLE g_stop_event, g_stop_wait;

/* the parameter, binding and power changes are taken, nothing else. */
#define ACCEPTED_CONTROLS (SERVICE_ACCEPT_PARAMCHANGE | SERVICE_ACCEPT_NETBINDCHANGE | SERVICE_ACCEPT_POWEREVENT)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void WINAPI SvchostPushServiceGlobals(void **globals)
{
    g_globals = globals;
}

void *WINAPI MIDL_user_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void WINAPI MIDL_user_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    switch (control) {
    case SERVICE_CONTROL_INTERROGATE:
    case SERVICE_CONTROL_PARAMCHANGE:
    case SERVICE_CONTROL_NETBINDADD:
    case SERVICE_CONTROL_NETBINDREMOVE:
    case SERVICE_CONTROL_NETBINDENABLE:
    case SERVICE_CONTROL_NETBINDDISABLE:
    case SERVICE_CONTROL_POWEREVENT:
        SetServiceStatus(g_handle, &g_status);
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/* ------------------------------------------------------------------ access */

/* Rpc_SecurityCallback of windows' dnsrslvr.dll: local rpc only. */
static RPC_STATUS RPC_ENTRY lpc_only(RPC_IF_HANDLE interface, void *context)
{
    RPC_WSTR binding = nullptr, protseq = nullptr;
    BOOL lpc = RpcBindingToStringBindingW(context, &binding) == RPC_S_OK &&
               RpcStringBindingParseW(binding, nullptr, &protseq, nullptr, nullptr, nullptr) == RPC_S_OK && protseq &&
               CompareStringW(0x409, NORM_IGNORECASE, (const WCHAR *)protseq, -1, L"ncalrpc", -1) == CSTR_EQUAL;
    if (protseq)
        RpcStringFreeW(&protseq);
    if (binding)
        RpcStringFreeW(&binding);
    return lpc ? RPC_S_OK : ERROR_ACCESS_DENIED;
}

/*
 * Rpc_AccessCheck: the local service may do everything, administrators, power users, the network service and the
 * network configuration operators read and write, through windows' mapping of the generic rights. 2 reads the cache,
 * 0x20 flushes it.
 */
static BOOL caller_may(DWORD desired)
{
    static const DWORD mapping[4] = {0x20013, 0x20033, 0x20013, 0x20133};
    if (!g_access || RpcImpersonateClient(nullptr) != RPC_S_OK)
        return FALSE;
    HANDLE token = nullptr;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    RpcRevertToSelf();
    if (!ok)
        return FALSE;
    BYTE privileges[20];
    DWORD length = sizeof(privileges), granted = 0;
    BOOL allowed = FALSE;
    ok = AccessCheck(g_access, token, desired, (PVOID)mapping, privileges, &length, &granted, &allowed);
    CloseHandle(token);
    return ok && allowed;
}

/* ------------------------------------------------------------------ windows' resolver interface */

typedef struct {
    DNS_RPC_CACHE_ENTRY *head, **tail;
    DWORD count, limit;
    BOOL failed;
} TABLE;

static void add_entry(const char *name, WORD type, void *context)
{
    TABLE *t = context;
    if (t->failed || t->count >= t->limit)
        return;
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    DNS_RPC_CACHE_ENTRY *e = n > 0 ? MIDL_user_allocate(sizeof(*e)) : nullptr;
    WCHAR *wide = e ? MIDL_user_allocate(n * sizeof(WCHAR)) : nullptr;
    if (!wide) {
        MIDL_user_free(e);
        t->failed = TRUE;
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, n);
    e->pszName = (unsigned short *)wide;
    e->wType = type;
    *t->tail = e;
    t->tail = &e->pNext;
    t->count++;
}

/* the names and types the cache holds, 15000 of them unless the first flag asks for every one. */
long WINAPI resolver_R_ReadCache_s(handle_t binding, long long flags, DNS_RPC_CACHE_ENTRY **table)
{
    *table = nullptr;
    if (!caller_may(2))
        return ERROR_ACCESS_DENIED;
    TABLE t = {nullptr, &t.head, 0, (flags & 1) ? 0xFFFFFFFF : 15000, FALSE};
    DnsCacheList(add_entry, &t);
    if (t.failed) {
        while (t.head) {
            DNS_RPC_CACHE_ENTRY *next = t.head->pNext;
            MIDL_user_free(t.head->pszName);
            MIDL_user_free(t.head);
            t.head = next;
        }
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *table = t.head;
    return 0;
}

long WINAPI resolver_R_ResolverFlushCache_s(handle_t binding)
{
    if (!caller_may(0x20))
        return ERROR_ACCESS_DENIED;
    DnsCacheFlush(nullptr);
    return 0;
}

long WINAPI resolver_R_ResolverFlushCacheEntry_s(handle_t binding, unsigned short *name)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    int n = WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)name, -1, nullptr, 0, nullptr, nullptr);
    char *utf8 = n > 0 ? DnsAllocate(n) : nullptr;
    if (!utf8)
        return ERROR_NOT_ENOUGH_MEMORY;
    WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)name, -1, utf8, n, nullptr, nullptr);
    SIZE_T length = strlen(utf8);
    if (length > 1 && utf8[length - 1] == '.')
        utf8[length - 1] = 0;
    DnsCacheFlush(utf8);
    DnsRelease(utf8);
    return 0;
}

#define NOT_SERVED(name)                                                                                               \
    long WINAPI resolver_##name##_s(handle_t binding)                                                                   \
    {                                                                                                                  \
        return ERROR_CALL_NOT_IMPLEMENTED;                                                                             \
    }

NOT_SERVED(R_ResolverGetConfig)
NOT_SERVED(R_ResolverQuery)
NOT_SERVED(R_DnsRegisterLocal)
NOT_SERVED(R_DnsDeRegisterLocal)
NOT_SERVED(R_DnsStartMulticastQuery)
NOT_SERVED(R_DnsGetMulticastData)
NOT_SERVED(R_DnsStopMulticastQuery)
NOT_SERVED(R_ResolverSimpleOp)
NOT_SERVED(R_DnsGetProxyInformation)
NOT_SERVED(R_DnsGetPolicyTableInfo)
NOT_SERVED(R_DnsSetConnectionPolicyInfo)
NOT_SERVED(R_DnsDeleteConnectionPolicyInfo)
NOT_SERVED(R_DnsGetSettings)
NOT_SERVED(R_DnsGetSettingsImpersonated)
NOT_SERVED(R_DnsSetSettings)
NOT_SERVED(R_DnsSetSettingsImpersonated)
NOT_SERVED(R_DnsGetInterfaceSettings)
NOT_SERVED(R_DnsGetInterfaceSettingsImpersonated)
NOT_SERVED(R_DnsSetInterfaceSettings)
NOT_SERVED(R_DnsSetInterfaceSettingsImpersonated)
NOT_SERVED(R_DnsGetAdaptersInfo)
NOT_SERVED(R_DnsSetNrptRules)
NOT_SERVED(R_DnsSetWellKnownServerProperty)
NOT_SERVED(R_DnsDeleteWellKnownServer)
NOT_SERVED(R_DnsGetWellKnownServerProperty)
NOT_SERVED(R_DnsGetWellKnownServers)
NOT_SERVED(R_DnsDeleteSettings)
NOT_SERVED(R_DnsGetDdrInfo)
NOT_SERVED(R_ReadCacheRecords)
NOT_SERVED(R_DnsGetNetworkServers)
NOT_SERVED(R_DnsZtNotifyDnscache)
NOT_SERVED(R_DnsGetZtFilterIds)
NOT_SERVED(R_DnsGetZtConfig)
NOT_SERVED(R_DnsGetZtTrustedServers)
NOT_SERVED(R_DnsGetZtRules)
NOT_SERVED(R_DnsGetZtClientCertConfig)
NOT_SERVED(R_DnsGetZtTrustedCa)
NOT_SERVED(R_DnsGetZtSettings)
NOT_SERVED(R_DnsUpdate)

/* ------------------------------------------------------------------ mime's lookups */

/* a lookup with the machine's cache: its status, and its records packed for the caller, or the size they need. */
long WINAPI mimeresolver_MimeResolverLookup_s(handle_t binding, char *name, unsigned short type,
                                              long long options, byte *buffer, unsigned long size,
                                              unsigned long *needed)
{
    *needed = 0;
    DNS_ANSWER answer;
    DNS_STATUS status = DnsLookup(name, type, (ULONGLONG)options, nullptr, 0, nullptr, &answer);
    DWORD packed = DnsRrPack(answer.records, buffer, size);
    DnsAnswerFree(&answer);
    *needed = packed;
    return packed > size ? ERROR_MORE_DATA : status;
}

/* ------------------------------------------------------------------ the service */

/* Rpc_Initialize: the endpoint's descriptor has no list, so it lets everyone in; the interfaces want privacy. */
static RPC_STATUS start_rpc(void)
{
    static BYTE open_sd[20] = {1, 0, 0x04, 0x80};
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"DNSResolver", open_sd);
    if (status == RPC_S_DUPLICATE_ENDPOINT)
        status = RPC_S_OK;
    const unsigned flags = RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_SECURE_ONLY | RPC_IF_ALLOW_LOCAL_ONLY;
    if (!status)
        status = RpcServerRegisterIf3(resolver_s_ifspec, nullptr, nullptr, flags, 1234, (unsigned)-1, lpc_only,
                                      open_sd);
    if (!status)
        status = RpcServerRegisterIf3(mimeresolver_s_ifspec, nullptr, nullptr, flags, 1234, (unsigned)-1, lpc_only,
                                      open_sd);
    return status;
}

static void report_stopped(DWORD error)
{
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwControlsAccepted = 0;
    g_status.dwWin32ExitCode = error;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_handle, &g_status);
}

/*
 * StopService: the stop event that RegisterStopService gave svchost, which nothing sets while the machine runs. the
 * registration holds svchost's reference to the library, which ServiceDllUnloadOnStop would otherwise drop when
 * ServiceMain returns.
 */
static void WINAPI stopped(void *context, BOOLEAN timed_out)
{
    RpcServerUnregisterIf(mimeresolver_s_ifspec, nullptr, 1);
    RpcServerUnregisterIf(resolver_s_ifspec, nullptr, 1);
    report_stopped(0);
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwCheckPoint = 1;
    g_status.dwWaitHint = 5000;
    g_handle = RegisterServiceCtrlHandlerExW(L"Dnscache", control_handler, nullptr);
    if (!g_handle)
        return;
    SetServiceStatus(g_handle, &g_status);
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAG:BAD:(A;;GA;;;LS)(A;;GRGW;;;BA)(A;;GRGW;;;PU)(A;;GRGW;;;NS)(A;;GRGW;;;NO)", 1, &g_access, nullptr);
    DnsCacheEnabled = TRUE;
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD status = g_stop_event ? start_rpc() : GetLastError();
    if (!status)
        status = g_globals ? ((REGISTER_STOP)g_globals[GLOBALS_REGISTER_STOP])(&g_stop_wait, L"Dnscache", g_stop_event,
                                                                               (void *)stopped, nullptr, WT_EXECUTEONLYONCE)
                           : ERROR_INVALID_DATA;
    if (status) {
        report_stopped(status);
        return;
    }
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = ACCEPTED_CONTROLS;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_handle, &g_status);
}
