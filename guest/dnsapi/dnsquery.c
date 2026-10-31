/*
 * the exports of the dns client that look names up: DnsQuery in its three character sets, DnsQueryEx with its
 * completion routine and its cancel handle, and the calls that free what they return or forget what they learned.
 */
#include "dns_private.h"
#include <mime/win32/winsock2.h>

struct DNS_QUERY_RESULT_;
typedef VOID(WINAPI *DNS_QUERY_COMPLETION_ROUTINE)(PVOID context, struct DNS_QUERY_RESULT_ *result);

typedef struct DNS_QUERY_RESULT_ {
    ULONG Version;
    DNS_STATUS QueryStatus;
    ULONG64 QueryOptions;
    PDNS_RECORD pQueryRecords;
    PVOID Reserved;
} DNS_QUERY_RESULT;

typedef struct {
    ULONG Version;
    PVOID QueryName;
    WORD QueryType;
    ULONG64 QueryOptions;
    PVOID pDnsServerList;
    ULONG InterfaceIndex;
    DNS_QUERY_COMPLETION_ROUTINE pQueryCompletionCallback;
    PVOID pQueryContext;
    BOOL IsNetworkQueryRequired;
    DWORD RequiredNetworkIndex;
    DWORD cCustomServers;
    PVOID pCustomServers;
} DNS_QUERY_REQUEST;
#ifdef _WIN64
MIME_FIELD_AT(DNS_QUERY_REQUEST, QueryOptions, 24);
MIME_FIELD_AT(DNS_QUERY_REQUEST, pDnsServerList, 32);
MIME_FIELD_AT(DNS_QUERY_REQUEST, pQueryCompletionCallback, 48);
MIME_FIELD_AT(DNS_QUERY_REQUEST, IsNetworkQueryRequired, 64);
MIME_FIELD_AT(DNS_QUERY_REQUEST, pCustomServers, 80);
MIME_FIELD_AT(DNS_QUERY_RESULT, pQueryRecords, 16);
#else
MIME_FIELD_AT(DNS_QUERY_REQUEST, QueryOptions, 16);
MIME_FIELD_AT(DNS_QUERY_REQUEST, pDnsServerList, 24);
MIME_FIELD_AT(DNS_QUERY_REQUEST, pQueryCompletionCallback, 32);
MIME_FIELD_AT(DNS_QUERY_REQUEST, IsNetworkQueryRequired, 40);
MIME_FIELD_AT(DNS_QUERY_REQUEST, pCustomServers, 52);
MIME_FIELD_AT(DNS_QUERY_RESULT, pQueryRecords, 16);
#endif

typedef struct {
    CHAR Reserved[32];
} DNS_QUERY_CANCEL;

/* the servers of an address array, in the form the lookups take. */
typedef struct {
    DWORD MaxCount;
    DWORD AddrCount;
    DWORD Tag;
    WORD Family;
    WORD WordReserved;
    DWORD Flags;
    DWORD MatchFlag;
    DWORD Reserved1;
    DWORD Reserved2;
    BYTE AddrArray[1][64];
} DNS_ADDR_ARRAY;
MIME_FIELD_AT(DNS_ADDR_ARRAY, AddrArray, 32);

#define EXTRA_INFO_MAGIC 0x80000001u

/* ------------------------------------------------------------------ names */

static char *narrow(const WCHAR *name)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    char *text = n > 0 ? DnsAllocate((SIZE_T)n) : nullptr;
    if (text)
        WideCharToMultiByte(CP_UTF8, 0, name, -1, text, n, nullptr, nullptr);
    return text;
}

/* a name in the ansi code page as utf-8. */
static char *from_ansi(const char *name)
{
    int n = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    WCHAR *wide = n > 0 ? DnsAllocate((SIZE_T)n * sizeof(WCHAR)) : nullptr;
    char *text = nullptr;
    if (wide) {
        MultiByteToWideChar(CP_ACP, 0, name, -1, wide, n);
        text = narrow(wide);
    }
    DnsRelease(wide);
    return text;
}

static char *name_in(const void *name, int charset)
{
    if (charset == DnsCharSetUnicode)
        return narrow(name);
    if (charset == DnsCharSetAnsi)
        return from_ansi(name);
    return DnsDuplicate(name);
}

/* ------------------------------------------------------------------ the servers a caller names */

