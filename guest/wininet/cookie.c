/*
 * cookies: the jar of the process, filled from Set-Cookie headers and InternetSetCookie, read for Cookie
 * headers and InternetGetCookie, after rfc 6265. windows keeps persistent cookies on disk; mime keeps them
 * for the life of the process.
 */
#include "wininet_private.h"

typedef struct INET_COOKIE {
    struct INET_COOKIE *Next;
    char *Name;
    char *Value;
    char *Domain;
    char *Path;
    BOOL HostOnly;
    BOOL Secure;
    BOOL HttpOnly;
    /* in FILETIME units, 0 for a session cookie. */
    ULONGLONG Expires;
    ULONGLONG Created;
} INET_COOKIE;

static SRWLOCK JarLock = SRWLOCK_INIT;
static INET_COOKIE *Jar;
static ULONGLONG Sequence;

static ULONGLONG Now(void)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return (ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime;
}

static void FreeCookie(INET_COOKIE *cookie)
{
    InetFree(cookie->Name);
    InetFree(cookie->Value);
    InetFree(cookie->Domain);
    InetFree(cookie->Path);
    InetFree(cookie);
}

static void Lower(char *text)
{
    for (; *text; text++)
        if (*text >= 'A' && *text <= 'Z')
            *text = (char)(*text + 32);
}

static BOOL IsAddress(const char *host)
{
    if (host[0] == '[')
        return TRUE;
    for (const char *p = host; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '.'))
            return FALSE;
    return TRUE;
}

/* rfc 6265 5.1.3. */
static BOOL DomainMatch(const char *host, const char *domain, BOOL host_only)
{
    if (!lstrcmpiA(host, domain))
        return TRUE;
    if (host_only || IsAddress(host))
        return FALSE;
    SIZE_T host_length = strlen(host), domain_length = strlen(domain);
    return host_length > domain_length && host[host_length - domain_length - 1] == '.' &&
           !lstrcmpiA(host + host_length - domain_length, domain);
}

/* rfc 6265 5.1.4. */
static BOOL PathMatch(const char *path, const char *cookie_path)
{
    SIZE_T length = strlen(cookie_path);
    if (strncmp(path, cookie_path, length))
        return FALSE;
    return path[length] == 0 || cookie_path[length - 1] == '/' || path[length] == '/';
}

static char *DefaultPath(const char *path)
{
    const char *end = path[0] == '/' ? strrchr(path, '/') : nullptr;
    if (!end || end == path)
        return InetDuplicateA("/", 1);
    return InetDuplicateA(path, (DWORD)(end - path));
}

static const char *Trim(const char *start, const char *end, DWORD *length)
{
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    *length = (DWORD)(end - start);
    return start;
}

