/* the device: the interfaces programs create objects with, laid out like each Windows version's, and Commit. */
#include "dcomp_private.h"

static CRITICAL_SECTION g_devices_lock;
static INIT_ONCE g_devices_once = INIT_ONCE_STATIC_INIT;
static DcDevice *g_devices;

static BOOL CALLBACK devices_init(INIT_ONCE *once, void *param, void **context)
{
    InitializeCriticalSection(&g_devices_lock);
    return TRUE;
}

static void devices_lock(void)
{
    InitOnceExecuteOnce(&g_devices_once, devices_init, nullptr, nullptr);
    EnterCriticalSection(&g_devices_lock);
}

void DcDeviceLock(DcDevice *device) { EnterCriticalSection(&device->lock); }
void DcDeviceUnlock(DcDevice *device) { LeaveCriticalSection(&device->lock); }

static DcDevice *dev(void *This) { return ((DcDeviceFace *)This)->device; }

/*
 * a device's references count its interfaces and every object made with it,
 * all under one lock, so a callback arriving after the last program reference
 * can check whether the device still exists.
 */
ULONG DcDeviceAddRef(DcDevice *device)
{
    devices_lock();
    ULONG refs = ++device->refs;
    LeaveCriticalSection(&g_devices_lock);
    return refs;
}

DcDevice *DcDeviceTryRef(DcDevice *device)
{
    devices_lock();
    DcDevice *found = nullptr;
    for (DcDevice *d = g_devices; d && !found; d = d->next_global)
        if (d == device && d->refs > 0) {
            d->refs++;
            found = d;
        }
    LeaveCriticalSection(&g_devices_lock);
    return found;
}

static void device_destroy(DcDevice *device)
{
    DcGpuRelease(device);
    if (device->d3d)
        device->d3d->lpVtbl->Release(device->d3d);
    if (device->d2d)
        device->d2d->lpVtbl->Release(device->d2d);
    if (device->own_d3d)
        device->own_d3d->lpVtbl->Release(device->own_d3d);
    DeleteCriticalSection(&device->lock);
    DcObjectCountChange(-1);
    HeapFree(GetProcessHeap(), 0, device);
}

ULONG DcDeviceRelease(DcDevice *device)
{
    devices_lock();
    ULONG refs = --device->refs;
    if (!refs)
        for (DcDevice **p = &g_devices; *p; p = &(*p)->next_global)
            if (*p == device) {
                *p = device->next_global;
                break;
            }
    LeaveCriticalSection(&g_devices_lock);
    if (!refs)
        device_destroy(device);
    return refs;
}

/* ------------------------------------------------------------------ creation */

/*
 * the device the program draws surfaces with: a Direct3D device that exposes IDXGIDevice3 and ID3D11Device1, or a
 * Direct2D device made on one.
 */
HRESULT DcResolveRenderingDevice(IUnknown *rendering_device, ID3D11Device **d3d, ID2D1Device **d2d)
{
    static const GUID IID_ID3D11Device1_ = {0xA04BFB29, 0x08EF, 0x43D6, {0xA4, 0x9C, 0xA9, 0xBD, 0xBD, 0xCB, 0xE6, 0x86}};
    *d3d = nullptr;
    *d2d = nullptr;
    if (!rendering_device)
        return S_OK;
    IUnknown *dxgi;
    ID2D1Device2 *device2 = nullptr;
    HRESULT hr = rendering_device->lpVtbl->QueryInterface(rendering_device, &IID_IDXGIDevice3, (void **)&dxgi);
    if (FAILED(hr)) {
        if (FAILED(rendering_device->lpVtbl->QueryInterface(rendering_device, &IID_ID2D1Device2, (void **)&device2)))
            return hr;
        hr = device2->lpVtbl->GetDxgiDevice(device2, (IDXGIDevice **)&dxgi);
        if (FAILED(hr)) {
            device2->lpVtbl->Release(device2);
            return hr;
        }
    }
    hr = dxgi->lpVtbl->QueryInterface(dxgi, &IID_ID3D11Device1_, (void **)d3d);
    dxgi->lpVtbl->Release(dxgi);
    if (device2 && FAILED(hr))
        device2->lpVtbl->Release(device2);
    else
        *d2d = (ID2D1Device *)device2;
    return hr;
}

