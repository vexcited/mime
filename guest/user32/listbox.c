/*
 * the ListBox and ComboLBox classes, as user32 of build 26300 has them:
 * items with strings, data or neither, one or many selected, owner drawn
 * at a fixed or variable height, in columns, with type-ahead, and the
 * list of a combo box tracking the mouse while dropped.
 */
#include "listbox.h"

#define TIMER_SCROLL 0xFFFE
#define TIMER_TYPEAHEAD 0xFFFC
#define INSERT_AT 0
#define INSERT_ADD 1

LISTBOX *ListBoxOf(HWND hwnd)
{
    return (LISTBOX *)GetWindowLongPtrW(hwnd, 0);
}

static int id_of(LISTBOX *lb)
{
    return (int)GetWindowLongPtrW(lb->hwnd, GWLP_ID);
}

static void notify(LISTBOX *lb, int code)
{
    SendMessageW(lb->parent, WM_COMMAND, MAKEWPARAM(id_of(lb), code), (LPARAM)lb->hwnd);
}

static BOOL shown(LISTBOX *lb)
{
    return lb->redraw && IsWindowVisible(lb->hwnd);
}

static BOOL item_selected(LISTBOX *lb, int i)
{
    if (i < 0 || i >= lb->count)
        return FALSE;
    return lb->mode == LB_SINGLE ? i == lb->sel : (lb->state[i] & 0x0F) != 0;
}

static BOOL item_shown_selected(LISTBOX *lb, int i)
{
    if (i < 0 || i >= lb->count || lb->nosel)
        return FALSE;
    return lb->mode == LB_SINGLE ? i == lb->sel : (lb->state[i] & 0xF0) != 0;
}

/* the droppable combo box this list belongs to, with its extended keyboard while closed. */
static BOOL closed_ext_ui(LISTBOX *lb)
{
    COMBO *cb = lb->combo;
    return cb && cb->droppable && cb->extended_ui && !cb->list_visible;
}

/* ------------------------------------------------------------------ metrics */

static int system_font_height(void)
{
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return tm.tmHeight;
}

/* the average character of a font, as dialogs measure it. */
static void char_size(HWND hwnd, HFONT font, int *cx, int *cy)
{
    static const WCHAR LETTERS[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW tm;
    SIZE size;
    if (GetTextMetricsW(dc, &tm) && GetTextExtentPoint32W(dc, LETTERS, 52, &size)) {
        *cx = (size.cx / 26 + 1) / 2;
        *cy = tm.tmHeight;
    } else {
        *cx = 8;
        *cy = 16;
    }
    if (old)
        SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
}

int ListBoxItemHeight(LISTBOX *lb, int index)
{
    if (lb->owner_draw != 2)
        return lb->cy_char;
    return index >= 0 && index < lb->count ? lb->heights[index] : 0;
}

static void client_rect(LISTBOX *lb, RECT *r)
{
    GetClientRect(lb->hwnd, r);
}

/* the layout of columns and how many items a page holds. */
static void layout(LISTBOX *lb)
{
    RECT r;
    client_rect(lb, &r);
    int h = r.bottom, w = r.right;
    if (lb->multicolumn) {
        lb->per_column = lb->cy_char > 0 ? h / lb->cy_char : 1;
        if (lb->per_column < 1)
            lb->per_column = 1;
        lb->columns = lb->cx_column > 0 ? w / lb->cx_column : 1;
        if (lb->columns < 1)
            lb->columns = 1;
        lb->full_max = lb->per_column * lb->columns;
    } else if (lb->owner_draw == 2) {
        int n = 0, sum = 0;
        for (int i = lb->count - 1; i >= 0; i--) {
            sum += lb->heights[i];
            if (sum > h)
                break;
            n++;
        }
        lb->full_max = n ? n : 1;
    } else {
        lb->full_max = lb->cy_char > 0 ? h / lb->cy_char : 1;
    }
}

/* how many items show from the top, partly shown ones counted when asked. */
static int visible_count(LISTBOX *lb, BOOL partial)
{
    RECT r;
    client_rect(lb, &r);
    if (lb->multicolumn) {
        int columns = lb->cx_column > 0 ? r.right / lb->cx_column : 1;
        if (partial && lb->cx_column > 0 && r.right % lb->cx_column)
            columns++;
        return lb->per_column * (columns < 1 ? 1 : columns);
    }
    if (lb->owner_draw == 2) {
        int n = 0, y = 0;
        for (int i = lb->top; i < lb->count; i++) {
            y += lb->heights[i];
            if (y > r.bottom && !partial)
                break;
            n++;
            if (y >= r.bottom)
                break;
        }
        return n;
    }
    if (lb->cy_char <= 0)
        return 0;
    return r.bottom / lb->cy_char + (partial && r.bottom % lb->cy_char ? 1 : 0);
}

static int last_full_visible(LISTBOX *lb)
{
    if (lb->multicolumn || lb->owner_draw == 2) {
        int n = visible_count(lb, FALSE);
        return lb->top + (n ? n - 1 : 0);
    }
    int last = (lb->top + lb->full_max < lb->count ? lb->top + lb->full_max : lb->count) - 1;
    return last < lb->top ? lb->top : last;
}

/* an item's rectangle, in the coordinates of a dc shifted by the horizontal scroll; whether it shows. */
static BOOL item_rect(LISTBOX *lb, int index, RECT *rect)
{
    RECT r;
    client_rect(lb, &r);
    if (lb->multicolumn) {
        int column = index / lb->per_column - lb->top / lb->per_column;
        rect->top = lb->cy_char * (index % lb->per_column);
        rect->bottom = rect->top + lb->cy_char;
        rect->left = lb->cx_column * column;
        rect->right = rect->left + lb->cx_column;
        if (lb->right) {
            int left = r.right - rect->right;
            rect->right = r.right - rect->left;
            rect->left = left;
        }
    } else if (lb->owner_draw == 2) {
        int y = 0;
        if (index >= lb->top)
            for (int i = lb->top; i < index; i++)
                y += lb->heights[i];
        else
            for (int i = index; i < lb->top; i++)
                y -= lb->heights[i];
        rect->left = 0;
        rect->right = r.right + lb->x_origin;
        rect->top = y;
        rect->bottom = y + ListBoxItemHeight(lb, index);
    } else {
        rect->left = 0;
        rect->right = r.right + lb->x_origin;
        rect->top = lb->cy_char * (index - lb->top);
        rect->bottom = rect->top + lb->cy_char;
    }
    return index >= lb->top && index < lb->top + visible_count(lb, TRUE);
}

static void invalidate_item(LISTBOX *lb, int index)
{
    if (!shown(lb) || index < 0 || index >= lb->count) {
        lb->deferred = TRUE;
        return;
    }
    RECT r;
    if (item_rect(lb, index, &r)) {
        OffsetRect(&r, -lb->x_origin, 0);
        InvalidateRect(lb->hwnd, &r, TRUE);
    }
}

static void invalidate_all(LISTBOX *lb)
{
    if (shown(lb))
        InvalidateRect(lb->hwnd, nullptr, TRUE);
    else
        lb->deferred = TRUE;
}

/* the item under a client point, and whether the point is outside every item. */
static int hit_test(LISTBOX *lb, int x, int y, BOOL *outside)
{
    RECT r;
    client_rect(lb, &r);
    *outside = FALSE;
    if (y < 0) {
        *outside = TRUE;
        return lb->top;
    }
    if (y > r.bottom) {
        y = r.bottom;
        *outside = TRUE;
    }
    if (x < 0 || x > r.right)
        *outside = TRUE;
    int index;
    if (lb->multicolumn) {
        int column = lb->cx_column > 0 ? (lb->right ? r.right - x : x) / lb->cx_column : 0;
        if (y < lb->per_column * lb->cy_char) {
            index = lb->top + y / lb->cy_char + column * lb->per_column;
        } else {
            *outside = TRUE;
            index = lb->top + column * lb->per_column + lb->per_column - 1;
        }
    } else if (lb->owner_draw == 2) {
        index = lb->count - 1;
        int top = 0;
        for (int i = lb->top; i < lb->count; i++) {
            int bottom = top + lb->heights[i];
            if (y >= top && y < bottom) {
                index = i;
                break;
            }
            top = bottom;
        }
        if (index == lb->count - 1 && y >= top)
            *outside = TRUE;
    } else {
        index = lb->top + (lb->cy_char > 0 ? y / lb->cy_char : 0);
    }
    if (index > lb->count - 1) {
        index = lb->count - 1;
        *outside = TRUE;
    }
    return index;
}

/* ------------------------------------------------------------------ scrolling */

static int max_top(LISTBOX *lb)
{
    if (lb->multicolumn) {
        int needed = (lb->count + lb->per_column - 1) / lb->per_column;
        int column = needed - lb->columns;
        return (column > 0 ? column : 0) * lb->per_column;
    }
    int top = lb->count - lb->full_max;
    return top > 0 ? top : 0;
}

static int clamp_top(LISTBOX *lb, int top)
{
    int max = max_top(lb);
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    if (lb->multicolumn)
        top -= top % lb->per_column;
    return top;
}

static void set_bar(LISTBOX *lb, int bar, int max, int page, int pos)
{
    int *last = bar == SB_VERT ? lb->last_vbar : lb->last_hbar;
    if (last[0] == max && last[1] == page && last[2] == pos)
        return;
    last[0] = max;
    last[1] = page;
    last[2] = pos;
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | (lb->disable_noscroll ? SIF_DISABLENOSCROLL : 0)};
    si.nMin = 0;
    si.nMax = max;
    si.nPage = (UINT)page;
    si.nPos = pos;
    SetScrollInfo(lb->hwnd, bar, &si, lb->redraw);
}

/* the scroll bars of the styles the list was made with, after its items or size changed. */
static void update_bars(LISTBOX *lb)
{
    if ((!lb->vbar && !lb->hbar) || !lb->redraw)
        return;
    RECT r;
    client_rect(lb, &r);
    if (!lb->in_insert)
        lb->top = clamp_top(lb, lb->top);
    if (lb->multicolumn) {
        if (lb->hbar) {
            int needed = (lb->count + lb->per_column - 1) / lb->per_column;
            set_bar(lb, SB_HORZ, needed - 1, lb->columns, lb->top / lb->per_column);
        }
        return;
    }
    int max_x = lb->extent - r.right;
    if (lb->x_origin > max_x)
        lb->x_origin = max_x > 0 ? max_x : 0;
    if (lb->vbar)
        set_bar(lb, SB_VERT, lb->count - 1, lb->full_max, lb->top);
    if (lb->hbar)
        set_bar(lb, SB_HORZ, lb->extent - 1, r.right, lb->x_origin);
}

static void draw_focus(LISTBOX *lb, BOOL on);

