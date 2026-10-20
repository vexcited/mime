#include "internal.h"

/*
 * the encoder. a row of MCUs at a time the input is converted to components, downsampled,
 * transformed and quantized. a plain baseline file with the standard tables is written as the
 * rows come. optimized tables and progressive files keep the quantized coefficients of the
 * whole image, gather the symbol statistics of every scan, build its tables and then write it.
 */

#define MAX_CLEN 48
#define MAX_CORR_BITS 1000

typedef struct Out {
    uint8_t *buf;
    size_t len, cap;
    bool fail;
    const MimeAllocator *alloc;
} Out;

typedef struct HTable {
    uint8_t bits[17]; /* number of codes of each length, from index 1. */
    uint8_t vals[256];
    uint16_t nvals;
    uint16_t code[256];
    uint8_t size[256];
    bool ready;
} HTable;

typedef struct EncComp {
    uint8_t id, h, v, tq, dc_tbl, ac_tbl;
    uint32_t wib, hib; /* blocks with image data. */
    uint32_t pitch;    /* blocks per row, padded to whole MCUs. */
    uint32_t dsw;      /* width of the downsampled samples, wib * 8. */
    uint32_t dss;      /* stride of the downsampled samples. */
    uint8_t *full;     /* full resolution samples of one MCU row. */
    uint8_t *ds;       /* downsampled samples of one MCU row. */
    int16_t *blocks;   /* quantized blocks of the whole image or one MCU row. */
    uint32_t last_dc;
} EncComp;

typedef struct Enc {
    const MimeAllocator *alloc;
    Out out;
    const uint8_t *pixels;
    uint32_t width, height;
    size_t stride;
    int format;
    unsigned flags;
    uint32_t restart_interval;

    int ncomp;
    EncComp comp[JPEG_MAX_COMPS];
    uint8_t maxh, maxv;
    uint32_t mcus_x, mcus_y;
    uint32_t full_w, full_stride; /* padded width and stride of the full resolution rows. */
    int color; /* MIME_JPEG_CS_ */
    bool progressive, optimize, full_buffer;

    uint16_t qt[2][64]; /* natural order. */
    bool qt_used[2];
    MimeJpegQuantizer qz[2];

    HTable dc[4], ac[4];
    uint32_t freq_dc[4][257], freq_ac[4][257];

    /* entropy coder state. */
    uint64_t acc;
    int nacc;
    bool gather;
    uint32_t eobrun;
    uint32_t be; /* correction bits waiting behind an end of band run. */
    uint8_t corr[MAX_CORR_BITS + 64];
    int ss, se, ah, al;
    int rst_next;

    void *allocs[24];
    int nallocs;
} Enc;

/* ---- output buffer ---- */

static bool out_reserve(Out *o, size_t extra)
{
    if (o->fail)
        return false;
    if (o->cap - o->len >= extra)
        return true;
    size_t ncap = o->cap ? o->cap : 4096;
    while (ncap - o->len < extra) {
        if (ncap > SIZE_MAX / 4) {
            o->fail = true;
            return false;
        }
        ncap *= 2;
    }
    uint8_t *nb = mime_alloc(o->alloc, ncap);
    if (!nb) {
        o->fail = true;
        return false;
    }
    if (o->len)
        __builtin_memcpy(nb, o->buf, o->len);
    mime_free(o->alloc, o->buf);
    o->buf = nb;
    o->cap = ncap;
    return true;
}

static void out_put8(Out *o, unsigned v)
{
    if (out_reserve(o, 1))
        o->buf[o->len++] = (uint8_t)v;
}

static void out_put16(Out *o, unsigned v)
{
    if (out_reserve(o, 2)) {
        o->buf[o->len++] = (uint8_t)(v >> 8);
        o->buf[o->len++] = (uint8_t)v;
    }
}

static void out_putmem(Out *o, const void *p, size_t n)
{
    if (n && out_reserve(o, n)) {
        __builtin_memcpy(o->buf + o->len, p, n);
        o->len += n;
    }
}

static void out_marker(Out *o, unsigned m)
{
    out_put8(o, 0xFF);
    out_put8(o, m);
}

/* ---- allocation ---- */

static void *enc_alloc(Enc *e, uint64_t size, bool zero)
{
    if (size == 0)
        size = 1;
    if (size > (uint64_t)SIZE_MAX / 2 || e->nallocs >= 24)
        return nullptr;
    void *p = mime_alloc(e->alloc, (size_t)size);
    if (!p)
        return nullptr;
    e->allocs[e->nallocs++] = p;
    if (zero)
        __builtin_memset(p, 0, (size_t)size);
    return p;
}

static void enc_release(Enc *e)
{
    for (int i = e->nallocs - 1; i >= 0; i--)
        mime_free(e->alloc, e->allocs[i]);
    e->nallocs = 0;
}

/* ---- quality tables ---- */

void mime_jpeg_quality_table(int quality, bool chroma, uint16_t table[64])
{
    if (quality < 1)
        quality = 1;
    if (quality > 100)
        quality = 100;
    uint32_t scale = quality < 50 ? 5000u / (uint32_t)quality : 200u - (uint32_t)quality * 2;
    const uint8_t *base = chroma ? mime_jpeg_std_quant_chroma : mime_jpeg_std_quant_luma;
    for (int i = 0; i < 64; i++) {
        uint32_t v = (base[i] * scale + 50) / 100;
        table[i] = (uint16_t)(v < 1 ? 1 : v > 32767 ? 32767 : v);
    }
}

/* ---- huffman tables ---- */

static void derive_table(HTable *t)
{
    unsigned code = 0, k = 0;
    __builtin_memset(t->size, 0, sizeof t->size);
    for (unsigned l = 1; l <= 16; l++) {
        for (unsigned i = 0; i < t->bits[l]; i++, k++, code++) {
            t->size[t->vals[k]] = (uint8_t)l;
            t->code[t->vals[k]] = (uint16_t)code;
        }
        code <<= 1;
    }
    t->ready = true;
}

static void set_std_table(HTable *t, const uint8_t bits[16], const uint8_t *vals, unsigned n)
{
    t->bits[0] = 0;
    for (int i = 0; i < 16; i++)
        t->bits[i + 1] = bits[i];
    __builtin_memcpy(t->vals, vals, n);
    t->nvals = (uint16_t)n;
    derive_table(t);
}

