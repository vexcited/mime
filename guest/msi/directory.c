#include "msi_private.h"

PWSTR MimeMsiJoinPath(PCWSTR parent, PCWSTR name)
{
    SIZE_T prefix = wcslen(parent), suffix = wcslen(name);
    if (prefix > 32760 || suffix > 32760 - prefix)
        return nullptr;
    BOOL separator = prefix && parent[prefix - 1] != L'\\' && parent[prefix - 1] != L'/';
    PWSTR path = MimeMsiAllocate((prefix + suffix + 2) * sizeof(WCHAR));
    if (!path)
        return nullptr;
    memcpy(path, parent, prefix * sizeof(WCHAR));
    if (separator)
        path[prefix++] = L'\\';
    memcpy(path + prefix, name, (suffix + 1) * sizeof(WCHAR));
    return path;
}

static UINT full_directory(PCWSTR name, PWSTR *out)
{
    DWORD capacity = GetFullPathNameW(name, 0, nullptr, nullptr);
    if (!capacity || capacity > 32760)
        return ERROR_INVALID_PARAMETER;
    PWSTR path = MimeMsiAllocate(((SIZE_T)capacity + 1) * sizeof(WCHAR));
    if (!path)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD length = GetFullPathNameW(name, capacity, path, nullptr);
    if (!length || length >= capacity) {
        MimeMsiFree(path);
        return ERROR_INVALID_PARAMETER;
    }
    if (path[length - 1] != L'\\') {
        path[length++] = L'\\';
        path[length] = 0;
    }
    *out = path;
    return ERROR_SUCCESS;
}

