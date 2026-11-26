/*
 * drawing primitives on surfaces and the gdi drawing services.
 */
#include "../win32k.h"
#include "gdimath.h"
#include "ttf/ttf.h"

/* ------------------------------------------------------------------ raster operations */

/* evaluates a ternary raster operation bitwise: bit i of the code is the result for p s d = i. */
static ULONG rop3(UCHAR code, ULONG p, ULONG s, ULONG d)
{
    switch (code) {
    case 0xCC:
        return s;
    case 0xF0:
        return p;
    case 0xAA:
        return d;
    case 0x00:
        return 0;
    case 0xFF:
        return 0xFFFFFFFF;
    case 0x66:
        return s ^ d;
    case 0x88:
        return s & d;
    case 0xEE:
        return s | d;
    case 0x5A:
        return p ^ d;
    case 0x55:
        return ~d;
    case 0x33:
        return ~s;
    }
    ULONG r = 0;
    for (int i = 0; i < 8; i++) {
        if (!((code >> i) & 1))
            continue;
        r |= ((i & 4) ? p : ~p) & ((i & 2) ? s : ~s) & ((i & 1) ? d : ~d);
    }
    return r;
}

static ULONG rop2_apply(int rop2, ULONG pen, ULONG d)
{
    switch (rop2) {
    case R2_BLACK:
        return 0;
    case R2_NOTMERGEPEN:
        return ~(pen | d);
    case R2_MASKNOTPEN:
        return ~pen & d;
    case R2_NOTCOPYPEN:
        return ~pen;
    case R2_MASKPENNOT:
        return pen & ~d;
    case R2_NOT:
        return ~d;
    case R2_XORPEN:
        return pen ^ d;
    case R2_NOTMASKPEN:
        return ~(pen & d);
    case R2_MASKPEN:
        return pen & d;
    case R2_NOTXORPEN:
        return ~(pen ^ d);
    case R2_NOP:
        return d;
    case R2_MERGENOTPEN:
        return ~pen | d;
    case R2_MERGEPENNOT:
        return pen | ~d;
    case R2_MERGEPEN:
        return pen | d;
    case R2_WHITE:
        return 0xFFFFFFFF;
    default:
        return pen;
    }
}

/* ------------------------------------------------------------------ fills */

static void fill_span32(SURFACE *s, LONG y, LONG x0, LONG x1, ULONG pixel)
{
    PULONG row = (PULONG)(s->Bits + (LONG_PTR)y * s->Stride);
    if (s->Flags & SURF_DEVICE) {
        SurfaceSetAlpha(s, x0, y, x1 - x0, (UCHAR)(pixel >> 24));
        pixel |= 0xFF000000u;
    }
    for (LONG x = x0; x < x1; x++)
        row[x] = pixel;
}

void DrawFillRect(SURFACE *s, const REGION *clip, const RECT *rect, ULONG pixel)
{
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++) {
            if (s->Bpp == 32)
                fill_span32(s, y, r.left, r.right, pixel);
            else
                for (LONG x = r.left; x < r.right; x++)
                    SurfacePutPixel(s, x, y, pixel);
        }
    }
}

void DrawFillRectRop(SURFACE *s, const REGION *clip, const RECT *rect, ULONG pixel, int rop2)
{
    if (rop2 == R2_COPYPEN) {
        DrawFillRect(s, clip, rect, pixel);
        return;
    }
    /* the mix works on the stored bits, alpha and palette indices included, as win32kfull's does. */
    ULONG pen = SurfaceRawOf(s, pixel);
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++)
            for (LONG x = r.left; x < r.right; x++)
                SurfacePutRaw(s, x, y, rop2_apply(rop2, pen, SurfaceGetRaw(s, x, y)));
    }
}

static ULONG brush_pixel(DC *dc, BRUSH *b, LONG x, LONG y)
{
    if (b->Header.Handle == GdiStockHandle(DC_BRUSH))
        return DcColorPixel(dc, dc->State.BrushColor);
    if (b->SysColor >= 0)
        return ColorToPixel(MetricsSysColor(b->SysColor));
    x -= dc->State.BrushOrg.x;
    y -= dc->State.BrushOrg.y;
    if (b->Pattern) {
        SURFACE *p = b->Pattern;
        LONG px = ((x % p->Width) + p->Width) % p->Width, py = ((y % p->Height) + p->Height) % p->Height;
        ULONG v = SurfaceGetPixel(p, px, py);
        if (p->Bpp == 1)
            return DcColorPixel(dc, (v & 0xFFFFFF) ? dc->State.BkColor : dc->State.TextColor);
        return v & 0xFFFFFF;
    }
    if (b->Style == BS_HATCHED) {
        LONG hx = ((x % 8) + 8) % 8, hy = ((y % 8) + 8) % 8;
        BOOLEAN on = FALSE;
        switch (b->Hatch) {
        case 0: on = hy == 4; break;           /* HS_HORIZONTAL */
        case 1: on = hx == 4; break;           /* HS_VERTICAL */
        case 2: on = hx == hy; break;          /* HS_FDIAGONAL */
        case 3: on = hx == 7 - hy; break;      /* HS_BDIAGONAL */
        case 4: on = hx == 4 || hy == 4; break; /* HS_CROSS */
        case 5: on = hx == hy || hx == 7 - hy; break;
        }
        return DcColorPixel(dc, on ? b->Color : dc->State.BkColor);
    }
    return DcColorPixel(dc, b->Color);
}

void DrawBrushRect(DC *dc, const REGION *clip, const RECT *rect, BRUSH *brush, DWORD rop)
{
    if (!brush || brush->Style == BS_NULL)
        return;
    UCHAR code = (UCHAR)(rop >> 16);
    SURFACE *s = dc->Surface;
    BOOLEAN solid = brush->Style == BS_SOLID;
    ULONG solid_pixel = solid ? brush_pixel(dc, brush, 0, 0) : 0;
    if (solid && code == 0xF0) {
        DrawFillRect(s, clip, rect, solid_pixel);
        return;
    }
    BOOLEAN needs_dst = ((code >> 1) & 0x55) != (code & 0x55);
    ULONG solid_raw = SurfaceRawOf(s, solid_pixel);
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++) {
            for (LONG x = r.left; x < r.right; x++) {
                ULONG p = solid ? solid_raw : SurfaceRawOf(s, brush_pixel(dc, brush, x - dc->Origin.x, y - dc->Origin.y));
                ULONG d = needs_dst ? SurfaceGetRaw(s, x, y) : 0;
                SurfacePutRaw(s, x, y, rop3(code, p, 0, d));
            }
        }
    }
}

/* ------------------------------------------------------------------ blits */

BOOLEAN BltSurface(SURFACE *dst, const REGION *clip, LONG dx, LONG dy, LONG width, LONG height, SURFACE *src,
                   LONG sx, LONG sy, DWORD rop, DC *dst_dc)
{
    UCHAR code = (UCHAR)(rop >> 16);
    if (width <= 0 || height <= 0)
        return TRUE;
    /* clip the source to its bounds by shrinking the destination rectangle. */
    if (src) {
        if (sx < 0) {
            dx -= sx;
            width += sx;
            sx = 0;
        }
        if (sy < 0) {
            dy -= sy;
            height += sy;
            sy = 0;
        }
        width = W32Min(width, src->Width - sx);
        height = W32Min(height, src->Height - sy);
        if (width <= 0 || height <= 0)
            return TRUE;
    }
    RECT target = {dx, dy, dx + width, dy + height};
    BOOLEAN needs_dst = ((code >> 1) & 0x55) != (code & 0x55);
    BRUSH *brush = dst_dc ? dst_dc->State.Brush : nullptr;
    BOOLEAN needs_pat = ((code >> 4) & 0x0F) != (code & 0x0F);
    BOOLEAN overlap = src == dst && sy < dy;
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], &target))
            continue;
        LONG rows = r.bottom - r.top;
        for (LONG k = 0; k < rows; k++) {
            /* copy bottom up when moving down within one surface. */
            LONG y = overlap ? r.bottom - 1 - k : r.top + k;
            LONG syy = sy + (y - dy);
            if (code == 0xCC && src && src->Bpp == 32 && dst->Bpp == 32) {
                PULONG d = (PULONG)(dst->Bits + (LONG_PTR)y * dst->Stride) + r.left;
                PULONG s = (PULONG)(src->Bits + (LONG_PTR)syy * src->Stride) + sx + (r.left - dx);
                SIZE_T n = (SIZE_T)(r.right - r.left);
                memmove(d, s, n * 4);
                if ((dst->Flags | src->Flags) & SURF_DEVICE)
                    SurfaceCopiedAlpha(dst, r.left, y, src, sx + (r.left - dx), syy, (LONG)n);
                continue;
            }
            for (LONG x = r.left; x < r.right; x++) {
                ULONG s = 0;
                if (src) {
                    s = SurfaceGetPixel(src, sx + (x - dx), syy);
                    if (src->Bpp == 1 && dst->Bpp != 1 && dst_dc) {
                        /* monochrome sources map 0 to the text color and 1 to the background color. */
                        s = DcColorPixel(dst_dc, (s & 0xFFFFFF) ? dst_dc->State.BkColor : dst_dc->State.TextColor);
                    } else if (dst->Bpp == 1 && src->Bpp != 1 && dst_dc) {
                        s = ((s & 0xFFFFFF) == DcColorPixel(dst_dc, dst_dc->State.BkColor)) ? 0xFFFFFF : 0;
                    }
                }
                ULONG p = needs_pat && dst_dc ? brush_pixel(dst_dc, brush, x - dst_dc->Origin.x, y - dst_dc->Origin.y) : 0;
                ULONG d = needs_dst ? SurfaceGetPixel(dst, x, y) : 0;
                ULONG v = rop3(code, p, s, d);
                if (dst->Bpp != 32)
                    v &= 0xFFFFFF;
                SurfacePutPixel(dst, x, y, v);
            }
        }
    }
    return TRUE;
}

