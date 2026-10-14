/*
 * windows sockets 2 tcp/ip extensions: name resolution, address strings
 * and the ip level structures of ws2tcpip.h and ws2ipdef.h.
 */
#ifndef MIME_WIN32_WS2TCPIP_H
#define MIME_WIN32_WS2TCPIP_H

#include <mime/win32/winsock2.h>

typedef int socklen_t;

typedef struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
} ADDRINFOA, *PADDRINFOA;
MIME_STATIC_ASSERT(sizeof(ADDRINFOA) == (sizeof(void *) == 4 ? 32 : 48), "ADDRINFOA");

typedef struct addrinfoW {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    PWSTR ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfoW *ai_next;
} ADDRINFOW, *PADDRINFOW;

#define AI_PASSIVE 0x00000001
#define AI_CANONNAME 0x00000002
#define AI_NUMERICHOST 0x00000004
#define AI_NUMERICSERV 0x00000008
#define AI_DNS_ONLY 0x00000010
#define AI_FORCE_CLEAR_TEXT 0x00000020
#define AI_BYPASS_DNS_CACHE 0x00000040
#define AI_RETURN_TTL 0x00000080
#define AI_ALL 0x00000100
#define AI_ADDRCONFIG 0x00000400
#define AI_V4MAPPED 0x00000800
#define AI_NON_AUTHORITATIVE 0x00004000
#define AI_SECURE 0x00008000
#define AI_RETURN_PREFERRED_NAMES 0x00010000
#define AI_FQDN 0x00020000
#define AI_FILESERVER 0x00040000
#define AI_DISABLE_IDN_ENCODING 0x00080000
#define AI_SECURE_WITH_FALLBACK 0x00100000
#define AI_EXCLUSIVE_CUSTOM_SERVERS 0x00200000
#define AI_RETURN_RESPONSE_FLAGS 0x10000000
#define AI_REQUIRE_SECURE 0x20000000
#define AI_RESOLUTION_HANDLE 0x40000000
#define AI_EXTENDED 0x80000000

#define EAI_AGAIN WSATRY_AGAIN
#define EAI_BADFLAGS WSAEINVAL
#define EAI_FAIL WSANO_RECOVERY
#define EAI_FAMILY WSAEAFNOSUPPORT
#define EAI_MEMORY WSA_NOT_ENOUGH_MEMORY
#define EAI_NOSECURENAME WSA_SECURE_HOST_NOT_FOUND
#define EAI_NONAME WSAHOST_NOT_FOUND
#define EAI_SERVICE WSATYPE_NOT_FOUND
#define EAI_SOCKTYPE WSAESOCKTNOSUPPORT
#define EAI_IPSECPOLICY WSA_IPSEC_NAME_POLICY_ERROR
#define EAI_NODATA EAI_NONAME

#define NI_NOFQDN 0x01
#define NI_NUMERICHOST 0x02
#define NI_NAMEREQD 0x04
#define NI_NUMERICSERV 0x08
#define NI_DGRAM 0x10
#define NI_MAXHOST 1025
#define NI_MAXSERV 32

#define INET_ADDRSTRLEN 22
#define INET6_ADDRSTRLEN 65

/* an ipv4 or ipv6 address with its family, used by the ip helper. */
typedef union _SOCKADDR_INET {
    SOCKADDR_IN Ipv4;
    SOCKADDR_IN6 Ipv6;
    ADDRESS_FAMILY si_family;
} SOCKADDR_INET, *PSOCKADDR_INET;
MIME_STATIC_ASSERT(sizeof(SOCKADDR_INET) == 28, "SOCKADDR_INET");

typedef struct ip_mreq {
    IN_ADDR imr_multiaddr;
    IN_ADDR imr_interface;
} IP_MREQ, *PIP_MREQ;

typedef struct ipv6_mreq {
    IN6_ADDR ipv6mr_multiaddr;
    ULONG ipv6mr_interface;
} IPV6_MREQ, *PIPV6_MREQ;

typedef struct in_pktinfo {
    IN_ADDR ipi_addr;
    ULONG ipi_ifindex;
} IN_PKTINFO, *PIN_PKTINFO;

typedef struct in6_pktinfo {
    IN6_ADDR ipi6_addr;
    ULONG ipi6_ifindex;
} IN6_PKTINFO, *PIN6_PKTINFO;

#define IN6ADDR_ANY_INIT {{{0}}}
#define IN6ADDR_LOOPBACK_INIT {{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}}

WINSOCK_API_LINKAGE INT WSAAPI getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA *hints, PADDRINFOA *result);
WINSOCK_API_LINKAGE INT WSAAPI GetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW *hints,
                                            PADDRINFOW *result);
WINSOCK_API_LINKAGE void WSAAPI freeaddrinfo(PADDRINFOA info);
WINSOCK_API_LINKAGE void WSAAPI FreeAddrInfoW(PADDRINFOW info);
WINSOCK_API_LINKAGE INT WSAAPI getnameinfo(const SOCKADDR *addr, socklen_t addrlen, PCHAR host, DWORD hostlen,
                                           PCHAR serv, DWORD servlen, INT flags);
WINSOCK_API_LINKAGE INT WSAAPI GetNameInfoW(const SOCKADDR *addr, socklen_t addrlen, PWCHAR host, DWORD hostlen,
                                            PWCHAR serv, DWORD servlen, INT flags);
WINSOCK_API_LINKAGE INT WSAAPI inet_pton(INT family, PCSTR string, PVOID addr);
WINSOCK_API_LINKAGE INT WSAAPI InetPtonW(INT family, PCWSTR string, PVOID addr);
WINSOCK_API_LINKAGE PCSTR WSAAPI inet_ntop(INT family, const VOID *addr, PSTR string, size_t size);
WINSOCK_API_LINKAGE PCWSTR WSAAPI InetNtopW(INT family, const VOID *addr, PWSTR string, size_t size);

#endif
