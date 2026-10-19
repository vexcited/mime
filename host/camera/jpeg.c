/*
 * baseline jpeg for the camera service. the decoder reads sequential huffman jpegs of 8 bit samples with one or three
 * components and any sampling factors up to 2x2, restart intervals and the standard tables for streams that leave
 * them out, as many webcams do. the encoder makes the frames of the test camera. written from the format's
 * description (itu t.81), not from any implementation.
 */
#include "jpeg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* the tables of annex k, which a stream without its own uses and the encoder always does. */
static const uint8_t std_dc_luma_bits[17] = {0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t std_dc_chroma_bits[17] = {0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t std_dc_vals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t std_ac_luma_bits[17] = {0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t std_ac_luma_vals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};
static const uint8_t std_ac_chroma_bits[17] = {0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t std_ac_chroma_vals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};
/* in natural order. */
static const uint8_t std_quant_luma[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,  14, 13, 16, 24, 40,  57,
    69, 56, 14, 17, 22,  29,  51,  87,  80, 62, 18, 22, 37,  56,  68,  109, 103, 77, 24, 35, 55,  64,
    81, 104, 113, 92, 49, 64,  78,  87,  103, 121, 120, 101, 72, 92, 95, 98,  112, 100, 103, 99,
};
static const uint8_t std_quant_chroma[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
    99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

/* ------------------------------------------------------------------ cosines */

/* basis[u][x] = alpha(u) / 2 * cos((2x + 1) u pi / 16), which the transform of a row or column is a sum of. */
static float g_basis[8][8];
static bool g_basis_ready;

static void init_basis(void)
{
    if (__atomic_load_n(&g_basis_ready, __ATOMIC_ACQUIRE))
        return;
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            g_basis[u][x] = (float)((u ? 1.0 : 0.70710678118654752) / 2 * cos((2 * x + 1) * u * 3.14159265358979323846 / 16));
    __atomic_store_n(&g_basis_ready, true, __ATOMIC_RELEASE);
}

/* the inverse transform of eight values, the even and odd terms apart since the basis is symmetric. */
static inline void idct8(const float *in, int in_step, float *out, int out_step)
{
    float even[4], odd[4];
    for (int x = 0; x < 4; x++) {
        even[x] = g_basis[0][x] * in[0] + g_basis[2][x] * in[2 * in_step] + g_basis[4][x] * in[4 * in_step] +
                  g_basis[6][x] * in[6 * in_step];
        odd[x] = g_basis[1][x] * in[in_step] + g_basis[3][x] * in[3 * in_step] + g_basis[5][x] * in[5 * in_step] +
                 g_basis[7][x] * in[7 * in_step];
    }
    for (int x = 0; x < 4; x++) {
        out[x * out_step] = even[x] + odd[x];
        out[(7 - x) * out_step] = even[x] - odd[x];
    }
}

static inline uint8_t clamp_sample(float v)
{
    int i = (int)lrintf(v) + 128;
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static void idct_block(const float *coef, uint8_t *dst, int stride, bool dc_only)
{
    if (dc_only) {
        uint8_t v = clamp_sample(coef[0] * g_basis[0][0] * g_basis[0][0]);
        for (int y = 0; y < 8; y++)
            memset(dst + (ptrdiff_t)y * stride, v, 8);
        return;
    }
    float tmp[64], res[64];
    for (int c = 0; c < 8; c++)
        idct8(coef + c, 8, tmp + c, 8);
    for (int r = 0; r < 8; r++)
        idct8(tmp + r * 8, 1, res + r * 8, 1);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            dst[(ptrdiff_t)y * stride + x] = clamp_sample(res[y * 8 + x]);
}

/* ------------------------------------------------------------------ huffman tables */

typedef struct Huff {
    uint8_t bits[17];
    uint8_t vals[256];
    bool defined;
    int32_t maxcode[17];
    int32_t offset[17];
    uint16_t fast[512]; /* length << 8 | symbol for codes of up to nine bits, 0 for the rest */
} Huff;

static void huff_build(Huff *h)
{
    memset(h->fast, 0, sizeof(h->fast));
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        int n = h->bits[l];
        h->offset[l] = k - code;
        for (int i = 0; i < n; i++, k++, code++) {
            if (l <= 9 && code < (1 << l)) {
                int shift = 9 - l;
                for (int e = 0; e < (1 << shift); e++)
                    h->fast[(code << shift) + e] = (uint16_t)(l << 8 | h->vals[k]);
            }
        }
        h->maxcode[l] = n ? code - 1 : -1;
        code <<= 1;
    }
    h->defined = true;
}

static void huff_set(Huff *h, const uint8_t bits[17], const uint8_t *vals, int count)
{
    memcpy(h->bits, bits, 17);
    memcpy(h->vals, vals, (size_t)count);
    huff_build(h);
}

typedef struct Bits {
    const uint8_t *p, *end;
    uint64_t buf;
    int count;  /* bits in buf, right aligned */
    int marker; /* the marker the data ran into, which is not consumed */
    int pad;    /* how many of the bits that were fed in are zeros past the end of the data */
} Bits;

static void bits_fill(Bits *b)
{
    while (b->count <= 48) {
        unsigned byte = 0;
        bool padding = true;
        if (!b->marker && b->p < b->end) {
            byte = *b->p;
            if (byte == 0xFF) {
                unsigned next = b->p + 1 < b->end ? b->p[1] : 0xD9;
                if (next == 0) {
                    b->p += 2;
                    padding = false;
                } else if (next == 0xFF) {
                    b->p++; /* a fill byte before a marker */
                    continue;
                } else {
                    b->marker = (int)next;
                    byte = 0;
                }
                padding = b->marker != 0;
            } else {
                b->p++;
                padding = false;
            }
        }
        b->pad += padding ? 8 : 0;
        b->buf = b->buf << 8 | byte;
        b->count += 8;
    }
}

static inline int huff_decode(Bits *b, const Huff *h)
{
    if (b->count < 16)
        bits_fill(b);
    unsigned look = (unsigned)(b->buf >> (b->count - 16)) & 0xFFFF;
    unsigned f = h->fast[look >> 7];
    if (f) {
        b->count -= (int)(f >> 8);
        return (int)(f & 255);
    }
    for (int l = 10; l <= 16; l++) {
        int code = (int)(look >> (16 - l));
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l]) {
            b->count -= l;
            return h->vals[h->offset[l] + code];
        }
    }
    b->count -= 16;
    return -1;
}

/* n bits as a signed value: the top half of the range is positive, the rest negative, as the format codes it. */
static inline int receive_extend(Bits *b, int n)
{
    if (!n)
        return 0;
    if (b->count < n)
        bits_fill(b);
    int v = (int)((b->buf >> (b->count - n)) & ((1u << n) - 1));
    b->count -= n;
    return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
}

/* ------------------------------------------------------------------ decoder */

typedef struct Component {
    int id;
    int h, v;
    int quant;
    int dc, ac;
    int pred;
} Component;

typedef struct Decoder {
    Huff dc[4], ac[4];
    uint16_t quant[4][64]; /* zigzag order, as the stream lists them */
    bool quant_defined[4];
    Component comp[3];
    int restart_interval;
    bool frame;
    bool scanned;
} Decoder;

static inline unsigned be16(const uint8_t *p)
{
    return (unsigned)(p[0] << 8 | p[1]);
}

static void use_standard_tables(Decoder *d)
{
    if (!d->dc[0].defined)
        huff_set(&d->dc[0], std_dc_luma_bits, std_dc_vals, 12);
    if (!d->dc[1].defined)
        huff_set(&d->dc[1], std_dc_chroma_bits, std_dc_vals, 12);
    if (!d->ac[0].defined)
        huff_set(&d->ac[0], std_ac_luma_bits, std_ac_luma_vals, 162);
    if (!d->ac[1].defined)
        huff_set(&d->ac[1], std_ac_chroma_bits, std_ac_chroma_vals, 162);
}

static bool decode_block(Bits *b, Component *c, const Decoder *d, uint8_t *dst, int stride)
{
    float coef[64];
    memset(coef, 0, sizeof(coef));
    const Huff *dc = &d->dc[c->dc], *ac = &d->ac[c->ac];
    const uint16_t *q = d->quant[c->quant];
    int s = huff_decode(b, dc);
    if (s < 0 || s > 16)
        return false;
    c->pred += receive_extend(b, s);
    coef[0] = (float)(c->pred * (int)q[0]);
    int last = 0;
    for (int k = 1; k < 64;) {
        int rs = huff_decode(b, ac);
        if (rs < 0)
            return false;
        int r = rs >> 4;
        s = rs & 15;
        if (!s) {
            if (r != 15)
                break;
            k += 16;
            continue;
        }
        k += r;
        if (k > 63)
            return false;
        coef[zigzag[k]] = (float)(receive_extend(b, s) * (int)q[k]);
        last = k;
        k++;
    }
    idct_block(coef, dst, stride, last == 0);
    /* the data ended before the block did: what it read past the end was zeros. */
    return b->pad <= b->count;
}

static void restart_reset(Bits *b, Decoder *d, int comps)
{
    b->count = 0;
    b->buf = 0;
    b->pad = 0;
    /* the marker, or the bytes up to the next restart marker if the data is damaged. */
    if (!b->marker) {
        while (b->p + 1 < b->end && !(b->p[0] == 0xFF && b->p[1] >= 0xD0 && b->p[1] <= 0xD7))
            b->p++;
        if (b->p + 1 < b->end)
            b->marker = b->p[1];
    }
    if (b->marker >= 0xD0 && b->marker <= 0xD7) {
        b->p += 2;
        b->marker = 0;
    }
    for (int i = 0; i < comps; i++)
        d->comp[i].pred = 0;
}

static bool ensure_plane(MimeJpeg *j, int i, int stride, int rows)
{
    size_t need = (size_t)stride * (size_t)rows;
    if (j->capacity[i] < need) {
        uint8_t *p = realloc(j->plane[i], need);
        if (!p)
            return false;
        j->plane[i] = p;
        j->capacity[i] = need;
    }
    j->stride[i] = stride;
    j->rows[i] = rows;
    return true;
}

/* a scan: the interleaved components of every mcu, or one component's blocks in rows. returns the rows lost. */
static bool decode_scan(MimeJpeg *j, Decoder *d, Bits *b, const int *scan, int ns)
{
    int mcu_w = 8 * j->h_max, mcu_h = 8 * j->v_max;
    int mcux = (j->width + mcu_w - 1) / mcu_w, mcuy = (j->height + mcu_h - 1) / mcu_h;
    int across = mcux, down = mcuy;
    if (ns == 1) {
        const Component *c = &d->comp[scan[0]];
        across = ((j->width * c->h + j->h_max - 1) / j->h_max + 7) / 8;
        down = ((j->height * c->v + j->v_max - 1) / j->v_max + 7) / 8;
    }
    for (int i = 0; i < ns; i++)
        d->comp[scan[i]].pred = 0;
    int until_restart = d->restart_interval;
    for (int my = 0; my < down; my++) {
        for (int mx = 0; mx < across; mx++) {
            if (d->restart_interval && until_restart-- == 0) {
                restart_reset(b, d, 3);
                until_restart = d->restart_interval - 1;
            }
            if (ns == 1) {
                int ci = scan[0];
                Component *c = &d->comp[ci];
                uint8_t *dst = j->plane[ci] + (ptrdiff_t)my * 8 * j->stride[ci] + mx * 8;
                if (!decode_block(b, c, d, dst, j->stride[ci]))
                    return false;
                continue;
            }
            for (int i = 0; i < ns; i++) {
                int ci = scan[i];
                Component *c = &d->comp[ci];
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        uint8_t *dst = j->plane[ci] + (ptrdiff_t)(my * c->v + by) * 8 * j->stride[ci] +
                                       (mx * c->h + bx) * 8;
                        if (!decode_block(b, c, d, dst, j->stride[ci]))
                            return false;
                    }
            }
        }
    }
    return true;
}

