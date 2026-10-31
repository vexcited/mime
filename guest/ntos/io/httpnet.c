/*
 * the connections of the http server driver: the listening sockets, request parsing, the queue a request waits in,
 * the entity body, and the responses. sockets are the host's, driven the way afd drives them: a non blocking socket,
 * a watch armed on what would block, and a worker that serves the sockets whose watch fired.
 *
 * one request at a time is served on a connection, the next is parsed once the response to the last one went out.
 */
#include "http_p.h"

#define HTTP_IO_CHUNK 65536
#define HTTP_IN_LIMIT (HTTP_MAX_HEADER_BYTES + HTTP_BODY_LIMIT + 4096)
#define HTTP_IDLE_SECONDS 120
#define WSAEWOULDBLOCK 10035
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAEADDRINUSE 10048
#define WSAEACCES 10013

static KEVENT g_work;
static PUCHAR g_bounce;
static MimeNetAddr g_addr __attribute__((aligned(16)));
static MimeNetPoll g_poll __attribute__((aligned(16)));

static const PCSTR g_request_names[HTTP_REQUEST_HEADER_COUNT] = {
    "Cache-Control", "Connection", "Date", "Keep-Alive", "Pragma", "Trailer", "Transfer-Encoding", "Upgrade", "Via",
    "Warning", "Allow", "Content-Length", "Content-Type", "Content-Encoding", "Content-Language", "Content-Location",
    "Content-MD5", "Content-Range", "Expires", "Last-Modified", "Accept", "Accept-Charset", "Accept-Encoding",
    "Accept-Language", "Authorization", "Cookie", "Expect", "From", "Host", "If-Match", "If-Modified-Since",
    "If-None-Match", "If-Range", "If-Unmodified-Since", "Max-Forwards", "Proxy-Authorization", "Referer", "Range", "TE",
    "Translate", "User-Agent"};

static const PCSTR g_response_names[HTTP_RESPONSE_HEADER_COUNT] = {
    "Cache-Control", "Connection", "Date", "Keep-Alive", "Pragma", "Trailer", "Transfer-Encoding", "Upgrade", "Via",
    "Warning", "Allow", "Content-Length", "Content-Type", "Content-Encoding", "Content-Language", "Content-Location",
    "Content-MD5", "Content-Range", "Expires", "Last-Modified", "Accept-Ranges", "Age", "ETag", "Location",
    "Proxy-Authenticate", "Retry-After", "Server", "Set-Cookie", "Vary", "WWW-Authenticate"};

static const struct {
    PCSTR Name;
    ULONG Verb;
} g_verbs[] = {{"OPTIONS", HTTP_VERB_OPTIONS},   {"GET", HTTP_VERB_GET},         {"HEAD", HTTP_VERB_HEAD},
               {"POST", HTTP_VERB_POST},         {"PUT", HTTP_VERB_PUT},         {"DELETE", HTTP_VERB_DELETE},
               {"TRACE", HTTP_VERB_TRACE},       {"CONNECT", HTTP_VERB_CONNECT}, {"TRACK", HTTP_VERB_TRACK},
               {"MOVE", HTTP_VERB_MOVE},         {"COPY", HTTP_VERB_COPY},       {"PROPFIND", HTTP_VERB_PROPFIND},
               {"PROPPATCH", HTTP_VERB_PROPPATCH}, {"MKCOL", HTTP_VERB_MKCOL},   {"LOCK", HTTP_VERB_LOCK},
               {"UNLOCK", HTTP_VERB_UNLOCK},     {"SEARCH", HTTP_VERB_SEARCH}};

static PCSTR reason_phrase(ULONG code)
{
    switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 422: return "Unprocessable Entity";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 426: return "Upgrade Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    }
    return "Unknown";
}

/* ------------------------------------------------------------ the host */

static LONG host(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 *value)
{
    ULONG64 v = 0;
    ULONG64 hs = HalHypercallUnlocked(nr, a1, a2, a3, a4, a5, 0, &v);
    if (value)
        *value = v;
    if (hs == MIME_HC_STATUS_OK)
        return 0;
    if (hs == MIME_HC_STATUS_NET_ERROR)
        return (LONG)v;
    return hs == MIME_HC_STATUS_UNSUPPORTED ? 10045 : 10022;
}

static ULONG64 pa(const void *p)
{
    return HalPhysicalOf(p);
}

static ULONG64 now_seconds(void)
{
    return KeQueryInterruptTime() / 10000000ull;
}

/* the watch: the host raises the network interrupt when what is asked for happens, once. */
static void arm(ULONG sock, ULONG events)
{
    g_poll.socket = sock;
    g_poll.events = (uint16_t)events;
    g_poll.revents = 0;
    g_poll.error = 0;
    ULONG64 ready;
    host(MIME_HC_NET_POLL, pa(&g_poll), 1, 1, 0, 0, &ready);
}

/* ------------------------------------------------------------ the listening ports */

static HTTP_ENDPOINT *find_endpoint(USHORT port)
{
    for (PLIST_ENTRY e = HttpEndpoints.Flink; e != &HttpEndpoints; e = e->Flink) {
        HTTP_ENDPOINT *ep = CONTAINING_RECORD(e, HTTP_ENDPOINT, Links);
        if (ep->Port == port)
            return ep;
    }
    return nullptr;
}

static LONG listen_on(USHORT family, USHORT port, ULONG *sock)
{
    ULONG64 id = 0;
    LONG e = host(MIME_HC_NET_SOCKET, family, 1, 6, 0, 0, &id);
    if (e)
        return e;
    ULONG s = (ULONG)id;
    host(MIME_HC_NET_OPTION, s, MIME_NET_OPT_REUSEADDR, 1, 1, 0, nullptr);
    if (family == 23)
        host(MIME_HC_NET_OPTION, s, MIME_NET_OPT_V6ONLY, 1, 1, 0, nullptr);
    memset(&g_addr, 0, sizeof(g_addr));
    g_addr.family = family;
    g_addr.data[0] = (UCHAR)(port >> 8);
    g_addr.data[1] = (UCHAR)port;
    e = host(MIME_HC_NET_BIND, s, pa(&g_addr), 0, 0, 0, nullptr);
    if (!e)
        e = host(MIME_HC_NET_LISTEN, s, 128, 0, 0, 0, nullptr);
    if (e) {
        host(MIME_HC_NET_CLOSE, s, 0, 0, 0, 0, nullptr);
        return e;
    }
    *sock = s;
    arm(s, MIME_NET_READ);
    return 0;
}

/*
 * a port is listened to on every address, whatever the prefixes name: http.sys serves one port for all of them
 * and picks by host and address afterwards, which the matching does here too.
 */
NTSTATUS HttpEndpointAdd(USHORT port)
{
    HTTP_ENDPOINT *ep = find_endpoint(port);
    if (ep) {
        ep->Urls++;
        return STATUS_SUCCESS;
    }
    ep = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ep), KI_TAG('H', 't', 'E', 'p'));
    if (!ep)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(ep, 0, sizeof(*ep));
    ep->Port = port;
    LONG e4 = listen_on(2, port, &ep->Sock4);
    LONG e6 = listen_on(23, port, &ep->Sock6);
    if (e4 && e6) {
        ExFreePool(ep);
        if (e4 == WSAEADDRINUSE || e6 == WSAEADDRINUSE)
            return STATUS_SHARING_VIOLATION;
        /* the host keeps ports below 1024 from a program that is not privileged there. */
        return e4 == WSAEACCES ? STATUS_ACCESS_DENIED : MimeAfdStatus(e4);
    }
    ep->Urls = 1;
    InsertTailList(&HttpEndpoints, &ep->Links);
    return STATUS_SUCCESS;
}

void HttpEndpointRelease(USHORT port)
{
    HTTP_ENDPOINT *ep = find_endpoint(port);
    if (!ep || --ep->Urls)
        return;
    if (ep->Sock4)
        host(MIME_HC_NET_CLOSE, ep->Sock4, 0, 0, 0, 0, nullptr);
    if (ep->Sock6)
        host(MIME_HC_NET_CLOSE, ep->Sock6, 0, 0, 0, 0, nullptr);
    RemoveEntryList(&ep->Links);
    ExFreePool(ep);
}

/* ------------------------------------------------------------ small helpers */

static void put_u16(PUCHAR b, ULONG off, ULONG v)
{
    b[off] = (UCHAR)v;
    b[off + 1] = (UCHAR)(v >> 8);
}

static void put_u32(PUCHAR b, ULONG off, ULONG v)
{
    put_u16(b, off, v);
    put_u16(b, off + 2, v >> 16);
}

static void put_u64(PUCHAR b, ULONG off, ULONG64 v)
{
    put_u32(b, off, (ULONG)v);
    put_u32(b, off + 4, (ULONG)(v >> 32));
}

static ULONG get_u16(const UCHAR *b, ULONG off)
{
    return b[off] | (ULONG)b[off + 1] << 8;
}

static ULONG get_u32(const UCHAR *b, ULONG off)
{
    return get_u16(b, off) | get_u16(b, off + 2) << 16;
}

static ULONG64 get_u64(const UCHAR *b, ULONG off)
{
    return get_u32(b, off) | (ULONG64)get_u32(b, off + 4) << 32;
}

static void put_ptr(PUCHAR b, ULONG off, ULONG64 v, ULONG psize)
{
    if (psize == 8)
        put_u64(b, off, v);
    else
        put_u32(b, off, (ULONG)v);
}

static ULONG64 get_ptr(const UCHAR *b, ULONG off, ULONG psize)
{
    return psize == 8 ? get_u64(b, off) : get_u32(b, off);
}

static ULONG align_up(ULONG v, ULONG a)
{
    return (v + a - 1) & ~(a - 1);
}

static BOOLEAN eq_nocase(PCSTR a, ULONG alen, PCSTR b)
{
    ULONG blen = (ULONG)strlen(b);
    return alen == blen && !_strnicmp(a, b, alen);
}

/* whether a comma separated header value holds the token. */
static BOOLEAN has_token(PCSTR value, ULONG length, PCSTR token)
{
    ULONG tlen = (ULONG)strlen(token);
    ULONG i = 0;
    while (i < length) {
        while (i < length && (value[i] == ' ' || value[i] == '\t' || value[i] == ','))
            i++;
        ULONG s = i;
        while (i < length && value[i] != ',' && value[i] != ';')
            i++;
        ULONG e = i;
        while (e > s && (value[e - 1] == ' ' || value[e - 1] == '\t'))
            e--;
        if (e - s == tlen && !_strnicmp(value + s, token, tlen))
            return TRUE;
        while (i < length && value[i] != ',')
            i++;
    }
    return FALSE;
}

static void format_date(char out[40])
{
    static const PCSTR days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const PCSTR months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    LARGE_INTEGER time;
    KeQuerySystemTime(&time);
    ULONG64 seconds = (ULONG64)time.QuadPart / 10000000ull;
    LONG64 day = (LONG64)(seconds / 86400);
    ULONG rest = (ULONG)(seconds % 86400);
    ULONG weekday = (ULONG)((day + 1) % 7); /* 1601-01-01 was a monday. */
    /* the civil date of a day count, counted from 1970-01-01 after the shift. */
    LONG64 z = day - 134774 + 719468;
    LONG64 era = (z >= 0 ? z : z - 146096) / 146097;
    LONG64 doe = z - era * 146097;
    LONG64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    LONG64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    LONG64 mp = (5 * doy + 2) / 153;
    LONG64 d = doy - (153 * mp + 2) / 5 + 1;
    LONG64 m = mp < 10 ? mp + 3 : mp - 9;
    LONG64 y = yoe + era * 400 + (m <= 2);
    _snprintf(out, 40, "%s, %02d %s %04d %02d:%02d:%02d GMT", days[weekday], (int)d, months[m - 1], (int)y, rest / 3600,
              rest / 60 % 60, rest % 60);
}

