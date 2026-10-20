/*
 * the scan converter of the drawing core. polygons in device space become rows of coverage, sampled the way
 * gdiplus.dll does it:
 *
 * a vertex is rounded up to 1/16 pixel: the device coordinate is multiplied by 256, rounded, and the result
 * goes up to a multiple of 16. an edge is part of every sample row between its vertices, a row at the top
 * vertex counts and a row at the bottom one does not, and on a row its crossing is where the edge passes, moved
 * up to the next sample column. a span runs from one crossing to the next, left end in and right end out.
 *
 * an aliased fill has one sample per pixel, at the pixel's own integer coordinate. antialiased fills move the
 * geometry half a pixel and sample on a lattice of 8 columns and 4 rows per pixel (8 and 8 for the second
 * mode); a pixel's coverage is the number of its samples inside, 0 to 32 or 64. the half pixel is why a
 * shape at integer coordinates is blurred half a pixel to the left and up unless the pixel offset mode shifts
 * the world by the same amount.
 */
#include "gp_private.h"

typedef struct REdge {
    long long q, r;   /* the crossing in sample units is q + (r != 0) on the current row, N = q * D + r */
    long long sq, sr; /* the step of N from a row to the next, divided by D */
    long long d;      /* D = 16 * dy */
    INT k0, k1;       /* the sample rows: first, and one past the last */
    INT cur;          /* the row q and r are for */
    INT dir;
} REdge;

typedef struct Cross {
    INT col;
    INT dir;
} Cross;

static long long floor_div(long long a, long long b)
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* the fixed point coordinate of a device coordinate, in 1/16 pixel. */
static INT to_fixed(float d)
{
    if (d > 4000000.0f)
        d = 4000000.0f;
    else if (d < -4000000.0f)
        d = -4000000.0f;
    INT n = (INT)mime_vec_floorf(d * 256.0f + 0.5f);
    return (n + 15) >> 4;
}

typedef struct RasterState {
    INT colshift;       /* log2 of the sample columns per pixel */
    INT subshift;       /* log2 of the sample rows per pixel */
    INT cmax;
    INT left, right;    /* the clip in pixels */
    INT top, bottom;
    INT *cnt;
    INT *delta;
    UINT8 *cov;
    INT row;            /* the pixel row being accumulated */
    INT pmin, pmax;     /* the pixels touched on the row */
    BOOL any;
    GSpanFn fn;
    void *user;
} RasterState;

static void row_flush(RasterState *s)
{
    if (!s->any)
        return;
    INT running = 0, run_start = -1;
    for (INT x = s->pmin; x <= s->pmax; x++) {
        INT i = x - s->left;
        running += s->delta[i];
        INT c = s->cnt[i] + running;
        s->cnt[i] = 0;
        s->delta[i] = 0;
        if (x >= s->right)
            break;
        if (c > s->cmax)
            c = s->cmax;
        s->cov[i] = (UINT8)c;
        if (c > 0 && run_start < 0)
            run_start = x;
        if (c == 0 && run_start >= 0) {
            s->fn(s->user, s->row, run_start, x - run_start, s->cov + (run_start - s->left), s->cmax);
            run_start = -1;
        }
    }
    if (run_start >= 0) {
        INT end = s->pmax < s->right ? s->pmax + 1 : s->right;
        s->fn(s->user, s->row, run_start, end - run_start, s->cov + (run_start - s->left), s->cmax);
    }
    s->any = FALSE;
}

static void row_add(RasterState *s, INT cl, INT cr)
{
    INT cols = 1 << s->colshift, mask = cols - 1;
    INT pa = cl >> s->colshift, pb = cr >> s->colshift;
    INT base = s->left;
    if (pa < base) {
        pa = base;
        cl = base << s->colshift;
    }
    if (pb > s->right) {
        pb = s->right;
        cr = s->right << s->colshift;
    }
    if (cr <= cl)
        return;
    if (!s->any) {
        s->pmin = pa;
        s->pmax = pb;
        s->any = TRUE;
    } else {
        if (pa < s->pmin)
            s->pmin = pa;
        if (pb > s->pmax)
            s->pmax = pb;
    }
    if (pa == pb) {
        s->cnt[pa - base] += cr - cl;
        return;
    }
    s->cnt[pa - base] += cols - (cl & mask);
    if (pb > pa + 1) {
        s->delta[pa + 1 - base] += cols;
        s->delta[pb - base] -= cols;
    }
    s->cnt[pb - base] += cr & mask;
}

