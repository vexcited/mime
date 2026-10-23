/*
 * tiff reader, decoder, encoder and ifd writer, per tiff 6.0 and exif. freestanding: needs only memcpy, memmove,
 * memset, memcmp, strlen and the allocator. deflate goes through ../png/inflate.h and ../png/deflate.h.
 *
 * multi byte values in structs and sample buffers are little endian, like every guest target. only the raw value
 * bytes of an entry keep the file's byte order.
 *
 * no state is kept between calls, so threads may share the library as long as they do not share an encoder or a
 * frame being modified.
 */
#ifndef MIME_LIB_TIFF_TIFF_H
#define MIME_LIB_TIFF_TIFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_TIFF_OK = 0,
    MIME_TIFF_ERR_FORMAT = -1,      /* bad header, truncated structure or impossible field values. */
    MIME_TIFF_ERR_UNSUPPORTED = -2, /* valid file using a feature that is not implemented. */
    MIME_TIFF_ERR_DATA = -3,        /* corrupt or truncated compressed image data. */
    MIME_TIFF_ERR_NOMEM = -4,       /* the allocator failed or a size limit was exceeded. */
    MIME_TIFF_ERR_PARAM = -5,       /* invalid arguments. */
    MIME_TIFF_ERR_RANGE = -6,       /* no such ifd, entry, chunk or row. */
};

/* the largest image handled, in bytes of decoded samples. */
#define MIME_TIFF_MAX_IMAGE_BYTES 0x7fffffffu

/* ---------------------------------------------------------------- tags, types and values. */

enum {
    MIME_TIFF_TAG_NEW_SUBFILE_TYPE = 254,
    MIME_TIFF_TAG_IMAGE_WIDTH = 256,
    MIME_TIFF_TAG_IMAGE_LENGTH = 257,
    MIME_TIFF_TAG_BITS_PER_SAMPLE = 258,
    MIME_TIFF_TAG_COMPRESSION = 259,
    MIME_TIFF_TAG_PHOTOMETRIC = 262,
    MIME_TIFF_TAG_FILL_ORDER = 266,
    MIME_TIFF_TAG_DOCUMENT_NAME = 269,
    MIME_TIFF_TAG_IMAGE_DESCRIPTION = 270,
    MIME_TIFF_TAG_MAKE = 271,
    MIME_TIFF_TAG_MODEL = 272,
    MIME_TIFF_TAG_STRIP_OFFSETS = 273,
    MIME_TIFF_TAG_ORIENTATION = 274,
    MIME_TIFF_TAG_SAMPLES_PER_PIXEL = 277,
    MIME_TIFF_TAG_ROWS_PER_STRIP = 278,
    MIME_TIFF_TAG_STRIP_BYTE_COUNTS = 279,
    MIME_TIFF_TAG_X_RESOLUTION = 282,
    MIME_TIFF_TAG_Y_RESOLUTION = 283,
    MIME_TIFF_TAG_PLANAR_CONFIG = 284,
    MIME_TIFF_TAG_T4_OPTIONS = 292,
    MIME_TIFF_TAG_T6_OPTIONS = 293,
    MIME_TIFF_TAG_RESOLUTION_UNIT = 296,
    MIME_TIFF_TAG_PAGE_NUMBER = 297,
    MIME_TIFF_TAG_SOFTWARE = 305,
    MIME_TIFF_TAG_DATE_TIME = 306,
    MIME_TIFF_TAG_ARTIST = 315,
    MIME_TIFF_TAG_HOST_COMPUTER = 316,
    MIME_TIFF_TAG_PREDICTOR = 317,
    MIME_TIFF_TAG_COLOR_MAP = 320,
    MIME_TIFF_TAG_TILE_WIDTH = 322,
    MIME_TIFF_TAG_TILE_LENGTH = 323,
    MIME_TIFF_TAG_TILE_OFFSETS = 324,
    MIME_TIFF_TAG_TILE_BYTE_COUNTS = 325,
    MIME_TIFF_TAG_SUB_IFDS = 330,
    MIME_TIFF_TAG_INK_SET = 332,
    MIME_TIFF_TAG_EXTRA_SAMPLES = 338,
    MIME_TIFF_TAG_SAMPLE_FORMAT = 339,
    MIME_TIFF_TAG_JPEG_TABLES = 347,
    MIME_TIFF_TAG_JPEG_INTERCHANGE_FORMAT = 513,
    MIME_TIFF_TAG_JPEG_INTERCHANGE_LENGTH = 514,
    MIME_TIFF_TAG_YCBCR_COEFFICIENTS = 529,
    MIME_TIFF_TAG_YCBCR_SUBSAMPLING = 530,
    MIME_TIFF_TAG_YCBCR_POSITIONING = 531,
    MIME_TIFF_TAG_REFERENCE_BLACK_WHITE = 532,
    MIME_TIFF_TAG_COPYRIGHT = 33432,
    MIME_TIFF_TAG_EXIF_IFD = 34665,
    MIME_TIFF_TAG_GPS_IFD = 34853,
    MIME_TIFF_TAG_INTEROP_IFD = 40965,
};

