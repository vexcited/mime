/*
 * setupapi: device information sets over cfgmgr32. a set holds devices
 * and the interfaces of a class, present as the kernel's device tree
 * lists them; their properties are those of their Enum keys. the CM_
 * functions are forwards to cfgmgr32, as on windows.
 */
#define MIME_BUILD_SETUPAPI
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/pnp.h>
#include <mime/win32/setupapi.h>

#define SET_MAGIC 0x53455444 /* 'DTES' */
#define MAX_PATH_CHARS 512

/* SP_DEVICE_INTERFACE_DETAIL_DATA's size: windows packs it to a byte on x86. */
#ifdef _WIN64
#define DETAIL_SIZE_W 8
#define DETAIL_SIZE_A 8
#else
#define DETAIL_SIZE_W 6
#define DETAIL_SIZE_A 5
#endif

typedef struct DEVICE {
    WCHAR Id[MAX_DEVICE_ID_LEN];
    DEVINST Inst;
    GUID Class;
} DEVICE;

typedef struct IFACE {
    WCHAR Path[MAX_PATH_CHARS];
    GUID Class;
    DWORD Device; /* its index in the set */
} IFACE;

typedef struct DEVICE_SET {
    DWORD Magic;
    GUID Class;
    BOOL HasClass;
    DEVICE *Devices;
    DWORD DeviceCount, DeviceCapacity;
    IFACE *Ifaces;
    DWORD IfaceCount, IfaceCapacity;
} DEVICE_SET;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}

