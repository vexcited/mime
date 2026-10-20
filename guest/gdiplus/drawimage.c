/*
 * drawing images: a rectangle of a picture is mapped onto a parallelogram of the page. The pixels of the
 * parallelogram take their color from the picture through the interpolation mode of the graphics, and the
 * color adjustments of the image attributes.
 */
#include "gp_private.h"

/* ------------------------------------------------------------------ sampling */

typedef struct ImageSource {
    GSource base;
    MimeVecMatrix inv;  /* device pixel to the picture, in pixels of the copied area */
    UINT32 *pixels;     /* premultiplied */
    INT w, h;
    WrapMode wrap;
    ARGB clamp_color;
    InterpolationMode mode;
    BOOL shrink;        /* the area filters apply: the picture is drawn smaller than it is */
    float step_x, step_y; /* the picture pixels one device pixel spans */
} ImageSource;

static INT wrap_axis(INT i, INT n, BOOL flip)
{
    INT q = i / n, r = i % n;
    if (r < 0) {
        r += n;
        q--;
    }
    if (flip && (q & 1))
        r = n - 1 - r;
    return r;
}

static UINT32 texel(const ImageSource *s, INT x, INT y)
{
    if (x >= 0 && y >= 0 && x < s->w && y < s->h)
        return s->pixels[(SSIZE_T)y * s->w + x];
    switch (s->wrap) {
    case WrapModeClamp:
        return gp_premultiply(s->clamp_color);
    case WrapModeTile:
        return s->pixels[(SSIZE_T)wrap_axis(y, s->h, FALSE) * s->w + wrap_axis(x, s->w, FALSE)];
    case WrapModeTileFlipX:
        return s->pixels[(SSIZE_T)wrap_axis(y, s->h, FALSE) * s->w + wrap_axis(x, s->w, TRUE)];
    case WrapModeTileFlipY:
        return s->pixels[(SSIZE_T)wrap_axis(y, s->h, TRUE) * s->w + wrap_axis(x, s->w, FALSE)];
    default:
        return s->pixels[(SSIZE_T)wrap_axis(y, s->h, TRUE) * s->w + wrap_axis(x, s->w, TRUE)];
    }
}

static UINT32 bilerp(UINT32 a, UINT32 b, UINT32 c, UINT32 d, UINT fx, UINT fy)
{
    UINT32 out = 0;
    for (INT sh = 0; sh < 32; sh += 8) {
        UINT va = (a >> sh) & 255, vb = (b >> sh) & 255, vc = (c >> sh) & 255, vd = (d >> sh) & 255;
        UINT top = va * (256 - fx) + vb * fx, bot = vc * (256 - fx) + vd * fx;
        UINT v = (top * (256 - fy) + bot * fy + 32768) >> 16;
        out |= v << sh;
    }
    return out;
}

/* the weights of the four taps of a cubic convolution with the parameter -0.5, as fractions of 1024. */
static void cubic_weights(float t, INT w[4])
{
    float t2 = t * t, t3 = t2 * t;
    float c0 = -0.5f * t3 + t2 - 0.5f * t;
    float c1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    w[0] = (INT)(c0 * 1024.0f + (c0 < 0 ? -0.5f : 0.5f));
    w[1] = (INT)(c1 * 1024.0f + (c1 < 0 ? -0.5f : 0.5f));
    w[2] = (INT)(c2 * 1024.0f + (c2 < 0 ? -0.5f : 0.5f));
    w[3] = 1024 - w[0] - w[1] - w[2];
}

static UINT32 bicubic(const ImageSource *s, INT ix, INT iy, float fx, float fy)
{
    INT wx[4], wy[4];
    cubic_weights(fx, wx);
    cubic_weights(fy, wy);
    INT acc[4] = {0, 0, 0, 0};
    for (INT j = 0; j < 4; j++) {
        for (INT i = 0; i < 4; i++) {
            UINT32 p = texel(s, ix - 1 + i, iy - 1 + j);
            INT w = wx[i] * wy[j];
            acc[0] += (INT)(p & 255) * w;
            acc[1] += (INT)((p >> 8) & 255) * w;
            acc[2] += (INT)((p >> 16) & 255) * w;
            acc[3] += (INT)(p >> 24) * w;
        }
    }
    UINT32 out = 0;
    INT a = (acc[3] + (1 << 19)) >> 20;
    if (a < 0)
        a = 0;
    if (a > 255)
        a = 255;
    for (INT k = 0; k < 3; k++) {
        INT v = (acc[k] + (1 << 19)) >> 20;
        if (v < 0)
            v = 0;
        if (v > a)
            v = a;
        out |= (UINT32)v << (8 * k);
    }
    return out | (UINT32)a << 24;
}

