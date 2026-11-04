/* the Graphics object: its target, modes, transforms, clip and saved states. */
#include "gp_private.h"

GGraphics *gp_graphics_cast(const void *p)
{
    return GP_CAST(GGraphics, GP_TAG_GRAPHICS, p);
}

#define GFX(g, p)                   \
    GGraphics *g = gp_graphics_cast(p); \
    if (!g)                         \
        return InvalidParameter

#define GFX_FREE(g, p)                  \
    GFX(g, p);                          \
    if (g->got_dc)                      \
        return ObjectBusy

/* ------------------------------------------------------------------ transforms */

void gp_graphics_page_scale(const GGraphics *g, float *sx, float *sy)
{
    float k = g->page_scale;
    switch (g->page_unit) {
    case UnitDisplay:
        if (g->target || g->hdc) {
            /* a display device counts its unit in pixels. */
            *sx = *sy = k;
            return;
        }
        *sx = k * g->dpi_x / 100.0f;
        *sy = k * g->dpi_y / 100.0f;
        return;
    case UnitPoint:
        *sx = k * g->dpi_x / 72.0f;
        *sy = k * g->dpi_y / 72.0f;
        return;
    case UnitInch:
        *sx = k * g->dpi_x;
        *sy = k * g->dpi_y;
        return;
    case UnitDocument:
        *sx = k * g->dpi_x / 300.0f;
        *sy = k * g->dpi_y / 300.0f;
        return;
    case UnitMillimeter:
        *sx = k * g->dpi_x / 25.4f;
        *sy = k * g->dpi_y / 25.4f;
        return;
    default:
        *sx = *sy = k;
    }
}

MimeVecMatrix gp_graphics_w2d_base(const GGraphics *g)
{
    float sx, sy;
    gp_graphics_page_scale(g, &sx, &sy);
    MimeVecMatrix m = g->world;
    m.m11 *= sx;
    m.m12 *= sy;
    m.m21 *= sx;
    m.m22 *= sy;
    m.dx *= sx;
    m.dy *= sy;
    if (!mime_vec_matrix_is_identity(&g->container))
        mime_vec_matrix_multiply(&m, &m, &g->container);
    return m;
}

const MimeVecMatrix *gp_graphics_w2d(GGraphics *g)
{
    if (!g->w2d_valid) {
        MimeVecMatrix m = gp_graphics_w2d_base(g);
        /* the half pixel offset modes shift the picture back by half a pixel. */
        if (g->pixel_offset == PixelOffsetModeHighQuality || g->pixel_offset == PixelOffsetModeHalf) {
            m.dx -= 0.5f;
            m.dy -= 0.5f;
        }
        m.dx += (float)g->dc_x;
        m.dy += (float)g->dc_y;
        g->w2d = m;
        g->w2d_valid = TRUE;
    }
    return &g->w2d;
}

float gp_graphics_unit_to_world(const GGraphics *g, GpUnit unit)
{
    if (unit == UnitWorld)
        return 1.0f;
    /* unit to pixels, then back to world units through the page scale, ignoring the world transform. */
    float px = gp_unit_to_pixels(unit, g->dpi_x);
    float sx, sy;
    gp_graphics_page_scale(g, &sx, &sy);
    return sx > 0.0f ? px / sx : px;
}

/* ------------------------------------------------------------------ creation */

static void graphics_defaults(GGraphics *g)
{
    g->interpolation = InterpolationModeBilinear;
    g->smoothing = SmoothingModeDefault;
    g->compositing_mode = CompositingModeSourceOver;
    g->compositing_quality = CompositingQualityDefault;
    g->pixel_offset = PixelOffsetModeDefault;
    g->text_hint = TextRenderingHintSystemDefault;
    g->text_contrast = 4;
    g->page_unit = UnitPixel;
    g->page_scale = 1.0f;
    mime_vec_matrix_identity(&g->world);
    mime_vec_matrix_identity(&g->container);
    mime_vec_mask_init(&g->clip, &gp_allocator);
    g->next_state_id = 1;
}

static GpStatus graphics_from_hdc(HDC hdc, HWND hwnd, BOOL owns, GpGraphics **graphics)
{
    if (!graphics)
        return InvalidParameter;
    if (!hdc)
        return OutOfMemory;
    if (GetObjectType(hdc) == 0)
        return OutOfMemory;
    GGraphics *g = gp_alloc_zero(sizeof(*g));
    if (!g)
        return OutOfMemory;
    g->tag = GP_TAG_GRAPHICS;
    g->hdc = hdc;
    g->hwnd = hwnd;
    g->owns_dc = owns;
    graphics_defaults(g);
    g->dpi_x = (float)GetDeviceCaps(hdc, LOGPIXELSX);
    g->dpi_y = (float)GetDeviceCaps(hdc, LOGPIXELSY);
    if (g->dpi_x <= 0.0f)
        g->dpi_x = 96.0f;
    if (g->dpi_y <= 0.0f)
        g->dpi_y = 96.0f;
    /* the surface size: the bitmap selected in a memory device context, else the display. */
    HGDIOBJ obj = GetCurrentObject(hdc, OBJ_BITMAP);
    BITMAP bm;
    if (GetObjectType(hdc) == OBJ_MEMDC && obj && GetObjectW(obj, sizeof(bm), &bm)) {
        g->width = bm.bmWidth;
        g->height = bm.bmHeight;
    } else {
        g->width = GetDeviceCaps(hdc, HORZRES);
        g->height = GetDeviceCaps(hdc, VERTRES);
    }
    /* winforms paints a parent's background into a child this way, offsetting the viewport of the child's dc. */
    POINT origin = {0, 0};
    if (LPtoDP(hdc, &origin, 1)) {
        g->dc_x = origin.x;
        g->dc_y = origin.y;
    }
    *graphics = g;
    return Ok;
}

