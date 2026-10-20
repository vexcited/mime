/*
 * loading a picture from a stream: bitmap files, icons and cursors, windows and enhanced metafiles, gif and jpeg.
 * The format is told by the first word of the data, as it is by the reference; png has no word it knows.
 */
#include "picture.h"

WINUSERAPI HICON WINAPI CreateIconFromResource(PBYTE bits, DWORD size, BOOL icon, DWORD version);

static ULONG le16(const BYTE *p)
{
    return p[0] | p[1] << 8;
}

static ULONG le32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

/* ------------------------------------------------------------------ sizes */

HRESULT PicStateSize(PICSTATE *s)
{
    BITMAP bitmap = {};
    ENHMETAHEADER header = {};
    switch (s->type) {
    case PICTYPE_NONE:
        return S_OK;
    case PICTYPE_BITMAP:
        GetObjectW(s->bitmap, sizeof(bitmap), &bitmap);
        s->pixels.cx = bitmap.bmWidth;
        s->pixels.cy = bitmap.bmHeight;
        s->himetric.cx = s->himetric.cy = 0;
        if (GetBitmapDimensionEx(s->bitmap, &s->himetric) && s->himetric.cx)
            return S_OK;
        s->himetric = s->pixels;
        return SUCCEEDED(PicHimetric(&s->himetric)) ? S_OK : E_FAIL;
    case PICTYPE_METAFILE:
        s->pixels.cx = s->pixels.cy = 0;
        if (s->xext <= 0)
            return E_FAIL;
        /* the extents of a metafile come in twips. */
        s->himetric.cx = (LONG)AutRoundDiv64((LONGLONG)2540 * s->xext, 1440);
        s->himetric.cy = (LONG)AutRoundDiv64((LONGLONG)2540 * s->yext, 1440);
        return S_OK;
    case PICTYPE_ICON:
        if (GetObjectW(s->and_bmp, sizeof(bitmap), &bitmap)) {
            s->pixels.cx = bitmap.bmWidth;
            s->pixels.cy = bitmap.bmHeight;
        } else {
            s->pixels.cx = GetSystemMetrics(SM_CXICON);
            s->pixels.cy = GetSystemMetrics(SM_CYICON);
        }
        s->himetric = s->pixels;
        return SUCCEEDED(PicHimetric(&s->himetric)) ? S_OK : E_FAIL;
    case PICTYPE_ENHMETAFILE:
        s->pixels.cx = s->pixels.cy = 0;
        if (!GetEnhMetaFileHeader(s->emf, sizeof(header), &header))
            return E_FAIL;
        s->himetric.cx = header.rclFrame.right - header.rclFrame.left;
        s->himetric.cy = header.rclFrame.bottom - header.rclFrame.top;
        return S_OK;
    default:
        return E_UNEXPECTED;
    }
}

void PicStateFree(PICSTATE *s, BOOL own_gdi, BOOL own_handles)
{
    if (own_gdi) {
        if (s->bitmap && s->type == PICTYPE_BITMAP)
            DeleteObject(s->bitmap);
        if (s->palette)
            DeleteObject(s->palette);
    }
    if (s->mask)
        DeleteObject(s->mask);
    if (own_handles) {
        if (s->type == PICTYPE_METAFILE && s->wmf)
            DeleteMetaFile(s->wmf);
        else if (s->type == PICTYPE_ICON && s->icon)
            DestroyIcon(s->icon);
        else if (s->type == PICTYPE_ENHMETAFILE && s->emf)
            DeleteEnhMetaFile(s->emf);
    }
    if (s->and_bmp)
        DeleteObject(s->and_bmp);
    if (s->xor_bmp)
        DeleteObject(s->xor_bmp);
    memset(s, 0, sizeof(*s));
    s->type = PIC_UNSET;
}

/* ------------------------------------------------------------------ bitmaps */

typedef struct {
    BITMAPINFOHEADER header;
    RGBQUAD colors[256];
} DIB_INFO;

