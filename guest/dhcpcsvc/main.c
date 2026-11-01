/*
 * the dhcp client api. mime runs no dhcp client service, so calls check their arguments like windows, look up the
 * adapter they name, then find no service to ask, which is what windows gives while the service is stopped.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/iphlpapi.h>

#define RPC_S_SERVER_UNAVAILABLE 1722L
#define ERROR_BUFFER_OVERFLOW_ 111L
#define ERROR_ARITHMETIC_RANGE 474L

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

typedef struct {
    ULONG Flags;
    ULONG OptionId;
    BOOL IsVendor;
    LPBYTE Data;
    DWORD nBytesData;
} DHCPCAPI_PARAMS;

typedef struct {
    ULONG nParams;
    DHCPCAPI_PARAMS *Params;
} DHCPCAPI_PARAMS_ARRAY;

typedef struct {
    ULONG Flags;
    LPBYTE Data;
    ULONG nBytesData;
} DHCPCAPI_CLASSID;

/* a client or class id: a pointer and its length. */
typedef struct {
    LPBYTE Data;
    DWORD Length;
} DHCP_UID;

typedef struct {
    BYTE Type;
    DWORD Length;
    LPBYTE Data;
} DHCP_CLIENT_ID;

ULONG_PTR DhcpGlobalIsShuttingDown;
ULONG_PTR DhcpGlobalServiceSyncEvent;
ULONG_PTR DhcpGlobalTerminateEvent;

/* ------------------------------------------------------------------ adapters and the missing service */

typedef ULONG(WINAPI *CONVERT_GUID)(const GUID *, ULONG64 *);
typedef ULONG(WINAPI *CONVERT_NAME)(const WCHAR *, ULONG64 *);
typedef ULONG(WINAPI *GET_ADAPTERS)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

static int hex_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} */
static BOOL parse_guid(const WCHAR *s, GUID *guid)
{
    static const BYTE order[16] = {0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34};
    BYTE bytes[16];
    if (lstrlenW(s) != 38 || s[0] != '{' || s[9] != '-' || s[14] != '-' || s[19] != '-' || s[24] != '-' || s[37] != '}')
        return FALSE;
    for (int i = 0; i < 16; i++) {
        int high = hex_value(s[1 + order[i]]), low = hex_value(s[2 + order[i]]);
        if (high < 0 || low < 0)
            return FALSE;
        bytes[i] = (BYTE)(high << 4 | low);
    }
    guid->Data1 = (ULONG)bytes[0] << 24 | (ULONG)bytes[1] << 16 | (ULONG)bytes[2] << 8 | bytes[3];
    guid->Data2 = (USHORT)(bytes[4] << 8 | bytes[5]);
    guid->Data3 = (USHORT)(bytes[6] << 8 | bytes[7]);
    memcpy(guid->Data4, bytes + 8, 8);
    return TRUE;
}

/* an adapter is named by its guid in braces or by its name, both end as the luid of the interface. */
static DWORD adapter_luid(const WCHAR *name, ULONG64 *luid)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    HMODULE module = LoadLibraryW(L"iphlpapi.dll");
    if (!module)
        return GetLastError();
    DWORD error;
    if (name[0] == '{') {
        GUID guid;
        CONVERT_GUID convert = (CONVERT_GUID)GetProcAddress(module, "ConvertInterfaceGuidToLuid");
        error = !convert ? ERROR_PROC_NOT_FOUND : !parse_guid(name, &guid) ? ERROR_INVALID_PARAMETER : convert(&guid, luid);
    } else {
        CONVERT_NAME convert = (CONVERT_NAME)GetProcAddress(module, "ConvertInterfaceNameToLuidW");
        error = convert ? convert(name, luid) : ERROR_PROC_NOT_FOUND;
    }
    return error;
}

/* what a call that names an adapter and then asks the service gives. */
static DWORD ask_service(const WCHAR *adapter)
{
    ULONG64 luid;
    DWORD error = adapter_luid(adapter, &luid);
    return error ? error : RPC_S_SERVER_UNAVAILABLE;
}

