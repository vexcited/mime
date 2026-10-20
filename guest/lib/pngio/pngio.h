/*
 * png reader and writer working in native pixel layouts, see the png specification
 * iso 15948 or the w3c png second edition. unlike png.h, which only produces 32 bpp bgra, the
 * decoder here hands out the layout an imaging api such as wic reports for the file, and
 * the encoder takes the same layouts.
 * freestanding: needs only memcpy, memmove, memset, memcmp and the allocator interface.
 */
#ifndef MIME_LIB_PNGIO_PNGIO_H
#define MIME_LIB_PNGIO_PNGIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_PNGIO_OK = 0,
    MIME_PNGIO_ERR_FORMAT = -1,      /* bad signature, chunk layout or header fields. */
    MIME_PNGIO_ERR_CRC = -2,         /* crc mismatch in a critical chunk. */
    MIME_PNGIO_ERR_UNSUPPORTED = -3, /* image over the limits or unknown critical chunk. */
    MIME_PNGIO_ERR_DATA = -4,        /* corrupt, truncated or oversized image data. */
    MIME_PNGIO_ERR_NOMEM = -5,       /* the allocator failed. */
    MIME_PNGIO_ERR_PARAM = -6,       /* invalid arguments. */
};

/* decoder flags. */
#define MIME_PNGIO_SKIP_CRC 0x1u   /* do not verify chunk crcs. */
#define MIME_PNGIO_SKIP_ADLER 0x2u /* do not verify the zlib adler32. */
#define MIME_PNGIO_RAW 0x4u        /* deliver the file's own sample layout, see the raw formats below. */

/* default limit of width times height, the width and the height are also capped at 2147483647. */
#define MIME_PNGIO_MAX_PIXELS (1u << 28)

/* png color types. */
enum {
    MIME_PNGIO_COLOR_GRAY = 0,
    MIME_PNGIO_COLOR_RGB = 2,
    MIME_PNGIO_COLOR_PALETTE = 3,
    MIME_PNGIO_COLOR_GRAY_ALPHA = 4,
    MIME_PNGIO_COLOR_RGBA = 6,
};

/*
 * pixel layouts. rows are packed from the left, bits within a byte most significant first,
 * 16 bit samples are little endian, channels are listed in byte order.
 *
 * native formats, what the decoder delivers by default:
 *   file                                        result
 *   gray 1, 2, 4, 8 bit                         GRAY1, GRAY2, GRAY4, GRAY8
 *   gray 16 bit                                 GRAY16
 *   indexed 1, 2, 4, 8 bit, palette apart       INDEXED1, INDEXED2, INDEXED4, INDEXED8
 *   rgb 8 bit                                   BGR24
 *   rgb 16 bit                                  RGB48    r, g, b per pixel
 *   rgba 8 bit                                  BGRA32   straight alpha
 *   rgba 16 bit                                 RGBA64   r, g, b, a per pixel
 *   gray alpha 8 bit                            BGRA32   gray replicated
 *   gray alpha 16 bit                           RGBA64   gray replicated
 *   gray or rgb 8 bit with a tRNS key           BGRA32   alpha 0 where the key matches
 *   gray or rgb 16 bit with a tRNS key          RGBA64   alpha 0 where the key matches
 *   gray 1, 2, 4 bit with a tRNS key            BGRA32   gray scaled to 8 bits
 *   indexed with tRNS                           INDEXED*, the palette alpha is in the info
 *
 * with MIME_PNGIO_RAW the decoder returns the file layout: gray and indexed as above,
 * rgb as RGB24 or RGB48, rgba as RGBA32 or RGBA64, gray alpha as GRAY_ALPHA16 or
 * GRAY_ALPHA32, no alpha expansion for tRNS, 16 bit samples converted to little endian.
 *
 * the encoder accepts every format of this list. BGRX32 ignores the fourth byte.
 */
