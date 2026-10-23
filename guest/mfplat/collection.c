/* collections of interfaces, and byte streams over an IStream. */
#include "mfplat_private.h"

#define STREAM_SEEK_SET 0

typedef struct {
    IMFCollection iface;
    LONG refs;
    CRITICAL_SECTION lock;
    IUnknown **elements;
    DWORD count;
    DWORD capacity;
} Collection;

static Collection *collection(IMFCollection *iface)
{
    return CONTAINING_RECORD(iface, Collection, iface);
}

static HRESULT STDMETHODCALLTYPE collection_query_interface(IMFCollection *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFCollection)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE collection_add_ref(IMFCollection *iface)
{
    return InterlockedIncrement(&collection(iface)->refs);
}

static HRESULT STDMETHODCALLTYPE collection_remove_all(IMFCollection *iface)
{
    Collection *c = collection(iface);
    EnterCriticalSection(&c->lock);
    for (DWORD i = 0; i < c->count; i++) {
        if (c->elements[i])
            c->elements[i]->lpVtbl->Release(c->elements[i]);
    }
    c->count = 0;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE collection_release(IMFCollection *iface)
{
    Collection *c = collection(iface);
    ULONG refs = InterlockedDecrement(&c->refs);
    if (!refs) {
        collection_remove_all(iface);
        DeleteCriticalSection(&c->lock);
        MfFree(c->elements);
        MfFree(c);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE collection_get_count(IMFCollection *iface, DWORD *count)
{
    if (!count)
        return E_POINTER;
    *count = collection(iface)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_get_element(IMFCollection *iface, DWORD index, IUnknown **element)
{
    Collection *c = collection(iface);
    if (!element)
        return E_POINTER;
    HRESULT hr = S_OK;
    EnterCriticalSection(&c->lock);
    if (index >= c->count) {
        hr = E_INVALIDARG;
    } else {
        *element = c->elements[index];
        if (*element)
            (*element)->lpVtbl->AddRef(*element);
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE collection_insert(IMFCollection *iface, DWORD index, IUnknown *element)
{
    Collection *c = collection(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&c->lock);
    if (index > c->count) {
        hr = E_INVALIDARG;
    } else if (c->count == c->capacity) {
        DWORD capacity = c->capacity * 2 + 4;
        IUnknown **elements = MfAlloc(capacity * sizeof(*elements));
        if (!elements) {
            hr = E_OUTOFMEMORY;
        } else {
            if (c->count)
                memcpy(elements, c->elements, c->count * sizeof(*elements));
            MfFree(c->elements);
            c->elements = elements;
            c->capacity = capacity;
        }
    }
    if (SUCCEEDED(hr)) {
        memmove(&c->elements[index + 1], &c->elements[index], (c->count - index) * sizeof(*c->elements));
        c->elements[index] = element;
        if (element)
            element->lpVtbl->AddRef(element);
        c->count++;
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE collection_add(IMFCollection *iface, IUnknown *element)
{
    return collection_insert(iface, collection(iface)->count, element);
}

static HRESULT STDMETHODCALLTYPE collection_remove(IMFCollection *iface, DWORD index, IUnknown **element)
{
    Collection *c = collection(iface);
    if (!element)
        return E_POINTER;
    HRESULT hr = S_OK;
    EnterCriticalSection(&c->lock);
    if (index >= c->count) {
        hr = E_INVALIDARG;
    } else {
        *element = c->elements[index];
        memmove(&c->elements[index], &c->elements[index + 1], (c->count - index - 1) * sizeof(*c->elements));
        c->count--;
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static const IMFCollectionVtbl COLLECTION_VTBL = {
    collection_query_interface, collection_add_ref, collection_release, collection_get_count, collection_get_element,
    collection_add,             collection_remove,  collection_insert,  collection_remove_all,
};

HRESULT WINAPI MFCreateCollection(IMFCollection **out)
{
    if (!out)
        return E_POINTER;
    Collection *c = MfAllocZero(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->iface.lpVtbl = &COLLECTION_VTBL;
    c->refs = 1;
    InitializeCriticalSection(&c->lock);
    *out = &c->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ byte streams */

typedef struct {
    IMFByteStream iface;
    LONG refs;
    CRITICAL_SECTION lock;
    IStream *stream;
    QWORD position;
} ByteStream;

/* the state of a BeginRead or BeginWrite, the object of its result. */
typedef struct {
    IUnknown iface;
    LONG refs;
    HRESULT status;
    ULONG done;
} Operation;

static ULONG STDMETHODCALLTYPE operation_add_ref(IUnknown *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, Operation, iface)->refs);
}

static ULONG STDMETHODCALLTYPE operation_release(IUnknown *iface)
{
    Operation *op = CONTAINING_RECORD(iface, Operation, iface);
    ULONG refs = InterlockedDecrement(&op->refs);
    if (!refs)
        MfFree(op);
    return refs;
}

static HRESULT STDMETHODCALLTYPE operation_query_interface(IUnknown *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown)) {
        *out = iface;
        operation_add_ref(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static const IUnknownVtbl OPERATION_VTBL = {operation_query_interface, operation_add_ref, operation_release};

static ByteStream *byte_stream(IMFByteStream *iface)
{
    return CONTAINING_RECORD(iface, ByteStream, iface);
}

static HRESULT STDMETHODCALLTYPE bs_query_interface(IMFByteStream *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFByteStream)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE bs_add_ref(IMFByteStream *iface)
{
    return InterlockedIncrement(&byte_stream(iface)->refs);
}

static ULONG STDMETHODCALLTYPE bs_release(IMFByteStream *iface)
{
    ByteStream *bs = byte_stream(iface);
    ULONG refs = InterlockedDecrement(&bs->refs);
    if (!refs) {
        bs->stream->lpVtbl->Release(bs->stream);
        DeleteCriticalSection(&bs->lock);
        MfFree(bs);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE bs_get_capabilities(IMFByteStream *iface, DWORD *capabilities)
{
    if (!capabilities)
        return E_POINTER;
    *capabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_WRITABLE | MFBYTESTREAM_IS_SEEKABLE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bs_get_length(IMFByteStream *iface, QWORD *length)
{
    if (!length)
        return E_POINTER;
    STATSTG stat = {};
    HRESULT hr = byte_stream(iface)->stream->lpVtbl->Stat(byte_stream(iface)->stream, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr))
        *length = stat.cbSize.QuadPart;
    return hr;
}

static HRESULT STDMETHODCALLTYPE bs_set_length(IMFByteStream *iface, QWORD length)
{
    ULARGE_INTEGER size;
    size.QuadPart = length;
    return byte_stream(iface)->stream->lpVtbl->SetSize(byte_stream(iface)->stream, size);
}

static HRESULT STDMETHODCALLTYPE bs_get_current_position(IMFByteStream *iface, QWORD *position)
{
    if (!position)
        return E_POINTER;
    *position = byte_stream(iface)->position;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bs_set_current_position(IMFByteStream *iface, QWORD position)
{
    byte_stream(iface)->position = position;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bs_is_end_of_stream(IMFByteStream *iface, BOOL *end)
{
    if (!end)
        return E_POINTER;
    QWORD length = 0;
    HRESULT hr = bs_get_length(iface, &length);
    if (SUCCEEDED(hr))
        *end = byte_stream(iface)->position >= length;
    return hr;
}

/* the stream is seeked to the byte stream's position before each transfer. */
static HRESULT transfer(ByteStream *bs, BYTE *buffer, const BYTE *source, ULONG size, ULONG *done)
{
    EnterCriticalSection(&bs->lock);
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)bs->position;
    ULONG count = 0;
    HRESULT hr = bs->stream->lpVtbl->Seek(bs->stream, position, STREAM_SEEK_SET, nullptr);
    if (SUCCEEDED(hr))
        hr = buffer ? bs->stream->lpVtbl->Read(bs->stream, buffer, size, &count)
                    : bs->stream->lpVtbl->Write(bs->stream, source, size, &count);
    if (SUCCEEDED(hr))
        bs->position += count;
    LeaveCriticalSection(&bs->lock);
    if (done)
        *done = count;
    return hr;
}

static HRESULT STDMETHODCALLTYPE bs_read(IMFByteStream *iface, BYTE *buffer, ULONG size, ULONG *read)
{
    return transfer(byte_stream(iface), buffer, nullptr, size, read);
}

static HRESULT STDMETHODCALLTYPE bs_write(IMFByteStream *iface, const BYTE *buffer, ULONG size, ULONG *written)
{
    return transfer(byte_stream(iface), nullptr, buffer, size, written);
}

/* the transfer happens at once, the callback then runs on the standard work queue. */
static HRESULT begin(ByteStream *bs, BYTE *buffer, const BYTE *source, ULONG size, IMFAsyncCallback *callback,
                     IUnknown *state)
{
    if (!callback)
        return E_INVALIDARG;
    Operation *op = MfAllocZero(sizeof(*op));
    if (!op)
        return E_OUTOFMEMORY;
    op->iface.lpVtbl = &OPERATION_VTBL;
    op->refs = 1;
    op->status = transfer(bs, buffer, source, size, &op->done);
    IMFAsyncResult *result;
    HRESULT hr = MfAsyncResultCreate(&op->iface, callback, state, &result);
    operation_release(&op->iface);
    if (FAILED(hr))
        return hr;
    result->lpVtbl->SetStatus(result, op->status);
    hr = MfQueueResult(MFASYNC_CALLBACK_QUEUE_STANDARD, result);
    result->lpVtbl->Release(result);
    return hr;
}

static HRESULT end(IMFAsyncResult *result, ULONG *done)
{
    if (!result || !done)
        return E_INVALIDARG;
    IUnknown *object = nullptr;
    HRESULT hr = result->lpVtbl->GetObject(result, &object);
    if (FAILED(hr))
        return E_INVALIDARG;
    Operation *op = CONTAINING_RECORD(object, Operation, iface);
    *done = op->done;
    hr = op->status;
    object->lpVtbl->Release(object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bs_begin_read(IMFByteStream *iface, BYTE *buffer, ULONG size,
                                               IMFAsyncCallback *callback, IUnknown *state)
{
    return begin(byte_stream(iface), buffer, nullptr, size, callback, state);
}

static HRESULT STDMETHODCALLTYPE bs_end_read(IMFByteStream *iface, IMFAsyncResult *result, ULONG *read)
{
    return end(result, read);
}

static HRESULT STDMETHODCALLTYPE bs_begin_write(IMFByteStream *iface, const BYTE *buffer, ULONG size,
                                                IMFAsyncCallback *callback, IUnknown *state)
{
    return begin(byte_stream(iface), nullptr, buffer, size, callback, state);
}

static HRESULT STDMETHODCALLTYPE bs_end_write(IMFByteStream *iface, IMFAsyncResult *result, ULONG *written)
{
    return end(result, written);
}

static HRESULT STDMETHODCALLTYPE bs_seek(IMFByteStream *iface, MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset,
                                         DWORD flags, QWORD *position)
{
    ByteStream *bs = byte_stream(iface);
    EnterCriticalSection(&bs->lock);
    HRESULT hr = S_OK;
    if (origin == msoBegin)
        bs->position = (QWORD)offset;
    else if (origin == msoCurrent)
        bs->position += (QWORD)offset;
    else
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr) && position)
        *position = bs->position;
    LeaveCriticalSection(&bs->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bs_flush(IMFByteStream *iface)
{
    return byte_stream(iface)->stream->lpVtbl->Commit(byte_stream(iface)->stream, 0);
}

static HRESULT STDMETHODCALLTYPE bs_close(IMFByteStream *iface)
{
    return S_OK;
}

static const IMFByteStreamVtbl BYTE_STREAM_VTBL = {
    bs_query_interface,
    bs_add_ref,
    bs_release,
    bs_get_capabilities,
    bs_get_length,
    bs_set_length,
    bs_get_current_position,
    bs_set_current_position,
    bs_is_end_of_stream,
    bs_read,
    bs_begin_read,
    bs_end_read,
    bs_write,
    bs_begin_write,
    bs_end_write,
    bs_seek,
    bs_flush,
    bs_close,
};

HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream *stream, IMFByteStream **out)
{
    if (!stream || !out)
        return E_POINTER;
    ByteStream *bs = MfAllocZero(sizeof(*bs));
    if (!bs)
        return E_OUTOFMEMORY;
    bs->iface.lpVtbl = &BYTE_STREAM_VTBL;
    bs->refs = 1;
    InitializeCriticalSection(&bs->lock);
    bs->stream = stream;
    stream->lpVtbl->AddRef(stream);
    *out = &bs->iface;
    return S_OK;
}
