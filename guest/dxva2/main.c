/*
 * dxva2.dll: the device manager that shares a Direct3D 9 device between video components, and the monitor
 * configuration calls. No video is decoded on the gpu and no monitor answers DDC/CI, so the services and the monitor
 * settings are refused the way Windows refuses them on such hardware. The answers were read from dxva2.dll of
 * Windows 11 with a test program.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <rtl.h>

#define DXVA2_E_NOT_INITIALIZED ((HRESULT)0x80041000L)
#define DXVA2_E_NEW_VIDEO_DEVICE ((HRESULT)0x80041001L)
#define DXVA2_E_VIDEO_DEVICE_LOCKED ((HRESULT)0x80041002L)
#define E_HANDLE_ ((HRESULT)0x80070006L)
#define ERROR_INVALID_MONITOR_HANDLE_ 1461
#define ERROR_GRAPHICS_INVALID_PHYSICAL_MONITOR_HANDLE_ 0xC026258C
#define ERROR_GRAPHICS_INVALID_POINTER_ 0xC02625E6
#define ERROR_GRAPHICS_OPM_NOT_SUPPORTED_ ((HRESULT)0xC0262500L)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* ------------------------------------------------------------------ the device manager */

typedef struct IDirect3DDeviceManager9 IDirect3DDeviceManager9;
typedef struct IDirect3DDeviceManager9Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDirect3DDeviceManager9 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDirect3DDeviceManager9 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDirect3DDeviceManager9 *This);
    HRESULT(STDMETHODCALLTYPE *ResetDevice)(IDirect3DDeviceManager9 *This, IUnknown *device, UINT token);
    HRESULT(STDMETHODCALLTYPE *OpenDeviceHandle)(IDirect3DDeviceManager9 *This, HANDLE *handle);
    HRESULT(STDMETHODCALLTYPE *CloseDeviceHandle)(IDirect3DDeviceManager9 *This, HANDLE handle);
    HRESULT(STDMETHODCALLTYPE *TestDevice)(IDirect3DDeviceManager9 *This, HANDLE handle);
    HRESULT(STDMETHODCALLTYPE *LockDevice)(IDirect3DDeviceManager9 *This, HANDLE handle, IUnknown **device, BOOL block);
    HRESULT(STDMETHODCALLTYPE *UnlockDevice)(IDirect3DDeviceManager9 *This, HANDLE handle, BOOL save_state);
    HRESULT(STDMETHODCALLTYPE *GetVideoService)(IDirect3DDeviceManager9 *This, HANDLE handle, REFIID riid, void **service);
} IDirect3DDeviceManager9Vtbl;
struct IDirect3DDeviceManager9 {
    const IDirect3DDeviceManager9Vtbl *lpVtbl;
};

static const GUID IID_IDirect3DDeviceManager9 = {0xa0cade0f, 0x06d5, 0x4cf4, {0xa1, 0xc7, 0xf3, 0xcd, 0xd7, 0x25, 0xaa, 0x75}};

/* a handle names the device that was current when it was opened. */
typedef struct DEVICE_HANDLE {
    ULONG_PTR Value;
    ULONG Generation;
} DEVICE_HANDLE;

typedef struct MANAGER {
    IDirect3DDeviceManager9 Interface;
    LONG References;
    UINT Token;
    CRITICAL_SECTION Lock;
    CONDITION_VARIABLE Unlocked;
    IUnknown *Device;
    ULONG Generation;
    DEVICE_HANDLE *Handles;
    ULONG Count, Room;
    ULONG_PTR NextHandle, Holder; /* Holder is the handle the device is locked through, or 0. */
} MANAGER;

static DEVICE_HANDLE *find_handle(MANAGER *m, HANDLE handle)
{
    for (ULONG i = 0; i < m->Count; i++)
        if (handle && m->Handles[i].Value == (ULONG_PTR)handle)
            return &m->Handles[i];
    return nullptr;
}

/* what every call that takes a handle answers first. the lock is held. */
static HRESULT check_handle(MANAGER *m, HANDLE handle)
{
    DEVICE_HANDLE *h = find_handle(m, handle);
    if (!h)
        return E_HANDLE_;
    return h->Generation == m->Generation ? S_OK : DXVA2_E_NEW_VIDEO_DEVICE;
}

static HRESULT STDMETHODCALLTYPE manager_query_interface(IDirect3DDeviceManager9 *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDirect3DDeviceManager9))
        return E_NOINTERFACE;
    InterlockedIncrement(&((MANAGER *)This)->References);
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE manager_add_ref(IDirect3DDeviceManager9 *This)
{
    return (ULONG)InterlockedIncrement(&((MANAGER *)This)->References);
}

