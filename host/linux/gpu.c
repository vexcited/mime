/*
 * replays per-process OpenGL command rings through Vulkan. the guest owns API state and render passes;
 * VK_EXT_external_memory_host imports guest memory directly. images stay in VK_IMAGE_LAYOUT_GENERAL except back
 * buffers held by the presenter. depth, stencil, culling and topology are dynamic, and uniform-ring data uses
 * dynamic offsets. rows retain OpenGL's bottom-first order, with VK_EXT_depth_clip_control preserving its depth
 * range. worker threads compile SPIR-V through host/gpu/shader.c; pipelines wait for programs and vkwsi.c presents
 * back buffers.
 */
#include "../gpu/gpu.h"
#include "../gpu/shader.h"
#include "../vulkan/mvk_host.h"
#include "spirv_patch.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* the Metal values the guest sends. */
enum {
    PIXEL_BGRA8 = 80,
    PIXEL_D32 = 252,
    PIXEL_S8 = 253,
    PIXEL_D32S8 = 260,
    TEXTURE_1D = 0,
    TEXTURE_1D_ARRAY = 1,
    TEXTURE_2D = 2,
    TEXTURE_2D_ARRAY = 3,
    TEXTURE_2D_MS = 4,
    TEXTURE_CUBE = 5,
    TEXTURE_CUBE_ARRAY = 6,
    TEXTURE_3D = 7,
    LOAD_DONT_CARE = 0,
    LOAD_LOAD = 1,
    LOAD_CLEAR = 2,
};

#define MAX_CHANNELS 256
#define ATTACHMENTS (MIME_GPU_MAX_COLORS + 2)
#define RING_BYTES (8u << 20)
#define MAX_BATCHES 8
#define BLOCK_BYTES (64ull << 20)
#define SETS_PER_POOL 256
#define MAX_COMPILERS 4

/* ---------------------------------------------------------------- device */

static MvkConnection g_vk;
static bool g_ok;
static VkQueue g_queue;
static uint32_t g_family;
static VkPhysicalDeviceMemoryProperties g_memory;
static VkPhysicalDeviceProperties g_props;
static bool g_polygon_mode, g_depth_clamp, g_clip_control, g_null_descriptor, g_logic_op, g_storage;
static bool g_geometry, g_tessellation, g_xfb, g_queries, g_generated_query, g_fp64, g_dual_blend, g_cube_array;
static uint32_t g_viewports = 1;
static PFN_vkGetMemoryHostPointerPropertiesEXT g_host_pointer;
static VkPipelineCache g_cache;
static VkShaderModule g_clear_vs, g_clear_fs;

static int32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < g_memory.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g_memory.memoryTypes[i].propertyFlags & want) == want)
            return (int32_t)i;
    return -1;
}

static bool has_extension(const VkExtensionProperties *list, uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(list[i].extensionName, name))
            return true;
    return false;
}

/* the clear shaders: a triangle over the target at a depth, writing one color to every output. */
static VkShaderModule internal_module(glslang_stage_t stage, const char *source)
{
    glslang_input_t in = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_0,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_0,
        .code = source,
        .default_version = 450,
        .default_profile = GLSLANG_NO_PROFILE,
        .messages = GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT,
        .resource = glslang_default_resource(),
    };
    glslang_shader_t *sh = glslang_shader_create(&in);
    glslang_program_t *prog = glslang_program_create();
    VkShaderModule module = VK_NULL_HANDLE;
    if (glslang_shader_preprocess(sh, &in) && glslang_shader_parse(sh, &in)) {
        glslang_program_add_shader(prog, sh);
        if (glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
            glslang_program_SPIRV_generate(prog, stage);
            VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
                                             glslang_program_SPIRV_get_size(prog) * 4,
                                             glslang_program_SPIRV_get_ptr(prog)};
            g_vk.fn.vkCreateShaderModule(g_vk.device, &info, nullptr, &module);
        }
    } else {
        mime_warn("gpu: internal shader: %s", glslang_shader_get_info_log(sh));
    }
    glslang_program_delete(prog);
    glslang_shader_delete(sh);
    return module;
}

static void init_device(void)
{
    glslang_initialize_process();
    if (!mvk_internal_instance(&g_vk, VK_API_VERSION_1_3))
        return;
    uint32_t count = 8;
    VkPhysicalDevice devices[8];
    if (g_vk.fn.vkEnumeratePhysicalDevices(g_vk.instance, &count, devices) < 0 || !count)
        return;
    /* the first discrete GPU, else the first one with Vulkan 1.3. */
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    for (int pass = 0; pass < 2 && !physical; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            VkPhysicalDeviceProperties p;
            g_vk.fn.vkGetPhysicalDeviceProperties(devices[i], &p);
            if (p.apiVersion >= VK_API_VERSION_1_3 && (pass || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) {
                physical = devices[i];
                break;
            }
        }
    }
    if (!physical) {
        mime_warn("gpu: opengl needs a vulkan 1.3 gpu on the host");
        return;
    }
    g_vk.fn.vkGetPhysicalDeviceProperties(physical, &g_props);
    g_vk.fn.vkGetPhysicalDeviceMemoryProperties(physical, &g_memory);

    VkQueueFamilyProperties families[32];
    uint32_t nf = 32;
    g_vk.fn.vkGetPhysicalDeviceQueueFamilyProperties(physical, &nf, families);
    g_family = UINT32_MAX;
    for (uint32_t i = 0; i < nf && g_family == UINT32_MAX; i++)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            g_family = i;
    if (g_family == UINT32_MAX)
        return;

    uint32_t ne = 0;
    g_vk.fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &ne, nullptr);
    VkExtensionProperties *list = calloc(ne ? ne : 1, sizeof(*list));
    g_vk.fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &ne, list);
    if (!has_extension(list, ne, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)) {
        mime_warn("gpu: opengl needs VK_EXT_external_memory_host on the host");
        free(list);
        return;
    }
    const char *names[14];
    uint32_t n = 0;
    names[n++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    if (has_extension(list, ne, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        names[n++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    g_clip_control = has_extension(list, ne, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
    if (g_clip_control)
        names[n++] = VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME;
    bool eds3 = has_extension(list, ne, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
    if (eds3)
        names[n++] = VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME;
    bool robustness = has_extension(list, ne, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    if (robustness)
        names[n++] = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
    bool xfb_ext = has_extension(list, ne, VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
    if (xfb_ext)
        names[n++] = VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
    bool pgq_ext = xfb_ext && has_extension(list, ne, VK_EXT_PRIMITIVES_GENERATED_QUERY_EXTENSION_NAME);
    if (pgq_ext)
        names[n++] = VK_EXT_PRIMITIVES_GENERATED_QUERY_EXTENSION_NAME;
    free(list);
    if (!g_clip_control)
        mime_warn("gpu: no VK_EXT_depth_clip_control: depth keeps vulkan's range");

    /* queries capabilities used by OpenGL. */
    VkPhysicalDeviceRobustness2FeaturesEXT robust = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dyn3 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, robustness ? &robust : nullptr};
    VkPhysicalDeviceDepthClipControlFeaturesEXT clip = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
                                                        eds3 ? (void *)&dyn3 : robustness ? (void *)&robust : nullptr};
    VkPhysicalDeviceVulkan13Features v13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                            g_clip_control ? (void *)&clip : eds3 ? (void *)&dyn3
                                                             : robustness ? (void *)&robust : nullptr};
    VkPhysicalDeviceVulkan12Features v12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &v13};
    VkPhysicalDeviceFeatures2 all = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v12};
    VkPhysicalDeviceTransformFeedbackFeaturesEXT xfbf = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
    VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT pgqf = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT};
    if (xfb_ext) {
        xfbf.pNext = all.pNext;
        all.pNext = &xfbf;
    }
    if (pgq_ext) {
        pgqf.pNext = all.pNext;
        all.pNext = &pgqf;
    }
    g_vk.fn.vkGetPhysicalDeviceFeatures2(physical, &all);
    if (!v13.dynamicRendering || !v13.synchronization2 || !v12.timelineSemaphore) {
        mime_warn("gpu: opengl needs dynamic rendering and timeline semaphores");
        return;
    }
    VkPhysicalDeviceFeatures want = {
        .fillModeNonSolid = all.features.fillModeNonSolid,
        .depthClamp = all.features.depthClamp,
        .depthBiasClamp = all.features.depthBiasClamp,
        .samplerAnisotropy = all.features.samplerAnisotropy,
        .textureCompressionBC = all.features.textureCompressionBC,
        .independentBlend = all.features.independentBlend,
        .dualSrcBlend = all.features.dualSrcBlend,
        .imageCubeArray = all.features.imageCubeArray,
        .largePoints = all.features.largePoints,
        .wideLines = all.features.wideLines,
        .logicOp = all.features.logicOp,
        .shaderClipDistance = all.features.shaderClipDistance,
        .shaderCullDistance = all.features.shaderCullDistance,
        .sampleRateShading = all.features.sampleRateShading,
        .fragmentStoresAndAtomics = all.features.fragmentStoresAndAtomics,
        .vertexPipelineStoresAndAtomics = all.features.vertexPipelineStoresAndAtomics,
        .shaderImageGatherExtended = all.features.shaderImageGatherExtended,
        .shaderStorageImageExtendedFormats = all.features.shaderStorageImageExtendedFormats,
        .shaderStorageImageReadWithoutFormat = all.features.shaderStorageImageReadWithoutFormat,
        .shaderStorageImageWriteWithoutFormat = all.features.shaderStorageImageWriteWithoutFormat,
        .geometryShader = all.features.geometryShader,
        .tessellationShader = all.features.tessellationShader,
        .shaderTessellationAndGeometryPointSize = all.features.shaderTessellationAndGeometryPointSize,
        .shaderFloat64 = all.features.shaderFloat64,
        .shaderInt64 = all.features.shaderInt64,
        .multiViewport = all.features.multiViewport,
        .occlusionQueryPrecise = all.features.occlusionQueryPrecise,
    };
    g_logic_op = all.features.logicOp;
    g_geometry = all.features.geometryShader;
    g_fp64 = all.features.shaderFloat64;
    g_dual_blend = all.features.dualSrcBlend;
    g_cube_array = all.features.imageCubeArray;
    g_tessellation = all.features.tessellationShader;
    g_viewports = all.features.multiViewport ? (g_props.limits.maxViewports < MIME_GPU_MAX_VIEWPORTS
                                                    ? g_props.limits.maxViewports
                                                    : MIME_GPU_MAX_VIEWPORTS)
                                             : 1;
    g_xfb = xfb_ext && xfbf.transformFeedback;
    g_queries = v12.hostQueryReset && all.features.occlusionQueryPrecise;
    g_storage = all.features.fragmentStoresAndAtomics && all.features.vertexPipelineStoresAndAtomics;
    g_polygon_mode = eds3 && dyn3.extendedDynamicState3PolygonMode;
    g_depth_clamp = eds3 && dyn3.extendedDynamicState3DepthClampEnable;
    g_null_descriptor = robustness && robust.nullDescriptor;
    VkPhysicalDeviceRobustness2FeaturesEXT robust_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
                                                        nullptr, VK_FALSE, VK_FALSE, g_null_descriptor};
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dyn3_on = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, robustness ? &robust_on : nullptr};
    dyn3_on.extendedDynamicState3PolygonMode = g_polygon_mode;
    dyn3_on.extendedDynamicState3DepthClampEnable = g_depth_clamp;
    VkPhysicalDeviceDepthClipControlFeaturesEXT clip_on = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
        eds3 ? (void *)&dyn3_on : robustness ? (void *)&robust_on : nullptr, g_clip_control && clip.depthClipControl};
    g_clip_control = g_clip_control && clip.depthClipControl;
    VkPhysicalDeviceVulkan13Features v13_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &clip_on};
    v13_on.dynamicRendering = VK_TRUE;
    v13_on.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features v12_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &v13_on};
    v12_on.timelineSemaphore = VK_TRUE;
    v12_on.samplerMirrorClampToEdge = v12.samplerMirrorClampToEdge;
    v12_on.scalarBlockLayout = v12.scalarBlockLayout;
    v12_on.uniformBufferStandardLayout = v12.uniformBufferStandardLayout;
    v12_on.hostQueryReset = v12.hostQueryReset;
    v12_on.shaderOutputViewportIndex = v12.shaderOutputViewportIndex;
    v12_on.shaderOutputLayer = v12.shaderOutputLayer;
    g_generated_query = pgq_ext && pgqf.primitivesGeneratedQuery;
    VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT pgq_on = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT, &v12_on, g_generated_query,
        g_generated_query && pgqf.primitivesGeneratedQueryWithRasterizerDiscard};
    VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT,
                                                           &pgq_on, g_xfb};
    VkPhysicalDeviceFeatures2 on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, g_xfb ? (void *)&xfb_on : &v12_on, want};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, g_family, 1, &priority};
    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &on, 0, 1, &queue, 0, nullptr, n, names, nullptr};
    VkDevice device;
    VkResult r = g_vk.fn.vkCreateDevice(physical, &info, nullptr, &device);
    if (r != VK_SUCCESS) {
        mime_warn("gpu: cannot make the opengl device (%d)", (int)r);
        return;
    }
    mvk_internal_device(&g_vk, physical, device, g_family);
    g_queue = g_vk.queues[0].queue;
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)g_vk.gipa(g_vk.instance, "vkGetDeviceProcAddr");
    g_host_pointer = (PFN_vkGetMemoryHostPointerPropertiesEXT)gdpa(device, "vkGetMemoryHostPointerPropertiesEXT");
    VkPipelineCacheCreateInfo cache = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    g_vk.fn.vkCreatePipelineCache(device, &cache, nullptr, &g_cache);
    g_clear_vs = internal_module(GLSLANG_STAGE_VERTEX,
                                 "#version 450\n"
                                 "layout(push_constant) uniform P { vec4 color; float depth; } p;\n"
                                 "void main() {\n"
                                 "  vec2 v = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
                                 "  gl_Position = vec4(v * 2.0 - 1.0, p.depth, 1.0);\n"
                                 "}\n");
    g_clear_fs = internal_module(GLSLANG_STAGE_FRAGMENT,
                                 "#version 450\n"
                                 "layout(push_constant) uniform P { vec4 color; float depth; } p;\n"
                                 "layout(location = 0) out vec4 o0; layout(location = 1) out vec4 o1;\n"
                                 "layout(location = 2) out vec4 o2; layout(location = 3) out vec4 o3;\n"
                                 "layout(location = 4) out vec4 o4; layout(location = 5) out vec4 o5;\n"
                                 "layout(location = 6) out vec4 o6; layout(location = 7) out vec4 o7;\n"
                                 "void main() { o0 = o1 = o2 = o3 = o4 = o5 = o6 = o7 = p.color; }\n");
    g_ok = g_clear_vs && g_clear_fs;
}

/* ---------------------------------------------------------------- formats */

