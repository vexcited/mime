/*
 * in-memory metadata handlers, block readers and writers, and enumerators. items contain schema, id and value;
 * handler-valued items represent nested blocks.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"

/* property variants. */

void WicPvInit(PROPVARIANT *pv) { memset(pv, 0, sizeof(*pv)); }

BOOL WicPvIsEmpty(const PROPVARIANT *pv) { return !pv || pv->vt == VT_EMPTY; }

/* ids compare by type and value, with exact string matching. */
BOOL WicPvSame(const PROPVARIANT *a, const PROPVARIANT *b)
{
    /* ANSI and wide names match by character value. */
    if ((a->vt == VT_LPSTR && b->vt == VT_LPWSTR) || (a->vt == VT_LPWSTR && b->vt == VT_LPSTR)) {
        const PROPVARIANT *n = a->vt == VT_LPSTR ? a : b, *w = a->vt == VT_LPSTR ? b : a;
        if (!n->pszVal || !w->pwszVal)
            return FALSE;
        const char *c = n->pszVal;
        const WCHAR *d = w->pwszVal;
        while (*c && (BYTE)*c == *d) {
            c++;
            d++;
        }
        return (BYTE)*c == *d;
    }
    if (a->vt != b->vt)
        return FALSE;
    switch (a->vt) {
    case VT_EMPTY:
        return TRUE;
    case VT_LPWSTR:
        return a->pwszVal && b->pwszVal && !wcscmp(a->pwszVal, b->pwszVal);
    case VT_LPSTR:
        return a->pszVal && b->pszVal && !strcmp(a->pszVal, b->pszVal);
    case VT_UI1:
    case VT_I1:
        return a->bVal == b->bVal;
    case VT_UI2:
    case VT_I2:
        return a->uiVal == b->uiVal;
    case VT_UI4:
    case VT_I4:
    case VT_R4:
    case VT_UINT:
    case VT_INT:
        return a->ulVal == b->ulVal;
    case VT_UI8:
    case VT_I8:
    case VT_R8:
        return a->uhVal.QuadPart == b->uhVal.QuadPart;
    case VT_CLSID:
        return a->puuid && b->puuid && IsEqualGUID(a->puuid, b->puuid);
    case VT_BOOL:
        return a->boolVal == b->boolVal;
    default:
        return FALSE;
    }
}

/* the handler. */

typedef struct WicMetaHandler {
    WicBase base;
    WicSlot reader;
    WicSlot writer;
    CRITICAL_SECTION lock;
    GUID format;
    WicMetaItem *items;
    UINT count;
    UINT capacity;
    BOOL writable;
    /* format-specific serialization state retained for codecs. */
    void *user;
} WicMetaHandler;

static WicMetaHandler *handler_from_writer(void *This) { return WIC_FROM_SLOT(WicMetaHandler, writer, This); }

static WicMetaHandler *handler_of(void *This) { return (WicMetaHandler *)WicSlotBase(This); }

static void item_clear(WicMetaItem *item)
{
    PropVariantClear(&item->schema);
    PropVariantClear(&item->id);
    PropVariantClear(&item->value);
}

static void handler_destroy(WicBase *base)
{
    WicMetaHandler *h = (WicMetaHandler *)base;
    for (UINT i = 0; i < h->count; i++)
        item_clear(&h->items[i]);
    WicFree(h->items);
    DeleteCriticalSection(&h->lock);
}

