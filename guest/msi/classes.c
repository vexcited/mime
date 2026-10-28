/*
 * file types and COM classes. the Extension, Verb, ProgId, Class and AppId rows of the components being installed go
 * under the Software\Classes key of the machine or the user, and those of the components being removed come out again.
 * the keys follow what the installer of Windows writes. a server is also given a descriptor, which the shell and COM
 * use to install a missing feature on demand.
 */
#include "msi_private.h"

#define CLASSES_KEY L"Software\\Classes"

typedef struct COMPONENT_INFO {
    PCWSTR Id, KeyPath;
    int Attributes;
    DWORD View;
} COMPONENT_INFO;

static PCWSTR cell_text(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    PCWSTR text = index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
    return text && *text ? text : nullptr;
}

static int cell_number(MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, FALSE);
    return index == UINT32_MAX ? MSI_NULL_INTEGER : MimeMsiInteger(table, row, index);
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

static BOOL same_text(PCWSTR a, PCWSTR b)
{
    return a && b && !_wcsicmp(a, b);
}

static HKEY classes_root(MIME_MSI_PACKAGE *package)
{
    return *MimeMsiGetProperty(package, L"ALLUSERS") ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

/* a 64-bit component writes to the 64-bit view of the registry. */
static DWORD registry_view(int attributes)
{
#ifdef _WIN64
    return attributes != MSI_NULL_INTEGER && (attributes & 256) ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
#else
    (void)attributes;
    return 0;
#endif
}

static UINT component_info(MIME_MSI_PACKAGE *package, PCWSTR name, COMPONENT_INFO *info)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Component", &table);
    ULONG id = error ? UINT32_MAX : MimeMsiColumn(table, L"Component", TRUE);
    if (error)
        return error;
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR key = MimeMsiText(package->Database, table, row, id);
        if (!key || wcscmp(key, name))
            continue;
        info->Id = cell_text(package, table, row, L"ComponentId");
        info->KeyPath = cell_text(package, table, row, L"KeyPath");
        info->Attributes = cell_number(table, row, L"Attributes");
        info->View = registry_view(info->Attributes);
        return ERROR_SUCCESS;
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

/* whether a row follows its component being installed, or being removed. */
static UINT wanted(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features, PCWSTR component, PCWSTR feature, BOOL remove,
                   BOOL *out)
{
    INSTALLSTATE installed, action;
    *out = FALSE;
    UINT error = MimeMsiComponentState(package, component, &installed, &action);
    if (error)
        return error;
    if (remove) {
        *out = action == INSTALLSTATE_ABSENT;
        return ERROR_SUCCESS;
    }
    if (action != INSTALLSTATE_LOCAL)
        return ERROR_SUCCESS;
    *out = TRUE;
    return feature ? MimeMsiFeatureSelected(package, features, feature, 0, out) : ERROR_SUCCESS;
}

/* the path of the key file of a component, none when the key path is not a file. */
static UINT key_file(MIME_MSI_PACKAGE *package, const COMPONENT_INFO *info, PWSTR *path)
{
    *path = nullptr;
    if (!info->KeyPath || (info->Attributes != MSI_NULL_INTEGER && (info->Attributes & (4 | 32))))
        return ERROR_SUCCESS;
    UINT error = MimeMsiFileTargetPath(package, info->KeyPath, path);
    return error == ERROR_INSTALL_PACKAGE_INVALID ? ERROR_SUCCESS : error;
}

static PWSTR format_text(MIME_MSI_PACKAGE *package, PCWSTR text)
{
    PWSTR out;
    return text && !MimeMsiFormat(package, text, &out) ? out : nullptr;
}

static PWSTR join_text(PCWSTR a, PCWSTR separator, PCWSTR b)
{
    SIZE_T length = wcslen(a) + wcslen(separator) + wcslen(b) + 1;
    PWSTR out = MimeMsiAllocate(length * sizeof(WCHAR));
    if (out)
        _snwprintf(out, length, L"%s%s%s", a, separator, b);
    return out;
}

/* ------------------------------------------------------------------ registry helpers */

/* the key below Software\Classes named by up to four parts. */
static PWSTR class_key(PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PCWSTR parts[4] = {a, b, c, d};
    SIZE_T length = wcslen(CLASSES_KEY) + 1;
    for (ULONG i = 0; i < 4; i++)
        if (parts[i] && *parts[i])
            length += wcslen(parts[i]) + 1;
    PWSTR key = MimeMsiAllocate(length * sizeof(WCHAR));
    if (!key)
        return nullptr;
    wcscpy(key, CLASSES_KEY);
    for (ULONG i = 0; i < 4; i++)
        if (parts[i] && *parts[i]) {
            wcscat(key, L"\\");
            wcscat(key, parts[i]);
        }
    return key;
}

/* an empty value writes nothing. */
static UINT put(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR name, PCWSTR value, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    if (!value || !*value)
        return ERROR_SUCCESS;
    PWSTR key = class_key(a, b, c, d);
    if (!key)
        return ERROR_NOT_ENOUGH_MEMORY;
    /* the key is made first, so that a rollback takes the parents it needed too. */
    UINT error = MimeMsiCreateRegistryKey(package, root, key);
    if (!error)
        error = MimeMsiWriteRegistry(package, root, key, name ? name : L"", REG_SZ, value,
                                     (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    MimeMsiFree(key);
    return error;
}

static UINT make_key(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PWSTR key = class_key(a, b, c, d);
    if (!key)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = MimeMsiCreateRegistryKey(package, root, key);
    MimeMsiFree(key);
    return error;
}

static BOOL has_key(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PWSTR path = class_key(a, b, c, d);
    HKEY key;
    if (!path || RegOpenKeyExW(root, path, 0, KEY_READ | package->RegistryView, &key)) {
        MimeMsiFree(path);
        return FALSE;
    }
    RegCloseKey(key);
    MimeMsiFree(path);
    return TRUE;
}

/* deletes the key when it holds nothing, then the parents that held only it. */
static void prune(MIME_MSI_PACKAGE *package, HKEY root, PWSTR path)
{
    while (wcslen(path) > wcslen(CLASSES_KEY)) {
        HKEY key;
        DWORD subkeys = 1, values = 1;
        if (RegOpenKeyExW(root, path, 0, KEY_READ | package->RegistryView, &key))
            return;
        RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr,
                         nullptr);
        RegCloseKey(key);
        if (subkeys || values || RegDeleteKeyExW(root, path, package->RegistryView, 0))
            return;
        PWSTR separator = wcsrchr(path, L'\\');
        if (!separator)
            return;
        *separator = 0;
    }
}

static UINT drop_value(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR name, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PWSTR key = class_key(a, b, c, d);
    if (!key)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = MimeMsiDeleteRegistryValue(package, root, key, name ? name : L"");
    if (!error)
        prune(package, root, key);
    MimeMsiFree(key);
    return error;
}

static UINT drop_tree(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PWSTR key = class_key(a, b, c, d);
    if (!key)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = MimeMsiDeleteRegistryTree(package, root, key, 0);
    if (!error)
        prune(package, root, key);
    MimeMsiFree(key);
    return error;
}

/* the value of the default value of a key, or none. */
static PWSTR default_text(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PWSTR path = class_key(a, b, c, d), text = nullptr;
    HKEY key;
    if (path && !RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | package->RegistryView, &key)) {
        MimeMsiRegistryText(key, L"", &text);
        RegCloseKey(key);
    }
    MimeMsiFree(path);
    return text;
}

/* ------------------------------------------------------------------ icons */

/* the icon is kept in the folder of the product, and a key names it with the index of the picture. */
static PWSTR icon_value(MIME_MSI_PACKAGE *package, PCWSTR icon, int index)
{
    WCHAR directory[MAX_PATH];
    PCWSTR product = MimeMsiGetProperty(package, L"ProductCode");
    UINT length = GetWindowsDirectoryW(directory, RTL_NUMBER_OF(directory));
    if (!icon || !length || length + wcslen(product) + wcslen(icon) + 32 >= RTL_NUMBER_OF(directory))
        return nullptr;
    WCHAR file[MAX_PATH], text[MAX_PATH + 16];
    _snwprintf(file, RTL_NUMBER_OF(file), L"%s\\Installer\\%s\\%s", directory, product, icon);
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
        WCHAR stream[256];
        BYTE *bytes = nullptr;
        SIZE_T size = 0;
        _snwprintf(stream, RTL_NUMBER_OF(stream), L"Icon.%s", icon);
        if (!MimeMsiReadBinary(package->Database, stream, &bytes, &size))
            MimeMsiWriteInstalledFile(package, bytes, size, file);
        MimeMsiFree(bytes);
    }
    _snwprintf(text, RTL_NUMBER_OF(text), L"%s,%d", file, index == MSI_NULL_INTEGER ? 0 : index);
    return MimeMsiDuplicate(text);
}

