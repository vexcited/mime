/*
 * the sample allocator of MFCreateVideoSampleAllocator, evr's CMFVideoSampleAllocator: samples in system memory, or on
 * surfaces made through the video processor service of a direct3d 9 device manager. Read from evr.dll of windows 11
 * and measured with evralloc.exe.
 */
#include "evr_private.h"

#define MF_E_NOT_INITIALIZED_ ((HRESULT)0xC00D36B6L)
#define MF_E_INVALIDMEDIATYPE_ ((HRESULT)0xC00D36B4L)
#define MF_E_SAMPLEALLOCATOR_EMPTY_ ((HRESULT)0xC00D4A3EL)
#define D3DERR_DEVICELOST_ ((HRESULT)0x88760868L)
#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)

MIME_DEFINE_GUID(IID_IMFAsyncCallback_, 0xA27003CF, 0x2354, 0x4F2A, 0x8D, 0x6A, 0xAB, 0x7C, 0xFF, 0x15, 0x43, 0x7E);

/* a list of samples. the free list holds a reference on each, the others do not. */
typedef struct {
    IMFSample **items;
    DWORD count, capacity;
} SampleList;

typedef struct {
    IMFVideoSampleAllocator iface;
    IMFVideoSampleAllocatorCallback callback;
    IMFAsyncCallback on_delete;
    LONG refs;
    CRITICAL_SECTION lock, allocate_lock;
    SampleList free, outstanding, retired;
    DWORD initialized, requested, free_count;
    IDirect3DDeviceManager9 *manager;
    IUnknown *processor;
    IMFMediaType *type;
    IMFVideoSampleAllocatorNotify *notify;
    HANDLE returned;
    BOOL waiting;
} Allocator;

static BOOL list_add(SampleList *list, IMFSample *sample)
{
    if (list->count == list->capacity) {
        DWORD capacity = list->capacity ? list->capacity * 2 : 16;
        IMFSample **items = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*items));
        if (!items)
            return FALSE;
        if (list->count)
            memcpy(items, list->items, list->count * sizeof(*items));
        HeapFree(GetProcessHeap(), 0, list->items);
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = sample;
    return TRUE;
}

static IMFSample *list_take_head(SampleList *list)
{
    if (!list->count)
        return nullptr;
    IMFSample *sample = list->items[0];
    memmove(list->items, list->items + 1, --list->count * sizeof(*list->items));
    return sample;
}

/* the entries are compared by their IUnknown, as evr does. */
static HRESULT list_remove(SampleList *list, IUnknown *unknown, BOOL *found)
{
    *found = FALSE;
    for (DWORD i = 0; i < list->count; i++) {
        IUnknown *other = nullptr;
        HRESULT hr = list->items[i]->lpVtbl->QueryInterface(list->items[i], &IID_IUnknown, (void **)&other);
        if (FAILED(hr))
            return hr;
        other->lpVtbl->Release(other);
        if (other == unknown) {
            memmove(&list->items[i], &list->items[i + 1], (list->count - i - 1) * sizeof(*list->items));
            list->count--;
            *found = TRUE;
            break;
        }
    }
    return S_OK;
}

static void list_free(SampleList *list)
{
    HeapFree(GetProcessHeap(), 0, list->items);
    memset(list, 0, sizeof(*list));
}

static Allocator *allocator(IMFVideoSampleAllocator *iface)
{
    return CONTAINING_RECORD(iface, Allocator, iface);
}

/* the samples out with the caller are forgotten: when they come back they are dropped. */
static void uninitialize(Allocator *a)
{
    for (DWORD i = 0; i < a->outstanding.count; i++)
        list_add(&a->retired, a->outstanding.items[i]);
    a->outstanding.count = 0;
    IMFSample *sample;
    while ((sample = list_take_head(&a->free)))
        sample->lpVtbl->Release(sample);
    a->initialized = 0;
    a->free_count = 0;
    if (a->processor) {
        a->processor->lpVtbl->Release(a->processor);
        a->processor = nullptr;
    }
}

