/*
 * path helpers. like windows they work on MAX_PATH buffers and fail
 * rather than overflow them; both separators are accepted where windows
 * accepts them.
 */
#include "shlwapi_private.h"

static BOOL is_separator(WCHAR c)
{
    return c == L'\\' || c == L'/';
}

static BOOL is_drive(LPCWSTR path)
{
    WCHAR c = path[0] | 0x20;
    return c >= L'a' && c <= L'z' && path[1] == L':';
}

/* converts an ansi path to a heap allocated wide string. */
static LPWSTR widen(LPCSTR path)
{
    if (!path)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    LPWSTR wide = n > 0 ? HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, path, -1, wide, n);
    return wide;
}

/* ------------------------------------------------------------------ existence */

LWSTDAPI BOOL WINAPI PathFileExistsW(LPCWSTR path)
{
    if (!path)
        return FALSE;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    DWORD attributes = GetFileAttributesW(path);
    SetErrorMode(mode);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

LWSTDAPI BOOL WINAPI PathFileExistsA(LPCSTR path)
{
    LPWSTR wide = widen(path);
    BOOL result = wide && PathFileExistsW(wide);
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

/* returns FILE_ATTRIBUTE_DIRECTORY rather than TRUE, as documented. */
LWSTDAPI BOOL WINAPI PathIsDirectoryW(LPCWSTR path)
{
    if (!path)
        return FALSE;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    return (BOOL)(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

LWSTDAPI BOOL WINAPI PathIsDirectoryA(LPCSTR path)
{
    LPWSTR wide = widen(path);
    BOOL result = wide ? PathIsDirectoryW(wide) : FALSE;
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

LWSTDAPI BOOL WINAPI PathIsDirectoryEmptyW(LPCWSTR path)
{
    if (!path)
        return FALSE;
    SIZE_T length = wcslen(path);
    if (length + 5 > MAX_PATH)
        return FALSE;
    WCHAR pattern[MAX_PATH];
    wcscpy(pattern, path);
    if (length && !is_separator(pattern[length - 1]))
        pattern[length++] = L'\\';
    wcscpy(pattern + length, L"*.*");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL empty = TRUE;
    do {
        if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..")) {
            empty = FALSE;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return empty;
}

LWSTDAPI BOOL WINAPI PathIsDirectoryEmptyA(LPCSTR path)
{
    LPWSTR wide = widen(path);
    BOOL result = wide && PathIsDirectoryEmptyW(wide);
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

/* ------------------------------------------------------------------ components */

LWSTDAPI LPWSTR WINAPI PathFindFileNameW(LPCWSTR path)
{
    if (!path)
        return nullptr;
    LPCWSTR name = path;
    for (LPCWSTR p = path; *p; p++)
        if ((is_separator(*p) || *p == L':') && p[1] && !is_separator(p[1]))
            name = p + 1;
    return (LPWSTR)name;
}

LWSTDAPI LPSTR WINAPI PathFindFileNameA(LPCSTR path)
{
    if (!path)
        return nullptr;
    LPCSTR name = path;
    for (LPCSTR p = path; *p; p++)
        if ((*p == '\\' || *p == '/' || *p == ':') && p[1] && p[1] != '\\' && p[1] != '/')
            name = p + 1;
    return (LPSTR)name;
}

LWSTDAPI LPWSTR WINAPI PathFindExtensionW(LPCWSTR path)
{
    if (!path)
        return nullptr;
    LPCWSTR dot = nullptr, p = path;
    for (; *p; p++) {
        if (is_separator(*p) || *p == L' ')
            dot = nullptr;
        else if (*p == L'.')
            dot = p;
    }
    return (LPWSTR)(dot ? dot : p);
}

LWSTDAPI LPSTR WINAPI PathFindExtensionA(LPCSTR path)
{
    if (!path)
        return nullptr;
    LPCSTR dot = nullptr, p = path;
    for (; *p; p++) {
        if (*p == '\\' || *p == '/' || *p == ' ')
            dot = nullptr;
        else if (*p == '.')
            dot = p;
    }
    return (LPSTR)(dot ? dot : p);
}

LWSTDAPI LPWSTR WINAPI PathAddBackslashW(LPWSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T length = wcslen(path);
    if (length && path[length - 1] != L'\\') {
        if (length + 1 >= MAX_PATH)
            return nullptr;
        path[length++] = L'\\';
        path[length] = 0;
    }
    return path + length;
}

/* the length of the root: "\", "x:", "x:\" or "\\server\share\". */
static SIZE_T root_length(LPCWSTR path)
{
    if (is_drive(path))
        return is_separator(path[2]) ? 3 : 2;
    if (is_separator(path[0]) && is_separator(path[1])) {
        LPCWSTR p = path + 2;
        for (int parts = 0; *p && parts < 2; parts++) {
            while (*p && !is_separator(*p))
                p++;
            if (*p)
                p++;
        }
        return (SIZE_T)(p - path);
    }
    return is_separator(path[0]) ? 1 : 0;
}

LWSTDAPI LPWSTR WINAPI PathRemoveBackslashW(LPWSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T length = wcslen(path);
    if (!length)
        return path;
    LPWSTR last = path + length - 1;
    if (*last == L'\\' && length > root_length(path)) {
        *last = 0;
        return last;
    }
    return last;
}

LWSTDAPI BOOL WINAPI PathRemoveFileSpecW(LPWSTR path)
{
    if (!path || !path[0])
        return FALSE;
    SIZE_T root = root_length(path), length = wcslen(path);
    LPWSTR cut = nullptr;
    for (LPWSTR p = path + root; *p; p++)
        if (is_separator(*p))
            cut = p;
    if (cut) {
        *cut = 0;
        return TRUE;
    }
    if (length > root) {
        path[root] = 0;
        return TRUE;
    }
    return FALSE;
}

LWSTDAPI void WINAPI PathRemoveExtensionW(LPWSTR path)
{
    if (path)
        *PathFindExtensionW(path) = 0;
}

LWSTDAPI BOOL WINAPI PathRenameExtensionW(LPWSTR path, LPCWSTR extension)
{
    if (!path || !extension)
        return FALSE;
    LPWSTR dot = PathFindExtensionW(path);
    if ((SIZE_T)(dot - path) + wcslen(extension) >= MAX_PATH)
        return FALSE;
    wcscpy(dot, extension);
    return TRUE;
}

LWSTDAPI void WINAPI PathStripPathW(LPWSTR path)
{
    if (!path)
        return;
    LPWSTR name = PathFindFileNameW(path);
    if (name != path)
        memmove(path, name, (wcslen(name) + 1) * sizeof(WCHAR));
}

LWSTDAPI LPWSTR WINAPI PathSkipRootW(LPCWSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T root = root_length(path);
    if (!root || (is_drive(path) && root == 2))
        return nullptr;
    return (LPWSTR)path + root;
}

LWSTDAPI BOOL WINAPI PathStripToRootW(LPWSTR path)
{
    if (!path)
        return FALSE;
    SIZE_T root = root_length(path);
    if (root && is_separator(path[0]) && is_separator(path[1]) && is_separator(path[root - 1]))
        root--;
    path[root] = 0;
    return root != 0;
}

LWSTDAPI BOOL WINAPI PathIsRelativeW(LPCWSTR path)
{
    if (!path || !path[0])
        return TRUE;
    return !is_separator(path[0]) && path[1] != L':';
}

LWSTDAPI BOOL WINAPI PathIsUNCW(LPCWSTR path)
{
    if (!path || path[0] != L'\\' || path[1] != L'\\')
        return FALSE;
    if (path[2] == L'?')
        return !_wcsnicmp(path, L"\\\\?\\UNC\\", 8);
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathIsRootW(LPCWSTR path)
{
    if (!path || !path[0])
        return FALSE;
    if (is_separator(path[0]) && !path[1])
        return TRUE;
    if (is_drive(path))
        return is_separator(path[2]) && !path[3];
    if (is_separator(path[0]) && is_separator(path[1])) {
        /* "\\server" and "\\server\share" with an optional final separator. */
        LPCWSTR p = path + 2;
        int separators = 0;
        for (; *p; p++)
            if (is_separator(*p) && p[1])
                separators++;
        return separators <= 1;
    }
    return FALSE;
}

LWSTDAPI int WINAPI PathGetDriveNumberW(LPCWSTR path)
{
    if (!path)
        return -1;
    if (!wcsncmp(path, L"\\\\?\\", 4))
        path += 4;
    if (!is_drive(path))
        return -1;
    return (path[0] | 0x20) - L'a';
}

/* ------------------------------------------------------------------ combining */

/* resolves "." and ".." segments of a path in place. */
static void canonicalize(LPWSTR path)
{
    SIZE_T root = root_length(path);
    LPWSTR out = path + root, in = path + root;
    while (*in) {
        LPWSTR end = in;
        while (*end && !is_separator(*end))
            end++;
        SIZE_T n = (SIZE_T)(end - in);
        if (n == 1 && in[0] == L'.') {
            /* nothing. */
        } else if (n == 2 && in[0] == L'.' && in[1] == L'.') {
            if (out > path + root) {
                out--;
                while (out > path + root && !is_separator(out[-1]))
                    out--;
            }
        } else {
            memmove(out, in, n * sizeof(WCHAR));
            out += n;
            if (*end)
                *out++ = L'\\';
        }
        in = *end ? end + 1 : end;
    }
    *out = 0;
}

LWSTDAPI LPWSTR WINAPI PathCombineW(LPWSTR out, LPCWSTR directory, LPCWSTR file)
{
    if (!out)
        return nullptr;
    if (!directory && !file) {
        out[0] = 0;
        return nullptr;
    }
    WCHAR buffer[MAX_PATH * 2];
    if (!file || !file[0]) {
        if (wcslen(directory) >= MAX_PATH)
            goto fail;
        wcscpy(buffer, directory);
    } else if (!directory || !directory[0] || is_drive(file) || (is_separator(file[0]) && is_separator(file[1]))) {
        if (wcslen(file) >= MAX_PATH)
            goto fail;
        wcscpy(buffer, file);
    } else {
        SIZE_T length = wcslen(directory);
        if (length >= MAX_PATH || wcslen(file) >= MAX_PATH)
            goto fail;
        wcscpy(buffer, directory);
        if (is_separator(file[0])) {
            /* relative to the root of the directory. */
            SIZE_T root = root_length(buffer);
            if (root && is_separator(buffer[root - 1]))
                root--;
            length = root;
        } else if (length && !is_separator(buffer[length - 1])) {
            buffer[length++] = L'\\';
        }
        wcscpy(buffer + length, file);
    }
    canonicalize(buffer);
    if (wcslen(buffer) >= MAX_PATH)
        goto fail;
    wcscpy(out, buffer);
    return out;
fail:
    out[0] = 0;
    return nullptr;
}

LWSTDAPI BOOL WINAPI PathAppendW(LPWSTR path, LPCWSTR more)
{
    if (!path || !more)
        return FALSE;
    if (!(is_separator(more[0]) && is_separator(more[1])) && !is_drive(more))
        while (is_separator(*more))
            more++;
    return PathCombineW(path, path, more) != nullptr;
}

/* ------------------------------------------------------------------ command lines and quoting */

LWSTDAPI LPWSTR WINAPI PathGetArgsW(LPCWSTR path)
{
    if (!path)
        return nullptr;
    BOOL quoted = FALSE;
    for (; *path; path++) {
        if (*path == L'"')
            quoted = !quoted;
        else if (*path == L' ' && !quoted)
            return (LPWSTR)path + 1;
    }
    return (LPWSTR)path;
}

LWSTDAPI BOOL WINAPI PathQuoteSpacesW(LPWSTR path)
{
    if (!path || !wcschr(path, L' ') || path[0] == L'"')
        return FALSE;
    SIZE_T length = wcslen(path);
    if (length + 3 > MAX_PATH)
        return FALSE;
    memmove(path + 1, path, (length + 1) * sizeof(WCHAR));
    path[0] = L'"';
    path[length + 1] = L'"';
    path[length + 2] = 0;
    return TRUE;
}

LWSTDAPI void WINAPI PathUnquoteSpacesW(LPWSTR path)
{
    if (!path || path[0] != L'"')
        return;
    SIZE_T length = wcslen(path);
    if (length < 2 || path[length - 1] != L'"')
        return;
    memmove(path, path + 1, (length - 2) * sizeof(WCHAR));
    path[length - 2] = 0;
}

LWSTDAPI void WINAPI PathRemoveBlanksW(LPWSTR path)
{
    if (!path)
        return;
    LPWSTR start = path;
    while (*start == L' ')
        start++;
    SIZE_T length = wcslen(start);
    while (length && start[length - 1] == L' ')
        length--;
    memmove(path, start, length * sizeof(WCHAR));
    path[length] = 0;
}

/* matches one pattern with * and ?, case insensitively. */
static BOOL match_one(LPCWSTR name, LPCWSTR spec, LPCWSTR spec_end)
{
    while (spec < spec_end) {
        if (*spec == L'*') {
            spec++;
            while (spec < spec_end && *spec == L'*')
                spec++;
            if (spec == spec_end)
                return TRUE;
            for (; *name; name++)
                if (match_one(name, spec, spec_end))
                    return TRUE;
            return FALSE;
        }
        if (!*name)
            return FALSE;
        if (*spec != L'?' && towupper(*spec) != towupper(*name))
            return FALSE;
        spec++;
        name++;
    }
    return !*name;
}

LWSTDAPI BOOL WINAPI PathMatchSpecW(LPCWSTR path, LPCWSTR spec)
{
    if (!path || !spec)
        return FALSE;
    if (!wcscmp(spec, L"*.*"))
        return TRUE;
    LPCWSTR name = PathFindFileNameW(path);
    while (*spec) {
        while (*spec == L' ')
            spec++;
        LPCWSTR end = spec;
        while (*end && *end != L';')
            end++;
        LPCWSTR trimmed = end;
        while (trimmed > spec && trimmed[-1] == L' ')
            trimmed--;
        if (trimmed > spec && match_one(name, spec, trimmed))
            return TRUE;
        spec = *end ? end + 1 : end;
    }
    return FALSE;
}
