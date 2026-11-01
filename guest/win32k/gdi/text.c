/* text output and measurement services. */
#include "../win32k.h"

/* combines surrogate pairs. */
static ULONG next_char(PCWSTR text, ULONG count, ULONG *i)
{
    WCHAR c = text[(*i)++];
    if (c >= 0xD800 && c < 0xDC00 && *i < count && text[*i] >= 0xDC00 && text[*i] < 0xE000)
        return 0x10000 + (((ULONG)c - 0xD800) << 10) + (text[(*i)++] - 0xDC00);
    return c;
}

/*
 * the world transform and the page scale without its flips, so text stays upright in mapping modes where y goes
 * up, like windows.
 */
static void glyph_matrix(DC *dc, XFORM *g)
{
    XFORM page = {1, 0, 0, 1, 0, 0};
    if (dc->State.HasPage) {
        LONG wx = dc->State.WindowExt.cx, wy = dc->State.WindowExt.cy;
        LONG vx = dc->State.ViewportExt.cx, vy = dc->State.ViewportExt.cy;
        page.eM11 = wx ? (float)vx / (float)wx : 1;
        page.eM22 = wy ? (float)vy / (float)wy : 1;
        if (page.eM11 < 0)
            page.eM11 = -page.eM11;
        if (page.eM22 < 0)
            page.eM22 = -page.eM22;
    }
    const XFORM *w = &dc->State.World;
    *g = (XFORM){w->eM11 * page.eM11, w->eM12 * page.eM22, w->eM21 * page.eM11, w->eM22 * page.eM22, 0, 0};
}

static LONG round_f(float v)
{
    return (LONG)(v >= 0 ? v + 0.5f : v - 0.5f);
}

/* offsets a surface origin by a text space vector. */
static void text_point(const XFORM *g, LONG ox, LONG oy, LONG dx, LONG dy, LONG *x, LONG *y)
{
    *x = ox + round_f((float)dx * g->eM11 + (float)dy * g->eM21);
    *y = oy + round_f((float)dx * g->eM12 + (float)dy * g->eM22);
}

/* the surface rectangle covering a text space rectangle. */
static RECT transformed_bounds(const XFORM *g, LONG ox, LONG oy, LONG l, LONG t, LONG r, LONG b)
{
    LONG xs[4] = {l, r, l, r}, ys[4] = {t, t, b, b};
    RECT out = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        text_point(g, ox, oy, xs[i], ys[i], &xs[i], &ys[i]);
        if (!i || xs[i] < out.left)
            out.left = xs[i];
        if (!i || xs[i] > out.right)
            out.right = xs[i];
        if (!i || ys[i] < out.top)
            out.top = ys[i];
        if (!i || ys[i] > out.bottom)
            out.bottom = ys[i];
    }
    return out;
}

/*
 * the font keeps its logical size, the reference point goes through the transform and the glyphs and their offsets
 * through the glyph matrix, so scaled and rotated text look like windows draws them.
 */
static void TextDrawTransformed(DC *dc, FONT_INSTANCE *fi, const REGION *clip, LONG x, LONG y, UINT options,
                                PCWSTR text, ULONG count, const INT *dx, LONG width)
{
    DC_STATE *st = &dc->State;
    XFORM gm;
    glyph_matrix(dc, &gm);
    LONG ox = x, oy = y;
    DcLogicalToSurface(dc, &ox, &oy);
    x = 0;
    UINT halign = st->TextAlign & (TA_LEFT | TA_RIGHT | TA_CENTER);
    if (halign == TA_RIGHT)
        x -= width;
    else if (halign == TA_CENTER)
        x -= width / 2;
    LONG ascent = fi->Metrics.tmAscent, descent = fi->Metrics.tmDescent, baseline;
    switch (st->TextAlign & (TA_TOP | TA_BOTTOM | TA_BASELINE)) {
    case TA_BASELINE:
        baseline = 0;
        break;
    case TA_BOTTOM:
        baseline = -descent;
        break;
    default:
        baseline = ascent;
        break;
    }
    RECT dirty = transformed_bounds(&gm, ox, oy, x, baseline - ascent, x + width, baseline + descent);
    if (st->BkMode == OPAQUE && !(options & ETO_OPAQUE))
        DrawFillRect(dc->Surface, clip, &dirty, ColorToPixel(st->BkColor));
    ULONG pixel = ColorToPixel(st->TextColor);
    LONG pen = x;
    ULONG i = 0, index = 0;
    while (i < count) {
        ULONG start = i;
        USHORT glyph = (options & ETO_GLYPH_INDEX) ? text[i++] : FontGlyphIndex(fi, next_char(text, count, &i));
        const GLYPH *g = FontGetGlyphIndex(fi, glyph);
        LONG gx, gy;
        text_point(&gm, ox, oy, pen, baseline, &gx, &gy);
        LONG left, top, w, h;
        UCHAR *cov = FontRenderTransformed(fi, glyph, &gm, &left, &top, &w, &h);
        if (cov) {
            DrawCoverage(dc->Surface, clip, gx + left, gy + top, cov, w, h, w, pixel);
            RECT r = {gx + left, gy + top, gx + left + w, gy + top + h};
            RectUnion(&dirty, &dirty, &r);
            W32Free(cov);
        }
        if (dx) {
            for (ULONG k = start; k < i; k++)
                pen += dx[index++];
        } else {
            pen += (g ? g->Advance : 0) + st->TextCharExtra;
        }
    }
    if (fi->Underline) {
        RECT u = transformed_bounds(&gm, ox, oy, x, baseline + fi->UnderlinePos, pen,
                                    baseline + fi->UnderlinePos + fi->UnderlineSize);
        DrawFillRect(dc->Surface, clip, &u, pixel);
    }
    if (fi->StrikeOut) {
        RECT u = transformed_bounds(&gm, ox, oy, x, baseline - fi->StrikeoutPos, pen,
                                    baseline - fi->StrikeoutPos + fi->UnderlineSize);
        DrawFillRect(dc->Surface, clip, &u, pixel);
    }
    if (st->TextAlign & TA_UPDATECP)
        st->CurrentPos.x += width;
    dirty.left -= 2;
    dirty.top -= 2;
    dirty.right += 2;
    dirty.bottom += 2;
    SurfaceMarkDirty(dc->Surface, &dirty);
}