/* a recording graphics has no surface, only the size of its page. */
GGraphics *gp_graphics_new_recording(struct GMetafile *meta, float dpi_x, float dpi_y, INT width, INT height)
{
    GGraphics *g = gp_alloc_zero(sizeof(*g));
    if (!g)
        return nullptr;
    g->tag = GP_TAG_GRAPHICS;
    g->meta = meta;
    graphics_defaults(g);
    g->dpi_x = dpi_x;
    g->dpi_y = dpi_y;
    g->width = width;
    g->height = height;
    return g;
}

WINGDIPAPI GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics)
{
    GP_REQUIRE_INIT();
    return graphics_from_hdc(hdc, nullptr, FALSE, graphics);
}

WINGDIPAPI GpStatus WINAPI GdipCreateFromHDC2(HDC hdc, HANDLE device, GpGraphics **graphics)
{
    GP_REQUIRE_INIT();
    return graphics_from_hdc(hdc, nullptr, FALSE, graphics);
}

WINGDIPAPI GpStatus WINAPI GdipCreateFromHWND(HWND hwnd, GpGraphics **graphics)
{
    GP_REQUIRE_INIT();
    if (!graphics)
        return InvalidParameter;
    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return OutOfMemory;
    GpStatus st = graphics_from_hdc(hdc, hwnd, TRUE, graphics);
    if (st != Ok)
        ReleaseDC(hwnd, hdc);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipCreateFromHWNDICM(HWND hwnd, GpGraphics **graphics)
{
    GP_REQUIRE_INIT();
    return GdipCreateFromHWND(hwnd, graphics);
}

WINGDIPAPI GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!graphics)
        return InvalidParameter;
    if (!b) {
        GImage *im = gp_image_cast(image);
        return im ? gp_meta_graphics_context(image, graphics) : InvalidParameter;
    }
    if (b->locked)
        return ObjectBusy;
    switch (b->format) {
    case PixelFormat1bppIndexed:
    case PixelFormat4bppIndexed:
    case PixelFormat8bppIndexed:
    case PixelFormat16bppGrayScale:
    case PixelFormat32bppCMYK:
        return OutOfMemory;
    default:
        break;
    }
    GGraphics *g = gp_alloc_zero(sizeof(*g));
    if (!g)
        return OutOfMemory;
    g->tag = GP_TAG_GRAPHICS;
    g->target = b;
    graphics_defaults(g);
    g->width = b->width;
    g->height = b->height;
    g->dpi_x = b->dpi_x > 0 ? b->dpi_x : 96.0f;
    g->dpi_y = b->dpi_y > 0 ? b->dpi_y : 96.0f;
    b->graphics_refs++;
    *graphics = g;
    return Ok;
}

static void state_free(GState *s)
{
    mime_vec_mask_free(&s->clip);
    gp_free(s);
}

