#include "msi_private.h"

static constexpr WCHAR component_catalog[] = L"Software\\Mime\\Installer\\Components";

/* the key path of a component that is a registry value: the root as two digits, a colon, the key and the name. */
static UINT registry_key_path(MIME_MSI_PACKAGE *package, PCWSTR registry_key, BOOL is64, PWSTR *out)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Registry", &table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(table, L"Registry", TRUE);
    ULONG root_column = MimeMsiColumn(table, L"Root", FALSE);
    ULONG key_column = MimeMsiColumn(table, L"Key", TRUE);
    ULONG name_column = MimeMsiColumn(table, L"Name", TRUE);
    if (id == UINT32_MAX || root_column == UINT32_MAX || key_column == UINT32_MAX || name_column == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR candidate = MimeMsiText(package->Database, table, row, id);
        if (!candidate || wcscmp(candidate, registry_key))
            continue;
        int root = MimeMsiInteger(table, row, root_column);
        if (root == -1)
            root = *MimeMsiGetProperty(package, L"ALLUSERS") ? 2 : 1;
        PWSTR key = nullptr, name = nullptr;
        error = MimeMsiFormat(package, MimeMsiText(package->Database, table, row, key_column), &key);
        if (!error)
            error = MimeMsiFormat(package, MimeMsiText(package->Database, table, row, name_column), &name);
        if (!error) {
            SIZE_T length = wcslen(key) + wcslen(name) + 8;
            *out = MimeMsiAllocate(length * sizeof(WCHAR));
            if (!*out)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else if (*name && wcscmp(name, L"+") && wcscmp(name, L"-") && wcscmp(name, L"*"))
                _snwprintf(*out, length, L"%02d:\\%s\\%s", root + (is64 ? 20 : 0), key, name);
            else
                _snwprintf(*out, length, L"%02d:\\%s\\", root + (is64 ? 20 : 0), key);
        }
        MimeMsiFree(key);
        MimeMsiFree(name);
        return error;
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

static UINT component_key_path(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR directory, PCWSTR key, int attributes,
                               PWSTR *out)
{
    if (key && *key && attributes != MSI_NULL_INTEGER && (attributes & 4))
        return registry_key_path(package, key, !!(attributes & 256), out);
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
    ULONG attributes_column = MimeMsiColumn(table, L"Attributes", FALSE);
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
        if (!selected) {
            /* a component the product no longer has gives up its place in the catalog. */
            _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\%s", component_catalog, packed);
            error = MimeMsiDeleteRegistryValue(package, root, path, product);
            HKEY emptied;
            if (!error && !RegOpenKeyExW(root, path, 0, KEY_READ, &emptied)) {
                DWORD values = 1;
                RegQueryInfoKeyW(emptied, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
                RegCloseKey(emptied);
                if (!values)
                    RegDeleteKeyW(root, path);
            }
            if (error)
                return error;
            continue;
        }
        PWSTR key_path = nullptr;
        error = component_key_path(package, name, directory, MimeMsiText(package->Database, table, row, key_column),
                                   attributes_column == UINT32_MAX ? 0 : MimeMsiInteger(table, row, attributes_column), &key_path);
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

/* whether the place a key path names is there: a file, a directory, or a registry key or value. */
static BOOL key_path_present(PCWSTR path)
{
    if (path[0] >= L'0' && path[0] <= L'9' && path[1] >= L'0' && path[1] <= L'9' && path[2] == L':' && path[3] == L'\\') {
        int number = (path[0] - L'0') * 10 + (path[1] - L'0');
        BOOL is64 = number >= 20;
        HKEY roots[] = {HKEY_CLASSES_ROOT, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, HKEY_USERS};
        if (number % 20 > 3)
            return FALSE;
        HKEY root = roots[number % 20];
        PWSTR copy = MimeMsiDuplicate(path + 4);
        if (!copy)
            return FALSE;
        SIZE_T length = wcslen(copy);
        PWSTR name = nullptr;
        if (length && copy[length - 1] == L'\\') {
            copy[length - 1] = 0;
        } else {
            name = wcsrchr(copy, L'\\');
            if (name)
                *name++ = 0;
        }
        DWORD view = 0;
#ifdef _WIN64
        view = is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
#else
        (void)is64;
#endif
        HKEY key;
        BOOL found = !RegOpenKeyExW(root, copy, 0, KEY_QUERY_VALUE | view, &key);
        if (found && name)
            found = !RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr);
        if (found)
            RegCloseKey(key);
        MimeMsiFree(copy);
        return found;
    }
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
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
    INSTALLSTATE state = key_path_present(path) ? INSTALLSTATE_LOCAL : INSTALLSTATE_ABSENT;
    if (out && MimeMsiCopyString(path, out, size) == ERROR_MORE_DATA)
        state = INSTALLSTATE_MOREDATA;
    else if (!out && size)
        *size = (DWORD)wcslen(path);
    MimeMsiFree(path);
    return state;
}
