/*
 * painting services and the user32 drawing helpers: rectangles, edges,
 * focus rectangles and DrawText.
 */
#include "user32_private.h"

WINUSERAPI HDC WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT ps)
{
    return NtUserBeginPaint(hwnd, ps);
}

WINUSERAPI BOOL WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps)
{
    return NtUserEndPaint(hwnd, ps);
}

WINUSERAPI HDC WINAPI GetDC(HWND hwnd)
{
    return NtUserGetDC(hwnd);
}

WINUSERAPI HDC WINAPI GetWindowDC(HWND hwnd)
{
    return NtUserGetWindowDC(hwnd);
}

WINUSERAPI HDC WINAPI GetDCEx(HWND hwnd, HRGN clip, DWORD flags)
{
    return NtUserGetDCEx(hwnd, clip, flags);
}

WINUSERAPI int WINAPI ReleaseDC(HWND hwnd, HDC dc)
{
    return NtUserReleaseDC(hwnd, dc);
}

WINUSERAPI BOOL WINAPI InvalidateRect(HWND hwnd, const RECT *rect, BOOL erase)
{
    return NtUserInvalidateRect(hwnd, rect, erase);
}

WINUSERAPI BOOL WINAPI InvalidateRgn(HWND hwnd, HRGN region, BOOL erase)
{
    return NtUserRedrawWindow(hwnd, NULL, region, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
}

WINUSERAPI BOOL WINAPI ValidateRect(HWND hwnd, const RECT *rect)
{
    return NtUserValidateRect(hwnd, rect);
}

WINUSERAPI BOOL WINAPI ValidateRgn(HWND hwnd, HRGN region)
{
    return NtUserRedrawWindow(hwnd, NULL, region, RDW_VALIDATE | RDW_NOCHILDREN);
}

WINUSERAPI BOOL WINAPI RedrawWindow(HWND hwnd, const RECT *rect, HRGN region, UINT flags)
{
    return NtUserRedrawWindow(hwnd, rect, region, flags);
}

WINUSERAPI BOOL WINAPI GetUpdateRect(HWND hwnd, LPRECT rect, BOOL erase)
{
    return NtUserGetUpdateRect(hwnd, rect, erase);
}

WINUSERAPI int WINAPI GetUpdateRgn(HWND hwnd, HRGN region, BOOL erase)
{
    RECT r;
    if (!NtUserGetUpdateRect(hwnd, &r, erase)) {
        SetRectRgn(region, 0, 0, 0, 0);
        return NULLREGION;
    }
    SetRectRgn(region, r.left, r.top, r.right, r.bottom);
    return SIMPLEREGION;
}

WINUSERAPI BOOL WINAPI LockWindowUpdate(HWND hwnd)
{
    return TRUE;
}

/* ------------------------------------------------------------------ rectangles and edges */

static HBRUSH brush_of(HBRUSH brush)
{
    if ((ULONG_PTR)brush && (ULONG_PTR)brush <= COLOR_MENUBAR + 1)
        return GetSysColorBrush((int)(ULONG_PTR)brush - 1);
    return brush;
}

WINUSERAPI int WINAPI FillRect(HDC dc, const RECT *rect, HBRUSH brush)
{
    HGDIOBJ old = SelectObject(dc, brush_of(brush));
    PatBlt(dc, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, PATCOPY);
    SelectObject(dc, old);
    return 1;
}

WINUSERAPI int WINAPI FrameRect(HDC dc, const RECT *r, HBRUSH brush)
{
    if (r->right <= r->left || r->bottom <= r->top)
        return 0;
    HGDIOBJ old = SelectObject(dc, brush_of(brush));
    PatBlt(dc, r->left, r->top, r->right - r->left, 1, PATCOPY);
    PatBlt(dc, r->left, r->bottom - 1, r->right - r->left, 1, PATCOPY);
    PatBlt(dc, r->left, r->top, 1, r->bottom - r->top, PATCOPY);
    PatBlt(dc, r->right - 1, r->top, 1, r->bottom - r->top, PATCOPY);
    SelectObject(dc, old);
    return 1;
}

WINUSERAPI BOOL WINAPI InvertRect(HDC dc, const RECT *r)
{
    return PatBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, DSTINVERT);
}

