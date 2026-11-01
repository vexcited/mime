/*
 * the parts of the bluetooth api that need the bluetooth support service or a radio, and the service record walkers.
 * the service is not running and there is no radio, so these fail as windows does on a machine without either.
 */
#define BLUETOOTHAPI
#include <mime/win32/bluetoothapis.h>
#include <mime/win32/rpc.h>
#include <mime/win32/objbase.h>

#define ERROR_REVISION_MISMATCH_ 1306L
#define ERROR_SERVICE_DOES_NOT_EXIST_ 1060L
#define ERROR_BAD_UNIT_ 20L
#define ERROR_CANCELLED_ 1223L
#define IOCTL_BTH_DISCONNECT_DEVICE 0x0041000C
#define IOCTL_BTH_AUTH_RESPONSE 0x00411004
#define IOCTL_BTH_AUTHENTICATE 0x00411010
#define IOCTL_BTH_GET_SERVICES 0x00411034
#define IOCTL_BTH_SET_LOCAL_SERVICE 0x0041104C
#define SERVICE_RECORD_SIZE (sizeof(void *) == 8 ? 1480 : 1404)
#define PROTOCOL_ENTRY_SIZE (sizeof(void *) == 8 ? 32 : 28)

typedef struct {
    BOOL Enabled;
    BLUETOOTH_ADDRESS btAddr;
    WCHAR szName[256];
    WCHAR szDeviceString[256];
} LOCAL_SERVICE_INFO;

typedef struct {
    DWORD dwSize;
    GUID classId;
    DWORD major;
    DWORD minor;
} PROFILE_DESCRIPTOR;

typedef struct {
    DWORD dwSize;
    GUID protocol;
    DWORD cbParams;
    PBYTE pParams;
} PROTOCOL_ENTRY;

typedef struct {
    DWORD dwSize;
    ULONG cNumOfClasses;
    PVOID prgClassOfDevices;
    PWSTR pszInfo;
    HWND hwndParent;
    BOOL fForceAuthentication;
    BOOL fShowAuthenticated;
    BOOL fShowRemembered;
    BOOL fShowUnknown;
    BOOL fAddNewDeviceWizard;
    BOOL fSkipServicesPage;
    PVOID pfnDeviceCallback;
    PVOID pvParam;
    DWORD cNumDevices;
    PBLUETOOTH_DEVICE_INFO pDevices;
} SELECT_DEVICES_PARAMS;

_Static_assert(sizeof(SELECT_DEVICES_PARAMS) == (sizeof(void *) == 8 ? 88 : 60), "the size of the picker parameters");

/* the walk handles are three pointers: the position, the end and a child. */
typedef struct {
    BYTE *at;
    BYTE *end;
    void *child;
} WALK;

/* ------------------------------------------------------------------ the service and the radios */

