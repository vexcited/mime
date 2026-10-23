/* HttpAddRequestHeaders and HttpQueryInfo use ANSI header lists; wide APIs wrap the ANSI implementation. */
#include "wininet_private.h"

/* the header of each HttpQueryInfo level and the modifier flags it accepts. */
typedef struct {
    const char *Name;
    DWORD Allowed;
} INET_LEVEL;

#define REQ HTTP_QUERY_FLAG_REQUEST_HEADERS
#define TIME (REQ | HTTP_QUERY_FLAG_SYSTEMTIME)

static const INET_LEVEL Levels[HTTP_QUERY_MAX + 1] = {
    {"Mime-Version", REQ},
    {"Content-Type", REQ},
    {"Content-Transfer-Encoding", REQ},
    {"Content-Id", REQ},
    {"Content-Description", REQ},
    {"Content-Length", REQ | HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_FLAG_NUMBER64},
    {"Content-Language", REQ},
    {"Allow", REQ},
    {"Public", REQ},
    {"Date", TIME},
    {"Expires", REQ},
    {"Last-Modified", TIME},
    {"Message-id", REQ},
    {"Uri", REQ},
    {"Derived-From", REQ},
    {"Cost", REQ},
    {"Link", REQ},
    {"Pragma", REQ},
    {nullptr, REQ},
    {nullptr, HTTP_QUERY_FLAG_NUMBER},
    {nullptr, 0},
    {nullptr, REQ},
    {nullptr, REQ},
    {"Connection", REQ},
    {"Accept", REQ},
    {"Accept-Charset", REQ},
    {"Accept-Encoding", REQ},
    {"Accept-Language", REQ},
    {"Authorization", REQ},
    {"Content-Encoding", REQ},
    {"Forwarded", REQ},
    {"From", REQ},
    {"If-Modified-Since", TIME},
    {"Location", REQ},
    {"Orig-Uri", REQ},
    {"Referer", REQ},
    {"Retry-After", TIME},
    {"Server", REQ},
    {"Title", REQ},
    {"User-Agent", REQ},
    {"WWW-Authenticate", REQ},
    {"Proxy-Authenticate", REQ},
    {"Accept-Ranges", REQ},
    {"Set-Cookie", REQ},
    {"Cookie", REQ},
    {nullptr, REQ},
    {"Refresh", 0},
    {"Content-Disposition", REQ},
    {"Age", REQ},
    {"Cache-Control", REQ},
    {"Content-Base", REQ},
    {"Content-Location", REQ},
    {"Content-Md5", REQ},
    {"Content-Range", REQ},
    {"Etag", REQ},
    {"Host", REQ},
    {"If-Match", REQ},
    {"If-None-Match", REQ},
    {"If-Range", REQ},
    {"If-Unmodified-Since", TIME},
    {"Max-Forwards", REQ},
    {"Proxy-Authorization", REQ},
    {"Range", REQ},
    {"Transfer-Encoding", REQ},
    {"Upgrade", REQ},
    {"Vary", REQ},
    {"Via", REQ},
    {"Warning", REQ},
    {"Expect", TIME},
    {"Proxy-Connection", REQ},
    {"Unless-Modified-Since", REQ},
    {"Ms-Echo-Request", 0},
    {"Ms-Echo-Reply", REQ},
    {nullptr, REQ},
    {nullptr, REQ},
    {"Proxy-Support", REQ},
    {"Authentication-Info", REQ},
    {"PassportURLs", REQ},
    {"PassportConfig", REQ},
    {"X-Content-Type-Options", REQ},
    {"P3P", REQ},
    {"X-P2P-PeerDist", REQ},
    {"Translate", REQ},
    {"X-UA-Compatible", REQ},
    {"Default-Style", REQ},
    {"X-Frame-Options", REQ},
    {"X-XSS-Protection", REQ},
    {"Set-Cookie2", REQ},
    {"DNT", REQ},
    {"Keep-Alive", REQ},
    {"HTTP2-Settings", REQ},
    {"Strict-Transport-Security", REQ},
    {"Sec-Token-Binding", REQ},
    {"Include-Referred-Token-Binding-ID", REQ},
    {"Public-Key-Pins", REQ},
    {"Public-Key-Pins-Report-Only", REQ},
};

const char *InetLevelName(DWORD level)
{
    return level <= HTTP_QUERY_MAX ? Levels[level].Name : nullptr;
}

/* header lists. */

