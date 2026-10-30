#include "msi_private.h"

UINT MimeMsiCatalogPath(PCWSTR product, WCHAR path[96])
{
    WCHAR packed[33];
    if (!MimeMsiPackGuid(product, packed))
        return ERROR_INVALID_PARAMETER;
    _snwprintf(path, 96, L"Software\\Classes\\Installer\\Products\\%s", packed);
    return ERROR_SUCCESS;
}

static UINT open_product(PCWSTR product, HKEY *out, BOOL *machine)
{
    WCHAR path[96];
    UINT error = MimeMsiCatalogPath(product, path);
    if (error)
        return error;
    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots); i++) {
        error = RegOpenKeyExW(roots[i], path, 0, KEY_READ, out);
        if (!error) {
            if (machine)
                *machine = i != 0;
            return ERROR_SUCCESS;
        }
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return error;
    }
    return ERROR_UNKNOWN_PRODUCT;
}

static UINT read_state(HKEY key, PCWSTR name, INSTALLSTATE *state)
{
    DWORD value, type, bytes = sizeof(value);
    UINT error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &bytes);
    if (!error && (bytes != sizeof(value) || type != REG_DWORD))
        error = ERROR_BAD_CONFIGURATION;
    if (!error) {
        *state = (INSTALLSTATE)(LONG)value;
        if (*state < INSTALLSTATE_NOTUSED || *state > INSTALLSTATE_DEFAULT)
            error = ERROR_BAD_CONFIGURATION;
    }
    return error;
}

INSTALLSTATE WINAPI MsiQueryProductStateW(LPCWSTR product)
{
    HKEY key;
    UINT error = open_product(product, &key, nullptr);
    if (error)
        return error == ERROR_INVALID_PARAMETER ? INSTALLSTATE_INVALIDARG :
               error == ERROR_UNKNOWN_PRODUCT ? INSTALLSTATE_UNKNOWN : INSTALLSTATE_BADCONFIG;
    INSTALLSTATE state;
    error = read_state(key, L"State", &state);
    RegCloseKey(key);
    return error ? INSTALLSTATE_BADCONFIG : state;
}

INSTALLSTATE WINAPI MsiQueryFeatureStateW(LPCWSTR product, LPCWSTR feature)
{
    if (!feature || !*feature || wcslen(feature) > 38)
        return INSTALLSTATE_INVALIDARG;
    HKEY key, features;
    UINT error = open_product(product, &key, nullptr);
    if (error)
        return error == ERROR_INVALID_PARAMETER ? INSTALLSTATE_INVALIDARG :
               error == ERROR_UNKNOWN_PRODUCT ? INSTALLSTATE_UNKNOWN : INSTALLSTATE_BADCONFIG;
    error = RegOpenKeyExW(key, L"Features", 0, KEY_READ, &features);
    RegCloseKey(key);
    INSTALLSTATE state = INSTALLSTATE_UNKNOWN;
    if (!error) {
        error = read_state(features, feature, &state);
        RegCloseKey(features);
    }
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return INSTALLSTATE_UNKNOWN;
    return error ? INSTALLSTATE_BADCONFIG : state;
}

UINT MimeMsiRegistryText(HKEY key, PCWSTR name, PWSTR *out)
{
    DWORD bytes, type;
    UINT error = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (error)
        return error;
    if (type != REG_SZ || bytes % sizeof(WCHAR) || bytes > 1024 * 1024)
        return ERROR_BAD_CONFIGURATION;
    PWSTR text = MimeMsiAllocate((SIZE_T)bytes + sizeof(WCHAR));
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)text, &bytes);
    if (!error && (type != REG_SZ || bytes % sizeof(WCHAR)))
        error = ERROR_BAD_CONFIGURATION;
    if (error)
        MimeMsiFree(text);
    else
        *out = text;
    return error;
}

UINT WINAPI MsiGetProductInfoW(LPCWSTR product, LPCWSTR attribute, LPWSTR out, LPDWORD size)
{
    MIME_REMOTE(PROXY_GetProductInfoW, UINT, MIME_ARG(product), MIME_ARG(attribute), MIME_ARG(out), MIME_ARG(size));
    if (!attribute || (out && !size))
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    BOOL machine;
    UINT error = open_product(product, &key, &machine);
    if (error)
        return error;
    static const PCWSTR attributes[] = {
        L"ProductName", L"InstalledProductName", L"VersionString", L"Publisher", L"LocalPackage",
        L"InstallSource", L"InstallLocation", L"InstallDate", L"Language", L"InstalledLanguage",
        L"HelpLink", L"HelpTelephone", L"URLInfoAbout", L"URLUpdateInfo", L"Transforms",
        L"PackageName", L"ProductIcon", L"RegOwner", L"RegCompany", L"ProductID", L"PackageCode",
        L"Version", L"VersionMajor", L"VersionMinor", L"InstanceType", L"AuthorizedLUAApp"
    };
    PWSTR text = nullptr;
    if (!wcscmp(attribute, L"AssignmentType"))
        text = MimeMsiDuplicate(machine ? L"1" : L"0");
    else {
        BOOL known = FALSE;
        for (ULONG i = 0; i < RTL_NUMBER_OF(attributes); i++)
            known |= !wcscmp(attribute, attributes[i]);
        if (!known)
            error = ERROR_UNKNOWN_PROPERTY;
        else {
            PCWSTR value_name = !wcscmp(attribute, L"InstalledProductName") ? L"ProductName" :
                                !wcscmp(attribute, L"InstalledLanguage") ? L"Language" : attribute;
            error = MimeMsiRegistryText(key, value_name, &text);
            if (error == ERROR_FILE_NOT_FOUND) {
                text = MimeMsiDuplicate(!wcscmp(attribute, L"InstanceType") ||
                                       !wcscmp(attribute, L"AuthorizedLUAApp") ? L"0" : L"");
                error = ERROR_SUCCESS;
            }
        }
    }
    RegCloseKey(key);
    if (!error && !text)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        if (out)
            error = MimeMsiCopyString(text, out, size);
        else if (size)
            *size = (DWORD)wcslen(text);
    }
    MimeMsiFree(text);
    return error;
}