static const RPC_CLIENT_INTERFACE bthserv_interface = {
    sizeof(RPC_CLIENT_INTERFACE),
    {{0x2acb9d68, 0xb434, 0x4b3e, {0xb9, 0x66, 0xe0, 0x6b, 0x4b, 0x3a, 0x84, 0xcb}}, {1, 0}},
    {{0x8a885d04, 0x1ceb, 0x11c9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    nullptr, 0, nullptr, 0, nullptr, 0};

/* what opening the connection to the bluetooth support service gives. */
DWORD service_error(void)
{
    RPC_WSTR string = nullptr;
    RPC_BINDING_HANDLE binding = nullptr;
    RPC_STATUS status = RpcStringBindingComposeW(nullptr, (RPC_WSTR)L"ncalrpc", nullptr, nullptr, nullptr, &string);
    if (!status) {
        status = RpcBindingFromStringBindingW(string, &binding);
        if (!status)
            status = RpcEpResolveBinding(binding, (RPC_IF_HANDLE)&bthserv_interface);
    }
    if (binding)
        RpcBindingFree(&binding);
    if (string)
        RpcStringFreeW(&string);
    return status ? status : RPC_S_SERVER_UNAVAILABLE;
}

/* a call on a radio handle: only a radio device could answer, and none exists. */
static DWORD radio_call(HANDLE radio, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size)
{
    DWORD returned = 0;
    if (DeviceIoControl(radio, code, in, in_size, out, out_size, &returned, nullptr))
        return ERROR_NOT_SUPPORTED;
    return GetLastError();
}

DWORD WINAPI BluetoothDisconnectDevice(HANDLE radio, const BLUETOOTH_ADDRESS *address)
{
    if (radio)
        return radio_call(radio, IOCTL_BTH_DISCONNECT_DEVICE, (void *)address, 8, nullptr, 0);
    return ERROR_NOT_FOUND;
}

DWORD WINAPI BluetoothEnumerateInstalledServices(HANDLE radio, const BLUETOOTH_DEVICE_INFO *device, DWORD *count,
                                                 GUID *services)
{
    if (!device)
        return ERROR_INVALID_PARAMETER;
    if (radio) {
        BTH_ADDR address = device->Address.ullLong;
        DWORD out = 0;
        return radio_call(radio, IOCTL_BTH_GET_SERVICES, &address, 8, &out, 4);
    }
    return ERROR_NOT_FOUND;
}

DWORD WINAPI BluetoothSetLocalServiceInfo(HANDLE radio, const GUID *class_id, ULONG instance,
                                          const LOCAL_SERVICE_INFO *info)
{
    if (!class_id || !info)
        return ERROR_INVALID_PARAMETER;
    if (!radio)
        return ERROR_BAD_UNIT_;
    /* the names are copied into 256 character buffers. */
    if (lstrlenW(info->szName) >= 256 || lstrlenW(info->szDeviceString) >= 256)
        return 0x8007007A;
    BYTE in[0x422] = {0};
    memcpy(in, class_id, 16);
    memcpy(in + 16, &instance, 4);
    memcpy(in + 20, &info->Enabled, 4);
    memcpy(in + 24, &info->btAddr, 8);
    return radio_call(radio, IOCTL_BTH_SET_LOCAL_SERVICE, in, sizeof(in), nullptr, 0);
}

static DWORD send_response(HANDLE radio)
{
    if (radio) {
        BYTE in[0x3D8] = {0}, out = 0;
        return radio_call(radio, IOCTL_BTH_AUTH_RESPONSE, in, sizeof(in), &out, 1);
    }
    return ERROR_NO_MORE_ITEMS;
}

DWORD WINAPI BluetoothSendAuthenticationResponseEx(HANDLE radio, PVOID response)
{
    if (!response)
        return ERROR_INVALID_PARAMETER;
    return send_response(radio);
}

DWORD WINAPI BluetoothSendAuthenticationResponse(HANDLE radio, const BLUETOOTH_DEVICE_INFO *device, LPCWSTR passkey)
{
    int length = lstrlenW(passkey);
    if (length < 1 || length > 16)
        return ERROR_INVALID_PARAMETER;
    return send_response(radio);
}

/* the registrations are made through the service, so no registration handle ever exists. */
DWORD WINAPI BluetoothRegisterForAuthentication(const BLUETOOTH_DEVICE_INFO *device, PVOID *registration,
                                                PVOID callback, PVOID param)
{
    if (!registration || !callback)
        return ERROR_INVALID_PARAMETER;
    *registration = nullptr;
    return service_error();
}

DWORD WINAPI BluetoothRegisterForAuthenticationEx(const BLUETOOTH_DEVICE_INFO *device, PVOID *registration,
                                                  PVOID callback, PVOID param)
{
    return BluetoothRegisterForAuthentication(device, registration, callback, param);
}

BOOL WINAPI BluetoothUnregisterAuthentication(PVOID registration)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

DWORD WINAPI BluetoothRemoveDevice(const BLUETOOTH_ADDRESS *address)
{
    if (!address)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

/* the service cannot be reached, which windows takes for there being nothing to update. */
DWORD WINAPI BluetoothUpdateDeviceRecord(const BLUETOOTH_DEVICE_INFO *device)
{
    if (!device)
        return ERROR_INVALID_PARAMETER;
    if (device->dwSize != sizeof(*device))
        return ERROR_REVISION_MISMATCH_;
    return ERROR_SUCCESS;
}

DWORD WINAPI BluetoothSetServiceState(HANDLE radio, const BLUETOOTH_DEVICE_INFO *device, const GUID *service,
                                      DWORD flags)
{
    if (!device)
        return ERROR_INVALID_PARAMETER;
    return ERROR_SERVICE_DOES_NOT_EXIST_;
}

/* ------------------------------------------------------------------ services of a device */

PVOID WINAPI BluetoothFindFirstServiceEx(PVOID search, PVOID record, DWORD type)
{
    if (type > 3)
        return nullptr;
    if (!search || !record) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const DWORD *size = search;
    if (*size != 32 || *(DWORD *)record != SERVICE_RECORD_SIZE) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return nullptr;
    }
    void **fields = (void **)((BYTE *)search + sizeof(void *));
    if (!fields[0]) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    /* the search names a radio, or goes through them all and finds none. */
    if (fields[1]) {
        SetLastError(service_error());
        return nullptr;
    }
    BLUETOOTH_FIND_RADIO_PARAMS params = {sizeof(params)};
    HANDLE radio;
    BluetoothFindFirstRadio(&params, &radio);
    return nullptr;
}

PVOID WINAPI BluetoothFindFirstService(PVOID search, PVOID record)
{
    return BluetoothFindFirstServiceEx(search, record, 2);
}

BOOL WINAPI BluetoothFindNextService(PVOID find, PVOID record)
{
    if (!find) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (*(DWORD *)record != SERVICE_RECORD_SIZE) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return FALSE;
    }
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL WINAPI BluetoothFindServiceClose(PVOID find)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* ------------------------------------------------------------------ the sdp walkers */

/* the size of the value that follows the header byte of an element, by its size index. */
static ULONG value_size(const BYTE *p)
{
    static const BYTE sizes[] = {1, 2, 4, 8, 16};
    BYTE index = *p & 7;
    if (index < 5)
        return !index && *p < 8 ? 0 : sizes[index];
    if (index == 5)
        return p[1];
    if (index == 6)
        return (ULONG)p[1] << 8 | p[2];
    return (ULONG)p[1] << 24 | (ULONG)p[2] << 16 | (ULONG)p[3] << 8 | p[4];
}

/* the first byte after the header of an element, which is null for a nil element. */
static BYTE *value_start(BYTE *p, ULONG *size)
{
    switch (*p & 7) {
    case 0: *size = *p >= 8; return *p >= 8 ? p + 1 : nullptr;
    case 1: *size = 2; return p + 1;
    case 2: *size = 4; return p + 1;
    case 3: *size = 8; return p + 1;
    case 4: *size = 16; return p + 1;
    case 5: *size = p[1]; return p + 2;
    case 6: *size = (ULONG)p[1] << 8 | p[2]; return p + 3;
    default: *size = (ULONG)p[1] << 24 | (ULONG)p[2] << 16 | (ULONG)p[3] << 8 | p[4]; return p + 5;
    }
}

/* the contents of the sequence or element at start, or null when it runs past end. */
static BYTE *inner_record(BYTE *start, BYTE *end, ULONG *size)
{
    if (end - start > 0 && (ULONG_PTR)(end - start - 1) >= value_size(start)) {
        BYTE *value = value_start(start, size);
        if (!value || value + *size <= end)
            return value;
    }
    *size = 0;
    return nullptr;
}

/* the element after the one at start. */
static BYTE *next_record(BYTE *start, BYTE *end)
{
    if (end - start <= 0 || (ULONG_PTR)(end - start - 1) < value_size(start))
        return nullptr;
    BYTE *next;
    switch (*start & 7) {
    case 0: next = start + (*start >= 8 ? 2 : 1); break;
    case 1: next = start + 3; break;
    case 2: next = start + 5; break;
    case 3: next = start + 9; break;
    case 4: next = start + 17; break;
    case 5: next = start + start[1] + 2; break;
    case 6: next = start + ((ULONG)start[1] << 8 | start[2]) + 3; break;
    default: next = start + ((ULONG)start[1] << 24 | (ULONG)start[2] << 16 | (ULONG)start[3] << 8 | start[4]) + 5; break;
    }
    return next > end ? nullptr : next;
}

static void uuid_from_stream(const BYTE *p, int size, GUID *guid)
{
    memset(guid, 0, sizeof(*guid));
    if (size == 2 || size == 4) {
        ULONG value = 0;
        for (int i = 0; i < size; i++)
            value = value << 8 | p[i];
        guid->Data1 = value;
        guid->Data2 = 0;
        guid->Data3 = 0x1000;
        static const BYTE tail[8] = {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
        memcpy(guid->Data4, tail, 8);
    } else if (size == 16) {
        guid->Data1 = (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
        guid->Data2 = (USHORT)(p[4] << 8 | p[5]);
        guid->Data3 = (USHORT)(p[6] << 8 | p[7]);
        memcpy(guid->Data4, p + 8, 8);
    }
}

/* a uuid element becomes a guid, any other kind of element does not. */
static BOOL element_uuid(const BYTE *p, GUID *guid)
{
    static const int sizes[] = {1, 2, 4, 8, 16};
    if ((*p & 7) > 4)
        return FALSE;
    uuid_from_stream(p + 1, sizes[*p & 7], guid);
    return TRUE;
}

static BOOL version_bytes(const BYTE *p, const BYTE *end, ULONG *value)
{
    switch (*p & 7) {
    case 0:
        if (p + 2 > end)
            break;
        *value = p[1];
        return TRUE;
    case 1:
        if (p + 3 > end)
            break;
        *value = p[1] << 8 | p[2];
        return TRUE;
    case 2:
        if (p + 5 > end)
            break;
        *value = (ULONG)p[1] << 24 | (ULONG)p[2] << 16 | (ULONG)p[3] << 8 | p[4];
        return TRUE;
    }
    *value = 0;
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
}

static void *record_field(const void *record, int offset64, int offset32)
{
    return *(void **)((const BYTE *)record + (sizeof(void *) == 8 ? offset64 : offset32));
}

static BYTE *record_end(const void *record)
{
    BYTE *stream = record_field(record, 8, 4);
    return stream + *(const DWORD *)((const BYTE *)record + (sizeof(void *) == 8 ? 16 : 8));
}

static DWORD record_flags(const void *record)
{
    return *(const DWORD *)((const BYTE *)record + (sizeof(void *) == 8 ? 20 : 12));
}

static void free_walk(WALK *walk)
{
    HeapFree(GetProcessHeap(), 0, walk);
}

BOOL WINAPI BluetoothFindNextBrowseGroup(WALK *walk, GUID *group)
{
    if (!walk) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!walk->at) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (walk->at >= walk->end || (*walk->at & 0xF8) != 0x18 || !element_uuid(walk->at, group)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    walk->at = next_record(walk->at, walk->end);
    return TRUE;
}

WALK *WINAPI BluetoothFindFirstBrowseGroup(const void *record, GUID *group)
{
    if (!record || !group) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (*(const DWORD *)record != SERVICE_RECORD_SIZE) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return nullptr;
    }
    SetLastError(ERROR_INVALID_DATA);
    BYTE *list = record_field(record, 80, 52);
    if (!(record_flags(record) & 0x20) || !list || (*list & 0xF8) != 0x30)
        return nullptr;
    WALK *walk = HeapAlloc(GetProcessHeap(), 0, sizeof(*walk));
    if (!walk)
        return nullptr;
    DWORD error = ERROR_NOT_FOUND;
    ULONG size;
    BYTE *inner = inner_record(list, record_end(record), &size);
    walk->at = inner;
    walk->end = inner + size;
    walk->child = nullptr;
    if (inner) {
        if (BluetoothFindNextBrowseGroup(walk, group)) {
            SetLastError(ERROR_SUCCESS);
            return walk;
        }
        error = GetLastError();
    }
    free_walk(walk);
    SetLastError(error);
    return nullptr;
}

BOOL WINAPI BluetoothFindNextClassId(WALK *walk, GUID *class_id)
{
    if (!walk) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!walk->at || walk->at >= walk->end) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (!element_uuid(walk->at, class_id))
        return FALSE;
    walk->at = next_record(walk->at, walk->end);
    return TRUE;
}

WALK *WINAPI BluetoothFindFirstClassId(const void *record, GUID *class_id)
{
    if (!record) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (*(const DWORD *)record != SERVICE_RECORD_SIZE) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return nullptr;
    }
    SetLastError(ERROR_INVALID_DATA);
    BYTE *list = record_field(record, 32, 20);
    if (!(record_flags(record) & 2) || !list || (*list & 0xF8) != 0x30)
        return nullptr;
    WALK *walk = HeapAlloc(GetProcessHeap(), 0, sizeof(*walk));
    if (!walk)
        return nullptr;
    walk->at = nullptr;
    walk->end = nullptr;
    walk->child = nullptr;
    BYTE *end = record_end(record);
    DWORD error = ERROR_NOT_FOUND;
    ULONG size;
    if (end - list > 0 && (ULONG_PTR)(end - list - 1) >= value_size(list)) {
        BYTE *data = value_start(list, &size);
        if (data && data + size <= end) {
            walk->at = data;
            walk->end = data + size;
            error = ERROR_SUCCESS;
            if (data >= walk->end) {
                SetLastError(ERROR_NO_MORE_ITEMS);
                error = GetLastError();
            } else if (element_uuid(data, class_id)) {
                walk->at = next_record(walk->at, walk->end);
                SetLastError(ERROR_SUCCESS);
                return walk;
            } else {
                error = GetLastError();
            }
        }
    }
    free_walk(walk);
    SetLastError(error);
    return nullptr;
}

BOOL WINAPI BluetoothFindNextProfileDescriptor(WALK *walk, PROFILE_DESCRIPTOR *info)
{
    if (!walk) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (info->dwSize != 28) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return FALSE;
    }
    if (!walk->at) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    SetLastError(ERROR_INVALID_DATA);
    if (walk->at < walk->end) {
        ULONG size;
        BYTE *inner = inner_record(walk->at, walk->end, &size);
        if (inner) {
            BYTE *version = next_record(inner, walk->end);
            ULONG value;
            if (element_uuid(inner, &info->classId) && version && version_bytes(version, walk->end, &value)) {
                info->major = (value >> 8) & 0xFF;
                info->minor = value & 0xFF;
                walk->at = next_record(walk->at, walk->end);
                return TRUE;
            }
        }
    }
    return FALSE;
}