static HRESULT init_system_memory(Allocator *a, DWORD count, IMFMediaType *type)
{
    IUnknown *video = nullptr;
    HRESULT hr = type->lpVtbl->QueryInterface(type, &IID_IMFVideoMediaType, (void **)&video);
    if (FAILED(hr))
        return hr;
    const MFVIDEOFORMAT *format = EvrVideoFormat(video);
    if (!format) {
        hr = E_INVALIDARG;
    } else {
        DWORD fourcc = MFGetUncompressedVideoFormat(format);
        if (fourcc != D3DFMT_R8G8B8 && fourcc != D3DFMT_X8R8G8B8 && fourcc != D3DFMT_R5G6B5 &&
            fourcc != D3DFMT_X1R5G5B5)
            hr = MF_E_INVALIDMEDIATYPE_;
    }
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; i++) {
        IMFSample *sample = nullptr;
        IMFMediaBuffer *buffer = nullptr;
        hr = EvrCreateVideoSample(nullptr, &sample);
        if (SUCCEEDED(hr))
            hr = MFCreate2DMediaBuffer(format->videoInfo.dwWidth, format->videoInfo.dwHeight,
                                       MFGetUncompressedVideoFormat(format), TRUE, &buffer);
        if (SUCCEEDED(hr))
            hr = sample->lpVtbl->AddBuffer(sample, buffer);
        if (SUCCEEDED(hr) && !list_add(&a->free, sample))
            hr = E_OUTOFMEMORY;
        if (buffer)
            buffer->lpVtbl->Release(buffer);
        if (FAILED(hr) && sample)
            sample->lpVtbl->Release(sample);
    }
    video->lpVtbl->Release(video);
    return hr;
}

/* the bits of a pixel of a format, from evr's table of media subtypes. */
static UINT format_bits(D3DFORMAT format)
{
    static const struct {
        DWORD format, bits;
    } BITS[] = {
        {FCC('Y', 'U', 'Y', '2'), 16}, {FCC('U', 'Y', 'V', 'Y'), 16}, {FCC('S', '3', '4', '2'), 16},
        {FCC('P', '2', '0', '8'), 16}, {FCC('P', '2', '1', '0'), 32}, {FCC('P', '2', '1', '6'), 32},
        {FCC('Y', '2', '1', '0'), 32}, {FCC('Y', '2', '1', '6'), 32}, {FCC('Y', 'V', '1', '2'), 12},
        {FCC('N', 'V', '1', '2'), 12}, {FCC('N', 'V', '2', '4'), 12}, {FCC('I', 'M', 'C', '1'), 16},
        {FCC('I', 'M', 'C', '2'), 12}, {FCC('I', 'M', 'C', '3'), 16}, {FCC('I', 'M', 'C', '4'), 12},
        {FCC('S', '3', '4', '0'), 16}, {FCC('P', '0', '1', '0'), 24}, {FCC('P', '0', '1', '6'), 24},
        {FCC('4', '2', '0', 'O'), 12}, {FCC('N', 'V', '1', '1'), 12}, {FCC('A', 'I', '4', '4'), 8},
        {FCC('I', 'A', '4', '4'), 8},  {24, 16},                       {23, 16},
        {20, 24},                      {22, 32},                       {25, 16},
        {26, 16},                      {21, 32},                       {35, 32},
        {31, 32},                      {FCC('A', 'Y', 'U', 'V'), 32},  {FCC('P', '4', '0', '8'), 24},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(BITS); i++)
        if (BITS[i].format == (DWORD)format)
            return BITS[i].bits;
    return 0;
}

