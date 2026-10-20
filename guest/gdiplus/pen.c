/*
 * pens and line caps: the Pen of the flat api, its widening of a path into the outline the fill loop draws, and
 * the custom and adjustable arrow caps.
 */
#include "gp_private.h"

GPen *gp_pen_cast(const void *p)
{
    return GP_CAST(GPen, GP_TAG_PEN, p);
}

GCap *gp_cap_cast(const void *p)
{
    return GP_CAST(GCap, GP_TAG_CAP, p);
}

static BOOL unit_valid(GpUnit unit) { return unit >= UnitWorld && unit <= UnitMillimeter && unit != UnitDisplay; }

static BOOL cap_valid(LineCap c)
{
    return c == LineCapFlat || c == LineCapSquare || c == LineCapRound || c == LineCapTriangle ||
           (c >= LineCapNoAnchor && c <= LineCapArrowAnchor) || c == LineCapCustom;
}

static GPen *pen_new(GBrush *brush, float width, GpUnit unit)
{
    GPen *p = gp_alloc_zero(sizeof(*p));
    if (!p)
        return nullptr;
    p->tag = GP_TAG_PEN;
    p->brush = brush;
    p->width = width;
    p->unit = unit;
    p->start_cap = p->end_cap = LineCapFlat;
    p->dash_cap = DashCapFlat;
    p->join = LineJoinMiter;
    p->miter_limit = 10.0f;
    p->align = PenAlignmentCenter;
    p->dash_style = DashStyleSolid;
    mime_vec_matrix_identity(&p->xform);
    return p;
}

static void pen_free(GPen *p)
{
    gp_brush_free(p->brush);
    gp_free(p->dash);
    gp_free(p->compound);
    gp_cap_free(p->custom_start);
    gp_cap_free(p->custom_end);
    p->tag = 0;
    gp_free(p);
}

