/*
 * the decoder: the markers, the entropy decoding of the sequential and progressive scans of annexes F and G, the
 * restart intervals and the driver that turns the blocks into planes and the planes into pixels.
 *
 * a sequential image whose one scan holds every component is transformed block by block as it is decoded. any
 * other image, which means a progressive one or one with a scan for each component, keeps the coefficients of the
 * whole picture until its last scan.
 */
#include "dec.h"

#define SCAN_LIMIT 1024

/* ------------------------------------------------------------------ memory */

void *jpeg_alloc(Jpeg *j, size_t size)
{
    if (j->mem_count >= (int)(sizeof j->mem / sizeof j->mem[0]))
        return NULL;
    void *p = mime_alloc(j->alloc, size ? size : 1);
    if (p)
        j->mem[j->mem_count++] = p;
    return p;
}

static void *alloc_array(Jpeg *j, uint64_t count, uint64_t item)
{
    uint64_t total = count * item;
    if (item && total / item != count)
        return NULL;
    if (total > (uint64_t)(SIZE_MAX / 2))
        return NULL;
    return jpeg_alloc(j, (size_t)total);
}

static void release(Jpeg *j)
{
    for (int i = 0; i < j->mem_count; i++)
        mime_free(j->alloc, j->mem[i]);
    j->mem_count = 0;
}

/* ------------------------------------------------------------------ huffman tables */

static int huff_build(JpegHuff *h, const uint8_t *bits, const uint8_t *vals, int count)
{
    int code = 0, k = 0;
    __builtin_memset(h, 0, sizeof *h);
    for (int len = 1; len <= 16; len++) {
        int n = bits[len];
        h->delta[len] = k - code;
        if (code + n > (1 << len))
            return -1; /* more codes than the length has room for. */
        for (int i = 0; i < n; i++, code++, k++) {
            if (len <= FAST_BITS) {
                int first = code << (FAST_BITS - len);
                int span = 1 << (FAST_BITS - len);
                for (int f = 0; f < span; f++)
                    h->fast[first + f] = (uint16_t)((len << 8) | vals[k]);
            }
        }
        h->limit[len] = n ? code << (16 - len) : 0;
        code <<= 1;
    }
    __builtin_memcpy(h->vals, vals, (size_t)count);
    h->defined = true;
    return 0;
}

/* ------------------------------------------------------------------ the bits of the entropy coded data */

typedef struct Bits {
    const uint8_t *p, *end;
    uint64_t acc; /* the next bits, first one on top. */
    int cnt;      /* how many bits of acc are valid. */
    int pad;      /* how many of those are zeros made up past the end of the data. */
    bool marker;  /* p is at a marker or the end, nothing more comes. */
} Bits;

static void bits_init(Bits *b, const uint8_t *p, const uint8_t *end)
{
    b->p = p;
    b->end = end;
    b->acc = 0;
    b->cnt = 0;
    b->pad = 0;
    b->marker = false;
}

/* one byte at a time, undoing the stuffing and stopping at a marker. */
static void bits_fill_slow(Bits *b)
{
    while (b->cnt <= 56) {
        if (!b->marker && b->p < b->end) {
            unsigned byte = *b->p;
            if (byte == 0xFF) {
                if (b->p + 1 < b->end && b->p[1] == 0) {
                    b->p += 2;
                } else {
                    b->marker = true;
                    continue;
                }
            } else {
                b->p++;
            }
            b->acc |= (uint64_t)byte << (56 - b->cnt);
            b->cnt += 8;
        } else {
            b->marker = true;
            b->cnt += 8;
            b->pad += 8;
        }
    }
}

static inline void bits_fill(Bits *b)
{
    if (b->end - b->p >= 8) {
        uint64_t w;
        __builtin_memcpy(&w, b->p, 8);
        w = __builtin_bswap64(w);
        uint64_t v = ~w;
        if (!(((v - 0x0101010101010101ull) & ~v) & 0x8080808080808080ull)) {
            /* no 0xff among the eight bytes: take as many as fit. */
            int take = (64 - b->cnt) >> 3;
            uint64_t mask = take == 8 ? ~0ull : ~(~0ull >> (8 * take));
            b->acc |= (w & mask) >> b->cnt;
            b->cnt += 8 * take;
            b->p += take;
            return;
        }
    }
    bits_fill_slow(b);
}

static inline int bits_symbol(Bits *b, const JpegHuff *h)
{
    unsigned e = h->fast[(unsigned)(b->acc >> (64 - FAST_BITS))];
    if (e) {
        b->acc <<= e >> 8;
        b->cnt -= (int)(e >> 8);
        return (int)(e & 255);
    }
    int32_t code = (int32_t)(b->acc >> 48);
    for (int len = FAST_BITS + 1; len <= 16; len++) {
        if (code < h->limit[len]) {
            b->acc <<= len;
            b->cnt -= len;
            return h->vals[((code >> (16 - len)) + h->delta[len]) & 255];
        }
    }
    return -1;
}

