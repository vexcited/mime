/*
 * private interfaces between dxgi.dll and the direct3d dlls: the vulkan device behind an adapter, swap chains a
 * direct3d device creates for the factory, the output a fullscreen swap chain owns, and the frames composition swap
 * chains hand to dcomp.dll.
 */
#ifndef MIME_DXGI_MIME_H
#define MIME_DXGI_MIME_H

#include <mime/win32/winbase.h>
#include <mime/win32/dxgi.h>

/* an adapter's vulkan objects: raw handles and vkGetInstanceProcAddr. */
typedef struct MIME_DXGI_VULKAN {
    void *Instance;
    void *PhysicalDevice;
    void *GetInstanceProcAddr;
    UINT ApiVersion;
} MIME_DXGI_VULKAN;

/* the adreno's vendor as dxgi gives it, the four characters of the acpi VEN_QCOM. */
#define DXGI_VENDOR_QUALCOMM 0x4D4F4351

#define MIME_VK_MAX_QUEUES 16
#define MIME_VK_PROFILE_BYTES 1024

/*
 * an adapter's vulkan device, shared by the process's direct3d 11 and 12 devices since the host serves one per
 * process. the first caller creates it with its queue family, the last release destroys it.
 */
typedef struct MIME_DXGI_SHARED_VK {
    void *Device;
    void *GetDeviceProcAddr;
    UINT QueueFamily;
    UINT QueueCount;
    void *Queues[MIME_VK_MAX_QUEUES];
    /* a queue is used by one thread at a time, whichever device submits on it. */
    SRWLOCK QueueLocks[MIME_VK_MAX_QUEUES];
    /* what the device was created for, opaque to dxgi.dll. */
    UINT ProfileSize;
    BYTE Profile[MIME_VK_PROFILE_BYTES];
} MIME_DXGI_SHARED_VK;

typedef struct IMimeDXGIAdapter IMimeDXGIAdapter;
#define IMimeDXGIAdapter_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetVulkan)(T * This, MIME_DXGI_VULKAN * vulkan);                                        \
    /* takes a reference on the shared device; with no creation info and no device yet, S_FALSE. */                    \
    HRESULT(STDMETHODCALLTYPE *AcquireSharedVulkan)(T * This, const void *create_info, const void *profile,           \
                                                    UINT profile_size, MIME_DXGI_SHARED_VK **shared);                  \
    void(STDMETHODCALLTYPE *ReleaseSharedVulkan)(T * This, MIME_DXGI_SHARED_VK * shared);                              \
    /*                                                                                                                 \
     * semaphores of fences shared by id that the devices of this process hold together: a device that opens a        \
     * shared fence the process made takes the semaphore instead of importing the payload. Published once the fence  \
     * is exported; the entry counts the fences that hold it and destroys the semaphore with the last.                \
     */                                                                                                                \
    HRESULT(STDMETHODCALLTYPE *PublishSemaphore)(T * This, MIME_DXGI_SHARED_VK * shared, UINT64 id,                    \
                                                 void *semaphore, void **entry);                                       \
    /* the semaphore published under the id, a reference taken on its entry; S_FALSE when there is none. */           \
    HRESULT(STDMETHODCALLTYPE *OpenSemaphore)(T * This, MIME_DXGI_SHARED_VK * shared, UINT64 id, void **semaphore,    \
                                              void **entry);                                                           \
    void(STDMETHODCALLTYPE *ReleaseSemaphore)(T * This, MIME_DXGI_SHARED_VK * shared, void *entry);
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
 * implemented by composition swap chains: the last presented frame, which dcomp.dll draws into the windows it
 * composes, plus a notification for new frames. the texture belongs to the device, so only a visual tree built on
 * the same device can read it.
 */
typedef struct MIME_COMPOSITION_FRAME {
    /* an ID3D11Texture2D, a reference the caller releases; NULL until the first Present. */
    IUnknown *Texture;
    UINT64 Serial;
    UINT Width, Height;
    /* the visible part of the texture, in pixels. */
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

/*
 * implemented by the textures of the software device direct3d 11 gives for warp when the machine has no gpu: the
 * pixels, 32 bits each, which dcomp.dll composes on the cpu. Lock holds the device until Unlock.
 */
typedef struct IMimeSoftwareTexture IMimeSoftwareTexture;
#define IMimeSoftwareTexture_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, const BYTE **bits, UINT *pitch);                                        \
    void(STDMETHODCALLTYPE *Unlock)(T * This);
MIME_COM_INTERFACE(IMimeSoftwareTexture)
MIME_DEFINE_GUID(IID_IMimeSoftwareTexture, 0x2F8A61C3, 0x47E9, 0x4B0D, 0x93, 0x5C, 0x1A, 0xE4, 0x76, 0x0B, 0xD2, 0x58);

#endif