static bool frame_header(MimeJpeg *j, Decoder *d, const uint8_t *seg, size_t n)
{
    if (n < 6 || seg[0] != 8)
        return false;
    j->height = (int)be16(seg + 1);
    j->width = (int)be16(seg + 3);
    int nf = seg[5];
    if (j->width <= 0 || j->height <= 0 || j->width > 16384 || j->height > 16384 || (nf != 1 && nf != 3) ||
        n < 6 + (size_t)nf * 3)
        return false;
    j->components = nf;
    j->h_max = j->v_max = 1;
    for (int i = 0; i < nf; i++) {
        Component *c = &d->comp[i];
        c->id = seg[6 + i * 3];
        c->h = seg[7 + i * 3] >> 4;
        c->v = seg[7 + i * 3] & 15;
        c->quant = seg[8 + i * 3];
        if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2 || c->quant > 3)
            return false;
        j->h[i] = c->h;
        j->v[i] = c->v;
        if (c->h > j->h_max)
            j->h_max = c->h;
        if (c->v > j->v_max)
            j->v_max = c->v;
    }
    if (nf == 1) {
        /* a single component is not subsampled, whatever it says. */
        d->comp[0].h = d->comp[0].v = j->h[0] = j->v[0] = 1;
        j->h_max = j->v_max = 1;
    }
    int mcux = (j->width + 8 * j->h_max - 1) / (8 * j->h_max);
    int mcuy = (j->height + 8 * j->v_max - 1) / (8 * j->v_max);
    for (int i = 0; i < nf; i++)
        if (!ensure_plane(j, i, mcux * j->h[i] * 8, mcuy * j->v[i] * 8))
            return false;
    d->frame = true;
    return true;
}

