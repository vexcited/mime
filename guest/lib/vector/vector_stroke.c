/*
 * stroker: widens polylines into fill polygons.
 *
 * the geometry is built from independent convex pieces that all have the
 * same orientation: one quad per segment, one wedge per join, one polygon per
 * cap. the nonzero rule unions them, so overlaps never cancel and an
 * antialiased fill shows no seams between the pieces.
 *
 * a pen is a set of bands across the line, each described by the signed offsets
 * t0 < t1 from the center line along the normal of the direction of travel, which
 * points to the right on a screen with y down. the plain pen has the single band
 * from -h to h, a compound pen has one band per line and inset alignment shifts
 * the bands by h, so the pen lies on the right of the direction of travel.
 */
#include "vector_internal.h"

#define PI_F 3.14159265358979f
#define MAX_BANDS 32
#define MAX_DASH_ELEMENTS 4096
#define MAX_ARC_STEPS 256
#define DASH_BUDGET (1 << 16)

typedef struct St {
    const MimeAllocator *a;
    MimeVecPolys *out;
    float w, h, tol, eps2;
    int join, cap0, cap1, dashcap;
    int rc0, rc1; /* start and end caps once anchors are resolved. */
    bool flip;    /* the output transform mirrors. */
    float miter_limit, lim2;
    int align;
    int nbands;
    float band[2 * MAX_BANDS];
    float anchor_scale;
    /* dashes. */
    bool dashed;
    bool adjust; /* round and triangle dash caps shorten the dashes and widen the gaps. */
    const float *dash;
    uint32_t ndash, cycle;
    float period, phase;
    int budget;
    /* scratch. */
    MimeVecPoint *work;
    uint32_t work_cap;
    MimeVecPoint *piece;
    uint32_t piece_n, piece_cap;
    int err;
} St;

static const float dash_presets[5][6] = {
    {0, 0, 0, 0, 0, 0},
    {3, 1, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0},
    {3, 1, 1, 1, 0, 0},
    {3, 1, 1, 1, 1, 1},
};
static const uint32_t dash_preset_len[5] = {0, 2, 2, 4, 6};

/* polygon emission. */

static void pbegin(St *s)
{
    if (!s->err)
        s->err = mime_vec_polys_begin(s->out);
}

static void padd(St *s, float x, float y)
{
    if (!s->err)
        s->err = mime_vec_polys_add(s->out, x, y);
}

/* ends a polygon: the orientation is made positive and degenerate ones are dropped. */
static void pend(St *s)
{
    if (s->err)
        return;
    MimeVecPolys *o = s->out;
    uint32_t f = o->cur_first, n = o->npts - f;
    if (n < 3) {
        o->npts = f;
        o->open = false;
        return;
    }
    MimeVecPoint *p = o->pts + f;
    float ox = p[0].x, oy = p[0].y;
    float area = 0.0f;
    for (uint32_t i = 1; i + 1 < n; i++)
        area += (p[i].x - ox) * (p[i + 1].y - oy) - (p[i + 1].x - ox) * (p[i].y - oy);
    if (!(area != 0.0f) || !mime_vec_finite(area)) {
        o->npts = f;
        o->open = false;
        return;
    }
    if (area < 0.0f) {
        for (uint32_t i = 0, j = n - 1; i < j; i++, j--) {
            MimeVecPoint t = p[i];
            p[i] = p[j];
            p[j] = t;
        }
    }
    s->err = mime_vec_polys_end(o, true);
}

static void pquad(St *s, MimeVecPoint a, MimeVecPoint b, MimeVecPoint c, MimeVecPoint d)
{
    pbegin(s);
    padd(s, a.x, a.y);
    padd(s, b.x, b.y);
    padd(s, c.x, c.y);
    padd(s, d.x, d.y);
    pend(s);
}

/* helpers. */

static inline MimeVecPoint pt(float x, float y)
{
    MimeVecPoint p = {x, y};
    return p;
}

static float arc_step(float r, float tol)
{
    float x = tol / r;
    if (!(x < 1.0f))
        return PI_F * 0.5f;
    float c = 1.0f - x;
    float sn = mime_vec_sqrtf(1.0f - c * c);
    float st = 2.0f * mime_vec_atan2f(sn, c);
    if (!(st < PI_F * 0.5f))
        st = PI_F * 0.5f;
    return st;
}

