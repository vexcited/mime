/* text output and measurement services. */
#include "font_private.h"

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

/*
 * the pen position after each code unit of a string: the advance and the extra of characters of each character, and
 * for each break character its share of the extra of SetTextJustification. as windows 11 does it, measured with textj.c
 * of scratch/dlls/gdibatch: the share keeps its 28.4 fraction, the positions round to pixels but the last one is
 * truncated, and the width returned rounds.
 */
static LONG text_positions(DC *dc, FONT_INSTANCE *fi, PCWSTR text, ULONG count, BOOLEAN indices, LONG *pos)
{
    const DC_STATE *st = &dc->State;
    LONG share = st->BreakCount > 0 ? (LONG)((LONG64)st->BreakExtra * 16 / st->BreakCount) : 0;
    ULONG brk = fi->Metrics.tmBreakChar;
    if (indices && share)
        brk = FontGlyphIndex(fi, brk);
    LONG64 at = 0;
    ULONG i = 0;
    while (i < count) {
        ULONG start = i, c = indices ? text[i++] : next_char(text, count, &i);
        const GLYPH *g = indices ? FontGetGlyphIndex(fi, (USHORT)c) : FontGetGlyph(fi, c);
        at += (LONG64)((g ? g->Advance : 0) + st->TextCharExtra) * 16;
        if (c == brk)
            at += share;
        for (ULONG k = start; pos && k < i; k++)
            pos[k] = (LONG)(i == count ? at >> 4 : (at + 8) >> 4);
    }
    return (LONG)((at + 8) >> 4);
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
                                PCWSTR text, ULONG count, const LONG *pos, LONG width)
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
        DrawFillRect(dc->Surface, clip, &dirty, DcColorPixel(dc, st->BkColor));
    ULONG pixel = DcColorPixel(dc, st->TextColor);
    LONG pen = x;
    ULONG i = 0;
    while (i < count) {
        FONT_INSTANCE *owner = fi;
        USHORT glyph = (options & ETO_GLYPH_INDEX) ? text[i++] : FontLinkedGlyph(fi, next_char(text, count, &i), &owner);
        LONG gx, gy;
        text_point(&gm, ox, oy, pen, baseline, &gx, &gy);
        LONG left, top, w, h;
        UCHAR *cov = FontRenderTransformed(owner, glyph, &gm, &left, &top, &w, &h);
        if (cov) {
            DrawCoverage(dc->Surface, clip, gx + left, gy + top, cov, w, h, w, pixel);
            RECT r = {gx + left, gy + top, gx + left + w, gy + top + h};
            RectUnion(&dirty, &dirty, &r);
            W32Free(cov);
        }
        pen = x + pos[i - 1];
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
    DcAccumulateBounds(dc, &dirty, clip);
    dirty.left -= 2;
    dirty.top -= 2;
    dirty.right += 2;
    dirty.bottom += 2;
    SurfaceMarkDrawn(dc->Surface, &dirty, clip);
}


