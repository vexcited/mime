/*
 * coverage masks as sorted bands of runs.
 */
#include "vector_internal.h"

#define MASK_LIMIT (1u << 24)

void mime_vec_mask_init(MimeVecMask *m, const MimeAllocator *alloc)
{
    __builtin_memset(m, 0, sizeof(*m));
    m->alloc = alloc;
}

void mime_vec_mask_free(MimeVecMask *m)
{
    mime_free(m->alloc, m->bands);
    mime_free(m->alloc, m->runs);
    const MimeAllocator *a = m->alloc;
    __builtin_memset(m, 0, sizeof(*m));
    m->alloc = a;
}

void mime_vec_mask_clear(MimeVecMask *m)
{
    m->nbands = 0;
    m->nruns = 0;
    m->failed = false;
    __builtin_memset(&m->bounds, 0, sizeof(m->bounds));
}

/* building. */

static int add_band(MimeVecMask *m, int32_t y0, int32_t y1)
{
    int r = vec_grow(m->alloc, (void **)&m->bands, &m->cap_bands, m->nbands + 1, sizeof(MimeVecBand), MASK_LIMIT);
    if (r)
        return r;
    MimeVecBand *b = &m->bands[m->nbands++];
    b->y0 = y0;
    b->y1 = y1;
    b->first = m->nruns;
    b->count = 0;
    return MIME_VEC_OK;
}

static int add_run(MimeVecMask *m, int32_t x0, int32_t x1, uint32_t v)
{
    MimeVecBand *b = &m->bands[m->nbands - 1];
    if (b->count) {
        MimeVecRun *l = &m->runs[m->nruns - 1];
        if (l->x1 == x0 && l->value == v) {
            l->x1 = x1;
            return MIME_VEC_OK;
        }
    }
    int r = vec_grow(m->alloc, (void **)&m->runs, &m->cap_runs, m->nruns + 1, sizeof(MimeVecRun), MASK_LIMIT);
    if (r)
        return r;
    MimeVecRun *run = &m->runs[m->nruns++];
    run->x0 = x0;
    run->x1 = x1;
    run->value = v;
    b->count++;
    return MIME_VEC_OK;
}

/* closes the band under construction: empty bands vanish, equal neighbors merge. */
static void end_band(MimeVecMask *m)
{
    MimeVecBand *b = &m->bands[m->nbands - 1];
    if (b->count == 0) {
        m->nbands--;
        return;
    }
    if (m->nbands >= 2) {
        MimeVecBand *p = &m->bands[m->nbands - 2];
        if (p->y1 == b->y0 && p->count == b->count &&
            __builtin_memcmp(m->runs + p->first, m->runs + b->first, (size_t)b->count * sizeof(MimeVecRun)) == 0) {
            p->y1 = b->y1;
            m->nruns -= b->count;
            m->nbands--;
        }
    }
}

static void finish(MimeVecMask *m)
{
    if (m->nbands == 0) {
        __builtin_memset(&m->bounds, 0, sizeof(m->bounds));
        return;
    }
    int32_t l = 0x7fffffff, r = -0x7fffffff;
    for (uint32_t i = 0; i < m->nbands; i++) {
        const MimeVecBand *b = &m->bands[i];
        if (m->runs[b->first].x0 < l)
            l = m->runs[b->first].x0;
        if (m->runs[b->first + b->count - 1].x1 > r)
            r = m->runs[b->first + b->count - 1].x1;
    }
    m->bounds.left = l;
    m->bounds.right = r;
    m->bounds.top = m->bands[0].y0;
    m->bounds.bottom = m->bands[m->nbands - 1].y1;
}

/* moves the content of tmp into dst. */
static void take(MimeVecMask *dst, MimeVecMask *tmp)
{
    const MimeAllocator *a = dst->alloc;
    mime_free(a, dst->bands);
    mime_free(a, dst->runs);
    *dst = *tmp;
    dst->alloc = a;
    __builtin_memset(tmp, 0, sizeof(*tmp));
}

int mime_vec_mask_set_rect(MimeVecMask *m, const MimeVecRect *r)
{
    mime_vec_mask_clear(m);
    if (r->right <= r->left || r->bottom <= r->top)
        return MIME_VEC_OK;
    int e = add_band(m, r->top, r->bottom);
    if (!e)
        e = add_run(m, r->left, r->right, 255);
    if (e) {
        mime_vec_mask_clear(m);
        m->failed = true;
        return e;
    }
    end_band(m);
    finish(m);
    return MIME_VEC_OK;
}

