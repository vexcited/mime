/* multiplexed stores, adapters, stores over property sets and the objects that describe changes to properties. */
#include "propsys_private.h"

#define COM_FROM(iface, member, type) ((type *)((BYTE *)(iface) - offsetof(type, member)))

static int compare_key(const PROPERTYKEY *a, const PROPERTYKEY *b)
{
    int d = memcmp(&a->fmtid, &b->fmtid, sizeof(GUID));
    if (d)
        return d;
    return a->pid < b->pid ? -1 : a->pid > b->pid;
}

/* ------------------------------------------------------------------ multiplexed stores */

typedef struct {
    IPropertyStore *store;
    /* a store made when it is first asked for, by the factory with the id. */
    IDelayedPropertyStoreFactory *factory;
    DWORD id;
} SLOT;

typedef struct {
    IPropertyStore store;
    INamedPropertyStore named;
    IPsStoreCapabilities capabilities;
    IPsMultiplexStores multiplex;
    LONG refs;
    CRITICAL_SECTION lock;
    SLOT *slots;
    UINT count, capacity;
    GETPROPERTYSTOREFLAGS flags;
    PROPERTYKEY *keys;
    UINT key_count;
    BOOL keys_valid;
    BSTR *names;
    UINT name_count;
} MUX;

#define MUX_FROM(iface, member) COM_FROM(iface, member, MUX)

/* the store of a slot, made when first needed. */
static IPropertyStore *slot_store(MUX *m, UINT index)
{
    SLOT *s = &m->slots[index];
    if (!s->store && s->factory) {
        IPropertyStore *made = nullptr;
        if (SUCCEEDED(s->factory->lpVtbl->GetDelayedPropertyStore(s->factory, m->flags, s->id, &IID_IPropertyStore,
                                                                  (void **)&made)))
            s->store = made;
        s->factory->lpVtbl->Release(s->factory);
        s->factory = nullptr;
    }
    return s->store;
}

static HRESULT mux_add_slot(MUX *m, IPropertyStore *store, IDelayedPropertyStoreFactory *factory, DWORD id)
{
    if (m->count == m->capacity) {
        UINT grown = m->capacity ? m->capacity * 2 : 4;
        SLOT *more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(SLOT));
        if (!more)
            return E_OUTOFMEMORY;
        if (m->slots) {
            memcpy(more, m->slots, m->count * sizeof(SLOT));
            HeapFree(GetProcessHeap(), 0, m->slots);
        }
        m->slots = more;
        m->capacity = grown;
    }
    m->slots[m->count].store = store;
    m->slots[m->count].factory = factory;
    m->slots[m->count].id = id;
    m->count++;
    return S_OK;
}

static void mux_forget_keys(MUX *m)
{
    HeapFree(GetProcessHeap(), 0, m->keys);
    m->keys = nullptr;
    m->key_count = 0;
    m->keys_valid = FALSE;
    for (UINT i = 0; i < m->name_count; i++)
        SysFreeString(m->names[i]);
    HeapFree(GetProcessHeap(), 0, m->names);
    m->names = nullptr;
    m->name_count = 0;
}

static HRESULT mux_add_store(MUX *m, IUnknown *unknown)
{
    if (!unknown)
        return E_POINTER;
    IPropertyStore *store = nullptr;
    HRESULT hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IPropertyStore, (void **)&store);
    if (FAILED(hr)) {
        IPropertySetStorage *sets;
        hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IPsPropertySetStorage, (void **)&sets);
        if (SUCCEEDED(hr)) {
            hr = PSCreatePropertyStoreFromPropertySetStorage(sets, STGM_READ, &IID_IPropertyStore, (void **)&store);
            sets->lpVtbl->Release(sets);
        }
    }
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&m->lock);
    hr = mux_add_slot(m, store, nullptr, 0);
    if (SUCCEEDED(hr))
        mux_forget_keys(m);
    else
        store->lpVtbl->Release(store);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT mux_query(MUX *m, REFIID riid, void **object)
{
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore))
        *object = &m->store;
    else if (IsEqualIID(riid, &IID_INamedPropertyStore))
        *object = &m->named;
    else if (IsEqualIID(riid, &IID_IPsStoreCapabilities))
        *object = &m->capabilities;
    else if (IsEqualIID(riid, &IID_IPsMultiplexStores))
        *object = &m->multiplex;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&m->refs);
    return S_OK;
}

static ULONG mux_release(MUX *m)
{
    LONG refs = InterlockedDecrement(&m->refs);
    if (!refs) {
        for (UINT i = 0; i < m->count; i++) {
            if (m->slots[i].store)
                m->slots[i].store->lpVtbl->Release(m->slots[i].store);
            if (m->slots[i].factory)
                m->slots[i].factory->lpVtbl->Release(m->slots[i].factory);
        }
        mux_forget_keys(m);
        HeapFree(GetProcessHeap(), 0, m->slots);
        DeleteCriticalSection(&m->lock);
        HeapFree(GetProcessHeap(), 0, m);
    }
    return refs;
}