/* a call that asks the service when it is given an adapter and goes to it at once when it is not. */
static DWORD ask_service_optional(const WCHAR *adapter)
{
    return adapter ? ask_service(adapter) : RPC_S_SERVER_UNAVAILABLE;
}

/* ------------------------------------------------------------------ initialization and memory */

DWORD WINAPI DhcpCApiInitialize(LPDWORD version)
{
    if (version)
        *version = 2;
    return ERROR_SUCCESS;
}

void WINAPI DhcpCApiCleanup(void)
{
}

DWORD WINAPI DhcpEnableTracing(ULONG_PTR unused)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/* the memory is a pointer to the block, which is freed and cleared. */
BOOL WINAPI DhcpFreeMem(PVOID *block)
{
    BOOL freed = TRUE;
    if (*block) {
        freed = HeapFree(GetProcessHeap(), 0, *block);
        *block = nullptr;
    }
    return freed;
}

DWORD WINAPI DhcpFreeEnumeratedInterfaces(PVOID list)
{
    struct {
        DWORD count;
        HLOCAL interfaces;
    } *head = list;
    if (head->interfaces) {
        LocalFree(head->interfaces);
        head->interfaces = nullptr;
    }
    head->count = 0;
    return 0;
}

/* the cached data of each parameter is a heap block. */
void WINAPI DhcpFreeCachedParamsData(DHCPCAPI_PARAMS_ARRAY array)
{
    if (!array.Params)
        return;
    for (ULONG i = 0; i < array.nParams; i++) {
        if (array.Params[i].Data) {
            HeapFree(GetProcessHeap(), 0, array.Params[i].Data);
            array.Params[i].Data = nullptr;
        }
        array.Params[i].nBytesData = 0;
    }
}

/* the blocks inside a lease record are local memory, the record is 88 bytes in 32 bits and 120 in 64. */
#define LEASE_SIZE (sizeof(void *) == 8 ? 120 : 88)

static void free_lease_blocks(BYTE *lease)
{
    static const int offsets64[] = {48, 64, 96, 112};
    static const int offsets32[] = {44, 52, 76, 84};
    const int *offsets = sizeof(void *) == 8 ? offsets64 : offsets32;
    for (int i = 0; i < 4; i++) {
        HLOCAL *block = (HLOCAL *)(lease + offsets[i]);
        if (*block) {
            LocalFree(*block);
            *block = nullptr;
        }
    }
}

void WINAPI DhcpFreeLeaseInfo(BYTE *lease)
{
    free_lease_blocks(lease);
    memset(lease, 0, LEASE_SIZE);
}

void WINAPI DhcpFreeLeaseInfoArray(DWORD count, BYTE **array)
{
    if (!array || !*array || !count)
        return;
    for (DWORD i = 0; i < count; i++)
        free_lease_blocks(*array + (SIZE_T)i * LEASE_SIZE);
    HeapFree(GetProcessHeap(), 0, *array);
    *array = nullptr;
}

/* ------------------------------------------------------------------ calls that name an adapter */

