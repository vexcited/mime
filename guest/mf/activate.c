/*
 * the activation objects of capture devices: the attributes that name a device, and the media source that
 * ActivateObject makes from them.
 */
#include "camera.h"

typedef struct Activate {
    IMFActivate iface;
    LONG refs;
    IMFAttributes *attributes;
    CRITICAL_SECTION lock;
    IMFMediaSource *source;
} Activate;

#define A(i) (CONTAINING_RECORD((i), Activate, iface)->attributes)

static HRESULT STDMETHODCALLTYPE a_query_interface(IMFActivate *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFAttributes) || IsEqualIID(riid, &IID_IMFActivate)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE a_add_ref(IMFActivate *iface)
{
    return (ULONG)InterlockedIncrement(&CONTAINING_RECORD(iface, Activate, iface)->refs);
}

static ULONG STDMETHODCALLTYPE a_release(IMFActivate *iface)
{
    Activate *a = CONTAINING_RECORD(iface, Activate, iface);
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        if (a->source)
            a->source->lpVtbl->Release(a->source);
        a->attributes->lpVtbl->Release(a->attributes);
        DeleteCriticalSection(&a->lock);
        CamFree(a);
    }
    return (ULONG)refs;
}

/* the attribute methods, handed on to the store the activation object keeps. */
static HRESULT STDMETHODCALLTYPE a_get_item(IMFActivate *i, REFGUID k, PROPVARIANT *v)
{
    return A(i)->lpVtbl->GetItem(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_get_item_type(IMFActivate *i, REFGUID k, MF_ATTRIBUTE_TYPE *t)
{
    return A(i)->lpVtbl->GetItemType(A(i), k, t);
}
static HRESULT STDMETHODCALLTYPE a_compare_item(IMFActivate *i, REFGUID k, const PROPVARIANT *v, BOOL *r)
{
    return A(i)->lpVtbl->CompareItem(A(i), k, v, r);
}
static HRESULT STDMETHODCALLTYPE a_compare(IMFActivate *i, IMFAttributes *o, MF_ATTRIBUTES_MATCH_TYPE t, BOOL *r)
{
    return A(i)->lpVtbl->Compare(A(i), o, t, r);
}
static HRESULT STDMETHODCALLTYPE a_get_uint32(IMFActivate *i, REFGUID k, UINT32 *v)
{
    return A(i)->lpVtbl->GetUINT32(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_get_uint64(IMFActivate *i, REFGUID k, UINT64 *v)
{
    return A(i)->lpVtbl->GetUINT64(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_get_double(IMFActivate *i, REFGUID k, double *v)
{
    return A(i)->lpVtbl->GetDouble(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_get_guid(IMFActivate *i, REFGUID k, GUID *v)
{
    return A(i)->lpVtbl->GetGUID(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_get_string_length(IMFActivate *i, REFGUID k, UINT32 *l)
{
    return A(i)->lpVtbl->GetStringLength(A(i), k, l);
}
static HRESULT STDMETHODCALLTYPE a_get_string(IMFActivate *i, REFGUID k, LPWSTR v, UINT32 size, UINT32 *l)
{
    return A(i)->lpVtbl->GetString(A(i), k, v, size, l);
}
static HRESULT STDMETHODCALLTYPE a_get_allocated_string(IMFActivate *i, REFGUID k, LPWSTR *v, UINT32 *l)
{
    return A(i)->lpVtbl->GetAllocatedString(A(i), k, v, l);
}
static HRESULT STDMETHODCALLTYPE a_get_blob_size(IMFActivate *i, REFGUID k, UINT32 *s)
{
    return A(i)->lpVtbl->GetBlobSize(A(i), k, s);
}
static HRESULT STDMETHODCALLTYPE a_get_blob(IMFActivate *i, REFGUID k, UINT8 *b, UINT32 size, UINT32 *w)
{
    return A(i)->lpVtbl->GetBlob(A(i), k, b, size, w);
}
static HRESULT STDMETHODCALLTYPE a_get_allocated_blob(IMFActivate *i, REFGUID k, UINT8 **b, UINT32 *s)
{
    return A(i)->lpVtbl->GetAllocatedBlob(A(i), k, b, s);
}
static HRESULT STDMETHODCALLTYPE a_get_unknown(IMFActivate *i, REFGUID k, REFIID riid, void **o)
{
    return A(i)->lpVtbl->GetUnknown(A(i), k, riid, o);
}
static HRESULT STDMETHODCALLTYPE a_set_item(IMFActivate *i, REFGUID k, const PROPVARIANT *v)
{
    return A(i)->lpVtbl->SetItem(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_delete_item(IMFActivate *i, REFGUID k)
{
    return A(i)->lpVtbl->DeleteItem(A(i), k);
}
static HRESULT STDMETHODCALLTYPE a_delete_all_items(IMFActivate *i)
{
    return A(i)->lpVtbl->DeleteAllItems(A(i));
}
static HRESULT STDMETHODCALLTYPE a_set_uint32(IMFActivate *i, REFGUID k, UINT32 v)
{
    return A(i)->lpVtbl->SetUINT32(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_set_uint64(IMFActivate *i, REFGUID k, UINT64 v)
{
    return A(i)->lpVtbl->SetUINT64(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_set_double(IMFActivate *i, REFGUID k, double v)
{
    return A(i)->lpVtbl->SetDouble(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_set_guid(IMFActivate *i, REFGUID k, REFGUID v)
{
    return A(i)->lpVtbl->SetGUID(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_set_string(IMFActivate *i, REFGUID k, LPCWSTR v)
{
    return A(i)->lpVtbl->SetString(A(i), k, v);
}
static HRESULT STDMETHODCALLTYPE a_set_blob(IMFActivate *i, REFGUID k, const UINT8 *b, UINT32 s)
{
    return A(i)->lpVtbl->SetBlob(A(i), k, b, s);
}
static HRESULT STDMETHODCALLTYPE a_set_unknown(IMFActivate *i, REFGUID k, IUnknown *o)
{
    return A(i)->lpVtbl->SetUnknown(A(i), k, o);
}
static HRESULT STDMETHODCALLTYPE a_lock_store(IMFActivate *i)
{
    return A(i)->lpVtbl->LockStore(A(i));
}
static HRESULT STDMETHODCALLTYPE a_unlock_store(IMFActivate *i)
{
    return A(i)->lpVtbl->UnlockStore(A(i));
}
static HRESULT STDMETHODCALLTYPE a_get_count(IMFActivate *i, UINT32 *c)
{
    return A(i)->lpVtbl->GetCount(A(i), c);
}
static HRESULT STDMETHODCALLTYPE a_get_item_by_index(IMFActivate *i, UINT32 index, GUID *k, PROPVARIANT *v)
{
    return A(i)->lpVtbl->GetItemByIndex(A(i), index, k, v);
}
static HRESULT STDMETHODCALLTYPE a_copy_all_items(IMFActivate *i, IMFAttributes *dest)
{
    return A(i)->lpVtbl->CopyAllItems(A(i), dest);
}

/* the source is made once, then handed out again: a second activation of the same object gets the same camera. */
static HRESULT STDMETHODCALLTYPE a_activate_object(IMFActivate *iface, REFIID riid, void **out)
{
    Activate *a = CONTAINING_RECORD(iface, Activate, iface);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    EnterCriticalSection(&a->lock);
    HRESULT hr = S_OK;
    if (!a->source)
        hr = CamCreateSource(a->attributes, &a->source);
    if (SUCCEEDED(hr))
        hr = a->source->lpVtbl->QueryInterface(a->source, riid, out);
    LeaveCriticalSection(&a->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_shutdown_object(IMFActivate *iface)
{
    Activate *a = CONTAINING_RECORD(iface, Activate, iface);
    EnterCriticalSection(&a->lock);
    IMFMediaSource *source = a->source;
    a->source = nullptr;
    LeaveCriticalSection(&a->lock);
    if (!source)
        return S_OK;
    HRESULT hr = source->lpVtbl->Shutdown(source);
    source->lpVtbl->Release(source);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_detach_object(IMFActivate *iface)
{
    Activate *a = CONTAINING_RECORD(iface, Activate, iface);
    EnterCriticalSection(&a->lock);
    IMFMediaSource *source = a->source;
    a->source = nullptr;
    LeaveCriticalSection(&a->lock);
    if (source)
        source->lpVtbl->Release(source);
    return S_OK;
}

static const IMFActivateVtbl ACTIVATE_VTBL = {
    a_query_interface, a_add_ref, a_release,
    a_get_item, a_get_item_type, a_compare_item, a_compare, a_get_uint32, a_get_uint64, a_get_double, a_get_guid,
    a_get_string_length, a_get_string, a_get_allocated_string, a_get_blob_size, a_get_blob, a_get_allocated_blob,
    a_get_unknown, a_set_item, a_delete_item, a_delete_all_items, a_set_uint32, a_set_uint64, a_set_double, a_set_guid,
    a_set_string, a_set_blob, a_set_unknown, a_lock_store, a_unlock_store, a_get_count, a_get_item_by_index,
    a_copy_all_items,
    a_activate_object, a_shutdown_object, a_detach_object,
};

/* an activation object over a store of attributes it keeps, the caller's reference to it passed on. */
HRESULT CamCreateActivate(IMFAttributes *attributes, IMFActivate **out)
{
    Activate *a = CamAlloc(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->iface.lpVtbl = &ACTIVATE_VTBL;
    a->refs = 1;
    a->attributes = attributes;
    attributes->lpVtbl->AddRef(attributes);
    InitializeCriticalSection(&a->lock);
    *out = &a->iface;
    return S_OK;
}
