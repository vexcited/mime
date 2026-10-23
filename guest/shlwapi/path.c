/*
 * the path functions shlwapi implements itself: searching, compacting, folders and environment expansion.
 * the rest live in kernelbase.
 */
#include "shlwapi_private.h"

/* ------------------------------------------------------------------ existence */

BOOL WINAPI PathFileExistsAndAttributesW(LPCWSTR path, DWORD *attributes)
{
    if (attributes)
        *attributes = INVALID_FILE_ATTRIBUTES;
    if (!path)
        return FALSE;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    DWORD found = GetFileAttributesW(path);
    SetErrorMode(mode);
    if (attributes)
        *attributes = found;
    return found != INVALID_FILE_ATTRIBUTES;
}

BOOL WINAPI PathFileExistsAndAttributesA(LPCSTR path, DWORD *attributes)
{
    if (attributes)
        *attributes = INVALID_FILE_ATTRIBUTES;
    LPWSTR w = ShlWide(path);
    BOOL result = w && PathFileExistsAndAttributesW(w, attributes);
    ShlFree(w);
    return result;
}

/* the extensions tried in WHICH_* order, the bare name last for WHICH_OPTIONAL. */
static const struct {
    DWORD which;
    PCWSTR extension;
} which_extensions[] = {{WHICH_PIF, L".pif"}, {WHICH_COM, L".com"}, {WHICH_EXE, L".exe"},
                        {WHICH_BAT, L".bat"}, {WHICH_LNK, L".lnk"}, {WHICH_CMD, L".cmd"}};

BOOL WINAPI PathFileExistsDefExtAndAttributesW(LPWSTR path, DWORD which, DWORD *attributes);
BOOL WINAPI PathFileExistsDefExtAndAttributesW(LPWSTR path, DWORD which, DWORD *attributes)
{
    if (attributes)
        *attributes = INVALID_FILE_ATTRIBUTES;
    if (!path)
        return FALSE;
    if (!which || *PathFindExtensionW(path))
        return PathFileExistsAndAttributesW(path, attributes);
    SIZE_T length = wcslen(path);
    if (length + 5 > MAX_PATH)
        return FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(which_extensions); i++) {
        if (!(which & which_extensions[i].which))
            continue;
        wcscpy(path + length, which_extensions[i].extension);
        if (PathFileExistsAndAttributesW(path, attributes))
            return TRUE;
    }
    path[length] = 0;
    if (which & WHICH_OPTIONAL)
        return PathFileExistsAndAttributesW(path, attributes);
    return FALSE;
}

BOOL WINAPI PathFileExistsDefExtW(LPWSTR path, DWORD which)
{
    return PathFileExistsDefExtAndAttributesW(path, which, nullptr);
}

BOOL WINAPI PathFileExistsDefExtA(LPSTR path, DWORD which)
{
    WCHAR wide[MAX_PATH];
    if (!path || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH))
        return FALSE;
    if (!PathFileExistsDefExtW(wide, which))
        return FALSE;
    WideCharToMultiByte(CP_ACP, 0, wide, -1, path, MAX_PATH, nullptr, nullptr);
    return TRUE;
}

/* ------------------------------------------------------------------ searching */

/* copies the next element of a ";" list, unquoted and trimmed, and returns where the one after starts,
   or NULL at the end. */
LPCWSTR WINAPI NextPathW(LPCWSTR list, LPWSTR out, int size);
LPCWSTR WINAPI NextPathW(LPCWSTR list, LPWSTR out, int size)
{
    if (!list || !out || size <= 0)
        return nullptr;
    while (*list == L';' || *list == L' ')
        list++;
    if (!*list)
        return nullptr;
    int n = 0;
    BOOL quoted = FALSE;
    for (; *list && (quoted || *list != L';'); list++) {
        if (*list == L'"') {
            quoted = !quoted;
            continue;
        }
        if (n < size - 1)
            out[n++] = *list;
    }
    while (n && out[n - 1] == L' ')
        n--;
    out[n] = 0;
    return list;
}

LPCSTR WINAPI NextPathA(LPCSTR list, LPSTR out, int size);
LPCSTR WINAPI NextPathA(LPCSTR list, LPSTR out, int size)
{
    if (!list || !out || size <= 0)
        return nullptr;
    while (*list == ';' || *list == ' ')
        list++;
    if (!*list)
        return nullptr;
    int n = 0;
    BOOL quoted = FALSE;
    for (; *list && (quoted || *list != ';'); list++) {
        if (*list == '"') {
            quoted = !quoted;
            continue;
        }
        if (n < size - 1)
            out[n++] = *list;
    }
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = 0;
    return list;
}