static void set_top(LISTBOX *lb, int top)
{
    if (!lb->count)
        return;
    top = clamp_top(lb, top);
    if (top == lb->top)
        return;
    int old = lb->top;
    BOOL caret = lb->caret_on;
    if (caret)
        draw_focus(lb, FALSE);
    lb->top = top;
    if (shown(lb)) {
        RECT r;
        client_rect(lb, &r);
        int dx = 0, dy = 0;
        if (lb->multicolumn) {
            dx = (old / lb->per_column - top / lb->per_column) * lb->cx_column;
            if (lb->right)
                dx = -dx;
        } else if (lb->owner_draw == 2) {
            dy = old - top == 1 ? lb->heights[top] : old - top == -1 ? -lb->heights[old] : 32000;
        } else {
            dy = (old - top) * lb->cy_char;
        }
        if (dy == 32000 || dx > r.right || -dx > r.right || dy > r.bottom || -dy > r.bottom)
            InvalidateRect(lb->hwnd, nullptr, TRUE);
        else
            ScrollWindowEx(lb->hwnd, dx, dy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_ERASE);
        UpdateWindow(lb->hwnd);
    } else {
        lb->deferred = TRUE;
    }
    if (lb->vbar && !lb->multicolumn)
        set_bar(lb, SB_VERT, lb->count - 1, lb->full_max, lb->top);
    if (lb->hbar && lb->multicolumn) {
        int needed = (lb->count + lb->per_column - 1) / lb->per_column;
        set_bar(lb, SB_HORZ, needed - 1, lb->columns, lb->top / lb->per_column);
    }
    if (caret)
        draw_focus(lb, TRUE);
}

static void set_x_origin(LISTBOX *lb, int x)
{
    RECT r;
    client_rect(lb, &r);
    int max = lb->extent - r.right;
    if (x > max)
        x = max;
    if (x < 0)
        x = 0;
    if (x == lb->x_origin)
        return;
    BOOL caret = lb->caret_on;
    if (caret)
        draw_focus(lb, FALSE);
    int dx = lb->x_origin - x;
    lb->x_origin = x;
    if (shown(lb)) {
        ScrollWindowEx(lb->hwnd, dx, 0, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_ERASE);
        UpdateWindow(lb->hwnd);
    }
    if (lb->hbar)
        set_bar(lb, SB_HORZ, lb->extent - 1, r.right, lb->x_origin);
    if (caret)
        draw_focus(lb, TRUE);
}

/* the top of the page ending at an item of a variable list. */
static int page_up_top(LISTBOX *lb, int index)
{
    RECT r;
    client_rect(lb, &r);
    if (lb->count <= 1)
        return 0;
    int sum = 0, k;
    for (k = index; k >= 0; k--) {
        sum += lb->heights[k];
        if (sum > r.bottom)
            break;
    }
    if (k < 0)
        return 0;
    return k + 1 < index - 1 ? k + 1 : index - 1;
}

static int page_down_top(LISTBOX *lb, int index)
{
    RECT r;
    client_rect(lb, &r);
    if (lb->count <= 1)
        return 0;
    int sum = 0, k;
    for (k = index; k < lb->count; k++) {
        sum += lb->heights[k];
        if (sum > r.bottom)
            break;
    }
    if (k >= lb->count)
        return lb->count - 1;
    return index + 1 > k - 1 ? index + 1 : k - 1;
}

static void make_visible(LISTBOX *lb, int index, BOOL partial)
{
    if (index < 0 || index >= lb->count)
        return;
    if (index < lb->top) {
        set_top(lb, index);
        return;
    }
    int last = partial ? lb->top + visible_count(lb, TRUE) - 1 : last_full_visible(lb);
    if (index <= last)
        return;
    if (lb->multicolumn) {
        int columns = lb->columns - 1;
        set_top(lb, lb->per_column * (index / lb->per_column - (columns > 0 ? columns : 0)));
    } else if (lb->owner_draw == 2) {
        set_top(lb, page_up_top(lb, index));
    } else {
        set_top(lb, index - (last - lb->top));
    }
}

/* ------------------------------------------------------------------ painting */

static HWND color_target(LISTBOX *lb)
{
    HWND parent = GetParent(lb->hwnd);
    return parent && parent != GetDesktopWindow() ? parent : lb->parent;
}

/* the brush the parent gives, with its colors in the dc. */
static HBRUSH list_brush(LISTBOX *lb, HDC dc)
{
    SetBkMode(dc, OPAQUE);
    HBRUSH brush = (HBRUSH)SendMessageW(color_target(lb), WM_CTLCOLORLISTBOX, (WPARAM)dc, (LPARAM)lb->hwnd);
    if (!brush) {
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        brush = GetSysColorBrush(COLOR_WINDOW);
    }
    return brush;
}

static UINT ui_state_bits(LISTBOX *lb)
{
    LRESULT ui = SendMessageW(lb->hwnd, WM_QUERYUISTATE, 0, 0);
    return ((ui & UISF_HIDEFOCUS) ? ODS_NOFOCUSRECT : 0) | ((ui & UISF_HIDEACCEL) ? ODS_NOACCEL : 0);
}

static void draw_item_message(LISTBOX *lb, HDC dc, int index, UINT action, UINT state)
{
    DRAWITEMSTRUCT dis = {ODT_LISTBOX, (UINT)id_of(lb)};
    dis.itemID = index < lb->count ? (UINT)index : (UINT)-1;
    dis.itemAction = action;
    dis.itemState = state | (IsWindowEnabled(lb->hwnd) ? 0 : ODS_DISABLED) | ui_state_bits(lb);
    dis.hwndItem = lb->hwnd;
    dis.hDC = dc;
    item_rect(lb, index, &dis.rcItem);
    dis.itemData = index < lb->count && lb->has_data ? lb->data[index] : 0;
    SendMessageW(lb->parent, WM_DRAWITEM, (WPARAM)dis.CtlID, (LPARAM)&dis);
}

/* an item of a list without owner drawing: its text in the highlight or the list's colors. */
static void draw_text_item(LISTBOX *lb, HDC dc, int index, BOOL selected, HBRUSH brush)
{
    RECT r;
    item_rect(lb, index, &r);
    COLORREF text = GetTextColor(dc), back = GetBkColor(dc);
    if (selected) {
        FillRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT));
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else if (index == lb->caret || lb->use_tabs) {
        FillRect(dc, &r, brush);
    }
    if (!IsWindowEnabled(lb->hwnd))
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    LPCWSTR s = lb->text && lb->text[index] ? lb->text[index] : L"";
    int n = (int)wcslen(s);
    int x = lb->use_tabs ? 0 : 2;
    UINT align = GetTextAlign(dc);
    if (lb->right) {
        SetTextAlign(dc, TA_RIGHT | (lb->rtl ? TA_RTLREADING : 0));
        x = r.right - r.left - x;
    } else if (lb->rtl) {
        SetTextAlign(dc, TA_RTLREADING);
    }
    if (lb->use_tabs) {
        int stops[1] = {lb->cx_char * 8};
        TabbedTextOutW(dc, r.left + x, r.top, s, n, lb->tab_count ? lb->tab_count : 1,
                       lb->tab_count ? lb->tabs : stops, lb->right ? r.right : r.left);
    } else if (lb->mode != LB_SINGLE) {
        ExtTextOutW(dc, r.left + x, r.top, ETO_OPAQUE, &r, s, (UINT)n, nullptr);
    } else if (lb->multicolumn) {
        ExtTextOutW(dc, r.left + x, r.top, ETO_CLIPPED, &r, s, (UINT)n, nullptr);
    } else {
        ExtTextOutW(dc, r.left + x, r.top, 0, nullptr, s, (UINT)n, nullptr);
        /* the type-ahead caret after the typed prefix of the selected item. */
        if (lb->typed_count && selected && !lb->owner_draw) {
            SIZE size;
            GetTextExtentPoint32W(dc, s, lb->typed_count < n ? lb->typed_count : n, &size);
            PatBlt(dc, r.left + x + size.cx - 1, r.top + 1, 1, r.bottom - r.top - 2, DSTINVERT);
        }
    }
    SetTextAlign(dc, align);
    SetTextColor(dc, text);
    SetBkColor(dc, back);
}

/* a dc for drawing outside WM_PAINT, with the font, clip and horizontal scroll of painting. */
static HDC list_dc(LISTBOX *lb, HGDIOBJ *old_font)
{
    HDC dc = GetDC(lb->hwnd);
    RECT r;
    client_rect(lb, &r);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    SetWindowOrgEx(dc, lb->x_origin, 0, nullptr);
    *old_font = lb->font ? SelectObject(dc, lb->font) : nullptr;
    return dc;
}

static void release_list_dc(LISTBOX *lb, HDC dc, HGDIOBJ old_font)
{
    if (old_font)
        SelectObject(dc, old_font);
    ReleaseDC(lb->hwnd, dc);
}

