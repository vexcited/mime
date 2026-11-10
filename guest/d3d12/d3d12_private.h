/*
 * d3d12.dll: Direct3D 12 over Vulkan 1.3. heaps are Vulkan memory with a buffer over it, descriptor heaps are
 * descriptor buffers, root signatures are push constants and pipeline states are shader objects with all other
 * state dynamic.
 */
#ifndef D3D12_PRIVATE_H
#define D3D12_PRIVATE_H

#define MIME_BUILD_D3D12
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/d3d12.h>
#include <mime/dxgi_mime.h>
#include "../lib/privdata/privdata.h"
#include "../lib/dxbc/dxbc.h"
#include "../lib/dxil/dxil.h"

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include "../d3d11/format.h"
#include "../d3d11/vkprofile.h"
#include "../d3d11/gfxshare.h"
#include <mime/mvk_share.h>

#define D12_MAX(a, b) ((a) > (b) ? (a) : (b))
#define D12_MIN(a, b) ((a) < (b) ? (a) : (b))
#define D12_ALIGN(v, a) (((v) + (a)-1) & ~((UINT64)(a)-1))

/* ------------------------------------------------------------------ vulkan entry points */

#define D12_VK_INSTANCE_FUNCS(X)                                                                                       \
    X(vkGetPhysicalDeviceProperties2)                                                                                  \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceFormatProperties)                                                                             \
    X(vkGetPhysicalDeviceSparseImageFormatProperties)                                                                  \
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

#define D12_VK_DEVICE_FUNCS(X)                                                                                         \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueSubmit2)                                                                                                  \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkResetCommandPool)                                                                                              \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkResetCommandBuffer)                                                                                            \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkWaitSemaphores)                                                                                                \
    X(vkSignalSemaphore)                                                                                               \
    X(vkGetSemaphoreCounterValue)                                                                                      \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkGetBufferDeviceAddress)                                                                                        \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkGetImageSparseMemoryRequirements)                                                                              \
    X(vkQueueBindSparse)                                                                                               \
    X(vkGetDeviceImageMemoryRequirements)                                                                              \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkGetDescriptorSetLayoutSizeEXT)                                                                                 \
    X(vkGetDescriptorEXT)                                                                                              \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreateGraphicsPipelines)                                                                                       \
    X(vkCreateComputePipelines)                                                                                        \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreatePipelineCache)                                                                                           \
    X(vkDestroyPipelineCache)                                                                                          \
    X(vkCreateQueryPool)                                                                                               \
    X(vkDestroyQueryPool)                                                                                              \
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
    X(vkCmdResolveImage)                                                                                               \
    X(vkCmdBlitImage)                                                                                                  \
    X(vkCmdFillBuffer)                                                                                                 \
    X(vkCmdUpdateBuffer)                                                                                               \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdPushConstants)                                                                                              \
    X(vkCmdBindDescriptorBuffersEXT)                                                                                   \
    X(vkCmdSetDescriptorBufferOffsetsEXT)                                                                              \
    X(vkCmdBindDescriptorBufferEmbeddedSamplersEXT)                                                                    \
    X(vkCmdBindVertexBuffers2)                                                                                         \
    X(vkCmdBindIndexBuffer)                                                                                            \
    X(vkCmdDraw)                                                                                                       \
    X(vkCmdDrawIndexed)                                                                                                \
    X(vkCmdDrawIndirect)                                                                                               \
    X(vkCmdDrawIndexedIndirect)                                                                                        \
    X(vkCmdDrawIndirectCount)                                                                                          \
    X(vkCmdDrawIndexedIndirectCount)                                                                                   \
    X(vkCmdDispatch)                                                                                                   \
    X(vkCmdDrawMeshTasksEXT)                                                                                           \
    X(vkCreateIndirectCommandsLayoutEXT)                                                                               \
    X(vkDestroyIndirectCommandsLayoutEXT)                                                                              \
    X(vkGetGeneratedCommandsMemoryRequirementsEXT)                                                                     \
    X(vkCmdExecuteGeneratedCommandsEXT)                                                                                \
    X(vkCmdDrawMeshTasksIndirectEXT)                                                                                   \
    X(vkCmdDrawMeshTasksIndirectCountEXT)                                                                              \
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
    X(vkCmdSetDepthBounds)                                                                                             \
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
    X(vkCmdSetAlphaToOneEnableEXT)                                                                                     \
    X(vkCmdSetLogicOpEnableEXT)                                                                                        \
    X(vkCmdSetLogicOpEXT)                                                                                              \
    X(vkCmdSetColorBlendEnableEXT)                                                                                     \
    X(vkCmdSetColorBlendEquationEXT)                                                                                   \
    X(vkCmdSetColorWriteMaskEXT)                                                                                       \
    X(vkCmdSetDepthClampEnableEXT)                                                                                     \
    X(vkCmdSetDepthClipEnableEXT)                                                                                      \
    X(vkCmdSetVertexInputEXT)                                                                                          \
    X(vkCmdSetPatchControlPointsEXT)                                                                                   \
    X(vkCmdSetFragmentShadingRateKHR)                                                                                  \
    X(vkCmdSetTessellationDomainOriginEXT)                                                                             \
    X(vkCmdSetRasterizationStreamEXT)                                                                                  \
    X(vkCmdSetConservativeRasterizationModeEXT)                                                                        \
    X(vkCmdSetLineRasterizationModeEXT)                                                                                \
    X(vkCmdSetLineStippleEnableEXT)                                                                                    \
    X(vkCmdBeginQuery)                                                                                                 \
    X(vkCmdEndQuery)                                                                                                   \
    X(vkCmdBeginQueryIndexedEXT)                                                                                       \
    X(vkCmdEndQueryIndexedEXT)                                                                                         \
    X(vkCmdResetQueryPool)                                                                                             \
    X(vkCmdWriteTimestamp2)                                                                                            \
    X(vkCmdCopyQueryPoolResults)                                                                                       \
    X(vkCmdBindTransformFeedbackBuffersEXT)                                                                            \
    X(vkCmdBeginTransformFeedbackEXT)                                                                                  \
    X(vkCmdEndTransformFeedbackEXT)                                                                                    \
    X(vkCmdBeginConditionalRenderingEXT)                                                                               \
    X(vkCmdEndConditionalRenderingEXT)                                                                                 \
    X(vkGetAccelerationStructureBuildSizesKHR)                                                                         \
    X(vkCreateAccelerationStructureKHR)                                                                                \
    X(vkDestroyAccelerationStructureKHR)                                                                               \
    X(vkGetAccelerationStructureDeviceAddressKHR)                                                                      \
    X(vkGetDeviceAccelerationStructureCompatibilityKHR)                                                                \
    X(vkCmdBuildAccelerationStructuresKHR)                                                                             \
    X(vkCmdCopyAccelerationStructureKHR)                                                                               \
    X(vkCmdCopyAccelerationStructureToMemoryKHR)                                                                       \
    X(vkCmdCopyMemoryToAccelerationStructureKHR)                                                                       \
    X(vkCmdWriteAccelerationStructuresPropertiesKHR)                                                                   \
    X(vkCreateRayTracingPipelinesKHR)                                                                                  \
    X(vkGetRayTracingShaderGroupHandlesKHR)                                                                            \
    X(vkGetRayTracingShaderGroupStackSizeKHR)                                                                          \
    X(vkCmdTraceRaysKHR)                                                                                               \
    X(vkCmdSetRayTracingPipelineStackSizeKHR)                                                                          \
    X(vkCmdTraceRaysIndirect2KHR)