static int arc_count(float sweep, float r, float tol)
{
    float a = sweep < 0 ? -sweep : sweep;
    float step = arc_step(r, tol);
    float n = mime_vec_ceilf(a / step);
    if (!(n >= 1.0f))
        return 1;
    if (n > (float)MAX_ARC_STEPS)
        return MAX_ARC_STEPS;
    return (int)n;
}

static int grow_pts(St *s, MimeVecPoint **buf, uint32_t *cap, uint32_t need)
{
    int r = vec_grow(s->a, (void **)buf, cap, need, sizeof(MimeVecPoint), MIME_VEC_MAX_POINTS);
    if (r && !s->err)
        s->err = r;
    return r;
}

static void piece_add(St *s, MimeVecPoint p)
{
    if (s->err)
        return;
    if (grow_pts(s, &s->piece, &s->piece_cap, s->piece_n + 1))
        return;
    s->piece[s->piece_n++] = p;
}

/* joins. */

/* the kinds of boundary chain of a join wedge. */
enum { CHAIN_BEVEL, CHAIN_MITER, CHAIN_CLIP, CHAIN_ARC };

typedef struct JoinCtx {
    MimeVecPoint p, u0, u1, d1, d2;
    int kind;
    float dot;
    float sweep;
    int steps;
    float clipdist;
} JoinCtx;

/* adds the boundary chain of the join at radius r, forward or backward. */
static void chain(St *s, const JoinCtx *j, float r, bool rev)
{
    MimeVecPoint a1 = pt(j->p.x + r * j->u0.x, j->p.y + r * j->u0.y);
    MimeVecPoint b2 = pt(j->p.x + r * j->u1.x, j->p.y + r * j->u1.y);
    MimeVecPoint buf[4];
    int n = 0;
    switch (j->kind) {
    case CHAIN_BEVEL:
        buf[n++] = a1;
        buf[n++] = b2;
        break;
    case CHAIN_MITER: {
        float k = r / (1.0f + j->dot);
        buf[n++] = a1;
        buf[n++] = pt(j->p.x + k * (j->u0.x + j->u1.x), j->p.y + k * (j->u0.y + j->u1.y));
        buf[n++] = b2;
        break;
    }
    case CHAIN_CLIP: {
        float D = j->clipdist;
        buf[n++] = a1;
        float bl2 = 2.0f + 2.0f * j->dot;
        if (r * r * 2.0f <= D * D * (1.0f + j->dot)) {
            /* the miter at this radius is short enough to stay whole. */
            float k = r / (1.0f + j->dot);
            buf[n++] = pt(j->p.x + k * (j->u0.x + j->u1.x), j->p.y + k * (j->u0.y + j->u1.y));
        } else {
            MimeVecPoint bd;
            float da;
            if (bl2 < 1.0e-12f) {
                bd = j->d1;
                da = 0.0f;
            } else {
                float inv = 1.0f / mime_vec_sqrtf(bl2);
                bd = pt((j->u0.x + j->u1.x) * inv, (j->u0.y + j->u1.y) * inv);
                da = r * (j->u0.x * bd.x + j->u0.y * bd.y);
            }
            float db = j->d1.x * bd.x + j->d1.y * bd.y;
            if (D > da && db > 1.0e-6f) {
                float lam = (D - da) / db;
                buf[n++] = pt(a1.x + j->d1.x * lam, a1.y + j->d1.y * lam);
                buf[n++] = pt(b2.x - j->d2.x * lam, b2.y - j->d2.y * lam);
            }
        }
        buf[n++] = b2;
        break;
    }
    default: {
        /* arc from u0 to u1 with the exact end vectors. */
        float delta = j->sweep / (float)j->steps;
        float sd, cd;
        mime_vec_sincosf(rev ? -delta : delta, &sd, &cd);
        MimeVecPoint v = rev ? j->u1 : j->u0;
        padd(s, j->p.x + r * v.x, j->p.y + r * v.y);
        for (int k = 1; k < j->steps; k++) {
            float nx = v.x * cd - v.y * sd, ny = v.x * sd + v.y * cd;
            v.x = nx;
            v.y = ny;
            padd(s, j->p.x + r * v.x, j->p.y + r * v.y);
        }
        MimeVecPoint e = rev ? j->u0 : j->u1;
        padd(s, j->p.x + r * e.x, j->p.y + r * e.y);
        return;
    }
    }
    if (rev) {
        for (int i = n - 1; i >= 0; i--)
            padd(s, buf[i].x, buf[i].y);
    } else {
        for (int i = 0; i < n; i++)
            padd(s, buf[i].x, buf[i].y);
    }
}

