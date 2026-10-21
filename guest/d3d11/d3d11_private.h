/*
 * d3d11.dll: direct3d 11 over vulkan 1.3, written for the way mime's
 * vulkan-1.dll works: every call is a message to the host, so nothing
 * a frame does may wait for an answer. resources live in a few large
 * allocations, views are descriptors in heaps indexed by the shaders, and
 * shaders are vulkan shader objects with all other state dynamic, so a
 * draw only records commands.
 */
#ifndef D3D11_PRIVATE_H
#define D3D11_PRIVATE_H

#define MIME_BUILD_D3D11
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/d3d11.h>
#include <mime/dxgi_mime.h>
#include <mime/d3d11_mime.h>
#include "../lib/privdata/privdata.h"
#include "../lib/dxbc/dxbc.h"
#include <mime/mvk_video.h>
#include "vkprofile.h"
#include "gfxshare.h"
#include <mime/mvk_share.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>

#define D3D_MAX(a, b) ((a) > (b) ? (a) : (b))
#define D3D_MIN(a, b) ((a) < (b) ? (a) : (b))
#define D3D_ALIGN(v, a) (((v) + (a)-1) & ~((UINT64)(a)-1))

/* ------------------------------------------------------------------ vulkan entry points */

#define D3D_VK_INSTANCE_FUNCS(X)                                                                                       \
    X(vkGetPhysicalDeviceProperties2)                                                                                  \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceFormatProperties)                                                                             \
    X(vkGetPhysicalDeviceImageFormatProperties)                                                                        \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                             \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                        \
    X(vkEnumerateDeviceExtensionProperties)                                                                            \
    X(vkCreateDevice)                                                                                                  \
    X(vkGetDeviceProcAddr)                                                                                             \
    X(vkCreateWin32SurfaceKHR)                                                                                         \
    X(vkDestroySurfaceKHR)                                                                                             \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                                       \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define D3D_VK_DEVICE_FUNCS(X)                                                                                         \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueSubmit2)                                                                                                  \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkDestroyDescriptorPool)                                                                                         \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkResetCommandBuffer)                                                                                            \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkWaitSemaphores)                                                                                                \
    X(vkGetSemaphoreCounterValue)                                                                                      \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateBufferView)                                                                                              \
    X(vkDestroyBufferView)                                                                                             \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkCreateDescriptorPool)                                                                                          \
    X(vkAllocateDescriptorSets)                                                                                        \
    X(vkUpdateDescriptorSets)                                                                                          \
    X(vkCreateShadersEXT)                                                                                              \
    X(vkDestroyShaderEXT)                                                                                              \
    X(vkCreateQueryPool)                                                                                               \
    X(vkDestroyQueryPool)                                                                                              \
    X(vkGetQueryPoolResults)                                                                                           \
    X(vkResetQueryPool)                                                                                                \
    X(vkCreateSwapchainKHR)                                                                                            \
    X(vkDestroySwapchainKHR)                                                                                           \
    X(vkGetSwapchainImagesKHR)                                                                                         \
    X(vkAcquireNextImageKHR)                                                                                           \
    X(vkQueuePresentKHR)                                                                                               \
    X(vkCmdPipelineBarrier2)                                                                                           \
    X(vkCmdBeginRendering)                                                                                             \
    X(vkCmdEndRendering)                                                                                               \
    X(vkCmdClearColorImage)                                                                                            \
    X(vkCmdClearDepthStencilImage)                                                                                     \
    X(vkCmdClearAttachments)                                                                                           \
    X(vkCmdCopyBuffer)                                                                                                 \
    X(vkCmdCopyImage)                                                                                                  \
    X(vkCmdCopyBufferToImage)                                                                                          \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCmdBlitImage)                                                                                                  \
    X(vkCmdResolveImage)                                                                                               \
    X(vkCmdUpdateBuffer)                                                                                               \
    X(vkCmdFillBuffer)                                                                                                 \
    X(vkCmdBindShadersEXT)                                                                                             \
    X(vkCmdBindDescriptorSets)                                                                                         \
    X(vkCmdBindVertexBuffers2)                                                                                         \
    X(vkCmdBindIndexBuffer)                                                                                            \
    X(vkCmdDraw)                                                                                                       \
    X(vkCmdDrawIndexed)                                                                                                \
    X(vkCmdDrawIndirect)                                                                                               \
    X(vkCmdDrawIndexedIndirect)                                                                                        \
    X(vkCmdDispatch)                                                                                                   \
    X(vkCmdDispatchIndirect)                                                                                           \
    X(vkCmdSetViewportWithCount)                                                                                       \
    X(vkCmdSetScissorWithCount)                                                                                        \
    X(vkCmdSetRasterizerDiscardEnable)                                                                                 \
    X(vkCmdSetCullMode)                                                                                                \
    X(vkCmdSetFrontFace)                                                                                               \
    X(vkCmdSetPrimitiveTopology)                                                                                       \
    X(vkCmdSetPrimitiveRestartEnable)                                                                                  \
    X(vkCmdSetDepthTestEnable)                                                                                         \
    X(vkCmdSetDepthWriteEnable)                                                                                        \
    X(vkCmdSetDepthCompareOp)                                                                                          \
    X(vkCmdSetDepthBoundsTestEnable)                                                                                   \
    X(vkCmdSetStencilTestEnable)                                                                                       \
    X(vkCmdSetStencilOp)                                                                                               \
    X(vkCmdSetStencilCompareMask)                                                                                      \
    X(vkCmdSetStencilWriteMask)                                                                                        \
    X(vkCmdSetStencilReference)                                                                                        \
    X(vkCmdSetDepthBiasEnable)                                                                                         \
    X(vkCmdSetDepthBias)                                                                                               \
    X(vkCmdSetBlendConstants)                                                                                          \
    X(vkCmdSetLineWidth)                                                                                               \
    X(vkCmdSetPolygonModeEXT)                                                                                          \
    X(vkCmdSetRasterizationSamplesEXT)                                                                                 \
    X(vkCmdSetSampleMaskEXT)                                                                                           \
    X(vkCmdSetAlphaToCoverageEnableEXT)                                                                                \
    X(vkCmdSetLogicOpEnableEXT)                                                                                        \
    X(vkCmdSetLogicOpEXT)                                                                                              \
    X(vkCmdSetColorBlendEnableEXT)                                                                                     \
    X(vkCmdSetColorBlendEquationEXT)                                                                                   \
    X(vkCmdSetColorWriteMaskEXT)                                                                                       \
    X(vkCmdSetDepthClampEnableEXT)                                                                                     \
    X(vkCmdSetDepthClipEnableEXT)                                                                                      \
    X(vkCmdSetVertexInputEXT)                                                                                          \
    X(vkCmdSetPatchControlPointsEXT)                                                                                   \
    X(vkCmdSetTessellationDomainOriginEXT)                                                                             \
    X(vkCmdSetRasterizationStreamEXT)                                                                                  \
    X(vkCmdSetConservativeRasterizationModeEXT)                                                                        \
    X(vkCmdSetFragmentShadingRateKHR)                                                                                  \
    X(vkCmdSetLineRasterizationModeEXT)                                                                                \
    X(vkCmdSetLineStippleEnableEXT)                                                                                    \
    X(vkCmdBeginQuery)                                                                                                 \
    X(vkCmdEndQuery)                                                                                                   \
    X(vkCmdResetQueryPool)                                                                                             \
    X(vkCmdWriteTimestamp2)                                                                                            \
    X(vkCmdCopyQueryPoolResults)                                                                                       \
    X(vkCmdBeginQueryIndexedEXT)                                                                                       \
    X(vkCmdEndQueryIndexedEXT)                                                                                         \
    X(vkCmdBindTransformFeedbackBuffersEXT)                                                                            \
    X(vkCmdBeginTransformFeedbackEXT)                                                                                  \
    X(vkCmdEndTransformFeedbackEXT)                                                                                    \
    X(vkCmdDrawIndirectByteCountEXT)