/* ------------------------------------------------------------------ extensions */

typedef struct VERB_ORDER {
    ULONG Row;
    int Sequence;
} VERB_ORDER;

/* the verbs of an extension, those without a sequence first. */
static UINT sorted_verbs(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *verbs, PCWSTR extension, VERB_ORDER **out,
                         ULONG *count)
{
    *out = nullptr;
    *count = 0;
    if (!verbs)
        return ERROR_SUCCESS;
    VERB_ORDER *list = MimeMsiAllocate((verbs->Rows + 1) * sizeof(*list));
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG row = 0; row < verbs->Rows; row++) {
        PCWSTR owner = cell_text(package, verbs, row, L"Extension_");
        if (!owner || wcscmp(owner, extension))
            continue;
        int sequence = cell_number(verbs, row, L"Sequence");
        ULONG at = (*count)++;
        while (at && (sequence == MSI_NULL_INTEGER ? list[at - 1].Sequence != MSI_NULL_INTEGER :
                      list[at - 1].Sequence != MSI_NULL_INTEGER && list[at - 1].Sequence > sequence)) {
            list[at] = list[at - 1];
            at--;
        }
        list[at].Row = row;
        list[at].Sequence = sequence;
    }
    *out = list;
    return ERROR_SUCCESS;
}

