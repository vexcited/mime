/*
 * cfgmgr32: the kernel's device tree read through NtPlugPlayControl, and device properties from the registry Enum
 * keys. a DEVINST is per process: an index into the instance ids seen so far, the root first.
 */
#define MIME_BUILD_CFGMGR32
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/pnp.h>
#include <mime/win32/cfgmgr32.h>
#include "cfgmgr32_private.h"

#define MAX_NODES 1024

static WCHAR *g_nodes[MAX_NODES];
static DWORD g_count;
static RTL_CRITICAL_SECTION g_lock;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        RtlInitializeCriticalSection(&g_lock);
    return TRUE;
}

/* assigns a handle the first time an id is seen. */
DEVINST node_of(PCWSTR id)
{
    RtlEnterCriticalSection(&g_lock);
    DEVINST found = 0;
    for (DWORD i = 0; i < g_count && !found; i++)
        if (!lstrcmpiW(g_nodes[i], id))
            found = i + 1;
    if (!found && g_count < MAX_NODES) {
        SIZE_T bytes = (lstrlenW(id) + 1) * sizeof(WCHAR);
        WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (copy) {
            memcpy(copy, id, bytes);
            g_nodes[g_count++] = copy;
            found = g_count;
        }
    }
    RtlLeaveCriticalSection(&g_lock);
    return found;
}

PCWSTR id_of(DEVINST devinst)
{
    if (!g_count)
        node_of(ROOT_ID);
    RtlEnterCriticalSection(&g_lock);
    PCWSTR id = devinst && devinst <= g_count ? g_nodes[devinst - 1] : nullptr;
    RtlLeaveCriticalSection(&g_lock);
    return id;
}

BOOL is_root(PCWSTR id)
{
    return !lstrcmpiW(id, ROOT_ID);
}

/* the caller frees *out. */
NTSTATUS request(MIME_PNP_CLASS klass, ULONG flags, const GUID *guid, PCWSTR in, MIME_PNP_REQUEST **out)
{
    ULONG chars = 1024;
    for (;;) {
        ULONG size = (ULONG)sizeof(MIME_PNP_REQUEST) + chars * sizeof(WCHAR);
        MIME_PNP_REQUEST *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (!r)
            return STATUS_NO_MEMORY;
        r->Flags = flags;
        if (guid)
            r->Class = *guid;
        if (in)
            lstrcpynW(r->Instance, in, RTL_NUMBER_OF(r->Instance));
        NTSTATUS status = NtPlugPlayControl(klass, r, size);
        if (status == STATUS_BUFFER_TOO_SMALL && r->OutChars > chars) {
            chars = r->OutChars;
            HeapFree(GetProcessHeap(), 0, r);
            continue;
        }
        if (!NT_SUCCESS(status)) {
            HeapFree(GetProcessHeap(), 0, r);
            return status;
        }
        *out = r;
        return status;
    }
}

BOOL present(PCWSTR id)
{
    if (is_root(id))
        return TRUE;
    MIME_PNP_REQUEST *r;
    if (!NT_SUCCESS(request(MIME_PNP_STATUS, 0, nullptr, id, &r)))
        return FALSE;
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

HKEY open_enum(PCWSTR id, REGSAM access)
{
    WCHAR path[MAX_DEVICE_ID_LEN + 64] = L"SYSTEM\\CurrentControlSet\\Enum\\";
    lstrcatW(path, id);
    HKEY key;
    return RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, &key) ? nullptr : key;
}

/* ------------------------------------------------------------------ device nodes */

CMAPI CONFIGRET WINAPI CM_Locate_DevNodeW(PDEVINST devinst, PCWSTR id, ULONG flags)
{
    if (!devinst)
        return CR_INVALID_POINTER;
    if (flags & ~CM_LOCATE_DEVNODE_BITS)
        return CR_INVALID_FLAG;
    *devinst = 0;
    if (!id || !id[0])
        id = ROOT_ID;
    if (lstrlenW(id) >= MAX_DEVICE_ID_LEN)
        return CR_INVALID_DEVICE_ID;
    if (!(flags & CM_LOCATE_DEVNODE_PHANTOM) && !present(id))
        return CR_NO_SUCH_DEVNODE;
    if (flags & CM_LOCATE_DEVNODE_PHANTOM) {
        HKEY key = is_root(id) ? nullptr : open_enum(id, KEY_READ);
        if (!key && !is_root(id))
            return CR_NO_SUCH_DEVNODE;
        if (key)
            RegCloseKey(key);
    }
    *devinst = node_of(id);
    return *devinst ? CR_SUCCESS : CR_OUT_OF_MEMORY;
}

