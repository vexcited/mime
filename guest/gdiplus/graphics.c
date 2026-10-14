/*
 * graphics: drawing bitmaps onto a device context or into another bitmap.
 * a device context target gets a 32 bpp premultiplied dib section blended
 * by gdi32. a bitmap target is drawn in software straight into its pixels,
 * so no gdi object mirrors it.
 */
#include "gdiplus_private.h"

static GpGraphics *graphics_cast(GpGraphics *graphics)
{
    return graphics && graphics->Magic == GP_GRAPHICS_MAGIC ? graphics : NULL;
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
    *graphics = g;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics)
{
    if (!graphics)
        return InvalidParameter;
    if (!hdc)
        return OutOfMemory;
    return graphics_create(hdc, NULL, graphics);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !graphics)
        return InvalidParameter;
    return graphics_create(NULL, b, graphics);
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
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = src->Width;
    info.bmiHeader.biHeight = -src->Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = NULL;
    HBITMAP dib = CreateDIBSection(g->Hdc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
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
    /* a negative size mirrors the image. */
    BOOL flip_x = width < 0, flip_y = height < 0;
    if (flip_x) {
        x += width;
        width = -width;
    }
    if (flip_y) {
        y += height;
        height = -height;
    }
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
