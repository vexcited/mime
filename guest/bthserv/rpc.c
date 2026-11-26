#include "bthserv_private.h"
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include "bthserv/bthserv.h"

WINOLEAPI HRESULT WINAPI CoInitializeSecurity(PSECURITY_DESCRIPTOR sd, LONG count, void *services, void *reserved1,
                                              DWORD authn_level, DWORD imp_level, void *auth_list, DWORD capabilities,
                                              void *reserved3);

#define RPC_C_AUTHN_LEVEL_PKT_PRIVACY 6
#define RPC_C_IMP_LEVEL_IMPERSONATE 3
#define PROCESS_DUP_HANDLE_ 0x0040
#define ERROR_PRODUCT_VERSION_ 1638
#define DEVICE_INFO_SIZE 560
#define DEVICE_UPDATE_SIZE 0x72A
#define ADDRESS_NAME_CHARS 62
#define PARAMETERS L"System\\CurrentControlSet\\Services\\BTHPORT\\Parameters"

/* what the service keeps for one caller of BthServOpen. */
typedef struct CLIENT {
    DWORD ProcessId, ThreadId;
    HANDLE Process;
    struct REGISTRATION *Registrations;
    struct RECORD *Records;
    DWORD Discovery, Incoming;
} CLIENT;

/* an event of the caller that an authentication request for a device sets. */
typedef struct REGISTRATION {
    struct REGISTRATION *Next;
    ULONGLONG Address;
    BOOLEAN Legacy;
    ULONG Events;
    ULONGLONG ClientEvent;
    HANDLE Event;
} REGISTRATION;

typedef struct RECORD {
    struct RECORD *Next;
    ULONGLONG Handle;
} RECORD;

static CRITICAL_SECTION g_lock;

void BthServInitClients(void)
{
    static LONG once;
    if (!InterlockedExchange(&once, 1))
        InitializeCriticalSection(&g_lock);
}

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

static long hresult(DWORD error)
{
    return (long)error > 0 ? (long)((error & 0xFFFF) | 0x80070000) : (long)error;
}

/* every procedure wants a caller that authenticated with privacy, and tells the others so by an exception. */
static void authenticate(void *client)
{
    RPC_AUTHZ_HANDLE privileges = nullptr;
    ULONG level = 0;
    if (!client)
        RpcRaiseException(ERROR_INVALID_PARAMETER);
    if (RpcBindingInqAuthClientW(nullptr, &privileges, nullptr, &level, nullptr, nullptr) ||
        level < RPC_C_AUTHN_LEVEL_PKT_PRIVACY)
        RpcRaiseException(ERROR_ACCESS_DENIED);
}

/* the radio the calls go to. mime has no bluetooth radio, so there is none to open and every call that needs it ends
 * as on a windows machine without one, with ERROR_INVALID_HANDLE. */
static DWORD radio_call(void)
{
    return ERROR_INVALID_HANDLE;
}

error_status_t WINAPI BthServ_Open_s(handle_t h, BTHSERV_HANDLE *client, long *result, BTHSERV_CLIENT_ID id)
{
    authenticate(client);
    *client = nullptr;
    CLIENT *c = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*c));
    if (!c) {
        *result = hresult(ERROR_OUTOFMEMORY);
        return RPC_S_OK;
    }
    c->ProcessId = id.ProcessId;
    c->ThreadId = id.ThreadId;
    if (RpcImpersonateClient(nullptr)) {
        LocalFree(c);
        *result = hresult(ERROR_ACCESS_DENIED);
        return RPC_S_OK;
    }
    c->Process = OpenProcess(PROCESS_DUP_HANDLE_, FALSE, c->ProcessId);
    RpcRevertToSelf();
    if (!c->Process) {
        LocalFree(c);
        *result = hresult(ERROR_BAD_ARGUMENTS);
        return RPC_S_OK;
    }
    *client = c;
    *result = 0;
    return RPC_S_OK;
}

