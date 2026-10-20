/*
 * the drawing calls: outlines with a pen, fills with a brush, of the shapes the flat api knows. each builds a
 * path in world space and hands it to the drawing core.
 */
#include "gp_private.h"

#define GFX(g, p)                       \
    GGraphics *g = gp_graphics_cast(p); \
    if (!g)                             \
        return InvalidParameter;        \
    if (g->got_dc)                      \
        return ObjectBusy

static GpStatus vec_status(int e) { return gp_path_status(e); }

/* ------------------------------------------------------------------ strokes and fills of a path */

GpStatus gp_stroke_path(GGraphics *g, const MimeVecPath *path, const GPen *pen)
{
    if (!path->count)
        return Ok;
    if (g->meta)
        return gp_rec_stroke_path(g, path, pen);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st = gp_pen_widen_ex(pen, path, gp_graphics_w2d(g), MIME_VEC_DEFAULT_FLATNESS, g->dpi_x, &polys);
    if (st == Ok)
        st = gp_fill_polys(g, &polys, MIME_VEC_FILL_WINDING, gp_smoothing_samples(g->smoothing), pen->brush);
    mime_vec_polys_free(&polys);
    return st;
}

GpStatus gp_fill_rect_world(GGraphics *g, const GBrush *brush, float x, float y, float w, float h)
{
    if (g->meta)
        return gp_rec_fill_rect(g, brush, x, y, w, h);
    MimeVecPath p;
    mime_vec_path_init(&p, &gp_allocator);
    GpStatus st = vec_status(mime_vec_path_add_rect(&p, x, y, w, h));
    if (st == Ok)
        st = gp_fill_path(g, &p, FillModeWinding, brush);
    mime_vec_path_free(&p);
    return st;
}

/* a temporary path for the shapes. */
typedef struct Tmp {
    MimeVecPath p;
} Tmp;

static void tmp_init(Tmp *t) { mime_vec_path_init(&t->p, &gp_allocator); }
static void tmp_free(Tmp *t) { mime_vec_path_free(&t->p); }

static GpStatus draw_tmp(GGraphics *g, GPen *pen, Tmp *t, GpStatus built)
{
    GpStatus st = built;
    if (st == Ok)
        st = gp_stroke_path(g, &t->p, pen);
    tmp_free(t);
    return st;
}

static GpStatus fill_tmp(GGraphics *g, GBrush *brush, Tmp *t, FillMode mode, GpStatus built)
{
    GpStatus st = built;
    if (st == Ok)
        st = gp_fill_path(g, &t->p, mode, brush);
    tmp_free(t);
    return st;
}

#define PEN_OR_FAIL(pen_in, pn)         \
    GPen *pn = gp_pen_cast(pen_in);     \
    if (!pn)                            \
        return InvalidParameter

#define BRUSH_OR_FAIL(brush_in, br)         \
    GBrush *br = gp_brush_cast(brush_in);   \
    if (!br)                                \
        return InvalidParameter

static void norm_rect(float *x, float *y, float *w, float *h)
{
    if (*w < 0.0f) {
        *x += *w;
        *w = -*w;
    }
    if (*h < 0.0f) {
        *y += *h;
        *h = -*h;
    }
}

/* ------------------------------------------------------------------ lines and curves */

WINGDIPAPI GpStatus WINAPI GdipDrawLine(GpGraphics *graphics, GpPen *pen, REAL x1, REAL y1, REAL x2, REAL y2)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_line(&t.p, x1, y1, x2, y2)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawLineI(GpGraphics *graphics, GpPen *pen, INT x1, INT y1, INT x2, INT y2)
{
    return GdipDrawLine(graphics, pen, (float)x1, (float)y1, (float)x2, (float)y2);
}

WINGDIPAPI GpStatus WINAPI GdipDrawLines(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!points || count < 2)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_lines(&t.p, (const MimeVecPoint *)points, (UINT)count)));
}

