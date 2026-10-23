/* encoder state, shared by the encoder files. */
#ifndef MIME_LIB_JPEG_ENC_H
#define MIME_LIB_JPEG_ENC_H

#include "internal.h"

typedef struct HuffSlot {
    uint16_t code[256];
    uint8_t size[256];
    uint32_t freq[257]; /* symbol counts for tables made for the image, 256 is the reserved code. */
    JpegHuffSpec spec;  /* what a DHT segment carries. */
} HuffSlot;

enum { MODE_GRAY, MODE_YCC, MODE_RGB, MODE_CMYK, MODE_YCCK };

typedef struct EncComp {
    int h, v;
    int tq;  /* the quantization table, 0 or 1. */
    int cls; /* the standard huffman tables it uses when not optimized, 0 or 1. */
    uint32_t bw, bh;   /* blocks across and down, padded to whole MCUs. */
    uint32_t cbw, cbh; /* blocks covering the component, which is what a single component scan codes. */
    int16_t *blocks;   /* quantized, in zigzag order. */
    int pred;
} EncComp;

typedef struct Enc {
    const MimeAllocator *alloc;
    void *mem[16];
    int mem_count;

    const uint8_t *pixels;
    uint32_t width, height;
    size_t stride;
    int format;

    int mode;
    int ncomp;
    EncComp comp[4];
    int hmax, vmax;
    uint32_t mcux, mcuy;
    bool progressive, optimize, buffered, wide_tables, baseline, separate;
    uint32_t restart;
    int nq; /* quantization tables in use. */
    uint16_t qt[2][64];
    float invq[2][64];

    /* work buffers for one MCU row. */
    uint8_t *full[4];
    size_t full_stride;
    uint8_t *sub[4];
    size_t sub_stride[4];

    /* output file. */
    uint8_t *buf;
    size_t pos, cap;
    uint64_t acc;
    int cnt;
    bool oom;
    unsigned rst_next;

    /* huffman. */
    HuffSlot dc[2], ac[2];
    bool counting;
} Enc;

void *enc_alloc(Enc *e, size_t size);
void enc_put_bytes(Enc *e, const void *data, size_t n);
void enc_put_u8(Enc *e, unsigned v);
void enc_put_u16(Enc *e, unsigned v);

/* entropy.c */
void huff_slot_standard(HuffSlot *s, const JpegHuffSpec *spec);
void huff_slot_optimal(HuffSlot *s);
void huff_slot_clear(HuffSlot *s);
void enc_write_dht(Enc *e, int cls, int id, const HuffSlot *s);
int enc_entropy_sequential(Enc *e);
int enc_entropy_progressive(Enc *e);

/* in encode.c. */
void enc_quantize_row(Enc *e, uint32_t mrow);

#endif
