/*
 * the attribute store behind every attribute object: typed items keyed by
 * guid, their comparison, and MFCreateAttributes.
 */
#include "mfplat_private.h"

static const GUID *const ATTRIBUTES_IIDS[] = {nullptr};
static const MfClass ATTRIBUTES_CLASS = {ATTRIBUTES_IIDS, nullptr};

/* ------------------------------------------------------------------ objects */

HRESULT MfObjectInit(MfObject *object, const void *vtbl, const MfClass *cls, UINT32 size)
{
    object->vtbl = vtbl;
    object->refs = 1;
    object->cls = cls;
    MfStoreInit(&object->store);
    if (size) {
        object->store.items = MfAllocZero((SIZE_T)size * sizeof(MfItem));
        if (!object->store.items)
            return E_OUTOFMEMORY;
        object->store.capacity = size;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfObjectQueryInterface(IMFAttributes *iface, REFIID riid, void **out)
{
    MfObject *object = mf_object(iface);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes))
        goto found;
    for (const GUID *const *iid = object->cls->iids; *iid; iid++) {
        if (IsEqualGUID(riid, *iid))
            goto found;
    }
    *out = nullptr;
    return E_NOINTERFACE;
found:
    *out = iface;
    InterlockedIncrement(&object->refs);
    return S_OK;
}

ULONG STDMETHODCALLTYPE MfObjectAddRef(IMFAttributes *iface)
{
    return InterlockedIncrement(&mf_object(iface)->refs);
}

ULONG STDMETHODCALLTYPE MfObjectRelease(IMFAttributes *iface)
{
    MfObject *object = mf_object(iface);
    ULONG refs = InterlockedDecrement(&object->refs);
    if (!refs) {
        if (object->cls->destroy)
            object->cls->destroy(object);
        MfStoreFree(&object->store);
        MfFree(object);
    }
    return refs;
}

/* ------------------------------------------------------------------ the store */

void MfStoreInit(MfStore *store)
{
    InitializeCriticalSection(&store->lock);
    store->items = nullptr;
    store->count = store->capacity = 0;
}

void MfStoreClear(MfStore *store)
{
    for (UINT32 i = 0; i < store->count; i++)
        PropVariantClear(&store->items[i].value);
    store->count = 0;
}

void MfStoreFree(MfStore *store)
{
    MfStoreClear(store);
    MfFree(store->items);
    DeleteCriticalSection(&store->lock);
}

static int find_index(MfStore *store, REFGUID key)
{
    for (UINT32 i = 0; i < store->count; i++) {
        if (store->items[i].key.Data1 > key->Data1)
            break;
        if (IsEqualGUID(&store->items[i].key, key))
            return (int)i;
    }
    return -1;
}

const PROPVARIANT *MfStoreFind(MfStore *store, REFGUID key)
{
    int index = find_index(store, key);
    return index < 0 ? nullptr : &store->items[index].value;
}

/* clears the value of key, or inserts a new item before the first one whose key does not sort lower. */
static PROPVARIANT *item_for_write(MfStore *store, REFGUID key)
{
    int index = find_index(store, key);
    if (index >= 0) {
        PropVariantClear(&store->items[index].value);
        return &store->items[index].value;
    }
    if (store->count == store->capacity) {
        UINT32 capacity = store->capacity * 2 + 4;
        MfItem *items = MfAlloc((SIZE_T)capacity * sizeof(MfItem));
        if (!items)
            return nullptr;
        if (store->count)
            memcpy(items, store->items, store->count * sizeof(MfItem));
        MfFree(store->items);
        store->items = items;
        store->capacity = capacity;
    }
    UINT32 at = 0;
    while (at < store->count && store->items[at].key.Data1 < key->Data1)
        at++;
    memmove(&store->items[at + 1], &store->items[at], (store->count - at) * sizeof(MfItem));
    store->items[at].key = *key;
    memset(&store->items[at].value, 0, sizeof(PROPVARIANT));
    store->count++;
    return &store->items[at].value;
}

static void delete_item(MfStore *store, REFGUID key)
{
    int index = find_index(store, key);
    if (index < 0)
        return;
    PropVariantClear(&store->items[index].value);
    memmove(&store->items[index], &store->items[index + 1], (store->count - index - 1) * sizeof(MfItem));
    store->count--;
}

