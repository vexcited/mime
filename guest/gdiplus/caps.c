/*
 * custom line caps. The line of a pen with a custom cap at an end is shortened by the base inset of the cap, the
 * shape of the cap is scaled by the width of the pen and the width scale of the cap, turned to the direction
 * the line leaves the end in and put on the end point. The cap has a fill shape that is filled and a stroke
 * shape that is widened with the stroke caps and join of the cap.
 */
#include "gp_private.h"

typedef struct CapEnd {
    const GCap *cap;
    MimeVecPoint at;     /* the original end of the line */
    MimeVecPoint dir;    /* the unit direction pointing away from the line */
} CapEnd;

static float length_of(float x, float y) { return mime_vec_sqrtf(x * x + y * y); }

/* the polyline without its last length, in place. false when nothing is left. */
static BOOL shorten_end(MimeVecPoint *pts, UINT *count, float length)
{
    while (*count >= 2 && length > 0.0f) {
        MimeVecPoint a = pts[*count - 2], b = pts[*count - 1];
        float seg = length_of(b.x - a.x, b.y - a.y);
        if (seg > length) {
            float t = (seg - length) / seg;
            pts[*count - 1].x = a.x + (b.x - a.x) * t;
            pts[*count - 1].y = a.y + (b.y - a.y) * t;
            return TRUE;
        }
        length -= seg;
        (*count)--;
    }
    return *count >= 2;
}

static void reverse_points(MimeVecPoint *pts, UINT count)
{
    for (UINT i = 0; i < count / 2; i++) {
        MimeVecPoint t = pts[i];
        pts[i] = pts[count - 1 - i];
        pts[count - 1 - i] = t;
    }
}

/* the cap space to the space of the line: x along the turned side, y along the direction away from the line. */
static MimeVecMatrix cap_matrix(const CapEnd *e, float scale)
{
    MimeVecMatrix m;
    m.m11 = e->dir.y * scale;
    m.m12 = -e->dir.x * scale;
    m.m21 = e->dir.x * scale;
    m.m22 = e->dir.y * scale;
    m.dx = e->at.x;
    m.dy = e->at.y;
    return m;
}

static float signed_area(const MimeVecPoint *p, UINT n)
{
    float a = 0.0f;
    for (UINT i = 0; i < n; i++) {
        const MimeVecPoint *u = &p[i], *v = &p[i + 1 == n ? 0 : i + 1];
        a += u->x * v->y - v->x * u->y;
    }
    return a;
}

/* the fill shape of a cap in the space of the output, all with the winding of the stroker's polygons. */
static GpStatus add_fill(const GCap *cap, const CapEnd *e, float scale, const MimeVecMatrix *to_out, float flatness,
                         float want_sign, MimeVecPolys *out)
{
    if (!cap->fill)
        return Ok;
    MimeVecMatrix m = cap_matrix(e, scale), full;
    mime_vec_matrix_multiply(&full, &m, to_out);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st = gp_path_flatten(&cap->fill->path, &full, flatness, &polys);
    for (UINT i = 0; st == Ok && i < polys.nsubs; i++) {
        MimeVecPoint *pts = polys.pts + polys.subs[i].first;
        UINT n = polys.subs[i].count;
        if (n < 3)
            continue;
        if (signed_area(pts, n) * want_sign < 0.0f)
            reverse_points(pts, n);
        st = gp_path_status(mime_vec_polys_add_sub(out, pts, n, true));
    }
    mime_vec_polys_free(&polys);
    return st;
}

/* the stroke shape of a cap, widened with the stroke settings of the cap. */
static GpStatus add_stroke(const GCap *cap, const CapEnd *e, float scale, float width, float min_width,
                           const MimeVecMatrix *to_out, float flatness, MimeVecPolys *out)
{
    if (!cap->stroke)
        return Ok;
    MimeVecMatrix m = cap_matrix(e, scale);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st = gp_path_flatten(&cap->stroke->path, &m, flatness, &polys);
    if (st == Ok) {
        MimeVecStroke s;
        mime_vec_stroke_defaults(&s, width * cap->width_scale);
        s.min_width = min_width;
        s.start_cap = cap->start_cap == LineCapCustom ? MIME_VEC_CAP_FLAT : cap->start_cap;
        s.end_cap = cap->end_cap == LineCapCustom ? MIME_VEC_CAP_FLAT : cap->end_cap;
        s.join = cap->join;
        s.miter_limit = 10.0f;
        s.flatness = flatness;
        s.out = to_out;
        st = gp_path_status(mime_vec_stroke(out, &polys, &s));
    }
    mime_vec_polys_free(&polys);
    return st;
}

BOOL gp_pen_has_custom_caps(const GPen *pen)
{
    return (pen->start_cap == LineCapCustom && pen->custom_start) || (pen->end_cap == LineCapCustom && pen->custom_end);
}

