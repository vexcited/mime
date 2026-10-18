/*
 * url parsing for the protocols urlmon knows: CoInternetParseUrl over shlwapi's url functions,
 * CoInternetCombineUrl through IUri, CoInternetCompareUrl over UrlCompare.
 */
#include "urlmon_private.h"

/* copies a result out; a buffer too small gets the length needed with the terminator. */
static HRESULT ReturnString(LPCWSTR text, LPWSTR result, DWORD size, DWORD *length, HRESULT too_small)
{
    DWORD chars = (DWORD)lstrlenW(text);
    if (chars >= size) {
        *length = chars + 1;
        return too_small;
    }
    memcpy(result, text, (chars + 1) * sizeof(WCHAR));
    *length = chars;
    return S_OK;
}

static BOOL HasServer(LPCWSTR url)
{
    static const LPCWSTR schemes[] = {L"http:", L"https:", L"ftp:", L"news:", L"nntp:", L"telnet:", L"snews:"};
    for (DWORD i = 0; i < ARRAYSIZE(schemes); i++)
        if (!_wcsnicmp(url, schemes[i], lstrlenW(schemes[i])))
            return TRUE;
    return FALSE;
}

DECLSPEC_EXPORT HRESULT WINAPI CoInternetParseUrl(LPCWSTR url, PARSEACTION action, DWORD flags, LPWSTR result,
                                                 DWORD size, DWORD *length, DWORD reserved)
{
    if (!url || !result || !length)
        return E_INVALIDARG;
    switch (action) {
    case PARSE_CANONICALIZE:
        *length = size;
        return UrlCanonicalizeW(url, result, length, flags);
    case PARSE_ENCODE_IS_UNESCAPE:
    case PARSE_UNESCAPE:
        *length = size;
        return UrlUnescapeW((LPWSTR)url, result, length, flags);
    case PARSE_DECODE_IS_ESCAPE:
    case PARSE_ESCAPE:
        *length = size;
        return UrlEscapeW(url, result, length, flags);
    case PARSE_PATH_FROM_URL:
        *length = size;
        return PathCreateFromUrlW(url, result, length, flags);
    case PARSE_URL_FROM_PATH:
        *length = size;
        return UrlCreateFromPathW(url, result, length, flags);
    case PARSE_SCHEMA:
        *length = size;
        return UrlGetPartW(url, result, length, URL_PART_SCHEME, 0);
    case PARSE_DOMAIN:
        *length = size;
        return UrlGetPartW(url, result, length, URL_PART_HOSTNAME, 0);
    case PARSE_LOCATION: {
        *length = 0;
        LPCWSTR location = UrlGetLocationW(url);
        if (!location || (DWORD)lstrlenW(location) >= size)
            return E_FAIL;
        lstrcpyW(result, location);
        *length = (DWORD)lstrlenW(location);
        return S_OK;
    }
    case PARSE_ROOTDOCUMENT: {
        if (!HasServer(url))
            return E_FAIL;
        WCHAR scheme[32], host[INTERNET_MAX_HOST_NAME_LENGTH];
        DWORD scheme_length = ARRAYSIZE(scheme), host_length = ARRAYSIZE(host);
        if (FAILED(UrlGetPartW(url, scheme, &scheme_length, URL_PART_SCHEME, 0)) ||
            FAILED(UrlGetPartW(url, host, &host_length, URL_PART_HOSTNAME, 0)))
            return E_FAIL;
        WCHAR root[ARRAYSIZE(scheme) + ARRAYSIZE(host) + 4];
        wsprintfW(root, L"%s://%s", scheme, host);
        return ReturnString(root, result, size, length, E_OUTOFMEMORY);
    }
    case PARSE_DOCUMENT:
    case PARSE_SECURITY_URL: {
        /* a document of a known protocol is its url without the fragment. */
        if (!HasServer(url) && _wcsnicmp(url, L"file:", 5))
            return E_FAIL;
        PWSTR copy = UrlDuplicate(url);
        if (!copy)
            return E_OUTOFMEMORY;
        PWSTR hash = wcschr(copy, '#');
        if (hash)
            *hash = 0;
        HRESULT hr = ReturnString(copy, result, size, length, E_POINTER);
        UrlFree(copy);
        return hr;
    }
    default:
        return E_FAIL;
    }
}

DECLSPEC_EXPORT HRESULT WINAPI CoInternetCombineUrl(LPCWSTR base, LPCWSTR relative, DWORD flags, LPWSTR result,
                                                   DWORD size, DWORD *length, DWORD reserved)
{
    if (length)
        *length = 0;
    IUri *base_uri = nullptr, *relative_uri = nullptr, *combined = nullptr;
    HRESULT hr = CreateUri(base, Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME, 0, &base_uri);
    if (SUCCEEDED(hr))
        hr = relative ? CreateUri(relative, Uri_CREATE_ALLOW_RELATIVE, 0, &relative_uri) : E_UNEXPECTED;
    if (SUCCEEDED(hr))
        hr = CoInternetCombineIUri(base_uri, relative_uri, flags, &combined, 0);
    BSTR text = nullptr;
    if (SUCCEEDED(hr)) {
        hr = combined->lpVtbl->GetPropertyBSTR(combined, Uri_PROPERTY_ABSOLUTE_URI, &text, 0);
        if (hr == S_FALSE)
            hr = INET_E_INVALID_URL;
    }
    if (SUCCEEDED(hr)) {
        DWORD chars = SysStringLen(text);
        if (size <= chars) {
            if (length)
                *length = chars + 1;
            hr = E_POINTER;
        } else if (result) {
            memcpy(result, text, (chars + 1) * sizeof(WCHAR));
            if (length)
                *length = chars;
            hr = S_OK;
        } else {
            hr = E_INVALIDARG;
        }
    }
    SysFreeString(text);
    if (combined)
        combined->lpVtbl->Release(combined);
    if (relative_uri)
        relative_uri->lpVtbl->Release(relative_uri);
    if (base_uri)
        base_uri->lpVtbl->Release(base_uri);
    return hr;
}

/* S_OK when the urls are the same, S_FALSE otherwise; bit 0 of the flags ignores a final slash. */
DECLSPEC_EXPORT HRESULT WINAPI CoInternetCompareUrl(LPCWSTR url1, LPCWSTR url2, DWORD flags)
{
    return UrlCompareW(url1, url2, flags & 1) ? S_FALSE : S_OK;
}
