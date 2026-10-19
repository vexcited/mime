/*
 * enumerators. the enumerators of interface pointers all have the same four methods after IUnknown, so one object
 * serves them, answering for the interface it was made for.
 */
#include "msctf_private.h"

typedef struct ENUM {
    const IEnumTfContextsVtbl *Vtbl;
    LONG Refs;
    GUID Iid;
    IUnknown **Items;
    ULONG Count, Position;
} ENUM;

static HRESULT STDMETHODCALLTYPE enum_query(IEnumTfContexts *This, REFIID riid, void **object)
{
    ENUM *e = (ENUM *)This;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &e->Iid)) {
        *object = This;
        InterlockedIncrement(&e->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumTfContexts *This)
{
    return InterlockedIncrement(&((ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumTfContexts *This)
{
    ENUM *e = (ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        for (ULONG i = 0; i < e->Count; i++)
            e->Items[i]->lpVtbl->Release(e->Items[i]);
        CtfFree(e->Items);
        CtfFree(e);
        CtfObjectDestroyed();
    }
    return refs;
}

static const IEnumTfContextsVtbl enum_vtbl;

static HRESULT enum_new(REFIID iid, IUnknown **items, ULONG count, ULONG position, void **out)
{
    ENUM *e = CtfAlloc(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Items = count ? CtfAlloc(count * sizeof(IUnknown *)) : nullptr;
    if (count && !e->Items) {
        CtfFree(e);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < count; i++) {
        e->Items[i] = items[i];
        e->Items[i]->lpVtbl->AddRef(e->Items[i]);
    }
    e->Vtbl = &enum_vtbl;
    e->Refs = 1;
    e->Iid = *iid;
    e->Count = count;
    e->Position = position;
    CtfObjectCreated();
    *out = e;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumTfContexts *This, IEnumTfContexts **clone)
{
    ENUM *e = (ENUM *)This;
    if (!clone)
        return E_INVALIDARG;
    return enum_new(&e->Iid, e->Items, e->Count, e->Position, (void **)clone);
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumTfContexts *This, ULONG count, ITfContext **items, ULONG *fetched)
{
    ENUM *e = (ENUM *)This;
    ULONG n = 0;
    if (!items || (!fetched && count != 1))
        return E_INVALIDARG;
    while (n < count && e->Position < e->Count) {
        items[n] = (ITfContext *)e->Items[e->Position++];
        items[n]->lpVtbl->AddRef(items[n]);
        n++;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumTfContexts *This)
{
    ((ENUM *)This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumTfContexts *This, ULONG count)
{
    ENUM *e = (ENUM *)This;
    ULONG left = e->Count - e->Position;
    if (count > left) {
        e->Position = e->Count;
        return S_FALSE;
    }
    e->Position += count;
    return S_OK;
}

static const IEnumTfContextsVtbl enum_vtbl = {enum_query, enum_add_ref, enum_release, enum_clone, enum_next,
                                              enum_reset, enum_skip};

HRESULT EnumCreate(REFIID iid, IUnknown **items, ULONG count, void **out)
{
    return enum_new(iid, items, count, 0, out);
}

/* ------------------------------------------------------------------ guids */

typedef struct GUID_ENUM {
    const IEnumGUIDVtbl *Vtbl;
    LONG Refs;
    GUID *Items;
    ULONG Count, Position;
} GUID_ENUM;

static const IEnumGUIDVtbl guid_enum_vtbl;

static HRESULT STDMETHODCALLTYPE guid_query(IEnumGUID *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumGUID)) {
        *object = This;
        InterlockedIncrement(&((GUID_ENUM *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE guid_add_ref(IEnumGUID *This)
{
    return InterlockedIncrement(&((GUID_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE guid_release(IEnumGUID *This)
{
    GUID_ENUM *e = (GUID_ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        CtfFree(e->Items);
        CtfFree(e);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT guid_new(const GUID *items, ULONG count, ULONG position, IEnumGUID **out)
{
    GUID_ENUM *e = CtfAlloc(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Items = count ? CtfAlloc(count * sizeof(GUID)) : nullptr;
    if (count && !e->Items) {
        CtfFree(e);
        return E_OUTOFMEMORY;
    }
    if (count)
        memcpy(e->Items, items, count * sizeof(GUID));
    e->Vtbl = &guid_enum_vtbl;
    e->Refs = 1;
    e->Count = count;
    e->Position = position;
    CtfObjectCreated();
    *out = (IEnumGUID *)e;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE guid_clone(IEnumGUID *This, IEnumGUID **clone)
{
    GUID_ENUM *e = (GUID_ENUM *)This;
    if (!clone)
        return E_INVALIDARG;
    return guid_new(e->Items, e->Count, e->Position, clone);
}

static HRESULT STDMETHODCALLTYPE guid_next(IEnumGUID *This, ULONG count, GUID *items, ULONG *fetched)
{
    GUID_ENUM *e = (GUID_ENUM *)This;
    ULONG n = 0;
    if (!items || (!fetched && count != 1))
        return E_INVALIDARG;
    while (n < count && e->Position < e->Count)
        items[n++] = e->Items[e->Position++];
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE guid_reset(IEnumGUID *This)
{
    ((GUID_ENUM *)This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE guid_skip(IEnumGUID *This, ULONG count)
{
    GUID_ENUM *e = (GUID_ENUM *)This;
    if (count > e->Count - e->Position) {
        e->Position = e->Count;
        return S_FALSE;
    }
    e->Position += count;
    return S_OK;
}

static const IEnumGUIDVtbl guid_enum_vtbl = {guid_query, guid_add_ref, guid_release, guid_next, guid_skip,
                                             guid_reset, guid_clone};

HRESULT EnumGuidsCreate(const GUID *items, ULONG count, IEnumGUID **out)
{
    return guid_new(items, count, 0, out);
}
