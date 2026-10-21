/*
 * the picture parameters, quantization matrices and slice control of directx video acceleration (dxva), as
 * programs fill them for a hardware decoder and the host reads them: byte layouts of the specifications, shared
 * by the host's decoder and the programs that test it. plain c, little endian, the bit fields of a group kept
 * in one integer of the width the specification gives them, so any compiler lays them out alike.
 */
#ifndef MIME_DXVA_H
#define MIME_DXVA_H

#include <stddef.h>
#include <stdint.h>

/* a reference to a surface of the decoder: an index the program picks, 127 when none. */
typedef union DxvaPicEntry {
    struct {
        uint8_t index : 7;
        uint8_t associated : 1; /* h.264: the entry is a long term reference or, in CurrPic, the bottom field */
    };
    uint8_t byte;
} DxvaPicEntry;

#define DXVA_INVALID_ENTRY 0xFF

/* ------------------------------------------------------------------ h.264 */

typedef struct DxvaPicParamsH264 {
    uint16_t width_in_mbs_minus1;
    uint16_t height_in_mbs_minus1;
    DxvaPicEntry curr_pic;
    uint8_t num_ref_frames;
    union {
        struct {
            uint16_t field_pic_flag : 1;
            uint16_t mbaff_frame_flag : 1;
            uint16_t residual_colour_transform_flag : 1;
            uint16_t sp_for_switch_flag : 1;
            uint16_t chroma_format_idc : 2;
            uint16_t ref_pic_flag : 1;
            uint16_t constrained_intra_pred_flag : 1;
            uint16_t weighted_pred_flag : 1;
            uint16_t weighted_bipred_idc : 2;
            uint16_t mbs_consecutive_flag : 1;
            uint16_t frame_mbs_only_flag : 1;
            uint16_t transform_8x8_mode_flag : 1;
            uint16_t min_luma_bipred_size_8x8_flag : 1;
            uint16_t intra_pic_flag : 1;
        };
        uint16_t bit_fields;
    };
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint16_t reserved16;
    uint32_t status_report_feedback_number;
    DxvaPicEntry ref_frame_list[16];
    int32_t curr_field_order_cnt[2];
    int32_t field_order_cnt_list[16][2];
    int8_t pic_init_qs_minus26;
    int8_t chroma_qp_index_offset;
    int8_t second_chroma_qp_index_offset;
    uint8_t continuation_flag;
    int8_t pic_init_qp_minus26;
    uint8_t num_ref_idx_l0_active_minus1;
    uint8_t num_ref_idx_l1_active_minus1;
    uint8_t reserved8a;
    uint16_t frame_num_list[16];
    uint32_t used_for_reference_flags; /* two bits per entry of ref_frame_list: top field, bottom field */
    uint16_t non_existing_frame_flags;
    uint16_t frame_num;
    uint8_t log2_max_frame_num_minus4;
    uint8_t pic_order_cnt_type;
    uint8_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t delta_pic_order_always_zero_flag;
    uint8_t direct_8x8_inference_flag;
    uint8_t entropy_coding_mode_flag;
    uint8_t pic_order_present_flag;
    uint8_t num_slice_groups_minus1;
    uint8_t slice_group_map_type;
    uint8_t deblocking_filter_control_present_flag;
    uint8_t redundant_pic_cnt_present_flag;
    uint8_t reserved8b;
    uint16_t slice_group_change_rate_minus1;
    uint8_t slice_group_map[810];
} DxvaPicParamsH264;

_Static_assert(sizeof(DxvaPicParamsH264) == 1040, "DXVA_PicParams_H264 is 1040 bytes");

/* scaling lists in the order of the bitstream, already resolved: the lists of the sps or pps in use. */
typedef struct DxvaQmatrixH264 {
    uint8_t scaling_lists_4x4[6][16];
    uint8_t scaling_lists_8x8[2][64];
} DxvaQmatrixH264;