/* builds the table with the shortest total length from symbol counts, limited to 16 bit codes. */
static void gen_optimal_table(HTable *t, const uint32_t freq_in[257])
{
    uint32_t freq[257];
    int codesize[257];
    int others[257];
    unsigned bits[MAX_CLEN + 1];

    __builtin_memcpy(freq, freq_in, sizeof freq);
    freq[256] = 1; /* a code that is never used keeps the all ones code free. */
    __builtin_memset(codesize, 0, sizeof codesize);
    __builtin_memset(bits, 0, sizeof bits);
    for (int i = 0; i < 257; i++)
        others[i] = -1;

    for (;;) {
        int c1 = -1, c2 = -1;
        uint32_t v = 0xffffffffu;
        for (int i = 0; i <= 256; i++) {
            if (freq[i] && freq[i] <= v) {
                v = freq[i];
                c1 = i;
            }
        }
        v = 0xffffffffu;
        for (int i = 0; i <= 256; i++) {
            if (freq[i] && freq[i] <= v && i != c1) {
                v = freq[i];
                c2 = i;
            }
        }
        if (c2 < 0)
            break;
        freq[c1] += freq[c2];
        freq[c2] = 0;
        codesize[c1]++;
        while (others[c1] >= 0) {
            c1 = others[c1];
            codesize[c1]++;
        }
        others[c1] = c2;
        codesize[c2]++;
        while (others[c2] >= 0) {
            c2 = others[c2];
            codesize[c2]++;
        }
    }
    for (int i = 0; i <= 256; i++)
        if (codesize[i] > 0)
            bits[codesize[i] <= MAX_CLEN ? codesize[i] : MAX_CLEN]++;

    /* codes longer than 16 bits are folded into shorter ones. */
    int i;
    for (i = MAX_CLEN; i > 16; i--) {
        while (bits[i] > 0) {
            int j = i - 2;
            while (bits[j] == 0)
                j--;
            bits[i] -= 2;
            bits[i - 1]++;
            bits[j + 1] += 2;
            bits[j]--;
        }
    }
    while (bits[i] == 0)
        i--;
    bits[i]--; /* the code of the reserved symbol. */

    t->bits[0] = 0;
    for (int l = 1; l <= 16; l++)
        t->bits[l] = (uint8_t)bits[l];
    unsigned p = 0;
    for (int l = 1; l <= MAX_CLEN; l++)
        for (int j = 0; j < 256; j++)
            if (codesize[j] == l)
                t->vals[p++] = (uint8_t)j;
    t->nvals = (uint16_t)p;
    derive_table(t);
}

static void out_dht(Out *o, const HTable *t, unsigned tc_th)
{
    out_marker(o, 0xC4);
    out_put16(o, 2 + 1 + 16 + t->nvals);
    out_put8(o, tc_th);
    for (int i = 1; i <= 16; i++)
        out_put8(o, t->bits[i]);
    out_putmem(o, t->vals, t->nvals);
}

/* ---- bit writer ---- */

static void flush32_slow(Enc *e, uint32_t w)
{
    uint8_t *p = e->out.buf + e->out.len;
    for (int sh = 24; sh >= 0; sh -= 8) {
        uint8_t b = (uint8_t)(w >> sh);
        *p++ = b;
        if (b == 0xFF)
            *p++ = 0;
    }
    e->out.len = (size_t)(p - e->out.buf);
}

/* code must have no bits above size. nothing is written when gathering. */
static inline void put_bits(Enc *e, uint32_t code, int size)
{
    if (e->gather || size == 0)
        return;
    e->acc = (e->acc << size) | code;
    e->nacc += size;
    if (e->nacc >= 32) {
        uint32_t w = (uint32_t)(e->acc >> (e->nacc - 32));
        e->nacc -= 32;
        if (__builtin_expect(((~w - 0x01010101u) & w & 0x80808080u) == 0, 1)) {
            uint32_t be = __builtin_bswap32(w);
            __builtin_memcpy(e->out.buf + e->out.len, &be, 4);
            e->out.len += 4;
        } else {
            flush32_slow(e, w);
        }
    }
}

/* pads to a byte boundary with ones and writes everything out. */
static void flush_bits(Enc *e)
{
    if (e->gather)
        return;
    int pad = (8 - (e->nacc & 7)) & 7;
    put_bits(e, (1u << pad) - 1, pad);
    while (e->nacc >= 8) {
        unsigned b = (unsigned)(e->acc >> (e->nacc - 8)) & 0xFF;
        e->nacc -= 8;
        e->out.buf[e->out.len++] = (uint8_t)b;
        if (b == 0xFF)
            e->out.buf[e->out.len++] = 0;
    }
    e->acc = 0;
    e->nacc = 0;
}

static inline void emit_symbol(Enc *e, const HTable *t, uint32_t *freq, unsigned sym)
{
    if (e->gather) {
        freq[sym]++;
        return;
    }
    if (__builtin_expect(t->size[sym] == 0, 0)) {
        e->out.fail = true;
        return;
    }
    put_bits(e, t->code[sym], t->size[sym]);
}

/* the symbol and the n low bits of val together. */
static inline void emit_sym_bits(Enc *e, const HTable *t, uint32_t *freq, unsigned sym, uint32_t val, int n)
{
    if (e->gather) {
        freq[sym]++;
        return;
    }
    unsigned sz = t->size[sym];
    if (__builtin_expect(sz == 0, 0)) {
        e->out.fail = true;
        return;
    }
    put_bits(e, ((uint32_t)t->code[sym] << n) | (val & ((1u << n) - 1)), (int)sz + n);
}

static inline int nbits_of(uint32_t v)
{
    return v ? 32 - __builtin_clz(v) : 0;
}

/* ---- baseline blocks ---- */

static void encode_block_baseline(Enc *e, EncComp *c, const int16_t *blk)
{
    const HTable *dct = &e->dc[c->dc_tbl], *act = &e->ac[c->ac_tbl];
    uint32_t *fdc = e->freq_dc[c->dc_tbl], *fac = e->freq_ac[c->ac_tbl];
    int32_t diff = (int32_t)((uint32_t)blk[0] - c->last_dc);
    c->last_dc = (uint32_t)(int32_t)blk[0];
    uint32_t mag = (uint32_t)(diff < 0 ? -diff : diff);
    int nb = nbits_of(mag);
    if (nb > 11) {
        e->out.fail = true;
        return;
    }
    emit_sym_bits(e, dct, fdc, (unsigned)nb, (uint32_t)(diff < 0 ? diff - 1 : diff), nb);

    /* the nonzero ac coefficients in zigzag order. */
    int16_t z[64];
    uint64_t nz = 0;
    for (int k = 1; k < 64; k++) {
        int16_t v = blk[mime_jpeg_natural_order[k]];
        z[k] = v;
        nz |= (uint64_t)(v != 0) << k;
    }
    int prev = 0;
    while (nz) {
        int k = __builtin_ctzll(nz);
        nz &= nz - 1;
        int run = k - prev - 1;
        while (run > 15) {
            emit_symbol(e, act, fac, 0xF0);
            run -= 16;
        }
        int32_t v = z[k];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        int n = 32 - __builtin_clz(a);
        if (n > 14) {
            e->out.fail = true;
            return;
        }
        emit_sym_bits(e, act, fac, (unsigned)((run << 4) | n), (uint32_t)(v < 0 ? v - 1 : v), n);
        prev = k;
    }
    if (prev < 63)
        emit_symbol(e, act, fac, 0);
}

/* ---- progressive blocks ---- */

static void emit_buffered_bits(Enc *e, const uint8_t *bits, uint32_t n)
{
    if (e->gather)
        return;
    for (uint32_t i = 0; i < n; i++)
        put_bits(e, bits[i], 1);
}