static BOOL values_equal(const PROPVARIANT *a, const PROPVARIANT *b)
{
    if (a->vt != b->vt)
        return FALSE;
    switch (a->vt) {
    case VT_R8:
        return a->dblVal == b->dblVal;
    case VT_UI4:
        return a->ulVal == b->ulVal;
    case VT_UI8:
        return a->uhVal.QuadPart == b->uhVal.QuadPart;
    case VT_LPWSTR:
        return !wcscmp(a->pwszVal, b->pwszVal);
    case VT_CLSID:
        return IsEqualGUID(a->puuid, b->puuid);
    case VT_VECTOR | VT_UI1:
        return a->caub.cElems == b->caub.cElems && !memcmp(a->caub.pElems, b->caub.pElems, a->caub.cElems);
    default:
        /* interfaces and the other types compare equal once their types match. */
        return TRUE;
    }
}

static BOOL skipped(REFGUID key, const GUID *skip, UINT32 skip_count)
{
    for (UINT32 i = 0; i < skip_count; i++) {
        if (IsEqualGUID(key, &skip[i]))
            return TRUE;
    }
    return FALSE;
}

/*
 * the comparison behind Compare and IsEqual's format data, which leaves out
 * some keys. the other store is locked first and compares our values with
 * its CompareItem.
 */
HRESULT MfStoreCompare(MfStore *ours, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type, const GUID *skip,
                       UINT32 skip_count, BOOL *result)
{
    HRESULT hr = theirs->lpVtbl->LockStore(theirs);
    EnterCriticalSection(&ours->lock);
    if (FAILED(hr))
        goto done;
    if (type == MF_ATTRIBUTES_MATCH_SMALLER) {
        UINT32 count = 0;
        hr = theirs->lpVtbl->GetCount(theirs, &count);
        if (FAILED(hr))
            goto unlock;
        type = count >= ours->count ? MF_ATTRIBUTES_MATCH_OUR_ITEMS : MF_ATTRIBUTES_MATCH_THEIR_ITEMS;
    }
    BOOL same = FALSE;
    switch (type) {
    case MF_ATTRIBUTES_MATCH_OUR_ITEMS:
    case MF_ATTRIBUTES_MATCH_ALL_ITEMS:
        for (UINT32 i = 0; i < ours->count; i++) {
            if (skipped(&ours->items[i].key, skip, skip_count))
                continue;
            BOOL equal = FALSE;
            hr = theirs->lpVtbl->CompareItem(theirs, &ours->items[i].key, &ours->items[i].value, &equal);
            if (FAILED(hr))
                goto unlock;
            if (!equal)
                goto differ;
        }
        if (type == MF_ATTRIBUTES_MATCH_ALL_ITEMS) {
            UINT32 count = 0;
            hr = theirs->lpVtbl->GetCount(theirs, &count);
            if (FAILED(hr))
                goto unlock;
            if (count != ours->count)
                goto differ;
        }
        break;
    case MF_ATTRIBUTES_MATCH_THEIR_ITEMS: {
        UINT32 count = 0;
        hr = theirs->lpVtbl->GetCount(theirs, &count);
        if (FAILED(hr))
            goto unlock;
        for (UINT32 i = 0; i < count; i++) {
            GUID key;
            hr = theirs->lpVtbl->GetItemByIndex(theirs, i, &key, nullptr);
            if (FAILED(hr))
                goto unlock;
            if (skipped(&key, skip, skip_count))
                continue;
            const PROPVARIANT *value = MfStoreFind(ours, &key);
            if (!value)
                goto differ;
            BOOL equal = FALSE;
            hr = theirs->lpVtbl->CompareItem(theirs, &key, value, &equal);
            if (FAILED(hr))
                goto unlock;
            if (!equal)
                goto differ;
        }
        break;
    }
    case MF_ATTRIBUTES_MATCH_INTERSECTION:
        for (UINT32 i = 0; i < ours->count; i++) {
            if (skipped(&ours->items[i].key, skip, skip_count))
                continue;
            if (FAILED(theirs->lpVtbl->GetItem(theirs, &ours->items[i].key, nullptr)))
                continue;
            BOOL equal = FALSE;
            hr = theirs->lpVtbl->CompareItem(theirs, &ours->items[i].key, &ours->items[i].value, &equal);
            if (FAILED(hr))
                goto unlock;
            if (!equal)
                goto differ;
        }
        break;
    default:
        hr = E_INVALIDARG;
        goto unlock;
    }
    same = TRUE;
differ:
    *result = same;
unlock:
    theirs->lpVtbl->UnlockStore(theirs);
done:
    LeaveCriticalSection(&ours->lock);
    return hr;
}