/* a bottom up bitmap with its bits in the process, for the caller to fill. */
static HBITMAP pic_dib(int width, int height, int bpp, const RGBQUAD *colors, int count, BYTE **bits, ULONG *stride)
{
    DIB_INFO info;
    memset(&info, 0, sizeof(info));
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = height;
    info.header.biPlanes = 1;
    info.header.biBitCount = (WORD)bpp;
    if (bpp <= 8 && colors) {
        info.header.biClrUsed = count;
        memcpy(info.colors, colors, (SIZE_T)count * sizeof(RGBQUAD));
    }
    void *memory = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, (BITMAPINFO *)&info, DIB_RGB_COLORS, &memory, nullptr, 0);
    if (!bitmap)
        return nullptr;
    *bits = memory;
    *stride = (ULONG)(((ULONGLONG)width * bpp + 31) / 32 * 4);
    return bitmap;
}

/* run length encoded rows into one index a byte, the first row of the data at the bottom. */
static void pic_rle(const BYTE *src, ULONG size, int w, int h, BOOL rle4, BYTE *indices)
{
    int x = 0, y = 0;
    ULONG i = 0;
    while (i + 1 < size) {
        int count = src[i], value = src[i + 1];
        i += 2;
        if (count) {
            for (int k = 0; k < count; k++, x++) {
                if (x < w && y < h)
                    indices[(SIZE_T)y * w + x] = (BYTE)(rle4 ? ((k & 1) ? value & 15 : value >> 4) : value);
            }
        } else if (value == 0) {
            x = 0;
            y++;
        } else if (value == 1) {
            return;
        } else if (value == 2) {
            if (i + 1 >= size)
                return;
            x += src[i];
            y += src[i + 1];
            i += 2;
        } else {
            ULONG bytes = rle4 ? (ULONG)(value + 1) / 2 : (ULONG)value;
            for (int k = 0; k < value; k++, x++) {
                ULONG at = i + (rle4 ? (ULONG)k / 2 : (ULONG)k);
                if (at >= size)
                    return;
                if (x < w && y < h)
                    indices[(SIZE_T)y * w + x] =
                        (BYTE)(rle4 ? ((k & 1) ? src[at] & 15 : src[at] >> 4) : src[at]);
            }
            i += bytes + (bytes & 1);
        }
    }
}

static BYTE pic_channel(ULONG value, ULONG mask)
{
    if (!mask)
        return 0;
    int shift = __builtin_ctz(mask);
    ULONG range = mask >> shift;
    ULONG part = (value & mask) >> shift;
    return (BYTE)(part * 255 / range);
}

/*
 * a bitmap file. The header is the 40 byte one or the 12 byte one of os/2 and nothing else, 16 bit pixels become
 * 24 bit ones, and run length encoding is undone here.
 */