static PWSTR with_argument(PCWSTR text, PCWSTR argument)
{
    return argument ? join_text(text, L" ", argument) : MimeMsiDuplicate(text);
}

static UINT write_extension(MIME_MSI_PACKAGE *package, HKEY root, MIME_MSI_TABLE *table, ULONG row,
                            MIME_MSI_TABLE *verbs, const COMPONENT_INFO *info, PCWSTR feature, BOOL remove)
{
    PCWSTR extension = cell_text(package, table, row, L"Extension");
    PCWSTR progid = cell_text(package, table, row, L"ProgId_");
    PCWSTR mime = cell_text(package, table, row, L"MIME_");
    if (!extension)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PWSTR dotted = join_text(L".", L"", extension), path = nullptr, descriptor = nullptr, list = nullptr;
    VERB_ORDER *order;
    ULONG count;
    UINT error = dotted ? sorted_verbs(package, verbs, extension, &order, &count) : ERROR_NOT_ENOUGH_MEMORY;
    if (error) {
        MimeMsiFree(dotted);
        return error;
    }
    PCWSTR base = progid ? progid : dotted;
    error = remove ? ERROR_SUCCESS : key_file(package, info, &path);
    if (!error && !remove && info->Id && feature)
        MimeMsiDescriptor(MimeMsiGetProperty(package, L"ProductCode"), feature, info->Id, &descriptor);
    for (ULONG i = 0; i < count && !error; i++) {
        ULONG verb_row = order[i].Row;
        PCWSTR verb = cell_text(package, verbs, verb_row, L"Verb");
        if (!verb)
            continue;
        if (remove) {
            error = drop_value(package, root, L"", base, L"shell", verb, L"command");
            if (!error)
                error = drop_value(package, root, L"command", base, L"shell", verb, L"command");
            if (!error)
                error = drop_value(package, root, L"", base, L"shell", verb, nullptr);
            continue;
        }
        PWSTR argument = format_text(package, cell_text(package, verbs, verb_row, L"Argument"));
        PWSTR command = format_text(package, cell_text(package, verbs, verb_row, L"Command"));
        PCWSTR extra = argument && *argument ? argument : nullptr;
        if (path) {
            PWSTR quoted = join_text(L"\"", path, L"\""), line = quoted ? with_argument(quoted, extra) : nullptr;
            error = line ? put(package, root, L"", line, base, L"shell", verb, L"command") : ERROR_NOT_ENOUGH_MEMORY;
            MimeMsiFree(line);
            MimeMsiFree(quoted);
        }
        if (!error && descriptor) {
            PWSTR line = with_argument(descriptor, extra);
            error = line ? put(package, root, L"command", line, base, L"shell", verb, L"command") : ERROR_NOT_ENOUGH_MEMORY;
            MimeMsiFree(line);
        }
        if (!error)
            error = put(package, root, L"", command, base, L"shell", verb, nullptr);
        /* the default verb is the first of those with a sequence, and the rest follow it. */
        if (!error && order[i].Sequence != MSI_NULL_INTEGER) {
            PWSTR joined = list ? join_text(list, L",", verb) : MimeMsiDuplicate(verb);
            MimeMsiFree(list);
            list = joined;
            if (!list)
                error = ERROR_NOT_ENOUGH_MEMORY;
        }
        MimeMsiFree(argument);
        MimeMsiFree(command);
    }
    if (!error && !remove)
        error = put(package, root, L"", list, base, L"shell", nullptr, nullptr);
    if (!error && remove) {
        PWSTR old = default_text(package, root, base, L"shell", nullptr, nullptr);
        if (old && count)
            error = drop_value(package, root, L"", base, L"shell", nullptr, nullptr);
        MimeMsiFree(old);
    }
    if (!error && !remove && progid)
        error = make_key(package, root, dotted, progid, L"ShellNew", nullptr);
    if (!error && !remove)
        error = put(package, root, L"Content Type", mime, dotted, nullptr, nullptr, nullptr);
    if (!error && !remove)
        error = put(package, root, L"", progid, dotted, nullptr, nullptr, nullptr);
    if (!error && remove) {
        error = mime ? drop_value(package, root, L"Content Type", dotted, nullptr, nullptr, nullptr) : ERROR_SUCCESS;
        PWSTR current = default_text(package, root, dotted, nullptr, nullptr, nullptr);
        if (!error && same_text(current, progid))
            error = drop_value(package, root, L"", dotted, nullptr, nullptr, nullptr);
        MimeMsiFree(current);
        /* a file type goes away with the last verb of its ProgId. */
        if (!error && progid && !has_key(package, root, progid, L"shell", nullptr, nullptr))
            error = drop_tree(package, root, dotted, nullptr, nullptr, nullptr);
    }
    MimeMsiFree(list);
    MimeMsiFree(descriptor);
    MimeMsiFree(path);
    MimeMsiFree(order);
    MimeMsiFree(dotted);
    return error;
}

