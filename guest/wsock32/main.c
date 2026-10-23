/*
 * Berkeley socket forwarding to ws2_32, provider extensions for AcceptEx and TransmitFile, Winsock 1.1 protocol and
 * service queries, and legacy Unix entry points.
 */
#include <mime/win32/ws2tcpip.h>
#include <mime/win32/winsock2.h>

typedef GUID *LPGUID;

#define MAX_PROTOCOLS 16

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}

WINSOCK_API_LINKAGE int WSAAPI WSAEnumProtocolsW(LPINT protocols, LPWSAPROTOCOL_INFOW buffer, LPDWORD length);

/* ------------------------------------------------------------------ extension functions */

static const GUID guid_accept_ex = {0xB5367DF1, 0xCBAC, 0x11CF, {0x95, 0xCA, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};
static const GUID guid_get_accept_ex_sockaddrs = {0xB5367DF2, 0xCBAC, 0x11CF,
                                                  {0x95, 0xCA, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};
static const GUID guid_transmit_file = {0xB5367DF0, 0xCBAC, 0x11CF, {0x95, 0xCA, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};

typedef BOOL(WSAAPI *ACCEPT_EX)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
typedef void(WSAAPI *GET_ACCEPT_EX_SOCKADDRS)(PVOID, DWORD, DWORD, DWORD, struct sockaddr **, LPINT, struct sockaddr **,
                                              LPINT);
typedef BOOL(WSAAPI *TRANSMIT_FILE)(SOCKET, HANDLE, DWORD, DWORD, LPOVERLAPPED, PVOID, DWORD);

static BOOL extension(SOCKET s, const GUID *id, PVOID *function)
{
    DWORD returned;
    *function = nullptr;
    return WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, (PVOID)id, sizeof(GUID), function, sizeof(PVOID), &returned,
                    nullptr, nullptr) != SOCKET_ERROR;
}

static GET_ACCEPT_EX_SOCKADDRS g_sockaddrs;

BOOL WSAAPI AcceptEx(SOCKET listener, SOCKET accepted, PVOID buffer, DWORD receive_length, DWORD local_length,
                     DWORD remote_length, LPDWORD received, LPOVERLAPPED overlapped)
{
    PVOID accept, sockaddrs;
    if (!extension(listener, &guid_accept_ex, &accept) || !extension(listener, &guid_get_accept_ex_sockaddrs, &sockaddrs))
        return FALSE;
    /* GetAcceptExSockaddrs has no socket to ask, so it uses the function that AcceptEx found. */
    g_sockaddrs = sockaddrs;
    return ((ACCEPT_EX)accept)(listener, accepted, buffer, receive_length, local_length, remote_length, received,
                               overlapped);
}

void WSAAPI GetAcceptExSockaddrs(PVOID buffer, DWORD receive_length, DWORD local_length, DWORD remote_length,
                                 struct sockaddr **local, LPINT local_size, struct sockaddr **remote, LPINT remote_size)
{
    if (g_sockaddrs) {
        g_sockaddrs(buffer, receive_length, local_length, remote_length, local, local_size, remote, remote_size);
        return;
    }
    if (local)
        *local = nullptr;
    if (local_size)
        *local_size = 0;
    if (remote)
        *remote = nullptr;
    if (remote_size)
        *remote_size = 0;
}

BOOL WSAAPI TransmitFile(SOCKET s, HANDLE file, DWORD count, DWORD per_send, LPOVERLAPPED overlapped, PVOID buffers,
                         DWORD flags)
{
    PVOID transmit;
    if (!extension(s, &guid_transmit_file, &transmit))
        return FALSE;
    return ((TRANSMIT_FILE)transmit)(s, file, count, per_send, overlapped, buffers, flags);
}

/* reports a truncated receive with MSG_PARTIAL. */
int WSAAPI WSARecvEx(SOCKET s, char *buffer, int length, int *flags)
{
    WSABUF wsabuf = {(ULONG)length, buffer};
    DWORD received = 0;
    *flags = 0;
    int r = WSARecv(s, &wsabuf, 1, &received, (LPDWORD)flags, nullptr, nullptr);
    if (!r)
        return (int)received;
    if (GetLastError() == WSAEMSGSIZE) {
        *flags |= MSG_PARTIAL;
        return (int)received;
    }
    return r;
}

/* ------------------------------------------------------------------ the unix remnants */

/* the remote shell and execution calls of Berkeley sockets are not offered. */
SOCKET WSAAPI rcmd(char **host, USHORT port, char *local_user, char *remote_user, char *command, int *error_socket)
{
    WSASetLastError(WSAVERNOTSUPPORTED);
    return (SOCKET)-1;
}

SOCKET WSAAPI rexec(char **host, int port, char *user, char *password, char *command, int *error_socket)
{
    WSASetLastError(WSAVERNOTSUPPORTED);
    return (SOCKET)-1;
}

SOCKET WSAAPI rresvport(int *port)
{
    WSASetLastError(WSAVERNOTSUPPORTED);
    return (SOCKET)-1;
}

int WSAAPI sethostname(char *name, int length)
{
    WSASetLastError(WSAVERNOTSUPPORTED);
    return -1;
}

struct netent;
struct netent *WSAAPI getnetbyname(char *name)
{
    WSASetLastError(WSAVERNOTSUPPORTED);
    return nullptr;
}

/* inet_network gives nothing, whatever the text is. */
ULONG WSAAPI inet_network(const char *text)
{
    return 0;
}

/* writes the caller's prefix and error text to standard error. */
void WSAAPI s_perror(char *prefix, int error)
{
    char message[512];
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)error, 0,
                             message, sizeof(message), nullptr);
    while (n && (message[n - 1] == '\r' || message[n - 1] == '\n' || message[n - 1] == ' '))
        n--;
    message[n] = 0;
    char line[640] = "> ";
    if (prefix)
        lstrcatA(line, prefix);
    lstrcatA(line, ":");
    lstrcatA(line, message);
    lstrcatA(line, "\n");
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, (DWORD)lstrlenA(line), &written, nullptr);
}

