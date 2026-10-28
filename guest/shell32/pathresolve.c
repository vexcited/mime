/* PathResolve: finds a program or file from a bare name, a relative path or a quoted path. */
#include "shell32_private.h"
#include <mime/win32/shlwapi.h>

#define PRF_VERIFYEXISTS 0x0001
#define PRF_TRYPROGRAMEXTENSIONS 0x0003
#define PRF_FIRSTDIRDEF 0x0004
#define PRF_DONTFINDLNK 0x0008
#define PRF_REQUIREABSOLUTE 0x0010

/* the extensions to try, all of them or all but .lnk. */
#define RESOLVE_ALL_EXTENSIONS 0x3F
#define RESOLVE_NO_LNK 0x0F

static BOOL resolve_is_absolute(LPCWSTR path)
{
    return PathIsUNCW(path) || (PathGetDriveNumberW(path) != -1 && path[2] == L'\\');
}

/* a path without a drive takes the root of the directory, else the root of the windows directory. */
static void resolve_qualify(LPWSTR path, LPCWSTR directory, BOOL keep_dot)
{
    WCHAR source[MAX_PATH], root[MAX_PATH];
    if (wcslen(path) >= MAX_PATH)
        return;
    wcscpy(source, path);
    for (LPWSTR s = source; *s; s++)
        if (*s == L'/')
            *s = L'\\';
    LPCWSTR in = source;
    LPCWSTR server = nullptr;
    if (PathIsUNCEx(source, &server)) {
        in = server;
        SIZE_T prefix = (SIZE_T)(server - source);
        memcpy(path, source, prefix * sizeof(WCHAR));
        path[prefix] = 0;
    } else {
        int drive = PathGetDriveNumberW(source);
        if (drive != -1) {
            PathBuildRootW(root, drive);
            in = source + 2;
            if (*in == L'\\')
                in++;
        } else {
            if (!directory || wcslen(directory) >= MAX_PATH) {
                root[0] = 0;
                UINT n = GetWindowsDirectoryW(root, MAX_PATH);
                if (!n || n >= MAX_PATH)
                    return;
                PathStripToRootW(root);
            } else {
                wcscpy(root, directory);
            }
            if (source[0] == L'\\')
                PathStripToRootW(root);
        }
        wcscpy(path, root);
    }
    LPWSTR end = path + wcslen(path);
    LPWSTR limit = path + MAX_PATH;
    while (*in) {
        if (end >= limit)
            break;
        if (in[0] == L'.' && (in[1] == 0 || in[1] == L'\\')) {
            /* the current directory. */
        } else if (in[0] == L'.' && in[1] == L'.' && (in[2] == 0 || in[2] == L'\\')) {
            *end = 0;
            PathRemoveFileSpecW(path);
            end = path + wcslen(path);
        } else {
            if (end > path && end[-1] != L'\\')
                *end++ = L'\\';
            while (*in && *in != L'\\' && end < limit)
                *end++ = *in++;
        }
        while (*in && *in != L'\\')
            in++;
        if (*in)
            in++;
    }
    if (end >= limit)
        end = limit - 1;
    *end = 0;
    PathRemoveBackslashW(path);
    if (!keep_dot && path[0]) {
        SIZE_T length = wcslen(path);
        if (path[length - 1] == L'.')
            path[length - 1] = 0;
    }
}

static BOOL resolve_extensions(LPWSTR path, LPCWSTR *directories, BOOL search, DWORD which)
{
    if (*PathFindExtensionW(path))
        return FALSE;
    return search ? PathFindOnPathExW(path, directories, which) : PathFileExistsDefExtW(path, which);
}

static BOOL resolve_make_absolute(LPWSTR path)
{
    WCHAR current[MAX_PATH];
    DWORD n = GetCurrentDirectoryW(MAX_PATH, current);
    return n && n < MAX_PATH && SUCCEEDED(PathCchCombine(path, MAX_PATH, current, path));
}

SHSTDAPI BOOL WINAPI PathResolve(LPWSTR path, LPCWSTR *directories, UINT flags)
{
    DWORD which = flags & PRF_DONTFINDLNK ? RESOLVE_NO_LNK : RESOLVE_ALL_EXTENSIONS;
    BOOL verify = flags & PRF_VERIFYEXISTS;
    LPCWSTR first = (flags & PRF_FIRSTDIRDEF) && directories ? directories[0] : nullptr;
    if (verify)
        SetLastError(ERROR_FILE_NOT_FOUND);
    PathUnquoteSpacesW(path);
    if (PathIsRootW(path)) {
        if (!PathIsUNCServerW(path) && !PathIsUNCServerShareW(path) && path[0] == L'\\' && !path[1])
            resolve_qualify(path, first, FALSE);
        return !verify || PathFileExistsAndAttributesW(path, nullptr);
    }
    if (PathIsFileSpecW(path)) {
        if ((flags & PRF_TRYPROGRAMEXTENSIONS) && resolve_extensions(path, directories, TRUE, which))
            return TRUE;
        if (!PathFindOnPathW(path, directories))
            return FALSE;
        if (!(flags & PRF_REQUIREABSOLUTE) || resolve_is_absolute(path))
            return TRUE;
        return resolve_make_absolute(path) && PathFileExistsAndAttributesW(path, nullptr);
    }
    if (PathIsURLW(path))
        return FALSE;
    resolve_qualify(path, first, TRUE);
    if (!verify)
        return TRUE;
    BOOL found = ((flags & PRF_TRYPROGRAMEXTENSIONS) && resolve_extensions(path, directories, FALSE, which)) ||
                 PathFileExistsAndAttributesW(path, nullptr);
    if (found && (flags & PRF_REQUIREABSOLUTE) && !resolve_is_absolute(path))
        return resolve_make_absolute(path) && PathFileExistsAndAttributesW(path, nullptr);
    return found;
}
