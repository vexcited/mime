/*
 * paths: the GraphicsPath of the flat api over the vector library's path builder, the point and type arrays of
 * gdi+, their editing, flattening, widening and warping, hit testing and the iterator.
 */
#include "gp_private.h"

GPath *gp_path_cast(const void *p)
{
    return GP_CAST(GPath, GP_TAG_PATH, p);
}

GpStatus gp_path_status(int e)
{
    switch (e) {
    case MIME_VEC_OK:
        return Ok;
    case MIME_VEC_NOMEM:
    case MIME_VEC_TOOBIG:
        return OutOfMemory;
    default:
        return InvalidParameter;
    }
}

GPath *gp_path_new(FillMode mode)
{
    GPath *p = gp_alloc_zero(sizeof(*p));
    if (!p)
        return nullptr;
    p->tag = GP_TAG_PATH;
    p->fill_mode = mode;
    mime_vec_path_init(&p->path, &gp_allocator);
    return p;
}

void gp_path_free(GPath *p)
{
    if (!p)
        return;
    mime_vec_path_free(&p->path);
    p->tag = 0;
    gp_free(p);
}

GPath *gp_path_clone(const GPath *src)
{
    GPath *p = gp_path_new(src->fill_mode);
    if (!p)
        return nullptr;
    if (mime_vec_path_copy(&p->path, &src->path) != MIME_VEC_OK) {
        gp_path_free(p);
        return nullptr;
    }
    return p;
}

GpStatus gp_path_flatten(const MimeVecPath *path, const MimeVecMatrix *m, float flatness, MimeVecPolys *out)
{
    return gp_path_status(mime_vec_path_flatten(path, m, flatness, out));
}

/* ------------------------------------------------------------------ creation */

WINGDIPAPI GpStatus WINAPI GdipCreatePath(GpFillMode mode, GpPath **path)
{
    if (!path)
        return InvalidParameter;
    GPath *p = gp_path_new(mode);
    *path = p;
    return p ? Ok : OutOfMemory;
}

BOOL gp_path_types_valid(const BYTE *types, INT count)
{
    if (count <= 0)
        return TRUE;
    if ((types[0] & MIME_VEC_PT_TYPE_MASK) != MIME_VEC_PT_START)
        return FALSE;
    for (INT i = 0; i < count; i++) {
        BYTE t = types[i] & MIME_VEC_PT_TYPE_MASK;
        if (t == 2 || t > 3)
            return FALSE;
        if (t == MIME_VEC_PT_BEZIER) {
            if (i + 2 >= count || (types[i + 1] & MIME_VEC_PT_TYPE_MASK) != MIME_VEC_PT_BEZIER ||
                (types[i + 2] & MIME_VEC_PT_TYPE_MASK) != MIME_VEC_PT_BEZIER)
                return FALSE;
            i += 2;
        }
    }
    return TRUE;
}