int mime_vec_mask_copy(MimeVecMask *dst, const MimeVecMask *src)
{
    if (dst == src)
        return MIME_VEC_OK;
    MimeVecMask t;
    mime_vec_mask_init(&t, dst->alloc);
    int e = vec_grow(t.alloc, (void **)&t.bands, &t.cap_bands, src->nbands ? src->nbands : 1, sizeof(MimeVecBand), MASK_LIMIT);
    if (!e)
        e = vec_grow(t.alloc, (void **)&t.runs, &t.cap_runs, src->nruns ? src->nruns : 1, sizeof(MimeVecRun), MASK_LIMIT);
    if (e) {
        mime_vec_mask_free(&t);
        dst->failed = true;
        return e;
    }
    __builtin_memcpy(t.bands, src->bands, (size_t)src->nbands * sizeof(MimeVecBand));
    __builtin_memcpy(t.runs, src->runs, (size_t)src->nruns * sizeof(MimeVecRun));
    t.nbands = src->nbands;
    t.nruns = src->nruns;
    t.bounds = src->bounds;
    take(dst, &t);
    return MIME_VEC_OK;
}

/* rasterized shapes. */

typedef struct Collect {
    MimeVecMask *m;
    int err;
} Collect;

static void collect_row(void *user, int y, int x, int count, const uint8_t *cov)
{
    Collect *c = user;
    if (c->err)
        return;
    MimeVecMask *m = c->m;
    c->err = add_band(m, y, y + 1);
    if (c->err)
        return;
    int i = 0;
    while (i < count) {
        uint8_t v = cov[i];
        int j = i + 1;
        while (j < count && cov[j] == v)
            j++;
        if (v) {
            c->err = add_run(m, x + i, x + j, v);
            if (c->err)
                return;
        }
        i = j;
    }
    end_band(m);
}

int mime_vec_mask_set_polys(MimeVecMask *m, const MimeVecPolys *polys, int rule, int aa, const MimeVecRect *clip)
{
    MimeVecMask t;
    mime_vec_mask_init(&t, m->alloc);
    Collect c = {&t, 0};
    int e = mime_vec_fill(m->alloc, polys, rule, aa, clip, collect_row, &c);
    if (!e)
        e = c.err;
    if (e) {
        mime_vec_mask_free(&t);
        mime_vec_mask_clear(m);
        m->failed = true;
        return e;
    }
    finish(&t);
    take(m, &t);
    return MIME_VEC_OK;
}

/* combining. */

static inline uint32_t apply_op(int op, uint32_t a, uint32_t b)
{
    switch (op) {
    case MIME_VEC_COMBINE_INTERSECT:
        return mime_vec_div255(a * b);
    case MIME_VEC_COMBINE_UNION:
        return a + b - mime_vec_div255(a * b);
    case MIME_VEC_COMBINE_XOR:
        return mime_vec_div255(a * (255 - b) + b * (255 - a));
    case MIME_VEC_COMBINE_EXCLUDE:
        return mime_vec_div255(a * (255 - b));
    case MIME_VEC_COMBINE_COMPLEMENT:
        return mime_vec_div255(b * (255 - a));
    default:
        return b;
    }
}

static int merge_runs(MimeVecMask *out, const MimeVecRun *A, uint32_t na, const MimeVecRun *B, uint32_t nb, int op)
{
    uint32_t ia = 0, ib = 0;
    int32_t x;
    if (na && nb)
        x = A[0].x0 < B[0].x0 ? A[0].x0 : B[0].x0;
    else if (na)
        x = A[0].x0;
    else if (nb)
        x = B[0].x0;
    else
        return MIME_VEC_OK;
    while (ia < na || ib < nb) {
        uint32_t av = 0, bv = 0;
        int32_t ae, be;
        if (ia < na) {
            if (A[ia].x0 <= x) {
                av = A[ia].value;
                ae = A[ia].x1;
            } else {
                ae = A[ia].x0;
            }
        } else {
            ae = 0x7fffffff;
        }
        if (ib < nb) {
            if (B[ib].x0 <= x) {
                bv = B[ib].value;
                be = B[ib].x1;
            } else {
                be = B[ib].x0;
            }
        } else {
            be = 0x7fffffff;
        }
        int32_t xe = ae < be ? ae : be;
        uint32_t v = apply_op(op, av, bv);
        if (v) {
            int e = add_run(out, x, xe, v);
            if (e)
                return e;
        }
        x = xe;
        if (ia < na && A[ia].x1 <= x)
            ia++;
        if (ib < nb && B[ib].x1 <= x)
            ib++;
    }
    return MIME_VEC_OK;
}