typedef struct D12Vk {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define X(name) PFN_##name name;
    D12_VK_INSTANCE_FUNCS(X)
    D12_VK_DEVICE_FUNCS(X)
#undef X
    /* mime's vulkan-1.dll extensions to share memory and semaphores with other processes. */
    PFN_vkMxShareExportMemory vkMxShareExportMemory;
    PFN_vkMxShareImportMemory vkMxShareImportMemory;
    PFN_vkMxShareExportSemaphore vkMxShareExportSemaphore;
    PFN_vkMxShareImportSemaphore vkMxShareImportSemaphore;
} D12Vk;

/* ------------------------------------------------------------------ objects */

typedef struct D12Device D12Device;
typedef struct D12Resource D12Resource;
typedef struct D12Heap D12Heap;

typedef struct D12Child {
    LONG refs;
    D12Device *device;
    MimePrivateData data;
} D12Child;

void D12ChildInit(D12Child *child, D12Device *device);
void D12ChildRelease(D12Child *child);
HRESULT D12ChildGetDevice(D12Child *child, REFIID riid, void **out);

/* ------------------------------------------------------------------ memory */

typedef enum D12MemoryKind {
    D12_MEMORY_DEVICE,   /* gpu memory, not mapped. */
    D12_MEMORY_UPLOAD,   /* guest ram the cpu writes and the gpu reads. */
    D12_MEMORY_READBACK, /* cached guest ram the gpu writes and the cpu reads. */
    D12_MEMORY_KINDS
} D12MemoryKind;

/*
 * one Vulkan allocation, plus a buffer over all of it when buffers may be placed in it. the buffer's device
 * address is the heap's gpu virtual address. committed resources are ranges of the device's internal heaps.
 */
struct D12Heap {
    ID3D12Heap1 iface;
    D12Child child;
    D3D12_HEAP_DESC desc;
    D12MemoryKind kind;
    UINT type;
    VkDeviceMemory memory;
    VkBuffer buffer;
    D3D12_GPU_VIRTUAL_ADDRESS va;
    BYTE *mapped;
    /* internal heaps that committed resources come from, with their free ranges. */
    BOOL internal;
    /* in system memory, since video memory had no room for it. */
    BOOL demoted;
    /* over memory that denies buffers, to clear it when the heap is made. */
    VkBuffer zero_buffer;
    struct D12Heap *next;
    struct D12Range *free;
    UINT free_count, free_capacity;
    UINT64 handed_out; /* no range past this offset was ever given out. */
    /* placed textures alike at one offset share one image, see create_texture. */
    SRWLOCK images_lock;
    struct D12SharedImage *images;
};

/* the image of the textures placed alike at one offset of a heap. */
typedef struct D12SharedImage {
    struct D12SharedImage *next;
    struct D12Heap *heap;
    UINT64 offset;
    D3D12_RESOURCE_DESC1 desc;
    VkImage image;
    LONG refs;
    /* the views of every texture that shares the image, since games place a new one alike each frame. */
    SRWLOCK views_lock;
    struct D12ImageView *views;
} D12SharedImage;

HRESULT D12CreateHeap(D12Device *device, const D3D12_HEAP_DESC *desc, D12Heap **out);
D12Heap *D12HeapFrom(ID3D12Heap *iface);

/* a range of a heap, for committed resources. */
typedef struct D12Allocation {
    D12Heap *heap;
    UINT64 offset, size;
    BOOL fresh; /* never used before, so mapped memory is still zero from the driver. */
} D12Allocation;

HRESULT D12Alloc(D12Device *device, D12MemoryKind kind, UINT type_bits, UINT64 size, UINT64 align, BOOL image,
                 D12Allocation *out);
void D12Free(D12Device *device, D12Allocation *alloc);
D12MemoryKind D12HeapKind(D12Device *device, const D3D12_HEAP_PROPERTIES *props);
void D12MemoryDestroy(D12Device *device);

/* the heap buffer behind a gpu virtual address, and the offset in it. */
BOOL D12AddressLookup(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va, VkBuffer *buffer, VkDeviceSize *offset,
                      UINT64 *available);
void D12AddressAdd(D12Device *device, D12Heap *heap);
void D12AddressRemove(D12Device *device, D12Heap *heap);

