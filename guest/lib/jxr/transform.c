#include "internal.h"

/* ------------------------------------------------------------------ lifting steps */

static void t2x2h(int32_t *a, int32_t *b, int32_t *c, int32_t *d, int round)
{
    *a += *d;
    *b -= *c;
    int32_t t1 = (*a - *b + round) >> 1;
    int32_t t2 = *c;
    *c = t1 - *d;
    *d = t1 - t2;
    *a -= *d;
    *b += *c;
}

static void inv_todd(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c1 += *c3;
    *c0 -= *c2;
    *c3 -= *c1 >> 1;
    *c2 += (*c0 + 1) >> 1;
    *c0 -= (3 * *c1 + 4) >> 3;
    *c1 += (3 * *c0 + 4) >> 3;
    *c2 -= (3 * *c3 + 4) >> 3;
    *c3 += (3 * *c2 + 4) >> 3;
    *c2 -= (*c1 + 1) >> 1;
    *c3 = ((*c0 + 1) >> 1) - *c3;
    *c1 += *c2;
    *c0 -= *c3;
}

static void fwd_todd(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c1 -= *c2;
    *c0 += *c3;
    *c2 += (*c1 + 1) >> 1;
    *c3 = ((*c0 + 1) >> 1) - *c3;
    *c1 -= (3 * *c0 + 4) >> 3;
    *c0 += (3 * *c1 + 4) >> 3;
    *c3 -= (3 * *c2 + 4) >> 3;
    *c2 += (3 * *c3 + 4) >> 3;
    *c3 += *c1 >> 1;
    *c2 -= (*c0 + 1) >> 1;
    *c1 -= *c3;
    *c0 += *c2;
}

static void inv_toddodd(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c3 += *c0;
    *c2 -= *c1;
    int32_t t1 = *c3 >> 1, t2 = *c2 >> 1;
    *c0 -= t1;
    *c1 += t2;
    *c0 -= (*c1 * 3 + 3) >> 3;
    *c1 += (*c0 * 3 + 3) >> 2;
    *c0 -= (*c1 * 3 + 4) >> 3;
    *c1 -= t2;
    *c0 += t1;
    *c2 += *c1;
    *c3 -= *c0;
    *c1 = -*c1;
    *c2 = -*c2;
}

static void fwd_toddodd(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c1 = -*c1;
    *c2 = -*c2;
    *c3 += *c0;
    *c2 -= *c1;
    int32_t t1 = *c3 >> 1, t2 = *c2 >> 1;
    *c0 -= t1;
    *c1 += t2;
    *c0 += (*c1 * 3 + 4) >> 3;
    *c1 -= (*c0 * 3 + 3) >> 2;
    *c0 += (*c1 * 3 + 3) >> 3;
    *c1 -= t2;
    *c0 += t1;
    *c2 += *c1;
    *c3 -= *c0;
}

static void ict4x4(int32_t *c)
{
    static const uint8_t perm[16] = {0, 8, 4, 13, 2, 15, 3, 14, 1, 12, 5, 9, 7, 11, 6, 10};
    int32_t t[16];
    for (int i = 0; i < 16; i++)
        t[perm[i]] = c[i];
    t2x2h(&t[0], &t[1], &t[4], &t[5], 1);
    inv_todd(&t[2], &t[3], &t[6], &t[7]);
    inv_todd(&t[8], &t[12], &t[9], &t[13]);
    inv_toddodd(&t[10], &t[11], &t[14], &t[15]);
    t2x2h(&t[0], &t[3], &t[12], &t[15], 0);
    t2x2h(&t[5], &t[6], &t[9], &t[10], 0);
    t2x2h(&t[1], &t[2], &t[13], &t[14], 0);
    t2x2h(&t[4], &t[7], &t[8], &t[11], 0);
    jxr_memcpy(c, t, sizeof(t));
}

static void fct4x4(int32_t *c)
{
    static const uint8_t perm[16] = {0, 8, 4, 6, 2, 10, 14, 12, 1, 11, 15, 13, 9, 3, 7, 5};
    t2x2h(&c[0], &c[3], &c[12], &c[15], 0);
    t2x2h(&c[5], &c[6], &c[9], &c[10], 0);
    t2x2h(&c[1], &c[2], &c[13], &c[14], 0);
    t2x2h(&c[4], &c[7], &c[8], &c[11], 0);
    t2x2h(&c[0], &c[1], &c[4], &c[5], 1);
    fwd_todd(&c[2], &c[3], &c[6], &c[7]);
    fwd_todd(&c[8], &c[12], &c[9], &c[13]);
    fwd_toddodd(&c[10], &c[11], &c[14], &c[15]);
    int32_t t[16];
    for (int i = 0; i < 16; i++)
        t[perm[i]] = c[i];
    jxr_memcpy(c, t, sizeof(t));
}