/*
 * expands DNS labels, following compression pointers within the message and escaping dots inside labels. returns the
 * encoded length, or -1 if the name exceeds the message or output buffer.
 */
int WSAAPI dn_expand(const unsigned char *message, const unsigned char *end, const unsigned char *source, char *name,
                     int size)
{
    const unsigned char *p = source;
    char *out = name, *limit = name + size;
    int consumed = -1;
    long followed = 0;
    for (;;) {
        unsigned c = *p++;
        if (!c)
            break;
        if (c & 0xC0) {
            if ((c & 0xC0) != 0xC0)
                return -1;
            if (consumed < 0)
                consumed = (int)(p - source) + 1;
            if (p >= end)
                return -1;
            p = message + (((c & 0x3F) << 8) | *p);
            if (p < message || p >= end)
                return -1;
            followed += 2;
            if (followed >= end - message)
                return -1;
            continue;
        }
        if (out != name) {
            if (out >= limit)
                return -1;
            *out++ = '.';
        }
        if (out + c >= limit)
            return -1;
        followed += c + 1;
        while (c--) {
            unsigned char b = *p++;
            if (b == '.') {
                if (out + c + 2 >= limit)
                    return -1;
                *out++ = '\\';
            }
            *out++ = (char)b;
            if (p >= end)
                return -1;
        }
    }
    *out = 0;
    return consumed < 0 ? (int)(p - source) : consumed;
}

/* ------------------------------------------------------------------ protocols */

typedef struct {
    DWORD dwServiceFlags;
    INT iAddressFamily, iMaxSockAddr, iMinSockAddr, iSocketType, iProtocol;
    DWORD dwMessageSize;
    LPWSTR lpProtocol;
} PROTOCOL_INFOW;

/*
 * lists selected IPv4 transports from the Winsock catalog with names after the records. WSAENOBUFS reports required
 * bytes in length. the first twelve service flag bits match this interface.
 */
INT WSAAPI EnumProtocolsW(LPINT protocols, LPVOID buffer, LPDWORD length)
{
    WSAPROTOCOL_INFOW catalog[MAX_PROTOCOLS * 4];
    DWORD size = sizeof(catalog);
    int count = WSAEnumProtocolsW(nullptr, catalog, &size);
    if (count == SOCKET_ERROR)
        return -1;
    int chosen[MAX_PROTOCOLS], found = 0;
    SIZE_T need = 0;
    for (int i = 0; i < count && found < MAX_PROTOCOLS; i++) {
        if (catalog[i].iAddressFamily != AF_INET)
            continue;
        if (protocols) {
            BOOL listed = FALSE;
            for (int *p = protocols; *p; p++)
                listed |= *p == catalog[i].iProtocol;
            if (!listed)
                continue;
        }
        chosen[found++] = i;
        need += sizeof(PROTOCOL_INFOW) + (lstrlenW(catalog[i].szProtocol) + 1) * sizeof(WCHAR);
    }
    if (*length < need) {
        *length = (DWORD)need;
        SetLastError(WSAENOBUFS);
        return -1;
    }
    PROTOCOL_INFOW *out = buffer;
    WCHAR *text = (WCHAR *)(out + found);
    for (int i = 0; i < found; i++) {
        const WSAPROTOCOL_INFOW *c = &catalog[chosen[i]];
        out[i].dwServiceFlags = c->dwServiceFlags1 & 0xFFF;
        out[i].iAddressFamily = c->iAddressFamily;
        out[i].iMaxSockAddr = c->iMaxSockAddr;
        out[i].iMinSockAddr = c->iMinSockAddr;
        out[i].iSocketType = c->iSocketType;
        out[i].iProtocol = c->iProtocol;
        out[i].dwMessageSize = c->dwMessageSize;
        out[i].lpProtocol = text;
        lstrcpyW(text, c->szProtocol);
        text += lstrlenW(c->szProtocol) + 1;
    }
    return found;
}

