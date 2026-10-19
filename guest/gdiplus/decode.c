/*
 * image decoders: png through guest/lib/png and windows bitmaps. other
 * formats are recognized only to log that their decoder is missing.
 */
#include "gdiplus_private.h"
#include "../lib/png/png.h"

static void *png_alloc(void *ctx, size_t size)
{
    return gp_alloc(size);
}

static void png_free(void *ctx, void *p)
{
    gp_free(p);
}

static const MimeAllocator g_png_alloc = {png_alloc, png_free, nullptr};

static GpStatus png_status(int error)
{
    switch (error) {
    case MIME_PNG_OK:
        return Ok;
    case MIME_PNG_ERR_NOMEM:
        return OutOfMemory;
    case MIME_PNG_ERR_UNSUPPORTED:
        return NotImplemented;
    default:
        return InvalidParameter;
    }
}

/* a png with alpha or transparency is 32bppARGB, any other png 24bppRGB. */
static GpStatus decode_png(const BYTE *data, SIZE_T size, GpBitmap **bitmap)
{
    MimePngInfo info;
    if (!mime_png_info(data, size, &info))
        return InvalidParameter;
    if (info.width > GP_MAX_DIMENSION || info.height > GP_MAX_DIMENSION)
        return OutOfMemory;
    PixelFormat format = info.has_alpha ? PixelFormat32bppARGB : PixelFormat24bppRGB;
    GpBitmap *b;
    GpStatus status = gp_bitmap_create((INT)info.width, (INT)info.height, format, &b);
    if (status != Ok)
        return status;
    if (format == PixelFormat32bppARGB) {
        status = png_status(mime_png_decode(data, size, &g_png_alloc, 0, b->Bits, (size_t)b->Stride));
    } else {
        uint8_t *pixels = nullptr;
        status = png_status(mime_png_decode_alloc(data, size, &g_png_alloc, 0, &pixels, nullptr));
        if (status == Ok)
            gp_convert(b->Bits, b->Stride, format, pixels, b->Width * 4, PixelFormat32bppRGB, b->Width, b->Height);
        mime_free(&g_png_alloc, pixels);
    }
    if (status != Ok) {
        GdipDisposeImage(&b->Image);
        return status;
    }
    *bitmap = b;
    return Ok;
}

/* ------------------------------------------------------------------ bmp */

#define BMP_RGB 0
#define BMP_RLE8 1
#define BMP_RLE4 2
#define BMP_BITFIELDS 3
#define BMP_ALPHABITFIELDS 6

static UINT read16(const BYTE *p)
{
    return p[0] | (UINT)p[1] << 8;
}

static UINT read32(const BYTE *p)
{
    return p[0] | (UINT)p[1] << 8 | (UINT)p[2] << 16 | (UINT)p[3] << 24;
}

typedef struct BMP_MASK {
    UINT Mask;
    UINT Shift;
    UINT Max; /* the largest channel value after the shift, 0 for an absent channel. */
} BMP_MASK;

static void mask_init(BMP_MASK *m, UINT mask)
{
    m->Mask = mask;
    m->Shift = 0;
    m->Max = 0;
    if (!mask)
        return;
    while (!(mask & 1)) {
        mask >>= 1;
        m->Shift++;
    }
    UINT bits = 0;
    while (mask & 1) {
        mask >>= 1;
        bits++;
    }
    /* keep at most 8 significant bits so the scaling fits in 32 bits. */
    if (bits > 8) {
        m->Shift += bits - 8;
        bits = 8;
    }
    m->Max = (1u << bits) - 1;
}

static UINT mask_get(const BMP_MASK *m, UINT v)
{
    if (!m->Max)
        return 0;
    v = (v & m->Mask) >> m->Shift;
    if (v > m->Max)
        v = m->Max;
    return (v * 255 + m->Max / 2) / m->Max;
}

/*
 * expands rle8 or rle4 data into one palette index per pixel, bottom-up rows
 * of width bytes. skipped pixels keep index 0.
 */
static BOOL bmp_unrle(const BYTE *src, SIZE_T size, BOOL rle4, INT width, INT height, BYTE *out)
{
    INT x = 0, y = 0;
    SIZE_T i = 0;
    while (i + 2 <= size && y < height) {
        UINT count = src[i], value = src[i + 1];
        i += 2;
        if (count) {
            for (UINT k = 0; k < count && x < width; k++, x++)
                out[(SIZE_T)y * width + x] = (BYTE)(rle4 ? (k & 1 ? value & 15 : value >> 4) : value);
            continue;
        }
        if (value == 0) {
            x = 0;
            y++;
        } else if (value == 1) {
            return TRUE;
        } else if (value == 2) {
            if (i + 2 > size)
                return FALSE;
            x += src[i];
            y += src[i + 1];
            i += 2;
            if (x > width)
                x = width;
        } else {
            SIZE_T bytes = rle4 ? (value + 1) / 2 : value;
            if (i + bytes > size)
                return FALSE;
            for (UINT k = 0; k < value; k++, x++) {
                BYTE b = src[i + (rle4 ? k / 2 : k)];
                if (x < width)
                    out[(SIZE_T)y * width + x] = (BYTE)(rle4 ? (k & 1 ? b & 15 : b >> 4) : b);
            }
            i += (bytes + 1) & ~(SIZE_T)1;
            if (x > width)
                x = width;
        }
    }
    return TRUE;
}