static VkFormat pixel_format(uint32_t f)
{
    switch (f) {
    case 1: return VK_FORMAT_R8_UNORM; /* alpha 8, swizzled. */
    case 10: return VK_FORMAT_R8_UNORM;
    case 11: return VK_FORMAT_R8_SRGB;
    case 12: return VK_FORMAT_R8_SNORM;
    case 13: return VK_FORMAT_R8_UINT;
    case 14: return VK_FORMAT_R8_SINT;
    case 20: return VK_FORMAT_R16_UNORM;
    case 22: return VK_FORMAT_R16_SNORM;
    case 23: return VK_FORMAT_R16_UINT;
    case 24: return VK_FORMAT_R16_SINT;
    case 25: return VK_FORMAT_R16_SFLOAT;
    case 30: return VK_FORMAT_R8G8_UNORM;
    case 31: return VK_FORMAT_R8G8_SRGB;
    case 32: return VK_FORMAT_R8G8_SNORM;
    case 33: return VK_FORMAT_R8G8_UINT;
    case 34: return VK_FORMAT_R8G8_SINT;
    case 40: return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case 41: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case 42: return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
    case 53: return VK_FORMAT_R32_UINT;
    case 54: return VK_FORMAT_R32_SINT;
    case 55: return VK_FORMAT_R32_SFLOAT;
    case 60: return VK_FORMAT_R16G16_UNORM;
    case 62: return VK_FORMAT_R16G16_SNORM;
    case 63: return VK_FORMAT_R16G16_UINT;
    case 64: return VK_FORMAT_R16G16_SINT;
    case 65: return VK_FORMAT_R16G16_SFLOAT;
    case 70: return VK_FORMAT_R8G8B8A8_UNORM;
    case 71: return VK_FORMAT_R8G8B8A8_SRGB;
    case 72: return VK_FORMAT_R8G8B8A8_SNORM;
    case 73: return VK_FORMAT_R8G8B8A8_UINT;
    case 74: return VK_FORMAT_R8G8B8A8_SINT;
    case 80: return VK_FORMAT_B8G8R8A8_UNORM;
    case 81: return VK_FORMAT_B8G8R8A8_SRGB;
    case 90: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case 91: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case 92: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case 93: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case 103: return VK_FORMAT_R32G32_UINT;
    case 104: return VK_FORMAT_R32G32_SINT;
    case 105: return VK_FORMAT_R32G32_SFLOAT;
    case 110: return VK_FORMAT_R16G16B16A16_UNORM;
    case 112: return VK_FORMAT_R16G16B16A16_SNORM;
    case 113: return VK_FORMAT_R16G16B16A16_UINT;
    case 114: return VK_FORMAT_R16G16B16A16_SINT;
    case 115: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 123: return VK_FORMAT_R32G32B32A32_UINT;
    case 124: return VK_FORMAT_R32G32B32A32_SINT;
    case 125: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case 130: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case 131: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case 132: return VK_FORMAT_BC2_UNORM_BLOCK;
    case 133: return VK_FORMAT_BC2_SRGB_BLOCK;
    case 134: return VK_FORMAT_BC3_UNORM_BLOCK;
    case 135: return VK_FORMAT_BC3_SRGB_BLOCK;
    case 140: return VK_FORMAT_BC4_UNORM_BLOCK;
    case 141: return VK_FORMAT_BC4_SNORM_BLOCK;
    case 142: return VK_FORMAT_BC5_UNORM_BLOCK;
    case 143: return VK_FORMAT_BC5_SNORM_BLOCK;
    case 150: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case 151: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case 152: return VK_FORMAT_BC7_UNORM_BLOCK;
    case 153: return VK_FORMAT_BC7_SRGB_BLOCK;
    case 250: return VK_FORMAT_D16_UNORM;
    case 252: return VK_FORMAT_D32_SFLOAT;
    case 253: return VK_FORMAT_S8_UINT;
    case 255: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case 260: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static VkImageAspectFlags format_aspects(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_S8_UINT:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static bool is_compressed(VkFormat f)
{
    return f >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && f <= VK_FORMAT_BC7_SRGB_BLOCK;
}

/* copy unit size: one texel or one compressed 4 by 4 block. */
static uint32_t texel_bytes(VkFormat f, VkImageAspectFlags aspect)
{
    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
        return 1;
    if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
        return f == VK_FORMAT_D16_UNORM ? 2 : 4;
    switch (f) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SRGB: case VK_FORMAT_R8_SNORM: case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
        return 1;
    case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SNORM: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8_SRGB: case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16: case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
        return 2;
    case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R16G16B16A16_UNORM: case VK_FORMAT_R16G16B16A16_SNORM: case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return 8;
    case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_BC2_UNORM_BLOCK: case VK_FORMAT_BC2_SRGB_BLOCK: case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK: case VK_FORMAT_BC5_UNORM_BLOCK: case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK: case VK_FORMAT_BC6H_UFLOAT_BLOCK: case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return 16;
    default:
        return 4;
    }
}

static VkFormat vertex_format(uint32_t f)
{
    static const VkFormat table[] = {
        [1] = VK_FORMAT_R8G8_UINT, [2] = VK_FORMAT_R8G8B8_UINT, [3] = VK_FORMAT_R8G8B8A8_UINT,
        [4] = VK_FORMAT_R8G8_SINT, [5] = VK_FORMAT_R8G8B8_SINT, [6] = VK_FORMAT_R8G8B8A8_SINT,
        [7] = VK_FORMAT_R8G8_UNORM, [8] = VK_FORMAT_R8G8B8_UNORM, [9] = VK_FORMAT_R8G8B8A8_UNORM,
        [10] = VK_FORMAT_R8G8_SNORM, [11] = VK_FORMAT_R8G8B8_SNORM, [12] = VK_FORMAT_R8G8B8A8_SNORM,
        [13] = VK_FORMAT_R16G16_UINT, [14] = VK_FORMAT_R16G16B16_UINT, [15] = VK_FORMAT_R16G16B16A16_UINT,
        [16] = VK_FORMAT_R16G16_SINT, [17] = VK_FORMAT_R16G16B16_SINT, [18] = VK_FORMAT_R16G16B16A16_SINT,
        [19] = VK_FORMAT_R16G16_UNORM, [20] = VK_FORMAT_R16G16B16_UNORM, [21] = VK_FORMAT_R16G16B16A16_UNORM,
        [22] = VK_FORMAT_R16G16_SNORM, [23] = VK_FORMAT_R16G16B16_SNORM, [24] = VK_FORMAT_R16G16B16A16_SNORM,
        [25] = VK_FORMAT_R16G16_SFLOAT, [26] = VK_FORMAT_R16G16B16_SFLOAT, [27] = VK_FORMAT_R16G16B16A16_SFLOAT,
        [28] = VK_FORMAT_R32_SFLOAT, [29] = VK_FORMAT_R32G32_SFLOAT, [30] = VK_FORMAT_R32G32B32_SFLOAT,
        [31] = VK_FORMAT_R32G32B32A32_SFLOAT, [32] = VK_FORMAT_R32_SINT, [33] = VK_FORMAT_R32G32_SINT,
        [34] = VK_FORMAT_R32G32B32_SINT, [35] = VK_FORMAT_R32G32B32A32_SINT, [36] = VK_FORMAT_R32_UINT,
        [37] = VK_FORMAT_R32G32_UINT, [38] = VK_FORMAT_R32G32B32_UINT, [39] = VK_FORMAT_R32G32B32A32_UINT,
        [40] = VK_FORMAT_A2B10G10R10_SNORM_PACK32, [41] = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        [42] = VK_FORMAT_B8G8R8A8_UNORM, [45] = VK_FORMAT_R8_UINT, [46] = VK_FORMAT_R8_SINT,
        [47] = VK_FORMAT_R8_UNORM, [48] = VK_FORMAT_R8_SNORM, [49] = VK_FORMAT_R16_UINT, [50] = VK_FORMAT_R16_SINT,
        [51] = VK_FORMAT_R16_UNORM, [52] = VK_FORMAT_R16_SNORM, [53] = VK_FORMAT_R16_SFLOAT,
        /* 64 bit components, for the double attributes of glVertexAttribLPointer. */
        [120] = VK_FORMAT_R64_SFLOAT, [121] = VK_FORMAT_R64G64_SFLOAT, [122] = VK_FORMAT_R64G64B64_SFLOAT,
        [123] = VK_FORMAT_R64G64B64A64_SFLOAT,
    };
    VkFormat v = f < sizeof(table) / sizeof(table[0]) ? table[f] : VK_FORMAT_UNDEFINED;
    VkFormatProperties props;
    g_vk.fn.vkGetPhysicalDeviceFormatProperties(g_vk.physical_device, v, &props);
    if (v && !(props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
        /* three components the GPU cannot fetch read as four, the fourth from the next element. */
        static const VkFormat wider[][2] = {
            {VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT}, {VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT},
            {VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}, {VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM},
            {VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT},
            {VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT},
            {VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM},
            {VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM},
            {VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
        };
        for (size_t i = 0; i < sizeof(wider) / sizeof(wider[0]); i++)
            if (wider[i][0] == v)
                return wider[i][1];
    }
    return v;
}

static VkBlendFactor blend_factor(uint8_t f)
{
    static const VkBlendFactor table[] = {
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
        VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA, VK_BLEND_FACTOR_SRC1_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, VK_BLEND_FACTOR_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
    };
    return f < sizeof(table) / sizeof(table[0]) ? table[f] : VK_BLEND_FACTOR_ONE;
}

/* Metal's write mask has red in its high bit, Vulkan's in its low one. */
static VkColorComponentFlags write_mask(uint8_t m)
{
    return ((m & 8) ? VK_COLOR_COMPONENT_R_BIT : 0) | ((m & 4) ? VK_COLOR_COMPONENT_G_BIT : 0) |
           ((m & 2) ? VK_COLOR_COMPONENT_B_BIT : 0) | ((m & 1) ? VK_COLOR_COMPONENT_A_BIT : 0);
}

static VkSamplerAddressMode address_mode(uint8_t m)
{
    switch (m) {
    case 1: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    case 2: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case 3: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 4:
    case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

/* Metal's swizzle names zero, one, then red to alpha from 0; Vulkan's from 1 after identity. */
static VkComponentSwizzle component(uint8_t s)
{
    return s <= 5 ? (VkComponentSwizzle)(s + 1) : VK_COMPONENT_SWIZZLE_IDENTITY;
}

/* ---------------------------------------------------------------- memory */

/* device memory in large blocks, handed out first fit. */
typedef struct Range {
    VkDeviceSize offset, size;
} Range;

typedef struct Block {
    struct Block *next;
    VkDeviceMemory memory;
    VkDeviceSize size;
    uint32_t type;
    Range *free;
    uint32_t free_count, free_cap;
} Block;

typedef struct Alloc {
    Block *block; /* null for memory of its own. */
    VkDeviceMemory memory;
    VkDeviceSize offset, size;
} Alloc;

static pthread_mutex_t g_blocks_lock = PTHREAD_MUTEX_INITIALIZER;
static Block *g_blocks;

static void range_insert(Block *b, uint32_t at, Range r)
{
    if (b->free_count == b->free_cap) {
        b->free_cap = b->free_cap ? b->free_cap * 2 : 16;
        b->free = realloc(b->free, b->free_cap * sizeof(Range));
    }
    memmove(&b->free[at + 1], &b->free[at], (b->free_count - at) * sizeof(Range));
    b->free[at] = r;
    b->free_count++;
}

static bool block_take(Block *b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *offset)
{
    for (uint32_t i = 0; i < b->free_count; i++) {
        Range *r = &b->free[i];
        VkDeviceSize start = (r->offset + align - 1) & ~(align - 1);
        if (start + size > r->offset + r->size)
            continue;
        VkDeviceSize end = r->offset + r->size;
        Range before = {r->offset, start - r->offset}, after = {start + size, end - start - size};
        memmove(&b->free[i], &b->free[i + 1], (b->free_count - i - 1) * sizeof(Range));
        b->free_count--;
        if (after.size)
            range_insert(b, i, after);
        if (before.size)
            range_insert(b, i, before);
        *offset = start;
        return true;
    }
    return false;
}

static void block_give(Block *b, VkDeviceSize offset, VkDeviceSize size)
{
    uint32_t i = 0;
    while (i < b->free_count && b->free[i].offset < offset)
        i++;
    range_insert(b, i, (Range){offset, size});
    if (i + 1 < b->free_count && b->free[i].offset + b->free[i].size == b->free[i + 1].offset) {
        b->free[i].size += b->free[i + 1].size;
        memmove(&b->free[i + 1], &b->free[i + 2], (b->free_count - i - 2) * sizeof(Range));
        b->free_count--;
    }
    if (i > 0 && b->free[i - 1].offset + b->free[i - 1].size == b->free[i].offset) {
        b->free[i - 1].size += b->free[i].size;
        memmove(&b->free[i], &b->free[i + 1], (b->free_count - i - 1) * sizeof(Range));
        b->free_count--;
    }
}

static bool allocate(const VkMemoryRequirements *req, VkMemoryPropertyFlags want, Alloc *out)
{
    int32_t type = memory_type(req->memoryTypeBits, want);
    if (type < 0)
        type = memory_type(req->memoryTypeBits, 0);
    if (type < 0)
        return false;
    memset(out, 0, sizeof(*out));
    if (req->size > BLOCK_BYTES / 4) {
        VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req->size, (uint32_t)type};
        if (g_vk.fn.vkAllocateMemory(g_vk.device, &info, nullptr, &out->memory) != VK_SUCCESS)
            return false;
        out->size = req->size;
        return true;
    }
    pthread_mutex_lock(&g_blocks_lock);
    VkDeviceSize offset = 0;
    Block *b;
    for (b = g_blocks; b; b = b->next)
        if (b->type == (uint32_t)type && block_take(b, req->size, req->alignment, &offset))
            break;
    if (!b) {
        b = calloc(1, sizeof(*b));
        VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, BLOCK_BYTES, (uint32_t)type};
        if (!b || g_vk.fn.vkAllocateMemory(g_vk.device, &info, nullptr, &b->memory) != VK_SUCCESS) {
            free(b);
            pthread_mutex_unlock(&g_blocks_lock);
            return false;
        }
        b->size = BLOCK_BYTES;
        b->type = (uint32_t)type;
        range_insert(b, 0, (Range){0, BLOCK_BYTES});
        b->next = g_blocks;
        g_blocks = b;
        block_take(b, req->size, req->alignment, &offset);
    }
    pthread_mutex_unlock(&g_blocks_lock);
    out->block = b;
    out->memory = b->memory;
    out->offset = offset;
    out->size = req->size;
    return true;
}

static void release_alloc(Alloc *a)
{
    if (a->block) {
        pthread_mutex_lock(&g_blocks_lock);
        block_give(a->block, a->offset, a->size);
        pthread_mutex_unlock(&g_blocks_lock);
    } else if (a->memory) {
        g_vk.fn.vkFreeMemory(g_vk.device, a->memory, nullptr);
    }
    memset(a, 0, sizeof(*a));
}

/* ---------------------------------------------------------------- objects */

typedef struct View {
    uint32_t key; /* level, slice and aspect. */
    VkImageView view;
} View;

typedef struct Texture {
    VkImage image;
    Alloc mem;
    VkImageView sampled;
    VkFormat format;
    VkImageAspectFlags aspect;
    VkImageViewType view_type;
    uint32_t mtl_format, type;
    uint32_t width, height, depth, levels, layers, samples;
    uint32_t swizzle;
    View *views;
    uint32_t view_count;
} Texture;

typedef struct Program {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done, compiled;
    VkShaderModule module[MIME_GPU_STAGES];
    VkShaderModule extra[MIME_SHADER_EXTRAS]; /* tessellation control, tessellation evaluation, geometry. */
    bool has_tess, has_geometry;
    uint32_t geometry_class; /* topology class of the geometry shader's input, as MimeGpuPipelineDesc has it. */
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    MimeShaderBinding *bindings;
    uint32_t binding_count;
    bool has_uniforms;
    bool compute;
    VkPipeline compute_pipeline;
} Program;

typedef struct Pipeline {
    bool valid;
    MimeGpuPipelineDesc desc;
    Program *program;
    VkPipeline variants[8]; /* fill mode times depth clamp, when they are not dynamic, times the depth range of clip space. */
} Pipeline;

typedef struct Surface {
    Texture color, depth;
    MvkImageChain *chain;
    VkSemaphore rendered;
    bool presented;  /* in the presenter's hands: the next use waits for it. */
    uint32_t width, height, samples;
    uint16_t depth_format, stencil_format;
    MvkTarget target;
    bool sync, live;
} Surface;

typedef struct Binding {
    uint32_t id;
    uint32_t sampler;
    bool surface;
} Binding;

/* deferred destruction after the last batch using each object completes. */
typedef struct Garbage {
    uint64_t value;
    VkImage image;
    VkImageView view;
    VkPipeline pipeline;
    Alloc mem;
} Garbage;

typedef struct Batch {
    VkCommandBuffer cmd;
    uint64_t value;      /* of the channel's timeline, 0 while recording or free. */
    uint64_t fence;      /* the guest's fence it signals, 0 for none. */
    uint32_t ring_start; /* where the uniform ring was when it began. */
    VkDescriptorPool pools[16];
    uint32_t pool_count;
} Batch;

typedef struct Channel {
    uint32_t id;
    MimeVm *vm;
    MimeGpuHeader *hdr;
    uint8_t *ring;
    VkBuffer blocks[MIME_GPU_MAX_BLOCKS];
    VkDeviceMemory block_memory[MIME_GPU_MAX_BLOCKS];
    uint8_t *block_base[MIME_GPU_MAX_BLOCKS];
    uint64_t block_size[MIME_GPU_MAX_BLOCKS];

    pthread_t thread, retirer;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool kicked, stop;

    VkCommandPool pool;
    VkSemaphore timeline;
    uint64_t submitted;            /* the last value of the timeline submitted. */
    volatile uint64_t completed;   /* the last one the GPU passed. */
    pthread_mutex_t retire_lock;
    pthread_cond_t retire_cond;
    bool retire_stop;
    Batch batches[MAX_BATCHES];
    Batch *batch;                  /* recording, or null. */
    VkSemaphore waits[8];
    uint32_t wait_count;
    VkSemaphore signal;            /* a binary semaphore the batch signals, for the presenter. */
    struct {
        uint64_t value, fence;
    } fences[256];                 /* guest fences by batch value, for the retirer. */
    uint32_t fence_head, fence_tail;
    Garbage *garbage;
    uint32_t garbage_count, garbage_cap;
    VkDescriptorPool *spare_pools;
    uint32_t spare_count, spare_cap;

    /* the uniform ring, host visible. */
    VkBuffer uniforms;
    Alloc uniforms_mem;
    uint8_t *uniforms_map;
    uint32_t ring_head;

    Texture *textures;
    uint32_t textures_cap;
    Program **programs;
    uint32_t programs_cap;
    Pipeline *pipelines;
    uint32_t pipelines_cap;
    MimeGpuDepthStencilCreate *depth_states;
    uint32_t depth_states_cap;
    VkSampler *samplers;
    uint32_t samplers_cap;
    Surface *surfaces;
    uint32_t surfaces_cap;

    /* the guest's pass and whether rendering to it is open. */
    MimeGpuPass pass;
    bool pass_live, rendering, wrote;
    VkFormat pass_color[MIME_GPU_MAX_COLORS], pass_depth, pass_stencil;
    uint32_t pass_samples;

    struct {
        Pipeline *pipeline;
        VkPipeline bound;
        Program *program;
        MimeGpuDepthStencilCreate depth;
        bool has_depth;
        uint32_t front_ref, back_ref;
        MimeGpuSetRaster raster;
        VkViewport viewport;
        MimeGpuRect scissor;
        bool has_scissor;
        /* viewports and scissors that differ from the one the plain commands set for all. */
        VkViewport viewport_own[MIME_GPU_MAX_VIEWPORTS];
        MimeGpuRect scissor_own[MIME_GPU_MAX_VIEWPORTS];
        uint32_t viewport_own_mask, scissor_own_mask;
        float blend[4];
        MimeGpuMem vb[MIME_GPU_MAX_ATTRIBS];
        uint32_t vb_mask;
        uint8_t uniforms[4096];
        uint32_t uniform_bytes;
        uint32_t uniform_offset; /* in the ring, for the dynamic offset. */
        bool uniforms_placed;
        MimeGpuMem uniform_buffers[MIME_GPU_STAGES][MIME_GPU_MAX_UNIFORM_BLOCKS];
        uint32_t uniform_buffer_mask[MIME_GPU_STAGES];
        Binding tex[MIME_GPU_STAGES][MIME_GPU_MAX_TEXTURES];
        uint32_t tex_mask[MIME_GPU_STAGES];
        struct {
            MimeGpuMem mem;
            uint64_t size;
        } storage[MIME_GPU_MAX_STORAGE_BLOCKS];
        MimeGpuSetImage images[MIME_GPU_MAX_IMAGES];
        VkDescriptorSet set;
        uint32_t dirty;
    } st;

    VkPipeline clear_pipelines[16];
    uint64_t clear_keys[16];
    VkPipelineLayout clear_layout;

    /* queries and transform feedback, see the scopes below. */
    struct HostQuery *queries;
    uint32_t queries_cap;
    uint32_t active_queries[16];
    uint32_t active_query_count;
    VkQueryPool query_pool[3];
    uint32_t *query_free[3];
    uint32_t query_free_count[3];
    struct QueryDone *query_done;
    uint32_t query_done_count, query_done_cap;
    pthread_mutex_t query_lock;
    struct {
        bool requested, inside, has_counter;
        uint32_t object, count;
        struct {
            MimeGpuMem mem;
            uint64_t size;
        } buffer[MIME_GPU_MAX_XFB_BUFFERS];
        VkBuffer counters;
        VkDeviceMemory counters_memory;
    } xfb;

    /* the other stages sent for the program the next PROGRAM_CREATE makes. */
    struct {
        bool valid;
        uint32_t id;
        char *extra[MIME_SHADER_EXTRAS];
        char *feedback, *fragment;
        size_t feedback_len, fragment_len;
        bool separate;
    } stages;
} Channel;

enum {
    DIRTY_PIPELINE = 1u << 0,
    DIRTY_DEPTH = 1u << 1,
    DIRTY_RASTER = 1u << 2,
    DIRTY_VIEWPORT = 1u << 3,
    DIRTY_SCISSOR = 1u << 4,
    DIRTY_BLEND = 1u << 5,
    DIRTY_VB = 1u << 6,
    DIRTY_UNIFORMS = 1u << 7,
    DIRTY_TEXTURES = 1u << 8,
    DIRTY_SET = 1u << 9,
    DIRTY_ALL = 0x3FF,
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Channel *g_channels[MAX_CHANNELS];

/* dummy textures for samplers with nothing bound, by view type, and their sampler. */
static Texture g_dummy[8];
static VkSampler g_default_sampler;

static Channel *find_channel(uint64_t id_)
{
    pthread_mutex_lock(&g_lock);
    Channel *c = id_ < MAX_CHANNELS ? g_channels[id_] : nullptr;
    pthread_mutex_unlock(&g_lock);
    return c;
}

static void *slot(void **table, uint32_t *cap, uint32_t id_, size_t size)
{
    if (id_ >= *cap) {
        uint32_t n = *cap ? *cap : 64;
        while (n <= id_)
            n *= 2;
        *table = realloc(*table, (size_t)n * size);
        memset((uint8_t *)*table + (size_t)*cap * size, 0, (size_t)(n - *cap) * size);
        *cap = n;
    }
    return (uint8_t *)*table + (size_t)id_ * size;
}

#define SLOT(c, table, id_) ((typeof((c)->table))slot((void **)&(c)->table, &(c)->table##_cap, id_, sizeof(*(c)->table)))

static void lose(Channel *c, const char *why)
{
    mime_warn("gpu: channel %u stops: %s", c->id, why);
    c->hdr->lost = 1;
}

/* the heap buffer and offset of guest memory, null when out of range. */
static VkBuffer heap(Channel *c, const MimeGpuMem *m, uint64_t size, uint64_t *offset)
{
    if (m->block >= MIME_GPU_MAX_BLOCKS || !c->blocks[m->block] || m->offset > c->block_size[m->block] ||
        size > c->block_size[m->block] - m->offset)
        return VK_NULL_HANDLE;
    *offset = m->offset;
    return c->blocks[m->block];
}

static void *heap_ptr(Channel *c, const MimeGpuMem *m, uint64_t size)
{
    uint64_t off;
    if (!heap(c, m, size, &off))
        return nullptr;
    return c->block_base[m->block] + off;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline void cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    __asm__ volatile("yield");
#endif
}

/* ---------------------------------------------------------------- batches */

static void defer(Channel *c, Garbage g)
{
    if (c->garbage_count == c->garbage_cap) {
        c->garbage_cap = c->garbage_cap ? c->garbage_cap * 2 : 64;
        c->garbage = realloc(c->garbage, c->garbage_cap * sizeof(Garbage));
    }
    g.value = c->submitted + 1;
    c->garbage[c->garbage_count++] = g;
}

static void destroy_garbage(Garbage *g)
{
    if (g->view)
        g_vk.fn.vkDestroyImageView(g_vk.device, g->view, nullptr);
    if (g->image)
        g_vk.fn.vkDestroyImage(g_vk.device, g->image, nullptr);
    if (g->pipeline)
        g_vk.fn.vkDestroyPipeline(g_vk.device, g->pipeline, nullptr);
    release_alloc(&g->mem);
}

/* releases resources held by completed batches. */
static void collect(Channel *c)
{
    uint64_t done = __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE);
    uint32_t keep = 0;
    for (uint32_t i = 0; i < c->garbage_count; i++) {
        if (c->garbage[i].value <= done)
            destroy_garbage(&c->garbage[i]);
        else
            c->garbage[keep++] = c->garbage[i];
    }
    c->garbage_count = keep;
}

static void recycle(Channel *c, Batch *b)
{
    for (uint32_t i = 0; i < b->pool_count; i++) {
        g_vk.fn.vkResetDescriptorPool(g_vk.device, b->pools[i], 0);
        if (c->spare_count == c->spare_cap) {
            c->spare_cap = c->spare_cap ? c->spare_cap * 2 : 16;
            c->spare_pools = realloc(c->spare_pools, c->spare_cap * sizeof(VkDescriptorPool));
        }
        c->spare_pools[c->spare_count++] = b->pools[i];
    }
    b->pool_count = 0;
    b->value = 0;
}

static void wait_value(Channel *c, uint64_t value)
{
    if (__atomic_load_n(&c->completed, __ATOMIC_ACQUIRE) >= value)
        return;
    VkSemaphoreWaitInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &c->timeline, &value};
    g_vk.fn.vkWaitSemaphores(g_vk.device, &info, UINT64_MAX);
    uint64_t done = __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE);
    while (done < value && !__atomic_compare_exchange_n(&c->completed, &done, value, false, __ATOMIC_ACQ_REL,
                                                         __ATOMIC_ACQUIRE))
        ;
}

/* starts recording on a free command buffer. */
static Batch *batch(Channel *c)
{
    if (c->batch)
        return c->batch;
    Batch *pick = nullptr;
    for (int i = 0; i < MAX_BATCHES && !pick; i++)
        if (!c->batches[i].value)
            pick = &c->batches[i];
    if (!pick) {
        for (int i = 0; i < MAX_BATCHES; i++)
            if (!pick || c->batches[i].value < pick->value)
                pick = &c->batches[i];
        wait_value(c, pick->value);
    }
    for (int i = 0; i < MAX_BATCHES; i++)
        if (c->batches[i].value && c->batches[i].value <= __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE))
            recycle(c, &c->batches[i]);
    collect(c);
    if (!pick->cmd) {
        VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, c->pool,
                                          VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        g_vk.fn.vkAllocateCommandBuffers(g_vk.device, &ai, &pick->cmd);
    } else {
        g_vk.fn.vkResetCommandBuffer(pick->cmd, 0);
    }
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    g_vk.fn.vkBeginCommandBuffer(pick->cmd, &begin);
    pick->fence = 0;
    pick->ring_start = c->ring_head;
    c->batch = pick;
    c->wrote = true;
    c->st.dirty = DIRTY_ALL;
    c->st.bound = VK_NULL_HANDLE;
    return pick;
}

static VkCommandBuffer cbuf(Channel *c)
{
    return batch(c)->cmd;
}

/* every write so far reaches every later read or write, transfers and the host's included. */
static void barrier(Channel *c)
{
    if (!c->wrote)
        return;
    VkMemoryBarrier2 mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           nullptr,
                           VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_MEMORY_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &mb};
    g_vk.fn.vkCmdPipelineBarrier2(cbuf(c), &dep);
    c->wrote = false;
}

static void image_layout(Channel *c, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier2 b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS},
    };
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &b};
    g_vk.fn.vkCmdPipelineBarrier2(cbuf(c), &dep);
}

static void end_rendering(Channel *c);
static void scopes_suspend(Channel *c);
static void scopes_resume(Channel *c);
static void queries_finish(Channel *c, uint64_t completed);
static void xfb_enter(Channel *c);

