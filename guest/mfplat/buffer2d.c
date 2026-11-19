/*
 * mfplat's 2D buffers: CMFDX9SurfaceBuffer over a direct3d 9 surface from MFCreateDXSurfaceBuffer, and
 * CMF2DMemMediaBuffer over memory from MFCreate2DMediaBuffer. Both share CMF2DMediaBuffer, whose lock rules, lengths
 * and copies were read from mfplat of windows 11 and measured with evrbuf.exe and evr2d.exe.
 */
#include "mfplat_private.h"
#include <mime/win32/evr.h>

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)
#define ERROR_WAS_LOCKED_HR HRESULT_FROM_WIN32(717)
#define ERROR_WAS_UNLOCKED_HR HRESULT_FROM_WIN32(715)
#ifndef MF_E_INVALIDMEDIATYPE
#define MF_E_INVALIDMEDIATYPE ((HRESULT)0xC00D36B4L)
#endif
#define D3DERR_WASSTILLDRAWING_HR ((HRESULT)0x8876021C)

/*
 * the planar formats: the first plane is followed by chroma rows of the pitch divided by width_div and multiplied by
 * pitch_mul, height divided by height_div.
 */
static const struct {
    DWORD format, width_div, height_div, pitch_mul, alignment, planes;
} PLANAR[] = {
    {FCC('N', 'V', '1', '2'), 1, 2, 1, 0, 2},   {FCC('N', 'V', '2', '4'), 1, 2, 1, 0, 2},
    {FCC('N', 'V', '1', '1'), 2, 1, 1, 128, 2}, {FCC('I', 'Y', 'U', 'V'), 2, 1, 1, 128, 3},
    {FCC('I', '4', '2', '0'), 2, 1, 1, 128, 3}, {FCC('Y', 'V', '1', '2'), 2, 1, 1, 128, 3},
    {FCC('I', 'M', 'C', '2'), 2, 1, 1, 128, 2}, {FCC('I', 'M', 'C', '4'), 2, 1, 1, 128, 2},
    {FCC('I', 'M', 'C', '1'), 2, 1, 2, 128, 3}, {FCC('I', 'M', 'C', '3'), 2, 1, 2, 128, 3},
    {FCC('P', '0', '1', '0'), 1, 2, 1, 0, 2},   {FCC('P', '0', '1', '6'), 1, 2, 1, 0, 2},
};

typedef struct {
    IMFMediaBuffer buffer;
    IMF2DBuffer2 buffer2d;
    IMFGetService service;
    LONG refs;
    CRITICAL_SECTION lock;
    /* the image is in the surface, or in memory rows of allocated_pitch. */
    IDirect3DSurface9 *surface;
    IMFMediaBuffer *memory;
    LONG allocated_pitch;
    DWORD width, height, format;
    LONG contiguous_pitch;
    DWORD valid_bytes, width_div, height_div, pitch_mul, alignment;
    DWORD current_length;
    DWORD locks_1d;
    BOOL clean;
    IMFMediaBuffer *copy;
    DWORD lock_flags;
    struct {
        DWORD refs;
        LONG pitch;
        BYTE *scanline0;
        DWORD height;
        BOOL write;
    } lock_2d;
} Buffer2D;

static Buffer2D *from_buffer(IMFMediaBuffer *iface)
{
    return CONTAINING_RECORD(iface, Buffer2D, buffer);
}

static Buffer2D *from_2d(IMF2DBuffer2 *iface)
{
    return CONTAINING_RECORD(iface, Buffer2D, buffer2d);
}

static Buffer2D *from_service(IMFGetService *iface)
{
    return CONTAINING_RECORD(iface, Buffer2D, service);
}

static DWORD plane_size(Buffer2D *b, LONG pitch)
{
    DWORD row = pitch < 0 ? (DWORD)-pitch : (DWORD)pitch, size = row * b->height;
    if (b->height_div * b->width_div)
        size += b->height * b->pitch_mul * (row / b->width_div) / b->height_div;
    return size;
}

