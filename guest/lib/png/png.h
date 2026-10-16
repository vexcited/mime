/*
 * png decoder producing 32 bpp bgra pixels, top-down rows.
 * freestanding: needs only memcpy, memmove, memset and the allocator interface.
 */
#ifndef MIME_LIB_PNG_PNG_H
#define MIME_LIB_PNG_PNG_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_PNG_OK = 0,
    MIME_PNG_ERR_FORMAT = -1,      /* bad signature, chunk layout or header fields. */
    MIME_PNG_ERR_CRC = -2,         /* chunk crc mismatch. */
    MIME_PNG_ERR_UNSUPPORTED = -3, /* image too large or unknown critical chunk. */
    MIME_PNG_ERR_DATA = -4,        /* corrupt compressed or filtered image data. */
    MIME_PNG_ERR_NOMEM = -5,       /* the allocator failed. */
    MIME_PNG_ERR_PARAM = -6,       /* invalid arguments. */
};

#define MIME_PNG_PREMULTIPLIED 0x1u /* premultiply color by alpha, otherwise straight alpha. */
#define MIME_PNG_SKIP_CRC 0x2u      /* do not verify chunk crcs. */
#define MIME_PNG_SKIP_ADLER 0x4u    /* do not verify the zlib adler32. */

#define MIME_PNG_MAX_DIMENSION 16384u

enum {
    MIME_PNG_COLOR_GRAY = 0,
    MIME_PNG_COLOR_RGB = 2,
    MIME_PNG_COLOR_PALETTE = 3,
    MIME_PNG_COLOR_GRAY_ALPHA = 4,
    MIME_PNG_COLOR_RGBA = 6,
};

typedef struct MimePngInfo {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    bool interlaced;
    bool has_alpha; /* alpha channel or tRNS transparency. */
} MimePngInfo;

/* true when data starts with the png signature. */
bool mime_png_is_png(const void *data, size_t size);

/* reads the header and the chunks before the image data, without crc checks. */
bool mime_png_info(const void *data, size_t size, MimePngInfo *info);

/*
 * decodes into out, which holds height rows of out_stride bytes, out_stride
 * being at least width * 4. pixels are b, g, r, a bytes. alloc provides
 * temporary memory only.
 */
int mime_png_decode(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t *out,
                    size_t out_stride);

/*
 * same, allocating the output with alloc, rows of width * 4 bytes. release
 * *out with mime_free. info may be NULL.
 */
int mime_png_decode_alloc(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t **out,
                          MimePngInfo *info);

#endif
