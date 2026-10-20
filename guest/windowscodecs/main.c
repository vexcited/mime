/*
 * the counted objects of windowscodecs.dll: memory, the shared methods of IUnknown and the class factories.
 */
#include "wic_private.h"

int _fltused = 0x9875;

static LONG volatile g_live;
static LONG volatile g_locks;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* ------------------------------------------------------------------ memory */

void *WicAlloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }

void *WicAllocZero(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1); }

void *WicRealloc(void *p, SIZE_T size)
{
    if (!p)
        return WicAlloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, p, size ? size : 1);
}

void WicFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static void *allocator_alloc(void *ctx, size_t size) { return WicAlloc(size); }

static void allocator_free(void *ctx, void *p) { WicFree(p); }

const MimeAllocator WicAllocator = {allocator_alloc, allocator_free, nullptr};

WCHAR *WicStrDup(const WCHAR *s)
{
    SIZE_T length = wcslen(s);
    WCHAR *copy = WicAlloc((length + 1) * sizeof(WCHAR));
    if (copy)
        memcpy(copy, s, (length + 1) * sizeof(WCHAR));
    return copy;
}

BOOL WicMul32(UINT a, UINT b, UINT *result)
{
    unsigned long long product = (unsigned long long)a * b;
    if (product > 0xFFFFFFFFull)
        return FALSE;
    *result = (UINT)product;
    return TRUE;
}

BOOL WicIsEqual(const GUID *a, const GUID *b) { return IsEqualGUID(a, b); }

/* ------------------------------------------------------------------ objects */

void *WicNew(SIZE_T size, const WicClass *cls)
{
    WicBase *base = WicAllocZero(size);
    if (!base)
        return nullptr;
    base->refs = 1;
    base->cls = cls;
    InterlockedIncrement(&g_live);
    return base;
}

HRESULT WicQueryBase(WicBase *base, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!riid)
        return E_INVALIDARG;
    const WicClass *cls = base->cls;
    const WicIfaceEntry *found = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown))
        found = &cls->ifaces[0];
    else {
        for (UINT i = 0; i < cls->iface_count; i++) {
            if (IsEqualGUID(riid, cls->ifaces[i].iid)) {
                found = &cls->ifaces[i];
                break;
            }
        }
    }
    if (!found)
        return E_NOINTERFACE;
    InterlockedIncrement(&base->refs);
    *object = (char *)base + found->offset;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WicQueryInterface(void *This, REFIID riid, void **object)
{
    return WicQueryBase(WicSlotBase(This), riid, object);
}

ULONG STDMETHODCALLTYPE WicAddRef(void *This)
{
    return (ULONG)InterlockedIncrement(&WicSlotBase(This)->refs);
}

ULONG STDMETHODCALLTYPE WicRelease(void *This)
{
    WicBase *base = WicSlotBase(This);
    LONG refs = InterlockedDecrement(&base->refs);
    if (refs == 0) {
        /* a destructor that hands out references while it runs is not given a second chance. */
        base->refs = 0x40000000;
        if (base->cls->destroy)
            base->cls->destroy(base);
        WicFree(base);
        InterlockedDecrement(&g_live);
    }
    return (ULONG)refs;
}

/* ------------------------------------------------------------------ class factories */

typedef HRESULT (*WicCreator)(REFIID riid, void **object);

HRESULT WicCreateFactoryObject(REFIID riid, void **object);
HRESULT WicCreateFactoryObject1(REFIID riid, void **object);
HRESULT WicCreateFactoryObject2(REFIID riid, void **object);
HRESULT WicCreateConverterObject(REFIID riid, void **object);
HRESULT WicCreateCodecObject(REFCLSID clsid, REFIID riid, void **object);

typedef struct WicClassEntry {
    const GUID *clsid;
    WicCreator create;
} WicClassEntry;

static const WicClassEntry g_classes[] = {
    {&CLSID_WICImagingFactory, WicCreateFactoryObject},
    {&CLSID_WICImagingFactory1, WicCreateFactoryObject1},
    {&CLSID_WICImagingFactory2, WicCreateFactoryObject2},
    {&CLSID_WICDefaultFormatConverter, WicCreateConverterObject},
};

typedef struct WicClassFactory {
    const IClassFactoryVtbl *lpVtbl;
    LONG refs;
    const GUID *clsid;
    WicCreator create;
} WicClassFactory;

static HRESULT STDMETHODCALLTYPE factory_qi(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_addref(IClassFactory *This)
{
    return (ULONG)InterlockedIncrement(&((WicClassFactory *)This)->refs);
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    WicClassFactory *factory = (WicClassFactory *)This;
    LONG refs = InterlockedDecrement(&factory->refs);
    if (!refs) {
        WicFree(factory);
        InterlockedDecrement(&g_live);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    WicClassFactory *factory = (WicClassFactory *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    /* the outer object is ignored, as the class factories of windows' do. */
    if (factory->create)
        return factory->create(riid, object);
    return WicCreateCodecObject(factory->clsid, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&g_locks);
    else
        InterlockedDecrement(&g_locks);
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {factory_qi, factory_addref, factory_release, factory_create,
                                                 factory_lock};

/* the component of a codec guid, from components.c. */
BOOL WicIsCodecClass(REFCLSID clsid);

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    WicCreator create = nullptr;
    BOOL known = FALSE;
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_classes); i++) {
        if (IsEqualGUID(clsid, g_classes[i].clsid)) {
            create = g_classes[i].create;
            known = TRUE;
            break;
        }
    }
    if (!known && !WicIsCodecClass(clsid))
        return CLASS_E_CLASSNOTAVAILABLE;
    WicClassFactory *factory = WicAllocZero(sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;
    InterlockedIncrement(&g_live);
    factory->lpVtbl = &g_factory_vtbl;
    factory->refs = 1;
    factory->clsid = clsid;
    factory->create = create;
    HRESULT hr = factory_qi((IClassFactory *)factory, riid, object);
    factory_release((IClassFactory *)factory);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void) { return g_live || g_locks ? S_FALSE : S_OK; }

DECLSPEC_EXPORT HRESULT WINAPI DllFreeCodecResources(void) { return S_OK; }
