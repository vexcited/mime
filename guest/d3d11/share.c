/*
 * shared textures, keyed mutexes and fences. A texture made to be shared keeps memory of its own that other
 * devices import; the first nt handle, legacy handle or keyed mutex acquire gives it a kernel object, which carries
 * what the texture was described with and the id the host keeps the memory under, see mime/gfxshare.h. A fence is a
 * timeline semaphore that is shared the same way.
 */
#include "d3d11_private.h"

#include <mime/ntuser.h>

/* direct3d 12's resource flags, which a texture made by direct3d 11 is described to it with. */
#define RESOURCE_FLAG_ALLOW_RENDER_TARGET 0x1
#define RESOURCE_FLAG_ALLOW_DEPTH_STENCIL 0x2
#define RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS 0x4
#define RESOURCE_FLAG_DENY_SHADER_RESOURCE 0x8

/* ------------------------------------------------------------------ creation rules */

/*
 * What sharing allows. A texture made to be shared by nt handle is default usage and not multisampled, with the shared
 * or keyed mutex flag, and not a cube or clamped, all of those from the validator of d3d11.dll. The documentation of
 * the flags adds that the texture is two dimensional. A shared texture of the legacy kind has the same, with
 * multisampling refused as a guess. Mip levels and array sizes pass, as at feature level 11 they do in d3d11.dll.
 */
HRESULT D3dShareCheck(D3D11_RESOURCE_DIMENSION dimension, UINT usage, UINT misc, UINT mips, UINT samples)
{
    if (!(misc & D3D_SHARE_MISC))
        return S_OK;
    if ((misc & D3D11_RESOURCE_MISC_SHARED) && (misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
        return E_INVALIDARG;
    if ((misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) &&
        (!(misc & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)) ||
         (misc & (D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_RESOURCE_CLAMP))))
        return E_INVALIDARG;
    if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || samples > 1 || usage != D3D11_USAGE_DEFAULT)
        return E_INVALIDARG;
    return S_OK;
}

/* the capabilities of a texture bound as direct3d 11 says, in the words both runtimes read. */
static UINT caps_of(UINT bind)
{
    UINT caps = 0;
    if (bind & D3D11_BIND_RENDER_TARGET)
        caps |= MIME_SHARE_CAN_RENDER;
    if (bind & D3D11_BIND_DEPTH_STENCIL)
        caps |= MIME_SHARE_CAN_DEPTH;
    if (bind & D3D11_BIND_UNORDERED_ACCESS)
        caps |= MIME_SHARE_CAN_WRITE;
    if (bind & D3D11_BIND_SHADER_RESOURCE)
        caps |= MIME_SHARE_CAN_READ;
    return caps;
}

static LUID adapter_luid(D3dDevice *device)
{
    DXGI_ADAPTER_DESC desc = {};
    if (device->adapter)
        device->adapter->lpVtbl->GetDesc(device->adapter, &desc);
    return desc.AdapterLuid;
}

void D3dShareDescribe(D3dResource *r, const VkImageCreateInfo *info, UINT type_index, UINT64 memory_size)
{
    MimeShareBlob *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return;
    b->magic = MIME_SHARE_MAGIC;
    b->kind = MIME_SHARE_TEXTURE;
    b->caps = caps_of(r->bind) | ((r->misc & D3D11_RESOURCE_MISC_TEXTURECUBE) ? MIME_SHARE_CUBE : 0) |
              ((r->misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) ? MIME_SHARE_NT_HANDLE : 0);
    b->dimension = 2;
    b->format = r->tex.format;
    b->width = r->tex.width;
    b->height = r->tex.height;
    b->depth = r->tex.layers;
    b->mips = r->tex.mips;
    b->samples = r->tex.samples;
    b->quality = r->desc.texture2d.SampleDesc.Quality;
    b->d3d11_bind = r->bind;
    b->d3d11_misc = r->misc;
    b->d3d12_flags = ((b->caps & MIME_SHARE_CAN_RENDER) ? RESOURCE_FLAG_ALLOW_RENDER_TARGET : 0) |
                     ((b->caps & MIME_SHARE_CAN_DEPTH) ? RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : 0) |
                     ((b->caps & MIME_SHARE_CAN_WRITE) ? RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : 0) |
                     (((b->caps & MIME_SHARE_CAN_DEPTH) && !(b->caps & MIME_SHARE_CAN_READ))
                          ? RESOURCE_FLAG_DENY_SHADER_RESOURCE
                          : 0);
    b->vk_format = info->format;
    b->vk_usage = info->usage;
    b->vk_flags = info->flags;
    b->vk_layers = info->arrayLayers;
    b->type_index = type_index;
    b->memory_size = memory_size;
    b->adapter = adapter_luid(r->child.device);
    r->share_blob = b;
}