static BOOL try_directory(LPWSTR file, LPCWSTR directory, DWORD which)
{
    WCHAR candidate[MAX_PATH];
    if (!PathCombineW(candidate, directory, file))
        return FALSE;
    if (!PathFileExistsDefExtW(candidate, which))
        return FALSE;
    wcscpy(file, candidate);
    return TRUE;
}

/* the given directories, then the system and windows directories, then PATH. */
BOOL WINAPI PathFindOnPathExW(LPWSTR file, LPCWSTR *directories, DWORD which)
{
    if (!file || !*file || !PathIsFileSpecW(file))
        return FALSE;
    which |= WHICH_OPTIONAL;
    for (LPCWSTR *d = directories; d && *d; d++)
        if (**d && try_directory(file, *d, which))
            return TRUE;
    WCHAR directory[MAX_PATH];
    if (GetSystemDirectoryW(directory, MAX_PATH) && try_directory(file, directory, which))
        return TRUE;
    if (GetWindowsDirectoryW(directory, MAX_PATH) && try_directory(file, directory, which))
        return TRUE;
    DWORD length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (!length)
        return FALSE;
    LPWSTR path = ShlAlloc((SIZE_T)length * sizeof(WCHAR));
    if (!path)
        return FALSE;
    GetEnvironmentVariableW(L"PATH", path, length);
    BOOL found = FALSE;
    for (LPCWSTR next = path; !found && (next = NextPathW(next, directory, MAX_PATH));)
        if (*directory)
            found = try_directory(file, directory, which);
    ShlFree(path);
    return found;
}

BOOL WINAPI PathFindOnPathW(LPWSTR file, LPCWSTR *directories)
{
    return PathFindOnPathExW(file, directories, 0);
}

BOOL WINAPI PathFindOnPathExA(LPSTR file, LPCSTR *directories, DWORD which)
{
    WCHAR wide[MAX_PATH];
    if (!file || !MultiByteToWideChar(CP_ACP, 0, file, -1, wide, MAX_PATH))
        return FALSE;
    LPWSTR *dirs = nullptr;
    int count = 0;
    while (directories && directories[count])
        count++;
    if (count) {
        dirs = ShlAlloc(((SIZE_T)count + 1) * sizeof(LPWSTR));
        if (!dirs)
            return FALSE;
        for (int i = 0; i < count; i++)
            dirs[i] = ShlWide(directories[i]);
        dirs[count] = nullptr;
    }
    BOOL found = PathFindOnPathExW(wide, (LPCWSTR *)dirs, which);
    if (found)
        WideCharToMultiByte(CP_ACP, 0, wide, -1, file, MAX_PATH, nullptr, nullptr);
    for (int i = 0; i < count; i++)
        ShlFree(dirs[i]);
    ShlFree(dirs);
    return found;
}

BOOL WINAPI PathFindOnPathA(LPSTR file, LPCSTR *directories)
{
    return PathFindOnPathExA(file, directories, 0);
}

/* the first suffix the path ends with, compared case sensitively. */
LPCWSTR WINAPI PathFindSuffixArrayW(LPCWSTR path, const LPCWSTR *suffixes, int count)
{
    if (!path || !suffixes || count <= 0)
        return nullptr;
    SIZE_T length = wcslen(path);
    for (int i = 0; i < count; i++) {
        SIZE_T n = suffixes[i] ? wcslen(suffixes[i]) : 0;
        if (n && n <= length && !wcscmp(path + length - n, suffixes[i]))
            return suffixes[i];
    }
    return nullptr;
}

LPCSTR WINAPI PathFindSuffixArrayA(LPCSTR path, const LPCSTR *suffixes, int count)
{
    if (!path || !suffixes || count <= 0)
        return nullptr;
    SIZE_T length = strlen(path);
    for (int i = 0; i < count; i++) {
        SIZE_T n = suffixes[i] ? strlen(suffixes[i]) : 0;
        if (n && n <= length && !strcmp(path + length - n, suffixes[i]))
            return suffixes[i];
    }
    return nullptr;
}

/* ------------------------------------------------------------------ small edits */

