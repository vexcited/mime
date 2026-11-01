/*
 * geometries: rectangles, rounded rectangles, ellipses, paths with their sinks, groups and transformed geometries,
 * and the stroke styles that widen them.
 */
#include "d2d_private.h"
#include <float.h>

#define SELF D2dGeometry *g = (D2dGeometry *)This
#define KAPPA 0.5522847498f

/* ------------------------------------------------------------------ building paths */

BOOL D2dPathBegin(D2dPath *p, D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin)
{
    if (!D2dArrayGrow(&p->figures, sizeof(D2dFigure), 1))
        return FALSE;
    D2dFigure f = {};
    f.start = start;
    f.begin = (UINT8)begin;
    f.end = D2D1_FIGURE_END_OPEN;
    f.first = p->segments.count;
    ((D2dFigure *)p->figures.data)[p->figures.count++] = f;
    return TRUE;
}

static D2dFigure *last_figure(D2dPath *p) { return p->figures.count ? (D2dFigure *)p->figures.data + p->figures.count - 1 : nullptr; }

BOOL D2dPathAdd(D2dPath *p, const D2dSegment *s)
{
    D2dFigure *f = last_figure(p);
    if (!f || !D2dArrayGrow(&p->segments, sizeof(D2dSegment), 1))
        return FALSE;
    f = last_figure(p);
    ((D2dSegment *)p->segments.data)[p->segments.count++] = *s;
    f->count++;
    return TRUE;
}

BOOL D2dPathLine(D2dPath *p, D2D1_POINT_2F pt)
{
    D2dSegment s = {};
    s.kind = D2D_SEG_LINE;
    s.p[0] = pt;
    return D2dPathAdd(p, &s);
}

BOOL D2dPathBezier(D2dPath *p, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c)
{
    D2dSegment s = {};
    s.kind = D2D_SEG_BEZIER;
    s.p[0] = a;
    s.p[1] = b;
    s.p[2] = c;
    return D2dPathAdd(p, &s);
}

void D2dPathEnd(D2dPath *p, D2D1_FIGURE_END end)
{
    D2dFigure *f = last_figure(p);
    if (f)
        f->end = (UINT8)end;
}

