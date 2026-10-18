/*
 * what dxgi.dll and the direct3d dlls tell each other beyond windows'
 * interfaces: the vulkan device behind an adapter, the swap chains a
 * direct3d device makes for the factory, the output a fullscreen swap
 * chain owns, and the frames composition swap chains hand to dcomp.dll.
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
                                                IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain);               \
    HRESULT(STDMETHODCALLTYPE *CreateCompositionSwapChain)(T * This, IDXGIFactory * factory, HANDLE surface,           \
                                                           const DXGI_SWAP_CHAIN_DESC1 *desc,                          \
                                                           IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain);
MIME_COM_INTERFACE(IMimeDXGIPresenter)
MIME_DEFINE_GUID(IID_IMimeDXGIPresenter, 0x313CF4C8, 0x7593, 0x4D3D, 0xB7, 0xC9, 0x95, 0xCE, 0x68, 0x96, 0x5A, 0x32);

/* implemented by outputs: a swap chain owns its output while fullscreen, as dxgi.dll's swap chains do. */
typedef struct IMimeDXGIOutput IMimeDXGIOutput;
#define IMimeDXGIOutput_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *SetFullscreenOwner)(T * This, BOOL owned);
MIME_COM_INTERFACE(IMimeDXGIOutput)
MIME_DEFINE_GUID(IID_IMimeDXGIOutput, 0x6B0E3A52, 0x9D4C, 0x4F1B, 0xA8, 0x27, 0x3E, 0x51, 0xC9, 0x0D, 0x7A, 0x64);

/*
 * implemented by the swap chains made for composition: the frame last presented, which dcomp.dll draws into the
 * windows it composes, and a call when a new one comes. The texture is the device's own, so only a visual tree
 * built on the same device can read it.
 */
typedef struct MIME_COMPOSITION_FRAME {
    /* an ID3D11Texture2D, a reference the caller releases; NULL until the first Present. */
    IUnknown *Texture;
    UINT64 Serial;
    UINT Width, Height;
    /* the part of the texture that shows, in pixels. */
    RECT Source;
    /* the format to read the texture with: never srgb, since composition blends the encoded values. */
    DXGI_FORMAT ViewFormat;
    DXGI_ALPHA_MODE AlphaMode;
    DXGI_COLOR_SPACE_TYPE ColorSpace;
    /* the scale and offset of IDXGISwapChain2::SetMatrixTransform. */
    DXGI_MATRIX_3X2_F Matrix;
} MIME_COMPOSITION_FRAME;

typedef void(WINAPI *MIME_COMPOSITION_LISTENER)(void *context, IUnknown *source);

typedef struct IMimeDXGICompositionSource IMimeDXGICompositionSource;
#define IMimeDXGICompositionSource_METHODS(T)                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetFrame)(T * This, MIME_COMPOSITION_FRAME * frame);                                    \
    HRESULT(STDMETHODCALLTYPE *AddListener)(T * This, MIME_COMPOSITION_LISTENER listener, void *context);              \
    void(STDMETHODCALLTYPE *RemoveListener)(T * This, void *context);
MIME_COM_INTERFACE(IMimeDXGICompositionSource)
MIME_DEFINE_GUID(IID_IMimeDXGICompositionSource, 0x9C4E1D07, 0x5B2A, 0x4F83, 0xA6, 0xD1, 0x2E, 0x70, 0x38, 0xB5, 0xC9, 0x1F);

#endif
