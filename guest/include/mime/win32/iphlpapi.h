/*
 * ip helper, iphlpapi.dll, with the structures of iptypes.h, ipifcons.h, ifmib.h,
 * ipmib.h, tcpmib.h, udpmib.h, ifdef.h, nldef.h and netioapi.h.
 */
#ifndef MIME_WIN32_IPHLPAPI_H
#define MIME_WIN32_IPHLPAPI_H

#include <mime/win32/ws2tcpip.h>

#ifdef MIME_BUILD_IPHLPAPI
#define IPHLPAPI_DLL_LINKAGE DECLSPEC_EXPORT
#else
#define IPHLPAPI_DLL_LINKAGE DECLSPEC_IMPORT
#endif
#define NETIOAPI_API_ WINAPI

typedef unsigned char UINT8, *PUINT8;
typedef ULONG NET_IFINDEX, *PNET_IFINDEX;
typedef NET_IFINDEX IF_INDEX, *PIF_INDEX;
typedef ULONG IFTYPE;
typedef UINT32 NET_IF_COMPARTMENT_ID;
typedef GUID NET_IF_NETWORK_GUID;
typedef PVOID HIFTIMESTAMPCHANGE;

typedef union _NET_LUID_LH {
    ULONG64 Value;
    struct {
        ULONG64 Reserved : 24;
        ULONG64 NetLuidIndex : 24;
        ULONG64 IfType : 16;
    } Info;
} NET_LUID_LH, *PNET_LUID_LH, NET_LUID, *PNET_LUID, IF_LUID, *PIF_LUID;

#define IF_MAX_STRING_SIZE 256
#define IF_MAX_PHYS_ADDRESS_LENGTH 32
#define NDIS_IF_MAX_STRING_SIZE IF_MAX_STRING_SIZE
#define IF_NAMESIZE NDIS_IF_MAX_STRING_SIZE
#define MAX_ADAPTER_ADDRESS_LENGTH 8
#define MAX_DHCPV6_DUID_LENGTH 130
#define MAX_DNS_SUFFIX_STRING_LENGTH 256
#define NET_IF_COMPARTMENT_ID_PRIMARY 1

#define IF_TYPE_OTHER 1
#define IF_TYPE_ETHERNET_CSMACD 6
#define IF_TYPE_ISO88025_TOKENRING 9
#define IF_TYPE_PPP 23
#define IF_TYPE_SOFTWARE_LOOPBACK 24
#define IF_TYPE_ATM 37
#define IF_TYPE_PROP_VIRTUAL 53
#define IF_TYPE_IEEE80211 71
#define IF_TYPE_TUNNEL 131
#define IF_TYPE_IEEE1394 144
#define IF_TYPE_WWANPP 243
#define IF_TYPE_WWANPP2 244

typedef enum {
    IfOperStatusUp = 1,
    IfOperStatusDown,
    IfOperStatusTesting,
    IfOperStatusUnknown,
    IfOperStatusDormant,
    IfOperStatusNotPresent,
    IfOperStatusLowerLayerDown
} IF_OPER_STATUS;

typedef enum _NET_IF_CONNECTION_TYPE {
    NET_IF_CONNECTION_DEDICATED = 1,
    NET_IF_CONNECTION_PASSIVE = 2,
    NET_IF_CONNECTION_DEMAND = 3,
    NET_IF_CONNECTION_MAXIMUM = 4
} NET_IF_CONNECTION_TYPE, *PNET_IF_CONNECTION_TYPE;

typedef enum {
    TUNNEL_TYPE_NONE = 0,
    TUNNEL_TYPE_OTHER = 1,
    TUNNEL_TYPE_DIRECT = 2,
    TUNNEL_TYPE_6TO4 = 11,
    TUNNEL_TYPE_ISATAP = 13,
    TUNNEL_TYPE_TEREDO = 14,
    TUNNEL_TYPE_IPHTTPS = 15
} TUNNEL_TYPE, *PTUNNEL_TYPE;

typedef enum {
    IpPrefixOriginOther = 0,
    IpPrefixOriginManual,
    IpPrefixOriginWellKnown,
    IpPrefixOriginDhcp,
    IpPrefixOriginRouterAdvertisement,
    IpPrefixOriginUnchanged = 1 << 4
} NL_PREFIX_ORIGIN, IP_PREFIX_ORIGIN;

typedef enum {
    NlsoOther = 0,
    NlsoManual,
    NlsoWellKnown,
    NlsoDhcp,
    NlsoLinkLayerAddress,
    NlsoRandom,
    IpSuffixOriginOther = 0,
    IpSuffixOriginManual,
    IpSuffixOriginWellKnown,
    IpSuffixOriginDhcp,
    IpSuffixOriginLinkLayerAddress,
    IpSuffixOriginRandom,
    IpSuffixOriginUnchanged = 1 << 4
} NL_SUFFIX_ORIGIN, IP_SUFFIX_ORIGIN;

typedef enum {
    NldsInvalid,
    NldsTentative,
    NldsDuplicate,
    NldsDeprecated,
    NldsPreferred,
    IpDadStateInvalid = 0,
    IpDadStateTentative,
    IpDadStateDuplicate,
    IpDadStateDeprecated,
    IpDadStatePreferred
} NL_DAD_STATE, IP_DAD_STATE;

typedef enum {
    RouteProtocolOther = 1,
    RouteProtocolLocal = 2,
    RouteProtocolNetMgmt = 3,
    RouteProtocolIcmp = 4,
    RouteProtocolEgp = 5,
    RouteProtocolGgp = 6,
    RouteProtocolHello = 7,
    RouteProtocolRip = 8,
    RouteProtocolIsIs = 9,
    RouteProtocolEsIs = 10,
    RouteProtocolCisco = 11,
    RouteProtocolBbn = 12,
    RouteProtocolOspf = 13,
    RouteProtocolBgp = 14,
    RouteProtocolIdpr = 15,
    RouteProtocolEigrp = 16,
    RouteProtocolDvmrp = 17,
    RouteProtocolRpl = 18,
    RouteProtocolDhcp = 19,
    MIB_IPPROTO_OTHER = 1,
    MIB_IPPROTO_LOCAL = 2,
    MIB_IPPROTO_NETMGMT = 3
} NL_ROUTE_PROTOCOL, *PNL_ROUTE_PROTOCOL;

typedef enum _NL_ROUTE_ORIGIN {
    NlroManual,
    NlroWellKnown,
    NlroDHCP,
    NlroRouterAdvertisement,
    Nlro6to4
} NL_ROUTE_ORIGIN, *PNL_ROUTE_ORIGIN;

typedef enum _MIB_NOTIFICATION_TYPE {
    MibParameterNotification,
    MibAddInstance,
    MibDeleteInstance,
    MibInitialNotification
} MIB_NOTIFICATION_TYPE, *PMIB_NOTIFICATION_TYPE;

/* ------------------------------------------------------------------ GetAdaptersAddresses */