enum {
    MIME_PNGIO_FMT_NONE = 0,
    MIME_PNGIO_FMT_GRAY1,
    MIME_PNGIO_FMT_GRAY2,
    MIME_PNGIO_FMT_GRAY4,
    MIME_PNGIO_FMT_GRAY8,
    MIME_PNGIO_FMT_GRAY16,
    MIME_PNGIO_FMT_INDEXED1,
    MIME_PNGIO_FMT_INDEXED2,
    MIME_PNGIO_FMT_INDEXED4,
    MIME_PNGIO_FMT_INDEXED8,
    MIME_PNGIO_FMT_BGR24,
    MIME_PNGIO_FMT_RGB48,
    MIME_PNGIO_FMT_BGRA32,
    MIME_PNGIO_FMT_RGBA64,
    MIME_PNGIO_FMT_BGRX32,        /* encoder input only. */
    MIME_PNGIO_FMT_RGB24,         /* raw mode and encoder input. */
    MIME_PNGIO_FMT_RGBA32,        /* raw mode and encoder input. */
    MIME_PNGIO_FMT_GRAY_ALPHA16,  /* raw mode and encoder input, gray and alpha bytes. */
    MIME_PNGIO_FMT_GRAY_ALPHA32,  /* raw mode and encoder input, two little endian samples. */
};

/* bits per pixel of a format, 0 for an invalid one. */
unsigned mime_pngio_bits_per_pixel(int format);

/* bytes of one row of width pixels, 0 on overflow or an invalid format. */
size_t mime_pngio_row_bytes(int format, uint32_t width);

typedef struct MimePngioTime {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} MimePngioTime;

typedef struct MimePngioInfo {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    bool interlaced;
    bool has_alpha; /* an alpha channel, a tRNS key or palette alpha. */

    /* the animation control chunk. only the default image is decoded. */
    bool has_actl;
    uint32_t actl_frames;
    uint32_t actl_plays;

    /* palette: always filled for indexed files, a suggested palette otherwise. */
    bool has_plte;
    uint16_t palette_count;      /* entries present, at most 2^bit_depth. */
    uint8_t palette[256][3];     /* r, g, b, zero beyond palette_count. */
    uint8_t palette_alpha[256];  /* 255 unless a tRNS says otherwise. */
    bool has_palette_alpha;      /* a tRNS chunk for an indexed file was read. */

    /* tRNS key of a gray or rgb file, masked to the bit depth. gray in [0], else r, g, b. */
    bool has_trns_key;
    uint16_t trns_key[3];

    bool has_gama;
    uint32_t gama; /* gamma times 100000. */
    bool has_chrm;
    uint32_t chrm[8]; /* white x, y, red x, y, green x, y, blue x, y, all times 100000. */
    bool has_srgb;
    uint8_t srgb_intent;
    bool has_iccp;
    char iccp_name[80]; /* null terminated, see mime_pngio_iccp_data for the profile. */
    bool has_phys;
    uint32_t phys_x, phys_y; /* pixels per unit. */
    uint8_t phys_unit;       /* 0 unknown, 1 meter. */
    bool has_bkgd;
    uint16_t bkgd[3]; /* palette index in [0] for indexed files, gray in [0], else r, g, b. */
    bool has_sbit;
    uint8_t sbit[4];     /* significant bits per channel of the file. */
    uint8_t sbit_count;
    bool has_time;
    MimePngioTime time;
    bool has_exif;
    const uint8_t *exif; /* the eXIf payload, points into the input. */
    size_t exif_size;

    unsigned text_count;  /* tEXt, zTXt and iTXt items. */
    unsigned chunk_count; /* ancillary chunks not interpreted above, see MimePngioChunk. */
} MimePngioInfo;

/* where an extra chunk sits relative to PLTE and IDAT. */
enum {
    MIME_PNGIO_POS_BEFORE_PLTE = 0,
    MIME_PNGIO_POS_BEFORE_IDAT = 1,
    MIME_PNGIO_POS_AFTER_IDAT = 2,
};

/* a chunk kept verbatim, decoder side the private and unknown ancillary ones. */
typedef struct MimePngioChunk {
    char type[4];
    const void *data;
    size_t size;
    unsigned position; /* MIME_PNGIO_POS_*, a hint only for the encoder. */
} MimePngioChunk;

enum {
    MIME_PNGIO_TEXT_LATIN1 = 0, /* tEXt. */
    MIME_PNGIO_TEXT_ZTXT = 1,   /* zTXt, always compressed. */
    MIME_PNGIO_TEXT_ITXT = 2,   /* iTXt, utf-8. */
};