/* ------------------------------------------------------------ requests and connections */

void HttpReqFree(HTTP_REQ *req)
{
    if (req->Blob)
        ExFreePool(req->Blob);
    if (req->FullUrl)
        ExFreePool(req->FullUrl);
    if (req->Body)
        ExFreePool(req->Body);
    ExFreePool(req);
}

HTTP_REQ *HttpFindRequest(ULONG64 id)
{
    for (PLIST_ENTRY e = HttpConns.Flink; e != &HttpConns; e = e->Flink) {
        HTTP_CONN *c = CONTAINING_RECORD(e, HTTP_CONN, Links);
        if (c->Req && c->Req->Id == id)
            return c->Req;
    }
    return nullptr;
}

HTTP_CONN *HttpFindConn(ULONG64 id)
{
    for (PLIST_ENTRY e = HttpConns.Flink; e != &HttpConns; e = e->Flink) {
        HTTP_CONN *c = CONTAINING_RECORD(e, HTTP_CONN, Links);
        if (c->Id == id)
            return c;
    }
    return nullptr;
}

static void conn_service(HTTP_CONN *c);
static void drop_request(HTTP_CONN *c);

/* the connection ends now: whoever waits on it learns, and the host socket closes. */
static void conn_destroy(HTTP_CONN *c)
{
    PLIST_ENTRY e = HttpWaits.Flink;
    while (e != &HttpWaits) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        e = e->Flink;
        if (w->Conn != c && !(w->Req && w->Req->Conn == c))
            continue;
        if (w->Kind == HTTP_WAIT_DISCONNECT)
            HttpFinishWait(w, STATUS_SUCCESS, 0);
        else
            HttpFinishWait(w, STATUS_CONNECTION_INVALID, 0);
    }
    while (!IsListEmpty(&c->Out)) {
        HTTP_OUT *n = CONTAINING_RECORD(RemoveHeadList(&c->Out), HTTP_OUT, Links);
        if (n->Data)
            ExFreePool(n->Data);
        ExFreePool(n);
    }
    drop_request(c);
    if (c->Sock && !c->Closed)
        host(MIME_HC_NET_CLOSE, c->Sock, 0, 0, 0, 0, nullptr);
    c->Closed = TRUE;
    RemoveEntryList(&c->Links);
    if (c->In)
        ExFreePool(c->In);
    ExFreePool(c);
}

/* the request of a connection goes away, with whatever waits for it. */
static void drop_request(HTTP_CONN *c)
{
    HTTP_REQ *req = c->Req;
    if (!req)
        return;
    PLIST_ENTRY e = HttpWaits.Flink;
    while (e != &HttpWaits) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        e = e->Flink;
        if (w->Req == req)
            HttpFinishWait(w, STATUS_CONNECTION_INVALID, 0);
    }
    if (req->State == HTTP_REQ_QUEUED) {
        RemoveEntryList(&req->Links);
        if (req->Queue && req->Queue->Enqueued)
            req->Queue->Enqueued--;
    }
    c->Req = nullptr;
    HttpReqFree(req);
}

void HttpAbortConnection(HTTP_CONN *c)
{
    conn_destroy(c);
}

void HttpConnClose(HTTP_CONN *c)
{
    c->Closing = TRUE;
    HttpConnWake(c);
}

void HttpConnWake(HTTP_CONN *c)
{
    c->Dirty = TRUE;
    KeSetEvent(&g_work, IO_NO_INCREMENT, FALSE);
}

static void complete_disconnects(HTTP_CONN *c)
{
    PLIST_ENTRY e = HttpWaits.Flink;
    while (e != &HttpWaits) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        e = e->Flink;
        if (w->Kind == HTTP_WAIT_DISCONNECT && w->Conn == c)
            HttpFinishWait(w, STATUS_SUCCESS, 0);
    }
}

/* ------------------------------------------------------------ errors the server answers itself */

static HTTP_OUT *new_out(HTTP_CONN *c, ULONG length)
{
    HTTP_OUT *n = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*n), KI_TAG('H', 't', 'O', 'u'));
    if (!n)
        return nullptr;
    memset(n, 0, sizeof(*n));
    n->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('H', 't', 'O', 'd'));
    if (!n->Data) {
        ExFreePool(n);
        return nullptr;
    }
    n->Length = length;
    InsertTailList(&c->Out, &n->Links);
    return n;
}

static void send_error(HTTP_CONN *c, ULONG code, PCSTR detail)
{
    char date[40], body[900], head[400];
    format_date(date);
    PCSTR reason = reason_phrase(code);
    int body_length = _snprintf(body, sizeof(body),
                                "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\"http://www.w3.org/TR/html4/strict.dtd\">\r\n"
                                "<HTML><HEAD><TITLE>%s</TITLE>\r\n"
                                "<META HTTP-EQUIV=\"Content-Type\" Content=\"text/html; charset=us-ascii\"></HEAD>\r\n"
                                "<BODY><h2>%s</h2>\r\n<hr><p>HTTP Error %u. %s</p>\r\n</BODY></HTML>\r\n",
                                reason, reason, code, detail);
    int head_length = _snprintf(head, sizeof(head),
                                "HTTP/1.1 %u %s\r\nContent-Type: text/html; charset=us-ascii\r\nServer: Microsoft-HTTPAPI/2.0\r\n"
                                "Date: %s\r\nConnection: close\r\nContent-Length: %d\r\n\r\n",
                                code, reason, date, body_length);
    HTTP_OUT *n = new_out(c, head_length + body_length);
    if (n) {
        memcpy(n->Data, head, head_length);
        memcpy(n->Data + head_length, body, body_length);
        n->Last = TRUE;
        n->Close = TRUE;
    }
    c->Closing = TRUE;
}

/* ------------------------------------------------------------ parsing a request */

static BOOLEAN token_char(UCHAR c)
{
    if (c <= 32 || c >= 127)
        return FALSE;
    return !strchr("()<>@,;:\\\"/[]?={}", c);
}

static int find_header_end(const UCHAR *b, ULONG n, ULONG *end)
{
    for (ULONG i = 0; i + 1 < n; i++) {
        if (b[i] == '\n' && b[i + 1] == '\n') {
            *end = i + 2;
            return 1;
        }
        if (b[i] == '\n' && i + 2 < n && b[i + 1] == '\r' && b[i + 2] == '\n') {
            *end = i + 3;
            return 1;
        }
    }
    return 0;
}

typedef struct PARSE_ERROR {
    ULONG Code;
    PCSTR Detail;
} PARSE_ERROR;

static void set_error(PARSE_ERROR *e, ULONG code, PCSTR detail)
{
    e->Code = code;
    e->Detail = detail;
}

static ULONG parse_decimal(PCSTR s, ULONG n, ULONG64 *v)
{
    if (!n)
        return 0;
    ULONG64 r = 0;
    for (ULONG i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        if (r > (~0ull - 9) / 10)
            return 0;
        r = r * 10 + (ULONG64)(s[i] - '0');
    }
    *v = r;
    return 1;
}

/* adds a header to the request, joining a second value of a known one. */
static BOOLEAN add_header(HTTP_REQ *req, PUCHAR *cursor, PUCHAR limit, PCSTR name, ULONG name_length, PCSTR value,
                          ULONG value_length)
{
    LONG known = -1;
    for (ULONG i = 0; i < HTTP_REQUEST_HEADER_COUNT; i++) {
        if (eq_nocase(name, name_length, g_request_names[i])) {
            known = (LONG)i;
            break;
        }
    }
    ULONG need = value_length + 1 + (known >= 0 ? 0 : name_length + 1);
    if (known >= 0 && req->Known[known].Value)
        need += req->Known[known].Length + 2;
    if ((ULONG)(limit - *cursor) < need)
        return FALSE;
    if (known >= 0) {
        PCHAR out = (PCHAR)*cursor;
        if (req->Known[known].Value) {
            memcpy(out, req->Known[known].Value, req->Known[known].Length);
            memcpy(out + req->Known[known].Length, ", ", 2);
            memcpy(out + req->Known[known].Length + 2, value, value_length);
            ULONG total = req->Known[known].Length + 2 + value_length;
            out[total] = 0;
            req->Known[known].Value = out;
            req->Known[known].Length = (USHORT)total;
            *cursor += total + 1;
        } else {
            memcpy(out, value, value_length);
            out[value_length] = 0;
            req->Known[known].Value = out;
            req->Known[known].Length = (USHORT)value_length;
            *cursor += value_length + 1;
        }
        return TRUE;
    }
    if (req->UnknownCount == HTTP_MAX_UNKNOWN)
        return FALSE;
    PCHAR n = (PCHAR)*cursor;
    memcpy(n, name, name_length);
    n[name_length] = 0;
    PCHAR v = n + name_length + 1;
    memcpy(v, value, value_length);
    v[value_length] = 0;
    req->Unknown[req->UnknownCount].Name = n;
    req->Unknown[req->UnknownCount].NameLength = (USHORT)name_length;
    req->Unknown[req->UnknownCount].Value = v;
    req->Unknown[req->UnknownCount].ValueLength = (USHORT)value_length;
    req->UnknownCount++;
    *cursor += name_length + 1 + value_length + 1;
    return TRUE;
}

