#include "msi_private.h"

/*
 * assemblies. a component with a row in MsiAssembly is an assembly. one whose File_Application is null is global: for a
 * .NET assembly its files go to the global assembly cache and not to the directory of the component. the others are private,
 * which stay in the directory of their application and are only published, so that the installer can find them again.
 * publishing writes the name of the assembly and a descriptor of its component under Installer\Assemblies.
 */

enum { TYPE_NET_GLOBAL = 1, TYPE_WIN32_GLOBAL, TYPE_NET_PRIVATE, TYPE_WIN32_PRIVATE };
enum { MODE_NONE, MODE_INSTALL, MODE_REMOVE };

static UINT optional_table(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_TABLE **table)
{
    UINT error = MimeMsiLoadTable(package->Database, name, table);
    if (error == ERROR_BAD_QUERY_SYNTAX) {
        *table = nullptr;
        return ERROR_SUCCESS;
    }
    return error;
}

static PCWSTR text_of(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    return index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
}

static BOOL same_name(PCWSTR a, PCWSTR b)
{
    return !_wcsicmp(a, b);
}

/* the parts of an assembly's identity, which MsiAssemblyName holds a row for each of. */
typedef struct IDENTITY {
    PCWSTR Name, Version, Culture, Token, Architecture;
    PWSTR Full;
} IDENTITY;

static void identity_free(IDENTITY *identity)
{
    MimeMsiFree(identity->Full);
}

/* the name as the installer writes it: the name, then each other part as a quoted value. */
static UINT identity_load(MIME_MSI_PACKAGE *package, PCWSTR component, IDENTITY *identity)
{
    memset(identity, 0, sizeof(*identity));
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"MsiAssemblyName", &table);
    if (error)
        return error;
    SIZE_T length = 1;
    for (ULONG row = 0; table && row < table->Rows; row++) {
        PCWSTR owner = text_of(package, table, row, L"Component_");
        PCWSTR key = text_of(package, table, row, L"Name");
        PCWSTR value = text_of(package, table, row, L"Value");
        if (!owner || wcscmp(owner, component) || !key)
            continue;
        if (!value)
            value = L"";
        length += wcslen(key) + wcslen(value) + 5;
        if (same_name(key, L"Name"))
            identity->Name = value;
        else if (same_name(key, L"Version"))
            identity->Version = value;
        else if (same_name(key, L"Culture"))
            identity->Culture = value;
        else if (same_name(key, L"PublicKeyToken"))
            identity->Token = value;
        else if (same_name(key, L"ProcessorArchitecture"))
            identity->Architecture = value;
    }
    if (!identity->Name || !*identity->Name)
        return ERROR_INSTALL_PACKAGE_INVALID;
    identity->Full = MimeMsiAllocate(length * sizeof(WCHAR));
    if (!identity->Full)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T at = _snwprintf(identity->Full, length, L"%s", identity->Name);
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR owner = text_of(package, table, row, L"Component_");
        PCWSTR key = text_of(package, table, row, L"Name");
        PCWSTR value = text_of(package, table, row, L"Value");
        if (!owner || wcscmp(owner, component) || !key || same_name(key, L"Name"))
            continue;
        at += _snwprintf(identity->Full + at, length - at, L",%s=\"%s\"", key, value ? value : L"");
    }
    return ERROR_SUCCESS;
}

/* the row of a component in MsiAssembly. */
static UINT find_assembly(MIME_MSI_PACKAGE *package, PCWSTR component, MIME_MSI_TABLE **table, ULONG *row)
{
    *table = nullptr;
    UINT error = optional_table(package, L"MsiAssembly", table);
    for (ULONG i = 0; !error && *table && i < (*table)->Rows; i++) {
        PCWSTR owner = text_of(package, *table, i, L"Component_");
        if (owner && !wcscmp(owner, component)) {
            *row = i;
            return ERROR_SUCCESS;
        }
    }
    *table = nullptr;
    return error;
}

static int assembly_attributes(MIME_MSI_TABLE *table, ULONG row)
{
    ULONG column = MimeMsiColumn(table, L"Attributes", FALSE);
    int value = column == UINT32_MAX ? MSI_NULL_INTEGER : MimeMsiInteger(table, row, column);
    return value == MSI_NULL_INTEGER ? 0 : value;
}

static int assembly_type(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    PCWSTR application = text_of(package, table, row, L"File_Application");
    BOOL global = !application || !*application;
    if (assembly_attributes(table, row) & 1)
        return global ? TYPE_WIN32_GLOBAL : TYPE_WIN32_PRIVATE;
    return global ? TYPE_NET_GLOBAL : TYPE_NET_PRIVATE;
}

/* ------------------------------------------------------------------ the global assembly cache */

static PCWSTR cache_architecture(PCWSTR architecture)
{
    if (architecture && same_name(architecture, L"x86"))
        return L"GAC_32";
    if (architecture && (same_name(architecture, L"amd64") || same_name(architecture, L"x64")))
        return L"GAC_64";
    return L"GAC_MSIL";
}

