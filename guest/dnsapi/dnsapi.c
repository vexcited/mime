/*
 * the dns client. mime has no network and so no dns server. like the
 * windows resolver, localhost and the computer name resolve to the
 * loopback addresses; every other name fails since no server is configured.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/iphlpapi.h>

typedef LONG DNS_STATUS;
typedef DWORD IP4_ADDRESS;

typedef struct {
    DWORD IP6Dword[4];
} IP6_ADDRESS;

typedef struct {
    DWORD Section : 2;
    DWORD Delete : 1;
    DWORD CharSet : 2;
    DWORD Unused : 3;
    DWORD Reserved : 24;
} DNS_RECORD_FLAGS;

typedef struct _DNS_RECORD {
    struct _DNS_RECORD *pNext;
    PVOID pName;
    WORD wType;
    WORD wDataLength;
    union {
        DWORD DW;
        DNS_RECORD_FLAGS S;
    } Flags;
    DWORD dwTtl;
    DWORD dwReserved;
    union {
        IP4_ADDRESS IpAddress;
        IP6_ADDRESS Ip6Address;
        PVOID pNameHost;
    } Data;
} DNS_RECORD, *PDNS_RECORD;
#ifdef _WIN64
MIME_FIELD_AT(DNS_RECORD, Flags, 20);
MIME_FIELD_AT(DNS_RECORD, Data, 32);
#else
MIME_FIELD_AT(DNS_RECORD, Flags, 12);
MIME_FIELD_AT(DNS_RECORD, Data, 24);
#endif

typedef enum {
    DnsFreeFlat = 0,
    DnsFreeRecordList,
    DnsFreeParsedMessageFields
} DNS_FREE_TYPE;

enum { DnsCharSetUnknown, DnsCharSetUnicode, DnsCharSetUtf8, DnsCharSetAnsi };

#define DNSREC_ANSWER 1
#define DNS_TYPE_A 0x0001
#define DNS_TYPE_PTR 0x000C
#define DNS_TYPE_AAAA 0x001C
#define DNS_TYPE_ALL 0x00FF
#define DNS_QUERY_NO_LOCAL_NAME 0x00000020

#define DNS_ERROR_RCODE_NAME_ERROR 9003L
#define DNS_INFO_NO_RECORDS 9501L
#define DNS_ERROR_NO_DNS_SERVERS 9852L

/* the ttl windows gives local name records; guessed. */
#define LOCAL_TTL 1200

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ local names */

static BOOL same_name(const char *name, const char *local)
{
    SIZE_T n = strlen(name), l = strlen(local);
    if (n && name[n - 1] == '.')
        n--;
    return l && n == l && !_strnicmp(name, local, n);
}

static void host_name(char *buffer, DWORD size)
{
    DWORD n = size;
    if (!GetComputerNameExA(ComputerNameDnsHostname, buffer, &n) || !n) {
        n = size;
        if (!GetComputerNameExA(ComputerNameNetBIOS, buffer, &n))
            buffer[0] = 0;
    }
}

static void full_name(char *buffer, DWORD size)
{
    char domain[256];
    DWORD n = sizeof(domain);
    host_name(buffer, size);
    if (GetComputerNameExA(ComputerNameDnsDomain, domain, &n) && n && strlen(buffer) + 1 + n < size) {
        strcat(buffer, ".");
        strcat(buffer, domain);
    }
}

static BOOL is_local_name(const char *name)
{
    char host[256], full[512], netbios[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD n = sizeof(netbios);
    if (same_name(name, "localhost"))
        return TRUE;
    host_name(host, sizeof(host));
    full_name(full, sizeof(full));
    if (same_name(name, host) || same_name(name, full))
        return TRUE;
    return GetComputerNameExA(ComputerNameNetBIOS, netbios, &n) && same_name(name, netbios);
}

/* 1.0.0.127.in-addr.arpa and the ip6.arpa form of ::1. */
static BOOL is_loopback_reverse(const char *name)
{
    static const char v6[] = "1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa";
    if (same_name(name, v6))
        return TRUE;
    /* any address in 127.0.0.0/8: four decimal labels ending with 127. */
    const char *p = name;
    for (int label = 0; label < 3; label++) {
        if (*p < '0' || *p > '9')
            return FALSE;
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p++ != '.')
            return FALSE;
    }
    return same_name(p, "127.in-addr.arpa");
}

/* ------------------------------------------------------------------ records */