static void emit_eobrun(Enc *e, EncComp *c)
{
    if (e->eobrun == 0)
        return;
    uint32_t t = e->eobrun;
    int nb = 0;
    while ((t >>= 1))
        nb++;
    emit_sym_bits(e, &e->ac[c->ac_tbl], e->freq_ac[c->ac_tbl], (unsigned)(nb << 4), e->eobrun, nb);
    e->eobrun = 0;
    emit_buffered_bits(e, e->corr, e->be);
    e->be = 0;
}

static void encode_dc_first(Enc *e, EncComp *c, const int16_t *blk)
{
    int32_t v = blk[0] >> e->al;
    int32_t diff = (int32_t)((uint32_t)v - c->last_dc);
    c->last_dc = (uint32_t)v;
    uint32_t mag = (uint32_t)(diff < 0 ? -diff : diff);
    int nb = nbits_of(mag);
    if (nb > 11) {
        e->out.fail = true;
        return;
    }
    emit_sym_bits(e, &e->dc[c->dc_tbl], e->freq_dc[c->dc_tbl], (unsigned)nb, (uint32_t)(diff < 0 ? diff - 1 : diff), nb);
}

static void encode_ac_first(Enc *e, EncComp *c, const int16_t *blk)
{
    const HTable *t = &e->ac[c->ac_tbl];
    uint32_t *freq = e->freq_ac[c->ac_tbl];
    int r = 0;
    for (int k = e->ss; k <= e->se; k++) {
        int32_t v = blk[mime_jpeg_natural_order[k]];
        if (v == 0) {
            r++;
            continue;
        }
        uint32_t a;
        uint32_t bitsval;
        if (v < 0) {
            a = (uint32_t)(-v) >> e->al;
            bitsval = ~a;
        } else {
            a = (uint32_t)v >> e->al;
            bitsval = a;
        }
        if (a == 0) {
            r++;
            continue;
        }
        if (e->eobrun > 0)
            emit_eobrun(e, c);
        while (r > 15) {
            emit_symbol(e, t, freq, 0xF0);
            r -= 16;
        }
        int nb = nbits_of(a);
        if (nb > 14) {
            e->out.fail = true;
            return;
        }
        emit_sym_bits(e, t, freq, (unsigned)((r << 4) + nb), bitsval, nb);
        r = 0;
    }
    if (r > 0) {
        e->eobrun++;
        if (e->eobrun == 0x7FFF)
            emit_eobrun(e, c);
    }
}

static void encode_dc_refine(Enc *e, const int16_t *blk)
{
    put_bits(e, (uint32_t)(blk[0] >> e->al) & 1, 1);
}

static void encode_ac_refine(Enc *e, EncComp *c, const int16_t *blk)
{
    const HTable *t = &e->ac[c->ac_tbl];
    uint32_t *freq = e->freq_ac[c->ac_tbl];
    uint32_t absv[64];
    int eob = 0;

    for (int k = e->ss; k <= e->se; k++) {
        int32_t v = blk[mime_jpeg_natural_order[k]];
        uint32_t a = (uint32_t)(v < 0 ? -v : v) >> e->al;
        absv[k] = a;
        if (a == 1)
            eob = k;
    }
    int r = 0;
    uint32_t br = 0;
    uint8_t *brbuf = e->corr + e->be;
    for (int k = e->ss; k <= e->se; k++) {
        uint32_t a = absv[k];
        if (a == 0) {
            r++;
            continue;
        }
        while (r > 15 && k <= eob) {
            emit_eobrun(e, c);
            emit_symbol(e, t, freq, 0xF0);
            r -= 16;
            emit_buffered_bits(e, brbuf, br);
            brbuf = e->corr;
            br = 0;
        }
        if (a > 1) {
            brbuf[br++] = (uint8_t)(a & 1);
            continue;
        }
        emit_eobrun(e, c);
        emit_sym_bits(e, t, freq, (unsigned)((r << 4) + 1), blk[mime_jpeg_natural_order[k]] < 0 ? 0 : 1, 1);
        emit_buffered_bits(e, brbuf, br);
        brbuf = e->corr;
        br = 0;
        r = 0;
    }
    if (r > 0 || br > 0) {
        e->eobrun++;
        e->be += br;
        if (e->eobrun == 0x7FFF || e->be > MAX_CORR_BITS - 64 + 1)
            emit_eobrun(e, c);
    }
}

/* ---- color conversion ---- */

#define FIX_Y_R 19595
#define FIX_Y_G 38470
#define FIX_Y_B 7471
#define FIX_CB_R 11059
#define FIX_CB_G 21709
#define FIX_CR_G 27439
#define FIX_CR_B 5329
#define CBCR_OFFSET (128 << 16)

static inline void rgb_ycc(int r, int g, int b, uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    *y = (uint8_t)((FIX_Y_R * r + FIX_Y_G * g + FIX_Y_B * b + 32768) >> 16);
    *cb = (uint8_t)((-FIX_CB_R * r - FIX_CB_G * g + 32768 * b + CBCR_OFFSET + 32767) >> 16);
    *cr = (uint8_t)((32768 * r - FIX_CR_G * g - FIX_CR_B * b + CBCR_OFFSET + 32767) >> 16);
}

#ifdef MIME_JPEG_SSE2

/* four pixels, b g r in the low three bytes of each lane, to the three transforms before the final shift. */
static inline void ycc4(__m128i px, __m128i *y, __m128i *cb, __m128i *cr)
{
    const __m128i m = _mm_set1_epi32(0x00ff00ff);
    const __m128i lo16 = _mm_set1_epi32(0xffff);
    __m128i m1 = _mm_and_si128(px, m);                       /* b, r. */
    __m128i m2 = _mm_and_si128(_mm_srli_epi32(px, 8), m);    /* g, ignored. */
    __m128i g2 = _mm_madd_epi16(m2, _mm_set1_epi32(19235));
    __m128i b15 = _mm_slli_epi32(_mm_and_si128(m1, lo16), 15);
    __m128i r15 = _mm_slli_epi32(_mm_srli_epi32(m1, 16), 15);
    const __m128i off = _mm_set1_epi32(CBCR_OFFSET + 32767);
    *y = _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(m1, _mm_set1_epi32((FIX_Y_R << 16) | FIX_Y_B)),
                                                    _mm_add_epi32(g2, g2)),
                                      _mm_set1_epi32(32768)),
                        16);
    *cb = _mm_srli_epi32(
        _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(m1, _mm_set1_epi32((int)((uint32_t)(uint16_t)-FIX_CB_R << 16))),
                                    _mm_madd_epi16(m2, _mm_set1_epi32((int)(uint16_t)-FIX_CB_G))),
                      _mm_add_epi32(b15, off)),
        16);
    *cr = _mm_srli_epi32(
        _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(m1, _mm_set1_epi32((int)(uint16_t)-FIX_CR_B)),
                                    _mm_madd_epi16(m2, _mm_set1_epi32((int)(uint16_t)-FIX_CR_G))),
                      _mm_add_epi32(r15, off)),
        16);
}