WALK *WINAPI BluetoothFindFirstProfileDescriptor(const void *record, PROFILE_DESCRIPTOR *info)
{
    if (!record) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (!info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (info->dwSize != 28) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return nullptr;
    }
    SetLastError(ERROR_INVALID_DATA);
    BYTE *list = record_field(record, 120, 76);
    if (!(record_flags(record) & 0x200) || !list || (*list & 0xF8) != 0x30)
        return nullptr;
    WALK *walk = HeapAlloc(GetProcessHeap(), 0, sizeof(*walk));
    if (!walk)
        return nullptr;
    ULONG size;
    BYTE *inner = inner_record(list, record_end(record), &size);
    walk->at = inner;
    walk->end = inner + size;
    walk->child = nullptr;
    BOOL ok = inner != nullptr;
    for (BYTE *at = inner; ok && at && at < walk->end; at = next_record(at, walk->end))
        ok = (*at & 0xF8) == 0x30;
    if (ok && BluetoothFindNextProfileDescriptor(walk, info))
        return walk;
    DWORD error = GetLastError();
    free_walk(walk);
    SetLastError(error);
    return nullptr;
}

BOOL WINAPI BluetoothFindNextProtocolEntry(WALK *walk, PROTOCOL_ENTRY *entry)
{
    if (!walk) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!walk->at) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    SetLastError(ERROR_INVALID_DATA);
    BOOL ok = FALSE;
    int remaining = 0;
    if (walk->at < walk->end && (*walk->at & 0xF8) == 0x30) {
        ULONG size;
        BYTE *inner = inner_record(walk->at, walk->end, &size);
        if (inner && (*inner & 0xF8) == 0x18 && element_uuid(inner, &entry->protocol)) {
            BYTE *params = next_record(inner, walk->end);
            entry->pParams = params;
            if (params)
                remaining = (int)(size + (inner - params));
            entry->cbParams = remaining;
            walk->at = next_record(walk->at, walk->end);
            ok = TRUE;
        }
    }
    return ok;
}