/* n bits, 1 to 16, as a number. */
static inline int bits_take(Bits *b, int n)
{
    int v = (int)(b->acc >> (64 - n));
    b->acc <<= n;
    b->cnt -= n;
    return v;
}

static inline int extend(int v, int s)
{
    return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}

/* ------------------------------------------------------------------ blocks */

/* a sequential block: the coefficients are put in the zeroed block, the result is the last one that is not zero. */
static int block_sequential(Bits *bp, const JpegHuff *dc, const JpegHuff *ac, int *pred, int16_t *blk)
{
    Bits b = *bp;
    int last = 0;
    if (b.cnt < 32)
        bits_fill(&b);
    int s = bits_symbol(&b, dc);
    if (s < 0 || s > 15)
        goto bad;
    int diff = 0;
    if (s)
        diff = extend(bits_take(&b, s), s);
    *pred = (int16_t)(*pred + diff);
    blk[0] = (int16_t)*pred;
    for (int k = 1; k < 64;) {
        if (b.cnt < 32)
            bits_fill(&b);
        int rs = bits_symbol(&b, ac);
        if (rs < 0)
            goto bad;
        int r = rs >> 4;
        s = rs & 15;
        if (s) {
            k += r;
            if (k > 63)
                goto bad;
            blk[jpeg_zigzag[k]] = (int16_t)extend(bits_take(&b, s), s);
            last = k++;
        } else if (r == 15) {
            k += 16;
        } else {
            break;
        }
    }
    *bp = b;
    return last;
bad:
    *bp = b;
    return -1;
}

static int block_dc_first(Bits *bp, const JpegHuff *dc, int *pred, int al, int16_t *blk)
{
    Bits b = *bp;
    if (b.cnt < 32)
        bits_fill(&b);
    int s = bits_symbol(&b, dc);
    if (s < 0 || s > 15) {
        *bp = b;
        return -1;
    }
    int diff = s ? extend(bits_take(&b, s), s) : 0;
    *pred = (int16_t)(*pred + diff);
    blk[0] = (int16_t)(*pred * (1 << al));
    *bp = b;
    return 0;
}

static int block_dc_refine(Bits *bp, int al, int16_t *blk)
{
    if (bp->cnt < 32)
        bits_fill(bp);
    if (bits_take(bp, 1))
        blk[0] = (int16_t)(blk[0] | (1 << al));
    return 0;
}

static int block_ac_first(Bits *bp, const JpegHuff *ac, int ss, int se, int al, uint32_t *eobrun, int16_t *blk)
{
    if (*eobrun) {
        (*eobrun)--;
        return 0;
    }
    Bits b = *bp;
    for (int k = ss; k <= se;) {
        if (b.cnt < 32)
            bits_fill(&b);
        int rs = bits_symbol(&b, ac);
        if (rs < 0)
            goto bad;
        int r = rs >> 4, s = rs & 15;
        if (s) {
            k += r;
            if (k > se)
                goto bad;
            blk[jpeg_zigzag[k]] = (int16_t)(extend(bits_take(&b, s), s) * (1 << al));
            k++;
        } else if (r == 15) {
            k += 16;
        } else {
            /* a run of 2^r blocks plus r more bits without anything left in the band, this one first. */
            uint32_t run = 1u << r;
            if (r)
                run += (uint32_t)bits_take(&b, r);
            *eobrun = run - 1;
            break;
        }
    }
    *bp = b;
    return 0;
bad:
    *bp = b;
    return -1;
}

/* a correction bit for a coefficient that already is not zero. */
static inline void refine_coef(Bits *b, int16_t *c, int bit)
{
    if (bits_take(b, 1)) {
        int mag = *c < 0 ? -*c : *c;
        if (!(mag & bit))
            *c = (int16_t)(*c < 0 ? *c - bit : *c + bit);
    }
}