static HRESULT STDMETHODCALLTYPE mh_get_format(void *This, GUID *format)
{
    if (!format)
        return E_INVALIDARG;
    *format = handler_of(This)->format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mh_get_info(void *This, IWICMetadataHandlerInfo **info)
{
    if (!info)
        return E_INVALIDARG;
    return WicMetaHandlerInfoCreate(&handler_of(This)->format, handler_of(This)->writable, info);
}

static HRESULT STDMETHODCALLTYPE mh_get_count(void *This, UINT *count)
{
    WicMetaHandler *h = handler_of(This);
    if (!count)
        return E_INVALIDARG;
    *count = h->count;
    return S_OK;
}

static HRESULT copy_out(const PROPVARIANT *src, PROPVARIANT *dst)
{
    if (!dst)
        return S_OK;
    return PropVariantCopy(dst, src);
}

static HRESULT STDMETHODCALLTYPE mh_get_by_index(void *This, UINT index, PROPVARIANT *schema, PROPVARIANT *id,
                                                 PROPVARIANT *value)
{
    WicMetaHandler *h = handler_of(This);
    EnterCriticalSection(&h->lock);
    HRESULT hr = S_OK;
    if (index >= h->count) {
        hr = E_INVALIDARG;
    } else {
        WicMetaItem *item = &h->items[index];
        if (schema)
            WicPvInit(schema);
        if (id)
            WicPvInit(id);
        if (value)
            WicPvInit(value);
        hr = copy_out(&item->schema, schema);
        if (SUCCEEDED(hr))
            hr = copy_out(&item->id, id);
        if (SUCCEEDED(hr))
            hr = copy_out(&item->value, value);
        if (FAILED(hr)) {
            if (schema)
                PropVariantClear(schema);
            if (id)
                PropVariantClear(id);
            if (value)
                PropVariantClear(value);
        }
    }
    LeaveCriticalSection(&h->lock);
    return hr;
}

static int find_item(WicMetaHandler *h, const PROPVARIANT *schema, const PROPVARIANT *id)
{
    for (UINT i = 0; i < h->count; i++) {
        WicMetaItem *item = &h->items[i];
        if (id && !WicPvIsEmpty(id) && !WicPvSame(&item->id, id))
            continue;
        if (schema && !WicPvIsEmpty(schema) && !WicPvSame(&item->schema, schema))
            continue;
        if ((!id || WicPvIsEmpty(id)) && (!schema || WicPvIsEmpty(schema)))
            continue;
        return (int)i;
    }
    return -1;
}

static HRESULT STDMETHODCALLTYPE mh_get_value(void *This, const PROPVARIANT *schema, const PROPVARIANT *id,
                                              PROPVARIANT *value)
{
    WicMetaHandler *h = handler_of(This);
    if (!id && !schema)
        return E_INVALIDARG;
    EnterCriticalSection(&h->lock);
    int k = find_item(h, schema, id);
    HRESULT hr;
    if (k < 0) {
        hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    } else {
        if (value)
            WicPvInit(value);
        hr = copy_out(&h->items[k].value, value);
    }
    LeaveCriticalSection(&h->lock);
    return hr;
}

/* enumerate an item snapshot. */
typedef struct WicItemEnum {
    WicBase base;
    WicSlot slot;
    WicMetaItem *items;
    UINT count;
    UINT position;
} WicItemEnum;

static WicItemEnum *ienum_from(void *This) { return WIC_FROM_SLOT(WicItemEnum, slot, This); }

static void ienum_destroy(WicBase *base)
{
    WicItemEnum *e = (WicItemEnum *)base;
    for (UINT i = 0; i < e->count; i++)
        item_clear(&e->items[i]);
    WicFree(e->items);
}

static HRESULT STDMETHODCALLTYPE ienum_next(IWICEnumMetadataItem *This, ULONG count, PROPVARIANT *schemas,
                                            PROPVARIANT *ids, PROPVARIANT *values, ULONG *fetched)
{
    WicItemEnum *e = ienum_from(This);
    ULONG got = 0;
    if (!count && !fetched)
        return E_INVALIDARG;
    if (count > 1 && !fetched)
        return E_INVALIDARG;
    while (got < count && e->position < e->count) {
        WicMetaItem *item = &e->items[e->position];
        HRESULT hr = S_OK;
        if (schemas) {
            WicPvInit(&schemas[got]);
            hr = copy_out(&item->schema, &schemas[got]);
        }
        if (SUCCEEDED(hr) && ids) {
            WicPvInit(&ids[got]);
            hr = copy_out(&item->id, &ids[got]);
        }
        if (SUCCEEDED(hr) && values) {
            WicPvInit(&values[got]);
            hr = copy_out(&item->value, &values[got]);
        }
        if (FAILED(hr))
            return hr;
        e->position++;
        got++;
    }
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ienum_skip(IWICEnumMetadataItem *This, ULONG count)
{
    WicItemEnum *e = ienum_from(This);
    if (e->position + count > e->count) {
        e->position = e->count;
        return S_FALSE;
    }
    e->position += count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ienum_reset(IWICEnumMetadataItem *This)
{
    ienum_from(This)->position = 0;
    return S_OK;
}

static HRESULT ienum_create(const WicMetaItem *items, UINT count, UINT position, IWICEnumMetadataItem **out);

static HRESULT STDMETHODCALLTYPE ienum_clone(IWICEnumMetadataItem *This, IWICEnumMetadataItem **out)
{
    WicItemEnum *e = ienum_from(This);
    if (!out)
        return E_INVALIDARG;
    return ienum_create(e->items, e->count, e->position, out);
}

static const IWICEnumMetadataItemVtbl g_ienum_vtbl = {WIC_IUNKNOWN_FUNCS, ienum_next, ienum_skip, ienum_reset,
                                                      ienum_clone};
static const WicIfaceEntry g_ienum_ifaces[] = {WIC_ENTRY(WicItemEnum, IID_IWICEnumMetadataItem, slot)};
static const WicClass g_ienum_class = {ienum_destroy, g_ienum_ifaces, 1};

static HRESULT ienum_create(const WicMetaItem *items, UINT count, UINT position, IWICEnumMetadataItem **out)
{
    WicItemEnum *e = WicNew(sizeof(*e), &g_ienum_class);
    if (!e)
        return E_OUTOFMEMORY;
    WIC_SLOT(e, slot, &g_ienum_vtbl);
    e->items = WicAllocZero(((SIZE_T)count + 1) * sizeof(WicMetaItem));
    if (!e->items) {
        WicRelease(&e->slot);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++) {
        HRESULT hr = PropVariantCopy(&e->items[i].schema, &items[i].schema);
        if (SUCCEEDED(hr))
            hr = PropVariantCopy(&e->items[i].id, &items[i].id);
        if (SUCCEEDED(hr))
            hr = PropVariantCopy(&e->items[i].value, &items[i].value);
        e->count = i + 1;
        if (FAILED(hr)) {
            WicRelease(&e->slot);
            return hr;
        }
    }
    e->position = position;
    *out = (IWICEnumMetadataItem *)&e->slot;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mh_get_enumerator(void *This, IWICEnumMetadataItem **out)
{
    WicMetaHandler *h = handler_of(This);
    if (!out)
        return E_INVALIDARG;
    EnterCriticalSection(&h->lock);
    HRESULT hr = ienum_create(h->items, h->count, 0, out);
    LeaveCriticalSection(&h->lock);
    return hr;
}

/* the writer methods. */

static HRESULT grow(WicMetaHandler *h)
{
    if (h->count < h->capacity)
        return S_OK;
    UINT capacity = h->capacity ? h->capacity * 2 : 8;
    WicMetaItem *bigger = WicRealloc(h->items, (SIZE_T)capacity * sizeof(WicMetaItem));
    if (!bigger)
        return E_OUTOFMEMORY;
    h->items = bigger;
    h->capacity = capacity;
    return S_OK;
}

static HRESULT store_item(WicMetaItem *item, const PROPVARIANT *schema, const PROPVARIANT *id,
                          const PROPVARIANT *value)
{
    WicMetaItem fresh;
    memset(&fresh, 0, sizeof(fresh));
    HRESULT hr = S_OK;
    if (schema && !WicPvIsEmpty(schema))
        hr = PropVariantCopy(&fresh.schema, schema);
    if (SUCCEEDED(hr) && id && !WicPvIsEmpty(id))
        hr = PropVariantCopy(&fresh.id, id);
    if (SUCCEEDED(hr) && value)
        hr = PropVariantCopy(&fresh.value, value);
    if (FAILED(hr)) {
        item_clear(&fresh);
        return hr;
    }
    *item = fresh;
    return S_OK;
}

static HRESULT set_value(WicMetaHandler *h, const PROPVARIANT *schema, const PROPVARIANT *id, const PROPVARIANT *value)
{
    HRESULT hr;
    EnterCriticalSection(&h->lock);
    int k = find_item(h, schema, id);
    if (k >= 0) {
        WicMetaItem old = h->items[k];
        WicMetaItem fresh;
        hr = store_item(&fresh, schema, id, value);
        if (SUCCEEDED(hr)) {
            /* omitted schema or id fields retain their previous values. */
            if (WicPvIsEmpty(&fresh.id) && !WicPvIsEmpty(&old.id))
                PropVariantCopy(&fresh.id, &old.id);
            if (WicPvIsEmpty(&fresh.schema) && !WicPvIsEmpty(&old.schema))
                PropVariantCopy(&fresh.schema, &old.schema);
            h->items[k] = fresh;
            item_clear(&old);
        }
    } else {
        hr = grow(h);
        if (SUCCEEDED(hr)) {
            hr = store_item(&h->items[h->count], schema, id, value);
            if (SUCCEEDED(hr))
                h->count++;
        }
    }
    LeaveCriticalSection(&h->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mw_set_value(void *This, const PROPVARIANT *schema, const PROPVARIANT *id,
                                              const PROPVARIANT *value)
{
    WicMetaHandler *h = handler_of(This);
    if (!value || (!id && !schema))
        return E_INVALIDARG;
    return set_value(h, schema, id, value);
}

static HRESULT STDMETHODCALLTYPE mw_set_by_index(void *This, UINT index, const PROPVARIANT *schema,
                                                 const PROPVARIANT *id, const PROPVARIANT *value)
{
    WicMetaHandler *h = handler_of(This);
    if (!value)
        return E_INVALIDARG;
    EnterCriticalSection(&h->lock);
    HRESULT hr;
    if (index >= h->count) {
        hr = E_INVALIDARG;
    } else {
        WicMetaItem fresh, old = h->items[index];
        hr = store_item(&fresh, schema, id, value);
        if (SUCCEEDED(hr)) {
            h->items[index] = fresh;
            item_clear(&old);
        }
    }
    LeaveCriticalSection(&h->lock);
    return hr;
}

static void remove_at(WicMetaHandler *h, UINT index)
{
    item_clear(&h->items[index]);
    memmove(&h->items[index], &h->items[index + 1], (SIZE_T)(h->count - index - 1) * sizeof(WicMetaItem));
    h->count--;
}

static HRESULT STDMETHODCALLTYPE mw_remove_value(void *This, const PROPVARIANT *schema, const PROPVARIANT *id)
{
    WicMetaHandler *h = handler_of(This);
    if (!id && !schema)
        return E_INVALIDARG;
    EnterCriticalSection(&h->lock);
    int k = find_item(h, schema, id);
    HRESULT hr = S_OK;
    if (k < 0)
        hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    else
        remove_at(h, (UINT)k);
    LeaveCriticalSection(&h->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mw_remove_by_index(void *This, UINT index)
{
    WicMetaHandler *h = handler_of(This);
    EnterCriticalSection(&h->lock);
    HRESULT hr = S_OK;
    if (index >= h->count)
        hr = E_INVALIDARG;
    else
        remove_at(h, index);
    LeaveCriticalSection(&h->lock);
    return hr;
}

#define READER_FUNCS                                                                                                   \
    VTBL_FN(mh_get_format), VTBL_FN(mh_get_info), VTBL_FN(mh_get_count), VTBL_FN(mh_get_by_index),                     \
        VTBL_FN(mh_get_value), VTBL_FN(mh_get_enumerator)

static const IWICMetadataReaderVtbl g_reader_vtbl = {WIC_IUNKNOWN_FUNCS, READER_FUNCS};
static const IWICMetadataWriterVtbl g_writer_vtbl = {
    WIC_IUNKNOWN_FUNCS, READER_FUNCS, VTBL_FN(mw_set_value), VTBL_FN(mw_set_by_index), VTBL_FN(mw_remove_value),
    VTBL_FN(mw_remove_by_index),
};

static const WicIfaceEntry g_reader_ifaces[] = {
    WIC_ENTRY(WicMetaHandler, IID_IWICMetadataReader, reader),
};
static const WicIfaceEntry g_writer_ifaces[] = {
    WIC_ENTRY(WicMetaHandler, IID_IWICMetadataWriter, writer),
    WIC_ENTRY(WicMetaHandler, IID_IWICMetadataReader, writer),
};
static const WicClass g_reader_class = {handler_destroy, g_reader_ifaces, 1};
static const WicClass g_writer_class = {handler_destroy, g_writer_ifaces, 2};

static HRESULT handler_create(const GUID *format, BOOL writable, WicMetaHandler **out)
{
    WicMetaHandler *h = WicNew(sizeof(*h), writable ? &g_writer_class : &g_reader_class);
    if (!h)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&h->lock);
    WIC_SLOT(h, reader, &g_reader_vtbl);
    WIC_SLOT(h, writer, &g_writer_vtbl);
    h->format = *format;
    h->writable = writable;
    *out = h;
    return S_OK;
}

HRESULT WicMetaHandlerCreate(const GUID *format, IWICMetadataWriter **out)
{
    WicMetaHandler *h;
    HRESULT hr = handler_create(format, TRUE, &h);
    if (SUCCEEDED(hr))
        *out = (IWICMetadataWriter *)&h->writer;
    return hr;
}

HRESULT WicMetaReaderCreate(const GUID *format, IWICMetadataReader **out)
{
    WicMetaHandler *h;
    HRESULT hr = handler_create(format, FALSE, &h);
    if (SUCCEEDED(hr))
        *out = (IWICMetadataReader *)&h->reader;
    return hr;
}

/* return our underlying handler, or NULL for foreign objects. */
static WicMetaHandler *our_handler(IUnknown *object)
{
    if (!object)
        return nullptr;
    void *slot = nullptr;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IWICMetadataReader, &slot)) || !slot)
        return nullptr;
    WicBase *base = WicSlotBase(slot);
    WicMetaHandler *h = (base->cls == &g_reader_class || base->cls == &g_writer_class) ? (WicMetaHandler *)base : nullptr;
    ((IUnknown *)slot)->lpVtbl->Release((IUnknown *)slot);
    return h;
}

HRESULT WicMetaSetItem(IWICMetadataWriter *w, const PROPVARIANT *schema, const PROPVARIANT *id, const PROPVARIANT *value)
{
    return set_value(handler_from_writer(w), schema, id, value);
}

/* create a reader that the decoder can fill through private methods. */
HRESULT WicMetaAdd(IWICMetadataReader *r, const PROPVARIANT *schema, const PROPVARIANT *id, const PROPVARIANT *value)
{
    WicMetaHandler *h = our_handler((IUnknown *)r);
    if (!h)
        return E_FAIL;
    EnterCriticalSection(&h->lock);
    HRESULT hr = grow(h);
    if (SUCCEEDED(hr)) {
        hr = store_item(&h->items[h->count], schema, id, value);
        if (SUCCEEDED(hr))
            h->count++;
    }
    LeaveCriticalSection(&h->lock);
    return hr;
}

static HRESULT name_pv(PROPVARIANT *pv, const WCHAR *name)
{
    WicPvInit(pv);
    pv->vt = VT_LPWSTR;
    pv->pwszVal = CoTaskMemAlloc((wcslen(name) + 1) * sizeof(WCHAR));
    if (!pv->pwszVal)
        return E_OUTOFMEMORY;
    wcscpy(pv->pwszVal, name);
    return S_OK;
}

static HRESULT set_named(IWICMetadataWriter *w, const WCHAR *name, PROPVARIANT *value)
{
    PROPVARIANT id;
    HRESULT hr = name_pv(&id, name);
    if (SUCCEEDED(hr)) {
        hr = set_value(handler_from_writer(w), nullptr, &id, value);
        PropVariantClear(&id);
    }
    return hr;
}

HRESULT WicMetaSetU8(IWICMetadataWriter *h, const WCHAR *name, BYTE value)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI1;
    pv.bVal = value;
    return set_named(h, name, &pv);
}

HRESULT WicMetaSetU16(IWICMetadataWriter *h, const WCHAR *name, UINT16 value)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI2;
    pv.uiVal = value;
    return set_named(h, name, &pv);
}

HRESULT WicMetaSetU32(IWICMetadataWriter *h, const WCHAR *name, UINT32 value)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI4;
    pv.ulVal = value;
    return set_named(h, name, &pv);
}

HRESULT WicMetaSetBool(IWICMetadataWriter *h, const WCHAR *name, BOOL value)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_BOOL;
    pv.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return set_named(h, name, &pv);
}