_Static_assert(sizeof(DxvaQmatrixH264) == 224, "DXVA_Qmatrix_H264 is 224 bytes");

/*
 * the short slice format (ConfigBitstreamRaw 2): where a slice, its start code included, is in the bitstream
 * buffer, and how much of it this buffer holds. bad_slice_chopping: bit 0, the slice goes on in the next buffer,
 * bit 1, it began in the previous one.
 */
typedef struct DxvaSliceShort {
    uint32_t bitstream_location;
    uint32_t bytes_in_buffer;
    uint16_t bad_slice_chopping;
} __attribute__((packed)) DxvaSliceShort;

_Static_assert(sizeof(DxvaSliceShort) == 10, "DXVA_Slice_Short is 10 bytes, dxva.h packs its structures by one");

/* ------------------------------------------------------------------ vp9 */

typedef struct DxvaSegmentationVP9 {
    union {
        struct {
            uint8_t enabled : 1;
            uint8_t update_map : 1;
            uint8_t temporal_update : 1;
            uint8_t abs_delta : 1;
            uint8_t reserved4 : 4;
        };
        uint8_t segment_info_flags;
    };
    uint8_t tree_probs[7];
    uint8_t pred_probs[3];
    int16_t feature_data[8][4];
    uint8_t feature_mask[8];
} __attribute__((packed)) DxvaSegmentationVP9;

typedef struct DxvaPicParamsVP9 {
    DxvaPicEntry curr_pic;
    uint8_t profile;
    union {
        struct {
            uint16_t frame_type : 1; /* 0 for a key frame */
            uint16_t show_frame : 1;
            uint16_t error_resilient_mode : 1;
            uint16_t subsampling_x : 1;
            uint16_t subsampling_y : 1;
            uint16_t extra_plane : 1;
            uint16_t refresh_frame_context : 1;
            uint16_t frame_parallel_decoding_mode : 1;
            uint16_t intra_only : 1;
            uint16_t frame_context_idx : 2;
            uint16_t reset_frame_context : 2;
            uint16_t allow_high_precision_mv : 1;
            uint16_t reserved2 : 2;
        };
        uint16_t format_and_picture_info_flags;
    };
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth_minus8_luma;
    uint8_t bit_depth_minus8_chroma;
    uint8_t interp_filter;
    uint8_t reserved8;
    DxvaPicEntry ref_frame_map[8];
    uint32_t ref_frame_coded_width[8];
    uint32_t ref_frame_coded_height[8];
    DxvaPicEntry frame_refs[3];
    int8_t ref_frame_sign_bias[4];
    int8_t filter_level;
    int8_t sharpness_level;
    union {
        struct {
            uint8_t mode_ref_delta_enabled : 1;
            uint8_t mode_ref_delta_update : 1;
            uint8_t use_prev_in_find_mv_refs : 1;
            uint8_t reserved5 : 5;
        };
        uint8_t control_info_flags;
    };
    int8_t ref_deltas[4];
    int8_t mode_deltas[2];
    int16_t base_qindex;
    int8_t y_dc_delta_q;
    int8_t uv_dc_delta_q;
    int8_t uv_ac_delta_q;
    DxvaSegmentationVP9 segments;
    uint8_t log2_tile_cols;
    uint8_t log2_tile_rows;
    uint16_t uncompressed_header_size_byte_aligned;
    uint16_t first_partition_size;
    uint16_t reserved16;
    uint32_t reserved32;
    uint32_t status_report_feedback_number;
} __attribute__((packed)) DxvaPicParamsVP9;

_Static_assert(sizeof(DxvaPicParamsVP9) == 208, "DXVA_PicParams_VP9 is 208 bytes, packed by one as dxva.h is");

/* ------------------------------------------------------------------ av1 */