DWORD WINAPI DhcpAcquireParameters(WCHAR *adapter)
{
    return adapter ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpAcquireParametersByBroadcast(WCHAR *adapter)
{
    return adapter ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpFallbackRefreshParams(WCHAR *adapter)
{
    return adapter ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpNotifyMediaReconnected(WCHAR *adapter)
{
    return adapter ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpReleaseParameters(WCHAR *adapter)
{
    return adapter ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

/* a refresh with no adapter goes to the service for all of them. */
DWORD WINAPI DhcpStaticRefreshParams(WCHAR *adapter)
{
    return ask_service_optional(adapter);
}

DWORD WINAPI DhcpGetOriginalSubnetMask(WCHAR *adapter, DWORD *mask)
{
    return adapter && mask ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpIsMeteredDetected(WCHAR *adapter, DWORD *metered)
{
    if (!adapter || !metered)
        return ERROR_INVALID_PARAMETER;
    ULONG64 luid;
    DWORD error = adapter_luid(adapter, &luid);
    if (error)
        return error;
    *metered = 0;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI Dhcpv4CheckServerAvailability(WCHAR *adapter, DWORD *available)
{
    if (!adapter || !available)
        return ERROR_INVALID_PARAMETER;
    ULONG64 luid;
    DWORD error = adapter_luid(adapter, &luid);
    if (error)
        return error;
    *available = 0;
    return RPC_S_SERVER_UNAVAILABLE;
}

/* whether DHCP is on for an adapter is the adapter's own flag, which is what Mime's adapters report. */
DWORD WINAPI DhcpIsEnabled(WCHAR *adapter, DWORD *enabled)
{
    if (!adapter || !enabled)
        return ERROR_INVALID_PARAMETER;
    ULONG64 luid;
    DWORD error = adapter_luid(adapter, &luid);
    if (error)
        return error;
    *enabled = 0;
    HMODULE module = LoadLibraryW(L"iphlpapi.dll");
    GET_ADAPTERS get = module ? (GET_ADAPTERS)GetProcAddress(module, "GetAdaptersAddresses") : nullptr;
    if (!get)
        return ERROR_SUCCESS;
    ULONG size = 0;
    ULONG flags = GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (get(AF_UNSPEC, flags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW)
        return ERROR_SUCCESS;
    PIP_ADAPTER_ADDRESSES list = HeapAlloc(GetProcessHeap(), 0, size);
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (get(AF_UNSPEC, flags, nullptr, list, &size) == ERROR_SUCCESS) {
        for (PIP_ADAPTER_ADDRESSES a = list; a; a = a->Next) {
            if (a->Luid.Value == luid) {
                *enabled = (a->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    return ERROR_SUCCESS;
}

/* the service is asked to turn DHCP on, and when it is not running windows tries to start it. */
DWORD WINAPI DhcpEnableDhcp(WCHAR *adapter, DWORD enable)
{
    return ask_service(adapter);
}

DWORD WINAPI DhcpEnableDhcpAdvanced(WCHAR *adapter, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e)
{
    return ask_service(adapter);
}

DWORD WINAPI Dhcpv4EnableDhcpEx(WCHAR *adapter)
{
    if (!adapter)
        return ERROR_INVALID_PARAMETER;
    DWORD enabled = 0;
    DWORD error = DhcpIsEnabled(adapter, &enabled);
    if (error || enabled)
        return error;
    return ask_service(adapter);
}

DWORD WINAPI DhcpGetClassId(WCHAR *adapter, DHCPCAPI_CLASSID *class_id)
{
    if (!adapter || !class_id || (class_id->Data == nullptr) != (class_id->nBytesData == 0))
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

DWORD WINAPI DhcpSetClassId(WCHAR *adapter, DHCPCAPI_CLASSID *class_id)
{
    if (!adapter)
        return ERROR_INVALID_PARAMETER;
    if (class_id) {
        if (class_id->Flags)
            return ERROR_INVALID_PARAMETER;
        if (class_id->nBytesData && class_id->Data && class_id->nBytesData > 0xFF)
            return ERROR_INVALID_PARAMETER;
    }
    return ask_service(adapter);
}

DWORD WINAPI DhcpGetClientId(WCHAR *adapter, DHCP_CLIENT_ID *client_id)
{
    if (!adapter || !client_id || (client_id->Data == nullptr) != (client_id->Length == 0))
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

DWORD WINAPI DhcpSetClientId(WCHAR *adapter, DHCP_CLIENT_ID *client_id)
{
    if (!adapter)
        return ERROR_INVALID_PARAMETER;
    if (client_id && client_id->Length && client_id->Data && client_id->Length > 0xFE)
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

DWORD WINAPI DhcpGetFallbackParams(WCHAR *adapter, PVOID params)
{
    return adapter && params ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpSetFallbackParams(WCHAR *adapter, PVOID params)
{
    return adapter && params ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpRetrieveOptionsViaInform(WCHAR *adapter, ULONG_PTR a, PVOID *options, ULONG_PTR b, ULONG_PTR c)
{
    if (!options)
        return ERROR_INVALID_PARAMETER;
    *options = nullptr;
    if (!adapter || b || c)
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

/* the services of an adapter by the class: no flags, the name of an adapter and a size that fits the buffer. */
DWORD WINAPI DhcpEnumClasses(DWORD reserved, WCHAR *adapter, DWORD *size, PVOID buffer)
{
    if (reserved || !adapter || !size)
        return ERROR_INVALID_PARAMETER;
    if (!buffer && *size)
        return ERROR_INVALID_DATA;
    return ask_service(adapter);
}

DWORD WINAPI DhcpEnumInterfaces(PVOID list)
{
    if (!list)
        return ERROR_INVALID_PARAMETER;
    struct {
        DWORD count;
        HLOCAL interfaces;
    } *head = list;
    head->count = 0;
    head->interfaces = nullptr;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpGetDhcpServicedConnections(ULONG_PTR reserved, PVOID *connections, DWORD *count)
{
    if (!count || !connections || reserved)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpGetNotificationStatus(ULONG_PTR handle, PVOID status)
{
    struct {
        PVOID list;
        DWORD count;
    } *out = status;
    if (!handle || !out || out->count || out->list)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpGetTraceArray(PVOID *trace)
{
    if (!trace || !*trace)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpRemoveDNSRegistrations(void)
{
    return RPC_S_SERVER_UNAVAILABLE;
}

/* a pnp event about an adapter: nothing changed in the version that is asked for, or the class id did. */
DWORD WINAPI DhcpHandlePnPEvent(DWORD flags, DWORD version, WCHAR *adapter, DWORD *change, PVOID reserved)
{
    if (flags || version != 1 || reserved || !change)
        return ERROR_INVALID_PARAMETER;
    if (*change)
        return ERROR_NOT_SUPPORTED;
    return ask_service_optional(adapter);
}

DWORD WINAPI DhcpNotifyConfigChangeEx(WCHAR *server, WCHAR *adapter, BOOL new_address, DWORD index, DWORD address,
                                      DWORD mask, DWORD action, DWORD flags)
{
    if (server || !adapter)
        return ERROR_INVALID_PARAMETER;
    if (action || new_address)
        return ERROR_NOT_SUPPORTED;
    if (index == 0xFFFF && (!mask || !address))
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

DWORD WINAPI DhcpNotifyConfigChange(WCHAR *server, WCHAR *adapter, BOOL new_address, DWORD index, DWORD address,
                                    DWORD mask, DWORD action)
{
    return DhcpNotifyConfigChangeEx(server, adapter, new_address, index, address, mask, action, 3);
}

/* ------------------------------------------------------------------ leases */

DWORD WINAPI DhcpQueryLeaseInfo(WCHAR *adapter, BYTE *lease)
{
    if (!lease)
        return ERROR_INVALID_PARAMETER;
    memset(lease, 0, LEASE_SIZE);
    return adapter ? ask_service(adapter) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI DhcpQueryLeaseInfoEx(WCHAR *adapter, BYTE *lease, DWORD *flags)
{
    if (lease)
        memset(lease, 0, LEASE_SIZE);
    if (flags)
        *flags = 0;
    if (!adapter || !lease || !flags)
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

DWORD WINAPI DhcpQueryLeaseInfoArray(WCHAR **adapters, DWORD count, BYTE **leases)
{
    *leases = nullptr;
    if (!adapters || !count)
        return ERROR_INVALID_PARAMETER;
    for (DWORD i = 0; i < count; i++) {
        ULONG64 luid;
        DWORD error = adapter_luid(adapters[i], &luid);
        if (error)
            return error;
    }
    return RPC_S_SERVER_UNAVAILABLE;
}

/* the client id and class id of a lease request are copied into buffers of 254 and 255 bytes. */
static DWORD lease_request(const DHCP_UID *client, const void *class_id, DWORD class_length)
{
    if (!client || !client->Data || !client->Length)
        return ERROR_INVALID_PARAMETER;
    if (client->Length > 0xFE)
        return ERROR_ARITHMETIC_RANGE;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpLeaseIpAddressEx(ULONG_PTR a, DHCP_UID *client, ULONG_PTR b, ULONG_PTR c, PVOID *lease, ULONG_PTR d,
                                  const void *class_id, DWORD class_length)
{
    if ((class_id == nullptr) != (class_length == 0))
        return ERROR_INVALID_PARAMETER;
    if (class_length > 0xFF)
        return ERROR_INVALID_PARAMETER;
    if (!lease)
        return ERROR_INVALID_PARAMETER;
    return lease_request(client, class_id, class_length);
}

DWORD WINAPI DhcpLeaseIpAddress(ULONG_PTR a, DHCP_UID *client, ULONG_PTR b, ULONG_PTR c, PVOID *lease, ULONG_PTR d)
{
    return DhcpLeaseIpAddressEx(a, client, b, c, lease, d, "RRAS.Microsoft", 14);
}

DWORD WINAPI DhcpReleaseIpAddressLeaseEx(ULONG_PTR a, DHCP_UID *client, const void *class_id, DWORD class_length)
{
    if ((class_id == nullptr) != (class_length == 0) || !client)
        return ERROR_INVALID_PARAMETER;
    if (class_id && class_length > 0xFF)
        return ERROR_ARITHMETIC_RANGE;
    return lease_request(client, class_id, class_length);
}

DWORD WINAPI DhcpReleaseIpAddressLease(ULONG_PTR a, DHCP_UID *client)
{
    return DhcpReleaseIpAddressLeaseEx(a, client, "RRAS.Microsoft", 14);
}

DWORD WINAPI DhcpRenewIpAddressLeaseEx(ULONG_PTR a, DHCP_UID *client, ULONG_PTR b, ULONG_PTR c, const void *class_id,
                                       DWORD class_length)
{
    if ((class_id == nullptr) != (class_length == 0) || !client)
        return ERROR_INVALID_PARAMETER;
    if (class_length > 0xFF)
        return ERROR_INVALID_PARAMETER;
    return lease_request(client, class_id, class_length);
}

DWORD WINAPI DhcpRenewIpAddressLease(ULONG_PTR a, DHCP_UID *client, ULONG_PTR b, ULONG_PTR c)
{
    return DhcpRenewIpAddressLeaseEx(a, client, b, c, "RRAS.Microsoft", 14);
}

/* ------------------------------------------------------------------ options and parameters */

DWORD WINAPI DhcpRequestCachedParams(DWORD reserved, WCHAR *adapter, DHCPCAPI_CLASSID *class_id,
                                     DHCPCAPI_PARAMS_ARRAY params)
{
    if (reserved || !params.nParams || !params.Params)
        return ERROR_INVALID_PARAMETER;
    if (class_id && (class_id->Flags || !class_id->Data || !class_id->nBytesData))
        return ERROR_INVALID_PARAMETER;
    for (ULONG i = 0; i < params.nParams; i++) {
        if (params.Params[i].nBytesData || params.Params[i].Data || params.Params[i].Flags)
            return ERROR_INVALID_PARAMETER;
    }
    return ask_service(adapter);
}

DWORD WINAPI DhcpRequestOptions(WCHAR *adapter, PVOID options, DWORD count, PVOID *values, DWORD *values_size,
                                PVOID *kinds, DWORD *kinds_count)
{
    if (!adapter || !options || !count || !kinds || !kinds_count || !values || !values_size)
        return ERROR_INVALID_PARAMETER;
    if (count >= 0xFF)
        return 1450;
    *values_size = 0;
    *kinds_count = 0;
    *values = nullptr;
    *kinds = nullptr;
    return ask_service(adapter);
}

DWORD WINAPI DhcpRequestParams(DWORD flags, LPVOID reserved, LPWSTR adapter, DHCPCAPI_CLASSID *class_id,
                               DHCPCAPI_PARAMS_ARRAY send, DHCPCAPI_PARAMS_ARRAY receive, LPBYTE buffer,
                               LPDWORD size, LPWSTR request_id)
{
    if (!adapter || !size || !(flags & 3) || (flags & ~3u) || reserved || !receive.nParams || (!buffer && *size))
        return ERROR_INVALID_PARAMETER;
    if (class_id && (class_id->Flags || !class_id->Data || !class_id->nBytesData))
        return ERROR_INVALID_PARAMETER;
    if (!receive.Params || (!send.Params && send.nParams))
        return ERROR_INVALID_PARAMETER;
    /* the persistent request is kept in the registry for the service, which is not there to use it. */
    if (!(flags & 2))
        return ERROR_SUCCESS;
    for (ULONG i = 0; i < receive.nParams; i++) {
        if (receive.Params[i].nBytesData || receive.Params[i].Data)
            return ERROR_INVALID_PARAMETER;
    }
    return ask_service(adapter);
}

/* the request ids the persistent requests were kept under are keys of the registry. */
DWORD WINAPI DhcpDelPersistentRequestParams(WCHAR *adapter, WCHAR *request_id)
{
    if (!request_id || lstrlenW(request_id) >= 256)
        return ERROR_BUFFER_OVERFLOW_;
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    typedef LONG(WINAPI * OPEN)(HKEY, const WCHAR *, DWORD, DWORD, HKEY *);
    typedef LONG(WINAPI * DELETE_TREE)(HKEY, const WCHAR *);
    typedef LONG(WINAPI * QUERY)(HKEY, const WCHAR *, DWORD *, DWORD *, BYTE *, DWORD *);
    typedef LONG(WINAPI * CLOSE)(HKEY);
    OPEN open = module ? (OPEN)GetProcAddress(module, "RegOpenKeyExW") : nullptr;
    DELETE_TREE delete_tree = module ? (DELETE_TREE)GetProcAddress(module, "RegDeleteTreeW") : nullptr;
    QUERY query = module ? (QUERY)GetProcAddress(module, "RegQueryValueExW") : nullptr;
    CLOSE close = module ? (CLOSE)GetProcAddress(module, "RegCloseKey") : nullptr;
    if (!open || !delete_tree || !query || !close)
        return ERROR_PROC_NOT_FOUND;
    HKEY key;
    LONG error = open(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\Dhcp\\Parameters\\Options", 0, 0xF, &key);
    if (error)
        return error;
    delete_tree(key, request_id);
    DWORD type, size = 0;
    error = query(key, L"DhcpOptionLocationList", nullptr, &type, nullptr, &size);
    close(key);
    return error ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS;
}

DWORD WINAPI DhcpUndoRequestParams(DWORD flags, LPVOID reserved, LPWSTR adapter, LPWSTR request_id)
{
    if (flags || reserved || !request_id)
        return ERROR_INVALID_PARAMETER;
    return DhcpDelPersistentRequestParams(adapter, request_id);
}

/* the option numbers a request names are kept as counts of the two kinds, plain and vendor. */
static DWORD check_option_array(DHCPCAPI_PARAMS_ARRAY array)
{
    for (ULONG i = 0; i < array.nParams; i++) {
        if (array.Params[i].nBytesData > 0xFF || (array.Params[i].nBytesData && !array.Params[i].Data))
            return ERROR_INVALID_PARAMETER;
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI DhcpPersistentRequestParams(WCHAR *adapter, LPBYTE class_data, DWORD class_length,
                                         DHCPCAPI_PARAMS *params, DWORD count, ULONG_PTR a, WCHAR *request_id)
{
    if ((class_data == nullptr) != (class_length == 0) || (params == nullptr) != (count == 0) || !adapter)
        return ERROR_INVALID_PARAMETER;
    DWORD plain = 0, vendor = 0, plain_length = 0, vendor_length = 0;
    DHCPCAPI_PARAMS_ARRAY array = {count, params};
    if (check_option_array(array))
        return ERROR_INVALID_PARAMETER;
    for (DWORD i = 0; i < count; i++) {
        if (params[i].OptionId == 55 && !params[i].IsVendor) {
            plain_length = params[i].nBytesData;
            plain++;
        }
        if (params[i].OptionId == 0 && params[i].IsVendor) {
            vendor_length = params[i].nBytesData;
            vendor++;
        }
    }
    if ((!plain && !vendor) || plain > 1 || vendor > 1)
        return ERROR_INVALID_PARAMETER;
    /* with nothing to ask the service only the registry copy is left to make. */
    if (!plain_length && !vendor_length)
        return ERROR_SUCCESS;
    return ask_service(adapter);
}

DWORD WINAPI DhcpSetMSFTVendorSpecificOptions(DWORD kind, WCHAR *adapter, DHCPCAPI_CLASSID *class_id,
                                              DHCPCAPI_PARAMS_ARRAY *array, ULONG_PTR a, ULONG_PTR b)
{
    if (!adapter || kind - 1 > 2 || (class_id && (class_id->Flags || !class_id->Data || !class_id->nBytesData)) ||
        !((array->nParams && array->Params) || kind == 2))
        return ERROR_INVALID_PARAMETER;
    DHCPCAPI_PARAMS_ARRAY params = *array;
    DWORD error = adapter_luid(adapter, &(ULONG64){0});
    if (error)
        return error;
    if (kind == 1) {
        for (ULONG i = 0; i < params.nParams; i++) {
            if (!params.Params[i].nBytesData || !params.Params[i].Data)
                return ERROR_INVALID_PARAMETER;
        }
    }
    return RPC_S_SERVER_UNAVAILABLE;
}

/* ------------------------------------------------------------------ notifications */

DWORD WINAPI DhcpRegisterParamChange(DWORD flags, LPVOID reserved, LPWSTR adapter, DHCPCAPI_CLASSID *class_id,
                                     DHCPCAPI_PARAMS_ARRAY params, LPVOID handle)
{
    if (flags != 1 || reserved || !adapter || !params.nParams || !handle)
        return ERROR_INVALID_PARAMETER;
    DWORD plain = 0, vendor = 0;
    for (ULONG i = 0; i < params.nParams; i++) {
        if (plain >= 0x100 || vendor >= 0x100)
            return ERROR_INVALID_PARAMETER;
        if (params.Params[i].IsVendor)
            vendor++;
        else
            plain++;
    }
    *(PVOID *)handle = nullptr;
    if (class_id && (class_id->Data == nullptr) != (class_id->nBytesData == 0))
        return ERROR_INVALID_PARAMETER;
    return ask_service(adapter);
}

/* nothing was registered with the service, so it is the service that is asked to forget it. */
DWORD WINAPI DhcpDeRegisterParamChange(DWORD flags, LPVOID reserved, LPVOID event)
{
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpDeRegisterOptions(HANDLE handle)
{
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpRegisterOptions(WCHAR *adapter, BYTE *options, DWORD count, PVOID *handle)
{
    if (!handle)
        return ERROR_INVALID_PARAMETER;
    *handle = nullptr;
    if (options == nullptr && count)
        return ERROR_INVALID_PARAMETER;
    if (!count)
        return ERROR_INVALID_PARAMETER;
    return ask_service_optional(adapter);
}

DWORD WINAPI DhcpRegisterConnectionStateNotification(DWORD flags, PVOID *event, PVOID *name)
{
    if (!event || !name || !(flags & 0xF))
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpDeRegisterConnectionStateNotification(DWORD flags, PVOID handle)
{
    if (!handle || !(flags & 0xF))
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

/* the event the client sets when an address changes is made here when no one has made it. */
HANDLE WINAPI DhcpOpenGlobalEvent(void)
{
    HANDLE event = OpenEventW(0x100002, FALSE, L"DHCPNEWIPADDRESS");
    if (event)
        return event;
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    typedef BOOL(WINAPI * INIT_ACL)(PVOID, DWORD, DWORD);
    typedef BOOL(WINAPI * ADD_ACE)(PVOID, DWORD, DWORD, PVOID);
    typedef BOOL(WINAPI * INIT_SD)(PVOID, DWORD);
    typedef BOOL(WINAPI * SET_DACL)(PVOID, BOOL, PVOID, BOOL);
    INIT_ACL init_acl = module ? (INIT_ACL)GetProcAddress(module, "InitializeAcl") : nullptr;
    ADD_ACE add_ace = module ? (ADD_ACE)GetProcAddress(module, "AddAccessAllowedAce") : nullptr;
    INIT_SD init_sd = module ? (INIT_SD)GetProcAddress(module, "InitializeSecurityDescriptor") : nullptr;
    SET_DACL set_dacl = module ? (SET_DACL)GetProcAddress(module, "SetSecurityDescriptorDacl") : nullptr;
    if (!init_acl || !add_ace || !init_sd || !set_dacl) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }
    /* everyone: the world authority with one sub authority of zero. */
    BYTE sid[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    BYTE acl[64], descriptor[40];
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    if (!init_acl(acl, sizeof(acl), 2) || !add_ace(acl, 2, 0x100002, sid) || !init_sd(descriptor, 1) ||
        !set_dacl(descriptor, TRUE, acl, FALSE))
        return nullptr;
    return CreateEventW(&attributes, TRUE, FALSE, L"DHCPNEWIPADDRESS");
}

/* the generated duid of the dhcpv6 client is removed so the next one is made again. */
DWORD WINAPI DhcpClient_Generalize(void)
{
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    typedef LONG(WINAPI * OPEN)(HKEY, const WCHAR *, DWORD, DWORD, HKEY *);
    typedef LONG(WINAPI * DELETE_VALUE)(HKEY, const WCHAR *);
    typedef LONG(WINAPI * CLOSE)(HKEY);
    OPEN open = module ? (OPEN)GetProcAddress(module, "RegOpenKeyExW") : nullptr;
    DELETE_VALUE delete_value = module ? (DELETE_VALUE)GetProcAddress(module, "RegDeleteValueW") : nullptr;
    CLOSE close = module ? (CLOSE)GetProcAddress(module, "RegCloseKey") : nullptr;
    HKEY key;
    if (open && delete_value && close &&
        !open(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\Tcpip6\\Parameters", 0, 0xF003F, &key)) {
        delete_value(key, L"Dhcpv6DUID");
        close(key);
    }
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ multicast address allocation */

typedef struct {
    LPBYTE ClientUID;
    DWORD ClientUIDLength;
} MCAST_CLIENT_UID;

void WINAPI McastApiCleanup(void)
{
}

DWORD WINAPI McastApiStartup(PDWORD version)
{
    return version ? RPC_S_SERVER_UNAVAILABLE : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI McastEnumerateScopes(DWORD family, BOOL requery, PVOID scopes, PDWORD scopes_length, PDWORD scope_count)
{
    if (scope_count)
        *scope_count = 0;
    if (!scopes_length || (scopes && !scope_count) || (!requery && !scopes))
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI McastGenUID(MCAST_CLIENT_UID *id)
{
    if (!id || !id->ClientUID || !id->ClientUIDLength)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI McastReleaseAddress(DWORD family, MCAST_CLIENT_UID *id, PVOID request)
{
    if (!id || !request || !id->ClientUID || !id->ClientUIDLength)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI McastRenewAddress(DWORD family, MCAST_CLIENT_UID *id, PVOID request, PVOID response)
{
    if (!id || !request || !id->ClientUID || !id->ClientUIDLength)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI McastRequestAddress(DWORD family, MCAST_CLIENT_UID *id, PVOID scope, PVOID request, PVOID response)
{
    if (!id || !request || !scope || !id->ClientUID || !id->ClientUIDLength)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}