/* video decoding, the private commands of mime's vulkan-1.dll, see include/mime/mvk_video.h. */
typedef VkResult(VKAPI_PTR *PFN_vkMimeVideoGetCaps)(VkDevice device, uint32_t *count, MvkVideoCaps *caps, uint32_t capacity);
typedef VkResult(VKAPI_PTR *PFN_vkMimeVideoCreateSession)(VkDevice device, const MvkVideoSessionInfo *info,
                                                          uint64_t *session);
typedef void(VKAPI_PTR *PFN_vkMimeVideoDestroySession)(VkDevice device, uint64_t session);
typedef VkResult(VKAPI_PTR *PFN_vkMimeVideoDecode)(VkDevice device, const MvkVideoDecodeInfo *info,
                                                   uint64_t picture_image, uint32_t picture_layer);

typedef struct D3dVk {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define X(name) PFN_##name name;
    D3D_VK_INSTANCE_FUNCS(X)
    D3D_VK_DEVICE_FUNCS(X)
#undef X
    /* null where the host does not decode video. */
    PFN_vkMimeVideoGetCaps vkMimeVideoGetCaps;
    PFN_vkMimeVideoCreateSession vkMimeVideoCreateSession;
    PFN_vkMimeVideoDestroySession vkMimeVideoDestroySession;
    PFN_vkMimeVideoDecode vkMimeVideoDecode;
    /* memory and semaphores crossing to other devices, see mime/mvk_share.h. */
    PFN_vkMimeShareExportMemory vkMimeShareExportMemory;
    PFN_vkMimeShareImportMemory vkMimeShareImportMemory;
    PFN_vkMimeShareExportSemaphore vkMimeShareExportSemaphore;
    PFN_vkMimeShareImportSemaphore vkMimeShareImportSemaphore;
} D3dVk;

/* ------------------------------------------------------------------ formats */

#include "format.h"

/* the bit depths the gpu decodes video in, as the pictures' formats say. */
#define D3D_VIDEO_8BIT 1
#define D3D_VIDEO_10BIT 2

/* ------------------------------------------------------------------ memory */

typedef struct D3dBlock D3dBlock;

/* a range of one of the device's large allocations. */
typedef struct D3dAllocation {
    D3dBlock *block;
    VkDeviceMemory memory;
    /* the vulkan buffer spanning a buffer block, null in image blocks. */
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
    BYTE *mapped;
} D3dAllocation;

typedef enum D3dMemoryKind {
    D3D_MEMORY_DEVICE,   /* the gpu's own memory, not mapped */
    D3D_MEMORY_UPLOAD,   /* guest ram the gpu reads, written by the cpu */
    D3D_MEMORY_READBACK, /* guest ram the gpu writes and the cpu reads, cached */
    D3D_MEMORY_KINDS
} D3dMemoryKind;

/* the ring of upload memory: per frame data, renamed buffers and descriptor tables. */
typedef struct D3dRingChunk D3dRingChunk;
typedef struct D3dCbChunk D3dCbChunk;

typedef struct D3dRingSlice {
    D3dRingChunk *chunk;
    VkBuffer buffer;
    VkDeviceSize offset;
    /* the offset from the chunk's start, as dynamic offsets of its descriptors count. */
    VkDeviceSize chunk_offset;
    BYTE *mapped;
} D3dRingSlice;

/* ------------------------------------------------------------------ objects */

typedef struct D3dDevice D3dDevice;
typedef struct D3dContext D3dContext;
typedef struct D3dResource D3dResource;
typedef struct D3dView D3dView;

/* the direct3d 10 interface an object also answers to, see d3d10_objects.c. */
typedef struct D3d10Iface {
    const void *lpVtbl;
} D3d10Iface;

/* the state shared by everything the device makes. */
typedef struct D3dChild {
    LONG refs;
    D3dDevice *device;
    MimePrivateData data;
} D3dChild;

/* a heap index of each descriptor array, 0 being the null descriptor. */
enum {
    D3D_HEAP_SAMPLED_IMAGE,
    D3D_HEAP_STORAGE_IMAGE,
    D3D_HEAP_UNIFORM_TEXEL,
    D3D_HEAP_STORAGE_TEXEL,
    D3D_HEAP_SAMPLER,
    D3D_HEAP_UNIFORM_BUFFER,
    D3D_HEAP_KINDS
};

typedef struct D3dHeap {
    SRWLOCK lock;
    VkDescriptorSet set;
    UINT capacity[D3D_HEAP_KINDS];
    UINT next[D3D_HEAP_KINDS];
    /* indices freed, with the submission after which the gpu is done with them. */
    struct {
        UINT *index;
        UINT64 *serial;
        UINT count, capacity;
    } freed[D3D_HEAP_KINDS];
} D3dHeap;