/* ------------------------------------------------------------------ IMFAttributes */

static MfStore *store_of(IMFAttributes *iface)
{
    return &mf_object(iface)->store;
}

static HRESULT typed(MfStore *store, REFGUID key, VARTYPE vt, const PROPVARIANT **out)
{
    const PROPVARIANT *value = MfStoreFind(store, key);
    if (!value)
        return MF_E_ATTRIBUTENOTFOUND;
    if (value->vt != vt)
        return MF_E_INVALIDTYPE;
    *out = value;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrGetItem(IMFAttributes *iface, REFGUID key, PROPVARIANT *value)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    const PROPVARIANT *item = MfStoreFind(store, key);
    HRESULT hr = item ? (value ? PropVariantCopy(value, item) : S_OK) : MF_E_ATTRIBUTENOTFOUND;
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetItemType(IMFAttributes *iface, REFGUID key, MF_ATTRIBUTE_TYPE *type)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    const PROPVARIANT *item = MfStoreFind(store, key);
    if (item)
        *type = (MF_ATTRIBUTE_TYPE)item->vt;
    LeaveCriticalSection(&store->lock);
    return item ? S_OK : MF_E_ATTRIBUTENOTFOUND;
}

HRESULT STDMETHODCALLTYPE MfAttrCompareItem(IMFAttributes *iface, REFGUID key, const PROPVARIANT *value,
                                                   BOOL *result)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    const PROPVARIANT *item = MfStoreFind(store, key);
    *result = item && values_equal(item, value);
    LeaveCriticalSection(&store->lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrCompare(IMFAttributes *iface, IMFAttributes *theirs,
                                              MF_ATTRIBUTES_MATCH_TYPE type, BOOL *result)
{
    return MfStoreCompare(store_of(iface), theirs, type, nullptr, 0, result);
}

HRESULT STDMETHODCALLTYPE MfAttrGetUint32(IMFAttributes *iface, REFGUID key, UINT32 *value)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_UI4, &item);
    if (SUCCEEDED(hr))
        *value = item->ulVal;
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetUint64(IMFAttributes *iface, REFGUID key, UINT64 *value)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_UI8, &item);
    if (SUCCEEDED(hr))
        *value = item->uhVal.QuadPart;
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetDouble(IMFAttributes *iface, REFGUID key, double *value)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_R8, &item);
    if (SUCCEEDED(hr))
        *value = item->dblVal;
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetGuid(IMFAttributes *iface, REFGUID key, GUID *value)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_CLSID, &item);
    if (SUCCEEDED(hr))
        *value = *item->puuid;
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetStringLength(IMFAttributes *iface, REFGUID key, UINT32 *length)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_LPWSTR, &item);
    if (SUCCEEDED(hr))
        *length = (UINT32)wcslen(item->pwszVal);
    LeaveCriticalSection(&store->lock);
    return hr;
}

