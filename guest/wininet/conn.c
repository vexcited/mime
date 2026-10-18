/*
 * connections: winsock for the process, resolving and connecting with the progress notifications, the
 * buffered reads both plain and tls connections share, and the pool of kept alive connections.
 */
#include "wininet_private.h"

#define INET_BUFFER 16384
#define INET_IDLE_MAX 16
#define INET_IDLE_TIMEOUT 60000

static INIT_ONCE StartupOnce = INIT_ONCE_STATIC_INIT;
static SRWLOCK PoolLock = SRWLOCK_INIT;
static INET_CONN *Idle;
static DWORD IdleCount;

static BOOL CALLBACK Startup(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    return TRUE;
}

/* winsock for the process, started outside the loader lock by the first session. */
void InetStartup(void)
{
    InitOnceExecuteOnce(&StartupOnce, Startup, nullptr, nullptr);
}

static BOOL Infinite(DWORD timeout)
{
    return !timeout || timeout == INFINITE;
}

static DWORD SocketError(void)
{
    int error = WSAGetLastError();
    if (error == WSAETIMEDOUT)
        return ERROR_INTERNET_TIMEOUT;
    if (error == WSAECONNRESET)
        return ERROR_INTERNET_CONNECTION_RESET;
    return ERROR_INTERNET_CONNECTION_ABORTED;
}

static void SetTimeout(SOCKET s, int option, DWORD timeout)
{
    DWORD value = Infinite(timeout) ? 0 : timeout;
    setsockopt(s, SOL_SOCKET, option, (const char *)&value, sizeof(value));
}

static DWORD RawSend(INET_CONN *conn, const void *data, DWORD size, DWORD timeout)
{
    SetTimeout(conn->Socket, SO_SNDTIMEO, timeout);
    const char *p = data;
    while (size) {
        int sent = send(conn->Socket, p, size > 0x10000 ? 0x10000 : (int)size, 0);
        if (sent <= 0)
            return SocketError();
        p += sent;
        size -= (DWORD)sent;
    }
    return ERROR_SUCCESS;
}

DWORD InetConnSend(INET_CONN *conn, const void *data, DWORD size, DWORD timeout)
{
    if (conn->Tls)
        return InetTlsSend(conn, data, size, timeout);
    return RawSend(conn, data, size, timeout);
}

DWORD InetConnRecv(INET_CONN *conn, void *data, DWORD size, DWORD *got, DWORD timeout)
{
    SetTimeout(conn->Socket, SO_RCVTIMEO, timeout);
    int received = recv(conn->Socket, data, (int)size, 0);
    if (received < 0) {
        *got = 0;
        return SocketError();
    }
    *got = (DWORD)received;
    return ERROR_SUCCESS;
}

/* makes room for room more bytes at the end of the plaintext buffer. */
static BOOL Reserve(INET_CONN *conn, DWORD room)
{
    if (conn->Start == conn->End)
        conn->Start = conn->End = 0;
    if (conn->Size - conn->End >= room)
        return TRUE;
    if (conn->Start) {
        memmove(conn->Buffer, conn->Buffer + conn->Start, conn->End - conn->Start);
        conn->End -= conn->Start;
        conn->Start = 0;
        if (conn->Size - conn->End >= room)
            return TRUE;
    }
    DWORD size = conn->Size ? conn->Size : INET_BUFFER;
    while (size - conn->End < room)
        size *= 2;
    BYTE *buffer = InetRealloc(conn->Buffer, size);
    if (!buffer)
        return FALSE;
    conn->Buffer = buffer;
    conn->Size = size;
    return TRUE;
}

BOOL InetConnAppend(INET_CONN *conn, const void *data, DWORD size)
{
    if (!Reserve(conn, size))
        return FALSE;
    memcpy(conn->Buffer + conn->End, data, size);
    conn->End += size;
    return TRUE;
}

DWORD InetConnBuffered(INET_CONN *conn)
{
    return conn->End - conn->Start;
}

