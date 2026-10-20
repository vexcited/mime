/*
 * scan converter: edge table with an active edge list, fixed point crossings,
 * integer coverage accumulation, streamed row by row.
 *
 * the sample grid: sub scanline j of a band samples y = j / vs + 0.5 / vs, and
 * horizontal sample k of pixel p sits at x = p + k / hs + 0.5 / hs. a sample is
 * inside a span from xa to xb when xa <= sample < xb, so left and top edges
 * include their samples and right and bottom edges do not. the aliased mode is
 * the case vs = hs = 1.
 */
#include "vector_internal.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#define MAX_STRIP 16384
#define MAX_BAND_ROWS 4096
#define NONE 0xffffffffu

typedef struct Edge {
    int64_t x;  /* 32.32 fixed, relative to the strip left. */
    int64_t dx; /* step per sub scanline. */
    int32_t jend;
    uint32_t next;
    int32_t dir;
    int32_t pad;
} Edge;

typedef struct Act {
    int32_t xc; /* crossing in 16.16, clamped to the strip. */
    int32_t dir;
    uint32_t idx;
} Act;

typedef struct Ctx {
    const MimeAllocator *a;
    const MimeVecPolys *polys;
    const uint8_t *valid;
    bool winding;
    int vshift;
    int sh;       /* log2 of the horizontal units per pixel. */
    bool exact;   /* units are 16.16 fractions rather than samples. */
    int tbits;    /* log2 of the full coverage count. */
    uint32_t half;
    Edge *edges;
    uint32_t nedges, cap_edges;
    Act *act;
    uint32_t cap_act;
    uint32_t *head;
    int32_t *acc, *dif;
    uint8_t *out;
    MimeVecSpanFn fn;
    void *user;
    /* the current pass. */
    float ox, oy;
    int xorg, yorg;
    int W, H;
    int32_t wfix;
    int32_t jmax;
    int minx, maxx;
    int err;
} Ctx;

static inline int32_t clamp_fix(int64_t v, int32_t wfix)
{
    if (v < 0)
        return 0;
    if (v > wfix)
        return wfix;
    return (int32_t)v;
}

/* float to 32.32. the value must be within +-2^30. */
static inline int64_t to_fix64(float v)
{
    float fl = mime_vec_floorf(v);
    int32_t ip = (int32_t)fl;
    int32_t fi = (int32_t)((v - fl) * 2147483648.0f);
    return ((int64_t)ip * 4294967296LL) + ((int64_t)fi * 2);
}

static inline int32_t ceil_to_int(float t)
{
    int32_t i = (int32_t)t;
    return (float)i < t ? i + 1 : i;
}

static void add_edge(Ctx *c, float ax, float ay, float bx, float by)
{
    int dir = 1;
    if (ay == by)
        return;
    if (ay > by) {
        float t = ax;
        ax = bx;
        bx = t;
        t = ay;
        ay = by;
        by = t;
        dir = -1;
    }
    float vs = (float)(1 << c->vshift);
    float jmaxf = (float)c->jmax;
    float t0 = ay * vs - 0.5f, t1 = by * vs - 0.5f;
    if (!(t1 > 0.0f) || !(t0 < jmaxf))
        return;
    if (t0 < 0.0f)
        t0 = 0.0f;
    if (t1 > jmaxf)
        t1 = jmaxf;
    int32_t jt = ceil_to_int(t0), jb = ceil_to_int(t1);
    if (jt >= jb)
        return;
    float dy = by - ay;
    float dxe = bx - ax;
    float inv_vs = 1.0f / vs;
    float u0 = (((float)jt + 0.5f) * inv_vs - ay) / dy;
    float u1 = (((float)(jb - 1) + 0.5f) * inv_vs - ay) / dy;
    u0 = u0 < 0 ? 0 : (u0 > 1 ? 1 : u0);
    u1 = u1 < 0 ? 0 : (u1 > 1 ? 1 : u1);
    float xf = ax + u0 * dxe, xl = ax + u1 * dxe;
    float wf = (float)c->W;
    /* edges wholly right of the strip only matter as a closing at the right side. */
    if (xf >= wf && xl >= wf)
        return;
    float slope = 0.0f;
    if (xf <= 0.0f && xl <= 0.0f) {
        xf = 0.0f;
    } else if (jb - jt > 1) {
        slope = (xl - xf) / (float)(jb - jt - 1);
    }
    if (c->nedges >= c->cap_edges) {
        int r = vec_grow(c->a, (void **)&c->edges, &c->cap_edges, c->nedges + 1, sizeof(Edge), MIME_VEC_MAX_POINTS);
        if (r) {
            c->err = r;
            return;
        }
    }
    uint32_t id = c->nedges++;
    Edge *e = &c->edges[id];
    e->x = to_fix64(xf);
    e->dx = to_fix64(slope);
    e->jend = jb;
    e->dir = dir;
    e->pad = 0;
    e->next = c->head[jt];
    c->head[jt] = id;
}