bool mime_jpeg_decode(MimeJpeg *j, const uint8_t *data, size_t length)
{
    init_basis();
    if (length < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return false;
    Decoder *d = calloc(1, sizeof(*d));
    if (!d)
        return false;
    j->restart_errors = false;
    const uint8_t *p = data + 2, *end = data + length;
    bool ok = false;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) {
            p++;
            continue;
        }
        int marker = p[1];
        if (marker == 0xFF) {
            p++;
            continue;
        }
        if (marker == 0xD9)
            break;
        if (marker == 0 || marker == 1 || (marker >= 0xD0 && marker <= 0xD8)) {
            p += 2;
            continue;
        }
        size_t seg_len = be16(p + 2);
        if (seg_len < 2 || p + 2 + seg_len > end)
            break;
        const uint8_t *seg = p + 4;
        size_t n = seg_len - 2;
        p += 2 + seg_len;
        switch (marker) {
        case 0xC0:
        case 0xC1:
            if (d->frame || !frame_header(j, d, seg, n))
                goto done;
            break;
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD:
        case 0xCE: case 0xCF:
            goto done; /* progressive, lossless, arithmetic */
        case 0xC4:
            while (n >= 17) {
                int tc = seg[0] >> 4, th = seg[0] & 15, total = 0;
                if (th > 3 || tc > 1)
                    goto done;
                Huff *h = tc ? &d->ac[th] : &d->dc[th];
                h->bits[0] = 0;
                for (int i = 1; i <= 16; i++)
                    total += h->bits[i] = seg[i];
                if (total > 256 || (size_t)total + 17 > n)
                    goto done;
                memcpy(h->vals, seg + 17, (size_t)total);
                huff_build(h);
                seg += 17 + total;
                n -= 17 + (size_t)total;
            }
            break;
        case 0xDB:
            while (n >= 1) {
                int pq = seg[0] >> 4, tq = seg[0] & 15;
                size_t entry = pq ? 2 : 1;
                if (tq > 3 || n < 1 + 64 * entry)
                    goto done;
                for (int i = 0; i < 64; i++)
                    d->quant[tq][i] = (uint16_t)(pq ? be16(seg + 1 + i * 2) : seg[1 + i]);
                d->quant_defined[tq] = true;
                seg += 1 + 64 * entry;
                n -= 1 + 64 * entry;
            }
            break;
        case 0xDD:
            if (n >= 2)
                d->restart_interval = (int)be16(seg);
            break;
        case 0xDA: {
            if (!d->frame || n < 1)
                goto done;
            int ns = seg[0];
            if (ns < 1 || ns > j->components || n < 1 + (size_t)ns * 2 + 3)
                goto done;
            int scan[3];
            for (int i = 0; i < ns; i++) {
                int id = seg[1 + i * 2], found = -1;
                for (int c = 0; c < j->components; c++)
                    if (d->comp[c].id == id)
                        found = c;
                if (found < 0)
                    goto done;
                scan[i] = found;
                d->comp[found].dc = seg[2 + i * 2] >> 4;
                d->comp[found].ac = seg[2 + i * 2] & 15;
                if (d->comp[found].dc > 3 || d->comp[found].ac > 3 || !d->quant_defined[d->comp[found].quant])
                    goto done;
            }
            use_standard_tables(d);
            for (int i = 0; i < ns; i++)
                if (!d->dc[d->comp[scan[i]].dc].defined || !d->ac[d->comp[scan[i]].ac].defined)
                    goto done;
            Bits b = {.p = p, .end = end};
            bool complete = decode_scan(j, d, &b, scan, ns);
            if (!complete)
                j->restart_errors = true;
            d->scanned = true;
            ok = true;
            /* on to what follows the entropy coded data, a marker. */
            p = b.p;
            while (p + 1 < end && !(p[0] == 0xFF && p[1] != 0 && p[1] != 0xFF && !(p[1] >= 0xD0 && p[1] <= 0xD7)))
                p++;
            if (!complete)
                goto done;
            break;
        }
        default:
            break;
        }
    }
