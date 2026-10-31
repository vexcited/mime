/*
 * the dns client: DnsQueryConfig, which reads the names and servers of the machine. the lookups are in dnsquery.c and
 * dnsclient.c.
 */
#include "dns_private.h"
#include <mime/win32/iphlpapi.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
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
        DnsHostName(name, sizeof(name));
        break;
    case DnsConfigFullHostName_W:
    case DnsConfigFullHostName_A:
    case DnsConfigFullHostName_UTF8:
        DnsFullName(name, sizeof(name));
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
