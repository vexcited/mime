#include "internal.h"

/*
 * the decoder reads the markers, then decodes the entropy coded scans. sequential files with
 * one interleaved scan stream: each row of MCUs is decoded into a small buffer of coefficients,
 * transformed into bands of samples and converted to pixels right away. progressive files and
 * sequential files with several scans keep the coefficients of the whole image and run the same
 * second half afterwards.
 */

#define FAST_BITS 10
#define COEF_LIMIT ((uint64_t)1 << 30)
#define MAX_SCANS 1000

/* fast table entry layout, see build_huff. */
#define E_BITS(e) ((e) & 31u)
#define E_VALUE 0x20u /* the value is in the upper half, or the symbol has no extra bits. */
#define E_RUN(e) (((e) >> 8) & 15u)
#define E_SIZE(e) (((e) >> 12) & 15u)

typedef struct Huff {
    uint32_t fast[1 << FAST_BITS];
    int32_t maxcode[18];
    int32_t valoff[17];
    uint8_t vals[256];
} Huff;

typedef struct Bits {
    const uint8_t *p;   /* next byte. */
    const uint8_t *lim; /* end of the entropy coded data, or just past the marker that ended it. */
    const uint8_t *end;
    uint64_t buf; /* valid bits at the top. */
    int cnt;
    int fake;   /* zero bits supplied at the end of the data, they are the last bits of buf. */
    int marker; /* marker that ended the data, 0 when none. */
    int bad;    /* a code that is in no table was met. */
} Bits;

typedef struct Comp {
    uint8_t id, h, v, tq;
    uint8_t dct; /* idct size of this component. */
    uint8_t hin, vin; /* sampling factors in units of the smallest idct size. */
    uint8_t mode; /* UP_* */
    uint8_t hexp, vexp;
    uint32_t wib, hib; /* blocks that carry image data. */
    uint32_t pitch;    /* blocks per row of the coefficient storage, a multiple of h. */
    uint32_t dw, dh;   /* size in samples after scaling. */
    uint32_t pstride;
    uint32_t bandrows;
    uint8_t *plane;
    uint8_t *tmp;
    int16_t *coef;   /* whole image, array mode. */
    int16_t *rowbuf; /* one row of MCUs, streaming. */
    int32_t cache_row;
    uint32_t last_dc;
    bool latched;
    uint16_t qt[64];
} Comp;

enum { UP_NONE, UP_H2V1_FANCY, UP_H2V1, UP_H1V2_FANCY, UP_H2V2_FANCY, UP_REPL, UP_NEAREST };
enum { K_SEQ, K_DC_FIRST, K_DC_REFINE, K_AC_FIRST, K_AC_REFINE };

typedef struct Dec {
    const MimeAllocator *alloc;
    const uint8_t *data;
    size_t size;
    size_t pos;
    int unread; /* marker found by the entropy decoder but not handled. */
    unsigned flags;
    bool tolerant;
    bool damaged;

    MimeJpegInfo info;
    bool have_sof;
    bool saw_scan;
    uint16_t restart_interval;
    uint16_t qt[4][64];
    bool qt_defined[4];
    Huff *huff; /* dc 0 to 3, then ac 0 to 3. NULL when only reading headers. */

    uint32_t mcus_x, mcus_y;
    uint8_t maxh, maxv;
    int dct;
    bool fancy;
    bool streaming;
    bool needs_ctx;
    Comp comp[JPEG_MAX_COMPS];

    /* current scan. */
    int ns;
    int sc[JPEG_MAX_COMPS];
    int tdc[JPEG_MAX_COMPS], tac[JPEG_MAX_COMPS];
    uint8_t sh[JPEG_MAX_COMPS], sv[JPEG_MAX_COMPS];
    int ss, se, ah, al;
    int kind;
    uint32_t scan_cols, scan_rows;
    uint32_t eobrun;
    bool insufficient;
    uint32_t restarts_left;
    int next_rst;
    Bits br;

    /* output. */
    uint8_t *out;
    size_t out_stride;
    size_t out_size;
    uint32_t out_w, out_h;
    int cs; /* MIME_JPEG_CS_ */
    bool adobe_invert;
    bool raw;

    void *allocs[24];
    int nallocs;
} Dec;

/* ---- allocation ---- */

static void *dec_alloc(Dec *d, uint64_t size, bool zero)
{
    if (size == 0)
        size = 1;
    if (size > (uint64_t)SIZE_MAX / 2 || d->nallocs >= 24)
        return nullptr;
    void *p = mime_alloc(d->alloc, (size_t)size);
    if (!p)
        return nullptr;
    d->allocs[d->nallocs++] = p;
    if (zero)
        __builtin_memset(p, 0, (size_t)size);
    return p;
}

static void dec_release(Dec *d)
{
    for (int i = d->nallocs - 1; i >= 0; i--)
        mime_free(d->alloc, d->allocs[i]);
    d->nallocs = 0;
}

/* ---- huffman tables ---- */

static bool build_huff(Huff *h, const uint8_t bits[16], const uint8_t *vals, unsigned nvals, bool dc)
{
    uint32_t code = 0;
    unsigned k = 0;

    __builtin_memset(h->fast, 0, sizeof h->fast);
    __builtin_memset(h->vals, 0, sizeof h->vals);
    __builtin_memcpy(h->vals, vals, nvals);
    h->maxcode[0] = -1;
    h->maxcode[17] = 0x7fffffff;
    h->valoff[0] = 0;
    for (unsigned l = 1; l <= 16; l++) {
        unsigned n = bits[l - 1];
        h->valoff[l] = (int32_t)k - (int32_t)code;
        for (unsigned i = 0; i < n; i++, k++, code++) {
            unsigned sym = h->vals[k];
            if (code >= (1u << l))
                return false;
            if (dc && sym > 15)
                return false;
            if (l > FAST_BITS)
                continue;
            unsigned run = sym >> 4, size = sym & 15;
            uint32_t base = code << (FAST_BITS - l);
            if (size == 0) {
                uint32_t e = E_VALUE | (run << 8) | l;
                for (uint32_t j = 0; j < (1u << (FAST_BITS - l)); j++)
                    h->fast[base + j] = e;
            } else if (l + size <= FAST_BITS) {
                unsigned spare = FAST_BITS - l - size;
                for (uint32_t ext = 0; ext < (1u << size); ext++) {
                    int32_t v = (ext >= (1u << (size - 1))) ? (int32_t)ext : (int32_t)ext - (int32_t)((1u << size) - 1);
                    uint32_t e = ((uint32_t)v << 16) | (size << 12) | (run << 8) | E_VALUE | (l + size);
                    uint32_t b2 = base | (ext << spare);
                    for (uint32_t j = 0; j < (1u << spare); j++)
                        h->fast[b2 + j] = e;
                }
            } else {
                uint32_t e = (size << 12) | (run << 8) | l;
                for (uint32_t j = 0; j < (1u << (FAST_BITS - l)); j++)
                    h->fast[base + j] = e;
            }
        }
        h->maxcode[l] = n ? (int32_t)code - 1 : -1;
        code <<= 1;
    }
    return true;
}