/* ------------------------------------------------------------------ the kernel object */

/* made when the texture needs it first, with the name and attributes of the call that does. */
static HRESULT ensure(D3dResource *r, BOOL legacy, LPCWSTR name, const SECURITY_ATTRIBUTES *attributes)
{
    D3dDevice *device = r->child.device;
    if (r->share.handle)
        return S_OK;
    if (!r->share_blob)
        return E_INVALIDARG;
    UINT flags = ((r->misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) ? MIME_GFXSHARE_KEYED_MUTEX : 0) |
                 (legacy ? MIME_GFXSHARE_LEGACY : 0);
    HRESULT hr = MimeShareCreate(&r->share, MIME_GFXSHARE_RESOURCE, r->share_blob, sizeof(*r->share_blob), flags,
                                 name, attributes);
    if (FAILED(hr))
        return hr;
    if (device->vk.vkMimeShareExportMemory(device->vk.device, r->tex.alloc.memory, r->share.id) != VK_SUCCESS) {
        MimeShareClose(&r->share);
        return E_FAIL;
    }
    return S_OK;
}

/*
 * A handle to the texture. The nt handle needs the texture made for it; the legacy one needs it shared without,
 * and any other texture has none, which GetSharedHandle answers with success and no handle, as d3d11.dll does.
 */
