/*
 * layout: the fonts of the formats, breaking paragraphs into lines at
 * the view's width, positions of characters and their reverse, the
 * scroll bars and scrolling, and EN_REQUESTRESIZE.
 */
#include "richedit_private.h"

static int imax(int a, int b) { return a > b ? a : b; }

static int imin(int a, int b) { return a < b ? a : b; }

static int zoom(RE *e, int v)
{
    if (e->zoom_num > 0 && e->zoom_den > 0)
        return MulDiv(v, e->zoom_num, e->zoom_den);
    return v;
}

int ReTwipsToPixels(RE *e, LONG twips)
{
    return zoom(e, MulDiv(twips, e->dpi, 1440));
}

int ReTwipsX(RE *e, LONG twips)
{
    return ReTwipsToPixels(e, twips);
}

void ReFlushFonts(RE *e)
{
    for (UINT i = 0; i < e->fmt_count; i++) {
        if (e->fonts[i].font)
            DeleteObject(e->fonts[i].font);
        memset(&e->fonts[i], 0, sizeof(RE_FONT));
    }
}

RE_FONT *ReFont(RE *e, WORD k)
{
    if (k >= e->fmt_count)
        k = 0;
    RE_FONT *f = &e->fonts[k];
    if (f->font)
        return f;
    const CHARFORMAT2W *cf = &e->fmts[k].cf;
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    int height = ReTwipsToPixels(e, cf->yHeight);
    if (cf->dwEffects & (CFE_SUBSCRIPT | CFE_SUPERSCRIPT))
        height = height * 2 / 3;
    lf.lfHeight = -imax(height, 1);
    lf.lfWeight = cf->wWeight ? cf->wWeight : FW_NORMAL;
    if ((cf->dwEffects & CFE_BOLD) && lf.lfWeight < FW_BOLD)
        lf.lfWeight = FW_BOLD;
    if (!(cf->dwEffects & CFE_BOLD) && lf.lfWeight >= FW_BOLD)
        lf.lfWeight = FW_NORMAL;
    lf.lfItalic = (cf->dwEffects & CFE_ITALIC) ? 1 : 0;
    lf.lfUnderline = ((cf->dwEffects & CFE_UNDERLINE) && cf->bUnderlineType != CFU_UNDERLINENONE) ||
                     (cf->dwEffects & CFE_LINK) || e->fmts[k].autolink;
    lf.lfStrikeOut = (cf->dwEffects & CFE_STRIKEOUT) ? 1 : 0;
    lf.lfCharSet = cf->bCharSet;
    lf.lfPitchAndFamily = cf->bPitchAndFamily;
    lf.lfQuality = DEFAULT_QUALITY;
    memcpy(lf.lfFaceName, cf->szFaceName, sizeof(lf.lfFaceName));
    f->font = CreateFontIndirectW(&lf);
    if (!f->font)
        f->font = (HFONT)GetStockObject(SYSTEM_FONT);
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, f->font);
    TEXTMETRICW tm;
    if (GetTextMetricsW(dc, &tm)) {
        f->ascent = tm.tmAscent;
        f->descent = tm.tmDescent;
        f->ave = tm.tmAveCharWidth;
    } else {
        f->ascent = imax(height, 1);
        f->descent = imax(height / 4, 1);
        f->ave = imax(height / 2, 1);
    }
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    f->offset = ReTwipsToPixels(e, cf->yOffset);
    if (cf->dwEffects & CFE_SUPERSCRIPT)
        f->offset += f->ascent / 2;
    if (cf->dwEffects & CFE_SUBSCRIPT)
        f->offset -= f->ascent / 3;
    return f;
}

void ReInvalidateLayout(RE *e)
{
    e->dirty = TRUE;
}

/* ------------------------------------------------------------------ view */

void ReComputeView(RE *e)
{
    RECT rc;
    GetClientRect(e->hwnd, &rc);
    if (e->rect_set) {
        e->view = e->fmt_rect;
        return;
    }
    int bar = e->selbar ? MulDiv(8, e->dpi, 96) : 0;
    e->view.left = rc.left + e->inset_l + e->margin_l + bar;
    e->view.top = rc.top + e->inset_t;
    e->view.right = rc.right - e->inset_r - e->margin_r;
    e->view.bottom = rc.bottom - e->inset_b;
    if (e->view.right < e->view.left)
        e->view.right = e->view.left;
    if (e->view.bottom < e->view.top)
        e->view.bottom = e->view.top;
}

