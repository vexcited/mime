/*
 * windows sockets 2: ws2_32.dll. the types, constants and error codes of
 * winsock2.h, ws2def.h and ws2ipdef.h, with the 64 bit layouts.
 */
#ifndef MIME_WIN32_WINSOCK2_H
#define MIME_WIN32_WINSOCK2_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_WS2_32
#define WINSOCK_API_LINKAGE
#else
#define WINSOCK_API_LINKAGE DECLSPEC_IMPORT
#endif
#define WSAAPI WINAPI

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned long long u_int64;

typedef UINT_PTR SOCKET;
typedef USHORT ADDRESS_FAMILY;
typedef HANDLE WSAEVENT, *LPWSAEVENT;
typedef OVERLAPPED WSAOVERLAPPED, *LPWSAOVERLAPPED;
typedef unsigned int GROUP;

#define INVALID_SOCKET ((SOCKET)(~0))
#define SOCKET_ERROR (-1)

/* ------------------------------------------------------------------ select */

#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif

typedef struct fd_set {
    u_int fd_count;
    SOCKET fd_array[FD_SETSIZE];
} fd_set, FD_SET, *PFD_SET, *LPFD_SET;

typedef struct timeval {
    long tv_sec;
    long tv_usec;
} TIMEVAL, *PTIMEVAL, *LPTIMEVAL;

WINSOCK_API_LINKAGE int WSAAPI __WSAFDIsSet(SOCKET fd, fd_set *set);

#define FD_CLR(fd, set)                                                                                            \
    do {                                                                                                           \
        u_int __i;                                                                                                 \
        for (__i = 0; __i < ((fd_set *)(set))->fd_count; __i++) {                                                  \
            if (((fd_set *)(set))->fd_array[__i] == (fd)) {                                                        \
                while (__i < ((fd_set *)(set))->fd_count - 1) {                                                    \
                    ((fd_set *)(set))->fd_array[__i] = ((fd_set *)(set))->fd_array[__i + 1];                       \
                    __i++;                                                                                         \
                }                                                                                                  \
                ((fd_set *)(set))->fd_count--;                                                                     \
                break;                                                                                             \
            }                                                                                                      \
        }                                                                                                          \
    } while (0)
#define FD_SET(fd, set)                                                                                            \
    do {                                                                                                           \
        u_int __i;                                                                                                 \
        for (__i = 0; __i < ((fd_set *)(set))->fd_count; __i++)                                                    \
            if (((fd_set *)(set))->fd_array[__i] == (fd))                                                          \
                break;                                                                                             \
        if (__i == ((fd_set *)(set))->fd_count && ((fd_set *)(set))->fd_count < FD_SETSIZE) {                      \
            ((fd_set *)(set))->fd_array[__i] = (fd);                                                               \
            ((fd_set *)(set))->fd_count++;                                                                         \
        }                                                                                                          \
    } while (0)
#define FD_ZERO(set) (((fd_set *)(set))->fd_count = 0)
#define FD_ISSET(fd, set) __WSAFDIsSet((SOCKET)(fd), (fd_set *)(set))

/* ------------------------------------------------------------------ addresses */

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_IMPLINK 3
#define AF_PUP 4
#define AF_CHAOS 5
#define AF_IPX 6
#define AF_NS 6
#define AF_ISO 7
#define AF_OSI AF_ISO
#define AF_ECMA 8
#define AF_DATAKIT 9
#define AF_CCITT 10
#define AF_SNA 11
#define AF_DECnet 12
#define AF_DLI 13
#define AF_LAT 14
#define AF_HYLINK 15
#define AF_APPLETALK 16
#define AF_NETBIOS 17
#define AF_VOICEVIEW 18
#define AF_FIREFOX 19
#define AF_UNKNOWN1 20
#define AF_BAN 21
#define AF_ATM 22
#define AF_INET6 23
#define AF_CLUSTER 24
#define AF_12844 25
#define AF_IRDA 26
#define AF_NETDES 28
#define AF_BTH 32
#define AF_HYPERV 34
#define AF_MAX 35

#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_INET AF_INET
#define PF_IPX AF_IPX
#define PF_NETBIOS AF_NETBIOS
#define PF_INET6 AF_INET6
#define PF_IRDA AF_IRDA
#define PF_BTH AF_BTH
#define PF_MAX AF_MAX

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_RDM 4
#define SOCK_SEQPACKET 5

#define IPPROTO_HOPOPTS 0
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_IGMP 2
#define IPPROTO_GGP 3
#define IPPROTO_IPV4 4
#define IPPROTO_ST 5
#define IPPROTO_TCP 6
#define IPPROTO_CBT 7
#define IPPROTO_EGP 8
#define IPPROTO_IGP 9
#define IPPROTO_PUP 12
#define IPPROTO_UDP 17
#define IPPROTO_IDP 22
#define IPPROTO_RDP 27
#define IPPROTO_IPV6 41
#define IPPROTO_ROUTING 43
#define IPPROTO_FRAGMENT 44
#define IPPROTO_ESP 50
#define IPPROTO_AH 51
#define IPPROTO_ICMPV6 58
#define IPPROTO_NONE 59
#define IPPROTO_DSTOPTS 60
#define IPPROTO_ND 77
#define IPPROTO_ICLFXBM 78
#define IPPROTO_PIM 103
#define IPPROTO_PGM 113
#define IPPROTO_L2TP 115
#define IPPROTO_SCTP 132
#define IPPROTO_RAW 255
#define IPPROTO_MAX 256

#define IPPORT_ECHO 7
#define IPPORT_DISCARD 9
#define IPPORT_SYSTAT 11
#define IPPORT_DAYTIME 13
#define IPPORT_NETSTAT 15
#define IPPORT_FTP 21
#define IPPORT_TELNET 23
#define IPPORT_SMTP 25
#define IPPORT_TIMESERVER 37
#define IPPORT_NAMESERVER 42
#define IPPORT_WHOIS 43
#define IPPORT_MTP 57
#define IPPORT_TFTP 69
#define IPPORT_RJE 77
#define IPPORT_FINGER 79
#define IPPORT_TTYLINK 87
#define IPPORT_SUPDUP 95
#define IPPORT_RESERVED 1024