static UINT extension_info(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *table, *verbs, *features;
    UINT error = optional_table(package, L"Extension", &table);
    if (!error)
        error = optional_table(package, L"Verb", &verbs);
    if (error || !table)
        return error;
    error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    HKEY root = classes_root(package);
    DWORD previous = package->RegistryView;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR feature = cell_text(package, table, row, L"Feature_");
        BOOL use;
        COMPONENT_INFO info;
        if (!component)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = wanted(package, features, component, feature, remove, &use);
        if (!error && use)
            error = component_info(package, component, &info);
        if (error || !use)
            continue;
        package->RegistryView = info.View;
        error = write_extension(package, root, table, row, verbs, &info, feature, remove);
        package->RegistryView = previous;
    }
    return error;
}

/* ------------------------------------------------------------------ ProgIds */

typedef struct PROGID_ROW {
    PCWSTR ProgId, Class, Description, Icon;
    int IconIndex;
    PCWSTR Parent, ParentDescription;
} PROGID_ROW;

/*
 * a ProgId is written only when something points to it, a class that names it or a verb that runs below it. the
 * version independent ProgId that names it as its parent follows it.
 */
static UINT write_progid(MIME_MSI_PACKAGE *package, HKEY root, const PROGID_ROW *row, BOOL remove)
{
    PCWSTR progid = row->ProgId, clsid = row->Class, parent = row->Parent;
    BOOL class_link = clsid && has_key(package, root, L"CLSID", clsid, L"ProgID", nullptr);
    BOOL shell_link = has_key(package, root, progid, L"shell", nullptr, nullptr);
    UINT error = ERROR_SUCCESS;
    if (!remove) {
        if (class_link) {
            error = put(package, root, L"", clsid, progid, L"CLSID", nullptr, nullptr);
            if (!error && parent) {
                error = put(package, root, L"", row->ParentDescription, parent, nullptr, nullptr, nullptr);
                if (!error)
                    error = put(package, root, L"", clsid, parent, L"CLSID", nullptr, nullptr);
                if (!error)
                    error = put(package, root, L"", progid, parent, L"CurVer", nullptr, nullptr);
            }
        }
        if (!error && (class_link || shell_link)) {
            error = put(package, root, L"", row->Description, progid, nullptr, nullptr, nullptr);
            PWSTR icon = row->Icon ? icon_value(package, row->Icon, row->IconIndex) : nullptr;
            if (!error)
                error = put(package, root, L"", icon, progid, L"DefaultIcon", nullptr, nullptr);
            MimeMsiFree(icon);
        }
        return error;
    }
    if (!class_link && !shell_link) {
        error = drop_tree(package, root, progid, nullptr, nullptr, nullptr);
        if (!error && parent)
            error = drop_tree(package, root, parent, nullptr, nullptr, nullptr);
    } else if (!class_link) {
        error = drop_value(package, root, L"", progid, L"CLSID", nullptr, nullptr);
        if (!error && parent) {
            error = drop_value(package, root, L"", parent, nullptr, nullptr, nullptr);
            if (!error)
                error = drop_value(package, root, L"", parent, L"CLSID", nullptr, nullptr);
            if (!error)
                error = drop_value(package, root, L"", parent, L"CurVer", nullptr, nullptr);
        }
    }
    return error;
}