static void focus_in_dc(LISTBOX *lb, HDC dc, BOOL on)
{
    int index = lb->caret;
    if (lb->owner_draw) {
        UINT state = (on ? ODS_FOCUS : 0) | (item_shown_selected(lb, index) ? ODS_SELECTED : 0);
        draw_item_message(lb, dc, index, ODA_FOCUS, state);
        return;
    }
    if (index < 0 || index >= lb->count || (SendMessageW(lb->hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
        return;
    RECT r;
    if (!item_rect(lb, index, &r))
        return;
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawFocusRect(dc, &r);
}

/* the focus rectangle, an xor, toggled to the state asked. */
static void draw_focus(LISTBOX *lb, BOOL on)
{
    if (on == lb->caret_on)
        return;
    if (on && (!lb->caret_enabled || !shown(lb)))
        return;
    lb->caret_on = on;
    if (!shown(lb))
        return;
    HGDIOBJ old;
    HDC dc = list_dc(lb, &old);
    focus_in_dc(lb, dc, on);
    release_list_dc(lb, dc, old);
}

/* one item repainted as selected or not, without the rest. */
static void invert_item(LISTBOX *lb, int index, BOOL on)
{
    if (lb->nosel || !shown(lb) || index < 0 || index >= lb->count)
        return;
    RECT r;
    if (!item_rect(lb, index, &r))
        return;
    HGDIOBJ old;
    HDC dc = list_dc(lb, &old);
    BOOL caret = lb->caret_on && index == lb->caret;
    if (caret && !lb->owner_draw)
        focus_in_dc(lb, dc, FALSE);
    HBRUSH brush = list_brush(lb, dc);
    if (lb->owner_draw) {
        draw_item_message(lb, dc, index, ODA_SELECT, on ? ODS_SELECTED : 0);
    } else {
        if (!on)
            FillRect(dc, &r, brush);
        draw_text_item(lb, dc, index, on, brush);
    }
    if (caret && !lb->owner_draw)
        focus_in_dc(lb, dc, TRUE);
    release_list_dc(lb, dc, old);
}

static void paint(LISTBOX *lb, HDC dc, const RECT *update)
{
    RECT r;
    client_rect(lb, &r);
    HGDIOBJ old_font = lb->font ? SelectObject(dc, lb->font) : nullptr;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    SetWindowOrgEx(dc, lb->x_origin, 0, nullptr);
    BOOL caret = lb->caret_on;
    if (caret)
        focus_in_dc(lb, dc, FALSE);
    HBRUSH brush = list_brush(lb, dc);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    if (!lb->count) {
        RECT all = r;
        OffsetRect(&all, lb->x_origin, 0);
        FillRect(dc, &all, brush);
    }
    int last = lb->top + visible_count(lb, TRUE);
    if (last > lb->count)
        last = lb->count;
    for (int i = lb->top; i < last; i++) {
        RECT ir, clip;
        item_rect(lb, i, &ir);
        OffsetRect(&ir, -lb->x_origin, 0);
        if (update && !IntersectRect(&clip, &ir, update))
            continue;
        BOOL selected = item_shown_selected(lb, i);
        if (lb->owner_draw)
            draw_item_message(lb, dc, i, ODA_DRAWENTIRE, selected ? ODS_SELECTED : 0);
        else
            draw_text_item(lb, dc, i, selected, brush);
    }
    if (caret)
        focus_in_dc(lb, dc, TRUE);
    SelectObject(dc, old_brush);
    RestoreDC(dc, saved);
    if (old_font)
        SelectObject(dc, old_font);
}

/* ------------------------------------------------------------------ selection */

static void set_state(LISTBOX *lb, int i, BOOL selected, BOOL displayed)
{
    lb->state[i] = (BYTE)((selected ? 1 : 0) | (displayed ? 0x10 : 0));
}

/* every item outside a range to a value, the single selection dropped when outside. */
static void reset_world(LISTBOX *lb, int a, int b, BOOL value)
{
    int lo = a < b ? a : b, hi = a < b ? b : a;
    if (lb->mode == LB_SINGLE) {
        if (lb->sel >= 0 && (lb->sel < lo || lb->sel > hi)) {
            invert_item(lb, lb->sel, FALSE);
            lb->sel = -1;
        }
        return;
    }
    BOOL caret = lb->caret_on;
    if (caret)
        draw_focus(lb, FALSE);
    for (int i = 0; i < lb->count; i++) {
        if (i >= lo && i <= hi)
            continue;
        BOOL was = (lb->state[i] & 0xF0) != 0;
        set_state(lb, i, value, value);
        if (was != value)
            invert_item(lb, i, value);
    }
    if (caret)
        draw_focus(lb, TRUE);
}

enum { HILITE_SHOWN = 1, HILITE_SELECTED = 2 };

/* the items between two, the first excluded, given a state. */
static void alter_hilite(LISTBOX *lb, int i, int j, BOOL value, int which, BOOL restore)
{
    if (i < 0 || j < 0)
        return;
    int lo = i < j ? i : j, hi = i < j ? j : i;
    for (int k = lo; k <= hi && k < lb->count; k++) {
        if (k == i)
            continue;
        if (which & HILITE_SHOWN) {
            BOOL shown_value = restore ? (lb->state[k] & 0x0F) != 0 : value;
            BOOL was = (lb->state[k] & 0xF0) != 0;
            lb->state[k] = (BYTE)((lb->state[k] & 0x0F) | (shown_value ? 0x10 : 0));
            if (was != shown_value)
                invert_item(lb, k, shown_value);
        }
        if (which & HILITE_SELECTED)
            lb->state[k] = (BYTE)((lb->state[k] & 0xF0) | (value ? 1 : 0));
    }
}

/* the extended block from the anchor moved to a new end, committed or only shown. */
static void block_hilite(LISTBOX *lb, int end, BOOL commit)
{
    int which = HILITE_SHOWN | (commit ? HILITE_SELECTED : 0);
    int anchor = lb->anchor, last = lb->last_move;
    if (anchor < 0)
        return;
    if (last < 0)
        last = anchor;
    if ((last - anchor) * (end - anchor) < 0) {
        alter_hilite(lb, anchor, last, lb->new_state, which, TRUE);
        alter_hilite(lb, anchor, end, lb->new_state, which, FALSE);
    } else if ((end > last && last >= anchor) || (end < last && last <= anchor)) {
        alter_hilite(lb, last, end, lb->new_state, which, FALSE);
    } else {
        alter_hilite(lb, end, last, lb->new_state, which, TRUE);
    }
    if (anchor < lb->count) {
        BOOL was = (lb->state[anchor] & 0xF0) != 0;
        set_state(lb, anchor, commit ? lb->new_state : (lb->state[anchor] & 0x0F) != 0, lb->new_state);
        if (was != lb->new_state)
            invert_item(lb, anchor, lb->new_state);
    }
    lb->last_move = end;
}

static void set_one(LISTBOX *lb, int i, BOOL value)
{
    BOOL was = (lb->state[i] & 0xF0) != 0;
    set_state(lb, i, value, value);
    if (was != value)
        invert_item(lb, i, value);
}

static void move_caret(LISTBOX *lb, int index)
{
    if (index == lb->caret)
        return;
    BOOL on = lb->caret_on;
    if (on)
        draw_focus(lb, FALSE);
    lb->caret = index;
    if (on)
        draw_focus(lb, TRUE);
    if (IsWindowVisible(lb->hwnd) || GetFocus() == lb->hwnd)
        NotifyWinEvent(EVENT_OBJECT_FOCUS, lb->hwnd, OBJID_CLIENT, index + 1);
}

/* the single selection moved to an item, or none. */
static void select_single(LISTBOX *lb, int index)
{
    if (index == lb->sel)
        return;
    if (lb->sel >= 0)
        invert_item(lb, lb->sel, FALSE);
    lb->sel = index;
    if (index >= 0)
        invert_item(lb, index, TRUE);
    NotifyWinEvent(EVENT_OBJECT_SELECTION, lb->hwnd, OBJID_CLIENT, index + 1);
}

/* ------------------------------------------------------------------ items */

static BOOL grow(LISTBOX *lb, int need)
{
    if (need <= lb->capacity)
        return TRUE;
    int capacity = need + 32;
    if (lb->has_strings) {
        LPWSTR *text = lb->text ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lb->text, capacity * sizeof(LPWSTR))
                                : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(LPWSTR));
        if (!text)
            return FALSE;
        lb->text = text;
    }
    if (lb->has_data) {
        ULONG_PTR *data = lb->data ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lb->data, capacity * sizeof(ULONG_PTR))
                                   : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(ULONG_PTR));
        if (!data)
            return FALSE;
        lb->data = data;
    }
    if (lb->mode != LB_SINGLE) {
        BYTE *state = lb->state ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lb->state, capacity)
                                : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
        if (!state)
            return FALSE;
        lb->state = state;
    }
    if (lb->owner_draw == 2) {
        BYTE *heights = lb->heights ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lb->heights, capacity)
                                    : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
        if (!heights)
            return FALSE;
        lb->heights = heights;
    }
    lb->capacity = capacity;
    return TRUE;
}

static void error_space(LISTBOX *lb)
{
    notify(lb, LBN_ERRSPACE);
}

/* bracketed directory and drive lines sort after names starting with a letter or digit. */
static int compare_strings(LISTBOX *lb, LPCWSTR item, LPCWSTR text)
{
    if (item[0] == L'[' && text[0] != L'[' && IsCharAlphaNumericW(text[0]))
        return 1;
    if (text[0] == L'[' && item[0] != L'[' && IsCharAlphaNumericW(item[0]))
        return -1;
    return CompareStringW(lb->locale, NORM_IGNORECASE, item, -1, text, -1) - CSTR_EQUAL;
}

/* the item compared with a new one through WM_COMPAREITEM: positive when the item goes after. */
static int compare_data(LISTBOX *lb, int index, ULONG_PTR data, BOOL search)
{
    COMPAREITEMSTRUCT cis = {ODT_LISTBOX, (UINT)id_of(lb), lb->hwnd};
    if (search) {
        cis.itemID1 = (UINT)-1;
        cis.itemData1 = data;
        cis.itemID2 = (UINT)index;
        cis.itemData2 = lb->has_data ? lb->data[index] : 0;
    } else {
        cis.itemID1 = (UINT)index;
        cis.itemData1 = lb->has_data ? lb->data[index] : 0;
        cis.itemID2 = (UINT)-1;
        cis.itemData2 = data;
    }
    cis.dwLocaleId = lb->locale;
    return (int)SendMessageW(lb->parent, WM_COMPAREITEM, (WPARAM)cis.CtlID, (LPARAM)&cis);
}

