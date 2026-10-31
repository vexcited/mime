/*
 * PSCreateMemoryPropertyStore: values by key in an open addressed table, enumerated
 * in slot order like windows does, named values beside them. both are saved and
 * loaded in the [MS-PROPSTORE] serialized format.
 */
#include "propsys_private.h"

HRESULT PsSerializeValue(const PROPVARIANT *value, BYTE **out, ULONG *size);
HRESULT PsDeserializeValue(const BYTE *data, ULONG size, PROPVARIANT *value);

#define STORAGE_VERSION 0x53505331
#define FIRST_TABLE_SIZE 19

MIME_DEFINE_GUID(FMTID_NamedProperties, 0xd5cdd505, 0x2e9c, 0x101b, 0x93, 0x97, 0x08, 0x00, 0x2b, 0x2c, 0xf9, 0xae);

typedef struct {
    DWORD used;
    PROPERTYKEY key;
    PSC_STATE state;
    PROPVARIANT value;
} SLOT;

typedef struct {
    LPWSTR name;
    PROPVARIANT value;
} NAMED;

typedef struct {
    IPropertyStoreCache cache;
    INamedPropertyStore named;
    IPersistSerializedPropStorage serialized;
    IPersistStream stream;
    IPropertyBag bag;
    LONG refs;
    SRWLOCK lock;
    SLOT *slots;
    UINT table_size, count;
    NAMED *names;
    UINT name_count, name_capacity;
    PERSIST_SPROPSTORE_FLAGS flags;
    BOOL dirty;
} STORE;

#define FROM(iface, member) ((STORE *)((BYTE *)(iface) - offsetof(STORE, member)))

/* ------------------------------------------------------------------ the table */

static ULONG hash_key(const PROPERTYKEY *key)
{
    const BYTE *bytes = (const BYTE *)key;
    ULONG h = ((ULONG)bytes[0] + 694790345u) ^ 0x12B9B0A5u;
    for (UINT i = 1; i < sizeof(PROPERTYKEY); i++)
        h = ((h >> 2) + 2080 * h + bytes[i]) ^ h;
    return h & 0x7FFFFFFF;
}

static BOOL same_key(const PROPERTYKEY *a, const PROPERTYKEY *b)
{
    return a->pid == b->pid && IsEqualGUID(&a->fmtid, &b->fmtid);
}

/* the slot holding key, or the free slot it would go in. */
static SLOT *find_slot(SLOT *slots, UINT size, const PROPERTYKEY *key)
{
    for (UINT i = hash_key(key) % size;; i = (i + 1) % size) {
        if (!slots[i].used || same_key(&slots[i].key, key))
            return &slots[i];
    }
}

static SLOT *lookup(STORE *store, const PROPERTYKEY *key)
{
    if (!store->slots)
        return nullptr;
    SLOT *slot = find_slot(store->slots, store->table_size, key);
    return slot->used ? slot : nullptr;
}

/* the table grows to 2n-1 slots once it is four fifths full. */
static HRESULT make_room(STORE *store)
{
    if (!store->slots) {
        store->slots = PsAllocZero(FIRST_TABLE_SIZE * sizeof(SLOT));
        if (!store->slots)
            return E_OUTOFMEMORY;
        store->table_size = FIRST_TABLE_SIZE;
    }
    if (5 * (store->count + 1) <= 4 * store->table_size)
        return S_OK;
    UINT size = 2 * store->table_size - 1;
    SLOT *slots = PsAllocZero((SIZE_T)size * sizeof(SLOT));
    if (!slots)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < store->table_size; i++)
        if (store->slots[i].used)
            *find_slot(slots, size, &store->slots[i].key) = store->slots[i];
    CoTaskMemFree(store->slots);
    store->slots = slots;
    store->table_size = size;
    return S_OK;
}

