/*
 * the writable side of cfgmgr32: device, class and interface keys and properties, device node actions, and
 * interface arrival and removal notifications.
 *
 * a property that is not a registry value of the device is kept under its key as Properties\<set>\<id as four hex
 * digits>, with a numeric Type value and the data as bytes in the default value. windows' own layout is private to
 * these functions, so this one is mime's.
 */
#define MIME_BUILD_CFGMGR32
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/pnp.h>
#include <mime/win32/cfgmgr32.h>
#include "cfgmgr32_private.h"

#ifndef CR_INVALID_MACHINENAME
#define CR_INVALID_MACHINENAME 0x0000002F
#endif

#define ENUM_PATH L"SYSTEM\\CurrentControlSet\\Enum\\"
#define CLASS_PATH L"SYSTEM\\CurrentControlSet\\Control\\Class"
#define INTERFACE_CLASS_PATH L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses"

static WCHAR *put_hex(WCHAR *out, ULONG value, int digits)
{
    for (int i = digits - 1; i >= 0; i--)
        *out++ = L"0123456789ABCDEF"[value >> (i * 4) & 15];
    return out;
}

static void format_guid(const GUID *g, WCHAR *out)
{
    *out++ = L'{';
    out = put_hex(out, g->Data1, 8);
    *out++ = L'-';
    out = put_hex(out, g->Data2, 4);
    *out++ = L'-';
    out = put_hex(out, g->Data3, 4);
    *out++ = L'-';
    out = put_hex(out, g->Data4[0], 2);
    out = put_hex(out, g->Data4[1], 2);
    *out++ = L'-';
    for (int i = 2; i < 8; i++)
        out = put_hex(out, g->Data4[i], 2);
    *out++ = L'}';
    *out = 0;
}

static CONFIGRET from_registry(LSTATUS error)
{
    if (!error)
        return CR_SUCCESS;
    if (error == ERROR_ACCESS_DENIED)
        return CR_ACCESS_DENIED;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return CR_NO_SUCH_REGISTRY_KEY;
    return CR_REGISTRY_ERROR;
}

/* ------------------------------------------------------------------ the property store */

static BOOL type_valid(DEVPROPTYPE type)
{
    DEVPROPTYPE base = type & 0xfff, mod = type & 0xf000;
    return base <= 0x1f && (mod == 0 || mod == DEVPROP_TYPEMOD_ARRAY || mod == DEVPROP_TYPEMOD_LIST);
}

static LSTATUS property_key(HKEY base, const DEVPROPKEY *key, BOOL create, REGSAM access, HKEY *out)
{
    WCHAR path[128] = L"Properties\\", *at = path + 11;
    format_guid(&key->fmtid, at);
    at += 38;
    *at++ = L'\\';
    at = put_hex(at, key->pid, 4);
    lstrcpyW(at, L"\\00000000");
    return create ? RegCreateKeyExW(base, path, 0, nullptr, 0, access, nullptr, out, nullptr)
                  : RegOpenKeyExW(base, path, 0, access, out);
}

static CONFIGRET store_get(HKEY base, const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer, PULONG size)
{
    HKEY k;
    LSTATUS error = property_key(base, key, FALSE, KEY_READ, &k);
    if (error)
        return CR_NO_SUCH_VALUE;
    DWORD kind = 0, bytes = sizeof(kind), value_type;
    if (RegQueryValueExW(k, L"Type", nullptr, &value_type, (BYTE *)&kind, &bytes) || value_type != REG_DWORD) {
        RegCloseKey(k);
        return CR_REGISTRY_ERROR;
    }
    DWORD length = buffer ? *size : 0;
    error = RegQueryValueExW(k, nullptr, nullptr, &value_type, buffer, &length);
    RegCloseKey(k);
    *type = kind;
    if (error == ERROR_FILE_NOT_FOUND) {
        *size = 0;
        return CR_SUCCESS;
    }
    if (error && error != ERROR_MORE_DATA)
        return CR_REGISTRY_ERROR;
    *size = length;
    return error == ERROR_MORE_DATA || (!buffer && length) ? CR_BUFFER_SMALL : CR_SUCCESS;
}

/* removed when the type is empty and there is no data. */
static CONFIGRET store_set(HKEY base, const DEVPROPKEY *key, DEVPROPTYPE type, const BYTE *buffer, ULONG size)
{
    if (type == DEVPROP_TYPE_EMPTY && !buffer && !size) {
        WCHAR path[128] = L"Properties\\", *at = path + 11;
        format_guid(&key->fmtid, at);
        at += 38;
        *at++ = L'\\';
        *put_hex(at, key->pid, 4) = 0;
        LSTATUS error = RegDeleteTreeW(base, path);
        return error == ERROR_FILE_NOT_FOUND ? CR_NO_SUCH_VALUE : from_registry(error);
    }
    HKEY k;
    LSTATUS error = property_key(base, key, TRUE, KEY_WRITE, &k);
    if (error)
        return from_registry(error);
    DWORD kind = type;
    error = RegSetValueExW(k, L"Type", 0, REG_DWORD, (const BYTE *)&kind, sizeof(kind));
    if (!error)
        error = RegSetValueExW(k, nullptr, 0, REG_BINARY, buffer ? buffer : (const BYTE *)"", size);
    RegCloseKey(k);
    return from_registry(error);
}

