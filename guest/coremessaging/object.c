#include "coremessaging_private.h"

struct CM_WEAK {
    ICmWeakReference Iface;
    volatile LONG Refs;
    SRWLOCK Lock;
    CM_OBJECT *Target;
};

void CmInit(CM_OBJECT *object, const WRT_CLASS *class, const void *const *vtables)
{
    WrtInit(&object->Base, class, vtables);
    object->Ftm = nullptr;
    object->Weak = nullptr;
}

static void weak_release(CM_WEAK *weak)
{
    if (!InterlockedDecrement(&weak->Refs))
        HeapFree(GetProcessHeap(), 0, weak);
}

HRESULT CmQuery(WRT_IFACE *self, REFIID riid, void **object)
{
    HRESULT hr = WrtQuery(self, riid, object);
    if (hr != E_NOINTERFACE || !IsEqualIID(riid, &IID_CmMarshal))
        return hr;
    CM_OBJECT *owner = (CM_OBJECT *)self->Owner;
    if (!owner->Ftm) {
        IUnknown *ftm;
        if (FAILED(CoCreateFreeThreadedMarshaler((IUnknown *)&owner->Base.Ifaces[0], &ftm)))
            return E_NOINTERFACE;
        if (InterlockedCompareExchangePointer((void *volatile *)&owner->Ftm, ftm, nullptr))
            ftm->lpVtbl->Release(ftm);
    }
    return owner->Ftm->lpVtbl->QueryInterface(owner->Ftm, riid, object);
}

ULONG CmAddRef(WRT_IFACE *self)
{
    return (ULONG)InterlockedIncrement(&self->Owner->Refs);
}

BOOL CmTryAddRef(CM_OBJECT *object)
{
    LONG refs = object->Base.Refs;
    while (refs > 0) {
        LONG seen = InterlockedCompareExchange(&object->Base.Refs, refs + 1, refs);
        if (seen == refs)
            return TRUE;
        refs = seen;
    }
    return FALSE;
}

ULONG CmRelease(WRT_IFACE *self)
{
    CM_OBJECT *owner = (CM_OBJECT *)self->Owner;
    LONG refs = InterlockedDecrement(&owner->Base.Refs);
    if (refs)
        return (ULONG)refs;
    CM_WEAK *weak = owner->Weak;
    if (weak) {
        AcquireSRWLockExclusive(&weak->Lock);
        weak->Target = nullptr;
        ReleaseSRWLockExclusive(&weak->Lock);
        weak_release(weak);
    }
    if (owner->Ftm)
        owner->Ftm->lpVtbl->Release(owner->Ftm);
    owner->Base.Class->Destroy(&owner->Base);
    return 0;
}

