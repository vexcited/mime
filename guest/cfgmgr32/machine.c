/*
 * machine handles and the calls that take one, and the devices and interfaces a program makes itself, which are keys
 * of the registry until a driver starts them.
 *
 * measured on windows 11 26100: CM_Connect_MachineW gives a handle for no name, an empty name and the computer's
 * name with or without the leading backslashes, and CR_CALL_NOT_IMPLEMENTED for any other name, localhost and
 * addresses included. a call with such a handle or with none is the plain call.
 */
#define MIME_BUILD_CFGMGR32
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/pnp.h>
#include <mime/win32/cfgmgr32.h>
#include "cfgmgr32_private.h"

#define CLASS_PATH L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
#define INTERFACE_CLASS_PATH L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\"
#define PROFILE_PATH L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\"

#define CM_CREATE_DEVNODE_BITS 0x0000000F
#define CM_CREATE_DEVNODE_PHANTOM 0x00000002
#define CM_CREATE_DEVNODE_GENERATE_ID 0x00000004
#define CM_SET_DEVNODE_PROBLEM_BITS 0x00000001
#define CSCONFIGFLAG_BITS 0x00000007
#define MACHINE_SIGNATURE 0x4863614D

typedef struct MACHINE {
    ULONG Signature;
} MACHINE;

BOOL machine_local(void *machine)
{
    return !machine || ((MACHINE *)machine)->Signature == MACHINE_SIGNATURE;
}

CMAPI CONFIGRET WINAPI CM_Connect_MachineW(PCWSTR name, void **machine)
{
    if (!machine)
        return CR_INVALID_POINTER;
    *machine = nullptr;
    if (name && name[0]) {
        WCHAR local[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD chars = RTL_NUMBER_OF(local);
        PCWSTR bare = name[0] == L'\\' && name[1] == L'\\' ? name + 2 : name;
        if (!GetComputerNameW(local, &chars) || lstrcmpiW(bare, local))
            return CR_CALL_NOT_IMPLEMENTED;
    }
    MACHINE *m = HeapAlloc(GetProcessHeap(), 0, sizeof(*m));
    if (!m)
        return CR_OUT_OF_MEMORY;
    m->Signature = MACHINE_SIGNATURE;
    *machine = m;
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Disconnect_Machine(void *machine)
{
    if (!machine || ((MACHINE *)machine)->Signature != MACHINE_SIGNATURE)
        return CR_INVALID_POINTER;
    ((MACHINE *)machine)->Signature = 0;
    HeapFree(GetProcessHeap(), 0, machine);
    return CR_SUCCESS;
}

/* ------------------------------------------------------------------ class names */

/* the Class value of the installer class. a class without a key, or without the value, has no name. */
CMAPI CONFIGRET WINAPI CM_Get_Class_NameW(GUID *klass, PWSTR buffer, PULONG length, ULONG flags)
{
    if (!klass || !buffer || !length)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    WCHAR path[128] = CLASS_PATH, name[MAX_CLASS_NAME_LEN + 1];
    format_guid(klass, path + lstrlenW(path));
    DWORD bytes = sizeof(name);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, L"Class", RRF_RT_REG_SZ, nullptr, name, &bytes))
        return CR_NO_SUCH_REGISTRY_KEY;
    ULONG chars = (ULONG)lstrlenW(name) + 1, room = *length;
    *length = chars;
    if (room < chars) {
        if (room)
            buffer[0] = 0;
        return CR_BUFFER_SMALL;
    }
    memcpy(buffer, name, chars * sizeof(WCHAR));
    return CR_SUCCESS;
}

/* ------------------------------------------------------------------ hardware profile flags */

/* enumerator\device\instance, three parts that are not empty. */
static BOOL id_valid(PCWSTR id)
{
    if (!id || lstrlenW(id) >= MAX_DEVICE_ID_LEN)
        return FALSE;
    int parts = 1, run = 0;
    for (PCWSTR p = id; *p; p++) {
        if (*p == L'\\') {
            if (!run)
                return FALSE;
            parts++;
            run = 0;
        } else if (*p < L' ' || *p == L'/') {
            return FALSE;
        } else {
            run++;
        }
    }
    return parts == 3 && run;
}