/* a new surface is painted black: zero luma and mid chroma for yuv, zero for rgb. */
static HRESULT paint_black(IDirect3DSurface9 *surface, const D3DSURFACE_DESC *desc, BOOL yuv)
{
    static const struct {
        DWORD format, step, pitch_div, width_div;
    } PLANAR[] = {
        {FCC('I', 'M', 'C', '1'), 1, 1, 2}, {FCC('I', 'M', 'C', '2'), 2, 1, 1}, {FCC('I', 'M', 'C', '3'), 1, 1, 2},
        {FCC('I', 'M', 'C', '4'), 2, 1, 1}, {FCC('Y', 'V', '1', '2'), 1, 2, 2}, {FCC('I', 'Y', 'U', 'V'), 1, 2, 2},
        {FCC('N', 'V', '1', '2'), 2, 1, 1}, {FCC('N', 'V', '2', '4'), 2, 1, 1}, {FCC('N', 'V', '1', '1'), 2, 1, 1},
    };
    D3DLOCKED_RECT locked = {0};
    HRESULT hr = surface->lpVtbl->LockRect(surface, &locked, nullptr, 0);
    if (hr != S_OK)
        return hr;
    BYTE *row = locked.pBits;
    if (yuv) {
        ULONG i = 0;
        while (i < RTL_NUMBER_OF(PLANAR) && PLANAR[i].format != (DWORD)desc->Format)
            i++;
        if (i < RTL_NUMBER_OF(PLANAR)) {
            UINT height = desc->Height;
            if (desc->Format == (D3DFORMAT)FCC('N', 'V', '2', '4'))
                height = (height + 31) & ~31u;
            for (UINT y = 0; y < height; y++, row += locked.Pitch)
                memset(row, 0, desc->Width);
            if (desc->Format == (D3DFORMAT)FCC('N', 'V', '2', '4'))
                height = ((height >> 1) + 31) & ~31u;
            UINT pitch = (UINT)locked.Pitch / PLANAR[i].pitch_div, width = desc->Width / PLANAR[i].width_div;
            for (UINT y = 0; y < height; y += PLANAR[i].step, row += pitch)
                memset(row, 128, width);
        } else {
            DWORD value, count = desc->Width >> 1;
            if (desc->Format == (D3DFORMAT)FCC('Y', 'U', 'Y', '2'))
                value = 0x80108010;
            else if (desc->Format == (D3DFORMAT)FCC('U', 'Y', 'V', 'Y'))
                value = 0x10801080;
            else if (desc->Format == (D3DFORMAT)FCC('A', 'Y', 'U', 'V'))
                value = 0x808010FF, count = desc->Width;
            else
                goto done;
            for (UINT y = 0; y < desc->Height; y++, row += 4 * (locked.Pitch / 4))
                for (DWORD x = 0; x < count; x++)
                    ((DWORD *)row)[x] = value;
        }
    } else {
        UINT bytes = (UINT)MulDiv((int)desc->Width, (int)format_bits(desc->Format), 8);
        UINT pitch = locked.Pitch < 0 ? (UINT)-locked.Pitch : (UINT)locked.Pitch;
        if (pitch < bytes)
            bytes = pitch;
        for (UINT y = 0; y < desc->Height; y++, row += locked.Pitch)
            memset(row, 0, bytes);
    }
done:
    surface->lpVtbl->UnlockRect(surface);
    return hr;
}

