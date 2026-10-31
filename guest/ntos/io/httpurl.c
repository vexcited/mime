/*
 * the url prefixes of the http server driver: parsing a registration, cooking the url of a request, and finding the
 * registration a request goes to. http.sys keeps its prefixes in trees of sites and path segments; the order of the
 * sites it looks in is what matters, and it is kept here: strong wildcards, addresses, host names, subdomain
 * wildcards, weak wildcards. within a site the longest path prefix wins.
 */
#include "http_p.h"

static BOOLEAN host_char(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

static BOOLEAN parse_v4(const WCHAR *s, ULONG n, UCHAR out[4])
{
    ULONG part = 0, count = 0, digits = 0;
    for (ULONG i = 0; i <= n; i++) {
        WCHAR c = i < n ? s[i] : '.';
        if (c >= '0' && c <= '9') {
            part = part * 10 + (c - '0');
            if (++digits > 3 || part > 255)
                return FALSE;
        } else if (c == '.' && digits) {
            if (count == 4)
                return FALSE;
            out[count++] = (UCHAR)part;
            part = digits = 0;
        } else {
            return FALSE;
        }
    }
    return count == 4;
}

static int hex_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOLEAN parse_v6(const WCHAR *s, ULONG n, UCHAR out[16])
{
    USHORT words[8] = {};
    ULONG count = 0;
    int gap = -1;
    ULONG i = 0;
    if (n >= 2 && s[0] == ':' && s[1] == ':') {
        gap = 0;
        i = 2;
    } else if (n && s[0] == ':') {
        return FALSE;
    }
    while (i < n) {
        ULONG digits = 0, value = 0;
        while (i < n && hex_value(s[i]) >= 0) {
            value = value * 16 + hex_value(s[i]);
            if (++digits > 4)
                return FALSE;
            i++;
        }
        if (!digits || count == 8)
            return FALSE;
        words[count++] = (USHORT)value;
        if (i == n)
            break;
        if (s[i] != ':')
            return FALSE;
        i++;
        if (i < n && s[i] == ':') {
            if (gap >= 0)
                return FALSE;
            gap = (int)count;
            i++;
            if (i == n)
                break;
        } else if (i == n) {
            return FALSE;
        }
    }
    if (gap < 0 && count != 8)
        return FALSE;
    if (gap >= 0 && count > 7)
        return FALSE;
    USHORT full[8] = {};
    ULONG tail = count - (gap >= 0 ? (ULONG)gap : count);
    for (ULONG k = 0; k < (gap >= 0 ? (ULONG)gap : count); k++)
        full[k] = words[k];
    for (ULONG k = 0; k < tail; k++)
        full[8 - tail + k] = words[(ULONG)gap + k];
    for (ULONG k = 0; k < 8; k++) {
        out[2 * k] = (UCHAR)(full[k] >> 8);
        out[2 * k + 1] = (UCHAR)full[k];
    }
    return TRUE;
}

ULONG HttpUtf8ToWide(PCSTR in, ULONG length, PWSTR out, ULONG room, BOOLEAN decode_percent)
{
    PUCHAR bytes = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('H', 't', 'U', '8'));
    if (!bytes)
        return 0;
    ULONG n = 0;
    for (ULONG i = 0; i < length; i++) {
        UCHAR c = (UCHAR)in[i];
        if (decode_percent && c == '%' && i + 2 < length && hex_value(in[i + 1]) >= 0 && hex_value(in[i + 2]) >= 0) {
            bytes[n++] = (UCHAR)(hex_value(in[i + 1]) * 16 + hex_value(in[i + 2]));
            i += 2;
        } else {
            bytes[n++] = c;
        }
    }
    ULONG written = 0;
    NTSTATUS status = RtlUTF8ToUnicodeN(out, room * sizeof(WCHAR), &written, (PCCH)bytes, n);
    if (!NT_SUCCESS(status)) {
        /* bytes that are not utf-8 count as latin-1. */
        written = 0;
        for (ULONG i = 0; i < n && i < room; i++)
            out[written++] = bytes[i];
        ExFreePool(bytes);
        return written;
    }
    ExFreePool(bytes);
    return written / sizeof(WCHAR);
}

void HttpFreeUrl(HTTP_URLREG *url)
{
    if (!url)
        return;
    if (url->Host)
        ExFreePool(url->Host);
    if (url->Text)
        ExFreePool(url->Text);
    ExFreePool(url);
}