static DWORD set_discovery(CLIENT *c, BOOL enable)
{
    if (enable) {
        if (c->Discovery)
            return ERROR_INVALID_PARAMETER;
        c->Discovery++;
        return ERROR_SUCCESS;
    }
    if (!c->Discovery)
        return ERROR_INVALID_PARAMETER;
    c->Discovery--;
    return ERROR_SUCCESS;
}

static DWORD set_incoming(CLIENT *c, BOOL enable)
{
    if (enable) {
        if (c->Incoming)
            return ERROR_INVALID_PARAMETER;
        c->Incoming++;
        return ERROR_SUCCESS;
    }
    if (!c->Incoming)
        return ERROR_INVALID_PARAMETER;
    c->Incoming--;
    return ERROR_SUCCESS;
}

static void close_client(CLIENT *c)
{
    EnterCriticalSection(&g_lock);
    set_discovery(c, FALSE);
    set_incoming(c, FALSE);
    while (c->Registrations) {
        REGISTRATION *r = c->Registrations;
        c->Registrations = r->Next;
        CloseHandle(r->Event);
        LocalFree(r);
    }
    while (c->Records) {
        RECORD *r = c->Records;
        c->Records = r->Next;
        LocalFree(r);
    }
    LeaveCriticalSection(&g_lock);
    CloseHandle(c->Process);
    LocalFree(c);
}

void WINAPI BTHSERV_HANDLE_rundown(BTHSERV_HANDLE client)
{
    if (client)
        close_client(client);
}

error_status_t WINAPI BthServ_Close_s(BTHSERV_HANDLE *client, long *result)
{
    if (!client || !*client) {
        *result = hresult(ERROR_BAD_ARGUMENTS);
        return RPC_S_OK;
    }
    close_client(*client);
    *client = nullptr;
    *result = 0;
    return RPC_S_OK;
}

/*
 * two registrations are for the same requests when they name the same device and kind, and for the kind of the
 * extended callback when their transports, bits 0xC000 of the events, overlap. a caller has one of each.
 */
static BOOL same_requests(const REGISTRATION *a, ULONGLONG address, BOOLEAN legacy, ULONG events)
{
    if (a->Address != address || a->Legacy != legacy)
        return FALSE;
    return legacy || (a->Events & events & 0xC000);
}

