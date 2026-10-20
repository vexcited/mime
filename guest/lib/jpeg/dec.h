/*
 * the state of a decode, shared by the parts of the decoder.
 */
#ifndef MIME_LIB_JPEG_DEC_H
#define MIME_LIB_JPEG_DEC_H

#include "internal.h"

#define FAST_BITS 9

/* a huffman table for decoding: a lookup for the short codes and the limits of the longer ones. */
typedef struct JpegHuff {
    uint16_t fast[1 << FAST_BITS]; /* the length in the high byte and the symbol in the low one, 0 for none. */
    int32_t limit[17];             /* the first 16 bit window past the codes of each length. */
    int32_t delta[17];             /* from a code of that length to the index of its symbol. */
    uint8_t vals[256];
    bool defined;
} JpegHuff;

typedef struct JpegComp {
    uint8_t id, h, v, tq;
    uint32_t bw, bh;   /* the blocks of the padded storage. */
    uint32_t cbw, cbh; /* the blocks that cover the component, which is what a single component scan codes. */
    int n;             /* the size of the transform of its blocks, 8, 4, 2 or 1. */
    uint32_t cw, ch;   /* the samples of the component after the transform. */
    uint32_t pw, ph;   /* the plane in samples, padded to whole MCUs. */
    uint8_t *plane;
    int16_t *coef; /* all the blocks, when scans have to be put together. */
    float dq[64];
    bool dq_ready;
    int pred;
    int td, ta; /* the tables of the scan that is being decoded. */
} JpegComp;

typedef struct Jpeg {
    const uint8_t *data;
    size_t size, pos;
    const MimeAllocator *alloc;
    unsigned flags;
    bool tolerant;

    /* frame. */
    bool have_frame, progressive, extended;
    uint32_t width, height;
    int ncomp;
    JpegComp comp[4];
    int hmax, vmax;
    uint32_t mcux, mcuy;
    int scale; /* the size of the transform, 8, 4, 2 or 1. */
    uint32_t out_width, out_height;

    /* tables. */
    uint16_t qt[4][64];
    bool qt_defined[4];
    JpegHuff dc[4], ac[4];
    uint32_t restart;

    /* what the application segments say. */
    bool jfif, adobe;
    uint8_t jfif_major, jfif_minor, density_unit, thumb_width, thumb_height, adobe_transform;
    uint16_t x_density, y_density;
    uint32_t first_restart; /* the interval at the first scan. */

    /* the scans. */
    bool buffered;
    int scans;
    bool damaged;
    void *mem[24];
    int mem_count;
} Jpeg;

int jpeg_color_space(const Jpeg *j);

/* memory that is freed when the decode ends. */
void *jpeg_alloc(Jpeg *j, size_t size);

/* the upsampling and color conversion of the planes into dest. */
int jpeg_output(Jpeg *j, uint8_t *dest, size_t stride);

#endif