static void inv_rotate(int32_t *a, int32_t *b)
{
    *a -= (*b + 1) >> 1;
    *b += (*a + 1) >> 1;
}

static void fwd_rotate(int32_t *a, int32_t *b)
{
    *b -= (*a + 1) >> 1;
    *a += (*b + 1) >> 1;
}

static void inv_scale(int32_t *a, int32_t *b)
{
    *a += *b;
    *b = (*a >> 1) - *b;
    *a += (*b * 3 + 0) >> 3;
    *b += (*a * 3 + 0) >> 4;
    *b += *a >> 7;
    *b -= *a >> 10;
}

static void fwd_scale(int32_t *a, int32_t *b)
{
    *b -= (*a * 3 + 0) >> 4;
    *b -= *a >> 7;
    *b += *a >> 10;
    *a -= (*b * 3 + 0) >> 3;
    *b = (*a >> 1) - *b;
    *a -= *b;
}

static void t2x2h_post(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c1 -= *c2;
    *c0 += (*c3 * 3 + 4) >> 3;
    *c3 -= *c1 >> 1;
    *c2 = ((*c0 - *c1) >> 1) - *c2;
    int32_t t = *c2;
    *c2 = *c3;
    *c3 = t;
    *c0 -= *c3;
    *c1 += *c2;
}

static void t2x2h_enc(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c0 += *c3;
    *c1 -= *c2;
    int32_t t1 = *c3, t2 = *c2;
    *c2 = ((*c0 - *c1) >> 1) - t1;
    *c3 = t2 + (*c1 >> 1);
    *c1 += *c2;
    *c0 -= (*c3 * 3 + 4) >> 3;
}

static void inv_toddodd_post(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c3 += *c0;
    *c2 -= *c1;
    int32_t t1 = *c3 >> 1, t2 = *c2 >> 1;
    *c0 -= t1;
    *c1 += t2;
    *c0 -= (*c1 * 3 + 6) >> 3;
    *c1 += (*c0 * 3 + 2) >> 2;
    *c0 -= (*c1 * 3 + 4) >> 3;
    *c1 -= t2;
    *c0 += t1;
    *c2 += *c1;
    *c3 -= *c0;
}

static void fwd_toddodd_pre(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c3 += *c0;
    *c2 -= *c1;
    int32_t t1 = *c3 >> 1, t2 = *c2 >> 1;
    *c0 -= t1;
    *c1 += t2;
    *c0 += (*c1 * 3 + 4) >> 3;
    *c1 -= (*c0 * 3 + 2) >> 2;
    *c0 += (*c1 * 3 + 6) >> 3;
    *c1 -= t2;
    *c0 += t1;
    *c2 += *c1;
    *c3 -= *c0;
}

static void post4x4(int32_t *const *c)
{
    t2x2h(c[0], c[3], c[12], c[15], 0);
    t2x2h(c[1], c[2], c[13], c[14], 0);
    t2x2h(c[4], c[7], c[8], c[11], 0);
    t2x2h(c[5], c[6], c[9], c[10], 0);
    inv_rotate(c[13], c[12]);
    inv_rotate(c[9], c[8]);
    inv_rotate(c[7], c[3]);
    inv_rotate(c[6], c[2]);
    inv_toddodd_post(c[10], c[11], c[14], c[15]);
    inv_scale(c[0], c[15]);
    inv_scale(c[1], c[14]);
    inv_scale(c[4], c[11]);
    inv_scale(c[5], c[10]);
    t2x2h_post(c[0], c[3], c[12], c[15]);
    t2x2h_post(c[1], c[2], c[13], c[14]);
    t2x2h_post(c[4], c[7], c[8], c[11]);
    t2x2h_post(c[5], c[6], c[9], c[10]);
}

static void pre4x4(int32_t *const *c)
{
    t2x2h_enc(c[0], c[3], c[12], c[15]);
    t2x2h_enc(c[1], c[2], c[13], c[14]);
    t2x2h_enc(c[4], c[7], c[8], c[11]);
    t2x2h_enc(c[5], c[6], c[9], c[10]);
    fwd_scale(c[0], c[15]);
    fwd_scale(c[1], c[14]);
    fwd_scale(c[4], c[11]);
    fwd_scale(c[5], c[10]);
    fwd_rotate(c[13], c[12]);
    fwd_rotate(c[9], c[8]);
    fwd_rotate(c[7], c[3]);
    fwd_rotate(c[6], c[2]);
    fwd_toddodd_pre(c[10], c[11], c[14], c[15]);
    t2x2h(c[0], c[12], c[3], c[15], 0);
    t2x2h(c[1], c[2], c[13], c[14], 0);
    t2x2h(c[4], c[7], c[8], c[11], 0);
    t2x2h(c[5], c[6], c[9], c[10], 0);
}

