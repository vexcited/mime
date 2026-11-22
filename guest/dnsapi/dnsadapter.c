/*
 * the dns configuration iphlpapi asks dnsapi for: the servers, suffix and search list of each adapter for
 * GetAdaptersAddresses, and the machine and interface settings of GetDnsSettings and GetInterfaceDnsSettings. the
 * resolver's configuration is the host's, which the socket driver tells; the adapters that carry it are those with a
 * default route, or with an address wider than the link when no adapter has one. shapes and codes are those of
 * dnsapi of windows 11 26100, measured through iphlpapi's own calls.
 */
#include "dns_private.h"
#include <mime/afd.h>

#define AF_INET 2
#define AF_INET6 23
#define DNS_FLAG_NO_SERVERS 0x8 /* GetAdaptersAddresses was asked to skip the servers */

/* an adapter as iphlpapi describes it. */
typedef struct {
    ULONG64 Luid;
    ULONG TunnelType;
    ULONG OperStatus;
    ULONG Ipv4Enabled;
    ULONG Ipv6Enabled;
    ULONG IfType;
    ULONG ZoneIndices[16];
    ULONG Pad;
} DNS_ADAPTER_QUERY;

typedef struct {
    BYTE Address[32]; /* a sockaddr */
    ULONG Length;
    BYTE Reserved[28];
} DNS_ADDRESS;

typedef struct {
    ULONG MaxCount;
    ULONG AddrCount;
    BYTE Reserved[24];
    DNS_ADDRESS Addresses[];
} DNS_ADDRESS_ARRAY;

typedef struct {
    PWSTR Suffix;
    PWSTR SearchList;
    DNS_ADDRESS_ARRAY *Servers;
    ULONG DdnsEnabled;
    ULONG RegisterSuffix;
} DNS_ADAPTER_ANSWER;

typedef struct {
    ULONG Version;
    ULONG64 Flags;
    PWSTR Hostname;
    PWSTR Domain;
    PWSTR SearchList;
    ULONG64 SettingFlags;
} DNS_MACHINE_SETTINGS;

typedef struct {
    ULONG Version;
    ULONG64 Flags;
    PWSTR Domain;
    PWSTR NameServer;
    PWSTR SearchList;
    ULONG RegistrationEnabled;
    ULONG RegisterAdapterName;
    ULONG EnableLlmnr;
    ULONG QueryAdapterName;
    PWSTR ProfileNameServer;
    ULONG DisableUnconstrainedQueries;
    PWSTR SupplementalSearchList;
    ULONG ServerPropertyCount;
    PVOID ServerProperties;
    ULONG ProfileServerPropertyCount;
    PVOID ProfileServerProperties;
    ULONG64 Reserved[2];
} DNS_ADAPTER_SETTINGS;

MIME_STATIC_ASSERT(sizeof(DNS_ADAPTER_QUERY) == 96 && sizeof(DNS_ADDRESS) == 64, "the adapters of iphlpapi");