static ULONG STDMETHODCALLTYPE manager_release(IDirect3DDeviceManager9 *This)
{
    MANAGER *m = (MANAGER *)This;
    ULONG left = (ULONG)InterlockedDecrement(&m->References);
    if (!left) {
        if (m->Device)
            m->Device->lpVtbl->Release(m->Device);
        DeleteCriticalSection(&m->Lock);
        HeapFree(GetProcessHeap(), 0, m->Handles);
        HeapFree(GetProcessHeap(), 0, m);
    }
    return left;
}

/* a new device makes every handle that is open stale, so their holders learn of it and open a new one. */
static HRESULT STDMETHODCALLTYPE manager_reset_device(IDirect3DDeviceManager9 *This, IUnknown *device, UINT token)
{
    MANAGER *m = (MANAGER *)This;
    if (!device || token != m->Token)
        return E_INVALIDARG;
    device->lpVtbl->AddRef(device);
    EnterCriticalSection(&m->Lock);
    IUnknown *old = m->Device;
    m->Device = device;
    m->Generation++;
    LeaveCriticalSection(&m->Lock);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_open_device_handle(IDirect3DDeviceManager9 *This, HANDLE *handle)
{
    MANAGER *m = (MANAGER *)This;
    if (!handle)
        return E_POINTER;
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->Lock);
    if (!m->Device) {
        hr = DXVA2_E_NOT_INITIALIZED;
    } else {
        if (m->Count == m->Room) {
            ULONG room = m->Room ? m->Room * 2 : 8;
            DEVICE_HANDLE *handles = HeapAlloc(GetProcessHeap(), 0, room * sizeof(*handles));
            if (handles) {
                if (m->Count)
                    memcpy(handles, m->Handles, m->Count * sizeof(*handles));
                HeapFree(GetProcessHeap(), 0, m->Handles);
                m->Handles = handles;
                m->Room = room;
            }
        }
        if (m->Count == m->Room) {
            hr = E_OUTOFMEMORY;
        } else {
            m->Handles[m->Count].Value = ++m->NextHandle;
            m->Handles[m->Count].Generation = m->Generation;
            m->Count++;
            *handle = (HANDLE)m->NextHandle;
        }
    }
    LeaveCriticalSection(&m->Lock);
    return hr;
}

