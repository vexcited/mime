/*
 * the host's video decoder, see video.h: capabilities, sessions and the decode of one picture at a time, in
 * the order of the connection's calls. the picture buffers are the guest's images; everything else, the
 * bitstream memory, the session and its parameters, belongs to the connection and goes with its device.
 */
#include "video.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the connection whose call a thread runs, which the image creation hook sees. */
static __thread MvkConnection *t_connection;

void mvk_video_enter(MvkConnection *c) { t_connection = c; }

static PFN_vkCreateImage g_create_image;

/* ------------------------------------------------------------------ profiles */

typedef struct Profile {
    VkVideoProfileInfoKHR info;
    VkVideoDecodeUsageInfoKHR usage;
    union {
        VkVideoDecodeH264ProfileInfoKHR h264;
        VkVideoDecodeH265ProfileInfoKHR h265;
        VkVideoDecodeVP9ProfileInfoKHR vp9;
        VkVideoDecodeAV1ProfileInfoKHR av1;
    } u;
} Profile;

/* the decoding profiles offered: what a dxva decoder profile guid stands for. */
static const struct {
    uint32_t codec, profile, bit_depth;
} g_profiles[] = {
    {MVK_VIDEO_CODEC_H264, MVK_VIDEO_H264_HIGH, 8},
    {MVK_VIDEO_CODEC_H265, MVK_VIDEO_H265_MAIN, 8},
    {MVK_VIDEO_CODEC_H265, MVK_VIDEO_H265_MAIN10, 10},
    {MVK_VIDEO_CODEC_VP9, 0, 8},
    {MVK_VIDEO_CODEC_VP9, 2, 10},
    {MVK_VIDEO_CODEC_AV1, 0, 8},
    {MVK_VIDEO_CODEC_AV1, 0, 10},
};
#define PROFILE_COUNT (sizeof(g_profiles) / sizeof(g_profiles[0]))

static void make_profile(Profile *p, uint32_t codec, uint32_t profile, uint32_t bit_depth)
{
    memset(p, 0, sizeof(*p));
    p->usage = (VkVideoDecodeUsageInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR, nullptr,
                                           VK_VIDEO_DECODE_USAGE_DEFAULT_KHR};
    p->info = (VkVideoProfileInfoKHR){VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR};
    p->info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    p->info.lumaBitDepth = p->info.chromaBitDepth =
        bit_depth == 10 ? VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR : VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    switch (codec) {
    case MVK_VIDEO_CODEC_H264:
        p->info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
        p->u.h264 = (VkVideoDecodeH264ProfileInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR, &p->usage,
                                                      (StdVideoH264ProfileIdc)profile,
                                                      VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR};
        p->info.pNext = &p->u.h264;
        break;
    case MVK_VIDEO_CODEC_H265:
        p->info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
        p->u.h265 = (VkVideoDecodeH265ProfileInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR, &p->usage,
                                                      (StdVideoH265ProfileIdc)profile};
        p->info.pNext = &p->u.h265;
        break;
    case MVK_VIDEO_CODEC_VP9:
        p->info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR;
        p->u.vp9 = (VkVideoDecodeVP9ProfileInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PROFILE_INFO_KHR, &p->usage,
                                                    (StdVideoVP9Profile)profile};
        p->info.pNext = &p->u.vp9;
        break;
    case MVK_VIDEO_CODEC_AV1:
        p->info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR;
        /* film grain is applied by the decoder: the guest's pictures are the output with the grain. */
        p->u.av1 = (VkVideoDecodeAV1ProfileInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_KHR, &p->usage,
                                                    (StdVideoAV1Profile)profile, VK_TRUE};
        p->info.pNext = &p->u.av1;
        break;
    }
}

static VkVideoCodecOperationFlagBitsKHR codec_operation(uint32_t codec)
{
    return codec == MVK_VIDEO_CODEC_H264   ? VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
           : codec == MVK_VIDEO_CODEC_H265 ? VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR
           : codec == MVK_VIDEO_CODEC_VP9  ? VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR
                                           : VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR;
}

static VkFormat picture_format(uint32_t bit_depth)
{
    return bit_depth == 10 ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 : VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
}