/* "http://host:port/path/": the prefix the way http.sys keeps it. */
NTSTATUS HttpParseRegistration(PCWSTR text, ULONG chars, HTTP_URLREG **out)
{
    *out = nullptr;
    if (chars < 8 || chars > HTTP_MAX_URL_CHARS)
        return STATUS_INVALID_PARAMETER;
    if (_wcsnicmp(text, L"https://", 8) == 0)
        return STATUS_NOT_SUPPORTED;
    if (_wcsnicmp(text, L"http://", 7) != 0)
        return STATUS_INVALID_PARAMETER;
    ULONG i = 7;
    ULONG host_start = i;
    BOOLEAN bracket = i < chars && text[i] == '[';
    ULONG host_end;
    if (bracket) {
        while (i < chars && text[i] != ']')
            i++;
        if (i == chars)
            return STATUS_INVALID_PARAMETER;
        host_start++;
        host_end = i;
        i++;
    } else {
        while (i < chars && text[i] != ':' && text[i] != '/')
            i++;
        host_end = i;
    }
    ULONG host_chars = host_end - host_start;
    if (!host_chars)
        return STATUS_INVALID_PARAMETER;
    ULONG port = 80;
    if (i < chars && text[i] == ':') {
        i++;
        ULONG digits = 0;
        port = 0;
        while (i < chars && text[i] >= '0' && text[i] <= '9') {
            port = port * 10 + (text[i] - '0');
            if (port > 65535)
                return STATUS_INVALID_PARAMETER;
            digits++;
            i++;
        }
        if (!digits || !port)
            return STATUS_INVALID_PARAMETER;
    }
    if (i >= chars || text[i] != '/')
        return STATUS_INVALID_PARAMETER;
    ULONG path_start = i;

    HTTP_URLREG *url = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*url), KI_TAG('H', 't', 'U', 'r'));
    if (!url)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(url, 0, sizeof(*url));
    InitializeListHead(&url->GroupLinks);
    InitializeListHead(&url->Links);
    url->Port = (USHORT)port;
    url->Host = ExAllocatePool2(POOL_FLAG_NON_PAGED, (host_chars + 1) * sizeof(WCHAR), KI_TAG('H', 't', 'U', 'h'));
    url->Text = ExAllocatePool2(POOL_FLAG_NON_PAGED, (chars + 40) * sizeof(WCHAR), KI_TAG('H', 't', 'U', 't'));
    if (!url->Host || !url->Text) {
        HttpFreeUrl(url);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (ULONG k = 0; k < host_chars; k++)
        url->Host[k] = towlower(text[host_start + k]);
    url->Host[host_chars] = 0;
    url->HostLength = (USHORT)host_chars;

    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (!bracket && host_chars == 1 && url->Host[0] == '+') {
        url->Kind = HTTP_URL_STRONG;
    } else if (!bracket && host_chars == 1 && url->Host[0] == '*') {
        url->Kind = HTTP_URL_WEAK;
    } else if (bracket) {
        url->Kind = HTTP_URL_IP;
        url->IpFamily = 23;
        if (!parse_v6(url->Host, host_chars, url->Ip))
            goto fail;
    } else if (parse_v4(url->Host, host_chars, url->Ip)) {
        url->Kind = HTTP_URL_IP;
        url->IpFamily = 2;
    } else {
        ULONG from = 0;
        if (host_chars > 2 && url->Host[0] == '*' && url->Host[1] == '.') {
            url->Kind = HTTP_URL_SUBDOMAIN;
            from = 2;
        } else {
            url->Kind = HTTP_URL_HOST;
        }
        for (ULONG k = from; k < host_chars; k++) {
            if (!host_char(url->Host[k]))
                goto fail;
        }
    }

    /* the path, segment by segment, without the empty ones. */
    PWSTR o = url->Text;
    o += _snwprintf(o, chars + 40, L"http://%s%s%s:%u/", bracket ? L"[" : L"", url->Host, bracket ? L"]" : L"", port);
    ULONG count = 0;
    for (ULONG k = path_start; k < chars;) {
        while (k < chars && text[k] == '/')
            k++;
        ULONG s = k;
        while (k < chars && text[k] != '/') {
            if (text[k] == '?' || text[k] == '#')
                goto fail;
            k++;
        }
        if (k == s)
            break;
        if ((k - s == 1 && text[s] == '.') || (k - s == 2 && text[s] == '.' && text[s + 1] == '.'))
            goto fail;
        if (count == 64)
            goto fail;
        url->Segments[count] = (USHORT)(o - url->Text);
        for (ULONG q = s; q < k; q++)
            *o++ = towlower(text[q]);
        url->SegmentLengths[count] = (USHORT)(k - s);
        *o++ = '/';
        count++;
    }
    *o = 0;
    url->SegmentCount = count;
    url->TextLength = (USHORT)(o - url->Text);
    *out = url;
    return STATUS_SUCCESS;
fail:
    HttpFreeUrl(url);
    return status;
}