INT WSAAPI EnumProtocolsA(LPINT protocols, LPVOID buffer, LPDWORD length)
{
    /* the wide records are made in the buffer and the names narrowed in place, which shrinks them. */
    INT n = EnumProtocolsW(protocols, buffer, length);
    PROTOCOL_INFOW *records = buffer;
    for (INT i = 0; i < n; i++) {
        WCHAR *wide = records[i].lpProtocol;
        char *narrow = (char *)wide;
        int k = 0;
        for (; wide[k]; k++)
            narrow[k] = (char)wide[k];
        narrow[k] = 0;
    }
    return n;
}

/* ------------------------------------------------------------------ service types */

static BOOL port_type(const GUID *type, USHORT *port, int *protocol)
{
    ULONG kind = type->Data1 & 0xFFFF0000;
    static const BYTE tail[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0x46};
    if ((kind != 0x90000 && kind != 0xA0000) || type->Data2 || type->Data3 || memcmp(type->Data4, tail, 8))
        return FALSE;
    *port = (USHORT)type->Data1;
    *protocol = kind == 0x90000 ? IPPROTO_TCP : IPPROTO_UDP;
    return TRUE;
}

/*
 * service types encode TCP or UDP as 0x90000 or 0xA0000 plus the port in the low word and the fixed Winsock GUID
 * tail. registry types and well-known GUID tables are not consulted.
 */
INT WSAAPI GetTypeByNameA(LPSTR name, LPGUID type)
{
    static const BYTE tail[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0x46};
    struct servent *service = getservbyname(name, "tcp");
    ULONG kind = 0x90000;
    if (!service) {
        service = getservbyname(name, "udp");
        kind = 0xA0000;
    }
    if (!service) {
        SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
        return -1;
    }
    type->Data1 = kind | ntohs((u_short)service->s_port);
    type->Data2 = 0;
    type->Data3 = 0;
    memcpy(type->Data4, tail, 8);
    return 0;
}

INT WSAAPI GetTypeByNameW(LPWSTR name, LPGUID type)
{
    char narrow[256];
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr)) {
        SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
        return -1;
    }
    return GetTypeByNameA(narrow, type);
}

INT WSAAPI GetNameByTypeA(LPGUID type, LPSTR name, DWORD length)
{
    USHORT port;
    int protocol;
    struct servent *service = nullptr;
    if (port_type(type, &port, &protocol))
        service = getservbyport(htons(port), protocol == IPPROTO_TCP ? "tcp" : "udp");
    if (!service) {
        SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
        return -1;
    }
    if (lstrlenA(service->s_name) + 1 > (int)length) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    }
    lstrcpyA(name, service->s_name);
    return 0;
}

INT WSAAPI GetNameByTypeW(LPGUID type, LPWSTR name, DWORD length)
{
    char narrow[256];
    INT r = GetNameByTypeA(type, narrow, sizeof(narrow));
    if (r)
        return r;
    if ((int)length / (int)sizeof(WCHAR) < lstrlenA(narrow) + 1) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    }
    MultiByteToWideChar(CP_ACP, 0, narrow, -1, name, (int)length / (int)sizeof(WCHAR));
    return 0;
}

/* ------------------------------------------------------------------ name spaces */

#define NS_ALL 0
#define NS_TCPIP_LOCAL 10
#define NS_TCPIP_HOSTS 11
#define NS_DNS 12

typedef struct {
    SOCKET_ADDRESS LocalAddr;
    SOCKET_ADDRESS RemoteAddr;
    INT iSocketType;
    INT iProtocol;
} CSADDR_INFO;

/*
 * returns IPv4 socket addresses for each requested protocol, defaulting to TCP and UDP. port-based service types
 * supply the port. aliases are not returned.
 */
