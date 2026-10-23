/*
 * bitmaps in any gdi+ pixel format: creation, copies, LockBits, and the palettes of the indexed formats.
 */
#include "gp_private.h"

/* ------------------------------------------------------------------ casts */

GBitmap *gp_bitmap_cast(const void *image)
{
    const GImage *i = image;
    if (!i || i->tag != GP_TAG_BITMAP || i->type != ImageTypeBitmap)
        return nullptr;
    return (GBitmap *)image;
}

GImage *gp_image_cast(const void *image)
{
    const GImage *i = image;
    if (!i || (i->tag != GP_TAG_BITMAP && i->tag != GP_TAG_METAFILE))
        return nullptr;
    return (GImage *)image;
}

/* ------------------------------------------------------------------ palettes */

static const ARGB vga16[16] = {0xff000000, 0xff800000, 0xff008000, 0xff808000, 0xff000080, 0xff800080,
                               0xff008080, 0xffc0c0c0, 0xff808080, 0xffff0000, 0xff00ff00, 0xffffff00,
                               0xff0000ff, 0xffff00ff, 0xff00ffff, 0xffffffff};

/* black and white, the 16 vga colors, or the halftone colors. */
ColorPalette *gp_palette_default(PixelFormat format)
{
    UINT count = format == PixelFormat1bppIndexed ? 2 : (format == PixelFormat4bppIndexed ? 16 : 256);
    ColorPalette *p = gp_alloc_zero(sizeof(ColorPalette) + sizeof(ARGB) * (count - 1));
    if (!p)
        return nullptr;
    p->Count = count;
    if (format == PixelFormat1bppIndexed) {
        p->Entries[0] = 0xff000000;
        p->Entries[1] = 0xffffffff;
    } else if (format == PixelFormat4bppIndexed) {
        p->Flags = 0;
        for (UINT i = 0; i < 16; i++)
            p->Entries[i] = vga16[i];
    } else {
        p->Flags = PaletteFlagsHalftone;
        gp_palette_halftone256(p->Entries);
    }
    return p;
}

ColorPalette *gp_palette_copy(const ColorPalette *src)
{
    if (!src)
        return nullptr;
    SIZE_T bytes = sizeof(ColorPalette) + sizeof(ARGB) * (src->Count ? src->Count - 1 : 0);
    return gp_dup(src, bytes);
}

/* ------------------------------------------------------------------ creation */

static BOOL format_indexed(PixelFormat f) { return IsIndexedPixelFormat(f); }

GpStatus gp_bitmap_create_with_bits(INT width, INT height, INT stride, PixelFormat format, BYTE *scan0, GBitmap **bitmap)
{
    if (width <= 0 || height <= 0 || !gp_format_known(format))
        return InvalidParameter;
    if (width > GP_MAX_DIMENSION || height > GP_MAX_DIMENSION)
        return OutOfMemory;
    UINT rowbytes = gp_default_stride(width, format);
    SIZE_T bytes = 0;
    if (!scan0) {
        ULONGLONG total = (ULONGLONG)rowbytes * (UINT)height;
        if (total > GP_MAX_BYTES)
            return OutOfMemory;
        bytes = (SIZE_T)total;
    }
    GBitmap *b = gp_alloc_zero(sizeof(*b));
    if (!b)
        return OutOfMemory;
    b->image.tag = GP_TAG_BITMAP;
    b->image.type = ImageTypeBitmap;
    b->width = width;
    b->height = height;
    b->format = format;
    if (scan0) {
        b->stride = stride;
        b->bits = scan0;
    } else {
        b->owned = gp_alloc_zero(bytes);
        if (!b->owned) {
            gp_free(b);
            return OutOfMemory;
        }
        b->stride = (INT)rowbytes;
        b->bits = b->owned;
    }
    if (format_indexed(format)) {
        b->palette = gp_palette_default(format);
        if (!b->palette) {
            gp_free(b->owned);
            gp_free(b);
            return OutOfMemory;
        }
    }
    b->dpi_x = b->dpi_y = 96.0f;
    b->raw_format = ImageFormatMemoryBMP;
    b->flags = ImageFlagsColorSpaceRGB;
    if (IsAlphaPixelFormat(format))
        b->flags |= ImageFlagsHasAlpha;
    *bitmap = b;
    return Ok;
}