/* the dotted focus rectangle: every other pixel inverted, like the classic look. */
void UserDrawFocusRect(HDC dc, const RECT *r)
{
    COLORREF old_text = SetTextColor(dc, RGB(0, 0, 0));
    for (LONG x = r->left; x < r->right; x += 2) {
        PatBlt(dc, x, r->top, 1, 1, DSTINVERT);
        PatBlt(dc, x, r->bottom - 1, 1, 1, DSTINVERT);
    }
    for (LONG y = r->top + 2; y < r->bottom - 1; y += 2) {
        PatBlt(dc, r->left, y, 1, 1, DSTINVERT);
        PatBlt(dc, r->right - 1, y, 1, 1, DSTINVERT);
    }
    SetTextColor(dc, old_text);
}

WINUSERAPI BOOL WINAPI DrawFocusRect(HDC dc, const RECT *r)
{
    UserDrawFocusRect(dc, r);
    return TRUE;
}

static void edge_line(HDC dc, int x, int y, int w, int h, int color)
{
    if (w <= 0 || h <= 0)
        return;
    HGDIOBJ old = SelectObject(dc, GetSysColorBrush(color));
    PatBlt(dc, x, y, w, h, PATCOPY);
    SelectObject(dc, old);
}

WINUSERAPI BOOL WINAPI DrawEdge(HDC dc, LPRECT rect, UINT edge, UINT flags)
{
    RECT r = *rect;
    /* outer then inner ring; raised edges are light at the top left. */
    for (int ring = 0; ring < 2; ring++) {
        UINT bits = ring == 0 ? (edge & (BDR_RAISEDOUTER | BDR_SUNKENOUTER)) : (edge & (BDR_RAISEDINNER | BDR_SUNKENINNER));
        if (!bits)
            continue;
        BOOL raised = ring == 0 ? (bits & BDR_RAISEDOUTER) != 0 : (bits & BDR_RAISEDINNER) != 0;
        int tl, br;
        if (flags & BF_FLAT) {
            tl = br = COLOR_3DSHADOW;
        } else if (ring == 0) {
            tl = raised ? COLOR_3DLIGHT : COLOR_3DSHADOW;
            br = raised ? COLOR_3DDKSHADOW : COLOR_3DHILIGHT;
        } else {
            tl = raised ? COLOR_3DHILIGHT : COLOR_3DDKSHADOW;
            br = raised ? COLOR_3DSHADOW : COLOR_3DLIGHT;
        }
        if (flags & BF_TOP)
            edge_line(dc, r.left, r.top, r.right - r.left, 1, tl);
        if (flags & BF_LEFT)
            edge_line(dc, r.left, r.top, 1, r.bottom - r.top, tl);
        if (flags & BF_BOTTOM)
            edge_line(dc, r.left, r.bottom - 1, r.right - r.left, 1, br);
        if (flags & BF_RIGHT)
            edge_line(dc, r.right - 1, r.top, 1, r.bottom - r.top, br);
        if (flags & BF_LEFT)
            r.left++;
        if (flags & BF_TOP)
            r.top++;
        if (flags & BF_RIGHT)
            r.right--;
        if (flags & BF_BOTTOM)
            r.bottom--;
    }
    if (flags & BF_MIDDLE)
        FillRect(dc, &r, GetSysColorBrush(COLOR_3DFACE));
    if (flags & BF_ADJUST)
        *rect = r;
    return TRUE;
}

/* ------------------------------------------------------------------ DrawText */