static void signal_fence(Channel *c, uint64_t value)
{
    uint64_t done = __atomic_load_n(&c->hdr->done_fence, __ATOMIC_ACQUIRE);
    while (done < value &&
           !__atomic_compare_exchange_n(&c->hdr->done_fence, &done, value, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        ;
    uint64_t wait = __atomic_load_n(&c->hdr->wait_fence, __ATOMIC_SEQ_CST);
    if (wait && value >= wait)
        mime_vm_raise_irq(c->vm, MIME_IRQ_GPU);
}

/* submits recorded commands and signals a nonzero guest fence after completion. */
static void commit(Channel *c, uint64_t fence)
{
    end_rendering(c);
    if (!c->batch && !fence && !c->wait_count && !c->signal)
        return;
    Batch *b = batch(c);
    /* the host reads what the GPU wrote of guest memory. */
    VkMemoryBarrier2 mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &mb};
    g_vk.fn.vkCmdPipelineBarrier2(b->cmd, &dep);
    g_vk.fn.vkEndCommandBuffer(b->cmd);
    uint64_t value = c->submitted + 1;
    VkSemaphoreSubmitInfo waits[8], signals[2];
    for (uint32_t i = 0; i < c->wait_count; i++)
        waits[i] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, c->waits[i], 0,
                                           VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    signals[0] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, c->timeline, value,
                                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    uint32_t signal_count = 1;
    if (c->signal)
        signals[signal_count++] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, c->signal,
                                                          0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkCommandBufferSubmitInfo cb = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, b->cmd};
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, c->wait_count, waits, 1, &cb,
                            signal_count, signals};
    pthread_mutex_lock(&g_vk.queue_lock);
    VkResult r = g_vk.fn.vkQueueSubmit2(g_queue, 1, &submit, VK_NULL_HANDLE);
    pthread_mutex_unlock(&g_vk.queue_lock);
    if (r != VK_SUCCESS) {
        lose(c, "the gpu refused a submission");
        signal_fence(c, UINT64_MAX >> 1);
    }
    b->value = value;
    c->batch = nullptr;
    c->wait_count = 0;
    c->signal = VK_NULL_HANDLE;
    pthread_mutex_lock(&c->retire_lock);
    __atomic_store_n(&c->submitted, value, __ATOMIC_RELEASE);
    if (fence) {
        /* a full table waits for the oldest entry first. */
        while ((c->fence_tail - c->fence_head) >= 256 && !c->retire_stop)
            pthread_cond_wait(&c->retire_cond, &c->retire_lock);
        c->fences[c->fence_tail % 256].value = value;
        c->fences[c->fence_tail % 256].fence = fence;
        c->fence_tail++;
    }
    pthread_cond_broadcast(&c->retire_cond);
    pthread_mutex_unlock(&c->retire_lock);
}

/* signals guest fences as their batch timeline values complete. */
static void *retire_main(void *arg)
{
    Channel *c = arg;
    mime_set_thread_name("mime-gpu-retire");
    pthread_mutex_lock(&c->retire_lock);
    for (;;) {
        while (!c->retire_stop && __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE) >= c->submitted)
            pthread_cond_wait(&c->retire_cond, &c->retire_lock);
        if (c->retire_stop && __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE) >= c->submitted)
            break;
        uint64_t target = c->submitted;
        pthread_mutex_unlock(&c->retire_lock);
        VkSemaphoreWaitInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &c->timeline, &target};
        VkResult r = g_vk.fn.vkWaitSemaphores(g_vk.device, &info, 100000000ull);
        uint64_t value = 0;
        g_vk.fn.vkGetSemaphoreCounterValue(g_vk.device, c->timeline, &value);
        pthread_mutex_lock(&c->retire_lock);
        uint64_t done = __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE);
        while (done < value &&
               !__atomic_compare_exchange_n(&c->completed, &done, value, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            ;
        uint64_t signal = 0;
        while (c->fence_head != c->fence_tail && c->fences[c->fence_head % 256].value <= value) {
            signal = c->fences[c->fence_head % 256].fence;
            c->fence_head++;
        }
        pthread_cond_broadcast(&c->retire_cond);
        pthread_mutex_unlock(&c->retire_lock);
        queries_finish(c, value);
        pthread_mutex_lock(&c->retire_lock);
        if (signal) {
            pthread_mutex_unlock(&c->retire_lock);
            signal_fence(c, signal);
            pthread_mutex_lock(&c->retire_lock);
        }
        if (r == VK_ERROR_DEVICE_LOST) {
            c->hdr->lost = 1;
            signal_fence(c, UINT64_MAX >> 1);
            break;
        }
    }
    pthread_mutex_unlock(&c->retire_lock);
    return nullptr;
}

/* ---------------------------------------------------------------- uniform ring */

/*
 * preserves uniform data from the oldest unfinished batch through the current head. a full ring waits for old
 * batches, or submits the current batch if it filled the ring alone. returns null if the batch changed, invalidating
 * the caller's prepared state.
 */
static uint8_t *ring_alloc(Channel *c, uint32_t bytes, uint32_t *offset)
{
    uint32_t align = (uint32_t)g_props.limits.minUniformBufferOffsetAlignment;
    if (align < 16)
        align = 16;
    bytes = (bytes + align - 1) & ~(align - 1);
    for (;;) {
        Batch *oldest = nullptr;
        uint64_t done = __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE);
        /* the oldest batch in flight, else the one being recorded. */
        for (int i = 0; i < MAX_BATCHES; i++) {
            Batch *b = &c->batches[i];
            if (b->value && b->value > done && (!oldest || b->value < oldest->value))
                oldest = b;
        }
        if (!oldest)
            oldest = c->batch;
        uint32_t head = c->ring_head, start = UINT32_MAX;
        if (!oldest) {
            start = head + bytes <= RING_BYTES ? head : 0;
        } else {
            uint32_t tail = oldest->ring_start;
            if (tail <= head) {
                if (head + bytes <= RING_BYTES)
                    start = head;
                else if (bytes < tail)
                    start = 0;
            } else if (head + bytes < tail) {
                start = head;
            }
        }
        if (start != UINT32_MAX) {
            c->ring_head = start + bytes;
            *offset = start;
            return c->uniforms_map + start;
        }
        if (oldest == c->batch) {
            commit(c, 0);
            return nullptr;
        }
        wait_value(c, oldest->value);
        recycle(c, oldest);
    }
}

/* ---------------------------------------------------------------- textures */

static Texture *texture_of(Channel *c, uint32_t id_, bool surface, bool depth)
{
    if (surface) {
        Surface *s = id_ < c->surfaces_cap ? &c->surfaces[id_] : nullptr;
        if (!s || !s->live)
            return nullptr;
        Texture *t = depth ? &s->depth : &s->color;
        return t->image ? t : nullptr;
    }
    Texture *t = id_ < c->textures_cap ? &c->textures[id_] : nullptr;
    return t && t->image ? t : nullptr;
}

static VkImageViewType view_type(uint32_t type)
{
    switch (type) {
    case TEXTURE_1D: return VK_IMAGE_VIEW_TYPE_1D;
    case TEXTURE_1D_ARRAY: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case TEXTURE_2D_ARRAY: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TEXTURE_CUBE: return VK_IMAGE_VIEW_TYPE_CUBE;
    case TEXTURE_CUBE_ARRAY: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TEXTURE_3D: return VK_IMAGE_VIEW_TYPE_3D;
    default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

/* sampling uses the depth aspect of depth-stencil images, combining format and guest swizzles. */
static VkImageView sampled_view(Texture *t)
{
    VkComponentMapping base = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
                               VK_COMPONENT_SWIZZLE_A};
    if (t->mtl_format == 1)
        base = (VkComponentMapping){VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO,
                                    VK_COMPONENT_SWIZZLE_R};
    VkComponentSwizzle by[6] = {VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE, base.r, base.g, base.b, base.a};
    uint32_t sw = t->swizzle;
    VkComponentMapping m;
    VkComponentSwizzle *out[4] = {&m.r, &m.g, &m.b, &m.a};
    for (int i = 0; i < 4; i++) {
        uint8_t s = (uint8_t)(sw >> (8 * i));
        *out[i] = s < 6 ? by[s] : component(s);
    }
    VkImageAspectFlags aspect = t->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : t->aspect;
    VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = t->view_type,
        .format = t->format,
        .components = m,
        .subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS},
    };
    VkImageView view = VK_NULL_HANDLE;
    g_vk.fn.vkCreateImageView(g_vk.device, &info, nullptr, &view);
    return view;
}

/* single-level, single-slice view for passes and blits. */
static VkImageView attachment_view(Texture *t, uint32_t level, uint32_t slice)
{
    uint32_t key = level << 16 | slice;
    for (uint32_t i = 0; i < t->view_count; i++)
        if (t->views[i].key == key)
            return t->views[i].view;
    bool layered3d = t->type == TEXTURE_3D;
    VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = t->type == TEXTURE_1D || t->type == TEXTURE_1D_ARRAY ? VK_IMAGE_VIEW_TYPE_1D
                                                                        : VK_IMAGE_VIEW_TYPE_2D,
        .format = t->format,
        .subresourceRange = {t->aspect, level, 1, slice, 1},
    };
    VkImageView view = VK_NULL_HANDLE;
    if (layered3d) {
        /* a slice of a 3d texture renders through a 2d array view of it. */
        info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        info.subresourceRange.baseArrayLayer = slice;
    }
    if (g_vk.fn.vkCreateImageView(g_vk.device, &info, nullptr, &view) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    t->views = realloc(t->views, (t->view_count + 1) * sizeof(View));
    t->views[t->view_count++] = (View){key, view};
    return view;
}

/* a view of one level for image load and store: all layers when layered, else the one layer as a 2d image. */
static VkImageView storage_view(Texture *t, uint32_t level, uint32_t layer, bool layered)
{
    if (!(t->aspect & VK_IMAGE_ASPECT_COLOR_BIT) || level >= t->levels)
        return VK_NULL_HANDLE;
    bool single = !layered && (t->type == TEXTURE_2D_ARRAY || t->type == TEXTURE_CUBE || t->type == TEXTURE_CUBE_ARRAY);
    if (single && layer >= t->layers)
        return VK_NULL_HANDLE;
    uint32_t key = 0x80000000u | (layered || !single ? 0x40000000u : 0) | (level & 0xFF) << 16 | (single ? layer & 0xFFFF : 0);
    for (uint32_t i = 0; i < t->view_count; i++)
        if (t->views[i].key == key)
            return t->views[i].view;
    VkImageViewType type = single ? VK_IMAGE_VIEW_TYPE_2D
                           : t->type == TEXTURE_CUBE || t->type == TEXTURE_CUBE_ARRAY ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                                                     : t->view_type;
    VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = type,
        .format = t->format,
        .subresourceRange = {t->aspect, level, 1, single ? layer : 0, single ? 1 : VK_REMAINING_ARRAY_LAYERS},
    };
    VkImageView view = VK_NULL_HANDLE;
    if (g_vk.fn.vkCreateImageView(g_vk.device, &info, nullptr, &view) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    t->views = realloc(t->views, (t->view_count + 1) * sizeof(View));
    t->views[t->view_count++] = (View){key, view};
    return view;
}

static void texture_free(Channel *c, Texture *t)
{
    if (c) {
        for (uint32_t i = 0; i < t->view_count; i++)
            defer(c, (Garbage){.view = t->views[i].view});
        if (t->sampled)
            defer(c, (Garbage){.view = t->sampled});
        if (t->image)
            defer(c, (Garbage){.image = t->image, .mem = t->mem});
    } else {
        for (uint32_t i = 0; i < t->view_count; i++)
            g_vk.fn.vkDestroyImageView(g_vk.device, t->views[i].view, nullptr);
        if (t->sampled)
            g_vk.fn.vkDestroyImageView(g_vk.device, t->sampled, nullptr);
        if (t->image)
            g_vk.fn.vkDestroyImage(g_vk.device, t->image, nullptr);
        release_alloc(&t->mem);
    }
    free(t->views);
    memset(t, 0, sizeof(*t));
}

static bool texture_make(Texture *t, uint32_t type, uint32_t mtl_format, uint32_t w, uint32_t h, uint32_t depth,
                         uint32_t levels, uint32_t samples, bool target)
{
    VkFormat format = pixel_format(mtl_format);
    if (!format)
        return false;
    memset(t, 0, sizeof(*t));
    t->format = format;
    t->aspect = format_aspects(format);
    t->mtl_format = mtl_format;
    t->type = type;
    t->width = w ? w : 1;
    t->height = type == TEXTURE_1D || type == TEXTURE_1D_ARRAY ? 1 : h ? h : 1;
    t->depth = type == TEXTURE_3D ? (depth ? depth : 1) : 1;
    t->layers = type == TEXTURE_CUBE ? 6 : type == TEXTURE_CUBE_ARRAY || type == TEXTURE_2D_ARRAY || type == TEXTURE_1D_ARRAY
                                               ? (depth ? depth : 1)
                                               : 1;
    t->levels = levels ? levels : 1;
    t->samples = samples ? samples : 1;
    t->view_type = view_type(type);
    VkFormatProperties props;
    g_vk.fn.vkGetPhysicalDeviceFormatProperties(g_vk.physical_device, format, &props);
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (g_storage && t->samples == 1 && (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (target || t->samples > 1) {
        if (t->aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        } else if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
    }
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = (type == TEXTURE_CUBE || type == TEXTURE_CUBE_ARRAY ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) |
                 (type == TEXTURE_3D && (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                      ? VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT
                      : 0),
        .imageType = type == TEXTURE_3D                                   ? VK_IMAGE_TYPE_3D
                     : type == TEXTURE_1D || type == TEXTURE_1D_ARRAY ? VK_IMAGE_TYPE_1D
                                                                      : VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {t->width, t->height, t->depth},
        .mipLevels = t->levels,
        .arrayLayers = t->layers,
        .samples = (VkSampleCountFlagBits)t->samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (g_vk.fn.vkCreateImage(g_vk.device, &info, nullptr, &t->image) != VK_SUCCESS) {
        t->image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements req;
    g_vk.fn.vkGetImageMemoryRequirements(g_vk.device, t->image, &req);
    if (!allocate(&req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &t->mem) ||
        g_vk.fn.vkBindImageMemory(g_vk.device, t->image, t->mem.memory, t->mem.offset) != VK_SUCCESS) {
        g_vk.fn.vkDestroyImage(g_vk.device, t->image, nullptr);
        release_alloc(&t->mem);
        t->image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* new images start in GENERAL; presentation may temporarily change back-buffer layout. */
static void texture_init_layout(Channel *c, Texture *t)
{
    image_layout(c, t->image, t->aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
}

static void texture_swizzle(Channel *c, uint32_t id_, uint32_t swizzle)
{
    if (id_ >= c->textures_cap || !c->textures[id_].image)
        return;
    Texture *t = &c->textures[id_];
    if (t->sampled)
        defer(c, (Garbage){.view = t->sampled});
    t->swizzle = swizzle;
    t->sampled = sampled_view(t);
    c->st.dirty |= DIRTY_TEXTURES;
}

static bool in_pass(Channel *c, uint32_t id_, bool surface);

static void texture_create(Channel *c, const MimeGpuTextureCreate *t)
{
    if (in_pass(c, t->id, false))
        end_rendering(c);
    Texture *x = SLOT(c, textures, t->id);
    texture_free(c, x);
    if (!texture_make(x, t->type, t->format, t->width, t->height, t->depth, t->levels, t->samples,
                      (t->usage & MIME_GPU_USAGE_TARGET) != 0)) {
        mime_warn("gpu: texture %u (%ux%u format %u) failed", t->id, t->width, t->height, t->format);
        return;
    }
    end_rendering(c);
    texture_init_layout(c, x);
    c->wrote = true;
    x->swizzle = t->swizzle;
    x->sampled = sampled_view(x);
    c->st.dirty |= DIRTY_TEXTURES;
}

static VkSemaphore surface_acquire(Channel *c, Surface *s);

/* a surface's back buffer the presenter had comes back to the general layout. */
static void use_surface(Channel *c, uint32_t id_, bool surface)
{
    if (!surface || id_ >= c->surfaces_cap)
        return;
    Surface *s = &c->surfaces[id_];
    if (!s->presented)
        return;
    VkSemaphore wait = surface_acquire(c, s);
    if (wait && c->wait_count < 8)
        c->waits[c->wait_count++] = wait;
    image_layout(c, s->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
}

static void texture_copy(Channel *c, const MimeGpuTextureCopy *t, bool upload)
{
    uint32_t kind = t->surface & 0xFF;
    Texture *tex = texture_of(c, t->id, kind != MIME_GPU_COPY_TEXTURE, kind == MIME_GPU_COPY_SURFACE_DEPTH);
    uint32_t image = t->image_bytes ? t->image_bytes : t->row_bytes * t->height;
    uint64_t bytes = (uint64_t)image * (t->depth ? t->depth : 1);
    uint64_t off;
    VkBuffer buf = heap(c, &t->mem, bytes, &off);
    if (!tex || !buf || !t->width || !t->height || t->level >= tex->levels)
        return;
    end_rendering(c);
    use_surface(c, t->id, kind == MIME_GPU_COPY_SURFACE);
    barrier(c);
    VkImageAspectFlags aspect = tex->aspect;
    if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
        aspect = (t->surface & MIME_GPU_COPY_STENCIL) ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    if (tex->samples > 1)
        return;
    uint32_t texel = texel_bytes(tex->format, aspect);
    bool compressed = is_compressed(tex->format);
    uint32_t row_texels = t->row_bytes / texel * (compressed ? 4 : 1);
    uint32_t rows = t->row_bytes ? image / t->row_bytes * (compressed ? 4 : 1) : 0;
    bool layered = tex->type != TEXTURE_3D;
    uint32_t height = tex->type == TEXTURE_1D || tex->type == TEXTURE_1D_ARRAY ? 1 : t->height;
    uint32_t slice = t->slice;
    VkBufferImageCopy region = {
        .bufferOffset = off,
        .bufferRowLength = row_texels,
        .bufferImageHeight = rows,
        .imageSubresource = {aspect, t->level, layered ? slice : 0, layered ? (t->depth > 1 ? t->depth : 1) : 1},
        .imageOffset = {(int32_t)t->x, height == 1 && tex->height == 1 ? 0 : (int32_t)t->y, layered ? 0 : (int32_t)(t->z + t->slice)},
        .imageExtent = {t->width, height, layered ? 1 : (t->depth ? t->depth : 1)},
    };
    if (upload)
        g_vk.fn.vkCmdCopyBufferToImage(cbuf(c), buf, tex->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    else
        g_vk.fn.vkCmdCopyImageToBuffer(cbuf(c), tex->image, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &region);
    c->wrote = true;
}

static void texture_mipmaps(Channel *c, uint32_t id_)
{
    Texture *t = texture_of(c, id_, false, false);
    if (!t || t->levels < 2 || !(t->aspect & VK_IMAGE_ASPECT_COLOR_BIT) || is_compressed(t->format))
        return;
    end_rendering(c);
    barrier(c);
    int32_t w = (int32_t)t->width, h = (int32_t)t->height, d = (int32_t)t->depth;
    for (uint32_t level = 1; level < t->levels; level++) {
        int32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1, nd = d > 1 ? d / 2 : 1;
        VkImageBlit region = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, t->layers},
            .srcOffsets = {{0, 0, 0}, {w, h, d}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, t->layers},
            .dstOffsets = {{0, 0, 0}, {nw, nh, nd}},
        };
        g_vk.fn.vkCmdBlitImage(cbuf(c), t->image, VK_IMAGE_LAYOUT_GENERAL, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region,
                               VK_FILTER_LINEAR);
        c->wrote = true;
        barrier(c);
        w = nw;
        h = nh;
        d = nd;
    }
}

/* ---------------------------------------------------------------- programs */

typedef struct CompileJob {
    struct CompileJob *next;
    Channel *c;
    Program *p;
    char *vs, *fs, *bindings;
    size_t vs_len, fs_len, bindings_len;
    char *extra[MIME_SHADER_EXTRAS];
    char *feedback, *fragment;
    size_t feedback_len, fragment_len;
    bool feedback_separate;
    bool compute;
    MimeGpuMem result;
    uint32_t capacity;
} CompileJob;

static pthread_mutex_t g_compile_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_compile_cond = PTHREAD_COND_INITIALIZER;
static CompileJob *g_compile_first, *g_compile_last;
static int g_compilers;

static void append_program_error(MimeShaderResult *result, const char *message)
{
    MimeGpuProgramInfo *info = (MimeGpuProgramInfo *)result->info;
    size_t length = strlen(message), old_length = info->log_bytes;
    size_t size = (result->info_size + old_length + length + 2 + 7) & ~(size_t)7;
    if (size > UINT32_MAX)
        return;
    uint8_t *data = realloc(result->info, size);
    if (!data)
        return;
    result->info = data;
    info = (MimeGpuProgramInfo *)data;
    uint32_t offset = result->info_size;
    memcpy(data + offset, data + info->log_offset, old_length);
    memcpy(data + offset + old_length, message, length);
    data[offset + old_length + length] = '\n';
    memset(data + offset + old_length + length + 1, 0, size - offset - old_length - length - 1);
    info->log_offset = offset;
    info->log_bytes = (uint32_t)(old_length + length + 1);
    info->size = result->info_size = (uint32_t)size;
}

static bool program_objects(Program *p, MimeShaderResult *r)
{
    for (int s = 0; s < (p->compute ? 1 : MIME_GPU_STAGES); s++) {
        VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, r->spirv_words[s] * 4,
                                         r->spirv[s]};
        if (g_vk.fn.vkCreateShaderModule(g_vk.device, &info, nullptr, &p->module[s]) != VK_SUCCESS)
            return false;
    }
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
        if (!r->spirv_extra[x])
            continue;
        VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
                                         r->extra_words[x] * 4, r->spirv_extra[x]};
        if (g_vk.fn.vkCreateShaderModule(g_vk.device, &info, nullptr, &p->extra[x]) != VK_SUCCESS)
            return false;
    }
    p->has_tess = p->extra[MIME_SHADER_CONTROL] && p->extra[MIME_SHADER_EVALUATION];
    p->has_geometry = p->extra[MIME_SHADER_GEOMETRY] != VK_NULL_HANDLE;
    VkDescriptorSetLayoutBinding *b = calloc(r->binding_count ? r->binding_count : 1, sizeof(*b));
    for (uint32_t i = 0; i < r->binding_count; i++) {
        const MimeShaderBinding *m = &r->bindings[i];
        b[i].binding = m->binding;
        b[i].descriptorCount = m->count;
        b[i].descriptorType = m->kind == MIME_SHADER_UNIFORMS ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                              : m->kind == MIME_SHADER_BLOCK  ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : m->kind == MIME_SHADER_STORAGE || m->kind == MIME_SHADER_COUNTER
                                  ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                              : m->kind == MIME_SHADER_IMAGE ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                             : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].stageFlags = p->compute ? VK_SHADER_STAGE_COMPUTE_BIT
                          : ((m->stages & MIME_SHADER_BIT_VERTEX) ? VK_SHADER_STAGE_VERTEX_BIT : 0) |
                              ((m->stages & MIME_SHADER_BIT_FRAGMENT) ? VK_SHADER_STAGE_FRAGMENT_BIT : 0) |
                          ((m->stages & MIME_SHADER_BIT_CONTROL) ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT : 0) |
                          ((m->stages & MIME_SHADER_BIT_EVALUATION) ? VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0) |
                          ((m->stages & MIME_SHADER_BIT_GEOMETRY) ? VK_SHADER_STAGE_GEOMETRY_BIT : 0);
        p->has_uniforms |= m->kind == MIME_SHADER_UNIFORMS;
    }
    VkDescriptorSetLayoutCreateInfo set = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
                                           r->binding_count, b};
    VkResult res = g_vk.fn.vkCreateDescriptorSetLayout(g_vk.device, &set, nullptr, &p->set_layout);
    free(b);
    if (res != VK_SUCCESS)
        return false;
    VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &p->set_layout};
    if (g_vk.fn.vkCreatePipelineLayout(g_vk.device, &layout, nullptr, &p->layout) != VK_SUCCESS)
        return false;
    p->bindings = r->bindings;
    p->binding_count = r->binding_count;
    r->bindings = nullptr;
    if (p->compute) {
        VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                                    VK_SHADER_STAGE_COMPUTE_BIT, p->module[0], "main"};
        ci.layout = p->layout;
        if (g_vk.fn.vkCreateComputePipelines(g_vk.device, g_cache, 1, &ci, nullptr, &p->compute_pipeline) != VK_SUCCESS)
            return false;
    }
    return true;
}

