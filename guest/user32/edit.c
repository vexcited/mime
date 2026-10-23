/*
 * the Edit control: single and multiline text, selection, caret, undo, clipboard, scrolling, keyboard and mouse, with
 * notifications in windows' order. the text is utf-16 in the moveable local block EM_GETHANDLE returns. multiline
 * edits keep a line table rebuilt after each change.
 */
#include "user32_private.h"
#include "listbox.h"

#define DEFAULT_LIMIT 30000
#define LONGEST_LINE 1024
#define SCROLL_TIMER 0xFFFE
#define PARKED (-20000)
#define CBEC_KILLCOMBOFOCUS 0x0167

enum { UNDO_INSERT = 1, UNDO_DELETE = 2 };

typedef struct LINE {
    UINT start, end;
    int width;
} LINE;

typedef struct EDIT {
    HWND hwnd, parent;
    UINT id;
    HLOCAL handle;
    UINT capacity, len, limit;
    BOOL multi, readonly, wrap, auto_h, auto_v, nohidesel, focus, modified, disabled, in_dialog, combo;
    BOOL border, flat, rtl, fmt_valid, redraw;
    UINT align;
    WCHAR password;
    int password_width;
    HFONT font;
    int line_height, ave_width, system_ave;
    RECT fmt;
    int left_margin, right_margin;
    UINT sel_min, sel_max, caret;
    UINT caret_line;
    LINE *lines;
    UINT line_count, line_capacity;
    int max_width;
    UINT first_line;
    int visible_lines;
    int x_scroll;
    UINT undo;
    UINT ins_start, ins_end;
    WCHAR *deleted;
    UINT deleted_len, deleted_at;
    int *tabs;
    UINT tab_count;
    EDITWORDBREAKPROCW word_proc;
    BOOL selecting, word_mode, rbutton;
    UINT word_min, word_max;
    POINT last_mouse;
    DWORD ime_status;
} EDIT;

static int g_wheel;

static int imax(int a, int b) { return a > b ? a : b; }

static int imin(int a, int b) { return a < b ? a : b; }

static UINT umax(UINT a, UINT b) { return a > b ? a : b; }

static UINT umin(UINT a, UINT b) { return a < b ? a : b; }

static EDIT *edit_of(HWND hwnd) { return (EDIT *)GetWindowLongPtrW(hwnd, 0); }

/* the block only moves when reallocated, so the pointer stays valid unlocked. */
static WCHAR *T(EDIT *e)
{
    WCHAR *t = LocalLock(e->handle);
    LocalUnlock(e->handle);
    return t;
}

static void notify(EDIT *e, UINT code)
{
    if (e->parent)
        SendMessageW(e->parent, WM_COMMAND, MAKEWPARAM(e->id, code), (LPARAM)e->hwnd);
}

static BOOL key_down(int vk) { return GetKeyState(vk) < 0; }

static BOOL is_space(WCHAR c) { return c == L' ' || c == L'\t'; }

static BOOL is_white(WCHAR c) { return c == L' ' || c == L'\t' || c == L'\n'; }

static BOOL full_width(WCHAR c)
{
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFF60) || (c >= 0x3000 && c <= 0x303F);
}

static BOOL high_surrogate(WCHAR c) { return c >= 0xD800 && c <= 0xDBFF; }

static BOOL low_surrogate(WCHAR c) { return c >= 0xDC00 && c <= 0xDFFF; }

/* ------------------------------------------------------------------ storage */

static BOOL reserve(EDIT *e, UINT length)
{
    if (length + 1 <= e->capacity)
        return TRUE;
    HLOCAL h = LocalReAlloc(e->handle, ((SIZE_T)length + 32) * sizeof(WCHAR), LMEM_MOVEABLE);
    if (!h) {
        notify(e, EN_ERRSPACE);
        return FALSE;
    }
    e->handle = h;
    e->capacity = length + 32;
    return TRUE;
}

static void shrink(EDIT *e)
{
    if (e->capacity - e->len <= 32)
        return;
    HLOCAL h = LocalReAlloc(e->handle, ((SIZE_T)e->len + 16) * sizeof(WCHAR), LMEM_MOVEABLE);
    if (h) {
        e->handle = h;
        e->capacity = e->len + 16;
    }
}

/* ------------------------------------------------------------------ measuring */

