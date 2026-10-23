/*
 * iphlpapi internals: a snapshot of the machine's adapters, addresses and routes, built from the host's
 * configuration plus the software loopback windows always has.
 */
#ifndef MIME_IPHLPAPI_PRIVATE_H
#define MIME_IPHLPAPI_PRIVATE_H

#define MIME_BUILD_IPHLPAPI
#include <mime/win32/iphlpapi.h>
#include <mime/win32/winnls.h>
#include <mime/afd.h>

#define LOOPBACK_INDEX 1
#define LOOPBACK_METRIC 75
#define ROUTE_METRIC 256
#define INFINITE_LIFETIME 0xFFFFFFFFu

#define RPC_S_SERVER_UNAVAILABLE 1722L

#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)

typedef struct {
    ADDRESS_FAMILY family;
    BYTE bytes[16];
    ULONG scope; /* sin6_scope_id, ipv6 only. */
    UINT8 prefix;
    ULONG index; /* adapter index. */
    ULONG flags; /* MIME_AFD_ADDR_* */
    ULONG valid;
    ULONG preferred;
    NL_PREFIX_ORIGIN prefix_origin;
    NL_SUFFIX_ORIGIN suffix_origin;
    NL_DAD_STATE dad;
    BOOL eligible; /* registered in dns: preferred, not temporary, wider than the link. */
} IPH_ADDRESS;

typedef struct {
    ADDRESS_FAMILY family;
    BYTE destination[16];
    UINT8 prefix;
    BYTE next_hop[16];
    ULONG next_scope;
    ULONG index;
    ULONG metric;
    NL_ROUTE_PROTOCOL protocol;
    NL_ROUTE_ORIGIN origin;
    BOOLEAN loopback;
    BOOLEAN immortal;
} IPH_ROUTE;

typedef struct {
    ULONG index;
    NET_LUID luid;
    GUID guid;
    GUID network;
    char name[40]; /* the guid in braces. */
    WCHAR alias[64];
    WCHAR description[96];
    char host[16];
    IFTYPE type;
    ULONG mtu;
    ULONG64 speed;
    BYTE mac[8];
    ULONG mac_length;
    BOOL admin_up;
    BOOL up;
    BOOL hardware;
    BOOL ipv4;
    BOOL ipv6;
    ULONG metric;
    BOOL dhcp;
    BOOL dns; /* the resolver's servers and domains belong to this adapter. */
    ULONG zones[16];
    ULONG first_address, address_count; /* ipv6 then ipv4, dns eligible first within each. */
} IPH_ADAPTER;

typedef struct {
    LONG refs;
    ULONGLONG taken;
    BOOL from_host;
    ULONG adapter_count;
    IPH_ADAPTER *adapters; /* loopback first. */
    ULONG address_count;
    IPH_ADDRESS *addresses;
    ULONG route_count;
    IPH_ROUTE *routes; /* ipv4 then ipv6, by destination. */
    ULONG multicast_count;
    IPH_ADDRESS *multicasts;
    ULONG server_count;
    SOCKADDR_INET servers[MIME_AFD_MAX_DNS];
    char domain[256];
    char search[256];
    LARGE_INTEGER created; /* creation time reported for every address. */
} IPH_SNAPSHOT;

/* ------------------------------------------------------------------ iphlpapi.c */

static inline void *IphAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static inline void IphFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* a snapshot at most half a second old, release it with IphRelease. never NULL. */
IPH_SNAPSHOT *IphAcquire(void);
/* a new snapshot of the host, for change detection. */
IPH_SNAPSHOT *IphFresh(void);
void IphRelease(IPH_SNAPSHOT *s);
HANDLE IphDevice(void);
NTSTATUS IphDeviceControl(ULONG code, void *output, ULONG out_len, ULONG_PTR *information);

const IPH_ADAPTER *IphAdapterByIndex(const IPH_SNAPSHOT *s, ULONG index);
const IPH_ADAPTER *IphAdapterByLuid(const IPH_SNAPSHOT *s, const NET_LUID *luid);
BOOL IphFamilyEnabled(const IPH_ADAPTER *a, ADDRESS_FAMILY family);
BOOL IphValidFamily(ADDRESS_FAMILY family);
BOOL IphFamilyWanted(ULONG family, ADDRESS_FAMILY f);
void IphSockaddr(SOCKADDR_INET *sa, ADDRESS_FAMILY family, const BYTE *bytes, ULONG scope);
SIZE_T IphSockaddrSize(ADDRESS_FAMILY family);
int IphAddressScope(ADDRESS_FAMILY family, const BYTE *bytes);
/* sin6_scope_id for an adapter address: its zone at the address's scope, 0 for ::1 and ipv4. */
ULONG IphZone(const IPH_ADAPTER *a, ADDRESS_FAMILY family, const BYTE *bytes);
void IphIpv4String(char *out, const BYTE *bytes);
ULONG IphIpv4Mask(UINT8 prefix);
void IphHint(const IPH_SNAPSHOT *s, const IPH_ADAPTER *only, NL_NETWORK_CONNECTIVITY_HINT *hint);
/* buffer table size: 12 bytes over the rows, plus room for growth when the caller's is too small. */
ULONG IphTableSize(ULONG rows, ULONG row, BOOL grow);
DWORD IphCheckTable(ULONG rows, ULONG row, const void *table, PULONG size);

/* ------------------------------------------------------------------ route.c */

/* the route to a destination and the source address the host would pick, or a win32 error. */
DWORD IphBestRoute(const IPH_SNAPSHOT *s, const NET_LUID *luid, ULONG index, const SOCKADDR_INET *source,
                   const SOCKADDR_INET *destination, const IPH_ROUTE **route, const IPH_ADDRESS **from);
void IphRouteRow(const IPH_SNAPSHOT *s, const IPH_ROUTE *r, MIB_IPFORWARD_ROW2 *row);

/* ------------------------------------------------------------------ tables.c */

void IphUnicastRow(const IPH_SNAPSHOT *s, const IPH_ADDRESS *a, MIB_UNICASTIPADDRESS_ROW *row);
void IphInterfaceRow(const IPH_ADAPTER *a, ADDRESS_FAMILY family, MIB_IPINTERFACE_ROW *row);



#endif
