/*
 * .ini files. WriteIniValues applies the IniFile rows of the components being installed, and RemoveIniValues undoes
 * those of the components being removed and applies the RemoveIniFile rows of the components being installed. the file
 * is kept before it changes, so that a failed install puts it back.
 */
#include "msi_private.h"

enum { INI_ADD_LINE, INI_CREATE_LINE, INI_REMOVE_LINE, INI_ADD_TAG, INI_REMOVE_TAG };

static const WCHAR ABSENT[] = L"\x7f<absent>\x7f";

static PCWSTR cell_text(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    return index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
}

/* the value of a key, none when the key is not in the file. */
static PWSTR read_value(PCWSTR path, PCWSTR section, PCWSTR key)
{
    DWORD size = 256;
    for (;;) {
        PWSTR text = MimeMsiAllocate(size * sizeof(WCHAR));
        if (!text)
            return nullptr;
        DWORD length = GetPrivateProfileStringW(section, key, ABSENT, text, size, path);
        if (length + 1 < size) {
            if (!wcscmp(text, ABSENT)) {
                MimeMsiFree(text);
                return nullptr;
            }
            return text;
        }
        MimeMsiFree(text);
        if (size > 1024 * 1024)
            return nullptr;
        size *= 4;
    }
}

static BOOL has_tag(PCWSTR list, PCWSTR tag)
{
    SIZE_T length = wcslen(tag);
    while (*list) {
        PCWSTR end = wcschr(list, L',');
        SIZE_T count = end ? (SIZE_T)(end - list) : wcslen(list);
        if (count == length && !_wcsnicmp(list, tag, count))
            return TRUE;
        if (!end)
            break;
        list = end + 1;
    }
    return FALSE;
}

/* the list without a tag. */
static PWSTR without_tag(PCWSTR list, PCWSTR tag)
{
    PWSTR out = MimeMsiAllocate((wcslen(list) + 1) * sizeof(WCHAR));
    SIZE_T length = wcslen(tag);
    while (out && *list) {
        PCWSTR end = wcschr(list, L',');
        SIZE_T count = end ? (SIZE_T)(end - list) : wcslen(list);
        if (count != length || _wcsnicmp(list, tag, count)) {
            SIZE_T used = wcslen(out);
            if (used)
                out[used++] = L',';
            memcpy(out + used, list, count * sizeof(WCHAR));
            out[used + count] = 0;
        }
        if (!end)
            break;
        list = end + 1;
    }
    return out;
}

static UINT write_value(PCWSTR path, PCWSTR section, PCWSTR key, PCWSTR value)
{
    return WritePrivateProfileStringW(section, key, value, path) ? ERROR_SUCCESS : GetLastError();
}

