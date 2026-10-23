/* HTTP connections, requests, responses, redirects, decompression and asynchronous workers. */
#include "winhttp_private.h"
#include "png/inflate.h"

#define WH_IDLE_MAX 8
#define WH_BUFFER 16384

/* sockets. */

static DWORD SocketError(WH_CONN *conn)
{
    int error = WSAGetLastError();
    if (error == WSAETIMEDOUT)
        return ERROR_WINHTTP_TIMEOUT;
    return ERROR_WINHTTP_CONNECTION_ERROR;
}

static void SetTimeout(SOCKET s, int option, int timeout)
{
    DWORD value = timeout <= 0 ? 0 : (DWORD)timeout;
    setsockopt(s, SOL_SOCKET, option, (const char *)&value, sizeof(value));
}

static DWORD RawSend(WH_CONN *conn, const void *data, DWORD size, int timeout)
{
    SetTimeout(conn->Socket, SO_SNDTIMEO, timeout);
    const char *p = data;
    while (size) {
        int sent = send(conn->Socket, p, size > 0x10000 ? 0x10000 : (int)size, 0);
        if (sent <= 0)
            return SocketError(conn);
        p += sent;
        size -= (DWORD)sent;
    }
    return ERROR_SUCCESS;
}

DWORD WhConnSend(WH_CONN *conn, const void *data, DWORD size, int timeout)
{
    if (conn->Tls)
        return WhTlsSend(conn, data, size, timeout);
    return RawSend(conn, data, size, timeout);
}

DWORD WhConnRecv(WH_CONN *conn, void *data, DWORD size, DWORD *got, int timeout)
{
    SetTimeout(conn->Socket, SO_RCVTIMEO, timeout);
    int received = recv(conn->Socket, data, (int)size, 0);
    if (received < 0) {
        *got = 0;
        return SocketError(conn);
    }
    *got = (DWORD)received;
    return ERROR_SUCCESS;
}

static BOOL Reserve(WH_CONN *conn, DWORD room)
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
    DWORD size = conn->Size ? conn->Size : WH_BUFFER;
    while (size - conn->End < room)
        size *= 2;
    BYTE *buffer = WhRealloc(conn->Buffer, size);
    if (!buffer)
        return FALSE;
    conn->Buffer = buffer;
    conn->Size = size;
    return TRUE;
}

/* append plaintext; EOF sets Eof without adding data. */
static DWORD Fill(WH_CONN *conn, int timeout)
{
    if (conn->Eof)
        return ERROR_SUCCESS;
    if (conn->Tls)
        return WhTlsFill(conn, timeout);
    if (!Reserve(conn, 4096))
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD got;
    DWORD error = WhConnRecv(conn, conn->Buffer + conn->End, conn->Size - conn->End, &got, timeout);
    if (error)
        return error;
    if (!got)
        conn->Eof = TRUE;
    conn->End += got;
    return ERROR_SUCCESS;
}

/* append plaintext decrypted by TLS. */
BOOL WhConnAppend(WH_CONN *conn, const void *data, DWORD size)
{
    if (!Reserve(conn, size))
        return FALSE;
    memcpy(conn->Buffer + conn->End, data, size);
    conn->End += size;
    return TRUE;
}

static DWORD Buffered(WH_CONN *conn)
{
    return conn->End - conn->Start;
}

/* read up to limit bytes into a heap string, excluding CRLF. */
static DWORD ReadLine(WH_CONN *conn, char **line, DWORD limit, int timeout, DWORD *total)
{
    while (TRUE) {
        BYTE *start = conn->Buffer + conn->Start;
        DWORD count = Buffered(conn);
        for (DWORD i = 0; i < count; i++)
            if (start[i] == '\n') {
                DWORD length = i && start[i - 1] == '\r' ? i - 1 : i;
                *line = WhDupAn((char *)start, length);
                conn->Start += i + 1;
                if (total)
                    *total += i + 1;
                return *line ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
            }
        if (count > limit)
            return ERROR_WINHTTP_HEADER_SIZE_OVERFLOW;
        if (conn->Eof)
            return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
        DWORD error = Fill(conn, timeout);
        if (error)
            return error;
    }
}

void WhConnClose(WH_OBJECT *notify, WH_CONN *conn)
{
    if (!conn)
        return;
    if (notify)
        WhNotify(notify, WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION, nullptr, 0);
    if (conn->Tls)
        WhTlsClose(conn);
    closesocket(conn->Socket);
    if (notify)
        WhNotify(notify, WINHTTP_CALLBACK_STATUS_CONNECTION_CLOSED, nullptr, 0);
    WhFree(conn->Buffer);
    WhFree(conn->Raw);
    WhFree(conn);
}

/* the pool. */

static WH_CONN *TakeIdle(WH_OBJECT *session, LPCWSTR host, INTERNET_PORT port, BOOL secure, BOOL proxy)
{
    EnterCriticalSection(&session->Lock);
    WH_CONN **link = &session->Idle, *conn = nullptr;
    while (*link) {
        WH_CONN *candidate = *link;
        if (candidate->Port == port && candidate->Secure == secure && candidate->ViaProxy == proxy &&
            !_wcsicmp(candidate->Host, host)) {
            *link = candidate->Next;
            session->IdleCount--;
            conn = candidate;
            break;
        }
        link = &candidate->Next;
    }
    LeaveCriticalSection(&session->Lock);
    if (conn)
        conn->Next = nullptr;
    return conn;
}

static void PutIdle(WH_OBJECT *session, WH_CONN *conn)
{
    WH_CONN *drop = nullptr;
    EnterCriticalSection(&session->Lock);
    conn->Next = session->Idle;
    session->Idle = conn;
    if (++session->IdleCount > WH_IDLE_MAX) {
        WH_CONN **link = &session->Idle;
        while ((*link)->Next)
            link = &(*link)->Next;
        drop = *link;
        *link = nullptr;
        session->IdleCount--;
    }
    LeaveCriticalSection(&session->Lock);
    WhConnClose(nullptr, drop);
}

void WhSessionCleanup(WH_OBJECT *session)
{
    while (session->Idle) {
        WH_CONN *conn = session->Idle;
        session->Idle = conn->Next;
        WhConnClose(nullptr, conn);
    }
}

/* pool connections after a clean response end; close others. */
static void FinishConnection(WH_OBJECT *request)
{
    EnterCriticalSection(&request->Lock);
    WH_CONN *conn = request->Conn;
    request->Conn = nullptr;
    LeaveCriticalSection(&request->Lock);
    if (!conn)
        return;
    if (request->KeepAlive && !conn->Eof && !Buffered(conn) && !request->Closed &&
        !(request->DisableFeatures & WINHTTP_DISABLE_KEEP_ALIVE))
        PutIdle(WhSession(request), conn);
    else
        WhConnClose(request, conn);
}

void WhDropConnection(WH_OBJECT *request)
{
    WH_CONN *conn = request->Conn;
    request->Conn = nullptr;
    WhConnClose(nullptr, conn);
}

/* connecting. */