typedef struct in_addr {
    union {
        struct {
            UCHAR s_b1, s_b2, s_b3, s_b4;
        } S_un_b;
        struct {
            USHORT s_w1, s_w2;
        } S_un_w;
        ULONG S_addr;
    } S_un;
} IN_ADDR, *PIN_ADDR, *LPIN_ADDR;
#define s_addr S_un.S_addr
#define s_host S_un.S_un_b.s_b2
#define s_net S_un.S_un_b.s_b1
#define s_imp S_un.S_un_w.s_w2
#define s_impno S_un.S_un_b.s_b4
#define s_lh S_un.S_un_b.s_b3

typedef struct in6_addr {
    union {
        UCHAR Byte[16];
        USHORT Word[8];
    } u;
} IN6_ADDR, *PIN6_ADDR, *LPIN6_ADDR;
#define in_addr6 in6_addr
#define _S6_un u
#define _S6_u8 Byte
#define s6_addr _S6_un._S6_u8
#define s6_bytes u.Byte
#define s6_words u.Word

#define INADDR_ANY ((ULONG)0x00000000)
#define INADDR_LOOPBACK 0x7f000001
#define INADDR_BROADCAST ((ULONG)0xffffffff)
#define INADDR_NONE 0xffffffff
#define ADDR_ANY INADDR_ANY

typedef struct sockaddr {
    ADDRESS_FAMILY sa_family;
    CHAR sa_data[14];
} SOCKADDR, *PSOCKADDR, *LPSOCKADDR;

typedef struct sockaddr_in {
    ADDRESS_FAMILY sin_family;
    USHORT sin_port;
    IN_ADDR sin_addr;
    CHAR sin_zero[8];
} SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;

typedef struct {
    union {
        struct {
            ULONG Zone : 28;
            ULONG Level : 4;
        };
        ULONG Value;
    };
} SCOPE_ID, *PSCOPE_ID;

typedef struct sockaddr_in6 {
    ADDRESS_FAMILY sin6_family;
    USHORT sin6_port;
    ULONG sin6_flowinfo;
    IN6_ADDR sin6_addr;
    union {
        ULONG sin6_scope_id;
        SCOPE_ID sin6_scope_struct;
    };
} SOCKADDR_IN6, SOCKADDR_IN6_LH, *PSOCKADDR_IN6, *LPSOCKADDR_IN6;
MIME_STATIC_ASSERT(sizeof(SOCKADDR_IN6) == 28, "SOCKADDR_IN6");

#define _SS_MAXSIZE 128
#define _SS_ALIGNSIZE (sizeof(LONGLONG))
#define _SS_PAD1SIZE (_SS_ALIGNSIZE - sizeof(USHORT))
#define _SS_PAD2SIZE (_SS_MAXSIZE - (sizeof(USHORT) + _SS_PAD1SIZE + _SS_ALIGNSIZE))

typedef struct sockaddr_storage {
    ADDRESS_FAMILY ss_family;
    CHAR __ss_pad1[_SS_PAD1SIZE];
    LONGLONG __ss_align;
    CHAR __ss_pad2[_SS_PAD2SIZE];
} SOCKADDR_STORAGE, SOCKADDR_STORAGE_LH, *PSOCKADDR_STORAGE, *LPSOCKADDR_STORAGE;
MIME_STATIC_ASSERT(sizeof(SOCKADDR_STORAGE) == 128, "SOCKADDR_STORAGE");

typedef struct _SOCKET_ADDRESS {
    LPSOCKADDR lpSockaddr;
    INT iSockaddrLength;
} SOCKET_ADDRESS, *PSOCKET_ADDRESS, *LPSOCKET_ADDRESS;

typedef struct _SOCKET_ADDRESS_LIST {
    INT iAddressCount;
    SOCKET_ADDRESS Address[1];
} SOCKET_ADDRESS_LIST, *PSOCKET_ADDRESS_LIST, *LPSOCKET_ADDRESS_LIST;

/* ------------------------------------------------------------------ database entries */

typedef struct hostent {
    char *h_name;
    char **h_aliases;
    short h_addrtype;
    short h_length;
    char **h_addr_list;
} HOSTENT, *PHOSTENT, *LPHOSTENT;
#define h_addr h_addr_list[0]

/* the 64 bit layout puts s_proto before s_port, the 32 bit one has the port first. */
typedef struct servent {
    char *s_name;
    char **s_aliases;
#ifdef __i386__
    short s_port;
    char *s_proto;
#else
    char *s_proto;
    short s_port;
#endif
} SERVENT, *PSERVENT, *LPSERVENT;

typedef struct protoent {
    char *p_name;
    char **p_aliases;
    short p_proto;
} PROTOENT, *PPROTOENT, *LPPROTOENT;

/* ------------------------------------------------------------------ startup */

#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128

typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
#ifdef __i386__
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
#else
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
#endif
} WSADATA, *LPWSADATA;
MIME_FIELD_AT(WSADATA, szDescription, sizeof(void *) == 4 ? 4 : 16);
MIME_STATIC_ASSERT(sizeof(WSADATA) == (sizeof(void *) == 4 ? 400 : 408), "WSADATA");

/* ------------------------------------------------------------------ options */

#define SOL_SOCKET 0xffff