static HDC text_dc(EDIT *e, HGDIOBJ *old)
{
    HDC dc = GetDC(e->hwnd);
    *old = SelectObject(dc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
    return dc;
}

static void release_dc(EDIT *e, HDC dc, HGDIOBJ old)
{
    SelectObject(dc, old);
    ReleaseDC(e->hwnd, dc);
}

static int next_tab(EDIT *e, int x)
{
    int step = 8 * e->ave_width;
    if (e->multi && e->tab_count == 1)
        step = e->tabs[0];
    else if (e->multi && e->tab_count > 1)
        for (UINT i = 0; i < e->tab_count; i++)
            if (e->tabs[i] > x)
                return e->tabs[i];
    if (step <= 0)
        step = 1;
    return (x / step + 1) * step;
}

/* x of each boundary in text[start, end) from the line's origin, tabs expanded. */
static int *measure(EDIT *e, HDC dc, UINT start, UINT end)
{
    UINT n = end - start;
    int *pos = UserAlloc(((SIZE_T)n + 1) * sizeof(int));
    if (!pos)
        return nullptr;
    if (e->password) {
        for (UINT i = 0; i <= n; i++)
            pos[i] = (int)i * e->password_width;
        return pos;
    }
    const WCHAR *t = T(e) + start;
    int x = 0;
    for (UINT i = 0; i < n;) {
        if (t[i] == L'\t') {
            x = next_tab(e, x);
            pos[++i] = x;
            continue;
        }
        UINT j = i;
        while (j < n && t[j] != L'\t')
            j++;
        SIZE size;
        GetTextExtentExPointW(dc, t + i, (int)(j - i), 0, nullptr, pos + i + 1, &size);
        for (UINT k = i + 1; k <= j; k++)
            pos[k] += x;
        x = pos[j];
        i = j;
    }
    return pos;
}

static int width_of(EDIT *e, HDC dc, UINT start, UINT end)
{
    if (start >= end)
        return 0;
    int *pos = measure(e, dc, start, end);
    int w = pos ? pos[end - start] : 0;
    UserFree(pos);
    return w;
}

static void set_font(EDIT *e, HFONT font)
{
    e->font = font;
    HGDIOBJ old;
    HDC dc = text_dc(e, &old);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SIZE size;
    GetTextExtentPoint32W(dc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size);
    e->line_height = tm.tmHeight;
    e->ave_width = (size.cx / 26 + 1) / 2;
    if (e->ave_width <= 0)
        e->ave_width = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 1;
    if (e->password) {
        GetTextExtentPoint32W(dc, &e->password, 1, &size);
        e->password_width = imax(size.cx, 1);
    }
    release_dc(e, dc, old);
}

/* ------------------------------------------------------------------ lines */

static BOOL add_line(EDIT *e, UINT start, UINT end, int width)
{
    if (e->line_count == e->line_capacity) {
        UINT capacity = e->line_capacity ? e->line_capacity * 2 : 16;
        LINE *lines = UserAlloc((SIZE_T)capacity * sizeof(LINE));
        if (!lines) {
            notify(e, EN_ERRSPACE);
            return FALSE;
        }
        if (e->lines)
            memcpy(lines, e->lines, (SIZE_T)e->line_count * sizeof(LINE));
        UserFree(e->lines);
        e->lines = lines;
        e->line_capacity = capacity;
    }
    e->lines[e->line_count++] = (LINE){start, end, width};
    if (width > e->max_width)
        e->max_width = width;
    return TRUE;
}

/* index of the first line break at or after i, or the text's end. */
static UINT hard_end(const WCHAR *t, UINT len, UINT i)
{
    for (; i < len; i++)
        if (t[i] == L'\r' && i + 1 < len && (t[i + 1] == L'\n' || (t[i + 1] == L'\r' && i + 2 < len && t[i + 2] == L'\n')))
            return i;
    return len;
}

static UINT word_left_of(EDIT *e, UINT i, UINT floor);

static int format_width(EDIT *e) { return e->fmt.right - e->fmt.left; }

static void update_scroll_bars(EDIT *e);
static void scroll_to(EDIT *e, BOOL vertical, int position, BOOL notify_scroll);

static void build_lines(EDIT *e)
{
    e->line_count = 0;
    e->max_width = 0;
    const WCHAR *t = T(e);
    UINT len = e->len;
    HGDIOBJ old;
    HDC dc = text_dc(e, &old);
    int W = format_width(e);
    UINT s = 0;
    for (;;) {
        UINT hard = hard_end(t, len, s), end;
        int width = 0;
        int *pos = measure(e, dc, s, hard);
        if (!e->wrap) {
            end = hard - s > LONGEST_LINE ? s + LONGEST_LINE : hard;
        } else if (W <= 0) {
            end = s < hard ? s + 1 : hard;
        } else {
            UINT fit = 0;
            while (pos && s + fit < hard && pos[fit + 1] <= W)
                fit++;
            if (s + fit == hard) {
                end = hard;
            } else if (!fit) {
                end = s + (high_surrogate(t[s]) && s + 1 < hard ? 2 : 1);
            } else {
                UINT b = s + fit;
                if (e->word_proc) {
                    if (e->word_proc((LPWSTR)t, (int)b, (int)len, WB_ISDELIMITER) ||
                        e->word_proc((LPWSTR)t, (int)b - 1, (int)len, WB_ISDELIMITER)) {
                        end = b;
                    } else {
                        UINT w = (UINT)e->word_proc((LPWSTR)t, (int)b, (int)len, WB_LEFT);
                        end = w > s ? w : b;
                    }
                } else if (t[b] != L'\r' && (is_space(t[b]) || is_space(t[b - 1]) || full_width(t[b]) ||
                                             full_width(t[b - 1]))) {
                    end = b;
                } else {
                    UINT w = word_left_of(e, b, s);
                    end = w > s ? w : b;
                }
            }
        }
        if (pos)
            width = pos[end - s];
        UserFree(pos);
        UINT next = end;
        if (e->wrap && next < hard && is_space(t[next]))
            next++;
        BOOL broke = FALSE;
        if (next == hard && hard < len) {
            next += t[next + 1] == L'\n' ? 2 : 3;
            if (next < len && t[next] == L'\n')
                next++;
            broke = TRUE;
        }
        if (!add_line(e, s, end, width))
            break;
        if (next >= len) {
            if (broke)
                add_line(e, len, len, 0);
            break;
        }
        s = next;
    }
    if (!e->line_count)
        add_line(e, 0, 0, 0);
    release_dc(e, dc, old);
    if (!len) {
        e->first_line = 0;
        e->x_scroll = 0;
    }
    update_scroll_bars(e);
    scroll_to(e, TRUE, (int)e->first_line, TRUE);
    scroll_to(e, FALSE, e->x_scroll, TRUE);
}

static UINT line_of(EDIT *e, UINT index)
{
    if (!e->multi)
        return 0;
    UINT lo = 0, hi = e->line_count;
    while (hi - lo > 1) {
        UINT mid = (lo + hi) / 2;
        if (e->lines[mid].start <= index)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

/* at a soft break the caret shows at the end of the wrapped line, unless a click put it on the next. */
static void fix_caret_line(EDIT *e)
{
    UINT l = line_of(e, e->caret);
    if (l && e->caret == e->lines[l].start && e->lines[l - 1].end == e->caret)
        l--;
    e->caret_line = l;
}

static UINT line_start(EDIT *e, UINT l) { return e->multi ? e->lines[l].start : 0; }

static UINT line_end(EDIT *e, UINT l) { return e->multi ? e->lines[l].end : e->len; }

/* ------------------------------------------------------------------ positions */

static int sl_text_width(EDIT *e, HDC dc) { return width_of(e, dc, 0, e->len); }

/* x where a line's text starts on screen, after alignment and scroll. */
static int line_origin(EDIT *e, HDC dc, UINT l)
{
    int W = format_width(e);
    if (!e->multi) {
        int tw = sl_text_width(e, dc);
        if (e->align == ES_CENTER && (W - tw) / 2 >= 0)
            return e->fmt.left + (W - tw) / 2;
        if (e->align == ES_RIGHT && W - tw >= 0)
            return e->fmt.left + W - tw;
        return e->fmt.left - e->x_scroll;
    }
    int offset = 0, lw = e->lines[l].width;
    if (e->align == ES_CENTER)
        offset = imax(0, (W - lw) / 2);
    else if (e->align == ES_RIGHT)
        offset = imax(0, W - lw);
    return e->fmt.left - e->x_scroll + offset;
}

/* index must be inside line l. */
static POINT position_on(EDIT *e, HDC dc, UINT l, UINT index)
{
    UINT start = line_start(e, l), end = line_end(e, l);
    if (index > end)
        index = end;
    POINT p;
    p.x = line_origin(e, dc, l) + width_of(e, dc, start, index);
    p.y = e->fmt.top + ((int)l - (int)e->first_line) * e->line_height;
    return p;
}

static UINT index_on_line(EDIT *e, HDC dc, UINT l, int x)
{
    UINT start = line_start(e, l), end = line_end(e, l);
    int origin = line_origin(e, dc, l);
    int *pos = measure(e, dc, start, end);
    if (!pos)
        return start;
    int rel = x - origin;
    UINT best = 0;
    for (UINT i = 0; i < end - start; i++) {
        if (rel < (pos[i] + pos[i + 1] + 1) / 2)
            break;
        best = i + 1;
    }
    UserFree(pos);
    UINT index = start + best;
    const WCHAR *t = T(e);
    if (index > start && index < e->len && low_surrogate(t[index]))
        index--;
    return index;
}

/* a point above the text gives the line before the first visible one, below it the line after the last. */
static UINT line_at(EDIT *e, int y)
{
    if (!e->multi)
        return 0;
    int l;
    if (y <= e->fmt.top)
        l = (int)e->first_line - 1;
    else if (y >= e->fmt.bottom)
        l = (int)e->first_line + e->visible_lines;
    else
        l = (int)e->first_line + (y - e->fmt.top) / e->line_height;
    if (l < 0)
        l = 0;
    if ((UINT)l >= e->line_count)
        l = (int)e->line_count - 1;
    return (UINT)l;
}

static UINT index_at(EDIT *e, int x, int y, UINT *line)
{
    HGDIOBJ old;
    HDC dc = text_dc(e, &old);
    UINT l = line_at(e, y);
    UINT index = index_on_line(e, dc, l, x);
    release_dc(e, dc, old);
    if (line)
        *line = l;
    return index;
}

/* ------------------------------------------------------------------ word boundaries */

static BOOL chunk_start(const WCHAR *t, UINT k)
{
    if (t[k] == L'\r' && t[k - 1] != L'\r')
        return TRUE;
    if (is_white(t[k - 1]) && !is_space(t[k]))
        return TRUE;
    return !is_space(t[k]) && (full_width(t[k]) || full_width(t[k - 1]));
}

/* words as Ctrl+arrows and double clicks see them: the end includes trailing spaces. */
static void word_at(EDIT *e, UINT i, BOOL left, UINT *start, UINT *end)
{
    UINT len = e->len;
    const WCHAR *t = T(e);
    if (e->word_proc) {
        if (left) {
            *start = (UINT)e->word_proc((LPWSTR)t, (int)i, (int)len, WB_LEFT);
            *end = (UINT)e->word_proc((LPWSTR)t, (int)*start, (int)len, WB_RIGHT);
        } else {
            *end = (UINT)e->word_proc((LPWSTR)t, (int)i, (int)len, WB_RIGHT);
            *start = (UINT)e->word_proc((LPWSTR)t, (int)*end, (int)len, WB_LEFT);
        }
        return;
    }
    if (e->password) {
        *start = 0;
        *end = len;
        return;
    }
    if ((left && !i) || (!left && i >= len)) {
        *start = *end = 0;
        return;
    }
    UINT s;
    if (left) {
        UINT k = i - 1;
        while (k > 0 && is_white(t[k]))
            k--;
        while (k > 0 && !chunk_start(t, k))
            k--;
        s = k;
    } else if (is_space(t[i])) {
        UINT k = i + 1;
        while (k < len && !chunk_start(t, k))
            k++;
        s = k;
    } else {
        UINT k = i;
        while (k > 0 && !chunk_start(t, k))
            k--;
        s = k;
    }
    UINT k = s + 1;
    while (k < len && !chunk_start(t, k))
        k++;
    *start = s;
    *end = s < len ? k : len;
}

static UINT word_left_of(EDIT *e, UINT i, UINT floor)
{
    const WCHAR *t = T(e);
    UINT k = i;
    while (k > floor && !chunk_start(t, k))
        k--;
    return k;
}

/* ------------------------------------------------------------------ scrolling */

static BOOL has_style(EDIT *e, DWORD style) { return ((DWORD)GetWindowLongW(e->hwnd, GWL_STYLE) & style) != 0; }

static void update_scroll_bars(EDIT *e)
{
    if (!e->multi)
        return;
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    if (has_style(e, WS_VSCROLL)) {
        si.nMax = (int)e->line_count - 1;
        si.nPage = (UINT)e->visible_lines;
        si.nPos = (int)e->first_line;
        SetScrollInfo(e->hwnd, SB_VERT, &si, TRUE);
    }
    if (has_style(e, WS_HSCROLL)) {
        si.nMax = e->max_width;
        si.nPage = (UINT)imax(format_width(e), 0);
        si.nPos = e->x_scroll;
        SetScrollInfo(e->hwnd, SB_HORZ, &si, TRUE);
    }
}

static void place_caret(EDIT *e);

/* clamps the line or pixel offset and notifies the parent before scrolling. */
static void scroll_to(EDIT *e, BOOL vertical, int position, BOOL notify_scroll)
{
    if (!e->multi)
        return;
    int max = vertical ? (int)e->line_count - 1 : e->max_width;
    int page = vertical ? e->visible_lines : format_width(e);
    int top = max - (page - 1);
    if (position > top)
        position = top;
    if (position < 0)
        position = 0;
    int old = vertical ? (int)e->first_line : e->x_scroll;
    if (position == old)
        return;
    if (notify_scroll)
        notify(e, vertical ? EN_VSCROLL : EN_HSCROLL);
    if (vertical)
        e->first_line = (UINT)position;
    else
        e->x_scroll = position;
    update_scroll_bars(e);
    if (e->redraw && IsWindowVisible(e->hwnd)) {
        InvalidateRect(e->hwnd, nullptr, TRUE);
        place_caret(e);
        UpdateWindow(e->hwnd);
    }
}

/* returns how far it moved. */
static LRESULT scroll_command(EDIT *e, BOOL vertical, int code, int amount)
{
    int old = vertical ? (int)e->first_line : e->x_scroll, pos = old;
    int line = vertical ? 1 : e->ave_width;
    int page = vertical ? imax(e->visible_lines - 1, 1) : format_width(e) - 1;
    switch (code) {
    case SB_LINEUP:
        pos -= line;
        break;
    case SB_LINEDOWN:
        pos += line;
        break;
    case SB_PAGEUP:
        pos -= page;
        break;
    case SB_PAGEDOWN:
        pos += page;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        pos = amount;
        break;
    case SB_TOP:
        pos = 0;
        break;
    case SB_BOTTOM:
        pos = vertical ? (int)e->line_count : e->max_width;
        break;
    case -1:
        pos += amount * line;
        break;
    default:
        return 0;
    }
    scroll_to(e, vertical, pos, code != SB_THUMBTRACK);
    int now = vertical ? (int)e->first_line : e->x_scroll;
    return now != old ? MAKELONG(now - old, TRUE) : 0;
}

/* scrolls by lines with auto vertical scroll, by pixels with auto horizontal scroll. */
static void scroll_caret(EDIT *e)
{
    if (!e->multi) {
        if (!e->auto_h || e->align != ES_LEFT) {
            e->x_scroll = 0;
            return;
        }
        HGDIOBJ old;
        HDC dc = text_dc(e, &old);
        int W = format_width(e), cx = width_of(e, dc, 0, e->caret), tw = sl_text_width(e, dc);
        int x = e->x_scroll;
        if (cx < x)
            x = cx - W / 4;
        else if (cx > x + W)
            x = cx - 3 * W / 4;
        if (tw > W && tw - x < W)
            x = tw - W;
        if (tw <= W || !e->len)
            x = 0;
        if (x < 0)
            x = 0;
        release_dc(e, dc, old);
        if (x != e->x_scroll) {
            e->x_scroll = x;
            InvalidateRect(e->hwnd, nullptr, FALSE);
        }
        return;
    }
    if (e->auto_v) {
        UINT l = e->caret_line;
        if (l >= e->first_line + (UINT)imax(e->visible_lines, 1))
            scroll_to(e, TRUE, (int)l - e->visible_lines + 1, TRUE);
        else if (l < e->first_line)
            scroll_to(e, TRUE, (int)l, TRUE);
    }
    if (e->auto_h && e->max_width > format_width(e)) {
        HGDIOBJ old;
        HDC dc = text_dc(e, &old);
        int x = position_on(e, dc, e->caret_line, e->caret).x;
        release_dc(e, dc, old);
        int W = format_width(e);
        if (x < e->fmt.left)
            scroll_to(e, FALSE, e->x_scroll + (x - (e->fmt.left + W / 3)) / e->ave_width * e->ave_width, TRUE);
        else if (x > e->fmt.right)
            scroll_to(e, FALSE, e->x_scroll + (x - (e->fmt.right - W / 3)) / e->ave_width * e->ave_width, TRUE);
    }
}

/* ------------------------------------------------------------------ caret */

static void place_caret(EDIT *e)
{
    if (!e->focus)
        return;
    HGDIOBJ old;
    HDC dc = text_dc(e, &old);
    POINT p;
    if (!e->multi) {
        p = position_on(e, dc, 0, e->caret);
        int k = e->system_ave <= e->ave_width ? 1 : 0;
        int max = e->fmt.right - 2 - k;
        if (p.x > max)
            p.x = max;
        if (p.x < 0)
            p.x = 0;
    } else {
        p = position_on(e, dc, e->caret_line, e->caret);
        RECT client;
        GetClientRect(e->hwnd, &client);
        int left = imax(e->fmt.left, client.left), right = imin(e->fmt.right, client.right);
        if (e->caret_line < e->first_line || e->caret_line >= e->first_line + (UINT)imax(e->visible_lines, 0) ||
            p.y < e->fmt.top || p.y > e->fmt.bottom - e->line_height ||
            (!e->wrap && (p.x <= e->fmt.left - 1 || p.x > e->fmt.right))) {
            p.x = p.y = PARKED;
        } else {
            if (p.x > right - 1)
                p.x = right - 1;
            if (p.x < left)
                p.x = left;
        }
    }
    release_dc(e, dc, old);
    SetCaretPos(p.x, p.y);
}

static void create_caret(EDIT *e)
{
    UINT width = 1;
    SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0);
    CreateCaret(e->hwnd, nullptr, (int)width, e->line_height);
    place_caret(e);
    ShowCaret(e->hwnd);
}

/* ------------------------------------------------------------------ painting */

static HBRUSH control_brush(EDIT *e, HDC dc)
{
    BOOL statics = e->readonly || e->disabled;
    UINT msg = statics ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT;
    HWND to = has_style(e, WS_POPUP) ? GetWindow(e->hwnd, GW_OWNER) : e->parent;
    HBRUSH brush = nullptr;
    if (to && GetWindowThreadProcessId(to, nullptr) == GetCurrentThreadId())
        brush = (HBRUSH)SendMessageW(to, msg, (WPARAM)dc, (LPARAM)e->hwnd);
    if (!brush)
        brush = (HBRUSH)DefWindowProcW(e->hwnd, msg, (WPARAM)dc, (LPARAM)e->hwnd);
    if (e->disabled && GetSysColor(COLOR_GRAYTEXT) != GetBkColor(dc))
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    return brush;
}

static BOOL shows_selection(EDIT *e) { return (e->focus || e->nohidesel) && e->sel_min != e->sel_max; }

/* draws a run piece by piece between tabs, opaque or transparent. */
static void draw_run(EDIT *e, HDC dc, const WCHAR *t, const int *pos, UINT from, UINT to, int origin, int y,
                     BOOL opaque)
{
    for (UINT i = from; i < to;) {
        RECT r = {origin + pos[i], y, origin + pos[i], y + e->line_height};
        if (!e->password && t[i] == L'\t') {
            r.right = origin + pos[i + 1];
            if (opaque)
                ExtTextOutW(dc, r.left, y, ETO_OPAQUE, &r, nullptr, 0, nullptr);
            i++;
            continue;
        }
        UINT j = i;
        while (j < to && (e->password || t[j] != L'\t'))
            j++;
        r.right = origin + pos[j];
        if (e->password) {
            WCHAR *masked = UserAlloc(((SIZE_T)(j - i) + 1) * sizeof(WCHAR));
            if (masked) {
                INT *dx = UserAlloc(((SIZE_T)(j - i) + 1) * sizeof(INT));
                for (UINT k = 0; k < j - i; k++) {
                    masked[k] = e->password;
                    if (dx)
                        dx[k] = e->password_width;
                }
                ExtTextOutW(dc, r.left, y, opaque ? ETO_OPAQUE : 0, &r, masked, j - i, dx);
                UserFree(dx);
                UserFree(masked);
            }
        } else {
            ExtTextOutW(dc, r.left, y, opaque ? ETO_OPAQUE : 0, &r, t + i, j - i, nullptr);
        }
        i = j;
    }
}

/* draws a line with its selection, and fills the background to its right when asked. */
static void draw_line(EDIT *e, HDC dc, UINT l, HBRUSH brush, BOOL fill_right)
{
    UINT start = line_start(e, l), end = line_end(e, l);
    int origin = line_origin(e, dc, l);
    int y = e->multi ? e->fmt.top + ((int)l - (int)e->first_line) * e->line_height : e->fmt.top;
    int *pos = measure(e, dc, start, end);
    if (!pos)
        return;
    const WCHAR *t = T(e) + start;
    BOOL opaque = GetBkMode(dc) != TRANSPARENT;
    RECT line_rect = {e->fmt.left, y, e->fmt.right, y + e->line_height};
    if (!opaque)
        FillRect(dc, &line_rect, brush);
    UINT a = start, b = start;
    if (shows_selection(e)) {
        a = umin(umax(e->sel_min, start), end);
        b = umin(umax(e->sel_max, start), end);
    }
    COLORREF text = GetTextColor(dc), back = GetBkColor(dc);
    if (opaque && origin + pos[0] > line_rect.left) {
        RECT gap = {line_rect.left, y, origin, y + e->line_height};
        ExtTextOutW(dc, gap.left, y, ETO_OPAQUE, &gap, nullptr, 0, nullptr);
    }
    draw_run(e, dc, t - start, pos - start, start, a, origin, y, opaque);
    if (b > a) {
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        if (!e->disabled)
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        draw_run(e, dc, t - start, pos - start, a, b, origin, y, TRUE);
        SetBkColor(dc, back);
        SetTextColor(dc, text);
    }
    draw_run(e, dc, t - start, pos - start, b, end, origin, y, opaque);
    if (opaque && fill_right) {
        RECT rest = {origin + pos[end - start], y, line_rect.right, y + e->line_height};
        if (rest.left < line_rect.left)
            rest.left = line_rect.left;
        if (rest.right > rest.left)
            ExtTextOutW(dc, rest.left, y, ETO_OPAQUE, &rest, nullptr, 0, nullptr);
    }
    UserFree(pos);
}

static void text_clip(EDIT *e, HDC dc)
{
    RECT client, clip = e->fmt;
    GetClientRect(e->hwnd, &client);
    if (e->flat)
        InflateRect(&client, -1, -1);
    if (!e->x_scroll)
        clip.left -= e->left_margin;
    clip.right += e->right_margin;
    IntersectRect(&clip, &clip, &client);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
}

static void paint(EDIT *e, HDC dc, const RECT *update)
{
    if (!IsWindowVisible(e->hwnd))
        return;
    RECT client;
    GetClientRect(e->hwnd, &client);
    SetBkMode(dc, OPAQUE);
    HBRUSH brush = control_brush(e, dc);
    if (!e->multi)
        FillRect(dc, &client, brush);
    if (e->flat)
        FrameRect(dc, &client, GetSysColorBrush(COLOR_WINDOWFRAME));
    int saved = SaveDC(dc);
    text_clip(e, dc);
    HGDIOBJ old = SelectObject(dc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
    if (!e->multi) {
        draw_line(e, dc, 0, brush, TRUE);
    } else {
        for (UINT l = e->first_line; l < e->line_count && l < e->first_line + (UINT)e->visible_lines + 1; l++) {
            int y = e->fmt.top + ((int)l - (int)e->first_line) * e->line_height;
            if (update && (y >= update->bottom || y + e->line_height <= update->top))
                continue;
            draw_line(e, dc, l, brush, TRUE);
        }
    }
    SelectObject(dc, old);
    RestoreDC(dc, saved);
}

/*
 * like windows, a change is drawn at once through the window dc. invalid areas stay invalid, for a parent that paints
 * over its children.
 */
static void redraw(EDIT *e, BOOL erase)
{
    if (!e->redraw || !IsWindowVisible(e->hwnd))
        return;
    HDC dc = GetDC(e->hwnd);
    if (!dc)
        return;
    HideCaret(e->hwnd);
    if (e->multi && erase) {
        RECT client;
        GetClientRect(e->hwnd, &client);
        int bottom = e->fmt.top + ((int)e->line_count - (int)e->first_line) * e->line_height;
        if (bottom < client.bottom) {
            RECT below = {client.left, imax(bottom, client.top), client.right, client.bottom};
            FillRect(dc, &below, control_brush(e, dc));
        }
    }
    paint(e, dc, nullptr);
    ShowCaret(e->hwnd);
    ReleaseDC(e->hwnd, dc);
}

/* ------------------------------------------------------------------ the format rectangle */

static void set_rect(EDIT *e, const RECT *given, BOOL repaint)
{
    RECT r;
    if (given)
        r = *given;
    else
        GetClientRect(e->hwnd, &r);
    if (r.right <= r.left || r.bottom <= r.top) {
        if (e->fmt_valid)
            return;
        SetRect(&r, 0, 0, 10 * e->ave_width, e->line_height);
    } else if (!given && r.right - r.left > e->left_margin + e->right_margin) {
        r.left += e->left_margin;
        r.right -= e->right_margin;
    }
    if (e->border) {
        int inset = e->flat ? 2 : 1;
        r.left += inset;
        r.right -= inset;
        if (r.bottom - r.top >= e->line_height + 2 * inset) {
            r.top += inset;
            r.bottom -= inset;
        }
    }
    if (!e->multi) {
        e->fmt = r;
        if (e->fmt.bottom > r.top + e->line_height)
            e->fmt.bottom = r.top + e->line_height;
        e->fmt_valid = TRUE;
    } else if (r.right - r.left >= e->ave_width && r.bottom - r.top >= e->line_height) {
        e->fmt = r;
        e->visible_lines = (r.bottom - r.top) / e->line_height;
        e->fmt.bottom = r.top + e->visible_lines * e->line_height;
        e->fmt_valid = TRUE;
        if (e->wrap)
            build_lines(e);
        else {
            update_scroll_bars(e);
            scroll_to(e, TRUE, (int)e->first_line, TRUE);
            scroll_to(e, FALSE, e->x_scroll, TRUE);
        }
    } else if (e->focus) {
        SetCaretPos(PARKED, PARKED);
    }
    if (repaint)
        InvalidateRect(e->hwnd, nullptr, TRUE);
    place_caret(e);
}

/* ------------------------------------------------------------------ selection */

static void set_selection(EDIT *e, UINT anchor, UINT caret)
{
    anchor = umin(anchor, e->len);
    caret = umin(caret, e->len);
    UINT min = umin(anchor, caret), max = umax(anchor, caret);
    BOOL changed = min != e->sel_min || max != e->sel_max;
    e->sel_min = min;
    e->sel_max = max;
    e->caret = caret;
    if (e->multi)
        fix_caret_line(e);
    if (changed && (e->focus || e->nohidesel))
        redraw(e, FALSE);
}

static UINT anchor_of(EDIT *e) { return e->caret == e->sel_min ? e->sel_max : e->sel_min; }

/* ------------------------------------------------------------------ undo */

static void undo_clear(EDIT *e)
{
    UserFree(e->deleted);
    e->deleted = nullptr;
    e->deleted_len = 0;
    e->undo = 0;
}

static void undo_insert(EDIT *e, UINT p, UINT n)
{
    if (!e->undo) {
        e->undo = UNDO_INSERT;
    } else if ((e->undo & UNDO_INSERT) && e->ins_end == p) {
        e->ins_end += n;
        return;
    } else if ((e->undo & UNDO_DELETE) && e->deleted_at != p) {
        undo_clear(e);
    }
    e->undo |= UNDO_INSERT;
    e->ins_start = p;
    e->ins_end = p + n;
}

static void undo_delete(EDIT *e, UINT a, UINT b, const WCHAR *removed)
{
    UINT n = b - a;
    if (e->undo == UNDO_DELETE && (e->deleted_at == b || e->deleted_at == a)) {
        WCHAR *joined = UserAlloc(((SIZE_T)e->deleted_len + n) * sizeof(WCHAR));
        if (joined) {
            if (e->deleted_at == b) {
                memcpy(joined, removed, n * sizeof(WCHAR));
                memcpy(joined + n, e->deleted, e->deleted_len * sizeof(WCHAR));
                e->deleted_at = a;
            } else {
                memcpy(joined, e->deleted, e->deleted_len * sizeof(WCHAR));
                memcpy(joined + e->deleted_len, removed, n * sizeof(WCHAR));
            }
            UserFree(e->deleted);
            e->deleted = joined;
            e->deleted_len += n;
        }
        return;
    }
    undo_clear(e);
    e->deleted = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!e->deleted)
        return;
    memcpy(e->deleted, removed, n * sizeof(WCHAR));
    e->deleted_len = n;
    e->deleted_at = a;
    e->undo = UNDO_DELETE;
}

/* ------------------------------------------------------------------ editing */

static void after_change(EDIT *e)
{
    e->modified = TRUE;
    if (e->multi)
        build_lines(e);
}

/* records the range for undo, the caret goes to its start. */
static void delete_range(EDIT *e, UINT a, UINT b)
{
    if (a >= b)
        return;
    WCHAR *t = T(e);
    undo_delete(e, a, b, t + a);
    memmove(t + a, t + b, (e->len - b) * sizeof(WCHAR));
    e->len -= b - a;
    shrink(e);
    e->caret = e->sel_min = e->sel_max = a;
    after_change(e);
    if (e->multi)
        fix_caret_line(e);
}

static BOOL delete_selection(EDIT *e)
{
    if (e->sel_min == e->sel_max)
        return FALSE;
    delete_range(e, e->sel_min, e->sel_max);
    return TRUE;
}

/* applies ES_UPPERCASE, ES_LOWERCASE and ES_OEMCONVERT to inserted text. */
static void filter(EDIT *e, WCHAR *s, UINT n)
{
    DWORD style = (DWORD)GetWindowLongW(e->hwnd, GWL_STYLE);
    if (style & ES_LOWERCASE)
        CharLowerBuffW(s, n);
    else if (style & ES_UPPERCASE)
        CharUpperBuffW(s, n);
    if (!(style & ES_OEMCONVERT))
        return;
    for (UINT i = 0; i < n; i++) {
        WCHAR c = s[i];
        if (c == L'\t' || c == L'\n' || c == L'\r')
            continue;
        BOOL lower = IsCharLowerW(c);
        WCHAR u = c;
        if (lower)
            CharUpperBuffW(&u, 1);
        char oem[2];
        int bytes = WideCharToMultiByte(CP_OEMCP, 0, &u, 1, oem, 2, "_", nullptr);
        if (bytes > 0 && MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS, oem, bytes, &u, 1) == 1) {
            if (lower)
                CharLowerBuffW(&u, 1);
            s[i] = u;
        }
    }
}

/* TRUE when the lines overflow an edit that cannot scroll down. */
static BOOL too_many_lines(EDIT *e) { return e->multi && !e->auto_v && (int)e->line_count > e->visible_lines; }

/* inserts at the caret as much as the limits allow. without limits, as for WM_SETTEXT, everything goes in. */
static UINT insert_text(EDIT *e, const WCHAR *s, UINT n, BOOL limits, BOOL *truncated)
{
    *truncated = FALSE;
    UINT count = n;
    if (limits) {
        if (e->len >= e->limit) {
            *truncated = n > 0;
            return 0;
        }
        count = umin(n, e->limit - e->len);
        if (!e->multi && !e->auto_h && count) {
            HGDIOBJ old;
            HDC dc = text_dc(e, &old);
            int room = format_width(e) - sl_text_width(e, dc);
            UINT fit = 0;
            if (e->password) {
                fit = room > 0 ? (UINT)(room / e->password_width) : 0;
            } else {
                int x = 0;
                while (fit < count) {
                    SIZE size;
                    GetTextExtentPoint32W(dc, s + fit, 1, &size);
                    if (x + size.cx > room)
                        break;
                    x += size.cx;
                    fit++;
                }
            }
            release_dc(e, dc, old);
            count = umin(count, fit);
        }
        if (count < n && count && high_surrogate(s[count - 1]))
            count--;
        if (e->multi && count < n && count && s[count - 1] == L'\r' && s[count] == L'\n')
            count--;
        *truncated = count < n;
    }
    if (!count || !reserve(e, e->len + count))
        return 0;
    WCHAR *t = T(e);
    UINT at = e->caret;
    memmove(t + at + count, t + at, (e->len - at) * sizeof(WCHAR));
    memcpy(t + at, s, count * sizeof(WCHAR));
    e->len += count;
    filter(e, t + at, count);
    undo_insert(e, at, count);
    e->caret = e->sel_min = e->sel_max = at + count;
    after_change(e);
    if (e->multi)
        fix_caret_line(e);
    return count;
}

static void value_changed(EDIT *e) { NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, e->hwnd, OBJID_CLIENT, 0); }