static void build_edges(Ctx *c)
{
    const MimeVecPolys *p = c->polys;
    for (uint32_t s = 0; s < p->nsubs && !c->err; s++) {
        if (!c->valid[s])
            continue;
        const MimeVecPoint *pt = p->pts + p->subs[s].first;
        uint32_t n = p->subs[s].count;
        float px = mime_vec_clamp_coord(pt[n - 1].x) - c->ox, py = mime_vec_clamp_coord(pt[n - 1].y) - c->oy;
        for (uint32_t i = 0; i < n; i++) {
            float qx = mime_vec_clamp_coord(pt[i].x) - c->ox, qy = mime_vec_clamp_coord(pt[i].y) - c->oy;
            add_edge(c, px, py, qx, qy);
            px = qx;
            py = qy;
        }
    }
}

static void heap_sift(Act *a, uint32_t start, uint32_t end)
{
    uint32_t root = start;
    for (;;) {
        uint32_t child = 2 * root + 1;
        if (child >= end)
            return;
        if (child + 1 < end && a[child].xc < a[child + 1].xc)
            child++;
        if (a[root].xc >= a[child].xc)
            return;
        Act t = a[root];
        a[root] = a[child];
        a[child] = t;
        root = child;
    }
}

static void heap_sort(Act *a, uint32_t n)
{
    if (n < 2)
        return;
    for (uint32_t i = n / 2; i-- > 0;)
        heap_sift(a, i, n);
    for (uint32_t end = n; end-- > 1;) {
        Act t = a[0];
        a[0] = a[end];
        a[end] = t;
        heap_sift(a, 0, end);
    }
}

/* insertion sort, which suits an almost sorted list, with a heap sort when it degrades. */
static void sort_act(Act *a, uint32_t n)
{
    int64_t budget = (int64_t)n * 8 + 64;
    for (uint32_t i = 1; i < n; i++) {
        if (a[i - 1].xc <= a[i].xc)
            continue;
        Act t = a[i];
        uint32_t j = i;
        while (j > 0 && a[j - 1].xc > t.xc) {
            a[j] = a[j - 1];
            j--;
            budget--;
        }
        a[j] = t;
        if (budget < 0) {
            heap_sort(a, n);
            return;
        }
    }
}

static inline void accumulate(Ctx *c, int32_t s0, int32_t s1)
{
    if (s1 <= s0)
        return;
    int sh = c->sh;
    int32_t u = 1 << sh, m = u - 1;
    int32_t pa = s0 >> sh, pb = s1 >> sh;
    int32_t *acc = c->acc, *dif = c->dif;
    if (pa == pb) {
        acc[pa] += s1 - s0;
    } else {
        acc[pa] += u - (s0 & m);
        dif[pa + 1] += u;
        dif[pb] -= u;
        acc[pb] += s1 & m;
    }
    if (pa < c->minx)
        c->minx = pa;
    int32_t last = (s1 - 1) >> sh;
    if (last > c->maxx)
        c->maxx = last;
}

static inline int32_t to_units(const Ctx *c, int32_t xc)
{
    if (c->exact)
        return xc;
    int sh = c->sh;
    return (xc + ((1 << (15 - sh)) - 1)) >> (16 - sh);
}

static inline void add_span(Ctx *c, int32_t xa, int32_t xb)
{
    accumulate(c, to_units(c, xa), to_units(c, xb));
}