WALK *WINAPI BluetoothFindFirstProtocolEntry(const BYTE *const *stack, PROTOCOL_ENTRY *entry)
{
    if (!stack) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (!entry) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (entry->dwSize != PROTOCOL_ENTRY_SIZE) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return nullptr;
    }
    SetLastError(ERROR_INVALID_DATA);
    if ((*stack[0] & 0xF8) != 0x30)
        return nullptr;
    WALK *walk = HeapAlloc(GetProcessHeap(), 0, sizeof(*walk));
    if (!walk)
        return nullptr;
    DWORD error = ERROR_NOT_FOUND;
    ULONG size;
    BYTE *inner = inner_record((BYTE *)stack[0], (BYTE *)stack[1], &size);
    walk->at = inner;
    walk->end = inner + size;
    walk->child = nullptr;
    if (inner) {
        if (BluetoothFindNextProtocolEntry(walk, entry)) {
            SetLastError(ERROR_SUCCESS);
            return walk;
        }
        error = GetLastError();
    }
    free_walk(walk);
    SetLastError(error);
    return nullptr;
}

BOOL WINAPI BluetoothFindNextProtocolDescriptorStack(WALK *walk, WALK **stack)
{
    if (!walk) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!walk->at || walk->at >= walk->end) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    WALK *child = walk->child;
    if (!child) {
        child = HeapAlloc(GetProcessHeap(), 0, sizeof(*child));
        if (!child) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        walk->child = child;
    }
    *stack = child;
    child->at = walk->at;
    child->end = walk->end;
    walk->at = next_record(walk->at, walk->end);
    return TRUE;
}

