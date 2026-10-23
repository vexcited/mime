/*
 * pixel formats: rows of any format to and from straight 8-bit argb, the common currency of the bitmap code, and
 * the format conversions built on it.
 */
#include "gp_private.h"

BOOL gp_format_known(PixelFormat format)
{
    switch (format) {
    case PixelFormat1bppIndexed:
    case PixelFormat4bppIndexed:
    case PixelFormat8bppIndexed:
    case PixelFormat16bppGrayScale:
    case PixelFormat16bppRGB555:
    case PixelFormat16bppRGB565:
    case PixelFormat16bppARGB1555:
    case PixelFormat24bppRGB:
    case PixelFormat32bppRGB:
    case PixelFormat32bppARGB:
    case PixelFormat32bppPARGB:
    case PixelFormat48bppRGB:
    case PixelFormat64bppARGB:
    case PixelFormat64bppPARGB:
    case PixelFormat32bppCMYK:
        return TRUE;
    default:
        return FALSE;
    }
}

/* the stride gdi+ gives its own buffers: rows padded to 4 bytes. */
INT gp_default_stride(INT width, PixelFormat format)
{
    return (INT)(((UINT)width * GetPixelFormatSize(format) + 31) / 32 * 4);
}

static UINT32 expand5(UINT32 v) { return (v << 3) | (v >> 2); }
static UINT32 expand6(UINT32 v) { return (v << 2) | (v >> 4); }
static UINT32 pack555(UINT32 r, UINT32 g, UINT32 b) { return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); }

static UINT32 narrow16(UINT32 v) { return (v * 255 + 32767) / 65535; }
static UINT32 widen8(UINT32 v) { return v * 257; }

static UINT32 palette_lookup(const ColorPalette *pal, UINT index)
{
    if (!pal || index >= pal->Count)
        return 0xff000000u;
    return pal->Entries[index];
}

/* by squared channel distance. */
static UINT nearest_entry(const ColorPalette *pal, UINT32 argb)
{
    if (!pal || !pal->Count)
        return 0;
    UINT best = 0;
    UINT best_d = ~0u;
    INT a = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    for (UINT i = 0; i < pal->Count; i++) {
        UINT32 e = pal->Entries[i];
        INT da = (INT)(e >> 24) - a, dr = (INT)((e >> 16) & 255) - r, dg = (INT)((e >> 8) & 255) - g,
            db = (INT)(e & 255) - b;
        UINT d = (UINT)(dr * dr + dg * dg + db * db) + (UINT)(da * da) / 2;
        if (d < best_d) {
            best_d = d;
            best = i;
            if (!d)
                break;
        }
    }
    return best;
}

