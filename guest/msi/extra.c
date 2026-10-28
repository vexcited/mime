#include "msi_private.h"
#include <mime/win32/objbase.h>
#include <mime/win32/shobjidl.h>
#include <mime/win32/winver.h>

/*
 * standard actions beyond files and registration: machine searches, launch conditions, and the folders, registry
 * values, environment variables and shortcuts of components. they run as the sequence reaches them and write through
 * the transaction journal, so a failure restores the machine.
 */

enum { ACTION_NONE, ACTION_LOCAL, ACTION_ABSENT };

typedef BOOL (*ROW_CALLBACK)(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, void *context);

static UINT optional_table(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_TABLE **table)
{
    UINT error = MimeMsiLoadTable(package->Database, name, table);
    if (error == ERROR_BAD_QUERY_SYNTAX) {
        *table = nullptr;
        return ERROR_SUCCESS;
    }
    return error;
}

static PCWSTR cell_text(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    return index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
}

static int cell_number(MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, FALSE);
    return index == UINT32_MAX ? MSI_NULL_INTEGER : MimeMsiInteger(table, row, index);
}

static BOOL ends_with_separator(PCWSTR path)
{
    SIZE_T length = wcslen(path);
    return length && (path[length - 1] == L'\\' || path[length - 1] == L'/');
}

/* ------------------------------------------------------------------ components */

static UINT find_component(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_TABLE **table, ULONG *row)
{
    UINT error = MimeMsiLoadTable(package->Database, L"Component", table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(*table, L"Component", TRUE);
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG i = 0; i < (*table)->Rows; i++) {
        PCWSTR key = MimeMsiText(package->Database, *table, i, id);
        if (key && !wcscmp(key, name)) {
            *row = i;
            return ERROR_SUCCESS;
        }
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

/* the 64-bit registry view for a 64-bit component. */
static UINT component_view(MIME_MSI_PACKAGE *package, PCWSTR name, DWORD *view)
{
    MIME_MSI_TABLE *table;
    ULONG row;
    UINT error = find_component(package, name, &table, &row);
    if (error)
        return error;
    int attributes = cell_number(table, row, L"Attributes");
    BOOL is64 = attributes != MSI_NULL_INTEGER && (attributes & 256);
#ifdef _WIN64
    *view = is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
#else
    *view = 0;
    (void)is64;
#endif
    return ERROR_SUCCESS;
}

static BOOL component_registered(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    PCWSTR guid = cell_text(package, table, row, L"ComponentId");
    WCHAR component[33], product[33], path[128];
    if (!guid || !*guid || !MimeMsiPackGuid(guid, component) ||
        !MimeMsiPackGuid(MimeMsiGetProperty(package, L"ProductCode"), product))
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path), L"Software\\Classes\\Installer\\Components\\%s", component);
    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (ULONG i = 0; i < 2; i++) {
        HKEY key;
        if (RegOpenKeyExW(roots[i], path, 0, KEY_QUERY_VALUE, &key))
            continue;
        BOOL found = !RegQueryValueExW(key, product, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        if (found)
            return TRUE;
    }
    return FALSE;
}

/* snapshots the product's components when costing ends, since registration changes while actions run. */
UINT MimeMsiSnapshotComponents(MIME_MSI_PACKAGE *package)
{
    MimeMsiFree(package->ComponentInstalled);
    package->ComponentInstalled = nullptr;
    package->ComponentRows = 0;
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Component", &table);
    if (error || !table)
        return error;
    package->ComponentInstalled = MimeMsiAllocate(table->Rows + 1);
    if (!package->ComponentInstalled)
        return ERROR_NOT_ENOUGH_MEMORY;
    package->ComponentRows = table->Rows;
    for (ULONG row = 0; row < table->Rows; row++)
        package->ComponentInstalled[row] = component_registered(package, table, row);
    return ERROR_SUCCESS;
}

/* whether a component is installed, removed because the product had it, or left alone. */
static UINT component_action(MIME_MSI_PACKAGE *package, PCWSTR name, ULONG *action)
{
    MIME_MSI_TABLE *table;
    ULONG row;
    UINT error = find_component(package, name, &table, &row);
    if (error)
        return error;
    PCWSTR directory;
    BOOL selected;
    error = MimeMsiComponentSelected(package, name, &directory, &selected);
    if (error)
        return error;
    BOOL registered = package->ComponentInstalled && row < package->ComponentRows ? package->ComponentInstalled[row] != 0 :
                                                                                     component_registered(package, table, row);
    *action = selected ? ACTION_LOCAL : registered ? ACTION_ABSENT : ACTION_NONE;
    return ERROR_SUCCESS;
}

UINT MimeMsiComponentState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action)
{
    MIME_MSI_TABLE *table;
    ULONG row, what;
    UINT error = find_component(package, name, &table, &row);
    if (error)
        return error == ERROR_INSTALL_PACKAGE_INVALID ? ERROR_UNKNOWN_COMPONENT : error;
    *installed = component_registered(package, table, row) ? INSTALLSTATE_LOCAL : INSTALLSTATE_ABSENT;
    error = component_action(package, name, &what);
    if (!error)
        *action = what == ACTION_LOCAL ? INSTALLSTATE_LOCAL : what == ACTION_ABSENT ? INSTALLSTATE_ABSENT : INSTALLSTATE_UNKNOWN;
    return error;
}

UINT MimeMsiComponentDirectory(MIME_MSI_PACKAGE *package, PCWSTR name, PWSTR *out)
{
    MIME_MSI_TABLE *table;
    ULONG row;
    UINT error = find_component(package, name, &table, &row);
    if (error)
        return error;
    PCWSTR directory = cell_text(package, table, row, L"Directory_");
    if (!directory)
        return ERROR_INSTALL_PACKAGE_INVALID;
    return MimeMsiDirectoryPath(package, directory, FALSE, out);
}

