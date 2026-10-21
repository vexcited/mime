/*
 * dxva h.265 pictures as vulkan video decodes them. dxva holds no parameter sets, only what decoding needs of
 * them in the picture parameters, so the video, sequence and picture parameter sets vulkan wants are made from
 * those, one object for as long as the values do not change. what the picture predicts from is already resolved
 * by the program: the lists of the reference pictures name surfaces, and vulkan names the dpb slots the
 * decoder holds them in.
 */
#include "video.h"

#include <stdlib.h>
#include <string.h>

#define NO_REFERENCE STD_VIDEO_H265_NO_REFERENCE_PICTURE
#define LIST_SIZE 15

typedef struct H265Key {
    StdVideoH265VideoParameterSet vps;
    StdVideoH265ProfileTierLevel level;
    StdVideoH265DecPicBufMgr buffering;
    StdVideoH265SequenceParameterSet sps;
    StdVideoH265PictureParameterSet pps;
    StdVideoH265ScalingLists lists;
} H265Key;

/* the sets of the sequence the slice headers name, which the gpu needs the count of and nothing more. */
typedef struct H265Tables {
    StdVideoH265ShortTermRefPicSet short_term[STD_VIDEO_H265_MAX_SHORT_TERM_REF_PIC_SETS];
    StdVideoH265LongTermRefPicsSps long_term;
} H265Tables;

static void fill_sequence(StdVideoH265SequenceParameterSet *sps, const DxvaPicParamsHEVC *p)
{
    uint32_t min_cb = p->log2_min_luma_coding_block_size_minus3 + 3u;
    sps->flags.sps_temporal_id_nesting_flag = 1;
    sps->flags.separate_colour_plane_flag = p->separate_colour_plane_flag;
    sps->flags.sps_sub_layer_ordering_info_present_flag = 1;
    sps->flags.scaling_list_enabled_flag = p->scaling_list_enabled_flag;
    sps->flags.sps_scaling_list_data_present_flag = p->scaling_list_enabled_flag;
    sps->flags.amp_enabled_flag = p->amp_enabled_flag;
    sps->flags.sample_adaptive_offset_enabled_flag = p->sample_adaptive_offset_enabled_flag;
    sps->flags.pcm_enabled_flag = p->pcm_enabled_flag;
    sps->flags.pcm_loop_filter_disabled_flag = p->pcm_loop_filter_disabled_flag;
    sps->flags.long_term_ref_pics_present_flag = p->long_term_ref_pics_present_flag;
    sps->flags.sps_temporal_mvp_enabled_flag = p->sps_temporal_mvp_enabled_flag;
    sps->flags.strong_intra_smoothing_enabled_flag = p->strong_intra_smoothing_enabled_flag;
    sps->chroma_format_idc = (StdVideoH265ChromaFormatIdc)p->chroma_format_idc;
    sps->pic_width_in_luma_samples = (uint32_t)p->pic_width_in_min_cbs << min_cb;
    sps->pic_height_in_luma_samples = (uint32_t)p->pic_height_in_min_cbs << min_cb;
    sps->bit_depth_luma_minus8 = p->bit_depth_luma_minus8;
    sps->bit_depth_chroma_minus8 = p->bit_depth_chroma_minus8;
    sps->log2_max_pic_order_cnt_lsb_minus4 = p->log2_max_pic_order_cnt_lsb_minus4;
    sps->log2_min_luma_coding_block_size_minus3 = p->log2_min_luma_coding_block_size_minus3;
    sps->log2_diff_max_min_luma_coding_block_size = p->log2_diff_max_min_luma_coding_block_size;
    sps->log2_min_luma_transform_block_size_minus2 = p->log2_min_transform_block_size_minus2;
    sps->log2_diff_max_min_luma_transform_block_size = p->log2_diff_max_min_transform_block_size;
    sps->max_transform_hierarchy_depth_inter = p->max_transform_hierarchy_depth_inter;
    sps->max_transform_hierarchy_depth_intra = p->max_transform_hierarchy_depth_intra;
    sps->num_short_term_ref_pic_sets = p->num_short_term_ref_pic_sets;
    sps->num_long_term_ref_pics_sps = p->num_long_term_ref_pics_sps;
    if (p->pcm_enabled_flag) {
        sps->pcm_sample_bit_depth_luma_minus1 = p->pcm_sample_bit_depth_luma_minus1;
        sps->pcm_sample_bit_depth_chroma_minus1 = p->pcm_sample_bit_depth_chroma_minus1;
        sps->log2_min_pcm_luma_coding_block_size_minus3 = p->log2_min_pcm_luma_coding_block_size_minus3;
        sps->log2_diff_max_min_pcm_luma_coding_block_size = p->log2_diff_max_min_pcm_luma_coding_block_size;
    }
}