HRESULT WicMetaSetAnsi(IWICMetadataWriter *h, const WCHAR *name, const char *text, SIZE_T length)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_LPSTR;
    pv.pszVal = CoTaskMemAlloc(length + 1);
    if (!pv.pszVal)
        return E_OUTOFMEMORY;
    memcpy(pv.pszVal, text, length);
    pv.pszVal[length] = 0;
    HRESULT hr = set_named(h, name, &pv);
    PropVariantClear(&pv);
    return hr;
}

HRESULT WicMetaSetBytes(IWICMetadataWriter *h, const WCHAR *name, const BYTE *data, UINT size)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI1 | VT_VECTOR;
    pv.caub.pElems = CoTaskMemAlloc(size ? size : 1);
    if (!pv.caub.pElems)
        return E_OUTOFMEMORY;
    memcpy(pv.caub.pElems, data, size);
    pv.caub.cElems = size;
    HRESULT hr = set_named(h, name, &pv);
    PropVariantClear(&pv);
    return hr;
}

HRESULT WicMetaSetNumbered(IWICMetadataWriter *h, UINT16 id, const PROPVARIANT *value)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI2;
    pv.uiVal = id;
    return set_value(handler_from_writer(h), nullptr, &pv, value);
}

/* reads for the codecs. */