enum {
    MIME_TIFF_TYPE_BYTE = 1,
    MIME_TIFF_TYPE_ASCII = 2,
    MIME_TIFF_TYPE_SHORT = 3,
    MIME_TIFF_TYPE_LONG = 4,
    MIME_TIFF_TYPE_RATIONAL = 5,
    MIME_TIFF_TYPE_SBYTE = 6,
    MIME_TIFF_TYPE_UNDEFINED = 7,
    MIME_TIFF_TYPE_SSHORT = 8,
    MIME_TIFF_TYPE_SLONG = 9,
    MIME_TIFF_TYPE_SRATIONAL = 10,
    MIME_TIFF_TYPE_FLOAT = 11,
    MIME_TIFF_TYPE_DOUBLE = 12,
    MIME_TIFF_TYPE_IFD = 13,
    MIME_TIFF_TYPE_LONG8 = 16,
    MIME_TIFF_TYPE_SLONG8 = 17,
    MIME_TIFF_TYPE_IFD8 = 18,
};

enum {
    MIME_TIFF_COMPRESSION_NONE = 1,
    MIME_TIFF_COMPRESSION_CCITT_RLE = 2, /* modified huffman, rows are byte aligned. */
    MIME_TIFF_COMPRESSION_CCITT_G3 = 3,  /* t.4, one dimensional or two dimensional. */
    MIME_TIFF_COMPRESSION_CCITT_G4 = 4,  /* t.6. */
    MIME_TIFF_COMPRESSION_LZW = 5,
    MIME_TIFF_COMPRESSION_OLD_JPEG = 6,
    MIME_TIFF_COMPRESSION_JPEG = 7,
    MIME_TIFF_COMPRESSION_DEFLATE = 8,
    MIME_TIFF_COMPRESSION_JPEG2000 = 34712,
    MIME_TIFF_COMPRESSION_DEFLATE_OLD = 32946,
    MIME_TIFF_COMPRESSION_PACKBITS = 32773,
};

enum {
    MIME_TIFF_PHOTOMETRIC_WHITE_IS_ZERO = 0,
    MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO = 1,
    MIME_TIFF_PHOTOMETRIC_RGB = 2,
    MIME_TIFF_PHOTOMETRIC_PALETTE = 3,
    MIME_TIFF_PHOTOMETRIC_MASK = 4,
    MIME_TIFF_PHOTOMETRIC_SEPARATED = 5,
    MIME_TIFF_PHOTOMETRIC_YCBCR = 6,
    MIME_TIFF_PHOTOMETRIC_CIELAB = 8,
};

enum {
    MIME_TIFF_ALPHA_NONE = 0,
    MIME_TIFF_ALPHA_ASSOCIATED = 1,   /* premultiplied. */
    MIME_TIFF_ALPHA_UNASSOCIATED = 2, /* straight. */
    MIME_TIFF_ALPHA_UNSPECIFIED = 3,  /* extra sample of unknown meaning. */
};

enum {
    MIME_TIFF_SAMPLE_UINT = 1,
    MIME_TIFF_SAMPLE_INT = 2,
    MIME_TIFF_SAMPLE_FLOAT = 3,
    MIME_TIFF_SAMPLE_UNDEFINED = 4,
};

/* ---------------------------------------------------------------- reading files and ifds. */

typedef struct MimeTiff {
    const uint8_t *data;
    size_t size;
    uint64_t first_ifd;
    bool big_endian;
    bool bigtiff;
} MimeTiff;