static void clear_all(STORE *store)
{
    for (UINT i = 0; store->slots && i < store->table_size; i++)
        if (store->slots[i].used)
            PropVariantClear(&store->slots[i].value);
    CoTaskMemFree(store->slots);
    store->slots = nullptr;
    store->table_size = store->count = 0;
    for (UINT i = 0; i < store->name_count; i++) {
        CoTaskMemFree(store->names[i].name);
        PropVariantClear(&store->names[i].value);
    }
    CoTaskMemFree(store->names);
    store->names = nullptr;
    store->name_count = store->name_capacity = 0;
}

/* takes ownership of value on success. */
static HRESULT put_value(STORE *store, const PROPERTYKEY *key, PROPVARIANT *value, PSC_STATE state)
{
    SLOT *slot = lookup(store, key);
    if (slot) {
        PropVariantClear(&slot->value);
        slot->value = *value;
        slot->state = state;
        return S_OK;
    }
    HRESULT hr = make_room(store);
    if (FAILED(hr))
        return hr;
    slot = find_slot(store->slots, store->table_size, key);
    slot->used = 1;
    slot->key = *key;
    slot->state = state;
    slot->value = *value;
    store->count++;
    return S_OK;
}

/* a null value only changes the state of a value the store has. */
static HRESULT set_value_and_state(STORE *store, const PROPERTYKEY *key, const PROPVARIANT *value, PSC_STATE state)
{
    if (store->flags & FPSPS_READONLY)
        return STG_E_ACCESSDENIED;
    SLOT *slot = lookup(store, key);
    if (!value) {
        if (!slot)
            return TYPE_E_ELEMENTNOTFOUND;
        if (slot->state != state) {
            slot->state = state;
            store->dirty = TRUE;
        }
        return S_OK;
    }
    /* with FPSPS_TREAT_NEW_VALUES_AS_DIRTY, setting an unchanged value does nothing. */
    if (store->flags & FPSPS_TREAT_NEW_VALUES_AS_DIRTY) {
        if (slot ? !PropVariantCompareEx(&slot->value, value, PVCU_DEFAULT, PVCF_USESTRCMP) : value->vt == VT_EMPTY)
            return S_FALSE;
    }
    PROPVARIANT copy;
    HRESULT hr = PropVariantCopy(&copy, value);
    if (FAILED(hr))
        return hr;
    hr = put_value(store, key, &copy, state);
    if (FAILED(hr))
        PropVariantClear(&copy);
    else
        store->dirty = TRUE;
    return hr;
}

static NAMED *find_name(STORE *store, LPCWSTR name)
{
    for (UINT i = 0; i < store->name_count; i++)
        if (!wcscmp(store->names[i].name, name))
            return &store->names[i];
    return nullptr;
}

/* takes ownership of value on success. */
static HRESULT put_named(STORE *store, LPCWSTR name, PROPVARIANT *value)
{
    NAMED *named = find_name(store, name);
    if (named) {
        PropVariantClear(&named->value);
        named->value = *value;
        return S_OK;
    }
    if (store->name_count == store->name_capacity) {
        UINT capacity = store->name_capacity ? store->name_capacity * 2 : 8;
        NAMED *grown = CoTaskMemRealloc(store->names, capacity * sizeof(NAMED));
        if (!grown)
            return E_OUTOFMEMORY;
        store->names = grown;
        store->name_capacity = capacity;
    }
    LPWSTR copy;
    HRESULT hr = PsAllocString(name, &copy);
    if (FAILED(hr))
        return hr;
    store->names[store->name_count].name = copy;
    store->names[store->name_count].value = *value;
    store->name_count++;
    return S_OK;
}

/* ------------------------------------------------------------------ serialized form */

typedef struct {
    BYTE *data;
    SIZE_T size, capacity;
    HRESULT status;
} BUFFER;

