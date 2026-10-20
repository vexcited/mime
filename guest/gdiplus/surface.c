/*
 * the surfaces a graphics draws on and the composition of source pixels onto them. a bitmap is drawn in its own
 * memory. a device context is drawn in the memory of the dib section selected into it when it has one, and
 * otherwise through a tile that is copied from the context, drawn on and copied back.
 *
 * a surface is a window of device space: row y is at bits + (y - surface.y) * stride and the pixel x of that row
 * is number x - surface.x.
 */
#include "gp_private.h"

void gp_graphics_bounds(const GGraphics *g, RECT *bounds)
{
    bounds->left = bounds->top = 0;
    bounds->right = g->width;
    bounds->bottom = g->height;
}

/* the memory of the dib section selected into a device context, when there is one a surface can use. */
static BOOL dc_dib(HDC hdc, BYTE **bits, INT *stride, INT *width, INT *height, PixelFormat *format)
{
    HGDIOBJ obj = GetCurrentObject(hdc, OBJ_BITMAP);
    DIBSECTION ds;
    if (!obj || GetObjectW(obj, sizeof(ds), &ds) != sizeof(ds) || !ds.dsBm.bmBits)
        return FALSE;
    if (ds.dsBmih.biCompression != BI_RGB && ds.dsBmih.biCompression != BI_BITFIELDS)
        return FALSE;
    INT w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight;
    INT row = ds.dsBm.bmWidthBytes;
    switch (ds.dsBm.bmBitsPixel) {
    case 32:
        *format = PixelFormat32bppRGB;
        break;
    case 24:
        *format = PixelFormat24bppRGB;
        break;
    case 16:
        *format = ds.dsBmih.biCompression == BI_BITFIELDS && ds.dsBitfields[1] == 0x07e0 ? PixelFormat16bppRGB565
                                                                                            : PixelFormat16bppRGB555;
        break;
    default:
        return FALSE;
    }
    BYTE *base = ds.dsBm.bmBits;
    if (ds.dsBmih.biHeight > 0) {
        /* bottom up: the first row in memory is the bottom row of the picture */
        *bits = base + (SSIZE_T)(h - 1) * row;
        *stride = -row;
    } else {
        *bits = base;
        *stride = row;
    }
    *width = w;
    *height = h;
    return TRUE;
}

GpStatus gp_surface_acquire(GGraphics *g, const RECT *area, GSurface *s)
{
    memset(s, 0, sizeof(*s));
    if (g->target) {
        GBitmap *b = g->target;
        s->format = b->format;
        s->palette = b->palette;
        s->stride = b->stride;
        s->bits = b->bits;
        s->width = b->width;
        s->height = b->height;
        s->direct = TRUE;
        return Ok;
    }
    if (!g->hdc)
        return WrongState;
    BYTE *bits;
    INT stride, w, h;
    PixelFormat format;
    if (dc_dib(g->hdc, &bits, &stride, &w, &h, &format)) {
        s->format = format;
        s->stride = stride;
        s->bits = bits;
        s->width = w;
        s->height = h;
        s->direct = TRUE;
        return Ok;
    }
    /* a tile of 32 bit pixels, with the contents of the context to start from */
    s->x = area->left;
    s->y = area->top;
    s->width = area->right - area->left;
    s->height = area->bottom - area->top;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = s->width;
    info.bmiHeader.biHeight = -s->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *tile = nullptr;
    s->dib = CreateDIBSection(g->hdc, &info, DIB_RGB_COLORS, (void **)&tile, nullptr, 0);
    if (!s->dib || !tile) {
        if (s->dib)
            DeleteObject(s->dib);
        s->dib = nullptr;
        return OutOfMemory;
    }
    s->mem = CreateCompatibleDC(g->hdc);
    if (!s->mem) {
        DeleteObject(s->dib);
        s->dib = nullptr;
        return OutOfMemory;
    }
    s->old = SelectObject(s->mem, s->dib);
    BitBlt(s->mem, 0, 0, s->width, s->height, g->hdc, area->left, area->top, SRCCOPY);
    s->hdc = g->hdc;
    s->bits = tile;
    s->stride = s->width * 4;
    s->format = PixelFormat32bppRGB;
    s->write_back = TRUE;
    return Ok;
}

void gp_surface_release(GGraphics *g, GSurface *s, BOOL modified)
{
    if (s->mem) {
        if (modified && s->write_back)
            BitBlt(s->hdc, s->x, s->y, s->width, s->height, s->mem, 0, 0, SRCCOPY);
        SelectObject(s->mem, s->old);
        DeleteDC(s->mem);
    }
    if (s->dib)
        DeleteObject(s->dib);
    memset(s, 0, sizeof(*s));
}