static int view_width(RE *e) { return e->view.right - e->view.left; }

static int view_height(RE *e) { return e->view.bottom - e->view.top; }

/* ------------------------------------------------------------------ lines */

static BOOL add_line(RE *e, const RE_LINE *line)
{
    if (e->line_count == e->line_cap) {
        UINT cap = e->line_cap ? e->line_cap * 2 : 64;
        RE_LINE *lines = ReRealloc(e->lines, cap * sizeof(RE_LINE));
        if (!lines)
            return FALSE;
        e->lines = lines;
        e->line_cap = cap;
    }
    e->lines[e->line_count++] = *line;
    return TRUE;
}

static int next_tab(RE *e, const PARAFORMAT2 *pf, int x)
{
    for (int i = 0; i < pf->cTabCount; i++) {
        int t = ReTwipsX(e, pf->rgxTabs[i] & 0x00FFFFFF);
        if (t > x)
            return t;
    }
    int step = ReTwipsX(e, lDefaultTab);
    if (step <= 0)
        step = 1;
    return (x / step + 1) * step;
}

static BOOL is_break(WCHAR c)
{
    return c == RE_CR || c == RE_VT || c == RE_FF;
}

static BOOL breaks_after(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '-' || c == 0x3000 || (c >= 0x2E80 && c <= 0x9FFF) ||
           (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF);
}

static void roman(int n, BOOL upper, WCHAR *out, UINT cap)
{
    static const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *const names[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    UINT k = 0;
    if (n <= 0)
        n = 1;
    for (int i = 0; i < 13 && k + 3 < cap; i++)
        while (n >= values[i] && k + 3 < cap) {
            for (const char *p = names[i]; *p; p++)
                out[k++] = (WCHAR)(upper ? *p - 32 : *p);
            n -= values[i];
        }
    out[k] = 0;
}

/* the bullet or number shown on a paragraph's first line. */
void ReBulletText(RE *e, const PARAFORMAT2 *pf, int number, WCHAR *out, UINT cap)
{
    (void)e;
    out[0] = 0;
    if (cap < 16)
        return;
    WCHAR body[12];
    body[0] = 0;
    switch (pf->wNumbering) {
    case PFN_BULLET:
        out[0] = 0x2022;
        out[1] = 0;
        return;
    case PFN_ARABIC:
        wsprintfW(body, L"%d", number);
        break;
    case PFN_LCLETTER:
    case PFN_UCLETTER:
        body[0] = (WCHAR)((pf->wNumbering == PFN_LCLETTER ? 'a' : 'A') + (number - 1) % 26);
        body[1] = 0;
        break;
    case PFN_LCROMAN:
    case PFN_UCROMAN:
        roman(number, pf->wNumbering == PFN_UCROMAN, body, 12);
        break;
    default:
        return;
    }
    switch (pf->wNumberingStyle & 0x0F00) {
    case PFNS_PARENS:
        wsprintfW(out, L"(%s)", body);
        break;
    case PFNS_PERIOD:
        wsprintfW(out, L"%s.", body);
        break;
    case PFNS_PLAIN:
        wsprintfW(out, L"%s", body);
        break;
    case PFNS_NONUMBER:
        out[0] = 0;
        break;
    default:
        wsprintfW(out, L"%s)", body);
        break;
    }
}

static int text_width(HDC dc, HFONT font, const WCHAR *s, int n)
{
    if (n <= 0)
        return 0;
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc, s, n, &size);
    SelectObject(dc, old);
    return size.cx;
}

static BOOL ensure_xpos(RE *e)
{
    if (e->len + 1 <= e->xpos_cap)
        return TRUE;
    UINT cap = e->len + 64;
    int *xpos = ReRealloc(e->xpos, cap * sizeof(int));
    if (!xpos)
        return FALSE;
    e->xpos = xpos;
    e->xpos_cap = cap;
    return TRUE;
}

