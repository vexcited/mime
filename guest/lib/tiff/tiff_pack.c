#include "tiff_internal.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

size_t tiff_packbits_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    const uint8_t *p = src, *end = src + n;
    uint8_t *o = dst, *oend = dst + cap;
    while (p < end && o < oend) {
        int h = (int8_t)*p++;
        if (h >= 0) {
            size_t len = (size_t)h + 1;
            if (len > (size_t)(end - p))
                len = (size_t)(end - p);
            if (len > (size_t)(oend - o))
                len = (size_t)(oend - o);
            memcpy(o, p, len);
            o += len;
            p += len;
        } else if (h != -128) {
            if (p == end)
                break;
            size_t len = (size_t)(1 - h);
            if (len > (size_t)(oend - o))
                len = (size_t)(oend - o);
            memset(o, *p++, len);
            o += len;
        }
    }
    return (size_t)(o - dst);
}

static uint8_t *packbits_literal(uint8_t *o, const uint8_t *src, size_t a, size_t b)
{
    while (a < b) {
        size_t len = b - a < 128 ? b - a : 128;
        *o++ = (uint8_t)(len - 1);
        memcpy(o, src + a, len);
        o += len;
        a += len;
    }
    return o;
}

size_t tiff_packbits_encode_row(const uint8_t *src, size_t n, uint8_t *dst)
{
    uint8_t *o = dst;
    size_t i = 0, lit = 0;
    while (i < n) {
        uint8_t c = src[i];
        size_t maxr = n - i < 128 ? n - i : 128;
        size_t r = 1;
        while (r < maxr && src[i + r] == c)
            r++;
        if (r >= 3) {
            o = packbits_literal(o, src, lit, i);
            *o++ = (uint8_t)(1 - (int)r);
            *o++ = c;
            i += r;
            lit = i;
        } else {
            i += r;
            if (i - lit >= 128) {
                o = packbits_literal(o, src, lit, lit + 128);
                lit += 128;
            }
        }
    }
    o = packbits_literal(o, src, lit, n);
    return (size_t)(o - dst);
}

/* running sums per channel kept in registers, the stores would otherwise be forwarded to the next loads. */
#define UNDO_CHANNELS(T, row, n, spp)                       \
    do {                                                    \
        T a0 = row[0], a1 = (spp) > 1 ? row[1] : 0;         \
        T a2 = (spp) > 2 ? row[2] : 0, a3 = (spp) > 3 ? row[3] : 0; \
        for (size_t k = (spp); k + (spp) <= (n); k += (spp)) { \
            a0 = (T)(a0 + row[k]);                          \
            row[k] = a0;                                    \
            if ((spp) > 1) {                                \
                a1 = (T)(a1 + row[k + 1]);                  \
                row[k + 1] = a1;                            \
            }                                               \
            if ((spp) > 2) {                                \
                a2 = (T)(a2 + row[k + 2]);                  \
                row[k + 2] = a2;                            \
            }                                               \
            if ((spp) > 3) {                                \
                a3 = (T)(a3 + row[k + 3]);                  \
                row[k + 3] = a3;                            \
            }                                               \
        }                                                   \
    } while (0)

void tiff_predictor_undo(uint8_t *row, size_t n, unsigned spp, unsigned bytes)
{
    if (n <= spp)
        return;
    if (bytes == 1) {
        size_t i = spp;
#if defined(__SSE2__)
        if (spp == 4) {
            __m128i carry = _mm_setzero_si128();
            size_t j = 0;
            for (; j + 16 <= n; j += 16) {
                __m128i x = _mm_loadu_si128((const __m128i *)(row + j));
                x = _mm_add_epi8(x, _mm_slli_si128(x, 4));
                x = _mm_add_epi8(x, _mm_slli_si128(x, 8));
                x = _mm_add_epi8(x, carry);
                _mm_storeu_si128((__m128i *)(row + j), x);
                carry = _mm_shuffle_epi32(x, 0xff);
            }
            i = j ? j : spp;
        } else if (spp == 1) {
            __m128i carry = _mm_setzero_si128();
            size_t j = 0;
            for (; j + 16 <= n; j += 16) {
                __m128i x = _mm_loadu_si128((const __m128i *)(row + j));
                x = _mm_add_epi8(x, _mm_slli_si128(x, 1));
                x = _mm_add_epi8(x, _mm_slli_si128(x, 2));
                x = _mm_add_epi8(x, _mm_slli_si128(x, 4));
                x = _mm_add_epi8(x, _mm_slli_si128(x, 8));
                x = _mm_add_epi8(x, carry);
                _mm_storeu_si128((__m128i *)(row + j), x);
                __m128i t = _mm_unpackhi_epi8(x, x);
                t = _mm_shufflehi_epi16(t, 0xff);
                carry = _mm_shuffle_epi32(t, 0xff);
            }
            i = j ? j : 1;
        } else
#endif
        if (spp == 3) {
            UNDO_CHANNELS(uint8_t, row, n, 3);
            return;
        } else if (spp == 2) {
            UNDO_CHANNELS(uint8_t, row, n, 2);
            return;
        }
        for (; i < n; i++)
            row[i] = (uint8_t)(row[i] + row[i - spp]);
    } else if (bytes == 2) {
        tiff_u16u *r = (tiff_u16u *)row;
        if (spp == 1) {
            UNDO_CHANNELS(uint16_t, r, n, 1);
        } else if (spp == 2) {
            UNDO_CHANNELS(uint16_t, r, n, 2);
        } else if (spp == 3) {
            UNDO_CHANNELS(uint16_t, r, n, 3);
        } else if (spp == 4) {
            UNDO_CHANNELS(uint16_t, r, n, 4);
        } else {
            for (size_t i = spp; i < n; i++)
                r[i] = (uint16_t)(r[i] + r[i - spp]);
        }
    } else if (bytes == 4) {
        tiff_u32u *r = (tiff_u32u *)row;
        for (size_t i = spp; i < n; i++)
            r[i] = r[i] + r[i - spp];
    } else {
        tiff_u64u *r = (tiff_u64u *)row;
        for (size_t i = spp; i < n; i++)
            r[i] = r[i] + r[i - spp];
    }
}

