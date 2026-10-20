/*
 * brushes: the solid, hatch, texture, linear gradient and path gradient brushes of the flat api.
 */
#include "gp_private.h"

GBrush *gp_brush_cast(const void *p)
{
    return GP_CAST(GBrush, GP_TAG_BRUSH, p);
}

static void mat_identity(MimeVecMatrix *m) { mime_vec_matrix_identity(m); }

GBrush *gp_brush_new(BrushType type)
{
    GBrush *b = gp_alloc_zero(sizeof(*b));
    if (!b)
        return nullptr;
    b->tag = GP_TAG_BRUSH;
    b->type = type;
    mat_identity(&b->xform);
    b->wrap = WrapModeTile;
    b->center_color = 0xffffffffu;
    return b;
}

GBrush *gp_brush_solid(ARGB color)
{
    GBrush *b = gp_brush_new(BrushTypeSolidColor);
    if (b)
        b->color = color;
    return b;
}

void gp_brush_free(GBrush *b)
{
    if (!b)
        return;
    if (b->image)
        gp_bitmap_free(b->image);
    gp_attrs_free(b->attrs);
    gp_free(b->blend.positions);
    gp_free(b->blend.factors);
    gp_free(b->preset.positions);
    gp_free(b->preset.colors);
    gp_path_free(b->pg_path);
    gp_free(b->surround);
    b->tag = 0;
    gp_free(b);
}

GBrush *gp_brush_clone(const GBrush *s)
{
    GBrush *b = gp_alloc(sizeof(*b));
    if (!b)
        return nullptr;
    *b = *s;
    b->image = nullptr;
    b->attrs = nullptr;
    b->blend.positions = b->blend.factors = nullptr;
    b->preset.positions = nullptr;
    b->preset.colors = nullptr;
    b->pg_path = nullptr;
    b->surround = nullptr;
    BOOL ok = TRUE;
    if (s->image) {
        GBitmap *copy;
        ok = ok && gp_bitmap_copy(s->image, &copy) == Ok;
        b->image = ok ? copy : nullptr;
    }
    if (s->attrs)
        ok = ok && (b->attrs = gp_attrs_clone(s->attrs)) != nullptr;
    if (s->blend.count) {
        b->blend.positions = gp_dup(s->blend.positions, sizeof(float) * s->blend.count);
        b->blend.factors = gp_dup(s->blend.factors, sizeof(float) * s->blend.count);
        ok = ok && b->blend.positions && b->blend.factors;
    }
    if (s->preset.count) {
        b->preset.positions = gp_dup(s->preset.positions, sizeof(float) * s->preset.count);
        b->preset.colors = gp_dup(s->preset.colors, sizeof(ARGB) * s->preset.count);
        ok = ok && b->preset.positions && b->preset.colors;
    }
    if (s->pg_path)
        ok = ok && (b->pg_path = gp_path_clone(s->pg_path)) != nullptr;
    if (s->surround_count) {
        b->surround = gp_dup(s->surround, sizeof(ARGB) * s->surround_count);
        ok = ok && b->surround;
    }
    if (!ok) {
        gp_brush_free(b);
        return nullptr;
    }
    return b;
}

/* ------------------------------------------------------------------ the common calls */

