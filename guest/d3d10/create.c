/*
 * what d3d10.dll and d3d10_1.dll share to make devices: the driver types
 * and flags as direct3d 11 has them, and d3d11.dll's device creation.
 */
#include "d3d10_private.h"

/* the type an adapter of another driver than the hardware one keeps in its private data. */
static const GUID GUID_DeviceType = {0xD722FB4D, 0x7A68, 0x437A, {0xB2, 0x0C, 0x58, 0x04, 0xEE, 0x24, 0x94, 0xA6}};

D3D_DRIVER_TYPE D3d10DriverType(D3D10_DRIVER_TYPE type)
{
    switch (type) {
    case D3D10_DRIVER_TYPE_HARDWARE:
        return D3D_DRIVER_TYPE_HARDWARE;
    case D3D10_DRIVER_TYPE_REFERENCE:
        return D3D_DRIVER_TYPE_REFERENCE;
    case D3D10_DRIVER_TYPE_NULL:
        return D3D_DRIVER_TYPE_NULL;
    case D3D10_DRIVER_TYPE_SOFTWARE:
        return D3D_DRIVER_TYPE_SOFTWARE;
    case D3D10_DRIVER_TYPE_WARP:
        return D3D_DRIVER_TYPE_WARP;
    }
    return D3D_DRIVER_TYPE_UNKNOWN;
}

BOOL D3d10AdapterMismatch(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type)
{
    if (!adapter)
        return FALSE;
    D3D_DRIVER_TYPE kind = D3D_DRIVER_TYPE_UNKNOWN;
    UINT size = sizeof(kind);
    return FAILED(adapter->lpVtbl->GetPrivateData(adapter, &GUID_DeviceType, &size, &kind)) ||
           kind != D3d10DriverType(type);
}

/* the direct3d 10 flags as direct3d 11 has them: debuggable moved, null maps and strict validation gone. */
UINT D3d10DeviceFlags(UINT flags)
{
    UINT out = flags & (D3D10_CREATE_DEVICE_SINGLETHREADED | D3D10_CREATE_DEVICE_DEBUG |
                        D3D10_CREATE_DEVICE_SWITCH_TO_REF |
                        D3D10_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
                        D3D10_CREATE_DEVICE_BGRA_SUPPORT |
                        D3D10_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY);
    if (flags & D3D10_CREATE_DEVICE_DEBUGGABLE)
        out |= D3D11_CREATE_DEVICE_DEBUGGABLE;
    return out | (flags & (D3D10_DEVICE_FLAG_D3D10 | D3D10_DEVICE_FLAG_D3D10_1));
}

void D3d10SdkMismatch(UINT sdk, UINT runtime)
{
    char text[200];
    wsprintfA(text, "\nD3D10: the program was built for SDK version %u, this runtime is version %u.\n\n", sdk,
              runtime);
    OutputDebugStringA(text);
}

HRESULT D3d10CreateDevice11(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                            D3D_FEATURE_LEVEL level, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain,
                            ID3D11Device **device)
{
    typedef HRESULT(WINAPI * Create)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                     UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                     D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    Create create = d3d11 ? (Create)(void *)GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain") : nullptr;
    if (!create)
        return E_FAIL;
    return create(adapter, type, software, flags, &level, 1, D3D11_SDK_VERSION, desc, swapchain, device, nullptr, nullptr);
}

/* unless single threaded, the device is protected from several threads at once. */
HRESULT D3d10Protect(IUnknown *device, UINT flags)
{
    ID3D10Multithread *multithread;
    if (flags & D3D10_CREATE_DEVICE_SINGLETHREADED)
        return S_OK;
    HRESULT hr = device->lpVtbl->QueryInterface(device, &IID_ID3D10Multithread, (void **)&multithread);
    if (SUCCEEDED(hr)) {
        multithread->lpVtbl->SetMultithreadProtected(multithread, TRUE);
        multithread->lpVtbl->Release(multithread);
    }
    return hr;
}
