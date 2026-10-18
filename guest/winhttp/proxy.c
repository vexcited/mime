/*
 * proxies: the default and per user configuration, the proxy of a request, auto detection and the
 * proxy resolver.
 */
#include "winhttp_private.h"
#include <mime/stub.h>

#define CONNECTIONS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Connections"

/* the wininet blob flags. */
#define BLOB_PROXY 0x2
#define BLOB_AUTOCONFIG_URL 0x4
#define BLOB_AUTO_DETECT 0x8

static PWSTR GlobalDupW(LPCWSTR text, DWORD length)
{
    PWSTR copy = GlobalAlloc(GMEM_FIXED, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, (SIZE_T)length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

static PWSTR GlobalFromAnsi(const BYTE *text, DWORD length)
{
    int count = MultiByteToWideChar(CP_ACP, 0, (const char *)text, (int)length, nullptr, 0);
    PWSTR copy = GlobalAlloc(GMEM_FIXED, ((SIZE_T)count + 1) * sizeof(WCHAR));
    if (copy) {
        MultiByteToWideChar(CP_ACP, 0, (const char *)text, (int)length, copy, count);
        copy[count] = 0;
    }
    return copy;
}

static BYTE *ReadBlob(HKEY root, LPCWSTR value, DWORD *size, BOOL create)
{
    HKEY key;
    *size = 0;
    LSTATUS status = create ? RegCreateKeyExW(root, CONNECTIONS_KEY, 0, nullptr, 0, KEY_READ, nullptr, &key, nullptr)
                            : RegOpenKeyExW(root, CONNECTIONS_KEY, 0, KEY_READ, &key);
    if (status)
        return nullptr;
    DWORD type, length = 0;
    BYTE *data = nullptr;
    if (!RegQueryValueExW(key, value, nullptr, &type, nullptr, &length) && type == REG_BINARY && length &&
        (data = WhAlloc(length)) && RegQueryValueExW(key, value, nullptr, &type, data, &length)) {
        WhFree(data);
        data = nullptr;
    }
    RegCloseKey(key);
    if (data)
        *size = length;
    return data;
}

typedef struct {
    const BYTE *Data;
    DWORD Size;
    DWORD Offset;
} WH_BLOB;

static DWORD BlobDword(WH_BLOB *blob)
{
    DWORD value = 0;
    if (blob->Offset + 4 <= blob->Size)
        memcpy(&value, blob->Data + blob->Offset, 4);
    blob->Offset += 4;
    return value;
}

/* a length prefixed ansi string of the blob, NULL when empty or cut. */
static PWSTR BlobString(WH_BLOB *blob)
{
    DWORD length = BlobDword(blob);
    if (blob->Offset > blob->Size || length > blob->Size - blob->Offset) {
        blob->Offset = blob->Size;
        return nullptr;
    }
    const BYTE *text = blob->Data + blob->Offset;
    blob->Offset += length;
    return length ? GlobalFromAnsi(text, length) : nullptr;
}

BOOL WhReadDefaultProxy(WINHTTP_PROXY_INFO *info)
{
    memset(info, 0, sizeof(*info));
    info->dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
    DWORD size;
    BYTE *data = ReadBlob(HKEY_LOCAL_MACHINE, L"WinHttpSettings", &size, FALSE);
    if (!data)
        return TRUE;
    WH_BLOB blob = {data, size, 0};
    if (BlobDword(&blob) >= 0x28) {
        BlobDword(&blob);
        DWORD flags = BlobDword(&blob) & ~0xCu;
        PWSTR proxy = BlobString(&blob), bypass = BlobString(&blob);
        if (flags == 3) {
            info->dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            info->lpszProxy = proxy;
            info->lpszProxyBypass = bypass;
        } else {
            GlobalFree(proxy);
            GlobalFree(bypass);
        }
    }
    WhFree(data);
    return TRUE;
}

BOOL WINAPI WinHttpGetDefaultProxyConfiguration(WINHTTP_PROXY_INFO *info)
{
    if (!info)
        return WhFail(ERROR_INVALID_PARAMETER);
    WhReadDefaultProxy(info);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static void PutDword(BYTE *data, DWORD *offset, DWORD value)
{
    memcpy(data + *offset, &value, 4);
    *offset += 4;
}

static void PutString(BYTE *data, DWORD *offset, const char *text)
{
    DWORD length = text ? (DWORD)strlen(text) : 0;
    PutDword(data, offset, length);
    if (length)
        memcpy(data + *offset, text, length);
    *offset += length;
}

BOOL WINAPI WinHttpSetDefaultProxyConfiguration(WINHTTP_PROXY_INFO *info)
{
    if (!info)
        return WhFail(ERROR_INVALID_PARAMETER);
    if (info->dwAccessType == WINHTTP_ACCESS_TYPE_NO_PROXY) {
        if (info->lpszProxy || info->lpszProxyBypass)
            return WhFail(ERROR_INVALID_PARAMETER);
    } else if (info->dwAccessType != WINHTTP_ACCESS_TYPE_NAMED_PROXY || !info->lpszProxy) {
        return WhFail(ERROR_INVALID_PARAMETER);
    }
    char *proxy = info->lpszProxy ? WhWideToAnsi(info->lpszProxy, (DWORD)wcslen(info->lpszProxy), nullptr) : nullptr;
    char *bypass = info->lpszProxyBypass
                       ? WhWideToAnsi(info->lpszProxyBypass, (DWORD)wcslen(info->lpszProxyBypass), nullptr)
                       : nullptr;
    DWORD size = 20 + (proxy ? (DWORD)strlen(proxy) : 0) + (bypass ? (DWORD)strlen(bypass) : 0);
    BYTE *data = WhAlloc(size);
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    if (data) {
        DWORD old_size, counter = 0;
        BYTE *old = ReadBlob(HKEY_LOCAL_MACHINE, L"WinHttpSettings", &old_size, FALSE);
        if (old && old_size >= 8)
            memcpy(&counter, old + 4, 4);
        WhFree(old);
        DWORD offset = 0;
        PutDword(data, &offset, 0x28);
        PutDword(data, &offset, counter + 1);
        PutDword(data, &offset, info->dwAccessType);
        PutString(data, &offset, proxy);
        PutString(data, &offset, bypass);
        HKEY key;
        error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, CONNECTIONS_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                nullptr);
        if (!error) {
            error = RegSetValueExW(key, L"WinHttpSettings", 0, REG_BINARY, data, offset);
            RegCloseKey(key);
        }
        WhFree(data);
    }
    WhFree(proxy);
    WhFree(bypass);
    SetLastError(error);
    return !error;
}

BOOL WINAPI WinHttpGetIEProxyConfigForCurrentUser(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *config)
{
    if (config)
        memset(config, 0, sizeof(*config));
    if (!config)
        return WhFail(ERROR_INVALID_PARAMETER);
    DWORD size;
    BYTE *data = ReadBlob(HKEY_CURRENT_USER, L"DefaultConnectionSettings", &size, TRUE);
    DWORD flags = BLOB_AUTO_DETECT;
    PWSTR proxy = nullptr, bypass = nullptr, url = nullptr;
    WH_BLOB blob = {data, size, 0};
    /* without settings, or with settings older than ie 5, windows reports automatic detection. */
    if (data && BlobDword(&blob) >= 0x3C) {
        BlobDword(&blob);
        flags = BlobDword(&blob);
        proxy = BlobString(&blob);
        bypass = BlobString(&blob);
        url = BlobString(&blob);
    }
    WhFree(data);
    if (flags & BLOB_AUTOCONFIG_URL)
        config->lpszAutoConfigUrl = url;
    else
        GlobalFree(url);
    if (flags & BLOB_PROXY) {
        config->lpszProxy = proxy;
        config->lpszProxyBypass = bypass;
    } else {
        GlobalFree(proxy);
        GlobalFree(bypass);
    }
    config->fAutoDetect = (flags & BLOB_AUTO_DETECT) != 0;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* ------------------------------------------------------------------ proxy lists */

/* the entry of a proxy list for a scheme: "scheme=host:port" first, else an entry without a scheme. */
static BOOL ProxyForScheme(LPCWSTR list, BOOL secure, PWSTR host, DWORD capacity, INTERNET_PORT *port)
{
    LPCWSTR scheme = secure ? L"https" : L"http";
    LPCWSTR best = nullptr, best_end = nullptr;
    for (LPCWSTR p = list; *p;) {
        while (*p == ';' || *p == ' ' || *p == '\t')
            p++;
        LPCWSTR start = p;
        while (*p && *p != ';' && *p != ' ' && *p != '\t')
            p++;
        if (p == start)
            continue;
        LPCWSTR equals = start;
        while (equals < p && *equals != '=')
            equals++;
        if (equals < p) {
            if ((DWORD)(equals - start) == wcslen(scheme) && !_wcsnicmp(start, scheme, equals - start)) {
                best = equals + 1;
                best_end = p;
                break;
            }
        } else if (!best) {
            best = start;
            best_end = p;
        }
    }
    if (!best)
        return FALSE;
    if (best_end - best > 7 && !_wcsnicmp(best, L"http://", 7))
        best += 7;
    LPCWSTR colon = best_end;
    for (LPCWSTR q = best; q < best_end; q++)
        if (*q == ':')
            colon = q;
    DWORD length = (DWORD)(colon - best);
    if (!length || length >= capacity)
        return FALSE;
    memcpy(host, best, length * sizeof(WCHAR));
    host[length] = 0;
    DWORD value = 0;
    for (LPCWSTR q = colon + 1; q < best_end && *q >= '0' && *q <= '9'; q++)
        value = value * 10 + (*q - '0');
    *port = (INTERNET_PORT)(colon < best_end && value ? value : 80);
    return TRUE;
}

static BOOL Wildcard(LPCWSTR pattern, DWORD pattern_length, LPCWSTR text)
{
    if (!pattern_length)
        return !*text;
    if (*pattern == '*') {
        for (LPCWSTR t = text;; t++) {
            if (Wildcard(pattern + 1, pattern_length - 1, t))
                return TRUE;
            if (!*t)
                return FALSE;
        }
    }
    if (!*text || towlower(*pattern) != towlower(*text))
        return FALSE;
    return Wildcard(pattern + 1, pattern_length - 1, text + 1);
}

static BOOL Bypassed(LPCWSTR list, LPCWSTR host)
{
    if (!list)
        return FALSE;
    for (LPCWSTR p = list; *p;) {
        while (*p == ';' || *p == ' ' || *p == '\t' || *p == ',')
            p++;
        LPCWSTR start = p;
        while (*p && *p != ';' && *p != ' ' && *p != '\t' && *p != ',')
            p++;
        DWORD length = (DWORD)(p - start);
        if (!length)
            continue;
        if (length == 7 && !_wcsnicmp(start, L"<local>", 7)) {
            if (!wcschr(host, '.'))
                return TRUE;
            continue;
        }
        if (length > 7 && !_wcsnicmp(start, L"http://", 7)) {
            start += 7;
            length -= 7;
        }
        if (Wildcard(start, length, host))
            return TRUE;
    }
    return FALSE;
}

BOOL WhPickProxy(WH_OBJECT *request, LPCWSTR host, BOOL secure, PWSTR proxy_host, DWORD capacity,
                 INTERNET_PORT *proxy_port)
{
    BOOL found = FALSE;
    if (request->AccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && request->Proxy) {
        found = !Bypassed(request->ProxyBypass, host) &&
                ProxyForScheme(request->Proxy, secure, proxy_host, capacity, proxy_port);
    } else if (request->AccessType == WINHTTP_ACCESS_TYPE_DEFAULT_PROXY) {
        WINHTTP_PROXY_INFO info;
        WhReadDefaultProxy(&info);
        if (info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && info.lpszProxy)
            found = !Bypassed(info.lpszProxyBypass, host) &&
                    ProxyForScheme(info.lpszProxy, secure, proxy_host, capacity, proxy_port);
        GlobalFree(info.lpszProxy);
        GlobalFree(info.lpszProxyBypass);
    }
    return found;
}

/* ------------------------------------------------------------------ auto proxy */

static BOOL ValidOptions(const WINHTTP_AUTOPROXY_OPTIONS *options)
{
    DWORD flags = options->dwFlags;
    if (!(flags & 0x703) || flags & 0xF800F8F0 || (flags & 0x30000) == 0x30000)
        return FALSE;
    if (flags & WINHTTP_AUTOPROXY_AUTO_DETECT && (options->dwAutoDetectFlags & ~3u || !(options->dwAutoDetectFlags & 3)))
        return FALSE;
    if (flags & WINHTTP_AUTOPROXY_CONFIG_URL && (!options->lpszAutoConfigUrl || !*options->lpszAutoConfigUrl))
        return FALSE;
    return TRUE;
}

/* downloads a proxy script with a synchronous session of its own. */
static DWORD Download(LPCWSTR url, char **script, DWORD *length)
{
    *script = nullptr;
    *length = 0;
    URL_COMPONENTS uc = {sizeof(uc)};
    uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WhCrack(url, 0, 0, &uc) || (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS))
        return ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT;
    PWSTR host = WhDupWn(uc.lpszHostName, uc.dwHostNameLength);
    PWSTR path = WhDupWn(uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength);
    DWORD error = ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT;
    HINTERNET session = WinHttpOpen(L"", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    HINTERNET connect = session && host ? WinHttpConnect(session, host, uc.nPort, 0) : nullptr;
    HINTERNET request = connect && path ? WinHttpOpenRequest(connect, L"GET", *path ? path : L"/", nullptr, nullptr,
                                                             nullptr,
                                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE
                                                                                                 : 0)
                                        : nullptr;
    DWORD status = 0, size = sizeof(status);
    if (request && WinHttpSetTimeouts(request, 10000, 10000, 10000, 10000) &&
        WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size,
                            nullptr) &&
        status == 200) {
        char *data = nullptr;
        DWORD used = 0, capacity = 0, got;
        error = ERROR_SUCCESS;
        while (!error) {
            if (capacity - used < 4096) {
                capacity = capacity ? capacity * 2 : 16384;
                char *grown = WhRealloc(data, capacity + 1);
                if (!grown) {
                    error = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }
                data = grown;
            }
            if (!WinHttpReadData(request, data + used, capacity - used, &got))
                error = ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT;
            else if (!got)
                break;
            used += got;
        }
        if (error) {
            WhFree(data);
        } else {
            data[used] = 0;
            *script = data;
            *length = used;
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    WhFree(host);
    WhFree(path);
    return error;
}

/*
 * mime has no javascript engine: a script whose FindProxyForURL returns one constant string is
 * understood, anything else is reported as a script windows could not run.
 */
static DWORD RunScript(const char *script, PWSTR *answer)
{
    const char *function = strstr(script, "FindProxyForURL");
    if (!function)
        return ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
    const char *ret = strstr(function, "return");
    if (!ret || strstr(ret + 6, "return") || strstr(function, "if"))
        return ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
    const char *p = ret + 6;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != '"' && *p != '\'')
        return ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
    char quote = *p++;
    const char *end = strchr(p, quote);
    if (!end)
        return ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
    *answer = WhAnsiToWide(p, (DWORD)(end - p));
    return *answer ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

/* "DIRECT" or "PROXY host:port; ..." into proxy result entries. */
static DWORD ParseAnswer(LPCWSTR answer, WINHTTP_PROXY_RESULT *result)
{
    DWORD count = 1;
    for (LPCWSTR p = answer; *p; p++)
        if (*p == ';')
            count++;
    WINHTTP_PROXY_RESULT_ENTRY *entries = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, count * sizeof(*entries));
    if (!entries)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD used = 0;
    for (LPCWSTR p = answer; *p;) {
        while (*p == ';' || *p == ' ' || *p == '\t')
            p++;
        LPCWSTR start = p;
        while (*p && *p != ';')
            p++;
        LPCWSTR end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if (end == start)
            continue;
        WINHTTP_PROXY_RESULT_ENTRY *entry = &entries[used];
        if (end - start == 6 && !_wcsnicmp(start, L"DIRECT", 6)) {
            used++;
            continue;
        }
        if (end - start > 6 && !_wcsnicmp(start, L"PROXY ", 6)) {
            LPCWSTR host = start + 6;
            while (*host == ' ')
                host++;
            LPCWSTR colon = end;
            for (LPCWSTR q = host; q < end; q++)
                if (*q == ':')
                    colon = q;
            DWORD port = 0;
            for (LPCWSTR q = colon + 1; q < end && *q >= '0' && *q <= '9'; q++)
                port = port * 10 + (*q - '0');
            entry->fProxy = TRUE;
            entry->ProxyScheme = INTERNET_SCHEME_HTTP;
            entry->pwszProxy = GlobalDupW(host, (DWORD)(colon - host));
            entry->ProxyPort = (INTERNET_PORT)(port ? port : 80);
            used++;
        }
    }
    if (!used) {
        used = 1;
        memset(entries, 0, sizeof(*entries));
    }
    result->cEntries = used;
    result->pEntries = entries;
    return ERROR_SUCCESS;
}

/* a step of the proxy resolution that took long, to the debug output: a browser's first request waits for it. */
static void TraceSlow(const char *what, ULONGLONG began, DWORD error)
{
    ULONGLONG took = GetTickCount64() - began;
    if (took < 100)
        return;
    char text[96];
    wsprintfA(text, "winhttp: %s took %lu ms, error %lu\n", what, (DWORD)took, error);
    OutputDebugStringA(text);
}

static DWORD DetectUrl(DWORD flags, PWSTR *url)
{
    *url = nullptr;
    WhStartup();
    ADDRINFOW hints = {0}, *addresses = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    /* dhcp option 252 does not exist under mime, so only the dns name of wpad is tried. */
    ULONGLONG began = GetTickCount64();
    if (!(flags & WINHTTP_AUTO_DETECT_TYPE_DNS_A) || GetAddrInfoW(L"wpad", L"80", &hints, &addresses) || !addresses) {
        TraceSlow("the lookup of wpad", began, ERROR_WINHTTP_AUTODETECTION_FAILED);
        return ERROR_WINHTTP_AUTODETECTION_FAILED;
    }
    TraceSlow("the lookup of wpad", began, 0);
    FreeAddrInfoW(addresses);
    *url = WhDupW(L"http://wpad/wpad.dat");
    return *url ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

/* the whole resolution of WinHttpGetProxyForUrl and the resolver, into a proxy result. */
static DWORD Resolve(LPCWSTR url, const WINHTTP_AUTOPROXY_OPTIONS *options, WINHTTP_PROXY_RESULT *result)
{
    memset(result, 0, sizeof(*result));
    PWSTR script_url = nullptr;
    DWORD error = ERROR_WINHTTP_AUTODETECTION_FAILED;
    if (options->dwFlags & WINHTTP_AUTOPROXY_AUTO_DETECT)
        error = DetectUrl(options->dwAutoDetectFlags, &script_url);
    if (!script_url && options->dwFlags & WINHTTP_AUTOPROXY_CONFIG_URL) {
        script_url = WhDupW(options->lpszAutoConfigUrl);
        error = script_url ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error)
        return error;
    char *script;
    DWORD length;
    ULONGLONG began = GetTickCount64();
    error = Download(script_url, &script, &length);
    TraceSlow("the download of the proxy script", began, error);
    WhFree(script_url);
    if (error)
        return error;
    PWSTR answer = nullptr;
    error = RunScript(script, &answer);
    WhFree(script);
    if (!error)
        error = ParseAnswer(answer, result);
    WhFree(answer);
    return error;
}

void WINAPI WinHttpFreeProxyResult(WINHTTP_PROXY_RESULT *result)
{
    if (!result)
        return;
    for (DWORD i = 0; i < result->cEntries; i++)
        if (result->pEntries[i].pwszProxy)
            GlobalFree(result->pEntries[i].pwszProxy);
    if (result->pEntries)
        GlobalFree(result->pEntries);
    memset(result, 0, sizeof(*result));
}

void WINAPI WinHttpFreeProxyResultEx(WINHTTP_PROXY_RESULT_EX *result)
{
    if (!result)
        return;
    WINHTTP_PROXY_RESULT plain = {result->cEntries, result->pEntries};
    WinHttpFreeProxyResult(&plain);
    memset(result, 0, sizeof(*result));
}

BOOL WINAPI WinHttpGetProxyForUrl(HINTERNET handle, LPCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *options,
                                  WINHTTP_PROXY_INFO *info)
{
    if (!handle)
        return WhFail(ERROR_INVALID_HANDLE);
    if (!url || !options || !info || !ValidOptions(options))
        return WhFail(ERROR_INVALID_PARAMETER);
    WH_OBJECT *session = WhReference(handle);
    if (!session)
        return FALSE;
    DWORD type = session->Type;
    WhRelease(session);
    if (type != WH_SESSION)
        return WhFail(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
    memset(info, 0, sizeof(*info));
    WINHTTP_PROXY_RESULT result;
    DWORD error = Resolve(url, options, &result);
    if (error == ERROR_WINHTTP_UNHANDLED_SCRIPT_TYPE || error == ERROR_WINHTTP_SCRIPT_EXECUTION_ERROR)
        error = ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
    if (error)
        return WhFail(error);
    /* the entries become a proxy list, or no proxy when the first answer is DIRECT. */
    info->dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
    if (result.cEntries && result.pEntries[0].fProxy) {
        SIZE_T size = 1;
        for (DWORD i = 0; i < result.cEntries; i++)
            if (result.pEntries[i].fProxy)
                size += wcslen(result.pEntries[i].pwszProxy) + 8;
        PWSTR list = GlobalAlloc(GMEM_FIXED, size * sizeof(WCHAR));
        if (list) {
            list[0] = 0;
            for (DWORD i = 0; i < result.cEntries; i++)
                if (result.pEntries[i].fProxy) {
                    PWSTR end = list + wcslen(list);
                    wsprintfW(end, L"%s%s:%u", *list ? L";" : L"", result.pEntries[i].pwszProxy,
                              result.pEntries[i].ProxyPort);
                }
            info->dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            info->lpszProxy = list;
        }
    }
    WinHttpFreeProxyResult(&result);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI WinHttpDetectAutoProxyConfigUrl(DWORD flags, LPWSTR *url)
{
    if (!url)
        return WhFail(ERROR_INVALID_PARAMETER);
    *url = nullptr;
    if (flags & ~3u || !(flags & 3))
        return WhFail(ERROR_INVALID_PARAMETER);
    PWSTR found;
    DWORD error = DetectUrl(flags, &found);
    if (error)
        return WhFail(ERROR_WINHTTP_AUTODETECTION_FAILED);
    *url = GlobalDupW(found, (DWORD)wcslen(found));
    WhFree(found);
    if (!*url)
        return WhFail(ERROR_NOT_ENOUGH_MEMORY);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* ------------------------------------------------------------------ the resolver */

DWORD WINAPI WinHttpCreateProxyResolver(HINTERNET handle, HINTERNET *resolver)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!resolver)
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *session = WhReference(handle);
    if (!session)
        return ERROR_INVALID_HANDLE;
    if (session->Type != WH_SESSION || !(session->Flags & WINHTTP_FLAG_ASYNC)) {
        WhRelease(session);
        return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    }
    WH_OBJECT *object = WhCreate(WH_RESOLVER, session);
    WhRelease(session);
    if (!object)
        return GetLastError();
    *resolver = object->Handle;
    WhRelease(object);
    return ERROR_SUCCESS;
}

typedef struct {
    WH_OBJECT *Resolver;
    PWSTR Url;
    WINHTTP_AUTOPROXY_OPTIONS Options;
    PWSTR ConfigUrl;
} WH_RESOLVE_JOB;

static DWORD WINAPI ResolveJob(LPVOID parameter)
{
    WH_RESOLVE_JOB *job = parameter;
    WH_OBJECT *resolver = job->Resolver;
    WINHTTP_PROXY_RESULT result;
    DWORD error = Resolve(job->Url, &job->Options, &result);
    if (error == ERROR_WINHTTP_UNHANDLED_SCRIPT_TYPE || error == ERROR_WINHTTP_SCRIPT_EXECUTION_ERROR)
        error = ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
    EnterCriticalSection(&resolver->Lock);
    if (!error) {
        resolver->ResolverResult = result;
        resolver->ResolverDone = TRUE;
    }
    resolver->ResolverBusy = FALSE;
    LeaveCriticalSection(&resolver->Lock);
    if (error) {
        WINHTTP_ASYNC_RESULT async = {API_GET_PROXY_FOR_URL, error};
        WhNotify(resolver, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &async, sizeof(async));
    } else {
        WhNotify(resolver, WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE, nullptr, 0);
    }
    WhFree(job->Url);
    WhFree(job->ConfigUrl);
    WhFree(job);
    WhRelease(resolver);
    return 0;
}

DWORD WINAPI WinHttpGetProxyForUrlEx2(HINTERNET handle, PCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *options,
                                      DWORD selection_size, BYTE *selection, DWORD_PTR context)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!url || !options || !selection_size != !selection || !ValidOptions(options))
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *resolver = WhReference(handle);
    if (!resolver)
        return ERROR_INVALID_HANDLE;
    if (resolver->Type != WH_RESOLVER) {
        WhRelease(resolver);
        return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    }
    EnterCriticalSection(&resolver->Lock);
    /* one resolution per resolver, reported through a callback that takes completions and errors. */
    if (resolver->ResolverBusy || resolver->ResolverUsed || !resolver->Callback ||
        (resolver->NotifyFlags & (WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE | WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)) !=
            (WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE | WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)) {
        LeaveCriticalSection(&resolver->Lock);
        WhRelease(resolver);
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    }
    WH_RESOLVE_JOB *job = WhAllocZero(sizeof(*job));
    if (job) {
        job->Resolver = resolver;
        job->Url = WhDupW(url);
        job->Options = *options;
        job->ConfigUrl = WhDupW(options->lpszAutoConfigUrl);
        job->Options.lpszAutoConfigUrl = job->ConfigUrl;
    }
    if (!job || !job->Url || !QueueUserWorkItem(ResolveJob, job, 0)) {
        LeaveCriticalSection(&resolver->Lock);
        if (job) {
            WhFree(job->Url);
            WhFree(job->ConfigUrl);
            WhFree(job);
        }
        WhRelease(resolver);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    resolver->Context = context;
    resolver->ResolverBusy = TRUE;
    resolver->ResolverUsed = TRUE;
    LeaveCriticalSection(&resolver->Lock);
    return ERROR_IO_PENDING;
}

DWORD WINAPI WinHttpGetProxyForUrlEx(HINTERNET handle, PCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *options,
                                     DWORD_PTR context)
{
    return WinHttpGetProxyForUrlEx2(handle, url, options, 0, nullptr, context);
}

static DWORD GetResult(HINTERNET handle, WINHTTP_PROXY_RESULT *result)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    WH_OBJECT *resolver = WhReference(handle);
    if (!resolver)
        return ERROR_INVALID_HANDLE;
    if (resolver->Type != WH_RESOLVER) {
        WhRelease(resolver);
        return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    }
    DWORD error = ERROR_SUCCESS;
    EnterCriticalSection(&resolver->Lock);
    if (!resolver->ResolverDone || resolver->Closed) {
        error = ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    } else {
        WINHTTP_PROXY_RESULT *source = &resolver->ResolverResult;
        result->pEntries = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, source->cEntries * sizeof(*result->pEntries) + 1);
        if (!result->pEntries) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            result->cEntries = source->cEntries;
            for (DWORD i = 0; i < source->cEntries; i++) {
                result->pEntries[i] = source->pEntries[i];
                if (source->pEntries[i].pwszProxy)
                    result->pEntries[i].pwszProxy =
                        GlobalDupW(source->pEntries[i].pwszProxy, (DWORD)wcslen(source->pEntries[i].pwszProxy));
            }
        }
    }
    LeaveCriticalSection(&resolver->Lock);
    WhRelease(resolver);
    return error;
}

DWORD WINAPI WinHttpGetProxyResult(HINTERNET handle, WINHTTP_PROXY_RESULT *result)
{
    if (result)
        memset(result, 0, sizeof(*result));
    if (handle && !result)
        return ERROR_INVALID_PARAMETER;
    return GetResult(handle, result);
}

DWORD WINAPI WinHttpGetProxyResultEx(HINTERNET handle, WINHTTP_PROXY_RESULT_EX *result)
{
    if (result)
        memset(result, 0, sizeof(*result));
    if (handle && !result)
        return ERROR_INVALID_PARAMETER;
    WINHTTP_PROXY_RESULT plain = {0};
    DWORD error = GetResult(handle, &plain);
    if (!error) {
        result->cEntries = plain.cEntries;
        result->pEntries = plain.pEntries;
    }
    return error;
}

DWORD WINAPI WinHttpResetAutoProxy(HINTERNET handle, DWORD flags)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (flags & ~0x3u)
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *session = WhReference(handle);
    if (!session)
        return ERROR_INVALID_HANDLE;
    DWORD type = session->Type;
    WhRelease(session);
    return type == WH_SESSION ? ERROR_SUCCESS : ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
}

DWORD WINAPI Private1(const WINHTTP_PROXY_INFO *info, PCWSTR host, INTERNET_SCHEME scheme, INTERNET_PORT port,
                      BOOL *bypassed)
{
    if (!info || !host || !bypassed)
        return ERROR_INVALID_PARAMETER;
    *bypassed = Bypassed(info->lpszProxyBypass, host);
    return ERROR_SUCCESS;
}
