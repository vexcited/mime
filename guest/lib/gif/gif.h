/*
 * gif87a and gif89a parser, lzw codec, frame composer, encoder and color
 * quantizer. pixels are 32 bpp b, g, r, a bytes, indices are 8 bit.
 * freestanding: needs only memcpy, memmove, memset, memcmp and the allocator.
 *
 * an opened MimeGif is read only, so several threads may decode its frames at
 * once. the input buffer must stay valid and unchanged for the life of the
 * handle, frame and extension data point into it.
 */
#ifndef MIME_LIB_GIF_GIF_H
#define MIME_LIB_GIF_GIF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_GIF_OK = 0,
    /*
     * non fatal: the result is usable. for open it means the file is damaged,
     * the details are in MimeGifInfo.warnings. for decode it means the lzw data
     * ended early or was invalid and the rest of the frame is filled.
     */
    MIME_GIF_TRUNCATED = 1, /* the data ended before the structure was complete. */
    MIME_GIF_DAMAGED = 2,   /* junk bytes were skipped, a code was invalid or a limit was hit. */

    MIME_GIF_ERR_FORMAT = -1,      /* not a gif, or the header is cut. */
    MIME_GIF_ERR_EMPTY = -2,       /* valid gif without any frame. */
    MIME_GIF_ERR_UNSUPPORTED = -3, /* too large. */
    MIME_GIF_ERR_DATA = -4,        /* the frame cannot be decoded at all, such as a bad lzw size. */
    MIME_GIF_ERR_NOMEM = -5,       /* the allocator failed. */
    MIME_GIF_ERR_PARAM = -6,       /* invalid arguments. */
};

/* MimeGifInfo.warnings bits, set by mime_gif_open. */
#define MIME_GIF_W_TRUNCATED 0x01u       /* the file ends inside a block or frame data. */
#define MIME_GIF_W_NO_TRAILER 0x02u      /* the file ends without the 0x3b trailer. */
#define MIME_GIF_W_JUNK 0x04u            /* bytes between blocks were skipped. */
#define MIME_GIF_W_LIMIT 0x08u           /* more frames or extensions than MIME_GIF_MAX_* were ignored. */
#define MIME_GIF_W_SCREEN_INFERRED 0x10u /* the logical screen was 0 sized, taken from the frames. */
#define MIME_GIF_W_BAD_FRAME 0x20u       /* a frame has an unusable lzw size or is larger than the pixel cap. */

#define MIME_GIF_MAX_FRAMES 65536u
#define MIME_GIF_MAX_EXTENSIONS 65536u
#define MIME_GIF_MAX_PIXELS (1u << 28) /* pixel cap of a decoded frame and of the logical screen. */

typedef struct MimeGif MimeGif;

typedef struct MimeGifInfo {
    uint16_t width; /* logical screen, or the frames' extent when the file declares 0. */
    uint16_t height;
    uint16_t declared_width; /* as stored in the file. */
    uint16_t declared_height;
    uint8_t version;             /* 87 or 89. */
    uint8_t color_resolution;    /* bits per primary color of the source image, 1..8. */
    uint8_t background_index;    /* into the global palette, meaningful only with one. */
    uint8_t pixel_aspect_ratio;  /* the raw byte, 0 is unspecified, else the ratio is n plus 15 over 64. */
    bool has_global_palette;
    bool global_palette_sorted;
    uint16_t global_palette_size;  /* entries, 0 or 2..256. */
    const uint8_t *global_palette; /* r, g, b triplets inside the input. */
    bool has_loop_count;           /* a NETSCAPE2.0 or ANIMEXTS1.0 loop extension was found. */
    uint16_t loop_count;           /* 0 means forever. */
    uint32_t frame_count;
    uint32_t extension_count;
    uint32_t comment_count;
    uint32_t warnings; /* MIME_GIF_W_*. */
} MimeGifInfo;

typedef struct MimeGifFrame {
    uint16_t left; /* as stored, may reach outside of the logical screen. */
    uint16_t top;
    uint16_t width; /* 0 for an empty frame, it has no data to decode. */
    uint16_t height;
    bool interlaced;
    bool has_local_palette;
    bool local_palette_sorted;
    uint16_t palette_size;  /* of the palette in effect, local else global, 0 if none. */
    const uint8_t *palette; /* r, g, b triplets inside the input, NULL when palette_size is 0. */
    bool has_gce;           /* a graphic control extension preceded this frame. */
    uint8_t disposal;       /* 0 unspecified, 1 keep, 2 restore to background, 3 restore to previous. */
    uint8_t disposal_raw;   /* the 3 bits as stored, 4..7 are reserved and read as 0 in disposal. */
    bool user_input;
    bool has_transparency;
    uint8_t transparent_index;
    uint16_t delay; /* in 1/100 s as stored. browsers raise values of 0 or 1 to 10. */
    uint8_t min_code_size; /* as stored. 2..8 decodes, 1 is read as 2. */
    bool complete;         /* the data blocks end with a terminator. false for a cut last frame. */
    bool decodable;        /* min_code_size is usable and the area is within MIME_GIF_MAX_PIXELS. */
    uint32_t data_bytes;   /* payload of the lzw sub-blocks without the length bytes. */
    size_t offset;         /* of the 0x2c separator in the input. */
    const uint8_t *data;   /* first lzw sub-block length byte. */
} MimeGifFrame;