#define SO_DEBUG 0x0001
#define SO_ACCEPTCONN 0x0002
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_DONTROUTE 0x0010
#define SO_BROADCAST 0x0020
#define SO_USELOOPBACK 0x0040
#define SO_LINGER 0x0080
#define SO_OOBINLINE 0x0100
#define SO_DONTLINGER ((int)(~SO_LINGER))
#define SO_EXCLUSIVEADDRUSE ((int)(~SO_REUSEADDR))
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_SNDLOWAT 0x1003
#define SO_RCVLOWAT 0x1004
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define SO_TYPE 0x1008
#define SO_BSP_STATE 0x1009
#define SO_GROUP_ID 0x2001
#define SO_GROUP_PRIORITY 0x2002
#define SO_MAX_MSG_SIZE 0x2003
#define SO_PROTOCOL_INFOA 0x2004
#define SO_PROTOCOL_INFOW 0x2005
#define PVD_CONFIG 0x3001
#define SO_CONDITIONAL_ACCEPT 0x3002
#define SO_PAUSE_ACCEPT 0x3003
#define SO_COMPARTMENT_ID 0x3004
#define SO_RANDOMIZE_PORT 0x3005
#define SO_PORT_SCALABILITY 0x3006
#define SO_REUSE_UNICASTPORT 0x3007
#define SO_REUSE_MULTICASTPORT 0x3008
#define SO_CONNDATA 0x7000
#define SO_CONNOPT 0x7001
#define SO_DISCDATA 0x7002
#define SO_DISCOPT 0x7003
#define SO_CONNDATALEN 0x7004
#define SO_CONNOPTLEN 0x7005
#define SO_DISCDATALEN 0x7006
#define SO_DISCOPTLEN 0x7007
#define SO_OPENTYPE 0x7008
#define SO_MAXDG 0x7009
#define SO_MAXPATHDG 0x700A
#define SO_UPDATE_ACCEPT_CONTEXT 0x700B
#define SO_CONNECT_TIME 0x700C
#define SO_UPDATE_CONNECT_CONTEXT 0x7010

#define TCP_NODELAY 0x0001
#define TCP_EXPEDITED_1122 0x0002
#define TCP_KEEPALIVE 3
#define TCP_MAXSEG 4
#define TCP_MAXRT 5
#define TCP_STDURG 6
#define TCP_NOURG 7
#define TCP_ATMARK 8
#define TCP_NOSYNRETRIES 9
#define TCP_TIMESTAMPS 10
#define TCP_OFFLOAD_PREFERENCE 11
#define TCP_CONGESTION_ALGORITHM 12
#define TCP_DELAY_FIN_ACK 13
#define TCP_MAXRTMS 14
#define TCP_FASTOPEN 15
#define TCP_KEEPCNT 16
#define TCP_KEEPIDLE TCP_KEEPALIVE
#define TCP_KEEPINTVL 17
#define TCP_FAIL_CONNECT_ON_ICMP_ERROR 18
#define TCP_ICMP_ERROR_INFO 19

#define IP_OPTIONS 1
#define IP_HDRINCL 2
#define IP_TOS 3
#define IP_TTL 4
#define IP_MULTICAST_IF 9
#define IP_MULTICAST_TTL 10
#define IP_MULTICAST_LOOP 11
#define IP_ADD_MEMBERSHIP 12
#define IP_DROP_MEMBERSHIP 13
#define IP_DONTFRAGMENT 14
#define IP_ADD_SOURCE_MEMBERSHIP 15
#define IP_DROP_SOURCE_MEMBERSHIP 16
#define IP_BLOCK_SOURCE 17
#define IP_UNBLOCK_SOURCE 18
#define IP_PKTINFO 19
#define IP_HOPLIMIT 21
#define IP_RECVTTL 21
#define IP_RECEIVE_BROADCAST 22
#define IP_RECVIF 24
#define IP_RECVDSTADDR 25
#define IP_IFLIST 28
#define IP_ADD_IFLIST 29
#define IP_DEL_IFLIST 30
#define IP_UNICAST_IF 31
#define IP_RTHDR 32
#define IP_RECVRTHDR 38
#define IP_TCLASS 39
#define IP_RECVTCLASS 40
#define IP_RECVTOS 40
#define IP_ORIGINAL_ARRIVAL_IF 47
#define IP_ECN 50
#define IP_PKTINFO_EX 51
#define IP_WFP_REDIRECT_RECORDS 60
#define IP_WFP_REDIRECT_CONTEXT 70
#define IP_MTU_DISCOVER 71
#define IP_MTU 73
#define IP_RECVERR 75

#define IPV6_HOPOPTS 1
#define IPV6_HDRINCL 2
#define IPV6_UNICAST_HOPS 4
#define IPV6_MULTICAST_IF 9
#define IPV6_MULTICAST_HOPS 10
#define IPV6_MULTICAST_LOOP 11
#define IPV6_ADD_MEMBERSHIP 12
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#define IPV6_DROP_MEMBERSHIP 13
#define IPV6_LEAVE_GROUP IPV6_DROP_MEMBERSHIP
#define IPV6_DONTFRAG 14
#define IPV6_PKTINFO 19
#define IPV6_HOPLIMIT 21
#define IPV6_PROTECTION_LEVEL 23
#define IPV6_RECVIF 24
#define IPV6_RECVDSTADDR 25
#define IPV6_CHECKSUM 26
#define IPV6_V6ONLY 27
#define IPV6_IFLIST 28
#define IPV6_ADD_IFLIST 29
#define IPV6_DEL_IFLIST 30
#define IPV6_UNICAST_IF 31
#define IPV6_RTHDR 32
#define IPV6_RECVRTHDR 38
#define IPV6_TCLASS 39
#define IPV6_RECVTCLASS 40
#define IPV6_ECN 50
#define IPV6_PKTINFO_EX 51
#define IPV6_MTU_DISCOVER 71
#define IPV6_MTU 72
#define IPV6_RECVERR 75

typedef struct linger {
    u_short l_onoff;
    u_short l_linger;
} LINGER, *PLINGER, *LPLINGER;

#define SOMAXCONN 0x7fffffff
#define SOMAXCONN_HINT(b) (-(b))

