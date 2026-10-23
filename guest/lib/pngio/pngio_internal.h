#ifndef MIME_LIB_PNGIO_INTERNAL_H
#define MIME_LIB_PNGIO_INTERNAL_H

#include "pngio.h"

#if defined(__SSE2__)
#define PNGIO_SSE2 1
#else
#define PNGIO_SSE2 0
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PNGIO_LE 1
#else
#define PNGIO_LE 0
#endif

extern bool pngio_simd;

static inline bool pngio_use_simd(void)
{
#if PNGIO_SSE2
    return pngio_simd;
#else
    return false;
#endif
}

static inline uint32_t pngio_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline unsigned pngio_be16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static inline void pngio_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* adam7 passes: x0, y0, dx, dy. */
extern const uint8_t pngio_adam7[7][4];

/* one pass of the image, the only one when not interlaced. */
typedef struct PngioPass {
    uint32_t x0, y0, dx, dy;
    uint32_t w, h;
    size_t rowbytes; /* bytes of a filtered row without its filter byte. */
} PngioPass;

/* fills in the passes and returns how many, 1 or 7. bits_per_pixel is the file's. */
unsigned pngio_pass_layout(uint32_t width, uint32_t height, bool interlaced, unsigned bits_per_pixel,
                           PngioPass pass[7]);

/* filters: 0 none, 1 sub, 2 up, 3 average, 4 paeth. */

/* in place, cur holds n filtered bytes. prev is the row above, zeros for the first. false on a bad type. */
bool pngio_unfilter_row(uint8_t *cur, const uint8_t *prev, size_t n, unsigned bpp, unsigned type);

/* dst gets cur filtered, cur and prev being raw rows. dst must not overlap them. */
void pngio_filter_row(uint8_t *dst, const uint8_t *cur, const uint8_t *prev, size_t n, unsigned bpp, unsigned type);

/* sum of the bytes read as signed values, absolute. */
uint64_t pngio_abs_sum(const uint8_t *p, size_t n);

/* channel shuffles over count pixels. dst may equal src but must not partially overlap it. */
void pngio_swap_rb24(uint8_t *dst, const uint8_t *src, size_t count);
void pngio_swap_rb32(uint8_t *dst, const uint8_t *src, size_t count);
void pngio_bgrx_to_rgb24(uint8_t *dst, const uint8_t *src, size_t count);
void pngio_ga8_to_bgra(uint8_t *dst, const uint8_t *src, size_t count);
void pngio_ga16_to_rgba64(uint8_t *dst, const uint8_t *src, size_t count);

/* converts between file rows, big endian in rgb order, and the formats. */
typedef struct PngioConv {
    int kind;
    unsigned depth;  /* file bit depth. */
    unsigned bits;   /* bits per pixel of the output. */
    unsigned samples; /* 16 bit samples per pixel for byte swaps. */
    uint16_t key[3]; /* tRNS key. */
    uint32_t lut[16]; /* gray below 8 bits with a key: bgra per sample value. */
} PngioConv;

/* format must be native or raw for this header. */
void pngio_conv_init(PngioConv *cv, const MimePngioInfo *info, int format);

/* converts count pixels of an unfiltered row, clearing the padding bits of packed rows. */
void pngio_conv_row(const PngioConv *cv, const uint8_t *src, uint8_t *dst, uint32_t count);

/* the reverse for the encoder: native row to a file row of the given color type and depth. */
void pngio_unconv_row(int format, const uint8_t *src, uint8_t *dst, uint32_t count);

#endif
