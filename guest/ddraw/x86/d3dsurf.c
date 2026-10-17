/*
 * surfaces direct3d draws to or samples, and their copies in the
 * backend. a copy follows the surface's memory: drawing makes the copy
 * newer, and the cpu reading or writing the memory reads it back first;
 * writes to the memory reach the copy before the next use. formats the
 * backend has no match for go through 32-bit pixels, as do textures whose
 * color key becomes alpha.
 */
#include "d3d_private.h"

/* ------------------------------------------------------------------ pixels */

typedef struct CHANNEL {
    DWORD mask, shift, bits;
} CHANNEL;

static CHANNEL channel(DWORD mask)
{
    CHANNEL c = {mask};
    if (!mask)
        return c;
    while (!(mask >> c.shift & 1))
        c.shift++;
    while (c.shift + c.bits < 32 && (mask >> (c.shift + c.bits) & 1))
        c.bits++;
    return c;
}

static DWORD expand(DWORD v, const CHANNEL *c, DWORD missing)
{
    if (!c->bits)
        return missing;
    v = (v & c->mask) >> c->shift;
    if (c->bits >= 8)
        return v >> (c->bits - 8);
    DWORD out = 0;
    for (int at = 8; at > 0; at -= (int)c->bits)
        out |= at >= (int)c->bits ? v << (at - (int)c->bits) : v >> ((int)c->bits - at);
    return out & 0xFF;
}

static DWORD reduce(DWORD v, const CHANNEL *c)
{
    if (!c->bits)
        return 0;
    DWORD bits = c->bits > 8 ? 8 : c->bits;
    return (((v >> (8 - bits)) << (c->bits - bits)) << c->shift) & c->mask;
}

static DWORD load(const BYTE *p, DWORD bits, LONG x)
{
    switch (bits) {
    case 1:
        return p[x / 8] >> (7 - x % 8) & 1;
    case 2:
        return p[x / 4] >> (6 - x % 4 * 2) & 3;
    case 4:
        return p[x / 2] >> (x % 2 ? 0 : 4) & 0xF;
    case 8:
        return p[x];
    case 16:
        return ((const WORD *)p)[x];
    case 24:
        p += x * 3;
        return p[0] | p[1] << 8 | (DWORD)p[2] << 16;
    default:
        return ((const DWORD *)p)[x];
    }
}

static void store(BYTE *p, DWORD bits, LONG x, DWORD v)
{
    switch (bits) {
    case 8:
        p[x] = (BYTE)v;
        break;
    case 16:
        ((WORD *)p)[x] = (WORD)v;
        break;
    case 24:
        p += x * 3;
        p[0] = (BYTE)v;
        p[1] = (BYTE)(v >> 8);
        p[2] = (BYTE)(v >> 16);
        break;
    default:
        ((DWORD *)p)[x] = v;
        break;
    }
}

/* how a surface's pixels decode to 8-bit channels. */
typedef struct DECODE {
    DWORD bits;
    BOOL palette, luminance;
    CHANNEL r, g, b, a;
    DWORD colors[256];
    BOOL keyed;
    DWORD key;
} DECODE;

static BOOL decoder(DDSURF *s, DECODE *d)
{
    const DDPIXELFORMAT *pf = &s->format.pf;
    memset(d, 0, sizeof(*d));
    d->bits = s->format.bpp;
    DWORD flags = pf->dwFlags;
    if (flags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) {
        d->palette = TRUE;
        DDPAL *pal = s->palette;
        BOOL alpha = pal && (pal->caps & DDPCAPS_ALPHA);
        for (DWORD i = 0; i < 256; i++) {
            PALETTEENTRY e = pal && i < pal->count ? pal->entries[i] : (PALETTEENTRY){0, 0, 0, 0};
            d->colors[i] = (DWORD)(alpha ? e.peFlags : 0xFF) << 24 | (DWORD)e.peRed << 16 | (DWORD)e.peGreen << 8 |
                           e.peBlue;
        }
        return TRUE;
    }
    if (flags & DDPF_LUMINANCE) {
        d->luminance = TRUE;
        d->r = channel(pf->dwLuminanceBitMask);
        if (flags & DDPF_ALPHAPIXELS)
            d->a = channel(pf->dwLuminanceAlphaBitMask);
        return d->bits == 8 || d->bits == 16;
    }
    if (flags == DDPF_ALPHA) {
        d->a = channel(0xFF);
        return d->bits == 8;
    }
    if (!(flags & DDPF_RGB) || (d->bits != 8 && d->bits != 16 && d->bits != 24 && d->bits != 32))
        return FALSE;
    d->r = channel(pf->dwRBitMask);
    d->g = channel(pf->dwGBitMask);
    d->b = channel(pf->dwBBitMask);
    if (flags & DDPF_ALPHAPIXELS)
        d->a = channel(pf->dwRGBAlphaBitMask);
    return TRUE;
}

