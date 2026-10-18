/*
 * input: typing and the editing keys, moving the caret by characters,
 * words, lines and pages, the mouse selecting characters, words and
 * lines with auto scrolling, the wheel, and the clipboard.
 */
#include "richedit_private.h"

static int imax(int a, int b) { return a > b ? a : b; }

static BOOL is_break(WCHAR c)
{
    return c == RE_CR || c == RE_VT || c == RE_FF;
}

static BOOL key_down(int vk) { return GetKeyState(vk) < 0; }

WCHAR *ReRangeText(RE *e, UINT from, UINT to, BOOL crlf, UINT *count)
{
    if (to > e->len)
        to = e->len;
    if (from > to)
        from = to;
    UINT n = to - from, extra = 0;
    if (crlf)
        for (UINT i = from; i < to; i++)
            extra += e->text[i] == RE_CR;
    WCHAR *s = ReAlloc(((SIZE_T)n + extra + 1) * sizeof(WCHAR));
    if (!s)
        return nullptr;
    UINT k = 0;
    for (UINT i = from; i < to; i++) {
        s[k++] = e->text[i];
        if (crlf && e->text[i] == RE_CR)
            s[k++] = '\n';
    }
    s[k] = 0;
    *count = k;
    return s;
}

/* ------------------------------------------------------------------ editing */

static void select_range(RE *e, UINT anchor, UINT active, BOOL at_end, BOOL keep_x)
{
    int x = e->caret_x;
    BOOL valid = e->caret_x_valid;
    ReSetSelection(e, anchor, active);
    e->at_line_end = at_end && anchor == active;
    if (keep_x) {
        e->caret_x = x;
        e->caret_x_valid = valid;
    }
    ReScrollCaret(e);
    RePlaceCaret(e);
}

BOOL ReInsertUser(RE *e, const WCHAR *s, UINT n, int kind)
{
    UINT from = ReSelMin(e), to = ReSelMax(e);
    if (to > e->len - 1)
        to = e->len - 1;
    UINT after = e->len - 1 - (to - from);
    if (after + n > e->limit) {
        UINT room = e->limit > after ? e->limit - after : 0;
        e->pend_maxtext = TRUE;
        if (e->edit_style & SES_BEEPONMAXTEXT)
            ReBeep(e);
        if (!room && n)
            return FALSE;
        n = room;
    }
    WCHAR *t = ReAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!t)
        return FALSE;
    memcpy(t, s, n * sizeof(WCHAR));
    if (e->edit_style & (SES_LOWERCASE | SES_UPPERCASE)) {
        if (e->edit_style & SES_LOWERCASE)
            CharLowerBuffW(t, n);
        else
            CharUpperBuffW(t, n);
    }
    WORD cf = ReInsertFormat(e);
    BOOL ok = ReReplaceText(e, from, to - from, t, n, cf, kind);
    ReFree(t);
    if (!ok) {
        e->pend_errspace = TRUE;
        return FALSE;
    }
    BOOL group = e->typing_group;
    select_range(e, from + n, from + n, FALSE, FALSE);
    e->typing_group = group;
    e->ins_cf = cf;
    e->ins_valid = kind == UNDO_TYPING;
    return TRUE;
}

BOOL ReDeleteSelection(RE *e, int kind)
{
    UINT from = ReSelMin(e), to = ReSelMax(e);
    if (to > e->len - 1)
        to = e->len - 1;
    if (from >= to)
        return FALSE;
    ReReplace(e, from, to - from, nullptr, 0, nullptr, nullptr, kind);
    BOOL group = e->typing_group;
    select_range(e, from, from, FALSE, FALSE);
    e->typing_group = group;
    return TRUE;
}

static UINT prev_char(RE *e, UINT cp)
{
    if (cp == 0)
        return 0;
    cp--;
    if (cp > 0 && e->text[cp] >= 0xDC00 && e->text[cp] <= 0xDFFF && e->text[cp - 1] >= 0xD800 &&
        e->text[cp - 1] <= 0xDBFF)
        cp--;
    return cp;
}

