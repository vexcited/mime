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
    if (!authored)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PWSTR segment = MimeMsiDuplicate(authored);
    if (!segment)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR colon = wcschr(segment, L':');
    PWSTR selected = source && colon ? colon + 1 : segment;
    if (!source && colon)
        *colon = 0;
    PWSTR long_name = wcschr(selected, L'|');
    if (long_name)
        selected = long_name + 1;
    UINT error = ERROR_SUCCESS;
    if (!wcscmp(selected, L"..") || wcschr(selected, L'\\') || wcschr(selected, L'/') || wcschr(selected, L':'))
        error = ERROR_INSTALL_PACKAGE_INVALID;
    PWSTR prefix = nullptr, joined = nullptr;
    if (!error)
        error = directory_path(package, table, parent, source, depth + 1, &prefix);
    if (!error) {
        joined = MimeMsiJoinPath(prefix, !wcscmp(selected, L".") ? L"" : selected);
        error = joined ? full_directory(joined, out) : ERROR_NOT_ENOUGH_MEMORY;
    }
    MimeMsiFree(prefix);
    MimeMsiFree(joined);
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

UINT WINAPI MsiSetTargetPathW(MSIHANDLE handle, LPCWSTR name, LPCWSTR value)
{
    if (!name || !*name || !value || !*value)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    PWSTR previous = nullptr, path = nullptr;
    UINT error = package ? MimeMsiDirectoryPath(package, name, FALSE, &previous) : ERROR_INVALID_HANDLE;
    if (!error)
        error = full_directory(value, &path);
    if (!error)
        error = MimeMsiSetProperty(package, name, path);
    MimeMsiFree(previous);
    MimeMsiFree(path);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
