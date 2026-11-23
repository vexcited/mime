#include "transfer.h"
#include <mime/win32/winsock2.h>

#define INET_E_INVALID_URL_ ((HRESULT)0x800C0002L)
#define READ_CHUNK (256 * 1024)

HINTERNET XferOpenSession(const WCHAR *agent, DWORD access, DWORD timeout)
{
    HINTERNET session = WinHttpOpen(agent, access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session && timeout)
        WinHttpSetTimeouts(session, (int)timeout, (int)timeout, (int)timeout, (int)timeout);
    return session;
}

/* the url must parse as scheme://host, and the scheme be one http carries. */
HRESULT XferCheckUrl(const WCHAR *url)
{
    if (!url || !*url)
        return INET_E_INVALID_URL_;
    const WCHAR *p = url;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' ||
           *p == '.')
        p++;
    if (p == url || p[0] != ':' || p[1] != '/' || p[2] != '/' || !p[3] || p[3] == '/')
        return INET_E_INVALID_URL_;
    size_t scheme = (size_t)(p - url);
    if (!((scheme == 4 && !_wcsnicmp(url, L"http", 4)) || (scheme == 5 && !_wcsnicmp(url, L"https", 5))))
        return HRESULT_FROM_WIN32(ERROR_WINHTTP_UNRECOGNIZED_SCHEME);
    URL_COMPONENTS parts = {sizeof(parts)};
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, 0, 0, &parts) || !parts.dwHostNameLength)
        return INET_E_INVALID_URL_;
    return S_OK;
}

static WCHAR *query_text(HINTERNET request, DWORD level, const WCHAR *name)
{
    DWORD size = 0;
    WinHttpQueryHeaders(request, level, name, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
        return nullptr;
    WCHAR *text = LocalAlloc(LMEM_FIXED, size + sizeof(WCHAR));
    if (text && !WinHttpQueryHeaders(request, level, name, text, &size, WINHTTP_NO_HEADER_INDEX)) {
        LocalFree(text);
        return nullptr;
    }
    if (text)
        text[size / sizeof(WCHAR)] = 0;
    return text;
}

static BOOL query_number(HINTERNET request, DWORD level, UINT64 *value)
{
    WCHAR *text = query_text(request, level, WINHTTP_HEADER_NAME_BY_INDEX);
    if (!text)
        return FALSE;
    UINT64 n = 0;
    const WCHAR *p = text;
    while (*p == ' ')
        p++;
    BOOL digits = FALSE;
    for (; *p >= '0' && *p <= '9'; p++) {
        n = n * 10 + (UINT64)(*p - '0');
        digits = TRUE;
    }
    LocalFree(text);
    *value = n;
    return digits;
}

static const WCHAR *number(const WCHAR *p, UINT64 *value, BOOL *ok)
{
    UINT64 n = 0;
    *ok = FALSE;
    for (; *p >= '0' && *p <= '9'; p++) {
        n = n * 10 + (UINT64)(*p - '0');
        *ok = TRUE;
    }
    *value = n;
    return p;
}

/* Content-Range: bytes first-last/total, the total * when the server does not know it. */
static void parse_range(const WCHAR *text, XFER_RESULT *r)
{
    const WCHAR *p = text;
    while (*p == ' ')
        p++;
    if (_wcsnicmp(p, L"bytes", 5))
        return;
    p += 5;
    while (*p == ' ')
        p++;
    BOOL ok;
    p = number(p, &r->range_first, &ok);
    if (!ok || *p++ != '-')
        return;
    p = number(p, &r->range_last, &ok);
    if (!ok || *p++ != '/')
        return;
    r->has_range = TRUE;
    if (*p == '*')
        return;
    p = number(p, &r->total, &ok);
    if (!ok)
        r->total = XFER_TO_END;
}

static void read_response(HINTERNET request, XFER_RESULT *r)
{
    DWORD size = sizeof(r->status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &r->status, &size, WINHTTP_NO_HEADER_INDEX);
    r->headers = query_text(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX);
    r->has_length = query_number(request, WINHTTP_QUERY_CONTENT_LENGTH, &r->length);
    WCHAR *text = query_text(request, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX);
    if (text) {
        parse_range(text, r);
        LocalFree(text);
    }
    text = query_text(request, WINHTTP_QUERY_TRANSFER_ENCODING, WINHTTP_HEADER_NAME_BY_INDEX);
    if (text) {
        r->chunked = wcsstr(text, L"chunked") != nullptr;
        LocalFree(text);
    }
    text = query_text(request, WINHTTP_QUERY_ACCEPT_RANGES, WINHTTP_HEADER_NAME_BY_INDEX);
    if (text) {
        r->accept_ranges = !_wcsicmp(text, L"bytes");
        LocalFree(text);
    }
    text = query_text(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX);
    if (text) {
        r->html = wcsstr(text, L"text/html") != nullptr;
        LocalFree(text);
    }
    UINT64 after = 0;
    if (query_number(request, WINHTTP_QUERY_RETRY_AFTER, &after))
        r->retry_after = (DWORD)(after > 86400 ? 86400 : after);
    if (!r->has_range && r->has_length && (r->status == 200 || r->status == 203))
        r->total = r->length;
    DWORD length = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &length);
    if (length) {
        r->final_url = LocalAlloc(LMEM_FIXED, length + sizeof(WCHAR));
        if (r->final_url && !WinHttpQueryOption(request, WINHTTP_OPTION_URL, r->final_url, &length)) {
            LocalFree(r->final_url);
            r->final_url = nullptr;
        } else if (r->final_url)
            r->final_url[length / sizeof(WCHAR)] = 0;
    }
    struct {
        DWORD cbSize;
        SOCKADDR_STORAGE LocalAddress;
        SOCKADDR_STORAGE RemoteAddress;
    } info = {sizeof(info)};
    length = sizeof(info);
    if (WinHttpQueryOption(request, WINHTTP_OPTION_CONNECTION_INFO, &info, &length)) {
        const SOCKADDR_STORAGE *a = &info.RemoteAddress;
        const BYTE *b = (const BYTE *)a;
        if (a->ss_family == 2)
            _snwprintf(r->address, ARRAYSIZE(r->address), L"%u.%u.%u.%u", b[4], b[5], b[6], b[7]);
        else if (a->ss_family == 23) {
            const USHORT *w = (const USHORT *)(b + 8);
            _snwprintf(r->address, ARRAYSIZE(r->address), L"%x:%x:%x:%x:%x:%x:%x:%x", __builtin_bswap16(w[0]), __builtin_bswap16(w[1]),
                      __builtin_bswap16(w[2]), __builtin_bswap16(w[3]), __builtin_bswap16(w[4]), __builtin_bswap16(w[5]),
                      __builtin_bswap16(w[6]), __builtin_bswap16(w[7]));
        }
    }
}