/* ends a program or user change with notifications and drawing in windows' order. */
static void changed(EDIT *e, BOOL maxtext)
{
    if (!e->multi) {
        if (maxtext)
            notify(e, EN_MAXTEXT);
        notify(e, EN_UPDATE);
        scroll_caret(e);
        redraw(e, FALSE);
        place_caret(e);
        notify(e, EN_CHANGE);
    } else {
        notify(e, EN_UPDATE);
        redraw(e, TRUE);
        scroll_caret(e);
        place_caret(e);
        notify(e, EN_CHANGE);
        if (maxtext)
            notify(e, EN_MAXTEXT);
    }
    value_changed(e);
}

/* typed or pasted text is taken back when a multiline edit runs out of lines. */
static UINT ml_insert(EDIT *e, const WCHAR *s, UINT n)
{
    if (e->len >= e->limit) {
        notify(e, EN_MAXTEXT);
        return 0;
    }
    UINT undo = e->undo, ins_start = e->ins_start, ins_end = e->ins_end;
    BOOL truncated;
    UINT at = e->caret;
    UINT count = insert_text(e, s, n, TRUE, &truncated);
    if (count && too_many_lines(e)) {
        WCHAR *t = T(e);
        memmove(t + at, t + at + count, (e->len - at - count) * sizeof(WCHAR));
        e->len -= count;
        e->caret = e->sel_min = e->sel_max = at;
        e->undo = undo;
        e->ins_start = ins_start;
        e->ins_end = ins_end;
        build_lines(e);
        fix_caret_line(e);
        MessageBeep(0);
        notify(e, EN_MAXTEXT);
        return 0;
    }
    if (count)
        changed(e, truncated);
    else if (truncated)
        notify(e, EN_MAXTEXT);
    return count;
}