static int sorted_position(LISTBOX *lb, LPCWSTR text, ULONG_PTR data)
{
    int lo = 0, hi = lb->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = lb->has_strings ? compare_strings(lb, lb->text[mid], text) : compare_data(lb, mid, data, FALSE);
        if (!c)
            return mid;
        if (c > 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return lo;
}

static LRESULT insert_item(LISTBOX *lb, int index, LPARAM value, int mode, BOOL from_dir)
{
    LPCWSTR text = lb->has_strings ? (LPCWSTR)value : nullptr;
    if (lb->has_strings && !text)
        text = L"";
    if (mode == INSERT_ADD)
        index = lb->sort ? sorted_position(lb, text, (ULONG_PTR)value) : -1;
    if (!lb->capacity && index != -1 && index != 0) {
        SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    }
    if (index == -1)
        index = lb->count;
    if (index < 0 || index > lb->count || lb->count == INT_MAX) {
        SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    }
    LPWSTR copy = nullptr;
    if (lb->has_strings) {
        SIZE_T n = wcslen(text) + 1;
        copy = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
        if (copy)
            memcpy(copy, text, n * sizeof(WCHAR));
    }
    if ((lb->has_strings && !copy) || !grow(lb, lb->count + 1)) {
        HeapFree(GetProcessHeap(), 0, copy);
        error_space(lb);
        return LB_ERRSPACE;
    }
    int tail = lb->count - index;
    if (lb->text)
        memmove(lb->text + index + 1, lb->text + index, tail * sizeof(LPWSTR));
    if (lb->data)
        memmove(lb->data + index + 1, lb->data + index, tail * sizeof(ULONG_PTR));
    if (lb->state)
        memmove(lb->state + index + 1, lb->state + index, tail);
    if (lb->heights)
        memmove(lb->heights + index + 1, lb->heights + index, tail);
    if (lb->text)
        lb->text[index] = copy;
    if (lb->data)
        lb->data[index] = lb->has_strings ? 0 : (ULONG_PTR)value;
    if (lb->state)
        lb->state[index] = 0;
    lb->count++;
    if (lb->owner_draw == 2) {
        MEASUREITEMSTRUCT mis = {ODT_LISTBOX, (UINT)id_of(lb), (UINT)index};
        mis.itemHeight = (UINT)system_font_height();
        mis.itemData = (ULONG_PTR)value;
        mis.itemWidth = lb->has_strings || from_dir ? 0x464C4154 : 0;
        SendMessageW(lb->parent, WM_MEASUREITEM, (WPARAM)mis.CtlID, (LPARAM)&mis);
        lb->heights[index] = (BYTE)mis.itemHeight;
        layout(lb);
    }
    if (lb->mode == LB_SINGLE && lb->sel >= index)
        lb->sel++;
    lb->in_insert = TRUE;
    update_bars(lb);
    lb->in_insert = FALSE;
    if (lb->count == 1 || index <= lb->top + visible_count(lb, TRUE))
        invalidate_all(lb);
    NotifyWinEvent(EVENT_OBJECT_CREATE, lb->hwnd, OBJID_CLIENT, index + 1);
    return index;
}

static void free_items(LISTBOX *lb)
{
    if (lb->text)
        for (int i = 0; i < lb->count; i++)
            HeapFree(GetProcessHeap(), 0, lb->text[i]);
    HeapFree(GetProcessHeap(), 0, lb->text);
    HeapFree(GetProcessHeap(), 0, lb->data);
    HeapFree(GetProcessHeap(), 0, lb->state);
    HeapFree(GetProcessHeap(), 0, lb->heights);
    lb->text = nullptr;
    lb->data = nullptr;
    lb->state = nullptr;
    lb->heights = nullptr;
    lb->count = lb->capacity = 0;
}

static void delete_item_message(LISTBOX *lb, int index)
{
    DELETEITEMSTRUCT dis = {ODT_LISTBOX, (UINT)id_of(lb), (UINT)index, lb->hwnd, lb->data ? lb->data[index] : 0};
    SendMessageW(lb->parent, WM_DELETEITEM, (WPARAM)dis.CtlID, (LPARAM)&dis);
}

/* owner drawn items with data hear of their end, last first. */
static void delete_all_messages(LISTBOX *lb)
{
    if (lb->owner_draw && lb->has_data)
        for (int i = lb->count - 1; i >= 0; i--)
            delete_item_message(lb, i);
}

static LRESULT reset_content(LISTBOX *lb)
{
    if (!lb->count)
        return 0;
    delete_all_messages(lb);
    free_items(lb);
    invalidate_all(lb);
    lb->caret = lb->top = lb->x_origin = lb->last_selection = 0;
    lb->sel = -1;
    update_bars(lb);
    return 0;
}

static LRESULT delete_item(LISTBOX *lb, int index)
{
    if (index < 0 || index >= lb->count) {
        SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    }
    NotifyWinEvent(EVENT_OBJECT_DESTROY, lb->hwnd, OBJID_CLIENT, index + 1);
    if (lb->count == 1) {
        SendMessageW(lb->hwnd, LB_RESETCONTENT, 0, 0);
    } else {
        invalidate_item(lb, lb->count - 1);
        if (lb->has_data)
            delete_item_message(lb, index);
        int tail = lb->count - index - 1;
        if (lb->text) {
            HeapFree(GetProcessHeap(), 0, lb->text[index]);
            memmove(lb->text + index, lb->text + index + 1, tail * sizeof(LPWSTR));
        }
        if (lb->data)
            memmove(lb->data + index, lb->data + index + 1, tail * sizeof(ULONG_PTR));
        if (lb->state)
            memmove(lb->state + index, lb->state + index + 1, tail);
        if (lb->heights)
            memmove(lb->heights + index, lb->heights + index + 1, tail);
        lb->count--;
        if (lb->mode == LB_SINGLE) {
            if (lb->sel == index) {
                lb->sel = -1;
                if (lb->combo)
                    ComboRefreshClosed(lb->combo);
            } else if (lb->sel > index) {
                lb->sel--;
            }
        }
        if (index <= lb->anchor)
            lb->anchor = -1;
        if (lb->caret && index == lb->caret)
            lb->caret--;
        if (lb->caret > lb->count - 1)
            lb->caret = lb->count - 1;
        if (lb->caret < 0)
            lb->caret = 0;
        if (lb->mode == LB_EXTENDED && lb->sel == -1)
            lb->sel = lb->caret;
        if (lb->owner_draw == 2)
            layout(lb);
        lb->top = clamp_top(lb, lb->top);
    }
    update_bars(lb);
    if (index <= lb->top + visible_count(lb, TRUE))
        invalidate_all(lb);
    make_visible(lb, lb->caret, FALSE);
    return lb->count;
}

/* ------------------------------------------------------------------ searching */

/* a prefix match skips the bracket of directory lines and the dash of drive lines. */
static BOOL prefix_match(LISTBOX *lb, LPCWSTR item, LPCWSTR text)
{
    if (text[0] != L'[' && item[0] == L'[') {
        item++;
        if (item[0] == L'-')
            item++;
    }
    int n = (int)wcslen(text), m = (int)wcslen(item);
    if (n > m)
        return FALSE;
    return CompareStringW(lb->locale, NORM_IGNORECASE, item, n, text, n) == CSTR_EQUAL;
}

static int search(LISTBOX *lb, int start, LPARAM value, BOOL exact)
{
    if (!lb->has_data && !lb->has_strings) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return LB_ERR;
    }
    if (lb->has_strings && (!value || !*(LPCWSTR)value))
        return LB_ERR;
    if (!lb->count)
        return LB_ERR;
    int first = start < 0 || start + 1 >= lb->count ? 0 : start + 1;
    for (int n = 0, i = first; n < lb->count; n++, i = (i + 1) % lb->count) {
        if (lb->has_strings) {
            LPCWSTR item = lb->text[i], text = (LPCWSTR)value;
            if (exact ? CompareStringW(lb->locale, NORM_IGNORECASE, item, -1, text, -1) == CSTR_EQUAL
                      : prefix_match(lb, item, text))
                return i;
        } else if (lb->sort) {
            int c = compare_data(lb, i, (ULONG_PTR)value, TRUE);
            if (c != -1 && c != 1)
                return i;
        } else if (lb->data[i] == (ULONG_PTR)value) {
            return i;
        }
    }
    return LB_ERR;
}

int ListBoxPrefixSearch(LISTBOX *lb, int start, LPCWSTR text)
{
    return search(lb, start, (LPARAM)text, FALSE);
}

int ListBoxExactSearch(LISTBOX *lb, int start, LPCWSTR text)
{
    return search(lb, start, (LPARAM)text, TRUE);
}

/* ------------------------------------------------------------------ LB_ messages */

static LRESULT set_cur_sel(LISTBOX *lb, int index)
{
    if (lb->mode != LB_SINGLE || index < -1 || index >= lb->count)
        return LB_ERR;
    draw_focus(lb, FALSE);
    if (index == -1) {
        select_single(lb, -1);
        if (lb->caret > lb->count - 1)
            lb->caret = lb->count ? lb->count - 1 : 0;
    } else {
        make_visible(lb, index, FALSE);
        lb->caret = index;
        select_single(lb, index);
    }
    if (GetFocus() == lb->hwnd || lb->caret_enabled)
        draw_focus(lb, TRUE);
    return lb->sel;
}

static LRESULT set_sel(LISTBOX *lb, BOOL value, int index)
{
    if (index == 0xFFFF && lb->count <= 0xFFFF)
        index = -1;
    if (lb->mode == LB_SINGLE || index >= lb->count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return LB_ERR;
    }
    if (index == -1) {
        for (int i = 0; i < lb->count; i++)
            set_one(lb, i, value);
        return 0;
    }
    if (index < 0)
        return 0;
    if (value) {
        make_visible(lb, index, TRUE);
        draw_focus(lb, FALSE);
        lb->sel = lb->caret = lb->anchor = lb->last_move = index;
        draw_focus(lb, TRUE);
    }
    set_one(lb, index, value);
    return 0;
}

static LRESULT sel_range(LISTBOX *lb, BOOL value, int first, int last)
{
    if (lb->mode == LB_SINGLE) {
        SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    }
    if (first > last) {
        int t = first;
        first = last;
        last = t;
    }
    if (first < 0)
        first = 0;
    if (last >= lb->count)
        last = lb->count - 1;
    for (int i = first; i <= last; i++)
        set_one(lb, i, value);
    return 0;
}

static LRESULT get_text(LISTBOX *lb, int index, LPWSTR buffer)
{
    if (index < 0 || index >= lb->count) {
        SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    }
    if (!lb->has_strings) {
        ULONG_PTR data = lb->has_data ? lb->data[index] : 0;
        if (buffer)
            memcpy(buffer, &data, sizeof(data));
        return sizeof(data);
    }
    int n = (int)wcslen(lb->text[index]);
    if (buffer)
        memcpy(buffer, lb->text[index], ((SIZE_T)n + 1) * sizeof(WCHAR));
    return n;
}

static LRESULT set_tabs(LISTBOX *lb, int count, const int *stops)
{
    if (!lb->use_tabs) {
        SetLastError(ERROR_LB_WITHOUT_TABSTOPS);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, lb->tabs);
    lb->tabs = nullptr;
    lb->tab_count = 0;
    if (!count)
        return TRUE;
    lb->tabs = HeapAlloc(GetProcessHeap(), 0, count * sizeof(int));
    if (!lb->tabs)
        return FALSE;
    for (int i = 0; i < count; i++)
        lb->tabs[i] = (lb->cx_char * stops[i] + 2) / 4;
    lb->tab_count = count;
    return TRUE;
}

static LRESULT set_count(LISTBOX *lb, int count)
{
    if (lb->has_strings || lb->has_data) {
        SetLastError(ERROR_SETCOUNT_ON_BAD_LB);
        return LB_ERR;
    }
    if (!count)
        return reset_content(lb);
    BOOL redraw = lb->redraw;
    lb->redraw = FALSE;
    lb->caret = lb->top = lb->x_origin = 0;
    lb->sel = -1;
    if (lb->mode != LB_SINGLE) {
        BYTE *state = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lb->state, (SIZE_T)count + 32);
        if (!state && !lb->state)
            state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)count + 32);
        if (!state) {
            lb->redraw = redraw;
            error_space(lb);
            return LB_ERRSPACE;
        }
        memset(state, 0, (SIZE_T)count + 32);
        lb->state = state;
    }
    lb->capacity = count + 32;
    lb->count = count;
    lb->redraw = redraw;
    invalidate_all(lb);
    update_bars(lb);
    return 0;
}

static LRESULT init_storage(LISTBOX *lb, int items, int bytes)
{
    if (items < 0 || bytes < 0) {
        error_space(lb);
        return LB_ERRSPACE;
    }
    if (!grow(lb, lb->count + items)) {
        error_space(lb);
        return LB_ERRSPACE;
    }
    return lb->capacity;
}

/* an item's line of a directory listing added: "[dir]" for directories. */
static LRESULT add_file_line(LISTBOX *lb, const WIN32_FIND_DATAW *fd)
{
    WCHAR line[MAX_PATH + 3];
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        wsprintfW(line, L"[%s]", fd->cFileName);
    else
        lstrcpynW(line, fd->cFileName, MAX_PATH);
    return insert_item(lb, -1, (LPARAM)line, INSERT_ADD, TRUE);
}