static void post4(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c0 += *c3;
    *c1 += *c2;
    *c3 -= (*c0 + 1) >> 1;
    *c2 -= (*c1 + 1) >> 1;
    inv_scale(c0, c3);
    inv_scale(c1, c2);
    *c0 += (*c3 * 3 + 4) >> 3;
    *c1 += (*c2 * 3 + 4) >> 3;
    *c3 -= *c0 >> 1;
    *c2 -= *c1 >> 1;
    *c0 += *c3;
    *c1 += *c2;
    *c3 = -*c3;
    *c2 = -*c2;
    inv_rotate(c2, c3);
    *c3 += (*c0 + 1) >> 1;
    *c2 += (*c1 + 1) >> 1;
    *c0 -= *c3;
    *c1 -= *c2;
}

static void pre4(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c0 += *c3;
    *c1 += *c2;
    *c3 -= (*c0 + 1) >> 1;
    *c2 -= (*c1 + 1) >> 1;
    fwd_rotate(c2, c3);
    *c3 = -*c3;
    *c2 = -*c2;
    *c0 -= *c3;
    *c1 -= *c2;
    *c3 += *c0 >> 1;
    *c2 += *c1 >> 1;
    *c0 -= (*c3 * 3 + 4) >> 3;
    *c1 -= (*c2 * 3 + 4) >> 3;
    fwd_scale(c0, c3);
    fwd_scale(c1, c2);
    *c3 += (*c0 + 1) >> 1;
    *c2 += (*c1 + 1) >> 1;
    *c0 -= *c3;
    *c1 -= *c2;
}

static void post2(int32_t *c0, int32_t *c1)
{
    *c1 += (*c0 + 2) >> 2;
    *c0 += (*c1 + 1) >> 1;
    *c0 += *c1 >> 5;
    *c0 += *c1 >> 9;
    *c0 += *c1 >> 13;
    *c1 += (*c0 + 2) >> 2;
}

static void pre2(int32_t *c0, int32_t *c1)
{
    *c1 -= (*c0 + 2) >> 2;
    *c0 -= *c1 >> 13;
    *c0 -= *c1 >> 9;
    *c0 -= *c1 >> 5;
    *c0 -= (*c1 + 1) >> 1;
    *c1 -= (*c0 + 2) >> 2;
}

static void post2x2(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c0 += *c3;
    *c1 += *c2;
    *c3 -= (*c0 + 1) >> 1;
    *c2 -= (*c1 + 1) >> 1;
    post2(c0, c1);
    *c3 += (*c0 + 1) >> 1;
    *c2 += (*c1 + 1) >> 1;
    *c0 -= *c3;
    *c1 -= *c2;
}

static void pre2x2(int32_t *c0, int32_t *c1, int32_t *c2, int32_t *c3)
{
    *c0 += *c3;
    *c1 += *c2;
    *c3 -= (*c0 + 1) >> 1;
    *c2 -= (*c1 + 1) >> 1;
    pre2(c0, c1);
    *c3 += (*c0 + 1) >> 1;
    *c2 += (*c1 + 1) >> 1;
    *c0 -= *c3;
    *c1 -= *c2;
}

/* ------------------------------------------------------------------ overlap filtering of a region */

typedef struct View {
    int32_t *base;
    ptrdiff_t sx, sy;
} View;

static int32_t *at(const View *v, int x, int y) { return v->base + (ptrdiff_t)y * v->sy + (ptrdiff_t)x * v->sx; }