static void progid_row(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PROGID_ROW *out)
{
    out->ProgId = cell_text(package, table, row, L"ProgId");
    out->Class = cell_text(package, table, row, L"Class_");
    out->Description = cell_text(package, table, row, L"Description");
    out->Icon = cell_text(package, table, row, L"Icon_");
    out->IconIndex = cell_number(table, row, L"IconIndex");
    out->Parent = out->ParentDescription = nullptr;
    for (ULONG other = 0; other < table->Rows; other++) {
        PCWSTR owner = cell_text(package, table, other, L"ProgId_Parent");
        if (owner && same_text(owner, out->ProgId)) {
            out->Parent = cell_text(package, table, other, L"ProgId");
            out->ParentDescription = cell_text(package, table, other, L"Description");
            break;
        }
    }
}

static UINT progid_info(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *table, *classes, *extensions, *features;
    UINT error = optional_table(package, L"ProgId", &table);
    if (!error)
        error = optional_table(package, L"Class", &classes);
    if (!error)
        error = optional_table(package, L"Extension", &extensions);
    if (error || !table)
        return error;
    error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    HKEY root = classes_root(package);
    DWORD previous = package->RegistryView;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PROGID_ROW progid;
        progid_row(package, table, row, &progid);
        if (!progid.ProgId)
            return ERROR_INSTALL_PACKAGE_INVALID;
        /* the owners are the classes that carry the ProgId and, without a class, the extensions that name it. */
        MIME_MSI_TABLE *owners = progid.Class ? classes : extensions;
        for (ULONG owner = 0; owners && owner < owners->Rows && !error; owner++) {
            PCWSTR key = cell_text(package, owners, owner, progid.Class ? L"CLSID" : L"ProgId_");
            PCWSTR component = cell_text(package, owners, owner, L"Component_");
            PCWSTR feature = cell_text(package, owners, owner, L"Feature_");
            if (!key || !component || wcscmp(key, progid.Class ? progid.Class : progid.ProgId))
                continue;
            /* a class with several contexts in one component is written once. */
            BOOL repeated = FALSE;
            for (ULONG earlier = 0; earlier < owner; earlier++) {
                PCWSTR other = cell_text(package, owners, earlier, progid.Class ? L"CLSID" : L"ProgId_");
                PCWSTR other_component = cell_text(package, owners, earlier, L"Component_");
                repeated |= other && other_component && !wcscmp(other, key) && !wcscmp(other_component, component);
            }
            BOOL use;
            COMPONENT_INFO info;
            if (repeated)
                continue;
            error = wanted(package, features, component, feature, remove, &use);
            if (!error && use)
                error = component_info(package, component, &info);
            if (error || !use)
                continue;
            package->RegistryView = info.View;
            error = write_progid(package, root, &progid, remove);
            package->RegistryView = previous;
        }
    }
    return error;
}

