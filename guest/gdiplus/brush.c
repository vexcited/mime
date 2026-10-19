/*
 * brushes and pens: the solid color brush and the pen made from a color or a brush. A pen keeps its own copy of the
 * brush it was made from. The other brush kinds are not drawn by this library, their constructors are absent.
 */
#include "gdiplus_private.h"

static GpBrush *brush_cast(GpBrush *brush)
{
    return brush && brush->Magic == GP_BRUSH_MAGIC ? brush : nullptr;
}

static GpPen *pen_cast(GpPen *pen)
{
    return pen && pen->Magic == GP_PEN_MAGIC ? pen : nullptr;
}

static GpBrush *brush_new(ARGB color)
{
    GpBrush *b = gp_alloc(sizeof(*b));
    if (!b)
        return nullptr;
    b->Magic = GP_BRUSH_MAGIC;
    b->Type = BrushTypeSolidColor;
    b->Color = color;
    return b;
}

static BOOL unit_valid(GpUnit unit)
{
    return unit >= UnitWorld && unit <= UnitMillimeter;
}

/* ------------------------------------------------------------------ brushes */

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateSolidFill(ARGB color, GpBrush **brush)
{
    if (!brush)
        return InvalidParameter;
    *brush = brush_new(color);
    return *brush ? Ok : OutOfMemory;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetSolidFillColor(GpBrush *brush, ARGB color)
{
    GpBrush *b = brush_cast(brush);
    if (!b)
        return InvalidParameter;
    if (b->Type != BrushTypeSolidColor)
        return WrongState;
    b->Color = color;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetSolidFillColor(GpBrush *brush, ARGB *color)
{
    GpBrush *b = brush_cast(brush);
    if (!b || !color)
        return InvalidParameter;
    if (b->Type != BrushTypeSolidColor)
        return WrongState;
    *color = b->Color;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetBrushType(GpBrush *brush, BrushType *type)
{
    GpBrush *b = brush_cast(brush);
    if (!b || !type)
        return InvalidParameter;
    *type = b->Type;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCloneBrush(GpBrush *brush, GpBrush **clone)
{
    GpBrush *b = brush_cast(brush);
    if (!b || !clone)
        return InvalidParameter;
    *clone = brush_new(b->Color);
    return *clone ? Ok : OutOfMemory;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDeleteBrush(GpBrush *brush)
{
    GpBrush *b = brush_cast(brush);
    if (!b)
        return InvalidParameter;
    b->Magic = 0;
    gp_free(b);
    return Ok;
}

/* ------------------------------------------------------------------ pens */

static GpStatus pen_new(GpBrush *brush, float width, GpUnit unit, GpPen **pen)
{
    GpPen *p = gp_alloc_zero(sizeof(*p));
    if (!p) {
        gp_free(brush);
        return OutOfMemory;
    }
    p->Magic = GP_PEN_MAGIC;
    p->Brush = brush;
    p->Width = width;
    p->Unit = unit;
    p->Dash = DashStyleSolid;
    *pen = p;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreatePen1(ARGB color, float width, GpUnit unit, GpPen **pen)
{
    if (!pen || !unit_valid(unit))
        return InvalidParameter;
    GpBrush *b = brush_new(color);
    if (!b)
        return OutOfMemory;
    return pen_new(b, width, unit, pen);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreatePen2(GpBrush *brush, float width, GpUnit unit, GpPen **pen)
{
    GpBrush *source = brush_cast(brush);
    if (!source || !pen || !unit_valid(unit))
        return InvalidParameter;
    GpBrush *b = brush_new(source->Color);
    if (!b)
        return OutOfMemory;
    return pen_new(b, width, unit, pen);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipClonePen(GpPen *pen, GpPen **clone)
{
    GpPen *p = pen_cast(pen);
    if (!p || !clone)
        return InvalidParameter;
    GpBrush *b = brush_new(p->Brush->Color);
    if (!b)
        return OutOfMemory;
    GpStatus status = pen_new(b, p->Width, p->Unit, clone);
    if (status == Ok)
        (*clone)->Dash = p->Dash;
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDeletePen(GpPen *pen)
{
    GpPen *p = pen_cast(pen);
    if (!p)
        return InvalidParameter;
    p->Magic = 0;
    gp_free(p->Brush);
    gp_free(p);
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPenColor(GpPen *pen, ARGB color)
{
    GpPen *p = pen_cast(pen);
    if (!p)
        return InvalidParameter;
    p->Brush->Color = color;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPenColor(GpPen *pen, ARGB *color)
{
    GpPen *p = pen_cast(pen);
    if (!p || !color)
        return InvalidParameter;
    *color = p->Brush->Color;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPenWidth(GpPen *pen, float width)
{
    GpPen *p = pen_cast(pen);
    if (!p || width < 0)
        return InvalidParameter;
    p->Width = width;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPenWidth(GpPen *pen, float *width)
{
    GpPen *p = pen_cast(pen);
    if (!p || !width)
        return InvalidParameter;
    *width = p->Width;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPenBrushFill(GpPen *pen, GpBrush *brush)
{
    GpPen *p = pen_cast(pen);
    GpBrush *b = brush_cast(brush);
    if (!p || !b)
        return InvalidParameter;
    p->Brush->Color = b->Color;
    return Ok;
}

/* the brush of a pen is a copy of the pen's own, the caller frees it. */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetPenBrushFill(GpPen *pen, GpBrush **brush)
{
    GpPen *p = pen_cast(pen);
    if (!p || !brush)
        return InvalidParameter;
    *brush = brush_new(p->Brush->Color);
    return *brush ? Ok : OutOfMemory;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPenFillType(GpPen *pen, BrushType *type)
{
    GpPen *p = pen_cast(pen);
    if (!p || !type)
        return InvalidParameter;
    *type = p->Brush->Type;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPenDashStyle(GpPen *pen, DashStyle style)
{
    GpPen *p = pen_cast(pen);
    if (!p || style < DashStyleSolid || style > DashStyleCustom)
        return InvalidParameter;
    p->Dash = style;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPenDashStyle(GpPen *pen, DashStyle *style)
{
    GpPen *p = pen_cast(pen);
    if (!p || !style)
        return InvalidParameter;
    *style = p->Dash;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPenUnit(GpPen *pen, GpUnit *unit)
{
    GpPen *p = pen_cast(pen);
    if (!p || !unit)
        return InvalidParameter;
    *unit = p->Unit;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPenUnit(GpPen *pen, GpUnit unit)
{
    GpPen *p = pen_cast(pen);
    if (!p || !unit_valid(unit))
        return InvalidParameter;
    p->Unit = unit;
    return Ok;
}
