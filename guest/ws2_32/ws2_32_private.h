/*
 * ws2_32 internal interface shared by its source files.
 */
#ifndef MIME_WS2_32_PRIVATE_H
#define MIME_WS2_32_PRIVATE_H

#define MIME_BUILD_WS2_32
#include <mime/win32/ws2tcpip.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>

#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_CONNECTION_RESET ((NTSTATUS)0xC000020DL)
#define STATUS_CONNECTION_ABORTED ((NTSTATUS)0xC0000241L)

static inline void *WsAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static inline void *WsAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static inline void WsFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* stores the error and returns SOCKET_ERROR, the usual failure of a socket call. */
static inline int WsFail(int error)
{
    SetLastError(error);
    return SOCKET_ERROR;
}

/* ------------------------------------------------------------------ main.c */

/* 0 when WSAStartup is in effect, WSANOTINITIALISED otherwise. */
int WsCheckStarted(void);

/* per thread storage for the functions returning static buffers. */
#define WS_HOST_ADDRESSES 8
#define WS_ALIASES 8

typedef struct _WS_THREAD {
    char ntoa[16];
    struct hostent host;
    char *host_aliases[1];
    char *host_list[WS_HOST_ADDRESSES + 1];
    BYTE host_addresses[WS_HOST_ADDRESSES][16];
    char host_name[256];
    struct servent serv;
    char *serv_aliases[WS_ALIASES + 1];
    char serv_names[WS_ALIASES + 1][32];
    char serv_proto[8];
    struct protoent proto;
    char *proto_aliases[WS_ALIASES + 1];
    char proto_names[WS_ALIASES + 1][32];
} WS_THREAD;

WS_THREAD *WsThread(void);

/* called by WSACleanup when the last reference goes. */
void WsCloseAllSockets(void);
void WsSocketsInitialize(void);

/* ------------------------------------------------------------------ addr.c */

/* the ipv4 parser of RtlIpv4StringToAddressA: four decimal parts when strict, the inet_aton forms otherwise. */
BOOL WsParseIpv4(const char *s, BOOL strict, IN_ADDR *addr, const char **end);
/* RtlIpv6StringToAddressExA: an optional [ ] with a %scope and a :port after the bracket. */
BOOL WsParseIpv6Ex(const char *s, IN6_ADDR *addr, ULONG *scope, USHORT *port);
/* the plain ipv6 text form of inet_pton, without scope, brackets or port. */
BOOL WsParseIpv6(const char *s, IN6_ADDR *addr);
/* formats into buffers of at least INET_ADDRSTRLEN and INET6_ADDRSTRLEN bytes, returns the length. */
int WsFormatIpv4(const IN_ADDR *addr, char *buffer);
int WsFormatIpv6(const IN6_ADDR *addr, char *buffer);
/* WSAAddressToString text of a sockaddr, or 0 when the address is invalid. */
int WsFormatSockaddr(const SOCKADDR *addr, DWORD length, char *buffer, DWORD size);

/* narrows an ascii utf-16 string; FALSE when it does not fit or holds other characters. */
BOOL WsNarrow(PCWSTR s, char *buffer, SIZE_T size);
/* widens ascii into a caller buffer; FALSE when it does not fit. */
BOOL WsWiden(const char *s, PWSTR buffer, SIZE_T count);

/* ------------------------------------------------------------------ resolve.c */

/* the host name programs see, from the machine identity. */
void WsHostName(char *buffer, SIZE_T size);
/* the fully qualified name: the host name and the dns domain when there is one. */
void WsFullHostName(char *buffer, SIZE_T size);
/* TRUE for localhost and the names of this machine. */
BOOL WsIsLocalName(const char *name);
/* TRUE for 127.0.0.0/8, ::1 and the v4 mapped loopback. */
BOOL WsIsLoopbackV4(const IN_ADDR *addr);
BOOL WsIsLoopbackV6(const IN6_ADDR *addr);

/* the service and protocol tables, used by getaddrinfo and getnameinfo too. */
BOOL WsServiceByName(const char *name, const char *proto, USHORT *port, const char **found_proto);
const char *WsServiceByPort(USHORT port, const char *proto);

/* ------------------------------------------------------------------ socket.c */

/* TRUE when s names an open socket. */
BOOL WsIsSocket(SOCKET s);

/* fills a WSAPROTOCOL_INFOW for the given family and type, FALSE when there is no such provider. */
BOOL WsProtocolInfo(int family, int type, WSAPROTOCOL_INFOW *info);

#endif