GpStatus gp_bitmap_create(INT width, INT height, PixelFormat format, GBitmap **bitmap)
{
    return gp_bitmap_create_with_bits(width, height, 0, format, nullptr, bitmap);
}

void gp_bitmap_free(GBitmap *b)
{
    if (!b)
        return;
    b->image.tag = 0;
    for (UINT i = 0; i < b->prop_count; i++)
        gp_free(b->props[i].value);
    gp_free(b->props);
    gp_free(b->lock_buffer);
    gp_free(b->lock_palette);
    gp_free(b->palette);
    gp_frame_source_free(b->source);
    gp_save_state_free(b->save_state);
    gp_free(b->owned);
    gp_free(b);
}

/* copies the pixels, palette, resolution, original format and properties. */
GpStatus gp_bitmap_copy(const GBitmap *b, GBitmap **copy)
{
    GBitmap *c;
    GpStatus st = gp_bitmap_create(b->width, b->height, b->format, &c);
    if (st != Ok)
        return st;
    for (INT y = 0; y < b->height; y++)
        memcpy(c->bits + (SSIZE_T)y * c->stride, b->bits + (SSIZE_T)y * b->stride, (SIZE_T)gp_default_stride(b->width, b->format));
    if (b->palette) {
        gp_free(c->palette);
        c->palette = gp_palette_copy(b->palette);
    }
    c->dpi_x = b->dpi_x;
    c->dpi_y = b->dpi_y;
    c->raw_format = b->raw_format;
    c->flags = b->flags;
    if (b->prop_count) {
        c->props = gp_alloc_zero(sizeof(GProperty) * b->prop_count);
        if (!c->props) {
            gp_bitmap_free(c);
            return OutOfMemory;
        }
        for (UINT i = 0; i < b->prop_count; i++) {
            GProperty *to = &c->props[c->prop_count];
            *to = b->props[i];
            to->value = gp_dup(b->props[i].value, to->length);
            if (to->length && !to->value) {
                gp_bitmap_free(c);
                return OutOfMemory;
            }
            c->prop_count++;
        }
    }
    st = gp_frame_source_clone(b->source, c);
    if (st != Ok) {
        gp_bitmap_free(c);
        return st;
    }
    c->frame_index = b->frame_index;
    *copy = c;
    return Ok;
}

void gp_bitmap_read_argb(const GBitmap *b, INT x, INT y, INT count, UINT32 *out)
{
    gp_row_to_argb(b->bits + (SSIZE_T)y * b->stride, b->format, b->palette, x, count, out);
}

void gp_bitmap_write_argb(GBitmap *b, INT x, INT y, INT count, const UINT32 *argb)
{
    gp_row_from_argb(b->bits + (SSIZE_T)y * b->stride, b->format, b->palette, x, count, argb);
}