static DWORD ConnectSocket(SOCKET s, const struct sockaddr *address, int length, int timeout)
{
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    if (!connect(s, address, length))
        goto connected;
    if (WSAGetLastError() != WSAEWOULDBLOCK)
        return ERROR_WINHTTP_CANNOT_CONNECT;
    fd_set write, except;
    FD_ZERO(&write);
    FD_ZERO(&except);
    FD_SET(s, &write);
    FD_SET(s, &except);
    struct timeval limit = {timeout / 1000, (timeout % 1000) * 1000};
    int ready = select(0, nullptr, &write, &except, timeout > 0 ? &limit : nullptr);
    if (!ready)
        return ERROR_WINHTTP_TIMEOUT;
    if (ready < 0 || !FD_ISSET(s, &write))
        return ERROR_WINHTTP_CANNOT_CONNECT;
    int error = 0, size = sizeof(error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&error, &size);
    if (error)
        return ERROR_WINHTTP_CANNOT_CONNECT;
connected:
    nonblocking = 0;
    ioctlsocket(s, FIONBIO, &nonblocking);
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
    return ERROR_SUCCESS;
}

static void AddressText(const struct sockaddr *address, PWSTR text, DWORD capacity)
{
    text[0] = 0;
    if (address->sa_family == AF_INET)
        InetNtopW(AF_INET, &((const struct sockaddr_in *)address)->sin_addr, text, capacity);
    else if (address->sa_family == AF_INET6)
        InetNtopW(AF_INET6, &((const struct sockaddr_in6 *)address)->sin6_addr, text, capacity);
}