/* ---- bit reader ---- */

static inline uint64_t load_be64(const uint8_t *p)
{
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(v);
#else
    return v;
#endif
}

static void bits_fill_slow(Bits *b)
{
    b->buf = b->cnt ? b->buf & (~(uint64_t)0 << (64 - b->cnt)) : 0;
    while (b->cnt <= 56) {
        if (b->p >= b->lim) {
            b->fake += 8;
            b->cnt += 8;
            continue;
        }
        unsigned c = *b->p++;
        if (c == 0xFF) {
            while (b->p < b->lim && *b->p == 0xFF)
                b->p++;
            if (b->p >= b->lim) {
                /* the file ends in the middle of a marker. */
                b->lim = b->p;
                continue;
            }
            c = *b->p++;
            if (c != 0) {
                b->marker = (int)c;
                b->lim = b->p;
                continue;
            }
            c = 0xFF;
        }
        b->buf |= (uint64_t)c << (56 - b->cnt);
        b->cnt += 8;
    }
}

static inline void bits_fill(Bits *b)
{
    if (__builtin_expect(b->lim - b->p >= 8, 1)) {
        uint64_t v = load_be64(b->p);
        if (!(((~v) - 0x0101010101010101ull) & v & 0x8080808080808080ull)) {
            b->buf |= v >> b->cnt;
            int nb = (63 - b->cnt) >> 3;
            b->p += nb;
            b->cnt += nb * 8;
            return;
        }
    }
    bits_fill_slow(b);
}

#define NEED(b, n)            \
    do {                      \
        if ((b)->cnt < (n))   \
            bits_fill(b);     \
    } while (0)

static inline uint32_t bits_get(Bits *b, int n)
{
    uint32_t v = (uint32_t)(b->buf >> (64 - n));
    b->buf <<= n;
    b->cnt -= n;
    return v;
}

static inline int32_t extend(uint32_t v, int s)
{
    int32_t t = (int32_t)(v >> (s - 1)) - 1;
    return (int32_t)v - (t & (int32_t)((1u << s) - 1));
}

/* receives s bits, 1 to 15, as a signed value. */
static inline int32_t bits_receive(Bits *b, int s)
{
    return extend(bits_get(b, s), s);
}

/* decodes a code longer than the fast table covers. */
static int huff_slow(Bits *b, const Huff *h)
{
    uint32_t v = (uint32_t)(b->buf >> 48);
    for (int l = FAST_BITS + 1; l <= 16; l++) {
        int32_t code = (int32_t)(v >> (16 - l));
        if (code <= h->maxcode[l]) {
            b->buf <<= l;
            b->cnt -= l;
            return h->vals[(code + h->valoff[l]) & 255];
        }
    }
    /* a code that is in the table nowhere decodes as symbol 0, like libjpeg does. */
    b->buf <<= 17;
    b->cnt -= 17;
    b->bad = 1;
    return 0;
}

/* decodes a symbol and consumes it, with at least 16 bits available. */
static inline int huff_symbol(Bits *b, const Huff *h)
{
    uint32_t e = h->fast[b->buf >> (64 - FAST_BITS)];
    unsigned n = E_BITS(e);
    if (__builtin_expect(n != 0, 1)) {
        /* the entry may include extra bits, take only the code here. the caller does not use those. */
        unsigned sym_bits = n;
        if (e & E_VALUE) {
            unsigned size = E_SIZE(e);
            sym_bits = n - size;
        }
        b->buf <<= sym_bits;
        b->cnt -= (int)sym_bits;
        return (int)((E_RUN(e) << 4) | E_SIZE(e));
    }
    return huff_slow(b, h);
}

/* ---- block decoding ---- */

/* sequential block: dc difference and run length coded ac values, coefficients in natural order. */
static inline void decode_block_seq(Bits *b, const Huff *dc, const Huff *ac, uint32_t *last_dc, int16_t *blk)
{
    NEED(b, 32);
    uint32_t e = dc->fast[b->buf >> (64 - FAST_BITS)];
    int32_t diff;
    unsigned n = E_BITS(e);
    if (__builtin_expect(n != 0, 1)) {
        b->buf <<= n;
        b->cnt -= (int)n;
        if (e & E_VALUE) {
            diff = (int32_t)e >> 16;
        } else {
            int s = (int)E_SIZE(e);
            diff = bits_receive(b, s);
        }
    } else {
        int s = huff_slow(b, dc);
        diff = s ? bits_receive(b, s) : 0;
    }
    *last_dc += (uint32_t)diff;
    blk[0] = (int16_t)*last_dc;

    int k = 1;
    while (k < 64) {
        NEED(b, 32);
        e = ac->fast[b->buf >> (64 - FAST_BITS)];
        n = E_BITS(e);
        if (__builtin_expect(n != 0, 1)) {
            b->buf <<= n;
            b->cnt -= (int)n;
            unsigned run = E_RUN(e);
            unsigned size = E_SIZE(e);
            if (e & E_VALUE) {
                if (__builtin_expect(size != 0, 1)) {
                    k += (int)run;
                    blk[mime_jpeg_natural_order[k]] = (int16_t)((int32_t)e >> 16);
                    k++;
                    continue;
                }
                if (run != 15)
                    break;
                k += 16;
                continue;
            }
            k += (int)run;
            blk[mime_jpeg_natural_order[k]] = (int16_t)bits_receive(b, (int)size);
            k++;
        } else {
            int sym = huff_slow(b, ac);
            int run = sym >> 4, size = sym & 15;
            if (size) {
                k += run;
                blk[mime_jpeg_natural_order[k]] = (int16_t)bits_receive(b, size);
                k++;
            } else {
                if (run != 15)
                    break;
                k += 16;
            }
        }
    }
}

static inline void decode_block_dc_first(Bits *b, const Huff *dc, uint32_t *last_dc, int al, int16_t *blk)
{
    NEED(b, 32);
    int s = huff_symbol(b, dc);
    int32_t diff = s ? bits_receive(b, s) : 0;
    *last_dc += (uint32_t)diff;
    blk[0] = (int16_t)(*last_dc << al);
}

static inline void decode_block_ac_first(Dec *d, Bits *b, const Huff *ac, int16_t *blk)
{
    if (d->eobrun > 0) {
        d->eobrun--;
        return;
    }
    int al = d->al;
    for (int k = d->ss; k <= d->se; k++) {
        NEED(b, 32);
        int sym = huff_symbol(b, ac);
        int r = sym >> 4, s = sym & 15;
        if (s) {
            k += r;
            int32_t v = bits_receive(b, s);
            blk[mime_jpeg_natural_order[k]] = (int16_t)((uint32_t)v << al);
        } else if (r == 15) {
            k += 15;
        } else {
            d->eobrun = 1u << r;
            if (r)
                d->eobrun += bits_get(b, r);
            d->eobrun--;
            break;
        }
    }
}

