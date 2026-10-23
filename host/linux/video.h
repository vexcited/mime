/*
 * shared Vulkan video decoder state. each codec converts guest DXVA buffers to Vulkan structures. connections
 * own command buffers and bitstream memory; see include/mime/mvk_video.h.
 */
#ifndef MIME_HOST_VIDEO_H
#define MIME_HOST_VIDEO_H

#include "mvk_host.h"

#include <mime/dxva.h>
#include <mime/mvk_video.h>
#include <vk_video/vulkan_video_codec_av1std_decode.h>
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h265std_decode.h>
#include <vk_video/vulkan_video_codec_vp9std_decode.h>

#define VIDEO_MAX_INDEX 128
#define VIDEO_MAX_DPB 17
#define VIDEO_MAX_SLICES 1024
#define VIDEO_RING 6

typedef struct VideoFn {
    PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR get_caps;
    PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR get_formats;
    PFN_vkCreateVideoSessionKHR create_session;
    PFN_vkDestroyVideoSessionKHR destroy_session;
    PFN_vkGetVideoSessionMemoryRequirementsKHR session_memory;
    PFN_vkBindVideoSessionMemoryKHR bind_memory;
    PFN_vkCreateVideoSessionParametersKHR create_parameters;
    PFN_vkDestroyVideoSessionParametersKHR destroy_parameters;
    PFN_vkCmdBeginVideoCodingKHR begin;
    PFN_vkCmdEndVideoCodingKHR end;
    PFN_vkCmdControlVideoCodingKHR control;
    PFN_vkCmdDecodeVideoKHR decode;
} VideoFn;

/* decoded picture indexed by its DXVA surface index. */
typedef struct VideoPicture {
    bool valid;
    uint64_t view_id;
    VkImageView view;
    uint64_t image_id;
    VkImage image;
    uint32_t layer;
    /* the DPB slot holding it, -1 when none, and whether the slot has been set up as a reference. */
    int slot;
    bool active;
    VkExtent2D extent;
    /*
     * AV1 film grain is applied to the guest image. later frames reference a separate grain-free mirror used
     * only by the decoder.
     */
    VkImage mirror_image;
    VkDeviceMemory mirror_memory;
    VkImageView mirror_view;
    bool mirrored; /* the picture is a reference in the mirror, not in the image of the guest. */
    bool mirror_ready; /* the mirror has been moved out of its undefined layout. */
    VkExtent2D mirror_extent;
} VideoPicture;

/* the work of one picture in flight: a command buffer and the bitstream it reads. */
typedef struct VideoJob {
    VkCommandBuffer cmd;
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t *map;
    VkDeviceSize size;
    uint64_t done; /* the value of the connection's decode timeline once it is done, 0 when never used. */
} VideoJob;

typedef struct VideoSession VideoSession;

typedef struct MvkVideo {
    VideoFn fn;
    uint32_t family;
    VkQueue queue;
    VkCommandPool pool;
    VkSemaphore timeline;
    uint64_t value;
    VideoJob jobs[VIDEO_RING];
    uint32_t next_job;
    uint64_t decoded;
    bool unified; /* the GPU keeps images in the general layout through decoding. */
    bool independent; /* images and buffers are made for any profile, not for the ones of a list. */
    uint32_t codecs; /* the codecs whose extensions are enabled, as bits of MVK_VIDEO_CODEC_ numbers. */
    /* supported profiles and capabilities, indexed by the table in video.c. */
    uint32_t supported;
    MvkVideoCaps caps[8];
    VkVideoCapabilitiesKHR vk_caps[8];
    VkVideoDecodeCapabilityFlagsKHR decode_flags[8];
    StdVideoH265LevelIdc h265_level[8]; /* the highest level the GPU decodes, of the h.265 profiles. */
    VkPhysicalDeviceMemoryProperties memory;
    VideoSession *sessions;
} MvkVideo;

struct VideoSession {
    VideoSession *next;
    uint64_t id;
    uint32_t codec, profile, bit_depth;
    uint32_t width, height;
    uint32_t profile_index;
    VkVideoSessionKHR session;
    VkDeviceMemory *memories;
    uint32_t memory_count;
    VkVideoSessionParametersKHR params;
    bool reset; /* the session has been reset on the queue. */
    VideoPicture pictures[VIDEO_MAX_INDEX];
    uint32_t slots_used; /* bit mask of the DPB slots in use. */
    uint32_t max_dpb_slots, max_active;
    VkFormat format;
    /* the parameter sets the current parameters object holds, to compare with. */
    uint8_t key[4096];
    uint32_t key_size;
    struct {
        uint32_t last_width, last_height;
    } vp9;
    struct {
        /* reference metadata for the last picture decoded at each DXVA index. */
        StdVideoDecodeAV1ReferenceInfo refs[VIDEO_MAX_INDEX];
        /* DXVA reports this in the last frame with film grain. */
        bool matrix_identity;
    } av1;
};

