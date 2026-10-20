/*
 * block compressed texture decoding to 32 bpp bgra, and a dds container
 * reader and writer. freestanding: needs only memcpy, memmove, memset,
 * memcmp and the allocator interface. no libm, no 64 bit division.
 *
 * pixels are b, g, r, a bytes in memory, in a uint32_t that is
 * a << 24 | r << 16 | g << 8 | b. rows are top down.
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
    MIME_BCN_ERR_NOMEM = -5,       /* the allocator failed. */
    MIME_BCN_ERR_PARAM = -6,       /* invalid arguments. */
};

/* block formats. */
enum {
    MIME_BCN_BC1 = 1, /* DXT1, 4 bits per pixel, optional 1 bit alpha. */
    MIME_BCN_BC2,     /* DXT2 and DXT3, explicit 4 bit alpha. */
    MIME_BCN_BC3,     /* DXT4 and DXT5, interpolated alpha. */
    MIME_BCN_BC4U,    /* one unsigned channel, decoded as gray. */
    MIME_BCN_BC4S,    /* one signed channel, decoded as gray, s + 128, and 0 for the -1.0 of the six value mode. */
    MIME_BCN_BC5U,    /* two unsigned channels, decoded as red and green. */
    MIME_BCN_BC5S,    /* two signed channels as BC4S, and blue 128. */
    MIME_BCN_BC6H_UF16,
    MIME_BCN_BC6H_SF16,
    MIME_BCN_BC7,
};

/* decode flags. */
#define MIME_BCN_OPAQUE 0x1u /* BC1: the fourth color of the 3 color mode is opaque black, and alpha is 255. */
/*
 * the stored color is premultiplied by alpha, as in DXT2 and DXT4: convert to
 * straight alpha on output. BC2 and BC3 only.
 */
#define MIME_BCN_UNPREMULTIPLY 0x2u
/* the bgra output holds premultiplied color. BC1 to BC3 and BC7. */
#define MIME_BCN_PREMULTIPLY 0x4u

/* bytes of one 4x4 block of the format, 0 for an unknown one. */
unsigned mime_bcn_block_size(unsigned format);

/* bytes of a width x height image in the format, 0 on overflow or an unknown format. */
size_t mime_bcn_size(unsigned format, unsigned width, unsigned height);

/*
 * block decoders. out has 16 pixels in raster order, 4 per row, 4x4 block.
 * src has block_size bytes.
 */
void mime_bcn_decode_bc1_block(const uint8_t *src, uint32_t out[16], unsigned flags);
void mime_bcn_decode_bc2_block(const uint8_t *src, uint32_t out[16], unsigned flags);
void mime_bcn_decode_bc3_block(const uint8_t *src, uint32_t out[16], unsigned flags);
/*
 * values 0..255. the signed variant gives the snorm value plus 128, with -128 read as -127, and the
 * special -1.0 of the six value mode as 0.
 */
void mime_bcn_decode_bc4_block(const uint8_t *src, uint8_t out[16], bool is_signed);
/* two channels, 16 values each. */
void mime_bcn_decode_bc5_block(const uint8_t *src, uint8_t out_r[16], uint8_t out_g[16], bool is_signed);
/*
 * bc6h: 16 pixels of r, g, b half floats, 48 values. a block with a reserved
 * mode gives zero and false. signed values are half floats with the sign bit
 * set, so negative values stay negative.
 */
bool mime_bcn_decode_bc6h_block(const uint8_t *src, bool is_signed, uint16_t out_rgb[48]);
/* bc7: a block with the reserved mode 8 gives all zero, alpha 0, and false. */
bool mime_bcn_decode_bc7_block(const uint8_t *src, uint32_t out[16], unsigned flags);

/*
 * decodes a width x height image of a block format to bgra, dst_stride bytes
 * per row, at least width * 4. src_size has to cover the image, else
 * MIME_BCN_ERR_DATA. bc6h is clamped to 0..1 and converted linearly, use
 * mime_bcn_decode_bc6h_half for the full range.
 */
int mime_bcn_decode(unsigned format, const void *src, size_t src_size, unsigned width, unsigned height, uint8_t *dst,
                    size_t dst_stride, unsigned flags);

/*
 * bc6h to half floats, r, g, b, a per pixel with a being 1.0, dst_stride bytes
 * per row, at least width * 8. is_signed selects BC6H_SF16.
 */
int mime_bcn_decode_bc6h_half(const void *src, size_t src_size, unsigned width, unsigned height, bool is_signed,
                              uint16_t *dst, size_t dst_stride);