/* a version as the cache writes it: four numbers, whatever zeros the package put in front of them. */
static void cache_version(PCWSTR version, WCHAR out[64])
{
    ULONG parts[4] = {};
    ULONG count = 0;
    PCWSTR p = version;
    BOOL valid = p && *p;
    while (valid && count < 4) {
        if (*p < L'0' || *p > L'9') {
            valid = FALSE;
            break;
        }
        ULONG64 value = 0;
        while (*p >= L'0' && *p <= L'9')
            value = value * 10 + (ULONG)(*p++ - L'0');
        if (value > 0xFFFF) {
            valid = FALSE;
            break;
        }
        parts[count++] = (ULONG)value;
        if (*p == L'.')
            p++;
        else
            break;
    }
    if (!valid || *p)
        lstrcpynW(out, version && *version ? version : L"0.0.0.0", 64);
    else
        _snwprintf(out, 64, L"%u.%u.%u.%u", parts[0], parts[1], parts[2], parts[3]);
}

/*
 * where the files of a global assembly go. the .NET Framework 4 keeps its cache below Microsoft.NET\assembly, by processor,
 * assembly name and a folder of the version, the culture and the public key token, the token in lower case. an assembly of
 * the older runtimes would go to Windows\assembly in the same arrangement, which this machine has no runtime for.
 */
UINT MimeMsiAssemblyFolder(MIME_MSI_PACKAGE *package, PCWSTR component, PWSTR *folder)
{
    *folder = nullptr;
    MIME_MSI_TABLE *table;
    ULONG row;
    UINT error = find_assembly(package, component, &table, &row);
    if (error || !table)
        return error;
    int type = assembly_type(package, table, row);
    if (type == TYPE_WIN32_GLOBAL)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (type != TYPE_NET_GLOBAL)
        return ERROR_SUCCESS;
    IDENTITY identity;
    error = identity_load(package, component, &identity);
    if (error)
        return error;
    /* an assembly without a strong name cannot be shared. */
    if (!identity.Token || !*identity.Token) {
        identity_free(&identity);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    WCHAR windows[MAX_PATH];
    UINT length = GetWindowsDirectoryW(windows, RTL_NUMBER_OF(windows));
    PCWSTR culture = identity.Culture && !same_name(identity.Culture, L"neutral") ? identity.Culture : L"";
    SIZE_T size = (SIZE_T)length + wcslen(identity.Name) + wcslen(identity.Token) + wcslen(culture) + 96;
    if (!length || length >= RTL_NUMBER_OF(windows) || (identity.Version ? wcslen(identity.Version) : 0) > 64) {
        identity_free(&identity);
        return ERROR_BAD_PATHNAME;
    }
    *folder = MimeMsiAllocate(size * sizeof(WCHAR));
    if (!*folder) {
        identity_free(&identity);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    WCHAR version[64];
    cache_version(identity.Version, version);
    SIZE_T at = _snwprintf(*folder, size, L"%s\\Microsoft.NET\\assembly\\%s\\%s\\v4.0_%s_%s_", windows,
                           cache_architecture(identity.Architecture), identity.Name, version, culture);
    for (PCWSTR p = identity.Token; *p; p++)
        (*folder)[at++] = (*p >= L'A' && *p <= L'Z') ? (WCHAR)(*p + L'a' - L'A') : *p;
    (*folder)[at] = 0;
    identity_free(&identity);
    return ERROR_SUCCESS;
}

/* the folders of a removed assembly, which are left empty, go with its files. */
UINT MimeMsiAssemblyCleanup(MIME_MSI_PACKAGE *package, PCWSTR component)
{
    PWSTR folder;
    UINT error = MimeMsiAssemblyFolder(package, component, &folder);
    if (error || !folder)
        return error;
    /* the folder of the version, then of the name. the cache itself stays. */
    error = MimeMsiRemoveDirectory(package, folder);
    PWSTR name = wcsrchr(folder, L'\\');
    if (!error && name) {
        *name = 0;
        error = MimeMsiRemoveDirectory(package, folder);
    }
    MimeMsiFree(folder);
    return error;
}

/* ------------------------------------------------------------------ publishing */

static PCWSTR component_code(MIME_MSI_PACKAGE *package, PCWSTR name)
{
    MIME_MSI_TABLE *table;
    if (MimeMsiLoadTable(package->Database, L"Component", &table))
        return nullptr;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR candidate = text_of(package, table, row, L"Component");
        if (candidate && !wcscmp(candidate, name))
            return text_of(package, table, row, L"ComponentId");
    }
    return nullptr;
}

/* how the product changes the component: installs it, removes it, or leaves it. */
static UINT component_mode(MIME_MSI_PACKAGE *package, PCWSTR name, ULONG *mode)
{
    BOOL local, registered;
    UINT error = MimeMsiComponentChange(package, name, &local, &registered);
    if (error)
        return error;
    *mode = local ? MODE_INSTALL : registered ? MODE_REMOVE : MODE_NONE;
    return ERROR_SUCCESS;
}

/* the component that holds a file of the package. */
static PCWSTR file_component(MIME_MSI_PACKAGE *package, PCWSTR file)
{
    MIME_MSI_TABLE *table;
    if (MimeMsiLoadTable(package->Database, L"File", &table))
        return nullptr;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR candidate = text_of(package, table, row, L"File");
        if (candidate && !wcscmp(candidate, file))
            return text_of(package, table, row, L"Component_");
    }
    return nullptr;
}