/* appends the stage fields the host found to the info the guest reads, after the data it has. */
static void append_stage_info(MimeShaderResult *r, const MimeGpuStageInfo *stage, const SpirvFeedback *fb)
{
    MimeGpuProgramInfo *info = (MimeGpuProgramInfo *)r->info;
    size_t base = r->info_size, names = 0;
    for (uint32_t i = 0; i < fb->count; i++)
        names += strlen(fb->names[i]) + 1;
    size_t size = (base + fb->count * sizeof(MimeGpuFeedbackInfo) + names + 7) & ~(size_t)7;
    uint8_t *data = realloc(r->info, size);
    if (!data)
        return;
    memset(data + base, 0, size - base);
    info = (MimeGpuProgramInfo *)data;
    info->stage = *stage;
    info->stage.feedback_count = fb->count;
    info->stage.feedback_offset = (uint32_t)base;
    info->stage.feedback_buffers = fb->buffers;
    memcpy(info->stage.feedback_stride, fb->stride, sizeof(fb->stride));
    MimeGpuFeedbackInfo *list = (MimeGpuFeedbackInfo *)(data + base);
    size_t at = base + fb->count * sizeof(MimeGpuFeedbackInfo);
    for (uint32_t i = 0; i < fb->count; i++) {
        list[i] = fb->info[i];
        list[i].name_offset = (uint32_t)at;
        size_t n = strlen(fb->names[i]) + 1;
        memcpy(data + at, fb->names[i], n);
        at += n;
    }
    info->size = (uint32_t)size;
    r->info = data;
    r->info_size = (uint32_t)size;
}

/* what the extra stages of a compiled program add to the host's view of it. */
static bool stage_details(Program *p, MimeShaderResult *r, CompileJob *j, MimeGpuStageInfo *stage, SpirvFeedback *fb)
{
    stage->stages = MIME_SHADER_BIT_VERTEX | MIME_SHADER_BIT_FRAGMENT;
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
        if (!r->spirv_extra[x])
            continue;
        stage->stages |= MIME_SHADER_BIT_CONTROL << x;
        spirv_modes(r->spirv_extra[x], r->extra_words[x], stage);
    }
    if (r->spirv_extra[MIME_SHADER_GEOMETRY]) {
        switch (stage->geometry_input) {
        case 0x0:
            p->geometry_class = 1;
            break;
        case 0x1:
        case 0xA:
            p->geometry_class = 2;
            break;
        default:
            p->geometry_class = 3;
            break;
        }
    }
    if (!j->feedback_len)
        return true;
    /* the last stage before rasterization captures. */
    int capture = r->spirv_extra[MIME_SHADER_GEOMETRY]     ? -1 - MIME_SHADER_GEOMETRY
                  : r->spirv_extra[MIME_SHADER_EVALUATION] ? -1 - MIME_SHADER_EVALUATION
                                                           : 0;
    uint32_t **spirv = capture < 0 ? &r->spirv_extra[-1 - capture] : &r->spirv[0];
    size_t *words = capture < 0 ? &r->extra_words[-1 - capture] : &r->spirv_words[0];
    uint32_t *patched;
    size_t patched_words;
    char error[200];
    if (!spirv_feedback(*spirv, *words, j->feedback, j->feedback_len, j->feedback_separate, &patched, &patched_words,
                        fb, error, sizeof(error))) {
        append_program_error(r, error);
        return false;
    }
    free(*spirv);
    *spirv = patched;
    *words = patched_words;
    return true;
}

static void compile_job(CompileJob *j)
{
    MimeShaderResult r;
    MimeShaderStages extra = {j->extra[MIME_SHADER_CONTROL],    j->extra[MIME_SHADER_EVALUATION],
                              j->extra[MIME_SHADER_GEOMETRY],
                              j->extra[MIME_SHADER_CONTROL] ? strlen(j->extra[MIME_SHADER_CONTROL]) : 0,
                              j->extra[MIME_SHADER_EVALUATION] ? strlen(j->extra[MIME_SHADER_EVALUATION]) : 0,
                              j->extra[MIME_SHADER_GEOMETRY] ? strlen(j->extra[MIME_SHADER_GEOMETRY]) : 0};
    bool has_extra = j->extra[0] || j->extra[1] || j->extra[2];
    if (j->compute)
        mime_shader_compile_compute(j->vs, j->vs_len, &r);
    else
        mime_shader_compile_stages(j->vs, j->vs_len, j->fs, j->fs_len, has_extra ? &extra : nullptr, j->bindings,
                                   j->bindings_len, &r);
    MimeGpuStageInfo stage = {};
    SpirvFeedback fb = {};
    bool ok = r.ok;
    if (ok && !j->compute && j->fragment_len) {
        uint32_t *patched;
        size_t patched_words;
        if (spirv_bind_outputs(r.spirv[1], r.spirv_words[1], j->fragment, j->fragment_len, &patched, &patched_words)) {
            free(r.spirv[1]);
            r.spirv[1] = patched;
            r.spirv_words[1] = patched_words;
        }
    }
    if (ok && !j->compute && (has_extra || j->feedback_len)) {
        ok = stage_details(j->p, &r, j, &stage, &fb);
        append_stage_info(&r, &stage, &fb);
    }
    const char *dump = getenv("MIME_GPU_DUMP_SPIRV");
    if (dump && r.ok) {
        static unsigned serial;
        unsigned n = __atomic_fetch_add(&serial, 1, __ATOMIC_RELAXED);
        const char *stage_names[] = {"vert", "frag", "tesc", "tese", "geom"};
        for (int k = 0; k < 5; k++) {
            const uint32_t *w = k < 2 ? r.spirv[k] : r.spirv_extra[k - 2];
            size_t words = k < 2 ? r.spirv_words[k] : r.extra_words[k - 2];
            char path[300];
            snprintf(path, sizeof(path), "%s/p%u-%s.spv", dump, n, stage_names[k]);
            FILE *f = w ? fopen(path, "wb") : nullptr;
            if (f) {
                fwrite(w, 4, words, f);
                fclose(f);
            }
        }
    }
    ok = ok && program_objects(j->p, &r);
    if (r.ok && !ok && !((MimeGpuProgramInfo *)r.info)->log_bytes)
        append_program_error(&r, "the gpu rejected a translated program");
    /* the info goes to the guest last: a nonzero size tells it the program is done. */
    uint8_t *dst = heap_ptr(j->c, &j->result, j->capacity);
    if (dst && j->capacity >= sizeof(MimeGpuProgramInfo)) {
        MimeGpuProgramInfo *info = (MimeGpuProgramInfo *)r.info;
        if (!ok)
            info->status = 1;
        uint32_t size = info->size;
        if (size > j->capacity) {
            /* the guest asks again with room for it. */
            MimeGpuProgramInfo small = *info;
            small.attrib_count = small.uniform_count = small.block_count = small.log_bytes = 0;
            small.stage.feedback_count = 0;
            memcpy(dst + sizeof(uint32_t) * 2, (uint8_t *)&small + sizeof(uint32_t) * 2,
                   sizeof(small) - sizeof(uint32_t) * 2);
            ((MimeGpuProgramInfo *)dst)->status = small.status;
        } else {
            memcpy(dst + sizeof(uint32_t) * 2, r.info + sizeof(uint32_t) * 2, size - sizeof(uint32_t) * 2);
            ((MimeGpuProgramInfo *)dst)->status = info->status;
        }
        __atomic_store_n(&((MimeGpuProgramInfo *)dst)->size, size, __ATOMIC_RELEASE);
    }
    mime_shader_free(&r);
    pthread_mutex_lock(&j->p->lock);
    j->p->compiled = ok;
    j->p->done = true;
    pthread_cond_broadcast(&j->p->cond);
    pthread_mutex_unlock(&j->p->lock);
    free(j->vs);
    free(j->fs);
    free(j->bindings);
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        free(j->extra[x]);
    free(j->feedback);
    free(j->fragment);
    free(j);
}

static void *compiler_main(void *arg)
{
    mime_set_thread_name("mime-gpu-compile");
    pthread_mutex_lock(&g_compile_lock);
    for (;;) {
        while (!g_compile_first)
            pthread_cond_wait(&g_compile_cond, &g_compile_lock);
        CompileJob *j = g_compile_first;
        g_compile_first = j->next;
        if (!g_compile_first)
            g_compile_last = nullptr;
        pthread_mutex_unlock(&g_compile_lock);
        compile_job(j);
        pthread_mutex_lock(&g_compile_lock);
    }
    return nullptr;
}

static void compile_async(CompileJob *j)
{
    pthread_mutex_lock(&g_compile_lock);
    if (g_compilers < MAX_COMPILERS) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        int want = cpus > 2 ? (int)(cpus / 2 < MAX_COMPILERS ? cpus / 2 : MAX_COMPILERS) : 1;
        while (g_compilers < want) {
            pthread_t t;
            if (pthread_create(&t, nullptr, compiler_main, nullptr))
                break;
            pthread_detach(t);
            g_compilers++;
        }
    }
    if (g_compile_last)
        g_compile_last->next = j;
    else
        g_compile_first = j;
    g_compile_last = j;
    pthread_cond_signal(&g_compile_cond);
    pthread_mutex_unlock(&g_compile_lock);
}

static void program_wait(Program *p)
{
    pthread_mutex_lock(&p->lock);
    while (!p->done)
        pthread_cond_wait(&p->cond, &p->lock);
    pthread_mutex_unlock(&p->lock);
}

/* after the GPU is done with it: a pipeline of the program may be in flight. */
static void program_free(Program *p)
{
    program_wait(p);
    if (p->compute_pipeline)
        g_vk.fn.vkDestroyPipeline(g_vk.device, p->compute_pipeline, nullptr);
    for (int s = 0; s < MIME_GPU_STAGES; s++)
        if (p->module[s])
            g_vk.fn.vkDestroyShaderModule(g_vk.device, p->module[s], nullptr);
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        if (p->extra[x])
            g_vk.fn.vkDestroyShaderModule(g_vk.device, p->extra[x], nullptr);
    if (p->layout)
        g_vk.fn.vkDestroyPipelineLayout(g_vk.device, p->layout, nullptr);
    if (p->set_layout)
        g_vk.fn.vkDestroyDescriptorSetLayout(g_vk.device, p->set_layout, nullptr);
    free(p->bindings);
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->cond);
    free(p);
}

static void program_create(Channel *c, const MimeGpuProgramCreate *pc, bool compute)
{
    uint64_t body = pc->cmd.size - sizeof(*pc);
    if ((uint64_t)pc->vertex_bytes + pc->fragment_bytes + pc->bindings_bytes > body) {
        lose(c, "bad program record");
        return;
    }
    Program **slotp = SLOT(c, programs, pc->id);
    if (*slotp) {
        /* waits for pending GPU work before destroying pipelines derived from the guest program. */
        commit(c, 0);
        wait_value(c, c->submitted);
        program_free(*slotp);
    }
    Program *p = calloc(1, sizeof(Program));
    pthread_mutex_init(&p->lock, nullptr);
    pthread_cond_init(&p->cond, nullptr);
    p->compute = compute;
    *slotp = p;
    const char *data = (const char *)(pc + 1);
    CompileJob *j = calloc(1, sizeof(CompileJob));
    j->c = c;
    j->p = p;
    j->compute = compute;
    j->vs = strndup(data, pc->vertex_bytes);
    j->vs_len = strlen(j->vs);
    j->fs = strndup(data + pc->vertex_bytes, pc->fragment_bytes);
    j->fs_len = strlen(j->fs);
    j->bindings = malloc(pc->bindings_bytes + 1);
    memcpy(j->bindings, data + pc->vertex_bytes + pc->fragment_bytes, pc->bindings_bytes);
    j->bindings_len = pc->bindings_bytes;
    j->result = pc->result;
    j->capacity = pc->result_capacity;
    if (c->stages.valid && c->stages.id == pc->id) {
        memcpy(j->extra, c->stages.extra, sizeof(j->extra));
        j->feedback = c->stages.feedback;
        j->feedback_len = c->stages.feedback_len;
        j->fragment = c->stages.fragment;
        j->fragment_len = c->stages.fragment_len;
        j->feedback_separate = c->stages.separate;
    } else {
        for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
            free(c->stages.extra[x]);
        free(c->stages.feedback);
        free(c->stages.fragment);
    }
    memset(&c->stages, 0, sizeof(c->stages));
    compile_async(j);
}

static void program_stages(Channel *c, const MimeGpuProgramStages *ps)
{
    uint64_t body = ps->cmd.size - sizeof(*ps);
    uint64_t total = (uint64_t)ps->control_bytes + ps->evaluation_bytes + ps->geometry_bytes + ps->feedback_bytes +
                     ps->fragment_bytes;
    if (total > body) {
        lose(c, "bad program stages record");
        return;
    }
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        free(c->stages.extra[x]);
    free(c->stages.feedback);
    free(c->stages.fragment);
    memset(&c->stages, 0, sizeof(c->stages));
    const char *data = (const char *)(ps + 1);
    const uint32_t sizes[MIME_SHADER_EXTRAS] = {ps->control_bytes, ps->evaluation_bytes, ps->geometry_bytes};
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
        if (sizes[x])
            c->stages.extra[x] = strndup(data, sizes[x]);
        data += sizes[x];
    }
    if (ps->feedback_bytes) {
        c->stages.feedback = malloc(ps->feedback_bytes);
        memcpy(c->stages.feedback, data, ps->feedback_bytes);
        c->stages.feedback_len = ps->feedback_bytes;
    }
    if (ps->fragment_bytes) {
        c->stages.fragment = malloc(ps->fragment_bytes);
        memcpy(c->stages.fragment, data + ps->feedback_bytes, ps->fragment_bytes);
        c->stages.fragment_len = ps->fragment_bytes;
    }
    c->stages.separate = ps->feedback_separate != 0;
    c->stages.id = ps->id;
    c->stages.valid = true;
}

static Program *program_ready(Channel *c, uint32_t id_)
{
    if (id_ >= c->programs_cap || !c->programs[id_])
        return nullptr;
    Program *p = c->programs[id_];
    program_wait(p);
    return p->compiled ? p : nullptr;
}

static void program_destroy(Channel *c, uint32_t id_)
{
    if (id_ >= c->programs_cap || !c->programs[id_])
        return;
    commit(c, 0);
    wait_value(c, c->submitted);
    for (uint32_t i = 0; i < c->pipelines_cap; i++)
        if (c->pipelines[i].program == c->programs[id_])
            c->pipelines[i].program = nullptr;
    if (c->st.program == c->programs[id_])
        c->st.program = nullptr;
    program_free(c->programs[id_]);
    c->programs[id_] = nullptr;
}

/* ---------------------------------------------------------------- pipelines */

static const VkDynamicState g_dynamic[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_DEPTH_BIAS,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_CULL_MODE,
    VK_DYNAMIC_STATE_FRONT_FACE,
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
    VK_DYNAMIC_STATE_STENCIL_OP,
    VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
    VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
    VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
    VK_DYNAMIC_STATE_POLYGON_MODE_EXT,
    VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,
};

static uint32_t dynamic_count(void)
{
    uint32_t n = 18;
    if (g_polygon_mode)
        n++;
    if (g_depth_clamp)
        n++;
    return n;
}

static const VkDynamicState *dynamic_states(VkDynamicState *buf)
{
    uint32_t n = 18;
    memcpy(buf, g_dynamic, sizeof(VkDynamicState) * 18);
    if (g_polygon_mode)
        buf[n++] = VK_DYNAMIC_STATE_POLYGON_MODE_EXT;
    if (g_depth_clamp)
        buf[n++] = VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT;
    return buf;
}

static VkPrimitiveTopology class_topology(uint32_t topology_class)
{
    return topology_class == 1 ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
           : topology_class == 2 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                 : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static VkPipeline build_pipeline(Pipeline *pl, uint32_t fill, bool clamp, bool zero_to_one)
{
    const MimeGpuPipelineDesc *d = &pl->desc;
    Program *p = pl->program;
    VkPipelineShaderStageCreateInfo stages[5] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, p->module[0], "main"},
    };
    uint32_t stage_count = 1;
    static const VkShaderStageFlagBits extra_stage[MIME_SHADER_EXTRAS] = {
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT};
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        if (p->extra[x])
            stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, extra_stage[x], p->extra[x], "main"};
    stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, p->module[1], "main"};
    VkVertexInputBindingDescription binds[MIME_GPU_MAX_ATTRIBS];
    VkVertexInputBindingDivisorDescription divisors[MIME_GPU_MAX_ATTRIBS];
    VkVertexInputAttributeDescription attrs[MIME_GPU_MAX_ATTRIBS];
    uint32_t nb = 0, na = 0, nd = 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < MIME_GPU_MAX_ATTRIBS; i++) {
        const MimeGpuVertexAttrib *a = &d->attribs[i];
        if (!a->format)
            continue;
        attrs[na++] = (VkVertexInputAttributeDescription){i, a->slot, vertex_format(a->format), a->offset};
        used |= 1u << a->slot;
    }
    for (uint32_t i = 0; i < MIME_GPU_MAX_ATTRIBS; i++) {
        if (!(used & (1u << i)))
            continue;
        const MimeGpuVertexLayout *l = &d->layouts[i];
        bool instanced = l->stride && l->instance_divisor;
        binds[nb++] = (VkVertexInputBindingDescription){i, l->stride,
                                                        instanced ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                                  : VK_VERTEX_INPUT_RATE_VERTEX};
        if (instanced && l->instance_divisor != 1)
            divisors[nd++] = (VkVertexInputBindingDivisorDescription){i, l->instance_divisor};
    }
    VkPipelineVertexInputDivisorStateCreateInfo divisor = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO,
                                                          nullptr, nd, divisors};
    VkPipelineVertexInputStateCreateInfo vertex = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                   nd ? &divisor : nullptr, 0, nb, binds, na, attrs};
    VkPrimitiveTopology topology = p->has_tess       ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
                                   : p->has_geometry ? class_topology(p->geometry_class)
                                                     : class_topology(d->topology & 0xFF);
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       nullptr, 0, topology, VK_FALSE};
    uint32_t patch_points = (d->topology >> 8) & 0xFF;
    VkPipelineTessellationStateCreateInfo tess = {VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO, nullptr, 0,
                                                  patch_points ? patch_points : 3};
    VkPipelineViewportDepthClipControlCreateInfoEXT clip = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT, nullptr, !zero_to_one};
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                  g_clip_control ? &clip : nullptr, 0, g_viewports, nullptr, g_viewports,
                                                  nullptr};
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = clamp,
        .polygonMode = fill ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0,
                                               (VkSampleCountFlagBits)(d->samples ? d->samples : 1)};
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend[MIME_GPU_MAX_COLORS] = {};
    VkFormat colors[MIME_GPU_MAX_COLORS];
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
        const MimeGpuColorTarget *t = &d->color[i];
        colors[i] = t->format ? pixel_format(t->format) : VK_FORMAT_UNDEFINED;
        if (!t->format)
            continue;
        blend[i] = (VkPipelineColorBlendAttachmentState){
            t->blend != 0,          blend_factor(t->src_rgb),   blend_factor(t->dst_rgb), (VkBlendOp)t->rgb_op,
            blend_factor(t->src_alpha), blend_factor(t->dst_alpha), (VkBlendOp)t->alpha_op, write_mask(t->write_mask)};
    }
    /* the logic operation is one for all the targets, the numbers of GL_CLEAR to GL_SET being the ones of Vulkan. */
    uint8_t logic = d->color[0].logic_op;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
                                              logic != 0 && g_logic_op, logic ? (VkLogicOp)(logic - 1) : VK_LOGIC_OP_COPY,
                                              MIME_GPU_MAX_COLORS, blend};
    VkDynamicState dyn[24];
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
                                                dynamic_count(), dynamic_states(dyn)};
    VkFormat depth = d->depth_format ? pixel_format(d->depth_format) : VK_FORMAT_UNDEFINED;
    VkFormat stencil = d->stencil_format ? pixel_format(d->stencil_format) : VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0,
                                               MIME_GPU_MAX_COLORS, colors, depth, stencil};
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = stage_count,
        .pStages = stages,
        .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly,
        .pTessellationState = p->has_tess ? &tess : nullptr,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dynamic,
        .layout = p->layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = g_vk.fn.vkCreateGraphicsPipelines(g_vk.device, g_cache, 1, &info, nullptr, &pipeline);
    if (r != VK_SUCCESS)
        mime_warn("gpu: pipeline failed (%d)", (int)r);
    return pipeline;
}

