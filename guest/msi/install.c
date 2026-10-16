#include "msi_private.h"

typedef struct MIME_MSI_FILE_PLAN {
    struct MIME_MSI_FILE_PLAN *Next;
    PWSTR Source, Target;
    DWORD Size, Attributes;
    const BYTE *Data;
    BOOL MissingOnly;
} MIME_MSI_FILE_PLAN;

typedef struct MIME_MSI_CHANGE {
    struct MIME_MSI_CHANGE *Next;
    PWSTR Path, Backup;
    BOOL Directory;
    ULONG RegistryKind;
    HKEY Root;
    PWSTR ValueName;
    BYTE *OldData;
    DWORD OldType, OldBytes;
    BOOL HadValue;
} MIME_MSI_CHANGE;

static void free_plan(MIME_MSI_PACKAGE *package)
{
    while (package->Files) {
        MIME_MSI_FILE_PLAN *file = package->Files;
        package->Files = file->Next;
        MimeMsiFree(file->Source);
        MimeMsiFree(file->Target);
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
        UINT error = RegDeleteKeyW(change->Root, change->Path);
        return absent(error) ? ERROR_SUCCESS : error;
    }
    HKEY key;
    UINT error = RegOpenKeyExW(change->Root, change->Path, 0, KEY_SET_VALUE, &key);
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
    value->Path = MimeMsiDuplicate(path);
    value->ValueName = MimeMsiDuplicate(name);
    UINT error = !created->Path || !value->Path || !value->ValueName ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    HKEY key = nullptr;
    DWORD disposition;
    if (!error)
        error = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition);
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
        if (!commit) {
            if (change->RegistryKind) {
                UINT status = restore_registry(change);
                restored = !status;
                if (status && !error)
                    error = status;
            } else if (change->Directory)
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
    ULONG row;
    for (row = 0; row < features->Rows; row++) {
        PCWSTR key = MimeMsiText(package->Database, features, row, id);
        if (key && !wcscmp(name, key))
            break;
    }
    if (row == features->Rows)
        return ERROR_INSTALL_PACKAGE_INVALID;
    int level = MimeMsiInteger(features, row, level_column);
    if (level < 0)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PCWSTR parent = MimeMsiText(package->Database, features, row, parent_column);
    if (parent && *parent) {
        UINT error = MimeMsiFeatureSelected(package, features, parent, depth + 1, selected);
        if (error || !*selected)
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
    *selected = level && level <= limit;
    if (package->Maintenance && !package->ConfigureDefaults)
        for (ULONG i = 0; i < package->FeatureCount; i++)
            if (!wcscmp(package->Features[i].Name, name))
                *selected = package->Features[i].Installed == INSTALLSTATE_LOCAL;
    PCWSTR local = MimeMsiGetProperty(package, L"ADDLOCAL");
    if (*local)
        *selected = level && list_contains(local, name);
    if (list_contains(MimeMsiGetProperty(package, L"REMOVE"), name))
        *selected = FALSE;
    if (list_contains(MimeMsiGetProperty(package, L"REINSTALL"), name))
        for (ULONG i = 0; i < package->FeatureCount; i++)
            if (!wcscmp(package->Features[i].Name, name) && package->Features[i].Installed == INSTALLSTATE_LOCAL)
                *selected = TRUE;
    for (ULONG i = 0; i < package->FeatureCount; i++) {
        MIME_MSI_FEATURE *feature = &package->Features[i];
        if (!wcscmp(feature->Name, name) && feature->Requested != INSTALLSTATE_UNKNOWN)
            *selected = level && feature->Requested == INSTALLSTATE_LOCAL;
    }
    int attributes = MimeMsiInteger(features, row, attributes_column);
    if (*selected && attributes)
        return ERROR_CALL_NOT_IMPLEMENTED;
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
    if (*selected && MimeMsiInteger(components, row, attributes))
        return ERROR_CALL_NOT_IMPLEMENTED;
    return ERROR_SUCCESS;
}

static UINT reinstall_mode(MIME_MSI_PACKAGE *package, DWORD *out)
{
    *out = 0;
    PCWSTR mode = MimeMsiGetProperty(package, L"REINSTALLMODE");
    if (!*mode && *MimeMsiGetProperty(package, L"REINSTALL"))
        mode = L"omus";
    for (; *mode; mode++) {
        WCHAR code = *mode >= L'A' && *mode <= L'Z' ? *mode + L'a' - L'A' : *mode;
        if (code == L'p')
            *out |= REINSTALLMODE_FILEMISSING;
        else if (code == L'a')
            *out |= REINSTALLMODE_FILEREPLACE;
        else if (code == L'u')
            *out |= REINSTALLMODE_USERDATA;
        else if (code == L'm')
            *out |= REINSTALLMODE_MACHINEDATA;
        else if (code == L's')
            *out |= REINSTALLMODE_SHORTCUT;
        else if (wcschr(L"oedcv", code))
            return ERROR_CALL_NOT_IMPLEMENTED;
        else
            return ERROR_INVALID_PARAMETER;
    }
    static const struct { PCWSTR Name; DWORD Modes; } tables[] = {
        {L"Registry", REINSTALLMODE_USERDATA | REINSTALLMODE_MACHINEDATA},
        {L"Class", REINSTALLMODE_MACHINEDATA}, {L"Verb", REINSTALLMODE_MACHINEDATA},
        {L"PublishComponent", REINSTALLMODE_MACHINEDATA}, {L"ProgId", REINSTALLMODE_MACHINEDATA},
        {L"MIME", REINSTALLMODE_MACHINEDATA}, {L"Icon", REINSTALLMODE_MACHINEDATA | REINSTALLMODE_SHORTCUT},
        {L"Extension", REINSTALLMODE_MACHINEDATA}, {L"AppId", REINSTALLMODE_MACHINEDATA},
        {L"TypeLib", REINSTALLMODE_MACHINEDATA}, {L"ODBCDriver", REINSTALLMODE_MACHINEDATA},
        {L"ODBCDataSource", REINSTALLMODE_MACHINEDATA}, {L"ODBCTranslator", REINSTALLMODE_MACHINEDATA},
        {L"Shortcut", REINSTALLMODE_SHORTCUT}
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(tables); i++) {
        if (!(*out & tables[i].Modes))
            continue;
        MIME_MSI_TABLE *table;
        UINT error = MimeMsiLoadTable(package->Database, tables[i].Name, &table);
        if (!error && table->Rows)
            return ERROR_CALL_NOT_IMPLEMENTED;
        if (error && error != ERROR_BAD_QUERY_SYNTAX)
            return error;
    }
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
        if (!error && !selected && package->Features[i].Installed == INSTALLSTATE_LOCAL)
            error = ERROR_CALL_NOT_IMPLEMENTED;
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

static UINT component_requested(MIME_MSI_PACKAGE *package, PCWSTR name, DWORD mode, BOOL *requested)
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
                ((mode & (REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEREPLACE)) &&
                 list_contains(MimeMsiGetProperty(package, L"REINSTALL"), linked)))
                *requested = TRUE;
        }
    }
    return ERROR_SUCCESS;
}

