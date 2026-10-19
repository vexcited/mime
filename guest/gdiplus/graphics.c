/*
 * graphics: drawing bitmaps onto a device context or into another bitmap.
 * a device context target gets a 32 bpp premultiplied dib section blended
 * by gdi32. a bitmap target is drawn in software straight into its pixels,
 * so no gdi object mirrors it.
 */
#include "gdiplus_private.h"

static GpStatus device_rect(GpGraphics *g, float x, float y, float width, float height, INT *left, INT *top,
                            INT *right, INT *bottom);

static GpGraphics *graphics_cast(GpGraphics *graphics)
{
    return graphics && graphics->Magic == GP_GRAPHICS_MAGIC ? graphics : nullptr;
}

static GpStatus graphics_create(HDC hdc, GpBitmap *target, GpGraphics **graphics)
{
    GpGraphics *g = gp_alloc_zero(sizeof(*g));
    if (!g)
        return OutOfMemory;
    g->Magic = GP_GRAPHICS_MAGIC;
    g->Hdc = hdc;
    g->Target = target;
    g->Interpolation = InterpolationModeBilinear;
    g->Smoothing = SmoothingModeNone;
    g->Compositing = CompositingModeSourceOver;
    g->CompositingQuality = CompositingQualityDefault;
    g->PixelOffset = PixelOffsetModeNone;
    g->PageUnit = UnitPixel;
    g->PageScale = 1.0f;
    g->World[0] = g->World[3] = 1.0f;
    *graphics = g;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics)
{
    if (!graphics)
        return InvalidParameter;
    if (!hdc)
        return OutOfMemory;
    return graphics_create(hdc, nullptr, graphics);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !graphics)
        return InvalidParameter;
    return graphics_create(nullptr, b, graphics);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    g->Magic = 0;
    gp_free(g);
    return Ok;
}

/* the quality modes resolve to the mode they stand for, like gdi+. */
DECLSPEC_EXPORT GpStatus WINAPI GdipSetInterpolationMode(GpGraphics *graphics, InterpolationMode mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || mode <= InterpolationModeInvalid || mode > InterpolationModeHighQualityBicubic)
        return InvalidParameter;
    if (mode == InterpolationModeDefault || mode == InterpolationModeLowQuality)
        mode = InterpolationModeBilinear;
    else if (mode == InterpolationModeHighQuality)
        mode = InterpolationModeHighQualityBicubic;
    g->Interpolation = mode;
    return Ok;
}

/* ------------------------------------------------------------------ drawing */

/* n / d for a quotient below 2^32, without the 64 bit division helpers x86 lacks here. */
static UINT divide(ULONGLONG n, ULONGLONG d)
{
    UINT q = 0;
    for (int bit = 31; bit >= 0; bit--) {
        if ((n >> bit) >= d) {
            n -= d << bit;
            q |= 1u << bit;
        }
    }
    return q;
}

/* the source pixel sampled by destination pixel i of count, for a source of size pixels. */
static INT source_index(INT i, INT count, INT size, BOOL flip)
{
    UINT s = divide(((ULONGLONG)(UINT)i * 2 + 1) * (UINT)size, (ULONGLONG)(UINT)count * 2);
    if (s >= (UINT)size)
        s = (UINT)size - 1;
    return flip ? size - 1 - (INT)s : (INT)s;
}