/* replaces the whole text without limits, for WM_SETTEXT and EM_SETHANDLE. */
static void reset(EDIT *e)
{
    e->caret = e->sel_min = e->sel_max = 0;
    e->first_line = 0;
    e->x_scroll = 0;
    e->caret_line = 0;
    e->modified = FALSE;
    undo_clear(e);
    if (!e->multi && !e->combo)
        notify(e, EN_UPDATE);
    if (e->multi)
        build_lines(e);
    if (IsWindowVisible(e->hwnd)) {
        RECT r;
        GetClientRect(e->hwnd, &r);
        if (e->flat)
            InflateRect(&r, -1, -1);
        BOOL erase = e->multi && e->first_line + (UINT)e->visible_lines >= e->line_count;
        InvalidateRect(e->hwnd, &r, erase);
    }
    place_caret(e);
    if (!e->multi && !e->combo)
        notify(e, EN_CHANGE);
    value_changed(e);
}

static BOOL set_text(EDIT *e, LPCWSTR s)
{
    UINT n = s ? (UINT)lstrlenW(s) : 0;
    e->len = 0;
    e->caret = e->sel_min = e->sel_max = 0;
    if (n) {
        BOOL truncated;
        if (!reserve(e, n) || insert_text(e, s, n, FALSE, &truncated) != n) {
            reset(e);
            return FALSE;
        }
    } else {
        shrink(e);
    }
    reset(e);
    return TRUE;
}

/* a line break or a surrogate pair counts as one character. */
static UINT previous_char(EDIT *e, UINT i)
{
    if (!i)
        return 0;
    const WCHAR *t = T(e);
    if (e->multi && i >= 2 && t[i - 1] == L'\n' && t[i - 2] == L'\r')
        return i >= 3 && t[i - 3] == L'\r' ? i - 3 : i - 2;
    if (i >= 2 && low_surrogate(t[i - 1]) && high_surrogate(t[i - 2]))
        return i - 2;
    return i - 1;
}

static UINT next_char(EDIT *e, UINT i)
{
    if (i >= e->len)
        return e->len;
    const WCHAR *t = T(e);
    if (e->multi && t[i] == L'\r') {
        if (i + 1 < e->len && t[i + 1] == L'\n')
            return i + 2;
        if (i + 2 < e->len && t[i + 1] == L'\r' && t[i + 2] == L'\n')
            return i + 3;
    }
    if (high_surrogate(t[i]) && i + 1 < e->len && low_surrogate(t[i + 1]))
        return i + 2;
    return i + 1;
}