/*
 * the layouts follow the specification of dxva for av1, its picture entry, picture parameters and tile control,
 * sections 3 to 5, with the sizes checked against the header of the same names in the mingw sdk. the values
 * are the syntax elements of the av1 specification, resolved by the application: sizes of the frame, global
 * motion, loop filter deltas, segmentation and film grain are the ones the frame uses, not the coded deltas. the sdk
 * packs the header by one, so do these.
 */

/* a reference of the frame: where it is in the reference map, its size and the global motion toward it. */
typedef struct DxvaPicEntryAV1 {
    uint32_t width;
    uint32_t height;
    int32_t wmmat[6];
    union {
        struct {
            uint8_t wminvalid : 1; /* an application sets it for the identity motion */
            uint8_t wmtype : 2;    /* 0 identity, 1 translation, 2 rotation and zoom, 3 affine */
            uint8_t reserved5 : 5;
        };
        uint8_t global_motion_flags;
    };
    uint8_t index; /* the slot of RefFrameMapTextureIndex, 255 when none */
    uint16_t reserved16;
} __attribute__((packed)) DxvaPicEntryAV1;

_Static_assert(sizeof(DxvaPicEntryAV1) == 36, "DXVA_PicEntry_AV1 is 36 bytes");

typedef struct DxvaTilesAV1 {
    uint8_t cols;
    uint8_t rows;
    uint16_t context_update_id;
    uint16_t widths[64];  /* in superblocks, the first cols are valid */
    uint16_t heights[64]; /* the first rows are valid */
} __attribute__((packed)) DxvaTilesAV1;

typedef struct DxvaLoopFilterAV1 {
    uint8_t filter_level[2];
    uint8_t filter_level_u;
    uint8_t filter_level_v;
    uint8_t sharpness_level;
    union {
        struct {
            uint8_t mode_ref_delta_enabled : 1;
            uint8_t mode_ref_delta_update : 1;
            uint8_t delta_lf_multi : 1;
            uint8_t delta_lf_present : 1;
            uint8_t reserved4 : 4;
        };
        uint8_t control_flags;
    };
    int8_t ref_deltas[8];
    int8_t mode_deltas[2];
    uint8_t delta_lf_res;
    uint8_t frame_restoration_type[3]; /* 0 none, 1 wiener, 2 sgrproj, 3 switchable */
    uint16_t log2_restoration_unit_size[3];
    uint16_t reserved16;
} __attribute__((packed)) DxvaLoopFilterAV1;

typedef struct DxvaQuantizationAV1 {
    union {
        struct {
            uint8_t delta_q_present : 1;
            uint8_t delta_q_res : 2;
            uint8_t reserved5 : 5;
        };
        uint8_t control_flags;
    };
    uint8_t base_qindex;
    int8_t y_dc_delta_q;
    int8_t u_dc_delta_q;
    int8_t v_dc_delta_q;
    int8_t u_ac_delta_q;
    int8_t v_ac_delta_q;
    uint8_t qm_y; /* 255 for all three when the frame has no quantizer matrix */
    uint8_t qm_u;
    uint8_t qm_v;
    uint16_t reserved16;
} __attribute__((packed)) DxvaQuantizationAV1;

typedef union DxvaCdefStrengthAV1 {
    struct {
        uint8_t primary : 6;
        uint8_t secondary : 2; /* as coded, 0 to 3 */
    };
    uint8_t combined;
} DxvaCdefStrengthAV1;

typedef struct DxvaCdefAV1 {
    union {
        struct {
            uint8_t damping : 2; /* cdef_damping_minus_3 */
            uint8_t bits : 2;
            uint8_t reserved4 : 4;
        };
        uint8_t control_flags;
    };
    DxvaCdefStrengthAV1 y_strengths[8];
    DxvaCdefStrengthAV1 uv_strengths[8];
} __attribute__((packed)) DxvaCdefAV1;

