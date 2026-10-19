/*
 * hardware video decoding through the vulkan wire: what the guest asks of the host's video decode queue, in
 * terms of codecs and dxva buffers, so the host does what is codec specific (see include/mime/dxva.h) and
 * the guest, direct3d 11's video decoders, never sees a vulkan video object. the images it decodes into are
 * its own, made with the decode usages: the host adds the video profiles and queue families to them.
 *
 * the calls, after those of mvk_wire.h:
 *   MVK_OP_VIDEO_CAPS     reply: result, count, count times a MvkVideoCaps
 *   MVK_OP_VIDEO_CREATE   id, codec, profile, bit depth, width, height; reply: result
 *   MVK_OP_VIDEO_DESTROY  id
 *   MVK_OP_VIDEO_DECODE   session, picture view, wait and signal semaphores, the dxva buffers
 */
#ifndef MIME_MVK_VIDEO_H
#define MIME_MVK_VIDEO_H

#include <stdint.h>

#define MVK_VIDEO_CODEC_H264 1
#define MVK_VIDEO_CODEC_H265 2
#define MVK_VIDEO_CODEC_VP9 3
#define MVK_VIDEO_CODEC_AV1 4

/* the video extensions the host offers, as the device's extension list shows them. */
#define MVK_VIDEO_DECODE_EXTENSION "VK_KHR_video_decode_queue"

/* the profiles of each codec, as dxva names them: h.264 by profile_idc, vp9 and av1 by their profile number. */
#define MVK_VIDEO_H264_HIGH 100

/* one decoding profile the gpu supports. */
typedef struct MvkVideoCaps {
    uint32_t codec;
    uint32_t profile;
    uint32_t bit_depth;
    uint32_t flags;
    uint32_t min_width, min_height, max_width, max_height;
    uint32_t max_dpb_slots, max_active_refs;
    /* the queue family that decodes. */
    uint32_t queue_family;
    uint32_t bitstream_alignment;
} MvkVideoCaps;

typedef struct MvkVideoSessionInfo {
    uint32_t codec;
    uint32_t profile;
    uint32_t bit_depth;
    uint32_t width, height;
} MvkVideoSessionInfo;

/* the buffers of a picture, as dxva programs fill them. */
typedef struct MvkVideoDecodeInfo {
    uint64_t session;
    /* the view of the picture decoded into, over one layer of an image made for decoding. */
    uint64_t picture_view;
    /* timeline semaphores: the work waits for the value, the decode signals its own. zero for none. */
    uint64_t wait_semaphore, wait_value;
    uint64_t signal_semaphore, signal_value;
    const void *params;
    uint32_t params_size;
    const void *qmatrix;
    uint32_t qmatrix_size;
    const void *slices;
    uint32_t slices_size;
    uint32_t slice_count;
    const void *bitstream;
    uint32_t bitstream_size;
} MvkVideoDecodeInfo;

#endif
