/*
 * the actions that copy and move files the package has put in place: the DuplicateFile and MoveFile tables, and the BindImage
 * table, whose rows ask for an optimization that leaves the files as they work without it.
 */
#include "msi_private.h"

static PCWSTR cell_text(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    PCWSTR text = index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
    return text && *text ? text : nullptr;
}

static UINT optional_table(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_TABLE **table)
{
    UINT error = MimeMsiLoadTable(package->Database, name, table);
    if (error == ERROR_BAD_QUERY_SYNTAX) {
        *table = nullptr;
        return ERROR_SUCCESS;
    }
    return error;
}

/* the folder a column names, as a property that holds a path or as a directory of the package. */
static UINT folder_path(MIME_MSI_PACKAGE *package, PCWSTR name, PWSTR *out)
{
    PCWSTR value = MimeMsiGetProperty(package, name);
    if (*value) {
        *out = MimeMsiDuplicate(value);
        return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    UINT error = MimeMsiDirectoryPath(package, name, FALSE, out);
    return error == ERROR_DIRECTORY ? ERROR_INSTALL_PACKAGE_INVALID : error;
}

/* whether a row follows its component being installed, or being removed. */
static UINT wanted(MIME_MSI_PACKAGE *package, PCWSTR component, BOOL remove, BOOL *out)
{
    INSTALLSTATE installed, action;
    *out = FALSE;
    UINT error = MimeMsiComponentState(package, component, &installed, &action);
    if (error == ERROR_UNKNOWN_COMPONENT)
        return ERROR_SUCCESS;
    if (!error)
        *out = action == (remove ? INSTALLSTATE_ABSENT : INSTALLSTATE_LOCAL);
    return error;
}

static PCWSTR file_name_of(PCWSTR path)
{
    PCWSTR separator = wcsrchr(path, L'\\');
    return separator ? separator + 1 : path;
}

/* where the copy of a file goes: the folder, and the name the row gives or the file has. */
static UINT duplicate_target(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR source, PWSTR *target)
{
    PCWSTR folder = cell_text(package, table, row, L"DestFolder");
    if (!folder)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PWSTR directory = nullptr, name = nullptr;
    UINT error = folder_path(package, folder, &directory);
    PCWSTR raw = cell_text(package, table, row, L"DestName");
    if (!error && raw)
        error = MimeMsiFormat(package, raw, &name);
    if (!error)
        *target = MimeMsiJoinPath(directory, name && *name ? name : file_name_of(source));
    if (!error && !*target)
        error = ERROR_NOT_ENOUGH_MEMORY;
    MimeMsiFree(directory);
    MimeMsiFree(name);
    return error;
}

static UINT duplicate_files(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"DuplicateFile", &table);
    if (error || !table || !table->Rows)
        return error;
    error = MimeMsiRequireTransaction(package);
    /* the file to copy is in place once the files are. */
    if (!error && !remove)
        error = MimeMsiInstallQueuedFiles(package);
    for (ULONG row = 0; !error && row < table->Rows; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR file = cell_text(package, table, row, L"File_");
        BOOL use;
        if (!component || !file)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = wanted(package, component, remove, &use);
        if (error || !use)
            continue;
        PWSTR source = nullptr, target = nullptr;
        error = MimeMsiFileTargetPath(package, file, &source);
        if (!error)
            error = duplicate_target(package, table, row, source, &target);
        if (!error)
            error = remove ? MimeMsiRemoveFile(package, target) : MimeMsiCopyInstalledFile(package, source, target);
        MimeMsiFree(source);
        MimeMsiFree(target);
    }
    return error;
}

static BOOL has_wildcard(PCWSTR name)
{
    return wcschr(name, L'*') || wcschr(name, L'?');
}