/* a decoded ifd header. the entries stay in the file data, which must outlive every copy. */
typedef struct MimeTiffIfd {
    MimeTiff tiff;
    uint64_t offset;
    uint64_t next;  /* 0 for none. */
    uint32_t count; /* entries that fit in the data. */
    bool sorted;    /* tags ascending, lookups by tag are binary searches. */
} MimeTiffIfd;

/*
 * one directory entry. data points at the value bytes in the file's byte order, size is count times the type
 * size. data is NULL when the type is unknown or the value is outside the file or over 4 GiB, the other fields
 * stay valid.
 */
typedef struct MimeTiffEntry {
    uint16_t tag;
    uint16_t type;
    bool big_endian;
    bool inline_value; /* the value is stored in the entry itself. */
    uint64_t count;
    uint64_t value_offset;
    const uint8_t *data;
    size_t size;
} MimeTiffEntry;

/* byte size of one value of a type, 0 when unknown. rationals are 8, a pair of 32 bit values. */
size_t mime_tiff_type_size(unsigned type);

/* validates a classic or bigtiff header in either byte order, offsets being relative to data. skips the first ifd. */
int mime_tiff_open(MimeTiff *tiff, const void *data, size_t size);

/* same for the payload of a jpeg APP1 marker, with or without the leading "Exif\0\0", or of a png eXIf chunk. */
int mime_tiff_open_exif(MimeTiff *tiff, const void *data, size_t size);

/* a truncated entry table is shortened to the entries present. */
int mime_tiff_ifd_at(const MimeTiff *tiff, uint64_t offset, MimeTiffIfd *ifd);

/* the chain length, the frame count of a multipage file. it ends at a zero or invalid offset or a revisited ifd. */
uint32_t mime_tiff_ifd_count(const MimeTiff *tiff);

int mime_tiff_ifd_index(const MimeTiff *tiff, uint32_t index, MimeTiffIfd *ifd);

/* no cycle check, unlike mime_tiff_ifd_count. */
int mime_tiff_ifd_next(const MimeTiffIfd *ifd, MimeTiffIfd *next);

int mime_tiff_ifd_entry(const MimeTiffIfd *ifd, uint32_t index, MimeTiffEntry *entry);

int mime_tiff_ifd_find(const MimeTiffIfd *ifd, uint16_t tag, MimeTiffEntry *entry);

/* first value of an integer tag, false when absent or not an integer. */
bool mime_tiff_ifd_uint(const MimeTiffIfd *ifd, uint16_t tag, uint64_t *value);

/*
 * opens the sub ifd that value index of a tag points at. index is 0 for the exif, gps and interoperability ifds
 * and picks one of the MIME_TIFF_TAG_SUB_IFDS. offsets pointing back into the chain are not detected, so a
 * recursive caller needs a depth limit.
 */
int mime_tiff_ifd_sub(const MimeTiffIfd *ifd, uint16_t tag, uint64_t index, MimeTiffIfd *sub);

/* the jpeg thumbnail of an exif blob, from JPEGInterchangeFormat and its length in the second ifd. */
int mime_tiff_thumbnail(const MimeTiff *tiff, const uint8_t **data, size_t *size);

/* types byte, short, long, ifd, long8, ifd8 and undefined. */
bool mime_tiff_entry_uint(const MimeTiffEntry *entry, uint64_t i, uint64_t *value);

/* signed types too. false when the value does not fit. */
bool mime_tiff_entry_int(const MimeTiffEntry *entry, uint64_t i, int64_t *value);

bool mime_tiff_entry_rational(const MimeTiffEntry *entry, uint64_t i, int64_t *num, int64_t *den);

/* the raw bits of a float or double value. */
bool mime_tiff_entry_bits(const MimeTiffEntry *entry, uint64_t i, uint64_t *bits);

/* the text up to the first nul. it points into the file and is not terminated. */
bool mime_tiff_entry_string(const MimeTiffEntry *entry, const char **text, size_t *length);

/* truncates to cap and returns the full length. */
size_t mime_tiff_entry_copy_string(const MimeTiffEntry *entry, char *dst, size_t cap);

/* ---------------------------------------------------------------- writing ifds and exif blobs. */

#define MIME_TIFF_W_BIG_ENDIAN 0x1u /* entry flag: the data is big endian. */

typedef struct MimeTiffWIfd MimeTiffWIfd;