LPCSTR WINAPI PathSkipLeadingSlashesA(LPCSTR path);
LPCSTR WINAPI PathSkipLeadingSlashesA(LPCSTR path)
{
    if (path && (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
        path += 2;
    return path;
}

/* slashes become backslashes. */
void WINAPI FixSlashesAndColonW(LPWSTR path);
void WINAPI FixSlashesAndColonW(LPWSTR path)
{
    for (; path && *path; path++)
        if (*path == L'/')
            *path = L'\\';
}

void WINAPI FixSlashesAndColonA(LPSTR path);
void WINAPI FixSlashesAndColonA(LPSTR path)
{
    for (; path && *path; path++)
        if (*path == '/')
            *path = '\\';
}

LPWSTR WINAPI PathBuildRootW(LPWSTR root, int drive)
{
    if (root && drive >= 0 && drive < 26) {
        root[0] = (WCHAR)(L'A' + drive);
        root[1] = L':';
        root[2] = L'\\';
        root[3] = 0;
    }
    return root;
}

LPSTR WINAPI PathBuildRootA(LPSTR root, int drive)
{
    if (root && drive >= 0 && drive < 26) {
        root[0] = (char)('A' + drive);
        root[1] = ':';
        root[2] = '\\';
        root[3] = 0;
    }
    return root;
}

/* drops the arguments after the program, and the space before them. */
void WINAPI PathRemoveArgsW(LPWSTR path)
{
    if (!path)
        return;
    LPWSTR args = PathGetArgsW(path);
    if (*args) {
        LPWSTR end = args;
        if (end > path && end[-1] == L' ')
            end--;
        *end = 0;
    } else {
        SIZE_T length = wcslen(path);
        if (length && path[length - 1] == L' ')
            path[length - 1] = 0;
    }
}

void WINAPI PathRemoveArgsA(LPSTR path)
{
    if (!path)
        return;
    LPSTR args = PathGetArgsA(path);
    if (*args) {
        LPSTR end = args;
        if (end > path && end[-1] == ' ')
            end--;
        *end = 0;
    } else {
        SIZE_T length = strlen(path);
        if (length && path[length - 1] == ' ')
            path[length - 1] = 0;
    }
}

/* "name[12].ext" back to "name.ext", undoing the download cache's decoration. */
void WINAPI PathUndecorateW(LPWSTR path)
{
    if (!path)
        return;
    LPWSTR extension = PathFindExtensionW(path), p = extension;
    if (p <= path || p[-1] != L']')
        return;
    p--;
    LPWSTR close = p;
    while (p > path && p[-1] >= L'0' && p[-1] <= L'9')
        p--;
    if (p == close || p <= path || p[-1] != L'[')
        return;
    p--;
    if (p > path && (p[-1] == L'\\' || p[-1] == L'/'))
        return;
    memmove(p, extension, (wcslen(extension) + 1) * sizeof(WCHAR));
}

void WINAPI PathUndecorateA(LPSTR path)
{
    if (!path)
        return;
    LPSTR extension = PathFindExtensionA(path), p = extension;
    if (p <= path || p[-1] != ']')
        return;
    p--;
    LPSTR close = p;
    while (p > path && p[-1] >= '0' && p[-1] <= '9')
        p--;
    if (p == close || p <= path || p[-1] != '[')
        return;
    p--;
    if (p > path && (p[-1] == '\\' || p[-1] == '/'))
        return;
    memmove(p, extension, strlen(extension) + 1);
}

/* a path all in capitals goes to lower case with a capital first letter. */
BOOL WINAPI PathMakePrettyW(LPWSTR path)
{
    if (!path || !*path)
        return FALSE;
    for (LPWSTR p = path; *p; p++)
        if (IsCharLowerW(*p))
            return FALSE;
    CharLowerW(path);
    LPWSTR first = path;
    while (*first && !IsCharAlphaW(*first))
        first++;
    if (*first)
        *first = (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)*first);
    return TRUE;
}

BOOL WINAPI PathMakePrettyA(LPSTR path)
{
    WCHAR wide[MAX_PATH];
    if (!path || !*path || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH))
        return FALSE;
    if (!PathMakePrettyW(wide))
        return FALSE;
    WideCharToMultiByte(CP_ACP, 0, wide, -1, path, (int)strlen(path) + 1, nullptr, nullptr);
    return TRUE;
}

/* ------------------------------------------------------------------ folders */

