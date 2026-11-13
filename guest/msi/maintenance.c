#include "msi_private.h"

/*
 * the name a directory is kept under. the names of a registry key ignore case and those of the table do not, so a
 * directory whose name an earlier row has in another case, as CommonAppDataFolder and COMMONAPPDATAFOLDER, takes its row.
 */
static void directory_value(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG id, ULONG row, WCHAR out[96])
{
    PCWSTR name = MimeMsiText(package->Database, table, row, id);
    for (ULONG earlier = 0; earlier < row; earlier++) {
        PCWSTR other = MimeMsiText(package->Database, table, earlier, id);
        if (other && !_wcsicmp(other, name)) {
            _snwprintf(out, 96, L"%s:%u", name, row);
            out[95] = 0;
            return;
        }
    }
    _snwprintf(out, 96, L"%s", name);
    out[95] = 0;
}

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
        PWSTR directory = nullptr;
        if (!name || !*name)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else
            error = MimeMsiDirectoryPath(package, name, FALSE, &directory);
        if (!error) {
            WCHAR value[96];
            directory_value(package, table, id, row, value);
            error = MimeMsiWriteRegistry(package, root, catalog, value, REG_SZ, directory,
                                         (DWORD)(wcslen(directory) + 1) * sizeof(WCHAR));
        }
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
        PWSTR directory = nullptr, current = nullptr;
        if (!name || !*name)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else {
            WCHAR value[96];
            directory_value(package, table, id, row, value);
            error = MimeMsiRegistryText(key, value, &directory);
        }
        /* a package from a source may have folders the installed one did not. */
        if (error == ERROR_FILE_NOT_FOUND && package->Recache) {
            error = ERROR_SUCCESS;
            continue;
        }
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

/* the letters of REINSTALLMODE in the order of the bits of MsiReinstallProduct. */
static constexpr WCHAR MODE_LETTERS[] = L"rpoedcamusv";

/* the mode of the install. a package without the property runs as omus, a property replaces the whole mode. */
UINT MimeMsiReinstallMode(MIME_MSI_PACKAGE *package, DWORD *mode)
{
    PCWSTR letters = MimeMsiGetProperty(package, L"REINSTALLMODE");
    *mode = REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA | REINSTALLMODE_SHORTCUT;
    if (!*letters)
        return ERROR_SUCCESS;
    *mode = 0;
    for (; *letters; letters++) {
        if (*letters == L' ')
            continue;
        WCHAR code = *letters >= L'A' && *letters <= L'Z' ? *letters + L'a' - L'A' : *letters;
        PCWSTR found = wcschr(MODE_LETTERS, code);
        if (!found)
            return ERROR_INSTALL_FAILURE;
        *mode |= 1u << (found - MODE_LETTERS);
    }
    return ERROR_SUCCESS;
}

BOOL MimeMsiModeAllows(MIME_MSI_PACKAGE *package, DWORD flag)
{
    DWORD mode;
    return MimeMsiReinstallMode(package, &mode) || (mode & flag);
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
