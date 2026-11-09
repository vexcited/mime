/*
 * the vulkan device d3d11.dll and d3d12.dll share in a process. the host serves one device per process, so both ask
 * for the same thing: the extensions and features either needs, and every queue of the family. dxgi.dll keeps it
 * for the adapter, and whichever dll needs it first describes it here.
 */
#ifndef MIME_VKPROFILE_H
#define MIME_VKPROFILE_H

#include <mime/win32/winbase.h>
#include <mime/dxgi_mime.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>

/* size of the push constants holding direct3d 12 root arguments. */
#define MIME_VK_D3D12_PUSH_SIZE 256
/* queues when direct3d 12 is loaded: one for direct3d 11 and three to spread direct3d 12 queues over. */
#define MIME_VK_D3D12_QUEUES 4

/* the device's creation settings and the gpu's capabilities. an api whose requirements the gpu lacks is not offered. */
typedef struct MimeVkProfile {
    BOOL d3d11, d3d12;

    BOOL has_depth_clip, has_custom_border, has_transform_feedback, has_conditional, has_line_rasterization;
    BOOL has_divisor, has_zero_divisor, has_statistics, has_precise_occlusion, has_so_queries;
    BOOL has_logic_op, has_depth_bounds, has_geometry, has_tessellation;
    BOOL has_video;
    /* descriptors may be null; else direct3d 11 writes zeroed stand-ins into its unbound slots. */
    BOOL has_null_descriptor;

    BOOL has_conservative, has_patch_points, has_unused_attachments;
    /* shader model 6: 16-bit arithmetic, wave operations in every stage, mesh and amplification shaders. */
    BOOL has_native16, has_waves, has_mesh;
    /* 64-bit atomics on typed views and on shared memory. */
    BOOL has_atomic64, has_shared_atomic64;
    BOOL has_barycentrics, has_maintenance5, has_maintenance8, has_compute_derivatives, has_msaa_storage;
    /* float widths whose nans, infinities and signed zeros shaders can keep: 1 for 16, 2 for 32, 4 for 64 bits. */
    UINT inf_nan_widths;
    /* when on, shader objects want a shading rate before every draw. */
    BOOL has_vrs;
    UINT vrs_tier, vrs_tile;
    UINT conservative_tier;
    UINT tiled_tier;
    /* the wave size the gpu runs, the range shaders may ask for, and the stages that may ask. */
    UINT wave_size, wave_min, wave_max;
    VkShaderStageFlags wave_stages;
    BOOL vrs_more_rates, vrs_viewports;
    /* device generated commands, and the stages they reach. */
    BOOL has_dgc;
    VkShaderStageFlags dgc_stages;
    /* has_rt: acceleration structures and ray queries. */
    BOOL has_rt, has_rt_pipeline;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT db;

    UINT queue_family;
    UINT queue_count;
} MimeVkProfile;

/*
 * the adapter's shared device, made on first use. both dlls fill their tables from the device's own proc address
 * function. profile is the one it was made with.
 */
typedef struct MimeVkShared {
    IMimeDXGIAdapter *adapter;
    MIME_DXGI_SHARED_VK *vk;
    const MimeVkProfile *profile;
} MimeVkShared;

HRESULT MimeVkAcquire(IDXGIAdapter *adapter, const MIME_DXGI_VULKAN *v, MimeVkShared *out);
void MimeVkRelease(MimeVkShared *shared);

#endif