static int block_ac_refine(Bits *bp, const JpegHuff *ac, int ss, int se, int al, uint32_t *eobrun, int16_t *blk)
{
    Bits b = *bp;
    int bit = 1 << al;
    int k = ss;
    if (!*eobrun) {
        for (; k <= se; k++) {
            if (b.cnt < 32)
                bits_fill(&b);
            int rs = bits_symbol(&b, ac);
            if (rs < 0)
                goto bad;
            int r = rs >> 4, s = rs & 15;
            int value = 0;
            if (s) {
                if (s != 1)
                    goto bad;
                value = bits_take(&b, 1) ? bit : -bit;
            } else if (r != 15) {
                uint32_t run = 1u << r;
                if (r)
                    run += (uint32_t)bits_take(&b, r);
                *eobrun = run;
                break;
            }
            /* r coefficients that are zero so far are passed, and the nonzero ones on the way get their bit. */
            for (; k <= se; k++) {
                int16_t *c = &blk[jpeg_zigzag[k]];
                if (b.cnt < 16)
                    bits_fill(&b);
                if (*c) {
                    refine_coef(&b, c, bit);
                } else if (--r < 0) {
                    break;
                }
            }
            if (value && k <= se)
                blk[jpeg_zigzag[k]] = (int16_t)value;
        }
    }
    if (*eobrun) {
        for (; k <= se; k++) {
            int16_t *c = &blk[jpeg_zigzag[k]];
            if (b.cnt < 16)
                bits_fill(&b);
            if (*c)
                refine_coef(&b, c, bit);
        }
        (*eobrun)--;
    }
    *bp = b;
    return 0;
bad:
    *bp = b;
    return -1;
}

/* ------------------------------------------------------------------ the markers before and between the scans */

static int be16(const uint8_t *p)
{
    return (p[0] << 8) | p[1];
}

/* the next marker, or -1 when the data ends. */
static int next_marker(Jpeg *j)
{
    const uint8_t *d = j->data;
    size_t n = j->size, p = j->pos;
    for (;;) {
        while (p < n && d[p] != 0xFF)
            p++;
        while (p < n && d[p] == 0xFF)
            p++;
        if (p >= n) {
            j->pos = n;
            return -1;
        }
        int m = d[p++];
        if (m == 0 || m == 1 || (m >= M_RST0 && m <= M_SOI))
            continue;
        j->pos = p;
        return m;
    }
}

static int parse_sof(Jpeg *j, int marker, const uint8_t *p, size_t len)
{
    if (j->have_frame)
        return MIME_JPEG_ERR_FORMAT;
    if (marker != M_SOF0 && marker != M_SOF1 && marker != M_SOF2)
        return MIME_JPEG_ERR_UNSUPPORTED;
    if (len < 6)
        return MIME_JPEG_ERR_FORMAT;
    if (p[0] != 8)
        return MIME_JPEG_ERR_UNSUPPORTED;
    uint32_t height = (uint32_t)be16(p + 1), width = (uint32_t)be16(p + 3);
    int nf = p[5];
    if (nf == 0)
        return MIME_JPEG_ERR_FORMAT;
    if (nf == 2 || nf > 4)
        return MIME_JPEG_ERR_UNSUPPORTED;
    if (len != 6 + 3 * (size_t)nf)
        return MIME_JPEG_ERR_FORMAT;
    if (height == 0)
        return MIME_JPEG_ERR_UNSUPPORTED;
    if (width == 0)
        return MIME_JPEG_ERR_FORMAT;
    if (width > MIME_JPEG_MAX_DIM || height > MIME_JPEG_MAX_DIM || (uint64_t)width * height > MIME_JPEG_MAX_PIXELS)
        return MIME_JPEG_ERR_TOO_LARGE;
    for (int i = 0; i < nf; i++) {
        JpegComp *c = &j->comp[i];
        c->id = p[6 + 3 * i];
        c->h = p[7 + 3 * i] >> 4;
        c->v = p[7 + 3 * i] & 15;
        c->tq = p[8 + 3 * i];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3)
            return MIME_JPEG_ERR_FORMAT;
        for (int k = 0; k < i; k++)
            if (j->comp[k].id == c->id)
                return MIME_JPEG_ERR_FORMAT;
    }
    j->have_frame = true;
    j->progressive = marker == M_SOF2;
    j->extended = marker == M_SOF1;
    j->width = width;
    j->height = height;
    j->ncomp = nf;
    return 0;
}

static int parse_dqt(Jpeg *j, const uint8_t *p, size_t len)
{
    while (len) {
        int precision = p[0] >> 4, id = p[0] & 15;
        size_t need = 1 + 64 * (precision ? 2u : 1u);
        if (precision > 1 || id > 3 || len < need)
            return MIME_JPEG_ERR_FORMAT;
        for (int k = 0; k < 64; k++)
            j->qt[id][jpeg_zigzag[k]] = precision ? (uint16_t)be16(p + 1 + 2 * k) : p[1 + k];
        j->qt_defined[id] = true;
        p += need;
        len -= need;
    }
    return 0;
}

static int parse_dht(Jpeg *j, const uint8_t *p, size_t len)
{
    while (len) {
        int cls = p[0] >> 4, id = p[0] & 15;
        if (cls > 1 || id > 3 || len < 17)
            return MIME_JPEG_ERR_FORMAT;
        int total = 0;
        uint8_t bits[17];
        bits[0] = 0;
        for (int i = 1; i <= 16; i++) {
            bits[i] = p[i];
            total += p[i];
        }
        if (total > 256 || len < 17 + (size_t)total)
            return MIME_JPEG_ERR_FORMAT;
        if (huff_build(cls ? &j->ac[id] : &j->dc[id], bits, p + 17, total))
            return MIME_JPEG_ERR_FORMAT;
        p += 17 + total;
        len -= 17 + (size_t)total;
    }
    return 0;
}