static MimeVecPoint *int_points(const GpPoint *points, INT count)
{
    MimeVecPoint *f = gp_alloc(sizeof(MimeVecPoint) * (count > 0 ? count : 1));
    if (!f)
        return nullptr;
    for (INT i = 0; i < count; i++) {
        f[i].x = (float)points[i].X;
        f[i].y = (float)points[i].Y;
    }
    return f;
}

WINGDIPAPI GpStatus WINAPI GdipDrawLinesI(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count)
{
    if (!points || count < 2)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipDrawLines(graphics, pen, (const GpPointF *)f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawArc(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL w, REAL h, REAL start,
                                       REAL sweep)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (w <= 0.0f || h <= 0.0f)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_arc(&t.p, x, y, w, h, start, sweep)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawArcI(GpGraphics *graphics, GpPen *pen, INT x, INT y, INT w, INT h, REAL start,
                                        REAL sweep)
{
    return GdipDrawArc(graphics, pen, (float)x, (float)y, (float)w, (float)h, start, sweep);
}

WINGDIPAPI GpStatus WINAPI GdipDrawBezier(GpGraphics *graphics, GpPen *pen, REAL x1, REAL y1, REAL x2, REAL y2, REAL x3,
                                          REAL y3, REAL x4, REAL y4)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_bezier(&t.p, x1, y1, x2, y2, x3, y3, x4, y4)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawBezierI(GpGraphics *graphics, GpPen *pen, INT x1, INT y1, INT x2, INT y2, INT x3,
                                           INT y3, INT x4, INT y4)
{
    return GdipDrawBezier(graphics, pen, (float)x1, (float)y1, (float)x2, (float)y2, (float)x3, (float)y3, (float)x4,
                          (float)y4);
}

WINGDIPAPI GpStatus WINAPI GdipDrawBeziers(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!points || count < 4 || (count - 1) % 3)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_beziers(&t.p, (const MimeVecPoint *)points, (UINT)count)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawBeziersI(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count)
{
    if (!points || count < 4)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipDrawBeziers(graphics, pen, (const GpPointF *)f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawCurve2(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count,
                                          REAL tension)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!points || count < 2)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t,
                    vec_status(mime_vec_path_add_curve(&t.p, (const MimeVecPoint *)points, (UINT)count, tension)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawCurve(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count)
{
    return GdipDrawCurve2(graphics, pen, points, count, 0.5f);
}

WINGDIPAPI GpStatus WINAPI GdipDrawCurve3(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count,
                                          INT offset, INT segments, REAL tension)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!points || count < 2 || offset < 0 || segments < 1 || offset + segments >= count)
        return InvalidParameter;
    return GdipDrawCurve2(graphics, pen, points + offset, segments + 1, tension);
}

#define DRAW_CURVE_I(call, ...)                    \
    if (!points || count < 2)                      \
        return InvalidParameter;                   \
    MimeVecPoint *f = int_points(points, count);   \
    if (!f)                                        \
        return OutOfMemory;                        \
    GpStatus st = call(__VA_ARGS__);               \
    gp_free(f);                                    \
    return st

WINGDIPAPI GpStatus WINAPI GdipDrawCurveI(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count)
{
    DRAW_CURVE_I(GdipDrawCurve, graphics, pen, (const GpPointF *)f, count);
}

WINGDIPAPI GpStatus WINAPI GdipDrawCurve2I(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count,
                                           REAL tension)
{
    DRAW_CURVE_I(GdipDrawCurve2, graphics, pen, (const GpPointF *)f, count, tension);
}

WINGDIPAPI GpStatus WINAPI GdipDrawCurve3I(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count,
                                           INT offset, INT segments, REAL tension)
{
    DRAW_CURVE_I(GdipDrawCurve3, graphics, pen, (const GpPointF *)f, count, offset, segments, tension);
}

WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurve2(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count,
                                                REAL tension)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!points || count < 3)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t,
                    vec_status(mime_vec_path_add_closed_curve(&t.p, (const MimeVecPoint *)points, (UINT)count, tension)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurve(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count)
{
    return GdipDrawClosedCurve2(graphics, pen, points, count, 0.5f);
}

WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurveI(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count)
{
    if (!points || count < 3)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipDrawClosedCurve(graphics, pen, (const GpPointF *)f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurve2I(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count,
                                                 REAL tension)
{
    if (!points || count < 3)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipDrawClosedCurve2(graphics, pen, (const GpPointF *)f, count, tension);
    gp_free(f);
    return st;
}

/* ------------------------------------------------------------------ closed shapes */

WINGDIPAPI GpStatus WINAPI GdipDrawRectangle(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL w, REAL h)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    norm_rect(&x, &y, &w, &h);
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_rect(&t.p, x, y, w, h)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawRectangleI(GpGraphics *graphics, GpPen *pen, INT x, INT y, INT w, INT h)
{
    return GdipDrawRectangle(graphics, pen, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipDrawRectangles(GpGraphics *graphics, GpPen *pen, const GpRectF *rects, INT count)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!rects || count < 1)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    GpStatus st = Ok;
    for (INT i = 0; i < count && st == Ok; i++) {
        float x = rects[i].X, y = rects[i].Y, w = rects[i].Width, h = rects[i].Height;
        norm_rect(&x, &y, &w, &h);
        st = vec_status(mime_vec_path_add_rect(&t.p, x, y, w, h));
    }
    return draw_tmp(g, pn, &t, st);
}

WINGDIPAPI GpStatus WINAPI GdipDrawRectanglesI(GpGraphics *graphics, GpPen *pen, const GpRect *rects, INT count)
{
    if (!rects || count < 1)
        return InvalidParameter;
    GpRectF *f = gp_alloc(sizeof(GpRectF) * count);
    if (!f)
        return OutOfMemory;
    for (INT i = 0; i < count; i++) {
        f[i].X = (float)rects[i].X;
        f[i].Y = (float)rects[i].Y;
        f[i].Width = (float)rects[i].Width;
        f[i].Height = (float)rects[i].Height;
    }
    GpStatus st = GdipDrawRectangles(graphics, pen, f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawEllipse(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL w, REAL h)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    norm_rect(&x, &y, &w, &h);
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_ellipse(&t.p, x, y, w, h)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawEllipseI(GpGraphics *graphics, GpPen *pen, INT x, INT y, INT w, INT h)
{
    return GdipDrawEllipse(graphics, pen, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipDrawPie(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL w, REAL h, REAL start,
                                       REAL sweep)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (w <= 0.0f || h <= 0.0f)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return draw_tmp(g, pn, &t, vec_status(mime_vec_path_add_pie(&t.p, x, y, w, h, start, sweep)));
}

WINGDIPAPI GpStatus WINAPI GdipDrawPieI(GpGraphics *graphics, GpPen *pen, INT x, INT y, INT w, INT h, REAL start,
                                        REAL sweep)
{
    return GdipDrawPie(graphics, pen, (float)x, (float)y, (float)w, (float)h, start, sweep);
}

WINGDIPAPI GpStatus WINAPI GdipDrawPolygon(GpGraphics *graphics, GpPen *pen, const GpPointF *points, INT count)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    if (!points || count < 2)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    GpStatus st;
    if (count == 2)
        st = vec_status(mime_vec_path_add_lines(&t.p, (const MimeVecPoint *)points, 2));
    else
        st = vec_status(mime_vec_path_add_polygon(&t.p, (const MimeVecPoint *)points, (UINT)count));
    return draw_tmp(g, pn, &t, st);
}

WINGDIPAPI GpStatus WINAPI GdipDrawPolygonI(GpGraphics *graphics, GpPen *pen, const GpPoint *points, INT count)
{
    if (!points || count < 2)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipDrawPolygon(graphics, pen, (const GpPointF *)f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawPath(GpGraphics *graphics, GpPen *pen, GpPath *path)
{
    GFX(g, graphics);
    PEN_OR_FAIL(pen, pn);
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    return gp_stroke_path(g, &p->path, pn);
}

/* ------------------------------------------------------------------ fills */

WINGDIPAPI GpStatus WINAPI GdipFillRectangle(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL w, REAL h)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    norm_rect(&x, &y, &w, &h);
    return gp_fill_rect_world(g, br, x, y, w, h);
}

WINGDIPAPI GpStatus WINAPI GdipFillRectangleI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT w, INT h)
{
    return GdipFillRectangle(graphics, brush, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipFillRectangles(GpGraphics *graphics, GpBrush *brush, const GpRectF *rects, INT count)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    if (!rects || count < 1)
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        float x = rects[i].X, y = rects[i].Y, w = rects[i].Width, h = rects[i].Height;
        norm_rect(&x, &y, &w, &h);
        GpStatus st = gp_fill_rect_world(g, br, x, y, w, h);
        if (st != Ok)
            return st;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipFillRectanglesI(GpGraphics *graphics, GpBrush *brush, const GpRect *rects, INT count)
{
    if (!rects || count < 1)
        return InvalidParameter;
    GpRectF *f = gp_alloc(sizeof(GpRectF) * count);
    if (!f)
        return OutOfMemory;
    for (INT i = 0; i < count; i++) {
        f[i].X = (float)rects[i].X;
        f[i].Y = (float)rects[i].Y;
        f[i].Width = (float)rects[i].Width;
        f[i].Height = (float)rects[i].Height;
    }
    GpStatus st = GdipFillRectangles(graphics, brush, f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipFillPolygon(GpGraphics *graphics, GpBrush *brush, const GpPointF *points, INT count,
                                           GpFillMode mode)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    if (!points || count < 3)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return fill_tmp(g, br, &t, mode,
                    vec_status(mime_vec_path_add_polygon(&t.p, (const MimeVecPoint *)points, (UINT)count)));
}

WINGDIPAPI GpStatus WINAPI GdipFillPolygonI(GpGraphics *graphics, GpBrush *brush, const GpPoint *points, INT count,
                                            GpFillMode mode)
{
    if (!points || count < 3)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipFillPolygon(graphics, brush, (const GpPointF *)f, count, mode);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipFillPolygon2(GpGraphics *graphics, GpBrush *brush, const GpPointF *points, INT count)
{
    return GdipFillPolygon(graphics, brush, points, count, FillModeAlternate);
}

WINGDIPAPI GpStatus WINAPI GdipFillPolygon2I(GpGraphics *graphics, GpBrush *brush, const GpPoint *points, INT count)
{
    return GdipFillPolygonI(graphics, brush, points, count, FillModeAlternate);
}

WINGDIPAPI GpStatus WINAPI GdipFillEllipse(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL w, REAL h)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    norm_rect(&x, &y, &w, &h);
    Tmp t;
    tmp_init(&t);
    return fill_tmp(g, br, &t, FillModeAlternate, vec_status(mime_vec_path_add_ellipse(&t.p, x, y, w, h)));
}

WINGDIPAPI GpStatus WINAPI GdipFillEllipseI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT w, INT h)
{
    return GdipFillEllipse(graphics, brush, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipFillPie(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL w, REAL h,
                                       REAL start, REAL sweep)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    if (w <= 0.0f || h <= 0.0f)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return fill_tmp(g, br, &t, FillModeAlternate, vec_status(mime_vec_path_add_pie(&t.p, x, y, w, h, start, sweep)));
}

WINGDIPAPI GpStatus WINAPI GdipFillPieI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT w, INT h, REAL start,
                                        REAL sweep)
{
    return GdipFillPie(graphics, brush, (float)x, (float)y, (float)w, (float)h, start, sweep);
}

WINGDIPAPI GpStatus WINAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    return gp_fill_path(g, &p->path, p->fill_mode, br);
}

WINGDIPAPI GpStatus WINAPI GdipFillClosedCurve2(GpGraphics *graphics, GpBrush *brush, const GpPointF *points,
                                                INT count, REAL tension, GpFillMode mode)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    if (!points || count < 3)
        return InvalidParameter;
    Tmp t;
    tmp_init(&t);
    return fill_tmp(g, br, &t, mode,
                    vec_status(mime_vec_path_add_closed_curve(&t.p, (const MimeVecPoint *)points, (UINT)count, tension)));
}

WINGDIPAPI GpStatus WINAPI GdipFillClosedCurve(GpGraphics *graphics, GpBrush *brush, const GpPointF *points, INT count)
{
    return GdipFillClosedCurve2(graphics, brush, points, count, 0.5f, FillModeAlternate);
}

WINGDIPAPI GpStatus WINAPI GdipFillClosedCurveI(GpGraphics *graphics, GpBrush *brush, const GpPoint *points, INT count)
{
    if (!points || count < 3)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipFillClosedCurve(graphics, brush, (const GpPointF *)f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipFillClosedCurve2I(GpGraphics *graphics, GpBrush *brush, const GpPoint *points,
                                                 INT count, REAL tension, GpFillMode mode)
{
    if (!points || count < 3)
        return InvalidParameter;
    MimeVecPoint *f = int_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipFillClosedCurve2(graphics, brush, (const GpPointF *)f, count, tension, mode);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipFillRegion(GpGraphics *graphics, GpBrush *brush, GpRegion *region)
{
    GFX(g, graphics);
    BRUSH_OR_FAIL(brush, br);
    GRegion *r = gp_region_cast(region);
    if (!r)
        return InvalidParameter;
    if (g->meta)
        return gp_rec_fill_region(g, br, r);
    MimeVecRect clip;
    RECT b;
    gp_graphics_bounds(g, &b);
    clip.left = b.left;
    clip.top = b.top;
    clip.right = b.right;
    clip.bottom = b.bottom;
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    GpStatus st = gp_region_to_mask(r, gp_graphics_w2d(g), &clip, &mask);
    if (st == Ok)
        st = gp_fill_mask(g, &mask, br);
    mime_vec_mask_free(&mask);
    return st;
}

/* ------------------------------------------------------------------ clearing */

WINGDIPAPI GpStatus WINAPI GdipGraphicsClear(GpGraphics *graphics, ARGB color)
{
    GFX(g, graphics);
    if (g->meta)
        return gp_rec_clear(g, color);
    RECT area;
    gp_graphics_bounds(g, &area);
    const MimeVecMask *mask;
    if (!gp_clip_narrow(g, &area, &mask))
        return Ok;
    GSurface s;
    GpStatus st = gp_surface_acquire(g, &area, &s);
    if (st != Ok)
        return st;
    UINT32 px = gp_premultiply(color);
    UINT32 row[1024];
    UINT8 mrow[1024];
    for (INT y = area.top; y < area.bottom; y++) {
        for (INT x = area.left; x < area.right; x += 1024) {
            INT n = area.right - x > 1024 ? 1024 : area.right - x;
            if (!mask) {
                for (INT i = 0; i < n; i++)
                    row[i] = px;
            } else {
                memset(mrow, 255, n);
                mime_vec_mask_apply_row(mask, y, x, n, mrow);
                gp_surface_read(&s, x, y, n, row);
                for (INT i = 0; i < n; i++) {
                    if (mrow[i] == 255)
                        row[i] = px;
                    else if (mrow[i])
                        row[i] = gp_scale_pargb(px, mrow[i]) + gp_scale_pargb(row[i], 255 - mrow[i]);
                }
            }
            gp_surface_write(&s, x, y, n, row);
        }
    }
    gp_surface_release(g, &s, TRUE);
    return Ok;
}