static void pipeline_drop(Channel *c, Pipeline *pl)
{
    for (int i = 0; i < 8; i++)
        if (pl->variants[i])
            defer(c, (Garbage){.pipeline = pl->variants[i]});
    memset(pl, 0, sizeof(*pl));
}

static void pipeline_create(Channel *c, const MimeGpuPipelineCreate *pc)
{
    Pipeline *pl = SLOT(c, pipelines, pc->id);
    if (c->st.pipeline == pl) {
        c->st.pipeline = nullptr;
        c->st.bound = VK_NULL_HANDLE;
    }
    pipeline_drop(c, pl);
    Program *p = program_ready(c, pc->desc.program);
    if (!p)
        return;
    pl->desc = pc->desc;
    pl->program = p;
    pl->valid = true;
    /* prebuilds the most common pipeline variant. */
    pl->variants[0] = build_pipeline(pl, 0, false, false);
}

static VkPipeline pipeline_variant(Pipeline *pl, uint32_t fill, bool clamp, bool zero_to_one)
{
    zero_to_one = zero_to_one && g_clip_control;
    uint32_t i = (g_polygon_mode ? 0 : fill ? 2 : 0) + (g_depth_clamp ? 0 : clamp ? 1 : 0) + (zero_to_one ? 4 : 0);
    if (!pl->variants[i] && pl->program)
        pl->variants[i] = build_pipeline(pl, g_polygon_mode ? 0 : fill, g_depth_clamp ? false : clamp, zero_to_one);
    return pl->variants[i];
}

static VkFilter filter(uint8_t f)
{
    return f ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

static void sampler_create(Channel *c, const MimeGpuSamplerCreate *s)
{
    VkSampler *slotp = SLOT(c, samplers, s->id);
    if (*slotp) {
        /* descriptor sets in flight may name it. */
        commit(c, 0);
        wait_value(c, c->submitted);
        g_vk.fn.vkDestroySampler(g_vk.device, *slotp, nullptr);
        *slotp = VK_NULL_HANDLE;
    }
    VkBorderColor border = s->border == 1 ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
                           : s->border == 2 ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                            : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    float lod_max = s->lod_max;
    if (!s->mip_filter)
        lod_max = 0.25f; /* not mipmapped: the base level, as Metal samples it. */
    VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter(s->mag_filter),
        .minFilter = filter(s->min_filter),
        .mipmapMode = s->mip_filter == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = address_mode(s->address_s),
        .addressModeV = address_mode(s->address_t),
        .addressModeW = address_mode(s->address_r),
        .anisotropyEnable = s->max_anisotropy > 1,
        .maxAnisotropy = s->max_anisotropy > 1 ? s->max_anisotropy : 1,
        .compareEnable = s->compare != 0,
        .compareOp = (VkCompareOp)s->compare,
        .minLod = s->mip_filter ? s->lod_min : 0.0f,
        .maxLod = lod_max,
        .borderColor = border,
    };
    if (info.maxAnisotropy > g_props.limits.maxSamplerAnisotropy)
        info.maxAnisotropy = g_props.limits.maxSamplerAnisotropy;
    if (g_vk.fn.vkCreateSampler(g_vk.device, &info, nullptr, slotp) != VK_SUCCESS)
        *slotp = VK_NULL_HANDLE;
}

/* ---------------------------------------------------------------- surfaces */

static VkSemaphore surface_acquire(Channel *c, Surface *s)
{
    s->presented = false;
    return s->chain ? mvk_wsi_chain_acquire(&g_vk, s->chain) : VK_NULL_HANDLE;
}

static void surface_release(Channel *c, Surface *s)
{
    if (s->chain) {
        /* the presenter goes idle first, whatever it still shows. */
        commit(c, 0);
        if (s->presented) {
            VkSemaphore wait = surface_acquire(c, s);
            if (wait) {
                VkSemaphoreSubmitInfo w = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, wait, 0,
                                           VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
                VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 1, &w};
                pthread_mutex_lock(&g_vk.queue_lock);
                g_vk.fn.vkQueueSubmit2(g_queue, 1, &submit, VK_NULL_HANDLE);
                pthread_mutex_unlock(&g_vk.queue_lock);
            }
        }
        mvk_wsi_chain_destroy(&g_vk, s->chain);
        s->chain = nullptr;
    }
    texture_free(c, &s->color);
    texture_free(c, &s->depth);
}

static void surface_config(Channel *c, const MimeGpuSurfaceConfig *sc)
{
    Surface *s = SLOT(c, surfaces, sc->id);
    uint32_t w = sc->width ? sc->width : 1, h = sc->height ? sc->height : 1;
    uint32_t samples = sc->samples ? sc->samples : 1;
    if (!s->live || s->width != w || s->height != h || s->samples != samples || s->depth_format != sc->depth_format ||
        s->stencil_format != sc->stencil_format) {
        end_rendering(c);
        surface_release(c, s);
        s->live = true;
        if (!texture_make(&s->color, samples > 1 ? TEXTURE_2D_MS : TEXTURE_2D, PIXEL_BGRA8, w, h, 1, 1, samples, true)) {
            mime_warn("gpu: surface %u (%ux%u) failed", sc->id, w, h);
            s->live = false;
            return;
        }
        texture_init_layout(c, &s->color);
        s->color.swizzle = 0x05040302;
        s->color.sampled = sampled_view(&s->color);
        uint32_t ds = sc->depth_format && sc->stencil_format ? PIXEL_D32S8
                      : sc->depth_format                     ? sc->depth_format
                                                             : sc->stencil_format;
        if (ds && texture_make(&s->depth, samples > 1 ? TEXTURE_2D_MS : TEXTURE_2D, ds, w, h, 1, 1, samples, true))
            texture_init_layout(c, &s->depth);
        c->wrote = true;
        s->width = w;
        s->height = h;
        s->samples = samples;
        s->depth_format = sc->depth_format;
        s->stencil_format = sc->stencil_format;
        if (samples == 1)
            s->chain = mvk_wsi_chain_create(&g_vk, s->color.image, s->color.format, (VkExtent2D){w, h});
        if (!s->rendered) {
            VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            g_vk.fn.vkCreateSemaphore(g_vk.device, &info, nullptr, &s->rendered);
        }
    }
    MimeRect client = {sc->x, sc->y, sc->x + (int32_t)w, sc->y + (int32_t)h};
    s->target = (MvkTarget){sc->window_id, sc->source, sc->order, client,
                            {sc->visible[0], sc->visible[1], sc->visible[2], sc->visible[3]}};
    s->sync = sc->sync != 0;
}

static void surface_destroy(Channel *c, uint32_t id_)
{
    if (id_ >= c->surfaces_cap)
        return;
    Surface *s = &c->surfaces[id_];
    if (in_pass(c, id_, true))
        end_rendering(c);
    surface_release(c, s);
    if (s->rendered) {
        commit(c, 0);
        wait_value(c, c->submitted);
        g_vk.fn.vkDestroySemaphore(g_vk.device, s->rendered, nullptr);
    }
    memset(s, 0, sizeof(*s));
}

/* hands the back buffer to the presenter, which waits for rendering before copying it to the window. */
static void present(Channel *c, uint32_t id_)
{
    Surface *s = id_ < c->surfaces_cap ? &c->surfaces[id_] : nullptr;
    if (!s || !s->live || !s->chain || !s->target.window) {
        commit(c, 0);
        return;
    }
    end_rendering(c);
    use_surface(c, id_, true);
    barrier(c);
    image_layout(c, s->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    c->signal = s->rendered;
    commit(c, 0);
    mvk_wsi_chain_present(&g_vk, s->chain, g_queue, g_family, s->rendered, &s->target, s->sync);
    s->presented = true;
}

/* ---------------------------------------------------------------- passes */

static MimeGpuAttachment *pass_att(Channel *c, int i)
{
    return i < MIME_GPU_MAX_COLORS ? &c->pass.color[i] : i == MIME_GPU_MAX_COLORS ? &c->pass.depth : &c->pass.stencil;
}

static bool in_pass(Channel *c, uint32_t id_, bool surface)
{
    if (!c->pass_live)
        return false;
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        if (a->id == id_ && !a->surface == !surface)
            return true;
    }
    return false;
}

static VkAttachmentLoadOp load_op(uint8_t load)
{
    return load == LOAD_LOAD ? VK_ATTACHMENT_LOAD_OP_LOAD
           : load == LOAD_CLEAR ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

/* begins rendering with the guest's load actions. */
static bool begin_rendering(Channel *c)
{
    if (c->rendering)
        return true;
    if (!c->pass_live)
        return false;
    VkRenderingAttachmentInfo colors[MIME_GPU_MAX_COLORS] = {}, depth = {}, stencil = {};
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        if (a->id)
            use_surface(c, a->id, a->surface);
    }
    barrier(c);
    VkRenderingAttachmentInfo *out[ATTACHMENTS];
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        out[i] = &colors[i];
    out[MIME_GPU_MAX_COLORS] = &depth;
    out[MIME_GPU_MAX_COLORS + 1] = &stencil;
    bool any_depth = false, any_stencil = false;
    for (int i = 0; i < ATTACHMENTS; i++) {
        VkRenderingAttachmentInfo *r = out[i];
        r->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        MimeGpuAttachment *a = pass_att(c, i);
        if (!a->id)
            continue;
        Texture *t = texture_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS);
        if (!t || a->level >= t->levels) {
            c->pass_live = false;
            return false;
        }
        r->imageView = attachment_view(t, a->level, a->slice);
        r->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        r->loadOp = load_op(a->load);
        r->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (i < MIME_GPU_MAX_COLORS) {
            memcpy(r->clearValue.color.float32, c->pass.clear_color[i], sizeof(float) * 4);
            Texture *resolve = a->resolve ? texture_of(c, a->resolve, false, false) : nullptr;
            if (resolve) {
                r->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                r->resolveImageView = attachment_view(resolve, 0, 0);
                r->resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
        } else {
            r->clearValue.depthStencil.depth = c->pass.clear_depth;
            r->clearValue.depthStencil.stencil = c->pass.clear_stencil;
            if (i == MIME_GPU_MAX_COLORS)
                any_depth = true;
            else
                any_stencil = true;
        }
    }
    /* depth and stencil must use the same image view when both are attached. */
    if (any_depth && any_stencil && depth.imageView != stencil.imageView)
        any_stencil = false;
    if (!any_depth && any_stencil && !(c->pass_stencil))
        any_stencil = false;
    VkRenderingInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {c->pass.width ? c->pass.width : 1, c->pass.height ? c->pass.height : 1}},
        .layerCount = 1,
        .colorAttachmentCount = MIME_GPU_MAX_COLORS,
        .pColorAttachments = colors,
        .pDepthAttachment = any_depth && c->pass_depth ? &depth : nullptr,
        .pStencilAttachment = any_stencil && c->pass_stencil ? &stencil : nullptr,
    };
    g_vk.fn.vkCmdBeginRendering(cbuf(c), &info);
    c->rendering = true;
    c->wrote = true;
    c->st.dirty = DIRTY_ALL;
    c->st.bound = VK_NULL_HANDLE;
    scopes_resume(c);
    return true;
}

static void end_rendering(Channel *c)
{
    if (!c->rendering) {
        if (!c->pass_live)
            return;
        /* a pass with no draws must still execute its clears. */
        bool clears = false;
        for (int i = 0; i < ATTACHMENTS; i++)
            clears |= pass_att(c, i)->id && pass_att(c, i)->load == LOAD_CLEAR;
        if (!clears || !begin_rendering(c))
            return;
    }
    scopes_suspend(c);
    g_vk.fn.vkCmdEndRendering(cbuf(c));
    c->rendering = false;
    /* resumed rendering loads the previous contents. */
    for (int i = 0; i < ATTACHMENTS; i++)
        pass_att(c, i)->load = LOAD_LOAD;
}

static VkFormat format_of(Channel *c, const MimeGpuAttachment *a, bool depth)
{
    if (!a->id)
        return VK_FORMAT_UNDEFINED;
    Texture *t = texture_of(c, a->id, a->surface, depth);
    return t ? t->format : VK_FORMAT_UNDEFINED;
}

static void pass_begin(Channel *c, const MimeGpuPass *p)
{
    end_rendering(c);
    c->pass = *p;
    c->pass_live = true;
    c->pass_samples = 0;
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        VkFormat f = format_of(c, a, i >= MIME_GPU_MAX_COLORS);
        if (i < MIME_GPU_MAX_COLORS)
            c->pass_color[i] = f;
        else if (i == MIME_GPU_MAX_COLORS)
            c->pass_depth = f == VK_FORMAT_S8_UINT ? VK_FORMAT_UNDEFINED : f;
        else
            c->pass_stencil = (f == VK_FORMAT_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT) ? f : VK_FORMAT_UNDEFINED;
        Texture *t = a->id ? texture_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS) : nullptr;
        if (t && !c->pass_samples)
            c->pass_samples = t->samples;
    }
    if (!c->pass_samples)
        c->pass_samples = 1;
    c->st.dirty = DIRTY_ALL;
}

/* ---------------------------------------------------------------- state */

static VkDescriptorSet allocate_set(Channel *c, VkDescriptorSetLayout layout)
{
    Batch *b = batch(c);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (b->pool_count) {
            VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                              b->pools[b->pool_count - 1], 1, &layout};
            VkDescriptorSet set;
            if (g_vk.fn.vkAllocateDescriptorSets(g_vk.device, &ai, &set) == VK_SUCCESS)
                return set;
        }
        if (b->pool_count == 16) {
            /* submits the batch and starts another when descriptor capacity is exhausted. */
            commit(c, 0);
            b = batch(c);
        }
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (c->spare_count) {
            pool = c->spare_pools[--c->spare_count];
        } else {
            VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SETS_PER_POOL},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SETS_PER_POOL * 4},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_PER_POOL * 16},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SETS_PER_POOL * 4},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SETS_PER_POOL * 4},
            };
            VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, SETS_PER_POOL, 5,
                                             sizes};
            if (g_vk.fn.vkCreateDescriptorPool(g_vk.device, &pi, nullptr, &pool) != VK_SUCCESS)
                return VK_NULL_HANDLE;
        }
        b->pools[b->pool_count++] = pool;
    }
    return VK_NULL_HANDLE;
}

static Texture *dummy_for(uint32_t gl_type)
{
    switch (gl_type) {
    case 0x8B5D: case 0x8B61: case 0x8DC9: case 0x8DD1: return &g_dummy[TEXTURE_1D];
    case 0x8DC0: case 0x8DC3: case 0x8DCE: case 0x8DD6: return &g_dummy[TEXTURE_1D_ARRAY];
    case 0x8DC1: case 0x8DC4: case 0x8DCF: case 0x8DD7: return &g_dummy[TEXTURE_2D_ARRAY];
    case 0x8B60: case 0x8DC5: case 0x8DCC: case 0x8DD4: return &g_dummy[TEXTURE_CUBE];
    case 0x900C: case 0x900D: case 0x900E: case 0x900F: return &g_dummy[TEXTURE_CUBE_ARRAY];
    case 0x8B5F: case 0x8DCB: case 0x8DD3: return &g_dummy[TEXTURE_3D];
    case 0x9108: case 0x9109: case 0x910A: return &g_dummy[TEXTURE_2D_MS];
    default: return &g_dummy[TEXTURE_2D];
    }
}

/* binds textures and named blocks; loose uniforms use a dynamic offset. */
static bool write_set(Channel *c, Program *p)
{
    VkDescriptorSet set = allocate_set(c, p->set_layout);
    if (!set)
        return false;
    VkWriteDescriptorSet writes[1 + MIME_GPU_MAX_UNIFORM_BLOCKS + MIME_GPU_STAGES * MIME_GPU_MAX_TEXTURES +
                                MIME_GPU_MAX_STORAGE_BLOCKS + MIME_GPU_MAX_IMAGES + MIME_GPU_MAX_COUNTER_BUFFERS];
    VkDescriptorBufferInfo buffers[1 + MIME_GPU_MAX_UNIFORM_BLOCKS + MIME_GPU_MAX_STORAGE_BLOCKS +
                                   MIME_GPU_MAX_COUNTER_BUFFERS];
    VkDescriptorImageInfo images[MIME_GPU_STAGES * MIME_GPU_MAX_TEXTURES + MIME_GPU_MAX_IMAGES];
    uint32_t nw = 0, nb = 0, ni = 0;
    for (uint32_t i = 0; i < p->binding_count; i++) {
        const MimeShaderBinding *m = &p->bindings[i];
        if (nw == sizeof(writes) / sizeof(writes[0]))
            break;
        VkWriteDescriptorSet *w = &writes[nw];
        *w = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, m->binding, 0, m->count};
        if (m->kind == MIME_SHADER_STORAGE || m->kind == MIME_SHADER_COUNTER) {
            /* a storage buffer nothing is bound to is left unwritten: a program that reads it is undefined as in GL. */
            if (m->slot >= MIME_GPU_MAX_STORAGE_BLOCKS || !c->st.storage[m->slot].size)
                continue;
            uint64_t off, range = c->st.storage[m->slot].size;
            VkBuffer buf = heap(c, &c->st.storage[m->slot].mem, range, &off);
            if (!buf)
                continue;
            w->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            buffers[nb] = (VkDescriptorBufferInfo){buf, off, range};
            w->pBufferInfo = &buffers[nb++];
            nw++;
            continue;
        }
        if (m->kind == MIME_SHADER_IMAGE) {
            w->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w->pImageInfo = &images[ni];
            uint32_t first = ni;
            bool any = false;
            for (uint32_t e = 0; e < m->count; e++) {
                uint32_t unit = m->slot + e;
                VkImageView view = VK_NULL_HANDLE;
                if (unit < MIME_GPU_MAX_IMAGES && c->st.images[unit].id) {
                    const MimeGpuSetImage *im = &c->st.images[unit];
                    Texture *t = texture_of(c, im->id, false, false);
                    if (t && (t->aspect & VK_IMAGE_ASPECT_COLOR_BIT))
                        view = storage_view(t, im->level, im->layer, im->layered != 0);
                }
                any |= view != VK_NULL_HANDLE;
                images[ni++] = (VkDescriptorImageInfo){VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
            }
            if (!any) {
                ni = first;
                continue;
            }
            nw++;
            continue;
        }
        nw++;
        if (m->kind == MIME_SHADER_UNIFORMS) {
            w->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            uint32_t bytes = m->bytes > c->st.uniform_bytes ? m->bytes : c->st.uniform_bytes;
            bytes = bytes ? (bytes + 15) & ~15u : 16;
            buffers[nb] = (VkDescriptorBufferInfo){c->uniforms, 0, bytes};
            w->pBufferInfo = &buffers[nb++];
        } else if (m->kind == MIME_SHADER_BLOCK) {
            w->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            /* the stage that uses the block holds its buffer; both stages share the slot. */
            uint32_t index = m->binding - 1;
            const MimeGpuMem *mem = nullptr;
            /* the stages after the vertex one read the buffer the vertex stage has. */
            uint32_t stages = (m->stages & 3) | ((m->stages & ~3u) ? 1u : 0);
            for (int s = 0; s < MIME_GPU_STAGES && !mem; s++)
                if ((stages & (1u << s)) && (c->st.uniform_buffer_mask[s] & (1u << index)))
                    mem = &c->st.uniform_buffers[s][index];
            uint64_t off = 0, range = m->bytes ? m->bytes : 16;
            VkBuffer buf = mem ? heap(c, mem, range, &off) : VK_NULL_HANDLE;
            if (!buf) {
                /* nothing bound reads zeros of the ring's start, as Metal would read nothing defined. */
                buf = c->uniforms;
                off = 0;
            }
            buffers[nb] = (VkDescriptorBufferInfo){buf, off, range};
            w->pBufferInfo = &buffers[nb++];
        } else {
            w->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w->pImageInfo = &images[ni];
            int stage = (m->binding - 16) / MIME_GPU_MAX_TEXTURES;
            for (uint32_t e = 0; e < m->count; e++) {
                uint32_t unit = m->slot + e;
                Binding *b = unit < MIME_GPU_MAX_TEXTURES ? &c->st.tex[stage][unit] : nullptr;
                Texture *t = b && b->id ? texture_of(c, b->id, b->surface, false) : nullptr;
                VkSampler smp = b && b->sampler < c->samplers_cap && c->samplers[b->sampler] ? c->samplers[b->sampler]
                                                                                            : g_default_sampler;
                if (!t || !t->sampled)
                    t = dummy_for(m->gl_type);
                images[ni++] = (VkDescriptorImageInfo){smp, t->sampled, VK_IMAGE_LAYOUT_GENERAL};
            }
        }
    }
    g_vk.fn.vkUpdateDescriptorSets(g_vk.device, nw, writes, 0, nullptr);
    c->st.set = set;
    return true;
}

