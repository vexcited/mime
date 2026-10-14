#include "msi_private.h"

static UINT write_text(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name, PCWSTR value)
{
    SIZE_T characters = wcslen(value) + 1;
    if (characters > UINT32_MAX / sizeof(WCHAR))
        return ERROR_NOT_ENOUGH_MEMORY;
    return MimeMsiWriteRegistry(package, root, path, name, REG_SZ, value, (DWORD)(characters * sizeof(WCHAR)));
}

static UINT register_product(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    if (package->CatalogRegistered)
        return ERROR_SUCCESS;
    if (package->Maintenance) {
        UINT error = MimeMsiValidateMaintenance(package);
        if (!error)
            package->CatalogRegistered = TRUE;
        return error;
    }
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    INSTALLSTATE previous = MsiQueryProductStateW(product);
    if (previous != INSTALLSTATE_UNKNOWN)
        return ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR packed[33], cache[MAX_PATH];
    if (!MimeMsiPackGuid(product, packed))
        return ERROR_INSTALL_PACKAGE_INVALID;
    UINT length = GetWindowsDirectoryW(cache, RTL_NUMBER_OF(cache));
    if (!length || length + 48 >= RTL_NUMBER_OF(cache))
        return ERROR_BAD_PATHNAME;
    _snwprintf(cache + length, RTL_NUMBER_OF(cache) - length, L"\\Installer\\%s.msi", packed);
    UINT error = MimeMsiWriteInstalledFile(package, package->Database->Data, package->Database->File.Size, cache);
    if (error)
        return error;
    static const struct { PCWSTR Name, Property; } fields[] = {
        {L"ProductName", L"ProductName"}, {L"VersionString", L"ProductVersion"},
        {L"Publisher", L"Manufacturer"}, {L"Language", L"ProductLanguage"},
        {L"InstallSource", L"SourceDir"}, {L"InstallLocation", L"ARPINSTALLLOCATION"},
        {L"HelpLink", L"ARPHELPLINK"}, {L"HelpTelephone", L"ARPHELPTELEPHONE"},
        {L"URLInfoAbout", L"ARPURLINFOABOUT"}, {L"URLUpdateInfo", L"ARPURLUPDATEINFO"},
        {L"RegOwner", L"USERNAME"}, {L"RegCompany", L"COMPANYNAME"}, {L"ProductID", L"ProductID"}
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(fields) && !error; i++)
        error = write_text(package, root, path, fields[i].Name, MimeMsiGetProperty(package, fields[i].Property));
    if (!error)
        error = write_text(package, root, path, L"LocalPackage", cache);
    PCWSTR filename = wcsrchr(package->Database->Path, L'\\');
    if (!error)
        error = write_text(package, root, path, L"PackageName", filename ? filename + 1 : package->Database->Path);
    if (!error) {
        SYSTEMTIME time;
        WCHAR date[16];
        GetLocalTime(&time);
        _snwprintf(date, RTL_NUMBER_OF(date), L"%04u%02u%02u", time.wYear, time.wMonth, time.wDay);
        error = write_text(package, root, path, L"InstallDate", date);
    }
    if (!error) {
        DWORD state = INSTALLSTATE_DEFAULT;
        error = MimeMsiWriteRegistry(package, root, path, L"State", REG_DWORD, &state, sizeof(state));
    }
    if (!error)
        error = MimeMsiCacheTransforms(package, root, path, cache);
    if (!error)
        error = MimeMsiSaveDirectories(package, root, path);
    if (!error)
        error = MimeMsiRegisterSource(package, root, path);
    if (!error)
        package->CatalogRegistered = TRUE;
    return error;
}

static UINT publish_features(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Feature", &table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(table, L"Feature", TRUE);
    ULONG parent = MimeMsiColumn(table, L"Feature_Parent", TRUE);
    if (id == UINT32_MAX || parent == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    WCHAR features[128], parents[128];
    _snwprintf(features, RTL_NUMBER_OF(features), L"%s\\Features", path);
    _snwprintf(parents, RTL_NUMBER_OF(parents), L"%s\\FeatureParents", path);
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        PCWSTR parent_name = MimeMsiText(package->Database, table, row, parent);
        if (!name || !*name || wcslen(name) > 38 || (parent_name && wcslen(parent_name) > 38))
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL selected;
        error = MimeMsiFeatureSelected(package, table, name, 0, &selected);
        if (error)
            return error;
        DWORD state = selected ? INSTALLSTATE_LOCAL : INSTALLSTATE_ABSENT;
        error = MimeMsiWriteRegistry(package, root, features, name, REG_DWORD, &state, sizeof(state));
        if (!error)
            error = write_text(package, root, parents, name, parent_name ? parent_name : L"");
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

static UINT publish_product(MIME_MSI_PACKAGE *package, HKEY root)
{
    PCWSTR upgrade = MimeMsiGetProperty(package, L"UpgradeCode");
    if (!*upgrade)
        return ERROR_SUCCESS;
    WCHAR packed_upgrade[33], packed_product[33], path[160];
    if (!MimeMsiPackGuid(upgrade, packed_upgrade) ||
        !MimeMsiPackGuid(MimeMsiGetProperty(package, L"ProductCode"), packed_product))
        return ERROR_INSTALL_PACKAGE_INVALID;
    _snwprintf(path, RTL_NUMBER_OF(path), root == HKEY_LOCAL_MACHINE ?
               L"Software\\Classes\\Installer\\UpgradeCodes\\%s" :
               L"Software\\Microsoft\\Installer\\UpgradeCodes\\%s", packed_upgrade);
    return write_text(package, root, path, packed_product, L"");
}

UINT MimeMsiRegistrationAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    BOOL registration = !wcscmp(action, L"RegisterProduct");
    BOOL features = !wcscmp(action, L"PublishFeatures");
    BOOL publish = !wcscmp(action, L"PublishProduct");
    BOOL components = !wcscmp(action, L"ProcessComponents");
    if (!registration && !features && !publish && !components)
        return UINT32_MAX;
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    if (package->Maintenance) {
        UINT error = MimeMsiValidateMaintenance(package);
        if (error)
            return error;
    }
    PCWSTR all_users = MimeMsiGetProperty(package, L"ALLUSERS");
    if (*all_users && wcscmp(all_users, L"1"))
        return ERROR_CALL_NOT_IMPLEMENTED;
    HKEY root = *all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    WCHAR path[96];
    UINT error = MimeMsiCatalogPath(MimeMsiGetProperty(package, L"ProductCode"), path);
    if (error)
        return ERROR_INSTALL_PACKAGE_INVALID;
    if (components)
        return MimeMsiProcessComponents(package, root);
    if (registration)
        return register_product(package, root, path);
    if (!package->CatalogRegistered)
        return ERROR_FUNCTION_NOT_CALLED;
    return features ? publish_features(package, root, path) : publish_product(package, root);
}
