#include "msi_private.h"

static ULONG find_feature(MIME_MSI_PACKAGE *package, PCWSTR name)
{
    for (ULONG i = 0; i < package->FeatureCount; i++)
        if (!wcscmp(package->Features[i].Name, name))
            return i;
    return UINT32_MAX;
}

UINT MimeMsiLoadFeatures(MIME_MSI_PACKAGE *package)
{
    if (package->Features && package->FeatureRevision == package->Database->Revision)
        return ERROR_SUCCESS;
    if (package->TransactionActive)
        return ERROR_FUNCTION_NOT_CALLED;
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Feature", &table);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(table, L"Feature", TRUE);
    ULONG parent = MimeMsiColumn(table, L"Feature_Parent", TRUE);
    if (id == UINT32_MAX || parent == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    SIZE_T bytes = (SIZE_T)table->Rows * sizeof(MIME_MSI_FEATURE);
    if (bytes / sizeof(MIME_MSI_FEATURE) != table->Rows)
        return ERROR_NOT_ENOUGH_MEMORY;
    MIME_MSI_FEATURE *features = MimeMsiAllocate(bytes ? bytes : 1);
    if (!features)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, id);
        PCWSTR ancestor = MimeMsiText(package->Database, table, row, parent);
        if (!name || !*name || wcslen(name) > 38 || (ancestor && wcslen(ancestor) > 38)) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        wcscpy(features[row].Name, name);
        if (ancestor)
            wcscpy(features[row].Parent, ancestor);
        features[row].Requested = INSTALLSTATE_UNKNOWN;
        INSTALLSTATE installed = INSTALLSTATE_UNKNOWN;
        if (!(package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE) &&
            *MimeMsiGetProperty(package, L"ProductCode"))
            installed = MsiQueryFeatureStateW(MimeMsiGetProperty(package, L"ProductCode"), name);
        features[row].Installed = installed == INSTALLSTATE_UNKNOWN ? INSTALLSTATE_ABSENT : installed;
        for (ULONG prior = 0; prior < row; prior++)
            if (!wcscmp(features[prior].Name, name))
                error = ERROR_INSTALL_PACKAGE_INVALID;
    }
    if (error)
        MimeMsiFree(features);
    else {
        MimeMsiFree(package->Features);
        package->Features = features;
        package->FeatureCount = table->Rows;
        package->FeatureRevision = package->Database->Revision;
    }
    return error;
}

UINT MimeMsiFeatureState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action)
{
    UINT error = MimeMsiLoadFeatures(package);
    if (error)
        return error;
    ULONG index = find_feature(package, name);
    if (index == UINT32_MAX)
        return ERROR_UNKNOWN_FEATURE;
    *installed = package->Features[index].Installed;
    *action = INSTALLSTATE_UNKNOWN;
    if (package->FeatureSelectionReady) {
        MIME_MSI_TABLE *table;
        error = MimeMsiLoadTable(package->Database, L"Feature", &table);
        BOOL selected;
        if (!error)
            error = MimeMsiFeatureSelected(package, table, name, 0, &selected);
        if (!error)
            *action = selected ? INSTALLSTATE_LOCAL : INSTALLSTATE_ABSENT;
    }
    return error;
}