void InetHeadersClear(INET_HEADERS *headers)
{
    for (DWORD i = 0; i < headers->Count; i++) {
        InetFree(headers->Items[i].Name);
        InetFree(headers->Items[i].Value);
    }
    InetFree(headers->Items);
    headers->Items = nullptr;
    headers->Count = headers->Capacity = 0;
}

BOOL InetHeadersAppend(INET_HEADERS *headers, const char *name, DWORD name_length, const char *value,
                       DWORD value_length)
{
    if (headers->Count == headers->Capacity) {
        DWORD capacity = headers->Capacity ? headers->Capacity * 2 : 16;
        INET_HEADER *items = InetRealloc(headers->Items, capacity * sizeof(*items));
        if (!items)
            return FALSE;
        headers->Items = items;
        headers->Capacity = capacity;
    }
    char *name_copy = InetDuplicateA(name, name_length), *value_copy = InetDuplicateA(value, value_length);
    if (!name_copy || !value_copy) {
        InetFree(name_copy);
        InetFree(value_copy);
        return FALSE;
    }
    headers->Items[headers->Count].Name = name_copy;
    headers->Items[headers->Count].Value = value_copy;
    headers->Count++;
    return TRUE;
}

static BOOL NameIs(const char *name, const char *other, DWORD other_length)
{
    return strlen(name) == other_length && !_strnicmp(name, other, other_length);
}

/* return the nth matching header position, or UINT32_MAX. */
static DWORD FindHeader(const INET_HEADERS *headers, const char *name, DWORD name_length, DWORD nth)
{
    for (DWORD i = 0; i < headers->Count; i++)
        if (NameIs(headers->Items[i].Name, name, name_length) && !nth--)
            return i;
    return UINT32_MAX;
}

const char *InetHeadersFind(const INET_HEADERS *headers, const char *name, DWORD index)
{
    DWORD at = FindHeader(headers, name, (DWORD)strlen(name), index);
    return at == UINT32_MAX ? nullptr : headers->Items[at].Value;
}

static void RemoveAt(INET_HEADERS *headers, DWORD index)
{
    InetFree(headers->Items[index].Name);
    InetFree(headers->Items[index].Value);
    headers->Count--;
    memmove(headers->Items + index, headers->Items + index + 1, (headers->Count - index) * sizeof(INET_HEADER));
}

void InetHeadersRemove(INET_HEADERS *headers, const char *name)
{
    DWORD at;
    while ((at = FindHeader(headers, name, (DWORD)strlen(name), 0)) != UINT32_MAX)
        RemoveAt(headers, at);
}

static BOOL IsSpace(char c)
{
    return c == ' ' || c == '\t';
}