typedef struct DxvaSegmentationAV1 {
    union {
        struct {
            uint8_t enabled : 1;
            uint8_t update_map : 1;
            uint8_t update_data : 1;
            uint8_t temporal_update : 1;
            uint8_t reserved4 : 4;
        };
        uint8_t control_flags;
    };
    uint8_t reserved24[3];
    uint8_t feature_mask[8]; /* bit j of entry i is FeatureEnabled[i][j] */
    int16_t feature_data[8][8];
} __attribute__((packed)) DxvaSegmentationAV1;

typedef struct DxvaFilmGrainAV1 {
    union {
        struct {
            uint16_t apply_grain : 1;
            uint16_t scaling_shift_minus8 : 2;
            uint16_t chroma_scaling_from_luma : 1;
            uint16_t ar_coeff_lag : 2;
            uint16_t ar_coeff_shift_minus6 : 2;
            uint16_t grain_scale_shift : 2;
            uint16_t overlap_flag : 1;
            uint16_t clip_to_restricted_range : 1;
            uint16_t matrix_coeff_is_identity : 1;
            uint16_t reserved3 : 3;
        };
        uint16_t control_flags;
    };
    uint16_t grain_seed;
    uint8_t scaling_points_y[14][2]; /* value, scaling */
    uint8_t num_y_points;
    uint8_t scaling_points_cb[10][2];
    uint8_t num_cb_points;
    uint8_t scaling_points_cr[10][2];
    uint8_t num_cr_points;
    uint8_t ar_coeffs_y[24]; /* the coded values, the coefficient plus 128 */
    uint8_t ar_coeffs_cb[25];
    uint8_t ar_coeffs_cr[25];
    uint8_t cb_mult;
    uint8_t cb_luma_mult;
    uint8_t cr_mult;
    uint8_t cr_luma_mult;
    uint8_t reserved8;
    int16_t cb_offset;
    int16_t cr_offset;
} __attribute__((packed)) DxvaFilmGrainAV1;

typedef struct DxvaPicParamsAV1 {
    uint32_t width; /* the upscaled width when the frame uses superres */
    uint32_t height;
    uint32_t max_width; /* of the sequence */
    uint32_t max_height;
    uint8_t curr_pic_texture_index;
    uint8_t superres_denom; /* 8 without superres */
    uint8_t bitdepth;
    uint8_t seq_profile;
    DxvaTilesAV1 tiles;
    union {
        struct {
            uint32_t use_128x128_superblock : 1;
            uint32_t intra_edge_filter : 1;
            uint32_t interintra_compound : 1;
            uint32_t masked_compound : 1;
            uint32_t warped_motion : 1; /* allow_warped_motion of the frame */
            uint32_t dual_filter : 1;
            uint32_t jnt_comp : 1;
            uint32_t screen_content_tools : 1; /* allow_screen_content_tools of the frame */
            uint32_t integer_mv : 1;           /* force_integer_mv */
            uint32_t cdef : 1;                 /* enable_cdef of the sequence */
            uint32_t restoration : 1;          /* enable_restoration of the sequence */
            uint32_t film_grain : 1;           /* film_grain_params_present */
            uint32_t intrabc : 1;
            uint32_t high_precision_mv : 1;
            uint32_t switchable_motion_mode : 1; /* is_motion_mode_switchable */
            uint32_t filter_intra : 1;
            uint32_t disable_frame_end_update_cdf : 1;
            uint32_t disable_cdf_update : 1;
            uint32_t reference_mode : 1; /* reference_select */
            uint32_t skip_mode : 1;      /* skip_mode_present */
            uint32_t reduced_tx_set : 1;
            uint32_t superres : 1;
            uint32_t tx_mode : 2; /* 0 only 4x4, 1 largest, 2 select */
            uint32_t use_ref_frame_mvs : 1;
            uint32_t enable_ref_frame_mvs : 1;
            uint32_t reference_frame_update : 1;
            uint32_t reserved5 : 5;
        };
        uint32_t flags;
    } coding;
    union {
        struct {
            uint8_t frame_type : 2; /* key, inter, intra only, switch */
            uint8_t show_frame : 1;
            uint8_t showable_frame : 1;
            uint8_t subsampling_x : 1;
            uint8_t subsampling_y : 1;
            uint8_t mono_chrome : 1;
            uint8_t reserved1 : 1;
        };
        uint8_t flags;
    } format;
    uint8_t primary_ref_frame; /* 7 when none */
    uint8_t order_hint;
    uint8_t order_hint_bits; /* 0 without order hints */
    DxvaPicEntryAV1 frame_refs[7];
    uint8_t ref_frame_map_texture_index[8]; /* 255 for an empty slot */
    DxvaLoopFilterAV1 loop_filter;
    DxvaQuantizationAV1 quantization;
    DxvaCdefAV1 cdef;
    uint8_t interp_filter; /* 0 to 3 as coded, 4 switchable */
    DxvaSegmentationAV1 segmentation;
    DxvaFilmGrainAV1 film_grain;
    uint32_t reserved32;
    uint32_t status_report_feedback_number;
} __attribute__((packed)) DxvaPicParamsAV1;