/* a texel matching the key, compared raw as windows' rasterizer does, keeps its color with alpha 0. */
static DWORD decode(const DECODE *d, DWORD v)
{
    DWORD argb;
    if (d->palette)
        argb = d->colors[v & 0xFF];
    else if (d->luminance) {
        DWORD l = expand(v, &d->r, 0xFF);
        argb = expand(v, &d->a, 0xFF) << 24 | l << 16 | l << 8 | l;
    } else
        argb = expand(v, &d->a, 0xFF) << 24 | expand(v, &d->r, 0) << 16 | expand(v, &d->g, 0) << 8 |
               expand(v, &d->b, 0);
    if (d->keyed && v == d->key)
        argb &= 0x00FFFFFF;
    return argb;
}

static DWORD encode(const DECODE *d, DWORD argb)
{
    return reduce(argb >> 16 & 0xFF, &d->r) | reduce(argb >> 8 & 0xFF, &d->g) | reduce(argb & 0xFF, &d->b) |
           reduce(argb >> 24, &d->a);
}

/* ------------------------------------------------------------------ the copy */

static D3DSURFX *extension(DDSURF *s)
{
    if (!s->d3d)
        s->d3d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(D3DSURFX));
    return s->d3d;
}

void D3dSurfFree(DDSURF *s)
{
    D3DSURFX *x = s->d3d;
    if (!x)
        return;
    DbImageDestroy(x->image);
    HeapFree(GetProcessHeap(), 0, x);
    s->d3d = nullptr;
}

/* the next smaller level of a mipmap. */
static DDSURF *next_level(DDSURF *s)
{
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
        if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth <= s->desc.dwWidth &&
            a->desc.dwHeight <= s->desc.dwHeight && a != s)
            return a;
    }
    return nullptr;
}

static UINT level_count(DDSURF *s)
{
    UINT n = 1;
    if (s->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP)
        for (DDSURF *l = next_level(s); l && n < 16; l = next_level(l))
            n++;
    return n;
}

/* changes of the pixels of every level, and of the palette the pixels index. */
static DWORD chain_generation(DDSURF *s, UINT levels)
{
    DWORD g = 0;
    DDSURF *l = s;
    for (UINT i = 0; i < levels && l; i++, l = next_level(l))
        g += l->generation * (i + 1);
    if (s->palette)
        g += s->palette->uniqueness * 977;
    return g;
}

/* one level's rows into the image, through 32-bit pixels when the backend lacks the format or keys it. */
static BOOL upload_level(D3DSURFX *x, DDSURF *s, UINT level)
{
    if (!s->memory)
        return FALSE;
    if (!x->converted)
        return DbImageWrite(x->image, 0, level, nullptr, s->memory, (UINT)s->desc.lPitch);
    DECODE d;
    if (!decoder(s, &d))
        return FALSE;
    d.keyed = x->keyed;
    d.key = x->key;
    UINT width = s->desc.dwWidth, height = s->desc.dwHeight;
    DWORD *rows = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * height * 4);
    if (!rows)
        return FALSE;
    for (UINT y = 0; y < height; y++) {
        const BYTE *src = s->memory + (SIZE_T)y * s->desc.lPitch;
        for (UINT xx = 0; xx < width; xx++)
            rows[(SIZE_T)y * width + xx] = decode(&d, load(src, d.bits, (LONG)xx));
    }
    BOOL ok = DbImageWrite(x->image, 0, level, nullptr, rows, width * 4);
    HeapFree(GetProcessHeap(), 0, rows);
    return ok;
}

static BOOL upload(D3DSURFX *x, DDSURF *s)
{
    UINT levels = (x->usage & DB_TARGET) ? 1 : level_count(s);
    DDSURF *l = s;
    for (UINT i = 0; i < levels && l; i++, l = next_level(l))
        if (!upload_level(x, l, i))
            return FALSE;
    x->generation = chain_generation(s, levels);
    return TRUE;
}

/* direct3d's drawing back in the memory. */
static void read_back(D3DSURFX *x, DDSURF *s)
{
    x->gpu_newer = FALSE;
    if (!s->memory || !x->image)
        return;
    if (!x->converted) {
        DbImageRead(x->image, nullptr, s->memory, (UINT)s->desc.lPitch);
    } else {
        DECODE d;
        UINT width = s->desc.dwWidth, height = s->desc.dwHeight;
        DWORD *rows = decoder(s, &d) && !d.palette ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * height * 4)
                                                   : nullptr;
        if (rows && DbImageRead(x->image, nullptr, rows, width * 4))
            for (UINT y = 0; y < height; y++) {
                BYTE *dst = s->memory + (SIZE_T)y * s->desc.lPitch;
                for (UINT xx = 0; xx < width; xx++)
                    store(dst, d.bits, (LONG)xx, encode(&d, rows[(SIZE_T)y * width + xx]));
            }
        HeapFree(GetProcessHeap(), 0, rows);
    }
    x->generation = s->generation;
}

