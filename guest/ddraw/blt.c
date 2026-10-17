/*
 * Blt, BltFast and BltBatch, as windows checks and runs them. windows
 * picks for each blt between the display driver and its emulation layer
 * by the caps each has for the memory the surfaces are in; mime's
 * surfaces are all in memory, but the choice decides which features a blt
 * may use and which errors it returns, so it is made the same way: the
 * "hardware" caps are those of a display driver of the windows display
 * driver model, the emulation's are windows' own. the pixels are then
 * moved by blit.c. see docs/ddraw.md.
 */
#include "ddraw_private.h"
#include "blit.h"

/* ------------------------------------------------------------------ caps */

typedef struct BLTCAPS {
    DWORD caps, ckey, fx;
    DWORD rops[DD_ROP_SPACE];
} BLTCAPS;

#define ROP_BIT(index) [(index) / 32] = 1u << ((index) % 32)

/* windows' emulation: srcblt keys only, every stretch and mirror, three rops; the same between all memories. */
static const BLTCAPS HEL = {0xF4C08241, DDCKEYCAPS_SRCBLT, 0x3FCE3, {[0] = 1, [6] = 1u << 12, [7] = 1u << 31}};

/*
 * a display driver of the windows display driver model: keys, stretches,
 * mirrors and srccopy between surfaces in video memory, plain copies
 * between system and video memory, nothing between system memory ones.
 */
static const BLTCAPS HW = {0x95D207C1, DDCKEYCAPS_SRCBLT | DDCKEYCAPS_DESTBLT, 0x154E0, {[6] = 1u << 12}};
static const BLTCAPS HW_SYSMEM = {DDCAPS_BLT, 0, 0, {[6] = 1u << 12}};
static const BLTCAPS HW_NONE = {};

static_assert((0xCC / 32) == 6 && (0xCC % 32) == 12 && (0xFF / 32) == 7);

/* the choice between the two, made cap by cap as windows' checks go. */
typedef struct CHOICE {
    const BLTCAPS *hw, *hel;
    BOOL force_hw, force_hel;
} CHOICE;

enum { CAPS, CKEY, FX };

static DWORD field(const BLTCAPS *c, int which)
{
    return which == CAPS ? c->caps : which == CKEY ? c->ckey : c->fx;
}

/* windows' cap test: what both have is free, else the first that has it is forced. */
static BOOL cap_test(CHOICE *ch, int which, DWORD bit, BOOL use_both)
{
    BOOL hw = (field(ch->hw, which) & bit) == bit, hel = (field(ch->hel, which) & bit) == bit;
    if (use_both && hw && hel)
        return TRUE;
    if (ch->force_hw)
        return hw;
    if (ch->force_hel)
        return hel;
    if (hw)
        ch->force_hw = TRUE;
    else if (hel)
        ch->force_hel = TRUE;
    else
        return FALSE;
    return TRUE;
}

static BOOL rop_test(CHOICE *ch, BYTE index)
{
    BOOL hw = (ch->hw->rops[index / 32] >> (index % 32)) & 1, hel = (ch->hel->rops[index / 32] >> (index % 32)) & 1;
    if (hw && hel)
        return TRUE;
    if (ch->force_hw)
        return hw;
    if (ch->force_hel)
        return hel;
    if (hw)
        ch->force_hw = TRUE;
    else if (hel)
        ch->force_hel = TRUE;
    else
        return FALSE;
    return TRUE;
}

static BOOL is_dxt(const DDSURF *s)
{
    return s->format.block != 0;
}

static BOOL is_fourcc(const DDSURF *s)
{
    return (s->format.pf.dwFlags & DDPF_FOURCC) != 0;
}