void TextDraw(DC *dc, LONG x, LONG y, UINT options, const RECT *rect, PCWSTR text, ULONG count, const INT *dx)
{
    FONT_INSTANCE *fi = DcGetFont(dc);
    REGION clip;
    RgnInit(&clip);
    LONG *pos = count ? W32Alloc(count * sizeof(LONG)) : nullptr;
    if (!DcGetClip(dc, &clip) || !fi || (count && !pos)) {
        RgnFree(&clip);
        W32Free(pos);
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
            DrawFillRect(dc->Surface, &clip, &opaque, DcColorPixel(dc, st->BkColor));
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
            pos[i] = width += dx[i];
    } else {
        width = text_positions(dc, fi, text, count, (options & ETO_GLYPH_INDEX) != 0, pos);
    }
    /* a raster font is not transformed, only where it is drawn. */
    if ((st->HasXform || st->HasPage) && !fi->Face->Raster) {
        TextDrawTransformed(dc, fi, &clip, x, y, options, text, count, pos, width);
        RgnFree(&clip);
        W32Free(pos);
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
        DrawFillRect(dc->Surface, &clip, &box, DcColorPixel(dc, st->BkColor));
    ULONG pixel = DcColorPixel(dc, st->TextColor);
    LONG pen = sx;
    ULONG i = 0;
    while (i < count) {
        const GLYPH *g = (options & ETO_GLYPH_INDEX) ? FontGetGlyphIndex(fi, text[i++])
                                                     : FontGetGlyph(fi, next_char(text, count, &i));
        if (g)
            DrawGlyph(dc->Surface, &clip, pen, baseline, g, pixel);
        pen = sx + pos[i - 1];
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
    DcAccumulateBounds(dc, &dirty, &clip);
    /* glyphs can overhang their advance box. */
    LONG slack = fi->EmPixels / 2 + 2;
    dirty.left -= slack;
    dirty.right += slack;
    dirty.top -= 2;
    dirty.bottom += 2;
    SurfaceMarkDrawn(dc->Surface, &dirty, &clip);
    RgnFree(&clip);
    W32Free(pos);
}

/* ------------------------------------------------------------------ services */

/* the limits of win32kfull. */
#define MAX_TEXT_OUT 0xFFFF
#define MAX_TEXT_EXTENT_EX 0x682AAA
#define MAX_TEXT_EXTENT 0x1388000

BOOL NTAPI NtGdiExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT *user_rect, LPCWSTR user_text, int count,
                            const INT *user_dx, DWORD code_page)
{
    if (count < 0 || count > MAX_TEXT_OUT)
        return FALSE;
    /* the rectangle is read only when it fills or clips. */
    RECT rect;
    BOOLEAN has_rect = user_rect && (options & (ETO_OPAQUE | ETO_CLIPPED));
    if (has_rect && !NT_SUCCESS(MimeCopyFromUser(&rect, user_rect, sizeof(rect))))
        return FALSE;
    SIZE_T dx_size = (SIZE_T)count * sizeof(INT) * ((options & ETO_PDY) ? 2 : 1);
    PWSTR text = W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2);
    INT *dx = user_dx ? W32Alloc(dx_size + 4) : nullptr;
    BOOL ok = FALSE;
    if (text && (!user_dx || dx) && NT_SUCCESS(MimeCopyFromUser(text, user_text, (SIZE_T)count * sizeof(WCHAR))) &&
        (!user_dx || NT_SUCCESS(MimeCopyFromUser(dx, user_dx, dx_size)))) {
        if (dx && (options & ETO_PDY)) {
            /* keep only the x advances; vertical offsets are not supported yet. */
            for (int i = 0; i < count; i++)
                dx[i] = dx[2 * i];
        }
        UserEnterRead();
        DC *dc = DcLookup(hdc);
        if (dc) {
            if (dc->Surface)
                TextDraw(dc, x, y, options, has_rect ? &rect : nullptr, text, (ULONG)count, dx);
            ok = TRUE;
        }
        UserLeave();
    }
    W32Free(text);
    W32Free(dx);
    return ok;
}

/* the extent of a string and the positions after each of its code units, in logical units. */
static BOOL text_extent(HDC hdc, LPCWSTR user_text, ULONG count, BOOLEAN indices, BOOLEAN overhang, LONG *pos,
                        SIZE *size)
{
    PWSTR text = count ? W32Alloc((SIZE_T)count * sizeof(WCHAR)) : nullptr;
    if (count && (!text || !NT_SUCCESS(MimeCopyFromUser(text, user_text, (SIZE_T)count * sizeof(WCHAR))))) {
        W32Free(text);
        return FALSE;
    }
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    *size = (SIZE){0, 0};
    if (fi && count) {
        size->cx = text_positions(dc, fi, text, count, indices, pos);
        size->cy = fi->Metrics.tmHeight;
        if (overhang)
            size->cx += fi->Metrics.tmOverhang;
    }
    UserLeave();
    W32Free(text);
    return fi != nullptr;
}

/* without fit the whole string counts and every position is written, with it only those that fit. */
BOOL NTAPI NtGdiGetTextExtentExW(HDC hdc, LPCWSTR user_text, ULONG count, ULONG max_extent, ULONG *fit, ULONG *user_dx,
                                 LPSIZE size, ULONG flags)
{
    if (count > MAX_TEXT_EXTENT_EX || !size)
        return FALSE;
    LONG *pos = count ? W32Alloc((SIZE_T)count * sizeof(LONG)) : nullptr;
    SIZE sz;
    if ((count && !pos) || !text_extent(hdc, user_text, count, (flags & MIME_GTEE_INDICES) != 0, FALSE, pos, &sz)) {
        W32Free(pos);
        return FALSE;
    }
    ULONG fitted = count;
    if (fit)
        for (fitted = 0; fitted < count && pos[fitted] >= 0 && (ULONG)pos[fitted] <= max_extent; fitted++)
            ;
    BOOL ok = NT_SUCCESS(MimeCopyToUser(size, &sz, sizeof(sz)));
    if (ok && fit)
        ok = NT_SUCCESS(MimeCopyToUser(fit, &fitted, sizeof(fitted)));
    if (ok && user_dx && fitted)
        ok = NT_SUCCESS(MimeCopyToUser(user_dx, pos, fitted * sizeof(ULONG)));
    W32Free(pos);
    return ok;
}