done:
    free(d);
    return ok;
}

bool mime_jpeg_size(const uint8_t *data, size_t length, int *width, int *height)
{
    if (length < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return false;
    const uint8_t *p = data + 2, *end = data + length;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) {
            p++;
            continue;
        }
        int marker = p[1];
        if (marker == 0xFF) {
            p++;
            continue;
        }
        if (marker == 0 || marker == 1 || (marker >= 0xD0 && marker <= 0xD8)) {
            p += 2;
            continue;
        }
        size_t seg_len = be16(p + 2);
        if (seg_len < 2 || p + 2 + seg_len > end)
            return false;
        if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (seg_len < 8)
                return false;
            *height = (int)be16(p + 5);
            *width = (int)be16(p + 7);
            return true;
        }
        p += 2 + seg_len;
    }
    return false;
}

void mime_jpeg_free(MimeJpeg *j)
{
    for (int i = 0; i < 3; i++)
        free(j->plane[i]);
    memset(j, 0, sizeof(*j));
}

/* ------------------------------------------------------------------ encoder */

typedef struct Writer {
    uint8_t *out;
    size_t capacity, length;
    uint32_t acc;
    int count;
    bool overflow;
} Writer;

static void put_byte(Writer *w, unsigned v)
{
    if (w->length >= w->capacity) {
        w->overflow = true;
        return;
    }
    w->out[w->length++] = (uint8_t)v;
}