static void parse_app(Jpeg *j, int marker, const uint8_t *p, size_t len)
{
    if (marker == M_APP0 && len >= 14 && !__builtin_memcmp(p, "JFIF\0", 5) && !j->jfif) {
        j->jfif = true;
        j->jfif_major = p[5];
        j->jfif_minor = p[6];
        j->density_unit = p[7];
        j->x_density = (uint16_t)be16(p + 8);
        j->y_density = (uint16_t)be16(p + 10);
        j->thumb_width = p[12];
        j->thumb_height = p[13];
    } else if (marker == M_APP14 && len >= 12 && !__builtin_memcmp(p, "Adobe", 5) && !j->adobe) {
        j->adobe = true;
        j->adobe_transform = p[11];
    }
}

/* the segment at the position, handled when it is one of the headers. */
static int header_segment(Jpeg *j, int marker)
{
    if (j->pos + 2 > j->size)
        return MIME_JPEG_ERR_FORMAT;
    size_t len = (size_t)be16(j->data + j->pos);
    if (len < 2 || j->pos + len > j->size)
        return MIME_JPEG_ERR_FORMAT;
    const uint8_t *p = j->data + j->pos + 2;
    size_t plen = len - 2;
    int rc = 0;
    if (marker >= M_SOF0 && marker <= 0xCF && marker != M_DHT && marker != M_JPG && marker != M_DAC)
        rc = parse_sof(j, marker, p, plen);
    else if (marker == M_DQT)
        rc = parse_dqt(j, p, plen);
    else if (marker == M_DHT)
        rc = parse_dht(j, p, plen);
    else if (marker == M_DRI)
        j->restart = plen >= 2 ? (uint32_t)be16(p) : 0;
    else if (marker == M_APP0 || marker == M_APP14)
        parse_app(j, marker, p, plen);
    j->pos += len;
    return rc;
}

/* reads the markers up to the first scan. */
static int parse_headers(Jpeg *j)
{
    for (;;) {
        int m = next_marker(j);
        if (m < 0 || m == M_EOI)
            return 0;
        if (m == M_SOS) {
            j->pos -= 2; /* back to the marker, for the scan. */
            return 0;
        }
        int rc = header_segment(j, m);
        if (rc)
            return rc;
    }
}

int jpeg_color_space(const Jpeg *j)
{
    if (j->ncomp == 1)
        return MIME_JPEG_CS_GRAY;
    if (j->ncomp == 3) {
        if (j->adobe)
            return j->adobe_transform == 0 ? MIME_JPEG_CS_RGB : MIME_JPEG_CS_YCBCR;
        if (j->jfif)
            return MIME_JPEG_CS_YCBCR;
        if (j->comp[0].id == 'R' && j->comp[1].id == 'G' && j->comp[2].id == 'B')
            return MIME_JPEG_CS_RGB;
        return MIME_JPEG_CS_YCBCR;
    }
    return j->adobe && j->adobe_transform == 2 ? MIME_JPEG_CS_YCCK : MIME_JPEG_CS_CMYK;
}

/* ------------------------------------------------------------------ the geometry */

static int set_geometry(Jpeg *j)
{
    unsigned scales = j->flags & (MIME_JPEG_SCALE_HALF | MIME_JPEG_SCALE_QUARTER | MIME_JPEG_SCALE_EIGHTH);
    if (scales & (scales - 1))
        return MIME_JPEG_ERR_PARAM;
    j->scale = scales == MIME_JPEG_SCALE_HALF ? 4 : scales == MIME_JPEG_SCALE_QUARTER ? 2 : scales ? 1 : 8;
    int hmax = 1, vmax = 1;
    for (int i = 0; i < j->ncomp; i++) {
        hmax = jpeg_max(hmax, j->comp[i].h);
        vmax = jpeg_max(vmax, j->comp[i].v);
    }
    j->hmax = hmax;
    j->vmax = vmax;
    j->mcux = jpeg_ceil_div(j->width, 8u * (uint32_t)hmax);
    j->mcuy = jpeg_ceil_div(j->height, 8u * (uint32_t)vmax);
    uint32_t n = (uint32_t)j->scale;
    j->out_width = (j->width * n + 7) / 8;
    j->out_height = (j->height * n + 7) / 8;
    for (int i = 0; i < j->ncomp; i++) {
        JpegComp *c = &j->comp[i];
        uint32_t cw = jpeg_ceil_div(j->width * c->h, (uint32_t)hmax);
        uint32_t chh = jpeg_ceil_div(j->height * c->v, (uint32_t)vmax);
        c->bw = j->mcux * c->h;
        c->bh = j->mcuy * c->v;
        c->cbw = jpeg_ceil_div(cw, 8);
        c->cbh = jpeg_ceil_div(chh, 8);
        /*
         * a component with fewer samples than the picture is decoded at a larger size when the picture is
         * scaled down, so that it needs no upsampling when its samples are a multiple of the output's.
         */
        int cn = (int)n;
        while (cn < 8 && ((uint32_t)hmax * n) % ((uint32_t)c->h * (uint32_t)cn * 2) == 0 &&
               ((uint32_t)vmax * n) % ((uint32_t)c->v * (uint32_t)cn * 2) == 0)
            cn *= 2;
        c->n = cn;
        c->pw = c->bw * (uint32_t)cn;
        c->ph = c->bh * (uint32_t)cn;
        c->cw = (cw * (uint32_t)cn + 7) / 8;
        c->ch = (chh * (uint32_t)cn + 7) / 8;
    }
    return 0;
}

