#include "msi_private.h"

static UINT write_text(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name, PCWSTR text)
{
    return MimeMsiWriteRegistry(package, root, path, name, REG_SZ, text, (DWORD)(wcslen(text) + 1) * sizeof(WCHAR));
}

UINT MimeMsiCacheTransforms(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path, PCWSTR cache)
{
    ULONG count = 0;
    SIZE_T characters = 1;
    for (MIME_MSI_TRANSFORM *entry = package->Database->Transforms; entry; entry = entry->Next) {
        if (++count > 256)
            return ERROR_BAD_CONFIGURATION;
        characters += wcslen(entry->Database->Path) + 1;
    }
    if (count != package->Database->Revision || characters > 512 * 1024)
        return ERROR_BAD_CONFIGURATION;
    if (!count)
        return ERROR_SUCCESS;
    PWSTR names = MimeMsiAllocate(characters * sizeof(WCHAR));
    if (!names)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR cursor = names;
    WCHAR path[160], name[32], filename[MAX_PATH + 32];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\Transforms", product_path);
    SIZE_T base_length = wcslen(cache);
    UINT error = base_length < 4 || base_length >= MAX_PATH ? ERROR_BAD_PATHNAME : ERROR_SUCCESS;
    ULONG index = 0;
    for (MIME_MSI_TRANSFORM *entry = package->Database->Transforms; !error && entry; entry = entry->Next) {
        index++;
        wcscpy(filename, cache);
        _snwprintf(filename + base_length - 4, RTL_NUMBER_OF(filename) - base_length + 4, L"-%08u.mst", index);
        error = MimeMsiWriteInstalledFile(package, entry->Database->Data, entry->Database->File.Size, filename);
        _snwprintf(name, RTL_NUMBER_OF(name), L"%u", index);
        if (!error)
            error = write_text(package, root, path, name, filename);
        _snwprintf(name, RTL_NUMBER_OF(name), L"Flags%u", index);
        if (!error)
            error = MimeMsiWriteRegistry(package, root, path, name, REG_DWORD, &entry->Flags, sizeof(entry->Flags));
        if (cursor != names)
            *cursor++ = L';';
        SIZE_T length = wcslen(entry->Database->Path);
        memcpy(cursor, entry->Database->Path, length * sizeof(WCHAR));
        cursor += length;
    }
    if (!error)
        error = MimeMsiWriteRegistry(package, root, path, L"Count", REG_DWORD, &count, sizeof(count));
    if (!error)
        error = write_text(package, root, product_path, L"Transforms", names);
    MimeMsiFree(names);
    return error;
}

static UINT read_number(HKEY key, PCWSTR name, DWORD *out)
{
    DWORD type, size = sizeof(*out);
    UINT error = RegQueryValueExW(key, name, NULL, &type, (BYTE *)out, &size);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_MORE_DATA ||
        (!error && (type != REG_DWORD || size != sizeof(*out))))
        return ERROR_BAD_CONFIGURATION;
    return error;
}

UINT MimeMsiRestoreTransforms(MSIHANDLE database, HKEY product)
{
    HKEY key;
    UINT error = RegOpenKeyExW(product, L"Transforms", 0, KEY_READ, &key);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        PWSTR names = NULL;
        error = MimeMsiRegistryText(product, L"Transforms", &names);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_SUCCESS;
        if (!error && names && *names)
            error = ERROR_BAD_CONFIGURATION;
        MimeMsiFree(names);
        return error;
    }
    if (error)
        return error;
    DWORD count;
    error = read_number(key, L"Count", &count);
    if (!error && (!count || count > 256))
        error = ERROR_BAD_CONFIGURATION;
    for (DWORD index = 1; !error && index <= count; index++) {
        WCHAR name[32];
        PWSTR path = NULL;
        DWORD flags;
        _snwprintf(name, RTL_NUMBER_OF(name), L"Flags%u", index);
        error = read_number(key, name, &flags);
        if (!error && (flags & ~0x3f))
            error = ERROR_BAD_CONFIGURATION;
        _snwprintf(name, RTL_NUMBER_OF(name), L"%u", index);
        if (!error)
            error = MimeMsiRegistryText(key, name, &path);
        if (!error && (!*path || wcslen(path) > 32767))
            error = ERROR_BAD_CONFIGURATION;
        if (!error)
            error = MsiDatabaseApplyTransformW(database, path, (int)flags);
        MimeMsiFree(path);
    }
    RegCloseKey(key);
    return error;
}

UINT MimeMsiApplyTransformList(MSIHANDLE handle, PCWSTR list)
{
    if (!list || !*list)
        return ERROR_SUCCESS;
    if (*list == L'@' || *list == L'|')
        return ERROR_CALL_NOT_IMPLEMENTED;
    PWSTR copy = MimeMsiDuplicate(list), directory = NULL;
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    if (database)
        directory = MimeMsiDuplicate(database->Path);
    ReleaseSRWLockShared(&MimeMsiLock);
    UINT error = !database ? ERROR_INVALID_HANDLE : !directory ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error) {
        PWSTR separator = wcsrchr(directory, L'\\');
        if (separator)
            separator[1] = 0;
        else
            error = ERROR_BAD_PATHNAME;
    }
    ULONG count = 0;
    int kind = -1;
    for (PWSTR cursor = copy; !error && cursor; ) {
        PWSTR next = wcschr(cursor, L';');
        if (next)
            *next++ = 0;
        SIZE_T length = wcslen(cursor);
        BOOL absolute = (length > 2 && cursor[1] == L':' && (cursor[2] == L'\\' || cursor[2] == L'/')) ||
                        (length > 1 && cursor[0] == L'\\' && cursor[1] == L'\\');
        if (!length || ++count > 256 || (kind >= 0 && kind != (int)absolute))
            error = ERROR_INVALID_PARAMETER;
        else if (*cursor == L':' || *cursor == L'@' || *cursor == L'|')
            error = ERROR_CALL_NOT_IMPLEMENTED;
        else if (!absolute && (wcschr(cursor, L'\\') || wcschr(cursor, L'/') || wcschr(cursor, L':')))
            error = ERROR_INVALID_PARAMETER;
        kind = absolute;
        PWSTR path = NULL;
        if (!error) {
            path = absolute ? MimeMsiDuplicate(cursor) : MimeMsiJoinPath(directory, cursor);
            if (!path)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else if (wcslen(path) > 32767)
                error = ERROR_BAD_PATHNAME;
            else
                error = MsiDatabaseApplyTransformW(handle, path, 0);
        }
        MimeMsiFree(path);
        cursor = next;
    }
    MimeMsiFree(directory);
    MimeMsiFree(copy);
    return error;
}