LRESULT ListBoxDir(LISTBOX *lb, UINT attributes, LPCWSTR spec)
{
    WCHAR path[MAX_PATH];
    lstrcpynW(path, spec ? spec : L"*.*", MAX_PATH);
    BOOL redraw = lb->redraw && IsWindowVisible(lb->hwnd);
    if (redraw)
        SendMessageW(lb->hwnd, WM_SETREDRAW, FALSE, 0);
    LRESULT r = 0;
    if (attributes != (DDL_EXCLUSIVE | DDL_DRIVES | 0x1000)) {
        DWORD mask = attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                                   FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL);
        if (!(attributes & DDL_EXCLUSIVE))
            mask |= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL;
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW(path, &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                DWORD a = fd.dwFileAttributes == FILE_ATTRIBUTE_COMPRESSED ? FILE_ATTRIBUTE_NORMAL : fd.dwFileAttributes;
                if (!a)
                    a = FILE_ATTRIBUTE_NORMAL;
                if (!(a & mask))
                    continue;
                if ((a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)) & ~attributes)
                    continue;
                if (a & FILE_ATTRIBUTE_DIRECTORY) {
                    if (!lstrcmpW(fd.cFileName, L"."))
                        continue;
                } else if (attributes & 0x1000) {
                    continue;
                }
                r = add_file_line(lb, &fd);
            } while (r != LB_ERRSPACE && FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    if (r != LB_ERRSPACE && (attributes & DDL_DRIVES)) {
        DWORD drives = GetLogicalDrives();
        for (int d = 0; d < 26 && r != LB_ERRSPACE; d++)
            if (drives >> d & 1) {
                WCHAR line[] = L"[-a-]";
                line[2] = (WCHAR)(L'a' + d);
                r = insert_item(lb, -1, (LPARAM)line, INSERT_AT, TRUE);
            }
    }
    update_bars(lb);
    if (redraw)
        SendMessageW(lb->hwnd, WM_SETREDRAW, TRUE, 0);
    invalidate_all(lb);
    return r == LB_ERRSPACE ? LB_ERRSPACE : lb->count - 1;
}

/* ------------------------------------------------------------------ mouse */

enum { UP_RELEASE = 1, UP_NOTIFY = 2, UP_SELECTABLE = 4, UP_RESTORE = 8, UP_ACCEPT = 16 };

static void button_up(LISTBOX *lb, int flags)
{
    if (lb->mode == LB_EXTENDED && lb->mouse_down && lb->anchor >= 0) {
        int lo = lb->anchor < lb->last_move ? lb->anchor : lb->last_move;
        int hi = lb->anchor < lb->last_move ? lb->last_move : lb->anchor;
        for (int i = lo; i <= hi && i < lb->count; i++)
            if (i >= 0)
                lb->state[i] = (BYTE)((lb->state[i] & 0xF0) | (lb->new_state ? 1 : 0));
    }
    if (lb->combo && (flags & UP_RESTORE) && lb->mode == LB_SINGLE) {
        int old = lb->sel;
        select_single(lb, lb->last_selection);
        if (lb->notify && (flags & UP_NOTIFY) && old != lb->sel)
            notify(lb, LBN_SELCHANGE);
    }
    KillTimer(lb->hwnd, TIMER_SCROLL);
    lb->mouse_down = FALSE;
    if (lb->captured) {
        lb->captured = FALSE;
        if ((flags & UP_RELEASE) && GetCapture() == lb->hwnd)
            ReleaseCapture();
    }
    if (lb->caret < lb->top || lb->caret > last_full_visible(lb))
        make_visible(lb, lb->caret, FALSE);
    if (!lb->notify)
        return;
    if (flags & UP_NOTIFY) {
        if (flags & UP_SELECTABLE)
            notify(lb, lb->double_click ? LBN_DBLCLK : LBN_SELCHANGE);
        else
            notify(lb, LBN_SELCANCEL);
    } else if ((flags & UP_ACCEPT) && lb->sel != lb->last_selection) {
        notify(lb, LBN_SELCHANGE);
    }
}

/* the item under the pointer selected as a plain click would, without a notification. */
static void plain_select(LISTBOX *lb, int index, BOOL selectable)
{
    if (index != lb->caret)
        draw_focus(lb, FALSE);
    if (lb->mode == LB_SINGLE) {
        select_single(lb, selectable ? index : -1);
    } else {
        if (lb->mode == LB_EXTENDED)
            reset_world(lb, index, index, FALSE);
        if (selectable) {
            if (lb->mode == LB_MULTIPLE)
                set_one(lb, index, !(lb->state[index] & 0x0F));
            else if (!(lb->state[index] & 0x0F))
                set_one(lb, index, TRUE);
        }
        lb->sel = selectable ? index : -1;
        lb->anchor = lb->last_move = index;
        lb->new_state = (lb->state[index] & 0x0F) != 0;
    }
    move_caret(lb, index);
    draw_focus(lb, TRUE);
}

