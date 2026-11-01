/*
 * drawing primitives on surfaces and the gdi drawing services.
 */
#include "../win32k.h"

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
    if (s->Flags & SURF_DEVICE)
        pixel |= 0xFF000000u;
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
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++)
            for (LONG x = r.left; x < r.right; x++)
                SurfacePutPixel(s, x, y, rop2_apply(rop2, pixel, SurfaceGetPixel(s, x, y)) & 0xFFFFFF);
    }
}

static ULONG brush_pixel(DC *dc, BRUSH *b, LONG x, LONG y)
{
    if (b->Header.Handle == GdiStockHandle(DC_BRUSH))
        return ColorToPixel(dc->State.BrushColor);
    if (b->SysColor >= 0)
        return ColorToPixel(MetricsSysColor(b->SysColor));
    if (b->Style == BS_PATTERN && b->Pattern) {
        SURFACE *p = b->Pattern;
        LONG px = ((x % p->Width) + p->Width) % p->Width, py = ((y % p->Height) + p->Height) % p->Height;
        ULONG v = SurfaceGetPixel(p, px, py);
        if (p->Bpp == 1)
            return ColorToPixel((v & 0xFFFFFF) ? dc->State.BkColor : dc->State.TextColor);
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
        return ColorToPixel(on ? b->Color : dc->State.BkColor);
    }
    return ColorToPixel(b->Color);
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
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++) {
            for (LONG x = r.left; x < r.right; x++) {
                ULONG p = solid ? solid_pixel : brush_pixel(dc, brush, x - dc->Origin.x, y - dc->Origin.y);
                ULONG d = needs_dst ? SurfaceGetPixel(s, x, y) : 0;
                SurfacePutPixel(s, x, y, rop3(code, p, 0, d) & 0xFFFFFF);
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
                if (dst->Flags & SURF_DEVICE)
                    for (SIZE_T j = 0; j < n; j++)
                        d[j] |= 0xFF000000u;
                continue;
            }
            for (LONG x = r.left; x < r.right; x++) {
                ULONG s = 0;
                if (src) {
                    s = SurfaceGetPixel(src, sx + (x - dx), syy);
                    if (src->Bpp == 1 && dst->Bpp != 1 && dst_dc) {
                        /* monochrome sources map 0 to the text color and 1 to the background color. */
                        s = ColorToPixel((s & 0xFFFFFF) ? dst_dc->State.BkColor : dst_dc->State.TextColor);
                    } else if (dst->Bpp == 1 && src->Bpp != 1 && dst_dc) {
                        s = ((s & 0xFFFFFF) == ColorToPixel(dst_dc->State.BkColor)) ? 0xFFFFFF : 0;
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
                ULONG ob = sb + div255((d & 0xFF) * inv);
                ULONG og = sg + div255(((d >> 8) & 0xFF) * inv);
                ULONG orr = sr + div255(((d >> 16) & 0xFF) * inv);
                ULONG oa = sa + div255((d >> 24) * inv);
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
 * tests whether a point is inside the ellipse with semi axes a and b around the origin, all in the same unit. for
 * large axes the squares are scaled down so the products fit in 64 bits.
 */
typedef struct ELLIPSE_TEST {
    ULONG64 A2, B2, Limit;
    UINT Shift;
} ELLIPSE_TEST;

static void ellipse_test_init(ELLIPSE_TEST *e, LONG a, LONG b)
{
    ULONG64 big = (ULONG64)(a > b ? a : b);
    UINT shift = 0;
    /* with the axes under 40000 the sum of two such products fits in 64 bits. */
    while ((big >> shift) > 40000)
        shift++;
    e->Shift = shift;
    ULONG64 sa = (ULONG64)a >> shift, sb = (ULONG64)b >> shift;
    e->A2 = sa * sa;
    e->B2 = sb * sb;
    e->Limit = e->A2 * e->B2;
}

/* grows with the distance from the center, compared against Limit. */
static ULONG64 ellipse_value(const ELLIPSE_TEST *e, LONG px, LONG py)
{
    ULONG64 x = (ULONG64)(px < 0 ? -px : px) >> e->Shift, y = (ULONG64)(py < 0 ? -py : py) >> e->Shift;
    return x * x * e->B2 + y * y * e->A2;
}

/*
 * anti-aliased rectangle whose corners are quarter ellipses with semi axes rx and ry, so an ellipse is its box with
 * the largest corners. an optional border of border_width pixels sits inside the rectangle, plus a fill. coverage
 * uses 4x4 supersampling, only on pixels the edge crosses.
 */
void DrawRoundRectAA(SURFACE *s, const REGION *clip, const RECT *rect, LONG rx, LONG ry, BOOLEAN filled, ULONG fill,
                     ULONG border, LONG border_width)
{
    LONG w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (w <= 0 || h <= 0)
        return;
    rx = W32Min(rx, w / 2);
    ry = W32Min(ry, h / 2);
    BOOLEAN corners = rx > 0 && ry > 0;
    ELLIPSE_TEST outer_test, inner_test;
    LONG rin_x = rx - border_width, rin_y = ry - border_width;
    BOOLEAN has_inner = rin_x > 0 && rin_y > 0;
    if (corners) {
        ellipse_test_init(&outer_test, rx * 8, ry * 8);
        if (has_inner)
            ellipse_test_init(&inner_test, rin_x * 8, rin_y * 8);
    }
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT r;
        if (!RectIntersect(&r, &clip->Rects[i], rect))
            continue;
        for (LONG y = r.top; y < r.bottom; y++) {
            for (LONG x = r.left; x < r.right; x++) {
                LONG lx = x - rect->left, ly = y - rect->top;
                /* the center of the corner the pixel is in, if it is in one. */
                LONG cx = lx < rx ? rx : lx >= w - rx ? w - rx : -1;
                LONG cy = ly < ry ? ry : ly >= h - ry ? h - ry : -1;
                ULONG outer = 16, inner = 16;
                if (cx >= 0 && cy >= 0 && corners) {
                    /* the pixel's nearest and farthest corners from the center, in 1/8 pixels. */
                    LONG near_x = (lx < cx ? cx - lx - 1 : lx - cx) * 8, far_x = near_x + 8;
                    LONG near_y = (ly < cy ? cy - ly - 1 : ly - cy) * 8, far_y = near_y + 8;
                    ULONG outer_hits = 16, inner_hits = 16;
                    if (ellipse_value(&outer_test, near_x, near_y) >= outer_test.Limit) {
                        outer_hits = 0;
                    } else if (ellipse_value(&outer_test, far_x, far_y) > outer_test.Limit) {
                        outer_hits = 0;
                        for (int sy = 0; sy < 4; sy++)
                            for (int sx = 0; sx < 4; sx++) {
                                LONG px = (lx * 8 + sx * 2 + 1) - cx * 8, py = (ly * 8 + sy * 2 + 1) - cy * 8;
                                if (ellipse_value(&outer_test, px, py) <= outer_test.Limit)
                                    outer_hits++;
                            }
                    }
                    if (!has_inner || ellipse_value(&inner_test, near_x, near_y) >= inner_test.Limit) {
                        inner_hits = 0;
                    } else if (ellipse_value(&inner_test, far_x, far_y) > inner_test.Limit) {
                        inner_hits = 0;
                        for (int sy = 0; sy < 4; sy++)
                            for (int sx = 0; sx < 4; sx++) {
                                LONG px = (lx * 8 + sx * 2 + 1) - cx * 8, py = (ly * 8 + sy * 2 + 1) - cy * 8;
                                if (ellipse_value(&inner_test, px, py) <= inner_test.Limit)
                                    inner_hits++;
                            }
                    }
                    outer = outer_hits;
                    inner = inner_hits;
                } else if (border_width > 0) {
                    BOOLEAN on_edge = lx < border_width || ly < border_width || lx >= w - border_width ||
                                      ly >= h - border_width;
                    inner = on_edge ? 0 : 16;
                }
                if (!border_width)
                    inner = outer;
                if (!outer)
                    continue;
                ULONG d = SurfaceGetPixel(s, x, y);
                /* the inner area takes the fill, the ring between takes the border. */
                ULONG fa = filled ? inner * 255 / 16 : 0, ba = (outer - inner) * 255 / 16;
                ULONG out_px = d;
                if (ba) {
                    ULONG inv = 255 - ba;
                    out_px = (div255(((border >> 16) & 0xFF) * ba + ((out_px >> 16) & 0xFF) * inv) << 16) |
                             (div255(((border >> 8) & 0xFF) * ba + ((out_px >> 8) & 0xFF) * inv) << 8) |
                             div255((border & 0xFF) * ba + (out_px & 0xFF) * inv);
                }
                if (fa) {
                    ULONG inv = 255 - fa;
                    out_px = (div255(((fill >> 16) & 0xFF) * fa + ((out_px >> 16) & 0xFF) * inv) << 16) |
                             (div255(((fill >> 8) & 0xFF) * fa + ((out_px >> 8) & 0xFF) * inv) << 8) |
                             div255((fill & 0xFF) * fa + (out_px & 0xFF) * inv);
                }
                SurfacePutPixel(s, x, y, out_px);
            }
        }
    }
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
        SurfaceMarkDirty(ctx->Dc->Surface, touched);
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

static ULONG pen_pixel(DC *dc)
{
    PEN *p = dc->State.Pen;
    if (p->Header.Handle == GdiStockHandle(DC_PEN))
        return ColorToPixel(dc->State.PenColor);
    return ColorToPixel(p->Color);
}

/* ------------------------------------------------------------------ services */

BOOL NTAPI NtGdiPatBlt(HDC hdc, int x, int y, int width, int height, DWORD rop)
{
    UserEnter();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT r = {0, 0, 0, 0};
    if (begin_draw(hdc, &ctx)) {
        r = logical_rect_to_surface(ctx.Dc, x, y, x + width, y + height);
        DrawBrushRect(ctx.Dc, &ctx.Clip, &r, ctx.Dc->State.Brush, rop);
        ok = TRUE;
    } else if (ctx.Dc) {
        ok = TRUE;
    }
    end_draw(&ctx, &r);
    UserLeave();
    return ok;
}

static BOOL stretch_blt(HDC hdst, int x, int y, int width, int height, HDC hsrc, int src_x, int src_y,
                        int src_width, int src_height, DWORD rop);

/*
 * the screen of a program that is not dpi aware has 96 dpi pixels, and what it reads of it comes from the physical
 * screen by taking one pixel in the scale of them.
 */
static BOOLEAN screen_is_scaled(const DC *dc)
{
    return ScreenIsSurface(dc->Surface) && DpiVirtual(dc->Awareness);
}

/* the pixel of the physical screen under a pixel of the dc's screen, the dc's device coordinates without its origin. */
static void physical_screen_point(const DC *dc, LONG *x, LONG *y)
{
    *x = DpiScale(*x, 96, W32SystemDpi()) + dc->Origin.x;
    *y = DpiScale(*y, 96, W32SystemDpi()) + dc->Origin.y;
}

/* a copy of the part of the screen that starts at x, y of the dc's device. */
static SURFACE *scaled_screen(DC *dc, LONG x, LONG y, LONG width, LONG height)
{
    if (width <= 0 || height <= 0)
        return nullptr;
    SURFACE *copy = SurfaceCreate(width, height, 32, SURF_TOPDOWN);
    if (!copy)
        return nullptr;
    LONG left = x, top = y, right = x + width, bottom = y + height;
    physical_screen_point(dc, &left, &top);
    physical_screen_point(dc, &right, &bottom);
    ScreenCompose(&(RECT){left, top, right + 1, bottom + 1});
    const SURFACE *screen = dc->Surface;
    for (LONG j = 0; j < height; j++) {
        LONG sy = DpiScale(y + j, 96, W32SystemDpi()) + dc->Origin.y;
        if (sy < 0 || sy >= screen->Height)
            continue;
        const ULONG *from = (const ULONG *)(screen->Bits + (LONG_PTR)sy * screen->Stride);
        ULONG *to = (ULONG *)(copy->Bits + (LONG_PTR)j * copy->Stride);
        for (LONG i = 0; i < width; i++) {
            LONG px = DpiScale(x + i, 96, W32SystemDpi()) + dc->Origin.x;
            to[i] = px >= 0 && px < screen->Width ? from[px] : 0xFF000000;
        }
    }
    return copy;
}

/* a scaling transform on either dc stretches. */
static BOOLEAN blt_scales(HDC hdst, HDC hsrc)
{
    UserEnter();
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
    UserEnter();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT r = {0, 0, 0, 0};
    DC *src = hsrc ? DcLookup(hsrc) : nullptr;
    if (begin_draw(hdst, &ctx)) {
        if (!uses_src) {
            r = logical_rect_to_surface(ctx.Dc, x, y, x + width, y + height);
            DrawBrushRect(ctx.Dc, &ctx.Clip, &r, ctx.Dc->State.Brush, rop);
            ok = TRUE;
        } else if (src && src->Surface && screen_is_scaled(src)) {
            LONG dx = x, dy = y, sx = src_x, sy = src_y;
            DcLogicalToSurface(ctx.Dc, &dx, &dy);
            DcLogicalToDevice(src, &sx, &sy);
            SURFACE *copy = scaled_screen(src, sx, sy, width, height);
            if (copy) {
                ok = BltSurface(ctx.Dc->Surface, &ctx.Clip, dx, dy, width, height, copy, 0, 0, rop, ctx.Dc);
                SurfaceDestroy(copy);
            }
            r = (RECT){dx, dy, dx + width, dy + height};
        } else if (src && src->Surface) {
            LONG dx = x, dy = y, sx = src_x, sy = src_y;
            DcLogicalToSurface(ctx.Dc, &dx, &dy);
            DcLogicalToSurface(src, &sx, &sy);
            if (ScreenIsSurface(src->Surface))
                ScreenCompose(&(RECT){sx, sy, sx + width, sy + height});
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
    UserEnter();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    DC *src = DcLookup(hsrc);
    if (begin_draw(hdst, &ctx) && src && src->Surface) {
        LONG x0 = x, y0 = y, x1 = x + width, y1 = y + height;
        DcLogicalToSurface(ctx.Dc, &x0, &y0);
        DcLogicalToSurface(ctx.Dc, &x1, &y1);
        LONG sx0 = src_x, sy0 = src_y, sx1 = src_x + src_width, sy1 = src_y + src_height;
        SURFACE *copy = nullptr;
        if (screen_is_scaled(src)) {
            DcLogicalToDevice(src, &sx0, &sy0);
            DcLogicalToDevice(src, &sx1, &sy1);
            LONG left = W32Min(sx0, sx1), top = W32Min(sy0, sy1);
            copy = scaled_screen(src, left, top, W32Max(sx0, sx1) - left, W32Max(sy0, sy1) - top);
            sx0 -= left, sx1 -= left, sy0 -= top, sy1 -= top;
        } else {
            DcLogicalToSurface(src, &sx0, &sy0);
            DcLogicalToSurface(src, &sx1, &sy1);
        }
        RECT dr = {x0, y0, x1, y1}, sr = {sx0, sy0, sx1, sy1};
        if (copy) {
            ok = StretchSurface(ctx.Dc->Surface, &ctx.Clip, &dr, copy, &sr, rop, ctx.Dc->State.StretchMode);
            SurfaceDestroy(copy);
        } else {
            if (ScreenIsSurface(src->Surface))
                ScreenCompose(&(RECT){W32Min(sx0, sx1), W32Min(sy0, sy1), W32Max(sx0, sx1), W32Max(sy0, sy1)});
            ok = StretchSurface(ctx.Dc->Surface, &ctx.Clip, &dr, src->Surface, &sr, rop, ctx.Dc->State.StretchMode);
        }
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
    UserEnter();
    DRAW_CONTEXT ctx;
    BOOL ok = FALSE;
    RECT dirty = {0, 0, 0, 0};
    DC *src = DcLookup(hsrc);
    if (begin_draw(hdst, &ctx) && src && src->Surface) {
        RECT dr = logical_rect_to_surface(ctx.Dc, x, y, x + width, y + height);
        RECT sr = logical_rect_to_surface(src, src_x, src_y, src_x + src_width, src_y + src_height);
        if (ScreenIsSurface(src->Surface))
            ScreenCompose(&sr);
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
    UserEnter();
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

static void frame_rect_pen(DC *dc, const REGION *clip, const RECT *r, RECT *dirty)
{
    PEN *pen = dc->State.Pen;
    if (PenStyle(pen) == PS_NULL)
        return;
    LONG w = DcPenWidth(dc);
    ULONG px = pen_pixel(dc);
    RECT edges[4] = {
        {r->left, r->top, r->right, r->top + w},
        {r->left, r->bottom - w, r->right, r->bottom},
        {r->left, r->top, r->left + w, r->bottom},
        {r->right - w, r->top, r->right, r->bottom},
    };
    for (int i = 0; i < 4; i++)
        DrawFillRectRop(dc->Surface, clip, &edges[i], px, dc->State.Rop2);
    RectUnion(dirty, dirty, r);
}

BOOL NTAPI NtGdiRectangle(HDC hdc, int left, int top, int right, int bottom)
{
    UserEnter();
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
        LONG inset = PenStyle(ctx.Dc->State.Pen) == PS_NULL ? 0 : 1;
        /* with a null pen the rectangle shrinks by one pixel at the right and bottom. */
        RECT inner = r;
        if (!inset) {
            inner.right--;
            inner.bottom--;
        }
        DrawBrushRect(ctx.Dc, &ctx.Clip, &inner, ctx.Dc->State.Brush, PATCOPY);
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

static BOOL draw_round_rect(HDC hdc, int left, int top, int right, int bottom, int width, int height, BOOLEAN ellipse)
{
    DRAW_CONTEXT ctx;
    RECT dirty = {0, 0, 0, 0};
    BOOL ok = FALSE;
    UserEnter();
    if (begin_draw(hdc, &ctx)) {
        RECT r = shape_rect(ctx.Dc, left, top, right, bottom);
        BRUSH *b = ctx.Dc->State.Brush;
        ULONG fill = b->Style == BS_NULL ? 0 : brush_pixel(ctx.Dc, b, 0, 0);
        PEN *p = ctx.Dc->State.Pen;
        LONG bw = PenStyle(p) == PS_NULL ? 0 : DcPenWidth(ctx.Dc);
        LONG rx, ry;
        if (ellipse) {
            rx = (r.right - r.left) / 2;
            ry = (r.bottom - r.top) / 2;
        } else {
            rx = DcScaleToDevice(ctx.Dc, width < 0 ? -width : width) / 2;
            ry = DcScaleToDevice(ctx.Dc, height < 0 ? -height : height) / 2;
        }
        if (b->Style == BS_NULL) {
            /* frame only: draw the border ring over the existing pixels. */
            REGION ring;
            RgnInit(&ring);
            RgnCopy(&ring, &ctx.Clip);
            DrawRoundRectAA(ctx.Dc->Surface, &ring, &r, rx, ry, FALSE, 0, pen_pixel(ctx.Dc), bw);
            RgnFree(&ring);
        } else {
            DrawRoundRectAA(ctx.Dc->Surface, &ctx.Clip, &r, rx, ry, TRUE, fill, bw ? pen_pixel(ctx.Dc) : fill, bw);
        }
        dirty = r;
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
    UserEnter();
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
    UserEnter();
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
ULONG NTAPI NtGdiPolyPolyDraw(HDC hdc, POINT *user_points, ULONG *user_counts, ULONG count, int function)
{
    if (count == 0 || count > 1024 || function < MIME_POLY_POLYGON || function > MIME_POLY_BEZIERTO)
        return 0;
    ULONG counts[1024];
    if (!NT_SUCCESS(MimeCopyFromUser(counts, user_counts, count * sizeof(ULONG))))
        return 0;
    ULONG total = 0;
    for (ULONG i = 0; i < count; i++) {
        total += counts[i];
        if (total > 65536)
            return 0;
    }
    if ((function == MIME_POLY_BEZIER && (counts[0] < 1 || (counts[0] - 1) % 3)) ||
        (function == MIME_POLY_BEZIERTO && counts[0] % 3))
        return 0;
    POINT *pts = W32Alloc(total * sizeof(POINT) + 1);
    if (!pts || !NT_SUCCESS(MimeCopyFromUser(pts, user_points, total * sizeof(POINT)))) {
        W32Free(pts);
        return 0;
    }
    UserEnter();
    DC *pdc = path_dc(hdc);
    if (pdc) {
        record_path(pdc, pts, counts, count, function);
        UserLeave();
        W32Free(pts);
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
    W32Free(pts);
    return ok;
}

COLORREF NTAPI NtGdiSetPixel(HDC hdc, int x, int y, COLORREF color)
{
    UserEnter();
    DRAW_CONTEXT ctx;
    COLORREF result = CLR_INVALID;
    RECT dirty = {0, 0, 0, 0};
    if (begin_draw(hdc, &ctx)) {
        LONG sx = x, sy = y;
        DcLogicalToSurface(ctx.Dc, &sx, &sy);
        if (RgnPtIn(&ctx.Clip, sx, sy)) {
            SurfacePutPixel(ctx.Dc->Surface, sx, sy, ColorToPixel(color));
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
    UserEnter();
    DC *dc = DcLookup(hdc);
    COLORREF result = CLR_INVALID;
    if (dc && dc->Surface && screen_is_scaled(dc)) {
        LONG sx = x, sy = y;
        DcLogicalToDevice(dc, &sx, &sy);
        physical_screen_point(dc, &sx, &sy);
        if (RgnPtIn(&dc->VisRgn, sx, sy) && sx < dc->Surface->Width && sy < dc->Surface->Height) {
            ScreenCompose(&(RECT){sx, sy, sx + 1, sy + 1});
            result = PixelToColor(SurfaceGetPixel(dc->Surface, sx, sy));
        }
    } else if (dc && dc->Surface) {
        LONG sx = x, sy = y;
        DcLogicalToSurface(dc, &sx, &sy);
        if (RgnPtIn(&dc->VisRgn, sx, sy) && sx < dc->Surface->Width && sy < dc->Surface->Height) {
            if (ScreenIsSurface(dc->Surface))
                ScreenCompose(&(RECT){sx, sy, sx + 1, sy + 1});
            result = PixelToColor(SurfaceGetPixel(dc->Surface, sx, sy));
        }
    }
    UserLeave();
    return result;
}

static BOOL fill_region(HDC hdc, HRGN region, HBRUSH hbrush, int frame_w, int frame_h, BOOLEAN invert)
{
    UserEnter();
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