void gp_row_to_argb(const BYTE *row, PixelFormat f, const ColorPalette *pal, INT x, INT n, UINT32 *out)
{
    switch (f) {
    case PixelFormat32bppARGB: {
        const UINT32 *p = (const UINT32 *)row + x;
        for (INT i = 0; i < n; i++)
            out[i] = p[i];
        break;
    }
    case PixelFormat32bppPARGB: {
        const UINT32 *p = (const UINT32 *)row + x;
        for (INT i = 0; i < n; i++)
            out[i] = gp_unpremultiply(p[i]);
        break;
    }
    case PixelFormat32bppRGB: {
        const UINT32 *p = (const UINT32 *)row + x;
        for (INT i = 0; i < n; i++)
            out[i] = p[i] | 0xff000000u;
        break;
    }
    case PixelFormat24bppRGB: {
        const BYTE *p = row + x * 3;
        for (INT i = 0; i < n; i++, p += 3)
            out[i] = 0xff000000u | (UINT32)p[2] << 16 | (UINT32)p[1] << 8 | p[0];
        break;
    }
    case PixelFormat16bppRGB555: {
        const BYTE *p = row + x * 2;
        for (INT i = 0; i < n; i++, p += 2) {
            UINT32 v = p[0] | (UINT32)p[1] << 8;
            out[i] = 0xff000000u | expand5((v >> 10) & 31) << 16 | expand5((v >> 5) & 31) << 8 | expand5(v & 31);
        }
        break;
    }
    case PixelFormat16bppRGB565: {
        const BYTE *p = row + x * 2;
        for (INT i = 0; i < n; i++, p += 2) {
            UINT32 v = p[0] | (UINT32)p[1] << 8;
            out[i] = 0xff000000u | expand5(v >> 11) << 16 | expand6((v >> 5) & 63) << 8 | expand5(v & 31);
        }
        break;
    }
    case PixelFormat16bppARGB1555: {
        const BYTE *p = row + x * 2;
        for (INT i = 0; i < n; i++, p += 2) {
            UINT32 v = p[0] | (UINT32)p[1] << 8;
            out[i] = ((v & 0x8000) ? 0xff000000u : 0) | expand5((v >> 10) & 31) << 16 | expand5((v >> 5) & 31) << 8 |
                     expand5(v & 31);
        }
        break;
    }
    case PixelFormat16bppGrayScale: {
        const USHORT *p = (const USHORT *)row + x;
        for (INT i = 0; i < n; i++) {
            UINT32 g = narrow16(p[i]);
            out[i] = 0xff000000u | g << 16 | g << 8 | g;
        }
        break;
    }
    case PixelFormat48bppRGB: {
        const USHORT *p = (const USHORT *)row + x * 3;
        for (INT i = 0; i < n; i++, p += 3)
            out[i] = 0xff000000u | narrow16(p[2]) << 16 | narrow16(p[1]) << 8 | narrow16(p[0]);
        break;
    }
    case PixelFormat64bppARGB: {
        const USHORT *p = (const USHORT *)row + x * 4;
        for (INT i = 0; i < n; i++, p += 4)
            out[i] = narrow16(p[3]) << 24 | narrow16(p[2]) << 16 | narrow16(p[1]) << 8 | narrow16(p[0]);
        break;
    }
    case PixelFormat64bppPARGB: {
        const USHORT *p = (const USHORT *)row + x * 4;
        for (INT i = 0; i < n; i++, p += 4) {
            UINT32 a = narrow16(p[3]);
            UINT32 c = a << 24 | narrow16(p[2]) << 16 | narrow16(p[1]) << 8 | narrow16(p[0]);
            out[i] = gp_unpremultiply(c);
        }
        break;
    }
    case PixelFormat32bppCMYK: {
        const BYTE *p = row + x * 4;
        for (INT i = 0; i < n; i++, p += 4) {
            UINT32 k = 255 - p[3];
            UINT32 r = (255 - p[0]) * k / 255, g = (255 - p[1]) * k / 255, b = (255 - p[2]) * k / 255;
            out[i] = 0xff000000u | r << 16 | g << 8 | b;
        }
        break;
    }
    case PixelFormat8bppIndexed:
        for (INT i = 0; i < n; i++)
            out[i] = palette_lookup(pal, row[x + i]);
        break;
    case PixelFormat4bppIndexed:
        for (INT i = 0; i < n; i++) {
            INT px = x + i;
            BYTE v = row[px >> 1];
            out[i] = palette_lookup(pal, (px & 1) ? (v & 15) : (v >> 4));
        }
        break;
    case PixelFormat1bppIndexed:
        for (INT i = 0; i < n; i++) {
            INT px = x + i;
            out[i] = palette_lookup(pal, (row[px >> 3] >> (7 - (px & 7))) & 1);
        }
        break;
    default:
        for (INT i = 0; i < n; i++)
            out[i] = 0;
    }
}

