#include "dhcpcore_private.h"
#include <mime/win32/iphlpapi.h>
#include "dhcpcore/dhcpcore.h"

#define TCPIP6_PARAMETERS L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters"
#define IF_TYPE_ETHERNET_CSMACD_ 6
#define IF_TYPE_IEEE80211_ 71
#define DUID_LLT_SIZE 14
#define DUID_MAX 130
/* the seconds the link layer and time identifier of dhcpcore6.dll counts from, which is not midnight of 2000. */
#define DUID_EPOCH 946751400ll
/* what an adapter that found no dhcpv6 server holds, measured on windows 11 26100. */
#define STATE_NO_SERVER 2
#define OPTIONS_VERSION 1
#define OPTIONS_LIMIT 128

static CRITICAL_SECTION g_lock;

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

/* the eleven procedures that need a dhcpv6 client are refused as a server without them refuses. */
static int refuse_unserved(PRPC_MESSAGE message)
{
    if (message->ProcNum != 7 && message->ProcNum != 10)
        RpcRaiseException(RPC_S_PROCNUM_OUT_OF_RANGE);
    return 0;
}

void Dhcpv6Init(void)
{
    static LONG once;
    if (InterlockedExchange(&once, 1))
        return;
    InitializeCriticalSection(&g_lock);
    Dhcpv6_hook = refuse_unserved;
}

/* the adapters the dhcpv6 client of windows keeps a context for: the ones with a hardware address of six bytes. the
 * loopback, the tunnels and the filter adapters answer ERROR_FILE_NOT_FOUND there. */
static BOOL managed(const MIB_IF_ROW2 *row)
{
    return (row->Type == IF_TYPE_ETHERNET_CSMACD_ || row->Type == IF_TYPE_IEEE80211_) && row->PhysicalAddressLength == 6;
}

static void guid_text(const GUID *guid, WCHAR *text)
{
    static const WCHAR digits[] = L"0123456789abcdef";
    const BYTE *b = (const BYTE *)guid;
    static const BYTE order[] = {3, 2, 1, 0, 0xFF, 5, 4, 0xFF, 7, 6, 0xFF, 8, 9, 0xFF, 10, 11, 12, 13, 14, 15};
    *text++ = L'{';
    for (size_t i = 0; i < sizeof(order); i++) {
        if (order[i] == 0xFF) {
            *text++ = L'-';
            continue;
        }
        *text++ = digits[b[order[i]] >> 4];
        *text++ = digits[b[order[i]] & 15];
    }
    *text++ = L'}';
    *text = 0;
}

/*
 * Dhcpv6GenerateDUIDLLT of dhcpcore6.dll: type 1, hardware type 1, the seconds since its epoch and the address of the
 * first adapter with hardware. the identifier is made once and kept as Dhcpv6DUID of the tcpip6 parameters.
 */
static DWORD client_duid(BYTE *duid, DWORD *size)
{
    HKEY key = nullptr;
    DWORD error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, TCPIP6_PARAMETERS, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                                  nullptr, &key, nullptr);
    if (error)
        return error;
    DWORD type = 0, length = DUID_MAX;
    if (!RegQueryValueExW(key, L"Dhcpv6DUID", nullptr, &type, duid, &length) && type == REG_BINARY && length) {
        *size = length;
        RegCloseKey(key);
        return 0;
    }
    MIB_IF_TABLE2 *table = nullptr;
    error = GetIfTable2(&table);
    if (!error) {
        error = ERROR_FILE_NOT_FOUND;
        for (ULONG i = 0; i < table->NumEntries; i++) {
            if (!managed(&table->Table[i]))
                continue;
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            LONGLONG seconds = (LONGLONG)(((ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime) / 10000000) -
                               11644473600ll - DUID_EPOCH;
            duid[0] = 0;
            duid[1] = 1;
            duid[2] = 0;
            duid[3] = 1;
            duid[4] = (BYTE)(seconds >> 24);
            duid[5] = (BYTE)(seconds >> 16);
            duid[6] = (BYTE)(seconds >> 8);
            duid[7] = (BYTE)seconds;
            memcpy(duid + 8, table->Table[i].PhysicalAddress, 6);
            *size = DUID_LLT_SIZE;
            error = RegSetValueExW(key, L"Dhcpv6DUID", 0, REG_BINARY, duid, DUID_LLT_SIZE);
            break;
        }
        FreeMibTable(table);
    }
    RegCloseKey(key);
    return error;
}

/*
 * InitializeInterfaceIaid of dhcpcore6.dll: the low byte of the interface index over the first three bytes of the
 * hardware address. it is made once and kept as Dhcpv6Iaid of the interface, with the state of the client, so it
 * stays when the index changes.
 */
