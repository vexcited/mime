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
    MIME_REMOTE(PROXY_GetFeatureStateW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(installed), MIME_ARG(action));
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
    MIME_REMOTE(PROXY_SetFeatureStateW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(state));
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
        MimeMsiClearComponentRequests(package);
        MimeMsiPlanChanged(package);
        package->FeatureSelectionReady = TRUE;
        replacement = previous;
    }
    MimeMsiFree(replacement);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiSetInstallLevel(MSIHANDLE handle, int level)
{
    MIME_REMOTE(PROXY_SetInstallLevel, UINT, MIME_ARG(handle), MIME_ARG(level));
    if (level > 32767)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->TransactionActive ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    if (!error && level > 0) {
        WCHAR text[16];
        _snwprintf(text, RTL_NUMBER_OF(text), L"%d", level);
        error = MimeMsiSetProperty(package, L"INSTALLLEVEL", text);
    }
    if (!error) {
        if (package->Maintenance)
            package->ConfigureDefaults = TRUE;
        MimeMsiClearComponentRequests(package);
        for (ULONG i = 0; i < package->FeatureCount; i++)
            package->Features[i].Requested = INSTALLSTATE_UNKNOWN;
        MimeMsiPlanChanged(package);
        package->FeatureSelectionReady = TRUE;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiSetComponentStateW(MSIHANDLE handle, LPCWSTR name, INSTALLSTATE state)
{
    MIME_REMOTE(PROXY_SetComponentStateW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(state));
    if (!name || !*name || (state != INSTALLSTATE_ABSENT && state != INSTALLSTATE_LOCAL && state != INSTALLSTATE_SOURCE))
        return ERROR_INVALID_PARAMETER;
    if (state == INSTALLSTATE_SOURCE)
        return ERROR_CALL_NOT_IMPLEMENTED;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    INSTALLSTATE installed, action;
    UINT error = !package ? ERROR_INVALID_HANDLE : package->TransactionActive ? ERROR_FUNCTION_NOT_CALLED :
                 MimeMsiComponentState(package, name, &installed, &action);
    ULONG index = 0;
    if (!error)
        while (index < package->ComponentRequestCount && wcscmp(package->ComponentRequests[index].Name, name))
            index++;
    if (!error && index == package->ComponentRequestCount) {
        ULONG count = package->ComponentRequestCount;
        MIME_MSI_COMPONENT_REQUEST *requests = MimeMsiAllocate(((SIZE_T)count + 1) * sizeof(*requests));
        PWSTR copy = MimeMsiDuplicate(name);
        if (!requests || !copy) {
            MimeMsiFree(requests);
            MimeMsiFree(copy);
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            if (count)
                memcpy(requests, package->ComponentRequests, (SIZE_T)count * sizeof(*requests));
            MimeMsiFree(package->ComponentRequests);
            package->ComponentRequests = requests;
            package->ComponentRequestCount = count + 1;
            requests[count].Name = copy;
            requests[count].State = INSTALLSTATE_UNKNOWN;
        }
    }
    if (!error) {
        package->ComponentRequests[index].State = state;
        MimeMsiPlanChanged(package);
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

/* the attributes are those of the INSTALLFEATUREATTRIBUTE flags, which the Feature table holds shifted. */
UINT WINAPI MsiSetFeatureAttributesW(MSIHANDLE handle, LPCWSTR name, DWORD attributes)
{
    MIME_REMOTE(PROXY_SetFeatureAttributesW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(attributes));
    if (!name || !*name)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->TransactionActive ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    ULONG index = error ? UINT32_MAX : find_feature(package, name);
    if (!error && index == UINT32_MAX)
        error = ERROR_UNKNOWN_FEATURE;
    if (!error) {
        MIME_MSI_FEATURE *feature = &package->Features[index];
        feature->AttributesSet = TRUE;
        feature->Attributes = (int)((attributes & 32) | ((attributes >> 1) & 15));
        MimeMsiPlanChanged(package);
    }
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

/* the cost of the files of a feature and the features around it, or of one component when it is named. */
static UINT feature_cost(MIME_MSI_PACKAGE *package, ULONG feature, MSICOSTTREE tree, PCWSTR only, ULONGLONG *cost)
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
        if (only) {
            if (wcscmp(only, name))
                continue;
            included = TRUE;
        }
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
    MIME_REMOTE(PROXY_GetFeatureCostW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(tree), MIME_ARG(state), MIME_ARG(cost));
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
        error = feature_cost(package, index, tree, nullptr, &value);
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
                if (package->Features && package->FeatureRevision == package->Database->Revision) {
                    ULONG index = find_feature(package, name);
                    if (index != UINT32_MAX && package->Features[index].AttributesSet)
                        value = package->Features[index].Attributes;
                }
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

/* the states a feature may be given, by its components and its attributes. */
UINT WINAPI MsiGetFeatureValidStatesW(MSIHANDLE handle, LPCWSTR name, LPDWORD states)
{
    MIME_REMOTE(PROXY_GetFeatureValidStatesW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(states));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : !name || !states ? ERROR_INVALID_PARAMETER :
                 package->TransactionActive ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    ULONG index = error ? UINT32_MAX : find_feature(package, name);
    if (!error && index == UINT32_MAX)
        error = ERROR_UNKNOWN_FEATURE;
    MIME_MSI_TABLE *features = nullptr, *links = nullptr, *components = nullptr;
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"FeatureComponents", &links);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"Component", &components);
    if (!error) {
        ULONG id = MimeMsiColumn(features, L"Feature", TRUE);
        ULONG flags = MimeMsiColumn(features, L"Attributes", FALSE);
        ULONG link_feature = MimeMsiColumn(links, L"Feature_", TRUE);
        ULONG link_component = MimeMsiColumn(links, L"Component_", TRUE);
        ULONG component_id = MimeMsiColumn(components, L"Component", TRUE);
        ULONG component_flags = MimeMsiColumn(components, L"Attributes", FALSE);
        if (id == UINT32_MAX || flags == UINT32_MAX || link_feature == UINT32_MAX || link_component == UINT32_MAX ||
            component_id == UINT32_MAX || component_flags == UINT32_MAX)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else {
            int attributes = 0;
            for (ULONG row = 0; row < features->Rows; row++) {
                PCWSTR key = MimeMsiText(package->Database, features, row, id);
                if (key && !wcscmp(key, name))
                    attributes = MimeMsiInteger(features, row, flags);
            }
            if (package->Features[index].AttributesSet)
                attributes = package->Features[index].Attributes;
            if (attributes == MSI_NULL_INTEGER)
                attributes = 0;
            BOOL linked = FALSE, local = FALSE, source = FALSE;
            for (ULONG row = 0; row < links->Rows; row++) {
                PCWSTR owner = MimeMsiText(package->Database, links, row, link_feature);
                PCWSTR component = MimeMsiText(package->Database, links, row, link_component);
                if (!owner || !component || wcscmp(owner, name))
                    continue;
                linked = TRUE;
                for (ULONG entry = 0; entry < components->Rows; entry++) {
                    PCWSTR key = MimeMsiText(package->Database, components, entry, component_id);
                    if (!key || wcscmp(key, component))
                        continue;
                    int kind = MimeMsiInteger(components, entry, component_flags) & 3;
                    local |= kind == 0 || kind == 2;
                    source |= kind == 1 || kind == 2;
                }
            }
            if (!linked)
                local = source = TRUE;
            DWORD valid = 0;
            if (local)
                valid |= 1u << INSTALLSTATE_LOCAL;
            if (source)
                valid |= 1u << INSTALLSTATE_SOURCE;
            if (!(attributes & 8))
                valid |= 1u << INSTALLSTATE_ADVERTISED;
            if (!(attributes & 16))
                valid |= 1u << INSTALLSTATE_ABSENT;
            *states = valid;
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

/* the cost of a component lands on the volume of its directory, and the installer here uses no space for backups. */
UINT WINAPI MsiEnumComponentCostsW(MSIHANDLE handle, LPCWSTR name, DWORD index, INSTALLSTATE state, LPWSTR drive,
                                   LPDWORD drive_size, LPINT cost, LPINT temporary)
{
    MIME_REMOTE(PROXY_EnumComponentCostsW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(index), MIME_ARG(state),
                MIME_ARG(drive), MIME_ARG(drive_size), MIME_ARG(cost), MIME_ARG(temporary));
    if (!drive || !drive_size || !cost || !temporary)
        return ERROR_INVALID_PARAMETER;
    if (!name || !*name || (state != INSTALLSTATE_UNKNOWN && state != INSTALLSTATE_ABSENT && state != INSTALLSTATE_LOCAL &&
                            state != INSTALLSTATE_SOURCE && state != INSTALLSTATE_DEFAULT))
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->CostStage != 3 ? ERROR_FUNCTION_NOT_CALLED : MimeMsiLoadFeatures(package);
    INSTALLSTATE installed, action;
    if (!error)
        error = MimeMsiComponentState(package, name, &installed, &action);
    if (!error && *MimeMsiGetProperty(package, L"Installed"))
        error = ERROR_CALL_NOT_IMPLEMENTED;
    if (!error && state == INSTALLSTATE_SOURCE)
        error = ERROR_CALL_NOT_IMPLEMENTED;
    if (!error && (state == INSTALLSTATE_UNKNOWN || state == INSTALLSTATE_DEFAULT))
        state = action == INSTALLSTATE_LOCAL ? INSTALLSTATE_LOCAL : INSTALLSTATE_ABSENT;
    if (!error && index)
        error = ERROR_NO_MORE_ITEMS;
    PWSTR directory = nullptr;
    if (!error)
        error = MimeMsiComponentDirectory(package, name, &directory);
    if (!error && (wcslen(directory) < 3 || directory[1] != L':'))
        error = ERROR_CALL_NOT_IMPLEMENTED;
    ULONGLONG value = 0;
    if (!error && state == INSTALLSTATE_LOCAL)
        error = feature_cost(package, 0, MSICOSTTREE_SELFONLY, name, &value);
    if (!error) {
        WCHAR text[3] = {directory[0], L':', 0};
        error = MimeMsiCopyString(text, drive, drive_size);
        if (!error || error == ERROR_MORE_DATA) {
            *cost = (int)value;
            *temporary = 0;
        }
    }
    MimeMsiFree(directory);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