/* stores one Set-Cookie line for a request to host and path, as rfc 6265 5.2 and 5.3 read it. */
static BOOL Store(const char *host, const char *path, const char *line, BOOL from_script)
{
    const char *semicolon = strchr(line, ';');
    const char *pair_end = semicolon ? semicolon : line + strlen(line);
    const char *equals = line;
    while (equals < pair_end && *equals != '=')
        equals++;
    DWORD name_length, value_length;
    const char *name, *value;
    if (equals < pair_end) {
        name = Trim(line, equals, &name_length);
        value = Trim(equals + 1, pair_end, &value_length);
    } else {
        /* a cookie without a name, as windows accepts. */
        name = "";
        name_length = 0;
        value = Trim(line, pair_end, &value_length);
    }
    if (!name_length && !value_length)
        return FALSE;
    INET_COOKIE *cookie = InetAllocZero(sizeof(*cookie));
    if (!cookie)
        return FALSE;
    cookie->Name = InetDuplicateA(name, name_length);
    cookie->Value = InetDuplicateA(value, value_length);
    BOOL max_age = FALSE;
    ULONGLONG now = Now();
    for (const char *p = semicolon; p && *p;) {
        p++;
        const char *end = strchr(p, ';');
        if (!end)
            end = p + strlen(p);
        const char *eq = p;
        while (eq < end && *eq != '=')
            eq++;
        DWORD key_length, attribute_length = 0;
        const char *key = Trim(p, eq, &key_length);
        const char *attribute = eq < end ? Trim(eq + 1, end, &attribute_length) : "";
        char *text = InetDuplicateA(attribute, attribute_length);
        if (key_length == 7 && !_strnicmp(key, "expires", 7) && text && !max_age) {
            SYSTEMTIME time;
            FILETIME file;
            if (InetParseTime(text, &time) && SystemTimeToFileTime(&time, &file))
                cookie->Expires = (ULONGLONG)file.dwHighDateTime << 32 | file.dwLowDateTime;
        } else if (key_length == 7 && !_strnicmp(key, "max-age", 7) && text && attribute_length) {
            BOOL negative = text[0] == '-';
            LONGLONG seconds = 0;
            const char *digit = text + negative;
            while (*digit >= '0' && *digit <= '9' && seconds < 0x7FFFFFFF)
                seconds = seconds * 10 + (*digit++ - '0');
            if (!*digit) {
                max_age = TRUE;
                cookie->Expires = negative || !seconds ? 1 : now + (ULONGLONG)seconds * 10000000;
            }
        } else if (key_length == 6 && !_strnicmp(key, "domain", 6) && text && attribute_length) {
            InetFree(cookie->Domain);
            cookie->Domain = InetDuplicateA(text[0] == '.' ? text + 1 : text, (DWORD)-1);
        } else if (key_length == 4 && !_strnicmp(key, "path", 4) && text && text[0] == '/') {
            InetFree(cookie->Path);
            cookie->Path = InetDuplicateA(text, (DWORD)-1);
        } else if (key_length == 6 && !_strnicmp(key, "secure", 6)) {
            cookie->Secure = TRUE;
        } else if (key_length == 8 && !_strnicmp(key, "httponly", 8)) {
            cookie->HttpOnly = TRUE;
        }
        InetFree(text);
        p = end;
    }
    if (cookie->Domain) {
        Lower(cookie->Domain);
        if (!DomainMatch(host, cookie->Domain, FALSE)) {
            FreeCookie(cookie);
            return FALSE;
        }
    } else {
        cookie->Domain = InetDuplicateA(host, (DWORD)-1);
        cookie->HostOnly = TRUE;
        if (cookie->Domain)
            Lower(cookie->Domain);
    }
    if (!cookie->Path)
        cookie->Path = DefaultPath(path);
    if (!cookie->Name || !cookie->Value || !cookie->Domain || !cookie->Path || (from_script && cookie->HttpOnly)) {
        FreeCookie(cookie);
        return FALSE;
    }
    BOOL expired = cookie->Expires && cookie->Expires <= now;
    AcquireSRWLockExclusive(&JarLock);
    cookie->Created = ++Sequence;
    for (INET_COOKIE **link = &Jar; *link; link = &(*link)->Next) {
        INET_COOKIE *old = *link;
        if (!strcmp(old->Name, cookie->Name) && !lstrcmpiA(old->Domain, cookie->Domain) &&
            !strcmp(old->Path, cookie->Path)) {
            *link = old->Next;
            cookie->Created = old->Created;
            FreeCookie(old);
            break;
        }
    }
    if (!expired) {
        cookie->Next = Jar;
        Jar = cookie;
    }
    ReleaseSRWLockExclusive(&JarLock);
    if (expired)
        FreeCookie(cookie);
    return TRUE;
}

void InetCookieStore(LPCWSTR host, LPCWSTR path, const char *set_cookie)
{
    char *narrow_host = InetNarrow(host, -1), *narrow_path = InetNarrow(path, -1);
    if (narrow_host && narrow_path) {
        char *query = strchr(narrow_path, '?');
        if (query)
            *query = 0;
        Store(narrow_host, narrow_path, set_cookie, FALSE);
    }
    InetFree(narrow_host);
    InetFree(narrow_path);
}

