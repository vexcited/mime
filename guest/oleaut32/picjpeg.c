/*
 * jpeg decoding: huffman coded images of 8 bits, sequential and progressive, in one, three or four components.
 * Coefficients of the whole image are kept, then transformed and converted at the end. Chroma is upsampled by
 * repeating samples.
 */
#include "picdecode.h"

#define JPEG_MAX_PIXELS (1u << 27)

typedef struct {
    unsigned char vals[256];
    short look[512];
    int maxcode[18];
    int mincode[18];
    int valptr[17];
    int defined;
} JHUFF;

typedef struct {
    int id, h, v, tq, td, ta;
    int bw, bh;
    int pw, ph;
    int pred;
    short *coef;
    unsigned char *plane;
} JCOMP;

typedef struct {
    const unsigned char *data;
    size_t size, pos;
    unsigned int buf;
    int cnt;
    int marker;
    int width, height, ncomp, hmax, vmax, mcux, mcuy;
    int frame, progressive, interval, adobe, jfif;
    int scans;
    int eobrun;
    int ss, se, ah, al;
    int scan_n, scan[4];
    unsigned short qt[4][64];
    JHUFF dc[4], ac[4];
    JCOMP comp[4];
} JPEG;

static const unsigned char zigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                         12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                         35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                         58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/* the cosines of the transform times 8192, row x and column u. */
static const int cosines[8][8] = {
    {2896, 4017, 3784, 3406, 2896, 2276, 1567, 799},       {2896, 3406, 1567, -799, -2896, -4017, -3784, -2276},
    {2896, 2276, -1567, -4017, -2896, 799, 3784, 3406},    {2896, 799, -3784, -2276, 2896, 3406, -1567, -4017},
    {2896, -799, -3784, 2276, 2896, -3406, -1567, 4017},   {2896, -2276, -1567, 4017, -2896, -799, 3784, -3406},
    {2896, -3406, 1567, 799, -2896, 4017, -3784, 2276},    {2896, -4017, 3784, -3406, 2896, -2276, 1567, -799},
};

/* ------------------------------------------------------------------ bits */

static void jpeg_fill(JPEG *j)
{
    while (j->cnt <= 24) {
        unsigned int byte = 0;
        if (!j->marker && j->pos < j->size) {
            byte = j->data[j->pos];
            if (byte == 0xFF) {
                size_t next = j->pos + 1;
                while (next < j->size && j->data[next] == 0xFF)
                    next++;
                if (next >= j->size) {
                    j->marker = 0xD9;
                    byte = 0;
                } else if (j->data[next] == 0) {
                    j->pos = next + 1;
                } else {
                    /* a marker ends the entropy coded data, which goes on as zero bits. */
                    j->pos = next - 1;
                    j->marker = j->data[next];
                    byte = 0;
                }
            } else {
                j->pos++;
            }
        } else if (!j->marker) {
            j->marker = 0xD9;
        }
        j->buf |= byte << (24 - j->cnt);
        j->cnt += 8;
    }
}

static unsigned int jpeg_bits(JPEG *j, int n)
{
    if (n == 0)
        return 0;
    if (j->cnt < n)
        jpeg_fill(j);
    unsigned int value = j->buf >> (32 - n);
    j->buf <<= n;
    j->cnt -= n;
    return value;
}

static int jpeg_extend(JPEG *j, int s)
{
    int value = (int)jpeg_bits(j, s);
    return value < (1 << (s - 1)) ? value - (1 << s) + 1 : value;
}