static DWORD interface_iaid(const MIB_IF_ROW2 *row, DWORD *iaid)
{
    WCHAR path[160] = TCPIP6_PARAMETERS L"\\Interfaces\\";
    guid_text(&row->InterfaceGuid, path + wcslen(path));
    HKEY key = nullptr;
    DWORD error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key,
                                  nullptr);
    if (error)
        return error;
    DWORD type = 0, size = sizeof(*iaid), state = STATE_NO_SERVER;
    if (RegQueryValueExW(key, L"Dhcpv6Iaid", nullptr, &type, (BYTE *)iaid, &size) || type != REG_DWORD || !*iaid) {
        *iaid = (row->InterfaceIndex & 0xFF) << 24 | (DWORD)row->PhysicalAddress[0] << 16 |
                (DWORD)row->PhysicalAddress[1] << 8 | row->PhysicalAddress[2];
        error = RegSetValueExW(key, L"Dhcpv6Iaid", 0, REG_DWORD, (const BYTE *)iaid, sizeof(*iaid));
        if (!error)
            error = RegSetValueExW(key, L"Dhcpv6State", 0, REG_DWORD, (const BYTE *)&state, sizeof(state));
    }
    RegCloseKey(key);
    return error;
}

/* RpcSrvQueryLeaseInfov6: the outputs are cleared, then filled for an adapter the client knows. mime runs no dhcpv6
 * exchange, so every adapter is in the state of one that found no server. */
static DWORD query_lease(unsigned short *server, DHCPV6_RPC_LUID *luid, DHCPV6_RPC_LEASE *lease,
                         DHCPV6_RPC_DUID *server_duid, DHCPV6_RPC_DUID *client_duid_out, DHCPV6_RPC_OPTIONS *options,
                         unsigned long *iaid)
{
    if (server && *server)
        return ERROR_NOT_SUPPORTED;
    if (!luid || !lease || !server_duid || !client_duid_out || !options || !iaid)
        return ERROR_INVALID_PARAMETER;
    memset(client_duid_out, 0, sizeof(*client_duid_out));
    memset(server_duid, 0, sizeof(*server_duid));
    memset(options, 0, sizeof(*options));
    MIB_IF_ROW2 row;
    memset(&row, 0, sizeof(row));
    row.InterfaceLuid.Value = luid->Value;
    if (GetIfEntry2(&row) || !managed(&row))
        return ERROR_FILE_NOT_FOUND;
    BYTE duid[DUID_MAX];
    DWORD duid_size = 0, number = 0;
    EnterCriticalSection(&g_lock);
    DWORD error = client_duid(duid, &duid_size);
    if (!error)
        error = interface_iaid(&row, &number);
    LeaveCriticalSection(&g_lock);
    if (error)
        return error;
    client_duid_out->Data = MIDL_user_allocate(duid_size);
    if (!client_duid_out->Data)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(client_duid_out->Data, duid, duid_size);
    client_duid_out->Length = (unsigned short)duid_size;
    memset(lease, 0, sizeof(*lease));
    lease->State = STATE_NO_SERVER;
    options->Version = OPTIONS_VERSION;
    options->Limit = OPTIONS_LIMIT;
    *iaid = number;
    return 0;
}

unsigned long WINAPI Dhcpv6_QueryLeaseInfov6_s(unsigned short *ServerName, DHCPV6_RPC_LUID *Luid, DHCPV6_RPC_LEASE *Lease,
                                               DHCPV6_RPC_DUID *ServerDuid, DHCPV6_RPC_DUID *ClientDuid,
                                               DHCPV6_RPC_OPTIONS *Options, unsigned long *Iaid)
{
    return query_lease(ServerName, Luid, Lease, ServerDuid, ClientDuid, Options, Iaid);
}

/* RpcSrvQueryLeaseInfov6Array: each entry says whether its adapter answered. */
unsigned long WINAPI Dhcpv6_QueryLeaseInfov6Array_s(unsigned short *ServerName, DHCPV6_RPC_LEASE_ARRAY *Array)
{
    if (ServerName && *ServerName)
        return ERROR_NOT_SUPPORTED;
    if (!Array)
        return ERROR_INVALID_PARAMETER;
    for (ULONG i = 0; i < Array->Count; i++) {
        DHCPV6_RPC_LEASE_ENTRY *e = &Array->Entries[i];
        e->Found = !query_lease(ServerName, e->Luid, e->Lease, e->ServerDuid, e->ClientDuid, e->Options, &e->Iaid);
    }
    return 0;
}

#define UNSERVED(name)                                               \
    unsigned long WINAPI Dhcpv6_##name##_s(unsigned short *ServerName) \
    {                                                                \
        return RPC_S_PROCNUM_OUT_OF_RANGE;                           \
    }

UNSERVED(RequestPrefixEx)
UNSERVED(RenewPrefixEx)
UNSERVED(ReleasePrefixEx)
UNSERVED(CancelOperation)
UNSERVED(RequestParams)
UNSERVED(AcquireParametersv6)
UNSERVED(ReleaseParametersv6)
UNSERVED(GetTraceArray)
UNSERVED(SetUserClass)
UNSERVED(EnableDhcpv6)
UNSERVED(RequestCachedParamsEx)
