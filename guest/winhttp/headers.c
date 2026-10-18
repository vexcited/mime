/*
 * request and response headers: WinHttpAddRequestHeaders and WinHttpQueryHeaders.
 */
#include "winhttp_private.h"

typedef struct {
    const char *Name;
    DWORD Flags;
} WH_KNOWN;

#define RQ WINHTTP_QUERY_FLAG_REQUEST_HEADERS
#define ST WINHTTP_QUERY_FLAG_SYSTEMTIME
#define NU WINHTTP_QUERY_FLAG_NUMBER
#define N6 WINHTTP_QUERY_FLAG_NUMBER64

/* the names of the query levels and the modifier flags each accepts. */
static const WH_KNOWN Known[WINHTTP_QUERY_MAX + 1] = {
    {"Mime-Version", RQ},
    {"Content-Type", RQ},
    {"Content-Transfer-Encoding", RQ},
    {"Content-Id", RQ},
    {"Content-Description", RQ},
    {"Content-Length", RQ | NU | N6},
    {"Content-Language", RQ},
    {"Allow", RQ},
    {"Public", RQ},
    {"Date", RQ | ST},
    {"Expires", RQ | ST},
    {"Last-Modified", RQ | ST},
    {"Message-id", RQ},
    {"Uri", RQ},
    {"Derived-From", RQ},
    {"Cost", RQ},
    {"Link", RQ},
    {"Pragma", RQ},
    {nullptr, RQ},
    {nullptr, NU},
    {nullptr, 0},
    {nullptr, RQ},
    {nullptr, RQ},
    {"Connection", RQ},
    {"Accept", RQ},
    {"Accept-Charset", RQ},
    {"Accept-Encoding", RQ},
    {"Accept-Language", RQ},
    {"Authorization", RQ},
    {"Content-Encoding", RQ},
    {"Forwarded", RQ},
    {"From", RQ},
    {"If-Modified-Since", RQ | ST},
    {"Location", RQ},
    {"Orig-Uri", RQ},
    {"Referer", RQ},
    {"Retry-After", RQ | ST},
    {"Server", RQ},
    {"Title", RQ},
    {"User-Agent", RQ},
    {"WWW-Authenticate", RQ},
    {"Proxy-Authenticate", RQ},
    {"Accept-Ranges", RQ},
    {"Set-Cookie", RQ},
    {"Cookie", RQ},
    {nullptr, RQ},
    {"Refresh", 0},
    {"Content-Disposition", RQ},
    {"Age", RQ},
    {"Cache-Control", RQ},
    {"Content-Base", RQ},
    {"Content-Location", RQ},
    {"Content-Md5", RQ},
    {"Content-Range", RQ},
    {"Etag", RQ},
    {"Host", RQ},
    {"If-Match", RQ},
    {"If-None-Match", RQ},
    {"If-Range", RQ},
    {"If-Unmodified-Since", RQ | ST},
    {"Max-Forwards", RQ},
    {"Proxy-Authorization", RQ},
    {"Range", RQ},
    {"Transfer-Encoding", RQ},
    {"Upgrade", RQ},
    {"Vary", RQ},
    {"Via", RQ},
    {"Warning", RQ},
    {"Expect", RQ | ST},
    {"Proxy-Connection", RQ},
    {"Unless-Modified-Since", RQ},
    {"Ms-Echo-Request", 0},
    {"Ms-Echo-Reply", RQ},
    {nullptr, RQ},
    {nullptr, RQ},
    {"Proxy-Support", RQ},
    {"Authentication-Info", RQ},
    {"PassportURLs", RQ},
    {"PassportConfig", RQ},
};

const char *WhKnownHeaderName(DWORD level)
{
    return level <= WINHTTP_QUERY_MAX ? Known[level].Name : nullptr;
}

/* ------------------------------------------------------------------ header lists */

