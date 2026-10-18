/*
 * the exports of dcomp.dll beyond the devices: surface handles, the clock of
 * the compositor and its statistics, which here are the display's blanks on
 * the performance counter. Frames are numbered by the blank they start at.
 */
#include "dcomp_private.h"

int _fltused = 0x9875;

static LONG g_live_devices;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

void DcObjectCountChange(LONG delta) { InterlockedExchangeAdd(&g_live_devices, delta); }

/* ------------------------------------------------------------------ devices */

static HRESULT create_device(UINT version, IUnknown *rendering_device, REFIID iid, void **out)
{
    DcDevice *device;
    HRESULT hr = DcDeviceCreate(version, rendering_device, &device);
    if (FAILED(hr)) {
        *out = nullptr;
        return hr;
    }
    /* the interface asked for answers by the device's own table, the version making what it offers. */
    hr = DcDeviceQuery(device, iid, out);
    DcDeviceRelease(device);
    if (FAILED(hr))
        *out = nullptr;
    return hr;
}

DCOMPAPI HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **out)
{
    if (!out || !iid)
        return E_INVALIDARG;
    if (!IsEqualGUID(iid, &IID_IDCompositionDevice)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    return create_device(0, (IUnknown *)dxgi_device, iid, out);
}

DCOMPAPI HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **out)
{
    if (!out || !iid)
        return E_INVALIDARG;
    if (!IsEqualGUID(iid, &IID_IDCompositionDevice) && !IsEqualGUID(iid, &IID_IDCompositionDesktopDevice)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    return create_device(1, rendering_device, iid, out);
}

DCOMPAPI HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **out)
{
    if (!out || !iid)
        return E_INVALIDARG;
    if (!IsEqualGUID(iid, &IID_IDCompositionDevice) && !IsEqualGUID(iid, &IID_IDCompositionDevice4) &&
        !IsEqualGUID(iid, &IID_IDCompositionDesktopDevice)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    return create_device(2, rendering_device, iid, out);
}

/* ------------------------------------------------------------------ surface handles */

/*
 * a handle names a composition surface by a section whose first bytes say
 * so. the section can be duplicated into another process; what it names is
 * found through those bytes.
 */
#define SURFACE_HANDLE_MAGIC 0x53434D44u

DCOMPAPI HRESULT WINAPI DCompositionCreateSurfaceHandle(DWORD access, SECURITY_ATTRIBUTES *attributes, HANDLE *handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    if (!access)
        return E_ACCESSDENIED_HR;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    if (attributes) {
        sa.lpSecurityDescriptor = attributes->lpSecurityDescriptor;
        sa.bInheritHandle = attributes->bInheritHandle;
    }
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, 4096, nullptr);
    if (!section)
        return HRESULT_FROM_WIN32(GetLastError());
    UINT32 *view = MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 4096);
    if (!view) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(section);
        return hr;
    }
    static volatile LONG serial;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    view[0] = SURFACE_HANDLE_MAGIC;
    view[1] = GetCurrentProcessId();
    view[2] = (UINT32)InterlockedIncrement(&serial);
    view[3] = (UINT32)counter.LowPart;
    UnmapViewOfFile(view);
    *handle = section;
    return S_OK;
}

/* ------------------------------------------------------------------ the rest of the exports */

DCOMPAPI HRESULT WINAPI DCompositionAttachMouseWheelToHwnd(IDCompositionVisual *visual, HWND hwnd, BOOL enable)
{
    if (!visual || (hwnd && !IsWindow(hwnd)))
        return E_INVALIDARG;
    return S_OK;
}

DCOMPAPI HRESULT WINAPI DCompositionAttachMouseDragToHwnd(IDCompositionVisual *visual, HWND hwnd, BOOL enable)
{
    if (!visual || (hwnd && !IsWindow(hwnd)))
        return E_INVALIDARG;
    return S_OK;
}

DCOMPAPI HRESULT WINAPI DwmEnableMMCSS(BOOL enable) { return S_OK; }

/* the dwm flushes its queue; the compositor here has none. */
DCOMPAPI HRESULT WINAPI DwmFlush(void) { return S_OK; }

DCOMPAPI HRESULT WINAPI DwmpEnableDDASupport(void) { return S_OK; }

DCOMPAPI HRESULT WINAPI DllCanUnloadNow(void) { return g_live_devices ? S_FALSE : S_OK; }

DCOMPAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    if (out)
        *out = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