/* windows' initBltCaps and the forcing that follows it. */
static void choose(CHOICE *ch, const DDSURF *dst, const DDSURF *src)
{
    memset(ch, 0, sizeof(*ch));
    ch->hel = &HEL;
    ch->hw = &HW;
    DWORD dcaps = dst->desc.ddsCaps.dwCaps, scaps = src ? src->desc.ddsCaps.dwCaps : 0;
    if (!src) {
        if (dcaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_NONLOCALVIDMEM))
            ch->force_hel = TRUE;
    } else if ((dcaps & scaps) & DDSCAPS_SYSTEMMEMORY) {
        ch->hw = &HW_NONE;
    } else if ((dcaps | scaps) & DDSCAPS_SYSTEMMEMORY) {
        ch->hw = &HW_SYSMEM;
    }
    if ((dcaps | scaps) & DDSCAPS_NONLOCALVIDMEM)
        ch->force_hel = TRUE;
    if (src && (is_fourcc(src) || is_fourcc(dst))) {
        BOOL differ = src->format.pf.dwFourCC != dst->format.pf.dwFourCC;
        if ((differ && (is_dxt(src) || is_dxt(dst))) || !differ)
            ch->force_hel = TRUE;
    }
    if (dcaps & DDSCAPS_OWNDC)
        ch->force_hel = TRUE;
}

/* ------------------------------------------------------------------ pixels */

static BOOL same_format(const DDSURF *a, const DDSURF *b)
{
    const DDPIXELFORMAT *x = &a->format.pf, *y = &b->format.pf;
    if (x->dwFlags != y->dwFlags || x->dwRGBBitCount != y->dwRGBBitCount)
        return FALSE;
    if (!(x->dwFlags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) &&
        (x->dwFlags & DDPF_RGB) &&
        (x->dwRBitMask != y->dwRBitMask || x->dwGBitMask != y->dwGBitMask || x->dwBBitMask != y->dwBBitMask))
        return FALSE;
    if ((x->dwFlags & (DDPF_ALPHAPIXELS | DDPF_ZPIXELS)) && x->dwRGBAlphaBitMask != y->dwRGBAlphaBitMask)
        return FALSE;
    return TRUE;
}

/* the destination formats windows' emulation draws on. */
static BOOL hel_format(const DDSURF *s)
{
    DWORD flags = s->format.pf.dwFlags;
    if (flags & DDPF_ZBUFFER)
        return !(flags & ~(DDPF_ZBUFFER | DDPF_STENCILBUFFER));
    if (!(flags & (DDPF_RGB | DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)))
        return FALSE;
    if (flags & ~(DDPF_RGB | DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8 | DDPF_ALPHAPIXELS |
                  DDPF_ZPIXELS | DDPF_ALPHAPREMULT))
        return FALSE;
    DWORD bpp = s->format.bpp;
    return bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32;
}

/* how keys compare: 8 and 16 bits whole, 24 bits without the top byte, 32 bits the color masks alone. */
static DWORD key_mask(const DDSURF *s)
{
    switch (s->format.bpp) {
    case 8:
        return 0xFF;
    case 16:
        return 0xFFFF;
    case 24:
        return 0xFFFFFF;
    default: {
        const DDPIXELFORMAT *pf = &s->format.pf;
        DWORD m = pf->dwRBitMask | pf->dwGBitMask | pf->dwBBitMask;
        return (pf->dwFlags & DDPF_RGB) && m ? m : 0x00FFFFFF;
    }
    }
}

static BYTE *at(DDSURF *s, LONG x, LONG y)
{
    return s->memory + (SSIZE_T)y * s->desc.lPitch + DdSurfBytes(s, x);
}

/* the work of one blt, rectangles already clipped. */
typedef struct BLTJOB {
    DDSURF *dst, *src;
    DWORD op;          /* DDBLT_ROP, DDBLT_COLORFILL or DDBLT_DEPTHFILL */
    DWORD fill;
    BOOL mirror_x, mirror_y;
    BOOL src_key, dst_key;
    DWORD src_low, dst_low;
    BOOL convert;      /* formats differ: the driver path converts */
} BLTJOB;

static void run_rect(const BLTJOB *j, const RECT *d, const RECT *s)
{
    DDSURF *dst = j->dst;
    LONG w = d->right - d->left, h = d->bottom - d->top;
    DWORD size = dst->format.bpp / 8;
    if (j->op != DDBLT_ROP || !j->src) {
        DdFill(at(dst, d->left, d->top), dst->desc.lPitch, size, w, h, j->fill);
        return;
    }
    DDSURF *src = j->src;
    if (j->convert) {
        DdBlitConvert(dst, d, src, s, j->mirror_x, j->mirror_y);
        return;
    }
    DDBLIT b = {
        .dst = at(dst, d->left, d->top),
        .dst_pitch = dst->desc.lPitch,
        .width = w,
        .height = h,
        .src = at(src, s->left, s->top),
        .src_pitch = src->desc.lPitch,
        .src_width = s->right - s->left,
        .src_height = s->bottom - s->top,
        .size = size,
        .mirror_x = j->mirror_x,
        .mirror_y = j->mirror_y,
        .src_key = j->src_key,
        .dst_key = j->dst_key,
        .key_mask = key_mask(dst),
    };
    b.src_low = b.src_high = j->src_low & b.key_mask;
    b.dst_low = b.dst_high = j->dst_low & b.key_mask;
    DdBlit(&b);
}

