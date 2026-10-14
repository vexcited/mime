/*
 * http requests: the request header list, which is real, and sending, which needs the network.
 */
#include "wininet_private.h"
#include <mime/stub.h>

static SRWLOCK HeaderLock = SRWLOCK_INIT;

static BOOL IsSpace(WCHAR c)
{
    return c == L' ' || c == L'\t';
}

static PWSTR CopyRange(LPCWSTR text, DWORD length)
{
    PWSTR copy = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

static BOOL NameIs(LPCWSTR name, LPCWSTR other, DWORD other_length)
{
    return CompareStringOrdinal(name, -1, other, (int)other_length, TRUE) == CSTR_EQUAL;
}

/* the index of the nth header with this name, or UINT32_MAX. */
static DWORD FindHeader(INET_OBJECT *request, LPCWSTR name, DWORD name_length, DWORD nth)
{
    for (DWORD i = 0; i < request->HeaderCount; i++)
        if (NameIs(request->Headers[i].Name, name, name_length) && !nth--)
            return i;
    return UINT32_MAX;
}

static void RemoveHeader(INET_OBJECT *request, DWORD index)
{
    InetFree(request->Headers[index].Name);
    InetFree(request->Headers[index].Value);
    request->HeaderCount--;
    for (DWORD i = index; i < request->HeaderCount; i++)
        request->Headers[i] = request->Headers[i + 1];
}

static DWORD AppendHeader(INET_OBJECT *request, LPCWSTR name, DWORD name_length, LPCWSTR value, DWORD value_length)
{
    if (request->HeaderCount == request->HeaderCapacity) {
        DWORD capacity = request->HeaderCapacity ? request->HeaderCapacity * 2 : 8;
        INET_HEADER *headers = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*headers));
        if (!headers)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (request->HeaderCount)
            memcpy(headers, request->Headers, request->HeaderCount * sizeof(*headers));
        InetFree(request->Headers);
        request->Headers = headers;
        request->HeaderCapacity = capacity;
    }
    PWSTR name_copy = CopyRange(name, name_length), value_copy = CopyRange(value, value_length);
    if (!name_copy || !value_copy) {
        InetFree(name_copy);
        InetFree(value_copy);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    request->Headers[request->HeaderCount].Name = name_copy;
    request->Headers[request->HeaderCount].Value = value_copy;
    request->HeaderCount++;
    return ERROR_SUCCESS;
}

/* applies one "Name: value" line with the HttpAddRequestHeaders modifiers. */
static DWORD ProcessHeader(INET_OBJECT *request, LPCWSTR line, DWORD length, DWORD modifiers)
{
    DWORD colon = 0;
    while (colon < length && line[colon] != L':')
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
    LPCWSTR value = line + start;
    DWORD value_length = end - start;
    DWORD index = FindHeader(request, line, name_length, 0);
    DWORD coalesce = modifiers & (HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA | HTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON);
    if (index == UINT32_MAX) {
        /* replacing a missing header with an empty value removes nothing. */
        if ((modifiers & HTTP_ADDREQ_FLAG_REPLACE) && !value_length)
            return ERROR_SUCCESS;
        return AppendHeader(request, line, name_length, value, value_length);
    }
    if (modifiers & HTTP_ADDREQ_FLAG_ADD_IF_NEW)
        return ERROR_HTTP_HEADER_ALREADY_EXISTS;
    if (modifiers & HTTP_ADDREQ_FLAG_REPLACE) {
        RemoveHeader(request, index);
        return value_length ? AppendHeader(request, line, name_length, value, value_length) : ERROR_SUCCESS;
    }
    if (coalesce) {
        PWSTR old = request->Headers[index].Value;
        DWORD old_length = lstrlenW(old);
        PWSTR merged = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)old_length + value_length + 3) * sizeof(WCHAR));
        if (!merged)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(merged, old, old_length * sizeof(WCHAR));
        merged[old_length] = coalesce & HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA ? L',' : L';';
        merged[old_length + 1] = L' ';
        memcpy(merged + old_length + 2, value, value_length * sizeof(WCHAR));
        merged[old_length + 2 + value_length] = 0;
        request->Headers[index].Value = merged;
        InetFree(old);
        return ERROR_SUCCESS;
    }
    return AppendHeader(request, line, name_length, value, value_length);
}