UINT D3dHeapAlloc(D3dDevice *device, UINT kind);
/* count indices in a row, never freed, or 0 when the heap has no such room left. */
UINT D3dHeapAllocRange(D3dDevice *device, UINT kind, UINT count);
void D3dHeapFree(D3dDevice *device, UINT kind, UINT index);
void D3dHeapWriteImage(D3dDevice *device, UINT kind, UINT index, VkImageView view);
void D3dHeapWriteTexel(D3dDevice *device, UINT kind, UINT index, VkBufferView view);
void D3dHeapWriteSampler(D3dDevice *device, UINT index, VkSampler sampler);
void D3dHeapWriteBuffer(D3dDevice *device, UINT index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
/* count uniform buffer descriptors from index, each stride bytes further in the buffer. */
void D3dHeapWriteBuffers(D3dDevice *device, UINT index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride,
                         VkDeviceSize range, UINT count);

struct D3dResource {
    ID3D11Resource *iface; /* the object's own interface, a buffer or a texture */
    IDXGIResource1 dxgi;
    IDXGISurface2 surface;
    D3dChild child;
    D3D11_RESOURCE_DIMENSION dimension;
    D3D11_USAGE usage;
    UINT bind, cpu_access, misc;
    /* the image is sampled for the video processor though the program did not bind it for shaders. */
    BOOL video_sampled;
    UINT eviction_priority;
    /* the submission after which the gpu no longer reads or writes it. */
    UINT64 last_use;
    /* the command list being recorded that last noted its use, so each notes it once. */
    UINT64 list_mark;
    /* buffers: their bytes in a buffer block or, renamed, in the ring. */
    struct {
        UINT size, stride;
        D3dAllocation alloc;
        VkBuffer buffer;
        VkDeviceSize offset;
        BYTE *mapped;
        /* changes with each rename, so contexts rebind. */
        UINT version;
        /* the ranges a dynamic buffer left behind, for reuse. */
        void *spares;
        /* the uniform buffer descriptor of the whole current range, made when first bound. */
        UINT cbv;
        /* a dynamic constant buffer discarded into the constant buffer ring: its chunk and descriptor. */
        D3dCbChunk *cb_chunk;
        UINT cb_cbv;
        /* stream output targets: where in the buffer the count of bytes written is, after the data. */
        VkDeviceSize so_counter;
    } buf;
    /* textures. */
    struct {
        DXGI_FORMAT format;
        const D3dFormat *fmt;
        UINT width, height, depth, mips, layers, samples;
        VkImage image;
        D3dAllocation alloc;
        VkImageAspectFlags aspect;
        /* staging and dynamic textures: their subresources packed in a mapped buffer. */
        D3dAllocation staging;
        UINT *sub_offset;
        UINT *sub_row_pitch;
        UINT *sub_slice_pitch;
        /* dynamic textures whose mapped bytes still need copying to the image. */
        BOOL dirty;
    } tex;
    union {
        D3D11_BUFFER_DESC buffer;
        D3D11_TEXTURE1D_DESC texture1d;
        D3D11_TEXTURE2D_DESC1 texture2d;
        D3D11_TEXTURE3D_DESC1 texture3d;
    } desc;
    /* buffers and textures of each dimension: one interface pointer. */
    union {
        ID3D11Buffer buffer;
        ID3D11Texture1D texture1d;
        ID3D11Texture2D1 texture2d;
        ID3D11Texture3D1 texture3d;
    } u;
    BOOL swapchain;
    D3d10Iface d3d10;
    /*
     * sharing: what a texture made to be shared was described with, the kernel object once it has a handle or was
     * opened from one, and the keyed mutex, whose acquire is noted by the device that made it.
     */
    MimeShareBlob *share_blob;
    MimeShare share;
    SRWLOCK share_lock;
    IDXGIKeyedMutex keyed;
    BOOL keyed_acquired;
};

D3dResource *D3dResourceFrom(ID3D11Resource *iface);
HRESULT D3dCreateBuffer(D3dDevice *device, const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
                        D3dResource **out);
HRESULT D3dCreateTexture(D3dDevice *device, D3D11_RESOURCE_DIMENSION dimension, const void *desc,
                         const D3D11_SUBRESOURCE_DATA *data, D3dResource **out);
/* share.c: textures made to be shared and the handles that open them, keyed mutexes and fences. */
#define D3D_SHARE_MISC (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
HRESULT D3dShareCheck(D3D11_RESOURCE_DIMENSION dimension, UINT usage, UINT misc, UINT mips, UINT samples);
/* the description a shared texture is made with: the blob of its kernel object. */
void D3dShareDescribe(D3dResource *r, const VkImageCreateInfo *info, UINT type_index, UINT64 memory_size);
HRESULT D3dShareHandle(D3dResource *r, BOOL nt, const SECURITY_ATTRIBUTES *attributes, LPCWSTR name, HANDLE *out);
HRESULT D3dShareOpen(D3dDevice *device, HANDLE handle, BOOL nt, REFIID riid, void **out);
void D3dShareDestroy(D3dResource *r);
void *D3dShareKeyedMutex(D3dResource *r);
/* the texture a description makes, memory imported from the object that is opened: the creation of resource.c. */
typedef struct D3dImport {
    const MimeShareBlob *blob;
    MimeShare share;
} D3dImport;
HRESULT D3dCreateTextureImported(D3dDevice *device, const D3D11_TEXTURE2D_DESC1 *desc, const D3dImport *import,
                                 D3dResource **out);
HRESULT D3dCreateFence(D3dDevice *device, UINT64 value, D3D11_FENCE_FLAG flags, REFIID riid, void **out);
HRESULT D3dOpenSharedFence(D3dDevice *device, HANDLE handle, REFIID riid, void **out);
HRESULT D3dFenceSignal(D3dContext *context, ID3D11Fence *fence, UINT64 value);
HRESULT D3dFenceWait(D3dContext *context, ID3D11Fence *fence, UINT64 value);
void D3dResourceAddRef(D3dResource *resource);
void D3dResourceRelease(D3dResource *resource);
UINT D3dSubresourceCount(const D3dResource *resource);
void D3dSubresourceDecompose(const D3dResource *resource, UINT index, UINT *mip, UINT *layer);
VkExtent3D D3dMipExtent(const D3dResource *resource, UINT mip);
UINT D3dBufferCbv(D3dResource *resource, UINT offset, UINT size);
BOOL D3dBufferRename(D3dDevice *device, D3dResource *resource);
BOOL D3dBufferDiscardCb(D3dDevice *device, D3dResource *resource, BOOL *retired);
void D3dImageCopyRegions(const D3dResource *resource, UINT index, VkDeviceSize base, VkBufferImageCopy *regions,
                         UINT *count);

/* views: a descriptor in the heap and what it covers. */
typedef enum D3dViewKind { D3D_VIEW_SRV, D3D_VIEW_RTV, D3D_VIEW_DSV, D3D_VIEW_UAV } D3dViewKind;
/* a shader view the video processor makes, which needs no bind flag, only taken by D3dCreateView. */
#define D3D_VIEW_PLANE ((D3dViewKind)4)

struct D3dView {
    union {
        ID3D11ShaderResourceView1 srv;
        ID3D11RenderTargetView1 rtv;
        ID3D11DepthStencilView dsv;
        ID3D11UnorderedAccessView1 uav;
    } iface;
    D3dChild child;
    D3dViewKind kind;
    D3dResource *resource;
    union {
        D3D11_SHADER_RESOURCE_VIEW_DESC1 srv;
        D3D11_RENDER_TARGET_VIEW_DESC1 rtv;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
        D3D11_UNORDERED_ACCESS_VIEW_DESC1 uav;
    } desc;
    VkImageView image_view;
    VkBufferView buffer_view;
    /* the buffer version the buffer view was made for. */
    UINT buffer_version;
    UINT heap_kind;
    UINT heap_index;
    VkFormat format;
    VkImageSubresourceRange range;
    VkExtent3D extent;
    UINT samples;
    /* the first element and count of buffer views, in bytes. */
    UINT buffer_offset, buffer_size;
    /* append and consume buffers: the hidden counter. */
    D3dAllocation counter;
    VkBufferView counter_view;
    UINT counter_index;
    D3d10Iface d3d10;
};

D3dView *D3dViewFrom(void *iface, D3dViewKind kind);
void D3dViewRefresh(D3dView *view);

/* ------------------------------------------------------------------ states */

typedef struct D3dBlendState {
    ID3D11BlendState1 iface;
    D3dChild child;
    struct D3dBlendState *next;
    D3D11_BLEND_DESC1 desc;
    D3d10Iface d3d10;
} D3dBlendState;

typedef struct D3dRasterizerState {
    ID3D11RasterizerState2 iface;
    D3dChild child;
    struct D3dRasterizerState *next;
    D3D11_RASTERIZER_DESC2 desc;
    D3d10Iface d3d10;
} D3dRasterizerState;

typedef struct D3dDepthStencilState {
    ID3D11DepthStencilState iface;
    D3dChild child;
    struct D3dDepthStencilState *next;
    D3D11_DEPTH_STENCIL_DESC desc;
    D3d10Iface d3d10;
} D3dDepthStencilState;

typedef struct D3dSamplerState {
    ID3D11SamplerState iface;
    D3dChild child;
    struct D3dSamplerState *next;
    D3D11_SAMPLER_DESC desc;
    VkSampler sampler;
    UINT heap_index;
    D3d10Iface d3d10;
} D3dSamplerState;

typedef struct D3dInputLayout {
    ID3D11InputLayout iface;
    D3dChild child;
    UINT count;
    VkVertexInputAttributeDescription2EXT attributes[D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT];
    /* per slot: whether instanced and the step rate. */
    UINT slot_mask;
    BYTE instanced[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT step_rate[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    D3d10Iface d3d10;
} D3dInputLayout;

/* ------------------------------------------------------------------ shaders */

typedef enum D3dStage { D3D_VS, D3D_HS, D3D_DS, D3D_GS, D3D_PS, D3D_CS, D3D_STAGES } D3dStage;

typedef struct D3dShader {
    union {
        ID3D11VertexShader vs;
        ID3D11HullShader hs;
        ID3D11DomainShader ds;
        ID3D11GeometryShader gs;
        ID3D11PixelShader ps;
        ID3D11ComputeShader cs;
    } iface;
    D3dChild child;
    D3dStage stage;
    VkShaderEXT shader;
    DxbcShaderInfo info;
    /*
     * geometry shaders with stream output: the stream rasterized. one made
     * from a vertex or domain shader passes vertices through, compiled for
     * each kind of input primitive when first drawn with: its source and
     * declaration, and the variants by dcl_inputprimitive.
     */
    BOOL so;
    UINT rasterized_stream;
    void *so_source;
    SIZE_T so_source_length;
    DxbcStreamOutput *so_entries;
    UINT so_count;
    UINT so_strides[D3D11_SO_BUFFER_SLOT_COUNT];
    VkShaderEXT so_variants[8];
    D3d10Iface d3d10;
} D3dShader;

VkShaderEXT D3dShaderVariant(D3dShader *shader, UINT primitive);

/* the per stage table of heap indices the shader reads, compact as dxbc.h lays it out. */
typedef struct D3dBindingTable {
    UINT entries[DXBC_TABLE_DWORDS];
} D3dBindingTable;

/* ------------------------------------------------------------------ queries */

/* what a query counts with: the kinds of vulkan queries behind direct3d's. */
typedef enum D3dQueryKind {
    D3D_QUERY_NONE,
    D3D_QUERY_OCCLUSION,
    D3D_QUERY_STATISTICS,
    D3D_QUERY_TIMESTAMP,
    D3D_QUERY_STREAM0,
    D3D_QUERY_KINDS = D3D_QUERY_STREAM0 + D3D11_SO_STREAM_COUNT
} D3dQueryKind;

/* vulkan queries of one kind, the readback memory their results are copied to, and how many queries hold each. */
typedef struct D3dQueryPool {
    struct D3dQueryPool *next;
    D3dQueryKind kind;
    VkQueryPool pool;
    D3dAllocation results;
    UINT used;
    LONG refs[256];
} D3dQueryPool;

typedef struct D3dQuerySlot {
    D3dQueryPool *pool;
    UINT index;
} D3dQuerySlot;

/* slots given up, in the order they were, waiting for the gpu to be done with them. */
typedef struct D3dRetiredSlots {
    struct D3dRetiredSlot {
        D3dQuerySlot slot;
        UINT64 serial;
    } *list;
    UINT head, count, capacity;
} D3dRetiredSlots;

typedef struct D3dQuery {
    ID3D11Query1 iface;
    D3dChild child;
    D3D11_QUERY_DESC1 desc;
    /* the kinds it counts with, all streams for stream output over every stream. */
    UINT kinds;
    /* the submission that carries its end. */
    UINT64 serial;
    BOOL begun, ended;
    /* calls of GetData without a flush that found it not ready, since the end. */
    BYTE polls;
    /* the segments it was begun over, its result their sum. */
    D3dQuerySlot *slots;
    UINT slot_count, slot_capacity;
    D3d10Iface d3d10;
} D3dQuery;

/* ------------------------------------------------------------------ device */

/* vulkan objects and memory released while the gpu may still use them, freed once it is done. */
typedef enum D3dGarbageType {
    D3D_GARBAGE_IMAGE,
    D3D_GARBAGE_IMAGE_VIEW,
    D3D_GARBAGE_BUFFER_VIEW,
    D3D_GARBAGE_SAMPLER,
    D3D_GARBAGE_SHADER,
    D3D_GARBAGE_QUERY_POOL,
    D3D_GARBAGE_MEMORY,
    D3D_GARBAGE_HEAP,
    D3D_GARBAGE_COMMAND_POOL,
} D3dGarbageType;

typedef struct D3dGarbage {
    UINT64 serial;
    D3dGarbageType type;
    UINT64 handle;
    D3dAllocation alloc;
    UINT heap_kind;
} D3dGarbage;

typedef struct D3dCommandBuffer {
    VkCommandBuffer buffer;
    UINT64 serial;
} D3dCommandBuffer;

struct D3dDevice {
    ID3D11Device5 iface;
    IDXGIDevice4 dxgi;
    IMimeDXGIPresenter presenter;
    IMimeD3D11Device mime;
    ID3D11Multithread multithread;
    ID3D11VideoDevice1 video;
    /* ID3D10Device1, for devices d3d10.dll and d3d10_1.dll make. */
    D3d10Iface d3d10;
    LONG refs;
    /* the adapter the device was made on, which GetAdapter hands out. */
    IDXGIAdapter *adapter;
    D3D_FEATURE_LEVEL feature_level;
    UINT flags;
    UINT exception_mode;
    UINT max_frame_latency;
    MimePrivateData data;

    D3dVk vk;
    UINT api_version;
    VkQueue queue;
    UINT queue_family;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    BOOL has_depth_clip, has_custom_border, has_transform_feedback, has_conditional, has_line_rasterization;
    BOOL has_divisor, has_zero_divisor, has_statistics, has_precise_occlusion, has_so_queries;
    BOOL has_logic_op, has_depth_bounds, has_geometry, has_tessellation;
    BOOL has_video;
    /* what the shared device enables for direct3d 12 that shader objects want state for before every draw. */
    BOOL has_vrs, has_conservative, has_mesh;

    /* the queue is shared by the process's devices on the adapter, submissions from contexts and swap chains alike. */
    SRWLOCK *queue_lock;
    MimeVkShared shared;
    VkSemaphore timeline;
    /* fences the program has the next submission wait for, under the queue's lock. */
#define D3D_MAX_FENCE_WAITS 8
    struct {
        VkSemaphore semaphore;
        UINT64 value;
    } fence_waits[D3D_MAX_FENCE_WAITS];
    UINT fence_wait_count;
    UINT64 next_serial;
    volatile UINT64 completed;
    /* ranges dynamic buffers took new since the last submission. */
    UINT renames;

    /*
     * video decoding: what the host's decode queue can do, asked once, and the timeline it signals as pictures
     * are decoded, which the next submission waits for.
     */
    SRWLOCK video_lock;
    BOOL video_probed;
    UINT video_count;
    MvkVideoCaps video_caps[8];
    UINT video_depths;
    VkSemaphore video_timeline;
    UINT64 video_value, video_waited;
    void *video_processor;

    /* memory. */
    SRWLOCK memory_lock;
    D3dBlock *blocks;
    UINT buffer_types[D3D_MEMORY_KINDS];
    UINT image_type_device;

    /* the upload ring. */
    SRWLOCK ring_lock;
    D3dRingChunk *ring_chunks;
    D3dRingChunk *ring_current;

    /* the constant buffer ring: where dynamic constant buffers go when discarded. */
    SRWLOCK cb_lock;
    D3dCbChunk *cb_chunks;
    D3dCbChunk *cb_current;
    UINT cb_chunk_count;

    /* descriptors. */
    D3dHeap heap;
    VkDescriptorSetLayout heap_layout;
    VkDescriptorSetLayout table_layout;
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool heap_pool;
    VkDescriptorPool table_pool;
    VkSampler null_sampler;

    /* work done outside the context, initial data and image layouts, run before the context's. */
    CRITICAL_SECTION init_lock;
    VkCommandPool init_pool;
    D3dCommandBuffer init_cmd;
    BOOL init_recording;

    D3dContext *immediate;

    /* what streams out to slots without a buffer, which keeps nothing. */
    D3dAllocation so_dummy;

    /* query slots. */
    SRWLOCK query_lock;
    D3dQueryPool *query_pools[D3D_QUERY_KINDS];
    D3dRetiredSlots query_retired[D3D_QUERY_KINDS];

    /* deferred destruction. */
    SRWLOCK garbage_lock;
    D3dGarbage *garbage;
    UINT garbage_count, garbage_capacity;

    /* state objects, one per distinct description as direct3d hands them out. */
    CRITICAL_SECTION state_lock;
    struct D3dBlendState *blend_states;
    struct D3dRasterizerState *rasterizer_states;
    struct D3dDepthStencilState *depth_stencil_states;
    struct D3dSamplerState *sampler_states;

    /* uniform buffer descriptors by buffer, offset and size. */
    SRWLOCK cbv_lock;
    struct D3dCbvEntry *cbv;
    UINT cbv_count, cbv_capacity;
};

void D3dDefer(D3dDevice *device, UINT64 serial, D3dGarbageType type, UINT64 handle);
void D3dDeferFree(D3dDevice *device, UINT64 serial, D3dAllocation *alloc);
HRESULT D3dMemoryInit(D3dDevice *device);
UINT D3dCbvIndex(D3dDevice *device, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

D3dDevice *D3dDeviceFrom(ID3D11Device *iface);
HRESULT D3dCreateDevice(IDXGIAdapter *adapter, UINT flags, const D3D_FEATURE_LEVEL *levels, UINT count,
                        D3dDevice **out);
void D3dChildInit(D3dChild *child, D3dDevice *device);
void D3dChildRelease(D3dChild *child);

/* the device's queue and timeline. */
UINT64 D3dSubmit(D3dDevice *device, VkCommandBuffer cmd, const VkSemaphoreSubmitInfo *wait,
                 const VkSemaphoreSubmitInfo *signal);
UINT64 D3dCompleted(D3dDevice *device);
void D3dQueueWait(D3dDevice *device, VkSemaphore semaphore, UINT64 value);
void D3dWaitSerial(D3dDevice *device, UINT64 serial);
VkCommandBuffer D3dInitCommands(D3dDevice *device);
void D3dInitDone(D3dDevice *device);
VkCommandBuffer D3dTakeInitCommands(D3dDevice *device);

/* memory.c. */
HRESULT D3dAllocBuffer(D3dDevice *device, D3dMemoryKind kind, VkDeviceSize size, VkDeviceSize align,
                       D3dAllocation *out);
HRESULT D3dAllocImage(D3dDevice *device, VkImage image, D3dAllocation *out);
/* an image of memory of its own that other devices can import, or memory imported from one: the shared id of the object. */
HRESULT D3dAllocImageShared(D3dDevice *device, VkImage image, UINT64 import_id, const MimeShareBlob *blob,
                            D3dAllocation *out, UINT *type_index);
void D3dFree(D3dDevice *device, D3dAllocation *alloc);
BOOL D3dRingAlloc(D3dDevice *device, VkDeviceSize size, VkDeviceSize align, D3dRingSlice *out);
/* chunks owned by command lists: made like the ring's, returned to it when the list is done. */
D3dRingChunk *D3dRingChunkNew(D3dDevice *device, VkDeviceSize size);
BOOL D3dRingChunkAlloc(D3dRingChunk *chunk, VkDeviceSize size, VkDeviceSize align, D3dRingSlice *out);
void D3dRingTrim(D3dDevice *device, const D3dRingSlice *slice, VkDeviceSize reserved, VkDeviceSize used);
void D3dRingChunkRelease(D3dDevice *device, D3dRingChunk *chunk, UINT64 serial);
void D3dRingRetire(D3dDevice *device, UINT64 serial);
VkDescriptorSet D3dRingTableSet(D3dDevice *device, D3dRingChunk *chunk);
void D3dMemoryDestroy(D3dDevice *device);

/*
 * a dynamic constant buffer's bytes after a discard: a slice of a chunk
 * whose uniform buffer descriptors, one each 256 bytes, were made with it.
 * a chunk is reused once no buffer is in it and the gpu is done with it.
 */
typedef struct D3dCbSlice {
    D3dCbChunk *chunk;
    VkBuffer buffer;
    VkDeviceSize offset;
    BYTE *mapped;
    UINT cbv;
} D3dCbSlice;
/* a slice, and whether the chunk before was retired, which the caller submits for. */
BOOL D3dCbAlloc(D3dDevice *device, VkDeviceSize size, D3dCbSlice *out, BOOL *retired);
void D3dCbUnpin(D3dDevice *device, D3dCbChunk *chunk, UINT64 serial);

/* the flags a format supports, for CheckFormatSupport. */
UINT D3dFormatSupport(D3dDevice *device, DXGI_FORMAT format, UINT *support2);
VkImageUsageFlags D3dImageUsage(UINT bind, UINT misc, D3D11_USAGE usage);

/* ------------------------------------------------------------------ the context */

typedef struct D3dStageState {
    D3dShader *shader;
    /* the table uploaded last holds what is bound. */
    BOOL table_valid;
    /*
     * the resource behind each entry of the table, and for buffers the
     * version it was read at: a renamed buffer makes the table stale.
     */
    D3dResource *table_resources[DXBC_TABLE_DWORDS];
    UINT table_versions[DXBC_TABLE_DWORDS];
    /* the submission or command list the table's resources were last marked used for. */
    UINT64 table_epoch;
    D3dResource *cb[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT cb_first[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT cb_count[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    D3dView *srv[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    D3dSamplerState *sampler[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    /* compute: the unordered access views. */
    D3dView *uav[D3D11_1_UAV_SLOT_COUNT];
    D3dBindingTable table;
} D3dStageState;

struct D3dContext {
    ID3D11DeviceContext4 iface;
    ID3DUserDefinedAnnotation annotation;
    ID3D11Multithread multithread;
    ID3D11VideoContext1 video;
    D3dChild child;
    D3D11_DEVICE_CONTEXT_TYPE type;
    /* the device's one recursive lock, held by Enter and by every call while protected_ is set. */
    CRITICAL_SECTION lock;
    BOOL protected_;

    /* recording. */
    VkCommandPool pool;
    D3dCommandBuffer *buffers;
    UINT buffer_count;
    D3dCommandBuffer *cmd;
    BOOL rendering;
    /* rendering ended to draw something writing views: the next draws reload the targets. */
    BOOL pending_barrier;
    BOOL state_dirty;
    /* the dynamic state set in the command buffer, so draws set only what changed. */
    struct {
        UINT discard, cull, front, polygon, bias_enable, clamp, clip, topology, restart, points, stream;
        float bias[3];
        UINT depth_test, depth_write, depth_op, stencil_test, stencil_ops[2][4], compare_mask, write_mask, reference;
        VkBool32 blend_enables[8];
        VkColorBlendEquationEXT equations[8];
        VkColorComponentFlags color_masks[8];
        UINT logic, logic_op, alpha_to_coverage, samples, sample_mask;
        float blend_constants[4];
    } dyn;
    D3dRingChunk *table_chunk;
    VkDescriptorSet table_set;
    UINT table_offsets[D3D_STAGES];
    BOOL tables_bound[2];
    BOOL heap_bound[2];
    BOOL compute_bound;
    /* the geometry shader bound, a variant for stream output from a vertex shader; transform feedback begun. */
    VkShaderEXT bound_gs;
    BOOL xfb_active;
    UINT render_samples;
    UINT render_color_count;

    /* the input assembler. */
    D3dInputLayout *layout;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    D3dResource *vb[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    /* past the last vertex buffer bound. */
    UINT vb_high;
    UINT vb_stride[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT vb_offset[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT vb_version[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    D3dResource *ib;
    DXGI_FORMAT ib_format;
    UINT ib_offset;
    UINT ib_version;
    BOOL ia_dirty, vertex_input_dirty;

    /* the stages. */
    D3dStageState stages[D3D_STAGES];
    BOOL shaders_dirty;

    /* the rasterizer and output merger. */
    D3dRasterizerState *rasterizer;
    D3dBlendState *blend;
    FLOAT blend_factor[4];
    UINT sample_mask;
    D3dDepthStencilState *depth_stencil;
    UINT stencil_ref;
    UINT viewport_count, scissor_count;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3dView *rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    UINT rtv_count;
    D3dView *dsv;
    D3dView *om_uav[D3D11_1_UAV_SLOT_COUNT];
    UINT om_uav_start;
    BOOL targets_dirty, rs_dirty, om_dirty, viewports_dirty;
    /* every resource bound for output, gathered when inputs are checked against them. */
    D3dResource *outputs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT + 1 + 2 * D3D11_1_UAV_SLOT_COUNT +
                         D3D11_SO_BUFFER_SLOT_COUNT];
    UINT output_count;
    BOOL outputs_dirty;

    /*
     * deferred contexts: the pool of the command buffer being recorded,
     * and the ring chunks and resources the command list will own.
     */
    VkCommandPool list_pool;
    D3dCommandBuffer list_cmd;
    D3dRingChunk **list_chunks;
    UINT list_chunk_count, list_chunk_capacity;
    D3dResource **list_resources;
    UINT list_resource_count, list_resource_capacity;
    UINT64 list_id;
    /* dynamic buffers a deferred context discarded: the list's own bytes stand for them. */
    struct D3dListRange {
        D3dResource *resource;
        D3dRingSlice slice;
    } *list_ranges;
    UINT list_range_count, list_range_capacity;
    /* the shaders, views and samplers the commands recorded so far use. */
    IUnknown **list_held;
    UINT list_held_count, list_held_capacity;
    /* dynamic textures a deferred context mapped, written to list memory until unmapped. */
    struct {
        D3dResource *resource;
        UINT sub;
        D3dRingSlice slice;
    } list_maps[8];
    UINT list_map_count;

    /*
     * the queries begun and not ended. they count in segments: a vulkan
     * query of each kind at a time, as vulkan has no more, nor any across
     * render pass instances or command buffers. a segment ends when the
     * queries begun change, and counts for all of them. then the slots
     * whose results are still to copy.
     */
    D3dQuery **queries;
    UINT query_count, query_capacity;
    D3dQuerySlot segments[D3D_QUERY_KINDS];
    BOOL segment_open[D3D_QUERY_KINDS];
    D3dQuerySlot *query_copies;
    UINT query_copy_count, query_copy_capacity;

    /* stream output and predication. */
    D3dResource *so[D3D11_SO_BUFFER_SLOT_COUNT];
    UINT so_offset[D3D11_SO_BUFFER_SLOT_COUNT];
    ID3D11Predicate *predicate;
    BOOL predicate_value;
};

extern const MimeAllocator g_d3d_allocator;

/*
 * the device's lock for the length of a call. the calls of the immediate context take it while the program has multithread
 * protection on, which costs a load and a branch while it is off, and the device's calls that make objects and ask
 * questions always, unless the device is single threaded. put D3D_SYNC at the top of a method of the immediate context or
 * D3D_DEVICE_SYNC at the top of one of the device's: the lock is let go when the scope ends, whatever way it ends.
 */
static inline CRITICAL_SECTION *D3dSyncEnter(D3dContext *context)
{
    if (!__atomic_load_n(&context->protected_, __ATOMIC_RELAXED))
        return nullptr;
    EnterCriticalSection(&context->lock);
    return &context->lock;
}

static inline CRITICAL_SECTION *D3dDeviceSyncEnter(D3dDevice *device)
{
    if ((device->flags & D3D11_CREATE_DEVICE_SINGLETHREADED) || !device->immediate)
        return nullptr;
    EnterCriticalSection(&device->immediate->lock);
    return &device->immediate->lock;
}

static inline void D3dSyncLeave(CRITICAL_SECTION **held)
{
    if (*held)
        LeaveCriticalSection(*held);
}

#define D3D_SYNC(context) CRITICAL_SECTION *d3d_held_ __attribute__((cleanup(D3dSyncLeave))) = D3dSyncEnter(context)
#define D3D_DEVICE_SYNC(device)                                                                                        \
    CRITICAL_SECTION *d3d_held_ __attribute__((cleanup(D3dSyncLeave))) = D3dDeviceSyncEnter(device)

HRESULT D3dCreateContext(D3dDevice *device, D3D11_DEVICE_CONTEXT_TYPE type, D3dContext **out);
void D3dContextDestroy(D3dContext *context);
D3dContext *D3dContextFrom(ID3D11DeviceContext *iface);
/* the ID3D11Multithread of an immediate context that has one, and its state, which the device shares. */
void *D3dMultithreadInterface(D3dContext *context);
BOOL D3dSetProtected(D3dContext *context, BOOL protect);
/* records into the context's command buffer, ending rendering for transfers. */
VkCommandBuffer D3dContextCommands(D3dContext *context, BOOL transfer);
void D3dContextFlush(D3dContext *context);
UINT64 D3dContextSubmit(D3dContext *context, const VkSemaphoreSubmitInfo *wait, const VkSemaphoreSubmitInfo *signal);
void D3dContextUseList(D3dContext *context, D3dResource *resource);
/* the resource is read or written by what is being recorded: on the immediate context, a serial. */
static inline void D3dContextUse(D3dContext *context, D3dResource *resource)
{
    if (!resource)
        return;
    if (context->type == D3D11_DEVICE_CONTEXT_IMMEDIATE)
        resource->last_use = context->child.device->next_serial;
    else
        D3dContextUseList(context, resource);
}
BOOL D3dContextRingAlloc(D3dContext *context, VkDeviceSize size, VkDeviceSize align, D3dRingSlice *out);
const D3dRingSlice *D3dContextListRange(const D3dContext *context, const D3dResource *resource);
const D3dRingSlice *D3dContextListDiscard(D3dContext *context, D3dResource *resource);
/* the vulkan range of a buffer as the commands being recorded see it. */
void D3dContextBufferRange(const D3dContext *context, const D3dResource *resource, VkBuffer *buffer,
                           VkDeviceSize *offset);
void D3dBarrier(VkCommandBuffer cmd, D3dDevice *device);

/* states.c, shader.c, query.c: creation. */
HRESULT D3dCreateBlendState(D3dDevice *device, const D3D11_BLEND_DESC1 *desc, D3dBlendState **out);
HRESULT D3dCreateRasterizerState(D3dDevice *device, const D3D11_RASTERIZER_DESC2 *desc, D3dRasterizerState **out);
HRESULT D3dCreateDepthStencilState(D3dDevice *device, const D3D11_DEPTH_STENCIL_DESC *desc,
                                   D3dDepthStencilState **out);
HRESULT D3dCreateSamplerState(D3dDevice *device, const D3D11_SAMPLER_DESC *desc, D3dSamplerState **out);
HRESULT D3dCreateInputLayout(D3dDevice *device, const D3D11_INPUT_ELEMENT_DESC *elements, UINT count,
                             const void *bytecode, SIZE_T length, D3dInputLayout **out);
HRESULT D3dCreateShader(D3dDevice *device, D3dStage stage, const void *bytecode, SIZE_T length,
                        const D3D11_SO_DECLARATION_ENTRY *so, UINT so_count, const UINT *strides,
                        UINT stride_count, UINT rasterized_stream, D3dShader **out);
HRESULT D3dCreateQuery(D3dDevice *device, const D3D11_QUERY_DESC1 *desc, D3dQuery **out);
HRESULT D3dCreateView(D3dDevice *device, D3dViewKind kind, ID3D11Resource *resource, const void *desc,
                      UINT swizzle, D3dView **out);
void D3dCtxClearDepthStencilRects(D3dContext *c, ID3D11DepthStencilView *iface, UINT flags, FLOAT depth,
                                  UINT8 stencil, const D3D11_RECT *rects, UINT count);
void D3dCtxBlit(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, const RECT *dst_rect, ID3D11Resource *src,
                UINT src_sub, const RECT *src_rect, BOOL linear);
void D3dCtxCopyResource(D3dContext *c, ID3D11Resource *dst, ID3D11Resource *src);
D3dBlendState *D3dBlendFrom(ID3D11BlendState *iface);
D3dRasterizerState *D3dRasterizerFrom(ID3D11RasterizerState *iface);
D3dDepthStencilState *D3dDepthStencilFrom(ID3D11DepthStencilState *iface);
D3dSamplerState *D3dSamplerFrom(ID3D11SamplerState *iface);
D3dInputLayout *D3dInputLayoutFrom(ID3D11InputLayout *iface);
D3dShader *D3dShaderFrom(void *iface, D3dStage stage);
D3dQuery *D3dQueryFrom(ID3D11Asynchronous *iface);
HRESULT D3dQueryGetData(D3dContext *context, D3dQuery *query, void *data, UINT size, UINT flags);
void D3dQueryBegin(D3dContext *context, D3dQuery *query);
void D3dQueryEnd(D3dContext *context, D3dQuery *query);
void D3dQueriesOpen(D3dContext *context, VkCommandBuffer cmd, BOOL drawing);
void D3dQueriesClose(D3dContext *context, VkCommandBuffer cmd);
void D3dQueriesCopy(D3dContext *context, VkCommandBuffer cmd);
void D3dQueriesRelease(D3dContext *context);
void D3dQueryPoolsDestroy(D3dDevice *device);

/*
 * d3d10_device.c and d3d10_objects.c: the direct3d 10 interface of an object
 * for an iid, or NULL. d3d10.dll and d3d10_1.dll make devices with these
 * flags, which give them ID3D10Device1.
 */
#define D3D_CREATE_DEVICE_D3D10 0x40000000
#define D3D_CREATE_DEVICE_D3D10_1 0x80000000
void *D3d10DeviceInterface(D3dDevice *device, REFIID riid);
void *D3d10ResourceInterface(D3dResource *resource, REFIID riid);
void *D3d10ViewInterface(D3dView *view, REFIID riid);
void *D3d10BlendInterface(D3dBlendState *state, REFIID riid);
void *D3d10RasterizerInterface(D3dRasterizerState *state, REFIID riid);
void *D3d10DepthStencilInterface(D3dDepthStencilState *state, REFIID riid);
void *D3d10SamplerInterface(D3dSamplerState *state, REFIID riid);
void *D3d10InputLayoutInterface(D3dInputLayout *layout, REFIID riid);
void *D3d10ShaderInterface(D3dShader *shader, REFIID riid);
void *D3d10AsyncInterface(D3dQuery *query, REFIID riid);

/* swapchain.c. */
HRESULT D3dCreateSwapChain(D3dDevice *device, IDXGIFactory *factory, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen, IDXGISwapChain1 **out);
HRESULT D3dCreateCompositionSwapChain(D3dDevice *device, IDXGIFactory *factory, HANDLE surface,
                                     const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGISwapChain1 **out);

/* video.c: direct3d 11 video over the host's decode queue. */
void D3dVideoProbe(D3dDevice *device);
void *D3dVideoDeviceInterface(D3dDevice *device, REFIID riid);
void *D3dVideoContextInterface(D3dContext *context, REFIID riid);
void D3dVideoDeviceInit(D3dDevice *device);
void D3dVideoContextInit(D3dContext *context);
void D3dVideoDeviceDestroy(D3dDevice *device);
void D3dVideoProcessorDestroy(D3dDevice *device);
HRESULT D3dVideoCreateEnumerator(D3dDevice *device, const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc,
                                 ID3D11VideoProcessorEnumerator **out);
HRESULT D3dVideoCreateProcessor(D3dDevice *device, ID3D11VideoProcessorEnumerator *enumerator, UINT rate_index,
                                ID3D11VideoProcessor **out);
HRESULT D3dVideoCreateInputView(D3dDevice *device, ID3D11Resource *resource, ID3D11VideoProcessorEnumerator *enumerator,
                                const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *desc, ID3D11VideoProcessorInputView **out);
HRESULT D3dVideoCreateOutputView(D3dDevice *device, ID3D11Resource *resource,
                                 ID3D11VideoProcessorEnumerator *enumerator,
                                 const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *desc,
                                 ID3D11VideoProcessorOutputView **out);

/* the debug output of the dll, to the kernel debugger log. */
void D3dLog(const char *format, ...);

#endif