enum {
    MIME_GIF_EXT_COMMENT = 1,
    MIME_GIF_EXT_PLAIN_TEXT = 2,
    MIME_GIF_EXT_APPLICATION = 3,
    MIME_GIF_EXT_UNKNOWN = 4, /* an extension label that is not known, kept raw. */
};

enum {
    MIME_GIF_APP_OTHER = 0,
    MIME_GIF_APP_NETSCAPE = 1, /* NETSCAPE2.0 and ANIMEXTS1.0, loop count and buffer size. */
    MIME_GIF_APP_XMP = 2,      /* "XMP DataXMP", the data is a raw packet and not chained. */
    MIME_GIF_APP_ICC = 3,      /* "ICCRGBG1012", an icc profile in sub-blocks. */
};

typedef struct MimeGifExtension {
    uint8_t kind; /* MIME_GIF_EXT_*. */
    uint8_t label;
    uint8_t app; /* MIME_GIF_APP_* for applications. */
    /* the frame this extension comes before, equal to frame_count when it follows the last one. */
    uint32_t frame_index;
    /*
     * application: the first block, normally the 8 byte identifier followed by
     * the 3 byte authentication code. plain text: the 12 byte grid header. else
     * empty. header_size is the stored block size.
     */
    const uint8_t *header;
    uint8_t header_size;
    /*
     * the content after the first block. when chained is true it is a run of
     * sub-blocks starting at a length byte, data_size is the payload size
     * without the length bytes and mime_gif_extension_copy flattens it. when
     * chained is false data holds data_size raw bytes, as for XMP.
     */
    const uint8_t *data;
    size_t data_size;
    bool chained;
    /* the graphic control extension that was pending before a plain text extension. */
    bool has_gce;
    uint8_t gce_disposal;
    bool gce_has_transparency;
    uint8_t gce_transparent_index;
    uint16_t gce_delay;
} MimeGifExtension;

/* true when data starts with GIF87a or GIF89a. */
bool mime_gif_is_gif(const void *data, size_t size);

/*
 * scans the block chain without decoding any lzw data and returns a handle,
 * allocated from alloc, in *out. the result is MIME_GIF_OK or a non fatal
 * MIME_GIF_TRUNCATED or MIME_GIF_DAMAGED, anything negative is an error and
 * leaves *out NULL. frames that were cut by the end of the file are listed too,
 * with complete set to false. junk bytes between blocks are skipped.
 */
int mime_gif_open(const void *data, size_t size, const MimeAllocator *alloc, MimeGif **out);
void mime_gif_close(MimeGif *gif);

const MimeGifInfo *mime_gif_info(const MimeGif *gif);
/* NULL when index is out of range. the pointers stay valid until mime_gif_close. */
const MimeGifFrame *mime_gif_frame(const MimeGif *gif, unsigned index);
const MimeGifExtension *mime_gif_extension(const MimeGif *gif, unsigned index);
/* copies the payload of a chained extension without length bytes, returns the bytes written. */
size_t mime_gif_extension_copy(const MimeGifExtension *ext, void *dst, size_t cap);

/*
 * raw frame path: decodes the indices of frame index into out, a buffer of
 * frame->height rows of stride bytes, stride at least frame->width, rows in
 * display order. nothing is written for an empty frame. when the lzw data ends
 * early or is invalid the rest is set to the transparent index of the frame or
 * 0 and MIME_GIF_TRUNCATED or MIME_GIF_DAMAGED is returned. data after the last
 * pixel is ignored. a frame that is not decodable gives MIME_GIF_ERR_DATA.
 */
int mime_gif_decode_frame(const MimeGif *gif, unsigned index, uint8_t *out, size_t stride);

/*
 * decodes one raw lzw stream, for callers that keep their own container. data
 * is the flattened payload of the sub-blocks, out holds width * height bytes
 * in stream order, not deinterlaced. returns like mime_gif_decode_frame. alloc
 * gives a few KiB of temporary memory.
 */