/* the cookies for a request, longest paths first, then oldest first, as "a=b; c=d". */
static char *Collect(const char *host, const char *path, BOOL secure, BOOL http, const char *name)
{
    ULONGLONG now = Now();
    INET_COOKIE *matches[256];
    DWORD count = 0;
    AcquireSRWLockExclusive(&JarLock);
    for (INET_COOKIE **link = &Jar; *link;) {
        INET_COOKIE *cookie = *link;
        if (cookie->Expires && cookie->Expires <= now) {
            *link = cookie->Next;
            FreeCookie(cookie);
            continue;
        }
        link = &cookie->Next;
        if (count == ARRAYSIZE(matches) || !DomainMatch(host, cookie->Domain, cookie->HostOnly) ||
            !PathMatch(path, cookie->Path) || (cookie->Secure && !secure) || (cookie->HttpOnly && !http) ||
            (name && strcmp(name, cookie->Name)))
            continue;
        matches[count++] = cookie;
    }
    for (DWORD i = 1; i < count; i++)
        for (DWORD k = i; k > 0; k--) {
            INET_COOKIE *a = matches[k - 1], *b = matches[k];
            SIZE_T la = strlen(a->Path), lb = strlen(b->Path);
            if (la > lb || (la == lb && a->Created <= b->Created))
                break;
            matches[k - 1] = b;
            matches[k] = a;
        }
    SIZE_T length = 1;
    for (DWORD i = 0; i < count; i++)
        length += strlen(matches[i]->Name) + strlen(matches[i]->Value) + 3;
    char *text = count ? InetAlloc(length) : nullptr;
    if (text) {
        text[0] = 0;
        for (DWORD i = 0; i < count; i++) {
            if (i)
                lstrcatA(text, "; ");
            if (matches[i]->Name[0]) {
                lstrcatA(text, matches[i]->Name);
                lstrcatA(text, "=");
            }
            lstrcatA(text, matches[i]->Value);
        }
    }
    ReleaseSRWLockExclusive(&JarLock);
    return text;
}

char *InetCookiesFor(LPCWSTR host, LPCWSTR path, BOOL secure)
{
    char *narrow_host = InetNarrow(host, -1), *narrow_path = InetNarrow(path, -1);
    char *text = nullptr;
    if (narrow_host && narrow_path) {
        char *query = strchr(narrow_path, '?');
        if (query)
            *query = 0;
        text = Collect(narrow_host, narrow_path, secure, TRUE, nullptr);
    }
    InetFree(narrow_host);
    InetFree(narrow_path);
    return text;
}