static BOOL get_named(IWICMetadataReader *r, const WCHAR *name, PROPVARIANT *value)
{
    PROPVARIANT id;
    if (FAILED(name_pv(&id, name)))
        return FALSE;
    WicPvInit(value);
    HRESULT hr = r->lpVtbl->GetValue(r, nullptr, &id, value);
    PropVariantClear(&id);
    return SUCCEEDED(hr);
}

BOOL WicMetaGetU32(IWICMetadataReader *r, const WCHAR *name, UINT32 *out)
{
    PROPVARIANT v;
    if (!get_named(r, name, &v))
        return FALSE;
    BOOL ok = TRUE;
    switch (v.vt) {
    case VT_UI1:
    case VT_I1:
        *out = v.bVal;
        break;
    case VT_UI2:
    case VT_I2:
        *out = v.uiVal;
        break;
    case VT_UI4:
    case VT_I4:
        *out = v.ulVal;
        break;
    case VT_BOOL:
        *out = v.boolVal != VARIANT_FALSE;
        break;
    default:
        ok = FALSE;
    }
    PropVariantClear(&v);
    return ok;
}

BOOL WicMetaGetBool(IWICMetadataReader *r, const WCHAR *name, BOOL *out)
{
    UINT32 v;
    if (!WicMetaGetU32(r, name, &v))
        return FALSE;
    *out = v != 0;
    return TRUE;
}