HRESULT D3dShareHandle(D3dResource *r, BOOL nt, const SECURITY_ATTRIBUTES *attributes, LPCWSTR name, HANDLE *out)
{
    if (nt) {
        if (!(r->misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
            return E_INVALIDARG;
    } else {
        if (r->misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
            return E_INVALIDARG;
        if (!(r->misc & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))) {
            *out = nullptr;
            return S_OK;
        }
    }
    AcquireSRWLockExclusive(&r->share_lock);
    BOOL created = !r->share.handle;
    HRESULT hr = ensure(r, !nt, name, attributes);
    if (SUCCEEDED(hr) && !created && name)
        hr = DXGI_ERROR_NAME_ALREADY_EXISTS;
    ReleaseSRWLockExclusive(&r->share_lock);
    if (FAILED(hr))
        return hr;
    if (!nt) {
        *out = MimeLegacyHandle(r->share.legacy);
        return r->share.legacy ? S_OK : E_INVALIDARG;
    }
    return MimeShareHandle(&r->share, 0, attributes, out);
}

void D3dShareDestroy(D3dResource *r)
{
    /* a texture that goes while it holds the keyed mutex abandons it. */
    if (r->keyed_acquired && r->share.handle)
        MimeShareAbandon(r->share.handle);
    MimeShareClose(&r->share);
    HeapFree(GetProcessHeap(), 0, r->share_blob);
    r->share_blob = nullptr;
}

/* the texture a description makes, in direct3d 11's words. */
static BOOL describe_texture(const MimeShareBlob *b, D3D11_TEXTURE2D_DESC1 *desc)
{
    if (b->magic != MIME_SHARE_MAGIC || b->kind != MIME_SHARE_TEXTURE || b->dimension != 2)
        return FALSE;
    memset(desc, 0, sizeof(*desc));
    desc->Width = b->width;
    desc->Height = b->height;
    desc->MipLevels = b->mips;
    desc->ArraySize = b->depth;
    desc->Format = (DXGI_FORMAT)b->format;
    desc->SampleDesc.Count = b->samples;
    desc->SampleDesc.Quality = b->quality;
    desc->Usage = D3D11_USAGE_DEFAULT;
    if (b->caps & MIME_SHARE_ORIGIN_D3D12) {
        /* a texture of direct3d 12 is what its flags allow: the views it can be bound for. */
        desc->BindFlags = ((b->caps & MIME_SHARE_CAN_RENDER) ? D3D11_BIND_RENDER_TARGET : 0) |
                          ((b->caps & MIME_SHARE_CAN_DEPTH) ? D3D11_BIND_DEPTH_STENCIL : 0) |
                          ((b->caps & MIME_SHARE_CAN_WRITE) ? D3D11_BIND_UNORDERED_ACCESS : 0) |
                          ((b->caps & MIME_SHARE_CAN_READ) ? D3D11_BIND_SHADER_RESOURCE : 0);
        desc->MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    } else {
        desc->BindFlags = b->d3d11_bind;
        desc->MiscFlags = b->d3d11_misc;
    }
    if (b->caps & MIME_SHARE_CUBE)
        desc->MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    desc->TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
    return TRUE;
}

/*
 * The texture a handle names, as a texture of this device over memory imported from the object. A legacy handle is
 * a number that only the object of a texture still held opens.
 */
HRESULT D3dShareOpen(D3dDevice *device, HANDLE handle, BOOL nt, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HANDLE own = nullptr;
    if (!nt) {
        if (!((ULONG_PTR)handle & MIME_LEGACY_HANDLE_BIT))
            return E_INVALIDARG;
        HRESULT hr = MimeShareOpenLegacy(MimeLegacyNumber(handle), &own);
        if (FAILED(hr))
            return E_INVALIDARG;
        handle = own;
    } else if (!handle || handle == INVALID_HANDLE_VALUE || ((ULONG_PTR)handle & MIME_LEGACY_HANDLE_BIT)) {
        return E_INVALIDARG;
    }
    D3dImport import = {};
    MimeShareBlob blob = {};
    UINT size = 0;
    HRESULT hr = MimeShareOpen(handle, MIME_GFXSHARE_RESOURCE, &import.share, &blob, sizeof(blob), &size);
    if (own)
        CloseHandle(own);
    if (FAILED(hr))
        return hr;
    D3D11_TEXTURE2D_DESC1 desc;
    LUID luid = adapter_luid(device);
    if (size != sizeof(blob) || !describe_texture(&blob, &desc) || blob.adapter.LowPart != luid.LowPart ||
        blob.adapter.HighPart != luid.HighPart) {
        MimeShareClose(&import.share);
        return E_INVALIDARG;
    }
    if (import.share.flags & MIME_GFXSHARE_KEYED_MUTEX)
        desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    import.blob = &blob;
    D3dResource *r;
    hr = D3dCreateTextureImported(device, &desc, &import, &r);
    if (FAILED(hr)) {
        MimeShareClose(&import.share);
        return hr;
    }
    hr = r->iface->lpVtbl->QueryInterface(r->iface, riid, out);
    D3dResourceRelease(r);
    return hr;
}

/* ------------------------------------------------------------------ keyed mutexes */

static D3dResource *keyed_impl(IDXGIKeyedMutex *iface) { return CONTAINING_RECORD(iface, D3dResource, keyed); }

static HRESULT STDMETHODCALLTYPE keyed_query(IDXGIKeyedMutex *iface, REFIID riid, void **out)
{
    return keyed_impl(iface)->dxgi.lpVtbl->QueryInterface(&keyed_impl(iface)->dxgi, riid, out);
}

static ULONG STDMETHODCALLTYPE keyed_add_ref(IDXGIKeyedMutex *iface)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->AddRef(&r->dxgi);
}

static ULONG STDMETHODCALLTYPE keyed_release(IDXGIKeyedMutex *iface)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->Release(&r->dxgi);
}

static HRESULT STDMETHODCALLTYPE keyed_set_private_data(IDXGIKeyedMutex *iface, REFGUID guid, UINT size,
                                                        const void *data)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->SetPrivateData(&r->dxgi, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE keyed_set_private_data_interface(IDXGIKeyedMutex *iface, REFGUID guid,
                                                                  const IUnknown *data)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->SetPrivateDataInterface(&r->dxgi, guid, data);
}