static void emit_join(St *s, MimeVecPoint p, MimeVecPoint d1, MimeVecPoint d2, float t0, float t1)
{
    float cross = d1.x * d2.y - d1.y * d2.x;
    float dot = d1.x * d2.x + d1.y * d2.y;
    float sgn = cross > 0.0f ? -1.0f : 1.0f;
    float ra, rb;
    if (sgn > 0.0f) {
        rb = t1;
        ra = t0 > 0.0f ? t0 : 0.0f;
    } else {
        rb = -t0;
        ra = -t1 > 0.0f ? -t1 : 0.0f;
    }
    if (!(rb > ra))
        return;
    float acr = cross < 0 ? -cross : cross;
    if (dot > 0.0f && rb * acr < s->tol * 0.005f)
        return;
    JoinCtx j;
    j.p = p;
    j.d1 = d1;
    j.d2 = d2;
    j.u0 = pt(-d1.y * sgn, d1.x * sgn);
    j.u1 = pt(-d2.y * sgn, d2.x * sgn);
    j.dot = dot;
    j.sweep = cross == 0.0f ? (dot < 0 ? -PI_F : 0.0f) : mime_vec_atan2f(cross, dot);
    j.steps = 1;
    j.clipdist = s->miter_limit * s->h;
    j.kind = CHAIN_BEVEL;
    switch (s->join) {
    case MIME_VEC_JOIN_ROUND:
        j.kind = CHAIN_ARC;
        j.steps = arc_count(j.sweep, rb, s->tol);
        break;
    case MIME_VEC_JOIN_MITER:
    case MIME_VEC_JOIN_MITERCLIPPED:
        if ((1.0f + dot) * s->lim2 >= 2.0f && dot > -0.999999f)
            j.kind = CHAIN_MITER;
        else if (s->join == MIME_VEC_JOIN_MITERCLIPPED)
            j.kind = CHAIN_CLIP;
        break;
    default:
        break;
    }
    pbegin(s);
    if (!(ra > 0.0f))
        padd(s, p.x, p.y);
    chain(s, &j, rb, false);
    if (ra > 0.0f)
        chain(s, &j, ra, true);
    pend(s);
}

/* caps. */

/* anchors use the dash cap as their base cap, like GDI+. */
static int resolve_cap(const St *s, int type)
{
    if (type == MIME_VEC_CAP_SQUARE || type == MIME_VEC_CAP_ROUND || type == MIME_VEC_CAP_TRIANGLE)
        return type;
    if (type == MIME_VEC_CAP_FLAT)
        return MIME_VEC_CAP_FLAT;
    if (type >= MIME_VEC_CAP_NOANCHOR)
        return s->dashcap;
    return MIME_VEC_CAP_FLAT;
}

/*
 * cap polygon at p. e points away from the line and nrm is the lateral axis. the
 * band is t0 to t1 and the pen it belongs to is centered on c, so the cap shape
 * is drawn around c and cut to the band.
 */