static HRESULT pic_load_bmp(const BYTE *data, ULONG got, ULONG length, PICSTATE *s)
{
    if (got < 54)
        return E_UNEXPECTED;
    if (length < 14)
        return E_INVALIDARG;
    if (got < length)
        return E_UNEXPECTED;
    ULONG offset = le32(data + 10);
    if (length < offset)
        return E_INVALIDARG;
    const BYTE *hdr = data + 14;
    ULONG hsize = length - 14;
    ULONG dsize = le32(hdr);
    BOOL win;
    if (hsize < 12 || (hsize < 40 && dsize != 12))
        return CTL_E_INVALIDPICTURE;
    if (hsize >= 40 && dsize == 40)
        win = TRUE;
    else if (dsize == 12)
        win = FALSE;
    else
        return CTL_E_INVALIDPICTURE;
    LONG width, height;
    int bpp, entries = 0;
    ULONG comp = 0, table = 0, size_image = 0;
    BOOL top_down = FALSE;
    ULONG bits_at = offset - 14;
    if (win) {
        width = (LONG)le32(hdr + 4);
        height = (LONG)le32(hdr + 8);
        if (height < 0) {
            if (height == (LONG)0x80000000)
                return CTL_E_INVALIDPICTURE;
            height = -height;
            top_down = TRUE;
        }
        if (width < 0 || le16(hdr + 12) != 1)
            return CTL_E_INVALIDPICTURE;
        bpp = (int)le16(hdr + 14);
        comp = le32(hdr + 16);
        size_image = le32(hdr + 20);
        ULONG used = le32(hdr + 32);
        switch (bpp) {
        case 1: entries = 2; table = 8; break;
        case 4: entries = 16; table = 64; break;
        case 8: entries = 256; table = 1024; break;
        case 16:
            table = comp == 3 ? 12 : 0;
            used = 0;
            break;
        case 24: used = 0; break;
        case 32: break;
        default: return CTL_E_INVALIDPICTURE;
        }
        if (used) {
            if (used > (bpp >= 32 ? 0xFFFFFFFFu : 1u << bpp))
                return CTL_E_INVALIDPICTURE;
            entries = used > 256 ? 256 : (int)used;
            table = 4 * used;
        }
    } else {
        width = (LONG)le16(hdr + 4);
        height = (LONG)le16(hdr + 6);
        if (le16(hdr + 8) != 1)
            return CTL_E_INVALIDPICTURE;
        bpp = (int)le16(hdr + 10);
        switch (bpp) {
        case 1: entries = 2; table = 6; break;
        case 4: entries = 16; table = 48; break;
        case 8: entries = 256; table = 768; break;
        case 24: break;
        default: return CTL_E_INVALIDPICTURE;
        }
    }
    if ((ULONGLONG)table + dsize > bits_at)
        return CTL_E_INVALIDPICTURE;
    BOOL rle = win && (comp == 1 || comp == 2);
    if (win && comp != 0 && comp != 3 && !rle)
        return CTL_E_INVALIDPICTURE;
    if (rle && ((comp == 1 && bpp != 8) || (comp == 2 && bpp != 4) || top_down))
        return CTL_E_INVALIDPICTURE;
    ULONGLONG row = ((ULONGLONG)width * bpp + 31) / 32 * 4;
    ULONG avail = length - offset;
    if (rle) {
        if (!size_image || size_image > avail)
            return CTL_E_INVALIDPICTURE;
    } else if (row * height + bits_at > hsize) {
        return CTL_E_INVALIDPICTURE;
    }
    if (width > 0x7FFF || height > 0x7FFF)
        return CTL_E_INVALIDPICTURE;
    /* the colors of the file become the table of the bitmap. */
    RGBQUAD colors[256] = {};
    int count = entries > 256 ? 256 : entries;
    if (bpp <= 8) {
        if (!win) {
            for (int i = 0; i < count; i++) {
                colors[i].rgbBlue = hdr[12 + 3 * i];
                colors[i].rgbGreen = hdr[13 + 3 * i];
                colors[i].rgbRed = hdr[14 + 3 * i];
            }
        } else {
            for (int i = 0; i < count; i++) {
                colors[i].rgbBlue = hdr[dsize + 4 * i];
                colors[i].rgbGreen = hdr[dsize + 4 * i + 1];
                colors[i].rgbRed = hdr[dsize + 4 * i + 2];
            }
        }
    }
    if (!width || !height)
        return E_OUTOFMEMORY;
    int target = bpp == 16 ? 24 : bpp;
    BYTE *bits;
    ULONG stride;
    HBITMAP bitmap = pic_dib(width, height, target, colors, count, &bits, &stride);
    if (!bitmap)
        return E_OUTOFMEMORY;
    const BYTE *src = data + offset;
    if (rle) {
        BYTE *indices = AutAllocZero((SIZE_T)width * height);
        if (!indices) {
            DeleteObject(bitmap);
            return E_OUTOFMEMORY;
        }
        pic_rle(src, size_image, width, height, comp == 2, indices);
        for (int y = 0; y < height; y++) {
            BYTE *out = bits + (SIZE_T)y * stride;
            const BYTE *in = indices + (SIZE_T)y * width;
            if (bpp == 8) {
                memcpy(out, in, width);
            } else {
                for (int x = 0; x < width; x++)
                    out[x / 2] |= (BYTE)(x & 1 ? in[x] & 15 : in[x] << 4);
            }
        }
        AutFree(indices);
    } else {
        ULONG masks[3] = {0x7C00, 0x03E0, 0x001F};
        BOOL custom = FALSE;
        if (win && comp == 3 && (bpp == 16 || bpp == 32) && bits_at >= 52) {
            for (int i = 0; i < 3; i++)
                masks[i] = le32(hdr + 40 + 4 * i);
            if (bpp == 32)
                custom = masks[0] != 0x00FF0000 || masks[1] != 0x0000FF00 || masks[2] != 0x000000FF;
        } else if (bpp == 32) {
            masks[0] = 0x00FF0000;
            masks[1] = 0x0000FF00;
            masks[2] = 0x000000FF;
        }
        for (int f = 0; f < height; f++) {
            const BYTE *in = src + (SIZE_T)f * row;
            BYTE *out = bits + (SIZE_T)(top_down ? height - 1 - f : f) * stride;
            if (bpp == 16) {
                for (int x = 0; x < width; x++) {
                    ULONG v = le16(in + 2 * x);
                    out[3 * x] = pic_channel(v, masks[2]);
                    out[3 * x + 1] = pic_channel(v, masks[1]);
                    out[3 * x + 2] = pic_channel(v, masks[0]);
                }
            } else if (bpp == 32 && custom) {
                for (int x = 0; x < width; x++) {
                    ULONG v = le32(in + 4 * x);
                    out[4 * x] = pic_channel(v, masks[2]);
                    out[4 * x + 1] = pic_channel(v, masks[1]);
                    out[4 * x + 2] = pic_channel(v, masks[0]);
                    out[4 * x + 3] = 0;
                }
            } else {
                memcpy(out, in, (SIZE_T)((ULONGLONG)width * bpp + 7) / 8);
            }
        }
    }
    s->type = PICTYPE_BITMAP;
    s->bitmap = bitmap;
    return S_OK;
}