UINT MimeMsiFileTargetPath(MIME_MSI_PACKAGE *package, PCWSTR key, PWSTR *out)
{
    MIME_MSI_TABLE *files;
    UINT error = MimeMsiLoadTable(package->Database, L"File", &files);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(files, L"File", TRUE);
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < files->Rows; row++) {
        PCWSTR candidate = MimeMsiText(package->Database, files, row, id);
        if (!candidate || wcscmp(candidate, key))
            continue;
        PCWSTR name = cell_text(package, files, row, L"FileName");
        PCWSTR owner = cell_text(package, files, row, L"Component_");
        if (!name || !owner)
            return ERROR_INSTALL_PACKAGE_INVALID;
        PCWSTR long_name = wcschr(name, L'|');
        PWSTR folder = nullptr;
        error = MimeMsiComponentDirectory(package, owner, &folder);
        if (error)
            return error;
        *out = MimeMsiJoinPath(folder, long_name ? long_name + 1 : name);
        MimeMsiFree(folder);
        return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

/* ------------------------------------------------------------------ registry values */

static HKEY registry_root(MIME_MSI_PACKAGE *package, int root)
{
    switch (root) {
    case -1: {
        PCWSTR all_users = MimeMsiGetProperty(package, L"ALLUSERS");
        return *all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    }
    case 0: return HKEY_CLASSES_ROOT;
    case 1: return HKEY_CURRENT_USER;
    case 2: return HKEY_LOCAL_MACHINE;
    case 3: return HKEY_USERS;
    default: return nullptr;
    }
}

static UINT format_length(MIME_MSI_PACKAGE *package, PCWSTR template, PWSTR *out, DWORD *length)
{
    *out = nullptr;
    MIME_MSI_RECORD *record = MimeMsiRecordCreate(0);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = MimeMsiFieldString(&record->Fields[0], template ? template : L"");
    if (!error && (!template || !*template)) {
        *out = MimeMsiDuplicate(L"");
        *length = 0;
        error = *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    } else if (!error)
        error = MimeMsiFormatRecord(package, record, out, length);
    record->Object.Destroy(&record->Object);
    return error;
}

static int hex_digit(WCHAR ch)
{
    return ch >= L'0' && ch <= L'9' ? ch - L'0' : ch >= L'a' && ch <= L'f' ? ch - L'a' + 10 :
           ch >= L'A' && ch <= L'F' ? ch - L'A' + 10 : -1;
}

/*
 * a formatted Registry row value. its prefix gives the type: #% an expandable string, #x hex bytes, ## a string that
 * starts with a number sign, # and digits a number. a value containing [~], which the formatter turned into a zero
 * character, is a list of strings.
 */
static UINT registry_value(PCWSTR text, DWORD length, DWORD *type, BYTE **data, DWORD *bytes)
{
    *data = nullptr;
    if (text[0] == L'#' && text[1] == L'%') {
        *type = REG_EXPAND_SZ;
        text += 2;
        length -= 2;
    } else if (text[0] == L'#' && (text[1] == L'x' || text[1] == L'X')) {
        *type = REG_BINARY;
        DWORD digits = length - 2;
        *bytes = (digits + 1) / 2;
        *data = MimeMsiAllocate(*bytes + 1);
        if (!*data)
            return ERROR_NOT_ENOUGH_MEMORY;
        for (DWORD i = 0; i < digits; i++) {
            int digit = hex_digit(text[2 + i]);
            if (digit < 0) {
                MimeMsiFree(*data);
                *data = nullptr;
                return ERROR_INSTALL_PACKAGE_INVALID;
            }
            if (i & 1)
                (*data)[i / 2] |= (BYTE)digit;
            else
                (*data)[i / 2] = (BYTE)(digit << 4);
        }
        return ERROR_SUCCESS;
    } else if (text[0] == L'#' && text[1] == L'#') {
        *type = REG_SZ;
        text += 1;
        length -= 1;
    } else if (text[0] == L'#' && ((text[1] >= L'0' && text[1] <= L'9') || (text[1] == L'-' && text[2]))) {
        BOOL negative = text[1] == L'-';
        ULONG number = 0;
        for (PCWSTR p = text + 1 + negative; *p; p++) {
            if (*p < L'0' || *p > L'9')
                return ERROR_INSTALL_PACKAGE_INVALID;
            number = number * 10 + (ULONG)(*p - L'0');
        }
        DWORD *value = MimeMsiAllocate(sizeof(DWORD));
        if (!value)
            return ERROR_NOT_ENOUGH_MEMORY;
        *value = negative ? 0u - number : number;
        *type = REG_DWORD;
        *data = (BYTE *)value;
        *bytes = sizeof(DWORD);
        return ERROR_SUCCESS;
    } else {
        BOOL list = FALSE;
        for (DWORD i = 0; i < length; i++)
            list |= text[i] == 0;
        *type = list ? REG_MULTI_SZ : REG_SZ;
    }
    /* strings keep their terminator, lists have two. */
    DWORD characters = length + 1 + (*type == REG_MULTI_SZ ? 1 : 0);
    WCHAR *copy = MimeMsiAllocate(characters * sizeof(WCHAR));
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(copy, text, length * sizeof(WCHAR));
    *data = (BYTE *)copy;
    *bytes = characters * sizeof(WCHAR);
    return ERROR_SUCCESS;
}

static UINT write_registry_rows(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Registry", &table);
    if (error || !table)
        return error;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!component || component_action(package, component, &action) || action != ACTION_LOCAL)
            continue;
        int root_number = cell_number(table, row, L"Root");
        HKEY root = registry_root(package, root_number);
        if (!root)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = component_view(package, component, &package->RegistryView);
        PWSTR key = nullptr, name = nullptr, value = nullptr;
        DWORD key_length, name_length, value_length;
        if (!error)
            error = format_length(package, cell_text(package, table, row, L"Key"), &key, &key_length);
        if (!error)
            error = format_length(package, cell_text(package, table, row, L"Name"), &name, &name_length);
        PCWSTR raw_value = cell_text(package, table, row, L"Value");
        if (!error)
            error = format_length(package, raw_value, &value, &value_length);
        if (!error && name_length == 1 && (name[0] == L'-')) {
            /* a name of - deletes the key on install. */
            error = MimeMsiDeleteRegistryTree(package, root, key, 0);
        } else if (!error && name_length == 1 && (name[0] == L'+' || name[0] == L'*')) {
            /* + and * create an empty key. on removal + keeps it and * deletes it with the component. */
            HKEY made;
            DWORD disposition;
            error = RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_READ | package->RegistryView, nullptr, &made, &disposition);
            if (!error)
                RegCloseKey(made);
        } else if (!error) {
            BYTE *data = nullptr;
            DWORD type, bytes;
            error = raw_value ? registry_value(value, value_length, &type, &data, &bytes) : ERROR_SUCCESS;
            if (!error && !raw_value) {
                type = REG_SZ;
                data = MimeMsiAllocate(sizeof(WCHAR));
                bytes = sizeof(WCHAR);
            }
            if (!error && !data)
                error = ERROR_NOT_ENOUGH_MEMORY;
            if (!error)
                error = MimeMsiWriteRegistry(package, root, key, name, type, data, bytes);
            MimeMsiFree(data);
        }
        package->RegistryView = 0;
        MimeMsiFree(key);
        MimeMsiFree(name);
        MimeMsiFree(value);
    }
    return error;
}