/* Vulkan frame parameters filled by the codec. */
typedef struct VideoFrame {
    VkVideoDecodeInfoKHR info;
    VkVideoPictureResourceInfoKHR setup_resource;
    VkVideoReferenceSlotInfoKHR setup;
    VkVideoPictureResourceInfoKHR ref_resources[VIDEO_MAX_DPB];
    VkVideoReferenceSlotInfoKHR refs[VIDEO_MAX_DPB];
    uint32_t ref_count;
    /* the pictures read as references, for layout changes. */
    VideoPicture *ref_pictures[VIDEO_MAX_DPB];
    VideoPicture *current;
    uint32_t slice_offsets[VIDEO_MAX_SLICES];
    VkVideoSessionParametersKHR params;
    /* whether the picture becomes a reference in its slot. */
    bool is_reference;
    union {
        struct {
            VkVideoDecodeH264PictureInfoKHR picture;
            StdVideoDecodeH264PictureInfo std_picture;
            VkVideoDecodeH264DpbSlotInfoKHR slots[VIDEO_MAX_DPB + 1];
            StdVideoDecodeH264ReferenceInfo std_refs[VIDEO_MAX_DPB + 1];
        } h264;
        struct {
            VkVideoDecodeH265PictureInfoKHR picture;
            StdVideoDecodeH265PictureInfo std_picture;
            VkVideoDecodeH265DpbSlotInfoKHR slots[VIDEO_MAX_DPB + 1];
            StdVideoDecodeH265ReferenceInfo std_refs[VIDEO_MAX_DPB + 1];
        } h265;
        struct {
            VkVideoDecodeVP9PictureInfoKHR picture;
            StdVideoDecodeVP9PictureInfo std_picture;
            StdVideoVP9ColorConfig color;
            StdVideoVP9LoopFilter loop_filter;
            StdVideoVP9Segmentation segmentation;
        } vp9;
        struct {
            VkVideoDecodeAV1PictureInfoKHR picture;
            StdVideoDecodeAV1PictureInfo std_picture;
            StdVideoAV1TileInfo tile_info;
            StdVideoAV1Quantization quantization;
            StdVideoAV1Segmentation segmentation;
            StdVideoAV1LoopFilter loop_filter;
            StdVideoAV1CDEF cdef;
            StdVideoAV1LoopRestoration loop_restoration;
            StdVideoAV1GlobalMotion global_motion;
            StdVideoAV1FilmGrain film_grain;
            uint16_t mi_col_starts[64], mi_row_starts[64], width_in_sbs_minus_1[64], height_in_sbs_minus_1[64];
            uint32_t tile_sizes[VIDEO_MAX_SLICES]; /* the offsets are in slice_offsets. */
            VkVideoDecodeAV1DpbSlotInfoKHR slots[VIDEO_MAX_DPB + 1];
            StdVideoDecodeAV1ReferenceInfo std_refs[VIDEO_MAX_DPB + 1];
        } av1;
    } u;
} VideoFrame;

/* session pictures shared by the codec implementations. */
VideoPicture *video_ref(MvkConnection *c, VideoSession *s, uint32_t index);
/* releases slots for pictures outside the set of current or future references. */
void video_retain(VideoSession *s, const uint8_t *indices, uint32_t count);
/* binds the output picture to a DXVA index and slot; null if no slot remains. */
VideoPicture *video_current(MvkConnection *c, VideoSession *s, uint32_t index, const MvkVideoDecodeInfo *in,
                            uint64_t image, uint32_t layer, uint32_t width, uint32_t height);
void video_picture_resource(const VideoPicture *p, VkVideoPictureResourceInfoKHR *r);
/* reuses the parameters object when its key matches. */
bool video_set_parameters(MvkConnection *c, VideoSession *s, const void *key, uint32_t key_size,
                          const VkVideoSessionParametersCreateInfoKHR *info);

/*
 * creates a grain-free reference image at the session picture size when the guest receives film grain output.
 * returns true if the mirror exists.
 */
bool video_mirror(MvkConnection *c, VideoSession *s, VideoPicture *p);

/* fills a frame from DXVA buffers; false for an invalid picture. */
bool video_h264_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f);
bool video_h265_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f);
bool video_vp9_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f);
bool video_av1_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f);

bool mvk_video_offered(MvkConnection *c, VkPhysicalDevice physical);
uint32_t mvk_video_queue_family(MvkConnection *c, VkPhysicalDevice physical);
/* required decode and supported codec extensions; returns the codec mask. */
size_t mvk_video_extensions(MvkConnection *c, VkPhysicalDevice physical, const char **names, uint32_t *codecs);
void mvk_video_device_created(MvkConnection *c, PFN_vkGetDeviceProcAddr gdpa, bool unified, bool independent,
                              uint32_t codecs);
void mvk_video_close(MvkConnection *c);
void mvk_video_enter(MvkConnection *c);
VkResult mvk_video_create_image(VkDevice device, const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator,
                                VkImage *image);
void mvk_video_caps(MvkCall *call);
void mvk_video_create(MvkCall *call);
void mvk_video_destroy(MvkCall *call);
void mvk_video_decode(MvkCall *call);

#endif