error_status_t WINAPI BthServ_RegisterAuthenticationEvent_s(BTHSERV_HANDLE client, long *result,
                                                            unsigned long long *address, unsigned long long event,
                                                            BLUETOOTH_AUTH_PARAMS *params)
{
    authenticate(client);
    CLIENT *c = client;
    HANDLE copy = nullptr;
    *result = hresult(ERROR_INVALID_HANDLE);
    if (!DuplicateHandle(c->Process, (HANDLE)(ULONG_PTR)event, GetCurrentProcess(), &copy, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
        return RPC_S_OK;
    BOOLEAN legacy = params->Flags != 0;
    REGISTRATION *r = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*r));
    EnterCriticalSection(&g_lock);
    for (REGISTRATION *other = c->Registrations; other && r; other = other->Next) {
        if (same_requests(other, *address, legacy, params->Events)) {
            LocalFree(r);
            r = nullptr;
        }
    }
    if (r) {
        r->Address = *address;
        r->Legacy = legacy;
        r->Events = params->Events;
        r->ClientEvent = event;
        r->Event = copy;
        r->Next = c->Registrations;
        c->Registrations = r;
        *result = 0;
    }
    LeaveCriticalSection(&g_lock);
    if (!r)
        CloseHandle(copy);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_DeregisterAuthenticationEvent_s(BTHSERV_HANDLE client, long *result,
                                                              unsigned long long *address, unsigned long long event)
{
    authenticate(client);
    CLIENT *c = client;
    *result = hresult(ERROR_NOT_FOUND);
    EnterCriticalSection(&g_lock);
    for (REGISTRATION **p = &c->Registrations; *p; p = &(*p)->Next) {
        REGISTRATION *r = *p;
        if (r->ClientEvent != event)
            continue;
        *p = r->Next;
        CloseHandle(r->Event);
        LocalFree(r);
        *result = 0;
        break;
    }
    LeaveCriticalSection(&g_lock);
    return RPC_S_OK;
}

/* a request is queued for a caller by the radio's events, so without a radio none ever waits. */
error_status_t WINAPI BthServ_GetAuthRequest_s(BTHSERV_HANDLE client, long *result,
                                               BTHSERV_AUTHENTICATION_REQUEST *request)
{
    authenticate(client);
    *result = hresult(ERROR_NOT_FOUND);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_GetDeviceInfo_s(BTHSERV_HANDLE client, long *result, BTHSERV_QUERY_TYPE type,
                                              unsigned long long *address, BTHSERV_DEVICE_INFO *info)
{
    authenticate(client);
    if ((ULONG)type > BthServQueryRemote)
        *result = hresult(ERROR_BAD_ARGUMENTS);
    else if (info->dwSize != DEVICE_INFO_SIZE)
        *result = hresult(ERROR_PRODUCT_VERSION_);
    else
        *result = hresult(radio_call());
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_SetDeviceName_s(BTHSERV_HANDLE client, long *result, unsigned long long *address,
                                              unsigned short (*name)[248])
{
    authenticate(client);
    *result = hresult(radio_call());
    return RPC_S_OK;
}

/* an inquiry is asked of the radio first, then the list of its devices is read, sized by a first call. */
error_status_t WINAPI BthServ_GetDeviceList_s(BTHSERV_HANDLE client, long *result, long inquire, char timeout,
                                              unsigned long size, byte *devices, unsigned long *needed)
{
    authenticate(client);
    *needed = 0;
    if (inquire && !timeout)
        *result = hresult(ERROR_BAD_ARGUMENTS);
    else
        *result = hresult(radio_call());
    return RPC_S_OK;
}

/* the size in bytes of the data element at p, header included, or 0 when it does not fit in the size left. */
static ULONG element_size(const BYTE *p, ULONG left)
{
    static const BYTE fixed[8] = {1, 2, 4, 8, 16};
    if (!left)
        return 0;
    ULONG type = p[0] >> 3, index = p[0] & 7, head = 1, body;
    if (type > 8)
        return 0;
    if (!type) {
        body = 0;
        if (index)
            return 0;
    } else if (index < 5) {
        body = fixed[index];
    } else {
        ULONG bytes = 1u << (index - 5);
        if (left < 1 + bytes)
            return 0;
        body = 0;
        for (ULONG i = 0; i < bytes; i++)
            body = body << 8 | p[1 + i];
        head += bytes;
    }
    return body <= left - head ? head + body : 0;
}

/*
 * a service record is one sequence that fills the stream, of pairs of a 16 bit attribute id and a well formed value.
 * handle receives the value of attribute 0, the record handle, when the record has it as a 32 bit number.
 */
static BOOL record_valid(const BYTE *record, ULONG size, BOOL *has_handle, ULONG *handle)
{
    *has_handle = FALSE;
    if (element_size(record, size) != size || record[0] >> 3 != 6)
        return FALSE;
    ULONG head = 1 + ((record[0] & 7) >= 5 ? 1u << ((record[0] & 7) - 5) : 0);
    const BYTE *p = record + head, *end = record + size;
    while (p < end) {
        if (end - p < 3 || p[0] != 0x09)
            return FALSE;
        USHORT id = (USHORT)(p[1] << 8 | p[2]);
        p += 3;
        ULONG value = element_size(p, (ULONG)(end - p));
        if (!value)
            return FALSE;
        if (!id && p[0] == 0x0A) {
            *has_handle = TRUE;
            *handle = (ULONG)p[1] << 24 | (ULONG)p[2] << 16 | (ULONG)p[3] << 8 | p[4];
        }
        p += value;
    }
    return TRUE;
}

/*
 * the record of a service of a device is kept for the next start of the stack, as a value named by the record handle
 * under the key of the device. the radio is not asked.
 */
error_status_t WINAPI BthServ_ActivateService_s(BTHSERV_HANDLE client, long *result, unsigned long long *address,
                                                unsigned long size, byte *record)
{
    authenticate(client);
    BOOL has_handle;
    ULONG handle = 0;
    if (!record || size < 2 || !record_valid(record, size, &has_handle, &handle)) {
        *result = hresult(ERROR_INVALID_PARAMETER);
        return RPC_S_OK;
    }
    WCHAR path[160], name[12];
    _snwprintf(path, RTL_NUMBER_OF(path), PARAMETERS L"\\Devices\\%012llX\\CachedServices", *address & 0xFFFFFFFFFFFFull);
    HKEY key = nullptr;
    DWORD error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    if (!error && !has_handle)
        error = ERROR_NOT_FOUND;
    if (!error) {
        _snwprintf(name, RTL_NUMBER_OF(name), L"%08x", handle);
        error = RegSetValueExW(key, name, 0, REG_BINARY, record, size);
    }
    if (key)
        RegCloseKey(key);
    *result = hresult(error);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_UpdateService_s(BTHSERV_HANDLE client, long *result, unsigned long size, byte *update,
                                              long wait)
{
    authenticate(client);
    *result = hresult(size < DEVICE_UPDATE_SIZE ? ERROR_INSUFFICIENT_BUFFER : radio_call());
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_GetSdpRecord_s(BTHSERV_HANDLE client, long *result, unsigned long long *address,
                                             BTHSERV_QUERY_TYPE kind, BTHSERV_QUERY_TYPE type, unsigned long size,
                                             byte *records, unsigned long *needed)
{
    authenticate(client);
    *needed = 0;
    if (kind || (ULONG)type > BthServQueryAny)
        *result = hresult(ERROR_BAD_ARGUMENTS);
    else
        *result = hresult(radio_call());
    return RPC_S_OK;
}

/* a record the radio took is listed for the caller, and goes with it. without a radio none is taken. */
static DWORD set_record(CLIENT *c, unsigned long size, const byte *record, unsigned long long *handle)
{
    *handle = 0;
    if (!record || !size)
        return ERROR_BAD_ARGUMENTS;
    return radio_call();
}

error_status_t WINAPI BthServ_SetSdpRecord_s(BTHSERV_HANDLE client, long *result, unsigned long size, byte *record,
                                             unsigned long long *handle)
{
    authenticate(client);
    *result = hresult(set_record(client, size, record, handle));
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_SetSdpRecordWithInfo_s(BTHSERV_HANDLE client, long *result, unsigned long security,
                                                     unsigned long options, unsigned long flags, unsigned long size,
                                                     byte *record, unsigned long long *handle)
{
    authenticate(client);
    *handle = 0;
    if (size >= 0xFFFFFFF0)
        *result = hresult(ERROR_ARITHMETIC_OVERFLOW);
    else
        *result = hresult(radio_call());
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_RemoveSdpRecord_s(BTHSERV_HANDLE client, long *result, unsigned long long handle)
{
    authenticate(client);
    CLIENT *c = client;
    DWORD error = ERROR_BAD_ARGUMENTS;
    EnterCriticalSection(&g_lock);
    for (RECORD **p = &c->Records; *p; p = &(*p)->Next) {
        RECORD *r = *p;
        if (r->Handle != handle)
            continue;
        *p = r->Next;
        LocalFree(r);
        error = radio_call();
        break;
    }
    LeaveCriticalSection(&g_lock);
    *result = hresult(error);
    return RPC_S_OK;
}

/*
 * windows' service counts the callers that asked for discovery and tells the radio at the first and after the last.
 * with no radio to tell, what is left is the state of each caller, and the call succeeds, which is what a windows
 * machine without a radio answers. a caller that asks twice, or takes back what it did not ask, gets
 * ERROR_INVALID_PARAMETER.
 */
error_status_t WINAPI BthServ_EnableDiscovery_s(BTHSERV_HANDLE client, long *result, long enable)
{
    authenticate(client);
    EnterCriticalSection(&g_lock);
    *result = hresult(set_discovery(client, enable != 0));
    LeaveCriticalSection(&g_lock);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_EnableIncomingConnections_s(BTHSERV_HANDLE client, long *result, long enable)
{
    authenticate(client);
    EnterCriticalSection(&g_lock);
    *result = hresult(set_incoming(client, enable != 0));
    LeaveCriticalSection(&g_lock);
    return RPC_S_OK;
}

static const GUID CLSID_TaskScheduler_ = {0x0F87369F, 0xA4E5, 0x4CFC, {0xBD, 0x3E, 0x73, 0xE6, 0x15, 0x45, 0x72, 0xDD}};
static const GUID IID_ITaskService_ = {0x2FABA4C7, 0x4DA9, 0x4013, {0x96, 0x97, 0x20, 0xCC, 0x3F, 0xD4, 0x0F, 0x85}};

/* the methods of the task scheduler's objects the removal uses, by their places in the tables. */
#define SLOT(object, index) ((*(void ***)(object))[index])
#define RELEASE(object) ((ULONG(WINAPI *)(void *))SLOT(object, 2))(object)
typedef HRESULT(WINAPI *TASK_CONNECT)(void *service, VARIANT server, VARIANT user, VARIANT domain, VARIANT password);
typedef HRESULT(WINAPI *TASK_GET)(void *object, BSTR path, void **out);
typedef HRESULT(WINAPI *TASK_RUN)(void *task, VARIANT parameters, void **running);

/*
 * the removal of a paired device is done by the task Microsoft\Windows\Bluetooth\UninstallDeviceTask, which the
 * service runs with the address and waits fifteen seconds for, on an event the task sets. a wait that ends without
 * the event is not an error.
 */
static HRESULT uninstall(const WCHAR *address)
{
    WCHAR name[96];
    _snwprintf(name, RTL_NUMBER_OF(name), L"Global\\BTH_UNINSTALL_DEVICE_%ls", address);
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, name);
    if (!event)
        return hresult(GetLastError());
    void *service = nullptr, *folder = nullptr, *task = nullptr, *running = nullptr;
    BSTR root = nullptr, path = nullptr, argument = nullptr;
    HRESULT hr = CoInitializeEx(nullptr, 0);
    BOOL initialized = hr >= 0;
    if (initialized) {
        hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, 0, nullptr);
        if (hr == RPC_E_TOO_LATE)
            hr = 0;
    }
    if (hr >= 0)
        hr = CoCreateInstance(&CLSID_TaskScheduler_, nullptr, CLSCTX_INPROC_SERVER, &IID_ITaskService_, &service);
    if (hr >= 0) {
        VARIANT empty;
        memset(&empty, 0, sizeof(empty));
        hr = ((TASK_CONNECT)SLOT(service, 10))(service, empty, empty, empty, empty);
    }
    if (hr >= 0 && !(root = SysAllocString(L"\\")))
        hr = E_OUTOFMEMORY;
    if (hr >= 0)
        hr = ((TASK_GET)SLOT(service, 7))(service, root, &folder);
    if (hr >= 0 && !(path = SysAllocString(L"Microsoft\\Windows\\Bluetooth\\UninstallDeviceTask")))
        hr = E_OUTOFMEMORY;
    if (hr >= 0)
        hr = ((TASK_GET)SLOT(folder, 13))(folder, path, &task);
    if (hr >= 0 && !(argument = SysAllocString(address)))
        hr = E_OUTOFMEMORY;
    if (hr >= 0) {
        VARIANT parameters;
        memset(&parameters, 0, sizeof(parameters));
        parameters.vt = VT_BSTR;
        parameters.bstrVal = argument;
        hr = ((TASK_RUN)SLOT(task, 12))(task, parameters, &running);
    }
    if (hr >= 0) {
        WaitForSingleObject(event, 15000);
        hr = 0;
    }
    if (running)
        RELEASE(running);
    if (task)
        RELEASE(task);
    if (folder)
        RELEASE(folder);
    if (service)
        RELEASE(service);
    SysFreeString(argument);
    SysFreeString(path);
    SysFreeString(root);
    if (initialized)
        CoUninitialize();
    CloseHandle(event);
    return hr;
}

error_status_t WINAPI BthServ_UninstallDevice_s(BTHSERV_HANDLE client, long *result, unsigned short (*address)[62])
{
    authenticate(client);
    WCHAR text[ADDRESS_NAME_CHARS];
    DWORD length = 0;
    while (length < ADDRESS_NAME_CHARS && (*address)[length]) {
        text[length] = (*address)[length];
        length++;
    }
    if (length == ADDRESS_NAME_CHARS) {
        *result = E_INVALIDARG;
        return RPC_S_OK;
    }
    text[length] = 0;
    *result = uninstall(text);
    return RPC_S_OK;
}

/* the four procedures of the extended inquiry response records are not served by windows either. */
error_status_t WINAPI BthServ_EirGetRecords_s(BTHSERV_HANDLE client, long *result, unsigned long long *address,
                                              BTHSERV_QUERY_TYPE type, unsigned long size, byte *records,
                                              unsigned long *needed)
{
    *needed = 0;
    authenticate(client);
    *result = hresult(ERROR_CALL_NOT_IMPLEMENTED);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_EirSetRecord_s(BTHSERV_HANDLE client, long *result, unsigned long size, byte *record,
                                             unsigned long long *handle)
{
    *handle = 0;
    authenticate(client);
    *result = hresult(ERROR_CALL_NOT_IMPLEMENTED);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_EirUpdateRecord_s(BTHSERV_HANDLE client, long *result, unsigned long size, byte *record,
                                                unsigned long long handle)
{
    authenticate(client);
    *result = hresult(ERROR_CALL_NOT_IMPLEMENTED);
    return RPC_S_OK;
}

error_status_t WINAPI BthServ_EirRemoveRecord_s(BTHSERV_HANDLE client, long *result, unsigned long long handle)
{
    authenticate(client);
    *result = hresult(ERROR_CALL_NOT_IMPLEMENTED);
    return RPC_S_OK;
}

/*
 * the gatt interface of a low energy device, which Microsoft.Bluetooth.Service.dll serves in the process of bthserv on
 * windows 11. a call names the device by the token its driver gave for a handle of it, and mime has no low energy
 * device, so no token is known. measured on a machine without a radio: every call succeeds and gives
 * HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE) as its result, with every output zero.
 */
#define UNKNOWN_TOKEN ((long)0x80070006)

#define NO_DEVICE()                                                                                                    \
    do {                                                                                                               \
        *result = UNKNOWN_TOKEN;                                                                                       \
        return 0;                                                                                                      \
    } while (0)

error_status_t WINAPI BthGatt_BluetoothGattGetServicesCount_s(handle_t h, GUID *token, unsigned long flags,
                                                              unsigned short *count, long *result)
{
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetServices_s(handle_t h, GUID *token, unsigned short capacity,
                                                         unsigned long flags, RPC_BTH_LE_GATT_SERVICE *items,
                                                         unsigned short *count, long *result)
{
    memset(items, 0, capacity * sizeof(*items));
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetIncludedServicesCount_s(handle_t h, GUID *token,
                                                                      RPC_BTH_LE_GATT_SERVICE *parent,
                                                                      unsigned long flags, unsigned short *count,
                                                                      long *result)
{
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetIncludedServices_s(handle_t h, GUID *token,
                                                                 RPC_BTH_LE_GATT_SERVICE *parent,
                                                                 unsigned short capacity, unsigned long flags,
                                                                 RPC_BTH_LE_GATT_SERVICE *items,
                                                                 unsigned short *count, long *result)
{
    memset(items, 0, capacity * sizeof(*items));
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetCharacteristicsCount_s(handle_t h, GUID *token,
                                                                     RPC_BTH_LE_GATT_SERVICE *service,
                                                                     unsigned long flags, unsigned short *count,
                                                                     long *result)
{
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetCharacteristics_s(handle_t h, GUID *token,
                                                                RPC_BTH_LE_GATT_SERVICE *service,
                                                                unsigned short capacity, unsigned long flags,
                                                                RPC_BTH_LE_GATT_CHARACTERISTIC *items,
                                                                unsigned short *count, long *result)
{
    memset(items, 0, capacity * sizeof(*items));
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetDescriptorsCount_s(handle_t h, GUID *token,
                                                                 RPC_BTH_LE_GATT_CHARACTERISTIC *characteristic,
                                                                 unsigned long flags, unsigned short *count,
                                                                 long *result)
{
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetDescriptors_s(handle_t h, GUID *token,
                                                            RPC_BTH_LE_GATT_CHARACTERISTIC *characteristic,
                                                            unsigned short capacity, unsigned long flags,
                                                            RPC_BTH_LE_GATT_DESCRIPTOR *items, unsigned short *count,
                                                            long *result)
{
    memset(items, 0, capacity * sizeof(*items));
    *count = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetCharacteristicValueSize_s(handle_t h, GUID *token,
                                                                        RPC_BTH_LE_GATT_CHARACTERISTIC *characteristic,
                                                                        unsigned long flags, unsigned short *size,
                                                                        long *result)
{
    *size = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetCharacteristicValue_s(handle_t h, GUID *token,
                                                                    RPC_BTH_LE_GATT_CHARACTERISTIC *characteristic,
                                                                    unsigned long capacity, unsigned long flags,
                                                                    char *data, unsigned short *size, long *result)
{
    memset(data, 0, capacity);
    *size = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetDescriptorValueSize_s(handle_t h, GUID *token,
                                                                    RPC_BTH_LE_GATT_DESCRIPTOR *descriptor,
                                                                    unsigned long flags, unsigned short *size,
                                                                    long *result)
{
    *size = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetDescriptorValue_s(handle_t h, GUID *token,
                                                                RPC_BTH_LE_GATT_DESCRIPTOR *descriptor,
                                                                unsigned long capacity, unsigned long flags,
                                                                RPC_BTH_LE_GATT_DESCRIPTOR_VALUE_NO_DATA *value,
                                                                char *data, unsigned short *size, long *result)
{
    memset(value, 0, sizeof(*value));
    memset(data, 0, capacity);
    *size = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattSetCharacteristicValue_s(handle_t h, GUID *token,
                                                                    RPC_BTH_LE_GATT_CHARACTERISTIC *characteristic,
                                                                    unsigned long size, char *data,
                                                                    unsigned long long context, unsigned long flags,
                                                                    long *result)
{
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattSetDescriptorValue_s(handle_t h, GUID *token,
                                                                RPC_BTH_LE_GATT_DESCRIPTOR *descriptor,
                                                                unsigned long size,
                                                                RPC_BTH_LE_GATT_DESCRIPTOR_VALUE_NO_DATA *value,
                                                                char *data, unsigned long flags, long *result)
{
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattBeginReliableWrite_s(handle_t h, GUID *token, unsigned long flags,
                                                                unsigned long long *context, long *result)
{
    *context = 0;
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattEndReliableWrite_s(handle_t h, GUID *token, unsigned long long context,
                                                              unsigned long flags, long *result)
{
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattAbortReliableWrite_s(handle_t h, GUID *token, unsigned long long context,
                                                                unsigned long flags, long *result)
{
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattRegisterEvent_s(handle_t h, GUID *token, unsigned short count,
                                                           RPC_BTH_LE_GATT_CHARACTERISTIC *characteristics,
                                                           unsigned long flags, HANDLE *event, GUID *registration,
                                                           long *result)
{
    *event = nullptr;
    memset(registration, 0, sizeof(*registration));
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattUnregisterEvent_s(handle_t h, GUID *token, GUID *registration,
                                                             unsigned long flags, long *result)
{
    NO_DEVICE();
}

error_status_t WINAPI BthGatt_BluetoothGattGetEvents_s(handle_t h, GUID *token, GUID *registration,
                                                       unsigned short *count,
                                                       RPC_BLUETOOTH_GATT_VALUE_CHANGED_EVENT_HEADER **events,
                                                       unsigned short *size, char **data, long *result)
{
    *count = 0;
    *events = nullptr;
    *size = 0;
    *data = nullptr;
    NO_DEVICE();
}