/* a text item as stored. the strings are null terminated and point into the input. */
typedef struct MimePngioText {
    unsigned kind;
    const char *keyword;            /* latin-1. */
    const char *language;           /* iTXt language tag, empty otherwise. */
    const char *translated_keyword; /* iTXt, utf-8, empty otherwise. */
    const uint8_t *data;            /* the text as stored, compressed or not. */
    size_t data_size;
    bool compressed;
    bool utf8; /* the text is utf-8, else latin-1. */
} MimePngioText;

/*
 * decoder behaviour, where the specification leaves room.
 *
 * checksums: a bad crc in a critical chunk is MIME_PNGIO_ERR_CRC unless MIME_PNGIO_SKIP_CRC, except for
 * IEND, whose crc is not looked at once the image data is complete. a bad crc in an ancillary chunk drops
 * that chunk. a wrong adler32 is an error unless MIME_PNGIO_SKIP_ADLER.
 *
 * truncation: a file may end after the image data without IEND, or inside the last IDAT or its crc, as long
 * as the zlib stream holds all the pixels. a missing or cut zlib trailer is fine. bytes after IEND, after the
 * zlib stream and inside the zlib stream beyond the image are ignored.
 *
 * limits: the decompressed image is never larger than what the header asks for, and a header asking for
 * more than a deflate stream of that size can hold is refused before anything is allocated. at most 16384
 * text items and 16384 other chunks are kept.
 *
 * chunks: unknown critical chunks are MIME_PNGIO_ERR_UNSUPPORTED, IDAT chunks must be consecutive, PLTE is
 * required for indexed images and ignored after the image data and for gray images. malformed or repeated
 * ancillary chunks are ignored, the first one counts. gAMA, cHRM, sRGB, iCCP, pHYs, bKGD and sBIT count
 * before the image data only, tIME, text, eXIf and acTL anywhere. tRNS may come before or after PLTE, a
 * tRNS key is reduced to the bit depth. an indexed palette is cut to 2^bit_depth entries, entries beyond
 * the PLTE read as opaque black. animated files decode as their first IDAT image and the animation chunks
 * are listed.
 *
 * memory: decoding needs the inflated image, from which the destination is written row by row. a file with
 * several IDAT chunks needs a joined copy of them as well. the inflate and deflate code of guest/lib/png
 * has stack frames above one page, so link __chkstk.
 */
typedef struct MimePngioDecoder MimePngioDecoder;

/* true when data starts with the png signature. */
bool mime_pngio_is_png(const void *data, size_t size);

/*
 * parses all chunks except the image data content. data must stay valid until
 * mime_pngio_close, the info and metadata point into it. alloc is copied.
 */
int mime_pngio_open(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags,
                    MimePngioDecoder **out);

/* same, with another limit on width times height, 0 for the default. */
int mime_pngio_open_limit(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags,
                          uint64_t max_pixels, MimePngioDecoder **out);

void mime_pngio_close(MimePngioDecoder *dec);

const MimePngioInfo *mime_pngio_info(const MimePngioDecoder *dec);

/* the native format of a file, and the raw one. */
int mime_pngio_native_format(const MimePngioInfo *info);
int mime_pngio_raw_format(const MimePngioInfo *info);

/* the format this decoder delivers, native or raw following the open flags. */
int mime_pngio_format(const MimePngioDecoder *dec);

/*
 * decodes the whole image into out, height rows of stride bytes, stride being at least
 * mime_pngio_row_bytes of the format and the width. may be called repeatedly.
 */
int mime_pngio_decode(MimePngioDecoder *dec, void *out, size_t stride);

/*
 * decodes the count rows from row y into out, whose first row is row y. the inflated image stays
 * in memory until mime_pngio_close or the next mime_pngio_decode, so rows can be requested in any
 * order without inflating again. interlaced images are decoded whole into a cache first.
 */
int mime_pngio_decode_rows(MimePngioDecoder *dec, uint32_t y, uint32_t count, void *out, size_t stride);

/* releases the memory kept for mime_pngio_decode_rows. */
void mime_pngio_release(MimePngioDecoder *dec);

/* metadata lists. text items and chunks are in file order. */
int mime_pngio_text(const MimePngioDecoder *dec, unsigned index, MimePngioText *text);
int mime_pngio_chunk(const MimePngioDecoder *dec, unsigned index, MimePngioChunk *chunk);