/* ------------------------------------------------------------------ the api */

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, PixelFormat format, BYTE *scan0,
                                                     GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!bitmap || width <= 0 || height <= 0)
        return InvalidParameter;
    if (scan0 && (!stride || stride % 4))
        return InvalidParameter;
    if (!gp_format_known(format))
        return InvalidParameter;
    if (scan0) {
        INT need = (INT)(((UINT)width * GetPixelFormatSize(format) + 7) / 8);
        INT mag = stride < 0 ? -stride : stride;
        if (mag < need)
            return InvalidParameter;
    }
    GBitmap *b;
    GpStatus st = gp_bitmap_create_with_bits(width, height, stride, format, scan0, &b);
    if (st == Ok)
        *bitmap = b;
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromGraphics(INT width, INT height, GpGraphics *graphics, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g || !bitmap || width <= 0 || height <= 0)
        return InvalidParameter;
    GBitmap *b;
    GpStatus st = gp_bitmap_create(width, height, PixelFormat32bppPARGB, &b);
    if (st != Ok)
        return st;
    b->dpi_x = g->dpi_x;
    b->dpi_y = g->dpi_y;
    *bitmap = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromDirectDrawSurface(IDirectDrawSurface7 *surface, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    return NotImplemented;
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromGdiDib(const BITMAPINFO *info, VOID *bits, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!info || !bits || !bitmap || info->bmiHeader.biSize < sizeof(BITMAPINFOHEADER))
        return InvalidParameter;
    const BITMAPINFOHEADER *h = &info->bmiHeader;
    INT width = h->biWidth, height = h->biHeight < 0 ? -h->biHeight : h->biHeight;
    BOOL bottom_up = h->biHeight > 0;
    PixelFormat format;
    switch (h->biBitCount) {
    case 1:
        format = PixelFormat1bppIndexed;
        break;
    case 4:
        format = PixelFormat4bppIndexed;
        break;
    case 8:
        format = PixelFormat8bppIndexed;
        break;
    case 16:
        format = PixelFormat16bppRGB555;
        break;
    case 24:
        format = PixelFormat24bppRGB;
        break;
    case 32:
        format = PixelFormat32bppRGB;
        break;
    default:
        return InvalidParameter;
    }
    if (h->biCompression != BI_RGB && !(h->biCompression == BI_BITFIELDS && h->biBitCount == 16))
        return InvalidParameter;
    GBitmap *b;
    GpStatus st = gp_bitmap_create(width, height, format, &b);
    if (st != Ok)
        return st;
    INT src_stride = (INT)((((UINT)width * h->biBitCount + 31) / 32) * 4);
    for (INT y = 0; y < height; y++) {
        const BYTE *s = (const BYTE *)bits + (SSIZE_T)(bottom_up ? height - 1 - y : y) * src_stride;
        memcpy(b->bits + (SSIZE_T)y * b->stride, s, (SIZE_T)min(src_stride, b->stride));
    }
    if (b->palette) {
        UINT colors = h->biClrUsed ? h->biClrUsed : (1u << h->biBitCount);
        if (colors > b->palette->Count)
            colors = b->palette->Count;
        const RGBQUAD *q = (const RGBQUAD *)((const BYTE *)info + h->biSize);
        for (UINT i = 0; i < colors; i++)
            b->palette->Entries[i] = 0xff000000u | (UINT)q[i].rgbRed << 16 | (UINT)q[i].rgbGreen << 8 | q[i].rgbBlue;
    }
    *bitmap = b;
    return Ok;
}