static inline void refine_coef(Bits *b, int16_t *c, int p1, int m1)
{
    NEED(b, 1);
    if (bits_get(b, 1) && !(*c & p1))
        *c = (int16_t)(*c + (*c >= 0 ? p1 : m1));
}

static inline void decode_block_ac_refine(Dec *d, Bits *b, const Huff *ac, int16_t *blk)
{
    int p1 = 1 << d->al, m1 = -p1;
    int k = d->ss, se = d->se;
    if (d->eobrun == 0) {
        for (; k <= se; k++) {
            NEED(b, 32);
            int sym = huff_symbol(b, ac);
            int r = sym >> 4, s = sym & 15;
            int newval = 0;
            if (s) {
                if (s != 1)
                    b->bad = 1;
                newval = bits_get(b, 1) ? p1 : m1;
            } else if (r != 15) {
                d->eobrun = 1u << r;
                if (r)
                    d->eobrun += bits_get(b, r);
                break;
            }
            for (; k <= se; k++) {
                int16_t *c = &blk[mime_jpeg_natural_order[k]];
                if (*c) {
                    NEED(b, 1);
                    refine_coef(b, c, p1, m1);
                } else if (--r < 0) {
                    break;
                }
            }
            if (newval && k <= se)
                blk[mime_jpeg_natural_order[k]] = (int16_t)newval;
        }
    }
    if (d->eobrun > 0) {
        for (; k <= se; k++) {
            int16_t *c = &blk[mime_jpeg_natural_order[k]];
            if (*c)
                refine_coef(b, c, p1, m1);
        }
        d->eobrun--;
    }
}

/* ---- markers ---- */

/* finds the next marker at or after p, returns it or -1 and moves p behind it. */
static int find_marker(const uint8_t **pp, const uint8_t *end)
{
    const uint8_t *p = *pp;
    while (p + 1 < end) {
        if (p[0] == 0xFF) {
            unsigned c = p[1];
            if (c == 0xFF) {
                p++;
                continue;
            }
            if (c == 0) {
                p += 2;
                continue;
            }
            *pp = p + 2;
            return (int)c;
        }
        p++;
    }
    *pp = end;
    return -1;
}

static int next_marker(Dec *d)
{
    if (d->unread) {
        int m = d->unread;
        d->unread = 0;
        return m;
    }
    const uint8_t *p = d->data + d->pos;
    int m = find_marker(&p, d->data + d->size);
    d->pos = (size_t)(p - d->data);
    return m;
}

static inline unsigned be16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

enum { R_SOS = 1, R_EOI = 2 };

static int parse_sof(Dec *d, int marker, const uint8_t *p, size_t len)
{
    MimeJpegInfo *info = &d->info;
    if (d->have_sof)
        return MIME_JPEG_ERR_FORMAT;
    if (len < 6)
        return MIME_JPEG_ERR_FORMAT;
    unsigned prec = p[0];
    unsigned height = be16(p + 1);
    unsigned width = be16(p + 3);
    unsigned n = p[5];
    if (n == 0 || n > 4 || len < 6 + 3 * (size_t)n)
        return MIME_JPEG_ERR_FORMAT;
    if (prec != 8)
        return prec == 12 || prec == 16 ? MIME_JPEG_ERR_UNSUPPORTED : MIME_JPEG_ERR_FORMAT;
    if (width == 0)
        return MIME_JPEG_ERR_FORMAT;
    if (height == 0)
        return MIME_JPEG_ERR_UNSUPPORTED; /* the height would come in a DNL segment. */
    if (width > MIME_JPEG_MAX_DIMENSION || height > MIME_JPEG_MAX_DIMENSION ||
        (uint64_t)width * height > MIME_JPEG_MAX_PIXELS)
        return MIME_JPEG_ERR_TOO_LARGE;
    if (n == 2)
        return MIME_JPEG_ERR_UNSUPPORTED;
    info->width = width;
    info->height = height;
    info->precision = (uint8_t)prec;
    info->components = (uint8_t)n;
    info->progressive = marker == 0xC2;
    info->extended = marker == 0xC1;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *c = p + 6 + 3 * i;
        unsigned h = c[1] >> 4, v = c[1] & 15;
        if (h < 1 || h > 4 || v < 1 || v > 4 || c[2] > 3)
            return MIME_JPEG_ERR_FORMAT;
        info->comp[i].id = c[0];
        info->comp[i].h = (uint8_t)h;
        info->comp[i].v = (uint8_t)v;
        info->comp[i].tq = c[2];
    }
    d->have_sof = true;
    return 0;
}

static int parse_dqt(Dec *d, const uint8_t *p, size_t len)
{
    while (len > 0) {
        unsigned pq = p[0] >> 4, tq = p[0] & 15;
        size_t need = 1 + (pq ? 128 : 64);
        if (pq > 1 || tq > 3 || len < need)
            return MIME_JPEG_ERR_FORMAT;
        for (int i = 0; i < 64; i++) {
            unsigned v = pq ? be16(p + 1 + 2 * i) : p[1 + i];
            d->qt[tq][mime_jpeg_natural_order[i]] = (uint16_t)v;
        }
        d->qt_defined[tq] = true;
        p += need;
        len -= need;
    }
    return 0;
}

static int parse_dht(Dec *d, const uint8_t *p, size_t len)
{
    while (len > 0) {
        if (len < 17)
            return MIME_JPEG_ERR_FORMAT;
        unsigned tc = p[0] >> 4, th = p[0] & 15;
        unsigned total = 0;
        for (int i = 0; i < 16; i++)
            total += p[1 + i];
        if (tc > 1 || th > 3 || total > 256 || len < 17 + (size_t)total)
            return MIME_JPEG_ERR_FORMAT;
        if (!build_huff(&d->huff[tc * 4 + th], p + 1, p + 17, total, tc == 0))
            return MIME_JPEG_ERR_FORMAT;
        p += 17 + total;
        len -= 17 + total;
    }
    return 0;
}

static void parse_app(Dec *d, int marker, const uint8_t *p, size_t len)
{
    MimeJpegInfo *info = &d->info;
    if (marker == 0xE0 && len >= 14 && !__builtin_memcmp(p, "JFIF\0", 5)) {
        info->jfif = true;
        info->jfif_major = p[5];
        info->jfif_minor = p[6];
        info->density_unit = p[7];
        info->x_density = (uint16_t)be16(p + 8);
        info->y_density = (uint16_t)be16(p + 10);
        info->thumb_width = p[12];
        info->thumb_height = p[13];
    } else if (marker == 0xEE && len >= 12 && !__builtin_memcmp(p, "Adobe", 5)) {
        info->adobe = true;
        info->adobe_transform = p[11];
    }
}