static UINT next_char(RE *e, UINT cp)
{
    if (cp >= e->len - 1)
        return e->len - 1;
    cp++;
    if (cp < e->len - 1 && e->text[cp] >= 0xDC00 && e->text[cp] <= 0xDFFF && e->text[cp - 1] >= 0xD800 &&
        e->text[cp - 1] <= 0xDBFF)
        cp++;
    return cp;
}

static void delete_range(RE *e, UINT from, UINT to, int kind)
{
    if (to > e->len - 1)
        to = e->len - 1;
    if (from >= to)
        return;
    BOOL group = e->typing_group;
    ReReplace(e, from, to - from, nullptr, 0, nullptr, nullptr, kind);
    e->typing_group = group || kind == UNDO_DELETE;
    group = e->typing_group;
    select_range(e, from, from, FALSE, FALSE);
    e->typing_group = group;
}

static void backspace(RE *e, BOOL word)
{
    if (e->anchor != e->active) {
        ReDeleteSelection(e, UNDO_DELETE);
        return;
    }
    UINT cp = e->active;
    if (!cp)
        return;
    UINT from = word ? ReWordBreak(e, cp, WB_MOVEWORDLEFT) : prev_char(e, cp);
    delete_range(e, from, cp, UNDO_DELETE);
}

static void delete_forward(RE *e, BOOL word)
{
    if (e->anchor != e->active) {
        ReDeleteSelection(e, UNDO_DELETE);
        return;
    }
    UINT cp = e->active;
    if (cp >= e->len - 1)
        return;
    UINT to = word ? ReWordBreak(e, cp, WB_MOVEWORDRIGHT) : next_char(e, cp);
    delete_range(e, cp, to, UNDO_DELETE);
}

void ReChar(RE *e, WCHAR c)
{
    if (c == 8 || c == 0x7F) {
        if (e->readonly) {
            ReBeep(e);
            return;
        }
        backspace(e, c == 0x7F || key_down(VK_CONTROL));
        return;
    }
    if (c == '\n' && !key_down(VK_CONTROL))
        return;
    if (c == '\n')
        c = '\r';
    if (c < 0x20 && c != '\t' && c != '\r')
        return;
    if (c == '\r' && !e->multiline) {
        ReBeep(e);
        return;
    }
    if (e->readonly) {
        ReBeep(e);
        return;
    }
    ReInsertUser(e, &c, 1, UNDO_TYPING);
}

/* ------------------------------------------------------------------ caret movement */

static int caret_x(RE *e)
{
    if (e->caret_x_valid)
        return e->caret_x;
    POINT p = RePosFromCp(e, e->active, e->at_line_end);
    e->caret_x = p.x + e->xscroll;
    e->caret_x_valid = TRUE;
    return e->caret_x;
}

static void move(RE *e, UINT cp, BOOL extend, BOOL at_end, BOOL keep_x)
{
    select_range(e, extend ? e->anchor : cp, cp, at_end, keep_x);
}

static void vertical(RE *e, int lines, BOOL extend)
{
    ReLayout(e);
    if (!e->line_count)
        return;
    int x = caret_x(e) - e->xscroll;
    int l = (int)ReLineOf(e, e->active, e->at_line_end) + lines;
    if (l < 0) {
        if (!e->multiline)
            return;
        move(e, 0, extend, FALSE, TRUE);
        return;
    }
    if (l >= (int)e->line_count) {
        move(e, e->len - 1, extend, FALSE, TRUE);
        return;
    }
    const RE_LINE *line = &e->lines[l];
    BOOL at_end;
    UINT cp = ReCpFromPoint(e, x, e->view.top - e->yscroll + line->y + line->height / 2, &at_end);
    move(e, cp, extend, at_end, TRUE);
}

