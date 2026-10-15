/*
 * what dxgi.dll and the direct3d dlls tell each other beyond windows'
 * interfaces: the vulkan device behind an adapter, and the swap chains a
 * direct3d device makes for the factory.
 */
#ifndef MIME_DXGI_MIME_H
#define MIME_DXGI_MIME_H

#include <mime/win32/dxgi.h>

/* the vulkan objects of an adapter, as vulkan handles and vkGetInstanceProcAddr. */
typedef struct MIME_DXGI_VULKAN {
    void *Instance;
    void *PhysicalDevice;
    void *GetInstanceProcAddr;
    UINT ApiVersion;
} MIME_DXGI_VULKAN;

typedef struct IMimeDXGIAdapter IMimeDXGIAdapter;
#define IMimeDXGIAdapter_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetVulkan)(T * This, MIME_DXGI_VULKAN * vulkan);
MIME_COM_INTERFACE(IMimeDXGIAdapter)
MIME_DEFINE_GUID(IID_IMimeDXGIAdapter, 0xE784F4ED, 0x16A6, 0x44A9, 0x85, 0x73, 0xB1, 0x88, 0xB5, 0x5D, 0x49, 0x25);

/* implemented by direct3d devices: the factory hands them its swap chain creations. */
typedef struct IMimeDXGIPresenter IMimeDXGIPresenter;
#define IMimeDXGIPresenter_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSwapChain)(T * This, IDXGIFactory * factory, HWND window,                         \
                                                const DXGI_SWAP_CHAIN_DESC1 *desc,                                     \
                                                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,                     \
                                                IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain);
MIME_COM_INTERFACE(IMimeDXGIPresenter)
MIME_DEFINE_GUID(IID_IMimeDXGIPresenter, 0x313CF4C8, 0x7593, 0x4D3D, 0xB7, 0xC9, 0x95, 0xCE, 0x68, 0x96, 0x5A, 0x32);

#endif