#define GAA_FLAG_SKIP_UNICAST 0x0001
#define GAA_FLAG_SKIP_ANYCAST 0x0002
#define GAA_FLAG_SKIP_MULTICAST 0x0004
#define GAA_FLAG_SKIP_DNS_SERVER 0x0008
#define GAA_FLAG_INCLUDE_PREFIX 0x0010
#define GAA_FLAG_SKIP_FRIENDLY_NAME 0x0020
#define GAA_FLAG_INCLUDE_WINS_INFO 0x0040
#define GAA_FLAG_INCLUDE_GATEWAYS 0x0080
#define GAA_FLAG_INCLUDE_ALL_INTERFACES 0x0100
#define GAA_FLAG_INCLUDE_ALL_COMPARTMENTS 0x0200
#define GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER 0x0400
#define GAA_FLAG_SKIP_DNS_INFO 0x0800

#define IP_ADAPTER_ADDRESS_DNS_ELIGIBLE 0x01
#define IP_ADAPTER_ADDRESS_TRANSIENT 0x02

#define IP_ADAPTER_DDNS_ENABLED 0x0001
#define IP_ADAPTER_REGISTER_ADAPTER_SUFFIX 0x0002
#define IP_ADAPTER_DHCP_ENABLED 0x0004
#define IP_ADAPTER_RECEIVE_ONLY 0x0008
#define IP_ADAPTER_NO_MULTICAST 0x0010
#define IP_ADAPTER_IPV6_OTHER_STATEFUL_CONFIG 0x0020
#define IP_ADAPTER_NETBIOS_OVER_TCPIP_ENABLED 0x0040
#define IP_ADAPTER_IPV4_ENABLED 0x0080
#define IP_ADAPTER_IPV6_ENABLED 0x0100
#define IP_ADAPTER_IPV6_MANAGE_ADDRESS_CONFIG 0x0200

typedef struct _IP_ADAPTER_UNICAST_ADDRESS_LH {
    union {
        ULONGLONG Alignment;
        struct {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_UNICAST_ADDRESS_LH *Next;
    SOCKET_ADDRESS Address;
    IP_PREFIX_ORIGIN PrefixOrigin;
    IP_SUFFIX_ORIGIN SuffixOrigin;
    IP_DAD_STATE DadState;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG LeaseLifetime;
    UINT8 OnLinkPrefixLength;
} IP_ADAPTER_UNICAST_ADDRESS_LH, *PIP_ADAPTER_UNICAST_ADDRESS_LH, IP_ADAPTER_UNICAST_ADDRESS,
    *PIP_ADAPTER_UNICAST_ADDRESS;
#ifdef _WIN64
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_UNICAST_ADDRESS_LH) == 64, "IP_ADAPTER_UNICAST_ADDRESS_LH");
#endif

typedef struct _IP_ADAPTER_ANYCAST_ADDRESS_XP {
    union {
        ULONGLONG Alignment;
        struct {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_ANYCAST_ADDRESS_XP *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_ANYCAST_ADDRESS_XP, *PIP_ADAPTER_ANYCAST_ADDRESS_XP, IP_ADAPTER_ANYCAST_ADDRESS,
    *PIP_ADAPTER_ANYCAST_ADDRESS, IP_ADAPTER_MULTICAST_ADDRESS_XP, *PIP_ADAPTER_MULTICAST_ADDRESS_XP,
    IP_ADAPTER_MULTICAST_ADDRESS, *PIP_ADAPTER_MULTICAST_ADDRESS;

typedef struct _IP_ADAPTER_DNS_SERVER_ADDRESS_XP {
    union {
        ULONGLONG Alignment;
        struct {
            ULONG Length;
            DWORD Reserved;
        };
    };
    struct _IP_ADAPTER_DNS_SERVER_ADDRESS_XP *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_DNS_SERVER_ADDRESS_XP, *PIP_ADAPTER_DNS_SERVER_ADDRESS_XP, IP_ADAPTER_DNS_SERVER_ADDRESS,
    *PIP_ADAPTER_DNS_SERVER_ADDRESS, IP_ADAPTER_WINS_SERVER_ADDRESS_LH, *PIP_ADAPTER_WINS_SERVER_ADDRESS_LH,
    IP_ADAPTER_GATEWAY_ADDRESS_LH, *PIP_ADAPTER_GATEWAY_ADDRESS_LH, IP_ADAPTER_GATEWAY_ADDRESS,
    *PIP_ADAPTER_GATEWAY_ADDRESS,
    IP_ADAPTER_WINS_SERVER_ADDRESS, *PIP_ADAPTER_WINS_SERVER_ADDRESS;

typedef struct _IP_ADAPTER_PREFIX_XP {
    union {
        ULONGLONG Alignment;
        struct {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_PREFIX_XP *Next;
    SOCKET_ADDRESS Address;
    ULONG PrefixLength;
} IP_ADAPTER_PREFIX_XP, *PIP_ADAPTER_PREFIX_XP, IP_ADAPTER_PREFIX, *PIP_ADAPTER_PREFIX;
#ifdef _WIN64
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_PREFIX_XP) == 40, "IP_ADAPTER_PREFIX_XP");
#endif

typedef struct _IP_ADAPTER_DNS_SUFFIX {
    struct _IP_ADAPTER_DNS_SUFFIX *Next;
    WCHAR String[MAX_DNS_SUFFIX_STRING_LENGTH];
} IP_ADAPTER_DNS_SUFFIX, *PIP_ADAPTER_DNS_SUFFIX;

typedef struct _IP_ADAPTER_ADDRESSES_LH {
    union {
        ULONGLONG Alignment;
        struct {
            ULONG Length;
            IF_INDEX IfIndex;
        };
    };
    struct _IP_ADAPTER_ADDRESSES_LH *Next;
    PCHAR AdapterName;
    PIP_ADAPTER_UNICAST_ADDRESS_LH FirstUnicastAddress;
    PIP_ADAPTER_ANYCAST_ADDRESS_XP FirstAnycastAddress;
    PIP_ADAPTER_MULTICAST_ADDRESS_XP FirstMulticastAddress;
    PIP_ADAPTER_DNS_SERVER_ADDRESS_XP FirstDnsServerAddress;
    PWCHAR DnsSuffix;
    PWCHAR Description;
    PWCHAR FriendlyName;
    BYTE PhysicalAddress[MAX_ADAPTER_ADDRESS_LENGTH];
    ULONG PhysicalAddressLength;
    union {
        ULONG Flags;
        struct {
            ULONG DdnsEnabled : 1;
            ULONG RegisterAdapterSuffix : 1;
            ULONG Dhcpv4Enabled : 1;
            ULONG ReceiveOnly : 1;
            ULONG NoMulticast : 1;
            ULONG Ipv6OtherStatefulConfig : 1;
            ULONG NetbiosOverTcpipEnabled : 1;
            ULONG Ipv4Enabled : 1;
            ULONG Ipv6Enabled : 1;
            ULONG Ipv6ManagedAddressConfigurationSupported : 1;
        };
    };
    ULONG Mtu;
    IFTYPE IfType;
    IF_OPER_STATUS OperStatus;
    IF_INDEX Ipv6IfIndex;
    ULONG ZoneIndices[16];
    PIP_ADAPTER_PREFIX_XP FirstPrefix;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    PIP_ADAPTER_WINS_SERVER_ADDRESS_LH FirstWinsServerAddress;
    PIP_ADAPTER_GATEWAY_ADDRESS_LH FirstGatewayAddress;
    ULONG Ipv4Metric;
    ULONG Ipv6Metric;
    IF_LUID Luid;
    SOCKET_ADDRESS Dhcpv4Server;
    NET_IF_COMPARTMENT_ID CompartmentId;
    NET_IF_NETWORK_GUID NetworkGuid;
    NET_IF_CONNECTION_TYPE ConnectionType;
    TUNNEL_TYPE TunnelType;
    SOCKET_ADDRESS Dhcpv6Server;
    BYTE Dhcpv6ClientDuid[MAX_DHCPV6_DUID_LENGTH];
    ULONG Dhcpv6ClientDuidLength;
    ULONG Dhcpv6Iaid;
    PIP_ADAPTER_DNS_SUFFIX FirstDnsSuffix;
} IP_ADAPTER_ADDRESSES_LH, *PIP_ADAPTER_ADDRESSES_LH, IP_ADAPTER_ADDRESSES, *PIP_ADAPTER_ADDRESSES;
#ifdef _WIN64
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, Flags, 92);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, ZoneIndices, 112);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, Luid, 224);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, NetworkGuid, 252);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, Dhcpv6Server, 280);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, FirstDnsSuffix, 440);
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_ADDRESSES_LH) == 448, "IP_ADAPTER_ADDRESSES_LH");
#else
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_ADDRESSES_LH) == 376, "IP_ADAPTER_ADDRESSES_LH");
#endif

