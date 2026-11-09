#include "vkprofile.h"
#include <mime/win32/winuser.h>
#include <mime/win32/d3d12.h>
#include <mime/mvk_video.h>

static void profile_log(const char *format, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

static BOOL has_extension(const VkExtensionProperties *list, UINT count, const char *name)
{
    for (UINT i = 0; i < count; i++)
        if (!strcmp(list[i].extensionName, name))
            return TRUE;
    return FALSE;
}

typedef struct Fns {
    PFN_vkGetPhysicalDeviceProperties2 properties2;
    PFN_vkGetPhysicalDeviceFeatures2 features2;
    PFN_vkGetPhysicalDeviceFormatProperties format_properties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties queue_families;
    PFN_vkEnumerateDeviceExtensionProperties extensions;
} Fns;

/* holds every create info structure so the pNext chain outlives build(). */
typedef struct Request {
    VkDeviceCreateInfo info;
    VkDeviceQueueCreateInfo queue;
    float priorities[MIME_VK_MAX_QUEUES];
    const char *names[64];
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan11Features f11;
    VkPhysicalDeviceVulkan12Features f12;
    VkPhysicalDeviceVulkan13Features f13;
    VkPhysicalDeviceShaderObjectFeaturesEXT object;
    VkPhysicalDeviceRobustness2FeaturesEXT robust;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT db;
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_type;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT clip;
    VkPhysicalDeviceCustomBorderColorFeaturesEXT border;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT feedback;
    VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR divisor;
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2;
    VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT unused;
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh;
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT atomic64;
    VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5;
    VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT dgc;
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric;
    VkPhysicalDeviceMaintenance8FeaturesKHR maintenance8;
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR vrs;
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as;
    VkPhysicalDeviceRayQueryFeaturesKHR ray_query;
    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rt_maintenance;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline;
    VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT group_handles;
} Request;

typedef struct Props {
    VkPhysicalDeviceProperties2 base;
    VkPhysicalDeviceTransformFeedbackPropertiesEXT xfb;
    VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservative;
    VkPhysicalDeviceFragmentShadingRatePropertiesKHR vrs;
    VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT dgc;
    VkPhysicalDeviceMeshShaderPropertiesEXT mesh;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT db;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as;
    VkPhysicalDeviceVulkan13Properties p13;
    VkPhysicalDeviceSubgroupProperties sub;
} Props;

/*
 * the union of what direct3d 11 and 12 need. each trims features it has no use for, and a feature stays off only
 * when both trim it. direct3d 11 draws with shader objects, which must set the dynamic state of every enabled
 * feature, so a feature only direct3d 12 wants costs direct3d 11 one more call per command buffer: see the
 * context's dynamic state for depth bounds and the shading rate.
 */
static HRESULT build(const Fns *fn, VkPhysicalDevice physical, MimeVkProfile *p, Request *r)
{
    memset(p, 0, sizeof(*p));
    VkPhysicalDeviceVulkan12Properties p12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 base = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p12};
    fn->properties2(physical, &base);
    p->inf_nan_widths = (p12.shaderSignedZeroInfNanPreserveFloat16 ? 1 : 0) |
                        (p12.shaderSignedZeroInfNanPreserveFloat32 ? 2 : 0) |
                        (p12.shaderSignedZeroInfNanPreserveFloat64 ? 4 : 0);

    UINT count = 0;
    fn->extensions(physical, nullptr, &count, nullptr);
    VkExtensionProperties *available = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(*available));
    if (!available)
        return E_OUTOFMEMORY;
    fn->extensions(physical, nullptr, &count, available);

    UINT n = 0;
    /* required extensions. */
    static const char *const both[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME};
    static const char *const only11[] = {VK_EXT_SHADER_OBJECT_EXTENSION_NAME};
    static const char *const only12[] = {VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
                                         VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME};
    BOOL ok_both = TRUE, ok11 = TRUE, ok12 = TRUE;
    for (UINT i = 0; i < RTL_NUMBER_OF(both); i++) {
        if (!has_extension(available, count, both[i])) {
            profile_log("d3d: the gpu lacks %s", both[i]);
            ok_both = FALSE;
        } else {
            r->names[n++] = both[i];
        }
    }
    for (UINT i = 0; i < RTL_NUMBER_OF(only11); i++) {
        if (has_extension(available, count, only11[i]))
            r->names[n++] = only11[i];
        else
            ok11 = FALSE;
    }
    for (UINT i = 0; i < RTL_NUMBER_OF(only12); i++) {
        if (has_extension(available, count, only12[i]))
            r->names[n++] = only12[i];
        else
            ok12 = FALSE;
    }
    if (!ok_both) {
        HeapFree(GetProcessHeap(), 0, available);
        return DXGI_ERROR_UNSUPPORTED;
    }
    BOOL shader_object = ok11;
    BOOL descriptor_buffer = ok12;