/* the key below Installer\Assemblies: Global, or the path of the application with bars for the backslashes. */
static UINT publish_key(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, int type, PWSTR *key)
{
    if (type == TYPE_NET_GLOBAL || type == TYPE_WIN32_GLOBAL) {
        *key = MimeMsiDuplicate(L"Global");
        return *key ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    PWSTR path;
    UINT error = MimeMsiFileTargetPath(package, text_of(package, table, row, L"File_Application"), &path);
    if (error)
        return error;
    for (PWSTR p = path; *p; p++)
        if (*p == L'\\')
            *p = L'|';
    *key = path;
    return ERROR_SUCCESS;
}

static UINT publish_assemblies(MIME_MSI_PACKAGE *package, HKEY root, BOOL publish)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"MsiAssembly", &table);
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    for (ULONG row = 0; table && !error && row < table->Rows; row++) {
        PCWSTR component = text_of(package, table, row, L"Component_");
        PCWSTR feature = text_of(package, table, row, L"Feature_");
        PCWSTR code = component ? component_code(package, component) : nullptr;
        if (!component || !feature || !code || !*code)
            return ERROR_INSTALL_PACKAGE_INVALID;
        int type = assembly_type(package, table, row);
        /* a private assembly follows its application. */
        PCWSTR owner = component;
        if (type == TYPE_NET_PRIVATE || type == TYPE_WIN32_PRIVATE) {
            owner = file_component(package, text_of(package, table, row, L"File_Application"));
            if (!owner)
                return ERROR_INSTALL_PACKAGE_INVALID;
        }
        ULONG mode;
        error = component_mode(package, owner, &mode);
        if (error)
            return error;
        if (mode != (publish ? MODE_INSTALL : MODE_REMOVE))
            continue;
        if (type == TYPE_WIN32_GLOBAL)
            return ERROR_CALL_NOT_IMPLEMENTED;
        IDENTITY identity;
        PWSTR descriptor = nullptr, key = nullptr, path = nullptr;
        error = identity_load(package, component, &identity);
        if (!error)
            error = MimeMsiDescriptor(product, feature, code, &descriptor);
        if (!error)
            error = publish_key(package, table, row, type, &key);
        if (!error) {
            SIZE_T size = wcslen(key) + 96;
            path = MimeMsiAllocate(size * sizeof(WCHAR));
            if (!path)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else
                _snwprintf(path, size, root == HKEY_LOCAL_MACHINE ? L"Software\\Classes\\Installer\\%s\\%s" :
                           L"Software\\Microsoft\\Installer\\%s\\%s",
                           type == TYPE_WIN32_PRIVATE ? L"Win32Assemblies" : L"Assemblies", key);
        }
        if (!error && publish)
            error = MimeMsiWriteRegistry(package, root, path, identity.Full, REG_SZ, descriptor,
                                         (DWORD)((wcslen(descriptor) + 1) * sizeof(WCHAR)));
        else if (!error) {
            /* another product may have published the same assembly, which stays. */
            PWSTR current = nullptr;
            HKEY opened;
            if (!RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | package->RegistryView, &opened)) {
                MimeMsiRegistryText(opened, identity.Full, &current);
                RegCloseKey(opened);
            }
            if (current && !wcscmp(current, descriptor)) {
                error = MimeMsiDeleteRegistryValue(package, root, path, identity.Full);
                /* the key of a private assembly goes with its last value. */
                if (!error && type != TYPE_NET_GLOBAL && !RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | package->RegistryView, &opened)) {
                    DWORD values = 1;
                    RegQueryInfoKeyW(opened, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
                    RegCloseKey(opened);
                    if (!values)
                        RegDeleteKeyExW(root, path, package->RegistryView, 0);
                }
            }
            MimeMsiFree(current);
        }
        MimeMsiFree(path);
        MimeMsiFree(key);
        MimeMsiFree(descriptor);
        identity_free(&identity);
    }
    return error;
}

UINT MimeMsiAssemblyAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    BOOL publish = !wcscmp(action, L"MsiPublishAssemblies");
    if (!publish && wcscmp(action, L"MsiUnpublishAssemblies"))
        return UINT32_MAX;
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"MsiAssembly", &table);
    if (error || !table || !table->Rows)
        return error;
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    PCWSTR all_users = MimeMsiGetProperty(package, L"ALLUSERS");
    /* 2 means all users when possible, which it is for an administrator. */
    if (*all_users && wcscmp(all_users, L"1") && wcscmp(all_users, L"2"))
        return ERROR_CALL_NOT_IMPLEMENTED;
    return publish_assemblies(package, *all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, publish);
}