/* the mean of the picture pixels a device pixel covers, for pictures drawn smaller. */
static UINT32 area_average(const ImageSource *s, float u, float v)
{
    float x0 = u - s->step_x * 0.5f, x1 = u + s->step_x * 0.5f, y0 = v - s->step_y * 0.5f, y1 = v + s->step_y * 0.5f;
    INT ix0 = (INT)mime_vec_floorf(x0), ix1 = (INT)mime_vec_ceilf(x1), iy0 = (INT)mime_vec_floorf(y0),
        iy1 = (INT)mime_vec_ceilf(y1);
    float acc[4] = {0, 0, 0, 0}, total = 0;
    for (INT y = iy0; y < iy1; y++) {
        float wy = (y + 1 < y1 ? (float)(y + 1) : y1) - (y > y0 ? (float)y : y0);
        for (INT x = ix0; x < ix1; x++) {
            float wx = (x + 1 < x1 ? (float)(x + 1) : x1) - (x > x0 ? (float)x : x0);
            float w = wx * wy;
            UINT32 p = texel(s, x, y);
            acc[0] += (float)(p & 255) * w;
            acc[1] += (float)((p >> 8) & 255) * w;
            acc[2] += (float)((p >> 16) & 255) * w;
            acc[3] += (float)(p >> 24) * w;
            total += w;
        }
    }
    if (total <= 0.0f)
        return 0;
    UINT32 out = 0;
    for (INT k = 0; k < 4; k++) {
        INT v2 = (INT)(acc[k] / total + 0.5f);
        out |= (UINT32)(v2 > 255 ? 255 : v2) << (8 * k);
    }
    return out;
}

static void image_fetch(GSource *src, INT x, INT y, INT count, UINT32 *out)
{
    ImageSource *s = (ImageSource *)src;
    float u = ((float)x + 0.5f) * s->inv.m11 + ((float)y + 0.5f) * s->inv.m21 + s->inv.dx;
    float v = ((float)x + 0.5f) * s->inv.m12 + ((float)y + 0.5f) * s->inv.m22 + s->inv.dy;
    for (INT i = 0; i < count; i++, u += s->inv.m11, v += s->inv.m12) {
        switch (s->mode) {
        case InterpolationModeNearestNeighbor:
            out[i] = texel(s, (INT)mime_vec_floorf(u), (INT)mime_vec_floorf(v));
            break;
        case InterpolationModeBicubic:
        case InterpolationModeHighQualityBicubic: {
            if (s->shrink) {
                out[i] = area_average(s, u, v);
                break;
            }
            float cu = u - 0.5f, cv = v - 0.5f;
            float fu = mime_vec_floorf(cu), fv = mime_vec_floorf(cv);
            out[i] = bicubic(s, (INT)fu, (INT)fv, cu - fu, cv - fv);
            break;
        }
        case InterpolationModeHighQualityBilinear:
            if (s->shrink) {
                out[i] = area_average(s, u, v);
                break;
            }
            [[fallthrough]];
        default: {
            float cu = u - 0.5f, cv = v - 0.5f;
            float fu = mime_vec_floorf(cu), fv = mime_vec_floorf(cv);
            INT iu = (INT)fu, iv = (INT)fv;
            UINT wx = (UINT)((cu - fu) * 256.0f), wy = (UINT)((cv - fv) * 256.0f);
            out[i] = bilerp(texel(s, iu, iv), texel(s, iu + 1, iv), texel(s, iu, iv + 1), texel(s, iu + 1, iv + 1), wx, wy);
            break;
        }
        }
    }
}

static void image_destroy(GSource *src)
{
    ImageSource *s = (ImageSource *)src;
    gp_free(s->pixels);
    gp_free(s);
}

/* ------------------------------------------------------------------ units */

static float unit_inches(GpUnit unit)
{
    switch (unit) {
    case UnitInch:
        return 1.0f;
    case UnitPoint:
        return 1.0f / 72.0f;
    case UnitDocument:
        return 1.0f / 300.0f;
    case UnitMillimeter:
        return 1.0f / 25.4f;
    case UnitDisplay:
        return 1.0f / 75.0f;
    default:
        return 0.0f;
    }
}

