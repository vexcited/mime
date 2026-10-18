/*
 * the dxgi device manager, sharing a direct3d 11 device between the parts
 * of a pipeline, and media buffers over a texture, read and written
 * through a staging copy while locked.
 */
#include "mfplat_private.h"
#include <mime/win32/d3d11.h>

/* ------------------------------------------------------------------ the device manager */

typedef struct {
    UINT_PTR id;
    /* cleared by ResetDevice, the handle then only closes. */
    BOOL valid;
    BOOL locked;
} DeviceHandle;

typedef struct {
    IMFDXGIDeviceManager iface;
    LONG refs;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE unlocked;
    UINT token;
    ID3D11Device *device;
    DeviceHandle *handles;
    DWORD count;
    DWORD capacity;
    /* the thread holding the device, and how many locks it holds. */
    DWORD owner;
    LONG locks;
} DeviceManager;

static DeviceManager *device_manager(IMFDXGIDeviceManager *iface)
{
    return CONTAINING_RECORD(iface, DeviceManager, iface);
}

static DeviceHandle *find_handle(DeviceManager *manager, HANDLE handle)
{
    for (DWORD i = 0; i < manager->count; i++) {
        if (manager->handles[i].id == (UINT_PTR)handle)
            return &manager->handles[i];
    }
    return nullptr;
}

static void release_lock(DeviceManager *manager, DeviceHandle *handle)
{
    handle->locked = FALSE;
    if (--manager->locks == 0) {
        manager->owner = 0;
        WakeAllConditionVariable(&manager->unlocked);
    }
}

static HRESULT STDMETHODCALLTYPE manager_query_interface(IMFDXGIDeviceManager *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFDXGIDeviceManager)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE manager_add_ref(IMFDXGIDeviceManager *iface)
{
    return InterlockedIncrement(&device_manager(iface)->refs);
}