/* deletes the selection, or the character before the caret. */
static BOOL backspace(EDIT *e)
{
    if (delete_selection(e))
        return TRUE;
    if (!e->caret)
        return FALSE;
    delete_range(e, previous_char(e, e->caret), e->caret);
    return TRUE;
}

/* ------------------------------------------------------------------ clipboard */

static LRESULT copy(EDIT *e)
{
    if (e->password) {
        MessageBeep(0);
        return 0;
    }
    UINT n = e->sel_max - e->sel_min;
    if (!n || !OpenClipboard(e->hwnd))
        return 0;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)n + 1) * sizeof(WCHAR));
    WCHAR *p = h ? GlobalLock(h) : nullptr;
    if (p) {
        memcpy(p, T(e) + e->sel_min, n * sizeof(WCHAR));
        p[n] = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    return p ? (LRESULT)n * sizeof(WCHAR) : 0;
}

static void clear(EDIT *e)
{
    if (e->readonly || e->sel_min == e->sel_max)
        return;
    if (e->multi) {
        SendMessageW(e->hwnd, WM_CHAR, VK_BACK, 0);
        return;
    }
    delete_selection(e);
    changed(e, FALSE);
}

static WCHAR *clipboard_text(EDIT *e)
{
    if (!OpenClipboard(e->hwnd))
        return nullptr;
    WCHAR *copy_text = nullptr;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const WCHAR *p = h ? GlobalLock(h) : nullptr;
    if (p) {
        int n = lstrlenW(p);
        copy_text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
        if (copy_text)
            memcpy(copy_text, p, ((SIZE_T)n + 1) * sizeof(WCHAR));
        GlobalUnlock(h);
    }
    CloseClipboard();
    return copy_text;
}

static LRESULT paste(EDIT *e)
{
    if (e->readonly)
        return 0;
    WCHAR *s = clipboard_text(e);
    if (!e->multi) {
        delete_selection(e);
        BOOL truncated = FALSE;
        if (s) {
            UINT n = 0;
            while (s[n] && s[n] != L'\r')
                n++;
            insert_text(e, s, n, TRUE, &truncated);
        }
        changed(e, truncated);
    } else {
        HCURSOR old = SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_WAIT));
        if (delete_selection(e))
            changed(e, FALSE);
        if (s)
            ml_insert(e, s, (UINT)lstrlenW(s));
        SetCursor(old);
    }
    UserFree(s);
    return 1;
}

/* ------------------------------------------------------------------ undo */