static UINT remove_registry_rows(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Registry", &table);
    if (error || !table)
        return error;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!component || component_action(package, component, &action) || action != ACTION_ABSENT)
            continue;
        HKEY root = registry_root(package, cell_number(table, row, L"Root"));
        if (!root)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = component_view(package, component, &package->RegistryView);
        PWSTR key = nullptr, name = nullptr;
        DWORD key_length, name_length;
        if (!error)
            error = format_length(package, cell_text(package, table, row, L"Key"), &key, &key_length);
        if (!error)
            error = format_length(package, cell_text(package, table, row, L"Name"), &name, &name_length);
        if (!error && name_length == 1 && name[0] == L'*')
            error = MimeMsiDeleteRegistryTree(package, root, key, 0);
        else if (!error && name_length == 1 && (name[0] == L'+' || name[0] == L'-'))
            error = ERROR_SUCCESS;
        else if (!error) {
            error = MimeMsiDeleteRegistryValue(package, root, key, name);
            /* delete the key too if the removal left it empty. */
            HKEY opened;
            if (!error && !RegOpenKeyExW(root, key, 0, KEY_READ | package->RegistryView, &opened)) {
                DWORD subkeys = 1, values = 1;
                RegQueryInfoKeyW(opened, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
                RegCloseKey(opened);
                if (!subkeys && !values)
                    RegDeleteKeyExW(root, key, package->RegistryView, 0);
            }
        }
        package->RegistryView = 0;
        MimeMsiFree(key);
        MimeMsiFree(name);
    }
    return error;
}

/* ------------------------------------------------------------------ the environment */

static PCWSTR environment_key(BOOL system)
{
    return system ? L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment" : L"Environment";
}

static UINT read_environment(HKEY root, PCWSTR path, PCWSTR name, PWSTR *value, DWORD *type)
{
    *value = nullptr;
    HKEY key;
    UINT error = RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return error;
    DWORD bytes = 0;
    error = RegQueryValueExW(key, name, nullptr, type, nullptr, &bytes);
    if (!error) {
        *value = MimeMsiAllocate(bytes + 2 * sizeof(WCHAR));
        if (!*value)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else
            error = RegQueryValueExW(key, name, nullptr, type, (BYTE *)*value, &bytes);
    }
    RegCloseKey(key);
    if (error) {
        MimeMsiFree(*value);
        *value = nullptr;
    }
    return error;
}