static void line_edge(RE *e, BOOL end, BOOL extend)
{
    ReLayout(e);
    if (!e->line_count)
        return;
    const RE_LINE *line = &e->lines[ReLineOf(e, e->active, e->at_line_end)];
    if (!end) {
        move(e, line->cp, extend, FALSE, FALSE);
        return;
    }
    UINT stop = line->cp + line->len;
    if (stop > line->cp && is_break(e->text[stop - 1]))
        move(e, stop - 1, extend, FALSE, FALSE);
    else
        move(e, stop, extend, TRUE, FALSE);
}

static void page(RE *e, BOOL down, BOOL extend)
{
    ReLayout(e);
    int h = imax(e->view.bottom - e->view.top, 1);
    int x = caret_x(e) - e->xscroll;
    POINT p = RePosFromCp(e, e->active, e->at_line_end);
    ReVScroll(e, down ? SB_PAGEDOWN : SB_PAGEUP, 0);
    int y = p.y + (down ? h : -h);
    if (y < e->view.top)
        y = e->view.top;
    if (y >= e->view.bottom)
        y = e->view.bottom - 1;
    BOOL at_end;
    UINT cp = ReCpFromPoint(e, x, y, &at_end);
    if (!down && cp == e->active && e->yscroll == 0)
        cp = 0;
    move(e, cp, extend, at_end, TRUE);
}

static void toggle_effect(RE *e, DWORD effect)
{
    CHARFORMAT2W cf;
    ReRangeCharFormat(e, ReSelMin(e), ReSelMax(e), &cf);
    CHARFORMAT2W set;
    memset(&set, 0, sizeof(set));
    set.cbSize = sizeof(set);
    set.dwMask = effect;
    set.dwEffects = (cf.dwEffects & effect) ? 0 : effect;
    SendMessageW(e->hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&set);
}