static void emit_cap(St *s, MimeVecPoint p, MimeVecPoint nrm, MimeVecPoint e, int type, float t0, float t1, float c)
{
    float h = s->h;
    float a = t0 - c, b = t1 - c;
#define CAPPT(v, u) padd(s, p.x + nrm.x * (c + (v)) + e.x * (u), p.y + nrm.y * (c + (v)) + e.y * (u))
    switch (type) {
    case MIME_VEC_CAP_SQUARE:
        pbegin(s);
        CAPPT(a, 0.0f);
        CAPPT(a, h);
        CAPPT(b, h);
        CAPPT(b, 0.0f);
        pend(s);
        break;
    case MIME_VEC_CAP_TRIANGLE: {
        float u0 = h - (a < 0 ? -a : a), u1 = h - (b < 0 ? -b : b);
        pbegin(s);
        CAPPT(a, 0.0f);
        CAPPT(a, u0 > 0 ? u0 : 0.0f);
        if (a < 0.0f && b > 0.0f)
            CAPPT(0.0f, h);
        CAPPT(b, u1 > 0 ? u1 : 0.0f);
        CAPPT(b, 0.0f);
        pend(s);
        break;
    }
    case MIME_VEC_CAP_ROUND: {
        float c0 = h * h - a * a, c1 = h * h - b * b;
        float uu0 = mime_vec_sqrtf(c0 > 0 ? c0 : 0.0f), uu1 = mime_vec_sqrtf(c1 > 0 ? c1 : 0.0f);
        float a0 = mime_vec_atan2f(a, uu0), a1 = mime_vec_atan2f(b, uu1);
        float sweep = a1 - a0;
        int steps = arc_count(sweep, h, s->tol);
        float delta = sweep / (float)steps;
        float sd, cd;
        mime_vec_sincosf(delta, &sd, &cd);
        pbegin(s);
        CAPPT(a, 0.0f);
        float v = a, u = uu0;
        CAPPT(v, u);
        for (int k = 1; k < steps; k++) {
            float nv = v * cd + u * sd, nu = u * cd - v * sd;
            v = nv;
            u = nu;
            CAPPT(v, u);
        }
        CAPPT(b, uu1);
        CAPPT(b, 0.0f);
        pend(s);
        break;
    }
    default:
        break;
    }
#undef CAPPT
}

/* anchor shapes in GDI+ reference cap units: x across the line, y along it. */
static const float anchor_square[4][2] = {
    {-0.70710678f, -0.70710678f}, {0.70710678f, -0.70710678f}, {0.70710678f, 0.70710678f}, {-0.70710678f, 0.70710678f}};