static void flush_row(Ctx *c, int row)
{
    int minx = c->minx, maxx = c->maxx;
    c->minx = 0x7fffffff;
    c->maxx = -1;
    if (maxx < minx)
        return;
    int32_t *acc = c->acc, *dif = c->dif;
    uint8_t *out = c->out;
    int tb = c->tbits;
    uint32_t half = c->half;
    int32_t run = 0;
    uint32_t last = 0;
    int n = maxx - minx + 1;
    int i = 0;
    int x = minx;
#if defined(__SSE2__)
    __m128i zero = _mm_setzero_si128();
    while (i + 4 <= n) {
        __m128i d = _mm_loadu_si128((const __m128i *)(dif + x));
        __m128i a = _mm_loadu_si128((const __m128i *)(acc + x));
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_or_si128(d, a), zero)) == 0xffff) {
            uint32_t v = last * 0x01010101u;
            __builtin_memcpy(out + i, &v, 4);
            i += 4;
            x += 4;
            continue;
        }
        for (int k = 0; k < 4; k++, i++, x++) {
            int32_t dd = dif[x], aa = acc[x];
            if (dd) {
                run += dd;
                last = ((uint32_t)run * 255u + half) >> tb;
                dif[x] = 0;
            }
            if (aa) {
                out[i] = (uint8_t)(((uint32_t)(run + aa) * 255u + half) >> tb);
                acc[x] = 0;
            } else {
                out[i] = (uint8_t)last;
            }
        }
    }
#endif
    for (; i < n; i++, x++) {
        int32_t dd = dif[x], aa = acc[x];
        if (dd) {
            run += dd;
            last = ((uint32_t)run * 255u + half) >> tb;
            dif[x] = 0;
        }
        if (aa) {
            out[i] = (uint8_t)(((uint32_t)(run + aa) * 255u + half) >> tb);
            acc[x] = 0;
        } else {
            out[i] = (uint8_t)last;
        }
    }
    /* the difference array has one entry past the last pixel. */
    dif[maxx + 1] = 0;
    c->fn(c->user, c->yorg + row, c->xorg + minx, n, out);
}

static int render(Ctx *c)
{
    c->nedges = 0;
    c->err = 0;
    uint32_t nhead = (uint32_t)c->jmax + 1;
    __builtin_memset(c->head, 0xff, (size_t)nhead * sizeof(uint32_t));
    build_edges(c);
    if (c->err)
        return c->err;
    if (c->nedges == 0)
        return MIME_VEC_OK;
    if (c->cap_act < c->nedges) {
        Act *na = mime_alloc(c->a, (size_t)c->nedges * sizeof(Act));
        if (!na)
            return MIME_VEC_NOMEM;
        mime_free(c->a, c->act);
        c->act = na;
        c->cap_act = c->nedges;
    }
    Act *act = c->act;
    Edge *edges = c->edges;
    uint32_t naet = 0;
    int32_t wfix = c->wfix;
    int32_t j = 0;
    int cur_row = -1;
    c->minx = 0x7fffffff;
    c->maxx = -1;
    while (j < c->jmax) {
        if (naet == 0) {
            while (j < c->jmax && c->head[j] == NONE)
                j++;
            if (j >= c->jmax)
                break;
        }
        int row = j >> c->vshift;
        if (row != cur_row) {
            if (cur_row >= 0)
                flush_row(c, cur_row);
            cur_row = row;
        }
        for (uint32_t e = c->head[j]; e != NONE; e = edges[e].next) {
            act[naet].xc = clamp_fix(edges[e].x >> 16, wfix);
            act[naet].dir = edges[e].dir;
            act[naet].idx = e;
            naet++;
        }
        sort_act(act, naet);
        if (c->winding) {
            int w = 0;
            int32_t start = 0;
            for (uint32_t k = 0; k < naet; k++) {
                int prev = w;
                w += act[k].dir;
                if (prev == 0) {
                    if (w != 0)
                        start = act[k].xc;
                } else if (w == 0) {
                    add_span(c, start, act[k].xc);
                }
            }
            if (w != 0)
                add_span(c, start, wfix);
        } else {
            uint32_t k = 0;
            for (; k + 1 < naet; k += 2)
                add_span(c, act[k].xc, act[k + 1].xc);
            if (k < naet)
                add_span(c, act[k].xc, wfix);
        }
        /* step to the next sub scanline and retire finished edges. */
        uint32_t m = 0;
        for (uint32_t k = 0; k < naet; k++) {
            Edge *e = &edges[act[k].idx];
            if (e->jend == j + 1)
                continue;
            e->x += e->dx;
            act[m] = act[k];
            act[m].xc = clamp_fix(e->x >> 16, wfix);
            m++;
        }
        naet = m;
        j++;
    }
    if (cur_row >= 0)
        flush_row(c, cur_row);
    return MIME_VEC_OK;
}

