/* the instance and the physical device: one Apple GPU, its features, limits and formats. */
#include "vkm.h"

#include <unistd.h>

#define API_VERSION VK_API_VERSION_1_3
#define DRIVER_VERSION VK_MAKE_VERSION(1, 0, 0)

static const VkExtensionProperties g_instance_extensions[] = {
    {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION},
    {VK_EXT_METAL_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_SPEC_VERSION},
    {VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, VK_KHR_GET_SURFACE_CAPABILITIES_2_SPEC_VERSION},
    {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_SPEC_VERSION},
    {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_SPEC_VERSION},
    {VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_SPEC_VERSION},
    {VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_FENCE_CAPABILITIES_SPEC_VERSION},
};

static const VkExtensionProperties g_device_extensions[] = {
    {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION},
    {VK_EXT_ROBUSTNESS_2_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_SPEC_VERSION},
    {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, VK_EXT_SHADER_OBJECT_SPEC_VERSION},
    {VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME, VK_EXT_DESCRIPTOR_BUFFER_SPEC_VERSION},
    {VK_KHR_MAINTENANCE_5_EXTENSION_NAME, VK_KHR_MAINTENANCE_5_SPEC_VERSION},
    {VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME, VK_EXT_TRANSFORM_FEEDBACK_SPEC_VERSION},
    {VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME, VK_EXT_DEVICE_GENERATED_COMMANDS_SPEC_VERSION},
    {VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_SPEC_VERSION},
    {VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME, VK_EXT_DEPTH_CLIP_ENABLE_SPEC_VERSION},
    {VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME, VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_SPEC_VERSION},
    {VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_SPEC_VERSION},
    {VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_SPEC_VERSION},
    {VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_2_SPEC_VERSION},
    {VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_3_SPEC_VERSION},
    {VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_SPEC_VERSION},
    {VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME,
     VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_SPEC_VERSION},
    {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_HOST_SPEC_VERSION},
    {VK_EXT_EXTERNAL_MEMORY_METAL_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_METAL_SPEC_VERSION},
    {VK_EXT_METAL_OBJECTS_EXTENSION_NAME, VK_EXT_METAL_OBJECTS_SPEC_VERSION},
    {VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, VK_EXT_MEMORY_BUDGET_SPEC_VERSION},
    {VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME, VK_EXT_SAMPLER_FILTER_MINMAX_SPEC_VERSION},
    {VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_SPEC_VERSION},
    {VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME, VK_KHR_SHADER_NON_SEMANTIC_INFO_SPEC_VERSION},
    {VK_GOOGLE_HLSL_FUNCTIONALITY_1_EXTENSION_NAME, VK_GOOGLE_HLSL_FUNCTIONALITY_1_SPEC_VERSION},
    {VK_GOOGLE_USER_TYPE_EXTENSION_NAME, VK_GOOGLE_USER_TYPE_SPEC_VERSION},
    {VK_GOOGLE_DECORATE_STRING_EXTENSION_NAME, VK_GOOGLE_DECORATE_STRING_SPEC_VERSION},
    {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_SPEC_VERSION},
    {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_SPEC_VERSION},
    {VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_RAY_QUERY_SPEC_VERSION},
    {VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME, VK_KHR_RAY_TRACING_MAINTENANCE_1_SPEC_VERSION},
};

bool vkm_device_extension(const char *name)
{
    for (size_t k = 0; k < MIME_ARRAY_COUNT(g_device_extensions); k++)
        if (!strcmp(name, g_device_extensions[k].extensionName))
            return true;
    return false;
}