static const float anchor_diamond[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
static const float anchor_arrow[3][2] = {{0, 0}, {-1, -1.7320508f}, {1, -1.7320508f}};

static bool is_anchor(int cap)
{
    return cap >= MIME_VEC_CAP_SQUAREANCHOR && cap <= MIME_VEC_CAP_ARROWANCHOR;
}

/* distance the line is pulled back from the end for an anchor. */
static float anchor_inset(St *s, int cap)
{
    return cap == MIME_VEC_CAP_ARROWANCHOR ? s->anchor_scale : 0.0f;
}

static void emit_anchor(St *s, MimeVecPoint p, MimeVecPoint e, int cap)
{
    float sc = s->anchor_scale;
    MimeVecPoint nrm = pt(-e.y, e.x);
#define APT(ax, ay) padd(s, p.x + nrm.x * (ax) * sc + e.x * (ay) * sc, p.y + nrm.y * (ax) * sc + e.y * (ay) * sc)
    pbegin(s);
    switch (cap) {
    case MIME_VEC_CAP_SQUAREANCHOR:
        for (int i = 0; i < 4; i++)
            APT(anchor_square[i][0], anchor_square[i][1]);
        break;
    case MIME_VEC_CAP_DIAMONDANCHOR:
        for (int i = 0; i < 4; i++)
            APT(anchor_diamond[i][0], anchor_diamond[i][1]);
        break;
    case MIME_VEC_CAP_ARROWANCHOR:
        for (int i = 0; i < 3; i++)
            APT(anchor_arrow[i][0], anchor_arrow[i][1]);
        break;
    default: {
        int steps = arc_count(2.0f * PI_F, sc, s->tol) * 2;
        if (steps < 8)
            steps = 8;
        if (steps > 2 * MAX_ARC_STEPS)
            steps = 2 * MAX_ARC_STEPS;
        float delta = 2.0f * PI_F / (float)steps;
        float sd, cd;
        mime_vec_sincosf(delta, &sd, &cd);
        float x = 1.0f, y = 0.0f;
        for (int k = 0; k < steps; k++) {
            APT(x, y);
            float nx = x * cd - y * sd, ny = x * sd + y * cd;
            x = nx;
            y = ny;
        }
        break;
    }
    }
    pend(s);
#undef APT
}

/* one polyline. */

static inline MimeVecPoint unit_dir(MimeVecPoint a, MimeVecPoint b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float l = mime_vec_sqrtf(dx * dx + dy * dy);
    if (!(l > 0.0f))
        return pt(1.0f, 0.0f);
    float inv = 1.0f / l;
    return pt(dx * inv, dy * inv);
}

/*
 * strokes a polyline whose points were already cleaned of duplicates. cap0 and cap1 are the
 * resolved cap types of the two ends. fdir is the direction used when the line has no length.
 */
static void stroke_line(St *s, const MimeVecPoint *p, uint32_t n, bool closed, int cap0, int cap1,
                        const MimeVecPoint *fdir)
{
    if (s->err || n == 0)
        return;
    float shift = 0.0f;
    if (s->align == MIME_VEC_ALIGN_INSET)
        shift = s->flip ? -s->h : s->h;
    if (n == 1) {
        MimeVecPoint d = fdir ? *fdir : pt(1.0f, 0.0f);
        MimeVecPoint nrm = pt(-d.y, d.x);
        MimeVecPoint back = pt(-d.x, -d.y);
        for (int b = 0; b < s->nbands; b++) {
            emit_cap(s, p[0], nrm, back, cap0, s->band[2 * b] + shift, s->band[2 * b + 1] + shift, shift);
            emit_cap(s, p[0], nrm, d, cap1, s->band[2 * b] + shift, s->band[2 * b + 1] + shift, shift);
        }
        return;
    }
    uint32_t nseg = closed ? n : n - 1;
    for (int b = 0; b < s->nbands && !s->err; b++) {
        float t0 = s->band[2 * b] + shift, t1 = s->band[2 * b + 1] + shift;
        MimeVecPoint dprev = pt(1, 0), d0 = pt(1, 0);
        for (uint32_t i = 0; i < nseg; i++) {
            MimeVecPoint a = p[i], c = p[i + 1 == n ? 0 : i + 1];
            MimeVecPoint d = unit_dir(a, c);
            MimeVecPoint nr = pt(-d.y, d.x);
            pquad(s, pt(a.x + nr.x * t1, a.y + nr.y * t1), pt(c.x + nr.x * t1, c.y + nr.y * t1),
                  pt(c.x + nr.x * t0, c.y + nr.y * t0), pt(a.x + nr.x * t0, a.y + nr.y * t0));
            if (i > 0)
                emit_join(s, a, dprev, d, t0, t1);
            else
                d0 = d;
            dprev = d;
        }
        if (closed) {
            emit_join(s, p[0], dprev, d0, t0, t1);
        } else {
            emit_cap(s, p[0], pt(-d0.y, d0.x), pt(-d0.x, -d0.y), cap0, t0, t1, shift);
            emit_cap(s, p[n - 1], pt(-dprev.y, dprev.x), dprev, cap1, t0, t1, shift);
        }
    }
}

/* dashes. */

static float elem_len(const St *s, uint32_t idx)
{
    float v = s->dash[idx % s->ndash];
    if (!(v > 0.0f))
        v = 0.0f;
    v *= s->w;
    if (s->adjust) {
        if ((idx & 1) == 0) {
            v -= s->w;
            if (v < 0.001f * s->w)
                v = 0.001f * s->w;
        } else {
            v += s->w;
        }
    }
    return v;
}

static void emit_piece(St *s, const MimeVecPoint *pts, uint32_t n, int cap0, int cap1, MimeVecPoint dir)
{
    if (s->err || n == 0)
        return;
    /* clean duplicates in place: the piece buffer is private. */
    MimeVecPoint *w = (MimeVecPoint *)pts;
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (m > 0) {
            float dx = w[i].x - w[m - 1].x, dy = w[i].y - w[m - 1].y;
            if (dx * dx + dy * dy <= s->eps2)
                continue;
        }
        w[m++] = w[i];
    }
    stroke_line(s, w, m, false, cap0, cap1, &dir);
}

/*
 * dashes a cleaned polyline. like GDI+, a closed one starts at its last vertex with the closing segment.
 * the dash ends take the dash cap, except those within half a width of the real ends of an open subpath,
 * which take the start and end caps.
 */
