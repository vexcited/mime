/*
 * the host's video decoder: dxva pictures from the guest decoded by the gpu's video decode queue, the
 * connection's own command buffers and bitstream memory, see include/mime/mvk_video.h. one source file per
 * codec turns the dxva buffers of a picture into what vulkan video wants.
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

/* one picture buffer the guest decoded into, as its dxva index names it. */
typedef struct VideoPicture {
    bool valid;
    uint64_t view_id;
    VkImageView view;
    uint64_t image_id;
    VkImage image;
    uint32_t layer;
    /* the dpb slot holding it, -1 when none, and whether the slot has been set up as a reference. */
    int slot;
    bool active;
    VkExtent2D extent;
    /*
     * av1 film grain: the image of the guest holds the picture with the grain, the picture as the later frames
     * refer to it has none and is kept apart, in an image only the decoder uses.
     */
    VkImage mirror_image;
    VkDeviceMemory mirror_memory;
    VkImageView mirror_view;
    bool mirrored; /* the picture is a reference in the mirror, not in the image of the guest */
    bool mirror_ready; /* the mirror has been moved out of its undefined layout */
    VkExtent2D mirror_extent;
} VideoPicture;

/* the work of one picture in flight: a command buffer and the bitstream it reads. */
typedef struct VideoJob {
    VkCommandBuffer cmd;
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t *map;
    VkDeviceSize size;
    uint64_t done; /* the value of the connection's decode timeline once it is done, 0 when never used */
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
    uint64_t decoded; /* the pictures sent to the decode queue */
    bool unified; /* the gpu keeps images in the general layout through decoding */
    bool independent; /* images and buffers are made for any profile, not for the ones of a list */
    uint32_t codecs; /* the codecs whose extensions are enabled, as bits of MVK_VIDEO_CODEC_ numbers */
    /* the profiles the gpu decodes, by index of the table in video.c, and what it said of them. */
    uint32_t supported;
    MvkVideoCaps caps[8];
    VkVideoCapabilitiesKHR vk_caps[8];
    VkVideoDecodeCapabilityFlagsKHR decode_flags[8];
    StdVideoH265LevelIdc h265_level[8]; /* the highest level the gpu decodes, of the h.265 profiles */
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
    bool reset; /* the session has been reset on the queue */
    VideoPicture pictures[VIDEO_MAX_INDEX];
    uint32_t slots_used; /* bit mask of the dpb slots in use */
    uint32_t max_dpb_slots, max_active;
    VkFormat format;
    /* the parameter sets the current parameters object holds, to compare with. */
    uint8_t key[4096];
    uint32_t key_size;
    /* codec state. */
    struct {
        uint32_t last_width, last_height;
    } vp9;
    struct {
        /* what the picture last decoded into each dxva index is as a reference. */
        StdVideoDecodeAV1ReferenceInfo refs[VIDEO_MAX_INDEX];
        /* from the last frame with film grain, which is where dxva says it. */
        bool matrix_identity;
    } av1;
};

/* a picture as vulkan wants it, built by the codec's code. */
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
            uint32_t tile_sizes[VIDEO_MAX_SLICES]; /* the offsets are in slice_offsets */
            VkVideoDecodeAV1DpbSlotInfoKHR slots[VIDEO_MAX_DPB + 1];
            StdVideoDecodeAV1ReferenceInfo std_refs[VIDEO_MAX_DPB + 1];
        } av1;
    } u;
} VideoFrame;

/* video.c: the pictures of a session, for the codecs. */
VideoPicture *video_ref(MvkConnection *c, VideoSession *s, uint32_t index);
/* every picture not in the set no longer holds a slot: what a frame may refer to now or later. */
void video_retain(VideoSession *s, const uint8_t *indices, uint32_t count);
/* the picture decoded now, bound to the dxva index and a slot, null when no slot is left. */
VideoPicture *video_current(MvkConnection *c, VideoSession *s, uint32_t index, const MvkVideoDecodeInfo *in,
                            uint64_t image, uint32_t layer, uint32_t width, uint32_t height);
void video_picture_resource(const VideoPicture *p, VkVideoPictureResourceInfoKHR *r);
/* makes a parameters object from the key, or keeps the one the same key made. */
bool video_set_parameters(MvkConnection *c, VideoSession *s, const void *key, uint32_t key_size,
                          const VkVideoSessionParametersCreateInfoKHR *info);

/*
 * makes the image a picture is reconstructed in when the image of the guest gets the output with film grain:
 * the size of the session's pictures, for decoding as a reference only. true when it is made or was.
 */
bool video_mirror(MvkConnection *c, VideoSession *s, VideoPicture *p);

/* the codecs: fill the frame from the dxva buffers, false when they do not describe a picture to decode. */
bool video_h264_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f);
bool video_h265_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f);
bool video_vp9_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f);
bool video_av1_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f);

/* what vulkan.c calls. */
bool mvk_video_offered(MvkConnection *c, VkPhysicalDevice physical);
uint32_t mvk_video_queue_family(MvkConnection *c, VkPhysicalDevice physical);
/* the extensions to enable: the decode queue and those of the codecs the gpu has, named in the mask returned. */
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