typedef struct MimeTiffWEntry {
    uint16_t tag;
    uint16_t type; /* not a bigtiff type. */
    uint32_t count;
    /*
     * count values of type, little endian unless flags has MIME_TIFF_W_BIG_ENDIAN. rationals are numerator,
     * denominator pairs. ascii counts include the nul.
     */
    const void *data;
    /* when set the entry points at this ifd, placed in the file by the writer. type, count and data are ignored. */
    const MimeTiffWIfd *sub;
    uint32_t flags;
} MimeTiffWEntry;

struct MimeTiffWIfd {
    const MimeTiffWEntry *entries; /* any order, the writer sorts them. a tag may appear once. */
    uint32_t entry_count;
    const MimeTiffWIfd *next; /* typically the thumbnail ifd after ifd 0. */
    const void *thumbnail;    /* jpeg data, placed in the file with JPEGInterchangeFormat and Length added. */
    size_t thumbnail_size;
};

#define MIME_TIFF_WRITE_BIG_ENDIAN 0x1u /* default little endian. */
#define MIME_TIFF_WRITE_EXIF_PREFIX 0x2u /* start the blob with "Exif\0\0", as in a jpeg APP1 marker. */

/*
 * serializes root, its chain and all its sub ifds as a classic tiff structure: an 8 byte header, then each
 * ifd followed by its out of line values, the sub ifds and the thumbnail. every offset is word aligned. the
 * result is released with mime_free. offsets are relative to the header, not to the optional prefix.
 */
int mime_tiff_write(const MimeTiffWIfd *root, unsigned flags, const MimeAllocator *alloc, void **out,
                    size_t *out_len);

#define MIME_TIFF_IMPORT_CHAIN 0x1u /* also import the ifds that follow, up to 8. */

/*
 * builds a writer description of a parsed ifd, to copy metadata between files. the exif, gps and interoperability
 * ifds become sub ifds, the sub ifds tag is dropped and JPEGInterchangeFormat becomes the thumbnail. entries point
 * at the source data, which must outlive the result. free *out with mime_free.
 */
int mime_tiff_import(const MimeTiffIfd *src, unsigned flags, const MimeAllocator *alloc, const MimeTiffWIfd **out);

/* ---------------------------------------------------------------- decoding frames. */

#define MIME_TIFF_DECODE_TOLERANT 0x1u   /* missing or corrupt chunks leave zeros and the call succeeds. */
#define MIME_TIFF_DECODE_RAW_YCBCR 0x2u  /* YCbCr stays Y, Cb, Cr, only upsampled. */
#define MIME_TIFF_DECODE_CCITT_PHOTOMETRIC 0x4u /* CCITT: black is 0 when photometric is BlackIsZero, see below. */

/*
 * one image of a file. the out_ fields give the pixel layout mime_tiff_frame_decode produces.
 *
 * the pixels are chunky, out_samples_per_pixel samples of out_bits_per_sample bits each, rows padded to whole
 * bytes. samples of 1, 2, 4 and 8 bits are returned as in the file, packed from the most significant bit
 * down. samples of 16, 32 and 64 bits are little endian, floating point samples are ieee bit patterns.
 * depths from 3 to 15 bits are expanded to 16 bits and scaled to the full range, except in palette images,
 * which are unsupported at those depths. fill order 2 is undone.
 *
 * YCbCr other than jpeg is upsampled and converted to 8 bit RGB, out_photometric says RGB then, unless
 * MIME_TIFF_DECODE_RAW_YCBCR is given. subsampled planes are never exposed. planar files are merged into
 * chunky pixels. the horizontal and floating point predictors are undone. the orientation is not applied.
 *
 * CCITT data has 1 for the pixels of black runs whatever the photometric says, like libtiff. with
 * MIME_TIFF_DECODE_CCITT_PHOTOMETRIC the bits are inverted for BlackIsZero, so black runs match the photometric.
 */
