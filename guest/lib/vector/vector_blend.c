/*
 * scanline blending of coverage masked sources into 32 bit BGRA rows. vector.h
 * documents the formulas and the rounding.
 */
#include "vector_internal.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

static inline uint32_t clamp255(uint32_t v)
{
    return v > 255 ? 255 : v;
}

/* straight color from a premultiplied one. */
static inline uint32_t unpremul(uint32_t pc, uint32_t a)
{
    if (a == 0)
        return 0;
    if (a == 255)
        return pc;
    return clamp255((pc * 255 + a / 2) / a);
}

static inline uint32_t make(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    return a << 24 | r << 16 | g << 8 | b;
}

/* straight color sc, the low 24 bits, over a premultiplied destination. */
static inline uint32_t over_pargb(uint32_t d, uint32_t sc, uint32_t sa)
{
    uint32_t ia = 255 - sa;
    uint32_t b = mime_vec_div255((sc & 255) * sa + (d & 255) * ia);
    uint32_t g = mime_vec_div255((sc >> 8 & 255) * sa + (d >> 8 & 255) * ia);
    uint32_t r = mime_vec_div255((sc >> 16 & 255) * sa + (d >> 16 & 255) * ia);
    uint32_t a = mime_vec_div255(255 * sa + (d >> 24) * ia);
    return make(a, r, g, b);
}

static inline uint32_t over_argb(uint32_t d, uint32_t sc, uint32_t sa)
{
    uint32_t da = d >> 24;
    if (da == 255) {
        /* opaque: the same formula as the premultiplied one. */
        return over_pargb(d, sc, sa);
    }
    if (da == 0)
        return make(sa, sc >> 16 & 255, sc >> 8 & 255, sc & 255);
    uint32_t ia = 255 - sa;
    uint32_t den = sa * 255 + da * ia;
    uint32_t wd = da * ia, ws = sa * 255;
    uint32_t h = den / 2;
    uint32_t b = ((sc & 255) * ws + (d & 255) * wd + h) / den;
    uint32_t g = ((sc >> 8 & 255) * ws + (d >> 8 & 255) * wd + h) / den;
    uint32_t r = ((sc >> 16 & 255) * ws + (d >> 16 & 255) * wd + h) / den;
    return make(mime_vec_div255(den), r, g, b);
}

static uint32_t blend_px(uint32_t d, uint32_t s, uint32_t cov, uint32_t alpha, int mode, int dfmt, int sfmt)
{
    uint32_t sA = s >> 24;
    if (mode == MIME_VEC_BLEND_OVER) {
        uint32_t e = cov == 255 ? alpha : (alpha == 255 ? cov : mime_vec_div255(cov * alpha));
        if (e == 0)
            return d;
        uint32_t sa = sA == 255 ? e : mime_vec_div255(sA * e);
        if (sa == 0 && sfmt == MIME_VEC_FMT_ARGB)
            return d;
        if (sfmt == MIME_VEC_FMT_PARGB) {
            if (dfmt == MIME_VEC_FMT_PARGB) {
                uint32_t ia = 255 - sa;
                uint32_t b = clamp255((e == 255 ? (s & 255) : mime_vec_div255((s & 255) * e)) + mime_vec_div255((d & 255) * ia));
                uint32_t g = clamp255((e == 255 ? (s >> 8 & 255) : mime_vec_div255((s >> 8 & 255) * e)) +
                                      mime_vec_div255((d >> 8 & 255) * ia));
                uint32_t r = clamp255((e == 255 ? (s >> 16 & 255) : mime_vec_div255((s >> 16 & 255) * e)) +
                                      mime_vec_div255((d >> 16 & 255) * ia));
                uint32_t a = clamp255(sa + mime_vec_div255((d >> 24) * ia));
                return make(a, r, g, b);
            }
            if (sa == 0)
                return d;
            uint32_t sc = make(0, unpremul(s >> 16 & 255, sA), unpremul(s >> 8 & 255, sA), unpremul(s & 255, sA));
            return over_argb(d, sc, sa);
        }
        return dfmt == MIME_VEC_FMT_PARGB ? over_pargb(d, s, sa) : over_argb(d, s, sa);
    }
    /* source copy. */
    if (cov == 0)
        return d;
    uint32_t sa = alpha == 255 ? sA : mime_vec_div255(sA * alpha);
    uint32_t sc;
    if (sfmt == MIME_VEC_FMT_PARGB) {
        /* premultiplied source: straight color for the straight destination, scaled color otherwise. */
        if (dfmt == MIME_VEC_FMT_ARGB)
            sc = make(0, unpremul(s >> 16 & 255, sA), unpremul(s >> 8 & 255, sA), unpremul(s & 255, sA));
        else
            sc = alpha == 255 ? s
                              : make(0, mime_vec_div255((s >> 16 & 255) * alpha), mime_vec_div255((s >> 8 & 255) * alpha),
                                     mime_vec_div255((s & 255) * alpha));
    } else {
        sc = s;
    }
    if (dfmt == MIME_VEC_FMT_PARGB) {
        uint32_t pb, pg, pr;
        if (sfmt == MIME_VEC_FMT_PARGB) {
            pb = sc & 255;
            pg = sc >> 8 & 255;
            pr = sc >> 16 & 255;
        } else {
            pb = mime_vec_div255((sc & 255) * sa);
            pg = mime_vec_div255((sc >> 8 & 255) * sa);
            pr = mime_vec_div255((sc >> 16 & 255) * sa);
        }
        if (cov == 255)
            return make(sa, pr, pg, pb);
        uint32_t ic = 255 - cov;
        return make(mime_vec_div255(sa * cov + (d >> 24) * ic), mime_vec_div255(pr * cov + (d >> 16 & 255) * ic),
                    mime_vec_div255(pg * cov + (d >> 8 & 255) * ic), mime_vec_div255(pb * cov + (d & 255) * ic));
    }
    if (cov == 255)
        return make(sa, sc >> 16 & 255, sc >> 8 & 255, sc & 255);
    uint32_t da = d >> 24, ic = 255 - cov;
    uint32_t den = sa * cov + da * ic;
    if (den == 0)
        return 0;
    uint32_t ws = sa * cov, wd = da * ic, h = den / 2;
    uint32_t b = ((sc & 255) * ws + (d & 255) * wd + h) / den;
    uint32_t g = ((sc >> 8 & 255) * ws + (d >> 8 & 255) * wd + h) / den;
    uint32_t r = ((sc >> 16 & 255) * ws + (d >> 16 & 255) * wd + h) / den;
    return make(mime_vec_div255(den), r, g, b);
}