void TextDraw(DC *dc, LONG x, LONG y, UINT options, const RECT *rect, PCWSTR text, ULONG count, const INT *dx)
{
    FONT_INSTANCE *fi = DcGetFont(dc);
    REGION clip;
    RgnInit(&clip);
    if (!DcGetClip(dc, &clip) || !fi) {
        RgnFree(&clip);
        return;
    }
    DC_STATE *st = &dc->State;
    /* without a rectangle there is nothing to fill or clip to: the background mode decides. */
    if (!rect)
        options &= ~(UINT)(ETO_OPAQUE | ETO_CLIPPED);
    RECT opaque = {0, 0, 0, 0};
    if (rect) {
        LONG l = rect->left, t = rect->top, r = rect->right, b = rect->bottom;
        DcLogicalToSurface(dc, &l, &t);
        DcLogicalToSurface(dc, &r, &b);
        opaque = (RECT){W32Min(l, r), W32Min(t, b), W32Max(l, r), W32Max(t, b)};
        if (options & ETO_OPAQUE)
            DrawFillRect(dc->Surface, &clip, &opaque, ColorToPixel(st->BkColor));
        if (options & ETO_CLIPPED)
            RgnCombineRect(&clip, &clip, &opaque, RGN_AND);
    }
    if (st->TextAlign & TA_UPDATECP) {
        x = st->CurrentPos.x;
        y = st->CurrentPos.y;
    }
    /* total advance for alignment and the background box. */
    LONG width = 0;
    if (dx) {
        for (ULONG i = 0; i < count; i++)
            width += dx[i];
    } else if (options & ETO_GLYPH_INDEX) {
        for (ULONG i = 0; i < count; i++) {
            const GLYPH *g = FontGetGlyphIndex(fi, text[i]);
            width += (g ? g->Advance : 0) + st->TextCharExtra;
        }
    } else {
        width = FontTextWidth(fi, text, count, st->TextCharExtra);
    }
    if (st->HasXform || st->HasPage) {
        TextDrawTransformed(dc, fi, &clip, x, y, options, text, count, dx, width);
        RgnFree(&clip);
        return;
    }
    LONG sx = x, sy = y;
    DcLogicalToSurface(dc, &sx, &sy);
    UINT halign = st->TextAlign & (TA_LEFT | TA_RIGHT | TA_CENTER);
    if (halign == TA_RIGHT)
        sx -= width;
    else if (halign == TA_CENTER)
        sx -= width / 2;
    LONG baseline;
    switch (st->TextAlign & (TA_TOP | TA_BOTTOM | TA_BASELINE)) {
    case TA_BASELINE:
        baseline = sy;
        break;
    case TA_BOTTOM:
        baseline = sy - fi->Metrics.tmDescent;
        break;
    default:
        baseline = sy + fi->Metrics.tmAscent;
        break;
    }
    RECT box = {sx, baseline - fi->Metrics.tmAscent, sx + width, baseline + fi->Metrics.tmDescent};
    if (st->BkMode == OPAQUE && !(options & ETO_OPAQUE))
        DrawFillRect(dc->Surface, &clip, &box, ColorToPixel(st->BkColor));
    ULONG pixel = ColorToPixel(st->TextColor);
    LONG pen = sx;
    ULONG i = 0, index = 0;
    while (i < count) {
        ULONG start = i;
        const GLYPH *g = (options & ETO_GLYPH_INDEX) ? FontGetGlyphIndex(fi, text[i++])
                                                     : FontGetGlyph(fi, next_char(text, count, &i));
        if (g)
            DrawGlyph(dc->Surface, &clip, pen, baseline, g, pixel);
        if (dx) {
            for (ULONG k = start; k < i; k++)
                pen += dx[index++];
        } else {
            pen += (g ? g->Advance : 0) + st->TextCharExtra;
        }
    }
    if (fi->Underline) {
        RECT u = {sx, baseline + fi->UnderlinePos, pen, baseline + fi->UnderlinePos + fi->UnderlineSize};
        DrawFillRect(dc->Surface, &clip, &u, pixel);
    }
    if (fi->StrikeOut) {
        RECT s = {sx, baseline - fi->StrikeoutPos, pen, baseline - fi->StrikeoutPos + fi->UnderlineSize};
        DrawFillRect(dc->Surface, &clip, &s, pixel);
    }
    if (st->TextAlign & TA_UPDATECP)
        st->CurrentPos.x += width;
    RECT dirty = box;
    if (rect && (options & ETO_OPAQUE))
        RectUnion(&dirty, &dirty, &opaque);
    /* glyphs can overhang their advance box. */
    LONG slack = fi->EmPixels / 2 + 2;
    dirty.left -= slack;
    dirty.right += slack;
    dirty.top -= 2;
    dirty.bottom += 2;
    SurfaceMarkDirty(dc->Surface, &dirty);
    RgnFree(&clip);
}