/* windows' XformRect: a clipped part of the destination and the part of the source it takes. */
static void xform(const RECT *src, const RECT *dst, const RECT *clip, RECT *out, BOOL mirror_x, BOOL mirror_y)
{
    LONG sw = src->right - src->left, sh = src->bottom - src->top;
    LONG dw = dst->right - dst->left, dh = dst->bottom - dst->top;
    if (sw == dw && sh == dh) {
        *out = (RECT){clip->left - dst->left + src->left, clip->top - dst->top + src->top,
                      clip->right - dst->left + src->left, clip->bottom - dst->top + src->top};
    } else {
        LONGLONG sx = ((LONGLONG)sw << 16) / dw, sy = ((LONGLONG)sh << 16) / dh;
        out->left = src->left + (LONG)(((clip->left - dst->left) * sx + 0x8000) >> 16);
        out->right = src->left + (LONG)(((clip->right - dst->left) * sx + 0x8000) >> 16);
        out->top = src->top + (LONG)(((clip->top - dst->top) * sy + 0x8000) >> 16);
        out->bottom = src->top + (LONG)(((clip->bottom - dst->top) * sy + 0x8000) >> 16);
        if (out->right == out->left) {
            if (out->right < src->right)
                out->right++;
            else
                out->left--;
        }
        if (out->bottom == out->top) {
            if (out->bottom < src->bottom)
                out->bottom++;
            else
                out->top--;
        }
    }
    if (mirror_x) {
        LONG l = src->left + src->right - out->right, r = src->left + src->right - out->left;
        out->left = l;
        out->right = r;
    }
    if (mirror_y) {
        LONG t = src->top + src->bottom - out->bottom, b = src->top + src->bottom - out->top;
        out->top = t;
        out->bottom = b;
    }
}

/* ------------------------------------------------------------------ Blt */

static BOOL rect_inside(const RECT *r, const DDSURF *s)
{
    return r->left >= 0 && r->top >= 0 && r->right <= (LONG)s->desc.dwWidth && r->bottom <= (LONG)s->desc.dwHeight;
}

/* windows' step 15: keys become overrides, from the surfaces that hold them. */
static HRESULT keys(DWORD *flags, const DDBLTFX *fx, DDSURF *dst, DDSURF *src, DWORD src_version, CHOICE *ch,
                    BLTJOB *job)
{
    if (*flags & (DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRCOVERRIDE)) {
        if (!cap_test(ch, CAPS, DDCAPS_COLORKEY, TRUE))
            return DDERR_NOCOLORKEYHW;
        if (*flags & DDBLT_KEYSRCOVERRIDE) {
            if (!cap_test(ch, CKEY, DDCKEYCAPS_SRCBLT, TRUE))
                return DDERR_NOCOLORKEYHW;
            job->src_key = TRUE;
            job->src_low = fx->ddckSrcColorkey.dwColorSpaceLowValue;
        }
        if (*flags & DDBLT_KEYDESTOVERRIDE) {
            if (!cap_test(ch, CKEY, DDCKEYCAPS_DESTBLT, TRUE))
                return DDERR_NOCOLORKEYHW;
            job->dst_key = TRUE;
            job->dst_low = fx->ddckDestColorkey.dwColorSpaceLowValue;
        }
    }
    if (*flags & DDBLT_KEYSRC) {
        if (!src)
            return DDERR_INVALIDOBJECT;
        if (*flags & DDBLT_KEYSRCOVERRIDE)
            return DDERR_INVALIDPARAMS;
        if (!(src->ckeys & DDSD_CKSRCBLT))
            return DDERR_INVALIDPARAMS;
        if (!cap_test(ch, CKEY, DDCKEYCAPS_SRCBLT, TRUE))
            return DDERR_NOCOLORKEYHW;
        job->src_key = TRUE;
        job->src_low = src->desc.ddckCKSrcBlt.dwColorSpaceLowValue;
        *flags = (*flags & ~DDBLT_KEYSRC) | DDBLT_KEYSRCOVERRIDE;
    }
    if (*flags & DDBLT_KEYDEST) {
        if (*flags & DDBLT_KEYDESTOVERRIDE)
            return DDERR_INVALIDPARAMS;
        if (!(dst->ckeys & DDSD_CKDESTBLT))
            return DDERR_INVALIDPARAMS;
        if (!cap_test(ch, CKEY, DDCKEYCAPS_DESTBLT, TRUE))
            return DDERR_NOCOLORKEYHW;
        /* a key asked through the first two surface interfaces comes from the source, as in windows. */
        DDSURF *holder = src_version <= DDV2 ? src : dst;
        if (!holder)
            return DDERR_INVALIDPARAMS;
        job->dst_key = TRUE;
        job->dst_low = holder->desc.ddckCKDestBlt.dwColorSpaceLowValue;
        *flags = (*flags & ~DDBLT_KEYDEST) | DDBLT_KEYDESTOVERRIDE;
    }
    return DD_OK;
}