/* the ip4 servers of an IP4_ARRAY, which is what DnsQuery takes as its extra argument. */
static int servers_of_ip4_array(const void *extra, DNS_SERVER *out)
{
    if (!extra || *(const DWORD *)extra == EXTRA_INFO_MAGIC)
        return 0;
    const DWORD *array = extra;
    int count = 0;
    for (DWORD i = 0; i < array[0] && count < DNS_MAX_SERVERS; i++) {
        SOCKADDR_IN in = {.sin_family = AF_INET, .sin_port = htons(53)};
        in.sin_addr.S_un.S_addr = array[1 + i];
        memset(&out[count], 0, sizeof(out[count]));
        memcpy(out[count].address, &in, sizeof(in));
        out[count++].length = sizeof(in);
    }
    return count;
}

static int servers_of_addr_array(const DNS_ADDR_ARRAY *array, DNS_SERVER *out)
{
    int count = 0;
    for (DWORD i = 0; array && i < array->AddrCount && count < DNS_MAX_SERVERS; i++) {
        const SOCKADDR *a = (const SOCKADDR *)array->AddrArray[i];
        memset(&out[count], 0, sizeof(out[count]));
        if (a->sa_family == AF_INET) {
            memcpy(out[count].address, a, sizeof(SOCKADDR_IN));
            out[count].length = sizeof(SOCKADDR_IN);
            if (!((SOCKADDR_IN *)out[count].address)->sin_port)
                ((SOCKADDR_IN *)out[count].address)->sin_port = htons(53);
        } else if (a->sa_family == AF_INET6) {
            memcpy(out[count].address, a, sizeof(SOCKADDR_IN6));
            out[count].length = sizeof(SOCKADDR_IN6);
            if (!((SOCKADDR_IN6 *)out[count].address)->sin6_port)
                ((SOCKADDR_IN6 *)out[count].address)->sin6_port = htons(53);
        } else {
            continue;
        }
        count++;
    }
    return count;
}

/* ------------------------------------------------------------------ DnsQuery */