/*
 * windows bitmaps of 1, 4, 8, 16, 24 and 32 bpp, uncompressed, rle or with
 * bit fields. a bitmap with an alpha mask becomes 32bppARGB, other 32 bpp
 * bitmaps 32bppRGB and anything else 24bppRGB; windows keeps indexed and
 * 16 bpp formats as they are.
 */
static GpStatus decode_bmp(const BYTE *data, SIZE_T size, GpBitmap **bitmap)
{
    if (size < 14 + 12)
        return InvalidParameter;
    UINT offset = read32(data + 10);
    const BYTE *h = data + 14;
    UINT header = read32(h);
    INT width, height;
    UINT bpp, compression = BMP_RGB, used = 0, entry = 4;
    if (header == 12) {
        width = (INT)read16(h + 4);
        height = (INT)read16(h + 6);
        bpp = read16(h + 10);
        entry = 3;
    } else if (header >= 40 && header <= size - 14) {
        width = (INT)read32(h + 4);
        height = (INT)read32(h + 8);
        bpp = read16(h + 14);
        compression = read32(h + 16);
        used = read32(h + 32);
    } else {
        return InvalidParameter;
    }
    BOOL top_down = height < 0;
    if (top_down)
        height = -height;
    if (width <= 0 || height <= 0 || (top_down && (compression == BMP_RLE8 || compression == BMP_RLE4)))
        return InvalidParameter;
    if (width > GP_MAX_DIMENSION || height > GP_MAX_DIMENSION)
        return OutOfMemory;

    /* the color masks follow a 40 byte header, larger headers contain them. */
    UINT masks[4] = {0, 0, 0, 0};
    SIZE_T table = 14 + header;
    if (compression == BMP_BITFIELDS || compression == BMP_ALPHABITFIELDS) {
        if (bpp != 16 && bpp != 32)
            return InvalidParameter;
        UINT count = header >= 52 ? (header >= 56 ? 4 : 3) : compression == BMP_ALPHABITFIELDS ? 4 : 3;
        const BYTE *m = header >= 52 ? h + 40 : data + table;
        if (header < 52) {
            if (table + count * 4 > size)
                return InvalidParameter;
            table += count * 4;
        }
        for (UINT i = 0; i < count; i++)
            masks[i] = read32(m + i * 4);
    } else if (compression == BMP_RGB || compression == BMP_RLE8 || compression == BMP_RLE4) {
        if (bpp == 16) {
            masks[0] = 0x7c00;
            masks[1] = 0x3e0;
            masks[2] = 0x1f;
        } else if (bpp == 32) {
            masks[0] = 0xff0000;
            masks[1] = 0xff00;
            masks[2] = 0xff;
        }
        if ((compression == BMP_RLE8 && bpp != 8) || (compression == BMP_RLE4 && bpp != 4))
            return InvalidParameter;
    } else {
        static LONG volatile once;
        MimeStubLog(&once, "gdiplus", "embedded jpeg or png bmp decoder");
        return NotImplemented;
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return InvalidParameter;

    ARGB palette[256] = {};
    if (bpp <= 8) {
        UINT count = used && used < (1u << bpp) ? used : 1u << bpp;
        if (table + (SIZE_T)count * entry > size)
            return InvalidParameter;
        for (UINT i = 0; i < count; i++) {
            const BYTE *p = data + table + i * entry;
            palette[i] = 0xff000000u | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
        }
    }

    if (offset >= size)
        return InvalidParameter;
    const BYTE *bits = data + offset;
    SIZE_T available = size - offset;
    SIZE_T stride = ((SIZE_T)width * bpp + 31) / 32 * 4;
    BYTE *indices = nullptr;
    if (compression == BMP_RLE8 || compression == BMP_RLE4) {
        indices = gp_alloc_zero((SIZE_T)width * (SIZE_T)height);
        if (!indices)
            return OutOfMemory;
        if (!bmp_unrle(bits, available, compression == BMP_RLE4, width, height, indices)) {
            gp_free(indices);
            return InvalidParameter;
        }
    } else if (stride * (SIZE_T)height > available) {
        return InvalidParameter;
    }

    BMP_MASK r, g, b, a;
    mask_init(&r, masks[0]);
    mask_init(&g, masks[1]);
    mask_init(&b, masks[2]);
    mask_init(&a, masks[3]);
    PixelFormat format = a.Max ? PixelFormat32bppARGB : bpp == 32 ? PixelFormat32bppRGB : PixelFormat24bppRGB;
    GpBitmap *out;
    GpStatus status = gp_bitmap_create(width, height, format, &out);
    if (status != Ok) {
        gp_free(indices);
        return status;
    }
    for (INT y = 0; y < height; y++) {
        INT source_row = top_down ? y : height - 1 - y;
        BYTE *dst = out->Bits + (INT_PTR)y * out->Stride;
        if (indices) {
            const BYTE *src = indices + (SIZE_T)source_row * width;
            for (INT x = 0; x < width; x++)
                gp_write_pixel(dst, x, format, palette[src[x]]);
            continue;
        }
        const BYTE *src = bits + (SIZE_T)source_row * stride;
        for (INT x = 0; x < width; x++) {
            ARGB c;
            UINT v;
            switch (bpp) {
            case 1:
                c = palette[src[x >> 3] >> (7 - (x & 7)) & 1];
                break;
            case 4:
                c = palette[x & 1 ? src[x >> 1] & 15 : src[x >> 1] >> 4];
                break;
            case 8:
                c = palette[src[x]];
                break;
            case 24:
                c = 0xff000000u | (ARGB)src[x * 3 + 2] << 16 | (ARGB)src[x * 3 + 1] << 8 | src[x * 3];
                break;
            default:
                v = bpp == 16 ? read16(src + x * 2) : read32(src + x * 4);
                c = (a.Max ? mask_get(&a, v) : 255) << 24 | mask_get(&r, v) << 16 | mask_get(&g, v) << 8 |
                    mask_get(&b, v);
                break;
            }
            gp_write_pixel(dst, x, format, c);
        }
    }
    gp_free(indices);
    *bitmap = out;
    return Ok;
}

/* ------------------------------------------------------------------ dispatch */

static GpStatus unsupported(LONG volatile *once, const char *what)
{
    MimeStubLog(once, "gdiplus", what);
    return UnknownImageFormat;
}

/* the dots per inch a pixels per meter value stands for. */
static float dpi_of(UINT per_meter)
{
    return per_meter ? (float)per_meter * 0.0254f : 96.0f;
}

/* the pHYs chunk of a png, in pixels per meter when the unit is the meter. */
static void png_resolution(const BYTE *data, SIZE_T size, GpBitmap *b)
{
    SIZE_T at = 8;
    while (at + 12 <= size) {
        UINT length = (UINT)data[at] << 24 | (UINT)data[at + 1] << 16 | (UINT)data[at + 2] << 8 | data[at + 3];
        if (length > size - at - 12)
            return;
        const BYTE *type = data + at + 4;
        if (!memcmp(type, "pHYs", 4) && length == 9 && type[12] == 1) {
            const BYTE *p = type + 4;
            UINT x = (UINT)p[0] << 24 | (UINT)p[1] << 16 | (UINT)p[2] << 8 | p[3];
            UINT y = (UINT)p[4] << 24 | (UINT)p[5] << 16 | (UINT)p[6] << 8 | p[7];
            b->DpiX = dpi_of(x);
            b->DpiY = dpi_of(y);
            return;
        }
        if (!memcmp(type, "IDAT", 4) || !memcmp(type, "IEND", 4))
            return;
        at += 12 + length;
    }
}

GpStatus gp_decode(const BYTE *data, SIZE_T size, GpBitmap **bitmap)
{
    static LONG volatile jpeg_once, gif_once, tiff_once, icon_once, metafile_once;
    GpStatus status;
    if (mime_png_is_png(data, size)) {
        status = decode_png(data, size, bitmap);
        if (status == Ok) {
            png_resolution(data, size, *bitmap);
            (*bitmap)->RawFormat = gp_format_png;
            (*bitmap)->Decoded = TRUE;
        }
        return status;
    }
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        status = decode_bmp(data, size, bitmap);
        if (status == Ok) {
            if (size >= 14 + 40 && read32(data + 14) >= 40) {
                (*bitmap)->DpiX = dpi_of(read32(data + 14 + 24));
                (*bitmap)->DpiY = dpi_of(read32(data + 14 + 28));
            }
            (*bitmap)->RawFormat = gp_format_bmp;
            (*bitmap)->Decoded = TRUE;
        }
        return status;
    }
    if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
        return unsupported(&jpeg_once, "jpeg decoder");
    if (size >= 6 && !memcmp(data, "GIF8", 4))
        return unsupported(&gif_once, "gif decoder");
    if (size >= 4 && (!memcmp(data, "II*\0", 4) || !memcmp(data, "MM\0*", 4)))
        return unsupported(&tiff_once, "tiff decoder");
    if (size >= 4 && !memcmp(data, "\0\0\1\0", 4))
        return unsupported(&icon_once, "icon decoder");
    if ((size >= 4 && !memcmp(data, "\xd7\xcd\xc6\x9a", 4)) || (size >= 44 && !memcmp(data + 40, " EMF", 4)))
        return unsupported(&metafile_once, "metafile decoder");
    return UnknownImageFormat;
}