static void append(BUFFER *b, const void *bytes, SIZE_T size)
{
    if (FAILED(b->status))
        return;
    if (b->size + size > b->capacity) {
        SIZE_T capacity = b->capacity ? b->capacity * 2 : 256;
        while (capacity < b->size + size)
            capacity *= 2;
        BYTE *grown = CoTaskMemRealloc(b->data, capacity);
        if (!grown) {
            b->status = E_OUTOFMEMORY;
            return;
        }
        b->data = grown;
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, bytes, size);
    b->size += size;
}

static void append_dword(BUFFER *b, DWORD value)
{
    append(b, &value, 4);
}

static void patch_dword(BUFFER *b, SIZE_T offset, DWORD value)
{
    if (SUCCEEDED(b->status))
        memcpy(b->data + offset, &value, 4);
}

static SIZE_T begin_storage(BUFFER *b, const GUID *fmtid)
{
    SIZE_T start = b->size;
    append_dword(b, 0);
    append_dword(b, STORAGE_VERSION);
    append(b, fmtid, sizeof(GUID));
    return start;
}

static void end_storage(BUFFER *b, SIZE_T start)
{
    append_dword(b, 0);
    patch_dword(b, start, (DWORD)(b->size - start));
}

static void append_value(BUFFER *b, const void *id, DWORD id_size, BOOL named, const PROPVARIANT *value)
{
    BYTE *typed;
    ULONG typed_size;
    HRESULT hr = PsSerializeValue(value, &typed, &typed_size);
    if (FAILED(hr)) {
        if (SUCCEEDED(b->status))
            b->status = hr;
        return;
    }
    BYTE reserved = 0;
    append_dword(b, 4 + (named ? 4 + id_size : 4) + 1 + typed_size);
    if (named) {
        append_dword(b, id_size);
        append(b, &reserved, 1);
        append(b, id, id_size);
    } else {
        append(b, id, 4);
        append(b, &reserved, 1);
    }
    append(b, typed, typed_size);
    CoTaskMemFree(typed);
}

/* storages are written in the order their first value appears in the table, named values last. */
static HRESULT serialize(STORE *store, BYTE **out, ULONG *size)
{
    BUFFER b = {0};
    BYTE *done = PsAllocZero(store->table_size ? store->table_size : 1);
    if (!done)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < store->table_size; i++) {
        SLOT *first = &store->slots[i];
        if (!first->used || done[i] || IsEqualGUID(&first->key.fmtid, &FMTID_NamedProperties))
            continue;
        SIZE_T start = begin_storage(&b, &first->key.fmtid);
        for (UINT j = i; j < store->table_size; j++) {
            SLOT *slot = &store->slots[j];
            if (slot->used && !done[j] && IsEqualGUID(&slot->key.fmtid, &first->key.fmtid)) {
                done[j] = 1;
                append_value(&b, &slot->key.pid, 4, FALSE, &slot->value);
            }
        }
        end_storage(&b, start);
    }
    CoTaskMemFree(done);
    if (store->name_count) {
        SIZE_T start = begin_storage(&b, &FMTID_NamedProperties);
        for (UINT i = 0; i < store->name_count; i++)
            append_value(&b, store->names[i].name, (DWORD)(wcslen(store->names[i].name) + 1) * 2, TRUE,
                         &store->names[i].value);
        end_storage(&b, start);
    }
    append_dword(&b, 0);
    if (FAILED(b.status)) {
        CoTaskMemFree(b.data);
        return b.status;
    }
    *out = b.data;
    *size = (ULONG)b.size;
    return S_OK;
}