/* which rops read the source and which the pattern, from the truth table index. */
static BOOL rop_uses_source(BYTE index)
{
    return ((index >> 2) & 0x33) != (index & 0x33);
}

static BOOL rop_uses_pattern(BYTE index)
{
    return ((index >> 4) & 0x0F) != (index & 0x0F);
}

static HRESULT stretching(CHOICE *ch, LONG sw, LONG sh, LONG dw, LONG dh)
{
    if (sw == dw && sh == dh)
        return DD_OK;
    if (!cap_test(ch, CAPS, DDCAPS_BLTSTRETCH, TRUE))
        return DDERR_NOSTRETCHHW;
    DWORD need = 0;
    if (sh < dh)
        need |= dh % sh ? DDFXCAPS_BLTSTRETCHY : DDFXCAPS_BLTSTRETCHYN;
    else if (sh > dh)
        need |= sh % dh ? DDFXCAPS_BLTSHRINKY : DDFXCAPS_BLTSHRINKYN;
    if (sw < dw)
        need |= dw % sw ? DDFXCAPS_BLTSTRETCHX : DDFXCAPS_BLTSTRETCHXN;
    else if (sw > dw)
        need |= sw % dw ? DDFXCAPS_BLTSHRINKX : DDFXCAPS_BLTSHRINKXN;
    for (DWORD bit = 1; bit; bit <<= 1)
        if ((need & bit) && !cap_test(ch, FX, bit, FALSE))
            return DDERR_NOSTRETCHHW;
    return DD_OK;
}

static DWORD truncate_fill(const DDSURF *s, DWORD value)
{
    DWORD bits = s->format.pf.dwRGBBitCount;
    return bits <= 8 ? value & 0xFF : bits == 16 ? value & 0xFFFF : bits == 24 ? value & 0xFFFFFF : value;
}