BOOLEAN HttpSameUrl(const HTTP_URLREG *a, const HTTP_URLREG *b)
{
    return a->TextLength == b->TextLength && !_wcsnicmp(a->Text, b->Text, a->TextLength);
}

/* the path of a request, cut into segments in place of a copy. */
static ULONG split_path(PCWSTR path, ULONG chars, USHORT starts[64], USHORT lengths[64])
{
    ULONG count = 0;
    for (ULONG k = 0; k < chars;) {
        while (k < chars && path[k] == '/')
            k++;
        ULONG s = k;
        while (k < chars && path[k] != '/')
            k++;
        if (k == s)
            break;
        if (count == 64)
            return 0xFFFF;
        starts[count] = (USHORT)s;
        lengths[count++] = (USHORT)(k - s);
    }
    return count;
}

static BOOLEAN host_matches(const HTTP_URLREG *url, PCWSTR host, ULONG host_chars)
{
    if (url->Kind == HTTP_URL_HOST)
        return url->HostLength == host_chars && !_wcsnicmp(url->Host, host, host_chars);
    if (url->Kind == HTTP_URL_SUBDOMAIN) {
        ULONG suffix = url->HostLength - 1; /* ".example.com" */
        return host_chars > suffix && !_wcsnicmp(host + host_chars - suffix, url->Host + 1, suffix);
    }
    return FALSE;
}

static BOOLEAN ip_matches(const HTTP_URLREG *url, const MimeNetAddr *local)
{
    if (url->IpFamily != local->family)
        return FALSE;
    if (local->family == 2)
        return !memcmp(url->Ip, local->data + 2, 4);
    return !memcmp(url->Ip, local->data + 6, 16);
}

HTTP_URLREG *HttpMatchUrl(const HTTP_REQ *req, const MimeNetAddr *local, USHORT port)
{
    /* the host without its port. */
    PCWSTR host = req->Host;
    ULONG host_chars = req->HostLength / sizeof(WCHAR);
    if (host_chars && host[0] == '[') {
        for (ULONG k = 1; k < host_chars; k++) {
            if (host[k] == ']') {
                host_chars = k + 1;
                break;
            }
        }
    } else {
        for (ULONG k = host_chars; k > 0; k--) {
            if (host[k - 1] == ':') {
                host_chars = k - 1;
                break;
            }
        }
    }
    USHORT starts[64], lengths[64];
    ULONG segments = split_path(req->AbsPath, req->AbsPathLength / sizeof(WCHAR), starts, lengths);
    static const ULONG order[] = {HTTP_URL_STRONG, HTTP_URL_IP, HTTP_URL_HOST, HTTP_URL_SUBDOMAIN, HTTP_URL_WEAK};
    for (ULONG pass = 0; pass < RTL_NUMBER_OF(order); pass++) {
        HTTP_URLREG *best = nullptr;
        for (PLIST_ENTRY e = HttpUrls.Flink; e != &HttpUrls; e = e->Flink) {
            HTTP_URLREG *url = CONTAINING_RECORD(e, HTTP_URLREG, Links);
            if (url->Kind != order[pass] || url->Port != port)
                continue;
            if (url->Kind == HTTP_URL_IP && !ip_matches(url, local))
                continue;
            if ((url->Kind == HTTP_URL_HOST || url->Kind == HTTP_URL_SUBDOMAIN) && !host_matches(url, host, host_chars))
                continue;
            if (url->SegmentCount > segments)
                continue;
            BOOLEAN ok = TRUE;
            for (ULONG k = 0; k < url->SegmentCount && ok; k++) {
                ok = url->SegmentLengths[k] == lengths[k] &&
                     !_wcsnicmp(url->Text + url->Segments[k], req->AbsPath + starts[k], lengths[k]);
            }
            if (ok && (!best || url->SegmentCount > best->SegmentCount))
                best = url;
        }
        if (best)
            return best;
    }
    return nullptr;
}

/* removes "." and ".." and turns backslashes into slashes, in place. returns the new length. */
static ULONG canonical_path(PWSTR p, ULONG n)
{
    for (ULONG i = 0; i < n; i++)
        if (p[i] == '\\')
            p[i] = '/';
    ULONG out = 0;
    for (ULONG i = 0; i < n;) {
        ULONG t = i + 1;
        ULONG j = t;
        while (j < n && p[j] != '/')
            j++;
        ULONG len = j - t;
        if (len == 1 && p[t] == '.') {
            if (j == n)
                p[out++] = '/';
        } else if (len == 2 && p[t] == '.' && p[t + 1] == '.') {
            while (out && p[out - 1] != '/')
                out--;
            if (out)
                out--;
            if (j == n)
                p[out++] = '/';
        } else {
            p[out++] = '/';
            for (ULONG k = 0; k < len; k++)
                p[out++] = p[t + k];
        }
        i = j;
    }
    if (!out)
        p[out++] = '/';
    return out;
}