void gp_row_from_argb(BYTE *row, PixelFormat f, const ColorPalette *pal, INT x, INT n, const UINT32 *in)
{
    switch (f) {
    case PixelFormat32bppARGB: {
        UINT32 *p = (UINT32 *)row + x;
        for (INT i = 0; i < n; i++)
            p[i] = in[i];
        break;
    }
    case PixelFormat32bppPARGB: {
        UINT32 *p = (UINT32 *)row + x;
        for (INT i = 0; i < n; i++)
            p[i] = gp_premultiply(in[i]);
        break;
    }
    case PixelFormat32bppRGB: {
        UINT32 *p = (UINT32 *)row + x;
        for (INT i = 0; i < n; i++)
            p[i] = in[i] | 0xff000000u;
        break;
    }
    case PixelFormat24bppRGB: {
        BYTE *p = row + x * 3;
        for (INT i = 0; i < n; i++, p += 3) {
            p[0] = (BYTE)in[i];
            p[1] = (BYTE)(in[i] >> 8);
            p[2] = (BYTE)(in[i] >> 16);
        }
        break;
    }
    case PixelFormat16bppRGB555: {
        BYTE *p = row + x * 2;
        for (INT i = 0; i < n; i++, p += 2) {
            UINT32 v = pack555((in[i] >> 16) & 255, (in[i] >> 8) & 255, in[i] & 255);
            p[0] = (BYTE)v;
            p[1] = (BYTE)(v >> 8);
        }
        break;
    }
    case PixelFormat16bppRGB565: {
        BYTE *p = row + x * 2;
        for (INT i = 0; i < n; i++, p += 2) {
            UINT32 v = (((in[i] >> 16) & 255) >> 3) << 11 | (((in[i] >> 8) & 255) >> 2) << 5 | ((in[i] & 255) >> 3);
            p[0] = (BYTE)v;
            p[1] = (BYTE)(v >> 8);
        }
        break;
    }
    case PixelFormat16bppARGB1555: {
        BYTE *p = row + x * 2;
        for (INT i = 0; i < n; i++, p += 2) {
            UINT32 v = ((in[i] >> 24) >= 128 ? 0x8000u : 0) | pack555((in[i] >> 16) & 255, (in[i] >> 8) & 255, in[i] & 255);
            p[0] = (BYTE)v;
            p[1] = (BYTE)(v >> 8);
        }
        break;
    }
    case PixelFormat16bppGrayScale: {
        USHORT *p = (USHORT *)row + x;
        for (INT i = 0; i < n; i++) {
            /* gdi+ uses the usual 0.299, 0.587 and 0.114 gray weights. */
            UINT32 g = (((in[i] >> 16) & 255) * 299 + ((in[i] >> 8) & 255) * 587 + (in[i] & 255) * 114) / 1000;
            p[i] = (USHORT)widen8(g);
        }
        break;
    }
    case PixelFormat48bppRGB: {
        USHORT *p = (USHORT *)row + x * 3;
        for (INT i = 0; i < n; i++, p += 3) {
            p[0] = (USHORT)widen8(in[i] & 255);
            p[1] = (USHORT)widen8((in[i] >> 8) & 255);
            p[2] = (USHORT)widen8((in[i] >> 16) & 255);
        }
        break;
    }
    case PixelFormat64bppARGB: {
        USHORT *p = (USHORT *)row + x * 4;
        for (INT i = 0; i < n; i++, p += 4) {
            p[0] = (USHORT)widen8(in[i] & 255);
            p[1] = (USHORT)widen8((in[i] >> 8) & 255);
            p[2] = (USHORT)widen8((in[i] >> 16) & 255);
            p[3] = (USHORT)widen8(in[i] >> 24);
        }
        break;
    }
    case PixelFormat64bppPARGB: {
        USHORT *p = (USHORT *)row + x * 4;
        for (INT i = 0; i < n; i++, p += 4) {
            UINT32 c = gp_premultiply(in[i]);
            p[0] = (USHORT)widen8(c & 255);
            p[1] = (USHORT)widen8((c >> 8) & 255);
            p[2] = (USHORT)widen8((c >> 16) & 255);
            p[3] = (USHORT)widen8(c >> 24);
        }
        break;
    }
    case PixelFormat32bppCMYK: {
        BYTE *p = row + x * 4;
        for (INT i = 0; i < n; i++, p += 4) {
            UINT32 r = (in[i] >> 16) & 255, g = (in[i] >> 8) & 255, b = in[i] & 255;
            UINT32 m = r > g ? (r > b ? r : b) : (g > b ? g : b);
            UINT32 k = 255 - m;
            if (m == 0) {
                p[0] = p[1] = p[2] = 0;
            } else {
                p[0] = (BYTE)(255 - r * 255 / m);
                p[1] = (BYTE)(255 - g * 255 / m);
                p[2] = (BYTE)(255 - b * 255 / m);
            }
            p[3] = (BYTE)k;
        }
        break;
    }
    case PixelFormat8bppIndexed:
        for (INT i = 0; i < n; i++)
            row[x + i] = (BYTE)nearest_entry(pal, in[i]);
        break;
    case PixelFormat4bppIndexed:
        for (INT i = 0; i < n; i++) {
            INT px = x + i;
            BYTE e = (BYTE)nearest_entry(pal, in[i]);
            if (px & 1)
                row[px >> 1] = (row[px >> 1] & 0xf0) | (e & 15);
            else
                row[px >> 1] = (row[px >> 1] & 0x0f) | (BYTE)(e << 4);
        }
        break;
    case PixelFormat1bppIndexed:
        for (INT i = 0; i < n; i++) {
            INT px = x + i;
            BYTE e = (BYTE)(nearest_entry(pal, in[i]) & 1);
            BYTE bit = (BYTE)(0x80 >> (px & 7));
            if (e)
                row[px >> 3] |= bit;
            else
                row[px >> 3] &= (BYTE)~bit;
        }
        break;
    default:
        break;
    }
}

void gp_convert_row(BYTE *dst, PixelFormat dst_format, const ColorPalette *dst_palette, const BYTE *src,
                    PixelFormat src_format, const ColorPalette *src_palette, INT count)
{
    if (dst_format == src_format && GetPixelFormatSize(src_format) >= 8) {
        memmove(dst, src, (SIZE_T)count * GetPixelFormatSize(src_format) / 8);
        return;
    }
    UINT32 tmp[256];
    for (INT done = 0; done < count; done += 256) {
        INT n = count - done > 256 ? 256 : count - done;
        gp_row_to_argb(src, src_format, src_palette, done, n, tmp);
        gp_row_from_argb(dst, dst_format, dst_palette, done, n, tmp);
    }
}
