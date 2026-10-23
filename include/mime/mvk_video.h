/*
 * hardware video decoding through the Vulkan wire. guest Direct3D 11 decoders send codec-specific DXVA buffers from
 * include/mime/dxva.h. the guest never sees Vulkan video objects, which the host owns. decode images belong
 * to the guest, with host-added profiles and queue families.
 *
 * calls follow mvk_wire.h:
 * MVK_OP_VIDEO_CAPS     reply: result, count, count times a MvkVideoCaps
 * MVK_OP_VIDEO_CREATE   id, codec, profile, bit depth, width, height; reply: result
 * MVK_OP_VIDEO_DESTROY  id
 * MVK_OP_VIDEO_DECODE   session, picture view, wait and signal semaphores, the DXVA buffers.
 */
#ifndef MIME_MVK_VIDEO_H
#define MIME_MVK_VIDEO_H

#include <stdint.h>

#define MVK_VIDEO_CODEC_H264 1
#define MVK_VIDEO_CODEC_H265 2
#define MVK_VIDEO_CODEC_VP9 3
#define MVK_VIDEO_CODEC_AV1 4

/* video extensions exposed in the device extension list. */
#define MVK_VIDEO_DECODE_EXTENSION "VK_KHR_video_decode_queue"

/* DXVA codec profiles: H.264 and H.265 use profile_idc; VP9 and AV1 use profile numbers. */
#define MVK_VIDEO_H264_HIGH 100
#define MVK_VIDEO_H265_MAIN 1
#define MVK_VIDEO_H265_MAIN10 2

typedef struct MvkVideoCaps {
    uint32_t codec;
    uint32_t profile;
    uint32_t bit_depth;
    uint32_t flags;
    uint32_t min_width, min_height, max_width, max_height;
    uint32_t max_dpb_slots, max_active_refs;
    uint32_t queue_family;
    uint32_t bitstream_alignment;
} MvkVideoCaps;

typedef struct MvkVideoSessionInfo {
    uint32_t codec;
    uint32_t profile;
    uint32_t bit_depth;
    uint32_t width, height;
} MvkVideoSessionInfo;

/* DXVA picture buffers. */
typedef struct MvkVideoDecodeInfo {
    uint64_t session;
    /* output picture view over one layer of a decode image. */
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