/* as 32 bit rows the callers convert from. NULL when it cannot be read. */
static BYTE *read_hbitmap(HBITMAP hbm, HPALETTE palette, INT *width, INT *height, INT *bits_pixel)
{
    BITMAP bm;
    if (GetObjectW(hbm, sizeof(bm), &bm) < (int)sizeof(bm) || bm.bmWidth <= 0 || !bm.bmHeight)
        return nullptr;
    *width = bm.bmWidth;
    *height = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
    *bits_pixel = bm.bmBitsPixel;
    BYTE *pixels = gp_alloc((SIZE_T)*width * 4 * (SIZE_T)*height);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!pixels || !dc) {
        if (dc)
            DeleteDC(dc);
        gp_free(pixels);
        return nullptr;
    }
    HGDIOBJ old_pal = nullptr;
    if (palette)
        old_pal = SelectPalette(dc, palette, FALSE);
    if (palette)
        RealizePalette(dc);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = *width;
    info.bmiHeader.biHeight = -*height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    int lines = GetDIBits(dc, hbm, 0, (UINT)*height, pixels, &info, DIB_RGB_COLORS);
    if (old_pal)
        SelectPalette(dc, old_pal, FALSE);
    DeleteDC(dc);
    if (lines <= 0) {
        gp_free(pixels);
        return nullptr;
    }
    return pixels;
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE palette, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!hbm || !bitmap)
        return InvalidParameter;
    INT width, height, bpp;
    BYTE *pixels = read_hbitmap(hbm, palette, &width, &height, &bpp);
    if (!pixels)
        return InvalidParameter;
    PixelFormat format = bpp == 32 ? PixelFormat32bppRGB : PixelFormat24bppRGB;
    if (bpp == 16)
        format = PixelFormat24bppRGB;
    GBitmap *b;
    GpStatus st = gp_bitmap_create(width, height, format, &b);
    if (st != Ok) {
        gp_free(pixels);
        return st;
    }
    for (INT y = 0; y < height; y++) {
        UINT32 *row = (UINT32 *)(pixels + (SIZE_T)y * width * 4);
        for (INT x = 0; x < width; x++)
            row[x] |= 0xff000000u;
        gp_row_from_argb(b->bits + (SSIZE_T)y * b->stride, format, nullptr, 0, width, row);
    }
    gp_free(pixels);
    *bitmap = b;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateHBITMAPFromBitmap(GpBitmap *bitmap, HBITMAP *hbitmap, ARGB background)
{
    GP_REQUIRE_INIT();
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !hbitmap)
        return InvalidParameter;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = b->width;
    info.bmiHeader.biHeight = b->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (!dib || !bits) {
        if (dib)
            DeleteObject(dib);
        return GenericError;
    }
    UINT bg_r = (background >> 16) & 0xff, bg_g = (background >> 8) & 0xff, bg_b = background & 0xff;
    UINT32 *row = gp_alloc(sizeof(UINT32) * b->width);
    if (!row) {
        DeleteObject(dib);
        return OutOfMemory;
    }
    for (INT y = 0; y < b->height; y++) {
        gp_bitmap_read_argb(b, 0, y, b->width, row);
        UINT32 *out = (UINT32 *)(bits + (SIZE_T)(b->height - 1 - y) * b->width * 4);
        for (INT x = 0; x < b->width; x++) {
            UINT32 c = row[x];
            UINT a = c >> 24;
            if (a == 255) {
                out[x] = c;
                continue;
            }
            UINT r = (((c >> 16) & 0xff) * a + bg_r * (255 - a) + 127) / 255;
            UINT g = (((c >> 8) & 0xff) * a + bg_g * (255 - a) + 127) / 255;
            UINT bl = ((c & 0xff) * a + bg_b * (255 - a) + 127) / 255;
            out[x] = 0xff000000u | r << 16 | g << 8 | bl;
        }
    }
    gp_free(row);
    *hbitmap = dib;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromResource(HINSTANCE instance, const WCHAR *name, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!bitmap)
        return InvalidParameter;
    HANDLE image = LoadImageW(instance, name, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!image)
        return InvalidParameter;
    GpStatus st = GdipCreateBitmapFromHBITMAP((HBITMAP)image, nullptr, bitmap);
    DeleteObject(image);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromHICON(HICON icon, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!icon || !bitmap)
        return InvalidParameter;
    ICONINFO info;
    if (!GetIconInfo(icon, &info))
        return InvalidParameter;
    BITMAP bm;
    GpStatus st = InvalidParameter;
    if (GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm)) {
        INT width = bm.bmWidth, height = info.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
        GBitmap *b;
        st = gp_bitmap_create(width, height, PixelFormat32bppARGB, &b);
        if (st == Ok) {
            HDC dc = CreateCompatibleDC(nullptr);
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = width;
            bi.bmiHeader.biHeight = -height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            UINT32 *color = gp_alloc((SIZE_T)width * height * 4), *mask = gp_alloc((SIZE_T)width * height * 4);
            if (dc && color && mask) {
                BOOL ok = TRUE;
                if (info.hbmColor)
                    ok = GetDIBits(dc, info.hbmColor, 0, (UINT)height, color, &bi, DIB_RGB_COLORS) > 0;
                if (ok)
                    ok = GetDIBits(dc, info.hbmMask, info.hbmColor ? 0 : (UINT)height, (UINT)height, mask, &bi,
                                   DIB_RGB_COLORS) > 0;
                if (ok) {
                    /* a color bitmap with an alpha channel keeps it, else the mask makes the alpha. */
                    BOOL has_alpha = FALSE;
                    if (info.hbmColor) {
                        for (SIZE_T i = 0; i < (SIZE_T)width * height; i++)
                            if (color[i] >> 24)
                                has_alpha = TRUE;
                    }
                    for (INT y = 0; y < height; y++) {
                        UINT32 *row = (UINT32 *)(b->bits + (SSIZE_T)y * b->stride);
                        for (INT x = 0; x < width; x++) {
                            SIZE_T i = (SIZE_T)y * width + x;
                            UINT32 c = info.hbmColor ? color[i] : (mask[i] & 0xffffff ? 0xffffffffu : 0xff000000u);
                            if (!has_alpha || !info.hbmColor)
                                c = (c & 0x00ffffffu) | ((mask[i] & 0x00ffffffu) ? 0u : 0xff000000u);
                            row[x] = c;
                        }
                    }
                } else {
                    st = GenericError;
                }
            } else {
                st = OutOfMemory;
            }
            if (dc)
                DeleteDC(dc);
            gp_free(color);
            gp_free(mask);
            if (st == Ok)
                *bitmap = b;
            else
                gp_bitmap_free(b);
        }
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipCreateHICONFromBitmap(GpBitmap *bitmap, HICON *icon)
{
    GP_REQUIRE_INIT();
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !icon)
        return InvalidParameter;
    BITMAPINFOHEADER h = {};
    h.biSize = sizeof(h);
    h.biWidth = b->width;
    h.biHeight = -b->height;
    h.biPlanes = 1;
    h.biBitCount = 32;
    h.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&h, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    HBITMAP mask = CreateBitmap(b->width, b->height, 1, 1, nullptr);
    if (dc)
        ReleaseDC(nullptr, dc);
    if (!color || !mask || !bits) {
        if (color)
            DeleteObject(color);
        if (mask)
            DeleteObject(mask);
        return GenericError;
    }
    for (INT y = 0; y < b->height; y++) {
        UINT32 *row = (UINT32 *)(bits + (SIZE_T)y * b->width * 4);
        gp_bitmap_read_argb(b, 0, y, b->width, row);
    }
    ICONINFO ii = {TRUE, 0, 0, mask, color};
    HICON h2 = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!h2)
        return GenericError;
    *icon = h2;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneBitmapArea(REAL x, REAL y, REAL w, REAL h, PixelFormat format, GpBitmap *bitmap,
                                               GpBitmap **clone)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !clone || !gp_format_known(format) || w <= 0.0f || h <= 0.0f)
        return InvalidParameter;
    INT ix = (INT)mime_vec_floorf(x), iy = (INT)mime_vec_floorf(y);
    INT iw = (INT)mime_vec_floorf(x + w + 0.5f) - ix, ih = (INT)mime_vec_floorf(y + h + 0.5f) - iy;
    if (iw <= 0)
        iw = 1;
    if (ih <= 0)
        ih = 1;
    if (ix < 0 || iy < 0 || ix + iw > b->width || iy + ih > b->height)
        return InvalidParameter;
    GBitmap *c;
    GpStatus st = gp_bitmap_create(iw, ih, format == PixelFormatDontCare ? b->format : format, &c);
    if (st != Ok)
        return st;
    ColorPalette *src_palette = b->palette;
    if (IsIndexedPixelFormat(c->format) && src_palette && c->format == b->format) {
        gp_free(c->palette);
        c->palette = gp_palette_copy(src_palette);
    }
    for (INT row = 0; row < ih; row++) {
        UINT32 *tmp = gp_alloc(sizeof(UINT32) * iw);
        if (!tmp) {
            gp_bitmap_free(c);
            return OutOfMemory;
        }
        gp_bitmap_read_argb(b, ix, iy + row, iw, tmp);
        gp_bitmap_write_argb(c, 0, row, iw, tmp);
        gp_free(tmp);
    }
    c->dpi_x = b->dpi_x;
    c->dpi_y = b->dpi_y;
    c->raw_format = b->raw_format;
    *clone = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneBitmapAreaI(INT x, INT y, INT w, INT h, PixelFormat format, GpBitmap *bitmap,
                                                GpBitmap **clone)
{
    return GdipCloneBitmapArea((float)x, (float)y, (float)w, (float)h, format, bitmap, clone);
}