/*
 * allocating uniforms or descriptors may submit and replace the batch. retry both in the new batch if that happens.
 */
static bool place_resources(Channel *c, Program *p)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        Batch *b = batch(c);
        if (p->has_uniforms && !c->st.uniforms_placed) {
            uint32_t bytes = c->st.uniform_bytes;
            for (uint32_t i = 0; i < p->binding_count; i++)
                if (p->bindings[i].kind == MIME_SHADER_UNIFORMS && p->bindings[i].bytes > bytes)
                    bytes = p->bindings[i].bytes;
            uint32_t offset;
            uint8_t *dst = ring_alloc(c, bytes ? bytes : 16, &offset);
            if (!dst) {
                c->st.dirty |= DIRTY_SET;
                continue;
            }
            memcpy(dst, c->st.uniforms, c->st.uniform_bytes);
            memset(dst + c->st.uniform_bytes, 0, (bytes > c->st.uniform_bytes ? bytes : c->st.uniform_bytes) -
                                                     c->st.uniform_bytes);
            c->st.uniform_offset = offset;
            c->st.uniforms_placed = true;
            c->st.dirty |= DIRTY_UNIFORMS;
        }
        if (c->st.dirty & (DIRTY_SET | DIRTY_TEXTURES) || c->st.program != p || !c->st.set) {
            if (!write_set(c, p))
                return false;
            if (c->batch != b) {
                c->st.uniforms_placed = false;
                c->st.dirty |= DIRTY_SET;
                continue;
            }
            c->st.dirty |= DIRTY_SET;
        }
        if (c->batch == b)
            return true;
        c->st.uniforms_placed = false;
        c->st.dirty |= DIRTY_SET;
    }
    return false;
}

static bool flush_state(Channel *c)
{
    Pipeline *pl = c->st.pipeline;
    if (!pl || !pl->valid || !pl->program || !c->pass_live)
        return false;
    bool clamp = !(c->st.raster.depth_clip & 1);
    VkPipeline pipeline = pipeline_variant(pl, c->st.raster.fill, clamp, c->st.raster.zero_to_one);
    if (!pipeline)
        return false;
    if (!c->rendering) {
        /* allocates resources before opening rendering in their final batch. */
        c->st.set = VK_NULL_HANDLE;
        if (!place_resources(c, pl->program) || !begin_rendering(c))
            return false;
        c->st.dirty |= DIRTY_SET;
    }
    if (!place_resources(c, pl->program) || !c->rendering)
        return false;
    VkCommandBuffer cb = cbuf(c);
    uint32_t d = c->st.dirty;
    if (pipeline != c->st.bound) {
        g_vk.fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        c->st.bound = pipeline;
        c->st.program = pl->program;
        d |= DIRTY_SET;
    }
    xfb_enter(c);
    if (d & (DIRTY_DEPTH | DIRTY_PIPELINE)) {
        const MimeGpuDepthStencilCreate *ds = &c->st.depth;
        bool has_depth = c->pass_depth != VK_FORMAT_UNDEFINED, has_stencil = c->pass_stencil != VK_FORMAT_UNDEFINED;
        g_vk.fn.vkCmdSetDepthTestEnable(cb, has_depth && c->st.has_depth);
        g_vk.fn.vkCmdSetDepthWriteEnable(cb, has_depth && c->st.has_depth && ds->depth_write);
        g_vk.fn.vkCmdSetDepthCompareOp(cb, c->st.has_depth ? (VkCompareOp)ds->depth_func : VK_COMPARE_OP_ALWAYS);
        bool stencil = has_stencil && c->st.has_depth && ds->stencil;
        g_vk.fn.vkCmdSetStencilTestEnable(cb, stencil);
        const MimeGpuStencilFace *faces[2] = {&ds->front, &ds->back};
        for (int f = 0; f < 2; f++) {
            VkStencilFaceFlags face = f ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT;
            const MimeGpuStencilFace *s = faces[f];
            g_vk.fn.vkCmdSetStencilOp(cb, face, (VkStencilOp)s->fail, (VkStencilOp)s->pass, (VkStencilOp)s->depth_fail,
                                      stencil ? (VkCompareOp)s->func : VK_COMPARE_OP_ALWAYS);
            g_vk.fn.vkCmdSetStencilCompareMask(cb, face, s->read_mask);
            g_vk.fn.vkCmdSetStencilWriteMask(cb, face, stencil ? s->write_mask : 0);
        }
        g_vk.fn.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_BIT, c->st.front_ref);
        g_vk.fn.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_BACK_BIT, c->st.back_ref);
    }
    if (d & DIRTY_RASTER) {
        const MimeGpuSetRaster *r = &c->st.raster;
        g_vk.fn.vkCmdSetCullMode(cb, (VkCullModeFlags)r->cull);
        /* both count windings on the framebuffer, where rows run the same way here. */
        g_vk.fn.vkCmdSetFrontFace(cb, r->winding ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
        g_vk.fn.vkCmdSetRasterizerDiscardEnable(cb, (r->depth_clip & 2) != 0);
        bool bias = r->depth_bias != 0.0f || r->slope_scale != 0.0f;
        g_vk.fn.vkCmdSetDepthBiasEnable(cb, bias);
        if (bias)
            g_vk.fn.vkCmdSetDepthBias(cb, r->depth_bias, r->bias_clamp, r->slope_scale);
        if (g_polygon_mode)
            g_vk.fn.vkCmdSetPolygonModeEXT(cb, r->fill ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);
        if (g_depth_clamp)
            g_vk.fn.vkCmdSetDepthClampEnableEXT(cb, clamp);
    }
    if (d & DIRTY_VIEWPORT) {
        VkViewport vs[MIME_GPU_MAX_VIEWPORTS];
        for (uint32_t i = 0; i < g_viewports; i++) {
            vs[i] = (c->st.viewport_own_mask & (1u << i)) ? c->st.viewport_own[i] : c->st.viewport;
            if (vs[i].width <= 0)
                vs[i].width = 1;
            if (vs[i].height == 0)
                vs[i].height = 1;
        }
        g_vk.fn.vkCmdSetViewport(cb, 0, g_viewports, vs);
    }
    if (d & DIRTY_SCISSOR) {
        VkRect2D rects[MIME_GPU_MAX_VIEWPORTS];
        for (uint32_t i = 0; i < g_viewports; i++) {
            bool own = (c->st.scissor_own_mask & (1u << i)) != 0;
            const MimeGpuRect *sc = own ? &c->st.scissor_own[i] : &c->st.scissor;
            int64_t x0 = 0, y0 = 0, x1 = c->pass.width, y1 = c->pass.height;
            if (own || c->st.has_scissor) {
                x0 = sc->x > 0 ? sc->x : 0;
                y0 = sc->y > 0 ? sc->y : 0;
                int64_t r = (int64_t)sc->x + sc->width, b = (int64_t)sc->y + sc->height;
                x1 = r < x1 ? r : x1;
                y1 = b < y1 ? b : y1;
            }
            rects[i] = (VkRect2D){{0, 0}, {0, 0}};
            if (x1 > x0 && y1 > y0)
                rects[i] = (VkRect2D){{(int32_t)x0, (int32_t)y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}};
        }
        g_vk.fn.vkCmdSetScissor(cb, 0, g_viewports, rects);
    }
    if (d & DIRTY_BLEND)
        g_vk.fn.vkCmdSetBlendConstants(cb, c->st.blend);
    if (d & DIRTY_VB) {
        for (uint32_t m = c->st.vb_mask, i; m; m &= m - 1) {
            i = (uint32_t)__builtin_ctz(m);
            uint64_t off;
            VkBuffer buf = heap(c, &c->st.vb[i], 0, &off);
            if (buf) {
                VkDeviceSize o = off;
                g_vk.fn.vkCmdBindVertexBuffers(cb, i, 1, &buf, &o);
            }
        }
    }
    Program *p = pl->program;
    if (d & (DIRTY_SET | DIRTY_UNIFORMS)) {
        uint32_t dynamic = c->st.uniform_offset;
        g_vk.fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, 1, &c->st.set,
                                        p->has_uniforms ? 1 : 0, &dynamic);
    }
    c->st.dirty = 0;
    return true;
}

/* ---------------------------------------------------------------- clears */

static VkPipeline clear_pipeline(Channel *c, uint32_t color_mask, bool depth, bool stencil)
{
    uint64_t key = (uint64_t)c->pass_depth | (uint64_t)c->pass_stencil << 12 | (uint64_t)c->pass_samples << 24 |
                   (uint64_t)color_mask << 32 | (uint64_t)depth << 40 | (uint64_t)stencil << 41;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        key ^= (uint64_t)c->pass_color[i] * (0x9E3779B97F4A7C15ull >> i);
    for (int i = 0; i < 16; i++)
        if (c->clear_pipelines[i] && c->clear_keys[i] == key)
            return c->clear_pipelines[i];
    if (!c->clear_layout) {
        VkPushConstantRange range = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
        VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 0, nullptr, 1, &range};
        if (g_vk.fn.vkCreatePipelineLayout(g_vk.device, &li, nullptr, &c->clear_layout) != VK_SUCCESS)
            return VK_NULL_HANDLE;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, g_clear_vs, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_clear_fs, "main"},
    };
    VkPipelineVertexInputStateCreateInfo vertex = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportDepthClipControlCreateInfoEXT clip = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT, nullptr, VK_FALSE};
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                  g_clip_control ? &clip : nullptr, 0, 1, nullptr, 1, nullptr};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0,
                                               (VkSampleCountFlagBits)c->pass_samples};
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend[MIME_GPU_MAX_COLORS] = {};
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        blend[i].colorWriteMask = c->pass_color[i] ? write_mask((uint8_t)color_mask) : 0;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
                                              VK_FALSE, VK_LOGIC_OP_COPY, MIME_GPU_MAX_COLORS, blend};
    VkDynamicState dyn[24];
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
                                                dynamic_count(), dynamic_states(dyn)};
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0,
                                               MIME_GPU_MAX_COLORS, c->pass_color, c->pass_depth, c->pass_stencil};
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dynamic,
        .layout = c->clear_layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (g_vk.fn.vkCreateGraphicsPipelines(g_vk.device, g_cache, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    int victim = (int)(key % 16);
    if (c->clear_pipelines[victim])
        defer(c, (Garbage){.pipeline = c->clear_pipelines[victim]});
    c->clear_pipelines[victim] = pipeline;
    c->clear_keys[victim] = key;
    return pipeline;
}

static void clear(Channel *c, const MimeGpuClear *cl)
{
    if (!c->pass_live)
        return;
    uint32_t mask = cl->mask;
    bool any_color = false;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        any_color |= c->pass_color[i] != VK_FORMAT_UNDEFINED;
    if (!any_color || !cl->color_mask)
        mask &= ~MIME_GPU_CLEAR_COLOR;
    if (!c->pass_depth)
        mask &= ~MIME_GPU_CLEAR_DEPTH;
    if (!c->pass_stencil)
        mask &= ~MIME_GPU_CLEAR_STENCIL;
    if (!mask)
        return;
    bool whole = !c->st.has_scissor || (c->st.scissor.x <= 0 && c->st.scissor.y <= 0 &&
                                        (int64_t)c->st.scissor.x + c->st.scissor.width >= c->pass.width &&
                                        (int64_t)c->st.scissor.y + c->st.scissor.height >= c->pass.height);
    bool full_color = !(mask & MIME_GPU_CLEAR_COLOR) || (cl->color_mask & 0xF) == 0xF;
    bool full_stencil = !(mask & MIME_GPU_CLEAR_STENCIL) || (cl->stencil_write_mask & 0xFF) == 0xFF;
    /* before rendering opens, a clear of whole attachments is their load action. */
    if (!c->rendering && whole && full_color && full_stencil) {
        for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
            if ((mask & MIME_GPU_CLEAR_COLOR) && c->pass.color[i].id) {
                c->pass.color[i].load = LOAD_CLEAR;
                memcpy(c->pass.clear_color[i], cl->color, sizeof(cl->color));
            }
        }
        if (mask & MIME_GPU_CLEAR_DEPTH) {
            c->pass.depth.load = LOAD_CLEAR;
            c->pass.clear_depth = cl->depth;
        }
        if (mask & MIME_GPU_CLEAR_STENCIL) {
            c->pass.stencil.load = LOAD_CLEAR;
            c->pass.clear_stencil = cl->stencil;
        }
        return;
    }
    if (!begin_rendering(c))
        return;
    VkCommandBuffer cb = cbuf(c);
    int64_t x0 = 0, y0 = 0, x1 = c->pass.width, y1 = c->pass.height;
    if (c->st.has_scissor) {
        x0 = c->st.scissor.x > 0 ? c->st.scissor.x : 0;
        y0 = c->st.scissor.y > 0 ? c->st.scissor.y : 0;
        int64_t r = (int64_t)c->st.scissor.x + c->st.scissor.width, b = (int64_t)c->st.scissor.y + c->st.scissor.height;
        x1 = r < x1 ? r : x1;
        y1 = b < y1 ? b : y1;
    }
    if (x1 <= x0 || y1 <= y0)
        return;
    VkClearRect rect = {{{(int32_t)x0, (int32_t)y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}}, 0, 1};
    if (full_color && full_stencil) {
        VkClearAttachment atts[ATTACHMENTS];
        uint32_t n = 0;
        if (mask & MIME_GPU_CLEAR_COLOR)
            for (uint32_t i = 0; i < MIME_GPU_MAX_COLORS; i++)
                if (c->pass_color[i]) {
                    atts[n] = (VkClearAttachment){VK_IMAGE_ASPECT_COLOR_BIT, i};
                    memcpy(atts[n++].clearValue.color.float32, cl->color, sizeof(cl->color));
                }
        VkImageAspectFlags ds = ((mask & MIME_GPU_CLEAR_DEPTH) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                                ((mask & MIME_GPU_CLEAR_STENCIL) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        if (ds) {
            atts[n] = (VkClearAttachment){ds, 0};
            atts[n].clearValue.depthStencil = (VkClearDepthStencilValue){cl->depth, cl->stencil};
            n++;
        }
        if (n)
            g_vk.fn.vkCmdClearAttachments(cb, n, atts, 1, &rect);
        return;
    }
    /* a masked clear draws through the scissor. */
    VkPipeline ps = clear_pipeline(c, (mask & MIME_GPU_CLEAR_COLOR) ? cl->color_mask : 0, (mask & MIME_GPU_CLEAR_DEPTH) != 0,
                                   (mask & MIME_GPU_CLEAR_STENCIL) != 0);
    if (!ps)
        return;
    g_vk.fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ps);
    c->st.bound = VK_NULL_HANDLE;
    VkViewport vp = {0, 0, (float)c->pass.width, (float)c->pass.height, 0, 1};
    g_vk.fn.vkCmdSetViewport(cb, 0, 1, &vp);
    g_vk.fn.vkCmdSetScissor(cb, 0, 1, &rect.rect);
    g_vk.fn.vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
    g_vk.fn.vkCmdSetFrontFace(cb, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    g_vk.fn.vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    g_vk.fn.vkCmdSetPrimitiveRestartEnable(cb, VK_FALSE);
    g_vk.fn.vkCmdSetDepthBiasEnable(cb, VK_FALSE);
    g_vk.fn.vkCmdSetDepthTestEnable(cb, (mask & MIME_GPU_CLEAR_DEPTH) != 0);
    g_vk.fn.vkCmdSetDepthWriteEnable(cb, (mask & MIME_GPU_CLEAR_DEPTH) != 0);
    g_vk.fn.vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_ALWAYS);
    g_vk.fn.vkCmdSetStencilTestEnable(cb, (mask & MIME_GPU_CLEAR_STENCIL) != 0);
    g_vk.fn.vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_REPLACE,
                              VK_STENCIL_OP_REPLACE, VK_COMPARE_OP_ALWAYS);
    g_vk.fn.vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
    g_vk.fn.vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, cl->stencil_write_mask);
    g_vk.fn.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, cl->stencil);
    if (g_polygon_mode)
        g_vk.fn.vkCmdSetPolygonModeEXT(cb, VK_POLYGON_MODE_FILL);
    if (g_depth_clamp)
        g_vk.fn.vkCmdSetDepthClampEnableEXT(cb, VK_FALSE);
    float push[8] = {cl->color[0], cl->color[1], cl->color[2], cl->color[3], cl->depth};
    g_vk.fn.vkCmdPushConstants(cb, c->clear_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(push), push);
    g_vk.fn.vkCmdDraw(cb, 3, 1, 0, 0);
    c->st.dirty = DIRTY_ALL;
}

/* ---------------------------------------------------------------- blits */

static void blit(Channel *c, const MimeGpuBlit *b)
{
    end_rendering(c);
    bool depth = (b->mask & (MIME_GPU_CLEAR_DEPTH | MIME_GPU_CLEAR_STENCIL)) != 0;
    Texture *src = texture_of(c, b->src.id, b->src.surface, depth), *dst = texture_of(c, b->dst.id, b->dst.surface, depth);
    if (!src || !dst || b->src.level >= src->levels || b->dst.level >= dst->levels)
        return;
    use_surface(c, b->src.id, b->src.surface);
    use_surface(c, b->dst.id, b->dst.surface);
    barrier(c);
    int32_t sw = b->src_x1 - b->src_x0, sh = b->src_y1 - b->src_y0;
    int32_t dw = b->dst_x1 - b->dst_x0, dh = b->dst_y1 - b->dst_y0;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (depth) {
        aspect = ((b->mask & MIME_GPU_CLEAR_DEPTH) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                 ((b->mask & MIME_GPU_CLEAR_STENCIL) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        aspect &= src->aspect & dst->aspect;
        if (!aspect)
            return;
    }
    VkImageSubresourceLayers ss = {aspect, b->src.level, b->src.slice, 1}, ds = {aspect, b->dst.level, b->dst.slice, 1};
    VkCommandBuffer cb = cbuf(c);
    if (sw == dw && sh == dh && sw > 0 && sh > 0 && src->samples == dst->samples && src->format == dst->format) {
        VkImageCopy region = {ss, {b->src_x0, b->src_y0, 0}, ds, {b->dst_x0, b->dst_y0, 0}, {(uint32_t)sw, (uint32_t)sh, 1}};
        g_vk.fn.vkCmdCopyImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    } else if (src->samples > 1 && dst->samples == 1 && !depth) {
        /* resolves the entire source into an equally sized destination. */
        int32_t w = (int32_t)(src->width < dst->width ? src->width : dst->width);
        int32_t h = (int32_t)(src->height < dst->height ? src->height : dst->height);
        if (src->format == dst->format) {
            VkImageResolve region = {ss, {0, 0, 0}, ds, {0, 0, 0}, {(uint32_t)w, (uint32_t)h, 1}};
            g_vk.fn.vkCmdResolveImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                      &region);
        } else {
            /*
             * Vulkan requires matching resolve formats; resolve to a temporary source-format image, then convert
             * with a blit.
             */
            Texture tmp;
            if (!texture_make(&tmp, TEXTURE_2D, src->mtl_format, (uint32_t)w, (uint32_t)h, 1, 1, 1, false))
                return;
            texture_init_layout(c, &tmp);
            VkImageSubresourceLayers whole = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            VkImageResolve region = {ss, {0, 0, 0}, whole, {0, 0, 0}, {(uint32_t)w, (uint32_t)h, 1}};
            g_vk.fn.vkCmdResolveImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, tmp.image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                      &region);
            c->wrote = true;
            barrier(c);
            VkImageBlit copy = {whole, {{0, 0, 0}, {w, h, 1}}, ds, {{0, 0, 0}, {w, h, 1}}};
            g_vk.fn.vkCmdBlitImage(cb, tmp.image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy,
                                   VK_FILTER_NEAREST);
            texture_free(c, &tmp);
        }
    } else if (src->samples == 1 && dst->samples == 1) {
        /* a stretch, mirrored when the rectangles run opposite ways. */
        VkImageBlit region = {ss, {{b->src_x0, b->src_y0, 0}, {b->src_x1, b->src_y1, 1}}, ds,
                              {{b->dst_x0, b->dst_y0, 0}, {b->dst_x1, b->dst_y1, 1}}};
        g_vk.fn.vkCmdBlitImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region,
                               depth || !b->linear ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    } else {
        return;
    }
    c->wrote = true;
}

/* ---------------------------------------------------------------- execution */

static bool draw_ready(Channel *c)
{
    if (!c->pass_live || !c->st.pipeline)
        return false;
    /* reacquires sampled surfaces from the presenter before rendering begins. */
    for (int s = 0; s < MIME_GPU_STAGES; s++)
        for (uint32_t m = c->st.tex_mask[s], i; m; m &= m - 1) {
            i = (uint32_t)__builtin_ctz(m);
            Binding *b = &c->st.tex[s][i];
            if (b->surface && b->id < c->surfaces_cap && c->surfaces[b->id].presented) {
                end_rendering(c);
                use_surface(c, b->id, true);
            }
        }
    return flush_state(c);
}

static void set_topology(Channel *c, uint32_t primitive)
{
    VkCommandBuffer cb = cbuf(c);
    VkPrimitiveTopology t = (VkPrimitiveTopology)(primitive <= 4 ? primitive : primitive <= 10 ? primitive : 3);
    g_vk.fn.vkCmdSetPrimitiveTopology(cb, t);
    /* Metal restarts strips at the largest index, always. */
    g_vk.fn.vkCmdSetPrimitiveRestartEnable(cb, t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
                                                   t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                                                   t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY ||
                                                   t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY);
}

static void draw(Channel *c, const MimeGpuDraw *d)
{
    if (!d->count || !draw_ready(c))
        return;
    set_topology(c, d->primitive);
    g_vk.fn.vkCmdDraw(cbuf(c), d->count, d->instances ? d->instances : 1, d->first, d->base_instance);
}