UINT WINAPI MsiGetFeatureStateW(MSIHANDLE handle, LPCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action)
{
    if (!name || !*name || !installed || !action)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = package ? MimeMsiFeatureState(package, name, installed, action) : ERROR_INVALID_HANDLE;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

static UINT feature_descendant(MIME_MSI_PACKAGE *package, ULONG child, ULONG ancestor, BOOL *out)
{
    *out = FALSE;
    for (ULONG depth = 0; depth <= package->FeatureCount; depth++) {
        if (child == ancestor) {
            *out = TRUE;
            return ERROR_SUCCESS;
        }
        PCWSTR parent = package->Features[child].Parent;
        if (!*parent)
            return ERROR_SUCCESS;
        child = find_feature(package, parent);
        if (child == UINT32_MAX)
            return ERROR_INSTALL_PACKAGE_INVALID;
    }
    return ERROR_BAD_CONFIGURATION;
}

UINT WINAPI MsiSetFeatureStateW(MSIHANDLE handle, LPCWSTR name, INSTALLSTATE state)
{
    if (!name || !*name || state < INSTALLSTATE_ADVERTISED || state > INSTALLSTATE_SOURCE)
        return ERROR_INVALID_PARAMETER;
    if (state != INSTALLSTATE_LOCAL && state != INSTALLSTATE_ABSENT)
        return ERROR_CALL_NOT_IMPLEMENTED;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->TransactionActive ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    ULONG index = error ? UINT32_MAX : find_feature(package, name);
    if (!error && index == UINT32_MAX)
        error = ERROR_UNKNOWN_FEATURE;
    MIME_MSI_FEATURE *replacement = nullptr;
    if (!error) {
        SIZE_T bytes = (SIZE_T)package->FeatureCount * sizeof(*replacement);
        replacement = MimeMsiAllocate(bytes ? bytes : 1);
        if (!replacement)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else
            memcpy(replacement, package->Features, bytes);
    }
    if (!error) {
        for (ULONG i = 0; i < package->FeatureCount && !error; i++) {
            BOOL child;
            error = feature_descendant(package, i, index, &child);
            if (!error && child)
                replacement[i].Requested = state;
            if (!error && !child && state == INSTALLSTATE_LOCAL) {
                error = feature_descendant(package, index, i, &child);
                if (!error && child)
                    replacement[i].Requested = INSTALLSTATE_LOCAL;
            }
        }
    }
    if (!error) {
        MIME_MSI_FEATURE *previous = package->Features;
        package->Features = replacement;
        error = MimeMsiRefreshFilePlan(package);
        if (error) {
            package->Features = previous;
            if (MimeMsiRefreshFilePlan(package)) {
                package->CostStage = 1;
                MimeMsiSetProperty(package, L"CostingComplete", L"0");
            }
        } else {
            package->FeatureSelectionReady = TRUE;
            replacement = previous;
        }
    }
    MimeMsiFree(replacement);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiSetInstallLevel(MSIHANDLE handle, int level)
{
    if (level > 32767)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->TransactionActive ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    PWSTR previous_level = nullptr;
    INSTALLSTATE *previous_states = nullptr;
    BOOL previous_defaults = package ? package->ConfigureDefaults : FALSE;
    if (!error) {
        previous_level = MimeMsiDuplicate(MimeMsiGetProperty(package, L"INSTALLLEVEL"));
        previous_states = MimeMsiAllocate(((SIZE_T)package->FeatureCount + 1) * sizeof(*previous_states));
        if (!previous_level || !previous_states)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error && level > 0) {
        WCHAR text[16];
        _snwprintf(text, RTL_NUMBER_OF(text), L"%d", level);
        error = MimeMsiSetProperty(package, L"INSTALLLEVEL", text);
    }
    if (!error) {
        if (package->Maintenance)
            package->ConfigureDefaults = TRUE;
        for (ULONG i = 0; i < package->FeatureCount; i++) {
            previous_states[i] = package->Features[i].Requested;
            package->Features[i].Requested = INSTALLSTATE_UNKNOWN;
        }
        error = MimeMsiRefreshFilePlan(package);
        if (error) {
            package->ConfigureDefaults = previous_defaults;
            MimeMsiSetProperty(package, L"INSTALLLEVEL", previous_level);
            for (ULONG i = 0; i < package->FeatureCount; i++)
                package->Features[i].Requested = previous_states[i];
            if (MimeMsiRefreshFilePlan(package)) {
                package->CostStage = 1;
                MimeMsiSetProperty(package, L"CostingComplete", L"0");
            }
        } else
            package->FeatureSelectionReady = TRUE;
    }
    MimeMsiFree(previous_states);
    MimeMsiFree(previous_level);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

static UINT feature_text(PCWSTR text, PWSTR out, DWORD *size)
{
    if (out)
        return MimeMsiCopyString(text ? text : L"", out, size);
    if (size)
        *size = text ? (DWORD)wcslen(text) : 0;
    return ERROR_SUCCESS;
}

static UINT feature_cost(MIME_MSI_PACKAGE *package, ULONG feature, MSICOSTTREE tree, ULONGLONG *cost)
{
    MIME_MSI_TABLE *files, *links, *components;
    UINT error = MimeMsiLoadTable(package->Database, L"File", &files);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"FeatureComponents", &links);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"Component", &components);
    if (error)
        return error;
    ULONG component = MimeMsiColumn(files, L"Component_", TRUE);
    ULONG bytes = MimeMsiColumn(files, L"FileSize", FALSE);
    ULONG attributes = MimeMsiColumn(files, L"Attributes", FALSE);
    ULONG version = MimeMsiColumn(files, L"Version", TRUE);
    ULONG link_feature = MimeMsiColumn(links, L"Feature_", TRUE);
    ULONG link_component = MimeMsiColumn(links, L"Component_", TRUE);
    ULONG component_id = MimeMsiColumn(components, L"Component", TRUE);
    ULONG component_directory = MimeMsiColumn(components, L"Directory_", TRUE);
    ULONG component_flags = MimeMsiColumn(components, L"Attributes", FALSE);
    if (component == UINT32_MAX || bytes == UINT32_MAX || attributes == UINT32_MAX || version == UINT32_MAX ||
        link_feature == UINT32_MAX || link_component == UINT32_MAX || component_id == UINT32_MAX ||
        component_directory == UINT32_MAX || component_flags == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    *cost = 0;
    for (ULONG row = 0; row < files->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, files, row, component);
        if (!name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        BOOL included = FALSE;
        for (ULONG link = 0; link < links->Rows && !included; link++) {
            PCWSTR owner = MimeMsiText(package->Database, links, link, link_component);
            PCWSTR title = MimeMsiText(package->Database, links, link, link_feature);
            if (!owner || !title)
                return ERROR_INSTALL_PACKAGE_INVALID;
            if (wcscmp(owner, name))
                continue;
            ULONG candidate = find_feature(package, title);
            if (candidate == UINT32_MAX)
                return ERROR_INSTALL_PACKAGE_INVALID;
            if (tree == MSICOSTTREE_SELFONLY)
                included = candidate == feature;
            else {
                error = feature_descendant(package, tree == MSICOSTTREE_CHILDREN ? candidate : feature,
                                            tree == MSICOSTTREE_CHILDREN ? feature : candidate, &included);
                if (error)
                    return error;
            }
        }
        if (!included)
            continue;
        int size = MimeMsiInteger(files, row, bytes);
        PCWSTR file_version = MimeMsiText(package->Database, files, row, version);
        if (size < 0)
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (MimeMsiInteger(files, row, attributes) != 0x4000 || (file_version && *file_version))
            return ERROR_CALL_NOT_IMPLEMENTED;
        PCWSTR directory = nullptr;
        for (ULONG entry = 0; entry < components->Rows; entry++) {
            PCWSTR key = MimeMsiText(package->Database, components, entry, component_id);
            if (!key || wcscmp(key, name))
                continue;
            if (MimeMsiInteger(components, entry, component_flags))
                return ERROR_CALL_NOT_IMPLEMENTED;
            directory = MimeMsiText(package->Database, components, entry, component_directory);
            break;
        }
        if (!directory)
            return ERROR_INSTALL_PACKAGE_INVALID;
        PWSTR target = nullptr;
        error = MimeMsiDirectoryPath(package, directory, FALSE, &target);
        if (error)
            return error;
        if (wcslen(target) < 3 || target[1] != L':') {
            MimeMsiFree(target);
            return ERROR_CALL_NOT_IMPLEMENTED;
        }
        WCHAR root[] = {target[0], L':', L'\\', 0};
        MimeMsiFree(target);
        DWORD sectors, sector_bytes, available, total;
        if (!GetDiskFreeSpaceW(root, &sectors, &sector_bytes, &available, &total))
            return GetLastError();
        if (!sectors || !sector_bytes || sectors > UINT32_MAX / sector_bytes)
            return ERROR_INVALID_DATA;
        ULONGLONG cluster = (ULONGLONG)sectors * sector_bytes;
        *cost += (((ULONGLONG)size + cluster - 1) / cluster * cluster + 511) / 512;
        if (*cost > INT32_MAX)
            return ERROR_ARITHMETIC_OVERFLOW;
    }
    return ERROR_SUCCESS;
}

UINT WINAPI MsiGetFeatureCostW(MSIHANDLE handle, LPCWSTR name, MSICOSTTREE tree, INSTALLSTATE state, LPINT cost)
{
    if (!name || !*name || !cost || tree < MSICOSTTREE_SELFONLY || tree > MSICOSTTREE_PARENTS ||
        (state != INSTALLSTATE_UNKNOWN && state != INSTALLSTATE_ABSENT && state != INSTALLSTATE_LOCAL &&
         state != INSTALLSTATE_SOURCE && state != INSTALLSTATE_DEFAULT))
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->CostStage != 3 ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    ULONG index = error ? UINT32_MAX : find_feature(package, name);
    if (!error && index == UINT32_MAX)
        error = ERROR_UNKNOWN_FEATURE;
    if (!error && *MimeMsiGetProperty(package, L"Installed"))
        error = ERROR_CALL_NOT_IMPLEMENTED;
    if (!error && (state == INSTALLSTATE_UNKNOWN || state == INSTALLSTATE_DEFAULT)) {
        INSTALLSTATE installed;
        error = MimeMsiFeatureState(package, name, &installed, &state);
    }
    ULONGLONG value = 0;
    if (!error && state == INSTALLSTATE_LOCAL)
        error = feature_cost(package, index, tree, &value);
    else if (!error && state != INSTALLSTATE_ABSENT)
        error = ERROR_CALL_NOT_IMPLEMENTED;
    if (!error)
        *cost = (int)value;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiGetFeatureInfoW(MSIHANDLE handle, LPCWSTR name, LPDWORD attributes,
                               LPWSTR title, LPDWORD title_size, LPWSTR help, LPDWORD help_size)
{
    if (!name || !*name || (title && !title_size) || (help && !help_size))
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    MIME_MSI_TABLE *table;
    UINT error = package ? MimeMsiLoadTable(package->Database, L"Feature", &table) : ERROR_INVALID_HANDLE;
    if (error == ERROR_BAD_QUERY_SYNTAX)
        error = ERROR_UNKNOWN_FEATURE;
    if (!error) {
        ULONG id = MimeMsiColumn(table, L"Feature", TRUE);
        ULONG flags = MimeMsiColumn(table, L"Attributes", FALSE);
        ULONG caption = MimeMsiColumn(table, L"Title", TRUE);
        ULONG description = MimeMsiColumn(table, L"Description", TRUE);
        if (id == UINT32_MAX || flags == UINT32_MAX || caption == UINT32_MAX || description == UINT32_MAX)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else {
            error = ERROR_UNKNOWN_FEATURE;
            for (ULONG row = 0; row < table->Rows; row++) {
                PCWSTR key = MimeMsiText(package->Database, table, row, id);
                if (!key || wcscmp(key, name))
                    continue;
                int value = MimeMsiInteger(table, row, flags);
                if (value < 0 || (value & ~63)) {
                    error = ERROR_INSTALL_PACKAGE_INVALID;
                    break;
                }
                if (attributes)
                    *attributes = ((DWORD)(value & 15) << 1) | (value & 32) |
                                  ((value & 3) ? 0 : INSTALLFEATUREATTRIBUTE_FAVORLOCAL);
                error = feature_text(MimeMsiText(package->Database, table, row, caption), title, title_size);
                UINT other = feature_text(MimeMsiText(package->Database, table, row, description), help, help_size);
                if (!error)
                    error = other;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
