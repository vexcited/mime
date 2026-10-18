/*
 * WinHttpCrackUrl and WinHttpCreateUrl.
 */
#include "winhttp_private.h"

typedef struct {
    LPCWSTR Name;
    DWORD Length;
    INTERNET_SCHEME Scheme;
} WH_SCHEME;

/* the empty scheme cracks too, as scheme 0. */
static const WH_SCHEME Schemes[] = {
    {L"", 0, 0},
    {L"http", 4, INTERNET_SCHEME_HTTP},
    {L"https", 5, INTERNET_SCHEME_HTTPS},
    {L"ftp", 3, INTERNET_SCHEME_FTP},
    {L"gopher", 6, -1},
};

static const WH_SCHEME *FindScheme(LPCWSTR name, DWORD length)
{
    for (DWORD i = 0; i < ARRAYSIZE(Schemes); i++)
        if (Schemes[i].Length == length && !_wcsnicmp(Schemes[i].Name, name, length))
            return &Schemes[i];
    return nullptr;
}

static BOOL Digit(WCHAR c)
{
    return c >= '0' && c <= '9';
}

/* bytes the url escaping of winhttp turns into %XX. */
static BOOL EscapedByte(BYTE c, BOOL gopher)
{
    if (c <= 0x20 || c >= 0x7F)
        return TRUE;
    if (strchr("\"#%<>[\\]^`{|}~", c))
        return TRUE;
    return gopher && (c == '+' || c == '?');
}

/* the utf-8 escaped form of text, a heap string. */
static char *EscapeUtf8(LPCWSTR text, DWORD length, BOOL gopher)
{
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int)length, nullptr, 0, nullptr, nullptr);
    char *utf8 = WhAlloc((SIZE_T)bytes + 1);
    if (!utf8)
        return nullptr;
    WideCharToMultiByte(CP_UTF8, 0, text, (int)length, utf8, bytes, nullptr, nullptr);
    char *escaped = WhAlloc((SIZE_T)bytes * 3 + 1);
    if (escaped) {
        static const char hex[] = "0123456789ABCDEF";
        char *out = escaped;
        for (int i = 0; i < bytes; i++) {
            BYTE c = (BYTE)utf8[i];
            if (!c)
                break;
            if (EscapedByte(c, gopher)) {
                *out++ = '%';
                *out++ = hex[c >> 4];
                *out++ = hex[c & 15];
            } else {
                *out++ = (char)c;
            }
        }
        *out = 0;
    }
    WhFree(utf8);
    return escaped;
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
        return (c | 0x20) - 'a' + 10;
    return -1;
}

