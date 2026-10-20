/*
 * jpeg codec written from ITU-T T.81 (ISO 10918-1): baseline, extended sequential and progressive huffman coded
 * images of 8 bits in one, three or four components, with the JFIF, Exif, ICC and Adobe conventions around them.
 * freestanding: needs only memcpy, memmove, memset, memcmp and the allocator interface.
 *
 * the decoder gives gray, b g r or cmyk pixels, optionally at 1/2, 1/4 or 1/8 of the size. the encoder takes gray,
 * b g r, b g r with a fourth byte, or cmyk pixels. every function is reentrant, nothing is kept between calls.
 * cmyk pixels hold the amount of ink, 0 is no ink, the files store the inverted values as Adobe programs do.
 */
#ifndef MIME_LIB_JPEG_JPEG_H
#define MIME_LIB_JPEG_JPEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_JPEG_OK = 0,
    MIME_JPEG_TRUNCATED = 1, /* tolerant decode: the entropy data was damaged or ended early, the rest is neutral. */

    MIME_JPEG_ERR_FORMAT = -1,      /* not a jpeg, or the headers are cut or inconsistent. */
    MIME_JPEG_ERR_UNSUPPORTED = -2, /* lossless, arithmetic, hierarchical, 12 bit, two components, no height. */
    MIME_JPEG_ERR_TOO_LARGE = -3,   /* more than MIME_JPEG_MAX_DIM or MIME_JPEG_MAX_PIXELS. */
    MIME_JPEG_ERR_DATA = -4,        /* strict decode: damaged or missing entropy coded data. */
    MIME_JPEG_ERR_NOMEM = -5,       /* the allocator failed. */
    MIME_JPEG_ERR_PARAM = -6,       /* invalid arguments. */
    MIME_JPEG_ERR_NOT_FOUND = -7,   /* a segment lookup found nothing. */
};

#define MIME_JPEG_MAX_DIM 65500u
#define MIME_JPEG_MAX_PIXELS (1u << 28)

/* decoder flags. */
#define MIME_JPEG_TOLERANT 0x01u    /* damaged data gives a picture and the status MIME_JPEG_TRUNCATED. */
#define MIME_JPEG_SCALE_HALF 0x02u  /* the size is divided by 2, 4 or 8 and rounded up, in the transform. */
#define MIME_JPEG_SCALE_QUARTER 0x04u
#define MIME_JPEG_SCALE_EIGHTH 0x08u
#define MIME_JPEG_NO_FANCY 0x10u    /* subsampled components are repeated instead of interpolated. */
#define MIME_JPEG_RAW_COLOR 0x20u   /* no color transform, the stored components are copied in their order. */

/* MimeJpegInfo.color_space. */
enum {
    MIME_JPEG_CS_GRAY = 1,
    MIME_JPEG_CS_RGB = 2,
    MIME_JPEG_CS_YCBCR = 3,
    MIME_JPEG_CS_CMYK = 4,
    MIME_JPEG_CS_YCCK = 5,
};

/* MimeJpegInfo.out_format, which is also the bytes per pixel of the output. */
enum {
    MIME_JPEG_FORMAT_GRAY8 = 1,
    MIME_JPEG_FORMAT_BGR24 = 3,
    MIME_JPEG_FORMAT_CMYK32 = 4,
};

typedef struct MimeJpegInfo {
    uint32_t width, height;
    int components;
    int precision;
    bool progressive;
    bool extended; /* sequential with the extended marker, which the encoder writes for 16 bit tables. */
    uint32_t restart_interval;
    bool jfif;
    uint8_t jfif_major, jfif_minor;
    uint8_t density_unit; /* 0 aspect ratio only, 1 dots per inch, 2 dots per centimeter. */
    uint16_t x_density, y_density;
    uint8_t thumb_width, thumb_height;
    bool adobe;
    uint8_t adobe_transform;
    int color_space; /* MIME_JPEG_CS_*, from the markers and the component identifiers. */
    struct {
        uint8_t id, h, v, tq;
    } comp[4];
    /* what a decode with the same flags gives. */
    uint32_t out_width, out_height;
    int out_format;
    int scale_denom; /* 1, 2, 4 or 8. */
} MimeJpegInfo;

/* the soi marker followed by another marker. */
bool mime_jpeg_is_jpeg(const uint8_t *data, size_t size);

/* reads the headers up to the first scan. flags select the scale. */
int mime_jpeg_info(const uint8_t *data, size_t size, unsigned flags, MimeJpegInfo *info);

/*
 * decodes into dest, rows at stride bytes, which has to hold out_width * out_format bytes at least, and dest_size
 * has to reach the end of the last row. nothing outside the pixels of a row is written. info may be null.
 * returns MIME_JPEG_OK, MIME_JPEG_TRUNCATED or an error.
 */
