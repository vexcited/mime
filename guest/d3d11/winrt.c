/*
 * the Windows Runtime wrappers of Direct3D 11 objects: CreateDirect3D11DeviceFromDXGIDevice and
 * CreateDirect3D11SurfaceFromDXGISurface make the IDirect3DDevice and IDirect3DSurface that Windows.Graphics.Capture
 * and the other runtime classes take and hand out. a wrapper holds the Direct3D object it was made from until it
 * is closed, gives it back to GetInterface, and has no other interface a program may enumerate (read from
 * d3d11.dll's dxrt11::Direct3DDevice).
 */
#include "d3d11_private.h"
#include <mime/winrt_object.h>

typedef struct WRAPPER {
    WRT_OBJECT Base;
    CRITICAL_SECTION Lock;
    /* an IDXGIDevice3 for a device, an IDXGISurface for a surface; none once closed. */
    IUnknown *Object;
} WRAPPER;

static WRAPPER *wrapper_of(void *iface)
{
    return (WRAPPER *)((WRT_IFACE *)iface)->Owner;
}

static void wrapper_destroy(WRT_OBJECT *object)
{
    WRAPPER *w = (WRAPPER *)object;
    if (w->Object)
        w->Object->lpVtbl->Release(w->Object);
    DeleteCriticalSection(&w->Lock);
    HeapFree(GetProcessHeap(), 0, w);
}

/* the held object, with a reference, or RO_E_CLOSED. */
static HRESULT wrapper_object(WRAPPER *w, IUnknown **object)
{
    EnterCriticalSection(&w->Lock);
    *object = w->Object;
    if (*object)
        (*object)->lpVtbl->AddRef(*object);
    LeaveCriticalSection(&w->Lock);
    return *object ? S_OK : RO_E_CLOSED;
}

static HRESULT wrapper_close(WRAPPER *w)
{
    EnterCriticalSection(&w->Lock);
    IUnknown *object = w->Object;
    w->Object = nullptr;
    LeaveCriticalSection(&w->Lock);
    if (object)
        object->lpVtbl->Release(object);
    return S_OK;
}

WRT_INSPECTABLE_THUNKS(IDirect3DDevice)
WRT_INSPECTABLE_THUNKS(IDirect3DSurface)
WRT_INSPECTABLE_THUNKS(IClosable)
WRT_UNKNOWN_THUNKS(IDirect3DDxgiInterfaceAccess)

static HRESULT STDMETHODCALLTYPE device_trim(IDirect3DDevice *This)
{
    IUnknown *object;
    HRESULT hr = wrapper_object(wrapper_of(This), &object);
    if (FAILED(hr))
        return hr;
    IDXGIDevice3 *device = (IDXGIDevice3 *)object;
    device->lpVtbl->Trim(device);
    object->lpVtbl->Release(object);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_description(IDirect3DSurface *This, Direct3DSurfaceDescription *description)
{
    IUnknown *object;
    if (!description)
        return E_POINTER;
    HRESULT hr = wrapper_object(wrapper_of(This), &object);
    if (FAILED(hr))
        return hr;
    DXGI_SURFACE_DESC desc;
    IDXGISurface *surface = (IDXGISurface *)object;
    hr = surface->lpVtbl->GetDesc(surface, &desc);
    object->lpVtbl->Release(object);
    if (SUCCEEDED(hr)) {
        description->Width = (INT32)desc.Width;
        description->Height = (INT32)desc.Height;
        description->Format = (DirectXPixelFormat)desc.Format;
        description->MultisampleDescription.Count = (INT32)desc.SampleDesc.Count;
        description->MultisampleDescription.Quality = (INT32)desc.SampleDesc.Quality;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE closable_close(IClosable *This)
{
    return wrapper_close(wrapper_of(This));
}

/* the Direct3D object behind the wrapper, asked for by interface. */
static HRESULT STDMETHODCALLTYPE access_get_interface(IDirect3DDxgiInterfaceAccess *This, REFIID riid, void **object)
{
    IUnknown *held;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    HRESULT hr = wrapper_object(wrapper_of(This), &held);
    if (FAILED(hr))
        return hr;
    hr = held->lpVtbl->QueryInterface(held, riid, object);
    held->lpVtbl->Release(held);
    return hr;
}

static const IDirect3DDeviceVtbl device_vtbl = {WRT_INSPECTABLE_ENTRIES(IDirect3DDevice), device_trim};
static const IClosableVtbl closable_vtbl = {WRT_INSPECTABLE_ENTRIES(IClosable), closable_close};
static const IDirect3DDxgiInterfaceAccessVtbl access_vtbl = {WRT_UNKNOWN_ENTRIES(IDirect3DDxgiInterfaceAccess),
                                                              access_get_interface};
static const IDirect3DSurfaceVtbl surface_vtbl = {WRT_INSPECTABLE_ENTRIES(IDirect3DSurface), surface_description};

static const WRT_CLASS device_class = {
    L"Windows.Graphics.DirectX.Direct3D11.IDirect3DDevice",
    3,
    {&IID_IDirect3DDevice, &IID_IClosable, &IID_IDirect3DDxgiInterfaceAccess},
    1u << 2,
    wrapper_destroy,
};

static const WRT_CLASS surface_class = {
    L"Windows.Graphics.DirectX.Direct3D11.IDirect3DSurface",
    3,
    {&IID_IDirect3DSurface, &IID_IClosable, &IID_IDirect3DDxgiInterfaceAccess},
    1u << 2,
    wrapper_destroy,
};

static HRESULT wrap(const WRT_CLASS *class, const void *const *vtables, IUnknown *object, IInspectable **result)
{
    WRAPPER *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&w->Lock);
    WrtInit(&w->Base, class, vtables);
    w->Object = object;
    object->lpVtbl->AddRef(object);
    *result = (IInspectable *)&w->Base.Ifaces[0];
    return S_OK;
}

/* the device has to answer IDXGIDevice3, whose Trim the wrapper passes on. */
D3D11API HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice *device, IInspectable **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!device)
        return E_INVALIDARG;
    IDXGIDevice3 *device3;
    HRESULT hr = device->lpVtbl->QueryInterface(device, &IID_IDXGIDevice3, (void **)&device3);
    if (FAILED(hr))
        return hr;
    const void *vtables[3] = {&device_vtbl, &closable_vtbl, &access_vtbl};
    hr = wrap(&device_class, vtables, (IUnknown *)device3, result);
    device3->lpVtbl->Release(device3);
    return hr;
}

D3D11API HRESULT WINAPI CreateDirect3D11SurfaceFromDXGISurface(IDXGISurface *surface, IInspectable **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!surface)
        return E_INVALIDARG;
    const void *vtables[3] = {&surface_vtbl, &closable_vtbl, &access_vtbl};
    return wrap(&surface_class, vtables, (IUnknown *)surface, result);
}
