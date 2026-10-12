/*
 * SHFileOperation: copying, moving, renaming and deleting lists of files.
 *
 * no progress or confirmation dialog is shown. an existing target is only
 * replaced with FOF_NOCONFIRMATION, as answering yes would; without it the
 * operation stops with ERROR_ALREADY_EXISTS. there is no recycle bin, so
 * FOF_ALLOWUNDO deletes permanently, like windows does on a drive without
 * one. failures return the win32 error rather than the legacy DE_ codes.
 */
#include "shell32_private.h"

#define LONG_PATH (MAX_PATH * 4)

static BOOL has_wildcards(LPCWSTR path)
{
    LPCWSTR name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return wcschr(name, L'*') || wcschr(name, L'?');
}

static BOOL is_dots(LPCWSTR name)
{
    return !wcscmp(name, L".") || !wcscmp(name, L"..");
}

static BOOL join(LPWSTR out, LPCWSTR directory, LPCWSTR name)
{
    SIZE_T length = wcslen(directory);
    if (length + wcslen(name) + 2 > LONG_PATH)
        return FALSE;
    wcscpy(out, directory);
    if (length && out[length - 1] != L'\\')
        out[length++] = L'\\';
    wcscpy(out + length, name);
    return TRUE;
}

static LPCWSTR last_name(LPCWSTR path)
{
    LPCWSTR slash = wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

static DWORD delete_tree(LPCWSTR path, BOOL recurse)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(path, attributes & ~FILE_ATTRIBUTE_READONLY);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(path) ? ERROR_SUCCESS : GetLastError();
    if (recurse) {
        WCHAR pattern[LONG_PATH], child[LONG_PATH];
        WIN32_FIND_DATAW data;
        if (!join(pattern, path, L"*"))
            return ERROR_FILENAME_EXCED_RANGE;
        HANDLE find = FindFirstFileW(pattern, &data);
        if (find != INVALID_HANDLE_VALUE) {
            DWORD error = ERROR_SUCCESS;
            do {
                if (is_dots(data.cFileName))
                    continue;
                error = join(child, path, data.cFileName) ? delete_tree(child, TRUE) : ERROR_FILENAME_EXCED_RANGE;
            } while (!error && FindNextFileW(find, &data));
            FindClose(find);
            if (error)
                return error;
        }
    }
    return RemoveDirectoryW(path) ? ERROR_SUCCESS : GetLastError();
}

