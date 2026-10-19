/*
 * dxgi.dll: the factory, the adapters and their outputs. adapters are the
 * gpus vulkan-1.dll offers, the host's; outputs are the monitors of user32.
 * swap chains belong to the direct3d device they present, which the
 * factory asks for them through IMimeDXGIPresenter.
 */
#ifndef DXGI_PRIVATE_H
#define DXGI_PRIVATE_H

#define MIME_BUILD_DXGI
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winspool.h>
#include <mime/dxgi_mime.h>
#include "../lib/privdata/privdata.h"

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>

typedef struct DxgiFactory DxgiFactory;
typedef struct DxgiAdapter DxgiAdapter;

/* a gpu as vulkan describes it, the same for every factory. */
typedef struct DxgiGpu {
    VkPhysicalDevice physical;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    DXGI_ADAPTER_DESC3 desc;
    LARGE_INTEGER umd_version;
} DxgiGpu;

/* the process' vulkan instance and its gpus, in the order adapters are enumerated. */
typedef struct DxgiVulkan {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr get_instance_proc;
    PFN_vkGetPhysicalDeviceMemoryProperties2 get_memory2;
    UINT api_version;
    UINT count;
    DxgiGpu gpus[8];
} DxgiVulkan;

const DxgiVulkan *DxgiGetVulkan(void);

struct DxgiFactory {
    IDXGIFactory7 iface;
    IDXGIFactoryMedia media;
    LONG refs;
    UINT flags;
    HWND association;
    MimePrivateData data;
};

struct DxgiAdapter {
    IDXGIAdapter4 iface;
    IMimeDXGIAdapter mime;
    LONG refs;
    DxgiFactory *factory;
    const DxgiGpu *gpu;
    MimePrivateData data;
};

HRESULT DxgiCreateFactory(UINT flags, REFIID riid, void **out);
HRESULT DxgiCreateAdapter(DxgiFactory *factory, const DxgiGpu *gpu, REFIID riid, void **out);
HRESULT DxgiCreateOutput(DxgiAdapter *adapter, HMONITOR monitor, IDXGIOutput **out);
DxgiAdapter *DxgiAdapterFrom(IUnknown *unknown);
BOOL DxgiMonitorNumber(HMONITOR monitor, UINT *number);
/* duplication.c: v1 is DuplicateOutput1, whose arguments are checked and whose process needs per monitor dpi. */
HRESULT DxgiDuplicateOutput(IDXGIOutput *output, DxgiAdapter *adapter, HMONITOR monitor, IUnknown *device, BOOL v1,
                            UINT flags, UINT count, const DXGI_FORMAT *formats, IDXGIOutputDuplication **out);

#endif