static HTTP_REQ *parse_request(HTTP_CONN *c, const UCHAR *b, ULONG n, PARSE_ERROR *err)
{
    HTTP_REQ *req = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*req), KI_TAG('H', 't', 'R', 'q'));
    if (!req) {
        set_error(err, 503, "The service is unavailable.");
        return nullptr;
    }
    memset(req, 0, sizeof(*req));
    InitializeListHead(&req->Links);
    req->Conn = c;
    req->HeaderBytes = n;
    req->BlobSize = n * 2 + 1024;
    req->Blob = ExAllocatePool2(POOL_FLAG_NON_PAGED, req->BlobSize, KI_TAG('H', 't', 'R', 'b'));
    if (!req->Blob) {
        HttpReqFree(req);
        set_error(err, 503, "The service is unavailable.");
        return nullptr;
    }
    PUCHAR cursor = req->Blob, limit = req->Blob + req->BlobSize;
    ULONG pos = 0;
    while (pos < n && (b[pos] == '\r' || b[pos] == '\n'))
        pos++;
    /* the request line. */
    ULONG line_end = pos;
    while (line_end < n && b[line_end] != '\n')
        line_end++;
    ULONG next = line_end + 1;
    if (line_end > pos && b[line_end - 1] == '\r')
        line_end--;
    ULONG a = pos, sp1 = a;
    while (sp1 < line_end && b[sp1] != ' ')
        sp1++;
    ULONG u = sp1 + 1, sp2 = u;
    while (sp2 < line_end && b[sp2] != ' ')
        sp2++;
    if (sp1 == a || sp1 >= line_end || sp2 >= line_end || sp2 == u) {
        set_error(err, 400, "The request is badly formed.");
        goto fail;
    }
    for (ULONG i = a; i < sp1; i++) {
        if (!token_char(b[i])) {
            set_error(err, 400, "The request is badly formed.");
            goto fail;
        }
    }
    ULONG method_length = sp1 - a;
    req->Verb = HTTP_VERB_UNKNOWN;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_verbs); i++) {
        if (strlen(g_verbs[i].Name) == method_length && !memcmp(g_verbs[i].Name, b + a, method_length)) {
            req->Verb = g_verbs[i].Verb;
            break;
        }
    }
    if (req->Verb == HTTP_VERB_UNKNOWN) {
        if (method_length > 255) {
            set_error(err, 501, "The server does not support the functionality required to fulfill the request.");
            goto fail;
        }
        req->Method = (PCHAR)cursor;
        memcpy(cursor, b + a, method_length);
        cursor[method_length] = 0;
        req->MethodLength = (USHORT)method_length;
        cursor += method_length + 1;
    }
    req->HeadRequest = req->Verb == HTTP_VERB_HEAD;
    ULONG url_length = sp2 - u;
    if (url_length > HTTP_MAX_HEADER_BYTES) {
        set_error(err, 414, "The request URL is too long.");
        goto fail;
    }
    req->RawUrl = (PCHAR)cursor;
    memcpy(cursor, b + u, url_length);
    cursor[url_length] = 0;
    req->RawUrlLength = (USHORT)url_length;
    cursor += url_length + 1;
    for (ULONG i = 0; i < url_length; i++) {
        if (b[u + i] < 33 || b[u + i] == 127) {
            set_error(err, 400, "The request is badly formed.");
            goto fail;
        }
    }
    /* HTTP/major.minor. */
    ULONG v = sp2 + 1, vlen = line_end - v;
    if (vlen < 8 || memcmp(b + v, "HTTP/", 5) || b[v + 5] < '0' || b[v + 5] > '9' || b[v + 6] != '.' || b[v + 7] < '0' ||
        b[v + 7] > '9' || vlen != 8) {
        set_error(err, 400, "The request is badly formed.");
        goto fail;
    }
    req->VerMajor = b[v + 5] - '0';
    req->VerMinor = b[v + 7] - '0';
    if (req->VerMajor > 1) {
        set_error(err, 505, "The HTTP version is not supported.");
        goto fail;
    }
    if (req->VerMajor == 0) {
        set_error(err, 400, "The request is badly formed.");
        goto fail;
    }
    /* the headers, folded lines joined. */
    char *folded = (char *)ExAllocatePool2(POOL_FLAG_NON_PAGED, n + 1, KI_TAG('H', 't', 'R', 'f'));
    if (!folded) {
        set_error(err, 503, "The service is unavailable.");
        goto fail;
    }
    ULONG count = 0;
    pos = next;
    while (pos < n) {
        ULONG le = pos;
        while (le < n && b[le] != '\n')
            le++;
        ULONG after = le + 1;
        if (le > pos && b[le - 1] == '\r')
            le--;
        if (le == pos)
            break;
        /* a line that starts with a space or tab continues the one before. */
        if (b[pos] == ' ' || b[pos] == '\t') {
            if (!count) {
                ExFreePool(folded);
                set_error(err, 400, "The request is badly formed.");
                goto fail;
            }
            ULONG p2 = pos;
            while (p2 < le && (b[p2] == ' ' || b[p2] == '\t'))
                p2++;
            folded[count++] = ' ';
            memcpy(folded + count, b + p2, le - p2);
            count += le - p2;
        } else {
            if (count)
                folded[count++] = '\n';
            memcpy(folded + count, b + pos, le - pos);
            count += le - pos;
        }
        pos = after;
    }
    ULONG line_start = 0;
    ULONG header_count = 0;
    while (count && line_start <= count) {
        ULONG le = line_start;
        while (le < count && folded[le] != '\n')
            le++;
        ULONG colon = line_start;
        while (colon < le && folded[colon] != ':')
            colon++;
        if (colon == le || colon == line_start) {
            ExFreePool(folded);
            set_error(err, 400, "The request is badly formed.");
            goto fail;
        }
        for (ULONG i = line_start; i < colon; i++) {
            if (!token_char((UCHAR)folded[i])) {
                ExFreePool(folded);
                set_error(err, 400, "The request is badly formed.");
                goto fail;
            }
        }
        ULONG vs = colon + 1, ve = le;
        while (vs < ve && (folded[vs] == ' ' || folded[vs] == '\t'))
            vs++;
        while (ve > vs && (folded[ve - 1] == ' ' || folded[ve - 1] == '\t'))
            ve--;
        if (++header_count > HTTP_MAX_HEADERS || !add_header(req, &cursor, limit, folded + line_start, colon - line_start,
                                                              folded + vs, ve - vs)) {
            ExFreePool(folded);
            set_error(err, 431, "The size of the request headers is too long.");
            goto fail;
        }
        if (le >= count)
            break;
        line_start = le + 1;
    }
    ExFreePool(folded);

    /* what the headers say about the body and the connection. */
    PCHAR te = req->Known[HTTP_HDR_TRANSFER_ENCODING].Value;
    ULONG te_length = req->Known[HTTP_HDR_TRANSFER_ENCODING].Length;
    PCHAR cl = req->Known[HTTP_HDR_CONTENT_LENGTH].Value;
    ULONG cl_length = req->Known[HTTP_HDR_CONTENT_LENGTH].Length;
    if (te) {
        if (!has_token(te, te_length, "chunked") || te_length > 7 + 2 + 7 + 10) {
            if (!has_token(te, te_length, "chunked")) {
                set_error(err, 501, "The server does not support the functionality required to fulfill the request.");
                goto fail;
            }
        }
        req->Chunked = TRUE;
        req->HasBody = TRUE;
    } else if (cl) {
        if (!parse_decimal(cl, cl_length, &req->ContentLength)) {
            set_error(err, 400, "The request is badly formed.");
            goto fail;
        }
        req->HasBody = req->ContentLength != 0;
        req->BodyDone = !req->HasBody;
    } else {
        if (req->Verb == HTTP_VERB_POST || req->Verb == HTTP_VERB_PUT) {
            set_error(err, 411, "The request must be chunked or have a content length.");
            goto fail;
        }
        req->BodyDone = TRUE;
    }
    if (req->Known[HTTP_HDR_EXPECT].Value && has_token(req->Known[HTTP_HDR_EXPECT].Value, req->Known[HTTP_HDR_EXPECT].Length,
                                                       "100-continue") &&
        req->HasBody && req->VerMinor >= 1)
        req->Expect100 = TRUE;
    PCHAR conn = req->Known[HTTP_HDR_CONNECTION].Value;
    ULONG conn_length = req->Known[HTTP_HDR_CONNECTION].Length;
    if (req->VerMinor >= 1)
        req->KeepAlive = !(conn && has_token(conn, conn_length, "close"));
    else
        req->KeepAlive = conn && has_token(conn, conn_length, "keep-alive");
    if (req->VerMinor >= 1 && !req->Known[HTTP_HDR_HOST].Value && !(req->RawUrlLength > 7 && !_strnicmp(req->RawUrl, "http://", 7))) {
        set_error(err, 400, "The request hostname is invalid.");
        goto fail;
    }
    NTSTATUS status = HttpCookUrl(req, &c->Local, c->LocalPort, req->Known[HTTP_HDR_HOST].Value,
                                  req->Known[HTTP_HDR_HOST].Length);
    if (!NT_SUCCESS(status)) {
        set_error(err, 400, req->VerMinor >= 1 ? "The request hostname is invalid." : "The request is badly formed.");
        goto fail;
    }
    return req;
fail:
    HttpReqFree(req);
    return nullptr;
}

/* the wait-for-request that a request goes to, or none. */
static HTTP_WAIT *find_receiver(HTTP_QUEUE *queue)
{
    for (PLIST_ENTRY e = HttpWaits.Flink; e != &HttpWaits; e = e->Flink) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        if (w->Kind == HTTP_WAIT_REQUEST && w->Queue == queue && !w->Id)
            return w;
    }
    return nullptr;
}

/* ------------------------------------------------------------ the request seen by the program */

typedef struct REQ_LAYOUT {
    ULONG P;
    ULONG V1Size, V2Size;
    ULONG Flags, ConnectionId, RequestId, UrlContext, Version, Verb, UnknownVerbLength, RawUrlLength, UnknownVerb, RawUrl,
        CookedLengths, CookedPointers, Address, UnknownHeaderCount, UnknownHeaders, TrailerCount, Trailers, KnownHeaders,
        BytesReceived, EntityChunkCount, EntityChunks, RawConnectionId, SslInfo, RequestInfoCount, RequestInfo;
    ULONG KnownSize, UnknownSize, ChunkSize;
} REQ_LAYOUT;

static const REQ_LAYOUT g_layout64 = {8,   848, 864, 0,   8,   16,  24,  32,  36,  40,  42,  48,  56,  64,  72,
                                      104, 120, 128, 136, 144, 152, 808, 816, 824, 832, 840, 848, 856, 16,  24, 32};
static const REQ_LAYOUT g_layout32 = {4,  464, 472, 0,  8,  16, 24, 32, 36, 40, 42, 44, 48, 52, 60,
                                      76, 84,  88,  92, 96, 100, 432, 440, 444, 448, 456, 464, 468, 8,  12, 32};