static void *heap_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

/* the name is stored after the record, in the query's character set. */
static PDNS_RECORD new_record(const char *name, int charset, WORD type, WORD length)
{
    SIZE_T chars = strlen(name) + 1;
    SIZE_T name_bytes = charset == DnsCharSetUnicode ? chars * sizeof(WCHAR) : chars;
    PDNS_RECORD r = heap_alloc(sizeof(DNS_RECORD) + name_bytes);
    if (!r)
        return nullptr;
    BYTE *storage = (BYTE *)(r + 1);
    if (charset == DnsCharSetUnicode) {
        for (SIZE_T i = 0; i < chars; i++)
            ((WCHAR *)storage)[i] = (WCHAR)(UCHAR)name[i];
    } else {
        memcpy(storage, name, chars);
    }
    r->pName = storage;
    r->wType = type;
    r->wDataLength = length;
    r->Flags.S.Section = DNSREC_ANSWER;
    r->Flags.S.CharSet = charset;
    r->dwTtl = LOCAL_TTL;
    return r;
}

static void free_list(PDNS_RECORD r)
{
    while (r) {
        PDNS_RECORD next = r->pNext;
        HeapFree(GetProcessHeap(), 0, r);
        r = next;
    }
}

/* a ptr record carries its target name in a second block, freed with the record. */
static PDNS_RECORD new_ptr_record(const char *name, int charset, const char *target)
{
    SIZE_T chars = strlen(target) + 1;
    SIZE_T bytes = charset == DnsCharSetUnicode ? chars * sizeof(WCHAR) : chars;
    PDNS_RECORD r = new_record(name, charset, DNS_TYPE_PTR, sizeof(PVOID));
    if (!r)
        return nullptr;
    SIZE_T name_chars = strlen(name) + 1;
    SIZE_T name_bytes = charset == DnsCharSetUnicode ? name_chars * sizeof(WCHAR) : name_chars;
    PDNS_RECORD grown = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, r, sizeof(DNS_RECORD) + name_bytes + bytes);
    if (!grown) {
        free_list(r);
        return nullptr;
    }
    r = grown;
    r->pName = r + 1;
    BYTE *storage = (BYTE *)(r + 1) + name_bytes;
    if (charset == DnsCharSetUnicode) {
        for (SIZE_T i = 0; i < chars; i++)
            ((WCHAR *)storage)[i] = (WCHAR)(UCHAR)target[i];
    } else {
        memcpy(storage, target, chars);
    }
    r->Data.pNameHost = storage;
    return r;
}

/* ------------------------------------------------------------------ queries */

