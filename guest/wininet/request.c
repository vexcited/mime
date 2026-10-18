/*
 * the http engine: sending requests, reading responses and bodies, redirects, authentication, cookies,
 * decoding, and the worker thread of asynchronous requests.
 */
#include "wininet_private.h"
#include "png/inflate.h"

#define INET_MAX_REDIRECTS 10
#define INET_HEAD_LIMIT 65536

static DWORD SendTimeout(INET_OBJECT *request)
{
    return INET_TIMEOUT(request, INTERNET_OPTION_SEND_TIMEOUT);
}

static DWORD ReceiveTimeout(INET_OBJECT *request)
{
    return INET_TIMEOUT(request, INTERNET_OPTION_RECEIVE_TIMEOUT);
}

static BOOL VerbIs(INET_OBJECT *request, LPCWSTR verb)
{
    return !lstrcmpiW(request->Verb, verb);
}

static DWORD Cancelled(INET_OBJECT *request, DWORD error)
{
    return request->Closed ? ERROR_INTERNET_OPERATION_CANCELLED : error;
}

/* ------------------------------------------------------------------ connections of a request */

static void SetConn(INET_OBJECT *request, INET_CONN *conn)
{
    AcquireSRWLockExclusive(&request->Lock);
    request->Conn = conn;
    BOOL closed = request->Closed;
    ReleaseSRWLockExclusive(&request->Lock);
    if (closed && conn)
        shutdown(conn->Socket, SD_BOTH);
}

static INET_CONN *TakeConn(INET_OBJECT *request)
{
    AcquireSRWLockExclusive(&request->Lock);
    INET_CONN *conn = request->Conn;
    request->Conn = nullptr;
    ReleaseSRWLockExclusive(&request->Lock);
    return conn;
}

static void DropConnection(INET_OBJECT *request)
{
    InetConnClose(nullptr, TakeConn(request));
}

/* the connection goes back to the pool when its response ended cleanly, else it is closed. */
static void FinishConnection(INET_OBJECT *request)
{
    INET_CONN *conn = TakeConn(request);
    if (!conn)
        return;
    if (request->KeepAlive && !conn->Eof && !InetConnBuffered(conn) && !request->Closed)
        InetPoolPut(conn);
    else
        InetConnClose(request, conn);
}

BOOL InetRequestClosing(INET_OBJECT *request)
{
    AcquireSRWLockExclusive(&request->Lock);
    request->Closed = TRUE;
    INET_CONN *conn = request->Conn;
    if (conn)
        shutdown(conn->Socket, SD_BOTH);
    BOOL deferred = request->Busy;
    if (deferred)
        request->CloseDeferred = TRUE;
    if (request->Wake)
        SetEvent(request->Wake);
    ReleaseSRWLockExclusive(&request->Lock);
    return deferred;
}

void InetRequestCleanup(INET_OBJECT *request)
{
    DropConnection(request);
    InetHeadersClear(&request->Response);
    InetFree(request->StatusLine);
    InetFree(request->StatusText);
    InetFree(request->VersionText);
    InetFree(request->Memory);
    InetFree(request->Optional);
    InetFree(request->Op.Headers);
    InetCertificateFree(request->Certificate);
    if (request->Worker)
        CloseHandle(request->Worker);
    if (request->Wake)
        CloseHandle(request->Wake);
}

/* the http/1.1 tunnel of an https request through a proxy. */
static DWORD Tunnel(INET_OBJECT *request, INET_CONN *conn)
{
    char *host = InetNarrow(request->Host, -1);
    if (!host)
        return ERROR_NOT_ENOUGH_MEMORY;
    char text[INTERNET_MAX_HOST_NAME_LENGTH * 2 + 64];
    wsprintfA(text, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n\r\n", host, request->HostPort, host,
              request->HostPort);
    InetFree(host);
    DWORD error = InetConnSend(conn, text, (DWORD)strlen(text), SendTimeout(request));
    char *line = nullptr;
    if (!error)
        error = InetConnReadLine(conn, &line, INET_HEAD_LIMIT, ReceiveTimeout(request), nullptr);
    if (error)
        return error;
    const char *code = strchr(line, ' ');
    BOOL ok = !_strnicmp(line, "HTTP/", 5) && code && code[1] == '2';
    InetFree(line);
    while (TRUE) {
        error = InetConnReadLine(conn, &line, INET_HEAD_LIMIT, ReceiveTimeout(request), nullptr);
        if (error)
            return error;
        BOOL end = !*line;
        InetFree(line);
        if (end)
            break;
    }
    return ok ? ERROR_SUCCESS : ERROR_INTERNET_CANNOT_CONNECT;
}

