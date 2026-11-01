/*
 * the file operations of the file system object: a walk over the files a path with wildcards names, and the delete, copy, move
 * and rename of files and folders that run on it, with the errors Windows gives for them.
 */
#include "scrrun_private.h"

/* the text copied into the buffer of a walk at an offset, with the buffer grown to hold it. */
static HRESULT buffer_put(WCHAR **buffer, SIZE_T *capacity, SIZE_T at, const WCHAR *text)
{
    SIZE_T length = wcslen(text);
    if (at + length + 1 > *capacity) {
        SIZE_T size = at + length + 1 + 256;
        WCHAR *grown = ScrAlloc(size * sizeof(WCHAR));
        if (!grown)
            return E_OUTOFMEMORY;
        if (*buffer && *capacity)
            memcpy(grown, *buffer, *capacity * sizeof(WCHAR));
        ScrFree(*buffer);
        *buffer = grown;
        *capacity = size;
    }
    memcpy(*buffer + at, text, (length + 1) * sizeof(WCHAR));
    return S_OK;
}

HRESULT EnumReserve(ENUM_DATA *data, SIZE_T length)
{
    if (length + 1 <= data->Capacity)
        return S_OK;
    return buffer_put(&data->Path, &data->Capacity, length, L"");
}

HRESULT EnumPath(ENUM_DATA *data)
{
    HANDLE handle = FindFirstFileW(data->Path, &data->Find);
    if (handle == INVALID_HANDLE_VALUE)
        return TranslateWin32Error(GetLastError());
    data->Depth++;
    HRESULT hr = S_OK;
    do {
        const WCHAR *name = data->Find.cFileName;
        if (data->Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
                continue;
            if (!data->FolderProc)
                continue;
            hr = buffer_put(&data->Path, &data->Capacity, data->NameStart, name);
            if (FAILED(hr))
                break;
            data->Folders++;
            hr = data->FolderProc(data);
        } else {
            if (!data->FileProc)
                continue;
            hr = buffer_put(&data->Path, &data->Capacity, data->NameStart, name);
            if (FAILED(hr))
                break;
            data->Files++;
            hr = data->FileProc(data);
        }
        if (FAILED(hr))
            break;
    } while (FindNextFileW(handle, &data->Find));
    data->Depth--;
    FindClose(handle);
    return hr;
}

/* the folder just found, walked from inside: its path gets a backslash and a star after it. */
HRESULT EnumStandardFolder(ENUM_DATA *data)
{
    SIZE_T saved = data->NameStart;
    SIZE_T name_end = saved + wcslen(data->Path + saved);
    data->NameStart = name_end + 1;
    HRESULT hr = buffer_put(&data->Path, &data->Capacity, data->NameStart, L"*");
    if (FAILED(hr))
        return hr;
    data->Path[data->NameStart - 1] = '\\';
    hr = EnumPath(data);
    if (FAILED(hr))
        return hr;
    data->Path[data->NameStart - 1] = 0;
    data->NameStart = saved;
    return S_OK;
}

/* ------------------------------------------------------------------ delete */

typedef struct DELETE_CONTEXT {
    BOOL Force;
} DELETE_CONTEXT;

/* a failed delete of a read only file goes on when force is given. */
static BOOL force_retry(DELETE_CONTEXT *context, const WCHAR *path, DWORD error, BOOL (*remove)(const WCHAR *))
{
    if (!context->Force || error != ERROR_ACCESS_DENIED)
        return FALSE;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_READONLY))
        return FALSE;
    if (!SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL))
        return FALSE;
    if (remove(path))
        return TRUE;
    SetFileAttributesW(path, attributes);
    return FALSE;
}

static BOOL remove_file(const WCHAR *path)
{
    return DeleteFileW(path);
}

static BOOL remove_folder(const WCHAR *path)
{
    return RemoveDirectoryW(path);
}

static HRESULT delete_file_proc(ENUM_DATA *data)
{
    if (data->Depth == 1 && data->FolderProc)
        return S_OK;
    if (DeleteFileW(data->Path))
        return S_OK;
    DWORD error = GetLastError();
    if (force_retry(data->Context, data->Path, error, remove_file))
        return S_OK;
    return TranslateWin32Error(error);
}

static HRESULT delete_folder_proc(ENUM_DATA *data)
{
    HRESULT hr = EnumStandardFolder(data);
    if (FAILED(hr))
        return hr;
    if (RemoveDirectoryW(data->Path))
        return S_OK;
    DWORD error = GetLastError();
    if (force_retry(data->Context, data->Path, error, remove_folder))
        return S_OK;
    return TranslateWin32Error(error);
}