/* ------------------------------------------------------------------ GetAdaptersInfo and GetNetworkParams */

#define MAX_ADAPTER_DESCRIPTION_LENGTH 128
#define MAX_ADAPTER_NAME_LENGTH 256
#define MAX_HOSTNAME_LEN 128
#define MAX_DOMAIN_NAME_LEN 128
#define MAX_SCOPE_ID_LEN 256
#define MAX_ADAPTER_NAME 128

#define BROADCAST_NODETYPE 1
#define PEER_TO_PEER_NODETYPE 2
#define MIXED_NODETYPE 4
#define HYBRID_NODETYPE 8

typedef struct {
    char String[4 * 4];
} IP_ADDRESS_STRING, *PIP_ADDRESS_STRING, IP_MASK_STRING, *PIP_MASK_STRING;

typedef struct _IP_ADDR_STRING {
    struct _IP_ADDR_STRING *Next;
    IP_ADDRESS_STRING IpAddress;
    IP_MASK_STRING IpMask;
    DWORD Context;
} IP_ADDR_STRING, *PIP_ADDR_STRING;

typedef struct _IP_ADAPTER_INFO {
    struct _IP_ADAPTER_INFO *Next;
    DWORD ComboIndex;
    char AdapterName[MAX_ADAPTER_NAME_LENGTH + 4];
    char Description[MAX_ADAPTER_DESCRIPTION_LENGTH + 4];
    UINT AddressLength;
    BYTE Address[MAX_ADAPTER_ADDRESS_LENGTH];
    DWORD Index;
    UINT Type;
    UINT DhcpEnabled;
    PIP_ADDR_STRING CurrentIpAddress;
    IP_ADDR_STRING IpAddressList;
    IP_ADDR_STRING GatewayList;
    IP_ADDR_STRING DhcpServer;
    BOOL HaveWins;
    IP_ADDR_STRING PrimaryWinsServer;
    IP_ADDR_STRING SecondaryWinsServer;
    LONGLONG LeaseObtained; /* time_t */
    LONGLONG LeaseExpires;
} IP_ADAPTER_INFO, *PIP_ADAPTER_INFO;
#ifdef _WIN64
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_INFO) == 704, "IP_ADAPTER_INFO");
#else
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_INFO) == 648, "IP_ADAPTER_INFO");
#endif

typedef struct {
    char HostName[MAX_HOSTNAME_LEN + 4];
    char DomainName[MAX_DOMAIN_NAME_LEN + 4];
    PIP_ADDR_STRING CurrentDnsServer;
    IP_ADDR_STRING DnsServerList;
    UINT NodeType;
    char ScopeId[MAX_SCOPE_ID_LEN + 4];
    UINT EnableRouting;
    UINT EnableProxy;
    UINT EnableDns;
} FIXED_INFO_W2KSP1, FIXED_INFO, *PFIXED_INFO;
#ifdef _WIN64
MIME_STATIC_ASSERT(sizeof(FIXED_INFO) == 600, "FIXED_INFO");
#endif

typedef struct _IP_PER_ADAPTER_INFO_W2KSP1 {
    UINT AutoconfigEnabled;
    UINT AutoconfigActive;
    PIP_ADDR_STRING CurrentDnsServer;
    IP_ADDR_STRING DnsServerList;
} IP_PER_ADAPTER_INFO_W2KSP1, IP_PER_ADAPTER_INFO, *PIP_PER_ADAPTER_INFO;

typedef struct _IP_ADAPTER_INDEX_MAP {
    ULONG Index;
    WCHAR Name[MAX_ADAPTER_NAME];
} IP_ADAPTER_INDEX_MAP, *PIP_ADAPTER_INDEX_MAP;

typedef struct _IP_INTERFACE_INFO {
    LONG NumAdapters;
    IP_ADAPTER_INDEX_MAP Adapter[1];
} IP_INTERFACE_INFO, *PIP_INTERFACE_INFO;

/* ------------------------------------------------------------------ interfaces */

#define MAX_INTERFACE_NAME_LEN 256
#define MAXLEN_PHYSADDR 8
#define MAXLEN_IFDESCR 256

#define MIB_IF_TYPE_OTHER 1
#define MIB_IF_TYPE_ETHERNET 6
#define MIB_IF_TYPE_PPP 23
#define MIB_IF_TYPE_LOOPBACK 24

#define MIB_IF_ADMIN_STATUS_UP 1
#define MIB_IF_ADMIN_STATUS_DOWN 2

#define IF_OPER_STATUS_NON_OPERATIONAL 0
#define IF_OPER_STATUS_UNREACHABLE 1
#define IF_OPER_STATUS_DISCONNECTED 2
#define IF_OPER_STATUS_CONNECTING 3
#define IF_OPER_STATUS_CONNECTED 4
#define IF_OPER_STATUS_OPERATIONAL 5

typedef struct _MIB_IFROW {
    WCHAR wszName[MAX_INTERFACE_NAME_LEN];
    IF_INDEX dwIndex;
    IFTYPE dwType;
    DWORD dwMtu;
    DWORD dwSpeed;
    DWORD dwPhysAddrLen;
    UCHAR bPhysAddr[MAXLEN_PHYSADDR];
    DWORD dwAdminStatus;
    DWORD dwOperStatus;
    DWORD dwLastChange;
    DWORD dwInOctets;
    DWORD dwInUcastPkts;
    DWORD dwInNUcastPkts;
    DWORD dwInDiscards;
    DWORD dwInErrors;
    DWORD dwInUnknownProtos;
    DWORD dwOutOctets;
    DWORD dwOutUcastPkts;
    DWORD dwOutNUcastPkts;
    DWORD dwOutDiscards;
    DWORD dwOutErrors;
    DWORD dwOutQLen;
    DWORD dwDescrLen;
    UCHAR bDescr[MAXLEN_IFDESCR];
} MIB_IFROW, *PMIB_IFROW;
MIME_STATIC_ASSERT(sizeof(MIB_IFROW) == 860, "MIB_IFROW");