static HRESULT STDMETHODCALLTYPE weak_query(ICmWeakReference *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_CmWeakReference) ||
        IsEqualIID(riid, &IID_IAgileObject)) {
        InterlockedIncrement(&((CM_WEAK *)This)->Refs);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE weak_add_ref(ICmWeakReference *This)
{
    return (ULONG)InterlockedIncrement(&((CM_WEAK *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE weak_release_iface(ICmWeakReference *This)
{
    CM_WEAK *weak = (CM_WEAK *)This;
    LONG refs = InterlockedDecrement(&weak->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, weak);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE weak_resolve(ICmWeakReference *This, REFIID riid, IInspectable **object)
{
    CM_WEAK *weak = (CM_WEAK *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    AcquireSRWLockShared(&weak->Lock);
    CM_OBJECT *target = weak->Target && CmTryAddRef(weak->Target) ? weak->Target : nullptr;
    ReleaseSRWLockShared(&weak->Lock);
    if (!target)
        return S_OK;
    WRT_IFACE *iface = &target->Base.Ifaces[0];
    HRESULT hr = CmQuery(iface, riid, (void **)object);
    CmRelease(iface);
    return hr;
}

static const ICmWeakReferenceVtbl weak_vtbl = {weak_query, weak_add_ref, weak_release_iface, weak_resolve};

static HRESULT STDMETHODCALLTYPE source_query(ICmWeakReferenceSource *This, REFIID riid, void **object)
{
    return CmQuery((WRT_IFACE *)This, riid, object);
}

static ULONG STDMETHODCALLTYPE source_add_ref(ICmWeakReferenceSource *This)
{
    return CmAddRef((WRT_IFACE *)This);
}

static ULONG STDMETHODCALLTYPE source_release(ICmWeakReferenceSource *This)
{
    return CmRelease((WRT_IFACE *)This);
}

static HRESULT STDMETHODCALLTYPE source_get(ICmWeakReferenceSource *This, ICmWeakReference **reference)
{
    if (!reference)
        return E_POINTER;
    *reference = nullptr;
    CM_OBJECT *owner = CmOwner(This);
    if (!owner->Weak) {
        CM_WEAK *weak = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*weak));
        if (!weak)
            return E_OUTOFMEMORY;
        weak->Iface.lpVtbl = &weak_vtbl;
        /* one for the object, which drops it when it goes. */
        weak->Refs = 1;
        weak->Target = owner;
        InitializeSRWLock(&weak->Lock);
        if (InterlockedCompareExchangePointer((void *volatile *)&owner->Weak, weak, nullptr))
            HeapFree(GetProcessHeap(), 0, weak);
    }
    InterlockedIncrement(&owner->Weak->Refs);
    *reference = &owner->Weak->Iface;
    return S_OK;
}

const ICmWeakReferenceSourceVtbl CmWeakSourceVtbl = {source_query, source_add_ref, source_release, source_get};

/* ------------------------------------------------------------------ events */

static volatile LONG64 g_tokens;

HRESULT CmEventAdd(CM_EVENT *event, ICmEventHandler *handler, EventRegistrationToken *token)
{
    if (!token)
        return E_POINTER;
    if (!handler)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&event->Lock);
    if (event->Count == event->Capacity) {
        UINT32 capacity = event->Capacity ? event->Capacity * 2 : 4;
        void *entries = event->Entries
                            ? HeapReAlloc(GetProcessHeap(), 0, event->Entries, capacity * sizeof(*event->Entries))
                            : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*event->Entries));
        if (!entries) {
            ReleaseSRWLockExclusive(&event->Lock);
            return E_OUTOFMEMORY;
        }
        event->Entries = entries;
        event->Capacity = capacity;
    }
    handler->lpVtbl->AddRef(handler);
    INT64 value = InterlockedIncrement64(&g_tokens);
    event->Entries[event->Count].Token = value;
    event->Entries[event->Count].Handler = handler;
    event->Count++;
    ReleaseSRWLockExclusive(&event->Lock);
    token->value = value;
    return S_OK;
}

HRESULT CmEventRemove(CM_EVENT *event, EventRegistrationToken token)
{
    ICmEventHandler *handler = nullptr;
    AcquireSRWLockExclusive(&event->Lock);
    for (UINT32 i = 0; i < event->Count; i++) {
        if (event->Entries[i].Token == token.value) {
            handler = event->Entries[i].Handler;
            memmove(&event->Entries[i], &event->Entries[i + 1], (event->Count - i - 1) * sizeof(*event->Entries));
            event->Count--;
            break;
        }
    }
    ReleaseSRWLockExclusive(&event->Lock);
    if (handler)
        handler->lpVtbl->Release(handler);
    return S_OK;
}

/* handlers added or removed by a handler take effect from the next raise. */
void CmEventInvoke(CM_EVENT *event, IInspectable *sender, IInspectable *args)
{
    AcquireSRWLockShared(&event->Lock);
    UINT32 count = event->Count;
    ICmEventHandler **handlers = count ? HeapAlloc(GetProcessHeap(), 0, count * sizeof(*handlers)) : nullptr;
    for (UINT32 i = 0; handlers && i < count; i++) {
        handlers[i] = event->Entries[i].Handler;
        handlers[i]->lpVtbl->AddRef(handlers[i]);
    }
    ReleaseSRWLockShared(&event->Lock);
    if (!handlers)
        return;
    for (UINT32 i = 0; i < count; i++) {
        handlers[i]->lpVtbl->Invoke(handlers[i], sender, args);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
    HeapFree(GetProcessHeap(), 0, handlers);
}

void CmEventClear(CM_EVENT *event)
{
    for (UINT32 i = 0; i < event->Count; i++)
        event->Entries[i].Handler->lpVtbl->Release(event->Entries[i].Handler);
    if (event->Entries)
        HeapFree(GetProcessHeap(), 0, event->Entries);
    event->Entries = nullptr;
    event->Count = event->Capacity = 0;
}