static BOOL same_guid(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

static DEVICE_SET *set_of(HDEVINFO h)
{
    DEVICE_SET *s = h;
    if (!s || h == INVALID_HANDLE_VALUE || s->Magic != SET_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return s;
}

static BOOL grow(void **items, DWORD *capacity, DWORD count, SIZE_T size)
{
    if (count < *capacity)
        return TRUE;
    DWORD next = *capacity ? *capacity * 2 : 8;
    void *grown = *items ? HeapReAlloc(GetProcessHeap(), 0, *items, next * size)
                         : HeapAlloc(GetProcessHeap(), 0, next * size);
    if (!grown)
        return FALSE;
    *items = grown;
    *capacity = next;
    return TRUE;
}

static BOOL parse_guid(PCWSTR text, GUID *out)
{
    static const BYTE DIGITS[] = {8, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2};
    ULONG v[11];
    if (*text++ != L'{')
        return FALSE;
    for (int i = 0; i < 11; i++) {
        v[i] = 0;
        for (int d = 0; d < DIGITS[i]; d++) {
            WCHAR c = *text++;
            int h = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
            if (h < 0)
                return FALSE;
            v[i] = v[i] << 4 | (ULONG)h;
        }
        if ((i < 3 || i == 4) && *text++ != L'-')
            return FALSE;
    }
    *out = (GUID){v[0], (USHORT)v[1], (USHORT)v[2],
                  {(BYTE)v[3], (BYTE)v[4], (BYTE)v[5], (BYTE)v[6], (BYTE)v[7], (BYTE)v[8], (BYTE)v[9], (BYTE)v[10]}};
    return TRUE;
}

static GUID class_of(DEVINST inst)
{
    WCHAR text[64];
    ULONG size = sizeof(text);
    GUID guid = {};
    if (!CM_Get_DevNode_Registry_PropertyW(inst, CM_DRP_CLASSGUID, nullptr, text, &size, 0))
        parse_guid(text, &guid);
    return guid;
}

/* a device of the set, added when new; its index or ~0. */
static DWORD add_device(DEVICE_SET *s, PCWSTR id)
{
    for (DWORD i = 0; i < s->DeviceCount; i++)
        if (!lstrcmpiW(s->Devices[i].Id, id))
            return i;
    DEVINST inst;
    if (CM_Locate_DevNodeW(&inst, id, CM_LOCATE_DEVNODE_PHANTOM))
        return ~0u;
    if (!grow((void **)&s->Devices, &s->DeviceCapacity, s->DeviceCount, sizeof(DEVICE)))
        return ~0u;
    DEVICE *d = &s->Devices[s->DeviceCount];
    lstrcpynW(d->Id, id, MAX_DEVICE_ID_LEN);
    d->Inst = inst;
    d->Class = class_of(inst);
    return s->DeviceCount++;
}

static BOOL prefix_matches(PCWSTR id, PCWSTR enumerator)
{
    if (!enumerator || !enumerator[0])
        return TRUE;
    int n = lstrlenW(enumerator);
    return CompareStringOrdinal(id, n, enumerator, n, TRUE) == CSTR_EQUAL && id[n] == L'\\';
}

/* the kernel's list: interface paths of a class or of every class, or present devices. */
static MIME_PNP_REQUEST *ask(MIME_PNP_CLASS klass, ULONG flags, const GUID *guid)
{
    for (ULONG chars = 4096;;) {
        ULONG size = (ULONG)sizeof(MIME_PNP_REQUEST) + chars * sizeof(WCHAR);
        MIME_PNP_REQUEST *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (!r)
            return nullptr;
        r->Flags = flags;
        if (guid)
            r->Class = *guid;
        NTSTATUS status = NtPlugPlayControl(klass, r, size);
        if (NT_SUCCESS(status))
            return r;
        ULONG need = r->OutChars;
        HeapFree(GetProcessHeap(), 0, r);
        if (status != STATUS_BUFFER_TOO_SMALL || need <= chars)
            return nullptr;
        chars = need;
    }
}

static BOOL add_interfaces(DEVICE_SET *s, const GUID *guid, BOOL all, PCWSTR enumerator)
{
    MIME_PNP_REQUEST *r = ask(MIME_PNP_LIST_INTERFACES, all, guid);
    if (!r)
        return FALSE;
    for (PCWSTR p = r->Out; *p; p += lstrlenW(p) + 1) {
        DEVPROPTYPE type;
        WCHAR id[MAX_DEVICE_ID_LEN];
        ULONG size = sizeof(id);
        if (CM_Get_Device_Interface_PropertyW(p, &DEVPKEY_Device_InstanceId, &type, (PBYTE)id, &size, 0))
            continue;
        if (!prefix_matches(id, enumerator))
            continue;
        DWORD device = add_device(s, id);
        if (device == ~0u || !grow((void **)&s->Ifaces, &s->IfaceCapacity, s->IfaceCount, sizeof(IFACE)))
            continue;
        IFACE *i = &s->Ifaces[s->IfaceCount++];
        lstrcpynW(i->Path, p, MAX_PATH_CHARS);
        i->Device = device;
        /* the class of each interface, from the kernel. */
        BYTE query[sizeof(MIME_PNP_REQUEST) + MAX_DEVICE_ID_LEN * sizeof(WCHAR)] = {};
        MIME_PNP_REQUEST *q = (MIME_PNP_REQUEST *)query;
        lstrcpynW(q->Instance, p, RTL_NUMBER_OF(q->Instance));
        if (NT_SUCCESS(NtPlugPlayControl(MIME_PNP_INTERFACE_DEVICE, q, sizeof(query))))
            i->Class = q->Class;
        else if (guid)
            i->Class = *guid;
    }
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

static BOOL add_devices(DEVICE_SET *s, const GUID *guid, PCWSTR enumerator)
{
    MIME_PNP_REQUEST *r = ask(MIME_PNP_LIST_DEVICES, 0, nullptr);
    if (!r)
        return FALSE;
    for (PCWSTR p = r->Out; *p; p += lstrlenW(p) + 1) {
        if (!prefix_matches(p, enumerator))
            continue;
        DEVINST inst;
        if (CM_Locate_DevNodeW(&inst, p, 0))
            continue;
        GUID klass = class_of(inst);
        if (guid && !same_guid(&klass, guid))
            continue;
        add_device(s, p);
    }
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}

static DEVICE_SET *new_set(const GUID *guid)
{
    DEVICE_SET *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    s->Magic = SET_MAGIC;
    if (guid) {
        s->Class = *guid;
        s->HasClass = TRUE;
    }
    return s;
}

/* ------------------------------------------------------------------ sets */

WINSETUPAPI HDEVINFO WINAPI SetupDiCreateDeviceInfoList(const GUID *guid, HWND parent)
{
    DEVICE_SET *s = new_set(guid);
    return s ? s : INVALID_HANDLE_VALUE;
}

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsExW(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags,
                                                   HDEVINFO existing, PCWSTR machine, PVOID reserved)
{
    if (machine && machine[0]) {
        SetLastError(ERROR_INVALID_MACHINENAME);
        return INVALID_HANDLE_VALUE;
    }
    if (!guid && !(flags & DIGCF_ALLCLASSES)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    DEVICE_SET *s = existing ? set_of(existing) : new_set((flags & DIGCF_DEVICEINTERFACE) ? nullptr : guid);
    if (!s)
        return INVALID_HANDLE_VALUE;
    BOOL all = (flags & DIGCF_ALLCLASSES) != 0;
    BOOL ok = (flags & DIGCF_DEVICEINTERFACE) ? add_interfaces(s, guid, all, enumerator)
                                              : add_devices(s, all ? nullptr : guid, enumerator);
    if (!ok && !existing) {
        HeapFree(GetProcessHeap(), 0, s);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    return s;
}

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsW(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags)
{
    return SetupDiGetClassDevsExW(guid, enumerator, parent, flags, nullptr, nullptr, nullptr);
}

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsA(const GUID *guid, PCSTR enumerator, HWND parent, DWORD flags)
{
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (enumerator)
        MultiByteToWideChar(CP_ACP, 0, enumerator, -1, w, MAX_DEVICE_ID_LEN);
    return SetupDiGetClassDevsW(guid, enumerator ? w : nullptr, parent, flags);
}

WINSETUPAPI BOOL WINAPI SetupDiDestroyDeviceInfoList(HDEVINFO set)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    s->Magic = 0;
    HeapFree(GetProcessHeap(), 0, s->Devices);
    HeapFree(GetProcessHeap(), 0, s->Ifaces);
    HeapFree(GetProcessHeap(), 0, s);
    return TRUE;
}

static BOOL fill_devinfo(DEVICE_SET *s, DWORD index, PSP_DEVINFO_DATA data)
{
    data->ClassGuid = s->Devices[index].Class;
    data->DevInst = s->Devices[index].Inst;
    data->Reserved = index + 1;
    return TRUE;
}

WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, PSP_DEVINFO_DATA data)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    if (!data || data->cbSize != sizeof(SP_DEVINFO_DATA)) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    if (index >= s->DeviceCount) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    return fill_devinfo(s, index, data);
}

/* a device of the set from what a caller holds. */
static DEVICE *device_of(DEVICE_SET *s, PSP_DEVINFO_DATA data)
{
    if (!data || data->cbSize != sizeof(SP_DEVINFO_DATA)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!data->Reserved || data->Reserved > s->DeviceCount) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return &s->Devices[data->Reserved - 1];
}

WINSETUPAPI BOOL WINAPI SetupDiOpenDeviceInfoW(HDEVINFO set, PCWSTR instance, HWND parent, DWORD flags,
                                               PSP_DEVINFO_DATA data)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    if (!instance) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (data && data->cbSize != sizeof(SP_DEVINFO_DATA)) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    DWORD index = add_device(s, instance);
    if (index == ~0u) {
        SetLastError(ERROR_NO_SUCH_DEVINST);
        return FALSE;
    }
    return !data || fill_devinfo(s, index, data);
}

WINSETUPAPI BOOL WINAPI SetupDiOpenDeviceInfoA(HDEVINFO set, PCSTR instance, HWND parent, DWORD flags,
                                               PSP_DEVINFO_DATA data)
{
    WCHAR w[MAX_DEVICE_ID_LEN];
    if (instance)
        MultiByteToWideChar(CP_ACP, 0, instance, -1, w, MAX_DEVICE_ID_LEN);
    return SetupDiOpenDeviceInfoW(set, instance ? w : nullptr, parent, flags, data);
}

/* ------------------------------------------------------------------ interfaces */

WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA device, const GUID *guid,
                                                    DWORD index, PSP_DEVICE_INTERFACE_DATA data)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    if (!guid || !data || data->cbSize != sizeof(*data)) {
        SetLastError(data && data->cbSize != sizeof(*data) ? ERROR_INVALID_USER_BUFFER : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD only = ~0u;
    if (device) {
        DEVICE *d = device_of(s, device);
        if (!d)
            return FALSE;
        only = (DWORD)(d - s->Devices);
    }
    for (DWORD i = 0, n = 0; i < s->IfaceCount; i++) {
        IFACE *f = &s->Ifaces[i];
        if (!same_guid(&f->Class, guid) || (only != ~0u && f->Device != only))
            continue;
        if (n++ != index)
            continue;
        data->InterfaceClassGuid = f->Class;
        data->Flags = SPINT_ACTIVE;
        data->Reserved = i + 1;
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

static IFACE *iface_of(DEVICE_SET *s, PSP_DEVICE_INTERFACE_DATA data)
{
    if (!data || data->cbSize != sizeof(*data) || !data->Reserved || data->Reserved > s->IfaceCount) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return &s->Ifaces[data->Reserved - 1];
}

/* the path after a 4 byte header; without a buffer or with a small one, the size needed. */
static BOOL interface_detail(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface, void *detail, DWORD size,
                             PDWORD required, PSP_DEVINFO_DATA device, BOOL wide)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    IFACE *f = iface_of(s, iface);
    if (!f)
        return FALSE;
    DWORD header_size = wide ? DETAIL_SIZE_W : DETAIL_SIZE_A;
    if (detail && *(DWORD *)detail != header_size) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    if (!detail && size) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    DWORD chars = (DWORD)lstrlenW(f->Path) + 1;
    DWORD need = sizeof(DWORD) + (wide ? chars * sizeof(WCHAR)
                                       : (DWORD)WideCharToMultiByte(CP_ACP, 0, f->Path, -1, nullptr, 0, nullptr, nullptr));
    if (required)
        *required = need;
    if (device && device->cbSize == sizeof(SP_DEVINFO_DATA))
        fill_devinfo(s, f->Device, device);
    if (!detail || size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (wide)
        memcpy((BYTE *)detail + sizeof(DWORD), f->Path, chars * sizeof(WCHAR));
    else
        WideCharToMultiByte(CP_ACP, 0, f->Path, -1, (CHAR *)detail + sizeof(DWORD), (int)(size - sizeof(DWORD)),
                            nullptr, nullptr);
    return TRUE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device)
{
    return interface_detail(set, iface, detail, size, required, device, TRUE);
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device)
{
    return interface_detail(set, iface, detail, size, required, device, FALSE);
}

/* ------------------------------------------------------------------ properties */

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInstanceIdW(HDEVINFO set, PSP_DEVINFO_DATA device, PWSTR buffer, DWORD size,
                                                    PDWORD required)
{
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return FALSE;
    DWORD need = (DWORD)lstrlenW(d->Id) + 1;
    if (required)
        *required = need;
    if (!buffer || size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, d->Id, need * sizeof(WCHAR));
    return TRUE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInstanceIdA(HDEVINFO set, PSP_DEVINFO_DATA device, PSTR buffer, DWORD size,
                                                    PDWORD required)
{
    WCHAR w[MAX_DEVICE_ID_LEN];
    DWORD need;
    if (!SetupDiGetDeviceInstanceIdW(set, device, w, MAX_DEVICE_ID_LEN, &need))
        return FALSE;
    DWORD bytes = (DWORD)WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (required)
        *required = bytes;
    if (!buffer || size < bytes) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, w, -1, buffer, (int)size, nullptr, nullptr);
    return TRUE;
}

static BOOL cr_result(CONFIGRET cr, DWORD missing)
{
    if (!cr)
        return TRUE;
    SetLastError(cr == CR_BUFFER_SMALL ? ERROR_INSUFFICIENT_BUFFER
                 : cr == CR_NO_SUCH_VALUE ? missing
                                          : CM_MapCrToWin32Err(cr, ERROR_INVALID_DATA));
    return FALSE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required)
{
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return FALSE;
    if (property >= SPDRP_MAXIMUM_PROPERTY) {
        SetLastError(ERROR_INVALID_REG_PROPERTY);
        return FALSE;
    }
    ULONG length = buffer ? size : 0;
    CONFIGRET cr = CM_Get_DevNode_Registry_PropertyW(d->Inst, property + 1, type, buffer, &length, 0);
    if (required && (!cr || cr == CR_BUFFER_SMALL))
        *required = length;
    return cr_result(cr, ERROR_INVALID_DATA);
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required)
{
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return FALSE;
    if (property >= SPDRP_MAXIMUM_PROPERTY) {
        SetLastError(ERROR_INVALID_REG_PROPERTY);
        return FALSE;
    }
    ULONG length = buffer ? size : 0;
    CONFIGRET cr = CM_Get_DevNode_Registry_PropertyA(d->Inst, property + 1, type, buffer, &length, 0);
    if (required && (!cr || cr == CR_BUFFER_SMALL))
        *required = length;
    return cr_result(cr, ERROR_INVALID_DATA);
}

WINSETUPAPI BOOL WINAPI SetupDiSetDeviceRegistryPropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          const BYTE *buffer, DWORD size)
{
    static const PCWSTR NAMES[] = {L"DeviceDesc", L"HardwareID", L"CompatibleIDs", nullptr, L"Service", nullptr,
                                   nullptr, L"Class", L"ClassGUID", L"Driver", L"ConfigFlags", L"Mfg",
                                   L"FriendlyName", L"LocationInformation"};
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return FALSE;
    if (property >= RTL_NUMBER_OF(NAMES) || !NAMES[property]) {
        SetLastError(ERROR_INVALID_REG_PROPERTY);
        return FALSE;
    }
    WCHAR path[MAX_DEVICE_ID_LEN + 64] = L"SYSTEM\\CurrentControlSet\\Enum\\";
    lstrcatW(path, d->Id);
    HKEY key;
    LSTATUS error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_SET_VALUE, &key);
    if (!error) {
        DWORD type = property == SPDRP_HARDWAREID || property == SPDRP_COMPATIBLEIDS ? REG_MULTI_SZ
                     : property == SPDRP_CONFIGFLAGS                                ? REG_DWORD
                                                                                    : REG_SZ;
        error = buffer ? RegSetValueExW(key, NAMES[property], 0, type, buffer, size)
                       : RegDeleteValueW(key, NAMES[property]);
        RegCloseKey(key);
    }
    if (error) {
        SetLastError((DWORD)error);
        return FALSE;
    }
    return TRUE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDevicePropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, const DEVPROPKEY *key,
                                                  DEVPROPTYPE *type, PBYTE buffer, DWORD size, PDWORD required,
                                                  DWORD flags)
{
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return FALSE;
    if (!key || !type || (!buffer && size)) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    ULONG length = size;
    CONFIGRET cr = CM_Get_DevNode_PropertyW(d->Inst, key, type, buffer, &length, flags);
    if (required && (!cr || cr == CR_BUFFER_SMALL))
        *required = length;
    return cr_result(cr, ERROR_NOT_FOUND);
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfacePropertyW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                           const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer,
                                                           DWORD size, PDWORD required, DWORD flags)
{
    DEVICE_SET *s = set_of(set);
    IFACE *f = s ? iface_of(s, iface) : nullptr;
    if (!f)
        return FALSE;
    if (!key || !type || (!buffer && size)) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    ULONG length = size;
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(f->Path, key, type, buffer, &length, flags);
    if (required && (!cr || cr == CR_BUFFER_SMALL))
        *required = length;
    return cr_result(cr, ERROR_NOT_FOUND);
}

/* ------------------------------------------------------------------ registry keys */

static HKEY device_key(DEVICE *d, DWORD type, REGSAM access, BOOL create)
{
    WCHAR path[MAX_DEVICE_ID_LEN + 96];
    if (type == DIREG_DRV) {
        /* the driver key named by the device's Driver value. */
        WCHAR driver[128];
        ULONG size = sizeof(driver);
        if (CM_Get_DevNode_Registry_PropertyW(d->Inst, CM_DRP_DRIVER, nullptr, driver, &size, 0)) {
            SetLastError(ERROR_KEY_DOES_NOT_EXIST);
            return INVALID_HANDLE_VALUE;
        }
        lstrcpyW(path, L"SYSTEM\\CurrentControlSet\\Control\\Class\\");
        lstrcatW(path, driver);
    } else {
        lstrcpyW(path, L"SYSTEM\\CurrentControlSet\\Enum\\");
        lstrcatW(path, d->Id);
        lstrcatW(path, L"\\Device Parameters");
    }
    HKEY key;
    LSTATUS error = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, &key, nullptr)
                           : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, &key);
    if (error) {
        SetLastError(error == ERROR_FILE_NOT_FOUND ? ERROR_KEY_DOES_NOT_EXIST : (DWORD)error);
        return INVALID_HANDLE_VALUE;
    }
    return key;
}

