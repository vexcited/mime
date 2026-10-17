/*
 * what a display driver does that windows' emulation does not: blts
 * between formats, as d3d9's StretchRect converts rgb, palettized and
 * packed yuv sources to rgb, and the blocks of four character code
 * surfaces, copied whole.
 */
#include "ddraw_private.h"

typedef struct CHANNEL {
    DWORD mask, shift, bits;
} CHANNEL;

typedef struct LAYOUT {
    enum { RGB, PALETTE, YUY2, UYVY } kind;
    DWORD size;
    CHANNEL r, g, b, a;
    const PALETTEENTRY *palette;
} LAYOUT;

static CHANNEL channel(DWORD mask)
{
    CHANNEL c = {mask};
    if (!mask)
        return c;
    while (!(mask & 1)) {
        mask >>= 1;
        c.shift++;
    }
    while (mask & 1) {
        mask >>= 1;
        c.bits++;
    }
    return c;
}

static BOOL layout(const DDSURF *s, LAYOUT *l)
{
    const DDPIXELFORMAT *pf = &s->format.pf;
    memset(l, 0, sizeof(*l));
    l->size = s->format.bpp / 8;
    if (pf->dwFlags & DDPF_FOURCC) {
        if (pf->dwFourCC == MAKEFOURCC('Y', 'U', 'Y', '2'))
            l->kind = YUY2;
        else if (pf->dwFourCC == MAKEFOURCC('U', 'Y', 'V', 'Y'))
            l->kind = UYVY;
        else
            return FALSE;
        return TRUE;
    }
    if (!(pf->dwFlags & DDPF_RGB) || (pf->dwFlags & (DDPF_ZBUFFER | DDPF_LUMINANCE | DDPF_BUMPDUDV)))
        return FALSE;
    if (pf->dwFlags & DDPF_PALETTEINDEXED8) {
        l->kind = PALETTE;
        l->palette = s->palette ? s->palette->entries : nullptr;
        return TRUE;
    }
    if (pf->dwFlags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4))
        return FALSE;
    if (l->size < 2 || !pf->dwRBitMask || !pf->dwGBitMask || !pf->dwBBitMask)
        return FALSE;
    l->kind = RGB;
    l->r = channel(pf->dwRBitMask);
    l->g = channel(pf->dwGBitMask);
    l->b = channel(pf->dwBBitMask);
    if (pf->dwFlags & DDPF_ALPHAPIXELS)
        l->a = channel(pf->dwRGBAlphaBitMask);
    return TRUE;
}

/* sources of any of the layouts, destinations rgb. */
BOOL DdCanConvert(const DDSURF *src, const DDSURF *dst)
{
    LAYOUT s, d;
    return layout(src, &s) && layout(dst, &d) && d.kind == RGB;
}

static DWORD expand(DWORD v, const CHANNEL *c)
{
    if (!c->bits)
        return 0xFF;
    v = (v & c->mask) >> c->shift;
    if (c->bits >= 8)
        return v >> (c->bits - 8);
    DWORD out = 0;
    for (int at = 8; at > 0; at -= (int)c->bits)
        out |= at >= (int)c->bits ? v << (at - c->bits) : v >> (c->bits - at);
    return out & 0xFF;
}

static DWORD reduce(DWORD v, const CHANNEL *c)
{
    if (!c->bits)
        return 0;
    DWORD bits = c->bits > 8 ? 8 : c->bits;
    DWORD x = (v >> (8 - bits)) << (c->bits - bits);
    return (x << c->shift) & c->mask;
}

static DWORD load(const BYTE *p, DWORD size)
{
    switch (size) {
    case 1:
        return p[0];
    case 2:
        return p[0] | p[1] << 8;
    case 3:
        return p[0] | p[1] << 8 | (DWORD)p[2] << 16;
    default:
        return p[0] | p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
    }
}

static void store(BYTE *p, DWORD size, DWORD v)
{
    for (DWORD i = 0; i < size; i++)
        p[i] = (BYTE)(v >> (8 * i));
}

static BYTE clamp(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (BYTE)v;
}

/* bt.601 video range, as drivers convert it. */
static DWORD yuv(int y, int u, int v)
{
    int c = (y - 16) * 298, d = u - 128, e = v - 128;
    return 0xFF000000u | (DWORD)clamp((c + 409 * e + 128) >> 8) << 16 |
           (DWORD)clamp((c - 100 * d - 208 * e + 128) >> 8) << 8 | clamp((c + 516 * d + 128) >> 8);
}