INT WSAAPI GetAddressByNameW(DWORD name_space, LPGUID type, LPWSTR name, LPINT protocols, DWORD resolution,
                             PVOID async_info, PVOID buffer, LPDWORD length, LPWSTR aliases, LPDWORD alias_length)
{
    if (async_info) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return -1;
    }
    if (name_space != NS_ALL && name_space != NS_DNS && name_space != NS_TCPIP_HOSTS && name_space != NS_TCPIP_LOCAL) {
        SetLastError(ERROR_SERVICE_NOT_FOUND);
        return -1;
    }
    if (!name || !length) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    USHORT port = 0;
    int type_protocol;
    if (type)
        port_type(type, &port, &type_protocol);
    static const int defaults[] = {IPPROTO_TCP, IPPROTO_UDP, 0};
    const int *wanted = protocols ? protocols : defaults;

    char narrow[256];
    ADDRINFOA hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr) ||
        getaddrinfo(narrow, nullptr, &hints, &result)) {
        SetLastError(WSASERVICE_NOT_FOUND);
        return -1;
    }
    int records = 0;
    /* getaddrinfo lists an address once for each socket type; the first of each address is enough. */
    struct in_addr seen[16];
    int seen_count = 0;
    for (ADDRINFOA *a = result; a && seen_count < 16; a = a->ai_next) {
        struct in_addr address = ((struct sockaddr_in *)a->ai_addr)->sin_addr;
        BOOL known = FALSE;
        for (int i = 0; i < seen_count; i++)
            known |= seen[i].s_addr == address.s_addr;
        if (!known)
            seen[seen_count++] = address;
    }
    for (const int *p = wanted; *p; p++)
        records += seen_count;
    SIZE_T need = (SIZE_T)records * (sizeof(CSADDR_INFO) + sizeof(struct sockaddr_in));
    if (*length < need) {
        *length = (DWORD)need;
        freeaddrinfo(result);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    }
    CSADDR_INFO *out = buffer;
    struct sockaddr_in *sockaddrs = (struct sockaddr_in *)(out + records);
    int at = 0;
    for (const int *p = wanted; *p; p++) {
        for (int i = 0; i < seen_count; i++, at++) {
            sockaddrs[at].sin_family = AF_INET;
            sockaddrs[at].sin_port = htons(port);
            sockaddrs[at].sin_addr = seen[i];
            memset(sockaddrs[at].sin_zero, 0, sizeof(sockaddrs[at].sin_zero));
            out[at].LocalAddr.lpSockaddr = nullptr;
            out[at].LocalAddr.iSockaddrLength = 0;
            out[at].RemoteAddr.lpSockaddr = (LPSOCKADDR)&sockaddrs[at];
            out[at].RemoteAddr.iSockaddrLength = sizeof(struct sockaddr_in);
            out[at].iSocketType = *p == IPPROTO_UDP ? SOCK_DGRAM : SOCK_STREAM;
            out[at].iProtocol = *p;
        }
    }
    freeaddrinfo(result);
    if (aliases && alias_length && *alias_length >= sizeof(WCHAR)) {
        aliases[0] = 0;
        *alias_length = 0;
    }
    return records;
}

INT WSAAPI GetAddressByNameA(DWORD name_space, LPGUID type, LPSTR name, LPINT protocols, DWORD resolution,
                             PVOID async_info, PVOID buffer, LPDWORD length, LPSTR aliases, LPDWORD alias_length)
{
    WCHAR wide[256];
    if (!name || !MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 256)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    WCHAR alias_wide[2] = {};
    DWORD alias_size = sizeof(alias_wide);
    INT n = GetAddressByNameW(name_space, type, wide, protocols, resolution, async_info, buffer, length,
                              aliases ? alias_wide : nullptr, aliases ? &alias_size : nullptr);
    if (aliases && alias_length && *alias_length)
        aliases[0] = 0;
    return n;
}

/* validates arguments, then reports no namespace: none here stores or registers services. */
INT WSAAPI GetServiceW(DWORD name_space, LPGUID type, LPWSTR name, DWORD properties, PVOID buffer, LPDWORD size,
                       PVOID async_info)
{
    if (async_info) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return -1;
    }
    if (!type || !size || !name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    SetLastError(ERROR_SERVICE_NOT_FOUND);
    return -1;
}

INT WSAAPI GetServiceA(DWORD name_space, LPGUID type, LPSTR name, DWORD properties, PVOID buffer, LPDWORD size,
                       PVOID async_info)
{
    return GetServiceW(name_space, type, (LPWSTR)name, properties, buffer, size, async_info);
}

INT WSAAPI SetServiceW(DWORD name_space, DWORD operation, DWORD flags, PVOID info, PVOID async_info, LPDWORD status)
{
    if (async_info) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return -1;
    }
    if (!info || !status) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    *status = 1;
    SetLastError(ERROR_SERVICE_NOT_FOUND);
    return -1;
}

INT WSAAPI SetServiceA(DWORD name_space, DWORD operation, DWORD flags, PVOID info, PVOID async_info, LPDWORD status)
{
    return SetServiceW(name_space, operation, flags, info, async_info, status);
}

/* reports only the interface version, omitting mswsock's built-in namespaces. */
INT WSAAPI NPLoadNameSpaces(LPDWORD version, PVOID routines, LPDWORD length)
{
    *version = 1;
    return 0;
}

/* there is no earlier Winsock configuration to bring over. */
INT WSAAPI MigrateWinsockConfiguration(PVOID a, PVOID b, PVOID c)
{
    return 0;
}