/* a decoded gif or jpeg as a bitmap, and for a gif with a transparent color a mask of that color. */
static HRESULT pic_from_image(const AUT_IMAGE *image, PICSTATE *s)
{
    RGBQUAD colors[256] = {};
    if (image->bpp == 8) {
        for (int i = 0; i < 256; i++) {
            colors[i].rgbBlue = image->palette[i][0];
            colors[i].rgbGreen = image->palette[i][1];
            colors[i].rgbRed = image->palette[i][2];
        }
    }
    BYTE *bits;
    ULONG stride;
    int count = image->bpp == 8 ? (image->colors < 2 ? 2 : image->colors) : 0;
    HBITMAP bitmap = pic_dib(image->width, image->height, image->bpp, colors, count, &bits, &stride);
    if (!bitmap)
        return E_OUTOFMEMORY;
    int bytes = image->bpp / 8;
    for (int y = 0; y < image->height; y++)
        memcpy(bits + (SIZE_T)(image->height - 1 - y) * stride, image->pixels + (SIZE_T)y * image->width * bytes,
               (SIZE_T)image->width * bytes);
    HBITMAP mask = nullptr;
    if (image->transparent >= 0) {
        ULONG mask_stride = (ULONG)((image->width + 15) / 16 * 2);
        BYTE *mask_bits = AutAllocZero((SIZE_T)mask_stride * image->height);
        if (!mask_bits) {
            DeleteObject(bitmap);
            return E_OUTOFMEMORY;
        }
        for (int y = 0; y < image->height; y++) {
            for (int x = 0; x < image->width; x++) {
                if (image->pixels[(SIZE_T)y * image->width + x] == image->transparent)
                    mask_bits[(SIZE_T)y * mask_stride + x / 8] |= (BYTE)(0x80 >> (x & 7));
            }
        }
        mask = CreateBitmap(image->width, image->height, 1, 1, mask_bits);
        AutFree(mask_bits);
        if (!mask) {
            DeleteObject(bitmap);
            return E_OUTOFMEMORY;
        }
    }
    s->type = PICTYPE_BITMAP;
    s->bitmap = bitmap;
    s->mask = mask;
    s->transparent = mask != nullptr;
    return S_OK;
}

static HRESULT pic_load_gif(const BYTE *data, ULONG got, PICSTATE *s)
{
    AUT_IMAGE image;
    if (!AutDecodeGif(data, got, &image))
        return CTL_E_INVALIDPICTURE;
    HRESULT hr = pic_from_image(&image, s);
    AutImageFree(&image);
    return hr;
}