static CONFIGRET store_keys(HKEY base, DEVPROPKEY *keys, ULONG capacity, PULONG count)
{
    HKEY root;
    if (RegOpenKeyExW(base, L"Properties", 0, KEY_READ, &root))
        return CR_SUCCESS;
    for (DWORD i = 0;; i++) {
        WCHAR set[64];
        DWORD n = RTL_NUMBER_OF(set);
        if (RegEnumKeyExW(root, i, set, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        GUID fmtid;
        HKEY group;
        if (!parse_guid(set, &fmtid) || RegOpenKeyExW(root, set, 0, KEY_READ, &group))
            continue;
        for (DWORD j = 0;; j++) {
            WCHAR id[16];
            DWORD m = RTL_NUMBER_OF(id);
            if (RegEnumKeyExW(group, j, id, &m, nullptr, nullptr, nullptr, nullptr))
                break;
            ULONG pid = 0;
            BOOL good = m > 0;
            for (DWORD d = 0; d < m; d++) {
                int digit = id[d] >= L'0' && id[d] <= L'9'   ? id[d] - L'0'
                            : id[d] >= L'A' && id[d] <= L'F' ? id[d] - L'A' + 10
                            : id[d] >= L'a' && id[d] <= L'f' ? id[d] - L'a' + 10
                                                             : -1;
                if (digit < 0)
                    good = FALSE;
                pid = pid << 4 | (ULONG)digit;
            }
            if (!good)
                continue;
            if (*count < capacity)
                keys[*count] = (DEVPROPKEY){fmtid, pid};
            (*count)++;
        }
        RegCloseKey(group);
    }
    RegCloseKey(root);
    return CR_SUCCESS;
}

/* reports the needed count when the caller's array is short. */
static CONFIGRET key_list_result(const DEVPROPKEY *found, ULONG found_count, DEVPROPKEY *out, PULONG count)
{
    ULONG capacity = out ? *count : 0;
    *count = found_count;
    if (found_count > capacity)
        return CR_BUFFER_SMALL;
    if (found_count)
        memcpy(out, found, found_count * sizeof(DEVPROPKEY));
    return CR_SUCCESS;
}

/* ------------------------------------------------------------------ device nodes */

CMAPI CONFIGRET WINAPI CM_Get_Depth(PULONG depth, DEVINST devinst, ULONG flags)
{
    if (!depth)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (!present(id))
        return CR_NO_SUCH_DEVNODE;
    ULONG n = 0;
    DEVINST at = devinst;
    while (!is_root(id_of(at)) && n < 64) {
        DEVINST parent;
        if (CM_Get_Parent(&parent, at, 0))
            return CR_FAILURE;
        at = parent;
        n++;
    }
    *depth = n;
    return CR_SUCCESS;
}

/* devices belong to mime's drivers, which stay loaded: always enabled, never disableable. */
CMAPI CONFIGRET WINAPI CM_Enable_DevNode(DEVINST devinst, ULONG flags)
{
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    return present(id) ? CR_SUCCESS : CR_NO_SUCH_DEVNODE;
}

CMAPI CONFIGRET WINAPI CM_Disable_DevNode(DEVINST devinst, ULONG flags)
{
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    return present(id) ? CR_NOT_DISABLEABLE : CR_NO_SUCH_DEVNODE;
}

/* a present device is already set up. */
CMAPI CONFIGRET WINAPI CM_Setup_DevNode(DEVINST devinst, ULONG flags)
{
    if (flags & ~1u)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    return present(id) ? CR_SUCCESS : CR_NO_SUCH_DEVNODE;
}

/*
 * only a device that is gone but left its key behind is removed; a present one belongs to a mime driver.
 */
CMAPI CONFIGRET WINAPI CM_Uninstall_DevNode(DEVINST devinst, ULONG flags)
{
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id || is_root(id))
        return CR_INVALID_DEVNODE;
    if (present(id))
        return CR_REMOVE_VETOED;
    WCHAR path[MAX_DEVICE_ID_LEN + 64] = ENUM_PATH;
    lstrcatW(path, id);
    LSTATUS error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
    return error == ERROR_FILE_NOT_FOUND ? CR_NO_SUCH_DEVNODE : from_registry(error);
}

#define PNP_VetoNonDisableable 10

/* a present device is always vetoed, and the veto names it. */
CMAPI CONFIGRET WINAPI CM_Query_And_Remove_SubTreeW(DEVINST devinst, PULONG veto_type, PWSTR veto_name, ULONG length,
                                                    ULONG flags)
{
    if (flags & ~3u)
        return CR_INVALID_FLAG;
    if (veto_name && !length)
        return CR_INVALID_POINTER;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (!present(id) || is_root(id))
        return CR_NO_SUCH_DEVNODE;
    if (veto_type)
        *veto_type = PNP_VetoNonDisableable;
    if (veto_name && length) {
        ULONG n = (ULONG)lstrlenW(id);
        if (n >= length)
            n = length - 1;
        memcpy(veto_name, id, n * sizeof(WCHAR));
        veto_name[n] = 0;
    }
    return CR_REMOVE_VETOED;
}

/* only the hardware key CM_Open_DevNode_Key opens, Device Parameters. */
CMAPI CONFIGRET WINAPI CM_Delete_DevNode_Key(DEVINST devinst, ULONG profile, ULONG flags)
{
    if (flags & ~(ULONG)(CM_REGISTRY_SOFTWARE | CM_REGISTRY_USER | CM_REGISTRY_CONFIG))
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    WCHAR path[MAX_DEVICE_ID_LEN + 96] = ENUM_PATH;
    lstrcatW(path, id);
    lstrcatW(path, L"\\Device Parameters");
    LSTATUS error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
    return from_registry(error);
}

/* ------------------------------------------------------------------ properties of device nodes */

static const struct {
    const DEVPROPKEY *Key;
    ULONG Property;
} g_mapped[] = {
    {&DEVPKEY_Device_DeviceDesc, CM_DRP_DEVICEDESC},
    {&DEVPKEY_Device_HardwareIds, CM_DRP_HARDWAREID},
    {&DEVPKEY_Device_CompatibleIds, CM_DRP_COMPATIBLEIDS},
    {&DEVPKEY_Device_Service, CM_DRP_SERVICE},
    {&DEVPKEY_Device_Class, CM_DRP_CLASS},
    {&DEVPKEY_Device_ClassGuid, CM_DRP_CLASSGUID},
    {&DEVPKEY_Device_Manufacturer, CM_DRP_MFG},
    {&DEVPKEY_Device_FriendlyName, CM_DRP_FRIENDLYNAME},
    {&DEVPKEY_Device_LocationInfo, CM_DRP_LOCATION_INFORMATION},
};

static PCWSTR property_value_name(ULONG property)
{
    switch (property) {
    case CM_DRP_DEVICEDESC: return L"DeviceDesc";
    case CM_DRP_HARDWAREID: return L"HardwareID";
    case CM_DRP_COMPATIBLEIDS: return L"CompatibleIDs";
    case CM_DRP_SERVICE: return L"Service";
    case CM_DRP_CLASS: return L"Class";
    case CM_DRP_CLASSGUID: return L"ClassGUID";
    case CM_DRP_DRIVER: return L"Driver";
    case CM_DRP_CONFIGFLAGS: return L"ConfigFlags";
    case CM_DRP_MFG: return L"Mfg";
    case CM_DRP_FRIENDLYNAME: return L"FriendlyName";
    case CM_DRP_LOCATION_INFORMATION: return L"LocationInformation";
    case CM_DRP_UPPERFILTERS: return L"UpperFilters";
    case CM_DRP_LOWERFILTERS: return L"LowerFilters";
    default: return nullptr;
    }
}

static DWORD property_registry_type(ULONG property)
{
    switch (property) {
    case CM_DRP_HARDWAREID:
    case CM_DRP_COMPATIBLEIDS:
    case CM_DRP_UPPERFILTERS:
    case CM_DRP_LOWERFILTERS:
        return REG_MULTI_SZ;
    case CM_DRP_CONFIGFLAGS:
        return REG_DWORD;
    default:
        return REG_SZ;
    }
}

CMAPI CONFIGRET WINAPI CM_Set_DevNode_Registry_PropertyW(DEVINST devinst, ULONG property, PVOID buffer, ULONG length,
                                                         ULONG flags)
{
    if (flags)
        return CR_INVALID_FLAG;
    if (buffer && !length)
        return CR_INVALID_DATA;
    if (property < CM_DRP_DEVICEDESC || property > CM_DRP_MAX)
        return CR_INVALID_PROPERTY;
    PCWSTR name = property_value_name(property);
    if (!name || property == CM_DRP_DRIVER)
        return CR_INVALID_PROPERTY;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    HKEY key = open_enum(id, KEY_SET_VALUE);
    if (!key)
        return is_root(id) ? CR_INVALID_DEVNODE : CR_NO_SUCH_DEVNODE;
    LSTATUS error;
    if (!buffer) {
        error = RegDeleteValueW(key, name);
        if (error == ERROR_FILE_NOT_FOUND)
            error = 0;
    } else {
        error = RegSetValueExW(key, name, 0, property_registry_type(property), buffer, length);
    }
    RegCloseKey(key);
    return from_registry(error);
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Property_Keys(DEVINST devinst, DEVPROPKEY *keys, PULONG count, ULONG flags)
{
    if (!count)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    DEVPROPKEY found[64];
    ULONG n = 0;
    found[n++] = DEVPKEY_Device_InstanceId;
    DEVINST parent;
    if (!is_root(id) && !CM_Get_Parent(&parent, devinst, 0))
        found[n++] = DEVPKEY_Device_Parent;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mapped) && n < RTL_NUMBER_OF(found); i++) {
        ULONG size = 0, kind;
        CONFIGRET cr = CM_Get_DevNode_Registry_PropertyW(devinst, g_mapped[i].Property, &kind, nullptr, &size, 0);
        if (cr == CR_SUCCESS || cr == CR_BUFFER_SMALL)
            found[n++] = *g_mapped[i].Key;
    }
    HKEY base = open_enum(id, KEY_READ);
    if (base) {
        ULONG stored = 0;
        store_keys(base, found + n, (ULONG)RTL_NUMBER_OF(found) - n, &stored);
        RegCloseKey(base);
        n += stored < RTL_NUMBER_OF(found) - n ? stored : (ULONG)RTL_NUMBER_OF(found) - n;
    }
    return key_list_result(found, n, keys, count);
}