/* nearest neighbor scaling, composited source over destination. */
static GpStatus draw_to_bitmap(GpBitmap *dst, GpBitmap *src, INT x, INT y, INT width, INT height, BOOL flip_x,
                               BOOL flip_y)
{
    LONGLONG left = x > 0 ? x : 0, top = y > 0 ? y : 0;
    LONGLONG right = (LONGLONG)x + width, bottom = (LONGLONG)y + height;
    if (right > dst->Width)
        right = dst->Width;
    if (bottom > dst->Height)
        bottom = dst->Height;
    if (left >= right || top >= bottom)
        return Ok;
    INT columns = (INT)(right - left);
    INT *map = gp_alloc((SIZE_T)columns * sizeof(INT));
    if (!map)
        return OutOfMemory;
    for (INT i = 0; i < columns; i++)
        map[i] = source_index((INT)left + i - x, width, src->Width, flip_x);
    for (INT row = (INT)top; row < (INT)bottom; row++) {
        INT sy = source_index(row - y, height, src->Height, flip_y);
        const BYTE *s = src->Bits + (INT_PTR)sy * src->Stride;
        BYTE *d = dst->Bits + (INT_PTR)row * dst->Stride;
        for (INT i = 0; i < columns; i++) {
            INT dx = (INT)left + i;
            ARGB c = gp_read_pixel(s, map[i], src->Format);
            UINT alpha = c >> 24;
            if (!alpha)
                continue;
            if (alpha != 255) {
                ARGB sp = gp_premultiply(c), dp = gp_premultiply(gp_read_pixel(d, dx, dst->Format));
                UINT inverse = 255 - alpha;
                c = 0;
                for (UINT shift = 0; shift < 32; shift += 8)
                    c |= ((sp >> shift & 0xff) + ((dp >> shift & 0xff) * inverse + 127) / 255) << shift;
                c = gp_unpremultiply(c);
            }
            gp_write_pixel(d, dx, dst->Format, c);
        }
    }
    gp_free(map);
    return Ok;
}

/*
 * copies the source into a premultiplied top-down dib section, then lets
 * gdi32 stretch it: StretchBlt for opaque pixels, GdiAlphaBlend otherwise.
 * nearest neighbor mode uses COLORONCOLOR stretching, the others HALFTONE.
 */
static GpStatus draw_to_hdc(GpGraphics *g, GpBitmap *src, INT x, INT y, INT width, INT height, BOOL flip_x,
                            BOOL flip_y)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = src->Width;
    info.bmiHeader.biHeight = -src->Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HBITMAP dib = CreateDIBSection(g->Hdc, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib)
            DeleteObject(dib);
        return OutOfMemory;
    }
    BOOL opaque = TRUE;
    for (INT row = 0; row < src->Height; row++) {
        const BYTE *s = src->Bits + (INT_PTR)(flip_y ? src->Height - 1 - row : row) * src->Stride;
        BYTE *d = bits + (SIZE_T)row * src->Width * 4;
        for (INT col = 0; col < src->Width; col++) {
            ARGB c = gp_premultiply(gp_read_pixel(s, flip_x ? src->Width - 1 - col : col, src->Format));
            if (c >> 24 != 255)
                opaque = FALSE;
            d[col * 4] = (BYTE)c;
            d[col * 4 + 1] = (BYTE)(c >> 8);
            d[col * 4 + 2] = (BYTE)(c >> 16);
            d[col * 4 + 3] = (BYTE)(c >> 24);
        }
    }
    GpStatus status = OutOfMemory;
    HDC mem = CreateCompatibleDC(g->Hdc);
    if (mem) {
        HGDIOBJ old = SelectObject(mem, dib);
        int mode = SetStretchBltMode(g->Hdc, g->Interpolation == InterpolationModeNearestNeighbor ? COLORONCOLOR
                                                                                                  : HALFTONE);
        BOOL done;
        if (opaque) {
            done = StretchBlt(g->Hdc, x, y, width, height, mem, 0, 0, src->Width, src->Height, SRCCOPY);
        } else {
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            done = GdiAlphaBlend(g->Hdc, x, y, width, height, mem, 0, 0, src->Width, src->Height, blend);
        }
        if (mode)
            SetStretchBltMode(g->Hdc, mode);
        SelectObject(mem, old);
        DeleteDC(mem);
        status = done ? Ok : GenericError;
    }
    DeleteObject(dib);
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width,
                                                   INT height)
{
    GpGraphics *g = graphics_cast(graphics);
    GpBitmap *src = gp_bitmap_cast(image);
    if (!g || !src)
        return InvalidParameter;
    if (!width || !height)
        return Ok;
    if (width < -0x7fffffff || height < -0x7fffffff)
        return InvalidParameter;
    /* a negative size mirrors the image. The rectangle goes through the world and page transforms to pixels. */
    BOOL flip_x = width < 0, flip_y = height < 0;
    INT left, top, right, bottom;
    GpStatus status = device_rect(g, (float)x, (float)y, (float)width, (float)height, &left, &top, &right, &bottom);
    if (status != Ok)
        return status;
    x = left;
    y = top;
    width = right - left;
    height = bottom - top;
    if (!width || !height)
        return Ok;
    if (g->Target)
        return draw_to_bitmap(g->Target, src, x, y, width, height, flip_x, flip_y);
    return draw_to_hdc(g, src, x, y, width, height, flip_x, flip_y);
}

