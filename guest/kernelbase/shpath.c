/*
 * the legacy shlwapi path functions kernelbase exports: MAX_PATH buffers over the PathCch core,
 * with the ansi forms converting through the ansi code page.
 */
#include "shlwapi_kb.h"

PWSTR KbShAnsiToWide(LPCSTR s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    PWSTR w = n > 0 ? KbAlloc((SIZE_T)n * sizeof(WCHAR)) : nullptr;
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

BOOL KbShWideToAnsiBuffer(LPCWSTR wide, LPSTR out, int size)
{
    if (!size)
        return FALSE;
    int n = WideCharToMultiByte(CP_ACP, 0, wide, -1, out, size, nullptr, nullptr);
    if (!n) {
        out[size - 1] = 0;
        return FALSE;
    }
    return TRUE;
}

/* where the ansi string a has the character wide_at of its wide form w. */
static LPSTR ansi_at(LPCSTR a, LPCWSTR w, LPCWSTR wide_at)
{
    if (!wide_at)
        return nullptr;
    int bytes = wide_at > w ? WideCharToMultiByte(CP_ACP, 0, w, (int)(wide_at - w), nullptr, 0, nullptr, nullptr) : 0;
    return (LPSTR)a + bytes;
}

/* the wide form of an ansi MAX_PATH path for the functions that edit it in place. */
static BOOL widen_path(LPCSTR path, WCHAR buffer[MAX_PATH])
{
    PWSTR w = KbShAnsiToWide(path);
    if (!w)
        return FALSE;
    KbShCopy(buffer, MAX_PATH, w);
    KbFree(w);
    return TRUE;
}

/* ------------------------------------------------------------------ backslashes */

LWSTDAPI LPWSTR WINAPI PathAddBackslashW(LPWSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T length = wcslen(path);
    if (length >= MAX_PATH)
        return nullptr;
    if (!length || path[length - 1] == L'\\')
        return path + length;
    if (length + 1 >= MAX_PATH)
        return nullptr;
    path[length] = L'\\';
    path[length + 1] = 0;
    return path + length + 1;
}

LWSTDAPI LPSTR WINAPI PathAddBackslashA(LPSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T length = strlen(path);
    if (length >= MAX_PATH)
        return nullptr;
    if (!length || *KbShPrevA(path, path + length) == '\\')
        return path + length;
    if (length + 1 >= MAX_PATH)
        return nullptr;
    path[length] = '\\';
    path[length + 1] = 0;
    return path + length + 1;
}

/* the terminator that replaced a final backslash, else the last character; a root keeps its own. */
LWSTDAPI LPWSTR WINAPI PathRemoveBackslashW(LPWSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T length = wcslen(path);
    if (length >= PATHCCH_MAX_CCH)
        return nullptr;
    if (!length)
        return path;
    if (path[length - 1] == L'\\') {
        if (!PathCchIsRoot(path)) {
            path[length - 1] = 0;
        }
        return path + length - 1;
    }
    return path + length - 1;
}

LWSTDAPI LPSTR WINAPI PathRemoveBackslashA(LPSTR path)
{
    if (!path)
        return nullptr;
    SIZE_T length = strlen(path);
    if (!length)
        return path;
    LPSTR last = KbShPrevA(path, path + length);
    if (*last == '\\' && !PathIsRootA(path))
        *last = 0;
    return last;
}

/* ------------------------------------------------------------------ components */

LWSTDAPI LPWSTR WINAPI PathFindExtensionW(LPCWSTR path)
{
    if (!path)
        return nullptr;
    LPCWSTR dot = nullptr, p = path;
    for (; *p; p++) {
        if (p - path >= PATHCCH_MAX_CCH)
            return (LPWSTR)path + wcslen(path);
        if (*p == L'.')
            dot = p;
        else if (*p == L' ' || *p == L'\\')
            dot = nullptr;
    }
    return (LPWSTR)(dot ? dot : p);
}

LWSTDAPI LPSTR WINAPI PathFindExtensionA(LPCSTR path)
{
    if (!path)
        return nullptr;
    LPCSTR dot = nullptr, p = path;
    for (; *p; p = KbShNextA(p)) {
        if (*p == '.')
            dot = p;
        else if (*p == ' ' || *p == '\\')
            dot = nullptr;
    }
    return (LPSTR)(dot ? dot : p);
}

/* after the last separator followed by a name, or after a lone drive colon. */
LWSTDAPI LPWSTR WINAPI PathFindFileNameW(LPCWSTR path)
{
    if (!path || !*path)
        return (LPWSTR)path;
    LPCWSTR name = path, after_colon = nullptr;
    int colons = 0;
    for (LPCWSTR p = path; *p; p++) {
        WCHAR next = p[1];
        BOOL starts = next && next != L'\\' && next != L'/';
        if ((*p == L'\\' || *p == L'/') && starts) {
            name = p + 1;
            colons = 0;
            after_colon = nullptr;
        } else if (*p == L':') {
            colons++;
            if (!after_colon && starts)
                after_colon = p + 1;
        }
    }
    return (LPWSTR)(after_colon && colons == 1 ? after_colon : name);
}

LWSTDAPI LPSTR WINAPI PathFindFileNameA(LPCSTR path)
{
    if (!path || !*path)
        return (LPSTR)path;
    LPCSTR name = path, after_colon = nullptr;
    int colons = 0;
    for (LPCSTR p = path; *p; p = KbShNextA(p)) {
        LPCSTR n = KbShNextA(p);
        BOOL starts = *n && *n != '\\' && *n != '/';
        if ((*p == '\\' || *p == '/') && starts) {
            name = n;
            colons = 0;
            after_colon = nullptr;
        } else if (*p == ':') {
            colons++;
            if (!after_colon && starts)
                after_colon = n;
        }
    }
    return (LPSTR)(after_colon && colons == 1 ? after_colon : name);
}

LWSTDAPI LPWSTR WINAPI PathFindNextComponentW(LPCWSTR path)
{
    if (!path || !*path)
        return nullptr;
    LPCWSTR slash = wcschr(path, L'\\');
    if (!slash)
        return (LPWSTR)path + wcslen(path);
    return (LPWSTR)(slash[1] == L'\\' ? slash + 2 : slash + 1);
}

LWSTDAPI LPSTR WINAPI PathFindNextComponentA(LPCSTR path)
{
    if (!path || !*path)
        return nullptr;
    for (LPCSTR p = path; *p; p = KbShNextA(p))
        if (*p == '\\')
            return (LPSTR)(p[1] == '\\' ? p + 2 : p + 1);
    return (LPSTR)path + strlen(path);
}

/* the arguments after the first unquoted space and the spaces that follow it. */
LWSTDAPI LPWSTR WINAPI PathGetArgsW(LPCWSTR path)
{
    if (!path)
        return nullptr;
    BOOL quoted = FALSE, spaced = FALSE;
    for (; *path; path++) {
        if (*path == L'"' && !spaced) {
            quoted = !quoted;
        } else if (!quoted) {
            if (*path == L' ')
                spaced = TRUE;
            else if (spaced)
                return (LPWSTR)path;
        }
    }
    return (LPWSTR)path;
}

LWSTDAPI LPSTR WINAPI PathGetArgsA(LPCSTR path)
{
    if (!path)
        return nullptr;
    BOOL quoted = FALSE, spaced = FALSE;
    for (; *path; path = KbShNextA(path)) {
        if (*path == '"' && !spaced) {
            quoted = !quoted;
        } else if (!quoted) {
            if (*path == ' ')
                spaced = TRUE;
            else if (spaced)
                return (LPSTR)path;
        }
    }
    return (LPSTR)path;
}

LWSTDAPI int WINAPI PathGetDriveNumberW(LPCWSTR path)
{
    if (!path)
        return -1;
    if (path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\')
        path += 4;
    if (!path[0] || path[1] != L':')
        return -1;
    if (path[0] >= L'a' && path[0] <= L'z')
        return path[0] - L'a';
    if (path[0] >= L'A' && path[0] <= L'Z')
        return path[0] - L'A';
    return -1;
}

LWSTDAPI int WINAPI PathGetDriveNumberA(LPCSTR path)
{
    if (!path)
        return -1;
    if (path[0] == '\\' && path[1] == '\\' && path[2] == '?' && path[3] == '\\')
        path += 4;
    if (!path[0] || path[1] != ':')
        return -1;
    if (path[0] >= 'a' && path[0] <= 'z')
        return path[0] - 'a';
    if (path[0] >= 'A' && path[0] <= 'Z')
        return path[0] - 'A';
    return -1;
}

/* ------------------------------------------------------------------ classification */

LWSTDAPI BOOL WINAPI PathIsRelativeW(LPCWSTR path)
{
    if (!path || !*path)
        return TRUE;
    if (path[0] == L'\\')
        return FALSE;
    return !(path[1] == L':' && KbIsDriveLetter(path[0]));
}

LWSTDAPI BOOL WINAPI PathIsRelativeA(LPCSTR path)
{
    if (!path || !*path)
        return TRUE;
    if (path[0] == '\\')
        return FALSE;
    return !(path[1] == ':' && KbIsDriveLetter((WCHAR)(UCHAR)path[0]) && !IsDBCSLeadByte((BYTE)path[0]));
}

static BOOL unc_pipe(LPCWSTR path)
{
    return path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\' &&
           towlower(path[4]) == L'u' && towlower(path[5]) == L'n' && towlower(path[6]) == L'c' && path[7] == L'\\';
}

LWSTDAPI BOOL WINAPI PathIsUNCW(LPCWSTR path)
{
    if (!path || path[0] != L'\\' || path[1] != L'\\')
        return FALSE;
    return path[2] != L'?' || unc_pipe(path);
}

LWSTDAPI BOOL WINAPI PathIsUNCA(LPCSTR path)
{
    return path && path[0] == '\\' && path[1] == '\\' && path[2] != '?';
}

/* the characters before the server of a unc path, or -1. */
static int unc_server(LPCWSTR path)
{
    if (!path || path[0] != L'\\' || path[1] != L'\\')
        return -1;
    if (path[2] != L'?')
        return 2;
    return unc_pipe(path) ? 8 : -1;
}

LWSTDAPI BOOL WINAPI PathIsUNCServerW(LPCWSTR path)
{
    int server = unc_server(path);
    return server >= 0 && !wcschr(path + server, L'\\');
}

LWSTDAPI BOOL WINAPI PathIsUNCServerA(LPCSTR path)
{
    if (!path || path[0] != '\\' || path[1] != '\\')
        return FALSE;
    for (LPCSTR p = path + 2; *p; p = KbShNextA(p))
        if (*p == '\\')
            return FALSE;
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathIsUNCServerShareW(LPCWSTR path)
{
    int server = unc_server(path);
    if (server < 0)
        return FALSE;
    LPCWSTR share = wcschr(path + server, L'\\');
    return share && !wcschr(share + 1, L'\\');
}

LWSTDAPI BOOL WINAPI PathIsUNCServerShareA(LPCSTR path)
{
    if (!path || path[0] != '\\' || path[1] != '\\')
        return FALSE;
    int separators = 0;
    for (LPCSTR p = path + 2; *p; p = KbShNextA(p))
        if (*p == '\\')
            separators++;
    return separators == 1;
}

LWSTDAPI BOOL WINAPI PathIsRootW(LPCWSTR path)
{
    return PathCchIsRoot(path);
}

LWSTDAPI BOOL WINAPI PathIsRootA(LPCSTR path)
{
    if (!path || !*path)
        return FALSE;
    if (!IsDBCSLeadByte((BYTE)path[0]) && KbIsDriveLetter((WCHAR)(UCHAR)path[0]) && path[1] == ':' &&
        path[2] == '\\' && !path[3])
        return TRUE;
    if (path[0] != '\\')
        return FALSE;
    if (!path[1])
        return TRUE;
    if (path[1] != '\\' || path[2] == '?')
        return FALSE;
    int separators = 0;
    for (LPCSTR p = path + 2; *p; p = KbShNextA(p))
        if (*p == '\\' && (++separators > 1 || !p[1]))
            return FALSE;
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathIsFileSpecW(LPCWSTR path)
{
    if (!path)
        return FALSE;
    for (; *path; path++)
        if (*path == L'\\' || *path == L':')
            return FALSE;
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathIsFileSpecA(LPCSTR path)
{
    if (!path)
        return FALSE;
    for (; *path; path = KbShNextA(path))
        if (*path == '\\' || *path == ':')
            return FALSE;
    return TRUE;
}

/* a name 8.3 can not hold: a space, a second dot, more than eight characters before the dot or
   more than four after it, the count checked before each character. */
LWSTDAPI BOOL WINAPI PathIsLFNFileSpecW(LPCWSTR name)
{
    if (!name)
        return FALSE;
    BOOL dot = FALSE;
    int count = 1;
    for (; *name; name++) {
        if ((dot && count > 3) || *name == L' ')
            return TRUE;
        if (*name == L'.') {
            if (dot)
                return TRUE;
            dot = TRUE;
            count = 0;
        } else if (count > 8) {
            return TRUE;
        }
        count++;
    }
    return FALSE;
}

LWSTDAPI BOOL WINAPI PathIsLFNFileSpecA(LPCSTR name)
{
    if (!name)
        return FALSE;
    BOOL dot = FALSE;
    int count = 1;
    while (*name) {
        if ((dot && count > 3) || *name == ' ')
            return TRUE;
        if (*name == '.') {
            if (dot)
                return TRUE;
            dot = TRUE;
            count = 0;
        } else if (count > 8) {
            return TRUE;
        }
        BOOL lead = IsDBCSLeadByte((BYTE)*name);
        name += lead && name[1] ? 2 : 1;
        count += lead ? 2 : 1;
    }
    return FALSE;
}

LWSTDAPI UINT WINAPI PathGetCharTypeW(WCHAR c)
{
    switch (c) {
    case L'<':
    case L'>':
    case L'|':
    case L'/':
    case L'"':
        return GCT_INVALID;
    case L'?':
    case L'*':
        return GCT_WILD;
    case L'\\':
    case L':':
        return GCT_SEPARATOR;
    case L';':
    case L' ':
    case L',':
        return GCT_LFNCHAR;
    }
    return c > 0x20 ? GCT_LFNCHAR | GCT_SHORTCHAR : GCT_INVALID;
}

LWSTDAPI UINT WINAPI PathGetCharTypeA(UCHAR c)
{
    return PathGetCharTypeW((WCHAR)c);
}

LWSTDAPI BOOL WINAPI PathIsValidCharW(WCHAR c, DWORD flags)
{
    switch (c) {
    case L' ':
        return (flags & PIVC_ALLOW_SPACE) != 0;
    case L'"':
        return (flags & PIVC_ALLOW_QUOTE) != 0;
    case L'*':
        return (flags & PIVC_ALLOW_STAR) != 0;
    case L',':
        return (flags & PIVC_ALLOW_COMMA) != 0;
    case L'.':
        return (flags & PIVC_ALLOW_DOT) != 0;
    case L'/':
    case L'<':
    case L'>':
    case L'|':
        return FALSE;
    case L':':
        return (flags & PIVC_ALLOW_COLON) != 0;
    case L';':
        return (flags & PIVC_ALLOW_SEMICOLON) != 0;
    case L'?':
        return (flags & PIVC_ALLOW_QUESTIONMARK) != 0;
    case L'\\':
        return (flags & PIVC_ALLOW_SLASH) != 0;
    }
    if (KbIsDriveLetter(c))
        return TRUE;
    if (c < 0x20)
        return FALSE;
    return (flags & PIVC_ALLOW_NONALPHA) != 0;
}

LWSTDAPI BOOL WINAPI PathIsValidCharA(UCHAR c, DWORD flags)
{
    return PathIsValidCharW((WCHAR)c, flags);
}

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
    if (!path)
        return FALSE;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    DWORD attributes = GetFileAttributesA(path);
    SetErrorMode(mode);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

/* ------------------------------------------------------------------ prefixes and roots */

/* components compared without case; a common drive keeps its backslash. */
LWSTDAPI int WINAPI PathCommonPrefixW(LPCWSTR path1, LPCWSTR path2, LPWSTR common)
{
    if (!path1 || !path2)
        return 0;
    if (common)
        *common = 0;
    BOOL unc1 = path1[0] == L'\\' && path1[1] == L'\\', unc2 = path2[0] == L'\\' && path2[1] == L'\\';
    if (unc1 != unc2)
        return 0;
    LPCWSTR p1 = unc1 ? path1 + 2 : path1, p2 = unc2 ? path2 + 2 : path2, last = nullptr;
    for (;;) {
        LPCWSTR end1 = p1, end2 = p2;
        while (*end1 && *end1 != L'\\')
            end1++;
        while (*end2 && *end2 != L'\\')
            end2++;
        if (end1 - p1 != end2 - p2 ||
            CompareStringW(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, p1, (int)(end1 - p1), p2, (int)(end1 - p1)) !=
                CSTR_EQUAL)
            break;
        last = end1;
        if (!*end1 || !*end2)
            break;
        p1 = end1 + 1;
        p2 = end2 + 1;
    }
    int length = 0;
    if (last) {
        length = (int)(last - path1);
        if (length == 2 && path1[1] == L':' && path1[2] == L'\\' && path2[2] == L'\\')
            length = 3;
    }
    if (common && length < MAX_PATH) {
        memcpy(common, path1, (SIZE_T)length * sizeof(WCHAR));
        common[length] = 0;
    }
    return length;
}

LWSTDAPI int WINAPI PathCommonPrefixA(LPCSTR path1, LPCSTR path2, LPSTR common)
{
    if (!path1 || !path2)
        return 0;
    if (common)
        *common = 0;
    PWSTR w1 = KbShAnsiToWide(path1), w2 = KbShAnsiToWide(path2);
    int length = 0;
    if (w1 && w2) {
        int wide = PathCommonPrefixW(w1, w2, nullptr);
        length = (int)(ansi_at(path1, w1, w1 + wide) - path1);
        if (common && length < MAX_PATH) {
            memcpy(common, path1, (SIZE_T)length);
            common[length] = 0;
        }
    }
    KbFree(w1);
    KbFree(w2);
    return length;
}

LWSTDAPI BOOL WINAPI PathIsPrefixW(LPCWSTR prefix, LPCWSTR path)
{
    if (!prefix || !path)
        return FALSE;
    return PathCommonPrefixW(path, prefix, nullptr) == (int)wcslen(prefix);
}

LWSTDAPI BOOL WINAPI PathIsPrefixA(LPCSTR prefix, LPCSTR path)
{
    if (!prefix || !path)
        return FALSE;
    return PathCommonPrefixA(path, prefix, nullptr) == (int)strlen(prefix);
}

LWSTDAPI LPWSTR WINAPI PathSkipRootW(LPCWSTR path)
{
    LPCWSTR end;
    if (FAILED(PathCchSkipRoot(path, &end)))
        return nullptr;
    if (end == path + 2 && path[1] == L':')
        return nullptr;
    return (LPWSTR)end;
}

LWSTDAPI LPSTR WINAPI PathSkipRootA(LPCSTR path)
{
    if (!path)
        return nullptr;
    PWSTR w = KbShAnsiToWide(path);
    LPSTR result = w ? ansi_at(path, w, PathSkipRootW(w)) : nullptr;
    KbFree(w);
    return result;
}

LWSTDAPI BOOL WINAPI PathIsSameRootW(LPCWSTR path1, LPCWSTR path2)
{
    if (!path1 || !path2)
        return FALSE;
    LPWSTR end = PathSkipRootW(path1);
    if (!end)
        return FALSE;
    return end - path1 <= PathCommonPrefixW(path1, path2, nullptr) + 1;
}

LWSTDAPI BOOL WINAPI PathIsSameRootA(LPCSTR path1, LPCSTR path2)
{
    if (!path1 || !path2)
        return FALSE;
    PWSTR w1 = KbShAnsiToWide(path1), w2 = KbShAnsiToWide(path2);
    BOOL result = w1 && w2 && PathIsSameRootW(w1, w2);
    KbFree(w1);
    KbFree(w2);
    return result;
}

LWSTDAPI BOOL WINAPI PathStripToRootW(LPWSTR path)
{
    return path && SUCCEEDED(PathCchStripToRoot(path, PATHCCH_MAX_CCH));
}

LWSTDAPI BOOL WINAPI PathStripToRootA(LPSTR path)
{
    if (!path)
        return FALSE;
    WCHAR buffer[MAX_PATH];
    if (!widen_path(path, buffer))
        return FALSE;
    BOOL result = PathStripToRootW(buffer);
    KbShWideToAnsiBuffer(buffer, path, MAX_PATH);
    return result;
}

LWSTDAPI BOOL WINAPI PathRemoveFileSpecW(LPWSTR path)
{
    if (!path)
        return FALSE;
    HRESULT hr = PathCchRemoveFileSpec(path, PATHCCH_MAX_CCH);
    if (hr == S_FALSE && PathIsUNCW(path))
        hr = PathCchRemoveFileSpec(path + 2, PATHCCH_MAX_CCH - 2);
    return hr == S_OK;
}

LWSTDAPI BOOL WINAPI PathRemoveFileSpecA(LPSTR path)
{
    if (!path)
        return FALSE;
    WCHAR buffer[MAX_PATH];
    if (!widen_path(path, buffer))
        return FALSE;
    BOOL result = PathRemoveFileSpecW(buffer);
    KbShWideToAnsiBuffer(buffer, path, MAX_PATH);
    return result;
}

/* ------------------------------------------------------------------ extensions */

LWSTDAPI void WINAPI PathRemoveExtensionW(LPWSTR path)
{
    if (path)
        *PathFindExtensionW(path) = 0;
}

LWSTDAPI void WINAPI PathRemoveExtensionA(LPSTR path)
{
    if (path)
        *PathFindExtensionA(path) = 0;
}

LWSTDAPI BOOL WINAPI PathRenameExtensionW(LPWSTR path, LPCWSTR extension)
{
    if (!path || !extension)
        return FALSE;
    LPWSTR dot = PathFindExtensionW(path);
    SIZE_T at = (SIZE_T)(dot - path);
    if (at + wcslen(extension) >= MAX_PATH)
        return FALSE;
    KbShCopy(dot, MAX_PATH - at, extension);
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathRenameExtensionA(LPSTR path, LPCSTR extension)
{
    if (!path || !extension)
        return FALSE;
    LPSTR dot = PathFindExtensionA(path);
    SIZE_T at = (SIZE_T)(dot - path);
    if (at + strlen(extension) >= MAX_PATH)
        return FALSE;
    strcpy(dot, extension);
    return TRUE;
}

/* ".exe" when no extension is given; a path that has one is left alone. */
LWSTDAPI BOOL WINAPI PathAddExtensionW(LPWSTR path, LPCWSTR extension)
{
    if (!path)
        return FALSE;
    if (!extension)
        extension = L".exe";
    LPWSTR dot = PathFindExtensionW(path);
    if (*dot)
        return FALSE;
    SIZE_T at = (SIZE_T)(dot - path);
    if (at + wcslen(extension) >= MAX_PATH)
        return FALSE;
    KbShCopy(dot, MAX_PATH - at, extension);
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathAddExtensionA(LPSTR path, LPCSTR extension)
{
    if (!path)
        return FALSE;
    if (!extension)
        extension = ".exe";
    LPSTR dot = PathFindExtensionA(path);
    if (*dot)
        return FALSE;
    SIZE_T at = (SIZE_T)(dot - path);
    if (at + strlen(extension) >= MAX_PATH)
        return FALSE;
    strcpy(dot, extension);
    return TRUE;
}

/* ------------------------------------------------------------------ combining */

LWSTDAPI BOOL WINAPI PathCanonicalizeW(LPWSTR out, LPCWSTR path)
{
    if (!out) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *out = 0;
    if (!path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HRESULT hr = PathCchCanonicalizeEx(out, MAX_PATH, path, PATHCCH_NONE);
    if (SUCCEEDED(hr))
        return TRUE;
    SetLastError((hr & 0x1FFF0000) == 0x70000 ? (DWORD)(WORD)hr : (DWORD)hr);
    return FALSE;
}

LWSTDAPI BOOL WINAPI PathCanonicalizeA(LPSTR out, LPCSTR path)
{
    if (!out) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *out = 0;
    if (!path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR w = KbShAnsiToWide(path);
    WCHAR buffer[MAX_PATH];
    BOOL result = w && PathCanonicalizeW(buffer, w);
    KbFree(w);
    if (result && !KbShWideToAnsiBuffer(buffer, out, MAX_PATH)) {
        *out = 0;
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    return result;
}

LWSTDAPI LPWSTR WINAPI PathCombineW(LPWSTR out, LPCWSTR directory, LPCWSTR file)
{
    if (!out)
        return nullptr;
    if (FAILED(PathCchCombineEx(out, MAX_PATH, directory, file, PATHCCH_NONE))) {
        *out = 0;
        return nullptr;
    }
    return out;
}

LWSTDAPI LPSTR WINAPI PathCombineA(LPSTR out, LPCSTR directory, LPCSTR file)
{
    if (!out)
        return nullptr;
    PWSTR d = KbShAnsiToWide(directory), f = KbShAnsiToWide(file);
    WCHAR buffer[MAX_PATH];
    LPSTR result = nullptr;
    if ((!directory || d) && (!file || f) && PathCombineW(buffer, d, f) && KbShWideToAnsiBuffer(buffer, out, MAX_PATH))
        result = out;
    else
        *out = 0;
    KbFree(d);
    KbFree(f);
    return result;
}

/* more keeps its leading backslashes only as a unc or extended path. */
LWSTDAPI BOOL WINAPI PathAppendW(LPWSTR path, LPCWSTR more)
{
    if (!path || !more)
        return FALSE;
    return SUCCEEDED(PathCchAppendEx(path, MAX_PATH, more, PATHCCH_NONE));
}

LWSTDAPI BOOL WINAPI PathAppendA(LPSTR path, LPCSTR more)
{
    if (!path || !more)
        return FALSE;
    PWSTR p = KbShAnsiToWide(path), m = KbShAnsiToWide(more);
    WCHAR buffer[MAX_PATH];
    BOOL result = FALSE;
    if (p && m) {
        KbShCopy(buffer, MAX_PATH, p);
        result = PathAppendW(buffer, m) && KbShWideToAnsiBuffer(buffer, path, MAX_PATH);
    }
    KbFree(p);
    KbFree(m);
    return result;
}

/* from a directory, or the folder of a file, to to: ".." per component left over after the common
   prefix, then what to has beyond it. */
LWSTDAPI BOOL WINAPI PathRelativePathToW(LPWSTR path, LPCWSTR from, DWORD from_attributes, LPCWSTR to,
                                         DWORD to_attributes)
{
    WCHAR from_dir[MAX_PATH], to_dir[MAX_PATH];
    if (from && FAILED(KbShCopy(from_dir, MAX_PATH, from)))
        from = nullptr;
    if (to && FAILED(KbShCopy(to_dir, MAX_PATH, to)))
        to = nullptr;
    if (!path)
        return FALSE;
    *path = 0;
    if (!from || !to)
        return FALSE;
    if (!(from_attributes & FILE_ATTRIBUTE_DIRECTORY))
        PathRemoveFileSpecW(from_dir);
    if (!(to_attributes & FILE_ATTRIBUTE_DIRECTORY))
        PathRemoveFileSpecW(to_dir);
    int common = PathCommonPrefixW(from_dir, to_dir, nullptr);
    if (!common)
        return FALSE;
    LPCWSTR rest = from_dir + common;
    HRESULT hr = S_OK;
    if (*rest == L'\\')
        rest++;
    if (!*rest) {
        hr = KbShCopy(path, MAX_PATH, L".");
    } else {
        while (SUCCEEDED(hr)) {
            rest = PathFindNextComponentW(rest);
            if (rest && *rest) {
                hr = KbShCopy(path + wcslen(path), MAX_PATH - wcslen(path), L"..\\");
            } else {
                hr = KbShCopy(path + wcslen(path), MAX_PATH - wcslen(path), L"..");
                break;
            }
        }
    }
    /* what the destination has past the common folders, a file's name included. */
    if (SUCCEEDED(hr) && to[common]) {
        int at = to[common] == L'\\' ? common : common - 1;
        hr = KbShCopy(path + wcslen(path), MAX_PATH - wcslen(path), to + at);
    }
    if (FAILED(hr)) {
        *path = 0;
        return FALSE;
    }
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathRelativePathToA(LPSTR path, LPCSTR from, DWORD from_attributes, LPCSTR to,
                                         DWORD to_attributes)
{
    if (!path)
        return FALSE;
    *path = 0;
    if (!from || !to)
        return FALSE;
    PWSTR f = KbShAnsiToWide(from), t = KbShAnsiToWide(to);
    WCHAR buffer[MAX_PATH];
    BOOL result = f && t && PathRelativePathToW(buffer, f, from_attributes, t, to_attributes) &&
                  KbShWideToAnsiBuffer(buffer, path, MAX_PATH);
    KbFree(f);
    KbFree(t);
    return result;
}

/* ------------------------------------------------------------------ quoting and blanks */

/* wraps a path that holds a space, even one quoted already. */
LWSTDAPI BOOL WINAPI PathQuoteSpacesW(LPWSTR path)
{
    if (!path || !wcschr(path, L' '))
        return FALSE;
    SIZE_T length = wcslen(path);
    if (length + 2 >= MAX_PATH)
        return FALSE;
    memmove(path + 1, path, (length + 1) * sizeof(WCHAR));
    path[0] = L'"';
    path[length + 1] = L'"';
    path[length + 2] = 0;
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathQuoteSpacesA(LPSTR path)
{
    if (!path || !strchr(path, ' '))
        return FALSE;
    SIZE_T length = strlen(path);
    if (length + 2 >= MAX_PATH)
        return FALSE;
    memmove(path + 1, path, length + 1);
    path[0] = '"';
    path[length + 1] = '"';
    path[length + 2] = 0;
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathUnquoteSpacesW(LPWSTR path)
{
    if (!path || path[0] != L'"')
        return FALSE;
    SIZE_T length = wcslen(path);
    if (path[length - 1] != L'"')
        return FALSE;
    path[length - 1] = 0;
    memmove(path, path + 1, (length - 1) * sizeof(WCHAR));
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathUnquoteSpacesA(LPSTR path)
{
    if (!path || path[0] != '"')
        return FALSE;
    SIZE_T length = strlen(path);
    if (path[length - 1] != '"')
        return FALSE;
    path[length - 1] = 0;
    memmove(path, path + 1, length - 1);
    return TRUE;
}

LWSTDAPI void WINAPI PathRemoveBlanksW(LPWSTR path)
{
    if (!path)
        return;
    LPWSTR start = path;
    while (*start == L' ')
        start++;
    if (start != path)
        memmove(path, start, (wcslen(start) + 1) * sizeof(WCHAR));
    LPWSTR last = nullptr;
    for (LPWSTR p = path; *p; p++)
        if (*p != L' ')
            last = p;
    if (last)
        last[1] = 0;
}

LWSTDAPI void WINAPI PathRemoveBlanksA(LPSTR path)
{
    if (!path)
        return;
    LPSTR start = path;
    while (*start == ' ')
        start++;
    if (start != path)
        memmove(path, start, strlen(start) + 1);
    LPSTR last = nullptr;
    for (LPSTR p = path; *p; p = KbShNextA(p))
        if (*p != ' ')
            last = p;
    if (last)
        *KbShNextA(last) = 0;
}

LWSTDAPI void WINAPI PathStripPathW(LPWSTR path)
{
    if (!path)
        return;
    LPWSTR name = PathFindFileNameW(path);
    if (name != path)
        memmove(path, name, (wcslen(name) + 1) * sizeof(WCHAR));
}

LWSTDAPI void WINAPI PathStripPathA(LPSTR path)
{
    if (!path)
        return;
    LPSTR name = PathFindFileNameA(path);
    if (name != path)
        memmove(path, name, strlen(name) + 1);
}

/* "file,index": the index after the last comma when a digit follows it, read without skipping spaces. */
LWSTDAPI int WINAPI PathParseIconLocationW(LPWSTR location)
{
    if (!location)
        return 0;
    int index = 0;
    LPWSTR comma = wcsrchr(location, L',');
    if (comma) {
        LPWSTR p = comma + 1;
        while (*p == L' ' || *p == L'-')
            p++;
        if (*p >= L'0' && *p <= L'9') {
            *comma = 0;
            index = StrToIntW(comma + 1);
        }
    }
    PathUnquoteSpacesW(location);
    PathRemoveBlanksW(location);
    return index;
}

LWSTDAPI int WINAPI PathParseIconLocationA(LPSTR location)
{
    if (!location)
        return 0;
    int index = 0;
    LPSTR comma = strrchr(location, ',');
    if (comma) {
        LPSTR p = comma + 1;
        while (*p == ' ' || *p == '-')
            p++;
        if (*p >= '0' && *p <= '9') {
            *comma = 0;
            index = StrToIntA(comma + 1);
        }
    }
    PathUnquoteSpacesA(location);
    PathRemoveBlanksA(location);
    return index;
}

/* ------------------------------------------------------------------ wildcards */

static WCHAR upper_char(WCHAR c)
{
    WCHAR u;
    if (LCMapStringEx(nullptr, LCMAP_UPPERCASE, &c, 1, &u, 1, nullptr, nullptr, 0) != 1)
        return c;
    return u;
}

/* the whole name against one pattern: "*" and "?" match anything, "*.*" everything, "*.ext" an
   extension after any dot, leading spaces of the pattern skipped unless asked otherwise. */
static BOOL match_single(LPCWSTR name, LPCWSTR spec, DWORD flags)
{
    if (!name || !spec)
        return FALSE;
    if (!(flags & PMSF_DONT_STRIP_SPACES))
        while (*spec == L' ')
            spec++;
    for (;;) {
        if (!*name) {
            if (!*spec)
                return TRUE;
            if (*spec == L'*' || *spec == L'?') {
                if (!spec[1])
                    return TRUE;
                if (spec[1] == L'.' && (spec[2] == L'*' || spec[2] == L'?') && !spec[3])
                    return TRUE;
            }
            return FALSE;
        }
        if (!*spec)
            return FALSE;
        if (*spec == L'*')
            break;
        if (*spec != L'?' && upper_char(*spec) != upper_char(*name))
            return FALSE;
        spec++;
        name++;
    }
    if (!spec[1])
        return TRUE;
    if (spec[1] == L'.') {
        if (spec[2] == L'*' && !spec[3])
            return TRUE;
        while (*name) {
            WCHAR c = *name++;
            if (c == L'.' && match_single(name, spec + 2, flags | PMSF_DONT_STRIP_SPACES))
                return TRUE;
        }
        return FALSE;
    }
    for (; *name; name++)
        if (match_single(name, spec + 1, flags | PMSF_DONT_STRIP_SPACES))
            return TRUE;
    return FALSE;
}

/* every pattern of a ";" separated list, an empty list matching anything. */
static BOOL match_list(LPCWSTR name, LPCWSTR spec, DWORD flags)
{
    if (!name || !spec)
        return FALSE;
    if (!*spec)
        return TRUE;
    SIZE_T length = wcslen(spec);
    LPWSTR copy = KbAlloc((length + 1) * sizeof(WCHAR));
    if (!copy)
        return FALSE;
    memcpy(copy, spec, (length + 1) * sizeof(WCHAR));
    BOOL matched = FALSE;
    for (LPWSTR part = copy;;) {
        LPWSTR semicolon = wcschr(part, L';');
        if (semicolon)
            *semicolon = 0;
        matched = match_single(name, part, flags);
        if (matched || !semicolon)
            break;
        part = semicolon + 1;
    }
    KbFree(copy);
    return matched;
}

LWSTDAPI BOOL WINAPI PathMatchSpecW(LPCWSTR file, LPCWSTR spec)
{
    return match_list(file, spec, PMSF_NORMAL);
}

LWSTDAPI BOOL WINAPI PathMatchSpecA(LPCSTR file, LPCSTR spec)
{
    if (!file || !spec)
        return FALSE;
    PWSTR f = KbShAnsiToWide(file), s = KbShAnsiToWide(spec);
    BOOL result = f && s && PathMatchSpecW(f, s);
    KbFree(f);
    KbFree(s);
    return result;
}

/* S_OK on a match, S_FALSE otherwise; a list only with PMSF_MULTIPLE. */
LWSTDAPI HRESULT WINAPI PathMatchSpecExW(LPCWSTR file, LPCWSTR spec, DWORD flags)
{
    BOOL matched = (flags & PMSF_MULTIPLE) ? match_list(file, spec, flags) : match_single(file, spec, flags);
    return matched ? S_OK : S_FALSE;
}

LWSTDAPI HRESULT WINAPI PathMatchSpecExA(LPCSTR file, LPCSTR spec, DWORD flags)
{
    PWSTR f = KbShAnsiToWide(file), s = KbShAnsiToWide(spec);
    HRESULT hr = PathMatchSpecExW(f, s, flags);
    KbFree(f);
    KbFree(s);
    return hr;
}

/* ------------------------------------------------------------------ environment strings and searching */

/* the first variable whose value starts the path, without case and without a component boundary. */
static BOOL unexpand(LPCWSTR path, LPWSTR out, UINT size)
{
    static const PCWSTR names[] = {L"%APPDATA%",    L"%USERPROFILE%", L"%ALLUSERSPROFILE%", L"%ProgramFiles%",
                                   L"%SystemRoot%", L"%SystemDrive%", L"\\\\%COMPUTERNAME%"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++) {
        WCHAR value[MAX_PATH];
        DWORD n = ExpandEnvironmentStringsW(names[i], value, MAX_PATH);
        if (!n || n > MAX_PATH)
            continue;
        int length = (int)n - 1;
        if ((int)wcslen(path) < length ||
            CompareStringW(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, value, length, path, length) != CSTR_EQUAL)
            continue;
        SIZE_T rest = wcslen(path) - (SIZE_T)length;
        if (rest + wcslen(names[i]) >= size)
            continue;
        wcscpy(out, names[i]);
        wcscat(out, path + length);
        return TRUE;
    }
    return FALSE;
}

LWSTDAPI BOOL WINAPI PathUnExpandEnvStringsW(LPCWSTR path, LPWSTR out, UINT size)
{
    if (!out)
        return FALSE;
    if (!path) {
        if (size)
            *out = 0;
        return FALSE;
    }
    return unexpand(path, out, size);
}

LWSTDAPI BOOL WINAPI PathUnExpandEnvStringsA(LPCSTR path, LPSTR out, UINT size)
{
    if (!out)
        return FALSE;
    if (!path) {
        if (size)
            *out = 0;
        return FALSE;
    }
    PWSTR w = KbShAnsiToWide(path);
    WCHAR buffer[MAX_PATH * 2];
    BOOL result = w && unexpand(w, buffer, MAX_PATH * 2);
    KbFree(w);
    if (!result)
        return FALSE;
    int n = WideCharToMultiByte(CP_ACP, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0 || (UINT)n > size)
        return FALSE;
    WideCharToMultiByte(CP_ACP, 0, buffer, -1, out, (int)size, nullptr, nullptr);
    return TRUE;
}

/* the characters written with the terminator; a short buffer gets a terminated prefix and 0, and a
   string that fails to expand is copied as it is. */
LWSTDAPI DWORD WINAPI SHExpandEnvironmentStringsW(LPCWSTR source, LPWSTR out, DWORD size)
{
    if (!source)
        return 0;
    DWORD needed = ExpandEnvironmentStringsW(source, out, size);
    if (needed > size) {
        if (size)
            out[size - 1] = 0;
        return 0;
    }
    if (!needed)
        KbShCopy(out, size, source);
    return needed;
}

LWSTDAPI DWORD WINAPI SHExpandEnvironmentStringsA(LPCSTR source, LPSTR out, DWORD size)
{
    if (!source)
        return 0;
    DWORD needed = ExpandEnvironmentStringsA(source, out, size);
    if (needed > size) {
        if (size)
            out[size - 1] = 0;
        return 0;
    }
    if (!needed && size) {
        lstrcpynA(out, source, (int)size);
    }
    return needed;
}

/* a bare name is searched for first, anything else made a full path. */
LWSTDAPI BOOL WINAPI PathSearchAndQualifyW(LPCWSTR path, LPWSTR out, UINT size)
{
    if (!path || !size || !out)
        return FALSE;
    if (!wcschr(path, L':') && !wcschr(path, L'\\') && !wcschr(path, L'/')) {
        LPWSTR file;
        DWORD n = SearchPathW(nullptr, path, nullptr, size, out, &file);
        if (n && n < size)
            return TRUE;
    }
    LPWSTR file;
    DWORD n = GetFullPathNameW(path, size, out, &file);
    if (!n || n >= size) {
        *out = 0;
        return FALSE;
    }
    return TRUE;
}

LWSTDAPI BOOL WINAPI PathSearchAndQualifyA(LPCSTR path, LPSTR out, UINT size)
{
    if (!path || !size || !out)
        return FALSE;
    PWSTR w = KbShAnsiToWide(path);
    WCHAR buffer[MAX_PATH];
    BOOL result = w && PathSearchAndQualifyW(w, buffer, MAX_PATH) && KbShWideToAnsiBuffer(buffer, out, (int)size);
    KbFree(w);
    if (!result)
        *out = 0;
    return result;
}
