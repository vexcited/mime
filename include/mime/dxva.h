/*
 * the picture parameters, quantization matrices and slice control of directx video acceleration (dxva), as
 * programs fill them for a hardware decoder and the host reads them: byte layouts of the specifications, shared
 * by the host's decoder and the programs that test it. plain c, little endian, the bit fields of a group kept
 * in one integer of the width the specification gives them, so any compiler lays them out alike.
 */
#ifndef MIME_DXVA_H
#define MIME_DXVA_H

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
} DxvaSliceShort;

_Static_assert(sizeof(DxvaSliceShort) == 12, "DXVA_Slice_Short is 12 bytes, its two bytes of padding included");

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
} DxvaSegmentationVP9;

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
} DxvaPicParamsVP9;

_Static_assert(sizeof(DxvaPicParamsVP9) == 212, "DXVA_PicParams_VP9 is 212 bytes");

#endif