/* a connection for the request: a pooled one when allowed, else a new one, with tls when secure. */
static DWORD Connect(INET_OBJECT *request, BOOL allow_reuse)
{
    WCHAR proxy[INTERNET_MAX_HOST_NAME_LENGTH] = L"";
    INTERNET_PORT proxy_port = 0;
    if (!InetPickProxy(request, request->Host, request->Secure, proxy, ARRAYSIZE(proxy), &proxy_port))
        proxy[0] = 0;
    INET_CONN *conn = allow_reuse ? InetPoolTake(request->Host, request->HostPort, request->Secure, proxy, proxy_port)
                                  : nullptr;
    request->ConnReused = conn != nullptr;
    if (!conn) {
        DWORD error = InetConnOpen(request, proxy[0] ? proxy : request->Host,
                                   proxy[0] ? proxy_port : request->HostPort, &conn);
        if (error)
            return error;
        lstrcpynW(conn->Host, request->Host, ARRAYSIZE(conn->Host));
        conn->Port = request->HostPort;
        conn->Secure = request->Secure;
        lstrcpyW(conn->Proxy, proxy);
        conn->ProxyPort = proxy_port;
        SetConn(request, conn);
        if (request->Secure) {
            if (proxy[0] && (error = Tunnel(request, conn)))
                return error;
            DWORD flags = request->SecurityFlags;
            if (request->Flags & INTERNET_FLAG_IGNORE_CERT_CN_INVALID)
                flags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            if (request->Flags & INTERNET_FLAG_IGNORE_CERT_DATE_INVALID)
                flags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            error = InetTlsHandshake(conn, request->Host, flags, INET_TIMEOUT(request, INTERNET_OPTION_CONNECT_TIMEOUT));
            request->CertErrors |= conn->CertErrors;
            if (error)
                return error;
        }
    } else {
        SetConn(request, conn);
    }
    if (conn->Certificate) {
        InetCertificateFree(request->Certificate);
        request->Certificate = InetCertificateDuplicate(conn->Certificate);
        request->CipherStrength = conn->CipherStrength;
        request->Protocol = conn->Protocol;
    }
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ building the request */

typedef struct {
    char *Data;
    DWORD Length;
    DWORD Capacity;
    BOOL Failed;
} INET_OUT;

static void Put(INET_OUT *out, const void *data, DWORD length)
{
    if (out->Failed)
        return;
    if (out->Length + length + 1 > out->Capacity) {
        DWORD capacity = (out->Length + length + 1) * 2;
        char *grown = InetRealloc(out->Data, capacity);
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

static void PutString(INET_OUT *out, const char *text)
{
    Put(out, text, (DWORD)strlen(text));
}

/* the request target: utf-8 with spaces, controls and bytes above ascii escaped. */
static void PutTarget(INET_OUT *out, LPCWSTR object)
{
    int bytes = WideCharToMultiByte(CP_UTF8, 0, object, -1, nullptr, 0, nullptr, nullptr);
    char *utf8 = InetAlloc((SIZE_T)bytes);
    if (!utf8) {
        out->Failed = TRUE;
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, object, -1, utf8, bytes, nullptr, nullptr);
    for (const BYTE *p = (const BYTE *)utf8; *p; p++) {
        if (*p <= ' ' || *p >= 0x7F || *p == '#') {
            char escaped[4];
            wsprintfA(escaped, "%%%02X", *p);
            Put(out, escaped, 3);
        } else {
            Put(out, p, 1);
        }
    }
    InetFree(utf8);
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

/* sets a request header the engine owns, replacing what was there. */
static BOOL SetHeader(INET_OBJECT *request, const char *name, const char *value)
{
    AcquireSRWLockExclusive(&request->Lock);
    InetHeadersRemove(&request->Request, name);
    BOOL ok = !value || InetHeadersAppend(&request->Request, name, (DWORD)strlen(name), value, (DWORD)strlen(value));
    ReleaseSRWLockExclusive(&request->Lock);
    return ok;
}

/* adds a request header the application did not give. */
static BOOL AddHeaderIfNew(INET_OBJECT *request, const char *name, const char *value)
{
    AcquireSRWLockExclusive(&request->Lock);
    BOOL ok = InetHeadersFind(&request->Request, name, 0) != nullptr ||
              InetHeadersAppend(&request->Request, name, (DWORD)strlen(name), value, (DWORD)strlen(value));
    ReleaseSRWLockExclusive(&request->Lock);
    return ok;
}

static BOOL SetBasic(INET_OBJECT *request, const char *name, LPCWSTR user, LPCWSTR password)
{
    SIZE_T length = (SIZE_T)lstrlenW(user) + 1 + (password ? lstrlenW(password) : 0);
    PWSTR joined = InetAlloc((length + 1) * sizeof(WCHAR));
    if (!joined)
        return FALSE;
    lstrcpyW(joined, user);
    lstrcatW(joined, L":");
    if (password)
        lstrcatW(joined, password);
    char *narrow = InetNarrow(joined, -1);
    InetFree(joined);
    if (!narrow)
        return FALSE;
    DWORD bytes = (DWORD)strlen(narrow);
    char *encoded = InetAlloc((SIZE_T)bytes * 2 + 16);
    BOOL ok = encoded != nullptr;
    if (ok) {
        lstrcpyA(encoded, "Basic ");
        Base64((BYTE *)narrow, bytes, encoded + 6);
        ok = SetHeader(request, name, encoded);
    }
    InetFree(encoded);
    InetFree(narrow);
    return ok;
}

/* the path part of the request object, for cookies. */
static LPCWSTR ObjectPath(INET_OBJECT *request)
{
    LPCWSTR object = request->Object;
    if (!_wcsnicmp(object, L"http://", 7) || !_wcsnicmp(object, L"https://", 8)) {
        LPCWSTR slash = wcschr(wcschr(object, ':') + 3, '/');
        return slash ? slash : L"/";
    }
    return object;
}

/* the headers wininet adds to the application's before each request goes out. */
static DWORD AddDefaultHeaders(INET_OBJECT *request, BOOL via_proxy)
{
    INET_OBJECT *session = InetSession(request);
    BOOL ok = TRUE;
    if (session->Agent && *session->Agent) {
        char *agent = InetNarrow(session->Agent, -1);
        ok &= agent && AddHeaderIfNew(request, "User-Agent", agent);
        InetFree(agent);
    }
    WCHAR host[INTERNET_MAX_HOST_NAME_LENGTH + 8];
    if (request->HostPort == (request->Secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT))
        lstrcpynW(host, request->Host, INTERNET_MAX_HOST_NAME_LENGTH);
    else
        wsprintfW(host, L"%s:%u", request->Host, request->HostPort);
    char *narrow_host = InetNarrow(host, -1);
    ok &= narrow_host && AddHeaderIfNew(request, "Host", narrow_host);
    InetFree(narrow_host);
    ULONGLONG length = request->TotalLength ? request->TotalLength : request->OptionalLength;
    if (length || (!VerbIs(request, L"GET") && !VerbIs(request, L"HEAD"))) {
        char text[24];
        wsprintfA(text, "%I64u", length);
        AcquireSRWLockShared(&request->Lock);
        BOOL chunked = InetHeadersFind(&request->Request, "Transfer-Encoding", 0) != nullptr;
        ReleaseSRWLockShared(&request->Lock);
        if (!chunked)
            ok &= AddHeaderIfNew(request, "Content-Length", text);
    }
    if (request->Flags & (INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE))
        ok &= AddHeaderIfNew(request, "Cache-Control", "no-cache");
    if (request->Decoding)
        ok &= AddHeaderIfNew(request, "Accept-Encoding", "gzip, deflate");
    ok &= AddHeaderIfNew(request, via_proxy && !request->Secure ? "Proxy-Connection" : "Connection", "Keep-Alive");
    if (request->JarCookie) {
        ok &= SetHeader(request, "Cookie", nullptr);
        request->JarCookie = FALSE;
    }
    if (!(request->Flags & INTERNET_FLAG_NO_COOKIES)) {
        AcquireSRWLockShared(&request->Lock);
        BOOL own = InetHeadersFind(&request->Request, "Cookie", 0) != nullptr;
        ReleaseSRWLockShared(&request->Lock);
        char *cookies = own ? nullptr : InetCookiesFor(request->Host, ObjectPath(request), request->Secure);
        if (cookies) {
            ok &= SetHeader(request, "Cookie", cookies);
            request->JarCookie = TRUE;
            InetNotify(request, INTERNET_STATUS_COOKIE_SENT, nullptr, 0);
        }
        InetFree(cookies);
    }
    if (request->AuthTried && request->User && *request->User && !(request->Flags & INTERNET_FLAG_NO_AUTH))
        ok &= SetBasic(request, "Authorization", request->User, request->Password);
    if (request->ProxyAuthTried && via_proxy && request->ProxyUser && *request->ProxyUser)
        ok &= SetBasic(request, "Proxy-Authorization", request->ProxyUser, request->ProxyPassword);
    return ok ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

/* the request line and headers as they go on the wire. */
static DWORD BuildRequest(INET_OBJECT *request, BOOL via_proxy, INET_OUT *out)
{
    char *verb = InetNarrow(request->Verb, -1), *version = InetNarrow(request->Version, -1);
    if (!verb || !version) {
        InetFree(verb);
        InetFree(version);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    PutString(out, verb);
    Put(out, " ", 1);
    LPCWSTR object = request->Object;
    BOOL absolute = !_wcsnicmp(object, L"http://", 7) || !_wcsnicmp(object, L"https://", 8);
    if (via_proxy && !request->Secure && !absolute) {
        PWSTR url = InetRequestUrl(request);
        if (url)
            PutTarget(out, url);
        else
            out->Failed = TRUE;
        InetFree(url);
    } else {
        PutTarget(out, object);
    }
    Put(out, " ", 1);
    PutString(out, version);
    Put(out, "\r\n", 2);
    InetFree(verb);
    InetFree(version);
    AcquireSRWLockShared(&request->Lock);
    for (DWORD i = 0; i < request->Request.Count; i++) {
        PutString(out, request->Request.Items[i].Name);
        Put(out, ": ", 2);
        PutString(out, request->Request.Items[i].Value);
        Put(out, "\r\n", 2);
    }
    ReleaseSRWLockShared(&request->Lock);
    Put(out, "\r\n", 2);
    return out->Failed ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
}

/* connects, sends the head and the body kept for the request; a pooled connection that failed is retried. */
static DWORD Transmit(INET_OBJECT *request)
{
    for (DWORD attempt = 0; attempt < 2; attempt++) {
        DropConnection(request);
        DWORD error = Connect(request, attempt == 0);
        if (error)
            return Cancelled(request, error);
        BOOL via_proxy = request->Conn->Proxy[0] != 0;
        request->ViaProxy = via_proxy;
        INET_OUT out = {0};
        error = AddDefaultHeaders(request, via_proxy);
        if (!error)
            error = BuildRequest(request, via_proxy, &out);
        if (!error && request->OptionalLength)
            Put(&out, request->Optional, request->OptionalLength);
        if (!error && out.Failed)
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (error) {
            InetFree(out.Data);
            return error;
        }
        InetNotify(request, INTERNET_STATUS_SENDING_REQUEST, nullptr, 0);
        error = InetConnSend(request->Conn, out.Data, out.Length, SendTimeout(request));
        DWORD sent = out.Length;
        InetFree(out.Data);
        if (error && request->ConnReused && !request->Closed)
            continue;
        if (error)
            return Cancelled(request, error);
        InetNotify(request, INTERNET_STATUS_REQUEST_SENT, &sent, sizeof(sent));
        request->Written = request->OptionalLength;
        request->Sent = TRUE;
        return ERROR_SUCCESS;
    }
    return ERROR_INTERNET_CONNECTION_ABORTED;
}

/* ------------------------------------------------------------------ responses */

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

static void ResetResponse(INET_OBJECT *request)
{
    AcquireSRWLockExclusive(&request->Lock);
    InetHeadersClear(&request->Response);
    InetFree(request->StatusLine);
    InetFree(request->StatusText);
    InetFree(request->VersionText);
    InetFree(request->Memory);
    request->StatusLine = request->StatusText = request->VersionText = nullptr;
    request->Memory = nullptr;
    request->MemorySize = request->MemoryOffset = 0;
    request->StatusCode = 0;
    request->BodyMode = INET_BODY_NONE;
    request->BodyLeft = 0;
    request->BodyDone = FALSE;
    request->KeepAlive = FALSE;
    request->Position = 0;
    ReleaseSRWLockExclusive(&request->Lock);
}

/* the status line and headers of one response. */
static DWORD ReadHead(INET_OBJECT *request)
{
    INET_CONN *conn = request->Conn;
    char *line = nullptr;
    DWORD total = 0;
    InetNotify(request, INTERNET_STATUS_RECEIVING_RESPONSE, nullptr, 0);
    DWORD error = InetConnReadLine(conn, &line, INET_HEAD_LIMIT, ReceiveTimeout(request), &total);
    if (error == ERROR_HTTP_INVALID_SERVER_RESPONSE && !total && conn->Eof)
        return ERROR_INTERNET_CONNECTION_RESET;
    if (error)
        return error;
    const char *p = line;
    if (_strnicmp(p, "HTTP/", 5)) {
        InetFree(line);
        return ERROR_HTTP_INVALID_SERVER_RESPONSE;
    }
    const char *version_end = strchr(p, ' ');
    if (!version_end)
        version_end = p + strlen(p);
    p = version_end;
    while (*p == ' ')
        p++;
    DWORD code = 0;
    const char *digits = p;
    while (*p >= '0' && *p <= '9')
        code = code * 10 + (DWORD)(*p++ - '0');
    if (p - digits != 3) {
        InetFree(line);
        return ERROR_HTTP_INVALID_SERVER_RESPONSE;
    }
    while (*p == ' ')
        p++;
    AcquireSRWLockExclusive(&request->Lock);
    request->StatusCode = code;
    request->StatusText = *p ? InetDuplicateA(p, (DWORD)-1) : nullptr;
    request->VersionText = InetDuplicateA(line, (DWORD)(version_end - line));
    request->StatusLine = line;
    ReleaseSRWLockExclusive(&request->Lock);
    if (!request->VersionText)
        return ERROR_NOT_ENOUGH_MEMORY;
    while (TRUE) {
        error = InetConnReadLine(conn, &line, INET_HEAD_LIMIT, ReceiveTimeout(request), &total);
        if (error)
            return error;
        if (total > INET_HEAD_LIMIT * 4) {
            InetFree(line);
            return ERROR_HTTP_INVALID_SERVER_RESPONSE;
        }
        if (!*line) {
            InetFree(line);
            break;
        }
        AcquireSRWLockExclusive(&request->Lock);
        if ((*line == ' ' || *line == '\t') && request->Response.Count) {
            /* a folded line continues the previous value. */
            INET_HEADER *last = &request->Response.Items[request->Response.Count - 1];
            char *start = line;
            while (*start == ' ' || *start == '\t')
                start++;
            SIZE_T old = strlen(last->Value);
            char *value = InetAlloc(old + strlen(start) + 2);
            if (value) {
                memcpy(value, last->Value, old);
                value[old] = ' ';
                lstrcpyA(value + old + 1, start);
                InetFree(last->Value);
                last->Value = value;
            }
        } else {
            char *colon = strchr(line, ':');
            if (colon && colon != line) {
                DWORD name_length = (DWORD)(colon - line);
                while (name_length && (line[name_length - 1] == ' ' || line[name_length - 1] == '\t'))
                    name_length--;
                char *value = colon + 1;
                while (*value == ' ' || *value == '\t')
                    value++;
                DWORD value_length = (DWORD)strlen(value);
                while (value_length && (value[value_length - 1] == ' ' || value[value_length - 1] == '\t'))
                    value_length--;
                if (!InetHeadersAppend(&request->Response, line, name_length, value, value_length))
                    error = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        ReleaseSRWLockExclusive(&request->Lock);
        InetFree(line);
        if (error)
            return error;
    }
    InetNotify(request, INTERNET_STATUS_RESPONSE_RECEIVED, &total, sizeof(total));
    return ERROR_SUCCESS;
}

static void SetupBody(INET_OBJECT *request)
{
    const char *connection = InetHeadersFind(&request->Response, "Connection", 0);
    if (!connection)
        connection = InetHeadersFind(&request->Response, "Proxy-Connection", 0);
    BOOL http11 = lstrcmpiA(request->VersionText, "HTTP/1.0") && lstrcmpiA(request->VersionText, "HTTP/0.9");
    if (http11)
        request->KeepAlive = !connection || !ValueHas(connection, "close");
    else
        request->KeepAlive = connection && ValueHas(connection, "keep-alive");
    const char *encoding = InetHeadersFind(&request->Response, "Transfer-Encoding", 0);
    const char *length = InetHeadersFind(&request->Response, "Content-Length", 0);
    DWORD code = request->StatusCode;
    if (VerbIs(request, L"HEAD") || code == 204 || code == 304 || (code >= 100 && code < 200)) {
        request->BodyMode = INET_BODY_NONE;
        request->BodyDone = TRUE;
    } else if (encoding && ValueHas(encoding, "chunked")) {
        request->BodyMode = INET_BODY_CHUNKED;
    } else if (length) {
        ULONGLONG value = 0;
        for (const char *p = length; *p >= '0' && *p <= '9'; p++)
            value = value * 10 + (ULONGLONG)(*p - '0');
        request->BodyMode = INET_BODY_LENGTH;
        request->BodyLeft = value;
        request->BodyDone = !value;
    } else {
        request->BodyMode = INET_BODY_CLOSE;
        request->KeepAlive = FALSE;
    }
}

/* the next chunk size, consuming the trailers after the last chunk. */
static DWORD NextChunk(INET_OBJECT *request)
{
    INET_CONN *conn = request->Conn;
    char *line;
    DWORD error = InetConnReadLine(conn, &line, 4096, ReceiveTimeout(request), nullptr);
    if (error)
        return error;
    ULONGLONG size = 0;
    const char *p = line;
    BOOL digits = FALSE;
    while (TRUE) {
        int v = *p >= '0' && *p <= '9'                         ? *p - '0'
                : (*p | 0x20) >= 'a' && (*p | 0x20) <= 'f' ? (*p | 0x20) - 'a' + 10
                                                               : -1;
        if (v < 0)
            break;
        size = size * 16 + (ULONGLONG)v;
        digits = TRUE;
        p++;
    }
    InetFree(line);
    if (!digits)
        return ERROR_HTTP_INVALID_SERVER_RESPONSE;
    if (size) {
        request->BodyLeft = size;
        return ERROR_SUCCESS;
    }
    while (TRUE) {
        error = InetConnReadLine(conn, &line, INET_HEAD_LIMIT, ReceiveTimeout(request), nullptr);
        if (error)
            return error;
        BOOL end = !*line;
        InetFree(line);
        if (end)
            break;
    }
    request->BodyDone = TRUE;
    return ERROR_SUCCESS;
}

/* how much of the body is buffered now; with wait, fills once when none is. */
static DWORD Available(INET_OBJECT *request, BOOL wait, DWORD *available)
{
    *available = 0;
    if (request->BodyMode == INET_BODY_MEMORY) {
        *available = request->MemorySize - request->MemoryOffset;
        return ERROR_SUCCESS;
    }
    while (!request->BodyDone) {
        INET_CONN *conn = request->Conn;
        if (!conn)
            return ERROR_INTERNET_CONNECTION_ABORTED;
        if (request->BodyMode == INET_BODY_CHUNKED && !request->BodyLeft) {
            if (!wait && !InetConnBuffered(conn))
                return ERROR_SUCCESS;
            DWORD error = NextChunk(request);
            if (error)
                return Cancelled(request, error);
            continue;
        }
        DWORD buffered = InetConnBuffered(conn);
        if (buffered) {
            if (request->BodyMode != INET_BODY_CLOSE && buffered > request->BodyLeft)
                buffered = (DWORD)request->BodyLeft;
            *available = buffered;
            return ERROR_SUCCESS;
        }
        if (conn->Eof) {
            if (request->BodyMode != INET_BODY_CLOSE)
                return ERROR_INTERNET_CONNECTION_ABORTED;
            request->BodyDone = TRUE;
            break;
        }
        if (!wait)
            return ERROR_SUCCESS;
        InetNotify(request, INTERNET_STATUS_RECEIVING_RESPONSE, nullptr, 0);
        DWORD before = InetConnBuffered(conn);
        DWORD error = InetConnFill(conn, ReceiveTimeout(request));
        if (error)
            return Cancelled(request, error);
        DWORD received = InetConnBuffered(conn) - before;
        InetNotify(request, INTERNET_STATUS_RESPONSE_RECEIVED, &received, sizeof(received));
    }
    if (request->Conn && request->BodyMode != INET_BODY_MEMORY)
        FinishConnection(request);
    return ERROR_SUCCESS;
}

/* copies body bytes out of the connection buffer. */
static DWORD Consume(INET_OBJECT *request, BYTE *buffer, DWORD size)
{
    if (request->BodyMode == INET_BODY_MEMORY) {
        DWORD count = request->MemorySize - request->MemoryOffset;
        if (count > size)
            count = size;
        memcpy(buffer, request->Memory + request->MemoryOffset, count);
        request->MemoryOffset += count;
        return count;
    }
    INET_CONN *conn = request->Conn;
    DWORD count = InetConnBuffered(conn);
    if (count > size)
        count = size;
    if (request->BodyMode != INET_BODY_CLOSE && count > request->BodyLeft)
        count = (DWORD)request->BodyLeft;
    memcpy(buffer, conn->Buffer + conn->Start, count);
    conn->Start += count;
    if (request->BodyMode != INET_BODY_CLOSE)
        request->BodyLeft -= count;
    return count;
}

/* the crlf closing a chunk, and the end of a sized body. */
static DWORD AfterConsume(INET_OBJECT *request)
{
    if (request->BodyMode == INET_BODY_LENGTH && !request->BodyLeft) {
        request->BodyDone = TRUE;
        FinishConnection(request);
    } else if (request->BodyMode == INET_BODY_CHUNKED && !request->BodyLeft && request->Conn) {
        char *line;
        DWORD error = InetConnReadLine(request->Conn, &line, 16, ReceiveTimeout(request), nullptr);
        if (error)
            return Cancelled(request, error);
        InetFree(line);
    }
    return ERROR_SUCCESS;
}

/* reads into the buffer: until it is full or the body ends, or with partial only what has arrived. */
static DWORD ReadBody(INET_OBJECT *request, void *buffer, DWORD size, DWORD *read, BOOL partial)
{
    *read = 0;
    BYTE *out = buffer;
    while (*read < size) {
        DWORD available;
        DWORD error = Available(request, !partial || *read == 0, &available);
        if (error)
            return error;
        if (!available)
            break;
        *read += Consume(request, out + *read, size - *read);
        if ((error = AfterConsume(request)))
            return error;
    }
    request->Position += *read;
    if (request->BodyDone && request->Conn && request->BodyMode != INET_BODY_MEMORY)
        FinishConnection(request);
    return ERROR_SUCCESS;
}

DWORD InetRead(INET_OBJECT *request, void *buffer, DWORD size, DWORD *read)
{
    *read = 0;
    if (request->State != INET_RECEIVED)
        return ERROR_INTERNET_INCORRECT_HANDLE_STATE;
    return ReadBody(request, buffer, size, read, FALSE);
}

DWORD InetQueryAvailable(INET_OBJECT *request, BOOL wait, DWORD *available)
{
    *available = 0;
    if (request->State != INET_RECEIVED)
        return ERROR_INTERNET_INCORRECT_HANDLE_STATE;
    return Available(request, wait, available);
}

BOOL InetDataReady(INET_OBJECT *request)
{
    if (request->State != INET_RECEIVED || request->BodyDone || request->BodyMode == INET_BODY_MEMORY)
        return TRUE;
    DWORD available = 0;
    return !Available(request, FALSE, &available) && (available || request->BodyDone);
}

/* ------------------------------------------------------------------ decoding */

static void *InflateAlloc(void *context, size_t size)
{
    return InetAlloc(size);
}

static void InflateFree(void *context, void *memory)
{
    InetFree(memory);
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

/* reads the whole encoded body and keeps it decoded in memory. */
static DWORD Decode(INET_OBJECT *request, BOOL gzip)
{
    BYTE *raw = nullptr;
    DWORD size = 0, capacity = 0;
    while (TRUE) {
        if (capacity - size < 65536) {
            capacity = capacity ? capacity * 2 : 131072;
            BYTE *grown = InetRealloc(raw, capacity);
            if (!grown) {
                InetFree(raw);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            raw = grown;
        }
        DWORD got;
        DWORD error = ReadBody(request, raw + size, capacity - size, &got, TRUE);
        if (error) {
            InetFree(raw);
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
    InetFree(raw);
    if (result != MIME_INFLATE_OK)
        return ERROR_INTERNET_DECODING_FAILED;
    request->BodyMode = INET_BODY_MEMORY;
    request->Memory = plain;
    request->MemorySize = (DWORD)plain_size;
    request->MemoryOffset = 0;
    request->BodyDone = TRUE;
    request->Position = 0;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ redirects */

/* removes the dot segments of a path, rfc 3986 5.2.4, in place. */
static void RemoveDots(PWSTR path)
{
    PWSTR copy = InetDuplicate(path);
    if (!copy)
        return;
    LPCWSTR in = copy;
    PWSTR out = path;
    while (*in) {
        if (!wcsncmp(in, L"../", 3)) {
            in += 3;
        } else if (!wcsncmp(in, L"./", 2) || !wcsncmp(in, L"/./", 3)) {
            in += 2;
        } else if (!lstrcmpW(in, L"/.")) {
            *out++ = '/';
            break;
        } else if (!wcsncmp(in, L"/../", 4) || !lstrcmpW(in, L"/..")) {
            BOOL last = !in[3];
            in += 3;
            while (out > path && *--out != '/')
                ;
            if (last) {
                *out++ = '/';
                break;
            }
        } else if (!lstrcmpW(in, L".") || !lstrcmpW(in, L"..")) {
            break;
        } else {
            do
                *out++ = *in++;
            while (*in && *in != '/');
        }
    }
    *out = 0;
    InetFree(copy);
}

/* the absolute url of a Location value, a relative one resolved against the request after rfc 3986. */
static PWSTR ResolveLocation(INET_OBJECT *request, const char *location)
{
    PWSTR value = InetWiden(location, -1);
    if (!value)
        return nullptr;
    LPCWSTR p = value;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#')
        p++;
    if (*p == ':' && p != value)
        return value;
    PWSTR base = InetRequestUrl(request);
    if (!base) {
        InetFree(value);
        return nullptr;
    }
    /* the scheme and authority of the base end at the third slash. */
    PWSTR authority_end = wcschr(wcschr(base, ':') + 3, '/');
    SIZE_T size = (SIZE_T)lstrlenW(base) + lstrlenW(value) + 4;
    PWSTR url = InetAlloc(size * sizeof(WCHAR));
    if (!url) {
        InetFree(value);
        InetFree(base);
        return nullptr;
    }
    if (value[0] == '/' && value[1] == '/') {
        lstrcpynW(url, base, (int)(wcschr(base, ':') - base + 2));
        lstrcatW(url, value);
    } else {
        PWSTR path = authority_end ? authority_end : base + lstrlenW(base);
        lstrcpynW(url, base, (int)(path - base + 1));
        PWSTR merged = InetAlloc(size * sizeof(WCHAR));
        if (!merged) {
            InetFree(url);
            url = nullptr;
        } else {
            PWSTR query = wcschr(path, '?');
            if (value[0] == '/') {
                lstrcpyW(merged, value);
            } else if (!value[0] || value[0] == '#') {
                lstrcpyW(merged, *path ? path : L"/");
            } else if (value[0] == '?') {
                lstrcpynW(merged, *path ? path : L"/", query ? (int)(query - path + 1) : (int)size);
                lstrcatW(merged, value);
            } else {
                lstrcpynW(merged, *path ? path : L"/", query ? (int)(query - path + 1) : (int)size);
                PWSTR slash = wcsrchr(merged, '/');
                if (slash)
                    slash[1] = 0;
                else
                    lstrcpyW(merged, L"/");
                lstrcatW(merged, value);
            }
            PWSTR rest = merged;
            while (*rest && *rest != '?' && *rest != '#')
                rest++;
            PWSTR tail = InetDuplicate(rest);
            *rest = 0;
            RemoveDots(merged);
            lstrcatW(url, merged);
            if (tail)
                lstrcatW(url, tail);
            InetFree(tail);
            InetFree(merged);
        }
    }
    InetFree(value);
    InetFree(base);
    return url;
}

static BOOL WarnOnZoneCrossing(void)
{
    HKEY key;
    DWORD value = 0, size = sizeof(value), type;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0,
                       KEY_READ, &key)) {
        if (RegQueryValueExW(key, L"WarnonZoneCrossing", nullptr, &type, (BYTE *)&value, &size) || type != REG_DWORD)
            value = 0;
        RegCloseKey(key);
    }
    return value != 0;
}

/* points the request at a new url. */
static DWORD ApplyRedirect(INET_OBJECT *request, LPCWSTR url)
{
    URL_COMPONENTSW uc = {sizeof(uc)};
    uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = 1;
    if (!InternetCrackUrlW(url, 0, 0, &uc))
        return ERROR_HTTP_REDIRECT_FAILED;
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)
        return ERROR_HTTP_REDIRECT_FAILED;
    BOOL secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    if (secure != request->Secure && WarnOnZoneCrossing()) {
        DWORD ignore = secure ? INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS : INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP;
        if (!((request->Flags | request->SecurityFlags) & ignore))
            return secure ? ERROR_INTERNET_HTTP_TO_HTTPS_ON_REDIR : ERROR_INTERNET_HTTPS_TO_HTTP_ON_REDIR;
    }
    PWSTR host = InetDuplicateN(uc.lpszHostName, uc.dwHostNameLength);
    DWORD extra = uc.dwExtraInfoLength;
    for (DWORD i = 0; i < extra; i++)
        if (uc.lpszExtraInfo[i] == '#') {
            extra = i;
            break;
        }
    PWSTR object = InetAlloc(((SIZE_T)uc.dwUrlPathLength + extra + 2) * sizeof(WCHAR));
    if (!host || !object) {
        InetFree(host);
        InetFree(object);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD length = uc.dwUrlPathLength;
    if (length)
        memcpy(object, uc.lpszUrlPath, length * sizeof(WCHAR));
    else
        object[length++] = '/';
    memcpy(object + length, uc.lpszExtraInfo, extra * sizeof(WCHAR));
    object[length + extra] = 0;
    BOOL same_host = !lstrcmpiW(host, request->Host) && uc.nPort == request->HostPort;
    AcquireSRWLockExclusive(&request->Lock);
    InetFree(request->Host);
    InetFree(request->Object);
    request->Host = host;
    request->Object = object;
    request->HostPort = uc.nPort;
    if (secure != request->Secure) {
        request->Flags = secure ? request->Flags | INTERNET_FLAG_SECURE : request->Flags & ~INTERNET_FLAG_SECURE;
        if (!secure)
            InetHeadersRemove(&request->Request, "Referer");
    }
    request->Secure = secure;
    InetHeadersRemove(&request->Request, "Host");
    InetHeadersRemove(&request->Request, "If-Modified-Since");
    if (!same_host) {
        InetHeadersRemove(&request->Request, "Authorization");
        request->AuthTried = FALSE;
    }
    ReleaseSRWLockExclusive(&request->Lock);
    /* the credentials of the connection belong to its server. */
    if (!same_host) {
        InetFree(request->User);
        InetFree(request->Password);
        request->User = request->Password = nullptr;
    }
    return ERROR_SUCCESS;
}

/* drains what is left of a body so the connection can carry the next request. */
static void Drain(INET_OBJECT *request)
{
    BYTE scratch[4096];
    DWORD drained = 0;
    while (!request->BodyDone && request->BodyMode != INET_BODY_CLOSE && drained < 1024 * 1024) {
        DWORD got;
        if (ReadBody(request, scratch, sizeof(scratch), &got, TRUE) || !got)
            break;
        drained += got;
    }
    if (!request->BodyDone)
        request->KeepAlive = FALSE;
    FinishConnection(request);
}

/* follows a redirect as wininet does, *followed telling whether a new request went out. */
static DWORD Redirect(INET_OBJECT *request, BOOL *followed)
{
    *followed = FALSE;
    DWORD code = request->StatusCode;
    if (code != 300 && code != 301 && code != 302 && code != 303 && code != 307 && code != 308)
        return ERROR_SUCCESS;
    if (request->Flags & INTERNET_FLAG_NO_AUTO_REDIRECT)
        return ERROR_SUCCESS;
    const char *location = InetHeadersFind(&request->Response, "Location", 0);
    if (!location || !*location)
        return ERROR_SUCCESS;
    /* 303 makes any request a GET, 300 to 302 a POST; 307 and 308 keep the verb. */
    BOOL get = code == 303 || (code <= 302 && VerbIs(request, L"POST"));
    BOOL safe = get || VerbIs(request, L"GET") || VerbIs(request, L"HEAD");
    if (!safe || (request->SendEx && !get))
        return ERROR_HTTP_REDIRECT_NEEDS_CONFIRMATION;
    if (request->Redirects++ >= INET_MAX_REDIRECTS)
        return ERROR_HTTP_REDIRECT_FAILED;
    PWSTR url = ResolveLocation(request, location);
    if (!url)
        return ERROR_NOT_ENOUGH_MEMORY;
    Drain(request);
    request->State = INET_SENDING;
    InetNotifyString(request, INTERNET_STATUS_REDIRECT, url);
    DWORD error = request->Closed ? ERROR_INTERNET_OPERATION_CANCELLED : ApplyRedirect(request, url);
    InetFree(url);
    if (error)
        return error;
    if (get && !VerbIs(request, L"GET")) {
        PWSTR verb = InetDuplicate(L"GET");
        if (!verb)
            return ERROR_NOT_ENOUGH_MEMORY;
        AcquireSRWLockExclusive(&request->Lock);
        InetFree(request->Verb);
        request->Verb = verb;
        InetHeadersRemove(&request->Request, "Content-Length");
        InetHeadersRemove(&request->Request, "Content-Type");
        ReleaseSRWLockExclusive(&request->Lock);
        InetFree(request->Optional);
        request->Optional = nullptr;
        request->OptionalLength = 0;
        request->TotalLength = 0;
        request->SendEx = FALSE;
    }
    ResetResponse(request);
    if ((error = Transmit(request)))
        return error;
    *followed = TRUE;
    return ERROR_SUCCESS;
}

/* a basic challenge the request has credentials for: the request goes out again with them. */
static DWORD Authenticate(INET_OBJECT *request, BOOL *retried)
{
    *retried = FALSE;
    BOOL proxy = request->StatusCode == 407;
    if ((request->StatusCode != 401 && !proxy) || request->Flags & INTERNET_FLAG_NO_AUTH || request->SendEx)
        return ERROR_SUCCESS;
    LPCWSTR user = proxy ? request->ProxyUser : request->User;
    if (!user || !*user || (proxy ? request->ProxyAuthTried : request->AuthTried))
        return ERROR_SUCCESS;
    BOOL basic = FALSE;
    const char *value;
    for (DWORD i = 0; (value = InetHeadersFind(&request->Response, proxy ? "Proxy-Authenticate" : "WWW-Authenticate",
                                                i));
         i++)
        if (!_strnicmp(value, "Basic", 5) && (!value[5] || value[5] == ' '))
            basic = TRUE;
    if (!basic)
        return ERROR_SUCCESS;
    if (proxy)
        request->ProxyAuthTried = TRUE;
    else
        request->AuthTried = TRUE;
    Drain(request);
    request->State = INET_SENDING;
    ResetResponse(request);
    DWORD error = Transmit(request);
    if (!error)
        *retried = TRUE;
    return error;
}

static void StoreCookies(INET_OBJECT *request)
{
    if (request->Flags & INTERNET_FLAG_NO_COOKIES)
        return;
    const char *value;
    for (DWORD i = 0; (value = InetHeadersFind(&request->Response, "Set-Cookie", i)); i++) {
        InetCookieStore(request->Host, ObjectPath(request), value);
        DWORD state = COOKIE_STATE_ACCEPT;
        InetNotify(request, INTERNET_STATUS_COOKIE_RECEIVED, &state, sizeof(state));
    }
}

/* reads the response to what went out, following redirects and challenges. */
static DWORD ReceiveResponse(INET_OBJECT *request)
{
    DWORD error;
    while (TRUE) {
        ResetResponse(request);
        error = ReadHead(request);
        /* a pooled connection the server had closed: the request goes out again once. */
        if ((error == ERROR_INTERNET_CONNECTION_RESET || error == ERROR_INTERNET_CONNECTION_ABORTED) &&
            request->ConnReused && !request->Closed && !request->SendEx) {
            request->ConnReused = FALSE;
            if (!(error = Transmit(request)))
                continue;
        }
        if (error)
            return Cancelled(request, error);
        if (request->StatusCode >= 100 && request->StatusCode < 200 && request->StatusCode != 101) {
            DWORD code = request->StatusCode;
            InetNotify(request, INTERNET_STATUS_INTERMEDIATE_RESPONSE, &code, sizeof(code));
            continue;
        }
        SetupBody(request);
        request->State = INET_RECEIVED;
        StoreCookies(request);
        BOOL again;
        if ((error = Redirect(request, &again)) || (!again && (error = Authenticate(request, &again))))
            return Cancelled(request, error);
        if (again) {
            request->State = INET_SENDING;
            continue;
        }
        break;
    }
    const char *encoding = InetHeadersFind(&request->Response, "Content-Encoding", 0);
    if (encoding && request->Decoding) {
        BOOL gzip = ValueHas(encoding, "gzip") || ValueHas(encoding, "x-gzip");
        BOOL deflate = ValueHas(encoding, "deflate");
        if ((gzip || deflate) && (error = Decode(request, gzip)))
            return Cancelled(request, error);
    }
    if (request->BodyDone && request->Conn && request->BodyMode != INET_BODY_MEMORY)
        FinishConnection(request);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ the operations */

DWORD InetPrepareSend(INET_OBJECT *request, LPCWSTR headers, DWORD headers_length, const void *optional,
                      DWORD optional_length, DWORD total_length, BOOL ex)
{
    if (request->Busy)
        return ERROR_INTERNET_INCORRECT_HANDLE_STATE;
    if (headers && headers_length && *headers) {
        if (headers_length == (DWORD)-1)
            headers_length = (DWORD)lstrlenW(headers);
        char *text = InetNarrow(headers, (int)headers_length);
        if (!text)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD error = InetAddRequestHeaders(request, text, (DWORD)strlen(text),
                                            request->Sent ? HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE : 0);
        InetFree(text);
        if (error)
            return error;
    }
    if (!optional)
        optional_length = 0;
    BYTE *copy = optional_length ? InetAlloc(optional_length) : nullptr;
    if (optional_length && !copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (copy)
        memcpy(copy, optional, optional_length);
    InetFree(request->Optional);
    request->Optional = copy;
    request->OptionalLength = optional_length;
    request->TotalLength = total_length > optional_length ? total_length : 0;
    request->SendEx = ex;
    request->Redirects = 0;
    request->AuthTried = request->ProxyAuthTried = FALSE;
    request->Written = 0;
    /* a request sent again starts over. */
    DropConnection(request);
    ResetResponse(request);
    request->State = INET_OPENED;
    return ERROR_SUCCESS;
}

static DWORD Send(INET_OBJECT *request)
{
    DWORD error = Transmit(request);
    if (error)
        return error;
    request->State = INET_SENDING;
    if (request->SendEx)
        return ERROR_SUCCESS;
    return ReceiveResponse(request);
}

DWORD InetEndRequest(INET_OBJECT *request)
{
    if (request->State != INET_SENDING || !request->SendEx || !request->Conn)
        return ERROR_INTERNET_INCORRECT_HANDLE_STATE;
    DWORD error = ReceiveResponse(request);
    request->SendEx = FALSE;
    return error;
}

DWORD InetWrite(INET_OBJECT *request, const void *buffer, DWORD size, DWORD *written)
{
    *written = 0;
    if (request->State != INET_SENDING || !request->SendEx || !request->Conn)
        return ERROR_INTERNET_INCORRECT_HANDLE_STATE;
    DWORD error = InetConnSend(request->Conn, buffer, size, INET_TIMEOUT(request, INTERNET_OPTION_DATA_SEND_TIMEOUT));
    if (error)
        return Cancelled(request, error);
    request->Written += size;
    *written = size;
    return ERROR_SUCCESS;
}

DWORD InetRunOp(INET_OBJECT *request, INET_OP *op)
{
    DWORD count = 0, error;
    op->Result = TRUE;
    if (request->Closed)
        return ERROR_INTERNET_OPERATION_CANCELLED;
    switch (op->Kind) {
    case INET_OP_SEND:
    case INET_OP_OPEN_URL:
        error = Send(request);
        if (op->Kind == INET_OP_OPEN_URL)
            op->Result = error ? 0 : (DWORD_PTR)request->Handle;
        /* a response that came, as a redirect needing confirmation, stays readable. */
        if (error && request->State != INET_RECEIVED)
            request->State = INET_OPENED;
        return error;
    case INET_OP_END:
        return InetEndRequest(request);
    case INET_OP_READ:
    case INET_OP_READ_EX:
        if (request->State != INET_RECEIVED)
            return ERROR_INTERNET_INCORRECT_HANDLE_STATE;
        error = ReadBody(request, op->Buffer, op->Size, &count, op->Kind == INET_OP_READ_EX || op->Flags);
        if (op->Count)
            *op->Count = count;
        if (op->Buffers)
            ((INTERNET_BUFFERSW *)op->Buffers)->dwBufferLength = count;
        return error;
    case INET_OP_QUERY:
        error = InetQueryAvailable(request, TRUE, &count);
        if (op->Count)
            *op->Count = count;
        return error;
    case INET_OP_WRITE:
        error = InetWrite(request, op->Buffer, op->Size, &count);
        if (op->Count)
            *op->Count = count;
        return error;
    }
    return ERROR_INVALID_PARAMETER;
}

static void Complete(INET_OBJECT *request, INET_OP *op, DWORD error)
{
    INTERNET_ASYNC_RESULT result = {error ? 0 : op->Result, error};
    DWORD_PTR saved = request->Context;
    request->Context = op->Context ? op->Context : saved;
    InetNotify(request, INTERNET_STATUS_REQUEST_COMPLETE, &result, sizeof(result));
    request->Context = saved;
}

static DWORD WINAPI Worker(LPVOID parameter)
{
    INET_OBJECT *request = parameter;
    while (TRUE) {
        AcquireSRWLockExclusive(&request->Lock);
        BOOL has = request->HasOp, closed = request->Closed;
        INET_OP op = request->Op;
        request->HasOp = FALSE;
        request->Op.Headers = nullptr;
        ReleaseSRWLockExclusive(&request->Lock);
        if (has) {
            DWORD error = InetRunOp(request, &op);
            if (request->Closed && error)
                error = ERROR_INTERNET_OPERATION_CANCELLED;
            InetFree(op.Headers);
            if (op.Kind == INET_OP_OPEN_URL && error)
                InternetCloseHandle(request->Handle);
            AcquireSRWLockExclusive(&request->Lock);
            request->Busy = FALSE;
            BOOL deferred = request->CloseDeferred;
            request->CloseDeferred = FALSE;
            ReleaseSRWLockExclusive(&request->Lock);
            Complete(request, &op, error);
            if (deferred) {
                HINTERNET closing = request->Handle;
                InetNotify(request, INTERNET_STATUS_HANDLE_CLOSING, &closing, sizeof(closing));
            }
            continue;
        }
        if (closed)
            break;
        WaitForSingleObject(request->Wake, INFINITE);
    }
    InetRelease(request);
    return 0;
}

BOOL InetStart(INET_OBJECT *request, INET_OP *op)
{
    if (!InetIsAsync(request, op->Context ? op->Context : request->Context)) {
        DWORD error = InetRunOp(request, op);
        SetLastError(error);
        return !error;
    }
    AcquireSRWLockExclusive(&request->Lock);
    if (request->Busy) {
        ReleaseSRWLockExclusive(&request->Lock);
        return InetFail(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
    }
    if (!request->Worker) {
        request->Wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        InetAddRef(request);
        request->Worker = request->Wake ? CreateThread(nullptr, 0, Worker, request, 0, nullptr) : nullptr;
        if (!request->Worker) {
            ReleaseSRWLockExclusive(&request->Lock);
            InetRelease(request);
            return InetFail(ERROR_INTERNET_ASYNC_THREAD_FAILED);
        }
    }
    request->Busy = TRUE;
    request->HasOp = TRUE;
    request->Op = *op;
    SetEvent(request->Wake);
    ReleaseSRWLockExclusive(&request->Lock);
    return InetFail(ERROR_IO_PENDING);
}

/* ------------------------------------------------------------------ the api */

static INET_OBJECT *ReferenceRequest(HINTERNET handle)
{
    return InetReferenceType(handle, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
}

DECLSPEC_EXPORT BOOL WINAPI HttpSendRequestW(HINTERNET handle, LPCWSTR headers, DWORD headers_length, LPVOID optional,
                                            DWORD optional_length)
{
    INET_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    DWORD error = InetPrepareSend(request, headers, headers_length, optional, optional_length, 0, FALSE);
    BOOL ok;
    if (error) {
        ok = InetFail(error);
    } else {
        INET_OP op = {INET_OP_SEND};
        ok = InetStart(request, &op);
    }
    InetRelease(request);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI HttpSendRequestExW(HINTERNET handle, LPINTERNET_BUFFERSW buffers_in,
                                              LPINTERNET_BUFFERSW buffers_out, DWORD flags, DWORD_PTR context)
{
    if (buffers_out)
        return InetFail(ERROR_INVALID_PARAMETER);
    INET_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    LPCWSTR headers = nullptr;
    DWORD headers_length = 0, total = 0, length = 0;
    void *data = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (buffers_in) {
        if (buffers_in->dwStructSize != sizeof(INTERNET_BUFFERSW))
            error = ERROR_INVALID_PARAMETER;
        headers = buffers_in->lpcszHeader;
        headers_length = buffers_in->dwHeadersLength;
        data = buffers_in->lpvBuffer;
        length = buffers_in->dwBufferLength;
        total = buffers_in->dwBufferTotal;
    }
    if (!error)
        error = InetPrepareSend(request, headers, headers_length, data, length, total, TRUE);
    BOOL ok;
    if (error) {
        ok = InetFail(error);
    } else {
        INET_OP op = {INET_OP_SEND, context};
        ok = InetStart(request, &op);
    }
    InetRelease(request);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI HttpEndRequestW(HINTERNET handle, LPINTERNET_BUFFERSW buffers_out, DWORD flags,
                                           DWORD_PTR context)
{
    if (buffers_out)
        return InetFail(ERROR_INVALID_PARAMETER);
    INET_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return FALSE;
    INET_OP op = {INET_OP_END, context};
    BOOL ok = InetStart(request, &op);
    InetRelease(request);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetWriteFile(HINTERNET file, LPCVOID buffer, DWORD length, LPDWORD written)
{
    if (!written)
        return InetFail(ERROR_INVALID_PARAMETER);
    *written = 0;
    INET_OBJECT *request = ReferenceRequest(file);
    if (!request)
        return FALSE;
    INET_OP op = {INET_OP_WRITE};
    op.Buffer = (void *)buffer;
    op.Size = length;
    op.Count = written;
    BOOL ok = InetStart(request, &op);
    InetRelease(request);
    return ok;
}

/* a read completes at once with what has arrived when the request is asynchronous and data is there. */
static BOOL Read(HINTERNET file, INET_OP *op)
{
    INET_OBJECT *request = ReferenceRequest(file);
    if (!request)
        return FALSE;
    BOOL ok;
    if (request->Busy) {
        ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
    } else if (InetIsAsync(request, op->Context ? op->Context : request->Context) && InetDataReady(request)) {
        op->Flags = TRUE;
        DWORD error = InetRunOp(request, op);
        SetLastError(error);
        ok = !error;
    } else {
        ok = InetStart(request, op);
    }
    InetRelease(request);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetReadFile(HINTERNET file, LPVOID buffer, DWORD length, LPDWORD read)
{
    if (!read)
        return InetFail(ERROR_INVALID_PARAMETER);
    *read = 0;
    if (!buffer && length)
        return InetFail(ERROR_INVALID_PARAMETER);
    INET_OP op = {INET_OP_READ};
    op.Buffer = buffer;
    op.Size = length;
    op.Count = read;
    return Read(file, &op);
}

/* InternetReadFileEx reads into the first buffer and sets its length; IRF_NO_WAIT takes what has arrived. */
static BOOL ReadEx(HINTERNET file, void *buffers, void *data, LPDWORD length, DWORD flags, DWORD_PTR context)
{
    INET_OP op = {INET_OP_READ_EX, context};
    op.Buffer = data;
    op.Size = *length;
    op.Buffers = buffers;
    op.Flags = flags & IRF_NO_WAIT;
    if (!(flags & IRF_NO_WAIT))
        op.Kind = INET_OP_READ;
    op.Count = length;
    return Read(file, &op);
}

DECLSPEC_EXPORT BOOL WINAPI InternetReadFileExW(HINTERNET file, LPINTERNET_BUFFERSW buffers, DWORD flags,
                                               DWORD_PTR context)
{
    if (!buffers || buffers->dwStructSize != sizeof(*buffers))
        return InetFail(ERROR_INVALID_PARAMETER);
    return ReadEx(file, nullptr, buffers->lpvBuffer, &buffers->dwBufferLength, flags, context);
}

DECLSPEC_EXPORT BOOL WINAPI InternetReadFileExA(HINTERNET file, LPINTERNET_BUFFERSA buffers, DWORD flags,
                                               DWORD_PTR context)
{
    if (!buffers || buffers->dwStructSize != sizeof(*buffers))
        return InetFail(ERROR_INVALID_PARAMETER);
    return ReadEx(file, nullptr, buffers->lpvBuffer, &buffers->dwBufferLength, flags, context);
}

DECLSPEC_EXPORT BOOL WINAPI InternetQueryDataAvailable(HINTERNET file, LPDWORD available, DWORD flags,
                                                      DWORD_PTR context)
{
    INET_OBJECT *request = ReferenceRequest(file);
    if (!request)
        return FALSE;
    DWORD count = 0;
    BOOL ok;
    if (request->Busy) {
        ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
    } else if (InetIsAsync(request, context ? context : request->Context) && !InetDataReady(request)) {
        INET_OP op = {INET_OP_QUERY, context};
        op.Count = available;
        ok = InetStart(request, &op);
    } else {
        DWORD error = InetQueryAvailable(request, TRUE, &count);
        if (available)
            *available = count;
        SetLastError(error);
        ok = !error;
    }
    InetRelease(request);
    return ok;
}

/* without a cache, only a body held in memory, as a decoded one, can be moved in. */
DECLSPEC_EXPORT DWORD WINAPI InternetSetFilePointer(HINTERNET file, LONG distance, PVOID distance_high, DWORD method,
                                                   DWORD_PTR context)
{
    INET_OBJECT *request = ReferenceRequest(file);
    if (!request)
        return INVALID_SET_FILE_POINTER;
    DWORD error = ERROR_SUCCESS;
    LONGLONG move = (LONGLONG)(ULONG)distance | (distance_high ? (LONGLONG)*(LONG *)distance_high << 32
                                                                : (LONGLONG)(distance < 0 ? -1 : 0) << 32);
    LONGLONG target = 0;
    if (request->State != INET_RECEIVED || request->BodyMode != INET_BODY_MEMORY || request->Busy) {
        error = ERROR_INTERNET_INVALID_OPERATION;
    } else if (method == FILE_BEGIN) {
        target = move;
    } else if (method == FILE_CURRENT) {
        target = (LONGLONG)request->MemoryOffset + move;
    } else if (method == FILE_END) {
        target = (LONGLONG)request->MemorySize + move;
    } else {
        error = ERROR_INVALID_PARAMETER;
    }
    if (!error && target < 0)
        error = ERROR_NEGATIVE_SEEK;
    if (!error) {
        if (target > request->MemorySize)
            target = request->MemorySize;
        request->MemoryOffset = (DWORD)target;
        request->Position = (ULONGLONG)target;
        if (distance_high)
            *(LONG *)distance_high = (LONG)(target >> 32);
    }
    InetRelease(request);
    SetLastError(error);
    return error ? INVALID_SET_FILE_POINTER : (DWORD)target;
}