WINSETUPAPI HKEY WINAPI SetupDiOpenDevRegKey(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD scope, DWORD profile,
                                             DWORD type, REGSAM access)
{
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return INVALID_HANDLE_VALUE;
    if ((scope != DICS_FLAG_GLOBAL && scope != DICS_FLAG_CONFIGSPECIFIC) || (type != DIREG_DEV && type != DIREG_DRV)) {
        SetLastError(ERROR_INVALID_FLAGS);
        return INVALID_HANDLE_VALUE;
    }
    return device_key(d, type, access, FALSE);
}

WINSETUPAPI HKEY WINAPI SetupDiCreateDevRegKeyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD scope, DWORD profile,
                                                DWORD type, PVOID inf, PCWSTR section)
{
    DEVICE_SET *s = set_of(set);
    DEVICE *d = s ? device_of(s, device) : nullptr;
    if (!d)
        return INVALID_HANDLE_VALUE;
    if ((scope != DICS_FLAG_GLOBAL && scope != DICS_FLAG_CONFIGSPECIFIC) || (type != DIREG_DEV && type != DIREG_DRV)) {
        SetLastError(ERROR_INVALID_FLAGS);
        return INVALID_HANDLE_VALUE;
    }
    return device_key(d, type, KEY_ALL_ACCESS, TRUE);
}