/* lays out [ps, pe] from y, pe being the paragraph mark. */
static void layout_paragraph(RE *e, HDC dc, UINT ps, UINT pe, int *y, int *number, WORD *last_numbering)
{
    const PARAFORMAT2 *pf = &e->paras[e->pfi[pe]];
    BOOL wrap = e->multiline && e->wrap && e->target >= 0;
    int width = view_width(e);
    if (e->target > 0)
        width = ReTwipsX(e, e->target);
    int right = ReTwipsX(e, pf->dxRightIndent);
    int first = ReTwipsX(e, pf->dxStartIndent);
    int rest = ReTwipsX(e, pf->dxStartIndent + pf->dxOffset);
    int before = ReTwipsToPixels(e, pf->dySpaceBefore), after = ReTwipsToPixels(e, pf->dySpaceAfter);
    int bullet_x = first, text_x = first;
    int num = 0;
    if (pf->wNumbering && e->multiline) {
        if (pf->wNumbering == *last_numbering && !(pf->wNumberingStyle & PFNS_NEWNUMBER))
            (*number)++;
        else
            *number = pf->wNumberingStart ? pf->wNumberingStart : 1;
        num = *number;
        WCHAR label[32];
        ReBulletText(e, pf, num, label, 32);
        RE_FONT *f = ReFont(e, e->cfi[pe]);
        int w = text_width(dc, f->font, label, lstrlenW(label));
        int gap = pf->wNumberingTab ? ReTwipsX(e, pf->wNumberingTab) : imax(ReTwipsX(e, pf->dxOffset), 0);
        if (gap < w + f->ave)
            gap = w + f->ave;
        text_x = first + gap;
        if (rest < text_x)
            rest = text_x;
    }
    *last_numbering = pf->wNumbering;
    int limit = wrap ? width - right : 0x3FFFFFFF;
    UINT cp = ps;
    BOOL first_line = TRUE;
    int ext[129];
    for (;;) {
        UINT start = cp;
        int x0 = first_line ? text_x : rest;
        int x = x0;
        UINT end = pe + 1, last_break = 0;
        UINT i = cp;
        while (i <= pe) {
            WCHAR c = e->text[i];
            if (c == RE_CR || i == pe) {
                e->xpos[i] = x;
                end = i + 1;
                break;
            }
            if (c == RE_VT || c == RE_FF) {
                e->xpos[i] = x;
                end = i + 1;
                break;
            }
            if (c == '\t' && !e->password) {
                e->xpos[i] = x;
                x = next_tab(e, pf, x);
                last_break = i + 1;
                i++;
                continue;
            }
            UINT j = i;
            WORD k = e->cfi[i];
            while (j < pe && j - i < 128 && e->cfi[j] == k && !is_break(e->text[j]) && e->text[j] != '\t')
                j++;
            RE_FONT *f = ReFont(e, k);
            HGDIOBJ old = SelectObject(dc, f->font);
            SIZE size;
            int n = (int)(j - i);
            if (e->password) {
                int w = 0;
                GetTextExtentPoint32W(dc, &e->password, 1, &size);
                w = size.cx;
                for (int m = 0; m < n; m++)
                    ext[m] = w * (m + 1);
            } else if (!GetTextExtentExPointW(dc, e->text + i, n, 0, nullptr, ext, &size)) {
                for (int m = 0; m < n; m++)
                    ext[m] = f->ave * (m + 1);
            }
            SelectObject(dc, old);
            BOOL overflow = FALSE;
            for (int m = 0; m < n; m++) {
                UINT at = i + (UINT)m;
                WCHAR ch = e->text[at];
                e->xpos[at] = x + (m ? ext[m - 1] : 0);
                int next = x + ext[m];
                if (wrap && next > limit && ch != ' ' && at > start) {
                    end = last_break > start ? last_break : at;
                    overflow = TRUE;
                    break;
                }
                if (breaks_after(ch))
                    last_break = at + 1;
            }
            if (overflow)
                break;
            x += ext[n - 1];
            i = j;
        }
        if (end > pe + 1)
            end = pe + 1;
        if (end <= start)
            end = start + 1;
        /* the line is as tall as the fonts on it. */
        int ascent = 0, descent = 0;
        UINT stop = end;
        for (UINT m = start; m < stop; m++) {
            if (m != start && e->cfi[m] == e->cfi[m - 1])
                continue;
            if (is_break(e->text[m]) && m != start)
                continue;
            RE_FONT *f = ReFont(e, e->cfi[m]);
            ascent = imax(ascent, f->ascent + imax(f->offset, 0));
            descent = imax(descent, f->descent - imin(f->offset, 0));
        }
        int height = ascent + descent;
        switch (pf->bLineSpacingRule) {
        case 1:
            height = height * 3 / 2;
            break;
        case 2:
            height *= 2;
            break;
        case 3:
            height = imax(height, ReTwipsToPixels(e, pf->dyLineSpacing));
            break;
        case 4:
            height = ReTwipsToPixels(e, pf->dyLineSpacing);
            break;
        case 5:
            height = height * pf->dyLineSpacing / 20;
            break;
        }
        if (height <= 0)
            height = 1;
        int top = first_line ? before : 0;
        BOOL last = end > pe;
        /* trailing spaces hang past the margin. */
        UINT visible = end;
        while (visible > start && (is_break(e->text[visible - 1]) || e->text[visible - 1] == ' '))
            visible--;
        int w = visible > start ? e->xpos[visible] - x0 : 0;
        RE_LINE line;
        memset(&line, 0, sizeof(line));
        line.cp = start;
        line.len = end - start;
        line.y = *y;
        line.height = top + height + (last ? after : 0);
        line.ascent = top + ascent + (height - ascent - descent) / 2;
        line.width = w + x0;
        line.flags = (first_line ? LINE_PARA_START : 0) | (last ? LINE_PARA_END : 0);
        line.number = first_line ? num : 0;
        line.bullet_x = bullet_x;
        int room = (wrap ? limit : imax(width - right, x0 + w)) - (x0 + w);
        if (pf->wAlignment == PFA_RIGHT && room > 0)
            line.x = room;
        else if (pf->wAlignment == PFA_CENTER && room > 0)
            line.x = room / 2;
        if (!add_line(e, &line))
            return;
        *y += line.height;
        if (line.x + line.width > e->content_width)
            e->content_width = line.x + line.width;
        first_line = FALSE;
        cp = end;
        if (cp > pe)
            break;
    }
}

