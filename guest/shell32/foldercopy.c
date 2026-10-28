/*
 * copying and moving what a folder holds into a directory, for Folder.CopyHere and Folder.MoveHere.
 *
 * an item that is a file of the file system goes to SHFileOperation. any other item, like a file of a zip file, is read
 * out of its folder: a folder item is made as a directory and its items follow, a file item is read as a stream
 * from BindToObject. the flags are the FOF_ flags of SHFileOperation. an existing file is only replaced with
 * FOF_NOCONFIRMATION, as the answer yes would; without it, or with FOF_RENAMEONCOLLISION, there is nobody to ask and
 * the file is kept, or the new one is given another name.
 */
#include "shell32_private.h"

/* the System.* keys that folders answer details for. */
static const GUID g_system_key = {0xB725F130, 0x47EF, 0x101A, {0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC}};
#define KEY_SIZE 12
#define KEY_MODIFIED 14

#define COPY_CHUNK (256 * 1024)

/* ------------------------------------------------------------------ names and details */

/* the text of a name a folder gave, as memory of the task allocator. */
static LPWSTR strret_text(STRRET *name, PCUITEMID_CHILD pidl)
{
    switch (name->uType) {
    case STRRET_WSTR:
        return name->pOleStr;
    case STRRET_CSTR:
        return ShWiden(name->cStr);
    case STRRET_OFFSET:
        return pidl ? ShWiden((LPCSTR)pidl + name->uOffset) : nullptr;
    }
    return nullptr;
}

HRESULT ShDisplayText(IShellFolder *folder, PCUITEMID_CHILD child, SHGDNF flags, LPWSTR *text)
{
    STRRET name;
    *text = nullptr;
    HRESULT hr = folder->lpVtbl->GetDisplayNameOf(folder, child, flags, &name);
    if (FAILED(hr))
        return hr;
    *text = strret_text(&name, child);
    return *text ? S_OK : E_OUTOFMEMORY;
}

static SFGAOF attributes_of(IShellFolder *folder, PCUITEMID_CHILD child, SFGAOF mask)
{
    SFGAOF attributes = mask;
    if (FAILED(folder->lpVtbl->GetAttributesOf(folder, 1, &child, &attributes)))
        return 0;
    return attributes & mask;
}

/* a detail of an item as the variant of the folder that holds it. */
static BOOL item_detail(IShellFolder *folder, PCUITEMID_CHILD child, DWORD pid, VARIANT *value)
{
    IShellFolder2 *folder2;
    PROPERTYKEY key = {g_system_key, pid};
    VariantInit(value);
    if (FAILED(folder->lpVtbl->QueryInterface(folder, &IID_IShellFolder2, (void **)&folder2)))
        return FALSE;
    HRESULT hr = folder2->lpVtbl->GetDetailsEx(folder2, child, &key, value);
    folder2->lpVtbl->Release(folder2);
    return SUCCEEDED(hr);
}

static BOOL date_filetime(DATE date, FILETIME *time)
{
    SYSTEMTIME system;
    FILETIME local;
    return VariantTimeToSystemTime(date, &system) && SystemTimeToFileTime(&system, &local) &&
           LocalFileTimeToFileTime(&local, time);
}