/* the surface is locked whole, and a busy surface is tried again a millisecond later. */
static HRESULT surface_lock(Buffer2D *b, BYTE **scanline0, LONG *pitch)
{
    D3DLOCKED_RECT locked = {0};
    HRESULT hr;
    if (b->memory) {
        DWORD length = 0;
        hr = b->memory->lpVtbl->Lock(b->memory, scanline0, nullptr, &length);
        if (FAILED(hr))
            return hr;
        if (b->allocated_pitch < 0)
            *scanline0 += -b->allocated_pitch * (LONG)(b->height - 1);
        *pitch = b->allocated_pitch;
        return hr;
    }
    while ((hr = b->surface->lpVtbl->LockRect(b->surface, &locked, nullptr, D3DLOCK_NOSYSLOCK | D3DLOCK_DONOTWAIT)) ==
           D3DERR_WASSTILLDRAWING_HR)
        Sleep(1);
    if (SUCCEEDED(hr)) {
        *scanline0 = locked.pBits;
        *pitch = locked.Pitch;
    }
    return hr;
}

static HRESULT do_lock_2d(Buffer2D *b, DWORD flags, BYTE **scanline0, LONG *pitch, BYTE **start, DWORD *length)
{
    HRESULT hr = S_OK;
    if (!flags)
        flags = MF2DBuffer_LockFlags_ReadWrite;
    if (b->lock_2d.refs) {
        if (b->lock_flags == MF2DBuffer_LockFlags_Write && b->lock_2d.write && flags != MF2DBuffer_LockFlags_Write) {
            hr = ERROR_WAS_LOCKED_HR;
            goto failed;
        }
        b->lock_flags |= flags;
        b->lock_2d.refs++;
    } else {
        hr = surface_lock(b, &b->lock_2d.scanline0, &b->lock_2d.pitch);
        if (FAILED(hr))
            goto failed;
        b->lock_2d.refs++;
        b->lock_2d.height = b->height;
        if (flags & MF2DBuffer_LockFlags_Write)
            b->lock_2d.write = TRUE;
        b->lock_flags = flags;
    }
    if (start) {
        *start = b->lock_2d.scanline0;
        if (b->lock_2d.pitch < 0)
            *start = b->lock_2d.scanline0 + b->lock_2d.pitch * (LONG)(b->height - 1);
    }
    if (scanline0)
        *scanline0 = b->lock_2d.scanline0;
    if (pitch)
        *pitch = b->lock_2d.pitch;
    if (length)
        *length = plane_size(b, b->lock_2d.pitch);
    return hr;
failed:
    if (scanline0)
        *scanline0 = nullptr;
    if (pitch)
        *pitch = 0;
    if (start)
        *start = nullptr;
    if (length)
        *length = 0;
    return hr;
}

static HRESULT do_unlock_2d(Buffer2D *b)
{
    if (!b->lock_2d.refs)
        return ERROR_WAS_UNLOCKED_HR;
    if (--b->lock_2d.refs)
        return S_OK;
    HRESULT hr = b->memory ? b->memory->lpVtbl->Unlock(b->memory) : b->surface->lpVtbl->UnlockRect(b->surface);
    if (SUCCEEDED(hr))
        memset(&b->lock_2d, 0, sizeof(b->lock_2d));
    return hr;
}

/* both sides are checked to hold height rows of their pitch, then the first plane and the chroma rows are copied. */
static HRESULT copy_image(Buffer2D *b, BYTE *dest, DWORD dest_size, LONG dest_pitch, const BYTE *src,
                          DWORD src_size, LONG src_pitch)
{
    const HRESULT overflow = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    const HRESULT too_small = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    DWORD lines = b->height;
    DWORD dest_row = dest_pitch < 0 ? (DWORD)-dest_pitch : (DWORD)dest_pitch;
    DWORD src_row = src_pitch < 0 ? (DWORD)-src_pitch : (DWORD)src_pitch;
    if ((ULONGLONG)lines * dest_row > 0xFFFFFFFF || (ULONGLONG)lines * src_row > 0xFFFFFFFF)
        return overflow;
    if (lines * dest_row > dest_size || lines * src_row > src_size)
        return too_small;
    BYTE *to = dest_pitch < 0 ? dest + (DWORD)(dest_pitch * (LONG)(1 - lines)) : dest;
    const BYTE *from = src_pitch < 0 ? src + (DWORD)(src_pitch * (LONG)(1 - lines)) : src;
    MFCopyImage(to, dest_pitch, from, src_pitch, b->valid_bytes, lines);
    if (!(b->height_div * b->width_div))
        return S_OK;
    DWORD chroma_lines = b->height / b->height_div;
    DWORD chroma_dest = b->pitch_mul * ((DWORD)dest_pitch / b->width_div);
    DWORD chroma_src = b->pitch_mul * ((DWORD)src_pitch / b->width_div);
    BYTE *chroma_to = to + (DWORD)dest_pitch * b->height;
    const BYTE *chroma_from = from + (DWORD)src_pitch * b->height;
    if ((ULONGLONG)chroma_lines * chroma_dest > 0xFFFFFFFF || (ULONGLONG)chroma_lines * chroma_src > 0xFFFFFFFF)
        return overflow;
    if ((DWORD)(chroma_to - dest) + chroma_lines * chroma_dest > dest_size ||
        (DWORD)(chroma_from - src) + chroma_lines * chroma_src > src_size)
        return too_small;
    MFCopyImage(chroma_to, (LONG)chroma_dest, chroma_from, (LONG)chroma_src, b->valid_bytes / b->width_div,
                chroma_lines);
    return S_OK;
}