/* the first device that renders to something takes the surface, refused for argb on a software device. */
static HRESULT create_surface(IDirectXVideoProcessorService *service, const MFVIDEOFORMAT *format,
                              IDirect3DSurface9 **surface)
{
    D3DFORMAT fourcc = (D3DFORMAT)MFGetUncompressedVideoFormat(format);
    DXVA2_VideoDesc desc;
    EvrVideoDesc(format, &desc);
    UINT count = 0;
    GUID *guids = nullptr;
    HRESULT hr = service->lpVtbl->GetVideoProcessorDeviceGuids(service, &desc, &count, &guids);
    if (FAILED(hr))
        return hr;
    DXVA2_VideoProcessorCaps caps = {0};
    UINT i;
    for (i = 0; i < count; i++) {
        UINT targets = 0;
        D3DFORMAT *formats = nullptr;
        if (FAILED(service->lpVtbl->GetVideoProcessorRenderTargets(service, &guids[i], &desc, &targets, &formats)))
            continue;
        D3DFORMAT first = formats[0];
        CoTaskMemFree(formats);
        if (SUCCEEDED(service->lpVtbl->GetVideoProcessorCaps(service, &guids[i], &desc, first, &caps)) &&
            (desc.Format != D3DFMT_A8R8G8B8 || !(caps.DeviceCaps & DXVA2_VPDev_SoftwareDevice)) && targets)
            break;
    }
    if (i < count) {
        hr = service->lpVtbl->CreateSurface(service, format->videoInfo.dwWidth, format->videoInfo.dwHeight, 0, fourcc,
                                            caps.InputPool, 0, DXVA2_VideoSoftwareRenderTarget, surface, nullptr);
        if (SUCCEEDED(hr)) {
            D3DSURFACE_DESC surface_desc = {0};
            hr = (*surface)->lpVtbl->GetDesc(*surface, &surface_desc);
            if (SUCCEEDED(hr))
                paint_black(*surface, &surface_desc, EvrIsFormatYUV(fourcc));
        }
    } else {
        hr = MF_E_INVALIDMEDIATYPE_;
    }
    CoTaskMemFree(guids);
    return hr;
}

static HRESULT create_video_sample(IDirectXVideoProcessorService *service, BOOL bottom_up,
                                   const MFVIDEOFORMAT *format, IMFSample **out)
{
    IMFSample *sample = nullptr;
    IDirect3DSurface9 *surface = nullptr;
    IDirect3DDevice9 *device = nullptr;
    IMFMediaBuffer *buffer = nullptr;
    HRESULT hr = EvrCreateVideoSample(nullptr, &sample);
    if (SUCCEEDED(hr))
        hr = create_surface(service, format, &surface);
    if (SUCCEEDED(hr))
        hr = surface->lpVtbl->GetDevice(surface, &device);
    if (SUCCEEDED(hr)) {
        D3DDEVICE_CREATION_PARAMETERS parameters = {0};
        hr = device->lpVtbl->GetCreationParameters(device, &parameters);
    }
    if (SUCCEEDED(hr))
        hr = MFCreateDXSurfaceBuffer(&IID_IDirect3DSurface9, (IUnknown *)surface, bottom_up, &buffer);
    if (SUCCEEDED(hr))
        hr = sample->lpVtbl->AddBuffer(sample, buffer);
    if (SUCCEEDED(hr)) {
        sample->lpVtbl->AddRef(sample);
        *out = sample;
    }
    if (surface)
        surface->lpVtbl->Release(surface);
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    if (sample)
        sample->lpVtbl->Release(sample);
    if (device)
        device->lpVtbl->Release(device);
    return hr;
}

static HRESULT init_video_memory(Allocator *a, DWORD count, IDirectXVideoProcessorService *service,
                                 IMFMediaType *type)
{
    if (!service)
        return E_POINTER;
    IUnknown *video = nullptr;
    HRESULT hr = type->lpVtbl->QueryInterface(type, &IID_IMFVideoMediaType, (void **)&video);
    if (FAILED(hr))
        return hr;
    const MFVIDEOFORMAT *format = EvrVideoFormat(video);
    if (!format)
        hr = E_INVALIDARG;
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; i++) {
        IMFSample *sample = nullptr;
        hr = create_video_sample(service, (format->videoInfo.VideoFlags & 0x80000) != 0, format, &sample);
        if (SUCCEEDED(hr) && !list_add(&a->free, sample)) {
            sample->lpVtbl->Release(sample);
            hr = E_OUTOFMEMORY;
        }
    }
    video->lpVtbl->Release(video);
    return hr;
}

