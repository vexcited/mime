/*
 * bitmaps: creation, pixel format conversion, LockBits and the image
 * queries. pixels stay in the bitmap's own format; conversions go through
 * straight, non premultiplied ARGB.
 */
#include "gdiplus_private.h"

BOOL gp_format_supported(PixelFormat format)
{
    switch (format) {
    case PixelFormat16bppRGB555:
    case PixelFormat16bppRGB565:
    case PixelFormat24bppRGB:
    case PixelFormat32bppRGB:
    case PixelFormat32bppARGB:
    case PixelFormat32bppPARGB:
        return TRUE;
    default:
        return FALSE;
    }
}

/* a format gdi+ defines, whether or not mime can hold its pixels. */
static BOOL format_known(PixelFormat format)
{
    switch (format) {
    case PixelFormat1bppIndexed:
    case PixelFormat4bppIndexed:
    case PixelFormat8bppIndexed:
    case PixelFormat16bppGrayScale:
    case PixelFormat16bppARGB1555:
    case PixelFormat48bppRGB:
    case PixelFormat64bppARGB:
    case PixelFormat64bppPARGB:
    case PixelFormat32bppCMYK:
        return TRUE;
    default:
        return gp_format_supported(format);
    }
}

/* the stride gdi+ gives its own buffers: rows padded to 4 bytes. */
INT gp_default_stride(INT width, PixelFormat format)
{
    return (width * GetPixelFormatSize(format) + 31) / 32 * 4;
}

GpBitmap *gp_bitmap_cast(GpImage *image)
{
    if (!image || image->Magic != GP_IMAGE_MAGIC || image->Type != ImageTypeBitmap)
        return nullptr;
    return (GpBitmap *)image;
}

GpStatus gp_bitmap_create(INT width, INT height, PixelFormat format, GpBitmap **bitmap)
{
    if (width <= 0 || height <= 0 || !gp_format_supported(format))
        return InvalidParameter;
    if (width > GP_MAX_DIMENSION || height > GP_MAX_DIMENSION)
        return OutOfMemory;
    INT stride = gp_default_stride(width, format);
    ULONGLONG bytes = (ULONGLONG)(UINT)stride * (UINT)height;
    if (bytes > GP_MAX_BYTES)
        return OutOfMemory;
    GpBitmap *b = gp_alloc_zero(sizeof(*b));
    BYTE *bits = gp_alloc_zero((SIZE_T)bytes);
    if (!b || !bits) {
        gp_free(b);
        gp_free(bits);
        return OutOfMemory;
    }
    b->Image.Magic = GP_IMAGE_MAGIC;
    b->Image.Type = ImageTypeBitmap;
    b->Width = width;
    b->Height = height;
    b->Format = format;
    b->Stride = stride;
    b->Bits = bits;
    b->Owned = bits;
    *bitmap = b;
    return Ok;
}

/* ------------------------------------------------------------------ pixels */

static BYTE expand5(UINT v)
{
    return (BYTE)((v << 3) | (v >> 2));
}

static BYTE expand6(UINT v)
{
    return (BYTE)((v << 2) | (v >> 4));
}