static void button_down(LISTBOX *lb, UINT msg, int x, int y, WPARAM keys)
{
    BOOL outside;
    if (lb->captured) {
        if (lb->combo) {
            int index = hit_test(lb, x, y, &outside);
            (void)index;
            if (outside) {
                POINT pt = {x, y};
                ClientToScreen(lb->hwnd, &pt);
                RECT window;
                GetWindowRect(lb->hwnd, &window);
                if (PtInRect(&window, pt)) {
                    LRESULT hit = SendMessageW(lb->hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                    if (hit != HTCLIENT) {
                        ReleaseCapture();
                        SendMessageW(lb->hwnd, WM_NCLBUTTONDOWN, (WPARAM)hit, MAKELPARAM(pt.x, pt.y));
                        SetCapture(lb->hwnd);
                        return;
                    }
                } else {
                    lb->captured = FALSE;
                    ReleaseCapture();
                    ComboHideList(lb->combo, TRUE, FALSE);
                    return;
                }
            }
        }
        lb->captured = FALSE;
        ReleaseCapture();
    }
    if (lb->combo) {
        SetFocus(lb->combo->has_edit ? lb->combo->edit : lb->combo->hwnd);
    } else {
        SetFocus(lb->hwnd);
        if (GetFocus() != lb->hwnd)
            return;
    }
    if (lb->add_mode)
        lb->add_mode = FALSE;
    int index = hit_test(lb, x, y, &outside);
    if (!lb->count || outside)
        return;
    lb->double_click = msg == WM_LBUTTONDBLCLK;
    if (!lb->double_click) {
        LRESULT r = SendMessageW(lb->parent, WM_LBTRACKPOINT, (WPARAM)index, MAKELPARAM(x + lb->x_origin, y));
        if (r)
            return;
    }
    if (lb->combo)
        lb->last_selection = lb->sel;
    lb->mouse_down = lb->captured = TRUE;
    lb->prev = (POINT){x, y};
    SetCapture(lb->hwnd);
    if (lb->double_click) {
        button_up(lb, UP_RELEASE | UP_NOTIFY | UP_SELECTABLE);
        return;
    }
    SetTimer(lb->hwnd, TIMER_SCROLL, GetDoubleClickTime() * 4 / 5, nullptr);
    BOOL shift = (keys & MK_SHIFT) != 0, ctrl = (keys & MK_CONTROL) != 0;
    if (lb->mode == LB_EXTENDED && shift && !ctrl) {
        lb->sel = lb->last_move = index;
        if (lb->anchor == -1) {
            lb->anchor = index;
            reset_world(lb, index, index, FALSE);
            set_one(lb, index, TRUE);
        } else {
            reset_world(lb, lb->anchor, lb->anchor, FALSE);
            lb->new_state = TRUE;
            lb->last_move = lb->anchor;
            block_hilite(lb, index, FALSE);
        }
        lb->new_state = TRUE;
        move_caret(lb, index);
    } else if (lb->mode == LB_EXTENDED && ctrl && !shift) {
        lb->sel = lb->last_move = lb->anchor = index;
        BOOL value = !(lb->state[index] & 0x0F);
        set_one(lb, index, value);
        lb->new_state = value;
        move_caret(lb, index);
    } else if (lb->mode == LB_EXTENDED && ctrl && shift) {
        if (lb->new_state && lb->anchor >= 0 && lb->last_move >= 0)
            alter_hilite(lb, lb->anchor, lb->last_move, FALSE, HILITE_SHOWN | HILITE_SELECTED, FALSE);
        lb->sel = index;
        if (lb->anchor == -1)
            lb->anchor = index;
        lb->new_state = (lb->state[lb->anchor] & 0xF0) != 0;
        lb->last_move = lb->anchor;
        block_hilite(lb, index, FALSE);
        move_caret(lb, index);
    } else {
        plain_select(lb, index, TRUE);
    }
}

static void mouse_move(LISTBOX *lb, int x, int y)
{
    lb->prev = (POINT){x, y};
    RECT r;
    client_rect(lb, &r);
    POINT pt = {x, y};
    if (!lb->mouse_down) {
        BOOL outside;
        if (PtInRect(&r, pt)) {
            int index = hit_test(lb, x, y, &outside);
            if (index >= 0 && index != lb->sel)
                plain_select(lb, index, !outside);
        }
        return;
    }
    if (!lb->multicolumn && (y < 0 || y >= r.bottom)) {
        set_top(lb, lb->top + (y < 0 ? -1 : 1));
        int distance = y < 0 ? -y : y - r.bottom;
        int interval = (int)(GetDoubleClickTime() * 4 / 5) - 16 * distance;
        SetTimer(lb->hwnd, TIMER_SCROLL, interval > 1 ? (UINT)interval : 1, nullptr);
    } else if (lb->multicolumn && (x < 0 || x >= r.right)) {
        int dir = x < 0 ? -1 : 1;
        if (lb->right)
            dir = -dir;
        set_top(lb, lb->top + dir * lb->per_column);
        int distance = x < 0 ? -x : x - r.right;
        int interval = (int)(GetDoubleClickTime() * 6 / 5) - 16 * distance;
        SetTimer(lb->hwnd, TIMER_SCROLL, interval > 1 ? (UINT)interval : 1, nullptr);
    }
    BOOL outside;
    int index = hit_test(lb, x, y, &outside);
    if (index < 0)
        return;
    if (lb->mode == LB_SINGLE) {
        if (index != lb->sel)
            plain_select(lb, index, TRUE);
        return;
    }
    if (index != lb->caret) {
        move_caret(lb, index);
        if (lb->mode == LB_EXTENDED) {
            block_hilite(lb, index, FALSE);
            lb->last_move = index;
        }
    }
}

/* ------------------------------------------------------------------ keyboard */

static int page_size(LISTBOX *lb)
{
    return lb->full_max <= 1 ? lb->full_max : lb->full_max - 1;
}

/* where a key moves the caret: the selection follows unless multiple selection or add mode keep it. */
static void navigate(LISTBOX *lb, int target, BOOL toggle, BOOL shift)
{
    if (target < 0)
        target = 0;
    if (target > lb->count - 1)
        target = lb->count - 1;
    if (lb->mode == LB_SINGLE && target == lb->sel)
        return;
    lb->typed_count = 0;
    draw_focus(lb, FALSE);
    move_caret(lb, target);
    if (lb->mode == LB_EXTENDED && shift) {
        if (lb->anchor == -1)
            lb->anchor = target;
        if (lb->last_move == -1)
            lb->last_move = target;
        if (!lb->add_mode)
            reset_world(lb, lb->anchor, lb->last_move, FALSE);
        else if (!lb->new_state)
            lb->last_move = lb->anchor;
        lb->new_state = (lb->state[lb->anchor] & 0x0F) != 0;
        block_hilite(lb, target, TRUE);
        lb->sel = target;
    } else if (lb->add_mode || lb->mode == LB_MULTIPLE) {
        if (toggle) {
            set_one(lb, target, !(lb->state[target] & 0x0F));
            lb->anchor = lb->last_move = target;
        }
    } else if (lb->mode == LB_SINGLE) {
        select_single(lb, target);
    } else {
        reset_world(lb, target, target, FALSE);
        set_one(lb, target, TRUE);
        lb->sel = lb->anchor = lb->last_move = target;
    }
    make_visible(lb, target, FALSE);
    update_bars(lb);
    draw_focus(lb, TRUE);
    if (lb->notify) {
        if (lb->combo && lb->combo->list_visible) {
            lb->combo->keyboard_sel = TRUE;
            lb->last_selection = target;
        }
        notify(lb, LBN_SELCHANGE);
    }
}

static void key_down(LISTBOX *lb, UINT vk)
{
    if (lb->mouse_down || (!lb->count && vk != VK_F4))
        return;
    BOOL shift = lb->mode == LB_EXTENDED && GetKeyState(VK_SHIFT) < 0;
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0;
    if (lb->want_keys) {
        LRESULT r = SendMessageW(lb->parent, WM_VKEYTOITEM, MAKEWPARAM(vk, lb->caret), (LPARAM)lb->hwnd);
        if (r == -2)
            return;
        if (r != -1) {
            navigate(lb, (int)r, FALSE, shift);
            return;
        }
    }
    COMBO *cb = lb->combo;
    int target, caret = lb->caret;
    BOOL toggle = FALSE;
    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT:
        if (lb->multicolumn) {
            BOOL mirror = lb->right != ((GetWindowLongW(lb->hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0);
            int dir = (vk == VK_RIGHT) != mirror ? 1 : -1;
            target = caret + dir * lb->per_column;
            if (target < 0 || target >= lb->count + lb->per_column - 1 - (lb->count - 1) % lb->per_column)
                return;
            break;
        }
        if (GetWindowLongW(lb->hwnd, GWL_STYLE) & WS_HSCROLL) {
            PostMessageW(lb->hwnd, WM_HSCROLL, vk == VK_LEFT ? SB_LINELEFT : SB_LINERIGHT, 0);
            return;
        }
        vk = vk == VK_LEFT ? VK_UP : VK_DOWN;
        [[fallthrough]];
    case VK_UP:
    case VK_DOWN:
        if (closed_ext_ui(lb)) {
            if (vk == VK_DOWN && !cb->list_visible)
                ComboShowList(cb, TRUE);
            return;
        }
        target = caret + (vk == VK_UP ? -1 : 1);
        if (lb->mode == LB_SINGLE && lb->sel == -1)
            target = caret;
        break;
    case VK_PRIOR:
    case VK_NEXT:
        if (closed_ext_ui(lb))
            return;
        if (lb->owner_draw == 2)
            target = vk == VK_PRIOR ? page_up_top(lb, caret) : page_down_top(lb, caret);
        else
            target = caret + (vk == VK_PRIOR ? -page_size(lb) : page_size(lb));
        break;
    case VK_HOME:
    case VK_END:
        if (closed_ext_ui(lb))
            return;
        target = vk == VK_HOME ? 0 : lb->count - 1;
        break;
    case VK_SPACE:
        target = caret;
        toggle = TRUE;
        break;
    case VK_F4:
        if (cb && cb->droppable && !cb->extended_ui) {
            if (cb->list_visible)
                ComboHideList(cb, TRUE, TRUE);
            else
                ComboShowList(cb, TRUE);
        }
        return;
    case VK_RETURN:
    case VK_ESCAPE:
        if (cb && cb->droppable && cb->list_visible && !cb->extended_ui)
            ComboHideList(cb, TRUE, vk == VK_RETURN);
        return;
    case VK_OEM_2:
    case VK_DIVIDE:
        if (lb->mode == LB_EXTENDED && ctrl) {
            for (int i = 0; i < lb->count; i++)
                set_one(lb, i, TRUE);
            NotifyWinEvent(EVENT_OBJECT_SELECTIONWITHIN, lb->hwnd, OBJID_CLIENT, 0);
            notify(lb, LBN_SELCHANGE);
        }
        return;
    case VK_OEM_5:
        if (lb->mode == LB_EXTENDED && ctrl) {
            reset_world(lb, caret, caret, FALSE);
            set_one(lb, caret, TRUE);
            notify(lb, LBN_SELCHANGE);
        }
        return;
    case VK_F8:
        if (lb->mode == LB_EXTENDED && GetKeyState(VK_SHIFT) < 0)
            lb->add_mode = !lb->add_mode;
        return;
    default:
        return;
    }
    navigate(lb, target, toggle, shift);
}

static void type_ahead(LISTBOX *lb, WCHAR c)
{
    if (!lb->count || lb->mouse_down)
        return;
    if (c == L'\b') {
        if (!lb->typed_count)
            return;
        lb->typed_count--;
        if (lb->sort && lb->typed_count) {
            SetTimer(lb->hwnd, TIMER_TYPEAHEAD, GetDoubleClickTime() * 4, nullptr);
            WCHAR text[257];
            memcpy(text, lb->typed, lb->typed_count * sizeof(WCHAR));
            text[lb->typed_count] = 0;
            int found = search(lb, -1, (LPARAM)text, FALSE);
            if (found >= 0)
                navigate(lb, found, FALSE, FALSE);
        }
        return;
    }
    if (c == 0x1B) {
        lb->typed_count = 0;
        return;
    }
    if (c == L' ' && (lb->add_mode || lb->mode == LB_MULTIPLE))
        return;
    if (lb->owner_draw && !lb->has_strings) {
        LRESULT r = SendMessageW(lb->parent, WM_CHARTOITEM, MAKEWPARAM(c, lb->caret), (LPARAM)lb->hwnd);
        if (r >= 0)
            navigate(lb, (int)r, FALSE, FALSE);
        return;
    }
    if (GetKeyState(VK_CONTROL) < 0 && c < 0x20)
        c += 0x40;
    if (lb->typed_count >= 256) {
        MessageBeep(0);
        return;
    }
    lb->typed[lb->typed_count++] = c;
    WCHAR text[257];
    int start;
    if (lb->sort) {
        SetTimer(lb->hwnd, TIMER_TYPEAHEAD, GetDoubleClickTime() * 4, nullptr);
        start = -1;
    } else {
        start = lb->caret;
    }
    memcpy(text, lb->typed, lb->typed_count * sizeof(WCHAR));
    text[lb->typed_count] = 0;
    if (!lb->sort)
        lb->typed_count = 0;
    int found = search(lb, start, (LPARAM)text, FALSE);
    if (found < 0 && lb->typed_count) {
        lb->typed_count--;
        if (lb->typed_count == 1 && lb->typed[0] == lb->typed[1]) {
            text[1] = 0;
            found = search(lb, lb->caret, (LPARAM)text, FALSE);
        }
    }
    if (found >= 0)
        navigate(lb, found, FALSE, FALSE);
}

/* ------------------------------------------------------------------ the scroll bars */

static void vscroll(LISTBOX *lb, int code)
{
    switch (code) {
    case SB_LINEUP:
        set_top(lb, lb->top - 1);
        break;
    case SB_LINEDOWN:
        set_top(lb, lb->top + 1);
        break;
    case SB_PAGEUP:
        set_top(lb, lb->owner_draw == 2 ? page_up_top(lb, lb->top) : lb->top - page_size(lb));
        break;
    case SB_PAGEDOWN:
        set_top(lb, lb->owner_draw == 2 ? page_down_top(lb, lb->top) : lb->top + page_size(lb));
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO si = {sizeof(si), SIF_TRACKPOS};
        GetScrollInfo(lb->hwnd, SB_VERT, &si);
        set_top(lb, si.nTrackPos);
        break;
    }
    case SB_TOP:
        set_top(lb, 0);
        break;
    case SB_BOTTOM:
        set_top(lb, lb->count - 1);
        break;
    case SB_ENDSCROLL:
        lb->last_vbar[0] = -2;
        update_bars(lb);
        break;
    }
}

static void hscroll(LISTBOX *lb, int code)
{
    RECT r;
    client_rect(lb, &r);
    if (lb->multicolumn) {
        int column = lb->top / lb->per_column;
        switch (code) {
        case SB_LINELEFT:
            column--;
            break;
        case SB_LINERIGHT:
            column++;
            break;
        case SB_PAGELEFT:
            column -= lb->columns;
            break;
        case SB_PAGERIGHT:
            column += lb->columns;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {sizeof(si), SIF_TRACKPOS};
            GetScrollInfo(lb->hwnd, SB_HORZ, &si);
            column = si.nTrackPos;
            break;
        }
        case SB_LEFT:
            column = 0;
            break;
        case SB_RIGHT:
            column = lb->count / lb->per_column;
            break;
        default:
            return;
        }
        set_top(lb, column * lb->per_column);
        return;
    }
    switch (code) {
    case SB_LINELEFT:
        set_x_origin(lb, lb->x_origin - lb->cx_char);
        break;
    case SB_LINERIGHT:
        set_x_origin(lb, lb->x_origin + lb->cx_char);
        break;
    case SB_PAGELEFT:
        set_x_origin(lb, lb->x_origin - r.right);
        break;
    case SB_PAGERIGHT:
        set_x_origin(lb, lb->x_origin + r.right);
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO si = {sizeof(si), SIF_TRACKPOS};
        GetScrollInfo(lb->hwnd, SB_HORZ, &si);
        set_x_origin(lb, si.nTrackPos);
        break;
    }
    case SB_LEFT:
        set_x_origin(lb, 0);
        break;
    case SB_RIGHT:
        set_x_origin(lb, lb->extent);
        break;
    }
}

static int g_wheel;

