#include "msi_private.h"
#include "../lib/cabinet/cabinet.h"
#include <mime/win32/winver.h>

typedef struct MIME_MSI_FILE_PLAN {
    struct MIME_MSI_FILE_PLAN *Next;
    PWSTR Source, Target;
    DWORD Size, Attributes;
    const BYTE *Data;
    PCWSTR Key, Version, Language, Cabinet;
    DWORD Mode;
    /* a file of the File table is judged by the versioning rules, other data is written whatever is there. */
    BOOL Judged, Decided, Keep, HasHash;
    MIME_MSI_FILE_HASH Hash;
    /* a companion file follows the file its Version column names. */
    PWSTR ParentTarget;
    PCWSTR ParentVersion, ParentLanguage;
    BOOL Done;
} MIME_MSI_FILE_PLAN;

typedef struct MIME_MSI_CHANGE {
    struct MIME_MSI_CHANGE *Next;
    PWSTR Path, Backup;
    BOOL Directory;
    ULONG RegistryKind;
    HKEY Root;
    PWSTR ValueName;
    BYTE *OldData;
    DWORD OldType, OldBytes, View;
    BOOL HadValue, Removed;
    MIME_MSI_UNDO Undo;
    void *UndoContext;
} MIME_MSI_CHANGE;

/* an action that waits for the files, such as starting a service the package has just installed. */
typedef struct MIME_MSI_QUEUED_ACTION {
    struct MIME_MSI_QUEUED_ACTION *Next;
    MIME_MSI_QUEUED Run;
    UINT_PTR Argument;
} MIME_MSI_QUEUED_ACTION;

static void free_queue(MIME_MSI_PACKAGE *package)
{
    while (package->Queued) {
        MIME_MSI_QUEUED_ACTION *action = package->Queued;
        package->Queued = action->Next;
        MimeMsiFree(action);
    }
}

static void free_plan(MIME_MSI_PACKAGE *package)
{
    while (package->Files) {
        MIME_MSI_FILE_PLAN *file = package->Files;
        package->Files = file->Next;
        MimeMsiFree(file->Source);
        MimeMsiFree(file->Target);
        MimeMsiFree(file->ParentTarget);
        MimeMsiFree(file);
    }
    package->FilesQueued = FALSE;
}

