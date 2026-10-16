/*
 * the exports of d3d11.dll: device creation, on an adapter of dxgi or on
 * the first one. every driver type gets the hardware device; the debug
 * layer is missing, as on a pc without the graphics tools.
 */
#include "d3d11_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

static HRESULT first_adapter(IDXGIAdapter **adapter)
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    HRESULT(WINAPI * create)(REFIID, void **) =
        dxgi ? (HRESULT(WINAPI *)(REFIID, void **))(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    if (!create)
        return E_FAIL;
    IDXGIFactory1 *factory;
    HRESULT hr = create(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr))
        return hr;
    hr = factory->lpVtbl->EnumAdapters(factory, 0, adapter);
    factory->lpVtbl->Release(factory);
    return hr == DXGI_ERROR_NOT_FOUND ? E_FAIL : hr;
}

D3D11API HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                          const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk,
                                          ID3D11Device **device, D3D_FEATURE_LEVEL *level,
                                          ID3D11DeviceContext **context)
{
    if (device)
        *device = nullptr;
    if (level)
        *level = 0;
    if (context)
        *context = nullptr;
    if ((adapter && type != D3D_DRIVER_TYPE_UNKNOWN) || (!adapter && type == D3D_DRIVER_TYPE_UNKNOWN))
        return E_INVALIDARG;
    if (type == D3D_DRIVER_TYPE_SOFTWARE && !software)
        return E_INVALIDARG;
    if (flags & D3D11_CREATE_DEVICE_DEBUG)
        return DXGI_ERROR_SDK_COMPONENT_MISSING;
    HRESULT hr;
    if (adapter)
        adapter->lpVtbl->AddRef(adapter);
    else if (FAILED(hr = first_adapter(&adapter)))
        return hr;
    D3dDevice *object;
    hr = D3dCreateDevice(adapter, flags, levels, count, &object);
    adapter->lpVtbl->Release(adapter);
    if (FAILED(hr))
        return hr;
    if (level)
        *level = object->feature_level;
    if (context) {
        object->iface.lpVtbl->GetImmediateContext(&object->iface, context);
    }
    if (device)
        *device = (ID3D11Device *)&object->iface;
    else
        object->iface.lpVtbl->Release(&object->iface);
    return device ? S_OK : S_FALSE;
}

D3D11API HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software,
                                                      UINT flags, const D3D_FEATURE_LEVEL *levels, UINT count,
                                                      UINT sdk, const DXGI_SWAP_CHAIN_DESC *desc,
                                                      IDXGISwapChain **swapchain, ID3D11Device **device,
                                                      D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **context)
{
    if (swapchain) {
        *swapchain = nullptr;
        if (!desc)
            return E_INVALIDARG;
    }
    ID3D11Device *d = nullptr;
    HRESULT hr = D3D11CreateDevice(adapter, type, software, flags, levels, count, sdk, &d, level, context);
    if (FAILED(hr))
        return hr;
    if (swapchain) {
        IDXGIDevice *dxgi = nullptr;
        IDXGIAdapter *parent = nullptr;
        IDXGIFactory *factory = nullptr;
        d->lpVtbl->QueryInterface(d, &IID_IDXGIDevice, (void **)&dxgi);
        dxgi->lpVtbl->GetAdapter(dxgi, &parent);
        hr = parent->lpVtbl->GetParent(parent, &IID_IDXGIFactory, (void **)&factory);
        if (SUCCEEDED(hr)) {
            DXGI_SWAP_CHAIN_DESC copy = *desc;
            hr = factory->lpVtbl->CreateSwapChain(factory, (IUnknown *)d, &copy, swapchain);
            factory->lpVtbl->Release(factory);
        }
        parent->lpVtbl->Release(parent);
        dxgi->lpVtbl->Release(dxgi);
        if (FAILED(hr)) {
            if (context && *context) {
                (*context)->lpVtbl->Release(*context);
                *context = nullptr;
            }
            d->lpVtbl->Release(d);
            return hr;
        }
    }
    if (device)
        *device = d;
    else
        d->lpVtbl->Release(d);
    return S_OK;
}

/* direct3d 11 on 12 needs direct3d 12, which is absent. */
D3D11API HRESULT WINAPI D3D11On12CreateDevice(IUnknown *device, UINT flags, const D3D_FEATURE_LEVEL *levels,
                                              UINT count, IUnknown *const *queues, UINT queue_count, UINT node_mask,
                                              ID3D11Device **out, ID3D11DeviceContext **context,
                                              D3D_FEATURE_LEVEL *level)
{
    if (out)
        *out = nullptr;
    if (context)
        *context = nullptr;
    return E_NOTIMPL;
}