/* ------------------------------------------------------------------ resources */

/* made once per view description and kept with the resource, or with the image it shares. */
typedef struct D12ImageView {
    struct D12ImageView *next;
    VkImageViewCreateInfo info;
    VkImageViewUsageCreateInfo usage;
    VkImageView view;
    BYTE descriptor[2][64];
    BOOL have[2];
} D12ImageView;

struct D12Resource {
    ID3D12Resource2 iface;
    D12Child child;
    D3D12_RESOURCE_DESC1 desc;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_HEAP_FLAGS heap_flags;
    D3D12_RESOURCE_STATES initial_state;
    /* placed in a heap, or a range of an internal heap when committed. */
    D12Heap *heap;
    UINT64 heap_offset;
    D12Allocation alloc;
    BOOL committed;
    /* buffers: the heap's buffer, the offset in it and the gpu virtual address. */
    VkBuffer buffer;
    VkDeviceSize offset;
    D3D12_GPU_VIRTUAL_ADDRESS va;
    BYTE *mapped;
    LONG map_count;
    /* textures, and the image the texture shares with others placed alike, if it does. */
    const D3dFormat *fmt;
    VkImage image;
    D12SharedImage *shared_image;
    VkImageAspectFlags aspect;
    VkImageUsageFlags usage;
    UINT mips, layers, samples;
    BOOL swapchain;
    SRWLOCK views_lock;
    D12ImageView *views;
    /* reserved resources use 64 KiB tiles. buffers get their own address range, images a tile shape and mip tail. */
    BOOL reserved;
    struct D12Heap *sparse;
    VkExtent3D tile_shape;
    UINT standard_mips, packed_tiles;
    VkDeviceSize tail_offset, tail_stride;
    BOOL single_tail;
    UINT tile_count;
    struct D12Tile *tiles;
    SRWLOCK tiles_lock;
    /* a shareable texture owns its memory, which other devices import. import_* is only set while opening. */
    VkDeviceMemory own_memory;
    MimeShareBlob *share_blob;
    MimeShare share;
    SRWLOCK share_lock;
    const MimeShareBlob *import_blob;
    MimeShare import_share;
};

/* where a reserved tile maps, heap is NULL when unmapped. */
typedef struct D12Tile {
    struct D12Heap *heap;
    UINT tile;
} D12Tile;

/* a reserved tile: in a mip's tile grid, among a layer's packed tiles, or in a buffer. */
typedef struct D12TilePlace {
    BOOL packed;
    UINT mip, layer;
    VkOffset3D at;
    VkExtent3D extent;
    VkDeviceSize opaque;
} D12TilePlace;

HRESULT D12CreateReservedResource(D12Device *device, const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES state,
                                  D12Resource **out);
void D12ResourceTiling(D12Resource *resource, UINT *tile_count, D3D12_PACKED_MIP_INFO *packed,
                       D3D12_TILE_SHAPE *shape, UINT *subresource_count, UINT first, D3D12_SUBRESOURCE_TILING *tilings);
UINT D12TileIndex(const D12Resource *resource, const D3D12_TILED_RESOURCE_COORDINATE *at);
BOOL D12TileAt(const D12Resource *resource, UINT tile, D12TilePlace *place);
/* lists a region's tiles in order, as a box from start or as a run in resource order, and returns the count. */
UINT D12RegionTiles(const D12Resource *resource, const D3D12_TILED_RESOURCE_COORDINATE *start,
                    const D3D12_TILE_REGION_SIZE *size, UINT *tiles, UINT max);
void D12FreeTiles(D12Resource *resource);

D12Resource *D12ResourceFrom(ID3D12Resource *iface);
/* share.c: shareable committed textures and their handles. */
HRESULT D12ResourceShare(D12Resource *resource, const SECURITY_ATTRIBUTES *attributes, LPCWSTR name, HANDLE *out);
HRESULT D12OpenSharedResource(D12Device *device, HANDLE handle, REFIID riid, void **out);
void D12ShareDescribe(D12Resource *resource, const VkImageCreateInfo *info, UINT type_index, UINT64 memory_size);
void D12ShareDestroy(D12Resource *resource);
/* a texture over a shared object's memory, with the description the object was made with. */
HRESULT D12CreateResourceImported(D12Device *device, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
                                  const D3D12_RESOURCE_DESC1 *desc, const MimeShareBlob *blob, const MimeShare *share,
                                  D12Resource **out);
HRESULT D12CreateResource(D12Device *device, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
                          const D3D12_RESOURCE_DESC1 *desc, D12Heap *heap, UINT64 offset,
                          D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear, D12Resource **out);
HRESULT D12CreateSwapchainResource(D12Device *device, VkImage image, const D3D12_RESOURCE_DESC1 *desc,
                                   D12Resource **out);
D3D12_RESOURCE_ALLOCATION_INFO D12AllocationInfo(D12Device *device, const D3D12_RESOURCE_DESC1 *desc);
UINT D12SubresourceCount(const D12Resource *resource);
void D12SubresourceDecompose(const D12Resource *resource, UINT index, UINT *mip, UINT *layer, UINT *plane);
VkExtent3D D12MipExtent(const D12Resource *resource, UINT mip);
void D12Footprints(D12Device *device, const D3D12_RESOURCE_DESC1 *desc, UINT first, UINT count, UINT64 base,
                   D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *rows, UINT64 *row_size, UINT64 *total);
D12ImageView *D12ResourceView(D12Resource *resource, const VkImageViewCreateInfo *info, VkImageUsageFlags usage);
VkImageUsageFlags D12ImageUsage(D3D12_RESOURCE_FLAGS flags, const D3dFormat *fmt);
VkImageAspectFlags D12PlaneAspect(const D12Resource *resource, UINT plane);
BOOL D12IsFeedback(DXGI_FORMAT format);
VkExtent2D D12FeedbackExtent(const D3D12_RESOURCE_DESC1 *desc);