/* ------------------------------------------------------------------ services */

#define MAX_TEXT 8192

BOOL NTAPI NtGdiExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT *user_rect, LPCWSTR user_text, int count,
                            const INT *user_dx, DWORD code_page)
{
    if (count < 0 || count > MAX_TEXT)
        return FALSE;
    RECT rect;
    if (user_rect && !NT_SUCCESS(MimeCopyFromUser(&rect, user_rect, sizeof(rect))))
        return FALSE;
    PWSTR text = W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2);
    INT *dx = user_dx ? W32Alloc((SIZE_T)count * sizeof(INT) * ((options & ETO_PDY) ? 2 : 1) + 4) : nullptr;
    BOOL ok = FALSE;
    if (text && (!user_dx || dx) && NT_SUCCESS(MimeCopyFromUser(text, user_text, (SIZE_T)count * sizeof(WCHAR))) &&
        (!user_dx || NT_SUCCESS(MimeCopyFromUser(dx, user_dx, (SIZE_T)count * sizeof(INT) * ((options & ETO_PDY) ? 2 : 1))))) {
        if (dx && (options & ETO_PDY)) {
            /* keep only the x advances; vertical offsets are not supported yet. */
            for (int i = 0; i < count; i++)
                dx[i] = dx[2 * i];
        }
        UserEnter();
        DC *dc = DcLookup(hdc);
        if (dc) {
            if (dc->Surface)
                TextDraw(dc, x, y, options, user_rect ? &rect : nullptr, text, (ULONG)count, dx);
            ok = TRUE;
        }
        UserLeave();
    }
    W32Free(text);
    W32Free(dx);
    return ok;
}

BOOL NTAPI NtGdiGetTextExtentExW(HDC hdc, LPCWSTR user_text, ULONG count, ULONG max_extent, ULONG *fit, ULONG *user_dx,
                                 LPSIZE size, ULONG flags)
{
    if (count > MAX_TEXT)
        return FALSE;
    PWSTR text = W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2);
    ULONG *dx = user_dx ? W32Alloc((SIZE_T)count * sizeof(ULONG) + 4) : nullptr;
    BOOL ok = FALSE;
    SIZE sz = {0, 0};
    ULONG fitted = 0;
    if (text && (!user_dx || dx) && NT_SUCCESS(MimeCopyFromUser(text, user_text, (SIZE_T)count * sizeof(WCHAR)))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
        if (fi) {
            LONG w = 0;
            for (ULONG i = 0; i < count; i++) {
                const GLYPH *g = (flags & MIME_GTEE_INDICES) ? FontGetGlyphIndex(fi, text[i]) : FontGetGlyph(fi, text[i]);
                w += (g ? g->Advance : 0) + dc->State.TextCharExtra;
                if (dx)
                    dx[i] = (ULONG)w;
                if ((ULONG)w <= max_extent)
                    fitted = i + 1;
            }
            sz.cx = w;
            sz.cy = fi->Metrics.tmHeight;
            ok = TRUE;
        }
        UserLeave();
    }
    if (ok) {
        if (size)
            ok = NT_SUCCESS(MimeCopyToUser(size, &sz, sizeof(sz)));
        if (ok && fit)
            ok = NT_SUCCESS(MimeCopyToUser(fit, &fitted, sizeof(fitted)));
        if (ok && dx)
            ok = NT_SUCCESS(MimeCopyToUser(user_dx, dx, count * sizeof(ULONG)));
    }
    W32Free(text);
    W32Free(dx);
    return ok;
}