typedef struct MimeTiffFrame {
    MimeTiffIfd ifd;
    uint32_t width;
    uint32_t height;
    uint16_t bits_per_sample; /* as in the file, all samples have the same depth. */
    uint16_t samples_per_pixel;
    uint16_t sample_format;
    uint16_t photometric;
    uint16_t compression;
    uint16_t planar; /* 1 chunky, 2 planar. */
    uint16_t fill_order;
    uint16_t predictor;
    uint16_t orientation;      /* 1 to 8, 1 when absent. */
    uint16_t resolution_unit;  /* 1 none, 2 inch, 3 centimeter, 2 when absent. */
    uint16_t extra_samples;    /* count of extra samples, always the last ones. */
    uint16_t alpha;            /* MIME_TIFF_ALPHA_ of the first extra sample. */
    uint16_t ink_set;          /* 1 cmyk, 2 other. */
    uint16_t ycbcr_h;          /* subsampling, 1 when absent. */
    uint16_t ycbcr_v;
    uint16_t ycbcr_positioning;
    uint32_t rows_per_strip; /* clamped to the height. 0 for tiled images. */
    uint32_t tile_width;     /* 0 for stripped images. */
    uint32_t tile_height;
    uint32_t t4_options;
    uint32_t t6_options;
    uint32_t new_subfile_type;
    uint32_t x_resolution_num; /* pixels per resolution unit as a fraction, 0 denominators when absent. */
    uint32_t x_resolution_den;
    uint32_t y_resolution_num;
    uint32_t y_resolution_den;
    uint32_t palette_entries; /* 1 << bits for palette images with a color map, else 0. */
    uint32_t chunk_count;     /* strips or tiles in the file, all planes. */
    uint32_t chunks_per_plane;
    /* decoded pixel layout. */
    uint16_t out_bits_per_sample;
    uint16_t out_samples_per_pixel;
    uint16_t out_photometric;
    uint32_t out_row_bytes; /* the smallest stride. */
    /* the JPEGTables value, NULL when absent. */
    const uint8_t *jpeg_tables;
    size_t jpeg_tables_size;
    int32_t ycbcr_coefficients[3]; /* 16.16 fixed point, the luma weights of red, green and blue. */
    int32_t reference_black_white[6]; /* 16.16 fixed point. */
    /* private. */
    MimeTiffEntry offsets;
    MimeTiffEntry byte_counts;
    MimeTiffEntry color_map;
    bool has_byte_counts;
} MimeTiffFrame;

/* a strip or a tile as stored in the file. */
typedef struct MimeTiffChunk {
    const uint8_t *data; /* compressed data, NULL when it lies outside of the file. */
    size_t size;
    uint32_t x; /* the pixel rectangle that is visible in the image, clipped to its size. */
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t plane; /* sample plane for planar files, else 0. */
} MimeTiffChunk;

/* fails with MIME_TIFF_ERR_FORMAT when the size is missing or impossible. */
int mime_tiff_frame_open(const MimeTiffIfd *ifd, MimeTiffFrame *frame);

int mime_tiff_frame_open_index(const MimeTiff *tiff, uint32_t index, MimeTiffFrame *frame);

int mime_tiff_frame_tag(const MimeTiffFrame *frame, uint16_t tag, MimeTiffEntry *entry);

/* copies up to cap color map entries as 16 bit red, green, blue triples, returns the count. */
uint32_t mime_tiff_frame_palette(const MimeTiffFrame *frame, uint16_t *rgb, uint32_t cap);

/*
 * the raw data of a strip or tile and where it goes. JPEG and JPEG 2000 are not decoded here, the caller decodes
 * the chunk with frame->jpeg_tables. a missing byte count makes a chunk run to the next one or the end of the file.
 */
int mime_tiff_frame_chunk(const MimeTiffFrame *frame, uint32_t index, MimeTiffChunk *chunk);

/*
 * decodes the whole image into height rows of stride bytes, stride at least out_row_bytes. alloc is for temporary
 * memory. corrupt data fails with MIME_TIFF_ERR_DATA after decoding what it can, leaving bad rows zero.
 * unsupported combinations fail before touching out.
 */
int mime_tiff_frame_decode(const MimeTiffFrame *frame, const MimeAllocator *alloc, unsigned flags, void *out,
                           size_t stride);

/* decodes count rows from row y. a strip straddling the band is decoded whole, so strip aligned bands are cheapest. */
int mime_tiff_frame_decode_rows(const MimeTiffFrame *frame, const MimeAllocator *alloc, unsigned flags, uint32_t y,
                                uint32_t count, void *out, size_t stride);

/* ---------------------------------------------------------------- encoding. */

/*
 * what to write for one frame, zero fields take the noted default. the sample buffer is chunky, wide samples
 * little endian, rows padded to whole bytes, sub byte depths packed from the most significant bit. the file is
 * always written chunky. the photometric is only checked for palette images, which need a color map.
 */