static void put_word(Writer *w, unsigned v)
{
    put_byte(w, v >> 8);
    put_byte(w, v & 255);
}

static void put_bits(Writer *w, unsigned code, int n)
{
    w->acc = w->acc << n | (code & ((1u << n) - 1));
    w->count += n;
    while (w->count >= 8) {
        unsigned byte = (w->acc >> (w->count - 8)) & 255;
        put_byte(w, byte);
        if (byte == 0xFF)
            put_byte(w, 0);
        w->count -= 8;
    }
}

typedef struct HuffEnc {
    uint16_t code[256];
    uint8_t size[256];
} HuffEnc;

static void huff_enc_build(HuffEnc *e, const uint8_t bits[17], const uint8_t *vals)
{
    memset(e, 0, sizeof(*e));
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < bits[l]; i++, k++, code++) {
            e->code[vals[k]] = (uint16_t)code;
            e->size[vals[k]] = (uint8_t)l;
        }
        code <<= 1;
    }
}

static void put_table(Writer *w, int klass, int id, const uint8_t bits[17], const uint8_t *vals, int count)
{
    put_word(w, 0xFFC4);
    put_word(w, (unsigned)(2 + 1 + 16 + count));
    put_byte(w, (unsigned)(klass << 4 | id));
    for (int i = 1; i <= 16; i++)
        put_byte(w, bits[i]);
    for (int i = 0; i < count; i++)
        put_byte(w, vals[i]);
}

