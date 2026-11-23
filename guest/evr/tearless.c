#include "evr_private.h"

#define D3DCAPS_READ_SCANLINE_ 0x00020000
#define D3DCREATE_ENABLE_PRESENTSTATS_ 0x00004000
#define D3DPRESENT_DONOTWAIT_ 0x1
#define D3DPRESENT_DONOTFLIP_ 0x4
#define D3DERR_WASSTILLDRAWING_ ((HRESULT)0x8876021C)
#define EC_SAMPLE_LATENCY 0x22

/*
 * evr's CBlitInSlices. its modes: 0 and 1 present the rectangles, 2 and 3 the whole chain, 3 needs a device made for
 * present statistics, 4 is forced when the device cannot read its scan line, 5 is flip mode. windows also slices the
 * blit against the scan line on a thread of its own while the clock runs; here each frame goes out at once.
 */
typedef struct {
    IUnknown inner;
    IEVRTearlessWindowPresenter9 iface;
    IQualProp qualprop;
    IPresenterStatistics statistics;
    IUnknown *outer;
    LONG refs;
    CRITICAL_SECTION lock;
    DWORD mode, prefs;
    BOOL started, device_changed;
    UINT width, height, refresh;
    DWORD presents;
} Tearless;

#define IMPL(iface) CONTAINING_RECORD(This, Tearless, iface)

/* ------------------------------------------------------------------ statistics */

/*
 * the figures a fresh presenter gives: the display it estimates the scan line of, the frames shown, and ten, a
 * constant of windows. the timing averages stay zero.
 */