static DWORD copy_tree(LPCWSTR from, LPCWSTR to, BOOL replace, BOOL recurse)
{
    DWORD attributes = GetFileAttributesW(from);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return CopyFileW(from, to, !replace) ? ERROR_SUCCESS : GetLastError();
    if (!CreateDirectoryW(to, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return GetLastError();
    if (!recurse)
        return ERROR_SUCCESS;
    WCHAR pattern[LONG_PATH], source[LONG_PATH], target[LONG_PATH];
    WIN32_FIND_DATAW data;
    if (!join(pattern, from, L"*"))
        return ERROR_FILENAME_EXCED_RANGE;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return ERROR_SUCCESS;
    DWORD error = ERROR_SUCCESS;
    do {
        if (is_dots(data.cFileName))
            continue;
        if (!join(source, from, data.cFileName) || !join(target, to, data.cFileName))
            error = ERROR_FILENAME_EXCED_RANGE;
        else
            error = copy_tree(source, target, replace, TRUE);
    } while (!error && FindNextFileW(find, &data));
    FindClose(find);
    return error;
}

static DWORD move_one(LPCWSTR from, LPCWSTR to, BOOL replace, BOOL recurse)
{
    if (MoveFileExW(from, to, replace ? MOVEFILE_REPLACE_EXISTING : 0))
        return ERROR_SUCCESS;
    DWORD error = GetLastError();
    if (error != ERROR_NOT_SAME_DEVICE)
        return error;
    error = copy_tree(from, to, replace, recurse);
    return error ? error : delete_tree(from, TRUE);
}

typedef struct _FILE_OP {
    SHFILEOPSTRUCTW *Op;
    LPCWSTR NextTarget;
    /* the target is a directory receiving every source by name. */
    BOOL IntoDirectory;
} FILE_OP;

static DWORD apply(FILE_OP *op, LPCWSTR source, BOOL from_wildcard)
{
    SHFILEOPSTRUCTW *s = op->Op;
    BOOL replace = (s->fFlags & FOF_NOCONFIRMATION) != 0, recurse = !(s->fFlags & FOF_NORECURSION);
    DWORD attributes = GetFileAttributesW(source);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (from_wildcard && (s->fFlags & FOF_FILESONLY) && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_SUCCESS;
    if (s->wFunc == FO_DELETE)
        return delete_tree(source, recurse);

    WCHAR target[LONG_PATH];
    LPCWSTR destination = op->NextTarget;
    if (!destination || !destination[0])
        return ERROR_INVALID_PARAMETER;
    if (s->fFlags & FOF_MULTIDESTFILES)
        op->NextTarget += wcslen(op->NextTarget) + 1;
    if (op->IntoDirectory) {
        if (!join(target, destination, last_name(source)))
            return ERROR_FILENAME_EXCED_RANGE;
    } else {
        if (wcslen(destination) >= LONG_PATH)
            return ERROR_FILENAME_EXCED_RANGE;
        wcscpy(target, destination);
    }
    if (!replace && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES)
        return ERROR_ALREADY_EXISTS;
    switch (s->wFunc) {
    case FO_COPY:
        return copy_tree(source, target, replace, recurse);
    case FO_MOVE:
        return move_one(source, target, replace, recurse);
    case FO_RENAME:
        return MoveFileW(source, target) ? ERROR_SUCCESS : GetLastError();
    default:
        return ERROR_INVALID_PARAMETER;
    }
}

static DWORD apply_pattern(FILE_OP *op, LPCWSTR pattern)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return ERROR_SUCCESS;
    WCHAR directory[LONG_PATH], source[LONG_PATH];
    wcscpy(directory, pattern);
    LPWSTR slash = wcsrchr(directory, L'\\');
    if (slash)
        slash[1] = 0;
    else
        directory[0] = 0;
    DWORD error = ERROR_SUCCESS;
    do {
        if (is_dots(data.cFileName))
            continue;
        error = join(source, directory, data.cFileName) ? apply(op, source, TRUE) : ERROR_FILENAME_EXCED_RANGE;
    } while (!error && FindNextFileW(find, &data));
    FindClose(find);
    return error;
}

SHSTDAPI int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW operation)
{
    if (!operation || !operation->pFrom || operation->wFunc < FO_MOVE || operation->wFunc > FO_RENAME)
        return ERROR_INVALID_PARAMETER;
    operation->fAnyOperationsAborted = FALSE;
    operation->hNameMappings = NULL;
    FILE_OP op = {operation, operation->pTo, FALSE};
    if (operation->wFunc != FO_DELETE) {
        if (!operation->pTo || !operation->pTo[0])
            return ERROR_INVALID_PARAMETER;
        ULONG sources = 0;
        BOOL wildcard = FALSE;
        for (LPCWSTR p = operation->pFrom; *p; p += wcslen(p) + 1) {
            sources++;
            wildcard |= has_wildcards(p);
        }
        DWORD attributes = GetFileAttributesW(operation->pTo);
        BOOL is_directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
        if (operation->wFunc != FO_RENAME && !(operation->fFlags & FOF_MULTIDESTFILES) &&
            (is_directory || sources > 1 || wildcard)) {
            op.IntoDirectory = TRUE;
            if (!is_directory && !CreateDirectoryW(operation->pTo, NULL))
                return (int)GetLastError();
        }
    }
    DWORD error = ERROR_SUCCESS;
    for (LPCWSTR p = operation->pFrom; *p && !error; p += wcslen(p) + 1) {
        WCHAR full[LONG_PATH];
        DWORD n = GetFullPathNameW(p, LONG_PATH, full, NULL);
        if (!n || n >= LONG_PATH)
            error = ERROR_FILENAME_EXCED_RANGE;
        else
            error = has_wildcards(full) ? apply_pattern(&op, full) : apply(&op, full, FALSE);
    }
    return (int)error;
}
