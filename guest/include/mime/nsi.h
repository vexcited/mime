/*
 * the network store interface, \Device\Nsi. nsi.dll sends it one request structure per call with a device control of
 * method neither, the structures the kernel reads and writes in the caller's memory. each module of the store has
 * tables, an object of a table has a key and up to three structures: the parameters that can be set, the dynamic ones
 * and the static ones. sizes and layouts are those of windows 11 26100, read in nsiproxy.sys, netio.sys and
 * iphlpapi.dll and measured through nsi.dll.
 */
#ifndef MIME_NSI_H
#define MIME_NSI_H

#include <mime/nt.h>

#define MIME_NSI_DEVICE L"\\Device\\Nsi"

#define IOCTL_NSI_GET_PARAMETER 0x120007
#define IOCTL_NSI_SET_PARAMETER 0x12000B
#define IOCTL_NSI_GET_ALL_PARAMETERS 0x12000F
#define IOCTL_NSI_SET_ALL_PARAMETERS 0x120013
#define IOCTL_NSI_ENUMERATE_ALL 0x12001B
#define IOCTL_NSI_REGISTER_CHANGE 0x12001F
#define IOCTL_NSI_DEREGISTER_CHANGE 0x120023
#define IOCTL_NSI_GET_SECURITY 0x120037
#define IOCTL_NSI_REQUEST_CHANGE 0x12003F
#define IOCTL_NSI_CANCEL_CHANGE 0x120040
#define IOCTL_NSI_ENUMERATE_PERSISTENT 0x120047
#define IOCTL_NSI_GET_PERSISTENT 0x12004B
#define IOCTL_NSI_SET_PERSISTENT 0x12004F

/*
 * mime's own request, for its nsi service: every object of a table of the active store as its key and a hash of the
 * rest of it, counters apart. two answers tell which objects came, went and changed between them. the input is
 * NSI_MIME_OBJECTS_QUERY, the output NSI_MIME_OBJECTS; a buffer too small gets the two counts and the status of an
 * overflow.
 */
#define IOCTL_NSI_MIME_OBJECTS 0x122403

/* the stores of a request: the persistent one is the registry, the active one the running stack. */
#define NSI_STORE_PERSISTENT 0
#define NSI_STORE_ACTIVE 1

/* the structure a parameter is read from. */
#define NSI_PARAM_RW 0
#define NSI_PARAM_DYNAMIC 1
#define NSI_PARAM_STATIC 2

typedef struct NSI_MODULEID {
    USHORT Length; /* 24 */
    USHORT Pad;
    ULONG Type; /* 1 a guid, 2 an interface luid */
    GUID Guid;
} NSI_MODULEID;

typedef struct NSI_MIME_OBJECTS_QUERY {
    NSI_MODULEID Module;
    ULONG Table;
} NSI_MIME_OBJECTS_QUERY;

typedef struct NSI_MIME_OBJECTS {
    ULONG KeySize;
    ULONG Count;
    UCHAR Objects[]; /* Count times a key of KeySize bytes and its 8 byte hash */
} NSI_MIME_OBJECTS;

/* every module's guid is eb004aXX-9b1a-11d4-9123-0050047759bc. */
#define NSI_MODULE_IPV4 0x00
#define NSI_MODULE_IPV6 0x01
#define NSI_MODULE_UDP 0x02
#define NSI_MODULE_TCP 0x03
#define NSI_MODULE_NDIS 0x11

/* the requests of a 64-bit caller. a 32-bit caller sends the same fields with 4 byte pointers and no padding. */
typedef struct NSI_ENUMERATE_ALL {
    ULONG64 Reserved[2];
    ULONG64 Module;
    ULONG64 Table;
    ULONG Store;
    ULONG Action; /* 1 or 2 */
    ULONG64 Key;
    ULONG64 KeySize;
    ULONG64 Rw;
    ULONG64 RwSize;
    ULONG64 Dynamic;
    ULONG64 DynamicSize;
    ULONG64 Static;
    ULONG64 StaticSize;
    ULONG64 Count;
} NSI_ENUMERATE_ALL;

typedef struct NSI_GET_ALL_PARAMETERS {
    ULONG64 Reserved[2];
    ULONG64 Module;
    ULONG64 Table;
    ULONG Store;
    ULONG Action;
    ULONG64 Key;
    ULONG64 KeySize;
    ULONG64 Rw;
    ULONG64 RwSize;
    ULONG64 Dynamic;
    ULONG64 DynamicSize;
    ULONG64 Static;
    ULONG64 StaticSize;
} NSI_GET_ALL_PARAMETERS;