void tiff_predictor_apply(uint8_t *row, size_t n, unsigned spp, unsigned bytes)
{
    if (n <= spp)
        return;
    if (bytes == 1) {
        for (size_t i = n; i-- > spp;)
            row[i] = (uint8_t)(row[i] - row[i - spp]);
    } else if (bytes == 2) {
        tiff_u16u *r = (tiff_u16u *)row;
        for (size_t i = n; i-- > spp;)
            r[i] = (uint16_t)(r[i] - r[i - spp]);
    } else if (bytes == 4) {
        tiff_u32u *r = (tiff_u32u *)row;
        for (size_t i = n; i-- > spp;)
            r[i] = r[i] - r[i - spp];
    } else {
        tiff_u64u *r = (tiff_u64u *)row;
        for (size_t i = n; i-- > spp;)
            r[i] = r[i] - r[i - spp];
    }
}

void tiff_float_undo(uint8_t *row, size_t count, unsigned spp, unsigned bytes, uint8_t *tmp)
{
    size_t total = count * bytes;
    for (size_t i = spp; i < total; i++)
        row[i] = (uint8_t)(row[i] + row[i - spp]);
    for (unsigned b = 0; b < bytes; b++) {
        const uint8_t *plane = row + (size_t)(bytes - 1 - b) * count;
        for (size_t j = 0; j < count; j++)
            tmp[j * bytes + b] = plane[j];
    }
    memcpy(row, tmp, total);
}

void tiff_float_apply(uint8_t *row, size_t count, unsigned spp, unsigned bytes, uint8_t *tmp)
{
    size_t total = count * bytes;
    for (unsigned b = 0; b < bytes; b++) {
        uint8_t *plane = tmp + (size_t)(bytes - 1 - b) * count;
        for (size_t j = 0; j < count; j++)
            plane[j] = row[j * bytes + b];
    }
    for (size_t i = total; i-- > spp;)
        tmp[i] = (uint8_t)(tmp[i] - tmp[i - spp]);
    memcpy(row, tmp, total);
}

void tiff_swap_units(uint8_t *p, size_t count, unsigned bytes)
{
    if (bytes == 2) {
        size_t i = 0;
#if defined(__SSE2__)
        for (; i + 8 <= count; i += 8) {
            __m128i x = _mm_loadu_si128((const __m128i *)(p + i * 2));
            x = _mm_or_si128(_mm_slli_epi16(x, 8), _mm_srli_epi16(x, 8));
            _mm_storeu_si128((__m128i *)(p + i * 2), x);
        }
#endif
        for (; i < count; i++) {
            uint8_t t = p[i * 2];
            p[i * 2] = p[i * 2 + 1];
            p[i * 2 + 1] = t;
        }
    } else if (bytes == 4) {
        for (size_t i = 0; i < count; i++) {
            uint32_t v;
            memcpy(&v, p + i * 4, 4);
            v = __builtin_bswap32(v);
            memcpy(p + i * 4, &v, 4);
        }
    } else if (bytes == 8) {
        for (size_t i = 0; i < count; i++) {
            uint64_t v;
            memcpy(&v, p + i * 8, 8);
            v = __builtin_bswap64(v);
            memcpy(p + i * 8, &v, 8);
        }
    }
}

void tiff_reverse_bits(uint8_t *p, size_t n)
{
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t v;
        memcpy(&v, p + i, 4);
        v = ((v >> 4) & 0x0f0f0f0fu) | ((v & 0x0f0f0f0fu) << 4);
        v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
        v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1);
        memcpy(p + i, &v, 4);
    }
    for (; i < n; i++) {
        unsigned v = p[i];
        v = ((v >> 4) & 0x0fu) | ((v & 0x0fu) << 4);
        v = ((v >> 2) & 0x33u) | ((v & 0x33u) << 2);
        v = ((v >> 1) & 0x55u) | ((v & 0x55u) << 1);
        p[i] = (uint8_t)v;
    }
}