static HRESULT delete_walk(const WCHAR *spec, BOOL force, BOOL folders)
{
    int length = 0;
    const WCHAR *name = PathNamePart(spec, &length);
    if (!name)
        return E_INVALIDARG;
    DELETE_CONTEXT context = {force};
    ENUM_DATA data = {.Context = &context, .FileProc = delete_file_proc, .FolderProc = folders ? delete_folder_proc : nullptr};
    data.NameStart = (SIZE_T)(name - spec);
    HRESULT hr = buffer_put(&data.Path, &data.Capacity, 0, spec);
    if (SUCCEEDED(hr)) {
        hr = EnumPath(&data);
        if (folders && hr == E_VB_FILENOTFOUND)
            hr = E_VB_PATHNOTFOUND;
        else if (!folders && hr == E_VB_PATHNOTFOUND)
            hr = E_VB_FILENOTFOUND;
    }
    ScrFree(data.Path);
    return hr;
}

HRESULT FsDeleteFile(const WCHAR *spec, BOOL force)
{
    return delete_walk(spec, force, FALSE);
}

HRESULT FsDeleteFolder(const WCHAR *spec, BOOL force)
{
    return delete_walk(spec, force, TRUE);
}

/* ------------------------------------------------------------------ copy and move */

#define RELOCATE_OVERWRITE 1
#define RELOCATE_DIRECTORY 2

typedef struct RELOCATE_CONTEXT {
    DWORD Flags;
    SIZE_T DestinationLength;
    SIZE_T Capacity;
    WCHAR *Destination;
} RELOCATE_CONTEXT;

/* whether the second path is under the first. */
static BOOL is_ancestor(const WCHAR *ancestor, const WCHAR *path)
{
    WCHAR *a, *b;
    if (FAILED(PathAbsolute(ancestor, FALSE, &a)))
        return FALSE;
    if (FAILED(PathAbsolute(path, FALSE, &b))) {
        ScrFree(a);
        return FALSE;
    }
    SIZE_T length = wcslen(a);
    BOOL result = PathIsSeparator(b[length]) && !_wcsnicmp(a, b, length);
    ScrFree(a);
    ScrFree(b);
    return result;
}

static HRESULT append_name(ENUM_DATA *data, RELOCATE_CONTEXT *context)
{
    if (!(context->Flags & RELOCATE_DIRECTORY))
        return S_OK;
    return buffer_put(&context->Destination, &context->Capacity, context->DestinationLength, data->Path + data->NameStart);
}

static HRESULT copy_file_proc(ENUM_DATA *data)
{
    RELOCATE_CONTEXT *context = data->Context;
    if (data->Depth == 1 && data->FolderProc)
        return S_OK;
    HRESULT hr = append_name(data, context);
    if (FAILED(hr))
        return hr;
    if (_wcsicmp(data->Path, context->Destination)) {
        if (!CopyFileW(data->Path, context->Destination, !(context->Flags & RELOCATE_OVERWRITE)))
            return TranslateWin32Error(GetLastError());
        return S_OK;
    }
    return (context->Flags & RELOCATE_OVERWRITE) ? S_OK : E_VB_FILEEXISTS;
}

static HRESULT copy_folder_proc(ENUM_DATA *data)
{
    RELOCATE_CONTEXT *context = data->Context;
    HRESULT hr = append_name(data, context);
    if (FAILED(hr))
        return hr;
    context->Flags |= RELOCATE_DIRECTORY;
    if (data->Depth == 1 && is_ancestor(data->Path, context->Destination))
        return E_VB_INVALID;
    DWORD attributes = PathAttributes(context->Destination);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(context->Destination, nullptr)) {
            hr = TranslateWin32Error(GetLastError());
            if (FAILED(hr))
                return hr;
        }
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || !(context->Flags & RELOCATE_OVERWRITE)) {
        return E_VB_FILEEXISTS;
    }
    SIZE_T saved = context->DestinationLength;
    context->DestinationLength = wcslen(context->Destination) + 1;
    hr = buffer_put(&context->Destination, &context->Capacity, context->DestinationLength - 1, L"\\");
    if (FAILED(hr))
        return hr;
    hr = EnumStandardFolder(data);
    if (FAILED(hr))
        return hr;
    context->DestinationLength = saved;
    return S_OK;
}

static HRESULT move_proc(ENUM_DATA *data)
{
    RELOCATE_CONTEXT *context = data->Context;
    HRESULT hr = append_name(data, context);
    if (FAILED(hr))
        return hr;
    if (data->FolderProc && is_ancestor(data->Path, context->Destination))
        return E_VB_INVALID;
    if (!MoveFileW(data->Path, context->Destination))
        return TranslateWin32Error(GetLastError());
    return S_OK;
}

