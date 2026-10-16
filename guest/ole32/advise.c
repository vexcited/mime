/* advise holders: the connection lists that ole objects use to notify their sinks. */
#include "ole32_private.h"

typedef struct {
    STATDATA *Items;
    ULONG Count, Capacity;
    DWORD NextConnection;
} ADVISE_LIST;

static void free_target_device(FORMATETC *format)
{
    CoTaskMemFree(format->ptd);
    format->ptd = nullptr;
}

/* copies a format with its target device into task memory. */
static HRESULT copy_format(FORMATETC *dest, const FORMATETC *source)
{
    *dest = *source;
    if (source->ptd) {
        dest->ptd = CoTaskMemAlloc(source->ptd->tdSize);
        if (!dest->ptd)
            return E_OUTOFMEMORY;
        memcpy(dest->ptd, source->ptd, source->ptd->tdSize);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ STATDATA enumerator */

typedef struct {
    IEnumSTATDATA Interface;
    LONG Refs;
    STATDATA *Items;
    ULONG Count;
    ULONG Position;
} STATDATA_ENUM;

static const IEnumSTATDATAVtbl statdata_enum_vtable;

static void free_statdata(STATDATA *items, ULONG count)
{
    for (ULONG i = 0; i < count; i++) {
        free_target_device(&items[i].formatetc);
        if (items[i].pAdvSink)
            items[i].pAdvSink->lpVtbl->Release(items[i].pAdvSink);
    }
    OleFree(items);
}

/* a referenced deep copy of an array of STATDATA. */
static STATDATA *copy_statdata(const STATDATA *items, ULONG count)
{
    STATDATA *copy = OleAllocZero((count ? count : 1) * sizeof(*copy));
    if (!copy)
        return nullptr;
    for (ULONG i = 0; i < count; i++) {
        if (FAILED(copy_format(&copy[i].formatetc, &items[i].formatetc))) {
            free_statdata(copy, i);
            return nullptr;
        }
        copy[i].advf = items[i].advf;
        copy[i].dwConnection = items[i].dwConnection;
        copy[i].pAdvSink = items[i].pAdvSink;
        if (copy[i].pAdvSink)
            copy[i].pAdvSink->lpVtbl->AddRef(copy[i].pAdvSink);
    }
    return copy;
}

static HRESULT create_statdata_enum(const STATDATA *items, ULONG count, ULONG position, IEnumSTATDATA **result)
{
    STATDATA_ENUM *e = OleAllocZero(sizeof(*e));
    STATDATA *copy = e ? copy_statdata(items, count) : nullptr;
    if (!copy) {
        OleFree(e);
        return E_OUTOFMEMORY;
    }
    e->Interface.lpVtbl = &statdata_enum_vtable;
    e->Refs = 1;
    e->Items = copy;
    e->Count = count;
    e->Position = position;
    *result = &e->Interface;
    return S_OK;
}

static HRESULT WINAPI statdata_enum_query(IEnumSTATDATA *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IEnumSTATDATA))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI statdata_enum_addref(IEnumSTATDATA *self)
{
    return InterlockedIncrement(&((STATDATA_ENUM *)self)->Refs);
}

static ULONG WINAPI statdata_enum_release(IEnumSTATDATA *self)
{
    STATDATA_ENUM *e = (STATDATA_ENUM *)self;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        free_statdata(e->Items, e->Count);
        OleFree(e);
    }
    return refs;
}