static DWORD OpenConnection(WH_OBJECT *request, LPCWSTR host, INTERNET_PORT port, WH_CONN **result)
{
    WCHAR name[INTERNET_MAX_HOST_NAME_LENGTH];
    DWORD length = (DWORD)wcslen(host);
    if (length >= ARRAYSIZE(name))
        return ERROR_WINHTTP_INVALID_URL;
    memcpy(name, host, (length + 1) * sizeof(WCHAR));
    /* strip brackets before resolving IPv6 literals. */
    if (name[0] == '[' && length > 2 && name[length - 1] == ']') {
        memmove(name, name + 1, (length - 2) * sizeof(WCHAR));
        name[length - 2] = 0;
    }
    WhNotify(request, WINHTTP_CALLBACK_STATUS_RESOLVING_NAME, (LPVOID)host, ((DWORD)wcslen(host) + 1) * sizeof(WCHAR));
    WCHAR service[8];
    wsprintfW(service, L"%u", port);
    ADDRINFOW hints = {0}, *addresses = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (GetAddrInfoW(name, service, &hints, &addresses) || !addresses)
        return ERROR_WINHTTP_NAME_NOT_RESOLVED;
    WCHAR ip[64];
    AddressText(addresses->ai_addr, ip, ARRAYSIZE(ip));
    WhNotify(request, WINHTTP_CALLBACK_STATUS_NAME_RESOLVED, ip, ((DWORD)wcslen(ip) + 1) * sizeof(WCHAR));
    DWORD error = ERROR_WINHTTP_CANNOT_CONNECT;
    SOCKET s = INVALID_SOCKET;
    for (ADDRINFOW *a = addresses; a; a = a->ai_next) {
        AddressText(a->ai_addr, ip, ARRAYSIZE(ip));
        WhNotify(request, WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER, ip, ((DWORD)wcslen(ip) + 1) * sizeof(WCHAR));
        s = socket(a->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            continue;
        error = ConnectSocket(s, a->ai_addr, (int)a->ai_addrlen, request->Timeouts.Connect);
        if (!error)
            break;
        closesocket(s);
        s = INVALID_SOCKET;
        if (error == ERROR_WINHTTP_TIMEOUT)
            break;
    }
    FreeAddrInfoW(addresses);
    if (s == INVALID_SOCKET)
        return error;
    WhNotify(request, WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER, ip, ((DWORD)wcslen(ip) + 1) * sizeof(WCHAR));
    WH_CONN *conn = WhAllocZero(sizeof(*conn));
    if (!conn) {
        closesocket(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    conn->Socket = s;
    *result = conn;
    return ERROR_SUCCESS;
}

static void SetConn(WH_OBJECT *request, WH_CONN *conn)
{
    EnterCriticalSection(&request->Lock);
    request->Conn = conn;
    BOOL closed = request->Closed;
    LeaveCriticalSection(&request->Lock);
    if (closed && conn)
        shutdown(conn->Socket, SD_BOTH);
}

/* HTTP/1.1 tunnel for an HTTPS request through a proxy. */
static DWORD Tunnel(WH_OBJECT *request, WH_CONN *conn)
{
    char *host = WhWideToAnsi(request->Host, (DWORD)wcslen(request->Host), nullptr);
    if (!host)
        return ERROR_NOT_ENOUGH_MEMORY;
    char text[600];
    wsprintfA(text, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n\r\n", host, request->HostPort, host, request->HostPort);
    WhFree(host);
    DWORD error = RawSend(conn, text, (DWORD)strlen(text), request->Timeouts.Send);
    char *line = nullptr;
    if (!error)
        error = ReadLine(conn, &line, 65536, request->Timeouts.ReceiveResponse, nullptr);
    if (error)
        return error;
    const char *code = strchr(line, ' ');
    BOOL ok = !strncmp(line, "HTTP/", 5) && code && code[1] == '2';
    WhFree(line);
    while (TRUE) {
        error = ReadLine(conn, &line, 65536, request->Timeouts.ReceiveResponse, nullptr);
        if (error)
            return error;
        BOOL end = !*line;
        WhFree(line);
        if (end)
            break;
    }
    return ok ? ERROR_SUCCESS : ERROR_WINHTTP_CANNOT_CONNECT;
}

/* reuse a pooled connection when possible; otherwise connect and negotiate TLS if secure. */
static DWORD Connect(WH_OBJECT *request, BOOL allow_reuse)
{
    WH_OBJECT *session = WhSession(request);
    WCHAR proxy[INTERNET_MAX_HOST_NAME_LENGTH];
    INTERNET_PORT proxy_port = 0;
    BOOL via_proxy = WhPickProxy(request, request->Host, request->Secure, proxy, ARRAYSIZE(proxy), &proxy_port);
    WH_CONN *conn = allow_reuse ? TakeIdle(session, request->Host, request->HostPort, request->Secure, via_proxy)
                                : nullptr;
    if (conn) {
        request->ConnReused = TRUE;
        SetConn(request, conn);
        return ERROR_SUCCESS;
    }
    request->ConnReused = FALSE;
    DWORD error = OpenConnection(request, via_proxy ? proxy : request->Host, via_proxy ? proxy_port : request->HostPort,
                                 &conn);
    if (error)
        return error;
    wcscpy(conn->Host, request->Host);
    conn->Port = request->HostPort;
    conn->Secure = request->Secure;
    conn->ViaProxy = via_proxy;
    SetConn(request, conn);
    if (request->Secure) {
        if (via_proxy && (error = Tunnel(request, conn)))
            return error;
        DWORD failure = 0;
        error = WhTlsHandshake(conn, request->Host, request->SecurityFlags, request->Timeouts.Connect, &failure);
        if (error) {
            if (failure)
                WhNotify(request, WINHTTP_CALLBACK_STATUS_SECURE_FAILURE, &failure, sizeof(failure));
            return error;
        }
    }
    return ERROR_SUCCESS;
}

/* sending. */

typedef struct {
    char *Data;
    DWORD Length;
    DWORD Capacity;
    BOOL Failed;
} WH_OUT;

static void Put(WH_OUT *out, const char *data, DWORD length)
{
    if (out->Failed)
        return;
    if (out->Length + length + 1 > out->Capacity) {
        DWORD capacity = (out->Length + length + 1) * 2;
        char *grown = WhRealloc(out->Data, capacity);
        if (!grown) {
            out->Failed = TRUE;
            return;
        }
        out->Data = grown;
        out->Capacity = capacity;
    }
    memcpy(out->Data + out->Length, data, length);
    out->Length += length;
    out->Data[out->Length] = 0;
}

static void PutString(WH_OUT *out, const char *text)
{
    Put(out, text, (DWORD)strlen(text));
}

static void PutWide(WH_OUT *out, LPCWSTR text)
{
    char *ansi = WhWideToAnsi(text, (DWORD)wcslen(text), nullptr);
    if (!ansi) {
        out->Failed = TRUE;
        return;
    }
    PutString(out, ansi);
    WhFree(ansi);
}

static void PutHeader(WH_OUT *out, const char *name, const char *value)
{
    PutString(out, name);
    Put(out, ": ", 2);
    PutString(out, value);
    Put(out, "\r\n", 2);
}

static void Base64(const BYTE *data, DWORD length, char *out)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    DWORD o = 0;
    for (DWORD i = 0; i < length; i += 3) {
        DWORD v = (DWORD)data[i] << 16 | (i + 1 < length ? (DWORD)data[i + 1] << 8 : 0) |
                  (i + 2 < length ? data[i + 2] : 0);
        out[o++] = table[v >> 18 & 63];
        out[o++] = table[v >> 12 & 63];
        out[o++] = i + 1 < length ? table[v >> 6 & 63] : '=';
        out[o++] = i + 2 < length ? table[v & 63] : '=';
    }
    out[o] = 0;
}

static void PutBasic(WH_OUT *out, const char *name, LPCWSTR user, LPCWSTR password)
{
    SIZE_T length = wcslen(user) + 1 + (password ? wcslen(password) : 0);
    PWSTR joined = WhAlloc((length + 1) * sizeof(WCHAR));
    if (!joined) {
        out->Failed = TRUE;
        return;
    }
    wsprintfW(joined, L"%s:%s", user, password ? password : L"");
    int bytes = WideCharToMultiByte(CP_UTF8, 0, joined, -1, nullptr, 0, nullptr, nullptr);
    char *utf8 = WhAlloc((SIZE_T)bytes);
    char *encoded = WhAlloc((SIZE_T)bytes * 2 + 16);
    if (utf8 && encoded) {
        WideCharToMultiByte(CP_UTF8, 0, joined, -1, utf8, bytes, nullptr, nullptr);
        memcpy(encoded, "Basic ", 6);
        Base64((BYTE *)utf8, (DWORD)bytes - 1, encoded + 6);
        PutHeader(out, name, encoded);
    } else {
        out->Failed = TRUE;
    }
    WhFree(utf8);
    WhFree(encoded);
    WhFree(joined);
}

static BOOL VerbIs(WH_OBJECT *request, LPCWSTR verb)
{
    return !_wcsicmp(request->Verb, verb);
}

static DWORD BuildRequest(WH_OBJECT *request, BOOL via_proxy, WH_OUT *out)
{
    WH_HEADERS *headers = &request->Request;
    PutWide(out, request->Verb);
    Put(out, " ", 1);
    if (via_proxy && !request->Secure) {
        PutString(out, "http://");
        PutWide(out, request->Host);
        if (request->HostPort != 80) {
            char port[8];
            wsprintfA(port, ":%u", request->HostPort);
            PutString(out, port);
        }
    }
    PutWide(out, request->Path);
    char version[24];
    wsprintfA(version, " HTTP/%u.%u\r\n", request->Major, request->Minor);
    PutString(out, version);
    if (!WhHeadersFind(headers, "Connection", 0))
        PutHeader(out, "Connection",
                  request->UpgradeRequested                                 ? "Upgrade"
                  : request->DisableFeatures & WINHTTP_DISABLE_KEEP_ALIVE ? "Close"
                                                                           : "Keep-Alive");
    for (DWORD i = 0; i < headers->Count; i++)
        PutHeader(out, headers->Items[i].Name, headers->Items[i].Value);
    if (request->Agent && *request->Agent && !WhHeadersFind(headers, "User-Agent", 0)) {
        char *agent = WhWideToAnsi(request->Agent, (DWORD)wcslen(request->Agent), nullptr);
        if (agent)
            PutHeader(out, "User-Agent", agent);
        WhFree(agent);
    }
    if (!WhHeadersFind(headers, "Content-Length", 0) && !WhHeadersFind(headers, "Transfer-Encoding", 0)) {
        if (request->ChunkedUpload)
            PutHeader(out, "Transfer-Encoding", "chunked");
        else if (request->TotalLength || VerbIs(request, L"POST") || VerbIs(request, L"PUT")) {
            char length[24], *p = length + sizeof(length) - 1;
            ULONGLONG value = request->TotalLength;
            *p = 0;
            do
                *--p = (char)('0' + value % 10);
            while (value /= 10);
            PutHeader(out, "Content-Length", p);
        }
    }
    if (!WhHeadersFind(headers, "Host", 0)) {
        PutString(out, "Host: ");
        PutWide(out, request->Host);
        if (request->HostPort != 80 && request->HostPort != 443) {
            char port[8];
            wsprintfA(port, ":%u", request->HostPort);
            PutString(out, port);
        }
        Put(out, "\r\n", 2);
    }
    if (request->Decompression && !WhHeadersFind(headers, "Accept-Encoding", 0))
        PutHeader(out, "Accept-Encoding",
                  (request->Decompression & WINHTTP_DECOMPRESSION_FLAG_ALL) == WINHTTP_DECOMPRESSION_FLAG_ALL
                      ? "gzip, deflate"
                  : request->Decompression & WINHTTP_DECOMPRESSION_FLAG_GZIP ? "gzip"
                                                                             : "deflate");
    if (request->User && request->AuthScheme == WINHTTP_AUTH_SCHEME_BASIC &&
        !WhHeadersFind(headers, "Authorization", 0))
        PutBasic(out, "Authorization", request->User, request->Password);
    if (via_proxy && !request->Secure && request->ProxyUser &&
        request->ProxyAuthScheme == WINHTTP_AUTH_SCHEME_BASIC && !WhHeadersFind(headers, "Proxy-Authorization", 0))
        PutBasic(out, "Proxy-Authorization", request->ProxyUser, request->ProxyPassword);
    if (request->UpgradeRequested) {
        if (!WhHeadersFind(headers, "Upgrade", 0))
            PutHeader(out, "Upgrade", "websocket");
        if (!WhHeadersFind(headers, "Sec-WebSocket-Version", 0))
            PutHeader(out, "Sec-WebSocket-Version", "13");
        if (!WhHeadersFind(headers, "Sec-WebSocket-Key", 0)) {
            BYTE key[16];
            char text[32];
            ULONG seed = GetTickCount() ^ (ULONG)(ULONG_PTR)request;
            for (DWORD i = 0; i < 16; i++)
                key[i] = (BYTE)(RtlRandomEx(&seed) >> 8);
            Base64(key, 16, text);
            PutHeader(out, "Sec-WebSocket-Key", text);
        }
    }
    Put(out, "\r\n", 2);
    return out->Failed ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
}

static DWORD Cancelled(WH_OBJECT *request, DWORD error)
{
    return request->Closed ? ERROR_WINHTTP_OPERATION_CANCELLED : error;
}

/* send headers and optional data, retrying once if the server closed a pooled connection. */
static DWORD Transmit(WH_OBJECT *request)
{
    for (DWORD attempt = 0; attempt < 2; attempt++) {
        WhDropConnection(request);
        DWORD error = Connect(request, attempt == 0);
        if (error)
            return Cancelled(request, error);
        WH_OUT out = {0};
        error = BuildRequest(request, request->Conn->ViaProxy, &out);
        if (!error && request->OptionalLength)
            Put(&out, (char *)request->Optional, request->OptionalLength);
        if (!error && out.Failed)
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (error) {
            WhFree(out.Data);
            return error;
        }
        WhNotify(request, WINHTTP_CALLBACK_STATUS_SENDING_REQUEST, nullptr, 0);
        error = WhConnSend(request->Conn, out.Data, out.Length, request->Timeouts.Send);
        DWORD sent = out.Length;
        WhFree(out.Data);
        if (error && request->ConnReused && !request->Closed)
            continue;
        if (error)
            return Cancelled(request, error);
        WhNotify(request, WINHTTP_CALLBACK_STATUS_REQUEST_SENT, &sent, sizeof(sent));
        request->Written = request->OptionalLength;
        return ERROR_SUCCESS;
    }
    return ERROR_WINHTTP_CONNECTION_ERROR;
}

static void ResetResponse(WH_OBJECT *request)
{
    WhHeadersClear(&request->Response);
    WhFree(request->StatusText);
    WhFree(request->RawStatusLine);
    WhFree(request->Memory);
    request->StatusText = request->RawStatusLine = nullptr;
    request->Memory = nullptr;
    request->MemorySize = request->MemoryOffset = 0;
    request->StatusCode = 0;
    request->BodyMode = WH_BODY_NONE;
    request->BodyLeft = 0;
    request->ChunkEnd = FALSE;
    request->BodyDone = FALSE;
    request->KeepAlive = FALSE;
}

DWORD WhSendRequest(WH_OBJECT *request)
{
    ResetResponse(request);
    request->Redirects = 0;
    return Transmit(request);
}

DWORD WhWrite(WH_OBJECT *request, const void *buffer, DWORD size, DWORD *written)
{
    *written = 0;
    if (request->State != 1 || !request->Conn)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    DWORD error;
    if (request->ChunkedUpload) {
        if (!size)
            return ERROR_SUCCESS;
        char head[16];
        wsprintfA(head, "%X\r\n", size);
        error = WhConnSend(request->Conn, head, (DWORD)strlen(head), request->Timeouts.Send);
        if (!error)
            error = WhConnSend(request->Conn, buffer, size, request->Timeouts.Send);
        if (!error)
            error = WhConnSend(request->Conn, "\r\n", 2, request->Timeouts.Send);
    } else {
        error = WhConnSend(request->Conn, buffer, size, request->Timeouts.Send);
    }
    if (error)
        return Cancelled(request, error);
    request->Written += size;
    *written = size;
    return ERROR_SUCCESS;
}

/* responses. */

static BOOL ValueHas(const char *value, const char *token)
{
    SIZE_T length = strlen(token);
    for (const char *p = value; *p;) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if ((SIZE_T)(end - start) == length && !_strnicmp(start, token, length))
            return TRUE;
    }
    return FALSE;
}

static DWORD ReadHead(WH_OBJECT *request)
{
    WH_CONN *conn = request->Conn;
    char *line = nullptr;
    DWORD total = 0, limit = request->MaxResponseHeaderSize;
    DWORD error = ReadLine(conn, &line, limit, request->Timeouts.ReceiveResponse, &total);
    if (error == ERROR_WINHTTP_INVALID_SERVER_RESPONSE && !total)
        return ERROR_WINHTTP_CONNECTION_ERROR;
    if (error)
        return error;
    DWORD major = 0, minor = 0, code = 0;
    const char *p = line;
    if (_strnicmp(p, "HTTP/", 5)) {
        WhFree(line);
        return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    }
    p += 5;
    while (*p >= '0' && *p <= '9')
        major = major * 10 + (DWORD)(*p++ - '0');
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++)
            minor = minor * 10 + (DWORD)(*p - '0');
    while (*p == ' ')
        p++;
    const char *digits = p;
    while (*p >= '0' && *p <= '9')
        code = code * 10 + (DWORD)(*p++ - '0');
    if (p - digits != 3) {
        WhFree(line);
        return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    }
    while (*p == ' ')
        p++;
    request->RespMajor = major;
    request->RespMinor = minor;
    request->StatusCode = code;
    request->StatusText = WhDupA(p);
    request->RawStatusLine = line;
    while (TRUE) {
        error = ReadLine(conn, &line, limit, request->Timeouts.ReceiveResponse, &total);
        if (error)
            return error;
        if (total > limit) {
            WhFree(line);
            return ERROR_WINHTTP_HEADER_SIZE_OVERFLOW;
        }
        if (!*line) {
            WhFree(line);
            break;
        }
        if ((*line == ' ' || *line == '\t') && request->Response.Count) {
            /* a folded line continues the previous value. */
            WH_HEADER *last = &request->Response.Items[request->Response.Count - 1];
            char *start = line;
            while (*start == ' ' || *start == '\t')
                start++;
            SIZE_T old = strlen(last->Value);
            char *value = WhAlloc(old + strlen(start) + 2);
            if (value) {
                memcpy(value, last->Value, old);
                value[old] = ' ';
                strcpy(value + old + 1, start);
                WhFree(last->Value);
                last->Value = value;
            }
            WhFree(line);
            continue;
        }
        char *colon = strchr(line, ':');
        if (colon && colon != line) {
            char *value = colon + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            DWORD value_length = (DWORD)strlen(value);
            while (value_length && (value[value_length - 1] == ' ' || value[value_length - 1] == '\t'))
                value_length--;
            if (!WhHeadersAppend(&request->Response, line, (DWORD)(colon - line), value, value_length)) {
                WhFree(line);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        WhFree(line);
    }
    WhNotify(request, WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED, &total, sizeof(total));
    return ERROR_SUCCESS;
}

static void SetupBody(WH_OBJECT *request)
{
    const char *connection = WhHeadersFind(&request->Response, "Connection", 0);
    if (!connection)
        connection = WhHeadersFind(&request->Response, "Proxy-Connection", 0);
    if (request->RespMajor > 1 || (request->RespMajor == 1 && request->RespMinor >= 1))
        request->KeepAlive = !connection || !ValueHas(connection, "close");
    else
        request->KeepAlive = connection && ValueHas(connection, "keep-alive");
    const char *encoding = WhHeadersFind(&request->Response, "Transfer-Encoding", 0);
    const char *length = WhHeadersFind(&request->Response, "Content-Length", 0);
    DWORD code = request->StatusCode;
    if (VerbIs(request, L"HEAD") || code == 204 || code == 304 || (code >= 100 && code < 200)) {
        request->BodyMode = WH_BODY_NONE;
        request->BodyDone = TRUE;
    } else if (encoding && ValueHas(encoding, "chunked")) {
        request->BodyMode = WH_BODY_CHUNKED;
    } else if (length) {
        ULONGLONG value = 0;
        for (const char *p = length; *p >= '0' && *p <= '9'; p++)
            value = value * 10 + (ULONGLONG)(*p - '0');
        request->BodyMode = WH_BODY_LENGTH;
        request->BodyLeft = value;
        request->BodyDone = !value;
    } else {
        request->BodyMode = WH_BODY_CLOSE;
        request->KeepAlive = FALSE;
    }
}

/* read the next chunk size; consume trailers after the final chunk. */
static DWORD NextChunk(WH_OBJECT *request)
{
    WH_CONN *conn = request->Conn;
    char *line;
    DWORD error = ReadLine(conn, &line, 4096, request->Timeouts.Receive, nullptr);
    if (error)
        return error == ERROR_WINHTTP_HEADER_SIZE_OVERFLOW ? ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW
                                                           : error;
    ULONGLONG size = 0;
    const char *p = line;
    BOOL digits = FALSE;
    while (TRUE) {
        int v = *p >= '0' && *p <= '9' ? *p - '0' : (*p | 0x20) >= 'a' && (*p | 0x20) <= 'f' ? (*p | 0x20) - 'a' + 10 : -1;
        if (v < 0)
            break;
        size = size * 16 + (ULONGLONG)v;
        digits = TRUE;
        p++;
    }
    WhFree(line);
    if (!digits)
        return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    if (size) {
        request->BodyLeft = size;
        return ERROR_SUCCESS;
    }
    while (TRUE) {
        error = ReadLine(conn, &line, 65536, request->Timeouts.Receive, nullptr);
        if (error)
            return error;
        BOOL end = !*line;
        WhFree(line);
        if (end)
            break;
    }
    request->ChunkEnd = TRUE;
    request->BodyDone = TRUE;
    return ERROR_SUCCESS;
}

/* return buffered body size, reading once if empty. */
static DWORD Available(WH_OBJECT *request, BOOL wait, DWORD *available)
{
    *available = 0;
    if (request->BodyMode == WH_BODY_MEMORY) {
        *available = request->MemorySize - request->MemoryOffset;
        return ERROR_SUCCESS;
    }
    while (!request->BodyDone) {
        WH_CONN *conn = request->Conn;
        if (!conn)
            return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
        if (request->BodyMode == WH_BODY_CHUNKED && !request->BodyLeft) {
            if (!wait && !Buffered(conn))
                return ERROR_SUCCESS;
            DWORD error = NextChunk(request);
            if (error)
                return Cancelled(request, error);
            continue;
        }
        DWORD buffered = Buffered(conn);
        if (buffered) {
            if (request->BodyMode != WH_BODY_CLOSE && buffered > request->BodyLeft)
                buffered = (DWORD)request->BodyLeft;
            *available = buffered;
            return ERROR_SUCCESS;
        }
        if (conn->Eof) {
            if (request->BodyMode != WH_BODY_CLOSE)
                return ERROR_WINHTTP_CONNECTION_ERROR;
            request->BodyDone = TRUE;
            break;
        }
        if (!wait)
            return ERROR_SUCCESS;
        WhNotify(request, WINHTTP_CALLBACK_STATUS_RECEIVING_RESPONSE, nullptr, 0);
        DWORD before = conn->End - conn->Start;
        DWORD error = Fill(conn, request->Timeouts.Receive);
        if (error)
            return Cancelled(request, error);
        DWORD received = conn->End - conn->Start - before;
        WhNotify(request, WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED, &received, sizeof(received));
    }
    if (request->Conn && request->BodyMode != WH_BODY_MEMORY)
        FinishConnection(request);
    return ERROR_SUCCESS;
}

static DWORD Consume(WH_OBJECT *request, BYTE *buffer, DWORD size)
{
    if (request->BodyMode == WH_BODY_MEMORY) {
        DWORD count = request->MemorySize - request->MemoryOffset;
        if (count > size)
            count = size;
        memcpy(buffer, request->Memory + request->MemoryOffset, count);
        request->MemoryOffset += count;
        return count;
    }
    WH_CONN *conn = request->Conn;
    DWORD count = Buffered(conn);
    if (count > size)
        count = size;
    if (request->BodyMode != WH_BODY_CLOSE && count > request->BodyLeft)
        count = (DWORD)request->BodyLeft;
    memcpy(buffer, conn->Buffer + conn->Start, count);
    conn->Start += count;
    if (request->BodyMode != WH_BODY_CLOSE)
        request->BodyLeft -= count;
    return count;
}

/* the CRLF closing a chunk, and the end of a sized body. */
static DWORD AfterConsume(WH_OBJECT *request)
{
    if (request->BodyMode == WH_BODY_LENGTH && !request->BodyLeft) {
        request->BodyDone = TRUE;
        FinishConnection(request);
    } else if (request->BodyMode == WH_BODY_CHUNKED && !request->BodyLeft && request->Conn) {
        char *line;
        DWORD error = ReadLine(request->Conn, &line, 16, request->Timeouts.Receive, nullptr);
        if (error)
            return Cancelled(request, error);
        WhFree(line);
    }
    return ERROR_SUCCESS;
}

DWORD WhRead(WH_OBJECT *request, void *buffer, DWORD size, DWORD *read)
{
    *read = 0;
    if (request->State != 2)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    BYTE *out = buffer;
    while (*read < size) {
        DWORD available;
        DWORD error = Available(request, *read == 0, &available);
        if (error)
            return error;
        if (!available)
            break;
        DWORD count = Consume(request, out + *read, size - *read);
        *read += count;
        if ((error = AfterConsume(request)))
            return error;
    }
    if (request->BodyDone && request->Conn && request->BodyMode != WH_BODY_MEMORY)
        FinishConnection(request);
    return ERROR_SUCCESS;
}

DWORD WhQueryAvailable(WH_OBJECT *request, DWORD *available)
{
    if (request->State != 2)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    return Available(request, TRUE, available);
}

/* decompression. */

static void *InflateAlloc(void *context, size_t size)
{
    return WhAlloc(size);
}

static void InflateFree(void *context, void *memory)
{
    WhFree(memory);
}

static const MimeAllocator Allocator = {InflateAlloc, InflateFree, nullptr};

/* the deflate data of a gzip member, after its header. */
static BOOL GzipPayload(const BYTE *data, DWORD size, DWORD *offset)
{
    if (size < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8)
        return FALSE;
    BYTE flags = data[3];
    DWORD p = 10;
    if (flags & 4) {
        if (p + 2 > size)
            return FALSE;
        p += 2 + (data[p] | data[p + 1] << 8);
    }
    if (flags & 8)
        while (p < size && data[p++])
            ;
    if (flags & 16)
        while (p < size && data[p++])
            ;
    if (flags & 2)
        p += 2;
    if (p >= size)
        return FALSE;
    *offset = p;
    return TRUE;
}

static DWORD Decompress(WH_OBJECT *request, BOOL gzip)
{
    BYTE *raw = nullptr;
    DWORD size = 0, capacity = 0;
    while (TRUE) {
        if (capacity - size < 65536) {
            capacity = capacity ? capacity * 2 : 131072;
            BYTE *grown = WhRealloc(raw, capacity);
            if (!grown) {
                WhFree(raw);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            raw = grown;
        }
        DWORD got;
        DWORD error = WhRead(request, raw + size, capacity - size, &got);
        if (error) {
            WhFree(raw);
            return error;
        }
        if (!got)
            break;
        size += got;
    }
    void *plain = nullptr;
    size_t plain_size = 0;
    int result = MIME_INFLATE_ERR_DATA;
    DWORD offset;
    if (!size)
        result = MIME_INFLATE_OK;
    else if (gzip && GzipPayload(raw, size, &offset))
        result = mime_inflate_alloc(raw + offset, size - offset, &Allocator, (size_t)size * 4, 0, &plain,
                                    &plain_size, 0);
    else if (!gzip) {
        result = mime_inflate_alloc(raw, size, &Allocator, (size_t)size * 4, 0, &plain, &plain_size,
                                    MIME_INFLATE_ZLIB | MIME_INFLATE_NO_CHECKSUM);
        if (result != MIME_INFLATE_OK)
            result = mime_inflate_alloc(raw, size, &Allocator, (size_t)size * 4, 0, &plain, &plain_size, 0);
    }
    WhFree(raw);
    if (result != MIME_INFLATE_OK)
        return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    request->BodyMode = WH_BODY_MEMORY;
    request->Memory = plain;
    request->MemorySize = (DWORD)plain_size;
    request->MemoryOffset = 0;
    request->BodyDone = TRUE;
    return ERROR_SUCCESS;
}

/* redirects. */

static int SchemeDefault(BOOL secure)
{
    return secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
}

/* resolve relative Location values against the request URL. */
static PWSTR ResolveLocation(WH_OBJECT *request, const char *location)
{
    PWSTR value = WhAnsiToWide(location, (DWORD)strlen(location));
    if (!value)
        return nullptr;
    LPCWSTR p = value;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#')
        p++;
    if (*p == ':')
        return value;
    SIZE_T size = wcslen(request->Host) + wcslen(request->Path) + wcslen(value) + 32;
    PWSTR url = WhAlloc(size * sizeof(WCHAR));
    if (!url) {
        WhFree(value);
        return nullptr;
    }
    LPCWSTR scheme = request->Secure ? L"https" : L"http";
    WCHAR port[8] = L"";
    if (request->HostPort != SchemeDefault(request->Secure))
        wsprintfW(port, L":%u", request->HostPort);
    if (value[0] == '/' && value[1] == '/') {
        wsprintfW(url, L"%s:%s", scheme, value);
    } else if (value[0] == '/') {
        wsprintfW(url, L"%s://%s%s%s", scheme, request->Host, port, value);
    } else {
        /* relative to the directory of the current path. */
        PWSTR base = WhDupW(request->Path);
        if (!base) {
            WhFree(value);
            WhFree(url);
            return nullptr;
        }
        PWSTR query = wcschr(base, '?');
        if (query)
            *query = 0;
        PWSTR slash = wcsrchr(base, '/');
        if (slash)
            slash[1] = 0;
        else
            wcscpy(base, L"/");
        wsprintfW(url, L"%s://%s%s%s%s", scheme, request->Host, port, base, value);
        WhFree(base);
    }
    WhFree(value);
    return url;
}

static DWORD ApplyRedirect(WH_OBJECT *request, LPCWSTR url)
{
    URL_COMPONENTS uc = {sizeof(uc)};
    uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WhCrack(url, 0, 0, &uc))
        return ERROR_WINHTTP_INVALID_URL;
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)
        return ERROR_WINHTTP_UNRECOGNIZED_SCHEME;
    BOOL secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    PWSTR host = WhDupWn(uc.lpszHostName, uc.dwHostNameLength);
    PWSTR path = WhAlloc(((SIZE_T)uc.dwUrlPathLength + uc.dwExtraInfoLength + 2) * sizeof(WCHAR));
    if (!host || !path) {
        WhFree(host);
        WhFree(path);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (uc.dwUrlPathLength)
        memcpy(path, uc.lpszUrlPath, uc.dwUrlPathLength * sizeof(WCHAR));
    else
        path[0] = '/';
    DWORD length = uc.dwUrlPathLength ? uc.dwUrlPathLength : 1;
    /* the fragment stays with the client. */
    DWORD extra = uc.dwExtraInfoLength;
    for (DWORD i = 0; i < extra; i++)
        if (uc.lpszExtraInfo[i] == '#') {
            extra = i;
            break;
        }
    memcpy(path + length, uc.lpszExtraInfo, extra * sizeof(WCHAR));
    path[length + extra] = 0;
    BOOL same_host = !_wcsicmp(host, request->Host);
    WhFree(request->Host);
    WhFree(request->Path);
    request->Host = host;
    request->Path = path;
    request->HostPort = uc.nPort;
    request->Secure = secure;
    if (!same_host) {
        WhHeadersRemove(&request->Request, "Authorization");
        WhHeadersRemove(&request->Request, "Host");
    }
    return ERROR_SUCCESS;
}

/* drain the body before reusing the connection. */
static void Drain(WH_OBJECT *request)
{
    BYTE scratch[4096];
    ULONGLONG drained = 0;
    while (!request->BodyDone && drained <= request->MaxResponseDrainSize) {
        DWORD got;
        if (WhRead(request, scratch, sizeof(scratch), &got) || !got)
            break;
        drained += got;
    }
    if (!request->BodyDone)
        request->KeepAlive = FALSE;
    FinishConnection(request);
}

/* follow policy-permitted redirects; TRUE means a new request was sent. */
static DWORD Redirect(WH_OBJECT *request, BOOL *followed)
{
    *followed = FALSE;
    DWORD code = request->StatusCode;
    if (code != 301 && code != 302 && code != 303 && code != 307 && code != 308)
        return ERROR_SUCCESS;
    if (request->RedirectPolicy == WINHTTP_OPTION_REDIRECT_POLICY_NEVER)
        return ERROR_SUCCESS;
    const char *location = WhHeadersFind(&request->Response, "Location", 0);
    if (!location || !*location)
        return ERROR_SUCCESS;
    PWSTR url = ResolveLocation(request, location);
    if (!url)
        return ERROR_NOT_ENOUGH_MEMORY;
    BOOL to_http = !_wcsnicmp(url, L"http:", 5);
    if (request->Secure && to_http &&
        request->RedirectPolicy == WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP) {
        WhFree(url);
        return ERROR_SUCCESS;
    }
    /* a body written with WinHttpWriteData cannot be sent again. */
    BOOL keep_verb = code == 307 || code == 308;
    if (keep_verb && request->TotalLength > request->OptionalLength) {
        WhFree(url);
        return ERROR_SUCCESS;
    }
    if (++request->Redirects > request->MaxRedirects) {
        WhFree(url);
        return ERROR_WINHTTP_REDIRECT_FAILED;
    }
    Drain(request);
    WhNotify(request, WINHTTP_CALLBACK_STATUS_REDIRECT, url, (DWORD)wcslen(url));
    DWORD error = ApplyRedirect(request, url);
    WhFree(url);
    if (error)
        return error;
    if (!keep_verb && (code == 303 ? !VerbIs(request, L"HEAD") : VerbIs(request, L"POST"))) {
        PWSTR get = WhDupW(L"GET");
        if (!get)
            return ERROR_NOT_ENOUGH_MEMORY;
        WhFree(request->Verb);
        request->Verb = get;
        WhFree(request->Optional);
        request->Optional = nullptr;
        request->OptionalLength = 0;
        request->TotalLength = 0;
        WhHeadersRemove(&request->Request, "Content-Length");
        WhHeadersRemove(&request->Request, "Content-Type");
    }
    ResetResponse(request);
    error = Transmit(request);
    if (error)
        return error;
    *followed = TRUE;
    return ERROR_SUCCESS;
}

DWORD WhReceiveResponse(WH_OBJECT *request)
{
    if (request->State != 1 || !request->Conn)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    DWORD error;
    if (request->ChunkedUpload && (error = WhConnSend(request->Conn, "0\r\n\r\n", 5, request->Timeouts.Send)))
        return Cancelled(request, error);
    while (TRUE) {
        WhNotify(request, WINHTTP_CALLBACK_STATUS_RECEIVING_RESPONSE, nullptr, 0);
        ResetResponse(request);
        error = ReadHead(request);
        /* retry once if the server closed the pooled connection. */
        if (error == ERROR_WINHTTP_CONNECTION_ERROR && request->ConnReused && !request->Closed &&
            request->TotalLength == request->OptionalLength) {
            request->ConnReused = FALSE;
            WhDropConnection(request);
            if (!(error = Connect(request, FALSE))) {
                WH_OUT out = {0};
                error = BuildRequest(request, request->Conn->ViaProxy, &out);
                if (!error && request->OptionalLength)
                    Put(&out, (char *)request->Optional, request->OptionalLength);
                if (!error)
                    error = out.Failed ? ERROR_NOT_ENOUGH_MEMORY
                                       : WhConnSend(request->Conn, out.Data, out.Length, request->Timeouts.Send);
                WhFree(out.Data);
            }
            if (!error)
                continue;
        }
        if (error)
            return Cancelled(request, error);
        if (request->StatusCode >= 100 && request->StatusCode < 200 && request->StatusCode != 101) {
            DWORD code = request->StatusCode;
            WhNotify(request, WINHTTP_CALLBACK_STATUS_INTERMEDIATE_RESPONSE, &code, sizeof(code));
            continue;
        }
        SetupBody(request);
        request->State = 2;
        if (request->StatusCode == 101)
            return ERROR_SUCCESS;
        BOOL followed;
        error = Redirect(request, &followed);
        if (error)
            return Cancelled(request, error);
        if (followed) {
            request->State = 1;
            continue;
        }
        break;
    }
    const char *encoding = WhHeadersFind(&request->Response, "Content-Encoding", 0);
    if (encoding && request->Decompression) {
        BOOL gzip = ValueHas(encoding, "gzip") && request->Decompression & WINHTTP_DECOMPRESSION_FLAG_GZIP;
        BOOL deflate = ValueHas(encoding, "deflate") && request->Decompression & WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
        if ((gzip || deflate) && (error = Decompress(request, gzip)))
            return error;
    }
    if (request->BodyDone && request->Conn && request->BodyMode != WH_BODY_MEMORY)
        FinishConnection(request);
    return ERROR_SUCCESS;
}

/* the api. */

static WH_OBJECT *ReferenceRequest(HINTERNET handle)
{
    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    WH_OBJECT *object = WhReference(handle);
    if (object && object->Type != WH_REQUEST) {
        WhRelease(object);
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
        return nullptr;
    }
    return object;
}

static DWORD OpApi(DWORD op)
{
    switch (op) {
    case WH_OP_SEND:
        return API_SEND_REQUEST;
    case WH_OP_RECEIVE:
        return API_RECEIVE_RESPONSE;
    case WH_OP_QUERY:
        return API_QUERY_DATA_AVAILABLE;
    case WH_OP_READ:
        return API_READ_DATA;
    }
    return API_WRITE_DATA;
}

static DWORD RunOp(WH_OBJECT *request, DWORD op, DWORD *count)
{
    *count = 0;
    if (request->Closed)
        return ERROR_WINHTTP_OPERATION_CANCELLED;
    switch (op) {
    case WH_OP_SEND:
        return WhSendRequest(request);
    case WH_OP_RECEIVE:
        return WhReceiveResponse(request);
    case WH_OP_QUERY:
        return WhQueryAvailable(request, count);
    case WH_OP_READ:
        return WhRead(request, request->OpBuffer, request->OpSize, count);
    case WH_OP_WRITE:
        return WhWrite(request, request->OpBuffer, request->OpSize, count);
    }
    return ERROR_SUCCESS;
}

static void AfterOp(WH_OBJECT *request, DWORD op, DWORD error)
{
    if (op == WH_OP_SEND)
        request->State = error ? 0 : 1;
    if (op == WH_OP_RECEIVE && error)
        request->State = 0;
}

static void Complete(WH_OBJECT *request, DWORD op, DWORD error, DWORD count)
{
    if (error) {
        WINHTTP_ASYNC_RESULT result = {OpApi(op), error};
        WhNotify(request, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &result, sizeof(result));
        return;
    }
    switch (op) {
    case WH_OP_SEND:
        WhNotify(request, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, nullptr, 0);
        break;
    case WH_OP_RECEIVE:
        WhNotify(request, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, nullptr, 0);
        break;
    case WH_OP_QUERY:
        WhNotify(request, WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, &count, sizeof(count));
        break;
    case WH_OP_READ:
        WhNotify(request, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, request->OpBuffer, count);
        break;
    case WH_OP_WRITE:
        WhNotify(request, WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE, &count, sizeof(count));
        break;
    }
}

static DWORD WINAPI Worker(LPVOID parameter)
{
    WH_OBJECT *request = parameter;
    while (TRUE) {
        EnterCriticalSection(&request->Lock);
        DWORD op = request->Op;
        request->Op = WH_OP_NONE;
        BOOL closed = request->Closed;
        LeaveCriticalSection(&request->Lock);
        if (op != WH_OP_NONE) {
            DWORD count;
            DWORD error = RunOp(request, op, &count);
            if (request->Closed && error)
                error = ERROR_WINHTTP_OPERATION_CANCELLED;
            AfterOp(request, op, error);
            EnterCriticalSection(&request->Lock);
            request->Busy = FALSE;
            LeaveCriticalSection(&request->Lock);
            Complete(request, op, error, count);
            continue;
        }
        if (closed)
            break;
        WaitForSingleObject(request->Wake, INFINITE);
    }
    WhRelease(request);
    return 0;
}

/* run on the session worker for asynchronous sessions, otherwise on the caller. */
static BOOL Start(WH_OBJECT *request, DWORD op, LPVOID buffer, DWORD size, LPDWORD count)
{
    if (!WhIsAsync(request)) {
        DWORD result;
        request->OpBuffer = buffer;
        request->OpSize = size;
        DWORD error = RunOp(request, op, &result);
        AfterOp(request, op, error);
        if (count)
            *count = result;
        WhRelease(request);
        SetLastError(error);
        return !error;
    }
    EnterCriticalSection(&request->Lock);
    if (request->Busy) {
        LeaveCriticalSection(&request->Lock);
        WhRelease(request);
        return WhFail(ERROR_WINHTTP_INCORRECT_HANDLE_STATE);
    }
    if (!request->Worker) {
        request->Wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        WhAddRef(request);
        request->Worker = request->Wake ? CreateThread(nullptr, 0, Worker, request, 0, nullptr) : nullptr;
        if (!request->Worker) {
            LeaveCriticalSection(&request->Lock);
            WhRelease(request);
            WhRelease(request);
            return WhFail(ERROR_NOT_ENOUGH_MEMORY);
        }
    }
    request->Busy = TRUE;
    request->Op = op;
    request->OpBuffer = buffer;
    request->OpSize = size;
    SetEvent(request->Wake);
    LeaveCriticalSection(&request->Lock);
    /* asynchronous reads report their size in the completion only. */
    if (count)
        *count = 0;
    WhRelease(request);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL ParseLength(const char *text, ULONGLONG *value)
{
    while (*text == ' ' || *text == '\t')
        text++;
    if (!*text)
        return FALSE;
    ULONGLONG v = 0;
    for (; *text >= '0' && *text <= '9'; text++)
        v = v * 10 + (ULONGLONG)(*text - '0');
    while (*text == ' ' || *text == '\t')
        text++;
    *value = v;
    return !*text;
}

BOOL WINAPI WinHttpSendRequest(HINTERNET handle, LPCWSTR headers, DWORD headers_length, LPVOID optional,
                               DWORD optional_length, DWORD total_length, DWORD_PTR context)
{
    if (!handle)
        return WhFail(ERROR_INVALID_HANDLE);
    if (headers && (!headers_length || headers_length == (DWORD)-1))
        headers_length = (DWORD)wcslen(headers);
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    if (context)
        request->Context = context;
    if (request->Busy) {
        WhRelease(request);
        return WhFail(ERROR_WINHTTP_INCORRECT_HANDLE_STATE);
    }
    if (!optional || !optional_length) {
        optional = nullptr;
        optional_length = 0;
    }
    DWORD error = ERROR_SUCCESS;
    if (request->State == 1)
        request->State = 0;
    if (headers && headers_length && *headers) {
        BOOL lossy = FALSE;
        char *text = WhWideToAnsi(headers, headers_length, &lossy);
        if (!text)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else if (lossy)
            error = ERROR_INVALID_PARAMETER;
        else
            error = WhAddHeaders(request, text, (DWORD)strlen(text),
                                 request->HeadersAdded ? WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE
                                                       : WINHTTP_ADDREQ_FLAG_ADD);
        WhFree(text);
        if (!error)
            request->HeadersAdded = TRUE;
    }
    ULONGLONG total = total_length;
    BOOL chunked = FALSE;
    if (!error) {
        const char *encoding = WhHeadersFind(&request->Request, "Transfer-Encoding", 0);
        const char *length = WhHeadersFind(&request->Request, "Content-Length", 0);
        if (encoding && (VerbIs(request, L"GET") || VerbIs(request, L"HEAD")))
            error = ERROR_INVALID_PARAMETER;
        else if (length && (encoding || !ParseLength(length, &total)))
            error = ERROR_INVALID_PARAMETER;
        else if (optional_length > total)
            error = ERROR_INVALID_PARAMETER;
        chunked = encoding != nullptr;
    }
    if (!error) {
        BYTE *copy = optional_length ? WhAlloc(optional_length) : nullptr;
        if (optional_length && !copy) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            if (copy)
                memcpy(copy, optional, optional_length);
            WhFree(request->Optional);
            request->Optional = copy;
            request->OptionalLength = optional_length;
            request->TotalLength = total;
            request->Written = 0;
            /* WinHTTP adds chunk framing only with WINHTTP_FLAG_AUTOMATIC_CHUNKING. */
            request->ChunkedUpload = !chunked && request->Flags & WINHTTP_FLAG_AUTOMATIC_CHUNKING && !total;
        }
    }
    if (error) {
        WhRelease(request);
        return WhFail(error);
    }
    return Start(request, WH_OP_SEND, nullptr, 0, nullptr);
}

BOOL WINAPI WinHttpReceiveResponse(HINTERNET handle, LPVOID reserved)
{
    if (!handle)
        return WhFail(ERROR_INVALID_HANDLE);
    if (reserved)
        return WhFail(ERROR_INVALID_PARAMETER);
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    if (request->State != 1) {
        WhRelease(request);
        return WhFail(ERROR_WINHTTP_INCORRECT_HANDLE_STATE);
    }
    return Start(request, WH_OP_RECEIVE, nullptr, 0, nullptr);
}

BOOL WINAPI WinHttpQueryDataAvailable(HINTERNET handle, LPDWORD available)
{
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    if (WhIsAsync(request)) {
        /* the count of an asynchronous query arrives with WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE. */
        return Start(request, WH_OP_QUERY, nullptr, 0, available);
    }
    DWORD count = 0;
    BOOL ok = Start(request, WH_OP_QUERY, nullptr, 0, &count);
    if (ok && available)
        *available = count;
    return ok;
}

BOOL WINAPI WinHttpReadData(HINTERNET handle, LPVOID buffer, DWORD size, LPDWORD read)
{
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    if (!buffer && size) {
        WhRelease(request);
        return WhFail(ERROR_INVALID_PARAMETER);
    }
    DWORD count = 0;
    BOOL ok = Start(request, WH_OP_READ, buffer, size, &count);
    if (ok && read)
        *read = count;
    return ok;
}

DWORD WINAPI WinHttpReadDataEx(HINTERNET handle, LPVOID buffer, DWORD size, LPDWORD read, ULONGLONG flags,
                               DWORD property_size, LPVOID property)
{
    if (flags & ~1ull || property_size || property)
        return ERROR_INVALID_PARAMETER;
    return WinHttpReadData(handle, buffer, size, read) ? ERROR_SUCCESS : GetLastError();
}

BOOL WINAPI WinHttpWriteData(HINTERNET handle, LPCVOID buffer, DWORD size, LPDWORD written)
{
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    if (!buffer && size) {
        WhRelease(request);
        return WhFail(ERROR_INVALID_PARAMETER);
    }
    DWORD count = 0;
    BOOL ok = Start(request, WH_OP_WRITE, (LPVOID)buffer, size, &count);
    if (ok && written)
        *written = count;
    return ok;
}

/* authentication. */

BOOL WINAPI WinHttpQueryAuthSchemes(HINTERNET handle, LPDWORD supported, LPDWORD first, LPDWORD target)
{
    if (!supported || !first || !target)
        return WhFail(ERROR_INVALID_PARAMETER);
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    DWORD error = ERROR_SUCCESS;
    if (request->State != 2) {
        error = ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    } else {
        BOOL proxy = request->StatusCode == 407;
        const char *name = proxy ? "Proxy-Authenticate" : "WWW-Authenticate";
        static const struct {
            const char *Name;
            DWORD Scheme;
        } schemes[] = {{"Basic", WINHTTP_AUTH_SCHEME_BASIC},
                       {"NTLM", WINHTTP_AUTH_SCHEME_NTLM},
                       {"Passport1.4", WINHTTP_AUTH_SCHEME_PASSPORT},
                       {"Digest", WINHTTP_AUTH_SCHEME_DIGEST},
                       {"Negotiate", WINHTTP_AUTH_SCHEME_NEGOTIATE}};
        *supported = *first = 0;
        const char *value;
        for (DWORD i = 0; (value = WhHeadersFind(&request->Response, name, i)); i++)
            for (DWORD k = 0; k < ARRAYSIZE(schemes); k++) {
                SIZE_T length = strlen(schemes[k].Name);
                if (!_strnicmp(value, schemes[k].Name, length) && (!value[length] || value[length] == ' ')) {
                    if (!*supported)
                        *first = schemes[k].Scheme;
                    *supported |= schemes[k].Scheme;
                }
            }
        *target = proxy ? WINHTTP_AUTH_TARGET_PROXY : WINHTTP_AUTH_TARGET_SERVER;
        if (!*supported)
            error = ERROR_INVALID_OPERATION;
    }
    WhRelease(request);
    SetLastError(error);
    return !error;
}

BOOL WINAPI WinHttpSetCredentials(HINTERNET handle, DWORD target, DWORD scheme, LPCWSTR user, LPCWSTR password,
                                  LPVOID params)
{
    if (target > WINHTTP_AUTH_TARGET_PROXY || !scheme || scheme & (scheme - 1) || scheme > WINHTTP_AUTH_SCHEME_NEGOTIATE ||
        (scheme == WINHTTP_AUTH_SCHEME_BASIC && !user))
        return WhFail(ERROR_INVALID_PARAMETER);
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    PWSTR user_copy = WhDupW(user), password_copy = WhDupW(password);
    if (target == WINHTTP_AUTH_TARGET_SERVER) {
        WhFree(request->User);
        WhFree(request->Password);
        request->User = user_copy;
        request->Password = password_copy;
        request->AuthScheme = scheme;
    } else {
        WhFree(request->ProxyUser);
        WhFree(request->ProxyPassword);
        request->ProxyUser = user_copy;
        request->ProxyPassword = password_copy;
        request->ProxyAuthScheme = scheme;
    }
    WhRelease(request);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}