typedef struct _MIB_IFTABLE {
    DWORD dwNumEntries;
    MIB_IFROW table[1];
} MIB_IFTABLE, *PMIB_IFTABLE;

typedef enum _NDIS_MEDIUM {
    NdisMedium802_3 = 0,
    NdisMediumWan = 3,
    NdisMediumLoopback = 11,
    NdisMediumNative802_11 = 16,
    NdisMediumIP = 19,
} NDIS_MEDIUM;

typedef enum _NDIS_PHYSICAL_MEDIUM {
    NdisPhysicalMediumUnspecified = 0,
    NdisPhysicalMediumWirelessLan = 1,
    NdisPhysicalMediumNative802_11 = 9,
    NdisPhysicalMedium802_3 = 14,
} NDIS_PHYSICAL_MEDIUM;

typedef enum _NET_IF_ACCESS_TYPE {
    NET_IF_ACCESS_LOOPBACK = 1,
    NET_IF_ACCESS_BROADCAST = 2,
    NET_IF_ACCESS_POINT_TO_POINT = 3,
    NET_IF_ACCESS_POINT_TO_MULTI_POINT = 4,
    NET_IF_ACCESS_MAXIMUM = 5
} NET_IF_ACCESS_TYPE;

typedef enum _NET_IF_DIRECTION_TYPE {
    NET_IF_DIRECTION_SENDRECEIVE,
    NET_IF_DIRECTION_SENDONLY,
    NET_IF_DIRECTION_RECEIVEONLY,
    NET_IF_DIRECTION_MAXIMUM
} NET_IF_DIRECTION_TYPE;

typedef enum _NET_IF_ADMIN_STATUS {
    NET_IF_ADMIN_STATUS_UP = 1,
    NET_IF_ADMIN_STATUS_DOWN = 2,
    NET_IF_ADMIN_STATUS_TESTING = 3
} NET_IF_ADMIN_STATUS;

typedef enum _NET_IF_MEDIA_CONNECT_STATE {
    MediaConnectStateUnknown,
    MediaConnectStateConnected,
    MediaConnectStateDisconnected
} NET_IF_MEDIA_CONNECT_STATE;

typedef struct _MIB_IF_ROW2 {
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    GUID InterfaceGuid;
    WCHAR Alias[IF_MAX_STRING_SIZE + 1];
    WCHAR Description[IF_MAX_STRING_SIZE + 1];
    ULONG PhysicalAddressLength;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR PermanentPhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG Mtu;
    IFTYPE Type;
    TUNNEL_TYPE TunnelType;
    NDIS_MEDIUM MediaType;
    NDIS_PHYSICAL_MEDIUM PhysicalMediumType;
    NET_IF_ACCESS_TYPE AccessType;
    NET_IF_DIRECTION_TYPE DirectionType;
    struct {
        BOOLEAN HardwareInterface : 1;
        BOOLEAN FilterInterface : 1;
        BOOLEAN ConnectorPresent : 1;
        BOOLEAN NotAuthenticated : 1;
        BOOLEAN NotMediaConnected : 1;
        BOOLEAN Paused : 1;
        BOOLEAN LowPower : 1;
        BOOLEAN EndPointInterface : 1;
    } InterfaceAndOperStatusFlags;
    IF_OPER_STATUS OperStatus;
    NET_IF_ADMIN_STATUS AdminStatus;
    NET_IF_MEDIA_CONNECT_STATE MediaConnectState;
    NET_IF_NETWORK_GUID NetworkGuid;
    NET_IF_CONNECTION_TYPE ConnectionType;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    ULONG64 InOctets;
    ULONG64 InUcastPkts;
    ULONG64 InNUcastPkts;
    ULONG64 InDiscards;
    ULONG64 InErrors;
    ULONG64 InUnknownProtos;
    ULONG64 InUcastOctets;
    ULONG64 InMulticastOctets;
    ULONG64 InBroadcastOctets;
    ULONG64 OutOctets;
    ULONG64 OutUcastPkts;
    ULONG64 OutNUcastPkts;
    ULONG64 OutDiscards;
    ULONG64 OutErrors;
    ULONG64 OutUcastOctets;
    ULONG64 OutMulticastOctets;
    ULONG64 OutBroadcastOctets;
    ULONG64 OutQLen;
} MIB_IF_ROW2, *PMIB_IF_ROW2;
MIME_FIELD_AT(MIB_IF_ROW2, PhysicalAddressLength, 1056);
MIME_FIELD_AT(MIB_IF_ROW2, Mtu, 1124);
MIME_FIELD_AT(MIB_IF_ROW2, InterfaceAndOperStatusFlags, 1152);
MIME_FIELD_AT(MIB_IF_ROW2, TransmitLinkSpeed, 1192);
MIME_STATIC_ASSERT(sizeof(MIB_IF_ROW2) == 1352, "MIB_IF_ROW2");

typedef struct _MIB_IF_TABLE2 {
    ULONG NumEntries;
    MIB_IF_ROW2 Table[1];
} MIB_IF_TABLE2, *PMIB_IF_TABLE2;

typedef enum _MIB_IF_TABLE_LEVEL {
    MibIfTableNormal,
    MibIfTableRaw,
    MibIfTableNormalWithoutStatistics,
} MIB_IF_TABLE_LEVEL, *PMIB_IF_TABLE_LEVEL;

typedef enum _MIB_IF_ENTRY_LEVEL {
    MibIfEntryNormal = 0,
    MibIfEntryNormalWithoutStatistics = 2
} MIB_IF_ENTRY_LEVEL, *PMIB_IF_ENTRY_LEVEL;

typedef enum {
    RouterDiscoveryDisabled = 0,
    RouterDiscoveryEnabled,
    RouterDiscoveryDhcp,
    RouterDiscoveryUnchanged = -1
} NL_ROUTER_DISCOVERY_BEHAVIOR;

typedef enum {
    LinkLocalAlwaysOff = 0,
    LinkLocalDelayed,
    LinkLocalAlwaysOn,
    LinkLocalUnchanged = -1
} NL_LINK_LOCAL_ADDRESS_BEHAVIOR;

typedef struct _NL_INTERFACE_OFFLOAD_ROD {
    BOOLEAN NlChecksumSupported : 1;
    BOOLEAN NlOptionsSupported : 1;
    BOOLEAN TlDatagramChecksumSupported : 1;
    BOOLEAN TlStreamChecksumSupported : 1;
    BOOLEAN TlStreamOptionsSupported : 1;
    BOOLEAN FastPathCompatible : 1;
    BOOLEAN TlLargeSendOffloadSupported : 1;
    BOOLEAN TlGiantSendOffloadSupported : 1;
} NL_INTERFACE_OFFLOAD_ROD;

#define ScopeLevelCount 16