HRESULT DcDeviceCreate(UINT version, IUnknown *rendering_device, DcDevice **out)
{
    ID3D11Device *d3d;
    ID2D1Device *d2d;
    HRESULT hr = DcResolveRenderingDevice(rendering_device, &d3d, &d2d);
    if (FAILED(hr))
        return hr;
    DcDevice *device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
    if (!device) {
        if (d3d)
            d3d->lpVtbl->Release(d3d);
        if (d2d)
            d2d->lpVtbl->Release(d2d);
        return E_OUTOFMEMORY;
    }
    device->dev1 = (DcDeviceFace){&g_device1_vtbl, device};
    device->desktop = (DcDeviceFace){&g_desktop_vtbl, device};
    device->dev5 = (DcDeviceFace){&g_device5_vtbl, device};
    device->debug = (DcDeviceFace){&g_debug_vtbl, device};
    device->mime = (DcDeviceFace){&g_mime_vtbl, device};
    device->refs = 1;
    device->version = version;
    device->d3d = d3d;
    device->d2d = d2d;
    InitializeCriticalSection(&device->lock);
    DcObjectCountChange(1);
    devices_lock();
    device->next_global = g_devices;
    g_devices = device;
    LeaveCriticalSection(&g_devices_lock);
    *out = device;
    return S_OK;
}

/* ------------------------------------------------------------------ IUnknown */

/*
 * version 1 has the original device, version 2 adds the desktop device and
 * debug, version 3 the effects and textures of devices 3 to 5.
 */
static HRESULT STDMETHODCALLTYPE dev_query(void *This, REFIID riid, void **out)
{
    DcDevice *d = dev(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    void *found = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDCompositionDevice))
        found = &d->dev1;
    else if (IsEqualGUID(riid, &IID_IMimeDCompositionDevice))
        found = &d->mime;
    else if (d->version >= 1 && (IsEqualGUID(riid, &IID_IDCompositionDevice2) ||
                                 IsEqualGUID(riid, &IID_IDCompositionDesktopDevice)))
        found = &d->desktop;
    else if (d->version >= 1 && IsEqualGUID(riid, &IID_IDCompositionDeviceDebug))
        found = &d->debug;
    else if (d->version >= 2 && (IsEqualGUID(riid, &IID_IDCompositionDevice3) ||
                                 IsEqualGUID(riid, &IID_IDCompositionDevice4) ||
                                 IsEqualGUID(riid, &IID_IDCompositionDevice5)))
        found = &d->dev5;
    if (!found)
        return E_NOINTERFACE;
    DcDeviceAddRef(d);
    *out = found;
    return S_OK;
}

HRESULT DcDeviceQuery(DcDevice *device, REFIID riid, void **out) { return dev_query(&device->dev1, riid, out); }

static ULONG STDMETHODCALLTYPE dev_add_ref(void *This) { return DcDeviceAddRef(dev(This)); }
static ULONG STDMETHODCALLTYPE dev_release(void *This) { return DcDeviceRelease(dev(This)); }

/* ------------------------------------------------------------------ commit and statistics */