static BOOL types_equivalent(IMFMediaType *a, IMFMediaType *b)
{
    DWORD flags = 0;
    UINT64 size_a = 0, size_b = 0;
    if (!a || !b || FAILED(a->lpVtbl->IsEqual(a, b, &flags)) || (flags & 3) != 3)
        return FALSE;
    if (FAILED(a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_SIZE, &size_a)) ||
        FAILED(b->lpVtbl->GetUINT64(b, &MF_MT_FRAME_SIZE, &size_b)))
        return FALSE;
    return size_a == size_b;
}

static HRESULT open_service(Allocator *a, HANDLE *handle, IDirectXVideoProcessorService **service)
{
    HRESULT hr = a->manager->lpVtbl->OpenDeviceHandle(a->manager, handle);
    if (SUCCEEDED(hr))
        hr = a->manager->lpVtbl->GetVideoService(a->manager, *handle, &IID_IDirectXVideoProcessorService,
                                                 (void **)service);
    return hr;
}

static HRESULT initialize(Allocator *a, DWORD count, IMFMediaType *type)
{
    HANDLE handle = nullptr;
    IDirectXVideoProcessorService *service = nullptr;
    HRESULT hr = S_OK;
    if (types_equivalent(type, a->type) && count == a->initialized)
        return S_OK;
    uninitialize(a);
    if (!type) {
        hr = E_POINTER;
        goto done;
    }
    if (!count)
        count = 1;
    if (a->manager) {
        hr = open_service(a, &handle, &service);
        if (SUCCEEDED(hr))
            hr = init_video_memory(a, count, service, type);
        if (SUCCEEDED(hr))
            service->lpVtbl->QueryInterface(service, &IID_IUnknown, (void **)&a->processor);
    } else {
        hr = init_system_memory(a, count, type);
    }
    if (SUCCEEDED(hr)) {
        a->initialized = a->requested = a->free_count = count;
        if (type != a->type) {
            if (a->type)
                a->type->lpVtbl->Release(a->type);
            a->type = type;
            type->lpVtbl->AddRef(type);
        }
    }
done:
    if (handle)
        a->manager->lpVtbl->CloseDeviceHandle(a->manager, handle);
    if (FAILED(hr))
        uninitialize(a);
    if (service)
        service->lpVtbl->Release(service);
    return hr;
}

/* a new video processor service means a new device, and the samples are made again on it. */
static HRESULT check_video_device(Allocator *a)
{
    if (!a->manager)
        return S_OK;
    HANDLE handle = nullptr;
    IDirectXVideoProcessorService *service = nullptr;
    IUnknown *unknown = nullptr;
    HRESULT hr = a->manager->lpVtbl->OpenDeviceHandle(a->manager, &handle);
    if (SUCCEEDED(hr))
        hr = a->manager->lpVtbl->GetVideoService(a->manager, handle, &IID_IDirectXVideoProcessorService,
                                                 (void **)&service);
    if (SUCCEEDED(hr))
        hr = service->lpVtbl->QueryInterface(service, &IID_IUnknown, (void **)&unknown);
    if (SUCCEEDED(hr) && unknown != a->processor) {
        uninitialize(a);
        hr = initialize(a, a->requested, a->type);
    }
    if (handle)
        a->manager->lpVtbl->CloseDeviceHandle(a->manager, handle);
    if (unknown)
        unknown->lpVtbl->Release(unknown);
    if (service)
        service->lpVtbl->Release(service);
    return hr;
}

