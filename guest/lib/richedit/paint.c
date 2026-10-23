/*
 * painting: the background, each line's runs in their fonts and colors, the
 * selection, bullets and numbers, drawn through a bitmap to avoid flicker.
 * also the caret.
 */
#include "richedit_private.h"

static int imax(int a, int b) { return a > b ? a : b; }

static BOOL is_break(WCHAR c)
{
    return c == RE_CR || c == RE_VT || c == RE_FF;
}

static BOOL shows_selection(RE *e)
{
    return e->anchor != e->active && (e->focus || e->nohidesel);
}

void ReInvalidate(RE *e)
{
    if (e->redraw)
        InvalidateRect(e->hwnd, nullptr, FALSE);
}

/* the right edge of the character at k on its line. */
static int right_of(RE *e, const RE_LINE *line, UINT k)
{
    UINT end = line->cp + line->len;
    if (k + 1 < end)
        return e->xpos[k + 1];
    RE_FONT *f = ReFont(e, e->cfi[k]);
    return imax(line->width, e->xpos[k] + (is_break(e->text[k]) ? f->ave : 0));
}

static COLORREF text_color(RE *e, const RE_FMT *f)
{
    if (e->disabled || (f->cf.dwEffects & CFE_DISABLED))
        return GetSysColor(COLOR_GRAYTEXT);
    if (!(f->cf.dwEffects & CFE_AUTOCOLOR))
        return f->cf.crTextColor;
    if ((f->cf.dwEffects & CFE_LINK) || f->autolink)
        return GetSysColor(COLOR_HOTLIGHT);
    return GetSysColor(COLOR_WINDOWTEXT);
}

static void draw_line(RE *e, HDC dc, const RE_LINE *line, UINT smin, UINT smax, BOOL sel)
{
    int base = e->view.left - e->xscroll + line->x;
    int top = e->view.top - e->yscroll + line->y;
    int baseline = top + line->ascent;
    UINT end = line->cp + line->len;
    if (line->number && (line->flags & LINE_PARA_START)) {
        UINT mark = ReParaEnd(e, line->cp);
        WCHAR label[32];
        ReBulletText(e, &e->paras[e->pfi[mark]], line->number, label, 32);
        RE_FONT *f = ReFont(e, e->cfi[line->cp < e->len ? line->cp : mark]);
        HGDIOBJ old = SelectObject(dc, f->font);
        SetTextColor(dc, text_color(e, &e->fmts[e->cfi[line->cp]]));
        SetBkMode(dc, TRANSPARENT);
        ExtTextOutW(dc, e->view.left - e->xscroll + line->bullet_x, baseline - f->ascent, 0, nullptr, label,
                    (UINT)lstrlenW(label), nullptr);
        SelectObject(dc, old);
    }
    INT dx[130];
    WCHAR masked[130];
    for (UINT k = line->cp; k < end;) {
        BOOL selected = sel && k >= smin && k < smax;
        WCHAR c = e->text[k];
        if (is_break(c) || c == '\t') {
            int l = base + e->xpos[k], r = base + right_of(e, line, k);
            if (selected && (c == RE_CR ? k < e->len - 1 || smax > e->len - 1 : TRUE)) {
                RECT rc = {l, top, r, top + line->height};
                SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
                ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
            } else if (c == '\t') {
                const RE_FMT *f = &e->fmts[e->cfi[k]];
                if (!(f->cf.dwEffects & CFE_AUTOBACKCOLOR)) {
                    RECT rc = {l, top, r, top + line->height};
                    SetBkColor(dc, f->cf.crBackColor);
                    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
                }
            }
            k++;
            continue;
        }
        WORD fk = e->cfi[k];
        UINT j = k;
        while (j < end && j - k < 128 && e->cfi[j] == fk && !is_break(e->text[j]) && e->text[j] != '\t' &&
               (sel && j >= smin && j < smax) == selected)
            j++;
        const RE_FMT *f = &e->fmts[fk];
        RE_FONT *font = ReFont(e, fk);
        int l = base + e->xpos[k];
        int r = base + right_of(e, line, j - 1);
        for (UINT m = k; m < j; m++) {
            dx[m - k] = (m + 1 < j ? e->xpos[m + 1] : right_of(e, line, m)) - e->xpos[m];
            masked[m - k] = e->password ? e->password : e->text[m];
        }
        RECT rc = {l, top, r, top + line->height};
        UINT flags = 0;
        if (selected) {
            SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            flags = ETO_OPAQUE;
        } else {
            SetTextColor(dc, text_color(e, f));
            if (!(f->cf.dwEffects & CFE_AUTOBACKCOLOR)) {
                SetBkColor(dc, f->cf.crBackColor);
                flags = ETO_OPAQUE;
            }
        }
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, font->font);
        if (flags & ETO_OPAQUE)
            ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
        if (!(f->cf.dwEffects & CFE_HIDDEN))
            ExtTextOutW(dc, l, baseline - font->ascent - font->offset, 0, nullptr, masked, j - k, dx);
        SelectObject(dc, old);
        k = j;
    }
}

