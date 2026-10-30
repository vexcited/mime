/*
 * qualified components. PublishComponents writes, for each qualifier of a category, a descriptor of the component
 * that serves it, followed by the row's application data. MsiProvideQualifiedComponent reads it back to the path of
 * the component. a descriptor is the product and the component as compressed GUIDs around the feature name.
 */
#include "msi_private.h"

static const char digits85[] = "!$%&'()*+,-.0123456789=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{}~";

static int hex_digit(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* the four words of a GUID as it lies in memory. */
static BOOL guid_words(PCWSTR text, ULONG words[4])
{
    static const UCHAR at[16] = {1, 3, 5, 7, 10, 12, 15, 17, 20, 22, 25, 27, 29, 31, 33, 35};
    UCHAR b[16];
    if (!text || wcslen(text) != 38 || text[0] != L'{' || text[9] != L'-' || text[14] != L'-' || text[19] != L'-' ||
        text[24] != L'-' || text[37] != L'}')
        return FALSE;
    for (ULONG i = 0; i < 16; i++) {
        int high = hex_digit(text[at[i]]), low = hex_digit(text[at[i] + 1]);
        if (high < 0 || low < 0)
            return FALSE;
        b[i] = (UCHAR)(high << 4 | low);
    }
    words[0] = (ULONG)b[0] << 24 | (ULONG)b[1] << 16 | (ULONG)b[2] << 8 | b[3];
    words[1] = (ULONG)b[4] << 8 | b[5] | (ULONG)b[6] << 24 | (ULONG)b[7] << 16;
    words[2] = b[8] | (ULONG)b[9] << 8 | (ULONG)b[10] << 16 | (ULONG)b[11] << 24;
    words[3] = b[12] | (ULONG)b[13] << 8 | (ULONG)b[14] << 16 | (ULONG)b[15] << 24;
    return TRUE;
}

/* five digits for each word, the lowest first. */
static BOOL compress_guid(PCWSTR guid, WCHAR out[21])
{
    ULONG words[4];
    if (!guid_words(guid, words))
        return FALSE;
    for (ULONG i = 0; i < 4; i++)
        for (ULONG j = 0; j < 5; j++) {
            out[i * 5 + j] = (WCHAR)digits85[words[i] % 85];
            words[i] /= 85;
        }
    out[20] = 0;
    return TRUE;
}

static BOOL expand_guid(PCWSTR text, WCHAR out[39])
{
    ULONG words[4];
    for (ULONG i = 0; i < 4; i++) {
        ULONGLONG value = 0, scale = 1;
        for (ULONG j = 0; j < 5; j++, scale *= 85) {
            WCHAR c = text[i * 5 + j];
            const char *digit = c > 0 && c < 128 ? strchr(digits85, (char)c) : nullptr;
            if (!digit)
                return FALSE;
            value += (ULONGLONG)(digit - digits85) * scale;
        }
        if (value > 0xFFFFFFFFull)
            return FALSE;
        words[i] = (ULONG)value;
    }
    _snwprintf(out, 39, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", words[0], words[1] & 0xFFFF,
               words[1] >> 16, words[2] & 0xFF, (words[2] >> 8) & 0xFF, (words[2] >> 16) & 0xFF, words[2] >> 24,
               words[3] & 0xFF, (words[3] >> 8) & 0xFF, (words[3] >> 16) & 0xFF, words[3] >> 24);
    return TRUE;
}

UINT MimeMsiDescriptor(PCWSTR product, PCWSTR feature, PCWSTR component, PWSTR *out)
{
    WCHAR packed_product[21], packed_component[21];
    if (!compress_guid(product, packed_product) || !compress_guid(component, packed_component) ||
        wcslen(feature) > MAX_FEATURE_CHARS)
        return ERROR_INVALID_PARAMETER;
    SIZE_T length = 20 + wcslen(feature) + 1 + 20 + 1;
    *out = MimeMsiAllocate(length * sizeof(WCHAR));
    if (!*out)
        return ERROR_NOT_ENOUGH_MEMORY;
    _snwprintf(*out, length, L"%s%s>%s", packed_product, feature, packed_component);
    return ERROR_SUCCESS;
}

/* a descriptor without its component, which '<' ends, names a feature of one component. */
UINT WINAPI MsiDecomposeDescriptorW(LPCWSTR descriptor, LPWSTR product, LPWSTR feature, LPWSTR component, LPDWORD used)
{
    WCHAR product_code[39], component_code[39] = L"";
    if (!descriptor || wcslen(descriptor) < 21 || !expand_guid(descriptor, product_code))
        return ERROR_INVALID_PARAMETER;
    PCWSTR end = descriptor + 20;
    while (*end && *end != L'>' && *end != L'<')
        end++;
    SIZE_T feature_length = (SIZE_T)(end - descriptor - 20);
    if (!*end || feature_length > MAX_FEATURE_CHARS)
        return ERROR_INVALID_PARAMETER;
    SIZE_T length = feature_length + 21;
    if (*end == L'>') {
        if (wcslen(end + 1) < 20 || !expand_guid(end + 1, component_code))
            return ERROR_INVALID_PARAMETER;
        length += 20;
    }
    if (product)
        wcscpy(product, product_code);
    if (feature) {
        memcpy(feature, descriptor + 20, feature_length * sizeof(WCHAR));
        feature[feature_length] = 0;
    }
    if (component)
        wcscpy(component, component_code);
    if (used)
        *used = (DWORD)length;
    return ERROR_SUCCESS;
}

UINT WINAPI MsiProvideComponentW(LPCWSTR product, LPCWSTR feature, LPCWSTR component, DWORD mode, LPWSTR path,
                                 LPDWORD size)
{
    if (!product || !feature || !component || (path && !size))
        return ERROR_INVALID_PARAMETER;
    INSTALLSTATE state = MsiQueryFeatureStateW(product, feature);
    if (state == INSTALLSTATE_UNKNOWN)
        return MsiQueryProductStateW(product) == INSTALLSTATE_UNKNOWN ? ERROR_UNKNOWN_PRODUCT : ERROR_UNKNOWN_FEATURE;
    if (state == INSTALLSTATE_INVALIDARG)
        return ERROR_INVALID_PARAMETER;
    /* a feature that is not there would be installed on demand, which needs the source. */
    if (state != INSTALLSTATE_LOCAL && state != INSTALLSTATE_SOURCE && state != INSTALLSTATE_DEFAULT)
        return ERROR_FILE_NOT_FOUND;
    WCHAR probe[1];
    DWORD none = 0;
    state = MsiGetComponentPathW(product, component, path ? path : probe, path ? size : &none);
    if (!path && size)
        *size = none;
    switch (state) {
    case INSTALLSTATE_LOCAL:
    case INSTALLSTATE_SOURCE:
        return ERROR_SUCCESS;
    case INSTALLSTATE_MOREDATA:
        return path ? ERROR_MORE_DATA : ERROR_SUCCESS;
    case INSTALLSTATE_UNKNOWN:
        return ERROR_UNKNOWN_COMPONENT;
    case INSTALLSTATE_INVALIDARG:
        return ERROR_INVALID_PARAMETER;
    case INSTALLSTATE_ABSENT:
        /* a key file that is gone counts only when the caller asked to look. */
        return mode == (DWORD)INSTALLMODE_NODETECTION || mode == (DWORD)INSTALLMODE_NODETECTION_ANY
                   ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
    default:
        return ERROR_FILE_NOT_FOUND;
    }
}

UINT WINAPI MsiProvideComponentFromDescriptorW(LPCWSTR descriptor, LPWSTR path, LPDWORD size, LPDWORD args)
{
    WCHAR product[39], feature[MAX_FEATURE_CHARS + 1], component[39];
    DWORD used;
    UINT error = MsiDecomposeDescriptorW(descriptor, product, feature, component, &used);
    if (error)
        return error;
    if (!*component)
        return ERROR_FILE_NOT_FOUND;
    error = MsiProvideComponentW(product, feature, component, INSTALLMODE_DEFAULT, path, size);
    if (!error && args)
        *args = used;
    return error;
}

/* the category in the user's catalog first, then in the machine's. */
static UINT open_category(PCWSTR category, HKEY keys[2])
{
    WCHAR packed[33], path[96];
    keys[0] = keys[1] = nullptr;
    if (!category || !MimeMsiPackGuid(category, packed))
        return ERROR_INVALID_PARAMETER;
    _snwprintf(path, RTL_NUMBER_OF(path), L"Software\\Microsoft\\Installer\\Components\\%s", packed);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &keys[0]))
        keys[0] = nullptr;
    _snwprintf(path, RTL_NUMBER_OF(path), L"Software\\Classes\\Installer\\Components\\%s", packed);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &keys[1]))
        keys[1] = nullptr;
    return keys[0] || keys[1] ? ERROR_SUCCESS : ERROR_UNKNOWN_COMPONENT;
}