static HRESULT test_cooperative_level(IDirect3DDevice9 *device)
{
    IDirect3DDevice9Ex *ex = nullptr;
    if (FAILED(device->lpVtbl->QueryInterface(device, &IID_IDirect3DDevice9Ex, (void **)&ex)))
        return device->lpVtbl->TestCooperativeLevel(device);
    HRESULT hr = ex->lpVtbl->CheckDeviceState(ex, nullptr);
    /* S_PRESENT_OCCLUDED counts as lost when asked from the allocator. */
    if (hr == (HRESULT)0x88760868L || hr == (HRESULT)0x88760870L || hr == (HRESULT)0x88760874L ||
        hr == (HRESULT)0x08760877L)
        hr = (HRESULT)0x88760869L;
    ex->lpVtbl->Release(ex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE allocator_query_interface(IMFVideoSampleAllocator *iface, REFIID riid, void **out)
{
    Allocator *a = allocator(iface);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFVideoSampleAllocator))
        *out = &a->iface;
    else if (IsEqualGUID(riid, &IID_IMFVideoSampleAllocatorCallback))
        *out = &a->callback;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&a->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE allocator_add_ref(IMFVideoSampleAllocator *iface)
{
    return InterlockedIncrement(&allocator(iface)->refs);
}

static ULONG STDMETHODCALLTYPE allocator_release(IMFVideoSampleAllocator *iface)
{
    Allocator *a = allocator(iface);
    ULONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        uninitialize(a);
        if (a->returned)
            CloseHandle(a->returned);
        if (a->type)
            a->type->lpVtbl->Release(a->type);
        if (a->manager)
            a->manager->lpVtbl->Release(a->manager);
        if (a->notify)
            a->notify->lpVtbl->Release(a->notify);
        DeleteCriticalSection(&a->allocate_lock);
        list_free(&a->retired);
        list_free(&a->outstanding);
        list_free(&a->free);
        DeleteCriticalSection(&a->lock);
        HeapFree(GetProcessHeap(), 0, a);
        EvrWorkQueueStop();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE allocator_set_directx_manager(IMFVideoSampleAllocator *iface, IUnknown *manager)
{
    Allocator *a = allocator(iface);
    IDirect3DDeviceManager9 *device_manager = nullptr;
    HRESULT hr = S_OK;
    EnterCriticalSection(&a->lock);
    uninitialize(a);
    if (manager)
        hr = manager->lpVtbl->QueryInterface(manager, &IID_IDirect3DDeviceManager9, (void **)&device_manager);
    if (SUCCEEDED(hr)) {
        if (a->manager)
            a->manager->lpVtbl->Release(a->manager);
        a->manager = device_manager;
    }
    LeaveCriticalSection(&a->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE allocator_uninitialize(IMFVideoSampleAllocator *iface)
{
    Allocator *a = allocator(iface);
    EnterCriticalSection(&a->lock);
    uninitialize(a);
    LeaveCriticalSection(&a->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE allocator_initialize(IMFVideoSampleAllocator *iface, DWORD count, IMFMediaType *type)
{
    Allocator *a = allocator(iface);
    EnterCriticalSection(&a->lock);
    HRESULT hr = initialize(a, count, type);
    LeaveCriticalSection(&a->lock);
    return hr;
}

/*
 * without a callback an empty allocator waits a second for a sample to come back. the sample handed out holds the
 * allocator through IMFTrackedSample, with itself as the state.
 */
static HRESULT STDMETHODCALLTYPE allocator_allocate_sample(IMFVideoSampleAllocator *iface, IMFSample **out)
{
    Allocator *a = allocator(iface);
    IMFSample *sample = nullptr, *result = nullptr;
    IMFTrackedSample *tracked = nullptr;
    IDirect3DDevice9 *device = nullptr;
    HANDLE handle = nullptr;
    HRESULT hr;
    EnterCriticalSection(&a->allocate_lock);
    EnterCriticalSection(&a->lock);
    if (!out) {
        hr = E_POINTER;
        goto done;
    }
    if (!a->initialized) {
        hr = MF_E_NOT_INITIALIZED_;
        goto done;
    }
    hr = check_video_device(a);
    if (FAILED(hr))
        goto done;
    sample = list_take_head(&a->free);
    if (!sample) {
        if (!a->notify) {
            a->waiting = TRUE;
            LeaveCriticalSection(&a->lock);
            WaitForSingleObject(a->returned, 1000);
            EnterCriticalSection(&a->lock);
            a->waiting = FALSE;
            ResetEvent(a->returned);
            sample = list_take_head(&a->free);
            if (!sample && a->manager) {
                hr = a->manager->lpVtbl->OpenDeviceHandle(a->manager, &handle);
                if (SUCCEEDED(hr))
                    hr = a->manager->lpVtbl->LockDevice(a->manager, handle, &device, FALSE);
                if (SUCCEEDED(hr) && FAILED(test_cooperative_level(device))) {
                    uninitialize(a);
                    hr = initialize(a, a->requested, a->type);
                }
                if (FAILED(hr))
                    goto done;
            }
        }
        if (!sample) {
            hr = MF_E_SAMPLEALLOCATOR_EMPTY_;
            goto done;
        }
    }
    hr = sample->lpVtbl->QueryInterface(sample, &IID_IMFSample, (void **)&result);
    if (FAILED(hr))
        goto done;
    if (!list_add(&a->outstanding, result)) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    hr = sample->lpVtbl->QueryInterface(sample, &IID_IMFTrackedSample, (void **)&tracked);
    if (SUCCEEDED(hr)) {
        hr = tracked->lpVtbl->SetAllocator(tracked, &a->on_delete, (IUnknown *)result);
        if (FAILED(hr)) {
            a->outstanding.count--;
        } else {
            a->free_count--;
            sample->lpVtbl->AddRef(sample);
            *out = result;
        }
    }
done:
    if (handle) {
        a->manager->lpVtbl->UnlockDevice(a->manager, handle, FALSE);
        a->manager->lpVtbl->CloseDeviceHandle(a->manager, handle);
    }
    LeaveCriticalSection(&a->lock);
    LeaveCriticalSection(&a->allocate_lock);
    if (device)
        device->lpVtbl->Release(device);
    if (sample)
        sample->lpVtbl->Release(sample);
    if (tracked)
        tracked->lpVtbl->Release(tracked);
    if (result)
        result->lpVtbl->Release(result);
    return hr;
}

static const IMFVideoSampleAllocatorVtbl ALLOCATOR_VTBL = {
    allocator_query_interface,     allocator_add_ref,      allocator_release,         allocator_set_directx_manager,
    allocator_uninitialize,        allocator_initialize,   allocator_allocate_sample,
};

static Allocator *of_callback(IMFVideoSampleAllocatorCallback *iface)
{
    return CONTAINING_RECORD(iface, Allocator, callback);
}

static HRESULT STDMETHODCALLTYPE callback_query_interface(IMFVideoSampleAllocatorCallback *iface, REFIID riid,
                                                          void **out)
{
    return allocator_query_interface(&of_callback(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE callback_add_ref(IMFVideoSampleAllocatorCallback *iface)
{
    return allocator_add_ref(&of_callback(iface)->iface);
}

static ULONG STDMETHODCALLTYPE callback_release(IMFVideoSampleAllocatorCallback *iface)
{
    return allocator_release(&of_callback(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE callback_set_callback(IMFVideoSampleAllocatorCallback *iface,
                                                       IMFVideoSampleAllocatorNotify *notify)
{
    Allocator *a = of_callback(iface);
    EnterCriticalSection(&a->lock);
    if (notify)
        notify->lpVtbl->AddRef(notify);
    if (a->notify)
        a->notify->lpVtbl->Release(a->notify);
    a->notify = notify;
    LeaveCriticalSection(&a->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE callback_get_free_sample_count(IMFVideoSampleAllocatorCallback *iface, LONG *count)
{
    Allocator *a = of_callback(iface);
    EnterCriticalSection(&a->lock);
    if (count)
        *count = (LONG)a->free_count;
    LeaveCriticalSection(&a->lock);
    return S_OK;
}

static const IMFVideoSampleAllocatorCallbackVtbl CALLBACK_VTBL = {
    callback_query_interface, callback_add_ref, callback_release, callback_set_callback, callback_get_free_sample_count,
};

static Allocator *of_on_delete(IMFAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, Allocator, on_delete);
}

static HRESULT STDMETHODCALLTYPE on_delete_query_interface(IMFAsyncCallback *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IMFAsyncCallback_) || IsEqualGUID(riid, &IID_IUnknown)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE on_delete_add_ref(IMFAsyncCallback *iface)
{
    return allocator_add_ref(&of_on_delete(iface)->iface);
}

static ULONG STDMETHODCALLTYPE on_delete_release(IMFAsyncCallback *iface)
{
    return allocator_release(&of_on_delete(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE on_delete_get_parameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    return E_NOTIMPL;
}

/* a sample came back: dropped when the allocator forgot it, else cleared and put back on the free list. */
static HRESULT STDMETHODCALLTYPE on_delete_invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    Allocator *a = of_on_delete(iface);
    IUnknown *state = nullptr, *unknown = nullptr;
    IMFSample *sample = nullptr;
    IMFMediaBuffer *buffer = nullptr;
    BOOL found = FALSE;
    HRESULT hr = E_POINTER;
    EnterCriticalSection(&a->lock);
    if (!result)
        goto done;
    hr = result->lpVtbl->GetState(result, &state);
    if (FAILED(hr))
        goto done;
    hr = state->lpVtbl->QueryInterface(state, &IID_IUnknown, (void **)&unknown);
    if (SUCCEEDED(hr))
        hr = list_remove(&a->retired, unknown, &found);
    if (unknown)
        unknown->lpVtbl->Release(unknown);
    unknown = nullptr;
    if (FAILED(hr) || found)
        goto done;
    hr = state->lpVtbl->QueryInterface(state, &IID_IMFSample, (void **)&sample);
    if (FAILED(hr))
        goto done;
    if (SUCCEEDED(sample->lpVtbl->GetBufferByIndex(sample, 0, &buffer)))
        buffer->lpVtbl->SetCurrentLength(buffer, 0);
    IMFDesiredSample *desired = nullptr;
    if (SUCCEEDED(sample->lpVtbl->QueryInterface(sample, &IID_IMFDesiredSample, (void **)&desired))) {
        desired->lpVtbl->Clear(desired);
        desired->lpVtbl->Release(desired);
    }
    hr = state->lpVtbl->QueryInterface(state, &IID_IUnknown, (void **)&unknown);
    if (SUCCEEDED(hr))
        hr = list_remove(&a->outstanding, unknown, &found);
    if (unknown)
        unknown->lpVtbl->Release(unknown);
    if (FAILED(hr))
        goto done;
    if (!list_add(&a->free, sample)) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    sample->lpVtbl->AddRef(sample);
    a->free_count++;
    if (a->notify) {
        a->notify->lpVtbl->NotifyRelease(a->notify);
    } else if (a->waiting) {
        a->waiting = FALSE;
        SetEvent(a->returned);
    }
done:
    LeaveCriticalSection(&a->lock);
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    if (sample)
        sample->lpVtbl->Release(sample);
    if (state)
        state->lpVtbl->Release(state);
    return S_OK;
}

static const IMFAsyncCallbackVtbl ON_DELETE_VTBL = {
    on_delete_query_interface, on_delete_add_ref, on_delete_release, on_delete_get_parameters, on_delete_invoke,
};

HRESULT WINAPI MFCreateVideoSampleAllocator(REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    Allocator *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->iface.lpVtbl = &ALLOCATOR_VTBL;
    a->callback.lpVtbl = &CALLBACK_VTBL;
    a->on_delete.lpVtbl = &ON_DELETE_VTBL;
    a->refs = 1;
    EvrWorkQueueStart();
    InitializeCriticalSection(&a->lock);
    InitializeCriticalSection(&a->allocate_lock);
    a->returned = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr = a->returned ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr))
        hr = allocator_query_interface(&a->iface, riid, out);
    allocator_release(&a->iface);
    return hr;
}
