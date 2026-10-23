/*
 * block compressed texture decoding to 32 bpp bgra, and a dds reader and writer. freestanding: needs only memcpy,
 * memmove, memset, memcmp and the allocator. no libm, no 64 bit division.
 *
 * pixels are b, g, r, a bytes in memory, a uint32_t of a << 24 | r << 16 | g << 8 | b. rows are top down.
 */
#ifndef MIME_LIB_BCN_BCN_H
#define MIME_LIB_BCN_BCN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_BCN_OK = 0,
    MIME_BCN_ERR_FORMAT = -1,      /* not a dds file or inconsistent header fields. */
    MIME_BCN_ERR_UNSUPPORTED = -3, /* a pixel format or size that is not handled. */
    MIME_BCN_ERR_DATA = -4,        /* the data is shorter than the header says. */
    MIME_BCN_ERR_NOMEM = -5,
    MIME_BCN_ERR_PARAM = -6,
};

enum {
    MIME_BCN_BC1 = 1, /* DXT1, 4 bits per pixel, optional 1 bit alpha. */
    MIME_BCN_BC2,     /* DXT2 and DXT3, explicit 4 bit alpha. */
    MIME_BCN_BC3,     /* DXT4 and DXT5, interpolated alpha. */
    MIME_BCN_BC4U,    /* one unsigned channel, decoded as gray. */
    MIME_BCN_BC4S,    /* one signed channel, decoded as gray s + 128, and 0 for the -1.0 of the six value mode. */
    MIME_BCN_BC5U,    /* two unsigned channels, decoded as red and green. */
    MIME_BCN_BC5S,    /* two signed channels as BC4S, and blue 128. */
    MIME_BCN_BC6H_UF16,
    MIME_BCN_BC6H_SF16,
    MIME_BCN_BC7,
};

/* decode flags. */
#define MIME_BCN_OPAQUE 0x1u /* BC1: the fourth color of the 3 color mode is opaque black, and alpha is 255. */
/* the stored color is premultiplied like DXT2 and DXT4, output straight alpha. BC2 and BC3 only. */
#define MIME_BCN_UNPREMULTIPLY 0x2u
/* output premultiplied color. BC1 to BC3 and BC7. */
#define MIME_BCN_PREMULTIPLY 0x4u

/* bytes per 4x4 block, 0 for an unknown format. */
unsigned mime_bcn_block_size(unsigned format);

/* 0 on overflow or an unknown format. */
size_t mime_bcn_size(unsigned format, unsigned width, unsigned height);

/* block decoders: src holds block_size bytes, out 16 pixels of the 4x4 block in raster order. */
void mime_bcn_decode_bc1_block(const uint8_t *src, uint32_t out[16], unsigned flags);
void mime_bcn_decode_bc2_block(const uint8_t *src, uint32_t out[16], unsigned flags);
void mime_bcn_decode_bc3_block(const uint8_t *src, uint32_t out[16], unsigned flags);
/*
 * values 0..255. signed gives the snorm value plus 128, reads -128 as -127, and the special -1.0 of the six value
 * mode as 0.
 */
void mime_bcn_decode_bc4_block(const uint8_t *src, uint8_t out[16], bool is_signed);
/* two channels, 16 values each. */
void mime_bcn_decode_bc5_block(const uint8_t *src, uint8_t out_r[16], uint8_t out_g[16], bool is_signed);
/*
 * bc6h: 48 half floats, r, g, b for 16 pixels. a reserved mode gives zeros and false. signed values keep the sign bit,
 * so negatives stay negative.
 */
bool mime_bcn_decode_bc6h_block(const uint8_t *src, bool is_signed, uint16_t out_rgb[48]);
/* bc7: the reserved mode 8 gives all zero, alpha 0 included, and false. */
bool mime_bcn_decode_bc7_block(const uint8_t *src, uint32_t out[16], unsigned flags);

/*
 * dst_stride is at least width * 4. src_size must cover the image, else MIME_BCN_ERR_DATA. bc6h is clamped to 0..1
 * and converted linearly, mime_bcn_decode_bc6h_half keeps the full range.
 */
int mime_bcn_decode(unsigned format, const void *src, size_t src_size, unsigned width, unsigned height, uint8_t *dst,
                    size_t dst_stride, unsigned flags);

/* r, g, b, a half floats per pixel with a at 1.0. dst_stride is at least width * 8. is_signed selects BC6H_SF16. */
int mime_bcn_decode_bc6h_half(const void *src, size_t src_size, unsigned width, unsigned height, bool is_signed,
                              uint16_t *dst, size_t dst_stride);

/* clamped to 0..1, rounded, linear. */
uint8_t mime_bcn_half_to_u8(uint16_t half);

/* encoders take 16 bgra pixels in raster order. with alpha, bc1 maps alpha below 128 to the transparent color. */
void mime_bcn_encode_bc1_block(const uint32_t src[16], uint8_t out[8], bool alpha);
void mime_bcn_encode_bc3_block(const uint32_t src[16], uint8_t out[16]);
/* the 8 byte bc3 alpha part. */
void mime_bcn_encode_alpha_block(const uint8_t alpha[16], uint8_t out[8]);

/*
 * edges are padded by repeating the last pixel. dst holds mime_bcn_size bytes. format is MIME_BCN_BC1 or
 * MIME_BCN_BC3.
 */
int mime_bcn_encode(unsigned format, const uint8_t *bgra, size_t stride, unsigned width, unsigned height, bool alpha,
                    void *dst, size_t dst_size);

