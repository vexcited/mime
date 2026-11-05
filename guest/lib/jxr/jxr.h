#ifndef MIME_LIB_JXR_JXR_H
#define MIME_LIB_JXR_JXR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_JXR_OK = 0,
    MIME_JXR_ERR_FORMAT = -1,      /* not a codestream, or inconsistent headers. */
    MIME_JXR_ERR_UNSUPPORTED = -2, /* a valid feature or pixel layout that is not handled. */
    MIME_JXR_ERR_DATA = -3,        /* the coded data ends early or is damaged. */
    MIME_JXR_ERR_NOMEM = -4,
    MIME_JXR_ERR_PARAM = -5,
};

/* pixel layouts, the last byte of the pixel format guids {6fddc324-4e03-4bfe-b185-3d77768dc9xx}. */
enum {
    MIME_JXR_BLACKWHITE = 0x05,
    MIME_JXR_GRAY8 = 0x08,
    MIME_JXR_BGR555 = 0x09,
    MIME_JXR_BGR565 = 0x0a,
    MIME_JXR_GRAY16 = 0x0b,
    MIME_JXR_BGR24 = 0x0c,
    MIME_JXR_RGB24 = 0x0d,
    MIME_JXR_BGR32 = 0x0e,
    MIME_JXR_BGRA32 = 0x0f,
    MIME_JXR_PBGRA32 = 0x10,
    MIME_JXR_GRAY32F = 0x11,
    MIME_JXR_RGB48FIXED = 0x12,
    MIME_JXR_GRAY16FIXED = 0x13,
    MIME_JXR_BGR101010 = 0x14,
    MIME_JXR_RGB48 = 0x15,
    MIME_JXR_RGBA64 = 0x16,
    MIME_JXR_PRGBA64 = 0x17,
    MIME_JXR_RGB96FIXED = 0x18,
    MIME_JXR_RGBA128F = 0x19,
    MIME_JXR_PRGBA128F = 0x1a,
    MIME_JXR_RGB128F = 0x1b,
    MIME_JXR_CMYK32 = 0x1c,
    MIME_JXR_RGBA64FIXED = 0x1d,
    MIME_JXR_RGBA128FIXED = 0x1e,
    MIME_JXR_CMYK64 = 0x1f,
    MIME_JXR_3CH8 = 0x20, /* to 0x25 for 8 channels. */
    MIME_JXR_3CH16 = 0x26, /* to 0x2b. */
    MIME_JXR_CMYKA40 = 0x2c,
    MIME_JXR_CMYKA80 = 0x2d,
    MIME_JXR_3CHA8 = 0x2e, /* to 0x33. */
    MIME_JXR_3CHA16 = 0x34, /* to 0x39. */
    MIME_JXR_RGBA64HALF = 0x3a,
    MIME_JXR_RGB48HALF = 0x3b,
    MIME_JXR_RGBE32 = 0x3d,
    MIME_JXR_GRAY16HALF = 0x3e,
    MIME_JXR_GRAY32FIXED = 0x3f,
    MIME_JXR_RGB64FIXED = 0x40,
    MIME_JXR_RGB128FIXED = 0x41,
    MIME_JXR_RGB64HALF = 0x42,
};

/* MimeJxrHeader.output_format and internal_format, the codestream colour formats. */
enum {
    MIME_JXR_CF_YONLY = 0,
    MIME_JXR_CF_YUV420 = 1,
    MIME_JXR_CF_YUV422 = 2,
    MIME_JXR_CF_YUV444 = 3,
    MIME_JXR_CF_CMYK = 4,
    MIME_JXR_CF_YUVK = 4,
    MIME_JXR_CF_CMYKDIRECT = 5,
    MIME_JXR_CF_NCOMPONENT = 6,
    MIME_JXR_CF_RGB = 7,
    MIME_JXR_CF_RGBE = 8,
};

/* MimeJxrHeader.bit_depth. */
enum {
    MIME_JXR_BD1WHITE1 = 0,
    MIME_JXR_BD8 = 1,
    MIME_JXR_BD16 = 2,
    MIME_JXR_BD16S = 3,
    MIME_JXR_BD16F = 4,
    MIME_JXR_BD32S = 6,
    MIME_JXR_BD32F = 7,
    MIME_JXR_BD5 = 8,
    MIME_JXR_BD10 = 9,
    MIME_JXR_BD565 = 10,
    MIME_JXR_BD1BLACK1 = 15,
};

typedef struct MimeJxrHeader {
    uint32_t width, height;
    uint8_t output_format;
    uint8_t bit_depth;
    uint8_t internal_format; /* of the primary plane. */
    uint8_t components;      /* of the primary plane. */
    uint8_t overlap;
    uint8_t bands;           /* 0 all, 1 no flexbits, 2 no highpass, 3 dc only. */
    uint8_t spatial_xfrm;
    bool alpha;              /* an interleaved alpha plane. */
    bool premultiplied;
    bool frequency_mode;
    bool hard_tiling;
    uint16_t tile_cols, tile_rows;
} MimeJxrHeader;

int mime_jxr_header(const uint8_t *data, size_t size, const MimeAllocator *alloc, MimeJxrHeader *header);

/* layout properties: bits per pixel, color channels without alpha, and whether there is an alpha channel. */
bool mime_jxr_layout(uint8_t layout, unsigned *bits, unsigned *channels, bool *alpha);

/*
 * decodes into width by height pixels of layout, rows of stride bytes; the codestream must have that size. alpha,
 * when not NULL, is a separate alpha codestream for a layout with an alpha channel. a layout with alpha and no alpha
 * plane gets opaque alpha.
 */
int mime_jxr_decode(const uint8_t *data, size_t size, const uint8_t *alpha, size_t alpha_size, uint8_t layout,
                    const MimeAllocator *alloc, uint32_t width, uint32_t height, void *out, size_t stride);

typedef struct MimeJxrEncodeParams {
    uint32_t width, height;
    uint8_t layout;
    const void *pixels;
    size_t stride;
    uint8_t quality;       /* the quantizer index, 1 is lossless, up to 255. */
    uint8_t alpha_quality;
    uint8_t overlap;       /* 0, 1 or 2. */
    uint8_t subsampling;   /* 0 luma only, 1 4:2:0, 2 4:2:2, 3 4:4:4, for rgb layouts. */
    uint8_t bands;         /* as MimeJxrHeader.bands. */
    uint8_t alpha_bands;
    uint8_t spatial_xfrm;
    bool frequency_mode;
    bool hard_tiling;
    bool planar_alpha;     /* the alpha plane goes to its own codestream. */
    bool premultiplied;
    uint16_t tile_cols;    /* 0 or 1 for a single tile, which is split when wider than 4096 pixels. */
    uint16_t tile_rows;
} MimeJxrEncodeParams;

/* *out and *alpha_out, which is only set for planar alpha, are freed with mime_free. */
int mime_jxr_encode(const MimeJxrEncodeParams *params, const MimeAllocator *alloc, uint8_t **out, size_t *out_size,
                    uint8_t **alpha_out, size_t *alpha_size);

#endif