/* half float to 8 bit, clamped to 0..1 and rounded, linear. */
uint8_t mime_bcn_half_to_u8(uint16_t half);

/*
 * encoders, from 4x4 blocks of bgra. src has 16 pixels in raster order.
 * with alpha set bc1 maps alpha below 128 to the transparent color.
 */
void mime_bcn_encode_bc1_block(const uint32_t src[16], uint8_t out[8], bool alpha);
void mime_bcn_encode_bc3_block(const uint32_t src[16], uint8_t out[16]);
/* the alpha part of bc3, 8 bytes, from the alpha bytes of 16 pixels. */
void mime_bcn_encode_alpha_block(const uint8_t alpha[16], uint8_t out[8]);

/*
 * encodes a whole bgra image, edges are padded by repeating the last pixel.
 * dst holds mime_bcn_size bytes. format is MIME_BCN_BC1 or MIME_BCN_BC3.
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
    uint32_t mip_count;   /* at least 1, never more than the chain of the size has. */
    uint32_t array_size;  /* array elements, a cube map counts as one element. */
    uint32_t face_count;  /* 6 for cube maps, else 1. */
    uint32_t face_mask;   /* DDSCAPS2 face bits >> 10 of the faces present, 0x3f for all. */
    uint32_t dimension;   /* 1, 2 or 3. */
    bool cube;
    bool volume;
    int format;           /* MIME_DDS_FMT_*. */
    uint32_t dxgi_format; /* 0 for a legacy pixel format. */
    uint32_t fourcc;      /* of the legacy pixel format, 0 if none. */
    bool premultiplied;   /* DXT2, DXT4 or the DX10 alpha mode. */
    uint32_t alpha_mode;  /* DX10 alpha mode, 0 unknown, 1 straight, 2 premultiplied, 3 opaque, 4 custom. */
    bool srgb;
    bool luminance;       /* the single channel is gray, not red. */
    bool has_alpha;       /* the format carries alpha, apart from the BC formats where it is a property of the data. */
    uint32_t bits_per_pixel; /* 0 for block compressed formats. */
    uint32_t block_bytes;    /* 0 when not block compressed. */
    uint32_t channels;       /* for F16, F32 and RGBA16. */
    uint32_t mask[4];        /* r, g, b, a masks of MIME_DDS_FMT_BITFIELD. */
    size_t data_offset;      /* start of the first subresource in the file. */
    uint64_t total_size;     /* bytes of all subresources. */
    bool complete;           /* the file holds total_size bytes after data_offset. */
} MimeDdsInfo;

typedef struct MimeDdsSubresource {
    size_t offset; /* in the file, of the first slice. */
    uint64_t size; /* of all depth slices. */
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    size_t row_pitch;   /* bytes of a row, or of a row of blocks. */
    size_t slice_pitch; /* bytes of one depth slice. */
} MimeDdsSubresource;

bool mime_dds_is_dds(const void *data, size_t size);

/* reads the headers. the pixel data is not touched, a short file gives complete false. */
int mime_dds_parse(const void *data, size_t size, MimeDdsInfo *info);

/*
 * place of a subresource. slice counts array elements and cube faces, element
 * * face_count + face, with the faces in the order +x, -x, +y, -y, +z, -z. for
 * a cube map with missing faces only the present ones are stored and counted.
 * returns MIME_BCN_ERR_PARAM for out of range indices and MIME_BCN_ERR_DATA when
 * the subresource reaches past the end of the file, the fields are filled in
 * both cases when the indices are valid.
 */
int mime_dds_subresource(const MimeDdsInfo *info, size_t file_size, unsigned slice, unsigned mip,
                         MimeDdsSubresource *out);

/*
 * decodes depth slice z of a subresource to bgra, dst_stride bytes per row, at
 * least width * 4 of the mip. block formats follow mime_bcn_decode. BC6H is
 * clamped to 0..1. channels that a format lacks are 0 and alpha is 255, gray
 * formats fill red, green and blue. flags are MIME_BCN_*, a premultiplied
 * file is converted to straight alpha when MIME_BCN_UNPREMULTIPLY is passed.
 */
int mime_dds_decode(const MimeDdsInfo *info, const void *file, size_t file_size, unsigned slice, unsigned mip,
                    unsigned z, uint8_t *dst, size_t dst_stride, unsigned flags);

/* size of a mip level, the base size shifted down and at least 1. */
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

/* builds a dds file from a bgra image. release *out with mime_free. */
int mime_dds_write(const uint8_t *bgra, size_t stride, unsigned width, unsigned height, unsigned format,
                   unsigned flags, const MimeAllocator *alloc, uint8_t **out, size_t *out_size);

#endif