/* every key of every store once, in key order. */
static HRESULT mux_build_keys(MUX *m)
{
    if (m->keys_valid)
        return S_OK;
    UINT total = 0;
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        DWORD n = 0;
        if (s && SUCCEEDED(s->lpVtbl->GetCount(s, &n)))
            total += n;
    }
    PROPERTYKEY *keys = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(PROPERTYKEY));
    if (!keys)
        return E_OUTOFMEMORY;
    UINT count = 0;
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        DWORD n = 0;
        if (!s || FAILED(s->lpVtbl->GetCount(s, &n)))
            continue;
        for (DWORD j = 0; j < n; j++) {
            PROPERTYKEY key;
            if (FAILED(s->lpVtbl->GetAt(s, j, &key)))
                continue;
            UINT at = 0;
            while (at < count && compare_key(&keys[at], &key) < 0)
                at++;
            if (at < count && !compare_key(&keys[at], &key))
                continue;
            memmove(&keys[at + 1], &keys[at], (count - at) * sizeof(PROPERTYKEY));
            keys[at] = key;
            count++;
        }
    }
    /* the names the stores with names have. */
    UINT name_total = 0;
    for (UINT i = 0; i < m->count; i++) {
        INamedPropertyStore *named;
        IPropertyStore *s = slot_store(m, i);
        DWORD n = 0;
        if (s && SUCCEEDED(s->lpVtbl->QueryInterface(s, &IID_INamedPropertyStore, (void **)&named))) {
            if (SUCCEEDED(named->lpVtbl->GetNameCount(named, &n)))
                name_total += n;
            named->lpVtbl->Release(named);
        }
    }
    BSTR *names = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (name_total + 1) * sizeof(BSTR));
    UINT name_count = 0;
    for (UINT i = 0; names && i < m->count; i++) {
        INamedPropertyStore *named;
        IPropertyStore *s = slot_store(m, i);
        DWORD n = 0;
        if (!s || FAILED(s->lpVtbl->QueryInterface(s, &IID_INamedPropertyStore, (void **)&named)))
            continue;
        if (SUCCEEDED(named->lpVtbl->GetNameCount(named, &n)))
            for (DWORD j = 0; j < n; j++) {
                BSTR name = nullptr;
                BOOL known = FALSE;
                if (FAILED(named->lpVtbl->GetNameAt(named, j, &name)))
                    continue;
                for (UINT k = 0; k < name_count; k++)
                    if (!_wcsicmp(names[k], name))
                        known = TRUE;
                if (known)
                    SysFreeString(name);
                else
                    names[name_count++] = name;
            }
        named->lpVtbl->Release(named);
    }
    m->keys = keys;
    m->key_count = count;
    m->names = names;
    m->name_count = name_count;
    m->keys_valid = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE m_query_interface(IPropertyStore *This, REFIID riid, void **object)
{
    return mux_query(MUX_FROM(This, store), riid, object);
}
static ULONG STDMETHODCALLTYPE m_add_ref(IPropertyStore *This)
{
    return InterlockedIncrement(&MUX_FROM(This, store)->refs);
}
static ULONG STDMETHODCALLTYPE m_release(IPropertyStore *This)
{
    return mux_release(MUX_FROM(This, store));
}

