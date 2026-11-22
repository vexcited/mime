#include "qmgr.h"

/*
 * CEnum: a snapshot taken when the enumerator is made, of interface pointers for the jobs and files of the current
 * interfaces and of guids for the groups and jobs of BITS 1.0. All five methods take the same shape.
 */
typedef struct ENUM {
    IUnknown iface;
    LONG refs;
    const IID *iid;
    BOOL guids;
    ULONG at;
    ULONG count;
    union {
        IUnknown **items;
        GUID *ids;
    };
} ENUM;

typedef struct ENUM_VTBL {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ENUM *This, REFIID iid, void **out);
    ULONG(STDMETHODCALLTYPE *AddRef)(ENUM *This);
    ULONG(STDMETHODCALLTYPE *Release)(ENUM *This);
    HRESULT(STDMETHODCALLTYPE *Next)(ENUM *This, ULONG count, void *items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(ENUM *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(ENUM *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(ENUM *This, ENUM **copy);
    HRESULT(STDMETHODCALLTYPE *GetCount)(ENUM *This, ULONG *count);
} ENUM_VTBL;

static HRESULT STDMETHODCALLTYPE enum_qi(ENUM *e, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, e->iid)) {
        *out = e;
        InterlockedIncrement(&e->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_addref(ENUM *e)
{
    return (ULONG)InterlockedIncrement(&e->refs);
}

static ULONG STDMETHODCALLTYPE enum_release(ENUM *e)
{
    LONG refs = InterlockedDecrement(&e->refs);
    if (refs)
        return (ULONG)refs;
    if (!e->guids)
        for (ULONG i = 0; i < e->count; i++)
            e->items[i]->lpVtbl->Release(e->items[i]);
    LocalFree(e->items);
    LocalFree(e);
    QmObjectRelease();
    return 0;
}

static HRESULT STDMETHODCALLTYPE enum_next(ENUM *e, ULONG count, void *items, ULONG *fetched)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    if (fetched)
        *fetched = 0;
    if (!items || (count != 1 && !fetched)) {
        QmLeave();
        return E_INVALIDARG;
    }
    ULONG n = 0;
    while (n < count && e->at < e->count) {
        if (e->guids)
            ((GUID *)items)[n] = e->ids[e->at];
        else {
            IUnknown *item = e->items[e->at];
            item->lpVtbl->AddRef(item);
            ((IUnknown **)items)[n] = item;
        }
        n++;
        e->at++;
    }
    if (fetched)
        *fetched = n;
    QmLeave();
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(ENUM *e, ULONG count)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    ULONG left = e->count - e->at;
    hr = count <= left ? S_OK : S_FALSE;
    e->at += count <= left ? count : left;
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE enum_reset(ENUM *e)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    e->at = 0;
    QmLeave();
    return S_OK;
}

static HRESULT make(const IID *iid, BOOL guids, const void *items, ULONG count, ULONG at, ENUM **out);

static HRESULT STDMETHODCALLTYPE enum_clone(ENUM *e, ENUM **copy)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    if (!copy)
        hr = E_INVALIDARG;
    else
        hr = make(e->iid, e->guids, e->guids ? (const void *)e->ids : (const void *)e->items, e->count, e->at, copy);
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE enum_count(ENUM *e, ULONG *count)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    if (!count)
        hr = E_INVALIDARG;
    else
        *count = e->count;
    QmLeave();
    return hr;
}

static const ENUM_VTBL g_enum_vtbl = {enum_qi, enum_addref, enum_release, enum_next,
                                      enum_skip, enum_reset, enum_clone, enum_count};

static HRESULT make(const IID *iid, BOOL guids, const void *items, ULONG count, ULONG at, ENUM **out)
{
    *out = nullptr;
    ENUM *e = LocalAlloc(LMEM_ZEROINIT, sizeof(*e));
    size_t size = (size_t)(count ? count : 1) * (guids ? sizeof(GUID) : sizeof(IUnknown *));
    void *copy = LocalAlloc(LMEM_FIXED, size);
    if (!e || !copy) {
        LocalFree(e);
        LocalFree(copy);
        return E_OUTOFMEMORY;
    }
    e->iface.lpVtbl = (const IUnknownVtbl *)&g_enum_vtbl;
    e->refs = 1;
    e->iid = iid;
    e->guids = guids;
    e->at = at;
    e->count = count;
    if (count)
        memcpy(copy, items, (size_t)count * (guids ? sizeof(GUID) : sizeof(IUnknown *)));
    e->items = copy;
    if (!guids)
        for (ULONG i = 0; i < count; i++)
            e->items[i]->lpVtbl->AddRef(e->items[i]);
    QmObjectAdd();
    *out = e;
    return S_OK;
}

HRESULT EnumCreate(const IID *iid, IUnknown **items, ULONG count, void **out)
{
    return make(iid, FALSE, items, count, 0, (ENUM **)out);
}

HRESULT EnumGuidCreate(const IID *iid, const GUID *items, ULONG count, void **out)
{
    return make(iid, TRUE, items, count, 0, (ENUM **)out);
}