static void fill_statistics(Tearless *t, DWORD *out)
{
    out[8] = t->width;
    out[9] = t->height;
    out[10] = t->refresh ? 1000000 / t->refresh : 0;
    out[15] = t->presents;
    out[56] = 10;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE inner_QueryInterface(IUnknown *This, REFIID riid, void **out)
{
    Tearless *t = IMPL(inner);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &t->inner;
    else if (IsEqualGUID(riid, &IID_IEVRTearlessWindowPresenter9))
        *out = &t->iface;
    else if (IsEqualGUID(riid, &IID_IPresenterStatistics))
        *out = &t->statistics;
    else if (IsEqualGUID(riid, &IID_IQualProp))
        *out = &t->qualprop;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE inner_AddRef(IUnknown *This)
{
    return InterlockedIncrement(&IMPL(inner)->refs);
}

static ULONG STDMETHODCALLTYPE inner_Release(IUnknown *This)
{
    Tearless *t = IMPL(inner);
    ULONG refs = InterlockedDecrement(&t->refs);
    if (refs)
        return refs;
    DeleteCriticalSection(&t->lock);
    HeapFree(GetProcessHeap(), 0, t);
    EvrObjectDestroyed();
    return 0;
}

static const IUnknownVtbl inner_vtbl = {inner_QueryInterface, inner_AddRef, inner_Release};

#define DELEGATE_IUNKNOWN(prefix, type, iface)                                                                         \
    static HRESULT STDMETHODCALLTYPE prefix##_QueryInterface(type *This, REFIID riid, void **out)                      \
    {                                                                                                                  \
        return IMPL(iface)->outer->lpVtbl->QueryInterface(IMPL(iface)->outer, riid, out);                              \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_AddRef(type *This)                                                         \
    {                                                                                                                  \
        return IMPL(iface)->outer->lpVtbl->AddRef(IMPL(iface)->outer);                                                 \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_Release(type *This)                                                        \
    {                                                                                                                  \
        return IMPL(iface)->outer->lpVtbl->Release(IMPL(iface)->outer);                                                \
    }

/* ------------------------------------------------------------------ IEVRTearlessWindowPresenter9 */

DELEGATE_IUNKNOWN(tear, IEVRTearlessWindowPresenter9, iface)

static BOOL reads_scanline(IDirect3DSwapChain9 *chain)
{
    IDirect3DDevice9 *device = nullptr;
    D3DCAPS9 caps = {0};
    BOOL yes = FALSE;
    if (SUCCEEDED(chain->lpVtbl->GetDevice(chain, &device))) {
        if (SUCCEEDED(device->lpVtbl->GetDeviceCaps(device, &caps)))
            yes = (caps.Caps & D3DCAPS_READ_SCANLINE_) != 0;
        device->lpVtbl->Release(device);
    }
    return yes;
}

/* evr's ValidatePresentStatsFlag. */
static HRESULT check_present_statistics(IDirect3DSwapChain9 *chain)
{
    IDirect3DDevice9 *device = nullptr;
    D3DDEVICE_CREATION_PARAMETERS params = {0};
    HRESULT hr = chain->lpVtbl->GetDevice(chain, &device);
    if (SUCCEEDED(hr)) {
        hr = device->lpVtbl->GetCreationParameters(device, &params);
        if (SUCCEEDED(hr) && !(params.BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS_))
            hr = E_INVALIDARG;
        device->lpVtbl->Release(device);
    }
    return hr;
}

static void estimate_display(Tearless *t, IDirect3DSwapChain9 *chain)
{
    D3DDISPLAYMODE mode = {0};
    if (SUCCEEDED(chain->lpVtbl->GetDisplayMode(chain, &mode))) {
        t->width = mode.Width;
        t->height = mode.Height;
        t->refresh = mode.RefreshRate ? mode.RefreshRate : 60;
    }
    t->device_changed = FALSE;
}

static LONGLONG now_100ns(void)
{
    LARGE_INTEGER count, frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    return (LONGLONG)((double)count.QuadPart * 10000000.0 / (double)frequency.QuadPart);
}

/*
 * evr's Present and PresentSingleFrame: count is how many times modes 3 and 5 show the frame, -1 meaning once. the
 * repeats do not flip. a busy device is asked again after a millisecond.
 */
static HRESULT STDMETHODCALLTYPE tear_Present(IEVRTearlessWindowPresenter9 *This, IDirect3DSwapChain9 *chain,
                                              IMediaEventSink *sink, const RECT *src, const RECT *dst, HWND window,
                                              LONGLONG time, DWORD count, BOOL wait, IUnknown *sample)
{
    Tearless *t = IMPL(iface);
    if (!chain)
        return E_POINTER;
    EnterCriticalSection(&t->lock);
    if (!reads_scanline(chain))
        t->mode = 4;
    DWORD mode = t->mode;
    if (mode == 4 || t->device_changed || !t->width)
        estimate_display(t, chain);
    LeaveCriticalSection(&t->lock);
    HRESULT hr = S_OK;
    if (mode == 3 && FAILED(hr = check_present_statistics(chain)))
        return hr;
    DWORD times = mode == 3 || mode == 5 ? (count == ~0u ? 1 : count) : 1;
    BOOL whole = mode == 2 || mode == 3;
    for (DWORD i = 0; i < times && SUCCEEDED(hr); i++) {
        DWORD flags = D3DPRESENT_DONOTWAIT_ | (i && (mode == 3 || mode == 5) ? D3DPRESENT_DONOTFLIP_ : 0);
        do {
            hr = chain->lpVtbl->Present(chain, whole ? nullptr : src, whole ? nullptr : dst, window, nullptr, flags);
            if (hr == D3DERR_WASSTILLDRAWING_)
                Sleep(1);
        } while (hr == D3DERR_WASSTILLDRAWING_);
    }
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&t->lock);
        t->presents++;
        LeaveCriticalSection(&t->lock);
        if (mode && sink && time) {
            LONGLONG latency = now_100ns() - time;
            sink->lpVtbl->Notify(sink, EC_SAMPLE_LATENCY, (LONG_PTR)&latency, 0);
        }
    }
    return hr;
}

static void STDMETHODCALLTYPE tear_UpdateDestRect(IEVRTearlessWindowPresenter9 *This, const RECT *dst)
{
}

static void STDMETHODCALLTYPE tear_SetDeviceChanged(IEVRTearlessWindowPresenter9 *This)
{
    Tearless *t = IMPL(iface);
    EnterCriticalSection(&t->lock);
    t->device_changed = TRUE;
    LeaveCriticalSection(&t->lock);
}

static HRESULT STDMETHODCALLTYPE tear_StartClock(IEVRTearlessWindowPresenter9 *This)
{
    IMPL(iface)->started = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tear_StopClock(IEVRTearlessWindowPresenter9 *This)
{
    IMPL(iface)->started = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tear_Pause(IEVRTearlessWindowPresenter9 *This, LONGLONG time)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tear_Resume(IEVRTearlessWindowPresenter9 *This, LONGLONG time)
{
    return S_OK;
}

static void STDMETHODCALLTYPE tear_Flush(IEVRTearlessWindowPresenter9 *This)
{
}

static HRESULT STDMETHODCALLTYPE tear_SetMode(IEVRTearlessWindowPresenter9 *This, DWORD mode)
{
    if (mode > 5)
        return E_INVALIDARG;
    InterlockedExchange((LONG *)&IMPL(iface)->mode, (LONG)mode);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tear_SetRenderingPrefs(IEVRTearlessWindowPresenter9 *This, DWORD prefs)
{
    IMPL(iface)->prefs = prefs;
    return S_OK;
}

static const IEVRTearlessWindowPresenter9Vtbl tear_vtbl = {
    tear_QueryInterface, tear_AddRef,  tear_Release, tear_Present, tear_UpdateDestRect, tear_SetDeviceChanged,
    tear_StartClock,     tear_StopClock, tear_Pause, tear_Resume,  tear_Flush,          tear_SetMode,
    tear_SetRenderingPrefs,
};

/* ------------------------------------------------------------------ IQualProp */

DELEGATE_IUNKNOWN(qualprop, IQualProp, qualprop)

/* windows answers from the slicer thread's own figures, which a frame presented at once never has. */
static HRESULT STDMETHODCALLTYPE qualprop_get(IQualProp *This, int *value)
{
    return E_NOTIMPL;
}

static const IQualPropVtbl qualprop_vtbl = {
    qualprop_QueryInterface, qualprop_AddRef, qualprop_Release, qualprop_get,
    qualprop_get,            qualprop_get,    qualprop_get,     qualprop_get,
    qualprop_get,
};

/* ------------------------------------------------------------------ IPresenterStatistics */

DELEGATE_IUNKNOWN(statistics, IPresenterStatistics, statistics)

/* only the figures the tearless presenter keeps are written; the rest of the block is left as it was. */
static HRESULT STDMETHODCALLTYPE statistics_GetPresenterStatistics(IPresenterStatistics *This, DWORD *stats)
{
    Tearless *t = IMPL(statistics);
    if (!stats)
        return E_POINTER;
    EnterCriticalSection(&t->lock);
    for (int i = 0; i <= 10; i++)
        stats[i] = 0;
    stats[13] = stats[14] = stats[17] = 0;
    stats[57] = stats[58] = stats[59] = 0;
    fill_statistics(t, stats);
    LeaveCriticalSection(&t->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE statistics_ResetStatistics(IPresenterStatistics *This)
{
    return S_OK;
}

static const IPresenterStatisticsVtbl statistics_vtbl = {
    statistics_QueryInterface,          statistics_AddRef,          statistics_Release,
    statistics_GetPresenterStatistics, statistics_ResetStatistics,
};

/* ------------------------------------------------------------------ creation */

HRESULT EvrCreateTearless(IUnknown *outer, REFIID riid, void **out)
{
    Tearless *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->inner.lpVtbl = &inner_vtbl;
    t->iface.lpVtbl = &tear_vtbl;
    t->qualprop.lpVtbl = &qualprop_vtbl;
    t->statistics.lpVtbl = &statistics_vtbl;
    t->outer = outer ? outer : &t->inner;
    t->refs = 1;
    t->mode = 2;
    InitializeCriticalSection(&t->lock);
    EvrObjectCreated();
    HRESULT hr = inner_QueryInterface(&t->inner, riid, out);
    inner_Release(&t->inner);
    return hr;
}