/* what the gpu says of a profile, false when it cannot decode it into the usual picture format. */
static bool query_caps(MvkConnection *c, uint32_t codec, uint32_t profile, uint32_t bit_depth, VkVideoCapabilitiesKHR *caps,
                       VkVideoDecodeCapabilityFlagsKHR *flags, StdVideoH265LevelIdc *h265_level)
{
    MvkVideo *v = c->video;
    Profile p;
    make_profile(&p, codec, profile, bit_depth);
    VkVideoDecodeH264CapabilitiesKHR h264 = {VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR};
    VkVideoDecodeH265CapabilitiesKHR h265 = {VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR};
    VkVideoDecodeVP9CapabilitiesKHR vp9 = {VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_CAPABILITIES_KHR};
    VkVideoDecodeAV1CapabilitiesKHR av1 = {VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_CAPABILITIES_KHR};
    VkVideoDecodeCapabilitiesKHR decode = {VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR};
    decode.pNext = codec == MVK_VIDEO_CODEC_H264   ? (void *)&h264
                   : codec == MVK_VIDEO_CODEC_H265 ? (void *)&h265
                   : codec == MVK_VIDEO_CODEC_VP9  ? (void *)&vp9
                                                   : (void *)&av1;
    *caps = (VkVideoCapabilitiesKHR){VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR, &decode};
    if (v->fn.get_caps(c->physical_device, &p.info, caps) != VK_SUCCESS)
        return false;
    caps->pNext = nullptr;
    /* the decoded pictures are the references: they must be one image, in the format the guest makes. */
    if (!(decode.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR))
        return false;
    *flags = decode.flags;
    *h265_level = h265.maxLevelIdc;
    VkVideoProfileListInfoKHR list = {VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR, nullptr, 1, &p.info};
    VkPhysicalDeviceVideoFormatInfoKHR info = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR, &list,
                                               VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
                                                   VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkVideoFormatPropertiesKHR formats[8];
    uint32_t count = 8;
    for (uint32_t i = 0; i < count; i++)
        formats[i] = (VkVideoFormatPropertiesKHR){VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR};
    if (v->fn.get_formats(c->physical_device, &info, &count, formats) < 0)
        return false;
    for (uint32_t i = 0; i < count; i++)
        if (formats[i].format == picture_format(bit_depth) && formats[i].imageTiling == VK_IMAGE_TILING_OPTIMAL)
            return true;
    return false;
}

/* ------------------------------------------------------------------ the connection's video state */

static bool has_device_extension(MvkConnection *c, VkPhysicalDevice physical, const char *name)
{
    uint32_t count = 0;
    if (c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    bool found = false;
    if (list && c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, list) >= 0)
        for (uint32_t i = 0; i < count && !found; i++)
            found = !strcmp(list[i].extensionName, name);
    free(list);
    return found;
}

/* the extensions decoding needs of any gpu, and those of each codec, which a gpu may have of some only. */
static const char *const g_core_extensions[] = {
    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
};

static const struct {
    uint32_t codec;
    const char *name;
} g_codec_extensions[] = {
    {MVK_VIDEO_CODEC_H264, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME},
    {MVK_VIDEO_CODEC_H265, VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME},
    {MVK_VIDEO_CODEC_VP9, VK_KHR_VIDEO_DECODE_VP9_EXTENSION_NAME},
    {MVK_VIDEO_CODEC_AV1, VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME},
};

/* the queue family that decodes the codecs offered, ~0 when none. */
static uint32_t decode_family(MvkConnection *c, VkPhysicalDevice physical, VkVideoCodecOperationFlagsKHR ops);

/* the codecs the gpu has the extensions for, as a mask of 1 << codec. */
static uint32_t gpu_codecs(MvkConnection *c, VkPhysicalDevice physical)
{
    uint32_t codecs = 0;
    for (size_t i = 0; i < sizeof(g_codec_extensions) / sizeof(g_codec_extensions[0]); i++)
        if (has_device_extension(c, physical, g_codec_extensions[i].name))
            codecs |= 1u << g_codec_extensions[i].codec;
    return codecs;
}

static VkVideoCodecOperationFlagsKHR codec_operations(uint32_t codecs)
{
    VkVideoCodecOperationFlagsKHR ops = 0;
    for (uint32_t codec = 0; codec < 32; codec++)
        if (codecs & (1u << codec))
            ops |= codec_operation(codec);
    return ops;
}

uint32_t mvk_video_queue_family(MvkConnection *c, VkPhysicalDevice physical)
{
    return decode_family(c, physical, codec_operations(gpu_codecs(c, physical)));
}

/* whether the gpu decodes video, which the guest sees as one extension. */
bool mvk_video_offered(MvkConnection *c, VkPhysicalDevice physical)
{
    if (!c->fn.vkEnumerateDeviceExtensionProperties)
        return false;
    for (size_t i = 0; i < sizeof(g_core_extensions) / sizeof(g_core_extensions[0]); i++)
        if (!has_device_extension(c, physical, g_core_extensions[i]))
            return false;
    return gpu_codecs(c, physical) != 0;
}

size_t mvk_video_extensions(MvkConnection *c, VkPhysicalDevice physical, const char **names, uint32_t *codecs)
{
    size_t n = 0;
    for (size_t i = 0; i < sizeof(g_core_extensions) / sizeof(g_core_extensions[0]); i++)
        names[n++] = g_core_extensions[i];
    *codecs = gpu_codecs(c, physical);
    for (size_t i = 0; i < sizeof(g_codec_extensions) / sizeof(g_codec_extensions[0]); i++)
        if (*codecs & (1u << g_codec_extensions[i].codec))
            names[n++] = g_codec_extensions[i].name;
    return n;
}

#define LOAD(field, name) v->fn.field = (PFN_##name)gdpa(c->device, #name)

/* the queue family that decodes a codec, ~0 when none. */
static uint32_t decode_family(MvkConnection *c, VkPhysicalDevice physical, VkVideoCodecOperationFlagsKHR ops)
{
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 get = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)c->gipa(
        c->instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
    if (!get)
        return ~0u;
    uint32_t count = 0;
    get(physical, &count, nullptr);
    if (count > 32)
        count = 32;
    VkQueueFamilyProperties2 props[32];
    VkQueueFamilyVideoPropertiesKHR video[32];
    for (uint32_t i = 0; i < count; i++) {
        video[i] = (VkQueueFamilyVideoPropertiesKHR){VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR};
        props[i] = (VkQueueFamilyProperties2){VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2, &video[i]};
    }
    get(physical, &count, props);
    for (uint32_t i = 0; i < count; i++)
        if ((props[i].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) &&
            (video[i].videoCodecOperations & ops))
            return i;
    return ~0u;
}