#define MSG_OOB 0x1
#define MSG_PEEK 0x2
#define MSG_DONTROUTE 0x4
#define MSG_WAITALL 0x8
#define MSG_PUSH_IMMEDIATE 0x20
#define MSG_PARTIAL 0x8000
#define MSG_INTERRUPT 0x10
#define MSG_TRUNC 0x0100
#define MSG_CTRUNC 0x0200
#define MSG_BCAST 0x0400
#define MSG_MCAST 0x0800
#define MSG_ERRQUEUE 0x1000
#define MSG_MAXIOVLEN 16

#define SD_RECEIVE 0x00
#define SD_SEND 0x01
#define SD_BOTH 0x02

/* ------------------------------------------------------------------ ioctls */

#define IOCPARM_MASK 0x7f
#define IOC_VOID 0x20000000
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define IOC_INOUT (IOC_IN | IOC_OUT)
#define _IO(x, y) (IOC_VOID | ((x) << 8) | (y))
#define _IOR(x, y, t) (IOC_OUT | (((long)sizeof(t) & IOCPARM_MASK) << 16) | ((x) << 8) | (y))
#define _IOW(x, y, t) (IOC_IN | (((long)sizeof(t) & IOCPARM_MASK) << 16) | ((x) << 8) | (y))

#define FIONREAD _IOR('f', 127, u_long)
#define FIONBIO _IOW('f', 126, u_long)
#define FIOASYNC _IOW('f', 125, u_long)
#define SIOCSHIWAT _IOW('s', 0, u_long)
#define SIOCGHIWAT _IOR('s', 1, u_long)
#define SIOCSLOWAT _IOW('s', 2, u_long)
#define SIOCGLOWAT _IOR('s', 3, u_long)
#define SIOCATMARK _IOR('s', 7, u_long)

#define IOC_UNIX 0x00000000
#define IOC_WS2 0x08000000
#define IOC_PROTOCOL 0x10000000
#define IOC_VENDOR 0x18000000
#define _WSAIO(x, y) (IOC_VOID | (x) | (y))
#define _WSAIOR(x, y) (IOC_OUT | (x) | (y))
#define _WSAIOW(x, y) (IOC_IN | (x) | (y))
#define _WSAIORW(x, y) (IOC_INOUT | (x) | (y))

#define SIO_ASSOCIATE_HANDLE _WSAIOW(IOC_WS2, 1)
#define SIO_ENABLE_CIRCULAR_QUEUEING _WSAIO(IOC_WS2, 2)
#define SIO_FIND_ROUTE _WSAIOR(IOC_WS2, 3)
#define SIO_FLUSH _WSAIO(IOC_WS2, 4)
#define SIO_GET_BROADCAST_ADDRESS _WSAIOR(IOC_WS2, 5)
#define SIO_GET_EXTENSION_FUNCTION_POINTER _WSAIORW(IOC_WS2, 6)
#define SIO_GET_QOS _WSAIORW(IOC_WS2, 7)
#define SIO_GET_GROUP_QOS _WSAIORW(IOC_WS2, 8)
#define SIO_MULTIPOINT_LOOPBACK _WSAIOW(IOC_WS2, 9)
#define SIO_MULTICAST_SCOPE _WSAIOW(IOC_WS2, 10)
#define SIO_SET_QOS _WSAIOW(IOC_WS2, 11)
#define SIO_SET_GROUP_QOS _WSAIOW(IOC_WS2, 12)
#define SIO_TRANSLATE_HANDLE _WSAIORW(IOC_WS2, 13)
#define SIO_ROUTING_INTERFACE_QUERY _WSAIORW(IOC_WS2, 20)
#define SIO_ROUTING_INTERFACE_CHANGE _WSAIOW(IOC_WS2, 21)
#define SIO_ADDRESS_LIST_QUERY _WSAIOR(IOC_WS2, 22)
#define SIO_ADDRESS_LIST_CHANGE _WSAIO(IOC_WS2, 23)
#define SIO_QUERY_TARGET_PNP_HANDLE _WSAIOR(IOC_WS2, 24)
#define SIO_QUERY_RSS_PROCESSOR_INFO _WSAIOR(IOC_WS2, 37)
#define SIO_ADDRESS_LIST_SORT _WSAIORW(IOC_WS2, 25)
#define SIO_RESERVED_1 _WSAIOW(IOC_WS2, 26)
#define SIO_RESERVED_2 _WSAIOW(IOC_WS2, 33)
#define SIO_GET_INTERFACE_LIST _IOR('t', 127, u_long)
#define SIO_GET_INTERFACE_LIST_EX _IOR('t', 126, u_long)
#define SIO_SET_MULTICAST_FILTER _IOW('t', 125, u_long)
#define SIO_GET_MULTICAST_FILTER _IOW('t', 124 | IOC_IN, u_long)
#define SIO_BASE_HANDLE _WSAIOR(IOC_WS2, 34)
#define SIO_BSP_HANDLE _WSAIOR(IOC_WS2, 27)
#define SIO_BSP_HANDLE_SELECT _WSAIOR(IOC_WS2, 28)
#define SIO_BSP_HANDLE_POLL _WSAIOR(IOC_WS2, 29)
#define SIO_EXT_SELECT _WSAIORW(IOC_WS2, 30)
#define SIO_EXT_POLL _WSAIORW(IOC_WS2, 31)
#define SIO_EXT_SENDMSG _WSAIORW(IOC_WS2, 32)
#define SIO_RCVALL _WSAIOW(IOC_VENDOR, 1)
#define SIO_RCVALL_MCAST _WSAIOW(IOC_VENDOR, 2)
#define SIO_RCVALL_IGMPMCAST _WSAIOW(IOC_VENDOR, 3)
#define SIO_KEEPALIVE_VALS _WSAIOW(IOC_VENDOR, 4)
#define SIO_ABSORB_RTRALERT _WSAIOW(IOC_VENDOR, 5)
#define SIO_UCAST_IF _WSAIOW(IOC_VENDOR, 6)
#define SIO_LIMIT_BROADCASTS _WSAIOW(IOC_VENDOR, 7)
#define SIO_INDEX_BIND _WSAIOW(IOC_VENDOR, 8)
#define SIO_INDEX_MCASTIF _WSAIOW(IOC_VENDOR, 9)
#define SIO_INDEX_ADD_MCAST _WSAIOW(IOC_VENDOR, 10)
#define SIO_INDEX_DEL_MCAST _WSAIOW(IOC_VENDOR, 11)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#define SIO_RCVALL_MCAST_IF _WSAIOW(IOC_VENDOR, 13)
#define SIO_RCVALL_IF _WSAIOW(IOC_VENDOR, 14)
#define SIO_UDP_NETRESET _WSAIOW(IOC_VENDOR, 15)
#define SIO_LOOPBACK_FAST_PATH _WSAIOW(IOC_VENDOR, 16)
#define SIO_TCP_INITIAL_RTO _WSAIOW(IOC_VENDOR, 17)
#define SIO_TCP_SET_ICW _WSAIOW(IOC_VENDOR, 22)
#define SIO_TCP_SET_ACK_FREQUENCY _WSAIOW(IOC_VENDOR, 23)
#define SIO_TCP_INFO _WSAIORW(IOC_VENDOR, 39)