static void close_category(HKEY keys[2])
{
    for (ULONG i = 0; i < 2; i++)
        if (keys[i])
            RegCloseKey(keys[i]);
}

/* the strings of a multi string value, two terminators at the end. */
static UINT read_descriptors(HKEY key, PCWSTR qualifier, PWSTR *out)
{
    DWORD type, bytes = 0;
    UINT error = RegQueryValueExW(key, qualifier, nullptr, &type, nullptr, &bytes);
    if (error)
        return error;
    if (type != REG_MULTI_SZ && type != REG_SZ)
        return ERROR_BAD_CONFIGURATION;
    *out = MimeMsiAllocate(bytes + 2 * sizeof(WCHAR));
    if (!*out)
        return ERROR_NOT_ENOUGH_MEMORY;
    error = RegQueryValueExW(key, qualifier, nullptr, nullptr, (BYTE *)*out, &bytes);
    if (error) {
        MimeMsiFree(*out);
        *out = nullptr;
        return error;
    }
    (*out)[bytes / sizeof(WCHAR)] = 0;
    (*out)[bytes / sizeof(WCHAR) + 1] = 0;
    return ERROR_SUCCESS;
}

UINT WINAPI MsiProvideQualifiedComponentExW(LPCWSTR category, LPCWSTR qualifier, DWORD mode, LPCWSTR product,
                                            DWORD unused1, DWORD unused2, LPWSTR path, LPDWORD size)
{
    if (!qualifier || (path && !size))
        return ERROR_INVALID_PARAMETER;
    HKEY keys[2];
    UINT error = open_category(category, keys);
    if (error)
        return error;
    error = ERROR_INDEX_ABSENT;
    for (ULONG i = 0; i < 2 && error == ERROR_INDEX_ABSENT; i++) {
        PWSTR list = nullptr;
        if (!keys[i] || read_descriptors(keys[i], qualifier, &list))
            continue;
        UINT found = ERROR_INDEX_ABSENT;
        for (PWSTR entry = list; *entry && found; entry += wcslen(entry) + 1) {
            WCHAR owner[39], feature[MAX_FEATURE_CHARS + 1], component[39];
            if (MsiDecomposeDescriptorW(entry, owner, feature, component, nullptr) || !*component)
                continue;
            if (product && _wcsicmp(product, owner))
                continue;
            found = MsiProvideComponentW(owner, feature, component, mode, path, size);
        }
        MimeMsiFree(list);
        error = found;
    }
    close_category(keys);
    return error;
}