/* ------------------------------------------------------------------ descriptors */

/*
 * a descriptor handle holds the heap id above D12_HANDLE_SHIFT and the slot times the increment below. ids index
 * the device's heap table, so a handle finds its heap at once.
 */
#ifdef _WIN64
#define D12_HANDLE_SHIFT 40
#define D12_INCREMENT_SHIFT 5
#else
#define D12_HANDLE_SHIFT 20
#define D12_INCREMENT_SHIFT 0
#endif
#define D12_INCREMENT (1u << D12_INCREMENT_SHIFT)
#define D12_MAX_HEAPS (1u << (sizeof(SIZE_T) * 8 - D12_HANDLE_SHIFT))

/* what a cbv, srv or uav is to shaders: a Vulkan descriptor or a record. */
typedef enum D12DescriptorKind {
    D12_DESC_NONE,
    D12_DESC_SAMPLED_IMAGE,
    D12_DESC_STORAGE_IMAGE,
    D12_DESC_UNIFORM_TEXEL,
    D12_DESC_STORAGE_TEXEL,
    D12_DESC_RECORD,
} D12DescriptorKind;

/* the view dimensions of shader resource and unordered access views, which share their numbers up to 3d. */
#define D12_NULL_DIMENSIONS 11
/* the images the stand ins of null views are views of: 1d, 2d and cube, 3d, multisampled 2d. */
#define D12_NULL_IMAGES 4

/* what cpu only heaps keep beside a view's bytes, for ClearUnorderedAccessView. */
typedef struct D12DescriptorInfo {
    D12Resource *resource;
    D12ImageView *view;
    VkFormat format;
    VkImageSubresourceRange range;
    UINT64 offset, size;
    BYTE kind;
} D12DescriptorInfo;

typedef struct D12TargetView {
    D12Resource *resource;
    D12ImageView *view;
    VkFormat format;
    VkImageSubresourceRange range;
    VkExtent2D extent;
    UINT samples;
    BOOL read_only_depth, read_only_stencil;
} D12TargetView;

#define D12_STAGE_SLOTS 32

typedef struct D12DescriptorHeap {
    ID3D12DescriptorHeap iface;
    D12Child child;
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    UINT id;
    /* shader visible heaps: the descriptor buffer and its device address. */
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceAddress address;
    /*
     * a staged heap's descriptor buffer is memory the cpu cannot map, as radv keeps descriptor buffers in memory
     * types that cannot take guest ram. the cpu writes a copy in guest ram and marks the pages it wrote for every
     * queue, each queue's next submission copies over what it has not, see D12DescriptorsUpload.
     */
    VkDeviceMemory shadow_memory;
    VkBuffer shadow;
    UINT64 size;
    volatile LONG64 *dirty; /* dirty_words for each queue slot. */
    UINT dirty_words;
    UINT64 upload_serials[D12_STAGE_SLOTS]; /* each queue slot's submission that last copied it. */
    struct D12DescriptorHeap *next_staged;
    /* Vulkan descriptors and records, both in guest memory. */
    BYTE *base;
    BYTE *descriptors;
    DxbcRecord *records;
    UINT stride;
    /* with 64-bit atomics: each slot's view of 64-bit texels, at atomic_offset in the buffer. */
    BYTE *atomics;
    UINT64 atomic_offset;
    D12DescriptorInfo *infos;
    D12TargetView *targets;
} D12DescriptorHeap;

HRESULT D12CreateDescriptorHeap(D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc,
                                D12DescriptorHeap **out);
D12DescriptorHeap *D12DescriptorHeapFrom(ID3D12DescriptorHeap *iface);
D12DescriptorHeap *D12HandleHeap(D12Device *device, UINT64 ptr, UINT *index);
void D12CreateCbv(D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE dest);
void D12CreateSrv(D12Device *device, D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
                  D3D12_CPU_DESCRIPTOR_HANDLE dest);
void D12CreateUav(D12Device *device, D12Resource *resource, D12Resource *counter,
                  const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE dest);
void D12CreateRtv(D12Device *device, D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
                  D3D12_CPU_DESCRIPTOR_HANDLE dest);
void D12CreateDsv(D12Device *device, D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
                  D3D12_CPU_DESCRIPTOR_HANDLE dest);
void D12CreateSampler(D12Device *device, const D3D12_SAMPLER_DESC2 *desc, D3D12_CPU_DESCRIPTOR_HANDLE dest);
void D12CopyDescriptors(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE dest, D3D12_CPU_DESCRIPTOR_HANDLE src,
                        UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type);
const D12TargetView *D12TargetFrom(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE handle);
const D12DescriptorInfo *D12DescriptorInfoFrom(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE handle);
VkSampler D12Sampler(D12Device *device, const D3D12_SAMPLER_DESC2 *desc, const BYTE **descriptor);
HRESULT D12DescriptorsInit(D12Device *device);
void D12DescriptorsDestroy(D12Device *device);

/* ------------------------------------------------------------------ root signatures */

typedef enum D12Stage { D12_VS, D12_HS, D12_DS, D12_GS, D12_PS, D12_CS, D12_AS, D12_MS, D12_STAGES } D12Stage;

/* push constant bytes root arguments may take: 64 dwords, which every Direct3D 12 gpu has. */
#define D12_PUSH_SIZE 256

/* where a root parameter's values are in the push constants. */
typedef struct D12RootParam {
    D3D12_ROOT_PARAMETER_TYPE type;
    UINT push_offset;
    UINT constants;
} D12RootParam;