ARGB gp_premultiply(ARGB c)
{
    UINT a = c >> 24;
    if (a == 255)
        return c;
    UINT r = ((c >> 16 & 0xff) * a + 127) / 255;
    UINT g = ((c >> 8 & 0xff) * a + 127) / 255;
    UINT b = ((c & 0xff) * a + 127) / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

static UINT unpremultiply_channel(UINT v, UINT a)
{
    v = (v * 255 + a / 2) / a;
    return v > 255 ? 255 : v;
}

ARGB gp_unpremultiply(ARGB c)
{
    UINT a = c >> 24;
    if (a == 255)
        return c;
    if (!a)
        return 0;
    return a << 24 | unpremultiply_channel(c >> 16 & 0xff, a) << 16 | unpremultiply_channel(c >> 8 & 0xff, a) << 8 |
           unpremultiply_channel(c & 0xff, a);
}

ARGB gp_read_pixel(const BYTE *row, INT x, PixelFormat format)
{
    const BYTE *p;
    UINT v;
    switch (format) {
    case PixelFormat16bppRGB555:
        p = row + x * 2;
        v = p[0] | (UINT)p[1] << 8;
        return 0xff000000u | (ARGB)expand5(v >> 10 & 31) << 16 | (ARGB)expand5(v >> 5 & 31) << 8 | expand5(v & 31);
    case PixelFormat16bppRGB565:
        p = row + x * 2;
        v = p[0] | (UINT)p[1] << 8;
        return 0xff000000u | (ARGB)expand5(v >> 11) << 16 | (ARGB)expand6(v >> 5 & 63) << 8 | expand5(v & 31);
    case PixelFormat24bppRGB:
        p = row + x * 3;
        return 0xff000000u | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
    case PixelFormat32bppRGB:
        p = row + x * 4;
        return 0xff000000u | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
    case PixelFormat32bppARGB:
        p = row + x * 4;
        return (ARGB)p[3] << 24 | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
    case PixelFormat32bppPARGB:
        p = row + x * 4;
        return gp_unpremultiply((ARGB)p[3] << 24 | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0]);
    default:
        return 0;
    }
}

void gp_write_pixel(BYTE *row, INT x, PixelFormat format, ARGB c)
{
    BYTE *p;
    UINT v;
    switch (format) {
    case PixelFormat16bppRGB555:
        p = row + x * 2;
        v = (c >> 9 & 0x7c00) | (c >> 6 & 0x3e0) | (c >> 3 & 0x1f);
        p[0] = (BYTE)v;
        p[1] = (BYTE)(v >> 8);
        break;
    case PixelFormat16bppRGB565:
        p = row + x * 2;
        v = (c >> 8 & 0xf800) | (c >> 5 & 0x7e0) | (c >> 3 & 0x1f);
        p[0] = (BYTE)v;
        p[1] = (BYTE)(v >> 8);
        break;
    case PixelFormat24bppRGB:
        p = row + x * 3;
        p[0] = (BYTE)c;
        p[1] = (BYTE)(c >> 8);
        p[2] = (BYTE)(c >> 16);
        break;
    case PixelFormat32bppRGB:
        c |= 0xff000000u;
        [[fallthrough]];
    case PixelFormat32bppARGB:
        p = row + x * 4;
        p[0] = (BYTE)c;
        p[1] = (BYTE)(c >> 8);
        p[2] = (BYTE)(c >> 16);
        p[3] = (BYTE)(c >> 24);
        break;
    case PixelFormat32bppPARGB:
        c = gp_premultiply(c);
        p = row + x * 4;
        p[0] = (BYTE)c;
        p[1] = (BYTE)(c >> 8);
        p[2] = (BYTE)(c >> 16);
        p[3] = (BYTE)(c >> 24);
        break;
    default:
        break;
    }
}

void gp_convert(BYTE *dst, INT dst_stride, PixelFormat dst_format, const BYTE *src, INT src_stride,
                PixelFormat src_format, INT width, INT height)
{
    for (INT y = 0; y < height; y++, dst += dst_stride, src += src_stride) {
        if (dst_format == src_format) {
            memmove(dst, src, (SIZE_T)width * (GetPixelFormatSize(src_format) / 8));
            continue;
        }
        for (INT x = 0; x < width; x++)
            gp_write_pixel(dst, x, dst_format, gp_read_pixel(src, x, src_format));
    }
}