/* the shapes that have no stored path. */
static BOOL shape_path(const D2dGeometry *g, D2dPath *p)
{
    p->fill_mode = D2D1_FILL_MODE_WINDING;
    if (g->kind == D2D_GEO_RECT) {
        const D2D1_RECT_F *r = &g->shape.rect;
        D2D1_POINT_2F s = {r->left, r->top};
        D2D1_POINT_2F a = {r->right, r->top}, b = {r->right, r->bottom}, c = {r->left, r->bottom};
        return D2dPathBegin(p, s, D2D1_FIGURE_BEGIN_FILLED) && D2dPathLine(p, a) && D2dPathLine(p, b) &&
               D2dPathLine(p, c) && (D2dPathEnd(p, D2D1_FIGURE_END_CLOSED), TRUE);
    }
    if (g->kind == D2D_GEO_ROUNDED) {
        const D2D1_ROUNDED_RECT *rr = &g->shape.rounded;
        float w = rr->rect.right - rr->rect.left, h = rr->rect.bottom - rr->rect.top;
        float rx = D2dMinF(D2dAbsF(rr->radiusX), D2dAbsF(w) * 0.5f), ry = D2dMinF(D2dAbsF(rr->radiusY), D2dAbsF(h) * 0.5f);
        float l = rr->rect.left, t = rr->rect.top, r = rr->rect.right, b = rr->rect.bottom;
        D2D1_POINT_2F s = {l + rx, t};
        if (!D2dPathBegin(p, s, D2D1_FIGURE_BEGIN_FILLED))
            return FALSE;
        D2D1_POINT_2F pt;
        D2D1_ARC_SEGMENT arc = {{0, 0}, {rx, ry}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        D2dSegment seg = {};
        seg.kind = D2D_SEG_ARC;
        pt.x = r - rx;
        pt.y = t;
        D2dPathLine(p, pt);
        arc.point.x = r;
        arc.point.y = t + ry;
        seg.arc = arc;
        D2dPathAdd(p, &seg);
        pt.x = r;
        pt.y = b - ry;
        D2dPathLine(p, pt);
        seg.arc.point.x = r - rx;
        seg.arc.point.y = b;
        D2dPathAdd(p, &seg);
        pt.x = l + rx;
        pt.y = b;
        D2dPathLine(p, pt);
        seg.arc.point.x = l;
        seg.arc.point.y = b - ry;
        D2dPathAdd(p, &seg);
        pt.x = l;
        pt.y = t + ry;
        D2dPathLine(p, pt);
        seg.arc.point.x = l + rx;
        seg.arc.point.y = t;
        D2dPathAdd(p, &seg);
        D2dPathEnd(p, D2D1_FIGURE_END_CLOSED);
        return TRUE;
    }
    if (g->kind == D2D_GEO_ELLIPSE) {
        const D2D1_ELLIPSE *e = &g->shape.ellipse;
        float cx = e->point.x, cy = e->point.y, rx = e->radiusX, ry = e->radiusY;
        D2D1_POINT_2F s = {cx + rx, cy};
        if (!D2dPathBegin(p, s, D2D1_FIGURE_BEGIN_FILLED))
            return FALSE;
        D2D1_POINT_2F c1 = {cx + rx, cy + KAPPA * ry}, c2 = {cx + KAPPA * rx, cy + ry}, e1 = {cx, cy + ry};
        D2dPathBezier(p, c1, c2, e1);
        D2D1_POINT_2F d1 = {cx - KAPPA * rx, cy + ry}, d2 = {cx - rx, cy + KAPPA * ry}, e2 = {cx - rx, cy};
        D2dPathBezier(p, d1, d2, e2);
        D2D1_POINT_2F f1 = {cx - rx, cy - KAPPA * ry}, f2 = {cx - KAPPA * rx, cy - ry}, e3 = {cx, cy - ry};
        D2dPathBezier(p, f1, f2, e3);
        D2D1_POINT_2F h1 = {cx + KAPPA * rx, cy - ry}, h2 = {cx + rx, cy - KAPPA * ry}, e4 = {cx + rx, cy};
        D2dPathBezier(p, h1, h2, e4);
        D2dPathEnd(p, D2D1_FIGURE_END_CLOSED);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ flattening */

D2dGeometry *D2dGeometryOf(ID2D1Geometry *g)
{
    if (!g || !D2dIsOurs(g))
        return nullptr;
    return (D2dGeometry *)g;
}

static D2dClass g_rect_class, g_rounded_class, g_ellipse_class, g_group_class, g_transformed_class, g_path_class;
static D2dClass g_stroke_class, g_mesh_class, g_sink_class, g_tess_class;

void D2dInitTempGeometry(D2dGeometry *g, int kind)
{
    memset(g, 0, sizeof(*g));
    g->obj.cls = &g_path_class;
    g->kind = kind;
    g->path_closed = TRUE;
}

BOOL D2dIsOurs(const void *p)
{
    if (!p)
        return FALSE;
    const D2dClass *c = ((const D2dObj *)p)->cls;
    return c == &g_rect_class || c == &g_rounded_class || c == &g_ellipse_class || c == &g_group_class ||
           c == &g_transformed_class || c == &g_path_class;
}

static HRESULT flatten_into(D2dGeometry *g, const D2dMatrix *m, float tol, BOOL only_filled, MimeVecPolys *out,
                            BOOL *winding)
{
    if (tol <= 0.0f)
        tol = D2D1_DEFAULT_FLATTENING_TOLERANCE;
    switch (g->kind) {
    case D2D_GEO_PATH:
        if (!g->path_closed)
            return D2DERR_WRONG_STATE;
        if (winding)
            *winding = g->path.fill_mode == D2D1_FILL_MODE_WINDING;
        return D2dPathFlatten(&g->path, m, tol, out, only_filled) ? S_OK : E_OUTOFMEMORY;
    case D2D_GEO_GROUP: {
        if (winding)
            *winding = g->group_mode == D2D1_FILL_MODE_WINDING;
        for (UINT32 i = 0; i < g->child_count; i++) {
            D2dGeometry *c = D2dGeometryOf(g->children[i]);
            if (!c)
                continue;
            HRESULT hr = flatten_into(c, m, tol, only_filled, out, nullptr);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    }
    case D2D_GEO_TRANSFORMED: {
        D2dGeometry *c = D2dGeometryOf(g->source);
        if (!c)
            return E_INVALIDARG;
        D2dMatrix mm;
        D2dMatMul(&mm, &g->transform, m);
        return flatten_into(c, &mm, tol, only_filled, out, winding);
    }
    default: {
        D2dPath p = {};
        if (!shape_path(g, &p)) {
            D2dPathFree(&p);
            return E_OUTOFMEMORY;
        }
        if (winding)
            *winding = TRUE;
        BOOL ok = D2dPathFlatten(&p, m, tol, out, only_filled);
        D2dPathFree(&p);
        return ok ? S_OK : E_OUTOFMEMORY;
    }
    }
}

HRESULT D2dGeometryFlatten(D2dGeometry *g, const D2dMatrix *m, float tolerance, BOOL only_filled, MimeVecPolys *out,
                           BOOL *winding)
{
    if (winding)
        *winding = TRUE;
    return flatten_into(g, m ? m : &d2d_identity, tolerance, only_filled, out, winding);
}

static void polys_bounds(const MimeVecPolys *p, D2D1_RECT_F *r)
{
    r->left = r->top = FLT_MAX;
    r->right = r->bottom = -FLT_MAX;
    for (UINT32 i = 0; i < p->npts; i++) {
        r->left = D2dMinF(r->left, p->pts[i].x);
        r->right = D2dMaxF(r->right, p->pts[i].x);
        r->top = D2dMinF(r->top, p->pts[i].y);
        r->bottom = D2dMaxF(r->bottom, p->pts[i].y);
    }
}

HRESULT D2dGeometryBounds(D2dGeometry *g, const D2dMatrix *m, float tolerance, D2D1_RECT_F *bounds)
{
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dGeometryFlatten(g, m, tolerance, FALSE, &polys, nullptr);
    if (SUCCEEDED(hr))
        polys_bounds(&polys, bounds);
    mime_vec_polys_free(&polys);
    return hr;
}

/* ------------------------------------------------------------------ stroke styles */

static const float g_dash_presets[5][6] = {{0}, {2, 1}, {0, 1}, {2, 1, 0, 1}, {2, 1, 0, 1, 0, 1}};
static const int g_dash_preset_count[5] = {0, 2, 2, 4, 6};

static D2dStroke *stroke_of(ID2D1StrokeStyle *s)
{
    return s && ((D2dObj *)s)->cls == &g_stroke_class ? (D2dStroke *)s : nullptr;
}

void D2dStrokeSetup(MimeVecStroke *s, ID2D1StrokeStyle *style, float width, float flatness, float scale,
                    const D2dMatrix *out, float **dash_storage)
{
    mime_vec_stroke_defaults(s, width);
    s->flatness = flatness;
    s->out = D2dVec(out);
    s->miter_limit = 10.0f;
    *dash_storage = nullptr;
    D2dStroke *st = stroke_of(style);
    if (!st)
        return;
    static const int caps[] = {MIME_VEC_CAP_FLAT, MIME_VEC_CAP_SQUARE, MIME_VEC_CAP_ROUND, MIME_VEC_CAP_TRIANGLE};
    s->start_cap = caps[st->props.startCap];
    s->end_cap = caps[st->props.endCap];
    s->dash_cap = st->props.dashCap == D2D1_CAP_STYLE_SQUARE ? MIME_VEC_DASHCAP_FLAT : caps[st->props.dashCap];
    switch (st->props.lineJoin) {
    case D2D1_LINE_JOIN_MITER:
        s->join = MIME_VEC_JOIN_MITERCLIPPED;
        break;
    case D2D1_LINE_JOIN_BEVEL:
        s->join = MIME_VEC_JOIN_BEVEL;
        break;
    case D2D1_LINE_JOIN_ROUND:
        s->join = MIME_VEC_JOIN_ROUND;
        break;
    default:
        s->join = MIME_VEC_JOIN_MITER;
        break;
    }
    s->miter_limit = D2dMaxF(st->props.miterLimit, 1.0f);
    if (st->props.dashStyle != D2D1_DASH_STYLE_SOLID) {
        const float *src;
        UINT32 n;
        if (st->props.dashStyle == D2D1_DASH_STYLE_CUSTOM) {
            src = st->dashes;
            n = st->dash_count;
        } else {
            src = g_dash_presets[st->props.dashStyle];
            n = (UINT32)g_dash_preset_count[st->props.dashStyle];
        }
        /* the dash cap lengthens a dash on both ends, where the stroker keeps the period and shortens the dash. */
        float *pattern = D2dAlloc((n + 1) * sizeof(float));
        if (pattern) {
            BOOL capped = s->dash_cap != MIME_VEC_DASHCAP_FLAT;
            UINT32 m = n;
            for (UINT32 i = 0; i < n; i++)
                pattern[i] = src[i];
            if (m & 1) {
                /* an odd list repeats with the roles swapped, so it is played twice. */
                float *twice = D2dAlloc(2 * m * sizeof(float));
                if (twice) {
                    for (UINT32 i = 0; i < 2 * m; i++)
                        twice[i] = src[i % m];
                    D2dFree(pattern);
                    pattern = twice;
                    m *= 2;
                }
            }
            if (capped) {
                for (UINT32 i = 0; i + 1 < m; i += 2) {
                    pattern[i] += 1.0f;
                    pattern[i + 1] = D2dMaxF(pattern[i + 1] - 1.0f, 0.0f);
                }
            }
            s->dash_style = MIME_VEC_DASH_CUSTOM;
            s->dash = pattern;
            s->ndash = m & ~1u;
            s->dash_offset = st->props.dashOffset;
            *dash_storage = pattern;
        }
    }
}

HRESULT D2dStrokeGeometry(D2dGeometry *g, float width, ID2D1StrokeStyle *style, const D2dMatrix *m, float tolerance,
                          MimeVecPolys *out)
{
    if (tolerance <= 0.0f)
        tolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE;
    D2dStroke *st = stroke_of(style);
    D2D1_STROKE_TRANSFORM_TYPE tt = st && st->v1 ? st->props.transformType : D2D1_STROKE_TRANSFORM_TYPE_NORMAL;
    MimeVecPolys flat;
    mime_vec_polys_init(&flat, &d2d_allocator);
    D2dMatrix geo_to_out = m ? *m : d2d_identity;
    float scale = D2dMatScale(&geo_to_out);
    if (scale <= 0.0f)
        scale = 1.0f;
    const D2dMatrix *flatten_m = &d2d_identity;
    const D2dMatrix *stroke_out = &geo_to_out;
    float flatness = tolerance / scale;
    if (tt != D2D1_STROKE_TRANSFORM_TYPE_NORMAL) {
        flatten_m = &geo_to_out;
        stroke_out = &d2d_identity;
        flatness = tolerance;
        if (tt == D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE)
            width = 1.0f;
    }
    HRESULT hr = D2dGeometryFlatten(g, flatten_m, flatness, FALSE, &flat, nullptr);
    if (SUCCEEDED(hr)) {
        MimeVecStroke pen;
        float *dashes;
        D2dStrokeSetup(&pen, style, width, flatness, scale, stroke_out, &dashes);
        if (mime_vec_stroke(out, &flat, &pen) != MIME_VEC_OK)
            hr = E_OUTOFMEMORY;
        D2dFree(dashes);
    }
    mime_vec_polys_free(&flat);
    return hr;
}

static void stroke_destroy(D2dObj *o) { D2dFree(((D2dStroke *)o)->dashes); }

#define STROKE_SELF D2dStroke *s = (D2dStroke *)This
static D2D1_CAP_STYLE STDMETHODCALLTYPE ss_start(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.startCap; }
static D2D1_CAP_STYLE STDMETHODCALLTYPE ss_end(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.endCap; }
static D2D1_CAP_STYLE STDMETHODCALLTYPE ss_dash_cap(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.dashCap; }
static FLOAT STDMETHODCALLTYPE ss_miter(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.miterLimit; }
static D2D1_LINE_JOIN STDMETHODCALLTYPE ss_join(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.lineJoin; }
static FLOAT STDMETHODCALLTYPE ss_offset(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.dashOffset; }
static D2D1_DASH_STYLE STDMETHODCALLTYPE ss_style(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->props.dashStyle; }
static UINT32 STDMETHODCALLTYPE ss_count(ID2D1StrokeStyle1 *This) { STROKE_SELF; return s->dash_count; }
static void STDMETHODCALLTYPE ss_dashes(ID2D1StrokeStyle1 *This, FLOAT *dashes, UINT32 count)
{
    STROKE_SELF;
    for (UINT32 i = 0; i < count; i++)
        dashes[i] = i < s->dash_count ? s->dashes[i] : 0.0f;
}
static D2D1_STROKE_TRANSFORM_TYPE STDMETHODCALLTYPE ss_transform(ID2D1StrokeStyle1 *This)
{
    STROKE_SELF;
    return s->props.transformType;
}

static const void *const g_stroke_vtbl[] = {D2dQueryInterface, D2dAddRef,  D2dRelease, D2dResourceGetFactory, ss_start,
                                            ss_end,            ss_dash_cap, ss_miter,   ss_join,              ss_offset,
                                            ss_style,          ss_count,    ss_dashes,  ss_transform};

HRESULT D2dCreateStrokeStyle(D2dFactory *f, const D2D1_STROKE_STYLE_PROPERTIES1 *props, BOOL v1, const FLOAT *dashes,
                             UINT32 count, ID2D1StrokeStyle1 **out)
{
    D2dStroke *s = D2dNew(sizeof(*s), g_stroke_vtbl, &g_stroke_class, f);
    if (!s)
        return E_OUTOFMEMORY;
    s->props = *props;
    s->v1 = v1;
    if (!v1)
        s->props.transformType = D2D1_STROKE_TRANSFORM_TYPE_NORMAL;
    if (props->dashStyle == D2D1_DASH_STYLE_CUSTOM) {
        s->dashes = D2dAlloc(count * sizeof(float));
        if (!s->dashes) {
            D2dRelease(s);
            return E_OUTOFMEMORY;
        }
        memcpy(s->dashes, dashes, count * sizeof(float));
        s->dash_count = count;
    }
    *out = (ID2D1StrokeStyle1 *)s;
    return S_OK;
}

/* ------------------------------------------------------------------ generic geometry methods */

static float tol_of(float t) { return t > 0.0f ? t : D2D1_DEFAULT_FLATTENING_TOLERANCE; }

static HRESULT STDMETHODCALLTYPE geo_bounds(ID2D1Geometry *This, const D2D1_MATRIX_3X2_F *m, D2D1_RECT_F *bounds)
{
    SELF;
    if (!bounds)
        return E_POINTER;
    return D2dGeometryBounds(g, m, D2D1_DEFAULT_FLATTENING_TOLERANCE, bounds);
}

static HRESULT STDMETHODCALLTYPE geo_widened(ID2D1Geometry *This, FLOAT width, ID2D1StrokeStyle *style,
                                             const D2D1_MATRIX_3X2_F *m, FLOAT tol, D2D1_RECT_F *bounds)
{
    SELF;
    if (!bounds)
        return E_POINTER;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dStrokeGeometry(g, width, style, m, tol_of(tol), &polys);
    if (SUCCEEDED(hr))
        polys_bounds(&polys, bounds);
    mime_vec_polys_free(&polys);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_stroke_contains(ID2D1Geometry *This, D2D1_POINT_2F pt, FLOAT width,
                                                     ID2D1StrokeStyle *style, const D2D1_MATRIX_3X2_F *m, FLOAT tol,
                                                     BOOL *contains)
{
    SELF;
    if (!contains)
        return E_POINTER;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dStrokeGeometry(g, width, style, m, tol_of(tol), &polys);
    *contains = SUCCEEDED(hr) && mime_vec_point_in_polys(&polys, pt.x, pt.y, MIME_VEC_FILL_WINDING);
    mime_vec_polys_free(&polys);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_fill_contains(ID2D1Geometry *This, D2D1_POINT_2F pt, const D2D1_MATRIX_3X2_F *m,
                                                   FLOAT tol, BOOL *contains)
{
    SELF;
    if (!contains)
        return E_POINTER;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    BOOL winding;
    HRESULT hr = D2dGeometryFlatten(g, m, tol_of(tol), TRUE, &polys, &winding);
    *contains = SUCCEEDED(hr) &&
                mime_vec_point_in_polys(&polys, pt.x, pt.y, winding ? MIME_VEC_FILL_WINDING : MIME_VEC_FILL_ALTERNATE);
    mime_vec_polys_free(&polys);
    return hr;
}

static float traps_area(const D2dArray *traps)
{
    const D2dTrap *t = traps->data;
    double area = 0;
    for (UINT32 i = 0; i < traps->count; i++)
        area += 0.5 * (t[i].y1 - t[i].y0) * ((t[i].xr0 - t[i].xl0) + (t[i].xr1 - t[i].xl1));
    return (float)area;
}

/* the region of a geometry as trapezoids. */
static HRESULT geo_traps(D2dGeometry *g, const D2dMatrix *m, float tol, int op, D2dGeometry *other,
                         const D2dMatrix *om, D2dArray *traps)
{
    MimeVecPolys a, b;
    mime_vec_polys_init(&a, &d2d_allocator);
    mime_vec_polys_init(&b, &d2d_allocator);
    BOOL wa = TRUE, wb = TRUE;
    HRESULT hr = D2dGeometryFlatten(g, m, tol, TRUE, &a, &wa);
    if (SUCCEEDED(hr) && other)
        hr = D2dGeometryFlatten(other, om, tol, TRUE, &b, &wb);
    if (SUCCEEDED(hr))
        hr = D2dRegionTraps(&a, wa, other ? &b : nullptr, wb, op, traps);
    mime_vec_polys_free(&a);
    mime_vec_polys_free(&b);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_compare(ID2D1Geometry *This, ID2D1Geometry *other, const D2D1_MATRIX_3X2_F *om,
                                             FLOAT tol, D2D1_GEOMETRY_RELATION *relation)
{
    SELF;
    if (!relation)
        return E_POINTER;
    D2dGeometry *o = D2dGeometryOf(other);
    if (!o)
        return E_INVALIDARG;
    D2dArray both = {}, only_a = {}, only_b = {}, area_a = {}, area_b = {};
    HRESULT hr = geo_traps(g, nullptr, tol_of(tol), 1, o, om, &both);
    if (SUCCEEDED(hr))
        hr = geo_traps(g, nullptr, tol_of(tol), 3, o, om, &only_a);
    if (SUCCEEDED(hr))
        hr = geo_traps(o, om, tol_of(tol), 3, g, nullptr, &only_b);
    if (SUCCEEDED(hr)) {
        float ab = traps_area(&both), aa = traps_area(&only_a), bb = traps_area(&only_b);
        float eps = 1e-3f;
        if (ab <= eps)
            *relation = D2D1_GEOMETRY_RELATION_DISJOINT;
        else if (aa <= eps && bb <= eps)
            *relation = D2D1_GEOMETRY_RELATION_IS_CONTAINED;
        else if (aa <= eps)
            *relation = D2D1_GEOMETRY_RELATION_IS_CONTAINED;
        else if (bb <= eps)
            *relation = D2D1_GEOMETRY_RELATION_CONTAINS;
        else
            *relation = D2D1_GEOMETRY_RELATION_OVERLAP;
    }
    D2dArrayFree(&both);
    D2dArrayFree(&only_a);
    D2dArrayFree(&only_b);
    D2dArrayFree(&area_a);
    D2dArrayFree(&area_b);
    return hr;
}

static void sink_polys(ID2D1SimplifiedGeometrySink *sink, const MimeVecPolys *p, BOOL closed_all)
{
    for (UINT32 i = 0; i < p->nsubs; i++) {
        const MimeVecSub *s = &p->subs[i];
        if (!s->count)
            continue;
        D2D1_POINT_2F start = {p->pts[s->first].x, p->pts[s->first].y};
        sink->lpVtbl->BeginFigure(sink, start, D2D1_FIGURE_BEGIN_FILLED);
        if (s->count > 1) {
            /* the points are written in a bounded buffer. */
            D2D1_POINT_2F buf[64];
            UINT32 n = 0;
            for (UINT32 j = 1; j < s->count; j++) {
                buf[n].x = p->pts[s->first + j].x;
                buf[n].y = p->pts[s->first + j].y;
                if (++n == 64) {
                    sink->lpVtbl->AddLines(sink, buf, n);
                    n = 0;
                }
            }
            if (n)
                sink->lpVtbl->AddLines(sink, buf, n);
        }
        sink->lpVtbl->EndFigure(sink, closed_all || s->closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    }
}

static HRESULT emit_path(const D2dPath *path, ID2D1SimplifiedGeometrySink *sink, BOOL cubics_only)
{
    sink->lpVtbl->SetFillMode(sink, path->fill_mode);
    const D2dFigure *figs = path->figures.data;
    const D2dSegment *segs = path->segments.data;
    for (UINT32 i = 0; i < path->figures.count; i++) {
        sink->lpVtbl->BeginFigure(sink, figs[i].start, (D2D1_FIGURE_BEGIN)figs[i].begin);
        D2D1_POINT_2F cur = figs[i].start;
        for (UINT32 j = 0; j < figs[i].count; j++) {
            const D2dSegment *s = &segs[figs[i].first + j];
            if (s->kind == D2D_SEG_LINE) {
                sink->lpVtbl->AddLines(sink, &s->p[0], 1);
                cur = s->p[0];
            } else if (s->kind == D2D_SEG_BEZIER) {
                D2D1_BEZIER_SEGMENT b = {s->p[0], s->p[1], s->p[2]};
                sink->lpVtbl->AddBeziers(sink, &b, 1);
                cur = s->p[2];
            } else if (s->kind == D2D_SEG_QUAD) {
                D2D1_POINT_2F c1 = {cur.x + 2.0f / 3.0f * (s->p[0].x - cur.x), cur.y + 2.0f / 3.0f * (s->p[0].y - cur.y)};
                D2D1_POINT_2F c2 = {s->p[1].x + 2.0f / 3.0f * (s->p[0].x - s->p[1].x),
                                    s->p[1].y + 2.0f / 3.0f * (s->p[0].y - s->p[1].y)};
                D2D1_BEZIER_SEGMENT b = {c1, c2, s->p[1]};
                sink->lpVtbl->AddBeziers(sink, &b, 1);
                cur = s->p[1];
            } else {
                /* an arc goes out as beziers through a one segment path. */
                D2dPath one = {};
                D2dPath tmp = {};
                D2dPathBegin(&tmp, cur, D2D1_FIGURE_BEGIN_FILLED);
                D2dPathAdd(&tmp, s);
                D2dMatrix m = d2d_identity;
                m._31 = 0.0f;
                D2dMatrix not_identity = {1, 0, 0, 1, 0, 0};
                (void)m;
                (void)not_identity;
                /* a matrix that is not the identity makes the append convert arcs. */
                D2dMatrix tiny = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1e-30f};
                D2dPathAppend(&one, &tmp, &tiny);
                const D2dSegment *os = one.segments.data;
                for (UINT32 k = 0; k < one.segments.count; k++) {
                    if (os[k].kind == D2D_SEG_BEZIER) {
                        D2D1_BEZIER_SEGMENT b = {os[k].p[0], os[k].p[1], os[k].p[2]};
                        sink->lpVtbl->AddBeziers(sink, &b, 1);
                    } else {
                        sink->lpVtbl->AddLines(sink, &os[k].p[0], 1);
                    }
                }
                D2dPathFree(&one);
                D2dPathFree(&tmp);
                cur = s->arc.point;
            }
        }
        sink->lpVtbl->EndFigure(sink, (D2D1_FIGURE_END)figs[i].end);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE geo_simplify(ID2D1Geometry *This, D2D1_GEOMETRY_SIMPLIFICATION_OPTION option,
                                              const D2D1_MATRIX_3X2_F *m, FLOAT tol, ID2D1SimplifiedGeometrySink *sink)
{
    SELF;
    if (!sink)
        return E_POINTER;
    if (option > D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES)
        return E_INVALIDARG;
    if (g->kind == D2D_GEO_PATH && !g->path_closed)
        return D2DERR_WRONG_STATE;
    D2dPath path = {};
    HRESULT hr = S_OK;
    if (option == D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES) {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        BOOL winding;
        hr = D2dGeometryFlatten(g, m, tol_of(tol), FALSE, &polys, &winding);
        if (SUCCEEDED(hr)) {
            sink->lpVtbl->SetFillMode(sink, winding ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
            sink_polys(sink, &polys, FALSE);
        }
        mime_vec_polys_free(&polys);
        return hr;
    }
    /* cubics and lines: the stored path, or the shapes' path, transformed. */
    D2dMatrix mm = m ? *m : d2d_identity;
    if (g->kind == D2D_GEO_PATH) {
        D2dPathAppend(&path, &g->path, &mm);
        path.fill_mode = g->path.fill_mode;
        hr = emit_path(&path, sink, TRUE);
    } else if (g->kind == D2D_GEO_RECT || g->kind == D2D_GEO_ROUNDED || g->kind == D2D_GEO_ELLIPSE) {
        D2dPath shape = {};
        shape_path(g, &shape);
        D2dPathAppend(&path, &shape, &mm);
        path.fill_mode = shape.fill_mode;
        D2dPathFree(&shape);
        hr = emit_path(&path, sink, TRUE);
    } else {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        BOOL winding;
        hr = D2dGeometryFlatten(g, m, tol_of(tol), FALSE, &polys, &winding);
        if (SUCCEEDED(hr)) {
            sink->lpVtbl->SetFillMode(sink, winding ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
            sink_polys(sink, &polys, FALSE);
        }
        mime_vec_polys_free(&polys);
    }
    D2dPathFree(&path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_tessellate(ID2D1Geometry *This, const D2D1_MATRIX_3X2_F *m, FLOAT tol,
                                                ID2D1TessellationSink *sink)
{
    SELF;
    if (!sink)
        return E_POINTER;
    D2dArray traps = {};
    HRESULT hr = geo_traps(g, m, tol_of(tol), 4, nullptr, nullptr, &traps);
    if (SUCCEEDED(hr)) {
        const D2dTrap *t = traps.data;
        D2D1_TRIANGLE tri[64];
        UINT32 n = 0;
        for (UINT32 i = 0; i < traps.count; i++) {
            D2D1_POINT_2F a = {t[i].xl0, t[i].y0}, b = {t[i].xr0, t[i].y0}, c = {t[i].xr1, t[i].y1},
                          d = {t[i].xl1, t[i].y1};
            tri[n].point1 = a;
            tri[n].point2 = b;
            tri[n].point3 = c;
            n++;
            tri[n].point1 = a;
            tri[n].point2 = c;
            tri[n].point3 = d;
            n++;
            if (n >= 62) {
                sink->lpVtbl->AddTriangles(sink, tri, n);
                n = 0;
            }
        }
        if (n)
            sink->lpVtbl->AddTriangles(sink, tri, n);
    }
    D2dArrayFree(&traps);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_combine(ID2D1Geometry *This, ID2D1Geometry *other, D2D1_COMBINE_MODE mode,
                                             const D2D1_MATRIX_3X2_F *om, FLOAT tol, ID2D1SimplifiedGeometrySink *sink)
{
    SELF;
    if (!sink)
        return E_POINTER;
    D2dGeometry *o = D2dGeometryOf(other);
    if (!o || mode > D2D1_COMBINE_MODE_EXCLUDE)
        return E_INVALIDARG;
    D2dArray traps = {};
    HRESULT hr = geo_traps(g, nullptr, tol_of(tol), (int)mode, o, om, &traps);
    if (SUCCEEDED(hr)) {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        hr = D2dTrapsToPolys(&traps, &polys);
        if (SUCCEEDED(hr)) {
            sink->lpVtbl->SetFillMode(sink, D2D1_FILL_MODE_WINDING);
            sink_polys(sink, &polys, TRUE);
        }
        mime_vec_polys_free(&polys);
    }
    D2dArrayFree(&traps);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_outline(ID2D1Geometry *This, const D2D1_MATRIX_3X2_F *m, FLOAT tol,
                                             ID2D1SimplifiedGeometrySink *sink)
{
    SELF;
    if (!sink)
        return E_POINTER;
    D2dArray traps = {};
    HRESULT hr = geo_traps(g, m, tol_of(tol), 4, nullptr, nullptr, &traps);
    if (SUCCEEDED(hr)) {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        hr = D2dTrapsToPolys(&traps, &polys);
        if (SUCCEEDED(hr)) {
            sink->lpVtbl->SetFillMode(sink, D2D1_FILL_MODE_WINDING);
            sink_polys(sink, &polys, TRUE);
        }
        mime_vec_polys_free(&polys);
    }
    D2dArrayFree(&traps);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_area(ID2D1Geometry *This, const D2D1_MATRIX_3X2_F *m, FLOAT tol, FLOAT *area)
{
    SELF;
    if (!area)
        return E_POINTER;
    D2dArray traps = {};
    HRESULT hr = geo_traps(g, m, tol_of(tol), 4, nullptr, nullptr, &traps);
    if (SUCCEEDED(hr))
        *area = traps_area(&traps);
    D2dArrayFree(&traps);
    return hr;
}

static float poly_length(const MimeVecPolys *p)
{
    double len = 0;
    for (UINT32 i = 0; i < p->nsubs; i++) {
        const MimeVecSub *s = &p->subs[i];
        for (UINT32 j = 1; j < s->count; j++) {
            float dx = p->pts[s->first + j].x - p->pts[s->first + j - 1].x;
            float dy = p->pts[s->first + j].y - p->pts[s->first + j - 1].y;
            len += D2dSqrtF(dx * dx + dy * dy);
        }
        if (s->closed && s->count > 1) {
            float dx = p->pts[s->first].x - p->pts[s->first + s->count - 1].x;
            float dy = p->pts[s->first].y - p->pts[s->first + s->count - 1].y;
            len += D2dSqrtF(dx * dx + dy * dy);
        }
    }
    return (float)len;
}

static HRESULT STDMETHODCALLTYPE geo_length(ID2D1Geometry *This, const D2D1_MATRIX_3X2_F *m, FLOAT tol, FLOAT *length)
{
    SELF;
    if (!length)
        return E_POINTER;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dGeometryFlatten(g, m, tol_of(tol), FALSE, &polys, nullptr);
    if (SUCCEEDED(hr))
        *length = poly_length(&polys);
    mime_vec_polys_free(&polys);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_point_at(ID2D1Geometry *This, FLOAT length, const D2D1_MATRIX_3X2_F *m, FLOAT tol,
                                              D2D1_POINT_2F *point, D2D1_POINT_2F *tangent)
{
    SELF;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dGeometryFlatten(g, m, tol_of(tol), FALSE, &polys, nullptr);
    if (SUCCEEDED(hr)) {
        float remaining = D2dMaxF(length, 0.0f);
        D2D1_POINT_2F p = {}, t = {1, 0};
        BOOL found = FALSE;
        for (UINT32 i = 0; i < polys.nsubs && !found; i++) {
            const MimeVecSub *s = &polys.subs[i];
            UINT32 n = s->count + (s->closed && s->count > 1 ? 1 : 0);
            for (UINT32 j = 1; j < n; j++) {
                const MimeVecPoint *a = &polys.pts[s->first + j - 1], *b = &polys.pts[s->first + (j % s->count)];
                float dx = b->x - a->x, dy = b->y - a->y;
                float seg = D2dSqrtF(dx * dx + dy * dy);
                if (seg > 0.0f) {
                    t.x = dx / seg;
                    t.y = dy / seg;
                }
                if (remaining <= seg) {
                    float u = seg > 0 ? remaining / seg : 0;
                    p.x = a->x + dx * u;
                    p.y = a->y + dy * u;
                    found = TRUE;
                    break;
                }
                remaining -= seg;
                p.x = b->x;
                p.y = b->y;
            }
        }
        if (point)
            *point = p;
        if (tangent)
            *tangent = t;
    }
    mime_vec_polys_free(&polys);
    return hr;
}

static HRESULT STDMETHODCALLTYPE geo_widen(ID2D1Geometry *This, FLOAT width, ID2D1StrokeStyle *style,
                                           const D2D1_MATRIX_3X2_F *m, FLOAT tol, ID2D1SimplifiedGeometrySink *sink)
{
    SELF;
    if (!sink)
        return E_POINTER;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dStrokeGeometry(g, width, style, m, tol_of(tol), &polys);
    if (SUCCEEDED(hr)) {
        sink->lpVtbl->SetFillMode(sink, D2D1_FILL_MODE_WINDING);
        sink_polys(sink, &polys, TRUE);
    }
    mime_vec_polys_free(&polys);
    return hr;
}

#define GEO_BASE                                                                                                       \
    D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, geo_bounds, geo_widened, geo_stroke_contains,     \
        geo_fill_contains, geo_compare, geo_simplify, geo_tessellate, geo_combine, geo_outline, geo_area, geo_length,   \
        geo_point_at, geo_widen

/* ------------------------------------------------------------------ shapes, groups and transformed */

static void STDMETHODCALLTYPE rect_get(ID2D1RectangleGeometry *This, D2D1_RECT_F *r) { SELF; *r = g->shape.rect; }
static void STDMETHODCALLTYPE rounded_get(ID2D1RoundedRectangleGeometry *This, D2D1_ROUNDED_RECT *r)
{
    SELF;
    *r = g->shape.rounded;
}
static void STDMETHODCALLTYPE ellipse_get(ID2D1EllipseGeometry *This, D2D1_ELLIPSE *e) { SELF; *e = g->shape.ellipse; }

static D2D1_FILL_MODE STDMETHODCALLTYPE group_mode(ID2D1GeometryGroup *This) { SELF; return g->group_mode; }
static UINT32 STDMETHODCALLTYPE group_count(ID2D1GeometryGroup *This) { SELF; return g->child_count; }
static void STDMETHODCALLTYPE group_get(ID2D1GeometryGroup *This, ID2D1Geometry **list, UINT32 count)
{
    SELF;
    for (UINT32 i = 0; i < count && i < g->child_count; i++) {
        list[i] = g->children[i];
        D2D_ADDREF(list[i]);
    }
}

static void STDMETHODCALLTYPE transformed_source(ID2D1TransformedGeometry *This, ID2D1Geometry **out)
{
    SELF;
    *out = g->source;
    D2D_ADDREF(*out);
}
static void STDMETHODCALLTYPE transformed_matrix(ID2D1TransformedGeometry *This, D2D1_MATRIX_3X2_F *m)
{
    SELF;
    *m = g->transform;
}

static void geometry_destroy(D2dObj *o)
{
    D2dGeometry *g = (D2dGeometry *)o;
    D2dPathFree(&g->path);
    for (UINT32 i = 0; i < g->child_count; i++)
        D2D_RELEASE(g->children[i]);
    D2dFree(g->children);
    D2D_RELEASE(g->source);
}

static const void *const g_rect_vtbl[] = {GEO_BASE, rect_get};
static const void *const g_rounded_vtbl[] = {GEO_BASE, rounded_get};
static const void *const g_ellipse_vtbl[] = {GEO_BASE, ellipse_get};
static const void *const g_group_vtbl[] = {GEO_BASE, group_mode, group_count, group_get};
static const void *const g_transformed_vtbl[] = {GEO_BASE, transformed_source, transformed_matrix};

D2D_IIDS(g_rect_iids, &IID_ID2D1Resource, &IID_ID2D1Geometry, &IID_ID2D1RectangleGeometry);
D2D_IIDS(g_rounded_iids, &IID_ID2D1Resource, &IID_ID2D1Geometry, &IID_ID2D1RoundedRectangleGeometry);
D2D_IIDS(g_ellipse_iids, &IID_ID2D1Resource, &IID_ID2D1Geometry, &IID_ID2D1EllipseGeometry);
D2D_IIDS(g_group_iids, &IID_ID2D1Resource, &IID_ID2D1Geometry, &IID_ID2D1GeometryGroup);
D2D_IIDS(g_transformed_iids, &IID_ID2D1Resource, &IID_ID2D1Geometry, &IID_ID2D1TransformedGeometry);
D2D_IIDS(g_path_iids, &IID_ID2D1Resource, &IID_ID2D1Geometry, &IID_ID2D1PathGeometry, &IID_ID2D1PathGeometry1);
D2D_IIDS(g_stroke_iids, &IID_ID2D1Resource, &IID_ID2D1StrokeStyle, &IID_ID2D1StrokeStyle1);
static D2dClass g_stroke_class = {"stroke style", g_stroke_iids, stroke_destroy, nullptr};
D2D_IIDS(g_mesh_iids, &IID_ID2D1Resource, &IID_ID2D1Mesh);
D2D_IIDS(g_sink_iids, &IID_ID2D1SimplifiedGeometrySink, &IID_ID2D1GeometrySink);
D2D_IIDS(g_tess_iids, &IID_ID2D1TessellationSink);

static D2dClass g_rect_class = {"rectangle geometry", g_rect_iids, geometry_destroy, nullptr};
static D2dClass g_rounded_class = {"rounded rectangle geometry", g_rounded_iids, geometry_destroy, nullptr};
static D2dClass g_ellipse_class = {"ellipse geometry", g_ellipse_iids, geometry_destroy, nullptr};
static D2dClass g_group_class = {"geometry group", g_group_iids, geometry_destroy, nullptr};
static D2dClass g_transformed_class = {"transformed geometry", g_transformed_iids, geometry_destroy, nullptr};

HRESULT D2dCreateRectGeometry(D2dFactory *f, const D2D1_RECT_F *rect, ID2D1RectangleGeometry **out)
{
    D2dGeometry *g = D2dNew(sizeof(*g), g_rect_vtbl, &g_rect_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->kind = D2D_GEO_RECT;
    g->shape.rect = *rect;
    *out = (ID2D1RectangleGeometry *)g;
    return S_OK;
}

HRESULT D2dCreateRoundedGeometry(D2dFactory *f, const D2D1_ROUNDED_RECT *rect, ID2D1RoundedRectangleGeometry **out)
{
    D2dGeometry *g = D2dNew(sizeof(*g), g_rounded_vtbl, &g_rounded_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->kind = D2D_GEO_ROUNDED;
    g->shape.rounded = *rect;
    *out = (ID2D1RoundedRectangleGeometry *)g;
    return S_OK;
}

HRESULT D2dCreateEllipseGeometry(D2dFactory *f, const D2D1_ELLIPSE *e, ID2D1EllipseGeometry **out)
{
    D2dGeometry *g = D2dNew(sizeof(*g), g_ellipse_vtbl, &g_ellipse_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->kind = D2D_GEO_ELLIPSE;
    g->shape.ellipse = *e;
    *out = (ID2D1EllipseGeometry *)g;
    return S_OK;
}

HRESULT D2dCreateGroupGeometry(D2dFactory *f, D2D1_FILL_MODE mode, ID2D1Geometry **list, UINT32 count,
                               ID2D1GeometryGroup **out)
{
    for (UINT32 i = 0; i < count; i++) {
        if (!D2dGeometryOf(list[i]))
            return E_INVALIDARG;
    }
    D2dGeometry *g = D2dNew(sizeof(*g), g_group_vtbl, &g_group_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->kind = D2D_GEO_GROUP;
    g->group_mode = mode;
    g->children = D2dAllocZero((count ? count : 1) * sizeof(ID2D1Geometry *));
    if (!g->children) {
        D2dRelease(g);
        return E_OUTOFMEMORY;
    }
    for (UINT32 i = 0; i < count; i++) {
        g->children[i] = list[i];
        D2D_ADDREF(list[i]);
    }
    g->child_count = count;
    *out = (ID2D1GeometryGroup *)g;
    return S_OK;
}

HRESULT D2dCreateTransformedGeometry(D2dFactory *f, ID2D1Geometry *source, const D2D1_MATRIX_3X2_F *m,
                                     ID2D1TransformedGeometry **out)
{
    D2dGeometry *src = D2dGeometryOf(source);
    if (!src)
        return E_INVALIDARG;
    D2dGeometry *g = D2dNew(sizeof(*g), g_transformed_vtbl, &g_transformed_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->kind = D2D_GEO_TRANSFORMED;
    g->transform = *m;
    /* a transformed geometry of a transformed geometry is flattened into one level. */
    if (src->kind == D2D_GEO_TRANSFORMED) {
        D2dMatMul(&g->transform, &src->transform, m);
        g->source = src->source;
    } else {
        g->source = source;
    }
    D2D_ADDREF(g->source);
    *out = (ID2D1TransformedGeometry *)g;
    return S_OK;
}

/* ------------------------------------------------------------------ path geometry and its sink */

typedef struct D2dSink {
    D2dObj obj;
    D2dGeometry *owner;
    BOOL in_figure;
    BOOL closed;
    BOOL began_any;
    HRESULT error;
    UINT8 flags;
} D2dSink;

#define SINK D2dSink *s = (D2dSink *)This

static void STDMETHODCALLTYPE sink_fill_mode(ID2D1GeometrySink *This, D2D1_FILL_MODE mode)
{
    SINK;
    if (s->closed || s->in_figure || s->began_any) {
        if (!s->error)
            s->error = D2DERR_WRONG_STATE;
        return;
    }
    s->owner->path.fill_mode = mode;
}

static void STDMETHODCALLTYPE sink_flags(ID2D1GeometrySink *This, D2D1_PATH_SEGMENT flags)
{
    SINK;
    if (s->closed && !s->error)
        s->error = D2DERR_WRONG_STATE;
    s->flags = (UINT8)flags;
}

static void STDMETHODCALLTYPE sink_begin(ID2D1GeometrySink *This, D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin)
{
    SINK;
    if (s->closed || s->in_figure) {
        if (!s->error)
            s->error = D2DERR_WRONG_STATE;
        return;
    }
    s->in_figure = TRUE;
    s->began_any = TRUE;
    D2dPathBegin(&s->owner->path, start, begin);
}

static void sink_segment(D2dSink *s, const D2dSegment *seg)
{
    if (s->closed || !s->in_figure) {
        if (!s->error)
            s->error = D2DERR_WRONG_STATE;
        return;
    }
    D2dSegment c = *seg;
    c.flags = s->flags;
    D2dPathAdd(&s->owner->path, &c);
}

static void STDMETHODCALLTYPE sink_lines(ID2D1GeometrySink *This, const D2D1_POINT_2F *pts, UINT32 count)
{
    SINK;
    for (UINT32 i = 0; i < count; i++) {
        D2dSegment seg = {};
        seg.kind = D2D_SEG_LINE;
        seg.p[0] = pts[i];
        sink_segment(s, &seg);
    }
}

static void STDMETHODCALLTYPE sink_beziers(ID2D1GeometrySink *This, const D2D1_BEZIER_SEGMENT *b, UINT32 count)
{
    SINK;
    for (UINT32 i = 0; i < count; i++) {
        D2dSegment seg = {};
        seg.kind = D2D_SEG_BEZIER;
        seg.p[0] = b[i].point1;
        seg.p[1] = b[i].point2;
        seg.p[2] = b[i].point3;
        sink_segment(s, &seg);
    }
}

static void STDMETHODCALLTYPE sink_end(ID2D1GeometrySink *This, D2D1_FIGURE_END end)
{
    SINK;
    if (s->closed || !s->in_figure) {
        if (!s->error)
            s->error = D2DERR_WRONG_STATE;
        return;
    }
    s->in_figure = FALSE;
    D2dPathEnd(&s->owner->path, end);
}

static HRESULT STDMETHODCALLTYPE sink_close(ID2D1GeometrySink *This)
{
    SINK;
    if (s->closed)
        return D2DERR_WRONG_STATE;
    if (s->in_figure && !s->error)
        s->error = D2DERR_WRONG_STATE;
    s->closed = TRUE;
    if (s->error)
        return s->error;
    s->owner->path_closed = TRUE;
    s->owner->path_open = FALSE;
    return S_OK;
}

static void STDMETHODCALLTYPE sink_line(ID2D1GeometrySink *This, D2D1_POINT_2F pt) { sink_lines(This, &pt, 1); }
static void STDMETHODCALLTYPE sink_bezier(ID2D1GeometrySink *This, const D2D1_BEZIER_SEGMENT *b) { sink_beziers(This, b, 1); }

static void STDMETHODCALLTYPE sink_quads(ID2D1GeometrySink *This, const D2D1_QUADRATIC_BEZIER_SEGMENT *q, UINT32 count)
{
    SINK;
    for (UINT32 i = 0; i < count; i++) {
        D2dSegment seg = {};
        seg.kind = D2D_SEG_QUAD;
        seg.p[0] = q[i].point1;
        seg.p[1] = q[i].point2;
        sink_segment(s, &seg);
    }
}

static void STDMETHODCALLTYPE sink_quad(ID2D1GeometrySink *This, const D2D1_QUADRATIC_BEZIER_SEGMENT *q)
{
    sink_quads(This, q, 1);
}

static void STDMETHODCALLTYPE sink_arc(ID2D1GeometrySink *This, const D2D1_ARC_SEGMENT *arc)
{
    SINK;
    D2dSegment seg = {};
    seg.kind = D2D_SEG_ARC;
    seg.arc = *arc;
    sink_segment(s, &seg);
}

static void sink_destroy(D2dObj *o)
{
    D2dSink *s = (D2dSink *)o;
    if (!s->closed)
        s->owner->path_open = FALSE;
    D2dRelease(s->owner);
}

static const void *const g_sink_vtbl[] = {D2dQueryInterface, D2dAddRef,    D2dRelease, sink_fill_mode, sink_flags,
                                          sink_begin,        sink_lines,   sink_beziers, sink_end,       sink_close,
                                          sink_line,         sink_bezier,  sink_quad,   sink_quads,     sink_arc};

static HRESULT STDMETHODCALLTYPE path_open(ID2D1PathGeometry1 *This, ID2D1GeometrySink **out)
{
    SELF;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (g->path_open || g->path_closed)
        return D2DERR_WRONG_STATE;
    D2dSink *s = D2dNew(sizeof(*s), g_sink_vtbl, &g_sink_class, g->obj.factory);
    if (!s)
        return E_OUTOFMEMORY;
    s->owner = g;
    D2dAddRef(g);
    g->path_open = TRUE;
    *out = (ID2D1GeometrySink *)s;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE path_stream(ID2D1PathGeometry1 *This, ID2D1GeometrySink *sink)
{
    SELF;
    if (!sink)
        return E_POINTER;
    if (!g->path_closed)
        return D2DERR_WRONG_STATE;
    return emit_path(&g->path, (ID2D1SimplifiedGeometrySink *)sink, FALSE);
}

static HRESULT STDMETHODCALLTYPE path_segments(ID2D1PathGeometry1 *This, UINT32 *count)
{
    SELF;
    if (!count)
        return E_POINTER;
    if (!g->path_closed)
        return D2DERR_WRONG_STATE;
    *count = g->path.segments.count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE path_figures(ID2D1PathGeometry1 *This, UINT32 *count)
{
    SELF;
    if (!count)
        return E_POINTER;
    if (!g->path_closed)
        return D2DERR_WRONG_STATE;
    *count = g->path.figures.count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE path_point_and_segment(ID2D1PathGeometry1 *This, FLOAT length, UINT32 start,
                                                        const D2D1_MATRIX_3X2_F *m, FLOAT tol,
                                                        D2D1_POINT_DESCRIPTION *desc)
{
    SELF;
    if (!desc)
        return E_POINTER;
    D2D1_POINT_2F p, t;
    HRESULT hr = geo_point_at((ID2D1Geometry *)g, length, m, tol, &p, &t);
    if (SUCCEEDED(hr)) {
        desc->point = p;
        desc->unitTangentVector = t;
        desc->endSegment = start;
        desc->endFigure = 0;
        desc->lengthToEndSegment = length;
    }
    return hr;
}

static const void *const g_path_vtbl[] = {GEO_BASE, path_open, path_stream, path_segments, path_figures,
                                          path_point_and_segment};
static D2dClass g_path_class = {"path geometry", g_path_iids, geometry_destroy, nullptr};
static D2dClass g_sink_class = {"geometry sink", g_sink_iids, sink_destroy, nullptr};

HRESULT D2dCreatePathGeometry(D2dFactory *f, ID2D1PathGeometry1 **out)
{
    D2dGeometry *g = D2dNew(sizeof(*g), g_path_vtbl, &g_path_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->kind = D2D_GEO_PATH;
    g->path.fill_mode = D2D1_FILL_MODE_ALTERNATE;
    *out = (ID2D1PathGeometry1 *)g;
    return S_OK;
}

/* ------------------------------------------------------------------ meshes */

typedef struct D2dMesh {
    D2dObj obj;
    D2dArray triangles;
    BOOL open;
    BOOL closed;
} D2dMesh;

typedef struct D2dTessSink {
    D2dObj obj;
    D2dMesh *mesh;
    BOOL closed;
} D2dTessSink;

static void STDMETHODCALLTYPE tess_add(ID2D1TessellationSink *This, const D2D1_TRIANGLE *tri, UINT32 count)
{
    D2dTessSink *s = (D2dTessSink *)This;
    if (s->closed || !D2dArrayGrow(&s->mesh->triangles, sizeof(D2D1_TRIANGLE), count))
        return;
    memcpy((D2D1_TRIANGLE *)s->mesh->triangles.data + s->mesh->triangles.count, tri, count * sizeof(*tri));
    s->mesh->triangles.count += count;
}

static HRESULT STDMETHODCALLTYPE tess_close(ID2D1TessellationSink *This)
{
    D2dTessSink *s = (D2dTessSink *)This;
    if (s->closed)
        return D2DERR_WRONG_STATE;
    s->closed = TRUE;
    s->mesh->closed = TRUE;
    s->mesh->open = FALSE;
    return S_OK;
}

static void tess_destroy(D2dObj *o)
{
    D2dTessSink *s = (D2dTessSink *)o;
    if (!s->closed)
        s->mesh->open = FALSE;
    D2dRelease(s->mesh);
}

static const void *const g_tess_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, tess_add, tess_close};
static D2dClass g_tess_class = {"tessellation sink", g_tess_iids, tess_destroy, nullptr};

static HRESULT STDMETHODCALLTYPE mesh_open(ID2D1Mesh *This, ID2D1TessellationSink **out)
{
    D2dMesh *m = (D2dMesh *)This;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (m->open || m->closed)
        return D2DERR_WRONG_STATE;
    D2dTessSink *s = D2dNew(sizeof(*s), g_tess_vtbl, &g_tess_class, m->obj.factory);
    if (!s)
        return E_OUTOFMEMORY;
    s->mesh = m;
    D2dAddRef(m);
    m->open = TRUE;
    *out = (ID2D1TessellationSink *)s;
    return S_OK;
}

static void mesh_destroy(D2dObj *o) { D2dArrayFree(&((D2dMesh *)o)->triangles); }

static const void *const g_mesh_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, mesh_open};
static D2dClass g_mesh_class = {"mesh", g_mesh_iids, mesh_destroy, nullptr};

HRESULT D2dCreateMesh(D2dFactory *f, ID2D1Mesh **out)
{
    D2dMesh *m = D2dNew(sizeof(*m), g_mesh_vtbl, &g_mesh_class, f);
    if (!m)
        return E_OUTOFMEMORY;
    *out = (ID2D1Mesh *)m;
    return S_OK;
}

HRESULT D2dMeshPolys(ID2D1Mesh *mesh, const D2dMatrix *m, MimeVecPolys *out)
{
    D2dMesh *me = (D2dMesh *)mesh;
    if (!D2dIsOfClass(me, &g_mesh_class))
        return E_INVALIDARG;
    if (!me->closed)
        return D2DERR_WRONG_STATE;
    const D2D1_TRIANGLE *t = me->triangles.data;
    for (UINT32 i = 0; i < me->triangles.count; i++) {
        MimeVecPoint pts[3];
        const D2D1_POINT_2F *src = &t[i].point1;
        for (int k = 0; k < 3; k++) {
            D2D1_POINT_2F p = D2dMatApply(m, src[k].x, src[k].y);
            pts[k].x = p.x;
            pts[k].y = p.y;
        }
        if (mime_vec_polys_add_sub(out, pts, 3, true) != MIME_VEC_OK)
            return E_OUTOFMEMORY;
    }
    return S_OK;
}