static VkResult enumerate(const void *list, size_t size, uint32_t n, uint32_t *count, void *out)
{
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t k = MIME_MIN(*count, n);
    memcpy(out, list, k * size);
    *count = k;
    return k < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceVersion(uint32_t *version)
{
    *version = API_VERSION;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                                                            VkExtensionProperties *out)
{
    if (layer)
        return VK_ERROR_LAYER_NOT_PRESENT;
    return enumerate(g_instance_extensions, sizeof(*out), MIME_ARRAY_COUNT(g_instance_extensions), count, out);
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t *count, VkLayerProperties *out)
{
    *count = 0;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physical, const char *layer,
                                                                          uint32_t *count, VkExtensionProperties *out)
{
    if (layer)
        return VK_ERROR_LAYER_NOT_PRESENT;
    return enumerate(g_device_extensions, sizeof(*out), MIME_ARRAY_COUNT(g_device_extensions), count, out);
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physical, uint32_t *count,
                                                                      VkLayerProperties *out)
{
    *count = 0;
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ the physical device. */

static struct VkPhysicalDevice_T g_physical;
static bool g_physical_ok;

static void physical_init(void)
{
    VkPhysicalDevice p = &g_physical;
    @autoreleasepool {
        p->mtl = MTLCreateSystemDefaultDevice();
        if (!p->mtl || ![p->mtl supportsFamily:MTLGPUFamilyMetal4]) {
            VKM_ERROR("the gpu has no Metal 4");
            return;
        }
        mime_strlcpy(p->name, p->mtl.name ? p->mtl.name.UTF8String : "", sizeof(p->name));
        p->memory_size = p->mtl.recommendedMaxWorkingSetSize;
        p->float_filter = p->mtl.supports32BitFloatFiltering;
        p->bc = p->mtl.supportsBCTextureCompression;
        uint32_t align = 16;
        for (uint32_t f = VK_FORMAT_R4G4_UNORM_PACK8; f <= VK_FORMAT_E5B9G9R9_UFLOAT_PACK32; f++) {
            const VkmFormat *fi = vkm_format(f);
            if (fi->caps & VKM_CAP_TEXEL)
                align = MIME_MAX(align, (uint32_t)[p->mtl minimumTextureBufferAlignmentForPixelFormat:fi->pixel]);
        }
        p->texel_alignment = align;
        uint64_t registry = p->mtl.registryID;
        memcpy(p->uuid, "mimemtl4", 8);
        memcpy(p->uuid + 8, &registry, 8);
        g_physical_ok = true;
    }
}

static VkPhysicalDevice physical_get(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, physical_init);
    return g_physical_ok ? &g_physical : nullptr;
}

/* ------------------------------------------------------------------ the instance. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *a,
                                                     VkInstance *out)
{
    VkPhysicalDevice physical = physical_get();
    if (!physical)
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    for (uint32_t i = 0; i < info->enabledLayerCount; i++)
        return VK_ERROR_LAYER_NOT_PRESENT;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
        bool known = false;
        for (size_t k = 0; k < MIME_ARRAY_COUNT(g_instance_extensions); k++)
            known |= !strcmp(info->ppEnabledExtensionNames[i], g_instance_extensions[k].extensionName);
        if (!known) {
            VKM_ERROR("no instance extension %s", info->ppEnabledExtensionNames[i]);
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    VkInstance instance = vkm_zalloc(sizeof(*instance));
    if (!instance)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    instance->api = info->pApplicationInfo && info->pApplicationInfo->apiVersion ? info->pApplicationInfo->apiVersion
                                                                                : VK_API_VERSION_1_0;
    instance->physical = physical;
    *out = instance;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *a)
{
    free(instance);
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance instance, uint32_t *count,
                                                               VkPhysicalDevice *out)
{
    return enumerate(&instance->physical, sizeof(*out), 1, count, out);
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *count,
                                                                    VkPhysicalDeviceGroupProperties *out)
{
    if (!out) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (!*count)
        return VK_INCOMPLETE;
    out[0].physicalDeviceCount = 1;
    out[0].physicalDevices[0] = instance->physical;
    out[0].subsetAllocation = VK_FALSE;
    *count = 1;
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ features. */

static const VkPhysicalDeviceFeatures g_features = {
    .robustBufferAccess = VK_TRUE,
    .fullDrawIndexUint32 = VK_TRUE,
    .imageCubeArray = VK_TRUE,
    .independentBlend = VK_TRUE,
    .geometryShader = VK_TRUE,
    .sampleRateShading = VK_TRUE,
    .dualSrcBlend = VK_TRUE,
    .multiDrawIndirect = VK_TRUE,
    .drawIndirectFirstInstance = VK_TRUE,
    .depthClamp = VK_TRUE,
    .depthBiasClamp = VK_TRUE,
    .fillModeNonSolid = VK_TRUE,
    .largePoints = VK_TRUE,
    .alphaToOne = VK_TRUE,
    .multiViewport = VK_TRUE,
    .samplerAnisotropy = VK_TRUE,
    .textureCompressionETC2 = VK_TRUE,
    .textureCompressionASTC_LDR = VK_TRUE,
    .textureCompressionBC = VK_TRUE,
    .occlusionQueryPrecise = VK_TRUE,
    .pipelineStatisticsQuery = VK_TRUE,
    .vertexPipelineStoresAndAtomics = VK_TRUE,
    .fragmentStoresAndAtomics = VK_TRUE,
    .shaderImageGatherExtended = VK_TRUE,
    .shaderStorageImageExtendedFormats = VK_TRUE,
    .shaderStorageImageReadWithoutFormat = VK_TRUE,
    .shaderStorageImageWriteWithoutFormat = VK_TRUE,
    .shaderUniformBufferArrayDynamicIndexing = VK_TRUE,
    .shaderSampledImageArrayDynamicIndexing = VK_TRUE,
    .shaderStorageBufferArrayDynamicIndexing = VK_TRUE,
    .shaderStorageImageArrayDynamicIndexing = VK_TRUE,
    .shaderClipDistance = VK_TRUE,
    .shaderInt64 = VK_TRUE,
    .shaderInt16 = VK_TRUE,
    .shaderResourceMinLod = VK_TRUE,
    .inheritedQueries = VK_TRUE,
};

static const VkPhysicalDeviceVulkan11Features g_features11 = {
    .storageBuffer16BitAccess = VK_TRUE,
    .uniformAndStorageBuffer16BitAccess = VK_TRUE,
    .storagePushConstant16 = VK_TRUE,
    .storageInputOutput16 = VK_TRUE,
    .variablePointersStorageBuffer = VK_TRUE,
    .variablePointers = VK_TRUE,
    .shaderDrawParameters = VK_TRUE,
};

static const VkPhysicalDeviceVulkan12Features g_features12 = {
    .samplerMirrorClampToEdge = VK_TRUE,
    .storageBuffer8BitAccess = VK_TRUE,
    .uniformAndStorageBuffer8BitAccess = VK_TRUE,
    .storagePushConstant8 = VK_TRUE,
    .shaderFloat16 = VK_TRUE,
    .shaderInt8 = VK_TRUE,
    .descriptorIndexing = VK_TRUE,
    .shaderInputAttachmentArrayDynamicIndexing = VK_TRUE,
    .shaderUniformTexelBufferArrayDynamicIndexing = VK_TRUE,
    .shaderStorageTexelBufferArrayDynamicIndexing = VK_TRUE,
    .shaderUniformBufferArrayNonUniformIndexing = VK_TRUE,
    .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
    .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
    .shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
    .shaderInputAttachmentArrayNonUniformIndexing = VK_TRUE,
    .shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE,
    .shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE,
    .descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
    .descriptorBindingPartiallyBound = VK_TRUE,
    .descriptorBindingVariableDescriptorCount = VK_TRUE,
    .runtimeDescriptorArray = VK_TRUE,
    .samplerFilterMinmax = VK_TRUE,
    .scalarBlockLayout = VK_TRUE,
    .imagelessFramebuffer = VK_TRUE,
    .uniformBufferStandardLayout = VK_TRUE,
    .shaderSubgroupExtendedTypes = VK_TRUE,
    .separateDepthStencilLayouts = VK_TRUE,
    .hostQueryReset = VK_TRUE,
    .timelineSemaphore = VK_TRUE,
    .bufferDeviceAddress = VK_TRUE,
    .vulkanMemoryModel = VK_TRUE,
    .vulkanMemoryModelDeviceScope = VK_TRUE,
    .shaderOutputViewportIndex = VK_TRUE,
    .shaderOutputLayer = VK_TRUE,
    .subgroupBroadcastDynamicId = VK_TRUE,
};

static const VkPhysicalDeviceVulkan13Features g_features13 = {
    .robustImageAccess = VK_TRUE,
    .pipelineCreationCacheControl = VK_TRUE,
    .shaderDemoteToHelperInvocation = VK_TRUE,
    .shaderTerminateInvocation = VK_TRUE,
    .subgroupSizeControl = VK_TRUE,
    .computeFullSubgroups = VK_TRUE,
    .synchronization2 = VK_TRUE,
    .shaderZeroInitializeWorkgroupMemory = VK_TRUE,
    .dynamicRendering = VK_TRUE,
    .shaderIntegerDotProduct = VK_TRUE,
    .maintenance4 = VK_TRUE,
};

#define COPY_FEATURES(out, in, first)                                                                               \
    memcpy((uint8_t *)(out) + offsetof(typeof(*(out)), first), (const uint8_t *)(in) + offsetof(typeof(*(in)), first), \
           sizeof(*(out)) - offsetof(typeof(*(out)), first))

static void fill_features(VkBaseOutStructure *chain)
{
    const VkPhysicalDeviceVulkan11Features *f11 = &g_features11;
    const VkPhysicalDeviceVulkan12Features *f12 = &g_features12;
    const VkPhysicalDeviceVulkan13Features *f13 = &g_features13;
    for (VkBaseOutStructure *s = chain; s; s = s->pNext) {
        switch (s->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
            COPY_FEATURES((VkPhysicalDeviceVulkan11Features *)s, f11, storageBuffer16BitAccess);
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
            COPY_FEATURES((VkPhysicalDeviceVulkan12Features *)s, f12, samplerMirrorClampToEdge);
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
            COPY_FEATURES((VkPhysicalDeviceVulkan13Features *)s, f13, robustImageAccess);
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            VkPhysicalDevice16BitStorageFeatures *x = (void *)s;
            x->storageBuffer16BitAccess = x->uniformAndStorageBuffer16BitAccess = VK_TRUE;
            x->storagePushConstant16 = x->storageInputOutput16 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: {
            VkPhysicalDevice8BitStorageFeatures *x = (void *)s;
            x->storageBuffer8BitAccess = x->uniformAndStorageBuffer8BitAccess = x->storagePushConstant8 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
            VkPhysicalDeviceVariablePointersFeatures *x = (void *)s;
            x->variablePointersStorageBuffer = x->variablePointers = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES:
            ((VkPhysicalDeviceShaderDrawParametersFeatures *)s)->shaderDrawParameters = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
            VkPhysicalDeviceShaderFloat16Int8Features *x = (void *)s;
            x->shaderFloat16 = x->shaderInt8 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
            COPY_FEATURES((VkPhysicalDeviceDescriptorIndexingFeatures *)s, f12,
                          shaderInputAttachmentArrayDynamicIndexing);
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
            ((VkPhysicalDeviceScalarBlockLayoutFeatures *)s)->scalarBlockLayout = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES:
            ((VkPhysicalDeviceImagelessFramebufferFeatures *)s)->imagelessFramebuffer = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES:
            ((VkPhysicalDeviceUniformBufferStandardLayoutFeatures *)s)->uniformBufferStandardLayout = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES:
            ((VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *)s)->shaderSubgroupExtendedTypes = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES:
            ((VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *)s)->separateDepthStencilLayouts = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES:
            ((VkPhysicalDeviceHostQueryResetFeatures *)s)->hostQueryReset = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            ((VkPhysicalDeviceTimelineSemaphoreFeatures *)s)->timelineSemaphore = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
            ((VkPhysicalDeviceBufferDeviceAddressFeatures *)s)->bufferDeviceAddress = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: {
            VkPhysicalDeviceVulkanMemoryModelFeatures *x = (void *)s;
            x->vulkanMemoryModel = x->vulkanMemoryModelDeviceScope = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
            ((VkPhysicalDeviceDynamicRenderingFeatures *)s)->dynamicRendering = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
            ((VkPhysicalDeviceSynchronization2Features *)s)->synchronization2 = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES:
            ((VkPhysicalDeviceMaintenance4Features *)s)->maintenance4 = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT:
            ((VkPhysicalDeviceTransformFeedbackFeaturesEXT *)s)->transformFeedback = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES:
            ((VkPhysicalDeviceMaintenance5Features *)s)->maintenance5 = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT: {
            VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *x = (void *)s;
            x->deviceGeneratedCommands = x->dynamicGeneratedPipelineLayout = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES:
            ((VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *)s)->shaderDemoteToHelperInvocation = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES:
            ((VkPhysicalDeviceShaderTerminateInvocationFeatures *)s)->shaderTerminateInvocation = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES:
            ((VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *)s)->shaderZeroInitializeWorkgroupMemory = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES:
            ((VkPhysicalDeviceImageRobustnessFeatures *)s)->robustImageAccess = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: {
            VkPhysicalDeviceSubgroupSizeControlFeatures *x = (void *)s;
            x->subgroupSizeControl = x->computeFullSubgroups = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
            VkPhysicalDeviceRobustness2FeaturesEXT *x = (void *)s;
            /*
             * a null descriptor would be resource id 0, which Metal does not read as zeros but as whatever texture
             * holds that id, and one slot is read as several kinds of texture. without it, direct3d's unbound slots
             * get stand ins that read zeros.
             */
            x->robustBufferAccess2 = x->robustImageAccess2 = VK_TRUE;
            x->nullDescriptor = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT:
            ((VkPhysicalDeviceShaderObjectFeaturesEXT *)s)->shaderObject = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT: {
            VkPhysicalDeviceDescriptorBufferFeaturesEXT *x = (void *)s;
            x->descriptorBuffer = VK_TRUE;
            x->descriptorBufferCaptureReplay = x->descriptorBufferImageLayoutIgnored = VK_FALSE;
            x->descriptorBufferPushDescriptors = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT:
            ((VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *)s)->mutableDescriptorType = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT:
            ((VkPhysicalDeviceDepthClipEnableFeaturesEXT *)s)->depthClipEnable = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR: {
            VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR *x = (void *)s;
            x->vertexAttributeInstanceRateDivisor = x->vertexAttributeInstanceRateZeroDivisor = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT:
            ((VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)s)->extendedDynamicState = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT:
            ((VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)s)->extendedDynamicState2 = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT: {
            VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *x = (void *)s;
            x->extendedDynamicState3DepthClampEnable = x->extendedDynamicState3PolygonMode = VK_TRUE;
            x->extendedDynamicState3RasterizationSamples = x->extendedDynamicState3SampleMask = VK_TRUE;
            x->extendedDynamicState3AlphaToCoverageEnable = x->extendedDynamicState3AlphaToOneEnable = VK_TRUE;
            x->extendedDynamicState3ColorBlendEnable = x->extendedDynamicState3ColorBlendEquation = VK_TRUE;
            x->extendedDynamicState3ColorWriteMask = x->extendedDynamicState3DepthClipEnable = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT:
            ((VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *)s)->vertexInputDynamicState = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT:
            ((VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *)s)->dynamicRenderingUnusedAttachments =
                VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: {
            /* builds run on the gpu: the wire carries no host memory for host commands to read. */
            VkPhysicalDeviceAccelerationStructureFeaturesKHR *x = (void *)s;
            x->accelerationStructure = VK_TRUE;
            x->accelerationStructureCaptureReplay = VK_FALSE;
            x->accelerationStructureIndirectBuild = VK_TRUE;
            x->accelerationStructureHostCommands = VK_FALSE;
            x->descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR:
            ((VkPhysicalDeviceRayQueryFeaturesKHR *)s)->rayQuery = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR: {
            VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *x = (void *)s;
            x->rayTracingMaintenance1 = VK_TRUE;
            x->rayTracingPipelineTraceRaysIndirect2 = VK_FALSE;
            break;
        }
        default:
            break;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures(VkPhysicalDevice physical, VkPhysicalDeviceFeatures *out)
{
    *out = g_features;
    out->textureCompressionBC = physical->bc;
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice physical, VkPhysicalDeviceFeatures2 *out)
{
    GetPhysicalDeviceFeatures(physical, &out->features);
    fill_features(out->pNext);
}

/* ------------------------------------------------------------------ properties. */

static VkSampleCountFlags sample_counts(VkPhysicalDevice p)
{
    VkSampleCountFlags out = VK_SAMPLE_COUNT_1_BIT;
    for (uint32_t s = 2; s <= 8; s *= 2)
        if ([p->mtl supportsTextureSampleCount:s])
            out |= s;
    return out;
}

static uint32_t max_samplers(VkPhysicalDevice p)
{
    return (uint32_t)MIME_MIN(p->mtl.maxArgumentBufferSamplerCount, 4096);
}

#define MAX_DESCRIPTORS (1u << 20)

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice p, VkPhysicalDeviceProperties *out)
{
    VkSampleCountFlags samples = sample_counts(p);
    MTLSize threads = p->mtl.maxThreadsPerThreadgroup;
    uint32_t page = (uint32_t)getpagesize();
    *out = (VkPhysicalDeviceProperties){
        .apiVersion = API_VERSION,
        .driverVersion = DRIVER_VERSION,
        .vendorID = 0x106b,
        .deviceID = 0x4d54,
        .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
        .limits =
            {
                .maxImageDimension1D = 16384,
                .maxImageDimension2D = 16384,
                .maxImageDimension3D = 2048,
                .maxImageDimensionCube = 16384,
                .maxImageArrayLayers = 2048,
                .maxTexelBufferElements = 1u << 28,
                .maxUniformBufferRange = 1u << 30,
                .maxStorageBufferRange = UINT32_MAX,
                .maxPushConstantsSize = VKM_PUSH_SIZE,
                .maxMemoryAllocationCount = 1u << 20,
                .maxSamplerAllocationCount = max_samplers(p),
                .bufferImageGranularity = 1,
                .maxBoundDescriptorSets = VKM_SETS,
                .maxPerStageDescriptorSamplers = max_samplers(p),
                .maxPerStageDescriptorUniformBuffers = MAX_DESCRIPTORS,
                .maxPerStageDescriptorStorageBuffers = MAX_DESCRIPTORS,
                .maxPerStageDescriptorSampledImages = MAX_DESCRIPTORS,
                .maxPerStageDescriptorStorageImages = MAX_DESCRIPTORS,
                .maxPerStageDescriptorInputAttachments = MAX_DESCRIPTORS,
                .maxPerStageResources = UINT32_MAX,
                .maxDescriptorSetSamplers = max_samplers(p),
                .maxDescriptorSetUniformBuffers = MAX_DESCRIPTORS,
                .maxDescriptorSetUniformBuffersDynamic = 64,
                .maxDescriptorSetStorageBuffers = MAX_DESCRIPTORS,
                .maxDescriptorSetStorageBuffersDynamic = 64,
                .maxDescriptorSetSampledImages = MAX_DESCRIPTORS,
                .maxDescriptorSetStorageImages = MAX_DESCRIPTORS,
                .maxDescriptorSetInputAttachments = MAX_DESCRIPTORS,
                .maxVertexInputAttributes = VKM_VERTEX_ATTRIBUTES,
                .maxVertexInputBindings = VKM_VERTEX_BINDINGS - 1, /* the last one gives zeros. */
                .maxVertexInputAttributeOffset = 2047,
                .maxVertexInputBindingStride = 2048,
                .maxVertexOutputComponents = 128,
                .maxFragmentInputComponents = 128,
                .maxFragmentOutputAttachments = VKM_COLORS,
                .maxFragmentDualSrcAttachments = 1,
                .maxFragmentCombinedOutputResources = MAX_DESCRIPTORS,
                .maxGeometryShaderInvocations = 32,
                .maxGeometryInputComponents = 128,
                .maxGeometryOutputComponents = 128,
                .maxGeometryOutputVertices = 1024,
                .maxGeometryTotalOutputComponents = 1024,
                .maxComputeSharedMemorySize = (uint32_t)p->mtl.maxThreadgroupMemoryLength,
                .maxComputeWorkGroupCount = {0x7fffffff, 65535, 65535},
                .maxComputeWorkGroupInvocations = 1024,
                .maxComputeWorkGroupSize = {(uint32_t)threads.width, (uint32_t)threads.height,
                                            (uint32_t)threads.depth},
                .subPixelPrecisionBits = 8,
                .subTexelPrecisionBits = 8,
                .mipmapPrecisionBits = 8,
                .maxDrawIndexedIndexValue = UINT32_MAX,
                .maxDrawIndirectCount = UINT16_MAX,
                .maxSamplerLodBias = 15,
                .maxSamplerAnisotropy = 16,
                .maxViewports = VKM_VIEWPORTS,
                .maxViewportDimensions = {32768, 32768},
                .viewportBoundsRange = {-65536, 65536},
                .viewportSubPixelBits = 8,
                .minMemoryMapAlignment = page,
                .minTexelBufferOffsetAlignment = p->texel_alignment,
                .minUniformBufferOffsetAlignment = 16,
                .minStorageBufferOffsetAlignment = 16,
                .minTexelOffset = -8,
                .maxTexelOffset = 7,
                .minTexelGatherOffset = -8,
                .maxTexelGatherOffset = 7,
                .minInterpolationOffset = -0.5f,
                .maxInterpolationOffset = 0.4375f,
                .subPixelInterpolationOffsetBits = 4,
                .maxFramebufferWidth = 16384,
                .maxFramebufferHeight = 16384,
                .maxFramebufferLayers = 2048,
                .framebufferColorSampleCounts = samples,
                .framebufferDepthSampleCounts = samples,
                .framebufferStencilSampleCounts = samples,
                .framebufferNoAttachmentsSampleCounts = samples,
                .maxColorAttachments = VKM_COLORS,
                .sampledImageColorSampleCounts = samples,
                .sampledImageIntegerSampleCounts = samples,
                .sampledImageDepthSampleCounts = samples,
                .sampledImageStencilSampleCounts = samples,
                .storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
                .maxSampleMaskWords = 1,
                .timestampComputeAndGraphics = VK_TRUE,
                .timestampPeriod = 1.0f,
                .maxClipDistances = 8,
                .maxCullDistances = 0,
                .maxCombinedClipAndCullDistances = 8,
                .discreteQueuePriorities = 2,
                .pointSizeRange = {1.0f, 511.0f},
                .lineWidthRange = {1.0f, 1.0f},
                .pointSizeGranularity = 0.0625f,
                .lineWidthGranularity = 0.0f,
                .strictLines = VK_FALSE,
                .standardSampleLocations = VK_TRUE,
                .optimalBufferCopyOffsetAlignment = 16,
                .optimalBufferCopyRowPitchAlignment = 16,
                .nonCoherentAtomSize = 64,
            },
    };
    mime_strlcpy(out->deviceName, p->name, sizeof(out->deviceName));
    memcpy(out->pipelineCacheUUID, p->uuid, VK_UUID_SIZE);
}

static void fill_properties(VkPhysicalDevice p, VkBaseOutStructure *chain)
{
    VkSampleCountFlags samples = sample_counts(p);
    uint64_t max_buffer = p->mtl.maxBufferLength;
    VkSubgroupFeatureFlags subgroup_ops = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                                          VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                          VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                                          VK_SUBGROUP_FEATURE_QUAD_BIT;
    for (VkBaseOutStructure *s = chain; s; s = s->pNext) {
        switch (s->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
            VkPhysicalDeviceVulkan11Properties *x = (void *)s;
            memcpy(x->deviceUUID, p->uuid, VK_UUID_SIZE);
            memcpy(x->driverUUID, "mime metal driver", VK_UUID_SIZE);
            x->deviceNodeMask = 1;
            x->deviceLUIDValid = VK_FALSE;
            x->subgroupSize = 32;
            x->subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            x->subgroupSupportedOperations = subgroup_ops;
            x->subgroupQuadOperationsInAllStages = VK_FALSE;
            x->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            x->maxMultiviewViewCount = 1;
            x->maxMultiviewInstanceIndex = UINT32_MAX;
            x->protectedNoFault = VK_FALSE;
            x->maxPerSetDescriptors = UINT32_MAX;
            x->maxMemoryAllocationSize = max_buffer;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
            VkPhysicalDeviceIDProperties *x = (void *)s;
            memcpy(x->deviceUUID, p->uuid, VK_UUID_SIZE);
            memcpy(x->driverUUID, "mime metal driver", VK_UUID_SIZE);
            x->deviceNodeMask = 1;
            x->deviceLUIDValid = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
            VkPhysicalDeviceMaintenance3Properties *x = (void *)s;
            x->maxPerSetDescriptors = UINT32_MAX;
            x->maxMemoryAllocationSize = max_buffer;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
            VkPhysicalDeviceSubgroupProperties *x = (void *)s;
            x->subgroupSize = 32;
            x->supportedStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            x->supportedOperations = subgroup_ops;
            x->quadOperationsInAllStages = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES: {
            VkPhysicalDeviceVulkan12Properties v = {
                .driverID = VK_DRIVER_ID_MOLTENVK,
                .conformanceVersion = {1, 3, 0, 0},
                .denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE,
                .roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE,
                .shaderSignedZeroInfNanPreserveFloat16 = VK_TRUE,
                .shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE,
                .shaderRoundingModeRTEFloat16 = VK_TRUE,
                .shaderRoundingModeRTEFloat32 = VK_TRUE,
                .maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX,
                .shaderUniformBufferArrayNonUniformIndexingNative = VK_TRUE,
                .shaderSampledImageArrayNonUniformIndexingNative = VK_TRUE,
                .shaderStorageBufferArrayNonUniformIndexingNative = VK_TRUE,
                .shaderStorageImageArrayNonUniformIndexingNative = VK_TRUE,
                .shaderInputAttachmentArrayNonUniformIndexingNative = VK_TRUE,
                .robustBufferAccessUpdateAfterBind = VK_TRUE,
                .quadDivergentImplicitLod = VK_FALSE,
                .maxPerStageDescriptorUpdateAfterBindSamplers = max_samplers(p),
                .maxPerStageDescriptorUpdateAfterBindUniformBuffers = MAX_DESCRIPTORS,
                .maxPerStageDescriptorUpdateAfterBindStorageBuffers = MAX_DESCRIPTORS,
                .maxPerStageDescriptorUpdateAfterBindSampledImages = MAX_DESCRIPTORS,
                .maxPerStageDescriptorUpdateAfterBindStorageImages = MAX_DESCRIPTORS,
                .maxPerStageDescriptorUpdateAfterBindInputAttachments = MAX_DESCRIPTORS,
                .maxPerStageUpdateAfterBindResources = UINT32_MAX,
                .maxDescriptorSetUpdateAfterBindSamplers = max_samplers(p),
                .maxDescriptorSetUpdateAfterBindUniformBuffers = MAX_DESCRIPTORS,
                .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 0,
                .maxDescriptorSetUpdateAfterBindStorageBuffers = MAX_DESCRIPTORS,
                .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 0,
                .maxDescriptorSetUpdateAfterBindSampledImages = MAX_DESCRIPTORS,
                .maxDescriptorSetUpdateAfterBindStorageImages = MAX_DESCRIPTORS,
                .maxDescriptorSetUpdateAfterBindInputAttachments = MAX_DESCRIPTORS,
                .supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT | VK_RESOLVE_MODE_MIN_BIT |
                                              VK_RESOLVE_MODE_MAX_BIT,
                .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
                .independentResolveNone = VK_TRUE,
                .independentResolve = VK_TRUE,
                .filterMinmaxSingleComponentFormats = VK_TRUE,
                .filterMinmaxImageComponentMapping = VK_TRUE,
                .maxTimelineSemaphoreValueDifference = UINT64_MAX,
                .framebufferIntegerColorSampleCounts = samples,
            };
            mime_strlcpy(v.driverName, "mime metal", sizeof(v.driverName));
            mime_strlcpy(v.driverInfo, "Metal 4", sizeof(v.driverInfo));
            if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES) {
                COPY_FEATURES((VkPhysicalDeviceVulkan12Properties *)s, &v, driverID);
            } else if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
                VkPhysicalDeviceDriverProperties *x = (void *)s;
                x->driverID = v.driverID;
                memcpy(x->driverName, v.driverName, sizeof(x->driverName));
                memcpy(x->driverInfo, v.driverInfo, sizeof(x->driverInfo));
                x->conformanceVersion = v.conformanceVersion;
            } else if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES) {
                COPY_FEATURES((VkPhysicalDeviceDescriptorIndexingProperties *)s, &v,
                              maxUpdateAfterBindDescriptorsInAllPools);
            } else if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES) {
                ((VkPhysicalDeviceTimelineSemaphoreProperties *)s)->maxTimelineSemaphoreValueDifference = UINT64_MAX;
            } else if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES) {
                VkPhysicalDeviceDepthStencilResolveProperties *x = (void *)s;
                x->supportedDepthResolveModes = v.supportedDepthResolveModes;
                x->supportedStencilResolveModes = v.supportedStencilResolveModes;
                x->independentResolveNone = x->independentResolve = VK_TRUE;
            } else if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES) {
                COPY_FEATURES((VkPhysicalDeviceFloatControlsProperties *)s, &v, denormBehaviorIndependence);
                VkPhysicalDeviceFloatControlsProperties *x = (void *)s;
                x->shaderSignedZeroInfNanPreserveFloat64 = VK_FALSE;
            } else {
                VkPhysicalDeviceSamplerFilterMinmaxProperties *x = (void *)s;
                x->filterMinmaxSingleComponentFormats = x->filterMinmaxImageComponentMapping = VK_TRUE;
            }
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES: {
            VkPhysicalDeviceVulkan13Properties *x = (void *)s;
            x->minSubgroupSize = x->maxSubgroupSize = 32;
            x->maxComputeWorkgroupSubgroups = 1024 / 32;
            x->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
            x->maxInlineUniformBlockSize = 256;
            x->maxPerStageDescriptorInlineUniformBlocks = 32;
            x->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = 32;
            x->maxDescriptorSetInlineUniformBlocks = 32;
            x->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = 32;
            x->maxInlineUniformTotalSize = 1 << 16;
            x->storageTexelBufferOffsetAlignmentBytes = p->texel_alignment;
            x->storageTexelBufferOffsetSingleTexelAlignment = VK_FALSE;
            x->uniformTexelBufferOffsetAlignmentBytes = p->texel_alignment;
            x->uniformTexelBufferOffsetSingleTexelAlignment = VK_FALSE;
            x->maxBufferSize = max_buffer;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES:
            ((VkPhysicalDeviceMaintenance4Properties *)s)->maxBufferSize = max_buffer;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES: {
            VkPhysicalDeviceTexelBufferAlignmentProperties *x = (void *)s;
            x->storageTexelBufferOffsetAlignmentBytes = x->uniformTexelBufferOffsetAlignmentBytes = p->texel_alignment;
            x->storageTexelBufferOffsetSingleTexelAlignment = x->uniformTexelBufferOffsetSingleTexelAlignment = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES: {
            VkPhysicalDeviceSubgroupSizeControlProperties *x = (void *)s;
            x->minSubgroupSize = x->maxSubgroupSize = 32;
            x->maxComputeWorkgroupSubgroups = 1024 / 32;
            x->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES:
            ((VkPhysicalDevicePointClippingProperties *)s)->pointClippingBehavior =
                VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
            VkPhysicalDeviceMultiviewProperties *x = (void *)s;
            x->maxMultiviewViewCount = 1;
            x->maxMultiviewInstanceIndex = UINT32_MAX;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT:
            ((VkPhysicalDeviceExternalMemoryHostPropertiesEXT *)s)->minImportedHostPointerAlignment =
                (VkDeviceSize)getpagesize();
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
            VkPhysicalDeviceRobustness2PropertiesEXT *x = (void *)s;
            x->robustStorageBufferAccessSizeAlignment = 4;
            x->robustUniformBufferAccessSizeAlignment = 4;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT: {
            /*
             * every descriptor is the 8 bytes of a set's argument buffer slot. a combined image sampler is two, and an
             * array of them is its images then its samplers, as single array false asks.
             */
            VkPhysicalDeviceDescriptorBufferPropertiesEXT *x = (void *)s;
            x->combinedImageSamplerDescriptorSingleArray = VK_FALSE;
            x->bufferlessPushDescriptors = VK_FALSE;
            x->allowSamplerImageViewPostSubmitCreation = VK_TRUE;
            x->descriptorBufferOffsetAlignment = 64;
            x->maxDescriptorBufferBindings = VKM_SETS;
            x->maxResourceDescriptorBufferBindings = VKM_SETS;
            x->maxSamplerDescriptorBufferBindings = VKM_SETS;
            x->maxEmbeddedImmutableSamplerBindings = VKM_SETS;
            x->maxEmbeddedImmutableSamplers = 2032;
            x->bufferCaptureReplayDescriptorDataSize = x->imageCaptureReplayDescriptorDataSize = 0;
            x->imageViewCaptureReplayDescriptorDataSize = x->samplerCaptureReplayDescriptorDataSize = 0;
            x->accelerationStructureCaptureReplayDescriptorDataSize = 0;
            x->samplerDescriptorSize = 8;
            x->combinedImageSamplerDescriptorSize = 16;
            x->sampledImageDescriptorSize = x->storageImageDescriptorSize = 8;
            x->uniformTexelBufferDescriptorSize = x->robustUniformTexelBufferDescriptorSize = 8;
            x->storageTexelBufferDescriptorSize = x->robustStorageTexelBufferDescriptorSize = 8;
            x->uniformBufferDescriptorSize = x->robustUniformBufferDescriptorSize = 8;
            x->storageBufferDescriptorSize = x->robustStorageBufferDescriptorSize = 8;
            x->inputAttachmentDescriptorSize = 8;
            x->accelerationStructureDescriptorSize = 8;
            x->maxSamplerDescriptorBufferRange = x->maxResourceDescriptorBufferRange = 1ull << 32;
            x->samplerDescriptorBufferAddressSpaceSize = x->resourceDescriptorBufferAddressSpaceSize = 1ull << 40;
            x->descriptorBufferAddressSpaceSize = 1ull << 40;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT: {
            VkPhysicalDeviceShaderObjectPropertiesEXT *x = (void *)s;
            memcpy(x->shaderBinaryUUID, p->uuid, VK_UUID_SIZE);
            x->shaderBinaryVersion = 1;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_KHR: {
            VkPhysicalDeviceVertexAttributeDivisorPropertiesKHR *x = (void *)s;
            x->maxVertexAttribDivisor = UINT32_MAX;
            x->supportsNonZeroFirstInstance = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT:
            ((VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)s)->maxVertexAttribDivisor = UINT32_MAX;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT:
            ((VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *)s)->dynamicPrimitiveTopologyUnrestricted = VK_TRUE;
            break;
        /* stream output is one stream, written by the geometry stage's emulation. */
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
            VkPhysicalDeviceTransformFeedbackPropertiesEXT *x = (void *)s;
            x->maxTransformFeedbackStreams = 1;
            x->maxTransformFeedbackBuffers = 4;
            x->maxTransformFeedbackBufferSize = 1ull << 31;
            x->maxTransformFeedbackStreamDataSize = 512;
            x->maxTransformFeedbackBufferDataSize = 512;
            x->maxTransformFeedbackBufferDataStride = 2048;
            x->transformFeedbackQueries = VK_TRUE;
            x->transformFeedbackStreamsLinesTriangles = VK_FALSE;
            x->transformFeedbackRasterizationStreamSelect = VK_FALSE;
            x->transformFeedbackDraw = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES: {
            VkPhysicalDeviceMaintenance5Properties *x = (void *)s;
            x->earlyFragmentMultisampleCoverageAfterSampleCounting = VK_TRUE;
            x->earlyFragmentSampleMaskTestBeforeSampleCounting = VK_TRUE;
            x->depthStencilSwizzleOneSupport = VK_FALSE;
            x->polygonModePointSize = VK_TRUE;
            x->nonStrictSinglePixelWideLinesUseParallelogram = VK_FALSE;
            x->nonStrictWideLinesUseParallelogram = VK_FALSE;
            break;
        }
        /* generated commands are encoded into indirect command buffers, without execution sets. */
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR: {
            /* metal's limits without its extended ones, which shaders would have to ask for. */
            VkPhysicalDeviceAccelerationStructurePropertiesKHR *x = (void *)s;
            x->maxGeometryCount = 1u << 24;
            x->maxInstanceCount = 1u << 24;
            x->maxPrimitiveCount = 1u << 28;
            x->maxPerStageDescriptorAccelerationStructures = 1u << 20;
            x->maxPerStageDescriptorUpdateAfterBindAccelerationStructures = 1u << 20;
            x->maxDescriptorSetAccelerationStructures = 1u << 20;
            x->maxDescriptorSetUpdateAfterBindAccelerationStructures = 1u << 20;
            x->minAccelerationStructureScratchOffsetAlignment = 256;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT: {
            VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *x = (void *)s;
            x->maxIndirectPipelineCount = x->maxIndirectShaderObjectCount = 0;
            x->maxIndirectSequenceCount = 1u << 20;
            x->maxIndirectCommandsTokenCount = VKM_DGC_TOKENS;
            x->maxIndirectCommandsTokenOffset = 2047;
            x->maxIndirectCommandsIndirectStride = 2048;
            x->supportedIndirectCommandsInputModes =
                VK_INDIRECT_COMMANDS_INPUT_MODE_VULKAN_INDEX_BUFFER_EXT | VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT;
            x->supportedIndirectCommandsShaderStages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            x->supportedIndirectCommandsShaderStagesPipelineBinding = 0;
            x->supportedIndirectCommandsShaderStagesShaderBinding = 0;
            x->deviceGeneratedCommandsTransformFeedback = VK_FALSE;
            x->deviceGeneratedCommandsMultiDrawIndirectCount = VK_FALSE;
            break;
        }
        default:
            break;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties2(VkPhysicalDevice physical,
                                                              VkPhysicalDeviceProperties2 *out)
{
    GetPhysicalDeviceProperties(physical, &out->properties);
    fill_properties(physical, out->pNext);
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physical, uint32_t *count,
                                                                        VkQueueFamilyProperties *out)
{
    static const VkQueueFamilyProperties family = {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
                                                   1, 64, {1, 1, 1}};
    enumerate(&family, sizeof(family), 1, count, out);
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physical, uint32_t *count,
                                                                         VkQueueFamilyProperties2 *out)
{
    if (!out) {
        *count = 1;
        return;
    }
    if (!*count)
        return;
    uint32_t one = 1;
    GetPhysicalDeviceQueueFamilyProperties(physical, &one, &out[0].queueFamilyProperties);
    *count = 1;
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceMemoryProperties(VkPhysicalDevice p,
                                                                   VkPhysicalDeviceMemoryProperties *out)
{
    *out = (VkPhysicalDeviceMemoryProperties){
        .memoryTypeCount = VKM_MEMORY_TYPES,
        .memoryTypes =
            {
                [VKM_MEMORY_DEVICE] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0},
                [VKM_MEMORY_HOST] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                     0},
            },
        .memoryHeapCount = 1,
        .memoryHeaps = {{p->memory_size, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT}},
    };
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice p,
                                                                    VkPhysicalDeviceMemoryProperties2 *out)
{
    GetPhysicalDeviceMemoryProperties(p, &out->memoryProperties);
    VkPhysicalDeviceMemoryBudgetPropertiesEXT *budget =
        (void *)vkm_find(out->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
    if (budget) {
        memset(budget->heapBudget, 0, sizeof(budget->heapBudget));
        memset(budget->heapUsage, 0, sizeof(budget->heapUsage));
        budget->heapBudget[0] = p->memory_size;
        budget->heapUsage[0] = p->mtl.currentAllocatedSize;
    }
}

/* ------------------------------------------------------------------ formats. */

static void format_features(VkPhysicalDevice p, VkFormat format, VkFormatFeatureFlags2 *linear,
                            VkFormatFeatureFlags2 *optimal, VkFormatFeatureFlags2 *buffer)
{
    const VkmFormat *f = vkm_format(format);
    *linear = vkm_format_features(f, true, false);
    *optimal = vkm_format_features(f, false, false);
    *buffer = vkm_format_features(f, false, true);
    if (f->compressed && !p->bc && f->pixel >= MTLPixelFormatBC1_RGBA && f->pixel <= MTLPixelFormatBC7_RGBAUnorm_sRGB)
        *linear = *optimal = 0;
    if (!p->float_filter && (f->caps & VKM_CAP_STORAGE) && f->bytes >= 4 &&
        (format == VK_FORMAT_R32_SFLOAT || format == VK_FORMAT_R32G32_SFLOAT ||
         format == VK_FORMAT_R32G32B32A32_SFLOAT)) {
        *linear &= ~VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        *optimal &= ~VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFormatProperties(VkPhysicalDevice p, VkFormat format,
                                                                   VkFormatProperties *out)
{
    VkFormatFeatureFlags2 linear, optimal, buffer;
    format_features(p, format, &linear, &optimal, &buffer);
    out->linearTilingFeatures = (VkFormatFeatureFlags)linear;
    out->optimalTilingFeatures = (VkFormatFeatureFlags)optimal;
    out->bufferFeatures = (VkFormatFeatureFlags)buffer;
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFormatProperties2(VkPhysicalDevice p, VkFormat format,
                                                                    VkFormatProperties2 *out)
{
    VkFormatFeatureFlags2 linear, optimal, buffer;
    format_features(p, format, &linear, &optimal, &buffer);
    out->formatProperties.linearTilingFeatures = (VkFormatFeatureFlags)linear;
    out->formatProperties.optimalTilingFeatures = (VkFormatFeatureFlags)optimal;
    out->formatProperties.bufferFeatures = (VkFormatFeatureFlags)buffer;
    VkFormatProperties3 *three = (void *)vkm_find(out->pNext, VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3);
    if (three) {
        three->linearTilingFeatures = linear;
        three->optimalTilingFeatures = optimal;
        three->bufferFeatures = buffer;
    }
}

static VkResult image_format(VkPhysicalDevice p, VkFormat format, VkImageType type, VkImageTiling tiling,
                             VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *out)
{
    memset(out, 0, sizeof(*out));
    VkFormatFeatureFlags2 linear, optimal, buffer;
    format_features(p, format, &linear, &optimal, &buffer);
    VkFormatFeatureFlags2 features = tiling == VK_IMAGE_TILING_LINEAR ? linear : optimal;
    if (!features)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    struct {
        VkImageUsageFlags usage;
        VkFormatFeatureFlags2 need;
    } static const needs[] = {
        {VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT},
        {VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT},
        {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT},
        {VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT},
        {VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT},
    };
    /* views of other formats may give the usage the image's own format lacks. */
    if (!(flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
        for (size_t i = 0; i < MIME_ARRAY_COUNT(needs); i++)
            if ((usage & needs[i].usage) && !(features & needs[i].need))
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
    const VkmFormat *f = vkm_format(format);
    if (type == VK_IMAGE_TYPE_3D && (f->caps & VKM_CAP_DEPTH))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (tiling == VK_IMAGE_TILING_LINEAR && type != VK_IMAGE_TYPE_2D)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint32_t dim = type == VK_IMAGE_TYPE_3D ? 2048 : 16384;
    out->maxExtent = (VkExtent3D){dim, type == VK_IMAGE_TYPE_1D ? 1 : dim, type == VK_IMAGE_TYPE_3D ? dim : 1};
    out->maxMipLevels = tiling == VK_IMAGE_TILING_LINEAR ? 1 : 15;
    out->maxArrayLayers = type == VK_IMAGE_TYPE_3D || tiling == VK_IMAGE_TILING_LINEAR ? 1 : 2048;
    out->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    if (type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL && (f->caps & VKM_CAP_MSAA) &&
        !(flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && !(usage & VK_IMAGE_USAGE_STORAGE_BIT))
        out->sampleCounts = sample_counts(p);
    out->maxResourceSize = p->mtl.maxBufferLength;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice p, VkFormat format,
                                                                            VkImageType type, VkImageTiling tiling,
                                                                            VkImageUsageFlags usage,
                                                                            VkImageCreateFlags flags,
                                                                            VkImageFormatProperties *out)
{
    return image_format(p, format, type, tiling, usage, flags, out);
}

static const VkExternalMemoryHandleTypeFlags g_shared_memory = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
                                                               VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT |
                                                               VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT;

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice p,
                                                                             const VkPhysicalDeviceImageFormatInfo2 *info,
                                                                             VkImageFormatProperties2 *out)
{
    VkResult r = image_format(p, info->format, info->type, info->tiling, info->usage, info->flags,
                              &out->imageFormatProperties);
    if (r != VK_SUCCESS)
        return r;
    const VkPhysicalDeviceExternalImageFormatInfo *external =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
    VkExternalImageFormatProperties *props = (void *)vkm_find(out->pNext, VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES);
    if (external && external->handleType) {
        if (!(external->handleType & g_shared_memory))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        if (props)
            props->externalMemoryProperties = (VkExternalMemoryProperties){
                VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
                g_shared_memory, g_shared_memory};
    }
    VkSamplerYcbcrConversionImageFormatProperties *ycbcr =
        (void *)vkm_find(out->pNext, VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES);
    if (ycbcr)
        ycbcr->combinedImageSamplerDescriptorCount = 1;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice p, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
    VkImageTiling tiling, uint32_t *count, VkSparseImageFormatProperties *out)
{
    *count = 0;
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice p, const VkPhysicalDeviceSparseImageFormatInfo2 *info, uint32_t *count,
    VkSparseImageFormatProperties2 *out)
{
    *count = 0;
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice p, const VkPhysicalDeviceExternalBufferInfo *info, VkExternalBufferProperties *out)
{
    VkExternalMemoryProperties *m = &out->externalMemoryProperties;
    *m = (VkExternalMemoryProperties){};
    if (info->handleType & g_shared_memory)
        *m = (VkExternalMemoryProperties){
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT, g_shared_memory,
            g_shared_memory};
    else if (info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT)
        *m = (VkExternalMemoryProperties){VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT, 0,
                                          VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice p, const VkPhysicalDeviceExternalSemaphoreInfo *info, VkExternalSemaphoreProperties *out)
{
    out->exportFromImportedHandleTypes = out->compatibleHandleTypes = out->externalSemaphoreFeatures = 0;
    if (info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) {
        out->exportFromImportedHandleTypes = out->compatibleHandleTypes = info->handleType;
        out->externalSemaphoreFeatures =
            VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice p, const VkPhysicalDeviceExternalFenceInfo *info, VkExternalFenceProperties *out)
{
    out->exportFromImportedHandleTypes = out->compatibleHandleTypes = out->externalFenceFeatures = 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceToolProperties(VkPhysicalDevice p, uint32_t *count,
                                                                     VkPhysicalDeviceToolProperties *out)
{
    *count = 0;
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ the entry points of this file. */

#define E VKM_ENTRY
const VkmEntry vkm_instance_entries[] = {
    E(EnumerateInstanceVersion),
    E(EnumerateInstanceExtensionProperties),
    E(EnumerateInstanceLayerProperties),
    E(EnumerateDeviceExtensionProperties),
    E(EnumerateDeviceLayerProperties),
    E(CreateInstance),
    E(DestroyInstance),
    E(EnumeratePhysicalDevices),
    E(EnumeratePhysicalDeviceGroups),
    VKM_ALIAS("vkEnumeratePhysicalDeviceGroupsKHR", EnumeratePhysicalDeviceGroups),
    E(GetPhysicalDeviceFeatures),
    E(GetPhysicalDeviceFeatures2),
    VKM_ALIAS("vkGetPhysicalDeviceFeatures2KHR", GetPhysicalDeviceFeatures2),
    E(GetPhysicalDeviceProperties),
    E(GetPhysicalDeviceProperties2),
    VKM_ALIAS("vkGetPhysicalDeviceProperties2KHR", GetPhysicalDeviceProperties2),
    E(GetPhysicalDeviceQueueFamilyProperties),
    E(GetPhysicalDeviceQueueFamilyProperties2),
    VKM_ALIAS("vkGetPhysicalDeviceQueueFamilyProperties2KHR", GetPhysicalDeviceQueueFamilyProperties2),
    E(GetPhysicalDeviceMemoryProperties),
    E(GetPhysicalDeviceMemoryProperties2),
    VKM_ALIAS("vkGetPhysicalDeviceMemoryProperties2KHR", GetPhysicalDeviceMemoryProperties2),
    E(GetPhysicalDeviceFormatProperties),
    E(GetPhysicalDeviceFormatProperties2),
    VKM_ALIAS("vkGetPhysicalDeviceFormatProperties2KHR", GetPhysicalDeviceFormatProperties2),
    E(GetPhysicalDeviceImageFormatProperties),
    E(GetPhysicalDeviceImageFormatProperties2),
    VKM_ALIAS("vkGetPhysicalDeviceImageFormatProperties2KHR", GetPhysicalDeviceImageFormatProperties2),
    E(GetPhysicalDeviceSparseImageFormatProperties),
    E(GetPhysicalDeviceSparseImageFormatProperties2),
    E(GetPhysicalDeviceExternalBufferProperties),
    VKM_ALIAS("vkGetPhysicalDeviceExternalBufferPropertiesKHR", GetPhysicalDeviceExternalBufferProperties),
    E(GetPhysicalDeviceExternalSemaphoreProperties),
    {"vkGetPhysicalDeviceExternalSemaphorePropertiesKHR",
     (PFN_vkVoidFunction)GetPhysicalDeviceExternalSemaphoreProperties},
    E(GetPhysicalDeviceExternalFenceProperties),
    VKM_ALIAS("vkGetPhysicalDeviceExternalFencePropertiesKHR", GetPhysicalDeviceExternalFenceProperties),
    E(GetPhysicalDeviceToolProperties),
    {nullptr, nullptr},
};