static HRESULT pic_load_jpeg(const BYTE *data, ULONG got, PICSTATE *s)
{
    AUT_IMAGE image;
    if (!AutDecodeJpeg(data, got, &image))
        return CTL_E_INVALIDPICTURE;
    HRESULT hr = pic_from_image(&image, s);
    AutImageFree(&image);
    return hr;
}

/* ------------------------------------------------------------------ icons */

/* the two bitmaps an icon is drawn with: its mask and its colors. */
HRESULT PicIconBitmaps(HICON icon, HBITMAP *and_bmp, HBITMAP *xor_bmp)
{
    ICONINFO info = {};
    int cx = 0, cy = 0;
    if (GetIconInfo(icon, &info)) {
        if (info.hbmMask && info.hbmColor) {
            *and_bmp = info.hbmMask;
            *xor_bmp = info.hbmColor;
            return S_OK;
        }
        BITMAP bitmap = {};
        if (info.hbmMask && GetObjectW(info.hbmMask, sizeof(bitmap), &bitmap)) {
            cx = bitmap.bmWidth;
            cy = bitmap.bmHeight / 2;
        }
        if (info.hbmMask)
            DeleteObject(info.hbmMask);
        if (info.hbmColor)
            DeleteObject(info.hbmColor);
    }
    if (!cx || !cy) {
        cx = GetSystemMetrics(SM_CXICON);
        cy = GetSystemMetrics(SM_CYICON);
    }
    /* the icon drawn on black gives its colors, and on white against black gives the mask. */
    HDC screen = GetDC(nullptr);
    if (!screen)
        return E_FAIL;
    HDC color_dc = CreateCompatibleDC(screen), mask_dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, cx, cy), mask = CreateBitmap(cx, cy, 1, 1, nullptr);
    ReleaseDC(nullptr, screen);
    BOOL ok = color_dc && mask_dc && color && mask;
    if (ok) {
        HGDIOBJ old_color = SelectObject(color_dc, color), old_mask = SelectObject(mask_dc, mask);
        ok = PatBlt(color_dc, 0, 0, cx, cy, BLACKNESS) && DrawIcon(color_dc, 0, 0, icon) &&
             PatBlt(mask_dc, 0, 0, cx, cy, WHITENESS) && DrawIcon(mask_dc, 0, 0, icon) &&
             BitBlt(mask_dc, 0, 0, cx, cy, color_dc, 0, 0, SRCINVERT);
        SelectObject(color_dc, old_color);
        SelectObject(mask_dc, old_mask);
    }
    if (color_dc)
        DeleteDC(color_dc);
    if (mask_dc)
        DeleteDC(mask_dc);
    if (!ok) {
        if (color)
            DeleteObject(color);
        if (mask)
            DeleteObject(mask);
        return E_FAIL;
    }
    *and_bmp = mask;
    *xor_bmp = color;
    return S_OK;
}