DCOMPAPI HRESULT WINAPI DllGetActivationFactory(void *class_id, void **factory)
{
    if (factory)
        *factory = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* the presentation manager is not offered. */
DCOMPAPI HRESULT WINAPI CreatePresentationFactory(IUnknown *device, REFIID iid, void **factory)
{
    if (factory)
        *factory = nullptr;
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ time and statistics */

UINT64 DcNow(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (UINT64)counter.QuadPart;
}

UINT64 DcFrequency(void)
{
    static UINT64 frequency;
    if (!frequency) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        frequency = (UINT64)f.QuadPart;
    }
    return frequency;
}

/* frames older than this are gone from the statistics, as the dwm keeps a few seconds. */
#define FRAME_HISTORY 512

static void frame_range(const MIME_VBLANK *vblank, UINT64 *first, UINT64 *last)
{
    UINT64 current = MimeVblankIndex(vblank, DcNow());
    *first = current > FRAME_HISTORY ? current - FRAME_HISTORY : 0;
    *last = current + 1;
}

DCOMPAPI HRESULT WINAPI DCompositionGetFrameId(COMPOSITION_FRAME_ID_TYPE type, COMPOSITION_FRAME_ID *frame_id)
{
    if (!frame_id)
        return E_INVALIDARG;
    if ((UINT)type > COMPOSITION_FRAME_ID_COMPLETED)
        return E_INVALIDARG;
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    UINT64 current = MimeVblankIndex(&vblank, DcNow());
    /* the frame being made is next, the one at the last blank confirmed, the one before it completed. */
    switch (type) {
    case COMPOSITION_FRAME_ID_CREATED:
        *frame_id = current + 1;
        break;
    case COMPOSITION_FRAME_ID_CONFIRMED:
        *frame_id = current;
        break;
    default:
        *frame_id = current ? current - 1 : 0;
        break;
    }
    return S_OK;
}

static void target_id(COMPOSITION_TARGET_ID *id)
{
    memset(id, 0, sizeof(*id));
    id->adapterLuid.LowPart = 0xD3D0;
}

DCOMPAPI HRESULT WINAPI DCompositionGetStatistics(COMPOSITION_FRAME_ID frame_id, COMPOSITION_FRAME_STATS *stats,
                                                  UINT target_count, COMPOSITION_TARGET_ID *targets,
                                                  UINT *actual_target_count)
{
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    UINT64 first, last;
    frame_range(&vblank, &first, &last);
    if (frame_id < first || frame_id > last)
        return E_INVALIDARG;
    if (stats) {
        stats->startTime = MimeVblankTime(&vblank, frame_id);
        stats->targetTime = stats->startTime + vblank.period;
        stats->framePeriod = vblank.period;
    }
    if (targets && target_count)
        target_id(&targets[0]);
    if (actual_target_count)
        *actual_target_count = 1;
    return S_OK;
}

DCOMPAPI HRESULT WINAPI DCompositionGetTargetStatistics(COMPOSITION_FRAME_ID frame_id,
                                                        const COMPOSITION_TARGET_ID *target,
                                                        COMPOSITION_TARGET_STATS *stats)
{
    if (!target || !stats)
        return E_INVALIDARG;
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    UINT64 first, last;
    frame_range(&vblank, &first, &last);
    if (frame_id < first || frame_id > last)
        return E_INVALIDARG;
    COMPOSITION_TARGET_ID only;
    target_id(&only);
    if (target->adapterLuid.LowPart != only.adapterLuid.LowPart || target->vidPnSourceId || target->uniqueId)
        return E_INVALIDARG;
    memset(stats, 0, sizeof(*stats));
    UINT64 time = MimeVblankTime(&vblank, frame_id);
    stats->presentTime = time;
    stats->vblankDuration = vblank.period;
    stats->presented.presentCount = stats->completed.presentCount = (UINT)frame_id;
    stats->presented.refreshCount = stats->completed.refreshCount = (UINT)frame_id;
    stats->presented.virtualRefreshCount = stats->completed.virtualRefreshCount = (UINT)frame_id;
    stats->presented.time = stats->completed.time = time;
    return S_OK;
}

DCOMPAPI HRESULT WINAPI DCompositionBoostCompositorClock(BOOL enable) { return S_OK; }

/*
 * waits for the compositor's clock to tick, which it does at each blank, or
 * for one of the handles: the index of the handle that was signaled, or the
 * count of handles when the clock ticked.
 */
DCOMPAPI DWORD WINAPI DCompositionWaitForCompositorClock(UINT count, const HANDLE *handles, DWORD timeout)
{
    if (count > MAXIMUM_WAIT_OBJECTS || (count && !handles)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    UINT64 now = DcNow();
    UINT64 next = MimeVblankTime(&vblank, MimeVblankIndex(&vblank, now) + 1);
    UINT64 wait = (next - now) * 1000 / vblank.frequency + 1;
    if (timeout != INFINITE && timeout < wait) {
        DWORD result = count ? WaitForMultipleObjectsEx(count, handles, FALSE, timeout, FALSE) : (Sleep(timeout), WAIT_TIMEOUT);
        return result;
    }
    DWORD result;
    if (count)
        result = WaitForMultipleObjectsEx(count, handles, FALSE, (DWORD)wait, FALSE);
    else
        result = (Sleep((DWORD)wait), WAIT_TIMEOUT);
    if (result == WAIT_TIMEOUT)
        return WAIT_OBJECT_0 + count;
    return result;
}

/* ------------------------------------------------------------------ status codes */

/* the hresults the dwm's statuses become, as dcomp.dll maps them. */
HRESULT DcResultFromStatus(LONG status)
{
    switch (status) {
    case 0:
        return S_OK;
    case (LONG)0xC0000022:
        return E_ACCESSDENIED_HR;
    case (LONG)0xC0000024:
    case (LONG)0xC0000008:
        return E_HANDLE_HR;
    case (LONG)0xC0000017:
        return E_OUTOFMEMORY;
    case (LONG)0x803E0006:
        return DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED;
    case (LONG)0xC0000001:
        return E_FAIL;
    case (LONG)0xC0000002:
        return E_NOTIMPL;
    case (LONG)0xC000000D:
    case (LONG)0xC00000BB:
        return E_INVALIDARG;
    default:
        return (HRESULT)((ULONG)status | 0x10000000);
    }
}