/* windows keeps an interface's settings under DeviceClasses, the path's \\?\ as ##?#. */
static HKEY interface_key(IFACE *f, REGSAM access, BOOL create)
{
    WCHAR path[MAX_PATH_CHARS + 160] = L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\";
    PCWSTR brace = f->Path;
    for (PCWSTR p = f->Path; *p; p++)
        if (*p == L'{')
            brace = p;
    lstrcatW(path, brace);
    lstrcatW(path, L"\\##?#");
    lstrcatW(path, f->Path + 4);
    lstrcatW(path, L"\\#\\Device Parameters");
    HKEY key;
    LSTATUS error = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, &key, nullptr)
                           : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, &key);
    if (error) {
        SetLastError((DWORD)error);
        return INVALID_HANDLE_VALUE;
    }
    return key;
}

WINSETUPAPI HKEY WINAPI SetupDiCreateDeviceInterfaceRegKeyW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                            DWORD reserved, REGSAM access, PVOID inf, PCWSTR section)
{
    DEVICE_SET *s = set_of(set);
    IFACE *f = s ? iface_of(s, iface) : nullptr;
    return f ? interface_key(f, access, TRUE) : INVALID_HANDLE_VALUE;
}

WINSETUPAPI HKEY WINAPI SetupDiOpenDeviceInterfaceRegKey(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface, DWORD reserved,
                                                         REGSAM access)
{
    DEVICE_SET *s = set_of(set);
    IFACE *f = s ? iface_of(s, iface) : nullptr;
    return f ? interface_key(f, access, FALSE) : INVALID_HANDLE_VALUE;
}

/* no class installers run: nothing to install, as for a device already set up. */
WINSETUPAPI BOOL WINAPI SetupDiCallClassInstaller(DI_FUNCTION function, HDEVINFO set, PSP_DEVINFO_DATA device)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    if (device && !device_of(s, device))
        return FALSE;
    return TRUE;
}