typedef struct _MIB_IPINTERFACE_ROW {
    ADDRESS_FAMILY Family;
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    ULONG MaxReassemblySize;
    ULONG64 InterfaceIdentifier;
    ULONG MinRouterAdvertisementInterval;
    ULONG MaxRouterAdvertisementInterval;
    BOOLEAN AdvertisingEnabled;
    BOOLEAN ForwardingEnabled;
    BOOLEAN WeakHostSend;
    BOOLEAN WeakHostReceive;
    BOOLEAN UseAutomaticMetric;
    BOOLEAN UseNeighborUnreachabilityDetection;
    BOOLEAN ManagedAddressConfigurationSupported;
    BOOLEAN OtherStatefulConfigurationSupported;
    BOOLEAN AdvertiseDefaultRoute;
    NL_ROUTER_DISCOVERY_BEHAVIOR RouterDiscoveryBehavior;
    ULONG DadTransmits;
    ULONG BaseReachableTime;
    ULONG RetransmitTime;
    ULONG PathMtuDiscoveryTimeout;
    NL_LINK_LOCAL_ADDRESS_BEHAVIOR LinkLocalAddressBehavior;
    ULONG LinkLocalAddressTimeout;
    ULONG ZoneIndices[ScopeLevelCount];
    ULONG SitePrefixLength;
    ULONG Metric;
    ULONG NlMtu;
    BOOLEAN Connected;
    BOOLEAN SupportsWakeUpPatterns;
    BOOLEAN SupportsNeighborDiscovery;
    BOOLEAN SupportsRouterDiscovery;
    ULONG ReachableTime;
    NL_INTERFACE_OFFLOAD_ROD TransmitOffload;
    NL_INTERFACE_OFFLOAD_ROD ReceiveOffload;
    BOOLEAN DisableDefaultRoutes;
} MIB_IPINTERFACE_ROW, *PMIB_IPINTERFACE_ROW;
MIME_FIELD_AT(MIB_IPINTERFACE_ROW, ZoneIndices, 80);
MIME_FIELD_AT(MIB_IPINTERFACE_ROW, Metric, 148);
MIME_FIELD_AT(MIB_IPINTERFACE_ROW, Connected, 156);
MIME_STATIC_ASSERT(sizeof(MIB_IPINTERFACE_ROW) == 168, "MIB_IPINTERFACE_ROW");

typedef struct _MIB_IPINTERFACE_TABLE {
    ULONG NumEntries;
    MIB_IPINTERFACE_ROW Table[1];
} MIB_IPINTERFACE_TABLE, *PMIB_IPINTERFACE_TABLE;

/* ------------------------------------------------------------------ addresses */

typedef struct _MIB_UNICASTIPADDRESS_ROW {
    SOCKADDR_INET Address;
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    NL_PREFIX_ORIGIN PrefixOrigin;
    NL_SUFFIX_ORIGIN SuffixOrigin;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    UINT8 OnLinkPrefixLength;
    BOOLEAN SkipAsSource;
    NL_DAD_STATE DadState;
    SCOPE_ID ScopeId;
    LARGE_INTEGER CreationTimeStamp;
} MIB_UNICASTIPADDRESS_ROW, *PMIB_UNICASTIPADDRESS_ROW;
MIME_FIELD_AT(MIB_UNICASTIPADDRESS_ROW, InterfaceLuid, 32);
MIME_FIELD_AT(MIB_UNICASTIPADDRESS_ROW, DadState, 64);
MIME_STATIC_ASSERT(sizeof(MIB_UNICASTIPADDRESS_ROW) == 80, "MIB_UNICASTIPADDRESS_ROW");

typedef struct _MIB_UNICASTIPADDRESS_TABLE {
    ULONG NumEntries;
    MIB_UNICASTIPADDRESS_ROW Table[1];
} MIB_UNICASTIPADDRESS_TABLE, *PMIB_UNICASTIPADDRESS_TABLE;

typedef struct _MIB_ANYCASTIPADDRESS_ROW {
    SOCKADDR_INET Address;
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    SCOPE_ID ScopeId;
} MIB_ANYCASTIPADDRESS_ROW, *PMIB_ANYCASTIPADDRESS_ROW, MIB_MULTICASTIPADDRESS_ROW, *PMIB_MULTICASTIPADDRESS_ROW;
MIME_STATIC_ASSERT(sizeof(MIB_ANYCASTIPADDRESS_ROW) == 48, "MIB_ANYCASTIPADDRESS_ROW");

typedef struct _MIB_ANYCASTIPADDRESS_TABLE {
    ULONG NumEntries;
    MIB_ANYCASTIPADDRESS_ROW Table[1];
} MIB_ANYCASTIPADDRESS_TABLE, *PMIB_ANYCASTIPADDRESS_TABLE, MIB_MULTICASTIPADDRESS_TABLE,
    *PMIB_MULTICASTIPADDRESS_TABLE;

#define MIB_IPADDR_PRIMARY 0x0001
#define MIB_IPADDR_DYNAMIC 0x0004
#define MIB_IPADDR_DISCONNECTED 0x0008
#define MIB_IPADDR_DELETED 0x0040
#define MIB_IPADDR_TRANSIENT 0x0080

typedef struct _MIB_IPADDRROW_XP {
    DWORD dwAddr;
    IF_INDEX dwIndex;
    DWORD dwMask;
    DWORD dwBCastAddr;
    DWORD dwReasmSize;
    unsigned short unused1;
    unsigned short wType;
} MIB_IPADDRROW, *PMIB_IPADDRROW;

typedef struct _MIB_IPADDRTABLE {
    DWORD dwNumEntries;
    MIB_IPADDRROW table[1];
} MIB_IPADDRTABLE, *PMIB_IPADDRTABLE;

/* ------------------------------------------------------------------ routes */

typedef struct _IP_ADDRESS_PREFIX {
    SOCKADDR_INET Prefix;
    UINT8 PrefixLength;
} IP_ADDRESS_PREFIX, *PIP_ADDRESS_PREFIX;

typedef struct _MIB_IPFORWARD_ROW2 {
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    IP_ADDRESS_PREFIX DestinationPrefix;
    SOCKADDR_INET NextHop;
    UCHAR SitePrefixLength;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG Metric;
    NL_ROUTE_PROTOCOL Protocol;
    BOOLEAN Loopback;
    BOOLEAN AutoconfigureAddress;
    BOOLEAN Publish;
    BOOLEAN Immortal;
    ULONG Age;
    NL_ROUTE_ORIGIN Origin;
} MIB_IPFORWARD_ROW2, *PMIB_IPFORWARD_ROW2;
MIME_FIELD_AT(MIB_IPFORWARD_ROW2, DestinationPrefix, 12);
MIME_FIELD_AT(MIB_IPFORWARD_ROW2, NextHop, 44);
MIME_FIELD_AT(MIB_IPFORWARD_ROW2, Metric, 84);
MIME_STATIC_ASSERT(sizeof(MIB_IPFORWARD_ROW2) == 104, "MIB_IPFORWARD_ROW2");

typedef struct _MIB_IPFORWARD_TABLE2 {
    ULONG NumEntries;
    MIB_IPFORWARD_ROW2 Table[1];
} MIB_IPFORWARD_TABLE2, *PMIB_IPFORWARD_TABLE2;

typedef enum {
    MIB_IPROUTE_TYPE_OTHER = 1,
    MIB_IPROUTE_TYPE_INVALID = 2,
    MIB_IPROUTE_TYPE_DIRECT = 3,
    MIB_IPROUTE_TYPE_INDIRECT = 4,
} MIB_IPFORWARD_TYPE;

