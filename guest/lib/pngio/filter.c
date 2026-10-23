#include "pngio_internal.h"

#if PNGIO_SSE2
#include <emmintrin.h>
#endif

static inline unsigned paeth_pred(unsigned a, unsigned b, unsigned c)
{
    int pa = (int)b - (int)c, pb = (int)a - (int)c, pc = pa + pb;
    pa = pa < 0 ? -pa : pa;
    pb = pb < 0 ? -pb : pb;
    pc = pc < 0 ? -pc : pc;
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* scalar unfilters, i is the first byte to do and at least bpp. */

static void sub_scalar(uint8_t *cur, size_t n, size_t bpp, size_t i)
{
    for (; i < n; i++)
        cur[i] = (uint8_t)(cur[i] + cur[i - bpp]);
}

static void up_scalar(uint8_t *cur, const uint8_t *prev, size_t n, size_t i)
{
    for (; i < n; i++)
        cur[i] = (uint8_t)(cur[i] + prev[i]);
}

static void avg_scalar(uint8_t *cur, const uint8_t *prev, size_t n, size_t bpp, size_t i)
{
    for (; i < n; i++)
        cur[i] = (uint8_t)(cur[i] + ((cur[i - bpp] + prev[i]) >> 1));
}

static void paeth_scalar(uint8_t *cur, const uint8_t *prev, size_t n, size_t bpp, size_t i)
{
    for (; i < n; i++)
        cur[i] = (uint8_t)(cur[i] + paeth_pred(cur[i - bpp], prev[i], prev[i - bpp]));
}

#if PNGIO_SSE2

/* pixels of 3, 4, 6 and 8 bytes fit one register, 3 and 6 byte ones with a spare lane. */
static inline bool simd_bpp(size_t bpp)
{
    return bpp == 3 || bpp == 4 || bpp == 6 || bpp == 8;
}

/* bytes read by one pixel step. */
static inline size_t step_width(size_t bpp)
{
    return bpp <= 4 ? 4 : 8;
}

static inline __m128i ld(const uint8_t *p, size_t bpp)
{
    if (bpp <= 4) {
        int v;
        __builtin_memcpy(&v, p, 4);
        return _mm_cvtsi32_si128(v);
    }
    return _mm_loadl_epi64((const __m128i *)p);
}

static inline void st(uint8_t *p, __m128i v, size_t bpp)
{
    if (bpp <= 4) {
        int x = _mm_cvtsi128_si32(v);
        __builtin_memcpy(p, &x, 4);
    } else {
        _mm_storel_epi64((__m128i *)p, v);
    }
}

/* keeps the bytes of one pixel, clears the spare lane. */
static inline __m128i pixel_mask(size_t bpp)
{
    switch (bpp) {
    case 3:
        return _mm_cvtsi32_si128(0x00ffffff);
    case 6:
        return _mm_set_epi32(0, 0, 0xffff, -1);
    case 4:
        return _mm_cvtsi32_si128(-1);
    default:
        return _mm_set_epi32(0, 0, -1, -1);
    }
}

static inline __m128i abs16(__m128i x)
{
    return _mm_max_epi16(x, _mm_sub_epi16(_mm_setzero_si128(), x));
}

/* paeth predictor on 8 sixteen bit lanes. */
static inline __m128i paeth16(__m128i a, __m128i b, __m128i c)
{
    __m128i pa = _mm_sub_epi16(b, c);
    __m128i pb = _mm_sub_epi16(a, c);
    __m128i pc = _mm_add_epi16(pa, pb);
    pa = abs16(pa);
    pb = abs16(pb);
    pc = abs16(pc);
    __m128i not_a = _mm_or_si128(_mm_cmpgt_epi16(pa, pb), _mm_cmpgt_epi16(pa, pc));
    __m128i not_b = _mm_cmpgt_epi16(pb, pc);
    __m128i bc = _mm_or_si128(_mm_and_si128(not_b, c), _mm_andnot_si128(not_b, b));
    return _mm_or_si128(_mm_and_si128(not_a, bc), _mm_andnot_si128(not_a, a));
}

/* paeth predictor on the low 8 bytes of a, b and c, result in the low 8 bytes. */
static inline __m128i paeth8(__m128i a, __m128i b, __m128i c)
{
    __m128i z = _mm_setzero_si128();
    __m128i p = paeth16(_mm_unpacklo_epi8(a, z), _mm_unpacklo_epi8(b, z), _mm_unpacklo_epi8(c, z));
    return _mm_packus_epi16(p, p);
}

/* paeth predictor on 16 bytes. */
static inline __m128i paeth16b(__m128i a, __m128i b, __m128i c)
{
    __m128i z = _mm_setzero_si128();
    __m128i lo = paeth16(_mm_unpacklo_epi8(a, z), _mm_unpacklo_epi8(b, z), _mm_unpacklo_epi8(c, z));
    __m128i hi = paeth16(_mm_unpackhi_epi8(a, z), _mm_unpackhi_epi8(b, z), _mm_unpackhi_epi8(c, z));
    return _mm_packus_epi16(lo, hi);
}

/* average of a and b per byte, rounded down. */
static inline __m128i avg_floor(__m128i a, __m128i b)
{
    __m128i t = _mm_and_si128(_mm_xor_si128(a, b), _mm_set1_epi8(1));
    return _mm_sub_epi8(_mm_avg_epu8(a, b), t);
}

static void sub_sse2(uint8_t *cur, size_t n, size_t bpp)
{
    size_t w = step_width(bpp), i = bpp;
    __m128i m = pixel_mask(bpp);

    if (n >= bpp + w) {
        __m128i a = _mm_and_si128(ld(cur, bpp), m);
        for (; i + w <= n; i += bpp) {
            __m128i s = _mm_add_epi8(ld(cur + i, bpp), a);
            st(cur + i, s, bpp);
            a = _mm_and_si128(s, m);
        }
    }
    sub_scalar(cur, n, bpp, i);
}

static void up_sse2(uint8_t *cur, const uint8_t *prev, size_t n)
{
    size_t i = 0;

    for (; i + 16 <= n; i += 16) {
        __m128i x = _mm_loadu_si128((const __m128i *)(cur + i));
        __m128i b = _mm_loadu_si128((const __m128i *)(prev + i));
        _mm_storeu_si128((__m128i *)(cur + i), _mm_add_epi8(x, b));
    }
    up_scalar(cur, prev, n, i);
}

static void avg_sse2(uint8_t *cur, const uint8_t *prev, size_t n, size_t bpp)
{
    size_t w = step_width(bpp), i = bpp;
    __m128i m = pixel_mask(bpp);

    if (n >= bpp + w) {
        __m128i a = _mm_and_si128(ld(cur, bpp), m);
        for (; i + w <= n; i += bpp) {
            __m128i pred = _mm_and_si128(avg_floor(a, ld(prev + i, bpp)), m);
            __m128i s = _mm_add_epi8(ld(cur + i, bpp), pred);
            st(cur + i, s, bpp);
            a = _mm_and_si128(s, m);
        }
    }
    avg_scalar(cur, prev, n, bpp, i);
}

static void paeth_sse2(uint8_t *cur, const uint8_t *prev, size_t n, size_t bpp)
{
    size_t w = step_width(bpp), i = bpp;
    __m128i m = pixel_mask(bpp);

    if (n >= bpp + w) {
        __m128i a = _mm_and_si128(ld(cur, bpp), m);
        __m128i c = _mm_and_si128(ld(prev, bpp), m);
        for (; i + w <= n; i += bpp) {
            __m128i b = ld(prev + i, bpp);
            __m128i pred = _mm_and_si128(paeth8(a, b, c), m);
            __m128i s = _mm_add_epi8(ld(cur + i, bpp), pred);
            st(cur + i, s, bpp);
            a = _mm_and_si128(s, m);
            c = _mm_and_si128(b, m);
        }
    }
    paeth_scalar(cur, prev, n, bpp, i);
}

#endif

bool pngio_unfilter_row(uint8_t *cur, const uint8_t *prev, size_t n, unsigned bpp, unsigned type)
{
    size_t first = bpp < n ? bpp : n;
    size_t i;

    switch (type) {
    case 0:
        return true;
    case 1:
#if PNGIO_SSE2
        if (pngio_use_simd() && simd_bpp(bpp)) {
            sub_sse2(cur, n, bpp);
            return true;
        }
#endif
        sub_scalar(cur, n, bpp, first);
        return true;
    case 2:
#if PNGIO_SSE2
        if (pngio_use_simd()) {
            up_sse2(cur, prev, n);
            return true;
        }
#endif
        up_scalar(cur, prev, n, 0);
        return true;
    case 3:
        for (i = 0; i < first; i++)
            cur[i] = (uint8_t)(cur[i] + (prev[i] >> 1));
#if PNGIO_SSE2
        if (pngio_use_simd() && simd_bpp(bpp) && n > bpp) {
            avg_sse2(cur, prev, n, bpp);
            return true;
        }
#endif
        avg_scalar(cur, prev, n, bpp, first);
        return true;
    case 4:
        for (i = 0; i < first; i++)
            cur[i] = (uint8_t)(cur[i] + prev[i]);
#if PNGIO_SSE2
        if (pngio_use_simd() && simd_bpp(bpp) && n > bpp) {
            paeth_sse2(cur, prev, n, bpp);
            return true;
        }
#endif
        paeth_scalar(cur, prev, n, bpp, first);
        return true;
    default:
        return false;
    }
}

/* encoder side: every output byte depends on raw bytes only. */

static void filter_scalar(uint8_t *dst, const uint8_t *cur, const uint8_t *prev, size_t n, size_t bpp,
                          unsigned type, size_t i)
{
    switch (type) {
    case 1:
        for (; i < n; i++)
            dst[i] = (uint8_t)(cur[i] - (i >= bpp ? cur[i - bpp] : 0));
        break;
    case 2:
        for (; i < n; i++)
            dst[i] = (uint8_t)(cur[i] - prev[i]);
        break;
    case 3:
        for (; i < n; i++)
            dst[i] = (uint8_t)(cur[i] - (((i >= bpp ? cur[i - bpp] : 0) + prev[i]) >> 1));
        break;
    case 4:
        for (; i < n; i++) {
            unsigned a = i >= bpp ? cur[i - bpp] : 0, c = i >= bpp ? prev[i - bpp] : 0;
            dst[i] = (uint8_t)(cur[i] - paeth_pred(a, prev[i], c));
        }
        break;
    }
}

void pngio_filter_row(uint8_t *dst, const uint8_t *cur, const uint8_t *prev, size_t n, unsigned bpp, unsigned type)
{
    size_t i = 0;

    if (type == 0) {
        __builtin_memcpy(dst, cur, n);
        return;
    }
#if PNGIO_SSE2
    if (pngio_use_simd()) {
        /* the first pixel has no left neighbour. */
        size_t first = bpp < n ? bpp : n;
        if (type != 2 && first) {
            filter_scalar(dst, cur, prev, first, bpp, type, 0);
            i = first;
        }
        for (; i + 16 <= n; i += 16) {
            __m128i x = _mm_loadu_si128((const __m128i *)(cur + i));
            __m128i r;
            if (type == 1) {
                r = _mm_sub_epi8(x, _mm_loadu_si128((const __m128i *)(cur + i - bpp)));
            } else if (type == 2) {
                r = _mm_sub_epi8(x, _mm_loadu_si128((const __m128i *)(prev + i)));
            } else if (type == 3) {
                __m128i a = _mm_loadu_si128((const __m128i *)(cur + i - bpp));
                r = _mm_sub_epi8(x, avg_floor(a, _mm_loadu_si128((const __m128i *)(prev + i))));
            } else {
                __m128i a = _mm_loadu_si128((const __m128i *)(cur + i - bpp));
                __m128i b = _mm_loadu_si128((const __m128i *)(prev + i));
                __m128i c = _mm_loadu_si128((const __m128i *)(prev + i - bpp));
                r = _mm_sub_epi8(x, paeth16b(a, b, c));
            }
            _mm_storeu_si128((__m128i *)(dst + i), r);
        }
    }
#endif
    filter_scalar(dst, cur, prev, n, bpp, type, i);
}

uint64_t pngio_abs_sum(const uint8_t *p, size_t n)
{
    uint64_t sum = 0;
    size_t i = 0;

#if PNGIO_SSE2
    if (pngio_use_simd()) {
        __m128i acc = _mm_setzero_si128(), z = _mm_setzero_si128();
        for (; i + 16 <= n; i += 16) {
            __m128i x = _mm_loadu_si128((const __m128i *)(p + i));
            __m128i a = _mm_min_epu8(x, _mm_sub_epi8(z, x));
            acc = _mm_add_epi64(acc, _mm_sad_epu8(a, z));
        }
        uint64_t lanes[2];
        _mm_storeu_si128((__m128i *)lanes, acc);
        sum = lanes[0] + lanes[1];
    }
#endif
    for (; i < n; i++)
        sum += p[i] < 128 ? p[i] : 256u - p[i];
    return sum;
}
