/*
 * polylines, flattening of paths and hit testing.
 */
#include "vector_internal.h"

void mime_vec_polys_init(MimeVecPolys *p, const MimeAllocator *alloc)
{
    __builtin_memset(p, 0, sizeof(*p));
    p->alloc = alloc;
}

void mime_vec_polys_free(MimeVecPolys *p)
{
    mime_free(p->alloc, p->pts);
    mime_free(p->alloc, p->subs);
    const MimeAllocator *a = p->alloc;
    __builtin_memset(p, 0, sizeof(*p));
    p->alloc = a;
}

void mime_vec_polys_clear(MimeVecPolys *p)
{
    p->npts = 0;
    p->nsubs = 0;
    p->open = false;
    p->cur_first = 0;
}

int mime_vec_polys_begin(MimeVecPolys *p)
{
    if (p->open) {
        int r = mime_vec_polys_end(p, false);
        if (r)
            return r;
    }
    p->open = true;
    p->cur_first = p->npts;
    return MIME_VEC_OK;
}

int mime_vec_polys_add(MimeVecPolys *p, float x, float y)
{
    if (!p->open) {
        int r = mime_vec_polys_begin(p);
        if (r)
            return r;
    }
    int r = vec_grow(p->alloc, (void **)&p->pts, &p->cap_pts, p->npts + 1, sizeof(MimeVecPoint), MIME_VEC_MAX_POINTS);
    if (r)
        return r;
    p->pts[p->npts].x = x;
    p->pts[p->npts].y = y;
    p->npts++;
    return MIME_VEC_OK;
}

int mime_vec_polys_end(MimeVecPolys *p, bool closed)
{
    if (!p->open)
        return MIME_VEC_OK;
    p->open = false;
    uint32_t count = p->npts - p->cur_first;
    if (count == 0)
        return MIME_VEC_OK;
    int r = vec_grow(p->alloc, (void **)&p->subs, &p->cap_subs, p->nsubs + 1, sizeof(MimeVecSub), MIME_VEC_MAX_POINTS);
    if (r) {
        p->npts = p->cur_first;
        return r;
    }
    p->subs[p->nsubs].first = p->cur_first;
    p->subs[p->nsubs].count = count;
    p->subs[p->nsubs].closed = closed ? 1 : 0;
    p->nsubs++;
    return MIME_VEC_OK;
}

int mime_vec_polys_add_sub(MimeVecPolys *p, const MimeVecPoint *pts, uint32_t count, bool closed)
{
    if (count == 0)
        return MIME_VEC_OK;
    int r = mime_vec_polys_begin(p);
    if (r)
        return r;
    if (count > MIME_VEC_MAX_POINTS || p->npts + count > MIME_VEC_MAX_POINTS)
        return MIME_VEC_TOOBIG;
    r = vec_grow(p->alloc, (void **)&p->pts, &p->cap_pts, p->npts + count, sizeof(MimeVecPoint), MIME_VEC_MAX_POINTS);
    if (r)
        return r;
    __builtin_memcpy(p->pts + p->npts, pts, (size_t)count * sizeof(MimeVecPoint));
    p->npts += count;
    return mime_vec_polys_end(p, closed);
}

bool mime_vec_polys_bounds(const MimeVecPolys *p, MimeVecBounds *b)
{
    uint32_t n = p->npts;
    if (n == 0)
        return false;
    float l = p->pts[0].x, r = l, t = p->pts[0].y, bt = t;
    for (uint32_t i = 1; i < n; i++) {
        float x = p->pts[i].x, y = p->pts[i].y;
        l = x < l ? x : l;
        r = x > r ? x : r;
        t = y < t ? y : t;
        bt = y > bt ? y : bt;
    }
    b->left = l;
    b->top = t;
    b->right = r;
    b->bottom = bt;
    return true;
}

void mime_vec_polys_transform(MimeVecPolys *p, const MimeVecMatrix *m)
{
    for (uint32_t i = 0; i < p->npts; i++) {
        MimeVecPoint q = mime_vec_xform(m, p->pts[i].x, p->pts[i].y);
        p->pts[i] = q;
    }
}

/* flattening. */

#define MAX_CURVE_DEPTH 10

typedef struct Cubic {
    float x[4], y[4];
    int depth;
} Cubic;