static void draw_indexed(Channel *c, const MimeGpuDrawIndexed *d)
{
    if (!d->count || !draw_ready(c))
        return;
    uint32_t size = d->index_type ? 4 : 2;
    uint64_t off;
    VkBuffer buf = heap(c, &d->indices, (uint64_t)d->count * size, &off);
    if (!buf)
        return;
    set_topology(c, d->primitive);
    /* the offset of an index buffer is a multiple of its index size; the first index takes the rest. */
    uint64_t base = off & ~(uint64_t)(size - 1);
    g_vk.fn.vkCmdBindIndexBuffer(cbuf(c), buf, base, d->index_type ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    g_vk.fn.vkCmdDrawIndexed(cbuf(c), d->count, d->instances ? d->instances : 1, (uint32_t)((off - base) / size),
                             d->base_vertex, d->base_instance);
}


/* ---------------------------------------------------------------- queries and transform feedback */

/*
 * a query or transform feedback the guest has begun lives across the host's rendering instances: each one ends it when
 * it ends and begins it again when it resumes. a query gets a slot of the pool for each stretch, and the guest's result
 * is their sum, written once the batch of the last stretch has completed. transform feedback ends the same way, saving
 * the byte counters of its buffers, and resumes from them.
 */
#define QUERY_SLOTS 2048

typedef struct HostQuery {
    bool used, active, inside;
    uint32_t type, stream;
    uint32_t *slots;
    uint32_t slot_count, slot_cap;
} HostQuery;

typedef struct QueryDone {
    uint64_t value; /* the batch whose completion makes the result final. */
    uint32_t type, kind;
    uint32_t *slots;
    uint32_t slot_count;
    MimeGpuMem result;
} QueryDone;

/* 0 occlusion, 1 captured vertices, 2 generated primitives: Vulkan allows one active query of a type and stream. */
static int query_kind(uint32_t type)
{
    return type == MIME_GPU_QUERY_SAMPLES || type == MIME_GPU_QUERY_ANY_SAMPLES ? 0
           : type == MIME_GPU_QUERY_PRIMITIVES && g_generated_query                ? 2
                                                                                   : 1;
}

static bool query_pool_ready(Channel *c, int kind)
{
    if (c->query_pool[kind])
        return true;
    VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0,
                                kind == 2   ? VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT
                                : kind == 1 ? VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT
                                            : VK_QUERY_TYPE_OCCLUSION,
                                QUERY_SLOTS};
    if (!g_queries || (kind && !g_xfb) ||
        g_vk.fn.vkCreateQueryPool(g_vk.device, &qi, nullptr, &c->query_pool[kind]) != VK_SUCCESS) {
        c->query_pool[kind] = VK_NULL_HANDLE;
        return false;
    }
    c->query_free[kind] = malloc(QUERY_SLOTS * sizeof(uint32_t));
    for (uint32_t i = 0; i < QUERY_SLOTS; i++)
        c->query_free[kind][i] = QUERY_SLOTS - 1 - i;
    c->query_free_count[kind] = QUERY_SLOTS;
    return true;
}

static HostQuery *query_of(Channel *c, uint32_t id)
{
    return SLOT(c, queries, id);
}

/* begins a slot inside the rendering that is open. */
static void query_enter(Channel *c, uint32_t id)
{
    HostQuery *q = query_of(c, id);
    int kind = query_kind(q->type);
    if (q->inside || !c->rendering || !query_pool_ready(c, kind))
        return;
    pthread_mutex_lock(&c->query_lock);
    uint32_t slot = c->query_free_count[kind] ? c->query_free[kind][--c->query_free_count[kind]] : UINT32_MAX;
    pthread_mutex_unlock(&c->query_lock);
    if (slot == UINT32_MAX)
        return;
    g_vk.fn.vkResetQueryPool(g_vk.device, c->query_pool[kind], slot, 1);
    if (kind != 1)
        g_vk.fn.vkCmdBeginQuery(cbuf(c), c->query_pool[kind], slot,
                                q->type == MIME_GPU_QUERY_SAMPLES ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
    else
        g_vk.fn.vkCmdBeginQueryIndexedEXT(cbuf(c), c->query_pool[kind], slot, 0, q->stream);
    if (q->slot_count == q->slot_cap) {
        q->slot_cap = q->slot_cap ? q->slot_cap * 2 : 8;
        q->slots = realloc(q->slots, q->slot_cap * sizeof(uint32_t));
    }
    q->slots[q->slot_count++] = slot;
    q->inside = true;
}

static void query_leave(Channel *c, uint32_t id)
{
    HostQuery *q = query_of(c, id);
    if (!q->inside)
        return;
    int kind = query_kind(q->type);
    uint32_t slot = q->slots[q->slot_count - 1];
    if (kind != 1)
        g_vk.fn.vkCmdEndQuery(cbuf(c), c->query_pool[kind], slot);
    else
        g_vk.fn.vkCmdEndQueryIndexedEXT(cbuf(c), c->query_pool[kind], slot, q->stream);
    q->inside = false;
}

static void xfb_leave(Channel *c)
{
    if (!c->xfb.inside)
        return;
    VkBuffer counters[MIME_GPU_MAX_XFB_BUFFERS];
    VkDeviceSize offsets[MIME_GPU_MAX_XFB_BUFFERS];
    for (uint32_t i = 0; i < c->xfb.count; i++) {
        counters[i] = c->xfb.counters;
        offsets[i] = ((VkDeviceSize)c->xfb.object * MIME_GPU_MAX_XFB_BUFFERS + i) * 16;
    }
    g_vk.fn.vkCmdEndTransformFeedbackEXT(cbuf(c), 0, c->xfb.count, counters, offsets);
    c->xfb.inside = false;
    c->xfb.has_counter = true;
}

static void scopes_suspend(Channel *c)
{
    for (uint32_t i = 0; i < c->active_query_count; i++)
        query_leave(c, c->active_queries[i]);
    xfb_leave(c);
}

static void scopes_resume(Channel *c)
{
    for (uint32_t i = 0; i < c->active_query_count; i++)
        query_enter(c, c->active_queries[i]);
}

static bool xfb_counters_ready(Channel *c)
{
    if (c->xfb.counters)
        return true;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 256ull * MIME_GPU_MAX_XFB_BUFFERS * 16,
                             VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT, VK_SHARING_MODE_EXCLUSIVE};
    if (g_vk.fn.vkCreateBuffer(g_vk.device, &bi, nullptr, &c->xfb.counters) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    g_vk.fn.vkGetBufferMemoryRequirements(g_vk.device, c->xfb.counters, &req);
    int32_t t = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0)
        t = memory_type(req.memoryTypeBits, 0);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)t};
    if (t < 0 || g_vk.fn.vkAllocateMemory(g_vk.device, &ai, nullptr, &c->xfb.counters_memory) != VK_SUCCESS ||
        g_vk.fn.vkBindBufferMemory(g_vk.device, c->xfb.counters, c->xfb.counters_memory, 0) != VK_SUCCESS) {
        g_vk.fn.vkDestroyBuffer(g_vk.device, c->xfb.counters, nullptr);
        c->xfb.counters = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* the first draw after a begin or a resume starts the capture, inside the rendering and after the pipeline is bound. */
static void xfb_enter(Channel *c)
{
    if (!c->xfb.requested || c->xfb.inside || !g_xfb || !xfb_counters_ready(c))
        return;
    VkBuffer buffers[MIME_GPU_MAX_XFB_BUFFERS], counters[MIME_GPU_MAX_XFB_BUFFERS];
    VkDeviceSize offsets[MIME_GPU_MAX_XFB_BUFFERS], sizes[MIME_GPU_MAX_XFB_BUFFERS],
        counter_offsets[MIME_GPU_MAX_XFB_BUFFERS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->xfb.count; i++) {
        uint64_t off = 0;
        VkBuffer b = c->xfb.buffer[i].size ? heap(c, &c->xfb.buffer[i].mem, c->xfb.buffer[i].size, &off) : VK_NULL_HANDLE;
        if (!b) {
            /* an empty binding still needs a buffer: the first block, one word of it. */
            b = c->blocks[0];
            off = 0;
            sizes[i] = 4;
        } else {
            sizes[i] = c->xfb.buffer[i].size;
        }
        buffers[i] = b;
        offsets[i] = off;
        counters[i] = c->xfb.has_counter ? c->xfb.counters : VK_NULL_HANDLE;
        counter_offsets[i] = ((VkDeviceSize)c->xfb.object * MIME_GPU_MAX_XFB_BUFFERS + i) * 16;
        n++;
    }
    if (!n)
        return;
    g_vk.fn.vkCmdBindTransformFeedbackBuffersEXT(cbuf(c), 0, n, buffers, offsets, sizes);
    g_vk.fn.vkCmdBeginTransformFeedbackEXT(cbuf(c), 0, n, c->xfb.has_counter ? counters : nullptr,
                                           c->xfb.has_counter ? counter_offsets : nullptr);
    c->xfb.inside = true;
    c->wrote = true;
}

static void xfb_buffers(Channel *c, const MimeGpuXfbBuffers *b)
{
    c->xfb.count = b->count > MIME_GPU_MAX_XFB_BUFFERS ? MIME_GPU_MAX_XFB_BUFFERS : b->count;
    for (uint32_t i = 0; i < MIME_GPU_MAX_XFB_BUFFERS; i++) {
        c->xfb.buffer[i].mem = b->buffer[i].mem;
        c->xfb.buffer[i].size = b->buffer[i].size;
    }
}

static void xfb_control(Channel *c, const MimeGpuXfbControl *x)
{
    switch (x->action) {
    case MIME_GPU_XFB_BEGIN:
        c->xfb.object = x->object < 256 ? x->object : 0;
        c->xfb.has_counter = false;
        c->xfb.requested = true;
        break;
    case MIME_GPU_XFB_RESUME:
        c->xfb.object = x->object < 256 ? x->object : 0;
        c->xfb.requested = true;
        break;
    case MIME_GPU_XFB_PAUSE:
    case MIME_GPU_XFB_END:
        c->xfb.requested = false;
        /* what was captured is read by later draws and copies: the rendering ends so a barrier can separate them. */
        if (c->xfb.inside)
            end_rendering(c);
        break;
    }
}

static void query_begin(Channel *c, const MimeGpuQuery *m)
{
    HostQuery *q = query_of(c, m->id);
    q->used = true;
    q->active = true;
    q->inside = false;
    q->type = m->type;
    q->stream = m->stream;
    q->slot_count = 0;
    if (c->active_query_count < 16)
        c->active_queries[c->active_query_count++] = m->id;
    if (c->rendering)
        query_enter(c, m->id);
}

static void query_write(Channel *c, const MimeGpuMem *mem, uint64_t value)
{
    uint64_t *p = heap_ptr(c, mem, 16);
    if (!p)
        return;
    p[0] = value;
    __atomic_store_n(&p[1], 1, __ATOMIC_RELEASE);
}

static void query_end(Channel *c, const MimeGpuQuery *m)
{
    HostQuery *q = query_of(c, m->id);
    if (!q->active)
        return;
    query_leave(c, m->id);
    q->active = false;
    for (uint32_t i = 0; i < c->active_query_count; i++) {
        if (c->active_queries[i] == m->id) {
            c->active_queries[i] = c->active_queries[--c->active_query_count];
            break;
        }
    }
    if (!q->slot_count) {
        query_write(c, &m->result, 0);
        return;
    }
    /* the slots are read once the batch holding the last stretch has completed. */
    pthread_mutex_lock(&c->query_lock);
    if (c->query_done_count == c->query_done_cap) {
        c->query_done_cap = c->query_done_cap ? c->query_done_cap * 2 : 16;
        c->query_done = realloc(c->query_done, c->query_done_cap * sizeof(QueryDone));
    }
    c->query_done[c->query_done_count++] = (QueryDone){c->submitted + 1, q->type, (uint32_t)query_kind(q->type), q->slots,
                                                       q->slot_count, m->result};
    pthread_mutex_unlock(&c->query_lock);
    q->slots = nullptr;
    q->slot_cap = q->slot_count = 0;
}

/* sums the slots of every query that is final and hands the result to the guest. */
static void queries_finish(Channel *c, uint64_t completed)
{
    for (;;) {
        QueryDone d;
        pthread_mutex_lock(&c->query_lock);
        uint32_t i = 0;
        while (i < c->query_done_count && c->query_done[i].value > completed)
            i++;
        if (i == c->query_done_count) {
            pthread_mutex_unlock(&c->query_lock);
            return;
        }
        d = c->query_done[i];
        c->query_done[i] = c->query_done[--c->query_done_count];
        pthread_mutex_unlock(&c->query_lock);
        uint64_t sum = 0;
        for (uint32_t k = 0; k < d.slot_count; k++) {
            uint64_t r[2] = {0, 0};
            g_vk.fn.vkGetQueryPoolResults(g_vk.device, c->query_pool[d.kind], d.slots[k], 1, sizeof(r), r, sizeof(r),
                                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            /* a feedback stream query answers written, then needed. */
            if (d.type == MIME_GPU_QUERY_OVERFLOW)
                sum |= r[1] > r[0];
            else
                sum += d.type == MIME_GPU_QUERY_PRIMITIVES && d.kind == 1 ? r[1] : r[0];
        }
        if (d.type == MIME_GPU_QUERY_ANY_SAMPLES)
            sum = sum != 0;
        query_write(c, &d.result, sum);
        pthread_mutex_lock(&c->query_lock);
        for (uint32_t k = 0; k < d.slot_count; k++)
            c->query_free[d.kind][c->query_free_count[d.kind]++] = d.slots[k];
        pthread_mutex_unlock(&c->query_lock);
        free(d.slots);
    }
}

static void draw_xfb(Channel *c, const MimeGpuDrawXfb *d)
{
    if (!g_xfb || !xfb_counters_ready(c) || !draw_ready(c))
        return;
    set_topology(c, d->primitive);
    VkDeviceSize offset = ((VkDeviceSize)(d->object < 256 ? d->object : 0) * MIME_GPU_MAX_XFB_BUFFERS +
                           (d->buffer < MIME_GPU_MAX_XFB_BUFFERS ? d->buffer : 0)) * 16;
    g_vk.fn.vkCmdDrawIndirectByteCountEXT(cbuf(c), d->instances ? d->instances : 1, 0, c->xfb.counters, offset, 0,
                                          d->stride ? d->stride : 4);
}

/* compute and the draws that follow it share nothing but the batch: the pass resumes at the next draw. */
static void dispatch(Channel *c, const MimeGpuDispatch *d)
{
    Program *p = program_ready(c, d->program);
    if (!p || !p->compute || !p->compute_pipeline || !d->x || !d->y || !d->z)
        return;
    end_rendering(c);
    barrier(c);
    c->st.set = VK_NULL_HANDLE;
    c->st.program = nullptr;
    if (!place_resources(c, p))
        return;
    VkCommandBuffer cb = cbuf(c);
    g_vk.fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->compute_pipeline);
    uint32_t dynamic = c->st.uniform_offset;
    g_vk.fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &c->st.set,
                                    p->has_uniforms ? 1 : 0, &dynamic);
    g_vk.fn.vkCmdDispatch(cb, d->x, d->y, d->z);
    c->wrote = true;
    /* the set belongs to the compute program: the next draw writes its own. */
    c->st.set = VK_NULL_HANDLE;
    c->st.program = nullptr;
    c->st.uniforms_placed = false;
    c->st.dirty |= DIRTY_SET | DIRTY_UNIFORMS;
}