CMAPI CONFIGRET WINAPI CM_Locate_DevNodeA(PDEVINST devinst, PCSTR id, ULONG flags)
{
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (id)
        MultiByteToWideChar(CP_ACP, 0, id, -1, w, MAX_DEVICE_ID_LEN);
    return CM_Locate_DevNodeW(devinst, id ? w : nullptr, flags);
}

static CONFIGRET related(PDEVINST out, DEVINST devinst, ULONG relation)
{
    if (!out)
        return CR_INVALID_POINTER;
    *out = 0;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    MIME_PNP_REQUEST *r;
    if (is_root(id)) {
        if (relation != MIME_PNP_CHILD)
            return CR_NO_SUCH_DEVNODE;
        /* the root's first child is the first device without a parent. */
        if (!NT_SUCCESS(request(MIME_PNP_LIST_DEVICES, 0, nullptr, nullptr, &r)))
            return CR_NO_SUCH_DEVNODE;
        CONFIGRET cr = CR_NO_SUCH_DEVNODE;
        for (PCWSTR p = r->Out; *p && cr; p += lstrlenW(p) + 1) {
            MIME_PNP_REQUEST *parent;
            if (NT_SUCCESS(request(MIME_PNP_RELATED, MIME_PNP_PARENT, nullptr, p, &parent))) {
                HeapFree(GetProcessHeap(), 0, parent);
                continue;
            }
            *out = node_of(p);
            cr = CR_SUCCESS;
        }
        HeapFree(GetProcessHeap(), 0, r);
        return cr;
    }
    NTSTATUS status = request(MIME_PNP_RELATED, relation, nullptr, id, &r);
    if (!NT_SUCCESS(status)) {
        if (relation == MIME_PNP_PARENT && present(id)) {
            *out = node_of(ROOT_ID);
            return CR_SUCCESS;
        }
        return CR_NO_SUCH_DEVNODE;
    }
    *out = node_of(r->Out);
    HeapFree(GetProcessHeap(), 0, r);
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Get_Parent(PDEVINST out, DEVINST devinst, ULONG flags)
{
    return flags ? CR_INVALID_FLAG : related(out, devinst, MIME_PNP_PARENT);
}

CMAPI CONFIGRET WINAPI CM_Get_Child(PDEVINST out, DEVINST devinst, ULONG flags)
{
    return flags ? CR_INVALID_FLAG : related(out, devinst, MIME_PNP_CHILD);
}

CMAPI CONFIGRET WINAPI CM_Get_Sibling(PDEVINST out, DEVINST devinst, ULONG flags)
{
    return flags ? CR_INVALID_FLAG : related(out, devinst, MIME_PNP_SIBLING);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_Size(PULONG length, DEVINST devinst, ULONG flags)
{
    if (!length)
        return CR_INVALID_POINTER;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    *length = (ULONG)lstrlenW(id);
    return CR_SUCCESS;
}

/* truncates with CR_BUFFER_SMALL, and terminates only when there is room. */
CMAPI CONFIGRET WINAPI CM_Get_Device_IDW(DEVINST devinst, PWSTR buffer, ULONG length, ULONG flags)
{
    if (!buffer)
        return CR_INVALID_POINTER;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    ULONG n = (ULONG)lstrlenW(id);
    memcpy(buffer, id, (n < length ? n : length) * sizeof(WCHAR));
    if (n < length)
        buffer[n] = 0;
    return n <= length ? CR_SUCCESS : CR_BUFFER_SMALL;
}

CMAPI CONFIGRET WINAPI CM_Get_Device_IDA(DEVINST devinst, PSTR buffer, ULONG length, ULONG flags)
{
    WCHAR w[MAX_DEVICE_ID_LEN + 1];
    CONFIGRET cr = CM_Get_Device_IDW(devinst, w, RTL_NUMBER_OF(w), flags);
    if (cr)
        return cr;
    int need = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if ((ULONG)need - 1 > length)
        return CR_BUFFER_SMALL;
    WideCharToMultiByte(CP_ACP, 0, w, -1, buffer, (int)length, nullptr, nullptr);
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DEVINST devinst, ULONG flags)
{
    if (!status || !problem)
        return CR_INVALID_POINTER;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (!present(id))
        return CR_NO_SUCH_DEVINST;
    /* every device belongs to a mime driver and cannot be disabled. */
    *status = DN_DRIVER_LOADED | DN_STARTED | DN_NT_ENUMERATOR | DN_NT_DRIVER;
    *problem = 0;
    return CR_SUCCESS;
}

/* ------------------------------------------------------------------ registry properties */

static PCWSTR value_name(ULONG property)
{
    switch (property) {
    case CM_DRP_DEVICEDESC:
        return L"DeviceDesc";
    case CM_DRP_HARDWAREID:
        return L"HardwareID";
    case CM_DRP_COMPATIBLEIDS:
        return L"CompatibleIDs";
    case CM_DRP_SERVICE:
        return L"Service";
    case CM_DRP_CLASS:
        return L"Class";
    case CM_DRP_CLASSGUID:
        return L"ClassGUID";
    case CM_DRP_DRIVER:
        return L"Driver";
    case CM_DRP_CONFIGFLAGS:
        return L"ConfigFlags";
    case CM_DRP_MFG:
        return L"Mfg";
    case CM_DRP_FRIENDLYNAME:
        return L"FriendlyName";
    case CM_DRP_LOCATION_INFORMATION:
        return L"LocationInformation";
    default:
        return nullptr;
    }
}

/* the enumerator is the first part of the instance id. */
static CONFIGRET enumerator(PCWSTR id, PULONG type, PVOID buffer, PULONG length)
{
    ULONG n = 0;
    while (id[n] && id[n] != L'\\')
        n++;
    ULONG need = (n + 1) * sizeof(WCHAR);
    if (type)
        *type = REG_SZ;
    if (!buffer || *length < need) {
        *length = need;
        return CR_BUFFER_SMALL;
    }
    memcpy(buffer, id, n * sizeof(WCHAR));
    ((PWCHAR)buffer)[n] = 0;
    *length = need;
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DEVINST devinst, ULONG property, PULONG type, PVOID buffer,
                                                         PULONG length, ULONG flags)
{
    if (!length)
        return CR_INVALID_POINTER;
    if (property < CM_DRP_DEVICEDESC || property > CM_DRP_MAX)
        return CR_INVALID_PROPERTY;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (property == CM_DRP_ENUMERATOR_NAME)
        return enumerator(id, type, buffer, length);
    PCWSTR name = value_name(property);
    HKEY key = name ? open_enum(id, KEY_READ) : nullptr;
    if (!key)
        return CR_NO_SUCH_VALUE;
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

CMAPI CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyA(DEVINST devinst, ULONG property, PULONG type, PVOID buffer,
                                                         PULONG length, ULONG flags)
{
    if (!length)
        return CR_INVALID_POINTER;
    BYTE wide[2048];
    ULONG size = sizeof(wide), kind = 0;
    CONFIGRET cr = CM_Get_DevNode_Registry_PropertyW(devinst, property, &kind, wide, &size, flags);
    if (cr)
        return cr;
    if (type)
        *type = kind;
    if (kind != REG_SZ && kind != REG_MULTI_SZ && kind != REG_EXPAND_SZ) {
        if (!buffer || *length < size) {
            *length = size;
            return CR_BUFFER_SMALL;
        }
        memcpy(buffer, wide, size);
        *length = size;
        return CR_SUCCESS;
    }
    int need = WideCharToMultiByte(CP_ACP, 0, (PCWSTR)wide, (int)(size / sizeof(WCHAR)), nullptr, 0, nullptr, nullptr);
    if (!buffer || *length < (ULONG)need) {
        *length = (ULONG)need;
        return CR_BUFFER_SMALL;
    }
    WideCharToMultiByte(CP_ACP, 0, (PCWSTR)wide, (int)(size / sizeof(WCHAR)), buffer, need, nullptr, nullptr);
    *length = (ULONG)need;
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Open_DevNode_Key(DEVINST devinst, REGSAM access, ULONG profile, DWORD disposition,
                                           HKEY *out, ULONG flags)
{
    if (!out)
        return CR_INVALID_POINTER;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    WCHAR path[MAX_DEVICE_ID_LEN + 96] = L"SYSTEM\\CurrentControlSet\\Enum\\";
    lstrcatW(path, id);
    lstrcatW(path, L"\\Device Parameters");
    /* RegDisposition_OpenAlways is 0 and makes the key if it is not there, RegDisposition_OpenExisting is 1. */
    if (disposition > RegDisposition_OpenExisting)
        return CR_INVALID_FLAG;
    LSTATUS error = disposition == RegDisposition_OpenAlways
                        ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, out, nullptr)
                        : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, out);
    if (error == ERROR_ACCESS_DENIED)
        return CR_ACCESS_DENIED;
    return error ? CR_NO_SUCH_REGISTRY_KEY : CR_SUCCESS;
}

/* ------------------------------------------------------------------ DEVPKEY properties */

BOOL key_is(const DEVPROPKEY *a, const DEVPROPKEY *b)
{
    return a->pid == b->pid && !memcmp(&a->fmtid, &b->fmtid, sizeof(GUID));
}

CONFIGRET put(DEVPROPTYPE kind, const void *data, ULONG size, DEVPROPTYPE *type, PBYTE buffer, PULONG length)
{
    *type = kind;
    if (!buffer || *length < size) {
        *length = size;
        return CR_BUFFER_SMALL;
    }
    memcpy(buffer, data, size);
    *length = size;
    return CR_SUCCESS;
}

static int hex(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} */
BOOL parse_guid(PCWSTR text, GUID *out)
{
    static const BYTE DIGITS[] = {8, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2};
    ULONG v[11];
    if (*text++ != L'{')
        return FALSE;
    for (int i = 0; i < 11; i++) {
        v[i] = 0;
        for (int d = 0; d < DIGITS[i]; d++) {
            int h = hex(*text++);
            if (h < 0)
                return FALSE;
            v[i] = v[i] << 4 | (ULONG)h;
        }
        if ((i < 3 || i == 4) && *text++ != L'-')
            return FALSE;
    }
    if (*text != L'}')
        return FALSE;
    *out = (GUID){v[0], (USHORT)v[1], (USHORT)v[2],
                  {(BYTE)v[3], (BYTE)v[4], (BYTE)v[5], (BYTE)v[6], (BYTE)v[7], (BYTE)v[8], (BYTE)v[9], (BYTE)v[10]}};
    return TRUE;
}

CMAPI CONFIGRET WINAPI CM_Get_DevNode_PropertyW(DEVINST devinst, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                PBYTE buffer, PULONG size, ULONG flags)
{
    if (!key || !type || !size)
        return CR_INVALID_POINTER;
    if (flags)
        return CR_INVALID_FLAG;
    PCWSTR id = id_of(devinst);
    if (!id)
        return CR_INVALID_DEVNODE;
    if (key_is(key, &DEVPKEY_Device_InstanceId))
        return put(DEVPROP_TYPE_STRING, id, (lstrlenW(id) + 1) * sizeof(WCHAR), type, buffer, size);
    if (key_is(key, &DEVPKEY_Device_Parent)) {
        DEVINST parent;
        WCHAR pid[MAX_DEVICE_ID_LEN + 1];
        if (CM_Get_Parent(&parent, devinst, 0) || CM_Get_Device_IDW(parent, pid, RTL_NUMBER_OF(pid), 0))
            return CR_NO_SUCH_VALUE;
        return put(DEVPROP_TYPE_STRING, pid, (lstrlenW(pid) + 1) * sizeof(WCHAR), type, buffer, size);
    }
    static const struct {
        const DEVPROPKEY *key;
        ULONG property;
    } REGISTRY[] = {
        {&DEVPKEY_Device_DeviceDesc, CM_DRP_DEVICEDESC},     {&DEVPKEY_Device_HardwareIds, CM_DRP_HARDWAREID},
        {&DEVPKEY_Device_CompatibleIds, CM_DRP_COMPATIBLEIDS}, {&DEVPKEY_Device_Service, CM_DRP_SERVICE},
        {&DEVPKEY_Device_Class, CM_DRP_CLASS},               {&DEVPKEY_Device_ClassGuid, CM_DRP_CLASSGUID},
        {&DEVPKEY_Device_Manufacturer, CM_DRP_MFG},          {&DEVPKEY_Device_FriendlyName, CM_DRP_FRIENDLYNAME},
        {&DEVPKEY_Device_LocationInfo, CM_DRP_LOCATION_INFORMATION},
        {&DEVPKEY_Device_BusReportedDeviceDesc, CM_DRP_DEVICEDESC},
        {&DEVPKEY_NAME, CM_DRP_FRIENDLYNAME},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(REGISTRY); i++) {
        if (!key_is(key, REGISTRY[i].key))
            continue;
        BYTE data[2048];
        ULONG length = sizeof(data), kind;
        CONFIGRET cr = CM_Get_DevNode_Registry_PropertyW(devinst, REGISTRY[i].property, &kind, data, &length, 0);
        /* like windows' DEVPKEY_NAME, the name falls back to the description. */
        if (cr == CR_NO_SUCH_VALUE && key_is(key, &DEVPKEY_NAME)) {
            length = sizeof(data);
            cr = CM_Get_DevNode_Registry_PropertyW(devinst, CM_DRP_DEVICEDESC, &kind, data, &length, 0);
        }
        if (cr)
            return cr;
        if (REGISTRY[i].property == CM_DRP_CLASSGUID) {
            GUID guid;
            return parse_guid((PCWSTR)data, &guid) ? put(DEVPROP_TYPE_GUID, &guid, sizeof(guid), type, buffer, size)
                                                    : CR_INVALID_DATA;
        }
        return put(kind == REG_MULTI_SZ ? DEVPROP_TYPE_STRING_LIST : DEVPROP_TYPE_STRING, data, length, type, buffer,
                   size);
    }
    if (key_is(key, &DEVPKEY_Device_ContainerId)) {
        HKEY k = open_enum(id, KEY_READ);
        WCHAR text[64];
        DWORD bytes = sizeof(text);
        LSTATUS error = k ? RegQueryValueExW(k, L"ContainerID", nullptr, nullptr, (BYTE *)text, &bytes) : 1;
        if (k)
            RegCloseKey(k);
        GUID guid;
        if (error || !parse_guid(text, &guid))
            return CR_NO_SUCH_VALUE;
        return put(DEVPROP_TYPE_GUID, &guid, sizeof(guid), type, buffer, size);
    }
    return CR_NO_SUCH_VALUE;
}

/* ------------------------------------------------------------------ interfaces */

static CONFIGRET interface_list(GUID *klass, PCWSTR device, MIME_PNP_REQUEST **out)
{
    if (!klass)
        return CR_INVALID_POINTER;
    if (device && device[0] && !present(device))
        return CR_INVALID_DEVICE_ID;
    return NT_SUCCESS(request(MIME_PNP_LIST_INTERFACES, 0, klass, device, out)) ? CR_SUCCESS : CR_FAILURE;
}

static ULONG multi_length(PCWSTR s)
{
    ULONG n = 0;
    while (s[n])
        n += (ULONG)lstrlenW(s + n) + 1;
    return n + 1;
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG length, GUID *klass, PCWSTR device, ULONG flags)
{
    if (!length)
        return CR_INVALID_POINTER;
    if (flags & ~CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES)
        return CR_INVALID_FLAG;
    MIME_PNP_REQUEST *r;
    CONFIGRET cr = interface_list(klass, device, &r);
    if (cr)
        return cr;
    *length = multi_length(r->Out);
    HeapFree(GetProcessHeap(), 0, r);
    return CR_SUCCESS;
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeA(PULONG length, GUID *klass, PCSTR device, ULONG flags)
{
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (device)
        MultiByteToWideChar(CP_ACP, 0, device, -1, w, MAX_DEVICE_ID_LEN);
    return CM_Get_Device_Interface_List_SizeW(length, klass, device ? w : nullptr, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_ListW(GUID *klass, PCWSTR device, PWSTR buffer, ULONG length,
                                                     ULONG flags)
{
    if (!buffer || !length)
        return CR_INVALID_POINTER;
    if (flags & ~CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES)
        return CR_INVALID_FLAG;
    MIME_PNP_REQUEST *r;
    CONFIGRET cr = interface_list(klass, device, &r);
    if (cr)
        return cr;
    ULONG n = multi_length(r->Out);
    if (n > length) {
        buffer[0] = 0;
        cr = CR_BUFFER_SMALL;
    } else {
        memcpy(buffer, r->Out, n * sizeof(WCHAR));
    }
    HeapFree(GetProcessHeap(), 0, r);
    return cr;
}

CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_ListA(GUID *klass, PCSTR device, PSTR buffer, ULONG length,
                                                     ULONG flags)
{
    if (!buffer || !length)
        return CR_INVALID_POINTER;
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (device)
        MultiByteToWideChar(CP_ACP, 0, device, -1, w, MAX_DEVICE_ID_LEN);
    ULONG chars;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(&chars, klass, device ? w : nullptr, flags);
    if (cr)
        return cr;
    PWSTR list = HeapAlloc(GetProcessHeap(), 0, chars * sizeof(WCHAR));
    if (!list)
        return CR_OUT_OF_MEMORY;
    cr = CM_Get_Device_Interface_ListW(klass, device ? w : nullptr, list, chars, flags);
    if (!cr && !WideCharToMultiByte(CP_ACP, 0, list, (int)chars, buffer, (int)length, nullptr, nullptr)) {
        buffer[0] = 0;
        cr = CR_BUFFER_SMALL;
    }
    HeapFree(GetProcessHeap(), 0, list);
    return cr;
}

/* looks up the interface's device, then reads that device's properties. */
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_PropertyW(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                         PBYTE buffer, PULONG size, ULONG flags)
{
    if (!path || !key || !type || !size)
        return CR_INVALID_POINTER;
    BOOL handled;
    CONFIGRET own = InterfaceOwnProperty(path, key, type, buffer, size, &handled);
    if (handled)
        return own;
    MIME_PNP_REQUEST *r;
    if (!NT_SUCCESS(request(MIME_PNP_INTERFACE_DEVICE, 0, nullptr, path, &r)))
        return CR_NO_SUCH_DEVICE_INTERFACE;
    DEVINST devinst = node_of(r->Out);
    HeapFree(GetProcessHeap(), 0, r);
    return CM_Get_DevNode_PropertyW(devinst, key, type, buffer, size, flags);
}

CMAPI DWORD WINAPI CM_MapCrToWin32Err(CONFIGRET cr, DWORD fallback)
{
    switch (cr) {
    case CR_SUCCESS:
        return ERROR_SUCCESS;
    case CR_OUT_OF_MEMORY:
        return ERROR_NOT_ENOUGH_MEMORY;
    case CR_INVALID_POINTER:
        return ERROR_INVALID_USER_BUFFER;
    case CR_INVALID_FLAG:
        return ERROR_INVALID_FLAGS;
    case CR_INVALID_DEVNODE:
    case CR_INVALID_DEVICE_ID:
        return ERROR_INVALID_DATA;
    case CR_NO_SUCH_DEVNODE:
    case CR_NO_SUCH_DEVICE_INTERFACE:
        return ERROR_NOT_FOUND;
    case CR_NO_SUCH_VALUE:
        return ERROR_NOT_FOUND;
    case CR_BUFFER_SMALL:
        return ERROR_INSUFFICIENT_BUFFER;
    case CR_REGISTRY_ERROR:
        return ERROR_REGISTRY_CORRUPT;
    case CR_NO_SUCH_REGISTRY_KEY:
        return ERROR_FILE_NOT_FOUND;
    default:
        return fallback;
    }
}

/* ------------------------------------------------------------------ device id lists */

#define CM_GETIDLIST_FILTER_NONE 0x0
#define CM_GETIDLIST_FILTER_ENUMERATOR 0x1
#define CM_GETIDLIST_FILTER_SERVICE 0x2
#define CM_GETIDLIST_FILTER_EJECTRELATIONS 0x4
#define CM_GETIDLIST_FILTER_REMOVALRELATIONS 0x8
#define CM_GETIDLIST_FILTER_POWERRELATIONS 0x10
#define CM_GETIDLIST_FILTER_BUSRELATIONS 0x20
#define CM_GETIDLIST_DONOTGENERATE 0x10000040
#define CM_GETIDLIST_FILTER_TRANSPORTRELATIONS 0x80
#define CM_GETIDLIST_FILTER_PRESENT 0x100
#define CM_GETIDLIST_FILTER_CLASS 0x200
#define CM_GETIDLIST_FILTER_BITS 0x100003FF

static BOOL value_is(PCWSTR id, PCWSTR name, PCWSTR wanted)
{
    HKEY key = open_enum(id, KEY_QUERY_VALUE);
    if (!key)
        return FALSE;
    WCHAR value[128];
    DWORD size = sizeof(value) - sizeof(WCHAR), type;
    BOOL match = !RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size) && type == REG_SZ &&
                 (value[size / sizeof(WCHAR)] = 0, !lstrcmpiW(value, wanted));
    RegCloseKey(key);
    return match;
}

static BOOL id_wanted(PCWSTR id, PCWSTR filter, ULONG flags)
{
    if (flags & CM_GETIDLIST_FILTER_ENUMERATOR) {
        int n = lstrlenW(filter);
        /* a bare enumerator matches its devices, a device id itself and its instances. */
        if (_wcsnicmp(id, filter, n) || (id[n] && id[n] != L'\\'))
            return FALSE;
    }
    if ((flags & CM_GETIDLIST_FILTER_SERVICE) && !value_is(id, L"Service", filter))
        return FALSE;
    if ((flags & CM_GETIDLIST_FILTER_CLASS) && !value_is(id, L"ClassGUID", filter))
        return FALSE;
    return TRUE;
}

/* a multi string of matching ids, the root first unless filtered out. */
static CONFIGRET id_list(PCWSTR filter, ULONG flags, PWSTR buffer, ULONG *length)
{
    if (flags & ~CM_GETIDLIST_FILTER_BITS)
        return CR_INVALID_FLAG;
    ULONG relations = flags & (CM_GETIDLIST_FILTER_EJECTRELATIONS | CM_GETIDLIST_FILTER_REMOVALRELATIONS |
                               CM_GETIDLIST_FILTER_POWERRELATIONS | CM_GETIDLIST_FILTER_BUSRELATIONS |
                               CM_GETIDLIST_FILTER_TRANSPORTRELATIONS);
    if ((flags & (CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_SERVICE | CM_GETIDLIST_FILTER_CLASS)) ||
        relations) {
        if (!filter || !filter[0])
            return CR_INVALID_POINTER;
    }
    MIME_PNP_REQUEST *r;
    if (!NT_SUCCESS(request(MIME_PNP_LIST_DEVICES, 0, nullptr, nullptr, &r)))
        return CR_FAILURE;
    ULONG used = 0, room = buffer ? *length : 0;
    CONFIGRET cr = CR_SUCCESS;
    BOOL with_root = !(flags & (CM_GETIDLIST_FILTER_SERVICE | CM_GETIDLIST_FILTER_CLASS)) && !relations &&
                     (!(flags & CM_GETIDLIST_FILTER_ENUMERATOR) || !lstrcmpiW(filter, L"HTREE"));
    static const WCHAR root[] = ROOT_ID;
    for (PCWSTR p = with_root ? root : r->Out; *p;) {
        BOOL wanted = p == root || (relations ? FALSE : id_wanted(p, filter, flags));
        if (wanted) {
            ULONG n = (ULONG)lstrlenW(p) + 1;
            if (buffer && used + n < room)
                memcpy(buffer + used, p, n * sizeof(WCHAR));
            else if (buffer)
                cr = CR_BUFFER_SMALL;
            used += n;
        }
        p = p == root ? r->Out : p + lstrlenW(p) + 1;
    }
    HeapFree(GetProcessHeap(), 0, r);
    if (buffer) {
        if (used < room)
            buffer[used] = 0;
        else
            cr = CR_BUFFER_SMALL;
    }
    *length = used + 1;
    return cr;
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG length, PCWSTR filter, ULONG flags)
{
    if (!length)
        return CR_INVALID_POINTER;
    return id_list(filter, flags, nullptr, length);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_ListW(PCWSTR filter, PWCHAR buffer, ULONG length, ULONG flags)
{
    if (!buffer || !length)
        return CR_INVALID_POINTER;
    ULONG n = length;
    return id_list(filter, flags, buffer, &n);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_List_SizeA(PULONG length, PCSTR filter, ULONG flags)
{
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (filter)
        MultiByteToWideChar(CP_ACP, 0, filter, -1, w, MAX_DEVICE_ID_LEN);
    return CM_Get_Device_ID_List_SizeW(length, filter ? w : nullptr, flags);
}

CMAPI CONFIGRET WINAPI CM_Get_Device_ID_ListA(PCSTR filter, PCHAR buffer, ULONG length, ULONG flags)
{
    if (!buffer || !length)
        return CR_INVALID_POINTER;
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (filter)
        MultiByteToWideChar(CP_ACP, 0, filter, -1, w, MAX_DEVICE_ID_LEN);
    PWSTR wide = HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (!wide)
        return CR_OUT_OF_MEMORY;
    ULONG n = length;
    CONFIGRET cr = id_list(filter ? w : nullptr, flags, wide, &n);
    if (!cr)
        WideCharToMultiByte(CP_ACP, 0, wide, (int)n, buffer, (int)length, nullptr, nullptr);
    HeapFree(GetProcessHeap(), 0, wide);
    return cr;
}