/* returns FILE_ATTRIBUTE_DIRECTORY itself, not just TRUE. */
BOOL WINAPI PathIsDirectoryW(LPCWSTR path)
{
    if (!path)
        return FALSE;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    DWORD attributes = GetFileAttributesW(path);
    SetErrorMode(mode);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    return attributes & FILE_ATTRIBUTE_DIRECTORY;
}

BOOL WINAPI PathIsDirectoryA(LPCSTR path)
{
    LPWSTR w = ShlWide(path);
    BOOL result = w ? PathIsDirectoryW(w) : FALSE;
    ShlFree(w);
    return result;
}

BOOL WINAPI PathIsDirectoryEmptyW(LPCWSTR path)
{
    WCHAR pattern[MAX_PATH];
    if (!path || !PathCombineW(pattern, path, L"*.*"))
        return FALSE;
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

BOOL WINAPI PathIsDirectoryEmptyA(LPCSTR path)
{
    LPWSTR w = ShlWide(path);
    BOOL result = w && PathIsDirectoryEmptyW(w);
    ShlFree(w);
    return result;
}

/* a directory marked read only or system, the way desktop.ini folders are. */
BOOL WINAPI PathIsSystemFolderW(LPCWSTR path, DWORD attributes)
{
    if (path && *path)
        attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;
    return (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

BOOL WINAPI PathIsSystemFolderA(LPCSTR path, DWORD attributes)
{
    LPWSTR w = ShlWide(path);
    BOOL result = PathIsSystemFolderW(w, attributes);
    ShlFree(w);
    return result;
}

BOOL WINAPI PathMakeSystemFolderW(LPCWSTR path)
{
    if (!path || !*path)
        return FALSE;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;
    attributes &= ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY);
    return SetFileAttributesW(path, attributes | FILE_ATTRIBUTE_READONLY);
}

BOOL WINAPI PathMakeSystemFolderA(LPCSTR path)
{
    LPWSTR w = ShlWide(path);
    BOOL result = w && PathMakeSystemFolderW(w);
    ShlFree(w);
    return result;
}

BOOL WINAPI PathUnmakeSystemFolderW(LPCWSTR path)
{
    if (!path || !*path)
        return FALSE;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;
    attributes &= ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY);
    return SetFileAttributesW(path, attributes ? attributes : FILE_ATTRIBUTE_NORMAL);
}

BOOL WINAPI PathUnmakeSystemFolderA(LPCSTR path)
{
    LPWSTR w = ShlWide(path);
    BOOL result = w && PathUnmakeSystemFolderW(w);
    ShlFree(w);
    return result;
}

BOOL WINAPI PathIsNetworkPathW(LPCWSTR path)
{
    if (!path)
        return FALSE;
    if (PathIsUNCW(path))
        return TRUE;
    int drive = PathGetDriveNumberW(path);
    if (drive < 0)
        return FALSE;
    WCHAR root[4];
    return GetDriveTypeW(PathBuildRootW(root, drive)) == DRIVE_REMOTE;
}

BOOL WINAPI PathIsNetworkPathA(LPCSTR path)
{
    LPWSTR w = ShlWide(path);
    BOOL result = w && PathIsNetworkPathW(w);
    ShlFree(w);
    return result;
}

/* compares the content type of the extension's association, ignoring case. */
BOOL WINAPI PathIsContentTypeW(LPCWSTR path, LPCWSTR type)
{
    if (!path || !type)
        return FALSE;
    LPCWSTR extension = PathFindExtensionW(path);
    if (!*extension)
        return FALSE;
    WCHAR found[MAX_PATH];
    DWORD size = MAX_PATH;
    if (FAILED(AssocQueryStringW(0, ASSOCSTR_CONTENTTYPE, extension, nullptr, found, &size)))
        return FALSE;
    return !lstrcmpiW(found, type);
}

BOOL WINAPI PathIsContentTypeA(LPCSTR path, LPCSTR type)
{
    LPWSTR p = ShlWide(path), t = ShlWide(type);
    BOOL result = p && t && PathIsContentTypeW(p, t);
    ShlFree(p);
    ShlFree(t);
    return result;
}

/* ------------------------------------------------------------------ compacting */

/* max counts the terminator. "..." replaces what comes before the last component, and the component
   itself is cut when it alone is too long. */
BOOL WINAPI PathCompactPathExW(LPWSTR out, LPCWSTR path, UINT max, DWORD flags)
{
    if (!out || !path || !max)
        return FALSE;
    SIZE_T length = wcslen(path);
    if (length + 1 <= max) {
        wcscpy(out, path);
        return TRUE;
    }
    LPCWSTR file = PathFindFileNameW(path);
    WCHAR separator = L'\\';
    if (file > path && (file[-1] == L'\\' || file[-1] == L'/')) {
        separator = file[-1];
    } else {
        LPCWSTR slash = wcschr(path, L'/');
        if (slash && !wcschr(path, L'\\'))
            separator = L'/';
    }
    SIZE_T file_length = wcslen(file), room = max - 1;
    if (room <= 3) {
        for (SIZE_T i = 0; i < room; i++)
            out[i] = L'.';
        out[room] = 0;
        return TRUE;
    }
    if (file_length + 4 > room) {
        /* only part of the last component fits. */
        SIZE_T keep = room - 3;
        memcpy(out, file, keep * sizeof(WCHAR));
        wcscpy(out + keep, L"...");
        return TRUE;
    }
    SIZE_T prefix = room - 4 - file_length;
    memcpy(out, path, prefix * sizeof(WCHAR));
    out[prefix] = L'.';
    out[prefix + 1] = L'.';
    out[prefix + 2] = L'.';
    out[prefix + 3] = separator;
    wcscpy(out + prefix + 4, file);
    return TRUE;
}

BOOL WINAPI PathCompactPathExA(LPSTR out, LPCSTR path, UINT max, DWORD flags)
{
    if (!out || !path || !max)
        return FALSE;
    WCHAR wide[MAX_PATH * 4], result[MAX_PATH * 4];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wide, RTL_NUMBER_OF(wide)))
        return FALSE;
    if (max > RTL_NUMBER_OF(result))
        max = RTL_NUMBER_OF(result);
    if (!PathCompactPathExW(result, wide, max, flags))
        return FALSE;
    SHUnicodeToAnsiCP(CP_ACP, result, out, (int)max);
    return TRUE;
}