BOOL NTAPI NtGdiGetTextExtent(HDC hdc, LPCWSTR user_text, int count, LPSIZE size, UINT flags)
{
    if (count < 0 || count > MAX_TEXT_EXTENT || !size)
        return FALSE;
    SIZE sz;
    if (!text_extent(hdc, user_text, (ULONG)count, (flags & MIME_GTE_INDICES) != 0, (flags & MIME_GTE_OVERHANG) != 0,
                     nullptr, &sz))
        return FALSE;
    return NT_SUCCESS(MimeCopyToUser(size, &sz, sizeof(sz)));
}

/* gdi32full asks 68 bytes: TEXTMETRICW, then a TMDIFF with the ansi characters TEXTMETRICA takes. */
typedef struct TMW_INTERNAL {
    TEXTMETRICW Metrics;
    ULONG Cjotma;
    UCHAR AnsiChars[4];
} TMW_INTERNAL;

BOOL NTAPI NtGdiGetTextMetricsW(HDC hdc, TEXTMETRICW *metrics, ULONG size)
{
    if (size < sizeof(TMW_INTERNAL))
        return FALSE;
    TMW_INTERNAL tmi = {};
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    if (fi) {
        tmi.Metrics = fi->Metrics;
        memcpy(tmi.AnsiChars, FontAnsiChars(fi), 4);
    }
    UserLeave();
    if (!fi)
        return FALSE;
    return NT_SUCCESS(MimeCopyToUser(metrics, &tmi, sizeof(tmi)));
}

/* the count copied, its terminating nul included, or the count a buffer needs. */
int NTAPI NtGdiGetTextFaceW(HDC hdc, int count, LPWSTR name, BOOL alias)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    WCHAR face[LF_FACESIZE] = {};
    if (fi)
        FontGetFaceName(fi, dc->State.Font->Logical.lfFaceName, face, LF_FACESIZE);
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
    return n;
}