WINGDIPAPI GpStatus WINAPI GdipCloneBrush(GpBrush *brush, GpBrush **clone)
{
    GBrush *b = gp_brush_cast(brush);
    if (!b || !clone)
        return InvalidParameter;
    *clone = gp_brush_clone(b);
    return *clone ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteBrush(GpBrush *brush)
{
    GBrush *b = gp_brush_cast(brush);
    if (!b)
        return InvalidParameter;
    gp_brush_free(b);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetBrushType(GpBrush *brush, GpBrushType *type)
{
    GBrush *b = gp_brush_cast(brush);
    if (!b || !type)
        return InvalidParameter;
    *type = b->type;
    return Ok;
}

/* the checked brush of one kind: NULL when it is no brush, and the status for the caller. */
static GBrush *brush_of(void *brush, BrushType type, GpStatus *status)
{
    GBrush *b = gp_brush_cast(brush);
    *status = b ? (b->type == type ? Ok : WrongState) : InvalidParameter;
    return *status == Ok ? b : nullptr;
}

static BOOL wrap_valid(WrapMode m) { return m >= WrapModeTile && m <= WrapModeClamp; }

/* the transform calls are the same for every kind that has one. */
static GpStatus brush_get_transform(GBrush *b, GpMatrix *matrix)
{
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    m->m = b->xform;
    return Ok;
}

static GpStatus brush_set_transform(GBrush *b, const GpMatrix *matrix)
{
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    if (!gp_matrix_invertible(&m->m))
        return InvalidParameter;
    b->xform = m->m;
    return Ok;
}

static GpStatus brush_multiply(GBrush *b, const GpMatrix *matrix, MatrixOrder order)
{
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m || (order != MatrixOrderPrepend && order != MatrixOrderAppend))
        return InvalidParameter;
    gp_matrix_apply(&b->xform, &m->m, order);
    return Ok;
}

static GpStatus brush_translate(GBrush *b, float dx, float dy, MatrixOrder order)
{
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op = {1, 0, 0, 1, dx, dy};
    gp_matrix_apply(&b->xform, &op, order);
    return Ok;
}

static GpStatus brush_scale(GBrush *b, float sx, float sy, MatrixOrder order)
{
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op = {sx, 0, 0, sy, 0, 0};
    gp_matrix_apply(&b->xform, &op, order);
    return Ok;
}

static GpStatus brush_rotate(GBrush *b, float angle, MatrixOrder order)
{
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op;
    mat_identity(&op);
    GMatrix tmp;
    tmp.tag = GP_TAG_MATRIX;
    mat_identity(&tmp.m);
    GdipRotateMatrix(&tmp, angle, MatrixOrderPrepend);
    gp_matrix_apply(&b->xform, &tmp.m, order);
    return Ok;
}

/* ------------------------------------------------------------------ solid fill */

WINGDIPAPI GpStatus WINAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush)
{
    GP_REQUIRE_INIT();
    if (!brush)
        return InvalidParameter;
    GBrush *b = gp_brush_solid(color);
    *brush = b;
    return b ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipSetSolidFillColor(GpSolidFill *brush, ARGB color)
{
    GpStatus st;
    GBrush *b = brush_of(brush, BrushTypeSolidColor, &st);
    if (!b)
        return st;
    b->color = color;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetSolidFillColor(GpSolidFill *brush, ARGB *color)
{
    GpStatus st;
    GBrush *b = brush_of(brush, BrushTypeSolidColor, &st);
    if (!b)
        return st;
    if (!color)
        return InvalidParameter;
    *color = b->color;
    return Ok;
}

/* ------------------------------------------------------------------ hatch */

WINGDIPAPI GpStatus WINAPI GdipCreateHatchBrush(GpHatchStyle style, ARGB fore, ARGB back, GpHatch **brush)
{
    GP_REQUIRE_INIT();
    if (!brush || style < HatchStyleMin || style > HatchStyleMax)
        return InvalidParameter;
    GBrush *b = gp_brush_new(BrushTypeHatchFill);
    if (!b)
        return OutOfMemory;
    b->hatch_style = style;
    b->hatch_fore = fore;
    b->hatch_back = back;
    *brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetHatchStyle(GpHatch *brush, GpHatchStyle *style)
{
    GpStatus st;
    GBrush *b = brush_of(brush, BrushTypeHatchFill, &st);
    if (!b)
        return st;
    if (!style)
        return InvalidParameter;
    *style = b->hatch_style;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetHatchForegroundColor(GpHatch *brush, ARGB *color)
{
    GpStatus st;
    GBrush *b = brush_of(brush, BrushTypeHatchFill, &st);
    if (!b)
        return st;
    if (!color)
        return InvalidParameter;
    *color = b->hatch_fore;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetHatchBackgroundColor(GpHatch *brush, ARGB *color)
{
    GpStatus st;
    GBrush *b = brush_of(brush, BrushTypeHatchFill, &st);
    if (!b)
        return st;
    if (!color)
        return InvalidParameter;
    *color = b->hatch_back;
    return Ok;
}

/* ------------------------------------------------------------------ texture */

static GpStatus texture_create(GImage *image, WrapMode wrap, struct GAttrs *attrs, float x, float y, float w, float h,
                               BOOL whole, GpTexture **out)
{
    GBitmap *src = gp_bitmap_cast(image);
    if (!out || !wrap_valid(wrap))
        return InvalidParameter;
    if (!src)
        return image ? (gp_image_cast(image) ? NotImplemented : InvalidParameter) : InvalidParameter;
    INT ix = (INT)x, iy = (INT)y, iw = (INT)w, ih = (INT)h;
    if (whole) {
        ix = iy = 0;
        iw = src->width;
        ih = src->height;
    }
    if (iw <= 0 || ih <= 0 || ix < 0 || iy < 0 || ix + iw > src->width || iy + ih > src->height)
        return OutOfMemory;
    GBitmap *copy;
    GpStatus st = gp_bitmap_create(iw, ih, PixelFormat32bppARGB, &copy);
    if (st != Ok)
        return st;
    for (INT row = 0; row < ih; row++)
        gp_bitmap_read_argb(src, ix, iy + row, iw, (UINT32 *)(copy->bits + (SSIZE_T)row * copy->stride));
    GBrush *b = gp_brush_new(BrushTypeTextureFill);
    if (!b) {
        gp_bitmap_free(copy);
        return OutOfMemory;
    }
    b->image = copy;
    b->wrap = wrap;
    if (attrs) {
        b->attrs = gp_attrs_clone(attrs);
        if (!b->attrs) {
            gp_brush_free(b);
            return OutOfMemory;
        }
    }
    *out = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateTexture(GpImage *image, GpWrapMode wrap, GpTexture **texture)
{
    GP_REQUIRE_INIT();
    return texture_create(image, wrap, nullptr, 0, 0, 0, 0, TRUE, texture);
}

WINGDIPAPI GpStatus WINAPI GdipCreateTexture2(GpImage *image, GpWrapMode wrap, REAL x, REAL y, REAL w, REAL h,
                                              GpTexture **texture)
{
    GP_REQUIRE_INIT();
    return texture_create(image, wrap, nullptr, x, y, w, h, FALSE, texture);
}

WINGDIPAPI GpStatus WINAPI GdipCreateTexture2I(GpImage *image, GpWrapMode wrap, INT x, INT y, INT w, INT h,
                                               GpTexture **texture)
{
    GP_REQUIRE_INIT();
    return texture_create(image, wrap, nullptr, (float)x, (float)y, (float)w, (float)h, FALSE, texture);
}

WINGDIPAPI GpStatus WINAPI GdipCreateTextureIA(GpImage *image, const GpImageAttributes *attributes, REAL x, REAL y,
                                               REAL w, REAL h, GpTexture **texture)
{
    GP_REQUIRE_INIT();
    struct GAttrs *a = nullptr;
    if (attributes) {
        a = gp_attrs_cast(attributes);
        if (!a)
            return InvalidParameter;
    }
    return texture_create(image, a ? gp_attrs_wrap(a, nullptr) : WrapModeClamp, a, x, y, w, h, FALSE, texture);
}

WINGDIPAPI GpStatus WINAPI GdipCreateTextureIAI(GpImage *image, const GpImageAttributes *attributes, INT x, INT y, INT w,
                                                INT h, GpTexture **texture)
{
    return GdipCreateTextureIA(image, attributes, (float)x, (float)y, (float)w, (float)h, texture);
}

WINGDIPAPI GpStatus WINAPI GdipGetTextureTransform(GpTexture *t, GpMatrix *matrix)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    return b ? brush_get_transform(b, matrix) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetTextureTransform(GpTexture *t, const GpMatrix *matrix)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    return b ? brush_set_transform(b, matrix) : st;
}

WINGDIPAPI GpStatus WINAPI GdipResetTextureTransform(GpTexture *t)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    if (!b)
        return st;
    mat_identity(&b->xform);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipMultiplyTextureTransform(GpTexture *t, const GpMatrix *matrix, GpMatrixOrder order)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    return b ? brush_multiply(b, matrix, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipTranslateTextureTransform(GpTexture *t, REAL dx, REAL dy, GpMatrixOrder order)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    return b ? brush_translate(b, dx, dy, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipScaleTextureTransform(GpTexture *t, REAL sx, REAL sy, GpMatrixOrder order)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    return b ? brush_scale(b, sx, sy, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipRotateTextureTransform(GpTexture *t, REAL angle, GpMatrixOrder order)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    return b ? brush_rotate(b, angle, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetTextureWrapMode(GpTexture *t, GpWrapMode wrap)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    if (!b)
        return st;
    if (!wrap_valid(wrap))
        return InvalidParameter;
    b->wrap = wrap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetTextureWrapMode(GpTexture *t, GpWrapMode *wrap)
{
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    if (!b)
        return st;
    if (!wrap)
        return InvalidParameter;
    *wrap = b->wrap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetTextureImage(GpTexture *t, GpImage **image)
{
    GP_REQUIRE_INIT();
    GpStatus st;
    GBrush *b = brush_of(t, BrushTypeTextureFill, &st);
    if (!b)
        return st;
    if (!image)
        return InvalidParameter;
    GBitmap *copy;
    st = gp_bitmap_copy(b->image, &copy);
    if (st == Ok)
        *image = copy;
    return st;
}

/* ------------------------------------------------------------------ blends, shared by the gradients */

static GpStatus blend_get_count(const GBrush *b, INT *count)
{
    if (!count)
        return InvalidParameter;
    *count = b->blend.count ? b->blend.count : 1;
    return Ok;
}

static GpStatus blend_get(const GBrush *b, float *factors, float *positions, INT count)
{
    if (!factors || !positions || count <= 0)
        return InvalidParameter;
    INT have = b->blend.count ? b->blend.count : 1;
    if (count < have)
        return InsufficientBuffer;
    if (!b->blend.count) {
        factors[0] = 1.0f;
        positions[0] = 0.0f;
        return Ok;
    }
    for (INT i = 0; i < have; i++) {
        factors[i] = b->blend.factors[i];
        positions[i] = b->blend.positions[i];
    }
    return Ok;
}

static void blend_clear(GBrush *b)
{
    gp_free(b->blend.factors);
    gp_free(b->blend.positions);
    b->blend.factors = b->blend.positions = nullptr;
    b->blend.count = 0;
    gp_free(b->preset.positions);
    gp_free(b->preset.colors);
    b->preset.positions = nullptr;
    b->preset.colors = nullptr;
    b->preset.count = 0;
}

static GpStatus blend_set(GBrush *b, const float *factors, const float *positions, INT count)
{
    if (!factors || !positions || count <= 0)
        return InvalidParameter;
    if (positions[0] != 0.0f || positions[count - 1] != 1.0f)
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        if (factors[i] < 0.0f || factors[i] > 1.0f)
            return InvalidParameter;
        if (i && positions[i] < positions[i - 1])
            return InvalidParameter;
    }
    float *f = gp_dup(factors, sizeof(float) * count), *p = gp_dup(positions, sizeof(float) * count);
    if (!f || !p) {
        gp_free(f);
        gp_free(p);
        return OutOfMemory;
    }
    blend_clear(b);
    b->blend.factors = f;
    b->blend.positions = p;
    b->blend.count = count;
    return Ok;
}

static GpStatus preset_get_count(const GBrush *b, INT *count)
{
    if (!count)
        return InvalidParameter;
    *count = b->preset.count;
    return Ok;
}

static GpStatus preset_get(const GBrush *b, ARGB *colors, float *positions, INT count)
{
    if (!colors || !positions || count < 2)
        return InvalidParameter;
    if (!b->preset.count)
        return GenericError;
    if (count < b->preset.count)
        return InsufficientBuffer;
    for (INT i = 0; i < b->preset.count; i++) {
        colors[i] = b->preset.colors[i];
        positions[i] = b->preset.positions[i];
    }
    return Ok;
}

static GpStatus preset_set(GBrush *b, const ARGB *colors, const float *positions, INT count)
{
    if (!colors || !positions || count < 2)
        return InvalidParameter;
    if (positions[0] != 0.0f || positions[count - 1] != 1.0f)
        return InvalidParameter;
    for (INT i = 1; i < count; i++) {
        if (positions[i] < positions[i - 1])
            return InvalidParameter;
    }
    ARGB *c = gp_dup(colors, sizeof(ARGB) * count);
    float *p = gp_dup(positions, sizeof(float) * count);
    if (!c || !p) {
        gp_free(c);
        gp_free(p);
        return OutOfMemory;
    }
    blend_clear(b);
    b->preset.colors = c;
    b->preset.positions = p;
    b->preset.count = count;
    return Ok;
}

static GpStatus blend_sigma(GBrush *b, float focus, float scale)
{
    if (focus < 0.0f || focus > 1.0f || scale < 0.0f || scale > 1.0f)
        return InvalidParameter;
    float *fac = gp_alloc(sizeof(float) * 512), *pos = gp_alloc(sizeof(float) * 512);
    if (!fac || !pos) {
        gp_free(fac);
        gp_free(pos);
        return OutOfMemory;
    }
    INT n;
    float k = scale / 65536.0f;
    const float *sigma_table = gp_sigma_table();
    if (focus <= 0.0f || focus >= 1.0f) {
        INT i;
        if (focus == 1.0f) {
            for (i = 0; i < 128; i++) {
                fac[i] = sigma_table[i];
                pos[i] = (float)i / 255.0f;
            }
            for (i = 128; i < 256; i++) {
                fac[i] = 65536.0f - sigma_table[127 - (i - 128)];
                pos[i] = (float)i / 255.0f;
            }
        } else {
            for (i = 0; i < 128; i++) {
                fac[i] = 65536.0f - sigma_table[i];
                pos[i] = (float)i / 255.0f;
            }
            for (i = 128; i < 256; i++) {
                fac[i] = sigma_table[127 - (i - 128)];
                pos[i] = (float)i / 255.0f;
            }
        }
        n = 256;
    } else {
        INT i;
        for (i = 0; i < 128; i++) {
            fac[i] = sigma_table[i];
            pos[i] = (float)i * focus / 255.0f;
        }
        for (i = 128; i < 256; i++) {
            fac[i] = 65536.0f - sigma_table[127 - (i - 128)];
            pos[i] = (float)i * focus / 255.0f;
        }
        for (i = 0; i < 127; i++) {
            fac[256 + i] = 65536.0f - sigma_table[1 + i];
            pos[256 + i] = (float)(i + 1) * (1.0f - focus) / 255.0f + focus;
        }
        for (i = 0; i < 128; i++) {
            fac[383 + i] = sigma_table[127 - i];
            pos[383 + i] = (float)(128 + i) * (1.0f - focus) / 255.0f + focus;
        }
        n = 511;
    }
    for (INT i = 0; i < n; i++)
        fac[i] *= k;
    GpStatus st = blend_set(b, fac, pos, n);
    gp_free(fac);
    gp_free(pos);
    return st;
}

static GpStatus blend_linear(GBrush *b, float focus, float scale)
{
    if (focus < 0.0f || focus > 1.0f || scale < 0.0f || scale > 1.0f)
        return InvalidParameter;
    float fac[3], pos[3];
    INT n;
    if (focus <= 0.0f || focus >= 1.0f) {
        n = 2;
        if (focus == 1.0f) {
            fac[0] = 0.0f;
            fac[1] = scale;
        } else {
            fac[0] = scale;
            fac[1] = 0.0f;
        }
        pos[0] = 0.0f;
        pos[1] = 1.0f;
    } else {
        n = 3;
        fac[0] = 0.0f;
        fac[1] = scale;
        fac[2] = 0.0f;
        pos[0] = 0.0f;
        pos[1] = focus;
        pos[2] = 1.0f;
    }
    return blend_set(b, fac, pos, n);
}

/* ------------------------------------------------------------------ linear gradient */

static GpStatus line_new(INT mode, ARGB c1, ARGB c2, WrapMode wrap, GBrush **out)
{
    if (!wrap_valid(wrap))
        return InvalidParameter;
    GBrush *b = gp_brush_new(BrushTypeLinearGradient);
    if (!b)
        return OutOfMemory;
    b->lg_mode = mode;
    b->color1 = c1;
    b->color2 = c2;
    b->wrap = wrap;
    *out = b;
    return Ok;
}

static BOOL close_float(float a, float b)
{
    float d = a - b;
    if (d < 0)
        d = -d;
    float m = a < 0 ? -a : a;
    if (m < 1.0f)
        m = 1.0f;
    return d / m < 0.0000011920929f;
}

WINGDIPAPI GpStatus WINAPI GdipCreateLineBrush(const GpPointF *p1, const GpPointF *p2, ARGB c1, ARGB c2, GpWrapMode wrap,
                                               GpLineGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!p1 || !p2 || !brush)
        return InvalidParameter;
    if (!wrap_valid(wrap))
        return InvalidParameter;
    if (close_float(p1->X, p2->X) && close_float(p1->Y, p2->Y))
        return OutOfMemory;
    GBrush *b;
    GpStatus st = line_new(GP_LINE_POINTS, c1, c2, wrap, &b);
    if (st != Ok)
        return st;
    b->p1 = *p1;
    b->p2 = *p2;
    /* the rectangle of the points: a vertical or horizontal line gets the other side as long as itself */
    float dx = p2->X - p1->X, dy = p2->Y - p1->Y;
    b->rect.X = p1->X < p2->X ? p1->X : p2->X;
    b->rect.Y = p1->Y < p2->Y ? p1->Y : p2->Y;
    b->rect.Width = dx < 0 ? -dx : dx;
    b->rect.Height = dy < 0 ? -dy : dy;
    if (close_float(p1->X, p2->X)) {
        b->rect.Width = b->rect.Height;
        b->rect.X -= b->rect.Width * 0.5f;
    }
    if (close_float(p1->Y, p2->Y)) {
        b->rect.Height = b->rect.Width;
        b->rect.Y -= b->rect.Height * 0.5f;
    }
    *brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushI(const GpPoint *p1, const GpPoint *p2, ARGB c1, ARGB c2, GpWrapMode wrap,
                                                GpLineGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!p1 || !p2)
        return InvalidParameter;
    GpPointF a = {(float)p1->X, (float)p1->Y}, b = {(float)p2->X, (float)p2->Y};
    return GdipCreateLineBrush(&a, &b, c1, c2, wrap, brush);
}

WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRect(const GpRectF *rect, ARGB c1, ARGB c2, LinearGradientMode mode,
                                                       GpWrapMode wrap, GpLineGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!rect || !brush)
        return InvalidParameter;
    if (mode < LinearGradientModeHorizontal || mode > LinearGradientModeBackwardDiagonal || !wrap_valid(wrap))
        return InvalidParameter;
    if (rect->Width == 0.0f || rect->Height == 0.0f)
        return OutOfMemory;
    GBrush *b;
    GpStatus st = line_new(mode, c1, c2, wrap, &b);
    if (st != Ok)
        return st;
    b->rect = *rect;
    *brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRectI(const GpRect *rect, ARGB c1, ARGB c2, LinearGradientMode mode,
                                                        GpWrapMode wrap, GpLineGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!rect)
        return InvalidParameter;
    GpRectF r = {(float)rect->X, (float)rect->Y, (float)rect->Width, (float)rect->Height};
    return GdipCreateLineBrushFromRect(&r, c1, c2, mode, wrap, brush);
}

WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRectWithAngle(const GpRectF *rect, ARGB c1, ARGB c2, REAL angle,
                                                                BOOL scalable, GpWrapMode wrap, GpLineGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!rect || !brush || !wrap_valid(wrap))
        return InvalidParameter;
    if (rect->Width == 0.0f || rect->Height == 0.0f)
        return OutOfMemory;
    GBrush *b;
    GpStatus st = line_new(GP_LINE_ANGLE, c1, c2, wrap, &b);
    if (st != Ok)
        return st;
    b->rect = *rect;
    b->angle = angle;
    b->scalable = scalable != FALSE;
    *brush = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRectWithAngleI(const GpRect *rect, ARGB c1, ARGB c2, REAL angle,
                                                                 BOOL scalable, GpWrapMode wrap, GpLineGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!rect)
        return InvalidParameter;
    GpRectF r = {(float)rect->X, (float)rect->Y, (float)rect->Width, (float)rect->Height};
    return GdipCreateLineBrushFromRectWithAngle(&r, c1, c2, angle, scalable, wrap, brush);
}

#define LINE_BRUSH(b, st) GBrush *b = brush_of(brush, BrushTypeLinearGradient, &st)

WINGDIPAPI GpStatus WINAPI GdipSetLineColors(GpLineGradient *brush, ARGB c1, ARGB c2)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    b->color1 = c1;
    b->color2 = c2;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineColors(GpLineGradient *brush, ARGB *colors)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    if (!colors)
        return InvalidParameter;
    colors[0] = b->color1;
    colors[1] = b->color2;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineRect(GpLineGradient *brush, GpRectF *rect)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    if (!rect)
        return InvalidParameter;
    *rect = b->rect;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineRectI(GpLineGradient *brush, GpRect *rect)
{
    GpRectF r;
    GpStatus st = GdipGetLineRect(brush, &r);
    if (st != Ok)
        return st;
    rect->X = (INT)r.X;
    rect->Y = (INT)r.Y;
    rect->Width = (INT)r.Width;
    rect->Height = (INT)r.Height;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetLineGammaCorrection(GpLineGradient *brush, BOOL gamma)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    b->gamma = gamma != FALSE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineGammaCorrection(GpLineGradient *brush, BOOL *gamma)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    if (!gamma)
        return InvalidParameter;
    *gamma = b->gamma;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineBlendCount(GpLineGradient *brush, INT *count)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? blend_get_count(b, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineBlend(GpLineGradient *brush, REAL *factors, REAL *positions, INT count)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? blend_get(b, factors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetLineBlend(GpLineGradient *brush, const REAL *factors, const REAL *positions, INT count)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? blend_set(b, factors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetLinePresetBlendCount(GpLineGradient *brush, INT *count)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? preset_get_count(b, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetLinePresetBlend(GpLineGradient *brush, ARGB *colors, REAL *positions, INT count)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? preset_get(b, colors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetLinePresetBlend(GpLineGradient *brush, const ARGB *colors, const REAL *positions,
                                                  INT count)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? preset_set(b, colors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetLineSigmaBlend(GpLineGradient *brush, REAL focus, REAL scale)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? blend_sigma(b, focus, scale) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetLineLinearBlend(GpLineGradient *brush, REAL focus, REAL scale)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? blend_linear(b, focus, scale) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetLineWrapMode(GpLineGradient *brush, GpWrapMode wrap)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    if (!wrap_valid(wrap) || wrap == WrapModeClamp)
        return InvalidParameter;
    b->wrap = wrap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineWrapMode(GpLineGradient *brush, GpWrapMode *wrap)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    if (!wrap)
        return InvalidParameter;
    *wrap = b->wrap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLineTransform(GpLineGradient *brush, GpMatrix *matrix)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? brush_get_transform(b, matrix) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetLineTransform(GpLineGradient *brush, const GpMatrix *matrix)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? brush_set_transform(b, matrix) : st;
}

WINGDIPAPI GpStatus WINAPI GdipResetLineTransform(GpLineGradient *brush)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    if (!b)
        return st;
    mat_identity(&b->xform);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipMultiplyLineTransform(GpLineGradient *brush, const GpMatrix *matrix, GpMatrixOrder order)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? brush_multiply(b, matrix, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipTranslateLineTransform(GpLineGradient *brush, REAL dx, REAL dy, GpMatrixOrder order)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? brush_translate(b, dx, dy, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipScaleLineTransform(GpLineGradient *brush, REAL sx, REAL sy, GpMatrixOrder order)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? brush_scale(b, sx, sy, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipRotateLineTransform(GpLineGradient *brush, REAL angle, GpMatrixOrder order)
{
    GpStatus st;
    LINE_BRUSH(b, st);
    return b ? brush_rotate(b, angle, order) : st;
}

/* ------------------------------------------------------------------ path gradient */

static void points_center(const GpPointF *pts, INT n, GpPointF *center, GpRectF *bounds)
{
    float minx = pts[0].X, maxx = minx, miny = pts[0].Y, maxy = miny, sx = 0, sy = 0;
    for (INT i = 0; i < n; i++) {
        sx += pts[i].X;
        sy += pts[i].Y;
        if (pts[i].X < minx)
            minx = pts[i].X;
        if (pts[i].X > maxx)
            maxx = pts[i].X;
        if (pts[i].Y < miny)
            miny = pts[i].Y;
        if (pts[i].Y > maxy)
            maxy = pts[i].Y;
    }
    center->X = sx / (float)n;
    center->Y = sy / (float)n;
    bounds->X = minx;
    bounds->Y = miny;
    bounds->Width = maxx - minx;
    bounds->Height = maxy - miny;
}

static GpStatus pathgrad_from_path(const GPath *src, WrapMode wrap, GBrush **out)
{
    if (src->path.count < 2)
        return OutOfMemory;
    GBrush *b = gp_brush_new(BrushTypePathGradient);
    if (!b)
        return OutOfMemory;
    b->pg_path = gp_path_clone(src);
    if (!b->pg_path) {
        gp_brush_free(b);
        return OutOfMemory;
    }
    GpPointF *pts = (GpPointF *)b->pg_path->path.pts;
    GpRectF bounds;
    points_center(pts, (INT)b->pg_path->path.count, &b->center, &bounds);
    if (bounds.Width <= 0.0f || bounds.Height <= 0.0f) {
        gp_brush_free(b);
        return OutOfMemory;
    }
    b->rect = bounds;
    b->wrap = wrap;
    b->surround = gp_alloc(sizeof(ARGB) * b->pg_path->path.count);
    if (!b->surround) {
        gp_brush_free(b);
        return OutOfMemory;
    }
    for (UINT i = 0; i < b->pg_path->path.count; i++)
        b->surround[i] = 0xffffffffu;
    b->surround_count = 1;
    *out = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreatePathGradient(const GpPointF *points, INT count, GpWrapMode wrap,
                                                  GpPathGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!points || !brush || count < 2 || wrap < WrapModeTile || wrap > WrapModeClamp)
        return InvalidParameter;
    GPath *p = gp_path_new(FillModeAlternate);
    if (!p)
        return OutOfMemory;
    if (mime_vec_path_add_polygon(&p->path, (const MimeVecPoint *)points, (UINT)count) != MIME_VEC_OK) {
        gp_path_free(p);
        return OutOfMemory;
    }
    GBrush *b;
    GpStatus st = pathgrad_from_path(p, wrap, &b);
    gp_path_free(p);
    if (st == Ok)
        *brush = b;
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipCreatePathGradientI(const GpPoint *points, INT count, GpWrapMode wrap,
                                                   GpPathGradient **brush)
{
    GP_REQUIRE_INIT();
    if (!points || !brush || count < 2)
        return InvalidParameter;
    GpPointF *f = gp_alloc(sizeof(GpPointF) * count);
    if (!f)
        return OutOfMemory;
    for (INT i = 0; i < count; i++) {
        f[i].X = (float)points[i].X;
        f[i].Y = (float)points[i].Y;
    }
    GpStatus st = GdipCreatePathGradient(f, count, wrap, brush);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipCreatePathGradientFromPath(const GpPath *path, GpPathGradient **brush)
{
    GP_REQUIRE_INIT();
    GPath *p = gp_path_cast(path);
    if (!p || !brush)
        return InvalidParameter;
    GBrush *b;
    GpStatus st = pathgrad_from_path(p, WrapModeClamp, &b);
    if (st == Ok)
        *brush = b;
    return st;
}

#define PG_BRUSH(b, st) GBrush *b = brush_of(brush, BrushTypePathGradient, &st)

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientCenterColor(GpPathGradient *brush, ARGB *color)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!color)
        return InvalidParameter;
    *color = b->center_color;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientCenterColor(GpPathGradient *brush, ARGB color)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    b->center_color = color;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientSurroundColorsWithCount(GpPathGradient *brush, ARGB *colors, INT *count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!colors || !count)
        return InvalidParameter;
    INT have = b->surround_count;
    if (*count < have || *count > (INT)b->pg_path->path.count)
        return InvalidParameter;
    for (INT i = 0; i < have; i++)
        colors[i] = b->surround[i];
    *count = have;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientSurroundColorsWithCount(GpPathGradient *brush, const ARGB *colors,
                                                                      INT *count)
{
    GP_REQUIRE_INIT();
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!colors || !count)
        return InvalidParameter;
    INT n = *count;
    if (n <= 0 || n > (INT)b->pg_path->path.count)
        return InvalidParameter;
    ARGB *copy = gp_alloc(sizeof(ARGB) * b->pg_path->path.count);
    if (!copy)
        return OutOfMemory;
    for (INT i = 0; i < n; i++)
        copy[i] = colors[i];
    /* the points without a color of their own take the last one */
    for (UINT i = (UINT)n; i < b->pg_path->path.count; i++)
        copy[i] = colors[n - 1];
    gp_free(b->surround);
    b->surround = copy;
    b->surround_count = n;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPath(GpPathGradient *brush, GpPath *path)
{
    GpStatus st;
    PG_BRUSH(b, st);
    GPath *p = gp_path_cast(path);
    if (!b)
        return st;
    if (!p)
        return InvalidParameter;
    mime_vec_path_clear(&p->path);
    if (mime_vec_path_copy(&p->path, &b->pg_path->path) != MIME_VEC_OK)
        return OutOfMemory;
    p->fill_mode = b->pg_path->fill_mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientPath(GpPathGradient *brush, const GpPath *path)
{
    GpStatus st;
    PG_BRUSH(b, st);
    GPath *p = gp_path_cast(path);
    if (!b)
        return st;
    if (!p || p->path.count < 2)
        return InvalidParameter;
    GPath *copy = gp_path_clone(p);
    ARGB *surround = gp_alloc(sizeof(ARGB) * p->path.count);
    if (!copy || !surround) {
        gp_path_free(copy);
        gp_free(surround);
        return OutOfMemory;
    }
    for (UINT i = 0; i < p->path.count; i++)
        surround[i] = 0xffffffffu;
    gp_path_free(b->pg_path);
    gp_free(b->surround);
    b->pg_path = copy;
    b->surround = surround;
    b->surround_count = 1;
    points_center((GpPointF *)copy->path.pts, (INT)copy->path.count, &b->center, &b->rect);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientCenterPoint(GpPathGradient *brush, GpPointF *point)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!point)
        return InvalidParameter;
    *point = b->center;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientCenterPointI(GpPathGradient *brush, GpPoint *point)
{
    GpPointF p;
    GpStatus st = GdipGetPathGradientCenterPoint(brush, &p);
    if (st != Ok)
        return st;
    point->X = (INT)p.X;
    point->Y = (INT)p.Y;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientCenterPoint(GpPathGradient *brush, const GpPointF *point)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!point)
        return InvalidParameter;
    b->center = *point;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientCenterPointI(GpPathGradient *brush, const GpPoint *point)
{
    if (!point)
        return InvalidParameter;
    GpPointF p = {(float)point->X, (float)point->Y};
    return GdipSetPathGradientCenterPoint(brush, &p);
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientRect(GpPathGradient *brush, GpRectF *rect)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!rect)
        return InvalidParameter;
    *rect = b->rect;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientRectI(GpPathGradient *brush, GpRect *rect)
{
    GpRectF r;
    GpStatus st = GdipGetPathGradientRect(brush, &r);
    if (st != Ok)
        return st;
    rect->X = (INT)r.X;
    rect->Y = (INT)r.Y;
    rect->Width = (INT)r.Width;
    rect->Height = (INT)r.Height;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPointCount(GpPathGradient *brush, INT *count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!count)
        return InvalidParameter;
    *count = (INT)b->pg_path->path.count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientSurroundColorCount(GpPathGradient *brush, INT *count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!count)
        return InvalidParameter;
    *count = b->surround_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientGammaCorrection(GpPathGradient *brush, BOOL gamma)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    b->gamma = gamma != FALSE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientGammaCorrection(GpPathGradient *brush, BOOL *gamma)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!gamma)
        return InvalidParameter;
    *gamma = b->gamma;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientBlendCount(GpPathGradient *brush, INT *count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? blend_get_count(b, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientBlend(GpPathGradient *brush, REAL *factors, REAL *positions, INT count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? blend_get(b, factors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientBlend(GpPathGradient *brush, const REAL *factors, const REAL *positions,
                                                    INT count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? blend_set(b, factors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPresetBlendCount(GpPathGradient *brush, INT *count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? preset_get_count(b, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPresetBlend(GpPathGradient *brush, ARGB *colors, REAL *positions,
                                                          INT count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? preset_get(b, colors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientPresetBlend(GpPathGradient *brush, const ARGB *colors,
                                                          const REAL *positions, INT count)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? preset_set(b, colors, positions, count) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientSigmaBlend(GpPathGradient *brush, REAL focus, REAL scale)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? blend_sigma(b, focus, scale) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientLinearBlend(GpPathGradient *brush, REAL focus, REAL scale)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? blend_linear(b, focus, scale) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientWrapMode(GpPathGradient *brush, GpWrapMode *wrap)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!wrap)
        return InvalidParameter;
    *wrap = b->wrap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientWrapMode(GpPathGradient *brush, GpWrapMode wrap)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!wrap_valid(wrap))
        return InvalidParameter;
    b->wrap = wrap;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientTransform(GpPathGradient *brush, GpMatrix *matrix)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? brush_get_transform(b, matrix) : st;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientTransform(GpPathGradient *brush, GpMatrix *matrix)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? brush_set_transform(b, matrix) : st;
}

WINGDIPAPI GpStatus WINAPI GdipResetPathGradientTransform(GpPathGradient *brush)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    mat_identity(&b->xform);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipMultiplyPathGradientTransform(GpPathGradient *brush, const GpMatrix *matrix,
                                                             GpMatrixOrder order)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? brush_multiply(b, matrix, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipTranslatePathGradientTransform(GpPathGradient *brush, REAL dx, REAL dy,
                                                              GpMatrixOrder order)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? brush_translate(b, dx, dy, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipScalePathGradientTransform(GpPathGradient *brush, REAL sx, REAL sy, GpMatrixOrder order)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? brush_scale(b, sx, sy, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipRotatePathGradientTransform(GpPathGradient *brush, REAL angle, GpMatrixOrder order)
{
    GpStatus st;
    PG_BRUSH(b, st);
    return b ? brush_rotate(b, angle, order) : st;
}

WINGDIPAPI GpStatus WINAPI GdipGetPathGradientFocusScales(GpPathGradient *brush, REAL *x, REAL *y)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (!x || !y)
        return InvalidParameter;
    *x = b->focus_x;
    *y = b->focus_y;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPathGradientFocusScales(GpPathGradient *brush, REAL x, REAL y)
{
    GpStatus st;
    PG_BRUSH(b, st);
    if (!b)
        return st;
    if (x < 0.0f || x >= 1.0f || y < 0.0f || y >= 1.0f)
        return InvalidParameter;
    b->focus_x = x;
    b->focus_y = y;
    return Ok;
}
