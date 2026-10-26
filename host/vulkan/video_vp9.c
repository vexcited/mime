/*
 * converts DXVA VP9 pictures to Vulkan video. DXVA supplies the parsed uncompressed header except
 * refresh_frame_flags, which is read from the frame bitstream. the bitstream includes both headers and all
 * tiles.
 */
#include "video.h"

#include <stdlib.h>
#include <string.h>

typedef struct Bits {
    const uint8_t *data;
    uint32_t size, at;
} Bits;

static uint32_t bits_get(Bits *b, uint32_t n)
{
    uint32_t v = 0;
    while (n--) {
        uint32_t bit = b->at < b->size * 8 ? (b->data[b->at >> 3] >> (7 - (b->at & 7))) & 1 : 0;
        b->at++;
        v = v << 1 | bit;
    }
    return v;
}

/* refresh_frame_flags of the frame header, which sits after fields the picture parameters do carry. */
static uint8_t refresh_flags(const uint8_t *data, uint32_t size, const DxvaPicParamsVP9 *p)
{
    if (!p->frame_type)
        return 0xFF;
    Bits b = {data, size, 0};
    bits_get(&b, 2); /* frame_marker */
    uint32_t profile = bits_get(&b, 1);
    profile |= bits_get(&b, 1) << 1;
    if (profile == 3)
        bits_get(&b, 1);
    if (bits_get(&b, 1)) /* show_existing_frame */
        return 0;
    bits_get(&b, 1); /* frame_type */
    uint32_t show_frame = bits_get(&b, 1);
    uint32_t error_resilient = bits_get(&b, 1);
    uint32_t intra_only = show_frame ? 0 : bits_get(&b, 1);
    if (!error_resilient)
        bits_get(&b, 2); /* reset_frame_context */
    if (intra_only) {
        bits_get(&b, 24); /* the sync code. */
        if (profile > 0) {
            if (profile >= 2)
                bits_get(&b, 1); /* ten or twelve bits. */
            uint32_t color_space = bits_get(&b, 3);
            if (color_space != 7) {
                bits_get(&b, 1); /* color_range */
                if (profile == 1 || profile == 3)
                    bits_get(&b, 3);
            } else if (profile == 1 || profile == 3) {
                bits_get(&b, 1);
            }
        }
    }
    return (uint8_t)bits_get(&b, 8);
}

