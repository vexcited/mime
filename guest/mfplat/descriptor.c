/* stream descriptors with their media type handlers, and presentation descriptors. */
#include "mfplat_private.h"

typedef struct {
    MfObject base;
    IMFMediaTypeHandler handler;
    DWORD identifier;
    IMFMediaType **types;
    DWORD count;
    IMFMediaType *current;
} StreamDescriptor;

typedef struct {
    IMFAttributesVtbl attributes;
    HRESULT(STDMETHODCALLTYPE *GetStreamIdentifier)(IMFStreamDescriptor *This, DWORD *identifier);
    HRESULT(STDMETHODCALLTYPE *GetMediaTypeHandler)(IMFStreamDescriptor *This, IMFMediaTypeHandler **handler);
} StreamDescriptorVtbl;

static StreamDescriptor *stream_descriptor(IMFStreamDescriptor *iface)
{
    return (StreamDescriptor *)iface;
}

static StreamDescriptor *handler_owner(IMFMediaTypeHandler *iface)
{
    return CONTAINING_RECORD(iface, StreamDescriptor, handler);
}

static void stream_destroy(MfObject *object)
{
    StreamDescriptor *stream = (StreamDescriptor *)object;
    for (DWORD i = 0; i < stream->count; i++) {
        if (stream->types[i])
            stream->types[i]->lpVtbl->Release(stream->types[i]);
    }
    MfFree(stream->types);
    if (stream->current)
        stream->current->lpVtbl->Release(stream->current);
}

static const GUID *const STREAM_IIDS[] = {&IID_IMFStreamDescriptor, nullptr};
static const MfClass STREAM_CLASS = {STREAM_IIDS, stream_destroy};