static LRESULT undo(EDIT *e)
{
    if (!e->undo)
        return e->multi ? FALSE : 1;
    UINT kind = e->undo, ins_start = e->ins_start, ins_end = e->ins_end, at = e->deleted_at, n = e->deleted_len;
    WCHAR *text = e->deleted;
    e->deleted = nullptr;
    e->undo = 0;
    e->deleted_len = 0;
    if (kind & UNDO_INSERT) {
        e->sel_min = e->caret = umin(ins_start, e->len);
        e->sel_max = umin(ins_end, e->len);
        if (e->multi)
            SendMessageW(e->hwnd, WM_CHAR, VK_BACK, 0);
        else
            delete_selection(e);
    }
    if ((kind & UNDO_DELETE) && text) {
        e->caret = e->sel_min = e->sel_max = umin(at, e->len);
        BOOL truncated;
        UINT inserted = e->multi ? ml_insert(e, text, n) : insert_text(e, text, n, FALSE, &truncated);
        set_selection(e, e->caret - inserted, e->caret);
    }
    UserFree(text);
    if (!e->multi) {
        notify(e, EN_UPDATE);
        if (IsWindowVisible(e->hwnd))
            InvalidateRect(e->hwnd, nullptr, FALSE);
        scroll_caret(e);
        place_caret(e);
        notify(e, EN_CHANGE);
        value_changed(e);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ characters */

static BOOL digit(WCHAR c)
{
    WORD type = 0;
    GetStringTypeW(CT_CTYPE1, &c, 1, &type);
    return (type & C1_DIGIT) != 0;
}

static BOOL combo_dropped(EDIT *e) { return e->combo && e->parent && SendMessageW(e->parent, CB_GETDROPPEDSTATE, 0, 0); }

static void sl_char(EDIT *e, WCHAR c)
{
    if (e->readonly && c != 3)
        return;
    if (c >= 0x20 && has_style(e, ES_NUMBER) && !digit(c)) {
        MessageBeep(0);
        return;
    }
    BOOL text_changed = FALSE, truncated = FALSE;
    if (c == VK_BACK || (c >= 0x20))
        text_changed = delete_selection(e);
    switch (c) {
    case VK_BACK:
        if (!text_changed && e->caret)
            text_changed = backspace(e);
        break;
    case 3:
        SendMessageW(e->hwnd, WM_COPY, 0, 0);
        return;
    case 0x16:
        SendMessageW(e->hwnd, WM_PASTE, 0, 0);
        return;
    case 0x18:
        if (e->sel_min != e->sel_max)
            SendMessageW(e->hwnd, WM_CUT, 0, 0);
        else
            MessageBeep(0);
        return;
    case 0x1A:
        SendMessageW(e->hwnd, EM_UNDO, 0, 0);
        return;
    case L'\r':
    case 0x1B:
        if (combo_dropped(e))
            SendMessageW(e->parent, WM_KEYDOWN, c == L'\r' ? VK_RETURN : VK_ESCAPE, 0);
        else
            MessageBeep(0);
        return;
    default:
        if (c >= 0x1E) {
            if (insert_text(e, &c, 1, TRUE, &truncated))
                text_changed = TRUE;
            else
                MessageBeep(0);
            ShowCursor(TRUE);
        } else {
            MessageBeep(0);
        }
        break;
    }
    if (text_changed || truncated)
        changed(e, truncated);
}

static void ml_char(EDIT *e, WCHAR c)
{
    if (c == 0x1B)
        return;
    BOOL ctrl = key_down(VK_CONTROL), shift = key_down(VK_SHIFT), alt = key_down(VK_MENU);
    if (e->in_dialog && c == L'\t' && !(ctrl && !shift && !alt))
        return;
    if (e->in_dialog && c == L'\r' && !has_style(e, ES_WANTRETURN) && !(ctrl && !shift && !alt))
        return;
    if (e->readonly) {
        if (c == 3)
            SendMessageW(e->hwnd, WM_COPY, 0, 0);
        return;
    }
    if (c == L'\n')
        c = L'\r';
    if (c >= 0x20 && has_style(e, ES_NUMBER) && !digit(c)) {
        MessageBeep(0);
        return;
    }
    if (c == VK_BACK || c == L'\t' || c == L'\r' || c >= 0x20) {
        if (delete_selection(e)) {
            changed(e, FALSE);
            if (c == VK_BACK)
                return;
        }
    }
    switch (c) {
    case VK_BACK:
        if (backspace(e))
            changed(e, FALSE);
        return;
    case L'\r':
        ml_insert(e, L"\r\n", 2);
        return;
    case 3:
        SendMessageW(e->hwnd, WM_COPY, 0, 0);
        return;
    case 0x16:
        SendMessageW(e->hwnd, WM_PASTE, 0, 0);
        return;
    case 0x18:
        if (e->sel_min != e->sel_max)
            SendMessageW(e->hwnd, WM_CUT, 0, 0);
        else
            MessageBeep(0);
        return;
    case 0x1A:
        SendMessageW(e->hwnd, WM_UNDO, 0, 0);
        return;
    default:
        if (c == L'\t' || c >= 0x1E)
            ml_insert(e, &c, 1);
        else
            MessageBeep(0);
        return;
    }
}

/* ------------------------------------------------------------------ keys */

/* with extend, the selection grows from the anchor. */
static void move_caret(EDIT *e, UINT to, BOOL extend)
{
    UINT anchor = extend ? anchor_of(e) : to;
    set_selection(e, anchor, to);
    scroll_caret(e);
    place_caret(e);
}

static void click(EDIT *e, int x, int y, BOOL shift, BOOL keep_line);

static void delete_key(EDIT *e, BOOL ctrl, BOOL shift)
{
    if (e->readonly || (ctrl && shift))
        return;
    if (shift) {
        if (e->sel_min != e->sel_max)
            SendMessageW(e->hwnd, WM_CUT, 0, 0);
        return;
    }
    if (e->sel_min == e->sel_max) {
        UINT end = ctrl ? (e->multi ? line_end(e, e->caret_line) : e->len) : next_char(e, e->caret);
        if (end <= e->caret)
            return;
        e->sel_min = e->caret;
        e->sel_max = end;
    }
    if (e->multi) {
        SendMessageW(e->hwnd, WM_CHAR, VK_BACK, 0);
    } else {
        delete_selection(e);
        changed(e, FALSE);
    }
}

static void horizontal_key(EDIT *e, BOOL right, BOOL ctrl, BOOL shift)
{
    UINT to;
    if (right) {
        if (e->caret >= e->len) {
            if (!shift)
                move_caret(e, e->len, FALSE);
            return;
        }
        UINT start, end;
        if (ctrl)
            word_at(e, e->caret, FALSE, &start, &end), to = end;
        else
            to = next_char(e, e->caret);
    } else {
        if (!e->caret) {
            if (!shift)
                move_caret(e, 0, FALSE);
            return;
        }
        UINT start, end;
        if (ctrl)
            word_at(e, e->caret, TRUE, &start, &end), to = start;
        else
            to = previous_char(e, e->caret);
    }
    move_caret(e, to, shift);
}

static void ml_key(EDIT *e, UINT vk)
{
    BOOL ctrl = key_down(VK_CONTROL), shift = key_down(VK_SHIFT);
    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT:
        horizontal_key(e, vk == VK_RIGHT, ctrl, shift);
        break;
    case VK_UP:
    case VK_DOWN: {
        if (ctrl)
            break;
        HGDIOBJ old;
        HDC dc = text_dc(e, &old);
        POINT p = position_on(e, dc, e->caret_line, e->caret);
        release_dc(e, dc, old);
        int y = vk == VK_UP ? p.y - 1 : p.y + e->line_height;
        if (vk == VK_UP && e->caret_line == 0)
            y = p.y;
        if (vk == VK_UP && e->caret_line == e->first_line && e->caret_line && !e->auto_v)
            break;
        click(e, p.x, y, shift, FALSE);
        break;
    }
    case VK_HOME:
        move_caret(e, ctrl ? 0 : line_start(e, e->caret_line), shift);
        break;
    case VK_END:
        move_caret(e, ctrl ? e->len : line_end(e, e->caret_line), shift);
        if (!ctrl)
            fix_caret_line(e), place_caret(e);
        break;
    case VK_PRIOR:
    case VK_NEXT: {
        if (ctrl) {
            int chars = format_width(e) / e->ave_width - 1;
            scroll_command(e, FALSE, -1, vk == VK_PRIOR ? -chars : chars);
            break;
        }
        HGDIOBJ old;
        HDC dc = text_dc(e, &old);
        POINT p = position_on(e, dc, e->caret_line, e->caret);
        release_dc(e, dc, old);
        SendMessageW(e->hwnd, WM_VSCROLL, vk == VK_PRIOR ? SB_PAGEUP : SB_PAGEDOWN, 0);
        click(e, p.x, p.y, shift, FALSE);
        break;
    }
    case VK_DELETE:
        delete_key(e, ctrl, shift);
        break;
    case VK_INSERT:
        if (ctrl)
            SendMessageW(e->hwnd, WM_COPY, 0, 0);
        else if (shift)
            SendMessageW(e->hwnd, WM_PASTE, 0, 0);
        break;
    case VK_TAB:
        if (e->in_dialog && !ctrl)
            SendMessageW(e->parent, WM_NEXTDLGCTL, shift, 0);
        break;
    case VK_RETURN:
        if (e->in_dialog && !has_style(e, ES_WANTRETURN) && !ctrl) {
            LRESULT id = SendMessageW(e->parent, DM_GETDEFID, 0, 0);
            HWND button = HIWORD(id) == DC_HASDEFID ? GetDlgItem(e->parent, LOWORD(id)) : nullptr;
            if (button) {
                SendMessageW(e->parent, WM_NEXTDLGCTL, (WPARAM)button, TRUE);
                if (GetFocus() != e->hwnd)
                    PostMessageW(button, WM_KEYDOWN, VK_RETURN, 0);
            }
        }
        break;
    case VK_ESCAPE:
        if (e->in_dialog)
            PostMessageW(e->parent, WM_CLOSE, 0, 0);
        break;
    }
}

static void sl_key(EDIT *e, UINT vk, LPARAM lparam)
{
    BOOL ctrl = key_down(VK_CONTROL), shift = key_down(VK_SHIFT);
    if (e->combo && (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_F4)) {
        SendMessageW(e->parent, WM_KEYDOWN, vk, lparam);
        return;
    }
    switch (vk) {
    case VK_LEFT:
    case VK_UP:
        horizontal_key(e, FALSE, ctrl, shift);
        break;
    case VK_RIGHT:
    case VK_DOWN:
        horizontal_key(e, TRUE, ctrl, shift);
        break;
    case VK_HOME:
        move_caret(e, 0, shift);
        break;
    case VK_END:
        move_caret(e, e->len, shift);
        break;
    case VK_DELETE:
        delete_key(e, ctrl, shift);
        break;
    case VK_INSERT:
        if (ctrl)
            SendMessageW(e->hwnd, WM_COPY, 0, 0);
        else if (shift)
            SendMessageW(e->hwnd, WM_PASTE, 0, 0);
        break;
    }
}

/* ------------------------------------------------------------------ mouse */

/* shift extends the selection from the anchor, and the caret goes on the clicked line. */
static void click(EDIT *e, int x, int y, BOOL shift, BOOL keep_line)
{
    UINT l;
    UINT index = index_at(e, x, y, &l);
    UINT anchor = shift ? anchor_of(e) : index;
    set_selection(e, anchor, index);
    if (e->multi) {
        e->caret_line = l;
        scroll_caret(e);
    } else {
        scroll_caret(e);
        redraw(e, FALSE);
    }
    place_caret(e);
    (void)keep_line;
}

static BOOL top_level_active(EDIT *e)
{
    HWND top = GetAncestor(e->hwnd, GA_ROOT);
    return !top || top == GetActiveWindow() || top == GetForegroundWindow();
}

static void button_down(EDIT *e, int x, int y, BOOL shift)
{
    if (!top_level_active(e) && !e->focus)
        return;
    if (!e->multi) {
        if (!e->focus) {
            if (!e->nohidesel)
                set_selection(e, e->caret, e->caret);
            SetFocus(e->hwnd);
            if (!e->focus)
                return;
        }
        e->word_mode = FALSE;
        click(e, x, y, shift, FALSE);
    } else {
        e->word_mode = FALSE;
        click(e, x, y, shift, FALSE);
        if (!e->focus)
            SetFocus(e->hwnd);
        SetTimer(e->hwnd, SCROLL_TIMER, 100, nullptr);
    }
    SetCapture(e->hwnd);
    e->selecting = TRUE;
    e->last_mouse = (POINT){x, y};
}

static void drag(EDIT *e, int x, int y)
{
    e->last_mouse = (POINT){x, y};
    UINT l;
    UINT index = index_at(e, x, y, &l);
    UINT anchor = anchor_of(e);
    if (e->word_mode) {
        UINT start, end;
        word_at(e, index, index && (!e->multi || index != line_start(e, l)), &start, &end);
        if (index < e->word_min) {
            anchor = e->word_max;
            index = start;
        } else {
            anchor = e->word_min;
            index = umax(end, e->word_max);
        }
    }
    set_selection(e, anchor, index);
    if (e->multi)
        e->caret_line = l;
    scroll_caret(e);
    if (!e->multi)
        redraw(e, FALSE);
    place_caret(e);
    if (e->multi) {
        int distance = y < 0 ? -y : y > e->fmt.bottom ? y - e->fmt.bottom : 0;
        int interval = 100 - 16 * distance;
        SetTimer(e->hwnd, SCROLL_TIMER, interval < 1 ? 1 : (UINT)interval, nullptr);
    }
}

static void double_click(EDIT *e, int x, int y, BOOL shift)
{
    UINT l;
    UINT index = index_at(e, x, y, &l);
    BOOL left = index && (!e->multi || index != line_start(e, l));
    UINT start, end;
    word_at(e, index, left, &start, &end);
    if (shift) {
        UINT anchor = anchor_of(e);
        if (index < anchor)
            set_selection(e, anchor, start);
        else
            set_selection(e, anchor, end);
    } else {
        set_selection(e, start, end);
    }
    e->word_min = start;
    e->word_max = end;
    e->word_mode = TRUE;
    if (e->multi)
        e->caret_line = l;
    scroll_caret(e);
    place_caret(e);
    SetCapture(e->hwnd);
    e->selecting = TRUE;
    if (e->multi)
        SetTimer(e->hwnd, SCROLL_TIMER, 100, nullptr);
}

static void end_selecting(EDIT *e)
{
    if (!e->selecting)
        return;
    e->selecting = FALSE;
    if (e->multi)
        KillTimer(e->hwnd, SCROLL_TIMER);
    if (GetCapture() == e->hwnd)
        ReleaseCapture();
    place_caret(e);
}

/* ------------------------------------------------------------------ context menu */

static void context_menu(EDIT *e, LPARAM lparam)
{
    if (!top_level_active(e))
        return;
    SetFocus(e->hwnd);
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    BOOL selected = e->sel_min != e->sel_max;
    UINT cut = selected && !e->readonly && !e->password ? MF_ENABLED : MF_GRAYED;
    AppendMenuW(menu, e->undo ? MF_ENABLED : MF_GRAYED, WM_UNDO, L"&Undo");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, cut, WM_CUT, L"Cu&t");
    AppendMenuW(menu, selected && !e->password ? MF_ENABLED : MF_GRAYED, WM_COPY, L"&Copy");
    AppendMenuW(menu, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED, WM_PASTE, L"&Paste");
    AppendMenuW(menu, cut, WM_CLEAR, L"&Delete");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, e->sel_min != 0 || e->sel_max != e->len ? MF_ENABLED : MF_GRAYED, EM_SETSEL, L"Select &All");
    int x = (short)LOWORD(lparam), y = (short)HIWORD(lparam);
    if (lparam == -1) {
        RECT r;
        GetWindowRect(e->hwnd, &r);
        x = (r.left + r.right) / 2;
        y = (r.top + r.bottom) / 2;
    }
    UINT cmd = (UINT)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, x, y, e->hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd)
        SendMessageW(e->hwnd, cmd, 0, cmd == EM_SETSEL ? -1 : 0);
}

/* ------------------------------------------------------------------ messages */

static LRESULT line_length(EDIT *e, WPARAM index)
{
    if (!e->multi)
        return e->len;
    if (index == (WPARAM)-1) {
        UINT first = line_of(e, e->sel_min), last = line_of(e, e->sel_max);
        if (first == last)
            return (LRESULT)(e->lines[first].end - e->lines[first].start) - (e->sel_max - e->sel_min);
        return (LRESULT)(e->sel_min - e->lines[first].start) +
               (LRESULT)(e->lines[last].end > e->sel_max ? e->lines[last].end - e->sel_max : 0);
    }
    UINT l = line_of(e, umin((UINT)index, e->len));
    return e->lines[l].end - e->lines[l].start;
}

static LRESULT get_line(EDIT *e, WPARAM index, WCHAR *buffer)
{
    if (!buffer)
        return 0;
    UINT size = *(WORD *)buffer;
    UINT start = 0, end = e->len;
    if (e->multi) {
        if (index >= e->line_count) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        start = e->lines[index].start;
        end = e->lines[index].end;
    }
    UINT n = umin(size, end - start);
    memcpy(buffer, T(e) + start, n * sizeof(WCHAR));
    return n;
}

static LRESULT replace_selection(EDIT *e, BOOL can_undo, LPCWSTR s)
{
    UINT n = s ? (UINT)lstrlenW(s) : 0;
    undo_clear(e);
    if (!e->multi) {
        BOOL deleted = delete_selection(e), truncated = FALSE;
        UINT inserted = n ? insert_text(e, s, n, TRUE, &truncated) : 0;
        if (n && !inserted && deleted) {
            undo(e);
        } else {
            if (truncated)
                notify(e, EN_MAXTEXT);
            notify(e, EN_UPDATE);
            scroll_caret(e);
            redraw(e, FALSE);
            place_caret(e);
            notify(e, EN_CHANGE);
            value_changed(e);
        }
    } else {
        if (delete_selection(e))
            changed(e, FALSE);
        if (n && !ml_insert(e, s, n) && e->undo == UNDO_DELETE)
            undo(e);
    }
    if (!can_undo)
        undo_clear(e);
    return 1;
}