#if defined(__SSE2__)

static inline __m128i div255w(__m128i x)
{
    x = _mm_add_epi16(x, _mm_set1_epi16(128));
    return _mm_srli_epi16(_mm_add_epi16(x, _mm_srli_epi16(x, 8)), 8);
}

/* broadcasts the alpha word of each of the two pixels over its four lanes. */
static inline __m128i bcast_alpha(__m128i px)
{
    return _mm_shufflehi_epi16(_mm_shufflelo_epi16(px, 0xff), 0xff);
}

/* four coverage bytes to broadcast lane vectors for two pairs of pixels. */
static inline void bcast_cov(uint32_t c4, __m128i *lo, __m128i *hi)
{
    __m128i v = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)c4), _mm_setzero_si128());
    __m128i w = _mm_unpacklo_epi16(v, v);
    *lo = _mm_unpacklo_epi32(w, w);
    *hi = _mm_unpackhi_epi32(w, w);
}

typedef struct Pair {
    __m128i s;  /* straight color lanes, alpha lane replaced by 255. */
    __m128i sA; /* source alpha broadcast. */
} Pair;

static inline __m128i blend_pair(__m128i d, Pair p, __m128i cov, __m128i alpha, bool have_cov, bool have_alpha,
                                 int mode)
{
    __m128i c255 = _mm_set1_epi16(255);
    if (mode == MIME_VEC_BLEND_OVER) {
        __m128i e = cov;
        if (!have_cov)
            e = alpha;
        else if (have_alpha)
            e = div255w(_mm_mullo_epi16(cov, alpha));
        __m128i sa = div255w(_mm_mullo_epi16(p.sA, e));
        __m128i ia = _mm_sub_epi16(c255, sa);
        return div255w(_mm_add_epi16(_mm_mullo_epi16(p.s, sa), _mm_mullo_epi16(d, ia)));
    }
    /* source copy into a premultiplied destination. */
    __m128i sa = have_alpha ? div255w(_mm_mullo_epi16(p.sA, alpha)) : p.sA;
    __m128i pm = div255w(_mm_mullo_epi16(p.s, sa));
    __m128i c = have_cov ? cov : c255;
    __m128i ic = _mm_sub_epi16(c255, c);
    return div255w(_mm_add_epi16(_mm_mullo_epi16(pm, c), _mm_mullo_epi16(d, ic)));
}

#endif