void WhHeadersClear(WH_HEADERS *headers)
{
    for (DWORD i = 0; i < headers->Count; i++) {
        WhFree(headers->Items[i].Name);
        WhFree(headers->Items[i].Value);
    }
    WhFree(headers->Items);
    headers->Items = nullptr;
    headers->Count = headers->Capacity = 0;
}

BOOL WhHeadersAppend(WH_HEADERS *headers, const char *name, DWORD name_length, const char *value, DWORD value_length)
{
    if (headers->Count == headers->Capacity) {
        DWORD capacity = headers->Capacity ? headers->Capacity * 2 : 16;
        WH_HEADER *items = WhRealloc(headers->Items, capacity * sizeof(*items));
        if (!items)
            return FALSE;
        headers->Items = items;
        headers->Capacity = capacity;
    }
    char *n = WhDupAn(name, name_length), *v = WhDupAn(value, value_length);
    if (!n || !v) {
        WhFree(n);
        WhFree(v);
        return FALSE;
    }
    headers->Items[headers->Count++] = (WH_HEADER){n, v};
    return TRUE;
}

static BOOL NameIs(const char *name, const char *match, DWORD length)
{
    return strlen(name) == length && !_strnicmp(name, match, length);
}

static WH_HEADER *FindHeader(const WH_HEADERS *headers, const char *name, DWORD length, DWORD index)
{
    for (DWORD i = 0; i < headers->Count; i++)
        if (NameIs(headers->Items[i].Name, name, length) && !index--)
            return &headers->Items[i];
    return nullptr;
}

const char *WhHeadersFind(const WH_HEADERS *headers, const char *name, DWORD index)
{
    WH_HEADER *header = FindHeader(headers, name, (DWORD)strlen(name), index);
    return header ? header->Value : nullptr;
}

static void RemoveAt(WH_HEADERS *headers, WH_HEADER *header)
{
    DWORD i = (DWORD)(header - headers->Items);
    WhFree(header->Name);
    WhFree(header->Value);
    memmove(header, header + 1, (headers->Count - i - 1) * sizeof(*header));
    headers->Count--;
}

void WhHeadersRemove(WH_HEADERS *headers, const char *name)
{
    WH_HEADER *header;
    while ((header = FindHeader(headers, name, (DWORD)strlen(name), 0)))
        RemoveAt(headers, header);
}

/* ------------------------------------------------------------------ adding */

static BOOL HeaderNameChar(char c)
{
    return c > 0x20 && c < 0x7F && !strchr("()<>@,;:\\\"/[]?={}", c);
}

typedef struct {
    const char *Name;
    DWORD NameLength;
    const char *Value;
    DWORD ValueLength;
} WH_LINE;