typedef struct NSI_GET_PARAMETER {
    ULONG64 Reserved[2];
    ULONG64 Module;
    ULONG64 Table;
    ULONG Store;
    ULONG Action;
    ULONG64 Key;
    ULONG64 KeySize;
    ULONG64 Type; /* NSI_PARAM_* */
    ULONG64 Data;
    ULONG DataSize;
    ULONG DataOffset;
} NSI_GET_PARAMETER;

typedef struct NSI_REQUEST_CHANGE {
    ULONG64 ModuleHandle;
    ULONG64 Module;
    ULONG64 Table;
    ULONG64 Overlapped; /* the request is found again by its apc context, which is this */
    ULONG64 Handle;
} NSI_REQUEST_CHANGE;

MIME_STATIC_ASSERT(sizeof(NSI_ENUMERATE_ALL) == 0x70 && sizeof(NSI_GET_ALL_PARAMETERS) == 0x68 &&
                       sizeof(NSI_GET_PARAMETER) == 0x50 && sizeof(NSI_REQUEST_CHANGE) == 0x28,
                   "nsi requests");

/* ------------------------------------------------------------------ the ndis module */

#define NSI_NDIS_IFINFO 0        /* key the luid: rw 1092, dynamic 216, static 600 */
#define NSI_NDIS_INTERFACE 1     /* key the luid: dynamic 656, static 568 */
#define NSI_NDIS_INDEX 2         /* key the index: static the luid, a parameter only */
#define NSI_NDIS_NETWORK 6       /* key the network guid: rw 528 */
#define NSI_NDIS_COMPARTMENT 7   /* key the compartment: rw 1640 */
#define NSI_NDIS_THREAD 8        /* no key: rw the compartment and the scope of the thread */
#define NSI_NDIS_SESSION 9       /* key the session: rw its compartment */
#define NSI_NDIS_COMPARTMENT_GUID 11 /* key the guid: static the compartment, a parameter only */
#define NSI_NDIS_IFINFO_ALL 12   /* as 0 */
#define NSI_NDIS_INTERFACE_ALL 13 /* as 1 */
#define NSI_NDIS_STACK 14        /* key the higher and the lower index */
#define NSI_NDIS_JOB 16          /* key the job: rw its compartment */
#define NSI_NDIS_TABLES 17

#pragma pack(push, 1)
typedef struct NSI_COUNTED_STRING {
    USHORT Length; /* bytes, without the terminator */
    WCHAR String[257];
} NSI_COUNTED_STRING;

typedef struct NSI_PHYSICAL_ADDRESS {
    USHORT Length;
    UCHAR Address[32];
} NSI_PHYSICAL_ADDRESS;

typedef struct NSI_NDIS_IFINFO_RW {
    GUID NetworkGuid;
    ULONG AdminStatus;
    NSI_COUNTED_STRING Alias;
    NSI_PHYSICAL_ADDRESS PhysicalAddress;
    UCHAR Reserved[522];
} NSI_NDIS_IFINFO_RW;

typedef struct NSI_NDIS_IFINFO_DYNAMIC {
    ULONG OperStatus;
    ULONG Flags; /* bit 1 the media is not connected */
    ULONG MediaConnectState;
    ULONG MediaDuplexState;
    ULONG Mtu;
    ULONG Pad;
    ULONG64 TransmitSpeed;
    ULONG64 ReceiveSpeed;
    ULONG64 InErrors;
    ULONG64 InDiscards;
    ULONG64 OutErrors;
    ULONG64 OutDiscards;
    ULONG64 Unknown;
    ULONG64 InOctets;
    ULONG64 InUcastPkts;
    ULONG64 InMulticastPkts;
    ULONG64 InBroadcastPkts;
    ULONG64 OutOctets;
    ULONG64 OutUcastPkts;
    ULONG64 OutMulticastPkts;
    ULONG64 OutBroadcastPkts;
    ULONG64 Unknown2[2];
    ULONG64 InUcastOctets;
    ULONG64 InMulticastOctets;
    ULONG64 InBroadcastOctets;
    ULONG64 OutUcastOctets;
    ULONG64 OutMulticastOctets;
    ULONG64 OutBroadcastOctets;
    ULONG CompartmentId;
    ULONG Pad2;
} NSI_NDIS_IFINFO_DYNAMIC;

typedef struct NSI_NDIS_IFINFO_STATIC {
    ULONG Index;
    NSI_COUNTED_STRING Description;
    ULONG Type;
    ULONG AccessType;
    ULONG DirectionType;
    ULONG ConnectionType;
    GUID Guid;
    USHORT ConnectorPresent;
    NSI_PHYSICAL_ADDRESS PermanentPhysicalAddress;
    ULONG Flags; /* bit 0 hardware, bit 1 a filter */
    ULONG MediaType;
    ULONG PhysicalMediumType;
} NSI_NDIS_IFINFO_STATIC;