static HRESULT STDMETHODCALLTYPE m_get_count(IPropertyStore *This, DWORD *count)
{
    MUX *m = MUX_FROM(This, store);
    *count = 0;
    EnterCriticalSection(&m->lock);
    HRESULT hr = mux_build_keys(m);
    if (SUCCEEDED(hr))
        *count = m->key_count;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE m_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    MUX *m = MUX_FROM(This, store);
    memset(key, 0, sizeof(*key));
    EnterCriticalSection(&m->lock);
    HRESULT hr = mux_build_keys(m);
    if (SUCCEEDED(hr)) {
        if (index < m->key_count)
            *key = m->keys[index];
        else
            hr = E_INVALIDARG;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE m_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    MUX *m = MUX_FROM(This, store);
    memset(value, 0, sizeof(*value));
    EnterCriticalSection(&m->lock);
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        PROPVARIANT v = {};
        if (s && SUCCEEDED(s->lpVtbl->GetValue(s, key, &v)) && v.vt != VT_EMPTY) {
            *value = v;
            break;
        }
        PropVariantClear(&v);
    }
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

/* a write goes to the first store that says the property can be written. */
static HRESULT STDMETHODCALLTYPE m_set_value(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    MUX *m = MUX_FROM(This, store);
    HRESULT hr = STG_E_ACCESSDENIED;
    EnterCriticalSection(&m->lock);
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        IPsStoreCapabilities *caps;
        if (!s)
            continue;
        if (SUCCEEDED(s->lpVtbl->QueryInterface(s, &IID_IPsStoreCapabilities, (void **)&caps))) {
            HRESULT writable = caps->lpVtbl->IsPropertyWritable(caps, key);
            caps->lpVtbl->Release(caps);
            if (writable != S_OK)
                continue;
        }
        hr = s->lpVtbl->SetValue(s, key, value);
        break;
    }
    if (SUCCEEDED(hr))
        mux_forget_keys(m);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE m_commit(IPropertyStore *This)
{
    MUX *m = MUX_FROM(This, store);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        HRESULT one = s ? s->lpVtbl->Commit(s) : S_OK;
        if (FAILED(one) && SUCCEEDED(hr))
            hr = one;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static const IPropertyStoreVtbl mux_store_vtbl = {m_query_interface, m_add_ref, m_release, m_get_count, m_get_at,
                                                  m_get_value, m_set_value, m_commit};

static HRESULT STDMETHODCALLTYPE mn_query_interface(INamedPropertyStore *This, REFIID riid, void **object)
{
    return mux_query(MUX_FROM(This, named), riid, object);
}
static ULONG STDMETHODCALLTYPE mn_add_ref(INamedPropertyStore *This)
{
    return InterlockedIncrement(&MUX_FROM(This, named)->refs);
}
static ULONG STDMETHODCALLTYPE mn_release(INamedPropertyStore *This)
{
    return mux_release(MUX_FROM(This, named));
}
static HRESULT STDMETHODCALLTYPE mn_get_named(INamedPropertyStore *This, LPCWSTR name, PROPVARIANT *value)
{
    MUX *m = MUX_FROM(This, named);
    memset(value, 0, sizeof(*value));
    EnterCriticalSection(&m->lock);
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        INamedPropertyStore *named;
        if (!s || FAILED(s->lpVtbl->QueryInterface(s, &IID_INamedPropertyStore, (void **)&named)))
            continue;
        PROPVARIANT v = {};
        HRESULT hr = named->lpVtbl->GetNamedValue(named, name, &v);
        named->lpVtbl->Release(named);
        if (SUCCEEDED(hr) && v.vt != VT_EMPTY) {
            *value = v;
            break;
        }
        PropVariantClear(&v);
    }
    LeaveCriticalSection(&m->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mn_set_named(INamedPropertyStore *This, LPCWSTR name, const PROPVARIANT *value)
{
    MUX *m = MUX_FROM(This, named);
    HRESULT hr = STG_E_ACCESSDENIED;
    EnterCriticalSection(&m->lock);
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        INamedPropertyStore *named;
        if (!s || FAILED(s->lpVtbl->QueryInterface(s, &IID_INamedPropertyStore, (void **)&named)))
            continue;
        hr = named->lpVtbl->SetNamedValue(named, name, value);
        named->lpVtbl->Release(named);
        break;
    }
    if (SUCCEEDED(hr))
        mux_forget_keys(m);
    LeaveCriticalSection(&m->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE mn_get_count(INamedPropertyStore *This, DWORD *count)
{
    MUX *m = MUX_FROM(This, named);
    *count = 0;
    EnterCriticalSection(&m->lock);
    HRESULT hr = mux_build_keys(m);
    if (SUCCEEDED(hr))
        *count = m->name_count;
    LeaveCriticalSection(&m->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE mn_get_at(INamedPropertyStore *This, DWORD index, BSTR *name)
{
    MUX *m = MUX_FROM(This, named);
    *name = nullptr;
    EnterCriticalSection(&m->lock);
    HRESULT hr = mux_build_keys(m);
    if (SUCCEEDED(hr)) {
        if (index < m->name_count) {
            *name = SysAllocString(m->names[index]);
            if (!*name)
                hr = E_OUTOFMEMORY;
        } else
            hr = E_INVALIDARG;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static const INamedPropertyStoreVtbl mux_named_vtbl = {mn_query_interface, mn_add_ref, mn_release, mn_get_named,
                                                       mn_set_named, mn_get_count, mn_get_at};

static HRESULT STDMETHODCALLTYPE mc_query_interface(IPsStoreCapabilities *This, REFIID riid, void **object)
{
    return mux_query(MUX_FROM(This, capabilities), riid, object);
}
static ULONG STDMETHODCALLTYPE mc_add_ref(IPsStoreCapabilities *This)
{
    return InterlockedIncrement(&MUX_FROM(This, capabilities)->refs);
}
static ULONG STDMETHODCALLTYPE mc_release(IPsStoreCapabilities *This)
{
    return mux_release(MUX_FROM(This, capabilities));
}
static HRESULT STDMETHODCALLTYPE mc_writable(IPsStoreCapabilities *This, REFPROPERTYKEY key)
{
    MUX *m = MUX_FROM(This, capabilities);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    for (UINT i = 0; i < m->count; i++) {
        IPropertyStore *s = slot_store(m, i);
        IPsStoreCapabilities *caps;
        if (s && SUCCEEDED(s->lpVtbl->QueryInterface(s, &IID_IPsStoreCapabilities, (void **)&caps))) {
            hr = caps->lpVtbl->IsPropertyWritable(caps, key);
            caps->lpVtbl->Release(caps);
            break;
        }
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static const IPsStoreCapabilitiesVtbl mux_capabilities_vtbl = {mc_query_interface, mc_add_ref, mc_release, mc_writable};

static HRESULT STDMETHODCALLTYPE mm_query_interface(IPsMultiplexStores *This, REFIID riid, void **object)
{
    return mux_query(MUX_FROM(This, multiplex), riid, object);
}
static ULONG STDMETHODCALLTYPE mm_add_ref(IPsMultiplexStores *This)
{
    return InterlockedIncrement(&MUX_FROM(This, multiplex)->refs);
}
static ULONG STDMETHODCALLTYPE mm_release(IPsMultiplexStores *This)
{
    return mux_release(MUX_FROM(This, multiplex));
}
static HRESULT STDMETHODCALLTYPE mm_add_store(IPsMultiplexStores *This, IUnknown *store)
{
    return mux_add_store(MUX_FROM(This, multiplex), store);
}

static const IPsMultiplexStoresVtbl mux_multiplex_vtbl = {mm_query_interface, mm_add_ref, mm_release, mm_add_store};

static MUX *mux_create(void)
{
    MUX *m = PsAllocZero(sizeof(*m));
    if (!m)
        return nullptr;
    m->store.lpVtbl = &mux_store_vtbl;
    m->named.lpVtbl = &mux_named_vtbl;
    m->capabilities.lpVtbl = &mux_capabilities_vtbl;
    m->multiplex.lpVtbl = &mux_multiplex_vtbl;
    m->refs = 1;
    InitializeCriticalSection(&m->lock);
    return m;
}

HRESULT MultiplexCreate(REFIID riid, void **object)
{
    MUX *m = mux_create();
    *object = nullptr;
    if (!m)
        return E_OUTOFMEMORY;
    HRESULT hr = mux_query(m, riid, object);
    mux_release(m);
    return hr;
}

HRESULT WINAPI PSCreateMultiplexPropertyStore(IUnknown **stores, DWORD count, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!stores)
        return E_POINTER;
    MUX *m = mux_create();
    if (!m)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++)
        hr = mux_add_store(m, stores[i]);
    if (SUCCEEDED(hr))
        hr = mux_query(m, riid, object);
    mux_release(m);
    return hr;
}

HRESULT WINAPI PSCreateDelayedMultiplexPropertyStore(GETPROPERTYSTOREFLAGS flags, IDelayedPropertyStoreFactory *factory,
                                                     const DWORD *ids, DWORD count, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!factory || !ids || !count)
        return E_POINTER;
    MUX *m = mux_create();
    if (!m)
        return E_OUTOFMEMORY;
    m->flags = flags;
    HRESULT hr = S_OK;
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++) {
        factory->lpVtbl->AddRef(factory);
        hr = mux_add_slot(m, nullptr, factory, ids[i]);
        if (FAILED(hr))
            factory->lpVtbl->Release(factory);
    }
    if (SUCCEEDED(hr))
        hr = mux_query(m, riid, object);
    mux_release(m);
    return hr;
}

/* ------------------------------------------------------------------ adapter over a store */

typedef struct {
    IPropertyStore store;
    INamedPropertyStore named;
    IPsStoreCapabilities capabilities;
    LONG refs;
    IPropertyStore *inner;
} ADAPTER;

#define AD_FROM(iface, member) COM_FROM(iface, member, ADAPTER)

static HRESULT ad_query(ADAPTER *a, REFIID riid, void **object)
{
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore))
        *object = &a->store;
    else if (IsEqualIID(riid, &IID_INamedPropertyStore))
        *object = &a->named;
    else if (IsEqualIID(riid, &IID_IPsStoreCapabilities))
        *object = &a->capabilities;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&a->refs);
    return S_OK;
}
static ULONG ad_release(ADAPTER *a)
{
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        a->inner->lpVtbl->Release(a->inner);
        HeapFree(GetProcessHeap(), 0, a);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE a_query_interface(IPropertyStore *This, REFIID riid, void **object)
{
    return ad_query(AD_FROM(This, store), riid, object);
}
static ULONG STDMETHODCALLTYPE a_add_ref(IPropertyStore *This)
{
    return InterlockedIncrement(&AD_FROM(This, store)->refs);
}
static ULONG STDMETHODCALLTYPE a_release(IPropertyStore *This)
{
    return ad_release(AD_FROM(This, store));
}
static HRESULT STDMETHODCALLTYPE a_get_count(IPropertyStore *This, DWORD *count)
{
    ADAPTER *a = AD_FROM(This, store);
    return a->inner->lpVtbl->GetCount(a->inner, count);
}
static HRESULT STDMETHODCALLTYPE a_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    ADAPTER *a = AD_FROM(This, store);
    return a->inner->lpVtbl->GetAt(a->inner, index, key);
}
static HRESULT STDMETHODCALLTYPE a_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    ADAPTER *a = AD_FROM(This, store);
    return a->inner->lpVtbl->GetValue(a->inner, key, value);
}
static HRESULT STDMETHODCALLTYPE a_set_value(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    ADAPTER *a = AD_FROM(This, store);
    return a->inner->lpVtbl->SetValue(a->inner, key, value);
}
static HRESULT STDMETHODCALLTYPE a_commit(IPropertyStore *This)
{
    ADAPTER *a = AD_FROM(This, store);
    return a->inner->lpVtbl->Commit(a->inner);
}
static const IPropertyStoreVtbl adapter_store_vtbl = {a_query_interface, a_add_ref, a_release, a_get_count, a_get_at,
                                                      a_get_value, a_set_value, a_commit};

static HRESULT STDMETHODCALLTYPE an_query_interface(INamedPropertyStore *This, REFIID riid, void **object)
{
    return ad_query(AD_FROM(This, named), riid, object);
}
static ULONG STDMETHODCALLTYPE an_add_ref(INamedPropertyStore *This)
{
    return InterlockedIncrement(&AD_FROM(This, named)->refs);
}
static ULONG STDMETHODCALLTYPE an_release(INamedPropertyStore *This)
{
    return ad_release(AD_FROM(This, named));
}

/* the named values of the wrapped store when it has them. */
static HRESULT inner_named(ADAPTER *a, INamedPropertyStore **named)
{
    return a->inner->lpVtbl->QueryInterface(a->inner, &IID_INamedPropertyStore, (void **)named);
}
static HRESULT STDMETHODCALLTYPE an_get_named(INamedPropertyStore *This, LPCWSTR name, PROPVARIANT *value)
{
    INamedPropertyStore *n;
    memset(value, 0, sizeof(*value));
    if (FAILED(inner_named(AD_FROM(This, named), &n)))
        return S_OK;
    HRESULT hr = n->lpVtbl->GetNamedValue(n, name, value);
    n->lpVtbl->Release(n);
    return hr;
}
static HRESULT STDMETHODCALLTYPE an_set_named(INamedPropertyStore *This, LPCWSTR name, const PROPVARIANT *value)
{
    INamedPropertyStore *n;
    if (FAILED(inner_named(AD_FROM(This, named), &n)))
        return STG_E_ACCESSDENIED;
    HRESULT hr = n->lpVtbl->SetNamedValue(n, name, value);
    n->lpVtbl->Release(n);
    return hr;
}
static HRESULT STDMETHODCALLTYPE an_get_count(INamedPropertyStore *This, DWORD *count)
{
    INamedPropertyStore *n;
    *count = 0;
    if (FAILED(inner_named(AD_FROM(This, named), &n)))
        return S_OK;
    HRESULT hr = n->lpVtbl->GetNameCount(n, count);
    n->lpVtbl->Release(n);
    return hr;
}
static HRESULT STDMETHODCALLTYPE an_get_at(INamedPropertyStore *This, DWORD index, BSTR *name)
{
    INamedPropertyStore *n;
    *name = nullptr;
    if (FAILED(inner_named(AD_FROM(This, named), &n)))
        return E_INVALIDARG;
    HRESULT hr = n->lpVtbl->GetNameAt(n, index, name);
    n->lpVtbl->Release(n);
    return hr;
}
static const INamedPropertyStoreVtbl adapter_named_vtbl = {an_query_interface, an_add_ref, an_release, an_get_named,
                                                           an_set_named, an_get_count, an_get_at};

static HRESULT STDMETHODCALLTYPE ac_query_interface(IPsStoreCapabilities *This, REFIID riid, void **object)
{
    return ad_query(AD_FROM(This, capabilities), riid, object);
}
static ULONG STDMETHODCALLTYPE ac_add_ref(IPsStoreCapabilities *This)
{
    return InterlockedIncrement(&AD_FROM(This, capabilities)->refs);
}
static ULONG STDMETHODCALLTYPE ac_release(IPsStoreCapabilities *This)
{
    return ad_release(AD_FROM(This, capabilities));
}
static HRESULT STDMETHODCALLTYPE ac_writable(IPsStoreCapabilities *This, REFPROPERTYKEY key)
{
    ADAPTER *a = AD_FROM(This, capabilities);
    IPsStoreCapabilities *caps;
    if (FAILED(a->inner->lpVtbl->QueryInterface(a->inner, &IID_IPsStoreCapabilities, (void **)&caps)))
        return S_OK;
    HRESULT hr = caps->lpVtbl->IsPropertyWritable(caps, key);
    caps->lpVtbl->Release(caps);
    return hr;
}
static const IPsStoreCapabilitiesVtbl adapter_capabilities_vtbl = {ac_query_interface, ac_add_ref, ac_release,
                                                                   ac_writable};

HRESULT WINAPI PSCreateAdapterFromPropertyStore(IPropertyStore *store, REFIID riid, void **object)
{
    if (!store || !object)
        return E_POINTER;
    *object = nullptr;
    ADAPTER *a = PsAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->store.lpVtbl = &adapter_store_vtbl;
    a->named.lpVtbl = &adapter_named_vtbl;
    a->capabilities.lpVtbl = &adapter_capabilities_vtbl;
    a->refs = 1;
    a->inner = store;
    store->lpVtbl->AddRef(store);
    HRESULT hr = ad_query(a, riid, object);
    ad_release(a);
    return hr;
}

/* ------------------------------------------------------------------ a store over property sets */

typedef struct {
    IPropertyStore store;
    IPsStoreCapabilities capabilities;
    LONG refs;
    CRITICAL_SECTION lock;
    IPropertySetStorage *sets;
    DWORD mode;
    PROPERTYKEY *keys;
    UINT key_count;
    BOOL keys_valid;
} SETSTORE;

#define SS_FROM(iface, member) COM_FROM(iface, member, SETSTORE)

static HRESULT ss_query(SETSTORE *s, REFIID riid, void **object)
{
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore))
        *object = &s->store;
    else if (IsEqualIID(riid, &IID_IPsStoreCapabilities))
        *object = &s->capabilities;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&s->refs);
    return S_OK;
}
static ULONG ss_release(SETSTORE *s)
{
    LONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        s->sets->lpVtbl->Release(s->sets);
        HeapFree(GetProcessHeap(), 0, s->keys);
        DeleteCriticalSection(&s->lock);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE s_query_interface(IPropertyStore *This, REFIID riid, void **object)
{
    return ss_query(SS_FROM(This, store), riid, object);
}
static ULONG STDMETHODCALLTYPE s_add_ref(IPropertyStore *This)
{
    return InterlockedIncrement(&SS_FROM(This, store)->refs);
}
static ULONG STDMETHODCALLTYPE s_release(IPropertyStore *This)
{
    return ss_release(SS_FROM(This, store));
}

static HRESULT ss_build_keys(SETSTORE *s)
{
    if (s->keys_valid)
        return S_OK;
    IPsEnumProp *sets;
    HRESULT hr = s->sets->lpVtbl->Enum(s->sets, &sets);
    if (FAILED(hr))
        return hr;
    PROPERTYKEY *keys = nullptr;
    UINT count = 0, capacity = 0;
    PS_STATPROPSETSTG set;
    ULONG got;
    while (sets->lpVtbl->Next(sets, 1, &set, &got) == S_OK && got == 1) {
        IPsPropertyStorage *storage;
        if (FAILED(s->sets->lpVtbl->Open(s->sets, &set.fmtid, STGM_READ | STGM_SHARE_EXCLUSIVE, &storage)))
            continue;
        IPsEnumProp *props;
        if (SUCCEEDED(storage->lpVtbl->Enum(storage, &props))) {
            PS_STATPROPSTG stat;
            while (props->lpVtbl->Next(props, 1, &stat, &got) == S_OK && got == 1) {
                if (stat.name)
                    CoTaskMemFree(stat.name);
                if (count == capacity) {
                    UINT grown = capacity ? capacity * 2 : 16;
                    PROPERTYKEY *more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(PROPERTYKEY));
                    if (!more)
                        break;
                    if (keys) {
                        memcpy(more, keys, count * sizeof(PROPERTYKEY));
                        HeapFree(GetProcessHeap(), 0, keys);
                    }
                    keys = more;
                    capacity = grown;
                }
                keys[count].fmtid = set.fmtid;
                keys[count++].pid = stat.propid;
            }
            props->lpVtbl->Release(props);
        }
        storage->lpVtbl->Release(storage);
    }
    sets->lpVtbl->Release(sets);
    s->keys = keys;
    s->key_count = count;
    s->keys_valid = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_get_count(IPropertyStore *This, DWORD *count)
{
    SETSTORE *s = SS_FROM(This, store);
    *count = 0;
    EnterCriticalSection(&s->lock);
    HRESULT hr = ss_build_keys(s);
    if (SUCCEEDED(hr))
        *count = s->key_count;
    LeaveCriticalSection(&s->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE s_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    SETSTORE *s = SS_FROM(This, store);
    memset(key, 0, sizeof(*key));
    EnterCriticalSection(&s->lock);
    HRESULT hr = ss_build_keys(s);
    if (SUCCEEDED(hr)) {
        if (index < s->key_count)
            *key = s->keys[index];
        else
            hr = E_INVALIDARG;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE s_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    SETSTORE *s = SS_FROM(This, store);
    IPsPropertyStorage *storage;
    memset(value, 0, sizeof(*value));
    HRESULT hr = s->sets->lpVtbl->Open(s->sets, &key->fmtid, STGM_READ | STGM_SHARE_EXCLUSIVE, &storage);
    if (FAILED(hr))
        return S_OK;
    PS_PROPSPEC spec;
    spec.kind = PS_PRSPEC_PROPID;
    spec.propid = key->pid;
    hr = storage->lpVtbl->ReadMultiple(storage, 1, &spec, value);
    storage->lpVtbl->Release(storage);
    if (FAILED(hr))
        memset(value, 0, sizeof(*value));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_set_value(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    SETSTORE *s = SS_FROM(This, store);
    IPsPropertyStorage *storage;
    if (!(s->mode & (STGM_WRITE | STGM_READWRITE)))
        return STG_E_ACCESSDENIED;
    HRESULT hr = s->sets->lpVtbl->Open(s->sets, &key->fmtid, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, &storage);
    if (FAILED(hr))
        hr = s->sets->lpVtbl->Create(s->sets, &key->fmtid, nullptr, 0, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, &storage);
    if (FAILED(hr))
        return hr;
    PS_PROPSPEC spec;
    spec.kind = PS_PRSPEC_PROPID;
    spec.propid = key->pid;
    hr = storage->lpVtbl->WriteMultiple(storage, 1, &spec, value, 2);
    if (SUCCEEDED(hr))
        storage->lpVtbl->Commit(storage, STGC_DEFAULT);
    storage->lpVtbl->Release(storage);
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&s->lock);
        s->keys_valid = FALSE;
        LeaveCriticalSection(&s->lock);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE s_commit(IPropertyStore *This)
{
    return S_OK;
}
static const IPropertyStoreVtbl set_store_vtbl = {s_query_interface, s_add_ref, s_release, s_get_count, s_get_at,
                                                  s_get_value, s_set_value, s_commit};

static HRESULT STDMETHODCALLTYPE sc_query_interface(IPsStoreCapabilities *This, REFIID riid, void **object)
{
    return ss_query(SS_FROM(This, capabilities), riid, object);
}
static ULONG STDMETHODCALLTYPE sc_add_ref(IPsStoreCapabilities *This)
{
    return InterlockedIncrement(&SS_FROM(This, capabilities)->refs);
}
static ULONG STDMETHODCALLTYPE sc_release(IPsStoreCapabilities *This)
{
    return ss_release(SS_FROM(This, capabilities));
}
static HRESULT STDMETHODCALLTYPE sc_writable(IPsStoreCapabilities *This, REFPROPERTYKEY key)
{
    SETSTORE *s = SS_FROM(This, capabilities);
    return s->mode & (STGM_WRITE | STGM_READWRITE) ? S_OK : S_FALSE;
}
static const IPsStoreCapabilitiesVtbl set_capabilities_vtbl = {sc_query_interface, sc_add_ref, sc_release,
                                                               sc_writable};

HRESULT WINAPI PSCreatePropertyStoreFromPropertySetStorage(IPropertySetStorage *sets, DWORD mode, REFIID riid,
                                                           void **object)
{
    if (!sets || !object)
        return E_POINTER;
    *object = nullptr;
    SETSTORE *s = PsAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->store.lpVtbl = &set_store_vtbl;
    s->capabilities.lpVtbl = &set_capabilities_vtbl;
    s->refs = 1;
    InitializeCriticalSection(&s->lock);
    s->sets = sets;
    sets->lpVtbl->AddRef(sets);
    s->mode = mode;
    HRESULT hr = ss_query(s, riid, object);
    ss_release(s);
    return hr;
}

HRESULT WINAPI PSCreatePropertyStoreFromObject(IUnknown *object, DWORD mode, REFIID riid, void **result)
{
    if (!result || !object)
        return E_POINTER;
    *result = nullptr;
    IPropertyStore *store;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IPropertyStore, (void **)&store))) {
        HRESULT hr = store->lpVtbl->QueryInterface(store, riid, result);
        store->lpVtbl->Release(store);
        return hr;
    }
    IPropertySetStorage *sets;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_IPsPropertySetStorage, (void **)&sets);
    if (FAILED(hr))
        return hr;
    hr = PSCreatePropertyStoreFromPropertySetStorage(sets, mode, riid, result);
    sets->lpVtbl->Release(sets);
    return hr;
}