/* ------------------------------------------------------------------ LockBits */

WINGDIPAPI GpStatus WINAPI GdipBitmapLockBits(GpBitmap *bitmap, const GpRect *rect, UINT flags, PixelFormat format,
                                              BitmapData *data)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !data || !(flags & (ImageLockModeRead | ImageLockModeWrite)))
        return InvalidParameter;
    GpRect r = {0, 0, b->width, b->height};
    if (rect) {
        r = *rect;
        if (r.X < 0 || r.Y < 0 || r.Width <= 0 || r.Height <= 0 || r.X > b->width - r.Width ||
            r.Y > b->height - r.Height)
            return InvalidParameter;
    }
    if (b->locked)
        return WrongState;
    if (!gp_format_known(format))
        return InvalidParameter;
    INT depth_bits = GetPixelFormatSize(b->format);
    BYTE *origin = b->bits + (SSIZE_T)r.Y * b->stride + (SSIZE_T)(r.X * depth_bits) / 8;
    BYTE *scan0;
    INT stride;
    BYTE *buffer = nullptr;
    BOOL direct = format == b->format && (depth_bits >= 8 || (r.X * depth_bits) % 8 == 0);
    if (flags & ImageLockModeUserInputBuf) {
        if (!data->Scan0 || !data->Stride)
            return InvalidParameter;
        scan0 = data->Scan0;
        stride = data->Stride;
    } else if (direct) {
        scan0 = origin;
        stride = b->stride;
    } else {
        stride = gp_default_stride(r.Width, format);
        buffer = gp_alloc_zero((SIZE_T)stride * (SIZE_T)r.Height);
        if (!buffer)
            return OutOfMemory;
        scan0 = buffer;
    }
    ColorPalette *lock_palette = nullptr;
    if (IsIndexedPixelFormat(format) && format != b->format)
        lock_palette = gp_palette_default(format);
    const ColorPalette *dst_palette = format == b->format ? b->palette : lock_palette;
    if ((flags & ImageLockModeRead) && scan0 != origin) {
        for (INT y = 0; y < r.Height; y++) {
            BYTE *dst = scan0 + (SSIZE_T)y * stride;
            const BYTE *src = b->bits + (SSIZE_T)(r.Y + y) * b->stride;
            if (format == b->format && depth_bits >= 8) {
                memcpy(dst, src + (SSIZE_T)(r.X * depth_bits) / 8, ((SIZE_T)r.Width * depth_bits + 7) / 8);
            } else {
                UINT32 *tmp = gp_alloc(sizeof(UINT32) * r.Width);
                if (!tmp) {
                    gp_free(buffer);
                    gp_free(lock_palette);
                    return OutOfMemory;
                }
                gp_row_to_argb(src, b->format, b->palette, r.X, r.Width, tmp);
                memset(dst, 0, ((SIZE_T)r.Width * GetPixelFormatSize(format) + 7) / 8);
                gp_row_from_argb(dst, format, dst_palette, 0, r.Width, tmp);
                gp_free(tmp);
            }
        }
    }
    b->locked = TRUE;
    b->lock_rect = r;
    b->lock_flags = flags;
    b->lock_format = format;
    b->lock_scan0 = scan0;
    b->lock_stride = stride;
    b->lock_buffer = buffer;
    b->lock_palette = lock_palette;
    data->Width = (UINT)r.Width;
    data->Height = (UINT)r.Height;
    data->Stride = stride;
    data->PixelFormat = format;
    data->Scan0 = scan0;
    data->Reserved = 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapUnlockBits(GpBitmap *bitmap, BitmapData *data)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !data)
        return InvalidParameter;
    if (!b->locked)
        return WrongState;
    GpRect r = b->lock_rect;
    INT depth_bits = GetPixelFormatSize(b->format);
    BYTE *origin = b->bits + (SSIZE_T)r.Y * b->stride + (SSIZE_T)(r.X * depth_bits) / 8;
    if ((b->lock_flags & ImageLockModeWrite) && b->lock_scan0 != origin) {
        for (INT y = 0; y < r.Height; y++) {
            BYTE *dst = b->bits + (SSIZE_T)(r.Y + y) * b->stride;
            const BYTE *src = b->lock_scan0 + (SSIZE_T)y * b->lock_stride;
            if (b->lock_format == b->format && depth_bits >= 8) {
                memcpy(dst + (SSIZE_T)(r.X * depth_bits) / 8, src, ((SIZE_T)r.Width * depth_bits + 7) / 8);
            } else {
                UINT32 *tmp = gp_alloc(sizeof(UINT32) * r.Width);
                if (!tmp)
                    break;
                const ColorPalette *sp = b->lock_format == b->format ? b->palette : b->lock_palette;
                gp_row_to_argb(src, b->lock_format, sp, 0, r.Width, tmp);
                gp_row_from_argb(dst, b->format, b->palette, r.X, r.Width, tmp);
                gp_free(tmp);
            }
        }
    }
    gp_free(b->lock_buffer);
    gp_free(b->lock_palette);
    b->lock_buffer = nullptr;
    b->lock_palette = nullptr;
    b->lock_scan0 = nullptr;
    b->locked = FALSE;
    return Ok;
}