static void align(RE *e, WORD alignment)
{
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = alignment;
    SendMessageW(e->hwnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void spacing(RE *e, BYTE rule)
{
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_LINESPACING;
    pf.bLineSpacingRule = rule;
    SendMessageW(e->hwnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

void ReKeyDown(RE *e, UINT vk)
{
    BOOL shift = key_down(VK_SHIFT), ctrl = key_down(VK_CONTROL), alt = key_down(VK_MENU);
    BOOL editable = !e->readonly;
    switch (vk) {
    case VK_LEFT:
        if (e->anchor != e->active && !shift)
            move(e, ReSelMin(e), FALSE, FALSE, FALSE);
        else
            move(e, ctrl ? ReWordBreak(e, e->active, WB_MOVEWORDLEFT) : prev_char(e, e->active), shift, FALSE, FALSE);
        return;
    case VK_RIGHT:
        if (e->anchor != e->active && !shift) {
            UINT to = ReSelMax(e);
            move(e, to > e->len - 1 ? e->len - 1 : to, FALSE, FALSE, FALSE);
        } else {
            move(e, ctrl ? ReWordBreak(e, e->active, WB_MOVEWORDRIGHT) : next_char(e, e->active), shift, FALSE,
                 FALSE);
        }
        return;
    case VK_UP:
        if (ctrl) {
            UINT cp = ReParaStart(e, e->active);
            if (cp == e->active && cp > 0)
                cp = ReParaStart(e, cp - 1);
            move(e, cp, shift, FALSE, FALSE);
        } else {
            vertical(e, -1, shift);
        }
        return;
    case VK_DOWN:
        if (ctrl) {
            UINT cp = ReParaEnd(e, e->active);
            move(e, cp < e->len - 1 ? cp + 1 : cp, shift, FALSE, FALSE);
        } else {
            vertical(e, 1, shift);
        }
        return;
    case VK_HOME:
        if (ctrl)
            move(e, 0, shift, FALSE, FALSE);
        else
            line_edge(e, FALSE, shift);
        return;
    case VK_END:
        if (ctrl)
            move(e, e->len - 1, shift, FALSE, FALSE);
        else
            line_edge(e, TRUE, shift);
        return;
    case VK_PRIOR:
    case VK_NEXT:
        page(e, vk == VK_NEXT, shift);
        return;
    case VK_DELETE:
        if (shift && !ctrl) {
            ReCut(e);
            return;
        }
        if (!editable) {
            ReBeep(e);
            return;
        }
        delete_forward(e, ctrl);
        return;
    case VK_INSERT:
        if (ctrl && !shift)
            ReCopy(e);
        else if (shift && !ctrl)
            RePaste(e, 0);
        return;
    }
    if (!ctrl || alt)
        return;
    switch (vk) {
    case 'A':
        select_range(e, 0, e->len, FALSE, FALSE);
        return;
    case 'C':
        ReCopy(e);
        return;
    case 'X':
        ReCut(e);
        return;
    case 'V':
        RePaste(e, 0);
        return;
    case 'Z':
        SendMessageW(e->hwnd, EM_UNDO, 0, 0);
        return;
    case 'Y':
        SendMessageW(e->hwnd, EM_REDO, 0, 0);
        return;
    }
    if (!editable || e->plain)
        return;
    switch (vk) {
    case 'B':
        toggle_effect(e, CFE_BOLD);
        return;
    case 'I':
        toggle_effect(e, CFE_ITALIC);
        return;
    case 'U':
        toggle_effect(e, CFE_UNDERLINE);
        return;
    case 'L':
        align(e, PFA_LEFT);
        return;
    case 'E':
        align(e, PFA_CENTER);
        return;
    case 'R':
        align(e, PFA_RIGHT);
        return;
    case 'J':
        align(e, PFA_JUSTIFY);
        return;
    case '1':
        spacing(e, 0);
        return;
    case '2':
        spacing(e, 2);
        return;
    case '5':
        spacing(e, 1);
        return;
    }
}

/* ------------------------------------------------------------------ the mouse */

enum { SELECT_CHARS, SELECT_WORDS, SELECT_LINES };

static void word_span(RE *e, UINT cp, UINT *from, UINT *to)
{
    UINT a = cp, b = cp;
    if (cp < e->len - 1 && ReIsWordChar(e->text[cp])) {
        while (a > 0 && ReIsWordChar(e->text[a - 1]))
            a--;
        while (b < e->len - 1 && ReIsWordChar(e->text[b]))
            b++;
        while (b < e->len - 1 && (e->text[b] == ' ' || e->text[b] == '\t'))
            b++;
    } else if (cp < e->len - 1 && !is_break(e->text[cp])) {
        b = cp + 1;
    }
    *from = a;
    *to = b;
}

static void line_span(RE *e, int y, UINT *from, UINT *to)
{
    BOOL at_end;
    UINT cp = ReCpFromPoint(e, e->view.left, y, &at_end);
    const RE_LINE *line = &e->lines[ReLineOf(e, cp, FALSE)];
    *from = line->cp;
    *to = line->cp + line->len;
}

void ReMouseDown(RE *e, int x, int y, UINT keys, BOOL dbl)
{
    ReLayout(e);
    if (!e->line_count)
        return;
    BOOL at_end;
    UINT cp = ReCpFromPoint(e, x, y, &at_end);
    BOOL shift = (keys & MK_SHIFT) != 0;
    if (e->selbar && x < e->view.left) {
        UINT a, b;
        line_span(e, y, &a, &b);
        if (dbl) {
            a = ReParaStart(e, a);
            b = ReParaEnd(e, a) + 1;
        }
        e->select_mode = SELECT_LINES;
        e->word_min = a;
        e->word_max = b;
        select_range(e, a, b, FALSE, FALSE);
    } else if (dbl) {
        UINT a, b;
        word_span(e, cp, &a, &b);
        e->select_mode = SELECT_WORDS;
        e->word_min = a;
        e->word_max = b;
        select_range(e, a, b, FALSE, FALSE);
    } else if (shift) {
        e->select_mode = SELECT_CHARS;
        select_range(e, e->anchor, cp, at_end, FALSE);
    } else {
        e->select_mode = SELECT_CHARS;
        select_range(e, cp, cp, at_end, FALSE);
    }
    e->tracking = TRUE;
    e->last_mouse.x = x;
    e->last_mouse.y = y;
    SetCapture(e->hwnd);
    SetTimer(e->hwnd, RE_SCROLL_TIMER, 100, nullptr);
}

static void drag_to(RE *e, int x, int y)
{
    BOOL at_end;
    UINT cp = ReCpFromPoint(e, x, y, &at_end);
    if (e->select_mode == SELECT_CHARS) {
        select_range(e, e->anchor, cp, at_end, FALSE);
        return;
    }
    UINT a, b;
    if (e->select_mode == SELECT_WORDS)
        word_span(e, cp, &a, &b);
    else
        line_span(e, y, &a, &b);
    if (a < e->word_min)
        select_range(e, e->word_max, a, FALSE, FALSE);
    else
        select_range(e, e->word_min, b > e->word_max ? b : e->word_max, FALSE, FALSE);
}

void ReMouseMove(RE *e, int x, int y, UINT keys)
{
    (void)keys;
    if (!e->tracking)
        return;
    e->last_mouse.x = x;
    e->last_mouse.y = y;
    drag_to(e, x, y);
}

void ReMouseUp(RE *e, int x, int y)
{
    (void)x;
    (void)y;
    if (!e->tracking)
        return;
    e->tracking = FALSE;
    KillTimer(e->hwnd, RE_SCROLL_TIMER);
    if (GetCapture() == e->hwnd)
        ReleaseCapture();
}

void ReMouseTimer(RE *e)
{
    if (!e->tracking)
        return;
    int x = e->last_mouse.x, y = e->last_mouse.y;
    if (y < e->view.top)
        ReVScroll(e, SB_LINEUP, 0);
    else if (y >= e->view.bottom)
        ReVScroll(e, SB_LINEDOWN, 0);
    else if (x < e->view.left || x >= e->view.right)
        ReHScroll(e, x < e->view.left ? SB_LINELEFT : SB_LINERIGHT, 0);
    else
        return;
    drag_to(e, x, y);
}

/* the wheel: ctrl zooms, shift does nothing, otherwise lines by whole notches. */
LRESULT ReWheel(RE *e, WPARAM wparam)
{
    UINT keys = LOWORD(wparam);
    int delta = (short)HIWORD(wparam);
    if (keys & MK_CONTROL) {
        if (!e->multiline)
            return 0;
        int zoom = (e->zoom_num > 0 && e->zoom_den > 0) ? MulDiv(100, e->zoom_num, e->zoom_den) : 100;
        int next = zoom + 10 * (delta / 120);
        if (next >= 10 && next <= 500) {
            e->zoom_num = next;
            e->zoom_den = 100;
            ReFlushFonts(e);
            ReInvalidateLayout(e);
            ReUpdateView(e);
        }
        return 0;
    }
    if (keys & MK_SHIFT)
        return 0;
    e->wheel += delta;
    int magnitude = e->wheel < 0 ? -e->wheel : e->wheel;
    if (magnitude < 120)
        return 0;
    UINT per = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &per, 0);
    int notches = magnitude / 120;
    BOOL up = e->wheel > 0;
    e->wheel -= (up ? 120 : -120) * notches;
    int code;
    if (per == WHEEL_PAGESCROLL) {
        code = up ? SB_PAGEUP : SB_PAGEDOWN;
        ReVScroll(e, code, 0);
    } else {
        code = up ? SB_LINEUP : SB_LINEDOWN;
        for (UINT i = 0; i < per * (UINT)notches; i++)
            ReVScroll(e, code, 0);
    }
    if (e->mask & ENM_SCROLLEVENTS) {
        MSGFILTER mf;
        memset(&mf, 0, sizeof(mf));
        mf.msg = WM_VSCROLL;
        mf.wParam = (WPARAM)code;
        ReNotify(e, EN_MSGFILTER, &mf.nmhdr);
    }
    return 1;
}

/* ------------------------------------------------------------------ the clipboard */

static UINT rtf_format(void)
{
    static UINT format;
    if (!format)
        format = RegisterClipboardFormatW(L"Rich Text Format");
    return format;
}

static HGLOBAL global_copy(const void *data, SIZE_T size)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!h)
        return nullptr;
    void *p = GlobalLock(h);
    if (p)
        memcpy(p, data, size);
    GlobalUnlock(h);
    return h;
}

BOOL ReCopy(RE *e)
{
    UINT from = ReSelMin(e), to = ReSelMax(e);
    if (from == to || e->password)
        return FALSE;
    UINT n = 0;
    WCHAR *s = ReRangeText(e, from, to > e->len - 1 ? e->len - 1 : to, TRUE, &n);
    if (!s)
        return FALSE;
    if (!OpenClipboard(e->hwnd)) {
        ReFree(s);
        return FALSE;
    }
    EmptyClipboard();
    HGLOBAL h = global_copy(s, ((SIZE_T)n + 1) * sizeof(WCHAR));
    if (h && !SetClipboardData(CF_UNICODETEXT, h))
        GlobalFree(h);
    ReFree(s);
    if (!e->plain) {
        UINT size = 0;
        char *rtf = ReWriteRtf(e, from, to, &size);
        if (rtf) {
            h = global_copy(rtf, size + 1);
            if (h && !SetClipboardData(rtf_format(), h))
                GlobalFree(h);
            ReFree(rtf);
        }
    }
    CloseClipboard();
    return TRUE;
}

BOOL ReCut(RE *e)
{
    if (e->readonly || e->password) {
        ReBeep(e);
        return FALSE;
    }
    if (!ReCopy(e))
        return FALSE;
    return ReDeleteSelection(e, UNDO_CUT);
}

BOOL ReCanPaste(RE *e, UINT format)
{
    if (e->readonly)
        return FALSE;
    if (format)
        return IsClipboardFormatAvailable(format);
    return IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT) ||
           (!e->plain && IsClipboardFormatAvailable(rtf_format()));
}