/* like windows, closing the handle that holds the device lets the device go. */
static HRESULT STDMETHODCALLTYPE manager_close_device_handle(IDirect3DDeviceManager9 *This, HANDLE handle)
{
    MANAGER *m = (MANAGER *)This;
    HRESULT hr = E_HANDLE_;
    EnterCriticalSection(&m->Lock);
    DEVICE_HANDLE *h = find_handle(m, handle);
    if (h) {
        if (m->Holder == h->Value) {
            m->Holder = 0;
            WakeAllConditionVariable(&m->Unlocked);
        }
        *h = m->Handles[--m->Count];
        hr = S_OK;
    }
    LeaveCriticalSection(&m->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_test_device(IDirect3DDeviceManager9 *This, HANDLE handle)
{
    MANAGER *m = (MANAGER *)This;
    EnterCriticalSection(&m->Lock);
    HRESULT hr = m->Device ? check_handle(m, handle) : DXVA2_E_NOT_INITIALIZED;
    LeaveCriticalSection(&m->Lock);
    return hr;
}

/* the lock is not recursive: a second lock waits, or fails when it may not wait. */
static HRESULT STDMETHODCALLTYPE manager_lock_device(IDirect3DDeviceManager9 *This, HANDLE handle, IUnknown **device,
                                                     BOOL block)
{
    MANAGER *m = (MANAGER *)This;
    if (!device)
        return E_POINTER;
    EnterCriticalSection(&m->Lock);
    HRESULT hr = m->Device ? check_handle(m, handle) : DXVA2_E_NOT_INITIALIZED;
    while (hr == S_OK && m->Holder) {
        if (!block) {
            hr = DXVA2_E_VIDEO_DEVICE_LOCKED;
            break;
        }
        SleepConditionVariableCS(&m->Unlocked, &m->Lock, INFINITE);
        hr = check_handle(m, handle);
    }
    if (hr == S_OK) {
        m->Holder = (ULONG_PTR)handle;
        *device = m->Device;
        m->Device->lpVtbl->AddRef(m->Device);
    }
    LeaveCriticalSection(&m->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_unlock_device(IDirect3DDeviceManager9 *This, HANDLE handle, BOOL save_state)
{
    MANAGER *m = (MANAGER *)This;
    HRESULT hr = E_INVALIDARG;
    EnterCriticalSection(&m->Lock);
    if (handle && m->Holder == (ULONG_PTR)handle) {
        m->Holder = 0;
        WakeAllConditionVariable(&m->Unlocked);
        hr = S_OK;
    }
    LeaveCriticalSection(&m->Lock);
    return hr;
}

/* there is no decoder and no video processor behind the device. */
static HRESULT STDMETHODCALLTYPE manager_get_video_service(IDirect3DDeviceManager9 *This, HANDLE handle, REFIID riid,
                                                           void **service)
{
    MANAGER *m = (MANAGER *)This;
    if (!service)
        return E_POINTER;
    *service = nullptr;
    EnterCriticalSection(&m->Lock);
    HRESULT hr = m->Device ? check_handle(m, handle) : DXVA2_E_NOT_INITIALIZED;
    LeaveCriticalSection(&m->Lock);
    return hr == S_OK ? E_NOINTERFACE : hr;
}

static const IDirect3DDeviceManager9Vtbl g_manager_vtbl = {
    manager_query_interface,   manager_add_ref,     manager_release,     manager_reset_device,
    manager_open_device_handle, manager_close_device_handle, manager_test_device, manager_lock_device,
    manager_unlock_device,     manager_get_video_service,
};

/* the token only has to be what ResetDevice is given back; windows takes it from the tick count as well. */
DECLSPEC_EXPORT HRESULT WINAPI DXVA2CreateDirect3DDeviceManager9(UINT *token, IDirect3DDeviceManager9 **manager)
{
    if (!token || !manager)
        return E_POINTER;
    MANAGER *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return E_OUTOFMEMORY;
    m->Interface.lpVtbl = &g_manager_vtbl;
    m->References = 1;
    m->Token = GetTickCount() | 1;
    InitializeCriticalSection(&m->Lock);
    InitializeConditionVariable(&m->Unlocked);
    *token = m->Token;
    *manager = &m->Interface;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DXVA2CreateVideoService(IUnknown *device, REFIID riid, void **service)
{
    if (!device || !service)
        return E_POINTER;
    *service = nullptr;
    return E_NOINTERFACE;
}

DECLSPEC_EXPORT HRESULT WINAPI DXVAHD_CreateDevice(IUnknown *device, const void *content, int usage, void *plugin,
                                                   void **result)
{
    if (!device || !content || !result)
        return E_POINTER;
    *result = nullptr;
    return E_NOINTERFACE;
}

DECLSPEC_EXPORT HRESULT WINAPI OPMGetVideoOutputsFromHMONITOR(HMONITOR monitor, int semantics, ULONG *count, void ***outputs)
{
    return ERROR_GRAPHICS_OPM_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT HRESULT WINAPI OPMGetVideoOutputsFromIDirect3DDevice9Object(IUnknown *device, int semantics, ULONG *count,
                                                                            void ***outputs)
{
    return ERROR_GRAPHICS_OPM_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT HRESULT WINAPI OPMGetVideoOutputForTarget(LUID *adapter, ULONG target, int semantics, void **output)
{
    return ERROR_GRAPHICS_OPM_NOT_SUPPORTED_;
}

/* ------------------------------------------------------------------ physical monitors */

typedef struct PHYSICAL_MONITOR {
    HANDLE hPhysicalMonitor;
    WCHAR szPhysicalMonitorDescription[128];
} PHYSICAL_MONITOR;

/* like windows, the handles are small numbers that start at zero. */
#define MAX_PHYSICAL 64
static BOOLEAN g_open[MAX_PHYSICAL];
static SRWLOCK g_physical_lock = SRWLOCK_INIT;

static BOOL fail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

static BOOL is_monitor(HMONITOR monitor)
{
    MONITORINFO info = {sizeof(info)};
    return monitor && GetMonitorInfoW(monitor, &info);
}

static BOOL is_physical(HANDLE monitor)
{
    ULONG_PTR index = (ULONG_PTR)monitor;
    AcquireSRWLockShared(&g_physical_lock);
    BOOL open = index < MAX_PHYSICAL && g_open[index];
    ReleaseSRWLockShared(&g_physical_lock);
    return open;
}

/* a display of the machine is one monitor. */
DECLSPEC_EXPORT BOOL WINAPI GetNumberOfPhysicalMonitorsFromHMONITOR(HMONITOR monitor, DWORD *count)
{
    if (!is_monitor(monitor))
        return fail(ERROR_INVALID_MONITOR_HANDLE_);
    if (!count)
        return fail(ERROR_GRAPHICS_INVALID_POINTER_);
    *count = 1;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI GetPhysicalMonitorsFromHMONITOR(HMONITOR monitor, DWORD count, PHYSICAL_MONITOR *monitors)
{
    if (!is_monitor(monitor))
        return fail(ERROR_INVALID_MONITOR_HANDLE_);
    if (!count || !monitors)
        return fail(ERROR_GRAPHICS_INVALID_POINTER_);
    if (count != 1)
        return fail(ERROR_GEN_FAILURE);
    ULONG index = MAX_PHYSICAL;
    AcquireSRWLockExclusive(&g_physical_lock);
    for (ULONG i = 0; i < MAX_PHYSICAL && index == MAX_PHYSICAL; i++)
        if (!g_open[i])
            index = i;
    if (index < MAX_PHYSICAL)
        g_open[index] = TRUE;
    ReleaseSRWLockExclusive(&g_physical_lock);
    if (index == MAX_PHYSICAL)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    monitors[0].hPhysicalMonitor = (HANDLE)(ULONG_PTR)index;
    wcscpy(monitors[0].szPhysicalMonitorDescription, L"Generic PnP Monitor");
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI GetNumberOfPhysicalMonitorsFromIDirect3DDevice9(IUnknown *device, DWORD *count)
{
    if (!device || !count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI GetPhysicalMonitorsFromIDirect3DDevice9(IUnknown *device, DWORD count,
                                                                       PHYSICAL_MONITOR *monitors)
{
    if (!device || !monitors)
        return E_POINTER;
    HMONITOR primary = MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    if (GetPhysicalMonitorsFromHMONITOR(primary, count, monitors))
        return S_OK;
    DWORD error = GetLastError();
    return (HRESULT)(error & 0x80000000 ? error : 0x80070000 | error);
}

DECLSPEC_EXPORT BOOL WINAPI DestroyPhysicalMonitor(HANDLE monitor)
{
    ULONG_PTR index = (ULONG_PTR)monitor;
    AcquireSRWLockExclusive(&g_physical_lock);
    BOOL open = index < MAX_PHYSICAL && g_open[index];
    if (open)
        g_open[index] = FALSE;
    ReleaseSRWLockExclusive(&g_physical_lock);
    return open ? TRUE : fail(ERROR_GRAPHICS_INVALID_PHYSICAL_MONITOR_HANDLE_);
}

DECLSPEC_EXPORT BOOL WINAPI DestroyPhysicalMonitors(DWORD count, PHYSICAL_MONITOR *monitors)
{
    if (!monitors)
        return fail(ERROR_GRAPHICS_INVALID_POINTER_);
    BOOL ok = TRUE;
    for (DWORD i = 0; i < count; i++)
        ok &= DestroyPhysicalMonitor(monitors[i].hPhysicalMonitor);
    return ok;
}

/* what every setting answers: the monitor does not speak DDC/CI, or the handle is none. */
static BOOL no_ddc(HANDLE monitor)
{
    return fail(is_physical(monitor) ? ERROR_NOT_SUPPORTED : ERROR_GRAPHICS_INVALID_PHYSICAL_MONITOR_HANDLE_);
}

DECLSPEC_EXPORT BOOL WINAPI GetMonitorCapabilities(HANDLE m, DWORD *capabilities, DWORD *temperatures) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorBrightness(HANDLE m, DWORD *low, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorContrast(HANDLE m, DWORD *low, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorColorTemperature(HANDLE m, int *temperature) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorTechnologyType(HANDLE m, int *type) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorDisplayAreaPosition(HANDLE m, int which, DWORD *low, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorDisplayAreaSize(HANDLE m, int which, DWORD *low, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorRedGreenOrBlueDrive(HANDLE m, int which, DWORD *low, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetMonitorRedGreenOrBlueGain(HANDLE m, int which, DWORD *low, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorBrightness(HANDLE m, DWORD value) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorContrast(HANDLE m, DWORD value) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorColorTemperature(HANDLE m, int temperature) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorDisplayAreaPosition(HANDLE m, int which, DWORD value) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorDisplayAreaSize(HANDLE m, int which, DWORD value) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorRedGreenOrBlueDrive(HANDLE m, int which, DWORD value) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetMonitorRedGreenOrBlueGain(HANDLE m, int which, DWORD value) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI DegaussMonitor(HANDLE m) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI RestoreMonitorFactoryDefaults(HANDLE m) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI RestoreMonitorFactoryColorDefaults(HANDLE m) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SaveCurrentMonitorSettings(HANDLE m) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SaveCurrentSettings(HANDLE m) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetCapabilitiesStringLength(HANDLE m, DWORD *length) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI CapabilitiesRequestAndCapabilitiesReply(HANDLE m, LPSTR text, DWORD length) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetTimingReport(HANDLE m, void *report) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI GetVCPFeatureAndVCPFeatureReply(HANDLE m, BYTE code, int *type, DWORD *now, DWORD *high) { return no_ddc(m); }
DECLSPEC_EXPORT BOOL WINAPI SetVCPFeature(HANDLE m, BYTE code, DWORD value) { return no_ddc(m); }
