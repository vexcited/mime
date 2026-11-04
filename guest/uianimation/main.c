#include "uianimation_private.h"

int _fltused = 0x9875;

HINSTANCE UaInstance;

static LONG g_objects;
static LONG g_locks;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        UaInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

void *UaAlloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1); }

void *UaRealloc(void *block, SIZE_T size) {
    if (!block)
        return UaAlloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, block, size ? size : 1);
}

void UaFree(void *block) {
    if (block)
        HeapFree(GetProcessHeap(), 0, block);
}

void UaObjectCountChange(LONG delta) { InterlockedExchangeAdd(&g_objects, delta); }

HRESULT UaQuery(IUnknown *object, const IID *own, REFIID iid, void **out) {
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, own)) {
        *out = object;
        object->lpVtbl->AddRef(object);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ class factories */

typedef HRESULT (*UaCreate)(int version, REFIID iid, void **out);

typedef struct UaFactory {
    IClassFactory iface;
    const CLSID *clsid;
    UaCreate create;
    int version;
} UaFactory;

static HRESULT timer_create(int version, REFIID iid, void **out) { return UaTimerCreate(iid, out); }

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *iface, REFIID iid, void **out) {
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory)) {
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_addref(IClassFactory *iface) { return 2; }

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *iface) { return 1; }

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out) {
    UaFactory *factory = (UaFactory *)iface;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return factory->create(factory->version, iid, out);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *iface, BOOL lock) {
    InterlockedExchangeAdd(&g_locks, lock ? 1 : -1);
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query, factory_addref, factory_release, factory_create, factory_lock,
};

static UaFactory g_factories[] = {
    {{&g_factory_vtbl}, &CLSID_UIAnimationManager, UaManagerCreate, 1},
    {{&g_factory_vtbl}, &CLSID_UIAnimationManager2, UaManagerCreate, 2},
    {{&g_factory_vtbl}, &CLSID_UIAnimationTransitionLibrary, UaLibraryCreate, 1},
    {{&g_factory_vtbl}, &CLSID_UIAnimationTransitionLibrary2, UaLibraryCreate, 2},
    {{&g_factory_vtbl}, &CLSID_UIAnimationTransitionFactory, UaFactoryCreate, 1},
    {{&g_factory_vtbl}, &CLSID_UIAnimationTransitionFactory2, UaFactoryCreate, 2},
    {{&g_factory_vtbl}, &CLSID_UIAnimationTimer, timer_create, 0},
};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    for (UINT i = 0; i < ARRAYSIZE(g_factories); i++)
        if (IsEqualGUID(clsid, g_factories[i].clsid))
            return factory_query(&g_factories[i].iface, iid, out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void) { return g_objects || g_locks ? S_FALSE : S_OK; }