/* ------------------------------------------------------------------ creation */

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, PixelFormat format,
                                                          BYTE *scan0, GpBitmap **bitmap)
{
    if (!bitmap || width <= 0 || height <= 0)
        return InvalidParameter;
    if (scan0 && (!stride || stride % 4))
        return InvalidParameter;
    if (!format_known(format))
        return InvalidParameter;
    if (!gp_format_supported(format)) {
        MIME_STUB("gdiplus");
        return NotImplemented;
    }
    if (!scan0)
        return gp_bitmap_create(width, height, format, bitmap);
    if (width > GP_MAX_DIMENSION || height > GP_MAX_DIMENSION)
        return OutOfMemory;
    GpBitmap *b = gp_alloc_zero(sizeof(*b));
    if (!b)
        return OutOfMemory;
    b->Image.Magic = GP_IMAGE_MAGIC;
    b->Image.Type = ImageTypeBitmap;
    b->Width = width;
    b->Height = height;
    b->Format = format;
    b->Stride = stride;
    b->Bits = scan0;
    *bitmap = b;
    return Ok;
}

/*
 * 32 bpp bitmaps become 32bppRGB, any other depth 24bppRGB, through a 32 bpp
 * GetDIBits. windows keeps indexed and 16 bpp formats; the palette argument
 * is ignored.
 */
DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE palette, GpBitmap **bitmap)
{
    if (!hbm || !bitmap)
        return InvalidParameter;
    BITMAP bm;
    if (GetObjectW(hbm, sizeof(bm), &bm) < (int)sizeof(bm) || bm.bmWidth <= 0 || !bm.bmHeight)
        return InvalidParameter;
    INT width = bm.bmWidth, height = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
    PixelFormat format = bm.bmBitsPixel == 32 ? PixelFormat32bppRGB : PixelFormat24bppRGB;
    GpBitmap *b;
    GpStatus status = gp_bitmap_create(width, height, format, &b);
    if (status != Ok)
        return status;
    BYTE *pixels = format == PixelFormat32bppRGB ? b->Bits : gp_alloc((SIZE_T)width * 4 * (SIZE_T)height);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!pixels || !dc) {
        if (dc)
            DeleteDC(dc);
        if (pixels != b->Bits)
            gp_free(pixels);
        GdipDisposeImage(&b->Image);
        return OutOfMemory;
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    int lines = GetDIBits(dc, hbm, 0, (UINT)height, pixels, &info, DIB_RGB_COLORS);
    DeleteDC(dc);
    if (lines <= 0) {
        if (pixels != b->Bits)
            gp_free(pixels);
        GdipDisposeImage(&b->Image);
        return GenericError;
    }
    if (pixels == b->Bits) {
        /* the alpha byte of a 32bppRGB pixel means nothing; keep it opaque. */
        for (SIZE_T i = 3, n = (SIZE_T)width * 4 * (SIZE_T)height; i < n; i += 4)
            pixels[i] = 0xff;
    } else {
        gp_convert(b->Bits, b->Stride, format, pixels, width * 4, PixelFormat32bppRGB, width, height);
        gp_free(pixels);
    }
    *bitmap = b;
    return Ok;
}

/* ------------------------------------------------------------------ LockBits */