static int parse_sos(Dec *d, const uint8_t *p, size_t len)
{
    if (!d->have_sof)
        return MIME_JPEG_ERR_FORMAT;
    if (len < 1)
        return MIME_JPEG_ERR_FORMAT;
    int ns = p[0];
    if (ns < 1 || ns > 4 || ns > d->info.components || len < 4 + 2 * (size_t)ns)
        return MIME_JPEG_ERR_FORMAT;
    uint32_t used = 0;
    int blocks = 0;
    for (int i = 0; i < ns; i++) {
        int found = -1;
        for (int c = 0; c < d->info.components; c++) {
            if (d->info.comp[c].id == p[1 + 2 * i] && !(used & (1u << c))) {
                found = c;
                break;
            }
        }
        unsigned t = p[2 + 2 * i];
        if (found < 0 || (t >> 4) > 3 || (t & 15) > 3)
            return MIME_JPEG_ERR_FORMAT;
        used |= 1u << found;
        d->sc[i] = found;
        d->tdc[i] = (int)(t >> 4);
        d->tac[i] = (int)(t & 15);
        blocks += d->info.comp[found].h * d->info.comp[found].v;
    }
    if (ns > 1 && blocks > 10)
        return MIME_JPEG_ERR_FORMAT;
    const uint8_t *q = p + 1 + 2 * ns;
    d->ns = ns;
    d->ss = q[0];
    d->se = q[1];
    d->ah = q[2] >> 4;
    d->al = q[2] & 15;
    if (d->info.progressive) {
        if (d->ss > 63 || d->se > 63 || d->ss > d->se || d->al > 13 || d->ah > 13)
            return MIME_JPEG_ERR_FORMAT;
        if (d->ss == 0) {
            if (d->se != 0)
                return MIME_JPEG_ERR_FORMAT;
            d->kind = d->ah ? K_DC_REFINE : K_DC_FIRST;
        } else {
            if (ns != 1)
                return MIME_JPEG_ERR_FORMAT;
            d->kind = d->ah ? K_AC_REFINE : K_AC_FIRST;
        }
    } else {
        d->ss = 0;
        d->se = 63;
        d->ah = d->al = 0;
        d->kind = K_SEQ;
    }
    return 0;
}

/*
 * reads markers until the next scan header or the end of the image. segments the decoder has
 * no use for are skipped. returns R_SOS, R_EOI or an error.
 */
static int parse_markers(Dec *d)
{
    for (;;) {
        int m = next_marker(d);
        if (m < 0)
            return d->have_sof ? R_EOI : MIME_JPEG_ERR_FORMAT;
        if (m == 0xD9)
            return R_EOI;
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7))
            continue;
        if (d->pos + 2 > d->size)
            return d->have_sof ? R_EOI : MIME_JPEG_ERR_FORMAT;
        size_t len = be16(d->data + d->pos);
        if (len < 2 || d->pos + len > d->size) {
            /* a segment that runs past the end of the file. */
            if (!d->tolerant && (m == 0xDA || m == 0xC0 || m == 0xC1 || m == 0xC2 || m == 0xC4 || m == 0xDB))
                return MIME_JPEG_ERR_FORMAT;
            return d->have_sof ? R_EOI : MIME_JPEG_ERR_FORMAT;
        }
        const uint8_t *p = d->data + d->pos + 2;
        size_t plen = len - 2;
        d->pos += len;
        int rc = 0;
        switch (m) {
        case 0xC0:
        case 0xC1:
        case 0xC2:
            rc = parse_sof(d, m, p, plen);
            break;
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            return MIME_JPEG_ERR_UNSUPPORTED;
        case 0xC4:
            if (d->huff)
                rc = parse_dht(d, p, plen);
            break;
        case 0xDB:
            if (d->huff)
                rc = parse_dqt(d, p, plen);
            break;
        case 0xDD:
            if (plen >= 2)
                d->restart_interval = (uint16_t)be16(p);
            else
                rc = MIME_JPEG_ERR_FORMAT;
            break;
        case 0xDA:
            rc = parse_sos(d, p, plen);
            if (rc == 0) {
                d->pos = (size_t)(p + plen - d->data);
                return R_SOS;
            }
            break;
        default:
            if (m >= 0xE0 && m <= 0xEF)
                parse_app(d, m, p, plen);
            break;
        }
        if (rc < 0)
            return rc;
    }
}

/* ---- frame geometry ---- */

static void decide_color(Dec *d)
{
    MimeJpegInfo *info = &d->info;
    d->adobe_invert = false;
    switch (info->components) {
    case 1:
        d->cs = MIME_JPEG_CS_GRAY;
        break;
    case 3:
        if (info->jfif)
            d->cs = MIME_JPEG_CS_YCBCR;
        else if (info->adobe)
            d->cs = info->adobe_transform == 0 ? MIME_JPEG_CS_RGB : MIME_JPEG_CS_YCBCR;
        else if (info->comp[0].id == 'R' && info->comp[1].id == 'G' && info->comp[2].id == 'B')
            d->cs = MIME_JPEG_CS_RGB;
        else
            d->cs = MIME_JPEG_CS_YCBCR;
        break;
    default:
        if (info->adobe) {
            d->cs = info->adobe_transform == 0 ? MIME_JPEG_CS_CMYK : MIME_JPEG_CS_YCCK;
            d->adobe_invert = true;
        } else {
            d->cs = MIME_JPEG_CS_CMYK;
        }
        break;
    }
    info->color_space = (uint8_t)d->cs;
}

static inline uint32_t ceil_div(uint32_t a, uint32_t b)
{
    return (a + b - 1) / b;
}

static int scale_dct(unsigned flags)
{
    switch (flags & MIME_JPEG_SCALE_MASK) {
    case MIME_JPEG_SCALE_HALF:
        return 4;
    case MIME_JPEG_SCALE_QUARTER:
        return 2;
    case MIME_JPEG_SCALE_EIGHTH:
        return 1;
    default:
        return 8;
    }
}