void RePaint(RE *e, HDC target, const RECT *update)
{
    ReLayout(e);
    RECT client;
    GetClientRect(e->hwnd, &client);
    int w = client.right - client.left, h = client.bottom - client.top;
    if (w <= 0 || h <= 0)
        return;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, w, h);
    HGDIOBJ old_bitmap = nullptr;
    if (dc && bitmap)
        old_bitmap = SelectObject(dc, bitmap);
    else {
        if (dc)
            DeleteDC(dc);
        if (bitmap)
            DeleteObject(bitmap);
        dc = target;
        bitmap = nullptr;
    }
    COLORREF bk = e->bk_sys ? GetSysColor(COLOR_WINDOW) : e->bk;
    HBRUSH brush = CreateSolidBrush(bk);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    SaveDC(dc);
    IntersectClipRect(dc, e->view.left, e->view.top, e->view.right, e->view.bottom);
    UINT smin = ReSelMin(e), smax = ReSelMax(e);
    BOOL sel = shows_selection(e);
    int vh = e->view.bottom - e->view.top;
    for (UINT i = 0; i < e->line_count; i++) {
        const RE_LINE *line = &e->lines[i];
        if (line->y + line->height <= e->yscroll)
            continue;
        if (line->y >= e->yscroll + vh)
            break;
        int top = e->view.top - e->yscroll + line->y;
        if (update && (top >= update->bottom || top + line->height <= update->top))
            continue;
        draw_line(e, dc, line, smin, smax, sel);
    }
    RestoreDC(dc, -1);
    if (bitmap) {
        BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
}

static int caret_height(RE *e)
{
    ReLayout(e);
    if (!e->line_count)
        return ReFont(e, e->def_cf)->ascent + ReFont(e, e->def_cf)->descent;
    UINT l = ReLineOf(e, e->active, e->at_line_end);
    RE_FONT *f = ReFont(e, ReInsertFormat(e));
    int h = f->ascent + f->descent;
    if (h > e->lines[l].height)
        h = e->lines[l].height;
    return h > 0 ? h : 1;
}

void RePlaceCaret(RE *e)
{
    if (!e->focus)
        return;
    if (caret_height(e) != e->caret_h) {
        ReCreateCaret(e);
        return;
    }
    POINT p = RePosFromCp(e, e->active, e->at_line_end);
    if (e->line_count) {
        UINT l = ReLineOf(e, e->active, e->at_line_end);
        RE_FONT *f = ReFont(e, ReInsertFormat(e));
        p.y += e->lines[l].ascent - f->ascent;
        if (p.y < e->view.top - e->yscroll + e->lines[l].y)
            p.y = e->view.top - e->yscroll + e->lines[l].y;
    }
    BOOL inside = p.x >= e->view.left - 1 && p.x <= e->view.right && p.y + 1 >= e->view.top && p.y < e->view.bottom;
    if (!inside || e->anchor != e->active) {
        p.x = -32000;
        p.y = -32000;
    }
    SetCaretPos(p.x, p.y);
}

void ReCreateCaret(RE *e)
{
    if (!e->focus)
        return;
    UINT width = 1;
    SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0);
    e->caret_h = caret_height(e);
    CreateCaret(e->hwnd, nullptr, (int)width, e->caret_h);
    RePlaceCaret(e);
    ShowCaret(e->hwnd);
}