/* INT widths with MIME_GCW_INT, else FLOAT ones, both logical; chars may give the characters or glyph indices. */
BOOL NTAPI NtGdiGetCharWidthW(HDC hdc, UINT first, UINT count, PWCHAR user_chars, ULONG flags, PVOID buffer)
{
    if (!count || count > 65536 || !buffer)
        return FALSE;
    INT *widths = W32Alloc((SIZE_T)count * sizeof(INT));
    PWCHAR chars = user_chars ? W32Alloc((SIZE_T)count * sizeof(WCHAR)) : nullptr;
    BOOL ok = FALSE;
    if (widths && (!user_chars || (chars && NT_SUCCESS(MimeCopyFromUser(chars, user_chars, count * sizeof(WCHAR)))))) {
        UserEnterRead();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
        if (fi) {
            LONG overhang = (flags & MIME_GCW_OVERHANG) ? fi->Metrics.tmOverhang : 0;
            for (UINT i = 0; i < count; i++) {
                ULONG c = chars ? chars[i] : first + i;
                const GLYPH *g = (flags & MIME_GCW_INDICES) ? FontGetGlyphIndex(fi, (USHORT)c) : FontGetGlyph(fi, c);
                LONG w = (g ? g->Advance : 0) + overhang;
                /*
                 * win32kfull's GrepGetCharWidthW scales the 28.4 advance by 1/16 and then by a device to world
                 * factor that holds another 1/16, so GetCharWidthFloat gives a sixteenth of the width.
                 */
                if (flags & MIME_GCW_INT)
                    widths[i] = w;
                else
                    ((FLOAT *)widths)[i] = (FLOAT)w / 16.0f;
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

/*
 * ABC with MIME_GCABCW_INT, else ABCFLOAT. the widths come from the rendered glyphs; windows gives the float ones the
 * same whole values for a truetype font, which uniscribe adds up to place glyphs.
 */
BOOL NTAPI NtGdiGetCharABCWidthsW(HDC hdc, UINT first, ULONG count, PWCHAR user_chars, ULONG flags, PVOID buffer)
{
    if (!count || count > 65536 || !buffer)
        return FALSE;
    ABC *abc = W32Alloc((SIZE_T)count * sizeof(ABC));
    PWCHAR chars = user_chars ? W32Alloc((SIZE_T)count * sizeof(WCHAR)) : nullptr;
    BOOL ok = FALSE;
    if (abc && (!user_chars || (chars && NT_SUCCESS(MimeCopyFromUser(chars, user_chars, count * sizeof(WCHAR)))))) {
        UserEnterRead();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
        if (fi) {
            for (ULONG i = 0; i < count; i++) {
                ULONG c = chars ? chars[i] : first + i;
                USHORT index = (flags & MIME_GCABCW_INDICES) ? (USHORT)c : FontGlyphIndex(fi, c);
                const GLYPH *g = FontGetGlyphIndex(fi, index);
                LONG a = 0, b = 0, cw = g ? g->Advance : 0;
                if (g && g->Width) {
                    a = g->Left;
                    b = g->Width;
                    cw = g->Advance - g->Left - g->Width;
                }
                if (flags & MIME_GCABCW_INT)
                    abc[i] = (ABC){a, (UINT)b, cw};
                else
                    ((ABCFLOAT *)abc)[i] = (ABCFLOAT){(FLOAT)a, (FLOAT)b, (FLOAT)cw};
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

/* ------------------------------------------------------------------ what gdi32full asks before it draws */

/*
 * FONT_REALIZATION_INFO of 16, 24 or 812 bytes. FontHasWesternScript of gdi32full keys its cache on the file id at
 * 12, uniscribe hashes the realization id at 8 with it. 4 is the technology, 3 for truetype outlines and 1 for a
 * raster face; 16 is 1 for every face measured, 20 the linked faces and 22 the simulations: 1 bold, 2 italic.
 */
BOOL NTAPI NtGdiGetRealizationInfo(HDC hdc, PVOID user_info)
{
    ULONG size;
    if (!NT_SUCCESS(MimeCopyFromUser(&size, user_info, sizeof(size))) || (size != 16 && size != 24 && size != 812))
        return FALSE;
    UCHAR *info = W32Alloc(812);
    if (!info)
        return FALSE;
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    if (fi) {
        *(ULONG *)info = size;
        info[4] = fi->Face->Raster ? 1 : 3;
        info[5] = 1;
        *(ULONG *)(info + 8) = fi->Id;
        *(ULONG *)(info + 12) = FontFaceId(fi->Face);
        *(ULONG *)(info + 16) = 1;
        *(USHORT *)(info + 22) = (USHORT)((fi->SimBold ? 1 : 0) | (fi->SimItalic ? 2 : 0));
        /* a raster face stretched, Small Fonts at -60 five times, says by how much. */
        if (fi->Face->Raster && fi->ScaleY > 1)
            *(USHORT *)(info + 20) = (USHORT)fi->ScaleY;
    }
    UserLeave();
    BOOL ok = fi && NT_SUCCESS(MimeCopyToUser(user_info, info, size));
    W32Free(info);
    return ok;
}

/* no font association is set up, as on a western windows; any other answer turns off the width cache of gdi32. */
DWORD NTAPI NtGdiQueryFontAssocInfo(HDC hdc)
{
    return 0;
}

/* faces always link others for the characters they lack, see font.c. */
BOOL NTAPI NtGdiAnyLinkedFonts(void)
{
    return TRUE;
}

BOOL NTAPI NtGdiFontIsLinked(HDC hdc)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    BOOL linked = fi && FontHasSystemLink(fi->Face);
    UserLeave();
    return linked;
}

/*
 * the charset of the font in the high word and its code page in the low one. like windows it also leaves them in the
 * attributes of the dc, where gdi32 looks first.
 */
DWORD NTAPI NtGdiGetCharSet(HDC hdc)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    DWORD value = DEFAULT_CHARSET << 16;
    if (fi) {
        value = (DWORD)fi->CharSet << 16 | FontCharSetCodePage(fi->CharSet);
        dc->AttrCharSet = value;
    }
    UserLeave();
    return value;
}

int NTAPI NtGdiGetTextCharsetInfo(HDC hdc, FONTSIGNATURE *user_signature, DWORD flags)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    int charset = DEFAULT_CHARSET;
    FONTSIGNATURE sig = {};
    if (fi) {
        charset = fi->CharSet;
        sig = fi->Face->Signature;
    }
    UserLeave();
    if (fi && user_signature && !NT_SUCCESS(MimeCopyToUser(user_signature, &sig, sizeof(sig))))
        return DEFAULT_CHARSET;
    return charset;
}

/* the dpi scale of the dc, which gdi32 also reads at 0x88 of its attributes: always 1. */
DWORD NTAPI NtGdiGetDCDpiScaleValue(HDC hdc)
{
    return 1;
}