static void dash_polyline(St *s, const MimeVecPoint *p, uint32_t n, bool closed)
{
    int dc = s->dashcap;
    uint32_t nseg = closed ? n : n - 1;
    float h2 = s->h * s->h;
    float total = 0.0f;
    for (uint32_t i = 0; i < nseg; i++) {
        MimeVecPoint a = p[i], b = p[i + 1 == n ? 0 : i + 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        total += mime_vec_sqrtf(dx * dx + dy * dy);
    }
    float est = total / s->period * (float)s->cycle;
    if (!(est < (float)s->budget)) {
        stroke_line(s, p, n, closed, s->rc0, s->rc1, NULL);
        return;
    }
    /* position inside the pattern. */
    uint32_t idx = 0;
    float rem = elem_len(s, 0);
    float phase = s->phase;
    for (uint32_t guard = 0; guard < 2 * s->cycle + 2 && phase > 0.0f && phase >= rem; guard++) {
        phase -= rem;
        idx = idx + 1 == s->cycle ? 0 : idx + 1;
        rem = elem_len(s, idx);
    }
    rem -= phase;
    bool on = (idx & 1) == 0;
    bool toggled = false;
    MimeVecPoint dir = pt(1, 0);
    s->piece_n = 0;
    if (on)
        piece_add(s, p[0]);
    for (uint32_t k = 0; k < nseg && !s->err; k++) {
        /* a closed polyline begins with the closing segment. */
        uint32_t i = closed ? (k == 0 ? n - 1 : k - 1) : k;
        MimeVecPoint a = p[i], b = p[i + 1 == n ? 0 : i + 1];
        if (closed && k == 0 && on) {
            s->piece_n = 0;
            piece_add(s, a);
        }
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = mime_vec_sqrtf(dx * dx + dy * dy);
        if (!(len > 0.0f))
            continue;
        MimeVecPoint d = pt(dx / len, dy / len);
        dir = d;
        float pos = 0.0f;
        while (len - pos > rem) {
            pos += rem;
            MimeVecPoint q = pt(a.x + d.x * pos, a.y + d.y * pos);
            if (on) {
                piece_add(s, q);
                if (s->err || s->piece_n == 0)
                    return;
                int c0 = dc, c1 = dc;
                if (!closed) {
                    float x0 = s->piece[0].x - p[0].x, y0 = s->piece[0].y - p[0].y;
                    float x1 = q.x - p[n - 1].x, y1 = q.y - p[n - 1].y;
                    if (x0 * x0 + y0 * y0 < h2)
                        c0 = s->rc0;
                    if (x1 * x1 + y1 * y1 < h2)
                        c1 = s->rc1;
                }
                emit_piece(s, s->piece, s->piece_n, c0, c1, d);
                s->piece_n = 0;
            }
            toggled = true;
            idx = idx + 1 == s->cycle ? 0 : idx + 1;
            on = (idx & 1) == 0;
            rem = elem_len(s, idx);
            if (--s->budget <= 0) {
                s->err = MIME_VEC_TOOBIG;
                return;
            }
            if (on) {
                s->piece_n = 0;
                piece_add(s, q);
            }
        }
        rem -= len - pos;
        if (on)
            piece_add(s, b);
    }
    if (s->err)
        return;
    if (!toggled) {
        if (on)
            stroke_line(s, p, n, closed, s->rc0, s->rc1, NULL);
        return;
    }
    if (on && s->piece_n > 0) {
        int c0 = dc, c1 = dc;
        if (!closed) {
            float x0 = s->piece[0].x - p[0].x, y0 = s->piece[0].y - p[0].y;
            float x1 = s->piece[s->piece_n - 1].x - p[n - 1].x, y1 = s->piece[s->piece_n - 1].y - p[n - 1].y;
            if (x0 * x0 + y0 * y0 < h2)
                c0 = s->rc0;
            if (x1 * x1 + y1 * y1 < h2)
                c1 = s->rc1;
        }
        emit_piece(s, s->piece, s->piece_n, c0, c1, dir);
    }
}

/* subpaths. */

static void trim_ends(MimeVecPoint *p, uint32_t *np, float d0, float d1)
{
    uint32_t n = *np;
    /* the end first. */
    while (d1 > 0.0f && n >= 2) {
        float dx = p[n - 1].x - p[n - 2].x, dy = p[n - 1].y - p[n - 2].y;
        float l = mime_vec_sqrtf(dx * dx + dy * dy);
        if (l > d1) {
            p[n - 1].x -= dx / l * d1;
            p[n - 1].y -= dy / l * d1;
            d1 = 0.0f;
            break;
        }
        d1 -= l;
        n--;
    }
    while (d0 > 0.0f && n >= 2) {
        float dx = p[1].x - p[0].x, dy = p[1].y - p[0].y;
        float l = mime_vec_sqrtf(dx * dx + dy * dy);
        if (l > d0) {
            p[0].x += dx / l * d0;
            p[0].y += dy / l * d0;
            d0 = 0.0f;
            break;
        }
        d0 -= l;
        for (uint32_t i = 1; i < n; i++)
            p[i - 1] = p[i];
        n--;
    }
    if ((d0 > 0.0f || d1 > 0.0f) && n < 2)
        n = 0;
    *np = n;
}

static void stroke_sub(St *s, const MimeVecPoint *in, uint32_t n, bool closed)
{
    if (n == 0 || (n == 1 && !closed))
        return;
    /* clean the points. */
    if (grow_pts(s, &s->work, &s->work_cap, n))
        return;
    MimeVecPoint *w = s->work;
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) {
        float x = in[i].x, y = in[i].y;
        if (!(x == x) || !(y == y))
            return;
        x = mime_vec_clamp_coord(x);
        y = mime_vec_clamp_coord(y);
        if (m > 0) {
            float dx = x - w[m - 1].x, dy = y - w[m - 1].y;
            if (dx * dx + dy * dy <= s->eps2)
                continue;
        }
        w[m].x = x;
        w[m].y = y;
        m++;
    }
    if (closed) {
        while (m > 1) {
            float dx = w[m - 1].x - w[0].x, dy = w[m - 1].y - w[0].y;
            if (dx * dx + dy * dy > s->eps2)
                break;
            m--;
        }
    }
    bool is_closed = closed && m >= 2;
    bool anchors = !is_closed && (is_anchor(s->cap0) || is_anchor(s->cap1));
    MimeVecPoint e0 = pt(-1, 0), e1 = pt(1, 0), pa = w[0], pb = w[m - 1];
    if (anchors && m >= 2) {
        e0 = unit_dir(w[1], w[0]);
        e1 = unit_dir(w[m - 2], w[m - 1]);
        float i0 = is_anchor(s->cap0) ? anchor_inset(s, s->cap0) : 0.0f;
        float i1 = is_anchor(s->cap1) ? anchor_inset(s, s->cap1) : 0.0f;
        if (i0 > 0.0f || i1 > 0.0f)
            trim_ends(w, &m, i0, i1);
    }
    if (m >= 1) {
        if (s->dashed && m >= 2)
            dash_polyline(s, w, m, is_closed);
        else
            stroke_line(s, w, m, is_closed, s->rc0, s->rc1, NULL);
    }
    if (anchors) {
        if (is_anchor(s->cap0))
            emit_anchor(s, pa, e0, s->cap0);
        if (is_anchor(s->cap1))
            emit_anchor(s, pb, e1, s->cap1);
    }
}