/* ------------------------------------------------------------------ pixels */

WINGDIPAPI GpStatus WINAPI GdipBitmapGetPixel(GpBitmap *bitmap, INT x, INT y, ARGB *color)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !color)
        return InvalidParameter;
    if (x < 0 || y < 0 || x >= b->width || y >= b->height)
        return InvalidParameter;
    if (b->locked)
        return WrongState;
    UINT32 c;
    gp_bitmap_read_argb(b, x, y, 1, &c);
    *color = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapSetPixel(GpBitmap *bitmap, INT x, INT y, ARGB color)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b)
        return InvalidParameter;
    if (x < 0 || y < 0 || x >= b->width || y >= b->height || IsIndexedPixelFormat(b->format))
        return InvalidParameter;
    if (b->locked)
        return WrongState;
    UINT32 c = color;
    gp_bitmap_write_argb(b, x, y, 1, &c);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapSetResolution(GpBitmap *bitmap, REAL xdpi, REAL ydpi)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !(xdpi > 0.0f) || !(ydpi > 0.0f))
        return InvalidParameter;
    b->dpi_x = xdpi;
    b->dpi_y = ydpi;
    return Ok;
}

/* ------------------------------------------------------------------ conversion */

WINGDIPAPI GpStatus WINAPI GdipBitmapConvertFormat(GpBitmap *bitmap, PixelFormat format, DitherType dither,
                                                   PaletteType palette_type, ColorPalette *palette, REAL threshold)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || !gp_format_known(format))
        return InvalidParameter;
    if (b->locked)
        return WrongState;
    if (format == b->format)
        return Ok;
    GBitmap *c;
    GpStatus st = gp_bitmap_create(b->width, b->height, format, &c);
    if (st != Ok)
        return st;
    if (IsIndexedPixelFormat(format)) {
        ColorPalette *np = palette ? gp_palette_copy(palette) : gp_palette_default(format);
        if (!np) {
            gp_bitmap_free(c);
            return OutOfMemory;
        }
        gp_free(c->palette);
        c->palette = np;
    }
    UINT32 *tmp = gp_alloc(sizeof(UINT32) * b->width);
    if (!tmp) {
        gp_bitmap_free(c);
        return OutOfMemory;
    }
    for (INT y = 0; y < b->height; y++) {
        gp_bitmap_read_argb(b, 0, y, b->width, tmp);
        gp_bitmap_write_argb(c, 0, y, b->width, tmp);
    }
    gp_free(tmp);
    gp_free(b->owned);
    gp_free(b->palette);
    b->owned = c->owned;
    b->bits = c->bits;
    b->stride = c->stride;
    b->format = format;
    b->palette = c->palette;
    b->flags = (b->flags & ~(UINT)ImageFlagsHasAlpha) | (IsAlphaPixelFormat(format) ? ImageFlagsHasAlpha : 0);
    c->owned = nullptr;
    c->palette = nullptr;
    gp_bitmap_free(c);
    return Ok;
}