int mime_jpeg_decode(const uint8_t *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t *dest,
                     size_t stride, size_t dest_size, MimeJpegInfo *info);

/* ------------------------------------------------------------------ marker segments */

typedef struct MimeJpegSegments {
    const uint8_t *data;
    size_t size;
    size_t pos;
    bool done;
} MimeJpegSegments;

typedef struct MimeJpegSegment {
    int marker;           /* the byte after 0xff. */
    const uint8_t *data;  /* the payload, without the length. */
    uint32_t size;
    size_t offset;        /* of the 0xff byte in the file. */
} MimeJpegSegment;

/* the segments from the soi to the first scan header, that one included. */
int mime_jpeg_segments_init(MimeJpegSegments *it, const uint8_t *data, size_t size);
/* 1 with a segment, 0 at the end, MIME_JPEG_ERR_FORMAT when the structure is broken. */
int mime_jpeg_next_segment(MimeJpegSegments *it, MimeJpegSegment *seg);

/* the tiff structure of the exif block, the xmp packet and the icc profile, joined from its chunks. */
int mime_jpeg_find_exif(const uint8_t *data, size_t size, const uint8_t **payload, size_t *length);
int mime_jpeg_find_xmp(const uint8_t *data, size_t size, const uint8_t **payload, size_t *length);
int mime_jpeg_get_icc(const uint8_t *data, size_t size, const MimeAllocator *alloc, uint8_t **profile, size_t *length);

/* ------------------------------------------------------------------ encoder */

/* MimeJpegEncodeParams.subsampling and the sampling of the luma against the chroma. */
enum {
    MIME_JPEG_SUBSAMPLE_420 = 0, /* the default. */
    MIME_JPEG_SUBSAMPLE_444 = 1,
    MIME_JPEG_SUBSAMPLE_422 = 2,
    MIME_JPEG_SUBSAMPLE_440 = 3,
    MIME_JPEG_SUBSAMPLE_411 = 4,
};

/* the pixel layouts the encoder reads. */
enum {
    MIME_JPEG_PIXEL_GRAY8 = 1,
    MIME_JPEG_PIXEL_BGR24 = 2,
    MIME_JPEG_PIXEL_BGRA32 = 3, /* the fourth byte is ignored. */
    MIME_JPEG_PIXEL_BGRX32 = 4,
    MIME_JPEG_PIXEL_CMYK32 = 5,
};

/* MimeJpegEncodeParams.flags. */
#define MIME_JPEG_ENC_OPTIMIZE 0x01u    /* huffman tables made for the image. */
#define MIME_JPEG_ENC_PROGRESSIVE 0x02u /* implies the optimized tables. */
#define MIME_JPEG_ENC_BASELINE 0x04u    /* 8 bit quantization tables only, clamping the entries. */
#define MIME_JPEG_ENC_RGB 0x08u         /* color stored as r g b, not transformed. */
#define MIME_JPEG_ENC_YCCK 0x10u        /* cmyk stored transformed. */
#define MIME_JPEG_ENC_NO_JFIF 0x20u     /* no JFIF segment. */
#define MIME_JPEG_ENC_GRAYSCALE 0x40u   /* a color source is written as gray. */
#define MIME_JPEG_ENC_ZIGZAG_QTABLE 0x80u /* the custom tables are in zigzag order. */

typedef struct MimeJpegAppSegment {
    int marker; /* 0xe0 to 0xef or 0xfe. */
    const void *data;
    uint32_t size; /* at most 65533. */
} MimeJpegAppSegment;

typedef struct MimeJpegEncodeParams {
    int quality;           /* 1 to 100, 0 is 75. */
    int subsampling;       /* MIME_JPEG_SUBSAMPLE_*. */
    unsigned flags;        /* MIME_JPEG_ENC_*. */
    uint32_t restart_interval; /* in MCUs, 0 for none. */
    const uint16_t *qtable_luma;   /* 64 entries of 1 or more in natural order, replaces the quality. */
    const uint16_t *qtable_chroma;
    uint8_t density_unit;
    uint16_t x_density, y_density;
    const char *comment;
    uint32_t comment_size;
    const MimeJpegAppSegment *segments; /* written after the JFIF segment, in this order. */
    uint32_t segment_count;
} MimeJpegEncodeParams;

/* params may be null. the file is allocated with alloc and freed with mime_free. */
int mime_jpeg_encode(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride, int format,
                     const MimeJpegEncodeParams *params, const MimeAllocator *alloc, uint8_t **out, size_t *length);

/* the quantization table of a quality, natural order. */
void mime_jpeg_quality_table(int quality, bool chroma, uint16_t table[64]);

#endif