static LRESULT set_margins(EDIT *e, WPARAM flags, LPARAM lparam)
{
    int left = e->left_margin, right = e->right_margin;
    BOOL font_info = FALSE;
    if (flags & EC_LEFTMARGIN) {
        short v = (short)LOWORD(lparam);
        if (v < 0) {
            left = 0;
            font_info = TRUE;
        } else {
            left = v;
        }
    }
    if (flags & EC_RIGHTMARGIN) {
        short v = (short)HIWORD(lparam);
        if (v < 0) {
            right = 0;
            font_info = TRUE;
        } else {
            right = v;
        }
    }
    int width = format_width(e) - (left - e->left_margin) - (right - e->right_margin);
    if (font_info && width < 2 * e->ave_width)
        return 0;
    e->fmt.left += left - e->left_margin;
    e->fmt.right -= right - e->right_margin;
    e->left_margin = left;
    e->right_margin = right;
    InvalidateRect(e->hwnd, nullptr, TRUE);
    return 0;
}

static LRESULT set_tab_stops(EDIT *e, WPARAM count, const INT *stops)
{
    if (!e->multi)
        return 0;
    UserFree(e->tabs);
    e->tabs = nullptr;
    e->tab_count = 0;
    if (count) {
        e->tabs = UserAlloc(count * sizeof(int));
        if (!e->tabs)
            return FALSE;
        for (UINT i = 0; i < count; i++)
            e->tabs[i] = (stops[i] * e->ave_width + 2) / 4;
        e->tab_count = (UINT)count;
    }
    build_lines(e);
    scroll_caret(e);
    InvalidateRect(e->hwnd, nullptr, TRUE);
    return TRUE;
}

static LRESULT fmt_lines(EDIT *e, BOOL add)
{
    if (!e->multi)
        return 0;
    WCHAR *t = T(e);
    if (add) {
        if (e->wrap) {
            UINT extra = 0;
            for (UINT l = 0; l + 1 < e->line_count; l++)
                if (e->lines[l].end == e->lines[l + 1].start ||
                    (e->lines[l + 1].start > e->lines[l].end && t[e->lines[l + 1].start - 1] != L'\n'))
                    extra++;
            if (extra && reserve(e, e->len + 3 * extra)) {
                t = T(e);
                for (UINT l = e->line_count - 1; l-- > 0;) {
                    UINT at = e->lines[l + 1].start;
                    if (t[at - 1] == L'\n')
                        continue;
                    memmove(t + at + 3, t + at, (e->len - at) * sizeof(WCHAR));
                    t[at] = L'\r';
                    t[at + 1] = L'\r';
                    t[at + 2] = L'\n';
                    e->len += 3;
                }
            }
        }
    } else {
        UINT w = 0;
        for (UINT r = 0; r < e->len;) {
            if (r + 2 < e->len && t[r] == L'\r' && t[r + 1] == L'\r' && t[r + 2] == L'\n') {
                r += 3;
                continue;
            }
            t[w++] = t[r++];
        }
        e->len = w;
        e->caret = umin(e->caret, w);
        e->sel_min = umin(e->sel_min, w);
        e->sel_max = umin(e->sel_max, w);
    }
    build_lines(e);
    fix_caret_line(e);
    InvalidateRect(e->hwnd, nullptr, TRUE);
    return add;
}

static LRESULT set_handle(EDIT *e, HLOCAL h)
{
    if (!e->multi || !h)
        return 0;
    SIZE_T size = LocalSize(h) / sizeof(WCHAR);
    WCHAR *p = LocalLock(h);
    UINT n = 0;
    while (p && n < size && p[n])
        n++;
    LocalUnlock(h);
    HLOCAL grown = LocalReAlloc(h, ((SIZE_T)n + 32) * sizeof(WCHAR), LMEM_MOVEABLE);
    if (grown)
        h = grown;
    e->handle = h;
    e->capacity = grown ? n + 32 : (UINT)size;
    e->len = n;
    reset(e);
    return 1;
}

static LRESULT wheel(EDIT *e, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!e->multi || (wparam & (MK_SHIFT | MK_CONTROL)))
        return DefWindowProcW(e->hwnd, msg, wparam, lparam);
    int delta = (short)HIWORD(wparam);
    BOOL vbar = has_style(e, WS_VSCROLL), hbar = has_style(e, WS_HSCROLL);
    if (msg == WM_MOUSEWHEEL && vbar) {
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (!lines)
            return 0;
        if (lines == WHEEL_PAGESCROLL)
            lines = (UINT)imax(e->visible_lines - 1, 1);
        int threshold = imax(1, WHEEL_DELTA / (int)lines);
        if ((g_wheel > 0) != (delta > 0))
            g_wheel = 0;
        g_wheel += delta;
        int count = g_wheel / threshold;
        if (count) {
            g_wheel -= count * threshold;
            if (-count > e->visible_lines - 1)
                count = -(e->visible_lines - 1);
            scroll_command(e, TRUE, -1, -count);
        }
        return 0;
    }
    if (hbar && (msg == WM_MOUSEHWHEEL || !vbar)) {
        UINT chars = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &chars, 0);
        int count = delta * (int)chars / WHEEL_DELTA;
        if (msg == WM_MOUSEWHEEL)
            count = -count;
        int cap = format_width(e) / e->ave_width - 1;
        if (count > cap)
            count = cap;
        if (count < -cap)
            count = -cap;
        scroll_command(e, FALSE, -1, count);
        return 0;
    }
    return DefWindowProcW(e->hwnd, msg, wparam, lparam);
}

static void apply_alignment(EDIT *e)
{
    DWORD style = (DWORD)GetWindowLongW(e->hwnd, GWL_STYLE), ex = (DWORD)GetWindowLongW(e->hwnd, GWL_EXSTYLE);
    e->align = style & 3;
    if (e->align == ES_LEFT && (ex & WS_EX_RIGHT))
        e->align = ES_RIGHT;
    e->rtl = (ex & WS_EX_RTLREADING) != 0;
    if (e->rtl && e->align != ES_CENTER)
        e->align = e->align == ES_RIGHT ? ES_LEFT : ES_RIGHT;
}

static LRESULT create(EDIT *e, const CREATESTRUCTW *cs)
{
    DWORD style = cs->style;
    e->auto_h = (style & ES_AUTOHSCROLL) != 0;
    e->nohidesel = (style & ES_NOHIDESEL) != 0;
    apply_alignment(e);
    e->limit = DEFAULT_LIMIT;
    set_font(e, nullptr);
    e->system_ave = e->ave_width;
    if (!e->multi) {
        if (style & ES_PASSWORD) {
            e->password = L'*';
            set_font(e, nullptr);
        }
        e->combo = (style & ES_COMBOBOX) != 0;
    } else {
        e->auto_v = (style & (ES_AUTOVSCROLL | WS_VSCROLL)) != 0;
        if (e->align != ES_LEFT) {
            SetWindowLongW(e->hwnd, GWL_STYLE, (LONG)((DWORD)GetWindowLongW(e->hwnd, GWL_STYLE) & ~WS_HSCROLL));
            e->auto_h = FALSE;
        } else if (style & WS_HSCROLL) {
            e->auto_h = TRUE;
        }
        e->wrap = !has_style(e, WS_HSCROLL) && !e->auto_h;
    }
    set_rect(e, nullptr, FALSE);
    if (e->multi)
        build_lines(e);
    if (cs->lpszName && !IS_INTRESOURCE(cs->lpszName) && cs->lpszName[0] && !set_text(e, cs->lpszName))
        return -1;
    return 0;
}

static BOOL nc_create(HWND hwnd, const CREATESTRUCTW *cs)
{
    EDIT *e = UserAlloc(sizeof(EDIT));
    if (!e)
        return FALSE;
    e->hwnd = hwnd;
    e->parent = cs->hwndParent;
    e->id = (UINT)(UINT_PTR)cs->hMenu;
    e->handle = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, 32 * sizeof(WCHAR));
    if (!e->handle) {
        UserFree(e);
        return FALSE;
    }
    e->capacity = 32;
    e->redraw = TRUE;
    e->multi = (cs->style & ES_MULTILINE) != 0;
    e->disabled = (cs->style & WS_DISABLED) != 0;
    e->readonly = (cs->style & ES_READONLY) != 0;
    if (cs->dwExStyle & (WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE)) {
        e->border = TRUE;
    } else if (cs->style & WS_BORDER) {
        e->border = e->flat = TRUE;
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)((DWORD)GetWindowLongW(hwnd, GWL_STYLE) & ~WS_BORDER));
    }
    SetWindowLongPtrW(hwnd, 0, (LONG_PTR)e);
    return TRUE;
}

static void destroy(EDIT *e)
{
    if (e->focus)
        DestroyCaret();
    LocalFree(e->handle);
    undo_clear(e);
    UserFree(e->tabs);
    UserFree(e->lines);
    SetWindowLongPtrW(e->hwnd, 0, 0);
    UserFree(e);
}

static void set_focus(EDIT *e)
{
    g_wheel = 0;
    if (!e->focus) {
        e->focus = TRUE;
        if (!e->nohidesel && e->sel_min != e->sel_max)
            redraw(e, FALSE);
        create_caret(e);
    }
    notify(e, EN_SETFOCUS);
}