/* fills the output fields of the info and the geometry the decoder needs. */
static void setup_geometry(Dec *d)
{
    MimeJpegInfo *info = &d->info;
    d->dct = scale_dct(d->flags);
    d->maxh = d->maxv = 1;
    /* a single component is always coded in blocks of its own, whatever its factors say. */
    for (int i = 0; i < info->components && info->components > 1; i++) {
        if (info->comp[i].h > d->maxh)
            d->maxh = info->comp[i].h;
        if (info->comp[i].v > d->maxv)
            d->maxv = info->comp[i].v;
    }
    d->mcus_x = ceil_div(info->width, 8u * d->maxh);
    d->mcus_y = ceil_div(info->height, 8u * d->maxv);
    d->out_w = ceil_div(info->width * d->dct, 8);
    d->out_h = ceil_div(info->height * d->dct, 8);
    d->raw = (d->flags & MIME_JPEG_RAW_COLOR) != 0;
    info->out_width = d->out_w;
    info->out_height = d->out_h;
    info->out_format = (uint8_t)(info->components == 1 ? MIME_JPEG_FORMAT_GRAY8
                                 : info->components == 3 ? MIME_JPEG_FORMAT_BGR24
                                                         : MIME_JPEG_FORMAT_CMYK32);
    info->scale_denom = (uint8_t)(8 / d->dct);
    d->fancy = !(d->flags & MIME_JPEG_NO_FANCY) && d->dct > 1;
    for (int i = 0; i < info->components; i++) {
        Comp *c = &d->comp[i];
        c->id = info->comp[i].id;
        c->h = info->components > 1 ? info->comp[i].h : 1;
        c->v = info->components > 1 ? info->comp[i].v : 1;
        c->tq = info->comp[i].tq;
        /* chroma is transformed at a larger size where that avoids upsampling it afterwards. */
        unsigned ssize = (unsigned)d->dct;
        while (ssize < 8 && (d->maxh * d->dct) % (c->h * ssize * 2u) == 0 &&
               (d->maxv * d->dct) % (c->v * ssize * 2u) == 0)
            ssize *= 2;
        c->dct = (uint8_t)ssize;
        c->hin = (uint8_t)(c->h * ssize / (unsigned)d->dct);
        c->vin = (uint8_t)(c->v * ssize / (unsigned)d->dct);
        c->wib = ceil_div(info->width * c->h, 8u * d->maxh);
        c->hib = ceil_div(info->height * c->v, 8u * d->maxv);
        c->pitch = d->mcus_x * c->h;
        c->dw = ceil_div(info->width * c->h * ssize, 8u * d->maxh);
        c->dh = ceil_div(info->height * c->v * ssize, 8u * d->maxv);
        c->bandrows = (uint32_t)c->v * ssize;
        bool integral = (d->maxh % c->hin) == 0 && (d->maxv % c->vin) == 0;
        c->hexp = (uint8_t)(d->maxh / c->hin);
        c->vexp = (uint8_t)(d->maxv / c->vin);
        if (c->hexp == 1 && c->vexp == 1 && integral)
            c->mode = UP_NONE;
        else if (!integral)
            c->mode = UP_NEAREST;
        else if (c->hexp == 2 && c->vexp == 1)
            c->mode = d->fancy && c->dw > 2 ? UP_H2V1_FANCY : UP_H2V1;
        else if (c->hexp == 1 && c->vexp == 2 && d->fancy)
            c->mode = UP_H1V2_FANCY;
        else if (c->hexp == 2 && c->vexp == 2 && d->fancy && c->dw > 2)
            c->mode = UP_H2V2_FANCY;
        else
            c->mode = UP_REPL;
    }
}

static int check_output(const Dec *d, size_t out_stride, size_t out_size)
{
    size_t bpp = d->info.out_format;
    uint64_t row = (uint64_t)d->out_w * bpp;
    if (out_stride < row || (uint64_t)out_stride > SIZE_MAX / 4)
        return MIME_JPEG_ERR_PARAM;
    if ((uint64_t)(d->out_h - 1) * out_stride + row > out_size)
        return MIME_JPEG_ERR_PARAM;
    return 0;
}

/* ---- output stages ---- */

static inline uint8_t *comp_row(Comp *c, int64_t r)
{
    if (r < 0)
        r = 0;
    if (r >= (int64_t)c->dh)
        r = (int64_t)c->dh - 1;
    uint32_t band = (uint32_t)r / c->bandrows;
    uint32_t in_band = (uint32_t)r - band * c->bandrows;
    return c->plane + ((size_t)(band % 3) * c->bandrows + in_band) * c->pstride;
}

/* the row of component c that lines up with output row y, upsampled to the output width. */
static const uint8_t *up_row(Dec *d, Comp *c, uint32_t y)
{
    switch (c->mode) {
    case UP_NONE:
        return comp_row(c, y);
    case UP_H2V1_FANCY:
        mime_jpeg_up_h2v1_fancy(comp_row(c, y), c->tmp, c->dw);
        return c->tmp;
    case UP_H2V1:
        if ((int32_t)y == c->cache_row)
            return c->tmp;
        c->cache_row = (int32_t)y;
        mime_jpeg_up_replicate(comp_row(c, y), c->tmp, c->dw, 2);
        return c->tmp;
    case UP_H1V2_FANCY: {
        uint32_t r = y >> 1;
        mime_jpeg_up_h1v2_fancy(comp_row(c, r), comp_row(c, (y & 1) ? (int64_t)r + 1 : (int64_t)r - 1), c->tmp,
                                c->dw, (int)(y & 1));
        return c->tmp;
    }
    case UP_H2V2_FANCY: {
        uint32_t r = y >> 1;
        mime_jpeg_up_h2v2_fancy(comp_row(c, r), comp_row(c, (y & 1) ? (int64_t)r + 1 : (int64_t)r - 1), c->tmp,
                                c->dw);
        return c->tmp;
    }
    case UP_REPL: {
        uint32_t r = y / c->vexp;
        if (c->hexp == 1)
            return comp_row(c, r);
        if ((int32_t)r == c->cache_row)
            return c->tmp;
        c->cache_row = (int32_t)r;
        mime_jpeg_up_replicate(comp_row(c, r), c->tmp, c->dw, c->hexp);
        return c->tmp;
    }
    default: {
        uint32_t r = (y * c->vin) / d->maxv;
        mime_jpeg_up_nearest(comp_row(c, r), c->tmp, d->out_w, c->hin, d->maxh);
        return c->tmp;
    }
    }
}

/* converts and writes the output rows of the band of MCU row b. */
static void emit_band(Dec *d, uint32_t b)
{
    uint32_t vrows = (uint32_t)d->maxv * d->dct;
    uint32_t y0 = b * vrows;
    uint32_t y1 = y0 + vrows;
    if (y1 > d->out_h)
        y1 = d->out_h;
    const int ncomp = d->info.components;
    for (int i = 0; i < ncomp; i++)
        d->comp[i].cache_row = -1;
    for (uint32_t y = y0; y < y1; y++) {
        uint8_t *dst = d->out + (size_t)y * d->out_stride;
        const uint8_t *r[JPEG_MAX_COMPS];
        for (int i = 0; i < ncomp; i++)
            r[i] = up_row(d, &d->comp[i], y);
        switch (ncomp) {
        case 1:
            __builtin_memcpy(dst, r[0], d->out_w);
            break;
        case 3:
            if (d->cs == MIME_JPEG_CS_YCBCR && !d->raw)
                mime_jpeg_row_ycc_bgr(r[0], r[1], r[2], dst, d->out_w);
            else if (d->cs == MIME_JPEG_CS_RGB && !d->raw)
                mime_jpeg_row_rgb_bgr(r[0], r[1], r[2], dst, d->out_w);
            else
                mime_jpeg_row_rgb_bgr(r[2], r[1], r[0], dst, d->out_w);
            break;
        default:
            mime_jpeg_row_cmyk(r[0], r[1], r[2], r[3], dst, d->out_w, d->cs == MIME_JPEG_CS_YCCK && !d->raw,
                               d->adobe_invert && !d->raw);
            break;
        }
    }
}