#define WANT(name, flag)                                                                                               \
    if (has_extension(available, count, name)) {                                                                     \
        r->names[n++] = name;                                                                                          \
        flag;                                                                                                          \
    }
    WANT(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME, p->has_depth_clip = TRUE)
    WANT(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME, p->has_custom_border = TRUE)
    WANT(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME, p->has_transform_feedback = TRUE)
    WANT(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME, p->has_conditional = TRUE)
    WANT(VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME, p->has_divisor = TRUE)
    WANT(MVK_VIDEO_DECODE_EXTENSION, p->has_video = TRUE)
    WANT(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME, (void)0)
    WANT(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME, (void)0)
    WANT(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME, p->has_conservative = TRUE)
    WANT(VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME, p->has_patch_points = TRUE)
    WANT(VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME, p->has_unused_attachments = TRUE)
    WANT(VK_EXT_MESH_SHADER_EXTENSION_NAME, p->has_mesh = TRUE)
    WANT(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME, p->has_atomic64 = TRUE)
    WANT(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME, p->has_barycentrics = TRUE)
    WANT(VK_KHR_MAINTENANCE_8_EXTENSION_NAME, p->has_maintenance8 = TRUE)
    WANT(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME, p->has_compute_derivatives = TRUE)
    WANT(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, p->has_vrs = TRUE)
    WANT(VK_KHR_MAINTENANCE_5_EXTENSION_NAME, p->has_maintenance5 = TRUE)
    if (p->has_maintenance5)
        WANT(VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME, p->has_dgc = TRUE)
    /*
     * acceleration structures and ray queries need all of these. they cost the host driver about 30 ms of the 80 a
     * device takes, so they are only enabled when d3d12.dll is already loaded at device creation. a browser's
     * direct3d 11 device, made before WebGPU loads direct3d 12, goes without ray tracing, which WebGPU does not use.
     */
    BOOL rt_extensions = GetModuleHandleW(L"d3d12.dll") &&
                         has_extension(available, count, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
                         has_extension(available, count, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                         has_extension(available, count, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                         has_extension(available, count, VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
    if (rt_extensions) {
        r->names[n++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        r->names[n++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        r->names[n++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        r->names[n++] = VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME;
        p->has_rt = TRUE;
        /* ray tracing pipelines, with library group handles for AddToStateObject. */
        if (has_extension(available, count, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
            has_extension(available, count, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) &&
            has_extension(available, count, VK_EXT_PIPELINE_LIBRARY_GROUP_HANDLES_EXTENSION_NAME)) {
            r->names[n++] = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
            r->names[n++] = VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME;
            r->names[n++] = VK_EXT_PIPELINE_LIBRARY_GROUP_HANDLES_EXTENSION_NAME;
            p->has_rt_pipeline = TRUE;
        }
    }
#undef WANT
    HeapFree(GetProcessHeap(), 0, available);

    /* chain the feature structures of every enabled extension for the gpu to fill in. */
    void *head = nullptr;
#define SET(s, type) ((s) = (typeof(s)){type})
#define LINK(s) ((s).pNext = head, head = &(s))
    SET(r->f11, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    SET(r->f12, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    SET(r->f13, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    SET(r->robust, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT);
    LINK(r->f11);
    LINK(r->f12);
    LINK(r->f13);
    LINK(r->robust);
    if (shader_object) {
        SET(r->object, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT);
        LINK(r->object);
    }
    if (descriptor_buffer) {
        SET(r->db, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
        SET(r->mutable_type, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT);
        LINK(r->db);
        LINK(r->mutable_type);
    }
    if (p->has_depth_clip) {
        SET(r->clip, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT);
        LINK(r->clip);
    }
    if (p->has_custom_border) {
        SET(r->border, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT);
        LINK(r->border);
    }
    if (p->has_transform_feedback) {
        SET(r->feedback, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT);
        LINK(r->feedback);
    }
    if (p->has_conditional) {
        SET(r->conditional, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT);
        LINK(r->conditional);
    }
    if (p->has_divisor) {
        SET(r->divisor, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR);
        LINK(r->divisor);
    }
    if (p->has_patch_points) {
        SET(r->eds2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT);
        LINK(r->eds2);
    }
    if (p->has_unused_attachments) {
        SET(r->unused, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT);
        LINK(r->unused);
    }
    if (p->has_mesh) {
        SET(r->mesh, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT);
        LINK(r->mesh);
    }
    if (p->has_atomic64) {
        SET(r->atomic64, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT);
        LINK(r->atomic64);
    }
    if (p->has_maintenance5) {
        SET(r->maintenance5, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR);
        LINK(r->maintenance5);
    }
    if (p->has_dgc) {
        SET(r->dgc, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT);
        LINK(r->dgc);
    }
    if (p->has_barycentrics) {
        SET(r->barycentric, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR);
        LINK(r->barycentric);
    }
    if (p->has_maintenance8) {
        SET(r->maintenance8, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR);
        LINK(r->maintenance8);
    }
    if (p->has_vrs) {
        SET(r->vrs, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
        LINK(r->vrs);
    }
    if (p->has_compute_derivatives) {
        SET(r->derivatives, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR);
        LINK(r->derivatives);
    }
    if (p->has_rt) {
        SET(r->as, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
        SET(r->ray_query, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
        SET(r->rt_maintenance, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR);
        LINK(r->as);
        LINK(r->ray_query);
        LINK(r->rt_maintenance);
    }
    if (p->has_rt_pipeline) {
        SET(r->rt_pipeline, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
        SET(r->group_handles, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_EXT);
        LINK(r->rt_pipeline);
        LINK(r->group_handles);
    }
    SET(r->features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    r->features.pNext = head;
#undef SET
#undef LINK
    fn->features2(physical, &r->features);

    VkPhysicalDeviceFeatures *f = &r->features.features;
    const VkPhysicalDeviceFeatures supported = *f;
    VkPhysicalDeviceVulkan11Features *f11 = &r->f11;
    VkPhysicalDeviceVulkan12Features *f12 = &r->f12;
    VkPhysicalDeviceVulkan13Features *f13 = &r->f13;

    /* the minimum each api needs. */
    BOOL core = f13->dynamicRendering && f13->synchronization2 && f12->timelineSemaphore && f12->hostQueryReset &&
                f12->runtimeDescriptorArray && f12->descriptorBindingPartiallyBound;
    p->has_null_descriptor = r->robust.nullDescriptor;
    p->d3d11 = ok11 && core && r->object.shaderObject && f12->descriptorBindingSampledImageUpdateAfterBind &&
               f12->descriptorBindingUniformBufferUpdateAfterBind;
    /* without null descriptors, direct3d 12 writes stand ins of each view dimension, see its descriptor.c. */
    p->d3d12 = ok12 && core && r->db.descriptorBuffer && r->mutable_type.mutableDescriptorType &&
               f12->bufferDeviceAddress && f12->shaderSampledImageArrayNonUniformIndexing &&
               base.properties.limits.maxPushConstantsSize >= MIME_VK_D3D12_PUSH_SIZE;
    if (!p->d3d11)
        profile_log("d3d11: the gpu lacks features direct3d 11 needs");
    if (!p->d3d12)
        profile_log("d3d12: the gpu lacks features direct3d 12 needs");
    if (!p->d3d11 && !p->d3d12)
        return DXGI_ERROR_UNSUPPORTED;

    f->sparseBinding = f->sparseResidencyBuffer = f->sparseResidencyImage2D = f->sparseResidencyImage3D = FALSE;
    f->sparseResidency2Samples = f->sparseResidency4Samples = f->sparseResidency8Samples = FALSE;
    f->sparseResidency16Samples = f->sparseResidencyAliased = FALSE;
    f->robustBufferAccess = TRUE;
    f11->protectedMemory = FALSE;
    f11->multiview = f11->multiviewGeometryShader = f11->multiviewTessellationShader = FALSE;
    f11->samplerYcbcrConversion = FALSE;
    f12->bufferDeviceAddressCaptureReplay = f12->bufferDeviceAddressMultiDevice = FALSE;
    f13->privateData = FALSE;
    f13->inlineUniformBlock = f13->descriptorBindingInlineUniformBlockUpdateAfterBind = FALSE;
    f13->textureCompressionASTC_HDR = FALSE;
    r->db.descriptorBufferCaptureReplay = r->db.descriptorBufferImageLayoutIgnored = FALSE;
    r->db.descriptorBufferPushDescriptors = FALSE;
    r->robust.robustBufferAccess2 = r->robust.robustBufferAccess2 && f->robustBufferAccess;
    r->feedback.geometryStreams = r->feedback.geometryStreams && r->feedback.transformFeedback;
    r->conditional.inheritedConditionalRendering = FALSE;
    p->has_logic_op = f->logicOp;
    p->has_msaa_storage = f->shaderStorageImageMultisample;
    p->has_statistics = f->pipelineStatisticsQuery;
    p->has_precise_occlusion = f->occlusionQueryPrecise;
    p->has_depth_bounds = f->depthBounds;
    p->has_geometry = f->geometryShader;
    p->has_tessellation = f->tessellationShader;
    if (!r->clip.depthClipEnable)
        p->has_depth_clip = FALSE;
    if (!r->feedback.transformFeedback)
        p->has_transform_feedback = FALSE;
    if (!r->conditional.conditionalRendering)
        p->has_conditional = FALSE;
    if (!r->divisor.vertexAttributeInstanceRateDivisor)
        p->has_divisor = FALSE;
    p->has_zero_divisor = p->has_divisor && r->divisor.vertexAttributeInstanceRateZeroDivisor;
    if (!r->border.customBorderColors)
        p->has_custom_border = FALSE;
    if (!r->eds2.extendedDynamicState2PatchControlPoints)
        p->has_patch_points = FALSE;
    r->eds2.extendedDynamicState2LogicOp = FALSE;
    if (!r->unused.dynamicRenderingUnusedAttachments)
        p->has_unused_attachments = FALSE;
    /* unused features that would make shader objects set their state every draw stay off. */
    f->alphaToOne = FALSE;
    f->wideLines = FALSE;
    p->has_native16 = f12->shaderFloat16 && f->shaderInt16;
    /* mesh shaders need direct3d's tier 1 limits. multiview stays off, per-primitive rates wait for the vrs tier. */
    BOOL mesh_rates = r->mesh.primitiveFragmentShadingRateMeshShader;
    r->mesh.multiviewMeshShader = r->mesh.primitiveFragmentShadingRateMeshShader = FALSE;
    if (!r->mesh.meshShader || !r->mesh.taskShader)
        p->has_mesh = FALSE;
    /* lets shader code go to pipelines without shader modules. */
    if (!r->maintenance5.maintenance5)
        p->has_maintenance5 = FALSE;
    if (!r->dgc.deviceGeneratedCommands || !p->has_maintenance5)
        p->has_dgc = FALSE;
    /* 64-bit atomics on typed views need image, buffer and int64 support. sparse image atomics are unused. */
    r->atomic64.sparseImageInt64Atomics = FALSE;
    VkFormatProperties wide;
    fn->format_properties(physical, VK_FORMAT_R64_UINT, &wide);
    if (!r->atomic64.shaderImageInt64Atomics || !f12->shaderBufferInt64Atomics || !f->shaderInt64 ||
        !(wide.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT))
        p->has_atomic64 = FALSE;
    p->has_shared_atomic64 = f12->shaderSharedInt64Atomics && f->shaderInt64;
    if (!r->barycentric.fragmentShaderBarycentric)
        p->has_barycentrics = FALSE;
    /* direct3d compute derivatives use quads of consecutive threads, which is vulkan's linear grouping. */
    r->derivatives.computeDerivativeGroupQuads = FALSE;
    if (!r->derivatives.computeDerivativeGroupLinear)
        p->has_compute_derivatives = FALSE;
    /* for copies between depth or stencil and color images. */
    if (!r->maintenance8.maintenance8)
        p->has_maintenance8 = FALSE;
    /* builds run on the gpu only, without capture and replay. */
    r->as.accelerationStructureCaptureReplay = r->as.accelerationStructureHostCommands = FALSE;
    r->as.accelerationStructureIndirectBuild = FALSE;
    if (!r->as.accelerationStructure || !r->ray_query.rayQuery || !r->rt_maintenance.rayTracingMaintenance1)
        p->has_rt = FALSE;
    r->rt_pipeline.rayTracingPipelineShaderGroupHandleCaptureReplay = FALSE;
    r->rt_pipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = FALSE;
    /* direct3d's indirect DispatchRays arguments map to vkCmdTraceRaysIndirect2KHR. */
    if (!p->has_rt || !r->rt_pipeline.rayTracingPipeline || !r->rt_pipeline.rayTraversalPrimitiveCulling ||
        !r->group_handles.pipelineLibraryGroupHandles || !r->rt_maintenance.rayTracingPipelineTraceRaysIndirect2)
        p->has_rt_pipeline = FALSE;
    if (!p->has_rt_pipeline)
        r->rt_maintenance.rayTracingPipelineTraceRaysIndirect2 = FALSE;

    Props props = {};
    void *chain = nullptr;
#define PSET(s, type) ((s) = (typeof(s)){type, chain}, chain = &(s))
    PSET(props.p13, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES);
    PSET(props.sub, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES);
    if (p->has_transform_feedback)
        PSET(props.xfb, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT);
    if (p->has_conservative)
        PSET(props.conservative, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT);
    if (p->has_vrs)
        PSET(props.vrs, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR);
    if (p->has_dgc)
        PSET(props.dgc, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT);
    if (p->has_mesh)
        PSET(props.mesh, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT);
    if (descriptor_buffer)
        PSET(props.db, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT);
    if (p->has_rt_pipeline)
        PSET(props.rt, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
    if (p->has_rt)
        PSET(props.as, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
#undef PSET
    props.base = (VkPhysicalDeviceProperties2){VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, chain};
    fn->properties2(physical, &props.base);
    p->db = props.db;
    p->db.pNext = nullptr;
    /* direct3d aligns scratch memory to 256 bytes. */
    if (props.as.minAccelerationStructureScratchOffsetAlignment > D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)
        p->has_rt = p->has_rt_pipeline = FALSE;
    /* shader identifiers are group handles and shader tables are used as is, so sizes and alignments must agree. */
    if (p->has_rt_pipeline &&
        (props.rt.shaderGroupHandleSize != D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES ||
         props.rt.shaderGroupHandleAlignment > D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT ||
         props.rt.shaderGroupBaseAlignment > D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT ||
         props.rt.maxShaderGroupStride < 4096 ||
         props.rt.maxRayRecursionDepth < D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH))
        p->has_rt_pipeline = FALSE;
    /* generated commands must read dxgi format index buffer views and reach the compute and vertex stages. */
    if (p->has_dgc) {
        p->dgc_stages = props.dgc.supportedIndirectCommandsShaderStages;
        if (!(props.dgc.supportedIndirectCommandsInputModes & VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT) ||
            !(p->dgc_stages & VK_SHADER_STAGE_COMPUTE_BIT) || !(p->dgc_stages & VK_SHADER_STAGE_VERTEX_BIT))
            p->has_dgc = FALSE;
    }
    if (p->has_mesh && (props.mesh.maxMeshOutputVertices < 256 || props.mesh.maxMeshOutputPrimitives < 256 ||
                        props.mesh.maxMeshWorkGroupInvocations < 128 || props.mesh.maxTaskWorkGroupInvocations < 128 ||
                        props.mesh.maxTaskPayloadSize < 16384))
        p->has_mesh = FALSE;
    /* shader objects want both stages bound when either feature is on, so both go off together. */
    if (!p->has_mesh)
        r->mesh.meshShader = r->mesh.taskShader = FALSE;

    /*
     * variable rate shading: tier 1 is a rate per draw. tier 2 adds per primitive rates and a rate image with square
     * tiles of a size direct3d knows, with every combiner.
     */
    VkFormatProperties rate_format;
    fn->format_properties(physical, VK_FORMAT_R8_UINT, &rate_format);
    VkExtent2D tile_min = props.vrs.minFragmentShadingRateAttachmentTexelSize;
    VkExtent2D tile_max = props.vrs.maxFragmentShadingRateAttachmentTexelSize;
    for (UINT size = 8; size <= 32 && !p->vrs_tile; size *= 2)
        if (size >= tile_min.width && size >= tile_min.height && size <= tile_max.width && size <= tile_max.height)
            p->vrs_tile = size;
    BOOL vrs_on = p->has_vrs && r->vrs.pipelineFragmentShadingRate;
    if (vrs_on) {
        p->vrs_tier = D3D12_VARIABLE_SHADING_RATE_TIER_1;
        if (r->vrs.primitiveFragmentShadingRate && r->vrs.attachmentFragmentShadingRate &&
            props.vrs.fragmentShadingRateNonTrivialCombinerOps && props.vrs.fragmentShadingRateStrictMultiplyCombiner &&
            p->vrs_tile && (rate_format.optimalTilingFeatures & VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR))
            p->vrs_tier = D3D12_VARIABLE_SHADING_RATE_TIER_2;
        p->vrs_more_rates = props.vrs.maxFragmentSize.width >= 4 && props.vrs.maxFragmentSize.height >= 4 &&
                            props.vrs.maxFragmentSizeAspectRatio >= 2;
        p->vrs_viewports = props.vrs.primitiveFragmentShadingRateWithMultipleViewports;
    }
    /* only keep the feature when the rate is set, since shader objects then want one before every draw. */
    p->has_vrs = vrs_on;
    if (p->vrs_tier < D3D12_VARIABLE_SHADING_RATE_TIER_2) {
        r->vrs.primitiveFragmentShadingRate = r->vrs.attachmentFragmentShadingRate = FALSE;
        p->vrs_tile = 0;
    } else {
        r->mesh.primitiveFragmentShadingRateMeshShader = p->has_mesh && mesh_rates;
    }

    /*
     * conservative rasterization: tier 1 overestimates by at most half a pixel, tier 2 by 1/256 and keeps degenerate
     * triangles, tier 3 also tells the pixel shader whether its pixel is fully covered.
     */
    if (p->has_conservative && props.conservative.primitiveOverestimationSize <= 0.5f) {
        p->conservative_tier = D3D12_CONSERVATIVE_RASTERIZATION_TIER_1;
        if (props.conservative.primitiveOverestimationSize <= 1.0f / 256.0f &&
            props.conservative.degenerateTrianglesRasterized) {
            p->conservative_tier = D3D12_CONSERVATIVE_RASTERIZATION_TIER_2;
            if (props.conservative.fullyCoveredFragmentShaderInputVariable)
                p->conservative_tier = D3D12_CONSERVATIVE_RASTERIZATION_TIER_3;
        }
    }

    /* wave sizes: what the gpu runs, and the range subgroup size control allows. */
    p->wave_size = p->wave_min = p->wave_max = props.sub.subgroupSize;
    if (f13->subgroupSizeControl && props.p13.minSubgroupSize && props.p13.maxSubgroupSize >= props.p13.minSubgroupSize) {
        p->wave_min = props.p13.minSubgroupSize;
        p->wave_max = props.p13.maxSubgroupSize;
        p->wave_stages = props.p13.requiredSubgroupSizeStages;
    }
    VkSubgroupFeatureFlags wave_ops = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                                      VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                      VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT;
    p->has_waves = (props.sub.supportedOperations & wave_ops) == wave_ops &&
                   (props.sub.supportedStages & (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)) ==
                       (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
    p->has_so_queries = p->has_transform_feedback && props.xfb.transformFeedbackQueries;

    UINT families = 0;
    fn->queue_families(physical, &families, nullptr);
    VkQueueFamilyProperties qprops[16];
    if (families > 16)
        families = 16;
    fn->queue_families(physical, &families, qprops);
    p->queue_family = ~0u;
    for (UINT i = 0; i < families && p->queue_family == ~0u; i++)
        if ((qprops[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            p->queue_family = i;
    if (p->queue_family == ~0u) {
        profile_log("d3d: the gpu has no queue family for graphics and compute");
        return DXGI_ERROR_UNSUPPORTED;
    }
    /*
     * each queue costs the host driver about 20 ms to create and 6 to destroy, so a process gets one, and a few more
     * once direct3d 12 is loaded. direct3d 11 submits on the first, and direct3d 12 queues take the others in turn,
     * sharing the first when it is the only one.
     */
    p->queue_count = GetModuleHandleW(L"d3d12.dll") ? MIME_VK_D3D12_QUEUES : 1;
    if (p->queue_count > qprops[p->queue_family].queueCount)
        p->queue_count = qprops[p->queue_family].queueCount;

    /*
     * tiled resources: tier 1 binds buffers and standard shaped 2D images, aliased. tier 2 also reads zero where
     * nothing is mapped and tells shaders what is. tier 3 also tiles 3D images.
     */
    const VkPhysicalDeviceSparseProperties *sp = &base.properties.sparseProperties;
    if (p->d3d12 && (qprops[p->queue_family].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) && supported.sparseBinding &&
        supported.sparseResidencyBuffer && supported.sparseResidencyImage2D && supported.sparseResidencyAliased &&
        sp->residencyStandard2DBlockShape) {
        p->tiled_tier = D3D12_TILED_RESOURCES_TIER_1;
        f->sparseBinding = f->sparseResidencyBuffer = f->sparseResidencyImage2D = f->sparseResidencyAliased = TRUE;
        if (sp->residencyNonResidentStrict && supported.shaderResourceResidency && supported.shaderResourceMinLod) {
            p->tiled_tier = D3D12_TILED_RESOURCES_TIER_2;
            if (supported.sparseResidencyImage3D && sp->residencyStandard3DBlockShape) {
                p->tiled_tier = D3D12_TILED_RESOURCES_TIER_3;
                f->sparseResidencyImage3D = TRUE;
            }
        }
    }

    for (UINT i = 0; i < MIME_VK_MAX_QUEUES; i++)
        r->priorities[i] = 1.0f;
    r->queue = (VkDeviceQueueCreateInfo){VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    r->queue.queueFamilyIndex = p->queue_family;
    r->queue.queueCount = p->queue_count;
    r->queue.pQueuePriorities = r->priorities;
    r->info = (VkDeviceCreateInfo){VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &r->features};
    r->info.queueCreateInfoCount = 1;
    r->info.pQueueCreateInfos = &r->queue;
    r->info.enabledExtensionCount = n;
    r->info.ppEnabledExtensionNames = r->names;
    return S_OK;
}

_Static_assert(sizeof(MimeVkProfile) <= MIME_VK_PROFILE_BYTES, "the profile fits what dxgi keeps");

HRESULT MimeVkAcquire(IDXGIAdapter *adapter, const MIME_DXGI_VULKAN *v, MimeVkShared *out)
{
    memset(out, 0, sizeof(*out));
    IMimeDXGIAdapter *mime = nullptr;
    if (FAILED(adapter->lpVtbl->QueryInterface(adapter, &IID_IMimeDXGIAdapter, (void **)&mime)))
        return E_INVALIDARG;
    MIME_DXGI_SHARED_VK *shared = nullptr;
    HRESULT hr = mime->lpVtbl->AcquireSharedVulkan(mime, nullptr, nullptr, 0, &shared);
    if (hr == S_FALSE) {
        PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)v->GetInstanceProcAddr;
        VkInstance instance = v->Instance;
        Fns fn = {
            (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2"),
            (PFN_vkGetPhysicalDeviceFeatures2)gipa(instance, "vkGetPhysicalDeviceFeatures2"),
            (PFN_vkGetPhysicalDeviceFormatProperties)gipa(instance, "vkGetPhysicalDeviceFormatProperties"),
            (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"),
            (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties"),
        };
        Request *request = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*request));
        MimeVkProfile *profile = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*profile));
        if (!request || !profile) {
            hr = E_OUTOFMEMORY;
        } else {
            hr = build(&fn, (VkPhysicalDevice)v->PhysicalDevice, profile, request);
            if (SUCCEEDED(hr)) {
                hr = mime->lpVtbl->AcquireSharedVulkan(mime, &request->info, profile, sizeof(*profile), &shared);
                if (FAILED(hr))
                    profile_log("d3d: vkCreateDevice failed");
            }
        }
        HeapFree(GetProcessHeap(), 0, request);
        HeapFree(GetProcessHeap(), 0, profile);
    }
    if (FAILED(hr) || hr == S_FALSE) {
        mime->lpVtbl->Release(mime);
        return FAILED(hr) ? hr : DXGI_ERROR_UNSUPPORTED;
    }
    out->adapter = mime;
    out->vk = shared;
    out->profile = (const MimeVkProfile *)shared->Profile;
    return S_OK;
}

void MimeVkRelease(MimeVkShared *shared)
{
    if (!shared->adapter)
        return;
    shared->adapter->lpVtbl->ReleaseSharedVulkan(shared->adapter, shared->vk);
    shared->adapter->lpVtbl->Release(shared->adapter);
    memset(shared, 0, sizeof(*shared));
}