bool video_vp9_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f)
{
    DxvaPicParamsVP9 p;
    if (in->params_size < sizeof(p)) {
        mime_warn("video: a vp9 picture parameter buffer of %u bytes", in->params_size);
        return false;
    }
    memcpy(&p, in->params, sizeof(p));
    uint32_t headers = p.uncompressed_header_size_byte_aligned;
    uint32_t tiles = headers + p.first_partition_size;
    if (!headers || tiles >= in->bitstream_size) {
        mime_warn("video: a vp9 frame of %u bytes with headers of %u and %u", in->bitstream_size, headers,
                  p.first_partition_size);
        return false;
    }
    uint8_t keep[8];
    uint32_t keep_count = 0;
    for (uint32_t i = 0; i < 8; i++)
        if (p.ref_frame_map[i].byte != DXVA_INVALID_ENTRY && p.ref_frame_map[i].index < VIDEO_MAX_INDEX)
            keep[keep_count++] = p.ref_frame_map[i].index;
    video_retain(s, keep, keep_count);
    VideoPicture *cur = video_current(c, s, p.curr_pic.index, in, image, layer, p.width, p.height);
    if (!cur) {
        mime_warn("video: no picture or slot for vp9 index %u", p.curr_pic.index);
        return false;
    }
    f->current = cur;
    /* any picture may be a reference to a later one, the reference map says which are. */
    f->is_reference = true;

    /* the three frame references, deduplicated and mapped to DPB slots. */
    VkVideoDecodeVP9PictureInfoKHR *pic = &f->u.vp9.picture;
    *pic = (VkVideoDecodeVP9PictureInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PICTURE_INFO_KHR};
    VideoPicture *used[3] = {0};
    for (uint32_t i = 0; i < 3; i++) {
        pic->referenceNameSlotIndices[i] = -1;
        if (!p.frame_type || p.intra_only || p.frame_refs[i].byte == DXVA_INVALID_ENTRY)
            continue;
        VideoPicture *r = video_ref(c, s, p.frame_refs[i].index);
        if (!r)
            continue;
        pic->referenceNameSlotIndices[i] = r->slot;
        used[i] = r;
        bool known = false;
        for (uint32_t j = 0; j < f->ref_count; j++)
            known |= f->ref_pictures[j] == r;
        if (known || f->ref_count >= s->max_active)
            continue;
        uint32_t k = f->ref_count++;
        video_picture_resource(r, &f->ref_resources[k]);
        f->refs[k] = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, nullptr, r->slot,
                                                   &f->ref_resources[k]};
        f->ref_pictures[k] = r;
    }
    (void)used;
    video_picture_resource(cur, &f->setup_resource);
    f->setup = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, nullptr, cur->slot,
                                             &f->setup_resource};

    StdVideoDecodeVP9PictureInfo *std = &f->u.vp9.std_picture;
    std->flags.error_resilient_mode = p.error_resilient_mode;
    std->flags.intra_only = p.intra_only;
    std->flags.allow_high_precision_mv = p.allow_high_precision_mv;
    std->flags.refresh_frame_context = p.refresh_frame_context;
    std->flags.frame_parallel_decoding_mode = p.frame_parallel_decoding_mode;
    std->flags.segmentation_enabled = p.segments.enabled;
    std->flags.show_frame = p.show_frame;
    std->flags.UsePrevFrameMvs = p.use_prev_in_find_mv_refs;
    std->profile = (StdVideoVP9Profile)p.profile;
    std->frame_type = p.frame_type ? STD_VIDEO_VP9_FRAME_TYPE_NON_KEY : STD_VIDEO_VP9_FRAME_TYPE_KEY;
    std->frame_context_idx = p.frame_context_idx;
    std->reset_frame_context = p.reset_frame_context;
    std->refresh_frame_flags = refresh_flags(in->bitstream, in->bitstream_size, &p);
    for (uint32_t i = 1; i < 4; i++)
        if (p.ref_frame_sign_bias[i])
            std->ref_frame_sign_bias_mask |= 1u << i;
    std->interpolation_filter = (StdVideoVP9InterpolationFilter)p.interp_filter;
    std->base_q_idx = (uint8_t)p.base_qindex;
    std->delta_q_y_dc = p.y_dc_delta_q;
    std->delta_q_uv_dc = p.uv_dc_delta_q;
    std->delta_q_uv_ac = p.uv_ac_delta_q;
    std->tile_cols_log2 = p.log2_tile_cols;
    std->tile_rows_log2 = p.log2_tile_rows;

    StdVideoVP9ColorConfig *color = &f->u.vp9.color;
    color->BitDepth = p.bit_depth_minus8_luma + 8;
    color->subsampling_x = p.subsampling_x;
    color->subsampling_y = p.subsampling_y;
    color->color_space = STD_VIDEO_VP9_COLOR_SPACE_UNKNOWN;
    std->pColorConfig = color;

    /* the deltas and segment data are given whole, as the frame uses them, so all count as updated. */
    StdVideoVP9LoopFilter *lf = &f->u.vp9.loop_filter;
    lf->flags.loop_filter_delta_enabled = p.mode_ref_delta_enabled;
    lf->flags.loop_filter_delta_update = p.mode_ref_delta_enabled;
    lf->loop_filter_level = (uint8_t)p.filter_level;
    lf->loop_filter_sharpness = (uint8_t)p.sharpness_level;
    if (p.mode_ref_delta_enabled) {
        lf->update_ref_delta = 0xF;
        lf->update_mode_delta = 0x3;
    }
    memcpy(lf->loop_filter_ref_deltas, p.ref_deltas, sizeof(p.ref_deltas));
    memcpy(lf->loop_filter_mode_deltas, p.mode_deltas, sizeof(p.mode_deltas));
    std->pLoopFilter = lf;

    StdVideoVP9Segmentation *seg = &f->u.vp9.segmentation;
    if (p.segments.enabled) {
        seg->flags.segmentation_update_map = p.segments.update_map;
        seg->flags.segmentation_temporal_update = p.segments.temporal_update;
        seg->flags.segmentation_update_data = 1;
        seg->flags.segmentation_abs_or_delta_update = p.segments.abs_delta;
        memcpy(seg->segmentation_tree_probs, p.segments.tree_probs, sizeof(p.segments.tree_probs));
        memcpy(seg->segmentation_pred_prob, p.segments.pred_probs, sizeof(p.segments.pred_probs));
        memcpy(seg->FeatureEnabled, p.segments.feature_mask, sizeof(p.segments.feature_mask));
        memcpy(seg->FeatureData, p.segments.feature_data, sizeof(p.segments.feature_data));
    }
    std->pSegmentation = seg;

    pic->pStdPictureInfo = std;
    pic->uncompressedHeaderOffset = 0;
    pic->compressedHeaderOffset = headers;
    pic->tilesOffset = tiles;
    f->info = (VkVideoDecodeInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR, pic};
    video_picture_resource(cur, &f->info.dstPictureResource);
    f->info.pSetupReferenceSlot = &f->setup;
    f->info.referenceSlotCount = f->ref_count;
    f->info.pReferenceSlots = f->refs;
    return true;
}