/* the size and last write time of an item, nothing known giving zeros. */
HRESULT ShItemDetails(IShellFolder *folder, PCUITEMID_CHILD child, ULONGLONG *size, FILETIME *time)
{
    *size = 0;
    time->dwLowDateTime = time->dwHighDateTime = 0;
    if (attributes_of(folder, child, SFGAO_FILESYSTEM) & SFGAO_FILESYSTEM) {
        LPWSTR path;
        HRESULT hr = ShDisplayText(folder, child, SHGDN_FORPARSING, &path);
        if (FAILED(hr))
            return hr;
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
            *size = (ULONGLONG)data.nFileSizeHigh << 32 | data.nFileSizeLow;
            *time = data.ftLastWriteTime;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                *size = 0;
        } else {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
        ShFree(path);
        return hr;
    }
    VARIANT value;
    if (item_detail(folder, child, KEY_SIZE, &value)) {
        VARIANT number;
        VariantInit(&number);
        if (SUCCEEDED(VariantChangeType(&number, &value, 0, VT_UI8)))
            *size = number.ullVal;
        VariantClear(&value);
    }
    if (item_detail(folder, child, KEY_MODIFIED, &value)) {
        if (value.vt == VT_DATE)
            date_filetime(value.date, time);
        VariantClear(&value);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ paths */

/* a path as the system takes a long one. the caller frees it. */
static LPWSTR api_path(LPCWSTR path)
{
    SIZE_T length = wcslen(path);
    LPCWSTR prefix = L"";
    if (length >= MAX_PATH - 12 && !(path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?')) {
        if (path[0] == L'\\' && path[1] == L'\\') {
            prefix = L"\\\\?\\UNC\\";
            path += 2;
            length -= 2;
        } else if (path[0] && path[1] == L':') {
            prefix = L"\\\\?\\";
        }
    }
    SIZE_T lead = wcslen(prefix);
    LPWSTR copy = ShAlloc((lead + length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, prefix, lead * sizeof(WCHAR));
        memcpy(copy + lead, path, (length + 1) * sizeof(WCHAR));
    }
    return copy;
}

static LPWSTR join_path(LPCWSTR directory, LPCWSTR name)
{
    SIZE_T length = wcslen(directory), size = length + wcslen(name) + 2;
    LPWSTR path = ShAlloc(size * sizeof(WCHAR));
    if (!path)
        return nullptr;
    wcscpy(path, directory);
    if (length && path[length - 1] != L'\\')
        path[length++] = L'\\';
    wcscpy(path + length, name);
    return path;
}

/* a name the file system takes: its reserved characters are replaced, and a trailing dot or space dropped. */
static void clean_name(LPWSTR name)
{
    SIZE_T length = wcslen(name);
    for (SIZE_T i = 0; i < length; i++) {
        WCHAR c = name[i];
        if (c < 0x20 || c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' ||
            c == L'?' || c == L'*')
            name[i] = L'_';
    }
    while (length && (name[length - 1] == L'.' || name[length - 1] == L' '))
        name[--length] = 0;
    if (!length)
        wcscpy(name, L"_");
}

static DWORD path_attributes(LPCWSTR path)
{
    LPWSTR api = api_path(path);
    DWORD attributes = api ? GetFileAttributesW(api) : INVALID_FILE_ATTRIBUTES;
    ShFree(api);
    return attributes;
}

/* "name (2).ext" and on, the first that is free. */
static LPWSTR free_name(LPCWSTR directory, LPCWSTR name)
{
    SIZE_T length = wcslen(name);
    LPCWSTR dot = wcsrchr(name, L'.');
    SIZE_T stem = dot && dot != name ? (SIZE_T)(dot - name) : length;
    for (int n = 2; n < 1000; n++) {
        WCHAR numbered[MAX_PATH + 16];
        if (stem + length - stem + 16 > RTL_NUMBER_OF(numbered))
            return nullptr;
        memcpy(numbered, name, stem * sizeof(WCHAR));
        _snwprintf(numbered + stem, 16, L" (%d)", n);
        wcscpy(numbered + wcslen(numbered), name + stem);
        LPWSTR path = join_path(directory, numbered);
        if (!path || path_attributes(path) == INVALID_FILE_ATTRIBUTES)
            return path;
        ShFree(path);
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the file system */

HRESULT ShCopyPath(LPCWSTR path, LPCWSTR directory, DWORD flags, BOOL move)
{
    SIZE_T from_length = wcslen(path), to_length = wcslen(directory);
    LPWSTR from = ShAllocZero((from_length + 2) * sizeof(WCHAR));
    LPWSTR to = ShAllocZero((to_length + 2) * sizeof(WCHAR));
    HRESULT hr = E_OUTOFMEMORY;
    if (from && to) {
        wcscpy(from, path);
        wcscpy(to, directory);
        SHFILEOPSTRUCTW operation = {};
        operation.wFunc = move ? FO_MOVE : FO_COPY;
        operation.pFrom = from;
        operation.pTo = to;
        operation.fFlags = (FILEOP_FLAGS)((flags & (FOF_NOCONFIRMATION | FOF_NORECURSION | FOF_FILESONLY)) |
                                          FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR);
        int error = SHFileOperationW(&operation);
        hr = !error || error == ERROR_ALREADY_EXISTS ? S_OK : HRESULT_FROM_WIN32(error);
    }
    ShFree(from);
    ShFree(to);
    return hr;
}

/* ------------------------------------------------------------------ the items of other folders */

typedef struct _EXTRACT {
    DWORD Flags;
    BYTE *Buffer;
} EXTRACT;

static HRESULT extract_item(IShellFolder *folder, PCUITEMID_CHILD child, LPCWSTR directory, EXTRACT *extract);

static HRESULT extract_folder(IShellFolder *folder, PCUITEMID_CHILD child, LPCWSTR target, EXTRACT *extract)
{
    LPWSTR api = api_path(target);
    if (!api)
        return E_OUTOFMEMORY;
    BOOL made = CreateDirectoryW(api, nullptr);
    DWORD error = GetLastError();
    ShFree(api);
    if (!made && !(error == ERROR_ALREADY_EXISTS && (path_attributes(target) & FILE_ATTRIBUTE_DIRECTORY)))
        return HRESULT_FROM_WIN32(error);
    if (extract->Flags & FOF_NORECURSION)
        return S_OK;
    IShellFolder *inner;
    HRESULT hr = folder->lpVtbl->BindToObject(folder, child, nullptr, &IID_IShellFolder, (void **)&inner);
    if (FAILED(hr))
        return hr;
    IEnumIDList *list;
    hr = inner->lpVtbl->EnumObjects(inner, nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &list);
    if (SUCCEEDED(hr)) {
        PITEMID_CHILD next;
        ULONG fetched;
        while (SUCCEEDED(hr) && list->lpVtbl->Next(list, 1, &next, &fetched) == S_OK && fetched) {
            hr = extract_item(inner, next, target, extract);
            ILFree(next);
        }
        list->lpVtbl->Release(list);
    }
    inner->lpVtbl->Release(inner);
    return hr;
}

static HRESULT extract_file(IShellFolder *folder, PCUITEMID_CHILD child, LPCWSTR directory, LPCWSTR name,
                            LPCWSTR target, EXTRACT *extract)
{
    LPWSTR renamed = nullptr;
    DWORD attributes = path_attributes(target);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
            return S_OK;
        if (!(extract->Flags & FOF_NOCONFIRMATION)) {
            if (!(extract->Flags & FOF_RENAMEONCOLLISION))
                return S_OK;
            renamed = free_name(directory, name);
            if (!renamed)
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            target = renamed;
        }
    }
    IStream *stream;
    HRESULT hr = folder->lpVtbl->BindToObject(folder, child, nullptr, &IID_IStream, (void **)&stream);
    if (FAILED(hr)) {
        ShFree(renamed);
        return hr;
    }
    LPWSTR api = api_path(target);
    HANDLE file = api ? CreateFileW(api, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                      : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE && api && GetLastError() == ERROR_ACCESS_DENIED &&
        (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(api, FILE_ATTRIBUTE_NORMAL);
        file = CreateFileW(api, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file == INVALID_HANDLE_VALUE) {
        hr = api ? HRESULT_FROM_WIN32(GetLastError()) : E_OUTOFMEMORY;
    } else {
        for (;;) {
            ULONG got = 0, written;
            hr = stream->lpVtbl->Read(stream, extract->Buffer, COPY_CHUNK, &got);
            if (FAILED(hr) || !got) {
                hr = SUCCEEDED(hr) ? S_OK : hr;
                break;
            }
            if (!WriteFile(file, extract->Buffer, got, &written, nullptr) || written != got) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }
        if (SUCCEEDED(hr)) {
            ULONGLONG size;
            FILETIME time;
            if (SUCCEEDED(ShItemDetails(folder, child, &size, &time)) && (time.dwLowDateTime || time.dwHighDateTime))
                SetFileTime(file, nullptr, nullptr, &time);
        }
        CloseHandle(file);
        if (FAILED(hr))
            DeleteFileW(api);
    }
    ShFree(api);
    ShFree(renamed);
    stream->lpVtbl->Release(stream);
    return hr;
}

static HRESULT extract_item(IShellFolder *folder, PCUITEMID_CHILD child, LPCWSTR directory, EXTRACT *extract)
{
    LPWSTR name;
    HRESULT hr = ShDisplayText(folder, child, SHGDN_INFOLDER | SHGDN_FORPARSING, &name);
    if (FAILED(hr))
        return hr;
    clean_name(name);
    LPWSTR target = join_path(directory, name);
    if (!target) {
        ShFree(name);
        return E_OUTOFMEMORY;
    }
    if (attributes_of(folder, child, SFGAO_FOLDER) & SFGAO_FOLDER)
        hr = extract_folder(folder, child, target, extract);
    else
        hr = extract_file(folder, child, directory, name, target, extract);
    ShFree(target);
    ShFree(name);
    return hr;
}

/* an item of a folder into a directory. */
HRESULT ShCopyItem(IShellFolder *folder, PCUITEMID_CHILD child, LPCWSTR directory, DWORD flags, BOOL move)
{
    if (attributes_of(folder, child, SFGAO_FILESYSTEM) & SFGAO_FILESYSTEM) {
        LPWSTR path;
        HRESULT hr = ShDisplayText(folder, child, SHGDN_FORPARSING, &path);
        if (FAILED(hr))
            return hr;
        hr = ShCopyPath(path, directory, flags, move);
        ShFree(path);
        return hr;
    }
    if (move)
        return E_NOTIMPL;
    EXTRACT extract = {flags, ShAlloc(COPY_CHUNK)};
    if (!extract.Buffer)
        return E_OUTOFMEMORY;
    HRESULT hr = extract_item(folder, child, directory, &extract);
    ShFree(extract.Buffer);
    return hr;
}