/*
 * shortens the open subpaths of flat at the ends that have a custom cap and adds the caps to out, in the space of
 * the output given by to_out. width is the width of the pen in the space of the line.
 */
GpStatus gp_pen_custom_caps(const GPen *pen, MimeVecPolys *flat, float width, float min_width,
                            const MimeVecMatrix *to_out, float flatness, MimeVecPolys *fills, MimeVecPolys *strokes)
{
    const GCap *start_cap = pen->start_cap == LineCapCustom ? pen->custom_start : nullptr;
    const GCap *end_cap = pen->end_cap == LineCapCustom ? pen->custom_end : nullptr;
    MimeVecPolys shortened;
    mime_vec_polys_init(&shortened, &gp_allocator);
    GpStatus st = Ok;
    /* the caps are collected first and their shapes drawn after the lines, which pick the winding of the fill */
    CapEnd *ends = gp_alloc(sizeof(CapEnd) * (flat->nsubs * 2 + 1));
    UINT nends = 0;
    if (!ends) {
        mime_vec_polys_free(&shortened);
        return OutOfMemory;
    }
    for (UINT i = 0; i < flat->nsubs && st == Ok; i++) {
        const MimeVecSub *sub = &flat->subs[i];
        MimeVecPoint *src = flat->pts + sub->first;
        UINT n = sub->count;
        if (sub->closed || n < 2) {
            st = gp_path_status(mime_vec_polys_add_sub(&shortened, src, n, sub->closed != 0));
            continue;
        }
        MimeVecPoint *pts = gp_dup(src, sizeof(MimeVecPoint) * n);
        if (!pts) {
            st = OutOfMemory;
            break;
        }
        /* the directions come from the first and the last segment that has a length */
        BOOL keep = TRUE;
        float s0 = start_cap ? width * start_cap->width_scale : 0.0f, s1 = end_cap ? width * end_cap->width_scale : 0.0f;
        if (start_cap) {
            UINT k = 1;
            while (k + 1 < n && length_of(pts[k].x - pts[0].x, pts[k].y - pts[0].y) < 1.0e-6f)
                k++;
            float dx = pts[0].x - pts[k].x, dy = pts[0].y - pts[k].y, l = length_of(dx, dy);
            if (l > 0.0f) {
                CapEnd *e = &ends[nends++];
                e->cap = start_cap;
                e->at = pts[0];
                e->dir.x = dx / l;
                e->dir.y = dy / l;
            }
        }
        if (end_cap) {
            UINT k = n - 2;
            while (k > 0 && length_of(pts[n - 1].x - pts[k].x, pts[n - 1].y - pts[k].y) < 1.0e-6f)
                k--;
            float dx = pts[n - 1].x - pts[k].x, dy = pts[n - 1].y - pts[k].y, l = length_of(dx, dy);
            if (l > 0.0f) {
                CapEnd *e = &ends[nends++];
                e->cap = end_cap;
                e->at = pts[n - 1];
                e->dir.x = dx / l;
                e->dir.y = dy / l;
            }
        }
        if (end_cap && end_cap->base_inset != 0.0f)
            keep = shorten_end(pts, &n, end_cap->base_inset * s1);
        if (keep && start_cap && start_cap->base_inset != 0.0f) {
            reverse_points(pts, n);
            keep = shorten_end(pts, &n, start_cap->base_inset * s0);
            reverse_points(pts, n);
        }
        if (keep && n >= 2)
            st = gp_path_status(mime_vec_polys_add_sub(&shortened, pts, n, false));
        gp_free(pts);
    }
    if (st == Ok) {
        mime_vec_polys_free(flat);
        *flat = shortened;
    } else {
        mime_vec_polys_free(&shortened);
    }
    for (UINT i = 0; i < nends && st == Ok; i++) {
        const CapEnd *e = &ends[i];
        float scale = width * e->cap->width_scale;
        st = add_stroke(e->cap, e, scale, width, min_width, to_out, flatness, strokes);
        if (st == Ok)
            st = add_fill(e->cap, e, scale, to_out, flatness, 1.0f, fills);
    }
    gp_free(ends);
    return st;
}

float gp_polys_orientation_of(const MimeVecPoint *pts, UINT n)
{
    float a = signed_area(pts, n);
    return a < 0.0f ? -1.0f : 1.0f;
}

float gp_polys_orientation(const MimeVecPolys *p)
{
    for (UINT i = 0; i < p->nsubs; i++) {
        if (p->subs[i].count >= 3) {
            float a = signed_area(p->pts + p->subs[i].first, p->subs[i].count);
            if (a != 0.0f)
                return a < 0.0f ? -1.0f : 1.0f;
        }
    }
    return 1.0f;
}