static DWORD ApplyHeader(WH_OBJECT *request, const WH_LINE *line, DWORD modifiers)
{
    DWORD flags = modifiers & 0xFFFF0000, index = modifiers & 0xFFFF;
    if (flags & WINHTTP_ADDREQ_FLAG_REPLACE && NameIs("Content-Length", line->Name, line->NameLength))
        flags |= WINHTTP_ADDREQ_FLAG_ADD;
    WH_HEADERS *headers = &request->Request;
    WH_HEADER *found = FindHeader(headers, line->Name, line->NameLength, index);
    if (flags & WINHTTP_ADDREQ_FLAG_ADD_IF_NEW) {
        if (found)
            return ERROR_WINHTTP_HEADER_ALREADY_EXISTS;
        return WhHeadersAppend(headers, line->Name, line->NameLength, line->Value, line->ValueLength)
                   ? ERROR_SUCCESS
                   : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (flags & WINHTTP_ADDREQ_FLAG_REPLACE) {
        if (found) {
            if (!line->ValueLength) {
                RemoveAt(headers, found);
                return ERROR_SUCCESS;
            }
            char *name = WhDupAn(line->Name, line->NameLength), *value = WhDupAn(line->Value, line->ValueLength);
            if (!name || !value) {
                WhFree(name);
                WhFree(value);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            WhFree(found->Name);
            WhFree(found->Value);
            found->Name = name;
            found->Value = value;
            return ERROR_SUCCESS;
        }
        if (flags & WINHTTP_ADDREQ_FLAG_ADD && line->ValueLength)
            return WhHeadersAppend(headers, line->Name, line->NameLength, line->Value, line->ValueLength)
                       ? ERROR_SUCCESS
                       : ERROR_NOT_ENOUGH_MEMORY;
        return ERROR_WINHTTP_HEADER_NOT_FOUND;
    }
    if (!(flags & (WINHTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA | WINHTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON)))
        return WhHeadersAppend(headers, line->Name, line->NameLength, line->Value, line->ValueLength)
                   ? ERROR_SUCCESS
                   : ERROR_NOT_ENOUGH_MEMORY;
    if (!line->ValueLength)
        return ERROR_SUCCESS;
    if (!found)
        return WhHeadersAppend(headers, line->Name, line->NameLength, line->Value, line->ValueLength)
                   ? ERROR_SUCCESS
                   : ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T old = strlen(found->Value);
    char *value = WhAlloc(old + 2 + line->ValueLength + 1);
    if (!value)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(value, found->Value, old);
    value[old] = flags & WINHTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON ? ';' : ',';
    value[old + 1] = ' ';
    memcpy(value + old + 2, line->Value, line->ValueLength);
    value[old + 2 + line->ValueLength] = 0;
    WhFree(found->Value);
    found->Value = value;
    return ERROR_SUCCESS;
}

DWORD WhAddHeaders(WH_OBJECT *request, const char *text, DWORD length, DWORD modifiers)
{
    if (!(modifiers & 0xFFFF0000))
        modifiers |= WINHTTP_ADDREQ_FLAG_ADD;
    DWORD flags = modifiers & 0xFFFF0000;
    if ((flags & 0xC0000000) == 0xC0000000 || (flags & 0x41000000) == 0x41000000 || (flags & 0x81000000) == 0x81000000)
        return ERROR_INVALID_PARAMETER;
    if (request->State == 1)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    /* every line is checked before any is applied. */
    WH_LINE *lines = nullptr;
    DWORD count = 0, capacity = 0;
    const char *p = text, *end = text + length;
    DWORD error = ERROR_SUCCESS;
    while (p < end) {
        while (p < end && (*p == '\r' || *p == '\n'))
            p++;
        if (p >= end || !*p)
            break;
        const char *name = p;
        while (p < end && *p != ':') {
            if (!HeaderNameChar(*p)) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            p++;
        }
        if (error || p >= end || p == name) {
            error = ERROR_INVALID_PARAMETER;
            break;
        }
        DWORD name_length = (DWORD)(p - name);
        p++;
        if (p < end && (*p == ' ' || *p == '\t'))
            p++;
        const char *value = p;
        while (p < end && *p) {
            if (*p == '\r' || *p == '\n') {
                if (end - p >= 3 && p[0] == '\r' && p[1] == '\n' && (p[2] == ' ' || p[2] == '\t')) {
                    p += 3;
                    continue;
                }
                break;
            }
            p++;
        }
        if (name_length > 0xFFFF || p - value > 0xFFFF) {
            error = ERROR_INVALID_PARAMETER;
            break;
        }
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 8;
            WH_LINE *grown = WhRealloc(lines, capacity * sizeof(*grown));
            if (!grown) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            lines = grown;
        }
        lines[count++] = (WH_LINE){name, name_length, value, (DWORD)(p - value)};
        if (p < end && !*p)
            break;
    }
    if (!error) {
        EnterCriticalSection(&request->Lock);
        for (DWORD i = 0; i < count && !error; i++)
            error = ApplyHeader(request, &lines[i], modifiers);
        LeaveCriticalSection(&request->Lock);
    }
    WhFree(lines);
    return error;
}

static WH_OBJECT *ReferenceRequest(HINTERNET handle)
{
    WH_OBJECT *object = WhReference(handle);
    if (object && object->Type != WH_REQUEST) {
        WhRelease(object);
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
        return nullptr;
    }
    return object;
}

BOOL WINAPI WinHttpAddRequestHeaders(HINTERNET handle, LPCWSTR headers, DWORD length, DWORD modifiers)
{
    if (!headers || !*headers || !length || modifiers & 0x0EFF0000)
        return WhFail(ERROR_INVALID_PARAMETER);
    if (length == (DWORD)-1)
        length = (DWORD)wcslen(headers);
    BOOL lossy = FALSE;
    char *text = WhWideToAnsi(headers, length, &lossy);
    if (!text)
        return WhFail(ERROR_NOT_ENOUGH_MEMORY);
    if (lossy) {
        WhFree(text);
        return WhFail(ERROR_INVALID_PARAMETER);
    }
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request) {
        WhFree(text);
        return FALSE;
    }
    DWORD error = WhAddHeaders(request, text, (DWORD)strlen(text), modifiers);
    WhRelease(request);
    WhFree(text);
    SetLastError(error);
    return !error;
}