_Static_assert(sizeof(DxvaTilesAV1) == 260, "tiles of DXVA_PicParams_AV1");
_Static_assert(sizeof(DxvaLoopFilterAV1) == 28, "loop filter of DXVA_PicParams_AV1");
_Static_assert(sizeof(DxvaQuantizationAV1) == 12, "quantization of DXVA_PicParams_AV1");
_Static_assert(sizeof(DxvaCdefAV1) == 17, "cdef of DXVA_PicParams_AV1");
_Static_assert(sizeof(DxvaSegmentationAV1) == 140, "segmentation of DXVA_PicParams_AV1");
_Static_assert(sizeof(DxvaFilmGrainAV1) == 158, "film grain of DXVA_PicParams_AV1");
_Static_assert(offsetof(DxvaPicParamsAV1, tiles) == 20 && offsetof(DxvaPicParamsAV1, coding) == 280 &&
                   offsetof(DxvaPicParamsAV1, format) == 284 && offsetof(DxvaPicParamsAV1, frame_refs) == 288 &&
                   offsetof(DxvaPicParamsAV1, ref_frame_map_texture_index) == 540 &&
                   offsetof(DxvaPicParamsAV1, loop_filter) == 548 && offsetof(DxvaPicParamsAV1, quantization) == 576 &&
                   offsetof(DxvaPicParamsAV1, cdef) == 588 && offsetof(DxvaPicParamsAV1, interp_filter) == 605 &&
                   offsetof(DxvaPicParamsAV1, segmentation) == 606 && offsetof(DxvaPicParamsAV1, film_grain) == 746 &&
                   offsetof(DxvaPicParamsAV1, status_report_feedback_number) == 908,
               "layout of DXVA_PicParams_AV1");
_Static_assert(sizeof(DxvaPicParamsAV1) == 912, "DXVA_PicParams_AV1 is 912 bytes");

/*
 * one tile, or one list of tiles, of the tile control buffer: where its data is in the bitstream buffer, which
 * is the data after tile_size_minus_1, where init_symbol starts.
 */
typedef struct DxvaTileAV1 {
    uint32_t data_offset;
    uint32_t data_size;
    uint16_t row;
    uint16_t column;
    uint16_t reserved16;
    uint8_t anchor_frame; /* 255 outside a tile list obu */
    uint8_t reserved8;
} __attribute__((packed)) DxvaTileAV1;

_Static_assert(sizeof(DxvaTileAV1) == 16, "DXVA_Tile_AV1 is 16 bytes");

/* ------------------------------------------------------------------ h.265 */

/*
 * the picture parameters of the specification of dxva for h.265 (main and main 10), 232 bytes packed by one. They are the
 * syntax elements of the sequence and picture parameter sets in use, and the reference picture set already resolved:
 * the lists of the reference pictures of the dpb, and for the picture the indices into that list of those it
 * predicts from. the slice control is the short form, DxvaSliceShort, one entry for each slice segment.
 */