/* the part of DefaultDir a directory adds to its parent, the long name of the target or source half. */
static UINT directory_segment(PCWSTR authored, BOOL source, PWSTR *segment, PWSTR *selected)
{
    if (!authored)
        return ERROR_INSTALL_PACKAGE_INVALID;
    *segment = MimeMsiDuplicate(authored);
    if (!*segment)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR colon = wcschr(*segment, L':');
    *selected = source && colon ? colon + 1 : *segment;
    if (!source && colon)
        *colon = 0;
    PWSTR long_name = wcsrchr(*selected, L'|');
    if (long_name)
        *selected = long_name + 1;
    if (!wcscmp(*selected, L"..") || wcschr(*selected, L'\\') || wcschr(*selected, L'/') || wcschr(*selected, L':')) {
        MimeMsiFree(*segment);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    if (!wcscmp(*selected, L"."))
        **selected = 0;
    return ERROR_SUCCESS;
}

static UINT child_directory(PCWSTR parent, PCWSTR segment, PWSTR *out)
{
    PWSTR joined = MimeMsiJoinPath(parent, segment);
    UINT error = joined ? full_directory(joined, out) : ERROR_NOT_ENOUGH_MEMORY;
    MimeMsiFree(joined);
    return error;
}

static UINT directory_path(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, PCWSTR name,
                           BOOL source, ULONG depth, PWSTR *out)
{
    if (depth >= 64)
        return ERROR_BAD_CONFIGURATION;
    ULONG id = MimeMsiColumn(table, L"Directory", TRUE);
    ULONG parent_column = MimeMsiColumn(table, L"Directory_Parent", TRUE);
    ULONG default_column = MimeMsiColumn(table, L"DefaultDir", TRUE);
    if (id == UINT32_MAX || parent_column == UINT32_MAX || default_column == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG row;
    for (row = 0; row < table->Rows; row++) {
        PCWSTR key = MimeMsiText(package->Database, table, row, id);
        if (key && !wcscmp(key, name))
            break;
    }
    if (row == table->Rows)
        return ERROR_DIRECTORY;
    PCWSTR parent = MimeMsiText(package->Database, table, row, parent_column);
    PCWSTR authored = MimeMsiText(package->Database, table, row, default_column);
    PCWSTR property = source ? L"" : MimeMsiGetProperty(package, name);
    if (*property)
        return full_directory(property, out);
    if (!parent || !*parent) {
        PCWSTR root = source ? MimeMsiGetProperty(package, L"SourceDir") : MimeMsiGetProperty(package, L"ROOTDRIVE");
        return full_directory(*root ? root : L"C:\\", out);
    }
    PWSTR segment, selected;
    UINT error = directory_segment(authored, source, &segment, &selected);
    PWSTR prefix = nullptr;
    if (!error)
        error = directory_path(package, table, parent, source, depth + 1, &prefix);
    if (!error)
        error = child_directory(prefix, selected, out);
    MimeMsiFree(prefix);
    MimeMsiFree(segment);
    return error;
}

UINT MimeMsiDirectoryPath(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL source, PWSTR *out)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Directory", &table);
    return error ? error : directory_path(package, table, name, source, 0, out);
}

/* after costing, each directory becomes a property holding its path, which is how formatted text resolves it. */
UINT MimeMsiPublishDirectories(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Directory", &table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(table, L"Directory", TRUE);
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        if (!name || !*name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        PWSTR path = nullptr;
        error = directory_path(package, table, name, FALSE, 0, &path);
        if (!error)
            error = MimeMsiSetProperty(package, name, path);
        MimeMsiFree(path);
    }
    return error;
}

static UINT get_path(MSIHANDLE handle, LPCWSTR name, BOOL source, LPWSTR out, LPDWORD size)
{
    if (!name || !*name)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    PWSTR path = nullptr;
    UINT error = package ? MimeMsiDirectoryPath(package, name, source, &path) : ERROR_INVALID_HANDLE;
    /* the directory table is loaded by CostInitialize, until then there is no target path to give. */
    if (!error && !source && !package->CostStage && !package->Maintenance)
        error = ERROR_DIRECTORY;
    if (!error)
        error = MimeMsiCopyString(path, out, size);
    MimeMsiFree(path);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiGetTargetPathW(MSIHANDLE handle, LPCWSTR name, LPWSTR out, LPDWORD size)
{
    MIME_REMOTE(PROXY_GetTargetPathW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(out), MIME_ARG(size));
    return get_path(handle, name, FALSE, out, size);
}

UINT WINAPI MsiGetSourcePathW(MSIHANDLE handle, LPCWSTR name, LPWSTR out, LPDWORD size)
{
    MIME_REMOTE(PROXY_GetSourcePathW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(out), MIME_ARG(size));
    return get_path(handle, name, TRUE, out, size);
}

/* every directory at or under the old path is rebased on the new one, by path and not by the table's parents. */
static UINT rebase_directories(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, PCWSTR previous, PCWSTR path)
{
    ULONG id = MimeMsiColumn(table, L"Directory", TRUE);
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    SIZE_T length = wcslen(previous);
    UINT error = ERROR_SUCCESS;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        PCWSTR current = name ? MimeMsiGetProperty(package, name) : L"";
        if (!*current || _wcsnicmp(current, previous, length))
            continue;
        PWSTR moved = MimeMsiJoinPath(path, current + length);
        error = moved ? MimeMsiSetProperty(package, name, moved) : ERROR_NOT_ENOUGH_MEMORY;
        MimeMsiFree(moved);
    }
    return error;
}

UINT MimeMsiSetTargetPath(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR value)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Directory", &table);
    PWSTR previous = nullptr, path = nullptr;
    if (!error)
        error = directory_path(package, table, name, FALSE, 0, &previous);
    if (!error)
        error = full_directory(value, &path);
    if (!error && _wcsicmp(previous, path))
        error = rebase_directories(package, table, previous, path);
    if (!error)
        error = MimeMsiSetProperty(package, name, path);
    MimeMsiFree(previous);
    MimeMsiFree(path);
    return error;
}

UINT WINAPI MsiSetTargetPathW(MSIHANDLE handle, LPCWSTR name, LPCWSTR value)
{
    MIME_REMOTE(PROXY_SetTargetPathW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(value));
    if (!name || !*name || !value || !*value)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = package ? MimeMsiSetTargetPath(package, name, value) : ERROR_INVALID_HANDLE;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
