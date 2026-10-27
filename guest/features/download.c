/*
 * The download of a package, with WinHTTP. A partial file stays in the cache with the validator the server gave, and
 * the next request asks for the rest when the validator is still the file's.
 */
#include "featurep.h"
#include <mime/win32/winhttp.h>

#define CHUNK (256 * 1024)
#define EXTRA_SPACE (256ull << 20)
#define ATTEMPTS 4

typedef struct TRANSFER {
    HINTERNET Session, Connection, Request;
    HANDLE File;
    BYTE *Buffer;
} TRANSFER;

static void close_transfer(TRANSFER *t)
{
    if (t->Request)
        WinHttpCloseHandle(t->Request);
    if (t->Connection)
        WinHttpCloseHandle(t->Connection);
    if (t->Session)
        WinHttpCloseHandle(t->Session);
    if (t->File != INVALID_HANDLE_VALUE)
        CloseHandle(t->File);
    if (t->Buffer)
        HeapFree(GetProcessHeap(), 0, t->Buffer);
}

static ULONGLONG file_size(PCWSTR path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        return 0;
    return (ULONGLONG)data.nFileSizeHigh << 32 | data.nFileSizeLow;
}

/* The validator of the partial file, the header the server gave when it began. */
static BOOL read_validator(PCWSTR path, WCHAR *validator, SIZE_T chars)
{
    validator[0] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    char text[256];
    DWORD read = 0;
    BOOL ok = ReadFile(file, text, sizeof(text) - 1, &read, nullptr) && read > 0;
    CloseHandle(file);
    if (!ok)
        return FALSE;
    text[read] = 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, validator, (int)chars);
    return n > 1;
}

static void write_validator(PCWSTR path, PCWSTR validator)
{
    char text[256];
    int n = WideCharToMultiByte(CP_UTF8, 0, validator, -1, text, sizeof(text), nullptr, nullptr);
    if (n <= 1)
        return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(file, text, (DWORD)(n - 1), &written, nullptr);
    CloseHandle(file);
}

/* A header as text, empty when the response lacks it. */
static BOOL header_text(HINTERNET request, DWORD query, WCHAR *text, DWORD chars)
{
    DWORD size = chars * sizeof(WCHAR);
    text[0] = 0;
    return WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX) && text[0];
}

static ULONGLONG header_number(HINTERNET request, DWORD query)
{
    WCHAR text[32];
    if (!header_text(request, query, text, RTL_NUMBER_OF(text)))
        return 0;
    ULONGLONG value = 0;
    for (const WCHAR *p = text; *p >= L'0' && *p <= L'9'; p++)
        value = value * 10 + (ULONGLONG)(*p - L'0');
    return value;
}

/* The last address of the request must still be Microsoft's, whatever the redirects did. */
static BOOL final_address_allowed(HINTERNET request)
{
    WCHAR url[2048];
    DWORD size = sizeof(url);
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, url, &size))
        return FALSE;
    char utf8[2048];
    if (!WideCharToMultiByte(CP_UTF8, 0, url, -1, utf8, sizeof(utf8), nullptr, nullptr))
        return FALSE;
    return FeatureUrlAllowed(utf8);
}

/*
 * One pass over the connection. Returns ERROR_SUCCESS when the whole file is in the partial file, ERROR_MORE_DATA
 * when the connection broke with some of it written, else the error.
 */