/* the 4x4 filters at block corners, the 4 point ones along the edges and the corners, w and h multiples of 4. */
static void overlap4(const View *v, int x0, int y0, int w, int h, bool pre)
{
    void (*f4)(int32_t *, int32_t *, int32_t *, int32_t *) = pre ? pre4 : post4;
    for (int y = 2; y + 4 <= h - 2; y += 4)
        for (int x = 2; x + 4 <= w - 2; x += 4) {
            int32_t *c[16];
            for (int j = 0; j < 16; j++)
                c[j] = at(v, x0 + x + (j & 3), y0 + y + (j >> 2));
            if (pre)
                pre4x4(c);
            else
                post4x4(c);
        }
    for (int y = 2; y + 4 <= h - 2; y += 4)
        for (int k = 0; k < 4; k++) {
            int x = k < 2 ? k : w - 4 + k;
            f4(at(v, x0 + x, y0 + y), at(v, x0 + x, y0 + y + 1), at(v, x0 + x, y0 + y + 2), at(v, x0 + x, y0 + y + 3));
        }
    for (int x = 2; x + 4 <= w - 2; x += 4)
        for (int k = 0; k < 4; k++) {
            int y = k < 2 ? k : h - 4 + k;
            f4(at(v, x0 + x, y0 + y), at(v, x0 + x + 1, y0 + y), at(v, x0 + x + 2, y0 + y), at(v, x0 + x + 3, y0 + y));
        }
    for (int k = 0; k < 4; k++) {
        int x = k & 1 ? w - 2 : 0, y = k & 2 ? h - 2 : 0;
        f4(at(v, x0 + x, y0 + y), at(v, x0 + x + 1, y0 + y), at(v, x0 + x, y0 + y + 1), at(v, x0 + x + 1, y0 + y + 1));
    }
}

/* the same with 2x2 blocks, for the chroma of 4:2:0 and 4:2:2 dc arrays. */
static void overlap2(const View *v, int x0, int y0, int w, int h, bool pre)
{
    int32_t *corner[4], *next[4];
    for (int k = 0; k < 4; k++) {
        int x = k & 1 ? w - 1 : 0, y = k & 2 ? h - 1 : 0;
        corner[k] = at(v, x0 + x, y0 + y);
        next[k] = at(v, x0 + (k & 1 ? x - 1 : x + 1), y0 + y);
    }
    for (int k = 0; k < 4; k++)
        *corner[k] -= *next[k];
    for (int y = 1; y + 2 <= h - 1; y += 2)
        for (int x = 1; x + 2 <= w - 1; x += 2) {
            int32_t *a = at(v, x0 + x, y0 + y), *b = at(v, x0 + x + 1, y0 + y);
            int32_t *c = at(v, x0 + x, y0 + y + 1), *d = at(v, x0 + x + 1, y0 + y + 1);
            if (pre)
                pre2x2(a, b, c, d);
            else
                post2x2(a, b, c, d);
        }
    void (*f2)(int32_t *, int32_t *) = pre ? pre2 : post2;
    for (int y = 1; y + 2 <= h - 1; y += 2) {
        f2(at(v, x0, y0 + y), at(v, x0, y0 + y + 1));
        f2(at(v, x0 + w - 1, y0 + y), at(v, x0 + w - 1, y0 + y + 1));
    }
    for (int x = 1; x + 2 <= w - 1; x += 2) {
        f2(at(v, x0 + x, y0), at(v, x0 + x + 1, y0));
        f2(at(v, x0 + x, y0 + h - 1), at(v, x0 + x + 1, y0 + h - 1));
    }
    for (int k = 0; k < 4; k++)
        *corner[k] += *next[k];
}

/* runs an overlap pass over the whole plane or over each tile. scale is the samples per macroblock. */
static void overlap_plane(JxrImage *im, const View *v, int sw, int sh, int block, bool pre)
{
    int tx = im->hard_tiling ? im->ntx : 1, ty = im->hard_tiling ? im->nty : 1;
    for (int j = 0; j < ty; j++)
        for (int i = 0; i < tx; i++) {
            int x0 = im->hard_tiling ? (int)im->tile_x[i] * sw : 0;
            int x1 = im->hard_tiling ? (int)im->tile_x[i + 1] * sw : im->mbw * sw;
            int y0 = im->hard_tiling ? (int)im->tile_y[j] * sh : 0;
            int y1 = im->hard_tiling ? (int)im->tile_y[j + 1] * sh : im->mbh * sh;
            if (block == 4)
                overlap4(v, x0, y0, x1 - x0, y1 - y0, pre);
            else
                overlap2(v, x0, y0, x1 - x0, y1 - y0, pre);
        }
}

/* ------------------------------------------------------------------ whole planes */