/* transforms the blocks of MCU row j into the band planes. */
static void idct_mcu_row(Dec *d, uint32_t j)
{
    for (int i = 0; i < d->info.components; i++) {
        Comp *c = &d->comp[i];
        for (uint32_t by = 0; by < c->v; by++) {
            uint32_t gb = j * c->v + by;
            if (gb >= c->hib)
                break;
            const int16_t *blocks = d->streaming ? c->rowbuf + (size_t)by * c->pitch * 64
                                                 : c->coef + (size_t)gb * c->pitch * 64;
            uint8_t *dst = c->plane + ((size_t)(j % 3) * c->bandrows + (size_t)by * c->dct) * c->pstride;
            mime_jpeg_idct_blocks(c->dct, blocks, c->wib, c->qt, dst, c->pstride);
        }
    }
}

static void finish_mcu_row(Dec *d, uint32_t j)
{
    idct_mcu_row(d, j);
    if (!d->needs_ctx)
        emit_band(d, j);
    else if (j > 0)
        emit_band(d, j - 1);
}

static void finish_image(Dec *d)
{
    if (d->needs_ctx && d->mcus_y > 0)
        emit_band(d, d->mcus_y - 1);
}

/* ---- scans ---- */

static int latch_tables(Dec *d)
{
    for (int i = 0; i < d->ns; i++) {
        Comp *c = &d->comp[d->sc[i]];
        if (c->latched)
            continue;
        if (!d->qt_defined[c->tq])
            return MIME_JPEG_ERR_FORMAT;
        __builtin_memcpy(c->qt, d->qt[c->tq], sizeof c->qt);
        c->latched = true;
    }
    return 0;
}

static void reset_scan_state(Dec *d)
{
    for (int i = 0; i < d->ns; i++)
        d->comp[d->sc[i]].last_dc = 0;
    d->eobrun = 0;
}

static int process_restart(Dec *d)
{
    Bits *b = &d->br;
    b->buf = 0;
    b->cnt = 0;
    b->fake = 0;
    reset_scan_state(d);
    int m = b->marker;
    if (!m) {
        const uint8_t *p = b->p;
        m = find_marker(&p, b->end);
        b->p = p;
        b->lim = b->end;
    }
    int desired = d->next_rst;
    d->restarts_left = d->restart_interval;
    for (;;) {
        if (m == 0xD0 + desired) {
            b->marker = 0;
            b->lim = b->end;
            d->insufficient = false;
            d->next_rst = (desired + 1) & 7;
            return 0;
        }
        if (!d->tolerant)
            return MIME_JPEG_ERR_DATA;
        d->damaged = true;
        int action;
        if (m < 0) {
            /* the file is over, which reads as the end of the image from now on. */
            m = 0xD9;
            b->p = b->end;
            action = 3;
        } else if (m < 0xC0) {
            action = 2;
        } else if (m < 0xD0 || m > 0xD7) {
            action = 3;
        } else if (m == 0xD0 + ((desired + 1) & 7) || m == 0xD0 + ((desired + 2) & 7)) {
            action = 3;
        } else if (m == 0xD0 + ((desired - 1) & 7) || m == 0xD0 + ((desired - 2) & 7)) {
            action = 2;
        } else {
            action = 1;
        }
        if (action == 1) {
            b->marker = 0;
            b->lim = b->end;
            d->insufficient = false;
            d->next_rst = (desired + 1) & 7;
            return 0;
        }
        if (action == 3) {
            /* the marker stays unread, so the next bits come out as zeros. */
            b->marker = m;
            b->lim = b->p;
            return 0;
        }
        const uint8_t *p = b->p;
        m = find_marker(&p, b->end);
        b->p = p;
        b->marker = 0;
        b->lim = b->end;
    }
}

/* decodes one MCU. */
static inline void decode_mcu(Dec *d, Bits *b, uint32_t mx, int16_t *const rows[], const Huff *dcs[],
                              const Huff *acs[], int kind)
{
    for (int i = 0; i < d->ns; i++) {
        Comp *c = &d->comp[d->sc[i]];
        unsigned sh = d->sh[i], sv = d->sv[i];
        for (unsigned vy = 0; vy < sv; vy++) {
            for (unsigned hx = 0; hx < sh; hx++) {
                int16_t *blk = rows[i] + ((size_t)vy * c->pitch + (size_t)mx * sh + hx) * 64;
                switch (kind) {
                case K_SEQ:
                    __builtin_memset(blk, 0, 128);
                    decode_block_seq(b, dcs[i], acs[i], &c->last_dc, blk);
                    break;
                case K_DC_FIRST:
                    decode_block_dc_first(b, dcs[i], &c->last_dc, d->al, blk);
                    break;
                case K_DC_REFINE:
                    NEED(b, 1);
                    if (bits_get(b, 1))
                        blk[0] = (int16_t)(blk[0] | (1 << d->al));
                    break;
                case K_AC_FIRST:
                    decode_block_ac_first(d, b, acs[i], blk);
                    break;
                default:
                    decode_block_ac_refine(d, b, acs[i], blk);
                    break;
                }
            }
        }
    }
}

static inline void zero_mcu(Dec *d, uint32_t mx, int16_t *const rows[], int kind)
{
    if (kind != K_SEQ)
        return;
    for (int i = 0; i < d->ns; i++) {
        Comp *c = &d->comp[d->sc[i]];
        unsigned sh = d->sh[i], sv = d->sv[i];
        for (unsigned vy = 0; vy < sv; vy++)
            for (unsigned hx = 0; hx < sh; hx++)
                __builtin_memset(rows[i] + ((size_t)vy * c->pitch + (size_t)mx * sh + hx) * 64, 0, 128);
    }
}