/* splits a value template around [~], which marks where the old value goes. */
static UINT environment_parts(MIME_MSI_PACKAGE *package, PCWSTR template, PWSTR *before, PWSTR *after, BOOL *has_old)
{
    PCWSTR marker = template ? wcsstr(template, L"[~]") : nullptr;
    *has_old = marker != nullptr;
    PWSTR head = nullptr, tail = nullptr;
    UINT error;
    if (marker) {
        PWSTR copy = MimeMsiDuplicate(template);
        if (!copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        copy[marker - template] = 0;
        error = MimeMsiFormat(package, copy, &head);
        MimeMsiFree(copy);
        if (!error)
            error = MimeMsiFormat(package, marker + 3, &tail);
    } else {
        error = MimeMsiFormat(package, template, &head);
        if (!error)
            tail = MimeMsiDuplicate(L"");
        if (!error && !tail)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error) {
        MimeMsiFree(head);
        MimeMsiFree(tail);
        return error;
    }
    *before = head;
    *after = tail;
    return ERROR_SUCCESS;
}

static UINT environment_rows(MIME_MSI_PACKAGE *package, BOOL install)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Environment", &table);
    if (error || !table)
        return error;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!component || component_action(package, component, &action) || action != (install ? ACTION_LOCAL : ACTION_ABSENT))
            continue;
        PCWSTR raw_name = cell_text(package, table, row, L"Name");
        if (!raw_name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL set = FALSE, create = FALSE, remove_on_install = FALSE, remove_on_uninstall = FALSE, system = FALSE;
        for (;; raw_name++) {
            if (*raw_name == L'=') set = TRUE;
            else if (*raw_name == L'+') create = TRUE;
            else if (*raw_name == L'!') remove_on_install = TRUE;
            else if (*raw_name == L'-') remove_on_uninstall = TRUE;
            else if (*raw_name == L'*') system = TRUE;
            else break;
        }
        PWSTR name = nullptr;
        error = MimeMsiFormat(package, raw_name, &name);
        if (error)
            return error;
        HKEY root = system ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        PCWSTR path = environment_key(system);
        PWSTR old_value = nullptr, before = nullptr, after = nullptr;
        DWORD old_type = REG_SZ;
        BOOL has_old = FALSE;
        read_environment(root, path, name, &old_value, &old_type);
        error = environment_parts(package, cell_text(package, table, row, L"Value"), &before, &after, &has_old);
        if (!error && install) {
            if (remove_on_install) {
                error = MimeMsiDeleteRegistryValue(package, root, path, name);
            } else if (set || create) {
                if (create && !set && old_value) {
                    /* already there, leave it. */
                } else {
                    /* with no old value, drop the separator next to its place. this is a guess. */
                    if (has_old && (!old_value || !*old_value)) {
                        SIZE_T before_length = wcslen(before);
                        if (*after == L';')
                            memmove(after, after + 1, wcslen(after) * sizeof(WCHAR));
                        else if (before_length && before[before_length - 1] == L';')
                            before[before_length - 1] = 0;
                    }
                    SIZE_T total = wcslen(before) + wcslen(after) + (has_old && old_value ? wcslen(old_value) : 0) + 1;
                    PWSTR value = MimeMsiAllocate(total * sizeof(WCHAR));
                    if (!value)
                        error = ERROR_NOT_ENOUGH_MEMORY;
                    else {
                        wcscpy(value, before);
                        if (has_old && old_value)
                            wcscat(value, old_value);
                        wcscat(value, after);
                        DWORD type = old_type == REG_EXPAND_SZ || wcschr(value, L'%') ? REG_EXPAND_SZ : REG_SZ;
                        /* skip the write when the package's text around the old value is already there. */
                        BOOL present = old_value && !wcscmp(old_value, value);
                        if (has_old && old_value && (*before || *after))
                            present = (!*before || wcsstr(old_value, before)) && (!*after || wcsstr(old_value, after));
                        if (!present)
                            error = MimeMsiWriteRegistry(package, root, path, name, type, value,
                                                         (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
                        MimeMsiFree(value);
                    }
                }
            }
        } else if (!error && !install && remove_on_uninstall) {
            if (old_value && has_old) {
                /* take what the package added around the old value back out. */
                SIZE_T length = wcslen(old_value);
                PWSTR value = MimeMsiDuplicate(old_value);
                if (!value)
                    error = ERROR_NOT_ENOUGH_MEMORY;
                else {
                    PCWSTR parts[2] = {before, after};
                    for (ULONG i = 0; i < 2; i++) {
                        SIZE_T part = wcslen(parts[i]);
                        PWSTR at = part ? wcsstr(value, parts[i]) : nullptr;
                        if (at)
                            memmove(at, at + part, (wcslen(at + part) + 1) * sizeof(WCHAR));
                    }
                    if (wcslen(value) != length) {
                        if (*value)
                            error = MimeMsiWriteRegistry(package, root, path, name, old_type, value,
                                                         (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
                        else
                            error = MimeMsiDeleteRegistryValue(package, root, path, name);
                    }
                    MimeMsiFree(value);
                }
            } else if (old_value)
                error = MimeMsiDeleteRegistryValue(package, root, path, name);
        }
        MimeMsiFree(name);
        MimeMsiFree(old_value);
        MimeMsiFree(before);
        MimeMsiFree(after);
    }
    return error;
}

/* ------------------------------------------------------------------ folders and files */

static BOOL wildcard_match(PCWSTR pattern, PCWSTR text)
{
    while (*pattern) {
        if (*pattern == L'*') {
            while (*pattern == L'*')
                pattern++;
            if (!*pattern)
                return TRUE;
            for (; *text; text++)
                if (wildcard_match(pattern, text))
                    return TRUE;
            return !*pattern;
        }
        if (!*text || (*pattern != L'?' && towlower(*pattern) != towlower(*text)))
            return FALSE;
        pattern++;
        text++;
    }
    return !*text;
}

static UINT create_folders(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"CreateFolder", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR directory = cell_text(package, table, row, L"Directory_");
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!directory || !component || component_action(package, component, &action) || action != ACTION_LOCAL)
            continue;
        PWSTR path = nullptr;
        error = MimeMsiDirectoryPath(package, directory, FALSE, &path);
        if (!error)
            error = MimeMsiMakeDirectory(package, path);
        MimeMsiFree(path);
    }
    return error;
}

static int compare_longest_first(const void *a, const void *b)
{
    SIZE_T x = wcslen(*(PWSTR const *)a), y = wcslen(*(PWSTR const *)b);
    return x < y ? 1 : x > y ? -1 : 0;
}

typedef struct PATH_LIST {
    PWSTR *Paths;
    ULONG Count, Capacity;
} PATH_LIST;

static UINT add_path(PATH_LIST *list, PWSTR path)
{
    for (ULONG i = 0; i < list->Count; i++)
        if (!_wcsicmp(list->Paths[i], path)) {
            MimeMsiFree(path);
            return ERROR_SUCCESS;
        }
    if (list->Count == list->Capacity) {
        ULONG capacity = list->Capacity ? list->Capacity * 2 : 64;
        PWSTR *bigger = MimeMsiAllocate(capacity * sizeof(PWSTR));
        if (!bigger) {
            MimeMsiFree(path);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        if (list->Paths)
            memcpy(bigger, list->Paths, list->Count * sizeof(PWSTR));
        MimeMsiFree(list->Paths);
        list->Paths = bigger;
        list->Capacity = capacity;
    }
    list->Paths[list->Count++] = path;
    return ERROR_SUCCESS;
}

static void free_paths(PATH_LIST *list)
{
    for (ULONG i = 0; i < list->Count; i++)
        MimeMsiFree(list->Paths[i]);
    MimeMsiFree(list->Paths);
}

/* removes the folders of removed components, deepest first, so each is empty when its turn comes. */
static UINT remove_folders(MIME_MSI_PACKAGE *package)
{
    PATH_LIST folders = {};
    UINT error = ERROR_SUCCESS;
    MIME_MSI_TABLE *table;
    error = optional_table(package, L"CreateFolder", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR directory = cell_text(package, table, row, L"Directory_");
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!directory || !component || component_action(package, component, &action) || action != ACTION_ABSENT)
            continue;
        PWSTR path = nullptr;
        error = MimeMsiDirectoryPath(package, directory, FALSE, &path);
        if (!error)
            error = add_path(&folders, path);
    }
    if (!error)
        error = optional_table(package, L"RemoveFile", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR name = cell_text(package, table, row, L"FileName");
        PCWSTR property = cell_text(package, table, row, L"DirProperty");
        int mode = cell_number(table, row, L"InstallMode");
        ULONG action;
        if (name && *name)
            continue;
        if (!component || !property || component_action(package, component, &action) || mode == MSI_NULL_INTEGER)
            continue;
        if (!((action == ACTION_LOCAL && (mode & 1)) || (action == ACTION_ABSENT && (mode & 2))))
            continue;
        PWSTR path = nullptr;
        error = MimeMsiDirectoryPath(package, property, FALSE, &path);
        if (!error)
            error = add_path(&folders, path);
    }
    /* the directories the component's files went to. */
    if (!error)
        error = optional_table(package, L"Component", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component");
        PCWSTR directory = cell_text(package, table, row, L"Directory_");
        ULONG action;
        if (!component || !directory || component_action(package, component, &action) || action != ACTION_ABSENT)
            continue;
        PWSTR path = nullptr;
        error = MimeMsiDirectoryPath(package, directory, FALSE, &path);
        if (!error)
            error = add_path(&folders, path);
    }
    if (!error && folders.Count > 1) {
        /* insertion sort is stable, keeping the order of folders at one depth. */
        for (ULONG i = 1; i < folders.Count; i++) {
            PWSTR item = folders.Paths[i];
            ULONG j = i;
            while (j && compare_longest_first(&folders.Paths[j - 1], &item) > 0) {
                folders.Paths[j] = folders.Paths[j - 1];
                j--;
            }
            folders.Paths[j] = item;
        }
    }
    for (ULONG i = 0; !error && i < folders.Count; i++) {
        PWSTR path = folders.Paths[i];
        SIZE_T length = wcslen(path);
        PWSTR trimmed = MimeMsiDuplicate(path);
        if (!trimmed) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        while (length > 3 && ends_with_separator(trimmed))
            trimmed[--length] = 0;
        if (length > 3)
            error = MimeMsiRemoveDirectory(package, trimmed);
        MimeMsiFree(trimmed);
    }
    free_paths(&folders);
    return error;
}

/* removes the files of removed components and the ones RemoveFile rows name. */
static UINT remove_files(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"File", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR key = cell_text(package, table, row, L"File");
        ULONG action;
        if (!component || !key || component_action(package, component, &action) || action != ACTION_ABSENT)
            continue;
        PWSTR path = nullptr;
        error = MimeMsiFileTargetPath(package, key, &path);
        if (!error)
            error = MimeMsiRemoveFile(package, path);
        MimeMsiFree(path);
    }
    if (!error)
        error = optional_table(package, L"RemoveFile", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR raw_name = cell_text(package, table, row, L"FileName");
        PCWSTR property = cell_text(package, table, row, L"DirProperty");
        int mode = cell_number(table, row, L"InstallMode");
        ULONG action;
        if (!raw_name || !*raw_name || !component || !property || mode == MSI_NULL_INTEGER ||
            component_action(package, component, &action))
            continue;
        if (!((action == ACTION_LOCAL && (mode & 1)) || (action == ACTION_ABSENT && (mode & 2))))
            continue;
        PCWSTR long_name = wcschr(raw_name, L'|');
        PWSTR pattern = nullptr, folder = nullptr;
        error = MimeMsiFormat(package, long_name ? long_name + 1 : raw_name, &pattern);
        if (!error)
            error = MimeMsiDirectoryPath(package, property, FALSE, &folder);
        if (!error) {
            PWSTR search = MimeMsiJoinPath(folder, L"*");
            WIN32_FIND_DATAW found;
            HANDLE find = search ? FindFirstFileW(search, &found) : INVALID_HANDLE_VALUE;
            MimeMsiFree(search);
            if (find != INVALID_HANDLE_VALUE) {
                do {
                    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wildcard_match(pattern, found.cFileName))
                        continue;
                    PWSTR path = MimeMsiJoinPath(folder, found.cFileName);
                    error = path ? MimeMsiRemoveFile(package, path) : ERROR_NOT_ENOUGH_MEMORY;
                    MimeMsiFree(path);
                } while (!error && FindNextFileW(find, &found));
                FindClose(find);
            }
        }
        MimeMsiFree(pattern);
        MimeMsiFree(folder);
    }
    return error;
}

