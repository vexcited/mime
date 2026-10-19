/*
 * the vulkan device d3d11.dll and d3d12.dll of a process share: the host serves a process one, so both ask for the
 * same, the extensions and features either needs, and every queue of the family. dxgi.dll keeps it for the adapter;
 * the first of the two to need it describes it here.
 */
#ifndef MIME_VKPROFILE_H
#define MIME_VKPROFILE_H

#include <mime/win32/winbase.h>
#include <mime/dxgi_mime.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>

/* direct3d 12's root arguments are push constants of this many bytes. */
#define MIME_VK_D3D12_PUSH_SIZE 256
/* the queues of a device made when direct3d 12 is loaded: one for direct3d 11 and three to spread queues over. */
#define MIME_VK_D3D12_QUEUES 4

/*
 * what the device was made with and what the gpu behind it can do, derived once from the gpu's features and limits.
 * An api the gpu lacks the requirements of is not offered.
 */
typedef struct MimeVkProfile {
    BOOL d3d11, d3d12;

    BOOL has_depth_clip, has_custom_border, has_transform_feedback, has_conditional, has_line_rasterization;
    BOOL has_divisor, has_zero_divisor, has_statistics, has_precise_occlusion, has_so_queries;
    BOOL has_logic_op, has_depth_bounds, has_geometry, has_tessellation;
    BOOL has_video;

    BOOL has_conservative, has_patch_points, has_unused_attachments;
    /* shader model 6: 16-bit arithmetic, the wave operations of every stage, mesh and amplification shaders. */
    BOOL has_native16, has_waves, has_mesh;
    /* 64-bit atomics of typed views and of shared memory. */
    BOOL has_atomic64, has_shared_atomic64;
    BOOL has_barycentrics, has_maintenance5, has_maintenance8, has_compute_derivatives, has_msaa_storage;
    /* variable rate shading, which makes shader objects want a shading rate before every draw. */
    BOOL has_vrs;
    UINT vrs_tier, vrs_tile;
    UINT conservative_tier;
    UINT tiled_tier;
    /* waves: the size the gpu runs, the range shaders may ask for, the stages that may ask. */
    UINT wave_size, wave_min, wave_max;
    VkShaderStageFlags wave_stages;
    BOOL vrs_more_rates, vrs_viewports;
    /* commands the gpu generates, and the stages they reach. */
    BOOL has_dgc;
    VkShaderStageFlags dgc_stages;
    /* acceleration structures and ray queries, and ray tracing pipelines. */
    BOOL has_rt, has_rt_pipeline;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT db;

    UINT queue_family;
    UINT queue_count;
} MimeVkProfile;

/*
 * The device of the adapter the process shares, made on first use. Both fill their tables from the device's own
 * proc address function. The profile is the one it was made with.
 */
typedef struct MimeVkShared {
    IMimeDXGIAdapter *adapter;
    MIME_DXGI_SHARED_VK *vk;
    const MimeVkProfile *profile;
} MimeVkShared;

HRESULT MimeVkAcquire(IDXGIAdapter *adapter, const MIME_DXGI_VULKAN *v, MimeVkShared *out);
void MimeVkRelease(MimeVkShared *shared);

#endif
