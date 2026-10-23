/*
 * the PathCch functions, the bounded path core every legacy shlwapi path function is built on. Only the backslash
 * separates components unless PATHCCH_CANONICALIZE_SLASHES asks for both.
 */
#include "shlwapi_kb.h"

#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#define E_FILENAME_EXCED_RANGE ((HRESULT)0x800700CEL)

static BOOL prefix_nocase(LPCWSTR s, LPCWSTR prefix)
{
    for (; *prefix; s++, prefix++)
        if (towlower(*s) != towlower(*prefix))
            return FALSE;
    return TRUE;
}

static BOOL is_extended(LPCWSTR path)
{
    return path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\';
}

static BOOL is_guid(LPCWSTR s)
{
    static const char pattern[] = "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}";
    for (int i = 0; pattern[i]; i++) {
        WCHAR c = s[i];
        if (pattern[i] == 'x') {
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')))
                return FALSE;
        } else if (c != (WCHAR)pattern[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* "\\?\Volume{guid}", 48 characters. */
static BOOL is_volume_path(LPCWSTR path)
{
    return is_extended(path) && prefix_nocase(path + 4, L"Volume") && is_guid(path + 10);
}

/* characters before the server: 2, or 8 for "\\?\UNC\", or 0 when the path is not unc. */
static SIZE_T unc_prefix(LPCWSTR path)
{
    if (path[0] != L'\\' || path[1] != L'\\')
        return 0;
    if (path[2] == L'?')
        return prefix_nocase(path + 3, L"\\UNC\\") ? 8 : 0;
    return 2;
}

/* after "server\share\", or the end when the share or its separator is missing. */
static LPCWSTR skip_server_share(LPCWSTR server)
{
    LPCWSTR first = wcschr(server, L'\\');
    if (!first)
        return server + wcslen(server);
    LPCWSTR second = wcschr(first + 1, L'\\');
    if (!second)
        return first + wcslen(first);
    return second == first + 1 ? second : second + 1;
}

LPCWSTR KbShSkipRoot(LPCWSTR path)
{
    SIZE_T unc = unc_prefix(path);
    if (unc)
        return skip_server_share(path + unc);
    if (path[0] == L'\\' && path[1] != L'\\')
        return path + 1;
    if (is_volume_path(path))
        return path + (path[48] == L'\\' ? 49 : 48);
    LPCWSTR drive = is_extended(path) ? path + 4 : path;
    if (KbIsDriveLetter(drive[0]) && drive[1] == L':')
        return drive + (drive[2] == L'\\' ? 3 : 2);
    return nullptr;
}

HRESULT KbShCopy(LPWSTR out, SIZE_T size, LPCWSTR in)
{
    if (!size || size > 0x7FFFFFFF)
        return E_INVALIDARG;
    SIZE_T n = 0;
    while (in[n] && n < size - 1) {
        out[n] = in[n];
        n++;
    }
    out[n] = 0;
    return in[n] ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}

static BOOL valid_size(SIZE_T size)
{
    return size - 1 <= PATHCCH_MAX_CCH - 1;
}

/* ------------------------------------------------------------------ roots */

LWSTDAPI HRESULT WINAPI PathCchSkipRoot(LPCWSTR path, LPCWSTR *root_end)
{
    if (!path || !*path || !root_end)
        return E_INVALIDARG;
    *root_end = KbShSkipRoot(path);
    return *root_end ? S_OK : E_INVALIDARG;
}

LWSTDAPI BOOL WINAPI PathCchIsRoot(LPCWSTR path)
{
    if (!path || !*path)
        return FALSE;
    if (KbIsDriveLetter(path[0]) && path[1] == L':' && path[2] == L'\\' && !path[3])
        return TRUE;
    if (path[0] == L'\\' && !path[1])
        return TRUE;
    SIZE_T unc = unc_prefix(path);
    if (unc) {
        int separators = 0;
        for (LPCWSTR p = path + unc; *p; p++) {
            if (*p != L'\\')
                continue;
            if (++separators > 1 || !p[1])
                return FALSE;
        }
        return TRUE;
    }
    if (is_extended(path) && KbIsDriveLetter(path[4]) && path[5] == L':' && path[6] == L'\\' && !path[7])
        return TRUE;
    return is_volume_path(path) && path[48] == L'\\' && !path[49];
}

/* drops a final backslash that does not belong to the root. */
static BOOL trim_backslash(LPWSTR path, SIZE_T size)
{
    SIZE_T length = wcslen(path);
    if (length < size && length && path[length - 1] == L'\\' && !PathCchIsRoot(path)) {
        path[length - 1] = 0;
        return TRUE;
    }
    return FALSE;
}

LWSTDAPI HRESULT WINAPI PathCchStripToRoot(LPWSTR path, SIZE_T size)
{
    if (!path || !valid_size(size))
        return E_INVALIDARG;
    LPWSTR end = *path ? (LPWSTR)KbShSkipRoot(path) : nullptr;
    if (!end || end >= path + size) {
        KbShCopy(path, size, L"");
        return E_INVALIDARG;
    }
    if (*end) {
        *end = 0;
        trim_backslash(path, size);
        return S_OK;
    }
    if (wcslen(path) >= size) {
        KbShCopy(path, size, L"");
        return E_INVALIDARG;
    }
    return trim_backslash(path, size) ? S_OK : S_FALSE;
}

LWSTDAPI HRESULT WINAPI PathCchRemoveFileSpec(LPWSTR path, SIZE_T size)
{
    if (!path || !valid_size(size))
        return E_INVALIDARG;
    LPWSTR cut = *path ? (LPWSTR)KbShSkipRoot(path) : nullptr;
    if (!cut)
        cut = path;
    if (cut >= path + size)
        return E_INVALIDARG;
    for (LPWSTR p = wcschr(cut, L'\\'); p; p = wcschr(p + 1, L'\\')) {
        if (p >= path + size)
            return E_INVALIDARG;
        cut = p;
    }
    if (*cut) {
        *cut = 0;
        trim_backslash(path, size);
        return S_OK;
    }
    if (wcslen(path) >= size)
        return E_INVALIDARG;
    return trim_backslash(path, size) ? S_OK : S_FALSE;
}

LWSTDAPI BOOL WINAPI PathIsUNCEx(LPCWSTR path, LPCWSTR *server)
{
    if (server)
        *server = nullptr;
    SIZE_T unc = unc_prefix(path);
    if (!unc)
        return FALSE;
    if (server)
        *server = path + unc;
    return TRUE;
}

/* ------------------------------------------------------------------ backslashes and extensions */

LWSTDAPI HRESULT WINAPI PathCchAddBackslashEx(LPWSTR path, SIZE_T size, LPWSTR *end, SIZE_T *remaining)
{
    if (end)
        *end = nullptr;
    if (remaining)
        *remaining = 0;
    SIZE_T length = wcslen(path);
    if (length >= size)
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    HRESULT hr = S_FALSE;
    if (length && path[length - 1] != L'\\') {
        if (size - length < 2)
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        path[length++] = L'\\';
        path[length] = 0;
        hr = S_OK;
    }
    if (end)
        *end = path + length;
    if (remaining)
        *remaining = size - length;
    return hr;
}

LWSTDAPI HRESULT WINAPI PathCchAddBackslash(LPWSTR path, SIZE_T size)
{
    return PathCchAddBackslashEx(path, size, nullptr, nullptr);
}

LWSTDAPI HRESULT WINAPI PathCchRemoveBackslashEx(LPWSTR path, SIZE_T size, LPWSTR *end, SIZE_T *remaining)
{
    if (end)
        *end = nullptr;
    if (remaining)
        *remaining = 0;
    SIZE_T length = wcslen(path);
    if (length >= size)
        return E_INVALIDARG;
    HRESULT hr = S_FALSE;
    if (length && path[length - 1] == L'\\') {
        length--;
        if (!PathCchIsRoot(path)) {
            path[length] = 0;
            hr = S_OK;
        }
    }
    if (end)
        *end = path + length;
    if (remaining)
        *remaining = size - length;
    return hr;
}

LWSTDAPI HRESULT WINAPI PathCchRemoveBackslash(LPWSTR path, SIZE_T size)
{
    return PathCchRemoveBackslashEx(path, size, nullptr, nullptr);
}

/* the last dot after the last backslash or space, or the end. */
static LPCWSTR find_extension(LPCWSTR path, SIZE_T size)
{
    LPCWSTR dot = nullptr, end = path + size;
    for (; *path; path++) {
        if (path + 1 >= end)
            return nullptr;
        if (*path == L'.')
            dot = path;
        else if (*path == L' ' || *path == L'\\')
            dot = nullptr;
    }
    return dot ? dot : path;
}

LWSTDAPI HRESULT WINAPI PathCchFindExtension(LPCWSTR path, SIZE_T size, LPCWSTR *extension)
{
    *extension = nullptr;
    if (!path || !valid_size(size))
        return E_INVALIDARG;
    *extension = find_extension(path, size);
    return *extension ? S_OK : E_INVALIDARG;
}

/* no space, backslash or second dot, below 256 characters. */
static BOOL valid_extension(LPCWSTR extension)
{
    if (!extension)
        return FALSE;
    SIZE_T length = wcslen(extension);
    if (length > 256 || (length == 256 && *extension != L'.'))
        return FALSE;
    return !wcschr(extension, L' ') && !wcschr(extension, L'\\') && (!length || !wcschr(extension + 1, L'.'));
}

/* writes ".ext" at dot, keeping the old text on failure. */
static HRESULT write_extension(LPWSTR path, SIZE_T size, LPWSTR dot, LPCWSTR extension, BOOL extended)
{
    SIZE_T room = size - (SIZE_T)(dot - path), needed = wcslen(extension) + (*extension != L'.');
    if (needed >= room) {
        if ((extended && size == PATHCCH_MAX_CCH) || (!extended && size == MAX_PATH))
            return E_FILENAME_EXCED_RANGE;
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    if (*extension != L'.')
        *dot++ = L'.';
    wcscpy(dot, extension);
    return S_OK;
}

LWSTDAPI HRESULT WINAPI PathCchAddExtension(LPWSTR path, SIZE_T size, LPCWSTR extension)
{
    if (!path || !valid_size(size) || !valid_extension(extension))
        return E_INVALIDARG;
    BOOL extended = is_extended(path);
    if (!extended && size > MAX_PATH)
        size = MAX_PATH;
    LPCWSTR dot;
    HRESULT hr = PathCchFindExtension(path, size, &dot);
    if (FAILED(hr))
        return hr;
    if (*dot)
        return S_FALSE;
    if (!*extension || (extension[0] == L'.' && !extension[1]))
        return S_OK;
    return write_extension(path, size, (LPWSTR)dot, extension, extended);
}

LWSTDAPI HRESULT WINAPI PathCchRenameExtension(LPWSTR path, SIZE_T size, LPCWSTR extension)
{
    if (!path || !valid_size(size) || !valid_extension(extension))
        return E_INVALIDARG;
    BOOL extended = is_extended(path);
    if (!extended && size > MAX_PATH)
        size = MAX_PATH;
    LPCWSTR dot;
    HRESULT hr = PathCchFindExtension(path, size, &dot);
    if (FAILED(hr))
        return hr;
    if (!*extension || (extension[0] == L'.' && !extension[1])) {
        *(LPWSTR)dot = 0;
        return S_OK;
    }
    WCHAR saved = *dot;
    hr = write_extension(path, size, (LPWSTR)dot, extension, extended);
    if (FAILED(hr))
        *(LPWSTR)dot = saved;
    return hr;
}

LWSTDAPI HRESULT WINAPI PathCchRemoveExtension(LPWSTR path, SIZE_T size)
{
    if (!path || !valid_size(size))
        return E_INVALIDARG;
    if (!is_extended(path) && size > MAX_PATH)
        size = MAX_PATH;
    LPWSTR dot = (LPWSTR)find_extension(path, size);
    if (!dot)
        return E_INVALIDARG;
    if (!*dot)
        return S_FALSE;
    *dot = 0;
    return S_OK;
}

LWSTDAPI HRESULT WINAPI PathCchStripPrefix(LPWSTR path, SIZE_T size)
{
    if (!path || !valid_size(size))
        return E_INVALIDARG;
    if (!is_extended(path))
        return S_FALSE;
    if (prefix_nocase(path + 4, L"UNC\\")) {
        if (size < 8)
            return E_INVALIDARG;
        return KbShCopy(path + 2, size - 2, path + 8);
    }
    if (size < 4)
        return E_INVALIDARG;
    if (!KbIsDriveLetter(path[4]) || path[5] != L':')
        return S_FALSE;
    return KbShCopy(path, size, path + 4);
}

/* ------------------------------------------------------------------ canonical forms */

typedef struct {
    LPWSTR start, at;
    SIZE_T room;
    HRESULT hr;
} PATH_OUT;

static void out_append(PATH_OUT *out, LPCWSTR text, SIZE_T length)
{
    if (FAILED(out->hr))
        return;
    if (length >= out->room) {
        out->hr = STRSAFE_E_INSUFFICIENT_BUFFER;
        return;
    }
    memcpy(out->at, text, length * sizeof(WCHAR));
    out->at += length;
    out->room -= length;
    *out->at = 0;
}

static BOOL out_is_root(PATH_OUT *out)
{
    return PathCchIsRoot(out->start);
}

/* "." and ".." are resolved segment by segment: ".." climbs to the previous backslash written, never above a
   root, and a path that empties becomes "\". */
LWSTDAPI HRESULT WINAPI PathCchCanonicalizeEx(LPWSTR result, SIZE_T size, LPCWSTR path, ULONG flags)
{
    if (!size || size > 0x7FFFFFFF) {
        return E_INVALIDARG;
    }
    *result = 0;
    if (size > PATHCCH_MAX_CCH)
        return E_INVALIDARG;
    if ((flags & PATHCCH_ALLOW_LONG_PATHS) &&
        (flags & (PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS | PATHCCH_FORCE_DISABLE_LONG_NAME_PROCESS)) ==
            (PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS | PATHCCH_FORCE_DISABLE_LONG_NAME_PROCESS))
        return E_INVALIDARG;
    if (!(flags & PATHCCH_ALLOW_LONG_PATHS) &&
        (flags & (PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS | PATHCCH_FORCE_DISABLE_LONG_NAME_PROCESS)))
        return E_INVALIDARG;
    if ((flags & (PATHCCH_ALLOW_LONG_PATHS | PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH)) ==
        (PATHCCH_ALLOW_LONG_PATHS | PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH))
        return E_INVALIDARG;
    if (!path)
        return E_INVALIDARG;
    BOOL long_paths = (flags & (PATHCCH_ALLOW_LONG_PATHS | PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH)) != 0;
    SIZE_T limit = long_paths ? PATHCCH_MAX_CCH : MAX_PATH;
    if (size < limit)
        limit = size;
    BOOL both = (flags & PATHCCH_CANONICALIZE_SLASHES) != 0;
    PATH_OUT out = {result, result, limit, S_OK};
    LPCWSTR in = path, segments_start;

    SIZE_T unc = unc_prefix(path);
    if (unc) {
        out_append(&out, (flags & PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH) ? L"\\\\?\\UNC\\" : L"\\\\",
                   (flags & PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH) ? 8 : 2);
        in += unc;
    } else {
        LPCWSTR drive = is_extended(path) ? path + 4 : path;
        if (KbIsDriveLetter(drive[0]) && drive[1] == L':') {
            if (flags & PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH)
                out_append(&out, L"\\\\?\\", 4);
            in = drive;
        }
    }
    segments_start = in;

    while (SUCCEEDED(out.hr) && *in) {
        LPCWSTR separator = in;
        while (*separator && *separator != L'\\' && !(both && *separator == L'/'))
            separator++;
        SIZE_T length = (SIZE_T)(separator - in);
        if ((length > 256 && !long_paths) || length >= PATHCCH_MAX_CCH) {
            out.hr = E_FILENAME_EXCED_RANGE;
            break;
        }
        if (length == 0) {
            out_append(&out, L"\\", 1);
            in++;
            if (both && in > segments_start)
                while (*in == L'\\' || *in == L'/')
                    in++;
            continue;
        }
        if (length == 1 && in[0] == L'.') {
            if (*separator) {
                in = separator + 1;
            } else {
                in = separator;
                if (out.at > out.start && !out_is_root(&out)) {
                    *--out.at = 0;
                    out.room++;
                }
            }
            continue;
        }
        if (length == 2 && in[0] == L'.' && in[1] == L'.') {
            if (out.at <= out.start || out_is_root(&out)) {
                in = *separator ? separator + 1 : separator;
                continue;
            }
            LPWSTR back = out.at - 1;
            while (back > out.start && *--back != L'\\')
                ;
            if (*back != L'\\')
                back = out.start;
            out.room += (SIZE_T)(out.at - back);
            out.at = back;
            *out.at = 0;
            in = separator;
            continue;
        }
        out_append(&out, in, length);
        in = separator;
    }

    if (SUCCEEDED(out.hr) && (flags & PATHCCH_ENSURE_TRAILING_SLASH) && out.at > out.start && out.at[-1] != L'\\')
        out_append(&out, L"\\", 1);
    if (FAILED(out.hr)) {
        KbShCopy(result, limit, L"");
        if (out.hr == STRSAFE_E_INSUFFICIENT_BUFFER && limit == (long_paths ? PATHCCH_MAX_CCH : MAX_PATH))
            return E_FILENAME_EXCED_RANGE;
        return out.hr;
    }
    if (!(flags & (PATHCCH_DO_NOT_NORMALIZE_SEGMENTS | PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH))) {
        /* trailing dots go, but not after a star, and an alternate data stream suffix too. */
        LPWSTR end = out.at;
        while (end > out.start && end[-1] == L'.') {
            if (end - 1 == out.start) {
                end[-1] = 0;
                end--;
                break;
            }
            if (end[-2] == L'*')
                break;
            *--end = 0;
        }
        out.at = end;
        SIZE_T length = wcslen(result);
        if (length >= 7 && !_wcsicmp(result + length - 7, L"::$DATA"))
            result[length - 7] = 0;
    }
    if (limit > 1 && !result[0]) {
        result[0] = L'\\';
        result[1] = 0;
    }
    if (limit > 3 && result[1] == L':' && !result[2]) {
        result[2] = L'\\';
        result[3] = 0;
    }
    return S_OK;
}

LWSTDAPI HRESULT WINAPI PathCchCanonicalize(LPWSTR result, SIZE_T size, LPCWSTR path)
{
    return PathCchCanonicalizeEx(result, size, path, PATHCCH_NONE);
}

/* path and more joined as PathCchCombineEx joins them, before canonicalization, into a heap buffer. */
static HRESULT join(LPCWSTR path, LPCWSTR more, LPWSTR *joined)
{
    SIZE_T path_length = path ? wcslen(path) : 0, more_length = more ? wcslen(more) : 0;
    if (path_length >= PATHCCH_MAX_CCH || more_length >= PATHCCH_MAX_CCH)
        return E_FILENAME_EXCED_RANGE;
    SIZE_T size = path_length + more_length + 3;
    LPWSTR buffer = KbAlloc(size * sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    buffer[0] = 0;
    if (!path_length) {
        if (more_length)
            wcscpy(buffer, more);
    } else if (!more_length) {
        wcscpy(buffer, path);
    } else if (more[0] != L'\\' && !(KbIsDriveLetter(more[0]) && more[1] == L':')) {
        wcscpy(buffer, path);
        if (buffer[path_length - 1] != L'\\')
            wcscat(buffer, L"\\");
        wcscat(buffer, more);
    } else if (more[0] != L'\\' || more[1] == L'\\') {
        wcscpy(buffer, more);
    } else {
        wcscpy(buffer, path);
        HRESULT hr = PathCchStripToRoot(buffer, size);
        if (FAILED(hr)) {
            KbFree(buffer);
            return hr;
        }
        SIZE_T length = wcslen(buffer);
        if (length && buffer[length - 1] != L'\\')
            wcscat(buffer, L"\\");
        wcscat(buffer, more + 1);
    }
    *joined = buffer;
    return S_OK;
}

LWSTDAPI HRESULT WINAPI PathCchCombineEx(LPWSTR result, SIZE_T size, LPCWSTR path, LPCWSTR more, ULONG flags)
{
    if (!result || !valid_size(size))
        return E_INVALIDARG;
    LPWSTR joined = nullptr;
    HRESULT hr = !path && !more ? E_INVALIDARG : join(path, more, &joined);
    if (FAILED(hr)) {
        KbShCopy(result, size, L"");
        return hr;
    }
    hr = PathCchCanonicalizeEx(result, size, joined, flags);
    KbFree(joined);
    return hr;
}

LWSTDAPI HRESULT WINAPI PathCchCombine(LPWSTR result, SIZE_T size, LPCWSTR path, LPCWSTR more)
{
    return PathCchCombineEx(result, size, path, more, PATHCCH_NONE);
}

/* leading backslashes of more go unless it is a unc or extended path. */
static LPCWSTR appendable(LPCWSTR more)
{
    if (!more)
        return more;
    if (more[0] == L'\\' && more[1] == L'\\' && !is_volume_path(more) &&
        (more[2] != L'?' || prefix_nocase(more + 3, L"\\UNC\\")))
        return more;
    if (!is_extended(more))
        while (*more == L'\\')
            more++;
    return more;
}

LWSTDAPI HRESULT WINAPI PathCchAppendEx(LPWSTR path, SIZE_T size, LPCWSTR more, ULONG flags)
{
    return PathCchCombineEx(path, size, path, appendable(more), flags);
}

LWSTDAPI HRESULT WINAPI PathCchAppend(LPWSTR path, SIZE_T size, LPCWSTR more)
{
    return PathCchAppendEx(path, size, more, PATHCCH_NONE);
}

static HRESULT check_alloc_flags(ULONG flags)
{
    if ((flags & PATHCCH_ALLOW_LONG_PATHS)
            ? ((flags & 6) && ((flags & 6) & ((flags & 6) - 1)))
            : (flags & 6) != 0)
        return E_INVALIDARG;
    if ((flags & 0x11) && ((flags & 0x11) & ((flags & 0x11) - 1)))
        return E_INVALIDARG;
    return S_OK;
}

/* the buffer holds the longest result the flags allow, freed with LocalFree. */
LWSTDAPI HRESULT WINAPI PathAllocCanonicalize(LPCWSTR path, ULONG flags, LPWSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = check_alloc_flags(flags);
    if (FAILED(hr))
        return hr;
    if (!path)
        return E_INVALIDARG;
    if (wcslen(path) >= PATHCCH_MAX_CCH)
        return E_FILENAME_EXCED_RANGE;
    SIZE_T size = (flags & 0x11) ? PATHCCH_MAX_CCH : MAX_PATH;
    LPWSTR buffer = LocalAlloc(LMEM_ZEROINIT, size * sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    hr = PathCchCanonicalizeEx(buffer, size, path, flags);
    if (FAILED(hr)) {
        LocalFree(buffer);
        return hr;
    }
    *out = buffer;
    return S_OK;
}

LWSTDAPI HRESULT WINAPI PathAllocCombine(LPCWSTR path, LPCWSTR more, ULONG flags, LPWSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = check_alloc_flags(flags);
    if (FAILED(hr))
        return hr;
    if (!path && !more)
        return E_INVALIDARG;
    if ((path && wcslen(path) >= PATHCCH_MAX_CCH) || (more && wcslen(more) >= PATHCCH_MAX_CCH))
        return E_FILENAME_EXCED_RANGE;
    SIZE_T size = (flags & 0x11) ? PATHCCH_MAX_CCH : MAX_PATH;
    LPWSTR buffer = LocalAlloc(LMEM_ZEROINIT, size * sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    hr = PathCchCombineEx(buffer, size, path, more, flags);
    if (FAILED(hr)) {
        LocalFree(buffer);
        return hr;
    }
    *out = buffer;
    return S_OK;
}