/* the profile 0 is the current one, which is the only one a machine has since windows 8: 0001. */
static void profile_path(PCWSTR id, ULONG profile, WCHAR *path)
{
    lstrcpyW(path, PROFILE_PATH);
    WCHAR *p = path + lstrlenW(path);
    if (!profile)
        profile = 1;
    for (int i = 3; i >= 0; i--)
        *p++ = (WCHAR)(L'0' + profile / (i == 3 ? 1000 : i == 2 ? 100 : i == 1 ? 10 : 1) % 10);
    *p = 0;
    lstrcatW(path, L"\\System\\CurrentControlSet\\Enum\\");
    lstrcatW(path, id);
}

/* the CSConfigFlags value of the device in the profile. a device or a profile that has none answers 0. */
CMAPI CONFIGRET WINAPI CM_Get_HW_Prof_FlagsW(PWSTR id, ULONG profile, PULONG value, ULONG flags)
{
    if (!id || !value)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    if (!id_valid(id))
        return CR_INVALID_DEVICE_ID;
    WCHAR path[MAX_DEVICE_ID_LEN + 128];
    profile_path(id, profile, path);
    DWORD data = 0, bytes = sizeof(data);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, L"CSConfigFlags", RRF_RT_REG_DWORD, nullptr, &data, &bytes))
        data = 0;
    *value = data;
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Set_HW_Prof_FlagsW(PWSTR id, ULONG profile, ULONG value, ULONG flags)
{
    if (!id)
        return CR_INVALID_POINTER;
    if (flags & ~1u)
        return CR_INVALID_FLAG;
    if (!id_valid(id))
        return CR_INVALID_DEVICE_ID;
    if (value & ~CSCONFIGFLAG_BITS)
        return CR_INVALID_DATA;
    WCHAR path[MAX_DEVICE_ID_LEN + 128];
    profile_path(id, profile, path);
    HKEY key;
    LSTATUS error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error)
        return from_registry(error);
    error = RegSetValueExW(key, L"CSConfigFlags", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return from_registry(error);
}

/* ------------------------------------------------------------------ devices of the registry */

/*
 * a phantom device is its key under Enum with the value Phantom, as windows leaves it: CM_Locate_DevNode does not
 * find it under any flag, and the handle this call gives is the way to it. a device that is live comes from a driver
 * of the kernel, which a program cannot make here: that needs the privilege to load one, as on windows.
 */
CMAPI CONFIGRET WINAPI CM_Create_DevNodeW(PDEVINST devinst, PWSTR id, DEVINST parent, ULONG flags)
{
    if (!devinst)
        return CR_INVALID_POINTER;
    WCHAR generated[MAX_DEVICE_ID_LEN];
    if (id && (flags & CM_CREATE_DEVNODE_GENERATE_ID) && !(flags & ~CM_CREATE_DEVNODE_BITS)) {
        /* read, the header of the call: the id is the name of a device under the root, the first free instance of it. */
        if (!id[0] || lstrlenW(id) > MAX_DEVICE_ID_LEN - 16)
            return CR_INVALID_DEVICE_ID;
        for (PCWSTR p = id; *p; p++)
            if (*p == L'\\')
                return CR_INVALID_DEVICE_ID;
        for (ULONG n = 0; n < 10000; n++) {
            _snwprintf(generated, RTL_NUMBER_OF(generated), L"ROOT\\%ls\\%04u", id, n);
            HKEY taken = present(generated) ? nullptr : open_enum(generated, KEY_READ);
            if (!taken && !present(generated))
                break;
            if (taken)
                RegCloseKey(taken);
        }
        id = generated;
    }
    if (!id_valid(id))
        return CR_INVALID_DEVICE_ID;
    if (flags & ~CM_CREATE_DEVNODE_BITS)
        return CR_INVALID_FLAG;
    if (!id_of(parent))
        return CR_INVALID_DEVNODE;
    if (present(id))
        return CR_ALREADY_SUCH_DEVNODE;
    HKEY key = open_enum(id, KEY_READ);
    if (key) {
        RegCloseKey(key);
        return CR_ALREADY_SUCH_DEVNODE;
    }
    if (!(flags & CM_CREATE_DEVNODE_PHANTOM))
        return CR_ACCESS_DENIED;
    /* the keys under Enum are the system's: the kernel's plug and play manager makes this one, for an administrator. */
    MIME_PNP_REQUEST *r;
    NTSTATUS status = request(MIME_PNP_CREATE_PHANTOM, 0, nullptr, id, &r);
    if (!NT_SUCCESS(status))
        return status == STATUS_ACCESS_DENIED ? CR_ACCESS_DENIED
               : status == STATUS_OBJECT_NAME_COLLISION ? CR_ALREADY_SUCH_DEVNODE : CR_FAILURE;
    HeapFree(GetProcessHeap(), 0, r);
    *devinst = node_of(id);
    return *devinst ? CR_SUCCESS : CR_OUT_OF_MEMORY;
}