/* the request as the library's buffer holds it, the pointers already those of the program. */
static NTSTATUS compose_request(HTTP_REQ *req, BOOLEAN wow, BOOLEAN v2, ULONG flags, ULONG64 user, ULONG length,
                                PUCHAR *image, ULONG *size, ULONG *needed, ULONG *body_copied)
{
    const REQ_LAYOUT *l = wow ? &g_layout32 : &g_layout64;
    ULONG P = l->P;
    ULONG pos = v2 ? l->V2Size : l->V1Size;
    ULONG unknown_at = align_up(pos, P);
    pos = unknown_at + req->UnknownCount * l->UnknownSize;
    ULONG addr_len = req->Conn->Remote.family == 23 ? 28 : 16;
    ULONG remote_at = align_up(pos, P);
    pos = remote_at + addr_len;
    ULONG local_at = align_up(pos, P);
    pos = local_at + (req->Conn->Local.family == 23 ? 28 : 16);
    ULONG url_at = align_up(pos, 2);
    pos = url_at + req->FullUrlLength + 2;
    ULONG host_at = pos;
    pos += req->HostLength + 2;
    ULONG path_at = pos;
    pos += req->AbsPathLength + 2;
    ULONG query_at = pos;
    pos += req->QueryLength + 2;
    ULONG raw_at = pos;
    pos += req->RawUrlLength + 1;
    ULONG verb_at = pos;
    pos += req->MethodLength ? req->MethodLength + 1 : 0;
    ULONG known_at[HTTP_REQUEST_HEADER_COUNT];
    for (ULONG i = 0; i < HTTP_REQUEST_HEADER_COUNT; i++) {
        known_at[i] = pos;
        if (req->Known[i].Value)
            pos += req->Known[i].Length + 1;
    }
    ULONG name_at[HTTP_MAX_UNKNOWN], value_at[HTTP_MAX_UNKNOWN];
    for (ULONG i = 0; i < req->UnknownCount; i++) {
        name_at[i] = pos;
        pos += req->Unknown[i].NameLength + 1;
        value_at[i] = pos;
        pos += req->Unknown[i].ValueLength + 1;
    }
    *needed = pos;
    if (length < pos)
        return STATUS_BUFFER_OVERFLOW;
    /* the entity body that already arrived, when the program asked for it with the request. */
    ULONG chunk_at = align_up(pos, 8);
    ULONG data_at = chunk_at + l->ChunkSize;
    ULONG copy = 0;
    if ((flags & HTTP_RECEIVE_REQUEST_COPY_BODY) && req->BodyLength && length > data_at) {
        copy = length - data_at;
        if (copy > req->BodyLength)
            copy = req->BodyLength;
    }
    ULONG total = copy ? data_at + copy : pos;
    PUCHAR b = ExAllocatePool2(POOL_FLAG_NON_PAGED, total, KI_TAG('H', 't', 'I', 'm'));
    if (!b)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(b, 0, total);
    HTTP_CONN *c = req->Conn;
    BOOLEAN more = !req->BodyDone || req->BodyLength > copy;
    put_u32(b, l->Flags, (more && req->HasBody ? HTTP_REQUEST_MORE_ENTITY_BODY_EXISTS : 0));
    put_u64(b, l->ConnectionId, c->Id);
    put_u64(b, l->RequestId, req->Id);
    put_u64(b, l->UrlContext, req->Context);
    put_u16(b, l->Version, req->VerMajor);
    put_u16(b, l->Version + 2, req->VerMinor);
    put_u32(b, l->Verb, req->Verb);
    put_u16(b, l->UnknownVerbLength, req->MethodLength);
    put_u16(b, l->RawUrlLength, req->RawUrlLength);
    if (req->MethodLength) {
        put_ptr(b, l->UnknownVerb, user + verb_at, P);
        memcpy(b + verb_at, req->Method, req->MethodLength + 1);
    }
    put_ptr(b, l->RawUrl, user + raw_at, P);
    memcpy(b + raw_at, req->RawUrl, req->RawUrlLength + 1);
    put_u16(b, l->CookedLengths, req->FullUrlLength);
    put_u16(b, l->CookedLengths + 2, req->HostLength);
    put_u16(b, l->CookedLengths + 4, req->AbsPathLength);
    put_u16(b, l->CookedLengths + 6, req->QueryLength);
    put_ptr(b, l->CookedPointers, user + url_at, P);
    put_ptr(b, l->CookedPointers + P, user + host_at, P);
    put_ptr(b, l->CookedPointers + 2 * P, user + path_at, P);
    put_ptr(b, l->CookedPointers + 3 * P, user + query_at, P);
    memcpy(b + url_at, req->FullUrl, req->FullUrlLength);
    memcpy(b + host_at, req->Host, req->HostLength);
    memcpy(b + path_at, req->AbsPath, req->AbsPathLength);
    memcpy(b + query_at, req->Query, req->QueryLength);
    put_ptr(b, l->Address, user + remote_at, P);
    put_ptr(b, l->Address + P, user + local_at, P);
    /* sockaddr_in and sockaddr_in6 are what the host's address holds. */
    memcpy(b + remote_at, &c->Remote, addr_len);
    memcpy(b + local_at, &c->Local, c->Local.family == 23 ? 28 : 16);
    put_u16(b, l->UnknownHeaderCount, req->UnknownCount);
    if (req->UnknownCount)
        put_ptr(b, l->UnknownHeaders, user + unknown_at, P);
    for (ULONG i = 0; i < req->UnknownCount; i++) {
        ULONG at = unknown_at + i * l->UnknownSize;
        put_u16(b, at, req->Unknown[i].NameLength);
        put_u16(b, at + 2, req->Unknown[i].ValueLength);
        put_ptr(b, at + 4 + (P == 8 ? 4 : 0), user + name_at[i], P);
        put_ptr(b, at + 4 + (P == 8 ? 4 : 0) + P, user + value_at[i], P);
        memcpy(b + name_at[i], req->Unknown[i].Name, req->Unknown[i].NameLength + 1);
        memcpy(b + value_at[i], req->Unknown[i].Value, req->Unknown[i].ValueLength + 1);
    }
    for (ULONG i = 0; i < HTTP_REQUEST_HEADER_COUNT; i++) {
        if (!req->Known[i].Value)
            continue;
        ULONG at = l->KnownHeaders + i * l->KnownSize;
        put_u16(b, at, req->Known[i].Length);
        put_ptr(b, at + P, user + known_at[i], P);
        memcpy(b + known_at[i], req->Known[i].Value, req->Known[i].Length + 1);
    }
    put_u64(b, l->BytesReceived, req->HeaderBytes + req->BodyTotal);
    put_u64(b, l->RawConnectionId, c->Id);
    if (copy) {
        put_u16(b, l->EntityChunkCount, 1);
        put_ptr(b, l->EntityChunks, user + chunk_at, P);
        put_u32(b, chunk_at, HTTP_CHUNK_FROM_MEMORY);
        put_ptr(b, chunk_at + 8, user + data_at, P);
        put_u32(b, chunk_at + 8 + P, copy);
        memcpy(b + data_at, req->Body, copy);
    }
    *image = b;
    *size = total;
    *body_copied = copy;
    return STATUS_SUCCESS;
}

static void body_consumed(HTTP_REQ *req, ULONG n)
{
    if (!n)
        return;
    memmove(req->Body, req->Body + n, req->BodyLength - n);
    req->BodyLength -= n;
    HttpConnWake(req->Conn);
}

/* hands a request to the program that waits, or fails the wait when the buffer is small. */
static void deliver(HTTP_WAIT *w, HTTP_REQ *req)
{
    HTTP_QUEUE *queue = req->Queue;
    BOOLEAN v2 = !(queue->VersionMajor == 1 && queue->VersionMinor == 0);
    const REQ_LAYOUT *l = w->Wow ? &g_layout32 : &g_layout64;
    PUCHAR image;
    ULONG size, needed, copied;
    NTSTATUS status = compose_request(req, w->Wow, v2, w->Flags, (ULONG64)(ULONG_PTR)w->Buffer, w->Length, &image, &size,
                                      &needed, &copied);
    if (req->State == HTTP_REQ_QUEUED) {
        RemoveEntryList(&req->Links);
        InitializeListHead(&req->Links);
        if (queue->Enqueued)
            queue->Enqueued--;
    }
    req->State = HTTP_REQ_DELIVERED;
    if (status == STATUS_BUFFER_OVERFLOW) {
        /* the program learns the ids and the size and asks again with the request id. */
        UCHAR head[16];
        put_u64(head, 0, req->Conn->Id);
        put_u64(head, 8, req->Id);
        IopCopyToRequester(w->R, (PUCHAR)w->Buffer + l->ConnectionId, head, 16);
        ULONG64 raw = req->Conn->Id;
        IopCopyToRequester(w->R, (PUCHAR)w->Buffer + l->RawConnectionId, &raw, 8);
        HttpFinishWait(w, STATUS_BUFFER_OVERFLOW, needed);
        return;
    }
    if (!NT_SUCCESS(status)) {
        HttpFinishWait(w, status, 0);
        return;
    }
    status = IopCopyToRequester(w->R, w->Buffer, image, size);
    ExFreePool(image);
    if (NT_SUCCESS(status))
        body_consumed(req, copied);
    HttpFinishWait(w, status, NT_SUCCESS(status) ? size : 0);
}

void HttpDispatchPending(HTTP_QUEUE *queue)
{
    while (!IsListEmpty(&queue->Pending) && !queue->Shutdown) {
        HTTP_WAIT *w = find_receiver(queue);
        if (!w)
            break;
        HTTP_REQ *req = CONTAINING_RECORD(queue->Pending.Flink, HTTP_REQ, Links);
        deliver(w, req);
    }
}

NTSTATUS HttpReceiveRequest(IO_USER_REQUEST *r, HTTP_OPEN *o, ULONG64 id, ULONG flags, PVOID buffer, ULONG length,
                            ULONG_PTR *information)
{
    HTTP_QUEUE *queue = o->Queue;
    BOOLEAN wow = HttpIsWow();
    BOOLEAN v2 = !(queue->VersionMajor == 1 && queue->VersionMinor == 0);
    const REQ_LAYOUT *l = wow ? &g_layout32 : &g_layout64;
    ULONG minimum = v2 ? l->V2Size : l->V1Size;
    if (queue->Shutdown)
        return STATUS_INVALID_HANDLE;
    if (length < minimum)
        return STATUS_BUFFER_TOO_SMALL;
    if ((ULONG_PTR)buffer & (l->P - 1))
        return STATUS_DATATYPE_MISALIGNMENT_ERROR;
    HTTP_WAIT *w = HttpNewWait(r, HTTP_WAIT_REQUEST);
    if (!w)
        return STATUS_INSUFFICIENT_RESOURCES;
    w->Queue = queue;
    w->Id = id;
    w->Flags = flags;
    w->Buffer = buffer;
    w->Length = length;
    if (id) {
        HTTP_REQ *req = HttpFindRequest(id);
        if (!req || req->Queue != queue || req->State != HTTP_REQ_DELIVERED) {
            HttpFinishWait(w, STATUS_CONNECTION_INVALID, 0);
            return STATUS_PENDING;
        }
        deliver(w, req);
        return STATUS_PENDING;
    }
    if (!IsListEmpty(&queue->Pending)) {
        deliver(w, CONTAINING_RECORD(queue->Pending.Flink, HTTP_REQ, Links));
        return STATUS_PENDING;
    }
    return STATUS_PENDING;
}

void HttpFailQueueWaits(HTTP_QUEUE *queue, NTSTATUS status)
{
    PLIST_ENTRY e = HttpWaits.Flink;
    while (e != &HttpWaits) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        e = e->Flink;
        if (w->Kind == HTTP_WAIT_REQUEST && w->Queue == queue)
            HttpFinishWait(w, status, 0);
    }
    /* what waited in the queue is told the service is not there. */
    while (!IsListEmpty(&queue->Pending)) {
        HTTP_REQ *req = CONTAINING_RECORD(queue->Pending.Flink, HTTP_REQ, Links);
        HTTP_CONN *c = req->Conn;
        RemoveEntryList(&req->Links);
        InitializeListHead(&req->Links);
        req->State = HTTP_REQ_DONE;
        if (queue->Enqueued)
            queue->Enqueued--;
        c->Req = nullptr;
        HttpReqFree(req);
        send_error(c, 503, "The service is unavailable.");
        HttpConnWake(c);
    }
}

void HttpQueueDetach(HTTP_QUEUE *queue, NTSTATUS status)
{
    queue->Shutdown = TRUE;
    HttpFailQueueWaits(queue, status);
    PLIST_ENTRY e = HttpConns.Flink;
    while (e != &HttpConns) {
        HTTP_CONN *c = CONTAINING_RECORD(e, HTTP_CONN, Links);
        e = e->Flink;
        if (c->Req && c->Req->Queue == queue)
            conn_destroy(c);
    }
}

/* ------------------------------------------------------------ the entity body */

static void complete_body_waits(HTTP_REQ *req);

static BOOLEAN body_append(HTTP_REQ *req, const UCHAR *data, ULONG n)
{
    if (req->BodyLength + n > req->BodyCapacity) {
        ULONG cap = req->BodyCapacity ? req->BodyCapacity : 4096;
        while (cap < req->BodyLength + n)
            cap *= 2;
        PUCHAR nb = ExAllocatePool2(POOL_FLAG_NON_PAGED, cap, KI_TAG('H', 't', 'B', 'd'));
        if (!nb)
            return FALSE;
        if (req->Body) {
            memcpy(nb, req->Body, req->BodyLength);
            ExFreePool(req->Body);
        }
        req->Body = nb;
        req->BodyCapacity = cap;
    }
    memcpy(req->Body + req->BodyLength, data, n);
    req->BodyLength += n;
    req->BodyTotal += n;
    return TRUE;
}

enum { CH_SIZE, CH_EXT, CH_SIZE_LF, CH_DATA, CH_DATA_CR, CH_DATA_LF, CH_TRAILER, CH_TRAILER_LF, CH_END_LF, CH_DONE };