/* decodes the MCUs of one row of the scan. */
static inline int decode_mcu_row_k(Dec *d, uint32_t my, int kind)
{
    Bits b = d->br;
    int16_t *rows[JPEG_MAX_COMPS];
    const Huff *dcs[JPEG_MAX_COMPS], *acs[JPEG_MAX_COMPS];
    int rc = 0;

    /* stored blocks keep what they have once the data is gone, so the rest of the scan costs nothing. */
    if (d->insufficient && !d->restart_interval && !d->streaming)
        return 0;

    for (int i = 0; i < d->ns; i++) {
        Comp *c = &d->comp[d->sc[i]];
        if (d->streaming)
            rows[i] = c->rowbuf;
        else if (d->ns == 1)
            rows[i] = c->coef + (size_t)my * c->pitch * 64;
        else
            rows[i] = c->coef + (size_t)my * c->v * c->pitch * 64;
        dcs[i] = &d->huff[d->tdc[i]];
        acs[i] = &d->huff[4 + d->tac[i]];
    }
    for (uint32_t mx = 0; mx < d->scan_cols; mx++) {
        if (d->restart_interval) {
            if (d->restarts_left == 0) {
                d->br = b;
                rc = process_restart(d);
                b = d->br;
                if (rc < 0)
                    break;
            }
            d->restarts_left--;
        }
        if (d->insufficient) {
            zero_mcu(d, mx, rows, kind);
            continue;
        }
        decode_mcu(d, &b, mx, rows, dcs, acs, kind);
        if (b.cnt < b.fake || b.bad) {
            d->damaged = true;
            if (!d->tolerant) {
                rc = MIME_JPEG_ERR_DATA;
                break;
            }
            /* running out of data neutralizes the rest of the interval, a bad code does not. */
            if (b.cnt < b.fake)
                d->insufficient = true;
            b.bad = 0;
        }
    }
    d->br = b;
    return rc;
}

static int decode_mcu_row(Dec *d, uint32_t my)
{
    switch (d->kind) {
    case K_SEQ:
        return decode_mcu_row_k(d, my, K_SEQ);
    case K_DC_FIRST:
        return decode_mcu_row_k(d, my, K_DC_FIRST);
    case K_DC_REFINE:
        return decode_mcu_row_k(d, my, K_DC_REFINE);
    case K_AC_FIRST:
        return decode_mcu_row_k(d, my, K_AC_FIRST);
    default:
        return decode_mcu_row_k(d, my, K_AC_REFINE);
    }
}

/* prepares the scan that parse_sos described: tables, geometry and the bit reader. */
static int begin_scan(Dec *d)
{
    int rc = latch_tables(d);
    if (rc < 0)
        return rc;
    for (int i = 0; i < d->ns; i++) {
        Comp *c = &d->comp[d->sc[i]];
        if (d->ns == 1) {
            d->sh[i] = d->sv[i] = 1;
        } else {
            d->sh[i] = c->h;
            d->sv[i] = c->v;
        }
    }
    if (d->ns == 1) {
        Comp *c = &d->comp[d->sc[0]];
        d->scan_cols = c->wib;
        d->scan_rows = c->hib;
    } else {
        d->scan_cols = d->mcus_x;
        d->scan_rows = d->mcus_y;
    }
    reset_scan_state(d);
    d->insufficient = false;
    d->restarts_left = d->restart_interval;
    d->next_rst = 0;
    Bits *b = &d->br;
    b->p = d->data + d->pos;
    b->end = d->data + d->size;
    b->lim = b->end;
    b->buf = 0;
    b->cnt = 0;
    b->fake = 0;
    b->marker = 0;
    b->bad = 0;
    return 0;
}

/* leaves the entropy data: the next markers are looked for behind what the reader consumed. */
static void end_scan(Dec *d)
{
    d->pos = (size_t)(d->br.p - d->data);
    d->unread = d->br.marker;
    if (!d->unread && d->br.p >= d->br.end)
        d->pos = d->size;
}

/* ---- the driver ---- */

static int alloc_buffers(Dec *d, bool streaming)
{
    const MimeJpegInfo *info = &d->info;
    uint64_t coef_bytes = 0;
    d->streaming = streaming;
    d->needs_ctx = false;
    for (int i = 0; i < info->components; i++) {
        Comp *c = &d->comp[i];
        uint32_t rows_padded = d->mcus_y * c->v;
        if (!streaming)
            coef_bytes += (uint64_t)c->pitch * rows_padded * 128;
        if (c->mode == UP_H1V2_FANCY || c->mode == UP_H2V2_FANCY)
            d->needs_ctx = true;
    }
    if (coef_bytes > COEF_LIMIT)
        return MIME_JPEG_ERR_TOO_LARGE;
    for (int i = 0; i < info->components; i++) {
        Comp *c = &d->comp[i];
        uint32_t rows_padded = d->mcus_y * c->v;
        if (streaming) {
            c->rowbuf = dec_alloc(d, (uint64_t)c->pitch * c->v * 128, true);
            if (!c->rowbuf)
                return MIME_JPEG_ERR_NOMEM;
        } else {
            c->coef = dec_alloc(d, (uint64_t)c->pitch * rows_padded * 128, true);
            if (!c->coef)
                return MIME_JPEG_ERR_NOMEM;
        }
        c->pstride = ((c->wib * c->dct + 15) & ~15u) + 32;
        c->plane = dec_alloc(d, (uint64_t)c->pstride * c->bandrows * 3, true);
        uint32_t tw = d->out_w > c->dw * c->hexp ? d->out_w : c->dw * c->hexp;
        c->tmp = c->mode == UP_NONE ? nullptr : dec_alloc(d, (uint64_t)((tw + 15) & ~15u) + 64, true);
        if (!c->plane || (c->mode != UP_NONE && !c->tmp))
            return MIME_JPEG_ERR_NOMEM;
        c->cache_row = -1;
    }
    return 0;
}