static void fill_info(const Jpeg *j, MimeJpegInfo *info)
{
    __builtin_memset(info, 0, sizeof *info);
    info->width = j->width;
    info->height = j->height;
    info->components = j->ncomp;
    info->precision = 8;
    info->progressive = j->progressive;
    info->extended = j->extended;
    info->restart_interval = j->restart;
    info->jfif = j->jfif;
    info->jfif_major = j->jfif_major;
    info->jfif_minor = j->jfif_minor;
    info->density_unit = j->density_unit;
    info->x_density = j->x_density;
    info->y_density = j->y_density;
    info->thumb_width = j->thumb_width;
    info->thumb_height = j->thumb_height;
    info->adobe = j->adobe;
    info->adobe_transform = j->adobe_transform;
    info->color_space = jpeg_color_space(j);
    for (int i = 0; i < j->ncomp; i++) {
        info->comp[i].id = j->comp[i].id;
        info->comp[i].h = j->comp[i].h;
        info->comp[i].v = j->comp[i].v;
        info->comp[i].tq = j->comp[i].tq;
    }
    info->out_width = j->out_width;
    info->out_height = j->out_height;
    info->out_format = j->ncomp == 1 ? MIME_JPEG_FORMAT_GRAY8 : j->ncomp == 3 ? MIME_JPEG_FORMAT_BGR24
                                                                               : MIME_JPEG_FORMAT_CMYK32;
    info->scale_denom = 8 / j->scale;
}

static int open_headers(Jpeg *j, const uint8_t *data, size_t size, unsigned flags)
{
    __builtin_memset(j, 0, sizeof *j);
    j->data = data;
    j->size = size;
    j->flags = flags;
    j->tolerant = (flags & MIME_JPEG_TOLERANT) != 0;
    if (!data || size < 4 || data[0] != 0xFF || data[1] != M_SOI)
        return MIME_JPEG_ERR_FORMAT;
    j->pos = 2;
    int rc = parse_headers(j);
    if (rc)
        return rc;
    if (!j->have_frame)
        return MIME_JPEG_ERR_FORMAT;
    return set_geometry(j);
}

int mime_jpeg_info(const uint8_t *data, size_t size, unsigned flags, MimeJpegInfo *info)
{
    Jpeg j;
    int rc = open_headers(&j, data, size, flags);
    if (rc)
        return rc;
    fill_info(&j, info);
    return 0;
}

/* ------------------------------------------------------------------ scans */

enum { SCAN_SEQ, SCAN_DC_FIRST, SCAN_DC_REFINE, SCAN_AC_FIRST, SCAN_AC_REFINE };

typedef struct Scan {
    int ns;
    int ci[4];
    int ss, se, ah, al;
    int kind;
} Scan;