HRESULT FsRelocate(const WCHAR *source, const WCHAR *destination, BOOL overwrite, BOOL folder, BOOL move,
                   WCHAR **final_path)
{
    if (final_path)
        *final_path = nullptr;
    if (!*source || !*destination)
        return E_INVALIDARG;
    int name_length = 0;
    const WCHAR *name = PathNamePart(source, &name_length);
    if (!name)
        return E_INVALIDARG;
    SIZE_T directory = (SIZE_T)(name - source);
    if ((PathScan(source, (int)directory) & SCAN_WILDCARD) || (PathScan(destination, -1) & SCAN_WILDCARD))
        return E_INVALIDARG;
    RELOCATE_CONTEXT context = {.Flags = overwrite ? RELOCATE_OVERWRITE : 0};
    ENUM_DATA data = {.Context = &context, .FileProc = move ? move_proc : copy_file_proc,
                      .FolderProc = folder ? (move ? move_proc : copy_folder_proc) : nullptr};
    data.NameStart = directory;
    HRESULT hr = buffer_put(&data.Path, &data.Capacity, 0, source);
    if (SUCCEEDED(hr))
        hr = buffer_put(&context.Destination, &context.Capacity, 0, destination);
    if (FAILED(hr))
        goto done;
    context.DestinationLength = wcslen(destination);
    if ((PathScan(name, -1) & SCAN_WILDCARD) && !PathIsSeparator(destination[context.DestinationLength - 1])) {
        hr = buffer_put(&context.Destination, &context.Capacity, context.DestinationLength, L"\\");
        if (FAILED(hr))
            goto done;
        context.DestinationLength++;
    }
    DWORD attributes = PathAttributes(context.Destination);
    if (PathIsSeparator(context.Destination[context.DestinationLength - 1])) {
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            hr = E_VB_PATHNOTFOUND;
            goto done;
        }
        context.Flags |= RELOCATE_DIRECTORY;
    }
    hr = EnumPath(&data);
    if (SUCCEEDED(hr)) {
        if (folder) {
            if (!data.Folders)
                hr = E_VB_PATHNOTFOUND;
        } else if (!data.Files) {
            hr = E_VB_FILENOTFOUND;
        }
        if (SUCCEEDED(hr) && final_path)
            hr = PathCanonical(context.Destination, final_path);
    } else if (folder && hr == E_VB_FILENOTFOUND) {
        hr = E_VB_PATHNOTFOUND;
    }
done:
    ScrFree(data.Path);
    ScrFree(context.Destination);
    return hr;
}

/* the new path of a file or folder given a new name in the folder it is in. */
HRESULT FsRename(const WCHAR *path, const WCHAR *name, WCHAR **new_path)
{
    *new_path = nullptr;
    int length = 0;
    const WCHAR *current = PathNamePart(path, &length);
    if (!current)
        return E_VB_INVALID;
    SIZE_T directory = (SIZE_T)(current - path);
    WCHAR *target = ScrCopy(path, directory);
    if (!target)
        return E_OUTOFMEMORY;
    SIZE_T capacity = directory + 1;
    HRESULT hr = buffer_put(&target, &capacity, directory, name);
    if (FAILED(hr)) {
        ScrFree(target);
        return hr;
    }
    if (PathAttributes(target) != INVALID_FILE_ATTRIBUTES) {
        ScrFree(target);
        return E_VB_FILEEXISTS;
    }
    DWORD attributes = PathAttributes(path);
    BOOL folder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    hr = FsRelocate(path, target, FALSE, folder, TRUE, nullptr);
    if (SUCCEEDED(hr))
        hr = PathCanonical(target, new_path);
    ScrFree(target);
    return hr;
}

/* ------------------------------------------------------------------ size */

static HRESULT size_file_proc(ENUM_DATA *data)
{
    *(ULONGLONG *)data->Context += ((ULONGLONG)data->Find.nFileSizeHigh << 32) | data->Find.nFileSizeLow;
    return S_OK;
}

HRESULT FsFolderSize(const WCHAR *path, ULONGLONG *size)
{
    *size = 0;
    ENUM_DATA data = {.Context = size, .FileProc = size_file_proc, .FolderProc = EnumStandardFolder};
    HRESULT hr = buffer_put(&data.Path, &data.Capacity, 0, path);
    if (SUCCEEDED(hr)) {
        data.NameStart = wcslen(path);
        hr = EnumStandardFolder(&data);
    }
    ScrFree(data.Path);
    return hr;
}