static void kill_focus(EDIT *e, HWND to)
{
    g_wheel = 0;
    if (e->focus) {
        e->focus = FALSE;
        DestroyCaret();
        if (!e->nohidesel && e->sel_min != e->sel_max)
            InvalidateRect(e->hwnd, nullptr, e->multi);
    }
    if (e->combo) {
        if (!to || (to != e->parent && !IsChild(e->parent, to)))
            SendMessageW(e->parent, CBEC_KILLCOMBOFOCUS, 0, 0);
        return;
    }
    notify(e, EN_KILLFOCUS);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    EDIT *e = edit_of(hwnd);
    if (!e) {
        if (msg == WM_NCCREATE) {
            if (!nc_create(hwnd, (const CREATESTRUCTW *)lparam))
                return FALSE;
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_CREATE:
        return create(e, (const CREATESTRUCTW *)lparam);
    case WM_NCDESTROY:
        destroy(e);
        return 0;
    case WM_SIZE:
        set_rect(e, nullptr, TRUE);
        return 0;
    case WM_SETFONT:
        set_font(e, (HFONT)wparam);
        if (e->focus) {
            DestroyCaret();
            create_caret(e);
        }
        if (e->multi && !e->wrap)
            build_lines(e);
        e->fmt_valid = FALSE;
        set_rect(e, nullptr, LOWORD(lparam) != 0);
        return 0;
    case WM_GETFONT:
        return (LRESULT)e->font;
    case WM_SETTEXT:
        return set_text(e, (LPCWSTR)lparam);
    case WM_GETTEXT: {
        WCHAR *out = (WCHAR *)lparam;
        if (!wparam || !out)
            return 0;
        UINT n = umin((UINT)wparam - 1, e->len);
        memcpy(out, T(e), n * sizeof(WCHAR));
        out[n] = 0;
        return n;
    }
    case WM_GETTEXTLENGTH:
        return e->len;
    case WM_CUT:
        if (!e->readonly && e->sel_min != e->sel_max && copy(e))
            clear(e);
        return 0;
    case WM_COPY:
        return copy(e);
    case WM_PASTE:
        paste(e);
        return e->multi;
    case WM_CLEAR:
        clear(e);
        return 0;
    case WM_UNDO:
    case EM_UNDO:
        return undo(e);
    case WM_CHAR: {
        if (e->selecting)
            return 0;
        UINT repeat = umax(LOWORD(lparam), 1);
        for (UINT i = 0; i < repeat; i++) {
            if (e->multi)
                ml_char(e, (WCHAR)wparam);
            else
                sl_char(e, (WCHAR)wparam);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (e->selecting)
            return 0;
        if (e->multi)
            ml_key(e, (UINT)wparam);
        else
            sl_key(e, (UINT)wparam, lparam);
        return 0;
    case WM_SYSKEYDOWN:
        if (wparam == VK_BACK && (lparam & 0x20000000)) {
            SendMessageW(hwnd, e->multi ? EM_UNDO : WM_UNDO, 0, 0);
            return 0;
        }
        if (e->combo && (wparam == VK_UP || wparam == VK_DOWN) && (lparam & 0x20000000))
            return SendMessageW(e->parent, WM_SYSKEYDOWN, wparam, lparam);
        break;
    case WM_SYSCHAR:
        if (wparam == VK_BACK && (lparam & 0x20000000))
            return 1;
        break;
    case WM_SETFOCUS:
        set_focus(e);
        return 0;
    case WM_KILLFOCUS:
        kill_focus(e, (HWND)wparam);
        return 0;
    case WM_ENABLE:
        e->disabled = !wparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_ERASEBKGND:
        if (e->multi) {
            RECT r;
            GetClientRect(hwnd, &r);
            FillRect((HDC)wparam, &r, control_brush(e, (HDC)wparam));
        }
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT && !wparam ? BeginPaint(hwnd, &ps) : (HDC)wparam;
        if (dc)
            paint(e, dc, msg == WM_PAINT && !wparam ? &ps.rcPaint : nullptr);
        if (msg == WM_PAINT && !wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETREDRAW:
        e->redraw = wparam != 0;
        if (e->multi && wparam) {
            DefWindowProcW(hwnd, msg, wparam, lparam);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
            return 0;
        }
        break;
    case WM_VSCROLL:
    case WM_HSCROLL: {
        if (!e->multi)
            break;
        int code = LOWORD(wparam), pos = HIWORD(wparam);
        if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) {
            SCROLLINFO si = {sizeof(si), SIF_TRACKPOS};
            if (GetScrollInfo(hwnd, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, &si))
                pos = si.nTrackPos;
        }
        return scroll_command(e, msg == WM_VSCROLL, code, pos);
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return wheel(e, msg, wparam, lparam);
    case WM_STYLECHANGED: {
        BOOL rtl = e->rtl;
        apply_alignment(e);
        if (rtl != e->rtl) {
            if (e->multi)
                build_lines(e);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_GETDLGCODE: {
        LRESULT code = DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS;
        const MSG *m = (const MSG *)lparam;
        if (e->multi) {
            if (m)
                e->in_dialog = TRUE;
            return code | DLGC_WANTALLKEYS;
        }
        if (m && ((m->message == WM_KEYDOWN && (m->wParam == VK_ESCAPE || m->wParam == VK_RETURN) &&
                   combo_dropped(e)) ||
                  (m->message == WM_SYSCHAR && m->wParam == VK_BACK)))
            code |= DLGC_WANTALLKEYS;
        return code;
    }
    case WM_LBUTTONDOWN:
        button_down(e, (short)LOWORD(lparam), (short)HIWORD(lparam), (wparam & MK_SHIFT) != 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        double_click(e, (short)LOWORD(lparam), (short)HIWORD(lparam), (wparam & MK_SHIFT) != 0);
        return 0;
    case WM_MOUSEMOVE:
        if (!e->selecting)
            return 1;
        drag(e, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_LBUTTONUP:
        end_selecting(e);
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lparam != hwnd && e->selecting) {
            e->selecting = FALSE;
            if (e->multi)
                KillTimer(hwnd, SCROLL_TIMER);
        }
        return 0;
    case WM_TIMER:
        if (wparam == SCROLL_TIMER) {
            if (e->selecting)
                drag(e, e->last_mouse.x, e->last_mouse.y);
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        e->rbutton = TRUE;
        return 0;
    case WM_RBUTTONUP:
        if (!e->rbutton)
            return 0;
        e->rbutton = FALSE;
        break;
    case WM_CONTEXTMENU: {
        if (lparam != -1) {
            POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            LRESULT hit = SendMessageW(hwnd, WM_NCHITTEST, 0, lparam);
            if (hit == HTVSCROLL || hit == HTHSCROLL)
                break;
            (void)pt;
        }
        context_menu(e, lparam);
        return 0;
    }

    case EM_GETSEL:
        if (wparam)
            *(DWORD *)wparam = e->sel_min;
        if (lparam)
            *(DWORD *)lparam = e->sel_max;
        return MAKELONG(e->sel_min, e->sel_max);
    case EM_SETSEL: {
        if ((int)wparam == -1) {
            set_selection(e, e->caret, e->caret);
            return 1;
        }
        UINT start = umin((UINT)wparam, e->len), end = umin((UINT)lparam, e->len);
        if (!e->multi) {
            set_selection(e, umin(start, end), umax(start, end));
            scroll_caret(e);
            redraw(e, FALSE);
        } else {
            set_selection(e, start, end);
        }
        place_caret(e);
        return 1;
    }
    case EM_GETRECT:
        if (lparam)
            *(RECT *)lparam = e->fmt;
        return 1;
    case EM_SETRECT:
    case EM_SETRECTNP:
        if (!e->multi)
            break;
        set_rect(e, (const RECT *)lparam, msg == EM_SETRECT);
        return 0;
    case EM_SCROLL:
        if (!e->multi)
            break;
        return scroll_command(e, TRUE, (int)wparam, 0);
    case EM_LINESCROLL:
        if (!e->multi)
            return FALSE;
        scroll_command(e, TRUE, -1, (int)lparam);
        scroll_command(e, FALSE, -1, (int)wparam);
        return TRUE;
    case EM_SCROLLCARET:
        if (!e->multi)
            break;
        scroll_caret(e);
        place_caret(e);
        return 1;
    case EM_GETMODIFY:
        return e->modified;
    case EM_SETMODIFY:
        e->modified = wparam != 0;
        return 1;
    case EM_GETLINECOUNT:
        return e->multi ? e->line_count : 1;
    case EM_LINEINDEX:
        if (!e->multi)
            return 0;
        if ((int)wparam == -1)
            return e->lines[e->caret_line].start;
        if (wparam >= e->line_count) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return -1;
        }
        return e->lines[wparam].start;
    case EM_SETHANDLE:
        return set_handle(e, (HLOCAL)wparam);
    case EM_GETHANDLE:
        if (!e->multi)
            return 0;
        if (reserve(e, e->len))
            T(e)[e->len] = 0;
        return (LRESULT)e->handle;
    case EM_GETTHUMB:
        return 0;
    case EM_LINELENGTH:
        return line_length(e, wparam);
    case EM_REPLACESEL:
        return replace_selection(e, (BOOL)wparam, (LPCWSTR)lparam);
    case EM_GETLINE:
        return get_line(e, wparam, (WCHAR *)lparam);
    case EM_SETLIMITTEXT:
        if (!e->multi)
            e->limit = !wparam || wparam > 0x7FFFFFFE ? 0x7FFFFFFE : (UINT)wparam;
        else
            e->limit = wparam ? (UINT)wparam : 0xFFFFFFFF;
        return 1;
    case EM_GETLIMITTEXT:
        return e->limit;
    case EM_CANUNDO:
        return e->undo != 0;
    case EM_FMTLINES:
        if (!e->multi)
            break;
        return fmt_lines(e, wparam != 0);
    case EM_LINEFROMCHAR:
        if (!e->multi)
            return 0;
        return line_of(e, (int)wparam == -1 ? e->sel_min : umin((UINT)wparam, e->len));
    case EM_SETTABSTOPS:
        return set_tab_stops(e, wparam, (const INT *)lparam);
    case EM_SETPASSWORDCHAR: {
        e->password = (WCHAR)wparam;
        set_font(e, e->font);
        DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)(e->password ? style | ES_PASSWORD : style & ~(DWORD)ES_PASSWORD));
        return 0;
    }
    case EM_GETPASSWORDCHAR:
        return e->password;
    case EM_EMPTYUNDOBUFFER:
        undo_clear(e);
        return 1;
    case EM_GETFIRSTVISIBLELINE:
        return e->multi ? e->first_line : 0;
    case EM_SETREADONLY: {
        e->readonly = wparam != 0;
        DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)(wparam ? style | ES_READONLY : style & ~(DWORD)ES_READONLY));
        InvalidateRect(hwnd, nullptr, TRUE);
        return 1;
    }
    case EM_SETWORDBREAKPROC:
        e->word_proc = (EDITWORDBREAKPROCW)lparam;
        return 1;
    case EM_GETWORDBREAKPROC:
        return (LRESULT)e->word_proc;
    case EM_SETMARGINS:
        return set_margins(e, wparam, lparam);
    case EM_GETMARGINS:
        return MAKELONG(e->left_margin, e->right_margin);
    case EM_POSFROMCHAR: {
        if (wparam >= e->len)
            return -1;
        HGDIOBJ old;
        HDC dc = text_dc(e, &old);
        UINT l = line_of(e, (UINT)wparam);
        POINT p = position_on(e, dc, l, (UINT)wparam);
        release_dc(e, dc, old);
        return e->multi ? MAKELONG(p.x, p.y) : MAKELONG(p.x, 0);
    }
    case EM_CHARFROMPOS: {
        RECT client;
        GetClientRect(hwnd, &client);
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        if (!PtInRect(&client, pt))
            return -1;
        UINT l;
        UINT index = index_at(e, pt.x, pt.y, &l);
        return e->multi ? MAKELONG(index, l) : MAKELONG(index, 0);
    }
    case EM_SETIMESTATUS:
        if (wparam == EMSIS_COMPOSITIONSTRING)
            e->ime_status = (DWORD)lparam;
        return 1;
    case EM_GETIMESTATUS:
        return wparam == EMSIS_COMPOSITIONSTRING ? e->ime_status : 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