static int flatten_cubic(MimeVecPolys *out, const MimeVecPoint *p0, const MimeVecPoint *p1, const MimeVecPoint *p2,
                         const MimeVecPoint *p3, float tol2)
{
    Cubic stack[MAX_CURVE_DEPTH + 2];
    int sp = 0;
    stack[0].x[0] = p0->x;
    stack[0].y[0] = p0->y;
    stack[0].x[1] = p1->x;
    stack[0].y[1] = p1->y;
    stack[0].x[2] = p2->x;
    stack[0].y[2] = p2->y;
    stack[0].x[3] = p3->x;
    stack[0].y[3] = p3->y;
    stack[0].depth = 0;
    sp = 1;
    while (sp > 0) {
        Cubic c = stack[--sp];
        float d1x = c.x[0] - 2.0f * c.x[1] + c.x[2];
        float d1y = c.y[0] - 2.0f * c.y[1] + c.y[2];
        float d2x = c.x[1] - 2.0f * c.x[2] + c.x[3];
        float d2y = c.y[1] - 2.0f * c.y[2] + c.y[3];
        float m1 = d1x * d1x + d1y * d1y;
        float m2 = d2x * d2x + d2y * d2y;
        float m = m1 > m2 ? m1 : m2;
        /* the chord error is at most 3/4 of the largest second difference. */
        if (0.5625f * m <= tol2 || c.depth >= MAX_CURVE_DEPTH || !(m == m)) {
            int r = mime_vec_polys_add(out, c.x[3], c.y[3]);
            if (r)
                return r;
            continue;
        }
        float x01 = (c.x[0] + c.x[1]) * 0.5f, y01 = (c.y[0] + c.y[1]) * 0.5f;
        float x12 = (c.x[1] + c.x[2]) * 0.5f, y12 = (c.y[1] + c.y[2]) * 0.5f;
        float x23 = (c.x[2] + c.x[3]) * 0.5f, y23 = (c.y[2] + c.y[3]) * 0.5f;
        float xa = (x01 + x12) * 0.5f, ya = (y01 + y12) * 0.5f;
        float xb = (x12 + x23) * 0.5f, yb = (y12 + y23) * 0.5f;
        float xm = (xa + xb) * 0.5f, ym = (ya + yb) * 0.5f;
        /* right half first so the left half is popped first. */
        Cubic *r = &stack[sp++];
        r->x[0] = xm;
        r->y[0] = ym;
        r->x[1] = xb;
        r->y[1] = yb;
        r->x[2] = x23;
        r->y[2] = y23;
        r->x[3] = c.x[3];
        r->y[3] = c.y[3];
        r->depth = c.depth + 1;
        Cubic *l = &stack[sp++];
        l->x[0] = c.x[0];
        l->y[0] = c.y[0];
        l->x[1] = x01;
        l->y[1] = y01;
        l->x[2] = xa;
        l->y[2] = ya;
        l->x[3] = xm;
        l->y[3] = ym;
        l->depth = c.depth + 1;
    }
    return MIME_VEC_OK;
}

/* ends the current subpath, a closed one does not repeat its first point. */
static int finish_sub(MimeVecPolys *out, bool closed)
{
    if (closed && out->npts - out->cur_first > 1) {
        const MimeVecPoint *f = &out->pts[out->cur_first], *l = &out->pts[out->npts - 1];
        if (f->x == l->x && f->y == l->y)
            out->npts--;
    }
    return mime_vec_polys_end(out, closed);
}

/* transforms one point after clamping, false for a NaN. */
static bool fetch_point(MimeVecPoint *o, const MimeVecPoint *src, const MimeVecMatrix *m)
{
    float x = src->x, y = src->y;
    if (!(x == x) || !(y == y))
        return false;
    x = mime_vec_clamp_coord(x);
    y = mime_vec_clamp_coord(y);
    if (m) {
        MimeVecPoint q = mime_vec_xform(m, x, y);
        x = q.x;
        y = q.y;
        if (!(x == x) || !(y == y))
            return false;
        x = mime_vec_clamp_coord(x);
        y = mime_vec_clamp_coord(y);
    }
    o->x = x;
    o->y = y;
    return true;
}