typedef struct _MIB_IPFORWARDROW {
    DWORD dwForwardDest;
    DWORD dwForwardMask;
    DWORD dwForwardPolicy;
    DWORD dwForwardNextHop;
    IF_INDEX dwForwardIfIndex;
    union {
        DWORD dwForwardType;
        MIB_IPFORWARD_TYPE ForwardType;
    };
    union {
        DWORD dwForwardProto;
        NL_ROUTE_PROTOCOL ForwardProto;
    };
    DWORD dwForwardAge;
    DWORD dwForwardNextHopAS;
    DWORD dwForwardMetric1;
    DWORD dwForwardMetric2;
    DWORD dwForwardMetric3;
    DWORD dwForwardMetric4;
    DWORD dwForwardMetric5;
} MIB_IPFORWARDROW, *PMIB_IPFORWARDROW;
MIME_STATIC_ASSERT(sizeof(MIB_IPFORWARDROW) == 56, "MIB_IPFORWARDROW");

typedef struct _MIB_IPFORWARDTABLE {
    DWORD dwNumEntries;
    MIB_IPFORWARDROW table[1];
} MIB_IPFORWARDTABLE, *PMIB_IPFORWARDTABLE;

/* ------------------------------------------------------------------ neighbors */

typedef enum _NL_NEIGHBOR_STATE {
    NlnsUnreachable,
    NlnsIncomplete,
    NlnsProbe,
    NlnsDelay,
    NlnsStale,
    NlnsReachable,
    NlnsPermanent,
    NlnsMaximum,
} NL_NEIGHBOR_STATE;

typedef struct _MIB_IPNET_ROW2 {
    SOCKADDR_INET Address;
    NET_IFINDEX InterfaceIndex;
    NET_LUID InterfaceLuid;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG PhysicalAddressLength;
    NL_NEIGHBOR_STATE State;
    union {
        struct {
            BOOLEAN IsRouter : 1;
            BOOLEAN IsUnreachable : 1;
        };
        UCHAR Flags;
    };
    union {
        ULONG LastReachable;
        ULONG LastUnreachable;
    } ReachabilityTime;
} MIB_IPNET_ROW2, *PMIB_IPNET_ROW2;
MIME_STATIC_ASSERT(sizeof(MIB_IPNET_ROW2) == 88, "MIB_IPNET_ROW2");

typedef struct _MIB_IPNET_TABLE2 {
    ULONG NumEntries;
    MIB_IPNET_ROW2 Table[1];
} MIB_IPNET_TABLE2, *PMIB_IPNET_TABLE2;

typedef struct _MIB_IPNETROW_LH {
    IF_INDEX dwIndex;
    DWORD dwPhysAddrLen;
    UCHAR bPhysAddr[MAXLEN_PHYSADDR];
    DWORD dwAddr;
    DWORD dwType;
} MIB_IPNETROW, *PMIB_IPNETROW;

typedef struct _MIB_IPNETTABLE {
    DWORD dwNumEntries;
    MIB_IPNETROW table[1];
} MIB_IPNETTABLE, *PMIB_IPNETTABLE;

/* ------------------------------------------------------------------ tcp and udp */

#define TCPIP_OWNING_MODULE_SIZE 16

typedef enum {
    MIB_TCP_STATE_CLOSED = 1,
    MIB_TCP_STATE_LISTEN = 2,
    MIB_TCP_STATE_SYN_SENT = 3,
    MIB_TCP_STATE_SYN_RCVD = 4,
    MIB_TCP_STATE_ESTAB = 5,
    MIB_TCP_STATE_FIN_WAIT1 = 6,
    MIB_TCP_STATE_FIN_WAIT2 = 7,
    MIB_TCP_STATE_CLOSE_WAIT = 8,
    MIB_TCP_STATE_CLOSING = 9,
    MIB_TCP_STATE_LAST_ACK = 10,
    MIB_TCP_STATE_TIME_WAIT = 11,
    MIB_TCP_STATE_DELETE_TCB = 12,
} MIB_TCP_STATE;

typedef enum {
    TCP_TABLE_BASIC_LISTENER,
    TCP_TABLE_BASIC_CONNECTIONS,
    TCP_TABLE_BASIC_ALL,
    TCP_TABLE_OWNER_PID_LISTENER,
    TCP_TABLE_OWNER_PID_CONNECTIONS,
    TCP_TABLE_OWNER_PID_ALL,
    TCP_TABLE_OWNER_MODULE_LISTENER,
    TCP_TABLE_OWNER_MODULE_CONNECTIONS,
    TCP_TABLE_OWNER_MODULE_ALL
} TCP_TABLE_CLASS, *PTCP_TABLE_CLASS;

typedef enum {
    UDP_TABLE_BASIC,
    UDP_TABLE_OWNER_PID,
    UDP_TABLE_OWNER_MODULE
} UDP_TABLE_CLASS, *PUDP_TABLE_CLASS;

typedef struct _MIB_TCPROW_LH {
    union {
        DWORD dwState;
        MIB_TCP_STATE State;
    };
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwRemoteAddr;
    DWORD dwRemotePort;
} MIB_TCPROW, *PMIB_TCPROW;

typedef struct _MIB_TCPTABLE {
    DWORD dwNumEntries;
    MIB_TCPROW table[1];
} MIB_TCPTABLE, *PMIB_TCPTABLE;

typedef struct _MIB_TCPROW2 {
    DWORD dwState;
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwRemoteAddr;
    DWORD dwRemotePort;
    DWORD dwOwningPid;
    DWORD dwOffloadState;
} MIB_TCPROW2, *PMIB_TCPROW2;

typedef struct _MIB_TCPTABLE2 {
    DWORD dwNumEntries;
    MIB_TCPROW2 table[1];
} MIB_TCPTABLE2, *PMIB_TCPTABLE2;

typedef struct _MIB_TCPROW_OWNER_PID {
    DWORD dwState;
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwRemoteAddr;
    DWORD dwRemotePort;
    DWORD dwOwningPid;
} MIB_TCPROW_OWNER_PID, *PMIB_TCPROW_OWNER_PID;

typedef struct _MIB_TCPTABLE_OWNER_PID {
    DWORD dwNumEntries;
    MIB_TCPROW_OWNER_PID table[1];
} MIB_TCPTABLE_OWNER_PID, *PMIB_TCPTABLE_OWNER_PID;

typedef struct _MIB_TCPROW_OWNER_MODULE {
    DWORD dwState;
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwRemoteAddr;
    DWORD dwRemotePort;
    DWORD dwOwningPid;
    LARGE_INTEGER liCreateTimestamp;
    ULONGLONG OwningModuleInfo[TCPIP_OWNING_MODULE_SIZE];
} MIB_TCPROW_OWNER_MODULE, *PMIB_TCPROW_OWNER_MODULE;
MIME_STATIC_ASSERT(sizeof(MIB_TCPROW_OWNER_MODULE) == 160, "MIB_TCPROW_OWNER_MODULE");

