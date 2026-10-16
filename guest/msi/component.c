#include "msi_private.h"

static constexpr WCHAR component_catalog[] = L"Software\\Mime\\Installer\\Components";

static UINT component_key_path(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR directory, PCWSTR key, PWSTR *out)
{
    PWSTR folder = nullptr;
    UINT error = MimeMsiDirectoryPath(package, directory, FALSE, &folder);
    if (error)
        return error;
    if (!key || !*key) {
        *out = folder;
        return ERROR_SUCCESS;
    }
    MIME_MSI_TABLE *files;
    error = MimeMsiLoadTable(package->Database, L"File", &files);
    if (!error) {
        ULONG id = MimeMsiColumn(files, L"File", TRUE);
        ULONG component = MimeMsiColumn(files, L"Component_", TRUE);
        ULONG filename = MimeMsiColumn(files, L"FileName", TRUE);
        error = ERROR_INSTALL_PACKAGE_INVALID;
        if (id != UINT32_MAX && component != UINT32_MAX && filename != UINT32_MAX) {
            for (ULONG row = 0; row < files->Rows; row++) {
                PCWSTR value = MimeMsiText(package->Database, files, row, id);
                if (!value || wcscmp(value, key))
                    continue;
                PCWSTR owner = MimeMsiText(package->Database, files, row, component);
                value = MimeMsiText(package->Database, files, row, filename);
                if (!owner || wcscmp(owner, name) || !value)
                    break;
                PCWSTR long_name = wcschr(value, L'|');
                if (long_name)
                    value = long_name + 1;
                if (!*value || !wcscmp(value, L".") || !wcscmp(value, L"..") ||
                    wcschr(value, L'\\') || wcschr(value, L'/') || wcschr(value, L':'))
                    break;
                *out = MimeMsiJoinPath(folder, value);
                error = *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
        }
    }
    MimeMsiFree(folder);
    return error;
}

UINT MimeMsiProcessComponents(MIME_MSI_PACKAGE *package, HKEY root)
{
    if (!package->Maintenance && (*MimeMsiGetProperty(package, L"Installed") || *MimeMsiGetProperty(package, L"REINSTALL")))
        return ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR product[33];
    if (!MimeMsiPackGuid(MimeMsiGetProperty(package, L"ProductCode"), product))
        return ERROR_INSTALL_PACKAGE_INVALID;
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Component", &table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(table, L"Component", TRUE);
    ULONG code = MimeMsiColumn(table, L"ComponentId", TRUE);
    ULONG key_column = MimeMsiColumn(table, L"KeyPath", TRUE);
    if (id == UINT32_MAX || code == UINT32_MAX || key_column == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        PCWSTR guid = MimeMsiText(package->Database, table, row, code);
        if (!name || !*name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (!guid || !*guid)
            continue;
        WCHAR packed[33], path[128];
        if (!MimeMsiPackGuid(guid, packed))
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL selected;
        PCWSTR directory = nullptr;
        error = MimeMsiComponentSelected(package, name, &directory, &selected);
        if (error)
            return error;
        if (!selected)
            continue;
        PWSTR key_path = nullptr;
        error = component_key_path(package, name, directory, MimeMsiText(package->Database, table, row, key_column), &key_path);
        if (!error) {
            _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\%s", component_catalog, packed);
            error = MimeMsiWriteRegistry(package, root, path, product, REG_SZ, key_path,
                                         (DWORD)((wcslen(key_path) + 1) * sizeof(WCHAR)));
        }
        MimeMsiFree(key_path);
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

static UINT open_component_catalogs(PCWSTR component, HKEY keys[2])
{
    WCHAR path[128];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%s%s%s", component_catalog, component ? L"\\" : L"", component ? component : L"");
    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (ULONG i = 0; i < 2; i++) {
        UINT error = RegOpenKeyExW(roots[i], path, 0, KEY_READ, &keys[i]);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            continue;
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

static UINT enumerate_components(PCWSTR component, DWORD index, PWSTR out)
{
    HKEY keys[2] = {};
    UINT error = open_component_catalogs(component, keys);
    if (error)
        goto done;
    error = component && !keys[0] && !keys[1] ? ERROR_UNKNOWN_COMPONENT : ERROR_NO_MORE_ITEMS;
    for (ULONG context = 0; context < 2; context++) {
        if (!keys[context])
            continue;
        for (DWORD entry = 0;; entry++) {
            WCHAR packed[33], guid[39];
            DWORD length = RTL_NUMBER_OF(packed);
            UINT status = component ? RegEnumValueW(keys[context], entry, packed, &length, nullptr, nullptr, nullptr, nullptr) :
                                      RegEnumKeyExW(keys[context], entry, packed, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status || !MimeMsiUnpackGuid(packed, guid)) {
                error = status && status != ERROR_MORE_DATA ? status : ERROR_BAD_CONFIGURATION;
                goto done;
            }
            if (context && keys[0]) {
                if (component) {
                    if (RegQueryValueExW(keys[0], packed, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
                        continue;
                } else {
                    HKEY duplicate;
                    if (!RegOpenKeyExW(keys[0], packed, 0, KEY_READ, &duplicate)) {
                        RegCloseKey(duplicate);
                        continue;
                    }
                }
            }
            if (index) {
                index--;
                continue;
            }
            wcscpy(out, guid);
            error = ERROR_SUCCESS;
            goto done;
        }
    }
done:
    for (ULONG i = 0; i < 2; i++)
        if (keys[i])
            RegCloseKey(keys[i]);
    return error;
}

UINT WINAPI MsiEnumComponentsW(DWORD index, LPWSTR component)
{
    return component ? enumerate_components(nullptr, index, component) : ERROR_INVALID_PARAMETER;
}

UINT WINAPI MsiEnumClientsW(LPCWSTR component, DWORD index, LPWSTR product)
{
    WCHAR packed[33];
    if (!product || !MimeMsiPackGuid(component, packed))
        return ERROR_INVALID_PARAMETER;
    return enumerate_components(packed, index, product);
}

INSTALLSTATE WINAPI MsiGetComponentPathW(LPCWSTR product, LPCWSTR component, LPWSTR out, LPDWORD size)
{
    WCHAR product_code[33], component_code[33];
    if ((out && !size) || !MimeMsiPackGuid(product, product_code) || !MimeMsiPackGuid(component, component_code))
        return INSTALLSTATE_INVALIDARG;
    HKEY keys[2] = {};
    UINT error = open_component_catalogs(component_code, keys);
    PWSTR path = nullptr;
    if (!error) {
        error = ERROR_FILE_NOT_FOUND;
        for (ULONG context = 0; context < 2; context++) {
            if (!keys[context])
                continue;
            error = MimeMsiRegistryText(keys[context], product_code, &path);
            if (error != ERROR_FILE_NOT_FOUND)
                break;
        }
    }
    for (ULONG i = 0; i < 2; i++)
        if (keys[i])
            RegCloseKey(keys[i]);
    if (error)
        return error == ERROR_FILE_NOT_FOUND ? INSTALLSTATE_UNKNOWN : INSTALLSTATE_BADCONFIG;
    INSTALLSTATE state = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? INSTALLSTATE_ABSENT : INSTALLSTATE_LOCAL;
    if (out && MimeMsiCopyString(path, out, size) == ERROR_MORE_DATA)
        state = INSTALLSTATE_MOREDATA;
    else if (!out && size)
        *size = (DWORD)wcslen(path);
    MimeMsiFree(path);
    return state;
}