static HRESULT deserialize(STORE *store, const BYTE *data, ULONG size)
{
    ULONG offset = 0;
    while (size - offset >= 4) {
        DWORD storage_size;
        memcpy(&storage_size, data + offset, 4);
        if (!storage_size)
            return S_OK;
        DWORD version;
        if (storage_size < 28 || storage_size > size - offset)
            return E_INVALIDARG;
        memcpy(&version, data + offset + 4, 4);
        if (version != STORAGE_VERSION)
            return E_INVALIDARG;
        GUID fmtid;
        memcpy(&fmtid, data + offset + 8, sizeof(GUID));
        BOOL named = IsEqualGUID(&fmtid, &FMTID_NamedProperties);
        const BYTE *values = data + offset + 24;
        ULONG left = storage_size - 24, at = 0;
        while (left - at >= 4) {
            DWORD value_size;
            memcpy(&value_size, values + at, 4);
            if (!value_size)
                break;
            if (value_size < 9 || value_size > left - at)
                return E_INVALIDARG;
            const BYTE *value = values + at;
            PROPVARIANT pv;
            HRESULT hr;
            if (named) {
                DWORD name_size;
                memcpy(&name_size, value + 4, 4);
                if (name_size < 2 || (name_size & 1) || name_size > value_size - 9 ||
                    *(const WCHAR *)(value + 9 + name_size - 2))
                    return E_INVALIDARG;
                hr = PsDeserializeValue(value + 9 + name_size, value_size - 9 - name_size, &pv);
                if (SUCCEEDED(hr)) {
                    hr = put_named(store, (LPCWSTR)(value + 9), &pv);
                    if (FAILED(hr))
                        PropVariantClear(&pv);
                }
            } else {
                PROPERTYKEY key = {fmtid, 0};
                memcpy(&key.pid, value + 4, 4);
                hr = PsDeserializeValue(value + 9, value_size - 9, &pv);
                if (SUCCEEDED(hr)) {
                    hr = put_value(store, &key, &pv, PSC_NORMAL);
                    if (FAILED(hr))
                        PropVariantClear(&pv);
                }
            }
            if (FAILED(hr))
                return hr;
            at += value_size;
        }
        offset += storage_size;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT query(STORE *store, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore) ||
        IsEqualIID(riid, &IID_IPropertyStoreCache) || IsEqualIID(riid, &IID_IAgileObject))
        *object = &store->cache;
    else if (IsEqualIID(riid, &IID_INamedPropertyStore))
        *object = &store->named;
    else if (IsEqualIID(riid, &IID_IPersistSerializedPropStorage) ||
             IsEqualIID(riid, &IID_IPersistSerializedPropStorage2))
        *object = &store->serialized;
    else if (IsEqualIID(riid, &IID_IPersistStream))
        *object = &store->stream;
    else if (IsEqualIID(riid, &IID_IPropertyBag))
        *object = &store->bag;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&store->refs);
    return S_OK;
}

static ULONG add_ref(STORE *store)
{
    return (ULONG)InterlockedIncrement(&store->refs);
}

static ULONG release(STORE *store)
{
    LONG refs = InterlockedDecrement(&store->refs);
    if (!refs) {
        clear_all(store);
        CoTaskMemFree(store);
    }
    return (ULONG)refs;
}

/* ------------------------------------------------------------------ IPropertyStoreCache */

static HRESULT WINAPI cache_query(IPropertyStoreCache *iface, REFIID riid, void **object)
{
    return query(FROM(iface, cache), riid, object);
}

static ULONG WINAPI cache_add_ref(IPropertyStoreCache *iface)
{
    return add_ref(FROM(iface, cache));
}

static ULONG WINAPI cache_release(IPropertyStoreCache *iface)
{
    return release(FROM(iface, cache));
}

static HRESULT WINAPI cache_get_count(IPropertyStoreCache *iface, DWORD *count)
{
    STORE *store = FROM(iface, cache);
    if (!count)
        return E_POINTER;
    AcquireSRWLockShared(&store->lock);
    *count = store->count;
    ReleaseSRWLockShared(&store->lock);
    return S_OK;
}