/* moves what arrived on the connection into the body of the request, by the framing the request declared. */
static BOOLEAN consume_body(HTTP_CONN *c, HTTP_REQ *req)
{
    BOOLEAN progress = FALSE;
    /* a response that went out before the body was read lets the rest of it go. */
    if (req->RespDone && !req->Opaque)
        req->BodyLength = 0;
    if (req->Opaque) {
        if (c->InLength && req->BodyLength < HTTP_BODY_LIMIT) {
            if (body_append(req, c->In, c->InLength)) {
                c->InLength = 0;
                progress = TRUE;
            }
        }
        if (progress || ((c->PeerClosed || c->Broken) && !req->BodyDone)) {
            if (c->PeerClosed || c->Broken)
                req->BodyDone = TRUE;
            complete_body_waits(req);
        }
        return progress;
    }
    if (req->BodyDone || !req->HasBody) {
        /* the framing ended already, or a response closed the request: whatever else arrives is the next request's. */
        return FALSE;
    }
    ULONG used = 0;
    if (!req->Chunked) {
        ULONG64 left = req->ContentLength - req->BodyTotal;
        ULONG take = c->InLength;
        if ((ULONG64)take > left)
            take = (ULONG)left;
        if (take && req->BodyLength < HTTP_BODY_LIMIT) {
            if (body_append(req, c->In, take)) {
                used = take;
                progress = TRUE;
            }
        }
        if (req->BodyTotal >= req->ContentLength)
            req->BodyDone = TRUE;
    } else {
        while (used < c->InLength && req->BodyLength < HTTP_BODY_LIMIT && req->ChunkState != CH_DONE) {
            UCHAR ch = c->In[used];
            switch (req->ChunkState) {
            case CH_SIZE:
                if (ch == '\r') {
                    req->ChunkState = CH_SIZE_LF;
                } else if (ch == '\n') {
                    goto size_end;
                } else if (ch == ';' || ch == ' ') {
                    req->ChunkState = CH_EXT;
                } else {
                    int digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                    if (digit < 0 || req->ChunkLeft > 0x0FFFFFFFFFFFFFFull)
                        goto bad;
                    req->ChunkLeft = req->ChunkLeft * 16 + digit;
                    req->LineLength++;
                }
                used++;
                break;
            case CH_EXT:
                if (ch == '\r')
                    req->ChunkState = CH_SIZE_LF;
                else if (ch == '\n')
                    goto size_end;
                used++;
                break;
            case CH_SIZE_LF:
                if (ch != '\n')
                    goto bad;
            size_end:
                if (!req->LineLength)
                    goto bad;
                req->LineLength = 0;
                req->ChunkState = req->ChunkLeft ? CH_DATA : CH_TRAILER;
                if (!req->ChunkLeft)
                    req->LineLength = 0;
                used++;
                break;
            case CH_DATA: {
                ULONG avail = c->InLength - used;
                ULONG take = req->ChunkLeft < avail ? (ULONG)req->ChunkLeft : avail;
                if (!body_append(req, c->In + used, take))
                    goto bad;
                used += take;
                req->ChunkLeft -= take;
                if (!req->ChunkLeft)
                    req->ChunkState = CH_DATA_CR;
                progress = TRUE;
                break;
            }
            case CH_DATA_CR:
                if (ch == '\r') {
                    req->ChunkState = CH_DATA_LF;
                } else if (ch == '\n') {
                    req->ChunkState = CH_SIZE;
                } else {
                    goto bad;
                }
                used++;
                break;
            case CH_DATA_LF:
                if (ch != '\n')
                    goto bad;
                req->ChunkState = CH_SIZE;
                used++;
                break;
            case CH_TRAILER:
                /* trailer lines until an empty one. */
                if (ch == '\r') {
                    req->ChunkState = CH_END_LF;
                } else if (ch == '\n') {
                    req->ChunkState = CH_DONE;
                } else {
                    req->ChunkState = CH_TRAILER_LF;
                }
                used++;
                break;
            case CH_TRAILER_LF:
                if (ch == '\n')
                    req->ChunkState = CH_TRAILER;
                used++;
                break;
            case CH_END_LF:
                if (ch != '\n')
                    goto bad;
                req->ChunkState = CH_DONE;
                used++;
                break;
            }
            continue;
        bad:
            req->Cancelled = TRUE;
            c->Broken = TRUE;
            break;
        }
        if (used)
            progress = TRUE;
        if (req->ChunkState == CH_DONE)
            req->BodyDone = TRUE;
    }
    if (used) {
        memmove(c->In, c->In + used, c->InLength - used);
        c->InLength -= used;
    }
    if (progress || req->BodyDone || c->PeerClosed || c->Broken) {
        if ((c->PeerClosed || c->Broken) && !req->BodyDone) {
            /* the client went before the body ended. */
            req->Cancelled = TRUE;
        }
        complete_body_waits(req);
    }
    return progress;
}

/* completes the waits for body bytes that the data on hand can answer. */
static void complete_body_waits(HTTP_REQ *req)
{
    PLIST_ENTRY e = HttpWaits.Flink;
    while (e != &HttpWaits) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        e = e->Flink;
        if (w->Kind != HTTP_WAIT_BODY || w->Req != req)
            continue;
        BOOLEAN ended = req->BodyDone || req->Cancelled || req->Conn->PeerClosed || req->Conn->Broken;
        if (!req->BodyLength) {
            if (req->BodyDone)
                HttpFinishWait(w, STATUS_END_OF_FILE, 0);
            else if (req->Cancelled || req->Conn->Broken || req->Conn->PeerClosed)
                HttpFinishWait(w, STATUS_CONNECTION_INVALID, 0);
            continue;
        }
        if ((w->Flags & HTTP_RECEIVE_BODY_FILL_BUFFER) && req->BodyLength < w->Length && !ended)
            continue;
        ULONG n = req->BodyLength < w->Length ? req->BodyLength : w->Length;
        NTSTATUS status = IopCopyToRequester(w->R, w->Buffer, req->Body, n);
        if (NT_SUCCESS(status))
            body_consumed(req, n);
        HttpFinishWait(w, status, NT_SUCCESS(status) ? n : 0);
    }
}

NTSTATUS HttpReceiveBody(IO_USER_REQUEST *r, HTTP_OPEN *o, ULONG64 id, ULONG flags, PVOID buffer, ULONG length,
                         ULONG_PTR *information)
{
    HTTP_REQ *req = HttpFindRequest(id);
    if (!req || req->Queue != o->Queue || req->State != HTTP_REQ_DELIVERED)
        return STATUS_CONNECTION_INVALID;
    if (flags & ~1u)
        return STATUS_INVALID_PARAMETER;
    if (!length)
        return STATUS_INVALID_PARAMETER;
    HTTP_CONN *c = req->Conn;
    if (req->Expect100 && !req->Sent100 && !req->RespStarted && !req->BodyLength) {
        static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
        HTTP_OUT *n = new_out(c, sizeof(cont) - 1);
        if (n)
            memcpy(n->Data, cont, sizeof(cont) - 1);
        req->Sent100 = TRUE;
        HttpConnWake(c);
    }
    HTTP_WAIT *w = HttpNewWait(r, HTTP_WAIT_BODY);
    if (!w)
        return STATUS_INSUFFICIENT_RESOURCES;
    w->Req = req;
    w->Flags = flags;
    w->Buffer = buffer;
    w->Length = length;
    if (!req->HasBody && !req->Opaque) {
        HttpFinishWait(w, STATUS_END_OF_FILE, 0);
        return STATUS_PENDING;
    }
    consume_body(c, req);
    BOOLEAN pending = FALSE;
    for (PLIST_ENTRY e = HttpWaits.Flink; e != &HttpWaits; e = e->Flink)
        if (CONTAINING_RECORD(e, HTTP_WAIT, Links) == w)
            pending = TRUE;
    if (pending)
        complete_body_waits(req);
    (void)information;
    return STATUS_PENDING;
}

/* ------------------------------------------------------------ responses */

typedef struct RESPONSE_CHUNKS {
    ULONG Count;
    struct {
        ULONG Type;
        PUCHAR Data;      /* memory: a copy */
        ULONG Length;
        PFILE_OBJECT File;
        ULONG64 Offset, Remaining;
    } Item[16];
} RESPONSE_CHUNKS;

static void free_chunks(RESPONSE_CHUNKS *rc)
{
    for (ULONG i = 0; i < rc->Count; i++) {
        if (rc->Item[i].Data)
            ExFreePool(rc->Item[i].Data);
        if (rc->Item[i].File)
            ObDereferenceObject(rc->Item[i].File);
    }
}

static BOOLEAN header_name_ok(PCSTR n, ULONG len)
{
    if (!len)
        return FALSE;
    for (ULONG i = 0; i < len; i++)
        if (!token_char((UCHAR)n[i]))
            return FALSE;
    return TRUE;
}

static BOOLEAN header_value_ok(PCSTR v, ULONG len)
{
    for (ULONG i = 0; i < len; i++)
        if (v[i] == '\r' || v[i] == '\n' || v[i] == 0)
            return FALSE;
    return TRUE;
}

static NTSTATUS read_user_string(ULONG64 address, ULONG length, PCHAR *out)
{
    *out = nullptr;
    if (!length)
        return STATUS_SUCCESS;
    if (!address)
        return STATUS_INVALID_PARAMETER;
    PCHAR s = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + 1, KI_TAG('H', 't', 'S', 't'));
    if (!s)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = KiCopyFromUser(s, (PVOID)(ULONG_PTR)address, length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(s);
        return status;
    }
    s[length] = 0;
    *out = s;
    return STATUS_SUCCESS;
}

typedef struct HEADER_TEXT {
    PCHAR Data;
    ULONG Length, Capacity;
    BOOLEAN Failed;
} HEADER_TEXT;

static void text_append(HEADER_TEXT *t, const void *data, ULONG n)
{
    if (t->Failed)
        return;
    if (t->Length + n + 1 > t->Capacity) {
        ULONG cap = t->Capacity ? t->Capacity : 2048;
        while (cap < t->Length + n + 1)
            cap *= 2;
        PCHAR nd = ExAllocatePool2(POOL_FLAG_NON_PAGED, cap, KI_TAG('H', 't', 'H', 'x'));
        if (!nd) {
            t->Failed = TRUE;
            return;
        }
        if (t->Data) {
            memcpy(nd, t->Data, t->Length);
            ExFreePool(t->Data);
        }
        t->Data = nd;
        t->Capacity = cap;
    }
    memcpy(t->Data + t->Length, data, n);
    t->Length += n;
    t->Data[t->Length] = 0;
}

static void text_puts(HEADER_TEXT *t, PCSTR s)
{
    text_append(t, s, (ULONG)strlen(s));
}

static void text_printf(HEADER_TEXT *t, PCSTR fmt, ...)
{
    char buffer[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (n > 0)
        text_append(t, buffer, (ULONG)(n < (int)sizeof(buffer) ? n : (int)sizeof(buffer) - 1));
}

static BOOLEAN text_has_header(const HEADER_TEXT *t, PCSTR name)
{
    ULONG nl = (ULONG)strlen(name);
    for (ULONG i = 0; i + nl + 1 < t->Length; i++) {
        if ((i == 0 || t->Data[i - 1] == '\n') && !_strnicmp(t->Data + i, name, nl) && t->Data[i + nl] == ':')
            return TRUE;
    }
    return FALSE;
}

static PCHAR text_header_value(const HEADER_TEXT *t, PCSTR name, ULONG *length)
{
    ULONG nl = (ULONG)strlen(name);
    for (ULONG i = 0; i + nl + 1 < t->Length; i++) {
        if ((i == 0 || t->Data[i - 1] == '\n') && !_strnicmp(t->Data + i, name, nl) && t->Data[i + nl] == ':') {
            ULONG s = i + nl + 1;
            while (s < t->Length && t->Data[s] == ' ')
                s++;
            ULONG e = s;
            while (e < t->Length && t->Data[e] != '\r')
                e++;
            *length = e - s;
            return t->Data + s;
        }
    }
    return nullptr;
}

static HTTP_OUT *queue_out(HTTP_CONN *c, PUCHAR data, ULONG length)
{
    HTTP_OUT *n = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*n), KI_TAG('H', 't', 'O', 'u'));
    if (!n)
        return nullptr;
    memset(n, 0, sizeof(*n));
    n->Data = data;
    n->Length = length;
    InsertTailList(&c->Out, &n->Links);
    return n;
}