/* called once the device exists with the video extensions: loads the commands, learns what is supported. */
void mvk_video_device_created(MvkConnection *c, PFN_vkGetDeviceProcAddr gdpa, bool unified, bool independent,
                              uint32_t codecs)
{
    MvkVideo *v = calloc(1, sizeof(*v));
    if (!v)
        return;
    LOAD(create_session, vkCreateVideoSessionKHR);
    LOAD(destroy_session, vkDestroyVideoSessionKHR);
    LOAD(session_memory, vkGetVideoSessionMemoryRequirementsKHR);
    LOAD(bind_memory, vkBindVideoSessionMemoryKHR);
    LOAD(create_parameters, vkCreateVideoSessionParametersKHR);
    LOAD(destroy_parameters, vkDestroyVideoSessionParametersKHR);
    LOAD(begin, vkCmdBeginVideoCodingKHR);
    LOAD(end, vkCmdEndVideoCodingKHR);
    LOAD(control, vkCmdControlVideoCodingKHR);
    LOAD(decode, vkCmdDecodeVideoKHR);
    v->fn.get_caps = (PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)c->gipa(c->instance,
                                                                           "vkGetPhysicalDeviceVideoCapabilitiesKHR");
    v->fn.get_formats = (PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR)c->gipa(
        c->instance, "vkGetPhysicalDeviceVideoFormatPropertiesKHR");
    v->unified = unified;
    v->independent = independent;
    v->codecs = codecs;
    if (!v->fn.create_session || !v->fn.begin || !v->fn.decode || !v->fn.get_caps || !v->fn.get_formats) {
        free(v);
        return;
    }
    c->video = v;
    c->fn.vkGetPhysicalDeviceMemoryProperties(c->physical_device, &v->memory);
    uint32_t n = 0;
    for (size_t i = 0; i < PROFILE_COUNT; i++) {
        VkVideoCapabilitiesKHR caps;
        VkVideoDecodeCapabilityFlagsKHR flags = 0;
        StdVideoH265LevelIdc level = STD_VIDEO_H265_LEVEL_IDC_6_2;
        if (!(codecs & (1u << g_profiles[i].codec)))
            continue;
        uint32_t family = decode_family(c, c->physical_device, codec_operation(g_profiles[i].codec));
        if (family == ~0u ||
            !query_caps(c, g_profiles[i].codec, g_profiles[i].profile, g_profiles[i].bit_depth, &caps, &flags, &level))
            continue;
        mime_debug("video: codec %u profile %u at %u bits decodes %ux%u to %ux%u, %u slots, %u references", g_profiles[i].codec,
                   g_profiles[i].profile, g_profiles[i].bit_depth, caps.minCodedExtent.width, caps.minCodedExtent.height,
                   caps.maxCodedExtent.width, caps.maxCodedExtent.height, caps.maxDpbSlots, caps.maxActiveReferencePictures);
        v->supported |= 1u << i;
        v->vk_caps[i] = caps;
        v->decode_flags[i] = flags;
        v->h265_level[i] = level;
        v->caps[i] = (MvkVideoCaps){g_profiles[i].codec,
                                    g_profiles[i].profile,
                                    g_profiles[i].bit_depth,
                                    0,
                                    caps.minCodedExtent.width,
                                    caps.minCodedExtent.height,
                                    caps.maxCodedExtent.width,
                                    caps.maxCodedExtent.height,
                                    caps.maxDpbSlots,
                                    caps.maxActiveReferencePictures,
                                    family,
                                    (uint32_t)caps.minBitstreamBufferOffsetAlignment};
        if (!v->family)
            v->family = family;
        n++;
    }
    /* the decoding goes to one queue family: the first profile's. */
    for (size_t i = 0; i < PROFILE_COUNT; i++)
        if (v->supported & (1u << i)) {
            v->family = v->caps[i].queue_family;
            break;
        }
    if (!n) {
        free(v);
        c->video = nullptr;
        return;
    }
    g_create_image = c->fn.vkCreateImage;
    c->fn.vkCreateImage = mvk_video_create_image;
}

static void free_job(MvkConnection *c, VideoJob *j)
{
    if (j->buffer)
        c->fn.vkDestroyBuffer(c->device, j->buffer, nullptr);
    if (j->memory)
        c->fn.vkFreeMemory(c->device, j->memory, nullptr);
    j->buffer = VK_NULL_HANDLE;
    j->memory = VK_NULL_HANDLE;
    j->map = nullptr;
    j->size = 0;
}

static void free_mirror(MvkConnection *c, VideoPicture *p);

static void destroy_session(MvkConnection *c, VideoSession *s)
{
    MvkVideo *v = c->video;
    for (uint32_t i = 0; i < VIDEO_MAX_INDEX; i++)
        if (s->pictures[i].mirror_image)
            free_mirror(c, &s->pictures[i]);
    if (s->params)
        v->fn.destroy_parameters(c->device, s->params, nullptr);
    if (s->session)
        v->fn.destroy_session(c->device, s->session, nullptr);
    for (uint32_t i = 0; i < s->memory_count; i++)
        c->fn.vkFreeMemory(c->device, s->memories[i], nullptr);
    free(s->memories);
    free(s);
}