struct tcp_keepalive {
    ULONG onoff;
    ULONG keepalivetime;
    ULONG keepaliveinterval;
};

typedef union sockaddr_gen {
    struct sockaddr Address;
    struct sockaddr_in AddressIn;
    struct sockaddr_in6 AddressIn6;
} sockaddr_gen;

typedef struct _INTERFACE_INFO {
    ULONG iiFlags;
    sockaddr_gen iiAddress;
    sockaddr_gen iiBroadcastAddress;
    sockaddr_gen iiNetmask;
} INTERFACE_INFO, *LPINTERFACE_INFO;

#define IFF_UP 0x00000001
#define IFF_BROADCAST 0x00000002
#define IFF_LOOPBACK 0x00000004
#define IFF_POINTTOPOINT 0x00000008
#define IFF_MULTICAST 0x00000010

/* ------------------------------------------------------------------ asynchronous notification */

#define FD_READ_BIT 0
#define FD_READ (1 << FD_READ_BIT)
#define FD_WRITE_BIT 1
#define FD_WRITE (1 << FD_WRITE_BIT)
#define FD_OOB_BIT 2
#define FD_OOB (1 << FD_OOB_BIT)
#define FD_ACCEPT_BIT 3
#define FD_ACCEPT (1 << FD_ACCEPT_BIT)
#define FD_CONNECT_BIT 4
#define FD_CONNECT (1 << FD_CONNECT_BIT)
#define FD_CLOSE_BIT 5
#define FD_CLOSE (1 << FD_CLOSE_BIT)
#define FD_QOS_BIT 6
#define FD_QOS (1 << FD_QOS_BIT)
#define FD_GROUP_QOS_BIT 7
#define FD_GROUP_QOS (1 << FD_GROUP_QOS_BIT)
#define FD_ROUTING_INTERFACE_CHANGE_BIT 8
#define FD_ROUTING_INTERFACE_CHANGE (1 << FD_ROUTING_INTERFACE_CHANGE_BIT)
#define FD_ADDRESS_LIST_CHANGE_BIT 9
#define FD_ADDRESS_LIST_CHANGE (1 << FD_ADDRESS_LIST_CHANGE_BIT)
#define FD_MAX_EVENTS 10
#define FD_ALL_EVENTS ((1 << FD_MAX_EVENTS) - 1)

