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
#include <vk_video/vulkan_video_codec_h264std_decode.h>
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
    bool unified; /* the gpu keeps images in the general layout through decoding */
    bool independent; /* images and buffers are made for any profile, not for the ones of a list */
    /* the profiles the gpu decodes, by index of the table in video.c, and what it said of them. */
    uint32_t supported;
    MvkVideoCaps caps[8];
    VkVideoCapabilitiesKHR vk_caps[8];
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
    uint8_t key[2048];
    uint32_t key_size;
    /* codec state. */
    struct {
        uint32_t last_width, last_height;
    } vp9;
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
            VkVideoDecodeVP9PictureInfoKHR picture;
            StdVideoDecodeVP9PictureInfo std_picture;
            StdVideoVP9ColorConfig color;
            StdVideoVP9LoopFilter loop_filter;
            StdVideoVP9Segmentation segmentation;
        } vp9;
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

/* the codecs: fill the frame from the dxva buffers, false when they do not describe a picture to decode. */
bool video_h264_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f);
bool video_vp9_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f);

/* what vulkan.c calls. */
bool mvk_video_offered(MvkConnection *c, VkPhysicalDevice physical);
uint32_t mvk_video_queue_family(MvkConnection *c, VkPhysicalDevice physical);
size_t mvk_video_extensions(const char *const **names);
void mvk_video_device_created(MvkConnection *c, PFN_vkGetDeviceProcAddr gdpa, bool unified, bool independent);
void mvk_video_close(MvkConnection *c);
void mvk_video_enter(MvkConnection *c);
VkResult mvk_video_create_image(VkDevice device, const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator,
                                VkImage *image);
void mvk_video_caps(MvkCall *call);
void mvk_video_create(MvkCall *call);
void mvk_video_destroy(MvkCall *call);
void mvk_video_decode(MvkCall *call);

#endif