typedef struct {
    union {
        LPCWSTR pwszName;
        LPCSTR pszName;
    };
    union {
        LPCWSTR pwszValue;
        LPCSTR pszValue;
    };
} WH_EXTENDED_HEADER;

#define WINHTTP_EXTENDED_HEADER_FLAG_UNICODE 0x1

DWORD WINAPI WinHttpAddRequestHeadersEx(HINTERNET handle, DWORD modifiers, ULONGLONG flags, ULONGLONG extra,
                                        DWORD count, WH_EXTENDED_HEADER *headers)
{
    if (!count || !headers || extra || flags & ~(ULONGLONG)WINHTTP_EXTENDED_HEADER_FLAG_UNICODE ||
        modifiers & 0x0EFF0000)
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request)
        return GetLastError();
    DWORD error = ERROR_SUCCESS;
    for (DWORD i = 0; i < count && !error; i++) {
        char *name, *value;
        if (flags & WINHTTP_EXTENDED_HEADER_FLAG_UNICODE) {
            name = headers[i].pwszName ? WhWideToAnsi(headers[i].pwszName, (DWORD)wcslen(headers[i].pwszName), nullptr)
                                       : nullptr;
            value = headers[i].pwszValue
                        ? WhWideToAnsi(headers[i].pwszValue, (DWORD)wcslen(headers[i].pwszValue), nullptr)
                        : WhDupA("");
        } else {
            name = WhDupA(headers[i].pszName);
            value = WhDupA(headers[i].pszValue ? headers[i].pszValue : "");
        }
        if (!name || !*name) {
            error = ERROR_INVALID_PARAMETER;
        } else if (!value) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            SIZE_T size = strlen(name) + strlen(value) + 3;
            char *line = WhAlloc(size);
            if (!line) {
                error = ERROR_NOT_ENOUGH_MEMORY;
            } else {
                wsprintfA(line, "%s: %s", name, value);
                error = WhAddHeaders(request, line, (DWORD)strlen(line), modifiers);
                WhFree(line);
            }
        }
        WhFree(name);
        WhFree(value);
    }
    WhRelease(request);
    return error;
}

/* ------------------------------------------------------------------ querying */

typedef struct {
    char *Data;
    DWORD Length;
    DWORD Capacity;
} WH_TEXT;

static BOOL TextAdd(WH_TEXT *text, const char *data, DWORD length)
{
    if (text->Length + length + 1 > text->Capacity) {
        DWORD capacity = (text->Length + length + 1) * 2;
        char *grown = WhRealloc(text->Data, capacity);
        if (!grown)
            return FALSE;
        text->Data = grown;
        text->Capacity = capacity;
    }
    memcpy(text->Data + text->Length, data, length);
    text->Length += length;
    text->Data[text->Length] = 0;
    return TRUE;
}