/* ------------------------------------------------------------------ classes */

static BOOL starts_with(PCWSTR text, PCWSTR prefix)
{
    return !_wcsnicmp(text, prefix, wcslen(prefix));
}

static UINT write_app_id(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR app_id)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"AppId", &table);
    for (ULONG row = 0; table && row < table->Rows && !error; row++) {
        PCWSTR key = cell_text(package, table, row, L"AppId");
        if (!key || wcscmp(key, app_id))
            continue;
        error = make_key(package, root, L"AppID", app_id, nullptr, nullptr);
        static const struct { PCWSTR Value, Column; } texts[] = {
            {L"DllSurrogate", L"DllSurrogate"}, {L"LocalService", L"LocalService"},
            {L"ServiceParameters", L"ServiceParameters"}, {L"RemoteServerName", L"RemoteServerName"}
        };
        for (ULONG i = 0; i < RTL_NUMBER_OF(texts) && !error; i++)
            error = put(package, root, texts[i].Value, cell_text(package, table, row, texts[i].Column), L"AppID", app_id,
                        nullptr, nullptr);
        int storage = cell_number(table, row, L"ActivateAtStorage");
        int interactive = cell_number(table, row, L"RunAsInteractiveUser");
        if (!error && storage != MSI_NULL_INTEGER && storage)
            error = put(package, root, L"ActivateAtStorage", L"Y", L"AppID", app_id, nullptr, nullptr);
        if (!error && interactive != MSI_NULL_INTEGER && interactive)
            error = put(package, root, L"RunAs", L"Interactive User", L"AppID", app_id, nullptr, nullptr);
    }
    return error;
}

/* the handler of a class is a number that stands for a library of OLE, or the name of one. */
static UINT write_handler(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR clsid, PCWSTR handler)
{
    PCWSTR end = handler;
    ULONG number = 0;
    while (*end >= L'0' && *end <= L'9' && number < 1000)
        number = number * 10 + (ULONG)(*end++ - L'0');
    if (*end || end == handler)
        return put(package, root, L"", handler, L"CLSID", clsid, L"InprocHandler32", nullptr);
    UINT error = ERROR_SUCCESS;
    if (number & 1)
        error = put(package, root, L"", L"ole2.dll", L"CLSID", clsid, L"InprocHandler", nullptr);
    if (!error && (number & 2))
        error = put(package, root, L"", L"ole32.dll", L"CLSID", clsid, L"InprocHandler32", nullptr);
    return error;
}