int mime_vec_flatten(MimeVecPolys *out, const MimeVecPoint *pts, const uint8_t *types, uint32_t count,
                     const MimeVecMatrix *m, float flatness)
{
    if (!(flatness > 0.0f))
        flatness = MIME_VEC_DEFAULT_FLATNESS;
    if (flatness < 0.001f)
        flatness = 0.001f;
    float tol2 = flatness * flatness;
    uint32_t save_pts = out->npts, save_subs = out->nsubs;
    int r = mime_vec_polys_end(out, false);
    if (r)
        return r;
    bool open = false;
    MimeVecPoint start = {0, 0}, cur = {0, 0};
    uint32_t i = 0;
    while (i < count) {
        uint8_t flags = types[i];
        unsigned t = flags & MIME_VEC_PT_TYPE_MASK;
        MimeVecPoint p;
        if (t == MIME_VEC_PT_START || (!open && i == 0)) {
            if (open) {
                r = finish_sub(out, false);
                if (r)
                    goto fail;
                open = false;
            }
            if (!fetch_point(&p, &pts[i], m)) {
                r = MIME_VEC_INVALID;
                goto fail;
            }
            r = mime_vec_polys_begin(out);
            if (!r)
                r = mime_vec_polys_add(out, p.x, p.y);
            if (r)
                goto fail;
            open = true;
            start = cur = p;
            i++;
            if (flags & MIME_VEC_PT_CLOSE) {
                r = finish_sub(out, true);
                if (r)
                    goto fail;
                open = false;
            }
            continue;
        }
        if (!open) {
            /* a figure continuing after a close restarts at the previous start. */
            r = mime_vec_polys_begin(out);
            if (!r)
                r = mime_vec_polys_add(out, start.x, start.y);
            if (r)
                goto fail;
            open = true;
            cur = start;
        }
        if (t == MIME_VEC_PT_BEZIER) {
            if (i + 2 >= count || (types[i + 1] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_START ||
                (types[i + 2] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_START)
                break;
            MimeVecPoint c1, c2, e;
            if (!fetch_point(&c1, &pts[i], m) || !fetch_point(&c2, &pts[i + 1], m) ||
                !fetch_point(&e, &pts[i + 2], m)) {
                r = MIME_VEC_INVALID;
                goto fail;
            }
            r = flatten_cubic(out, &cur, &c1, &c2, &e, tol2);
            if (r)
                goto fail;
            cur = e;
            flags = types[i + 2];
            i += 3;
        } else {
            if (!fetch_point(&p, &pts[i], m)) {
                r = MIME_VEC_INVALID;
                goto fail;
            }
            r = mime_vec_polys_add(out, p.x, p.y);
            if (r)
                goto fail;
            cur = p;
            i++;
        }
        if (flags & MIME_VEC_PT_CLOSE) {
            r = finish_sub(out, true);
            if (r)
                goto fail;
            open = false;
        }
    }
    if (open) {
        r = mime_vec_polys_end(out, false);
        if (r)
            goto fail;
    }
    return MIME_VEC_OK;
fail:
    out->open = false;
    out->npts = save_pts;
    out->nsubs = save_subs;
    return r;
}

int mime_vec_path_flatten(const MimeVecPath *p, const MimeVecMatrix *m, float flatness, MimeVecPolys *out)
{
    return mime_vec_flatten(out, p->pts, p->types, p->count, m, flatness);
}

/* hit testing. */

int mime_vec_winding_at(const MimeVecPolys *p, float x, float y)
{
    int wn = 0;
    for (uint32_t s = 0; s < p->nsubs; s++) {
        const MimeVecPoint *pt = p->pts + p->subs[s].first;
        uint32_t n = p->subs[s].count;
        if (n < 2)
            continue;
        MimeVecPoint a = pt[n - 1];
        for (uint32_t i = 0; i < n; i++) {
            MimeVecPoint b = pt[i];
            if (a.y <= y) {
                if (b.y > y && (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y) > 0.0f)
                    wn++;
            } else if (b.y <= y && (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y) < 0.0f) {
                wn--;
            }
            a = b;
        }
    }
    return wn;
}

bool mime_vec_point_in_polys(const MimeVecPolys *p, float x, float y, int rule)
{
    int w = mime_vec_winding_at(p, x, y);
    return rule == MIME_VEC_FILL_WINDING ? w != 0 : (w & 1) != 0;
}

static float seg_dist2(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (l2 > 0.0f) {
        t = ((px - ax) * dx + (py - ay) * dy) / l2;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
    }
    float qx = ax + t * dx - px, qy = ay + t * dy - py;
    return qx * qx + qy * qy;
}

float mime_vec_distance2(const MimeVecPolys *p, float x, float y)
{
    float best = -1.0f;
    for (uint32_t s = 0; s < p->nsubs; s++) {
        const MimeVecPoint *pt = p->pts + p->subs[s].first;
        uint32_t n = p->subs[s].count;
        if (n == 0)
            continue;
        if (n == 1) {
            float d = seg_dist2(x, y, pt[0].x, pt[0].y, pt[0].x, pt[0].y);
            if (best < 0 || d < best)
                best = d;
            continue;
        }
        for (uint32_t i = 0; i + 1 < n; i++) {
            float d = seg_dist2(x, y, pt[i].x, pt[i].y, pt[i + 1].x, pt[i + 1].y);
            if (best < 0 || d < best)
                best = d;
        }
        if (p->subs[s].closed && n > 2) {
            float d = seg_dist2(x, y, pt[n - 1].x, pt[n - 1].y, pt[0].x, pt[0].y);
            if (best < 0 || d < best)
                best = d;
        }
    }
    return best;
}

bool mime_vec_point_near(const MimeVecPolys *p, float x, float y, float dist)
{
    float d2 = mime_vec_distance2(p, x, y);
    return d2 >= 0.0f && d2 <= dist * dist;
}