UINT WINAPI MsiProvideQualifiedComponentW(LPCWSTR category, LPCWSTR qualifier, DWORD mode, LPWSTR path, LPDWORD size)
{
    return MsiProvideQualifiedComponentExW(category, qualifier, mode, nullptr, 0, 0, path, size);
}

UINT WINAPI MsiEnumComponentQualifiersW(LPCWSTR category, DWORD index, LPWSTR qualifier, LPDWORD qualifier_size,
                                        LPWSTR data, LPDWORD data_size)
{
    if (!qualifier || !qualifier_size || (data && !data_size))
        return ERROR_INVALID_PARAMETER;
    HKEY keys[2];
    UINT error = open_category(category, keys);
    if (error)
        return error;
    error = ERROR_NO_MORE_ITEMS;
    for (ULONG i = 0; i < 2; i++) {
        DWORD values = 0, longest = 0;
        if (!keys[i] || RegQueryInfoKeyW(keys[i], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values,
                                         &longest, nullptr, nullptr, nullptr))
            continue;
        if (index >= values) {
            index -= values;
            continue;
        }
        PWSTR name = MimeMsiAllocate(((SIZE_T)longest + 1) * sizeof(WCHAR)), list = nullptr;
        DWORD length = longest + 1;
        error = !name ? ERROR_NOT_ENOUGH_MEMORY
                      : RegEnumValueW(keys[i], index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (!error)
            error = read_descriptors(keys[i], name, &list);
        if (!error) {
            DWORD used = 0;
            PCWSTR text = MsiDecomposeDescriptorW(list, nullptr, nullptr, nullptr, &used) ? L"" : list + used;
            error = MimeMsiCopyString(name, qualifier, qualifier_size);
            if (data_size) {
                UINT data_error = data ? MimeMsiCopyString(text, data, data_size) : ERROR_SUCCESS;
                if (!data)
                    *data_size = (DWORD)wcslen(text);
                if (!error)
                    error = data_error;
            }
        }
        MimeMsiFree(list);
        MimeMsiFree(name);
        break;
    }
    close_category(keys);
    return error;
}

/* ------------------------------------------------------------------ the actions */

static PCWSTR component_code(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *components, PCWSTR name)
{
    ULONG id = MimeMsiColumn(components, L"Component", TRUE);
    ULONG code = MimeMsiColumn(components, L"ComponentId", TRUE);
    if (id == UINT32_MAX || code == UINT32_MAX)
        return nullptr;
    for (ULONG row = 0; row < components->Rows; row++) {
        PCWSTR candidate = MimeMsiText(package->Database, components, row, id);
        if (candidate && !wcscmp(candidate, name))
            return MimeMsiText(package->Database, components, row, code);
    }
    return nullptr;
}

/*
 * replaces this product's entry of a qualifier and keeps the other products' ones. with no entry left the value
 * goes away.
 */
static UINT update_qualifier(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR qualifier, PCWSTR own,
                             PCWSTR entry)
{
    PWSTR list = nullptr;
    HKEY key;
    if (!RegOpenKeyExW(root, path, 0, KEY_READ | package->RegistryView, &key)) {
        read_descriptors(key, qualifier, &list);
        RegCloseKey(key);
    }
    SIZE_T total = entry ? wcslen(entry) + 1 : 0;
    for (PWSTR item = list; item && *item; item += wcslen(item) + 1)
        total += wcslen(item) + 1;
    PWSTR merged = MimeMsiAllocate((total + 1) * sizeof(WCHAR));
    if (!merged) {
        MimeMsiFree(list);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    SIZE_T at = 0;
    if (entry) {
        wcscpy(merged, entry);
        at = wcslen(entry) + 1;
    }
    for (PWSTR item = list; item && *item; item += wcslen(item) + 1) {
        if (!wcsncmp(item, own, 20))
            continue;
        wcscpy(merged + at, item);
        at += wcslen(item) + 1;
    }
    merged[at] = 0;
    UINT error;
    if (at)
        error = MimeMsiWriteRegistry(package, root, path, qualifier, REG_MULTI_SZ, merged, (DWORD)((at + 1) * sizeof(WCHAR)));
    else
        error = list ? MimeMsiDeleteRegistryValue(package, root, path, qualifier) : ERROR_SUCCESS;
    /* a category without qualifiers goes away. */
    if (!error && !at && list && !RegOpenKeyExW(root, path, 0, KEY_READ | package->RegistryView, &key)) {
        DWORD values = 1;
        RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr,
                         nullptr);
        RegCloseKey(key);
        if (!values)
            RegDeleteKeyExW(root, path, package->RegistryView, 0);
    }
    MimeMsiFree(merged);
    MimeMsiFree(list);
    return error;
}

/* PublishComponents with publish, UnpublishComponents without: each row follows the state of its component. */
UINT MimeMsiPublishComponents(MIME_MSI_PACKAGE *package, HKEY root, BOOL publish, BOOL removing)
{
    MIME_MSI_TABLE *table, *components;
    UINT error = MimeMsiLoadTable(package->Database, L"PublishComponent", &table);
    if (error == ERROR_BAD_QUERY_SYNTAX || (!error && !table->Rows) || !MimeMsiModeAllows(package, REINSTALLMODE_MACHINEDATA))
        return ERROR_SUCCESS;
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"Component", &components);
    if (error)
        return error;
    ULONG category = MimeMsiColumn(table, L"ComponentId", TRUE);
    ULONG qualifier = MimeMsiColumn(table, L"Qualifier", TRUE);
    ULONG component = MimeMsiColumn(table, L"Component_", TRUE);
    ULONG data = MimeMsiColumn(table, L"AppData", TRUE);
    ULONG feature = MimeMsiColumn(table, L"Feature_", TRUE);
    if (category == UINT32_MAX || qualifier == UINT32_MAX || component == UINT32_MAX || feature == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    WCHAR own[21];
    if (!compress_guid(product, own))
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR category_code = MimeMsiText(package->Database, table, row, category);
        PCWSTR name = MimeMsiText(package->Database, table, row, qualifier);
        PCWSTR component_name = MimeMsiText(package->Database, table, row, component);
        PCWSTR feature_name = MimeMsiText(package->Database, table, row, feature);
        PCWSTR code = component_name ? component_code(package, components, component_name) : nullptr;
        WCHAR packed[33], path[96];
        if (!category_code || !MimeMsiPackGuid(category_code, packed) || !name || !code || !feature_name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL selected = FALSE;
        if (!removing) {
            PCWSTR directory;
            error = MimeMsiComponentSelected(package, component_name, &directory, &selected);
            if (error)
                return error;
        }
        if (selected != publish)
            continue;
        _snwprintf(path, RTL_NUMBER_OF(path), root == HKEY_LOCAL_MACHINE ?
                   L"Software\\Classes\\Installer\\Components\\%s" : L"Software\\Microsoft\\Installer\\Components\\%s",
                   packed);
        PWSTR entry = nullptr;
        if (publish) {
            PWSTR descriptor;
            error = MimeMsiDescriptor(product, feature_name, code, &descriptor);
            if (error)
                return error == ERROR_INVALID_PARAMETER ? ERROR_INSTALL_PACKAGE_INVALID : error;
            PWSTR app_data = nullptr;
            PCWSTR raw = data == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, data);
            error = MimeMsiFormat(package, raw ? raw : L"", &app_data);
            if (!error) {
                SIZE_T length = wcslen(descriptor) + wcslen(app_data) + 1;
                entry = MimeMsiAllocate(length * sizeof(WCHAR));
                if (entry)
                    _snwprintf(entry, length, L"%s%s", descriptor, app_data);
                else
                    error = ERROR_NOT_ENOUGH_MEMORY;
            }
            MimeMsiFree(app_data);
            MimeMsiFree(descriptor);
        }
        if (!error)
            error = update_qualifier(package, root, path, name, own, entry);
        MimeMsiFree(entry);
    }
    return error;
}