HRESULT DdBlt(DDSURF *dst, const RECT *dst_in, DDSURF *src, DWORD src_version, const RECT *src_in, DWORD flags,
              DDBLTFX *fx, DWORD dst_version)
{
    if (dst->lost || (src && src->lost))
        return DDERR_SURFACELOST;
    if (flags & (0x80000000 | DDBLT_LAST_PRESENTATION))
        return DDERR_INVALIDPARAMS;
    if (src && src->dd && dst->dd && src->dd != dst->dd)
        return DDERR_DEVICEDOESNTOWNSURFACE;
    if ((dst->desc.ddsCaps.dwCaps & DDSCAPS_OPTIMIZED) || (src && (src->desc.ddsCaps.dwCaps & DDSCAPS_OPTIMIZED)))
        return DDERR_INVALIDPARAMS;
    if (dst_version == DDV7) {
        if (!(flags & DDBLT_DONOTWAIT))
            flags |= DDBLT_WAIT;
        else if (flags & DDBLT_WAIT)
            return DDERR_INVALIDPARAMS;
    }
    if (DdSurfBusy(dst) || (src && DdSurfBusy(src)))
        return DDERR_SURFACEBUSY;
    CHOICE ch;
    choose(&ch, dst, src);
    if (!cap_test(&ch, CAPS, DDCAPS_BLT, TRUE))
        return DDERR_NOBLTHW;
    if (fx && fx->dwSize != sizeof(DDBLTFX))
        return DDERR_INVALIDPARAMS;
    if (!fx && (flags & 0x02F75D4A))
        return DDERR_INVALIDPARAMS;
    BLTJOB job = {.dst = dst, .src = src, .op = DDBLT_ROP};
    if (!(flags & ~DDBLT_WAIT)) {
        if (!src)
            return DDERR_INVALIDPARAMS;
    } else {
        if (flags & (DDBLT_KEYDEST | DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRC | DDBLT_KEYSRCOVERRIDE)) {
            HRESULT hr = keys(&flags, fx, dst, src, src_version, &ch, &job);
            if (FAILED(hr))
                return hr;
        }
        if (!(flags & 0x02061C00)) {
            if (!src)
                return DDERR_INVALIDPARAMS;
        } else if (flags & DDBLT_ROP) {
            if (flags & 0x02041C00)
                return DDERR_INVALIDPARAMS;
            BYTE index = (BYTE)(fx->dwROP >> 16);
            if (fx->dwROP != SRCCOPY && !rop_test(&ch, index))
                return DDERR_NORASTEROPHW;
            if (rop_uses_source(index) && !src)
                return DDERR_INVALIDPARAMS;
            if (rop_uses_pattern(index)) {
                DDSURF *pattern = DdSurfFromIface(fx->lpDDSPattern);
                if (pattern && pattern->lost)
                    return DDERR_SURFACELOST;
                if (!pattern || pattern->desc.dwWidth != 8 || pattern->desc.dwHeight != 8)
                    return DDERR_INVALIDPARAMS;
            }
            if (fx->dwROP != SRCCOPY) {
                /* emulation fills: blackness, whiteness, and all ones for anything else. */
                job.op = DDBLT_COLORFILL;
                job.fill = fx->dwROP == BLACKNESS ? 0 : fx->dwROP == WHITENESS ? 0x00FFFFFF : 0xFFFFFFFF;
                job.src = nullptr;
            }
        } else if (flags & DDBLT_COLORFILL) {
            if (src || (dst->desc.ddsCaps.dwCaps & DDSCAPS_ZBUFFER))
                return DDERR_INVALIDPARAMS;
            if (!cap_test(&ch, CAPS, DDCAPS_BLTCOLORFILL, TRUE))
                return DDERR_UNSUPPORTED;
            fx->dwFillColor = truncate_fill(dst, fx->dwFillColor);
            job.op = DDBLT_COLORFILL;
            job.fill = fx->dwFillColor;
        } else if (flags & DDBLT_DEPTHFILL) {
            if (src || !(dst->desc.ddsCaps.dwCaps & DDSCAPS_ZBUFFER))
                return DDERR_INVALIDPARAMS;
            if (!cap_test(&ch, CAPS, DDCAPS_BLTDEPTHFILL, TRUE))
                return DDERR_UNSUPPORTED;
            job.op = DDBLT_DEPTHFILL;
            job.fill = fx->dwFillDepth;
        } else if (flags & DDBLT_DDROPS) {
            if (flags & (DDBLT_ROTATIONANGLE | DDBLT_DDFX))
                return DDERR_INVALIDPARAMS;
            return DDERR_NODDROPSHW;
        } else if (flags & DDBLT_DDFX) {
            if (flags & DDBLT_ROTATIONANGLE)
                return DDERR_INVALIDPARAMS;
            DWORD ddfx = fx->dwDDFX;
            if (ddfx & DDBLTFX_ARITHSTRETCHY)
                return DDERR_NOSTRETCHHW;
            if ((ddfx & DDBLTFX_MIRRORLEFTRIGHT) && !cap_test(&ch, FX, DDFXCAPS_BLTMIRRORLEFTRIGHT, FALSE))
                return DDERR_NOMIRRORHW;
            if ((ddfx & DDBLTFX_MIRRORUPDOWN) && !cap_test(&ch, FX, DDFXCAPS_BLTMIRRORUPDOWN, FALSE))
                return DDERR_NOMIRRORHW;
            if ((ddfx & (DDBLTFX_ROTATE90 | DDBLTFX_ROTATE180 | DDBLTFX_ROTATE270)) &&
                !cap_test(&ch, FX, DDFXCAPS_BLTROTATION90, FALSE))
                return DDERR_NOROTATIONHW;
            if (!src)
                return DDERR_INVALIDPARAMS;
            job.mirror_x = (ddfx & DDBLTFX_MIRRORLEFTRIGHT) != 0;
            job.mirror_y = (ddfx & DDBLTFX_MIRRORUPDOWN) != 0;
        } else {
            return DDERR_NOROTATIONHW;
        }
    }
    if (job.op != DDBLT_ROP)
        job.src = nullptr;
    /* rectangles: the destination is cut by a clipper, the source never. */
    RECT d = dst_in ? *dst_in : (RECT){0, 0, (LONG)dst->desc.dwWidth, (LONG)dst->desc.dwHeight};
    if (d.bottom - d.top <= 0 || d.right - d.left <= 0)
        return DDERR_INVALIDRECT;
    if (!dst->clipper && !rect_inside(&d, dst))
        return DDERR_INVALIDRECT;
    RECT s = {};
    if (job.src) {
        s = src_in ? *src_in : (RECT){0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight};
        if (s.bottom - s.top <= 0 || s.right - s.left <= 0 || !rect_inside(&s, src))
            return DDERR_INVALIDRECT;
        HRESULT hr = stretching(&ch, s.right - s.left, s.bottom - s.top, d.right - d.left, d.bottom - d.top);
        if (FAILED(hr))
            return hr;
    }
    if (ch.force_hw && ch.force_hel)
        return DDERR_NOBLTHW;
    /* the pixels: emulation needs the same format, a driver converts. */
    if (job.src && !same_format(dst, job.src)) {
        if (ch.force_hel || !DdCanConvert(job.src, dst))
            return DDERR_UNSUPPORTED;
        job.convert = TRUE;
    } else if (!hel_format(dst) && !(job.src && same_format(dst, job.src) && is_fourcc(dst))) {
        return DDERR_UNSUPPORTED;
    }
    if (!dst->memory || (job.src && !job.src->memory))
        return DDERR_SURFACELOST;
    DdSurfSync(dst);
    if (job.src)
        DdSurfSync(job.src);
    if (job.op == DDBLT_DEPTHFILL)
        D3dSurfDepthFill(dst, &d, job.fill);
    if (job.src && (is_fourcc(dst) || is_fourcc(job.src)) && !job.convert)
        return DdBltBlocks(dst, &d, job.src, &s);
    RECT dirty = {};
    if (dst->clipper) {
        RGNDATA *region = DdClipRegion(dst->clipper, &d);
        if (!region)
            return DDERR_NOCLIPLIST;
        RECT bounds = {0, 0, (LONG)dst->desc.dwWidth, (LONG)dst->desc.dwHeight};
        RECT *rects = (RECT *)region->Buffer;
        for (DWORD i = 0; i < region->rdh.nCount; i++) {
            RECT c;
            if (!IntersectRect(&c, &rects[i], &d) || !IntersectRect(&c, &c, &bounds))
                continue;
            RECT sc;
            if (job.src)
                xform(&s, &d, &c, &sc, job.mirror_x, job.mirror_y);
            run_rect(&job, &c, &sc);
            UnionRect(&dirty, &dirty, &c);
        }
        HeapFree(GetProcessHeap(), 0, region);
    } else {
        run_rect(&job, &d, &s);
        dirty = d;
    }
    DdSurfWritten(dst, &dirty);
    return DD_OK;
}

