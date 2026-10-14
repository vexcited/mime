#include "msi_private.h"

UINT MimeMsiSaveDirectories(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Directory", &table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(table, L"Directory", TRUE);
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    WCHAR catalog[128];
    _snwprintf(catalog, RTL_NUMBER_OF(catalog), L"%s\\Directories", path);
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        PWSTR directory = NULL;
        if (!name || !*name)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else
            error = MimeMsiDirectoryPath(package, name, FALSE, &directory);
        if (!error)
            error = MimeMsiWriteRegistry(package, root, catalog, name, REG_SZ, directory,
                                         (DWORD)(wcslen(directory) + 1) * sizeof(WCHAR));
        MimeMsiFree(directory);
    }
    return error;
}

static UINT restore_directories(MIME_MSI_PACKAGE *package, HKEY product, BOOL validate)
{
    HKEY key;
    UINT error = RegOpenKeyExW(product, L"Directories", 0, KEY_READ, &key);
    if (!validate && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
        return ERROR_SUCCESS;
    if (error)
        return error;
    MIME_MSI_TABLE *table;
    error = MimeMsiLoadTable(package->Database, L"Directory", &table);
    ULONG id = error ? UINT32_MAX : MimeMsiColumn(table, L"Directory", TRUE);
    if (!error && id == UINT32_MAX)
        error = ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; !error && row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        PWSTR directory = NULL, current = NULL;
        if (!name || !*name)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else
            error = MimeMsiRegistryText(key, name, &directory);
        if (!error) {
            SIZE_T length = wcslen(directory);
            BOOL absolute = length > 2 && ((directory[1] == L':' && directory[2] == L'\\') ||
                                          (directory[0] == L'\\' && directory[1] == L'\\'));
            if (!absolute || length > 32760)
                error = ERROR_BAD_CONFIGURATION;
        }
        if (!error && validate) {
            error = MimeMsiDirectoryPath(package, name, FALSE, &current);
            if (!error && _wcsicmp(current, directory))
                error = ERROR_CALL_NOT_IMPLEMENTED;
        } else if (!error)
            error = MimeMsiSetProperty(package, name, directory);
        MimeMsiFree(directory);
        MimeMsiFree(current);
    }
    RegCloseKey(key);
    if (!error)
        package->MaintenanceReady = TRUE;
    return error;
}

UINT MimeMsiLoadMaintenance(MIME_MSI_PACKAGE *package, HKEY key, PCWSTR product, BOOL machine)
{
    UINT error = restore_directories(package, key, FALSE);
    if (!error)
        error = MimeMsiSetProperty(package, L"ALLUSERS", machine ? L"1" : L"");
    if (!error)
        error = MimeMsiSetProperty(package, L"Installed", L"1");
    if (!error) {
        package->Maintenance = TRUE;
        package->MaintenanceRoot = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        wcscpy(package->MaintenanceProduct, product);
    }
    return error;
}

UINT MimeMsiValidateMaintenance(MIME_MSI_PACKAGE *package)
{
    if (!package->Maintenance)
        return ERROR_SUCCESS;
    if (!package->MaintenanceReady)
        return ERROR_BAD_CONFIGURATION;
    PCWSTR all_users = MimeMsiGetProperty(package, L"ALLUSERS");
    if (_wcsicmp(package->MaintenanceProduct, MimeMsiGetProperty(package, L"ProductCode")) ||
        wcscmp(all_users, package->MaintenanceRoot == HKEY_LOCAL_MACHINE ? L"1" : L""))
        return ERROR_INVALID_PARAMETER;
    WCHAR path[96];
    UINT error = MimeMsiCatalogPath(package->MaintenanceProduct, path);
    HKEY key;
    if (!error) {
        error = RegOpenKeyExW(package->MaintenanceRoot, path, 0, KEY_READ, &key);
        if (!error) {
            error = restore_directories(package, key, TRUE);
            RegCloseKey(key);
        }
    }
    return error;
}
