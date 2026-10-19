/*
 * dxva av1 pictures as vulkan video decodes them. dxva carries the frame header with every reference resolved,
 * the tile control entries and the tile data alone, no obu headers. the sequence header vulkan wants is not in the
 * buffers, so the one made from the picture parameters is kept in the session's parameters object for as long as
 * its values do not change. what dxva leaves out is guessed as the comments say, every guess being a value that
 * does not change the decoding of the frame.
 *
 * film grain is applied by the decoder into the surface of the guest, the picture the guest keeps referring to
 * has none: such a picture is decoded as the mirror video_mirror makes, and the surface of the guest gets the
 * decode output.
 */
#include "video.h"

#include <stdlib.h>
#include <string.h>

#define NO_SLOT 0xFF

typedef struct Av1Key {
    StdVideoAV1SequenceHeader sequence;
    StdVideoAV1ColorConfig color;
} Av1Key;

static uint32_t bit_length(uint32_t v)
{
    uint32_t n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static int relative_dist(uint32_t order_hint_bits, int a, int b)
{
    if (!order_hint_bits)
        return 0;
    int diff = a - b, m = 1 << (order_hint_bits - 1);
    return (diff & (m - 1)) - (diff & m);
}

/* whether the sizes in superblocks are what uniform spacing makes of the count, as the tile info codes it. */
static bool uniform_spacing(const uint16_t *sizes, uint32_t count, uint32_t total)
{
    for (uint32_t log2 = 0; log2 <= 6; log2++) {
        uint32_t size = (total + (1u << log2) - 1) >> log2;
        if (!size || (total + size - 1) / size != count)
            continue;
        bool same = true;
        for (uint32_t i = 0; i < count; i++)
            same &= sizes[i] == (i + 1 < count ? size : total - size * (count - 1));
        if (same)
            return true;
    }
    return false;
}

/* the picture a reference is read from: the mirror when the guest's image holds it with film grain. */
static void reference_resource(const VideoPicture *p, VkVideoPictureResourceInfoKHR *r)
{
    video_picture_resource(p, r);
    if (p->mirrored)
        r->imageViewBinding = p->mirror_view;
}

bool video_av1_prepare(MvkConnection *c, VideoSession *s, const MvkVideoDecodeInfo *in, uint64_t image, uint32_t layer,
                       VideoFrame *f)
{
    DxvaPicParamsAV1 p;
    if (in->params_size < sizeof(p)) {
        mime_warn("video: an av1 picture parameter buffer of %u bytes", in->params_size);
        return false;
    }
    memcpy(&p, in->params, sizeof(p));
    if (p.bitdepth != s->bit_depth || p.seq_profile != s->profile || p.format.mono_chrome || !p.format.subsampling_x ||
        !p.format.subsampling_y) {
        mime_warn("video: an av1 picture of profile %u at %u bits is not decoded by this session", p.seq_profile,
                  p.bitdepth);
        return false;
    }

    /* the tiles, in raster order whatever order the entries are in. */
    uint32_t cols = p.tiles.cols, rows = p.tiles.rows;
    uint32_t tile_count = in->slices_size / (uint32_t)sizeof(DxvaTileAV1);
    if (!cols || !rows || cols > 64 || rows > 64 || tile_count != cols * rows || tile_count > VIDEO_MAX_SLICES) {
        mime_warn("video: an av1 picture of %ux%u tiles with %u tile entries", cols, rows, tile_count);
        return false;
    }
    uint32_t *sizes = f->u.av1.tile_sizes;
    for (uint32_t i = 0; i < tile_count; i++) {
        DxvaTileAV1 t;
        memcpy(&t, (const uint8_t *)in->slices + i * sizeof(t), sizeof(t));
        uint32_t n = (uint32_t)t.row * cols + t.column;
        if (t.anchor_frame != 0xFF || t.row >= rows || t.column >= cols || sizes[n] || !t.data_size ||
            t.data_offset > in->bitstream_size || t.data_size > in->bitstream_size - t.data_offset) {
            mime_warn("video: av1 tile %u at %u+%u of %u bytes is not decodable", i, t.data_offset, t.data_size,
                      in->bitstream_size);
            return false;
        }
        f->slice_offsets[n] = t.data_offset;
        sizes[n] = t.data_size;
    }

    uint8_t keep[8];
    uint32_t keep_count = 0;
    for (uint32_t i = 0; i < 8; i++)
        if (p.ref_frame_map_texture_index[i] < VIDEO_MAX_INDEX)
            keep[keep_count++] = p.ref_frame_map_texture_index[i];
    video_retain(s, keep, keep_count);
    VideoPicture *cur = video_current(c, s, p.curr_pic_texture_index, in, image, layer, p.width, p.height);
    if (!cur) {
        mime_warn("video: no picture or slot for av1 index %u", p.curr_pic_texture_index);
        return false;
    }
    f->current = cur;
    /* any picture may be a reference to a later one, the reference map says which are. */
    f->is_reference = true;
    bool intra = p.format.frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY ||
                 p.format.frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY;
    bool grain = p.film_grain.apply_grain;
    bool uses_lr = false, uses_chroma_lr = false;
    for (uint32_t i = 0; i < 3; i++) {
        uses_lr |= p.loop_filter.frame_restoration_type[i] != 0;
        uses_chroma_lr |= i > 0 && p.loop_filter.frame_restoration_type[i] != 0;
    }

    /* the film grain output is another image than the one the picture is reconstructed in. */
    if (grain && !video_mirror(c, s, cur)) {
        mime_warn("video: no image to reconstruct the film grain picture %u in", p.curr_pic_texture_index);
        return false;
    }
    cur->mirrored = grain;

    /* the sequence header: the values the picture parameters repeat from it, the others as the comments say. */
    if (grain)
        s->av1.matrix_identity = p.film_grain.matrix_coeff_is_identity;
    Av1Key key;
    memset(&key, 0, sizeof(key));
    StdVideoAV1SequenceHeader *seq = &key.sequence;
    seq->flags.use_128x128_superblock = p.coding.use_128x128_superblock;
    seq->flags.enable_filter_intra = p.coding.filter_intra;
    seq->flags.enable_intra_edge_filter = p.coding.intra_edge_filter;
    seq->flags.enable_interintra_compound = p.coding.interintra_compound;
    seq->flags.enable_masked_compound = p.coding.masked_compound;
    /* dxva has the picture's allow_warped_motion, which is all decoding reads. */
    seq->flags.enable_warped_motion = 1;
    seq->flags.enable_dual_filter = p.coding.dual_filter;
    seq->flags.enable_order_hint = p.order_hint_bits != 0;
    seq->flags.enable_jnt_comp = p.coding.jnt_comp;
    seq->flags.enable_ref_frame_mvs = p.coding.enable_ref_frame_mvs;
    /* likewise the picture's use_superres. */
    seq->flags.enable_superres = 1;
    seq->flags.enable_cdef = p.coding.cdef;
    seq->flags.enable_restoration = p.coding.restoration;
    seq->flags.film_grain_params_present = p.coding.film_grain;
    seq->seq_profile = (StdVideoAV1Profile)p.seq_profile;
    seq->frame_width_bits_minus_1 = (uint8_t)(bit_length(p.max_width > 1 ? p.max_width - 1 : 1) - 1);
    seq->frame_height_bits_minus_1 = (uint8_t)(bit_length(p.max_height > 1 ? p.max_height - 1 : 1) - 1);
    seq->max_frame_width_minus_1 = (uint16_t)(p.max_width - 1);
    seq->max_frame_height_minus_1 = (uint16_t)(p.max_height - 1);
    seq->order_hint_bits_minus_1 = p.order_hint_bits ? (uint8_t)(p.order_hint_bits - 1) : 0;
    seq->seq_force_integer_mv = STD_VIDEO_AV1_SELECT_INTEGER_MV;
    seq->seq_force_screen_content_tools = STD_VIDEO_AV1_SELECT_SCREEN_CONTENT_TOOLS;
    StdVideoAV1ColorConfig *color = &key.color;
    color->flags.mono_chrome = 0;
    /* so a picture's diff_uv_delta can be set, the frame header codes it only then. */
    color->flags.separate_uv_delta_q = 1;
    color->flags.color_description_present_flag = s->av1.matrix_identity;
    color->BitDepth = p.bitdepth;
    color->subsampling_x = p.format.subsampling_x;
    color->subsampling_y = p.format.subsampling_y;
    color->color_primaries = STD_VIDEO_AV1_COLOR_PRIMARIES_UNSPECIFIED;
    color->transfer_characteristics = STD_VIDEO_AV1_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
    /* the film grain wants to know whether the matrix is the identity, which only the color config says. */
    color->matrix_coefficients = s->av1.matrix_identity ? STD_VIDEO_AV1_MATRIX_COEFFICIENTS_IDENTITY
                                                        : STD_VIDEO_AV1_MATRIX_COEFFICIENTS_UNSPECIFIED;
    color->chroma_sample_position = STD_VIDEO_AV1_CHROMA_SAMPLE_POSITION_UNKNOWN;
    Av1Key work = key;
    work.sequence.pColorConfig = &work.color;
    VkVideoDecodeAV1SessionParametersCreateInfoKHR av1 = {
        VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR, nullptr, &work.sequence};
    VkVideoSessionParametersCreateInfoKHR create = {VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR, &av1};
    if (!video_set_parameters(c, s, &key, sizeof(key), &create)) {
        mime_warn("video: the av1 sequence header was refused");
        return false;
    }
    f->params = s->params;

    /* the references by name: the slot of the map each names, the picture in it, and what that picture was. */
    VkVideoDecodeAV1PictureInfoKHR *pic = &f->u.av1.picture;
    *pic = (VkVideoDecodeAV1PictureInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PICTURE_INFO_KHR};
    StdVideoDecodeAV1PictureInfo *std = &f->u.av1.std_picture;
    uint32_t hints[8] = {0};
    for (uint32_t i = 0; i < 7; i++) {
        pic->referenceNameSlotIndices[i] = -1;
        uint32_t map = p.frame_refs[i].index;
        if (intra || map >= 8 || p.ref_frame_map_texture_index[map] >= VIDEO_MAX_INDEX)
            continue;
        uint32_t texture = p.ref_frame_map_texture_index[map];
        hints[1 + i] = s->av1.refs[texture].OrderHint;
        std->OrderHints[1 + i] = s->av1.refs[texture].OrderHint;
        VideoPicture *r = video_ref(c, s, texture);
        if (!r) {
            mime_warn("video: av1 reference %u is picture %u, which is not decoded", i, texture);
            continue;
        }
        pic->referenceNameSlotIndices[i] = r->slot;
        bool known = false;
        for (uint32_t j = 0; j < f->ref_count; j++)
            known |= f->ref_pictures[j] == r;
        if (known)
            continue;
        if (f->ref_count >= s->max_active) {
            mime_warn("video: an av1 picture with more than %u references", s->max_active);
            pic->referenceNameSlotIndices[i] = -1;
            continue;
        }
        uint32_t k = f->ref_count++;
        reference_resource(r, &f->ref_resources[k]);
        f->u.av1.std_refs[k] = s->av1.refs[texture];
        f->u.av1.slots[k] = (VkVideoDecodeAV1DpbSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR,
                                                             nullptr, &f->u.av1.std_refs[k]};
        f->refs[k] = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &f->u.av1.slots[k],
                                                   r->slot, &f->ref_resources[k]};
        f->ref_pictures[k] = r;
    }

    /* the reference info of the picture, which the later frames read back: its sign biases and order hints. */
    StdVideoDecodeAV1ReferenceInfo *self = &f->u.av1.std_refs[VIDEO_MAX_DPB];
    self->flags.disable_frame_end_update_cdf = p.coding.disable_frame_end_update_cdf;
    self->flags.segmentation_enabled = p.segmentation.enabled;
    self->frame_type = p.format.frame_type;
    self->OrderHint = p.order_hint_bits ? p.order_hint : 0;
    if (!intra) {
        for (uint32_t i = 1; i < 8; i++) {
            self->SavedOrderHints[i] = (uint8_t)hints[i];
            if (relative_dist(p.order_hint_bits, (int)hints[i], self->OrderHint) > 0)
                self->RefFrameSignBias |= (uint8_t)(1u << i);
        }
    }
    s->av1.refs[p.curr_pic_texture_index] = *self;
    f->u.av1.slots[VIDEO_MAX_DPB] = (VkVideoDecodeAV1DpbSlotInfoKHR){
        VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR, nullptr, self};
    video_picture_resource(cur, &f->setup_resource);
    if (grain)
        f->setup_resource.imageViewBinding = cur->mirror_view;
    f->setup = (VkVideoReferenceSlotInfoKHR){VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
                                             &f->u.av1.slots[VIDEO_MAX_DPB], cur->slot, &f->setup_resource};

    /* the tiles: the sizes in superblocks give the starts in units of four samples. */
    StdVideoAV1TileInfo *tiles = &f->u.av1.tile_info;
    uint16_t widths[64], heights[64];
    memcpy(widths, p.tiles.widths, sizeof(widths));
    memcpy(heights, p.tiles.heights, sizeof(heights));
    uint32_t sb_shift = p.coding.use_128x128_superblock ? 5 : 4, sb_cols = 0, sb_rows = 0;
    for (uint32_t i = 0; i < cols; i++) {
        f->u.av1.mi_col_starts[i] = (uint16_t)(sb_cols << sb_shift);
        f->u.av1.width_in_sbs_minus_1[i] = (uint16_t)(widths[i] ? widths[i] - 1 : 0);
        sb_cols += widths[i];
    }
    for (uint32_t i = 0; i < rows; i++) {
        f->u.av1.mi_row_starts[i] = (uint16_t)(sb_rows << sb_shift);
        f->u.av1.height_in_sbs_minus_1[i] = (uint16_t)(heights[i] ? heights[i] - 1 : 0);
        sb_rows += heights[i];
    }
    tiles->flags.uniform_tile_spacing_flag = uniform_spacing(widths, cols, sb_cols) &&
                                             uniform_spacing(heights, rows, sb_rows);
    tiles->TileCols = (uint8_t)cols;
    tiles->TileRows = (uint8_t)rows;
    tiles->context_update_tile_id = p.tiles.context_update_id;
    /* the tile data comes without the sizes the tile group codes in front of it, so this is never read. */
    tiles->tile_size_bytes_minus_1 = 3;
    tiles->pMiColStarts = f->u.av1.mi_col_starts;
    tiles->pMiRowStarts = f->u.av1.mi_row_starts;
    tiles->pWidthInSbsMinus1 = f->u.av1.width_in_sbs_minus_1;
    tiles->pHeightInSbsMinus1 = f->u.av1.height_in_sbs_minus_1;

    StdVideoAV1Quantization *quant = &f->u.av1.quantization;
    quant->flags.using_qmatrix = p.quantization.qm_y != 0xFF;
    quant->flags.diff_uv_delta = p.quantization.v_dc_delta_q != p.quantization.u_dc_delta_q ||
                                 p.quantization.v_ac_delta_q != p.quantization.u_ac_delta_q;
    quant->base_q_idx = p.quantization.base_qindex;
    quant->DeltaQYDc = p.quantization.y_dc_delta_q;
    quant->DeltaQUDc = p.quantization.u_dc_delta_q;
    quant->DeltaQUAc = p.quantization.u_ac_delta_q;
    quant->DeltaQVDc = p.quantization.v_dc_delta_q;
    quant->DeltaQVAc = p.quantization.v_ac_delta_q;
    if (quant->flags.using_qmatrix) {
        quant->qm_y = p.quantization.qm_y;
        quant->qm_u = p.quantization.qm_u;
        quant->qm_v = p.quantization.qm_v;
    }

    StdVideoAV1Segmentation *seg = &f->u.av1.segmentation;
    if (p.segmentation.enabled) {
        memcpy(seg->FeatureEnabled, p.segmentation.feature_mask, sizeof(seg->FeatureEnabled));
        memcpy(seg->FeatureData, p.segmentation.feature_data, sizeof(seg->FeatureData));
    }

    /* the deltas are given whole, as the frame uses them; the masks say which the frame codes anew. */
    StdVideoAV1LoopFilter *lf = &f->u.av1.loop_filter;
    lf->flags.loop_filter_delta_enabled = p.loop_filter.mode_ref_delta_enabled;
    lf->flags.loop_filter_delta_update = p.loop_filter.mode_ref_delta_update;
    lf->loop_filter_level[0] = p.loop_filter.filter_level[0];
    lf->loop_filter_level[1] = p.loop_filter.filter_level[1];
    lf->loop_filter_level[2] = p.loop_filter.filter_level_u;
    lf->loop_filter_level[3] = p.loop_filter.filter_level_v;
    lf->loop_filter_sharpness = p.loop_filter.sharpness_level;
    if (p.loop_filter.mode_ref_delta_update) {
        lf->update_ref_delta = 0xFF;
        lf->update_mode_delta = 0x3;
    }
    memcpy(lf->loop_filter_ref_deltas, p.loop_filter.ref_deltas, sizeof(lf->loop_filter_ref_deltas));
    memcpy(lf->loop_filter_mode_deltas, p.loop_filter.mode_deltas, sizeof(lf->loop_filter_mode_deltas));

    StdVideoAV1CDEF *cdef = &f->u.av1.cdef;
    cdef->cdef_damping_minus_3 = p.cdef.damping;
    cdef->cdef_bits = p.cdef.bits;
    for (uint32_t i = 0; i < 8; i++) {
        cdef->cdef_y_pri_strength[i] = p.cdef.y_strengths[i].primary;
        cdef->cdef_y_sec_strength[i] = p.cdef.y_strengths[i].secondary;
        cdef->cdef_uv_pri_strength[i] = p.cdef.uv_strengths[i].primary;
        cdef->cdef_uv_sec_strength[i] = p.cdef.uv_strengths[i].secondary;
    }

    /* vulkan has the log2 of the unit size less five, the least dxva gives is 64 samples. */
    StdVideoAV1LoopRestoration *lr = &f->u.av1.loop_restoration;
    for (uint32_t i = 0; i < 3; i++) {
        lr->FrameRestorationType[i] = (StdVideoAV1FrameRestorationType)p.loop_filter.frame_restoration_type[i];
        uint32_t log2 = p.loop_filter.log2_restoration_unit_size[i];
        lr->LoopRestorationSize[i] = p.loop_filter.frame_restoration_type[i] && log2 > 5 ? (uint16_t)(log2 - 5) : 0;
    }

    /* the identity wherever the frame has no motion toward the reference, whatever wmmat holds. */
    StdVideoAV1GlobalMotion *gm = &f->u.av1.global_motion;
    for (uint32_t r = 0; r < 8; r++) {
        gm->gm_params[r][2] = 1 << 16;
        gm->gm_params[r][5] = 1 << 16;
    }
    for (uint32_t i = 0; i < 7 && !intra; i++) {
        const DxvaPicEntryAV1 *e = &p.frame_refs[i];
        if (e->index == 0xFF || e->wmtype == 0)
            continue;
        gm->GmType[1 + i] = e->wmtype;
        memcpy(gm->gm_params[1 + i], e->wmmat, sizeof(gm->gm_params[0]));
    }

    /* the parameters dxva has as coded: the multipliers and offsets are the syntax elements, not differences. */
    StdVideoAV1FilmGrain *fg = &f->u.av1.film_grain;
    if (grain) {
        const DxvaFilmGrainAV1 *g = &p.film_grain;
        fg->flags.chroma_scaling_from_luma = g->chroma_scaling_from_luma;
        fg->flags.overlap_flag = g->overlap_flag;
        fg->flags.clip_to_restricted_range = g->clip_to_restricted_range;
        /* vulkan ignores update_grain, the parameters are the ones the frame uses. */
        fg->flags.update_grain = 1;
        fg->grain_scaling_minus_8 = g->scaling_shift_minus8;
        fg->ar_coeff_lag = g->ar_coeff_lag;
        fg->ar_coeff_shift_minus_6 = g->ar_coeff_shift_minus6;
        fg->grain_scale_shift = g->grain_scale_shift;
        fg->grain_seed = g->grain_seed;
        fg->num_y_points = g->num_y_points < 14 ? g->num_y_points : 14;
        for (uint32_t i = 0; i < fg->num_y_points; i++) {
            fg->point_y_value[i] = g->scaling_points_y[i][0];
            fg->point_y_scaling[i] = g->scaling_points_y[i][1];
        }
        fg->num_cb_points = g->num_cb_points < 10 ? g->num_cb_points : 10;
        for (uint32_t i = 0; i < fg->num_cb_points; i++) {
            fg->point_cb_value[i] = g->scaling_points_cb[i][0];
            fg->point_cb_scaling[i] = g->scaling_points_cb[i][1];
        }
        fg->num_cr_points = g->num_cr_points < 10 ? g->num_cr_points : 10;
        for (uint32_t i = 0; i < fg->num_cr_points; i++) {
            fg->point_cr_value[i] = g->scaling_points_cr[i][0];
            fg->point_cr_scaling[i] = g->scaling_points_cr[i][1];
        }
        memcpy(fg->ar_coeffs_y_plus_128, g->ar_coeffs_y, sizeof(fg->ar_coeffs_y_plus_128));
        memcpy(fg->ar_coeffs_cb_plus_128, g->ar_coeffs_cb, sizeof(fg->ar_coeffs_cb_plus_128));
        memcpy(fg->ar_coeffs_cr_plus_128, g->ar_coeffs_cr, sizeof(fg->ar_coeffs_cr_plus_128));
        fg->cb_mult = g->cb_mult;
        fg->cb_luma_mult = g->cb_luma_mult;
        fg->cb_offset = (uint16_t)g->cb_offset;
        fg->cr_mult = g->cr_mult;
        fg->cr_luma_mult = g->cr_luma_mult;
        fg->cr_offset = (uint16_t)g->cr_offset;
    }

    /* the picture itself. */
    bool super = p.coding.superres;
    std->flags.error_resilient_mode = p.format.frame_type == STD_VIDEO_AV1_FRAME_TYPE_SWITCH ||
                                      (p.format.frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY && p.format.show_frame);
    std->flags.disable_cdf_update = p.coding.disable_cdf_update;
    std->flags.use_superres = super;
    std->flags.allow_screen_content_tools = p.coding.screen_content_tools;
    std->flags.is_filter_switchable = p.interp_filter == STD_VIDEO_AV1_INTERPOLATION_FILTER_SWITCHABLE;
    std->flags.force_integer_mv = p.coding.integer_mv;
    std->flags.frame_size_override_flag = p.width != p.max_width || p.height != p.max_height;
    std->flags.allow_intrabc = p.coding.intrabc;
    std->flags.allow_high_precision_mv = p.coding.high_precision_mv;
    std->flags.is_motion_mode_switchable = p.coding.switchable_motion_mode;
    std->flags.use_ref_frame_mvs = p.coding.use_ref_frame_mvs;
    std->flags.disable_frame_end_update_cdf = p.coding.disable_frame_end_update_cdf;
    std->flags.allow_warped_motion = p.coding.warped_motion;
    std->flags.reduced_tx_set = p.coding.reduced_tx_set;
    std->flags.reference_select = p.coding.reference_mode;
    std->flags.skip_mode_present = p.coding.skip_mode;
    std->flags.delta_q_present = p.quantization.delta_q_present;
    std->flags.delta_lf_present = p.loop_filter.delta_lf_present;
    std->flags.delta_lf_multi = p.loop_filter.delta_lf_multi;
    std->flags.segmentation_enabled = p.segmentation.enabled;
    std->flags.segmentation_update_map = p.segmentation.update_map;
    std->flags.segmentation_temporal_update = p.segmentation.temporal_update;
    std->flags.segmentation_update_data = p.segmentation.update_data;
    std->flags.UsesLr = uses_lr;
    std->flags.usesChromaLr = uses_chroma_lr;
    std->flags.apply_grain = grain;
    std->frame_type = (StdVideoAV1FrameType)p.format.frame_type;
    std->OrderHint = self->OrderHint;
    std->primary_ref_frame = p.primary_ref_frame;
    /* only whether it is zero matters to vulkan, and every picture is kept in case a frame later refers to it. */
    std->refresh_frame_flags = 0xFF;
    std->interpolation_filter = (StdVideoAV1InterpolationFilter)p.interp_filter;
    std->TxMode = (StdVideoAV1TxMode)p.coding.tx_mode;
    std->delta_q_res = p.quantization.delta_q_res;
    std->delta_lf_res = p.loop_filter.delta_lf_res;
    std->coded_denom = super && p.superres_denom >= 9 ? (uint8_t)(p.superres_denom - 9) : 0;

    /* skip mode: the two references closest in order, as the frame header derives them. */
    if (p.coding.skip_mode && !intra) {
        int forward = -1, backward = -1, second = -1, forward_hint = 0, backward_hint = 0, second_hint = 0;
        for (int i = 0; i < 7; i++) {
            int hint = (int)hints[1 + i];
            if (relative_dist(p.order_hint_bits, hint, self->OrderHint) < 0) {
                if (forward < 0 || relative_dist(p.order_hint_bits, hint, forward_hint) > 0) {
                    forward = i;
                    forward_hint = hint;
                }
            } else if (relative_dist(p.order_hint_bits, hint, self->OrderHint) > 0) {
                if (backward < 0 || relative_dist(p.order_hint_bits, hint, backward_hint) < 0) {
                    backward = i;
                    backward_hint = hint;
                }
            }
        }
        if (forward >= 0 && backward < 0) {
            for (int i = 0; i < 7; i++) {
                int hint = (int)hints[1 + i];
                if (relative_dist(p.order_hint_bits, hint, forward_hint) < 0 &&
                    (second < 0 || relative_dist(p.order_hint_bits, hint, second_hint) > 0)) {
                    second = i;
                    second_hint = hint;
                }
            }
        }
        int other = backward >= 0 ? backward : second;
        if (forward >= 0 && other >= 0) {
            std->SkipModeFrame[0] = (uint8_t)(1 + (forward < other ? forward : other));
            std->SkipModeFrame[1] = (uint8_t)(1 + (forward < other ? other : forward));
        }
    }
    std->pTileInfo = tiles;
    std->pQuantization = quant;
    std->pSegmentation = seg;
    std->pLoopFilter = lf;
    std->pCDEF = cdef;
    std->pLoopRestoration = lr;
    std->pGlobalMotion = gm;
    std->pFilmGrain = fg;

    /* the tile data is all the bitstream holds, no frame header obu in front of it. */
    pic->pStdPictureInfo = std;
    pic->frameHeaderOffset = 0;
    pic->tileCount = tile_count;
    pic->pTileOffsets = f->slice_offsets;
    pic->pTileSizes = sizes;
    f->info = (VkVideoDecodeInfoKHR){VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR, pic};
    video_picture_resource(cur, &f->info.dstPictureResource);
    f->info.pSetupReferenceSlot = &f->setup;
    f->info.referenceSlotCount = f->ref_count;
    f->info.pReferenceSlots = f->refs;
    return true;
}
