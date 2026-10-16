/*
 * the devices of d3d10.dll: direct3d 11 devices of d3d11.dll at feature
 * level 10_0, made with a flag that gives them ID3D10Device, and the
 * version and layers of d3d10core.dll.
 */
#include "d3d10_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        FxInit();
    return TRUE;
}

/*
 * a hardware device unless another driver is asked, the adapter given then
 * having to be of that type. bgra support is dropped, as d3d10.dll does.
 */
D3D10API HRESULT WINAPI D3D10CreateDeviceAndSwapChain(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type, HMODULE software,
                                                      UINT flags, UINT sdk, DXGI_SWAP_CHAIN_DESC *desc,
                                                      IDXGISwapChain **swapchain, ID3D10Device **device)
{
    if (swapchain)
        *swapchain = nullptr;
    if (!device)
        return E_INVALIDARG;
    *device = nullptr;
    if (sdk != D3D10_SDK_VERSION) {
        D3d10SdkMismatch(sdk, D3D10_1_SDK_VERSION);
        return E_INVALIDARG;
    }
    if (type != D3D10_DRIVER_TYPE_HARDWARE) {
        if (D3d10DriverType(type) == D3D_DRIVER_TYPE_UNKNOWN)
            return E_INVALIDARG;
        if (D3d10AdapterMismatch(adapter, type))
            return E_INVALIDARG;
    }
    flags = (flags | D3D10_DEVICE_FLAG_D3D10) & ~D3D10_CREATE_DEVICE_BGRA_SUPPORT;
    ID3D11Device *d3d11 = nullptr;
    HRESULT hr = D3d10CreateDevice11(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3d10DriverType(type), software,
                                     D3d10DeviceFlags(flags), D3D_FEATURE_LEVEL_10_0, desc, swapchain, &d3d11);
    if (SUCCEEDED(hr))
        hr = d3d11->lpVtbl->QueryInterface(d3d11, &IID_ID3D10Device, (void **)device);
    if (d3d11)
        d3d11->lpVtbl->Release(d3d11);
    if (SUCCEEDED(hr) && *device)
        hr = D3d10Protect((IUnknown *)*device, (*device)->lpVtbl->GetCreationFlags(*device));
    return hr;
}

D3D10API HRESULT WINAPI D3D10CreateDevice(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type, HMODULE software, UINT flags,
                                          UINT sdk, ID3D10Device **device)
{
    return D3D10CreateDeviceAndSwapChain(adapter, type, software, flags, sdk, nullptr, nullptr, device);
}

/* d3d10core.dll's, which windows' d3d10.dll imports. */
static FARPROC core_entry(const char *name)
{
    HMODULE core = LoadLibraryW(L"d3d10core.dll");
    return core ? GetProcAddress(core, name) : nullptr;
}

D3D10API UINT64 WINAPI D3D10GetVersion(void)
{
    UINT64(WINAPI * get)(void) = (UINT64(WINAPI *)(void))(void *)core_entry("D3D10CoreGetVersion");
    return get ? get() : 0;
}

D3D10API HRESULT WINAPI D3D10RegisterLayers(void)
{
    HRESULT(WINAPI * reg)(void) = (HRESULT(WINAPI *)(void))(void *)core_entry("D3D10CoreRegisterLayers");
    return reg ? reg() : E_NOTIMPL;
}