static UINT text_width(HDC dc, LPCWSTR text, int length)
{
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc, text, length, &size);
    return (UINT)size.cx;
}

/* shortens in place until it fits width pixels in the DC's font. */
BOOL WINAPI PathCompactPathW(HDC dc, LPWSTR path, UINT width)
{
    if (!path)
        return FALSE;
    HDC own = dc ? nullptr : GetDC(nullptr);
    if (own)
        dc = own;
    BOOL result = TRUE;
    int length = (int)wcslen(path);
    if (text_width(dc, path, length) > width) {
        LPWSTR file = PathFindFileNameW(path);
        int file_length = (int)wcslen(file);
        WCHAR buffer[MAX_PATH * 2];
        int prefix = (int)(file - path);
        if (prefix > 0)
            prefix--;
        BOOL done = FALSE;
        for (; prefix >= 0 && !done; prefix--) {
            if (prefix + 4 + file_length >= (int)RTL_NUMBER_OF(buffer))
                continue;
            memcpy(buffer, path, (SIZE_T)prefix * sizeof(WCHAR));
            wcscpy(buffer + prefix, L"...\\");
            wcscpy(buffer + prefix + 4, file);
            if (text_width(dc, buffer, (int)wcslen(buffer)) <= width) {
                wcscpy(path, buffer);
                done = TRUE;
            }
        }
        if (!done) {
            /* just the name with dots, as much of the name as fits. */
            int keep = file_length;
            for (; keep >= 0; keep--) {
                memcpy(buffer, file, (SIZE_T)keep * sizeof(WCHAR));
                wcscpy(buffer + keep, L"...");
                if (text_width(dc, buffer, keep + 3) <= width)
                    break;
            }
            if (keep < 0) {
                buffer[0] = 0;
                result = FALSE;
            }
            wcscpy(path, buffer);
        }
    }
    if (own)
        ReleaseDC(nullptr, own);
    return result;
}

BOOL WINAPI PathCompactPathA(HDC dc, LPSTR path, UINT width)
{
    WCHAR wide[MAX_PATH * 2];
    if (!path || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, RTL_NUMBER_OF(wide)))
        return FALSE;
    BOOL result = PathCompactPathW(dc, wide, width);
    WideCharToMultiByte(CP_ACP, 0, wide, -1, path, MAX_PATH, nullptr, nullptr);
    return result;
}