static void layout_pass(RE *e)
{
    e->line_count = 0;
    e->content_width = 0;
    if (!ensure_xpos(e))
        return;
    HDC dc = GetDC(e->hwnd);
    int y = 0, number = 0;
    WORD numbering = 0;
    UINT ps = 0;
    while (ps < e->len) {
        UINT pe = ReParaEnd(e, ps);
        if (!e->multiline)
            pe = e->len - 1;
        layout_paragraph(e, dc, ps, pe, &y, &number, &numbering);
        ps = pe + 1;
    }
    e->xpos[e->len] = e->len ? e->xpos[e->len - 1] : 0;
    ReleaseDC(e->hwnd, dc);
    e->content_height = y;
}

/* windows shows the scroll bars only when the text needs them, unless ES_DISABLENOSCROLL keeps them. */
static BOOL fit_bars(RE *e)
{
    if (!e->multiline)
        return FALSE;
    BOOL changed = FALSE;
    BOOL keep = (e->style & ES_DISABLENOSCROLL) != 0;
    if (e->style & WS_VSCROLL) {
        BOOL need = keep || e->content_height > view_height(e);
        if (need != e->vbar) {
            e->vbar = need;
            ShowScrollBar(e->hwnd, SB_VERT, need);
            changed = TRUE;
        }
    }
    if (e->style & WS_HSCROLL) {
        BOOL need = keep || e->content_width > view_width(e);
        if (need != e->hbar) {
            e->hbar = need;
            ShowScrollBar(e->hwnd, SB_HORZ, need);
            changed = TRUE;
        }
    }
    return changed;
}

void ReLayout(RE *e)
{
    if (!e->dirty || e->laying_out)
        return;
    e->laying_out = TRUE;
    for (int pass = 0; pass < 3; pass++) {
        e->dirty = FALSE;
        ReComputeView(e);
        layout_pass(e);
        if (!fit_bars(e))
            break;
    }
    e->dirty = FALSE;
    e->laying_out = FALSE;
    int max_y = imax(e->content_height - view_height(e), 0);
    if (e->yscroll > max_y)
        e->yscroll = max_y;
    int max_x = imax(e->content_width - view_width(e), 0);
    if (e->xscroll > max_x)
        e->xscroll = max_x;
    ReUpdateScrollBars(e);
    ReRequestResize(e, FALSE);
}