/* dds. */

#define MIME_DDS_MAX_DIMENSION 32768u
#define MIME_DDS_MAX_DEPTH 2048u
#define MIME_DDS_MAX_ARRAY 2048u

enum {
    MIME_DDS_FMT_UNKNOWN = 0,
    MIME_DDS_FMT_BC1,
    MIME_DDS_FMT_BC2,
    MIME_DDS_FMT_BC3,
    MIME_DDS_FMT_BC4U,
    MIME_DDS_FMT_BC4S,
    MIME_DDS_FMT_BC5U,
    MIME_DDS_FMT_BC5S,
    MIME_DDS_FMT_BC6H_UF16,
    MIME_DDS_FMT_BC6H_SF16,
    MIME_DDS_FMT_BC7,
    MIME_DDS_FMT_BITFIELD, /* 8 to 32 bits per pixel, described by the masks. */
    MIME_DDS_FMT_RGBA16,   /* 16 bit unorm channels, r, g, b, a in memory order. */
    MIME_DDS_FMT_F16,      /* half float channels. */
    MIME_DDS_FMT_F32,      /* float channels. */
};

typedef struct MimeDdsInfo {
    uint32_t width;
    uint32_t height;
    uint32_t depth;       /* 1 unless a volume. */
    uint32_t mip_count;   /* at least 1, never more than the full chain. */
    uint32_t array_size;  /* a cube map counts as one element. */
    uint32_t face_count;  /* 6 for cube maps, else 1. */
    uint32_t face_mask;   /* DDSCAPS2 face bits >> 10 of the present faces, 0x3f for all. */
    uint32_t dimension;   /* 1, 2 or 3. */
    bool cube;
    bool volume;
    int format;           /* MIME_DDS_FMT_*. */
    uint32_t dxgi_format; /* 0 for a legacy pixel format. */
    uint32_t fourcc;      /* legacy pixel format fourcc, 0 if none. */
    bool premultiplied;   /* DXT2, DXT4 or the DX10 alpha mode. */
    uint32_t alpha_mode;  /* DX10 alpha mode, 0 unknown, 1 straight, 2 premultiplied, 3 opaque, 4 custom. */
    bool srgb;
    bool luminance;       /* the single channel is gray, not red. */
    bool has_alpha;       /* the format carries alpha. for BC formats alpha depends on the data instead. */
    uint32_t bits_per_pixel; /* 0 for block compressed formats. */
    uint32_t block_bytes;    /* 0 when not block compressed. */
    uint32_t channels;       /* for F16, F32 and RGBA16. */
    uint32_t mask[4];        /* r, g, b, a masks of MIME_DDS_FMT_BITFIELD. */
    size_t data_offset;      /* file offset of the first subresource. */
    uint64_t total_size;     /* bytes of all subresources. */
    bool complete;           /* the file holds total_size bytes after data_offset. */
} MimeDdsInfo;

typedef struct MimeDdsSubresource {
    size_t offset; /* file offset of the first slice. */
    uint64_t size; /* of all depth slices. */
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    size_t row_pitch;   /* bytes of a row, or of a row of blocks. */
    size_t slice_pitch; /* bytes of one depth slice. */
} MimeDdsSubresource;

bool mime_dds_is_dds(const void *data, size_t size);

/* reads the headers only. a short file gives complete false. */
int mime_dds_parse(const void *data, size_t size, MimeDdsInfo *info);

/*
 * slice is element * face_count + face, faces ordered +x, -x, +y, -y, +z, -z. a cube map with missing faces only
 * stores and counts the present ones. returns MIME_BCN_ERR_PARAM for out of range indices and MIME_BCN_ERR_DATA when
 * the subresource runs past the file end, filling the fields either way when the indices are valid.
 */
int mime_dds_subresource(const MimeDdsInfo *info, size_t file_size, unsigned slice, unsigned mip,
                         MimeDdsSubresource *out);

/*
 * decodes depth slice z to bgra, dst_stride at least the mip width * 4. block formats follow mime_bcn_decode, BC6H is
 * clamped to 0..1. missing channels are 0 and alpha 255, gray fills red, green and blue. MIME_BCN_UNPREMULTIPLY turns
 * a premultiplied file into straight alpha.
 */
int mime_dds_decode(const MimeDdsInfo *info, const void *file, size_t file_size, unsigned slice, unsigned mip,
                    unsigned z, uint8_t *dst, size_t dst_stride, unsigned flags);

/* the base size shifted down, at least 1. */
uint32_t mime_dds_mip_size(uint32_t base, unsigned mip);

enum {
    MIME_DDS_WRITE_BC1 = 1,
    MIME_DDS_WRITE_BC3,
    MIME_DDS_WRITE_BGRA8, /* B8G8R8A8, with the A8R8G8B8 masks. */
    MIME_DDS_WRITE_RGBA8, /* R8G8B8A8 through a DX10 header. */
};
#define MIME_DDS_WRITE_MIPMAPS 0x1u /* a full chain, made with a box filter. */
#define MIME_DDS_WRITE_ALPHA 0x2u   /* BC1 only: alpha below 128 becomes transparent. */
#define MIME_DDS_WRITE_DX10 0x4u    /* always write the DX10 extension header. */

/* release *out with mime_free. */
int mime_dds_write(const uint8_t *bgra, size_t stride, unsigned width, unsigned height, unsigned format,
                   unsigned flags, const MimeAllocator *alloc, uint8_t **out, size_t *out_size);

#endif
