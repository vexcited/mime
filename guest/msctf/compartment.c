/*
 * compartments: values that the document managers, the contexts and the thread keep for the programs and text
 * services around them, such as whether the keyboard is open. a compartment is the value and its sinks, which the
 * manager keeps whoever holds an interface to it.
 */
#include "msctf_private.h"

typedef struct ENTRY {
    GUID Guid;
    VARIANT Value;
    BOOL Set;
    SINKS Sinks;
} ENTRY;

struct COMPARTMENTS {
    LONG Refs;
    ENTRY *Items;
    ULONG Count, Capacity;
};

COMPARTMENTS *CompartmentsCreate(void)
{
    COMPARTMENTS *comps = CtfAlloc(sizeof(*comps));
    if (comps)
        comps->Refs = 1;
    return comps;
}

static void comps_release(COMPARTMENTS *comps)
{
    if (InterlockedDecrement(&comps->Refs))
        return;
    for (ULONG i = 0; i < comps->Count; i++) {
        VariantClear(&comps->Items[i].Value);
        SinksFree(&comps->Items[i].Sinks);
    }
    CtfFree(comps->Items);
    CtfFree(comps);
}

void CompartmentsFree(COMPARTMENTS *comps)
{
    if (comps)
        comps_release(comps);
}

static ENTRY *find_entry(COMPARTMENTS *comps, REFGUID guid)
{
    for (ULONG i = 0; i < comps->Count; i++)
        if (IsEqualGUID(&comps->Items[i].Guid, guid))
            return &comps->Items[i];
    return nullptr;
}

static ENTRY *add_entry(COMPARTMENTS *comps, REFGUID guid)
{
    ENTRY *entry = find_entry(comps, guid);
    if (entry)
        return entry;
    if (comps->Count == comps->Capacity) {
        ULONG capacity = comps->Capacity ? comps->Capacity * 2 : 4;
        ENTRY *items = CtfAlloc(capacity * sizeof(ENTRY));
        if (!items)
            return nullptr;
        if (comps->Items)
            memcpy(items, comps->Items, comps->Count * sizeof(ENTRY));
        CtfFree(comps->Items);
        comps->Items = items;
        comps->Capacity = capacity;
    }
    entry = &comps->Items[comps->Count++];
    memset(entry, 0, sizeof(*entry));
    entry->Guid = *guid;
    VariantInit(&entry->Value);
    return entry;
}

/* ------------------------------------------------------------------ one compartment */

typedef struct COMPARTMENT {
    ITfCompartment Iface;
    ITfSource Source;
    LONG Refs;
    COMPARTMENTS *Owner;
    GUID Guid;
} COMPARTMENT;