/* the forward transform of a block of samples, quantized, in zigzag order. */
static void fdct_block(const uint8_t *src, int stride, int bw, int bh, const float *recip, int16_t *out)
{
    float in[64], tmp[64], res[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int sx = x < bw ? x : bw - 1, sy = y < bh ? y : bh - 1;
            in[y * 8 + x] = (float)src[(ptrdiff_t)sy * stride + sx] - 128.0f;
        }
    /* rows then columns: F(u) = sum over x of basis[u][x] * f(x). */
    for (int r = 0; r < 8; r++)
        for (int u = 0; u < 8; u++) {
            float s = 0;
            for (int x = 0; x < 8; x++)
                s += g_basis[u][x] * in[r * 8 + x];
            tmp[r * 8 + u] = s;
        }
    for (int c = 0; c < 8; c++)
        for (int v = 0; v < 8; v++) {
            float s = 0;
            for (int y = 0; y < 8; y++)
                s += g_basis[v][y] * tmp[y * 8 + c];
            res[v * 8 + c] = s;
        }
    for (int k = 0; k < 64; k++)
        out[k] = (int16_t)lrintf(res[zigzag[k]] * recip[zigzag[k]]);
}

static int category(int v)
{
    unsigned m = (unsigned)(v < 0 ? -v : v);
    int n = 0;
    while (m) {
        n++;
        m >>= 1;
    }
    return n;
}

static void encode_block(Writer *w, const int16_t *z, int *pred, const HuffEnc *dc, const HuffEnc *ac)
{
    int diff = z[0] - *pred;
    *pred = z[0];
    int n = category(diff);
    put_bits(w, dc->code[n], dc->size[n]);
    if (n)
        put_bits(w, (unsigned)(diff < 0 ? diff - 1 : diff), n);
    int run = 0;
    for (int k = 1; k < 64; k++) {
        if (!z[k]) {
            run++;
            continue;
        }
        while (run > 15) {
            put_bits(w, ac->code[0xF0], ac->size[0xF0]);
            run -= 16;
        }
        n = category(z[k]);
        int sym = run << 4 | n;
        put_bits(w, ac->code[sym], ac->size[sym]);
        put_bits(w, (unsigned)(z[k] < 0 ? z[k] - 1 : z[k]), n);
        run = 0;
    }
    if (run)
        put_bits(w, ac->code[0], ac->size[0]);
}