typedef struct _MIB_TCPTABLE_OWNER_MODULE {
    DWORD dwNumEntries;
    MIB_TCPROW_OWNER_MODULE table[1];
} MIB_TCPTABLE_OWNER_MODULE, *PMIB_TCPTABLE_OWNER_MODULE;

typedef struct _MIB_TCP6ROW_OWNER_PID {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    UCHAR ucRemoteAddr[16];
    DWORD dwRemoteScopeId;
    DWORD dwRemotePort;
    DWORD dwState;
    DWORD dwOwningPid;
} MIB_TCP6ROW_OWNER_PID, *PMIB_TCP6ROW_OWNER_PID;
MIME_STATIC_ASSERT(sizeof(MIB_TCP6ROW_OWNER_PID) == 56, "MIB_TCP6ROW_OWNER_PID");

typedef struct _MIB_TCP6TABLE_OWNER_PID {
    DWORD dwNumEntries;
    MIB_TCP6ROW_OWNER_PID table[1];
} MIB_TCP6TABLE_OWNER_PID, *PMIB_TCP6TABLE_OWNER_PID;

typedef struct _MIB_TCP6ROW_OWNER_MODULE {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    UCHAR ucRemoteAddr[16];
    DWORD dwRemoteScopeId;
    DWORD dwRemotePort;
    DWORD dwState;
    DWORD dwOwningPid;
    LARGE_INTEGER liCreateTimestamp;
    ULONGLONG OwningModuleInfo[TCPIP_OWNING_MODULE_SIZE];
} MIB_TCP6ROW_OWNER_MODULE, *PMIB_TCP6ROW_OWNER_MODULE;
MIME_STATIC_ASSERT(sizeof(MIB_TCP6ROW_OWNER_MODULE) == 192, "MIB_TCP6ROW_OWNER_MODULE");

typedef struct _MIB_TCP6TABLE_OWNER_MODULE {
    DWORD dwNumEntries;
    MIB_TCP6ROW_OWNER_MODULE table[1];
} MIB_TCP6TABLE_OWNER_MODULE, *PMIB_TCP6TABLE_OWNER_MODULE;

typedef struct _MIB_UDPROW {
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
} MIB_UDPROW, *PMIB_UDPROW;

typedef struct _MIB_UDPTABLE {
    DWORD dwNumEntries;
    MIB_UDPROW table[1];
} MIB_UDPTABLE, *PMIB_UDPTABLE;

typedef struct _MIB_UDPROW_OWNER_PID {
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwOwningPid;
} MIB_UDPROW_OWNER_PID, *PMIB_UDPROW_OWNER_PID;

typedef struct _MIB_UDPTABLE_OWNER_PID {
    DWORD dwNumEntries;
    MIB_UDPROW_OWNER_PID table[1];
} MIB_UDPTABLE_OWNER_PID, *PMIB_UDPTABLE_OWNER_PID;

typedef struct _MIB_UDPROW_OWNER_MODULE {
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwOwningPid;
    LARGE_INTEGER liCreateTimestamp;
    union {
        struct {
            int SpecificPortBind : 1;
        };
        int dwFlags;
    };
    ULONGLONG OwningModuleInfo[TCPIP_OWNING_MODULE_SIZE];
} MIB_UDPROW_OWNER_MODULE, *PMIB_UDPROW_OWNER_MODULE;
MIME_STATIC_ASSERT(sizeof(MIB_UDPROW_OWNER_MODULE) == 160, "MIB_UDPROW_OWNER_MODULE");

typedef struct _MIB_UDPTABLE_OWNER_MODULE {
    DWORD dwNumEntries;
    MIB_UDPROW_OWNER_MODULE table[1];
} MIB_UDPTABLE_OWNER_MODULE, *PMIB_UDPTABLE_OWNER_MODULE;

typedef struct _MIB_UDP6ROW {
    IN6_ADDR dwLocalAddr;
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
} MIB_UDP6ROW, *PMIB_UDP6ROW;

typedef struct _MIB_UDP6TABLE {
    DWORD dwNumEntries;
    MIB_UDP6ROW table[1];
} MIB_UDP6TABLE, *PMIB_UDP6TABLE;

typedef struct _MIB_UDP6ROW_OWNER_PID {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    DWORD dwOwningPid;
} MIB_UDP6ROW_OWNER_PID, *PMIB_UDP6ROW_OWNER_PID;

typedef struct _MIB_UDP6TABLE_OWNER_PID {
    DWORD dwNumEntries;
    MIB_UDP6ROW_OWNER_PID table[1];
} MIB_UDP6TABLE_OWNER_PID, *PMIB_UDP6TABLE_OWNER_PID;

typedef struct _MIB_UDP6ROW_OWNER_MODULE {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    DWORD dwOwningPid;
    LARGE_INTEGER liCreateTimestamp;
    union {
        struct {
            int SpecificPortBind : 1;
        };
        int dwFlags;
    };
    ULONGLONG OwningModuleInfo[TCPIP_OWNING_MODULE_SIZE];
} MIB_UDP6ROW_OWNER_MODULE, *PMIB_UDP6ROW_OWNER_MODULE;
MIME_STATIC_ASSERT(sizeof(MIB_UDP6ROW_OWNER_MODULE) == 176, "MIB_UDP6ROW_OWNER_MODULE");

typedef struct _MIB_UDP6TABLE_OWNER_MODULE {
    DWORD dwNumEntries;
    MIB_UDP6ROW_OWNER_MODULE table[1];
} MIB_UDP6TABLE_OWNER_MODULE, *PMIB_UDP6TABLE_OWNER_MODULE;

/* ------------------------------------------------------------------ change notifications */

typedef void(NETIOAPI_API_ *PIPFORWARD_CHANGE_CALLBACK)(PVOID context, PMIB_IPFORWARD_ROW2 row,
                                                        MIB_NOTIFICATION_TYPE type);
typedef void(NETIOAPI_API_ *PIPINTERFACE_CHANGE_CALLBACK)(PVOID context, PMIB_IPINTERFACE_ROW row,
                                                          MIB_NOTIFICATION_TYPE type);
typedef void(NETIOAPI_API_ *PUNICAST_IPADDRESS_CHANGE_CALLBACK)(PVOID context, PMIB_UNICASTIPADDRESS_ROW row,
                                                                MIB_NOTIFICATION_TYPE type);

typedef enum _NL_NETWORK_CONNECTIVITY_LEVEL_HINT {
    NetworkConnectivityLevelHintUnknown = 0,
    NetworkConnectivityLevelHintNone,
    NetworkConnectivityLevelHintLocalAccess,
    NetworkConnectivityLevelHintInternetAccess,
    NetworkConnectivityLevelHintConstrainedInternetAccess,
    NetworkConnectivityLevelHintHidden,
} NL_NETWORK_CONNECTIVITY_LEVEL_HINT;

typedef enum _NL_NETWORK_CONNECTIVITY_COST_HINT {
    NetworkConnectivityCostHintUnknown = 0,
    NetworkConnectivityCostHintUnrestricted,
    NetworkConnectivityCostHintFixed,
    NetworkConnectivityCostHintVariable,
} NL_NETWORK_CONNECTIVITY_COST_HINT;