static HRESULT last_error(void)
{
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static BOOL cancelled(const XFER_REQUEST *q)
{
    return q->cancel && WaitForSingleObject(q->cancel, 0) == WAIT_OBJECT_0;
}

HRESULT XferRun(HINTERNET session, const XFER_REQUEST *q, XFER_RESULT *r)
{
    memset(r, 0, sizeof(*r));
    r->total = XFER_TO_END;
    WCHAR host[INTERNET_MAX_HOST_NAME_LENGTH + 1];
    URL_COMPONENTS parts = {sizeof(parts)};
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(q->url, 0, 0, &parts))
        return last_error();
    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connect)
        return last_error();
    size_t path_length = parts.dwUrlPathLength + parts.dwExtraInfoLength;
    WCHAR *path = LocalAlloc(LMEM_FIXED, (path_length + 2) * sizeof(WCHAR));
    if (!path) {
        WinHttpCloseHandle(connect);
        return E_OUTOFMEMORY;
    }
    if (path_length)
        memcpy(path, parts.lpszUrlPath ? parts.lpszUrlPath : parts.lpszExtraInfo, path_length * sizeof(WCHAR));
    else
        path[path_length++] = '/';
    path[path_length] = 0;
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    static const WCHAR *accept[] = {L"*/*", nullptr};
    HINTERNET request = WinHttpOpenRequest(connect, q->verb ? q->verb : L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           accept, flags);
    LocalFree(path);
    HRESULT hr = S_OK;
    BYTE *buffer = nullptr;
    if (!request) {
        hr = last_error();
        WinHttpCloseHandle(connect);
        return hr;
    }
    if (q->active)
        InterlockedExchangePointer((void *volatile *)q->active, request);
    if (cancelled(q)) {
        hr = E_ABORT;
        goto done;
    }
    if (q->security_flags)
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, (void *)&q->security_flags, sizeof(DWORD));
    if (q->revocation) {
        DWORD feature = WINHTTP_ENABLE_SSL_REVOCATION;
        WinHttpSetOption(request, WINHTTP_OPTION_ENABLE_FEATURE, &feature, sizeof(feature));
    }
    if (q->has_redirect_policy)
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, (void *)&q->redirect_policy, sizeof(DWORD));
    if (q->no_redirects) {
        DWORD feature = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
    }
    if (q->has_autologon)
        WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, (void *)&q->autologon, sizeof(DWORD));
    if (q->proxy)
        WinHttpSetOption(request, WINHTTP_OPTION_PROXY, (void *)q->proxy, sizeof(*q->proxy));
    if (q->client_cert)
        WinHttpSetOption(request, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, (void *)q->client_cert, q->client_cert_size);
    if (q->identity)
        WinHttpAddRequestHeaders(request, L"Accept-encoding: identity", (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    if (q->range) {
        WCHAR range[80];
        if (q->length == XFER_TO_END)
            _snwprintf(range, ARRAYSIZE(range), L"Range: bytes=%I64u-\r\n", q->offset);
        else
            _snwprintf(range, ARRAYSIZE(range), L"Range: bytes=%I64u-%I64u\r\n", q->offset, q->offset + q->length - 1);
        WinHttpAddRequestHeaders(request, range, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    if (q->headers && *q->headers)
        WinHttpAddRequestHeaders(request, q->headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (q->read || q->body_length) {
        buffer = LocalAlloc(LMEM_FIXED, READ_CHUNK);
        if (!buffer) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
    }
    DWORD used_schemes = 0;
    for (;;) {
        DWORD total = q->body_length > 0xFFFFFFFEull ? WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH : (DWORD)q->body_length;
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, total, 0)) {
            hr = cancelled(q) ? E_ABORT : last_error();
            goto done;
        }
        for (UINT64 sent = 0; sent < q->body_length;) {
            if (cancelled(q)) {
                hr = E_ABORT;
                goto done;
            }
            DWORD want = q->body_length - sent > READ_CHUNK ? READ_CHUNK : (DWORD)(q->body_length - sent), got = 0;
            if (q->read)
                hr = q->read(q->context, q->body_offset + sent, buffer, want, &got);
            else
                memset(buffer, 0, got = want);
            if (FAILED(hr))
                goto done;
            if (!got) {
                hr = HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
                goto done;
            }
            DWORD written = 0;
            if (!WinHttpWriteData(request, buffer, got, &written)) {
                hr = cancelled(q) ? E_ABORT : last_error();
                goto done;
            }
            sent += got;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            hr = cancelled(q) ? E_ABORT : last_error();
            goto done;
        }
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if ((status != 401 && status != 407) || !q->credential_count || r->auth_tries >= XFER_MAX_AUTH_TRIES)
            break;
        DWORD supported = 0, first = 0, target = 0;
        if (!WinHttpQueryAuthSchemes(request, &supported, &first, &target))
            break;
        /* the scheme the server prefers when the caller gave one for it, else any other it offers, each once. */
        const XFER_CREDENTIAL *pick = nullptr;
        for (DWORD i = 0; i < q->credential_count; i++) {
            const XFER_CREDENTIAL *c = &q->credentials[i];
            if (c->target != target || !(supported & c->scheme) || (used_schemes & c->scheme))
                continue;
            if (!pick || c->scheme == first)
                pick = c;
        }
        if (!pick || !WinHttpSetCredentials(request, target, pick->scheme, pick->user, pick->password, nullptr))
            break;
        used_schemes |= pick->scheme;
        r->auth_tries++;
    }
    read_response(request, r);
    if (q->headers_ready)
        q->headers_ready(q->context, r);
    if (r->status < 200 || r->status >= 300 || !q->write || (q->verb && !wcscmp(q->verb, L"HEAD")))
        goto done;
    if (!buffer)
        buffer = LocalAlloc(LMEM_FIXED, READ_CHUNK);
    if (!buffer) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    UINT64 at = r->has_range ? r->range_first : 0;
    for (;;) {
        if (cancelled(q)) {
            hr = E_ABORT;
            break;
        }
        DWORD got = 0;
        if (!WinHttpReadData(request, buffer, READ_CHUNK, &got)) {
            hr = cancelled(q) ? E_ABORT : last_error();
            break;
        }
        if (!got)
            break;
        hr = q->write(q->context, at, buffer, got);
        if (FAILED(hr))
            break;
        at += got;
        r->received += got;
    }
done:
    if (buffer)
        LocalFree(buffer);
    /* whoever takes the handle out of the slot closes it, the aborting thread or this one. */
    if (!q->active || InterlockedCompareExchangePointer((void *volatile *)q->active, nullptr, request) == request)
        WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    return hr;
}

void XferAbort(HINTERNET volatile *active)
{
    HINTERNET request = InterlockedExchangePointer((void *volatile *)active, nullptr);
    if (request)
        WinHttpCloseHandle(request);
}

/* the value of the first header of that name in raw CRLF headers, trimmed, or null. */
WCHAR *XferHeader(const WCHAR *headers, const WCHAR *name)
{
    if (!headers)
        return nullptr;
    size_t length = wcslen(name);
    const WCHAR *line = wcschr(headers, '\n');
    while (line) {
        line++;
        const WCHAR *end = line;
        while (*end && *end != '\r' && *end != '\n')
            end++;
        if ((size_t)(end - line) > length && line[length] == ':' && !_wcsnicmp(line, name, length)) {
            const WCHAR *value = line + length + 1;
            while (value < end && (*value == ' ' || *value == '\t'))
                value++;
            const WCHAR *stop = end;
            while (stop > value && (stop[-1] == ' ' || stop[-1] == '\t'))
                stop--;
            WCHAR *copy = LocalAlloc(LMEM_FIXED, ((size_t)(stop - value) + 1) * sizeof(WCHAR));
            if (copy) {
                memcpy(copy, value, (size_t)(stop - value) * sizeof(WCHAR));
                copy[stop - value] = 0;
            }
            return copy;
        }
        line = *end ? wcschr(end, '\n') : nullptr;
    }
    return nullptr;
}

void XferFreeResult(XFER_RESULT *r)
{
    if (r->headers)
        LocalFree(r->headers);
    if (r->final_url)
        LocalFree(r->final_url);
    r->headers = nullptr;
    r->final_url = nullptr;
}