/* a source pixel as a8r8g8b8. */
static DWORD fetch(const LAYOUT *l, const BYTE *row, LONG x)
{
    switch (l->kind) {
    case PALETTE: {
        if (!l->palette)
            return 0xFF000000;
        const PALETTEENTRY *e = &l->palette[row[x]];
        return 0xFF000000u | (DWORD)e->peRed << 16 | (DWORD)e->peGreen << 8 | e->peBlue;
    }
    case YUY2: {
        const BYTE *m = row + (x & ~1) * 2;
        return yuv(m[(x & 1) * 2], m[1], m[3]);
    }
    case UYVY: {
        const BYTE *m = row + (x & ~1) * 2;
        return yuv(m[1 + (x & 1) * 2], m[0], m[2]);
    }
    default: {
        DWORD v = load(row + x * l->size, l->size);
        return expand(v, &l->a) << 24 | expand(v, &l->r) << 16 | expand(v, &l->g) << 8 | expand(v, &l->b);
    }
    }
}

static DWORD pack(const LAYOUT *l, DWORD argb)
{
    return reduce(argb >> 24, &l->a) | reduce(argb >> 16 & 0xFF, &l->r) | reduce(argb >> 8 & 0xFF, &l->g) |
           reduce(argb & 0xFF, &l->b);
}

/* nearest point stretching, as the emulation's blts do. */
void DdBlitConvert(DDSURF *dst, const RECT *d, DDSURF *src, const RECT *s, BOOL mirror_x, BOOL mirror_y)
{
    LAYOUT sl, dl;
    if (!layout(src, &sl) || !layout(dst, &dl) || dl.kind != RGB)
        return;
    LONG w = d->right - d->left, h = d->bottom - d->top;
    LONG sw = s->right - s->left, sh = s->bottom - s->top;
    for (LONG y = 0; y < h; y++) {
        LONG sy = (LONG)((LONGLONG)y * sh / h);
        if (mirror_y)
            sy = sh - 1 - sy;
        const BYTE *srow = src->memory + (SSIZE_T)(s->top + sy) * src->desc.lPitch;
        BYTE *drow = dst->memory + (SSIZE_T)(d->top + y) * dst->desc.lPitch + (SSIZE_T)d->left * dl.size;
        for (LONG x = 0; x < w; x++) {
            LONG sx = (LONG)((LONGLONG)x * sw / w);
            if (mirror_x)
                sx = sw - 1 - sx;
            store(drow + x * dl.size, dl.size, pack(&dl, fetch(&sl, srow, s->left + sx)));
        }
    }
}

/*
 * four character code surfaces of the same format: whole blocks, which
 * a driver copies without stretching; packed yuv by pixel pairs.
 */
HRESULT DdBltBlocks(DDSURF *dst, const RECT *d, DDSURF *src, const RECT *s)
{
    LONG w = d->right - d->left, h = d->bottom - d->top;
    if (w != s->right - s->left || h != s->bottom - s->top) {
        if (dst->format.block)
            return DDERR_UNSUPPORTED;
        for (LONG y = 0; y < h; y++) {
            LONG sy = s->top + (LONG)((LONGLONG)y * (s->bottom - s->top) / h);
            const BYTE *srow = src->memory + (SSIZE_T)sy * src->desc.lPitch;
            BYTE *drow = dst->memory + (SSIZE_T)(d->top + y) * dst->desc.lPitch;
            for (LONG x = d->left & ~1; x < d->right; x += 2) {
                LONG sx = s->left + (LONG)((LONGLONG)(x - d->left) * (s->right - s->left) / w);
                memcpy(drow + x * 2, srow + (sx & ~1) * 2, 4);
            }
        }
        DdSurfWritten(dst, d);
        return DD_OK;
    }
    if (dst->format.block) {
        LONG dw = (LONG)dst->desc.dwWidth, dh = (LONG)dst->desc.dwHeight;
        LONG sw = (LONG)src->desc.dwWidth, sh = (LONG)src->desc.dwHeight;
        BOOL aligned = !(d->left & 3) && !(d->top & 3) && !(s->left & 3) && !(s->top & 3) &&
                       (!(w & 3) || (d->right == dw && s->right == sw)) &&
                       (!(h & 3) || (d->bottom == dh && s->bottom == sh));
        if (!aligned)
            return DDERR_INVALIDRECT;
        DWORD block = dst->format.block;
        LONG bw = (w + 3) / 4, bh = (h + 3) / 4;
        LONG dpitch = block * ((dw + 3) / 4), spitch = block * ((sw + 3) / 4);
        for (LONG y = 0; y < bh; y++)
            memmove(dst->memory + (SSIZE_T)(d->top / 4 + y) * dpitch + (d->left / 4) * block,
                    src->memory + (SSIZE_T)(s->top / 4 + y) * spitch + (s->left / 4) * block, (SIZE_T)bw * block);
    } else {
        LONG left = d->left & ~1, sleft = s->left & ~1;
        SIZE_T bytes = (SIZE_T)(((d->right + 1) & ~1) - left) * 2;
        for (LONG y = 0; y < h; y++)
            memmove(dst->memory + (SSIZE_T)(d->top + y) * dst->desc.lPitch + left * 2,
                    src->memory + (SSIZE_T)(s->top + y) * src->desc.lPitch + sleft * 2, bytes);
    }
    DdSurfWritten(dst, d);
    return DD_OK;
}
