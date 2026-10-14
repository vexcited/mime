#include "shlwapi_private.h"

static BOOL scheme_char(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.';
}

HRESULT WINAPI UrlGetPartW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD part, DWORD flags)
{
    if (!url || !out || !size || part < URL_PART_SCHEME || part > URL_PART_QUERY ||
        (flags & ~URL_PARTFLAG_KEEPSCHEME))
        return E_INVALIDARG;
    PCWSTR colon = url;
    while (scheme_char(*colon))
        colon++;
    if (colon == url || *colon != L':')
        return E_FAIL;
    PCWSTR start = colon + 1, end = start;
    if (part == URL_PART_SCHEME) {
        start = url;
        end = colon;
    } else if (part == URL_PART_QUERY) {
        PCWSTR query = wcschr(colon + 1, L'?');
        PCWSTR fragment = wcschr(colon + 1, L'#');
        if (query && (!fragment || query < fragment)) {
            start = query;
            end = fragment ? fragment : query + wcslen(query);
        }
    } else if (start[0] == L'/' && start[1] == L'/') {
        PCWSTR authority = start + 2;
        PCWSTR limit = authority;
        while (*limit && *limit != L'/' && *limit != L'?' && *limit != L'#')
            limit++;
        PCWSTR at = NULL, password = NULL;
        for (PCWSTR p = authority; p < limit; p++)
            if (*p == L'@')
                at = p;
        if (at) {
            for (PCWSTR p = authority; p < at && !password; p++)
                if (*p == L':')
                    password = p;
        }
        PCWSTR host = at ? at + 1 : authority;
        PCWSTR host_end = host;
        if (host < limit && *host == L'[') {
            while (host_end < limit && *host_end != L']')
                host_end++;
            if (host_end < limit)
                host_end++;
        } else {
            while (host_end < limit && *host_end != L':')
                host_end++;
        }
        switch (part) {
        case URL_PART_HOSTNAME:
            start = host;
            end = host_end;
            break;
        case URL_PART_USERNAME:
            if (at) {
                start = authority;
                end = password ? password : at;
            }
            break;
        case URL_PART_PASSWORD:
            if (password) {
                start = password + 1;
                end = at;
            }
            break;
        case URL_PART_PORT:
            if (host_end < limit && *host_end == L':') {
                start = host_end + 1;
                end = limit;
            }
            break;
        }
    }
    SIZE_T count = end - start;
    SIZE_T prefix = count && part != URL_PART_SCHEME && (flags & URL_PARTFLAG_KEEPSCHEME) ? colon - url + 1 : 0;
    if (*size <= count + prefix) {
        *size = (DWORD)(count + prefix + 1);
        return E_POINTER;
    }
    if (prefix)
        memcpy(out, url, prefix * sizeof(WCHAR));
    memcpy(out + prefix, start, count * sizeof(WCHAR));
    out[count + prefix] = 0;
    for (SIZE_T i = 0; i < (part == URL_PART_SCHEME ? count : prefix); i++)
        if (out[i] >= L'A' && out[i] <= L'Z')
            out[i] += L'a' - L'A';
    *size = (DWORD)(count + prefix);
    return count ? S_OK : S_FALSE;
}

static int hex_digit(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

static DWORD unescape(PCWSTR url, PWSTR out, DWORD flags)
{
    DWORD count = 0;
    BOOL extra = FALSE;
    while (*url) {
        WCHAR c = *url++;
        if ((flags & URL_DONT_UNESCAPE_EXTRA_INFO) && (c == L'?' || c == L'#'))
            extra = TRUE;
        if (!extra && c == L'%' && url[0] && url[1]) {
            int high = hex_digit(url[0]), low = hex_digit(url[1]);
            if (high >= 0 && low >= 0) {
                c = (WCHAR)((high << 4) | low);
                url += 2;
            }
        }
        if (out)
            out[count] = c;
        count++;
    }
    if (out)
        out[count] = 0;
    return count;
}

HRESULT WINAPI UrlUnescapeW(LPWSTR url, LPWSTR out, DWORD *size, DWORD flags)
{
    if (!url || (flags & ~(URL_DONT_UNESCAPE_EXTRA_INFO | URL_UNESCAPE_INPLACE | URL_UNESCAPE_AS_UTF8)))
        return E_INVALIDARG;
    if (flags & URL_UNESCAPE_AS_UTF8)
        return E_NOTIMPL;
    if (flags & URL_UNESCAPE_INPLACE) {
        unescape(url, url, flags);
        return S_OK;
    }
    if (!out || !size)
        return E_INVALIDARG;
    DWORD count = unescape(url, NULL, flags);
    if (*size <= count) {
        *size = count + 1;
        return E_POINTER;
    }
    unescape(url, out, flags);
    *size = count;
    return S_OK;
}