/* converts n pixels of b, g, r with the given pixel size, returns how many were done. */
static uint32_t convert_ycc_sse2(const uint8_t *src, int bpp, uint8_t *y, uint8_t *cb, uint8_t *cr, uint32_t n)
{
    uint32_t x = 0;
    for (; x + 16 + (bpp == 3 ? 2 : 0) <= n; x += 16) {
        __m128i py[4], pcb[4], pcr[4];
        for (int g = 0; g < 4; g++) {
            __m128i px;
            if (bpp == 4) {
                px = _mm_loadu_si128((const __m128i *)(src + 4 * (size_t)(x + 4 * g)));
            } else {
                const uint8_t *p = src + 3 * (size_t)(x + 4 * g);
                uint32_t a, b, c, d;
                __builtin_memcpy(&a, p, 4);
                __builtin_memcpy(&b, p + 3, 4);
                __builtin_memcpy(&c, p + 6, 4);
                __builtin_memcpy(&d, p + 9, 4);
                px = _mm_unpacklo_epi64(_mm_unpacklo_epi32(_mm_cvtsi32_si128((int)a), _mm_cvtsi32_si128((int)b)),
                                        _mm_unpacklo_epi32(_mm_cvtsi32_si128((int)c), _mm_cvtsi32_si128((int)d)));
            }
            ycc4(px, &py[g], &pcb[g], &pcr[g]);
        }
        _mm_storeu_si128((__m128i *)(y + x), _mm_packus_epi16(_mm_packs_epi32(py[0], py[1]), _mm_packs_epi32(py[2], py[3])));
        _mm_storeu_si128((__m128i *)(cb + x), _mm_packus_epi16(_mm_packs_epi32(pcb[0], pcb[1]), _mm_packs_epi32(pcb[2], pcb[3])));
        _mm_storeu_si128((__m128i *)(cr + x), _mm_packus_epi16(_mm_packs_epi32(pcr[0], pcr[1]), _mm_packs_epi32(pcr[2], pcr[3])));
    }
    return x;
}

#endif

/* converts one input row into the full resolution component rows, width pixels. */
static void convert_row(const Enc *e, const uint8_t *src, uint8_t *const dst[], uint32_t n)
{
    const int bpp = e->format == MIME_JPEG_PIXEL_GRAY8 ? 1 : e->format == MIME_JPEG_PIXEL_BGR24 ? 3 : 4;

    if (e->format == MIME_JPEG_PIXEL_GRAY8) {
        __builtin_memcpy(dst[0], src, n);
        return;
    }
    if (e->format == MIME_JPEG_PIXEL_CMYK32) {
        for (uint32_t x = 0; x < n; x++) {
            int c = src[4 * x], m = src[4 * x + 1], yy = src[4 * x + 2], k = src[4 * x + 3];
            if (e->color == MIME_JPEG_CS_YCCK) {
                rgb_ycc(c, m, yy, &dst[0][x], &dst[1][x], &dst[2][x]);
            } else {
                dst[0][x] = (uint8_t)(255 - c);
                dst[1][x] = (uint8_t)(255 - m);
                dst[2][x] = (uint8_t)(255 - yy);
            }
            dst[3][x] = (uint8_t)(255 - k);
        }
        return;
    }
    if (e->ncomp == 1) {
        for (uint32_t x = 0; x < n; x++)
            dst[0][x] = (uint8_t)((FIX_Y_R * src[bpp * x + 2] + FIX_Y_G * src[bpp * x + 1] + FIX_Y_B * src[bpp * x] +
                                   32768) >> 16);
        return;
    }
    if (e->color == MIME_JPEG_CS_RGB) {
        for (uint32_t x = 0; x < n; x++) {
            dst[0][x] = src[bpp * x + 2];
            dst[1][x] = src[bpp * x + 1];
            dst[2][x] = src[bpp * x];
        }
        return;
    }
    uint32_t x = 0;
#ifdef MIME_JPEG_SSE2
    x = convert_ycc_sse2(src, bpp, dst[0], dst[1], dst[2], n);
#endif
    for (; x < n; x++)
        rgb_ycc(src[bpp * x + 2], src[bpp * x + 1], src[bpp * x], &dst[0][x], &dst[1][x], &dst[2][x]);
}

/* ---- downsampling ---- */

/* in is the rows of the component, hexp and vexp the ratios, out the dsw samples of each of v * 8 rows. */
static void downsample(const EncComp *c, int hexp, int vexp, const uint8_t *in, uint32_t in_stride, uint8_t *out,
                       uint32_t rows)
{
    uint32_t cols = c->dsw;

    for (uint32_t r = 0; r < rows; r++) {
        const uint8_t *i0 = in + (size_t)r * vexp * in_stride;
        const uint8_t *i1 = i0 + in_stride;
        uint8_t *o = out + (size_t)r * c->dss;
        if (hexp == 1 && vexp == 1) {
            __builtin_memcpy(o, i0, cols);
        } else if (hexp == 2 && vexp == 1) {
            uint32_t x = 0;
#ifdef MIME_JPEG_SSE2
            const __m128i mask = _mm_set1_epi16(0x00ff);
            const __m128i bias = _mm_set1_epi32(0x00010000);
            for (; x + 8 <= cols; x += 8) {
                __m128i a = _mm_loadu_si128((const __m128i *)(i0 + 2 * x));
                __m128i s = _mm_add_epi16(_mm_add_epi16(_mm_and_si128(a, mask), _mm_srli_epi16(a, 8)), bias);
                s = _mm_srli_epi16(s, 1);
                _mm_storel_epi64((__m128i *)(o + x), _mm_packus_epi16(s, s));
            }
#endif
            for (; x < cols; x++)
                o[x] = (uint8_t)((i0[2 * x] + i0[2 * x + 1] + (x & 1)) >> 1);
        } else if (hexp == 2 && vexp == 2) {
            uint32_t x = 0;
#ifdef MIME_JPEG_SSE2
            const __m128i mask = _mm_set1_epi16(0x00ff);
            const __m128i bias = _mm_set1_epi32(0x00020001);
            for (; x + 8 <= cols; x += 8) {
                __m128i a = _mm_loadu_si128((const __m128i *)(i0 + 2 * x));
                __m128i b = _mm_loadu_si128((const __m128i *)(i1 + 2 * x));
                __m128i s = _mm_add_epi16(_mm_add_epi16(_mm_and_si128(a, mask), _mm_srli_epi16(a, 8)),
                                          _mm_add_epi16(_mm_and_si128(b, mask), _mm_srli_epi16(b, 8)));
                s = _mm_srli_epi16(_mm_add_epi16(s, bias), 2);
                _mm_storel_epi64((__m128i *)(o + x), _mm_packus_epi16(s, s));
            }
#endif
            for (; x < cols; x++)
                o[x] = (uint8_t)((i0[2 * x] + i0[2 * x + 1] + i1[2 * x] + i1[2 * x + 1] + 1 + (x & 1)) >> 2);
        } else if (hexp == 1 && vexp == 2) {
            for (uint32_t x = 0; x < cols; x++)
                o[x] = (uint8_t)((i0[x] + i1[x] + 1) >> 1);
        } else {
            unsigned numpix = (unsigned)(hexp * vexp);
            for (uint32_t x = 0; x < cols; x++) {
                unsigned sum = 0;
                for (int vy = 0; vy < vexp; vy++)
                    for (int hx = 0; hx < hexp; hx++)
                        sum += i0[(size_t)vy * in_stride + (size_t)x * hexp + hx];
                o[x] = (uint8_t)((sum + numpix / 2) / numpix);
            }
        }
    }
}