/* an icon or cursor file: the entry that fits the size and the colors asked for becomes the icon. */
static HRESULT pic_load_icon(const BYTE *data, ULONG got, DWORD cx, DWORD cy, DWORD flags, PICSTATE *s)
{
    if (got < 6)
        return E_UNEXPECTED;
    ULONG type = le16(data + 2), count = le16(data + 4);
    if (!count || (type - 1) > 1)
        return CTL_E_INVALIDPICTURE;
    BOOL cursor = type == 2;
    ULONG limit;
    if (flags & 7) {
        limit = (flags & LP_MONOCHROME) ? 2 : (flags & LP_VGACOLOR) ? 16 : 255;
    } else {
        HDC dc = PicDc();
        int bits = dc ? GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES) : 32;
        limit = bits < 31 ? 1u << bits : 0x7FFFFFFF;
    }
    ULONG want_x = cx, want_y = cy;
    if (!want_x && !want_y) {
        want_x = (ULONG)GetSystemMetrics(SM_CXICON);
        want_y = (ULONG)GetSystemMetrics(SM_CYICON);
    }
    const BYTE *best = nullptr;
    ULONG best_under = 0, best_over = 0, exact_under = 0;
    for (ULONG i = 0; i < count; i++) {
        if (got < 6 + 16 * (ULONGLONG)(i + 1))
            return E_UNEXPECTED;
        const BYTE *e = data + 6 + 16 * i;
        ULONG colors = e[2] ? e[2] : 255;
        if (!i)
            best = e;
        if (!best_over && !exact_under && colors <= limit && colors > best_under) {
            best_under = colors;
            best = e;
        }
        if (e[0] != want_x || e[1] != want_y)
            continue;
        if (colors == limit) {
            best = e;
            break;
        }
        if (colors < limit) {
            if (colors > exact_under) {
                exact_under = colors;
                best = e;
            }
        } else if (!exact_under && (!best_over || colors < best_over)) {
            best_over = colors;
            best = e;
        }
    }
    ULONG offset = le32(best + 12), size = le32(best + 8);
    if (size < 0x2C)
        return CTL_E_INVALIDPICTURE;
    if ((ULONGLONG)offset + size > got)
        return E_UNEXPECTED;
    const BYTE *image = data + offset;
    if (le32(image) != 40)
        return CTL_E_INVALIDPICTURE;
    LONG width = (LONG)le32(image + 4), height = (LONG)le32(image + 8);
    ULONG comp = le32(image + 16), used = le32(image + 32);
    ULONG bpp = le16(image + 14);
    if (width <= 0 || height == 0 || le16(image + 12) != 1 || (height < 0 && comp != 0 && comp != 3) ||
        (used > 256 && bpp == 8) || (comp == 3 && bpp != 16 && bpp != 32))
        return CTL_E_INVALIDPICTURE;
    BYTE *copy = AutAlloc((SIZE_T)size + 4);
    if (!copy)
        return E_OUTOFMEMORY;
    DWORD copy_size = size;
    BYTE *resource = copy;
    if (cursor) {
        /* a cursor resource starts with the hotspot. */
        memcpy(copy, best + 4, 4);
        resource = copy;
        copy_size = size + 4;
        memcpy(copy + 4, image, size);
    } else {
        memcpy(copy, image, size);
    }
    HICON icon = CreateIconFromResource(resource, copy_size, !cursor, 0x30000);
    AutFree(copy);
    if (!icon)
        return E_OUTOFMEMORY;
    HBITMAP and_bmp, xor_bmp;
    HRESULT hr = PicIconBitmaps(icon, &and_bmp, &xor_bmp);
    if (FAILED(hr)) {
        DestroyIcon(icon);
        return hr;
    }
    s->type = PICTYPE_ICON;
    s->icon = icon;
    s->and_bmp = and_bmp;
    s->xor_bmp = xor_bmp;
    return S_OK;
}

/* ------------------------------------------------------------------ metafiles */

typedef struct {
    LONG left, top, right, bottom;
} WMF_BOX;

/*
 * the bits of a windows metafile. A placeable one gets the window origin and extent records the box gives, unless the
 * metafile starts with them, so it plays into any rectangle.
 */