static HRESULT WINAPI statdata_enum_next(IEnumSTATDATA *self, ULONG count, STATDATA *items, ULONG *fetched)
{
    STATDATA_ENUM *e = (STATDATA_ENUM *)self;
    if (!items || (count > 1 && !fetched))
        return E_INVALIDARG;
    ULONG n = 0;
    HRESULT hr = S_OK;
    while (n < count && e->Position < e->Count) {
        STATDATA *source = &e->Items[e->Position];
        hr = copy_format(&items[n].formatetc, &source->formatetc);
        if (FAILED(hr))
            break;
        items[n].advf = source->advf;
        items[n].dwConnection = source->dwConnection;
        items[n].pAdvSink = source->pAdvSink;
        if (items[n].pAdvSink)
            items[n].pAdvSink->lpVtbl->AddRef(items[n].pAdvSink);
        e->Position++;
        n++;
    }
    if (fetched)
        *fetched = n;
    if (FAILED(hr))
        return hr;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT WINAPI statdata_enum_skip(IEnumSTATDATA *self, ULONG count)
{
    STATDATA_ENUM *e = (STATDATA_ENUM *)self;
    ULONG left = e->Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT WINAPI statdata_enum_reset(IEnumSTATDATA *self)
{
    ((STATDATA_ENUM *)self)->Position = 0;
    return S_OK;
}

static HRESULT WINAPI statdata_enum_clone(IEnumSTATDATA *self, IEnumSTATDATA **clone)
{
    if (!clone)
        return E_POINTER;
    *clone = nullptr;
    STATDATA_ENUM *e = (STATDATA_ENUM *)self;
    return create_statdata_enum(e->Items, e->Count, e->Position, clone);
}

static const IEnumSTATDATAVtbl statdata_enum_vtable = {
    statdata_enum_query, statdata_enum_addref, statdata_enum_release, statdata_enum_next,
    statdata_enum_skip,  statdata_enum_reset,  statdata_enum_clone,
};

/* ------------------------------------------------------------------ the connection list */

static HRESULT list_add(ADVISE_LIST *list, const FORMATETC *format, DWORD advf, IAdviseSink *sink, DWORD *connection)
{
    if (list->Count == list->Capacity) {
        ULONG capacity = list->Capacity ? list->Capacity * 2 : 4;
        STATDATA *grown = list->Items ? HeapReAlloc(GetProcessHeap(), 0, list->Items, capacity * sizeof(*grown)) :
                                        OleAlloc(capacity * sizeof(*grown));
        if (!grown)
            return E_OUTOFMEMORY;
        list->Items = grown;
        list->Capacity = capacity;
    }
    STATDATA *item = &list->Items[list->Count];
    memset(item, 0, sizeof(*item));
    if (format && FAILED(copy_format(&item->formatetc, format)))
        return E_OUTOFMEMORY;
    item->advf = advf;
    item->pAdvSink = sink;
    sink->lpVtbl->AddRef(sink);
    item->dwConnection = ++list->NextConnection;
    list->Count++;
    *connection = item->dwConnection;
    return S_OK;
}

/* removes a connection and hands back its sink reference, or returns NULL. */
static IAdviseSink *list_remove(ADVISE_LIST *list, DWORD connection)
{
    for (ULONG i = 0; i < list->Count; i++) {
        if (list->Items[i].dwConnection != connection)
            continue;
        IAdviseSink *sink = list->Items[i].pAdvSink;
        free_target_device(&list->Items[i].formatetc);
        memmove(&list->Items[i], &list->Items[i + 1], (list->Count - i - 1) * sizeof(*list->Items));
        list->Count--;
        return sink;
    }
    return nullptr;
}

static void list_free(ADVISE_LIST *list)
{
    free_statdata(list->Items, list->Count);
    list->Items = nullptr;
    list->Count = list->Capacity = 0;
}

/* ------------------------------------------------------------------ IDataAdviseHolder */

typedef struct {
    IDataAdviseHolder Interface;
    LONG Refs;
    ADVISE_LIST List;
} DATA_HOLDER;

static HRESULT WINAPI data_query(IDataAdviseHolder *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IDataAdviseHolder))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI data_addref(IDataAdviseHolder *self)
{
    return InterlockedIncrement(&((DATA_HOLDER *)self)->Refs);
}

static ULONG WINAPI data_release(IDataAdviseHolder *self)
{
    DATA_HOLDER *holder = (DATA_HOLDER *)self;
    LONG refs = InterlockedDecrement(&holder->Refs);
    if (!refs) {
        list_free(&holder->List);
        OleFree(holder);
    }
    return refs;
}

/* sends the current data in format to one sink. */
static void send_data(IDataObject *data, FORMATETC *format, DWORD advf, IAdviseSink *sink)
{
    STGMEDIUM medium = {};
    if (advf & ADVF_NODATA) {
        sink->lpVtbl->OnDataChange(sink, format, &medium);
        return;
    }
    if (!data || FAILED(data->lpVtbl->GetData(data, format, &medium)))
        return;
    sink->lpVtbl->OnDataChange(sink, format, &medium);
    ReleaseStgMedium(&medium);
}

static HRESULT WINAPI data_advise(IDataAdviseHolder *self, IDataObject *data, FORMATETC *format, DWORD advf,
                                  IAdviseSink *sink, DWORD *connection)
{
    if (connection)
        *connection = 0;
    if (!format || !sink || !connection)
        return E_INVALIDARG;
    DATA_HOLDER *holder = (DATA_HOLDER *)self;
    HRESULT hr = list_add(&holder->List, format, advf, sink, connection);
    if (FAILED(hr))
        return hr;
    if (advf & ADVF_PRIMEFIRST) {
        send_data(data, format, advf, sink);
        if (advf & ADVF_ONLYONCE) {
            IAdviseSink *removed = list_remove(&holder->List, *connection);
            if (removed)
                removed->lpVtbl->Release(removed);
            *connection = 0;
        }
    }
    return S_OK;
}

static HRESULT WINAPI data_unadvise(IDataAdviseHolder *self, DWORD connection)
{
    IAdviseSink *sink = list_remove(&((DATA_HOLDER *)self)->List, connection);
    if (!sink)
        return OLE_E_NOCONNECTION;
    sink->lpVtbl->Release(sink);
    return S_OK;
}

static HRESULT WINAPI data_enum(IDataAdviseHolder *self, IEnumSTATDATA **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    ADVISE_LIST *list = &((DATA_HOLDER *)self)->List;
    return create_statdata_enum(list->Items, list->Count, 0, enumerator);
}

static HRESULT WINAPI data_send(IDataAdviseHolder *self, IDataObject *data, DWORD reserved, DWORD advf)
{
    ADVISE_LIST *list = &((DATA_HOLDER *)self)->List;
    /* sinks may unadvise while notified, so the current connections are copied first. */
    ULONG count = list->Count;
    STATDATA *snapshot = copy_statdata(list->Items, count);
    if (!snapshot)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < count; i++) {
        STATDATA *item = &snapshot[i];
        send_data(data, &item->formatetc, item->advf | (advf & ADVF_NODATA), item->pAdvSink);
        if (item->advf & ADVF_ONLYONCE) {
            IAdviseSink *removed = list_remove(list, item->dwConnection);
            if (removed)
                removed->lpVtbl->Release(removed);
        }
    }
    free_statdata(snapshot, count);
    return S_OK;
}