/* ---- transform of one row of MCUs ---- */

/* fills the quantized blocks of MCU row j into dest, one array per component. */
static void transform_mcu_row(Enc *e, uint32_t j, int16_t *const dest[])
{
    const uint32_t mrows = (uint32_t)e->maxv * 8;
    uint8_t *planes[JPEG_MAX_COMPS];

    /* the last rows of the image are padded to whole groups of rows before downsampling, the rest of the
     * block rows repeats the last downsampled row. */
    uint32_t real = e->height - j * mrows;
    if (real > mrows)
        real = mrows;
    uint32_t group_rows = (real + e->maxv - 1) / e->maxv * e->maxv;
    for (int i = 0; i < e->ncomp; i++)
        planes[i] = e->comp[i].full;
    for (uint32_t r = 0; r < group_rows; r++) {
        uint32_t y = j * mrows + r;
        if (y >= e->height)
            y = e->height - 1;
        uint8_t *rows[JPEG_MAX_COMPS];
        for (int i = 0; i < e->ncomp; i++)
            rows[i] = planes[i] + (size_t)r * e->full_stride;
        convert_row(e, e->pixels + (size_t)y * e->stride, rows, e->width);
        for (int i = 0; i < e->ncomp; i++)
            if (e->full_w > e->width)
                __builtin_memset(rows[i] + e->width, rows[i][e->width - 1], e->full_w - e->width);
    }
    for (int i = 0; i < e->ncomp; i++) {
        EncComp *c = &e->comp[i];
        const MimeJpegQuantizer *qz = &e->qz[c->tq];
        uint32_t ds_rows = group_rows / (e->maxv / c->v);
        downsample(c, e->maxh / c->h, e->maxv / c->v, c->full, e->full_stride, c->ds, ds_rows);
        for (uint32_t r = ds_rows; r < c->v * 8u; r++)
            __builtin_memcpy(c->ds + (size_t)r * c->dss, c->ds + (size_t)(ds_rows - 1) * c->dss, c->dsw);
        for (uint32_t by = 0; by < c->v; by++) {
            uint32_t gb = j * c->v + by;
            int16_t *row = dest[i] + (size_t)by * c->pitch * 64;
            if (gb < c->hib) {
                const uint8_t *src = c->ds + (size_t)by * 8 * c->dss;
                for (uint32_t bx = 0; bx < c->wib; bx++)
                    mime_jpeg_fdct_quant(src + bx * 8, c->dss, qz, row + (size_t)bx * 64);
                /* blocks right of the image repeat the dc of the last one. */
                for (uint32_t bx = c->wib; bx < c->pitch; bx++) {
                    __builtin_memset(row + (size_t)bx * 64, 0, 128);
                    row[(size_t)bx * 64] = row[(size_t)(c->wib - 1) * 64];
                }
            } else {
                for (uint32_t bx = 0; bx < c->pitch; bx++) {
                    int16_t *blk = row + (size_t)bx * 64;
                    __builtin_memset(blk, 0, 128);
                    /* below the image the dc is that of the last block of the row above in the same MCU. */
                    uint32_t mx = bx / c->h;
                    blk[0] = (row - (size_t)c->pitch * 64 + (size_t)(mx * c->h + c->h - 1) * 64)[0];
                }
            }
        }
    }
}

/* ---- scans ---- */

typedef struct Scan {
    int ncomp;
    int comp[JPEG_MAX_COMPS];
    int ss, se, ah, al;
} Scan;

static void start_scan_state(Enc *e, const Scan *s)
{
    e->ss = s->ss;
    e->se = s->se;
    e->ah = s->ah;
    e->al = s->al;
    e->eobrun = 0;
    e->be = 0;
    e->acc = 0;
    e->nacc = 0;
    e->rst_next = 0;
    for (int i = 0; i < e->ncomp; i++)
        e->comp[i].last_dc = 0;
}

static void restart_boundary(Enc *e, const Scan *s)
{
    if (e->progressive && s->ss > 0 && e->eobrun > 0)
        emit_eobrun(e, &e->comp[s->comp[0]]);
    if (!e->gather) {
        flush_bits(e);
        out_marker(&e->out, 0xD0 + e->rst_next);
        e->rst_next = (e->rst_next + 1) & 7;
    }
    for (int i = 0; i < e->ncomp; i++)
        e->comp[i].last_dc = 0;
    e->eobrun = 0;
    e->be = 0;
}

static inline void encode_block(Enc *e, const Scan *s, EncComp *c, const int16_t *blk)
{
    if (!e->progressive)
        encode_block_baseline(e, c, blk);
    else if (s->ss == 0)
        s->ah == 0 ? encode_dc_first(e, c, blk) : encode_dc_refine(e, blk);
    else
        s->ah == 0 ? encode_ac_first(e, c, blk) : encode_ac_refine(e, c, blk);
}

/* encodes the MCUs of one row of the scan taken from rows, which hold one MCU row or the whole image. */
static void encode_scan_row(Enc *e, const Scan *s, uint32_t my, int16_t *const rows[], uint32_t *mcu_count)
{
    uint32_t cols = s->ncomp > 1 ? e->mcus_x : e->comp[s->comp[0]].wib;
    for (uint32_t mx = 0; mx < cols; mx++) {
        if (e->restart_interval) {
            if (*mcu_count == e->restart_interval) {
                restart_boundary(e, s);
                *mcu_count = 0;
            }
            (*mcu_count)++;
        }
        if (!e->gather && !out_reserve(&e->out, 16384))
            return;
        for (int i = 0; i < s->ncomp; i++) {
            EncComp *c = &e->comp[s->comp[i]];
            if (s->ncomp == 1) {
                encode_block(e, s, c, rows[i] + ((size_t)my * c->pitch + mx) * 64);
            } else {
                for (uint32_t vy = 0; vy < c->v; vy++)
                    for (uint32_t hx = 0; hx < c->h; hx++)
                        encode_block(e, s, c, rows[i] + ((size_t)vy * c->pitch + (size_t)mx * c->h + hx) * 64);
            }
        }
    }
}

static void write_sos(Enc *e, const Scan *s)
{
    Out *o = &e->out;
    out_marker(o, 0xDA);
    out_put16(o, 6 + 2 * (unsigned)s->ncomp);
    out_put8(o, (unsigned)s->ncomp);
    for (int i = 0; i < s->ncomp; i++) {
        EncComp *c = &e->comp[s->comp[i]];
        unsigned td = c->dc_tbl, ta = c->ac_tbl;
        if (e->progressive) {
            if (s->ss == 0) {
                ta = 0;
                if (s->ah != 0)
                    td = 0;
            } else {
                td = 0;
            }
        }
        out_put8(o, c->id);
        out_put8(o, td << 4 | ta);
    }
    out_put8(o, (unsigned)s->ss);
    out_put8(o, (unsigned)s->se);
    out_put8(o, (unsigned)(s->ah << 4 | s->al));
}