/* reads more plaintext; at the end of the stream Eof is set and nothing is added. */
DWORD InetConnFill(INET_CONN *conn, DWORD timeout)
{
    if (conn->Eof)
        return ERROR_SUCCESS;
    if (conn->Tls)
        return InetTlsFill(conn, timeout);
    if (!Reserve(conn, 4096))
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD got;
    DWORD error = InetConnRecv(conn, conn->Buffer + conn->End, conn->Size - conn->End, &got, timeout);
    if (error)
        return error;
    if (!got)
        conn->Eof = TRUE;
    conn->End += got;
    return ERROR_SUCCESS;
}

DWORD InetConnReadLine(INET_CONN *conn, char **line, DWORD limit, DWORD timeout, DWORD *total)
{
    DWORD scanned = 0;
    while (TRUE) {
        BYTE *start = conn->Buffer + conn->Start;
        DWORD count = InetConnBuffered(conn);
        for (DWORD i = scanned; i < count; i++)
            if (start[i] == '\n') {
                DWORD length = i && start[i - 1] == '\r' ? i - 1 : i;
                *line = InetDuplicateA((char *)start, length);
                conn->Start += i + 1;
                if (total)
                    *total += i + 1;
                return *line ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
            }
        scanned = count;
        if (count > limit)
            return ERROR_HTTP_INVALID_SERVER_RESPONSE;
        if (conn->Eof)
            return ERROR_HTTP_INVALID_SERVER_RESPONSE;
        DWORD error = InetConnFill(conn, timeout);
        if (error)
            return error;
    }
}

void InetConnClose(INET_OBJECT *notify, INET_CONN *conn)
{
    if (!conn)
        return;
    if (notify)
        InetNotify(notify, INTERNET_STATUS_CLOSING_CONNECTION, nullptr, 0);
    InetTlsClose(conn);
    closesocket(conn->Socket);
    if (notify)
        InetNotify(notify, INTERNET_STATUS_CONNECTION_CLOSED, nullptr, 0);
    InetFree(conn->Buffer);
    InetFree(conn->Raw);
    InetFree(conn);
}

/* ------------------------------------------------------------------ the pool */

static BOOL SameKey(INET_CONN *conn, LPCWSTR host, INTERNET_PORT port, BOOL secure, LPCWSTR proxy,
                    INTERNET_PORT proxy_port)
{
    return conn->Port == port && conn->Secure == secure && conn->ProxyPort == proxy_port &&
           !lstrcmpiW(conn->Host, host) && !lstrcmpiW(conn->Proxy, proxy ? proxy : L"");
}

INET_CONN *InetPoolTake(LPCWSTR host, INTERNET_PORT port, BOOL secure, LPCWSTR proxy, INTERNET_PORT proxy_port)
{
    INET_CONN *found = nullptr, *stale = nullptr;
    DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&PoolLock);
    for (INET_CONN **link = &Idle; *link;) {
        INET_CONN *conn = *link;
        if (now - conn->IdleSince > INET_IDLE_TIMEOUT) {
            *link = conn->Next;
            IdleCount--;
            conn->Next = stale;
            stale = conn;
            continue;
        }
        if (!found && SameKey(conn, host, port, secure, proxy, proxy_port)) {
            *link = conn->Next;
            IdleCount--;
            found = conn;
            continue;
        }
        link = &conn->Next;
    }
    ReleaseSRWLockExclusive(&PoolLock);
    while (stale) {
        INET_CONN *next = stale->Next;
        InetConnClose(nullptr, stale);
        stale = next;
    }
    if (found)
        found->Next = nullptr;
    return found;
}

void InetPoolPut(INET_CONN *conn)
{
    INET_CONN *drop = nullptr;
    conn->IdleSince = GetTickCount();
    AcquireSRWLockExclusive(&PoolLock);
    conn->Next = Idle;
    Idle = conn;
    if (++IdleCount > INET_IDLE_MAX) {
        INET_CONN **link = &Idle;
        while ((*link)->Next)
            link = &(*link)->Next;
        drop = *link;
        *link = nullptr;
        IdleCount--;
    }
    ReleaseSRWLockExclusive(&PoolLock);
    InetConnClose(nullptr, drop);
}

/* ------------------------------------------------------------------ connecting */