int mime_vec_mask_combine(MimeVecMask *dst, const MimeVecMask *a, const MimeVecMask *b, int op)
{
    if (op == MIME_VEC_COMBINE_REPLACE)
        return mime_vec_mask_copy(dst, b);
    MimeVecMask t;
    mime_vec_mask_init(&t, dst->alloc);
    uint32_t ia = 0, ib = 0;
    int e = MIME_VEC_OK;
    int32_t y = 0;
    if (a->nbands && b->nbands)
        y = a->bands[0].y0 < b->bands[0].y0 ? a->bands[0].y0 : b->bands[0].y0;
    else if (a->nbands)
        y = a->bands[0].y0;
    else if (b->nbands)
        y = b->bands[0].y0;
    while (!e && (ia < a->nbands || ib < b->nbands)) {
        bool in_a = false, in_b = false;
        int32_t ae, be;
        if (ia < a->nbands) {
            if (a->bands[ia].y0 <= y) {
                in_a = true;
                ae = a->bands[ia].y1;
            } else {
                ae = a->bands[ia].y0;
            }
        } else {
            ae = 0x7fffffff;
        }
        if (ib < b->nbands) {
            if (b->bands[ib].y0 <= y) {
                in_b = true;
                be = b->bands[ib].y1;
            } else {
                be = b->bands[ib].y0;
            }
        } else {
            be = 0x7fffffff;
        }
        int32_t ye = ae < be ? ae : be;
        if (in_a || in_b) {
            e = add_band(&t, y, ye);
            if (!e) {
                const MimeVecRun *ra = in_a ? a->runs + a->bands[ia].first : NULL;
                const MimeVecRun *rb = in_b ? b->runs + b->bands[ib].first : NULL;
                e = merge_runs(&t, ra, in_a ? a->bands[ia].count : 0, rb, in_b ? b->bands[ib].count : 0, op);
                if (!e)
                    end_band(&t);
            }
        }
        y = ye;
        if (ia < a->nbands && a->bands[ia].y1 <= y)
            ia++;
        if (ib < b->nbands && b->bands[ib].y1 <= y)
            ib++;
    }
    if (e) {
        mime_vec_mask_free(&t);
        dst->failed = true;
        return e;
    }
    finish(&t);
    take(dst, &t);
    return MIME_VEC_OK;
}

int mime_vec_mask_invert(MimeVecMask *dst, const MimeVecMask *src, const MimeVecRect *r)
{
    MimeVecMask rect;
    mime_vec_mask_init(&rect, dst->alloc);
    int e = mime_vec_mask_set_rect(&rect, r);
    if (!e)
        e = mime_vec_mask_combine(dst, &rect, src, MIME_VEC_COMBINE_EXCLUDE);
    mime_vec_mask_free(&rect);
    return e;
}

void mime_vec_mask_translate(MimeVecMask *m, int dx, int dy)
{
    for (uint32_t i = 0; i < m->nbands; i++) {
        m->bands[i].y0 += dy;
        m->bands[i].y1 += dy;
    }
    for (uint32_t i = 0; i < m->nruns; i++) {
        m->runs[i].x0 += dx;
        m->runs[i].x1 += dx;
    }
    if (m->nbands) {
        m->bounds.left += dx;
        m->bounds.right += dx;
        m->bounds.top += dy;
        m->bounds.bottom += dy;
    }
}

/* queries. */

bool mime_vec_mask_is_empty(const MimeVecMask *m)
{
    return m->nbands == 0;
}

bool mime_vec_mask_is_rect(const MimeVecMask *m, MimeVecRect *r)
{
    if (m->nbands == 1 && m->bands[0].count == 1 && m->runs[m->bands[0].first].value == 255) {
        if (r) {
            r->left = m->runs[m->bands[0].first].x0;
            r->right = m->runs[m->bands[0].first].x1;
            r->top = m->bands[0].y0;
            r->bottom = m->bands[0].y1;
        }
        return true;
    }
    return false;
}

MimeVecRect mime_vec_mask_bounds(const MimeVecMask *m)
{
    return m->bounds;
}