static BOOL absent(DWORD error)
{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static UINT restore_registry(MIME_MSI_CHANGE *change)
{
    if (change->RegistryKind == 2) {
        UINT error = RegDeleteKeyExW(change->Root, change->Path, change->View, 0);
        return absent(error) ? ERROR_SUCCESS : error;
    }
    HKEY key;
    UINT error = RegOpenKeyExW(change->Root, change->Path, 0, KEY_SET_VALUE | change->View, &key);
    /* a value removed along with its key is restored with it. */
    if (error && change->HadValue && absent(error))
        error = RegCreateKeyExW(change->Root, change->Path, 0, nullptr, 0, KEY_SET_VALUE | change->View, nullptr, &key, nullptr);
    if (error)
        return !change->HadValue && absent(error) ? ERROR_SUCCESS : error;
    error = change->HadValue ? RegSetValueExW(key, change->ValueName, 0, change->OldType, change->OldData, change->OldBytes) :
                              RegDeleteValueW(key, change->ValueName);
    RegCloseKey(key);
    return !change->HadValue && absent(error) ? ERROR_SUCCESS : error;
}

UINT MimeMsiWriteRegistry(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name,
                         DWORD type, const void *data, DWORD bytes)
{
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    MIME_MSI_CHANGE *created = MimeMsiAllocate(sizeof(*created));
    MIME_MSI_CHANGE *value = MimeMsiAllocate(sizeof(*value));
    if (!created || !value) {
        MimeMsiFree(created);
        MimeMsiFree(value);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    created->Path = MimeMsiDuplicate(path);
    created->View = value->View = package->RegistryView;
    value->Path = MimeMsiDuplicate(path);
    value->ValueName = MimeMsiDuplicate(name);
    UINT error = !created->Path || !value->Path || !value->ValueName ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    HKEY key = nullptr;
    DWORD disposition;
    if (!error)
        error = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | package->RegistryView, nullptr, &key, &disposition);
    if (!error && disposition == REG_CREATED_NEW_KEY) {
        created->RegistryKind = 2;
        created->Root = root;
        created->Next = package->Changes;
        package->Changes = created;
        created = nullptr;
    }
    if (!error) {
        error = RegQueryValueExW(key, name, nullptr, &value->OldType, nullptr, &value->OldBytes);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_SUCCESS;
        else if (!error) {
            value->HadValue = TRUE;
            if (value->OldBytes > 1024 * 1024)
                error = ERROR_BAD_CONFIGURATION;
            else {
                value->OldData = MimeMsiAllocate(value->OldBytes ? value->OldBytes : 1);
                if (!value->OldData)
                    error = ERROR_NOT_ENOUGH_MEMORY;
                else
                    error = RegQueryValueExW(key, name, nullptr, &value->OldType, value->OldData, &value->OldBytes);
            }
        }
    }
    if (!error)
        error = RegSetValueExW(key, name, 0, type, data, bytes);
    if (key)
        RegCloseKey(key);
    if (!error) {
        value->RegistryKind = 1;
        value->Root = root;
        value->Next = package->Changes;
        package->Changes = value;
        value = nullptr;
    }
    if (created) {
        MimeMsiFree(created->Path);
        MimeMsiFree(created);
    }
    if (value) {
        MimeMsiFree(value->Path);
        MimeMsiFree(value->ValueName);
        MimeMsiFree(value->OldData);
        MimeMsiFree(value);
    }
    return error;
}

UINT MimeMsiFinishTransaction(MIME_MSI_PACKAGE *package, BOOL commit)
{
    UINT error = ERROR_SUCCESS;
    while (package->Changes) {
        MIME_MSI_CHANGE *change = package->Changes;
        package->Changes = change->Next;
        BOOL restored = TRUE;
        if (change->Undo) {
            UINT status = change->Undo(change->UndoContext, !commit);
            if (status && !commit && !error)
                error = status;
        } else if (!commit) {
            if (change->RegistryKind) {
                UINT status = restore_registry(change);
                restored = !status;
                if (status && !error)
                    error = status;
            } else if (change->Directory && change->Removed)
                restored = CreateDirectoryW(change->Path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
            else if (change->Directory)
                restored = RemoveDirectoryW(change->Path) || absent(GetLastError());
            else if (change->Backup)
                restored = CopyFileW(change->Backup, change->Path, FALSE);
            else
                restored = DeleteFileW(change->Path) || absent(GetLastError());
            if (!restored && !error)
                error = GetLastError();
        }
        if (change->Backup && restored && !DeleteFileW(change->Backup)) {
            UINT cleanup = GetLastError();
            DbgPrint("msi: could not remove backup %ws, error %u\n", change->Backup, cleanup);
            if (!commit && !error)
                error = cleanup;
        }
        if (!restored && change->Backup)
            DbgPrint("msi: rollback failed, original file retained at %ws\n", change->Backup);
        MimeMsiFree(change->Path);
        MimeMsiFree(change->Backup);
        MimeMsiFree(change->ValueName);
        MimeMsiFree(change->OldData);
        MimeMsiFree(change);
    }
    free_plan(package);
    free_queue(package);
    if (package->TransactionActive && commit && !error)
        package->TransactionCommitted = TRUE;
    package->TransactionActive = FALSE;
    package->CatalogRegistered = FALSE;
    return error;
}

static UINT make_directory(MIME_MSI_PACKAGE *package, PCWSTR path, ULONG depth)
{
    if (depth >= 128)
        return ERROR_BAD_PATHNAME;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return attributes & FILE_ATTRIBUTE_DIRECTORY ? ERROR_SUCCESS : ERROR_DIRECTORY;
    DWORD error = GetLastError();
    if (!absent(error))
        return error;
    PWSTR parent = MimeMsiDuplicate(path);
    if (!parent)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T length = wcslen(parent);
    while (length && parent[length - 1] == L'\\')
        parent[--length] = 0;
    PWSTR separator = wcsrchr(parent, L'\\');
    if (!separator || separator < parent + 2) {
        MimeMsiFree(parent);
        return ERROR_PATH_NOT_FOUND;
    }
    if (separator == parent + 2 && parent[1] == L':')
        separator[1] = 0;
    else
        *separator = 0;
    error = make_directory(package, parent, depth + 1);
    MimeMsiFree(parent);
    if (error)
        return error;
    MIME_MSI_CHANGE *change = MimeMsiAllocate(sizeof(*change));
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    change->Path = MimeMsiDuplicate(path);
    if (!change->Path) {
        MimeMsiFree(change);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!CreateDirectoryW(path, nullptr)) {
        error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            attributes = GetFileAttributesW(path);
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                error = ERROR_SUCCESS;
        }
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    change->Directory = TRUE;
    change->Next = package->Changes;
    package->Changes = change;
    return ERROR_SUCCESS;
}

UINT MimeMsiMakeDirectory(MIME_MSI_PACKAGE *package, PCWSTR path)
{
    return make_directory(package, path, 0);
}

static MIME_MSI_CHANGE *new_change(MIME_MSI_PACKAGE *package, PCWSTR path)
{
    MIME_MSI_CHANGE *change = MimeMsiAllocate(sizeof(*change));
    if (!change)
        return nullptr;
    change->Path = MimeMsiDuplicate(path);
    if (!change->Path) {
        MimeMsiFree(change);
        return nullptr;
    }
    change->View = package->RegistryView;
    return change;
}

static void push_change(MIME_MSI_PACKAGE *package, MIME_MSI_CHANGE *change)
{
    change->Next = package->Changes;
    package->Changes = change;
}

/* records a file the package created outside the installed files, for a rollback to delete. */
UINT MimeMsiNoteNewFile(MIME_MSI_PACKAGE *package, PCWSTR path)
{
    MIME_MSI_CHANGE *change = new_change(package, path);
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    push_change(package, change);
    return ERROR_SUCCESS;
}

/* the callback ends the transaction's hold on the context, undoing the work when the install rolls back. */
UINT MimeMsiNoteUndo(MIME_MSI_PACKAGE *package, MIME_MSI_UNDO undo, void *context)
{
    MIME_MSI_CHANGE *change = MimeMsiAllocate(sizeof(*change));
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    change->Undo = undo;
    change->UndoContext = context;
    push_change(package, change);
    return ERROR_SUCCESS;
}

/* a key without values, and the parents it needs, which a rollback deletes when this call made them. */
UINT MimeMsiCreateRegistryKey(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path)
{
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    PCWSTR separator = wcsrchr(path, L'\\');
    if (separator) {
        PWSTR parent = MimeMsiDuplicate(path);
        if (!parent)
            return ERROR_NOT_ENOUGH_MEMORY;
        parent[separator - path] = 0;
        UINT status = MimeMsiCreateRegistryKey(package, root, parent);
        MimeMsiFree(parent);
        if (status)
            return status;
    }
    MIME_MSI_CHANGE *change = new_change(package, path);
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    HKEY key;
    DWORD disposition;
    UINT error = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_READ | package->RegistryView, nullptr, &key, &disposition);
    if (error || disposition != REG_CREATED_NEW_KEY) {
        if (!error)
            RegCloseKey(key);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    RegCloseKey(key);
    change->RegistryKind = 2;
    change->Root = root;
    push_change(package, change);
    return ERROR_SUCCESS;
}

/* queued actions run in order once the files are in place. */
UINT MimeMsiQueueAction(MIME_MSI_PACKAGE *package, MIME_MSI_QUEUED run, UINT_PTR argument)
{
    MIME_MSI_QUEUED_ACTION *action = MimeMsiAllocate(sizeof(*action));
    if (!action)
        return ERROR_NOT_ENOUGH_MEMORY;
    action->Run = run;
    action->Argument = argument;
    MIME_MSI_QUEUED_ACTION **tail = &package->Queued;
    while (*tail)
        tail = &(*tail)->Next;
    *tail = action;
    return ERROR_SUCCESS;
}

/* keeps the old copy of a file about to be replaced or removed until the transaction ends. */
UINT MimeMsiBackupFile(MIME_MSI_PACKAGE *package, PCWSTR path, BOOL *existed)
{
    *existed = FALSE;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return absent(GetLastError()) ? ERROR_SUCCESS : GetLastError();
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        return ERROR_ACCESS_DENIED;
    MIME_MSI_CHANGE *change = new_change(package, path);
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T capacity = wcslen(path) + 48;
    change->Backup = MimeMsiAllocate(capacity * sizeof(WCHAR));
    UINT error = change->Backup ? ERROR_FILE_EXISTS : ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG attempt = 0; change->Backup && attempt < 128; attempt++) {
        _snwprintf(change->Backup, capacity, L"%s.%08x-%08x.bak", path, GetCurrentProcessId(), ++package->BackupCounter);
        if (CopyFileW(path, change->Backup, TRUE)) {
            error = ERROR_SUCCESS;
            break;
        }
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            break;
    }
    if (error) {
        MimeMsiFree(change->Backup);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    push_change(package, change);
    *existed = TRUE;
    return ERROR_SUCCESS;
}

/* the file is moved aside rather than copied, so its folder can be emptied before the transaction ends. */
UINT MimeMsiRemoveFile(MIME_MSI_PACKAGE *package, PCWSTR path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return absent(GetLastError()) ? ERROR_SUCCESS : GetLastError();
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        return ERROR_ACCESS_DENIED;
    WCHAR directory[MAX_PATH];
    UINT length = GetWindowsDirectoryW(directory, RTL_NUMBER_OF(directory));
    if (!length || length + 64 >= RTL_NUMBER_OF(directory))
        return ERROR_BAD_PATHNAME;
    wcscpy(directory + length, L"\\Installer");
    CreateDirectoryW(directory, nullptr);
    MIME_MSI_CHANGE *change = new_change(package, path);
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T capacity = wcslen(directory) + 48;
    change->Backup = MimeMsiAllocate(capacity * sizeof(WCHAR));
    UINT error = change->Backup ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    if (!error && (attributes & FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(path, attributes & ~FILE_ATTRIBUTE_READONLY))
        error = GetLastError();
    for (ULONG attempt = 0; !error && attempt < 128; attempt++) {
        _snwprintf(change->Backup, capacity, L"%s\\%08x-%08x.rbf", directory, GetCurrentProcessId(), ++package->BackupCounter);
        if (MoveFileExW(path, change->Backup, MOVEFILE_COPY_ALLOWED))
            break;
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            break;
        error = ERROR_SUCCESS;
        if (attempt == 127)
            error = ERROR_FILE_EXISTS;
    }
    if (error) {
        MimeMsiFree(change->Backup);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    push_change(package, change);
    return ERROR_SUCCESS;
}

/* only an empty directory is removed, a non empty one is not an error. */
UINT MimeMsiRemoveDirectory(MIME_MSI_PACKAGE *package, PCWSTR path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_SUCCESS;
    if (!RemoveDirectoryW(path))
        return ERROR_SUCCESS;
    MIME_MSI_CHANGE *change = new_change(package, path);
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    change->Directory = change->Removed = TRUE;
    push_change(package, change);
    return ERROR_SUCCESS;
}

/* a rollback restores the deleted value. */
UINT MimeMsiDeleteRegistryValue(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name)
{
    HKEY key;
    UINT error = RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | package->RegistryView, &key);
    if (error)
        return absent(error) ? ERROR_SUCCESS : error;
    MIME_MSI_CHANGE *change = new_change(package, path);
    if (!change) {
        RegCloseKey(key);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    change->ValueName = MimeMsiDuplicate(name);
    change->Root = root;
    change->RegistryKind = 1;
    error = !change->ValueName ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error) {
        error = RegQueryValueExW(key, name, nullptr, &change->OldType, nullptr, &change->OldBytes);
        if (error == ERROR_FILE_NOT_FOUND) {
            RegCloseKey(key);
            MimeMsiFree(change->ValueName);
            MimeMsiFree(change->Path);
            MimeMsiFree(change);
            return ERROR_SUCCESS;
        }
        if (!error) {
            change->OldData = MimeMsiAllocate(change->OldBytes ? change->OldBytes : 1);
            error = change->OldData ? RegQueryValueExW(key, name, nullptr, &change->OldType, change->OldData, &change->OldBytes) :
                                      ERROR_NOT_ENOUGH_MEMORY;
        }
        change->HadValue = !error;
    }
    if (!error)
        error = RegDeleteValueW(key, name);
    RegCloseKey(key);
    if (error) {
        MimeMsiFree(change->OldData);
        MimeMsiFree(change->ValueName);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    push_change(package, change);
    return ERROR_SUCCESS;
}

/* deletes a key tree value by value, so a rollback can restore it. */
UINT MimeMsiDeleteRegistryTree(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, ULONG depth)
{
    if (depth > 64)
        return ERROR_BAD_PATHNAME;
    HKEY key;
    UINT error = RegOpenKeyExW(root, path, 0, KEY_READ | package->RegistryView, &key);
    if (error)
        return absent(error) ? ERROR_SUCCESS : error;
    DWORD subkeys, longest_key, values, longest_value;
    error = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, &longest_key, nullptr, &values, &longest_value,
                             nullptr, nullptr, nullptr);
    PWSTR name = error ? nullptr : MimeMsiAllocate(((SIZE_T)(longest_key > longest_value ? longest_key : longest_value) + 2) * sizeof(WCHAR));
    if (!error && !name)
        error = ERROR_NOT_ENOUGH_MEMORY;
    while (!error && subkeys) {
        DWORD length = longest_key + 1;
        error = RegEnumKeyExW(key, --subkeys, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (error)
            break;
        SIZE_T parent = wcslen(path);
        PWSTR child = MimeMsiAllocate((parent + length + 2) * sizeof(WCHAR));
        if (!child) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        _snwprintf(child, parent + length + 2, L"%s\\%s", path, name);
        error = MimeMsiDeleteRegistryTree(package, root, child, depth + 1);
        MimeMsiFree(child);
    }
    while (!error && values) {
        DWORD length = longest_value + 1;
        error = RegEnumValueW(key, --values, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (!error)
            error = MimeMsiDeleteRegistryValue(package, root, path, name);
    }
    MimeMsiFree(name);
    RegCloseKey(key);
    if (error)
        return error;
    /* a rollback recreates the key as a new one, undoing only its absence. */
    error = RegDeleteKeyExW(root, path, package->RegistryView, 0);
    return absent(error) ? ERROR_SUCCESS : error;
}

static BOOL list_contains(PCWSTR list, PCWSTR name)
{
    SIZE_T length = wcslen(name);
    if (!wcscmp(list, L"ALL"))
        return TRUE;
    while (*list) {
        PCWSTR end = wcschr(list, L',');
        SIZE_T count = end ? (SIZE_T)(end - list) : wcslen(list);
        if (count == length && !wcsncmp(list, name, count))
            return TRUE;
        if (!end)
            break;
        list = end + 1;
    }
    return FALSE;
}

static ULONG feature_row(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features, ULONG id, PCWSTR name)
{
    for (ULONG row = 0; row < features->Rows; row++) {
        PCWSTR key = MimeMsiText(package->Database, features, row, id);
        if (key && !wcscmp(name, key))
            return row;
    }
    return UINT32_MAX;
}

/* a feature named in the list, or one of its ancestors, as a feature turned on turns its parents on. */
static BOOL list_reaches(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features, ULONG id, ULONG parent_column, PCWSTR list,
                         PCWSTR name)
{
    if (!*list)
        return FALSE;
    if (list_contains(list, name))
        return TRUE;
    for (ULONG row = 0; row < features->Rows; row++) {
        PCWSTR child = MimeMsiText(package->Database, features, row, id);
        if (!child || !list_contains(list, child))
            continue;
        PCWSTR up = MimeMsiText(package->Database, features, row, parent_column);
        for (ULONG depth = 0; up && *up && depth < 64; depth++) {
            if (!wcscmp(up, name))
                return TRUE;
            ULONG at = feature_row(package, features, id, up);
            up = at == UINT32_MAX ? nullptr : MimeMsiText(package->Database, features, at, parent_column);
        }
    }
    return FALSE;
}

/*
 * the state a feature is installed in, as the engine of windows sets it. with none of ADDLOCAL, REMOVE and REINSTALL
 * on a first install, a feature is installed when its level is within INSTALLLEVEL and its parent is. with them, the
 * features they name are, the parents of a named one too, and a feature marked to follow its parent takes the
 * parent's state; the others stay as they are.
 */
UINT MimeMsiFeatureSelected(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features,
                            PCWSTR name, ULONG depth, BOOL *selected)
{
    *selected = FALSE;
    if (depth >= 64)
        return ERROR_BAD_CONFIGURATION;
    ULONG id = MimeMsiColumn(features, L"Feature", TRUE);
    ULONG level_column = MimeMsiColumn(features, L"Level", FALSE);
    ULONG parent_column = MimeMsiColumn(features, L"Feature_Parent", TRUE);
    ULONG attributes_column = MimeMsiColumn(features, L"Attributes", FALSE);
    if (id == UINT32_MAX || level_column == UINT32_MAX || parent_column == UINT32_MAX || attributes_column == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG row = feature_row(package, features, id, name);
    if (row == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    int level = MimeMsiInteger(features, row, level_column);
    if (level < 0)
        return ERROR_INSTALL_PACKAGE_INVALID;
    level = MimeMsiFeatureLevel(package, name, level);
    int attributes = MimeMsiInteger(features, row, attributes_column);
    BOOL follows = attributes != MSI_NULL_INTEGER && (attributes & 2);
    PCWSTR parent = MimeMsiText(package->Database, features, row, parent_column);
    BOOL parent_selected = TRUE;
    if (parent && *parent) {
        UINT error = MimeMsiFeatureSelected(package, features, parent, depth + 1, &parent_selected);
        if (error)
            return error;
    }
    PCWSTR install_level = MimeMsiGetProperty(package, L"INSTALLLEVEL");
    int limit = 1;
    if (*install_level) {
        limit = 0;
        for (PCWSTR digit = install_level; *digit; digit++) {
            if (*digit < L'0' || *digit > L'9' || limit > (32767 - (*digit - L'0')) / 10)
                return ERROR_INVALID_PARAMETER;
            limit = limit * 10 + *digit - L'0';
        }
    }
    PCWSTR local = MimeMsiGetProperty(package, L"ADDLOCAL"), removed = MimeMsiGetProperty(package, L"REMOVE");
    PCWSTR reinstall = MimeMsiGetProperty(package, L"REINSTALL");
    BOOL requested = *local || *removed || *reinstall;
    BOOL installed = FALSE;
    for (ULONG i = 0; i < package->FeatureCount; i++)
        if (!wcscmp(package->Features[i].Name, name))
            installed = package->Features[i].Installed == INSTALLSTATE_LOCAL;
    if (!requested) {
        *selected = parent_selected && level && level <= limit;
        if (package->Maintenance && !package->ConfigureDefaults)
            *selected = installed;
    } else {
        /* the state it has, which on a first install is the one its level gives, then what the properties ask for it. */
        *selected = package->Maintenance ? installed : parent_selected && level && level <= limit;
        if (follows && parent && *parent)
            *selected = parent_selected;
        if (list_reaches(package, features, id, parent_column, local, name))
            *selected = TRUE;
        if (list_contains(reinstall, name) && installed)
            *selected = TRUE;
        *selected = *selected && level;
    }
    if (list_contains(removed, name))
        *selected = FALSE;
    for (ULONG i = 0; i < package->FeatureCount; i++) {
        MIME_MSI_FEATURE *feature = &package->Features[i];
        if (!wcscmp(feature->Name, name) && feature->Requested != INSTALLSTATE_UNKNOWN)
            *selected = level && feature->Requested == INSTALLSTATE_LOCAL;
    }
    /* a feature is only there with its parent. */
    if (!parent_selected)
        *selected = FALSE;
    return ERROR_SUCCESS;
}

UINT MimeMsiComponentSelected(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR *directory, BOOL *selected)
{
    MIME_MSI_DATABASE *database = package->Database;
    MIME_MSI_TABLE *components, *links, *features;
    UINT error = MimeMsiLoadTable(database, L"Component", &components);
    if (!error)
        error = MimeMsiLoadTable(database, L"FeatureComponents", &links);
    if (!error)
        error = MimeMsiLoadTable(database, L"Feature", &features);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(components, L"Component", TRUE);
    ULONG dir = MimeMsiColumn(components, L"Directory_", TRUE);
    ULONG condition = MimeMsiColumn(components, L"Condition", TRUE);
    ULONG attributes = MimeMsiColumn(components, L"Attributes", FALSE);
    ULONG feature = MimeMsiColumn(links, L"Feature_", TRUE);
    ULONG component = MimeMsiColumn(links, L"Component_", TRUE);
    if (id == UINT32_MAX || dir == UINT32_MAX || condition == UINT32_MAX || attributes == UINT32_MAX ||
        feature == UINT32_MAX || component == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG row;
    for (row = 0; row < components->Rows; row++) {
        PCWSTR key = MimeMsiText(database, components, row, id);
        if (key && !wcscmp(key, name))
            break;
    }
    if (row == components->Rows)
        return ERROR_INSTALL_PACKAGE_INVALID;
    *directory = MimeMsiText(database, components, row, dir);
    if (!*directory || !**directory)
        return ERROR_INSTALL_PACKAGE_INVALID;
    *selected = FALSE;
    for (ULONG i = 0; i < links->Rows; i++) {
        PCWSTR key = MimeMsiText(database, links, i, component);
        if (!key || wcscmp(key, name))
            continue;
        PCWSTR feature_name = MimeMsiText(database, links, i, feature);
        if (!feature_name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL enabled;
        error = MimeMsiFeatureSelected(package, features, feature_name, 0, &enabled);
        if (error)
            return error;
        *selected |= enabled;
    }
    if (!*selected)
        return ERROR_SUCCESS;
    PCWSTR expression = MimeMsiText(database, components, row, condition);
    MSICONDITION result = MimeMsiEvaluateCondition(package, expression ? expression : L"");
    if (result == MSICONDITION_ERROR)
        return ERROR_INSTALL_FAILURE;
    if (result == MSICONDITION_FALSE)
        *selected = FALSE;
    /* source and data source components are not installed, the rest are read as met. a transitive component, which a
       reinstall tests again, is installed like any other. */
    int flags = MimeMsiInteger(components, row, attributes);
    if (*selected && flags != MSI_NULL_INTEGER && (flags & ~(2 | 4 | 8 | 16 | 64 | 128 | 256 | 512 | 1024 | 2048)))
        return ERROR_CALL_NOT_IMPLEMENTED;
    return ERROR_SUCCESS;
}

static UINT maintenance_selection(MIME_MSI_PACKAGE *package)
{
    UINT error = MimeMsiValidateMaintenance(package);
    if (error || !package->Maintenance)
        return error;
    MIME_MSI_TABLE *features;
    error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    for (ULONG i = 0; !error && i < package->FeatureCount; i++) {
        BOOL selected;
        error = MimeMsiFeatureSelected(package, features, package->Features[i].Name, 0, &selected);
    }
    PCWSTR list = MimeMsiGetProperty(package, L"REINSTALL");
    if (!wcscmp(list, L"ALL"))
        return error;
    while (!error && *list) {
        PCWSTR end = wcschr(list, L',');
        SIZE_T length = end ? (SIZE_T)(end - list) : wcslen(list);
        BOOL found = FALSE;
        for (ULONG i = 0; i < package->FeatureCount; i++)
            if (length == wcslen(package->Features[i].Name) && !wcsncmp(list, package->Features[i].Name, length))
                found = TRUE;
        if (!found)
            return ERROR_UNKNOWN_FEATURE;
        if (!end)
            break;
        list = end + 1;
        if (!*list)
            return ERROR_UNKNOWN_FEATURE;
    }
    return error;
}

/* whether the component is installed by this run, because its feature is new or is to be reinstalled. */
UINT MimeMsiComponentRequested(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL *requested)
{
    *requested = !package->Maintenance;
    if (*requested)
        return ERROR_SUCCESS;
    MIME_MSI_TABLE *links, *features;
    UINT error = MimeMsiLoadTable(package->Database, L"FeatureComponents", &links);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    if (error)
        return error;
    ULONG feature = MimeMsiColumn(links, L"Feature_", TRUE), component = MimeMsiColumn(links, L"Component_", TRUE);
    if (feature == UINT32_MAX || component == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < links->Rows; row++) {
        PCWSTR key = MimeMsiText(package->Database, links, row, component);
        if (!key || wcscmp(key, name))
            continue;
        PCWSTR linked = MimeMsiText(package->Database, links, row, feature);
        if (!linked)
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL selected;
        error = MimeMsiFeatureSelected(package, features, linked, 0, &selected);
        if (error)
            return error;
        if (!selected)
            continue;
        for (ULONG i = 0; i < package->FeatureCount; i++) {
            if (wcscmp(linked, package->Features[i].Name))
                continue;
            if (package->Features[i].Installed != INSTALLSTATE_LOCAL ||
                list_contains(MimeMsiGetProperty(package, L"REINSTALL"), linked))
                *requested = TRUE;
        }
    }
    return ERROR_SUCCESS;
}

/* the cabinet of the medium holding a sequence number, or none when that medium has no cabinet. */
static PCWSTR media_cabinet(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *media, int sequence)
{
    ULONG last_column = MimeMsiColumn(media, L"LastSequence", FALSE);
    ULONG cabinet_column = MimeMsiColumn(media, L"Cabinet", TRUE);
    if (last_column == UINT32_MAX || cabinet_column == UINT32_MAX || sequence == MSI_NULL_INTEGER)
        return nullptr;
    int best = INT32_MAX;
    PCWSTR cabinet = nullptr;
    for (ULONG row = 0; row < media->Rows; row++) {
        int last = MimeMsiInteger(media, row, last_column);
        if (last == MSI_NULL_INTEGER || last < sequence || last >= best)
            continue;
        best = last;
        cabinet = MimeMsiText(package->Database, media, row, cabinet_column);
    }
    return cabinet && *cabinet ? cabinet : nullptr;
}

/* the hash the package gives for a file, which only counts when its options are zero. */
static UINT file_hash(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *hashes, MIME_MSI_FILE_PLAN *file)
{
    if (!hashes || !file->Key)
        return ERROR_SUCCESS;
    ULONG key = MimeMsiColumn(hashes, L"File_", TRUE), options = MimeMsiColumn(hashes, L"Options", FALSE);
    ULONG parts[4] = {MimeMsiColumn(hashes, L"HashPart1", FALSE), MimeMsiColumn(hashes, L"HashPart2", FALSE),
                      MimeMsiColumn(hashes, L"HashPart3", FALSE), MimeMsiColumn(hashes, L"HashPart4", FALSE)};
    if (key == UINT32_MAX || options == UINT32_MAX || parts[0] == UINT32_MAX || parts[1] == UINT32_MAX ||
        parts[2] == UINT32_MAX || parts[3] == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < hashes->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, hashes, row, key);
        int flags = MimeMsiInteger(hashes, row, options);
        if (!name || wcscmp(name, file->Key) || flags == MSI_NULL_INTEGER || (flags & 0xF))
            continue;
        file->HasHash = TRUE;
        file->Hash.Size = file->Size;
        for (ULONG i = 0; i < 4; i++)
            file->Hash.Words[i] = (DWORD)MimeMsiInteger(hashes, row, parts[i]);
        break;
    }
    return ERROR_SUCCESS;
}

/* a version that is no number names the file whose version the file follows. */
static UINT file_companion(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG key_column, ULONG version_column,
                           ULONG language_column, MIME_MSI_FILE_PLAN *file)
{
    DWORD high, low;
    if (!file->Version || MimeMsiParseVersion(file->Version, &high, &low))
        return ERROR_SUCCESS;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, key_column);
        if (!name || wcscmp(name, file->Version))
            continue;
        UINT error = MimeMsiFileTargetPath(package, name, &file->ParentTarget);
        if (error)
            return error;
        PCWSTR version = MimeMsiText(package->Database, table, row, version_column);
        file->ParentVersion = version ? version : L"";
        file->ParentLanguage = language_column == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, language_column);
        return ERROR_SUCCESS;
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

static UINT plan_files(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error;
    if (*MimeMsiGetProperty(package, L"ADDSOURCE") || *MimeMsiGetProperty(package, L"ADVERTISE") ||
        (!package->Maintenance && (*MimeMsiGetProperty(package, L"REINSTALL") || *MimeMsiGetProperty(package, L"Installed"))))
        return ERROR_CALL_NOT_IMPLEMENTED;
    error = maintenance_selection(package);
    DWORD mode;
    if (!error)
        error = MimeMsiReinstallMode(package, &mode);
    if (error)
        return error;
    error = MimeMsiLoadTable(package->Database, L"File", &table);
    if (error)
        return error;
    MIME_MSI_TABLE *hashes = nullptr;
    error = MimeMsiLoadTable(package->Database, L"MsiFileHash", &hashes);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        hashes = nullptr;
    else if (error)
        return error;
    ULONG language = MimeMsiColumn(table, L"Language", TRUE);
    ULONG component = MimeMsiColumn(table, L"Component_", TRUE);
    ULONG name = MimeMsiColumn(table, L"FileName", TRUE);
    ULONG size = MimeMsiColumn(table, L"FileSize", FALSE);
    ULONG attributes = MimeMsiColumn(table, L"Attributes", FALSE);
    ULONG version = MimeMsiColumn(table, L"Version", TRUE);
    ULONG key_column = MimeMsiColumn(table, L"File", TRUE);
    ULONG sequence_column = MimeMsiColumn(table, L"Sequence", FALSE);
    if (component == UINT32_MAX || name == UINT32_MAX || size == UINT32_MAX ||
        attributes == UINT32_MAX || version == UINT32_MAX || key_column == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    MIME_MSI_TABLE *media = nullptr;
    error = MimeMsiLoadTable(package->Database, L"Media", &media);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        media = nullptr;
    else if (error)
        return error;
    MIME_MSI_FILE_PLAN **tail = &package->Files;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR component_name = MimeMsiText(package->Database, table, row, component);
        PCWSTR filename = MimeMsiText(package->Database, table, row, name);
        PCWSTR file_version = MimeMsiText(package->Database, table, row, version);
        PCWSTR directory = nullptr;
        BOOL selected;
        if (!component_name || !filename || !*filename)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = MimeMsiComponentSelected(package, component_name, &directory, &selected);
        if (error)
            return error;
        if (!selected)
            continue;
        error = MimeMsiComponentRequested(package, component_name, &selected);
        if (error)
            return error;
        if (!selected)
            continue;
        int file_size = MimeMsiInteger(table, row, size);
        int flags = MimeMsiInteger(table, row, attributes);
        if (flags == MSI_NULL_INTEGER)
            flags = 0;
        if (file_size < 0 || flags < 0)
            return ERROR_INSTALL_PACKAGE_INVALID;
        PCWSTR long_name = wcschr(filename, L'|');
        if (long_name)
            filename = long_name + 1;
        if (!*filename || !wcscmp(filename, L".") || !wcscmp(filename, L"..") ||
            wcschr(filename, L'\\') || wcschr(filename, L'/') || wcschr(filename, L':'))
            return ERROR_INSTALL_PACKAGE_INVALID;
        PWSTR source = nullptr, target = nullptr;
        error = MimeMsiDirectoryPath(package, directory, TRUE, &source);
        if (!error)
            error = MimeMsiAssemblyFolder(package, component_name, &target);
        if (!error && !target)
            error = MimeMsiDirectoryPath(package, directory, FALSE, &target);
        MIME_MSI_FILE_PLAN *file = !error ? MimeMsiAllocate(sizeof(*file)) : nullptr;
        if (!error && !file)
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (!error) {
            file->Source = MimeMsiJoinPath(source, filename);
            file->Target = MimeMsiJoinPath(target, filename);
            file->Size = (DWORD)file_size;
            file->Attributes = (DWORD)flags;
            file->Judged = TRUE;
            file->Mode = mode & (REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEOLDERVERSION |
                                 REINSTALLMODE_FILEEQUALVERSION | REINSTALLMODE_FILEEXACT | REINSTALLMODE_FILEREPLACE);
            /* only a file that is marked for it has its checksum verified. */
            if (flags & 0x400)
                file->Mode |= mode & REINSTALLMODE_FILEVERIFY;
            file->Key = MimeMsiText(package->Database, table, row, key_column);
            file->Version = file_version && *file_version ? file_version : nullptr;
            file->Language = language == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, language);
            error = file_hash(package, hashes, file);
            if (!error)
                error = file_companion(package, table, key_column, version, language, file);
            /* compressed files come from their cabinet, the rest from the source tree. */
            if (media && !(flags & 0x2000) && sequence_column != UINT32_MAX)
                file->Cabinet = media_cabinet(package, media, MimeMsiInteger(table, row, sequence_column));
            if (!file->Source || !file->Target)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else if (!_wcsicmp(file->Source, file->Target))
                error = ERROR_INSTALL_PACKAGE_INVALID;
        }
        if (file && error) {
            MimeMsiFree(file->Source);
            MimeMsiFree(file->Target);
            MimeMsiFree(file->ParentTarget);
            MimeMsiFree(file);
        } else if (file) {
            *tail = file;
            tail = &file->Next;
        }
        MimeMsiFree(source);
        MimeMsiFree(target);
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

static UINT validate_cost(MIME_MSI_PACKAGE *package)
{
    ULONGLONG required[26] = {};
    DWORD cluster[26] = {};
    BOOL insufficient = FALSE;
    for (MIME_MSI_FILE_PLAN *file = package->Files; file; file = file->Next) {
        WCHAR drive = file->Target[0];
        if (drive >= L'a' && drive <= L'z')
            drive -= L'a' - L'A';
        if (drive < L'A' || drive > L'Z' || file->Target[1] != L':')
            return ERROR_CALL_NOT_IMPLEMENTED;
        ULONG index = drive - L'A';
        WCHAR root[] = {drive, L':', L'\\', 0};
        if (!cluster[index]) {
            DWORD sectors, bytes, available, total;
            if (!GetDiskFreeSpaceW(root, &sectors, &bytes, &available, &total)) {
                DbgPrint("msi: the space of %ws for %ws is not known, error %u\n", root, file->Target, GetLastError());
                return GetLastError();
            }
            if (!bytes || !sectors || bytes > UINT32_MAX / sectors)
                return ERROR_INVALID_DATA;
            cluster[index] = bytes * sectors;
        }
        ULONGLONG bytes = ((ULONGLONG)file->Size + cluster[index] - 1) / cluster[index] * cluster[index];
        WIN32_FILE_ATTRIBUTE_DATA existing;
        if (GetFileAttributesExW(file->Target, GetFileExInfoStandard, &existing)) {
            if (existing.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                return ERROR_ACCESS_DENIED;
            ULONGLONG old_size = ((ULONGLONG)existing.nFileSizeHigh << 32) | existing.nFileSizeLow;
            if (old_size > UINT64_MAX - cluster[index])
                return ERROR_INVALID_DATA;
            ULONGLONG backup = (old_size + cluster[index] - 1) / cluster[index] * cluster[index];
            if (backup > UINT64_MAX - bytes)
                return ERROR_INVALID_DATA;
            bytes += backup;
        } else if (!absent(GetLastError())) {
            DbgPrint("msi: the target %ws cannot be looked at, error %u\n", file->Target, GetLastError());
            return GetLastError();
        }
        if (bytes > UINT64_MAX - required[index])
            return ERROR_INVALID_DATA;
        required[index] += bytes;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(required); i++) {
        if (!required[i])
            continue;
        WCHAR root[] = {(WCHAR)(L'A' + i), L':', L'\\', 0};
        ULARGE_INTEGER available;
        if (!GetDiskFreeSpaceExW(root, &available, nullptr, nullptr))
            return GetLastError();
        if (required[i] > available.QuadPart)
            insufficient = TRUE;
    }
    UINT error = MimeMsiSetProperty(package, L"OutOfDiskSpace", insufficient ? L"1" : L"0");
    return error ? error : insufficient ? ERROR_DISK_FULL : ERROR_SUCCESS;
}

UINT MimeMsiRefreshFilePlan(MIME_MSI_PACKAGE *package)
{
    if (package->CostStage < 2)
        return ERROR_SUCCESS;
    free_plan(package);
    UINT error = plan_files(package);
    if (!error && package->CostStage >= 3)
        error = validate_cost(package);
    if (error)
        free_plan(package);
    return error;
}

/* the target is created and backed up before the data is written. */
typedef struct INSTALL_TARGET {
    MIME_MSI_FILE_PLAN *File;
    HANDLE Output;
    BOOL Skip;
} INSTALL_TARGET;

/* whether the versioning rules keep the file that is there. */
static UINT keeps_existing(MIME_MSI_FILE_PLAN *file, BOOL *keep)
{
    *keep = FALSE;
    if (!file->Judged)
        return ERROR_SUCCESS;
    if (file->Decided) {
        *keep = file->Keep;
        return ERROR_SUCCESS;
    }
    BOOL overwrite;
    UINT error;
    if (file->ParentTarget)
        error = MimeMsiCompanionOverwrite(file->ParentTarget, file->ParentVersion, file->ParentLanguage, file->Target,
                                          file->HasHash ? &file->Hash : nullptr, file->Mode, &overwrite);
    else
        error = MimeMsiFileOverwrite(file->Target, file->Version ? file->Version : L"", file->Language,
                                     file->HasHash ? &file->Hash : nullptr, file->Mode, &overwrite);
    *keep = !error && !overwrite;
    if (!error) {
        file->Decided = TRUE;
        file->Keep = *keep;
    }
    return error;
}

static UINT target_open(MIME_MSI_PACKAGE *package, MIME_MSI_FILE_PLAN *file, INSTALL_TARGET *target)
{
    memset(target, 0, sizeof(*target));
    target->File = file;
    target->Output = INVALID_HANDLE_VALUE;
    BOOL keep;
    UINT error = keeps_existing(file, &keep);
    if (error || keep) {
        target->Skip = keep;
        return error;
    }
    PWSTR parent = MimeMsiDuplicate(file->Target);
    if (!parent)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR separator = wcsrchr(parent, L'\\');
    if (!separator) {
        MimeMsiFree(parent);
        return ERROR_BAD_PATHNAME;
    }
    *separator = 0;
    error = make_directory(package, parent, 0);
    MimeMsiFree(parent);
    if (error)
        return error;
    DWORD attributes = GetFileAttributesW(file->Target);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return ERROR_ACCESS_DENIED;
        if (attributes & FILE_ATTRIBUTE_READONLY) {
            /* read only files get replaced too, and stay read only if the package asks. */
            if (!SetFileAttributesW(file->Target, attributes & ~FILE_ATTRIBUTE_READONLY))
                return ERROR_ACCESS_DENIED;
        }
    } else if (!absent(GetLastError()))
        return GetLastError();
    MIME_MSI_CHANGE *change = MimeMsiAllocate(sizeof(*change));
    if (!change)
        return ERROR_NOT_ENOUGH_MEMORY;
    change->Path = MimeMsiDuplicate(file->Target);
    if (!change->Path)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error && attributes != INVALID_FILE_ATTRIBUTES) {
        SIZE_T capacity = wcslen(file->Target) + 48;
        change->Backup = MimeMsiAllocate(capacity * sizeof(WCHAR));
        if (!change->Backup)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            error = ERROR_FILE_EXISTS;
            for (ULONG attempt = 0; attempt < 128; attempt++) {
                _snwprintf(change->Backup, capacity, L"%s.%08x-%08x.bak", file->Target,
                           GetCurrentProcessId(), ++package->BackupCounter);
                if (CopyFileW(file->Target, change->Backup, TRUE)) {
                    error = ERROR_SUCCESS;
                    break;
                }
                error = GetLastError();
                if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                    break;
            }
        }
    }
    if (error) {
        MimeMsiFree(change->Path);
        MimeMsiFree(change->Backup);
        MimeMsiFree(change);
        return error;
    }
    HANDLE output = CreateFileW(file->Target, GENERIC_WRITE, 0, nullptr,
                                attributes == INVALID_FILE_ATTRIBUTES ? CREATE_NEW : OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (change->Backup)
            DeleteFileW(change->Backup);
        MimeMsiFree(change->Backup);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    change->Next = package->Changes;
    package->Changes = change;
    if (!SetEndOfFile(output)) {
        error = GetLastError();
        CloseHandle(output);
        return error;
    }
    target->Output = output;
    return ERROR_SUCCESS;
}

static UINT target_write(INSTALL_TARGET *target, const BYTE *data, SIZE_T length)
{
    while (length) {
        DWORD wanted = length < 0x100000 ? (DWORD)length : 0x100000, written;
        if (!WriteFile(target->Output, data, wanted, &written, nullptr))
            return GetLastError();
        if (written != wanted)
            return ERROR_WRITE_FAULT;
        data += wanted;
        length -= wanted;
    }
    return ERROR_SUCCESS;
}

/* sets the finished file's time and attributes, then closes it. */
static UINT target_close(INSTALL_TARGET *target, const FILETIME *time, UINT error)
{
    if (target->Output == INVALID_HANDLE_VALUE)
        return error;
    if (!error && time && !SetFileTime(target->Output, time, time, time))
        error = GetLastError();
    if (!error && !FlushFileBuffers(target->Output))
        error = GetLastError();
    CloseHandle(target->Output);
    target->Output = INVALID_HANDLE_VALUE;
    if (!error) {
        DWORD attributes = FILE_ATTRIBUTE_NORMAL;
        DWORD flags = target->File->Attributes;
        if (flags & 1) attributes |= FILE_ATTRIBUTE_READONLY;
        if (flags & 2) attributes |= FILE_ATTRIBUTE_HIDDEN;
        if (flags & 4) attributes |= FILE_ATTRIBUTE_SYSTEM;
        if ((attributes & ~FILE_ATTRIBUTE_NORMAL) && !SetFileAttributesW(target->File->Target, attributes))
            error = GetLastError();
    }
    return error;
}

static UINT copy_planned_file(MIME_MSI_PACKAGE *package, MIME_MSI_FILE_PLAN *file)
{
    BOOL keep;
    UINT error = keeps_existing(file, &keep);
    if (error || keep)
        return error;
    HANDLE source = INVALID_HANDLE_VALUE;
    FILETIME created = {}, accessed = {}, modified = {};
    if (!file->Data) {
        source = CreateFileW(file->Source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (source == INVALID_HANDLE_VALUE && absent(GetLastError())) {
            /* another source of the product may have the file. */
            PCWSTR root = MimeMsiGetProperty(package, L"SourceDir");
            SIZE_T length = wcslen(root);
            PWSTR other = nullptr;
            if (length && !_wcsnicmp(file->Source, root, length) && MimeMsiFindSourceFile(package, file->Source + length, &other)) {
                source = CreateFileW(other, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                MimeMsiFree(other);
            }
            SetLastError(source == INVALID_HANDLE_VALUE ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS);
        }
        if (source == INVALID_HANDLE_VALUE)
            return GetLastError();
        LARGE_INTEGER size;
        error = !GetFileSizeEx(source, &size) ? GetLastError() :
                size.QuadPart != file->Size ? ERROR_INSTALL_PACKAGE_INVALID : ERROR_SUCCESS;
        if (!error && !GetFileTime(source, &created, &accessed, &modified))
            error = GetLastError();
    }
    INSTALL_TARGET target;
    if (!error)
        error = target_open(package, file, &target);
    if (error || target.Skip) {
        if (source != INVALID_HANDLE_VALUE)
            CloseHandle(source);
        return error;
    }
    BYTE *buffer = file->Data ? nullptr : MimeMsiAllocate(64 * 1024);
    if (!file->Data && !buffer)
        error = ERROR_NOT_ENOUGH_MEMORY;
    DWORD remaining = file->Size;
    while (remaining && !error) {
        DWORD wanted = remaining < 64 * 1024 ? remaining : 64 * 1024, read = wanted;
        const BYTE *data = file->Data ? file->Data + file->Size - remaining : buffer;
        if (!file->Data && !ReadFile(source, buffer, wanted, &read, nullptr))
            error = GetLastError();
        else if (!read)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else
            error = target_write(&target, data, read);
        if (!error)
            remaining -= read;
    }
    error = target_close(&target, file->Data ? nullptr : &modified, error);
    if (source != INVALID_HANDLE_VALUE)
        CloseHandle(source);
    MimeMsiFree(buffer);
    return error;
}

/* one pass over a cabinet's folders hands each file to its target. */
typedef struct EXTRACT_CONTEXT {
    MIME_MSI_PACKAGE *Package;
    MIME_MSI_FILE_PLAN *Plan;
    PCWSTR Cabinet;
    UINT Error;
} EXTRACT_CONTEXT;

typedef struct EXTRACT_FILE {
    INSTALL_TARGET Target;
    BOOL Failed;
    /* a companion file is looked at when its data comes, after the file it follows is done. */
    BOOL Pending;
} EXTRACT_FILE;

static EXTRACT_FILE g_extract_failed = {.Failed = TRUE};

static void *extract_begin(void *context, const MIME_CAB_FILE *file)
{
    EXTRACT_CONTEXT *extract = context;
    WCHAR name[128];
    ULONG length = 0;
    for (const char *p = file->Name; *p && length < RTL_NUMBER_OF(name) - 1; p++)
        name[length++] = (UCHAR)*p;
    name[length] = 0;
    for (MIME_MSI_FILE_PLAN *plan = extract->Plan; plan; plan = plan->Next) {
        if (plan->Done || !plan->Cabinet || wcscmp(plan->Cabinet, extract->Cabinet) || !plan->Key || _wcsicmp(plan->Key, name))
            continue;
        plan->Done = TRUE;
        EXTRACT_FILE *entry = MimeMsiAllocate(sizeof(*entry));
        if (!entry) {
            extract->Error = ERROR_NOT_ENOUGH_MEMORY;
            return &g_extract_failed;
        }
        if (plan->Size != file->Size) {
            extract->Error = ERROR_INSTALL_PACKAGE_INVALID;
            MimeMsiFree(entry);
            return &g_extract_failed;
        }
        if (plan->ParentTarget) {
            entry->Pending = TRUE;
            entry->Target.File = plan;
            entry->Target.Output = INVALID_HANDLE_VALUE;
            return entry;
        }
        extract->Error = target_open(extract->Package, plan, &entry->Target);
        if (extract->Error) {
            MimeMsiFree(entry);
            return &g_extract_failed;
        }
        if (entry->Target.Skip) {
            MimeMsiFree(entry);
            return nullptr;
        }
        return entry;
    }
    return nullptr;
}

static UINT extract_open_pending(EXTRACT_CONTEXT *extract, EXTRACT_FILE *entry)
{
    MIME_MSI_FILE_PLAN *plan = entry->Target.File;
    entry->Pending = FALSE;
    return target_open(extract->Package, plan, &entry->Target);
}

static int extract_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    EXTRACT_CONTEXT *extract = context;
    EXTRACT_FILE *entry = file_context;
    if (entry->Failed)
        return 1;
    if (entry->Pending) {
        extract->Error = extract_open_pending(extract, entry);
        if (extract->Error)
            return 1;
    }
    if (entry->Target.Skip)
        return 0;
    extract->Error = target_write(&entry->Target, data, length);
    return extract->Error != ERROR_SUCCESS;
}

static int extract_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    EXTRACT_CONTEXT *extract = context;
    EXTRACT_FILE *entry = file_context;
    if (entry->Failed)
        return 1;
    if (entry->Pending) {
        extract->Error = extract_open_pending(extract, entry);
        if (extract->Error)
            return 1;
    }
    FILETIME local, utc;
    BOOL has_time = DosDateTimeToFileTime(file->Date, file->Time, &local) && LocalFileTimeToFileTime(&local, &utc);
    extract->Error = target_close(&entry->Target, has_time ? &utc : nullptr, ERROR_SUCCESS);
    MimeMsiFree(entry);
    return extract->Error != ERROR_SUCCESS;
}

/* a cabinet of the source stays in its file, which can be larger than the address space of the installer. */
static int read_cabinet_file(void *context, uint64_t offset, void *buffer, size_t length)
{
    LARGE_INTEGER position = {.QuadPart = (LONGLONG)offset};
    DWORD read;
    return !SetFilePointerEx(context, position, nullptr, FILE_BEGIN) || !ReadFile(context, buffer, (DWORD)length, &read, nullptr) ||
           read != length;
}

static UINT extract_cabinet(MIME_MSI_PACKAGE *package, PCWSTR cabinet)
{
    UINT error = ERROR_SUCCESS;
    BYTE *stream = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    MIME_CAB *cab = nullptr;
    int status = MIME_CAB_OK;
    if (cabinet[0] == L'#') {
        SIZE_T size = 0;
        error = MimeMsiReadBinary(package->Database, cabinet + 1, &stream, &size);
        /* a patch's cabinet is a stream in the patch file. */
        for (ULONG i = 0; error == ERROR_FILE_NOT_FOUND && i < package->Database->PatchCount; i++)
            error = MimeMsiReadBinary(package->Database->Patches[i], cabinet + 1, &stream, &size);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_INSTALL_SOURCE_ABSENT;
        if (!error)
            status = MimeCabOpen(&cab, stream, size, MimeMsiAllocate, MimeMsiFree);
    } else {
        PCWSTR source_dir = MimeMsiGetProperty(package, L"SourceDir");
        PWSTR path = MimeMsiJoinPath(source_dir, cabinet);
        if (!path)
            return ERROR_NOT_ENOUGH_MEMORY;
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        PWSTR other = nullptr;
        if (file == INVALID_HANDLE_VALUE && MimeMsiFindSourceFile(package, cabinet, &other)) {
            file = CreateFileW(other, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                               nullptr);
            MimeMsiFree(other);
        }
        if (file == INVALID_HANDLE_VALUE)
            DbgPrint("msi: the cabinet %ws does not open, error %u\n", path, GetLastError());
        MimeMsiFree(path);
        LARGE_INTEGER length;
        if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &length) || length.QuadPart <= 0) {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            return ERROR_INSTALL_SOURCE_ABSENT;
        }
        status = MimeCabOpenStream(&cab, read_cabinet_file, file, (uint64_t)length.QuadPart, MimeMsiAllocate, MimeMsiFree);
    }
    if (!error && status)
        DbgPrint("msi: the cabinet %ws does not open, status %d\n", cabinet, status);
    if (!error)
        error = status == MIME_CAB_MEMORY ? ERROR_NOT_ENOUGH_MEMORY : status ? ERROR_INSTALL_PACKAGE_INVALID : ERROR_SUCCESS;
    if (!error) {
        EXTRACT_CONTEXT context = {package, package->Files, cabinet, ERROR_SUCCESS};
        static const MIME_CAB_FILE_SINK sink = {extract_begin, extract_write, extract_end};
        status = MimeCabExtract(cab, &sink, &context);
        if (status || context.Error)
            DbgPrint("msi: the cabinet %ws does not extract, status %d, error %u\n", cabinet, status, context.Error);
        error = context.Error ? context.Error : status == MIME_CAB_MEMORY ? ERROR_NOT_ENOUGH_MEMORY :
                status ? ERROR_INSTALL_PACKAGE_INVALID : ERROR_SUCCESS;
        for (MIME_MSI_FILE_PLAN *plan = package->Files; !error && plan; plan = plan->Next) {
            if (plan->Cabinet && !wcscmp(plan->Cabinet, cabinet) && !plan->Done) {
                DbgPrint("msi: the cabinet %ws lacks the file %ws\n", cabinet, plan->Key);
                error = ERROR_INSTALL_PACKAGE_INVALID;
            }
        }
    }
    MimeCabClose(cab);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    MimeMsiFree(stream);
    return error;
}

UINT MimeMsiCopyInstalledFile(MIME_MSI_PACKAGE *package, PCWSTR source, PCWSTR target)
{
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(source, GetFileExInfoStandard, &info))
        return GetLastError();
    if (info.nFileSizeHigh || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_INSTALL_PACKAGE_INVALID;
    MIME_MSI_FILE_PLAN file = {};
    file.Source = (PWSTR)source;
    file.Target = (PWSTR)target;
    file.Size = info.nFileSizeLow;
    return copy_planned_file(package, &file);
}

UINT MimeMsiWriteInstalledFile(MIME_MSI_PACKAGE *package, const void *data, SIZE_T size, PCWSTR target)
{
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    if (!data || size > UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    MIME_MSI_FILE_PLAN file = {.Target = (PWSTR)target, .Size = (DWORD)size, .Data = data};
    return copy_planned_file(package, &file);
}

static UINT execute_files(MIME_MSI_PACKAGE *package)
{
    if (!package->FilesQueued && !package->Queued)
        return ERROR_SUCCESS;
    UINT error = ERROR_SUCCESS;
    if (package->FilesQueued) {
        /* a cabinet or a source file nobody needs is not read, the letters of the mode may keep every file in it. */
        for (MIME_MSI_FILE_PLAN *file = package->Files; file && !error; file = file->Next) {
            BOOL keep;
            if (!file->ParentTarget)
                error = keeps_existing(file, &keep);
        }
        /* each cabinet is read once, in the order the plan first names it. */
        for (MIME_MSI_FILE_PLAN *file = package->Files; file && !error; file = file->Next) {
            if (!file->Cabinet || file->Done)
                continue;
            BOOL needed = FALSE;
            for (MIME_MSI_FILE_PLAN *other = file; other; other = other->Next)
                needed |= other->Cabinet && !wcscmp(other->Cabinet, file->Cabinet) && !other->Keep;
            if (!needed) {
                for (MIME_MSI_FILE_PLAN *other = file; other; other = other->Next)
                    if (other->Cabinet && !wcscmp(other->Cabinet, file->Cabinet))
                        other->Done = TRUE;
                continue;
            }
            error = extract_cabinet(package, file->Cabinet);
        }
        for (MIME_MSI_FILE_PLAN *file = package->Files; file && !error; file = file->Next)
            if (!file->Cabinet)
                error = copy_planned_file(package, file);
        free_plan(package);
    }
    while (!error && package->Queued) {
        MIME_MSI_QUEUED_ACTION *action = package->Queued;
        package->Queued = action->Next;
        error = action->Run(package, action->Argument);
        MimeMsiFree(action);
    }
    if (error) {
        UINT rollback = MimeMsiFinishTransaction(package, FALSE);
        if (rollback)
            DbgPrint("msi: rollback failed with error %u after installation error %u\n", rollback, error);
    }
    return error;
}

/* an action that reads or loads what the package installs needs the files in place, which they are at InstallFiles. */
UINT MimeMsiInstallQueuedFiles(MIME_MSI_PACKAGE *package)
{
    return execute_files(package);
}

UINT MimeMsiStandardAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (!wcscmp(action, L"CostInitialize")) {
        if (package->TransactionActive)
            return ERROR_INSTALL_ALREADY_RUNNING;
        free_plan(package);
        package->CostStage = 1;
        UINT error = MimeMsiLoadFeatures(package);
        if (error)
            return error;
        if (!*MimeMsiGetProperty(package, L"INSTALLLEVEL")) {
            UINT error = MimeMsiSetProperty(package, L"INSTALLLEVEL", L"1");
            if (error)
                return error;
        }
        return MimeMsiSetProperty(package, L"CostingComplete", L"0");
    }
    if (!wcscmp(action, L"FileCost")) {
        if (package->CostStage != 1)
            return ERROR_FUNCTION_NOT_CALLED;
        free_plan(package);
        UINT error = plan_files(package);
        if (error)
            free_plan(package);
        else
            package->CostStage = 2;
        return error;
    }
    if (!wcscmp(action, L"CostFinalize")) {
        if (package->CostStage != 2)
            return ERROR_FUNCTION_NOT_CALLED;
        BOOL changed;
        UINT error = MimeMsiProcessConditions(package, &changed);
        /* the levels decide which files are installed. */
        if (!error && changed)
            error = MimeMsiRefreshFilePlan(package);
        if (!error)
            error = validate_cost(package);
        if (!error)
            error = MimeMsiPublishDirectories(package);
        if (!error)
            error = MimeMsiSnapshotComponents(package);
        if (!error)
            error = MimeMsiSetProperty(package, L"CostingComplete", L"1");
        if (!error)
            package->CostStage = 3;
        if (!error)
            package->FeatureSelectionReady = TRUE;
        return error;
    }
    if (!wcscmp(action, L"InstallValidate"))
        return package->CostStage == 3 ? validate_cost(package) : ERROR_FUNCTION_NOT_CALLED;
    BOOL initialize = !wcscmp(action, L"InstallInitialize");
    BOOL files = !wcscmp(action, L"InstallFiles");
    BOOL execute = !wcscmp(action, L"InstallExecute");
    BOOL finalize = !wcscmp(action, L"InstallFinalize");
    if (!initialize && !files && !execute && !finalize)
        return MimeMsiRegistrationAction(package, action);
    if (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE)
        return ERROR_FUNCTION_NOT_CALLED;
    if (initialize) {
        if (package->TransactionActive)
            return ERROR_INSTALL_ALREADY_RUNNING;
        if (package->CostStage != 3)
            return ERROR_FUNCTION_NOT_CALLED;
        MimeMsiClearScript(package);
        package->TransactionActive = TRUE;
        package->TransactionCommitted = FALSE;
        return ERROR_SUCCESS;
    }
    if (!package->TransactionActive)
        return ERROR_FUNCTION_NOT_CALLED;
    if (files) {
        if (package->FilesQueued)
            return ERROR_INSTALL_ALREADY_RUNNING;
        free_plan(package);
        UINT error = plan_files(package);
        if (error)
            free_plan(package);
        else
            package->FilesQueued = TRUE;
        return error;
    }
    UINT error = execute_files(package);
    /* deferred actions run now, without the lock, and the transaction ends after them. */
    if (!error && (package->Deferred || (finalize && package->Commit)))
        return MIME_MSI_SCRIPT_PENDING;
    if (!error && finalize) {
        error = MimeMsiFinishTransaction(package, TRUE);
        MimeMsiClearScript(package);
    }
    return error;
}