static HRESULT WINAPI cache_get_at(IPropertyStoreCache *iface, DWORD index, PROPERTYKEY *key)
{
    STORE *store = FROM(iface, cache);
    if (!key)
        return E_POINTER;
    memset(key, 0, sizeof(*key));
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockShared(&store->lock);
    for (UINT i = 0, n = 0; i < store->table_size; i++) {
        if (!store->slots[i].used)
            continue;
        if (n++ == index) {
            *key = store->slots[i].key;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockShared(&store->lock);
    return hr;
}

static HRESULT get_value_and_state(STORE *store, REFPROPERTYKEY key, PROPVARIANT *value, PSC_STATE *state,
                                   BOOL missing_fails)
{
    if (value)
        memset(value, 0, sizeof(*value));
    if (state)
        *state = PSC_NORMAL;
    if (!key)
        return E_POINTER;
    HRESULT hr = S_OK;
    AcquireSRWLockShared(&store->lock);
    SLOT *slot = lookup(store, key);
    if (!slot) {
        hr = missing_fails ? TYPE_E_ELEMENTNOTFOUND : S_OK;
    } else {
        if (value)
            hr = PropVariantCopy(value, &slot->value);
        if (state)
            *state = slot->state;
    }
    ReleaseSRWLockShared(&store->lock);
    return hr;
}

static HRESULT WINAPI cache_get_value(IPropertyStoreCache *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    if (!value)
        return E_POINTER;
    return get_value_and_state(FROM(iface, cache), key, value, nullptr, FALSE);
}

static HRESULT locked_set(STORE *store, REFPROPERTYKEY key, const PROPVARIANT *value, PSC_STATE state)
{
    if (!key)
        return E_POINTER;
    AcquireSRWLockExclusive(&store->lock);
    HRESULT hr = set_value_and_state(store, key, value, state);
    ReleaseSRWLockExclusive(&store->lock);
    return hr;
}

static HRESULT WINAPI cache_set_value(IPropertyStoreCache *iface, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    STORE *store = FROM(iface, cache);
    if (!value)
        return E_POINTER;
    PSC_STATE state = value->vt == VT_EMPTY ? PSC_NOTINSOURCE
                      : (store->flags & FPSPS_TREAT_NEW_VALUES_AS_DIRTY) ? PSC_DIRTY
                                                                         : PSC_NORMAL;
    return locked_set(store, key, value, state);
}

static HRESULT WINAPI cache_commit(IPropertyStoreCache *iface)
{
    return S_OK;
}

static HRESULT WINAPI cache_get_state(IPropertyStoreCache *iface, REFPROPERTYKEY key, PSC_STATE *state)
{
    if (!state)
        return E_POINTER;
    return get_value_and_state(FROM(iface, cache), key, nullptr, state, TRUE);
}

static HRESULT WINAPI cache_get_value_and_state(IPropertyStoreCache *iface, REFPROPERTYKEY key, PROPVARIANT *value,
                                                PSC_STATE *state)
{
    if (!value || !state)
        return E_POINTER;
    return get_value_and_state(FROM(iface, cache), key, value, state, TRUE);
}

static HRESULT WINAPI cache_set_state(IPropertyStoreCache *iface, REFPROPERTYKEY key, PSC_STATE state)
{
    return locked_set(FROM(iface, cache), key, nullptr, state);
}

static HRESULT WINAPI cache_set_value_and_state(IPropertyStoreCache *iface, REFPROPERTYKEY key,
                                                const PROPVARIANT *value, PSC_STATE state)
{
    return locked_set(FROM(iface, cache), key, value, state);
}

static const IPropertyStoreCacheVtbl cache_vtbl = {
    cache_query, cache_add_ref, cache_release, cache_get_count, cache_get_at, cache_get_value, cache_set_value,
    cache_commit, cache_get_state, cache_get_value_and_state, cache_set_state, cache_set_value_and_state,
};

/* ------------------------------------------------------------------ INamedPropertyStore */

static HRESULT WINAPI named_query(INamedPropertyStore *iface, REFIID riid, void **object)
{
    return query(FROM(iface, named), riid, object);
}

static ULONG WINAPI named_add_ref(INamedPropertyStore *iface)
{
    return add_ref(FROM(iface, named));
}

static ULONG WINAPI named_release(INamedPropertyStore *iface)
{
    return release(FROM(iface, named));
}

static HRESULT get_named(STORE *store, LPCWSTR name, PROPVARIANT *value)
{
    if (!value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    if (!name)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    AcquireSRWLockShared(&store->lock);
    NAMED *named = find_name(store, name);
    if (named)
        hr = PropVariantCopy(value, &named->value);
    ReleaseSRWLockShared(&store->lock);
    return hr;
}

static HRESULT set_named(STORE *store, LPCWSTR name, const PROPVARIANT *value)
{
    if (!name || !value)
        return E_INVALIDARG;
    PROPVARIANT copy;
    HRESULT hr = PropVariantCopy(&copy, value);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&store->lock);
    if (store->flags & FPSPS_READONLY)
        hr = STG_E_ACCESSDENIED;
    else
        hr = put_named(store, name, &copy);
    if (SUCCEEDED(hr))
        store->dirty = TRUE;
    ReleaseSRWLockExclusive(&store->lock);
    if (FAILED(hr))
        PropVariantClear(&copy);
    return hr;
}

static HRESULT WINAPI named_get_value(INamedPropertyStore *iface, LPCWSTR name, PROPVARIANT *value)
{
    return get_named(FROM(iface, named), name, value);
}

static HRESULT WINAPI named_set_value(INamedPropertyStore *iface, LPCWSTR name, const PROPVARIANT *value)
{
    return set_named(FROM(iface, named), name, value);
}

static HRESULT WINAPI named_get_count(INamedPropertyStore *iface, DWORD *count)
{
    STORE *store = FROM(iface, named);
    if (!count)
        return E_POINTER;
    AcquireSRWLockShared(&store->lock);
    *count = store->name_count;
    ReleaseSRWLockShared(&store->lock);
    return S_OK;
}

static HRESULT WINAPI named_get_at(INamedPropertyStore *iface, DWORD index, BSTR *name)
{
    STORE *store = FROM(iface, named);
    if (!name)
        return E_POINTER;
    *name = nullptr;
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockShared(&store->lock);
    if (index < store->name_count) {
        *name = SysAllocString(store->names[index].name);
        hr = *name ? S_OK : E_OUTOFMEMORY;
    }
    ReleaseSRWLockShared(&store->lock);
    return hr;
}

static const INamedPropertyStoreVtbl named_vtbl = {
    named_query, named_add_ref, named_release, named_get_value, named_set_value, named_get_count, named_get_at,
};

/* ------------------------------------------------------------------ IPersistSerializedPropStorage2 */

static HRESULT WINAPI serialized_query(IPersistSerializedPropStorage *iface, REFIID riid, void **object)
{
    return query(FROM(iface, serialized), riid, object);
}

static ULONG WINAPI serialized_add_ref(IPersistSerializedPropStorage *iface)
{
    return add_ref(FROM(iface, serialized));
}

static ULONG WINAPI serialized_release(IPersistSerializedPropStorage *iface)
{
    return release(FROM(iface, serialized));
}

static HRESULT WINAPI serialized_set_flags(IPersistSerializedPropStorage *iface, PERSIST_SPROPSTORE_FLAGS flags)
{
    STORE *store = FROM(iface, serialized);
    AcquireSRWLockExclusive(&store->lock);
    store->flags = flags;
    ReleaseSRWLockExclusive(&store->lock);
    return S_OK;
}

/* loading replaces everything, read only or not. */
static HRESULT load(STORE *store, const BYTE *data, ULONG size)
{
    AcquireSRWLockExclusive(&store->lock);
    clear_all(store);
    HRESULT hr = data && size ? deserialize(store, data, size) : S_OK;
    if (FAILED(hr))
        clear_all(store);
    store->dirty = FALSE;
    ReleaseSRWLockExclusive(&store->lock);
    return hr;
}

static HRESULT WINAPI serialized_set_storage(IPersistSerializedPropStorage *iface, PCUSERIALIZEDPROPSTORAGE storage,
                                             DWORD size)
{
    if (!storage && size)
        return E_POINTER;
    return load(FROM(iface, serialized), (const BYTE *)storage, size);
}

static HRESULT locked_serialize(STORE *store, BYTE **data, ULONG *size)
{
    AcquireSRWLockShared(&store->lock);
    HRESULT hr = serialize(store, data, size);
    ReleaseSRWLockShared(&store->lock);
    return hr;
}

static HRESULT WINAPI serialized_get_storage(IPersistSerializedPropStorage *iface, SERIALIZEDPROPSTORAGE **storage,
                                             DWORD *size)
{
    if (!storage || !size)
        return E_POINTER;
    *storage = nullptr;
    *size = 0;
    return locked_serialize(FROM(iface, serialized), (BYTE **)storage, size);
}

static HRESULT WINAPI serialized_get_size(IPersistSerializedPropStorage *iface, DWORD *size)
{
    if (!size)
        return E_POINTER;
    BYTE *data;
    HRESULT hr = locked_serialize(FROM(iface, serialized), &data, size);
    if (SUCCEEDED(hr))
        CoTaskMemFree(data);
    else
        *size = 0;
    return hr;
}

static HRESULT WINAPI serialized_get_buffer(IPersistSerializedPropStorage *iface, SERIALIZEDPROPSTORAGE *storage,
                                            DWORD size, DWORD *written)
{
    if (!storage || !written)
        return E_POINTER;
    *written = 0;
    BYTE *data;
    ULONG needed;
    HRESULT hr = locked_serialize(FROM(iface, serialized), &data, &needed);
    if (FAILED(hr))
        return hr;
    if (needed > size) {
        hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    } else {
        memcpy(storage, data, needed);
        *written = needed;
    }
    CoTaskMemFree(data);
    return hr;
}

static const IPersistSerializedPropStorageVtbl serialized_vtbl = {
    serialized_query, serialized_add_ref, serialized_release, serialized_set_flags, serialized_set_storage,
    serialized_get_storage, serialized_get_size, serialized_get_buffer,
};

/* ------------------------------------------------------------------ IPersistStream */

static HRESULT WINAPI stream_query(IPersistStream *iface, REFIID riid, void **object)
{
    return query(FROM(iface, stream), riid, object);
}

static ULONG WINAPI stream_add_ref(IPersistStream *iface)
{
    return add_ref(FROM(iface, stream));
}

static ULONG WINAPI stream_release(IPersistStream *iface)
{
    return release(FROM(iface, stream));
}

static HRESULT WINAPI stream_get_class(IPersistStream *iface, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_InMemoryPropertyStore;
    return S_OK;
}

static HRESULT WINAPI stream_is_dirty(IPersistStream *iface)
{
    return FROM(iface, stream)->dirty ? S_OK : S_FALSE;
}

/* the stream holds the storage size, then the storage. */
static HRESULT WINAPI stream_load(IPersistStream *iface, IStream *stream)
{
    if (!stream)
        return E_POINTER;
    DWORD size;
    ULONG read;
    HRESULT hr = stream->lpVtbl->Read(stream, &size, 4, &read);
    if (FAILED(hr))
        return hr;
    if (read != 4)
        return E_FAIL;
    BYTE *data = PsAllocZero(size);
    if (!data)
        return E_OUTOFMEMORY;
    hr = stream->lpVtbl->Read(stream, data, size, &read);
    if (SUCCEEDED(hr))
        hr = read == size ? load(FROM(iface, stream), data, size) : E_FAIL;
    CoTaskMemFree(data);
    return hr;
}

static HRESULT WINAPI stream_save(IPersistStream *iface, IStream *stream, BOOL clear_dirty)
{
    STORE *store = FROM(iface, stream);
    if (!stream)
        return E_POINTER;
    BYTE *data;
    ULONG size, written;
    HRESULT hr = locked_serialize(store, &data, &size);
    if (FAILED(hr))
        return hr;
    hr = stream->lpVtbl->Write(stream, &size, 4, &written);
    if (SUCCEEDED(hr))
        hr = stream->lpVtbl->Write(stream, data, size, &written);
    CoTaskMemFree(data);
    if (SUCCEEDED(hr) && clear_dirty)
        store->dirty = FALSE;
    return hr;
}

static HRESULT WINAPI stream_get_size(IPersistStream *iface, ULARGE_INTEGER *size)
{
    if (!size)
        return E_POINTER;
    BYTE *data;
    ULONG bytes;
    HRESULT hr = locked_serialize(FROM(iface, stream), &data, &bytes);
    if (FAILED(hr))
        return hr;
    CoTaskMemFree(data);
    size->QuadPart = bytes + 4;
    return S_OK;
}

static const IPersistStreamVtbl stream_vtbl = {
    stream_query, stream_add_ref, stream_release, stream_get_class, stream_is_dirty, stream_load, stream_save,
    stream_get_size,
};

/* ------------------------------------------------------------------ IPropertyBag */

static HRESULT WINAPI bag_query(IPropertyBag *iface, REFIID riid, void **object)
{
    return query(FROM(iface, bag), riid, object);
}

static ULONG WINAPI bag_add_ref(IPropertyBag *iface)
{
    return add_ref(FROM(iface, bag));
}

static ULONG WINAPI bag_release(IPropertyBag *iface)
{
    return release(FROM(iface, bag));
}

/* a missing name fails like other property bags, a typed variant is converted to its type. */
static HRESULT WINAPI bag_read(IPropertyBag *iface, LPCOLESTR name, VARIANT *value, IErrorLog *log)
{
    STORE *store = FROM(iface, bag);
    if (!name || !value)
        return E_POINTER;
    VARTYPE wanted = value->vt;
    PROPVARIANT pv;
    HRESULT hr = get_named(store, name, &pv);
    if (FAILED(hr))
        return hr;
    if (pv.vt == VT_EMPTY)
        return E_INVALIDARG;
    if (wanted != VT_EMPTY && wanted != pv.vt) {
        PROPVARIANT converted;
        hr = PropVariantChangeType(&converted, &pv, PVCHF_DEFAULT, wanted);
        PropVariantClear(&pv);
        if (FAILED(hr))
            return hr;
        pv = converted;
    }
    hr = PropVariantToVariant(&pv, value);
    PropVariantClear(&pv);
    return hr;
}

static HRESULT WINAPI bag_write(IPropertyBag *iface, LPCOLESTR name, VARIANT *value)
{
    if (!name || !value)
        return E_POINTER;
    return set_named(FROM(iface, bag), name, (const PROPVARIANT *)value);
}

static const IPropertyBagVtbl bag_vtbl = {
    bag_query, bag_add_ref, bag_release, bag_read, bag_write,
};

HRESULT WINAPI PSCreateMemoryPropertyStore(REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    STORE *store = PsAllocZero(sizeof(*store));
    if (!store)
        return E_OUTOFMEMORY;
    store->cache.lpVtbl = &cache_vtbl;
    store->named.lpVtbl = &named_vtbl;
    store->serialized.lpVtbl = &serialized_vtbl;
    store->stream.lpVtbl = &stream_vtbl;
    store->bag.lpVtbl = &bag_vtbl;
    store->refs = 1;
    InitializeSRWLock(&store->lock);
    HRESULT hr = query(store, riid, object);
    release(store);
    return hr;
}
