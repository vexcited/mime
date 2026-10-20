/*
 * jpeg decoder and encoder, see ITU T.81.
 * freestanding: needs only memcpy, memmove, memset, memcmp, strlen and the allocator interface.
 * no floating point is used. SSE2 code is compiled in when __SSE2__ is defined, unless
 * MIME_JPEG_NO_SIMD is defined, and gives the same output as the scalar code. kernel mode
 * users that do not save the vector state have to define MIME_JPEG_NO_SIMD.
 *
 * decoder: baseline, extended sequential 8 bit and progressive huffman files with 1, 3 or 4
 * components and any sampling factors from 1 to 4. 12 bit precision, arithmetic coding, lossless
 * and hierarchical files, files with 2 components and files that give the height in a DNL
 * segment return MIME_JPEG_ERR_UNSUPPORTED.
 *
 * bit exactness: for every well formed file the output equals the default decode of
 * libjpeg-turbo, djpeg -dct int, byte for byte, with and without MIME_JPEG_NO_FANCY, and for
 * every scale. this covers the slow integer idct and its reduced size forms, the triangle filter
 * upsampling of 2h1v, 2h2v and 1h2v chroma, replication for the other ratios, and the fixed
 * point color conversion. it does not hold in these cases. progressive files whose scans do
 * not refine every coefficient completely, for example cut short, where libjpeg smooths the
 * blocks and this decoder shows the coefficients as they are. sampling ratios that are not
 * whole numbers, which libjpeg refuses and this decoder upsamples by nearest neighbour. damaged
 * entropy data in tolerant mode, which follows libjpeg's rules for sequential files and restart
 * markers but does not match it in every corner. coefficients that overflow 16 bits after
 * dequantization, which only damaged or hostile files have.
 *
 * the encoder is bit exact against cjpeg -dct int for the same image and settings, see
 * mime_jpeg_encode.
 */
#ifndef MIME_LIB_JPEG_JPEG_H
#define MIME_LIB_JPEG_JPEG_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_JPEG_OK = 0,
    /* not an error: tolerant decode hit truncated or corrupt entropy data and filled the rest. */
    MIME_JPEG_TRUNCATED = 1,
    MIME_JPEG_ERR_FORMAT = -1,      /* not a jpeg file or damaged headers. */
    MIME_JPEG_ERR_UNSUPPORTED = -2, /* 12 bit, arithmetic coding, lossless, hierarchical, 2 components. */
    MIME_JPEG_ERR_DATA = -3,        /* corrupt entropy data in strict mode. */
    MIME_JPEG_ERR_NOMEM = -4,       /* the allocator failed. */
    MIME_JPEG_ERR_PARAM = -5,       /* invalid arguments, or an output buffer that is too small. */
    MIME_JPEG_ERR_TOO_LARGE = -6,   /* more than MIME_JPEG_MAX_PIXELS pixels. */
    MIME_JPEG_ERR_NOT_FOUND = -7,   /* metadata lookup found nothing. */
};

#define MIME_JPEG_MAX_DIMENSION 65500u
#define MIME_JPEG_MAX_PIXELS (1u << 28)

/* decode flags. */
#define MIME_JPEG_TOLERANT 0x1u       /* decode what is there on damaged data and return MIME_JPEG_TRUNCATED. */
#define MIME_JPEG_SCALE_HALF 0x2u     /* output 1/2 size, 4x4 reduced idct. */
#define MIME_JPEG_SCALE_QUARTER 0x4u  /* output 1/4 size, 2x2 reduced idct. */
#define MIME_JPEG_SCALE_EIGHTH 0x6u   /* output 1/8 size, dc only. */
#define MIME_JPEG_SCALE_MASK 0x6u
#define MIME_JPEG_NO_FANCY 0x8u       /* replicate chroma instead of the triangle filter, like djpeg -nosmooth. */
#define MIME_JPEG_RAW_COLOR 0x10u     /* no color transform or Adobe inversion, components are returned as stored. */

/* what the decoder writes per pixel. the value is the number of bytes per pixel. */
enum {
    MIME_JPEG_FORMAT_GRAY8 = 1,  /* one byte, 0 is black. */
    MIME_JPEG_FORMAT_BGR24 = 3,  /* b, g, r. */
    MIME_JPEG_FORMAT_CMYK32 = 4, /* c, m, y, k, 0 means no ink. */
};

/* color space the file is stored in, as decided from the markers and component ids. */
enum {
    MIME_JPEG_CS_GRAY = 1,
    MIME_JPEG_CS_YCBCR = 2,
    MIME_JPEG_CS_RGB = 3,
    MIME_JPEG_CS_CMYK = 4,
    MIME_JPEG_CS_YCCK = 5,
};

typedef struct MimeJpegComponent {
    uint8_t id;
    uint8_t h; /* horizontal sampling factor, 1 to 4. */
    uint8_t v; /* vertical sampling factor, 1 to 4. */
    uint8_t tq; /* quantization table selector. */
} MimeJpegComponent;