/* the device is going: what the sessions hold goes with them, after the queue is done. */
void mvk_video_close(MvkConnection *c)
{
    MvkVideo *v = c->video;
    if (!v)
        return;
    c->fn.vkDeviceWaitIdle(c->device);
    if (v->decoded)
        mime_info("video: %llu pictures decoded by the gpu", (unsigned long long)v->decoded);
    while (v->sessions) {
        VideoSession *s = v->sessions;
        v->sessions = s->next;
        destroy_session(c, s);
    }
    for (uint32_t i = 0; i < VIDEO_RING; i++)
        free_job(c, &v->jobs[i]);
    if (v->pool)
        c->fn.vkDestroyCommandPool(c->device, v->pool, nullptr);
    if (v->timeline)
        c->fn.vkDestroySemaphore(c->device, v->timeline, nullptr);
    free(v);
    c->video = nullptr;
}

/* ------------------------------------------------------------------ images made for decoding */

/*
 * an image with the decode usages gets the profiles it may be decoded with, and is shared by the queue
 * families of the device, which spares the guest the transfers of ownership its graphics queue would need.
 */
VkResult mvk_video_create_image(VkDevice device, const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator,
                                VkImage *image)
{
    MvkConnection *c = t_connection;
    if (!c || !c->video ||
        !(info->usage & (VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR)))
        return g_create_image(device, info, allocator, image);
    MvkVideo *v = c->video;
    VkImageCreateInfo copy = *info;
    /* an image goes to any profile, or to the one of its bit depth when the gpu has no such images. */
    Profile profile;
    VkVideoProfileListInfoKHR list = {VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR, info->pNext, 0, &profile.info};
    if (v->independent) {
        copy.flags |= VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
    } else {
        uint32_t depth = info->format == picture_format(10) ? 10 : 8;
        for (size_t i = 0; i < PROFILE_COUNT && !list.profileCount; i++)
            if (g_profiles[i].bit_depth == depth && (v->supported & (1u << i))) {
                make_profile(&profile, g_profiles[i].codec, g_profiles[i].profile, depth);
                list.profileCount = 1;
            }
        copy.pNext = &list;
    }
    uint32_t families[64];
    uint32_t family_count = 0;
    for (uint32_t i = 0; i < c->queue_count; i++) {
        bool known = false;
        for (uint32_t j = 0; j < family_count; j++)
            known |= families[j] == c->queues[i].family;
        if (!known && family_count < 64)
            families[family_count++] = c->queues[i].family;
    }
    bool known = false;
    for (uint32_t j = 0; j < family_count; j++)
        known |= families[j] == v->family;
    if (!known && family_count < 64)
        families[family_count++] = v->family;
    if (family_count > 1) {
        copy.sharingMode = VK_SHARING_MODE_CONCURRENT;
        copy.queueFamilyIndexCount = family_count;
        copy.pQueueFamilyIndices = families;
    }
    return g_create_image(device, &copy, allocator, image);
}

/* ------------------------------------------------------------------ calls */

static void reply_result(MvkCall *call, VkResult result)
{
    MvkEncoder *e = mvk_reply_begin(call);
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

void mvk_video_caps(MvkCall *call)
{
    MvkVideo *v = call->conn->video;
    MvkEncoder *e = mvk_reply_begin(call);
    if (!v) {
        mvk_put_u32(e, (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT);
        mvk_put_u32(e, 0);
        mvk_reply_end(call);
        return;
    }
    uint32_t n = 0;
    for (size_t i = 0; i < PROFILE_COUNT; i++)
        n += (v->supported >> i) & 1;
    mvk_put_u32(e, (uint32_t)VK_SUCCESS);
    mvk_put_u32(e, n);
    for (size_t i = 0; i < PROFILE_COUNT; i++)
        if (v->supported & (1u << i))
            mvk_put_bytes(e, &v->caps[i], sizeof(v->caps[i]));
    mvk_reply_end(call);
}

static VideoSession *find_session(MvkVideo *v, uint64_t id)
{
    for (VideoSession *s = v->sessions; s; s = s->next)
        if (s->id == id)
            return s;
    return nullptr;
}

static uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want,
                                 VkMemoryPropertyFlags avoid)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want &&
            !(mp->memoryTypes[i].propertyFlags & avoid))
            return i;
    return ~0u;
}

/* the queue, command pool and timeline the decoding shares, made when the first session needs them. */
static bool start_queue(MvkConnection *c)
{
    MvkVideo *v = c->video;
    if (v->queue)
        return true;
    for (uint32_t i = 0; i < c->queue_count; i++)
        if (c->queues[i].family == v->family)
            v->queue = c->queues[i].queue;
    if (!v->queue)
        c->fn.vkGetDeviceQueue(c->device, v->family, 0, &v->queue);
    if (!v->queue) {
        mime_warn("video: the device has no queue of the decode family %u", v->family);
        return false;
    }
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = v->family;
    if (c->fn.vkCreateCommandPool(c->device, &pool, nullptr, &v->pool) != VK_SUCCESS)
        return false;
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    return c->fn.vkCreateSemaphore(c->device, &sem, nullptr, &v->timeline) == VK_SUCCESS;
}