/* registry mapped keys are written through their value, others into the property store. */
CMAPI CONFIGRET WINAPI CM_Set_DevNode_PropertyW(DEVINST devinst, const DEVPROPKEY *key, DEVPROPTYPE type,
                                                PBYTE buffer, ULONG size, ULONG flags)
{
    if (!key)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (!type_valid(type) || (buffer && !size) || (!buffer && size))
        return CR_INVALID_DATA;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (key_is(key, &DEVPKEY_Device_InstanceId) || key_is(key, &DEVPKEY_Device_Parent) ||
        key_is(key, &DEVPKEY_Device_Children))
        return CR_INVALID_PROPERTY;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mapped); i++) {
        if (!key_is(key, g_mapped[i].Key) || g_mapped[i].Property == CM_DRP_CLASSGUID)
            continue;
        DEVPROPTYPE want = property_registry_type(g_mapped[i].Property) == REG_MULTI_SZ ? DEVPROP_TYPE_STRING_LIST
                                                                                       : DEVPROP_TYPE_STRING;
        if (buffer && type != want)
            return CR_INVALID_DATA;
        return CM_Set_DevNode_Registry_PropertyW(devinst, g_mapped[i].Property, buffer, size, 0);
    }
    HKEY base = open_enum(id, KEY_ALL_ACCESS);
    if (!base)
        return is_root(id) ? CR_INVALID_DEVNODE : CR_NO_SUCH_DEVNODE;
    CONFIGRET cr = store_set(base, key, type, buffer, size);
    RegCloseKey(base);
    return cr;
}

/* ------------------------------------------------------------------ classes */

static LSTATUS class_open(const GUID *klass, REGSAM access, BOOL create, HKEY *out)
{
    WCHAR path[128] = CLASS_PATH L"\\";
    format_guid(klass, path + lstrlenW(path));
    return create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, out, nullptr)
                  : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, out);
}

