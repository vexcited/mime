/*
 * converts DXVA H.264 pictures to Vulkan video. reconstruct the missing sequence and picture parameter sets
 * from picture parameters and reuse the object while unchanged. DXVA has already selected the active scaling
 * lists from the sequence and picture, so put them in the sequence.
 */
#include "video.h"

#include <stdlib.h>
#include <string.h>

typedef struct H264Key {
    StdVideoH264SequenceParameterSet sps;
    StdVideoH264PictureParameterSet pps;
    StdVideoH264ScalingLists lists;
} H264Key;

/* the bits of a slice header, its emulation prevention bytes removed. */
typedef struct Bits {
    uint8_t data[48];
    uint32_t size, at;
} Bits;

static void bits_init(Bits *b, const uint8_t *p, uint32_t size)
{
    b->size = b->at = 0;
    int zeros = 0;
    for (uint32_t i = 0; i < size && b->size < sizeof(b->data); i++) {
        if (zeros >= 2 && p[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = p[i] == 0 ? zeros + 1 : 0;
        b->data[b->size++] = p[i];
    }
}

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

static uint32_t bits_ue(Bits *b)
{
    uint32_t zeros = 0;
    while (zeros < 32 && b->at < b->size * 8 && !bits_get(b, 1))
        zeros++;
    return zeros >= 32 ? 0 : ((1u << zeros) - 1) + bits_get(b, zeros);
}

/* reads the NAL type and IDR pic id from the first slice. */
static void first_slice(const MvkVideoDecodeInfo *in, const DxvaPicParamsH264 *p, bool *idr, uint32_t *idr_pic_id)
{
    *idr = false;
    *idr_pic_id = 0;
    if (in->slices_size < sizeof(DxvaSliceShort))
        return;
    DxvaSliceShort slice;
    memcpy(&slice, in->slices, sizeof(slice));
    uint32_t at = slice.bitstream_location;
    /* the start code, three or four bytes, then the unit's header byte. */
    while (at + 1 < in->bitstream_size && ((const uint8_t *)in->bitstream)[at] == 0)
        at++;
    if (at >= in->bitstream_size || ((const uint8_t *)in->bitstream)[at] != 1)
        return;
    at++;
    if (at >= in->bitstream_size)
        return;
    uint8_t header = ((const uint8_t *)in->bitstream)[at++];
    if ((header & 0x1F) != 5)
        return;
    *idr = true;
    Bits b;
    bits_init(&b, (const uint8_t *)in->bitstream + at, in->bitstream_size - at);
    bits_ue(&b); /* first_mb_in_slice */
    bits_ue(&b); /* slice_type */
    bits_ue(&b); /* pic_parameter_set_id */
    if (p->residual_colour_transform_flag)
        bits_get(&b, 2);
    bits_get(&b, p->log2_max_frame_num_minus4 + 4u);
    if (!p->frame_mbs_only_flag && bits_get(&b, 1))
        bits_get(&b, 1);
    *idr_pic_id = bits_ue(&b);
}

bool video_h264_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f)
{
    DxvaPicParamsH264 p;
    if (in->params_size < sizeof(p)) {
        mime_warn("video: an h.264 picture parameter buffer of %u bytes", in->params_size);
        return false;
    }
    memcpy(&p, in->params, sizeof(p));
    if (p.num_slice_groups_minus1 || in->slices_size < sizeof(DxvaSliceShort) || in->slice_count == 0) {
        mime_warn("video: an h.264 picture without short slices or with slice groups is not decoded");
        return false;
    }
    if (p.field_pic_flag || !p.frame_mbs_only_flag) {
        mime_warn("video: interlaced h.264 pictures are not decoded");
        return false;
    }
    uint8_t keep[16];
    uint32_t keep_count = 0;
    for (uint32_t i = 0; i < 16; i++)
        if (p.ref_frame_list[i].byte != DXVA_INVALID_ENTRY && p.ref_frame_list[i].index < VIDEO_MAX_INDEX)
            keep[keep_count++] = p.ref_frame_list[i].index;
    video_retain(s, keep, keep_count);
    uint32_t width = (p.width_in_mbs_minus1 + 1u) * 16, height = (p.height_in_mbs_minus1 + 1u) * 16;
    VideoPicture *cur = video_current(c, s, p.curr_pic.index, in, image, layer, width, height);
    if (!cur) {
        mime_warn("video: no picture or slot for h.264 index %u", p.curr_pic.index);
        return false;
    }
    f->current = cur;
    f->is_reference = p.ref_pic_flag;

    H264Key key;
    memset(&key, 0, sizeof(key));
    StdVideoH264SequenceParameterSet *sps = &key.sps;
    sps->flags.direct_8x8_inference_flag = p.direct_8x8_inference_flag;
    sps->flags.mb_adaptive_frame_field_flag = p.mbaff_frame_flag;
    sps->flags.frame_mbs_only_flag = p.frame_mbs_only_flag;
    sps->flags.delta_pic_order_always_zero_flag = p.delta_pic_order_always_zero_flag;
    sps->flags.separate_colour_plane_flag = p.residual_colour_transform_flag;
    sps->flags.seq_scaling_matrix_present_flag = 1;
    sps->profile_idc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
    sps->level_idc = STD_VIDEO_H264_LEVEL_IDC_5_1;
    sps->chroma_format_idc = (StdVideoH264ChromaFormatIdc)p.chroma_format_idc;
    sps->bit_depth_luma_minus8 = p.bit_depth_luma_minus8;
    sps->bit_depth_chroma_minus8 = p.bit_depth_chroma_minus8;
    sps->log2_max_frame_num_minus4 = p.log2_max_frame_num_minus4;
    sps->pic_order_cnt_type = (StdVideoH264PocType)p.pic_order_cnt_type;
    sps->log2_max_pic_order_cnt_lsb_minus4 = p.log2_max_pic_order_cnt_lsb_minus4;
    sps->max_num_ref_frames = p.num_ref_frames;
    sps->pic_width_in_mbs_minus1 = p.width_in_mbs_minus1;
    sps->pic_height_in_map_units_minus1 = p.height_in_mbs_minus1;
    StdVideoH264PictureParameterSet *pps = &key.pps;
    pps->flags.transform_8x8_mode_flag = p.transform_8x8_mode_flag;
    pps->flags.redundant_pic_cnt_present_flag = p.redundant_pic_cnt_present_flag;
    pps->flags.constrained_intra_pred_flag = p.constrained_intra_pred_flag;
    pps->flags.deblocking_filter_control_present_flag = p.deblocking_filter_control_present_flag;
    pps->flags.weighted_pred_flag = p.weighted_pred_flag;
    pps->flags.bottom_field_pic_order_in_frame_present_flag = p.pic_order_present_flag;
    pps->flags.entropy_coding_mode_flag = p.entropy_coding_mode_flag;
    pps->num_ref_idx_l0_default_active_minus1 = p.num_ref_idx_l0_active_minus1;
    pps->num_ref_idx_l1_default_active_minus1 = p.num_ref_idx_l1_active_minus1;
    pps->weighted_bipred_idc = (StdVideoH264WeightedBipredIdc)p.weighted_bipred_idc;
    pps->pic_init_qp_minus26 = p.pic_init_qp_minus26;
    pps->pic_init_qs_minus26 = p.pic_init_qs_minus26;
    pps->chroma_qp_index_offset = p.chroma_qp_index_offset;
    pps->second_chroma_qp_index_offset = p.second_chroma_qp_index_offset;
    DxvaQmatrixH264 q;
    if (in->qmatrix_size >= sizeof(q)) {
        memcpy(&q, in->qmatrix, sizeof(q));
    } else {
        memset(&q, 16, sizeof(q));
    }
    key.lists.scaling_list_present_mask = 0xFF;
    memcpy(key.lists.ScalingList4x4, q.scaling_lists_4x4, sizeof(q.scaling_lists_4x4));
    memcpy(key.lists.ScalingList8x8, q.scaling_lists_8x8, sizeof(q.scaling_lists_8x8));
    H264Key work = key;
    work.sps.pScalingLists = &work.lists;
    VkVideoDecodeH264SessionParametersAddInfoKHR add = {VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
                                                        nullptr, 1, &work.sps, 1, &work.pps};
    VkVideoDecodeH264SessionParametersCreateInfoKHR h264 = {
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR, nullptr, 1, 1, &add};
    VkVideoSessionParametersCreateInfoKHR create = {VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR, &h264};
    if (!video_set_parameters(c, s, &key, sizeof(key), &create)) {
        mime_warn("video: the h.264 parameter sets were refused");
        return false;
    }
    f->params = s->params;

    /* maps reference pictures to DPB slots. */
    for (uint32_t i = 0; i < 16; i++) {
        DxvaPicEntry e = p.ref_frame_list[i];
        if (e.byte == DXVA_INVALID_ENTRY)
            continue;
        VideoPicture *r = video_ref(c, s, e.index);
        if (!r || f->ref_count >= s->max_active)
            continue;
        uint32_t k = f->ref_count++;
        StdVideoDecodeH264ReferenceInfo *ri = &f->u.h264.std_refs[k];
        /* a frame has neither field flag set. */
        ri->flags.used_for_long_term_reference = e.associated;
        ri->flags.is_non_existing = (p.non_existing_frame_flags >> i) & 1;
        ri->FrameNum = p.frame_num_list[i];
        ri->PicOrderCnt[0] = p.field_order_cnt_list[i][0];
        ri->PicOrderCnt[1] = p.field_order_cnt_list[i][1];
        f->u.h264.slots[k] = (VkVideoDecodeH264DpbSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR,
                                                               nullptr, ri};
        video_picture_resource(r, &f->ref_resources[k]);
        f->refs[k] = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &f->u.h264.slots[k],
                                                   r->slot, &f->ref_resources[k]};
        f->ref_pictures[k] = r;
    }

    /* the output picture also supplies its setup slot. */
    uint32_t last = VIDEO_MAX_DPB;
    StdVideoDecodeH264ReferenceInfo *self = &f->u.h264.std_refs[last];
    self->FrameNum = p.frame_num;
    self->PicOrderCnt[0] = p.curr_field_order_cnt[0];
    self->PicOrderCnt[1] = p.curr_field_order_cnt[1];
    f->u.h264.slots[last] = (VkVideoDecodeH264DpbSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR,
                                                              nullptr, self};
    video_picture_resource(cur, &f->setup_resource);
    f->setup = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &f->u.h264.slots[last],
                                             cur->slot, &f->setup_resource};

    bool idr;
    uint32_t idr_pic_id;
    first_slice(in, &p, &idr, &idr_pic_id);
    StdVideoDecodeH264PictureInfo *pic = &f->u.h264.std_picture;
    pic->flags.field_pic_flag = p.field_pic_flag;
    pic->flags.is_intra = p.intra_pic_flag;
    pic->flags.IdrPicFlag = idr;
    pic->flags.bottom_field_flag = p.curr_pic.associated;
    pic->flags.is_reference = p.ref_pic_flag;
    pic->frame_num = p.frame_num;
    pic->idr_pic_id = (uint16_t)idr_pic_id;
    pic->PicOrderCnt[0] = p.curr_field_order_cnt[0];
    pic->PicOrderCnt[1] = p.curr_field_order_cnt[1];

    /* slice offsets include start codes and omit continuation fragments. */
    uint32_t count = in->slices_size / sizeof(DxvaSliceShort);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < VIDEO_MAX_SLICES; i++) {
        DxvaSliceShort slice;
        memcpy(&slice, (const uint8_t *)in->slices + i * sizeof(slice), sizeof(slice));
        if ((slice.bad_slice_chopping & 2) || slice.bitstream_location >= in->bitstream_size)
            continue;
        f->slice_offsets[n++] = slice.bitstream_location;
    }
    if (!n) {
        mime_warn("video: an h.264 picture without slices in its bitstream");
        return false;
    }
    f->u.h264.picture = (VkVideoDecodeH264PictureInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR, nullptr, pic,
                                                          n, f->slice_offsets};
    f->info = (VkVideoDecodeInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR, &f->u.h264.picture};
    video_picture_resource(cur, &f->info.dstPictureResource);
    f->info.pSetupReferenceSlot = &f->setup;
    f->info.referenceSlotCount = f->ref_count;
    f->info.pReferenceSlots = f->refs;
    return true;
}