/* ------------------------------------------------------------------ shortcuts */

static PWSTR shortcut_path(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    PCWSTR directory = cell_text(package, table, row, L"Directory_");
    PCWSTR name = cell_text(package, table, row, L"Name");
    if (!directory || !name)
        return nullptr;
    PCWSTR long_name = wcschr(name, L'|');
    PWSTR formatted = nullptr, folder = nullptr;
    if (MimeMsiFormat(package, long_name ? long_name + 1 : name, &formatted))
        return nullptr;
    if (MimeMsiDirectoryPath(package, directory, FALSE, &folder)) {
        MimeMsiFree(formatted);
        return nullptr;
    }
    SIZE_T length = wcslen(formatted);
    PWSTR file = MimeMsiAllocate((length + 5) * sizeof(WCHAR));
    if (file) {
        wcscpy(file, formatted);
        if (length < 4 || _wcsicmp(file + length - 4, L".lnk"))
            wcscat(file, L".lnk");
    }
    PWSTR path = file ? MimeMsiJoinPath(folder, file) : nullptr;
    MimeMsiFree(file);
    MimeMsiFree(formatted);
    MimeMsiFree(folder);
    return path;
}

static UINT create_shortcuts(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Shortcut", &table);
    if (error || !table || !table->Rows)
        return error;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!component || component_action(package, component, &action) || action != ACTION_LOCAL)
            continue;
        PWSTR path = shortcut_path(package, table, row);
        PWSTR target = nullptr, arguments = nullptr, description = nullptr, directory = nullptr;
        if (!path) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        error = MimeMsiFormat(package, cell_text(package, table, row, L"Target"), &target);
        if (!error)
            error = MimeMsiFormat(package, cell_text(package, table, row, L"Arguments"), &arguments);
        if (!error)
            error = MimeMsiFormat(package, cell_text(package, table, row, L"Description"), &description);
        PCWSTR work = cell_text(package, table, row, L"WkDir");
        if (!error && work) {
            error = MimeMsiDirectoryPath(package, work, FALSE, &directory);
            if (error == ERROR_DIRECTORY) {
                error = MimeMsiFormat(package, work, &directory);
            }
        }
        IShellLinkW *link = nullptr;
        if (!error) {
            HRESULT hr = CoCreateInstance(&CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
            if (FAILED(hr))
                error = ERROR_FUNCTION_FAILED;
        }
        if (!error) {
            /* a feature's file stands for an advertised shortcut, which is not created. */
            if (!*target) {
                error = ERROR_INSTALL_PACKAGE_INVALID;
            } else {
                link->lpVtbl->SetPath(link, target);
                if (*arguments)
                    link->lpVtbl->SetArguments(link, arguments);
                if (*description)
                    link->lpVtbl->SetDescription(link, description);
                if (directory && *directory)
                    link->lpVtbl->SetWorkingDirectory(link, directory);
                int show = cell_number(table, row, L"ShowCmd");
                link->lpVtbl->SetShowCmd(link, show == MSI_NULL_INTEGER || !show ? 1 : show);
                int hotkey = cell_number(table, row, L"Hotkey");
                if (hotkey != MSI_NULL_INTEGER && hotkey)
                    link->lpVtbl->SetHotkey(link, (WORD)hotkey);
            }
        }
        if (!error) {
            IPersistFile *file = nullptr;
            if (SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file))) {
                BOOL existed;
                PWSTR parent = MimeMsiDuplicate(path);
                PWSTR separator = parent ? wcsrchr(parent, L'\\') : nullptr;
                if (separator) {
                    *separator = 0;
                    error = MimeMsiMakeDirectory(package, parent);
                }
                MimeMsiFree(parent);
                if (!error)
                    error = MimeMsiBackupFile(package, path, &existed);
                if (!error && !existed)
                    error = MimeMsiNoteNewFile(package, path);
                if (!error && FAILED(file->lpVtbl->Save(file, path, TRUE)))
                    error = ERROR_FUNCTION_FAILED;
                file->lpVtbl->Release(file);
            } else
                error = ERROR_FUNCTION_FAILED;
        }
        if (link)
            link->lpVtbl->Release(link);
        MimeMsiFree(path);
        MimeMsiFree(target);
        MimeMsiFree(arguments);
        MimeMsiFree(description);
        MimeMsiFree(directory);
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return error;
}

static UINT remove_shortcuts(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Shortcut", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        ULONG action;
        if (!component || component_action(package, component, &action) || action != ACTION_ABSENT)
            continue;
        PWSTR path = shortcut_path(package, table, row);
        if (path)
            error = MimeMsiRemoveFile(package, path);
        MimeMsiFree(path);
    }
    return error;
}

/* ------------------------------------------------------------------ searches */

static int compare_versions(PCWSTR a, PCWSTR b)
{
    for (ULONG i = 0; i < 4; i++) {
        ULONG x = 0, y = 0;
        while (*a >= L'0' && *a <= L'9')
            x = x * 10 + (ULONG)(*a++ - L'0');
        while (*b >= L'0' && *b <= L'9')
            y = y * 10 + (ULONG)(*b++ - L'0');
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == L'.')
            a++;
        if (*b == L'.')
            b++;
    }
    return 0;
}