/* finds the Control\Class key whose Class value is the name. */
static BOOL class_by_name(PCWSTR name, GUID *klass)
{
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, CLASS_PATH, 0, KEY_READ, &root))
        return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        WCHAR sub[64];
        DWORD n = RTL_NUMBER_OF(sub);
        if (RegEnumKeyExW(root, i, sub, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        GUID g;
        HKEY k;
        if (!parse_guid(sub, &g) || RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &k))
            continue;
        WCHAR value[64];
        DWORD bytes = sizeof(value) - sizeof(WCHAR), type;
        if (!RegQueryValueExW(k, L"Class", nullptr, &type, (BYTE *)value, &bytes) && type == REG_SZ) {
            value[bytes / sizeof(WCHAR)] = 0;
            if (!lstrcmpiW(value, name)) {
                *klass = g;
                found = TRUE;
            }
        }
        RegCloseKey(k);
    }
    RegCloseKey(root);
    return found;
}

CMAPI CONFIGRET WINAPI CM_Open_Class_KeyW(const GUID *klass, PCWSTR name, REGSAM access, DWORD disposition, HKEY *key,
                                          ULONG flags)
{
    if (!key)
        return CR_INVALID_POINTER;
    if (flags > CM_OPEN_CLASS_KEY_INTERFACE)
        return CR_INVALID_FLAG;
    if (disposition > RegDisposition_OpenExisting)
        return CR_INVALID_FLAG;
    GUID by_name;
    if (!klass && name) {
        if (flags == CM_OPEN_CLASS_KEY_INTERFACE || !class_by_name(name, &by_name))
            return CR_NO_SUCH_REGISTRY_KEY;
        klass = &by_name;
    }
    WCHAR path[128];
    lstrcpyW(path, flags == CM_OPEN_CLASS_KEY_INTERFACE ? INTERFACE_CLASS_PATH : CLASS_PATH);
    if (klass) {
        lstrcatW(path, L"\\");
        format_guid(klass, path + lstrlenW(path));
    }
    LSTATUS error = disposition == RegDisposition_OpenAlways
                        ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, key, nullptr)
                        : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, key);
    return from_registry(error);
}

CMAPI CONFIGRET WINAPI CM_Delete_Class_Key(const GUID *klass, ULONG flags)
{
    if (!klass)
        return CR_INVALID_POINTER;
    if (flags > CM_DELETE_CLASS_SUBKEYS)
        return CR_INVALID_FLAG;
    WCHAR path[128] = CLASS_PATH L"\\";
    format_guid(klass, path + lstrlenW(path));
    if (flags == CM_DELETE_CLASS_ONLY) {
        /* only a class with no subkeys is deleted, otherwise it is a registry error. */
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
            return CR_NO_SUCH_REGISTRY_KEY;
        DWORD subkeys = 0;
        RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr);
        RegCloseKey(key);
        if (subkeys)
            return CR_REGISTRY_ERROR;
        return from_registry(RegDeleteKeyExW(HKEY_LOCAL_MACHINE, path, 0, 0));
    }
    return from_registry(RegDeleteTreeW(HKEY_LOCAL_MACHINE, path));
}

static PCWSTR class_value_name(ULONG property, DWORD *type)
{
    switch (property) {
    case CM_CRP_UPPERFILTERS:
        *type = REG_MULTI_SZ;
        return L"UpperFilters";
    case CM_CRP_LOWERFILTERS:
        *type = REG_MULTI_SZ;
        return L"LowerFilters";
    case CM_CRP_SECURITY:
        *type = REG_BINARY;
        return L"Security";
    case CM_CRP_DEVTYPE:
        *type = REG_DWORD;
        return L"DeviceType";
    case CM_CRP_EXCLUSIVE:
        *type = REG_DWORD;
        return L"Exclusive";
    case CM_CRP_CHARACTERISTICS:
        *type = REG_DWORD;
        return L"DeviceCharacteristics";
    default:
        return nullptr;
    }
}