static LRESULT wheel(LISTBOX *lb, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (wparam & (MK_SHIFT | MK_CONTROL))
        return DefWindowProcW(lb->hwnd, msg, wparam, lparam);
    UINT lines = 3, chars = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &chars, 0);
    int delta = (short)HIWORD(wparam);
    BOOL vertical = msg == WM_MOUSEWHEEL && (GetWindowLongW(lb->hwnd, GWL_STYLE) & WS_VSCROLL);
    if (msg == WM_MOUSEWHEEL && !lines)
        return 1;
    if (msg == WM_MOUSEWHEEL && !vertical) {
        if (!chars || !(GetWindowLongW(lb->hwnd, GWL_STYLE) & WS_HSCROLL))
            return 1;
        delta = -delta;
    }
    if ((g_wheel > 0) != (delta > 0))
        g_wheel = 0;
    g_wheel += delta;
    int threshold = 120 / (int)(vertical ? (lines == WHEEL_PAGESCROLL ? 120 : lines) : chars);
    if (threshold < 1)
        threshold = 1;
    if (g_wheel / threshold == 0)
        return 1;
    int steps = g_wheel / threshold;
    g_wheel -= steps * threshold;
    if (vertical) {
        set_top(lb, lb->top - steps);
    } else if (lb->multicolumn) {
        set_top(lb, lb->top + steps * lb->per_column);
    } else {
        set_x_origin(lb, lb->x_origin + lb->cx_char * steps);
    }
    return 1;
}

/* ------------------------------------------------------------------ the window */