static UINT plan_files(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table, *conditions;
    UINT error = MimeMsiLoadTable(package->Database, L"Condition", &conditions);
    if (!error && conditions->Rows)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (error && error != ERROR_BAD_QUERY_SYNTAX)
        return error;
    if (*MimeMsiGetProperty(package, L"ADDSOURCE") || *MimeMsiGetProperty(package, L"ADVERTISE") ||
        (!package->Maintenance && (*MimeMsiGetProperty(package, L"REINSTALL") || *MimeMsiGetProperty(package, L"Installed"))))
        return ERROR_CALL_NOT_IMPLEMENTED;
    error = maintenance_selection(package);
    DWORD mode;
    if (!error)
        error = reinstall_mode(package, &mode);
    if (error)
        return error;
    error = MimeMsiLoadTable(package->Database, L"File", &table);
    if (error)
        return error;
    ULONG component = MimeMsiColumn(table, L"Component_", TRUE);
    ULONG name = MimeMsiColumn(table, L"FileName", TRUE);
    ULONG size = MimeMsiColumn(table, L"FileSize", FALSE);
    ULONG attributes = MimeMsiColumn(table, L"Attributes", FALSE);
    ULONG version = MimeMsiColumn(table, L"Version", TRUE);
    if (component == UINT32_MAX || name == UINT32_MAX || size == UINT32_MAX ||
        attributes == UINT32_MAX || version == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
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
        error = component_requested(package, component_name, mode, &selected);
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
        if (!(flags & 0x4000) || (flags & ~0x4000) || (file_version && *file_version))
            return ERROR_CALL_NOT_IMPLEMENTED;
        PCWSTR long_name = wcschr(filename, L'|');
        if (long_name)
            filename = long_name + 1;
        if (!*filename || !wcscmp(filename, L".") || !wcscmp(filename, L"..") ||
            wcschr(filename, L'\\') || wcschr(filename, L'/') || wcschr(filename, L':'))
            return ERROR_INSTALL_PACKAGE_INVALID;
        PWSTR source = nullptr, target = nullptr;
        error = MimeMsiDirectoryPath(package, directory, TRUE, &source);
        if (!error)
            error = MimeMsiDirectoryPath(package, directory, FALSE, &target);
        MIME_MSI_FILE_PLAN *file = !error ? MimeMsiAllocate(sizeof(*file)) : nullptr;
        if (!error && !file)
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (!error) {
            file->Source = MimeMsiJoinPath(source, filename);
            file->Target = MimeMsiJoinPath(target, filename);
            file->Size = (DWORD)file_size;
            file->Attributes = (DWORD)flags;
            file->MissingOnly = (mode & REINSTALLMODE_FILEMISSING) && !(mode & REINSTALLMODE_FILEREPLACE);
            if (!file->Source || !file->Target)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else if (!_wcsicmp(file->Source, file->Target))
                error = ERROR_INSTALL_PACKAGE_INVALID;
        }
        BOOL skip = FALSE;
        if (!error && (mode & REINSTALLMODE_FILEMISSING) && !(mode & REINSTALLMODE_FILEREPLACE)) {
            DWORD present = GetFileAttributesW(file->Target);
            if (present != INVALID_FILE_ATTRIBUTES) {
                if (present & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
                    error = ERROR_ACCESS_DENIED;
                else
                    skip = TRUE;
            } else if (!absent(GetLastError()))
                error = GetLastError();
        }
        if (file && (error || skip)) {
            MimeMsiFree(file->Source);
            MimeMsiFree(file->Target);
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
            if (!GetDiskFreeSpaceW(root, &sectors, &bytes, &available, &total))
                return GetLastError();
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
        } else if (!absent(GetLastError()))
            return GetLastError();
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

static UINT copy_planned_file(MIME_MSI_PACKAGE *package, MIME_MSI_FILE_PLAN *file)
{
    if (file->MissingOnly) {
        DWORD attributes = GetFileAttributesW(file->Target);
        if (attributes != INVALID_FILE_ATTRIBUTES)
            return attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
        if (!absent(GetLastError()))
            return GetLastError();
    }
    HANDLE source = INVALID_HANDLE_VALUE;
    UINT error = ERROR_SUCCESS;
    if (!file->Data) {
        source = CreateFileW(file->Source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (source == INVALID_HANDLE_VALUE)
            return GetLastError();
        LARGE_INTEGER size;
        error = !GetFileSizeEx(source, &size) ? GetLastError() :
                size.QuadPart != file->Size ? ERROR_INSTALL_PACKAGE_INVALID : ERROR_SUCCESS;
        CloseHandle(source);
        source = INVALID_HANDLE_VALUE;
    }
    if (error)
        return error;
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
    if (file->MissingOnly && attributes != INVALID_FILE_ATTRIBUTES)
        return attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return ERROR_ACCESS_DENIED;
    if (attributes == INVALID_FILE_ATTRIBUTES && !absent(GetLastError()))
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
                _snwprintf(change->Backup, capacity, L"%s.mime-%08x-%08x.bak", file->Target,
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
    BYTE *buffer = file->Data ? nullptr : MimeMsiAllocate(64 * 1024);
    if (buffer)
        source = CreateFileW(file->Source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (!file->Data && (!buffer || source == INVALID_HANDLE_VALUE)) {
        error = buffer ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        if (change->Backup)
            DeleteFileW(change->Backup);
        MimeMsiFree(buffer);
        MimeMsiFree(change->Backup);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    HANDLE output = CreateFileW(file->Target, GENERIC_WRITE, 0, nullptr,
                                attributes == INVALID_FILE_ATTRIBUTES ? CREATE_NEW : OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (file->MissingOnly && (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)) {
            DWORD present = GetFileAttributesW(file->Target);
            if (present != INVALID_FILE_ATTRIBUTES && !(present & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                error = ERROR_SUCCESS;
        }
        if (source != INVALID_HANDLE_VALUE)
            CloseHandle(source);
        if (change->Backup)
            DeleteFileW(change->Backup);
        MimeMsiFree(buffer);
        MimeMsiFree(change->Backup);
        MimeMsiFree(change->Path);
        MimeMsiFree(change);
        return error;
    }
    change->Next = package->Changes;
    package->Changes = change;
    if (!SetEndOfFile(output))
        error = GetLastError();
    DWORD remaining = file->Size;
    while (remaining && !error) {
        DWORD wanted = remaining < 64 * 1024 ? remaining : 64 * 1024, read = wanted, written;
        const BYTE *data = file->Data ? file->Data + file->Size - remaining : buffer;
        if (!file->Data && !ReadFile(source, buffer, wanted, &read, nullptr))
            error = GetLastError();
        else if (!read)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else if (!WriteFile(output, data, read, &written, nullptr))
            error = GetLastError();
        else if (written != read)
            error = ERROR_WRITE_FAULT;
        else
            remaining -= read;
    }
    FILETIME created, accessed, modified;
    if (!error && ((!file->Data && (!GetFileTime(source, &created, &accessed, &modified) ||
        !SetFileTime(output, &created, &accessed, &modified))) || !FlushFileBuffers(output)))
        error = GetLastError();
    CloseHandle(output);
    if (source != INVALID_HANDLE_VALUE)
        CloseHandle(source);
    MimeMsiFree(buffer);
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
    if (!package->FilesQueued)
        return ERROR_SUCCESS;
    UINT error = ERROR_SUCCESS;
    for (MIME_MSI_FILE_PLAN *file = package->Files; file && !error; file = file->Next)
        error = copy_planned_file(package, file);
    free_plan(package);
    if (error) {
        UINT rollback = MimeMsiFinishTransaction(package, FALSE);
        if (rollback)
            DbgPrint("msi: rollback failed with error %u after installation error %u\n", rollback, error);
    }
    return error;
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
        UINT error = validate_cost(package);
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
    if (!error && finalize)
        error = MimeMsiFinishTransaction(package, TRUE);
    return error;
}
