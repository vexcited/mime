/* clones share global storage and retain independent seek positions. */
#include "ole32_private.h"
#include <mime/win32/objidl.h>

typedef struct {
    SRWLOCK Lock;
    LONG Refs;
    HGLOBAL Memory;
    SIZE_T Size;
    BOOL Delete;
} STREAM_STORAGE;
typedef struct {
    IStream Interface;
    LONG Refs;
    STREAM_STORAGE *Storage;
    ULONGLONG Position;
} MEMORY_STREAM;
static const IStreamVtbl stream_vtable;

static ULONG WINAPI stream_addref(IStream *self)
{
    return InterlockedIncrement(&((MEMORY_STREAM *)self)->Refs);
}
static ULONG WINAPI stream_release(IStream *self)
{
    MEMORY_STREAM *stream = (MEMORY_STREAM *)self;
    LONG refs = InterlockedDecrement(&stream->Refs);
    if (!refs) {
        STREAM_STORAGE *storage = stream->Storage;
        if (!InterlockedDecrement(&storage->Refs)) {
            if (storage->Delete)
                GlobalFree(storage->Memory);
            OleFree(storage);
        }
        OleFree(stream);
    }
    return refs;
}
static HRESULT WINAPI stream_query(IStream *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!iid)
        return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IStream) && !IsEqualIID(iid, &IID_ISequentialStream))
        return E_NOINTERFACE;
    *result = self;
    stream_addref(self);
    return S_OK;
}
static HRESULT resize_storage(STREAM_STORAGE *storage, ULONGLONG size)
{
    if (size > (SIZE_T)-1)
        return STG_E_MEDIUMFULL;
    HGLOBAL memory = GlobalReAlloc(storage->Memory, size ? (SIZE_T)size : 1, GMEM_MOVEABLE | GMEM_ZEROINIT);
    if (!memory)
        return E_OUTOFMEMORY;
    storage->Memory = memory;
    if (size > storage->Size) {
        BYTE *data = GlobalLock(memory);
        if (!data)
            return STG_E_INVALIDPOINTER;
        memset(data + storage->Size, 0, (SIZE_T)size - storage->Size);
        GlobalUnlock(memory);
    }
    storage->Size = (SIZE_T)size;
    return S_OK;
}
static HRESULT WINAPI stream_read(IStream *self, void *buffer, ULONG size, ULONG *read)
{
    if (read)
        *read = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    MEMORY_STREAM *stream = (MEMORY_STREAM *)self;
    STREAM_STORAGE *storage = stream->Storage;
    AcquireSRWLockExclusive(&storage->Lock);
    ULONG count = stream->Position >= storage->Size ? 0 :
        (ULONG)((storage->Size - stream->Position) < size ? storage->Size - stream->Position : size);
    if (count) {
        BYTE *data = GlobalLock(storage->Memory);
        if (!data) {
            ReleaseSRWLockExclusive(&storage->Lock);
            return STG_E_INVALIDPOINTER;
        }
        memcpy(buffer, data + (SIZE_T)stream->Position, count);
        GlobalUnlock(storage->Memory);
        stream->Position += count;
    }
    ReleaseSRWLockExclusive(&storage->Lock);
    if (read)
        *read = count;
    return count == size ? S_OK : S_FALSE;
}
static HRESULT WINAPI stream_write(IStream *self, const void *buffer, ULONG size, ULONG *written)
{
    if (written)
        *written = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    MEMORY_STREAM *stream = (MEMORY_STREAM *)self;
    STREAM_STORAGE *storage = stream->Storage;
    AcquireSRWLockExclusive(&storage->Lock);
    HRESULT hr = S_OK;
    ULONGLONG end = stream->Position + size;
    if (end < stream->Position)
        hr = STG_E_MEDIUMFULL;
    else if (size && end > storage->Size)
        hr = resize_storage(storage, end);
    if (SUCCEEDED(hr) && size) {
        BYTE *data = GlobalLock(storage->Memory);
        if (!data)
            hr = STG_E_INVALIDPOINTER;
        else {
            memcpy(data + (SIZE_T)stream->Position, buffer, size);
            GlobalUnlock(storage->Memory);
            stream->Position = end;
            if (written)
                *written = size;
        }
    }
    ReleaseSRWLockExclusive(&storage->Lock);
    return hr;
}
static HRESULT WINAPI stream_seek(IStream *self, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    MEMORY_STREAM *stream = (MEMORY_STREAM *)self;
    AcquireSRWLockExclusive(&stream->Storage->Lock);
    ULONGLONG base = origin == 0 ? 0 : origin == 1 ? stream->Position : stream->Storage->Size;
    ULONGLONG offset = move.QuadPart < 0 ? 0ull - (ULONGLONG)move.QuadPart : (ULONGLONG)move.QuadPart;
    HRESULT hr = S_OK;
    if (origin > 2 || (move.QuadPart < 0 ? offset > base : base + offset < base))
        hr = STG_E_INVALIDFUNCTION;
    else {
        stream->Position = move.QuadPart < 0 ? base - offset : base + offset;
        if (position)
            position->QuadPart = stream->Position;
    }
    ReleaseSRWLockExclusive(&stream->Storage->Lock);
    return hr;
}
static HRESULT WINAPI stream_size(IStream *self, ULARGE_INTEGER size)
{
    STREAM_STORAGE *storage = ((MEMORY_STREAM *)self)->Storage;
    AcquireSRWLockExclusive(&storage->Lock);
    HRESULT hr = resize_storage(storage, size.QuadPart);
    ReleaseSRWLockExclusive(&storage->Lock);
    return hr;
}
static HRESULT WINAPI stream_copy(IStream *self, IStream *other, ULARGE_INTEGER size,
    ULARGE_INTEGER *read, ULARGE_INTEGER *written)
{
    if (read) read->QuadPart = 0;
    if (written) written->QuadPart = 0;
    if (!other)
        return STG_E_INVALIDPOINTER;
    BYTE buffer[4096];
    ULONGLONG remaining = size.QuadPart;
    while (remaining) {
        ULONG count = remaining < sizeof(buffer) ? (ULONG)remaining : sizeof(buffer), got = 0, put = 0;
        HRESULT hr = stream_read(self, buffer, count, &got);
        if (read) read->QuadPart += got;
        if (FAILED(hr)) return hr;
        hr = other->lpVtbl->Write(other, buffer, got, &put);
        if (written) written->QuadPart += put;
        if (FAILED(hr)) return hr;
        if (put != got) return STG_E_MEDIUMFULL;
        if (got != count) return S_FALSE;
        remaining -= got;
    }
    return S_OK;
}
static HRESULT WINAPI stream_commit(IStream *self, DWORD flags) { return S_OK; }
static HRESULT WINAPI stream_revert(IStream *self) { return S_OK; }
static HRESULT WINAPI stream_region(IStream *self, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}
static HRESULT WINAPI stream_stat(IStream *self, STATSTG *stat, DWORD flags)
{
    if (!stat) return STG_E_INVALIDPOINTER;
    if (flags > 1) return E_INVALIDARG;
    memset(stat, 0, sizeof(*stat));
    MEMORY_STREAM *stream = (MEMORY_STREAM *)self;
    AcquireSRWLockShared(&stream->Storage->Lock);
    stat->cbSize.QuadPart = stream->Storage->Size;
    ReleaseSRWLockShared(&stream->Storage->Lock);
    stat->type = 2;
    stat->grfMode = 2;
    return S_OK;
}
static HRESULT WINAPI stream_clone(IStream *self, IStream **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    MEMORY_STREAM *stream = (MEMORY_STREAM *)self, *clone = OleAllocZero(sizeof(*clone));
    if (!clone) return E_OUTOFMEMORY;
    clone->Interface.lpVtbl = &stream_vtable;
    clone->Refs = 1;
    clone->Storage = stream->Storage;
    AcquireSRWLockShared(&stream->Storage->Lock);
    clone->Position = stream->Position;
    InterlockedIncrement(&stream->Storage->Refs);
    ReleaseSRWLockShared(&stream->Storage->Lock);
    *result = &clone->Interface;
    return S_OK;
}
static const IStreamVtbl stream_vtable = {
    stream_query, stream_addref, stream_release, stream_read, stream_write, stream_seek,
    stream_size, stream_copy, stream_commit, stream_revert, stream_region, stream_region, stream_stat, stream_clone
};
WINOLEAPI HRESULT WINAPI CreateStreamOnHGlobal(HGLOBAL memory, BOOL delete, IStream **result)
{
    if (!result) return E_INVALIDARG;
    *result = nullptr;
    if (memory && GlobalFlags(memory) == GMEM_INVALID_HANDLE) return E_INVALIDARG;
    MEMORY_STREAM *stream = OleAllocZero(sizeof(*stream));
    STREAM_STORAGE *storage = OleAllocZero(sizeof(*storage));
    if (!stream || !storage) {
        OleFree(stream); OleFree(storage);
        return E_OUTOFMEMORY;
    }
    storage->Size = memory ? GlobalSize(memory) : 0;
    storage->Memory = memory ? memory : GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
    if (!storage->Memory) {
        OleFree(stream); OleFree(storage);
        return E_OUTOFMEMORY;
    }
    storage->Refs = 1;
    storage->Delete = delete;
    stream->Interface.lpVtbl = &stream_vtable;
    stream->Refs = 1;
    stream->Storage = storage;
    *result = &stream->Interface;
    return S_OK;
}
WINOLEAPI HRESULT WINAPI GetHGlobalFromStream(IStream *stream, HGLOBAL *memory)
{
    if (!memory) return E_INVALIDARG;
    *memory = nullptr;
    if (!stream || stream->lpVtbl != &stream_vtable) return E_INVALIDARG;
    STREAM_STORAGE *storage = ((MEMORY_STREAM *)stream)->Storage;
    AcquireSRWLockShared(&storage->Lock);
    *memory = storage->Memory;
    ReleaseSRWLockShared(&storage->Lock);
    return S_OK;
}