static void integral_height(LISTBOX *lb)
{
    if (lb->no_integral || lb->cy_char <= 0)
        return;
    RECT w;
    GetWindowRect(lb->hwnd, &w);
    DWORD style = (DWORD)GetWindowLongW(lb->hwnd, GWL_STYLE), ex = (DWORD)GetWindowLongW(lb->hwnd, GWL_EXSTYLE);
    int border = (style & WS_BORDER) ? GetSystemMetrics(SM_CYBORDER) : 0;
    if (ex & WS_EX_CLIENTEDGE)
        border += GetSystemMetrics(SM_CYEDGE);
    int height = w.bottom - w.top - 2 * border;
    int remainder = height % lb->cy_char;
    if (remainder > 0)
        SetWindowPos(lb->hwnd, nullptr, 0, 0, w.right - w.left, w.bottom - w.top - remainder,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT create(HWND hwnd, LISTBOX *lb, const CREATESTRUCTW *cs)
{
    DWORD style = (DWORD)cs->style, ex = (DWORD)cs->dwExStyle;
    lb->hwnd = hwnd;
    lb->parent = cs->hwndParent ? cs->hwndParent : GetDesktopWindow();
    lb->notify = (style & LBS_NOTIFY) != 0;
    lb->sort = (style & LBS_SORT) != 0;
    lb->redraw = !(style & LBS_NOREDRAW);
    lb->mode = (style & LBS_NOSEL) ? LB_SINGLE
               : (style & LBS_EXTENDEDSEL) ? LB_EXTENDED
               : (style & LBS_MULTIPLESEL) ? LB_MULTIPLE
                                            : LB_SINGLE;
    lb->nosel = (style & LBS_NOSEL) != 0;
    lb->multicolumn = (style & LBS_MULTICOLUMN) != 0;
    if (style & LBS_OWNERDRAWFIXED)
        lb->owner_draw = 1;
    else if ((style & LBS_OWNERDRAWVARIABLE) && !lb->multicolumn)
        lb->owner_draw = 2;
    lb->no_integral = (style & LBS_NOINTEGRALHEIGHT) || lb->owner_draw == 2;
    lb->has_strings = !lb->owner_draw || (style & LBS_HASSTRINGS);
    lb->has_data = TRUE;
    lb->use_tabs = (style & LBS_USETABSTOPS) != 0;
    lb->want_keys = (style & LBS_WANTKEYBOARDINPUT) != 0;
    lb->disable_noscroll = (style & LBS_DISABLENOSCROLL) != 0;
    if (style & LBS_NODATA) {
        if (lb->owner_draw == 1 && !lb->sort && !lb->has_strings)
            lb->has_data = FALSE;
        else
            SetLastError(ERROR_INVALID_FLAGS);
    }
    if ((style & LBS_COMBOBOX) && cs->hwndParent && GetClassLongW(cs->hwndParent, GCL_CBWNDEXTRA) >= (int)sizeof(void *))
        lb->combo = (COMBO *)GetWindowLongPtrW(cs->hwndParent, 0);
    lb->vbar = (style & WS_VSCROLL) != 0;
    lb->hbar = (style & WS_HSCROLL) != 0;
    lb->right = (ex & WS_EX_RIGHT) != 0;
    lb->rtl = (ex & WS_EX_RTLREADING) != 0;
    lb->sel = lb->anchor = lb->last_move = lb->last_selection = -1;
    lb->locale = GetThreadLocale();
    for (int i = 0; i < 3; i++)
        lb->last_vbar[i] = lb->last_hbar[i] = -2;
    char_size(hwnd, nullptr, &lb->cx_char, &lb->cy_char);
    if (lb->owner_draw == 1) {
        MEASUREITEMSTRUCT mis = {ODT_LISTBOX, (UINT)(UINT_PTR)cs->hMenu, 0, 0, (UINT)lb->cy_char, 0};
        SendMessageW(lb->parent, WM_MEASUREITEM, (WPARAM)mis.CtlID, (LPARAM)&mis);
        if (mis.itemHeight)
            lb->cy_char = (int)mis.itemHeight;
        if (lb->multicolumn)
            lb->cx_column = (int)mis.itemWidth;
    } else if (lb->owner_draw == 2) {
        lb->cy_char = 0;
    }
    if (lb->multicolumn) {
        if (lb->cx_column <= 0)
            lb->cx_column = 15 * lb->cx_char;
        lb->per_column = lb->columns = 1;
    }
    layout(lb);
    if (!lb->no_integral)
        PostMessageW(hwnd, WM_SIZE, 0, 0);
    return 0;
}

static void destroy(LISTBOX *lb)
{
    delete_all_messages(lb);
    free_items(lb);
    HeapFree(GetProcessHeap(), 0, lb->tabs);
    if (lb->combo && lb->combo->list == lb->hwnd) {
        lb->combo->list = nullptr;
        lb->combo->lb = nullptr;
        lb->combo->droppable = FALSE;
    }
}

static void size(LISTBOX *lb)
{
    if (!lb->no_integral) {
        RECT before;
        GetWindowRect(lb->hwnd, &before);
        integral_height(lb);
        RECT after;
        GetWindowRect(lb->hwnd, &after);
        if (after.bottom - after.top != before.bottom - before.top)
            return;
    }
    RECT r;
    client_rect(lb, &r);
    int old_top = lb->top;
    if (lb->multicolumn) {
        layout(lb);
    } else {
        int max = lb->extent - r.right;
        lb->x_origin = lb->x_origin > max ? (max > 0 ? max : 0) : lb->x_origin;
        layout(lb);
    }
    lb->top = clamp_top(lb, lb->top);
    if ((lb->multicolumn && r.right && r.bottom) || old_top != lb->top)
        invalidate_all(lb);
    else
        invalidate_item(lb, lb->caret);
    BOOL caret = lb->caret_on;
    if (caret)
        draw_focus(lb, FALSE);
    lb->last_vbar[0] = lb->last_hbar[0] = -2;
    update_bars(lb);
    if (caret)
        draw_focus(lb, TRUE);
}

static void set_font(LISTBOX *lb, HFONT font, BOOL redraw)
{
    lb->font = font;
    int old_cy = lb->cy_char;
    int cy;
    char_size(lb->hwnd, font, &lb->cx_char, &cy);
    if (!lb->owner_draw) {
        lb->cy_char = cy;
        if (cy != old_cy)
            integral_height(lb);
    }
    layout(lb);
    if (redraw)
        invalidate_all(lb);
}

LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LISTBOX *lb = ListBoxOf(hwnd);

    if (!lb) {
        if (msg == WM_NCCREATE) {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
            if ((cs->style & WS_VSCROLL) && (cs->style & LBS_MULTICOLUMN))
                SetWindowLongW(hwnd, GWL_STYLE, (LONG)(cs->style & ~WS_VSCROLL));
            lb = UserAlloc(sizeof(LISTBOX));
            if (!lb)
                return FALSE;
            SetWindowLongPtrW(hwnd, 0, (LONG_PTR)lb);
            lb->hwnd = hwnd;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_CREATE:
        return create(hwnd, lb, (const CREATESTRUCTW *)lparam);
    case WM_NCDESTROY:
        destroy(lb);
        SetWindowLongPtrW(hwnd, 0, 0);
        UserFree(lb);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SIZE:
        size(lb);
        return 0;
    case WM_SETFONT:
        set_font(lb, (HFONT)wparam, LOWORD(lparam) != 0);
        return 0;
    case WM_GETFONT:
        return (LRESULT)lb->font;
    case WM_SETREDRAW:
        lb->redraw = wparam != 0;
        if (lb->redraw) {
            if (lb->caret_enabled && GetFocus() == hwnd)
                draw_focus(lb, TRUE);
            update_bars(lb);
            if (lb->deferred) {
                lb->deferred = FALSE;
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            }
        }
        return 0;
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wparam;
        RECT r;
        client_rect(lb, &r);
        FillRect(dc, &r, list_brush(lb, dc));
        return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (msg == WM_PRINTCLIENT || shown(lb))
            paint(lb, dc, wparam ? nullptr : &ps.rcPaint);
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        g_wheel = 0;
        lb->caret_enabled = TRUE;
        draw_focus(lb, TRUE);
        notify(lb, LBN_SETFOCUS);
        NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, lb->caret + 1);
        return 0;
    case WM_KILLFOCUS:
        g_wheel = 0;
        draw_focus(lb, FALSE);
        if (lb->captured)
            button_up(lb, UP_RELEASE | UP_NOTIFY | (lb->mouse_down ? UP_SELECTABLE : 0));
        lb->add_mode = FALSE;
        lb->caret_enabled = FALSE;
        notify(lb, LBN_KILLFOCUS);
        KillTimer(hwnd, TIMER_TYPEAHEAD);
        lb->typed_count = 0;
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        button_down(lb, msg, (short)LOWORD(lparam), (short)HIWORD(lparam), wparam);
        return 0;
    case WM_MOUSEMOVE:
        if (lb->captured)
            mouse_move(lb, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_LBUTTONUP:
        if (lb->mouse_down) {
            BOOL outside;
            hit_test(lb, (short)LOWORD(lparam), (short)HIWORD(lparam), &outside);
            int flags = UP_RELEASE | UP_NOTIFY | (!outside || !lb->combo ? UP_SELECTABLE : 0) | (outside ? UP_RESTORE : 0);
            button_up(lb, flags);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (lb->captured && (HWND)lparam != hwnd)
            button_up(lb, UP_NOTIFY);
        return 0;
    case WM_TIMER:
        if (wparam == TIMER_TYPEAHEAD) {
            KillTimer(hwnd, TIMER_TYPEAHEAD);
            lb->typed_count = 0;
            invalidate_item(lb, lb->sel);
        } else if (lb->mouse_down) {
            mouse_move(lb, lb->prev.x, lb->prev.y);
        }
        return 0;
    case WM_KEYDOWN:
        key_down(lb, (UINT)wparam);
        return 0;
    case WM_CHAR:
        type_ahead(lb, (WCHAR)wparam);
        return 0;
    case WM_VSCROLL:
        vscroll(lb, LOWORD(wparam));
        return 0;
    case WM_HSCROLL:
        hscroll(lb, LOWORD(wparam));
        return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return wheel(lb, msg, wparam, lparam);
    case WM_STYLECHANGED: {
        DWORD ex = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);
        lb->right = (ex & WS_EX_RIGHT) != 0;
        lb->rtl = (ex & WS_EX_RTLREADING) != 0;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case LB_ADDSTRING:
    case LB_ADDSTRINGUPPER:
    case LB_ADDSTRINGLOWER:
    case LB_INSERTSTRING:
    case LB_INSERTSTRINGUPPER:
    case LB_INSERTSTRINGLOWER: {
        BOOL add = msg == LB_ADDSTRING || msg == LB_ADDSTRINGUPPER || msg == LB_ADDSTRINGLOWER;
        if (lb->has_strings && lparam) {
            /* the case variants change the caller's buffer. */
            LPWSTR s = (LPWSTR)lparam;
            if (msg == LB_ADDSTRINGUPPER || msg == LB_INSERTSTRINGUPPER)
                CharUpperBuffW(s, (DWORD)wcslen(s));
            else if (msg == LB_ADDSTRINGLOWER || msg == LB_INSERTSTRINGLOWER)
                CharLowerBuffW(s, (DWORD)wcslen(s));
        }
        return insert_item(lb, add ? -1 : (int)wparam, lparam, add ? INSERT_ADD : INSERT_AT, FALSE);
    }
    case LB_DELETESTRING:
        return delete_item(lb, (int)wparam);
    case LB_SELITEMRANGEEX:
        if (lb->mode == LB_SINGLE) {
            SetLastError(ERROR_INVALID_LB_MESSAGE);
            return LB_ERR;
        }
        if ((UINT)lparam <= (UINT)wparam)
            return sel_range(lb, FALSE, (int)lparam, (int)wparam);
        return sel_range(lb, TRUE, (int)wparam, (int)lparam);
    case LB_RESETCONTENT:
        return reset_content(lb);
    case LB_SETSEL:
        return set_sel(lb, wparam != 0, (int)lparam);
    case LB_SETCURSEL:
        return set_cur_sel(lb, (int)wparam);
    case LB_GETSEL:
        if ((UINT)wparam >= (UINT)lb->count)
            return LB_ERR;
        return item_selected(lb, (int)wparam);
    case LB_GETCURSEL:
        return lb->mode == LB_SINGLE ? lb->sel : lb->caret;
    case LB_GETTEXT:
        return get_text(lb, (int)wparam, (LPWSTR)lparam);
    case LB_GETTEXTLEN:
        if ((int)wparam < 0 || (int)wparam >= lb->count) {
            SetLastError(ERROR_INVALID_INDEX);
            return LB_ERR;
        }
        return lb->has_strings ? (LRESULT)wcslen(lb->text[wparam]) : (LRESULT)sizeof(ULONG_PTR);
    case LB_GETCOUNT:
        return lb->count;
    case LB_SELECTSTRING: {
        int found = search(lb, (int)wparam, lparam, FALSE);
        return found < 0 ? LB_ERR : set_cur_sel(lb, found);
    }
    case LB_DIR:
        return ListBoxDir(lb, (UINT)wparam, (LPCWSTR)lparam);
    case LB_GETTOPINDEX:
        return lb->top;
    case LB_FINDSTRING:
        return search(lb, (int)wparam, lparam, FALSE);
    case LB_FINDSTRINGEXACT:
        return search(lb, (int)wparam, lparam, TRUE);
    case LB_GETSELCOUNT: {
        if (lb->mode == LB_SINGLE)
            return LB_ERR;
        int n = 0;
        for (int i = 0; i < lb->count; i++)
            n += (lb->state[i] & 0x0F) != 0;
        return n;
    }
    case LB_GETSELITEMS: {
        if (lb->mode == LB_SINGLE)
            return LB_ERR;
        int n = 0, *out = (int *)lparam;
        for (int i = 0; i < lb->count && n < (int)wparam; i++)
            if (lb->state[i] & 0x0F)
                out[n++] = i;
        return n;
    }
    case LB_SETTABSTOPS:
        return set_tabs(lb, (int)wparam, (const int *)lparam);
    case LB_GETHORIZONTALEXTENT:
        return lb->extent;
    case LB_SETHORIZONTALEXTENT:
        if ((int)wparam != lb->extent) {
            lb->extent = (int)wparam;
            update_bars(lb);
        }
        return 0;
    case LB_SETCOLUMNWIDTH:
        lb->cx_column = (int)wparam;
        layout(lb);
        invalidate_all(lb);
        update_bars(lb);
        return 0;
    case LB_ADDFILE: {
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((LPCWSTR)lparam, &fd);
        if (find == INVALID_HANDLE_VALUE)
            return LB_ERR;
        FindClose(find);
        LRESULT r = add_file_line(lb, &fd);
        invalidate_all(lb);
        return r;
    }
    case LB_SETTOPINDEX:
        if (wparam && ((int)wparam < 0 || (int)wparam >= lb->count)) {
            SetLastError(ERROR_INVALID_INDEX);
            return LB_ERR;
        }
        if (lb->count)
            set_top(lb, (int)wparam);
        return 0;
    case LB_GETITEMRECT: {
        RECT *r = (RECT *)lparam;
        int index = (int)wparam;
        if (index != -1 && index != 0 && (index < 0 || index >= lb->count)) {
            SetRectEmpty(r);
            SetLastError(ERROR_INVALID_INDEX);
            return LB_ERR;
        }
        BOOL visible = item_rect(lb, index < 0 ? 0 : index, r);
        OffsetRect(r, -lb->x_origin, 0);
        return visible;
    }
    case LB_GETITEMDATA:
        if ((int)wparam < 0 || (int)wparam >= lb->count) {
            SetLastError(ERROR_INVALID_INDEX);
            return LB_ERR;
        }
        return lb->has_data ? (LRESULT)lb->data[wparam] : 0;
    case LB_SETITEMDATA:
        if ((int)wparam == -1) {
            for (int i = 0; lb->has_data && i < lb->count; i++)
                lb->data[i] = (ULONG_PTR)lparam;
            return TRUE;
        }
        if ((int)wparam < 0 || (int)wparam >= lb->count) {
            SetLastError(ERROR_INVALID_INDEX);
            return LB_ERR;
        }
        if (lb->has_data)
            lb->data[wparam] = (ULONG_PTR)lparam;
        return TRUE;
    case LB_SELITEMRANGE:
        return sel_range(lb, wparam != 0, LOWORD(lparam), HIWORD(lparam));
    case LB_SETANCHORINDEX:
        if ((int)wparam >= lb->count) {
            SetLastError(ERROR_INVALID_INDEX);
            return LB_ERR;
        }
        lb->anchor = lb->last_move = (int)wparam;
        make_visible(lb, (int)wparam, lparam != 0);
        return 0;
    case LB_GETANCHORINDEX:
        return lb->anchor;
    case LB_SETCARETINDEX:
        if (lb->sel == -1 || (lb->mode != LB_SINGLE && (int)wparam < lb->count)) {
            draw_focus(lb, FALSE);
            lb->caret = (int)wparam;
            draw_focus(lb, TRUE);
            make_visible(lb, (int)wparam, LOWORD(lparam) != 0);
            NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, (LONG)wparam + 1);
            return 0;
        }
        if ((int)wparam >= lb->count)
            SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    case LB_GETCARETINDEX:
        return lb->caret;
    case LB_SETITEMHEIGHT: {
        int height = LOWORD(lparam);
        if (height < 1 || height > 255) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return LB_ERR;
        }
        if (lb->owner_draw == 2) {
            if ((int)wparam < 0 || (int)wparam >= lb->count) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return LB_ERR;
            }
            lb->heights[wparam] = (BYTE)height;
        } else {
            lb->cy_char = height;
        }
        layout(lb);
        return 0;
    }
    case LB_GETITEMHEIGHT:
        if (lb->owner_draw != 2)
            return lb->cy_char;
        if (!lb->count)
            return system_font_height();
        if ((int)wparam >= 0 && (int)wparam < lb->count)
            return lb->heights[wparam];
        SetLastError(ERROR_INVALID_INDEX);
        return LB_ERR;
    case LBCB_CARETON:
        lb->caret_enabled = TRUE;
        lb->caret = lb->sel;
        draw_focus(lb, TRUE);
        if (IsWindowVisible(hwnd) || GetFocus() == hwnd)
            NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, lb->caret + 1);
        return lb->sel;
    case LBCB_CARETOFF:
        draw_focus(lb, FALSE);
        lb->caret_enabled = FALSE;
        return 0;
    case LB_SETLOCALE: {
        LCID locale = ConvertDefaultLocale((LCID)wparam);
        if (!IsValidLocale(locale, LCID_INSTALLED))
            return LB_ERR;
        LCID old = lb->locale;
        lb->locale = locale;
        return (LRESULT)old;
    }
    case LB_GETLOCALE:
        return (LRESULT)lb->locale;
    case LB_SETCOUNT:
        return set_count(lb, (int)wparam);
    case LB_INITSTORAGE:
        return init_storage(lb, (int)wparam, (int)lparam);
    case LB_ITEMFROMPOINT: {
        BOOL outside;
        int index = hit_test(lb, (short)LOWORD(lparam), (short)HIWORD(lparam), &outside);
        return MAKELONG(index, outside);
    }
    case LBCB_STARTTRACK:
        if (!lb->combo)
            return 0;
        lb->captured = FALSE;
        if (!wparam) {
            SetCapture(hwnd);
            lb->last_selection = lb->sel;
            lb->captured = TRUE;
        } else {
            POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            ScreenToClient(hwnd, &pt);
            button_down(lb, WM_LBUTTONDOWN, pt.x, pt.y, 0);
        }
        return 0;
    case LBCB_ENDTRACK:
        if (lb->captured)
            button_up(lb, wparam ? UP_RELEASE | UP_ACCEPT : UP_RELEASE | UP_RESTORE);
        return 0;
    case LB_GETLISTBOXINFO:
        return lb->multicolumn ? lb->per_column : lb->count;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINUSERAPI DWORD WINAPI GetListBoxInfo(HWND hwnd)
{
    return (DWORD)SendMessageW(hwnd, LB_GETLISTBOXINFO, 0, 0);
}
