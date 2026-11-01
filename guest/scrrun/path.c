/*
 * the rules the file system object has for the names of paths: the drive or share, the name, the folder, the extension, the
 * absolute and the canonical form with the case the file system has, and how two parts join.
 */
#include "scrrun_private.h"

BOOL PathIsSeparator(int c)
{
    return c == '\\' || c == '/';
}

BOOL PathIsUnc(const WCHAR *path)
{
    return PathIsSeparator(path[0]) && PathIsSeparator(path[1]);
}

BOOL PathIsDriveLetter(const WCHAR *path)
{
    return IsCharAlphaW(path[0]) && path[1] == ':';
}

/* "C:" or "\\server\share" without what follows, nothing for a path that has no drive. */
const WCHAR *PathDrivePart(const WCHAR *path, int *length)
{
    if (PathIsUnc(path)) {
        const WCHAR *server = path + 2, *end = server;
        while (*end && !PathIsSeparator(*end))
            end++;
        if (end == server || !*end)
            return nullptr;
        const WCHAR *share = end + 1, *share_end = share;
        while (*share_end && !PathIsSeparator(*share_end))
            share_end++;
        if (share_end == share)
            return nullptr;
        *length = (int)(share_end - path);
        return path;
    }
    if (PathIsDriveLetter(path)) {
        *length = 2;
        return path;
    }
    return nullptr;
}

/* the last name of a path, the part after the last separator that is not the end of the path. */
const WCHAR *PathNamePart(const WCHAR *path, int *length)
{
    int drive = 0;
    const WCHAR *p = path;
    if (PathDrivePart(path, &drive))
        p += drive;
    else if (PathIsUnc(path))
        return nullptr;
    if (!*p)
        return nullptr;
    BOOL first_separator = PathIsSeparator(*p);
    if (first_separator && !p[1])
        return nullptr;
    const WCHAR *start = nullptr, *last = nullptr;
    for (const WCHAR *q = p; *q; q++) {
        if (PathIsSeparator(*q)) {
            if (!PathIsSeparator(q[1]) && q[1])
                start = q + 1;
        } else {
            last = q;
        }
    }
    if (!start) {
        if (first_separator)
            return nullptr;
        start = p;
    }
    *length = (int)(last - start) + 1;
    return start;
}

/* the folder a path is in, which for a name right under a drive is the root of the drive. */
const WCHAR *PathParentPart(const WCHAR *path, int *length)
{
    int drive = 0;
    const WCHAR *p = path;
    BOOL has_drive = PathDrivePart(path, &drive) != nullptr;
    if (has_drive)
        p += drive;
    else if (PathIsUnc(path))
        return nullptr;
    if (!*p)
        return nullptr;
    const WCHAR *last = nullptr, *previous = nullptr;
    for (const WCHAR *q = p; *q; q++) {
        if (!PathIsSeparator(*q) && (!q[1] || PathIsSeparator(q[1]))) {
            previous = last;
            last = q;
        }
    }
    if (!last)
        return nullptr;
    const WCHAR *end = previous;
    if (!end) {
        if (p == path)
            return nullptr;
        if (!PathIsUnc(path) && PathIsSeparator(*p))
            end = p;
        else
            end = path + drive - 1;
    }
    *length = (int)(end - path) + 1;
    return path;
}

/* what follows the last dot of the name. */
const WCHAR *PathExtensionPart(const WCHAR *path, int *length)
{
    int name_length = 0;
    const WCHAR *name = PathNamePart(path, &name_length);
    if (!name || name_length <= 0)
        return nullptr;
    int dot = -1;
    for (int i = 0; i < name_length; i++) {
        if (name[i] == '.')
            dot = i + 1;
    }
    if (dot < 0)
        return nullptr;
    *length = name_length - dot;
    return name + dot;
}