size_t mime_jpeg_encode(const uint8_t *y, int y_stride, const uint8_t *cb, const uint8_t *cr, int c_stride, int width,
                        int height, int quality, uint8_t *out, size_t capacity)
{
    init_basis();
    if (quality < 1)
        quality = 1;
    if (quality > 100)
        quality = 100;
    int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    uint8_t q[2][64];
    float recip[2][64];
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 64; i++) {
            int v = ((t ? std_quant_chroma[i] : std_quant_luma[i]) * scale + 50) / 100;
            q[t][i] = (uint8_t)(v < 1 ? 1 : v > 255 ? 255 : v);
            recip[t][i] = 1.0f / q[t][i];
        }
    HuffEnc dc[2], ac[2];
    huff_enc_build(&dc[0], std_dc_luma_bits, std_dc_vals);
    huff_enc_build(&dc[1], std_dc_chroma_bits, std_dc_vals);
    huff_enc_build(&ac[0], std_ac_luma_bits, std_ac_luma_vals);
    huff_enc_build(&ac[1], std_ac_chroma_bits, std_ac_chroma_vals);

    Writer w = {.out = out, .capacity = capacity};
    put_word(&w, 0xFFD8);
    put_word(&w, 0xFFE0); /* jfif */
    put_word(&w, 16);
    for (const char *s = "JFIF"; *s; s++)
        put_byte(&w, (unsigned)*s);
    put_byte(&w, 0);
    put_word(&w, 0x0101);
    put_byte(&w, 0);
    put_word(&w, 1);
    put_word(&w, 1);
    put_word(&w, 0);
    for (int t = 0; t < 2; t++) {
        put_word(&w, 0xFFDB);
        put_word(&w, 67);
        put_byte(&w, (unsigned)t);
        for (int i = 0; i < 64; i++)
            put_byte(&w, q[t][zigzag[i]]);
    }
    put_word(&w, 0xFFC0);
    put_word(&w, 17);
    put_byte(&w, 8);
    put_word(&w, (unsigned)height);
    put_word(&w, (unsigned)width);
    put_byte(&w, 3);
    put_byte(&w, 1);
    put_byte(&w, 0x22);
    put_byte(&w, 0);
    put_byte(&w, 2);
    put_byte(&w, 0x11);
    put_byte(&w, 1);
    put_byte(&w, 3);
    put_byte(&w, 0x11);
    put_byte(&w, 1);
    put_table(&w, 0, 0, std_dc_luma_bits, std_dc_vals, 12);
    put_table(&w, 1, 0, std_ac_luma_bits, std_ac_luma_vals, 162);
    put_table(&w, 0, 1, std_dc_chroma_bits, std_dc_vals, 12);
    put_table(&w, 1, 1, std_ac_chroma_bits, std_ac_chroma_vals, 162);
    put_word(&w, 0xFFDA);
    put_word(&w, 12);
    put_byte(&w, 3);
    put_byte(&w, 1);
    put_byte(&w, 0x00);
    put_byte(&w, 2);
    put_byte(&w, 0x11);
    put_byte(&w, 3);
    put_byte(&w, 0x11);
    put_byte(&w, 0);
    put_byte(&w, 63);
    put_byte(&w, 0);

    int cw = (width + 1) / 2, ch = (height + 1) / 2;
    int mcux = (width + 15) / 16, mcuy = (height + 15) / 16;
    int pred[3] = {};
    int16_t z[64];
    for (int my = 0; my < mcuy; my++)
        for (int mx = 0; mx < mcux; mx++) {
            for (int by = 0; by < 2; by++)
                for (int bx = 0; bx < 2; bx++) {
                    int x0 = mx * 16 + bx * 8, y0 = my * 16 + by * 8;
                    int bw = width - x0 < 8 ? width - x0 : 8, bh = height - y0 < 8 ? height - y0 : 8;
                    if (bw <= 0)
                        bw = 1, x0 = width - 1;
                    if (bh <= 0)
                        bh = 1, y0 = height - 1;
                    fdct_block(y + (ptrdiff_t)y0 * y_stride + x0, y_stride, bw, bh, recip[0], z);
                    encode_block(&w, z, &pred[0], &dc[0], &ac[0]);
                }
            int x0 = mx * 8, y0 = my * 8;
            int bw = cw - x0 < 8 ? cw - x0 : 8, bh = ch - y0 < 8 ? ch - y0 : 8;
            if (bw <= 0)
                bw = 1, x0 = cw - 1;
            if (bh <= 0)
                bh = 1, y0 = ch - 1;
            fdct_block(cb + (ptrdiff_t)y0 * c_stride + x0, c_stride, bw, bh, recip[1], z);
            encode_block(&w, z, &pred[1], &dc[1], &ac[1]);
            fdct_block(cr + (ptrdiff_t)y0 * c_stride + x0, c_stride, bw, bh, recip[1], z);
            encode_block(&w, z, &pred[2], &dc[1], &ac[1]);
        }
    if (w.count)
        put_bits(&w, 0x7F, 8 - w.count);
    put_word(&w, 0xFFD9);
    return w.overflow ? 0 : w.length;
}