static DNS_STATUS query(const char *name, int charset, WORD type, DWORD options, PDNS_RECORD *results)
{
    if (!name || !results)
        return ERROR_INVALID_PARAMETER;
    *results = nullptr;
    SIZE_T n = strlen(name);
    if (!n || n > 255 || name[0] == '.' || strstr(name, ".."))
        return ERROR_INVALID_NAME;
    if (!(options & DNS_QUERY_NO_LOCAL_NAME)) {
        if (is_local_name(name)) {
            PDNS_RECORD head = nullptr, *tail = &head;
            if (type == DNS_TYPE_AAAA || type == DNS_TYPE_ALL) {
                PDNS_RECORD r = new_record(name, charset, DNS_TYPE_AAAA, sizeof(IP6_ADDRESS));
                if (!r)
                    return ERROR_NOT_ENOUGH_MEMORY;
                r->Data.Ip6Address.IP6Dword[3] = 0x01000000;
                *tail = r;
                tail = &r->pNext;
            }
            if (type == DNS_TYPE_A || type == DNS_TYPE_ALL) {
                PDNS_RECORD r = new_record(name, charset, DNS_TYPE_A, sizeof(IP4_ADDRESS));
                if (!r) {
                    free_list(head);
                    return ERROR_NOT_ENOUGH_MEMORY;
                }
                r->Data.IpAddress = 0x0100007F;
                *tail = r;
            }
            *results = head;
            return head ? ERROR_SUCCESS : DNS_INFO_NO_RECORDS;
        }
        if (is_loopback_reverse(name)) {
            if (type != DNS_TYPE_PTR && type != DNS_TYPE_ALL)
                return DNS_INFO_NO_RECORDS;
            char target[512];
            full_name(target, sizeof(target));
            *results = new_ptr_record(name, charset, target);
            return *results ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    /* single label names would go to multicast resolution, which finds nobody without a network; guessed. */
    if (!strchr(name, '.') || (strchr(name, '.') == name + n - 1))
        return DNS_ERROR_RCODE_NAME_ERROR;
    return DNS_ERROR_NO_DNS_SERVERS;
}

DNS_STATUS WINAPI DnsQuery_A(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                             PVOID *reserved)
{
    return query(name, DnsCharSetAnsi, type, options, results);
}

DNS_STATUS WINAPI DnsQuery_UTF8(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                                PVOID *reserved)
{
    return query(name, DnsCharSetUtf8, type, options, results);
}

DNS_STATUS WINAPI DnsQuery_W(PCWSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD *results,
                             PVOID *reserved)
{
    if (!name || !results)
        return ERROR_INVALID_PARAMETER;
    char narrow[512];
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
    if (n <= 0) {
        *results = nullptr;
        return ERROR_INVALID_NAME;
    }
    /* a non-ascii name is never local, so only ascii names need the unicode copy. */
    for (int i = 0; narrow[i]; i++) {
        if ((UCHAR)narrow[i] >= 0x80) {
            *results = nullptr;
            return strchr(narrow, '.') ? DNS_ERROR_NO_DNS_SERVERS : DNS_ERROR_RCODE_NAME_ERROR;
        }
    }
    return query(narrow, DnsCharSetUnicode, type, options, results);
}

void WINAPI DnsRecordListFree(PDNS_RECORD list, DNS_FREE_TYPE type)
{
    free_list(list);
}

void WINAPI DnsFree(PVOID data, DNS_FREE_TYPE type)
{
    if (!data)
        return;
    switch (type) {
    case DnsFreeRecordList:
        free_list(data);
        break;
    case DnsFreeParsedMessageFields:
        /* dnsapi never hands out parsed message fields. */
        break;
    default:
        HeapFree(GetProcessHeap(), 0, data);
        break;
    }
}

/* ------------------------------------------------------------------ configuration */

enum {
    DnsConfigPrimaryDomainName_W,
    DnsConfigPrimaryDomainName_A,
    DnsConfigPrimaryDomainName_UTF8,
    DnsConfigAdapterDomainName_W,
    DnsConfigAdapterDomainName_A,
    DnsConfigAdapterDomainName_UTF8,
    DnsConfigDnsServerList,
    DnsConfigSearchList,
    DnsConfigAdapterInfo,
    DnsConfigPrimaryHostNameRegistrationEnabled,
    DnsConfigAdapterHostNameRegistrationEnabled,
    DnsConfigAddressRegistrationMaxCount,
    DnsConfigHostName_W,
    DnsConfigHostName_A,
    DnsConfigHostName_UTF8,
    DnsConfigFullHostName_W,
    DnsConfigFullHostName_A,
    DnsConfigFullHostName_UTF8,
};

#define DNS_CONFIG_FLAG_ALLOC 0x00000001

typedef struct {
    DWORD AddrCount;
    IP4_ADDRESS AddrArray[1];
} DNS_IP4_ARRAY;

/* a result is copied to the caller's buffer, or made for the caller when asked to be. */
static DNS_STATUS give(const void *data, DWORD size, DWORD flags, PVOID buffer, PDWORD length)
{
    if (flags & DNS_CONFIG_FLAG_ALLOC) {
        void *copy = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
        if (!copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(copy, data, size);
        *(void **)buffer = copy;
        *length = size;
        return ERROR_SUCCESS;
    }
    DWORD capacity = *length;
    *length = size;
    if (!buffer)
        return ERROR_SUCCESS;
    if (capacity < size)
        return ERROR_MORE_DATA;
    memcpy(buffer, data, size);
    return ERROR_SUCCESS;
}

static BOOL parse_ip4(const char *text, IP4_ADDRESS *address)
{
    BYTE bytes[4];
    for (int i = 0; i < 4; i++) {
        if (*text < '0' || *text > '9')
            return FALSE;
        DWORD value = 0;
        while (*text >= '0' && *text <= '9')
            value = value * 10 + (DWORD)(*text++ - '0');
        if (value > 255 || (i < 3 && *text++ != '.'))
            return FALSE;
        bytes[i] = (BYTE)value;
    }
    memcpy(address, bytes, 4);
    return TRUE;
}

/* the servers the host resolves names with, the ones of ip4 only. */
static DNS_STATUS server_list(DWORD flags, PVOID buffer, PDWORD length)
{
    typedef DWORD(WINAPI * GET_NETWORK_PARAMS)(PFIXED_INFO, PULONG);
    HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    GET_NETWORK_PARAMS get = iphlpapi ? (GET_NETWORK_PARAMS)GetProcAddress(iphlpapi, "GetNetworkParams") : nullptr;
    ULONG size = 0;
    DNS_STATUS status = get ? get(nullptr, &size) : ERROR_NOT_SUPPORTED;
    PFIXED_INFO info = status == ERROR_BUFFER_OVERFLOW ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
    DNS_IP4_ARRAY *list = nullptr;
    SIZE_T bytes = sizeof(DWORD);
    if (info && !(status = get(info, &size))) {
        DWORD count = 0;
        for (IP_ADDR_STRING *entry = &info->DnsServerList; entry; entry = entry->Next)
            count++;
        list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DWORD) * (1 + count));
        for (IP_ADDR_STRING *entry = &info->DnsServerList; list && entry; entry = entry->Next)
            if (parse_ip4(entry->IpAddress.String, &list->AddrArray[list->AddrCount]))
                list->AddrCount++;
        if (list)
            bytes = sizeof(DWORD) * (1 + list->AddrCount);
    } else if (status == ERROR_BUFFER_OVERFLOW) {
        status = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!status && !list)
        status = ERROR_NOT_ENOUGH_MEMORY;
    if (!status && !list->AddrCount)
        status = DNS_ERROR_NO_DNS_SERVERS;
    if (!status)
        status = give(list, (DWORD)bytes, flags, buffer, length);
    if (list)
        HeapFree(GetProcessHeap(), 0, list);
    if (info)
        HeapFree(GetProcessHeap(), 0, info);
    if (iphlpapi)
        FreeLibrary(iphlpapi);
    return status;
}

/*
 * host and domain names in the three character sets, the registration settings, and the servers. the search list and
 * the adapter information are not made, since the resolver here takes names to its own tables.
 */
DNS_STATUS WINAPI DnsQueryConfig(int config, DWORD flags, PWSTR adapter, PVOID reserved, PVOID buffer, PDWORD length)
{
    if (!length || reserved || ((flags & DNS_CONFIG_FLAG_ALLOC) && !buffer))
        return ERROR_INVALID_PARAMETER;
    char name[512];
    BOOL wide = config == DnsConfigPrimaryDomainName_W || config == DnsConfigHostName_W ||
                config == DnsConfigFullHostName_W;
    switch (config) {
    case DnsConfigPrimaryDomainName_W:
    case DnsConfigPrimaryDomainName_A:
    case DnsConfigPrimaryDomainName_UTF8: {
        DWORD n = sizeof(name);
        if (!GetComputerNameExA(ComputerNameDnsDomain, name, &n))
            name[0] = 0;
        break;
    }
    case DnsConfigHostName_W:
    case DnsConfigHostName_A:
    case DnsConfigHostName_UTF8:
        host_name(name, sizeof(name));
        break;
    case DnsConfigFullHostName_W:
    case DnsConfigFullHostName_A:
    case DnsConfigFullHostName_UTF8:
        full_name(name, sizeof(name));
        break;
    case DnsConfigDnsServerList:
        return server_list(flags, buffer, length);
    case DnsConfigPrimaryHostNameRegistrationEnabled:
    case DnsConfigAdapterHostNameRegistrationEnabled: {
        DWORD enabled = 1;
        return give(&enabled, sizeof(enabled), flags, buffer, length);
    }
    case DnsConfigAddressRegistrationMaxCount: {
        DWORD most = 1;
        return give(&most, sizeof(most), flags, buffer, length);
    }
    case DnsConfigAdapterDomainName_W:
    case DnsConfigAdapterDomainName_A:
    case DnsConfigAdapterDomainName_UTF8:
    case DnsConfigSearchList:
    case DnsConfigAdapterInfo:
        return ERROR_NOT_SUPPORTED;
    default:
        return ERROR_INVALID_PARAMETER;
    }
    if (!wide)
        return give(name, (DWORD)strlen(name) + 1, flags, buffer, length);
    WCHAR text[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, text, ARRAYSIZE(text));
    return give(text, n > 0 ? (DWORD)n * sizeof(WCHAR) : sizeof(WCHAR), flags, buffer, length);
}