static BOOL TextAddString(WH_TEXT *text, const char *data)
{
    return TextAdd(text, data, (DWORD)strlen(data));
}

/* the request line of a request as sent, with its path. */
static BOOL RequestLine(WH_OBJECT *request, WH_TEXT *text)
{
    char *verb = WhWideToAnsi(request->Verb, (DWORD)wcslen(request->Verb), nullptr);
    char *path = WhWideToAnsi(request->Path, (DWORD)wcslen(request->Path), nullptr);
    char version[32];
    wsprintfA(version, " HTTP/%u.%u", request->Major, request->Minor);
    BOOL ok = verb && path && TextAddString(text, verb) && TextAdd(text, " ", 1) && TextAddString(text, path) &&
              TextAddString(text, version);
    WhFree(verb);
    WhFree(path);
    return ok;
}

/* raw headers: a status or request line, then the headers, NUL or CRLF separated. */
static BOOL RawHeaders(WH_OBJECT *request, BOOL request_headers, BOOL crlf, WH_TEXT *text)
{
    const char *separator = crlf ? "\r\n" : "";
    DWORD separator_length = crlf ? 2 : 1;
    if (request_headers) {
        if (!RequestLine(request, text))
            return FALSE;
    } else if (!TextAddString(text, request->RawStatusLine ? request->RawStatusLine : "")) {
        return FALSE;
    }
    if (!TextAdd(text, separator, separator_length))
        return FALSE;
    WH_HEADERS *headers = request_headers ? &request->Request : &request->Response;
    for (DWORD i = 0; i < headers->Count; i++) {
        if (!TextAddString(text, headers->Items[i].Name) || !TextAdd(text, ": ", 2) ||
            !TextAddString(text, headers->Items[i].Value) || !TextAdd(text, separator, separator_length))
            return FALSE;
    }
    return TextAdd(text, separator, separator_length);
}