BOOL RePaste(RE *e, UINT format)
{
    if (e->readonly) {
        ReBeep(e);
        return FALSE;
    }
    if (!OpenClipboard(e->hwnd))
        return FALSE;
    BOOL done = FALSE;
    UINT rtf = rtf_format();
    if (!e->plain && (!format || format == rtf) && IsClipboardFormatAvailable(rtf)) {
        HGLOBAL h = GetClipboardData(rtf);
        const char *p = h ? GlobalLock(h) : nullptr;
        if (p) {
            UINT from = ReSelMin(e), to = ReSelMax(e);
            if (to > e->len - 1)
                to = e->len - 1;
            LONG error = 0;
            LONG n = ReReadRtf(e, p, (UINT)GlobalSize(h), from, to - from, &error);
            GlobalUnlock(h);
            ReSetSelection(e, from + (UINT)n, from + (UINT)n);
            done = TRUE;
        }
    }
    if (!done && (!format || format == CF_UNICODETEXT || format == CF_TEXT)) {
        HGLOBAL h = GetClipboardData(CF_UNICODETEXT);
        const WCHAR *p = h ? GlobalLock(h) : nullptr;
        if (p) {
            UINT n = (UINT)lstrlenW(p);
            WCHAR *s = ReAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
            if (s) {
                UINT k = 0;
                for (UINT i = 0; i < n; i++) {
                    WCHAR c = p[i];
                    if (c == '\r' && i + 1 < n && p[i + 1] == '\n')
                        i++;
                    if (c == '\n')
                        c = '\r';
                    if (c == '\r' && !e->multiline)
                        break;
                    s[k++] = c;
                }
                ReInsertUser(e, s, k, UNDO_PASTE);
                ReFree(s);
            }
            GlobalUnlock(h);
            done = TRUE;
        }
    }
    CloseClipboard();
    return done;
}
