/*
 * proxies: the list of INTERNET_OPEN_TYPE_PROXY, or internet explorer's settings for
 * INTERNET_OPEN_TYPE_PRECONFIG, the entry for a scheme and the bypass list.
 */
#include "wininet_private.h"

#define SETTINGS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"
#define CONNECTIONS_KEY SETTINGS_KEY L"\\Connections"

/* the flags of the connection settings blob. */
#define BLOB_PROXY 0x2

static BYTE *ReadValue(LPCWSTR path, LPCWSTR name, DWORD type, DWORD *size)
{
    HKEY key;
    *size = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key))
        return nullptr;
    DWORD actual, length = 0;
    BYTE *data = nullptr;
    if (!RegQueryValueExW(key, name, nullptr, &actual, nullptr, &length) && actual == type &&
        (data = InetAlloc((SIZE_T)length + sizeof(WCHAR))) &&
        RegQueryValueExW(key, name, nullptr, &actual, data, &length)) {
        InetFree(data);
        data = nullptr;
    }
    RegCloseKey(key);
    if (data) {
        memset(data + length, 0, sizeof(WCHAR));
        *size = length;
    }
    return data;
}

static DWORD BlobDword(const BYTE *data, DWORD size, DWORD *offset)
{
    DWORD value = 0;
    if (*offset + 4 <= size)
        memcpy(&value, data + *offset, 4);
    *offset += 4;
    return value;
}

/* a length prefixed ansi string of the blob, NULL when empty or cut. */
static PWSTR BlobString(const BYTE *data, DWORD size, DWORD *offset)
{
    DWORD length = BlobDword(data, size, offset);
    if (*offset > size || length > size - *offset) {
        *offset = size;
        return nullptr;
    }
    const char *text = (const char *)data + *offset;
    *offset += length;
    return length ? InetWiden(text, (int)length) : nullptr;
}

/*
 * the per user settings: the DefaultConnectionSettings blob internet explorer writes, else the older
 * ProxyEnable, ProxyServer and ProxyOverride values.
 */
BOOL InetReadIEProxy(PWSTR *proxy, PWSTR *bypass)
{
    *proxy = *bypass = nullptr;
    DWORD size;
    BYTE *blob = ReadValue(CONNECTIONS_KEY, L"DefaultConnectionSettings", REG_BINARY, &size);
    if (blob && size >= 12) {
        DWORD offset = 8;
        DWORD flags = BlobDword(blob, size, &offset);
        PWSTR list = BlobString(blob, size, &offset), skip = BlobString(blob, size, &offset);
        InetFree(blob);
        if (flags & BLOB_PROXY && list) {
            *proxy = list;
            *bypass = skip;
            return TRUE;
        }
        InetFree(list);
        InetFree(skip);
        return FALSE;
    }
    InetFree(blob);
    DWORD *enable = (DWORD *)ReadValue(SETTINGS_KEY, L"ProxyEnable", REG_DWORD, &size);
    BOOL on = enable && size >= sizeof(DWORD) && *enable;
    InetFree(enable);
    if (!on)
        return FALSE;
    *proxy = (PWSTR)ReadValue(SETTINGS_KEY, L"ProxyServer", REG_SZ, &size);
    *bypass = (PWSTR)ReadValue(SETTINGS_KEY, L"ProxyOverride", REG_SZ, &size);
    if (*proxy && **proxy)
        return TRUE;
    InetFree(*proxy);
    InetFree(*bypass);
    *proxy = *bypass = nullptr;
    return FALSE;
}

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
            if ((DWORD)(equals - start) == (DWORD)lstrlenW(scheme) && !_wcsnicmp(start, scheme, equals - start)) {
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
    /* a bracketed ipv6 address keeps its colons. */
    if (best[0] == '[') {
        LPCWSTR close = best;
        while (close < best_end && *close != ']')
            close++;
        colon = close < best_end && close + 1 < best_end && close[1] == ':' ? close + 1 : best_end;
    }
    DWORD length = (DWORD)(colon - best);
    if (!length || length >= capacity)
        return FALSE;
    memcpy(host, best, length * sizeof(WCHAR));
    host[length] = 0;
    DWORD value = 0;
    for (LPCWSTR q = colon + 1; q < best_end && *q >= '0' && *q <= '9'; q++)
        value = value * 10 + (DWORD)(*q - '0');
    *port = (INTERNET_PORT)(colon < best_end && value ? value : INTERNET_DEFAULT_HTTP_PORT);
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
        } else if (length > 8 && !_wcsnicmp(start, L"https://", 8)) {
            start += 8;
            length -= 8;
        }
        if (Wildcard(start, length, host))
            return TRUE;
    }
    return FALSE;
}

BOOL InetPickProxy(INET_OBJECT *request, LPCWSTR host, BOOL secure, PWSTR proxy, DWORD capacity,
                   INTERNET_PORT *proxy_port)
{
    INET_OBJECT *session = InetSession(request);
    if (!session || session->AccessType == INTERNET_OPEN_TYPE_DIRECT)
        return FALSE;
    if (session->AccessType == INTERNET_OPEN_TYPE_PROXY) {
        if (!session->Proxy)
            return FALSE;
        return !Bypassed(session->ProxyBypass, host) &&
               ProxyForScheme(session->Proxy, secure, proxy, capacity, proxy_port);
    }
    PWSTR list, bypass;
    if (!InetReadIEProxy(&list, &bypass))
        return FALSE;
    BOOL found = !Bypassed(bypass, host) && ProxyForScheme(list, secure, proxy, capacity, proxy_port);
    InetFree(list);
    InetFree(bypass);
    return found;
}