static DWORD ConnectSocket(SOCKET s, const struct sockaddr *address, int length, DWORD timeout)
{
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    if (connect(s, address, length)) {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            return ERROR_INTERNET_CANNOT_CONNECT;
        fd_set write, except;
        FD_ZERO(&write);
        FD_ZERO(&except);
        FD_SET(s, &write);
        FD_SET(s, &except);
        struct timeval limit = {(long)(timeout / 1000), (long)(timeout % 1000) * 1000};
        int ready = select(0, nullptr, &write, &except, Infinite(timeout) ? nullptr : &limit);
        if (!ready)
            return ERROR_INTERNET_TIMEOUT;
        if (ready < 0 || !FD_ISSET(s, &write))
            return ERROR_INTERNET_CANNOT_CONNECT;
        int error = 0, size = sizeof(error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&error, &size);
        if (error)
            return ERROR_INTERNET_CANNOT_CONNECT;
    }
    nonblocking = 0;
    ioctlsocket(s, FIONBIO, &nonblocking);
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
    return ERROR_SUCCESS;
}

/* an address as wininet reports it: dotted ipv4, or ipv6 in brackets. */
static void AddressText(const struct sockaddr *address, PWSTR text, DWORD capacity)
{
    text[0] = 0;
    if (address->sa_family == AF_INET) {
        InetNtopW(AF_INET, &((const struct sockaddr_in *)address)->sin_addr, text, capacity);
    } else if (address->sa_family == AF_INET6) {
        text[0] = '[';
        InetNtopW(AF_INET6, &((const struct sockaddr_in6 *)address)->sin6_addr, text + 1, capacity - 2);
        lstrcatW(text, L"]");
    }
}

DWORD InetConnOpen(INET_OBJECT *request, LPCWSTR host, INTERNET_PORT port, INET_CONN **result)
{
    WCHAR name[INTERNET_MAX_HOST_NAME_LENGTH];
    DWORD length = (DWORD)lstrlenW(host);
    if (!length || length >= ARRAYSIZE(name))
        return ERROR_INTERNET_INVALID_URL;
    memcpy(name, host, (length + 1) * sizeof(WCHAR));
    /* a bracketed ipv6 literal resolves without its brackets. */
    if (name[0] == '[' && length > 2 && name[length - 1] == ']') {
        memmove(name, name + 1, (length - 2) * sizeof(WCHAR));
        name[length - 2] = 0;
    }
    InetNotifyString(request, INTERNET_STATUS_RESOLVING_NAME, host);
    WCHAR service[8];
    wsprintfW(service, L"%u", port);
    ADDRINFOW hints = {0}, *addresses = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (GetAddrInfoW(name, service, &hints, &addresses) || !addresses)
        return ERROR_INTERNET_NAME_NOT_RESOLVED;
    WCHAR ip[64];
    AddressText(addresses->ai_addr, ip, ARRAYSIZE(ip));
    InetNotifyString(request, INTERNET_STATUS_NAME_RESOLVED, ip);
    DWORD error = ERROR_INTERNET_CANNOT_CONNECT;
    SOCKET s = INVALID_SOCKET;
    for (ADDRINFOW *a = addresses; a; a = a->ai_next) {
        AddressText(a->ai_addr, ip, ARRAYSIZE(ip));
        InetNotifyString(request, INTERNET_STATUS_CONNECTING_TO_SERVER, ip);
        s = socket(a->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            continue;
        error = ConnectSocket(s, a->ai_addr, (int)a->ai_addrlen, INET_TIMEOUT(request, INTERNET_OPTION_CONNECT_TIMEOUT));
        if (!error)
            break;
        closesocket(s);
        s = INVALID_SOCKET;
        if (error == ERROR_INTERNET_TIMEOUT)
            break;
    }
    FreeAddrInfoW(addresses);
    if (s == INVALID_SOCKET)
        return error;
    InetNotifyString(request, INTERNET_STATUS_CONNECTED_TO_SERVER, ip);
    INET_CONN *conn = InetAllocZero(sizeof(*conn));
    if (!conn) {
        closesocket(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    conn->Socket = s;
    *result = conn;
    return ERROR_SUCCESS;
}