BOOLEAN StretchSurface(SURFACE *dst, const REGION *clip, const RECT *dst_rect, SURFACE *src, const RECT *src_rect,
                       DWORD rop, int mode)
{
    LONG dw = dst_rect->right - dst_rect->left, dh = dst_rect->bottom - dst_rect->top;
    LONG sw = src_rect->right - src_rect->left, sh = src_rect->bottom - src_rect->top;
    if (!dw || !dh || !sw || !sh)
        return TRUE;
    UCHAR code = (UCHAR)(rop >> 16);
    RECT target = {W32Min(dst_rect->left, dst_rect->right), W32Min(dst_rect->top, dst_rect->bottom),
                   W32Max(dst_rect->left, dst_rect->right), W32Max(dst_rect->top, dst_rect->bottom)};
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], &target))
            continue;
        for (LONG y = r.top; y < r.bottom; y++) {
            /* nearest sample at the destination pixel center; negative extents mirror. */
            LONG64 fy = ((LONG64)(2 * (y - dst_rect->top) + 1) * sh) / (2 * (LONG64)dh);
            LONG syy = src_rect->top + (LONG)fy;
            if (syy < 0 || syy >= src->Height)
                continue;
            for (LONG x = r.left; x < r.right; x++) {
                LONG64 fx = ((LONG64)(2 * (x - dst_rect->left) + 1) * sw) / (2 * (LONG64)dw);
                LONG sxx = src_rect->left + (LONG)fx;
                if (sxx < 0 || sxx >= src->Width)
                    continue;
                ULONG s = SurfaceGetPixel(src, sxx, syy);
                ULONG d = code == 0xCC ? 0 : SurfaceGetPixel(dst, x, y);
                SurfacePutPixel(dst, x, y, rop3(code, 0, s, d));
            }
        }
    }
    return TRUE;
}

static inline UCHAR div255(ULONG v)
{
    return (UCHAR)((v + 1 + (v >> 8)) >> 8);
}

/* v / 255 to the nearest. */
static inline UCHAR div255_round(ULONG v)
{
    v += 128;
    return (UCHAR)((v + (v >> 8)) >> 8);
}

BOOLEAN AlphaBlendSurface(SURFACE *dst, const REGION *clip, const RECT *dst_rect, SURFACE *src, const RECT *src_rect,
                          BLENDFUNCTION blend)
{
    LONG dw = dst_rect->right - dst_rect->left, dh = dst_rect->bottom - dst_rect->top;
    LONG sw = src_rect->right - src_rect->left, sh = src_rect->bottom - src_rect->top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return FALSE;
    BOOLEAN per_pixel = (blend.AlphaFormat & AC_SRC_ALPHA) && src->Bpp == 32;
    ULONG constant = blend.SourceConstantAlpha;
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], dst_rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++) {
            LONG syy = src_rect->top + (LONG)(((LONG64)(2 * (y - dst_rect->top) + 1) * sh) / (2 * (LONG64)dh));
            for (LONG x = r.left; x < r.right; x++) {
                LONG sxx = src_rect->left + (LONG)(((LONG64)(2 * (x - dst_rect->left) + 1) * sw) / (2 * (LONG64)dw));
                ULONG s = SurfaceGetPixel(src, sxx, syy);
                ULONG sb = s & 0xFF, sg = (s >> 8) & 0xFF, sr = (s >> 16) & 0xFF, sa = per_pixel ? s >> 24 : 255;
                if (constant != 255) {
                    sb = div255(sb * constant);
                    sg = div255(sg * constant);
                    sr = div255(sr * constant);
                    sa = div255(sa * constant);
                } else if (!per_pixel) {
                    sa = 255;
                }
                if (sa == 0 && per_pixel)
                    continue;
                ULONG d = SurfaceGetPixel(dst, x, y);
                ULONG inv = 255 - sa;
                /* the destination's share is rounded: measured with curpx.exe of scratch/dlls/cursor. */
                ULONG ob = sb + div255_round((d & 0xFF) * inv);
                ULONG og = sg + div255_round(((d >> 8) & 0xFF) * inv);
                ULONG orr = sr + div255_round(((d >> 16) & 0xFF) * inv);
                ULONG oa = sa + div255_round((d >> 24) * inv);
                SurfacePutPixel(dst, x, y, (oa << 24) | (W32Min((LONG)orr, 255) << 16) | (W32Min((LONG)og, 255) << 8) |
                                               W32Min((LONG)ob, 255));
            }
        }
    }
    return TRUE;
}

/* blends an 8 bit coverage mask in a solid color. */
void DrawCoverage(SURFACE *s, const REGION *clip, LONG x, LONG y, const UCHAR *coverage, LONG width, LONG height,
                  LONG stride, ULONG pixel)
{
    RECT box = {x, y, x + width, y + height};
    ULONG cr = (pixel >> 16) & 0xFF, cg = (pixel >> 8) & 0xFF, cb = pixel & 0xFF;
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], &box))
            continue;
        for (LONG yy = r.top; yy < r.bottom; yy++) {
            const UCHAR *cov = coverage + (yy - y) * stride;
            for (LONG xx = r.left; xx < r.right; xx++) {
                ULONG a = cov[xx - x];
                if (!a)
                    continue;
                if (a == 255) {
                    SurfacePutPixel(s, xx, yy, pixel);
                    continue;
                }
                ULONG d = SurfaceGetPixel(s, xx, yy);
                ULONG inv = 255 - a;
                ULONG nb = div255(cb * a + (d & 0xFF) * inv);
                ULONG ng = div255(cg * a + ((d >> 8) & 0xFF) * inv);
                ULONG nr = div255(cr * a + ((d >> 16) & 0xFF) * inv);
                SurfacePutPixel(s, xx, yy, (d & 0xFF000000u) | (nr << 16) | (ng << 8) | nb);
            }
        }
    }
}

/*
 * ClearType: every channel of a pixel has its own coverage, a level from 0 to 6. the blend happens in a space made
 * with the font smoothing gamma, as windows does it: the destination and the color go through the table, are mixed
 * by the level in twenty bit fractions of a sixth, and come back through the inverse table.
 */
static UINT g_ct_gamma;
static UCHAR g_ct_linear[256], g_ct_encoded[256];

static void clear_type_tables(UINT gamma)
{
    if (g_ct_gamma != gamma) {
        mime_ttf_gamma_tables(gamma, g_ct_linear, g_ct_encoded);
        g_ct_gamma = gamma;
    }
}

static BOOLEAN surface_takes_clear_type(const SURFACE *s)
{
    if (s->Bpp < 16)
        return FALSE;
    /* the pixels of a layered window carry alpha that stripes of color would break. */
    if (s->Window && s->Window->LayerFlags)
        return FALSE;
    return TRUE;
}

static void draw_subpixel(SURFACE *s, const REGION *clip, LONG x, LONG y, const UCHAR *levels, LONG width, LONG height,
                          ULONG pixel)
{
    static const LONG weight[7] = {0, 0x2AAAB, 0x55555, 0x80000, 0xAAAAB, 0xD5555, 0x100000};
    UINT type, gamma, orientation;
    MetricsFontSmoothing(&type, &gamma, &orientation);
    clear_type_tables(gamma);
    LONG fg[3] = {g_ct_linear[(pixel >> 16) & 0xFF], g_ct_linear[(pixel >> 8) & 0xFF], g_ct_linear[pixel & 0xFF]};
    /* the stripes run left to right, red first unless the panel is the other way round. */
    int stripe_r = orientation ? 0 : 2, stripe_b = orientation ? 2 : 0;
    RECT box = {x, y, x + width, y + height};
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], &box))
            continue;
        for (LONG yy = r.top; yy < r.bottom; yy++) {
            const UCHAR *row = levels + (SIZE_T)(yy - y) * (SIZE_T)width * 3;
            for (LONG xx = r.left; xx < r.right; xx++) {
                const UCHAR *k = row + (SIZE_T)(xx - x) * 3;
                if (!(k[0] | k[1] | k[2]))
                    continue;
                ULONG d = SurfaceGetPixel(s, xx, yy);
                if (k[0] == 6 && k[1] == 6 && k[2] == 6) {
                    SurfacePutPixel(s, xx, yy, (d & 0xFF000000u) | (pixel & 0xFFFFFF));
                    continue;
                }
                LONG dr = g_ct_linear[(d >> 16) & 0xFF], dg = g_ct_linear[(d >> 8) & 0xFF], db = g_ct_linear[d & 0xFF];
                LONG nr = dr + (LONG)(((LONG64)weight[k[stripe_r]] * (fg[0] - dr) + 0x80000) >> 20);
                LONG ng = dg + (LONG)(((LONG64)weight[k[1]] * (fg[1] - dg) + 0x80000) >> 20);
                LONG nb = db + (LONG)(((LONG64)weight[k[stripe_b]] * (fg[2] - db) + 0x80000) >> 20);
                SurfacePutPixel(s, xx, yy,
                                (d & 0xFF000000u) | ((ULONG)g_ct_encoded[nr] << 16) | ((ULONG)g_ct_encoded[ng] << 8) |
                                    g_ct_encoded[nb]);
            }
        }
    }
}