static void copy_image(Channel *c, const MimeGpuCopyImage *ci)
{
    end_rendering(c);
    Texture *src = texture_of(c, ci->src, false, false), *dst = texture_of(c, ci->dst, false, false);
    if (!src || !dst || ci->src_level >= src->levels || ci->dst_level >= dst->levels || src->samples != dst->samples ||
        src->format != dst->format)
        return;
    barrier(c);
    bool s3 = src->type == TEXTURE_3D, d3 = dst->type == TEXTURE_3D;
    /* layers of an array or cube are array layers; a 3d image counts its slices in the extent. */
    VkImageCopy region = {
        {src->aspect, ci->src_level, s3 ? 0 : ci->src_z, s3 ? 1 : (d3 ? ci->depth : ci->depth)},
        {(int32_t)ci->src_x, (int32_t)ci->src_y, s3 ? (int32_t)ci->src_z : 0},
        {dst->aspect, ci->dst_level, d3 ? 0 : ci->dst_z, d3 ? 1 : ci->depth},
        {(int32_t)ci->dst_x, (int32_t)ci->dst_y, d3 ? (int32_t)ci->dst_z : 0},
        {ci->width, ci->height, s3 || d3 ? ci->depth : 1},
    };
    g_vk.fn.vkCmdCopyImage(cbuf(c), src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    c->wrote = true;
}

/* glMemoryBarrier: every write reaches every later access, inside passes too, so the pass ends and resumes. */
static void memory_barrier(Channel *c)
{
    end_rendering(c);
    c->wrote = true;
    barrier(c);
}

#define BODY(type) ((const type *)cmd)
#define NEED(type)                                                                                                     \
    if (cmd->size < sizeof(type)) {                                                                                    \
        lose(c, "short record");                                                                                       \
        return;                                                                                                        \
    }

static void execute(Channel *c, const MimeGpuCmd *cmd)
{
    switch (cmd->op) {
    case MIME_GPU_NOP:
        break;
    case MIME_GPU_FENCE:
        NEED(MimeGpuFence);
        commit(c, BODY(MimeGpuFence)->value);
        break;
    case MIME_GPU_FLUSH:
        commit(c, 0);
        break;
    case MIME_GPU_TEXTURE_CREATE:
        NEED(MimeGpuTextureCreate);
        texture_create(c, BODY(MimeGpuTextureCreate));
        break;
    case MIME_GPU_TEXTURE_DESTROY:
        NEED(MimeGpuObject);
        if (BODY(MimeGpuObject)->id < c->textures_cap) {
            if (in_pass(c, BODY(MimeGpuObject)->id, false))
                end_rendering(c);
            texture_free(c, &c->textures[BODY(MimeGpuObject)->id]);
        }
        break;
    case MIME_GPU_TEXTURE_SWIZZLE:
        NEED(MimeGpuTextureSwizzle);
        texture_swizzle(c, BODY(MimeGpuTextureSwizzle)->id, BODY(MimeGpuTextureSwizzle)->swizzle);
        break;
    case MIME_GPU_TEXTURE_UPLOAD:
    case MIME_GPU_TEXTURE_READ:
        NEED(MimeGpuTextureCopy);
        texture_copy(c, BODY(MimeGpuTextureCopy), cmd->op == MIME_GPU_TEXTURE_UPLOAD);
        break;
    case MIME_GPU_TEXTURE_MIPMAPS:
        NEED(MimeGpuObject);
        texture_mipmaps(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_BUFFER_COPY: {
        NEED(MimeGpuBufferCopy);
        const MimeGpuBufferCopy *b = BODY(MimeGpuBufferCopy);
        uint64_t so, dof;
        VkBuffer src = heap(c, &b->src, b->size, &so), dst = heap(c, &b->dst, b->size, &dof);
        if (src && dst && b->size) {
            end_rendering(c);
            barrier(c);
            VkBufferCopy region = {so, dof, b->size};
            g_vk.fn.vkCmdCopyBuffer(cbuf(c), src, dst, 1, &region);
            c->wrote = true;
        }
        break;
    }
    case MIME_GPU_PROGRAM_CREATE:
        NEED(MimeGpuProgramCreate);
        program_create(c, BODY(MimeGpuProgramCreate), false);
        break;
    case MIME_GPU_PROGRAM_STAGES:
        NEED(MimeGpuProgramStages);
        program_stages(c, BODY(MimeGpuProgramStages));
        break;
    case MIME_GPU_PROGRAM_DESTROY:
        NEED(MimeGpuObject);
        program_destroy(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_PIPELINE_CREATE:
        NEED(MimeGpuPipelineCreate);
        pipeline_create(c, BODY(MimeGpuPipelineCreate));
        break;
    case MIME_GPU_DEPTH_STENCIL_CREATE:
        NEED(MimeGpuDepthStencilCreate);
        *SLOT(c, depth_states, BODY(MimeGpuDepthStencilCreate)->id) = *BODY(MimeGpuDepthStencilCreate);
        break;
    case MIME_GPU_SAMPLER_CREATE:
        NEED(MimeGpuSamplerCreate);
        sampler_create(c, BODY(MimeGpuSamplerCreate));
        break;
    case MIME_GPU_SURFACE_CONFIG:
        NEED(MimeGpuSurfaceConfig);
        surface_config(c, BODY(MimeGpuSurfaceConfig));
        break;
    case MIME_GPU_SURFACE_DESTROY:
        NEED(MimeGpuObject);
        surface_destroy(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_PRESENT:
        NEED(MimeGpuObject);
        present(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_PASS_BEGIN:
        NEED(MimeGpuPass);
        pass_begin(c, BODY(MimeGpuPass));
        break;
    case MIME_GPU_PASS_END:
        end_rendering(c);
        c->pass_live = false;
        break;
    case MIME_GPU_CLEAR:
        NEED(MimeGpuClear);
        clear(c, BODY(MimeGpuClear));
        break;
    case MIME_GPU_BLIT:
        NEED(MimeGpuBlit);
        blit(c, BODY(MimeGpuBlit));
        break;
    case MIME_GPU_SET_PIPELINE: {
        NEED(MimeGpuObject);
        uint32_t i = BODY(MimeGpuObject)->id;
        c->st.pipeline = i < c->pipelines_cap ? &c->pipelines[i] : nullptr;
        c->st.dirty |= DIRTY_PIPELINE;
        break;
    }
    case MIME_GPU_SET_DEPTH_STENCIL: {
        NEED(MimeGpuSetDepthStencil);
        const MimeGpuSetDepthStencil *s = BODY(MimeGpuSetDepthStencil);
        c->st.has_depth = s->id < c->depth_states_cap;
        if (c->st.has_depth)
            c->st.depth = c->depth_states[s->id];
        c->st.front_ref = s->front_ref;
        c->st.back_ref = s->back_ref;
        c->st.dirty |= DIRTY_DEPTH;
        break;
    }
    case MIME_GPU_SET_RASTER:
        NEED(MimeGpuSetRaster);
        c->st.raster = *BODY(MimeGpuSetRaster);
        c->st.dirty |= DIRTY_RASTER;
        break;
    case MIME_GPU_SET_VIEWPORT: {
        NEED(MimeGpuSetViewport);
        const MimeGpuSetViewport *v = BODY(MimeGpuSetViewport);
        c->st.viewport = (VkViewport){v->x, v->y, v->width, v->height, v->znear, v->zfar};
        c->st.dirty |= DIRTY_VIEWPORT;
        break;
    }
    case MIME_GPU_SET_VIEWPORT_INDEXED: {
        NEED(MimeGpuSetViewportIndexed);
        const MimeGpuSetViewportIndexed *v = BODY(MimeGpuSetViewportIndexed);
        if (v->index < MIME_GPU_MAX_VIEWPORTS) {
            c->st.viewport_own[v->index] = (VkViewport){v->x, v->y, v->width, v->height, v->znear, v->zfar};
            c->st.viewport_own_mask |= 1u << v->index;
            c->st.dirty |= DIRTY_VIEWPORT;
        }
        break;
    }
    case MIME_GPU_SET_SCISSOR_INDEXED: {
        NEED(MimeGpuSetScissorIndexed);
        const MimeGpuSetScissorIndexed *v = BODY(MimeGpuSetScissorIndexed);
        if (v->index < MIME_GPU_MAX_VIEWPORTS) {
            c->st.scissor_own[v->index] = (MimeGpuRect){{}, v->x, v->y, v->width, v->height};
            c->st.scissor_own_mask |= 1u << v->index;
            c->st.dirty |= DIRTY_SCISSOR;
        }
        break;
    }
    case MIME_GPU_SET_SCISSOR:
        NEED(MimeGpuRect);
        c->st.scissor = *BODY(MimeGpuRect);
        c->st.has_scissor = true;
        c->st.dirty |= DIRTY_SCISSOR;
        break;
    case MIME_GPU_SET_BLEND_COLOR:
        NEED(MimeGpuSetBlendColor);
        memcpy(c->st.blend, BODY(MimeGpuSetBlendColor)->rgba, sizeof(c->st.blend));
        c->st.dirty |= DIRTY_BLEND;
        break;
    case MIME_GPU_SET_VERTEX_BUFFER: {
        NEED(MimeGpuSetVertexBuffer);
        const MimeGpuSetVertexBuffer *v = BODY(MimeGpuSetVertexBuffer);
        if (v->slot < MIME_GPU_MAX_ATTRIBS) {
            c->st.vb[v->slot] = v->mem;
            c->st.vb_mask |= 1u << v->slot;
            c->st.dirty |= DIRTY_VB;
        }
        break;
    }
    case MIME_GPU_SET_UNIFORMS: {
        NEED(MimeGpuSetUniforms);
        const MimeGpuSetUniforms *u = BODY(MimeGpuSetUniforms);
        uint32_t n = u->bytes;
        if (n > sizeof(c->st.uniforms) || n > cmd->size - sizeof(*u)) {
            lose(c, "uniform record");
            return;
        }
        memcpy(c->st.uniforms, u + 1, n);
        if (n != c->st.uniform_bytes)
            c->st.dirty |= DIRTY_SET; /* the dynamic buffer's range follows the size. */
        c->st.uniform_bytes = n;
        c->st.uniforms_placed = false;
        c->st.dirty |= DIRTY_UNIFORMS;
        break;
    }
    case MIME_GPU_SET_UNIFORM_BUFFER: {
        NEED(MimeGpuSetUniformBuffer);
        const MimeGpuSetUniformBuffer *binding = BODY(MimeGpuSetUniformBuffer);
        if (binding->slot >= MIME_GPU_MAX_UNIFORM_BLOCKS) {
            lose(c, "uniform buffer slot");
            return;
        }
        uint32_t stages = (binding->stages & 3) | ((binding->stages & ~3u) ? 1u : 0);
        for (int stage = 0; stage < MIME_GPU_STAGES; stage++) {
            if (!(stages & (1u << stage)))
                continue;
            c->st.uniform_buffers[stage][binding->slot] = binding->mem;
            c->st.uniform_buffer_mask[stage] |= 1u << binding->slot;
        }
        c->st.dirty |= DIRTY_SET;
        break;
    }
    case MIME_GPU_SET_TEXTURE: {
        NEED(MimeGpuSetTexture);
        const MimeGpuSetTexture *t = BODY(MimeGpuSetTexture);
        if (t->stage < MIME_GPU_STAGES && t->index < MIME_GPU_MAX_TEXTURES) {
            c->st.tex[t->stage][t->index] = (Binding){t->id, t->sampler, t->surface != 0};
            if (t->id)
                c->st.tex_mask[t->stage] |= 1u << t->index;
            else
                c->st.tex_mask[t->stage] &= ~(1u << t->index);
            c->st.dirty |= DIRTY_TEXTURES;
        }
        break;
    }
    case MIME_GPU_COMPUTE_PROGRAM_CREATE:
        NEED(MimeGpuProgramCreate);
        program_create(c, BODY(MimeGpuProgramCreate), true);
        break;
    case MIME_GPU_DISPATCH:
        NEED(MimeGpuDispatch);
        dispatch(c, BODY(MimeGpuDispatch));
        break;
    case MIME_GPU_SET_STORAGE_BUFFER: {
        NEED(MimeGpuSetStorageBuffer);
        const MimeGpuSetStorageBuffer *b = BODY(MimeGpuSetStorageBuffer);
        if (b->slot >= MIME_GPU_MAX_STORAGE_BLOCKS) {
            lose(c, "storage buffer slot");
            return;
        }
        c->st.storage[b->slot].mem = b->mem;
        c->st.storage[b->slot].size = b->size;
        c->st.dirty |= DIRTY_SET;
        break;
    }
    case MIME_GPU_SET_IMAGE: {
        NEED(MimeGpuSetImage);
        const MimeGpuSetImage *im = BODY(MimeGpuSetImage);
        if (im->slot >= MIME_GPU_MAX_IMAGES) {
            lose(c, "image slot");
            return;
        }
        c->st.images[im->slot] = *im;
        c->st.dirty |= DIRTY_SET;
        break;
    }
    case MIME_GPU_MEMORY_BARRIER:
        memory_barrier(c);
        break;
    case MIME_GPU_COPY_IMAGE:
        NEED(MimeGpuCopyImage);
        copy_image(c, BODY(MimeGpuCopyImage));
        break;
    case MIME_GPU_DRAW:
        NEED(MimeGpuDraw);
        draw(c, BODY(MimeGpuDraw));
        break;
    case MIME_GPU_DRAW_INDEXED:
        NEED(MimeGpuDrawIndexed);
        draw_indexed(c, BODY(MimeGpuDrawIndexed));
        break;
    case MIME_GPU_XFB_BUFFERS:
        NEED(MimeGpuXfbBuffers);
        xfb_buffers(c, BODY(MimeGpuXfbBuffers));
        break;
    case MIME_GPU_XFB_CONTROL:
        NEED(MimeGpuXfbControl);
        xfb_control(c, BODY(MimeGpuXfbControl));
        break;
    case MIME_GPU_QUERY_BEGIN:
        NEED(MimeGpuQuery);
        query_begin(c, BODY(MimeGpuQuery));
        break;
    case MIME_GPU_QUERY_END:
        NEED(MimeGpuQuery);
        query_end(c, BODY(MimeGpuQuery));
        break;
    case MIME_GPU_DRAW_XFB:
        NEED(MimeGpuDrawXfb);
        draw_xfb(c, BODY(MimeGpuDrawXfb));
        break;
    default:
        lose(c, "unknown command");
        break;
    }
}

/* spins a little before sleeping: a guest in the middle of a frame publishes again within microseconds. */
static bool wait_for_work(Channel *c, uint64_t get)
{
    /* submits pending work before sleeping unless the guest immediately supplies more. */
    if (c->batch) {
        double end = now_s() + 0.0005;
        while (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) == get && !c->stop && now_s() < end)
            cpu_relax();
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) != get || c->stop)
            return !c->stop;
        commit(c, 0);
    }
    for (int i = 0; i < 4000; i++) {
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) != get || c->stop)
            return !c->stop;
        cpu_relax();
    }
    pthread_mutex_lock(&c->lock);
    c->hdr->host_idle = 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    while (!c->stop && !c->kicked && __atomic_load_n(&c->hdr->put, __ATOMIC_SEQ_CST) == get) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&c->cond, &c->lock, &ts);
    }
    c->kicked = false;
    c->hdr->host_idle = 0;
    bool run = !c->stop;
    pthread_mutex_unlock(&c->lock);
    return run;
}

static void *channel_main(void *arg)
{
    Channel *c = arg;
    mime_set_thread_name("mime-gpu");
    uint64_t get = c->hdr->get;
    for (;;) {
        for (int n = 0; n < 4096; n++) {
            uint64_t put = __atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE);
            if (put == get || c->hdr->lost)
                break;
            uint64_t pos = get % MIME_GPU_RING_SIZE;
            const MimeGpuCmd *cmd = (const MimeGpuCmd *)(c->ring + pos);
            if (put - get > MIME_GPU_RING_SIZE || cmd->size < sizeof(MimeGpuCmd) || (cmd->size & 7) ||
                cmd->size > put - get || pos + cmd->size > MIME_GPU_RING_SIZE) {
                lose(c, "bad ring record");
                break;
            }
            execute(c, cmd);
            get += cmd->size;
            __atomic_store_n(&c->hdr->get, get, __ATOMIC_RELEASE);
        }
        if (c->hdr->lost) {
            /* nothing more runs: the guest sees lost and stops waiting. */
            __atomic_store_n(&c->hdr->get, __atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
            signal_fence(c, UINT64_MAX >> 1);
        }
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) == get && !wait_for_work(c, get))
            break;
    }
    commit(c, 0);
    return nullptr;
}

/* ---------------------------------------------------------------- hypercalls */

static bool add_block(Channel *c, uint32_t index, uint64_t pa, uint64_t size)
{
    uint8_t *base = mime_vm_phys(c->vm, pa, size);
    if (!base || index >= MIME_GPU_MAX_BLOCKS || c->blocks[index] || ((uintptr_t)base % g_vk.import_alignment) ||
        (size % g_vk.import_alignment))
        return false;
    VkMemoryHostPointerPropertiesEXT props = {VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (!g_host_pointer ||
        g_host_pointer(g_vk.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, base, &props) != VK_SUCCESS)
        return false;
    VkExternalMemoryBufferCreateInfo ext = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, nullptr,
                                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             &ext,
                             0,
                             size,
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 (g_xfb ? VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT : 0),
                             VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;
    if (g_vk.fn.vkCreateBuffer(g_vk.device, &bi, nullptr, &buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    g_vk.fn.vkGetBufferMemoryRequirements(g_vk.device, buffer, &req);
    int32_t type = memory_type(req.memoryTypeBits & props.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type < 0)
        type = memory_type(req.memoryTypeBits & props.memoryTypeBits, 0);
    VkImportMemoryHostPointerInfoEXT import = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT, nullptr,
                                               VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, base};
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import, size, (uint32_t)type};
    VkDeviceMemory memory;
    if (type < 0 || g_vk.fn.vkAllocateMemory(g_vk.device, &ai, nullptr, &memory) != VK_SUCCESS) {
        g_vk.fn.vkDestroyBuffer(g_vk.device, buffer, nullptr);
        return false;
    }
    g_vk.fn.vkBindBufferMemory(g_vk.device, buffer, memory, 0);
    c->block_base[index] = base;
    c->block_size[index] = size;
    c->block_memory[index] = memory;
    __atomic_store_n(&c->blocks[index], buffer, __ATOMIC_RELEASE);
    return true;
}

/* the dummy textures unbound samplers read, one per view type: opaque black. */
static void init_dummies(Channel *c)
{
    static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&once);
    if (g_default_sampler) {
        pthread_mutex_unlock(&once);
        return;
    }
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = 0.25f};
    g_vk.fn.vkCreateSampler(g_vk.device, &si, nullptr, &g_default_sampler);
    static const uint32_t types[] = {TEXTURE_1D, TEXTURE_1D_ARRAY, TEXTURE_2D, TEXTURE_2D_ARRAY,
                                     TEXTURE_2D_MS, TEXTURE_CUBE, TEXTURE_CUBE_ARRAY, TEXTURE_3D};
    for (int i = 0; i < 8; i++) {
        uint32_t t = types[i];
        uint32_t layers = t == TEXTURE_CUBE || t == TEXTURE_CUBE_ARRAY ? 6 : 1;
        if (!texture_make(&g_dummy[t], t, 70, 1, 1, layers, 1, t == TEXTURE_2D_MS ? 4 : 1, t == TEXTURE_2D_MS))
            continue;
        g_dummy[t].swizzle = 0x05040302;
        g_dummy[t].sampled = sampled_view(&g_dummy[t]);
        texture_init_layout(c, &g_dummy[t]);
        if (t != TEXTURE_2D_MS) {
            VkClearColorValue black = {{0, 0, 0, 1}};
            VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS};
            g_vk.fn.vkCmdClearColorImage(cbuf(c), g_dummy[t].image, VK_IMAGE_LAYOUT_GENERAL, &black, 1, &all);
        }
    }
    c->wrote = true;
    commit(c, 0);
    wait_value(c, c->submitted);
    pthread_mutex_unlock(&once);
}

uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_device);
    if (!g_ok || size != MIME_GPU_CHANNEL_SIZE)
        return MIME_HC_STATUS_UNSUPPORTED;
    uint8_t *base = mime_vm_phys(vm, pa, size);
    if (!base)
        return MIME_HC_STATUS_INVALID;
    Channel *c = calloc(1, sizeof(Channel));
    c->vm = vm;
    c->hdr = (MimeGpuHeader *)base;
    c->hdr->features = (g_storage ? MIME_GPU_FEATURE_COMPUTE : 0) | (g_clip_control ? MIME_GPU_FEATURE_CLIP_CONTROL : 0);
    c->ring = base + MIME_GPU_HEADER_SIZE;
    c->hdr->features |= (g_geometry ? MIME_GPU_FEATURE_GEOMETRY : 0) | (g_tessellation ? MIME_GPU_FEATURE_TESSELLATION : 0) |
                        (g_xfb && g_queries ? MIME_GPU_FEATURE_FEEDBACK : 0) |
                        (g_queries ? MIME_GPU_FEATURE_QUERY : 0) | (g_viewports > 1 ? MIME_GPU_FEATURE_VIEWPORTS : 0) |
                        (g_fp64 ? MIME_GPU_FEATURE_FP64 : 0) | (g_dual_blend ? MIME_GPU_FEATURE_DUAL_BLEND : 0) |
                        (g_cube_array ? MIME_GPU_FEATURE_CUBE_ARRAY : 0);
    pthread_mutex_init(&c->lock, nullptr);
    pthread_cond_init(&c->cond, nullptr);
    pthread_mutex_init(&c->retire_lock, nullptr);
    pthread_mutex_init(&c->query_lock, nullptr);
    pthread_cond_init(&c->retire_cond, nullptr);
    bool ok = add_block(c, 0, pa, size);
    VkCommandPoolCreateInfo pi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, g_family};
    ok = ok && g_vk.fn.vkCreateCommandPool(g_vk.device, &pi, nullptr, &c->pool) == VK_SUCCESS;
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr, VK_SEMAPHORE_TYPE_TIMELINE, 0};
    VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    ok = ok && g_vk.fn.vkCreateSemaphore(g_vk.device, &si, nullptr, &c->timeline) == VK_SUCCESS;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, RING_BYTES,
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE};
    ok = ok && g_vk.fn.vkCreateBuffer(g_vk.device, &bi, nullptr, &c->uniforms) == VK_SUCCESS;
    if (ok) {
        VkMemoryRequirements req;
        g_vk.fn.vkGetBufferMemoryRequirements(g_vk.device, c->uniforms, &req);
        ok = allocate(&req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &c->uniforms_mem);
        if (ok && c->uniforms_mem.block) {
            /* a block is shared: the ring maps memory of its own. */
            release_alloc(&c->uniforms_mem);
            int32_t t = memory_type(req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)t};
            ok = t >= 0 && g_vk.fn.vkAllocateMemory(g_vk.device, &ai, nullptr, &c->uniforms_mem.memory) == VK_SUCCESS;
            c->uniforms_mem.size = req.size;
        }
        ok = ok && g_vk.fn.vkBindBufferMemory(g_vk.device, c->uniforms, c->uniforms_mem.memory, 0) == VK_SUCCESS &&
             g_vk.fn.vkMapMemory(g_vk.device, c->uniforms_mem.memory, 0, VK_WHOLE_SIZE, 0, (void **)&c->uniforms_map) ==
                 VK_SUCCESS;
    }
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 1; ok && i < MAX_CHANNELS; i++) {
        if (!g_channels[i]) {
            c->id = i;
            g_channels[i] = c;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (!ok || !c->id) {
        mime_warn("gpu: cannot open a channel");
        if (c->uniforms)
            g_vk.fn.vkDestroyBuffer(g_vk.device, c->uniforms, nullptr);
        release_alloc(&c->uniforms_mem);
        if (c->timeline)
            g_vk.fn.vkDestroySemaphore(g_vk.device, c->timeline, nullptr);
        if (c->pool)
            g_vk.fn.vkDestroyCommandPool(g_vk.device, c->pool, nullptr);
        if (c->blocks[0]) {
            g_vk.fn.vkDestroyBuffer(g_vk.device, c->blocks[0], nullptr);
            g_vk.fn.vkFreeMemory(g_vk.device, c->block_memory[0], nullptr);
        }
        free(c);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_create(&c->retirer, nullptr, retire_main, c);
    init_dummies(c);
    pthread_create(&c->thread, nullptr, channel_main, c);
    *channel = c->id;
    return MIME_HC_STATUS_OK;
}

uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size)
{
    Channel *c = find_channel(channel);
    if (!c || c->vm != vm)
        return MIME_HC_STATUS_NOT_FOUND;
    return add_block(c, (uint32_t)index, pa, size) ? MIME_HC_STATUS_OK : MIME_HC_STATUS_INVALID;
}

void mime_gpu_kick(uint64_t channel)
{
    Channel *c = find_channel(channel);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->kicked = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
}

void mime_gpu_close_all(void)
{
    for (uint64_t i = 0; i < MAX_CHANNELS; i++)
        mime_gpu_close(i);
}

void mime_gpu_close(uint64_t channel)
{
    pthread_mutex_lock(&g_lock);
    Channel *c = channel < MAX_CHANNELS ? g_channels[channel] : nullptr;
    if (c)
        g_channels[channel] = nullptr;
    pthread_mutex_unlock(&g_lock);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->stop = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, nullptr);

    end_rendering(c);
    for (uint32_t i = 0; i < c->surfaces_cap; i++)
        if (c->surfaces[i].live || c->surfaces[i].rendered)
            surface_destroy(c, i);
    commit(c, 0);
    wait_value(c, c->submitted);
    pthread_mutex_lock(&c->retire_lock);
    c->retire_stop = true;
    pthread_cond_broadcast(&c->retire_cond);
    pthread_mutex_unlock(&c->retire_lock);
    pthread_join(c->retirer, nullptr);

    for (uint32_t i = 0; i < c->textures_cap; i++)
        texture_free(nullptr, &c->textures[i]);
    for (uint32_t i = 0; i < c->pipelines_cap; i++)
        for (int k = 0; k < 8; k++)
            if (c->pipelines[i].variants[k])
                g_vk.fn.vkDestroyPipeline(g_vk.device, c->pipelines[i].variants[k], nullptr);
    for (uint32_t i = 0; i < c->programs_cap; i++)
        if (c->programs[i])
            program_free(c->programs[i]);
    for (uint32_t i = 0; i < c->samplers_cap; i++)
        if (c->samplers[i])
            g_vk.fn.vkDestroySampler(g_vk.device, c->samplers[i], nullptr);
    for (int i = 0; i < 16; i++)
        if (c->clear_pipelines[i])
            g_vk.fn.vkDestroyPipeline(g_vk.device, c->clear_pipelines[i], nullptr);
    if (c->clear_layout)
        g_vk.fn.vkDestroyPipelineLayout(g_vk.device, c->clear_layout, nullptr);
    for (uint32_t i = 0; i < c->garbage_count; i++)
        destroy_garbage(&c->garbage[i]);
    for (int i = 0; i < MAX_BATCHES; i++)
        for (uint32_t k = 0; k < c->batches[i].pool_count; k++)
            g_vk.fn.vkDestroyDescriptorPool(g_vk.device, c->batches[i].pools[k], nullptr);
    for (uint32_t i = 0; i < c->spare_count; i++)
        g_vk.fn.vkDestroyDescriptorPool(g_vk.device, c->spare_pools[i], nullptr);
    g_vk.fn.vkDestroyCommandPool(g_vk.device, c->pool, nullptr);
    g_vk.fn.vkDestroySemaphore(g_vk.device, c->timeline, nullptr);
    g_vk.fn.vkDestroyBuffer(g_vk.device, c->uniforms, nullptr);
    release_alloc(&c->uniforms_mem);
    for (int i = 0; i < MIME_GPU_MAX_BLOCKS; i++) {
        if (c->blocks[i]) {
            g_vk.fn.vkDestroyBuffer(g_vk.device, c->blocks[i], nullptr);
            g_vk.fn.vkFreeMemory(g_vk.device, c->block_memory[i], nullptr);
        }
    }
    free(c->textures);
    free(c->programs);
    free(c->pipelines);
    free(c->depth_states);
    free(c->samplers);
    free(c->surfaces);
    free(c->garbage);
    free(c->spare_pools);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->retire_lock);
    pthread_cond_destroy(&c->retire_cond);
    free(c);
}