static HRESULT STDMETHODCALLTYPE cp_query(ITfCompartment *This, REFIID riid, void **object)
{
    COMPARTMENT *cp = (COMPARTMENT *)This;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfCompartment))
        *object = &cp->Iface;
    else if (IsEqualIID(riid, &IID_ITfSource))
        *object = &cp->Source;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&cp->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cp_add_ref(ITfCompartment *This)
{
    return InterlockedIncrement(&((COMPARTMENT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE cp_release(ITfCompartment *This)
{
    COMPARTMENT *cp = (COMPARTMENT *)This;
    LONG refs = InterlockedDecrement(&cp->Refs);
    if (!refs) {
        comps_release(cp->Owner);
        CtfFree(cp);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE cp_set_value(ITfCompartment *This, TfClientId tid, const VARIANT *value)
{
    COMPARTMENT *cp = (COMPARTMENT *)This;
    ENTRY *entry = find_entry(cp->Owner, &cp->Guid);
    if (!value || !entry)
        return E_INVALIDARG;
    /* a compartment holds numbers, strings and objects, and is cleared with ClearCompartment. */
    if (V_VT(value) != VT_I4 && V_VT(value) != VT_BSTR && V_VT(value) != VT_UNKNOWN)
        return E_INVALIDARG;
    VARIANT copy;
    VariantInit(&copy);
    HRESULT hr = VariantCopy(&copy, value);
    if (FAILED(hr))
        return hr;
    VariantClear(&entry->Value);
    entry->Value = copy;
    entry->Set = TRUE;
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&entry->Sinks, &IID_ITfCompartmentEventSink, &sinks);
    GUID guid = cp->Guid;
    for (ULONG i = 0; i < n; i++) {
        ITfCompartmentEventSink *sink = (ITfCompartmentEventSink *)sinks[i];
        sink->lpVtbl->OnChange(sink, &guid);
    }
    SinksSnapshotFree(sinks, n);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_get_value(ITfCompartment *This, VARIANT *value)
{
    COMPARTMENT *cp = (COMPARTMENT *)This;
    ENTRY *entry = find_entry(cp->Owner, &cp->Guid);
    if (!value)
        return E_INVALIDARG;
    VariantInit(value);
    if (!entry || !entry->Set)
        return S_FALSE;
    return VariantCopy(value, &entry->Value);
}

static const ITfCompartmentVtbl compartment_vtbl = {cp_query, cp_add_ref, cp_release, cp_set_value, cp_get_value};

static HRESULT STDMETHODCALLTYPE cps_query(ITfSource *This, REFIID riid, void **object)
{
    COMPARTMENT *cp = CONTAINING_RECORD(This, COMPARTMENT, Source);
    return cp_query(&cp->Iface, riid, object);
}

static ULONG STDMETHODCALLTYPE cps_add_ref(ITfSource *This)
{
    return InterlockedIncrement(&CONTAINING_RECORD(This, COMPARTMENT, Source)->Refs);
}

static ULONG STDMETHODCALLTYPE cps_release(ITfSource *This)
{
    return cp_release(&CONTAINING_RECORD(This, COMPARTMENT, Source)->Iface);
}

static const IID *const g_compartment_sinks[] = {&IID_ITfCompartmentEventSink};

static HRESULT STDMETHODCALLTYPE cps_advise(ITfSource *This, REFIID riid, IUnknown *sink, DWORD *cookie)
{
    COMPARTMENT *cp = CONTAINING_RECORD(This, COMPARTMENT, Source);
    ENTRY *entry = find_entry(cp->Owner, &cp->Guid);
    if (!entry)
        return E_INVALIDARG;
    return SinksAdvise(&entry->Sinks, g_compartment_sinks, 1, riid, sink, cookie);
}

static HRESULT STDMETHODCALLTYPE cps_unadvise(ITfSource *This, DWORD cookie)
{
    COMPARTMENT *cp = CONTAINING_RECORD(This, COMPARTMENT, Source);
    ENTRY *entry = find_entry(cp->Owner, &cp->Guid);
    return entry ? SinksUnadvise(&entry->Sinks, cookie) : E_INVALIDARG;
}

static const ITfSourceVtbl compartment_source_vtbl = {cps_query, cps_add_ref, cps_release, cps_advise, cps_unadvise};

HRESULT CompartmentsGet(COMPARTMENTS *comps, REFGUID guid, ITfCompartment **out)
{
    if (!guid || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!add_entry(comps, guid))
        return E_OUTOFMEMORY;
    COMPARTMENT *cp = CtfAlloc(sizeof(*cp));
    if (!cp)
        return E_OUTOFMEMORY;
    cp->Iface.lpVtbl = &compartment_vtbl;
    cp->Source.lpVtbl = &compartment_source_vtbl;
    cp->Refs = 1;
    cp->Owner = comps;
    InterlockedIncrement(&comps->Refs);
    cp->Guid = *guid;
    CtfObjectCreated();
    *out = &cp->Iface;
    return S_OK;
}

HRESULT CompartmentsClear(COMPARTMENTS *comps, REFGUID guid)
{
    ENTRY *entry = guid ? find_entry(comps, guid) : nullptr;
    if (!entry)
        return E_INVALIDARG;
    VariantClear(&entry->Value);
    entry->Set = FALSE;
    return S_OK;
}

HRESULT CompartmentsEnum(COMPARTMENTS *comps, IEnumGUID **out)
{
    GUID *guids = nullptr;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    if (comps->Count) {
        guids = CtfAlloc(comps->Count * sizeof(GUID));
        if (!guids)
            return E_OUTOFMEMORY;
        for (ULONG i = 0; i < comps->Count; i++)
            guids[i] = comps->Items[i].Guid;
    }
    hr = EnumGuidsCreate(guids, comps->Count, out);
    CtfFree(guids);
    return hr;
}

/* ------------------------------------------------------------------ the global compartments */

static COMPARTMENTS *g_global;

typedef struct {
    ITfCompartmentMgr Iface;
    LONG Refs;
} GLOBAL_MGR;

static HRESULT STDMETHODCALLTYPE gm_query(ITfCompartmentMgr *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfCompartmentMgr)) {
        *object = This;
        InterlockedIncrement(&((GLOBAL_MGR *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE gm_add_ref(ITfCompartmentMgr *This)
{
    return InterlockedIncrement(&((GLOBAL_MGR *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE gm_release(ITfCompartmentMgr *This)
{
    GLOBAL_MGR *mgr = (GLOBAL_MGR *)This;
    LONG refs = InterlockedDecrement(&mgr->Refs);
    if (!refs) {
        CtfFree(mgr);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE gm_get(ITfCompartmentMgr *This, REFGUID guid, ITfCompartment **out)
{
    return CompartmentsGet(g_global, guid, out);
}

static HRESULT STDMETHODCALLTYPE gm_clear(ITfCompartmentMgr *This, TfClientId tid, REFGUID guid)
{
    return CompartmentsClear(g_global, guid);
}

static HRESULT STDMETHODCALLTYPE gm_enum(ITfCompartmentMgr *This, IEnumGUID **out)
{
    return CompartmentsEnum(g_global, out);
}

static const ITfCompartmentMgrVtbl global_vtbl = {gm_query, gm_add_ref, gm_release, gm_get, gm_clear, gm_enum};

HRESULT GlobalCompartmentMgr(ITfCompartmentMgr **out)
{
    if (!out)
        return E_INVALIDARG;
    if (!g_global) {
        COMPARTMENTS *comps = CompartmentsCreate();
        if (!comps)
            return E_OUTOFMEMORY;
        if (InterlockedCompareExchangePointer((void *volatile *)&g_global, comps, nullptr))
            CompartmentsFree(comps);
    }
    GLOBAL_MGR *mgr = CtfAlloc(sizeof(*mgr));
    if (!mgr)
        return E_OUTOFMEMORY;
    mgr->Iface.lpVtbl = &global_vtbl;
    mgr->Refs = 1;
    CtfObjectCreated();
    *out = &mgr->Iface;
    return S_OK;
}