/* the problem of a live device is the kernel's to set. measured on windows 11: the root is refused. */
CMAPI CONFIGRET WINAPI CM_Set_DevNode_Problem(DEVINST devinst, ULONG problem, ULONG flags)
{
    if (flags & ~CM_SET_DEVNODE_PROBLEM_BITS)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (!present(id))
        return CR_NO_SUCH_DEVNODE;
    return CR_ACCESS_DENIED;
}

/* ------------------------------------------------------------------ interfaces of the registry */

/* the link of a device in a class: \\?\ and the id with its separators as #, then the class and the reference. */
static void make_link(PCWSTR id, const GUID *klass, PCWSTR reference, WCHAR *link)
{
    lstrcpyW(link, L"\\\\?\\");
    WCHAR *p = link + 4;
    for (; *id; id++)
        *p++ = *id == L'\\' ? L'#' : *id;
    *p++ = L'#';
    format_guid(klass, p);
    for (; *p; p++)
        if (*p >= L'A' && *p <= L'F')
            *p = (WCHAR)(*p - L'A' + L'a');
    if (reference && reference[0]) {
        *p++ = L'\\';
        lstrcpyW(p, reference);
    }
}

/* the two keys of a link under DeviceClasses: the device's, and under it # and the reference. */
static BOOL link_keys(PCWSTR link, WCHAR *device_key, WCHAR *reference_key)
{
    PCWSTR brace = nullptr;
    for (PCWSTR p = link; *p; p++)
        if (*p == L'{')
            brace = p;
    GUID klass;
    if (!brace || lstrlenW(link) < 5 || link[0] != L'\\' || lstrlenW(link) > MAX_PATH || !parse_guid(brace, &klass))
        return FALSE;
    lstrcpyW(device_key, INTERFACE_CLASS_PATH);
    WCHAR *p = device_key + lstrlenW(device_key);
    memcpy(p, brace, (MAX_GUID_STRING_LEN - 1) * sizeof(WCHAR));
    p += MAX_GUID_STRING_LEN - 1;
    lstrcpyW(p, L"\\##?#");
    p += 5;
    for (PCWSTR q = link + 4; q < brace + MAX_GUID_STRING_LEN - 1; q++)
        *p++ = *q;
    *p = 0;
    lstrcpyW(reference_key, device_key);
    lstrcatW(reference_key, L"\\#");
    PCWSTR reference = brace + MAX_GUID_STRING_LEN - 1;
    if (*reference == L'\\')
        lstrcatW(reference_key, reference + 1);
    else if (*reference)
        return FALSE;
    return TRUE;
}

/*
 * registers an interface of a device in a class: the keys windows' plug and play manager writes, DeviceInstance on the
 * device's key and SymbolicLink on the key of the reference. the interface is not enabled, which is a driver's doing.
 */