int mime_gif_lzw_decode(const uint8_t *data, size_t size, unsigned min_code_size, uint8_t *out, size_t out_size,
                        uint8_t fill, const MimeAllocator *alloc);

/* converts r, g, b triplets to bgra, entries past count become 0. transparent < 0 means none. */
void mime_gif_palette_to_bgra(const uint8_t *rgb, unsigned count, int transparent, uint32_t out[256]);

/*
 * decodes a frame to bgra pixels of the frame rectangle, straight alpha. pixels
 * with the transparent index or an index past the palette are 0. out_stride is
 * in bytes and at least width * 4.
 */
int mime_gif_decode_frame_bgra(const MimeGif *gif, unsigned index, uint8_t *out, size_t out_stride);

/*
 * composed path. renders the logical screen as a browser does. frame n is drawn
 * over the canvas the previous frame left after its disposal. pixels with
 * the transparent index leave the canvas alone, parts outside of the logical
 * screen are clipped. the canvas starts fully transparent. browsers ignore the
 * background color of the logical screen and so does this by default. disposal
 * 2 clears the frame rectangle to transparent, disposal 3 puts back what was
 * there before the frame, disposal 0 and 1 leave the frame in place. indices
 * past the palette are drawn as transparent. delays and the loop count are for
 * the caller.
 */
typedef struct MimeGifComposer MimeGifComposer;

/*
 * with this flag the canvas is cleared to the opaque background color of the
 * global palette, and disposal 2 restores it, as the specification describes.
 * a missing global palette or an index past it gives transparent.
 */
#define MIME_GIF_COMPOSE_USE_BACKGROUND 0x1u

int mime_gif_composer_create(const MimeGif *gif, unsigned flags, MimeGifComposer **out);
void mime_gif_composer_free(MimeGifComposer *composer);

/*
 * makes the canvas show frame index. consecutive indices are cheap. going
 * backwards or skipping restarts at the closest frame that repaints the whole
 * screen opaquely, else at frame 0. returns MIME_GIF_OK or a non fatal code from
 * the frame decode, MIME_GIF_ERR_DATA frames are skipped as empty and reported
 * as MIME_GIF_DAMAGED.
 */
int mime_gif_composer_render(MimeGifComposer *composer, unsigned index);

/* the bgra canvas, width * 4 bytes per row, valid until the next render. */
const uint8_t *mime_gif_composer_pixels(const MimeGifComposer *composer);

/* low level: draws decoded indices of a frame over a bgra canvas, clipped. */
void mime_gif_blit(const MimeGifFrame *frame, const uint8_t *indices, size_t indices_stride, uint8_t *canvas,
                   unsigned canvas_width, unsigned canvas_height, size_t canvas_stride);

/* encoder. */

typedef struct MimeGifEncoder MimeGifEncoder;

typedef struct MimeGifEncFrame {
    const uint8_t *indices; /* height rows of stride bytes, width bytes used. */
    size_t stride;
    uint16_t left;
    uint16_t top;
    uint16_t width;  /* 1 or more. */
    uint16_t height; /* 1 or more. */
    /*
     * local palette as r, g, b triplets. NULL uses the global palette, which
     * must exist then. a count that is not a power of two is padded with black
     * to the next one, a count of 1 becomes 2.
     */
    const uint8_t *palette;
    unsigned palette_size; /* 1..256. */
    uint16_t delay;        /* in 1/100 s. */
    uint8_t disposal;      /* 0..3. */
    bool user_input;
    bool has_transparency;
    uint8_t transparent_index;
    bool interlace;
} MimeGifEncFrame;

/*
 * starts a gif of the given logical screen. the global palette, r, g, b
 * triplets, may be NULL when every frame has a local one. same padding rules as
 * a frame palette. the color resolution is derived from the palette size.
 * background_index only matters with a global palette. the output starts as
 * GIF89a and is turned into GIF87a by mime_gif_encoder_finish when no frame
 * needs the 89a features, which are graphic control, comment and application
 * extensions.
 */
int mime_gif_encoder_create(const MimeAllocator *alloc, unsigned width, unsigned height,
                            const uint8_t *global_palette, unsigned global_palette_size, uint8_t background_index,
                            MimeGifEncoder **out);

/* NETSCAPE2.0 loop block, 0 loops forever. must come before the first frame. */
int mime_gif_encoder_set_loop(MimeGifEncoder *enc, unsigned loop_count);

/* a comment extension, split into sub-blocks. may be called between frames. */
int mime_gif_encoder_add_comment(MimeGifEncoder *enc, const char *text, size_t length);

/* a raw application extension, id is 8 bytes and auth 3 bytes, data is split into sub-blocks. */
int mime_gif_encoder_add_application(MimeGifEncoder *enc, const uint8_t id[8], const uint8_t auth[3],
                                     const uint8_t *data, size_t size);