CMAPI CONFIGRET WINAPI CM_Get_Class_Registry_PropertyW(const GUID *klass, ULONG property, PULONG type, PVOID buffer,
                                                       PULONG length, ULONG flags, void *machine)
{
    if (!klass || !length)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (machine)
        return CR_INVALID_MACHINENAME;
    DWORD expected;
    PCWSTR name = class_value_name(property, &expected);
    if (!name)
        return CR_INVALID_PROPERTY;
    HKEY key;
    if (class_open(klass, KEY_QUERY_VALUE, FALSE, &key))
        return CR_NO_SUCH_REGISTRY_KEY;
    DWORD kind, size = buffer ? *length : 0;
    LSTATUS error = RegQueryValueExW(key, name, nullptr, &kind, buffer, &size);
    RegCloseKey(key);
    if (error == ERROR_FILE_NOT_FOUND)
        return CR_NO_SUCH_VALUE;
    if (type)
        *type = kind;
    *length = size;
    if (error == ERROR_MORE_DATA || (!buffer && !error))
        return CR_BUFFER_SMALL;
    return error ? CR_REGISTRY_ERROR : CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Set_Class_Registry_PropertyW(const GUID *klass, ULONG property, PVOID buffer, ULONG length,
                                                       ULONG flags, void *machine)
{
    if (!klass)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (machine)
        return CR_INVALID_MACHINENAME;
    DWORD kind;
    PCWSTR name = class_value_name(property, &kind);
    if (!name)
        return CR_INVALID_PROPERTY;
    if (buffer && !length)
        return CR_INVALID_DATA;
    HKEY key;
    if (class_open(klass, KEY_SET_VALUE, FALSE, &key))
        return CR_NO_SUCH_REGISTRY_KEY;
    LSTATUS error;
    if (!buffer) {
        error = RegDeleteValueW(key, name);
        if (error == ERROR_FILE_NOT_FOUND)
            error = 0;
    } else {
        error = RegSetValueExW(key, name, 0, kind, buffer, length);
    }
    RegCloseKey(key);
    return from_registry(error);
}

CMAPI CONFIGRET WINAPI CM_Get_Class_PropertyW(const GUID *klass, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                              PBYTE buffer, PULONG size, ULONG flags)
{
    if (!klass || !key || !type || !size)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    HKEY base;
    if (class_open(klass, KEY_READ, FALSE, &base))
        return CR_NO_SUCH_REGISTRY_KEY;
    CONFIGRET cr = store_get(base, key, type, buffer, size);
    RegCloseKey(base);
    return cr;
}

CMAPI CONFIGRET WINAPI CM_Set_Class_PropertyW(const GUID *klass, const DEVPROPKEY *key, DEVPROPTYPE type,
                                              PBYTE buffer, ULONG size, ULONG flags)
{
    if (!klass || !key)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (!type_valid(type) || (buffer && !size) || (!buffer && size))
        return CR_INVALID_DATA;
    HKEY base;
    if (class_open(klass, KEY_ALL_ACCESS, FALSE, &base))
        return CR_NO_SUCH_REGISTRY_KEY;
    CONFIGRET cr = store_set(base, key, type, buffer, size);
    RegCloseKey(base);
    return cr;
}

CMAPI CONFIGRET WINAPI CM_Get_Class_Property_Keys(const GUID *klass, DEVPROPKEY *keys, PULONG count, ULONG flags)
{
    if (!klass || !count)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    HKEY base;
    if (class_open(klass, KEY_READ, FALSE, &base))
        return CR_NO_SUCH_REGISTRY_KEY;
    DEVPROPKEY found[64];
    ULONG n = 0;
    store_keys(base, found, RTL_NUMBER_OF(found), &n);
    RegCloseKey(base);
    if (n > RTL_NUMBER_OF(found))
        n = RTL_NUMBER_OF(found);
    return key_list_result(found, n, keys, count);
}

/* ------------------------------------------------------------------ interfaces */

/* the key under DeviceClasses names the path with \\?\ as ##?#; its parent holds the properties. */
static BOOL interface_path(PCWSTR link, WCHAR *path, ULONG chars)
{
    PCWSTR brace = nullptr;
    for (PCWSTR p = link; *p; p++)
        if (*p == L'{')
            brace = p;
    if (!brace || lstrlenW(link) < 5 || lstrlenW(link) + 200 > (int)chars)
        return FALSE;
    lstrcpyW(path, INTERFACE_CLASS_PATH L"\\");
    lstrcatW(path, brace);
    lstrcatW(path, L"\\##?#");
    lstrcatW(path, link + 4);
    lstrcatW(path, L"\\#");
    return TRUE;
}

static BOOL interface_known(PCWSTR link)
{
    MIME_PNP_REQUEST *r;
    if (!NT_SUCCESS(request(MIME_PNP_INTERFACE_DEVICE, 0, nullptr, link, &r)))
        return FALSE;
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

static CONFIGRET interface_base(PCWSTR link, BOOL create, REGSAM access, HKEY *out)
{
    WCHAR path[MAX_PATH + 256];
    if (!link)
        return CR_INVALID_POINTER;
    if (!interface_path(link, path, RTL_NUMBER_OF(path)))
        return CR_NO_SUCH_DEVICE_INTERFACE;
    if (!interface_known(link))
        return CR_NO_SUCH_DEVICE_INTERFACE;
    LSTATUS error = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, out, nullptr)
                           : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, out);
    return from_registry(error);
}

CMAPI CONFIGRET WINAPI CM_Open_Device_Interface_KeyW(PCWSTR path, REGSAM access, DWORD disposition, HKEY *key,
                                                     ULONG flags)
{
    if (!path || !key)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (disposition > RegDisposition_OpenExisting)
        return CR_INVALID_FLAG;
    HKEY base;
    CONFIGRET cr = interface_base(path, disposition == RegDisposition_OpenAlways, KEY_ALL_ACCESS, &base);
    if (cr)
        return cr;
    LSTATUS error = disposition == RegDisposition_OpenAlways
                        ? RegCreateKeyExW(base, L"Device Parameters", 0, nullptr, 0, access, nullptr, key, nullptr)
                        : RegOpenKeyExW(base, L"Device Parameters", 0, access, key);
    RegCloseKey(base);
    return from_registry(error);
}

CMAPI CONFIGRET WINAPI CM_Delete_Device_Interface_KeyW(PCWSTR path, ULONG flags)
{
    if (!path)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    HKEY base;
    CONFIGRET cr = interface_base(path, FALSE, KEY_ALL_ACCESS, &base);
    if (cr)
        return cr;
    LSTATUS error = RegDeleteTreeW(base, L"Device Parameters");
    RegCloseKey(base);
    return from_registry(error);
}

/* every interface has these: enabled, and its class. */
MIME_DEVPROPKEY(g_interface_enabled, 0x14C83A99, 0x0B3F, 0x44B7, 0xBE, 0x4C, 0xA1, 0x78, 0xD3, 0x99, 0x05, 0x64, 2);
MIME_DEVPROPKEY(g_interface_class, 0x026E516E, 0xB814, 0x414B, 0x83, 0xCD, 0x85, 0x6D, 0x6F, 0xEF, 0x48, 0x22, 4);