BOOL PathIsRoot(const WCHAR *path)
{
    int drive = 0;
    if (!PathDrivePart(path, &drive))
        return FALSE;
    if (PathIsUnc(path) && !path[drive])
        return TRUE;
    return PathIsSeparator(path[drive]) && !path[drive + 1];
}

/* the flags of the first length characters of a text, all of them for a length of -1. */
DWORD PathScan(const WCHAR *text, int length)
{
    DWORD flags = 0;
    int count = 0;
    for (; text[count] && (length < 0 || count < length); count++) {
        WCHAR c = text[count];
        if (c < 0x20 || c == '"' || c == ':' || c == '<' || c == '>' || c == '|')
            flags |= SCAN_INVALID;
        else if (c == '*' || c == '?')
            flags |= SCAN_WILDCARD;
        else if (c == '/' || c == '\\')
            flags |= SCAN_SEPARATOR;
    }
    return count ? flags : flags | SCAN_EMPTY;
}

/* ------------------------------------------------------------------ building */

HRESULT PathBuild(const WCHAR *path, const WCHAR *name, WCHAR **out)
{
    SIZE_T path_length = wcslen(path), name_length = wcslen(name);
    SIZE_T total = path_length + name_length;
    BOOL separator = FALSE;
    if (path_length && name_length) {
        if (PathIsSeparator(path[path_length - 1])) {
            if (PathIsSeparator(name[0])) {
                name++;
                name_length--;
            }
        } else if (path_length != 2 || !PathIsDriveLetter(path)) {
            if (!PathIsSeparator(name[0]))
                separator = TRUE;
        }
    }
    (void)total;
    WCHAR *result = ScrAlloc((path_length + name_length + 2) * sizeof(WCHAR));
    if (!result)
        return E_OUTOFMEMORY;
    memcpy(result, path, path_length * sizeof(WCHAR));
    SIZE_T at = path_length;
    if (separator)
        result[at++] = '\\';
    memcpy(result + at, name, name_length * sizeof(WCHAR));
    result[at + name_length] = 0;
    *out = result;
    return S_OK;
}

/* ------------------------------------------------------------------ absolute and canonical */

HRESULT PathAbsolute(const WCHAR *path, BOOL short_form, WCHAR **out)
{
    *out = nullptr;
    if (wcslen(path) > 0x1000)
        return E_INVALIDARG;
    DWORD size = MAX_PATH;
    WCHAR *buffer = nullptr;
    for (;;) {
        buffer = ScrAlloc(size * sizeof(WCHAR));
        if (!buffer)
            return E_OUTOFMEMORY;
        WCHAR *file_part = nullptr;
        DWORD written = short_form ? GetShortPathNameW(path, buffer, size) : GetFullPathNameW(path, size, buffer, &file_part);
        if (!written) {
            ScrFree(buffer);
            return E_INVALIDARG;
        }
        if (written < size)
            break;
        ScrFree(buffer);
        size = written + 1;
    }
    SIZE_T length = wcslen(buffer);
    if (length && PathIsSeparator(buffer[length - 1]) && (PathIsUnc(buffer) || !PathIsRoot(buffer)))
        buffer[length - 1] = 0;
    *out = buffer;
    return S_OK;
}

/* the name of the file system for one component of a path, in the case it has or in its short form. */
static BOOL component_name(WCHAR *path, SIZE_T start, SIZE_T length, BOOL short_form)
{
    WIN32_FIND_DATAW find;
    memset(&find, 0, sizeof(find));
    HANDLE handle = FindFirstFileW(path, &find);
    if (handle == INVALID_HANDLE_VALUE)
        return FALSE;
    FindClose(handle);
    const WCHAR *name = short_form ? find.cAlternateFileName : find.cFileName;
    if (wcslen(name) != length)
        return FALSE;
    memcpy(path + start, name, length * sizeof(WCHAR));
    return TRUE;
}

