/*
 * output duplication: IDXGIOutputDuplication over win32k's capture (guest/win32k/capture.c). the kernel composes
 * the monitor, gives the rows that changed into a buffer of the process and keeps the rectangles and the
 * pointer's image apart; a frame here is that buffer written into the program's Direct3D 11 texture, which is
 * the one resource every AcquireNextFrame returns.
 */
#include "dxgi_private.h"
#include <mime/ntuser.h>
#include <mime/vblank.h>
#include <mime/win32/d3d11.h>

#define DUPLICATION_RECTS 128

typedef struct DxgiDuplication {
    IDXGIOutputDuplication iface;
    LONG refs;
    CRITICAL_SECTION lock;
    IDXGIOutput *output;
    IUnknown *device;
    ID3D11Device *d3d;
    ID3D11DeviceContext *context;
    ID3D11Multithread *multithread;
    ID3D11Texture2D *texture;
    IDXGIResource *resource;
    ULONG capture;
    DXGI_OUTDUPL_DESC desc;
    DXGI_FORMAT format;
    UINT width, height;
    BYTE *pixels;
    /* a frame was acquired and not released, another is being waited for, and the kernel lost the monitor. */
    BOOL acquired, acquiring, lost;
    UINT dirty_count;
    RECT dirty[DUPLICATION_RECTS];
    /* the image of the pointer as the last frame that had one gave it. */
    MIME_CAPTURE_SHAPE shape;
    BYTE *shape_pixels;
    MimePrivateData data;
} DxgiDuplication;

static DxgiDuplication *impl(IDXGIOutputDuplication *iface)
{
    return CONTAINING_RECORD(iface, DxgiDuplication, iface);
}

/* the errors dxgi.dll makes of the kernel's statuses. */
static HRESULT status_to_hresult(LONG status)
{
    switch ((ULONG)status) {
    case 0:
        return S_OK;
    case 0xC0000225: /* STATUS_NOT_FOUND: the monitor is not what the duplication was made for. */
        return DXGI_ERROR_ACCESS_LOST;
    case 0xC00000AB: /* STATUS_INSTANCE_NOT_AVAILABLE */
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    case 0xC0000022: /* STATUS_ACCESS_DENIED */
        return E_ACCESSDENIED;
    case 0xC0000023: /* STATUS_BUFFER_TOO_SMALL */
        return DXGI_ERROR_MORE_DATA;
    case 0xC0000017: /* STATUS_NO_MEMORY */
        return E_OUTOFMEMORY;
    case 0xC000000D: /* STATUS_INVALID_PARAMETER */
        return E_INVALIDARG;
    default:
        return E_FAIL;
    }
}