static HRESULT STDMETHODCALLTYPE keyed_get_private_data(IDXGIKeyedMutex *iface, REFGUID guid, UINT *size, void *data)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->GetPrivateData(&r->dxgi, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE keyed_get_parent(IDXGIKeyedMutex *iface, REFIID riid, void **parent)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->GetParent(&r->dxgi, riid, parent);
}

static HRESULT STDMETHODCALLTYPE keyed_get_device(IDXGIKeyedMutex *iface, REFIID riid, void **device)
{
    D3dResource *r = keyed_impl(iface);
    return r->dxgi.lpVtbl->GetDevice(&r->dxgi, riid, device);
}

void D3dContextFlush(D3dContext *c);

/*
 * Acquiring waits for the key. A mutex that is held already, here, is an invalid call, and the work the program
 * recorded is submitted first, as d3d11.dll flushes its context before it acquires.
 */
static HRESULT STDMETHODCALLTYPE keyed_acquire_sync(IDXGIKeyedMutex *iface, UINT64 key, DWORD milliseconds)
{
    D3dResource *r = keyed_impl(iface);
    D3dDevice *device = r->child.device;
    if (r->keyed_acquired)
        return DXGI_ERROR_INVALID_CALL;
    D3dContextFlush(device->immediate);
    AcquireSRWLockExclusive(&r->share_lock);
    HRESULT hr = ensure(r, !(r->misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE), nullptr, nullptr);
    ReleaseSRWLockExclusive(&r->share_lock);
    if (FAILED(hr))
        return hr;
    NTSTATUS status = MimeShareAcquire(r->share.handle, key, milliseconds);
    if (status == STATUS_SUCCESS)
        r->keyed_acquired = TRUE;
    /*
     * d3d11.dll turns the status of the kernel's acquire into an error as it turns every one, so a timeout is
     * DXGI_ERROR_WAIT_TIMEOUT and an abandoned mutex DXGI_ERROR_ACCESS_LOST, and neither marks the resource as acquired.
     */
    if (status == STATUS_TIMEOUT)
        return DXGI_ERROR_WAIT_TIMEOUT;
    if (status == STATUS_ABANDONED)
        return DXGI_ERROR_ACCESS_LOST;
    return MimeShareHResult(status);
}

/*
 * What the program drew is what the next device to acquire sees: the work is submitted and done before the key is
 * handed on, which orders the two devices' commands without a semaphore between them.
 */
static HRESULT STDMETHODCALLTYPE keyed_release_sync(IDXGIKeyedMutex *iface, UINT64 key)
{
    D3dResource *r = keyed_impl(iface);
    D3dDevice *device = r->child.device;
    if (!r->keyed_acquired)
        return DXGI_ERROR_INVALID_CALL;
    D3dContextFlush(device->immediate);
    D3dWaitSerial(device, device->next_serial - 1);
    NTSTATUS status = MimeShareRelease(r->share.handle, key);
    if (!NT_SUCCESS(status))
        return MimeShareHResult(status);
    r->keyed_acquired = FALSE;
    return S_OK;
}

static const IDXGIKeyedMutexVtbl keyed_vtbl = {
    keyed_query,
    keyed_add_ref,
    keyed_release,
    keyed_set_private_data,
    keyed_set_private_data_interface,
    keyed_get_private_data,
    keyed_get_parent,
    keyed_get_device,
    keyed_acquire_sync,
    keyed_release_sync,
};

