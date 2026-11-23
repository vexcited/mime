/*
 * the dns client: DnsQueryConfig, which reads the names and servers of the machine. the lookups are in dnsquery.c and
 * dnsclient.c.
 */
#include "dns_private.h"

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

/* a result is copied to the caller's buffer, or made for the caller when asked to be. */
static DNS_STATUS give(const void *data, DWORD size, DWORD flags, PVOID buffer, PDWORD length)
{
    if (flags & DNS_CONFIG_FLAG_ALLOC) {
        /* the buffer is a pointer the result goes to, and must be as long as one. */
        if (*length < sizeof(void *)) {
            *length = sizeof(void *);
            return ERROR_MORE_DATA;
        }
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

/* the ip4 servers the host resolves names with, or those of the adapter that carries them. */
static DNS_STATUS server_list(DWORD flags, const WCHAR *adapter, PVOID buffer, PDWORD length)
{
    struct {
        DWORD AddrCount;
        IP4_ADDRESS AddrArray[MIME_DNS_MAX_CONFIG_SERVERS];
    } list;
    list.AddrCount = DnsConfigServers(adapter, list.AddrArray, MIME_DNS_MAX_CONFIG_SERVERS);
    if (!list.AddrCount) {
        /* windows answers for an adapter with none as if it had run out of memory. */
        *length = 0;
        return adapter ? ERROR_OUTOFMEMORY : DNS_ERROR_NO_DNS_SERVERS;
    }
    return give(&list, sizeof(DWORD) * (1 + list.AddrCount), flags, buffer, length);
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
        /* a machine of no domain has no name to give, which windows tells with this code. */
        if (!name[0]) {
            *length = 0;
            return ERROR_OUTOFMEMORY;
        }
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
        return server_list(flags, adapter, buffer, length);
    case DnsConfigPrimaryHostNameRegistrationEnabled:
    case DnsConfigAdapterHostNameRegistrationEnabled:
    case DnsConfigAddressRegistrationMaxCount:
        /* the registration settings are not there to read on windows 11. */
        *length = 0;
        return ERROR_FILE_NOT_FOUND;
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