static HRESULT STDMETHODCALLTYPE dup_query(IDXGIOutputDuplication *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIOutputDuplication)) {
        InterlockedIncrement(&impl(iface)->refs);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dup_add_ref(IDXGIOutputDuplication *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE dup_release(IDXGIOutputDuplication *iface)
{
    DxgiDuplication *d = impl(iface);
    ULONG refs = InterlockedDecrement(&d->refs);
    if (!refs) {
        if (d->capture)
            NtUserMimeCapture(MIME_CAPOP_CLOSE, d->capture, nullptr, nullptr, 0);
        if (d->resource)
            d->resource->lpVtbl->Release(d->resource);
        if (d->texture)
            d->texture->lpVtbl->Release(d->texture);
        if (d->multithread)
            d->multithread->lpVtbl->Release(d->multithread);
        if (d->context)
            d->context->lpVtbl->Release(d->context);
        if (d->d3d)
            d->d3d->lpVtbl->Release(d->d3d);
        if (d->device)
            d->device->lpVtbl->Release(d->device);
        if (d->output)
            d->output->lpVtbl->Release(d->output);
        MimePrivateClear(&d->data);
        HeapFree(GetProcessHeap(), 0, d->pixels);
        HeapFree(GetProcessHeap(), 0, d->shape_pixels);
        DeleteCriticalSection(&d->lock);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE dup_set_private_data(IDXGIOutputDuplication *iface, REFGUID guid, UINT size,
                                                      const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE dup_set_private_data_interface(IDXGIOutputDuplication *iface, REFGUID guid,
                                                                const IUnknown *unknown)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE dup_get_private_data(IDXGIOutputDuplication *iface, REFGUID guid, UINT *size,
                                                      void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

/* the parent of a duplication is its output. */
static HRESULT STDMETHODCALLTYPE dup_get_parent(IDXGIOutputDuplication *iface, REFIID riid, void **parent)
{
    if (!parent)
        return DXGI_ERROR_INVALID_CALL;
    IDXGIOutput *output = impl(iface)->output;
    return output->lpVtbl->QueryInterface(output, riid, parent);
}

static void STDMETHODCALLTYPE dup_get_desc(IDXGIOutputDuplication *iface, DXGI_OUTDUPL_DESC *desc)
{
    *desc = impl(iface)->desc;
}

/* sleeps to the next blank of the display, or to the end of the wait when that comes first. */
static void sleep_to_blank(ULONGLONG remaining_ms)
{
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    UINT64 next = MimeVblankTime(&vblank, MimeVblankIndex(&vblank, now.QuadPart) + 1);
    ULONGLONG ms = (next - now.QuadPart) * 1000 / vblank.frequency;
    Sleep((DWORD)(ms < remaining_ms ? (ms ? ms : 1) : remaining_ms));
}

/* the rows the kernel wrote into the texture, one rectangle at a time. */
static void upload(DxgiDuplication *d)
{
    ID3D11DeviceContext *c = d->context;
    UINT pitch = d->width * 4;
    if (d->multithread)
        d->multithread->lpVtbl->Enter(d->multithread);
    for (UINT i = 0; i < d->dirty_count; i++) {
        const RECT *r = &d->dirty[i];
        D3D11_BOX box = {(UINT)r->left, (UINT)r->top, 0, (UINT)r->right, (UINT)r->bottom, 1};
        const BYTE *source = d->pixels + (SIZE_T)r->top * pitch + (SIZE_T)r->left * 4;
        BYTE *swapped = nullptr;
        if (d->format == DXGI_FORMAT_R8G8B8A8_UNORM || d->format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            /* the kernel gives bgra, this texture reads rgba. */
            UINT w = (UINT)(r->right - r->left);
            swapped = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * (r->bottom - r->top) * 4);
            if (!swapped)
                continue;
            for (LONG y = r->top; y < r->bottom; y++) {
                const UINT *in = (const UINT *)(d->pixels + (SIZE_T)y * pitch) + r->left;
                UINT *out = (UINT *)(swapped + (SIZE_T)(y - r->top) * w * 4);
                for (UINT x = 0; x < w; x++)
                    out[x] = (in[x] & 0xFF00FF00) | (in[x] >> 16 & 0xFF) | (in[x] & 0xFF) << 16;
            }
            c->lpVtbl->UpdateSubresource(c, (ID3D11Resource *)d->texture, 0, &box, swapped, w * 4, 0);
            HeapFree(GetProcessHeap(), 0, swapped);
            continue;
        }
        c->lpVtbl->UpdateSubresource(c, (ID3D11Resource *)d->texture, 0, &box, source, pitch, 0);
    }
    if (d->multithread)
        d->multithread->lpVtbl->Leave(d->multithread);
}

static HRESULT STDMETHODCALLTYPE dup_acquire_next_frame(IDXGIOutputDuplication *iface, UINT timeout,
                                                        DXGI_OUTDUPL_FRAME_INFO *info, IDXGIResource **resource)
{
    DxgiDuplication *d = impl(iface);
    EnterCriticalSection(&d->lock);
    /* a frame not released yet, or another thread waiting, is an invalid call before anything else is checked. */
    if (d->acquired || d->acquiring) {
        LeaveCriticalSection(&d->lock);
        return DXGI_ERROR_INVALID_CALL;
    }
    if (!info || !resource) {
        LeaveCriticalSection(&d->lock);
        return E_INVALIDARG;
    }
    if (d->lost) {
        LeaveCriticalSection(&d->lock);
        return DXGI_ERROR_ACCESS_LOST;
    }
    d->acquiring = TRUE;
    LeaveCriticalSection(&d->lock);

    /* frames come with the display's blanks: the kernel is asked once a blank until one has changed. */
    ULONGLONG start = GetTickCount64();
    MIME_CAPTURE_FRAME frame;
    LONG status;
    for (;;) {
        status = NtUserMimeCapture(MIME_CAPOP_FRAME, d->capture, (PUCHAR)&frame, d->pixels, d->width * d->height * 4);
        if (status != STATUS_TIMEOUT)
            break;
        ULONGLONG elapsed = GetTickCount64() - start;
        if (timeout != INFINITE && elapsed >= timeout)
            break;
        sleep_to_blank(timeout == INFINITE ? 1000 : timeout - elapsed);
    }

    EnterCriticalSection(&d->lock);
    d->acquiring = FALSE;
    if (status == STATUS_TIMEOUT) {
        LeaveCriticalSection(&d->lock);
        return DXGI_ERROR_WAIT_TIMEOUT;
    }
    if (status < 0) {
        HRESULT hr = status_to_hresult(status);
        if (hr == DXGI_ERROR_ACCESS_LOST)
            d->lost = TRUE;
        LeaveCriticalSection(&d->lock);
        return hr;
    }
    d->dirty_count = frame.DirtyBytes / sizeof(RECT);
    if (d->dirty_count > DUPLICATION_RECTS)
        d->dirty_count = DUPLICATION_RECTS;
    if (d->dirty_count && NtUserMimeCapture(MIME_CAPOP_DIRTY, d->capture, nullptr, (PUCHAR)d->dirty,
                                            d->dirty_count * sizeof(RECT)) < 0)
        d->dirty_count = 0;
    if (frame.ShapeBytes) {
        MIME_CAPTURE_SHAPE shape;
        BYTE *pixels = HeapAlloc(GetProcessHeap(), 0, frame.ShapeBytes);
        if (pixels && NtUserMimeCapture(MIME_CAPOP_SHAPE, d->capture, (PUCHAR)&shape, pixels, frame.ShapeBytes) >= 0) {
            HeapFree(GetProcessHeap(), 0, d->shape_pixels);
            d->shape_pixels = pixels;
            d->shape = shape;
        } else {
            HeapFree(GetProcessHeap(), 0, pixels);
        }
    }
    upload(d);

    memset(info, 0, sizeof(*info));
    info->LastPresentTime.LowPart = frame.LastPresentLow;
    info->LastPresentTime.HighPart = (LONG)frame.LastPresentHigh;
    info->LastMouseUpdateTime.LowPart = frame.LastMouseLow;
    info->LastMouseUpdateTime.HighPart = (LONG)frame.LastMouseHigh;
    info->AccumulatedFrames = frame.Accumulated;
    info->RectsCoalesced = frame.RectsCoalesced;
    info->PointerPosition.Position.x = frame.PointerX;
    info->PointerPosition.Position.y = frame.PointerY;
    info->PointerPosition.Visible = frame.PointerVisible;
    info->TotalMetadataBufferSize = frame.DirtyBytes + frame.MoveBytes;
    info->PointerShapeBufferSize = frame.ShapeBytes;
    d->resource->lpVtbl->AddRef(d->resource);
    *resource = d->resource;
    d->acquired = TRUE;
    LeaveCriticalSection(&d->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dup_get_frame_dirty_rects(IDXGIOutputDuplication *iface, UINT size, RECT *rects,
                                                           UINT *required)
{
    DxgiDuplication *d = impl(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&d->lock);
    if (!d->acquired) {
        hr = DXGI_ERROR_INVALID_CALL;
    } else if (!rects || !required) {
        hr = E_INVALIDARG;
    } else {
        UINT need = d->dirty_count * (UINT)sizeof(RECT);
        *required = need;
        if (size < need)
            hr = DXGI_ERROR_MORE_DATA;
        else
            memcpy(rects, d->dirty, need);
    }
    LeaveCriticalSection(&d->lock);
    return hr;
}

/* the desktop never moves a block of its pixels here, windows that move are rectangles that changed. */
static HRESULT STDMETHODCALLTYPE dup_get_frame_move_rects(IDXGIOutputDuplication *iface, UINT size,
                                                          DXGI_OUTDUPL_MOVE_RECT *rects, UINT *required)
{
    DxgiDuplication *d = impl(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&d->lock);
    if (!d->acquired) {
        hr = DXGI_ERROR_INVALID_CALL;
    } else if (!rects || !required) {
        hr = E_INVALIDARG;
    } else {
        *required = 0;
    }
    LeaveCriticalSection(&d->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dup_get_frame_pointer_shape(IDXGIOutputDuplication *iface, UINT size, void *buffer,
                                                             UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *info)
{
    DxgiDuplication *d = impl(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&d->lock);
    if (!d->acquired) {
        hr = DXGI_ERROR_INVALID_CALL;
    } else if (!buffer || !required || !info) {
        hr = E_INVALIDARG;
    } else {
        *required = d->shape.Size;
        if (size < d->shape.Size) {
            hr = DXGI_ERROR_MORE_DATA;
        } else {
            memcpy(buffer, d->shape_pixels, d->shape.Size);
            info->Type = d->shape.Type;
            info->Width = d->shape.Width;
            info->Height = d->shape.Height;
            info->Pitch = d->shape.Pitch;
            info->HotSpot.x = d->shape.HotspotX;
            info->HotSpot.y = d->shape.HotspotY;
        }
    }
    LeaveCriticalSection(&d->lock);
    return hr;
}

/* the desktop image is in video memory, which only the system memory kind of duplication maps. */
static HRESULT STDMETHODCALLTYPE dup_map_desktop_surface(IDXGIOutputDuplication *iface, DXGI_MAPPED_RECT *rect)
{
    if (!rect)
        return E_INVALIDARG;
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dup_unmap_desktop_surface(IDXGIOutputDuplication *iface)
{
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE dup_release_frame(IDXGIOutputDuplication *iface)
{
    DxgiDuplication *d = impl(iface);
    EnterCriticalSection(&d->lock);
    HRESULT hr = d->acquired ? S_OK : DXGI_ERROR_INVALID_CALL;
    d->acquired = FALSE;
    LeaveCriticalSection(&d->lock);
    return hr;
}

static const IDXGIOutputDuplicationVtbl dup_vtbl = {
    dup_query,
    dup_add_ref,
    dup_release,
    dup_set_private_data,
    dup_set_private_data_interface,
    dup_get_private_data,
    dup_get_parent,
    dup_get_desc,
    dup_acquire_next_frame,
    dup_get_frame_dirty_rects,
    dup_get_frame_move_rects,
    dup_get_frame_pointer_shape,
    dup_map_desktop_surface,
    dup_unmap_desktop_surface,
    dup_release_frame,
};

/*
 * the format of the texture frames are written to: the desktop's own when the program lists it, else the first
 * of the listed formats that are the same pixels in another order or encoding.
 */
static BOOL pick_format(UINT count, const DXGI_FORMAT *formats, DXGI_FORMAT *picked)
{
    for (UINT i = 0; i < count; i++)
        if (formats[i] == DXGI_FORMAT_B8G8R8A8_UNORM) {
            *picked = formats[i];
            return TRUE;
        }
    for (UINT i = 0; i < count; i++)
        if (formats[i] == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || formats[i] == DXGI_FORMAT_R8G8B8A8_UNORM ||
            formats[i] == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            *picked = formats[i];
            return TRUE;
        }
    return FALSE;
}

/* a monitor's number, the one its device name ends with. */
BOOL DxgiMonitorNumber(HMONITOR monitor, UINT *number)
{
    MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info))
        return FALSE;
    size_t n = lstrlenW(info.szDevice);
    ULONG value = 0, scale = 1;
    while (n && info.szDevice[n - 1] >= L'0' && info.szDevice[n - 1] <= L'9') {
        value += (info.szDevice[n - 1] - L'0') * scale;
        scale *= 10;
        n--;
    }
    if (!value)
        return FALSE;
    *number = value;
    return TRUE;
}

HRESULT DxgiDuplicateOutput(IDXGIOutput *output, DxgiAdapter *adapter, HMONITOR monitor, IUnknown *device, BOOL v1,
                            UINT flags, UINT count, const DXGI_FORMAT *formats, IDXGIOutputDuplication **out)
{
    if (!out || !device)
        return E_INVALIDARG;
    *out = nullptr;
    if (v1) {
        /* only the compositor's capture of the interface is a flag, and the call needs per monitor dpi. */
        if (flags & ~1u || !count || !formats)
            return E_INVALIDARG;
        if (GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()) != DPI_AWARENESS_PER_MONITOR_AWARE)
            return DXGI_ERROR_UNSUPPORTED;
    } else {
        static const DXGI_FORMAT desktop = DXGI_FORMAT_B8G8R8A8_UNORM;
        count = 1;
        formats = &desktop;
    }
    DXGI_FORMAT format;
    if (!pick_format(count, formats, &format))
        return E_INVALIDARG;

    ID3D11Device *d3d;
    if (FAILED(device->lpVtbl->QueryInterface(device, &IID_ID3D11Device, (void **)&d3d)))
        return E_NOINTERFACE;
    /* the device has to be on the output's adapter. */
    IDXGIDevice *dxgi_device;
    HRESULT hr = d3d->lpVtbl->QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter *device_adapter;
        if (SUCCEEDED(dxgi_device->lpVtbl->GetAdapter(dxgi_device, &device_adapter))) {
            DXGI_ADAPTER_DESC a, b;
            if (SUCCEEDED(device_adapter->lpVtbl->GetDesc(device_adapter, &a)) &&
                SUCCEEDED(adapter->iface.lpVtbl->GetDesc(&adapter->iface, &b)) &&
                (a.AdapterLuid.LowPart != b.AdapterLuid.LowPart || a.AdapterLuid.HighPart != b.AdapterLuid.HighPart))
                hr = E_INVALIDARG;
            device_adapter->lpVtbl->Release(device_adapter);
        }
        dxgi_device->lpVtbl->Release(dxgi_device);
    }
    if (FAILED(hr)) {
        d3d->lpVtbl->Release(d3d);
        return hr;
    }

    UINT index;
    MIME_CAPTURE_OPEN open = {};
    /* an output that is not attached any more (dxgi.dll's own check, read from DuplicateOutputInternal). */
    if (!DxgiMonitorNumber(monitor, &index)) {
        d3d->lpVtbl->Release(d3d);
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    open.Monitor = index - 1;
    open.Flags = flags;
    LONG status = NtUserMimeCapture(MIME_CAPOP_OPEN, 0, (PUCHAR)&open, nullptr, 0);
    hr = status == (LONG)0xC0000225 ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : status_to_hresult(status);
    if (FAILED(hr)) {
        d3d->lpVtbl->Release(d3d);
        return hr;
    }

    DxgiDuplication *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) {
        NtUserMimeCapture(MIME_CAPOP_CLOSE, open.Handle, nullptr, nullptr, 0);
        d3d->lpVtbl->Release(d3d);
        return E_OUTOFMEMORY;
    }
    d->iface.lpVtbl = &dup_vtbl;
    d->refs = 1;
    InitializeCriticalSection(&d->lock);
    d->capture = open.Handle;
    d->d3d = d3d;
    d->device = device;
    device->lpVtbl->AddRef(device);
    d->output = output;
    output->lpVtbl->AddRef(output);
    d->width = open.Width;
    d->height = open.Height;
    d->format = format;
    d->desc.ModeDesc.Width = open.Width;
    d->desc.ModeDesc.Height = open.Height;
    d->desc.ModeDesc.RefreshRate = (DXGI_RATIONAL){open.RefreshNumerator, open.RefreshDenominator};
    d->desc.ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d->desc.ModeDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    d->desc.Rotation = DXGI_MODE_ROTATION_IDENTITY;
    d->pixels = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)open.Width * open.Height * 4);

    /* a texture the program can copy from, sample or draw over, as the frames of windows' duplications are. */
    D3D11_TEXTURE2D_DESC td = {.Width = open.Width,
                               .Height = open.Height,
                               .MipLevels = 1,
                               .ArraySize = 1,
                               .Format = format,
                               .SampleDesc = {1, 0},
                               .Usage = D3D11_USAGE_DEFAULT,
                               .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET};
    hr = d->pixels ? d3d->lpVtbl->CreateTexture2D(d3d, &td, nullptr, &d->texture) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
        hr = d->texture->lpVtbl->QueryInterface(d->texture, &IID_IDXGIResource, (void **)&d->resource);
    if (SUCCEEDED(hr)) {
        d3d->lpVtbl->GetImmediateContext(d3d, &d->context);
        if (d->context)
            d->context->lpVtbl->QueryInterface(d->context, &IID_ID3D11Multithread, (void **)&d->multithread);
        else
            hr = E_FAIL;
    }
    if (FAILED(hr)) {
        dup_release(&d->iface);
        return hr;
    }
    *out = &d->iface;
    return S_OK;
}