MimeVecRect mime_vec_bounds_to_rect(const MimeVecBounds *b)
{
    MimeVecRect r;
    float lim = (float)MIME_VEC_CLIP_LIMIT;
    float l = mime_vec_floorf(b->left), t = mime_vec_floorf(b->top);
    float rr = mime_vec_ceilf(b->right), bb = mime_vec_ceilf(b->bottom);
    l = !(l > -lim) ? -lim : (l > lim ? lim : l);
    t = !(t > -lim) ? -lim : (t > lim ? lim : t);
    rr = !(rr > -lim) ? -lim : (rr > lim ? lim : rr);
    bb = !(bb > -lim) ? -lim : (bb > lim ? lim : bb);
    r.left = (int32_t)l;
    r.top = (int32_t)t;
    r.right = (int32_t)rr;
    r.bottom = (int32_t)bb;
    return r;
}

int mime_vec_fill(const MimeAllocator *a, const MimeVecPolys *polys, int rule, int aa, const MimeVecRect *clip_in,
                  MimeVecSpanFn fn, void *user)
{
    if (!polys || !clip_in || !fn || polys->nsubs == 0)
        return MIME_VEC_OK;
    int64_t cl = clip_in->left, ct = clip_in->top, cr = clip_in->right, cb = clip_in->bottom;
    if (cl < -MIME_VEC_CLIP_LIMIT)
        cl = -MIME_VEC_CLIP_LIMIT;
    if (ct < -MIME_VEC_CLIP_LIMIT)
        ct = -MIME_VEC_CLIP_LIMIT;
    if (cr > MIME_VEC_CLIP_LIMIT)
        cr = MIME_VEC_CLIP_LIMIT;
    if (cb > MIME_VEC_CLIP_LIMIT)
        cb = MIME_VEC_CLIP_LIMIT;
    if (cr <= cl || cb <= ct)
        return MIME_VEC_OK;

    Ctx c;
    __builtin_memset(&c, 0, sizeof(c));
    c.a = a;
    c.polys = polys;
    c.fn = fn;
    c.user = user;
    c.winding = rule == MIME_VEC_FILL_WINDING;
    switch (aa) {
    case MIME_VEC_AA_8X4:
        c.vshift = 2;
        c.sh = 3;
        c.tbits = 5;
        break;
    case MIME_VEC_AA_8X8:
        c.vshift = 3;
        c.sh = 3;
        c.tbits = 6;
        break;
    case MIME_VEC_AA_EXACT:
        c.vshift = 4;
        c.sh = 16;
        c.exact = true;
        c.tbits = 20;
        break;
    default:
        c.vshift = 0;
        c.sh = 0;
        c.tbits = 0;
        break;
    }
    c.half = c.tbits ? 1u << (c.tbits - 1) : 0;

    /* validity and bounds of the polygons. */
    uint8_t *valid = mime_alloc(a, polys->nsubs);
    if (!valid)
        return MIME_VEC_NOMEM;
    float minx = 0, miny = 0, maxx = 0, maxy = 0;
    bool any = false;
    for (uint32_t s = 0; s < polys->nsubs; s++) {
        const MimeVecPoint *pt = polys->pts + polys->subs[s].first;
        uint32_t n = polys->subs[s].count;
        valid[s] = 0;
        if (n < 3)
            continue;
        bool ok = true;
        float l = 1e30f, t = 1e30f, r = -1e30f, b = -1e30f;
        for (uint32_t i = 0; i < n; i++) {
            float x = pt[i].x, y = pt[i].y;
            if (!(x == x) || !(y == y)) {
                ok = false;
                break;
            }
            x = mime_vec_clamp_coord(x);
            y = mime_vec_clamp_coord(y);
            l = x < l ? x : l;
            r = x > r ? x : r;
            t = y < t ? y : t;
            b = y > b ? y : b;
        }
        if (!ok)
            continue;
        valid[s] = 1;
        if (!any) {
            minx = l;
            miny = t;
            maxx = r;
            maxy = b;
            any = true;
        } else {
            minx = l < minx ? l : minx;
            miny = t < miny ? t : miny;
            maxx = r > maxx ? r : maxx;
            maxy = b > maxy ? b : maxy;
        }
    }
    int ret = MIME_VEC_OK;
    if (!any)
        goto done;
    {
        MimeVecBounds bb = {minx, miny, maxx, maxy};
        MimeVecRect br = mime_vec_bounds_to_rect(&bb);
        if (br.left > cl)
            cl = br.left;
        if (br.top > ct)
            ct = br.top;
        if (br.right < cr)
            cr = br.right;
        if (br.bottom < cb)
            cb = br.bottom;
        if (cr <= cl || cb <= ct)
            goto done;
    }
    c.valid = valid;
    {
        int64_t w = cr - cl, h = cb - ct;
        int sw = w > MAX_STRIP ? MAX_STRIP : (int)w;
        int sh_rows = h > MAX_BAND_ROWS ? MAX_BAND_ROWS : (int)h;
        uint32_t nhead = (uint32_t)(sh_rows << c.vshift) + 1;
        c.head = mime_alloc(a, (size_t)nhead * sizeof(uint32_t));
        c.acc = mime_alloc(a, (size_t)(sw + 8) * sizeof(int32_t));
        c.dif = mime_alloc(a, (size_t)(sw + 8) * sizeof(int32_t));
        c.out = mime_alloc(a, (size_t)sw + 8);
        if (!c.head || !c.acc || !c.dif || !c.out) {
            ret = MIME_VEC_NOMEM;
            goto done;
        }
        __builtin_memset(c.acc, 0, (size_t)(sw + 8) * sizeof(int32_t));
        __builtin_memset(c.dif, 0, (size_t)(sw + 8) * sizeof(int32_t));
        for (int64_t y0 = ct; y0 < cb && !ret; y0 += MAX_BAND_ROWS) {
            int64_t y1 = y0 + MAX_BAND_ROWS < cb ? y0 + MAX_BAND_ROWS : cb;
            for (int64_t x0 = cl; x0 < cr && !ret; x0 += MAX_STRIP) {
                int64_t x1 = x0 + MAX_STRIP < cr ? x0 + MAX_STRIP : cr;
                c.ox = (float)x0;
                c.oy = (float)y0;
                c.xorg = (int)x0;
                c.yorg = (int)y0;
                c.W = (int)(x1 - x0);
                c.H = (int)(y1 - y0);
                c.wfix = (int32_t)c.W << 16;
                c.jmax = c.H << c.vshift;
                ret = render(&c);
            }
        }
    }
done:
    mime_free(a, valid);
    mime_free(a, c.edges);
    mime_free(a, c.act);
    mime_free(a, c.head);
    mime_free(a, c.acc);
    mime_free(a, c.dif);
    mime_free(a, c.out);
    return ret;
}

int mime_vec_fill_polygon(const MimeAllocator *a, const MimeVecPoint *pts, uint32_t count, int rule, int aa,
                          const MimeVecRect *clip, MimeVecSpanFn fn, void *user)
{
    MimeVecSub sub = {0, count, 1};
    MimeVecPolys p;
    __builtin_memset(&p, 0, sizeof(p));
    p.alloc = a;
    p.pts = (MimeVecPoint *)pts;
    p.npts = count;
    p.subs = &sub;
    p.nsubs = 1;
    return mime_vec_fill(a, &p, rule, aa, clip, fn, user);
}