static const IDataAdviseHolderVtbl data_vtable = {
    data_query, data_addref, data_release, data_advise, data_unadvise, data_enum, data_send,
};

WINOLEAPI HRESULT WINAPI CreateDataAdviseHolder(IDataAdviseHolder **result)
{
    if (!result)
        return E_INVALIDARG;
    DATA_HOLDER *holder = OleAllocZero(sizeof(*holder));
    *result = nullptr;
    if (!holder)
        return E_OUTOFMEMORY;
    holder->Interface.lpVtbl = &data_vtable;
    holder->Refs = 1;
    *result = &holder->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ IOleAdviseHolder */

typedef struct {
    IOleAdviseHolder Interface;
    LONG Refs;
    ADVISE_LIST List;
} OLE_HOLDER;

static HRESULT WINAPI ole_query(IOleAdviseHolder *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IOleAdviseHolder))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI ole_addref(IOleAdviseHolder *self)
{
    return InterlockedIncrement(&((OLE_HOLDER *)self)->Refs);
}

static ULONG WINAPI ole_release(IOleAdviseHolder *self)
{
    OLE_HOLDER *holder = (OLE_HOLDER *)self;
    LONG refs = InterlockedDecrement(&holder->Refs);
    if (!refs) {
        list_free(&holder->List);
        OleFree(holder);
    }
    return refs;
}

static HRESULT WINAPI ole_advise(IOleAdviseHolder *self, IAdviseSink *sink, DWORD *connection)
{
    if (connection)
        *connection = 0;
    if (!sink || !connection)
        return E_INVALIDARG;
    return list_add(&((OLE_HOLDER *)self)->List, nullptr, 0, sink, connection);
}

static HRESULT WINAPI ole_unadvise(IOleAdviseHolder *self, DWORD connection)
{
    IAdviseSink *sink = list_remove(&((OLE_HOLDER *)self)->List, connection);
    if (!sink)
        return OLE_E_NOCONNECTION;
    sink->lpVtbl->Release(sink);
    return S_OK;
}

static HRESULT WINAPI ole_enum(IOleAdviseHolder *self, IEnumSTATDATA **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    ADVISE_LIST *list = &((OLE_HOLDER *)self)->List;
    return create_statdata_enum(list->Items, list->Count, 0, enumerator);
}

enum { SEND_RENAME, SEND_SAVE, SEND_CLOSE };

static HRESULT ole_send(IOleAdviseHolder *self, int kind, IMoniker *moniker)
{
    ADVISE_LIST *list = &((OLE_HOLDER *)self)->List;
    ULONG count = list->Count;
    STATDATA *snapshot = copy_statdata(list->Items, count);
    if (!snapshot)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < count; i++) {
        IAdviseSink *sink = snapshot[i].pAdvSink;
        if (kind == SEND_RENAME)
            sink->lpVtbl->OnRename(sink, moniker);
        else if (kind == SEND_SAVE)
            sink->lpVtbl->OnSave(sink);
        else
            sink->lpVtbl->OnClose(sink);
    }
    free_statdata(snapshot, count);
    return S_OK;
}

static HRESULT WINAPI ole_send_rename(IOleAdviseHolder *self, IMoniker *moniker)
{
    return ole_send(self, SEND_RENAME, moniker);
}

static HRESULT WINAPI ole_send_save(IOleAdviseHolder *self)
{
    return ole_send(self, SEND_SAVE, nullptr);
}

static HRESULT WINAPI ole_send_close(IOleAdviseHolder *self)
{
    return ole_send(self, SEND_CLOSE, nullptr);
}

static const IOleAdviseHolderVtbl ole_vtable = {
    ole_query, ole_addref, ole_release, ole_advise, ole_unadvise, ole_enum, ole_send_rename, ole_send_save,
    ole_send_close,
};

WINOLEAPI HRESULT WINAPI CreateOleAdviseHolder(IOleAdviseHolder **result)
{
    if (!result)
        return E_INVALIDARG;
    OLE_HOLDER *holder = OleAllocZero(sizeof(*holder));
    *result = nullptr;
    if (!holder)
        return E_OUTOFMEMORY;
    holder->Interface.lpVtbl = &ole_vtable;
    holder->Refs = 1;
    *result = &holder->Interface;
    return S_OK;
}