WINGDIPAPI GpStatus WINAPI GdipCreatePath2(const GpPointF *points, const BYTE *types, INT count, GpFillMode mode,
                                           GpPath **path)
{
    if (!path || !points || !types || count < 0)
        return InvalidParameter;
    if (!gp_path_types_valid(types, count))
        return InvalidParameter;
    GPath *p = gp_path_new(mode);
    if (!p)
        return OutOfMemory;
    if (count && mime_vec_path_add_raw(&p->path, (const MimeVecPoint *)points, types, (UINT)count) != MIME_VEC_OK) {
        gp_path_free(p);
        return OutOfMemory;
    }
    *path = p;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreatePath2I(const GpPoint *points, const BYTE *types, INT count, GpFillMode mode,
                                            GpPath **path)
{
    if (!points || count < 0)
        return InvalidParameter;
    GpPointF *f = count ? gp_alloc(sizeof(GpPointF) * count) : nullptr;
    if (count && !f)
        return OutOfMemory;
    for (INT i = 0; i < count; i++) {
        f[i].X = (float)points[i].X;
        f[i].Y = (float)points[i].Y;
    }
    GpStatus st = GdipCreatePath2(f ? f : (const GpPointF *)points, types, count, mode, path);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipClonePath(GpPath *path, GpPath **clone)
{
    GPath *p = gp_path_cast(path);
    if (!p || !clone)
        return InvalidParameter;
    GPath *c = gp_path_clone(p);
    *clone = c;
    return c ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipDeletePath(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    gp_path_free(p);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipResetPath(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    mime_vec_path_clear(&p->path);
    p->fill_mode = FillModeAlternate;
    return Ok;
}

/* ------------------------------------------------------------------ the arrays */

WINGDIPAPI GpStatus WINAPI GdipGetPointCount(GpPath *path, INT *count)
{
    GPath *p = gp_path_cast(path);
    if (!p || !count)
        return InvalidParameter;
    *count = (INT)p->path.count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathTypes(GpPath *path, BYTE *types, INT count)
{
    GPath *p = gp_path_cast(path);
    if (!p || !types)
        return InvalidParameter;
    if (count < (INT)p->path.count)
        return InsufficientBuffer;
    memcpy(types, p->path.types, p->path.count);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathPoints(GpPath *path, GpPointF *points, INT count)
{
    GPath *p = gp_path_cast(path);
    if (!p || !points)
        return InvalidParameter;
    if (count < (INT)p->path.count)
        return InsufficientBuffer;
    memcpy(points, p->path.pts, p->path.count * sizeof(GpPointF));
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathPointsI(GpPath *path, GpPoint *points, INT count)
{
    GPath *p = gp_path_cast(path);
    if (!p || !points)
        return InvalidParameter;
    if (count < (INT)p->path.count)
        return InsufficientBuffer;
    for (UINT i = 0; i < p->path.count; i++) {
        float x = p->path.pts[i].x, y = p->path.pts[i].y;
        points[i].X = (INT)mime_vec_floorf(x + 0.5f);
        points[i].Y = (INT)mime_vec_floorf(y + 0.5f);
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathFillMode(GpPath *path, GpFillMode *mode)
{
    GPath *p = gp_path_cast(path);
    if (!p || !mode)
        return InvalidParameter;
    *mode = p->fill_mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathFillMode(GpPath *path, GpFillMode mode)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    p->fill_mode = mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathData(GpPath *path, GpPathData *data_)
{
    GPath *p = gp_path_cast(path);
    PathData *data = data_;
    if (!p || !data)
        return InvalidParameter;
    if (data->Count < (INT)p->path.count)
        return InsufficientBuffer;
    if (p->path.count) {
        if (!data->Points || !data->Types)
            return InvalidParameter;
        memcpy(data->Points, p->path.pts, p->path.count * sizeof(GpPointF));
        memcpy(data->Types, p->path.types, p->path.count);
    }
    data->Count = (INT)p->path.count;
    return Ok;
}

/* ------------------------------------------------------------------ figures and markers */

WINGDIPAPI GpStatus WINAPI GdipStartPathFigure(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    mime_vec_path_start_figure(&p->path);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipClosePathFigure(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_close_figure(&p->path));
}

WINGDIPAPI GpStatus WINAPI GdipClosePathFigures(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    /* every figure that is open gets closed: its last point takes the close flag */
    for (UINT i = 0; i < p->path.count; i++) {
        BOOL last = i + 1 == p->path.count || (p->path.types[i + 1] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_START;
        if (last && (p->path.types[i] & MIME_VEC_PT_TYPE_MASK) != MIME_VEC_PT_START)
            p->path.types[i] |= MIME_VEC_PT_CLOSE;
    }
    p->path.need_start = TRUE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathMarker(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    if (!p->path.count)
        return Ok;
    p->path.types[p->path.count - 1] |= MIME_VEC_PT_MARKER;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipClearPathMarkers(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    for (UINT i = 0; i < p->path.count; i++)
        p->path.types[i] &= (BYTE)~MIME_VEC_PT_MARKER;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipReversePath(GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    UINT n = p->path.count;
    if (n < 2)
        return Ok;
    MimeVecPoint *pts = gp_alloc(sizeof(MimeVecPoint) * n);
    BYTE *types = gp_alloc(n);
    if (!pts || !types) {
        gp_free(pts);
        gp_free(types);
        return OutOfMemory;
    }
    /* figures are reversed in place and in order: the whole array is read backwards */
    UINT out = 0;
    UINT end = n;
    while (end > 0) {
        UINT start = end - 1;
        while (start > 0 && (p->path.types[start] & MIME_VEC_PT_TYPE_MASK) != MIME_VEC_PT_START)
            start--;
        BOOL closed = (p->path.types[end - 1] & MIME_VEC_PT_CLOSE) != 0;
        for (UINT k = 0; k < end - start; k++) {
            UINT src = end - 1 - k;
            pts[out + k] = p->path.pts[src];
            BYTE keep = p->path.types[src] & (BYTE)(MIME_VEC_PT_DASHMODE | MIME_VEC_PT_MARKER);
            BYTE t = k == 0 ? MIME_VEC_PT_START : (p->path.types[src + 1] & MIME_VEC_PT_TYPE_MASK);
            if (k && (p->path.types[src + 1] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_START)
                t = MIME_VEC_PT_LINE;
            types[out + k] = (BYTE)(t | keep);
        }
        if (closed)
            types[out + end - start - 1] |= MIME_VEC_PT_CLOSE;
        out += end - start;
        end = start;
    }
    memcpy(p->path.pts, pts, sizeof(MimeVecPoint) * n);
    memcpy(p->path.types, types, n);
    gp_free(pts);
    gp_free(types);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathLastPoint(GpPath *path, GpPointF *point)
{
    GPath *p = gp_path_cast(path);
    if (!p || !point)
        return InvalidParameter;
    if (!p->path.count)
        return InvalidParameter;
    *point = *(GpPointF *)&p->path.pts[p->path.count - 1];
    return Ok;
}

/* ------------------------------------------------------------------ the shapes */

#define PATH_OR_FAIL(p, path)       \
    GPath *p = gp_path_cast(path);  \
    if (!p)                         \
        return InvalidParameter

WINGDIPAPI GpStatus WINAPI GdipAddPathLine(GpPath *path, REAL x1, REAL y1, REAL x2, REAL y2)
{
    PATH_OR_FAIL(p, path);
    return gp_path_status(mime_vec_path_add_line(&p->path, x1, y1, x2, y2));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathLine2(GpPath *path, const GpPointF *points, INT count)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 2)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_lines(&p->path, (const MimeVecPoint *)points, (UINT)count));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathArc(GpPath *path, REAL x, REAL y, REAL w, REAL h, REAL start, REAL sweep)
{
    PATH_OR_FAIL(p, path);
    if (w <= 0.0f || h <= 0.0f)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_arc(&p->path, x, y, w, h, start, sweep));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathBezier(GpPath *path, REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3,
                                             REAL x4, REAL y4)
{
    PATH_OR_FAIL(p, path);
    return gp_path_status(mime_vec_path_add_bezier(&p->path, x1, y1, x2, y2, x3, y3, x4, y4));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathBeziers(GpPath *path, const GpPointF *points, INT count)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 4 || (count - 1) % 3)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_beziers(&p->path, (const MimeVecPoint *)points, (UINT)count));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathCurve(GpPath *path, const GpPointF *points, INT count)
{
    return GdipAddPathCurve2(path, points, count, 0.5f);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathCurve2(GpPath *path, const GpPointF *points, INT count, REAL tension)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 2)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_curve(&p->path, (const MimeVecPoint *)points, (UINT)count, tension));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathCurve3(GpPath *path, const GpPointF *points, INT count, INT offset, INT segments,
                                             REAL tension)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 2 || offset < 0 || segments < 1 || offset + segments >= count)
        return InvalidParameter;
    return gp_path_status(
        mime_vec_path_add_curve(&p->path, (const MimeVecPoint *)points + offset, (UINT)segments + 1, tension));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurve(GpPath *path, const GpPointF *points, INT count)
{
    return GdipAddPathClosedCurve2(path, points, count, 0.5f);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurve2(GpPath *path, const GpPointF *points, INT count, REAL tension)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 3)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_closed_curve(&p->path, (const MimeVecPoint *)points, (UINT)count, tension));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathRectangle(GpPath *path, REAL x, REAL y, REAL w, REAL h)
{
    PATH_OR_FAIL(p, path);
    if (w < 0.0f || h < 0.0f)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_rect(&p->path, x, y, w, h));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathRectangles(GpPath *path, const GpRectF *rects, INT count)
{
    PATH_OR_FAIL(p, path);
    if (!rects || count < 1)
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        if (rects[i].Width < 0.0f || rects[i].Height < 0.0f)
            return InvalidParameter;
    }
    for (INT i = 0; i < count; i++) {
        int e = mime_vec_path_add_rect(&p->path, rects[i].X, rects[i].Y, rects[i].Width, rects[i].Height);
        if (e)
            return gp_path_status(e);
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipAddPathEllipse(GpPath *path, REAL x, REAL y, REAL w, REAL h)
{
    PATH_OR_FAIL(p, path);
    if (w < 0.0f || h < 0.0f)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_ellipse(&p->path, x, y, w, h));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathPie(GpPath *path, REAL x, REAL y, REAL w, REAL h, REAL start, REAL sweep)
{
    PATH_OR_FAIL(p, path);
    if (w < 0.0f || h < 0.0f)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_pie(&p->path, x, y, w, h, start, sweep));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathPolygon(GpPath *path, const GpPointF *points, INT count)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 3)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_add_polygon(&p->path, (const MimeVecPoint *)points, (UINT)count));
}

WINGDIPAPI GpStatus WINAPI GdipAddPathPath(GpPath *path, const GpPath *add, BOOL connect)
{
    PATH_OR_FAIL(p, path);
    GPath *a = gp_path_cast(add);
    if (!a)
        return InvalidParameter;
    return gp_path_status(mime_vec_path_append(&p->path, &a->path, connect != FALSE));
}

/* the integer variants turn their points into floats and call the float ones. */
static GpPointF *to_float_points(const GpPoint *points, INT count)
{
    GpPointF *f = gp_alloc(sizeof(GpPointF) * (count > 0 ? count : 1));
    if (!f)
        return nullptr;
    for (INT i = 0; i < count; i++) {
        f[i].X = (float)points[i].X;
        f[i].Y = (float)points[i].Y;
    }
    return f;
}

WINGDIPAPI GpStatus WINAPI GdipAddPathLineI(GpPath *path, INT x1, INT y1, INT x2, INT y2)
{
    return GdipAddPathLine(path, (float)x1, (float)y1, (float)x2, (float)y2);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathLine2I(GpPath *path, const GpPoint *points, INT count)
{
    if (!points || count < 2)
        return InvalidParameter;
    GpPointF *f = to_float_points(points, count);
    if (!f)
        return OutOfMemory;
    GpStatus st = GdipAddPathLine2(path, f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipAddPathArcI(GpPath *path, INT x, INT y, INT w, INT h, REAL start, REAL sweep)
{
    return GdipAddPathArc(path, (float)x, (float)y, (float)w, (float)h, start, sweep);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathBezierI(GpPath *path, INT x1, INT y1, INT x2, INT y2, INT x3, INT y3, INT x4,
                                              INT y4)
{
    return GdipAddPathBezier(path, (float)x1, (float)y1, (float)x2, (float)y2, (float)x3, (float)y3, (float)x4,
                             (float)y4);
}

#define POINTS_I(call_f, ...)                         \
    if (!points || count < 1)                         \
        return InvalidParameter;                      \
    GpPointF *f = to_float_points(points, count);     \
    if (!f)                                           \
        return OutOfMemory;                           \
    GpStatus st = call_f(__VA_ARGS__);                \
    gp_free(f);                                       \
    return st

WINGDIPAPI GpStatus WINAPI GdipAddPathBeziersI(GpPath *path, const GpPoint *points, INT count)
{
    POINTS_I(GdipAddPathBeziers, path, f, count);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathCurveI(GpPath *path, const GpPoint *points, INT count)
{
    POINTS_I(GdipAddPathCurve, path, f, count);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathCurve2I(GpPath *path, const GpPoint *points, INT count, REAL tension)
{
    POINTS_I(GdipAddPathCurve2, path, f, count, tension);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathCurve3I(GpPath *path, const GpPoint *points, INT count, INT offset, INT segments,
                                              REAL tension)
{
    POINTS_I(GdipAddPathCurve3, path, f, count, offset, segments, tension);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurveI(GpPath *path, const GpPoint *points, INT count)
{
    POINTS_I(GdipAddPathClosedCurve, path, f, count);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurve2I(GpPath *path, const GpPoint *points, INT count, REAL tension)
{
    POINTS_I(GdipAddPathClosedCurve2, path, f, count, tension);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathRectangleI(GpPath *path, INT x, INT y, INT w, INT h)
{
    return GdipAddPathRectangle(path, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathRectanglesI(GpPath *path, const GpRect *rects, INT count)
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
    GpStatus st = GdipAddPathRectangles(path, f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipAddPathEllipseI(GpPath *path, INT x, INT y, INT w, INT h)
{
    return GdipAddPathEllipse(path, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathPieI(GpPath *path, INT x, INT y, INT w, INT h, REAL start, REAL sweep)
{
    return GdipAddPathPie(path, (float)x, (float)y, (float)w, (float)h, start, sweep);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathPolygonI(GpPath *path, const GpPoint *points, INT count)
{
    POINTS_I(GdipAddPathPolygon, path, f, count);
}

/* ------------------------------------------------------------------ transforms and operations */

WINGDIPAPI GpStatus WINAPI GdipTransformPath(GpPath *path, GpMatrix *matrix)
{
    PATH_OR_FAIL(p, path);
    if (!matrix)
        return Ok;
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    mime_vec_path_transform(&p->path, &m->m);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipFlattenPath(GpPath *path, GpMatrix *matrix, REAL flatness)
{
    PATH_OR_FAIL(p, path);
    GMatrix *m = matrix ? gp_matrix_cast(matrix) : nullptr;
    if (matrix && !m)
        return InvalidParameter;
    BOOL curves = FALSE;
    for (UINT i = 0; i < p->path.count; i++) {
        if ((p->path.types[i] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_BEZIER)
            curves = TRUE;
    }
    if (!curves && !m)
        return Ok;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    if (flatness <= 0.0f)
        flatness = 0.25f;
    MimeVecMatrix id;
    mime_vec_matrix_identity(&id);
    GpStatus st = gp_path_status(mime_vec_path_flatten(&p->path, &id, flatness, &polys));
    if (st == Ok) {
        MimeVecPath np;
        mime_vec_path_init(&np, &gp_allocator);
        for (UINT s = 0; s < polys.nsubs && st == Ok; s++) {
            const MimeVecSub *sub = &polys.subs[s];
            for (UINT k = 0; k < sub->count; k++) {
                int e = k == 0 ? mime_vec_path_move_to(&np, polys.pts[sub->first].x, polys.pts[sub->first].y)
                               : mime_vec_path_line_to(&np, polys.pts[sub->first + k].x, polys.pts[sub->first + k].y);
                if (e) {
                    st = gp_path_status(e);
                    break;
                }
            }
            if (st == Ok && sub->closed)
                mime_vec_path_close_figure(&np);
        }
        if (st == Ok) {
            if (m)
                mime_vec_path_transform(&np, &m->m);
            mime_vec_path_free(&p->path);
            p->path = np;
        } else {
            mime_vec_path_free(&np);
        }
    }
    mime_vec_polys_free(&polys);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipWindingModeOutline(GpPath *path, GpMatrix *matrix, REAL flatness)
{
    GpStatus st = GdipFlattenPath(path, matrix, flatness);
    if (st != Ok)
        return st;
    GPath *p = gp_path_cast(path);
    p->fill_mode = FillModeWinding;
    return Ok;
}

/* the output of the stroker as a path of closed figures. */
static GpStatus polys_to_path(const MimeVecPolys *polys, MimeVecPath *out)
{
    for (UINT s = 0; s < polys->nsubs; s++) {
        const MimeVecSub *sub = &polys->subs[s];
        for (UINT k = 0; k < sub->count; k++) {
            int e = k == 0 ? mime_vec_path_move_to(out, polys->pts[sub->first].x, polys->pts[sub->first].y)
                           : mime_vec_path_line_to(out, polys->pts[sub->first + k].x, polys->pts[sub->first + k].y);
            if (e)
                return gp_path_status(e);
        }
        mime_vec_path_close_figure(out);
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipWidenPath(GpPath *path, GpPen *pen, GpMatrix *matrix, REAL flatness)
{
    PATH_OR_FAIL(p, path);
    GPen *pn = gp_pen_cast(pen);
    if (!pn)
        return InvalidParameter;
    GMatrix *m = matrix ? gp_matrix_cast(matrix) : nullptr;
    if (matrix && !m)
        return InvalidParameter;
    if (!p->path.count)
        return OutOfMemory;
    if (flatness <= 0.0f)
        flatness = 0.25f;
    MimeVecPolys out;
    mime_vec_polys_init(&out, &gp_allocator);
    GpStatus st = gp_pen_widen(pn, &p->path, m ? &m->m : nullptr, flatness, &out);
    if (st == Ok) {
        MimeVecPath np;
        mime_vec_path_init(&np, &gp_allocator);
        st = polys_to_path(&out, &np);
        if (st == Ok) {
            mime_vec_path_free(&p->path);
            p->path = np;
            p->fill_mode = FillModeWinding;
        } else {
            mime_vec_path_free(&np);
        }
    }
    mime_vec_polys_free(&out);
    return st;
}

/* solves the projective map of the unit square onto four points, for the perspective warp. */
static BOOL square_to_quad(const GpPointF *q, float *a)
{
    float x0 = q[0].X, y0 = q[0].Y, x1 = q[1].X, y1 = q[1].Y, x2 = q[2].X, y2 = q[2].Y, x3 = q[3].X, y3 = q[3].Y;
    float dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
    float dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
    float den = dx1 * dy2 - dx2 * dy1;
    float g, h;
    if (dx3 == 0.0f && dy3 == 0.0f) {
        g = h = 0.0f;
    } else {
        if (den == 0.0f)
            return FALSE;
        g = (dx3 * dy2 - dx2 * dy3) / den;
        h = (dx1 * dy3 - dx3 * dy1) / den;
    }
    a[0] = x1 - x0 + g * x1;
    a[1] = x3 - x0 + h * x3;
    a[2] = x0;
    a[3] = y1 - y0 + g * y1;
    a[4] = y3 - y0 + h * y3;
    a[5] = y0;
    a[6] = g;
    a[7] = h;
    return TRUE;
}

WINGDIPAPI GpStatus WINAPI GdipWarpPath(GpPath *path, GpMatrix *matrix, const GpPointF *points, INT count, REAL sx,
                                        REAL sy, REAL sw, REAL sh, WarpMode mode, REAL flatness)
{
    PATH_OR_FAIL(p, path);
    if (!points || count < 3 || sw == 0.0f || sh == 0.0f)
        return InvalidParameter;
    if (mode != WarpModePerspective && mode != WarpModeBilinear)
        return InvalidParameter;
    if (count < 4 && mode == WarpModePerspective)
        return InvalidParameter;
    GMatrix *m = matrix ? gp_matrix_cast(matrix) : nullptr;
    if (matrix && !m)
        return InvalidParameter;
    if (count > 4)
        count = 4;
    if (!p->path.count)
        return Ok;
    GpPointF quad[4];
    for (INT i = 0; i < count; i++)
        quad[i] = points[i];
    if (count == 3) {
        quad[3].X = quad[1].X + quad[2].X - quad[0].X;
        quad[3].Y = quad[1].Y + quad[2].Y - quad[0].Y;
    }
    if (m) {
        for (INT i = 0; i < 4; i++) {
            MimeVecPoint t = mime_vec_xform(&m->m, quad[i].X, quad[i].Y);
            quad[i].X = t.x;
            quad[i].Y = t.y;
        }
    }
    float a[8];
    if (mode == WarpModePerspective && !square_to_quad(quad, a))
        return InvalidParameter;
    /* flatten first so curves follow the warp */
    GpStatus st = GdipFlattenPath(path, nullptr, flatness);
    if (st != Ok)
        return st;
    for (UINT i = 0; i < p->path.count; i++) {
        float u = (p->path.pts[i].x - sx) / sw, v = (p->path.pts[i].y - sy) / sh;
        float x, y;
        if (mode == WarpModePerspective) {
            float w = a[6] * u + a[7] * v + 1.0f;
            x = (a[0] * u + a[1] * v + a[2]) / w;
            y = (a[3] * u + a[4] * v + a[5]) / w;
        } else {
            x = (1 - u) * (1 - v) * quad[0].X + u * (1 - v) * quad[1].X + (1 - u) * v * quad[2].X + u * v * quad[3].X;
            y = (1 - u) * (1 - v) * quad[0].Y + u * (1 - v) * quad[1].Y + (1 - u) * v * quad[2].Y + u * v * quad[3].Y;
        }
        p->path.pts[i].x = x;
        p->path.pts[i].y = y;
    }
    return Ok;
}

/* ------------------------------------------------------------------ bounds and hit tests */

static GpStatus path_bounds(GPath *p, const MimeVecMatrix *m, GPen *pen, GpRectF *bounds)
{
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st;
    MimeVecBounds b;
    BOOL have;
    if (pen) {
        st = gp_pen_widen(pen, &p->path, m, 0.25f, &polys);
        have = st == Ok && mime_vec_polys_bounds(&polys, &b);
    } else {
        st = gp_path_status(mime_vec_path_flatten(&p->path, m, 0.25f, &polys));
        have = st == Ok && mime_vec_polys_bounds(&polys, &b);
    }
    if (st == Ok) {
        if (have) {
            bounds->X = b.left;
            bounds->Y = b.top;
            bounds->Width = b.right - b.left;
            bounds->Height = b.bottom - b.top;
        } else {
            memset(bounds, 0, sizeof(*bounds));
        }
    }
    mime_vec_polys_free(&polys);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathWorldBounds(GpPath *path, GpRectF *bounds, const GpMatrix *matrix,
                                                  const GpPen *pen)
{
    PATH_OR_FAIL(p, path);
    if (!bounds)
        return InvalidParameter;
    GMatrix *m = matrix ? gp_matrix_cast(matrix) : nullptr;
    if (matrix && !m)
        return InvalidParameter;
    GPen *pn = pen ? gp_pen_cast(pen) : nullptr;
    if (pen && !pn)
        return InvalidParameter;
    return path_bounds(p, m ? &m->m : nullptr, pn, bounds);
}

WINGDIPAPI GpStatus WINAPI GdipGetPathWorldBoundsI(GpPath *path, GpRect *bounds, const GpMatrix *matrix,
                                                   const GpPen *pen)
{
    if (!bounds)
        return InvalidParameter;
    GpRectF b;
    GpStatus st = GdipGetPathWorldBounds(path, &b, matrix, pen);
    if (st != Ok)
        return st;
    /* the integer rectangle that holds the float one */
    INT l = (INT)mime_vec_floorf(b.X), t = (INT)mime_vec_floorf(b.Y);
    INT r = (INT)mime_vec_ceilf(b.X + b.Width), bt = (INT)mime_vec_ceilf(b.Y + b.Height);
    bounds->X = l;
    bounds->Y = t;
    bounds->Width = r - l;
    bounds->Height = bt - t;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisiblePathPoint(GpPath *path, REAL x, REAL y, GpGraphics *graphics, BOOL *result)
{
    PATH_OR_FAIL(p, path);
    if (!result)
        return InvalidParameter;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st = gp_path_status(mime_vec_path_flatten(&p->path, nullptr, 0.25f, &polys));
    if (st == Ok) {
        *result = mime_vec_point_in_polys(&polys, x, y,
                                          p->fill_mode == FillModeWinding ? MIME_VEC_FILL_WINDING
                                                                           : MIME_VEC_FILL_ALTERNATE);
        /* a graphics with a clip hides the points the clip hides */
        GGraphics *g = gp_graphics_cast(graphics);
        if (g && *result && g->clip_set) {
            MimeVecPoint pt = mime_vec_xform(gp_graphics_w2d(g), x, y);
            *result = mime_vec_mask_coverage_at(&g->clip, (int)mime_vec_floorf(pt.x), (int)mime_vec_floorf(pt.y)) != 0;
        }
    }
    mime_vec_polys_free(&polys);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisiblePathPointI(GpPath *path, INT x, INT y, GpGraphics *graphics, BOOL *result)
{
    return GdipIsVisiblePathPoint(path, (float)x, (float)y, graphics, result);
}

WINGDIPAPI GpStatus WINAPI GdipIsOutlineVisiblePathPoint(GpPath *path, REAL x, REAL y, GpPen *pen,
                                                         GpGraphics *graphics, BOOL *result)
{
    PATH_OR_FAIL(p, path);
    GPen *pn = gp_pen_cast(pen);
    if (!pn || !result)
        return InvalidParameter;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st = gp_pen_widen(pn, &p->path, nullptr, 0.25f, &polys);
    if (st == Ok)
        *result = mime_vec_point_in_polys(&polys, x, y, MIME_VEC_FILL_WINDING);
    mime_vec_polys_free(&polys);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipIsOutlineVisiblePathPointI(GpPath *path, INT x, INT y, GpPen *pen, GpGraphics *graphics,
                                                          BOOL *result)
{
    return GdipIsOutlineVisiblePathPoint(path, (float)x, (float)y, pen, graphics, result);
}

/* ------------------------------------------------------------------ the iterator */

typedef struct GPathIter {
    DWORD tag;
    GPath *path; /* a copy */
    INT sub_pos;     /* next figure start for subpath iteration */
    INT marker_pos;
    INT type_pos;
} GPathIter;

static GPathIter *iter_cast(const void *p)
{
    return GP_CAST(GPathIter, GP_TAG_PATHITER, p);
}

WINGDIPAPI GpStatus WINAPI GdipCreatePathIter(GpPathIterator **iterator, GpPath *path)
{
    GPath *p = gp_path_cast(path);
    if (!iterator || !p)
        return InvalidParameter;
    GPathIter *it = gp_alloc_zero(sizeof(*it));
    if (!it)
        return OutOfMemory;
    it->tag = GP_TAG_PATHITER;
    it->path = gp_path_clone(p);
    if (!it->path) {
        gp_free(it);
        return OutOfMemory;
    }
    *iterator = it;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeletePathIter(GpPathIterator *iterator)
{
    GPathIter *it = iter_cast(iterator);
    if (!it)
        return InvalidParameter;
    gp_path_free(it->path);
    it->tag = 0;
    gp_free(it);
    return Ok;
}

static BYTE type_at(const GPathIter *it, INT i) { return it->path->path.types[i]; }

/* the end (exclusive) of the figure that starts at start */
static INT figure_end(const GPathIter *it, INT start)
{
    INT n = (INT)it->path->path.count, i = start + 1;
    while (i < n && (type_at(it, i) & MIME_VEC_PT_TYPE_MASK) != MIME_VEC_PT_START)
        i++;
    return i;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterNextSubpath(GpPathIterator *iterator, INT *result, INT *start, INT *end,
                                                   BOOL *closed)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !result || !start || !end || !closed)
        return InvalidParameter;
    INT n = (INT)it->path->path.count;
    if (it->sub_pos >= n) {
        *result = 0;
        *start = *end = 0;
        *closed = TRUE;
        return Ok;
    }
    INT s = it->sub_pos, e = figure_end(it, s);
    *start = s;
    *end = e - 1;
    *closed = (type_at(it, e - 1) & MIME_VEC_PT_CLOSE) != 0;
    *result = e - s;
    it->sub_pos = e;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterNextSubpathPath(GpPathIterator *iterator, INT *result, GpPath *path,
                                                       BOOL *closed)
{
    GPathIter *it = iter_cast(iterator);
    GPath *dst = gp_path_cast(path);
    if (!it || !result || !closed)
        return InvalidParameter;
    INT s, e;
    BOOL c;
    INT count;
    GpStatus st = GdipPathIterNextSubpath(iterator, &count, &s, &e, &c);
    if (st != Ok)
        return st;
    *result = count;
    *closed = c;
    if (dst && count) {
        mime_vec_path_clear(&dst->path);
        return gp_path_status(mime_vec_path_add_raw(&dst->path, it->path->path.pts + s, it->path->path.types + s,
                                                    (UINT)count));
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterNextPathType(GpPathIterator *iterator, INT *result, BYTE *type, INT *start,
                                                    INT *end)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !result || !type || !start || !end)
        return InvalidParameter;
    INT n = (INT)it->path->path.count;
    INT pos = it->type_pos;
    if (pos >= n - 1) {
        *result = 0;
        return Ok;
    }
    /* a run of points of one type, the first of the run is the end of the previous one */
    INT s = pos;
    BYTE t = type_at(it, s + 1) & MIME_VEC_PT_TYPE_MASK;
    INT e = s + 1;
    while (e + 1 < n && (type_at(it, e + 1) & MIME_VEC_PT_TYPE_MASK) == t && !(type_at(it, e) & MIME_VEC_PT_CLOSE))
        e++;
    *type = t;
    *start = s;
    *end = e;
    *result = e - s + 1;
    it->type_pos = e;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterNextMarker(GpPathIterator *iterator, INT *result, INT *start, INT *end)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !result || !start || !end)
        return InvalidParameter;
    INT n = (INT)it->path->path.count;
    if (it->marker_pos >= n) {
        *result = 0;
        return Ok;
    }
    INT s = it->marker_pos, e = s;
    while (e < n - 1 && !(type_at(it, e) & MIME_VEC_PT_MARKER))
        e++;
    *start = s;
    *end = e;
    *result = e - s + 1;
    it->marker_pos = e + 1;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterNextMarkerPath(GpPathIterator *iterator, INT *result, GpPath *path)
{
    GPathIter *it = iter_cast(iterator);
    GPath *dst = gp_path_cast(path);
    if (!it || !result)
        return InvalidParameter;
    INT s, e, count;
    GpStatus st = GdipPathIterNextMarker(iterator, &count, &s, &e);
    if (st != Ok)
        return st;
    *result = count;
    if (dst && count) {
        mime_vec_path_clear(&dst->path);
        return gp_path_status(mime_vec_path_add_raw(&dst->path, it->path->path.pts + s, it->path->path.types + s,
                                                    (UINT)count));
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterGetCount(GpPathIterator *iterator, INT *count)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !count)
        return InvalidParameter;
    *count = (INT)it->path->path.count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterGetSubpathCount(GpPathIterator *iterator, INT *count)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !count)
        return InvalidParameter;
    INT c = 0;
    for (UINT i = 0; i < it->path->path.count; i++) {
        if ((it->path->path.types[i] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_START)
            c++;
    }
    *count = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterIsValid(GpPathIterator *iterator, BOOL *valid)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !valid)
        return InvalidParameter;
    *valid = TRUE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterHasCurve(GpPathIterator *iterator, BOOL *has)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !has)
        return InvalidParameter;
    *has = FALSE;
    for (UINT i = 0; i < it->path->path.count; i++) {
        if ((it->path->path.types[i] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_BEZIER)
            *has = TRUE;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterRewind(GpPathIterator *iterator)
{
    GPathIter *it = iter_cast(iterator);
    if (!it)
        return InvalidParameter;
    it->sub_pos = it->marker_pos = it->type_pos = 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterEnumerate(GpPathIterator *iterator, INT *result, GpPointF *points, BYTE *types,
                                                 INT count)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !result || !points || !types || count < 0)
        return InvalidParameter;
    INT n = (INT)it->path->path.count;
    if (count > n)
        count = n;
    memcpy(points, it->path->path.pts, sizeof(GpPointF) * count);
    memcpy(types, it->path->path.types, count);
    *result = count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPathIterCopyData(GpPathIterator *iterator, INT *result, GpPointF *points, BYTE *types,
                                                INT start, INT end)
{
    GPathIter *it = iter_cast(iterator);
    if (!it || !result || !points || !types || start < 0 || end < start || end >= (INT)it->path->path.count)
        return InvalidParameter;
    INT c = end - start + 1;
    memcpy(points, it->path->path.pts + start, sizeof(GpPointF) * c);
    memcpy(types, it->path->path.types + start, c);
    *result = c;
    return Ok;
}