static UINT apply(MIME_MSI_PACKAGE *package, PCWSTR path, PCWSTR section, PCWSTR key, PCWSTR value, int action)
{
    PWSTR current = read_value(path, section, key);
    BOOL adding = action == INI_ADD_LINE || action == INI_CREATE_LINE || action == INI_ADD_TAG;
    PWSTR changed = nullptr;
    PCWSTR next = value;
    BOOL write = TRUE;
    if (action == INI_CREATE_LINE)
        write = !current;
    else if (action == INI_REMOVE_LINE)
        next = nullptr;
    else if (action == INI_ADD_TAG) {
        write = !current || !has_tag(current, value);
        if (current && *current && write) {
            changed = MimeMsiAllocate((wcslen(current) + wcslen(value) + 2) * sizeof(WCHAR));
            if (!changed) {
                MimeMsiFree(current);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            _snwprintf(changed, wcslen(current) + wcslen(value) + 2, L"%s,%s", current, value);
            next = changed;
        }
    } else if (action == INI_REMOVE_TAG) {
        write = current && has_tag(current, value);
        if (write) {
            changed = without_tag(current, value);
            next = changed && *changed ? changed : nullptr;
        }
    }
    UINT error = ERROR_SUCCESS;
    if (write && (adding || GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)) {
        BOOL existed = FALSE;
        PWSTR folder = MimeMsiDuplicate(path);
        PWSTR separator = folder ? wcsrchr(folder, L'\\') : nullptr;
        if (!folder)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else if (separator && adding) {
            *separator = 0;
            error = MimeMsiMakeDirectory(package, folder);
        }
        MimeMsiFree(folder);
        if (!error)
            error = MimeMsiBackupFile(package, path, &existed);
        if (!error && !existed && adding)
            error = MimeMsiNoteNewFile(package, path);
        if (!error)
            error = write_value(path, section, key, next);
    }
    MimeMsiFree(changed);
    MimeMsiFree(current);
    return error;
}

static UINT ini_row(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, BOOL undo)
{
    PCWSTR file = cell_text(package, table, row, L"FileName");
    PCWSTR directory = cell_text(package, table, row, L"DirProperty");
    PCWSTR section_text = cell_text(package, table, row, L"Section");
    PCWSTR key_text = cell_text(package, table, row, L"Key");
    PCWSTR value_text = cell_text(package, table, row, L"Value");
    ULONG action_column = MimeMsiColumn(table, L"Action", FALSE);
    int action = action_column == UINT32_MAX ? MSI_NULL_INTEGER : MimeMsiInteger(table, row, action_column);
    if (!file || !section_text || !key_text || action < INI_ADD_LINE || action > INI_REMOVE_TAG)
        return ERROR_INSTALL_PACKAGE_INVALID;
    /* what a row of the IniFile table wrote is taken out again. */
    if (undo) {
        if (action == INI_REMOVE_LINE || action == INI_REMOVE_TAG)
            return ERROR_SUCCESS;
        action = action == INI_ADD_TAG ? INI_REMOVE_TAG : INI_REMOVE_LINE;
    }
    PCWSTR base = MimeMsiGetProperty(package, directory && *directory ? directory : L"WindowsFolder");
    if (!*base) {
        DbgPrint("msi: no folder for the .ini file %ws\n", file);
        return ERROR_INSTALL_FAILURE;
    }
    PCWSTR bar = wcsrchr(file, L'|');
    PWSTR path = MimeMsiJoinPath(base, bar ? bar + 1 : file);
    PWSTR section = nullptr, key = nullptr, value = nullptr;
    UINT error = path ? MimeMsiFormat(package, section_text, &section) : ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = MimeMsiFormat(package, key_text, &key);
    if (!error)
        error = MimeMsiFormat(package, value_text, &value);
    if (!error)
        error = apply(package, path, section, key, value, action);
    MimeMsiFree(value);
    MimeMsiFree(key);
    MimeMsiFree(section);
    MimeMsiFree(path);
    return error;
}

/* the rows of a table whose component is being installed, or being removed. */
static UINT ini_rows(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL removing, BOOL undo)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, name, &table);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        return ERROR_SUCCESS;
    if (!error && table->Rows)
        error = MimeMsiRequireTransaction(package);
    for (ULONG row = 0; !error && row < table->Rows; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        INSTALLSTATE installed, action;
        if (!component)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = MimeMsiComponentState(package, component, &installed, &action);
        if (!error && action == (removing ? INSTALLSTATE_ABSENT : INSTALLSTATE_LOCAL))
            error = ini_row(package, table, row, undo);
    }
    return error;
}

UINT MimeMsiIniAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    BOOL write = !wcscmp(action, L"WriteIniValues");
    if (!write && wcscmp(action, L"RemoveIniValues"))
        return UINT32_MAX;
    if (write)
        return ini_rows(package, L"IniFile", FALSE, FALSE);
    UINT error = ini_rows(package, L"IniFile", TRUE, TRUE);
    return error ? error : ini_rows(package, L"RemoveIniFile", FALSE, FALSE);
}