static void fill_picture_set(StdVideoH265PictureParameterSet *pps, const DxvaPicParamsHEVC *p)
{
    pps->flags.dependent_slice_segments_enabled_flag = p->dependent_slice_segments_enabled_flag;
    pps->flags.output_flag_present_flag = p->output_flag_present_flag;
    pps->flags.sign_data_hiding_enabled_flag = p->sign_data_hiding_enabled_flag;
    pps->flags.cabac_init_present_flag = p->cabac_init_present_flag;
    pps->flags.constrained_intra_pred_flag = p->constrained_intra_pred_flag;
    pps->flags.transform_skip_enabled_flag = p->transform_skip_enabled_flag;
    pps->flags.cu_qp_delta_enabled_flag = p->cu_qp_delta_enabled_flag;
    pps->flags.pps_slice_chroma_qp_offsets_present_flag = p->pps_slice_chroma_qp_offsets_present_flag;
    pps->flags.weighted_pred_flag = p->weighted_pred_flag;
    pps->flags.weighted_bipred_flag = p->weighted_bipred_flag;
    pps->flags.transquant_bypass_enabled_flag = p->transquant_bypass_enabled_flag;
    pps->flags.tiles_enabled_flag = p->tiles_enabled_flag;
    pps->flags.entropy_coding_sync_enabled_flag = p->entropy_coding_sync_enabled_flag;
    pps->flags.uniform_spacing_flag = p->uniform_spacing_flag;
    pps->flags.loop_filter_across_tiles_enabled_flag = p->loop_filter_across_tiles_enabled_flag;
    pps->flags.pps_loop_filter_across_slices_enabled_flag = p->pps_loop_filter_across_slices_enabled_flag;
    /* the deblocking control is in the picture parameters in parts: the override and the disable flag. */
    pps->flags.deblocking_filter_control_present_flag =
        p->deblocking_filter_override_enabled_flag || p->pps_deblocking_filter_disabled_flag || p->pps_beta_offset_div2 ||
        p->pps_tc_offset_div2;
    pps->flags.deblocking_filter_override_enabled_flag = p->deblocking_filter_override_enabled_flag;
    pps->flags.pps_deblocking_filter_disabled_flag = p->pps_deblocking_filter_disabled_flag;
    pps->flags.lists_modification_present_flag = p->lists_modification_present_flag;
    pps->flags.slice_segment_header_extension_present_flag = p->slice_segment_header_extension_present_flag;
    pps->num_extra_slice_header_bits = p->num_extra_slice_header_bits;
    pps->num_ref_idx_l0_default_active_minus1 = p->num_ref_idx_l0_default_active_minus1;
    pps->num_ref_idx_l1_default_active_minus1 = p->num_ref_idx_l1_default_active_minus1;
    pps->init_qp_minus26 = p->init_qp_minus26;
    pps->diff_cu_qp_delta_depth = p->diff_cu_qp_delta_depth;
    pps->pps_cb_qp_offset = p->pps_cb_qp_offset;
    pps->pps_cr_qp_offset = p->pps_cr_qp_offset;
    pps->pps_beta_offset_div2 = p->pps_beta_offset_div2;
    pps->pps_tc_offset_div2 = p->pps_tc_offset_div2;
    pps->log2_parallel_merge_level_minus2 = p->log2_parallel_merge_level_minus2;
    pps->num_tile_columns_minus1 = p->num_tile_columns_minus1;
    pps->num_tile_rows_minus1 = p->num_tile_rows_minus1;
    memcpy(pps->column_width_minus1, p->column_width_minus1, sizeof(pps->column_width_minus1));
    memcpy(pps->row_height_minus1, p->row_height_minus1, sizeof(pps->row_height_minus1));
}

