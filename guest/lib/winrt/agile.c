#include "winrt.h"

DECLSPEC_IMPORT HRESULT WINAPI CoCreateFreeThreadedMarshaler(IUnknown *outer, IUnknown **result);

struct WRT_WEAK {
    IWeakReference Iface;
    volatile LONG Refs;
    SRWLOCK Lock;
    WRT_AGILE *Target;
};

void WrtAgileInit(WRT_AGILE *object, const WRT_CLASS *class, const void *const *vtables)
{
    WrtInit(&object->Base, class, vtables);
    object->Ftm = nullptr;
    object->Weak = nullptr;
}

static void weak_release(WRT_WEAK *weak)
{
    if (!InterlockedDecrement(&weak->Refs))
        HeapFree(GetProcessHeap(), 0, weak);
}

HRESULT WrtAgileQuery(WRT_IFACE *self, REFIID riid, void **object)
{
    HRESULT hr = WrtQuery(self, riid, object);
    if (hr != E_NOINTERFACE || !IsEqualIID(riid, &IID_WrtMarshal))
        return hr;
    WRT_AGILE *owner = (WRT_AGILE *)self->Owner;
    if (!owner->Ftm) {
        IUnknown *ftm;
        if (FAILED(CoCreateFreeThreadedMarshaler((IUnknown *)&owner->Base.Ifaces[0], &ftm)))
            return E_NOINTERFACE;
        if (InterlockedCompareExchangePointer((void *volatile *)&owner->Ftm, ftm, nullptr))
            ftm->lpVtbl->Release(ftm);
    }
    return owner->Ftm->lpVtbl->QueryInterface(owner->Ftm, riid, object);
}

ULONG WrtAgileAddRef(WRT_IFACE *self)
{
    return (ULONG)InterlockedIncrement(&self->Owner->Refs);
}

BOOL WrtAgileTryAddRef(WRT_AGILE *object)
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

ULONG WrtAgileRelease(WRT_IFACE *self)
{
    WRT_AGILE *owner = (WRT_AGILE *)self->Owner;
    LONG refs = InterlockedDecrement(&owner->Base.Refs);
    if (refs)
        return (ULONG)refs;
    WRT_WEAK *weak = owner->Weak;
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

static HRESULT STDMETHODCALLTYPE weak_query(IWeakReference *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWeakReference) ||
        IsEqualIID(riid, &IID_IAgileObject)) {
        InterlockedIncrement(&((WRT_WEAK *)This)->Refs);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE weak_add_ref(IWeakReference *This)
{
    return (ULONG)InterlockedIncrement(&((WRT_WEAK *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE weak_release_iface(IWeakReference *This)
{
    WRT_WEAK *weak = (WRT_WEAK *)This;
    LONG refs = InterlockedDecrement(&weak->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, weak);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE weak_resolve(IWeakReference *This, REFIID riid, IInspectable **object)
{
    WRT_WEAK *weak = (WRT_WEAK *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    AcquireSRWLockShared(&weak->Lock);
    WRT_AGILE *target = weak->Target && WrtAgileTryAddRef(weak->Target) ? weak->Target : nullptr;
    ReleaseSRWLockShared(&weak->Lock);
    if (!target)
        return S_OK;
    WRT_IFACE *iface = &target->Base.Ifaces[0];
    HRESULT hr = WrtAgileQuery(iface, riid, (void **)object);
    WrtAgileRelease(iface);
    return hr;
}

static const IWeakReferenceVtbl weak_vtbl = {weak_query, weak_add_ref, weak_release_iface, weak_resolve};

static HRESULT STDMETHODCALLTYPE source_query(IWeakReferenceSource *This, REFIID riid, void **object)
{
    return WrtAgileQuery((WRT_IFACE *)This, riid, object);
}

static ULONG STDMETHODCALLTYPE source_add_ref(IWeakReferenceSource *This)
{
    return WrtAgileAddRef((WRT_IFACE *)This);
}

static ULONG STDMETHODCALLTYPE source_release(IWeakReferenceSource *This)
{
    return WrtAgileRelease((WRT_IFACE *)This);
}

static HRESULT STDMETHODCALLTYPE source_get(IWeakReferenceSource *This, IWeakReference **reference)
{
    if (!reference)
        return E_POINTER;
    *reference = nullptr;
    WRT_AGILE *owner = WrtAgileOf(This);
    if (!owner->Weak) {
        WRT_WEAK *weak = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*weak));
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

const IWeakReferenceSourceVtbl WrtWeakSourceVtbl = {source_query, source_add_ref, source_release, source_get};