/* ------------------------------------------------------------------ rows of premultiplied pixels */

static BYTE *surface_row(const GSurface *s, INT y)
{
    return s->bits + (SSIZE_T)(y - s->y) * s->stride;
}

/* the destination pixels as premultiplied argb, count of them from x on row y. */
void gp_surface_read(const GSurface *s, INT x, INT y, INT count, UINT32 *out)
{
    BYTE *row = surface_row(s, y);
    INT px = x - s->x;
    if (s->format == PixelFormat32bppPARGB) {
        memcpy(out, (UINT32 *)row + px, (SIZE_T)count * 4);
        return;
    }
    gp_row_to_argb(row, s->format, s->palette, px, count, out);
    if (IsAlphaPixelFormat(s->format)) {
        for (INT i = 0; i < count; i++)
            out[i] = gp_premultiply(out[i]);
    }
}

void gp_surface_write(const GSurface *s, INT x, INT y, INT count, const UINT32 *pargb)
{
    BYTE *row = surface_row(s, y);
    INT px = x - s->x;
    if (s->format == PixelFormat32bppPARGB) {
        memcpy((UINT32 *)row + px, pargb, (SIZE_T)count * 4);
        return;
    }
    UINT32 tmp[256];
    for (INT done = 0; done < count; done += 256) {
        INT n = count - done > 256 ? 256 : count - done;
        if (IsAlphaPixelFormat(s->format)) {
            for (INT i = 0; i < n; i++)
                tmp[i] = gp_unpremultiply(pargb[done + i]);
        } else {
            for (INT i = 0; i < n; i++)
                tmp[i] = pargb[done + i] | 0xff000000u;
        }
        gp_row_from_argb(row, s->format, s->palette, px + done, n, tmp);
    }
}

/* the source over the destination, both premultiplied. */
static UINT32 over(UINT32 s, UINT32 d)
{
    UINT32 keep = 255 - (s >> 24);
    if (keep == 0)
        return s;
    if (keep == 255)
        return d;
    UINT32 out = 0;
    for (INT shift = 0; shift < 32; shift += 8) {
        UINT32 t = ((d >> shift) & 255) * keep + 128;
        out |= (((t + (t >> 8)) >> 8) + ((s >> shift) & 255)) << shift;
    }
    return out;
}

void gp_compose_over(const GSurface *s, INT x, INT y, INT count, const UINT32 *pargb)
{
    UINT32 dst[256];
    BYTE *row = surface_row(s, y);
    INT px = x - s->x;
    if (s->format == PixelFormat32bppPARGB) {
        UINT32 *d = (UINT32 *)row + px;
        for (INT i = 0; i < count; i++)
            d[i] = over(pargb[i], d[i]);
        return;
    }
    for (INT done = 0; done < count; done += 256) {
        INT n = count - done > 256 ? 256 : count - done;
        /* skip the work for the pixels that are clear */
        BOOL any = FALSE;
        for (INT i = 0; i < n; i++) {
            if (pargb[done + i] >> 24) {
                any = TRUE;
                break;
            }
        }
        if (!any)
            continue;
        gp_surface_read(s, x + done, y, n, dst);
        for (INT i = 0; i < n; i++)
            dst[i] = over(pargb[done + i], dst[i]);
        gp_surface_write(s, x + done, y, n, dst);
    }
    (void)px;
}

/* source copy: the color replaces the destination, by the coverage when the edge of a shape is partly covered. */
void gp_compose_copy(const GSurface *s, INT x, INT y, INT count, const UINT32 *pargb, const UINT8 *cov, UINT cmax)
{
    UINT32 dst[256];
    for (INT done = 0; done < count; done += 256) {
        INT n = count - done > 256 ? 256 : count - done;
        gp_surface_read(s, x + done, y, n, dst);
        for (INT i = 0; i < n; i++) {
            UINT c = cov ? cov[done + i] : cmax;
            if (!c)
                continue;
            if (c >= cmax) {
                dst[i] = pargb[done + i];
                continue;
            }
            UINT32 sp = pargb[done + i], dp = dst[i], out = 0;
            for (INT sh = 0; sh < 32; sh += 8) {
                UINT v = (c * ((sp >> sh) & 255) + (cmax - c) * ((dp >> sh) & 255) + cmax / 2) / cmax;
                out |= v << sh;
            }
            dst[i] = out;
        }
        gp_surface_write(s, x + done, y, n, dst);
    }
}