typedef struct MimeJpegInfo {
    uint32_t width;  /* image size in pixels. */
    uint32_t height;
    uint8_t components; /* 1, 3 or 4. */
    uint8_t precision;  /* bits per sample. */
    bool progressive;
    bool extended; /* SOF1, extended sequential. */
    uint8_t color_space; /* MIME_JPEG_CS_*. */
    MimeJpegComponent comp[4];
    uint16_t restart_interval; /* in MCUs, 0 when none, the last DRI before the first scan. */

    bool jfif;
    uint8_t jfif_major;
    uint8_t jfif_minor;
    uint8_t density_unit; /* 0 aspect ratio only, 1 dots per inch, 2 dots per cm. */
    uint16_t x_density;
    uint16_t y_density;
    uint8_t thumb_width; /* JFIF thumbnail, 0 when none. */
    uint8_t thumb_height;
    bool adobe;
    uint8_t adobe_transform;

    /* output the decode produces for the flags given to mime_jpeg_info. */
    uint32_t out_width;
    uint32_t out_height;
    uint8_t out_format;   /* MIME_JPEG_FORMAT_*. */
    uint8_t scale_denom;  /* 1, 2, 4 or 8. */
} MimeJpegInfo;

/*
 * reads the headers up to the first scan, without decoding pixels and without checking that
 * the entropy data is there. flags select the scale and MIME_JPEG_RAW_COLOR, and out_width,
 * out_height and out_format of info follow from them.
 */
int mime_jpeg_info(const void *data, size_t size, unsigned flags, MimeJpegInfo *info);

/*
 * decodes into out, which holds out_height rows of out_stride bytes, out_stride being at least
 * out_width times the bytes per pixel and out_size the size of the buffer. returns MIME_JPEG_OK,
 * MIME_JPEG_TRUNCATED, which only tolerant mode gives and means that the picture is complete but
 * part of it is neutral, or an error. info may be NULL. alloc provides temporary memory only: a
 * few times the size of one MCU row for sequential files with one scan, and the coefficients of
 * the whole image, 2 bytes per sample, for progressive and multi scan files. a tolerant decode
 * of a file that ends before its first scan gives a gray picture and MIME_JPEG_TRUNCATED. blocks
 * that the data does not reach keep the coefficients decoded so far, which are zero for sequential
 * files, so they come out as mid gray.
 */
int mime_jpeg_decode(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t *out,
                     size_t out_stride, size_t out_size, MimeJpegInfo *info);

/* same, allocating the output with alloc, rows of out_width times the bytes per pixel. release *out with mime_free. */
int mime_jpeg_decode_alloc(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t **out,
                           size_t *out_stride, MimeJpegInfo *info);

/* true when data starts with SOI and a marker. */
bool mime_jpeg_is_jpeg(const void *data, size_t size);

/*
 * marker segment iteration. fill a MimeJpegSegments with mime_jpeg_segments_init, then call
 * mime_jpeg_next_segment until it returns 0. segments come in file order from the one after SOI
 * up to and including the first SOS, which ends the iteration, or an EOI. markers that stand
 * alone, such as TEM and RSTn, are skipped. data points into the input and size is the payload
 * length without the two length bytes. returns 1 for a segment, 0 at the end and
 * MIME_JPEG_ERR_FORMAT when the structure is broken, after the intact segments were returned.
 */
typedef struct MimeJpegSegments {
    const uint8_t *base;
    size_t size;
    size_t pos;
    bool started;
    bool done;
} MimeJpegSegments;

typedef struct MimeJpegSegment {
    uint8_t marker; /* the byte after 0xFF, such as 0xE1 for APP1 or 0xFE for COM. */
    const uint8_t *data;
    uint32_t size;
    size_t offset; /* of the marker's 0xFF in the input. */
} MimeJpegSegment;

int mime_jpeg_segments_init(MimeJpegSegments *it, const void *data, size_t size);
int mime_jpeg_next_segment(MimeJpegSegments *it, MimeJpegSegment *seg);

/*
 * metadata lookup. the pointers go into the input and stay valid as long as it does.
 * exif: the TIFF stream that follows the "Exif\0\0" signature of an APP1 segment.
 * xmp: the packet after "http://ns.adobe.com/xap/1.0/\0" of an APP1 segment.
 * both return MIME_JPEG_OK or MIME_JPEG_ERR_NOT_FOUND, MIME_JPEG_ERR_FORMAT for broken files.
 */
int mime_jpeg_find_exif(const void *data, size_t size, const uint8_t **payload, size_t *payload_size);
int mime_jpeg_find_xmp(const void *data, size_t size, const uint8_t **payload, size_t *payload_size);

/*
 * assembles the ICC profile that is spread over APP2 "ICC_PROFILE\0" segments, ordered by their
 * sequence numbers, into a buffer from alloc, to release with mime_free. returns
 * MIME_JPEG_ERR_NOT_FOUND when there is none and MIME_JPEG_ERR_FORMAT when the sequence is
 * incomplete or inconsistent.
 */