static HRESULT pic_load_wmf_bits(const BYTE *data, ULONG size, const WMF_BOX *box, PICSTATE *s)
{
    if (size < 18)
        return E_OUTOFMEMORY;
    ULONG header_size = le16(data + 2), meta_size = le32(data + 6);
    if (header_size != 9 || meta_size - 9 > 0x7FFFFFF6u)
        return CTL_E_INVALIDPICTURE;
    BOOL present = size >= 38 && le32(data + 18) == 5 && le16(data + 22) == 0x20B && le32(data + 28) == 5 &&
                   le16(data + 32) == 0x20C;
    BOOL insert = box && !present;
    SIZE_T total = (SIZE_T)size + (insert ? 20 : 0);
    BYTE *buffer = AutAlloc(total);
    if (!buffer)
        return E_OUTOFMEMORY;
    memcpy(buffer, data, 18);
    SIZE_T at = 18;
    if (insert) {
        BYTE *p = buffer + 18;
        LONG cy = box->bottom - box->top, cx = box->right - box->left;
        ULONG words = meta_size + 10;
        /* the window origin and then the window extent, five words each. */
        static const BYTE origin[6] = {5, 0, 0, 0, 0x0B, 0x02};
        static const BYTE extent[6] = {5, 0, 0, 0, 0x0C, 0x02};
        memcpy(p, origin, 6);
        p[6] = (BYTE)box->top, p[7] = (BYTE)(box->top >> 8);
        p[8] = (BYTE)box->left, p[9] = (BYTE)(box->left >> 8);
        memcpy(p + 10, extent, 6);
        p[16] = (BYTE)cy, p[17] = (BYTE)(cy >> 8);
        p[18] = (BYTE)cx, p[19] = (BYTE)(cx >> 8);
        buffer[6] = (BYTE)words, buffer[7] = (BYTE)(words >> 8);
        buffer[8] = (BYTE)(words >> 16), buffer[9] = (BYTE)(words >> 24);
        at = 38;
    }
    memcpy(buffer + at, data + 18, size - 18);
    HMETAFILE wmf = SetMetaFileBitsEx((UINT)total, buffer);
    AutFree(buffer);
    if (!wmf)
        return CTL_E_INVALIDPICTURE;
    s->type = PICTYPE_METAFILE;
    s->wmf = wmf;
    s->xext = s->yext = 0;
    return S_OK;
}

/* a placeable metafile: a signature, the box and resolution, then the metafile. */
static HRESULT pic_load_placeable(const BYTE *data, ULONG got, ULONG length, PICSTATE *s)
{
    if (got < length)
        return E_UNEXPECTED;
    if (length < 22 || le32(data) != 0x9AC6CDD7)
        return CTL_E_INVALIDPICTURE;
    const BYTE *header = data + 4;
    WMF_BOX box = {(SHORT)le16(header + 2), (SHORT)le16(header + 4), (SHORT)le16(header + 6), (SHORT)le16(header + 8)};
    ULONG inch = le16(header + 10);
    HRESULT hr = pic_load_wmf_bits(data + 22, length - 22, &box, s);
    if (FAILED(hr))
        return hr;
    LONG cy = box.bottom - box.top, cx = box.right - box.left;
    if (inch) {
        s->xext = (int)((LONGLONG)1440 * cx / (LONG)inch);
        s->yext = (int)((LONGLONG)1440 * cy / (LONG)inch);
    } else {
        s->xext = cx;
        s->yext = cy;
    }
    return S_OK;
}

/* a metafile without a placeable header, or an enhanced one. */
static HRESULT pic_load_metafile(const BYTE *data, ULONG got, ULONG length, PICSTATE *s)
{
    if (got < 44)
        return E_UNEXPECTED;
    if (le32(data + 40) == 0x464D4520) {
        if (got < length)
            return E_UNEXPECTED;
        HENHMETAFILE emf = SetEnhMetaFileBits(length, data);
        if (!emf)
            return E_OUTOFMEMORY;
        s->type = PICTYPE_ENHMETAFILE;
        s->emf = emf;
        return S_OK;
    }
    if (got < length)
        return E_UNEXPECTED;
    return pic_load_wmf_bits(data, length, nullptr, s);
}

/* ------------------------------------------------------------------ the stream */

static HRESULT pic_read_exact(IStream *stream, void *buffer, ULONG size)
{
    ULONG got = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, buffer, size, &got);
    if (FAILED(hr))
        return hr;
    return got == size ? S_OK : E_UNEXPECTED;
}

/* up to length bytes of the stream, in a buffer that grows as the stream does. */
static HRESULT pic_read_all(IStream *stream, ULONG length, BYTE **data, ULONG *got)
{
    SIZE_T capacity = length < 65536 ? length : 65536;
    BYTE *buffer = AutAlloc(capacity ? capacity : 1);
    SIZE_T total = 0;
    if (!buffer)
        return E_OUTOFMEMORY;
    while (total < length) {
        if (total == capacity) {
            SIZE_T grown = capacity * 2 > length ? length : capacity * 2;
            BYTE *bigger = AutAlloc(grown);
            if (!bigger) {
                AutFree(buffer);
                return E_OUTOFMEMORY;
            }
            memcpy(bigger, buffer, total);
            AutFree(buffer);
            buffer = bigger;
            capacity = grown;
        }
        ULONG read = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, buffer + total, (ULONG)(capacity - total), &read);
        if (FAILED(hr)) {
            AutFree(buffer);
            return hr;
        }
        if (!read)
            break;
        total += read;
    }
    *data = buffer;
    *got = (ULONG)total;
    return S_OK;
}

