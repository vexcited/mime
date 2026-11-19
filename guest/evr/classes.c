/*
 * the COM side of evr.dll: its class objects and the functions that make the mixer and the presenter through
 * CoCreateInstance, as evr.dll of windows 11 does. Read from evr.dll and measured with evrobj.exe and evrfn.exe.
 */
#include "evr_private.h"

/* samples and their buffers keep the library loaded, as evr's CBaseObject count does. allocators do not. */
static LONG g_objects;

void EvrObjectCreated(void)
{
    InterlockedIncrement(&g_objects);
}

void EvrObjectDestroyed(void)
{
    InterlockedDecrement(&g_objects);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_objects > 0 ? S_FALSE : S_OK;
}

typedef HRESULT (*CREATE)(IUnknown *outer, REFIID riid, void **out);

typedef struct {
    IClassFactory iface;
    LONG refs;
    CREATE create;
} Factory;

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IClassFactory *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IClassFactory)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    *out = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IClassFactory *This)
{
    return InterlockedIncrement(&CONTAINING_RECORD(This, Factory, iface)->refs);
}

static ULONG STDMETHODCALLTYPE factory_Release(IClassFactory *This)
{
    Factory *f = CONTAINING_RECORD(This, Factory, iface);
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        HeapFree(GetProcessHeap(), 0, f);
        EvrObjectDestroyed();
    }
    return refs;
}

/* an outer object may only ask for IUnknown, as the DirectShow class factory has it. */
static HRESULT STDMETHODCALLTYPE factory_CreateInstance(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer && !IsEqualGUID(riid, &IID_IUnknown))
        return E_NOINTERFACE;
    return CONTAINING_RECORD(This, Factory, iface)->create(outer, riid, out);
}

static HRESULT STDMETHODCALLTYPE factory_LockServer(IClassFactory *This, BOOL lock)
{
    if (lock)
        EvrObjectCreated();
    else
        EvrObjectDestroyed();
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {
    factory_QueryInterface, factory_AddRef, factory_Release, factory_CreateInstance, factory_LockServer,
};

static HRESULT create_presenter(IUnknown *outer, REFIID riid, void **out)
{
    return EvrCreatePresenter(outer, riid, out, FALSE);
}

static HRESULT create_presenter_flip(IUnknown *outer, REFIID riid, void **out)
{
    return EvrCreatePresenter(outer, riid, out, TRUE);
}

/* the interface is checked before the class. */
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    static const struct {
        const GUID *clsid;
        CREATE create;
    } CLASSES[] = {
        {&CLSID_MFVideoMixer9, EvrCreateMixer},
        {&CLSID_MFVideoPresenter9, create_presenter},
        {&CLSID_MFVideoPresenter9FlipMode, create_presenter_flip},
        {&CLSID_EVRTearlessWindowPresenter9, EvrCreateTearless},
    };
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(CLASSES); i++) {
        if (!IsEqualGUID(clsid, CLASSES[i].clsid))
            continue;
        Factory *f = HeapAlloc(GetProcessHeap(), 0, sizeof(*f));
        if (!f)
            return E_OUTOFMEMORY;
        f->iface.lpVtbl = &factory_vtbl;
        f->refs = 1;
        f->create = CLASSES[i].create;
        EvrObjectCreated();
        *out = &f->iface;
        return S_OK;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

static HRESULT create_for_device(REFCLSID clsid, IUnknown *owner, REFIID device, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (!IsEqualGUID(device, &IID_IDirect3DDevice9))
        return E_INVALIDARG;
    return CoCreateInstance(clsid, owner, CLSCTX_INPROC_SERVER, riid, out);
}

HRESULT WINAPI MFCreateVideoMixer(IUnknown *owner, REFIID device, REFIID riid, void **out)
{
    return create_for_device(&CLSID_MFVideoMixer9, owner, device, riid, out);
}

HRESULT WINAPI MFCreateVideoPresenter(IUnknown *owner, REFIID device, REFIID riid, void **out)
{
    return create_for_device(&CLSID_MFVideoPresenter9, owner, device, riid, out);
}

HRESULT WINAPI MFCreateVideoPresenter2(IUnknown *owner, REFIID device, REFIID riid, void **out)
{
    return create_for_device(&CLSID_MFVideoPresenter9FlipMode, owner, device, riid, out);
}

/* the presenter is made first, and neither is handed out unless both were made. */
HRESULT WINAPI MFCreateVideoMixerAndPresenter(IUnknown *mixer_owner, IUnknown *presenter_owner, REFIID mixer_iid,
                                              void **mixer, REFIID presenter_iid, void **presenter)
{
    IUnknown *made_mixer = nullptr, *made_presenter = nullptr;
    if (!mixer || !presenter)
        return E_POINTER;
    HRESULT hr = MFCreateVideoPresenter(presenter_owner, &IID_IDirect3DDevice9, presenter_iid, (void **)&made_presenter);
    if (SUCCEEDED(hr))
        hr = MFCreateVideoMixer(mixer_owner, &IID_IDirect3DDevice9, mixer_iid, (void **)&made_mixer);
    if (SUCCEEDED(hr)) {
        *mixer = made_mixer;
        *presenter = made_presenter;
        return hr;
    }
    if (made_mixer)
        made_mixer->lpVtbl->Release(made_mixer);
    if (made_presenter)
        made_presenter->lpVtbl->Release(made_presenter);
    return hr;
}