int mime_jpeg_get_icc(const void *data, size_t size, const MimeAllocator *alloc, uint8_t **profile,
                      size_t *profile_size);

/*
 * encoder.
 *
 * pixels are rows of stride bytes, top down. alpha of BGRA and BGRX is ignored. CMYK input has
 * 0 for no ink and is stored inverted, as Photoshop does, in a file with an Adobe marker.
 */
enum {
    MIME_JPEG_PIXEL_GRAY8 = 1,
    MIME_JPEG_PIXEL_BGR24 = 2,
    MIME_JPEG_PIXEL_BGRA32 = 3,
    MIME_JPEG_PIXEL_BGRX32 = 4,
    MIME_JPEG_PIXEL_CMYK32 = 5,
};

/* chroma subsampling, the luma sampling factors are given as horizontal x vertical. */
enum {
    MIME_JPEG_SUBSAMPLE_420 = 0, /* 2x2, the default. */
    MIME_JPEG_SUBSAMPLE_444 = 1, /* 1x1. */
    MIME_JPEG_SUBSAMPLE_422 = 2, /* 2x1. */
    MIME_JPEG_SUBSAMPLE_440 = 3, /* 1x2. */
    MIME_JPEG_SUBSAMPLE_411 = 4, /* 4x1. */
};

#define MIME_JPEG_ENC_OPTIMIZE 0x1u     /* optimal huffman tables, like cjpeg -optimize. */
#define MIME_JPEG_ENC_PROGRESSIVE 0x2u  /* libjpeg's standard progressive script, implies optimized tables. */
#define MIME_JPEG_ENC_NO_JFIF 0x4u      /* no JFIF segment, so the file is a raw jpeg or an Adobe one. */
#define MIME_JPEG_ENC_RGB 0x8u          /* BGR input is stored as R, G, B components without transform. */
#define MIME_JPEG_ENC_YCCK 0x10u        /* CMYK input is stored as YCCK, Adobe transform 2. */
#define MIME_JPEG_ENC_ZIGZAG_QTABLE 0x20u /* the custom quantization tables are in zigzag order. */
#define MIME_JPEG_ENC_GRAYSCALE 0x40u   /* color input is converted to a one component file. */
#define MIME_JPEG_ENC_BASELINE 0x80u    /* limit the tables made from quality to 8 bit entries, like cjpeg -baseline. */

typedef struct MimeJpegAppSegment {
    uint8_t marker; /* 0xE0 to 0xEF or 0xFE, written as is. */
    const void *data;
    uint32_t size; /* at most 65533. */
} MimeJpegAppSegment;

typedef struct MimeJpegEncodeParams {
    int quality;         /* 1 to 100, 0 means 75. the tables are libjpeg's scaling of the Annex K tables, which
                            below quality 8 or so give 16 bit entries unless MIME_JPEG_ENC_BASELINE is set. */
    int subsampling;     /* MIME_JPEG_SUBSAMPLE_*, only used for 3 and 4 component YCbCr and YCCK files. */
    unsigned flags;      /* MIME_JPEG_ENC_*. */
    uint32_t restart_interval; /* in MCUs, 0 for none, at most 65535. */
    uint8_t density_unit;      /* JFIF density: 0 none, 1 dpi, 2 dots per cm. */
    uint16_t x_density;        /* 0 means 1. */
    uint16_t y_density;        /* 0 means 1. */
    const void *comment;       /* COM segment after the header, NULL for none. */
    uint32_t comment_size;
    const MimeJpegAppSegment *segments; /* written after the JFIF or Adobe segment, before the tables. */
    uint32_t segment_count;
    /* custom quantization tables of 64 entries, in natural order unless MIME_JPEG_ENC_ZIGZAG_QTABLE
     * is set, luma and chroma, or NULL to take them from quality. values above 255 make a 16 bit
     * table and an extended sequential file, they are limited to 65535. a zero entry is invalid. */
    const uint16_t *qtable_luma;
    const uint16_t *qtable_chroma;
} MimeJpegEncodeParams;

/*
 * encodes into a buffer obtained from alloc that grows as needed, to release with mime_free,
 * *out_len being its used length. there is no write callback. params may be NULL for libjpeg's
 * defaults: quality 75, 4:2:0, JFIF, the standard huffman tables. 3 component input is YCbCr
 * with libjpeg's fixed point transform unless MIME_JPEG_ENC_RGB is set. the output is the same
 * byte for byte as cjpeg -dct int writes for the same image with the matching -quality,
 * -sample, -restart with B, -optimize, -progressive, -rgb and -grayscale switches. returns
 * MIME_JPEG_OK or an error. on failure *out is NULL.
 */
int mime_jpeg_encode(const void *pixels, uint32_t width, uint32_t height, size_t stride, int pixel_format,
                     const MimeJpegEncodeParams *params, const MimeAllocator *alloc, uint8_t **out, size_t *out_len);

/* fills the 64 entry table of libjpeg's quality scaling of the Annex K table, in natural order. */
void mime_jpeg_quality_table(int quality, bool chroma, uint16_t table[64]);

#endif