void mvk_video_create(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    uint64_t id = mvk_get_u64(d);
    MvkVideoSessionInfo in;
    in.codec = mvk_get_u32(d);
    in.profile = mvk_get_u32(d);
    in.bit_depth = mvk_get_u32(d);
    in.width = mvk_get_u32(d);
    in.height = mvk_get_u32(d);
    MvkVideo *v = c->video;
    if (d->error || !v || !c->device || !start_queue(c)) {
        reply_result(call, VK_ERROR_FEATURE_NOT_PRESENT);
        return;
    }
    size_t index = PROFILE_COUNT;
    for (size_t i = 0; i < PROFILE_COUNT; i++)
        if ((v->supported & (1u << i)) && g_profiles[i].codec == in.codec && g_profiles[i].profile == in.profile &&
            g_profiles[i].bit_depth == in.bit_depth)
            index = i;
    if (index == PROFILE_COUNT || in.width == 0 || in.height == 0) {
        reply_result(call, VK_ERROR_FEATURE_NOT_PRESENT);
        return;
    }
    const VkVideoCapabilitiesKHR *caps = &v->vk_caps[index];
    if (in.width > caps->maxCodedExtent.width || in.height > caps->maxCodedExtent.height) {
        reply_result(call, VK_ERROR_FORMAT_NOT_SUPPORTED);
        return;
    }
    VideoSession *s = calloc(1, sizeof(*s));
    if (!s) {
        reply_result(call, VK_ERROR_OUT_OF_HOST_MEMORY);
        return;
    }
    s->id = id;
    s->codec = in.codec;
    s->profile = in.profile;
    s->bit_depth = in.bit_depth;
    s->width = in.width;
    s->height = in.height;
    s->profile_index = (uint32_t)index;
    s->format = picture_format(in.bit_depth);
    s->max_dpb_slots = caps->maxDpbSlots < VIDEO_MAX_DPB ? caps->maxDpbSlots : VIDEO_MAX_DPB;
    s->max_active = caps->maxActiveReferencePictures < s->max_dpb_slots ? caps->maxActiveReferencePictures
                                                                         : s->max_dpb_slots;
    for (uint32_t i = 0; i < VIDEO_MAX_INDEX; i++)
        s->pictures[i].slot = -1;
    Profile profile;
    make_profile(&profile, in.codec, in.profile, in.bit_depth);
    VkVideoSessionCreateInfoKHR info = {VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
    info.queueFamilyIndex = v->family;
    info.pVideoProfile = &profile.info;
    info.pictureFormat = info.referencePictureFormat = s->format;
    info.maxCodedExtent = (VkExtent2D){in.width < caps->minCodedExtent.width ? caps->minCodedExtent.width : in.width,
                                       in.height < caps->minCodedExtent.height ? caps->minCodedExtent.height
                                                                               : in.height};
    info.maxDpbSlots = s->max_dpb_slots;
    info.maxActiveReferencePictures = s->max_active;
    info.pStdHeaderVersion = &caps->stdHeaderVersion;
    VkResult result = v->fn.create_session(c->device, &info, nullptr, &s->session);
    if (result == VK_SUCCESS) {
        uint32_t count = 0;
        v->fn.session_memory(c->device, s->session, &count, nullptr);
        VkVideoSessionMemoryRequirementsKHR *reqs = calloc(count ? count : 1, sizeof(*reqs));
        VkBindVideoSessionMemoryInfoKHR *binds = calloc(count ? count : 1, sizeof(*binds));
        s->memories = calloc(count ? count : 1, sizeof(*s->memories));
        if (!reqs || !binds || !s->memories)
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
        for (uint32_t i = 0; result == VK_SUCCESS && i < count; i++)
            reqs[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
        if (result == VK_SUCCESS)
            v->fn.session_memory(c->device, s->session, &count, reqs);
        for (uint32_t i = 0; result == VK_SUCCESS && i < count; i++) {
            uint32_t type = find_memory_type(&v->memory, reqs[i].memoryRequirements.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
            if (type == ~0u)
                type = find_memory_type(&v->memory, reqs[i].memoryRequirements.memoryTypeBits, 0, 0);
            VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
                                          reqs[i].memoryRequirements.size, type};
            result = type == ~0u ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                                 : c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &s->memories[i]);
            if (result == VK_SUCCESS) {
                s->memory_count = i + 1;
                binds[i] = (VkBindVideoSessionMemoryInfoKHR){VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR,
                                                             nullptr, reqs[i].memoryBindIndex, s->memories[i], 0,
                                                             reqs[i].memoryRequirements.size};
            }
        }
        if (result == VK_SUCCESS)
            result = v->fn.bind_memory(c->device, s->session, count, binds);
        free(reqs);
        free(binds);
    }
    if (result != VK_SUCCESS) {
        mime_warn("video: a decoder session of codec %u was not made (%d)", in.codec, (int)result);
        destroy_session(c, s);
        reply_result(call, result);
        return;
    }
    s->next = v->sessions;
    v->sessions = s;
    reply_result(call, VK_SUCCESS);
}

/* waits for the decodes sent so far, which read what is about to be destroyed. */
static void wait_decodes(MvkConnection *c)
{
    MvkVideo *v = c->video;
    if (!v->timeline || !v->value)
        return;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &v->timeline, &v->value};
    c->fn.vkWaitSemaphores(c->device, &wait, UINT64_MAX);
}

