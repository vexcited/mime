#include "internal.h"

/*
 * chroma upsampling. the fancy methods are triangle filters that weigh the nearer sample 3 to
 * 1 against its neighbour, with the rounding biases below, and keep the first and last column.
 */

void mime_jpeg_up_h2v1_fancy(const uint8_t *in, uint8_t *out, uint32_t n)
{
    uint32_t i = 1;
    out[0] = in[0];
    out[1] = (uint8_t)((in[0] * 3 + in[1] + 2) >> 2);
#ifdef MIME_JPEG_SSE2
    const __m128i z = _mm_setzero_si128();
    const __m128i one = _mm_set1_epi16(1);
    const __m128i two = _mm_set1_epi16(2);
    for (; i + 8 <= n - 1; i += 8) {
        __m128i cur = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(in + i)), z);
        __m128i prev = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(in + i - 1)), z);
        __m128i next = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(in + i + 1)), z);
        __m128i c3 = _mm_add_epi16(_mm_add_epi16(cur, cur), cur);
        __m128i a = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(c3, prev), one), 2);
        __m128i b = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(c3, next), two), 2);
        __m128i lo = _mm_unpacklo_epi16(a, b);
        __m128i hi = _mm_unpackhi_epi16(a, b);
        _mm_storeu_si128((__m128i *)(out + 2 * i), _mm_packus_epi16(lo, hi));
    }
#endif
    for (; i + 1 < n; i++) {
        unsigned v = in[i] * 3u;
        out[2 * i] = (uint8_t)((v + in[i - 1] + 1) >> 2);
        out[2 * i + 1] = (uint8_t)((v + in[i + 1] + 2) >> 2);
    }
    out[2 * n - 2] = (uint8_t)((in[n - 1] * 3 + in[n - 2] + 1) >> 2);
    out[2 * n - 1] = in[n - 1];
}

void mime_jpeg_up_h2v2_fancy(const uint8_t *nrow, const uint8_t *frow, uint8_t *out, uint32_t n)
{
    uint32_t i = 1;
    unsigned cs0 = nrow[0] * 3u + frow[0];
    unsigned cs1 = nrow[1] * 3u + frow[1];
    out[0] = (uint8_t)((cs0 * 4 + 8) >> 4);
    out[1] = (uint8_t)((cs0 * 3 + cs1 + 7) >> 4);
#ifdef MIME_JPEG_SSE2
    const __m128i z = _mm_setzero_si128();
    const __m128i b8 = _mm_set1_epi16(8);
    const __m128i b7 = _mm_set1_epi16(7);
    for (; i + 8 <= n - 1; i += 8) {
        __m128i cur = _mm_add_epi16(
            _mm_mullo_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(nrow + i)), z), _mm_set1_epi16(3)),
            _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(frow + i)), z));
        __m128i prev = _mm_add_epi16(
            _mm_mullo_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(nrow + i - 1)), z), _mm_set1_epi16(3)),
            _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(frow + i - 1)), z));
        __m128i next = _mm_add_epi16(
            _mm_mullo_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(nrow + i + 1)), z), _mm_set1_epi16(3)),
            _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(frow + i + 1)), z));
        __m128i c3 = _mm_add_epi16(_mm_add_epi16(cur, cur), cur);
        __m128i a = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(c3, prev), b8), 4);
        __m128i b = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(c3, next), b7), 4);
        _mm_storeu_si128((__m128i *)(out + 2 * i), _mm_packus_epi16(_mm_unpacklo_epi16(a, b), _mm_unpackhi_epi16(a, b)));
    }
#endif
    if (i > 1) {
        cs0 = nrow[i - 1] * 3u + frow[i - 1];
        cs1 = nrow[i] * 3u + frow[i];
    }
    /* here cs0 is the column sum before i and cs1 the one at i. */
    for (; i + 1 < n; i++) {
        unsigned nx = nrow[i + 1] * 3u + frow[i + 1];
        out[2 * i] = (uint8_t)((cs1 * 3 + cs0 + 8) >> 4);
        out[2 * i + 1] = (uint8_t)((cs1 * 3 + nx + 7) >> 4);
        cs0 = cs1;
        cs1 = nx;
    }
    out[2 * n - 2] = (uint8_t)((cs1 * 3 + cs0 + 8) >> 4);
    out[2 * n - 1] = (uint8_t)((cs1 * 4 + 7) >> 4);
}

void mime_jpeg_up_h1v2_fancy(const uint8_t *nrow, const uint8_t *frow, uint8_t *out, uint32_t n, int lower)
{
    uint32_t i = 0;
    unsigned bias = lower ? 2 : 1;
#ifdef MIME_JPEG_SSE2
    const __m128i z = _mm_setzero_si128();
    const __m128i three = _mm_set1_epi16(3);
    const __m128i vb = _mm_set1_epi16((short)bias);
    for (; i + 16 <= n; i += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(nrow + i));
        __m128i f = _mm_loadu_si128((const __m128i *)(frow + i));
        __m128i lo = _mm_srli_epi16(
            _mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(a, z), three), _mm_unpacklo_epi8(f, z)), vb), 2);
        __m128i hi = _mm_srli_epi16(
            _mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(a, z), three), _mm_unpackhi_epi8(f, z)), vb), 2);
        _mm_storeu_si128((__m128i *)(out + i), _mm_packus_epi16(lo, hi));
    }
#endif
    for (; i < n; i++)
        out[i] = (uint8_t)((nrow[i] * 3u + frow[i] + bias) >> 2);
}

void mime_jpeg_up_replicate(const uint8_t *in, uint8_t *out, uint32_t n, uint32_t hexp)
{
    uint32_t i = 0;
#ifdef MIME_JPEG_SSE2
    if (hexp == 2) {
        for (; i + 16 <= n; i += 16) {
            __m128i v = _mm_loadu_si128((const __m128i *)(in + i));
            _mm_storeu_si128((__m128i *)(out + 2 * i), _mm_unpacklo_epi8(v, v));
            _mm_storeu_si128((__m128i *)(out + 2 * i + 16), _mm_unpackhi_epi8(v, v));
        }
    } else if (hexp == 4) {
        for (; i + 16 <= n; i += 16) {
            __m128i v = _mm_loadu_si128((const __m128i *)(in + i));
            __m128i lo = _mm_unpacklo_epi8(v, v);
            __m128i hi = _mm_unpackhi_epi8(v, v);
            _mm_storeu_si128((__m128i *)(out + 4 * i), _mm_unpacklo_epi16(lo, lo));
            _mm_storeu_si128((__m128i *)(out + 4 * i + 16), _mm_unpackhi_epi16(lo, lo));
            _mm_storeu_si128((__m128i *)(out + 4 * i + 32), _mm_unpacklo_epi16(hi, hi));
            _mm_storeu_si128((__m128i *)(out + 4 * i + 48), _mm_unpackhi_epi16(hi, hi));
        }
    }
#endif
    for (; i < n; i++)
        for (uint32_t k = 0; k < hexp; k++)
            out[i * hexp + k] = in[i];
}

void mime_jpeg_up_nearest(const uint8_t *in, uint8_t *out, uint32_t out_n, uint32_t num, uint32_t den)
{
    uint32_t src = 0, acc = 0;
    for (uint32_t x = 0; x < out_n; x++) {
        out[x] = in[src];
        acc += num;
        while (acc >= den) {
            acc -= den;
            src++;
        }
    }
}