/* req->RawUrl and the host header become the cooked parts of the request. */
NTSTATUS HttpCookUrl(HTTP_REQ *req, const MimeNetAddr *local, USHORT port, PCSTR host_header, ULONG host_header_length)
{
    PCSTR raw = req->RawUrl;
    ULONG raw_length = req->RawUrlLength;
    PCSTR authority = host_header;
    ULONG authority_length = host_header_length;
    PCSTR path = raw;
    ULONG path_length = raw_length;
    if (raw_length >= 7 && !_strnicmp(raw, "http://", 7)) {
        authority = raw + 7;
        ULONG k = 0;
        while (7 + k < raw_length && raw[7 + k] != '/' && raw[7 + k] != '?')
            k++;
        authority_length = k;
        path = raw + 7 + k;
        path_length = raw_length - 7 - k;
        if (!path_length || path[0] == '?') {
            /* the path of an absolute uri that names none is a slash. */
        }
    } else if (raw_length && raw[0] != '/' && !(raw_length == 1 && raw[0] == '*')) {
        return STATUS_INVALID_PARAMETER;
    }
    /* the query. */
    ULONG query_at = path_length;
    for (ULONG k = 0; k < path_length; k++) {
        if (path[k] == '?') {
            query_at = k;
            break;
        }
    }
    ULONG pc = query_at;
    ULONG qc = path_length - query_at;
    /* the host: from the authority, or the address the client connected to. */
    WCHAR host[300];
    ULONG hc = 0;
    if (authority_length) {
        PCSTR a = authority;
        ULONG n = authority_length;
        for (ULONG k = 0; k < n; k++)
            if (a[k] == '@') {
                a += k + 1;
                n -= k + 1;
                break;
            }
        if (!n || n > 255)
            return STATUS_INVALID_PARAMETER;
        for (ULONG k = 0; k < n; k++) {
            UCHAR c = (UCHAR)a[k];
            if (c <= 32 || c >= 127 || c == '/' || c == '\\' || c == '?' || c == '#')
                return STATUS_INVALID_PARAMETER;
            host[hc++] = c;
        }
    } else {
        if (req->VerMajor > 1 || (req->VerMajor == 1 && req->VerMinor >= 1))
            return STATUS_INVALID_PARAMETER;
        if (local->family == 2)
            hc = _snwprintf(host, 300, L"%u.%u.%u.%u", local->data[2], local->data[3], local->data[4], local->data[5]);
        else
            hc = _snwprintf(host, 300, L"[::1]");
    }
    /* a host without a port gets the one the connection came in on. */
    BOOLEAN has_port = FALSE;
    ULONG from = 0;
    if (hc && host[0] == '[') {
        while (from < hc && host[from] != ']')
            from++;
    }
    for (ULONG k = from; k < hc; k++)
        if (host[k] == ':')
            has_port = TRUE;
    if (!has_port)
        hc += _snwprintf(host + hc, 300 - hc, L":%u", port);

    ULONG path_room = pc + 2;
    ULONG total = 7 + hc + path_room + qc + 1;
    PWSTR full = ExAllocatePool2(POOL_FLAG_NON_PAGED, total * sizeof(WCHAR), KI_TAG('H', 't', 'C', 'k'));
    if (!full)
        return STATUS_INSUFFICIENT_RESOURCES;
    PWSTR o = full;
    memcpy(o, L"http://", 7 * sizeof(WCHAR));
    o += 7;
    PWSTR host_at = o;
    memcpy(o, host, hc * sizeof(WCHAR));
    o += hc;
    PWSTR path_at = o;
    ULONG written;
    if (pc == 1 && path[0] == '*') {
        *o++ = '*';
        written = 1;
    } else if (pc == 0) {
        *o++ = '/';
        written = 1;
    } else {
        written = HttpUtf8ToWide(path, pc, o, pc + 1, TRUE);
        written = canonical_path(o, written);
        o += written;
    }
    PWSTR query_at_w = o;
    for (ULONG k = 0; k < qc; k++)
        *o++ = (UCHAR)path[query_at + k];
    *o = 0;
    req->FullUrl = full;
    req->FullUrlLength = (USHORT)((o - full) * sizeof(WCHAR));
    req->Host = host_at;
    req->HostLength = (USHORT)(hc * sizeof(WCHAR));
    req->AbsPath = path_at;
    req->AbsPathLength = (USHORT)(written * sizeof(WCHAR));
    req->Query = query_at_w;
    req->QueryLength = (USHORT)(qc * sizeof(WCHAR));
    return STATUS_SUCCESS;
}