WALK *WINAPI BluetoothFindFirstProtocolDescriptorStack(const void *record, WALK **stack)
{
    if (!record) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (*(const DWORD *)record != SERVICE_RECORD_SIZE) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return nullptr;
    }
    SetLastError(ERROR_INVALID_DATA);
    BYTE *list = record_field(record, 64, 44);
    if (!(record_flags(record) & 0x10) || !list || ((*list >> 3) != 6 && (*list >> 3) != 7))
        return nullptr;
    WALK *walk = HeapAlloc(GetProcessHeap(), 0, sizeof(*walk));
    if (!walk)
        return nullptr;
    ULONG size;
    BYTE *inner = inner_record(list, record_end(record), &size);
    if ((*list & 0xF8) != 0x30)
        list = inner;
    walk->at = list;
    walk->end = inner + size;
    walk->child = nullptr;
    if (list && !BluetoothFindNextProtocolDescriptorStack(walk, stack)) {
        DWORD error = GetLastError();
        free_walk(walk);
        SetLastError(error);
        return nullptr;
    }
    return walk;
}

static BOOL close_plain(PVOID find)
{
    if (!find) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, find);
    return TRUE;
}

BOOL WINAPI BluetoothFindBrowseGroupClose(PVOID find) { return close_plain(find); }
BOOL WINAPI BluetoothFindClassIdClose(PVOID find) { return close_plain(find); }
BOOL WINAPI BluetoothFindProfileDescriptorClose(PVOID find) { return close_plain(find); }
BOOL WINAPI BluetoothFindProtocolEntryClose(PVOID find) { return close_plain(find); }