static void sort_edges(REdge **e, INT n)
{
    /* shell sort by the first row */
    for (INT gap = n / 2; gap > 0; gap /= 2) {
        for (INT i = gap; i < n; i++) {
            REdge *t = e[i];
            INT j = i;
            while (j >= gap && e[j - gap]->k0 > t->k0) {
                e[j] = e[j - gap];
                j -= gap;
            }
            e[j] = t;
        }
    }
}

static void sort_cross(Cross *c, INT n)
{
    if (n < 24) {
        for (INT i = 1; i < n; i++) {
            Cross t = c[i];
            INT j = i;
            while (j > 0 && c[j - 1].col > t.col) {
                c[j] = c[j - 1];
                j--;
            }
            c[j] = t;
        }
        return;
    }
    for (INT gap = n / 2; gap > 0; gap /= 2) {
        for (INT i = gap; i < n; i++) {
            Cross t = c[i];
            INT j = i;
            while (j >= gap && c[j - gap].col > t.col) {
                c[j] = c[j - gap];
                j -= gap;
            }
            c[j] = t;
        }
    }
}

GpStatus gp_raster(const MimeVecPolys *polys, INT fill_rule, INT aa, const RECT *clip, GSpanFn fn, void *user)
{
    if (!polys->npts || clip->right <= clip->left || clip->bottom <= clip->top)
        return Ok;
    RasterState s;
    memset(&s, 0, sizeof(s));
    s.colshift = aa ? 3 : 0;
    s.subshift = aa == 0 ? 0 : (aa == 1 ? 2 : 3);
    s.cmax = aa == 0 ? 1 : (aa == 1 ? 32 : 64);
    s.left = clip->left;
    s.right = clip->right;
    s.top = clip->top;
    s.bottom = clip->bottom;
    s.fn = fn;
    s.user = user;
    INT width = s.right - s.left;
    INT kmin = s.top << s.subshift, kmax = s.bottom << s.subshift;

    /* the edges */
    UINT total = 0;
    for (UINT i = 0; i < polys->nsubs; i++)
        total += polys->subs[i].count;
    REdge *edges = gp_alloc(sizeof(REdge) * (total + 1));
    REdge **order = gp_alloc(sizeof(REdge *) * (total + 1));
    Cross *cross = gp_alloc(sizeof(Cross) * (total + 2));
    REdge **active = gp_alloc(sizeof(REdge *) * (total + 1));
    s.cnt = gp_alloc_zero(sizeof(INT) * (width + 2));
    s.delta = gp_alloc_zero(sizeof(INT) * (width + 2));
    s.cov = gp_alloc(width + 2);
    if (!edges || !order || !cross || !active || !s.cnt || !s.delta || !s.cov) {
        gp_free(edges);
        gp_free(order);
        gp_free(cross);
        gp_free(active);
        gp_free(s.cnt);
        gp_free(s.delta);
        gp_free(s.cov);
        return OutOfMemory;
    }
    INT nedges = 0;
    for (UINT i = 0; i < polys->nsubs; i++) {
        const MimeVecSub *sub = &polys->subs[i];
        if (sub->count < 2)
            continue;
        const MimeVecPoint *pts = polys->pts + sub->first;
        BOOL bad = FALSE;
        for (UINT j = 0; j < sub->count; j++) {
            if (!mime_vec_finite(pts[j].x) || !mime_vec_finite(pts[j].y))
                bad = TRUE;
        }
        if (bad)
            continue;
        for (UINT j = 0; j < sub->count; j++) {
            const MimeVecPoint *p0 = &pts[j], *p1 = &pts[j + 1 == sub->count ? 0 : j + 1];
            INT x0 = to_fixed(p0->x), y0 = to_fixed(p0->y), x1 = to_fixed(p1->x), y1 = to_fixed(p1->y);
            INT xa, ya, xb, yb, dir;
            if (aa) {
                x0 = 8 * x0 + 64;
                x1 = 8 * x1 + 64;
                y0 = (y0 + 8) << s.subshift;
                y1 = (y1 + 8) << s.subshift;
            }
            if (y0 == y1)
                continue;
            if (y0 < y1) {
                xa = x0;
                ya = y0;
                xb = x1;
                yb = y1;
                dir = 1;
            } else {
                xa = x1;
                ya = y1;
                xb = x0;
                yb = y0;
                dir = -1;
            }
            INT k0 = (ya + 15) >> 4, k1 = (yb + 15) >> 4;
            if (k0 >= k1)
                continue;
            if (k0 < kmin)
                k0 = kmin;
            if (k1 > kmax)
                k1 = kmax;
            if (k0 >= k1)
                continue;
            long long dy = yb - ya, dx = xb - xa;
            REdge *e = &edges[nedges];
            e->d = 16 * dy;
            long long n0 = (long long)xa * dy + (long long)(16 * k0 - ya) * dx;
            e->q = floor_div(n0, e->d);
            e->r = n0 - e->q * e->d;
            long long step = 16 * dx;
            e->sq = floor_div(step, e->d);
            e->sr = step - e->sq * e->d;
            e->k0 = k0;
            e->k1 = k1;
            e->cur = k0;
            e->dir = dir;
            order[nedges] = e;
            nedges++;
        }
    }
    GpStatus status = Ok;
    if (nedges) {
        sort_edges(order, nedges);
        INT next = 0, nactive = 0;
        INT k = order[0]->k0;
        INT clip_l = s.left << s.colshift, clip_r = s.right << s.colshift;
        s.row = k >> s.subshift;
        while (next < nedges || nactive) {
            if (!nactive && next < nedges && order[next]->k0 > k)
                k = order[next]->k0;
            INT row = k >> s.subshift;
            if (row != s.row) {
                row_flush(&s);
                s.row = row;
            }
            while (next < nedges && order[next]->k0 <= k)
                active[nactive++] = order[next++];
            INT n = 0;
            for (INT i = 0; i < nactive; i++) {
                REdge *e = active[i];
                if (e->k1 <= k)
                    continue;
                while (e->cur < k) {
                    e->q += e->sq;
                    e->r += e->sr;
                    if (e->r >= e->d) {
                        e->r -= e->d;
                        e->q++;
                    }
                    e->cur++;
                }
                active[n] = e;
                long long c = e->q + (e->r != 0);
                if (c < -0x3fffffff)
                    c = -0x3fffffff;
                else if (c > 0x3fffffff)
                    c = 0x3fffffff;
                cross[n].col = (INT)c;
                cross[n].dir = e->dir;
                n++;
            }
            nactive = n;
            if (n >= 2) {
                sort_cross(cross, n);
                if (fill_rule == MIME_VEC_FILL_WINDING) {
                    INT w = 0, start = 0;
                    for (INT i = 0; i < n; i++) {
                        INT was = w;
                        w += cross[i].dir;
                        if (was == 0 && w != 0)
                            start = cross[i].col;
                        else if (was != 0 && w == 0) {
                            INT cl = start < clip_l ? clip_l : start, cr = cross[i].col > clip_r ? clip_r : cross[i].col;
                            if (cr > cl)
                                row_add(&s, cl, cr);
                        }
                    }
                } else {
                    for (INT i = 0; i + 1 < n; i += 2) {
                        INT cl = cross[i].col < clip_l ? clip_l : cross[i].col;
                        INT cr = cross[i + 1].col > clip_r ? clip_r : cross[i + 1].col;
                        if (cr > cl)
                            row_add(&s, cl, cr);
                    }
                }
            }
            k++;
        }
        row_flush(&s);
    }
    gp_free(edges);
    gp_free(order);
    gp_free(cross);
    gp_free(active);
    gp_free(s.cnt);
    gp_free(s.delta);
    gp_free(s.cov);
    return status;
}