void ReUpdateView(RE *e)
{
    ReLayout(e);
    ReInvalidate(e);
    RePlaceCaret(e);
}

/* ------------------------------------------------------------------ positions */

UINT ReLineOf(RE *e, UINT cp, BOOL at_end)
{
    ReLayout(e);
    if (!e->line_count)
        return 0;
    UINT lo = 0, hi = e->line_count - 1;
    if (cp >= e->len)
        return hi;
    while (lo < hi) {
        UINT mid = (lo + hi + 1) / 2;
        if (e->lines[mid].cp <= cp)
            lo = mid;
        else
            hi = mid - 1;
    }
    if (at_end && lo > 0 && cp == e->lines[lo].cp) {
        const RE_LINE *prev = &e->lines[lo - 1];
        if (!is_break(e->text[prev->cp + prev->len - 1]))
            lo--;
    }
    return lo;
}

/* the top left of the character, in client coordinates. */
POINT RePosFromCp(RE *e, UINT cp, BOOL at_end)
{
    ReLayout(e);
    POINT p = {e->view.left, e->view.top};
    if (!e->line_count)
        return p;
    if (cp > e->len)
        cp = e->len;
    UINT l = ReLineOf(e, cp, at_end);
    const RE_LINE *line = &e->lines[l];
    int x;
    if (cp >= line->cp + line->len)
        x = line->width;
    else
        x = e->xpos[cp];
    if (cp >= e->len)
        x = e->xpos[e->len - 1];
    p.x = e->view.left - e->xscroll + line->x + x;
    p.y = e->view.top - e->yscroll + line->y;
    return p;
}

UINT ReCpFromPoint(RE *e, int x, int y, BOOL *at_end)
{
    ReLayout(e);
    if (at_end)
        *at_end = FALSE;
    if (!e->line_count)
        return 0;
    int vy = y - e->view.top + e->yscroll;
    UINT l = 0;
    while (l + 1 < e->line_count && e->lines[l + 1].y <= vy)
        l++;
    const RE_LINE *line = &e->lines[l];
    int vx = x - e->view.left + e->xscroll - line->x;
    UINT end = line->cp + line->len;
    for (UINT k = line->cp; k < end; k++) {
        WCHAR c = e->text[k];
        if (is_break(c))
            return k;
        int left = e->xpos[k];
        int right = k + 1 < end ? e->xpos[k + 1] : imax(line->width, left);
        if (vx < (left + right) / 2)
            return k;
    }
    if (end > 0 && is_break(e->text[end - 1]))
        return end - 1;
    if (at_end)
        *at_end = TRUE;
    return end;
}

/* ------------------------------------------------------------------ scrolling */

void ReUpdateScrollBars(RE *e)
{
    if (!e->multiline)
        return;
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    if (e->vbar) {
        si.nMin = 0;
        si.nMax = imax(e->content_height - 1, 0);
        si.nPage = (UINT)imax(view_height(e), 0);
        si.nPos = e->yscroll;
        SetScrollInfo(e->hwnd, SB_VERT, &si, e->redraw);
    }
    if (e->hbar) {
        si.nMin = 0;
        si.nMax = imax(e->content_width - 1, 0);
        si.nPage = (UINT)imax(view_width(e), 0);
        si.nPos = e->xscroll;
        SetScrollInfo(e->hwnd, SB_HORZ, &si, e->redraw);
    }
}

void ReScrollTo(RE *e, int x, int y)
{
    ReLayout(e);
    int max_y = imax(e->content_height - view_height(e), 0);
    int max_x = imax(e->content_width - view_width(e), 0);
    if (!e->multiline) {
        max_y = 0;
        max_x = imax(e->content_width - view_width(e) + 1, 0);
    }
    x = imax(0, imin(x, max_x));
    y = imax(0, imin(y, max_y));
    BOOL vertical = y != e->yscroll, horizontal = x != e->xscroll;
    if (!vertical && !horizontal)
        return;
    e->yscroll = y;
    e->xscroll = x;
    ReUpdateScrollBars(e);
    ReInvalidate(e);
    RePlaceCaret(e);
    if ((e->mask & ENM_SCROLL) && vertical)
        ReNotifyCommand(e, EN_VSCROLL);
    if ((e->mask & ENM_SCROLL) && horizontal)
        ReNotifyCommand(e, EN_HSCROLL);
}