/* the response headers, from the HTTP_RESPONSE the program passed. */
static NTSTATUS build_headers(HTTP_REQ *req, const UCHAR *resp, BOOLEAN wow, HEADER_TEXT *text, ULONG *status_out,
                              BOOLEAN *has_length, ULONG64 *declared_length)
{
    ULONG P = wow ? 4 : 8;
    ULONG version = get_u32(resp, 4);
    ULONG status_code = get_u16(resp, 8);
    ULONG reason_length = get_u16(resp, 10);
    ULONG64 reason_ptr = get_ptr(resp, wow ? 12 : 16, P);
    ULONG headers = wow ? 16 : 24;
    ULONG unknown_count = get_u16(resp, headers);
    ULONG64 unknown_ptr = get_ptr(resp, headers + (wow ? 4 : 8), P);
    ULONG known = headers + (wow ? 16 : 32);
    ULONG known_size = wow ? 8 : 16;
    if (status_code < 100 || status_code > 999)
        return STATUS_INVALID_PARAMETER;
    PCHAR reason = nullptr;
    NTSTATUS status = read_user_string(reason_ptr, reason_length, &reason);
    if (!NT_SUCCESS(status))
        return status;
    if (reason && !header_value_ok(reason, reason_length)) {
        ExFreePool(reason);
        return STATUS_INVALID_PARAMETER;
    }
    USHORT major = (USHORT)version, minor = (USHORT)(version >> 16);
    if (!(major == 1 && (minor == 0 || minor == 1))) {
        major = 1;
        minor = 1;
    }
    text_printf(text, "HTTP/%u.%u %u ", major, minor, status_code);
    if (reason_length)
        text_append(text, reason, reason_length);
    else
        text_puts(text, reason_phrase(status_code));
    text_puts(text, "\r\n");
    if (reason)
        ExFreePool(reason);
    for (ULONG i = 0; i < HTTP_RESPONSE_HEADER_COUNT; i++) {
        ULONG len = get_u16(resp, known + i * known_size);
        ULONG64 ptr = get_ptr(resp, known + i * known_size + P, P);
        if (!len || !ptr)
            continue;
        PCHAR v;
        status = read_user_string(ptr, len, &v);
        if (!NT_SUCCESS(status))
            return status;
        if (!header_value_ok(v, len)) {
            ExFreePool(v);
            return STATUS_INVALID_PARAMETER;
        }
        text_printf(text, "%s: ", g_response_names[i]);
        text_append(text, v, len);
        text_puts(text, "\r\n");
        ExFreePool(v);
    }
    if (unknown_count) {
        if (!unknown_ptr || unknown_count > 1024)
            return STATUS_INVALID_PARAMETER;
        ULONG usize = wow ? 12 : 24;
        for (ULONG i = 0; i < unknown_count; i++) {
            UCHAR h[24];
            status = KiCopyFromUser(h, (PVOID)(ULONG_PTR)(unknown_ptr + (ULONG64)i * usize), usize);
            if (!NT_SUCCESS(status))
                return status;
            ULONG nl = get_u16(h, 0), vl = get_u16(h, 2);
            ULONG64 np = get_ptr(h, 4 + (wow ? 0 : 4), P), vp = get_ptr(h, 4 + (wow ? 0 : 4) + P, P);
            PCHAR name, value;
            status = read_user_string(np, nl, &name);
            if (!NT_SUCCESS(status))
                return status;
            if (!name || !header_name_ok(name, nl)) {
                if (name)
                    ExFreePool(name);
                return STATUS_INVALID_PARAMETER;
            }
            status = read_user_string(vp, vl, &value);
            if (!NT_SUCCESS(status)) {
                ExFreePool(name);
                return status;
            }
            if (value && !header_value_ok(value, vl)) {
                ExFreePool(name);
                ExFreePool(value);
                return STATUS_INVALID_PARAMETER;
            }
            text_append(text, name, nl);
            text_puts(text, ": ");
            if (value)
                text_append(text, value, vl);
            text_puts(text, "\r\n");
            ExFreePool(name);
            if (value)
                ExFreePool(value);
        }
    }
    *status_out = status_code;
    ULONG cl_length;
    PCHAR cl = text_header_value(text, "Content-Length", &cl_length);
    *has_length = FALSE;
    if (cl) {
        *has_length = parse_decimal(cl, cl_length, declared_length) != 0;
    }
    return STATUS_SUCCESS;
}

static void chunk_header(HTTP_CONN *c, ULONG length, HEADER_TEXT *t)
{
    (void)c;
    text_printf(t, "%x\r\n", length);
}

/* the flags of a send, checked as http.sys does. */
static NTSTATUS check_send_flags(ULONG flags, BOOLEAN with_response)
{
    if (flags & ~(HTTP_SEND_DISCONNECT | HTTP_SEND_MORE_DATA | HTTP_SEND_BUFFER_DATA | HTTP_SEND_ENABLE_NAGLING |
                  HTTP_SEND_PROCESS_RANGES | HTTP_SEND_OPAQUE | HTTP_SEND_SYNCHRONOUS))
        return STATUS_INVALID_PARAMETER;
    if ((flags & HTTP_SEND_OPAQUE) && (flags & HTTP_SEND_DISCONNECT))
        return STATUS_INVALID_PARAMETER;
    (void)with_response;
    return STATUS_SUCCESS;
}