static void blend_run(uint32_t *dst, const uint32_t *src, uint32_t color, const uint8_t *cov, uint32_t n, uint32_t alpha,
                      int mode, int dfmt, int sfmt)
{
    uint32_t i = 0;
#if defined(__SSE2__)
    /* the SIMD path covers straight sources and a destination that is premultiplied or opaque. */
    if (sfmt == MIME_VEC_FMT_ARGB && n >= 4) {
        const bool have_cov = cov != NULL;
        const bool have_alpha = alpha != 255;
        const bool pre = dfmt == MIME_VEC_FMT_PARGB;
        const __m128i zero = _mm_setzero_si128();
        const __m128i rgbmask = _mm_set_epi16(0, -1, -1, -1, 0, -1, -1, -1);
        const __m128i a255 = _mm_set_epi16(255, 0, 0, 0, 255, 0, 0, 0);
        const __m128i alpha_v = _mm_set1_epi16((short)alpha);
        __m128i cs = zero, csA = zero;
        if (!src) {
            uint32_t cb = color & 255, cg = color >> 8 & 255, cr = color >> 16 & 255, ca = color >> 24;
            cs = _mm_set_epi16(255, (short)cr, (short)cg, (short)cb, 255, (short)cr, (short)cg, (short)cb);
            csA = _mm_set1_epi16((short)ca);
        }
        /* full coverage of an opaque source replaces the pixels in the over mode. */
        const bool opaque_const = !src && (color >> 24) == 255 && alpha == 255 && mode == MIME_VEC_BLEND_OVER;
        for (; i + 4 <= n; i += 4) {
            uint32_t c4 = 0xffffffffu;
            if (have_cov) {
                __builtin_memcpy(&c4, cov + i, 4);
                if (c4 == 0)
                    continue;
            }
            __m128i dx = _mm_loadu_si128((const __m128i *)(dst + i));
            if (!pre) {
                /* straight destination: only when all four pixels are opaque. */
                uint32_t a4 = dst[i] & dst[i + 1] & dst[i + 2] & dst[i + 3];
                if ((a4 >> 24) != 255 || mode == MIME_VEC_BLEND_COPY) {
                    if (mode == MIME_VEC_BLEND_COPY && c4 == 0xffffffffu && alpha == 255) {
                        for (int k = 0; k < 4; k++)
                            dst[i + k] = src ? src[i + k] : color;
                        continue;
                    }
                    for (int k = 0; k < 4; k++)
                        dst[i + k] = blend_px(dst[i + k], src ? src[i + k] : color, have_cov ? cov[i + k] : 255, alpha,
                                              mode, dfmt, sfmt);
                    continue;
                }
            }
            __m128i sx = zero;
            __m128i s_lo, s_hi, a_lo, a_hi;
            if (src) {
                sx = _mm_loadu_si128((const __m128i *)(src + i));
                __m128i l = _mm_unpacklo_epi8(sx, zero), h = _mm_unpackhi_epi8(sx, zero);
                a_lo = bcast_alpha(l);
                a_hi = bcast_alpha(h);
                s_lo = _mm_or_si128(_mm_and_si128(l, rgbmask), a255);
                s_hi = _mm_or_si128(_mm_and_si128(h, rgbmask), a255);
            } else {
                s_lo = s_hi = cs;
                a_lo = a_hi = csA;
            }
            if (opaque_const && c4 == 0xffffffffu) {
                _mm_storeu_si128((__m128i *)(dst + i), _mm_set1_epi32((int)color));
                continue;
            }
            __m128i c_lo = zero, c_hi = zero;
            if (have_cov)
                bcast_cov(c4, &c_lo, &c_hi);
            Pair p0 = {s_lo, a_lo}, p1 = {s_hi, a_hi};
            __m128i o_lo = blend_pair(_mm_unpacklo_epi8(dx, zero), p0, c_lo, alpha_v, have_cov, have_alpha, mode);
            __m128i o_hi = blend_pair(_mm_unpackhi_epi8(dx, zero), p1, c_hi, alpha_v, have_cov, have_alpha, mode);
            _mm_storeu_si128((__m128i *)(dst + i), _mm_packus_epi16(o_lo, o_hi));
        }
    }
#endif
    for (; i < n; i++) {
        uint32_t c = cov ? cov[i] : 255;
        if (c == 0 && mode == MIME_VEC_BLEND_OVER)
            continue;
        dst[i] = blend_px(dst[i], src ? src[i] : color, c, alpha, mode, dfmt, sfmt);
    }
}

void mime_vec_blend_color(uint32_t *dst, uint32_t color, const uint8_t *cov, uint32_t count, int mode, int dst_fmt)
{
    blend_run(dst, NULL, color, cov, count, 255, mode, dst_fmt, MIME_VEC_FMT_ARGB);
}

void mime_vec_blend_row(uint32_t *dst, const uint32_t *src, const uint8_t *cov, uint32_t count, uint32_t alpha,
                        int mode, int dst_fmt, int src_fmt)
{
    if (alpha > 255)
        alpha = 255;
    blend_run(dst, src, 0, cov, count, alpha, mode, dst_fmt, src_fmt);
}
