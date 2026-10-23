/* IWICStream over files, memory, streams or stream ranges. */
#include "wic_private.h"

enum { STREAM_NONE, STREAM_MEMORY, STREAM_FILE, STREAM_ISTREAM, STREAM_REGION };

typedef struct WicStream {
    WicBase base;
    WicSlot stream;
    CRITICAL_SECTION lock;
    int mode;
    BYTE *memory;
    ULONGLONG size;
    ULONGLONG position;
    HANDLE file;
    IStream *inner;
    ULONGLONG region_start;
    ULONGLONG region_max;
    BOOL owns_memory;
    BOOL writable;
} WicStream;

static WicStream *stream_from(IStream *This) { return WIC_FROM_SLOT(WicStream, stream, This); }

static void stream_destroy(WicBase *base)
{
    WicStream *stream = (WicStream *)base;
    if (stream->file && stream->file != INVALID_HANDLE_VALUE)
        CloseHandle(stream->file);
    if (stream->inner)
        stream->inner->lpVtbl->Release(stream->inner);
    if (stream->owns_memory)
        WicFree(stream->memory);
    DeleteCriticalSection(&stream->lock);
}

static HRESULT file_error(void)
{
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error);
}

static HRESULT STDMETHODCALLTYPE stream_read(IStream *This, void *buffer, ULONG count, ULONG *read)
{
    WicStream *s = stream_from(This);
    HRESULT hr = S_OK;
    ULONG got = 0;
    if (read)
        *read = 0;
    if (!buffer && count)
        return STG_E_INVALIDPOINTER;
    EnterCriticalSection(&s->lock);
    switch (s->mode) {
    case STREAM_MEMORY:
        if (s->position < s->size) {
            ULONGLONG left = s->size - s->position;
            got = count < left ? count : (ULONG)left;
            memcpy(buffer, s->memory + s->position, got);
            s->position += got;
        }
        break;
    case STREAM_FILE:
        if (!ReadFile(s->file, buffer, count, &got, nullptr))
            hr = file_error();
        break;
    case STREAM_ISTREAM:
        hr = s->inner->lpVtbl->Read(s->inner, buffer, count, &got);
        break;
    case STREAM_REGION: {
        ULONGLONG left = s->region_max > s->position ? s->region_max - s->position : 0;
        ULONG want = count < left ? count : (ULONG)left;
        hr = s->inner->lpVtbl->Read(s->inner, buffer, want, &got);
        s->position += got;
        break;
    }
    default:
        hr = WINCODEC_ERR_NOTINITIALIZED;
    }
    LeaveCriticalSection(&s->lock);
    if (read)
        *read = got;
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_write(IStream *This, const void *buffer, ULONG count, ULONG *written)
{
    WicStream *s = stream_from(This);
    HRESULT hr = S_OK;
    ULONG put = 0;
    if (written)
        *written = 0;
    if (!buffer && count)
        return STG_E_INVALIDPOINTER;
    EnterCriticalSection(&s->lock);
    switch (s->mode) {
    case STREAM_MEMORY:
        if (!s->writable) {
            hr = STG_E_ACCESSDENIED;
        } else if (s->position + count > s->size) {
            put = s->position < s->size ? (ULONG)(s->size - s->position) : 0;
            memcpy(s->memory + s->position, buffer, put);
            s->position += put;
            hr = STG_E_MEDIUMFULL;
        } else {
            memcpy(s->memory + s->position, buffer, count);
            s->position += count;
            put = count;
        }
        break;
    case STREAM_FILE:
        if (!s->writable)
            hr = STG_E_ACCESSDENIED;
        else if (!WriteFile(s->file, buffer, count, &put, nullptr))
            hr = file_error();
        break;
    case STREAM_ISTREAM:
        hr = s->inner->lpVtbl->Write(s->inner, buffer, count, &put);
        break;
    case STREAM_REGION: {
        ULONGLONG left = s->region_max > s->position ? s->region_max - s->position : 0;
        if (count > left) {
            hr = STG_E_MEDIUMFULL;
            count = (ULONG)left;
        }
        HRESULT w = s->inner->lpVtbl->Write(s->inner, buffer, count, &put);
        if (SUCCEEDED(hr))
            hr = w;
        s->position += put;
        break;
    }
    default:
        hr = WINCODEC_ERR_NOTINITIALIZED;
    }
    LeaveCriticalSection(&s->lock);
    if (written)
        *written = put;
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_seek(IStream *This, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *result)
{
    WicStream *s = stream_from(This);
    HRESULT hr = S_OK;
    if (result)
        result->QuadPart = 0;
    EnterCriticalSection(&s->lock);
    if (s->mode == STREAM_NONE) {
        LeaveCriticalSection(&s->lock);
        return WINCODEC_ERR_NOTINITIALIZED;
    }
    if (origin > 2) {
        LeaveCriticalSection(&s->lock);
        return STG_E_INVALIDFUNCTION;
    }
    if (s->mode == STREAM_MEMORY || s->mode == STREAM_REGION) {
        LONGLONG base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (LONGLONG)s->position
                        : (LONGLONG)(s->mode == STREAM_MEMORY ? s->size : s->region_max);
        LONGLONG target = base + move.QuadPart;
        if (target < 0) {
            hr = STG_E_INVALIDFUNCTION;
        } else {
            if (s->mode == STREAM_REGION) {
                LARGE_INTEGER abs;
                abs.QuadPart = (LONGLONG)s->region_start + target;
                hr = s->inner->lpVtbl->Seek(s->inner, abs, STREAM_SEEK_SET, nullptr);
            }
            if (SUCCEEDED(hr))
                s->position = (ULONGLONG)target;
        }
        if (result && SUCCEEDED(hr))
            result->QuadPart = s->position;
    } else if (s->mode == STREAM_FILE) {
        LARGE_INTEGER out;
        if (!SetFilePointerEx(s->file, move, &out, origin))
            hr = file_error();
        else if (result)
            result->QuadPart = (ULONGLONG)out.QuadPart;
    } else {
        hr = s->inner->lpVtbl->Seek(s->inner, move, origin, result);
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_set_size(IStream *This, ULARGE_INTEGER size)
{
    WicStream *s = stream_from(This);
    HRESULT hr;
    EnterCriticalSection(&s->lock);
    if (s->mode == STREAM_FILE && s->writable) {
        LARGE_INTEGER pos, now, end;
        pos.QuadPart = 0;
        SetFilePointerEx(s->file, pos, &now, FILE_CURRENT);
        end.QuadPart = (LONGLONG)size.QuadPart;
        hr = SetFilePointerEx(s->file, end, nullptr, FILE_BEGIN) && SetEndOfFile(s->file) ? S_OK : file_error();
        SetFilePointerEx(s->file, now, nullptr, FILE_BEGIN);
    } else if (s->mode == STREAM_ISTREAM) {
        hr = s->inner->lpVtbl->SetSize(s->inner, size);
    } else if (s->mode == STREAM_NONE) {
        hr = WINCODEC_ERR_NOTINITIALIZED;
    } else {
        hr = E_NOTIMPL;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_copy_to(IStream *This, IStream *dest, ULARGE_INTEGER count,
                                                ULARGE_INTEGER *read, ULARGE_INTEGER *written)
{
    BYTE buffer[4096];
    ULONGLONG total_read = 0, total_written = 0, left = count.QuadPart;
    HRESULT hr = S_OK;
    if (read)
        read->QuadPart = 0;
    if (written)
        written->QuadPart = 0;
    if (!dest)
        return STG_E_INVALIDPOINTER;
    while (left) {
        ULONG want = left < sizeof(buffer) ? (ULONG)left : (ULONG)sizeof(buffer), got = 0, put = 0;
        hr = stream_read(This, buffer, want, &got);
        if (FAILED(hr) || !got)
            break;
        total_read += got;
        hr = dest->lpVtbl->Write(dest, buffer, got, &put);
        total_written += put;
        if (FAILED(hr) || put != got)
            break;
        left -= got;
    }
    if (read)
        read->QuadPart = total_read;
    if (written)
        written->QuadPart = total_written;
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_commit(IStream *This, DWORD flags)
{
    WicStream *s = stream_from(This);
    if (s->mode == STREAM_NONE)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (s->mode == STREAM_FILE && s->writable)
        FlushFileBuffers(s->file);
    else if (s->mode == STREAM_ISTREAM || s->mode == STREAM_REGION)
        return s->inner->lpVtbl->Commit(s->inner, flags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_revert(IStream *This) { return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE stream_lock_region(IStream *This, ULARGE_INTEGER offset, ULARGE_INTEGER count,
                                                    DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE stream_unlock_region(IStream *This, ULARGE_INTEGER offset, ULARGE_INTEGER count,
                                                      DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE stream_stat(IStream *This, STATSTG *stat, DWORD flags)
{
    WicStream *s = stream_from(This);
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    EnterCriticalSection(&s->lock);
    HRESULT hr = S_OK;
    stat->type = 2; /* STGTY_STREAM. */
    switch (s->mode) {
    case STREAM_MEMORY:
        stat->cbSize.QuadPart = s->size;
        break;
    case STREAM_FILE: {
        LARGE_INTEGER size;
        if (GetFileSizeEx(s->file, &size))
            stat->cbSize.QuadPart = (ULONGLONG)size.QuadPart;
        else
            hr = file_error();
        break;
    }
    case STREAM_ISTREAM:
        hr = s->inner->lpVtbl->Stat(s->inner, stat, flags);
        break;
    case STREAM_REGION:
        hr = s->inner->lpVtbl->Stat(s->inner, stat, flags);
        if (SUCCEEDED(hr)) {
            ULONGLONG total = stat->cbSize.QuadPart;
            ULONGLONG avail = total > s->region_start ? total - s->region_start : 0;
            stat->cbSize.QuadPart = avail < s->region_max ? avail : s->region_max;
        }
        break;
    default:
        hr = WINCODEC_ERR_NOTINITIALIZED;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_clone(IStream *This, IStream **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT begin_init(WicStream *s)
{
    if (s->mode != STREAM_NONE)
        return WINCODEC_ERR_WRONGSTATE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_init_from_istream(IWICStream *This, IStream *inner)
{
    WicStream *s = WIC_FROM_SLOT(WicStream, stream, This);
    if (!inner)
        return E_INVALIDARG;
    EnterCriticalSection(&s->lock);
    HRESULT hr = begin_init(s);
    if (SUCCEEDED(hr)) {
        inner->lpVtbl->AddRef(inner);
        s->inner = inner;
        s->mode = STREAM_ISTREAM;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_init_from_filename(IWICStream *This, LPCWSTR name, DWORD access)
{
    WicStream *s = WIC_FROM_SLOT(WicStream, stream, This);
    if (!name)
        return E_INVALIDARG;
    EnterCriticalSection(&s->lock);
    HRESULT hr = begin_init(s);
    if (SUCCEEDED(hr)) {
        BOOL write = (access & GENERIC_WRITE) != 0;
        HANDLE file = CreateFileW(name, access, write ? 0 : FILE_SHARE_READ, nullptr,
                                  write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            hr = file_error();
        } else {
            s->file = file;
            s->writable = write;
            s->mode = STREAM_FILE;
        }
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_init_from_memory(IWICStream *This, BYTE *buffer, DWORD size)
{
    WicStream *s = WIC_FROM_SLOT(WicStream, stream, This);
    if (!buffer && size)
        return E_INVALIDARG;
    EnterCriticalSection(&s->lock);
    HRESULT hr = begin_init(s);
    if (SUCCEEDED(hr)) {
        s->memory = buffer;
        s->size = size;
        s->writable = TRUE;
        s->mode = STREAM_MEMORY;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_init_from_region(IWICStream *This, IStream *inner, ULARGE_INTEGER offset,
                                                         ULARGE_INTEGER max_size)
{
    WicStream *s = WIC_FROM_SLOT(WicStream, stream, This);
    if (!inner)
        return E_INVALIDARG;
    EnterCriticalSection(&s->lock);
    HRESULT hr = begin_init(s);
    if (SUCCEEDED(hr)) {
        LARGE_INTEGER at;
        at.QuadPart = (LONGLONG)offset.QuadPart;
        hr = inner->lpVtbl->Seek(inner, at, STREAM_SEEK_SET, nullptr);
        if (SUCCEEDED(hr)) {
            inner->lpVtbl->AddRef(inner);
            s->inner = inner;
            s->region_start = offset.QuadPart;
            s->region_max = max_size.QuadPart;
            s->position = 0;
            s->mode = STREAM_REGION;
        }
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static const IWICStreamVtbl g_stream_vtbl = {
    WIC_IUNKNOWN_FUNCS,
    VTBL_FN(stream_read),
    VTBL_FN(stream_write),
    VTBL_FN(stream_seek),
    VTBL_FN(stream_set_size),
    VTBL_FN(stream_copy_to),
    VTBL_FN(stream_commit),
    VTBL_FN(stream_revert),
    VTBL_FN(stream_lock_region),
    VTBL_FN(stream_unlock_region),
    VTBL_FN(stream_stat),
    VTBL_FN(stream_clone),
    stream_init_from_istream,
    stream_init_from_filename,
    stream_init_from_memory,
    stream_init_from_region,
};

static const WicIfaceEntry g_stream_ifaces[] = {
    WIC_ENTRY(WicStream, IID_IWICStream, stream),
    WIC_ENTRY(WicStream, IID_IStream, stream),
    WIC_ENTRY(WicStream, IID_ISequentialStream, stream),
};
static const WicClass g_stream_class = {stream_destroy, g_stream_ifaces, 3};

HRESULT WicStreamCreate(IWICStream **out)
{
    WicStream *s = WicNew(sizeof(*s), &g_stream_class);
    if (!s)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&s->lock);
    WIC_SLOT(s, stream, &g_stream_vtbl);
    *out = (IWICStream *)&s->stream;
    return S_OK;
}

HRESULT WicMemoryStreamCreate(const BYTE *data, SIZE_T size, BOOL copy, IStream **out)
{
    WicStream *s = WicNew(sizeof(*s), &g_stream_class);
    if (!s)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&s->lock);
    WIC_SLOT(s, stream, &g_stream_vtbl);
    if (copy) {
        s->memory = WicAlloc(size);
        if (!s->memory) {
            WicRelease(&s->stream);
            return E_OUTOFMEMORY;
        }
        memcpy(s->memory, data, size);
        s->owns_memory = TRUE;
    } else {
        s->memory = (BYTE *)data;
    }
    s->size = size;
    s->writable = FALSE;
    s->mode = STREAM_MEMORY;
    *out = (IStream *)&s->stream;
    return S_OK;
}

/* whole streams. */

HRESULT WicReadStream(IStream *stream, BYTE **data, SIZE_T *size, SIZE_T limit)
{
    *data = nullptr;
    *size = 0;
    SIZE_T capacity = 0x10000, used = 0;
    STATSTG stat;
    if (SUCCEEDED(stream->lpVtbl->Stat(stream, &stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0 &&
        stat.cbSize.QuadPart <= limit) {
        capacity = (SIZE_T)stat.cbSize.QuadPart + 1;
        LARGE_INTEGER zero;
        ULARGE_INTEGER pos;
        zero.QuadPart = 0;
        if (SUCCEEDED(stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &pos)) && pos.QuadPart < stat.cbSize.QuadPart)
            capacity = (SIZE_T)(stat.cbSize.QuadPart - pos.QuadPart) + 1;
    }
    BYTE *buffer = WicAlloc(capacity);
    if (!buffer)
        return E_OUTOFMEMORY;
    for (;;) {
        if (used == capacity) {
            if (capacity >= limit) {
                WicFree(buffer);
                return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
            }
            capacity *= 2;
            BYTE *bigger = WicRealloc(buffer, capacity);
            if (!bigger) {
                WicFree(buffer);
                return E_OUTOFMEMORY;
            }
            buffer = bigger;
        }
        ULONG got = 0, want = (ULONG)(capacity - used > 0x40000000 ? 0x40000000 : capacity - used);
        HRESULT hr = stream->lpVtbl->Read(stream, buffer + used, want, &got);
        if (FAILED(hr)) {
            WicFree(buffer);
            return WINCODEC_ERR_STREAMREAD;
        }
        used += got;
        if (!got || hr != S_OK)
            break;
    }
    *data = buffer;
    *size = used;
    return S_OK;
}

HRESULT WicWriteStream(IStream *stream, const void *data, SIZE_T size)
{
    const BYTE *p = data;
    while (size) {
        ULONG chunk = size > 0x40000000 ? 0x40000000 : (ULONG)size, put = 0;
        HRESULT hr = stream->lpVtbl->Write(stream, p, chunk, &put);
        if (FAILED(hr) || !put)
            return FAILED(hr) ? hr : WINCODEC_ERR_STREAMWRITE;
        p += put;
        size -= put;
    }
    return S_OK;
}