/*
 * what the stream holds after an optional header of 'lt', two bytes and a length, in the length given or the rest of
 * the stream, when that is zero. The state is only touched when the picture is found.
 */
HRESULT PicLoad(IStream *stream, ULONG length, BOOL run_mode, DWORD cx, DWORD cy, DWORD flags, PICSTATE *state,
                BYTE **original, ULONG *original_length, BOOL *header_seen)
{
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER start = {}, end = {};
    WORD preamble = 0;
    BOOL header = FALSE;
    HRESULT hr;
    *original = nullptr;
    *original_length = 0;
    *header_seen = FALSE;
    memset(state, 0, sizeof(*state));
    state->type = PIC_UNSET;
    if (!stream)
        return E_POINTER;
    for (;;) {
        hr = stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &start);
        if (FAILED(hr))
            return hr;
        ULONG got = 0;
        hr = stream->lpVtbl->Read(stream, &preamble, 2, &got);
        if ((FAILED(hr) || got != 2) && FAILED(pic_read_exact(stream, &preamble, 2)))
            return CTL_E_INVALIDPICTURE;
        if (preamble != 0x746C)
            break;
        header = *header_seen = TRUE;
        WORD skip;
        hr = pic_read_exact(stream, &skip, 2);
        if (FAILED(hr))
            return hr;
        hr = pic_read_exact(stream, &length, 4);
        if (FAILED(hr))
            return hr;
        if (length == 0) {
            state->type = PICTYPE_NONE;
            return S_OK;
        }
    }
    if (!header && length == 0) {
        hr = stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_END, &end);
        if (FAILED(hr))
            return hr;
        if (end.HighPart != start.HighPart)
            return E_FAIL;
        length = end.LowPart - start.LowPart;
    }
    LARGE_INTEGER back;
    back.QuadPart = (LONGLONG)start.QuadPart;
    hr = stream->lpVtbl->Seek(stream, back, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr))
        return hr;
    if (length == 0) {
        state->type = PICTYPE_NONE;
        return S_OK;
    }
    if ((LONG)length < 0)
        return E_FAIL;
    BYTE *data;
    ULONG got;
    hr = pic_read_all(stream, length, &data, &got);
    if (FAILED(hr))
        return hr;
    /* the original format is kept whole, and a stream that ends early cannot be kept. */
    if (!run_mode && got < length) {
        AutFree(data);
        return E_UNEXPECTED;
    }
    PICSTATE loaded;
    memset(&loaded, 0, sizeof(loaded));
    switch (preamble) {
    case 0x0000: hr = pic_load_icon(data, got, cx, cy, flags, &loaded); break;
    case 0x0001: hr = pic_load_metafile(data, got, length, &loaded); break;
    case 0x4947:
    case 0x6967: hr = pic_load_gif(data, got, &loaded); break;
    case 0x4D42: hr = pic_load_bmp(data, got, length, &loaded); break;
    case 0xCDD7: hr = pic_load_placeable(data, got, length, &loaded); break;
    case 0xD8FF: hr = pic_load_jpeg(data, got, &loaded); break;
    default: hr = CTL_E_INVALIDPICTURE; break;
    }
    if (SUCCEEDED(hr)) {
        hr = PicStateSize(&loaded);
        if (FAILED(hr))
            PicStateFree(&loaded, TRUE, TRUE);
    }
    if (FAILED(hr)) {
        AutFree(data);
        return hr;
    }
    *state = loaded;
    if (!run_mode) {
        *original = data;
        *original_length = length;
    } else {
        AutFree(data);
    }
    return S_OK;
}