/* the image is drawn at its pixel size; its resolution is not taken into account. */
DECLSPEC_EXPORT GpStatus WINAPI GdipDrawImageI(GpGraphics *graphics, GpImage *image, INT x, INT y)
{
    GpBitmap *src = gp_bitmap_cast(image);
    if (!src)
        return InvalidParameter;
    return GdipDrawImageRectI(graphics, image, x, y, src->Width, src->Height);
}

/* ------------------------------------------------------------------ state */

DECLSPEC_EXPORT GpStatus WINAPI GdipGetInterpolationMode(GpGraphics *graphics, InterpolationMode *mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !mode)
        return InvalidParameter;
    *mode = g->Interpolation;
    return Ok;
}

/* the smoothing modes are hints; they are kept, and nothing here draws with anti aliasing. */
DECLSPEC_EXPORT GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, SmoothingMode mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || mode <= SmoothingModeInvalid || mode > SmoothingModeAntiAlias8x8)
        return InvalidParameter;
    g->Smoothing = mode == SmoothingModeDefault ? SmoothingModeNone : mode == SmoothingModeHighSpeed
                                                                       ? SmoothingModeNone
                                                                       : mode == SmoothingModeHighQuality
                                                                             ? SmoothingModeAntiAlias8x4
                                                                             : mode;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetSmoothingMode(GpGraphics *graphics, SmoothingMode *mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !mode)
        return InvalidParameter;
    *mode = g->Smoothing;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetCompositingMode(GpGraphics *graphics, CompositingMode mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || (UINT)mode > CompositingModeSourceCopy)
        return InvalidParameter;
    g->Compositing = mode;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetCompositingMode(GpGraphics *graphics, CompositingMode *mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !mode)
        return InvalidParameter;
    *mode = g->Compositing;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetCompositingQuality(GpGraphics *graphics, CompositingQuality quality)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || quality <= CompositingQualityInvalid || quality > CompositingQualityAssumeLinear)
        return InvalidParameter;
    g->CompositingQuality = quality;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetCompositingQuality(GpGraphics *graphics, CompositingQuality *quality)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !quality)
        return InvalidParameter;
    *quality = g->CompositingQuality;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPixelOffsetMode(GpGraphics *graphics, PixelOffsetMode mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || mode <= PixelOffsetModeInvalid || mode > PixelOffsetModeHalf)
        return InvalidParameter;
    g->PixelOffset = mode;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPixelOffsetMode(GpGraphics *graphics, PixelOffsetMode *mode)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !mode)
        return InvalidParameter;
    *mode = g->PixelOffset;
    return Ok;
}

/* the world unit cannot be a page unit, and a scale must be positive. */
DECLSPEC_EXPORT GpStatus WINAPI GdipSetPageUnit(GpGraphics *graphics, GpUnit unit)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || unit <= UnitWorld || unit > UnitMillimeter)
        return InvalidParameter;
    g->PageUnit = unit;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPageUnit(GpGraphics *graphics, GpUnit *unit)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !unit)
        return InvalidParameter;
    *unit = g->PageUnit;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPageScale(GpGraphics *graphics, float scale)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !(scale > 0))
        return InvalidParameter;
    g->PageScale = scale;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPageScale(GpGraphics *graphics, float *scale)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !scale)
        return InvalidParameter;
    *scale = g->PageScale;
    return Ok;
}

