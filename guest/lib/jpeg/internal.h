#ifndef MIME_LIB_JPEG_INTERNAL_H
#define MIME_LIB_JPEG_INTERNAL_H

#include "jpeg.h"

#if defined(__SSE2__) && !defined(MIME_JPEG_NO_SIMD)
#define MIME_JPEG_SSE2 1
#include <emmintrin.h>
#endif

#define JPEG_MAX_COMPS 4

/* tables.c */
extern const uint8_t mime_jpeg_natural_order[80];
extern const uint8_t mime_jpeg_std_quant_luma[64];
extern const uint8_t mime_jpeg_std_quant_chroma[64];
extern const uint8_t mime_jpeg_std_dc_luma_bits[16];
extern const uint8_t mime_jpeg_std_dc_luma_vals[12];
extern const uint8_t mime_jpeg_std_ac_luma_bits[16];
extern const uint8_t mime_jpeg_std_ac_luma_vals[162];
extern const uint8_t mime_jpeg_std_dc_chroma_bits[16];
extern const uint8_t mime_jpeg_std_dc_chroma_vals[12];
extern const uint8_t mime_jpeg_std_ac_chroma_bits[16];
extern const uint8_t mime_jpeg_std_ac_chroma_vals[162];

/* idct.c: inverse transform of nblocks adjacent blocks of coefficients, dct is 8, 4, 2 or 1. */
void mime_jpeg_idct_blocks(int dct, const int16_t *coef, size_t nblocks, const uint16_t *qt, uint8_t *out,
                           size_t stride);

/* upsample.c */
void mime_jpeg_up_h2v1_fancy(const uint8_t *in, uint8_t *out, uint32_t n);
void mime_jpeg_up_h2v2_fancy(const uint8_t *nrow, const uint8_t *frow, uint8_t *out, uint32_t n);
void mime_jpeg_up_h1v2_fancy(const uint8_t *nrow, const uint8_t *frow, uint8_t *out, uint32_t n, int lower);
void mime_jpeg_up_replicate(const uint8_t *in, uint8_t *out, uint32_t n, uint32_t hexp);
void mime_jpeg_up_nearest(const uint8_t *in, uint8_t *out, uint32_t out_n, uint32_t num, uint32_t den);

/* color.c: planar rows to interleaved output. */
void mime_jpeg_row_ycc_bgr(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, uint8_t *out, uint32_t n);
void mime_jpeg_row_rgb_bgr(const uint8_t *r, const uint8_t *g, const uint8_t *b, uint8_t *out, uint32_t n);
/* inverted 1 undoes the Adobe inversion, ycck 1 converts YCC to CMY first. */
void mime_jpeg_row_cmyk(const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, uint8_t *out,
                        uint32_t n, int ycck, int inverted);

/* encoder side, fdct.c */
typedef struct MimeJpegQuantizer {
    uint16_t mul[64];   /* reciprocal of the divisor, see fdct.c. */
    uint16_t bias[64];  /* half the divisor, added before dividing. */
    uint16_t scale[64]; /* second stage multiplier that completes the shift. */
} MimeJpegQuantizer;

void mime_jpeg_make_quantizer(const uint16_t qt_natural[64], MimeJpegQuantizer *qz);
/* forward dct and quantization of one block of 8 bit samples, result in natural order. */
void mime_jpeg_fdct_quant(const uint8_t *src, size_t stride, const MimeJpegQuantizer *qz, int16_t out[64]);

#endif