CMAPI CONFIGRET WINAPI CM_Register_Device_InterfaceW(DEVINST devinst, GUID *klass, PCWSTR reference, PWSTR link,
                                                     PULONG length, ULONG flags)
{
    if (!klass || !link || !length)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (reference && lstrlenW(reference) > 64)
        return CR_INVALID_REFERENCE_STRING;
    for (PCWSTR p = reference; p && *p; p++)
        if (*p == L'\\' || *p == L'/')
            return CR_INVALID_REFERENCE_STRING;
    WCHAR made[MAX_PATH + 128], device_key[MAX_PATH + 256], reference_key[MAX_PATH + 256];
    make_link(id, klass, reference, made);
    ULONG chars = (ULONG)lstrlenW(made) + 1, room = *length;
    *length = chars;
    if (room < chars)
        return CR_BUFFER_SMALL;
    if (!link_keys(made, device_key, reference_key))
        return CR_FAILURE;
    HKEY key;
    LSTATUS error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, device_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error)
        return from_registry(error);
    RegSetValueExW(key, L"DeviceInstance", 0, REG_SZ, (const BYTE *)id, (DWORD)((lstrlenW(id) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, reference_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error)
        return from_registry(error);
    RegSetValueExW(key, L"SymbolicLink", 0, REG_SZ, (const BYTE *)made, chars * sizeof(WCHAR));
    RegCloseKey(key);
    memcpy(link, made, chars * sizeof(WCHAR));
    return CR_SUCCESS;
}

static BOOL key_empty(PCWSTR path)
{
    HKEY key;
    DWORD subkeys = 1;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return FALSE;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return !subkeys;
}

/* removes the registration: the key of the reference, then the device's and the class's when nothing is left in them. */
CMAPI CONFIGRET WINAPI CM_Unregister_Device_InterfaceW(PCWSTR link, ULONG flags)
{
    if (!link)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    WCHAR device_key[MAX_PATH + 256], reference_key[MAX_PATH + 256];
    BOOL enabled;
    if (!link_keys(link, device_key, reference_key) || !interface_owner(link, nullptr, nullptr, &enabled))
        return CR_NO_SUCH_DEVICE_INTERFACE;
    /* an interface a driver holds enabled stays. */
    if (enabled)
        return CR_ACCESS_DENIED;
    LSTATUS error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, reference_key);
    if (error)
        return error == ERROR_FILE_NOT_FOUND ? CR_NO_SUCH_DEVICE_INTERFACE : from_registry(error);
    if (key_empty(device_key) && RegDeleteKeyExW(HKEY_LOCAL_MACHINE, device_key, 0, 0) == ERROR_SUCCESS) {
        WCHAR *cut = device_key + lstrlenW(INTERFACE_CLASS_PATH) + MAX_GUID_STRING_LEN - 1;
        *cut = 0;
        if (key_empty(device_key))
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, device_key, 0, 0);
    }
    return CR_SUCCESS;
}