static HRESULT STDMETHODCALLTYPE stream_get_identifier(IMFStreamDescriptor *iface, DWORD *identifier)
{
    if (!identifier)
        return E_POINTER;
    *identifier = stream_descriptor(iface)->identifier;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_media_type_handler(IMFStreamDescriptor *iface,
                                                               IMFMediaTypeHandler **handler)
{
    if (!handler)
        return E_POINTER;
    *handler = &stream_descriptor(iface)->handler;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

static const StreamDescriptorVtbl STREAM_DESCRIPTOR_VTBL = {
    MF_ATTRIBUTES_VTBL,
    stream_get_identifier,
    stream_get_media_type_handler,
};

/* ------------------------------------------------------------------ the media type handler */

static HRESULT STDMETHODCALLTYPE handler_query_interface(IMFMediaTypeHandler *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaTypeHandler)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return MfObjectQueryInterface((IMFAttributes *)handler_owner(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE handler_add_ref(IMFMediaTypeHandler *iface)
{
    return MfObjectAddRef((IMFAttributes *)handler_owner(iface));
}

static ULONG STDMETHODCALLTYPE handler_release(IMFMediaTypeHandler *iface)
{
    return MfObjectRelease((IMFAttributes *)handler_owner(iface));
}

/* matches one of the types or the current one, as IsEqual sees them. */
static HRESULT STDMETHODCALLTYPE handler_is_media_type_supported(IMFMediaTypeHandler *iface, IMFMediaType *type,
                                                                 IMFMediaType **closest)
{
    StreamDescriptor *stream = handler_owner(iface);
    if (!type)
        return E_POINTER;
    HRESULT hr = MF_E_INVALIDMEDIATYPE;
    DWORD flags;
    EnterCriticalSection(&stream->base.store.lock);
    for (DWORD i = 0; i < stream->count; i++) {
        if (stream->types[i] && type->lpVtbl->IsEqual(type, stream->types[i], &flags) == S_OK) {
            hr = S_OK;
            break;
        }
    }
    if (FAILED(hr) && stream->current && type->lpVtbl->IsEqual(type, stream->current, &flags) == S_OK)
        hr = S_OK;
    LeaveCriticalSection(&stream->base.store.lock);
    if (closest)
        *closest = nullptr;
    return hr;
}

static HRESULT STDMETHODCALLTYPE handler_get_media_type_count(IMFMediaTypeHandler *iface, DWORD *count)
{
    if (!count)
        return E_POINTER;
    *count = handler_owner(iface)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE handler_get_media_type_by_index(IMFMediaTypeHandler *iface, DWORD index,
                                                                 IMFMediaType **type)
{
    StreamDescriptor *stream = handler_owner(iface);
    if (!type)
        return E_POINTER;
    if (index >= stream->count)
        return MF_E_NO_MORE_TYPES;
    if (!stream->types[index])
        return E_FAIL;
    *type = stream->types[index];
    (*type)->lpVtbl->AddRef(*type);
    return S_OK;
}

/* any type can become current, the source behind the descriptor decides what it accepts. */
static HRESULT STDMETHODCALLTYPE handler_set_current_media_type(IMFMediaTypeHandler *iface, IMFMediaType *type)
{
    StreamDescriptor *stream = handler_owner(iface);
    if (!type)
        return E_POINTER;
    EnterCriticalSection(&stream->base.store.lock);
    type->lpVtbl->AddRef(type);
    if (stream->current)
        stream->current->lpVtbl->Release(stream->current);
    stream->current = type;
    LeaveCriticalSection(&stream->base.store.lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE handler_get_current_media_type(IMFMediaTypeHandler *iface, IMFMediaType **type)
{
    StreamDescriptor *stream = handler_owner(iface);
    if (!type)
        return E_POINTER;
    HRESULT hr = MF_E_NOT_INITIALIZED;
    EnterCriticalSection(&stream->base.store.lock);
    if (stream->current) {
        *type = stream->current;
        (*type)->lpVtbl->AddRef(*type);
        hr = S_OK;
    }
    LeaveCriticalSection(&stream->base.store.lock);
    return hr;
}

/* the major type of the current type, else of the first one. */
static HRESULT STDMETHODCALLTYPE handler_get_major_type(IMFMediaTypeHandler *iface, GUID *major)
{
    StreamDescriptor *stream = handler_owner(iface);
    if (!major)
        return E_POINTER;
    HRESULT hr = MF_E_NOT_INITIALIZED;
    EnterCriticalSection(&stream->base.store.lock);
    IMFMediaType *type = stream->current ? stream->current : stream->count ? stream->types[0] : nullptr;
    if (type)
        hr = type->lpVtbl->GetMajorType(type, major);
    LeaveCriticalSection(&stream->base.store.lock);
    return hr;
}

static const IMFMediaTypeHandlerVtbl HANDLER_VTBL = {
    handler_query_interface,         handler_add_ref,
    handler_release,                 handler_is_media_type_supported,
    handler_get_media_type_count,    handler_get_media_type_by_index,
    handler_set_current_media_type,  handler_get_current_media_type,
    handler_get_major_type,
};

HRESULT WINAPI MFCreateStreamDescriptor(DWORD identifier, DWORD count, IMFMediaType **types,
                                        IMFStreamDescriptor **out)
{
    if (!count || !types)
        return E_INVALIDARG;
    StreamDescriptor *stream = MfAllocZero(sizeof(*stream));
    IMFMediaType **copy = MfAlloc((SIZE_T)count * sizeof(*copy));
    if (!stream || !copy) {
        MfFree(stream);
        MfFree(copy);
        return E_OUTOFMEMORY;
    }
    MfObjectInit(&stream->base, &STREAM_DESCRIPTOR_VTBL, &STREAM_CLASS, 0);
    stream->handler.lpVtbl = &HANDLER_VTBL;
    stream->identifier = identifier;
    for (DWORD i = 0; i < count; i++) {
        if ((copy[i] = types[i]))
            copy[i]->lpVtbl->AddRef(copy[i]);
    }
    stream->types = copy;
    stream->count = count;
    *out = (IMFStreamDescriptor *)stream;
    return S_OK;
}

/* ------------------------------------------------------------------ presentation descriptors */

typedef struct {
    MfObject base;
    IMFStreamDescriptor **streams;
    BOOL *selected;
    DWORD count;
} PresentationDescriptor;

typedef struct {
    IMFAttributesVtbl attributes;
    HRESULT(STDMETHODCALLTYPE *GetStreamDescriptorCount)(IMFPresentationDescriptor *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetStreamDescriptorByIndex)(IMFPresentationDescriptor *This, DWORD index,
                                                           BOOL *selected, IMFStreamDescriptor **descriptor);
    HRESULT(STDMETHODCALLTYPE *SelectStream)(IMFPresentationDescriptor *This, DWORD index);
    HRESULT(STDMETHODCALLTYPE *DeselectStream)(IMFPresentationDescriptor *This, DWORD index);
    HRESULT(STDMETHODCALLTYPE *Clone)(IMFPresentationDescriptor *This, IMFPresentationDescriptor **copy);
} PresentationDescriptorVtbl;

static PresentationDescriptor *presentation(IMFPresentationDescriptor *iface)
{
    return (PresentationDescriptor *)iface;
}

static void presentation_destroy(MfObject *object)
{
    PresentationDescriptor *pd = (PresentationDescriptor *)object;
    for (DWORD i = 0; i < pd->count; i++)
        pd->streams[i]->lpVtbl->Release(pd->streams[i]);
    MfFree(pd->streams);
    MfFree(pd->selected);
}

static const GUID *const PRESENTATION_IIDS[] = {&IID_IMFPresentationDescriptor, nullptr};
static const MfClass PRESENTATION_CLASS = {PRESENTATION_IIDS, presentation_destroy};

static HRESULT STDMETHODCALLTYPE pd_get_stream_descriptor_count(IMFPresentationDescriptor *iface, DWORD *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = presentation(iface)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pd_get_stream_descriptor_by_index(IMFPresentationDescriptor *iface, DWORD index,
                                                                   BOOL *selected, IMFStreamDescriptor **descriptor)
{
    PresentationDescriptor *pd = presentation(iface);
    if (index >= pd->count || !selected || !descriptor)
        return E_INVALIDARG;
    *selected = pd->selected[index];
    *descriptor = pd->streams[index];
    (*descriptor)->lpVtbl->AddRef(*descriptor);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pd_select_stream(IMFPresentationDescriptor *iface, DWORD index)
{
    PresentationDescriptor *pd = presentation(iface);
    if (index >= pd->count)
        return E_INVALIDARG;
    pd->selected[index] = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pd_deselect_stream(IMFPresentationDescriptor *iface, DWORD index)
{
    PresentationDescriptor *pd = presentation(iface);
    if (index >= pd->count)
        return E_INVALIDARG;
    pd->selected[index] = FALSE;
    return S_OK;
}

/* the same stream descriptors, with the attributes and selections copied. */
static HRESULT STDMETHODCALLTYPE pd_clone(IMFPresentationDescriptor *iface, IMFPresentationDescriptor **copy)
{
    PresentationDescriptor *pd = presentation(iface);
    if (!copy)
        return E_INVALIDARG;
    IMFPresentationDescriptor *clone;
    HRESULT hr = MFCreatePresentationDescriptor(pd->count, pd->streams, &clone);
    if (FAILED(hr))
        return hr;
    hr = iface->lpVtbl->CopyAllItems(iface, (IMFAttributes *)clone);
    if (FAILED(hr)) {
        clone->lpVtbl->Release(clone);
        return hr;
    }
    memcpy(presentation(clone)->selected, pd->selected, pd->count * sizeof(BOOL));
    *copy = clone;
    return S_OK;
}

static const PresentationDescriptorVtbl PRESENTATION_VTBL = {
    MF_ATTRIBUTES_VTBL, pd_get_stream_descriptor_count, pd_get_stream_descriptor_by_index,
    pd_select_stream,   pd_deselect_stream,             pd_clone,
};

HRESULT WINAPI MFCreatePresentationDescriptor(DWORD count, IMFStreamDescriptor **streams,
                                              IMFPresentationDescriptor **out)
{
    if (!out)
        return E_INVALIDARG;
    for (DWORD i = 0; i < count; i++) {
        if (!streams[i])
            return E_INVALIDARG;
    }
    PresentationDescriptor *pd = MfAllocZero(sizeof(*pd));
    IMFStreamDescriptor **copy = MfAlloc((SIZE_T)count * sizeof(*copy));
    BOOL *selected = MfAllocZero((SIZE_T)count * sizeof(BOOL));
    if (!pd || !copy || !selected) {
        MfFree(pd);
        MfFree(copy);
        MfFree(selected);
        return E_OUTOFMEMORY;
    }
    MfObjectInit(&pd->base, &PRESENTATION_VTBL, &PRESENTATION_CLASS, 0);
    for (DWORD i = 0; i < count; i++) {
        copy[i] = streams[i];
        copy[i]->lpVtbl->AddRef(copy[i]);
    }
    pd->streams = copy;
    pd->selected = selected;
    pd->count = count;
    *out = (IMFPresentationDescriptor *)pd;
    return S_OK;
}