/* like windows, the length is written before the size check. */
HRESULT STDMETHODCALLTYPE MfAttrGetString(IMFAttributes *iface, REFGUID key, LPWSTR value, UINT32 size,
                                                 UINT32 *length)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_LPWSTR, &item);
    if (SUCCEEDED(hr)) {
        SIZE_T chars = wcslen(item->pwszVal);
        if (chars >= 0x7FFFFFFE) {
            hr = E_OUTOFMEMORY;
        } else {
            if (length)
                *length = (UINT32)chars;
            if (chars + 1 > size)
                hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            else
                memcpy(value, item->pwszVal, (chars + 1) * sizeof(WCHAR));
        }
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetAllocatedString(IMFAttributes *iface, REFGUID key, LPWSTR *value,
                                                           UINT32 *length)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_LPWSTR, &item);
    if (SUCCEEDED(hr)) {
        SIZE_T chars = wcslen(item->pwszVal);
        if (length)
            *length = (UINT32)chars;
        *value = CoTaskMemAlloc((chars + 1) * sizeof(WCHAR));
        if (*value)
            memcpy(*value, item->pwszVal, (chars + 1) * sizeof(WCHAR));
        else
            hr = E_OUTOFMEMORY;
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetBlobSize(IMFAttributes *iface, REFGUID key, UINT32 *size)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_VECTOR | VT_UI1, &item);
    if (SUCCEEDED(hr))
        *size = item->caub.cElems;
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetBlob(IMFAttributes *iface, REFGUID key, UINT8 *buffer, UINT32 size,
                                               UINT32 *written)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_VECTOR | VT_UI1, &item);
    if (SUCCEEDED(hr)) {
        if (written)
            *written = item->caub.cElems;
        if (item->caub.cElems > size)
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        else if (item->caub.cElems)
            memcpy(buffer, item->caub.pElems, item->caub.cElems);
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetAllocatedBlob(IMFAttributes *iface, REFGUID key, UINT8 **buffer,
                                                         UINT32 *size)
{
    MfStore *store = store_of(iface);
    const PROPVARIANT *item;
    EnterCriticalSection(&store->lock);
    HRESULT hr = typed(store, key, VT_VECTOR | VT_UI1, &item);
    if (SUCCEEDED(hr)) {
        if (size)
            *size = item->caub.cElems;
        *buffer = CoTaskMemAlloc(item->caub.cElems ? item->caub.cElems : 1);
        if (*buffer)
            memcpy(*buffer, item->caub.pElems, item->caub.cElems);
        else
            hr = E_OUTOFMEMORY;
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrGetUnknown(IMFAttributes *iface, REFGUID key, REFIID riid, void **object)
{
    MfStore *store = store_of(iface);
    *object = nullptr;
    EnterCriticalSection(&store->lock);
    const PROPVARIANT *item = MfStoreFind(store, key);
    HRESULT hr;
    if (!item)
        hr = MF_E_ATTRIBUTENOTFOUND;
    else if (item->vt != VT_UNKNOWN || !item->punkVal)
        hr = MF_E_INVALIDTYPE;
    else
        hr = item->punkVal->lpVtbl->QueryInterface(item->punkVal, riid, object);
    LeaveCriticalSection(&store->lock);
    return hr;
}

/* like windows, a value of another type still leaves the key, empty. */
HRESULT STDMETHODCALLTYPE MfAttrSetItem(IMFAttributes *iface, REFGUID key, const PROPVARIANT *value)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    HRESULT hr;
    switch (value->vt) {
    case VT_UI4:
    case VT_UI8:
    case VT_R8:
    case VT_CLSID:
    case VT_LPWSTR:
    case VT_UNKNOWN:
    case VT_VECTOR | VT_UI1:
    case VT_VECTOR | VT_LPWSTR:
        hr = item ? PropVariantCopy(item, value) : E_OUTOFMEMORY;
        break;
    default:
        hr = MF_E_INVALIDTYPE;
        break;
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrDeleteItem(IMFAttributes *iface, REFGUID key)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    delete_item(store, key);
    LeaveCriticalSection(&store->lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrDeleteAllItems(IMFAttributes *iface)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    MfStoreClear(store);
    LeaveCriticalSection(&store->lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrSetUint32(IMFAttributes *iface, REFGUID key, UINT32 value)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    if (item) {
        item->vt = VT_UI4;
        item->ulVal = value;
    }
    LeaveCriticalSection(&store->lock);
    return item ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE MfAttrSetUint64(IMFAttributes *iface, REFGUID key, UINT64 value)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    if (item) {
        item->vt = VT_UI8;
        item->uhVal.QuadPart = value;
    }
    LeaveCriticalSection(&store->lock);
    return item ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE MfAttrSetDouble(IMFAttributes *iface, REFGUID key, double value)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    if (item) {
        item->vt = VT_R8;
        item->dblVal = value;
    }
    LeaveCriticalSection(&store->lock);
    return item ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE MfAttrSetGuid(IMFAttributes *iface, REFGUID key, REFGUID value)
{
    MfStore *store = store_of(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    GUID *copy = item ? CoTaskMemAlloc(sizeof(GUID)) : nullptr;
    if (copy) {
        *copy = *value;
        item->vt = VT_CLSID;
        item->puuid = copy;
    } else {
        if (item)
            delete_item(store, key);
        hr = E_OUTOFMEMORY;
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

/* a null string still leaves the key, empty. */
HRESULT STDMETHODCALLTYPE MfAttrSetString(IMFAttributes *iface, REFGUID key, LPCWSTR value)
{
    MfStore *store = store_of(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    if (!item) {
        hr = E_OUTOFMEMORY;
    } else if (!value) {
        hr = E_INVALIDARG;
    } else {
        SIZE_T bytes = (wcslen(value) + 1) * sizeof(WCHAR);
        WCHAR *copy = CoTaskMemAlloc(bytes);
        if (copy) {
            memcpy(copy, value, bytes);
            item->vt = VT_LPWSTR;
            item->pwszVal = copy;
        } else {
            delete_item(store, key);
            hr = E_OUTOFMEMORY;
        }
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrSetBlob(IMFAttributes *iface, REFGUID key, const UINT8 *buffer, UINT32 size)
{
    MfStore *store = store_of(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    UINT8 *copy = item ? CoTaskMemAlloc(size ? size : 1) : nullptr;
    if (copy) {
        if (size)
            memcpy(copy, buffer, size);
        item->vt = VT_VECTOR | VT_UI1;
        item->caub.cElems = size;
        item->caub.pElems = copy;
    } else {
        if (item)
            delete_item(store, key);
        hr = E_OUTOFMEMORY;
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrSetUnknown(IMFAttributes *iface, REFGUID key, IUnknown *object)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    PROPVARIANT *item = item_for_write(store, key);
    if (item) {
        item->vt = VT_UNKNOWN;
        item->punkVal = object;
        if (object)
            object->lpVtbl->AddRef(object);
    }
    LeaveCriticalSection(&store->lock);
    return item ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE MfAttrLockStore(IMFAttributes *iface)
{
    EnterCriticalSection(&store_of(iface)->lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrUnlockStore(IMFAttributes *iface)
{
    LeaveCriticalSection(&store_of(iface)->lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrGetCount(IMFAttributes *iface, UINT32 *count)
{
    *count = store_of(iface)->count;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MfAttrGetItemByIndex(IMFAttributes *iface, UINT32 index, GUID *key,
                                                        PROPVARIANT *value)
{
    MfStore *store = store_of(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&store->lock);
    if (index >= store->count) {
        hr = E_INVALIDARG;
    } else {
        *key = store->items[index].key;
        if (value)
            hr = PropVariantCopy(value, &store->items[index].value);
    }
    LeaveCriticalSection(&store->lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE MfAttrCopyAllItems(IMFAttributes *iface, IMFAttributes *dest)
{
    MfStore *store = store_of(iface);
    EnterCriticalSection(&store->lock);
    HRESULT hr = dest->lpVtbl->DeleteAllItems(dest);
    for (UINT32 i = 0; SUCCEEDED(hr) && i < store->count; i++)
        hr = dest->lpVtbl->SetItem(dest, &store->items[i].key, &store->items[i].value);
    LeaveCriticalSection(&store->lock);
    return hr;
}

const IMFAttributesVtbl MfAttributesVtbl = MF_ATTRIBUTES_VTBL;

HRESULT WINAPI MFCreateAttributes(IMFAttributes **attributes, UINT32 size)
{
    if (!attributes)
        return E_POINTER;
    MfObject *object = MfAlloc(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    if (FAILED(MfObjectInit(object, &MfAttributesVtbl, &ATTRIBUTES_CLASS, size))) {
        MfStoreFree(&object->store);
        MfFree(object);
        return E_OUTOFMEMORY;
    }
    *attributes = (IMFAttributes *)object;
    return S_OK;
}
