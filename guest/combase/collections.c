/*
 * read only views over a list of items fixed when the view is made, with the iterable and the iterator that every
 * view of the runtime has. the items are runtime objects or runtime strings.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>
#include <mime/win32/winrt_async.h>

typedef struct VECTOR {
    WRT_OBJECT Base;
    CB_VECTOR_KIND Kind;
    UINT32 Count;
    void **Items;
} VECTOR;

typedef struct ITERATOR {
    WRT_OBJECT Base;
    VECTOR *Vector;
    UINT32 Position;
} ITERATOR;

static VECTOR *vector_of(void *iface)
{
    return (VECTOR *)((WRT_IFACE *)iface)->Owner;
}

static ITERATOR *iterator_of(void *iface)
{
    return (ITERATOR *)((WRT_IFACE *)iface)->Owner;
}

/* a string item is copied out, an object item gets a reference. */
static HRESULT item_out(const VECTOR *v, UINT32 index, void **out)
{
    if (v->Kind == CB_VECTOR_STRINGS)
        return WindowsDuplicateString((HSTRING)v->Items[index], (HSTRING *)out);
    IUnknown *object = v->Items[index];
    object->lpVtbl->AddRef(object);
    *out = object;
    return S_OK;
}

/* ------------------------------------------------------------------ the iterator */

WRT_INSPECTABLE_THUNKS(IIteratorAny)

static HRESULT STDMETHODCALLTYPE iterator_get_current(IIteratorAny *This, void **item)
{
    ITERATOR *it = iterator_of(This);
    if (!item)
        return E_POINTER;
    *item = nullptr;
    return it->Position < it->Vector->Count ? item_out(it->Vector, it->Position, item) : E_BOUNDS_;
}