/* writes the tables the scan uses that have not been sent since they were last built. */
static void write_scan_tables(Enc *e, const Scan *s, bool sent_dc[4], bool sent_ac[4])
{
    for (int i = 0; i < s->ncomp; i++) {
        EncComp *c = &e->comp[s->comp[i]];
        if (s->ss == 0 && s->ah == 0 && !sent_dc[c->dc_tbl]) {
            out_dht(&e->out, &e->dc[c->dc_tbl], c->dc_tbl);
            sent_dc[c->dc_tbl] = true;
        }
        if (s->se != 0 && !sent_ac[c->ac_tbl]) {
            out_dht(&e->out, &e->ac[c->ac_tbl], 0x10 | c->ac_tbl);
            sent_ac[c->ac_tbl] = true;
        }
    }
}

/* builds the optimal tables of the scan from the gathered counts. */
static void build_scan_tables(Enc *e, const Scan *s)
{
    bool done_dc[4] = {false}, done_ac[4] = {false};
    for (int i = 0; i < s->ncomp; i++) {
        EncComp *c = &e->comp[s->comp[i]];
        if (s->ss == 0 && s->ah == 0 && !done_dc[c->dc_tbl]) {
            gen_optimal_table(&e->dc[c->dc_tbl], e->freq_dc[c->dc_tbl]);
            done_dc[c->dc_tbl] = true;
        }
        if (s->se != 0 && !done_ac[c->ac_tbl]) {
            gen_optimal_table(&e->ac[c->ac_tbl], e->freq_ac[c->ac_tbl]);
            done_ac[c->ac_tbl] = true;
        }
    }
}

static void finish_scan_entropy(Enc *e, const Scan *s)
{
    if (e->progressive && s->ss > 0)
        emit_eobrun(e, &e->comp[s->comp[0]]);
    flush_bits(e);
}

/* runs one scan over the stored coefficients, once to count and once to write when tables are optimized. */
static void run_stored_scan(Enc *e, const Scan *s, bool sent_dc[4], bool sent_ac[4], bool *dri_sent)
{
    int16_t *rows[JPEG_MAX_COMPS];
    uint32_t nrows = s->ncomp > 1 ? e->mcus_y : e->comp[s->comp[0]].hib;

    if (e->optimize) {
        for (int i = 0; i < s->ncomp; i++) {
            EncComp *c = &e->comp[s->comp[i]];
            if (s->ss == 0 && s->ah == 0)
                __builtin_memset(e->freq_dc[c->dc_tbl], 0, sizeof e->freq_dc[0]);
            if (s->se != 0)
                __builtin_memset(e->freq_ac[c->ac_tbl], 0, sizeof e->freq_ac[0]);
        }
        e->gather = true;
        start_scan_state(e, s);
        uint32_t cnt = 0;
        for (uint32_t my = 0; my < nrows; my++) {
            for (int i = 0; i < s->ncomp; i++) {
                EncComp *c = &e->comp[s->comp[i]];
                rows[i] = s->ncomp > 1 ? c->blocks + (size_t)my * c->v * c->pitch * 64 : c->blocks;
            }
            encode_scan_row(e, s, my, rows, &cnt);
        }
        finish_scan_entropy(e, s);
        e->gather = false;
        build_scan_tables(e, s);
        __builtin_memset(sent_dc, 0, 4);
        __builtin_memset(sent_ac, 0, 4);
    }
    write_scan_tables(e, s, sent_dc, sent_ac);
    if (e->restart_interval && !*dri_sent) {
        out_marker(&e->out, 0xDD);
        out_put16(&e->out, 4);
        out_put16(&e->out, e->restart_interval);
        *dri_sent = true;
    }
    write_sos(e, s);
    start_scan_state(e, s);
    uint32_t cnt = 0;
    for (uint32_t my = 0; my < nrows; my++) {
        for (int i = 0; i < s->ncomp; i++) {
            EncComp *c = &e->comp[s->comp[i]];
            rows[i] = s->ncomp > 1 ? c->blocks + (size_t)my * c->v * c->pitch * 64 : c->blocks;
        }
        encode_scan_row(e, s, my, rows, &cnt);
        if (e->out.fail)
            return;
    }
    finish_scan_entropy(e, s);
}

/* libjpeg's standard progression. */
static int make_script(const Enc *e, Scan *scans)
{
    int n = 0;
    int nc = e->ncomp;
#define ADD_AC(c, ss_, se_, ah_, al_) \
    scans[n++] = (Scan){.ncomp = 1, .comp = {(c)}, .ss = (ss_), .se = (se_), .ah = (ah_), .al = (al_)}
    Scan dc = {.ncomp = nc, .ss = 0, .se = 0};
    for (int i = 0; i < nc; i++)
        dc.comp[i] = i;
    if (nc == 3 && e->color == MIME_JPEG_CS_YCBCR) {
        scans[n] = dc;
        scans[n].ah = 0;
        scans[n++].al = 1;
        ADD_AC(0, 1, 5, 0, 2);
        ADD_AC(2, 1, 63, 0, 1);
        ADD_AC(1, 1, 63, 0, 1);
        ADD_AC(0, 6, 63, 0, 2);
        ADD_AC(0, 1, 63, 2, 1);
        scans[n] = dc;
        scans[n].ah = 1;
        scans[n++].al = 0;
        ADD_AC(2, 1, 63, 1, 0);
        ADD_AC(1, 1, 63, 1, 0);
        ADD_AC(0, 1, 63, 1, 0);
    } else {
        scans[n] = dc;
        scans[n].ah = 0;
        scans[n++].al = 1;
        for (int i = 0; i < nc; i++)
            ADD_AC(i, 1, 5, 0, 2);
        for (int i = 0; i < nc; i++)
            ADD_AC(i, 6, 63, 0, 2);
        for (int i = 0; i < nc; i++)
            ADD_AC(i, 1, 63, 2, 1);
        scans[n] = dc;
        scans[n].ah = 1;
        scans[n++].al = 0;
        for (int i = 0; i < nc; i++)
            ADD_AC(i, 1, 63, 1, 0);
    }
#undef ADD_AC
    return n;
}

/* ---- headers ---- */

static void write_qtable(Out *o, int idx, const uint16_t *nat)
{
    bool wide = false;
    for (int i = 0; i < 64; i++)
        if (nat[i] > 255)
            wide = true;
    out_marker(o, 0xDB);
    out_put16(o, 2 + 1 + (wide ? 128 : 64));
    out_put8(o, (unsigned)((wide ? 16 : 0) | idx));
    for (int i = 0; i < 64; i++) {
        unsigned v = nat[mime_jpeg_natural_order[i]];
        if (wide)
            out_put16(o, v);
        else
            out_put8(o, v);
    }
}

static bool qtable_wide(const uint16_t *nat)
{
    for (int i = 0; i < 64; i++)
        if (nat[i] > 255)
            return true;
    return false;
}

/* ---- driver ---- */