NTSTATUS HttpSendResponse(IO_USER_REQUEST *r, HTTP_OPEN *o, const UCHAR *in, ULONG in_len, BOOLEAN with_response,
                          ULONG_PTR *information)
{
    BOOLEAN wow = HttpIsWow();
    ULONG P = wow ? 4 : 8;
    ULONG64 response_ptr = get_ptr(in, 0, P);
    ULONG chunk_count = get_u16(in, wow ? 4 : 8);
    ULONG64 chunks_ptr = get_ptr(in, wow ? 8 : 16, P);
    ULONG64 id = get_u64(in, wow ? 0x18 : 0x20);
    ULONG flags = get_u32(in, wow ? 0x20 : 0x28);
    NTSTATUS status = check_send_flags(flags, with_response);
    if (!NT_SUCCESS(status))
        return status;
    HTTP_REQ *req = HttpFindRequest(id);
    if (!req || req->Queue != o->Queue || req->State != HTTP_REQ_DELIVERED)
        return STATUS_CONNECTION_INVALID;
    HTTP_CONN *c = req->Conn;
    if (req->RespDone || (with_response && req->RespStarted) || (!with_response && !req->RespStarted))
        return STATUS_INVALID_PARAMETER;
    if (with_response && !response_ptr)
        return STATUS_INVALID_PARAMETER;
    if (chunk_count && !chunks_ptr)
        return STATUS_INVALID_PARAMETER;
    if (chunk_count > 4096)
        return STATUS_INVALID_PARAMETER;

    /* the chunks of the body, captured now: the caller's memory is gone once the call returns. */
    RESPONSE_CHUNKS *rc = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*rc), KI_TAG('H', 't', 'C', 'h'));
    if (!rc)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(rc, 0, sizeof(*rc));
    ULONG64 body_total = 0;
    BOOLEAN have_trailers = FALSE;
    HEADER_TEXT trailers = {};
    PUCHAR memory = nullptr;
    ULONG memory_length = 0, memory_capacity = 0;
    /* memory chunks join into one buffer in front of a file chunk or the end. */
    HEADER_TEXT head = {};
    HEADER_TEXT body = {};
    BOOLEAN has_length = FALSE;
    ULONG64 declared = 0;
    ULONG status_code = 200;
    BOOLEAN chunked_now = req->RespChunked;
    status = STATUS_SUCCESS;
    PUCHAR chunks_buffer = nullptr;
    if (chunk_count) {
        chunks_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, chunk_count * 32, KI_TAG('H', 't', 'C', 'c'));
        if (!chunks_buffer) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }
        status = KiCopyFromUser(chunks_buffer, (PVOID)(ULONG_PTR)chunks_ptr, chunk_count * 32);
        if (!NT_SUCCESS(status))
            goto done;
    }
    UCHAR response[600];
    memset(response, 0, sizeof(response));
    if (with_response) {
        ULONG size = wow ? 280 : 552;
        status = KiCopyFromUser(response, (PVOID)(ULONG_PTR)response_ptr, size);
        if (!NT_SUCCESS(status))
            goto done;
        status = build_headers(req, response, wow, &head, &status_code, &has_length, &declared);
        if (!NT_SUCCESS(status))
            goto done;
        if (head.Failed) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }
    }
    /* walk the chunks once to know the size, and capture the memory. */
    for (ULONG i = 0; i < chunk_count; i++) {
        const UCHAR *ch = chunks_buffer + i * 32;
        ULONG type = get_u32(ch, 0);
        if (type == HTTP_CHUNK_FROM_MEMORY) {
            ULONG64 p = get_ptr(ch, 8, P);
            ULONG len = get_u32(ch, 8 + P);
            if (!len)
                continue;
            if (!p || body_total + len > 0x20000000) {
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
            if (memory_length + len > memory_capacity) {
                ULONG cap = memory_capacity ? memory_capacity : 4096;
                while (cap < memory_length + len)
                    cap *= 2;
                PUCHAR nm = ExAllocatePool2(POOL_FLAG_NON_PAGED, cap, KI_TAG('H', 't', 'M', 'm'));
                if (!nm) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto done;
                }
                if (memory) {
                    memcpy(nm, memory, memory_length);
                    ExFreePool(memory);
                }
                memory = nm;
                memory_capacity = cap;
            }
            status = KiCopyFromUser(memory + memory_length, (PVOID)(ULONG_PTR)p, len);
            if (!NT_SUCCESS(status))
                goto done;
            memory_length += len;
            body_total += len;
            /* a chunked response frames each one, which the frame below does when it knows the size. */
            if (rc->Count && rc->Item[rc->Count - 1].Type == HTTP_CHUNK_FROM_MEMORY) {
                rc->Item[rc->Count - 1].Length += len;
            } else {
                if (rc->Count == RTL_NUMBER_OF(rc->Item)) {
                    status = STATUS_INVALID_PARAMETER;
                    goto done;
                }
                rc->Item[rc->Count].Type = HTTP_CHUNK_FROM_MEMORY;
                rc->Item[rc->Count].Length = len;
                rc->Count++;
            }
        } else if (type == HTTP_CHUNK_FROM_FILE_HANDLE) {
            ULONG64 start = get_u64(ch, 8), len = get_u64(ch, 16);
            ULONG64 handle = get_ptr(ch, 24, P);
            PFILE_OBJECT file;
            status = ObReferenceHandle((HANDLE)(ULONG_PTR)handle, IoFileObjectTypeObject, UserMode, (PVOID *)&file, nullptr);
            if (!NT_SUCCESS(status)) {
                status = STATUS_INVALID_HANDLE;
                goto done;
            }
            ULONG64 size = 0;
            if (!NT_SUCCESS(IoQueryFileSize(file, &size)) || start > size) {
                ObDereferenceObject(file);
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
            if (len == 0 || len > size - start)
                len = size - start;
            if (rc->Count == RTL_NUMBER_OF(rc->Item)) {
                ObDereferenceObject(file);
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
            rc->Item[rc->Count].Type = HTTP_CHUNK_FROM_FILE_HANDLE;
            rc->Item[rc->Count].File = file;
            rc->Item[rc->Count].Offset = start;
            rc->Item[rc->Count].Remaining = len;
            rc->Count++;
            body_total += len;
        } else if (type == HTTP_CHUNK_TRAILERS) {
            ULONG count = get_u16(ch, 8);
            ULONG64 p = get_ptr(ch, 8 + P, P);
            if (flags & HTTP_SEND_MORE_DATA) {
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
            ULONG usize = wow ? 12 : 24;
            for (ULONG k = 0; k < count && k < 64; k++) {
                UCHAR h[24];
                status = KiCopyFromUser(h, (PVOID)(ULONG_PTR)(p + (ULONG64)k * usize), usize);
                if (!NT_SUCCESS(status))
                    goto done;
                ULONG nl = get_u16(h, 0), vl = get_u16(h, 2);
                PCHAR name, value;
                status = read_user_string(get_ptr(h, 4 + (wow ? 0 : 4), P), nl, &name);
                if (!NT_SUCCESS(status))
                    goto done;
                status = read_user_string(get_ptr(h, 4 + (wow ? 0 : 4) + P, P), vl, &value);
                if (!NT_SUCCESS(status) || !name || !header_name_ok(name, nl)) {
                    if (name)
                        ExFreePool(name);
                    status = NT_SUCCESS(status) ? STATUS_INVALID_PARAMETER : status;
                    goto done;
                }
                text_append(&trailers, name, nl);
                text_puts(&trailers, ": ");
                if (value)
                    text_append(&trailers, value, vl);
                text_puts(&trailers, "\r\n");
                ExFreePool(name);
                if (value)
                    ExFreePool(value);
                have_trailers = TRUE;
            }
        } else {
            /* the fragment cache has no entries. */
            status = STATUS_NOT_SUPPORTED;
            goto done;
        }
    }

    BOOLEAN more = (flags & HTTP_SEND_MORE_DATA) != 0;
    if (with_response) {
        BOOLEAN bodyless = status_code < 200 || status_code == 204 || status_code == 304;
        BOOLEAN opaque = (flags & HTTP_SEND_OPAQUE) != 0;
        if (opaque && status_code != 101) {
            status = STATUS_INVALID_PARAMETER;
            goto done;
        }
        ULONG te_length = 0, cn_length = 0;
        PCHAR te = text_header_value(&head, "Transfer-Encoding", &te_length);
        BOOLEAN te_chunked = te && has_token(te, te_length, "chunked");
        PCHAR cn = text_header_value(&head, "Connection", &cn_length);
        BOOLEAN cn_present = cn != nullptr;
        BOOLEAN cn_close = cn && has_token(cn, cn_length, "close");
        BOOLEAN close = (flags & HTTP_SEND_DISCONNECT) != 0 || !req->KeepAlive || cn_close;
        if (te_chunked)
            chunked_now = TRUE;
        else if (more && !has_length && !bodyless && !opaque && !(req->VerMinor == 0)) {
            text_puts(&head, "Transfer-Encoding: chunked\r\n");
            chunked_now = TRUE;
        } else if (more && !has_length && !bodyless && !opaque) {
            /* HTTP/1.0 cannot frame a body of unknown length: the end of it is the end of the connection. */
            close = TRUE;
        }
        if (!has_length && !more && !chunked_now && !bodyless && !opaque) {
            text_printf(&head, "Content-Length: %llu\r\n", body_total);
            has_length = TRUE;
            declared = body_total;
        }
        if (!text_has_header(&head, "Date")) {
            char date[40];
            format_date(date);
            text_printf(&head, "Date: %s\r\n", date);
        }
        if (!text_has_header(&head, "Server"))
            text_puts(&head, "Server: Microsoft-HTTPAPI/2.0\r\n");
        if (opaque) {
            req->Opaque = TRUE;
        } else if (close) {
            if (!cn_present)
                text_puts(&head, "Connection: close\r\n");
        } else if (req->VerMinor == 0 && !cn_present) {
            text_puts(&head, "Connection: keep-alive\r\n");
        }
        text_puts(&head, "\r\n");
        req->RespClose = close;
        req->RespChunked = chunked_now;
        req->RespStarted = TRUE;
        (void)declared;
    }
    if (head.Failed || body.Failed) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    /* the bytes to write, in order. */
    {
        BOOLEAN skip_body = req->HeadRequest;
        BOOLEAN raw = req->Opaque;
        ULONG64 sent_total = 0;
        HTTP_OUT *last = nullptr;
        ULONG memory_at = 0;
        HEADER_TEXT pending = {};
        if (with_response)
            text_append(&pending, head.Data, head.Length);
        ULONG chunks = rc->Count;
        for (ULONG i = 0; i < chunks; i++) {
            if (rc->Item[i].Type == HTTP_CHUNK_FROM_MEMORY) {
                ULONG len = rc->Item[i].Length;
                if (!skip_body) {
                    if (req->RespChunked && !raw)
                        chunk_header(c, len, &pending);
                    text_append(&pending, memory + memory_at, len);
                    if (req->RespChunked && !raw)
                        text_puts(&pending, "\r\n");
                }
                memory_at += len;
            } else {
                if (skip_body)
                    continue;
                /* the text so far goes out first, then the file in the order it was named. */
                if (req->RespChunked && !raw)
                    chunk_header(c, (ULONG)rc->Item[i].Remaining, &pending);
                if (pending.Length) {
                    PUCHAR copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, pending.Length, KI_TAG('H', 't', 'O', 'd'));
                    if (!copy) {
                        status = STATUS_INSUFFICIENT_RESOURCES;
                        goto done;
                    }
                    memcpy(copy, pending.Data, pending.Length);
                    last = queue_out(c, copy, pending.Length);
                    sent_total += pending.Length;
                    pending.Length = 0;
                }
                HTTP_OUT *n = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*n) + sizeof(PFILE_OBJECT) + 16, KI_TAG('H', 't', 'O', 'f'));
                if (!n) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto done;
                }
                memset(n, 0, sizeof(*n));
                n->File = rc->Item[i].File;
                rc->Item[i].File = nullptr;
                n->FileOffset = rc->Item[i].Offset;
                n->FileRemaining = rc->Item[i].Remaining;
                InsertTailList(&c->Out, &n->Links);
                last = n;
                sent_total += rc->Item[i].Remaining;
                if (req->RespChunked && !raw)
                    text_puts(&pending, "\r\n");
            }
        }
        BOOLEAN finish = !more;
        if (finish && req->RespChunked && !raw) {
            text_puts(&pending, "0\r\n");
            if (have_trailers)
                text_append(&pending, trailers.Data, trailers.Length);
            text_puts(&pending, "\r\n");
        }
        if (pending.Length || !last) {
            PUCHAR copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, pending.Length ? pending.Length : 1, KI_TAG('H', 't', 'O', 'd'));
            if (!copy) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto done;
            }
            memcpy(copy, pending.Data, pending.Length);
            if (pending.Length || (flags & HTTP_SEND_DISCONNECT)) {
                last = queue_out(c, copy, pending.Length);
                sent_total += pending.Length;
            } else {
                ExFreePool(copy);
            }
        }
        if (pending.Data)
            ExFreePool(pending.Data);
        if (finish) {
            req->RespDone = TRUE;
            if (last) {
                last->Last = TRUE;
                last->Close = req->RespClose || (flags & HTTP_SEND_DISCONNECT);
            } else if (req->RespClose || (flags & HTTP_SEND_DISCONNECT)) {
                c->Closing = TRUE;
            }
        }
        if (req->Opaque)
            c->Raw = TRUE;
        *information = sent_total;
        /* the call completes once everything is handed to the transport. */
        conn_service(c);
        if (last && !(flags & HTTP_SEND_BUFFER_DATA)) {
            BOOLEAN still = FALSE;
            for (PLIST_ENTRY e = c->Out.Flink; e != &c->Out; e = e->Flink)
                if (CONTAINING_RECORD(e, HTTP_OUT, Links) == last)
                    still = TRUE;
            if (still) {
                HTTP_WAIT *w = HttpNewWait(r, HTTP_WAIT_SEND);
                if (!w) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto done;
                }
                w->Conn = c;
                w->Total = sent_total;
                last->Wait = w;
                status = STATUS_PENDING;
            }
        }
    }
done:
    if (chunks_buffer)
        ExFreePool(chunks_buffer);
    if (memory)
        ExFreePool(memory);
    if (head.Data)
        ExFreePool(head.Data);
    if (body.Data)
        ExFreePool(body.Data);
    if (trailers.Data)
        ExFreePool(trailers.Data);
    free_chunks(rc);
    ExFreePool(rc);
    (void)in_len;
    return status;
}

NTSTATUS HttpWaitDisconnect(IO_USER_REQUEST *r, HTTP_OPEN *o, ULONG64 id)
{
    HTTP_CONN *c = HttpFindConn(id);
    if (!c || (c->Req && c->Req->Queue != o->Queue))
        return STATUS_CONNECTION_INVALID;
    if (c->PeerClosed || c->Broken || c->Closing)
        return STATUS_SUCCESS;
    HTTP_WAIT *w = HttpNewWait(r, HTTP_WAIT_DISCONNECT);
    if (!w)
        return STATUS_INSUFFICIENT_RESOURCES;
    w->Conn = c;
    return STATUS_PENDING;
}

NTSTATUS HttpCancelRequest(HTTP_OPEN *o, ULONG64 id)
{
    HTTP_REQ *req = HttpFindRequest(id);
    if (!req || req->Queue != o->Queue)
        return STATUS_CONNECTION_INVALID;
    conn_destroy(req->Conn);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------ serving a connection */

static void reject(HTTP_CONN *c, ULONG code, PCSTR detail)
{
    send_error(c, code, detail);
}

/* the pieces of out that the socket takes now. */
static BOOLEAN flush_out(HTTP_CONN *c, BOOLEAN *want_write)
{
    BOOLEAN progress = FALSE;
    while (!IsListEmpty(&c->Out) && !c->Broken) {
        HTTP_OUT *n = CONTAINING_RECORD(c->Out.Flink, HTTP_OUT, Links);
        if (n->File && n->Sent == n->Length) {
            /* the next block of the file. */
            if (n->Data) {
                ExFreePool(n->Data);
                n->Data = nullptr;
            }
            n->Sent = n->Length = 0;
            if (n->FileRemaining) {
                ULONG want = n->FileRemaining < HTTP_IO_CHUNK ? (ULONG)n->FileRemaining : HTTP_IO_CHUNK;
                n->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, want, KI_TAG('H', 't', 'O', 'd'));
                ULONG got = 0;
                if (!n->Data || !NT_SUCCESS(IoReadFileObject(n->File, n->Data, want, n->FileOffset, &got)) || !got) {
                    c->Broken = TRUE;
                    break;
                }
                n->Length = got;
                n->FileOffset += got;
                n->FileRemaining -= got;
            }
        }
        if (n->Sent < n->Length) {
            ULONG chunk = n->Length - n->Sent;
            if (chunk > HTTP_IO_CHUNK)
                chunk = HTTP_IO_CHUNK;
            memcpy(g_bounce, n->Data + n->Sent, chunk);
            ULONG64 sent = 0;
            LONG e = host(MIME_HC_NET_SEND, c->Sock, pa(g_bounce), chunk, 0, 0, &sent);
            if (e == WSAEWOULDBLOCK) {
                *want_write = TRUE;
                break;
            }
            if (e) {
                c->Broken = TRUE;
                break;
            }
            n->Sent += (ULONG)sent;
            progress = TRUE;
            if (n->Sent < n->Length)
                continue;
        }
        if (n->File && n->FileRemaining)
            continue;
        /* this one is out. */
        RemoveEntryList(&n->Links);
        if (n->Wait)
            HttpFinishWait(n->Wait, STATUS_SUCCESS, n->Wait->Total);
        if (n->Close)
            c->Closing = TRUE;
        if (n->Data)
            ExFreePool(n->Data);
        if (n->File)
            ObDereferenceObject(n->File);
        ExFreePool(n);
        progress = TRUE;
    }
    return progress;
}