void DdSurfSync(DDSURF *s)
{
    D3DSURFX *x = s->d3d;
    if (x && x->gpu_newer)
        read_back(x, s);
}

void D3dSurfDrawn(DDSURF *s)
{
    if (s->d3d)
        s->d3d->gpu_newer = TRUE;
}

void D3dSurfReplaced(DDSURF *s)
{
    D3DSURFX *x = s->d3d;
    if (x) {
        x->gpu_newer = TRUE;
        x->generation = s->generation;
    }
}

/* the format and usage the copy needs now; a changed need makes a new copy. */
DbImage *D3dSurfImage(D3DDEV *dev, DDSURF *s, DWORD usage)
{
    DbDevice *db = dev->db;
    D3DSURFX *x = extension(s);
    if (!x)
        return nullptr;
    /* a surface both drawn to and sampled has one copy for both. */
    DWORD caps = s->desc.ddsCaps.dwCaps;
    if (!(usage & DB_DEPTH) && (caps & DDSCAPS_TEXTURE) && (caps & DDSCAPS_3DDEVICE))
        usage |= DB_TARGET | DB_TEXTURE;
    BOOL keyed = (usage & DB_TEXTURE) && !(usage & DB_TARGET) && dev->rs[D3DRENDERSTATE_COLORKEYENABLE] &&
                 (s->ckeys & DDSD_CKSRCBLT);
    DWORD key = keyed ? s->desc.ddckCKSrcBlt.dwColorSpaceLowValue : 0;
    if (x->image && ((x->usage & usage) != usage || x->keyed != keyed || x->key != key)) {
        if (x->gpu_newer)
            read_back(x, s);
        DbImageDestroy(x->image);
        x->image = nullptr;
    }
    if (!x->image) {
        DWORD format = keyed ? 0 : DbFormatOf(&s->format.pf, usage);
        x->converted = !format;
        if (!format) {
            if (usage & DB_DEPTH)
                return nullptr;
            DECODE d;
            if (!decoder(s, &d))
                return nullptr;
            format = (usage & DB_TARGET) && !(s->format.pf.dwFlags & DDPF_ALPHAPIXELS) ? DB_FORMAT_XRGB
                                                                                       : DB_FORMAT_ARGB;
        }
        UINT levels = (usage & (DB_TARGET | DB_DEPTH)) ? 1 : level_count(s);
        x->image = DbImageCreate(db, usage, s->desc.dwWidth, s->desc.dwHeight, levels, format);
        if (!x->image)
            return nullptr;
        x->usage = usage;
        x->keyed = keyed;
        x->key = key;
        x->gpu_newer = FALSE;
        if (usage & DB_DEPTH) {
            /* a new depth buffer starts as the far plane, its memory never reaching it. */
            x->fill = TRUE;
            x->fill_z = 1.0f;
            x->fill_stencil = 0;
            x->fill_rect = (RECT){0, 0, (LONG)s->desc.dwWidth, (LONG)s->desc.dwHeight};
            return x->image;
        }
        upload(x, s);
        return x->image;
    }
    if (usage & DB_DEPTH)
        return x->image;
    UINT levels = (x->usage & DB_TARGET) ? 1 : level_count(s);
    if (!x->gpu_newer && x->generation != chain_generation(s, levels))
        upload(x, s);
    return x->image;
}

void D3dSurfDepthFill(DDSURF *s, const RECT *rect, DWORD value)
{
    D3DSURFX *x = s->d3d;
    if (!x || !x->image || !(x->usage & DB_DEPTH))
        return;
    CHANNEL z = channel(s->format.pf.dwZBitMask);
    if (!z.bits)
        z = channel(s->format.pf.dwZBufferBitDepth >= 32 ? 0xFFFFFFFF
                                                          : (1u << s->format.pf.dwZBufferBitDepth) - 1);
    DWORD max = z.bits >= 32 ? 0xFFFFFFFF : (1u << z.bits) - 1;
    x->fill_z = (float)((double)((value & z.mask) >> z.shift) / (double)max);
    /* a fill of part of the buffer after another waiting one takes both. */
    if (x->fill)
        UnionRect(&x->fill_rect, &x->fill_rect, rect);
    else
        x->fill_rect = *rect;
    x->fill = TRUE;
}