static int format_bpp(int f)
{
    switch (f) {
    case MIME_JPEG_PIXEL_GRAY8:
        return 1;
    case MIME_JPEG_PIXEL_BGR24:
        return 3;
    case MIME_JPEG_PIXEL_BGRA32:
    case MIME_JPEG_PIXEL_BGRX32:
    case MIME_JPEG_PIXEL_CMYK32:
        return 4;
    default:
        return 0;
    }
}

static void set_comp(Enc *e, int i, unsigned id, unsigned h, unsigned v, unsigned tq, unsigned tbl)
{
    EncComp *c = &e->comp[i];
    c->id = (uint8_t)id;
    c->h = (uint8_t)h;
    c->v = (uint8_t)v;
    c->tq = (uint8_t)tq;
    c->dc_tbl = c->ac_tbl = (uint8_t)tbl;
}

int mime_jpeg_encode(const void *pixels, uint32_t width, uint32_t height, size_t stride, int pixel_format,
                     const MimeJpegEncodeParams *params, const MimeAllocator *alloc, uint8_t **out, size_t *out_len)
{
    MimeJpegEncodeParams defaults = {0};
    int bpp = format_bpp(pixel_format);

    if (!pixels || !alloc || !out || !out_len || !bpp)
        return MIME_JPEG_ERR_PARAM;
    *out = nullptr;
    *out_len = 0;
    if (!params)
        params = &defaults;
    if (width == 0 || height == 0 || width > MIME_JPEG_MAX_DIMENSION || height > MIME_JPEG_MAX_DIMENSION)
        return MIME_JPEG_ERR_TOO_LARGE;
    if ((uint64_t)width * height > MIME_JPEG_MAX_PIXELS)
        return MIME_JPEG_ERR_TOO_LARGE;
    if (stride < (size_t)width * (size_t)bpp)
        return MIME_JPEG_ERR_PARAM;
    int quality = params->quality ? params->quality : 75;
    if (quality < 1 || quality > 100 || params->subsampling < 0 || params->subsampling > MIME_JPEG_SUBSAMPLE_411 ||
        params->restart_interval > 65535 || params->comment_size > 65533 || params->segment_count > 64)
        return MIME_JPEG_ERR_PARAM;
    if (params->comment_size && !params->comment)
        return MIME_JPEG_ERR_PARAM;
    for (uint32_t i = 0; i < params->segment_count; i++) {
        const MimeJpegAppSegment *s = &params->segments[i];
        if (!((s->marker >= 0xE0 && s->marker <= 0xEF) || s->marker == 0xFE) || s->size > 65533 ||
            (s->size && !s->data))
            return MIME_JPEG_ERR_PARAM;
    }

    Enc *e = mime_alloc(alloc, sizeof *e);
    if (!e)
        return MIME_JPEG_ERR_NOMEM;
    __builtin_memset(e, 0, sizeof *e);
    e->alloc = alloc;
    e->out.alloc = alloc;
    e->pixels = pixels;
    e->width = width;
    e->height = height;
    e->stride = stride;
    e->format = pixel_format;
    e->flags = params->flags;
    e->restart_interval = params->restart_interval;
    e->progressive = (params->flags & MIME_JPEG_ENC_PROGRESSIVE) != 0;
    e->optimize = e->progressive || (params->flags & MIME_JPEG_ENC_OPTIMIZE) != 0;
    e->full_buffer = e->optimize;

    /* components. */
    int sub = params->subsampling;
    unsigned lh = 2, lv = 2;
    switch (sub) {
    case MIME_JPEG_SUBSAMPLE_444:
        lh = 1, lv = 1;
        break;
    case MIME_JPEG_SUBSAMPLE_422:
        lh = 2, lv = 1;
        break;
    case MIME_JPEG_SUBSAMPLE_440:
        lh = 1, lv = 2;
        break;
    case MIME_JPEG_SUBSAMPLE_411:
        lh = 4, lv = 1;
        break;
    default:
        break;
    }
    bool gray_out = pixel_format == MIME_JPEG_PIXEL_GRAY8 || (params->flags & MIME_JPEG_ENC_GRAYSCALE) != 0;
    bool jfif = !(params->flags & MIME_JPEG_ENC_NO_JFIF);
    int adobe_transform = -1;
    if (pixel_format == MIME_JPEG_PIXEL_CMYK32) {
        if (params->flags & MIME_JPEG_ENC_YCCK) {
            e->color = MIME_JPEG_CS_YCCK;
            e->ncomp = 4;
            set_comp(e, 0, 1, lh, lv, 0, 0);
            set_comp(e, 1, 2, 1, 1, 1, 1);
            set_comp(e, 2, 3, 1, 1, 1, 1);
            set_comp(e, 3, 4, lh, lv, 0, 0);
            adobe_transform = 2;
        } else {
            e->color = MIME_JPEG_CS_CMYK;
            e->ncomp = 4;
            set_comp(e, 0, 'C', 1, 1, 0, 0);
            set_comp(e, 1, 'M', 1, 1, 0, 0);
            set_comp(e, 2, 'Y', 1, 1, 0, 0);
            set_comp(e, 3, 'K', 1, 1, 0, 0);
            adobe_transform = 0;
        }
        jfif = false;
    } else if (gray_out) {
        e->color = MIME_JPEG_CS_GRAY;
        e->ncomp = 1;
        set_comp(e, 0, 1, 1, 1, 0, 0);
    } else if (params->flags & MIME_JPEG_ENC_RGB) {
        e->color = MIME_JPEG_CS_RGB;
        e->ncomp = 3;
        set_comp(e, 0, 'R', 1, 1, 0, 0);
        set_comp(e, 1, 'G', 1, 1, 0, 0);
        set_comp(e, 2, 'B', 1, 1, 0, 0);
        adobe_transform = 0;
        jfif = false;
    } else {
        e->color = MIME_JPEG_CS_YCBCR;
        e->ncomp = 3;
        set_comp(e, 0, 1, lh, lv, 0, 0);
        set_comp(e, 1, 2, 1, 1, 1, 1);
        set_comp(e, 2, 3, 1, 1, 1, 1);
    }

    /* quantization tables. */
    {
        const uint16_t *src[2] = {params->qtable_luma, params->qtable_chroma};
        for (int t = 0; t < 2; t++) {
            if (!src[t]) {
                mime_jpeg_quality_table(quality, t == 1, e->qt[t]);
                if (params->flags & MIME_JPEG_ENC_BASELINE)
                    for (int i = 0; i < 64; i++)
                        if (e->qt[t][i] > 255)
                            e->qt[t][i] = 255;
                continue;
            }
            for (int i = 0; i < 64; i++) {
                unsigned v = src[t][i];
                if (v == 0) {
                    mime_free(alloc, e);
                    return MIME_JPEG_ERR_PARAM;
                }
                int pos = (params->flags & MIME_JPEG_ENC_ZIGZAG_QTABLE) ? mime_jpeg_natural_order[i] : i;
                e->qt[t][pos] = (uint16_t)v;
            }
        }
    }
    for (int i = 0; i < e->ncomp; i++) {
        e->qt_used[e->comp[i].tq] = true;
        if (e->comp[i].h > e->maxh)
            e->maxh = e->comp[i].h;
        if (e->comp[i].v > e->maxv)
            e->maxv = e->comp[i].v;
    }
    for (int t = 0; t < 2; t++)
        if (e->qt_used[t])
            mime_jpeg_make_quantizer(e->qt[t], &e->qz[t]);

    /* geometry. */
    e->mcus_x = (width + 8u * e->maxh - 1) / (8u * e->maxh);
    e->mcus_y = (height + 8u * e->maxv - 1) / (8u * e->maxv);
    e->full_w = e->mcus_x * e->maxh * 8;
    e->full_stride = ((e->full_w + 15) & ~15u) + 32;
    uint64_t coef_bytes = 0;
    int rc = MIME_JPEG_OK;
    for (int i = 0; i < e->ncomp; i++) {
        EncComp *c = &e->comp[i];
        c->wib = (width * c->h + 8u * e->maxh - 1) / (8u * e->maxh);
        c->hib = (height * c->v + 8u * e->maxv - 1) / (8u * e->maxv);
        c->pitch = e->mcus_x * c->h;
        c->dsw = c->wib * 8;
        c->dss = ((c->dsw + 15) & ~15u) + 16;
        coef_bytes += (uint64_t)c->pitch * (e->full_buffer ? e->mcus_y : 1) * c->v * 128;
    }
    if (coef_bytes > ((uint64_t)1 << 30)) {
        mime_free(alloc, e);
        return MIME_JPEG_ERR_TOO_LARGE;
    }
    for (int i = 0; i < e->ncomp; i++) {
        EncComp *c = &e->comp[i];
        c->full = enc_alloc(e, (uint64_t)e->full_stride * e->maxv * 8, false);
        c->ds = enc_alloc(e, (uint64_t)c->dss * c->v * 8, false);
        c->blocks = enc_alloc(e, (uint64_t)c->pitch * (e->full_buffer ? e->mcus_y : 1) * c->v * 128, false);
        if (!c->full || !c->ds || !c->blocks) {
            enc_release(e);
            mime_free(alloc, e);
            return MIME_JPEG_ERR_NOMEM;
        }
    }
    e->out.cap = 0;
    out_reserve(&e->out, 4096 + ((uint64_t)width * height >> 2 < (1u << 26) ? (size_t)((uint64_t)width * height >> 2) : (1u << 26)));

    /* headers. */
    Out *o = &e->out;
    out_marker(o, 0xD8);
    if (jfif) {
        out_marker(o, 0xE0);
        out_put16(o, 16);
        out_putmem(o, "JFIF", 5);
        out_put8(o, 1);
        out_put8(o, 1);
        out_put8(o, params->density_unit);
        out_put16(o, params->x_density ? params->x_density : 1);
        out_put16(o, params->y_density ? params->y_density : 1);
        out_put8(o, 0);
        out_put8(o, 0);
    } else if (adobe_transform >= 0) {
        out_marker(o, 0xEE);
        out_put16(o, 14);
        out_putmem(o, "Adobe", 5);
        out_put16(o, 100);
        out_put16(o, 0);
        out_put16(o, 0);
        out_put8(o, (unsigned)adobe_transform);
    }
    for (uint32_t i = 0; i < params->segment_count; i++) {
        out_marker(o, params->segments[i].marker);
        out_put16(o, params->segments[i].size + 2);
        out_putmem(o, params->segments[i].data, params->segments[i].size);
    }
    if (params->comment_size) {
        out_marker(o, 0xFE);
        out_put16(o, params->comment_size + 2);
        out_putmem(o, params->comment, params->comment_size);
    }
    bool wide = false;
    bool sent_q[2] = {false, false};
    for (int i = 0; i < e->ncomp; i++) {
        int tq = e->comp[i].tq;
        if (sent_q[tq])
            continue;
        sent_q[tq] = true;
        wide |= qtable_wide(e->qt[tq]);
        write_qtable(o, tq, e->qt[tq]);
    }
    out_marker(o, e->progressive ? 0xC2 : wide ? 0xC1 : 0xC0);
    out_put16(o, 8 + 3 * (unsigned)e->ncomp);
    out_put8(o, 8);
    out_put16(o, height);
    out_put16(o, width);
    out_put8(o, (unsigned)e->ncomp);
    for (int i = 0; i < e->ncomp; i++) {
        out_put8(o, e->comp[i].id);
        out_put8(o, (unsigned)(e->comp[i].h << 4 | e->comp[i].v));
        out_put8(o, e->comp[i].tq);
    }

    bool sent_dc[4] = {false}, sent_ac[4] = {false};
    bool dri_sent = false;
    if (!e->optimize) {
        set_std_table(&e->dc[0], mime_jpeg_std_dc_luma_bits, mime_jpeg_std_dc_luma_vals, 12);
        set_std_table(&e->ac[0], mime_jpeg_std_ac_luma_bits, mime_jpeg_std_ac_luma_vals, 162);
        set_std_table(&e->dc[1], mime_jpeg_std_dc_chroma_bits, mime_jpeg_std_dc_chroma_vals, 12);
        set_std_table(&e->ac[1], mime_jpeg_std_ac_chroma_bits, mime_jpeg_std_ac_chroma_vals, 162);
        Scan s = {.ncomp = e->ncomp, .ss = 0, .se = 63};
        for (int i = 0; i < e->ncomp; i++)
            s.comp[i] = i;
        write_scan_tables(e, &s, sent_dc, sent_ac);
        if (e->restart_interval) {
            out_marker(o, 0xDD);
            out_put16(o, 4);
            out_put16(o, e->restart_interval);
        }
        write_sos(e, &s);
        start_scan_state(e, &s);
        uint32_t cnt = 0;
        int16_t *rows[JPEG_MAX_COMPS];
        for (int i = 0; i < e->ncomp; i++)
            rows[i] = e->comp[i].blocks;
        for (uint32_t j = 0; j < e->mcus_y && !o->fail; j++) {
            transform_mcu_row(e, j, rows);
            encode_scan_row(e, &s, 0, rows, &cnt);
        }
        finish_scan_entropy(e, &s);
    } else {
        int16_t *rows[JPEG_MAX_COMPS];
        for (uint32_t j = 0; j < e->mcus_y; j++) {
            for (int i = 0; i < e->ncomp; i++)
                rows[i] = e->comp[i].blocks + (size_t)j * e->comp[i].v * e->comp[i].pitch * 64;
            transform_mcu_row(e, j, rows);
        }
        if (e->progressive) {
            Scan scans[32];
            int n = make_script(e, scans);
            for (int i = 0; i < n && !o->fail; i++)
                run_stored_scan(e, &scans[i], sent_dc, sent_ac, &dri_sent);
        } else {
            Scan s = {.ncomp = e->ncomp, .ss = 0, .se = 63};
            for (int i = 0; i < e->ncomp; i++)
                s.comp[i] = i;
            run_stored_scan(e, &s, sent_dc, sent_ac, &dri_sent);
        }
    }
    out_marker(o, 0xD9);
    if (o->fail) {
        rc = MIME_JPEG_ERR_NOMEM;
        mime_free(alloc, o->buf);
    } else {
        *out = o->buf;
        *out_len = o->len;
    }
    enc_release(e);
    mime_free(alloc, e);
    return rc;
}