WINGDIPAPI GpStatus WINAPI GdipCreatePen1(ARGB color, REAL width, GpUnit unit, GpPen **pen)
{
    if (!pen || !unit_valid(unit) || width < 0.0f)
        return InvalidParameter;
    GBrush *b = gp_brush_solid(color);
    if (!b)
        return OutOfMemory;
    GPen *p = pen_new(b, width, unit);
    if (!p) {
        gp_brush_free(b);
        return OutOfMemory;
    }
    *pen = p;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreatePen2(GpBrush *brush, REAL width, GpUnit unit, GpPen **pen)
{
    GBrush *src = gp_brush_cast(brush);
    if (!src || !pen || !unit_valid(unit) || width < 0.0f)
        return InvalidParameter;
    GBrush *b = gp_brush_clone(src);
    if (!b)
        return OutOfMemory;
    GPen *p = pen_new(b, width, unit);
    if (!p) {
        gp_brush_free(b);
        return OutOfMemory;
    }
    *pen = p;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipClonePen(GpPen *pen, GpPen **clone)
{
    GPen *s = gp_pen_cast(pen);
    if (!s || !clone)
        return InvalidParameter;
    GPen *p = gp_alloc(sizeof(*p));
    if (!p)
        return OutOfMemory;
    *p = *s;
    p->brush = gp_brush_clone(s->brush);
    p->dash = s->dash_count ? gp_dup(s->dash, sizeof(float) * s->dash_count) : nullptr;
    p->compound = s->compound_count ? gp_dup(s->compound, sizeof(float) * s->compound_count) : nullptr;
    p->custom_start = s->custom_start ? gp_cap_clone(s->custom_start) : nullptr;
    p->custom_end = s->custom_end ? gp_cap_clone(s->custom_end) : nullptr;
    if (!p->brush || (s->dash_count && !p->dash) || (s->compound_count && !p->compound) ||
        (s->custom_start && !p->custom_start) || (s->custom_end && !p->custom_end)) {
        pen_free(p);
        return OutOfMemory;
    }
    *clone = p;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeletePen(GpPen *pen)
{
    GPen *p = gp_pen_cast(pen);
    if (!p)
        return InvalidParameter;
    pen_free(p);
    return Ok;
}

#define PEN(p, pen)                 \
    GPen *p = gp_pen_cast(pen);     \
    if (!p)                         \
        return InvalidParameter

WINGDIPAPI GpStatus WINAPI GdipSetPenWidth(GpPen *pen, REAL width)
{
    PEN(p, pen);
    if (width < 0.0f)
        return InvalidParameter;
    p->width = width;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenWidth(GpPen *pen, REAL *width)
{
    PEN(p, pen);
    if (!width)
        return InvalidParameter;
    *width = p->width;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenUnit(GpPen *pen, GpUnit unit)
{
    PEN(p, pen);
    if (!unit_valid(unit))
        return InvalidParameter;
    p->unit = unit;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenUnit(GpPen *pen, GpUnit *unit)
{
    PEN(p, pen);
    if (!unit)
        return InvalidParameter;
    *unit = p->unit;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenLineCap197819(GpPen *pen, GpLineCap start, GpLineCap end, GpDashCap dash)
{
    PEN(p, pen);
    if (!cap_valid(start) || !cap_valid(end) || (dash != DashCapFlat && dash != DashCapRound && dash != DashCapTriangle))
        return InvalidParameter;
    p->start_cap = start;
    p->end_cap = end;
    p->dash_cap = dash;
    gp_cap_free(p->custom_start);
    gp_cap_free(p->custom_end);
    p->custom_start = p->custom_end = nullptr;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenStartCap(GpPen *pen, GpLineCap cap)
{
    PEN(p, pen);
    if (!cap_valid(cap))
        return InvalidParameter;
    p->start_cap = cap;
    gp_cap_free(p->custom_start);
    p->custom_start = nullptr;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenEndCap(GpPen *pen, GpLineCap cap)
{
    PEN(p, pen);
    if (!cap_valid(cap))
        return InvalidParameter;
    p->end_cap = cap;
    gp_cap_free(p->custom_end);
    p->custom_end = nullptr;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenDashCap197819(GpPen *pen, GpDashCap cap)
{
    PEN(p, pen);
    if (cap != DashCapFlat && cap != DashCapRound && cap != DashCapTriangle)
        return InvalidParameter;
    p->dash_cap = cap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenStartCap(GpPen *pen, GpLineCap *cap)
{
    PEN(p, pen);
    if (!cap)
        return InvalidParameter;
    *cap = p->start_cap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenEndCap(GpPen *pen, GpLineCap *cap)
{
    PEN(p, pen);
    if (!cap)
        return InvalidParameter;
    *cap = p->end_cap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenDashCap197819(GpPen *pen, GpDashCap *cap)
{
    PEN(p, pen);
    if (!cap)
        return InvalidParameter;
    *cap = p->dash_cap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenLineJoin(GpPen *pen, GpLineJoin join)
{
    PEN(p, pen);
    if (join < LineJoinMiter || join > LineJoinMiterClipped)
        return InvalidParameter;
    p->join = join;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenLineJoin(GpPen *pen, GpLineJoin *join)
{
    PEN(p, pen);
    if (!join)
        return InvalidParameter;
    *join = p->join;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenCustomStartCap(GpPen *pen, GpCustomLineCap *cap)
{
    PEN(p, pen);
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    GCap *copy = gp_cap_clone(c);
    if (!copy)
        return OutOfMemory;
    gp_cap_free(p->custom_start);
    p->custom_start = copy;
    p->start_cap = LineCapCustom;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenCustomStartCap(GpPen *pen, GpCustomLineCap **cap)
{
    PEN(p, pen);
    if (!cap)
        return InvalidParameter;
    if (!p->custom_start)
        return GenericError;
    GCap *c = gp_cap_clone(p->custom_start);
    if (!c)
        return OutOfMemory;
    *cap = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenCustomEndCap(GpPen *pen, GpCustomLineCap *cap)
{
    PEN(p, pen);
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    GCap *copy = gp_cap_clone(c);
    if (!copy)
        return OutOfMemory;
    gp_cap_free(p->custom_end);
    p->custom_end = copy;
    p->end_cap = LineCapCustom;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenCustomEndCap(GpPen *pen, GpCustomLineCap **cap)
{
    PEN(p, pen);
    if (!cap)
        return InvalidParameter;
    if (!p->custom_end)
        return GenericError;
    GCap *c = gp_cap_clone(p->custom_end);
    if (!c)
        return OutOfMemory;
    *cap = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenMiterLimit(GpPen *pen, REAL limit)
{
    PEN(p, pen);
    if (limit < 1.0f)
        limit = 1.0f;
    p->miter_limit = limit;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenMiterLimit(GpPen *pen, REAL *limit)
{
    PEN(p, pen);
    if (!limit)
        return InvalidParameter;
    *limit = p->miter_limit;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenMode(GpPen *pen, GpPenAlignment mode)
{
    PEN(p, pen);
    if (mode != PenAlignmentCenter && mode != PenAlignmentInset)
        return InvalidParameter;
    p->align = mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenMode(GpPen *pen, GpPenAlignment *mode)
{
    PEN(p, pen);
    if (!mode)
        return InvalidParameter;
    *mode = p->align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenTransform(GpPen *pen, GpMatrix *matrix)
{
    PEN(p, pen);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m || !gp_matrix_invertible(&m->m))
        return InvalidParameter;
    p->xform = m->m;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenTransform(GpPen *pen, GpMatrix *matrix)
{
    PEN(p, pen);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    m->m = p->xform;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipResetPenTransform(GpPen *pen)
{
    PEN(p, pen);
    mime_vec_matrix_identity(&p->xform);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipMultiplyPenTransform(GpPen *pen, const GpMatrix *matrix, GpMatrixOrder order)
{
    PEN(p, pen);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m || (order != MatrixOrderPrepend && order != MatrixOrderAppend))
        return InvalidParameter;
    gp_matrix_apply(&p->xform, &m->m, order);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipTranslatePenTransform(GpPen *pen, REAL dx, REAL dy, GpMatrixOrder order)
{
    PEN(p, pen);
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op = {1, 0, 0, 1, dx, dy};
    gp_matrix_apply(&p->xform, &op, order);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipScalePenTransform(GpPen *pen, REAL sx, REAL sy, GpMatrixOrder order)
{
    PEN(p, pen);
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op = {sx, 0, 0, sy, 0, 0};
    gp_matrix_apply(&p->xform, &op, order);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipRotatePenTransform(GpPen *pen, REAL angle, GpMatrixOrder order)
{
    PEN(p, pen);
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    GMatrix tmp;
    tmp.tag = GP_TAG_MATRIX;
    mime_vec_matrix_identity(&tmp.m);
    GdipRotateMatrix(&tmp, angle, MatrixOrderPrepend);
    gp_matrix_apply(&p->xform, &tmp.m, order);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenColor(GpPen *pen, ARGB color)
{
    PEN(p, pen);
    GBrush *b = gp_brush_solid(color);
    if (!b)
        return OutOfMemory;
    gp_brush_free(p->brush);
    p->brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenColor(GpPen *pen, ARGB *color)
{
    PEN(p, pen);
    if (!color)
        return InvalidParameter;
    if (p->brush->type != BrushTypeSolidColor)
        return GenericError;
    *color = p->brush->color;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenBrushFill(GpPen *pen, GpBrush *brush)
{
    PEN(p, pen);
    GBrush *src = gp_brush_cast(brush);
    if (!src)
        return InvalidParameter;
    GBrush *b = gp_brush_clone(src);
    if (!b)
        return OutOfMemory;
    gp_brush_free(p->brush);
    p->brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenBrushFill(GpPen *pen, GpBrush **brush)
{
    PEN(p, pen);
    if (!brush)
        return InvalidParameter;
    GBrush *b = gp_brush_clone(p->brush);
    if (!b)
        return OutOfMemory;
    *brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenFillType(GpPen *pen, GpPenType *type)
{
    PEN(p, pen);
    if (!type)
        return InvalidParameter;
    *type = (GpPenType)p->brush->type;
    return Ok;
}

static const float dash_presets[5][6] = {{0}, {3, 1}, {1, 1}, {3, 1, 1, 1}, {3, 1, 1, 1, 1, 1}};
static const INT dash_preset_count[5] = {0, 2, 2, 4, 6};

WINGDIPAPI GpStatus WINAPI GdipGetPenDashStyle(GpPen *pen, GpDashStyle *style)
{
    PEN(p, pen);
    if (!style)
        return InvalidParameter;
    *style = p->dash_style;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenDashStyle(GpPen *pen, GpDashStyle style)
{
    PEN(p, pen);
    if (style < DashStyleSolid || style > DashStyleCustom)
        return InvalidParameter;
    if (style == p->dash_style && style != DashStyleCustom)
        return Ok;
    if (style != DashStyleCustom) {
        gp_free(p->dash);
        p->dash = nullptr;
        p->dash_count = 0;
    } else if (!p->dash_count) {
        return Ok;
    }
    p->dash_style = style;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenDashOffset(GpPen *pen, REAL *offset)
{
    PEN(p, pen);
    if (!offset)
        return InvalidParameter;
    *offset = p->dash_offset;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenDashOffset(GpPen *pen, REAL offset)
{
    PEN(p, pen);
    p->dash_offset = offset;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenDashCount(GpPen *pen, INT *count)
{
    PEN(p, pen);
    if (!count)
        return InvalidParameter;
    *count = p->dash_style == DashStyleCustom ? p->dash_count : dash_preset_count[p->dash_style];
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenDashArray(GpPen *pen, const REAL *dash, INT count)
{
    PEN(p, pen);
    if (!dash || count <= 0)
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        if (dash[i] <= 0.0f)
            return InvalidParameter;
    }
    float *copy = gp_dup(dash, sizeof(float) * count);
    if (!copy)
        return OutOfMemory;
    gp_free(p->dash);
    p->dash = copy;
    p->dash_count = count;
    p->dash_style = DashStyleCustom;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenDashArray(GpPen *pen, REAL *dash, INT count)
{
    PEN(p, pen);
    if (!dash || count < 0)
        return InvalidParameter;
    INT have = p->dash_style == DashStyleCustom ? p->dash_count : dash_preset_count[p->dash_style];
    if (count > have)
        count = have;
    const float *src = p->dash_style == DashStyleCustom ? p->dash : dash_presets[p->dash_style];
    for (INT i = 0; i < count; i++)
        dash[i] = src[i];
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenCompoundCount(GpPen *pen, INT *count)
{
    PEN(p, pen);
    if (!count)
        return InvalidParameter;
    *count = p->compound_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPenCompoundArray(GpPen *pen, const REAL *array, INT count)
{
    PEN(p, pen);
    if (!array || count < 2 || (count & 1))
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        if (array[i] < 0.0f || array[i] > 1.0f || (i && array[i] < array[i - 1]))
            return InvalidParameter;
    }
    float *copy = gp_dup(array, sizeof(float) * count);
    if (!copy)
        return OutOfMemory;
    gp_free(p->compound);
    p->compound = copy;
    p->compound_count = count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPenCompoundArray(GpPen *pen, REAL *array, INT count)
{
    PEN(p, pen);
    if (!array || count < 0)
        return InvalidParameter;
    if (count > p->compound_count)
        count = p->compound_count;
    for (INT i = 0; i < count; i++)
        array[i] = p->compound[i];
    return Ok;
}

/* ------------------------------------------------------------------ widening */

/* the factor from the width of a pen in its unit to pixels on a device of the given resolution. */
static float unit_pixels(GpUnit unit, float dpi)
{
    switch (unit) {
    case UnitPoint:
        return dpi / 72.0f;
    case UnitInch:
        return dpi;
    case UnitDocument:
        return dpi / 300.0f;
    case UnitMillimeter:
        return dpi / 25.4f;
    default:
        return 1.0f;
    }
}

float gp_unit_to_pixels(GpUnit unit, float dpi) { return unit_pixels(unit, dpi); }

static void stroke_setup(const GPen *pen, float width, MimeVecStroke *s, const MimeVecMatrix *out, float flatness)
{
    mime_vec_stroke_defaults(s, width);
    s->start_cap = pen->start_cap == LineCapCustom ? MIME_VEC_CAP_FLAT : pen->start_cap;
    s->end_cap = pen->end_cap == LineCapCustom ? MIME_VEC_CAP_FLAT : pen->end_cap;
    s->dash_cap = pen->dash_cap;
    s->join = pen->join;
    s->miter_limit = pen->miter_limit;
    s->align = pen->align;
    s->dash_style = pen->dash_style;
    s->dash = pen->dash;
    s->ndash = (uint32_t)pen->dash_count;
    s->dash_offset = pen->dash_offset;
    s->compound = pen->compound;
    s->ncompound = (uint32_t)pen->compound_count;
    s->flatness = flatness;
    s->out = out;
}

/*
 * the outline of a stroke. the path is in world space, the widened polygons come out in the space of the
 * matrix, or the world when there is none. device_dpi is the resolution for pens whose width is not in world
 * units, 0 when the pen is in world units.
 */
GpStatus gp_pen_widen_ex(const GPen *pen, const MimeVecPath *path, const MimeVecMatrix *world_to_out, float flatness,
                         float dpi, MimeVecPolys *out)
{
    MimeVecMatrix w2o;
    if (world_to_out)
        w2o = *world_to_out;
    else
        mime_vec_matrix_identity(&w2o);
    MimeVecMatrix nib_inv;
    if (!gp_matrix_inverse(&nib_inv, &pen->xform))
        return InvalidParameter;
    MimeVecMatrix in, outm;
    float width;
    float min_width;
    if (pen->unit == UnitWorld) {
        in = nib_inv;
        mime_vec_matrix_multiply(&outm, &pen->xform, &w2o);
        width = pen->width;
        float scale = mime_vec_matrix_max_scale(&outm);
        min_width = scale > 0.0f ? 1.0f / scale : 1.0f;
    } else {
        /* the width is in device pixels and does not follow the world transform */
        mime_vec_matrix_multiply(&in, &w2o, &nib_inv);
        outm = pen->xform;
        width = pen->width * unit_pixels(pen->unit, dpi > 0.0f ? dpi : 96.0f);
        float scale = mime_vec_matrix_max_scale(&outm);
        min_width = scale > 0.0f ? 1.0f / scale : 1.0f;
    }
    float scale = mime_vec_matrix_max_scale(&outm);
    float stroke_flat = scale > 0.0f ? flatness / scale : flatness;
    MimeVecPolys flat;
    mime_vec_polys_init(&flat, &gp_allocator);
    int e = mime_vec_path_flatten(path, &in, stroke_flat, &flat);
    if (e != MIME_VEC_OK) {
        mime_vec_polys_free(&flat);
        return gp_path_status(e);
    }
    MimeVecStroke s;
    stroke_setup(pen, width, &s, &outm, stroke_flat);
    s.min_width = min_width;
    e = mime_vec_stroke(out, &flat, &s);
    mime_vec_polys_free(&flat);
    return gp_path_status(e);
}

GpStatus gp_pen_widen(const GPen *pen, const MimeVecPath *path, const MimeVecMatrix *matrix, float flatness,
                      MimeVecPolys *out)
{
    return gp_pen_widen_ex(pen, path, matrix, flatness, 96.0f, out);
}

/* ------------------------------------------------------------------ custom line caps */

void gp_cap_free(GCap *c)
{
    if (!c)
        return;
    gp_path_free(c->fill);
    gp_path_free(c->stroke);
    c->tag = 0;
    gp_free(c);
}

GCap *gp_cap_clone(const GCap *s)
{
    GCap *c = gp_alloc(sizeof(*c));
    if (!c)
        return nullptr;
    *c = *s;
    c->fill = s->fill ? gp_path_clone(s->fill) : nullptr;
    c->stroke = s->stroke ? gp_path_clone(s->stroke) : nullptr;
    if ((s->fill && !c->fill) || (s->stroke && !c->stroke)) {
        gp_cap_free(c);
        return nullptr;
    }
    return c;
}

static GCap *cap_new(void)
{
    GCap *c = gp_alloc_zero(sizeof(*c));
    if (!c)
        return nullptr;
    c->tag = GP_TAG_CAP;
    c->base_cap = LineCapFlat;
    c->join = LineJoinMiter;
    c->width_scale = 1.0f;
    c->start_cap = c->end_cap = LineCapFlat;
    return c;
}

WINGDIPAPI GpStatus WINAPI GdipCreateCustomLineCap(GpPath *fill, GpPath *stroke, GpLineCap base, REAL inset,
                                                   GpCustomLineCap **cap)
{
    if (!cap || (!fill && !stroke))
        return InvalidParameter;
    GPath *f = fill ? gp_path_cast(fill) : nullptr, *s = stroke ? gp_path_cast(stroke) : nullptr;
    if ((fill && !f) || (stroke && !s) || (base != LineCapFlat && base != LineCapSquare && base != LineCapRound &&
                                           base != LineCapTriangle))
        return InvalidParameter;
    GCap *c = cap_new();
    if (!c)
        return OutOfMemory;
    if (f)
        c->fill = gp_path_clone(f);
    if (s)
        c->stroke = gp_path_clone(s);
    if ((f && !c->fill) || (s && !c->stroke)) {
        gp_cap_free(c);
        return OutOfMemory;
    }
    c->base_cap = base;
    c->base_inset = inset;
    *cap = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteCustomLineCap(GpCustomLineCap *cap)
{
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    gp_cap_free(c);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneCustomLineCap(GpCustomLineCap *cap, GpCustomLineCap **clone)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !clone)
        return InvalidParameter;
    GCap *n = gp_cap_clone(c);
    *clone = n;
    return n ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapType(GpCustomLineCap *cap, CustomLineCapType *type)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !type)
        return InvalidParameter;
    *type = c->arrow ? CustomLineCapTypeAdjustableArrow : CustomLineCapTypeDefault;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapStrokeCaps(GpCustomLineCap *cap, GpLineCap start, GpLineCap end)
{
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    c->start_cap = start;
    c->end_cap = end;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapStrokeCaps(GpCustomLineCap *cap, GpLineCap *start, GpLineCap *end)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !start || !end)
        return InvalidParameter;
    *start = c->start_cap;
    *end = c->end_cap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapStrokeJoin(GpCustomLineCap *cap, GpLineJoin join)
{
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    c->join = join;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapStrokeJoin(GpCustomLineCap *cap, GpLineJoin *join)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !join)
        return InvalidParameter;
    *join = c->join;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapBaseCap(GpCustomLineCap *cap, GpLineCap base)
{
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    c->base_cap = base;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapBaseCap(GpCustomLineCap *cap, GpLineCap *base)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !base)
        return InvalidParameter;
    *base = c->base_cap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapBaseInset(GpCustomLineCap *cap, REAL inset)
{
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    c->base_inset = inset;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapBaseInset(GpCustomLineCap *cap, REAL *inset)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !inset)
        return InvalidParameter;
    *inset = c->base_inset;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapWidthScale(GpCustomLineCap *cap, REAL scale)
{
    GCap *c = gp_cap_cast(cap);
    if (!c)
        return InvalidParameter;
    c->width_scale = scale;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapWidthScale(GpCustomLineCap *cap, REAL *scale)
{
    GCap *c = gp_cap_cast(cap);
    if (!c || !scale)
        return InvalidParameter;
    *scale = c->width_scale;
    return Ok;
}

/* the arrow is a custom cap whose paths follow its width, height and inset. */
static GpStatus arrow_update(GCap *c)
{
    GPath *p = gp_path_new(FillModeWinding);
    if (!p)
        return OutOfMemory;
    float w = c->arrow_width, h = c->arrow_height, inset = c->arrow_inset;
    mime_vec_path_move_to(&p->path, w * 0.5f, -h);
    mime_vec_path_line_to(&p->path, 0.0f, 0.0f);
    mime_vec_path_line_to(&p->path, -w * 0.5f, -h);
    if (inset != 0.0f)
        mime_vec_path_line_to(&p->path, 0.0f, inset - h);
    if (c->arrow_fill)
        mime_vec_path_close_figure(&p->path);
    gp_path_free(c->fill);
    gp_path_free(c->stroke);
    c->fill = c->stroke = nullptr;
    if (c->arrow_fill)
        c->fill = p;
    else
        c->stroke = p;
    c->width_scale = w != 0.0f ? h / w : 0.0f;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateAdjustableArrowCap(REAL height, REAL width, BOOL fill, GpAdjustableArrowCap **cap)
{
    if (!cap)
        return InvalidParameter;
    GCap *c = cap_new();
    if (!c)
        return OutOfMemory;
    c->arrow = TRUE;
    c->arrow_height = height;
    c->arrow_width = width;
    c->arrow_fill = fill != FALSE;
    c->base_cap = LineCapTriangle;
    GpStatus st = arrow_update(c);
    if (st != Ok) {
        gp_cap_free(c);
        return st;
    }
    *cap = c;
    return Ok;
}

#define ARROW(c, cap)                                  \
    GCap *c = gp_cap_cast(cap);                        \
    if (!c || !c->arrow)                               \
        return InvalidParameter

WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapHeight(GpAdjustableArrowCap *cap, REAL height)
{
    ARROW(c, cap);
    c->arrow_height = height;
    return arrow_update(c);
}

WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapHeight(GpAdjustableArrowCap *cap, REAL *height)
{
    ARROW(c, cap);
    if (!height)
        return InvalidParameter;
    *height = c->arrow_height;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapWidth(GpAdjustableArrowCap *cap, REAL width)
{
    ARROW(c, cap);
    c->arrow_width = width;
    return arrow_update(c);
}

WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapWidth(GpAdjustableArrowCap *cap, REAL *width)
{
    ARROW(c, cap);
    if (!width)
        return InvalidParameter;
    *width = c->arrow_width;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapMiddleInset(GpAdjustableArrowCap *cap, REAL inset)
{
    ARROW(c, cap);
    c->arrow_inset = inset;
    return arrow_update(c);
}

WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapMiddleInset(GpAdjustableArrowCap *cap, REAL *inset)
{
    ARROW(c, cap);
    if (!inset)
        return InvalidParameter;
    *inset = c->arrow_inset;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapFillState(GpAdjustableArrowCap *cap, BOOL fill)
{
    ARROW(c, cap);
    c->arrow_fill = fill != FALSE;
    return arrow_update(c);
}

WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapFillState(GpAdjustableArrowCap *cap, BOOL *fill)
{
    ARROW(c, cap);
    if (!fill)
        return InvalidParameter;
    *fill = c->arrow_fill;
    return Ok;
}