typedef struct TEXT_LINE {
    int start;  /* index in the processed text */
    int length;
    int width;
} TEXT_LINE;

static int text_width(HDC dc, LPCWSTR text, int count, UINT format, int tab_width)
{
    if (count <= 0)
        return 0;
    if (!(format & DT_EXPANDTABS)) {
        SIZE size;
        GetTextExtentPoint32W(dc, text, count, &size);
        return size.cx;
    }
    int x = 0, start = 0;
    for (int i = 0; i <= count; i++) {
        if (i == count || text[i] == L'\t') {
            SIZE size = {0, 0};
            if (i > start)
                GetTextExtentPoint32W(dc, text + start, i - start, &size);
            x += size.cx;
            if (i < count && tab_width)
                x = (x / tab_width + 1) * tab_width;
            start = i + 1;
        }
    }
    return x;
}

/* removes mnemonic prefixes; returns the processed length and the position of the underlined character. */
static int strip_prefix(LPCWSTR in, int count, LPWSTR out, int *underline, UINT format)
{
    int n = 0;
    *underline = -1;
    for (int i = 0; i < count; i++) {
        if (in[i] == L'&' && !(format & DT_NOPREFIX)) {
            if (i + 1 < count && in[i + 1] == L'&') {
                out[n++] = L'&';
                i++;
                continue;
            }
            if (i + 1 < count && *underline < 0 && !(format & DT_HIDEPREFIX))
                *underline = n;
            continue;
        }
        out[n++] = in[i];
    }
    return n;
}

static void draw_underline(HDC dc, int x, int y, LPCWSTR line, int pos, const TEXTMETRICW *tm, UINT format,
                           int tab_width)
{
    int left = x + text_width(dc, line, pos, format, tab_width);
    SIZE ch;
    GetTextExtentPoint32W(dc, line + pos, 1, &ch);
    HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(dc));
    HGDIOBJ old = SelectObject(dc, pen);
    int uy = y + tm->tmAscent + 1;
    MoveToEx(dc, left, uy, NULL);
    LineTo(dc, left + ch.cx, uy);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void draw_tabbed(HDC dc, int x, int y, LPCWSTR text, int count, UINT format, int tab_width, const RECT *clip)
{
    UINT opts = (format & DT_NOCLIP) ? 0 : ETO_CLIPPED;
    if (!(format & DT_EXPANDTABS)) {
        ExtTextOutW(dc, x, y, opts, clip, text, (UINT)count, NULL);
        return;
    }
    int pos = x, start = 0;
    for (int i = 0; i <= count; i++) {
        if (i == count || text[i] == L'\t') {
            if (i > start) {
                ExtTextOutW(dc, pos, y, opts, clip, text + start, (UINT)(i - start), NULL);
                pos += text_width(dc, text + start, i - start, 0, 0);
            }
            if (i < count && tab_width)
                pos = x + ((pos - x) / tab_width + 1) * tab_width;
            start = i + 1;
        }
    }
}