/* ------------------------------------------------------------------ the draw call */

GpStatus gp_draw_image(GGraphics *g, GImage *image, const GpPointF *dst, INT dst_count, float sx, float sy, float sw,
                       float sh, GpUnit src_unit, struct GAttrs *attrs)
{
    if (!g || !image || !dst || dst_count != 3)
        return InvalidParameter;
    if (g->busy)
        return ObjectBusy;
    GBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return gp_metafile_draw(g, image, dst, sx, sy, sw, sh, src_unit, attrs);
    if (b->locked && b->lock_flags & ImageLockModeWrite)
        return ObjectBusy;
    if (src_unit == UnitWorld || src_unit == UnitDisplay) {
        /* the display unit is 1/75 inch, the world unit is not defined for pictures and counts as pixels */
        if (src_unit == UnitWorld)
            src_unit = UnitPixel;
    }
    if (src_unit != UnitPixel) {
        float k = unit_inches(src_unit);
        if (k == 0.0f)
            return InvalidParameter;
        float kx = k * b->dpi_x, ky = k * b->dpi_y;
        sx *= kx;
        sw *= kx;
        sy *= ky;
        sh *= ky;
    }
    if (!(sw > 0.0f) || !(sh > 0.0f))
        return Ok;

    /* the area of the picture that can be seen, rounded outwards */
    INT ax0 = (INT)mime_vec_floorf(sx), ay0 = (INT)mime_vec_floorf(sy);
    INT ax1 = (INT)mime_vec_ceilf(sx + sw), ay1 = (INT)mime_vec_ceilf(sy + sh);
    INT cx0 = ax0 < 0 ? 0 : ax0, cy0 = ay0 < 0 ? 0 : ay0;
    INT cx1 = ax1 > b->width ? b->width : ax1, cy1 = ay1 > b->height ? b->height : ay1;
    if (cx1 <= cx0 || cy1 <= cy0)
        return Ok;
    /* the pixels outside the source rectangle are not read: the wrap mode decides what lies there */
    sx -= (float)cx0;
    sy -= (float)cy0;

    ImageSource *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return OutOfMemory;
    s->w = cx1 - cx0;
    s->h = cy1 - cy0;
    s->pixels = gp_alloc((SIZE_T)sizeof(UINT32) * s->w * s->h);
    if (!s->pixels) {
        gp_free(s);
        return OutOfMemory;
    }
    for (INT y = 0; y < s->h; y++) {
        UINT32 *row = s->pixels + (SSIZE_T)y * s->w;
        gp_bitmap_read_argb(b, cx0, cy0 + y, s->w, row);
        if (attrs && gp_attrs_active(attrs, ColorAdjustTypeBitmap))
            gp_attrs_apply(attrs, ColorAdjustTypeBitmap, row, s->w);
        for (INT x = 0; x < s->w; x++)
            row[x] = gp_premultiply(row[x]);
    }
    s->wrap = gp_attrs_wrap(attrs, &s->clamp_color);
    s->mode = g->interpolation;
    s->base.fetch = image_fetch;
    s->base.destroy = image_destroy;

    /* picture pixel to device: the source rectangle onto the parallelogram, then the transform of the page */
    const MimeVecMatrix *w2d = gp_graphics_w2d(g);
    MimeVecMatrix to_world;
    float ex = (dst[1].X - dst[0].X) / sw, ey = (dst[1].Y - dst[0].Y) / sw;
    float fx = (dst[2].X - dst[0].X) / sh, fy = (dst[2].Y - dst[0].Y) / sh;
    to_world.m11 = ex;
    to_world.m12 = ey;
    to_world.m21 = fx;
    to_world.m22 = fy;
    to_world.dx = dst[0].X - sx * ex - sy * fx;
    to_world.dy = dst[0].Y - sx * ey - sy * fy;
    MimeVecMatrix to_dev, inv;
    mime_vec_matrix_multiply(&to_dev, &to_world, w2d);
    if (!mime_vec_matrix_invert(&inv, &to_dev)) {
        gp_free(s->pixels);
        gp_free(s);
        return Ok;
    }
    s->inv = inv;
    s->step_x = mime_vec_sqrtf(inv.m11 * inv.m11 + inv.m12 * inv.m12);
    s->step_y = mime_vec_sqrtf(inv.m21 * inv.m21 + inv.m22 * inv.m22);
    s->shrink = (s->step_x > 1.0f || s->step_y > 1.0f) && inv.m12 == 0.0f && inv.m21 == 0.0f;
    if (s->step_x < 1.0f)
        s->step_x = 1.0f;
    if (s->step_y < 1.0f)
        s->step_y = 1.0f;

    /* the parallelogram in device space */
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    float px[4] = {dst[0].X, dst[1].X, dst[1].X + dst[2].X - dst[0].X, dst[2].X};
    float py[4] = {dst[0].Y, dst[1].Y, dst[1].Y + dst[2].Y - dst[0].Y, dst[2].Y};
    GpStatus st = Ok;
    mime_vec_polys_begin(&polys);
    for (INT i = 0; i < 4; i++) {
        float dx, dy;
        gp_matrix_point(&w2d->m11, px[i], py[i], &dx, &dy);
        if (mime_vec_polys_add(&polys, dx, dy) != MIME_VEC_OK)
            st = OutOfMemory;
    }
    mime_vec_polys_end(&polys, true);
    if (st == Ok) {
        MimeVecBounds bb;
        if (mime_vec_polys_bounds(&polys, &bb)) {
            RECT area = {(INT)mime_vec_floorf(bb.left) - 1, (INT)mime_vec_floorf(bb.top) - 1,
                         (INT)mime_vec_ceilf(bb.right) + 2, (INT)mime_vec_ceilf(bb.bottom) + 2};
            st = gp_fill_polys_src(g, &polys, MIME_VEC_FILL_WINDING, 0, &s->base, &area);
        }
    }
    mime_vec_polys_free(&polys);
    gp_source_free(&s->base);
    return st;
}

