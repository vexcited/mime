/*
 * what dnsapi asks the dns client service, Dnscache, over ncalrpc:[DNSResolver]: the lookups, whose answers and cache are
 * the service's, the cache's table and its flushes. without the service a lookup is made here, with no cache, as
 * windows' dnsapi does when the service does not answer.
 */
#define RESOLVER_CLIENT_ONLY
#define MIMERESOLVER_CLIENT_ONLY
#include "dns_private.h"
#include <mime/win32/rpc.h>
#include "dnsrslvr/dnsrslvr.h"

#define RPC_C_QOS_IDENTITY_DYNAMIC_ 1

void *WINAPI MIDL_user_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void WINAPI MIDL_user_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* windows' dnsapi binds with the privacy level, which the service's interface asks for. */
static RPC_BINDING_HANDLE bind_service(void)
{
    RPC_BINDING_HANDLE b = nullptr;
    if (RpcBindingFromStringBindingW((RPC_WSTR)L"ncalrpc:[DNSResolver]", &b) != RPC_S_OK)
        return nullptr;
    RPC_SECURITY_QOS qos = {RPC_C_SECURITY_QOS_VERSION, 0, RPC_C_QOS_IDENTITY_DYNAMIC_, RPC_C_IMP_LEVEL_IMPERSONATE};
    if (RpcBindingSetAuthInfoExW(b, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_AUTHN_WINNT, nullptr,
                                 RPC_C_AUTHZ_NONE, &qos) != RPC_S_OK) {
        RpcBindingFree(&b);
        return nullptr;
    }
    return b;
}

/* a lookup by the service; FALSE when it cannot be asked. */
static BOOL service_lookup(const char *name, WORD type, ULONGLONG options, DNS_STATUS *status, DNS_ANSWER *answer)
{
    RPC_BINDING_HANDLE b = bind_service();
    if (!b)
        return FALSE;
    BOOL asked = FALSE;
    unsigned long capacity = 4096;
    for (int attempt = 0; attempt < 3; attempt++) {
        BYTE *buffer = DnsAllocate(capacity);
        if (!buffer)
            break;
        unsigned long needed = 0;
        long result;
        __try {
            result = mimeresolver_MimeResolverLookup(b, (char *)name, type, (long long)options, buffer,
                                                     capacity, &needed);
            asked = TRUE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            result = 0;
        }
        if (asked && result == ERROR_MORE_DATA && needed > capacity && needed <= 65536) {
            DnsRelease(buffer);
            capacity = needed;
            asked = FALSE;
            continue;
        }
        if (asked) {
            BOOL ok;
            answer->records = DnsRrUnpack(buffer, needed <= capacity ? needed : capacity, &ok);
            if (!ok)
                result = ERROR_NOT_ENOUGH_MEMORY;
            answer->status = *status = result;
        }
        DnsRelease(buffer);
        break;
    }
    RpcBindingFree(&b);
    return asked;
}

/* the service makes the lookups programs ask for; one that names its servers or wants the message is made here. */
DNS_STATUS DnsResolve(const char *name, WORD type, ULONGLONG options, const DNS_SERVER *servers, int server_count,
                      volatile LONG *cancelled, DNS_ANSWER *answer)
{
    memset(answer, 0, sizeof(*answer));
    DNS_STATUS status;
    if (!server_count && !(options & DNS_QUERY_RETURN_MESSAGE) && service_lookup(name, type, options, &status, answer))
        return status;
    DnsAnswerFree(answer);
    return DnsLookup(name, type, options, servers, server_count, cancelled, answer);
}

/* ------------------------------------------------------------------ the cache */

BOOL WINAPI DnsFlushResolverCache(void)
{
    RPC_BINDING_HANDLE b = bind_service();
    if (!b)
        return FALSE;
    __try {
        resolver_R_ResolverFlushCache(b);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    RpcBindingFree(&b);
    return TRUE;
}

BOOL WINAPI DnsFlushResolverCacheEntry_W(PCWSTR name)
{
    if (!name)
        return FALSE;
    RPC_BINDING_HANDLE b = bind_service();
    if (!b)
        return FALSE;
    long result;
    __try {
        result = resolver_R_ResolverFlushCacheEntry(b, (unsigned short *)name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result = __exception_code();
    }
    RpcBindingFree(&b);
    return result == 0;
}

static BOOL flush_narrow(PCSTR name, UINT codepage)
{
    if (!name)
        return FALSE;
    int n = MultiByteToWideChar(codepage, 0, name, -1, nullptr, 0);
    WCHAR *wide = n > 0 ? DnsAllocate(n * sizeof(WCHAR)) : nullptr;
    BOOL ok = wide && MultiByteToWideChar(codepage, 0, name, -1, wide, n) > 0 && DnsFlushResolverCacheEntry_W(wide);
    DnsRelease(wide);
    return ok;
}

BOOL WINAPI DnsFlushResolverCacheEntry_A(PCSTR name)
{
    return flush_narrow(name, CP_ACP);
}

BOOL WINAPI DnsFlushResolverCacheEntry_UTF8(PCSTR name)
{
    return flush_narrow(name, CP_UTF8);
}

/* the table of what the service holds, a list from the rpc allocator the caller frees entry by entry with DnsFree. */
DNS_STATUS WINAPI DnsGetCacheDataTableEx(ULONG64 flags, DNS_RPC_CACHE_ENTRY **table)
{
    if (!table)
        return ERROR_INVALID_PARAMETER;
    *table = nullptr;
    RPC_BINDING_HANDLE b = bind_service();
    if (!b)
        return RPC_S_SERVER_UNAVAILABLE;
    DNS_RPC_CACHE_ENTRY *list = nullptr;
    long result;
    __try {
        result = resolver_R_ReadCache(b, (long long)flags, &list);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result = __exception_code();
    }
    RpcBindingFree(&b);
    if (!result)
        *table = list;
    return result;
}

BOOL WINAPI DnsGetCacheDataTable(DNS_RPC_CACHE_ENTRY **table)
{
    return DnsGetCacheDataTableEx(0, table) == 0;
}