void WINAPI PathSetDlgItemPathW(HWND dialog, int id, LPCWSTR path)
{
    HWND item = GetDlgItem(dialog, id);
    if (!item)
        return;
    WCHAR text[MAX_PATH * 2];
    lstrcpynW(text, path ? path : L"", RTL_NUMBER_OF(text));
    RECT rect;
    GetClientRect(item, &rect);
    HDC dc = GetDC(item);
    HFONT font = (HFONT)SendMessageW(item, WM_GETFONT, 0, 0);
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    PathCompactPathW(dc, text, (UINT)(rect.right - rect.left));
    if (old)
        SelectObject(dc, old);
    ReleaseDC(item, dc);
    SetWindowTextW(item, text);
}

void WINAPI PathSetDlgItemPathA(HWND dialog, int id, LPCSTR path)
{
    LPWSTR w = ShlWide(path);
    PathSetDlgItemPathW(dialog, id, w);
    ShlFree(w);
}

/* ------------------------------------------------------------------ environment */

DWORD WINAPI SHExpandEnvironmentStringsForUserW(HANDLE token, LPCWSTR source, LPWSTR out, DWORD size);
DWORD WINAPI SHExpandEnvironmentStringsForUserW(HANDLE token, LPCWSTR source, LPWSTR out, DWORD size)
{
    if (token) {
        BOOL(WINAPI * expand)(HANDLE, LPCWSTR, LPWSTR, DWORD) =
            (BOOL(WINAPI *)(HANDLE, LPCWSTR, LPWSTR, DWORD))ShlProc(L"userenv.dll", "ExpandEnvironmentStringsForUserW");
        if (expand)
            return expand(token, source, out, size) ? (DWORD)wcslen(out) + 1 : 0;
    }
    return ExpandEnvironmentStringsW(source, out, size);
}

DWORD WINAPI SHExpandEnvironmentStringsForUserA(HANDLE token, LPCSTR source, LPSTR out, DWORD size);
DWORD WINAPI SHExpandEnvironmentStringsForUserA(HANDLE token, LPCSTR source, LPSTR out, DWORD size)
{
    if (!token)
        return ExpandEnvironmentStringsA(source, out, size);
    LPWSTR w = ShlWide(source);
    LPWSTR result = ShlAlloc((SIZE_T)(size ? size : 1) * sizeof(WCHAR));
    DWORD n = 0;
    if (w && result) {
        n = SHExpandEnvironmentStringsForUserW(token, w, result, size);
        if (n && n <= size)
            SHUnicodeToAnsiCP(CP_ACP, result, out, (int)size);
    }
    ShlFree(w);
    ShlFree(result);
    return n;
}

/* the caller frees the result with CoTaskMemFree. */
HRESULT WINAPI SHExpandEnvironmentStringsAlloc(LPCWSTR source, LPWSTR *out);
HRESULT WINAPI SHExpandEnvironmentStringsAlloc(LPCWSTR source, LPWSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!source)
        return E_INVALIDARG;
    DWORD n = ExpandEnvironmentStringsW(source, nullptr, 0);
    if (!n)
        return HRESULT_FROM_WIN32(GetLastError());
    *out = ShlTaskAlloc((SIZE_T)n * sizeof(WCHAR));
    if (!*out)
        return E_OUTOFMEMORY;
    ExpandEnvironmentStringsW(source, *out, n);
    return S_OK;
}

/* another user's profile is out of reach, so the current one's variables are used. */
BOOL WINAPI PathUnExpandEnvStringsForUserW(HANDLE token, LPCWSTR path, LPWSTR out, UINT size);
BOOL WINAPI PathUnExpandEnvStringsForUserW(HANDLE token, LPCWSTR path, LPWSTR out, UINT size)
{
    return PathUnExpandEnvStringsW(path, out, size);
}

BOOL WINAPI PathUnExpandEnvStringsForUserA(HANDLE token, LPCSTR path, LPSTR out, UINT size);
BOOL WINAPI PathUnExpandEnvStringsForUserA(HANDLE token, LPCSTR path, LPSTR out, UINT size)
{
    return PathUnExpandEnvStringsA(path, out, size);
}

BOOL WINAPI PathUnExpandSystemEnvStringsW(LPCWSTR path, LPWSTR out, UINT size);
BOOL WINAPI PathUnExpandSystemEnvStringsW(LPCWSTR path, LPWSTR out, UINT size)
{
    return PathUnExpandEnvStringsW(path, out, size);
}