typedef struct _WSANETWORKEVENTS {
    long lNetworkEvents;
    int iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

#define WSAMAKEASYNCREPLY(buflen, error) MAKELONG(buflen, error)
#define WSAMAKESELECTREPLY(event, error) MAKELONG(event, error)
#define WSAGETASYNCBUFLEN(lparam) LOWORD(lparam)
#define WSAGETASYNCERROR(lparam) HIWORD(lparam)
#define WSAGETSELECTEVENT(lparam) LOWORD(lparam)
#define WSAGETSELECTERROR(lparam) HIWORD(lparam)

#define WSA_IO_PENDING (ERROR_IO_PENDING)
#define WSA_IO_INCOMPLETE (ERROR_IO_INCOMPLETE)
#define WSA_INVALID_HANDLE (ERROR_INVALID_HANDLE)
#define WSA_INVALID_PARAMETER (ERROR_INVALID_PARAMETER)
#define WSA_NOT_ENOUGH_MEMORY (ERROR_NOT_ENOUGH_MEMORY)
#define WSA_OPERATION_ABORTED (ERROR_OPERATION_ABORTED)
#define WSA_INVALID_EVENT ((WSAEVENT)NULL)
#define WSA_MAXIMUM_WAIT_EVENTS (MAXIMUM_WAIT_OBJECTS)
#define WSA_WAIT_FAILED (WAIT_FAILED)
#define WSA_WAIT_EVENT_0 (WAIT_OBJECT_0)
#define WSA_WAIT_IO_COMPLETION (WAIT_IO_COMPLETION)
#define WSA_WAIT_TIMEOUT (WAIT_TIMEOUT)
#define WSA_INFINITE (INFINITE)

typedef struct _WSABUF {
    ULONG len;
    CHAR *buf;
} WSABUF, *LPWSABUF;

typedef struct _WSAMSG {
    LPSOCKADDR name;
    INT namelen;
    LPWSABUF lpBuffers;
    ULONG dwBufferCount;
    WSABUF Control;
    ULONG dwFlags;
} WSAMSG, *PWSAMSG, *LPWSAMSG;

typedef void(CALLBACK *LPWSAOVERLAPPED_COMPLETION_ROUTINE)(DWORD error, DWORD transferred,
                                                           LPWSAOVERLAPPED overlapped, DWORD flags);

typedef struct pollfd {
    SOCKET fd;
    SHORT events;
    SHORT revents;
} WSAPOLLFD, *PWSAPOLLFD, *LPWSAPOLLFD;

#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define POLLIN (POLLRDNORM | POLLRDBAND)
#define POLLPRI 0x0400
#define POLLWRNORM 0x0010
#define POLLOUT (POLLWRNORM)
#define POLLWRBAND 0x0020
#define POLLERR 0x0001
#define POLLHUP 0x0002
#define POLLNVAL 0x0004

/* ------------------------------------------------------------------ protocol information */

#define MAX_PROTOCOL_CHAIN 7
#define BASE_PROTOCOL 1
#define LAYERED_PROTOCOL 0
#define WSAPROTOCOL_LEN 255

typedef struct _WSAPROTOCOLCHAIN {
    int ChainLen;
    DWORD ChainEntries[MAX_PROTOCOL_CHAIN];
} WSAPROTOCOLCHAIN, *LPWSAPROTOCOLCHAIN;

typedef struct _WSAPROTOCOL_INFOA {
    DWORD dwServiceFlags1;
    DWORD dwServiceFlags2;
    DWORD dwServiceFlags3;
    DWORD dwServiceFlags4;
    DWORD dwProviderFlags;
    GUID ProviderId;
    DWORD dwCatalogEntryId;
    WSAPROTOCOLCHAIN ProtocolChain;
    int iVersion;
    int iAddressFamily;
    int iMaxSockAddr;
    int iMinSockAddr;
    int iSocketType;
    int iProtocol;
    int iProtocolMaxOffset;
    int iNetworkByteOrder;
    int iSecurityScheme;
    DWORD dwMessageSize;
    DWORD dwProviderReserved;
    CHAR szProtocol[WSAPROTOCOL_LEN + 1];
} WSAPROTOCOL_INFOA, *LPWSAPROTOCOL_INFOA;
MIME_STATIC_ASSERT(sizeof(WSAPROTOCOL_INFOA) == 372, "WSAPROTOCOL_INFOA");

typedef struct _WSAPROTOCOL_INFOW {
    DWORD dwServiceFlags1;
    DWORD dwServiceFlags2;
    DWORD dwServiceFlags3;
    DWORD dwServiceFlags4;
    DWORD dwProviderFlags;
    GUID ProviderId;
    DWORD dwCatalogEntryId;
    WSAPROTOCOLCHAIN ProtocolChain;
    int iVersion;
    int iAddressFamily;
    int iMaxSockAddr;
    int iMinSockAddr;
    int iSocketType;
    int iProtocol;
    int iProtocolMaxOffset;
    int iNetworkByteOrder;
    int iSecurityScheme;
    DWORD dwMessageSize;
    DWORD dwProviderReserved;
    WCHAR szProtocol[WSAPROTOCOL_LEN + 1];
} WSAPROTOCOL_INFOW, *LPWSAPROTOCOL_INFOW;
MIME_STATIC_ASSERT(sizeof(WSAPROTOCOL_INFOW) == 628, "WSAPROTOCOL_INFOW");

#define XP1_CONNECTIONLESS 0x00000001
#define XP1_GUARANTEED_DELIVERY 0x00000002
#define XP1_GUARANTEED_ORDER 0x00000004
#define XP1_MESSAGE_ORIENTED 0x00000008
#define XP1_PSEUDO_STREAM 0x00000010
#define XP1_GRACEFUL_CLOSE 0x00000020
#define XP1_EXPEDITED_DATA 0x00000040
#define XP1_CONNECT_DATA 0x00000080
#define XP1_DISCONNECT_DATA 0x00000100
#define XP1_SUPPORT_BROADCAST 0x00000200
#define XP1_SUPPORT_MULTIPOINT 0x00000400
#define XP1_MULTIPOINT_CONTROL_PLANE 0x00000800
#define XP1_MULTIPOINT_DATA_PLANE 0x00001000
#define XP1_QOS_SUPPORTED 0x00002000
#define XP1_INTERRUPT 0x00004000
#define XP1_UNI_SEND 0x00008000
#define XP1_UNI_RECV 0x00010000
#define XP1_IFS_HANDLES 0x00020000
#define XP1_PARTIAL_MESSAGE 0x00040000
#define XP1_SAN_SUPPORT_SDP 0x00080000

#define PFL_MULTIPLE_PROTO_ENTRIES 0x00000001
#define PFL_RECOMMENDED_PROTO_ENTRY 0x00000002
#define PFL_HIDDEN 0x00000004
#define PFL_MATCHES_PROTOCOL_ZERO 0x00000008
#define PFL_NETWORKDIRECT_PROVIDER 0x00000010

#define BIGENDIAN 0x0000
#define LITTLEENDIAN 0x0001
#define SECURITY_PROTOCOL_NONE 0x0000

#define FROM_PROTOCOL_INFO (-1)

#define WSA_FLAG_OVERLAPPED 0x01
#define WSA_FLAG_MULTIPOINT_C_ROOT 0x02
#define WSA_FLAG_MULTIPOINT_C_LEAF 0x04
#define WSA_FLAG_MULTIPOINT_D_ROOT 0x08
#define WSA_FLAG_MULTIPOINT_D_LEAF 0x10
#define WSA_FLAG_ACCESS_SYSTEM_SECURITY 0x40
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#define WSA_FLAG_REGISTERED_IO 0x100

/* ------------------------------------------------------------------ errors */

#define WSABASEERR 10000
#define WSAEINTR 10004L
#define WSAEBADF 10009L
#define WSAEACCES 10013L
#define WSAEFAULT 10014L
#define WSAEINVAL 10022L
#define WSAEMFILE 10024L
#define WSAEWOULDBLOCK 10035L
#define WSAEINPROGRESS 10036L
#define WSAEALREADY 10037L
#define WSAENOTSOCK 10038L
#define WSAEDESTADDRREQ 10039L
#define WSAEMSGSIZE 10040L
#define WSAEPROTOTYPE 10041L
#define WSAENOPROTOOPT 10042L
#define WSAEPROTONOSUPPORT 10043L
#define WSAESOCKTNOSUPPORT 10044L
#define WSAEOPNOTSUPP 10045L
#define WSAEPFNOSUPPORT 10046L
#define WSAEAFNOSUPPORT 10047L
#define WSAEADDRINUSE 10048L
#define WSAEADDRNOTAVAIL 10049L
#define WSAENETDOWN 10050L
#define WSAENETUNREACH 10051L
#define WSAENETRESET 10052L
#define WSAECONNABORTED 10053L
#define WSAECONNRESET 10054L
#define WSAENOBUFS 10055L
#define WSAEISCONN 10056L
#define WSAENOTCONN 10057L
#define WSAESHUTDOWN 10058L
#define WSAETOOMANYREFS 10059L
#define WSAETIMEDOUT 10060L
#define WSAECONNREFUSED 10061L
#define WSAELOOP 10062L
#define WSAENAMETOOLONG 10063L
#define WSAEHOSTDOWN 10064L
#define WSAEHOSTUNREACH 10065L
#define WSAENOTEMPTY 10066L
#define WSAEPROCLIM 10067L
#define WSAEUSERS 10068L
#define WSAEDQUOT 10069L
#define WSAESTALE 10070L
#define WSAEREMOTE 10071L
#define WSASYSNOTREADY 10091L
#define WSAVERNOTSUPPORTED 10092L
#define WSANOTINITIALISED 10093L
#define WSAEDISCON 10101L
#define WSAENOMORE 10102L
#define WSAECANCELLED 10103L
#define WSAEINVALIDPROCTABLE 10104L
#define WSAEINVALIDPROVIDER 10105L
#define WSAEPROVIDERFAILEDINIT 10106L
#define WSASYSCALLFAILURE 10107L
#define WSASERVICE_NOT_FOUND 10108L
#define WSATYPE_NOT_FOUND 10109L
#define WSA_E_NO_MORE 10110L
#define WSA_E_CANCELLED 10111L
#define WSAEREFUSED 10112L
#define WSAHOST_NOT_FOUND 11001L
#define WSATRY_AGAIN 11002L
#define WSANO_RECOVERY 11003L
#define WSANO_DATA 11004L
#define WSA_SECURE_HOST_NOT_FOUND 11032L
#define WSA_IPSEC_NAME_POLICY_ERROR 11033L
#define WSANO_ADDRESS WSANO_DATA

/* ------------------------------------------------------------------ functions */

WINSOCK_API_LINKAGE int WSAAPI WSAStartup(WORD version, LPWSADATA data);
WINSOCK_API_LINKAGE int WSAAPI WSACleanup(void);
WINSOCK_API_LINKAGE int WSAAPI WSAGetLastError(void);
WINSOCK_API_LINKAGE void WSAAPI WSASetLastError(int error);

WINSOCK_API_LINKAGE u_long WSAAPI htonl(u_long v);
WINSOCK_API_LINKAGE u_short WSAAPI htons(u_short v);
WINSOCK_API_LINKAGE u_long WSAAPI ntohl(u_long v);
WINSOCK_API_LINKAGE u_short WSAAPI ntohs(u_short v);
WINSOCK_API_LINKAGE int WSAAPI WSAHtonl(SOCKET s, u_long v, u_long *out);
WINSOCK_API_LINKAGE int WSAAPI WSAHtons(SOCKET s, u_short v, u_short *out);
WINSOCK_API_LINKAGE int WSAAPI WSANtohl(SOCKET s, u_long v, u_long *out);
WINSOCK_API_LINKAGE int WSAAPI WSANtohs(SOCKET s, u_short v, u_short *out);

WINSOCK_API_LINKAGE unsigned long WSAAPI inet_addr(const char *cp);
WINSOCK_API_LINKAGE char *WSAAPI inet_ntoa(struct in_addr in);
WINSOCK_API_LINKAGE INT WSAAPI WSAStringToAddressA(LPSTR string, INT family, LPWSAPROTOCOL_INFOA info,
                                                   LPSOCKADDR address, LPINT length);
WINSOCK_API_LINKAGE INT WSAAPI WSAStringToAddressW(LPWSTR string, INT family, LPWSAPROTOCOL_INFOW info,
                                                   LPSOCKADDR address, LPINT length);
WINSOCK_API_LINKAGE INT WSAAPI WSAAddressToStringA(LPSOCKADDR address, DWORD length, LPWSAPROTOCOL_INFOA info,
                                                   LPSTR string, LPDWORD string_length);
WINSOCK_API_LINKAGE INT WSAAPI WSAAddressToStringW(LPSOCKADDR address, DWORD length, LPWSAPROTOCOL_INFOW info,
                                                   LPWSTR string, LPDWORD string_length);

WINSOCK_API_LINKAGE struct hostent *WSAAPI gethostbyname(const char *name);
WINSOCK_API_LINKAGE struct hostent *WSAAPI gethostbyaddr(const char *addr, int len, int type);
WINSOCK_API_LINKAGE int WSAAPI gethostname(char *name, int namelen);
WINSOCK_API_LINKAGE int WSAAPI GetHostNameW(PWSTR name, int namelen);
WINSOCK_API_LINKAGE struct servent *WSAAPI getservbyname(const char *name, const char *proto);
WINSOCK_API_LINKAGE struct servent *WSAAPI getservbyport(int port, const char *proto);
WINSOCK_API_LINKAGE struct protoent *WSAAPI getprotobyname(const char *name);
WINSOCK_API_LINKAGE struct protoent *WSAAPI getprotobynumber(int number);

WINSOCK_API_LINKAGE SOCKET WSAAPI socket(int af, int type, int protocol);
WINSOCK_API_LINKAGE SOCKET WSAAPI WSASocketA(int af, int type, int protocol, LPWSAPROTOCOL_INFOA info, GROUP g,
                                             DWORD flags);
WINSOCK_API_LINKAGE int WSAAPI WSADuplicateSocketW(SOCKET s, DWORD process_id, LPWSAPROTOCOL_INFOW info);
WINSOCK_API_LINKAGE int WSAAPI WSADuplicateSocketA(SOCKET s, DWORD process_id, LPWSAPROTOCOL_INFOA info);
WINSOCK_API_LINKAGE SOCKET WSAAPI WSASocketW(int af, int type, int protocol, LPWSAPROTOCOL_INFOW info, GROUP g,
                                             DWORD flags);
WINSOCK_API_LINKAGE int WSAAPI closesocket(SOCKET s);
WINSOCK_API_LINKAGE int WSAAPI bind(SOCKET s, const struct sockaddr *name, int namelen);
WINSOCK_API_LINKAGE int WSAAPI listen(SOCKET s, int backlog);
WINSOCK_API_LINKAGE SOCKET WSAAPI accept(SOCKET s, struct sockaddr *addr, int *addrlen);
WINSOCK_API_LINKAGE int WSAAPI connect(SOCKET s, const struct sockaddr *name, int namelen);
WINSOCK_API_LINKAGE int WSAAPI shutdown(SOCKET s, int how);
WINSOCK_API_LINKAGE int WSAAPI getsockname(SOCKET s, struct sockaddr *name, int *namelen);
WINSOCK_API_LINKAGE int WSAAPI getpeername(SOCKET s, struct sockaddr *name, int *namelen);
WINSOCK_API_LINKAGE int WSAAPI getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen);
WINSOCK_API_LINKAGE int WSAAPI setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen);
WINSOCK_API_LINKAGE int WSAAPI ioctlsocket(SOCKET s, long cmd, u_long *argp);
WINSOCK_API_LINKAGE int WSAAPI recv(SOCKET s, char *buf, int len, int flags);
WINSOCK_API_LINKAGE int WSAAPI recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from,
                                        int *fromlen);