typedef struct _NL_NETWORK_CONNECTIVITY_HINT {
    NL_NETWORK_CONNECTIVITY_LEVEL_HINT ConnectivityLevel;
    NL_NETWORK_CONNECTIVITY_COST_HINT ConnectivityCost;
    BOOLEAN ApproachingDataLimit;
    BOOLEAN OverDataLimit;
    BOOLEAN Roaming;
} NL_NETWORK_CONNECTIVITY_HINT;
MIME_STATIC_ASSERT(sizeof(NL_NETWORK_CONNECTIVITY_HINT) == 12, "NL_NETWORK_CONNECTIVITY_HINT");

typedef void(NETIOAPI_API_ *PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK)(PVOID context,
                                                                        NL_NETWORK_CONNECTIVITY_HINT hint);

/* ------------------------------------------------------------------ functions */

IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved,
                                                       PIP_ADAPTER_ADDRESSES addresses, PULONG size);
IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetNetworkParams(PFIXED_INFO info, PULONG size);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetPerAdapterInfo(ULONG index, PIP_PER_ADAPTER_INFO info, PULONG size);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetInterfaceInfo(PIP_INTERFACE_INFO table, PULONG size);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetNumberOfInterfaces(PDWORD count);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetAdapterIndex(LPWSTR name, PULONG index);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetFriendlyIfIndex(DWORD index);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI IpReleaseAddress(PIP_ADAPTER_INDEX_MAP adapter);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI IpRenewAddress(PIP_ADAPTER_INDEX_MAP adapter);

IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetIfTable(PMIB_IFTABLE table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetIfEntry(PMIB_IFROW row);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIfTable2(PMIB_IF_TABLE2 *table);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIfTable2Ex(MIB_IF_TABLE_LEVEL level, PMIB_IF_TABLE2 *table);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIfEntry2(PMIB_IF_ROW2 row);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIfEntry2Ex(MIB_IF_ENTRY_LEVEL level, PMIB_IF_ROW2 row);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIpInterfaceTable(ADDRESS_FAMILY family, PMIB_IPINTERFACE_TABLE *table);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIpInterfaceEntry(PMIB_IPINTERFACE_ROW row);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ InitializeIpInterfaceEntry(PMIB_IPINTERFACE_ROW row);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetUnicastIpAddressTable(ADDRESS_FAMILY family,
                                                                  PMIB_UNICASTIPADDRESS_TABLE *table);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetUnicastIpAddressEntry(PMIB_UNICASTIPADDRESS_ROW row);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ InitializeUnicastIpAddressEntry(PMIB_UNICASTIPADDRESS_ROW row);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetAnycastIpAddressTable(ADDRESS_FAMILY family,
                                                                  PMIB_ANYCASTIPADDRESS_TABLE *table);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetAnycastIpAddressEntry(PMIB_ANYCASTIPADDRESS_ROW row);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetMulticastIpAddressTable(ADDRESS_FAMILY family,
                                                                    PMIB_MULTICASTIPADDRESS_TABLE *table);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetMulticastIpAddressEntry(PMIB_MULTICASTIPADDRESS_ROW row);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetIpAddrTable(PMIB_IPADDRTABLE table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIpNetTable2(ADDRESS_FAMILY family, PMIB_IPNET_TABLE2 *table);
IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetIpNetTable(PMIB_IPNETTABLE table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ResolveIpNetEntry2(PMIB_IPNET_ROW2 row, const SOCKADDR_INET *source);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI SendARP(ULONG destination, ULONG source, PVOID address, PULONG length);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ FreeMibTable(PVOID memory);

IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIpForwardTable2(ADDRESS_FAMILY family, PMIB_IPFORWARD_TABLE2 *table);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetIpForwardTable(PMIB_IPFORWARDTABLE table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetBestRoute2(NET_LUID *luid, NET_IFINDEX index, const SOCKADDR_INET *source,
                                                       const SOCKADDR_INET *destination, ULONG options,
                                                       PMIB_IPFORWARD_ROW2 best, SOCKADDR_INET *best_source);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetBestRoute(DWORD destination, DWORD source, PMIB_IPFORWARDROW best);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetBestInterface(DWORD destination, PDWORD index);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetBestInterfaceEx(struct sockaddr *destination, PDWORD index);

IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetTcpTable(PMIB_TCPTABLE table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetTcpTable2(PMIB_TCPTABLE2 table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetExtendedTcpTable(PVOID table, PDWORD size, BOOL order, ULONG family,
                                                      TCP_TABLE_CLASS table_class, ULONG reserved);
IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetUdpTable(PMIB_UDPTABLE table, PULONG size, BOOL order);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetExtendedUdpTable(PVOID table, PDWORD size, BOOL order, ULONG family,
                                                      UDP_TABLE_CLASS table_class, ULONG reserved);

IPHLPAPI_DLL_LINKAGE DWORD WINAPI NotifyAddrChange(PHANDLE handle, LPOVERLAPPED overlapped);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI NotifyRouteChange(PHANDLE handle, LPOVERLAPPED overlapped);
IPHLPAPI_DLL_LINKAGE BOOL WINAPI CancelIPChangeNotify(LPOVERLAPPED overlapped);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ NotifyRouteChange2(ADDRESS_FAMILY family, PIPFORWARD_CHANGE_CALLBACK callback,
                                                            PVOID context, BOOLEAN initial, HANDLE *handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ NotifyIpInterfaceChange(ADDRESS_FAMILY family,
                                                                 PIPINTERFACE_CHANGE_CALLBACK callback, PVOID context,
                                                                 BOOLEAN initial, HANDLE *handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ NotifyUnicastIpAddressChange(ADDRESS_FAMILY family,
                                                                      PUNICAST_IPADDRESS_CHANGE_CALLBACK callback,
                                                                      PVOID context, BOOLEAN initial,
                                                                      HANDLE *handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ NotifyNetworkConnectivityHintChange(
    PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK callback, PVOID context, BOOLEAN initial, HANDLE *handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetNetworkConnectivityHint(NL_NETWORK_CONNECTIVITY_HINT *hint);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetNetworkConnectivityHintForInterface(NET_IFINDEX index,
                                                                                NL_NETWORK_CONNECTIVITY_HINT *hint);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ CancelMibChangeNotify2(HANDLE handle);

IPHLPAPI_DLL_LINKAGE NET_IFINDEX NETIOAPI_API_ if_nametoindex(PCSTR name);
IPHLPAPI_DLL_LINKAGE PCHAR NETIOAPI_API_ if_indextoname(NET_IFINDEX index, PCHAR name);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceIndexToLuid(NET_IFINDEX index, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToIndex(const NET_LUID *luid, PNET_IFINDEX index);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameA(const NET_LUID *luid, PSTR name, SIZE_T length);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameW(const NET_LUID *luid, PWSTR name, SIZE_T length);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidA(const CHAR *name, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidW(const WCHAR *name, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToGuid(const NET_LUID *luid, GUID *guid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceGuidToLuid(const GUID *guid, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToAlias(const NET_LUID *luid, PWSTR alias, SIZE_T length);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceAliasToLuid(const WCHAR *alias, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertLengthToIpv4Mask(ULONG length, PULONG mask);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertIpv4MaskToLength(ULONG mask, PUINT8 length);

#endif