static HRESULT STDMETHODCALLTYPE iterator_get_has_current(IIteratorAny *This, BOOLEAN *value)
{
    ITERATOR *it = iterator_of(This);
    if (!value)
        return E_POINTER;
    *value = it->Position < it->Vector->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE iterator_move_next(IIteratorAny *This, BOOLEAN *value)
{
    ITERATOR *it = iterator_of(This);
    if (!value)
        return E_POINTER;
    if (it->Position < it->Vector->Count)
        it->Position++;
    *value = it->Position < it->Vector->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE iterator_get_many(IIteratorAny *This, UINT32 capacity, void **items, UINT32 *actual)
{
    ITERATOR *it = iterator_of(This);
    if (!actual || (capacity && !items))
        return E_POINTER;
    UINT32 n = 0;
    while (n < capacity && it->Position < it->Vector->Count) {
        HRESULT hr = item_out(it->Vector, it->Position, &items[n]);
        if (FAILED(hr)) {
            *actual = n;
            return hr;
        }
        n++;
        it->Position++;
    }
    *actual = n;
    return S_OK;
}

static const IIteratorAnyVtbl iterator_vtbl = {WRT_INSPECTABLE_ENTRIES(IIteratorAny), iterator_get_current,
                                               iterator_get_has_current, iterator_move_next, iterator_get_many};

static void vector_release(VECTOR *v)
{
    WrtRelease(&v->Base.Ifaces[0]);
}

static void iterator_destroy(WRT_OBJECT *object)
{
    ITERATOR *it = (ITERATOR *)object;
    vector_release(it->Vector);
    CbFree(it);
}

static const WRT_CLASS g_iterator_classes[CB_VECTOR_KINDS] = {
    [CB_VECTOR_PROFILES] = {L"Windows.Foundation.Collections.IIterator`1<Windows.Networking.Connectivity.ConnectionProfile>",
                            1,
                            {&IID_IIteratorConnectionProfile},
                            0,
                            iterator_destroy},
    [CB_VECTOR_STRINGS] = {L"Windows.Foundation.Collections.IIterator`1<String>",
                           1,
                           {&IID_IIteratorString},
                           0,
                           iterator_destroy},
};

/* ------------------------------------------------------------------ the iterable */

WRT_INSPECTABLE_THUNKS(IIterableAny)

static HRESULT STDMETHODCALLTYPE iterable_first(IIterableAny *This, IIteratorAny **iterator)
{
    VECTOR *v = vector_of(This);
    if (!iterator)
        return E_POINTER;
    *iterator = nullptr;
    ITERATOR *it = CbAlloc(sizeof(*it));
    if (!it)
        return E_OUTOFMEMORY;
    memset(it, 0, sizeof(*it));
    const void *vtables[1] = {&iterator_vtbl};
    WrtInit(&it->Base, &g_iterator_classes[v->Kind], vtables);
    WrtAddRef(&v->Base.Ifaces[0]);
    it->Vector = v;
    *iterator = (IIteratorAny *)&it->Base.Ifaces[0];
    return S_OK;
}

static const IIterableAnyVtbl iterable_vtbl = {WRT_INSPECTABLE_ENTRIES(IIterableAny), iterable_first};

/* ------------------------------------------------------------------ the view */

WRT_INSPECTABLE_THUNKS(IVectorViewPtr)

static HRESULT STDMETHODCALLTYPE vec_get_at(IVectorViewPtr *This, UINT32 index, void **item)
{
    VECTOR *v = vector_of(This);
    if (!item)
        return E_POINTER;
    *item = nullptr;
    return index < v->Count ? item_out(v, index, item) : E_BOUNDS_;
}

static HRESULT STDMETHODCALLTYPE vec_size(IVectorViewPtr *This, UINT32 *size)
{
    if (!size)
        return E_POINTER;
    *size = vector_of(This)->Count;
    return S_OK;
}

static BOOL same_item(const VECTOR *v, UINT32 index, void *item)
{
    if (v->Kind == CB_VECTOR_STRINGS) {
        INT32 order;
        return SUCCEEDED(WindowsCompareStringOrdinal((HSTRING)v->Items[index], (HSTRING)item, &order)) && !order;
    }
    return v->Items[index] == item;
}

static HRESULT STDMETHODCALLTYPE vec_index_of(IVectorViewPtr *This, void *item, UINT32 *index, BOOLEAN *found)
{
    VECTOR *v = vector_of(This);
    if (!index || !found)
        return E_POINTER;
    *found = FALSE;
    *index = 0;
    /* an object is told by its identity. */
    IUnknown *identity = nullptr;
    if (v->Kind == CB_VECTOR_PROFILES && item) {
        if (FAILED(((IUnknown *)item)->lpVtbl->QueryInterface((IUnknown *)item, &IID_IUnknown, (void **)&identity)))
            return S_OK;
        item = identity;
    }
    for (UINT32 i = 0; i < v->Count; i++)
        if (same_item(v, i, item)) {
            *index = i;
            *found = TRUE;
            break;
        }
    if (identity)
        identity->lpVtbl->Release(identity);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vec_get_many(IVectorViewPtr *This, UINT32 start, UINT32 capacity, void **items,
                                              UINT32 *actual)
{
    VECTOR *v = vector_of(This);
    if (!actual || (capacity && !items))
        return E_POINTER;
    *actual = 0;
    if (start > v->Count)
        return E_BOUNDS_;
    UINT32 n = 0;
    for (UINT32 i = start; i < v->Count && n < capacity; i++, n++) {
        HRESULT hr = item_out(v, i, &items[n]);
        if (FAILED(hr)) {
            *actual = n;
            return hr;
        }
    }
    *actual = n;
    return S_OK;
}

static const IVectorViewPtrVtbl vec_vtbl = {WRT_INSPECTABLE_ENTRIES(IVectorViewPtr), vec_get_at, vec_size,
                                            vec_index_of, vec_get_many};

static void vector_destroy(WRT_OBJECT *object)
{
    VECTOR *v = (VECTOR *)object;
    for (UINT32 i = 0; i < v->Count; i++) {
        if (v->Kind == CB_VECTOR_STRINGS)
            WindowsDeleteString((HSTRING)v->Items[i]);
        else
            ((IUnknown *)v->Items[i])->lpVtbl->Release((IUnknown *)v->Items[i]);
    }
    CbFree(v->Items);
    CbFree(v);
}

static const WRT_CLASS g_classes[CB_VECTOR_KINDS] = {
    [CB_VECTOR_PROFILES] = {L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.Connectivity.ConnectionProfile>",
                            2,
                            {&IID_IVectorViewConnectionProfile, &IID_IIterableConnectionProfile},
                            0,
                            vector_destroy},
    [CB_VECTOR_STRINGS] = {L"Windows.Foundation.Collections.IVectorView`1<String>",
                           2,
                           {&IID_IVectorViewString, &IID_IIterableString},
                           0,
                           vector_destroy},
};

HRESULT CbNewVectorView(CB_VECTOR_KIND kind, void **items, UINT32 count, void **view)
{
    *view = nullptr;
    VECTOR *v = CbAlloc(sizeof(*v));
    if (!v)
        return E_OUTOFMEMORY;
    memset(v, 0, sizeof(*v));
    const void *vtables[2] = {&vec_vtbl, &iterable_vtbl};
    WrtInit(&v->Base, &g_classes[kind], vtables);
    v->Kind = kind;
    v->Count = count;
    v->Items = items;
    *view = &v->Base.Ifaces[0];
    return S_OK;
}