static UINT write_class(MIME_MSI_PACKAGE *package, HKEY root, MIME_MSI_TABLE *table, ULONG row,
                        const COMPONENT_INFO *info, PCWSTR feature)
{
    PCWSTR clsid = cell_text(package, table, row, L"CLSID");
    PCWSTR context = cell_text(package, table, row, L"Context");
    PCWSTR progid = cell_text(package, table, row, L"ProgId_Default");
    PCWSTR app_id = cell_text(package, table, row, L"AppId_");
    PCWSTR masks = cell_text(package, table, row, L"FileTypeMask");
    PCWSTR handler = cell_text(package, table, row, L"DefInprocHandler");
    PCWSTR icon = cell_text(package, table, row, L"Icon_");
    if (!clsid || !context)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PWSTR path = nullptr, server = nullptr, line = nullptr, descriptor = nullptr;
    PWSTR description = format_text(package, cell_text(package, table, row, L"Description"));
    PWSTR argument = format_text(package, cell_text(package, table, row, L"Argument"));
    PCWSTR extra = argument && *argument ? argument : nullptr;
    UINT error = key_file(package, info, &path);
    if (!error && path) {
        WCHAR shortened[MAX_PATH];
        int attributes = cell_number(table, row, L"Attributes");
        if (attributes != MSI_NULL_INTEGER && (attributes & 1) && wcsrchr(path, L'\\'))
            server = MimeMsiDuplicate(wcsrchr(path, L'\\') + 1);
        else if (starts_with(context, L"LocalServer") && GetShortPathNameW(path, shortened, RTL_NUMBER_OF(shortened)))
            server = MimeMsiDuplicate(shortened);
        else
            server = MimeMsiDuplicate(path);
        line = server ? with_argument(server, extra) : nullptr;
        if (!line)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error && info->Id && feature && !MimeMsiDescriptor(MimeMsiGetProperty(package, L"ProductCode"), feature, info->Id,
                                                              &descriptor)) {
        PWSTR joined = with_argument(descriptor, extra);
        MimeMsiFree(descriptor);
        descriptor = joined;
    }
    if (!error)
        error = put(package, root, L"", line, L"CLSID", clsid, context, nullptr);
    if (!error)
        error = put(package, root, context, descriptor, L"CLSID", clsid, context, nullptr);
    if (!error)
        error = put(package, root, L"", description, L"CLSID", clsid, nullptr, nullptr);
    if (!error)
        error = put(package, root, L"AppID", app_id, L"CLSID", clsid, nullptr, nullptr);
    if (!error)
        error = put(package, root, L"", progid, L"CLSID", clsid, L"ProgID", nullptr);
    if (!error && progid) {
        MIME_MSI_TABLE *progids;
        error = optional_table(package, L"ProgId", &progids);
        for (ULONG other = 0; progids && other < progids->Rows && !error; other++)
            if (same_text(cell_text(package, progids, other, L"ProgId_Parent"), progid)) {
                error = put(package, root, L"", cell_text(package, progids, other, L"ProgId"), L"CLSID", clsid,
                            L"VersionIndependentProgID", nullptr);
                break;
            }
    }
    if (!error && icon) {
        PWSTR value = icon_value(package, icon, cell_number(table, row, L"IconIndex"));
        error = put(package, root, L"", value, L"CLSID", clsid, L"DefaultIcon", nullptr);
        MimeMsiFree(value);
    }
    if (!error && handler)
        error = write_handler(package, root, clsid, handler);
    /* each mask of the list is a file type signature, numbered from zero. */
    ULONG number = 0;
    for (PCWSTR mask = masks; mask && *mask && !error; number++) {
        PCWSTR end = wcschr(mask, L';');
        SIZE_T length = end ? (SIZE_T)(end - mask) : wcslen(mask);
        WCHAR index[16];
        PWSTR text = MimeMsiAllocate((length + 1) * sizeof(WCHAR));
        if (!text)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            memcpy(text, mask, length * sizeof(WCHAR));
            _snwprintf(index, RTL_NUMBER_OF(index), L"%u", number);
            error = put(package, root, L"", text, L"FileType", clsid, index, nullptr);
            MimeMsiFree(text);
        }
        mask = end ? end + 1 : nullptr;
    }
    if (!error && app_id)
        error = write_app_id(package, root, app_id);
    MimeMsiFree(description);
    MimeMsiFree(argument);
    MimeMsiFree(descriptor);
    MimeMsiFree(server);
    MimeMsiFree(line);
    MimeMsiFree(path);
    return error;
}

