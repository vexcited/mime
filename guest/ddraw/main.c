/*
 * ddraw.dll's exports: creating directdraw objects and clippers,
 * enumerating the display, the class objects CoCreateInstance reaches, and
 * the internal entry points other system dlls once called.
 *
 * there is one display, the host's, without a guid of its own: windows
 * enumerates it as the primary display driver and names monitors with
 * guids only on machines with several.
 */
#include "ddraw_private.h"

RTL_CRITICAL_SECTION DdLock;
HINSTANCE DdModule;
LIST_ENTRY DdObjects;
static LONG g_locks;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DdModule = instance;
        RtlInitializeCriticalSection(&DdLock);
        InitializeListHead(&DdObjects);
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ creation */

/* the guids windows takes for the display: none, DDCREATE_HARDWAREONLY and DDCREATE_EMULATIONONLY. */
static BOOL our_guid(const GUID *guid)
{
    return guid == nullptr || guid == (const GUID *)DDCREATE_HARDWAREONLY ||
           guid == (const GUID *)DDCREATE_EMULATIONONLY || IsEqualGUID(guid, &GUID_NULL);
}

DDRAWAPI HRESULT WINAPI DirectDrawCreate(GUID *guid, LPDIRECTDRAW *ddraw, IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!ddraw)
        return DDERR_INVALIDPARAMS;
    *ddraw = nullptr;
    if (!our_guid(guid))
        return DDERR_INVALIDDIRECTDRAWGUID;
    return DdCreate(guid, DDV1, (void **)ddraw, TRUE);
}

/* only IDirectDraw7 is made this way; the object behaves as directx 7's. */
DDRAWAPI HRESULT WINAPI DirectDrawCreateEx(GUID *guid, LPVOID *ddraw, REFIID iid, IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!iid || !IsEqualGUID(iid, &IID_IDirectDraw7) || !ddraw)
        return DDERR_INVALIDPARAMS;
    *ddraw = nullptr;
    if (!our_guid(guid))
        return DDERR_INVALIDDIRECTDRAWGUID;
    return DdCreate(guid, DDV7, ddraw, TRUE);
}

DDRAWAPI HRESULT WINAPI DirectDrawCreateClipper(DWORD flags, LPDIRECTDRAWCLIPPER *clipper, IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!clipper)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    DDCLIP *c;
    HRESULT hr = DdCreateClipper(nullptr, flags, &c, TRUE);
    *clipper = SUCCEEDED(hr) ? &c->iface : nullptr;
    return DdLeave(hr);
}

/* ------------------------------------------------------------------ enumeration */

/* only the ansi forms enumerate; the wide ones are not implemented in windows either. */
DDRAWAPI HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA callback, LPVOID context, DWORD flags)
{
    if (!callback || (flags & ~(DDENUM_ATTACHEDSECONDARYDEVICES | DDENUM_DETACHEDSECONDARYDEVICES |
                                DDENUM_NONDISPLAYDEVICES)))
        return DDERR_INVALIDPARAMS;
    char description[] = "Primary Display Driver", name[] = "display";
    callback(nullptr, description, name, context, nullptr);
    return DD_OK;
}

typedef struct ENUMA {
    LPDDENUMCALLBACKA callback;
    LPVOID context;
} ENUMA;

static BOOL WINAPI enum_a(GUID *guid, LPSTR description, LPSTR name, LPVOID context, HMONITOR monitor)
{
    ENUMA *e = context;
    return e->callback(guid, description, name, e->context);
}

DDRAWAPI HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA callback, LPVOID context)
{
    if (!callback)
        return DDERR_INVALIDPARAMS;
    ENUMA e = {callback, context};
    return DirectDrawEnumerateExA(enum_a, &e, DDENUM_NONDISPLAYDEVICES);
}

DDRAWAPI HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW callback, LPVOID context)
{
    return callback ? DDERR_UNSUPPORTED : DDERR_INVALIDPARAMS;
}