static HRESULT copy_from(Buffer2D *b, const BYTE *src, DWORD size)
{
    DWORD contiguous = plane_size(b, b->contiguous_pitch), length = 0;
    BYTE *scanline0 = nullptr, *start = nullptr;
    LONG pitch = 0;
    if (!src || contiguous > size)
        return E_INVALIDARG;
    HRESULT hr = do_lock_2d(b, MF2DBuffer_LockFlags_Write, &scanline0, &pitch, &start, &length);
    if (FAILED(hr))
        return hr;
    hr = copy_image(b, start, length, pitch, src, size, b->contiguous_pitch);
    if (SUCCEEDED(hr) && !b->current_length) {
        b->current_length = contiguous;
        b->clean = FALSE;
    }
    HRESULT unlocked = do_unlock_2d(b);
    return SUCCEEDED(hr) && FAILED(unlocked) ? unlocked : hr;
}

static HRESULT copy_to(Buffer2D *b, BYTE *dest, DWORD size)
{
    DWORD contiguous = plane_size(b, b->contiguous_pitch), length = 0;
    BYTE *scanline0 = nullptr, *start = nullptr;
    LONG pitch = 0;
    if (!dest)
        return E_POINTER;
    if (contiguous > size)
        return E_INVALIDARG;
    HRESULT hr = do_lock_2d(b, MF2DBuffer_LockFlags_Read, &scanline0, &pitch, &start, &length);
    if (FAILED(hr))
        return hr;
    hr = copy_image(b, dest, size, b->contiguous_pitch, start, length, pitch);
    HRESULT unlocked = do_unlock_2d(b);
    return SUCCEEDED(hr) && FAILED(unlocked) ? unlocked : hr;
}

static HRESULT query_interface(Buffer2D *b, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    /* read from mfplat: IUnknown is the IMF2DBuffer2 part, not the IMFMediaBuffer one. */
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMF2DBuffer) || IsEqualGUID(riid, &IID_IMF2DBuffer2))
        *out = &b->buffer2d;
    else if (IsEqualGUID(riid, &IID_IMFMediaBuffer))
        *out = &b->buffer;
    else if (IsEqualGUID(riid, &IID_IMFGetService))
        *out = &b->service;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&b->refs);
    return S_OK;
}

static ULONG add_ref(Buffer2D *b)
{
    return InterlockedIncrement(&b->refs);
}