static UINT top_line(RE *e)
{
    UINT l = 0;
    while (l + 1 < e->line_count && e->lines[l + 1].y <= e->yscroll)
        l++;
    return l;
}

/* returns what EM_SCROLL reports: the lines moved and TRUE. */
LRESULT ReVScroll(RE *e, int code, int pos)
{
    ReLayout(e);
    if (!e->multiline)
        return 0;
    UINT before = top_line(e);
    int y = e->yscroll;
    UINT l = before;
    switch (code) {
    case SB_LINEUP:
        if (l > 0 && e->lines[l].y == y)
            l--;
        y = e->line_count ? e->lines[l].y : 0;
        break;
    case SB_LINEDOWN:
        y = l + 1 < e->line_count ? e->lines[l + 1].y : y;
        break;
    case SB_PAGEUP:
        y -= imax(view_height(e) - (e->line_count ? e->lines[l].height : 0), 1);
        break;
    case SB_PAGEDOWN:
        y += imax(view_height(e) - (e->line_count ? e->lines[l].height : 0), 1);
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        y = pos;
        break;
    case SB_TOP:
        y = 0;
        break;
    case SB_BOTTOM:
        y = e->content_height;
        break;
    default:
        return 0;
    }
    ReScrollTo(e, e->xscroll, y);
    int moved = (int)top_line(e) - (int)before;
    return MAKELRESULT((WORD)(SHORT)moved, TRUE);
}

void ReHScroll(RE *e, int code, int pos)
{
    ReLayout(e);
    int x = e->xscroll;
    int step = imax(MulDiv(e->dpi, 50, 100) / 4, 8);
    switch (code) {
    case SB_LINELEFT:
        x -= step;
        break;
    case SB_LINERIGHT:
        x += step;
        break;
    case SB_PAGELEFT:
        x -= view_width(e);
        break;
    case SB_PAGERIGHT:
        x += view_width(e);
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        x = pos;
        break;
    case SB_LEFT:
        x = 0;
        break;
    case SB_RIGHT:
        x = e->content_width;
        break;
    default:
        return;
    }
    ReScrollTo(e, x, e->yscroll);
}

/* keeps the caret in view, as auto scrolling does. */
void ReScrollCaret(RE *e)
{
    ReLayout(e);
    if (!e->line_count)
        return;
    UINT l = ReLineOf(e, e->active, e->at_line_end);
    const RE_LINE *line = &e->lines[l];
    int y = e->yscroll, x = e->xscroll;
    int vh = view_height(e), vw = view_width(e);
    if (e->multiline) {
        if (line->y < y)
            y = line->y;
        else if (line->y + line->height > y + vh)
            y = line->y + line->height - vh;
    }
    POINT p = RePosFromCp(e, e->active, e->at_line_end);
    int cx = p.x - e->view.left + e->xscroll;
    if (cx < x)
        x = imax(cx - vw / 3, 0);
    else if (cx >= x + vw)
        x = cx - vw + imax(vw / 3, 1);
    if (e->wrap && e->multiline && e->target >= 0 && e->content_width <= vw)
        x = 0;
    ReScrollTo(e, x, y);
}

/* sent when the size the text needs changed or EM_REQUESTRESIZE asks. */
void ReRequestResize(RE *e, BOOL force)
{
    if (!(e->mask & ENM_REQUESTRESIZE))
        return;
    int bar = e->selbar ? MulDiv(8, e->dpi, 96) : 0;
    int w = e->content_width + 1 + e->inset_l + e->inset_r + e->margin_l + e->margin_r + bar;
    int h = e->content_height;
    if (!force && w == e->last_req_w && h == e->last_req_h)
        return;
    e->last_req_w = w;
    e->last_req_h = h;
    REQRESIZE rr;
    memset(&rr, 0, sizeof(rr));
    rr.rc.right = w;
    rr.rc.bottom = h;
    ReNotify(e, EN_REQUESTRESIZE, &rr.nmhdr);
}
