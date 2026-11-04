#include "wg_private.h"
#include <rtl.h>

static const WCHAR g_display_class[] = L"Windows.Graphics.Display.DisplayInformation";

static WG_DISPLAY *display_of(void *iface)
{
    return (WG_DISPLAY *)((WRT_IFACE *)iface)->Owner;
}

static IInspectable *display_iface(WG_DISPLAY *d)
{
    return WrtAgileIface(&d->Object);
}

static HMONITOR display_monitor(WG_DISPLAY *d)
{
    return WgMonitorOf(d->Window, d->Monitor);
}

static void display_mode(WG_DISPLAY *d, WG_STATE *state)
{
    memset(state, 0, sizeof(*state));
    WgQueryMode(display_monitor(d), state);
}

WRT_AGILE_THUNKS(IDisplayInformation)
WRT_AGILE_THUNKS(IDisplayInformation2)
WRT_AGILE_THUNKS(IDisplayInformation3)
WRT_AGILE_THUNKS(IDisplayInformation4)
WRT_AGILE_THUNKS(IDisplayInformation5)

/* ------------------------------------------------------------------ events */

static BOOL any_handlers(WG_DISPLAY *d)
{
    return WrtEventCount(&d->Orientation) || WrtEventCount(&d->Dpi) || WrtEventCount(&d->Stereo) ||
           WrtEventCount(&d->ColorProfile) || WrtEventCount(&d->AdvancedColor);
}