/*
 * the text of an item, decompressed when needed, in a buffer of size + 1 bytes with a null
 * terminator after the data. release with mime_pngio_free. a text that fails to decompress
 * gives MIME_PNGIO_ERR_DATA. the size is bounded to 64 MiB.
 */
int mime_pngio_text_data(const MimePngioDecoder *dec, unsigned index, void **data, size_t *size);

/* the decompressed icc profile, same ownership. MIME_PNGIO_ERR_PARAM when the file has none. */
int mime_pngio_iccp_data(const MimePngioDecoder *dec, void **data, size_t *size);

void mime_pngio_free(const MimePngioDecoder *dec, void *p);

/*
 * decodes a file into a buffer from alloc, release with mime_free. info, format and stride
 * may be NULL. the row stride is the smallest one.
 */
int mime_pngio_decode_alloc(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags,
                            void **pixels, size_t *stride, MimePngioInfo *info, int *format);

/* encoder. */

#define MIME_PNGIO_ENC_INTERLACE 0x1u /* write adam7 interlaced. */

enum {
    MIME_PNGIO_FILTER_ADAPTIVE = 0, /* per row minimum sum of absolute differences, the default. */
    MIME_PNGIO_FILTER_NONE = 1,
    MIME_PNGIO_FILTER_SUB = 2,
    MIME_PNGIO_FILTER_UP = 3,
    MIME_PNGIO_FILTER_AVERAGE = 4,
    MIME_PNGIO_FILTER_PAETH = 5,
};

typedef struct MimePngioTextItem {
    unsigned kind;                  /* MIME_PNGIO_TEXT_*. */
    const char *keyword;            /* 1 to 79 latin-1 characters. */
    const char *language;           /* iTXt only, NULL for none. */
    const char *translated_keyword; /* iTXt only, utf-8, NULL for none. */
    const void *text;
    size_t text_size;
    bool compress; /* tEXt never, zTXt always, iTXt as asked. */
} MimePngioTextItem;

typedef struct MimePngioEncode {
    const MimeAllocator *alloc;
    uint32_t width, height;
    int format;          /* MIME_PNGIO_FMT_*. */
    const void *pixels;  /* top down rows. */
    size_t stride;       /* bytes from a row to the next, 0 for the smallest one. */

    /* indexed formats: r, g, b entries, 1 to 2^depth of them, and optional alpha per entry. */
    const uint8_t *palette;
    unsigned palette_count;
    const uint8_t *palette_alpha;
    unsigned palette_alpha_count;

    /* tRNS key for gray and rgb output, in the sample range of the format. gray in [0]. */
    bool has_trns_key;
    uint16_t trns_key[3];

    unsigned flags;     /* MIME_PNGIO_ENC_*. */
    int filter;         /* MIME_PNGIO_FILTER_*. */
    int level;          /* zlib level 0 to 9, negative for the default 6. */
    size_t idat_size;   /* bytes of image data per chunk, 0 for 65536. */

    /* ancillary chunks. */
    bool has_gama;
    uint32_t gama;
    bool has_chrm;
    uint32_t chrm[8];
    bool has_srgb;
    uint8_t srgb_intent;
    const char *iccp_name; /* NULL for no profile, else 1 to 79 characters. */
    const void *iccp_data;
    size_t iccp_size;
    bool has_phys;
    uint32_t phys_x, phys_y;
    uint8_t phys_unit;
    bool has_bkgd;
    uint16_t bkgd[3];
    bool has_sbit;
    uint8_t sbit[4];
    bool has_time;
    MimePngioTime time;
    const void *exif;
    size_t exif_size;
    const MimePngioTextItem *texts;
    unsigned text_count;
    const MimePngioChunk *chunks; /* anything else, written at the given position. */
    unsigned chunk_count;
} MimePngioEncode;

/* writes a png file into a buffer from params->alloc, release with mime_free. */
int mime_pngio_encode(const MimePngioEncode *params, void **out, size_t *out_size);

/* helpers. */

/* swaps the bytes of count 16 bit units, src and dst may be equal or must not overlap. */
void mime_pngio_swap16(void *dst, const void *src, size_t count);

/* crc32 as used by png, start with 0. */
uint32_t mime_pngio_crc32(uint32_t crc, const void *data, size_t size);

/* chooses between the sse2 and the scalar code, on by default where compiled in. for tests. */
void mime_pngio_set_simd(bool enable);

#endif