WINGDIPAPI GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics)
{
    GFX(g, graphics);
    if (g->meta)
        gp_meta_graphics_deleted(g);
    if (g->bitmap_dc)
        gp_graphics_release_bitmap_dc(g, g->bitmap_dc, FALSE);
    while (g->states) {
        GState *s = g->states;
        g->states = s->next;
        state_free(s);
    }
    mime_vec_mask_free(&g->clip);
    if (g->target && --g->target->graphics_refs == 0 && g->target->disposed)
        gp_bitmap_free(g->target);
    if (g->owns_dc)
        ReleaseDC(g->hwnd, g->hdc);
    g->tag = 0;
    gp_free(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipFlush(GpGraphics *graphics, GpFlushIntention intention)
{
    GFX(g, graphics);
    if (g->hdc && !g->target)
        GdiFlush();
    return Ok;
}

/* ------------------------------------------------------------------ device context */

WINGDIPAPI GpStatus WINAPI GdipGetDC(GpGraphics *graphics, HDC *hdc)
{
    GFX(g, graphics);
    if (!hdc)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    if (g->meta) {
        GpStatus rs = gp_rec_get_dc(g, hdc);
        if (rs == Ok)
            g->got_dc = TRUE;
        return rs;
    }
    if (g->hdc && !g->target) {
        g->got_dc = TRUE;
        /* the graphics clip applies to the context while it is out. */
        *hdc = g->hdc;
        return Ok;
    }
    if (!g->target)
        return GenericError;
    HDC dc;
    GpStatus st = gp_graphics_make_bitmap_dc(g, &dc);
    if (st != Ok)
        return st;
    g->got_dc = TRUE;
    *hdc = dc;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipReleaseDC(GpGraphics *graphics, HDC hdc)
{
    GFX(g, graphics);
    if (!g->got_dc)
        return InvalidParameter;
    if (g->meta) {
        gp_rec_release_dc(g);
        g->got_dc = FALSE;
        return Ok;
    }
    if (g->bitmap_dc) {
        if (hdc != g->bitmap_dc)
            return InvalidParameter;
        gp_graphics_release_bitmap_dc(g, hdc, TRUE);
    } else if (hdc != g->hdc) {
        return InvalidParameter;
    }
    g->got_dc = FALSE;
    return Ok;
}

/* ------------------------------------------------------------------ modes */

WINGDIPAPI GpStatus WINAPI GdipSetCompositingMode(GpGraphics *graphics, GpCompositingMode mode)
{
    GFX_FREE(g, graphics);
    if (mode != CompositingModeSourceOver && mode != CompositingModeSourceCopy)
        return InvalidParameter;
    g->compositing_mode = mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCompositingMode(GpGraphics *graphics, GpCompositingMode *mode)
{
    GFX(g, graphics);
    if (!mode)
        return InvalidParameter;
    *mode = g->compositing_mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetRenderingOrigin(GpGraphics *graphics, INT x, INT y)
{
    GFX_FREE(g, graphics);
    g->origin.x = x;
    g->origin.y = y;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetRenderingOrigin(GpGraphics *graphics, INT *x, INT *y)
{
    GFX(g, graphics);
    if (!x || !y)
        return InvalidParameter;
    *x = g->origin.x;
    *y = g->origin.y;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetCompositingQuality(GpGraphics *graphics, GpCompositingQuality quality)
{
    GFX_FREE(g, graphics);
    if (quality < CompositingQualityDefault || quality > CompositingQualityAssumeLinear)
        return InvalidParameter;
    g->compositing_quality = quality;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetCompositingQuality(GpGraphics *graphics, GpCompositingQuality *quality)
{
    GFX(g, graphics);
    if (!quality)
        return InvalidParameter;
    *quality = g->compositing_quality;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, GpSmoothingMode mode)
{
    GFX_FREE(g, graphics);
    if (mode < SmoothingModeDefault || mode > SmoothingModeAntiAlias8x8)
        return InvalidParameter;
    g->smoothing = mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetSmoothingMode(GpGraphics *graphics, GpSmoothingMode *mode)
{
    GFX(g, graphics);
    if (!mode)
        return InvalidParameter;
    *mode = g->smoothing;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPixelOffsetMode(GpGraphics *graphics, GpPixelOffsetMode mode)
{
    GFX_FREE(g, graphics);
    if (mode < PixelOffsetModeDefault || mode > PixelOffsetModeHalf)
        return InvalidParameter;
    g->pixel_offset = mode;
    g->w2d_valid = FALSE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPixelOffsetMode(GpGraphics *graphics, GpPixelOffsetMode *mode)
{
    GFX(g, graphics);
    if (!mode)
        return InvalidParameter;
    *mode = g->pixel_offset;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetTextRenderingHint(GpGraphics *graphics, GpTextRenderingHint hint)
{
    GFX_FREE(g, graphics);
    if (hint < TextRenderingHintSystemDefault || hint > TextRenderingHintClearTypeGridFit)
        return InvalidParameter;
    g->text_hint = hint;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetTextRenderingHint(GpGraphics *graphics, GpTextRenderingHint *hint)
{
    GFX(g, graphics);
    if (!hint)
        return InvalidParameter;
    *hint = g->text_hint;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetTextContrast(GpGraphics *graphics, UINT contrast)
{
    GFX_FREE(g, graphics);
    if (contrast > 12)
        return InvalidParameter;
    g->text_contrast = contrast;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetTextContrast(GpGraphics *graphics, UINT *contrast)
{
    GFX(g, graphics);
    if (!contrast)
        return InvalidParameter;
    *contrast = g->text_contrast;
    return Ok;
}

/* like gdi+, the quality modes resolve to a concrete mode. */
WINGDIPAPI GpStatus WINAPI GdipSetInterpolationMode(GpGraphics *graphics, GpInterpolationMode mode)
{
    GFX_FREE(g, graphics);
    if (mode <= InterpolationModeInvalid || mode > InterpolationModeHighQualityBicubic)
        return InvalidParameter;
    if (mode == InterpolationModeDefault || mode == InterpolationModeLowQuality)
        mode = InterpolationModeBilinear;
    else if (mode == InterpolationModeHighQuality)
        mode = InterpolationModeHighQualityBicubic;
    g->interpolation = mode;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetInterpolationMode(GpGraphics *graphics, GpInterpolationMode *mode)
{
    GFX(g, graphics);
    if (!mode)
        return InvalidParameter;
    *mode = g->interpolation;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGraphicsSetAbort(GpGraphics *graphics, GdiplusAbort *abort)
{
    GFX(g, graphics);
    return Ok;
}

/* ------------------------------------------------------------------ transforms */

static void world_changed(GGraphics *g) { g->w2d_valid = FALSE; }

WINGDIPAPI GpStatus WINAPI GdipSetWorldTransform(GpGraphics *graphics, GpMatrix *matrix)
{
    GFX_FREE(g, graphics);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m || !gp_matrix_invertible(&m->m))
        return InvalidParameter;
    g->world = m->m;
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipResetWorldTransform(GpGraphics *graphics)
{
    GFX_FREE(g, graphics);
    mime_vec_matrix_identity(&g->world);
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipMultiplyWorldTransform(GpGraphics *graphics, const GpMatrix *matrix, GpMatrixOrder order)
{
    GFX_FREE(g, graphics);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m || (order != MatrixOrderPrepend && order != MatrixOrderAppend))
        return InvalidParameter;
    gp_matrix_apply(&g->world, &m->m, order);
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipTranslateWorldTransform(GpGraphics *graphics, REAL dx, REAL dy, GpMatrixOrder order)
{
    GFX_FREE(g, graphics);
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op = {1, 0, 0, 1, dx, dy};
    gp_matrix_apply(&g->world, &op, order);
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipScaleWorldTransform(GpGraphics *graphics, REAL sx, REAL sy, GpMatrixOrder order)
{
    GFX_FREE(g, graphics);
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    MimeVecMatrix op = {sx, 0, 0, sy, 0, 0};
    gp_matrix_apply(&g->world, &op, order);
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipRotateWorldTransform(GpGraphics *graphics, REAL angle, GpMatrixOrder order)
{
    GFX_FREE(g, graphics);
    if (order != MatrixOrderPrepend && order != MatrixOrderAppend)
        return InvalidParameter;
    GMatrix tmp;
    tmp.tag = GP_TAG_MATRIX;
    mime_vec_matrix_identity(&tmp.m);
    GdipRotateMatrix(&tmp, angle, MatrixOrderPrepend);
    gp_matrix_apply(&g->world, &tmp.m, order);
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetWorldTransform(GpGraphics *graphics, GpMatrix *matrix)
{
    GFX(g, graphics);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    m->m = g->world;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipResetPageTransform(GpGraphics *graphics)
{
    GFX_FREE(g, graphics);
    g->page_unit = UnitPixel;
    g->page_scale = 1.0f;
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPageUnit(GpGraphics *graphics, GpUnit *unit)
{
    GFX(g, graphics);
    if (!unit)
        return InvalidParameter;
    *unit = g->page_unit;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPageScale(GpGraphics *graphics, REAL *scale)
{
    GFX(g, graphics);
    if (!scale)
        return InvalidParameter;
    *scale = g->page_scale;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPageUnit(GpGraphics *graphics, GpUnit unit)
{
    GFX_FREE(g, graphics);
    if (unit == UnitWorld || unit < UnitWorld || unit > UnitMillimeter)
        return InvalidParameter;
    g->page_unit = unit;
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPageScale(GpGraphics *graphics, REAL scale)
{
    GFX_FREE(g, graphics);
    if (scale <= 0.0f || scale > 1.0e8f)
        return InvalidParameter;
    g->page_scale = scale;
    world_changed(g);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetDpiX(GpGraphics *graphics, REAL *dpi)
{
    GFX(g, graphics);
    if (!dpi)
        return InvalidParameter;
    *dpi = g->dpi_x;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetDpiY(GpGraphics *graphics, REAL *dpi)
{
    GFX(g, graphics);
    if (!dpi)
        return InvalidParameter;
    *dpi = g->dpi_y;
    return Ok;
}

static BOOL space_matrix(GGraphics *g, GpCoordinateSpace dst, GpCoordinateSpace src, MimeVecMatrix *out)
{
    float sx, sy;
    gp_graphics_page_scale(g, &sx, &sy);
    MimeVecMatrix scale = {sx, 0, 0, sy, 0, 0};
    MimeVecMatrix up;
    mime_vec_matrix_identity(&up);
    /* src to device. */
    if (src == CoordinateSpaceWorld) {
        mime_vec_matrix_multiply(&up, &g->world, &scale);
    } else if (src == CoordinateSpacePage) {
        up = scale;
    }
    if (dst == CoordinateSpaceDevice) {
        *out = up;
        return TRUE;
    }
    MimeVecMatrix down;
    if (dst == CoordinateSpacePage) {
        if (!gp_matrix_inverse(&down, &scale))
            return FALSE;
    } else {
        MimeVecMatrix w2d;
        mime_vec_matrix_multiply(&w2d, &g->world, &scale);
        if (!gp_matrix_inverse(&down, &w2d))
            return FALSE;
    }
    mime_vec_matrix_multiply(out, &up, &down);
    return TRUE;
}

WINGDIPAPI GpStatus WINAPI GdipTransformPoints(GpGraphics *graphics, GpCoordinateSpace dst, GpCoordinateSpace src,
                                               GpPointF *points, INT count)
{
    GFX(g, graphics);
    if (!points || count <= 0 || dst < CoordinateSpaceWorld || dst > CoordinateSpaceDevice ||
        src < CoordinateSpaceWorld || src > CoordinateSpaceDevice)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    MimeVecMatrix m;
    if (!space_matrix(g, dst, src, &m))
        return GenericError;
    for (INT i = 0; i < count; i++) {
        MimeVecPoint p = mime_vec_xform(&m, points[i].X, points[i].Y);
        points[i].X = p.x;
        points[i].Y = p.y;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipTransformPointsI(GpGraphics *graphics, GpCoordinateSpace dst, GpCoordinateSpace src,
                                                GpPoint *points, INT count)
{
    GFX(g, graphics);
    if (!points || count <= 0 || dst < CoordinateSpaceWorld || dst > CoordinateSpaceDevice ||
        src < CoordinateSpaceWorld || src > CoordinateSpaceDevice)
        return InvalidParameter;
    MimeVecMatrix m;
    if (!space_matrix(g, dst, src, &m))
        return GenericError;
    for (INT i = 0; i < count; i++) {
        MimeVecPoint p = mime_vec_xform(&m, (float)points[i].X, (float)points[i].Y);
        points[i].X = (INT)mime_vec_floorf(p.x + 0.5f);
        points[i].Y = (INT)mime_vec_floorf(p.y + 0.5f);
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetNearestColor(GpGraphics *graphics, ARGB *color)
{
    GFX(g, graphics);
    if (!color)
        return InvalidParameter;
    if (g->target) {
        /* a lower bit depth bitmap keeps only what its format can hold. */
        UINT32 c = *color;
        BYTE row[8];
        gp_row_from_argb(row, g->target->format, g->target->palette, 0, 1, &c);
        UINT32 back;
        gp_row_to_argb(row, g->target->format, g->target->palette, 0, 1, &back);
        if (IsAlphaPixelFormat(g->target->format))
            *color = back;
        else
            *color = (c & 0xff000000u) | (back & 0x00ffffffu);
    } else if (g->hdc) {
        COLORREF ref = (COLORREF)((*color & 0xff) << 16 | (*color & 0xff00) | ((*color >> 16) & 0xff));
        COLORREF near_ = GetNearestColor(g->hdc, ref);
        if (near_ != CLR_INVALID)
            *color = (*color & 0xff000000u) | ((near_ & 0xff) << 16) | (near_ & 0xff00) | ((near_ >> 16) & 0xff);
    }
    return Ok;
}

WINGDIPAPI HPALETTE WINAPI GdipCreateHalftonePalette(void)
{
    HDC dc = GetDC(nullptr);
    if (!dc)
        return nullptr;
    HPALETTE pal = CreateHalftonePalette(dc);
    ReleaseDC(nullptr, dc);
    return pal;
}

WINGDIPAPI GpStatus WINAPI GdipComment(GpGraphics *graphics, UINT size, const BYTE *data)
{
    GFX(g, graphics);
    if (g->meta)
        gp_rec_comment(g, size, data);
    return Ok;
}

/* ------------------------------------------------------------------ clip */

static MimeVecRect target_rect(const GGraphics *g)
{
    RECT b;
    gp_graphics_bounds(g, &b);
    MimeVecRect r = {b.left, b.top, b.right, b.bottom};
    return r;
}

/* the whole target when no clip is set. */
static GpStatus clip_current(GGraphics *g, MimeVecMask *out)
{
    if (g->clip_set)
        return mime_vec_mask_copy(out, &g->clip) == MIME_VEC_OK ? Ok : OutOfMemory;
    MimeVecRect r = target_rect(g);
    return mime_vec_mask_set_rect(out, &r) == MIME_VEC_OK ? Ok : OutOfMemory;
}

/* takes ownership of the mask. */
static GpStatus clip_apply(GGraphics *g, MimeVecMask *mask, CombineMode mode)
{
    GpStatus st = Ok;
    if (mode == CombineModeReplace) {
        mime_vec_mask_free(&g->clip);
        g->clip = *mask;
        memset(mask, 0, sizeof(*mask));
        mask->alloc = &gp_allocator;
        g->clip_set = TRUE;
        return Ok;
    }
    MimeVecMask cur, out;
    mime_vec_mask_init(&cur, &gp_allocator);
    mime_vec_mask_init(&out, &gp_allocator);
    st = clip_current(g, &cur);
    if (st == Ok && mime_vec_mask_combine(&out, &cur, mask, (int)mode) != MIME_VEC_OK)
        st = OutOfMemory;
    if (st == Ok) {
        mime_vec_mask_free(&g->clip);
        g->clip = out;
        g->clip_set = TRUE;
    } else {
        mime_vec_mask_free(&out);
    }
    mime_vec_mask_free(&cur);
    mime_vec_mask_free(mask);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSetClipRect(GpGraphics *graphics, REAL x, REAL y, REAL w, REAL h, GpCombineMode mode)
{
    GFX_FREE(g, graphics);
    if (mode < CombineModeReplace || mode > CombineModeComplement)
        return InvalidParameter;
    GRegion *r = gp_region_new_infinite();
    if (!r)
        return OutOfMemory;
    GpRectF rc = {x, y, w, h};
    GpStatus st = GdipCombineRegionRect(r, &rc, CombineModeReplace);
    if (st == Ok)
        st = GdipSetClipRegion(graphics, r, mode);
    gp_region_free(r);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSetClipRectI(GpGraphics *graphics, INT x, INT y, INT w, INT h, GpCombineMode mode)
{
    return GdipSetClipRect(graphics, (float)x, (float)y, (float)w, (float)h, mode);
}

WINGDIPAPI GpStatus WINAPI GdipSetClipPath(GpGraphics *graphics, GpPath *path, GpCombineMode mode)
{
    GFX_FREE(g, graphics);
    GPath *p = gp_path_cast(path);
    if (!p || mode < CombineModeReplace || mode > CombineModeComplement)
        return InvalidParameter;
    GRegion *r;
    GpStatus st = GdipCreateRegionPath(path, (GpRegion **)&r);
    if (st != Ok)
        return st;
    st = GdipSetClipRegion(graphics, r, mode);
    gp_region_free(r);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSetClipRegion(GpGraphics *graphics, GpRegion *region, GpCombineMode mode)
{
    GFX_FREE(g, graphics);
    GRegion *r = gp_region_cast(region);
    if (!r || mode < CombineModeReplace || mode > CombineModeComplement)
        return InvalidParameter;
    if (g->meta)
        gp_rec_set_clip(g, r, mode);
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    MimeVecRect clip = target_rect(g);
    GpStatus st = gp_region_to_mask(r, gp_graphics_w2d(g), &clip, &mask);
    if (st != Ok) {
        mime_vec_mask_free(&mask);
        return st;
    }
    return clip_apply(g, &mask, mode);
}

WINGDIPAPI GpStatus WINAPI GdipSetClipHrgn(GpGraphics *graphics, HRGN hrgn, GpCombineMode mode)
{
    GFX_FREE(g, graphics);
    if (!hrgn || mode < CombineModeReplace || mode > CombineModeComplement)
        return InvalidParameter;
    DWORD size = GetRegionData(hrgn, 0, nullptr);
    if (!size)
        return InvalidParameter;
    RGNDATA *data = gp_alloc(size);
    if (!data)
        return OutOfMemory;
    if (!GetRegionData(hrgn, size, data)) {
        gp_free(data);
        return InvalidParameter;
    }
    MimeVecMask mask, one, tmp;
    mime_vec_mask_init(&mask, &gp_allocator);
    mime_vec_mask_init(&one, &gp_allocator);
    mime_vec_mask_init(&tmp, &gp_allocator);
    const RECT *rects = (const RECT *)data->Buffer;
    GpStatus st = Ok;
    for (DWORD i = 0; i < data->rdh.nCount && st == Ok; i++) {
        MimeVecRect r = {rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
        if (mime_vec_mask_set_rect(&one, &r) != MIME_VEC_OK ||
            mime_vec_mask_combine(&tmp, &mask, &one, MIME_VEC_COMBINE_UNION) != MIME_VEC_OK)
            st = OutOfMemory;
        else {
            mime_vec_mask_free(&mask);
            mask = tmp;
            mime_vec_mask_init(&tmp, &gp_allocator);
        }
    }
    gp_free(data);
    mime_vec_mask_free(&one);
    mime_vec_mask_free(&tmp);
    if (st != Ok) {
        mime_vec_mask_free(&mask);
        return st;
    }
    return clip_apply(g, &mask, mode);
}

WINGDIPAPI GpStatus WINAPI GdipSetClipGraphics(GpGraphics *graphics, GpGraphics *other, GpCombineMode mode)
{
    GFX_FREE(g, graphics);
    GGraphics *o = gp_graphics_cast(other);
    if (!o || mode < CombineModeReplace || mode > CombineModeComplement)
        return InvalidParameter;
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    GpStatus st = clip_current(o, &mask);
    if (st != Ok) {
        mime_vec_mask_free(&mask);
        return st;
    }
    return clip_apply(g, &mask, mode);
}

WINGDIPAPI GpStatus WINAPI GdipResetClip(GpGraphics *graphics)
{
    GFX_FREE(g, graphics);
    if (g->meta)
        gp_rec_reset_clip(g);
    g->clip_set = FALSE;
    mime_vec_mask_clear(&g->clip);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipTranslateClip(GpGraphics *graphics, REAL dx, REAL dy)
{
    GFX_FREE(g, graphics);
    if (g->meta)
        gp_rec_offset_clip(g, dx, dy);
    if (!g->clip_set)
        return Ok;
    const MimeVecMatrix *m = gp_graphics_w2d(g);
    float x = dx * m->m11 + dy * m->m21, y = dx * m->m12 + dy * m->m22;
    mime_vec_mask_translate(&g->clip, (int)mime_vec_floorf(x + 0.5f), (int)mime_vec_floorf(y + 0.5f));
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipTranslateClipI(GpGraphics *graphics, INT dx, INT dy)
{
    return GdipTranslateClip(graphics, (float)dx, (float)dy);
}

/* in world units: a rectangle when the device mask is one, else a union of its runs. */
WINGDIPAPI GpStatus WINAPI GdipGetClip(GpGraphics *graphics, GpRegion *region)
{
    GFX(g, graphics);
    GRegion *r = gp_region_cast(region);
    if (!r)
        return InvalidParameter;
    if (!g->clip_set)
        return GdipSetInfinite(region);
    const MimeVecMatrix *m = gp_graphics_w2d(g);
    MimeVecMatrix inv;
    if (!mime_vec_matrix_invert(&inv, m))
        return GenericError;
    GpStatus st = GdipSetEmpty(region);
    for (UINT b = 0; b < g->clip.nbands && st == Ok; b++) {
        const MimeVecBand *band = &g->clip.bands[b];
        for (UINT i = 0; i < band->count && st == Ok; i++) {
            const MimeVecRun *run = &g->clip.runs[band->first + i];
            GPath *p = gp_path_new(FillModeWinding);
            if (!p)
                return OutOfMemory;
            mime_vec_path_add_rect(&p->path, (float)run->x0, (float)band->y0, (float)(run->x1 - run->x0),
                                   (float)(band->y1 - band->y0));
            mime_vec_path_transform(&p->path, &inv);
            GpRegion *one;
            st = GdipCreateRegionPath(p, &one);
            gp_path_free(p);
            if (st == Ok) {
                st = GdipCombineRegionRegion(region, one, CombineModeUnion);
                gp_region_free(one);
            }
        }
    }
    return st;
}

static void clip_world_bounds(GGraphics *g, BOOL visible, GpRectF *out)
{
    if (!g->clip_set && !visible) {
        out->X = out->Y = -4194304.0f;
        out->Width = out->Height = 8388608.0f;
        return;
    }
    MimeVecRect b = target_rect(g);
    if (g->clip_set) {
        MimeVecRect cb = mime_vec_mask_bounds(&g->clip);
        if (cb.left > b.left)
            b.left = cb.left;
        if (cb.top > b.top)
            b.top = cb.top;
        if (cb.right < b.right)
            b.right = cb.right;
        if (cb.bottom < b.bottom)
            b.bottom = cb.bottom;
    }
    if (b.right <= b.left || b.bottom <= b.top) {
        memset(out, 0, sizeof(*out));
        return;
    }
    MimeVecMatrix inv;
    const MimeVecMatrix *m = gp_graphics_w2d(g);
    float x0 = (float)b.left, y0 = (float)b.top, x1 = (float)b.right, y1 = (float)b.bottom;
    if (mime_vec_matrix_invert(&inv, m)) {
        MimeVecPoint p[4] = {mime_vec_xform(&inv, x0, y0), mime_vec_xform(&inv, x1, y0), mime_vec_xform(&inv, x1, y1),
                             mime_vec_xform(&inv, x0, y1)};
        float lx = p[0].x, hx = p[0].x, ly = p[0].y, hy = p[0].y;
        for (INT i = 1; i < 4; i++) {
            if (p[i].x < lx)
                lx = p[i].x;
            if (p[i].x > hx)
                hx = p[i].x;
            if (p[i].y < ly)
                ly = p[i].y;
            if (p[i].y > hy)
                hy = p[i].y;
        }
        x0 = lx;
        x1 = hx;
        y0 = ly;
        y1 = hy;
    }
    out->X = x0;
    out->Y = y0;
    out->Width = x1 - x0;
    out->Height = y1 - y0;
}

WINGDIPAPI GpStatus WINAPI GdipGetClipBounds(GpGraphics *graphics, GpRectF *rect)
{
    GFX(g, graphics);
    if (!rect)
        return InvalidParameter;
    clip_world_bounds(g, FALSE, rect);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetClipBoundsI(GpGraphics *graphics, GpRect *rect)
{
    GFX(g, graphics);
    if (!rect)
        return InvalidParameter;
    GpRectF f;
    clip_world_bounds(g, FALSE, &f);
    INT l = (INT)mime_vec_floorf(f.X), t = (INT)mime_vec_floorf(f.Y);
    rect->X = l;
    rect->Y = t;
    rect->Width = (INT)mime_vec_ceilf(f.X + f.Width) - l;
    rect->Height = (INT)mime_vec_ceilf(f.Y + f.Height) - t;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsClipEmpty(GpGraphics *graphics, BOOL *result)
{
    GFX(g, graphics);
    if (!result)
        return InvalidParameter;
    *result = g->clip_set && mime_vec_mask_is_empty(&g->clip);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetVisibleClipBounds(GpGraphics *graphics, GpRectF *rect)
{
    GFX(g, graphics);
    if (!rect)
        return InvalidParameter;
    clip_world_bounds(g, TRUE, rect);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetVisibleClipBoundsI(GpGraphics *graphics, GpRect *rect)
{
    GFX(g, graphics);
    if (!rect)
        return InvalidParameter;
    GpRectF f;
    clip_world_bounds(g, TRUE, &f);
    INT l = (INT)mime_vec_floorf(f.X), t = (INT)mime_vec_floorf(f.Y);
    rect->X = l;
    rect->Y = t;
    rect->Width = (INT)mime_vec_ceilf(f.X + f.Width) - l;
    rect->Height = (INT)mime_vec_ceilf(f.Y + f.Height) - t;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleClipEmpty(GpGraphics *graphics, BOOL *result)
{
    GFX(g, graphics);
    if (!result)
        return InvalidParameter;
    GpRectF f;
    clip_world_bounds(g, TRUE, &f);
    *result = f.Width <= 0.0f || f.Height <= 0.0f || (g->clip_set && mime_vec_mask_is_empty(&g->clip));
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisiblePoint(GpGraphics *graphics, REAL x, REAL y, BOOL *result)
{
    GFX(g, graphics);
    if (!result)
        return InvalidParameter;
    MimeVecPoint p = mime_vec_xform(gp_graphics_w2d(g), x, y);
    INT px = (INT)mime_vec_floorf(p.x), py = (INT)mime_vec_floorf(p.y);
    RECT b;
    gp_graphics_bounds(g, &b);
    BOOL in = px >= b.left && px < b.right && py >= b.top && py < b.bottom;
    if (in && g->clip_set)
        in = mime_vec_mask_coverage_at(&g->clip, px, py) != 0;
    *result = in;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisiblePointI(GpGraphics *graphics, INT x, INT y, BOOL *result)
{
    return GdipIsVisiblePoint(graphics, (float)x, (float)y, result);
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleRect(GpGraphics *graphics, REAL x, REAL y, REAL w, REAL h, BOOL *result)
{
    GFX(g, graphics);
    if (!result)
        return InvalidParameter;
    const MimeVecMatrix *m = gp_graphics_w2d(g);
    MimeVecPoint p0 = mime_vec_xform(m, x, y), p1 = mime_vec_xform(m, x + w, y + h);
    float lx = p0.x < p1.x ? p0.x : p1.x, hx = p0.x < p1.x ? p1.x : p0.x;
    float ly = p0.y < p1.y ? p0.y : p1.y, hy = p0.y < p1.y ? p1.y : p0.y;
    MimeVecRect r = {(INT)mime_vec_floorf(lx), (INT)mime_vec_floorf(ly), (INT)mime_vec_ceilf(hx), (INT)mime_vec_ceilf(hy)};
    if (r.right <= r.left)
        r.right = r.left + 1;
    if (r.bottom <= r.top)
        r.bottom = r.top + 1;
    MimeVecRect b = target_rect(g);
    if (r.left < b.left)
        r.left = b.left;
    if (r.top < b.top)
        r.top = b.top;
    if (r.right > b.right)
        r.right = b.right;
    if (r.bottom > b.bottom)
        r.bottom = b.bottom;
    if (r.right <= r.left || r.bottom <= r.top) {
        *result = FALSE;
        return Ok;
    }
    *result = !g->clip_set || mime_vec_mask_test_rect(&g->clip, &r) != MIME_VEC_TEST_OUTSIDE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleRectI(GpGraphics *graphics, INT x, INT y, INT w, INT h, BOOL *result)
{
    return GdipIsVisibleRect(graphics, (float)x, (float)y, (float)w, (float)h, result);
}

/* ------------------------------------------------------------------ states and containers */

static GpStatus push_state(GGraphics *g, INT kind, UINT *id)
{
    GState *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return OutOfMemory;
    s->kind = kind;
    s->id = g->next_state_id++;
    s->world = g->world;
    s->container = g->container;
    s->page_unit = g->page_unit;
    s->page_scale = g->page_scale;
    s->smoothing = g->smoothing;
    s->compositing_mode = g->compositing_mode;
    s->compositing_quality = g->compositing_quality;
    s->interpolation = g->interpolation;
    s->pixel_offset = g->pixel_offset;
    s->text_hint = g->text_hint;
    s->text_contrast = g->text_contrast;
    s->origin = g->origin;
    s->clip_set = g->clip_set;
    mime_vec_mask_init(&s->clip, &gp_allocator);
    if (g->clip_set && mime_vec_mask_copy(&s->clip, &g->clip) != MIME_VEC_OK) {
        state_free(s);
        return OutOfMemory;
    }
    s->next = g->states;
    g->states = s;
    *id = s->id;
    return Ok;
}

static void restore_state(GGraphics *g, const GState *s)
{
    g->world = s->world;
    g->container = s->container;
    g->page_unit = s->page_unit;
    g->page_scale = s->page_scale;
    g->smoothing = s->smoothing;
    g->compositing_mode = s->compositing_mode;
    g->compositing_quality = s->compositing_quality;
    g->interpolation = s->interpolation;
    g->pixel_offset = s->pixel_offset;
    g->text_hint = s->text_hint;
    g->text_contrast = s->text_contrast;
    g->origin = s->origin;
    mime_vec_mask_free(&g->clip);
    mime_vec_mask_init(&g->clip, &gp_allocator);
    g->clip_set = s->clip_set;
    if (s->clip_set)
        mime_vec_mask_copy(&g->clip, &s->clip);
    g->w2d_valid = FALSE;
}

static GpStatus pop_to(GGraphics *g, UINT id, INT kind)
{
    GState *s = g->states;
    while (s && s->id != id)
        s = s->next;
    if (!s || s->kind != kind)
        return Ok;
    /* the states above it go too. */
    while (g->states != s) {
        GState *top = g->states;
        g->states = top->next;
        state_free(top);
    }
    restore_state(g, s);
    g->states = s->next;
    state_free(s);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSaveGraphics(GpGraphics *graphics, GraphicsState *state)
{
    GFX_FREE(g, graphics);
    if (!state)
        return InvalidParameter;
    UINT id;
    GpStatus st = push_state(g, 0, &id);
    if (st == Ok) {
        *state = id;
        if (g->meta)
            gp_rec_save(g, id);
    }
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipRestoreGraphics(GpGraphics *graphics, GraphicsState state)
{
    GFX_FREE(g, graphics);
    if (g->meta)
        gp_rec_restore(g, state, 0);
    return pop_to(g, state, 0);
}

WINGDIPAPI GpStatus WINAPI GdipBeginContainer2(GpGraphics *graphics, GraphicsContainer *container)
{
    GFX_FREE(g, graphics);
    if (!container)
        return InvalidParameter;
    UINT id;
    if (g->meta)
        gp_rec_sync(g);
    GpStatus st = push_state(g, 1, &id);
    if (st != Ok)
        return st;
    /* the new container starts with no world transform, but draws where its parent does. */
    MimeVecMatrix total = gp_graphics_w2d_base(g);
    g->container = total;
    mime_vec_matrix_identity(&g->world);
    g->page_unit = UnitPixel;
    g->page_scale = 1.0f;
    g->w2d_valid = FALSE;
    *container = id;
    if (g->meta)
        gp_rec_begin_container(g, id, nullptr, nullptr, UnitPixel);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBeginContainer(GpGraphics *graphics, const GpRectF *dst, const GpRectF *src, GpUnit unit,
                                              GraphicsContainer *container)
{
    GFX_FREE(g, graphics);
    if (!dst || !src || !container || unit < UnitPixel || unit > UnitMillimeter || src->Width == 0.0f ||
        src->Height == 0.0f)
        return InvalidParameter;
    struct GMetafile *recording = g->meta;
    if (recording)
        gp_rec_sync(g);
    g->meta = nullptr;
    GpStatus st = GdipBeginContainer2(graphics, container);
    g->meta = recording;
    if (st != Ok)
        return st;
    if (recording)
        gp_rec_begin_container(g, *container, dst, src, unit);
    /* the source rectangle is in unit, the destination in the parent's world. */
    float k = unit == UnitPixel ? 1.0f : gp_unit_to_pixels(unit, g->dpi_x);
    MimeVecMatrix map;
    float sx = dst->Width / (src->Width * k), sy = dst->Height / (src->Height * k);
    map.m11 = sx;
    map.m12 = 0;
    map.m21 = 0;
    map.m22 = sy;
    map.dx = dst->X - src->X * k * sx;
    map.dy = dst->Y - src->Y * k * sy;
    mime_vec_matrix_multiply(&g->container, &map, &g->container);
    g->w2d_valid = FALSE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBeginContainerI(GpGraphics *graphics, const GpRect *dst, const GpRect *src, GpUnit unit,
                                               GraphicsContainer *container)
{
    if (!dst || !src)
        return InvalidParameter;
    GpRectF d = {(float)dst->X, (float)dst->Y, (float)dst->Width, (float)dst->Height};
    GpRectF s = {(float)src->X, (float)src->Y, (float)src->Width, (float)src->Height};
    return GdipBeginContainer(graphics, &d, &s, unit, container);
}

WINGDIPAPI GpStatus WINAPI GdipEndContainer(GpGraphics *graphics, GraphicsContainer container)
{
    GFX_FREE(g, graphics);
    if (g->meta)
        gp_rec_restore(g, container, 1);
    return pop_to(g, container, 1);
}

/* ------------------------------------------------------------------ bitmap device context */

GpStatus gp_graphics_make_bitmap_dc(GGraphics *g, HDC *out)
{
    GBitmap *b = g->target;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = b->width;
    info.bmiHeader.biHeight = -b->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib)
            DeleteObject(dib);
        return OutOfMemory;
    }
    HDC dc = CreateCompatibleDC(nullptr);
    UINT32 *copy = gp_alloc((SIZE_T)b->width * b->height * 4);
    if (!dc || !copy) {
        if (dc)
            DeleteDC(dc);
        gp_free(copy);
        DeleteObject(dib);
        return OutOfMemory;
    }
    for (INT y = 0; y < b->height; y++) {
        UINT32 *row = (UINT32 *)(bits + (SIZE_T)y * b->width * 4);
        gp_row_to_argb(b->bits + (SSIZE_T)y * b->stride, b->format, b->palette, 0, b->width, row);
        memcpy(copy + (SSIZE_T)y * b->width, row, (SIZE_T)b->width * 4);
    }
    g->bitmap_old = SelectObject(dc, dib);
    g->bitmap_dc = dc;
    g->bitmap_dib = dib;
    g->bitmap_bits = bits;
    g->bitmap_copy = copy;
    *out = dc;
    return Ok;
}

void gp_graphics_release_bitmap_dc(GGraphics *g, HDC dc, BOOL write_back)
{
    GBitmap *b = g->target;
    if (write_back && b) {
        GdiFlush();
        for (INT y = 0; y < b->height; y++) {
            UINT32 *row = (UINT32 *)(g->bitmap_bits + (SIZE_T)y * b->width * 4);
            UINT32 *orig = g->bitmap_copy + (SSIZE_T)y * b->width;
            BOOL changed = FALSE;
            for (INT x = 0; x < b->width; x++) {
                /* GDI output has no alpha, so it is opaque. */
                if ((row[x] & 0x00ffffffu) != (orig[x] & 0x00ffffffu)) {
                    row[x] |= 0xff000000u;
                    changed = TRUE;
                } else {
                    row[x] = orig[x];
                }
            }
            if (changed)
                gp_row_from_argb(b->bits + (SSIZE_T)y * b->stride, b->format, b->palette, 0, b->width, row);
        }
    }
    SelectObject(g->bitmap_dc, g->bitmap_old);
    DeleteDC(g->bitmap_dc);
    DeleteObject(g->bitmap_dib);
    gp_free(g->bitmap_copy);
    g->bitmap_dc = nullptr;
    g->bitmap_dib = nullptr;
    g->bitmap_bits = nullptr;
    g->bitmap_copy = nullptr;
}