static DWORD ReturnString(const char *data, DWORD length, LPVOID buffer, LPDWORD size, BOOL wire)
{
    if (wire) {
        if (*size < length + 1) {
            *size = length + 1;
            return ERROR_INSUFFICIENT_BUFFER;
        }
        memcpy(buffer, data, length);
        ((char *)buffer)[length] = 0;
        *size = length;
        return ERROR_SUCCESS;
    }
    int count = MultiByteToWideChar(CP_ACP, 0, data, (int)length, nullptr, 0);
    DWORD needed = ((DWORD)count + 1) * sizeof(WCHAR);
    if (!buffer || *size < needed) {
        *size = needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    MultiByteToWideChar(CP_ACP, 0, data, (int)length, buffer, count);
    ((LPWSTR)buffer)[count] = 0;
    *size = (DWORD)count * sizeof(WCHAR);
    return ERROR_SUCCESS;
}

static DWORD ConvertValue(const char *value, DWORD level, LPVOID buffer, LPDWORD size, BOOL wire)
{
    while (*value == ' ' || *value == '\t')
        value++;
    DWORD length = (DWORD)strlen(value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t'))
        length--;
    if (level & WINHTTP_QUERY_FLAG_SYSTEMTIME) {
        char *copy = WhDupAn(value, length);
        SYSTEMTIME time;
        BOOL ok = copy && WhParseHttpDate(copy, &time);
        WhFree(copy);
        if (!ok)
            return ERROR_WINHTTP_INVALID_QUERY_REQUEST;
        if (*size < sizeof(time)) {
            *size = sizeof(time);
            return ERROR_INSUFFICIENT_BUFFER;
        }
        memcpy(buffer, &time, sizeof(time));
        *size = sizeof(time);
        return ERROR_SUCCESS;
    }
    if (level & (WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_FLAG_NUMBER64)) {
        BOOL wide = (level & WINHTTP_QUERY_FLAG_NUMBER64) != 0;
        ULONGLONG number = 0, limit = wide ? UINT64_MAX : UINT32_MAX;
        const char *p = value;
        if (!wide)
            for (DWORD skipped = 0; *p == '0' && skipped < 10; skipped++)
                p++;
        const char *digits = p;
        while (*p >= '0' && *p <= '9') {
            DWORD digit = (DWORD)(*p - '0');
            if (number > (limit - digit) / 10)
                return ERROR_ARITHMETIC_OVERFLOW;
            number = number * 10 + digit;
            p++;
        }
        if (p == digits && !(digits > value))
            return ERROR_WINHTTP_INVALID_QUERY_REQUEST;
        DWORD needed = wide ? 8 : 4;
        if (*size < needed) {
            *size = needed;
            return ERROR_INSUFFICIENT_BUFFER;
        }
        if (wide)
            memcpy(buffer, &number, 8);
        else
            *(DWORD *)buffer = (DWORD)number;
        *size = needed;
        return ERROR_SUCCESS;
    }
    return ReturnString(value, length, buffer, size, wire);
}

static DWORD QueryHeaders(WH_OBJECT *request, DWORD level, const char *custom, LPVOID buffer, LPDWORD size,
                          LPDWORD index_pointer)
{
    BOOL wire = (level & WINHTTP_QUERY_FLAG_WIRE_ENCODING) != 0;
    level &= ~WINHTTP_QUERY_FLAG_WIRE_ENCODING;
    DWORD info = level & 0x14FFFFFF, flags = level & 0xEB000000;
    if (info > WINHTTP_QUERY_MAX && info != WINHTTP_QUERY_CUSTOM)
        return ERROR_INVALID_PARAMETER;
    if ((level & 0x28000000) == 0x28000000 || (level & 0x48000000) == 0x48000000 ||
        (level & 0x60000000) == 0x60000000 || (level & 0x82000000) == 0x82000000)
        return ERROR_INVALID_PARAMETER;
    DWORD index = index_pointer ? *index_pointer : 0;
    if (index && info >= WINHTTP_QUERY_VERSION && info <= WINHTTP_QUERY_RAW_HEADERS_CRLF)
        return ERROR_WINHTTP_HEADER_NOT_FOUND;
    DWORD name_length = 0;
    if (info == WINHTTP_QUERY_CUSTOM) {
        while (custom[name_length] && custom[name_length] != ':' && custom[name_length] != '\r' &&
               custom[name_length] != '\n') {
            BYTE c = (BYTE)custom[name_length];
            if (c < 0x20 || c == 0x7F || c == ' ')
                return ERROR_INVALID_PARAMETER;
            name_length++;
        }
    } else if (flags & ~Known[info].Flags & 0xE9000000) {
        return ERROR_WINHTTP_INVALID_QUERY_REQUEST;
    }
    if (!buffer)
        *size = 0;
    if (info == WINHTTP_QUERY_REQUEST_METHOD) {
        char *verb = WhWideToAnsi(request->Verb, (DWORD)wcslen(request->Verb), nullptr);
        DWORD error = verb && *verb ? ReturnString(verb, (DWORD)strlen(verb), buffer, size, wire)
                                    : ERROR_WINHTTP_HEADER_NOT_FOUND;
        WhFree(verb);
        return error;
    }
    BOOL from_request = (flags & WINHTTP_QUERY_FLAG_REQUEST_HEADERS) != 0;
    if (!from_request && request->State < 2)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    if (flags & WINHTTP_QUERY_FLAG_TRAILERS) {
        if (info >= WINHTTP_QUERY_VERSION && info <= WINHTTP_QUERY_STATUS_TEXT)
            return ERROR_WINHTTP_INVALID_QUERY_REQUEST;
        return ERROR_WINHTTP_HEADER_NOT_FOUND;
    }
    if (from_request && info >= WINHTTP_QUERY_VERSION && info <= WINHTTP_QUERY_STATUS_TEXT)
        return ERROR_WINHTTP_INVALID_QUERY_REQUEST;
    if (info == WINHTTP_QUERY_RAW_HEADERS || info == WINHTTP_QUERY_RAW_HEADERS_CRLF) {
        WH_TEXT text = {0};
        if (!RawHeaders(request, from_request, info == WINHTTP_QUERY_RAW_HEADERS_CRLF, &text)) {
            WhFree(text.Data);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        DWORD error = ReturnString(text.Data, text.Length, buffer, size, wire);
        WhFree(text.Data);
        return error;
    }
    if (info == WINHTTP_QUERY_VERSION) {
        char version[32];
        wsprintfA(version, "HTTP/%u.%u", request->RespMajor, request->RespMinor);
        return ReturnString(version, (DWORD)strlen(version), buffer, size, wire);
    }
    if (info == WINHTTP_QUERY_STATUS_CODE) {
        if (flags & WINHTTP_QUERY_FLAG_NUMBER) {
            if (*size < 4)
                return ERROR_INSUFFICIENT_BUFFER;
            *(DWORD *)buffer = request->StatusCode;
            *size = 4;
            return ERROR_SUCCESS;
        }
        char code[16];
        wsprintfA(code, "%u", request->StatusCode);
        return ReturnString(code, (DWORD)strlen(code), buffer, size, wire);
    }
    if (info == WINHTTP_QUERY_STATUS_TEXT) {
        const char *text = request->StatusText ? request->StatusText : "";
        return ReturnString(text, (DWORD)strlen(text), buffer, size, wire);
    }
    const char *name = info == WINHTTP_QUERY_CUSTOM ? custom : Known[info].Name;
    if (info != WINHTTP_QUERY_CUSTOM)
        name_length = name ? (DWORD)strlen(name) : 0;
    if (!name)
        return ERROR_WINHTTP_HEADER_NOT_FOUND;
    WH_HEADER *header = FindHeader(from_request ? &request->Request : &request->Response, name, name_length, index);
    if (!header)
        return ERROR_WINHTTP_HEADER_NOT_FOUND;
    DWORD error = ConvertValue(header->Value, level, buffer, size, wire);
    if (!error && index_pointer)
        *index_pointer = index + 1;
    return error;
}

BOOL WINAPI WinHttpQueryHeaders(HINTERNET handle, DWORD level, LPCWSTR name, LPVOID buffer, LPDWORD size,
                                LPDWORD index)
{
    if (!size)
        return WhFail(ERROR_INVALID_PARAMETER);
    char *custom = nullptr;
    if (((level & ~WINHTTP_QUERY_FLAG_WIRE_ENCODING) & 0x14FFFFFF) == WINHTTP_QUERY_CUSTOM) {
        if (!name)
            return WhFail(ERROR_INVALID_PARAMETER);
        BOOL lossy = FALSE;
        custom = WhWideToAnsi(name, (DWORD)wcslen(name), &lossy);
        if (!custom)
            return WhFail(ERROR_NOT_ENOUGH_MEMORY);
        if (lossy) {
            WhFree(custom);
            return WhFail(ERROR_INVALID_PARAMETER);
        }
    }
    WH_OBJECT *request = ReferenceRequest(handle);
    if (!request) {
        WhFree(custom);
        return FALSE;
    }
    EnterCriticalSection(&request->Lock);
    DWORD error = QueryHeaders(request, level, custom, buffer, size, index);
    LeaveCriticalSection(&request->Lock);
    WhRelease(request);
    WhFree(custom);
    SetLastError(error);
    return !error;
}

DWORD WINAPI WinHttpQueryHeadersEx(HINTERNET handle, DWORD level, ULONGLONG flags, UINT codepage, PDWORD index,
                                   LPVOID name, PVOID buffer, PDWORD length, LPVOID *headers, PDWORD count)
{
    return ERROR_NOT_SUPPORTED;
}