static BOOL interface_class(PCWSTR link, GUID *klass)
{
    MIME_PNP_REQUEST *r;
    if (!NT_SUCCESS(request(MIME_PNP_INTERFACE_DEVICE, 0, nullptr, link, &r)))
        return FALSE;
    *klass = r->Class;
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_Property_KeysW(PCWSTR path, DEVPROPKEY *keys, PULONG count,
                                                              ULONG flags)
{
    if (!path || !count)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (!interface_known(path))
        return CR_NO_SUCH_DEVICE_INTERFACE;
    DEVPROPKEY found[64];
    ULONG n = 0;
    found[n++] = g_interface_enabled;
    found[n++] = g_interface_class;
    HKEY base;
    if (!interface_base(path, FALSE, KEY_READ, &base)) {
        ULONG stored = 0;
        store_keys(base, found + n, (ULONG)RTL_NUMBER_OF(found) - n, &stored);
        RegCloseKey(base);
        n += stored < RTL_NUMBER_OF(found) - n ? stored : (ULONG)RTL_NUMBER_OF(found) - n;
    }
    return key_list_result(found, n, keys, count);
}

CMAPI CONFIGRET WINAPI CM_Set_Device_Interface_PropertyW(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE type,
                                                         PBYTE buffer, ULONG size, ULONG flags)
{
    if (!path || !key)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (!type_valid(type) || (buffer && !size) || (!buffer && size))
        return CR_INVALID_DATA;
    if (key_is(key, &g_interface_class))
        return CR_INVALID_PROPERTY;
    HKEY base;
    CONFIGRET cr = interface_base(path, TRUE, KEY_ALL_ACCESS, &base);
    if (cr)
        return cr;
    cr = store_set(base, key, type, buffer, size);
    RegCloseKey(base);
    return cr;
}

/* there are never aliases in other classes. */
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_AliasW(PCWSTR path, GUID *alias_class, PWSTR alias, PULONG length,
                                                      ULONG flags)
{
    if (!path || !alias_class || !alias || !length)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (!interface_known(path))
        return CR_NO_SUCH_DEVICE_INTERFACE;
    return CR_NO_SUCH_DEVICE_INTERFACE;
}

/* answers the interface's own keys; any other property is the device's. */
CONFIGRET InterfaceOwnProperty(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer, PULONG size,
                               BOOL *handled)
{
    *handled = FALSE;
    GUID klass;
    if (key_is(key, &g_interface_enabled)) {
        *handled = TRUE;
        if (!interface_known(path))
            return CR_NO_SUCH_DEVICE_INTERFACE;
        BYTE enabled = 0xff;
        return put(DEVPROP_TYPE_BOOLEAN, &enabled, sizeof(enabled), type, buffer, size);
    }
    if (key_is(key, &g_interface_class)) {
        *handled = TRUE;
        if (!interface_class(path, &klass))
            return CR_NO_SUCH_DEVICE_INTERFACE;
        return put(DEVPROP_TYPE_GUID, &klass, sizeof(klass), type, buffer, size);
    }
    HKEY base;
    if (interface_base(path, FALSE, KEY_READ, &base))
        return CR_SUCCESS;
    CONFIGRET cr = store_get(base, key, type, buffer, size);
    RegCloseKey(base);
    if (cr != CR_NO_SUCH_VALUE)
        *handled = TRUE;
    return cr;
}

/* ------------------------------------------------------------------ _Ex versions */

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Registry_Property_ExW(DEVINST devinst, ULONG property, PULONG type,
                                                            PVOID buffer, PULONG length, ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_DevNode_Registry_PropertyW(devinst, property, type, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Parent_Ex(PDEVINST out, DEVINST devinst, ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Parent(out, devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_List_ExW(GUID *klass, PCWSTR device, PWSTR buffer, ULONG length,
                                                        ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Device_Interface_ListW(klass, device, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_List_Size_ExW(PULONG length, GUID *klass, PCWSTR device, ULONG flags,
                                                             void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Device_Interface_List_SizeW(length, klass, device, flags);
}

/* ------------------------------------------------------------------ notifications */

/*
 * a process thread waits for the tree to change, diffs interfaces and devices against what it saw, and calls the
 * matching callbacks. callbacks never run with the lock held, and removing a registration waits for a running
 * callback unless it is removed from inside that callback.
 */
typedef struct NOTIFICATION {
    struct NOTIFICATION *Next;
    CM_NOTIFY_FILTER Filter;
    PVOID Context;
    PCM_NOTIFY_CALLBACK Callback;
    LONG Running;
    DWORD CallerThread;
    BOOL Removed;
} NOTIFICATION;

static NOTIFICATION *g_notifications;
static RTL_CRITICAL_SECTION g_notify_lock = RTL_CRITICAL_SECTION_INIT;
static BOOL g_watching;

#define WATCH_MAX 256

typedef struct SEEN_INTERFACE {
    GUID Class;
    WCHAR Path[MAX_PATH];
} SEEN_INTERFACE;

typedef struct SEEN_DEVICE {
    WCHAR Id[MAX_DEVICE_ID_LEN];
} SEEN_DEVICE;

static BOOL read_interfaces(SEEN_INTERFACE *list, ULONG *count)
{
    MIME_PNP_REQUEST *r;
    *count = 0;
    if (!NT_SUCCESS(request(MIME_PNP_LIST_INTERFACES, 1, nullptr, nullptr, &r)))
        return FALSE;
    for (PCWSTR p = r->Out; *p && *count < WATCH_MAX; p += lstrlenW(p) + 1) {
        MIME_PNP_REQUEST *q;
        if (lstrlenW(p) >= MAX_PATH || !NT_SUCCESS(request(MIME_PNP_INTERFACE_DEVICE, 0, nullptr, p, &q)))
            continue;
        list[*count].Class = q->Class;
        lstrcpyW(list[*count].Path, p);
        (*count)++;
        HeapFree(GetProcessHeap(), 0, q);
    }
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

static BOOL read_devices(SEEN_DEVICE *list, ULONG *count)
{
    MIME_PNP_REQUEST *r;
    *count = 0;
    if (!NT_SUCCESS(request(MIME_PNP_LIST_DEVICES, 0, nullptr, nullptr, &r)))
        return FALSE;
    for (PCWSTR p = r->Out; *p && *count < WATCH_MAX; p += lstrlenW(p) + 1)
        if (lstrlenW(p) < MAX_DEVICE_ID_LEN)
            lstrcpyW(list[(*count)++].Id, p);
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

static BOOL interface_in(const SEEN_INTERFACE *list, ULONG count, const SEEN_INTERFACE *x)
{
    for (ULONG i = 0; i < count; i++)
        if (!memcmp(&list[i].Class, &x->Class, sizeof(GUID)) && !lstrcmpiW(list[i].Path, x->Path))
            return TRUE;
    return FALSE;
}

static BOOL device_in(const SEEN_DEVICE *list, ULONG count, PCWSTR id)
{
    for (ULONG i = 0; i < count; i++)
        if (!lstrcmpiW(list[i].Id, id))
            return TRUE;
    return FALSE;
}

/* frees a removed registration once no callback runs; the lock is held. */
static BOOL notification_release(NOTIFICATION *n)
{
    if (!n->Removed || n->Running)
        return FALSE;
    NOTIFICATION **at = &g_notifications;
    while (*at && *at != n)
        at = &(*at)->Next;
    if (*at)
        *at = n->Next;
    HeapFree(GetProcessHeap(), 0, n);
    return TRUE;
}

/* each matching callback runs once, without the lock. */
static void deliver(CM_NOTIFY_ACTION action, CM_NOTIFY_FILTER_TYPE type, const GUID *klass, PCWSTR text)
{
    ULONG chars = (ULONG)lstrlenW(text) + 1;
    DWORD size = (DWORD)(type == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE
                             ? FIELD_OFFSET(CM_NOTIFY_EVENT_DATA, u.DeviceInterface.SymbolicLink)
                             : FIELD_OFFSET(CM_NOTIFY_EVENT_DATA, u.DeviceInstance.InstanceId)) +
                 chars * sizeof(WCHAR);
    CM_NOTIFY_EVENT_DATA *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!data)
        return;
    data->FilterType = type;
    if (type == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE) {
        data->u.DeviceInterface.ClassGuid = *klass;
        memcpy(data->u.DeviceInterface.SymbolicLink, text, chars * sizeof(WCHAR));
    } else {
        memcpy(data->u.DeviceInstance.InstanceId, text, chars * sizeof(WCHAR));
    }
    NOTIFICATION *targets[32];
    ULONG count = 0;
    RtlEnterCriticalSection(&g_notify_lock);
    for (NOTIFICATION *r = g_notifications; r && count < RTL_NUMBER_OF(targets); r = r->Next) {
        if (r->Removed || r->Filter.FilterType != type)
            continue;
        BOOL wanted;
        if (type == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE)
            wanted = (r->Filter.Flags & CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES) ||
                     !memcmp(&r->Filter.u.DeviceInterface.ClassGuid, klass, sizeof(GUID));
        else
            wanted = (r->Filter.Flags & CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES) ||
                     !lstrcmpiW(r->Filter.u.DeviceInstance.InstanceId, text);
        if (!wanted)
            continue;
        r->Running++;
        targets[count++] = r;
    }
    RtlLeaveCriticalSection(&g_notify_lock);
    for (ULONG i = 0; i < count; i++) {
        NOTIFICATION *n = targets[i];
        n->CallerThread = GetCurrentThreadId();
        BOOL removed;
        RtlEnterCriticalSection(&g_notify_lock);
        removed = n->Removed;
        RtlLeaveCriticalSection(&g_notify_lock);
        if (!removed)
            n->Callback((HCMNOTIFICATION)n, n->Context, action, data, size);
        RtlEnterCriticalSection(&g_notify_lock);
        n->Running--;
        notification_release(n);
        RtlLeaveCriticalSection(&g_notify_lock);
    }
    HeapFree(GetProcessHeap(), 0, data);
}

static DWORD WINAPI watch_tree(void *param)
{
    SEEN_INTERFACE *seen = HeapAlloc(GetProcessHeap(), 0, sizeof(SEEN_INTERFACE) * WATCH_MAX);
    SEEN_INTERFACE *now = HeapAlloc(GetProcessHeap(), 0, sizeof(SEEN_INTERFACE) * WATCH_MAX);
    SEEN_DEVICE *seen_devices = HeapAlloc(GetProcessHeap(), 0, sizeof(SEEN_DEVICE) * WATCH_MAX);
    SEEN_DEVICE *now_devices = HeapAlloc(GetProcessHeap(), 0, sizeof(SEEN_DEVICE) * WATCH_MAX);
    if (!seen || !now || !seen_devices || !now_devices)
        return 0;
    ULONG seen_count = 0, now_count = 0, seen_devices_count = 0, now_devices_count = 0;
    ULONG generation = 0xFFFFFFFF;
    BOOL known = FALSE;
    for (;;) {
        MIME_PNP_REQUEST *r;
        NTSTATUS status = request(MIME_PNP_WAIT_CHANGE, generation, nullptr, nullptr, &r);
        if (!NT_SUCCESS(status)) {
            if (status == STATUS_ALERTED || status == STATUS_USER_APC)
                continue;
            break;
        }
        generation = r->Flags;
        HeapFree(GetProcessHeap(), 0, r);
        if (!read_interfaces(now, &now_count) || !read_devices(now_devices, &now_devices_count))
            continue;
        if (known) {
            for (ULONG i = 0; i < seen_count; i++)
                if (!interface_in(now, now_count, &seen[i]))
                    deliver(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE,
                            &seen[i].Class, seen[i].Path);
            for (ULONG i = 0; i < seen_devices_count; i++)
                if (!device_in(now_devices, now_devices_count, seen_devices[i].Id))
                    deliver(CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED, CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE, nullptr,
                            seen_devices[i].Id);
            for (ULONG i = 0; i < now_devices_count; i++)
                if (!device_in(seen_devices, seen_devices_count, now_devices[i].Id)) {
                    deliver(CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED, CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE, nullptr,
                            now_devices[i].Id);
                    deliver(CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED, CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE, nullptr,
                            now_devices[i].Id);
                }
            for (ULONG i = 0; i < now_count; i++)
                if (!interface_in(seen, seen_count, &now[i]))
                    deliver(CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE,
                            &now[i].Class, now[i].Path);
        }
        memcpy(seen, now, now_count * sizeof(SEEN_INTERFACE));
        seen_count = now_count;
        memcpy(seen_devices, now_devices, now_devices_count * sizeof(SEEN_DEVICE));
        seen_devices_count = now_devices_count;
        known = TRUE;
    }
    return 0;
}

/*
 * device handle filters are accepted but never called: devices here cannot be queried for removal.
 */
CMAPI CONFIGRET WINAPI CM_Register_Notification(PCM_NOTIFY_FILTER filter, PVOID context, PCM_NOTIFY_CALLBACK callback,
                                                PHCMNOTIFICATION notify)
{
    if (!filter || !callback || !notify)
        return CR_INVALID_POINTER;
    if (filter->cbSize != sizeof(CM_NOTIFY_FILTER))
        return CR_INVALID_STRUCTURE_SIZE;
    if ((ULONG)filter->FilterType >= CM_NOTIFY_FILTER_TYPE_MAX)
        return CR_INVALID_DATA;
    if (filter->Reserved)
        return CR_INVALID_DATA;
    ULONG all = filter->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE ? CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES
                : filter->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE ? CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES
                                                                              : 0;
    if (filter->Flags & ~all)
        return CR_INVALID_FLAG;
    *notify = nullptr;
    if (filter->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE) {
        DWORD flags;
        if (!filter->u.DeviceHandle.hTarget || !GetHandleInformation(filter->u.DeviceHandle.hTarget, &flags))
            return CR_INVALID_DATA;
    }
    if (filter->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE && !(filter->Flags & all)) {
        if (!filter->u.DeviceInstance.InstanceId[0])
            return CR_INVALID_DATA;
    }
    NOTIFICATION *n = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*n));
    if (!n)
        return CR_OUT_OF_MEMORY;
    n->Filter = *filter;
    n->Context = context;
    n->Callback = callback;
    RtlEnterCriticalSection(&g_notify_lock);
    n->Next = g_notifications;
    g_notifications = n;
    BOOL start = !g_watching;
    g_watching = TRUE;
    RtlLeaveCriticalSection(&g_notify_lock);
    if (start) {
        HANDLE thread = CreateThread(nullptr, 0, watch_tree, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    *notify = (HCMNOTIFICATION)n;
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Unregister_Notification(HCMNOTIFICATION notify)
{
    NOTIFICATION *n = notify;
    RtlEnterCriticalSection(&g_notify_lock);
    NOTIFICATION *at = g_notifications;
    while (at && at != n)
        at = at->Next;
    if (!at || n->Removed) {
        RtlLeaveCriticalSection(&g_notify_lock);
        return CR_INVALID_DATA;
    }
    n->Removed = TRUE;
    /* wait for a running callback outside the lock, unless we are that callback: it frees on return. */
    DWORD self = GetCurrentThreadId();
    while (n->Running && n->CallerThread != self) {
        RtlLeaveCriticalSection(&g_notify_lock);
        Sleep(1);
        RtlEnterCriticalSection(&g_notify_lock);
    }
    notification_release(n);
    RtlLeaveCriticalSection(&g_notify_lock);
    return CR_SUCCESS;
}

/* ------------------------------------------------------------------ logical configurations */

/*
 * no device of this machine holds a hardware resource: none has an interrupt line, a port or a memory range of its
 * own. so a device has no configuration of any kind, and no configuration or resource handle is ever handed out.
 */
CMAPI CONFIGRET WINAPI CM_Get_First_Log_Conf(PLOG_CONF out, DEVINST devinst, ULONG flags)
{
    if (flags & ~LOG_CONF_BITS)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (!is_root(id) && !present(id))
        return CR_NO_SUCH_DEVNODE;
    return CR_NO_MORE_LOG_CONF;
}

CMAPI CONFIGRET WINAPI CM_Get_First_Log_Conf_Ex(PLOG_CONF out, DEVINST devinst, ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_First_Log_Conf(out, devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Next_Log_Conf(PLOG_CONF out, LOG_CONF conf, ULONG flags)
{
    return flags ? CR_INVALID_FLAG : CR_INVALID_LOG_CONF;
}

CMAPI CONFIGRET WINAPI CM_Get_Next_Log_Conf_Ex(PLOG_CONF out, LOG_CONF conf, ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Next_Log_Conf(out, conf, flags);
}

CMAPI CONFIGRET WINAPI CM_Free_Log_Conf_Handle(LOG_CONF conf)
{
    return CR_INVALID_LOG_CONF;
}

CMAPI CONFIGRET WINAPI CM_Get_Next_Res_Des(PRES_DES out, RES_DES from, RESOURCEID wanted, PRESOURCEID found, ULONG flags)
{
    if (!out)
        return CR_INVALID_POINTER;
    return flags ? CR_INVALID_FLAG : CR_INVALID_RES_DES;
}

CMAPI CONFIGRET WINAPI CM_Get_Next_Res_Des_Ex(PRES_DES out, RES_DES from, RESOURCEID wanted, PRESOURCEID found,
                                              ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Next_Res_Des(out, from, wanted, found, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Res_Des_Data_Size(PULONG size, RES_DES des, ULONG flags)
{
    if (!size)
        return CR_INVALID_POINTER;
    *size = 0;
    return flags ? CR_INVALID_FLAG : CR_INVALID_RES_DES;
}

CMAPI CONFIGRET WINAPI CM_Get_Res_Des_Data_Size_Ex(PULONG size, RES_DES des, ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Res_Des_Data_Size(size, des, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Res_Des_Data(RES_DES des, PVOID buffer, ULONG length, ULONG flags)
{
    if (!buffer)
        return CR_INVALID_POINTER;
    return flags ? CR_INVALID_FLAG : CR_INVALID_RES_DES;
}

CMAPI CONFIGRET WINAPI CM_Get_Res_Des_Data_Ex(RES_DES des, PVOID buffer, ULONG length, ULONG flags, void *machine)
{
    if (machine)
        return CR_INVALID_MACHINENAME;
    return CM_Get_Res_Des_Data(des, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Free_Res_Des_Handle(RES_DES des)
{
    return CR_INVALID_RES_DES;
}