static BOOL server_remains(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR clsid)
{
    static const PCWSTR servers[] = {L"InprocServer", L"InprocServer32", L"LocalServer", L"LocalServer32",
                                     L"InprocHandler", L"InprocHandler32"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(servers); i++)
        if (has_key(package, root, L"CLSID", clsid, servers[i], nullptr))
            return TRUE;
    return FALSE;
}

/* the class goes away with its last server. */
static UINT remove_class(MIME_MSI_PACKAGE *package, HKEY root, MIME_MSI_TABLE *table, ULONG row)
{
    PCWSTR clsid = cell_text(package, table, row, L"CLSID");
    PCWSTR context = cell_text(package, table, row, L"Context");
    PCWSTR app_id = cell_text(package, table, row, L"AppId_");
    if (!clsid || !context)
        return ERROR_INSTALL_PACKAGE_INVALID;
    UINT error = drop_tree(package, root, L"CLSID", clsid, context, nullptr);
    if (!error && cell_text(package, table, row, L"DefInprocHandler")) {
        error = drop_tree(package, root, L"CLSID", clsid, L"InprocHandler", nullptr);
        if (!error)
            error = drop_tree(package, root, L"CLSID", clsid, L"InprocHandler32", nullptr);
    }
    if (error || server_remains(package, root, clsid))
        return error;
    error = drop_tree(package, root, L"CLSID", clsid, nullptr, nullptr);
    if (!error)
        error = drop_tree(package, root, L"FileType", clsid, nullptr, nullptr);
    if (!error && app_id)
        error = drop_tree(package, root, L"AppID", app_id, nullptr, nullptr);
    return error;
}

static UINT class_info(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *table, *features;
    UINT error = optional_table(package, L"Class", &table);
    if (error || !table)
        return error;
    /* a local server is given by its short path, which a file has once it is there. */
    if (!remove)
        error = MimeMsiInstallQueuedFiles(package);
    if (error)
        return error;
    error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    HKEY root = classes_root(package);
    DWORD previous = package->RegistryView;
    for (ULONG row = 0; row < table->Rows && !error; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR feature = cell_text(package, table, row, L"Feature_");
        BOOL use;
        COMPONENT_INFO info;
        if (!component)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = wanted(package, features, component, feature, remove, &use);
        if (!error && use)
            error = component_info(package, component, &info);
        if (error || !use)
            continue;
        package->RegistryView = info.View;
        error = remove ? remove_class(package, root, table, row) : write_class(package, root, table, row, &info, feature);
        package->RegistryView = previous;
    }
    return error;
}

UINT MimeMsiClassesAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    static const struct {
        PCWSTR Name;
        UINT (*Run)(MIME_MSI_PACKAGE *package, BOOL remove);
        BOOL Remove;
    } actions[] = {
        {L"RegisterClassInfo", class_info, FALSE}, {L"UnregisterClassInfo", class_info, TRUE},
        {L"RegisterExtensionInfo", extension_info, FALSE}, {L"UnregisterExtensionInfo", extension_info, TRUE},
        {L"RegisterProgIdInfo", progid_info, FALSE}, {L"UnregisterProgIdInfo", progid_info, TRUE},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(actions); i++) {
        if (wcscmp(action, actions[i].Name))
            continue;
        if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
            return ERROR_FUNCTION_NOT_CALLED;
        return actions[i].Run(package, actions[i].Remove);
    }
    return UINT32_MAX;
}