BOOL WicMetaGetBytes(IWICMetadataReader *r, const WCHAR *name, BYTE **data, UINT *size)
{
    PROPVARIANT v;
    if (!get_named(r, name, &v))
        return FALSE;
    BOOL ok = FALSE;
    if (v.vt == (VT_UI1 | VT_VECTOR)) {
        *data = WicAlloc(v.caub.cElems ? v.caub.cElems : 1);
        if (*data) {
            memcpy(*data, v.caub.pElems, v.caub.cElems);
            *size = v.caub.cElems;
            ok = TRUE;
        }
    } else if (v.vt == VT_BLOB) {
        *data = WicAlloc(v.blob.cbSize ? v.blob.cbSize : 1);
        if (*data) {
            memcpy(*data, v.blob.pBlobData, v.blob.cbSize);
            *size = v.blob.cbSize;
            ok = TRUE;
        }
    }
    PropVariantClear(&v);
    return ok;
}

BOOL WicMetaGetAnsi(IWICMetadataReader *r, const WCHAR *name, char **text)
{
    PROPVARIANT v;
    if (!get_named(r, name, &v))
        return FALSE;
    BOOL ok = FALSE;
    if (v.vt == VT_LPSTR && v.pszVal) {
        SIZE_T n = strlen(v.pszVal);
        *text = WicAlloc(n + 1);
        if (*text) {
            memcpy(*text, v.pszVal, n + 1);
            ok = TRUE;
        }
    } else if (v.vt == VT_LPWSTR && v.pwszVal) {
        SIZE_T n = wcslen(v.pwszVal);
        *text = WicAlloc(n + 1);
        if (*text) {
            for (SIZE_T i = 0; i <= n; i++)
                (*text)[i] = (char)v.pwszVal[i];
            ok = TRUE;
        }
    }
    PropVariantClear(&v);
    return ok;
}

/* borrow items without copying for serialization. */
UINT WicMetaCount(IWICMetadataReader *r)
{
    WicMetaHandler *h = our_handler((IUnknown *)r);
    return h ? h->count : 0;
}

const WicMetaItem *WicMetaItemAt(IWICMetadataReader *r, UINT index)
{
    WicMetaHandler *h = our_handler((IUnknown *)r);
    return h && index < h->count ? &h->items[index] : nullptr;
}

/* blocks. */

typedef struct WicBlocks {
    WicBase base;
    WicSlot reader;
    WicSlot writer;
    CRITICAL_SECTION lock;
    GUID container;
    IWICMetadataReader **blocks;
    UINT count;
    UINT capacity;
    BOOL writable;
} WicBlocks;

static WicBlocks *blocks_of(void *This) { return (WicBlocks *)WicSlotBase(This); }

static void blocks_destroy(WicBase *base)
{
    WicBlocks *b = (WicBlocks *)base;
    for (UINT i = 0; i < b->count; i++)
        b->blocks[i]->lpVtbl->Release(b->blocks[i]);
    WicFree(b->blocks);
    DeleteCriticalSection(&b->lock);
}