static void *allocate(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void release(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* the host's configuration, released with release. */
static MIME_AFD_CONFIG *read_config(void)
{
    static const WCHAR device[] = MIME_AFD_DEVICE;
    UNICODE_STRING name = {sizeof(device) - sizeof(WCHAR), sizeof(device), (PWSTR)device};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    HANDLE afd;
    if (!NT_SUCCESS(NtCreateFile(&afd, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT,
                                 nullptr, 0)))
        return nullptr;
    MIME_AFD_CONFIG *config = nullptr;
    for (ULONG size = 65536; size <= 1u << 20 && !config; size *= 2) {
        MIME_AFD_CONFIG *c = allocate(size);
        if (!c)
            break;
        NTSTATUS status = NtDeviceIoControlFile(afd, nullptr, nullptr, nullptr, &io, IOCTL_AFD_CONFIG, nullptr, 0, c, size);
        if (NT_SUCCESS(status) && io.Information >= sizeof(*c) && c->Size <= io.Information) {
            config = c;
        } else {
            release(c);
            if (status != (NTSTATUS)0x80000005L)
                break;
        }
    }
    NtClose(afd);
    return config;
}

static const MIME_AFD_LINK *links_of(const MIME_AFD_CONFIG *c)
{
    return (const MIME_AFD_LINK *)(c + 1);
}

static const MIME_AFD_LINK_ADDRESS *addresses_of(const MIME_AFD_CONFIG *c)
{
    return (const MIME_AFD_LINK_ADDRESS *)(links_of(c) + c->Links);
}

static const MIME_AFD_ROUTE *routes_of(const MIME_AFD_CONFIG *c)
{
    return (const MIME_AFD_ROUTE *)(addresses_of(c) + c->Addresses);
}

static BOOL link_up(const MIME_AFD_CONFIG *c, ULONG index)
{
    for (ULONG i = 0; i < c->Links; i++) {
        const MIME_AFD_LINK *l = &links_of(c)[i];
        if (l->Index == index)
            return (l->Flags & MIME_AFD_IF_UP) && (l->Flags & MIME_AFD_IF_RUNNING) && !(l->Flags & MIME_AFD_IF_LOOPBACK);
    }
    return FALSE;
}

static BOOL has_default_route(const MIME_AFD_CONFIG *c, ULONG index)
{
    for (ULONG i = 0; i < c->Routes; i++)
        if (routes_of(c)[i].Index == index && !routes_of(c)[i].Prefix)
            return TRUE;
    return FALSE;
}

static BOOL has_wide_address(const MIME_AFD_CONFIG *c, ULONG index)
{
    for (ULONG i = 0; i < c->Addresses; i++) {
        const MIME_AFD_LINK_ADDRESS *a = &addresses_of(c)[i];
        if (a->Index != index)
            continue;
        const BYTE *b = a->Address.Family == AF_INET ? a->Address.Data + 2 : a->Address.Data + 6;
        if (a->Address.Family == AF_INET ? !(b[0] == 169 && b[1] == 254) : !(b[0] == 0xFE && (b[1] & 0xC0) == 0x80))
            return TRUE;
    }
    return FALSE;
}

/* whether the resolver's servers and domains belong to the host link of this index. */
static BOOL carries_dns(const MIME_AFD_CONFIG *c, ULONG index)
{
    if (!link_up(c, index))
        return FALSE;
    if (has_default_route(c, index))
        return TRUE;
    for (ULONG i = 0; i < c->Links; i++)
        if (link_up(c, links_of(c)[i].Index) && has_default_route(c, links_of(c)[i].Index))
            return FALSE;
    return has_wide_address(c, index);
}

/* the network store numbers the luid of a host link 32768 above its index, and the loopback 0. */
static ULONG link_of_luid(ULONG64 luid)
{
    ULONG number = (ULONG)(luid >> 24) & 0xFFFFFF;
    return number >= 32768 ? number - 32768 : 0;
}

static PWSTR wide_copy(const char *text, SIZE_T length)
{
    PWSTR out = allocate((length + 1) * sizeof(WCHAR));
    for (SIZE_T i = 0; out && i < length; i++)
        out[i] = (BYTE)text[i];
    return out;
}

/* the connection specific suffix: the resolver's domain, or else its first search domain. */
static PWSTR suffix_of(const MIME_AFD_CONFIG *c)
{
    const char *from = c->Domain[0] ? c->Domain : c->Search;
    SIZE_T n = 0;
    while (n < sizeof(c->Domain) && from[n] && from[n] != ' ' && from[n] != ',')
        n++;
    return n ? wide_copy(from, n) : nullptr;
}

static void put_server(DNS_ADDRESS_ARRAY *array, ULONG family, const BYTE *bytes, ULONG scope)
{
    DNS_ADDRESS *a = &array->Addresses[array->AddrCount++];
    a->Address[0] = (BYTE)family;
    if (family == AF_INET) {
        memcpy(a->Address + 4, bytes, 4);
        a->Length = 16;
    } else {
        memcpy(a->Address + 8, bytes, 16);
        memcpy(a->Address + 24, &scope, 4);
        a->Length = 28;
    }
    array->MaxCount = array->AddrCount;
}

/* the host's servers of a family, or of both for family 0. */
static DNS_ADDRESS_ARRAY *servers_of(const MIME_AFD_CONFIG *c, ULONG family)
{
    ULONG count = c->Servers < MIME_AFD_MAX_DNS ? c->Servers : MIME_AFD_MAX_DNS;
    DNS_ADDRESS_ARRAY *array = allocate(sizeof(*array) + (count + 3) * sizeof(DNS_ADDRESS));
    if (!array)
        return nullptr;
    for (ULONG i = 0; i < count; i++) {
        const MIME_AFD_ADDRESS *s = &c->Server[i];
        if ((s->Family != AF_INET && s->Family != AF_INET6) || (family && s->Family != family))
            continue;
        ULONG scope = 0;
        if (s->Family == AF_INET6)
            memcpy(&scope, s->Data + 22, 4);
        put_server(array, s->Family, s->Family == AF_INET ? s->Data + 2 : s->Data + 6, scope);
    }
    if (!array->AddrCount) {
        release(array);
        return nullptr;
    }
    return array;
}

/*
 * one answer for each adapter asked about, in the same order. the flags are 64 bits wide, two arguments of a 32-bit
 * caller. an adapter the ip stack is bound to has dynamic
 * updates on. the loopback of windows lists the three site local servers nobody answers at when ipv6 has no other.
 */
DNS_STATUS WINAPI DnsGetAdaptersInfo(USHORT family, ULONG64 flags, ULONG count, const DNS_ADAPTER_QUERY *adapters,
                                     DNS_ADAPTER_ANSWER **answers)
{
    if (!answers || (count && !adapters))
        return ERROR_INVALID_PARAMETER;
    *answers = nullptr;
    DNS_ADAPTER_ANSWER *out = allocate(count * sizeof(*out));
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    MIME_AFD_CONFIG *c = read_config();
    for (ULONG i = 0; i < count; i++) {
        const DNS_ADAPTER_QUERY *a = &adapters[i];
        out[i].DdnsEnabled = a->Ipv4Enabled || a->Ipv6Enabled;
        ULONG link = link_of_luid(a->Luid);
        if (!link) {
            static const BYTE site[16] = {0xFE, 0xC0, 0, 0, 0, 0, 0xFF, 0xFF};
            if (!(a->Luid >> 48 == 24 && a->Ipv6Enabled) || (flags & DNS_FLAG_NO_SERVERS) || family == AF_INET)
                continue;
            DNS_ADDRESS_ARRAY *array = allocate(sizeof(*array) + 3 * sizeof(DNS_ADDRESS));
            for (BYTE k = 1; array && k <= 3; k++) {
                BYTE bytes[16];
                memcpy(bytes, site, 16);
                bytes[15] = k;
                put_server(array, AF_INET6, bytes, 1);
            }
            out[i].Servers = array;
            continue;
        }
        if (!c || !carries_dns(c, link))
            continue;
        out[i].Suffix = suffix_of(c);
        if (flags & DNS_FLAG_NO_SERVERS)
            continue;
        out[i].Servers = servers_of(c, family);
        SIZE_T n = strnlen(c->Search, sizeof(c->Search));
        if (n)
            out[i].SearchList = wide_copy(c->Search, n);
    }
    release(c);
    *answers = out;
    return ERROR_SUCCESS;
}

void WINAPI DnsFreeAdaptersInfo(ULONG count, DNS_ADAPTER_ANSWER **answers)
{
    if (!answers || !*answers)
        return;
    for (ULONG i = 0; i < count; i++) {
        release((*answers)[i].Suffix);
        release((*answers)[i].SearchList);
        release((*answers)[i].Servers);
    }
    release(*answers);
    *answers = nullptr;
}

void WINAPI DnsApiFree(PVOID memory)
{
    release(memory);
}

/* the ip4 servers of the machine, or of the adapter that carries them. */
ULONG DnsConfigServers(const WCHAR *adapter, IP4_ADDRESS *list, ULONG max)
{
    MIME_AFD_CONFIG *c = read_config();
    if (!c)
        return 0;
    BOOL carried = FALSE;
    for (ULONG i = 0; i < c->Links && !carried; i++)
        carried = carries_dns(c, links_of(c)[i].Index);
    if (adapter && carried) {
        /* an adapter is named by its guid, which iphlpapi turns into the luid. */
        typedef DWORD(WINAPI * GUID_TO_LUID)(const GUID *, ULONG64 *);
        typedef LONG(WINAPI * GUID_FROM_STRING)(const UNICODE_STRING *, GUID *);
        HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
        GUID_TO_LUID convert = iphlpapi ? (GUID_TO_LUID)GetProcAddress(iphlpapi, "ConvertInterfaceGuidToLuid") : nullptr;
        GUID_FROM_STRING parse =
            (GUID_FROM_STRING)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGUIDFromString");
        UNICODE_STRING text;
        RtlInitUnicodeString(&text, adapter);
        GUID guid;
        ULONG64 luid = 0;
        carried = convert && parse && parse(&text, &guid) >= 0 && !convert(&guid, &luid) &&
                  carries_dns(c, link_of_luid(luid));
        if (iphlpapi)
            FreeLibrary(iphlpapi);
    }
    ULONG count = 0;
    for (ULONG i = 0; carried && i < c->Servers && i < MIME_AFD_MAX_DNS && count < max; i++)
        if (c->Server[i].Family == AF_INET)
            memcpy(&list[count++], c->Server[i].Data + 2, 4);
    release(c);
    return count;
}

/* ------------------------------------------------------------------ settings */

/* the names of the machine. only the first version of the structure exists for dnsapi, iphlpapi converts the others. */
DNS_STATUS WINAPI DnsGetSettings(DNS_MACHINE_SETTINGS *settings)
{
    if (!settings || settings->Version != 1)
        return ERROR_INVALID_PARAMETER;
    char host[256], domain[256];
    DnsHostName(host, sizeof(host));
    DWORD n = sizeof(domain);
    if (!GetComputerNameExA(ComputerNameDnsDomain, domain, &n))
        domain[0] = 0;
    settings->Hostname = wide_copy(host, strlen(host));
    settings->Domain = wide_copy(domain, strlen(domain));
    settings->SearchList = nullptr;
    /* what windows tells on a machine nobody configured. */
    settings->SettingFlags = 0x81;
    if (!settings->Hostname || !settings->Domain) {
        release(settings->Hostname);
        release(settings->Domain);
        settings->Hostname = settings->Domain = nullptr;
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}

/* the resolver is the host's: its names and servers are set there. */
DNS_STATUS WINAPI DnsSetSettings(const DNS_MACHINE_SETTINGS *settings)
{
    if (!settings || settings->Version != 1)
        return ERROR_INVALID_PARAMETER;
    return ERROR_ACCESS_DENIED;
}

/*
 * the settings of an adapter that has a configuration of its own, which the loopback and the unknown have not: no
 * domain and no server set by hand, registration and name resolution on the link as windows has them by default.
 */
DNS_STATUS WINAPI DnsGetInterfaceSettings(const GUID *adapter, DNS_ADAPTER_SETTINGS *settings)
{
    if (!adapter || !settings || settings->Version != 1)
        return ERROR_INVALID_PARAMETER;
    typedef DWORD(WINAPI * GUID_TO_LUID)(const GUID *, ULONG64 *);
    HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    GUID_TO_LUID convert = iphlpapi ? (GUID_TO_LUID)GetProcAddress(iphlpapi, "ConvertInterfaceGuidToLuid") : nullptr;
    ULONG64 luid = 0;
    BOOL known = convert && !convert(adapter, &luid);
    if (iphlpapi)
        FreeLibrary(iphlpapi);
    if (!known)
        return ERROR_FILE_NOT_FOUND;
    BOOL loopback = luid >> 48 == 24;
    if (loopback && settings->Flags)
        return ERROR_FILE_NOT_FOUND;
    if (!loopback) {
        settings->Domain = wide_copy("", 0);
        if (!settings->Flags)
            settings->NameServer = wide_copy("", 0);
    }
    settings->RegistrationEnabled = 1;
    settings->EnableLlmnr = 1;
    settings->QueryAdapterName = 1;
    return ERROR_SUCCESS;
}

DNS_STATUS WINAPI DnsSetInterfaceSettings(const GUID *adapter, const DNS_ADAPTER_SETTINGS *settings)
{
    if (!adapter || !settings || settings->Version != 1)
        return ERROR_INVALID_PARAMETER;
    return ERROR_ACCESS_DENIED;
}