/* the links of the registry for a class that no driver has enabled, of one device or of all. */
WCHAR *registered_interfaces(const GUID *klass, PCWSTR device, ULONG *chars)
{
    WCHAR path[128] = INTERFACE_CLASS_PATH;
    format_guid(klass, path + lstrlenW(path));
    ULONG room = 1024, used = 0;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, room * sizeof(WCHAR));
    HKEY root;
    if (list && !RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &root)) {
        for (DWORD i = 0;; i++) {
            WCHAR name[MAX_PATH + 64], instance[MAX_DEVICE_ID_LEN];
            DWORD n = RTL_NUMBER_OF(name);
            if (RegEnumKeyExW(root, i, name, &n, nullptr, nullptr, nullptr, nullptr))
                break;
            DWORD bytes = sizeof(instance);
            HKEY entry;
            if (RegGetValueW(root, name, L"DeviceInstance", RRF_RT_REG_SZ, nullptr, instance, &bytes) ||
                (device && device[0] && lstrcmpiW(device, instance)) || RegOpenKeyExW(root, name, 0, KEY_READ, &entry))
                continue;
            for (DWORD j = 0;; j++) {
                WCHAR reference[128], link[MAX_PATH + 128];
                n = RTL_NUMBER_OF(reference);
                if (RegEnumKeyExW(entry, j, reference, &n, nullptr, nullptr, nullptr, nullptr))
                    break;
                bytes = sizeof(link);
                BOOL enabled;
                if (reference[0] != L'#' || RegGetValueW(entry, reference, L"SymbolicLink", RRF_RT_REG_SZ, nullptr, link, &bytes) ||
                    !interface_owner(link, nullptr, nullptr, &enabled) || enabled)
                    continue;
                ULONG length = (ULONG)lstrlenW(link) + 1;
                if (used + length + 1 > room) {
                    room = (used + length + 1) * 2;
                    WCHAR *grown = HeapReAlloc(GetProcessHeap(), 0, list, room * sizeof(WCHAR));
                    if (!grown)
                        break;
                    list = grown;
                }
                memcpy(list + used, link, length * sizeof(WCHAR));
                used += length;
            }
            RegCloseKey(entry);
        }
        RegCloseKey(root);
    }
    if (list)
        list[used] = 0;
    *chars = used;
    return list;
}

/* ------------------------------------------------------------------ the calls with a machine handle */

#define LOCAL(machine)               \
    if (!machine_local(machine))     \
        return CR_INVALID_MACHINENAME

CMAPI CONFIGRET WINAPI CM_Create_DevNode_ExW(PDEVINST devinst, PWSTR id, DEVINST parent, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Create_DevNodeW(devinst, id, parent, flags);
}

CMAPI CONFIGRET WINAPI CM_Delete_DevNode_Key_Ex(DEVINST devinst, ULONG profile, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Delete_DevNode_Key(devinst, profile, flags);
}

CMAPI CONFIGRET WINAPI CM_Delete_Device_Interface_Key_ExW(PCWSTR path, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Delete_Device_Interface_KeyW(path, flags);
}