WINSOCK_API_LINKAGE int WSAAPI send(SOCKET s, const char *buf, int len, int flags);
WINSOCK_API_LINKAGE int WSAAPI sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to,
                                      int tolen);
WINSOCK_API_LINKAGE int WSAAPI select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                                      const struct timeval *timeout);
WINSOCK_API_LINKAGE int WSAAPI WSAPoll(LPWSAPOLLFD fds, ULONG count, INT timeout);

WINSOCK_API_LINKAGE int WSAAPI WSARecv(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                                       LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);
WINSOCK_API_LINKAGE int WSAAPI WSARecvFrom(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received,
                                           LPDWORD flags, struct sockaddr *from, LPINT fromlen,
                                           LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);
WINSOCK_API_LINKAGE int WSAAPI WSASend(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags,
                                       LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);
WINSOCK_API_LINKAGE int WSAAPI WSASendTo(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags,
                                         const struct sockaddr *to, int tolen, LPWSAOVERLAPPED overlapped,
                                         LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);
WINSOCK_API_LINKAGE BOOL WSAAPI WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED overlapped, LPDWORD transferred,
                                                       BOOL wait, LPDWORD flags);
WINSOCK_API_LINKAGE int WSAAPI WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                                        LPDWORD returned, LPWSAOVERLAPPED overlapped,
                                        LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);