static int jpeg_huff(JPEG *j, const JHUFF *h)
{
    if (j->cnt < 16)
        jpeg_fill(j);
    unsigned int code = j->buf >> 16;
    int entry = h->look[code >> 7];
    if (entry) {
        int length = entry >> 8;
        j->buf <<= length;
        j->cnt -= length;
        return entry & 255;
    }
    for (int length = 10; length <= 16; length++) {
        int c = (int)(code >> (16 - length));
        if (c <= h->maxcode[length]) {
            j->buf <<= length;
            j->cnt -= length;
            return h->vals[(h->valptr[length] + c - h->mincode[length]) & 255];
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ blocks */

static int jpeg_block_sequential(JPEG *j, JCOMP *c, short *block)
{
    int s = jpeg_huff(j, &j->dc[c->td]);
    if (s < 0 || s > 11)
        return 0;
    c->pred += s ? jpeg_extend(j, s) : 0;
    block[0] = (short)c->pred;
    for (int k = 1; k < 64; k++) {
        int rs = jpeg_huff(j, &j->ac[c->ta]);
        if (rs < 0)
            return 0;
        int r = rs >> 4;
        s = rs & 15;
        if (s == 0) {
            if (r != 15)
                break;
            k += 15;
            continue;
        }
        k += r;
        if (k > 63)
            return 0;
        block[zigzag[k]] = (short)jpeg_extend(j, s);
    }
    return 1;
}

static int jpeg_block_dc_first(JPEG *j, JCOMP *c, short *block)
{
    int s = jpeg_huff(j, &j->dc[c->td]);
    if (s < 0 || s > 11)
        return 0;
    c->pred += s ? jpeg_extend(j, s) : 0;
    block[0] = (short)(c->pred * (1 << j->al));
    return 1;
}

static int jpeg_block_dc_refine(JPEG *j, short *block)
{
    if (jpeg_bits(j, 1))
        block[0] |= (short)(1 << j->al);
    return 1;
}

static int jpeg_block_ac_first(JPEG *j, JCOMP *c, short *block)
{
    if (j->eobrun > 0) {
        j->eobrun--;
        return 1;
    }
    for (int k = j->ss; k <= j->se; k++) {
        int rs = jpeg_huff(j, &j->ac[c->ta]);
        if (rs < 0)
            return 0;
        int r = rs >> 4, s = rs & 15;
        if (s) {
            k += r;
            if (k > 63)
                return 0;
            block[zigzag[k]] = (short)(jpeg_extend(j, s) * (1 << j->al));
        } else if (r == 15) {
            k += 15;
        } else {
            j->eobrun = 1 << r;
            if (r)
                j->eobrun += (int)jpeg_bits(j, r);
            j->eobrun--;
            break;
        }
    }
    return 1;
}

static void jpeg_refine_one(JPEG *j, short *coef, int plus, int minus)
{
    if (jpeg_bits(j, 1) && (*coef & plus) == 0)
        *coef = (short)(*coef >= 0 ? *coef + plus : *coef + minus);
}

static int jpeg_block_ac_refine(JPEG *j, JCOMP *c, short *block)
{
    int plus = 1 << j->al, minus = -(1 << j->al);
    int k = j->ss;
    if (j->eobrun == 0) {
        for (; k <= j->se; k++) {
            int rs = jpeg_huff(j, &j->ac[c->ta]);
            if (rs < 0)
                return 0;
            int r = rs >> 4, s = rs & 15, value = 0;
            if (s) {
                value = jpeg_bits(j, 1) ? plus : minus;
            } else if (r != 15) {
                j->eobrun = 1 << r;
                if (r)
                    j->eobrun += (int)jpeg_bits(j, r);
                break;
            }
            do {
                short *coef = &block[zigzag[k]];
                if (*coef != 0) {
                    jpeg_refine_one(j, coef, plus, minus);
                } else if (--r < 0) {
                    break;
                }
                k++;
            } while (k <= j->se);
            if (value && k <= j->se)
                block[zigzag[k]] = (short)value;
        }
    }
    if (j->eobrun > 0) {
        for (; k <= j->se; k++) {
            short *coef = &block[zigzag[k]];
            if (*coef != 0)
                jpeg_refine_one(j, coef, plus, minus);
        }
        j->eobrun--;
    }
    return 1;
}

static int jpeg_block(JPEG *j, JCOMP *c, short *block)
{
    if (!j->progressive)
        return jpeg_block_sequential(j, c, block);
    if (j->ss == 0)
        return j->ah == 0 ? jpeg_block_dc_first(j, c, block) : jpeg_block_dc_refine(j, block);
    return j->ah == 0 ? jpeg_block_ac_first(j, c, block) : jpeg_block_ac_refine(j, c, block);
}

/* ------------------------------------------------------------------ scans */

/* a restart marker at the end of an interval; zero when the scan has to stop. */
static int jpeg_restart(JPEG *j)
{
    j->buf = 0;
    j->cnt = 0;
    if (!j->marker) {
        while (j->pos + 1 < j->size && !(j->data[j->pos] == 0xFF && j->data[j->pos + 1] != 0 && j->data[j->pos + 1] != 0xFF))
            j->pos++;
        if (j->pos + 1 >= j->size)
            return 0;
        j->marker = j->data[j->pos + 1];
    }
    if (j->marker < 0xD0 || j->marker > 0xD7)
        return 0;
    j->pos += 2;
    j->marker = 0;
    for (int i = 0; i < j->scan_n; i++)
        j->comp[j->scan[i]].pred = 0;
    j->eobrun = 0;
    return 1;
}

static int jpeg_scan(JPEG *j)
{
    j->buf = 0;
    j->cnt = 0;
    j->marker = 0;
    j->eobrun = 0;
    for (int i = 0; i < j->scan_n; i++)
        j->comp[j->scan[i]].pred = 0;
    int units;
    if (j->scan_n == 1) {
        JCOMP *c = &j->comp[j->scan[0]];
        units = c->pw * c->ph;
    } else {
        units = j->mcux * j->mcuy;
    }
    for (int unit = 0; unit < units; unit++) {
        if (j->interval && unit && unit % j->interval == 0 && !jpeg_restart(j))
            return unit > 0;
        if (j->scan_n == 1) {
            JCOMP *c = &j->comp[j->scan[0]];
            short *block = c->coef + ((size_t)(unit / c->pw) * c->bw + unit % c->pw) * 64;
            if (!jpeg_block(j, c, block))
                return 1;
        } else {
            int mx = unit % j->mcux, my = unit / j->mcux;
            for (int i = 0; i < j->scan_n; i++) {
                JCOMP *c = &j->comp[j->scan[i]];
                for (int v = 0; v < c->v; v++) {
                    for (int h = 0; h < c->h; h++) {
                        short *block = c->coef + ((size_t)(my * c->v + v) * c->bw + mx * c->h + h) * 64;
                        if (!jpeg_block(j, c, block))
                            return 1;
                    }
                }
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ segments */

static int jpeg_u16(const unsigned char *p)
{
    return p[0] << 8 | p[1];
}

static int jpeg_dqt(JPEG *j, const unsigned char *p, int length)
{
    while (length > 0) {
        int precision = p[0] >> 4, id = p[0] & 15;
        int need = 1 + 64 * (precision ? 2 : 1);
        if (precision > 1 || id > 3 || length < need)
            return 0;
        for (int i = 0; i < 64; i++)
            j->qt[id][zigzag[i]] = precision ? (unsigned short)jpeg_u16(p + 1 + 2 * i) : p[1 + i];
        p += need;
        length -= need;
    }
    return 1;
}

static int jpeg_dht(JPEG *j, const unsigned char *p, int length)
{
    while (length >= 17) {
        int class = p[0] >> 4, id = p[0] & 15;
        int count = 0;
        for (int i = 1; i <= 16; i++)
            count += p[i];
        if (class > 1 || id > 3 || count > 256 || length < 17 + count)
            return 0;
        JHUFF *h = class ? &j->ac[id] : &j->dc[id];
        memset(h, 0, sizeof(*h));
        memcpy(h->vals, p + 17, count);
        int code = 0, k = 0;
        for (int l = 1; l <= 16; l++) {
            h->mincode[l] = code;
            h->valptr[l] = k;
            for (int i = 0; i < p[l]; i++, k++, code++) {
                if (l <= 9) {
                    int first = code << (9 - l), last = ((code + 1) << (9 - l)) - 1;
                    if (last > 511)
                        return 0;
                    for (int e = first; e <= last; e++)
                        h->look[e] = (short)(l << 8 | h->vals[k]);
                }
            }
            h->maxcode[l] = p[l] ? code - 1 : -1;
            if (code > (1 << l))
                return 0;
            code <<= 1;
        }
        h->defined = 1;
        p += 17 + count;
        length -= 17 + count;
    }
    return length == 0;
}

static int jpeg_sof(JPEG *j, const unsigned char *p, int length, int progressive)
{
    if (j->frame || length < 6 || p[0] != 8)
        return 0;
    j->height = jpeg_u16(p + 1);
    j->width = jpeg_u16(p + 3);
    j->ncomp = p[5];
    if (j->width <= 0 || j->height <= 0 || (j->ncomp != 1 && j->ncomp != 3 && j->ncomp != 4) || length < 6 + 3 * j->ncomp)
        return 0;
    if ((unsigned)j->width * (unsigned)j->height > JPEG_MAX_PIXELS)
        return 0;
    j->hmax = j->vmax = 1;
    for (int i = 0; i < j->ncomp; i++) {
        JCOMP *c = &j->comp[i];
        c->id = p[6 + 3 * i];
        c->h = p[7 + 3 * i] >> 4;
        c->v = p[7 + 3 * i] & 15;
        c->tq = p[8 + 3 * i];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3)
            return 0;
        if (c->h > j->hmax)
            j->hmax = c->h;
        if (c->v > j->vmax)
            j->vmax = c->v;
    }
    j->mcux = (j->width + 8 * j->hmax - 1) / (8 * j->hmax);
    j->mcuy = (j->height + 8 * j->vmax - 1) / (8 * j->vmax);
    for (int i = 0; i < j->ncomp; i++) {
        JCOMP *c = &j->comp[i];
        c->bw = j->mcux * c->h;
        c->bh = j->mcuy * c->v;
        c->pw = ((j->width * c->h + j->hmax - 1) / j->hmax + 7) / 8;
        c->ph = ((j->height * c->v + j->vmax - 1) / j->vmax + 7) / 8;
        c->coef = AUT_ALLOC((size_t)c->bw * c->bh * 64 * sizeof(short));
        if (!c->coef)
            return 0;
    }
    j->progressive = progressive;
    j->frame = 1;
    return 1;
}

static int jpeg_sos(JPEG *j, const unsigned char *p, int length)
{
    if (!j->frame || length < 1)
        return 0;
    int n = p[0];
    if (n < 1 || n > j->ncomp || length < 1 + 2 * n + 3)
        return 0;
    j->scan_n = n;
    for (int i = 0; i < n; i++) {
        int found = -1;
        for (int k = 0; k < j->ncomp; k++) {
            if (j->comp[k].id == p[1 + 2 * i])
                found = k;
        }
        if (found < 0)
            return 0;
        j->scan[i] = found;
        j->comp[found].td = p[2 + 2 * i] >> 4;
        j->comp[found].ta = p[2 + 2 * i] & 15;
        if (j->comp[found].td > 3 || j->comp[found].ta > 3)
            return 0;
    }
    j->ss = p[1 + 2 * n];
    j->se = p[2 + 2 * n];
    j->ah = p[3 + 2 * n] >> 4;
    j->al = p[3 + 2 * n] & 15;
    if (!j->progressive) {
        j->ss = 0;
        j->se = 63;
        j->ah = j->al = 0;
    } else if (j->ss > j->se || j->se > 63 || j->al > 13 || (j->ss == 0 && j->se != 0) || (j->ss > 0 && n != 1)) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ output */

static unsigned char jpeg_clamp(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : (unsigned char)value;
}

static void jpeg_idct(const short *block, const unsigned short *q, unsigned char *out, int stride)
{
    int input[64];
    int ac = 0;
    for (int i = 0; i < 64; i++) {
        input[i] = block[i] * q[i];
        if (i)
            ac |= input[i];
    }
    if (!ac) {
        unsigned char value = jpeg_clamp(((input[0] + 4) >> 3) + 128);
        for (int y = 0; y < 8; y++)
            memset(out + y * stride, value, 8);
        return;
    }
    int temp[64];
    for (int v = 0; v < 8; v++) {
        for (int x = 0; x < 8; x++) {
            long long sum = 0;
            for (int u = 0; u < 8; u++)
                sum += (long long)cosines[x][u] * input[v * 8 + u];
            temp[v * 8 + x] = (int)((sum + 4096) >> 13);
        }
    }
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            long long sum = 0;
            for (int v = 0; v < 8; v++)
                sum += (long long)cosines[y][v] * temp[v * 8 + x];
            out[y * stride + x] = jpeg_clamp((int)((sum + 4096) >> 13) + 128);
        }
    }
}

static int jpeg_planes(JPEG *j)
{
    for (int i = 0; i < j->ncomp; i++) {
        JCOMP *c = &j->comp[i];
        int stride = c->bw * 8;
        c->plane = AUT_ALLOC((size_t)stride * c->bh * 8);
        if (!c->plane)
            return 0;
        for (int by = 0; by < c->bh; by++) {
            for (int bx = 0; bx < c->bw; bx++)
                jpeg_idct(c->coef + ((size_t)by * c->bw + bx) * 64, j->qt[c->tq], c->plane + (by * 8) * stride + bx * 8,
                          stride);
        }
    }
    return 1;
}

static int jpeg_image(JPEG *j, AUT_IMAGE *image)
{
    if (!jpeg_planes(j))
        return 0;
    int width = j->width, height = j->height;
    int bytes = j->ncomp == 1 ? 1 : 3;
    unsigned char *pixels = AUT_ALLOC((size_t)width * height * bytes);
    int *xmap = AUT_ALLOC((size_t)width * j->ncomp * sizeof(int));
    if (!pixels || !xmap) {
        AUT_FREE(pixels);
        AUT_FREE(xmap);
        return 0;
    }
    for (int i = 0; i < j->ncomp; i++) {
        for (int x = 0; x < width; x++)
            xmap[i * width + x] = x * j->comp[i].h / j->hmax;
    }
    /* what the color components hold: luma and chroma, or the colors themselves. */
    int ycc = 1;
    if (j->ncomp == 3) {
        if (j->adobe >= 0)
            ycc = j->adobe != 0;
        else if (!j->jfif && j->comp[0].id == 'R' && j->comp[1].id == 'G' && j->comp[2].id == 'B')
            ycc = 0;
    } else if (j->ncomp == 4) {
        ycc = j->adobe == 2;
    }
    for (int y = 0; y < height; y++) {
        const unsigned char *rows[4];
        for (int i = 0; i < j->ncomp; i++)
            rows[i] = j->comp[i].plane + (size_t)(y * j->comp[i].v / j->vmax) * j->comp[i].bw * 8;
        unsigned char *out = pixels + (size_t)y * width * bytes;
        for (int x = 0; x < width; x++) {
            int s[4];
            for (int i = 0; i < j->ncomp; i++)
                s[i] = rows[i][xmap[i * width + x]];
            if (j->ncomp == 1) {
                out[x] = (unsigned char)s[0];
                continue;
            }
            int r = s[0], g = s[1], b = s[2];
            if (ycc) {
                int cb = s[1] - 128, cr = s[2] - 128;
                r = jpeg_clamp(s[0] + ((91881 * cr + 32768) >> 16));
                g = jpeg_clamp(s[0] - ((22554 * cb + 46802 * cr + 32768) >> 16));
                b = jpeg_clamp(s[0] + ((116130 * cb + 32768) >> 16));
            }
            if (j->ncomp == 4) {
                /* adobe stores cmyk inverted, and ycck holds the inverted colors as luma and chroma. */
                if (ycc) {
                    r = 255 - r;
                    g = 255 - g;
                    b = 255 - b;
                }
                r = r * s[3] / 255;
                g = g * s[3] / 255;
                b = b * s[3] / 255;
            }
            out[x * 3] = (unsigned char)b;
            out[x * 3 + 1] = (unsigned char)g;
            out[x * 3 + 2] = (unsigned char)r;
        }
    }
    AUT_FREE(xmap);
    memset(image, 0, sizeof(*image));
    image->width = width;
    image->height = height;
    image->bpp = j->ncomp == 1 ? 8 : 24;
    image->transparent = -1;
    if (j->ncomp == 1) {
        image->colors = 256;
        for (int i = 0; i < 256; i++)
            image->palette[i][0] = image->palette[i][1] = image->palette[i][2] = (unsigned char)i;
    }
    image->pixels = pixels;
    return 1;
}

/* ------------------------------------------------------------------ entry */

int AutDecodeJpeg(const unsigned char *data, size_t size, AUT_IMAGE *image)
{
    JPEG *j = AUT_ALLOC(sizeof(JPEG));
    int ok = 0;
    if (!j)
        return 0;
    j->data = data;
    j->size = size;
    j->adobe = -1;
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8)
        goto done;
    j->pos = 2;
    for (;;) {
        /* the next marker, past any fill bytes. */
        while (j->pos < size && data[j->pos] != 0xFF)
            j->pos++;
        while (j->pos < size && data[j->pos] == 0xFF)
            j->pos++;
        if (j->pos >= size)
            break;
        int marker = data[j->pos++];
        if (marker == 0xD9)
            break;
        if (marker == 0 || marker == 1 || (marker >= 0xD0 && marker <= 0xD8))
            continue;
        if (j->pos + 2 > size)
            break;
        int length = jpeg_u16(data + j->pos);
        if (length < 2 || j->pos + length > size)
            break;
        const unsigned char *body = data + j->pos + 2;
        int body_length = length - 2;
        j->pos += length;
        if (marker == 0xDB) {
            if (!jpeg_dqt(j, body, body_length))
                goto done;
        } else if (marker == 0xC4) {
            if (!jpeg_dht(j, body, body_length))
                goto done;
        } else if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (!jpeg_sof(j, body, body_length, marker == 0xC2))
                goto done;
        } else if (marker >= 0xC3 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            goto done;
        } else if (marker == 0xDD) {
            if (body_length < 2)
                goto done;
            j->interval = jpeg_u16(body);
        } else if (marker == 0xE0) {
            if (body_length >= 5 && !memcmp(body, "JFIF", 5))
                j->jfif = 1;
        } else if (marker == 0xEE) {
            if (body_length >= 12 && !memcmp(body, "Adobe", 5))
                j->adobe = body[11];
        } else if (marker == 0xDA) {
            if (!jpeg_sos(j, body, body_length))
                goto done;
            if (!jpeg_scan(j))
                break;
            j->scans++;
        }
    }
    /* a file cut short still gives the scans it had. */
    if (!j->frame || !j->scans)
        goto done;
    ok = jpeg_image(j, image);
done:
    for (int i = 0; i < 4; i++) {
        AUT_FREE(j->comp[i].coef);
        AUT_FREE(j->comp[i].plane);
    }
    AUT_FREE(j);
    return ok;
}