/* the host, path and security of a cookie url. */
static BOOL CrackCookieUrl(LPCWSTR url, char **host, char **path, BOOL *secure)
{
    URL_COMPONENTSW parts = {sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = 1;
    *host = *path = nullptr;
    if (!url || !InternetCrackUrlW(url, 0, 0, &parts) || !parts.dwHostNameLength)
        return FALSE;
    *secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    *host = InetNarrow(parts.lpszHostName, (int)parts.dwHostNameLength);
    *path = parts.dwUrlPathLength ? InetNarrow(parts.lpszUrlPath, (int)parts.dwUrlPathLength)
                                  : InetDuplicateA("/", 1);
    if (*host)
        Lower(*host);
    return *host && *path;
}

DECLSPEC_EXPORT BOOL WINAPI InternetGetCookieExW(LPCWSTR url, LPCWSTR name, LPWSTR data, LPDWORD size, DWORD flags,
                                                LPVOID reserved)
{
    if (!url || !size || reserved)
        return InetFail(ERROR_INVALID_PARAMETER);
    DWORD capacity = *size;
    *size = 0;
    char *host, *path, *narrow_name = name ? InetNarrow(name, -1) : nullptr;
    BOOL secure;
    if (!CrackCookieUrl(url, &host, &path, &secure)) {
        InetFree(host);
        InetFree(path);
        InetFree(narrow_name);
        return InetFail(ERROR_INVALID_PARAMETER);
    }
    char *text = Collect(host, path, secure, (flags & INTERNET_COOKIE_HTTPONLY) != 0, narrow_name);
    InetFree(host);
    InetFree(path);
    InetFree(narrow_name);
    if (!text)
        return InetFail(ERROR_NO_MORE_ITEMS);
    PWSTR wide = InetWiden(text, -1);
    InetFree(text);
    if (!wide)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    DWORD chars = (DWORD)lstrlenW(wide) + 1;
    BOOL ok = TRUE;
    if (!data) {
        *size = chars * sizeof(WCHAR);
    } else if (capacity < chars) {
        *size = chars * sizeof(WCHAR);
        ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
    } else {
        memcpy(data, wide, chars * sizeof(WCHAR));
        *size = chars;
    }
    InetFree(wide);
    if (ok)
        SetLastError(ERROR_SUCCESS);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetGetCookieW(LPCWSTR url, LPCWSTR name, LPWSTR data, LPDWORD size)
{
    return InternetGetCookieExW(url, name, data, size, 0, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI InternetGetCookieExA(LPCSTR url, LPCSTR name, LPSTR data, LPDWORD size, DWORD flags,
                                                LPVOID reserved)
{
    if (!url || !size || reserved)
        return InetFail(ERROR_INVALID_PARAMETER);
    DWORD capacity = *size;
    *size = 0;
    PWSTR wide_url = InetWiden(url, -1), wide_name = name ? InetWiden(name, -1) : nullptr;
    DWORD chars = 0;
    BOOL ok = InternetGetCookieExW(wide_url, wide_name, nullptr, &chars, flags, nullptr);
    PWSTR wide = ok ? InetAlloc(chars) : nullptr;
    if (ok && !wide)
        ok = InetFail(ERROR_NOT_ENOUGH_MEMORY);
    chars /= sizeof(WCHAR);
    if (ok)
        ok = InternetGetCookieExW(wide_url, wide_name, wide, &chars, flags, nullptr);
    if (ok) {
        char *narrow = InetNarrow(wide, -1);
        DWORD bytes = narrow ? (DWORD)strlen(narrow) + 1 : 0;
        if (!narrow) {
            ok = InetFail(ERROR_NOT_ENOUGH_MEMORY);
        } else {
            *size = bytes;
            if (data && capacity < bytes)
                ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
            else if (data)
                memcpy(data, narrow, bytes);
        }
        InetFree(narrow);
    }
    InetFree(wide);
    InetFree(wide_url);
    InetFree(wide_name);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetGetCookieA(LPCSTR url, LPCSTR name, LPSTR data, LPDWORD size)
{
    return InternetGetCookieExA(url, name, data, size, 0, nullptr);
}

/* a cookie set by name gets the name in front of its data. */
static DWORD SetCookie(LPCWSTR url, LPCWSTR name, LPCWSTR data, DWORD flags)
{
    if (!url || !*url || !data)
        return ERROR_INVALID_PARAMETER;
    char *host, *path;
    BOOL secure;
    if (!CrackCookieUrl(url, &host, &path, &secure)) {
        InetFree(host);
        InetFree(path);
        return ERROR_INVALID_PARAMETER;
    }
    PWSTR line = InetAlloc(((SIZE_T)(name ? lstrlenW(name) : 0) + lstrlenW(data) + 2) * sizeof(WCHAR));
    char *narrow = nullptr;
    if (line) {
        line[0] = 0;
        if (name && *name) {
            lstrcpyW(line, name);
            lstrcatW(line, L"=");
        }
        lstrcatW(line, data);
        narrow = InetNarrow(line, -1);
    }
    DWORD error = narrow ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    if (narrow && !Store(host, path, narrow, !(flags & INTERNET_COOKIE_HTTPONLY)))
        error = ERROR_INVALID_OPERATION;
    InetFree(narrow);
    InetFree(line);
    InetFree(host);
    InetFree(path);
    return error;
}

DECLSPEC_EXPORT BOOL WINAPI InternetSetCookieW(LPCWSTR url, LPCWSTR name, LPCWSTR data)
{
    DWORD error = SetCookie(url, name, data, 0);
    SetLastError(error);
    return !error;
}

DECLSPEC_EXPORT DWORD WINAPI InternetSetCookieExW(LPCWSTR url, LPCWSTR name, LPCWSTR data, DWORD flags,
                                                 DWORD_PTR reserved)
{
    DWORD error = SetCookie(url, name, data, flags);
    SetLastError(error);
    return error ? COOKIE_STATE_UNKNOWN : COOKIE_STATE_ACCEPT;
}

DECLSPEC_EXPORT DWORD WINAPI InternetSetCookieExA(LPCSTR url, LPCSTR name, LPCSTR data, DWORD flags,
                                                 DWORD_PTR reserved)
{
    PWSTR wide_url = InetWiden(url, -1), wide_name = InetWiden(name, -1), wide_data = InetWiden(data, -1);
    DWORD state = InternetSetCookieExW(wide_url, wide_name, wide_data, flags, reserved);
    InetFree(wide_url);
    InetFree(wide_name);
    InetFree(wide_data);
    return state;
}

DECLSPEC_EXPORT BOOL WINAPI InternetSetCookieA(LPCSTR url, LPCSTR name, LPCSTR data)
{
    return InternetSetCookieExA(url, name, data, 0, 0) == COOKIE_STATE_ACCEPT;
}