/* copies one component as winhttp does: a pointer into the url, or a copy, decoded and escaped on request. */
static DWORD CopyComponent(LPCWSTR source, DWORD length, LPWSTR *buffer, DWORD *size, BOOL decode, BOOL escape,
                           BOOL *short_buffer)
{
    if (!*buffer) {
        if (*size) {
            *buffer = (LPWSTR)source;
            *size = length;
        }
        return ERROR_SUCCESS;
    }
    if (*size < length + 1 || !source || !*source) {
        (*buffer)[0] = 0;
        if (length) {
            *size = length * (escape ? 18 : 1) + 1;
            *short_buffer = TRUE;
        } else {
            *size = 0;
        }
        return ERROR_SUCCESS;
    }
    memcpy(*buffer, source, length * sizeof(WCHAR));
    (*buffer)[length] = 0;
    if (!decode && !escape) {
        *size = (DWORD)wcslen(*buffer);
        return ERROR_SUCCESS;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, *buffer, (int)length, nullptr, 0, nullptr, nullptr);
    char *utf8 = WhAlloc((SIZE_T)bytes + 1);
    if (!utf8)
        return ERROR_NOT_ENOUGH_MEMORY;
    WideCharToMultiByte(CP_UTF8, 0, *buffer, (int)length, utf8, bytes, nullptr, nullptr);
    utf8[bytes] = 0;
    if (decode) {
        if (strstr(utf8, "%00")) {
            WhFree(utf8);
            return ERROR_WINHTTP_INVALID_URL;
        }
        char *out = utf8;
        for (char *p = utf8; *p; p++) {
            int high, low;
            if (*p == '%' && (high = HexValue(p[1])) >= 0 && (low = HexValue(p[2])) >= 0) {
                *out++ = (char)(high << 4 | low);
                p += 2;
            } else {
                *out++ = *p;
            }
        }
        *out = 0;
    }
    char *text = utf8;
    if (escape) {
        DWORD count = (DWORD)strlen(utf8);
        text = WhAlloc((SIZE_T)count * 3 + 1);
        if (!text) {
            WhFree(utf8);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        static const char hex[] = "0123456789ABCDEF";
        char *out = text;
        for (DWORD i = 0; i < count; i++) {
            BYTE c = (BYTE)utf8[i];
            if (EscapedByte(c, FALSE)) {
                *out++ = '%';
                *out++ = hex[c >> 4];
                *out++ = hex[c & 15];
            } else {
                *out++ = (char)c;
            }
        }
        *out = 0;
    }
    int count = (int)strlen(text);
    int needed = MultiByteToWideChar(CP_UTF8, 0, text, count, nullptr, 0) + 1;
    DWORD error = ERROR_SUCCESS;
    if ((DWORD)needed > *size) {
        (*buffer)[0] = 0;
        *size = (DWORD)needed;
        *short_buffer = TRUE;
    } else {
        int written = MultiByteToWideChar(CP_UTF8, 0, text, count, *buffer, (int)*size - 1);
        if (!written && count)
            error = ERROR_INVALID_PARAMETER;
        else {
            (*buffer)[written] = 0;
            *size = (DWORD)written;
        }
    }
    if (text != utf8)
        WhFree(text);
    WhFree(utf8);
    return error;
}

BOOL WhCrack(LPCWSTR url, DWORD length, DWORD flags, URL_COMPONENTS *uc)
{
    if (!url || !uc || uc->dwStructSize != sizeof(URL_COMPONENTS) || flags & ~(ICU_ESCAPE | ICU_DECODE | ICU_REJECT_USERPWD))
        return WhFail(ERROR_INVALID_PARAMETER);
    struct {
        LPWSTR *Buffer;
        DWORD *Size;
    } parts[] = {
        {&uc->lpszScheme, &uc->dwSchemeLength},     {&uc->lpszHostName, &uc->dwHostNameLength},
        {&uc->lpszUserName, &uc->dwUserNameLength}, {&uc->lpszPassword, &uc->dwPasswordLength},
        {&uc->lpszUrlPath, &uc->dwUrlPathLength},   {&uc->lpszExtraInfo, &uc->dwExtraInfoLength},
    };
    for (DWORD i = 0; i < ARRAYSIZE(parts); i++) {
        if (*parts[i].Buffer ? !*parts[i].Size : *parts[i].Size && flags & (ICU_ESCAPE | ICU_DECODE))
            return WhFail(ERROR_INVALID_PARAMETER);
    }
    if (!length)
        length = (DWORD)wcslen(url);
    LPCWSTR end = url + length;
    LPCWSTR colon = url;
    while (colon < end && *colon && *colon != ':')
        colon++;
    if (colon >= end || !*colon)
        return WhFail(ERROR_WINHTTP_UNRECOGNIZED_SCHEME);
    const WH_SCHEME *scheme = FindScheme(url, (DWORD)(colon - url));
    if (!scheme)
        return WhFail(ERROR_WINHTTP_UNRECOGNIZED_SCHEME);
    if (colon + 3 > end || colon[1] != '/' || colon[2] != '/')
        return WhFail(ERROR_WINHTTP_INVALID_URL);

    /* the authority: [user[:password]@]host[:port]. */
    LPCWSTR authority = colon + 3;
    LPCWSTR text_end = authority;
    while (text_end < end && *text_end)
        text_end++;
    LPCWSTR authority_end = authority;
    while (authority_end < text_end && *authority_end != '/' && *authority_end != '?' && *authority_end != '#')
        authority_end++;
    LPCWSTR at = authority;
    while (at < authority_end && *at != '@')
        at++;
    LPCWSTR user = authority_end, password = authority_end, host = authority;
    DWORD user_length = 0, password_length = 0;
    if (at < authority_end) {
        if (flags & ICU_REJECT_USERPWD)
            return WhFail(ERROR_WINHTTP_INVALID_URL);
        LPCWSTR split = authority;
        while (split < at && *split != ':')
            split++;
        user = authority;
        user_length = (DWORD)(split - authority);
        password = split < at ? split + 1 : split;
        password_length = (DWORD)(at - password);
        host = at + 1;
    }
    LPCWSTR p = host;
    DWORD left = (DWORD)(text_end - host), host_length = 0;
    if (left && *p == '[') {
        while (TRUE) {
            if (*p > 0x7F)
                return WhFail(ERROR_WINHTTP_INVALID_URL);
            host_length++;
            p++;
            if (!--left)
                return WhFail(ERROR_WINHTTP_INVALID_URL);
            if (*p == ']') {
                host_length++;
                p++;
                left--;
                if (left && *p != ':' && *p != '/')
                    return WhFail(ERROR_WINHTTP_INVALID_URL);
                break;
            }
        }
    }
    LPCWSTR port = nullptr;
    DWORD port_length = 0;
    BOOL host_empty = FALSE;
    while (left && *p && *p != '#' && *p != '/' && *p != '?') {
        if (*p == ':') {
            if (port)
                return WhFail(ERROR_WINHTTP_INVALID_URL);
            port = p + 1;
            if (!host_length)
                host_empty = TRUE;
        } else if (port) {
            port_length++;
        } else {
            host_length++;
        }
        p++;
        left--;
    }
    if (host_empty || !host_length)
        return WhFail(ERROR_WINHTTP_INVALID_URL);
    BOOL explicit_port = FALSE;
    DWORD port_value = 0;
    if (port && port_length) {
        for (DWORD i = 0; i < port_length; i++) {
            if (!Digit(port[i]))
                return WhFail(ERROR_WINHTTP_INVALID_URL);
            port_value = port_value * 10 + (port[i] - '0');
            if (port_value > 0xFFFF)
                return WhFail(ERROR_WINHTTP_INVALID_URL);
        }
        explicit_port = TRUE;
    }

    /* the path keeps the query unless the caller asked for the extra info. */
    LPCWSTR path = p;
    DWORD path_length = (DWORD)(text_end - p);
    LPCWSTR extra = nullptr;
    DWORD extra_length = 0;
    if (uc->dwExtraInfoLength) {
        LPCWSTR q = path;
        while (q < path + path_length && *q != '#' && *q != '?')
            q++;
        extra = q;
        extra_length = (DWORD)(path + path_length - q);
        path_length -= extra_length;
    }

    BOOL short_buffer = FALSE, decode = (flags & ICU_DECODE) != 0, escape = (flags & ICU_ESCAPE) != 0;
    DWORD error = CopyComponent(url, (DWORD)(colon - url), &uc->lpszScheme, &uc->dwSchemeLength, FALSE, FALSE,
                                &short_buffer);
    if (!error)
        error = CopyComponent(host, host_length, &uc->lpszHostName, &uc->dwHostNameLength, decode, FALSE,
                              &short_buffer);
    BOOL user_by_pointer = uc->dwUserNameLength && !uc->lpszUserName;
    if (!error)
        error = CopyComponent(user, user_length, &uc->lpszUserName, &uc->dwUserNameLength, decode, FALSE,
                              &short_buffer);
    if (!error && user_by_pointer && !uc->dwUserNameLength)
        uc->lpszUserName = nullptr;
    BOOL password_by_pointer = uc->dwPasswordLength && !uc->lpszPassword;
    if (!error)
        error = CopyComponent(password, password_length, &uc->lpszPassword, &uc->dwPasswordLength, decode, FALSE,
                              &short_buffer);
    if (!error && password_by_pointer && !uc->dwPasswordLength)
        uc->lpszPassword = nullptr;
    if (!error)
        error = CopyComponent(path, path_length, &uc->lpszUrlPath, &uc->dwUrlPathLength, decode, escape,
                              &short_buffer);
    if (!error)
        error = CopyComponent(extra, extra_length, &uc->lpszExtraInfo, &uc->dwExtraInfoLength, decode, escape,
                              &short_buffer);
    if (error)
        return WhFail(error);
    uc->nScheme = scheme->Scheme;
    if (!explicit_port)
        port_value = scheme->Scheme == INTERNET_SCHEME_HTTP    ? INTERNET_DEFAULT_HTTP_PORT
                     : scheme->Scheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT
                                                               : 0;
    uc->nPort = (INTERNET_PORT)port_value;
    if (short_buffer)
        return WhFail(ERROR_INSUFFICIENT_BUFFER);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI WinHttpCrackUrl(LPCWSTR url, DWORD length, DWORD flags, LPURL_COMPONENTS components)
{
    return WhCrack(url, length, flags, components);
}

/* escapes a host, user or password with ICU_ESCAPE_AUTHORITY into a buffer of capacity characters. */
static DWORD EscapeAuthority(LPCWSTR text, DWORD length, PWSTR out, DWORD capacity, DWORD *written)
{
    static const char hex[] = "0123456789ABCDEF";
    DWORD o = 0;
    for (DWORD i = 0; i < length && text[i]; i++) {
        WCHAR c = text[i];
        if (c <= 0xFF && (c <= 0x20 || c >= 0x7F || strchr("\"#%/<>?@\\^`{|}", (char)c))) {
            if (capacity - o < 3)
                return ERROR_INSUFFICIENT_BUFFER;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        } else {
            if (capacity - o < 1)
                return ERROR_INSUFFICIENT_BUFFER;
            out[o++] = c;
        }
    }
    if (o >= capacity)
        return ERROR_INSUFFICIENT_BUFFER;
    out[o] = 0;
    *written = o;
    return ERROR_SUCCESS;
}

static DWORD CreateUrl(LPURL_COMPONENTS uc, DWORD flags, LPWSTR url, LPDWORD url_length)
{
    if (!url_length || !uc || uc->dwStructSize != sizeof(URL_COMPONENTS) ||
        flags & ~(ICU_ESCAPE | ICU_ESCAPE_AUTHORITY | ICU_REJECT_USERPWD | 0x8000))
        return ERROR_INVALID_PARAMETER;
    LPCWSTR scheme = uc->lpszScheme;
    DWORD scheme_length = uc->dwSchemeLength;
    INTERNET_SCHEME number = uc->nScheme;
    if (scheme) {
        if (!scheme_length)
            scheme_length = (DWORD)wcslen(scheme);
        const WH_SCHEME *known = FindScheme(scheme, scheme_length);
        number = known ? known->Scheme : -1;
    } else if (number) {
        if ((DWORD)(number - 1) <= 3) {
            scheme = Schemes[number].Name;
            scheme_length = Schemes[number].Length;
        } else {
            scheme_length = 0;
        }
    } else {
        scheme = L"http";
        scheme_length = 4;
        number = INTERNET_SCHEME_HTTP;
    }
    /* the escaping class: http and https 4, ftp 1, gopher 2, compared on the given length. */
    DWORD class = 0;
    if (scheme_length) {
        if (!_wcsnicmp(scheme, L"http", scheme_length) || !_wcsnicmp(scheme, L"https", scheme_length))
            class = 4;
        else if (!_wcsnicmp(scheme, L"ftp", scheme_length))
            class = 1;
        else if (!_wcsnicmp(scheme, L"gopher", scheme_length))
            class = 2;
    }

    WCHAR host_buffer[257], user_buffer[129], password_buffer[129];
    LPCWSTR host = uc->lpszHostName;
    DWORD host_length = uc->dwHostNameLength;
    BOOL brackets = FALSE;
    WCHAR port_text[12];
    DWORD port_length = 0;
    if (host) {
        if (!host_length)
            host_length = (DWORD)wcslen(host);
        if (flags & ICU_ESCAPE_AUTHORITY) {
            DWORD error = EscapeAuthority(host, host_length, host_buffer, 257, &host_length);
            if (error)
                return error;
            host = host_buffer;
        }
        DWORD colons = 0;
        for (DWORD i = 0; i < host_length; i++)
            if (host[i] == ':' && ++colons > 1) {
                brackets = !(host[0] == '[' && host[host_length - 1] == ']');
                break;
            }
        INTERNET_PORT default_port = number == INTERNET_SCHEME_HTTP    ? 80
                                     : number == INTERNET_SCHEME_HTTPS ? 443
                                                                       : 0;
        if (uc->nPort != default_port || flags & 0x8000)
            port_length = (DWORD)wsprintfW(port_text, L":%u", uc->nPort);
    }
    LPCWSTR user = uc->lpszUserName;
    DWORD user_length = 0;
    if (user) {
        user_length = uc->dwUserNameLength ? uc->dwUserNameLength : (DWORD)wcslen(user);
        if (flags & ICU_ESCAPE_AUTHORITY) {
            DWORD error = EscapeAuthority(user, user_length, user_buffer, 129, &user_length);
            if (error)
                return error;
            user = user_buffer;
        }
    }
    LPCWSTR password = uc->lpszPassword;
    DWORD password_length = 0;
    if (password) {
        password_length = uc->dwPasswordLength ? uc->dwPasswordLength : (DWORD)wcslen(password);
        if (flags & ICU_ESCAPE_AUTHORITY) {
            DWORD error = EscapeAuthority(password, password_length, password_buffer, 129, &password_length);
            if (error)
                return error;
            password = password_buffer;
        }
        if (!user)
            return ERROR_INVALID_PARAMETER;
    }
    if (flags & ICU_REJECT_USERPWD && class & 4)
        user = password = nullptr;

    LPCWSTR path = uc->lpszUrlPath;
    DWORD path_length = 0;
    BOOL slash = FALSE;
    char *path_escaped = nullptr, *extra_escaped = nullptr;
    PWSTR path_wide = nullptr, extra_wide = nullptr;
    if (path) {
        path_length = uc->dwUrlPathLength ? uc->dwUrlPathLength : (DWORD)wcslen(path);
        if (path[0] != '/' && path[0] != '\\' && (user_length || host_length || port_length))
            slash = TRUE;
        if (flags & ICU_ESCAPE && class) {
            path_escaped = EscapeUtf8(path, path_length, class == 2);
            path_wide = path_escaped ? WhAnsiToWide(path_escaped, (DWORD)strlen(path_escaped)) : nullptr;
            if (!path_wide) {
                WhFree(path_escaped);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            path = path_wide;
            path_length = (DWORD)wcslen(path_wide);
        }
    }
    LPCWSTR extra = uc->lpszExtraInfo;
    DWORD extra_length = 0;
    if (extra) {
        extra_length = uc->dwExtraInfoLength ? uc->dwExtraInfoLength : (DWORD)wcslen(extra);
        if (flags & ICU_ESCAPE && class) {
            extra_escaped = EscapeUtf8(extra, extra_length, class == 2);
            extra_wide = extra_escaped ? WhAnsiToWide(extra_escaped, (DWORD)strlen(extra_escaped)) : nullptr;
            if (!extra_wide) {
                WhFree(path_escaped);
                WhFree(path_wide);
                WhFree(extra_escaped);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            extra = extra_wide;
            extra_length = (DWORD)wcslen(extra_wide);
        }
    }

    /* the known schemes take "://", the empty one ":", others "://" only with a host. */
    const WH_SCHEME *known = FindScheme(scheme ? scheme : L"", scheme_length);
    BOOL authority = known ? known->Length != 0 : host != nullptr;
    LPCWSTR separator = authority ? L"://" : L":";
    DWORD separator_length = authority ? 3 : 1;
    DWORD needed = scheme_length + separator_length + (user ? user_length + 1 : 0) +
                   (user && password ? password_length + 1 : 0) + (host ? host_length + port_length : 0) +
                   (brackets ? 2 : 0) + (slash ? 1 : 0) + path_length + extra_length + 1;
    DWORD error = ERROR_SUCCESS;
    if (*url_length < needed) {
        *url_length = needed;
        error = ERROR_INSUFFICIENT_BUFFER;
    } else if (!url) {
        error = ERROR_INVALID_PARAMETER;
    } else {
        PWSTR out = url;
        memcpy(out, scheme, scheme_length * sizeof(WCHAR));
        out += scheme_length;
        memcpy(out, separator, separator_length * sizeof(WCHAR));
        out += separator_length;
        if (user) {
            memcpy(out, user, user_length * sizeof(WCHAR));
            out += user_length;
            if (password) {
                *out++ = ':';
                memcpy(out, password, password_length * sizeof(WCHAR));
                out += password_length;
            }
            *out++ = '@';
        }
        if (host) {
            if (brackets)
                *out++ = '[';
            memcpy(out, host, host_length * sizeof(WCHAR));
            out += host_length;
            if (brackets)
                *out++ = ']';
            memcpy(out, port_text, port_length * sizeof(WCHAR));
            out += port_length;
        }
        if (slash)
            *out++ = '/';
        if (path_length)
            memcpy(out, path, path_length * sizeof(WCHAR));
        out += path_length;
        if (extra_length)
            memcpy(out, extra, extra_length * sizeof(WCHAR));
        out += extra_length;
        *out = 0;
        *url_length = (DWORD)(out - url);
    }
    WhFree(path_escaped);
    WhFree(path_wide);
    WhFree(extra_escaped);
    WhFree(extra_wide);
    return error;
}

BOOL WINAPI WinHttpCreateUrl(LPURL_COMPONENTS components, DWORD flags, LPWSTR url, LPDWORD length)
{
    DWORD error = CreateUrl(components, flags, url, length);
    SetLastError(error);
    return !error;
}
