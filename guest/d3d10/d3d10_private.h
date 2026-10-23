/*
 * d3d10.dll and d3d10_1.dll: devices made by d3d11.dll, state blocks, and what they read from compiled shaders.
 */
#ifndef D3D10_DLL_PRIVATE_H
#define D3D10_DLL_PRIVATE_H

#define MIME_BUILD_D3D10
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/d3d10_1.h>
#include <mime/win32/d3d11.h>

#define D3DERR_INVALIDCALL ((HRESULT)0x8876086C)

/* the flags d3d11.dll knows direct3d 10 and 10.1 devices by. */
#define D3D10_DEVICE_FLAG_D3D10 0x40000000
#define D3D10_DEVICE_FLAG_D3D10_1 0x80000000

/* create.c. */
D3D_DRIVER_TYPE D3d10DriverType(D3D10_DRIVER_TYPE type);
BOOL D3d10AdapterMismatch(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type);
UINT D3d10DeviceFlags(UINT flags);
void D3d10SdkMismatch(UINT sdk, UINT runtime);
HRESULT D3d10CreateDevice11(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                            D3D_FEATURE_LEVEL level, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain,
                            ID3D11Device **device);
HRESULT D3d10Protect(IUnknown *device, UINT flags);

/* shader.c. */
BOOL D3d10ContainerValid(const void *bytecode, SIZE_T length);

/* effect_variable.c: the objects every effect shares. */
void FxInit(void);

#endif