static int parse_sos(Jpeg *j, Scan *sc)
{
    const uint8_t *p = j->data + j->pos;
    size_t avail = j->size - j->pos;
    if (avail < 2)
        return MIME_JPEG_ERR_FORMAT;
    size_t len = (size_t)be16(p);
    if (len < 6 || len > avail)
        return MIME_JPEG_ERR_FORMAT;
    int ns = p[2];
    if (ns < 1 || ns > j->ncomp || len != 6 + 2 * (size_t)ns)
        return MIME_JPEG_ERR_FORMAT;
    sc->ns = ns;
    for (int i = 0; i < ns; i++) {
        int id = p[3 + 2 * i], found = -1;
        for (int k = 0; k < j->ncomp; k++)
            if (j->comp[k].id == id)
                found = k;
        if (found < 0)
            return MIME_JPEG_ERR_FORMAT;
        for (int k = 0; k < i; k++)
            if (sc->ci[k] == found)
                return MIME_JPEG_ERR_FORMAT;
        sc->ci[i] = found;
        j->comp[found].td = p[4 + 2 * i] >> 4;
        j->comp[found].ta = p[4 + 2 * i] & 15;
        if (j->comp[found].td > 3 || j->comp[found].ta > 3)
            return MIME_JPEG_ERR_FORMAT;
    }
    sc->ss = p[3 + 2 * ns];
    sc->se = p[4 + 2 * ns];
    sc->ah = p[5 + 2 * ns] >> 4;
    sc->al = p[5 + 2 * ns] & 15;
    if (j->progressive) {
        if (sc->ss == 0) {
            sc->se = 0;
            sc->kind = sc->ah ? SCAN_DC_REFINE : SCAN_DC_FIRST;
        } else {
            if (ns != 1 || sc->se < sc->ss || sc->se > 63)
                return MIME_JPEG_ERR_FORMAT;
            sc->kind = sc->ah ? SCAN_AC_REFINE : SCAN_AC_FIRST;
        }
        if (sc->al > 13 || sc->ah > 13)
            return MIME_JPEG_ERR_FORMAT;
    } else {
        sc->ss = 0;
        sc->se = 63;
        sc->ah = sc->al = 0;
        sc->kind = SCAN_SEQ;
    }
    j->pos += len;
    return 0;
}

static JpegHuff *table_for(Jpeg *j, bool ac, int id)
{
    JpegHuff *h = ac ? &j->ac[id] : &j->dc[id];
    if (!h->defined && id < 2) {
        /* a stream without tables of its own, as motion jpeg often is, uses the ones of annex K. */
        const JpegHuffSpec *s = ac ? &jpeg_std_ac[id] : &jpeg_std_dc[id];
        huff_build(h, s->bits, s->vals, s->count);
    }
    return h->defined ? h : NULL;
}

static int latch_quant(Jpeg *j, JpegComp *c)
{
    if (!c->dq_ready) {
        if (!j->qt_defined[c->tq])
            return MIME_JPEG_ERR_FORMAT;
        jpeg_idct_prepare(j->qt[c->tq], c->dq);
        c->dq_ready = true;
    }
    return 0;
}

static int alloc_planes(Jpeg *j)
{
    for (int i = 0; i < j->ncomp; i++) {
        JpegComp *c = &j->comp[i];
        c->plane = alloc_array(j, (uint64_t)c->pw * c->ph, 1);
        if (!c->plane)
            return MIME_JPEG_ERR_NOMEM;
    }
    return 0;
}

static int alloc_coefficients(Jpeg *j)
{
    for (int i = 0; i < j->ncomp; i++) {
        JpegComp *c = &j->comp[i];
        uint64_t blocks = (uint64_t)c->bw * c->bh;
        c->coef = alloc_array(j, blocks * 64, 2);
        if (!c->coef)
            return MIME_JPEG_ERR_NOMEM;
        __builtin_memset(c->coef, 0, (size_t)(blocks * 128));
    }
    return 0;
}

/* a block of the plane without any content: the mean gray. */
static void plane_neutral(JpegComp *c, uint32_t bx, uint32_t by)
{
    if (bx >= c->cbw || by >= c->cbh)
        return;
    size_t n = (size_t)c->n;
    uint8_t *dst = c->plane + (size_t)by * n * c->pw + (size_t)bx * n;
    for (size_t y = 0; y < n; y++)
        __builtin_memset(dst + y * c->pw, 128, n);
}

static void neutral_mcu(Jpeg *j, const Scan *sc, uint32_t mcu)
{
    if (j->buffered)
        return;
    if (sc->ns == 1) {
        JpegComp *c = &j->comp[sc->ci[0]];
        plane_neutral(c, mcu % c->cbw, mcu / c->cbw);
        return;
    }
    uint32_t mx = mcu % j->mcux, my = mcu / j->mcux;
    for (int k = 0; k < sc->ns; k++) {
        JpegComp *c = &j->comp[sc->ci[k]];
        for (int v = 0; v < c->v; v++)
            for (int h = 0; h < c->h; h++)
                plane_neutral(c, mx * c->h + (uint32_t)h, my * c->v + (uint32_t)v);
    }
}

/*
 * at the end of a restart interval: finds the marker, which has to be the next restart marker. returns 1 to go on,
 * with *lost the intervals that were lost between the previous marker and this one, and 0 when the scan ends here,
 * leaving the position at the marker that ended it.
 */
static int restart_sync(Jpeg *j, Bits *b, unsigned *next, unsigned *lost)
{
    const uint8_t *q = b->p, *end = b->end;
    *lost = 0;
    for (;;) {
        while (q + 1 < end && !(q[0] == 0xFF && q[1] != 0 && q[1] != 0xFF))
            q++;
        if (q + 1 >= end) {
            j->pos = j->size;
            return 0;
        }
        int m = q[1];
        if (m >= M_RST0 && m < M_RST0 + 8) {
            unsigned d = ((unsigned)(m - M_RST0) - *next) & 7;
            if (d == 0 || (j->tolerant && d <= 3)) {
                *lost = d;
                *next = (*next + d + 1) & 7;
                b->p = q + 2;
                return 1;
            }
            if (!j->tolerant) {
                j->pos = (size_t)(q - j->data);
                return -1;
            }
            q += 2; /* an old marker out of its turn. */
            continue;
        }
        j->pos = (size_t)(q - j->data);
        return 0;
    }
}