/* ------------------------------------------------------------------ image palettes */

static UINT palette_flags(const ColorPalette *p)
{
    UINT flags = p->Flags & PaletteFlagsHalftone;
    BOOL gray = TRUE;
    for (UINT i = 0; i < p->Count; i++) {
        UINT32 e = p->Entries[i];
        if ((e >> 24) != 255)
            flags |= PaletteFlagsHasAlpha;
        if (((e >> 16) & 255) != ((e >> 8) & 255) || ((e >> 8) & 255) != (e & 255))
            gray = FALSE;
    }
    if (gray && p->Count)
        flags |= PaletteFlagsGrayScale;
    return flags;
}

WINGDIPAPI GpStatus WINAPI GdipGetImagePaletteSize(GpImage *image, INT *size)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !size)
        return InvalidParameter;
    if (!b->palette) {
        *size = (INT)sizeof(ColorPalette) - (INT)sizeof(ARGB);
        return Ok;
    }
    *size = (INT)(sizeof(ColorPalette) - sizeof(ARGB) + sizeof(ARGB) * b->palette->Count);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImagePalette(GpImage *image, ColorPalette *palette, INT size)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !palette)
        return InvalidParameter;
    INT need = (INT)(sizeof(ColorPalette) - sizeof(ARGB) + (b->palette ? sizeof(ARGB) * b->palette->Count : 0));
    if (size < need)
        return InsufficientBuffer;
    if (!b->palette) {
        palette->Flags = 0;
        palette->Count = 0;
        return Ok;
    }
    palette->Flags = palette_flags(b->palette);
    palette->Count = b->palette->Count;
    memcpy(palette->Entries, b->palette->Entries, sizeof(ARGB) * b->palette->Count);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImagePalette(GpImage *image, const ColorPalette *palette)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !palette)
        return InvalidParameter;
    if (!IsIndexedPixelFormat(b->format))
        return GenericError;
    if (palette->Count > 256)
        return InvalidParameter;
    UINT max = 1u << GetPixelFormatSize(b->format);
    ColorPalette *np = gp_alloc_zero(sizeof(ColorPalette) + sizeof(ARGB) * max);
    if (!np)
        return OutOfMemory;
    np->Flags = palette->Flags;
    np->Count = max;
    UINT n = palette->Count < max ? palette->Count : max;
    memcpy(np->Entries, palette->Entries, sizeof(ARGB) * n);
    gp_free(b->palette);
    b->palette = np;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipInitializePalette(ColorPalette *palette, PaletteType type, INT count, BOOL transparent,
                                                 GpBitmap *bitmap)
{
    if (!palette)
        return InvalidParameter;
    switch (type) {
    case PaletteTypeFixedBW:
        palette->Count = 2;
        palette->Entries[0] = 0xff000000;
        palette->Entries[1] = 0xffffffff;
        break;
    case PaletteTypeFixedHalftone8:
    case PaletteTypeFixedHalftone27:
    case PaletteTypeFixedHalftone64:
    case PaletteTypeFixedHalftone125:
    case PaletteTypeFixedHalftone216:
    case PaletteTypeFixedHalftone252:
    case PaletteTypeFixedHalftone256:
        palette->Count = gp_palette_halftone(type, palette->Entries, transparent);
        palette->Flags = PaletteFlagsHalftone;
        return Ok;
    case PaletteTypeOptimal: {
        GBitmap *b = gp_bitmap_cast(bitmap);
        if (!b || count < 2 || count > 256)
            return InvalidParameter;
        palette->Count = gp_palette_optimal(b, (UINT)count, transparent, palette->Entries);
        return Ok;
    }
    default:
        return InvalidParameter;
    }
    palette->Flags = 0;
    return Ok;
}

/* ------------------------------------------------------------------ disposal and cloning */

WINGDIPAPI GpStatus WINAPI GdipCloneImage(GpImage *image, GpImage **clone)
{
    GImage *i = gp_image_cast(image);
    if (!i || !clone)
        return InvalidParameter;
    if (i->tag == GP_TAG_METAFILE)
        return gp_metafile_clone(image, clone);
    GBitmap *copy;
    GpStatus st = gp_bitmap_copy((GBitmap *)i, &copy);
    if (st == Ok)
        *clone = &copy->image;
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDisposeImage(GpImage *image)
{
    GImage *i = gp_image_cast(image);
    if (!i)
        return InvalidParameter;
    if (i->tag == GP_TAG_METAFILE)
        return gp_metafile_dispose(image);
    GBitmap *b = (GBitmap *)i;
    if (b->graphics_refs) {
        /* a graphics still draws on it, so the pixels go away with the graphics. */
        b->image.tag = 0;
        b->disposed = TRUE;
        return Ok;
    }
    gp_bitmap_free(b);
    return Ok;
}
