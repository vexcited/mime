#include "msi_private.h"

static UINT write_text(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name, PCWSTR value)
{
    SIZE_T characters = wcslen(value) + 1;
    if (characters > UINT32_MAX / sizeof(WCHAR))
        return ERROR_NOT_ENOUGH_MEMORY;
    return MimeMsiWriteRegistry(package, root, path, name, REG_SZ, value, (DWORD)(characters * sizeof(WCHAR)));
}

static BOOL removing_product(const MIME_MSI_PACKAGE *package)
{
    return package->Maintenance && !_wcsicmp(MimeMsiGetProperty(package, L"REMOVE"), L"ALL");
}

static UINT uninstall_key(MIME_MSI_PACKAGE *package, WCHAR path[160])
{
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    if (wcslen(product) != 38)
        return ERROR_INSTALL_PACKAGE_INVALID;
    _snwprintf(path, 160, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%s", product);
    return ERROR_SUCCESS;
}

/* uninstall entries live in the native registry view for a 64-bit package. */
static DWORD uninstall_view(const MIME_MSI_PACKAGE *package)
{
    return package->Is64Bit ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
}

static UINT write_number(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name, DWORD value)
{
    return MimeMsiWriteRegistry(package, root, path, name, REG_DWORD, &value, sizeof(value));
}

/* writes the installed programs entry from the package properties. */
static UINT write_uninstall_entry(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR cache)
{
    WCHAR path[160];
    UINT error = uninstall_key(package, path);
    if (error)
        return error;
    DWORD previous = package->RegistryView;
    package->RegistryView = uninstall_view(package);
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    PCWSTR version = MimeMsiGetProperty(package, L"ProductVersion");
    error = write_text(package, root, path, L"DisplayName", MimeMsiGetProperty(package, L"ProductName"));
    if (!error)
        error = write_text(package, root, path, L"DisplayVersion", version);
    if (!error)
        error = write_text(package, root, path, L"Publisher", MimeMsiGetProperty(package, L"Manufacturer"));
    static const struct { PCWSTR Name, Property; } texts[] = {
        {L"InstallLocation", L"ARPINSTALLLOCATION"}, {L"HelpLink", L"ARPHELPLINK"}, {L"HelpTelephone", L"ARPHELPTELEPHONE"},
        {L"URLInfoAbout", L"ARPURLINFOABOUT"}, {L"URLUpdateInfo", L"ARPURLUPDATEINFO"}, {L"Comments", L"ARPCOMMENTS"},
        {L"Contact", L"ARPCONTACT"}, {L"Readme", L"ARPREADME"}
    };
    for (ULONG i = 0; !error && i < RTL_NUMBER_OF(texts); i++)
        if (*MimeMsiGetProperty(package, texts[i].Property))
            error = write_text(package, root, path, texts[i].Name, MimeMsiGetProperty(package, texts[i].Property));
    if (!error)
        error = write_text(package, root, path, L"InstallSource", MimeMsiGetProperty(package, L"SourceDir"));
    if (!error) {
        SYSTEMTIME time;
        WCHAR date[16];
        GetLocalTime(&time);
        _snwprintf(date, RTL_NUMBER_OF(date), L"%04u%02u%02u", time.wYear, time.wMonth, time.wDay);
        error = write_text(package, root, path, L"InstallDate", date);
    }
    WCHAR command[96];
    BOOL remove_allowed = !*MimeMsiGetProperty(package, L"ARPNOREMOVE");
    BOOL modify_allowed = !*MimeMsiGetProperty(package, L"ARPNOMODIFY");
    if (!error && remove_allowed) {
        _snwprintf(command, RTL_NUMBER_OF(command), L"MsiExec.exe /X%s", product);
        error = write_text(package, root, path, L"UninstallString", command);
    } else if (!error)
        error = write_number(package, root, path, L"NoRemove", 1);
    if (!error && modify_allowed) {
        _snwprintf(command, RTL_NUMBER_OF(command), L"MsiExec.exe /I%s", product);
        error = write_text(package, root, path, L"ModifyPath", command);
    } else if (!error)
        error = write_number(package, root, path, L"NoModify", 1);
    if (!error && *MimeMsiGetProperty(package, L"ARPNOREPAIR"))
        error = write_number(package, root, path, L"NoRepair", 1);
    if (!error && *MimeMsiGetProperty(package, L"ARPSYSTEMCOMPONENT"))
        error = write_number(package, root, path, L"SystemComponent", 1);
    if (!error)
        error = write_number(package, root, path, L"WindowsInstaller", 1);
    DWORD major = 0, minor = 0, build = 0;
    PCWSTR p = version;
    while (*p >= L'0' && *p <= L'9')
        major = major * 10 + (DWORD)(*p++ - L'0');
    if (*p == L'.')
        p++;
    while (*p >= L'0' && *p <= L'9')
        minor = minor * 10 + (DWORD)(*p++ - L'0');
    if (*p == L'.')
        p++;
    while (*p >= L'0' && *p <= L'9')
        build = build * 10 + (DWORD)(*p++ - L'0');
    if (!error)
        error = write_number(package, root, path, L"VersionMajor", major);
    if (!error)
        error = write_number(package, root, path, L"VersionMinor", minor);
    if (!error)
        error = write_number(package, root, path, L"Version", (major << 24) | ((minor & 0xFF) << 16) | (build & 0xFFFF));
    PCWSTR language = MimeMsiGetProperty(package, L"ProductLanguage");
    DWORD lang = 0;
    while (*language >= L'0' && *language <= L'9')
        lang = lang * 10 + (DWORD)(*language++ - L'0');
    if (!error)
        error = write_number(package, root, path, L"Language", lang);
    if (!error && *MimeMsiGetProperty(package, L"ARPSIZE")) {
        DWORD size = 0;
        for (PCWSTR digit = MimeMsiGetProperty(package, L"ARPSIZE"); *digit >= L'0' && *digit <= L'9'; digit++)
            size = size * 10 + (DWORD)(*digit - L'0');
        error = write_number(package, root, path, L"EstimatedSize", size);
    }
    /* the list icon is written from the Icon table into the installer folder. */
    PCWSTR icon = MimeMsiGetProperty(package, L"ARPPRODUCTICON");
    if (!error && *icon) {
        WCHAR stream[256];
        BYTE *bytes = nullptr;
        SIZE_T size = 0;
        _snwprintf(stream, RTL_NUMBER_OF(stream), L"Icon.%s", icon);
        if (!MimeMsiReadBinary(package->Database, stream, &bytes, &size)) {
            WCHAR file[MAX_PATH];
            UINT length = GetWindowsDirectoryW(file, RTL_NUMBER_OF(file));
            if (length && length + wcslen(product) + wcslen(icon) + 16 < RTL_NUMBER_OF(file)) {
                _snwprintf(file + length, RTL_NUMBER_OF(file) - length, L"\\Installer\\%s\\%s", product, icon);
                error = MimeMsiWriteInstalledFile(package, bytes, size, file);
                if (!error)
                    error = write_text(package, root, path, L"DisplayIcon", file);
            }
        }
        MimeMsiFree(bytes);
    }
    package->RegistryView = previous;
    (void)cache;
    return error;
}

static UINT unregister_product(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    HKEY key;
    UINT error = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return ERROR_SUCCESS;
    if (error)
        return error;
    /* the cached package and its transforms are machine files like any other. */
    PWSTR local = nullptr;
    if (!MimeMsiRegistryText(key, L"LocalPackage", &local) && local) {
        error = MimeMsiRemoveFile(package, local);
        HKEY transforms;
        if (!error && !RegOpenKeyExW(key, L"Transforms", 0, KEY_READ, &transforms)) {
            DWORD count = 0, size = sizeof(count);
            RegQueryValueExW(transforms, L"Count", nullptr, nullptr, (BYTE *)&count, &size);
            for (DWORD index = 1; !error && index <= count && index <= 256; index++) {
                WCHAR name[32];
                PWSTR file = nullptr;
                _snwprintf(name, RTL_NUMBER_OF(name), L"%u", index);
                if (!MimeMsiRegistryText(transforms, name, &file) && file)
                    error = MimeMsiRemoveFile(package, file);
                MimeMsiFree(file);
            }
            RegCloseKey(transforms);
        }
    }
    MimeMsiFree(local);
    RegCloseKey(key);
    if (!error)
        error = MimeMsiDeleteRegistryTree(package, root, path, 0);
    /* the installed programs entry and its icon. */
    WCHAR entry[160];
    if (!error && !uninstall_key(package, entry)) {
        DWORD previous = package->RegistryView;
        package->RegistryView = uninstall_view(package);
        error = MimeMsiDeleteRegistryTree(package, root, entry, 0);
        package->RegistryView = previous;
    }
    PCWSTR icon = MimeMsiGetProperty(package, L"ARPPRODUCTICON");
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    if (!error && *icon) {
        WCHAR file[MAX_PATH];
        UINT length = GetWindowsDirectoryW(file, RTL_NUMBER_OF(file));
        if (length && length + wcslen(product) + wcslen(icon) + 16 < RTL_NUMBER_OF(file)) {
            _snwprintf(file + length, RTL_NUMBER_OF(file) - length, L"\\Installer\\%s\\%s", product, icon);
            error = MimeMsiRemoveFile(package, file);
            PWSTR separator = wcsrchr(file, L'\\');
            if (!error && separator) {
                *separator = 0;
                error = MimeMsiRemoveDirectory(package, file);
            }
        }
    }
    return error;
}

/* what the package says about the product, as the list of programs shows it. */
static UINT write_product_fields(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    static const struct { PCWSTR Name, Property; } fields[] = {
        {L"ProductName", L"ProductName"}, {L"VersionString", L"ProductVersion"},
        {L"Publisher", L"Manufacturer"}, {L"Language", L"ProductLanguage"},
        {L"InstallSource", L"SourceDir"}, {L"InstallLocation", L"ARPINSTALLLOCATION"},
        {L"HelpLink", L"ARPHELPLINK"}, {L"HelpTelephone", L"ARPHELPTELEPHONE"},
        {L"URLInfoAbout", L"ARPURLINFOABOUT"}, {L"URLUpdateInfo", L"ARPURLUPDATEINFO"},
        {L"RegOwner", L"USERNAME"}, {L"RegCompany", L"COMPANYNAME"}, {L"ProductID", L"ProductID"}
    };
    UINT error = ERROR_SUCCESS;
    for (ULONG i = 0; i < RTL_NUMBER_OF(fields) && !error; i++)
        error = write_text(package, root, path, fields[i].Name, MimeMsiGetProperty(package, fields[i].Property));
    return error;
}

/* a package that is not the cached one replaces it, and its source becomes the one the product reads from. */
UINT MimeMsiRecachePackage(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    HKEY key;
    PWSTR cache = nullptr;
    UINT error = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
    if (!error) {
        error = MimeMsiRegistryText(key, L"LocalPackage", &cache);
        RegCloseKey(key);
    }
    if (!error && _wcsicmp(cache, package->Database->Path)) {
        error = MimeMsiWriteInstalledFile(package, package->Database->Data, package->Database->File.Size, cache);
        if (!error)
            error = write_product_fields(package, root, path);
        if (!error)
            error = write_text(package, root, path, L"LocalPackage", cache);
        if (!error)
            error = MimeMsiRegisterSource(package, root, path);
    }
    MimeMsiFree(cache);
    return error;
}

static UINT register_product(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    if (package->CatalogRegistered)
        return ERROR_SUCCESS;
    if (removing_product(package)) {
        UINT error = MimeMsiValidateMaintenance(package);
        if (!error)
            error = unregister_product(package, root, path);
        if (!error)
            package->CatalogRegistered = TRUE;
        return error;
    }
    if (package->Maintenance) {
        UINT error = MimeMsiValidateMaintenance(package);
        if (!error && package->Recache)
            error = MimeMsiRecachePackage(package, root, path);
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
    error = write_product_fields(package, root, path);
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
        error = MimeMsiCachePatches(package, root, path, cache);
    if (!error)
        error = MimeMsiSaveDirectories(package, root, path);
    if (!error)
        error = MimeMsiRegisterSource(package, root, path);
    if (!error)
        error = write_uninstall_entry(package, root, cache);
    if (!error)
        package->CatalogRegistered = TRUE;
    return error;
}

static UINT publish_features(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    if (removing_product(package))
        return ERROR_SUCCESS;
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
    if (removing_product(package)) {
        WCHAR packed_upgrade[33], packed_product[33], path[160];
        if (!MimeMsiPackGuid(upgrade, packed_upgrade) ||
            !MimeMsiPackGuid(MimeMsiGetProperty(package, L"ProductCode"), packed_product))
            return ERROR_INSTALL_PACKAGE_INVALID;
        _snwprintf(path, RTL_NUMBER_OF(path), root == HKEY_LOCAL_MACHINE ?
                   L"Software\\Classes\\Installer\\UpgradeCodes\\%s" :
                   L"Software\\Microsoft\\Installer\\UpgradeCodes\\%s", packed_upgrade);
        return MimeMsiDeleteRegistryValue(package, root, path, packed_product);
    }
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
    BOOL unpublish = !wcscmp(action, L"UnpublishFeatures");
    BOOL qualified = !wcscmp(action, L"PublishComponents"), unqualified = !wcscmp(action, L"UnpublishComponents");
    if (unpublish)
        return ERROR_SUCCESS;
    if (!registration && !features && !publish && !components && !qualified && !unqualified)
        return UINT32_MAX;
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    /* an unregistered product has no catalog left to validate maintenance against. */
    if (package->Maintenance && !(removing_product(package) && package->CatalogRegistered)) {
        UINT error = MimeMsiValidateMaintenance(package);
        if (error)
            return error;
    }
    PCWSTR all_users = MimeMsiGetProperty(package, L"ALLUSERS");
    /* 2 means all users when possible, which it is for an administrator. */
    if (*all_users && wcscmp(all_users, L"1") && wcscmp(all_users, L"2"))
        return ERROR_CALL_NOT_IMPLEMENTED;
    HKEY root = *all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    WCHAR path[96];
    UINT error = MimeMsiCatalogPath(MimeMsiGetProperty(package, L"ProductCode"), path);
    if (error)
        return ERROR_INSTALL_PACKAGE_INVALID;
    if (components)
        return MimeMsiProcessComponents(package, root);
    if (qualified || unqualified)
        return MimeMsiPublishComponents(package, root, qualified, removing_product(package));
    if (registration)
        return register_product(package, root, path);
    if (!package->CatalogRegistered)
        return ERROR_FUNCTION_NOT_CALLED;
    return features ? publish_features(package, root, path) : publish_product(package, root);
}
