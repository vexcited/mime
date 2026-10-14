/* byte arrays over a global memory block, the backing store of compound files in memory. */
#include "ole32_private.h"

typedef struct {
    ILockBytes Interface;
    LONG Refs;
    SRWLOCK Lock;
    HGLOBAL Memory;
    SIZE_T Size;
    BOOL Delete;
} GLOBAL_BYTES;

static const ILockBytesVtbl bytes_vtable;

static HRESULT WINAPI bytes_query(ILockBytes *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!iid)
        return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ILockBytes))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI bytes_addref(ILockBytes *self)
{
    return InterlockedIncrement(&((GLOBAL_BYTES *)self)->Refs);
}

static ULONG WINAPI bytes_release(ILockBytes *self)
{
    GLOBAL_BYTES *bytes = (GLOBAL_BYTES *)self;
    LONG refs = InterlockedDecrement(&bytes->Refs);
    if (!refs) {
        if (bytes->Delete)
            GlobalFree(bytes->Memory);
        OleFree(bytes);
    }
    return refs;
}

static HRESULT resize(GLOBAL_BYTES *bytes, ULONGLONG size)
{
    if (size > (SIZE_T)-1)
        return STG_E_MEDIUMFULL;
    if (size == bytes->Size)
        return S_OK;
    HGLOBAL memory = GlobalReAlloc(bytes->Memory, size ? (SIZE_T)size : 1, GMEM_MOVEABLE | GMEM_ZEROINIT);
    if (!memory)
        return STG_E_MEDIUMFULL;
    bytes->Memory = memory;
    if (size > bytes->Size) {
        BYTE *data = GlobalLock(memory);
        if (!data)
            return STG_E_INVALIDPOINTER;
        memset(data + bytes->Size, 0, (SIZE_T)size - bytes->Size);
        GlobalUnlock(memory);
    }
    bytes->Size = (SIZE_T)size;
    return S_OK;
}

static HRESULT WINAPI bytes_read(ILockBytes *self, ULARGE_INTEGER offset, void *buffer, ULONG size, ULONG *read)
{
    if (read)
        *read = 0;
    if (!buffer)
        return STG_E_INVALIDPOINTER;
    GLOBAL_BYTES *bytes = (GLOBAL_BYTES *)self;
    AcquireSRWLockShared(&bytes->Lock);
    ULONG count = 0;
    HRESULT hr = S_OK;
    if (offset.QuadPart < bytes->Size) {
        ULONGLONG left = bytes->Size - offset.QuadPart;
        count = left < size ? (ULONG)left : size;
        BYTE *data = GlobalLock(bytes->Memory);
        if (!data) {
            count = 0;
            hr = STG_E_READFAULT;
        } else {
            memcpy(buffer, data + (SIZE_T)offset.QuadPart, count);
            GlobalUnlock(bytes->Memory);
        }
    }
    ReleaseSRWLockShared(&bytes->Lock);
    if (read)
        *read = count;
    return hr;
}

static HRESULT WINAPI bytes_write(ILockBytes *self, ULARGE_INTEGER offset, const void *buffer, ULONG size,
                                  ULONG *written)
{
    if (written)
        *written = 0;
    if (!buffer)
        return STG_E_INVALIDPOINTER;
    if (!size)
        return S_OK;
    GLOBAL_BYTES *bytes = (GLOBAL_BYTES *)self;
    AcquireSRWLockExclusive(&bytes->Lock);
    ULONGLONG end = offset.QuadPart + size;
    HRESULT hr = end < offset.QuadPart ? STG_E_MEDIUMFULL : S_OK;
    if (SUCCEEDED(hr) && end > bytes->Size)
        hr = resize(bytes, end);
    if (SUCCEEDED(hr)) {
        BYTE *data = GlobalLock(bytes->Memory);
        if (!data)
            hr = STG_E_WRITEFAULT;
        else {
            memcpy(data + (SIZE_T)offset.QuadPart, buffer, size);
            GlobalUnlock(bytes->Memory);
            if (written)
                *written = size;
        }
    }
    ReleaseSRWLockExclusive(&bytes->Lock);
    return hr;
}

static HRESULT WINAPI bytes_flush(ILockBytes *self)
{
    return S_OK;
}

static HRESULT WINAPI bytes_set_size(ILockBytes *self, ULARGE_INTEGER size)
{
    GLOBAL_BYTES *bytes = (GLOBAL_BYTES *)self;
    AcquireSRWLockExclusive(&bytes->Lock);
    HRESULT hr = resize(bytes, size.QuadPart);
    ReleaseSRWLockExclusive(&bytes->Lock);
    return hr;
}

static HRESULT WINAPI bytes_region(ILockBytes *self, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI bytes_stat(ILockBytes *self, STATSTG *stat, DWORD flags)
{
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    GLOBAL_BYTES *bytes = (GLOBAL_BYTES *)self;
    AcquireSRWLockShared(&bytes->Lock);
    stat->cbSize.QuadPart = bytes->Size;
    ReleaseSRWLockShared(&bytes->Lock);
    stat->type = STGTY_LOCKBYTES;
    return S_OK;
}

static const ILockBytesVtbl bytes_vtable = {
    bytes_query, bytes_addref,   bytes_release, bytes_read,  bytes_write,
    bytes_flush, bytes_set_size, bytes_region,  bytes_region, bytes_stat,
};

WINOLEAPI HRESULT WINAPI CreateILockBytesOnHGlobal(HGLOBAL memory, BOOL delete_on_release, ILockBytes **result)
{
    if (!result)
        return E_INVALIDARG;
    *result = NULL;
    GLOBAL_BYTES *bytes = OleAllocZero(sizeof(*bytes));
    if (!bytes)
        return E_OUTOFMEMORY;
    bytes->Memory = memory ? memory : GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
    if (!bytes->Memory) {
        OleFree(bytes);
        return E_OUTOFMEMORY;
    }
    bytes->Size = memory ? GlobalSize(memory) : 0;
    bytes->Interface.lpVtbl = &bytes_vtable;
    bytes->Refs = 1;
    bytes->Delete = delete_on_release;
    *result = &bytes->Interface;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI GetHGlobalFromILockBytes(ILockBytes *self, HGLOBAL *memory)
{
    if (!memory)
        return E_INVALIDARG;
    *memory = NULL;
    if (!self || self->lpVtbl != &bytes_vtable)
        return E_INVALIDARG;
    GLOBAL_BYTES *bytes = (GLOBAL_BYTES *)self;
    AcquireSRWLockShared(&bytes->Lock);
    *memory = bytes->Memory;
    ReleaseSRWLockShared(&bytes->Lock);
    return S_OK;
}