typedef struct NSI_NDIS_INTERFACE_DYNAMIC {
    ULONG CompartmentId;
    GUID NetworkGuid;
    NSI_COUNTED_STRING Alias;
    ULONG OperStatus;
    ULONG Flags;
    ULONG Mtu;
    NSI_PHYSICAL_ADDRESS PhysicalAddress;
    NSI_PHYSICAL_ADDRESS PermanentPhysicalAddress;
    ULONG MediaConnectState;
    ULONG Pad;
    ULONG64 TransmitSpeed;
    ULONG64 ReceiveSpeed;
    ULONG Unknown;
    ULONG AdminStatus;
    ULONG MediaDuplexState;
    ULONG Unknown2;
} NSI_NDIS_INTERFACE_DYNAMIC;

typedef struct NSI_NDIS_INTERFACE_STATIC {
    ULONG Index;
    NSI_COUNTED_STRING Description;
    ULONG Type;
    ULONG AccessType;
    ULONG DirectionType;
    ULONG ConnectionType;
    GUID Guid;
    ULONG ConnectorPresent;
    ULONG Flags;
    ULONG MediaType;
    ULONG PhysicalMediumType;
} NSI_NDIS_INTERFACE_STATIC;
#pragma pack(pop)

MIME_STATIC_ASSERT(sizeof(NSI_NDIS_IFINFO_RW) == 1092 && sizeof(NSI_NDIS_IFINFO_DYNAMIC) == 216 &&
                       sizeof(NSI_NDIS_IFINFO_STATIC) == 600 && sizeof(NSI_NDIS_INTERFACE_DYNAMIC) == 656 &&
                       sizeof(NSI_NDIS_INTERFACE_STATIC) == 568,
                   "ndis interface structures");

/* ------------------------------------------------------------------ the ipv4 and ipv6 modules */

#define NSI_IP_BEST_ROUTE 0   /* key the destination: dynamic the route and the source address */
#define NSI_IP_COMPARTMENT 2
#define NSI_IP_ICMP_STATS 3
#define NSI_IP_STATS 6
#define NSI_IP_INTERFACE 7    /* key the luid: rw 240, dynamic 88, static 32 */
#define NSI_IP_ANYCAST 8
#define NSI_IP_MULTICAST 9
#define NSI_IP_UNICAST 10
#define NSI_IP_NEIGHBOR 11
#define NSI_IP_PATH 12
#define NSI_IP_FORWARD 16
#define NSI_IP_RESOLVE_NEIGHBOR 20
#define NSI_IP_TABLES 37

#pragma pack(push, 1)
typedef struct NSI_IPV4_UNICAST_KEY {
    ULONG64 Luid;
    UCHAR Address[4];
    ULONG Pad;
} NSI_IPV4_UNICAST_KEY;

typedef struct NSI_IPV6_UNICAST_KEY {
    ULONG64 Luid;
    UCHAR Address[16];
} NSI_IPV6_UNICAST_KEY;

typedef struct NSI_IP_UNICAST_RW {
    ULONG PreferredLifetime;
    ULONG ValidLifetime;
    ULONG PrefixOrigin;
    ULONG SuffixOrigin;
    ULONG OnLinkPrefixLength;
    ULONG Unknown[2];
} NSI_IP_UNICAST_RW;

typedef struct NSI_IP_UNICAST_DYNAMIC {
    ULONG ScopeId;
    ULONG DadState;
} NSI_IP_UNICAST_DYNAMIC;

typedef struct NSI_IP_UNICAST_STATIC {
    ULONG64 CreationTime;
} NSI_IP_UNICAST_STATIC;

typedef struct NSI_IP_MULTICAST_DYNAMIC {
    ULONG Unknown[3];
} NSI_IP_MULTICAST_DYNAMIC;

typedef struct NSI_IPV4_FORWARD_KEY {
    ULONG CompartmentId;
    UCHAR Prefix[4];
    UCHAR PrefixLength;
    UCHAR Pad[3];
    ULONG Unknown[3];
    ULONG64 Luid;
    ULONG64 Luid2;
    UCHAR NextHop[4];
    ULONG Pad2;
} NSI_IPV4_FORWARD_KEY;

typedef struct NSI_IPV6_FORWARD_KEY {
    ULONG CompartmentId;
    UCHAR Prefix[16];
    UCHAR PrefixLength;
    UCHAR Pad[3];
    ULONG Unknown[3];
    ULONG Pad2;
    ULONG64 Luid;
    ULONG64 Luid2;
    UCHAR NextHop[16];
} NSI_IPV6_FORWARD_KEY;