DECLSPEC_EXPORT GpStatus WINAPI GdipBitmapLockBits(GpBitmap *bitmap, const GpRect *rect, UINT flags,
                                                   PixelFormat format, BitmapData *data)
{
    GpBitmap *b = gp_bitmap_cast((GpImage *)bitmap);
    if (!b || !data || !(flags & (ImageLockModeRead | ImageLockModeWrite)))
        return InvalidParameter;
    GpRect r = {0, 0, b->Width, b->Height};
    if (rect) {
        r = *rect;
        if (r.X < 0 || r.Y < 0 || r.Width <= 0 || r.Height <= 0 || r.X > b->Width - r.Width ||
            r.Y > b->Height - r.Height)
            return InvalidParameter;
    }
    if (b->Locked)
        return WrongState;
    if (!format_known(format))
        return InvalidParameter;
    if (!gp_format_supported(format)) {
        MIME_STUB("gdiplus");
        return NotImplemented;
    }
    BYTE *origin = b->Bits + (INT_PTR)r.Y * b->Stride + (INT_PTR)r.X * (GetPixelFormatSize(b->Format) / 8);
    BYTE *scan0;
    INT stride;
    BYTE *buffer = nullptr;
    if (flags & ImageLockModeUserInputBuf) {
        if (!data->Scan0 || !data->Stride)
            return InvalidParameter;
        scan0 = data->Scan0;
        stride = data->Stride;
    } else if (format == b->Format) {
        scan0 = origin;
        stride = b->Stride;
    } else {
        stride = gp_default_stride(r.Width, format);
        buffer = gp_alloc_zero((SIZE_T)stride * (SIZE_T)r.Height);
        if (!buffer)
            return OutOfMemory;
        scan0 = buffer;
    }
    if ((flags & ImageLockModeRead) && scan0 != origin)
        gp_convert(scan0, stride, format, origin, b->Stride, b->Format, r.Width, r.Height);
    b->Locked = TRUE;
    b->LockRect = r;
    b->LockFlags = flags;
    b->LockFormat = format;
    b->LockScan0 = scan0;
    b->LockStride = stride;
    b->LockBuffer = buffer;
    data->Width = (UINT)r.Width;
    data->Height = (UINT)r.Height;
    data->Stride = stride;
    data->PixelFormat = format;
    data->Scan0 = scan0;
    data->Reserved = 0;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipBitmapUnlockBits(GpBitmap *bitmap, BitmapData *data)
{
    GpBitmap *b = gp_bitmap_cast((GpImage *)bitmap);
    if (!b || !data)
        return InvalidParameter;
    if (!b->Locked)
        return WrongState;
    GpRect r = b->LockRect;
    BYTE *origin = b->Bits + (INT_PTR)r.Y * b->Stride + (INT_PTR)r.X * (GetPixelFormatSize(b->Format) / 8);
    if ((b->LockFlags & ImageLockModeWrite) && b->LockScan0 != origin)
        gp_convert(origin, b->Stride, b->Format, b->LockScan0, b->LockStride, b->LockFormat, r.Width, r.Height);
    gp_free(b->LockBuffer);
    b->LockBuffer = nullptr;
    b->LockScan0 = nullptr;
    b->Locked = FALSE;
    return Ok;
}

/* ------------------------------------------------------------------ queries */

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageWidth(GpImage *image, UINT *width)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !width)
        return InvalidParameter;
    *width = (UINT)b->Width;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageHeight(GpImage *image, UINT *height)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !height)
        return InvalidParameter;
    *height = (UINT)b->Height;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImagePixelFormat(GpImage *image, PixelFormat *format)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !format)
        return InvalidParameter;
    *format = b->Format;
    return Ok;
}

/* no supported format is indexed, so every palette is empty. */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetImagePaletteSize(GpImage *image, INT *size)
{
    if (!gp_bitmap_cast(image) || !size)
        return InvalidParameter;
    *size = sizeof(ColorPalette);
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImagePalette(GpImage *image, ColorPalette *palette, INT size)
{
    if (!gp_bitmap_cast(image) || !palette)
        return InvalidParameter;
    if (size < (INT)(2 * sizeof(UINT)))
        return InsufficientBuffer;
    palette->Flags = 0;
    palette->Count = 0;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCloneImage(GpImage *image, GpImage **clone)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !clone)
        return InvalidParameter;
    GpBitmap *copy;
    GpStatus status = gp_bitmap_create(b->Width, b->Height, b->Format, &copy);
    if (status != Ok)
        return status;
    gp_convert(copy->Bits, copy->Stride, copy->Format, b->Bits, b->Stride, b->Format, b->Width, b->Height);
    *clone = &copy->Image;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDisposeImage(GpImage *image)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return InvalidParameter;
    b->Image.Magic = 0;
    gp_free(b->LockBuffer);
    gp_free(b->Owned);
    gp_free(b);
    return Ok;
}
