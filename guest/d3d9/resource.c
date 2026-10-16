/*
 * what every direct3d 9 resource does alike: the device it holds a
 * reference on, private data, priorities, and the memory programs lock.
 */
#include "d3d9_private.h"

#define D3DSPD_IUNKNOWN 0x00000001

void D9ResourceInit(D9Resource *r, D9Device *device, D3DRESOURCETYPE type, D3DPOOL pool, DWORD usage,
                    D3DFORMAT format, void (*destroy)(D9Resource *r))
{
    r->refs = 1;
    r->destroy = destroy;
    r->device = device;
    r->type = type;
    r->pool = pool;
    r->usage = usage;
    r->d3d_format = format;
    r->format = D9GetFormat(format);
    if (device)
        device->iface.lpVtbl->AddRef(&device->iface);
}

void D9ResourceClear(D9Resource *r) { MimePrivateClear(&r->data); }

ULONG D9ResourceAddRef(D9Resource *r)
{
    ULONG refs = InterlockedIncrement(&r->refs);
    if (refs == 1 && r->device)
        r->device->iface.lpVtbl->AddRef(&r->device->iface);
    return refs;
}

ULONG D9ResourceRelease(D9Resource *r)
{
    D9Device *device = r->device;
    ULONG refs = InterlockedDecrement(&r->refs);
    if (!refs) {
        if (device)
            D9Lock(device);
        BOOL gone = !r->binds && !r->implicit;
        if (gone)
            r->destroy(r);
        if (device) {
            D9Unlock(device);
            device->iface.lpVtbl->Release(&device->iface);
        }
    }
    return refs;
}

void D9Bind(D9Resource *r)
{
    if (r)
        r->binds++;
}

void D9Unbind(D9Resource *r)
{
    if (r && !--r->binds && !r->refs && !r->implicit)
        r->destroy(r);
}

HRESULT D9ResourceGetDevice(D9Resource *r, IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    *device = (IDirect3DDevice9 *)&r->device->iface;
    r->device->iface.lpVtbl->AddRef(&r->device->iface);
    return D3D_OK;
}

HRESULT D9ResourceSetPrivateData(D9Resource *r, REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    if (!guid)
        return D3DERR_INVALIDCALL;
    if (flags & D3DSPD_IUNKNOWN) {
        if (size != sizeof(IUnknown *) || !data)
            return D3DERR_INVALIDCALL;
        return MimePrivateSetInterface(&r->data, guid, *(IUnknown *const *)data);
    }
    if (size && !data)
        return D3DERR_INVALIDCALL;
    return FAILED(MimePrivateSet(&r->data, guid, size, data)) ? D3DERR_INVALIDCALL : D3D_OK;
}

HRESULT D9ResourceGetPrivateData(D9Resource *r, REFGUID guid, void *data, DWORD *size)
{
    if (!guid || !size)
        return D3DERR_INVALIDCALL;
    UINT n = *size;
    HRESULT hr = MimePrivateGet(&r->data, guid, &n, data);
    *size = n;
    if (hr == (HRESULT)0x887A0002)
        return D3DERR_NOTFOUND;
    if (hr == (HRESULT)0x887A0003)
        return D3DERR_MOREDATA;
    return FAILED(hr) ? D3DERR_INVALIDCALL : D3D_OK;
}

HRESULT D9ResourceFreePrivateData(D9Resource *r, REFGUID guid)
{
    UINT n = 0;
    if (!guid)
        return D3DERR_INVALIDCALL;
    if (MimePrivateGet(&r->data, guid, &n, NULL) == (HRESULT)0x887A0002)
        return D3DERR_NOTFOUND;
    MimePrivateSet(&r->data, guid, 0, NULL);
    return D3D_OK;
}

DWORD D9ResourceSetPriority(D9Resource *r, DWORD priority)
{
    if (r->pool != D3DPOOL_MANAGED)
        return 0;
    DWORD old = r->priority;
    r->priority = priority;
    return old;
}

/* ------------------------------------------------------------------ memory */

HRESULT D9AllocMemory(D9Memory *memory, const D9Format *f, UINT width, UINT height, UINT depth)
{
    memory->pitch = D9FormatPitch(f, width);
    memory->slice = memory->pitch * D9FormatRows(f, height);
    memory->size = memory->slice * D9_MAX(depth, 1);
    memory->bytes = VirtualAlloc(NULL, D9_MAX(memory->size, 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return memory->bytes ? D3D_OK : E_OUTOFMEMORY;
}

void D9FreeMemory(D9Memory *memory)
{
    if (memory->bytes)
        VirtualFree(memory->bytes, 0, MEM_RELEASE);
    memory->bytes = NULL;
}

/* a lock's rectangle within a level, whole blocks of compressed formats. */
BOOL D9LockRect(const D9Format *f, UINT width, UINT height, const RECT *rect, RECT *out)
{
    if (!rect) {
        *out = (RECT){0, 0, (LONG)width, (LONG)height};
        return TRUE;
    }
    if (rect->left < 0 || rect->top < 0 || rect->left >= rect->right || rect->top >= rect->bottom ||
        (UINT)rect->right > width || (UINT)rect->bottom > height)
        return FALSE;
    if (f->block > 1 && ((rect->left | rect->top) % f->block ||
                         ((UINT)rect->right != width && rect->right % f->block) ||
                         ((UINT)rect->bottom != height && rect->bottom % f->block)))
        return FALSE;
    *out = *rect;
    return TRUE;
}

BYTE *D9MemoryAt(const D9Memory *memory, const D9Format *f, UINT x, UINT y, UINT z)
{
    return memory->bytes + (SIZE_T)z * memory->slice + (SIZE_T)(y / f->block) * memory->pitch +
           (SIZE_T)(x / f->block) * f->bytes;
}