static BOOLEAN read_in(HTTP_CONN *c)
{
    BOOLEAN progress = FALSE;
    while (!c->PeerClosed && !c->Broken && !c->Closing && c->InLength < HTTP_IN_LIMIT) {
        ULONG room = HTTP_IN_LIMIT - c->InLength;
        ULONG chunk = room < HTTP_IO_CHUNK ? room : HTTP_IO_CHUNK;
        ULONG64 got = 0;
        LONG e = host(MIME_HC_NET_RECV, c->Sock, pa(g_bounce), chunk, 0, 0, &got);
        if (e == WSAEWOULDBLOCK)
            break;
        if (e) {
            c->Broken = TRUE;
            break;
        }
        if (!got) {
            c->PeerClosed = TRUE;
            break;
        }
        if (c->InLength + got > c->InCapacity) {
            ULONG cap = c->InCapacity ? c->InCapacity : 4096;
            while (cap < c->InLength + got)
                cap *= 2;
            PUCHAR nb = ExAllocatePool2(POOL_FLAG_NON_PAGED, cap, KI_TAG('H', 't', 'I', 'n'));
            if (!nb) {
                c->Broken = TRUE;
                break;
            }
            if (c->In) {
                memcpy(nb, c->In, c->InLength);
                ExFreePool(c->In);
            }
            c->In = nb;
            c->InCapacity = cap;
        }
        memcpy(c->In + c->InLength, g_bounce, (ULONG)got);
        c->InLength += (ULONG)got;
        c->LastActive = now_seconds();
        progress = TRUE;
        if (got < chunk)
            break;
    }
    return progress;
}

/* finds the queue a request goes to and queues it, or answers it with an error. */
static void route_request(HTTP_CONN *c, HTTP_REQ *req)
{
    HTTP_URLREG *url = HttpMatchUrl(req, &c->Local, c->LocalPort);
    if (!url) {
        HttpReqFree(req);
        reject(c, 404, "The requested resource is not found.");
        return;
    }
    HTTP_GROUP *g = url->Group;
    HTTP_QUEUE *q = g->Queue;
    if (!q || q->Shutdown || q->Closed || q->State || g->State || g->Session->State || q->Enqueued >= q->MaxLength) {
        HttpReqFree(req);
        reject(c, 503, "The service is unavailable.");
        return;
    }
    req->Queue = q;
    req->Context = url->Context;
    req->Id = HttpNewId(4);
    req->State = HTTP_REQ_QUEUED;
    c->Req = req;
    c->Requests++;
    InsertTailList(&q->Pending, &req->Links);
    q->Enqueued++;
    HttpDispatchPending(q);
}

static BOOLEAN process(HTTP_CONN *c)
{
    BOOLEAN progress = FALSE;
    if (c->Req) {
        HTTP_REQ *req = c->Req;
        if (consume_body(c, req))
            progress = TRUE;
        if (req->Cancelled && c->Broken) {
            return progress;
        }
        /* done once the response went out and the body is read; a connection that closes need not read it. */
        if (req->RespDone && (req->BodyDone || req->RespClose || req->Opaque || c->Closing || c->PeerClosed)) {
            BOOLEAN out_empty = IsListEmpty(&c->Out);
            if (req->Opaque) {
                /* the raw connection lasts until one side ends it. */
            } else if (req->RespClose || !req->KeepAlive || !req->BodyDone) {
                c->Closing = TRUE;
                if (out_empty) {
                    drop_request(c);
                    progress = TRUE;
                }
            } else {
                drop_request(c);
                c->LastActive = now_seconds();
                progress = TRUE;
            }
        }
        return progress;
    }
    if (c->Closing)
        return FALSE;
    /* a new request, when its headers are all here. blank lines before it are skipped. */
    ULONG end;
    ULONG blank = 0;
    while (blank < c->InLength && (c->In[blank] == '\r' || c->In[blank] == '\n'))
        blank++;
    if (blank) {
        memmove(c->In, c->In + blank, c->InLength - blank);
        c->InLength -= blank;
        progress = TRUE;
    }
    if (!c->InLength)
        return progress;
    if (!find_header_end(c->In, c->InLength, &end)) {
        if (c->InLength > HTTP_MAX_HEADER_BYTES) {
            reject(c, 431, "The size of the request headers is too long.");
            progress = TRUE;
        }
        return progress;
    }
    if (end > HTTP_MAX_HEADER_BYTES) {
        reject(c, 431, "The size of the request headers is too long.");
        return TRUE;
    }
    PARSE_ERROR err = {};
    HTTP_REQ *req = parse_request(c, c->In, end, &err);
    memmove(c->In, c->In + end, c->InLength - end);
    c->InLength -= end;
    if (!req) {
        reject(c, err.Code, err.Detail);
        return TRUE;
    }
    c->RequestStart = now_seconds();
    route_request(c, req);
    return TRUE;
}

static void conn_service(HTTP_CONN *c)
{
    if (c->Closed)
        return;
    BOOLEAN want_write = FALSE;
    for (ULONG round = 0; round < 64; round++) {
        BOOLEAN progress = FALSE;
        want_write = FALSE;
        progress |= flush_out(c, &want_write);
        progress |= read_in(c);
        progress |= process(c);
        if (!progress)
            break;
    }
    c->Dirty = FALSE;
    if (c->PeerClosed || c->Broken)
        complete_disconnects(c);
    BOOLEAN out_empty = IsListEmpty(&c->Out);
    /* a client that went takes its requests with it, unless the response is out and only waits to drain. */
    if (c->Broken || (c->Closing && out_empty) ||
        (c->PeerClosed && (c->Req ? !c->Req->RespDone : out_empty))) {
        conn_destroy(c);
        return;
    }
    ULONG events = 0;
    if (!c->PeerClosed && !c->Closing && c->InLength < HTTP_IN_LIMIT)
        events |= MIME_NET_READ;
    if (!out_empty)
        events |= MIME_NET_WRITE;
    if (events)
        arm(c->Sock, events);
}

static void accept_ready(HTTP_ENDPOINT *ep, ULONG sock)
{
    for (;;) {
        ULONG64 id = 0;
        LONG e = host(MIME_HC_NET_ACCEPT, sock, pa(&g_addr), 0, 0, 0, &id);
        if (e == WSAEWOULDBLOCK)
            break;
        if (e == WSAECONNABORTED || e == WSAECONNRESET)
            continue;
        if (e)
            break;
        HTTP_CONN *c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('H', 't', 'C', 'n'));
        if (!c) {
            host(MIME_HC_NET_CLOSE, id, 0, 0, 0, 0, nullptr);
            continue;
        }
        memset(c, 0, sizeof(*c));
        InitializeListHead(&c->Out);
        c->Sock = (ULONG)id;
        c->Remote = g_addr;
        memset(&g_addr, 0, sizeof(g_addr));
        if (host(MIME_HC_NET_NAME, c->Sock, 0, pa(&g_addr), 0, 0, nullptr) == 0)
            c->Local = g_addr;
        else
            c->Local.family = 2;
        c->LocalPort = (USHORT)((c->Local.data[0] << 8) | c->Local.data[1]);
        if (!c->LocalPort)
            c->LocalPort = ep->Port;
        c->Id = HttpNewId(5);
        c->LastActive = now_seconds();
        host(MIME_HC_NET_OPTION, c->Sock, MIME_NET_OPT_NODELAY, 1, 1, 0, nullptr);
        InsertTailList(&HttpConns, &c->Links);
        conn_service(c);
    }
    arm(sock, MIME_NET_READ);
}

/* the afd worker saw the host raise a socket this driver watches. */
BOOLEAN HttpHostEvent(ULONG sock)
{
    BOOLEAN mine = FALSE;
    HttpLock();
    for (PLIST_ENTRY e = HttpConns.Flink; e != &HttpConns; e = e->Flink) {
        HTTP_CONN *c = CONTAINING_RECORD(e, HTTP_CONN, Links);
        if (c->Sock == sock) {
            c->Dirty = TRUE;
            mine = TRUE;
        }
    }
    for (PLIST_ENTRY e = HttpEndpoints.Flink; e != &HttpEndpoints; e = e->Flink) {
        HTTP_ENDPOINT *ep = CONTAINING_RECORD(e, HTTP_ENDPOINT, Links);
        if (ep->Sock4 == sock || ep->Sock6 == sock) {
            ep->Dirty = TRUE;
            mine = TRUE;
        }
    }
    HttpUnlock();
    if (mine)
        KeSetEvent(&g_work, IO_NO_INCREMENT, FALSE);
    return mine;
}

void HttpServiceAll(void)
{
    for (PLIST_ENTRY e = HttpEndpoints.Flink; e != &HttpEndpoints; e = e->Flink) {
        HTTP_ENDPOINT *ep = CONTAINING_RECORD(e, HTTP_ENDPOINT, Links);
        if (!ep->Dirty)
            continue;
        ep->Dirty = FALSE;
        if (ep->Sock4)
            accept_ready(ep, ep->Sock4);
        if (ep->Sock6)
            accept_ready(ep, ep->Sock6);
    }
    ULONG64 now = now_seconds();
    PLIST_ENTRY e = HttpConns.Flink;
    while (e != &HttpConns) {
        HTTP_CONN *c = CONTAINING_RECORD(e, HTTP_CONN, Links);
        e = e->Flink;
        if (c->Dirty) {
            conn_service(c);
            continue;
        }
        /* connections nobody uses end after the idle time. */
        if (!c->Req && IsListEmpty(&c->Out) && now - c->LastActive > HTTP_IDLE_SECONDS)
            conn_destroy(c);
        else if (!c->Req && c->InLength && now - c->LastActive > HTTP_IDLE_SECONDS)
            conn_destroy(c);
    }
}

static void NTAPI worker(PVOID context)
{
    LARGE_INTEGER timeout;
    timeout.QuadPart = -50000000LL; /* five seconds for the timers. */
    for (;;) {
        KeWaitForSingleObject(&g_work, Executive, KernelMode, FALSE, &timeout);
        HttpLock();
        HttpServiceAll();
        HttpUnlock();
    }
}

void HttpNetInitialize(void)
{
    KeInitializeEvent(&g_work, SynchronizationEvent, FALSE);
    g_bounce = ExAllocatePool2(POOL_FLAG_NON_PAGED, HTTP_IO_CHUNK, KI_TAG('H', 't', 'B', 'n'));
    if (!g_bounce) {
        KiLogPrintf("http has no bounce buffer");
        return;
    }
    NTSTATUS status = PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr);
    if (!NT_SUCCESS(status))
        KiLogPrintf("http has no worker: %08x", status);
}