void PathMakeCanonical(WCHAR *path, SIZE_T capacity, BOOL short_form)
{
    int drive = 0;
    if (!PathDrivePart(path, &drive))
        return;
    if (PathIsUnc(path)) {
        for (int i = 0; i < drive; i++)
            path[i] = towlower(path[i]);
        if (!path[drive])
            return;
    } else {
        path[0] = towupper(path[0]);
    }
    SIZE_T start = (SIZE_T)drive + 1;
    SIZE_T i = start;
    while (path[i]) {
        if (path[i] == '\\') {
            path[i] = 0;
            BOOL found = component_name(path, start, i - start, short_form);
            path[i] = '\\';
            if (!found)
                return;
            start = i + 1;
        }
        i++;
    }
    component_name(path, start, i - start, short_form);
}

HRESULT PathCanonical(const WCHAR *path, WCHAR **out)
{
    HRESULT hr = PathAbsolute(path, FALSE, out);
    if (SUCCEEDED(hr))
        PathMakeCanonical(*out, wcslen(*out) + 1, FALSE);
    return hr;
}

HRESULT PathCanonicalShort(const WCHAR *path, WCHAR **out)
{
    HRESULT hr = PathAbsolute(path, TRUE, out);
    if (SUCCEEDED(hr))
        PathMakeCanonical(*out, wcslen(*out) + 1, TRUE);
    return hr;
}

/* ------------------------------------------------------------------ what exists */

DWORD PathAttributes(const WCHAR *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_SHARING_VIOLATION) {
        WIN32_FIND_DATAW find;
        memset(&find, 0, sizeof(find));
        HANDLE handle = FindFirstFileW(path, &find);
        if (handle != INVALID_HANDLE_VALUE) {
            FindClose(handle);
            return find.dwFileAttributes;
        }
    }
    return attributes;
}

HRESULT PathFileExists(const WCHAR *path)
{
    DWORD attributes = PathAttributes(path);
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ? E_VB_FILENOTFOUND : S_OK;
}

HRESULT PathFolderExists(const WCHAR *path)
{
    DWORD attributes = PathAttributes(path);
    return attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ? E_VB_PATHNOTFOUND : S_OK;
}

HRESULT PathDriveExists(const WCHAR *path)
{
    if (PathIsUnc(path))
        return PathFolderExists(path);
    return GetDriveTypeW(path) == DRIVE_NO_ROOT_DIR ? VBERR(68) : S_OK;
}

/* "C" "C:" and "C:\" mean the drive C:\, a share keeps its server and name. */
HRESULT PathCanonicalDrive(const WCHAR *path, WCHAR **out)
{
    *out = nullptr;
    if (PathIsUnc(path)) {
        const WCHAR *p = path + 2;
        while (*p && !PathIsSeparator(*p))
            p++;
        if (!*p || p == path + 2)
            return E_INVALIDARG;
        const WCHAR *share = p + 1, *end = share;
        while (*end && !PathIsSeparator(*end))
            end++;
        if (end == share || (end[0] && end[1]))
            return E_INVALIDARG;
        SIZE_T length = (SIZE_T)(end - path);
        WCHAR *result = ScrAlloc((length + 2) * sizeof(WCHAR));
        if (!result)
            return E_OUTOFMEMORY;
        memcpy(result, path, length * sizeof(WCHAR));
        result[length] = '\\';
        *out = result;
        return S_OK;
    }
    SIZE_T length = wcslen(path);
    if (length - 1 > 2)
        return E_INVALIDARG;
    WCHAR letter = towupper(path[0]);
    if (letter < 'A' || letter > 'Z')
        return E_INVALIDARG;
    if (length > 1 && path[1] != ':')
        return E_INVALIDARG;
    if (length > 2 && !PathIsSeparator(path[2]))
        return E_INVALIDARG;
    WCHAR *result = ScrAlloc(4 * sizeof(WCHAR));
    if (!result)
        return E_OUTOFMEMORY;
    result[0] = letter;
    result[1] = ':';
    result[2] = '\\';
    *out = result;
    return S_OK;
}