CMAPI CONFIGRET WINAPI CM_Enable_DevNode_Ex(DEVINST devinst, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Enable_DevNode(devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Enumerate_Classes_Ex(ULONG index, GUID *guid, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Enumerate_Classes(index, guid, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Class_Name_ExW(GUID *klass, PWSTR buffer, PULONG length, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Class_NameW(klass, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Class_Property_ExW(const GUID *klass, const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer,
                                                 PULONG size, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Class_PropertyW(klass, key, type, buffer, size, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Class_Property_Keys_Ex(const GUID *klass, DEVPROPKEY *keys, PULONG count, ULONG flags,
                                                     void *machine)
{
    LOCAL(machine);
    return CM_Get_Class_Property_Keys(klass, keys, count, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Property_ExW(DEVINST devinst, const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer,
                                                   PULONG size, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_DevNode_PropertyW(devinst, key, type, buffer, size, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Property_Keys_Ex(DEVINST devinst, DEVPROPKEY *keys, PULONG count, ULONG flags,
                                                       void *machine)
{
    LOCAL(machine);
    return CM_Get_DevNode_Property_Keys(devinst, keys, count, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Status_Ex(PULONG status, PULONG problem, DEVINST devinst, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_DevNode_Status(status, problem, devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_ExW(DEVINST devinst, PWSTR buffer, ULONG length, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_IDW(devinst, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_ExA(DEVINST devinst, PSTR buffer, ULONG length, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_IDA(devinst, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_List_ExW(PCWSTR filter, PWCHAR buffer, ULONG length, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_ID_ListW(filter, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_List_Size_ExW(PULONG length, PCWSTR filter, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_ID_List_SizeW(length, filter, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_Size_Ex(PULONG length, DEVINST devinst, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_ID_Size(length, devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_Alias_ExW(PCWSTR path, GUID *alias_class, PWSTR alias, PULONG length,
                                                         ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_Interface_AliasW(path, alias_class, alias, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_Property_ExW(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                            PBYTE buffer, PULONG size, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_Interface_PropertyW(path, key, type, buffer, size, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_Property_Keys_ExW(PCWSTR path, DEVPROPKEY *keys, PULONG count, ULONG flags,
                                                                 void *machine)
{
    LOCAL(machine);
    return CM_Get_Device_Interface_Property_KeysW(path, keys, count, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_HW_Prof_Flags_ExW(PWSTR id, ULONG profile, PULONG value, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Get_HW_Prof_FlagsW(id, profile, value, flags);
}

CMAPI CONFIGRET WINAPI CM_Locate_DevNode_ExW(PDEVINST devinst, PCWSTR id, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Locate_DevNodeW(devinst, id, flags);
}

CMAPI CONFIGRET WINAPI CM_Open_Class_Key_ExW(const GUID *klass, PCWSTR name, REGSAM access, DWORD disposition, HKEY *key,
                                             ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Open_Class_KeyW(klass, name, access, disposition, key, flags);
}

CMAPI CONFIGRET WINAPI CM_Open_DevNode_Key_Ex(DEVINST devinst, REGSAM access, ULONG profile, DWORD disposition, HKEY *key,
                                              ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Open_DevNode_Key(devinst, access, profile, disposition, key, flags);
}

CMAPI CONFIGRET WINAPI CM_Open_Device_Interface_Key_ExW(PCWSTR path, REGSAM access, DWORD disposition, HKEY *key,
                                                        ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Open_Device_Interface_KeyW(path, access, disposition, key, flags);
}

CMAPI CONFIGRET WINAPI CM_Query_And_Remove_SubTree_ExW(DEVINST devinst, PULONG veto_type, PWSTR veto_name, ULONG length,
                                                       ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Query_And_Remove_SubTreeW(devinst, veto_type, veto_name, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Register_Device_Interface_ExW(DEVINST devinst, GUID *klass, PCWSTR reference, PWSTR link,
                                                        PULONG length, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Register_Device_InterfaceW(devinst, klass, reference, link, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Set_Class_Property_ExW(const GUID *klass, const DEVPROPKEY *key, DEVPROPTYPE type, PBYTE buffer,
                                                 ULONG size, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Set_Class_PropertyW(klass, key, type, buffer, size, flags);
}

CMAPI CONFIGRET WINAPI CM_Set_DevNode_Problem_Ex(DEVINST devinst, ULONG problem, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Set_DevNode_Problem(devinst, problem, flags);
}

CMAPI CONFIGRET WINAPI CM_Set_DevNode_Property_ExW(DEVINST devinst, const DEVPROPKEY *key, DEVPROPTYPE type, PBYTE buffer,
                                                   ULONG size, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Set_DevNode_PropertyW(devinst, key, type, buffer, size, flags);
}

CMAPI CONFIGRET WINAPI CM_Set_DevNode_Registry_Property_ExW(DEVINST devinst, ULONG property, PVOID buffer, ULONG length,
                                                            ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Set_DevNode_Registry_PropertyW(devinst, property, buffer, length, flags);
}

CMAPI CONFIGRET WINAPI CM_Set_Device_Interface_Property_ExW(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE type,
                                                            PBYTE buffer, ULONG size, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Set_Device_Interface_PropertyW(path, key, type, buffer, size, flags);
}

CMAPI CONFIGRET WINAPI CM_Set_HW_Prof_Flags_ExW(PWSTR id, ULONG profile, ULONG value, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Set_HW_Prof_FlagsW(id, profile, value, flags);
}

CMAPI CONFIGRET WINAPI CM_Setup_DevNode_Ex(DEVINST devinst, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Setup_DevNode(devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Uninstall_DevNode_Ex(DEVINST devinst, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Uninstall_DevNode(devinst, flags);
}

CMAPI CONFIGRET WINAPI CM_Unregister_Device_Interface_ExW(PCWSTR link, ULONG flags, void *machine)
{
    LOCAL(machine);
    return CM_Unregister_Device_InterfaceW(link, flags);
}