/* the interface of a resource that was made with a keyed mutex, or opened from one that was. */
void *D3dShareKeyedMutex(D3dResource *r)
{
    if (!(r->misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
        return nullptr;
    r->keyed.lpVtbl = &keyed_vtbl;
    return &r->keyed;
}

/* ------------------------------------------------------------------ fences */

typedef struct D3dFence {
    ID3D11Fence iface;
    D3dChild child;
    D3D11_FENCE_FLAG flags;
    VkSemaphore semaphore;
    /*
     * The process's table entry that holds the semaphore, once it was shared by id or taken from another fence of the
     * process that did: the semaphore goes with the last fence that holds it. None while the fence owns its own.
     */
    void *entry;
    SRWLOCK lock;
    MimeShare share;
    /* the highest value a wait saw reached: the mirror of the host's value may be behind a moment. */
    volatile UINT64 observed;
    /* the last submission that signals or waits for it: the fence goes once the gpu is done with that. */
    volatile UINT64 last_use;
} D3dFence;

static D3dFence *fence_impl(ID3D11Fence *iface) { return CONTAINING_RECORD(iface, D3dFence, iface); }

static HRESULT STDMETHODCALLTYPE fence_query(ID3D11Fence *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11Fence)) {
        InterlockedIncrement(&fence_impl(iface)->child.refs);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE fence_add_ref(ID3D11Fence *iface)
{
    return InterlockedIncrement(&fence_impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE fence_release(ID3D11Fence *iface)
{
    D3dFence *f = fence_impl(iface);
    ULONG refs = InterlockedDecrement(&f->child.refs);
    if (!refs) {
        D3dDevice *device = f->child.device;
        /* work submitted may still wait on or signal it. */
        if (f->last_use > D3dCompleted(device)) {
            /* a wait the program asked for is submitted with the next batch, which this one is. */
            D3dSubmit(device, VK_NULL_HANDLE, nullptr, nullptr);
            D3dWaitSerial(device, device->next_serial - 1);
        }
        if (f->entry)
            device->shared.adapter->lpVtbl->ReleaseSemaphore(device->shared.adapter, device->shared.vk, f->entry);
        else
            device->vk.vkDestroySemaphore(device->vk.device, f->semaphore, nullptr);
        MimeShareClose(&f->share);
        D3dChildRelease(&f->child);
        HeapFree(GetProcessHeap(), 0, f);
    }
    return refs;
}

static void STDMETHODCALLTYPE fence_get_device(ID3D11Fence *iface, ID3D11Device **device)
{
    D3dDevice *d = fence_impl(iface)->child.device;
    *device = (ID3D11Device *)&d->iface;
    d->iface.lpVtbl->AddRef(&d->iface);
}

static HRESULT STDMETHODCALLTYPE fence_get_private_data(ID3D11Fence *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&fence_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE fence_set_private_data(ID3D11Fence *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&fence_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE fence_set_private_data_interface(ID3D11Fence *iface, REFGUID guid,
                                                                  const IUnknown *data)
{
    return MimePrivateSetInterface(&fence_impl(iface)->child.data, guid, data);
}

static void fence_observe(D3dFence *f, UINT64 value)
{
    UINT64 seen = f->observed;
    while (value > seen) {
        UINT64 was = (UINT64)InterlockedCompareExchange64((volatile LONG64 *)&f->observed, (LONG64)value, (LONG64)seen);
        if (was == seen)
            break;
        seen = was;
    }
}

static UINT64 fence_value(D3dFence *f)
{
    D3dDevice *device = f->child.device;
    UINT64 value = 0;
    device->vk.vkGetSemaphoreCounterValue(device->vk.device, f->semaphore, &value);
    return value > f->observed ? value : f->observed;
}

static UINT64 STDMETHODCALLTYPE fence_get_completed_value(ID3D11Fence *iface) { return fence_value(fence_impl(iface)); }

/* waits on the cpu, for an event or for the caller: a thread of the process' pool waits and sets the event. */
typedef struct FenceWait {
    D3dFence *fence;
    UINT64 value;
    HANDLE event;
} FenceWait;

static DWORD WINAPI fence_waiter(void *context)
{
    FenceWait *w = context;
    D3dDevice *device = w->fence->child.device;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &w->fence->semaphore;
    wait.pValues = &w->value;
    if (device->vk.vkWaitSemaphores(device->vk.device, &wait, UINT64_MAX) == VK_SUCCESS)
        fence_observe(w->fence, w->value);
    SetEvent(w->event);
    fence_release(&w->fence->iface);
    HeapFree(GetProcessHeap(), 0, w);
    return 0;
}

static HRESULT STDMETHODCALLTYPE fence_set_event_on_completion(ID3D11Fence *iface, UINT64 value, HANDLE event)
{
    D3dFence *f = fence_impl(iface);
    D3dDevice *device = f->child.device;
    if (fence_value(f) >= value) {
        if (event)
            SetEvent(event);
        return S_OK;
    }
    if (!event) {
        VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &f->semaphore;
        wait.pValues = &value;
        if (device->vk.vkWaitSemaphores(device->vk.device, &wait, UINT64_MAX) == VK_SUCCESS)
            fence_observe(f, value);
        return S_OK;
    }
    FenceWait *w = HeapAlloc(GetProcessHeap(), 0, sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    w->fence = f;
    w->value = value;
    w->event = event;
    InterlockedIncrement(&f->child.refs);
    if (!QueueUserWorkItem(fence_waiter, w, WT_EXECUTELONGFUNCTION)) {
        InterlockedDecrement(&f->child.refs);
        HeapFree(GetProcessHeap(), 0, w);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fence_create_shared_handle(ID3D11Fence *iface, const SECURITY_ATTRIBUTES *attributes,
                                                            DWORD access, LPCWSTR name, HANDLE *handle)
{
    D3dFence *f = fence_impl(iface);
    D3dDevice *device = f->child.device;
    if (!handle)
        return E_INVALIDARG;
    if (!(f->flags & D3D11_FENCE_FLAG_SHARED))
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&f->lock);
    if (!f->share.handle) {
        MimeShareBlob blob = {};
        blob.magic = MIME_SHARE_MAGIC;
        blob.kind = MIME_SHARE_FENCE;
        blob.fence_flags = f->flags;
        blob.adapter = adapter_luid(device);
        hr = MimeShareCreate(&f->share, MIME_GFXSHARE_SYNC, &blob, sizeof(blob), 0, name, attributes);
        if (SUCCEEDED(hr) &&
            device->vk.vkMimeShareExportSemaphore(device->vk.device, f->semaphore, f->share.id) != VK_SUCCESS) {
            MimeShareClose(&f->share);
            hr = E_FAIL;
        } else if (SUCCEEDED(hr)) {
            /* the other devices of the process open this fence without importing it. */
            device->shared.adapter->lpVtbl->PublishSemaphore(device->shared.adapter, device->shared.vk, f->share.id,
                                                             (void *)f->semaphore, &f->entry);
        }
    } else if (name) {
        hr = DXGI_ERROR_NAME_ALREADY_EXISTS;
    }
    ReleaseSRWLockExclusive(&f->lock);
    if (SUCCEEDED(hr))
        hr = MimeShareHandle(&f->share, 0, attributes, handle);
    return hr;
}

static const ID3D11FenceVtbl fence_vtbl = {
    fence_query,
    fence_add_ref,
    fence_release,
    fence_get_device,
    fence_get_private_data,
    fence_set_private_data,
    fence_set_private_data_interface,
    fence_create_shared_handle,
    fence_get_completed_value,
    fence_set_event_on_completion,
};

static D3dFence *alloc_fence(D3dDevice *device, D3D11_FENCE_FLAG flags)
{
    D3dFence *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
        return nullptr;
    f->iface.lpVtbl = &fence_vtbl;
    f->flags = flags;
    InitializeSRWLock(&f->lock);
    D3dChildInit(&f->child, device);
    return f;
}

/* a fence with a semaphore of its own, exportable when it is made to be shared. */
static HRESULT create_fence(D3dDevice *device, UINT64 value, D3D11_FENCE_FLAG flags, D3dFence **out)
{
    D3dFence *f = alloc_fence(device, flags);
    if (!f)
        return E_OUTOFMEMORY;
    VkExportSemaphoreCreateInfo export = {VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr,
                                          VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = value;
    if (flags & D3D11_FENCE_FLAG_SHARED)
        type.pNext = &export;
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (device->vk.vkCreateSemaphore(device->vk.device, &info, nullptr, &f->semaphore) != VK_SUCCESS) {
        D3dChildRelease(&f->child);
        HeapFree(GetProcessHeap(), 0, f);
        return E_OUTOFMEMORY;
    }
    *out = f;
    return S_OK;
}

/*
 * The flags d3d11.dll accepts, in its order of checks: only the four flags, a fence that is not marked
 * non-monitored needs monitored fences of the adapter, which this one has, and then a non-monitored one needs
 * the adapter to have them and, as the adapter has both, the cross adapter flag; the cross adapter flag needs
 * the shared one. With no place for the fence, the arguments are checked and S_FALSE answers.
 */
HRESULT D3dCreateFence(D3dDevice *device, UINT64 value, D3D11_FENCE_FLAG flags, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (flags & ~0xFu)
        return E_INVALIDARG;
    if ((flags & D3D11_FENCE_FLAG_NON_MONITORED) && !(flags & D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER))
        return E_INVALIDARG;
    if ((flags & D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER) && !(flags & D3D11_FENCE_FLAG_SHARED))
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;
    D3dFence *f;
    HRESULT hr = create_fence(device, value, flags, &f);
    if (FAILED(hr))
        return hr;
    hr = f->iface.lpVtbl->QueryInterface(&f->iface, riid, out);
    f->iface.lpVtbl->Release(&f->iface);
    return hr;
}

HRESULT D3dOpenSharedFence(D3dDevice *device, HANDLE handle, REFIID riid, void **out)
{
    *out = nullptr;
    MimeShareBlob blob = {};
    MimeShare own;
    UINT size = 0;
    HRESULT hr = MimeShareOpen(handle, MIME_GFXSHARE_SYNC, &own, &blob, sizeof(blob), &size);
    if (FAILED(hr))
        return hr;
    if (size != sizeof(blob) || blob.magic != MIME_SHARE_MAGIC || blob.kind != MIME_SHARE_FENCE) {
        MimeShareClose(&own);
        return E_INVALIDARG;
    }
    D3D11_FENCE_FLAG flags = (D3D11_FENCE_FLAG)(blob.fence_flags | D3D11_FENCE_FLAG_SHARED);
    D3dFence *f = alloc_fence(device, flags);
    if (!f) {
        MimeShareClose(&own);
        return E_OUTOFMEMORY;
    }
    void *semaphore = nullptr;
    if (device->shared.adapter->lpVtbl->OpenSemaphore(device->shared.adapter, device->shared.vk, own.id, &semaphore,
                                                      &f->entry) == S_OK) {
        /* a fence of this process made it: the devices share its semaphore. */
        f->semaphore = (VkSemaphore)semaphore;
    } else {
        /* another process's: a semaphore here takes the payload, which nothing has to export again. */
        VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        if (device->vk.vkCreateSemaphore(device->vk.device, &info, nullptr, &f->semaphore) != VK_SUCCESS ||
            device->vk.vkMimeShareImportSemaphore(device->vk.device, f->semaphore, own.id) != VK_SUCCESS) {
            if (f->semaphore)
                device->vk.vkDestroySemaphore(device->vk.device, f->semaphore, nullptr);
            f->semaphore = VK_NULL_HANDLE;
            MimeShareClose(&own);
            D3dChildRelease(&f->child);
            HeapFree(GetProcessHeap(), 0, f);
            return E_FAIL;
        }
    }
    f->share = own;
    hr = f->iface.lpVtbl->QueryInterface(&f->iface, riid, out);
    f->iface.lpVtbl->Release(&f->iface);
    return hr;
}

/* ID3D11DeviceContext4::Signal: what the context recorded runs, then the fence takes the value. */
HRESULT D3dFenceSignal(D3dContext *c, ID3D11Fence *iface, UINT64 value)
{
    if (!iface)
        return E_INVALIDARG;
    D3dFence *f = fence_impl(iface);
    if (f->child.device != c->child.device || c->type != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    /* a timeline only goes up: a value at or below the present one signals nothing. */
    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = f->semaphore;
    signal.value = value;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    BOOL raises = value > fence_value(f);
    UINT64 serial = D3dContextSubmit(c, nullptr, raises ? &signal : nullptr);
    if (serial > f->last_use)
        f->last_use = serial;
    return S_OK;
}

/* ID3D11DeviceContext4::Wait: what the context records after this waits for the value. */
HRESULT D3dFenceWait(D3dContext *c, ID3D11Fence *iface, UINT64 value)
{
    if (!iface)
        return E_INVALIDARG;
    D3dFence *f = fence_impl(iface);
    D3dDevice *device = c->child.device;
    if (f->child.device != device || c->type != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    D3dContextFlush(c);
    D3dQueueWait(device, f->semaphore, value);
    /* the next submission carries the wait, or an earlier one if the waits overflowed. */
    if (device->next_serial > f->last_use)
        f->last_use = device->next_serial;
    return S_OK;
}