static DWORD transfer(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR part, PCWSTR validator_path, BOOL *progressed)
{
    TRANSFER t = {.File = INVALID_HANDLE_VALUE};
    DWORD error = ERROR_SUCCESS;
    ULONGLONG have = file_size(part);
    WCHAR validator[200] = L"";
    if (have && !read_validator(validator_path, validator, RTL_NUMBER_OF(validator)))
        have = 0;

    URL_COMPONENTS parts = {sizeof(parts)};
    WCHAR host[256], path[1536];
    parts.lpszHostName = host;
    parts.dwHostNameLength = RTL_NUMBER_OF(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = RTL_NUMBER_OF(path);
    if (!WinHttpCrackUrl(url, 0, 0, &parts))
        return GetLastError();
    t.Session = WinHttpOpen(L"Mime-Features/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!t.Session)
        return GetLastError();
    WinHttpSetTimeouts(t.Session, 30000, 30000, 60000, 60000);
    t.Connection = WinHttpConnect(t.Session, host, parts.nPort, 0);
    if (!t.Connection) {
        error = GetLastError();
        goto done;
    }
    t.Request = WinHttpOpenRequest(t.Connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!t.Request) {
        error = GetLastError();
        goto done;
    }
    if (have) {
        WCHAR headers[400];
        _snwprintf(headers, RTL_NUMBER_OF(headers), L"Range: bytes=%llu-\r\nIf-Range: %ls\r\n", have, validator);
        headers[RTL_NUMBER_OF(headers) - 1] = 0;
        WinHttpAddRequestHeaders(t.Request, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!WinHttpSendRequest(t.Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(t.Request, nullptr)) {
        error = GetLastError();
        goto done;
    }
    if (!final_address_allowed(t.Request)) {
        /* A redirect to anywhere but Microsoft is no download of this feature. */
        error = ERROR_ACCESS_DENIED;
        goto done;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(t.Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    BOOL resumed = status == 206 && have;
    if (status == 416 && have) {
        /* The server has no more bytes after ours: the file was whole, or it changed. Start over. */
        DeleteFileW(part);
        DeleteFileW(validator_path);
        error = ERROR_MORE_DATA;
        goto done;
    }
    if (status != 200 && !resumed) {
        error = status == 404 ? ERROR_FILE_NOT_FOUND : status == 403 || status == 401 ? ERROR_ACCESS_DENIED : ERROR_BAD_NET_RESP;
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"The server answered %lu.", status);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
        goto done;
    }
    if (!resumed)
        have = 0;
    ULONGLONG remaining = header_number(t.Request, WINHTTP_QUERY_CONTENT_LENGTH);
    ULONGLONG total = remaining ? have + remaining : 0;
    if (total && total > feature->MaxSize) {
        error = ERROR_FILE_TOO_LARGE;
        goto done;
    }
    /* A server that does not say how much is held to the limit while it is read. */
    if (total) {
        ULARGE_INTEGER free_bytes;
        if (GetDiskFreeSpaceExW(run->Cache, &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < remaining + EXTRA_SPACE) {
            error = ERROR_DISK_FULL;
            goto done;
        }
    }
    if (!resumed && header_text(t.Request, WINHTTP_QUERY_ETAG, validator, RTL_NUMBER_OF(validator)))
        write_validator(validator_path, validator);
    else if (!resumed && header_text(t.Request, WINHTTP_QUERY_LAST_MODIFIED, validator, RTL_NUMBER_OF(validator)))
        write_validator(validator_path, validator);
    else if (!resumed)
        DeleteFileW(validator_path);
    t.File = CreateFileW(part, GENERIC_WRITE, 0, nullptr, resumed ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (t.File == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto done;
    }
    if (resumed) {
        LARGE_INTEGER end = {.QuadPart = (LONGLONG)have};
        if (!SetFilePointerEx(t.File, end, nullptr, FILE_BEGIN)) {
            error = GetLastError();
            goto done;
        }
    }
    t.Buffer = HeapAlloc(GetProcessHeap(), 0, CHUNK);
    if (!t.Buffer) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    ULONGLONG position = have, last_report = 0;
    for (;;) {
        if (run->Cancel) {
            error = ERROR_CANCELLED;
            goto done;
        }
        DWORD read = 0;
        if (!WinHttpReadData(t.Request, t.Buffer, CHUNK, &read)) {
            error = GetLastError();
            if (position > have)
                *progressed = TRUE;
            if (position > have || resumed)
                error = ERROR_MORE_DATA;
            goto done;
        }
        if (!read)
            break;
        DWORD written;
        if (!WriteFile(t.File, t.Buffer, read, &written, nullptr) || written != read) {
            error = GetLastError() ? GetLastError() : ERROR_DISK_FULL;
            goto done;
        }
        position += read;
        if (position > feature->MaxSize) {
            error = ERROR_FILE_TOO_LARGE;
            goto done;
        }
        ULONGLONG tick = GetTickCount64();
        if (tick - last_report >= 100) {
            last_report = tick;
            RunReport(run, position, total, nullptr);
        }
    }
    RunReport(run, position, total ? total : position, nullptr);
    if (total && position < total) {
        *progressed = TRUE;
        error = ERROR_MORE_DATA;
    }
done:
    close_transfer(&t);
    return error;
}

DWORD DownloadPackage(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination)
{
    WCHAR part[MAX_PATH], validator[MAX_PATH];
    _snwprintf(part, RTL_NUMBER_OF(part), L"%ls.part", destination);
    _snwprintf(validator, RTL_NUMBER_OF(validator), L"%ls.tag", destination);
    part[RTL_NUMBER_OF(part) - 1] = validator[RTL_NUMBER_OF(validator) - 1] = 0;
    DWORD error = ERROR_SUCCESS;
    for (int attempt = 0; attempt < ATTEMPTS; attempt++) {
        BOOL progressed = FALSE;
        error = transfer(run, feature, url, part, validator, &progressed);
        if (error != ERROR_MORE_DATA)
            break;
        if (attempt + 1 < ATTEMPTS)
            Sleep(1000 * (DWORD)(attempt + 1));
    }
    if (error == ERROR_MORE_DATA)
        error = ERROR_WINHTTP_CONNECTION_ERROR;
    /* A refusal that no new try fixes leaves nothing behind. */
    if (error == ERROR_FILE_TOO_LARGE || error == ERROR_ACCESS_DENIED || error == ERROR_BAD_NET_RESP) {
        DeleteFileW(part);
        DeleteFileW(validator);
    }
    if (error != ERROR_SUCCESS)
        return error;
    if (!MoveFileExW(part, destination, MOVEFILE_REPLACE_EXISTING))
        return GetLastError();
    DeleteFileW(validator);
    return ERROR_SUCCESS;
}