HRESULT DcDeviceCommit(DcDevice *d)
{
    DcDeviceLock(d);
    BOOL changed = d->dirty != nullptr || d->content_changed;
    d->content_changed = FALSE;
    while (d->dirty) {
        DcObject *object = d->dirty;
        DcObjectUndirty(object);
        object->cls->commit(object);
    }
    d->commit_count++;
    /* a changed transform, clip or effect may sit under any target. */
    if (changed) {
        for (DcTarget *t = d->targets; t; t = t->next_in_device)
            t->needs_compose = TRUE;
        DcComposeAll(d, TRUE);
    }
    DcDeviceUnlock(d);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dev_commit(void *This) { return DcDeviceCommit(dev(This)); }

/* the compositor draws during Commit, so there is nothing to wait for. */
static HRESULT STDMETHODCALLTYPE dev_wait_for_commit_completion(void *This) { return S_OK; }

static HRESULT STDMETHODCALLTYPE dev_get_frame_statistics(void *This, DCOMPOSITION_FRAME_STATISTICS *stats)
{
    if (!stats)
        return E_INVALIDARG;
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    UINT64 now = DcNow(), index = MimeVblankIndex(&vblank, now);
    memset(stats, 0, sizeof(*stats));
    stats->lastFrameTime.QuadPart = (LONGLONG)MimeVblankTime(&vblank, index);
    stats->currentCompositionRate.Numerator = vblank.rate;
    stats->currentCompositionRate.Denominator = 1;
    stats->currentTime.QuadPart = (LONGLONG)now;
    stats->timeFrequency.QuadPart = (LONGLONG)vblank.frequency;
    stats->nextEstimatedFrameTime.QuadPart = (LONGLONG)MimeVblankTime(&vblank, index + 1);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dev_check_device_state(void *This, BOOL *valid)
{
    DcDevice *d = dev(This);
    if (!valid)
        return E_INVALIDARG;
    *valid = TRUE;
    if (d->d3d && FAILED(d->d3d->lpVtbl->GetDeviceRemovedReason(d->d3d)))
        *valid = FALSE;
    return S_OK;
}

/* ------------------------------------------------------------------ targets, visuals and surfaces */

static HRESULT STDMETHODCALLTYPE dev_create_target_for_hwnd(void *This, HWND hwnd, BOOL topmost,
                                                            IDCompositionTarget **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateTarget(dev(This), hwnd, topmost, out);
}

static HRESULT STDMETHODCALLTYPE dev_create_visual(void *This, IDCompositionVisual3 **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateVisual(dev(This), out);
}

/* a surface's texture needs a Direct3D device, which a device made without one lacks. */
static HRESULT STDMETHODCALLTYPE dev_create_surface(void *This, UINT width, UINT height, DXGI_FORMAT format,
                                                    DXGI_ALPHA_MODE alpha, IDCompositionSurface **out)
{
    DcDevice *d = dev(This);
    if (!d->d3d) {
        if (out)
            *out = nullptr;
        return E_ILLEGAL_METHOD_CALL;
    }
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateSurface(d, d->d3d, d->d2d, FALSE, width, height, format, alpha, (void **)out);
}

static HRESULT STDMETHODCALLTYPE dev_create_virtual_surface(void *This, UINT width, UINT height, DXGI_FORMAT format,
                                                            DXGI_ALPHA_MODE alpha, IDCompositionVirtualSurface **out)
{
    DcDevice *d = dev(This);
    if (!d->d3d) {
        if (out)
            *out = nullptr;
        return E_ILLEGAL_METHOD_CALL;
    }
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateSurface(d, d->d3d, d->d2d, TRUE, width, height, format, alpha, (void **)out);
}

static HRESULT STDMETHODCALLTYPE dev_create_surface_factory(void *This, IUnknown *rendering_device,
                                                            IDCompositionSurfaceFactory **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!rendering_device)
        return E_INVALIDARG;
    ID3D11Device *d3d;
    ID2D1Device *d2d;
    HRESULT hr = DcResolveRenderingDevice(rendering_device, &d3d, &d2d);
    if (FAILED(hr))
        return hr;
    hr = DcCreateSurfaceFactory(dev(This), d3d, d2d, out);
    d3d->lpVtbl->Release(d3d);
    if (d2d)
        d2d->lpVtbl->Release(d2d);
    return hr;
}

/* a handle from another process or device: the surface shows what is bound to it. */
static HRESULT STDMETHODCALLTYPE dev_create_surface_from_handle(void *This, HANDLE handle, IUnknown **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateSurfaceFromHandle(dev(This), handle, out);
}

/* window contents are not offered as a surface. */
static HRESULT STDMETHODCALLTYPE dev_create_surface_from_hwnd(void *This, HWND hwnd, IUnknown **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return IsWindow(hwnd) ? E_NOTIMPL : E_HANDLE_HR;
}

/* ------------------------------------------------------------------ transforms, clips, effects and animations */

#define CREATE_TRANSFORM(name, kind, type)                                                                             \
    static HRESULT STDMETHODCALLTYPE dev_create_##name(void *This, type **out)                                         \
    {                                                                                                                  \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        *out = nullptr;                                                                                                \
        return DcCreateTransform(dev(This), kind, (void **)out);                                                       \
    }

CREATE_TRANSFORM(translate, DCT_TRANSLATE, IDCompositionTranslateTransform)
CREATE_TRANSFORM(scale, DCT_SCALE, IDCompositionScaleTransform)
CREATE_TRANSFORM(rotate, DCT_ROTATE, IDCompositionRotateTransform)
CREATE_TRANSFORM(skew, DCT_SKEW, IDCompositionSkewTransform)
CREATE_TRANSFORM(matrix, DCT_MATRIX, IDCompositionMatrixTransform)
CREATE_TRANSFORM(translate3d, DCT_TRANSLATE3D, IDCompositionTranslateTransform3D)
CREATE_TRANSFORM(scale3d, DCT_SCALE3D, IDCompositionScaleTransform3D)
CREATE_TRANSFORM(rotate3d, DCT_ROTATE3D, IDCompositionRotateTransform3D)
CREATE_TRANSFORM(matrix3d, DCT_MATRIX3D, IDCompositionMatrixTransform3D)

static HRESULT STDMETHODCALLTYPE dev_create_transform_group(void *This, IDCompositionTransform **transforms, UINT count,
                                                            IDCompositionTransform **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateTransformGroup(dev(This), FALSE, (void *const *)transforms, count, (void **)out);
}

static HRESULT STDMETHODCALLTYPE dev_create_transform3d_group(void *This, IDCompositionTransform3D **transforms,
                                                              UINT count, IDCompositionTransform3D **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateTransformGroup(dev(This), TRUE, (void *const *)transforms, count, (void **)out);
}

static HRESULT STDMETHODCALLTYPE dev_create_effect_group(void *This, IDCompositionEffectGroup **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateEffectGroup(dev(This), out);
}

static HRESULT STDMETHODCALLTYPE dev_create_rectangle_clip(void *This, IDCompositionRectangleClip **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateRectangleClip(dev(This), out);
}

static HRESULT STDMETHODCALLTYPE dev_create_animation(void *This, IDCompositionAnimation **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateAnimation(dev(This), out);
}

/* Direct2D effect graph filters that are not drawn return E_NOTIMPL. */
#define CREATE_FILTER(name, kind, type)                                                                                \
    static HRESULT STDMETHODCALLTYPE dev_create_##name(void *This, type **out)                                         \
    {                                                                                                                  \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        *out = nullptr;                                                                                                \
        return DcCreateFilter(dev(This), kind, (void **)out);                                                          \
    }

CREATE_FILTER(blur, DCF_BLUR, IDCompositionGaussianBlurEffect)
CREATE_FILTER(color_matrix, DCF_COLOR_MATRIX, IDCompositionColorMatrixEffect)
CREATE_FILTER(saturation, DCF_SATURATION, IDCompositionSaturationEffect)
CREATE_FILTER(table_transfer, DCF_TABLE, IDCompositionTableTransferEffect)

static HRESULT STDMETHODCALLTYPE dev_create_unsupported_effect(void *This, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ device 4 and 5 */

/* textures bound with fences are not supported, so every device reports no support. */
static HRESULT STDMETHODCALLTYPE dev_check_composition_texture_support(void *This, IUnknown *device, BOOL *supported)
{
    if (!device || !supported)
        return E_INVALIDARG;
    *supported = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dev_create_composition_texture(void *This, IUnknown *texture,
                                                                IDCompositionTexture **out)
{
    if (!texture || !out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateTexture(dev(This), texture, out);
}

static HRESULT STDMETHODCALLTYPE dev_create_dynamic_texture(void *This, IDCompositionDynamicTexture **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateDynamicTexture(dev(This), out);
}

/* ------------------------------------------------------------------ the tables */

const IDCompositionDeviceVtbl g_device1_vtbl = {
    VTBL_FN(dev_query),
    VTBL_FN(dev_add_ref),
    VTBL_FN(dev_release),
    VTBL_FN(dev_commit),
    VTBL_FN(dev_wait_for_commit_completion),
    VTBL_FN(dev_get_frame_statistics),
    VTBL_FN(dev_create_target_for_hwnd),
    VTBL_FN(dev_create_visual),
    VTBL_FN(dev_create_surface),
    VTBL_FN(dev_create_virtual_surface),
    VTBL_FN(dev_create_surface_from_handle),
    VTBL_FN(dev_create_surface_from_hwnd),
    VTBL_FN(dev_create_translate),
    VTBL_FN(dev_create_scale),
    VTBL_FN(dev_create_rotate),
    VTBL_FN(dev_create_skew),
    VTBL_FN(dev_create_matrix),
    VTBL_FN(dev_create_transform_group),
    VTBL_FN(dev_create_translate3d),
    VTBL_FN(dev_create_scale3d),
    VTBL_FN(dev_create_rotate3d),
    VTBL_FN(dev_create_matrix3d),
    VTBL_FN(dev_create_transform3d_group),
    VTBL_FN(dev_create_effect_group),
    VTBL_FN(dev_create_rectangle_clip),
    VTBL_FN(dev_create_animation),
    VTBL_FN(dev_check_device_state),
};

#define DEVICE2_METHODS                                                                                                \
    VTBL_FN(dev_query), VTBL_FN(dev_add_ref), VTBL_FN(dev_release), VTBL_FN(dev_commit),                               \
        VTBL_FN(dev_wait_for_commit_completion), VTBL_FN(dev_get_frame_statistics), VTBL_FN(dev_create_visual),        \
        VTBL_FN(dev_create_surface_factory), VTBL_FN(dev_create_surface), VTBL_FN(dev_create_virtual_surface),         \
        VTBL_FN(dev_create_translate), VTBL_FN(dev_create_scale), VTBL_FN(dev_create_rotate),                          \
        VTBL_FN(dev_create_skew), VTBL_FN(dev_create_matrix), VTBL_FN(dev_create_transform_group),                     \
        VTBL_FN(dev_create_translate3d), VTBL_FN(dev_create_scale3d), VTBL_FN(dev_create_rotate3d),                    \
        VTBL_FN(dev_create_matrix3d), VTBL_FN(dev_create_transform3d_group), VTBL_FN(dev_create_effect_group),         \
        VTBL_FN(dev_create_rectangle_clip), VTBL_FN(dev_create_animation)

const IDCompositionDesktopDeviceVtbl g_desktop_vtbl = {
    DEVICE2_METHODS,
    VTBL_FN(dev_create_target_for_hwnd),
    VTBL_FN(dev_create_surface_from_handle),
    VTBL_FN(dev_create_surface_from_hwnd),
};

const IDCompositionDevice5Vtbl g_device5_vtbl = {
    DEVICE2_METHODS,
    VTBL_FN(dev_create_blur),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_color_matrix),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_saturation),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_table_transfer),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_create_unsupported_effect),
    VTBL_FN(dev_check_composition_texture_support),
    VTBL_FN(dev_create_composition_texture),
    VTBL_FN(dev_create_dynamic_texture),
};