void mime_vec_stroke_defaults(MimeVecStroke *p, float width)
{
    __builtin_memset(p, 0, sizeof(*p));
    p->width = width;
    p->start_cap = MIME_VEC_CAP_FLAT;
    p->end_cap = MIME_VEC_CAP_FLAT;
    p->dash_cap = MIME_VEC_DASHCAP_FLAT;
    p->join = MIME_VEC_JOIN_MITER;
    p->miter_limit = 10.0f;
    p->align = MIME_VEC_ALIGN_CENTER;
    p->dash_style = MIME_VEC_DASH_SOLID;
    p->flatness = MIME_VEC_DEFAULT_FLATNESS;
}

int mime_vec_stroke(MimeVecPolys *out, const MimeVecPolys *in, const MimeVecStroke *pen)
{
    St s;
    __builtin_memset(&s, 0, sizeof(s));
    s.a = out->alloc;
    s.out = out;
    float w = pen->width;
    if (!(w == w))
        return MIME_VEC_INVALID;
    if (pen->min_width > w)
        w = pen->min_width;
    if (!(w > 0.0f))
        return MIME_VEC_OK;
    if (w > MIME_VEC_COORD_LIMIT)
        w = MIME_VEC_COORD_LIMIT;
    s.w = w;
    s.h = w * 0.5f;
    float tol = pen->flatness;
    if (!(tol > 0.0f))
        tol = MIME_VEC_DEFAULT_FLATNESS;
    if (tol < 1.0e-4f)
        tol = 1.0e-4f;
    s.tol = tol;
    float eps = tol * 1.0e-3f;
    s.eps2 = eps * eps;
    s.join = pen->join;
    s.cap0 = pen->start_cap;
    s.cap1 = pen->end_cap;
    s.dashcap = pen->dash_cap == MIME_VEC_DASHCAP_ROUND || pen->dash_cap == MIME_VEC_DASHCAP_TRIANGLE
                    ? pen->dash_cap
                    : MIME_VEC_DASHCAP_FLAT;
    s.rc0 = resolve_cap(&s, s.cap0);
    s.rc1 = resolve_cap(&s, s.cap1);
    if (pen->out) {
        float det = pen->out->m11 * pen->out->m22 - pen->out->m12 * pen->out->m21;
        s.flip = det < 0.0f;
    }
    float ml = pen->miter_limit;
    if (!(ml >= 1.0f))
        ml = 1.0f;
    if (ml > 1.0e6f)
        ml = 1.0e6f;
    s.miter_limit = ml;
    s.lim2 = ml * ml;
    s.align = pen->align;
    float unit = pen->min_width > 0.0f ? pen->min_width : 1.0f;
    s.anchor_scale = w > unit ? w : unit;

    /* bands across the pen. */
    s.nbands = 0;
    if (pen->compound && pen->ncompound >= 2) {
        for (uint32_t i = 0; i + 1 < pen->ncompound && s.nbands < MAX_BANDS; i += 2) {
            float a = pen->compound[i], b = pen->compound[i + 1];
            if (!(a == a) || !(b == b))
                continue;
            a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            b = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
            if (!(b > a))
                continue;
            s.band[2 * s.nbands] = -s.h + a * w;
            s.band[2 * s.nbands + 1] = -s.h + b * w;
            s.nbands++;
        }
    }
    if (s.nbands == 0) {
        s.band[0] = -s.h;
        s.band[1] = s.h;
        s.nbands = 1;
    }

    /* dash pattern. */
    int style = pen->dash_style;
    if (style > MIME_VEC_DASH_SOLID && style <= MIME_VEC_DASH_DASHDOTDOT) {
        s.dash = dash_presets[style];
        s.ndash = dash_preset_len[style];
    } else if (style == MIME_VEC_DASH_CUSTOM && pen->dash && pen->ndash > 0) {
        s.dash = pen->dash;
        s.ndash = pen->ndash > MAX_DASH_ELEMENTS ? MAX_DASH_ELEMENTS : pen->ndash;
    }
    if (s.ndash) {
        s.adjust = s.dashcap != MIME_VEC_DASHCAP_FLAT;
        s.cycle = (s.ndash & 1) ? 2 * s.ndash : s.ndash;
        float period = 0.0f;
        for (uint32_t i = 0; i < s.cycle; i++)
            period += elem_len(&s, i);
        if (mime_vec_finite(period) && period > 1.0e-6f * w) {
            s.dashed = true;
            s.period = period;
            float off = pen->dash_offset;
            if (!mime_vec_finite(off))
                off = 0.0f;
            float ph = off * w;
            ph -= mime_vec_floorf(ph / period) * period;
            if (!(ph >= 0.0f) || !(ph < period))
                ph = 0.0f;
            s.phase = ph;
        }
    }
    s.budget = DASH_BUDGET;

    int r = mime_vec_polys_end(out, false);
    if (r)
        return r;
    uint32_t first_pt = out->npts;
    for (uint32_t i = 0; i < in->nsubs && !s.err; i++)
        stroke_sub(&s, in->pts + in->subs[i].first, in->subs[i].count, in->subs[i].closed != 0);
    if (out->open) {
        out->npts = out->cur_first;
        out->open = false;
    }
    if (pen->out && !mime_vec_matrix_is_identity(pen->out)) {
        for (uint32_t i = first_pt; i < out->npts; i++) {
            MimeVecPoint q = mime_vec_xform(pen->out, out->pts[i].x, out->pts[i].y);
            out->pts[i].x = mime_vec_clamp_coord(q.x);
            out->pts[i].y = mime_vec_clamp_coord(q.y);
        }
    }
    mime_free(s.a, s.work);
    mime_free(s.a, s.piece);
    return s.err;
}

bool mime_vec_stroke_bounds(const MimeVecPolys *in, const MimeVecStroke *pen, MimeVecBounds *b)
{
    MimeVecPolys tmp;
    mime_vec_polys_init(&tmp, in->alloc);
    bool ok = mime_vec_stroke(&tmp, in, pen) == MIME_VEC_OK && mime_vec_polys_bounds(&tmp, b);
    mime_vec_polys_free(&tmp);
    return ok;
}