BOOL WINAPI BluetoothFindProtocolDescriptorStackClose(WALK *walk)
{
    if (!walk) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (walk->child)
        HeapFree(GetProcessHeap(), 0, walk->child);
    free_walk(walk);
    return TRUE;
}

/* ------------------------------------------------------------------ strings of a record */

/* the code page of a character set by its iana mib number, zero for those not known. */
static UINT charset_code_page(USHORT mib)
{
    switch (mib) {
    case 3: return 20127;
    case 4: return 28591;
    case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: return 28592u + (mib - 5);
    case 17: return 932;
    case 18: return 51932;
    case 36: return 949;
    case 106: return CP_UTF8;
    case 1000: case 1014: case 1015: return 1200;
    case 1013: return 1201;
    case 2025: return 936;
    case 2026: return 950;
    case 2084: return 20866;
    case 2088: return 21866;
    default:
        if (mib >= 2250 && mib <= 2258)
            return 1250u + (mib - 2250);
        return 0;
    }
}

/* the character set and attribute number of the string for the language of the thread, from the language base list. */
static DWORD language_base(BYTE *record, ULONG length, USHORT *mib, USHORT *base)
{
    SDP_ELEMENT_DATA list;
    DWORD error = BluetoothSdpGetAttributeValue(record, length, 6, &list);
    if (error)
        return ERROR_SUCCESS;
    if (list.type != SDP_TYPE_SEQUENCE)
        return ERROR_INVALID_PARAMETER;
    WCHAR wanted[16] = {0};
    GetLocaleInfoW(GetThreadLocale(), LOCALE_SISO639LANGNAME, wanted, 16);
    HBLUETOOTH_CONTAINER_ELEMENT element = nullptr;
    SDP_ELEMENT_DATA triple[3];
    BOOL default_found = FALSE;
    USHORT default_mib = 0;
    for (;;) {
        BOOL complete = TRUE;
        for (int i = 0; i < 3; i++) {
            error = BluetoothSdpGetContainerElementData(list.data.sequence.value, list.data.sequence.length, &element,
                                                        &triple[i]);
            if (error == ERROR_NO_MORE_ITEMS && !i)
                goto done;
            if (error) {
                complete = FALSE;
                break;
            }
        }
        if (!complete)
            return error == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : error;
        WCHAR language[3] = {(WCHAR)(triple[0].data.uint16 >> 8), (WCHAR)(triple[0].data.uint16 & 0xFF), 0};
        if (!lstrcmpiW(language, wanted)) {
            *mib = triple[1].data.uint16;
            *base = triple[2].data.uint16;
            return ERROR_SUCCESS;
        }
        if (triple[2].data.uint16 == 0x100 && !default_found) {
            default_found = TRUE;
            default_mib = triple[1].data.uint16;
        }
    }
done:
    if (default_found) {
        *mib = default_mib;
        *base = 0x100;
    }
    return ERROR_SUCCESS;
}