UINT WINAPI MsiEnumFeaturesW(LPCWSTR product, DWORD index, LPWSTR feature, LPWSTR parent)
{
    if (!feature)
        return ERROR_INVALID_PARAMETER;
    HKEY key, features;
    UINT error = open_product(product, &key, nullptr);
    if (error)
        return error;
    error = RegOpenKeyExW(key, L"Features", 0, KEY_READ, &features);
    if (!error) {
        DWORD length = 39;
        error = RegEnumValueW(features, index, feature, &length, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(features);
        if (error == ERROR_MORE_DATA)
            error = ERROR_BAD_CONFIGURATION;
    } else if (error == ERROR_FILE_NOT_FOUND)
        error = ERROR_NO_MORE_ITEMS;
    if (!error && parent) {
        HKEY parents;
        error = RegOpenKeyExW(key, L"FeatureParents", 0, KEY_READ, &parents);
        if (!error) {
            PWSTR text = nullptr;
            error = MimeMsiRegistryText(parents, feature, &text);
            if (!error && wcslen(text) > 38)
                error = ERROR_BAD_CONFIGURATION;
            if (!error)
                wcscpy(parent, text);
            MimeMsiFree(text);
            RegCloseKey(parents);
        }
        if (error == ERROR_FILE_NOT_FOUND) {
            *parent = 0;
            error = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return error;
}

/* the product from its cached package, or from the package of a source, which is then cached again. */
UINT MimeMsiOpenProductEx(PCWSTR product, PCWSTR package, MSIHANDLE *out)
{
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    HKEY key;
    BOOL machine;
    UINT error = open_product(product, &key, &machine);
    if (error)
        return error;
    PWSTR path = nullptr, source = nullptr, transforms = nullptr;
    MSIHANDLE database = 0;
    error = package ? ERROR_SUCCESS : MimeMsiRegistryText(key, L"LocalPackage", &path);
    if (!error && !package && (!*path || wcslen(path) > 32760))
        error = ERROR_BAD_CONFIGURATION;
    if (!error)
        error = MsiOpenDatabaseW(package ? package : path, MSIDBOPEN_READONLY, &database);
    if (!error)
        error = MimeMsiRestoreTransforms(database, key);
    if (!error)
        error = MimeMsiRestorePatches(database, key);
    if (!error) {
        WCHAR borrowed[32];
        _snwprintf(borrowed, RTL_NUMBER_OF(borrowed), L"#%u", database);
        error = MsiOpenPackageW(borrowed, out);
    }
    if (!error && !package)
        error = MimeMsiRegistryText(key, L"InstallSource", &source);
    if (!error && !package) {
        /* the source is where the package is found, which is not always where it was installed from. */
        WCHAR found[MAX_PATH];
        if (!MimeMsiFindPackageSource(product, found, RTL_NUMBER_OF(found))) {
            PWSTR separator = wcsrchr(found, L'\\');
            if (separator)
                separator[1] = 0;
            error = MsiSetPropertyW(*out, L"SourceDir", found);
        } else
            error = MsiSetPropertyW(*out, L"SourceDir", source);
    }
    if (!error) {
        error = MimeMsiRegistryText(key, L"Transforms", &transforms);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_SUCCESS;
        if (!error && transforms)
            error = MsiSetPropertyW(*out, L"TRANSFORMS", transforms);
    }
    if (!error) {
        AcquireSRWLockExclusive(&MimeMsiLock);
        MIME_MSI_PACKAGE *session = (void *)MimeMsiGetHandle(*out, MIME_MSI_KIND_PACKAGE);
        if (session)
            session->Recache = package != nullptr;
        error = session ? MimeMsiLoadMaintenance(session, key, product, machine) : ERROR_INVALID_HANDLE;
        ReleaseSRWLockExclusive(&MimeMsiLock);
    }
    if (error && *out) {
        MsiCloseHandle(*out);
        *out = 0;
    }
    if (database)
        MsiCloseHandle(database);
    MimeMsiFree(path);
    MimeMsiFree(source);
    MimeMsiFree(transforms);
    RegCloseKey(key);
    return error;
}

UINT WINAPI MsiOpenProductW(LPCWSTR product, MSIHANDLE *out)
{
    return MimeMsiOpenProductEx(product, nullptr, out);
}
