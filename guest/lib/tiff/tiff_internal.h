/* private definitions shared by the files of the tiff library. */
#ifndef MIME_LIB_TIFF_INTERNAL_H
#define MIME_LIB_TIFF_INTERNAL_H

#include "tiff.h"

#define memcpy __builtin_memcpy
#define memmove __builtin_memmove
#define memset __builtin_memset
#define memcmp __builtin_memcmp
#define strlen __builtin_strlen

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error the tiff library assumes a little endian host
#endif

#define TIFF_LIKELY(x) __builtin_expect(!!(x), 1)
#define TIFF_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* unaligned accessors. */
typedef uint16_t __attribute__((aligned(1))) tiff_u16u;
typedef uint32_t __attribute__((aligned(1))) tiff_u32u;
typedef uint64_t __attribute__((aligned(1))) tiff_u64u;

static inline uint16_t tiff_rd16(const uint8_t *p, bool be)
{
    return be ? (uint16_t)(p[0] << 8 | p[1]) : (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t tiff_rd32(const uint8_t *p, bool be)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return be ? __builtin_bswap32(v) : v;
}

static inline uint64_t tiff_rd64(const uint8_t *p, bool be)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return be ? __builtin_bswap64(v) : v;
}

static inline void tiff_wr16(uint8_t *p, uint16_t v, bool be)
{
    if (be) {
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
    } else {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
    }
}

static inline void tiff_wr32(uint8_t *p, uint32_t v, bool be)
{
    if (be)
        v = __builtin_bswap32(v);
    memcpy(p, &v, 4);
}

/* ---------------------------------------------------------------- lzw. */

typedef struct TiffLzwEntry {
    const uint8_t *p;
    uint32_t len;
} TiffLzwEntry;

typedef struct TiffLzwDec {
    TiffLzwEntry tab[4096];
    uint8_t ident[256 + 16];
} TiffLzwDec;

typedef struct TiffLzwEnc {
    uint32_t hash[8192];
} TiffLzwEnc;

/*
 * decodes one lzw stream into dst, at most cap bytes. returns the number of bytes produced and sets *error when
 * the stream was invalid before it completed, a stream that is merely cut short is not an error.
 */
size_t tiff_lzw_decode(TiffLzwDec *st, const uint8_t *src, size_t n, uint8_t *dst, size_t cap, bool *error);

/* upper bound of the encoded size of n bytes. */
static inline size_t tiff_lzw_bound(size_t n)
{
    return n + n / 2 + n / 64 + 64;
}

/* encodes n bytes, dst has room for the bound of n bytes. returns the encoded size. */
size_t tiff_lzw_encode(TiffLzwEnc *st, const uint8_t *src, size_t n, uint8_t *dst);

/* ---------------------------------------------------------------- packbits and predictors. */

/* decodes into dst, at most cap bytes, returns the bytes produced. */
size_t tiff_packbits_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);

/* encodes one row, dst has room for n + n / 128 + 2, returns the encoded size. */
size_t tiff_packbits_encode_row(const uint8_t *src, size_t n, uint8_t *dst);

/* horizontal differencing, samples are native little endian units of 1, 2, 4 or 8 bytes. */
void tiff_predictor_undo(uint8_t *row, size_t samples, unsigned spp, unsigned bytes);
void tiff_predictor_apply(uint8_t *row, size_t samples, unsigned spp, unsigned bytes);

/* floating point predictor on one row of count samples of bytes each, tmp has room for the row. */
void tiff_float_undo(uint8_t *row, size_t count, unsigned spp, unsigned bytes, uint8_t *tmp);
void tiff_float_apply(uint8_t *row, size_t count, unsigned spp, unsigned bytes, uint8_t *tmp);

/* swaps the bytes of count units of 2, 4 or 8 bytes. */
void tiff_swap_units(uint8_t *p, size_t count, unsigned bytes);

/* reverses the bits of every byte. */
void tiff_reverse_bits(uint8_t *p, size_t n);

/* ---------------------------------------------------------------- ccitt. */

typedef struct TiffCcitt TiffCcitt;

/* bytes of state for images of a width, which the caller allocates. */
size_t tiff_ccitt_state_size(uint32_t width);

/* prepares the state for a decoder or an encoder, false for a width that is not supported. */
bool tiff_ccitt_init(TiffCcitt *st, uint32_t width, bool decode);

/*
 * decodes rows rows of pixels into dst, with row_bytes per row, 1 meaning black. compression is 2, 3 or 4.
 * the buffer holds zeros where the data ends early. returns the number of rows decoded, with *error set when
 * the data was invalid or ended before the last row.
 */
uint32_t tiff_ccitt_decode(TiffCcitt *st, unsigned compression, unsigned t4_options, const uint8_t *src, size_t n,
                           uint8_t *dst, size_t row_bytes, uint32_t rows, bool *error);

typedef struct TiffByteSink {
    const MimeAllocator *alloc;
    uint8_t **buf;
    size_t *len;
    size_t *cap;
    bool failed;
} TiffByteSink;

/*
 * appends the encoding of rows rows of bilevel pixels, row_bytes apart, to the sink. 1 means black unless invert
 * is set. the sink fails on an allocation error.
 */
void tiff_ccitt_encode(TiffCcitt *st, unsigned compression, bool two_dim, bool fill, bool invert, const uint8_t *src,
                       size_t row_bytes, uint32_t rows, TiffByteSink *out);

/* ---------------------------------------------------------------- writer internals. */

/* growing byte buffer used by the writer and the encoder. offsets are relative to base. */
typedef struct TiffBuf {
    const MimeAllocator *alloc;
    uint8_t *p;
    size_t len;
    size_t cap;
    size_t base;
    bool be;
    bool failed;
} TiffBuf;

bool tiff_buf_reserve(TiffBuf *b, size_t extra);
/* appends n zero bytes and returns their position, or SIZE_MAX on failure. */
size_t tiff_buf_zeros(TiffBuf *b, size_t n);
/* aligns the length to a power of two. */
bool tiff_buf_align(TiffBuf *b, size_t align);

/*
 * writes an ifd, its values, its sub ifds, thumbnail and chain at the end of b. returns the offset relative to
 * b->base in *off. depth guards the recursion.
 */
int tiff_write_ifd(TiffBuf *b, const MimeTiffWIfd *ifd, uint32_t depth, uint32_t *off);

/* copies count values of a type from src into dst changing the byte order when swap is set. */
void tiff_copy_values(uint8_t *dst, const uint8_t *src, uint64_t count, unsigned type, bool swap);

/* ---------------------------------------------------------------- helpers. */

/* num / den as 16.16 fixed point, saturating. */
int32_t tiff_ratio_fixed(uint32_t num, uint32_t den);

/* a / b for 16.16 values, a >= 0 and b > 0. */
int64_t tiff_fixed_div(int64_t a, int64_t b);

static inline bool tiff_is_bigendian_flag(uint32_t flags)
{
    return (flags & MIME_TIFF_W_BIG_ENDIAN) != 0;
}

#endif
