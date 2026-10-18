/* memory buffers, aligned ones and wrappers over a range of another buffer. */
#include "mfplat_private.h"

static DWORD g_min_alignment;

typedef struct {
    IMFMediaBuffer iface;
    LONG refs;
    CRITICAL_SECTION lock;
    BYTE *allocation;
    BYTE *data;
    DWORD max_length;
    DWORD current_length;
} MemoryBuffer;

typedef struct {
    IMFMediaBuffer iface;
    LONG refs;
    IMFMediaBuffer *inner;
    DWORD offset;
    DWORD max_length;
    DWORD current_length;
} WrapperBuffer;

/* the smallest power of two not below value, 0 for 0. */
static DWORD round_power_of_two(DWORD value)
{
    DWORD v = value - 1;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

HRESULT WINAPI MFSetMinimumMemoryAlignment(DWORD alignment)
{
    g_min_alignment = alignment ? round_power_of_two(alignment) : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_query_interface(IMFMediaBuffer *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaBuffer)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ memory buffers */

static MemoryBuffer *memory_buffer(IMFMediaBuffer *iface)
{
    return CONTAINING_RECORD(iface, MemoryBuffer, iface);
}

static ULONG STDMETHODCALLTYPE memory_add_ref(IMFMediaBuffer *iface)
{
    return InterlockedIncrement(&memory_buffer(iface)->refs);
}

static ULONG STDMETHODCALLTYPE memory_release(IMFMediaBuffer *iface)
{
    MemoryBuffer *buffer = memory_buffer(iface);
    ULONG refs = InterlockedDecrement(&buffer->refs);
    if (!refs) {
        DeleteCriticalSection(&buffer->lock);
        MfFree(buffer->allocation);
        MfFree(buffer);
    }
    return refs;
}

/* locks do not nest or pin anything, the memory stays where it is. */
static HRESULT STDMETHODCALLTYPE memory_lock(IMFMediaBuffer *iface, BYTE **data, DWORD *max_length,
                                             DWORD *current_length)
{
    MemoryBuffer *buffer = memory_buffer(iface);
    if (!data) {
        if (max_length)
            *max_length = 0;
        if (current_length)
            *current_length = 0;
        return E_POINTER;
    }
    EnterCriticalSection(&buffer->lock);
    *data = buffer->data;
    if (max_length)
        *max_length = buffer->max_length;
    if (current_length)
        *current_length = buffer->current_length;
    LeaveCriticalSection(&buffer->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE memory_unlock(IMFMediaBuffer *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE memory_get_current_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = memory_buffer(iface)->current_length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE memory_set_current_length(IMFMediaBuffer *iface, DWORD length)
{
    MemoryBuffer *buffer = memory_buffer(iface);
    if (length > buffer->max_length)
        return E_INVALIDARG;
    buffer->current_length = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE memory_get_max_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = memory_buffer(iface)->max_length;
    return S_OK;
}

static const IMFMediaBufferVtbl MEMORY_BUFFER_VTBL = {
    buffer_query_interface,    memory_add_ref,        memory_release,         memory_lock, memory_unlock,
    memory_get_current_length, memory_set_current_length, memory_get_max_length,
};

/*
 * the data aligned to the larger of the requested alignment and the
 * minimum one, whose setting also rounds the length up to it.
 */
HRESULT MfCreateMemoryBuffer(DWORD max_length, DWORD alignment, IMFMediaBuffer **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (g_min_alignment > alignment)
        alignment = g_min_alignment;
    DWORD mask = alignment ? alignment - 1 : 0;
    if (g_min_alignment)
        max_length = (max_length + mask) & ~mask;
    if (max_length + mask < mask)
        return E_FAIL;
    MemoryBuffer *buffer = MfAlloc(sizeof(*buffer));
    BYTE *allocation = MfAllocZero((SIZE_T)max_length + mask);
    if (!buffer || !allocation) {
        MfFree(buffer);
        MfFree(allocation);
        return E_OUTOFMEMORY;
    }
    buffer->iface.lpVtbl = &MEMORY_BUFFER_VTBL;
    buffer->refs = 1;
    InitializeCriticalSection(&buffer->lock);
    buffer->allocation = allocation;
    buffer->data = (BYTE *)(((ULONG_PTR)allocation + mask) & ~(ULONG_PTR)mask);
    buffer->max_length = max_length;
    buffer->current_length = 0;
    *out = &buffer->iface;
    return S_OK;
}

HRESULT WINAPI MFCreateMemoryBuffer(DWORD max_length, IMFMediaBuffer **buffer)
{
    return MfCreateMemoryBuffer(max_length, 1, buffer);
}

/* the alignment is a mask as MF_16_BYTE_ALIGNMENT, any value rounds up to a power of two. */
HRESULT WINAPI MFCreateAlignedMemoryBuffer(DWORD max_length, DWORD alignment, IMFMediaBuffer **buffer)
{
    return MfCreateMemoryBuffer(max_length, round_power_of_two(alignment), buffer);
}

/* ------------------------------------------------------------------ wrappers */

static WrapperBuffer *wrapper_buffer(IMFMediaBuffer *iface)
{
    return CONTAINING_RECORD(iface, WrapperBuffer, iface);
}

static ULONG STDMETHODCALLTYPE wrapper_add_ref(IMFMediaBuffer *iface)
{
    return InterlockedIncrement(&wrapper_buffer(iface)->refs);
}

static ULONG STDMETHODCALLTYPE wrapper_release(IMFMediaBuffer *iface)
{
    WrapperBuffer *buffer = wrapper_buffer(iface);
    ULONG refs = InterlockedDecrement(&buffer->refs);
    if (!refs) {
        if (buffer->inner)
            buffer->inner->lpVtbl->Release(buffer->inner);
        MfFree(buffer);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE wrapper_lock(IMFMediaBuffer *iface, BYTE **data, DWORD *max_length,
                                              DWORD *current_length)
{
    WrapperBuffer *buffer = wrapper_buffer(iface);
    if (!data)
        return E_POINTER;
    if (!buffer->inner)
        return MF_E_UNEXPECTED;
    BYTE *inner = nullptr;
    HRESULT hr = buffer->inner->lpVtbl->Lock(buffer->inner, &inner, nullptr, nullptr);
    if (FAILED(hr))
        return hr;
    *data = inner + buffer->offset;
    if (current_length)
        *current_length = buffer->current_length;
    if (max_length)
        *max_length = buffer->max_length;
    return hr;
}

static HRESULT STDMETHODCALLTYPE wrapper_unlock(IMFMediaBuffer *iface)
{
    WrapperBuffer *buffer = wrapper_buffer(iface);
    return buffer->inner ? buffer->inner->lpVtbl->Unlock(buffer->inner) : MF_E_UNEXPECTED;
}

static HRESULT STDMETHODCALLTYPE wrapper_get_current_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = wrapper_buffer(iface)->current_length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wrapper_set_current_length(IMFMediaBuffer *iface, DWORD length)
{
    WrapperBuffer *buffer = wrapper_buffer(iface);
    if (length > buffer->max_length)
        return E_INVALIDARG;
    buffer->current_length = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wrapper_get_max_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = wrapper_buffer(iface)->max_length;
    return S_OK;
}

static const IMFMediaBufferVtbl WRAPPER_BUFFER_VTBL = {
    buffer_query_interface,     wrapper_add_ref,            wrapper_release,        wrapper_lock, wrapper_unlock,
    wrapper_get_current_length, wrapper_set_current_length, wrapper_get_max_length,
};

/* a range inside the current data of the buffer, full from the start. */
HRESULT WINAPI MFCreateMediaBufferWrapper(IMFMediaBuffer *inner, DWORD offset, DWORD length, IMFMediaBuffer **out)
{
    if (!out)
        return E_POINTER;
    DWORD current = 0;
    HRESULT hr = inner->lpVtbl->GetCurrentLength(inner, &current);
    if (FAILED(hr))
        return hr;
    if ((ULONGLONG)offset + length > current)
        return E_INVALIDARG;
    WrapperBuffer *buffer = MfAlloc(sizeof(*buffer));
    if (!buffer)
        return E_OUTOFMEMORY;
    buffer->iface.lpVtbl = &WRAPPER_BUFFER_VTBL;
    buffer->refs = 1;
    buffer->inner = inner;
    inner->lpVtbl->AddRef(inner);
    buffer->offset = offset;
    buffer->max_length = length;
    buffer->current_length = length;
    *out = &buffer->iface;
    return S_OK;
}