static ULONG STDMETHODCALLTYPE manager_release(IMFDXGIDeviceManager *iface)
{
    DeviceManager *manager = device_manager(iface);
    ULONG refs = InterlockedDecrement(&manager->refs);
    if (!refs) {
        if (manager->device)
            manager->device->lpVtbl->Release(manager->device);
        DeleteCriticalSection(&manager->lock);
        MfFree(manager->handles);
        MfFree(manager);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE manager_close_device_handle(IMFDXGIDeviceManager *iface, HANDLE handle)
{
    DeviceManager *manager = device_manager(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&manager->lock);
    DeviceHandle *entry = find_handle(manager, handle);
    if (!entry) {
        hr = E_HANDLE;
    } else {
        if (entry->locked && manager->owner == GetCurrentThreadId())
            release_lock(manager, entry);
        *entry = manager->handles[--manager->count];
    }
    LeaveCriticalSection(&manager->lock);
    return hr;
}

static HRESULT get_service_locked(DeviceManager *manager, HANDLE handle, REFIID riid, void **service)
{
    if (!manager->device)
        return MF_E_DXGI_DEVICE_NOT_INITIALIZED;
    DeviceHandle *entry = find_handle(manager, handle);
    if (!entry)
        return E_HANDLE;
    if (!entry->valid)
        return MF_E_DXGI_NEW_VIDEO_DEVICE;
    return manager->device->lpVtbl->QueryInterface(manager->device, riid, service);
}

static HRESULT STDMETHODCALLTYPE manager_get_video_service(IMFDXGIDeviceManager *iface, HANDLE handle, REFIID riid,
                                                          void **service)
{
    DeviceManager *manager = device_manager(iface);
    if (!service)
        return E_POINTER;
    EnterCriticalSection(&manager->lock);
    HRESULT hr = get_service_locked(manager, handle, riid, service);
    LeaveCriticalSection(&manager->lock);
    return hr;
}

/* one thread at a time holds the device, as often as it locks it. */
static HRESULT STDMETHODCALLTYPE manager_lock_device(IMFDXGIDeviceManager *iface, HANDLE handle, REFIID riid,
                                                    void **object, BOOL block)
{
    DeviceManager *manager = device_manager(iface);
    if (!object)
        return E_POINTER;
    DWORD self = GetCurrentThreadId();
    HRESULT hr = S_OK;
    EnterCriticalSection(&manager->lock);
    DeviceHandle *entry = find_handle(manager, handle);
    if (!entry) {
        hr = E_HANDLE;
        goto done;
    }
    while (manager->locks && manager->owner != self) {
        if (!block) {
            hr = MF_E_DXGI_VIDEO_DEVICE_LOCKED;
            goto done;
        }
        SleepConditionVariableCS(&manager->unlocked, &manager->lock, INFINITE);
        entry = find_handle(manager, handle);
        if (!entry) {
            hr = E_HANDLE;
            goto done;
        }
    }
    hr = get_service_locked(manager, handle, riid, object);
    if (SUCCEEDED(hr)) {
        manager->owner = self;
        manager->locks++;
        entry->locked = TRUE;
    }
done:
    LeaveCriticalSection(&manager->lock);
    return hr;
}

/* the lowest number no other handle has. */
static HRESULT STDMETHODCALLTYPE manager_open_device_handle(IMFDXGIDeviceManager *iface, HANDLE *handle)
{
    DeviceManager *manager = device_manager(iface);
    if (!handle)
        return E_POINTER;
    HRESULT hr = S_OK;
    EnterCriticalSection(&manager->lock);
    if (!manager->device) {
        hr = MF_E_DXGI_DEVICE_NOT_INITIALIZED;
        goto done;
    }
    if (manager->count == manager->capacity) {
        DWORD capacity = manager->capacity * 2 + 8;
        DeviceHandle *handles = MfAlloc(capacity * sizeof(*handles));
        if (!handles) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        if (manager->count)
            memcpy(handles, manager->handles, manager->count * sizeof(*handles));
        MfFree(manager->handles);
        manager->handles = handles;
        manager->capacity = capacity;
    }
    UINT_PTR id = 1;
    while (find_handle(manager, (HANDLE)id))
        id++;
    manager->handles[manager->count++] = (DeviceHandle){id, TRUE, FALSE};
    *handle = (HANDLE)id;
done:
    LeaveCriticalSection(&manager->lock);
    return hr;
}

/* a new device for the token CreateDXGIDeviceManager gave, the open handles going stale. */
static HRESULT STDMETHODCALLTYPE manager_reset_device(IMFDXGIDeviceManager *iface, IUnknown *device, UINT token)
{
    DeviceManager *manager = device_manager(iface);
    if (token != manager->token || !device)
        return E_INVALIDARG;
    ID3D11Device *d3d11 = nullptr;
    if (FAILED(device->lpVtbl->QueryInterface(device, &IID_ID3D11Device, (void **)&d3d11)))
        return E_INVALIDARG;
    EnterCriticalSection(&manager->lock);
    if (manager->device)
        manager->device->lpVtbl->Release(manager->device);
    manager->device = d3d11;
    for (DWORD i = 0; i < manager->count; i++)
        manager->handles[i].valid = FALSE;
    LeaveCriticalSection(&manager->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_test_device(IMFDXGIDeviceManager *iface, HANDLE handle)
{
    DeviceManager *manager = device_manager(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&manager->lock);
    DeviceHandle *entry = find_handle(manager, handle);
    if (!manager->device)
        hr = MF_E_DXGI_DEVICE_NOT_INITIALIZED;
    else if (!entry)
        hr = E_HANDLE;
    else if (!entry->valid)
        hr = MF_E_DXGI_NEW_VIDEO_DEVICE;
    LeaveCriticalSection(&manager->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_unlock_device(IMFDXGIDeviceManager *iface, HANDLE handle, BOOL save_state)
{
    DeviceManager *manager = device_manager(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&manager->lock);
    DeviceHandle *entry = find_handle(manager, handle);
    if (!entry)
        hr = E_FAIL;
    else if (!entry->locked)
        hr = E_INVALIDARG;
    else if (manager->owner != GetCurrentThreadId())
        hr = MF_E_INVALIDREQUEST;
    else
        release_lock(manager, entry);
    LeaveCriticalSection(&manager->lock);
    return hr;
}

static const IMFDXGIDeviceManagerVtbl DEVICE_MANAGER_VTBL = {
    manager_query_interface,    manager_add_ref,      manager_release,
    manager_close_device_handle, manager_get_video_service, manager_lock_device,
    manager_open_device_handle, manager_reset_device, manager_test_device,
    manager_unlock_device,
};

/* the reset token is the tick count at creation, as windows makes it. */
HRESULT WINAPI MFCreateDXGIDeviceManager(UINT *token, IMFDXGIDeviceManager **out)
{
    if (!token || !out)
        return E_POINTER;
    DeviceManager *manager = MfAllocZero(sizeof(*manager));
    if (!manager)
        return E_OUTOFMEMORY;
    manager->iface.lpVtbl = &DEVICE_MANAGER_VTBL;
    manager->refs = 1;
    InitializeCriticalSection(&manager->lock);
    InitializeConditionVariable(&manager->unlocked);
    manager->token = GetTickCount();
    *token = manager->token;
    *out = &manager->iface;
    return S_OK;
}

static CRITICAL_SECTION g_shared_lock;
static INIT_ONCE g_shared_once = INIT_ONCE_STATIC_INIT;
static IMFDXGIDeviceManager *g_shared;
static UINT g_shared_token;
static LONG g_shared_locks;

static BOOL CALLBACK init_shared(INIT_ONCE *once, void *parameter, void **context)
{
    InitializeCriticalSection(&g_shared_lock);
    return TRUE;
}

/* the process' shared manager, made by its first lock and released with its last unlock. */
HRESULT WINAPI MFLockDXGIDeviceManager(UINT *token, IMFDXGIDeviceManager **manager)
{
    if (!manager)
        return E_POINTER;
    InitOnceExecuteOnce(&g_shared_once, init_shared, nullptr, nullptr);
    HRESULT hr = S_OK;
    EnterCriticalSection(&g_shared_lock);
    if (!g_shared)
        hr = MFCreateDXGIDeviceManager(&g_shared_token, &g_shared);
    if (SUCCEEDED(hr)) {
        g_shared_locks++;
        *manager = g_shared;
        g_shared->lpVtbl->AddRef(g_shared);
        if (token)
            *token = g_shared_token;
    }
    LeaveCriticalSection(&g_shared_lock);
    return hr;
}

HRESULT WINAPI MFUnlockDXGIDeviceManager(void)
{
    InitOnceExecuteOnce(&g_shared_once, init_shared, nullptr, nullptr);
    EnterCriticalSection(&g_shared_lock);
    if (g_shared_locks > 0 && --g_shared_locks == 0) {
        g_shared->lpVtbl->Release(g_shared);
        g_shared = nullptr;
        g_shared_token = 0;
    }
    LeaveCriticalSection(&g_shared_lock);
    return S_OK;
}

/* ------------------------------------------------------------------ surface buffers */

typedef struct {
    IMFMediaBuffer buffer;
    IMF2DBuffer2 buffer2d;
    IMFDXGIBuffer dxgi;
    LONG refs;
    CRITICAL_SECTION lock;
    ID3D11Texture2D *texture;
    UINT subresource;
    BOOL bottom_up;
    D3D11_TEXTURE2D_DESC desc;
    /* bytes per row and rows of the contiguous image, planes after each other. */
    DWORD pitch;
    DWORD rows;
    DWORD length;
    DWORD current_length;
    ID3D11Texture2D *staging;
    BYTE *image;
    LONG locks;
    IMFAttributes *attributes;
} SurfaceBuffer;

/* the row size and the rows of each format, chroma planes included. */
static void image_layout(DXGI_FORMAT format, UINT width, UINT height, DWORD *pitch, DWORD *rows)
{
    *rows = height;
    switch (format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
        *pitch = width;
        *rows = height + (height + 1) / 2;
        break;
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        *pitch = width * 2;
        *rows = height + (height + 1) / 2;
        break;
    case DXGI_FORMAT_YUY2:
    case DXGI_FORMAT_R8G8_UNORM:
        *pitch = width * 2;
        break;
    case DXGI_FORMAT_R8_UNORM:
        *pitch = width;
        break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_Y210:
        *pitch = width * 8;
        break;
    default:
        *pitch = width * 4;
        break;
    }
}

static BOOL rgb_format(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM ||
           format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R10G10B10A2_UNORM;
}

static SurfaceBuffer *surface_from_buffer(IMFMediaBuffer *iface)
{
    return CONTAINING_RECORD(iface, SurfaceBuffer, buffer);
}

static SurfaceBuffer *surface_from_2d(IMF2DBuffer2 *iface)
{
    return CONTAINING_RECORD(iface, SurfaceBuffer, buffer2d);
}

static SurfaceBuffer *surface_from_dxgi(IMFDXGIBuffer *iface)
{
    return CONTAINING_RECORD(iface, SurfaceBuffer, dxgi);
}

static HRESULT surface_query_interface(SurfaceBuffer *surface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaBuffer))
        *out = &surface->buffer;
    else if (IsEqualGUID(riid, &IID_IMF2DBuffer) || IsEqualGUID(riid, &IID_IMF2DBuffer2))
        *out = &surface->buffer2d;
    else if (IsEqualGUID(riid, &IID_IMFDXGIBuffer))
        *out = &surface->dxgi;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&surface->refs);
    return S_OK;
}

static ULONG surface_release(SurfaceBuffer *surface)
{
    ULONG refs = InterlockedDecrement(&surface->refs);
    if (!refs) {
        if (surface->staging)
            surface->staging->lpVtbl->Release(surface->staging);
        surface->texture->lpVtbl->Release(surface->texture);
        if (surface->attributes)
            surface->attributes->lpVtbl->Release(surface->attributes);
        MfFree(surface->image);
        DeleteCriticalSection(&surface->lock);
        MfFree(surface);
    }
    return refs;
}

/* the row of the image at row, upside down for a bottom up rgb buffer. */
static BYTE *image_row(SurfaceBuffer *surface, DWORD row)
{
    if (surface->bottom_up && rgb_format(surface->desc.Format))
        row = surface->rows - 1 - row;
    return surface->image + (SIZE_T)row * surface->pitch;
}

/* copies the subresource into the image through the staging texture, at the first lock. */
static HRESULT surface_map(SurfaceBuffer *surface)
{
    if (surface->locks++)
        return S_OK;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    HRESULT hr = S_OK;
    surface->texture->lpVtbl->GetDevice(surface->texture, &device);
    if (!device) {
        hr = E_FAIL;
        goto done;
    }
    if (!surface->staging) {
        D3D11_TEXTURE2D_DESC desc = surface->desc;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        hr = device->lpVtbl->CreateTexture2D(device, &desc, nullptr, &surface->staging);
        if (FAILED(hr))
            goto done;
    }
    if (!surface->image && !(surface->image = MfAllocZero(surface->length))) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    device->lpVtbl->GetImmediateContext(device, &context);
    context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource *)surface->staging, 0, 0, 0, 0,
                                           (ID3D11Resource *)surface->texture, surface->subresource, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->lpVtbl->Map(context, (ID3D11Resource *)surface->staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        DWORD copy = surface->pitch < mapped.RowPitch ? surface->pitch : mapped.RowPitch;
        for (DWORD row = 0; row < surface->rows; row++)
            memcpy(image_row(surface, row), (BYTE *)mapped.pData + (SIZE_T)row * mapped.RowPitch, copy);
        context->lpVtbl->Unmap(context, (ID3D11Resource *)surface->staging, 0);
    }
done:
    if (context)
        context->lpVtbl->Release(context);
    if (device)
        device->lpVtbl->Release(device);
    if (FAILED(hr))
        surface->locks--;
    return hr;
}

/* writes the image back into the subresource, at the last unlock. */
static HRESULT surface_unmap(SurfaceBuffer *surface)
{
    if (!surface->locks)
        return MF_E_INVALIDREQUEST;
    if (--surface->locks)
        return S_OK;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    surface->texture->lpVtbl->GetDevice(surface->texture, &device);
    if (!device)
        return E_FAIL;
    device->lpVtbl->GetImmediateContext(device, &context);
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->lpVtbl->Map(context, (ID3D11Resource *)surface->staging, 0, D3D11_MAP_WRITE, 0, &mapped);
    if (SUCCEEDED(hr)) {
        DWORD copy = surface->pitch < mapped.RowPitch ? surface->pitch : mapped.RowPitch;
        for (DWORD row = 0; row < surface->rows; row++)
            memcpy((BYTE *)mapped.pData + (SIZE_T)row * mapped.RowPitch, image_row(surface, row), copy);
        context->lpVtbl->Unmap(context, (ID3D11Resource *)surface->staging, 0);
        context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource *)surface->texture, surface->subresource, 0,
                                               0, 0, (ID3D11Resource *)surface->staging, 0, nullptr);
    }
    context->lpVtbl->Release(context);
    device->lpVtbl->Release(device);
    return hr;
}