/* ------------------------------------------------------------------ BltFast */

HRESULT DdBltFast(DDSURF *dst, DWORD x, DWORD y, DDSURF *src, const RECT *src_in, DWORD trans, DWORD dst_version)
{
    if (dst->lost || src->lost)
        return DDERR_SURFACELOST;
    if (trans & ~0x33u)
        return DDERR_INVALIDPARAMS;
    if ((dst->desc.ddsCaps.dwCaps | src->desc.ddsCaps.dwCaps) & DDSCAPS_OPTIMIZED)
        return DDERR_INVALIDPARAMS;
    if (src->dd && dst->dd && src->dd != dst->dd)
        return DDERR_DEVICEDOESNTOWNSURFACE;
    if (dst_version == DDV7) {
        if (!(trans & DDBLTFAST_DONOTWAIT))
            trans |= DDBLTFAST_WAIT;
        else if (trans & DDBLTFAST_WAIT)
            return DDERR_INVALIDPARAMS;
    }
    if (DdSurfBusy(dst) || DdSurfBusy(src))
        return DDERR_SURFACEBUSY;
    CHOICE ch;
    choose(&ch, dst, src);
    if (!cap_test(&ch, CAPS, DDCAPS_BLT, TRUE))
        return DDERR_NOBLTHW;
    RECT s = src_in ? *src_in : (RECT){0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight};
    if (s.bottom - s.top <= 0 || s.right - s.left <= 0 || !rect_inside(&s, src))
        return DDERR_INVALIDRECT;
    RECT d = {(LONG)x, (LONG)y, (LONG)x + (s.right - s.left), (LONG)y + (s.bottom - s.top)};
    if (!rect_inside(&d, dst))
        return DDERR_INVALIDRECT;
    BLTJOB job = {.dst = dst, .src = src, .op = DDBLT_ROP};
    switch (trans & 0xF) {
    case DDBLTFAST_NOCOLORKEY:
    case DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY:
        break;
    case DDBLTFAST_SRCCOLORKEY:
        if (!(src->ckeys & DDSD_CKSRCBLT))
            return DDERR_INVALIDPARAMS;
        if (!cap_test(&ch, CKEY, DDCKEYCAPS_SRCBLT, TRUE))
            return DDERR_NOCOLORKEYHW;
        job.src_key = TRUE;
        job.src_low = src->desc.ddckCKSrcBlt.dwColorSpaceLowValue;
        break;
    case DDBLTFAST_DESTCOLORKEY:
        if (!(dst->ckeys & DDSD_CKDESTBLT))
            return DDERR_INVALIDPARAMS;
        if (!cap_test(&ch, CKEY, DDCKEYCAPS_DESTBLT, TRUE))
            return DDERR_NOCOLORKEYHW;
        job.dst_key = TRUE;
        job.dst_low = dst->desc.ddckCKDestBlt.dwColorSpaceLowValue;
        break;
    }
    if (ch.force_hw && ch.force_hel)
        return DDERR_NOBLTHW;
    if (dst->clipper)
        return DDERR_BLTFASTCANTCLIP;
    if (!same_format(dst, src)) {
        if (ch.force_hel || !DdCanConvert(src, dst))
            return DDERR_UNSUPPORTED;
        job.convert = TRUE;
    } else if (!hel_format(dst) && !is_fourcc(dst)) {
        return DDERR_UNSUPPORTED;
    }
    if (!dst->memory || !src->memory)
        return DDERR_SURFACELOST;
    DdSurfSync(dst);
    DdSurfSync(src);
    if ((is_fourcc(dst) || is_fourcc(src)) && !job.convert)
        return DdBltBlocks(dst, &d, src, &s);
    run_rect(&job, &d, &s);
    DdSurfWritten(dst, &d);
    return DD_OK;
}

/* windows checks the entries and blts nothing. */
HRESULT DdBltBatch(DDSURF *dst, const DDBLTBATCH *batch, DWORD count)
{
    if (dst->lost)
        return DDERR_SURFACELOST;
    if (DdSurfBusy(dst))
        return DDERR_SURFACEBUSY;
    for (LONG i = 0; i < (LONG)count; i++) {
        DDSURF *src = DdSurfFromIface(batch[i].lpDDSSrc);
        if (src) {
            if (src->lost)
                return DDERR_SURFACELOST;
            if (DdSurfBusy(src))
                return DDERR_SURFACEBUSY;
            if (src->desc.ddsCaps.dwCaps & DDSCAPS_OPTIMIZED)
                return MAKE_DDHRESULT(655);
        }
        if (dst->desc.ddsCaps.dwCaps & DDSCAPS_OPTIMIZED)
            return MAKE_DDHRESULT(655);
        const DDBLTFX *fx = batch[i].lpDDBltFx;
        if (fx ? fx->dwSize != sizeof(DDBLTFX) : (batch[i].dwFlags & 0x02F75D4A) != 0)
            return DDERR_INVALIDPARAMS;
    }
    return DDERR_UNSUPPORTED;
}