static BOOL read_text_value(HKEY key, PCWSTR name, DWORD type_wanted, PWSTR *text)
{
    DWORD type, bytes = 0;
    *text = nullptr;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes))
        return FALSE;
    BYTE *data = MimeMsiAllocate(bytes + 2 * sizeof(WCHAR));
    if (!data)
        return FALSE;
    if (RegQueryValueExW(key, name, nullptr, &type, data, &bytes)) {
        MimeMsiFree(data);
        return FALSE;
    }
    if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
        *text = (PWSTR)data;
        if (type_wanted == 2 && type == REG_EXPAND_SZ) {
            /* a raw read marks an expandable string with #%. */
            SIZE_T length = wcslen(*text);
            PWSTR marked = MimeMsiAllocate((length + 3) * sizeof(WCHAR));
            if (marked) {
                wcscpy(marked, L"#%");
                wcscat(marked, *text);
            }
            MimeMsiFree(*text);
            *text = marked;
        }
        return *text != nullptr;
    }
    WCHAR number[16];
    if (type == REG_DWORD && bytes >= sizeof(DWORD)) {
        _snwprintf(number, RTL_NUMBER_OF(number), L"#%d", *(int *)data);
        *text = MimeMsiDuplicate(number);
    } else if (type == REG_BINARY || type == REG_NONE) {
        PWSTR hex = MimeMsiAllocate((bytes * 2 + 3) * sizeof(WCHAR));
        if (hex) {
            wcscpy(hex, L"#x");
            for (DWORD i = 0; i < bytes; i++)
                _snwprintf(hex + 2 + i * 2, 3, L"%02X", data[i]);
            *text = hex;
        }
    }
    MimeMsiFree(data);
    return *text != nullptr;
}

/* checks a file against a Signature row's name, version and size. */
static BOOL signature_matches(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *signatures, ULONG row, PCWSTR path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;
    ULONGLONG size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    int minimum = cell_number(signatures, row, L"MinSize");
    int maximum = cell_number(signatures, row, L"MaxSize");
    if ((minimum != MSI_NULL_INTEGER && size < (ULONGLONG)(ULONG)minimum) ||
        (maximum != MSI_NULL_INTEGER && size > (ULONGLONG)(ULONG)maximum))
        return FALSE;
    PCWSTR low = cell_text(package, signatures, row, L"MinVersion");
    PCWSTR high = cell_text(package, signatures, row, L"MaxVersion");
    if ((low && *low) || (high && *high)) {
        DWORD handle;
        DWORD bytes = GetFileVersionInfoSizeW(path, &handle);
        VS_FIXEDFILEINFO *fixed = nullptr;
        UINT length = 0;
        void *data = bytes ? MimeMsiAllocate(bytes) : nullptr;
        BOOL have = data && GetFileVersionInfoW(path, 0, bytes, data) && VerQueryValueW(data, L"\\", (void **)&fixed, &length) && length >= sizeof(*fixed);
        WCHAR version[64] = {};
        if (have)
            _snwprintf(version, RTL_NUMBER_OF(version), L"%u.%u.%u.%u", fixed->dwFileVersionMS >> 16, fixed->dwFileVersionMS & 0xFFFF,
                       fixed->dwFileVersionLS >> 16, fixed->dwFileVersionLS & 0xFFFF);
        MimeMsiFree(data);
        if (!have || (low && *low && compare_versions(version, low) < 0) || (high && *high && compare_versions(version, high) > 0))
            return FALSE;
    }
    return TRUE;
}

/* the first match for a signature under a directory, searched down to a depth. */
static PWSTR locate(MIME_MSI_PACKAGE *package, PCWSTR directory, PCWSTR signature, int depth, ULONG level)
{
    MIME_MSI_TABLE *signatures;
    ULONG row = UINT32_MAX;
    if (optional_table(package, L"Signature", &signatures))
        return nullptr;
    for (ULONG i = 0; signatures && signature && i < signatures->Rows; i++) {
        PCWSTR key = cell_text(package, signatures, i, L"Signature");
        if (key && !wcscmp(key, signature)) {
            row = i;
            break;
        }
    }
    if (row == UINT32_MAX) {
        /* without a signature, the directory itself is the target. */
        DWORD attributes = GetFileAttributesW(directory);
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? MimeMsiDuplicate(directory) : nullptr;
    }
    PCWSTR names = cell_text(package, signatures, row, L"FileName");
    PCWSTR long_name = names ? wcschr(names, L'|') : nullptr;
    PCWSTR pattern = long_name ? long_name + 1 : names;
    if (!pattern)
        return nullptr;
    PWSTR search = MimeMsiJoinPath(directory, L"*");
    WIN32_FIND_DATAW found;
    HANDLE find = search ? FindFirstFileW(search, &found) : INVALID_HANDLE_VALUE;
    MimeMsiFree(search);
    PWSTR result = nullptr;
    PATH_LIST subdirectories = {};
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L".."))
                continue;
            PWSTR path = MimeMsiJoinPath(directory, found.cFileName);
            if (!path)
                break;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if ((int)level < depth)
                    add_path(&subdirectories, path);
                else
                    MimeMsiFree(path);
            } else if (wildcard_match(pattern, found.cFileName) && signature_matches(package, signatures, row, path)) {
                result = path;
                break;
            } else
                MimeMsiFree(path);
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    for (ULONG i = 0; !result && i < subdirectories.Count; i++)
        result = locate(package, subdirectories.Paths[i], signature, depth, level + 1);
    free_paths(&subdirectories);
    return result;
}

static PWSTR search_registry(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    int root_number = cell_number(table, row, L"Root");
    HKEY root = registry_root(package, root_number);
    PCWSTR raw_key = cell_text(package, table, row, L"Key");
    PCWSTR raw_name = cell_text(package, table, row, L"Name");
    int type = cell_number(table, row, L"Type");
    PCWSTR signature = cell_text(package, table, row, L"Signature_");
    if (!root || !raw_key || type == MSI_NULL_INTEGER)
        return nullptr;
    PWSTR key = nullptr, name = nullptr;
    if (MimeMsiFormat(package, raw_key, &key))
        return nullptr;
    if (MimeMsiFormat(package, raw_name, &name)) {
        MimeMsiFree(key);
        return nullptr;
    }
    DWORD view = 0;
#ifdef _WIN64
    view = (type & 16) ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
#endif
    HKEY opened;
    PWSTR text = nullptr, result = nullptr;
    if (!RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE | view, &opened)) {
        if (read_text_value(opened, *name ? name : nullptr, (DWORD)(type & 15), &text)) {
            if ((type & 15) == 2)
                result = text;
            else {
                /* the value is a path: a file, or a directory to look in for the signature's file. */
                PWSTR expanded = text;
                DWORD attributes = GetFileAttributesW(text);
                if (attributes != INVALID_FILE_ATTRIBUTES) {
                    if ((type & 15) == 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !signature)
                        result = MimeMsiDuplicate(expanded);
                    else if ((type & 15) == 1 && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                        result = MimeMsiDuplicate(expanded);
                    else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
                        result = locate(package, text, signature, 0, 0);
                }
                MimeMsiFree(text);
            }
        }
        RegCloseKey(opened);
    }
    MimeMsiFree(key);
    MimeMsiFree(name);
    return result;
}