void DrawGlyph(SURFACE *s, const REGION *clip, LONG x, LONG y, const GLYPH *g, ULONG pixel)
{
    if (g->Subpixel && g->SubWidth && g->SubHeight && surface_takes_clear_type(s)) {
        draw_subpixel(s, clip, x + g->SubLeft, y + g->SubTop, g->Subpixel, g->SubWidth, g->SubHeight, pixel);
        return;
    }
    if (g->Coverage && g->Width && g->Height)
        DrawCoverage(s, clip, x + g->Left, y + g->Top, g->Coverage, g->Width, g->Height, g->Width, pixel);
}

/* ------------------------------------------------------------------ helpers for services */

typedef struct DRAW_CONTEXT {
    DC *Dc;
    REGION Clip;
} DRAW_CONTEXT;

static BOOLEAN begin_draw(HDC hdc, DRAW_CONTEXT *ctx)
{
    ctx->Dc = DcLookup(hdc);
    RgnInit(&ctx->Clip);
    if (!ctx->Dc || !ctx->Dc->Surface)
        return FALSE;
    return DcGetClip(ctx->Dc, &ctx->Clip);
}

static void end_draw(DRAW_CONTEXT *ctx, const RECT *touched)
{
    if (ctx->Dc && ctx->Dc->Surface && touched)
        DcMarkDrawn(ctx->Dc, touched, &ctx->Clip);
    RgnFree(&ctx->Clip);
}

static RECT logical_rect_to_surface(DC *dc, LONG left, LONG top, LONG right, LONG bottom)
{
    DcLogicalToSurface(dc, &left, &top);
    DcLogicalToSurface(dc, &right, &bottom);
    RECT r = {W32Min(left, right), W32Min(top, bottom), W32Max(left, right), W32Max(top, bottom)};
    return r;
}

/* rectangles, ellipses and round rectangles include their right and bottom edges in the advanced mode. */
static RECT shape_rect(DC *dc, LONG left, LONG top, LONG right, LONG bottom)
{
    RECT r = logical_rect_to_surface(dc, left, top, right, bottom);
    if (dc->State.GraphicsMode == GM_ADVANCED) {
        r.right++;
        r.bottom++;
    }
    return r;
}

/* the inside of a shape is the brush through the mix mode, as R2_XORPEN xors it: the rop3 of each rop2. */
static DWORD brush_rop(DC *dc)
{
    static const DWORD rop_of_rop2[16] = {BLACKNESS, 0x050000, 0x0A0000, 0x0F0000, 0x500000, DSTINVERT, PATINVERT,
                                          0x5F0000, 0xA00000, 0xA50000, 0xAA0000, 0xAF0000, PATCOPY, 0xF50000,
                                          0xFA0000, WHITENESS};
    int rop2 = dc->State.Rop2;
    return rop2 >= R2_BLACK && rop2 <= R2_WHITE ? rop_of_rop2[rop2 - 1] : PATCOPY;
}

static ULONG pen_pixel(DC *dc)
{
    PEN *p = dc->State.Pen;
    if (p->Header.Handle == GdiStockHandle(DC_PEN))
        return DcColorPixel(dc, dc->State.PenColor);
    return DcColorPixel(dc, p->Color);
}

/* ------------------------------------------------------------------ services */

/* a raster operation that reads a source, which PatBlt and PolyPatBlt refuse. */
static BOOLEAN rop_uses_source(DWORD rop)
{
    UCHAR code = (UCHAR)(rop >> 16);
    return ((code ^ (code << 2)) & 0xCC) != 0;
}

/* PatBlt on a dc the caller holds, with a brush of its own or the one of the dc for null. */
BOOL GdiPatBltDc(DC *dc, int x, int y, int width, int height, DWORD rop, BRUSH *brush)
{
    if (!dc->Surface)
        return TRUE;
    REGION clip;
    RgnInit(&clip);
    if (DcGetClip(dc, &clip)) {
        RECT r = logical_rect_to_surface(dc, x, y, x + width, y + height);
        DrawBrushRect(dc, &clip, &r, brush ? brush : dc->State.Brush, rop);
        DcMarkDrawn(dc, &r, &clip);
    }
    RgnFree(&clip);
    return TRUE;
}

/*
 * PolyPatBlt and FillRect of user32: rectangles of 24 bytes, each with a brush of its own or null for the one of
 * the dc. like win32kfull's GrepPolyPatBlt, a mode other than 0 or a raster operation with a source fails.
 */
BOOL GdiPolyPatBltDc(DC *dc, DWORD rop, const UCHAR *list, ULONG count)
{
    for (ULONG i = 0; i < count; i++) {
        const UCHAR *item = list + (SIZE_T)i * 24;
        int r[4];
        HBRUSH h;
        memcpy(r, item, sizeof(r));
        memcpy(&h, item + 16, sizeof(h));
        BRUSH *brush = h ? (BRUSH *)GdiLookup(h, GDI_TYPE_BRUSH) : nullptr;
        GdiPatBltDc(dc, r[0], r[1], r[2], r[3], rop, brush);
    }
    return TRUE;
}

BOOL NTAPI NtGdiPolyPatBlt(HDC hdc, DWORD rop, const void *user_list, ULONG count, ULONG mode)
{
    if (!count)
        return TRUE;
    if (!user_list || mode || count > 0x100000 || rop_uses_source(rop))
        return FALSE;
    UCHAR *list = W32Alloc((SIZE_T)count * 24);
    if (!list)
        return FALSE;
    BOOL ok = FALSE;
    if (NT_SUCCESS(MimeCopyFromUser(list, user_list, (SIZE_T)count * 24))) {
        UserEnterRead();
        DC *dc = DcLookup(hdc);
        if (dc)
            ok = GdiPolyPatBltDc(dc, rop, list, count);
        UserLeave();
    }
    W32Free(list);
    return ok;
}

BOOL NTAPI NtGdiPatBlt(HDC hdc, int x, int y, int width, int height, DWORD rop)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc && GdiPatBltDc(dc, x, y, width, height, rop, nullptr);
    UserLeave();
    return ok;
}

static BOOL stretch_blt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y,
                        int src_width, int src_height, DWORD rop);

/* a scaling transform on either dc stretches. */
static BOOLEAN blt_scales(HDC hdst, HDC hsrc)
{
    UserEnterRead();
    DC *d = DcLookup(hdst), *s = hsrc ? DcLookup(hsrc) : nullptr;
    BOOLEAN scaled = (d && DcIsScaled(d)) || (s && DcIsScaled(s));
    UserLeave();
    return scaled;
}

BOOL NTAPI NtGdiBitBlt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y, DWORD rop,
                       DWORD bk_color, ULONG flags)
{
    UCHAR code = (UCHAR)(rop >> 16);
    BOOLEAN uses_src = ((code >> 2) & 0x33) != (code & 0x33);
    if (uses_src && blt_scales(hdst, hsrc))
        return stretch_blt(hdst, x, y, width, height, hsrc, src_x, src_y, width, height, rop);
    UserEnterRead();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT r = {0, 0, 0, 0};
    DC *src = hsrc ? DcLookup(hsrc) : nullptr;
    if (begin_draw(hdst, &ctx)) {
        if (!uses_src) {
            r = logical_rect_to_surface(ctx.Dc, x, y, x + width, y + height);
            DrawBrushRect(ctx.Dc, &ctx.Clip, &r, ctx.Dc->State.Brush, rop);
            ok = TRUE;
        } else if (src && src->Surface) {
            LONG dx = x, dy = y, sx = src_x, sy = src_y;
            DcLogicalToSurface(ctx.Dc, &dx, &dy);
            DcLogicalToSurface(src, &sx, &sy);
            if (ScreenIsSurface(src->Surface))
                ScreenComposeFor(src->Surface, &(RECT){sx, sy, sx + width, sy + height});
            /* restrict the source to what the source dc may read. */
            REGION src_vis;
            RgnInit(&src_vis);
            RgnCopy(&src_vis, &src->VisRgn);
            RgnOffset(&src_vis, dx - sx, dy - sy);
            RgnCombine(&ctx.Clip, &ctx.Clip, &src_vis, RGN_AND);
            RgnFree(&src_vis);
            ok = BltSurface(ctx.Dc->Surface, &ctx.Clip, dx, dy, width, height, src->Surface, sx, sy, rop, ctx.Dc);
            r = (RECT){dx, dy, dx + width, dy + height};
        }
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &r);
    UserLeave();
    return ok;
}