/* ------------------------------------------------------------------ the IMFMediaBuffer of surfaces */

static HRESULT STDMETHODCALLTYPE surface_buffer_query_interface(IMFMediaBuffer *iface, REFIID riid, void **out)
{
    return surface_query_interface(surface_from_buffer(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE surface_buffer_add_ref(IMFMediaBuffer *iface)
{
    return InterlockedIncrement(&surface_from_buffer(iface)->refs);
}

static ULONG STDMETHODCALLTYPE surface_buffer_release(IMFMediaBuffer *iface)
{
    return surface_release(surface_from_buffer(iface));
}

static HRESULT STDMETHODCALLTYPE surface_lock(IMFMediaBuffer *iface, BYTE **data, DWORD *max_length,
                                              DWORD *current_length)
{
    SurfaceBuffer *surface = surface_from_buffer(iface);
    if (!data)
        return E_POINTER;
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_map(surface);
    if (SUCCEEDED(hr)) {
        *data = surface->image;
        if (max_length)
            *max_length = surface->length;
        if (current_length)
            *current_length = surface->current_length;
    }
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_unlock(IMFMediaBuffer *iface)
{
    SurfaceBuffer *surface = surface_from_buffer(iface);
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_unmap(surface);
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_get_current_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = surface_from_buffer(iface)->current_length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_set_current_length(IMFMediaBuffer *iface, DWORD length)
{
    SurfaceBuffer *surface = surface_from_buffer(iface);
    if (length > surface->length)
        return E_INVALIDARG;
    surface->current_length = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_get_max_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = surface_from_buffer(iface)->length;
    return S_OK;
}

static const IMFMediaBufferVtbl SURFACE_BUFFER_VTBL = {
    surface_buffer_query_interface, surface_buffer_add_ref,     surface_buffer_release, surface_lock,
    surface_unlock,                 surface_get_current_length, surface_set_current_length,
    surface_get_max_length,
};

/* ------------------------------------------------------------------ the IMF2DBuffer2 of surfaces */

static HRESULT STDMETHODCALLTYPE surface_2d_query_interface(IMF2DBuffer2 *iface, REFIID riid, void **out)
{
    return surface_query_interface(surface_from_2d(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE surface_2d_add_ref(IMF2DBuffer2 *iface)
{
    return InterlockedIncrement(&surface_from_2d(iface)->refs);
}

static ULONG STDMETHODCALLTYPE surface_2d_release(IMF2DBuffer2 *iface)
{
    return surface_release(surface_from_2d(iface));
}

static HRESULT STDMETHODCALLTYPE surface_lock_2d(IMF2DBuffer2 *iface, BYTE **scanline0, LONG *pitch)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    if (!scanline0 || !pitch)
        return E_POINTER;
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_map(surface);
    if (SUCCEEDED(hr)) {
        *scanline0 = surface->image;
        *pitch = (LONG)surface->pitch;
    }
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_unlock_2d(IMF2DBuffer2 *iface)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_unmap(surface);
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_get_scanline0_and_pitch(IMF2DBuffer2 *iface, BYTE **scanline0, LONG *pitch)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    if (!scanline0 || !pitch)
        return E_POINTER;
    if (!surface->locks)
        return MF_E_UNEXPECTED;
    *scanline0 = surface->image;
    *pitch = (LONG)surface->pitch;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_is_contiguous_format(IMF2DBuffer2 *iface, BOOL *contiguous)
{
    if (!contiguous)
        return E_POINTER;
    *contiguous = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_get_contiguous_length(IMF2DBuffer2 *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = surface_from_2d(iface)->length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_contiguous_copy_to(IMF2DBuffer2 *iface, BYTE *dest, DWORD size)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    if (size < surface->length)
        return E_INVALIDARG;
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_map(surface);
    if (SUCCEEDED(hr)) {
        memcpy(dest, surface->image, surface->length);
        surface->locks--;
    }
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_contiguous_copy_from(IMF2DBuffer2 *iface, const BYTE *source, DWORD size)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    if (size < surface->length)
        return E_INVALIDARG;
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_map(surface);
    if (SUCCEEDED(hr)) {
        memcpy(surface->image, source, surface->length);
        hr = surface_unmap(surface);
    }
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_lock_2d_size(IMF2DBuffer2 *iface, MF2DBuffer_LockFlags flags,
                                                      BYTE **scanline0, LONG *pitch, BYTE **start, DWORD *length)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    if (!start || !length)
        return E_POINTER;
    HRESULT hr = surface_lock_2d(iface, scanline0, pitch);
    if (SUCCEEDED(hr)) {
        *start = surface->image;
        *length = surface->length;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_copy_2d_to(IMF2DBuffer2 *iface, IMF2DBuffer2 *dest)
{
    SurfaceBuffer *surface = surface_from_2d(iface);
    if (!dest)
        return E_POINTER;
    EnterCriticalSection(&surface->lock);
    HRESULT hr = surface_map(surface);
    if (SUCCEEDED(hr)) {
        hr = dest->lpVtbl->ContiguousCopyFrom(dest, surface->image, surface->length);
        surface->locks--;
    }
    LeaveCriticalSection(&surface->lock);
    return hr;
}

static const IMF2DBuffer2Vtbl SURFACE_2D_VTBL = {
    surface_2d_query_interface,     surface_2d_add_ref,
    surface_2d_release,             surface_lock_2d,
    surface_unlock_2d,              surface_get_scanline0_and_pitch,
    surface_is_contiguous_format,   surface_get_contiguous_length,
    surface_contiguous_copy_to,     surface_contiguous_copy_from,
    surface_lock_2d_size,           surface_copy_2d_to,
};

/* ------------------------------------------------------------------ the IMFDXGIBuffer of surfaces */

static HRESULT STDMETHODCALLTYPE surface_dxgi_query_interface(IMFDXGIBuffer *iface, REFIID riid, void **out)
{
    return surface_query_interface(surface_from_dxgi(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE surface_dxgi_add_ref(IMFDXGIBuffer *iface)
{
    return InterlockedIncrement(&surface_from_dxgi(iface)->refs);
}

static ULONG STDMETHODCALLTYPE surface_dxgi_release(IMFDXGIBuffer *iface)
{
    return surface_release(surface_from_dxgi(iface));
}

static HRESULT STDMETHODCALLTYPE surface_get_resource(IMFDXGIBuffer *iface, REFIID riid, void **object)
{
    SurfaceBuffer *surface = surface_from_dxgi(iface);
    if (!object)
        return E_POINTER;
    return surface->texture->lpVtbl->QueryInterface(surface->texture, riid, object);
}

static HRESULT STDMETHODCALLTYPE surface_get_subresource_index(IMFDXGIBuffer *iface, UINT *index)
{
    if (!index)
        return E_POINTER;
    *index = surface_from_dxgi(iface)->subresource;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_get_unknown(IMFDXGIBuffer *iface, REFIID guid, REFIID riid, void **object)
{
    SurfaceBuffer *surface = surface_from_dxgi(iface);
    if (!object)
        return E_POINTER;
    return surface->attributes->lpVtbl->GetUnknown(surface->attributes, guid, riid, object);
}

static HRESULT STDMETHODCALLTYPE surface_set_unknown(IMFDXGIBuffer *iface, REFIID guid, IUnknown *object)
{
    SurfaceBuffer *surface = surface_from_dxgi(iface);
    if (!object)
        return surface->attributes->lpVtbl->DeleteItem(surface->attributes, guid);
    return surface->attributes->lpVtbl->SetUnknown(surface->attributes, guid, object);
}

static const IMFDXGIBufferVtbl SURFACE_DXGI_VTBL = {
    surface_dxgi_query_interface,  surface_dxgi_add_ref, surface_dxgi_release, surface_get_resource,
    surface_get_subresource_index, surface_get_unknown,  surface_set_unknown,
};

/* a buffer over one subresource of a texture 2d, its length that of the whole image. */
HRESULT WINAPI MFCreateDXGISurfaceBuffer(REFIID riid, IUnknown *object, UINT subresource, BOOL bottom_up,
                                         IMFMediaBuffer **out)
{
    if (!out || !object)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_ID3D11Texture2D))
        return E_INVALIDARG;
    ID3D11Texture2D *texture = nullptr;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_ID3D11Texture2D, (void **)&texture);
    if (FAILED(hr))
        return hr;
    SurfaceBuffer *surface = MfAllocZero(sizeof(*surface));
    IMFAttributes *attributes = nullptr;
    if (!surface || FAILED(MFCreateAttributes(&attributes, 0))) {
        MfFree(surface);
        texture->lpVtbl->Release(texture);
        return E_OUTOFMEMORY;
    }
    surface->buffer.lpVtbl = &SURFACE_BUFFER_VTBL;
    surface->buffer2d.lpVtbl = &SURFACE_2D_VTBL;
    surface->dxgi.lpVtbl = &SURFACE_DXGI_VTBL;
    surface->refs = 1;
    InitializeCriticalSection(&surface->lock);
    surface->texture = texture;
    surface->subresource = subresource;
    surface->bottom_up = bottom_up;
    surface->attributes = attributes;
    texture->lpVtbl->GetDesc(texture, &surface->desc);
    image_layout(surface->desc.Format, surface->desc.Width, surface->desc.Height, &surface->pitch, &surface->rows);
    surface->length = surface->pitch * surface->rows;
    surface->current_length = surface->length;
    *out = &surface->buffer;
    return S_OK;
}