/* first band whose bottom is below y. */
static uint32_t find_band(const MimeVecMask *m, int y)
{
    uint32_t lo = 0, hi = m->nbands;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (m->bands[mid].y1 <= y)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* first run of the band whose right end is beyond x. */
static uint32_t find_run(const MimeVecRun *runs, uint32_t count, int x)
{
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (runs[mid].x1 <= x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

uint32_t mime_vec_mask_coverage_at(const MimeVecMask *m, int x, int y)
{
    uint32_t bi = find_band(m, y);
    if (bi >= m->nbands || m->bands[bi].y0 > y)
        return 0;
    const MimeVecBand *b = &m->bands[bi];
    const MimeVecRun *runs = m->runs + b->first;
    uint32_t ri = find_run(runs, b->count, x);
    if (ri >= b->count || runs[ri].x0 > x)
        return 0;
    return runs[ri].value;
}

int mime_vec_mask_test_rect(const MimeVecMask *m, const MimeVecRect *r)
{
    if (r->right <= r->left || r->bottom <= r->top)
        return MIME_VEC_TEST_OUTSIDE;
    bool any = false, all = true;
    int32_t y = r->top;
    for (uint32_t bi = find_band(m, r->top); bi < m->nbands && m->bands[bi].y0 < r->bottom; bi++) {
        const MimeVecBand *b = &m->bands[bi];
        if (b->y0 > y)
            all = false;
        const MimeVecRun *runs = m->runs + b->first;
        int32_t x = r->left;
        for (uint32_t ri = find_run(runs, b->count, r->left); ri < b->count && runs[ri].x0 < r->right; ri++) {
            any = true;
            if (runs[ri].x0 > x || runs[ri].value != 255)
                all = false;
            if (runs[ri].x1 > x)
                x = runs[ri].x1;
        }
        if (x < r->right)
            all = false;
        y = b->y1;
        if (!all && any)
            return MIME_VEC_TEST_PARTIAL;
    }
    if (y < r->bottom)
        all = false;
    if (!any)
        return MIME_VEC_TEST_OUTSIDE;
    return all ? MIME_VEC_TEST_INSIDE : MIME_VEC_TEST_PARTIAL;
}

void mime_vec_mask_apply_row(const MimeVecMask *m, int y, int x, int count, uint8_t *cov)
{
    if (count <= 0)
        return;
    uint32_t bi = find_band(m, y);
    if (bi >= m->nbands || m->bands[bi].y0 > y) {
        __builtin_memset(cov, 0, (size_t)count);
        return;
    }
    const MimeVecBand *b = &m->bands[bi];
    const MimeVecRun *runs = m->runs + b->first;
    int pos = x, end = x + count;
    for (uint32_t ri = find_run(runs, b->count, x); ri < b->count && runs[ri].x0 < end; ri++) {
        int x0 = runs[ri].x0, x1 = runs[ri].x1;
        if (x0 > pos) {
            __builtin_memset(cov + (pos - x), 0, (size_t)(x0 - pos));
            pos = x0;
        }
        if (x1 > end)
            x1 = end;
        uint32_t v = runs[ri].value;
        if (v != 255) {
            for (int i = pos; i < x1; i++)
                cov[i - x] = (uint8_t)mime_vec_div255(cov[i - x] * v);
        }
        pos = x1;
    }
    if (pos < end)
        __builtin_memset(cov + (pos - x), 0, (size_t)(end - pos));
}

void mime_vec_mask_emit(const MimeVecMask *m, const MimeVecRect *r, MimeVecSpanFn fn, void *user)
{
    uint8_t buf[256];
    for (uint32_t bi = find_band(m, r->top); bi < m->nbands && m->bands[bi].y0 < r->bottom; bi++) {
        const MimeVecBand *b = &m->bands[bi];
        int32_t y0 = b->y0 > r->top ? b->y0 : r->top;
        int32_t y1 = b->y1 < r->bottom ? b->y1 : r->bottom;
        const MimeVecRun *runs = m->runs + b->first;
        for (int32_t y = y0; y < y1; y++) {
            for (uint32_t ri = find_run(runs, b->count, r->left); ri < b->count && runs[ri].x0 < r->right; ri++) {
                int32_t x0 = runs[ri].x0 > r->left ? runs[ri].x0 : r->left;
                int32_t x1 = runs[ri].x1 < r->right ? runs[ri].x1 : r->right;
                __builtin_memset(buf, (int)runs[ri].value, sizeof(buf));
                for (int32_t x = x0; x < x1; x += (int32_t)sizeof(buf)) {
                    int32_t n = x1 - x;
                    if (n > (int32_t)sizeof(buf))
                        n = (int32_t)sizeof(buf);
                    fn(user, y, x, n, buf);
                }
            }
        }
    }
}