static HRESULT STDMETHODCALLTYPE debug_counters(void *This) { return S_OK; }

const IDCompositionDeviceDebugVtbl g_debug_vtbl = {
    VTBL_FN(dev_query), VTBL_FN(dev_add_ref), VTBL_FN(dev_release), VTBL_FN(debug_counters), VTBL_FN(debug_counters),
};

/* ------------------------------------------------------------------ reading a target back */

static HRESULT STDMETHODCALLTYPE mime_read_target(void *This, IDCompositionTarget *target, const RECT *rect,
                                                  void *pixels, UINT pitch)
{
    DcDevice *d = dev(This);
    DcObject *object = target ? DcObjectFrom((IUnknown *)target) : nullptr;
    if (!object || object->cls->kind != DC_TARGET || object->device != d || !pixels) {
        if (object)
            DcObjectRelease(object);
        return E_INVALIDARG;
    }
    DcDeviceLock(d);
    HRESULT hr = DcReadTarget(CONTAINING_RECORD(object, DcTarget, base), rect, pixels, pitch);
    DcDeviceUnlock(d);
    DcObjectRelease(object);
    return hr;
}

const IMimeDCompositionDeviceVtbl g_mime_vtbl = {
    VTBL_FN(dev_query), VTBL_FN(dev_add_ref), VTBL_FN(dev_release), VTBL_FN(mime_read_target),
};