/* splits text into lines at newlines and, with DT_WORDBREAK, at word boundaries within width. */
static int break_lines(HDC dc, LPCWSTR text, int count, UINT format, int width, int tab_width, TEXT_LINE *lines,
                       int max_lines)
{
    int n = 0, i = 0;
    BOOL single = (format & DT_SINGLELINE) != 0;
    while (i <= count && n < max_lines) {
        int start = i, end = i;
        while (end < count && (single || (text[end] != L'\n' && text[end] != L'\r')))
            end++;
        /* the paragraph [start, end) is cut into lines that fit. */
        int p = start;
        do {
            int line_end = end;
            if ((format & DT_WORDBREAK) && !single && width > 0 && text_width(dc, text + p, end - p, format, tab_width) > width) {
                int last_break = -1;
                int k = p;
                for (; k < end; k++) {
                    if (text_width(dc, text + p, k - p + 1, format, tab_width) > width)
                        break;
                    if (text[k] == L' ' || text[k] == L'\t' || text[k] == L'-')
                        last_break = k;
                }
                if (last_break >= p)
                    line_end = text[last_break] == L'-' ? last_break + 1 : last_break;
                else if (format & DT_EDITCONTROL)
                    line_end = k > p ? k : p + 1;
                else {
                    /* a single word wider than the line: keep it whole, like windows without DT_EDITCONTROL. */
                    line_end = p;
                    while (line_end < end && text[line_end] != L' ')
                        line_end++;
                }
            }
            int len = line_end - p;
            /* trailing spaces of a wrapped line are dropped. */
            while (len > 0 && line_end < end && text[p + len - 1] == L' ')
                len--;
            lines[n].start = p;
            lines[n].length = len;
            lines[n].width = text_width(dc, text + p, len, format, tab_width);
            n++;
            p = line_end;
            while (p < end && text[p] == L' ' && line_end < end)
                p++;
        } while (p < end && n < max_lines);
        if (end >= count)
            break;
        i = end + 1;
        if (text[end] == L'\r' && i < count && text[i] == L'\n')
            i++;
        if (i > count)
            break;
        if (i == count) {
            /* a trailing newline starts an empty last line. */
            if (n < max_lines)
                lines[n++] = (TEXT_LINE){count, 0, 0};
            break;
        }
    }
    return n;
}

static void apply_end_ellipsis(HDC dc, LPWSTR text, int *length, int width, UINT format, int tab_width)
{
    if (text_width(dc, text, *length, format, tab_width) <= width)
        return;
    int dots = text_width(dc, L"...", 3, 0, 0);
    int n = *length;
    while (n > 0 && text_width(dc, text, n, format, tab_width) + dots > width)
        n--;
    text[n] = L'.';
    text[n + 1] = L'.';
    text[n + 2] = L'.';
    *length = n + 3;
}

WINUSERAPI int WINAPI DrawTextExW(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params)
{
    if (!text || !rect)
        return 0;
    if (count < 0)
        count = (int)wcslen(text);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int line_height = tm.tmHeight + ((format & DT_EXTERNALLEADING) ? tm.tmExternalLeading : 0);
    int tab_width = tm.tmAveCharWidth * ((format & DT_TABSTOP) ? (int)((format >> 8) & 0xFF) : 8);
    if (format & DT_TABSTOP)
        format &= ~0xFF00u;
    if (params && params->iTabLength)
        tab_width = tm.tmAveCharWidth * params->iTabLength;

    /* processed copy with prefixes removed, room for an ellipsis. */
    LPWSTR buf = UserAlloc(((SIZE_T)count + 4) * sizeof(WCHAR));
    if (!buf)
        return 0;
    int underline;
    int length = strip_prefix(text, count, buf, &underline, format);
    if (format & DT_PREFIXONLY) {
        UserFree(buf);
        return 0;
    }

    int width = rect->right - rect->left;
    TEXT_LINE local[64];
    int max_lines = length / 2 + 2;
    TEXT_LINE *lines = max_lines <= 64 ? local : UserAlloc((SIZE_T)max_lines * sizeof(TEXT_LINE));
    if (!lines) {
        UserFree(buf);
        return 0;
    }
    if (format & DT_SINGLELINE) {
        for (int i = 0; i < length; i++)
            if (buf[i] == L'\r' || buf[i] == L'\n')
                buf[i] = L' ';
        if (format & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS)) {
            apply_end_ellipsis(dc, buf, &length, width, format, tab_width);
            if (underline >= length - 3)
                underline = -1;
        }
    }
    int nlines = break_lines(dc, buf, length, format, width, tab_width, lines, max_lines);
    int max_width = 0;
    for (int i = 0; i < nlines; i++)
        if (lines[i].width > max_width)
            max_width = lines[i].width;
    int total = nlines * line_height;

    if (format & DT_CALCRECT) {
        if (format & DT_SINGLELINE) {
            rect->right = rect->left + max_width;
            rect->bottom = rect->top + line_height;
        } else {
            if (max_width > width || !(format & DT_WORDBREAK))
                rect->right = rect->left + max_width;
            else if (format & DT_WORDBREAK)
                rect->right = rect->left + max_width;
            rect->bottom = rect->top + total;
        }
        if (lines != local)
            UserFree(lines);
        UserFree(buf);
        return total;
    }

    int y = rect->top;
    if (format & DT_SINGLELINE) {
        if (format & DT_VCENTER)
            y = rect->top + (rect->bottom - rect->top - line_height) / 2;
        else if (format & DT_BOTTOM)
            y = rect->bottom - line_height;
    }
    UINT old_align = SetTextAlign(dc, TA_LEFT | TA_TOP);
    for (int i = 0; i < nlines; i++) {
        int x = rect->left;
        if (format & DT_CENTER)
            x = rect->left + (width - lines[i].width) / 2;
        else if (format & DT_RIGHT)
            x = rect->right - lines[i].width;
        draw_tabbed(dc, x, y, buf + lines[i].start, lines[i].length, format, tab_width, rect);
        if (underline >= lines[i].start && underline < lines[i].start + lines[i].length)
            draw_underline(dc, x, y, buf + lines[i].start, underline - lines[i].start, &tm, format, tab_width);
        y += line_height;
    }
    SetTextAlign(dc, old_align);
    if (lines != local)
        UserFree(lines);
    UserFree(buf);
    if (format & DT_VCENTER && format & DT_SINGLELINE)
        return line_height + (rect->bottom - rect->top - line_height) / 2;
    return total;
}