WINSOCK_API_LINKAGE WSAEVENT WSAAPI WSACreateEvent(void);
WINSOCK_API_LINKAGE BOOL WSAAPI WSACloseEvent(WSAEVENT event);
WINSOCK_API_LINKAGE BOOL WSAAPI WSASetEvent(WSAEVENT event);
WINSOCK_API_LINKAGE BOOL WSAAPI WSAResetEvent(WSAEVENT event);
WINSOCK_API_LINKAGE DWORD WSAAPI WSAWaitForMultipleEvents(DWORD count, const WSAEVENT *events, BOOL all,
                                                          DWORD timeout, BOOL alertable);
WINSOCK_API_LINKAGE int WSAAPI WSAEventSelect(SOCKET s, WSAEVENT event, long events);
WINSOCK_API_LINKAGE int WSAAPI WSAAsyncSelect(SOCKET s, HWND hwnd, u_int msg, long events);

/* the asynchronous database calls of winsock 1.1, answered with a window message. */
#define MAXGETHOSTSTRUCT 1024
WINSOCK_API_LINKAGE HANDLE WSAAPI WSAAsyncGetHostByName(HWND hwnd, u_int msg, const char *name, char *buffer, int length);
WINSOCK_API_LINKAGE HANDLE WSAAPI WSAAsyncGetHostByAddr(HWND hwnd, u_int msg, const char *addr, int len, int type,
                                                        char *buffer, int length);
WINSOCK_API_LINKAGE HANDLE WSAAPI WSAAsyncGetProtoByName(HWND hwnd, u_int msg, const char *name, char *buffer,
                                                         int length);
WINSOCK_API_LINKAGE HANDLE WSAAPI WSAAsyncGetProtoByNumber(HWND hwnd, u_int msg, int number, char *buffer, int length);
WINSOCK_API_LINKAGE HANDLE WSAAPI WSAAsyncGetServByName(HWND hwnd, u_int msg, const char *name, const char *proto,
                                                        char *buffer, int length);
WINSOCK_API_LINKAGE HANDLE WSAAPI WSAAsyncGetServByPort(HWND hwnd, u_int msg, int port, const char *proto, char *buffer,
                                                        int length);
WINSOCK_API_LINKAGE int WSAAPI WSACancelAsyncRequest(HANDLE task);
WINSOCK_API_LINKAGE int WSAAPI WSAEnumNetworkEvents(SOCKET s, WSAEVENT event, LPWSANETWORKEVENTS events);

#endif