/* events are raised through the DispatcherQueue of the thread that made the object, which it must have. */
static HRESULT add_handler(WG_DISPLAY *d, WRT_EVENT *event, IWrtEventHandler *handler, EventRegistrationToken *token)
{
    if (!token)
        return E_POINTER;
    token->value = 0;
    if (!d->Queue)
        return E_ILLEGAL_METHOD_CALL;
    HRESULT hr = WrtEventAdd(event, handler, token);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&d->Lock);
    BOOL start = !d->Watching;
    d->Watching = TRUE;
    if (start)
        WgQueryState(display_monitor(d), &d->Last);
    ReleaseSRWLockExclusive(&d->Lock);
    if (start && FAILED(hr = WgWatchStart(d))) {
        AcquireSRWLockExclusive(&d->Lock);
        d->Watching = FALSE;
        ReleaseSRWLockExclusive(&d->Lock);
        WrtEventRemove(event, *token);
        token->value = 0;
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT remove_handler(WG_DISPLAY *d, WRT_EVENT *event, EventRegistrationToken token)
{
    WrtEventRemove(event, token);
    AcquireSRWLockExclusive(&d->Lock);
    BOOL stop = d->Watching && !any_handlers(d);
    if (stop)
        d->Watching = FALSE;
    ReleaseSRWLockExclusive(&d->Lock);
    if (stop)
        WgWatchStop(d);
    return S_OK;
}

/* the work a change posts to the queue of the display, which raises its events there. */
typedef struct RAISE {
    IWgQueueHandler Iface;
    volatile LONG Refs;
    WG_DISPLAY *Display;
    UINT Changed;
} RAISE;

static HRESULT STDMETHODCALLTYPE raise_query(IWgQueueHandler *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatcherQueueHandler_) ||
        IsEqualIID(riid, &IID_IAgileObject)) {
        InterlockedIncrement(&((RAISE *)This)->Refs);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE raise_add_ref(IWgQueueHandler *This)
{
    return (ULONG)InterlockedIncrement(&((RAISE *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE raise_release(IWgQueueHandler *This)
{
    RAISE *r = (RAISE *)This;
    LONG refs = InterlockedDecrement(&r->Refs);
    if (!refs) {
        WrtAgileRelease(&r->Display->Object.Base.Ifaces[0]);
        HeapFree(GetProcessHeap(), 0, r);
    }
    return (ULONG)refs;
}

/* in the order Windows raises them. */
static HRESULT STDMETHODCALLTYPE raise_invoke(IWgQueueHandler *This)
{
    RAISE *r = (RAISE *)This;
    WG_DISPLAY *d = r->Display;
    IInspectable *sender = display_iface(d);
    if (r->Changed & WG_ORIENTATION)
        WrtEventInvoke(&d->Orientation, sender, nullptr);
    if (r->Changed & WG_DPI)
        WrtEventInvoke(&d->Dpi, sender, nullptr);
    if (r->Changed & WG_STEREO)
        WrtEventInvoke(&d->Stereo, sender, nullptr);
    if (r->Changed & WG_COLOR_PROFILE)
        WrtEventInvoke(&d->ColorProfile, sender, nullptr);
    if (r->Changed & WG_ADVANCED_COLOR)
        WrtEventInvoke(&d->AdvancedColor, sender, nullptr);
    return S_OK;
}

static const IWgQueueHandlerVtbl raise_vtbl = {raise_query, raise_add_ref, raise_release, raise_invoke};

static BOOL same_color(const WG_COLOR *a, const WG_COLOR *b)
{
    return !memcmp(a, b, sizeof(*a));
}

void WgDisplayRefresh(WG_DISPLAY *d)
{
    WG_STATE now;
    WgQueryState(display_monitor(d), &now);
    AcquireSRWLockExclusive(&d->Lock);
    WG_STATE *last = &d->Last;
    UINT changed = 0;
    if (d->Watching) {
        if (now.Current != last->Current || now.Native != last->Native)
            changed |= WG_ORIENTATION;
        if (now.Scale != last->Scale || now.RawDpiX != last->RawDpiX || now.RawDpiY != last->RawDpiY)
            changed |= WG_DPI;
        if (wcscmp(now.Profile, last->Profile) || now.Monitor != last->Monitor)
            changed |= WG_COLOR_PROFILE;
        if (!same_color(&now.Color, &last->Color) || now.Monitor != last->Monitor)
            changed |= WG_ADVANCED_COLOR;
        *last = now;
    }
    ReleaseSRWLockExclusive(&d->Lock);
    if (!changed)
        return;
    RAISE *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        return;
    r->Iface.lpVtbl = &raise_vtbl;
    r->Refs = 1;
    r->Display = d;
    r->Changed = changed;
    WrtAgileAddRef(&d->Object.Base.Ifaces[0]);
    BOOLEAN queued = FALSE;
    d->Queue->lpVtbl->TryEnqueueWithPriority(d->Queue, 0, &r->Iface, &queued);
    raise_release(&r->Iface);
}

/* ------------------------------------------------------------------ IDisplayInformation */

static HRESULT STDMETHODCALLTYPE di_get_current_orientation(IDisplayInformation *This, DisplayOrientations *value)
{
    if (!value)
        return E_POINTER;
    WG_STATE state;
    display_mode(display_of(This), &state);
    *value = state.Current;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_native_orientation(IDisplayInformation *This, DisplayOrientations *value)
{
    if (!value)
        return E_POINTER;
    WG_STATE state;
    display_mode(display_of(This), &state);
    *value = state.Native;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_add_orientation_changed(IDisplayInformation *This, IWrtEventHandler *handler,
                                                           EventRegistrationToken *token)
{
    WG_DISPLAY *d = display_of(This);
    return add_handler(d, &d->Orientation, handler, token);
}

static HRESULT STDMETHODCALLTYPE di_remove_orientation_changed(IDisplayInformation *This, EventRegistrationToken token)
{
    WG_DISPLAY *d = display_of(This);
    return remove_handler(d, &d->Orientation, token);
}

static UINT display_scale(WG_DISPLAY *d)
{
    return WgQueryScale(display_monitor(d));
}


static HRESULT STDMETHODCALLTYPE di_get_resolution_scale(IDisplayInformation *This, ResolutionScale *value)
{
    if (!value)
        return E_POINTER;
    *value = (ResolutionScale)display_scale(display_of(This));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_logical_dpi(IDisplayInformation *This, float *value)
{
    if (!value)
        return E_POINTER;
    *value = (float)(int)(96 * display_scale(display_of(This))) / 100.0f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_raw_dpi_x(IDisplayInformation *This, float *value)
{
    if (!value)
        return E_POINTER;
    float y;
    WgQueryRawDpi(display_monitor(display_of(This)), value, &y);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_raw_dpi_y(IDisplayInformation *This, float *value)
{
    if (!value)
        return E_POINTER;
    float x;
    WgQueryRawDpi(display_monitor(display_of(This)), &x, value);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_add_dpi_changed(IDisplayInformation *This, IWrtEventHandler *handler,
                                                   EventRegistrationToken *token)
{
    WG_DISPLAY *d = display_of(This);
    return add_handler(d, &d->Dpi, handler, token);
}

static HRESULT STDMETHODCALLTYPE di_remove_dpi_changed(IDisplayInformation *This, EventRegistrationToken token)
{
    WG_DISPLAY *d = display_of(This);
    return remove_handler(d, &d->Dpi, token);
}

/* no display of Mime shows stereo images. */
static HRESULT STDMETHODCALLTYPE di_get_stereo_enabled(IDisplayInformation *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_add_stereo_changed(IDisplayInformation *This, IWrtEventHandler *handler,
                                                      EventRegistrationToken *token)
{
    WG_DISPLAY *d = display_of(This);
    return add_handler(d, &d->Stereo, handler, token);
}

static HRESULT STDMETHODCALLTYPE di_remove_stereo_changed(IDisplayInformation *This, EventRegistrationToken token)
{
    WG_DISPLAY *d = display_of(This);
    return remove_handler(d, &d->Stereo, token);
}

static HRESULT STDMETHODCALLTYPE di_get_color_profile_async(IDisplayInformation *This, void **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return WgColorProfileAsync(display_monitor(display_of(This)), operation);
}

static HRESULT STDMETHODCALLTYPE di_add_color_profile_changed(IDisplayInformation *This, IWrtEventHandler *handler,
                                                             EventRegistrationToken *token)
{
    WG_DISPLAY *d = display_of(This);
    return add_handler(d, &d->ColorProfile, handler, token);
}

static HRESULT STDMETHODCALLTYPE di_remove_color_profile_changed(IDisplayInformation *This,
                                                                EventRegistrationToken token)
{
    WG_DISPLAY *d = display_of(This);
    return remove_handler(d, &d->ColorProfile, token);
}

static const IDisplayInformationVtbl di_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDisplayInformation),
    di_get_current_orientation,
    di_get_native_orientation,
    di_add_orientation_changed,
    di_remove_orientation_changed,
    di_get_resolution_scale,
    di_get_logical_dpi,
    di_get_raw_dpi_x,
    di_get_raw_dpi_y,
    di_add_dpi_changed,
    di_remove_dpi_changed,
    di_get_stereo_enabled,
    di_add_stereo_changed,
    di_remove_stereo_changed,
    di_get_color_profile_async,
    di_add_color_profile_changed,
    di_remove_color_profile_changed,
};

/* ------------------------------------------------------------------ IDisplayInformation2 to 5 */

static HRESULT STDMETHODCALLTYPE di2_get_raw_pixels_per_view_pixel(IDisplayInformation2 *This, double *value)
{
    if (!value)
        return E_POINTER;
    *value = (double)(int)display_scale(display_of(This)) / 100.0;
    return S_OK;
}

/* Newton steps from above, which only go down until they reach the root. */
static double square_root(double x)
{
    if (!(x > 0.0))
        return 0.0;
    double root = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 100; i++) {
        double next = 0.5 * (root + x / root);
        if (next >= root)
            break;
        root = next;
    }
    return root;
}

/* the diagonal follows from the raw dpi; a display without one has no size, which is not an error. */
static HRESULT STDMETHODCALLTYPE di3_get_diagonal_size(IDisplayInformation3 *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    WG_STATE state;
    display_mode(display_of(This), &state);
    WgQueryRawDpi(display_monitor(display_of(This)), &state.RawDpiX, &state.RawDpiY);
    if (state.RawDpiX <= 0.0f || state.RawDpiY <= 0.0f)
        return S_OK;
    float x = (float)state.Width / state.RawDpiX, y = (float)state.Height / state.RawDpiY;
    double inches = square_root((double)(x * x + y * y));
    HSTRING_HEADER header;
    HSTRING name;
    IWgPropertyValueStatics *statics = nullptr;
    HRESULT hr = WindowsCreateStringReference(L"Windows.Foundation.PropertyValue", 32, &header, &name);
    if (SUCCEEDED(hr))
        hr = RoGetActivationFactory(name, &IID_IPropertyValueStatics_, (void **)&statics);
    if (FAILED(hr))
        return hr;
    IInspectable *box = nullptr;
    hr = statics->lpVtbl->CreateDouble(statics, inches, &box);
    statics->lpVtbl->Release(statics);
    if (SUCCEEDED(hr)) {
        hr = box->lpVtbl->QueryInterface(box, &IID_IReferenceDouble, (void **)value);
        box->lpVtbl->Release(box);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE di4_get_screen_width(IDisplayInformation4 *This, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    WG_STATE state;
    display_mode(display_of(This), &state);
    *value = state.Width;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di4_get_screen_height(IDisplayInformation4 *This, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    WG_STATE state;
    display_mode(display_of(This), &state);
    *value = state.Height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di5_get_advanced_color_info(IDisplayInformation5 *This, IAdvancedColorInfo **value)
{
    if (!value)
        return E_POINTER;
    WG_COLOR color;
    WgQueryColor(display_monitor(display_of(This)), &color);
    return WgAdvancedColorCreate(&color, value);
}

static HRESULT STDMETHODCALLTYPE di5_add_advanced_color_changed(IDisplayInformation5 *This,
                                                               IWrtEventHandler *handler,
                                                               EventRegistrationToken *token)
{
    WG_DISPLAY *d = display_of(This);
    return add_handler(d, &d->AdvancedColor, handler, token);
}

static HRESULT STDMETHODCALLTYPE di5_remove_advanced_color_changed(IDisplayInformation5 *This,
                                                                  EventRegistrationToken token)
{
    WG_DISPLAY *d = display_of(This);
    return remove_handler(d, &d->AdvancedColor, token);
}

static const IDisplayInformation2Vtbl di2_vtbl = {WRT_INSPECTABLE_ENTRIES(IDisplayInformation2),
                                                  di2_get_raw_pixels_per_view_pixel};
static const IDisplayInformation3Vtbl di3_vtbl = {WRT_INSPECTABLE_ENTRIES(IDisplayInformation3),
                                                  di3_get_diagonal_size};
static const IDisplayInformation4Vtbl di4_vtbl = {WRT_INSPECTABLE_ENTRIES(IDisplayInformation4),
                                                  di4_get_screen_width, di4_get_screen_height};
static const IDisplayInformation5Vtbl di5_vtbl = {WRT_INSPECTABLE_ENTRIES(IDisplayInformation5),
                                                  di5_get_advanced_color_info, di5_add_advanced_color_changed,
                                                  di5_remove_advanced_color_changed};

static void display_destroy(WRT_OBJECT *object)
{
    WG_DISPLAY *d = (WG_DISPLAY *)object;
    if (d->Watching)
        WgWatchStop(d);
    WrtEventClear(&d->Orientation);
    WrtEventClear(&d->Dpi);
    WrtEventClear(&d->Stereo);
    WrtEventClear(&d->ColorProfile);
    WrtEventClear(&d->AdvancedColor);
    if (d->Queue)
        d->Queue->lpVtbl->Release(d->Queue);
    HeapFree(GetProcessHeap(), 0, d);
}

static const WRT_CLASS g_display_class_info = {g_display_class,
                                               6,
                                               {&IID_IDisplayInformation, &IID_IDisplayInformation2,
                                                &IID_IDisplayInformation3, &IID_IDisplayInformation4,
                                                &IID_IDisplayInformation5, &IID_IWeakReferenceSource},
                                               1u << 5,
                                               display_destroy};

/* the queue of the calling thread, or nothing when it has none. */
static IWgQueue *current_queue(void)
{
    HSTRING_HEADER header;
    HSTRING name;
    IWgQueueStatics *statics = nullptr;
    IWgQueue *queue = nullptr;
    if (FAILED(WindowsCreateStringReference(L"Windows.System.DispatcherQueue", 30, &header, &name)) ||
        FAILED(RoGetActivationFactory(name, &IID_IDispatcherQueueStatics_, (void **)&statics)))
        return nullptr;
    if (FAILED(statics->lpVtbl->GetForCurrentThread(statics, &queue)))
        queue = nullptr;
    statics->lpVtbl->Release(statics);
    return queue;
}

/* one made for a window needs the DispatcherQueue of its thread; one made for a monitor only needs it for events. */
HRESULT WgDisplayCreate(HWND window, HMONITOR monitor, REFIID riid, void **result)
{
    IWgQueue *queue = current_queue();
    if (window && !queue)
        return E_NOTIMPL;
    WG_DISPLAY *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) {
        if (queue)
            queue->lpVtbl->Release(queue);
        return E_OUTOFMEMORY;
    }
    const void *vtables[6] = {&di_vtbl, &di2_vtbl, &di3_vtbl, &di4_vtbl, &di5_vtbl, &WrtWeakSourceVtbl};
    WrtAgileInit(&d->Object, &g_display_class_info, vtables);
    InitializeSRWLock(&d->Lock);
    d->Window = window;
    d->Monitor = monitor;
    d->Thread = GetCurrentThreadId();
    d->Queue = queue;
    HRESULT hr = WrtAgileQuery(&d->Object.Base.Ifaces[0], riid, result);
    WrtAgileRelease(&d->Object.Base.Ifaces[0]);
    return hr;
}