typedef struct NSI_IP_FORWARD_RW {
    ULONG SitePrefixLength;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG Metric;
    ULONG Protocol;
    UCHAR Loopback;
    UCHAR AutoconfigureAddress;
    UCHAR Publish;
    UCHAR Immortal;
    UCHAR Unknown[4];
    ULONG Unknown2;
} NSI_IP_FORWARD_RW;

typedef struct NSI_IPV4_FORWARD_DYNAMIC {
    ULONG Age;
    ULONG Unknown[3];
    UCHAR Address[4];
} NSI_IPV4_FORWARD_DYNAMIC;

typedef struct NSI_IPV6_FORWARD_DYNAMIC {
    ULONG Age;
    ULONG Unknown[3];
    UCHAR Address[16];
} NSI_IPV6_FORWARD_DYNAMIC;

typedef struct NSI_IP_FORWARD_STATIC {
    ULONG Origin;
    ULONG Index;
} NSI_IP_FORWARD_STATIC;

/* the key of a best route question and its answer, the dynamic structure. */
typedef struct NSI_IPV4_BEST_ROUTE_KEY {
    ULONG CompartmentId;
    UCHAR Destination[4];
    ULONG DestinationScope;
    UCHAR Source[4];
    ULONG SourceScope;
    ULONG Pad;
    ULONG64 Luid;
} NSI_IPV4_BEST_ROUTE_KEY;

typedef struct NSI_IPV6_BEST_ROUTE_KEY {
    ULONG CompartmentId;
    UCHAR Destination[16];
    ULONG DestinationScope;
    UCHAR Source[16];
    ULONG SourceScope;
    ULONG Pad;
    ULONG64 Luid;
} NSI_IPV6_BEST_ROUTE_KEY;

typedef struct NSI_IPV4_BEST_ROUTE {
    NSI_IPV4_FORWARD_KEY Key;
    UCHAR Rw[28]; /* NSI_IP_FORWARD_RW without its last field */
    ULONG Age;
    UCHAR NextHop[4];
    NSI_IP_FORWARD_STATIC Static;
    ULONG SourceScope;
    UCHAR Source[4];
    ULONG Pad;
} NSI_IPV4_BEST_ROUTE;

typedef struct NSI_IPV6_BEST_ROUTE {
    NSI_IPV6_FORWARD_KEY Key;
    UCHAR Rw[28];
    ULONG Age;
    UCHAR NextHop[16];
    NSI_IP_FORWARD_STATIC Static;
    ULONG SourceScope;
    UCHAR Source[16];
    ULONG Pad;
} NSI_IPV6_BEST_ROUTE;
#pragma pack(pop)

MIME_STATIC_ASSERT(sizeof(NSI_IPV4_FORWARD_KEY) == 48 && sizeof(NSI_IPV6_FORWARD_KEY) == 72 &&
                       sizeof(NSI_IP_FORWARD_RW) == 32 && sizeof(NSI_IPV4_BEST_ROUTE_KEY) == 32 &&
                       sizeof(NSI_IPV6_BEST_ROUTE_KEY) == 56 && sizeof(NSI_IPV4_BEST_ROUTE) == 104 &&
                       sizeof(NSI_IPV6_BEST_ROUTE) == 152 && sizeof(NSI_IP_UNICAST_RW) == 28,
                   "ip structures");

/* ------------------------------------------------------------------ the tcp and udp modules */

#define NSI_TCP_STATS 0       /* key the family: dynamic 104, static 20 */
#define NSI_TCP_ALL 3         /* key the two addresses: dynamic 24, static 32 */
#define NSI_TCP_CONNECTIONS 4
#define NSI_TCP_LISTENERS 5
#define NSI_TCP_BOUND 35
#define NSI_TCP_TABLES 38
#define NSI_UDP_STATS 0       /* key the family: rw 2, dynamic 48 */
#define NSI_UDP_ENDPOINT 1    /* key the address: dynamic 32, static 32 */
#define NSI_UDP_TABLES 7

#pragma pack(push, 1)
typedef struct NSI_TCP_DYNAMIC {
    ULONG State;
    ULONG Unknown[5];
} NSI_TCP_DYNAMIC;

typedef struct NSI_TCP_STATIC {
    ULONG Unknown[3];
    ULONG ProcessId;
    ULONG64 CreateTime;
    ULONG64 ModuleInfo;
} NSI_TCP_STATIC;

typedef struct NSI_UDP_STATIC {
    ULONG ProcessId;
    ULONG Unknown;
    ULONG64 CreateTime;
    ULONG Flags;
    ULONG Unknown2;
    ULONG64 ModuleInfo;
} NSI_UDP_STATIC;
#pragma pack(pop)

#endif
