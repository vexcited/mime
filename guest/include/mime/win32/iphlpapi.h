/*
 * ip helper: iphlpapi.dll. the adapter, interface and route structures of
 * iptypes.h, ifdef.h, nldef.h and netioapi.h, with the 64 bit layouts.
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

typedef unsigned char UINT8;
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
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_UNICAST_ADDRESS_LH) == 64, "IP_ADAPTER_UNICAST_ADDRESS_LH");

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
    IP_ADAPTER_GATEWAY_ADDRESS_LH, *PIP_ADAPTER_GATEWAY_ADDRESS_LH;

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
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_PREFIX_XP) == 40, "IP_ADAPTER_PREFIX_XP");

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
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, Flags, 92);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, ZoneIndices, 112);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, Luid, 224);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, NetworkGuid, 252);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, Dhcpv6Server, 280);
MIME_FIELD_AT(IP_ADAPTER_ADDRESSES_LH, FirstDnsSuffix, 440);
MIME_STATIC_ASSERT(sizeof(IP_ADAPTER_ADDRESSES_LH) == 448, "IP_ADAPTER_ADDRESSES_LH");

/* the ipv4 only adapter list of GetAdaptersInfo; mime never fills one. */
typedef struct _IP_ADAPTER_INFO IP_ADAPTER_INFO, *PIP_ADAPTER_INFO;

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

typedef void(NETIOAPI_API_ *PIPFORWARD_CHANGE_CALLBACK)(PVOID context, PMIB_IPFORWARD_ROW2 row,
                                                        MIB_NOTIFICATION_TYPE type);

/* the interface rows are never passed: the only notification is the initial one, without a row. */
typedef struct _MIB_IPINTERFACE_ROW MIB_IPINTERFACE_ROW, *PMIB_IPINTERFACE_ROW;
typedef void(NETIOAPI_API_ *PIPINTERFACE_CHANGE_CALLBACK)(PVOID context, PMIB_IPINTERFACE_ROW row,
                                                          MIB_NOTIFICATION_TYPE type);

/* ------------------------------------------------------------------ functions */

IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved,
                                                       PIP_ADAPTER_ADDRESSES addresses, PULONG size);
IPHLPAPI_DLL_LINKAGE ULONG WINAPI GetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size);
IPHLPAPI_DLL_LINKAGE DWORD WINAPI GetNumberOfInterfaces(PDWORD count);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetIpForwardTable2(ADDRESS_FAMILY family, PMIB_IPFORWARD_TABLE2 *table);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ FreeMibTable(PVOID memory);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ NotifyRouteChange2(ADDRESS_FAMILY family, PIPFORWARD_CHANGE_CALLBACK callback,
                                                            PVOID context, BOOLEAN initial, HANDLE *handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ NotifyIpInterfaceChange(ADDRESS_FAMILY family,
                                                                 PIPINTERFACE_CHANGE_CALLBACK callback, PVOID context,
                                                                 BOOLEAN initial, HANDLE *handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ CancelMibChangeNotify2(HANDLE handle);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ GetBestRoute2(NET_LUID *luid, NET_IFINDEX index, const SOCKADDR_INET *source,
                                                       const SOCKADDR_INET *destination, ULONG options,
                                                       PMIB_IPFORWARD_ROW2 best, SOCKADDR_INET *best_source);
IPHLPAPI_DLL_LINKAGE NET_IFINDEX NETIOAPI_API_ if_nametoindex(PCSTR name);
IPHLPAPI_DLL_LINKAGE PCHAR NETIOAPI_API_ if_indextoname(NET_IFINDEX index, PCHAR name);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceIndexToLuid(NET_IFINDEX index, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToIndex(const NET_LUID *luid, PNET_IFINDEX index);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameA(const NET_LUID *luid, PSTR name, SIZE_T length);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameW(const NET_LUID *luid, PWSTR name, SIZE_T length);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidA(const CHAR *name, PNET_LUID luid);
IPHLPAPI_DLL_LINKAGE DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidW(const WCHAR *name, PNET_LUID luid);

#endif