bool video_h265_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                        VideoFrame *f)
{
    MvkVideo *v = c->video;
    DxvaPicParamsHEVC p;
    if (in->params_size < sizeof(p)) {
        mime_warn("video: an h.265 picture parameter buffer of %u bytes", in->params_size);
        return false;
    }
    memcpy(&p, in->params, sizeof(p));
    if (in->slices_size < sizeof(DxvaSliceShort) || in->slice_count == 0) {
        mime_warn("video: an h.265 picture without short slices");
        return false;
    }
    if (p.chroma_format_idc != 1 || p.separate_colour_plane_flag) {
        mime_warn("video: an h.265 picture that is not 4:2:0 is not decoded");
        return false;
    }
    uint8_t keep[LIST_SIZE];
    uint32_t keep_count = 0;
    for (uint32_t i = 0; i < LIST_SIZE; i++)
        if (p.ref_pic_list[i].byte != DXVA_INVALID_ENTRY && p.ref_pic_list[i].index < VIDEO_MAX_INDEX)
            keep[keep_count++] = p.ref_pic_list[i].index;
    video_retain(s, keep, keep_count);
    uint32_t min_cb = p.log2_min_luma_coding_block_size_minus3 + 3u;
    uint32_t width = (uint32_t)p.pic_width_in_min_cbs << min_cb, height = (uint32_t)p.pic_height_in_min_cbs << min_cb;
    VideoPicture *cur = video_current(c, s, p.curr_pic.index, in, image, layer, width, height);
    if (!cur) {
        mime_warn("video: no picture or slot for h.265 index %u", p.curr_pic.index);
        return false;
    }
    f->current = cur;
    /* the program keeps the pictures a later one predicts from in its list, which says which pictures were references. */
    f->is_reference = true;

    /* the parameter sets. */
    H265Key key;
    memset(&key, 0, sizeof(key));
    bool deep = p.bit_depth_luma_minus8 != 0 || p.bit_depth_chroma_minus8 != 0;
    key.level.general_profile_idc = deep ? STD_VIDEO_H265_PROFILE_IDC_MAIN_10 : STD_VIDEO_H265_PROFILE_IDC_MAIN;
    key.level.general_level_idc = v->h265_level[s->profile_index];
    key.level.flags.general_progressive_source_flag = 1;
    key.level.flags.general_frame_only_constraint_flag = 1;
    key.buffering.max_dec_pic_buffering_minus1[0] = p.sps_max_dec_pic_buffering_minus1;
    key.buffering.max_num_reorder_pics[0] = p.no_pic_reordering_flag ? 0 : p.sps_max_dec_pic_buffering_minus1;
    key.vps.flags.vps_temporal_id_nesting_flag = 1;
    key.vps.flags.vps_sub_layer_ordering_info_present_flag = 1;
    fill_sequence(&key.sps, &p);
    fill_picture_set(&key.pps, &p);
    if (p.scaling_list_enabled_flag) {
        DxvaQmatrixHEVC q;
        if (in->qmatrix_size >= sizeof(q)) {
            memcpy(&q, in->qmatrix, sizeof(q));
        } else {
            /* the flat lists. */
            memset(&q, 16, sizeof(q));
        }
        memcpy(key.lists.ScalingList4x4, q.lists_4x4, sizeof(q.lists_4x4));
        memcpy(key.lists.ScalingList8x8, q.lists_8x8, sizeof(q.lists_8x8));
        memcpy(key.lists.ScalingList16x16, q.lists_16x16, sizeof(q.lists_16x16));
        memcpy(key.lists.ScalingList32x32, q.lists_32x32, sizeof(q.lists_32x32));
        memcpy(key.lists.ScalingListDCCoef16x16, q.dc_16x16, sizeof(q.dc_16x16));
        memcpy(key.lists.ScalingListDCCoef32x32, q.dc_32x32, sizeof(q.dc_32x32));
    }
    H265Key work = key;
    H265Tables tables;
    memset(&tables, 0, sizeof(tables));
    work.vps.pDecPicBufMgr = &work.buffering;
    work.vps.pProfileTierLevel = &work.level;
    work.sps.pProfileTierLevel = &work.level;
    work.sps.pDecPicBufMgr = &work.buffering;
    work.sps.pShortTermRefPicSet = tables.short_term;
    work.sps.pLongTermRefPicsSps = &tables.long_term;
    if (p.scaling_list_enabled_flag)
        work.sps.pScalingLists = &work.lists;
    VkVideoDecodeH265SessionParametersAddInfoKHR add = {
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR, nullptr, 1, &work.vps, 1, &work.sps, 1, &work.pps};
    VkVideoDecodeH265SessionParametersCreateInfoKHR h265 = {
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR, nullptr, 1, 1, 1, &add};
    VkVideoSessionParametersCreateInfoKHR create = {VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR, &h265};
    if (!video_set_parameters(c, s, &key, sizeof(key), &create)) {
        mime_warn("video: the h.265 parameter sets were refused");
        return false;
    }
    f->params = s->params;

    /* the pictures of the dpb, by the slots they hold. */
    uint8_t slot_of[LIST_SIZE];
    memset(slot_of, NO_REFERENCE, sizeof(slot_of));
    for (uint32_t i = 0; i < LIST_SIZE; i++) {
        DxvaPicEntry e = p.ref_pic_list[i];
        if (e.byte == DXVA_INVALID_ENTRY)
            continue;
        VideoPicture *r = video_ref(c, s, e.index);
        if (!r || f->ref_count >= s->max_active)
            continue;
        uint32_t k = f->ref_count++;
        StdVideoDecodeH265ReferenceInfo *ri = &f->u.h265.std_refs[k];
        ri->flags.used_for_long_term_reference = e.associated;
        ri->PicOrderCntVal = p.pic_order_cnt_val_list[i];
        f->u.h265.slots[k] = (VkVideoDecodeH265DpbSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR,
                                                               nullptr, ri};
        video_picture_resource(r, &f->ref_resources[k]);
        f->refs[k] = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &f->u.h265.slots[k],
                                                   r->slot, &f->ref_resources[k]};
        f->ref_pictures[k] = r;
        slot_of[i] = (uint8_t)r->slot;
    }

    /* the picture itself, which is also the slot it is set up in. */
    uint32_t last = VIDEO_MAX_DPB;
    StdVideoDecodeH265ReferenceInfo *self = &f->u.h265.std_refs[last];
    self->PicOrderCntVal = p.curr_pic_order_cnt_val;
    f->u.h265.slots[last] = (VkVideoDecodeH265DpbSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR, nullptr,
                                                              self};
    video_picture_resource(cur, &f->setup_resource);
    f->setup = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &f->u.h265.slots[last],
                                             cur->slot, &f->setup_resource};

    StdVideoDecodeH265PictureInfo *pic = &f->u.h265.std_picture;
    pic->flags.IrapPicFlag = p.irap_pic_flag;
    pic->flags.IdrPicFlag = p.idr_pic_flag;
    pic->flags.IsReference = 1;
    pic->flags.short_term_ref_pic_set_sps_flag = p.num_bits_for_short_term_rps_in_slice == 0;
    pic->NumDeltaPocsOfRefRpsIdx = p.num_delta_pocs_of_ref_rps_idx;
    pic->PicOrderCntVal = p.curr_pic_order_cnt_val;
    pic->NumBitsForSTRefPicSetInSlice = p.num_bits_for_short_term_rps_in_slice;
    const uint8_t *sets[3] = {p.ref_pic_set_st_curr_before, p.ref_pic_set_st_curr_after, p.ref_pic_set_lt_curr};
    uint8_t *slots[3] = {pic->RefPicSetStCurrBefore, pic->RefPicSetStCurrAfter, pic->RefPicSetLtCurr};
    for (uint32_t k = 0; k < 3; k++)
        for (uint32_t i = 0; i < STD_VIDEO_DECODE_H265_REF_PIC_SET_LIST_SIZE; i++)
            slots[k][i] = sets[k][i] < LIST_SIZE ? slot_of[sets[k][i]] : NO_REFERENCE;

    /* the slice segments: where each begins, the start code included, the pieces a chopped one continues in left out. */
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
        mime_warn("video: an h.265 picture without slices in its bitstream");
        return false;
    }
    f->u.h265.picture = (VkVideoDecodeH265PictureInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR, nullptr, pic,
                                                          n, f->slice_offsets};
    f->info = (VkVideoDecodeInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR, &f->u.h265.picture};
    video_picture_resource(cur, &f->info.dstPictureResource);
    f->info.pSetupReferenceSlot = &f->setup;
    f->info.referenceSlotCount = f->ref_count;
    f->info.pReferenceSlots = f->refs;
    return true;
}