/* ------------------------------------------------------------------ world transform */

DECLSPEC_EXPORT GpStatus WINAPI GdipSetWorldTransform(GpGraphics *graphics, GpMatrix *matrix)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !matrix || matrix->Magic != GP_MATRIX_MAGIC)
        return InvalidParameter;
    /* a matrix that cannot be inverted is refused, as the transform has to map back. */
    float det = matrix->M[0] * matrix->M[3] - matrix->M[1] * matrix->M[2];
    if (det < 1.0e-6f && det > -1.0e-6f)
        return InvalidParameter;
    memcpy(g->World, matrix->M, sizeof(g->World));
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetWorldTransform(GpGraphics *graphics, GpMatrix *matrix)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !matrix || matrix->Magic != GP_MATRIX_MAGIC)
        return InvalidParameter;
    memcpy(matrix->M, g->World, sizeof(g->World));
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipResetWorldTransform(GpGraphics *graphics)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    static const float identity[6] = {1, 0, 0, 1, 0, 0};
    memcpy(g->World, identity, sizeof(identity));
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipMultiplyWorldTransform(GpGraphics *graphics, const GpMatrix *matrix,
                                                           MatrixOrder order)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !matrix || matrix->Magic != GP_MATRIX_MAGIC || (order != MatrixOrderPrepend && order != MatrixOrderAppend))
        return InvalidParameter;
    if (order == MatrixOrderPrepend)
        gp_matrix_multiply(matrix->M, g->World, g->World);
    else
        gp_matrix_multiply(g->World, matrix->M, g->World);
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipTranslateWorldTransform(GpGraphics *graphics, float dx, float dy,
                                                            MatrixOrder order)
{
    GpMatrix m = {GP_MATRIX_MAGIC, {1, 0, 0, 1, dx, dy}};
    return GdipMultiplyWorldTransform(graphics, &m, order);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipScaleWorldTransform(GpGraphics *graphics, float sx, float sy, MatrixOrder order)
{
    GpMatrix m = {GP_MATRIX_MAGIC, {sx, 0, 0, sy, 0, 0}};
    return GdipMultiplyWorldTransform(graphics, &m, order);
}

/* ------------------------------------------------------------------ filling */

static INT round_to_int(float v)
{
    float t = (float)(long long)v;
    if (t > v)
        t -= 1;
    return (INT)(t + 0.5f > v ? t : t + 1);
}

/* the pixels per unit of the page, from the resolution of the device. */
static float unit_scale(GpGraphics *g)
{
    float dpi = 96.0f;
    if (g->Target)
        dpi = g->Target->DpiX;
    else if (g->Hdc)
        dpi = (float)GetDeviceCaps(g->Hdc, LOGPIXELSX);
    float scale;
    switch (g->PageUnit) {
    case UnitDisplay:
        scale = dpi / 75.0f;
        break;
    case UnitPoint:
        scale = dpi / 72.0f;
        break;
    case UnitInch:
        scale = dpi;
        break;
    case UnitDocument:
        scale = dpi / 300.0f;
        break;
    case UnitMillimeter:
        scale = dpi / 25.4f;
        break;
    default:
        scale = 1.0f;
        break;
    }
    return scale * g->PageScale;
}

/*
 * the rectangle in pixels, from the world through the page. A transform with a rotation or a shear turns the
 * rectangle into a parallelogram, which this library has no means of filling.
 */
static GpStatus device_rect(GpGraphics *g, float x, float y, float width, float height, INT *left, INT *top,
                            INT *right, INT *bottom)
{
    if (g->World[1] != 0 || g->World[2] != 0)
        return NotImplemented;
    float scale = unit_scale(g);
    float x0 = (x * g->World[0] + g->World[4]) * scale, x1 = ((x + width) * g->World[0] + g->World[4]) * scale;
    float y0 = (y * g->World[3] + g->World[5]) * scale, y1 = ((y + height) * g->World[3] + g->World[5]) * scale;
    *left = round_to_int(x0 < x1 ? x0 : x1);
    *right = round_to_int(x0 < x1 ? x1 : x0);
    *top = round_to_int(y0 < y1 ? y0 : y1);
    *bottom = round_to_int(y0 < y1 ? y1 : y0);
    return Ok;
}

static ARGB over(ARGB source, ARGB destination)
{
    UINT alpha = source >> 24;
    if (alpha == 255)
        return source;
    if (!alpha)
        return destination;
    ARGB sp = gp_premultiply(source), dp = gp_premultiply(destination);
    UINT inverse = 255 - alpha;
    ARGB c = 0;
    for (UINT shift = 0; shift < 32; shift += 8)
        c |= ((sp >> shift & 0xff) + ((dp >> shift & 0xff) * inverse + 127) / 255) << shift;
    return gp_unpremultiply(c);
}

/* the color over a rectangle of device pixels: composed over what is there, or replacing it for source copy. */
GpStatus gp_fill_rect(GpGraphics *g, ARGB color, INT left, INT top, INT right, INT bottom)
{
    if (left >= right || top >= bottom)
        return Ok;
    if (g->Target) {
        GpBitmap *b = g->Target;
        if (left < 0)
            left = 0;
        if (top < 0)
            top = 0;
        if (right > b->Width)
            right = b->Width;
        if (bottom > b->Height)
            bottom = b->Height;
        for (INT y = top; y < bottom; y++) {
            BYTE *row = b->Bits + (INT_PTR)y * b->Stride;
            for (INT x = left; x < right; x++)
                gp_write_pixel(row, x, b->Format,
                               g->Compositing == CompositingModeSourceCopy ? color
                                                                           : over(color, gp_read_pixel(row, x, b->Format)));
        }
        return Ok;
    }
    UINT alpha = color >> 24;
    if (!alpha)
        return Ok;
    if (alpha == 255 || g->Compositing == CompositingModeSourceCopy) {
        HBRUSH brush = CreateSolidBrush(RGB(color >> 16 & 0xff, color >> 8 & 0xff, color & 0xff));
        if (!brush)
            return OutOfMemory;
        RECT r = {left, top, right, bottom};
        BOOL ok = FillRect(g->Hdc, &r, brush);
        DeleteObject(brush);
        return ok ? Ok : GenericError;
    }
    /* a translucent color is one premultiplied pixel stretched over the rectangle and blended. */
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HBITMAP dib = CreateDIBSection(g->Hdc, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    HDC mem = dib ? CreateCompatibleDC(g->Hdc) : nullptr;
    GpStatus status = OutOfMemory;
    if (mem && bits) {
        ARGB p = gp_premultiply(color);
        bits[0] = (BYTE)p;
        bits[1] = (BYTE)(p >> 8);
        bits[2] = (BYTE)(p >> 16);
        bits[3] = (BYTE)(p >> 24);
        HGDIOBJ old = SelectObject(mem, dib);
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        status = GdiAlphaBlend(g->Hdc, left, top, right - left, bottom - top, mem, 0, 0, 1, 1, blend) ? Ok
                                                                                                    : GenericError;
        SelectObject(mem, old);
    }
    if (mem)
        DeleteDC(mem);
    if (dib)
        DeleteObject(dib);
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipFillRectangle(GpGraphics *graphics, GpBrush *brush, float x, float y, float width,
                                                  float height)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !brush || brush->Magic != GP_BRUSH_MAGIC)
        return InvalidParameter;
    INT left, top, right, bottom;
    GpStatus status = device_rect(g, x, y, width, height, &left, &top, &right, &bottom);
    return status == Ok ? gp_fill_rect(g, brush->Color, left, top, right, bottom) : status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipFillRectangleI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT width,
                                                   INT height)
{
    return GdipFillRectangle(graphics, brush, (float)x, (float)y, (float)width, (float)height);
}

/*
 * the outline of a rectangle is four strips as wide as the pen, centered on the path: a one pixel pen on a whole
 * number rectangle covers its sides and its far corner. Dashes and the other dash styles are not drawn.
 */
DECLSPEC_EXPORT GpStatus WINAPI GdipDrawRectangle(GpGraphics *graphics, GpPen *pen, float x, float y, float width,
                                                  float height)
{
    GpGraphics *g = graphics_cast(graphics);
    if (!g || !pen || pen->Magic != GP_PEN_MAGIC)
        return InvalidParameter;
    if (pen->Dash != DashStyleSolid)
        return NotImplemented;
    float w = pen->Width * (pen->Unit == UnitWorld || pen->Unit == UnitPixel ? 1.0f : unit_scale(g));
    if (pen->Unit == UnitWorld && g->World[1] == 0 && g->World[2] == 0)
        w = pen->Width * g->World[0] * unit_scale(g);
    float half = w / 2;
    INT ol, ot, or_, ob, il, it, ir, ib;
    GpStatus status = device_rect(g, x - half, y - half, width + w, height + w, &ol, &ot, &or_, &ob);
    if (status != Ok)
        return status;
    status = device_rect(g, x + half, y + half, width - w, height - w, &il, &it, &ir, &ib);
    if (status != Ok)
        return status;
    ARGB color = pen->Brush->Color;
    if (il >= ir || it >= ib)
        return gp_fill_rect(g, color, ol, ot, or_, ob);
    status = gp_fill_rect(g, color, ol, ot, or_, it);
    if (status == Ok)
        status = gp_fill_rect(g, color, ol, ib, or_, ob);
    if (status == Ok)
        status = gp_fill_rect(g, color, ol, it, il, ib);
    if (status == Ok)
        status = gp_fill_rect(g, color, ir, it, or_, ib);
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDrawRectangleI(GpGraphics *graphics, GpPen *pen, INT x, INT y, INT width,
                                                   INT height)
{
    return GdipDrawRectangle(graphics, pen, (float)x, (float)y, (float)width, (float)height);
}

/* ------------------------------------------------------------------ drawing part of an image */

/* the source rectangle in pixels; only pixel units name one here. */
static GpStatus source_pixels(GpBitmap *src, GpUnit unit, INT x, INT y, INT width, INT height, GpRect *rect)
{
    if (unit != UnitPixel)
        return NotImplemented;
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > src->Width - width || y > src->Height - height)
        return InvalidParameter;
    rect->X = x;
    rect->Y = y;
    rect->Width = width;
    rect->Height = height;
    return Ok;
}

/* a view of the rectangle of a bitmap, sharing its pixels. */
static GpBitmap view_of(const GpBitmap *src, const GpRect *r)
{
    GpBitmap view = *src;
    view.Width = r->Width;
    view.Height = r->Height;
    view.Bits = src->Bits + (INT_PTR)r->Y * src->Stride + (INT_PTR)r->X * (GetPixelFormatSize(src->Format) / 8);
    view.Owned = nullptr;
    view.LockBuffer = nullptr;
    view.Properties = nullptr;
    view.PropertyCount = 0;
    return view;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image, INT dst_x, INT dst_y,
                                                       INT dst_width, INT dst_height, INT src_x, INT src_y,
                                                       INT src_width, INT src_height, GpUnit src_unit,
                                                       void *attributes, void *callback, void *data)
{
    GpGraphics *g = graphics_cast(graphics);
    GpBitmap *src = gp_bitmap_cast(image);
    if (!g || !src)
        return InvalidParameter;
    GpRect rect;
    GpStatus status = source_pixels(src, src_unit, src_x, src_y, src_width, src_height, &rect);
    if (status != Ok)
        return status;
    GpBitmap view = view_of(src, &rect);
    return GdipDrawImageRectI(graphics, &view.Image, dst_x, dst_y, dst_width, dst_height);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDrawImagePointRectI(GpGraphics *graphics, GpImage *image, INT x, INT y,
                                                        INT src_x, INT src_y, INT src_width, INT src_height,
                                                        GpUnit src_unit)
{
    return GdipDrawImageRectRectI(graphics, image, x, y, src_width, src_height, src_x, src_y, src_width, src_height,
                                  src_unit, nullptr, nullptr, nullptr);
}
