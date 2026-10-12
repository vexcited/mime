/*
 * text output and measurement services.
 */
#include "../win32k.h"

/* code points from utf-16, surrogate pairs combined. */
static ULONG next_char(PCWSTR text, ULONG count, ULONG *i)
{
    WCHAR c = text[(*i)++];
    if (c >= 0xD800 && c < 0xDC00 && *i < count && text[*i] >= 0xDC00 && text[*i] < 0xE000)
        return 0x10000 + (((ULONG)c - 0xD800) << 10) + (text[(*i)++] - 0xDC00);
    return c;
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
    } else {
        width = FontTextWidth(fi, text, count, st->TextCharExtra);
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
        ULONG cp = (options & ETO_GLYPH_INDEX) ? text[i++] : next_char(text, count, &i);
        const GLYPH *g = FontGetGlyph(fi, cp);
        if (g && g->Width && g->Coverage)
            DrawCoverage(dc->Surface, &clip, pen + g->Left, baseline + g->Top, g->Coverage, g->Width, g->Height,
                         g->Width, pixel);
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
    INT *dx = user_dx ? W32Alloc((SIZE_T)count * sizeof(INT) * ((options & ETO_PDY) ? 2 : 1) + 4) : NULL;
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
                TextDraw(dc, x, y, options, user_rect ? &rect : NULL, text, (ULONG)count, dx);
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
    ULONG *dx = user_dx ? W32Alloc((SIZE_T)count * sizeof(ULONG) + 4) : NULL;
    BOOL ok = FALSE;
    SIZE sz = {0, 0};
    ULONG fitted = 0;
    if (text && (!user_dx || dx) && NT_SUCCESS(MimeCopyFromUser(text, user_text, (SIZE_T)count * sizeof(WCHAR)))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : NULL;
        if (fi) {
            LONG w = 0;
            for (ULONG i = 0; i < count; i++) {
                const GLYPH *g = FontGetGlyph(fi, text[i]);
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
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : NULL;
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
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : NULL;
    WCHAR face[LF_FACESIZE] = {0};
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

/* flags bit 0: widths as integers rather than floats; chars may give explicit characters. */
BOOL NTAPI NtGdiGetCharWidthW(HDC hdc, UINT first, UINT count, PWCHAR user_chars, ULONG flags, PVOID buffer)
{
    if (count > 65536)
        return FALSE;
    INT *widths = W32Alloc((SIZE_T)count * sizeof(INT) + 4);
    PWCHAR chars = user_chars ? W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2) : NULL;
    BOOL ok = FALSE;
    if (widths && (!user_chars || (chars && NT_SUCCESS(MimeCopyFromUser(chars, user_chars, count * sizeof(WCHAR)))))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : NULL;
        if (fi) {
            for (UINT i = 0; i < count; i++) {
                const GLYPH *g = FontGetGlyph(fi, chars ? chars[i] : first + i);
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

BOOL NTAPI NtGdiGetCharABCWidthsW(HDC hdc, UINT first, ULONG count, PWCHAR user_chars, ULONG flags, PVOID buffer)
{
    if (count > 65536)
        return FALSE;
    ABC *abc = W32Alloc((SIZE_T)count * sizeof(ABC) + 4);
    PWCHAR chars = user_chars ? W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2) : NULL;
    BOOL ok = FALSE;
    if (abc && (!user_chars || (chars && NT_SUCCESS(MimeCopyFromUser(chars, user_chars, count * sizeof(WCHAR)))))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : NULL;
        if (fi) {
            for (ULONG i = 0; i < count; i++) {
                const GLYPH *g = FontGetGlyph(fi, chars ? chars[i] : first + i);
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

int NTAPI NtGdiAddFontResourceW(LPCWSTR files, ULONG chars, ULONG files_count, ULONG flags, DWORD tid, PVOID dv)
{
    /* private fonts are not supported yet; report success so installers continue. */
    return 1;
}

ULONG NTAPI NtGdiGetFontData(HDC hdc, DWORD table, DWORD offset, PVOID buffer, ULONG size)
{
    return GDI_ERROR;
}