/* ------------------------------------------------------------------ the api */

/* the size an image has on a page: its physical size in the units of the page. */
static void natural_size(GGraphics *g, GImage *image, float *w, float *h)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (b) {
        float inch = gp_graphics_unit_to_world(g, UnitInch);
        *w = (float)b->width / b->dpi_x * inch;
        *h = (float)b->height / b->dpi_y * inch;
        return;
    }
    float iw = 0, ih = 0;
    gp_metafile_dimension(image, &iw, &ih);
    *w = iw;
    *h = ih;
}

static void rect_points(float x, float y, float w, float h, GpPointF *p)
{
    p[0].X = x;
    p[0].Y = y;
    p[1].X = x + w;
    p[1].Y = y;
    p[2].X = x;
    p[2].Y = y + h;
}

static GpStatus image_dimensions(GImage *image, float *w, float *h, GpUnit *unit)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (b) {
        *w = (float)b->width;
        *h = (float)b->height;
        *unit = UnitPixel;
        return Ok;
    }
    GpRectF r;
    GpStatus st = gp_metafile_bounds(image, &r, unit);
    if (st == Ok) {
        *w = r.Width;
        *h = r.Height;
    }
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImage(GpGraphics *graphics, GpImage *image, REAL x, REAL y)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *i = gp_image_cast(image);
    if (!g || !i)
        return InvalidParameter;
    float w, h;
    natural_size(g, i, &w, &h);
    GpPointF p[3];
    rect_points(x, y, w, h, p);
    float sw, sh;
    GpUnit unit;
    GpStatus st = image_dimensions(i, &sw, &sh, &unit);
    if (st != Ok)
        return st;
    return gp_draw_image(g, i, p, 3, 0, 0, sw, sh, unit, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageI(GpGraphics *graphics, GpImage *image, INT x, INT y)
{
    return GdipDrawImage(graphics, image, (float)x, (float)y);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRect(GpGraphics *graphics, GpImage *image, REAL x, REAL y, REAL w, REAL h)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *i = gp_image_cast(image);
    if (!g || !i)
        return InvalidParameter;
    GpPointF p[3];
    rect_points(x, y, w, h, p);
    float sw, sh;
    GpUnit unit;
    GpStatus st = image_dimensions(i, &sw, &sh, &unit);
    if (st != Ok)
        return st;
    return gp_draw_image(g, i, p, 3, 0, 0, sw, sh, unit, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT w, INT h)
{
    return GdipDrawImageRect(graphics, image, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePoints(GpGraphics *graphics, GpImage *image, const GpPointF *points, INT count)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *i = gp_image_cast(image);
    if (!g || !i || !points)
        return InvalidParameter;
    if (count != 3)
        return count == 4 ? NotImplemented : InvalidParameter;
    float sw, sh;
    GpUnit unit;
    GpStatus st = image_dimensions(i, &sw, &sh, &unit);
    if (st != Ok)
        return st;
    return gp_draw_image(g, i, points, 3, 0, 0, sw, sh, unit, nullptr);
}

static GpStatus points_from_int(const GpPoint *points, INT count, GpPointF *out)
{
    if (!points || count != 3)
        return count == 4 ? NotImplemented : InvalidParameter;
    for (INT k = 0; k < 3; k++) {
        out[k].X = (float)points[k].X;
        out[k].Y = (float)points[k].Y;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsI(GpGraphics *graphics, GpImage *image, const GpPoint *points, INT count)
{
    GpPointF p[3];
    GpStatus st = points_from_int(points, count, p);
    if (st != Ok)
        return st;
    return GdipDrawImagePoints(graphics, image, p, 3);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRect(GpGraphics *graphics, GpImage *image, REAL x, REAL y, REAL sx,
                                                  REAL sy, REAL sw, REAL sh, GpUnit unit)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *i = gp_image_cast(image);
    if (!g || !i)
        return InvalidParameter;
    /* the destination is the source rectangle at its size on the page */
    GBitmap *b = gp_bitmap_cast(i);
    float w = sw, h = sh;
    if (b) {
        float pw = sw, ph = sh;
        if (unit != UnitPixel && unit != UnitWorld) {
            float k = unit_inches(unit);
            if (k == 0.0f)
                return InvalidParameter;
            pw = sw * k * b->dpi_x;
            ph = sh * k * b->dpi_y;
        }
        float inch = gp_graphics_unit_to_world(g, UnitInch);
        w = pw / b->dpi_x * inch;
        h = ph / b->dpi_y * inch;
    }
    GpPointF p[3];
    rect_points(x, y, w, h, p);
    return gp_draw_image(g, i, p, 3, sx, sy, sw, sh, unit, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT sx, INT sy,
                                                   INT sw, INT sh, GpUnit unit)
{
    return GdipDrawImagePointRect(graphics, image, (float)x, (float)y, (float)sx, (float)sy, (float)sw, (float)sh, unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRect(GpGraphics *graphics, GpImage *image, REAL dx, REAL dy, REAL dw,
                                                 REAL dh, REAL sx, REAL sy, REAL sw, REAL sh, GpUnit unit,
                                                 const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *i = gp_image_cast(image);
    if (!g || !i)
        return InvalidParameter;
    if (attrs && !gp_attrs_cast(attrs))
        return InvalidParameter;
    GpPointF p[3];
    rect_points(dx, dy, dw, dh, p);
    return gp_draw_image(g, i, p, 3, sx, sy, sw, sh, unit, (struct GAttrs *)gp_attrs_cast(attrs));
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image, INT dx, INT dy, INT dw, INT dh,
                                                  INT sx, INT sy, INT sw, INT sh, GpUnit unit,
                                                  const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    return GdipDrawImageRectRect(graphics, image, (float)dx, (float)dy, (float)dw, (float)dh, (float)sx, (float)sy,
                                 (float)sw, (float)sh, unit, attrs, abort, data);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRect(GpGraphics *graphics, GpImage *image, const GpPointF *points,
                                                   INT count, REAL sx, REAL sy, REAL sw, REAL sh, GpUnit unit,
                                                   const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *i = gp_image_cast(image);
    if (!g || !i || !points)
        return InvalidParameter;
    if (count != 3)
        return count == 4 ? NotImplemented : InvalidParameter;
    if (attrs && !gp_attrs_cast(attrs))
        return InvalidParameter;
    return gp_draw_image(g, i, points, 3, sx, sy, sw, sh, unit, (struct GAttrs *)gp_attrs_cast(attrs));
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRectI(GpGraphics *graphics, GpImage *image, const GpPoint *points,
                                                    INT count, INT sx, INT sy, INT sw, INT sh, GpUnit unit,
                                                    const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    GpPointF p[3];
    GpStatus st = points_from_int(points, count, p);
    if (st != Ok)
        return st;
    return GdipDrawImagePointsRect(graphics, image, p, 3, (float)sx, (float)sy, (float)sw, (float)sh, unit, attrs, abort,
                                   data);
}