static PWSTR search_directory(MIME_MSI_PACKAGE *package, PCWSTR signature, PCWSTR found_parents[], ULONG count)
{
    MIME_MSI_TABLE *table;
    if (optional_table(package, L"DrLocator", &table) || !table)
        return nullptr;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR key = cell_text(package, table, row, L"Signature_");
        if (!key || wcscmp(key, signature))
            continue;
        PCWSTR parent = cell_text(package, table, row, L"Parent");
        PCWSTR raw_path = cell_text(package, table, row, L"Path");
        int depth = cell_number(table, row, L"Depth");
        PWSTR base = nullptr, formatted = nullptr;
        if (MimeMsiFormat(package, raw_path, &formatted))
            return nullptr;
        if (parent && *parent) {
            /* the path is relative to what the parent search found. */
            PWSTR location = search_directory(package, parent, found_parents, count);
            if (!location) {
                MimeMsiFree(formatted);
                return nullptr;
            }
            base = MimeMsiJoinPath(location, formatted);
            MimeMsiFree(location);
            MimeMsiFree(formatted);
        } else
            base = formatted;
        PWSTR result = base ? locate(package, base, key, depth == MSI_NULL_INTEGER ? 0 : depth, 0) : nullptr;
        MimeMsiFree(base);
        return result;
    }
    return nullptr;
}

static PWSTR search_component(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    PCWSTR guid = cell_text(package, table, row, L"ComponentId");
    int type = cell_number(table, row, L"Type");
    WCHAR packed[33], path[128];
    if (!guid || !MimeMsiPackGuid(guid, packed))
        return nullptr;
    _snwprintf(path, RTL_NUMBER_OF(path), L"Software\\Classes\\Installer\\Components\\%s", packed);
    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (ULONG i = 0; i < 2; i++) {
        HKEY key;
        if (RegOpenKeyExW(roots[i], path, 0, KEY_QUERY_VALUE, &key))
            continue;
        WCHAR name[40];
        DWORD length = RTL_NUMBER_OF(name), bytes = 0;
        PWSTR result = nullptr;
        if (!RegEnumValueW(key, 0, name, &length, nullptr, nullptr, nullptr, &bytes) &&
            read_text_value(key, name, 2, &result)) {
            if (type == 0) {
                /* type 0 wants the key path's directory. */
                PWSTR separator = wcsrchr(result, L'\\');
                DWORD attributes = GetFileAttributesW(result);
                if (separator && !(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)))
                    *separator = 0;
            }
            RegCloseKey(key);
            return result;
        }
        RegCloseKey(key);
    }
    return nullptr;
}

static UINT app_search(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *searches;
    UINT error = optional_table(package, L"AppSearch", &searches);
    if (error || !searches)
        return error;
    MIME_MSI_TABLE *registry, *components;
    error = optional_table(package, L"RegLocator", &registry);
    if (!error)
        error = optional_table(package, L"CompLocator", &components);
    for (ULONG row = 0; row < searches->Rows && !error; row++) {
        PCWSTR property = cell_text(package, searches, row, L"Property");
        PCWSTR signature = cell_text(package, searches, row, L"Signature_");
        if (!property || !signature)
            continue;
        PWSTR result = nullptr;
        for (ULONG i = 0; registry && !result && i < registry->Rows; i++) {
            PCWSTR key = cell_text(package, registry, i, L"Signature_");
            if (key && !wcscmp(key, signature)) {
                result = search_registry(package, registry, i);
            }
        }
        for (ULONG i = 0; components && !result && i < components->Rows; i++) {
            PCWSTR key = cell_text(package, components, i, L"Signature_");
            if (key && !wcscmp(key, signature))
                result = search_component(package, components, i);
        }
        if (!result)
            result = search_directory(package, signature, nullptr, 0);
        if (result) {
            error = MimeMsiSetProperty(package, property, result);
            MimeMsiFree(result);
        }
    }
    return error;
}

/* ------------------------------------------------------------------ related products */

static UINT find_related_products(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"Upgrade", &table);
    if (error || !table)
        return error;
    PCWSTR current = MimeMsiGetProperty(package, L"ProductCode");
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR upgrade = cell_text(package, table, row, L"UpgradeCode");
        PCWSTR property = cell_text(package, table, row, L"ActionProperty");
        PCWSTR minimum = cell_text(package, table, row, L"VersionMin");
        PCWSTR maximum = cell_text(package, table, row, L"VersionMax");
        PCWSTR languages = cell_text(package, table, row, L"Language");
        int attributes = cell_number(table, row, L"Attributes");
        if (!upgrade || !property)
            continue;
        if (attributes == MSI_NULL_INTEGER)
            attributes = 0;
        PWSTR found = nullptr;
        for (DWORD index = 0;; index++) {
            WCHAR product[39];
            UINT status = MsiEnumRelatedProductsW(upgrade, 0, index, product);
            if (status)
                break;
            if (!_wcsicmp(product, current))
                continue;
            WCHAR version[64] = {}, language[16] = {};
            DWORD size = RTL_NUMBER_OF(version);
            MsiGetProductInfoW(product, L"VersionString", version, &size);
            size = RTL_NUMBER_OF(language);
            MsiGetProductInfoW(product, L"Language", language, &size);
            BOOL match = TRUE;
            if (minimum && *minimum) {
                int order = compare_versions(version, minimum);
                match = order > 0 || (order == 0 && (attributes & 256));
            }
            if (match && maximum && *maximum) {
                int order = compare_versions(version, maximum);
                match = order < 0 || (order == 0 && (attributes & 512));
            }
            if (match && languages && *languages) {
                BOOL listed = FALSE;
                PCWSTR p = languages;
                while (*p) {
                    PCWSTR end = wcschr(p, L',');
                    SIZE_T length = end ? (SIZE_T)(end - p) : wcslen(p);
                    if (length == wcslen(language) && !wcsncmp(p, language, length))
                        listed = TRUE;
                    if (!end)
                        break;
                    p = end + 1;
                }
                match = (attributes & 1024) ? !listed : listed;
            }
            if (!match)
                continue;
            SIZE_T used = found ? wcslen(found) : 0;
            PWSTR bigger = MimeMsiAllocate((used + wcslen(product) + 2) * sizeof(WCHAR));
            if (!bigger) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            if (found) {
                wcscpy(bigger, found);
                wcscat(bigger, L";");
            }
            wcscat(bigger, product);
            MimeMsiFree(found);
            found = bigger;
        }
        if (!error && found)
            error = MimeMsiSetProperty(package, property, found);
        MimeMsiFree(found);
    }
    return error;
}