void mvk_video_destroy(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    MvkVideo *v = c->video;
    if (!v)
        return;
    for (VideoSession **p = &v->sessions; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            VideoSession *s = *p;
            *p = s->next;
            wait_decodes(c);
            destroy_session(c, s);
            return;
        }
    }
}

/* ------------------------------------------------------------------ pictures */

static void release_picture(VideoSession *s, VideoPicture *p)
{
    if (p->slot >= 0)
        s->slots_used &= ~(1u << p->slot);
    p->slot = -1;
    p->valid = false;
    p->active = false;
}

VideoPicture *video_ref(MvkConnection *c, VideoSession *s, uint32_t index)
{
    if (index >= VIDEO_MAX_INDEX)
        return nullptr;
    VideoPicture *p = &s->pictures[index];
    if (!p->valid || !p->active || p->slot < 0)
        return nullptr;
    /* ids are reused: a view the guest destroyed and made again is another picture. */
    if (mvk_lookup(c, p->view_id) != (uint64_t)p->view) {
        release_picture(s, p);
        return nullptr;
    }
    return p;
}

void video_retain(VideoSession *s, const uint8_t *indices, uint32_t count)
{
    for (uint32_t i = 0; i < VIDEO_MAX_INDEX; i++) {
        VideoPicture *p = &s->pictures[i];
        if (!p->valid)
            continue;
        bool kept = false;
        for (uint32_t j = 0; j < count; j++)
            kept |= indices[j] == i;
        if (!kept)
            release_picture(s, p);
    }
}

VideoPicture *video_current(MvkConnection *c, VideoSession *s, uint32_t index, const MvkVideoDecodeInfo *in,
                            uint64_t image, uint32_t layer, uint32_t width, uint32_t height)
{
    if (index >= VIDEO_MAX_INDEX)
        return nullptr;
    /* a view decoded into again no longer holds what it did, under whatever index it did. */
    for (uint32_t i = 0; i < VIDEO_MAX_INDEX; i++)
        if (i != index && s->pictures[i].valid && s->pictures[i].view_id == in->picture_view)
            release_picture(s, &s->pictures[i]);
    VideoPicture *p = &s->pictures[index];
    if (p->valid && p->view_id != in->picture_view)
        release_picture(s, p);
    if (p->slot < 0) {
        for (uint32_t i = 0; i < s->max_dpb_slots; i++) {
            if (!(s->slots_used & (1u << i))) {
                s->slots_used |= 1u << i;
                p->slot = (int)i;
                break;
            }
        }
        if (p->slot < 0)
            return nullptr;
    }
    p->valid = true;
    p->active = false;
    p->view_id = in->picture_view;
    p->view = (VkImageView)mvk_lookup(c, in->picture_view);
    p->image_id = image;
    p->image = (VkImage)mvk_lookup(c, image);
    p->layer = layer;
    p->extent = (VkExtent2D){width, height};
    return p->view ? p : nullptr;
}