/* decodes one scan. returns 0, MIME_JPEG_TRUNCATED when it was damaged and tolerated, or an error. */
static int decode_scan(Jpeg *j, const Scan *sc)
{
    const uint8_t *start = j->data + j->pos, *end = j->data + j->size;
    Bits b;
    int16_t scratch[64] __attribute__((aligned(16)));
    uint32_t eobrun = 0;
    unsigned next_rst = 0;
    int status = 0;
    const JpegHuff *dc[4] = {0}, *ac[4] = {0};
    for (int k = 0; k < sc->ns; k++) {
        JpegComp *c = &j->comp[sc->ci[k]];
        c->pred = 0;
        if (sc->kind == SCAN_SEQ || sc->kind == SCAN_DC_FIRST) {
            dc[k] = table_for(j, false, c->td);
            if (!dc[k])
                return MIME_JPEG_ERR_FORMAT;
        }
        if (sc->kind == SCAN_SEQ || sc->kind == SCAN_AC_FIRST || sc->kind == SCAN_AC_REFINE) {
            ac[k] = table_for(j, true, c->ta);
            if (!ac[k])
                return MIME_JPEG_ERR_FORMAT;
        }
        int rc = latch_quant(j, c);
        if (rc)
            return rc;
    }
    bits_init(&b, start, end);

    uint32_t nmcu;
    uint32_t wmcu;
    if (sc->ns == 1) {
        wmcu = j->comp[sc->ci[0]].cbw;
        nmcu = wmcu * j->comp[sc->ci[0]].cbh;
    } else {
        wmcu = j->mcux;
        nmcu = j->mcux * j->mcuy;
    }
    uint32_t restart = j->restart;
    uint32_t mcu = 0;
    while (mcu < nmcu) {
        if (restart && mcu && mcu % restart == 0) {
            unsigned lost;
            int r = restart_sync(j, &b, &next_rst, &lost);
            if (r < 0)
                return MIME_JPEG_ERR_DATA;
            if (r == 0) {
                /* the scan ends before its last blocks. */
                if (!j->tolerant)
                    return MIME_JPEG_ERR_DATA;
                status = MIME_JPEG_TRUNCATED;
                for (; mcu < nmcu; mcu++)
                    neutral_mcu(j, sc, mcu);
                return status;
            }
            bits_init(&b, b.p, end);
            eobrun = 0;
            for (int k = 0; k < sc->ns; k++)
                j->comp[sc->ci[k]].pred = 0;
            if (lost) {
                status = MIME_JPEG_TRUNCATED;
                uint32_t stop = mcu + lost * restart < nmcu ? mcu + lost * restart : nmcu;
                for (; mcu < stop; mcu++)
                    neutral_mcu(j, sc, mcu);
            }
        }
        uint32_t interval_end = restart ? (mcu / restart + 1) * restart : nmcu;
        if (interval_end > nmcu)
            interval_end = nmcu;
        for (; mcu < interval_end; mcu++) {
            int bad = 0;
            uint32_t mx = mcu % wmcu, my = mcu / wmcu;
            for (int k = 0; k < sc->ns && !bad; k++) {
                JpegComp *c = &j->comp[sc->ci[k]];
                int hn = sc->ns == 1 ? 1 : c->h, vn = sc->ns == 1 ? 1 : c->v;
                for (int v = 0; v < vn && !bad; v++) {
                    for (int h = 0; h < hn; h++) {
                        uint32_t bx = sc->ns == 1 ? mx : mx * c->h + (uint32_t)h;
                        uint32_t by = sc->ns == 1 ? my : my * c->v + (uint32_t)v;
                        int16_t *blk;
                        if (j->buffered) {
                            blk = c->coef + ((size_t)by * c->bw + bx) * 64;
                        } else {
                            blk = scratch;
                            __builtin_memset(scratch, 0, sizeof scratch);
                        }
                        int rc;
                        switch (sc->kind) {
                        case SCAN_SEQ:
                            rc = block_sequential(&b, dc[k], ac[k], &c->pred, blk);
                            break;
                        case SCAN_DC_FIRST:
                            rc = block_dc_first(&b, dc[k], &c->pred, sc->al, blk);
                            break;
                        case SCAN_DC_REFINE:
                            rc = block_dc_refine(&b, sc->al, blk);
                            break;
                        case SCAN_AC_FIRST:
                            rc = block_ac_first(&b, ac[k], sc->ss, sc->se, sc->al, &eobrun, blk);
                            break;
                        default:
                            rc = block_ac_refine(&b, ac[k], sc->ss, sc->se, sc->al, &eobrun, blk);
                            break;
                        }
                        if (rc < 0) {
                            bad = 1;
                            break;
                        }
                        if (!j->buffered && bx < c->cbw && by < c->cbh)
                            jpeg_idct(blk, c->dq, rc, c->n,
                                      c->plane + (size_t)by * (size_t)c->n * c->pw + (size_t)bx * (size_t)c->n, c->pw);
                    }
                }
            }
            if (!bad && b.cnt < b.pad)
                bad = 1; /* the data ended inside the block. */
            if (bad) {
                if (!j->tolerant)
                    return MIME_JPEG_ERR_DATA;
                status = MIME_JPEG_TRUNCATED;
                /* the rest of the interval is lost, with restarts the next interval can still be read. */
                for (; mcu < interval_end; mcu++)
                    neutral_mcu(j, sc, mcu);
                if (!restart) {
                    j->pos = (size_t)(b.p - j->data);
                    return status;
                }
                break;
            }
        }
    }
    j->pos = (size_t)(b.p - j->data);
    return status;
}