static DNS_STATUS query(const char *name, int charset, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                        PVOID *message)
{
    if (results)
        *results = nullptr;
    if (message)
        *message = nullptr;
    if (!name)
        return ERROR_INVALID_PARAMETER;
    DNS_SERVER servers[DNS_MAX_SERVERS];
    int server_count = servers_of_ip4_array(extra, servers);
    ULONGLONG flags = options;
    if (message)
        flags |= DNS_QUERY_RETURN_MESSAGE;
    DNS_ANSWER answer;
    DNS_STATUS status = DnsLookup(name, type, flags, servers, server_count, nullptr, &answer);
    if (status == ERROR_SUCCESS && results) {
        *results = DnsRecordsBuild(answer.records, charset, 0);
        if (!*results && answer.records)
            status = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (message && answer.message) {
        PVOID copy = LocalAlloc(LMEM_FIXED, answer.message_length);
        if (copy) {
            memcpy(copy, answer.message, answer.message_length);
            *message = copy;
        }
    }
    DnsAnswerFree(&answer);
    return status;
}

DNS_STATUS WINAPI DnsQuery_A(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                             PVOID *reserved)
{
    if (!name) {
        if (results)
            *results = nullptr;
        return ERROR_INVALID_PARAMETER;
    }
    char *utf8 = from_ansi(name);
    if (!utf8)
        return ERROR_NOT_ENOUGH_MEMORY;
    DNS_STATUS status = query(utf8, DnsCharSetAnsi, type, options, extra, results, reserved);
    DnsRelease(utf8);
    return status;
}

DNS_STATUS WINAPI DnsQuery_UTF8(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                                PVOID *reserved)
{
    return query(name, DnsCharSetUtf8, type, options, extra, results, reserved);
}

DNS_STATUS WINAPI DnsQuery_W(PCWSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                             PVOID *reserved)
{
    if (!name) {
        if (results)
            *results = nullptr;
        return ERROR_INVALID_PARAMETER;
    }
    char *utf8 = narrow(name);
    if (!utf8)
        return ERROR_NOT_ENOUGH_MEMORY;
    DNS_STATUS status = query(utf8, DnsCharSetUnicode, type, options, extra, results, reserved);
    DnsRelease(utf8);
    return status;
}

/* ------------------------------------------------------------------ DnsQueryEx */

typedef struct PENDING {
    struct PENDING *next;
    volatile LONG cancelled;
    char *name;
    WORD type;
    ULONGLONG options;
    int charset;
    DNS_SERVER servers[DNS_MAX_SERVERS];
    int server_count;
    DNS_QUERY_COMPLETION_ROUTINE callback;
    PVOID context;
    DNS_QUERY_RESULT *result;
} PENDING;

static SRWLOCK pending_lock = SRWLOCK_INIT;
static PENDING *pending_list;

static void pending_remove(PENDING *p)
{
    AcquireSRWLockExclusive(&pending_lock);
    for (PENDING **link = &pending_list; *link; link = &(*link)->next) {
        if (*link == p) {
            *link = p->next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&pending_lock);
}

static DWORD WINAPI query_thread(PVOID argument)
{
    PENDING *p = argument;
    DNS_ANSWER answer;
    DNS_STATUS status = DnsLookup(p->name, p->type, p->options, p->servers, p->server_count, &p->cancelled, &answer);
    PDNS_RECORD records = nullptr;
    if (status == ERROR_SUCCESS) {
        records = DnsRecordsBuild(answer.records, p->charset, 0);
        if (!records && answer.records)
            status = ERROR_NOT_ENOUGH_MEMORY;
    }
    DnsAnswerFree(&answer);
    /* from here a cancel finds nothing to cancel. */
    pending_remove(p);
    if (p->cancelled && status == ERROR_SUCCESS) {
        DnsRecordsFree(records);
        records = nullptr;
        status = ERROR_CANCELLED;
    }
    p->result->QueryStatus = status;
    p->result->pQueryRecords = records;
    DNS_QUERY_COMPLETION_ROUTINE callback = p->callback;
    PVOID context = p->context;
    DNS_QUERY_RESULT *result = p->result;
    DnsRelease(p->name);
    DnsRelease(p);
    callback(context, result);
    return 0;
}

static DNS_STATUS query_ex(const DNS_QUERY_REQUEST *request, DNS_QUERY_RESULT *result, DNS_QUERY_CANCEL *cancel,
                           int charset)
{
    if (!request || request->Version < 1 || request->Version > 3 || !request->QueryName)
        return ERROR_INVALID_PARAMETER;
    if (!result || (result->Version != 1 && result->Version != 0xFFFFFFFF))
        return ERROR_INVALID_PARAMETER;
    if (request->Version == 3 && request->pDnsServerList && request->pCustomServers)
        return ERROR_INVALID_PARAMETER;
    result->QueryOptions = request->QueryOptions;
    result->pQueryRecords = nullptr;
    char *name = name_in(request->QueryName, charset);
    if (!name)
        return ERROR_NOT_ENOUGH_MEMORY;
    DNS_SERVER servers[DNS_MAX_SERVERS];
    int server_count = servers_of_addr_array(request->pDnsServerList, servers);

    if (!request->pQueryCompletionCallback) {
        DNS_ANSWER answer;
        DNS_STATUS status = DnsLookup(name, request->QueryType, request->QueryOptions, servers, server_count, nullptr,
                                      &answer);
        if (status == ERROR_SUCCESS) {
            result->pQueryRecords = DnsRecordsBuild(answer.records, charset, 0);
            if (!result->pQueryRecords && answer.records)
                status = ERROR_NOT_ENOUGH_MEMORY;
        }
        DnsAnswerFree(&answer);
        DnsRelease(name);
        result->QueryStatus = status;
        return status;
    }

    PENDING *p = DnsAllocate(sizeof(*p));
    if (!p) {
        DnsRelease(name);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    p->name = name;
    p->type = request->QueryType;
    p->options = request->QueryOptions;
    p->charset = charset;
    memcpy(p->servers, servers, sizeof(servers));
    p->server_count = server_count;
    p->callback = request->pQueryCompletionCallback;
    p->context = request->pQueryContext;
    p->result = result;
    AcquireSRWLockExclusive(&pending_lock);
    p->next = pending_list;
    pending_list = p;
    ReleaseSRWLockExclusive(&pending_lock);
    if (cancel) {
        memset(cancel, 0, sizeof(*cancel));
        memcpy(cancel->Reserved, &p, sizeof(p));
    }
    HANDLE thread = CreateThread(nullptr, 0, query_thread, p, 0, nullptr);
    if (!thread) {
        pending_remove(p);
        DnsRelease(name);
        DnsRelease(p);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    CloseHandle(thread);
    return DNS_REQUEST_PENDING;
}

DNS_STATUS WINAPI DnsQueryEx(DNS_QUERY_REQUEST *request, DNS_QUERY_RESULT *result, DNS_QUERY_CANCEL *cancel)
{
    return query_ex(request, result, cancel, DnsCharSetUnicode);
}

DNS_STATUS WINAPI DnsQueryExW(DNS_QUERY_REQUEST *request, DNS_QUERY_RESULT *result, DNS_QUERY_CANCEL *cancel)
{
    return query_ex(request, result, cancel, DnsCharSetUnicode);
}

DNS_STATUS WINAPI DnsQueryExA(DNS_QUERY_REQUEST *request, DNS_QUERY_RESULT *result, DNS_QUERY_CANCEL *cancel)
{
    return query_ex(request, result, cancel, DnsCharSetAnsi);
}

DNS_STATUS WINAPI DnsQueryExUTF8(DNS_QUERY_REQUEST *request, DNS_QUERY_RESULT *result, DNS_QUERY_CANCEL *cancel)
{
    return query_ex(request, result, cancel, DnsCharSetUtf8);
}

/* a query that has not completed stops and completes with ERROR_CANCELLED. */
DNS_STATUS WINAPI DnsCancelQuery(DNS_QUERY_CANCEL *cancel)
{
    if (!cancel)
        return ERROR_INVALID_PARAMETER;
    PENDING *handle;
    memcpy(&handle, cancel->Reserved, sizeof(handle));
    DNS_STATUS status = ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&pending_lock);
    for (PENDING *p = pending_list; p; p = p->next) {
        if (p == handle) {
            InterlockedExchange(&p->cancelled, 1);
            status = ERROR_SUCCESS;
            break;
        }
    }
    ReleaseSRWLockExclusive(&pending_lock);
    return status;
}

/* ------------------------------------------------------------------ freeing and forgetting */

void WINAPI DnsRecordListFree(PDNS_RECORD list, DNS_FREE_TYPE type)
{
    DnsRecordsFree(list);
}

void WINAPI DnsFree(PVOID data, DNS_FREE_TYPE type)
{
    if (!data)
        return;
    switch (type) {
    case DnsFreeRecordList:
        DnsRecordsFree(data);
        break;
    case DnsFreeParsedMessageFields:
        /* dnsapi never hands out parsed message fields. */
        break;
    default:
        DnsRelease(data);
        break;
    }
}

BOOL WINAPI DnsFlushResolverCache(void)
{
    DnsCacheFlush(nullptr);
    return TRUE;
}

BOOL WINAPI DnsFlushResolverCacheEntry_UTF8(PCSTR name)
{
    if (!name)
        return FALSE;
    DnsCacheFlush(name);
    return TRUE;
}

BOOL WINAPI DnsFlushResolverCacheEntry_A(PCSTR name)
{
    char *utf8 = name ? from_ansi(name) : nullptr;
    BOOL ok = utf8 != nullptr;
    if (ok)
        DnsCacheFlush(utf8);
    DnsRelease(utf8);
    return ok;
}

BOOL WINAPI DnsFlushResolverCacheEntry_W(PCWSTR name)
{
    char *utf8 = name ? narrow(name) : nullptr;
    BOOL ok = utf8 != nullptr;
    if (ok)
        DnsCacheFlush(utf8);
    DnsRelease(utf8);
    return ok;
}

/* ------------------------------------------------------------------ DnsValidateName */

static DNS_STATUS validate(const char *name)
{
    BOOL numeric;
    DNS_STATUS status = DnsCheckName(name, &numeric);
    if (status)
        return status;
    for (const char *p = name; *p; p++) {
        BYTE c = (BYTE)*p;
        BOOL plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '.';
        if (!plain)
            return DNS_ERROR_NON_RFC_NAME;
    }
    return numeric ? DNS_ERROR_NUMERIC_NAME : ERROR_SUCCESS;
}

DNS_STATUS WINAPI DnsValidateName_UTF8(PCSTR name, int format)
{
    return name ? validate(name) : ERROR_INVALID_NAME;
}

DNS_STATUS WINAPI DnsValidateName_A(PCSTR name, int format)
{
    char *utf8 = name ? from_ansi(name) : nullptr;
    DNS_STATUS status = utf8 ? validate(utf8) : ERROR_INVALID_NAME;
    DnsRelease(utf8);
    return status;
}

DNS_STATUS WINAPI DnsValidateName_W(PCWSTR name, int format)
{
    char *utf8 = name ? narrow(name) : nullptr;
    DNS_STATUS status = utf8 ? validate(utf8) : ERROR_INVALID_NAME;
    DnsRelease(utf8);
    return status;
}