void video_picture_resource(const VideoPicture *p, VkVideoPictureResourceInfoKHR *r)
{
    *r = (VkVideoPictureResourceInfoKHR){VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    r->codedExtent = p->extent;
    r->imageViewBinding = p->view;
}

static void free_mirror(MvkConnection *c, VideoPicture *p)
{
    if (p->mirror_view)
        c->fn.vkDestroyImageView(c->device, p->mirror_view, nullptr);
    if (p->mirror_image)
        c->fn.vkDestroyImage(c->device, p->mirror_image, nullptr);
    if (p->mirror_memory)
        c->fn.vkFreeMemory(c->device, p->mirror_memory, nullptr);
    p->mirror_view = VK_NULL_HANDLE;
    p->mirror_image = VK_NULL_HANDLE;
    p->mirror_memory = VK_NULL_HANDLE;
    p->mirror_ready = false;
}

bool video_mirror(MvkConnection *c, VideoSession *s, VideoPicture *p)
{
    MvkVideo *v = c->video;
    if (p->mirror_view && p->mirror_extent.width >= p->extent.width && p->mirror_extent.height >= p->extent.height)
        return true;
    if (p->mirror_view) {
        /* the decodes in flight may read it. */
        wait_decodes(c);
        free_mirror(c, p);
    }
    Profile profile;
    make_profile(&profile, s->codec, s->profile, s->bit_depth);
    VkVideoProfileListInfoKHR list = {VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR, nullptr, 1, &profile.info};
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = s->format;
    info.extent = (VkExtent3D){p->extent.width > s->width ? p->extent.width : s->width,
                               p->extent.height > s->height ? p->extent.height : s->height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (v->independent)
        info.flags |= VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
    else
        info.pNext = &list;
    /* the decode queue's own: the picture is never seen by another. */
    if (g_create_image(c->device, &info, nullptr, &p->mirror_image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    c->fn.vkGetImageMemoryRequirements(c->device, p->mirror_image, &req);
    uint32_t type = find_memory_type(&v->memory, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, type};
    VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = p->mirror_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = s->format;
    view.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageViewUsageCreateInfo usage = {VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO, nullptr,
                                        VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR};
    view.pNext = &usage;
    if (type == ~0u || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->mirror_memory) != VK_SUCCESS ||
        c->fn.vkBindImageMemory(c->device, p->mirror_image, p->mirror_memory, 0) != VK_SUCCESS ||
        c->fn.vkCreateImageView(c->device, &view, nullptr, &p->mirror_view) != VK_SUCCESS) {
        free_mirror(c, p);
        return false;
    }
    p->mirror_extent = (VkExtent2D){info.extent.width, info.extent.height};
    return true;
}

bool video_set_parameters(MvkConnection *c, VideoSession *s, const void *key, uint32_t key_size,
                          const VkVideoSessionParametersCreateInfoKHR *info)
{
    MvkVideo *v = c->video;
    if (s->params && s->key_size == key_size && !memcmp(s->key, key, key_size))
        return true;
    if (key_size > sizeof(s->key))
        return false;
    if (s->params) {
        wait_decodes(c);
        v->fn.destroy_parameters(c->device, s->params, nullptr);
        s->params = VK_NULL_HANDLE;
    }
    VkVideoSessionParametersCreateInfoKHR copy = *info;
    copy.videoSession = s->session;
    if (v->fn.create_parameters(c->device, &copy, nullptr, &s->params) != VK_SUCCESS) {
        s->key_size = 0;
        return false;
    }
    memcpy(s->key, key, key_size);
    s->key_size = key_size;
    return true;
}

/* ------------------------------------------------------------------ decoding */

/* a job to build a picture in: its buffer holds at least the size, once the work that used it is done. */
static VideoJob *take_job(MvkConnection *c, const VideoSession *s, VkDeviceSize need)
{
    MvkVideo *v = c->video;
    VideoJob *j = &v->jobs[v->next_job++ % VIDEO_RING];
    if (j->done) {
        VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &v->timeline, &j->done};
        c->fn.vkWaitSemaphores(c->device, &wait, UINT64_MAX);
        j->done = 0;
    }
    if (!j->cmd) {
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, v->pool,
                                             VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        if (c->fn.vkAllocateCommandBuffers(c->device, &alloc, &j->cmd) != VK_SUCCESS)
            return nullptr;
    }
    if (j->buffer && j->size >= need)
        return j;
    free_job(c, j);
    VkDeviceSize size = 1u << 20;
    while (size < need)
        size *= 2;
    Profile profile;
    VkVideoProfileListInfoKHR list = {VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR, nullptr, 1, &profile.info};
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size,
                               VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR, VK_SHARING_MODE_EXCLUSIVE};
    if (v->independent) {
        info.flags = VK_BUFFER_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
    } else {
        make_profile(&profile, s->codec, s->profile, s->bit_depth);
        info.pNext = &list;
    }
    VkResult made = c->fn.vkCreateBuffer(c->device, &info, nullptr, &j->buffer);
    if (made != VK_SUCCESS) {
        mime_warn("video: a bitstream buffer of %llu bytes was not made (%d)", (unsigned long long)size, (int)made);
        return nullptr;
    }
    VkMemoryRequirements req;
    c->fn.vkGetBufferMemoryRequirements(c->device, j->buffer, &req);
    uint32_t type = find_memory_type(&v->memory, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == ~0u)
        type = find_memory_type(&v->memory, req.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, type};
    void *map = nullptr;
    if (type == ~0u || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &j->memory) != VK_SUCCESS ||
        c->fn.vkBindBufferMemory(c->device, j->buffer, j->memory, 0) != VK_SUCCESS ||
        c->fn.vkMapMemory(c->device, j->memory, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS) {
        mime_warn("video: the memory of a bitstream buffer was not made");
        free_job(c, j);
        return nullptr;
    }
    j->map = map;
    j->size = size;
    return j;
}

/*
 * the images a decode reads or writes, moved to the layouts decoding wants and back when the gpu keeps them
 * apart. a mirror, which film grain pictures are reconstructed in, only ever holds the layout of a reference.
 */
static void layout_barriers(MvkConnection *c, VkCommandBuffer cmd, VideoFrame *f, bool before)
{
    MvkVideo *v = c->video;
    VkImageMemoryBarrier2 barriers[2 * (VIDEO_MAX_DPB + 1)];
    uint32_t n = 0;
    for (uint32_t i = 0; i <= f->ref_count; i++) {
        VideoPicture *p = i == f->ref_count ? f->current : f->ref_pictures[i];
        if (!p)
            continue;
        bool is_current = i == f->ref_count;
        if (p->mirrored && p->mirror_image && before) {
            VkImageMemoryBarrier2 *b = &barriers[n++];
            *b = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            b->srcStageMask = b->dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            b->srcAccessMask = b->dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            b->oldLayout = p->mirror_ready ? (v->unified ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR)
                                           : VK_IMAGE_LAYOUT_UNDEFINED;
            b->newLayout = v->unified ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
            b->srcQueueFamilyIndex = b->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b->image = p->mirror_image;
            b->subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            p->mirror_ready = true;
        }
        /* a reference in a mirror leaves the image of the guest alone. */
        if (v->unified || !p->image || (p->mirrored && !is_current))
            continue;
        VkImageLayout busy = p->mirrored ? VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR : VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
        VkImageMemoryBarrier2 *b = &barriers[n++];
        *b = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b->srcStageMask = b->dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b->srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        b->dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        b->oldLayout = before ? VK_IMAGE_LAYOUT_GENERAL : busy;
        b->newLayout = before ? busy : VK_IMAGE_LAYOUT_GENERAL;
        b->srcQueueFamilyIndex = b->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b->image = p->image;
        b->subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, p->layer, 1};
    }
    if (!n)
        return;
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = n;
    dep.pImageMemoryBarriers = barriers;
    c->fn.vkCmdPipelineBarrier2(cmd, &dep);
}