static HRESULT blocks_add(WicBlocks *b, IWICMetadataReader *r)
{
    if (b->count == b->capacity) {
        UINT capacity = b->capacity ? b->capacity * 2 : 8;
        IWICMetadataReader **bigger = WicRealloc(b->blocks, (SIZE_T)capacity * sizeof(*bigger));
        if (!bigger)
            return E_OUTOFMEMORY;
        b->blocks = bigger;
        b->capacity = capacity;
    }
    r->lpVtbl->AddRef(r);
    b->blocks[b->count++] = r;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bk_container(void *This, GUID *container)
{
    if (!container)
        return E_INVALIDARG;
    *container = blocks_of(This)->container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bk_count(void *This, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = blocks_of(This)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bk_get_reader(void *This, UINT index, IWICMetadataReader **out)
{
    WicBlocks *b = blocks_of(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    EnterCriticalSection(&b->lock);
    HRESULT hr = S_OK;
    if (index >= b->count) {
        hr = WINCODEC_ERR_VALUEOUTOFRANGE;
    } else {
        *out = b->blocks[index];
        (*out)->lpVtbl->AddRef(*out);
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bk_enumerator(void *This, IEnumUnknown **out)
{
    WicBlocks *b = blocks_of(This);
    if (!out)
        return E_INVALIDARG;
    EnterCriticalSection(&b->lock);
    IUnknown **items = WicAlloc(((SIZE_T)b->count + 1) * sizeof(IUnknown *));
    HRESULT hr = items ? S_OK : E_OUTOFMEMORY;
    if (items) {
        for (UINT i = 0; i < b->count; i++) {
            items[i] = (IUnknown *)b->blocks[i];
            items[i]->lpVtbl->AddRef(items[i]);
        }
        hr = WicEnumFromItems(items, b->count, out);
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bk_initialize(void *This, IWICMetadataBlockReader *source)
{
    WicBlocks *b = blocks_of(This);
    if (!source)
        return E_INVALIDARG;
    UINT count = 0;
    HRESULT hr = source->lpVtbl->GetCount(source, &count);
    if (FAILED(hr))
        return hr;
    GUID container;
    EnterCriticalSection(&b->lock);
    for (UINT i = 0; i < b->count; i++)
        b->blocks[i]->lpVtbl->Release(b->blocks[i]);
    b->count = 0;
    if (SUCCEEDED(source->lpVtbl->GetContainerFormat(source, &container)))
        b->container = container;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        IWICMetadataReader *reader = nullptr;
        hr = source->lpVtbl->GetReaderByIndex(source, i, &reader);
        if (FAILED(hr))
            break;
        /* copy reader items into a writable handler of the same format. */
        IWICMetadataWriter *writer = nullptr;
        hr = WicMetaCopyToWriter(reader, &writer);
        reader->lpVtbl->Release(reader);
        if (SUCCEEDED(hr)) {
            hr = blocks_add(b, (IWICMetadataReader *)writer);
            writer->lpVtbl->Release(writer);
        }
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bk_get_writer(void *This, UINT index, IWICMetadataWriter **out)
{
    WicBlocks *b = blocks_of(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    EnterCriticalSection(&b->lock);
    HRESULT hr;
    if (index >= b->count)
        hr = WINCODEC_ERR_VALUEOUTOFRANGE;
    else
        hr = b->blocks[index]->lpVtbl->QueryInterface(b->blocks[index], &IID_IWICMetadataWriter, (void **)out);
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bk_add_writer(void *This, IWICMetadataWriter *writer)
{
    WicBlocks *b = blocks_of(This);
    if (!writer)
        return E_INVALIDARG;
    EnterCriticalSection(&b->lock);
    HRESULT hr = blocks_add(b, (IWICMetadataReader *)writer);
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bk_set_writer(void *This, UINT index, IWICMetadataWriter *writer)
{
    WicBlocks *b = blocks_of(This);
    if (!writer)
        return E_INVALIDARG;
    EnterCriticalSection(&b->lock);
    HRESULT hr = S_OK;
    if (index >= b->count) {
        hr = WINCODEC_ERR_VALUEOUTOFRANGE;
    } else {
        writer->lpVtbl->AddRef(writer);
        b->blocks[index]->lpVtbl->Release(b->blocks[index]);
        b->blocks[index] = (IWICMetadataReader *)writer;
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bk_remove_writer(void *This, UINT index)
{
    WicBlocks *b = blocks_of(This);
    EnterCriticalSection(&b->lock);
    HRESULT hr = S_OK;
    if (index >= b->count) {
        hr = WINCODEC_ERR_VALUEOUTOFRANGE;
    } else {
        b->blocks[index]->lpVtbl->Release(b->blocks[index]);
        memmove(&b->blocks[index], &b->blocks[index + 1], (SIZE_T)(b->count - index - 1) * sizeof(*b->blocks));
        b->count--;
    }
    LeaveCriticalSection(&b->lock);
    return hr;
}

static const IWICMetadataBlockReaderVtbl g_blocks_reader_vtbl = {
    WIC_IUNKNOWN_FUNCS, VTBL_FN(bk_container), VTBL_FN(bk_count), VTBL_FN(bk_get_reader), VTBL_FN(bk_enumerator),
};
static const IWICMetadataBlockWriterVtbl g_blocks_writer_vtbl = {
    WIC_IUNKNOWN_FUNCS, VTBL_FN(bk_container), VTBL_FN(bk_count), VTBL_FN(bk_get_reader), VTBL_FN(bk_enumerator),
    VTBL_FN(bk_initialize), VTBL_FN(bk_get_writer), VTBL_FN(bk_add_writer), VTBL_FN(bk_set_writer),
    VTBL_FN(bk_remove_writer),
};

static const WicIfaceEntry g_blocks_reader_ifaces[] = {WIC_ENTRY(WicBlocks, IID_IWICMetadataBlockReader, reader)};
static const WicIfaceEntry g_blocks_writer_ifaces[] = {
    WIC_ENTRY(WicBlocks, IID_IWICMetadataBlockWriter, writer),
    WIC_ENTRY(WicBlocks, IID_IWICMetadataBlockReader, writer),
};
static const WicClass g_blocks_reader_class = {blocks_destroy, g_blocks_reader_ifaces, 1};
static const WicClass g_blocks_writer_class = {blocks_destroy, g_blocks_writer_ifaces, 2};

static HRESULT blocks_create(const GUID *container, BOOL writable, WicBlocks **out)
{
    WicBlocks *b = WicNew(sizeof(*b), writable ? &g_blocks_writer_class : &g_blocks_reader_class);
    if (!b)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&b->lock);
    WIC_SLOT(b, reader, &g_blocks_reader_vtbl);
    WIC_SLOT(b, writer, &g_blocks_writer_vtbl);
    b->container = *container;
    b->writable = writable;
    *out = b;
    return S_OK;
}

HRESULT WicBlockReaderCreate(const GUID *container, IWICMetadataReader *const *readers, UINT count,
                             IWICMetadataBlockReader **out)
{
    WicBlocks *b;
    HRESULT hr = blocks_create(container, FALSE, &b);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++)
        hr = blocks_add(b, readers[i]);
    if (FAILED(hr)) {
        WicRelease(&b->reader);
        return hr;
    }
    *out = (IWICMetadataBlockReader *)&b->reader;
    return S_OK;
}

HRESULT WicBlockWriterCreate(const GUID *container, IWICMetadataBlockWriter **out)
{
    WicBlocks *b;
    HRESULT hr = blocks_create(container, TRUE, &b);
    if (SUCCEEDED(hr))
        *out = (IWICMetadataBlockWriter *)&b->writer;
    return hr;
}

IWICMetadataReader *WicBlocksFind(IWICMetadataBlockReader *blocks, const GUID *format)
{
    UINT count = 0;
    if (!blocks || FAILED(blocks->lpVtbl->GetCount(blocks, &count)))
        return nullptr;
    for (UINT i = 0; i < count; i++) {
        IWICMetadataReader *r = nullptr;
        GUID f;
        if (SUCCEEDED(blocks->lpVtbl->GetReaderByIndex(blocks, i, &r)) && r) {
            if (SUCCEEDED(r->lpVtbl->GetMetadataFormat(r, &f)) && IsEqualGUID(&f, format))
                return r;
            r->lpVtbl->Release(r);
        }
    }
    return nullptr;
}

/* copy reader items into a writer, recursively copying nested blocks. */
HRESULT WicMetaCopyToWriter(IWICMetadataReader *reader, IWICMetadataWriter **out)
{
    GUID format;
    UINT count = 0;
    *out = nullptr;
    HRESULT hr = reader->lpVtbl->GetMetadataFormat(reader, &format);
    if (SUCCEEDED(hr))
        hr = reader->lpVtbl->GetCount(reader, &count);
    if (FAILED(hr))
        return hr;
    IWICMetadataWriter *writer;
    hr = WicMetaHandlerCreate(&format, &writer);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        PROPVARIANT schema, id, value;
        hr = reader->lpVtbl->GetValueByIndex(reader, i, &schema, &id, &value);
        if (FAILED(hr))
            break;
        if (value.vt == VT_UNKNOWN && value.punkVal) {
            IWICMetadataReader *sub = nullptr;
            if (SUCCEEDED(value.punkVal->lpVtbl->QueryInterface(value.punkVal, &IID_IWICMetadataReader, (void **)&sub))) {
                IWICMetadataWriter *copy = nullptr;
                hr = WicMetaCopyToWriter(sub, &copy);
                sub->lpVtbl->Release(sub);
                if (SUCCEEDED(hr)) {
                    PropVariantClear(&value);
                    value.vt = VT_UNKNOWN;
                    value.punkVal = (IUnknown *)copy;
                }
            }
        }
        if (SUCCEEDED(hr)) {
            WicMetaAdd((IWICMetadataReader *)writer, &schema, &id, &value);
        }
        PropVariantClear(&schema);
        PropVariantClear(&id);
        PropVariantClear(&value);
    }
    if (FAILED(hr)) {
        writer->lpVtbl->Release(writer);
        return hr;
    }
    *out = writer;
    return S_OK;
}

/* enumerators of strings and objects. */

HRESULT WicEnumFromItems(IUnknown **items, UINT count, IEnumUnknown **out) { return WicEnumUnknownCreate(items, count, out); }

/* enumerate names allocated with the task allocator. */
typedef struct WicStringEnum {
    WicBase base;
    WicSlot slot;
    WCHAR **names;
    UINT count;
    UINT position;
} WicStringEnum;

static WicStringEnum *senum_from(void *This) { return WIC_FROM_SLOT(WicStringEnum, slot, This); }

static void senum_destroy(WicBase *base)
{
    WicStringEnum *e = (WicStringEnum *)base;
    for (UINT i = 0; i < e->count; i++)
        WicFree(e->names[i]);
    WicFree(e->names);
}

static HRESULT STDMETHODCALLTYPE senum_next(IEnumString *This, ULONG count, LPOLESTR *out, ULONG *fetched)
{
    WicStringEnum *e = senum_from(This);
    ULONG got = 0;
    if (!out && count)
        return E_INVALIDARG;
    if (count > 1 && !fetched)
        return E_INVALIDARG;
    while (got < count && e->position < e->count) {
        SIZE_T n = wcslen(e->names[e->position]) + 1;
        out[got] = CoTaskMemAlloc(n * sizeof(WCHAR));
        if (!out[got]) {
            while (got)
                CoTaskMemFree(out[--got]);
            return E_OUTOFMEMORY;
        }
        memcpy(out[got], e->names[e->position], n * sizeof(WCHAR));
        e->position++;
        got++;
    }
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE senum_skip(IEnumString *This, ULONG count)
{
    WicStringEnum *e = senum_from(This);
    if (e->position + count > e->count) {
        e->position = e->count;
        return S_FALSE;
    }
    e->position += count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE senum_reset(IEnumString *This)
{
    senum_from(This)->position = 0;
    return S_OK;
}

static HRESULT senum_create_copy(WCHAR *const *names, UINT count, UINT position, IEnumString **out);

static HRESULT STDMETHODCALLTYPE senum_clone(IEnumString *This, IEnumString **out)
{
    WicStringEnum *e = senum_from(This);
    if (!out)
        return E_INVALIDARG;
    return senum_create_copy(e->names, e->count, e->position, out);
}

static const void *const g_senum_vtbl[] = {VTBL_FN(WicQueryInterface), VTBL_FN(WicAddRef), VTBL_FN(WicRelease),
                                          VTBL_FN(senum_next),        VTBL_FN(senum_skip), VTBL_FN(senum_reset),
                                          VTBL_FN(senum_clone)};
static const WicIfaceEntry g_senum_ifaces[] = {WIC_ENTRY(WicStringEnum, IID_IEnumString, slot)};
static const WicClass g_senum_class = {senum_destroy, g_senum_ifaces, 1};

/* the names are copied. */
static HRESULT senum_create_copy(WCHAR *const *names, UINT count, UINT position, IEnumString **out)
{
    WicStringEnum *e = WicNew(sizeof(*e), &g_senum_class);
    if (!e)
        return E_OUTOFMEMORY;
    e->slot.lpVtbl = g_senum_vtbl;
    e->slot.delta = (INT)(__builtin_offsetof(WicStringEnum, slot) - __builtin_offsetof(WicStringEnum, base));
    e->names = WicAllocZero(((SIZE_T)count + 1) * sizeof(WCHAR *));
    if (!e->names) {
        WicRelease(&e->slot);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++) {
        e->names[i] = WicStrDup(names[i]);
        e->count = i + 1;
        if (!e->names[i]) {
            WicRelease(&e->slot);
            return E_OUTOFMEMORY;
        }
    }
    e->position = position;
    *out = (IEnumString *)&e->slot;
    return S_OK;
}

HRESULT WicEnumStringCreate(WCHAR *const *names, UINT count, IEnumString **out)
{
    return senum_create_copy(names, count, 0, out);
}

/* items put by decoders. */

static HRESULT put_named(IWICMetadataReader *r, const WCHAR *name, PROPVARIANT *value)
{
    PROPVARIANT id;
    HRESULT hr = name_pv(&id, name);
    if (SUCCEEDED(hr)) {
        hr = WicMetaAdd(r, nullptr, &id, value);
        PropVariantClear(&id);
    }
    return hr;
}

HRESULT WicMetaPutNamed(IWICMetadataReader *r, const WCHAR *name, VARTYPE vt, UINT64 value)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = vt;
    switch (vt) {
    case VT_UI1:
        pv.bVal = (BYTE)value;
        break;
    case VT_UI2:
        pv.uiVal = (USHORT)value;
        break;
    case VT_UI4:
        pv.ulVal = (ULONG)value;
        break;
    case VT_BOOL:
        pv.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    default:
        pv.uhVal.QuadPart = value;
    }
    return put_named(r, name, &pv);
}

HRESULT WicMetaPutAnsi(IWICMetadataReader *r, const WCHAR *name, const char *text, SIZE_T length)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_LPSTR;
    pv.pszVal = CoTaskMemAlloc(length + 1);
    if (!pv.pszVal)
        return E_OUTOFMEMORY;
    memcpy(pv.pszVal, text, length);
    pv.pszVal[length] = 0;
    HRESULT hr = put_named(r, name, &pv);
    PropVariantClear(&pv);
    return hr;
}

HRESULT WicMetaPutBytes(IWICMetadataReader *r, const WCHAR *name, const void *data, UINT size)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI1 | VT_VECTOR;
    pv.caub.pElems = CoTaskMemAlloc(size ? size : 1);
    if (!pv.caub.pElems)
        return E_OUTOFMEMORY;
    memcpy(pv.caub.pElems, data, size);
    pv.caub.cElems = size;
    HRESULT hr = put_named(r, name, &pv);
    PropVariantClear(&pv);
    return hr;
}