static UINT launch_conditions(MIME_MSI_PACKAGE *package, PWSTR *message)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"LaunchCondition", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR condition = cell_text(package, table, row, L"Condition");
        PCWSTR description = cell_text(package, table, row, L"Description");
        MSICONDITION result = MimeMsiEvaluateCondition(package, condition ? condition : L"");
        if (result == MSICONDITION_ERROR)
            return ERROR_INSTALL_FAILURE;
        if (result == MSICONDITION_FALSE) {
            MimeMsiFormat(package, description ? description : L"", message);
            return ERROR_INSTALL_FAILURE;
        }
    }
    return error;
}

/* ------------------------------------------------------------------ actions of tables */

/* standard actions that work on rows of their own tables. with no rows there is nothing to do and the action
 * succeeds. none of them carries out rows yet. */
static const struct {
    PCWSTR Action;
    PCWSTR Tables[3];
} TABLE_ACTIONS[] = {
    {L"SetODBCFolders", {L"ODBCDriver", L"ODBCTranslator", L"ODBCDataSource"}},
    {L"MsiPublishAssemblies", {L"MsiAssembly"}},
    {L"MsiUnpublishAssemblies", {L"MsiAssembly"}},
    {L"RegisterComPlus", {L"Complus"}},
    {L"UnregisterComPlus", {L"Complus"}},
    {L"SelfRegModules", {L"SelfReg"}},
    {L"SelfUnregModules", {L"SelfReg"}},
    {L"RegisterTypeLibraries", {L"TypeLib"}},
    {L"UnregisterTypeLibraries", {L"TypeLib"}},
    {L"RegisterFonts", {L"Font"}},
    {L"UnregisterFonts", {L"Font"}},
    {L"RegisterClassInfo", {L"Class"}},
    {L"UnregisterClassInfo", {L"Class"}},
    {L"RegisterMIMEInfo", {L"MIME"}},
    {L"UnregisterMIMEInfo", {L"MIME"}},
    {L"WriteIniValues", {L"IniFile"}},
    {L"RemoveIniValues", {L"IniFile", L"RemoveIniFile"}},
    {L"DuplicateFiles", {L"DuplicateFile"}},
    {L"RemoveDuplicateFiles", {L"DuplicateFile"}},
    {L"MoveFiles", {L"MoveFile"}},
    {L"BindImage", {L"BindImage"}},
    {L"InstallServices", {L"ServiceInstall"}},
    {L"DeleteServices", {L"ServiceInstall"}},
    {L"StartServices", {L"ServiceControl"}},
    {L"StopServices", {L"ServiceControl"}},
    {L"CCPSearch", {L"CCPSearch"}},
    {L"RMCCPSearch", {L"CCPSearch"}},
};

static UINT table_action(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(TABLE_ACTIONS); i++) {
        if (wcscmp(action, TABLE_ACTIONS[i].Action))
            continue;
        for (ULONG j = 0; j < RTL_NUMBER_OF(TABLE_ACTIONS[i].Tables) && TABLE_ACTIONS[i].Tables[j]; j++) {
            MIME_MSI_TABLE *table;
            UINT error = MimeMsiLoadTable(package->Database, TABLE_ACTIONS[i].Tables[j], &table);
            if (error && error != ERROR_BAD_QUERY_SYNTAX)
                return error;
            if (!error && table->Rows)
                return ERROR_CALL_NOT_IMPLEMENTED;
        }
        return ERROR_SUCCESS;
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------ dispatch */

UINT MimeMsiExtraAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (!wcscmp(action, L"AppSearch"))
        return app_search(package);
    if (!wcscmp(action, L"FindRelatedProducts"))
        return find_related_products(package);
    if (!wcscmp(action, L"LaunchConditions")) {
        PWSTR message = nullptr;
        UINT error = launch_conditions(package, &message);
        if (message) {
            MimeMsiFree(package->ActionFormat);
            package->ActionFormat = nullptr;
            DbgPrint("msi: launch condition failed: %ws\n", message);
            package->LaunchMessage = message;
        }
        return error;
    }
    static const PCWSTR trivial[] = {L"MigrateFeatureStates", L"ValidateProductID", L"ResolveSource", L"AllocateRegistrySpace",
                                     L"RegisterUser", L"InstallODBC", L"RemoveODBC", L"IsolateComponents"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(trivial); i++)
        if (!wcscmp(action, trivial[i]))
            return ERROR_SUCCESS;
    if (!wcscmp(action, L"DisableRollback")) {
        package->NoRollback = TRUE;
        return ERROR_SUCCESS;
    }
    BOOL modifies = !wcscmp(action, L"CreateFolders") || !wcscmp(action, L"RemoveFolders") ||
                    !wcscmp(action, L"RemoveFiles") || !wcscmp(action, L"WriteRegistryValues") ||
                    !wcscmp(action, L"RemoveRegistryValues") || !wcscmp(action, L"WriteEnvironmentStrings") ||
                    !wcscmp(action, L"RemoveEnvironmentStrings") || !wcscmp(action, L"CreateShortcuts") ||
                    !wcscmp(action, L"RemoveShortcuts");
    if (modifies) {
        if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
            return ERROR_FUNCTION_NOT_CALLED;
        if (!wcscmp(action, L"CreateFolders")) return create_folders(package);
        if (!wcscmp(action, L"RemoveFolders")) return remove_folders(package);
        if (!wcscmp(action, L"RemoveFiles")) return remove_files(package);
        if (!wcscmp(action, L"WriteRegistryValues")) return write_registry_rows(package);
        if (!wcscmp(action, L"RemoveRegistryValues")) return remove_registry_rows(package);
        if (!wcscmp(action, L"WriteEnvironmentStrings")) return environment_rows(package, TRUE);
        if (!wcscmp(action, L"RemoveEnvironmentStrings")) return environment_rows(package, FALSE);
        if (!wcscmp(action, L"CreateShortcuts")) return create_shortcuts(package);
        return remove_shortcuts(package);
    }
    if (!wcscmp(action, L"RemoveExistingProducts"))
        return MIME_MSI_UNLOCKED_PENDING;
    return table_action(package, action);
}
