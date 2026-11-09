/*
 * Reading parts of a file at an address with range requests, for the features whose files are a few of the many in a
 * big archive: the fonts in a release of Cascadia, the streams of a Windows image. The connection is kept between the
 * reads. Nothing read is trusted here, the callers check every byte against a hash the catalog names.
 */
#include "featurep.h"
#include <mime/win32/winhttp.h>

#define CHUNK (256 * 1024)
#define ATTEMPTS 4

struct RANGES {
    RUN *Run;
    const FEATURE_DEF *Feature;
    HINTERNET Session, Connection;
    WCHAR Path[1536];
    BOOL Secure;
    ULONGLONG Total;  /* The size of the file, when a response told. */
    ULONGLONG Bytes;  /* Read so far, for the progress. */
    ULONGLONG Expect; /* About how much will be read, for the progress. */
    BYTE *Buffer;
};

RANGES *RangesOpen(RUN *run, const FEATURE_DEF *feature, PCWSTR url)
{
    URL_COMPONENTS parts = {sizeof(parts)};
    WCHAR host[256], path[1536];
    parts.lpszHostName = host;
    parts.dwHostNameLength = RTL_NUMBER_OF(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = RTL_NUMBER_OF(path);
    if (!WinHttpCrackUrl(url, 0, 0, &parts))
        return nullptr;
    RANGES *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        return nullptr;
    r->Run = run;
    r->Feature = feature;
    lstrcpynW(r->Path, path, RTL_NUMBER_OF(r->Path));
    r->Secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    r->Session = WinHttpOpen(L"Microsoft-Windows-Features/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (r->Session) {
        WinHttpSetTimeouts(r->Session, 30000, 30000, 60000, 60000);
        r->Connection = WinHttpConnect(r->Session, host, parts.nPort, 0);
    }
    r->Buffer = HeapAlloc(GetProcessHeap(), 0, CHUNK);
    if (!r->Connection || !r->Buffer) {
        RangesClose(r);
        return nullptr;
    }
    return r;
}

void RangesClose(RANGES *r)
{
    if (!r)
        return;
    if (r->Connection)
        WinHttpCloseHandle(r->Connection);
    if (r->Session)
        WinHttpCloseHandle(r->Session);
    if (r->Buffer)
        HeapFree(GetProcessHeap(), 0, r->Buffer);
    HeapFree(GetProcessHeap(), 0, r);
}

ULONGLONG RangesTotal(const RANGES *r)
{
    return r->Total;
}

void RangesExpect(RANGES *r, ULONGLONG bytes)
{
    r->Expect = bytes;
}

static BOOL final_address_allowed(RANGES *r, HINTERNET request)
{
    WCHAR url[2048];
    DWORD size = sizeof(url);
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, url, &size))
        return FALSE;
    char utf8[2048];
    if (!WideCharToMultiByte(CP_UTF8, 0, url, -1, utf8, sizeof(utf8), nullptr, nullptr))
        return FALSE;
    return FeatureUrlAllowedFor(r->Feature, utf8);
}

/* The size after the slash of "bytes 5-9/1000". */
static ULONGLONG total_of(HINTERNET request)
{
    WCHAR text[96];
    DWORD size = sizeof(text);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, text, &size,
                             WINHTTP_NO_HEADER_INDEX))
        return 0;
    const WCHAR *slash = wcschr(text, L'/');
    ULONGLONG value = 0;
    for (const WCHAR *p = slash ? slash + 1 : L""; *p >= L'0' && *p <= L'9'; p++)
        value = value * 10 + (ULONGLONG)(*p - L'0');
    return value;
}

/* One try. ERROR_MORE_DATA means the connection broke and another try may work. */
static DWORD read_once(RANGES *r, ULONGLONG offset, SIZE_T length, BYTE *buffer)
{
    HINTERNET request = WinHttpOpenRequest(r->Connection, L"GET", r->Path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, r->Secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request)
        return GetLastError();
    DWORD error = ERROR_SUCCESS;
    WCHAR header[96];
    _snwprintf(header, RTL_NUMBER_OF(header), L"Range: bytes=%llu-%llu\r\n", offset, offset + length - 1);
    header[RTL_NUMBER_OF(header) - 1] = 0;
    WinHttpAddRequestHeaders(request, header, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        error = GetLastError();
        goto done;
    }
    if (!final_address_allowed(r, request)) {
        error = ERROR_ACCESS_DENIED;
        goto done;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 206) {
        /* The whole file would be read to the end if the server did not do ranges. */
        error = status == 404 ? ERROR_FILE_NOT_FOUND : status == 403 || status == 401 ? ERROR_ACCESS_DENIED : ERROR_BAD_NET_RESP;
        _snwprintf(r->Run->Detail, RTL_NUMBER_OF(r->Run->Detail), L"The server answered %lu to a request for a part of a file.", status);
        r->Run->Detail[RTL_NUMBER_OF(r->Run->Detail) - 1] = 0;
        goto done;
    }
    ULONGLONG total = total_of(request);
    if (total)
        r->Total = total;
    SIZE_T done_bytes = 0;
    while (done_bytes < length) {
        if (r->Run->Cancel) {
            error = ERROR_CANCELLED;
            goto done;
        }
        DWORD got = 0, want = (DWORD)(length - done_bytes < CHUNK ? length - done_bytes : CHUNK);
        if (!WinHttpReadData(request, r->Buffer, want, &got)) {
            error = ERROR_MORE_DATA;
            goto done;
        }
        if (!got) {
            error = ERROR_MORE_DATA;
            goto done;
        }
        memcpy(buffer + done_bytes, r->Buffer, got);
        done_bytes += got;
        r->Bytes += got;
        RunReport(r->Run, r->Bytes, r->Expect > r->Bytes ? r->Expect : r->Bytes, nullptr);
    }
done:
    WinHttpCloseHandle(request);
    return error;
}

DWORD RangesRead(RANGES *r, ULONGLONG offset, SIZE_T length, void *buffer)
{
    DWORD error = ERROR_SUCCESS;
    ULONGLONG before = r->Bytes;
    for (int attempt = 0; attempt < ATTEMPTS; attempt++) {
        r->Bytes = before;
        error = read_once(r, offset, length, buffer);
        if (error != ERROR_MORE_DATA && error != ERROR_WINHTTP_CONNECTION_ERROR && error != ERROR_WINHTTP_TIMEOUT)
            break;
        if (attempt + 1 < ATTEMPTS)
            Sleep(1000 * (DWORD)(attempt + 1));
    }
    if (error == ERROR_MORE_DATA)
        error = ERROR_WINHTTP_CONNECTION_ERROR;
    return error;
}