/* ------------------------------------------------------------------ the driver */

/* the transform of every block of a picture put together from several scans. */
static void transform_buffered(Jpeg *j)
{
    for (int i = 0; i < j->ncomp; i++) {
        JpegComp *c = &j->comp[i];
        size_t n = (size_t)c->n;
        if (!c->dq_ready) {
            for (uint32_t by = 0; by < c->cbh; by++)
                for (uint32_t bx = 0; bx < c->cbw; bx++)
                    plane_neutral(c, bx, by);
            continue;
        }
        for (uint32_t by = 0; by < c->cbh; by++) {
            for (uint32_t bx = 0; bx < c->cbw; bx++) {
                const int16_t *blk = c->coef + ((size_t)by * c->bw + bx) * 64;
                int last = 0;
                for (int k = 63; k > 0; k--)
                    if (blk[jpeg_zigzag[k]]) {
                        last = k;
                        break;
                    }
                jpeg_idct(blk, c->dq, last, c->n, c->plane + (size_t)by * n * c->pw + (size_t)bx * n, c->pw);
            }
        }
    }
}

static int decode_image(Jpeg *j, uint8_t *dest, size_t stride)
{
    int rc = alloc_planes(j);
    if (rc)
        return rc;
    int status = 0;
    bool scanned = false;
    for (;;) {
        int m = next_marker(j);
        if (m < 0 || m == M_EOI)
            break;
        if (m == M_SOS) {
            if (j->scans >= SCAN_LIMIT)
                break;
            Scan sc;
            rc = parse_sos(j, &sc);
            if (rc) {
                /* once a scan was read, a broken header ends the picture where it is. */
                if (!scanned)
                    return rc;
                if (!j->tolerant)
                    return MIME_JPEG_ERR_DATA;
                status = MIME_JPEG_TRUNCATED;
                break;
            }
            if (!scanned) {
                j->buffered = j->progressive || sc.ns < j->ncomp;
                if (j->buffered) {
                    rc = alloc_coefficients(j);
                    if (rc)
                        return rc;
                }
            }
            scanned = true;
            j->scans++;
            rc = decode_scan(j, &sc);
            if (rc < 0)
                return rc;
            if (rc > 0)
                status = rc;
            if (!j->buffered)
                break;
            continue;
        }
        rc = header_segment(j, m);
        if (rc) {
            if (!scanned)
                return rc;
            if (!j->tolerant)
                return MIME_JPEG_ERR_DATA;
            status = MIME_JPEG_TRUNCATED;
            break;
        }
    }
    if (!scanned)
        return MIME_JPEG_ERR_FORMAT;
    if (j->buffered)
        transform_buffered(j);
    rc = jpeg_output(j, dest, stride);
    return rc ? rc : status;
}

int mime_jpeg_decode(const uint8_t *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t *dest,
                     size_t stride, size_t dest_size, MimeJpegInfo *info)
{
    Jpeg j;
    int rc = open_headers(&j, data, size, flags);
    if (rc)
        return rc;
    if (!alloc || !dest)
        return MIME_JPEG_ERR_PARAM;
    int bpp = j.ncomp == 1 ? 1 : j.ncomp == 3 ? 3 : 4;
    size_t row = (size_t)j.out_width * (size_t)bpp;
    if (stride < row || dest_size < (size_t)(j.out_height - 1) * stride + row)
        return MIME_JPEG_ERR_PARAM;
    if (info)
        fill_info(&j, info);
    j.alloc = alloc;
    rc = decode_image(&j, dest, stride);
    release(&j);
    return rc;
}