static void first_level(JxrImage *im, JxrPlane *p, int i, bool inverse)
{
    int w = jxr_comp_w(im, p, i);
    int nb = jxr_blocks(p, i);
    bool sub = i && jxr_chroma_sub(p);
    bool halve = i && p->scaled;
    for (int mby = 0; mby < im->mbh; mby++)
        for (int mbx = 0; mbx < im->mbw; mbx++) {
            int32_t *c[16], t[16];
            for (int b = 0; b < nb; b++) {
                c[b] = jxr_coef(im, p, i, mbx, mby, b, 0);
                t[b] = *c[b];
            }
            if (inverse) {
                if (!sub) {
                    ict4x4(t);
                } else if (p->cf == MIME_JXR_CF_YUV420) {
                    t2x2h(&t[0], &t[1], &t[2], &t[3], 0);
                    int32_t s = t[1];
                    t[1] = t[2];
                    t[2] = s;
                } else {
                    t[0] -= (t[4] + 1) >> 1;
                    t[4] += t[0];
                    t2x2h(&t[0], &t[1], &t[2], &t[3], 0);
                    int32_t s = t[1];
                    t[1] = t[2];
                    t[2] = s;
                    t2x2h(&t[4], &t[6], &t[5], &t[7], 0);
                    s = t[5];
                    t[5] = t[6];
                    t[6] = s;
                }
                if (halve)
                    for (int b = 0; b < nb; b++)
                        t[b] *= 2;
            } else {
                if (halve)
                    for (int b = 0; b < nb; b++)
                        t[b] = (t[b] + 1) >> 1;
                if (!sub) {
                    fct4x4(t);
                } else if (p->cf == MIME_JXR_CF_YUV420) {
                    int32_t s = t[1];
                    t[1] = t[2];
                    t[2] = s;
                    t2x2h(&t[0], &t[1], &t[2], &t[3], 0);
                } else {
                    int32_t s = t[5];
                    t[5] = t[6];
                    t[6] = s;
                    t2x2h(&t[4], &t[6], &t[5], &t[7], 0);
                    s = t[1];
                    t[1] = t[2];
                    t[2] = s;
                    t2x2h(&t[0], &t[1], &t[2], &t[3], 0);
                    t[4] -= t[0];
                    t[0] += (t[4] + 1) >> 1;
                }
            }
            for (int b = 0; b < nb; b++)
                *c[b] = t[b];
        }
    (void)w;
}

static void second_level(JxrImage *im, JxrPlane *p, int i, bool inverse)
{
    int w = jxr_comp_w(im, p, i), h = jxr_comp_h(im, p, i);
    for (int y = 0; y < h; y += 4)
        for (int x = 0; x < w; x += 4) {
            int32_t t[16];
            int32_t *row = p->c[i] + (size_t)y * w + x;
            for (int j = 0; j < 16; j++)
                t[j] = row[(j >> 2) * w + (j & 3)];
            if (inverse)
                ict4x4(t);
            else
                fct4x4(t);
            for (int j = 0; j < 16; j++)
                row[(j >> 2) * w + (j & 3)] = t[j];
        }
}

static void dc_view(JxrImage *im, JxrPlane *p, int i, View *v, int *sw, int *sh, int *block)
{
    int w = jxr_comp_w(im, p, i);
    v->base = p->c[i];
    v->sx = 4;
    v->sy = 4 * (ptrdiff_t)w;
    bool sub = i && jxr_chroma_sub(p);
    *sw = sub ? 2 : 4;
    *sh = sub && p->cf == MIME_JXR_CF_YUV420 ? 2 : 4;
    *block = sub ? 2 : 4;
}

void jxr_inverse(JxrImage *im, JxrPlane *p)
{
    for (int i = 0; i < p->ncomp; i++) {
        first_level(im, p, i, true);
        if (im->overlap == 2) {
            View v;
            int sw, sh, block;
            dc_view(im, p, i, &v, &sw, &sh, &block);
            overlap_plane(im, &v, sw, sh, block, false);
        }
        second_level(im, p, i, true);
        if (im->overlap) {
            View v = {p->c[i], 1, jxr_comp_w(im, p, i)};
            overlap_plane(im, &v, jxr_mbsize_w(p, i), jxr_mbsize_h(p, i), 4, false);
        }
    }
}

void jxr_forward(JxrImage *im, JxrPlane *p)
{
    for (int i = 0; i < p->ncomp; i++) {
        if (im->overlap) {
            View v = {p->c[i], 1, jxr_comp_w(im, p, i)};
            overlap_plane(im, &v, jxr_mbsize_w(p, i), jxr_mbsize_h(p, i), 4, true);
        }
        second_level(im, p, i, false);
        if (im->overlap == 2) {
            View v;
            int sw, sh, block;
            dc_view(im, p, i, &v, &sw, &sh, &block);
            overlap_plane(im, &v, sw, sh, block, true);
        }
        first_level(im, p, i, false);
    }
}