DECLSPEC_EXPORT BOOL WINAPI HttpAddRequestHeadersW(HINTERNET request, LPCWSTR headers, DWORD length, DWORD modifiers)
{
    INET_OBJECT *object = InetReferenceType(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
    if (!object)
        return FALSE;
    if (!headers || !length) {
        InetRelease(object);
        return InetFail(ERROR_INVALID_PARAMETER);
    }
    if (length == (DWORD)-1)
        length = lstrlenW(headers);
    DWORD error = ERROR_SUCCESS;
    AcquireSRWLockExclusive(&HeaderLock);
    for (DWORD at = 0; at < length && !error;) {
        DWORD end = at;
        while (end < length && headers[end] != L'\r' && headers[end] != L'\n' && headers[end])
            end++;
        if (end > at)
            error = ProcessHeader(object, headers + at, end - at, modifiers);
        if (end < length && !headers[end])
            break;
        at = end + 1;
    }
    ReleaseSRWLockExclusive(&HeaderLock);
    InetRelease(object);
    return error ? InetFail(error) : TRUE;
}

static LPCWSTR LevelName(DWORD level)
{
    switch (level) {
    case HTTP_QUERY_CONTENT_TYPE:
        return L"Content-Type";
    case HTTP_QUERY_CONTENT_LENGTH:
        return L"Content-Length";
    case HTTP_QUERY_ACCEPT:
        return L"Accept";
    case HTTP_QUERY_REFERER:
        return L"Referer";
    case HTTP_QUERY_USER_AGENT:
        return L"User-Agent";
    case HTTP_QUERY_HOST:
        return L"Host";
    }
    return NULL;
}

/* copies a string result, the length in bytes with the terminator when too small and without it after. */
static DWORD ReturnString(LPCWSTR text, DWORD chars, LPVOID buffer, LPDWORD length)
{
    DWORD bytes = (chars + 1) * sizeof(WCHAR);
    if (!buffer || *length < bytes) {
        *length = bytes;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(buffer, text, chars * sizeof(WCHAR));
    ((WCHAR *)buffer)[chars] = 0;
    *length = chars * sizeof(WCHAR);
    return ERROR_SUCCESS;
}

/* the request line and headers as sent, each line ended by a crlf and the block by an empty line. */
static DWORD RawRequestHeaders(INET_OBJECT *request, LPVOID buffer, LPDWORD length)
{
    SIZE_T chars = lstrlenW(request->Verb) + 1 + lstrlenW(request->Object) + 1 + lstrlenW(request->Version) + 2 + 2;
    for (DWORD i = 0; i < request->HeaderCount; i++)
        chars += lstrlenW(request->Headers[i].Name) + 2 + lstrlenW(request->Headers[i].Value) + 2;
    PWSTR text = HeapAlloc(GetProcessHeap(), 0, (chars + 1) * sizeof(WCHAR));
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    text[0] = 0;
    lstrcatW(text, request->Verb);
    lstrcatW(text, L" ");
    lstrcatW(text, request->Object);
    lstrcatW(text, L" ");
    lstrcatW(text, request->Version);
    lstrcatW(text, L"\r\n");
    for (DWORD i = 0; i < request->HeaderCount; i++) {
        lstrcatW(text, request->Headers[i].Name);
        lstrcatW(text, L": ");
        lstrcatW(text, request->Headers[i].Value);
        lstrcatW(text, L"\r\n");
    }
    lstrcatW(text, L"\r\n");
    DWORD error = ReturnString(text, (DWORD)chars, buffer, length);
    InetFree(text);
    return error;
}

static DWORD QueryRequestHeader(INET_OBJECT *request, DWORD level, LPVOID buffer, LPDWORD length, LPDWORD index)
{
    DWORD field = level & HTTP_QUERY_HEADER_MASK;
    if (field == HTTP_QUERY_RAW_HEADERS_CRLF)
        return RawRequestHeaders(request, buffer, length);
    LPCWSTR name = field == HTTP_QUERY_CUSTOM ? buffer : LevelName(field);
    if (!name)
        return ERROR_HTTP_HEADER_NOT_FOUND;
    DWORD at = FindHeader(request, name, (DWORD)lstrlenW(name), index ? *index : 0);
    if (at == UINT32_MAX)
        return ERROR_HTTP_HEADER_NOT_FOUND;
    LPCWSTR value = request->Headers[at].Value;
    DWORD error;
    if (level & HTTP_QUERY_FLAG_NUMBER) {
        DWORD number = 0;
        for (LPCWSTR p = value; *p >= L'0' && *p <= L'9'; p++)
            number = number * 10 + (*p - L'0');
        if (!buffer || *length < sizeof(DWORD)) {
            *length = sizeof(DWORD);
            return ERROR_INSUFFICIENT_BUFFER;
        }
        *(DWORD *)buffer = number;
        *length = sizeof(DWORD);
        error = ERROR_SUCCESS;
    } else {
        error = ReturnString(value, lstrlenW(value), buffer, length);
    }
    if (!error && index)
        ++*index;
    return error;
}

DECLSPEC_EXPORT BOOL WINAPI HttpQueryInfoW(HINTERNET request, DWORD level, LPVOID buffer, LPDWORD length,
                                          LPDWORD index)
{
    INET_OBJECT *object = InetReferenceType(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
    if (!object)
        return FALSE;
    DWORD error;
    if (!length)
        error = ERROR_INVALID_PARAMETER;
    else if (level & HTTP_QUERY_FLAG_SYSTEMTIME)
        error = ERROR_HTTP_HEADER_NOT_FOUND;
    else if (level & HTTP_QUERY_FLAG_REQUEST_HEADERS) {
        AcquireSRWLockShared(&HeaderLock);
        error = QueryRequestHeader(object, level, buffer, length, index);
        ReleaseSRWLockShared(&HeaderLock);
    } else {
        /* response headers only exist once a request was sent, which needs the network. */
        error = ERROR_INTERNET_INCORRECT_HANDLE_STATE;
    }
    InetRelease(object);
    return error ? InetFail(error) : TRUE;
}

/* the request handle is checked first so a bad handle reports ERROR_INVALID_HANDLE as windows does. */
static BOOL FailRequest(HINTERNET request, DWORD error)
{
    INET_OBJECT *object = InetReferenceType(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
    if (!object)
        return FALSE;
    InetRelease(object);
    return InetFail(error);
}

DECLSPEC_EXPORT BOOL WINAPI HttpSendRequestW(HINTERNET request, LPCWSTR headers, DWORD headers_length,
                                            LPVOID optional, DWORD optional_length)
{
    MIME_STUB("wininet");
    return FailRequest(request, ERROR_INTERNET_CANNOT_CONNECT);
}

DECLSPEC_EXPORT BOOL WINAPI HttpSendRequestExW(HINTERNET request, LPINTERNET_BUFFERSW buffers_in,
                                              LPINTERNET_BUFFERSW buffers_out, DWORD flags, DWORD_PTR context)
{
    MIME_STUB("wininet");
    return FailRequest(request, ERROR_INTERNET_CANNOT_CONNECT);
}

DECLSPEC_EXPORT BOOL WINAPI HttpEndRequestW(HINTERNET request, LPINTERNET_BUFFERSW buffers_out, DWORD flags,
                                           DWORD_PTR context)
{
    MIME_STUB("wininet");
    return FailRequest(request, ERROR_INTERNET_INCORRECT_HANDLE_STATE);
}