WINUSERAPI int WINAPI DrawTextW(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format)
{
    return DrawTextExW(dc, (LPWSTR)text, count, rect, format & ~DT_MODIFYSTRING, NULL);
}

WINUSERAPI int WINAPI DrawTextA(HDC dc, LPCSTR text, int count, LPRECT rect, UINT format)
{
    int n;
    LPWSTR w = UserAtoWn(text, count, &n);
    if (!w)
        return 0;
    int r = DrawTextExW(dc, w, n, rect, format & ~DT_MODIFYSTRING, NULL);
    UserFree(w);
    return r;
}

WINUSERAPI LONG WINAPI TabbedTextOutW(HDC dc, int x, int y, LPCWSTR text, int count, int tabs, const INT *positions,
                                      int origin)
{
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int tab_width = tabs > 0 && positions ? positions[0] : tm.tmAveCharWidth * 8;
    draw_tabbed(dc, x, y, text, count, DT_EXPANDTABS | DT_NOCLIP, tab_width, NULL);
    return MAKELONG(text_width(dc, text, count, DT_EXPANDTABS, tab_width), tm.tmHeight);
}

WINUSERAPI DWORD WINAPI GetTabbedTextExtentW(HDC dc, LPCWSTR text, int count, int tabs, const INT *positions)
{
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int tab_width = tabs > 0 && positions ? positions[0] : tm.tmAveCharWidth * 8;
    return MAKELONG(text_width(dc, text, count, DT_EXPANDTABS, tab_width), tm.tmHeight);
}

/* the message font of NONCLIENTMETRICS for a dpi, cached. */
HFONT UserGetMessageFont(UINT dpi)
{
    static HFONT fonts[4];
    static UINT dpis[4];
    for (int i = 0; i < 4; i++)
        if (fonts[i] && dpis[i] == dpi)
            return fonts[i];
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi))
        return GetStockObject(DEFAULT_GUI_FONT);
    HFONT f = CreateFontIndirectW(&ncm.lfMessageFont);
    for (int i = 0; i < 4; i++)
        if (!fonts[i]) {
            fonts[i] = f;
            dpis[i] = dpi;
            return f;
        }
    return f;
}