typedef struct D12RootSignature {
    ID3D12RootSignature iface;
    D12Child child;
    D3D12_ROOT_SIGNATURE_FLAGS flags;
    UINT param_count;
    D12RootParam *params;
    /* the bindings the compiler sees, and the push constant bytes they take. */
    DxbcRootBinding *bindings;
    UINT *binding_stages;
    UINT binding_count;
    UINT push_size;
    /* local root signatures: the size of their arguments in a shader record. */
    UINT record_size;
    UINT static_count;
    VkDescriptorSetLayout static_layout;
    VkPipelineLayout layout;
    /* the serialized form, to compare with the root signatures shaders carry. */
    void *blob;
    SIZE_T blob_size;
} D12RootSignature;

HRESULT D12CreateRootSignature(D12Device *device, const void *blob, SIZE_T size, D12RootSignature **out);
D12RootSignature *D12RootSignatureFrom(ID3D12RootSignature *iface);
DxbcRootBinding *D12RootBindings(D12RootSignature *root, D12Stage stage, UINT *count);
/* parses an RTS0 part into a versioned description, in one block the caller frees. */
HRESULT D12ParseRootSignature(const void *data, SIZE_T size, D3D12_VERSIONED_ROOT_SIGNATURE_DESC **out);
HRESULT D12SerializeRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob,
                                  ID3DBlob **error);
HRESULT D12CreateBlob(const void *data, SIZE_T size, ID3DBlob **out);

/* ------------------------------------------------------------------ pipeline states */