int mime_gif_encoder_add_frame(MimeGifEncoder *enc, const MimeGifEncFrame *frame);

/*
 * writes the trailer and hands the buffer, allocated from the allocator of the
 * encoder, over to the caller who releases it with mime_free. the encoder is
 * freed. on failure the encoder is freed too and the result is an error.
 */
int mime_gif_encoder_finish(MimeGifEncoder *enc, uint8_t **data, size_t *size);
void mime_gif_encoder_free(MimeGifEncoder *enc);

/*
 * lzw compresses count indices in stream order and appends the
 * result as sub-blocks with the terminator to buf, which is grown with alloc.
 * min_code_size is 2..8, indices are masked to fit it. the building block of
 * the encoder, exposed for callers with their own container.
 */
typedef struct MimeGifBuffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} MimeGifBuffer;
int mime_gif_lzw_encode(const uint8_t *indices, size_t count, unsigned min_code_size, MimeGifBuffer *buf,
                        const MimeAllocator *alloc);

/* color quantizer. */

#define MIME_GIF_QUANT_DITHER 0x1u /* Floyd Steinberg error diffusion. */
/*
 * the palette keeps exactly max_colors entries even for few colors, unused ones
 * are black. without it the palette is as short as the image allows.
 */
#define MIME_GIF_QUANT_FULL_PALETTE 0x2u

/*
 * reduces a bgra image to at most max_colors, 2 to 256, palette entries and 8 bit
 * indices, width bytes per row at index_stride. pixels with alpha below 128 get
 * the transparent index, which then is the last palette entry and is counted in
 * max_colors. an image of at most max_colors colors, including that transparent
 * one, is mapped exactly and never dithered, else median cut is used with
 * 6 bits per channel precision. palette receives r, g, b triplets, 768 bytes.
 * *palette_size gets the entries used, *transparent_index the transparent entry
 * or -1.
 */
int mime_gif_quantize(const uint8_t *bgra, unsigned width, unsigned height, size_t stride, unsigned max_colors,
                      unsigned flags, uint8_t palette[768], unsigned *palette_size, int *transparent_index,
                      uint8_t *indices, size_t index_stride, const MimeAllocator *alloc);

/* fixed palettes, the layout matches WICBitmapPaletteType. */
enum {
    MIME_GIF_PAL_BLACK_WHITE = 0, /* 2. */
    MIME_GIF_PAL_GRAY4,           /* 4. */
    MIME_GIF_PAL_GRAY16,          /* 16. */
    MIME_GIF_PAL_GRAY256,         /* 256. */
    MIME_GIF_PAL_HALFTONE8,       /* 2 x 2 x 2 cube. */
    MIME_GIF_PAL_HALFTONE27,      /* 3 x 3 x 3. */
    MIME_GIF_PAL_HALFTONE64,      /* 4 x 4 x 4. */
    MIME_GIF_PAL_HALFTONE125,     /* 5 x 5 x 5. */
    MIME_GIF_PAL_HALFTONE216,     /* 6 x 6 x 6, the web safe colors. */
    MIME_GIF_PAL_HALFTONE252,     /* 6 x 7 x 6. */
    MIME_GIF_PAL_HALFTONE256,     /* 6 x 7 x 6 plus 4 grays. */
    MIME_GIF_PAL_COUNT,
};

/*
 * writes the entries of a fixed palette as r, g, b triplets, up to 768 bytes,
 * and returns the entry count, 0 for an unknown type. with add_transparent the
 * palette is one entry longer if that fits in 256, else the last entry is
 * replaced, and the extra entry is black and meant as the transparent one.
 */
unsigned mime_gif_fixed_palette(unsigned type, bool add_transparent, uint8_t palette[768]);

/*
 * maps a bgra image to a fixed palette: nearest entry per pixel, with optional
 * Floyd Steinberg dithering. alpha below 128 maps to transparent_index when it
 * is 0..255, else to the nearest color. returns MIME_GIF_OK.
 */
int mime_gif_map_fixed(const uint8_t *bgra, unsigned width, unsigned height, size_t stride, unsigned type,
                       unsigned flags, int transparent_index, uint8_t *indices, size_t index_stride,
                       const MimeAllocator *alloc);

/*
 * maps to any palette of count 1..256 entries by nearest color in rgb space.
 * transparent_index as above, that entry is never a target for opaque pixels.
 */
int mime_gif_map_palette(const uint8_t *bgra, unsigned width, unsigned height, size_t stride, const uint8_t *palette,
                         unsigned count, unsigned flags, int transparent_index, uint8_t *indices,
                         size_t index_stride, const MimeAllocator *alloc);

#endif