/* copies from a surface win32k keeps for itself, the strip of the system bitmaps, at a position of the dc. */
BOOL GdiBltFromSurface(HDC hdst, int x, int y, int width, int height, SURFACE *src, int src_x, int src_y, DWORD rop)
{
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT r = {0, 0, 0, 0};
    if (begin_draw(hdst, &ctx)) {
        LONG dx = x, dy = y;
        DcLogicalToSurface(ctx.Dc, &dx, &dy);
        ok = BltSurface(ctx.Dc->Surface, &ctx.Clip, dx, dy, width, height, src, src_x, src_y, rop, ctx.Dc);
        r = (RECT){dx, dy, dx + width, dy + height};
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &r);
    return ok;
}

BOOL NTAPI NtGdiStretchBlt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y,
                           int src_width, int src_height, DWORD rop, DWORD bk_color)
{
    if (width == src_width && height == src_height && !blt_scales(hdst, hsrc))
        return NtGdiBitBlt(hdst, x, y, width, height, hsrc, src_x, src_y, rop, bk_color, 0);
    return stretch_blt(hdst, x, y, width, height, hsrc, src_x, src_y, src_width, src_height, rop);
}

static BOOL stretch_blt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y,
                        int src_width, int src_height, DWORD rop)
{
    UserEnterRead();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    DC *src = DcLookup(hsrc);
    if (begin_draw(hdst, &ctx) && src && src->Surface) {
        LONG x0 = x, y0 = y, x1 = x + width, y1 = y + height;
        DcLogicalToSurface(ctx.Dc, &x0, &y0);
        DcLogicalToSurface(ctx.Dc, &x1, &y1);
        LONG sx0 = src_x, sy0 = src_y, sx1 = src_x + src_width, sy1 = src_y + src_height;
        DcLogicalToSurface(src, &sx0, &sy0);
        DcLogicalToSurface(src, &sx1, &sy1);
        RECT dr = {x0, y0, x1, y1}, sr = {sx0, sy0, sx1, sy1};
        if (ScreenIsSurface(src->Surface))
            ScreenComposeFor(src->Surface, &(RECT){W32Min(sx0, sx1), W32Min(sy0, sy1), W32Max(sx0, sx1), W32Max(sy0, sy1)});
        ok = StretchSurface(ctx.Dc->Surface, &ctx.Clip, &dr, src->Surface, &sr, rop, ctx.Dc->State.StretchMode);
        dirty = (RECT){W32Min(x0, x1), W32Min(y0, y1), W32Max(x0, x1), W32Max(y0, y1)};
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

BOOL NTAPI NtGdiAlphaBlend(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y,
                           int src_width, int src_height, BLENDFUNCTION blend, HANDLE transform)
{
    UserEnterRead();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    DC *src = DcLookup(hsrc);
    if (begin_draw(hdst, &ctx) && src && src->Surface) {
        RECT dr = logical_rect_to_surface(ctx.Dc, x, y, x + width, y + height);
        RECT sr = logical_rect_to_surface(src, src_x, src_y, src_x + src_width, src_y + src_height);
        if (ScreenIsSurface(src->Surface))
            ScreenComposeFor(src->Surface, &sr);
        RECT sb = {0, 0, src->Surface->Width, src->Surface->Height}, tmp;
        if (RectIntersect(&tmp, &sr, &sb) && tmp.left == sr.left && tmp.right == sr.right && tmp.top == sr.top &&
            tmp.bottom == sr.bottom)
            ok = AlphaBlendSurface(ctx.Dc->Surface, &ctx.Clip, &dr, src->Surface, &sr, blend);
        dirty = dr;
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

/* a pixel of a source surface as a colour of the destination: a monochrome one takes the colours of the dc. */
static ULONG source_pixel(DC *dst, SURFACE *src, LONG x, LONG y)
{
    ULONG v = SurfaceGetPixel(src, x, y);
    if (src->Bpp == 1)
        return DcColorPixel(dst, (v & 0xFFFFFF) ? dst->State.BkColor : dst->State.TextColor);
    return v & 0xFFFFFF;
}

/* the source rectangle stretched over the destination by nearest pixels, without the colour of the key. */
BOOL NTAPI NtGdiTransparentBlt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y,
                               int src_width, int src_height, COLORREF transparent)
{
    if (width <= 0 || height <= 0 || src_width <= 0 || src_height <= 0)
        return FALSE;
    UserEnterRead();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    DC *src = DcLookup(hsrc);
    if (begin_draw(hdst, &ctx) && src && src->Surface) {
        RECT dr = logical_rect_to_surface(ctx.Dc, x, y, x + width, y + height);
        RECT sr = logical_rect_to_surface(src, src_x, src_y, src_x + src_width, src_y + src_height);
        SURFACE *ss = src->Surface, *ds = ctx.Dc->Surface;
        if (ScreenIsSurface(ss))
            ScreenComposeFor(ss, &sr);
        ULONG key = SurfaceRawOf(ss, ColorToPixel(transparent));
        ULONG mask = ss->Bpp == 32 ? 0xFFFFFF : 0xFFFFFFFF;
        LONG dw = dr.right - dr.left, dh = dr.bottom - dr.top, sw = sr.right - sr.left, sh = sr.bottom - sr.top;
        for (ULONG i = 0; dw > 0 && dh > 0 && i < ctx.Clip.Count; i++) {
            RECT r;
            if (!RectIntersect(&r, &ctx.Clip.Rects[i], &dr))
                continue;
            for (LONG py = r.top; py < r.bottom; py++) {
                LONG sy = sr.top + (LONG)((LONG64)(py - dr.top) * sh / dh);
                if (sy < 0 || sy >= ss->Height)
                    continue;
                for (LONG px = r.left; px < r.right; px++) {
                    LONG sx = sr.left + (LONG)((LONG64)(px - dr.left) * sw / dw);
                    if (sx < 0 || sx >= ss->Width || (SurfaceGetRaw(ss, sx, sy) & mask) == (key & mask))
                        continue;
                    SurfacePutPixel(ds, px, py, source_pixel(ctx.Dc, ss, sx, sy));
                }
            }
        }
        dirty = dr;
        ok = TRUE;
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

/*
 * where the monochrome mask has a 1 the foreground rop of the low three bytes of rop4 runs, elsewhere the background
 * rop of its high byte. without a mask it is a BitBlt of the foreground rop.
 */
BOOL NTAPI NtGdiMaskBlt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y, HBITMAP mask,
                        int mask_x, int mask_y, DWORD rop4, DWORD bk_color)
{
    DWORD fore = rop4 & 0xFFFFFF;
    if (!mask)
        return NtGdiBitBlt(hdst, x, y, width, height, hsrc, src_x, src_y, fore, bk_color, 0);
    UserEnterRead();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    DC *src = hsrc ? DcLookup(hsrc) : nullptr;
    SURFACE *m = SurfaceLookup(mask);
    if (begin_draw(hdst, &ctx) && m && m->Bpp == 1) {
        DC *dc = ctx.Dc;
        SURFACE *ds = dc->Surface, *ss = src ? src->Surface : nullptr;
        UCHAR fcode = (UCHAR)(fore >> 16), bcode = (UCHAR)(rop4 >> 24);
        LONG dx = x, dy = y, sx = src_x, sy = src_y;
        DcLogicalToSurface(dc, &dx, &dy);
        if (src)
            DcLogicalToSurface(src, &sx, &sy);
        RECT dr = {dx, dy, dx + width, dy + height};
        for (ULONG i = 0; i < ctx.Clip.Count; i++) {
            RECT r;
            if (!RectIntersect(&r, &ctx.Clip.Rects[i], &dr))
                continue;
            for (LONG py = r.top; py < r.bottom; py++) {
                for (LONG px = r.left; px < r.right; px++) {
                    LONG mx = mask_x + px - dx, my = mask_y + py - dy;
                    BOOLEAN on = mx >= 0 && my >= 0 && mx < m->Width && my < m->Height && SurfaceGetRaw(m, mx, my);
                    UCHAR code = on ? fcode : bcode;
                    LONG qx = sx + px - dx, qy = sy + py - dy;
                    ULONG s = ss && qx >= 0 && qy >= 0 && qx < ss->Width && qy < ss->Height
                                  ? SurfaceRawOf(ds, source_pixel(dc, ss, qx, qy))
                                  : 0;
                    ULONG p = SurfaceRawOf(ds, brush_pixel(dc, dc->State.Brush, px - dc->Origin.x, py - dc->Origin.y));
                    SurfacePutRaw(ds, px, py, rop3(code, p, s, SurfaceGetRaw(ds, px, py)));
                }
            }
        }
        dirty = dr;
        ok = TRUE;
    } else if (ctx.Dc) {
        ok = m != nullptr;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

typedef struct GRADIENT_VERTEX {
    LONG X, Y;
    USHORT Red, Green, Blue, Alpha;
} GRADIENT_VERTEX;

static void gradient_put(DC *dc, SURFACE *s, LONG x, LONG y, const double c[4])
{
    ULONG v = (ULONG)((USHORT)c[2] >> 8) | (ULONG)((USHORT)c[1] >> 8) << 8 | (ULONG)((USHORT)c[0] >> 8) << 16;
    if (s->Bpp == 32)
        SurfacePutRaw(s, x, y, v | (ULONG)((USHORT)c[3] >> 8) << 24);
    else
        SurfacePutPixel(s, x, y, v);
}

/* modes 0 and 1 shade rectangles across and down, 2 triangles, the colours in 16 bits each. */
BOOL NTAPI NtGdiGradientFill(HDC hdc, PVOID user_vertices, ULONG vertices, PVOID user_mesh, ULONG meshes, ULONG mode)
{
    if (!vertices || !meshes || mode > 2 || vertices > 0x100000 || meshes > 0x100000)
        return FALSE;
    ULONG each = mode == 2 ? 3 : 2;
    GRADIENT_VERTEX *v = W32Alloc((SIZE_T)vertices * sizeof(*v));
    ULONG *mesh = W32Alloc((SIZE_T)meshes * each * sizeof(ULONG));
    BOOL ok = v && mesh && NT_SUCCESS(MimeCopyFromUser(v, user_vertices, (SIZE_T)vertices * sizeof(*v))) &&
              NT_SUCCESS(MimeCopyFromUser(mesh, user_mesh, (SIZE_T)meshes * each * sizeof(ULONG)));
    for (ULONG i = 0; ok && i < meshes * each; i++)
        ok = mesh[i] < vertices;
    UserEnterRead();
    DRAW_CONTEXT ctx;
    RECT dirty = {0, 0, 0, 0};
    if (ok && begin_draw(hdc, &ctx)) {
        DC *dc = ctx.Dc;
        for (ULONG i = 0; i < vertices; i++)
            DcLogicalToSurface(dc, &v[i].X, &v[i].Y);
        for (ULONG k = 0; k < meshes; k++) {
            const ULONG *idx = mesh + k * each;
            RECT box;
            if (mode != 2) {
                const GRADIENT_VERTEX *a = &v[idx[0]], *b = &v[idx[1]];
                if (a->X > b->X || (mode == 1 && a->Y > b->Y)) {
                    const GRADIENT_VERTEX *t = a;
                    a = b;
                    b = t;
                }
                box = (RECT){W32Min(a->X, b->X), W32Min(a->Y, b->Y), W32Max(a->X, b->X), W32Max(a->Y, b->Y)};
                LONG span = mode == 0 ? box.right - box.left : box.bottom - box.top;
                for (ULONG i = 0; span > 0 && i < ctx.Clip.Count; i++) {
                    RECT r;
                    if (!RectIntersect(&r, &ctx.Clip.Rects[i], &box))
                        continue;
                    for (LONG y = r.top; y < r.bottom; y++)
                        for (LONG x = r.left; x < r.right; x++) {
                            double t = (double)(mode == 0 ? x - box.left : y - box.top) / span;
                            double c[4] = {a->Red + (b->Red - a->Red) * t, a->Green + (b->Green - a->Green) * t,
                                           a->Blue + (b->Blue - a->Blue) * t, a->Alpha + (b->Alpha - a->Alpha) * t};
                            gradient_put(dc, dc->Surface, x, y, c);
                        }
                }
            } else {
                const GRADIENT_VERTEX *a = &v[idx[0]], *b = &v[idx[1]], *c3 = &v[idx[2]];
                box = (RECT){W32Min(a->X, W32Min(b->X, c3->X)), W32Min(a->Y, W32Min(b->Y, c3->Y)),
                             W32Max(a->X, W32Max(b->X, c3->X)) + 1, W32Max(a->Y, W32Max(b->Y, c3->Y)) + 1};
                double area = (double)(b->X - a->X) * (c3->Y - a->Y) - (double)(c3->X - a->X) * (b->Y - a->Y);
                for (ULONG i = 0; area != 0 && i < ctx.Clip.Count; i++) {
                    RECT r;
                    if (!RectIntersect(&r, &ctx.Clip.Rects[i], &box))
                        continue;
                    for (LONG y = r.top; y < r.bottom; y++)
                        for (LONG x = r.left; x < r.right; x++) {
                            double w1 = ((double)(b->X - x) * (c3->Y - y) - (double)(c3->X - x) * (b->Y - y)) / area;
                            double w2 = ((double)(c3->X - x) * (a->Y - y) - (double)(a->X - x) * (c3->Y - y)) / area;
                            double w3 = 1 - w1 - w2;
                            if (w1 < 0 || w2 < 0 || w3 < 0)
                                continue;
                            double c[4] = {a->Red * w1 + b->Red * w2 + c3->Red * w3,
                                           a->Green * w1 + b->Green * w2 + c3->Green * w3,
                                           a->Blue * w1 + b->Blue * w2 + c3->Blue * w3,
                                           a->Alpha * w1 + b->Alpha * w2 + c3->Alpha * w3};
                            gradient_put(dc, dc->Surface, x, y, c);
                        }
                }
            }
            RectUnion(&dirty, &dirty, &box);
        }
    }
    if (ok)
        end_draw(&ctx, &dirty);
    UserLeave();
    W32Free(v);
    W32Free(mesh);
    return ok;
}

/* bresenham in surface coordinates, last point excluded like gdi. */
void DrawLine(DC *dc, const REGION *clip, LONG x0, LONG y0, LONG x1, LONG y1, RECT *dirty)
{
    PEN *pen = dc->State.Pen;
    if (PenStyle(pen) == PS_NULL)
        return;
    ULONG px = pen_pixel(dc);
    LONG width = DcPenWidth(dc);
    LONG dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    LONG sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    LONG step = 0;
    while (x0 != x1 || y0 != y1) {
        BOOLEAN on = TRUE;
        if (PenStyle(pen) == PS_DOT)
            on = (step & 1) == 0;
        else if (PenStyle(pen) == PS_DASH)
            on = (step % 24) < 18;
        if (on) {
            RECT r = {x0 - (width - 1) / 2, y0 - (width - 1) / 2, x0 - (width - 1) / 2 + width, y0 - (width - 1) / 2 + width};
            DrawFillRectRop(dc->Surface, clip, &r, px, dc->State.Rop2);
            RectUnion(dirty, dirty, &r);
        }
        step++;
        LONG e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* while a path is open, drawing calls only record figures. */
static DC *path_dc(HDC hdc)
{
    DC *dc = DcLookup(hdc);
    return dc && DcIsPathOpen(dc) ? dc : nullptr;
}

BOOL NTAPI NtGdiLineTo(HDC hdc, int x, int y)
{
    UserEnterRead();
    DC *pdc = path_dc(hdc);
    if (pdc) {
        BOOL recorded = PathLineTo(pdc, x, y);
        pdc->State.CurrentPos = (POINT){x, y};
        UserLeave();
        return recorded;
    }
    DRAW_CONTEXT ctx;
    RECT dirty = {0, 0, 0, 0};
    BOOL ok = begin_draw(hdc, &ctx) || ctx.Dc;
    if (ctx.Dc && !RgnIsEmpty(&ctx.Clip)) {
        LONG x0 = ctx.Dc->State.CurrentPos.x, y0 = ctx.Dc->State.CurrentPos.y, x1 = x, y1 = y;
        DcLogicalToSurface(ctx.Dc, &x0, &y0);
        DcLogicalToSurface(ctx.Dc, &x1, &y1);
        DrawLine(ctx.Dc, &ctx.Clip, x0, y0, x1, y1, &dirty);
    }
    if (ctx.Dc)
        ctx.Dc->State.CurrentPos = (POINT){x, y};
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

/*
 * the frame of a rectangle: a pen centred on the edges l, t, r - 1 and b - 1, or inside them for PS_INSIDEFRAME,
 * with square corners. each pixel is drawn once, which a mix mode like R2_XORPEN shows. measured with rx.exe of
 * scratch/dlls/gdibatch.
 */
static void frame_rect_pen(DC *dc, const REGION *clip, const RECT *r, RECT *dirty)
{
    PEN *pen = dc->State.Pen;
    if (PenStyle(pen) == PS_NULL)
        return;
    LONG w = DcPenWidth(dc);
    ULONG px = pen_pixel(dc);
    RECT o = *r;
    if (PenStyle(pen) != PS_INSIDEFRAME && w > 1) {
        o = (RECT){r->left - (w - 1) / 2, r->top - (w - 1) / 2, r->right + w / 2, r->bottom + w / 2};
    }
    LONG t = w;
    if (2 * t > o.right - o.left || 2 * t > o.bottom - o.top) {
        DrawFillRectRop(dc->Surface, clip, &o, px, dc->State.Rop2);
    } else {
        RECT edges[4] = {
            {o.left, o.top, o.right, o.top + t},
            {o.left, o.bottom - t, o.right, o.bottom},
            {o.left, o.top + t, o.left + t, o.bottom - t},
            {o.right - t, o.top + t, o.right, o.bottom - t},
        };
        for (int i = 0; i < 4; i++)
            DrawFillRectRop(dc->Surface, clip, &edges[i], px, dc->State.Rop2);
    }
    RectUnion(dirty, dirty, &o);
}

BOOL NTAPI NtGdiRectangle(HDC hdc, int left, int top, int right, int bottom)
{
    UserEnterRead();
    DC *pdc = path_dc(hdc);
    if (pdc) {
        BOOL recorded = PathRectangle(pdc, left, top, right, bottom);
        UserLeave();
        return recorded;
    }
    DRAW_CONTEXT ctx;
    RECT dirty = {0, 0, 0, 0};
    BOOL ok = FALSE;
    if (begin_draw(hdc, &ctx)) {
        RECT r = shape_rect(ctx.Dc, left, top, right, bottom);
        /* the inside stops at the pen, and with a null pen the rectangle loses its right and bottom pixel. */
        RECT inner = r;
        if (PenStyle(ctx.Dc->State.Pen) == PS_NULL) {
            inner.right--;
            inner.bottom--;
        } else {
            inner = (RECT){r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        }
        DrawBrushRect(ctx.Dc, &ctx.Clip, &inner, ctx.Dc->State.Brush, brush_rop(ctx.Dc));
        frame_rect_pen(ctx.Dc, &ctx.Clip, &r, &dirty);
        RectUnion(&dirty, &dirty, &inner);
        ok = TRUE;
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

/*
 * an ellipse or a rectangle with elliptic corners, without smoothing like windows: the shape is the region of the
 * same corners grown by the last column and row, which Ellipse draws, the pen takes the ring of its width inside it
 * and the brush the rest. windows makes both from a flattened path, so the corners may differ by a pixel.
 */
static BOOL draw_round_rect(HDC hdc, int left, int top, int right, int bottom, int width, int height, BOOLEAN ellipse)
{
    DRAW_CONTEXT ctx;
    RECT dirty = {0, 0, 0, 0};
    BOOL ok = FALSE;
    UserEnterRead();
    if (begin_draw(hdc, &ctx)) {
        DC *dc = ctx.Dc;
        RECT r = shape_rect(dc, left, top, right, bottom);
        PEN *p = dc->State.Pen;
        LONG w = PenStyle(p) == PS_NULL ? 0 : DcPenWidth(dc);
        LONG ew = ellipse ? r.right - r.left : DcScaleToDevice(dc, width < 0 ? -width : width);
        LONG eh = ellipse ? r.bottom - r.top : DcScaleToDevice(dc, height < 0 ? -height : height);
        LONG grow = w > 1 && PenStyle(p) != PS_INSIDEFRAME ? (w - 1) / 2 : 0;
        RECT o = {r.left - grow, r.top - grow, r.right + 1 + grow, r.bottom + 1 + grow};
        REGION outer, inner, part;
        RgnInit(&outer);
        RgnInit(&inner);
        RgnInit(&part);
        RgnRoundRect(&outer, o.left, o.top, o.right, o.bottom, ew + 2 * grow, eh + 2 * grow);
        if (w)
            RgnRoundRect(&inner, o.left + w, o.top + w, o.right - w, o.bottom - w, ew + 2 * grow - 2 * w,
                         eh + 2 * grow - 2 * w);
        else
            RgnRoundRect(&inner, r.left, r.top, r.right, r.bottom, ew, eh);
        if (dc->State.Brush->Style != BS_NULL) {
            RgnCombine(&part, &inner, &ctx.Clip, RGN_AND);
            DrawBrushRect(dc, &part, &part.Extents, dc->State.Brush, brush_rop(dc));
        }
        if (w) {
            RgnCombine(&part, &outer, &inner, RGN_DIFF);
            RgnCombine(&part, &part, &ctx.Clip, RGN_AND);
            ULONG px = pen_pixel(dc);
            for (ULONG i = 0; i < part.Count; i++)
                DrawFillRectRop(dc->Surface, &part, &part.Rects[i], px, dc->State.Rop2);
        }
        dirty = outer.Extents;
        RgnFree(&outer);
        RgnFree(&inner);
        RgnFree(&part);
        ok = TRUE;
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

BOOL NTAPI NtGdiRoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height)
{
    UserEnterRead();
    DC *pdc = path_dc(hdc);
    if (pdc) {
        /* a path keeps the rounded rectangle as a plain rectangle. */
        BOOL recorded = PathRectangle(pdc, left, top, right, bottom);
        UserLeave();
        return recorded;
    }
    UserLeave();
    return draw_round_rect(hdc, left, top, right, bottom, width, height, FALSE);
}

BOOL NTAPI NtGdiEllipse(HDC hdc, int left, int top, int right, int bottom)
{
    UserEnterRead();
    DC *pdc = path_dc(hdc);
    if (pdc) {
        BOOL recorded = PathEllipse(pdc, left, top, right, bottom);
        UserLeave();
        return recorded;
    }
    UserLeave();
    return draw_round_rect(hdc, left, top, right, bottom, 0, 0, TRUE);
}

/* flattens one cubic bezier from p0 into 16 segments, in surface coordinates. */
static void draw_bezier(DC *dc, const REGION *clip, POINT p0, const POINT *c, RECT *dirty)
{
    LONG px = p0.x, py = p0.y;
    for (int s = 1; s <= 16; s++) {
        LONG64 t = s, u = 16 - s, d = 16 * 16 * 16;
        LONG64 x = u * u * u * p0.x + 3 * u * u * t * c[0].x + 3 * u * t * t * c[1].x + t * t * t * c[2].x;
        LONG64 y = u * u * u * p0.y + 3 * u * u * t * c[0].y + 3 * u * t * t * c[1].y + t * t * t * c[2].y;
        LONG nx = (LONG)((x + d / 2) / d), ny = (LONG)((y + d / 2) / d);
        DrawLine(dc, clip, px, py, nx, ny, dirty);
        px = nx;
        py = ny;
    }
}

static void to_surface(DC *dc, POINT *pts, ULONG n)
{
    for (ULONG i = 0; i < n; i++)
        DcLogicalToSurface(dc, &pts[i].x, &pts[i].y);
}

static void record_path(DC *dc, POINT *pts, const ULONG *counts, ULONG count, int function)
{
    ULONG base = 0;
    for (ULONG i = 0; i < count; i++) {
        ULONG n = counts[i];
        POINT *p = pts + base;
        switch (function) {
        case MIME_POLY_POLYGON:
            PathPolygon(dc, p, n, TRUE);
            break;
        case MIME_POLY_POLYLINE:
            PathPolygon(dc, p, n, FALSE);
            break;
        case MIME_POLY_BEZIER:
            if (n) {
                PathPolygon(dc, p, 1, FALSE);
                PathBezierTo(dc, p + 1, (n - 1) / 3 * 3);
            }
            break;
        case MIME_POLY_LINETO:
            for (ULONG k = 0; k < n; k++)
                PathLineTo(dc, p[k].x, p[k].y);
            if (n)
                dc->State.CurrentPos = p[n - 1];
            break;
        case MIME_POLY_BEZIERTO:
            PathBezierTo(dc, p, n / 3 * 3);
            if (n >= 3)
                dc->State.CurrentPos = p[n / 3 * 3 - 1];
            break;
        }
        base += n;
    }
}

/* polygons are filled with the brush and outlined with the pen; the other functions draw lines and curves. */
/* draws figures given in kernel memory, as NtGdiPolyPolyDraw does: pts is changed. */
static ULONG poly_draw(HDC hdc, POINT *pts, const ULONG *counts, ULONG count, int function)
{
    ULONG total = 0;
    for (ULONG i = 0; i < count; i++)
        total += counts[i];
    UserEnterRead();
    DC *pdc = path_dc(hdc);
    if (pdc) {
        record_path(pdc, pts, counts, count, function);
        UserLeave();
        return 1;
    }
    DRAW_CONTEXT ctx;
    RECT dirty = {0, 0, 0, 0};
    BOOL ok = FALSE;
    POINT cur = {0, 0};
    BOOLEAN have_dc = begin_draw(hdc, &ctx);
    if (ctx.Dc) {
        cur = ctx.Dc->State.CurrentPos;
        if (function == MIME_POLY_LINETO && total)
            ctx.Dc->State.CurrentPos = pts[total - 1];
        else if (function == MIME_POLY_BEZIERTO && total >= 3)
            ctx.Dc->State.CurrentPos = pts[total / 3 * 3 - 1];
        ok = TRUE;
    }
    if (have_dc) {
        DC *dc = ctx.Dc;
        if (function == MIME_POLY_POLYGON && dc->State.Brush->Style != BS_NULL) {
            POINT *spts = W32Alloc(total * sizeof(POINT) + 1);
            if (spts) {
                memcpy(spts, pts, total * sizeof(POINT));
                to_surface(dc, spts, total);
                REGION area, c;
                RgnInit(&area);
                RgnInit(&c);
                if (RgnFromPolygons(&area, spts, counts, count,
                                    dc->State.PolyFillMode == WINDING ? WINDING : ALTERNATE)) {
                    RgnCombine(&c, &ctx.Clip, &area, RGN_AND);
                    if (!RgnIsEmpty(&c)) {
                        DrawBrushRect(dc, &c, &c.Extents, dc->State.Brush, PATCOPY);
                        RectUnion(&dirty, &dirty, &c.Extents);
                    }
                }
                RgnFree(&area);
                RgnFree(&c);
                W32Free(spts);
            }
        }
        DcLogicalToSurface(dc, &cur.x, &cur.y);
        to_surface(dc, pts, total);
        ULONG base = 0;
        for (ULONG i = 0; i < count; i++) {
            ULONG n = counts[i];
            POINT *p = pts + base;
            switch (function) {
            case MIME_POLY_POLYGON:
            case MIME_POLY_POLYLINE:
                for (ULONG k = 0; k + 1 < n; k++)
                    DrawLine(dc, &ctx.Clip, p[k].x, p[k].y, p[k + 1].x, p[k + 1].y, &dirty);
                if (function == MIME_POLY_POLYGON && n > 2)
                    DrawLine(dc, &ctx.Clip, p[n - 1].x, p[n - 1].y, p[0].x, p[0].y, &dirty);
                break;
            case MIME_POLY_LINETO:
                for (ULONG k = 0; k < n; k++) {
                    DrawLine(dc, &ctx.Clip, cur.x, cur.y, p[k].x, p[k].y, &dirty);
                    cur = p[k];
                }
                break;
            case MIME_POLY_BEZIER:
                for (ULONG k = 1; k + 2 < n; k += 3)
                    draw_bezier(dc, &ctx.Clip, p[k - 1], p + k, &dirty);
                break;
            case MIME_POLY_BEZIERTO:
                for (ULONG k = 0; k + 2 < n; k += 3) {
                    draw_bezier(dc, &ctx.Clip, cur, p + k, &dirty);
                    cur = p[k + 2];
                }
                break;
            }
            base += n;
        }
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

/*
 * as win32kfull: up to 0x9C4000 figures and 0x4E2000 points, beziers in one figure only. function 6 is
 * CreatePolyPolygonRgn, where the first argument is the fill mode and the result a new region.
 */
ULONG_PTR NTAPI NtGdiPolyPolyDraw(HDC hdc, POINT *user_points, ULONG *user_counts, ULONG count, int function)
{
    if (count == 0 || count > 0x9C4000 || function < MIME_POLY_POLYGON || function > MIME_POLY_REGION)
        return 0;
    if (function >= MIME_POLY_BEZIER && function <= MIME_POLY_BEZIERTO && count != 1)
        return 0;
    ULONG *counts = W32Alloc((SIZE_T)count * sizeof(ULONG));
    if (!counts || !NT_SUCCESS(MimeCopyFromUser(counts, user_counts, (SIZE_T)count * sizeof(ULONG)))) {
        W32Free(counts);
        return 0;
    }
    ULONG total = 0;
    BOOLEAN ok = TRUE;
    for (ULONG i = 0; i < count && ok; i++) {
        total += counts[i];
        ok = counts[i] <= 0x4E2000 && total <= 0x4E2000;
    }
    if ((function == MIME_POLY_BEZIER && (counts[0] < 1 || (counts[0] - 1) % 3)) ||
        (function == MIME_POLY_BEZIERTO && counts[0] % 3) || !total)
        ok = FALSE;
    POINT *pts = ok ? W32Alloc((SIZE_T)total * sizeof(POINT)) : nullptr;
    ULONG_PTR result = 0;
    if (pts && NT_SUCCESS(MimeCopyFromUser(pts, user_points, (SIZE_T)total * sizeof(POINT)))) {
        if (function == MIME_POLY_REGION) {
            UserEnterRead();
            GDI_REGION *r = GdiCreateRegion();
            if (r && !RgnFromPolygons(&r->Region, pts, counts, count, (int)(ULONG_PTR)hdc == WINDING ? WINDING : ALTERNATE)) {
                GdiDeleteObject(&r->Header);
                r = nullptr;
            }
            result = r ? (ULONG_PTR)r->Header.Handle : 0;
            UserLeave();
        } else {
            result = poly_draw(hdc, pts, counts, count, function);
        }
    }
    W32Free(pts);
    W32Free(counts);
    return result;
}

/* ------------------------------------------------------------------ arcs */

/*
 * elliptic arcs as cubic beziers of at most a quarter turn each. the box gives the ellipse, the radials through the
 * two points give the angles and the arc direction of the dc picks the way around. angles grow counterclockwise on
 * screen, with y down.
 */
typedef struct ARC {
    double Cx, Cy, Rx, Ry;
    double Start, Sweep;
} ARC;

static LONG round_d(double v)
{
    return (LONG)(v >= 0 ? v + 0.5 : v - 0.5);
}

static POINT ellipse_point(const ARC *a, double t)
{
    double s, c;
    GdiSinCos(t, &s, &c);
    return (POINT){round_d(a->Cx + a->Rx * c), round_d(a->Cy - a->Ry * s)};
}

static void arc_setup(ARC *a, int direction, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    a->Cx = (left + right) / 2.0;
    a->Cy = (top + bottom) / 2.0;
    a->Rx = (right > left ? right - left : left - right) / 2.0;
    a->Ry = (bottom > top ? bottom - top : top - bottom) / 2.0;
    double rx = a->Rx ? a->Rx : 1, ry = a->Ry ? a->Ry : 1;
    double t1 = GdiAtan2(-(y1 - a->Cy) / ry, (x1 - a->Cx) / rx);
    double t2 = GdiAtan2(-(y2 - a->Cy) / ry, (x2 - a->Cx) / rx);
    double sweep = t2 - t1;
    if (direction == AD_CLOCKWISE) {
        while (sweep >= 0)
            sweep -= 2 * GDI_PI;
    } else {
        while (sweep <= 0)
            sweep += 2 * GDI_PI;
    }
    a->Start = t1;
    a->Sweep = sweep;
}

/* the points after the start, three for each segment. */
static ULONG arc_curves(const ARC *a, POINT *out)
{
    double sweep = a->Sweep < 0 ? -a->Sweep : a->Sweep;
    int segs = (int)(sweep / (GDI_PI / 2) + 0.999);
    segs = segs < 1 ? 1 : segs > 16 ? 16 : segs;
    double step = a->Sweep / segs, s, c;
    GdiSinCos(step / 4, &s, &c);
    double k = 4.0 / 3.0 * s / c;
    ULONG n = 0;
    for (int i = 0; i < segs; i++) {
        double t0 = a->Start + step * i, t1 = t0 + step, s0, c0, s1, c1;
        GdiSinCos(t0, &s0, &c0);
        GdiSinCos(t1, &s1, &c1);
        double x0 = a->Cx + a->Rx * c0, y0 = a->Cy - a->Ry * s0;
        double x3 = a->Cx + a->Rx * c1, y3 = a->Cy - a->Ry * s1;
        out[n++] = (POINT){round_d(x0 - k * a->Rx * s0), round_d(y0 - k * a->Ry * c0)};
        out[n++] = (POINT){round_d(x3 + k * a->Rx * s1), round_d(y3 + k * a->Ry * c1)};
        out[n++] = (POINT){round_d(x3), round_d(y3)};
    }
    return n;
}

static int arc_direction(HDC hdc)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    int direction = dc ? dc->State.ArcDirection : 0;
    UserLeave();
    return direction;
}

/* gdi32full passes 0 for Arc, 1 for ArcTo, 2 for Chord and 3 for Pie, before the dc. */
BOOL NTAPI NtGdiArcInternal(ULONG type, HDC hdc, int left, int top, int right, int bottom, int x1, int y1, int x2,
                            int y2)
{
    int direction = arc_direction(hdc);
    if (!direction || type > 3)
        return FALSE;
    /* the box leaves out its right column and bottom row, as Ellipse does: measured with shp.exe. */
    if (left > right) {
        int t = left;
        left = right;
        right = t;
    }
    if (top > bottom) {
        int t = top;
        top = bottom;
        bottom = t;
    }
    ARC a;
    arc_setup(&a, direction, left, top, right - 1, bottom - 1, x1, y1, x2, y2);
    POINT pts[2 + 48];
    pts[0] = ellipse_point(&a, a.Start);
    if (type <= 1) {
        ULONG n = arc_curves(&a, pts + 1);
        if (type == 1) {
            ULONG one = 1;
            if (!poly_draw(hdc, pts, &one, 1, MIME_POLY_LINETO))
                return FALSE;
            return (BOOL)poly_draw(hdc, pts + 1, &n, 1, MIME_POLY_BEZIERTO);
        }
        n++;
        return (BOOL)poly_draw(hdc, pts, &n, 1, MIME_POLY_BEZIER);
    }
    /* chords and pies are one closed polygon, filled and outlined. */
    ULONG n = 49;
    for (ULONG i = 0; i < n; i++)
        pts[i] = ellipse_point(&a, a.Start + a.Sweep * i / (n - 1));
    if (type == 3)
        pts[n++] = (POINT){round_d(a.Cx), round_d(a.Cy)};
    return (BOOL)poly_draw(hdc, pts, &n, 1, MIME_POLY_POLYGON);
}

/* a line to the start of a circular arc, then the arc. the angles come as the bits of floats. */
BOOL NTAPI NtGdiAngleArc(HDC hdc, int x, int y, DWORD radius, DWORD start_bits, DWORD sweep_bits)
{
    if ((LONG)radius < 0) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    float start, sweep;
    memcpy(&start, &start_bits, sizeof(start));
    memcpy(&sweep, &sweep_bits, sizeof(sweep));
    ARC a = {x, y, radius, radius, start * GDI_PI / 180, sweep * GDI_PI / 180};
    if (a.Sweep > 2 * GDI_PI)
        a.Sweep = 2 * GDI_PI;
    if (a.Sweep < -2 * GDI_PI)
        a.Sweep = -2 * GDI_PI;
    POINT pts[1 + 48];
    pts[0] = ellipse_point(&a, a.Start);
    ULONG one = 1;
    if (!poly_draw(hdc, pts, &one, 1, MIME_POLY_LINETO))
        return FALSE;
    if (!a.Sweep)
        return TRUE;
    ULONG n = arc_curves(&a, pts + 1);
    return (BOOL)poly_draw(hdc, pts + 1, &n, 1, MIME_POLY_BEZIERTO);
}

/*
 * PT_MOVETO, PT_LINETO and PT_BEZIERTO points with PT_CLOSEFIGURE on the last two. the types are checked before
 * anything is drawn, as on windows.
 */
BOOL NTAPI NtGdiPolyDraw(HDC hdc, POINT *user_points, BYTE *user_types, ULONG count)
{
    if (!count || count > 0x4E2000)
        return FALSE;
    POINT *pts = W32Alloc((SIZE_T)count * sizeof(POINT));
    BYTE *types = W32Alloc(count);
    BOOL ok = pts && types && NT_SUCCESS(MimeCopyFromUser(pts, user_points, (SIZE_T)count * sizeof(POINT))) &&
              NT_SUCCESS(MimeCopyFromUser(types, user_types, count));
    for (ULONG i = 0; ok && i < count; i++) {
        BYTE t = types[i] & ~PT_CLOSEFIGURE;
        if (t == PT_BEZIERTO) {
            ok = i + 2 < count && (types[i + 1] & ~PT_CLOSEFIGURE) == PT_BEZIERTO &&
                 (types[i + 2] & ~PT_CLOSEFIGURE) == PT_BEZIERTO;
            i += 2;
        } else {
            ok = t == PT_MOVETO || t == PT_LINETO;
        }
    }
    UserEnterRead();
    DC *dc = ok ? DcLookup(hdc) : nullptr;
    POINT start = dc ? dc->State.CurrentPos : (POINT){0, 0};
    BOOLEAN path = dc && DcIsPathOpen(dc);
    UserLeave();
    ok = ok && dc;
    for (ULONG i = 0; ok && i < count; i++) {
        BYTE t = types[i] & ~PT_CLOSEFIGURE;
        ULONG n = t == PT_BEZIERTO ? 3 : 1;
        if (t == PT_MOVETO) {
            NtGdiMoveTo(hdc, pts[i].x, pts[i].y, nullptr);
            start = pts[i];
            continue;
        }
        POINT seg[3];
        memcpy(seg, pts + i, n * sizeof(POINT));
        ok = (BOOL)poly_draw(hdc, seg, &n, 1, t == PT_BEZIERTO ? MIME_POLY_BEZIERTO : MIME_POLY_LINETO);
        i += n - 1;
        if (ok && (types[i] & PT_CLOSEFIGURE)) {
            if (path) {
                NtGdiCloseFigure(hdc);
            } else {
                POINT back = start;
                ULONG one = 1;
                ok = (BOOL)poly_draw(hdc, &back, &one, 1, MIME_POLY_LINETO);
            }
        }
    }
    W32Free(pts);
    W32Free(types);
    return ok;
}

COLORREF NTAPI NtGdiSetPixel(HDC hdc, int x, int y, COLORREF color)
{
    UserEnterRead();
    DRAW_CONTEXT ctx;
    COLORREF result = CLR_INVALID;
    RECT dirty = {0, 0, 0, 0};
    if (begin_draw(hdc, &ctx)) {
        LONG sx = x, sy = y;
        DcLogicalToSurface(ctx.Dc, &sx, &sy);
        if (RgnPtIn(&ctx.Clip, sx, sy)) {
            SurfacePutPixel(ctx.Dc->Surface, sx, sy, DcColorPixel(ctx.Dc, color));
            result = PixelToColor(SurfaceGetPixel(ctx.Dc->Surface, sx, sy));
            dirty = (RECT){sx, sy, sx + 1, sy + 1};
        }
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return result;
}

COLORREF NTAPI NtGdiGetPixel(HDC hdc, int x, int y)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    COLORREF result = CLR_INVALID;
    if (dc && dc->Surface) {
        LONG sx = x, sy = y;
        DcLogicalToSurface(dc, &sx, &sy);
        if (RgnPtIn(&dc->VisRgn, sx, sy) && sx < dc->Surface->Width && sy < dc->Surface->Height) {
            if (ScreenIsSurface(dc->Surface))
                ScreenComposeFor(dc->Surface, &(RECT){sx, sy, sx + 1, sy + 1});
            ULONG pixel;
            if (!ScreenUnpaintedPixel(dc->Surface, sx, sy, &pixel))
                pixel = SurfaceGetPixel(dc->Surface, sx, sy);
            result = PixelToColor(pixel);
        }
    }
    UserLeave();
    return result;
}

static BOOL fill_region(HDC hdc, HRGN region, HBRUSH hbrush, int frame_w, int frame_h, BOOLEAN invert)
{
    UserEnterRead();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    GDI_REGION *r = (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION);
    BRUSH *b = hbrush ? (BRUSH *)GdiLookup(hbrush, GDI_TYPE_BRUSH) : nullptr;
    if (begin_draw(hdc, &ctx) && r && (b || invert)) {
        REGION area;
        RgnInit(&area);
        RgnCopy(&area, &r->Region);
        if (frame_w || frame_h) {
            /* the frame is the region minus the region eroded by the frame size. */
            REGION inner, shifted;
            RgnInit(&inner);
            RgnInit(&shifted);
            RgnCopy(&inner, &r->Region);
            LONG offs[4][2] = {{frame_w, 0}, {-frame_w, 0}, {0, frame_h}, {0, -frame_h}};
            for (int k = 0; k < 4; k++) {
                RgnCopy(&shifted, &r->Region);
                RgnOffset(&shifted, offs[k][0], offs[k][1]);
                RgnCombine(&inner, &inner, &shifted, RGN_AND);
            }
            RgnCombine(&area, &area, &inner, RGN_DIFF);
            RgnFree(&inner);
            RgnFree(&shifted);
        }
        if (DcIsScaled(ctx.Dc)) {
            /* the region is logical: its rectangles go through the transform. */
            XFORM m;
            DcGetXform(ctx.Dc, &m);
            m.eDx += (float)ctx.Dc->Origin.x;
            m.eDy += (float)ctx.Dc->Origin.y;
            REGION copy;
            RgnInit(&copy);
            RgnCopy(&copy, &area);
            RgnTransform(&area, copy.Rects, copy.Count, &m);
            RgnFree(&copy);
        } else {
            LONG ox = 0, oy = 0;
            DcLogicalToSurface(ctx.Dc, &ox, &oy);
            RgnOffset(&area, ox, oy);
        }
        RgnCombine(&ctx.Clip, &ctx.Clip, &area, RGN_AND);
        if (invert)
            DrawFillRectRop(ctx.Dc->Surface, &ctx.Clip, &area.Extents, 0, R2_NOT);
        else
            DrawBrushRect(ctx.Dc, &ctx.Clip, &area.Extents, b, PATCOPY);
        dirty = area.Extents;
        RgnFree(&area);
        ok = TRUE;
    }
    end_draw(&ctx, &dirty);
    UserLeave();
    return ok;
}

BOOL NTAPI NtGdiFillRgn(HDC hdc, HRGN region, HBRUSH brush)
{
    return fill_region(hdc, region, brush, 0, 0, FALSE);
}

BOOL NTAPI NtGdiFrameRgn(HDC hdc, HRGN region, HBRUSH brush, int width, int height)
{
    return fill_region(hdc, region, brush, width, height, FALSE);
}

BOOL NTAPI NtGdiInvertRgn(HDC hdc, HRGN region)
{
    return fill_region(hdc, region, nullptr, 0, 0, TRUE);
}