typedef struct DxvaPicParamsHEVC {
    uint16_t pic_width_in_min_cbs;
    uint16_t pic_height_in_min_cbs;
    union {
        struct {
            uint16_t chroma_format_idc : 2;
            uint16_t separate_colour_plane_flag : 1;
            uint16_t bit_depth_luma_minus8 : 3;
            uint16_t bit_depth_chroma_minus8 : 3;
            uint16_t log2_max_pic_order_cnt_lsb_minus4 : 4;
            uint16_t no_pic_reordering_flag : 1;
            uint16_t no_bi_pred_flag : 1;
            uint16_t reserved1 : 1;
        };
        uint16_t format_and_sequence_info_flags;
    };
    DxvaPicEntry curr_pic;
    uint8_t sps_max_dec_pic_buffering_minus1;
    uint8_t log2_min_luma_coding_block_size_minus3;
    uint8_t log2_diff_max_min_luma_coding_block_size;
    uint8_t log2_min_transform_block_size_minus2;
    uint8_t log2_diff_max_min_transform_block_size;
    uint8_t max_transform_hierarchy_depth_inter;
    uint8_t max_transform_hierarchy_depth_intra;
    uint8_t num_short_term_ref_pic_sets;
    uint8_t num_long_term_ref_pics_sps;
    uint8_t num_ref_idx_l0_default_active_minus1;
    uint8_t num_ref_idx_l1_default_active_minus1;
    int8_t init_qp_minus26;
    /* NumDeltaPocs of the set the slice header's own set predicts from, 0 when it does not predict. */
    uint8_t num_delta_pocs_of_ref_rps_idx;
    /* the bits the slice header's own short term set takes, 0 when it names one of the sequence's. */
    uint16_t num_bits_for_short_term_rps_in_slice;
    uint16_t reserved2;
    union {
        struct {
            uint32_t scaling_list_enabled_flag : 1;
            uint32_t amp_enabled_flag : 1;
            uint32_t sample_adaptive_offset_enabled_flag : 1;
            uint32_t pcm_enabled_flag : 1;
            uint32_t pcm_sample_bit_depth_luma_minus1 : 4;
            uint32_t pcm_sample_bit_depth_chroma_minus1 : 4;
            uint32_t log2_min_pcm_luma_coding_block_size_minus3 : 2;
            uint32_t log2_diff_max_min_pcm_luma_coding_block_size : 2;
            uint32_t pcm_loop_filter_disabled_flag : 1;
            uint32_t long_term_ref_pics_present_flag : 1;
            uint32_t sps_temporal_mvp_enabled_flag : 1;
            uint32_t strong_intra_smoothing_enabled_flag : 1;
            uint32_t dependent_slice_segments_enabled_flag : 1;
            uint32_t output_flag_present_flag : 1;
            uint32_t num_extra_slice_header_bits : 3;
            uint32_t sign_data_hiding_enabled_flag : 1;
            uint32_t cabac_init_present_flag : 1;
            uint32_t reserved3 : 5;
        };
        uint32_t coding_param_tool_flags;
    };
    union {
        struct {
            uint32_t constrained_intra_pred_flag : 1;
            uint32_t transform_skip_enabled_flag : 1;
            uint32_t cu_qp_delta_enabled_flag : 1;
            uint32_t pps_slice_chroma_qp_offsets_present_flag : 1;
            uint32_t weighted_pred_flag : 1;
            uint32_t weighted_bipred_flag : 1;
            uint32_t transquant_bypass_enabled_flag : 1;
            uint32_t tiles_enabled_flag : 1;
            uint32_t entropy_coding_sync_enabled_flag : 1;
            uint32_t uniform_spacing_flag : 1;
            uint32_t loop_filter_across_tiles_enabled_flag : 1;
            uint32_t pps_loop_filter_across_slices_enabled_flag : 1;
            uint32_t deblocking_filter_override_enabled_flag : 1;
            uint32_t pps_deblocking_filter_disabled_flag : 1;
            uint32_t lists_modification_present_flag : 1;
            uint32_t slice_segment_header_extension_present_flag : 1;
            uint32_t irap_pic_flag : 1;
            uint32_t idr_pic_flag : 1;
            uint32_t intra_pic_flag : 1;
            uint32_t reserved4 : 13;
        };
        uint32_t coding_setting_picture_property_flags;
    };
    int8_t pps_cb_qp_offset;
    int8_t pps_cr_qp_offset;
    uint8_t num_tile_columns_minus1;
    uint8_t num_tile_rows_minus1;
    uint16_t column_width_minus1[19];
    uint16_t row_height_minus1[21];
    uint8_t diff_cu_qp_delta_depth;
    int8_t pps_beta_offset_div2;
    int8_t pps_tc_offset_div2;
    uint8_t log2_parallel_merge_level_minus2;
    int32_t curr_pic_order_cnt_val;
    /* the reference pictures of the dpb: their surface indices, the long term ones marked. */
    DxvaPicEntry ref_pic_list[15];
    uint8_t reserved5;
    int32_t pic_order_cnt_val_list[15];
    /* the pictures the picture predicts from, as indices into ref_pic_list, 0xFF for none. */
    uint8_t ref_pic_set_st_curr_before[8];
    uint8_t ref_pic_set_st_curr_after[8];
    uint8_t ref_pic_set_lt_curr[8];
    uint16_t reserved6;
    uint16_t reserved7;
    uint32_t status_report_feedback_number;
} __attribute__((packed)) DxvaPicParamsHEVC;

