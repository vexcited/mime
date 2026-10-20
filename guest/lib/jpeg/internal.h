/*
 * what the parts of the jpeg codec share.
 */
#ifndef MIME_LIB_JPEG_INTERNAL_H
#define MIME_LIB_JPEG_INTERNAL_H

#include "jpeg.h"

#if defined(__SSE2__) && !defined(MIME_JPEG_NO_SIMD)
#define MIME_JPEG_SSE2 1
#include <emmintrin.h>
#endif

/* markers. */
#define M_SOF0 0xC0
#define M_SOF1 0xC1
#define M_SOF2 0xC2
#define M_DHT 0xC4
#define M_JPG 0xC8
#define M_DAC 0xCC
#define M_RST0 0xD0
#define M_SOI 0xD8
#define M_EOI 0xD9
#define M_SOS 0xDA
#define M_DQT 0xDB
#define M_DNL 0xDC
#define M_DRI 0xDD
#define M_APP0 0xE0
#define M_APP1 0xE1
#define M_APP2 0xE2
#define M_APP14 0xEE
#define M_COM 0xFE

/* the zigzag position k of the standard to the position in an 8 by 8 block in row order. */
extern const uint8_t jpeg_zigzag[64];

/* a huffman table as a file carries it: the counts of the codes of each length and the symbols in code order. */
typedef struct JpegHuffSpec {
    uint8_t bits[17]; /* [1] to [16]. */
    uint8_t vals[256];
    int count;
} JpegHuffSpec;

extern const JpegHuffSpec jpeg_std_dc[2], jpeg_std_ac[2];
extern const uint8_t jpeg_std_qtable[2][64]; /* annex K, natural order, luma then chroma. */

static inline int jpeg_min(int a, int b) { return a < b ? a : b; }
static inline int jpeg_max(int a, int b) { return a > b ? a : b; }
static inline uint8_t jpeg_clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static inline uint32_t jpeg_ceil_div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

/* the idct of one block at a time: coefficients in natural order, an output of n by n samples, n 8, 4, 2 or 1. */
void jpeg_idct(const int16_t *coef, const float *dequant, int last, int n, uint8_t *out, size_t stride);
/* the dequantization factors of a table, folding in whatever the transform of size n expects. */
void jpeg_idct_prepare(const uint16_t *qtable, float *dequant);

/* the forward transform of a block of 8 by 8 samples, centered on 0, and its quantization. */
void jpeg_fdct_quant(const uint8_t *samples, size_t stride, const float *inv_quant, int16_t *out);
void jpeg_fdct_prepare(const uint16_t *qtable, float *inv_quant);

#endif