/* ------------------------------------------------------------------ property changes */

typedef struct {
    IPropertyChange change;
    LONG refs;
    SRWLOCK lock;
    PKA_FLAGS action;
    PROPERTYKEY key;
    PROPVARIANT value;
} CHANGE;

static HRESULT ch_query(CHANGE *c, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertyChange) &&
        !IsEqualIID(riid, &IID_IObjectWithPropertyKey))
        return E_NOINTERFACE;
    *object = &c->change;
    InterlockedIncrement(&c->refs);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ch_query_interface(IPropertyChange *This, REFIID riid, void **object)
{
    return ch_query((CHANGE *)This, riid, object);
}
static ULONG STDMETHODCALLTYPE ch_add_ref(IPropertyChange *This)
{
    return InterlockedIncrement(&((CHANGE *)This)->refs);
}
static ULONG STDMETHODCALLTYPE ch_release(IPropertyChange *This)
{
    CHANGE *c = (CHANGE *)This;
    LONG refs = InterlockedDecrement(&c->refs);
    if (!refs) {
        PropVariantClear(&c->value);
        HeapFree(GetProcessHeap(), 0, c);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE ch_set_key(IPropertyChange *This, REFPROPERTYKEY key)
{
    CHANGE *c = (CHANGE *)This;
    AcquireSRWLockExclusive(&c->lock);
    c->key = *key;
    ReleaseSRWLockExclusive(&c->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ch_get_key(IPropertyChange *This, PROPERTYKEY *key)
{
    CHANGE *c = (CHANGE *)This;
    AcquireSRWLockShared(&c->lock);
    *key = c->key;
    ReleaseSRWLockShared(&c->lock);
    return S_OK;
}

/* the elements of a value as a vector of the type of the elements, built from a scalar or a vector. */
static HRESULT as_elements(const PROPVARIANT *source, PROPVARIANT *elements)
{
    memset(elements, 0, sizeof(*elements));
    if (source->vt == VT_EMPTY)
        return S_OK;
    if (source->vt & VT_VECTOR)
        return PropVariantCopy(elements, source);
    return InitPropVariantVectorFromPropVariant(source, elements);
}

static BOOL has_element(const PROPVARIANT *vector, const PROPVARIANT *element)
{
    ULONG n = PropVariantGetElementCount(vector);
    for (ULONG i = 0; i < n; i++) {
        PROPVARIANT one = {};
        BOOL same = FALSE;
        if (SUCCEEDED(InitPropVariantFromPropVariantVectorElem(vector, i, &one)))
            same = element->vt == one.vt && !PropVariantCompareEx(element, &one, PVCU_DEFAULT, 0);
        PropVariantClear(&one);
        if (same)
            return TRUE;
    }
    return FALSE;
}

/* the elements of the source, with those of the change added or taken away. */
static HRESULT merge_values(PKA_FLAGS action, const PROPVARIANT *source, const PROPVARIANT *change, PROPVARIANT *out)
{
    PROPVARIANT base, extra;
    HRESULT hr = as_elements(source, &base);
    if (FAILED(hr))
        return hr;
    hr = as_elements(change, &extra);
    if (FAILED(hr)) {
        PropVariantClear(&base);
        return hr;
    }
    VARTYPE vt = extra.vt ? extra.vt : base.vt;
    ULONG have = PropVariantGetElementCount(&base), more = PropVariantGetElementCount(&extra);
    memset(out, 0, sizeof(*out));
    if (!vt) {
        PropVariantClear(&base);
        PropVariantClear(&extra);
        return S_OK;
    }
    if (base.vt && base.vt != extra.vt && extra.vt) {
        PROPVARIANT converted = {};
        if (SUCCEEDED(PropVariantChangeType(&converted, &base, 0, extra.vt))) {
            PropVariantClear(&base);
            base = converted;
        }
    }
    /* collect element by element through single element vectors. */
    PROPVARIANT *items = HeapAlloc(GetProcessHeap(), 0, (have + more + 1) * sizeof(PROPVARIANT));
    ULONG n = 0;
    if (!items) {
        PropVariantClear(&base);
        PropVariantClear(&extra);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < have; i++) {
        PROPVARIANT one = {};
        if (FAILED(InitPropVariantFromPropVariantVectorElem(&base, i, &one)))
            continue;
        if (action == PKA_DELETE && has_element(&extra, &one)) {
            PropVariantClear(&one);
            continue;
        }
        items[n++] = one;
    }
    if (action == PKA_APPEND)
        for (ULONG i = 0; i < more; i++) {
            PROPVARIANT one = {};
            if (FAILED(InitPropVariantFromPropVariantVectorElem(&extra, i, &one)))
                continue;
            BOOL dup = FALSE;
            for (ULONG j = 0; j < n; j++)
                if (items[j].vt == one.vt && !PropVariantCompareEx(&items[j], &one, PVCU_DEFAULT, 0))
                    dup = TRUE;
            if (dup)
                PropVariantClear(&one);
            else
                items[n++] = one;
        }
    /* build the vector of the result. */
    if (n) {
        VARTYPE element = vt & VT_TYPEMASK;
        UINT size = PsElementSize(element);
        void *array = size ? PsAllocZero((SIZE_T)size * n) : nullptr;
        if (!array) {
            hr = E_OUTOFMEMORY;
        } else {
            for (ULONG i = 0; i < n; i++) {
                BYTE *slot = (BYTE *)array + (SIZE_T)i * size;
                switch (element) {
                case VT_LPWSTR: *(LPWSTR *)slot = items[i].pwszVal; items[i].vt = VT_EMPTY; break;
                case VT_BSTR: *(BSTR *)slot = items[i].bstrVal; items[i].vt = VT_EMPTY; break;
                case VT_LPSTR: *(LPSTR *)slot = items[i].pszVal; items[i].vt = VT_EMPTY; break;
                default: memcpy(slot, &items[i].boolVal, size); break;
                }
            }
            out->vt = (VARTYPE)(element | VT_VECTOR);
            out->cac.cElems = n;
            out->cac.pElems = array;
        }
    }
    for (ULONG i = 0; i < n; i++)
        PropVariantClear(&items[i]);
    HeapFree(GetProcessHeap(), 0, items);
    PropVariantClear(&base);
    PropVariantClear(&extra);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ch_apply(IPropertyChange *This, REFPROPVARIANT source, PROPVARIANT *dest)
{
    CHANGE *c = (CHANGE *)This;
    memset(dest, 0, sizeof(*dest));
    AcquireSRWLockShared(&c->lock);
    PKA_FLAGS action = c->action;
    HRESULT hr;
    if (action == PKA_SET)
        hr = PropVariantCopy(dest, &c->value);
    else if (action == PKA_APPEND || action == PKA_DELETE)
        hr = merge_values(action, source, &c->value, dest);
    else
        hr = E_INVALIDARG;
    ReleaseSRWLockShared(&c->lock);
    return hr;
}

static const IPropertyChangeVtbl change_vtbl = {ch_query_interface, ch_add_ref, ch_release, ch_set_key, ch_get_key,
                                                ch_apply};

static HRESULT change_create(PKA_FLAGS action, REFPROPERTYKEY key, const PROPVARIANT *value, REFIID riid,
                             void **object)
{
    CHANGE *c = PsAllocZero(sizeof(*c));
    *object = nullptr;
    if (!c)
        return E_OUTOFMEMORY;
    c->change.lpVtbl = &change_vtbl;
    c->refs = 1;
    InitializeSRWLock(&c->lock);
    c->action = action;
    c->key = *key;
    HRESULT hr = PropVariantCopy(&c->value, value);
    if (SUCCEEDED(hr))
        hr = ch_query(c, riid, object);
    ch_release(&c->change);
    return hr;
}

HRESULT WINAPI PSCreateSimplePropertyChange(PKA_FLAGS action, REFPROPERTYKEY key, REFPROPVARIANT value, REFIID riid,
                                            void **object)
{
    if (!object)
        return E_POINTER;
    return change_create(action, key, value, riid, object);
}

typedef struct {
    IPropertyChangeArray array;
    LONG refs;
    SRWLOCK lock;
    IPropertyChange **items;
    UINT count, capacity;
} CHANGES;

static ULONG STDMETHODCALLTYPE ca_add_ref(IPropertyChangeArray *This)
{
    return InterlockedIncrement(&((CHANGES *)This)->refs);
}
static ULONG STDMETHODCALLTYPE ca_release(IPropertyChangeArray *This)
{
    CHANGES *a = (CHANGES *)This;
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        for (UINT i = 0; i < a->count; i++)
            a->items[i]->lpVtbl->Release(a->items[i]);
        HeapFree(GetProcessHeap(), 0, a->items);
        HeapFree(GetProcessHeap(), 0, a);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE ca_query_interface(IPropertyChangeArray *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertyChangeArray))
        return E_NOINTERFACE;
    *object = This;
    ca_add_ref(This);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ca_get_count(IPropertyChangeArray *This, UINT *count)
{
    CHANGES *a = (CHANGES *)This;
    AcquireSRWLockShared(&a->lock);
    *count = a->count;
    ReleaseSRWLockShared(&a->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ca_get_at(IPropertyChangeArray *This, UINT index, REFIID riid, void **object)
{
    CHANGES *a = (CHANGES *)This;
    *object = nullptr;
    AcquireSRWLockShared(&a->lock);
    HRESULT hr = E_INVALIDARG;
    if (index < a->count)
        hr = a->items[index]->lpVtbl->QueryInterface(a->items[index], riid, object);
    ReleaseSRWLockShared(&a->lock);
    return hr;
}
static HRESULT array_insert(CHANGES *a, UINT index, IPropertyChange *change)
{
    if (a->count == a->capacity) {
        UINT grown = a->capacity ? a->capacity * 2 : 10;
        IPropertyChange **more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*more));
        if (!more)
            return E_OUTOFMEMORY;
        if (a->items) {
            memcpy(more, a->items, a->count * sizeof(*more));
            HeapFree(GetProcessHeap(), 0, a->items);
        }
        a->items = more;
        a->capacity = grown;
    }
    if (index > a->count)
        index = a->count;
    memmove(&a->items[index + 1], &a->items[index], (a->count - index) * sizeof(*a->items));
    a->items[index] = change;
    a->count++;
    change->lpVtbl->AddRef(change);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ca_insert_at(IPropertyChangeArray *This, UINT index, IPropertyChange *change)
{
    CHANGES *a = (CHANGES *)This;
    AcquireSRWLockExclusive(&a->lock);
    HRESULT hr = array_insert(a, index, change);
    ReleaseSRWLockExclusive(&a->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE ca_append(IPropertyChangeArray *This, IPropertyChange *change)
{
    CHANGES *a = (CHANGES *)This;
    AcquireSRWLockExclusive(&a->lock);
    HRESULT hr = array_insert(a, a->count, change);
    ReleaseSRWLockExclusive(&a->lock);
    return hr;
}
static int find_key(CHANGES *a, REFPROPERTYKEY key)
{
    for (UINT i = 0; i < a->count; i++) {
        PROPERTYKEY k;
        if (SUCCEEDED(a->items[i]->lpVtbl->GetPropertyKey(a->items[i], &k)) && !compare_key(&k, key))
            return (int)i;
    }
    return -1;
}
static HRESULT STDMETHODCALLTYPE ca_append_or_replace(IPropertyChangeArray *This, IPropertyChange *change)
{
    CHANGES *a = (CHANGES *)This;
    PROPERTYKEY key;
    HRESULT hr = change->lpVtbl->GetPropertyKey(change, &key);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&a->lock);
    int at = find_key(a, &key);
    if (at < 0)
        hr = array_insert(a, a->count, change);
    else {
        a->items[at]->lpVtbl->Release(a->items[at]);
        a->items[at] = change;
        change->lpVtbl->AddRef(change);
    }
    ReleaseSRWLockExclusive(&a->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE ca_remove_at(IPropertyChangeArray *This, UINT index)
{
    CHANGES *a = (CHANGES *)This;
    IPropertyChange *removed = nullptr;
    AcquireSRWLockExclusive(&a->lock);
    if (index < a->count) {
        removed = a->items[index];
        memmove(&a->items[index], &a->items[index + 1], (a->count - index - 1) * sizeof(*a->items));
        a->count--;
    }
    ReleaseSRWLockExclusive(&a->lock);
    if (!removed)
        return E_FAIL;
    removed->lpVtbl->Release(removed);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ca_is_key_in_array(IPropertyChangeArray *This, REFPROPERTYKEY key)
{
    CHANGES *a = (CHANGES *)This;
    AcquireSRWLockShared(&a->lock);
    int at = find_key(a, key);
    ReleaseSRWLockShared(&a->lock);
    return at < 0 ? S_FALSE : S_OK;
}

static const IPropertyChangeArrayVtbl changes_vtbl = {
    ca_query_interface, ca_add_ref, ca_release, ca_get_count, ca_get_at, ca_insert_at, ca_append, ca_append_or_replace,
    ca_remove_at, ca_is_key_in_array,
};

HRESULT WINAPI PSCreatePropertyChangeArray(const PROPERTYKEY *keys, const PKA_FLAGS *actions, const PROPVARIANT *values,
                                           UINT count, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (count && (!keys || !actions || !values))
        return E_INVALIDARG;
    CHANGES *a = PsAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->array.lpVtbl = &changes_vtbl;
    a->refs = 1;
    InitializeSRWLock(&a->lock);
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        IPropertyChange *change;
        hr = change_create(actions[i], &keys[i], &values[i], &IID_IPropertyChange, (void **)&change);
        if (SUCCEEDED(hr)) {
            hr = array_insert(a, a->count, change);
            change->lpVtbl->Release(change);
        }
    }
    if (SUCCEEDED(hr))
        hr = ca_query_interface(&a->array, riid, object);
    ca_release(&a->array);
    return hr;
}