_Static_assert(sizeof(DxvaPicParamsHEVC) == 232, "DXVA_PicParams_HEVC is 232 bytes");
_Static_assert(offsetof(DxvaPicParamsHEVC, curr_pic) == 6 && offsetof(DxvaPicParamsHEVC, num_delta_pocs_of_ref_rps_idx) == 19 &&
                   offsetof(DxvaPicParamsHEVC, coding_param_tool_flags) == 24 &&
                   offsetof(DxvaPicParamsHEVC, coding_setting_picture_property_flags) == 28 &&
                   offsetof(DxvaPicParamsHEVC, pps_cb_qp_offset) == 32 &&
                   offsetof(DxvaPicParamsHEVC, column_width_minus1) == 36 &&
                   offsetof(DxvaPicParamsHEVC, row_height_minus1) == 74 &&
                   offsetof(DxvaPicParamsHEVC, diff_cu_qp_delta_depth) == 116 &&
                   offsetof(DxvaPicParamsHEVC, curr_pic_order_cnt_val) == 120 &&
                   offsetof(DxvaPicParamsHEVC, ref_pic_list) == 124 &&
                   offsetof(DxvaPicParamsHEVC, pic_order_cnt_val_list) == 140 &&
                   offsetof(DxvaPicParamsHEVC, ref_pic_set_st_curr_before) == 200 &&
                   offsetof(DxvaPicParamsHEVC, status_report_feedback_number) == 228,
               "layout of DXVA_PicParams_HEVC");

/* the scaling lists in use, in the order of the bitstream: sizeId 0 to 3 and the dc values of 16x16 and 32x32. */
typedef struct DxvaQmatrixHEVC {
    uint8_t lists_4x4[6][16];
    uint8_t lists_8x8[6][64];
    uint8_t lists_16x16[6][64];
    uint8_t lists_32x32[2][64];
    uint8_t dc_16x16[6];
    uint8_t dc_32x32[2];
} DxvaQmatrixHEVC;

_Static_assert(sizeof(DxvaQmatrixHEVC) == 1000, "DXVA_Qmatrix_HEVC is 1000 bytes");


#endif