DDRAWAPI HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW callback, LPVOID context, DWORD flags)
{
    return DDERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------ class objects */

typedef struct FACTORY {
    IClassFactory iface;
    LONG ref;
    CLSID clsid;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        InterlockedIncrement(&CONTAINING_RECORD(This, FACTORY, iface)->ref);
        *out = This;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return InterlockedIncrement(&CONTAINING_RECORD(This, FACTORY, iface)->ref);
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    FACTORY *f = CONTAINING_RECORD(This, FACTORY, iface);
    LONG ref = InterlockedDecrement(&f->ref);
    if (!ref) {
        HeapFree(GetProcessHeap(), 0, f);
        InterlockedDecrement(&g_locks);
    }
    return ref;
}

/* objects made here are uninitialized: Initialize makes them usable. */
static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    FACTORY *f = CONTAINING_RECORD(This, FACTORY, iface);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!riid)
        return DDERR_INVALIDPARAMS;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (IsEqualGUID(&f->clsid, &CLSID_DirectDrawClipper)) {
        DdEnter();
        DDCLIP *c;
        HRESULT hr = DdCreateClipper(nullptr, 0, &c, FALSE);
        if (SUCCEEDED(hr)) {
            hr = c->iface.lpVtbl->QueryInterface(&c->iface, riid, out);
            c->iface.lpVtbl->Release(&c->iface);
        }
        return DdLeave(hr);
    }
    void *dd;
    DWORD version = IsEqualGUID(&f->clsid, &CLSID_DirectDraw7) ? DDV7 : DDV1;
    HRESULT hr = DdCreate(nullptr, version, &dd, FALSE);
    if (FAILED(hr))
        return hr;
    IUnknown *unknown = dd;
    hr = unknown->lpVtbl->QueryInterface(unknown, riid, out);
    unknown->lpVtbl->Release(unknown);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&g_locks);
    else
        InterlockedDecrement(&g_locks);
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create,
                                               factory_lock};

static const CLSID CLSID_DirectDrawFactory2 = {0xB9DC4790, 0x4AF1, 0x11D1, {0x8C, 0x4C, 0x00, 0xC0, 0x4F, 0xD9, 0x30, 0xC5}};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!clsid || !riid)
        return DDERR_INVALIDPARAMS;
    if (!IsEqualGUID(clsid, &CLSID_DirectDraw) && !IsEqualGUID(clsid, &CLSID_DirectDraw7) &&
        !IsEqualGUID(clsid, &CLSID_DirectDrawClipper) && !IsEqualGUID(clsid, &CLSID_DirectDrawFactory2))
        return E_FAIL;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    FACTORY *f = HeapAlloc(GetProcessHeap(), 0, sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->iface.lpVtbl = &factory_vtbl;
    f->ref = 1;
    f->clsid = *clsid;
    InterlockedIncrement(&g_locks);
    *out = &f->iface;
    return S_OK;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_locks || !IsListEmpty(&DdObjects) ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ internal entry points */

/* the lock every directdraw call takes, which d3d once took around its own calls. */
void WINAPI AcquireDDThreadLock(void)
{
    RtlEnterCriticalSection(&DdLock);
}

void WINAPI ReleaseDDThreadLock(void)
{
    RtlLeaveCriticalSection(&DdLock);
}

/* the setup and driver hooks of other system components, which mime's do not use. */
HRESULT WINAPI CompleteCreateSysmemSurface(void *surface)
{
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI D3DParseUnknownCommand(void *command, void **next)
{
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DDGetAttachedSurfaceLcl(void *surface, void *caps, void **attached)
{
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DDInternalLock(void *surface, void **bits)
{
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DDInternalUnlock(void *surface)
{
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DSoundHelp(HWND window, void *proc, DWORD pid)
{
    return DD_OK;
}

void *WINAPI GetDDSurfaceLocal(void *ddraw, DWORD handle, void *unused)
{
    return nullptr;
}

ULONG_PTR WINAPI GetOLEThunkData(int index)
{
    return 0;
}

/* the surface an IDirectDrawSurface dc belongs to, as IDirectDraw4::GetSurfaceFromDC finds it. */
HRESULT WINAPI GetSurfaceFromDC(HDC dc, LPDIRECTDRAWSURFACE *surface, HDC *device)
{
    if (!surface)
        return DDERR_INVALIDPARAMS;
    *surface = nullptr;
    DdEnter();
    for (LIST_ENTRY *o = DdObjects.Flink; o != &DdObjects; o = o->Flink) {
        DDRAW *dd = CONTAINING_RECORD(o, DDRAW, links);
        for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink) {
            DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
            if (s->dc && s->dc == dc) {
                *surface = &s->iface1;
                DdSurfAddRef(s, &s->ref1);
                if (device)
                    *device = nullptr;
                return DdLeave(DD_OK);
            }
        }
    }
    return DdLeave(DDERR_NOTFOUND);
}

HRESULT WINAPI RegisterSpecialCase(DWORD a, DWORD b, DWORD c, DWORD d)
{
    return DD_OK;
}

/* the compatibility shims windows' application database applies; mime has none to set. */
HRESULT WINAPI SetAppCompatData(DWORD index, DWORD data)
{
    return index > 12 ? DDERR_INVALIDPARAMS : DD_OK;
}