typedef struct D12PipelineState {
    ID3D12PipelineState iface;
    D12Child child;
    BOOL compute;
    /* graphics with mesh shaders, so no vertex input. */
    BOOL mesh;
    D12RootSignature *root;
    VkPipeline pipeline;
    UINT stage_mask;
    /* graphics: fixed function state, set dynamically when the state is set. */
    D3D12_BLEND_DESC blend;
    UINT sample_mask;
    D3D12_RASTERIZER_DESC2 rasterizer;
    D3D12_DEPTH_STENCIL_DESC2 depth_stencil;
    UINT attribute_count;
    VkVertexInputAttributeDescription2EXT attributes[32];
    UINT slot_mask;
    BYTE instanced[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT step_rate[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type;
    UINT rt_count;
    DXGI_FORMAT rtv_formats[8];
    DXGI_FORMAT dsv_format;
    VkFormat depth_format, stencil_format;
    UINT samples;
    UINT patch_points;
    UINT rasterized_stream;
    BOOL stream_output;
    UINT so_strides[D3D12_SO_BUFFER_SLOT_COUNT];
    BOOL depth_bounds;
} D12PipelineState;

HRESULT D12CreateGraphicsPipeline(D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                                  D12PipelineState **out);
HRESULT D12CreateComputePipeline(D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                                 D12PipelineState **out);
HRESULT D12CreatePipelineFromStream(D12Device *device, const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                                    D12PipelineState **out);
D12PipelineState *D12PipelineFrom(ID3D12PipelineState *iface);

/* ------------------------------------------------------------------ queries and command signatures */

typedef struct D12QueryHeap {
    ID3D12QueryHeap iface;
    D12Child child;
    D3D12_QUERY_HEAP_DESC desc;
    VkQueryPool pool;
    /* stream output statistics: a pool per stream. */
    VkQueryPool so_pools[4];
} D12QueryHeap;

HRESULT D12CreateQueryHeap(D12Device *device, const D3D12_QUERY_HEAP_DESC *desc, D12QueryHeap **out);
D12QueryHeap *D12QueryHeapFrom(ID3D12QueryHeap *iface);

typedef struct D12CommandSignature {
    ID3D12CommandSignature iface;
    D12Child child;
    UINT stride;
    UINT arg_count;
    D3D12_INDIRECT_ARGUMENT_DESC *args;
    D12RootSignature *root;
    /* generated commands: their layout and the stages it reaches, when the signature needs them. */
    VkIndirectCommandsLayoutEXT layout;
    VkShaderStageFlags stages;
} D12CommandSignature;

HRESULT D12CreateCommandSignature(D12Device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
                                  D12RootSignature *root, D12CommandSignature **out);
D12CommandSignature *D12CommandSignatureFrom(ID3D12CommandSignature *iface);

/* ------------------------------------------------------------------ fences and queues */

typedef struct D12FenceWait {
    UINT64 value;
    HANDLE event;
} D12FenceWait;

typedef struct D12Fence {
    ID3D12Fence1 iface;
    D12Child child;
    D3D12_FENCE_FLAGS flags;
    VkSemaphore semaphore;
    /* the highest value seen completed, and the pending cpu waits. */
    volatile UINT64 completed;
    /* the highest value a submission or the cpu signals, so waits up to it only wait for the gpu. */
    volatile UINT64 queued;
    SRWLOCK lock;
    D12FenceWait *waits;
    UINT wait_count, wait_capacity;
    struct D12Fence *next_waiting;
    /* the kernel object of a shared fence, once it has a handle or was opened from one. */
    MimeShare share;
    /* the process table entry holding the semaphore when other fences share it, see d3d11's share.c. */
    void *entry;
} D12Fence;

HRESULT D12CreateFence(D12Device *device, UINT64 value, D3D12_FENCE_FLAGS flags, D12Fence **out);
/* the first handle makes the fence shared. */
HRESULT D12FenceShare(D12Fence *fence, const SECURITY_ATTRIBUTES *attributes, LPCWSTR name, HANDLE *out);
HRESULT D12FenceOpen(D12Device *device, HANDLE handle, D12Fence **out);
D12Fence *D12FenceFrom(ID3D12Fence *iface);
UINT64 D12FenceValue(D12Fence *fence);
void D12FenceWake(D12Device *device);

typedef struct D12Queue {
    ID3D12CommandQueue iface;
    IMimeDXGIPresenter presenter;
    D12Child child;
    D3D12_COMMAND_QUEUE_DESC desc;
    VkQueue queue;
    SRWLOCK *queue_lock;
    UINT family;
    /* the queue's own timeline: what was submitted and what the gpu finished. */
    VkSemaphore timeline;
    UINT64 next_serial;
    /* the tile mapping serial the next submission must wait for, since Vulkan does not order them. */
    UINT64 bind_serial;
    /* what the queue got after a wait for a value nothing signals yet, see queue.c. */
    struct D12QueueOp *held, **held_tail;
    struct D12Queue *next;
    /* its slot among the queues staged descriptor heaps upload for, and the command buffers of its uploads. */
    int stage_slot;
    VkCommandPool upload_pool;
    struct D12Upload *uploads;
    UINT upload_count, upload_capacity;
} D12Queue;

typedef struct D12QueueOp D12QueueOp;

HRESULT D12CreateQueue(D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, D12Queue **out);
/* a queue takes a slot among those staged descriptor heaps upload for, and gives it back once idle. */
void D12DescriptorsAddQueue(D12Queue *queue);
void D12DescriptorsRemoveQueue(D12Queue *queue);
/*
 * the copies of what the cpu wrote to staged descriptor heaps since the queue's last upload, for the submission that
 * signals serial on the queue's timeline to run first; VK_NULL_HANDLE when there is nothing to copy.
 */
VkCommandBuffer D12DescriptorsUpload(D12Queue *queue, UINT64 serial);
D12Queue *D12QueueFrom(ID3D12CommandQueue *iface);
UINT64 D12QueueSubmit(D12Queue *queue, const VkCommandBuffer *buffers, UINT count, const VkSemaphoreSubmitInfo *waits,
                      UINT wait_count, const VkSemaphoreSubmitInfo *signals, UINT signal_count);
void D12QueueWaitIdle(D12Queue *queue);
/* lets what the queue holds back go to vulkan first, for work that goes there at once. */
void D12QueueSettle(D12Queue *queue);
void D12UpdateTileMappings(D12Queue *queue, D12Resource *resource, UINT region_count,
                           const D3D12_TILED_RESOURCE_COORDINATE *starts, const D3D12_TILE_REGION_SIZE *sizes,
                           struct D12Heap *heap, UINT range_count, const D3D12_TILE_RANGE_FLAGS *flags,
                           const UINT *heap_starts, const UINT *range_counts);
void D12CopyTileMappings(D12Queue *queue, D12Resource *dst, const D3D12_TILED_RESOURCE_COORDINATE *dst_start,
                         D12Resource *src, const D3D12_TILED_RESOURCE_COORDINATE *src_start,
                         const D3D12_TILE_REGION_SIZE *size);

HRESULT D12CreateSwapChain(D12Queue *queue, IDXGIFactory *factory, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen, IDXGISwapChain1 **out);

/* ------------------------------------------------------------------ command lists */

typedef struct D12CommandAllocator {
    ID3D12CommandAllocator iface;
    D12Child child;
    D3D12_COMMAND_LIST_TYPE type;
    VkCommandPool pool;
    VkCommandBuffer *buffers;
    UINT used, count, capacity;
    /* the list recording with it, which a reset may not overlap. */
    struct D12CommandList *recording;
    /* generated command memory, reused at each reset. */
    struct D12Scratch *scratch;
    /* queries the lists' commands use, such as acceleration structure sizes, reused at each reset. */
    struct D12QueryBlock *queries;
} D12CommandAllocator;

typedef struct D12QueryBlock {
    struct D12QueryBlock *next;
    VkQueryPool pool;
    VkQueryType type;
    UINT used;
} D12QueryBlock;

/* takes count queries of a type from the allocator; the caller resets them before use. */
BOOL D12QueryAlloc(D12CommandAllocator *allocator, VkQueryType type, UINT count, VkQueryPool *pool, UINT *first);

/* a block of device memory that commands of an allocator take from in order. */
typedef struct D12Scratch {
    struct D12Scratch *next;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress address;
    UINT64 size, used;
    UINT type;
} D12Scratch;

/* device memory for generated commands from the allocator. */
BOOL D12ScratchAlloc(D12CommandAllocator *allocator, UINT64 size, UINT64 alignment, UINT type_bits,
                     VkDeviceAddress *address);

HRESULT D12CreateCommandAllocator(D12Device *device, D3D12_COMMAND_LIST_TYPE type, D12CommandAllocator **out);
D12CommandAllocator *D12AllocatorFrom(ID3D12CommandAllocator *iface);

/* the root arguments of one bind point, as pushed. */
typedef struct D12RootState {
    D12RootSignature *root;
    UINT push[64];
    BOOL push_dirty;
    /* words changed since the last push. only these go again while the push constants stay ours. */
    UINT push_lo, push_hi;
    BOOL sets_dirty;
} D12RootState;

typedef struct D12CommandList {
    ID3D12GraphicsCommandList10 iface;
    /* the ray tracing pipeline set, and the one bound. */
    struct D12StateObject *rt_state;
    VkPipeline bound_rt;
    D12Child child;
    D3D12_COMMAND_LIST_TYPE type;
    D12CommandAllocator *allocator;
    VkCommandBuffer cmd;
    BOOL open;
    /* bundles: the recorded calls the executing list replays, and the initial pipeline state. */
    BOOL recording;
    BYTE *calls;
    SIZE_T call_size, call_capacity;
    struct D12PipelineState *bundle_initial;
    HRESULT error;

    D12PipelineState *pipeline;
    /* what the command buffer has bound: the graphics state's shaders and the compute shader. */
    D12PipelineState *bound_graphics;
    VkPipeline bound_cs;
    /* the attachments of the current rendering, laid out by the graphics state bound when it began. */
    UINT render_rt_count;
    VkFormat render_depth_format, render_stencil_format;
    D12RootState graphics, compute;
    /* the bind point whose root arguments are in the push constants, -1 for none. */
    int pushed_compute;
    D12DescriptorHeap *heaps[2];
    BOOL heaps_dirty;
    /* a copy, fill or clear was recorded since the last draw, dispatch or barrier the list made after one. */
    BOOL copied;

    /* the input assembler. */
    D3D_PRIMITIVE_TOPOLOGY topology;
    D3D12_VERTEX_BUFFER_VIEW vb[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT vb_dirty;
    D3D12_INDEX_BUFFER_VIEW ib;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut;
    BOOL ib_dirty, vertex_input_dirty, topology_dirty;

    /* the rasterizer and output merger. */
    UINT viewport_count, scissor_count;
    VkViewport viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    VkRect2D scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    BOOL viewports_dirty;
    FLOAT blend_factor[4];
    UINT stencil_ref[2];
    FLOAT depth_bounds[2];
    FLOAT depth_bias[3];
    BOOL depth_bias_set;
    BOOL state_dirty;
    D12TargetView rtv[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    UINT rtv_count;
    D12TargetView dsv;
    BOOL has_dsv;
    BOOL rendering;
    /* images written by attachments or clears since the last barrier ordering them. */
    VkImage written[16];
    UINT written_count;
    BOOL targets_dirty;
    /* variable rate shading: the rate, its two combiners and the rate image. */
    UINT shading_rate, rate_combiners[2];
    D12Resource *rate_image;
    BOOL rate_dirty;
    UINT render_samples;
    /* the current render pass, for its resolves at the end. */
    D3D12_RENDER_PASS_RENDER_TARGET_DESC pass_targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    UINT pass_count;

    /* stream output and predication. */
    D3D12_STREAM_OUTPUT_BUFFER_VIEW so[D3D12_SO_BUFFER_SLOT_COUNT];
    BOOL so_active;
    BOOL predicated;
} D12CommandList;

HRESULT D12CreateCommandList(D12Device *device, D3D12_COMMAND_LIST_TYPE type, D12CommandAllocator *allocator,
                             D12PipelineState *initial, D12CommandList **out);
D12CommandList *D12CommandListFrom(ID3D12CommandList *iface);

/* ------------------------------------------------------------------ the device */

/* vulkan objects released while the device's own commands that may name them record. */
typedef struct D12Garbage {
    UINT64 handle;
    VkObjectType type;
} D12Garbage;

/* a typed buffer descriptor, keyed by address, range and format. */
typedef struct D12TexelEntry {
    VkDeviceAddress address;
    VkDeviceSize range;
    VkFormat format;
    BYTE storage;
    BYTE descriptor[64];
} D12TexelEntry;

typedef struct D12SamplerEntry {
    D3D12_SAMPLER_DESC2 desc;
    VkSampler sampler;
    BYTE descriptor[64];
} D12SamplerEntry;

/* an acceleration structure object over the memory at a gpu virtual address. */
typedef struct D12AccelerationStructure {
    D3D12_GPU_VIRTUAL_ADDRESS va;
    VkAccelerationStructureKHR handle;
} D12AccelerationStructure;

struct D12Device {
    ID3D12Device14 iface;
    LONG refs;
    LUID adapter_luid;
    IDXGIAdapter *adapter;
    D3D_FEATURE_LEVEL feature_level;
    MimePrivateData data;
    HRESULT removed;

    D12Vk vk;
    UINT api_version;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT db;
    BOOL has_depth_clip, has_custom_border, has_transform_feedback, has_conditional, has_divisor, has_zero_divisor;
    BOOL has_logic_op, has_depth_bounds, has_geometry, has_tessellation, has_statistics, has_so_queries;
    BOOL has_null_descriptor;
    BOOL has_conservative, has_line_rasterization, has_patch_points, has_unused_attachments;
    /* shader model 6: 16-bit arithmetic, wave operations in every stage, mesh and amplification shaders. */
    BOOL has_native16, has_waves, has_mesh;
    /* 64-bit atomics of typed views and of shared memory. */
    BOOL has_atomic64, has_shared_atomic64;
    BOOL has_barycentrics, has_maintenance5, has_maintenance8, has_compute_derivatives, has_msaa_storage;
    UINT inf_nan_widths;
    /* variable rate shading: the Direct3D tier, rate image tile size, 4 pixel wide rates, per viewport rates. */
    UINT vrs_tier, vrs_tile;
    UINT conservative_tier;
    UINT tiled_tier;
    /* waves: the size the gpu runs, the range shaders may ask for and the stages that may ask. */
    UINT wave_size, wave_min, wave_max;
    VkShaderStageFlags wave_stages;
    BOOL vrs_more_rates, vrs_viewports;
    /* gpu generated commands, for ExecuteIndirect with state changing signatures, and the stages they reach. */
    BOOL has_dgc;
    VkShaderStageFlags dgc_stages;
    /* acceleration structures and ray queries, and ray tracing pipelines. */
    BOOL has_rt, has_rt_pipeline;
    /* the compute pipeline that clamps indirect rays to a count. */
    VkPipelineLayout rays_count_layout;
    VkPipeline rays_count_pipeline;
    /* acceleration structures by gpu virtual address. */
    SRWLOCK as_lock;
    D12AccelerationStructure *as_list;
    UINT as_count, as_capacity;
    /* buffers made for acceleration structures, sorted by address; they bound the objects over them. */
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE *as_regions;
    UINT as_region_count, as_region_capacity;
    VkPipelineCache pipeline_cache;
    UINT family;
    UINT queue_count;
    VkQueue queues[16];
    MimeVkShared shared;
    LONG next_queue;
    /* the queues; the lock keeps submissions, signals and what queues hold back in the program's order. */
    SRWLOCK submit_lock;
    CONDITION_VARIABLE released;
    struct D12Queue *queue_list;

    /* memory: the heaps committed resources come from, and gpu virtual addresses by heap. */
    SRWLOCK memory_lock;
    D12Heap *internal_heaps;
    UINT buffer_types[D12_MEMORY_KINDS];
    SRWLOCK address_lock;
    D12Heap **address_heaps;
    UINT address_count, address_capacity;
    /* what null descriptors' records read, never written. */
    D12Allocation null_buffer;
    D3D12_GPU_VIRTUAL_ADDRESS null_address;

    /* descriptors: heaps by id, the set layouts all shaders share and cached descriptor bytes. */
    SRWLOCK heap_lock;
    void **heaps;
    UINT heap_next_id;
    UINT *heap_free_ids;
    UINT heap_free_count, heap_free_capacity;
    VkDescriptorSetLayout resource_layout, sampler_layout, record_layout, empty_layout;
    UINT mutable_stride;
    UINT record_offset;
    /* 64-bit atomics of typed views: the set layout of their 64-bit views and one view's size, 0 without. */
    VkDescriptorSetLayout atomic_layout;
    UINT atomic_stride;
    UINT sampler_size;
    BYTE null_descriptors[D12_DESC_RECORD][64];
    /* the null views of each dimension, shader resource then unordered access: stand ins of it without null descriptors. */
    BYTE null_views[2][D12_NULL_DIMENSIONS][64];
    VkImage null_images[D12_NULL_IMAGES];
    D12Allocation null_image_memory[D12_NULL_IMAGES];
    VkImageView null_image_views[D12_NULL_DIMENSIONS];
    SRWLOCK texel_lock;
    D12TexelEntry *texels;
    UINT texel_count, texel_capacity;
    /* bound when the program set no heap of a kind, so every set has a buffer. */
    D12DescriptorHeap *dummy_heaps[2];
    SRWLOCK sampler_lock;
    D12SamplerEntry *samplers;
    UINT sampler_count, sampler_capacity;

    /* fences waited on with events. the waiter thread serves them and waiter_wake wakes it. */
    SRWLOCK fence_lock;
    D12Fence *waiting_fences;
    HANDLE waiter;
    VkSemaphore waiter_wake;
    volatile LONG64 waiter_wakes;
    volatile LONG waiter_stop;
    DWORD waiter_id;
    BOOL *waiter_exit;

    SRWLOCK garbage_lock;
    D12Garbage *garbage;
    UINT garbage_count, garbage_capacity;
    /* what was released while the last batch of the device's commands recorded, kept until it ran. */
    D12Garbage *retired;
    UINT retired_count;

    /* the device's own commands: initial clears of resources. */
    CRITICAL_SECTION init_lock;
    VkCommandPool init_pool;
    VkCommandBuffer init_cmd;
    /* a full barrier before every ExecuteCommandLists, since resources decay between them. */
    VkCommandBuffer boundary_cmd;
    BOOL init_recording;
    VkSemaphore init_timeline;
    UINT64 init_serial;

    /* descriptor heaps whose cpu copy submissions upload, and the queue slots they upload for. */
    SRWLOCK staged_lock;
    D12DescriptorHeap *staged;
    UINT stage_slots;
    VkSemaphore stage_timelines[D12_STAGE_SLOTS];
    /* released staged heaps whose memory waits for the queues' last uploads of it. */
    struct D12StagedGrave *graves;
};

/* stateobject.c: ray tracing pipelines and collections. */
typedef struct D12StateObject D12StateObject;
HRESULT D12CreateStateObject(D12Device *device, const D3D12_STATE_OBJECT_DESC *desc, D12StateObject *parent,
                             D12StateObject **out);
D12StateObject *D12StateObjectFrom(ID3D12StateObject *iface);
/* the pipeline of an executable state object, VK_NULL_HANDLE for a collection, and its stack size. */
VkPipeline D12StateObjectPipeline(D12StateObject *so);
UINT64 D12StateObjectStackSize(D12StateObject *so);

/* raytracing.c: acceleration structures, by the gpu virtual address direct3d gives them. */
VkAccelerationStructureKHR D12AccelerationStructureAt(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va);
void D12AccelerationRegionAdd(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va, UINT64 size);
/* drops the acceleration structures in a resource or heap's memory when it goes. */
void D12AccelerationStructuresForget(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va, UINT64 size);
void D12AccelerationStructuresDestroy(D12Device *device);
void D12PrebuildInfo(D12Device *device, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info);
UINT D12CheckDriverMatchingIdentifier(D12Device *device, UINT type, const void *identifier);
HRESULT D12RaysInit(D12Device *device);
void D12RaysDestroy(D12Device *device);
/* copies indirect ray arguments packed into dst, zeroing the width of those past the count. */
void D12LimitRays(D12Device *device, VkCommandBuffer cmd, VkDeviceAddress src, UINT stride, UINT max_count,
                  VkDeviceAddress count, VkDeviceAddress dst);
/* recorded outside rendering. */
void D12BuildAccelerationStructure(D12Device *device, VkCommandBuffer cmd,
                                   const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc);
void D12EmitPostbuildInfo(D12CommandAllocator *allocator, VkCommandBuffer cmd,
                          const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc, UINT count,
                          const D3D12_GPU_VIRTUAL_ADDRESS *sources);
void D12CopyAccelerationStructure(D12Device *device, VkCommandBuffer cmd, D3D12_GPU_VIRTUAL_ADDRESS dst,
                                  D3D12_GPU_VIRTUAL_ADDRESS src, UINT mode);

D12Device *D12DeviceFrom(ID3D12Device *iface);
HRESULT D12CreateDevice(IDXGIAdapter *adapter, D3D_FEATURE_LEVEL level, D12Device **out);
void D12Defer(D12Device *device, VkObjectType type, UINT64 handle);
/* runs the device's own commands before the next submission of any queue. */
VkCommandBuffer D12InitCommands(D12Device *device);
void D12InitDone(D12Device *device);
VkCommandBuffer D12TakeInitCommands(D12Device *device, UINT64 *serial);
void D12Barrier(D12Device *device, VkCommandBuffer cmd);
UINT D12FormatSupport(D12Device *device, DXGI_FORMAT format, UINT *support2);

extern const MimeAllocator g_d12_allocator;
void D12Log(const char *format, ...);

#endif