static ULONG release(Buffer2D *b)
{
    ULONG refs = InterlockedDecrement(&b->refs);
    if (!refs) {
        if (b->surface)
            b->surface->lpVtbl->Release(b->surface);
        if (b->memory)
            b->memory->lpVtbl->Release(b->memory);
        if (b->copy)
            b->copy->lpVtbl->Release(b->copy);
        DeleteCriticalSection(&b->lock);
        MfFree(b);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE buffer_query_interface(IMFMediaBuffer *iface, REFIID riid, void **out)
{
    return query_interface(from_buffer(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE buffer_add_ref(IMFMediaBuffer *iface)
{
    return add_ref(from_buffer(iface));
}

static ULONG STDMETHODCALLTYPE buffer_release(IMFMediaBuffer *iface)
{
    return release(from_buffer(iface));
}

/* a plain lock goes through a memory copy of the image, written back when the last lock ends. */
static HRESULT STDMETHODCALLTYPE buffer_lock(IMFMediaBuffer *iface, BYTE **data, DWORD *max_length,
                                             DWORD *current_length)
{
    Buffer2D *b = from_buffer(iface);
    HRESULT hr = E_POINTER;
    EnterCriticalSection(&b->lock);
    if (!data)
        goto failed;
    hr = MF_E_INVALIDREQUEST;
    if (b->lock_2d.refs)
        goto failed_data;
    if (!b->copy) {
        IMFMediaBuffer *copy = nullptr;
        BYTE *bytes = nullptr;
        DWORD length = plane_size(b, b->contiguous_pitch);
        hr = MfCreateMemoryBuffer(length, 0, &copy);
        if (FAILED(hr))
            goto failed_data;
        hr = copy->lpVtbl->Lock(copy, &bytes, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            if (!b->clean && !b->locks_1d)
                hr = copy_to(b, bytes, length);
            if (SUCCEEDED(hr))
                hr = copy->lpVtbl->SetCurrentLength(copy, length);
            copy->lpVtbl->Unlock(copy);
        }
        if (FAILED(hr)) {
            copy->lpVtbl->Release(copy);
            goto failed_data;
        }
        b->copy = copy;
    }
    hr = b->copy->lpVtbl->Lock(b->copy, data, max_length, current_length);
    if (FAILED(hr))
        goto failed_data;
    b->locks_1d++;
    b->clean = FALSE;
    LeaveCriticalSection(&b->lock);
    return hr;
failed_data:
    *data = nullptr;
failed:
    if (max_length)
        *max_length = 0;
    if (current_length)
        *current_length = 0;
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE buffer_unlock(IMFMediaBuffer *iface)
{
    Buffer2D *b = from_buffer(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&b->lock);
    if (!b->locks_1d) {
        LeaveCriticalSection(&b->lock);
        return ERROR_WAS_UNLOCKED_HR;
    }
    if (!--b->locks_1d && b->copy) {
        const BYTE *bytes = nullptr;
        DWORD length = 0;
        hr = b->copy->lpVtbl->Lock(b->copy, (BYTE **)&bytes, nullptr, &length);
        if (SUCCEEDED(hr)) {
            hr = copy_from(b, bytes, length);
            if (SUCCEEDED(hr))
                hr = b->copy->lpVtbl->Unlock(b->copy);
        }
        if (SUCCEEDED(hr))
            hr = b->copy->lpVtbl->Unlock(b->copy);
        if (SUCCEEDED(hr)) {
            b->copy->lpVtbl->Release(b->copy);
            b->copy = nullptr;
        }
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE buffer_get_current_length(IMFMediaBuffer *iface, DWORD *length)
{
    Buffer2D *b = from_buffer(iface);
    if (!length)
        return E_POINTER;
    EnterCriticalSection(&b->lock);
    *length = b->current_length;
    LeaveCriticalSection(&b->lock);
    return S_OK;
}

/* any length is taken, and a length of zero marks the image as never written. */
static HRESULT STDMETHODCALLTYPE buffer_set_current_length(IMFMediaBuffer *iface, DWORD length)
{
    Buffer2D *b = from_buffer(iface);
    if (b->memory) {
        HRESULT hr = b->memory->lpVtbl->SetCurrentLength(b->memory, length);
        if (FAILED(hr))
            return hr;
    }
    EnterCriticalSection(&b->lock);
    b->current_length = length;
    b->clean = !length;
    LeaveCriticalSection(&b->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_get_max_length(IMFMediaBuffer *iface, DWORD *length)
{
    Buffer2D *b = from_buffer(iface);
    if (!length)
        return E_POINTER;
    if (b->memory)
        return b->memory->lpVtbl->GetMaxLength(b->memory, length);
    EnterCriticalSection(&b->lock);
    *length = plane_size(b, b->lock_2d.refs ? b->lock_2d.pitch : b->contiguous_pitch);
    LeaveCriticalSection(&b->lock);
    return S_OK;
}

static const IMFMediaBufferVtbl BUFFER_VTBL = {
    buffer_query_interface,     buffer_add_ref,           buffer_release,        buffer_lock, buffer_unlock,
    buffer_get_current_length, buffer_set_current_length, buffer_get_max_length,
};

static HRESULT STDMETHODCALLTYPE b2d_query_interface(IMF2DBuffer2 *iface, REFIID riid, void **out)
{
    return query_interface(from_2d(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE b2d_add_ref(IMF2DBuffer2 *iface)
{
    return add_ref(from_2d(iface));
}

static ULONG STDMETHODCALLTYPE b2d_release(IMF2DBuffer2 *iface)
{
    return release(from_2d(iface));
}

static HRESULT STDMETHODCALLTYPE b2d_lock_2d(IMF2DBuffer2 *iface, BYTE **scanline0, LONG *pitch)
{
    Buffer2D *b = from_2d(iface);
    if (!scanline0 || !pitch)
        return E_POINTER;
    EnterCriticalSection(&b->lock);
    HRESULT hr = MF_E_UNEXPECTED;
    if (!b->locks_1d) {
        DWORD length;
        hr = do_lock_2d(b, MF2DBuffer_LockFlags_ReadWrite, scanline0, pitch, nullptr, &length);
        if (SUCCEEDED(hr))
            b->clean = FALSE;
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE b2d_unlock_2d(IMF2DBuffer2 *iface)
{
    Buffer2D *b = from_2d(iface);
    EnterCriticalSection(&b->lock);
    HRESULT hr = do_unlock_2d(b);
    if (SUCCEEDED(hr) && !b->lock_2d.refs)
        b->lock_flags = MF2DBuffer_LockFlags_ReadWrite;
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE b2d_get_scanline0_and_pitch(IMF2DBuffer2 *iface, BYTE **scanline0, LONG *pitch)
{
    Buffer2D *b = from_2d(iface);
    HRESULT hr = E_POINTER;
    EnterCriticalSection(&b->lock);
    if (scanline0 && pitch) {
        if (b->lock_2d.refs) {
            *scanline0 = b->lock_2d.scanline0;
            *pitch = b->lock_2d.pitch;
            LeaveCriticalSection(&b->lock);
            return S_OK;
        }
        hr = ERROR_WAS_UNLOCKED_HR;
    }
    if (scanline0)
        *scanline0 = nullptr;
    if (pitch)
        *pitch = 0;
    LeaveCriticalSection(&b->lock);
    return hr;
}

/* the lock pitch is compared, so an unlocked buffer is never contiguous. */
static HRESULT STDMETHODCALLTYPE b2d_is_contiguous_format(IMF2DBuffer2 *iface, BOOL *contiguous)
{
    Buffer2D *b = from_2d(iface);
    if (!contiguous)
        return E_POINTER;
    EnterCriticalSection(&b->lock);
    *contiguous = b->lock_2d.pitch == b->contiguous_pitch;
    LeaveCriticalSection(&b->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b2d_get_contiguous_length(IMF2DBuffer2 *iface, DWORD *length)
{
    Buffer2D *b = from_2d(iface);
    if (!length)
        return E_POINTER;
    EnterCriticalSection(&b->lock);
    *length = plane_size(b, b->contiguous_pitch);
    LeaveCriticalSection(&b->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b2d_contiguous_copy_to(IMF2DBuffer2 *iface, BYTE *dest, DWORD size)
{
    Buffer2D *b = from_2d(iface);
    EnterCriticalSection(&b->lock);
    HRESULT hr = copy_to(b, dest, size);
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE b2d_contiguous_copy_from(IMF2DBuffer2 *iface, const BYTE *src, DWORD size)
{
    Buffer2D *b = from_2d(iface);
    EnterCriticalSection(&b->lock);
    HRESULT hr = copy_from(b, src, size);
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE b2d_lock_2d_size(IMF2DBuffer2 *iface, MF2DBuffer_LockFlags flags, BYTE **scanline0,
                                                  LONG *pitch, BYTE **start, DWORD *length)
{
    Buffer2D *b = from_2d(iface);
    if (!scanline0 || !pitch || !start || !length)
        return E_POINTER;
    EnterCriticalSection(&b->lock);
    HRESULT hr = MF_E_UNEXPECTED;
    if (!b->locks_1d) {
        *length = 0;
        hr = do_lock_2d(b, flags, scanline0, pitch, start, length);
        if (SUCCEEDED(hr))
            b->clean = FALSE;
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE b2d_copy_2d_to(IMF2DBuffer2 *iface, IMF2DBuffer2 *dest)
{
    Buffer2D *b = from_2d(iface);
    if (!dest)
        return E_INVALIDARG;
    EnterCriticalSection(&b->lock);
    DWORD dest_length = 0, length = 0, src_size = 0, dest_size = 0;
    BYTE *scanline0, *src = nullptr, *dest_scanline0 = nullptr, *dest_start = nullptr;
    LONG pitch = 0, dest_pitch = 0;
    HRESULT hr = dest->lpVtbl->GetContiguousLength(dest, &dest_length);
    if (SUCCEEDED(hr))
        hr = buffer_get_max_length(&b->buffer, &length);
    if (SUCCEEDED(hr) && dest_length < length)
        hr = E_INVALIDARG;
    if (FAILED(hr))
        goto done;
    hr = do_lock_2d(b, MF2DBuffer_LockFlags_Read, &scanline0, &pitch, &src, &src_size);
    if (FAILED(hr))
        goto done;
    hr = dest->lpVtbl->Lock2DSize(dest, MF2DBuffer_LockFlags_Write, &dest_scanline0, &dest_pitch, &dest_start,
                                  &dest_size);
    BOOL dest_locked = SUCCEEDED(hr);
    if (SUCCEEDED(hr))
        hr = copy_image(b, dest_start, dest_size, dest_pitch, src, src_size, pitch);
    if (SUCCEEDED(hr)) {
        dest_locked = FALSE;
        hr = dest->lpVtbl->Unlock2D(dest);
    }
    if (SUCCEEDED(hr)) {
        IMFMediaBuffer *media = nullptr;
        hr = dest->lpVtbl->QueryInterface(dest, &IID_IMFMediaBuffer, (void **)&media);
        if (SUCCEEDED(hr)) {
            hr = media->lpVtbl->SetCurrentLength(media, length);
            media->lpVtbl->Release(media);
        }
    }
    HRESULT unlocked = do_unlock_2d(b);
    if (SUCCEEDED(hr) && FAILED(unlocked))
        hr = unlocked;
    if (dest_locked) {
        unlocked = dest->lpVtbl->Unlock2D(dest);
        if (SUCCEEDED(hr) && FAILED(unlocked))
            hr = unlocked;
    }
done:
    LeaveCriticalSection(&b->lock);
    return hr;
}

static const IMF2DBuffer2Vtbl BUFFER_2D_VTBL = {
    b2d_query_interface,      b2d_add_ref,
    b2d_release,              b2d_lock_2d,
    b2d_unlock_2d,            b2d_get_scanline0_and_pitch,
    b2d_is_contiguous_format, b2d_get_contiguous_length,
    b2d_contiguous_copy_to,   b2d_contiguous_copy_from,
    b2d_lock_2d_size,         b2d_copy_2d_to,
};

static HRESULT STDMETHODCALLTYPE service_query_interface(IMFGetService *iface, REFIID riid, void **out)
{
    return query_interface(from_service(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE service_add_ref(IMFGetService *iface)
{
    return add_ref(from_service(iface));
}

static ULONG STDMETHODCALLTYPE service_release(IMFGetService *iface)
{
    return release(from_service(iface));
}

static HRESULT STDMETHODCALLTYPE service_get_service(IMFGetService *iface, REFGUID service, REFIID riid, void **out)
{
    Buffer2D *b = from_service(iface);
    if (!b->surface) {
        if (out)
            *out = nullptr;
        return E_NOTIMPL;
    }
    if (!IsEqualGUID(service, &MR_BUFFER_SERVICE))
        return MF_E_UNSUPPORTED_SERVICE;
    return b->surface->lpVtbl->QueryInterface(b->surface, riid, out);
}

static const IMFGetServiceVtbl SERVICE_VTBL = {
    service_query_interface,
    service_add_ref,
    service_release,
    service_get_service,
};

/* the bits of a pixel for the formats MFGetStrideForBitmapInfoHeader refuses, whose rows then run upwards. */
static DWORD fallback_bits(DWORD format)
{
    switch (format) {
    case 50:
        return 8;
    case 63:
        return 32;
    case 81:
        return 16;
    case 110:
        return 64;
    }
    return 0;
}

static HRESULT buffer_init(Buffer2D *b, DWORD width, DWORD height, DWORD format, BOOL bottom_up)
{
    b->width = width;
    b->height = height;
    b->format = format;
    b->lock_flags = MF2DBuffer_LockFlags_ReadWrite;
    LONG stride = 0;
    HRESULT hr = MFGetStrideForBitmapInfoHeader(format, width, &stride);
    if (FAILED(hr)) {
        DWORD bits = fallback_bits(format);
        if (bits)
            hr = S_OK;
        stride = -(LONG)((width * (bits >> 3) + 3) & ~3u);
        if (FAILED(hr))
            return hr;
    }
    if (!stride)
        return MF_E_INVALIDMEDIATYPE;
    LONG magnitude = stride < 0 ? -stride : stride;
    b->contiguous_pitch = bottom_up ? -magnitude : magnitude;
    b->valid_bytes = (DWORD)magnitude;
    for (ULONG i = 0; i < RTL_NUMBER_OF(PLANAR); i++) {
        if (PLANAR[i].format == format) {
            b->width_div = PLANAR[i].width_div;
            b->height_div = PLANAR[i].height_div;
            b->pitch_mul = PLANAR[i].pitch_mul;
            b->alignment = PLANAR[i].alignment;
            break;
        }
    }
    if (b->width_div && b->contiguous_pitch < 0)
        return MF_E_INVALIDMEDIATYPE;
    return S_OK;
}

HRESULT WINAPI MFCreateDXSurfaceBuffer(REFIID riid, IUnknown *object, BOOL bottom_up, IMFMediaBuffer **out)
{
    if (!out)
        return E_POINTER;
    if (!IsEqualGUID(riid, &IID_IDirect3DSurface9))
        return E_INVALIDARG;
    IDirect3DSurface9 *surface = nullptr;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_IDirect3DSurface9, (void **)&surface);
    if (FAILED(hr))
        return hr;
    D3DSURFACE_DESC desc = {0};
    hr = surface->lpVtbl->GetDesc(surface, &desc);
    Buffer2D *b = nullptr;
    if (SUCCEEDED(hr)) {
        b = MfAllocZero(sizeof(*b));
        hr = b ? S_OK : E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        b->buffer.lpVtbl = &BUFFER_VTBL;
        b->buffer2d.lpVtbl = &BUFFER_2D_VTBL;
        b->service.lpVtbl = &SERVICE_VTBL;
        b->refs = 1;
        InitializeCriticalSection(&b->lock);
        b->surface = surface;
        surface->lpVtbl->AddRef(surface);
        b->clean = FALSE;
        hr = buffer_init(b, desc.Width, desc.Height, desc.Format, bottom_up);
        if (SUCCEEDED(hr))
            hr = query_interface(b, &IID_IMFMediaBuffer, (void **)out);
    }
    surface->lpVtbl->Release(surface);
    if (b)
        release(b);
    return hr;
}

/* rows of memory are aligned to 64 bytes, or to the alignment of the planar format. */
static LONG align_cpu_stride(DWORD alignment, LONG pitch)
{
    LONG unit = alignment ? (LONG)alignment : 64;
    if (pitch < 0)
        return -(LONG)(~(unit - 1) & (unit - pitch - 1));
    return (LONG)(~(DWORD)(unit - 1) & (DWORD)(unit + pitch - 1));
}

HRESULT WINAPI MFCreate2DMediaBuffer(DWORD width, DWORD height, DWORD format, BOOL bottom_up, IMFMediaBuffer **out)
{
    if (!out)
        return E_POINTER;
    /* read from mfplat: NV24 wants a height that is a multiple of 32. */
    if (format == FCC('N', 'V', '2', '4') && height != ((height + 31) & ~31u)) {
        *out = nullptr;
        return E_INVALIDARG;
    }
    Buffer2D *b = MfAllocZero(sizeof(*b));
    if (!b) {
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    b->buffer.lpVtbl = &BUFFER_VTBL;
    b->buffer2d.lpVtbl = &BUFFER_2D_VTBL;
    b->service.lpVtbl = &SERVICE_VTBL;
    b->refs = 1;
    InitializeCriticalSection(&b->lock);
    b->clean = TRUE;
    HRESULT hr = buffer_init(b, width, height, format, bottom_up);
    if (SUCCEEDED(hr)) {
        b->allocated_pitch = align_cpu_stride(b->alignment, b->contiguous_pitch);
        hr = MfCreateMemoryBuffer(plane_size(b, b->allocated_pitch), 0x1000, &b->memory);
    }
    if (SUCCEEDED(hr))
        hr = query_interface(b, &IID_IMFMediaBuffer, (void **)out);
    if (FAILED(hr))
        *out = nullptr;
    release(b);
    return hr;
}