BOOL NTAPI NtGdiGetTextMetricsW(HDC hdc, TEXTMETRICW *metrics, ULONG size)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    TEXTMETRICW tm;
    if (fi)
        tm = fi->Metrics;
    UserLeave();
    if (!fi)
        return FALSE;
    ULONG n = size < sizeof(tm) ? size : sizeof(tm);
    return NT_SUCCESS(MimeCopyToUser(metrics, &tm, n));
}

int NTAPI NtGdiGetTextFaceW(HDC hdc, int count, LPWSTR name, BOOL alias)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    WCHAR face[LF_FACESIZE] = {};
    if (fi)
        FontGetFaceName(fi, face, LF_FACESIZE);
    UserLeave();
    if (!fi)
        return 0;
    int n = (int)wcslen(face) + 1;
    if (!name)
        return n;
    if (count <= 0)
        return 0;
    if (n > count) {
        n = count;
        face[n - 1] = 0;
    }
    if (!NT_SUCCESS(MimeCopyToUser(name, face, (SIZE_T)n * sizeof(WCHAR))))
        return 0;
    return n - 1;
}

/* widths are integers; chars may give explicit characters, or glyph indices with MIME_GCW_INDICES. */
BOOL NTAPI NtGdiGetCharWidthW(HDC hdc, UINT first, UINT count, PWCHAR user_chars, ULONG flags, PVOID buffer)
{
    if (count > 65536)
        return FALSE;
    INT *widths = W32Alloc((SIZE_T)count * sizeof(INT) + 4);
    PWCHAR chars = user_chars ? W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2) : nullptr;
    BOOL ok = FALSE;
    if (widths && (!user_chars || (chars && NT_SUCCESS(MimeCopyFromUser(chars, user_chars, count * sizeof(WCHAR)))))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
        if (fi) {
            for (UINT i = 0; i < count; i++) {
                ULONG c = chars ? chars[i] : first + i;
                const GLYPH *g = (flags & MIME_GCW_INDICES) ? FontGetGlyphIndex(fi, (USHORT)c) : FontGetGlyph(fi, c);
                widths[i] = g ? g->Advance : 0;
            }
            ok = TRUE;
        }
        UserLeave();
        if (ok)
            ok = NT_SUCCESS(MimeCopyToUser(buffer, widths, count * sizeof(INT)));
    }
    W32Free(widths);
    W32Free(chars);
    return ok;
}

/* integer ABC widths come from the rendered glyphs, MIME_GCABCW_FLOAT ones from the outline bounds. */
BOOL NTAPI NtGdiGetCharABCWidthsW(HDC hdc, UINT first, ULONG count, PWCHAR user_chars, ULONG flags, PVOID buffer)
{
    if (count > 65536)
        return FALSE;
    ABC *abc = W32Alloc((SIZE_T)count * sizeof(ABC) + 4);
    PWCHAR chars = user_chars ? W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2) : nullptr;
    BOOL ok = FALSE;
    if (abc && (!user_chars || (chars && NT_SUCCESS(MimeCopyFromUser(chars, user_chars, count * sizeof(WCHAR)))))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
        if (fi) {
            for (ULONG i = 0; i < count; i++) {
                ULONG c = chars ? chars[i] : first + i;
                USHORT index = (flags & MIME_GCABCW_INDICES) ? (USHORT)c : FontGlyphIndex(fi, c);
                if (flags & MIME_GCABCW_FLOAT) {
                    FontGlyphAbcFloat(fi, index, (ABCFLOAT *)&abc[i]);
                    continue;
                }
                const GLYPH *g = FontGetGlyphIndex(fi, index);
                if (g && g->Width) {
                    abc[i].abcA = g->Left;
                    abc[i].abcB = g->Width;
                    abc[i].abcC = g->Advance - g->Left - g->Width;
                } else {
                    abc[i].abcA = 0;
                    abc[i].abcB = 0;
                    abc[i].abcC = g ? g->Advance : 0;
                }
            }
            ok = TRUE;
        }
        UserLeave();
        if (ok)
            ok = NT_SUCCESS(MimeCopyToUser(buffer, abc, count * sizeof(ABC)));
    }
    W32Free(abc);
    W32Free(chars);
    return ok;
}