static int run_decode(Dec *d)
{
    int rc = parse_markers(d);
    bool no_scan = false;
    if (rc < 0)
        return rc;
    if (rc == R_EOI) {
        /* tolerant decoding of a file that ends before its first scan gives a neutral picture. */
        if (!d->tolerant || !d->have_sof)
            return MIME_JPEG_ERR_FORMAT;
        no_scan = true;
        d->damaged = true;
        d->ns = d->info.components;
        for (int i = 0; i < d->ns; i++) {
            d->sc[i] = i;
            d->tdc[i] = d->tac[i] = d->ns == 3 && i > 0 ? 1 : 0;
            if (!d->qt_defined[d->info.comp[i].tq]) {
                for (int k = 0; k < 64; k++)
                    d->qt[d->info.comp[i].tq][k] = 1;
                d->qt_defined[d->info.comp[i].tq] = true;
            }
        }
        d->ss = 0;
        d->se = d->info.progressive ? 0 : 63;
        d->ah = d->al = 0;
        d->kind = d->info.progressive ? K_DC_FIRST : K_SEQ;
        d->pos = d->size;
    }
    decide_color(d);
    d->info.restart_interval = d->restart_interval;
    setup_geometry(d);
    rc = check_output(d, d->out_stride, d->out_size);
    if (rc < 0)
        return rc;

    /* a file this short cannot hold the picture it claims, which also bounds the work and memory. */
    uint64_t total_blocks = 0;
    for (int i = 0; i < d->info.components; i++)
        total_blocks += (uint64_t)d->comp[i].pitch * d->mcus_y * d->comp[i].v;
    if (total_blocks > 32ull * d->size + 65536)
        return MIME_JPEG_ERR_DATA;

    /* the scan header was read, decide how to run it. */
    bool streaming = !d->info.progressive && d->ns == d->info.components;
    rc = alloc_buffers(d, streaming);
    if (rc < 0)
        return rc;

    if (streaming) {
        rc = begin_scan(d);
        if (rc < 0)
            return rc;
        for (uint32_t my = 0; my < d->mcus_y; my++) {
            rc = decode_mcu_row(d, my);
            if (rc < 0)
                return rc;
            finish_mcu_row(d, my);
        }
        finish_image(d);
        end_scan(d);
        return 0;
    }

    for (int scans = 0; !no_scan; scans++) {
        if (scans == MAX_SCANS) {
            if (!d->tolerant)
                return MIME_JPEG_ERR_DATA;
            d->damaged = true;
            break;
        }
        rc = begin_scan(d);
        if (rc < 0)
            return rc;
        for (uint32_t my = 0; my < d->scan_rows; my++) {
            rc = decode_mcu_row(d, my);
            if (rc < 0)
                return rc;
        }
        end_scan(d);
        rc = parse_markers(d);
        if (rc < 0) {
            if (!d->tolerant)
                return rc;
            d->damaged = true;
            break;
        }
        if (rc == R_EOI)
            break;
    }
    for (int i = 0; i < d->info.components; i++) {
        Comp *c = &d->comp[i];
        if (!c->latched) {
            if (d->qt_defined[c->tq])
                __builtin_memcpy(c->qt, d->qt[c->tq], sizeof c->qt);
            else
                for (int k = 0; k < 64; k++)
                    c->qt[k] = 1;
            c->latched = true;
        }
    }
    for (uint32_t j = 0; j < d->mcus_y; j++)
        finish_mcu_row(d, j);
    finish_image(d);
    return 0;
}

static void dec_init(Dec *d, const MimeAllocator *alloc, const void *data, size_t size, unsigned flags)
{
    __builtin_memset(d, 0, sizeof *d);
    d->alloc = alloc;
    d->data = data;
    d->size = size;
    d->flags = flags;
    d->tolerant = (flags & MIME_JPEG_TOLERANT) != 0;
}

static int check_soi(const void *data, size_t size)
{
    const uint8_t *p = data;
    if (!p || size < 4 || p[0] != 0xFF || p[1] != 0xD8)
        return MIME_JPEG_ERR_FORMAT;
    return 0;
}

bool mime_jpeg_is_jpeg(const void *data, size_t size)
{
    const uint8_t *p = data;
    return p && size >= 4 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF;
}

int mime_jpeg_info(const void *data, size_t size, unsigned flags, MimeJpegInfo *info)
{
    int rc = check_soi(data, size);
    if (rc < 0)
        return rc;
    if (!info)
        return MIME_JPEG_ERR_PARAM;
    Dec d;
    dec_init(&d, nullptr, data, size, flags);
    d.pos = 2;
    rc = parse_markers(&d);
    if (rc < 0)
        return rc;
    if (rc == R_EOI && !(d.tolerant && d.have_sof))
        return MIME_JPEG_ERR_FORMAT;
    decide_color(&d);
    d.info.restart_interval = d.restart_interval;
    setup_geometry(&d);
    *info = d.info;
    return 0;
}

int mime_jpeg_decode(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t *out,
                     size_t out_stride, size_t out_size, MimeJpegInfo *info)
{
    int rc = check_soi(data, size);
    if (rc < 0)
        return rc;
    if (!alloc || !out)
        return MIME_JPEG_ERR_PARAM;
    Dec *d = mime_alloc(alloc, sizeof *d);
    if (!d)
        return MIME_JPEG_ERR_NOMEM;
    dec_init(d, alloc, data, size, flags);
    d->pos = 2;
    d->out = out;
    d->out_stride = out_stride;
    d->out_size = out_size;
    d->huff = dec_alloc(d, 8 * sizeof(Huff), false);
    if (!d->huff) {
        mime_free(alloc, d);
        return MIME_JPEG_ERR_NOMEM;
    }
    /* the standard tables stand in for tables a file does not define. */
    build_huff(&d->huff[0], mime_jpeg_std_dc_luma_bits, mime_jpeg_std_dc_luma_vals, 12, true);
    build_huff(&d->huff[1], mime_jpeg_std_dc_chroma_bits, mime_jpeg_std_dc_chroma_vals, 12, true);
    build_huff(&d->huff[4], mime_jpeg_std_ac_luma_bits, mime_jpeg_std_ac_luma_vals, 162, false);
    build_huff(&d->huff[5], mime_jpeg_std_ac_chroma_bits, mime_jpeg_std_ac_chroma_vals, 162, false);
    for (int i = 2; i < 4; i++) {
        build_huff(&d->huff[i], mime_jpeg_std_dc_luma_bits, mime_jpeg_std_dc_luma_vals, 12, true);
        build_huff(&d->huff[4 + i], mime_jpeg_std_ac_luma_bits, mime_jpeg_std_ac_luma_vals, 162, false);
    }
    rc = run_decode(d);
    if (info && d->info.out_width)
        *info = d->info;
    int result = rc < 0 ? rc : (d->damaged ? MIME_JPEG_TRUNCATED : MIME_JPEG_OK);
    dec_release(d);
    mime_free(alloc, d);
    return result;
}

int mime_jpeg_decode_alloc(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t **out,
                           size_t *out_stride, MimeJpegInfo *info)
{
    MimeJpegInfo local;
    if (!out || !alloc)
        return MIME_JPEG_ERR_PARAM;
    *out = nullptr;
    int rc = mime_jpeg_info(data, size, flags, &local);
    if (rc < 0)
        return rc;
    uint64_t stride = (uint64_t)local.out_width * local.out_format;
    uint64_t total = stride * local.out_height;
    if (total > SIZE_MAX / 2)
        return MIME_JPEG_ERR_TOO_LARGE;
    uint8_t *buf = mime_alloc(alloc, (size_t)total);
    if (!buf)
        return MIME_JPEG_ERR_NOMEM;
    rc = mime_jpeg_decode(data, size, alloc, flags, buf, (size_t)stride, (size_t)total, info);
    if (rc < 0) {
        mime_free(alloc, buf);
        return rc;
    }
    *out = buf;
    if (out_stride)
        *out_stride = (size_t)stride;
    return rc;
}