typedef struct {
    USHORT mibeNum;
    USHORT attributeId;
} SDP_STRING_TYPE_DATA;

DWORD WINAPI BluetoothSdpGetString(LPBYTE record, ULONG length, const SDP_STRING_TYPE_DATA *string_data,
                                   USHORT offset, PWSTR buffer, PULONG characters)
{
    if (!record || !length || !characters)
        return ERROR_INVALID_PARAMETER;
    USHORT mib = 106, base = 256;
    if (string_data) {
        mib = string_data->mibeNum;
        base = string_data->attributeId;
    } else {
        DWORD error = language_base(record, length, &mib, &base);
        if (error)
            return error;
    }
    SDP_ELEMENT_DATA element;
    DWORD error = BluetoothSdpGetElementData(record, length, &element);
    if (error)
        return error;
    if (element.type != SDP_TYPE_SEQUENCE)
        return ERROR_INVALID_PARAMETER;
    error = BluetoothSdpGetAttributeValue(record, length, (USHORT)(base + offset), &element);
    if (error == ERROR_FILE_NOT_FOUND)
        return ERROR_FILE_NOT_FOUND;
    if (error)
        return ERROR_INVALID_PARAMETER;
    if (element.type != SDP_TYPE_STRING)
        return ERROR_INVALID_PARAMETER;
    ULONG size = element.data.string.length;
    if (!size) {
        *characters = 0;
        return ERROR_SUCCESS;
    }
    UINT code_page = charset_code_page(mib);
    if (!code_page)
        return ERROR_INVALID_DATA;
    const char *bytes = (const char *)element.data.string.value;
    int needed = MultiByteToWideChar(code_page, 0, bytes, (int)size, nullptr, 0);
    if (needed <= 0)
        return ERROR_INVALID_DATA;
    ULONG total = (ULONG)needed + (bytes[size - 1] != 0);
    BOOL small = !buffer || *characters < total;
    *characters = total;
    if (small)
        return ERROR_MORE_DATA;
    if (MultiByteToWideChar(code_page, 0, bytes, (int)size, buffer, needed) <= 0)
        return ERROR_INVALID_DATA;
    if (bytes[size - 1])
        buffer[needed] = 0;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ the control panel part */

static DWORD authenticate(HANDLE radio, DWORD count, BLUETOOTH_DEVICE_INFO *devices, PWSTR passkey, ULONG passkey_length)
{
    if ((passkey && passkey_length > 16) || (count && !devices))
        return ERROR_INVALID_PARAMETER;
    BOOL all = TRUE;
    for (DWORD i = 0; i < count; i++) {
        if (devices[i].dwSize != sizeof(*devices))
            return ERROR_INVALID_PARAMETER;
        if (!devices[i].fAuthenticated)
            all = FALSE;
    }
    if (all)
        return ERROR_NO_MORE_ITEMS;
    DWORD result = 0;
    for (DWORD i = 0; i < count; i++) {
        devices[i].fRemembered = FALSE;
        devices[i].fAuthenticated = FALSE;
        /* the wizard asks the radio, which is the first one when none is given. */
        DWORD error = radio ? radio_call(radio, IOCTL_BTH_AUTHENTICATE, nullptr, 0, nullptr, 0) : ERROR_NO_MORE_ITEMS;
        if (!result)
            result = error;
    }
    return result;
}

DWORD WINAPI BluetoothAuthenticateDevice(HWND parent, HANDLE radio, BLUETOOTH_DEVICE_INFO *device, PWSTR passkey,
                                         ULONG passkey_length)
{
    return authenticate(radio, 1, device, passkey, passkey_length);
}

DWORD WINAPI BluetoothAuthenticateDeviceEx(HWND parent, HANDLE radio, BLUETOOTH_DEVICE_INFO *device, PVOID oob,
                                           DWORD requirement)
{
    return authenticate(radio, 1, device, nullptr, 0);
}

DWORD WINAPI BluetoothAuthenticateMultipleDevices(HWND parent, HANDLE radio, DWORD count,
                                                  BLUETOOTH_DEVICE_INFO *devices)
{
    return authenticate(radio, count, devices, nullptr, 0);
}

/* an export that returns at once, with whatever the register held. */
DWORD WINAPI BluetoothAuthenticationAgent(PVOID a, PVOID b, PVOID c, PVOID d)
{
    return 0;
}

/* the dialog is a property sheet of the device, there is no device to show. */
BOOL WINAPI BluetoothDisplayDeviceProperties(HWND parent, BLUETOOTH_DEVICE_INFO *device)
{
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
}

static const CLSID class_resource_info = {0xd2e86c4f, 0xea06, 0x4a89, {0xbf, 0x00, 0xb4, 0x97, 0x06, 0xdb, 0x46, 0xe6}};
static const IID iid_resource_info = {0x03e05342, 0x07f2, 0x4dee, {0x98, 0xf7, 0xc4, 0xec, 0x68, 0x37, 0x0c, 0xf4}};

/* the display strings come from a shell object that is created after the category of the class is known. */
static HRESULT class_of_device_text(LPWSTR *text)
{
    *text = nullptr;
    void *object = nullptr;
    HRESULT hr = CoCreateInstance(&class_resource_info, nullptr, 0x17, &iid_resource_info, &object);
    if (hr < 0)
        return hr;
    return E_NOTIMPL;
}

HRESULT WINAPI BluetoothMapClassOfDeviceToIconPath(ULONG class_of_device, LPWSTR *path)
{
    return class_of_device_text(path);
}

HRESULT WINAPI BluetoothMapClassOfDeviceToString(ULONG class_of_device, BOOL plural, LPWSTR *text)
{
    return class_of_device_text(text);
}

/* the picker is a dialog the user closes without choosing. */
BOOL WINAPI BluetoothSelectDevices(SELECT_DEVICES_PARAMS *params)
{
    if (!params) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    params->pDevices = nullptr;
    if (params->dwSize != sizeof(*params)) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return FALSE;
    }
    SetLastError(ERROR_CANCELLED_);
    return FALSE;
}

BOOL WINAPI BluetoothSelectDevicesFree(SELECT_DEVICES_PARAMS *params)
{
    if (params->dwSize != sizeof(*params)) {
        SetLastError(ERROR_REVISION_MISMATCH_);
        return FALSE;
    }
    BOOL freed = params->pDevices != nullptr;
    if (freed)
        HeapFree(GetProcessHeap(), 0, params->pDevices);
    params->cNumDevices = 0;
    params->pDevices = nullptr;
    return freed;
}

/* the class objects of the applet are shell dialogs. */
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}