/* one file of a move: copied to its place, and taken away from where it was when the row moves. */
static UINT move_one(MIME_MSI_PACKAGE *package, PCWSTR source, PCWSTR target, BOOL move)
{
    PWSTR parent = MimeMsiDuplicate(target);
    PWSTR separator = parent ? wcsrchr(parent, L'\\') : nullptr;
    UINT error = parent ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    if (!error && separator) {
        *separator = 0;
        error = MimeMsiMakeDirectory(package, parent);
    }
    MimeMsiFree(parent);
    if (!error)
        error = MimeMsiCopyInstalledFile(package, source, target);
    if (!error && move)
        error = MimeMsiRemoveFile(package, source);
    return error;
}

static UINT move_row(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    PCWSTR component = cell_text(package, table, row, L"Component_");
    PCWSTR from = cell_text(package, table, row, L"SourceFolder");
    PCWSTR to = cell_text(package, table, row, L"DestFolder");
    PCWSTR pattern = cell_text(package, table, row, L"SourceName");
    PCWSTR rename = cell_text(package, table, row, L"DestName");
    ULONG options_column = MimeMsiColumn(table, L"Options", FALSE);
    int options = options_column == UINT32_MAX ? 0 : MimeMsiInteger(table, row, options_column);
    BOOL use;
    if (!component || !to)
        return ERROR_INSTALL_PACKAGE_INVALID;
    UINT error = wanted(package, component, FALSE, &use);
    if (error || !use)
        return error;
    PWSTR source_folder = nullptr, target_folder = nullptr, source_name = nullptr, target_name = nullptr;
    error = from ? folder_path(package, from, &source_folder) : MimeMsiComponentDirectory(package, component, &source_folder);
    if (!error)
        error = folder_path(package, to, &target_folder);
    if (!error && pattern)
        error = MimeMsiFormat(package, pattern, &source_name);
    if (!error && rename)
        error = MimeMsiFormat(package, rename, &target_name);
    if (!error) {
        PCWSTR what = source_name && *source_name ? source_name : L"*";
        PWSTR search = MimeMsiJoinPath(source_folder, what);
        WIN32_FIND_DATAW found;
        HANDLE handle = search ? FindFirstFileW(search, &found) : INVALID_HANDLE_VALUE;
        if (!search)
            error = ERROR_NOT_ENOUGH_MEMORY;
        /* a name that matches no file moves nothing. */
        if (handle != INVALID_HANDLE_VALUE) {
            do {
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                PWSTR from_path = MimeMsiJoinPath(source_folder, found.cFileName);
                PWSTR to_path = MimeMsiJoinPath(target_folder, target_name && *target_name && !has_wildcard(what) ?
                                                target_name : found.cFileName);
                error = from_path && to_path ? move_one(package, from_path, to_path, options != MSI_NULL_INTEGER && (options & 1)) :
                        ERROR_NOT_ENOUGH_MEMORY;
                MimeMsiFree(from_path);
                MimeMsiFree(to_path);
            } while (!error && FindNextFileW(handle, &found));
            FindClose(handle);
        }
        MimeMsiFree(search);
    }
    MimeMsiFree(source_folder);
    MimeMsiFree(target_folder);
    MimeMsiFree(source_name);
    MimeMsiFree(target_name);
    return error;
}

static UINT move_files(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"MoveFile", &table);
    if (error || !table || !table->Rows)
        return error;
    error = MimeMsiRequireTransaction(package);
    if (!error)
        error = MimeMsiInstallQueuedFiles(package);
    for (ULONG row = 0; !error && row < table->Rows; row++)
        error = move_row(package, table, row);
    return error;
}

UINT MimeMsiFileTablesAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (!wcscmp(action, L"DuplicateFiles"))
        return duplicate_files(package, FALSE);
    if (!wcscmp(action, L"RemoveDuplicateFiles"))
        return duplicate_files(package, TRUE);
    if (!wcscmp(action, L"MoveFiles"))
        return move_files(package);
    /* the images work unbound, which only saves the loader the work of binding them. */
    if (!wcscmp(action, L"BindImage"))
        return ERROR_SUCCESS;
    return UINT32_MAX;
}