/* applies one "Name: value" line with the HttpAddRequestHeaders modifiers. */
static DWORD ProcessHeader(INET_HEADERS *headers, const char *line, DWORD length, DWORD modifiers)
{
    DWORD colon = 0;
    while (colon < length && line[colon] != ':')
        colon++;
    DWORD name_length = colon;
    while (name_length && IsSpace(line[name_length - 1]))
        name_length--;
    if (colon == length || !name_length)
        return ERROR_INVALID_PARAMETER;
    DWORD start = colon + 1, end = length;
    while (start < end && IsSpace(line[start]))
        start++;
    while (end > start && IsSpace(line[end - 1]))
        end--;
    const char *value = line + start;
    DWORD value_length = end - start;
    DWORD index = FindHeader(headers, line, name_length, 0);
    DWORD coalesce = modifiers & (HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA | HTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON);
    if (index == UINT32_MAX) {
        /* replacing a missing header with an empty value removes nothing. */
        if ((modifiers & HTTP_ADDREQ_FLAG_REPLACE) && !value_length)
            return ERROR_SUCCESS;
        return InetHeadersAppend(headers, line, name_length, value, value_length) ? ERROR_SUCCESS
                                                                                  : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (modifiers & HTTP_ADDREQ_FLAG_ADD_IF_NEW)
        return ERROR_HTTP_HEADER_ALREADY_EXISTS;
    if (modifiers & HTTP_ADDREQ_FLAG_REPLACE) {
        RemoveAt(headers, index);
        if (!value_length)
            return ERROR_SUCCESS;
        return InetHeadersAppend(headers, line, name_length, value, value_length) ? ERROR_SUCCESS
                                                                                  : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (coalesce) {
        char *old = headers->Items[index].Value;
        DWORD old_length = (DWORD)strlen(old);
        char *merged = InetAlloc((SIZE_T)old_length + value_length + 3);
        if (!merged)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(merged, old, old_length);
        merged[old_length] = coalesce & HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA ? ',' : ';';
        merged[old_length + 1] = ' ';
        memcpy(merged + old_length + 2, value, value_length);
        merged[old_length + 2 + value_length] = 0;
        headers->Items[index].Value = merged;
        InetFree(old);
        return ERROR_SUCCESS;
    }
    return InetHeadersAppend(headers, line, name_length, value, value_length) ? ERROR_SUCCESS
                                                                              : ERROR_NOT_ENOUGH_MEMORY;
}

DWORD InetAddRequestHeaders(INET_OBJECT *request, const char *text, DWORD length, DWORD modifiers)
{
    DWORD error = ERROR_SUCCESS;
    AcquireSRWLockExclusive(&request->Lock);
    for (DWORD at = 0; at < length && !error;) {
        DWORD end = at;
        while (end < length && text[end] != '\r' && text[end] != '\n' && text[end])
            end++;
        if (end > at)
            error = ProcessHeader(&request->Request, text + at, end - at, modifiers);
        if (end < length && !text[end])
            break;
        at = end + 1;
    }
    ReleaseSRWLockExclusive(&request->Lock);
    return error;
}

DECLSPEC_EXPORT BOOL WINAPI HttpAddRequestHeadersA(HINTERNET request, LPCSTR headers, DWORD length, DWORD modifiers)
{
    INET_OBJECT *object = InetReferenceType(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
    if (!object)
        return FALSE;
    if (!headers || !length) {
        InetRelease(object);
        return InetFail(ERROR_INVALID_PARAMETER);
    }
    if (length == (DWORD)-1)
        length = (DWORD)strlen(headers);
    DWORD error = InetAddRequestHeaders(object, headers, length, modifiers);
    InetRelease(object);
    return error ? InetFail(error) : TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI HttpAddRequestHeadersW(HINTERNET request, LPCWSTR headers, DWORD length, DWORD modifiers)
{
    INET_OBJECT *object = InetReferenceType(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
    if (!object)
        return FALSE;
    InetRelease(object);
    if (!headers || !length)
        return InetFail(ERROR_INVALID_PARAMETER);
    if (length == (DWORD)-1)
        length = (DWORD)lstrlenW(headers);
    char *narrow = InetNarrow(headers, (int)length);
    if (!narrow)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = HttpAddRequestHeadersA(request, narrow, (DWORD)strlen(narrow), modifiers);
    InetFree(narrow);
    return ok;
}

/* queries. */

/* report length without the terminator on success, including it when the buffer is too small. */
static DWORD ReturnText(const char *text, DWORD length, LPVOID buffer, LPDWORD size)
{
    if (!buffer || *size < length + 1) {
        *size = length + 1;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(buffer, text, length);
    ((char *)buffer)[length] = 0;
    *size = length;
    return ERROR_SUCCESS;
}

/* serialize the start line and headers with CRLF or NUL separators. CRLF blocks end with an empty line. */
static DWORD RawHeaders(const char *first, const INET_HEADERS *headers, BOOL crlf, LPVOID buffer, LPDWORD size)
{
    DWORD separator = crlf ? 2 : 1;
    DWORD length = (DWORD)strlen(first) + separator;
    for (DWORD i = 0; i < headers->Count; i++)
        length += (DWORD)strlen(headers->Items[i].Name) + 2 + (DWORD)strlen(headers->Items[i].Value) + separator;
    if (crlf)
        length += 2;
    if (!buffer || *size <= length) {
        *size = length + 1;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    char *p = buffer;
    const char *end = crlf ? "\r\n" : "";
    p += wsprintfA(p, "%s", first);
    memcpy(p, end, separator);
    p += separator;
    for (DWORD i = 0; i < headers->Count; i++) {
        p += wsprintfA(p, "%s: %s", headers->Items[i].Name, headers->Items[i].Value);
        memcpy(p, end, separator);
        p += separator;
    }
    if (crlf) {
        memcpy(p, "\r\n", 2);
        p += 2;
    }
    *p = 0;
    *size = length;
    return ERROR_SUCCESS;
}

/* parse leading decimal digits as WinINet does. */
static DWORD ParseNumber(const char *value, ULONGLONG *number, BOOL wide)
{
    ULONGLONG v = 0;
    const char *p = value;
    for (; *p >= '0' && *p <= '9'; p++) {
        if (v > (~0ull - (ULONGLONG)(*p - '0')) / 10)
            return ERROR_ARITHMETIC_OVERFLOW;
        v = v * 10 + (ULONGLONG)(*p - '0');
    }
    if (p == value)
        return ERROR_HTTP_INVALID_QUERY_REQUEST;
    if (!wide && v > 0xFFFFFFFF)
        return ERROR_HTTP_INVALID_HEADER;
    *number = v;
    return ERROR_SUCCESS;
}

static DWORD FormatValue(const char *value, DWORD level, LPVOID buffer, LPDWORD size)
{
    if (level & HTTP_QUERY_FLAG_SYSTEMTIME) {
        SYSTEMTIME time;
        if (strlen(value) >= 128)
            return ERROR_INTERNET_INTERNAL_ERROR;
        if (!InetParseTime(value, &time))
            return ERROR_HTTP_INVALID_QUERY_REQUEST;
        if (!buffer || *size < sizeof(time)) {
            *size = sizeof(time);
            return ERROR_INSUFFICIENT_BUFFER;
        }
        memcpy(buffer, &time, sizeof(time));
        *size = sizeof(time);
        return ERROR_SUCCESS;
    }
    if (level & (HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_FLAG_NUMBER64)) {
        BOOL wide = (level & HTTP_QUERY_FLAG_NUMBER64) != 0;
        ULONGLONG number;
        DWORD error = ParseNumber(value, &number, wide);
        if (error)
            return error;
        DWORD bytes = wide ? 8 : 4;
        if (!buffer || *size < bytes) {
            *size = bytes;
            return ERROR_INSUFFICIENT_BUFFER;
        }
        if (wide)
            memcpy(buffer, &number, 8);
        else
            *(DWORD *)buffer = (DWORD)number;
        *size = bytes;
        return ERROR_SUCCESS;
    }
    return ReturnText(value, (DWORD)strlen(value), buffer, size);
}

static DWORD QueryHeader(const INET_HEADERS *headers, const char *name, DWORD name_length, DWORD level,
                         LPVOID buffer, LPDWORD size, LPDWORD index)
{
    if (level & HTTP_QUERY_FLAG_COALESCE_WITH_COMMA) {
        SIZE_T length = 0;
        DWORD count = 0;
        for (DWORD i = 0; i < headers->Count; i++)
            if (NameIs(headers->Items[i].Name, name, name_length)) {
                length += strlen(headers->Items[i].Value) + 2;
                count++;
            }
        if (!count)
            return ERROR_HTTP_HEADER_NOT_FOUND;
        char *joined = InetAlloc(length + 1);
        if (!joined)
            return ERROR_NOT_ENOUGH_MEMORY;
        joined[0] = 0;
        for (DWORD i = 0; i < headers->Count; i++)
            if (NameIs(headers->Items[i].Name, name, name_length)) {
                if (joined[0])
                    lstrcatA(joined, ", ");
                lstrcatA(joined, headers->Items[i].Value);
            }
        DWORD error = FormatValue(joined, level, buffer, size);
        InetFree(joined);
        return error;
    }
    DWORD at = FindHeader(headers, name, name_length, *index);
    if (at == UINT32_MAX)
        return ERROR_HTTP_HEADER_NOT_FOUND;
    DWORD error = FormatValue(headers->Items[at].Value, level, buffer, size);
    if (!error)
        ++*index;
    return error;
}

static DWORD QueryRequest(INET_OBJECT *request, DWORD level, const char *name, DWORD name_length, LPVOID buffer,
                          LPDWORD size, LPDWORD index)
{
    DWORD field = level & 0x3FFFFFF;
    if (field == HTTP_QUERY_RAW_HEADERS || field == HTTP_QUERY_RAW_HEADERS_CRLF ||
        field == HTTP_QUERY_ECHO_HEADERS || field == HTTP_QUERY_ECHO_HEADERS_CRLF) {
        char *verb = InetNarrow(request->Verb, -1), *object = InetNarrow(request->Object, -1);
        char *version = InetNarrow(request->Version, -1);
        char *line = verb && object && version ? InetAlloc(strlen(verb) + strlen(object) + strlen(version) + 3)
                                               : nullptr;
        DWORD error = ERROR_NOT_ENOUGH_MEMORY;
        if (line) {
            wsprintfA(line, "%s %s %s", verb, object, version);
            error = RawHeaders(line, &request->Request,
                               field == HTTP_QUERY_RAW_HEADERS_CRLF || field == HTTP_QUERY_ECHO_HEADERS_CRLF, buffer,
                               size);
        }
        InetFree(line);
        InetFree(verb);
        InetFree(object);
        InetFree(version);
        return error;
    }
    if (field == HTTP_QUERY_VERSION) {
        char *version = InetNarrow(request->Version, -1);
        DWORD error = version ? ReturnText(version, (DWORD)strlen(version), buffer, size) : ERROR_NOT_ENOUGH_MEMORY;
        InetFree(version);
        return error;
    }
    if (field == HTTP_QUERY_STATUS_CODE || field == HTTP_QUERY_STATUS_TEXT)
        return ERROR_HTTP_INVALID_QUERY_REQUEST;
    return QueryHeader(&request->Request, name, name_length, level, buffer, size, index);
}

static DWORD QueryResponse(INET_OBJECT *request, DWORD level, const char *name, DWORD name_length, LPVOID buffer,
                           LPDWORD size, LPDWORD index)
{
    DWORD field = level & 0x3FFFFFF;
    switch (field) {
    case HTTP_QUERY_RAW_HEADERS:
    case HTTP_QUERY_RAW_HEADERS_CRLF:
        return RawHeaders(request->StatusLine ? request->StatusLine : "", &request->Response,
                          field == HTTP_QUERY_RAW_HEADERS_CRLF, buffer, size);
    case HTTP_QUERY_VERSION:
        if (!request->VersionText)
            return ERROR_HTTP_HEADER_NOT_FOUND;
        return ReturnText(request->VersionText, (DWORD)strlen(request->VersionText), buffer, size);
    case HTTP_QUERY_STATUS_CODE:
        if (level & HTTP_QUERY_FLAG_NUMBER) {
            if (!buffer || *size < sizeof(DWORD)) {
                *size = sizeof(DWORD);
                return ERROR_INSUFFICIENT_BUFFER;
            }
            *(DWORD *)buffer = request->StatusCode;
            *size = sizeof(DWORD);
            return ERROR_SUCCESS;
        } else {
            char code[12];
            wsprintfA(code, "%u", request->StatusCode);
            return ReturnText(code, (DWORD)strlen(code), buffer, size);
        }
    case HTTP_QUERY_STATUS_TEXT:
        if (!request->StatusText)
            return ERROR_HTTP_HEADER_NOT_FOUND;
        return ReturnText(request->StatusText, (DWORD)strlen(request->StatusText), buffer, size);
    case HTTP_QUERY_ECHO_HEADERS:
    case HTTP_QUERY_ECHO_HEADERS_CRLF:
        return ERROR_HTTP_INVALID_QUERY_REQUEST;
    }
    return QueryHeader(&request->Response, name, name_length, level, buffer, size, index);
}

/* Windows rejects these modifier combinations. */
static BOOL BadModifiers(DWORD level)
{
    static const DWORD pairs[] = {
        HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_FLAG_NUMBER64,
        HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_FLAG_NUMBER64,
        HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_FLAG_NUMBER,
        HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_FLAG_COALESCE_WITH_COMMA,
        HTTP_QUERY_FLAG_NUMBER64 | HTTP_QUERY_FLAG_COALESCE_WITH_COMMA,
        HTTP_QUERY_FLAG_REQUEST_HEADERS | HTTP_QUERY_FLAG_COALESCE_WITH_COMMA,
        HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_FLAG_COALESCE_WITH_COMMA,
    };
    for (DWORD i = 0; i < ARRAYSIZE(pairs); i++)
        if ((level & pairs[i]) == pairs[i])
            return TRUE;
    return FALSE;
}

DECLSPEC_EXPORT BOOL WINAPI HttpQueryInfoA(HINTERNET request, DWORD level, LPVOID buffer, LPDWORD length,
                                          LPDWORD index)
{
    INET_OBJECT *object = InetReferenceType(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
    if (!object)
        return FALSE;
    DWORD field = level & 0x3FFFFFF, error = ERROR_SUCCESS, zero = 0;
    LPDWORD at = index ? index : &zero;
    const char *name = nullptr;
    DWORD name_length = 0;
    if (!length || (field > HTTP_QUERY_MAX && field != HTTP_QUERY_CUSTOM) || BadModifiers(level)) {
        error = ERROR_INVALID_PARAMETER;
    } else if (*at && (level & HTTP_QUERY_FLAG_COALESCE_WITH_COMMA ||
                       (field >= HTTP_QUERY_VERSION && field <= HTTP_QUERY_RAW_HEADERS_CRLF))) {
        error = ERROR_HTTP_HEADER_NOT_FOUND;
    } else if (field == HTTP_QUERY_CUSTOM) {
        /* names end at a colon or line break; spaces and controls are invalid. */
        name = buffer;
        if (!name) {
            error = ERROR_INVALID_PARAMETER;
        } else {
            while (name[name_length] && name[name_length] != ':' && name[name_length] != '\r' &&
                   name[name_length] != '\n') {
                BYTE c = (BYTE)name[name_length];
                if (c < 0x80 && (c <= ' ' || c == 0x7F))
                    error = ERROR_INVALID_PARAMETER;
                name_length++;
            }
        }
    } else if (level & ~Levels[field].Allowed & 0xF8000000) {
        error = ERROR_HTTP_INVALID_QUERY_REQUEST;
    } else {
        name = Levels[field].Name;
        name_length = name ? (DWORD)strlen(name) : 0;
        if (!buffer)
            *length = 0;
    }
    if (!error) {
        char *custom = nullptr;
        /* a custom name lives in the buffer the answer overwrites. */
        if (field == HTTP_QUERY_CUSTOM && !(custom = InetDuplicateA(name, name_length)))
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (custom)
            name = custom;
        AcquireSRWLockShared(&object->Lock);
        if (error) {
        } else if (field == HTTP_QUERY_REQUEST_METHOD) {
            char *verb = InetNarrow(object->Verb, -1);
            error = verb ? ReturnText(verb, (DWORD)strlen(verb), buffer, length) : ERROR_NOT_ENOUGH_MEMORY;
            InetFree(verb);
        } else if (level & HTTP_QUERY_FLAG_REQUEST_HEADERS) {
            error = QueryRequest(object, level, name, name_length, buffer, length, at);
        } else if (object->State != INET_RECEIVED) {
            error = ERROR_INTERNET_INCORRECT_HANDLE_STATE;
        } else {
            error = QueryResponse(object, level, name, name_length, buffer, length, at);
        }
        ReleaseSRWLockShared(&object->Lock);
        InetFree(custom);
    }
    InetRelease(object);
    SetLastError(error);
    return !error;
}

/*
 * forward to HttpQueryInfoA. numbers and times pass through; widen text in place and report byte length, excluding
 * the terminator on success and including it for a short buffer.
 */
DECLSPEC_EXPORT BOOL WINAPI HttpQueryInfoW(HINTERNET request, DWORD level, LPVOID buffer, LPDWORD length,
                                          LPDWORD index)
{
    if (!length || (*length && !buffer))
        return InetFail(ERROR_INVALID_PARAMETER);
    BOOL binary = (level & (HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_FLAG_NUMBER64)) != 0;
    DWORD capacity = *length;
    char *narrow = nullptr;
    DWORD narrow_capacity = capacity;
    if ((level & 0x3FFFFFF) == HTTP_QUERY_CUSTOM) {
        if (!buffer)
            return InetFail(ERROR_INVALID_PARAMETER);
        DWORD needed = (DWORD)WideCharToMultiByte(CP_ACP, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
        narrow_capacity = max(capacity, needed);
        narrow = InetAlloc(narrow_capacity);
        if (!narrow)
            return InetFail(ERROR_NOT_ENOUGH_MEMORY);
        WideCharToMultiByte(CP_ACP, 0, buffer, -1, narrow, (int)narrow_capacity, nullptr, nullptr);
    } else {
        narrow = InetAlloc(capacity);
        if (!narrow)
            return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    }
    DWORD got = capacity;
    BOOL ok = HttpQueryInfoA(request, level, buffer ? narrow : nullptr, &got, index);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (binary) {
        if (ok)
            memcpy(buffer, narrow, got);
        *length = got;
    } else if (ok) {
        DWORD chars = (DWORD)MultiByteToWideChar(CP_ACP, 0, narrow, (int)got + 1, nullptr, 0);
        *length = chars * sizeof(WCHAR);
        if (*length > capacity) {
            ok = FALSE;
            error = ERROR_INSUFFICIENT_BUFFER;
        } else {
            MultiByteToWideChar(CP_ACP, 0, narrow, (int)got + 1, buffer, (int)chars);
            *length -= sizeof(WCHAR);
        }
    } else if (error == ERROR_INSUFFICIENT_BUFFER) {
        *length = got * sizeof(WCHAR);
    }
    InetFree(narrow);
    SetLastError(error);
    return ok;
}