typedef struct MimeTiffFrameSpec {
    uint32_t width;
    uint32_t height;
    uint16_t bits_per_sample;    /* 1, 2, 4, 8 or 16, also 32 and 64 together with a sample format. */
    uint16_t samples_per_pixel;  /* 1 to 16, with extra samples included. */
    uint16_t photometric;
    uint16_t sample_format;      /* 0 for unsigned integer, 2 signed, 3 floating point, from 16 bits up. */
    uint16_t extra_samples;      /* the last extra_samples samples are not color. */
    uint16_t alpha;              /* MIME_TIFF_ALPHA_ of the first extra sample, default unspecified. */
    uint16_t compression;        /* 0 or 1 none, 32773, 5, 8, 32946, and 2, 3, 4 for bilevel images. */
    uint16_t predictor;          /* 0 or 1 none, 2 horizontal, 3 floating point, only used with lzw and deflate. */
    uint16_t orientation;        /* 0 to omit the tag. */
    uint16_t resolution_unit;    /* 0 to omit the tag. */
    uint16_t deflate_level;      /* 0 for 6, up to 9. */
    uint16_t group3_two_dim;     /* nonzero for 2d t.4, a one dimensional row every fourth row. */
    uint16_t group3_fill;        /* nonzero to byte align the end of line codes. */
    uint16_t ccitt_photometric;  /* nonzero: BlackIsZero data has its black runs as 0 bits, see the decode flag. */
    uint32_t rows_per_strip;     /* 0 for about 8 KiB of samples per strip, at least one row. */
    uint32_t x_resolution_num;   /* both 0 to omit the tags. */
    uint32_t x_resolution_den;
    uint32_t y_resolution_num;
    uint32_t y_resolution_den;
    uint32_t new_subfile_type;   /* written when nonzero. */
    const char *software;        /* ascii tags, NULL to omit. */
    const char *date_time;       /* "YYYY:MM:DD HH:MM:SS". */
    const char *description;
    const char *artist;
    const char *copyright;
    const char *host_computer;
    const char *make;
    const char *model;
    const uint16_t *color_map;   /* 3 << bits_per_sample values, all reds, then greens, then blues. */
    const MimeTiffWEntry *extra_tags; /* more tags, replacing the generated ones with the same tag. */
    uint32_t extra_tag_count;
    const MimeTiffWIfd *exif_ifd; /* placed in the file, ExifIFD points at it. */
    const MimeTiffWIfd *gps_ifd;
} MimeTiffFrameSpec;

typedef struct MimeTiffEncoder {
    const MimeAllocator *alloc;
    uint8_t *buf;
    size_t len;
    size_t cap;
    size_t last_next_slot; /* where the offset of the next ifd gets patched in, 0 before the first frame. */
    uint32_t frames;
    bool big_endian;
    bool failed;
} MimeTiffEncoder;

#define MIME_TIFF_ENCODE_BIG_ENDIAN 0x1u /* default little endian. */

/*
 * starts a file. each frame writes its strips, then its ifd with its out of line values and exif and gps ifds,
 * so pixel data comes before the ifds. the header and next ifd offsets are patched as each following ifd is
 * written. a classic tiff cannot exceed 4 GiB, bigger results fail with MIME_TIFF_ERR_NOMEM. the default of
 * about 8 KiB of samples per strip matches libtiff. an error from mime_tiff_encoder_add_frame other than a
 * parameter error leaves the encoder unusable, it must still be finished or aborted to free its memory.
 */
int mime_tiff_encoder_init(MimeTiffEncoder *enc, const MimeAllocator *alloc, unsigned flags);

int mime_tiff_encoder_add_frame(MimeTiffEncoder *enc, const MimeTiffFrameSpec *spec, const void *pixels,
                                size_t stride);

/* *out is freed with mime_free. the encoder cannot be used any more. */
int mime_tiff_encoder_finish(MimeTiffEncoder *enc, void **out, size_t *out_len);

void mime_tiff_encoder_abort(MimeTiffEncoder *enc);

/* one frame files. */
int mime_tiff_encode(const MimeTiffFrameSpec *spec, const void *pixels, size_t stride, unsigned flags,
                     const MimeAllocator *alloc, void **out, size_t *out_len);

#endif