void mvk_video_decode(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    MvkVideoDecodeInfo in = {0};
    in.session = mvk_get_u64(d);
    in.picture_view = mvk_get_u64(d);
    uint64_t image = mvk_get_u64(d);
    uint32_t layer = mvk_get_u32(d);
    (void)mvk_get_u32(d);
    in.wait_semaphore = mvk_get_u64(d);
    in.wait_value = mvk_get_u64(d);
    in.signal_semaphore = mvk_get_u64(d);
    in.signal_value = mvk_get_u64(d);
    in.slice_count = mvk_get_u32(d);
    (void)mvk_get_u32(d);
    size_t size;
    in.params = mvk_get_blob(d, &size);
    in.params_size = (uint32_t)size;
    in.qmatrix = mvk_get_blob(d, &size);
    in.qmatrix_size = (uint32_t)size;
    in.slices = mvk_get_blob(d, &size);
    in.slices_size = (uint32_t)size;
    in.bitstream = mvk_get_blob(d, &size);
    in.bitstream_size = (uint32_t)size;
    MvkVideo *v = c->video;
    VideoSession *s = v ? find_session(v, in.session) : nullptr;
    if (d->error || !s || !in.params || !in.bitstream || !in.bitstream_size) {
        mime_warn("video: a decode of a session that does not exist or without its buffers");
        return;
    }
    /* the frame is large: one per call, on the connection's thread. */
    VideoFrame *f = calloc(1, sizeof(*f));
    if (!f)
        return;
    bool ok = s->codec == MVK_VIDEO_CODEC_H264   ? video_h264_prepare(c, s, &in, image, layer, f)
              : s->codec == MVK_VIDEO_CODEC_H265 ? video_h265_prepare(c, s, &in, image, layer, f)
              : s->codec == MVK_VIDEO_CODEC_VP9  ? video_vp9_prepare(c, s, &in, image, layer, f)
                                                 : video_av1_prepare(c, s, &in, image, layer, f);
    VideoJob *job = ok ? take_job(c, s, (in.bitstream_size + 255u) & ~255u) : nullptr;
    if (!job) {
        mime_warn("video: a picture was not decoded");
        free(f);
        return;
    }
    /* the bitstream, zeroed up to the alignment of its range. */
    VkDeviceSize range = (in.bitstream_size + 255u) & ~255u;
    memcpy(job->map, in.bitstream, in.bitstream_size);
    memset(job->map + in.bitstream_size, 0, range - in.bitstream_size);
    f->info.srcBuffer = job->buffer;
    f->info.srcBufferOffset = 0;
    f->info.srcBufferRange = range;

    VkCommandBuffer cmd = job->cmd;
    c->fn.vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    c->fn.vkBeginCommandBuffer(cmd, &begin);
    layout_barriers(c, cmd, f, true);
    /* the scope names the references and the picture to set up, the latter in no slot yet. */
    VkVideoReferenceSlotInfoKHR slots[VIDEO_MAX_DPB + 1];
    memcpy(slots, f->refs, f->ref_count * sizeof(slots[0]));
    slots[f->ref_count] = f->setup;
    slots[f->ref_count].slotIndex = -1;
    VkVideoBeginCodingInfoKHR scope = {VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR, nullptr, 0, s->session, f->params,
                                       f->ref_count + 1, slots};
    v->fn.begin(cmd, &scope);
    if (!s->reset) {
        VkVideoCodingControlInfoKHR control = {VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR, nullptr,
                                               VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR};
        v->fn.control(cmd, &control);
        s->reset = true;
    }
    v->fn.decode(cmd, &f->info);
    VkVideoEndCodingInfoKHR end = {VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
    v->fn.end(cmd, &end);
    layout_barriers(c, cmd, f, false);
    c->fn.vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo waits[1], signals[2];
    uint32_t wait_count = 0, signal_count = 0;
    VkSemaphore wait_sem = (VkSemaphore)mvk_lookup(c, in.wait_semaphore);
    VkSemaphore signal_sem = (VkSemaphore)mvk_lookup(c, in.signal_semaphore);
    if (wait_sem)
        waits[wait_count++] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, wait_sem,
                                                      in.wait_value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    uint64_t value = ++v->value;
    signals[signal_count++] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, v->timeline,
                                                      value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    if (signal_sem)
        signals[signal_count++] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, signal_sem,
                                                          in.signal_value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkCommandBufferSubmitInfo buffer = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd};
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = wait_count;
    submit.pWaitSemaphoreInfos = waits;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &buffer;
    submit.signalSemaphoreInfoCount = signal_count;
    submit.pSignalSemaphoreInfos = signals;
    pthread_mutex_lock(&c->queue_lock);
    VkResult result = c->fn.vkQueueSubmit2(v->queue, 1, &submit, VK_NULL_HANDLE);
    pthread_mutex_unlock(&c->queue_lock);
    mvk_check(call, result, "video decode submit");
    job->done = value;
    v->decoded++;
    /* a reference from now on, if the codec says the picture is one. */
    f->current->active = f->is_reference;
    free(f);
}
