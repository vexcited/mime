/*
 * the ComboBox class: an edit or static area over a list box. the list drops below or above a drop down combo and
 * stays under a simple one. while dropped it belongs to the desktop and notifies the combo, which turns the
 * notifications into its own.
 */
#include "listbox.h"

#define ID_LIST 1000
#define ID_EDIT 1001

static COMBO *combo_of(HWND hwnd)
{
    return (COMBO *)GetWindowLongPtrW(hwnd, 0);
}

static int combo_id(COMBO *cb)
{
    return (int)GetWindowLongPtrW(cb->hwnd, GWLP_ID);
}

/* always sent, to the parent or to the combo itself when it has none. */
static void notify(COMBO *cb, int code)
{
    HWND to = cb->parent ? cb->parent : cb->hwnd;
    SendMessageW(to, WM_COMMAND, MAKEWPARAM(combo_id(cb), code), (LPARAM)cb->hwnd);
}

static LRESULT to_list(COMBO *cb, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return cb->list ? SendMessageW(cb->list, msg, wparam, lparam) : CB_ERR;
}

static LRESULT to_edit(COMBO *cb, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!cb->has_edit) {
        SetLastError(ERROR_INVALID_COMBOBOX_MESSAGE);
        return CB_ERR;
    }
    return SendMessageW(cb->edit, msg, wparam, lparam);
}

static int cur_sel(COMBO *cb)
{
    return (int)to_list(cb, LB_GETCURSEL, 0, 0);
}

/* ------------------------------------------------------------------ geometry */

static int edit_height(COMBO *cb)
{
    if (cb->owner_draw && !IsRectEmpty(&cb->edit_rect))
        return cb->edit_rect.bottom - cb->edit_rect.top;
    HDC dc = GetDC(cb->hwnd);
    HGDIOBJ old = cb->font ? SelectObject(dc, cb->font) : nullptr;
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc, L"0", 1, &size);
    if (old)
        SelectObject(dc, old);
    ReleaseDC(cb->hwnd, dc);
    int height = size.cy + 2;
    if (cb->owner_draw) {
        MEASUREITEMSTRUCT mis = {ODT_COMBOBOX, (UINT)combo_id(cb), (UINT)-1, 0, (UINT)height, 0};
        SendMessageW(cb->parent ? cb->parent : cb->hwnd, WM_MEASUREITEM, (WPARAM)mis.CtlID, (LPARAM)&mis);
        height = (int)mis.itemHeight;
    }
    return height;
}

static void compute_rects(COMBO *cb)
{
    int cy_edit = edit_height(cb);
    RECT w;
    GetWindowRect(cb->hwnd, &w);
    cb->cy_combo = cy_edit + 6;
    if (cb->cy_drop == -1) {
        int drop = w.bottom - w.top - cb->cy_combo;
        cb->cy_drop = drop > 0 ? drop : 0;
    }
    int cx_button = GetSystemMetricsForDpi(SM_CXVSCROLL, UserWindowDpi(cb->hwnd));
    if (cb->droppable) {
        if (cb->right)
            cb->button_rect = (RECT){3, 2, 3 + cx_button, cy_edit + 4};
        else
            cb->button_rect = (RECT){cb->cx_combo - 2 - cx_button, 2, cb->cx_combo - 2, cy_edit + 4};
    } else {
        SetRectEmpty(&cb->button_rect);
    }
    int width = cb->cx_combo - 6 - (cb->droppable ? cx_button : 0);
    if (width < 0)
        width = 0;
    if (cb->right && cb->droppable)
        cb->edit_rect = (RECT){cb->cx_combo - 2 - width, 3, cb->cx_combo - 2, 3 + cy_edit};
    else
        cb->edit_rect = (RECT){3, 3, 3 + width, 3 + cy_edit};
}

static int drop_width(COMBO *cb)
{
    return cb->cx_drop > cb->cx_combo ? cb->cx_drop : cb->cx_combo;
}

/* recomputes the rectangles, moves the edit, then places and closes the list. */
static void position(COMBO *cb)
{
    compute_rects(cb);
    if (cb->has_edit)
        MoveWindow(cb->edit, cb->edit_rect.left, cb->edit_rect.top, cb->edit_rect.right - cb->edit_rect.left,
                   cb->edit_rect.bottom - cb->edit_rect.top, TRUE);
    if (!cb->list)
        return;
    cb->list_visible = TRUE;
    ComboHideList(cb, FALSE, FALSE);
    if (cb->droppable) {
        RECT w;
        GetWindowRect(cb->hwnd, &w);
        SetWindowPos(cb->list, nullptr, w.left, w.top + cb->cy_combo, drop_width(cb), cb->cy_drop,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    } else {
        SetWindowPos(cb->list, nullptr, 0, cb->cy_combo, drop_width(cb), cb->cy_drop,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
}

/* ------------------------------------------------------------------ the closed area */

static void invalidate_static(COMBO *cb)
{
    if (!cb->has_edit) {
        RECT r = cb->edit_rect;
        InflateRect(&r, 1, 1);
        InvalidateRect(cb->hwnd, &r, FALSE);
    }
}

static void select_all_edit(COMBO *cb)
{
    if (cb->has_edit)
        SendMessageW(cb->edit, EM_SETSEL, 0, -1);
}

/* shows the list's selection in the edit or static area. */
void ComboRefreshClosed(COMBO *cb)
{
    if (!cb->has_edit) {
        invalidate_static(cb);
        return;
    }
    if (!(GetWindowLongW(cb->hwnd, GWL_STYLE) & CBS_HASSTRINGS))
        return;
    int sel = cur_sel(cb);
    if (sel < 0) {
        SetWindowTextW(cb->edit, L"");
    } else {
        int n = (int)to_list(cb, LB_GETTEXTLEN, (WPARAM)sel, 0);
        LPWSTR text = UserAlloc(((SIZE_T)(n > 0 ? n : 0) + 1) * sizeof(WCHAR));
        if (!text)
            return;
        text[0] = 0;
        to_list(cb, LB_GETTEXT, (WPARAM)sel, (LPARAM)text);
        SetWindowTextW(cb->edit, text);
        UserFree(text);
    }
    if (cb->focus)
        select_all_edit(cb);
}

/* moves the list's caret and top to the item matching the edit's prefix, selecting it unless told not to. */
static int sync_list(COMBO *cb, BOOL select)
{
    int n = GetWindowTextLengthW(cb->edit);
    LPWSTR text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return -1;
    GetWindowTextW(cb->edit, text, n + 1);
    int found = n ? ListBoxPrefixSearch(cb->lb, -1, text) : -1;
    UserFree(text);
    to_list(cb, LB_SETCURSEL, select ? (WPARAM)found : (WPARAM)-1, 0);
    to_list(cb, LB_SETCARETINDEX, found < 0 ? 0 : (WPARAM)found, 0);
    to_list(cb, LB_SETTOPINDEX, found < 0 ? 0 : (WPARAM)found, 0);
    return found;
}

/* completes the edit's text to the item it matches exactly or by prefix. */
static void complete_edit(COMBO *cb)
{
    int n = GetWindowTextLengthW(cb->edit);
    if (!n)
        return;
    LPWSTR text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return;
    GetWindowTextW(cb->edit, text, n + 1);
    int found = ListBoxExactSearch(cb->lb, -1, text);
    if (found < 0)
        found = ListBoxPrefixSearch(cb->lb, -1, text);
    UserFree(text);
    if (found < 0)
        return;
    int len = (int)to_list(cb, LB_GETTEXTLEN, (WPARAM)found, 0);
    LPWSTR item = UserAlloc(((SIZE_T)len + 1) * sizeof(WCHAR));
    if (!item)
        return;
    to_list(cb, LB_GETTEXT, (WPARAM)found, (LPARAM)item);
    SetWindowTextW(cb->edit, item);
    UserFree(item);
    SendMessageW(cb->edit, EM_SETSEL, 0, -1);
}

/* ------------------------------------------------------------------ dropping down and closing up */

void ComboShowList(COMBO *cb, BOOL track)
{
    if (!cb->list)
        return;
    notify(cb, CBN_DROPDOWN);
    InvalidateRect(cb->hwnd, &cb->button_rect, TRUE);
    cb->list_visible = TRUE;
    if (cb->has_edit) {
        BOOL pressed = cb->mouse_down;
        int found = sync_list(cb, !pressed);
        (void)found;
        if (!pressed)
            complete_edit(cb);
    } else {
        int sel = cur_sel(cb);
        to_list(cb, LB_SETTOPINDEX, sel < 0 ? 0 : (WPARAM)sel, 0);
        to_list(cb, LBCB_CARETON, 0, 0);
        invalidate_static(cb);
    }
    RECT w;
    GetWindowRect(cb->hwnd, &w);
    RECT lw;
    GetWindowRect(cb->list, &lw);
    int h = cb->cy_drop > lw.bottom - lw.top ? cb->cy_drop : lw.bottom - lw.top;
    int count = cb->lb ? cb->lb->count : 0;
    if (count) {
        int item = (int)to_list(cb, LB_GETITEMHEIGHT, 0, 0);
        if (item <= 0)
            item = GetSystemMetrics(SM_CYMENU);
        int needed = (short)(item * count + 2);
        if (needed > 0 && needed < 0x7FFF && needed < h)
            h = needed;
        if (!(GetWindowLongW(cb->hwnd, GWL_STYLE) & CBS_NOINTEGRALHEIGHT) && item > 0)
            h -= (h - 2) % item;
    }
    int x = w.left, y = w.top + cb->cy_combo;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(cb->hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    if (y + h > mi.rcMonitor.bottom) {
        y = w.top - h;
        if (y < mi.rcMonitor.top)
            y = mi.rcMonitor.top;
    }
    SetWindowPos(cb->list, HWND_TOPMOST, x, y, drop_width(cb), h, SWP_NOACTIVATE);
    UpdateWindow(cb->hwnd);
    ShowWindow(cb->list, SW_SHOWNA);
    if (cb->lb)
        cb->lb->typed_count = 0;
    if (track)
        to_list(cb, LBCB_STARTTRACK, 0, 0);
}

/* FALSE when a notification destroyed the combo. */
BOOL ComboHideList(COMBO *cb, BOOL notify_parent, BOOL accept)
{
    if (notify_parent && (cb->droppable || accept)) {
        notify(cb, accept ? CBN_SELENDOK : CBN_SELENDCANCEL);
        if (cb->destroyed)
            return FALSE;
    }
    if (!cb->droppable || !cb->list)
        return TRUE;
    to_list(cb, LBCB_ENDTRACK, accept, 0);
    if (cb->list_visible) {
        cb->list_visible = FALSE;
        ShowWindow(cb->list, SW_HIDE);
        invalidate_static(cb);
        InvalidateRect(cb->hwnd, &cb->button_rect, TRUE);
        SetWindowPos(cb->hwnd, nullptr, 0, 0, cb->cx_combo, cb->cy_combo,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateWindow(cb->hwnd);
        if (cb->has_edit)
            complete_edit(cb);
        if (notify_parent)
            notify(cb, CBN_CLOSEUP);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ focus */

static void got_focus(COMBO *cb)
{
    if (cb->focus)
        return;
    cb->focus = TRUE;
    if (!cb->has_edit) {
        to_list(cb, LBCB_CARETON, 0, 0);
        invalidate_static(cb);
    } else {
        select_all_edit(cb);
    }
    notify(cb, CBN_SETFOCUS);
}

static void lost_focus(COMBO *cb)
{
    if (!cb->focus)
        return;
    SendMessageW(cb->hwnd, WM_LBUTTONUP, 0, 0xFFFFFFFF);
    if (ComboHideList(cb, TRUE, FALSE)) {
        if (!cb->has_edit) {
            to_list(cb, LBCB_CARETOFF, 0, 0);
            cb->focus = FALSE;
            invalidate_static(cb);
        } else {
            SendMessageW(cb->edit, EM_SETSEL, 0, 0);
        }
    }
    cb->focus = FALSE;
    notify(cb, CBN_KILLFOCUS);
}

/* ------------------------------------------------------------------ painting */

static HBRUSH area_brush(COMBO *cb, HDC dc)
{
    BOOL disabled = !IsWindowEnabled(cb->hwnd);
    BOOL readonly = cb->has_edit && (GetWindowLongW(cb->edit, GWL_STYLE) & ES_READONLY);
    UINT msg = disabled || readonly ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT;
    HWND to = cb->parent ? cb->parent : cb->hwnd;
    HBRUSH brush = (HBRUSH)SendMessageW(to, msg, (WPARAM)dc, (LPARAM)cb->hwnd);
    if (!brush) {
        brush = (HBRUSH)DefWindowProcW(cb->hwnd, msg, (WPARAM)dc, (LPARAM)cb->hwnd);
        if (!brush)
            brush = GetSysColorBrush(msg == WM_CTLCOLOREDIT ? COLOR_WINDOW : COLOR_3DFACE);
    }
    return brush;
}

static void paint_static(COMBO *cb, HDC dc)
{
    if (cb->no_redraw || !IsWindowVisible(cb->hwnd))
        return;
    BOOL disabled = !IsWindowEnabled(cb->hwnd);
    SetBkMode(dc, OPAQUE);
    HBRUSH brush = area_brush(cb, dc);
    RECT outer = cb->edit_rect;
    InflateRect(&outer, 1, 1);
    FillRect(dc, &outer, brush);
    BOOL highlight = cb->focus && !cb->list_visible;
    int sel = cur_sel(cb);
    HGDIOBJ old = cb->font ? SelectObject(dc, cb->font) : nullptr;
    LRESULT ui = SendMessageW(cb->hwnd, WM_QUERYUISTATE, 0, 0);
    if (cb->owner_draw) {
        DRAWITEMSTRUCT dis = {ODT_COMBOBOX, (UINT)combo_id(cb), (UINT)sel, ODA_DRAWENTIRE};
        dis.itemState = ODS_COMBOBOXEDIT | (highlight ? ODS_SELECTED | ODS_FOCUS : 0) | (disabled ? ODS_DISABLED : 0) |
                        ((ui & UISF_HIDEFOCUS) ? ODS_NOFOCUSRECT : 0) | ((ui & UISF_HIDEACCEL) ? ODS_NOACCEL : 0);
        dis.hwndItem = cb->hwnd;
        dis.hDC = dc;
        dis.rcItem = cb->edit_rect;
        dis.itemData = sel >= 0 ? (ULONG_PTR)to_list(cb, LB_GETITEMDATA, (WPARAM)sel, 0) : 0;
        int saved = SaveDC(dc);
        IntersectClipRect(dc, cb->edit_rect.left, cb->edit_rect.top, cb->edit_rect.right, cb->edit_rect.bottom);
        SendMessageW(cb->parent ? cb->parent : cb->hwnd, WM_DRAWITEM, (WPARAM)dis.CtlID, (LPARAM)&dis);
        RestoreDC(dc, saved);
    } else {
        if (highlight) {
            FillRect(dc, &cb->edit_rect, GetSysColorBrush(COLOR_HIGHLIGHT));
            SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else if (disabled && GetSysColor(COLOR_GRAYTEXT) != GetBkColor(dc)) {
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        }
        LPWSTR text = nullptr;
        int n = 0;
        if (sel >= 0) {
            n = (int)to_list(cb, LB_GETTEXTLEN, (WPARAM)sel, 0);
            text = n >= 0 ? UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR)) : nullptr;
            if (text)
                to_list(cb, LB_GETTEXT, (WPARAM)sel, (LPARAM)text);
        }
        UINT align = GetTextAlign(dc);
        int x = cb->edit_rect.left + 1;
        if (cb->right) {
            SetTextAlign(dc, TA_RIGHT | (cb->rtl ? TA_RTLREADING : 0));
            x = cb->edit_rect.right - 1;
        } else if (cb->rtl) {
            SetTextAlign(dc, TA_RTLREADING);
        }
        ExtTextOutW(dc, x, cb->edit_rect.top + 1, ETO_CLIPPED | ETO_OPAQUE, &cb->edit_rect, text ? text : L"",
                    text ? (UINT)n : 0, nullptr);
        SetTextAlign(dc, align);
        UserFree(text);
        if (highlight && !(ui & UISF_HIDEFOCUS))
            DrawFocusRect(dc, &cb->edit_rect);
    }
    if (old)
        SelectObject(dc, old);
}

static void paint(COMBO *cb, HDC dc)
{
    RECT r = {0, 0, cb->cx_combo, cb->cy_combo};
    DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
    if (cb->droppable) {
        UINT state = DFCS_SCROLLCOMBOBOX | (cb->button_pressed ? DFCS_PUSHED | DFCS_FLAT : 0) |
                     (IsWindowEnabled(cb->hwnd) ? 0 : DFCS_INACTIVE);
        RECT button = cb->button_rect;
        DrawFrameControl(dc, &button, DFC_SCROLL, state);
        if (cb->right)
            r.left = cb->button_rect.right;
        else
            r.right = cb->button_rect.left;
    }
    if (cb->has_edit) {
        FillRect(dc, &r, area_brush(cb, dc));
        return;
    }
    paint_static(cb, dc);
}

static void press_button(COMBO *cb, BOOL pressed)
{
    if (cb->button_pressed == pressed)
        return;
    cb->button_pressed = pressed;
    InvalidateRect(cb->hwnd, &cb->button_rect, TRUE);
    UpdateWindow(cb->hwnd);
}

/* ------------------------------------------------------------------ creation */

static LRESULT nc_create(HWND hwnd, CREATESTRUCTW *cs)
{
    COMBO *cb = UserAlloc(sizeof(COMBO));
    if (!cb)
        return FALSE;
    SetWindowLongPtrW(hwnd, 0, (LONG_PTR)cb);
    cb->hwnd = hwnd;
    DWORD style = (DWORD)cs->style;
    cb->list_scroll = style & (WS_VSCROLL | WS_HSCROLL);
    if (!(style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)))
        style |= CBS_HASSTRINGS;
    style &= ~(WS_VSCROLL | WS_HSCROLL | WS_BORDER);
    SetWindowLongW(hwnd, GWL_STYLE, (LONG)style);
    SetWindowLongW(hwnd, GWL_EXSTYLE,
                   (LONG)((DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE) & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)));
    return TRUE;
}

static LRESULT create(COMBO *cb, const CREATESTRUCTW *cs)
{
    DWORD style = (DWORD)GetWindowLongW(cb->hwnd, GWL_STYLE), ex = (DWORD)cs->dwExStyle;
    DWORD kind = style & 3;
    cb->parent = cs->hwndParent;
    cb->droppable = kind == CBS_DROPDOWN || kind == CBS_DROPDOWNLIST;
    cb->editable = kind != CBS_DROPDOWNLIST;
    cb->rtl = (ex & WS_EX_RTLREADING) != 0;
    cb->right = (ex & WS_EX_RIGHT) != 0;
    cb->upper = (style & CBS_UPPERCASE) != 0;
    cb->lower = !cb->upper && (style & CBS_LOWERCASE);
    cb->owner_draw = (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) != 0;
    RECT w;
    GetWindowRect(cb->hwnd, &w);
    cb->cx_combo = w.right - w.left;
    cb->cy_drop = -1;
    compute_rects(cb);
    DWORD list_style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LBS_COMBOBOX | LBS_NOTIFY | cb->list_scroll;
    if (style & WS_DISABLED)
        list_style |= WS_DISABLED;
    if (style & CBS_NOINTEGRALHEIGHT)
        list_style |= LBS_NOINTEGRALHEIGHT;
    if (style & CBS_SORT)
        list_style |= LBS_SORT;
    if (style & CBS_HASSTRINGS)
        list_style |= LBS_HASSTRINGS;
    if (style & CBS_DISABLENOSCROLL)
        list_style |= LBS_DISABLENOSCROLL;
    if (style & CBS_OWNERDRAWFIXED)
        list_style |= LBS_OWNERDRAWFIXED;
    else if (style & CBS_OWNERDRAWVARIABLE)
        list_style |= LBS_OWNERDRAWVARIABLE;
    if (cb->droppable)
        list_style |= WS_BORDER;
    DWORD list_ex = (ex & (WS_EX_RIGHT | WS_EX_RTLREADING | WS_EX_LEFTSCROLLBAR)) |
                    (cb->droppable ? WS_EX_TOOLWINDOW : WS_EX_CLIENTEDGE);
    int list_height = cb->cy_drop;
    cb->list = CreateWindowExW(list_ex, L"ComboLBox", nullptr, list_style, 0, cb->cy_combo, drop_width(cb), list_height,
                               cb->hwnd, (HMENU)(UINT_PTR)ID_LIST, cs->hInstance, nullptr);
    if (!cb->list)
        return -1;
    cb->lb = ListBoxOf(cb->list);
    if (cb->editable) {
        DWORD edit_style = WS_CHILD | WS_VISIBLE | ES_NOHIDESEL | ES_COMBOBOX | (style & WS_DISABLED);
        if (style & CBS_AUTOHSCROLL)
            edit_style |= ES_AUTOHSCROLL;
        if (style & CBS_OEMCONVERT)
            edit_style |= ES_OEMCONVERT;
        if (cb->upper)
            edit_style |= ES_UPPERCASE;
        else if (cb->lower)
            edit_style |= ES_LOWERCASE;
        if (cb->right)
            edit_style |= ES_RIGHT;
        cb->edit = CreateWindowExW(ex & (WS_EX_RIGHT | WS_EX_RTLREADING | WS_EX_LEFTSCROLLBAR), L"Edit", nullptr,
                                   edit_style, cb->edit_rect.left, cb->edit_rect.top,
                                   cb->edit_rect.right - cb->edit_rect.left, cb->edit_rect.bottom - cb->edit_rect.top,
                                   cb->hwnd, (HMENU)(UINT_PTR)ID_EDIT, cs->hInstance, nullptr);
        if (!cb->edit)
            return -1;
        cb->has_edit = TRUE;
    } else {
        cb->edit = cb->hwnd;
    }
    if (cb->droppable) {
        ShowWindow(cb->list, SW_HIDE);
        SetParent(cb->list, nullptr);
        position(cb);
    }
    return 0;
}

/* ------------------------------------------------------------------ commands from the children */

static void command(COMBO *cb, WPARAM wparam, LPARAM lparam)
{
    HWND from = (HWND)lparam;
    int code = HIWORD(wparam);
    if (cb->has_edit && (LOWORD((ULONG_PTR)from) == LOWORD((ULONG_PTR)cb->edit))) {
        switch (code) {
        case EN_SETFOCUS:
            got_focus(cb);
            break;
        case EN_KILLFOCUS:
            break;
        case EN_UPDATE:
            notify(cb, CBN_EDITUPDATE);
            break;
        case EN_CHANGE:
            notify(cb, CBN_EDITCHANGE);
            sync_list(cb, FALSE);
            break;
        case EN_ERRSPACE:
            notify(cb, CBN_ERRSPACE);
            break;
        }
        return;
    }
    if (from != cb->list)
        return;
    switch (code) {
    case LBN_SELCHANGE:
    case LBN_SELCANCEL:
        if (cb->keyboard_sel && cb->list_visible) {
            cb->keyboard_sel = FALSE;
        } else if (!ComboHideList(cb, TRUE, TRUE)) {
            return;
        }
        notify(cb, CBN_SELCHANGE);
        ComboRefreshClosed(cb);
        if (!cb->has_edit)
            NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, cb->hwnd, OBJID_CLIENT, 0);
        break;
    case LBN_DBLCLK:
        notify(cb, CBN_DBLCLK);
        break;
    }
}

/* ------------------------------------------------------------------ input */

static void button_down(COMBO *cb, int x, int y)
{
    if (GetFocus() != cb->edit && GetFocus() != cb->hwnd) {
        SetFocus(cb->hwnd);
        if (GetFocus() != cb->edit && GetFocus() != cb->hwnd)
            return;
    }
    POINT pt = {x, y};
    if (!cb->droppable || (cb->has_edit && !PtInRect(&cb->button_rect, pt)))
        return;
    press_button(cb, TRUE);
    if (cb->list_visible) {
        cb->mouse_down = FALSE;
        press_button(cb, FALSE);
        ComboHideList(cb, TRUE, TRUE);
        return;
    }
    cb->mouse_down = TRUE;
    ComboShowList(cb, FALSE);
    SetCapture(cb->hwnd);
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, cb->hwnd, OBJID_CLIENT, 1);
}

static void mouse_move(COMBO *cb, int x, int y)
{
    if (!cb->mouse_down)
        return;
    POINT pt = {x, y};
    press_button(cb, PtInRect(&cb->button_rect, pt));
    POINT screen = pt;
    ClientToScreen(cb->hwnd, &screen);
    RECT lr;
    GetClientRect(cb->list, &lr);
    POINT in_list = screen;
    ScreenToClient(cb->list, &in_list);
    if (PtInRect(&lr, in_list)) {
        cb->mouse_down = FALSE;
        press_button(cb, FALSE);
        ReleaseCapture();
        if (cb->has_edit)
            sync_list(cb, TRUE);
        SendMessageW(cb->list, WM_LBUTTONDOWN, 0, MAKELPARAM(in_list.x, in_list.y));
    }
}

static void button_up(COMBO *cb)
{
    press_button(cb, FALSE);
    if (!cb->mouse_down)
        return;
    cb->mouse_down = FALSE;
    if (cb->has_edit) {
        sync_list(cb, TRUE);
        complete_edit(cb);
    }
    ReleaseCapture();
    to_list(cb, LBCB_STARTTRACK, 0, 0);
}

static int g_wheel;

static LRESULT wheel(COMBO *cb, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (wparam & (MK_SHIFT | MK_CONTROL))
        return DefWindowProcW(cb->hwnd, msg, wparam, lparam);
    if (cb->list_visible)
        return SendMessageW(cb->list, msg, wparam, lparam);
    if (cb->extended_ui)
        return 1;
    int delta = (short)HIWORD(wparam);
    if ((g_wheel > 0) != (delta > 0))
        g_wheel = 0;
    g_wheel += delta;
    HWND target = cb->has_edit ? cb->edit : cb->hwnd;
    while (g_wheel >= WHEEL_DELTA || g_wheel <= -WHEEL_DELTA) {
        BOOL up = g_wheel > 0;
        g_wheel += up ? -WHEEL_DELTA : WHEEL_DELTA;
        UINT vk = msg == WM_MOUSEWHEEL ? (up ? VK_UP : VK_DOWN) : (up ? VK_RIGHT : VK_LEFT);
        SendMessageW(target, WM_KEYDOWN, vk, 0);
    }
    return 1;
}

/* a drop down list combo keeps the focus itself, and most keys go to the list. */
static void key_down(COMBO *cb, WPARAM vk, LPARAM lparam)
{
    if ((vk == VK_RETURN || vk == VK_ESCAPE) && cb->list_visible) {
        ComboHideList(cb, TRUE, vk == VK_RETURN);
        return;
    }
    if (cb->has_edit) {
        /* the keys the edit hands over: page keys, F4 and the arrows. */
        if (vk == VK_UP || vk == VK_DOWN) {
            if (cb->list_visible || !cb->extended_ui) {
                to_list(cb, WM_KEYDOWN, vk, lparam);
            } else {
                cb->extended_ui = FALSE;
                to_list(cb, WM_KEYDOWN, VK_F4, 0);
                cb->extended_ui = TRUE;
            }
            return;
        }
    }
    to_list(cb, WM_KEYDOWN, vk, lparam);
}

/* ------------------------------------------------------------------ the procedure */

static LRESULT get_text(COMBO *cb, int max, LPWSTR buffer)
{
    if (cb->has_edit)
        return SendMessageW(cb->edit, WM_GETTEXT, (WPARAM)max, (LPARAM)buffer);
    int sel = cur_sel(cb);
    if (!buffer || !max) {
        return 0;
    }
    buffer[0] = 0;
    if (sel < 0)
        return 0;
    int n = (int)to_list(cb, LB_GETTEXTLEN, (WPARAM)sel, 0);
    LPWSTR text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return 0;
    to_list(cb, LB_GETTEXT, (WPARAM)sel, (LPARAM)text);
    if (n >= max) {
        memcpy(buffer, text, (SIZE_T)(max - 1) * sizeof(WCHAR));
        buffer[max - 1] = 0;
        UserFree(text);
        return max;
    }
    memcpy(buffer, text, ((SIZE_T)n + 1) * sizeof(WCHAR));
    UserFree(text);
    return n;
}

static LRESULT set_item_height(COMBO *cb, WPARAM index, LPARAM lparam)
{
    if ((int)index != -1)
        return to_list(cb, LB_SETITEMHEIGHT, index, lparam);
    if (HIWORD(lparam))
        return CB_ERR;
    int height = LOWORD(lparam);
    if (height > 255) {
        SetLastError(ERROR_INVALID_EDIT_HEIGHT);
        return CB_ERR;
    }
    cb->edit_rect.bottom = cb->edit_rect.top + height;
    cb->cy_combo = height + 6;
    cb->button_rect.bottom = height + 4;
    if (cb->has_edit)
        MoveWindow(cb->edit, cb->edit_rect.left, cb->edit_rect.top, cb->edit_rect.right - cb->edit_rect.left, height,
                   TRUE);
    RECT lw;
    GetWindowRect(cb->list, &lw);
    if (!cb->droppable) {
        MoveWindow(cb->list, 0, cb->cy_combo, cb->cx_combo, cb->cy_drop, FALSE);
        SetWindowPos(cb->hwnd, nullptr, 0, 0, cb->cx_combo, cb->cy_combo + (lw.bottom - lw.top),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        RECT w;
        GetWindowRect(cb->hwnd, &w);
        SetWindowPos(cb->list, nullptr, w.left, w.top + cb->cy_combo, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(cb->hwnd, nullptr, 0, 0, cb->cx_combo, cb->cy_combo, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return 0;
}

static BOOL combo_info(COMBO *cb, COMBOBOXINFO *info)
{
    if (!info || info->cbSize != sizeof(COMBOBOXINFO)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    info->rcItem = cb->edit_rect;
    info->rcButton = cb->button_rect;
    info->stateButton = !cb->droppable ? STATE_SYSTEM_INVISIBLE : cb->button_pressed ? STATE_SYSTEM_PRESSED : 0;
    info->hwndCombo = cb->hwnd;
    info->hwndItem = cb->has_edit ? cb->edit : nullptr;
    info->hwndList = cb->list;
    return TRUE;
}

static BOOL needs_list(UINT msg)
{
    switch (msg) {
    case WM_SETFONT:
    case WM_STYLECHANGED:
    case WM_KEYDOWN:
    case WM_CHAR:
    case WM_SYSKEYDOWN:
    case WM_COMMAND:
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SETREDRAW:
    case CB_ADDSTRING:
    case CB_DIR:
    case CB_SETDROPPEDWIDTH:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_CAPTURECHANGED:
    case WM_PRINT:
    case WM_PRINTCLIENT:
        return TRUE;
    }
    return FALSE;
}

LRESULT CALLBACK ComboBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    COMBO *cb = combo_of(hwnd);
    if (!cb) {
        if (msg == WM_NCCREATE)
            return nc_create(hwnd, (CREATESTRUCTW *)lparam);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (!cb->list && needs_list(msg) && msg != WM_COMMAND)
        return CB_ERR;
    switch (msg) {
    case WM_CREATE:
        return create(cb, (const CREATESTRUCTW *)lparam);
    case WM_NCDESTROY:
        cb->destroyed = TRUE;
        if (cb->list)
            DestroyWindow(cb->list);
        SetWindowLongPtrW(hwnd, 0, 0);
        UserFree(cb);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_DESTROY:
        cb->destroyed = TRUE;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SIZE: {
        if (!cb->list)
            return 0;
        int width = LOWORD(lparam), height = HIWORD(lparam);
        if (!width || !height)
            return 0;
        if (width == cb->cx_combo && (height == cb->cy_combo || (cb->list_visible && height == cb->cy_combo + cb->cy_drop)))
            return 0;
        cb->cx_combo = width;
        if (height > cb->cy_combo)
            cb->cy_drop = -1;
        position(cb);
        return 1;
    }
    case WM_SETFONT:
        cb->font = (HFONT)wparam;
        if (cb->has_edit)
            SendMessageW(cb->edit, WM_SETFONT, wparam, FALSE);
        SendMessageW(cb->list, WM_SETFONT, wparam, FALSE);
        position(cb);
        if (LOWORD(lparam))
            InvalidateRect(hwnd, nullptr, TRUE);
        return 1;
    case WM_GETFONT:
        return (LRESULT)cb->font;
    case WM_SETREDRAW:
        cb->no_redraw = !wparam;
        if (cb->has_edit)
            SendMessageW(cb->edit, WM_SETREDRAW, wparam, lparam);
        return SendMessageW(cb->list, WM_SETREDRAW, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (msg == WM_PRINTCLIENT || (!cb->no_redraw && IsWindowVisible(hwnd)))
            paint(cb, dc);
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 1;
    }
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        if (cb->has_edit)
            EnableWindow(cb->edit, wparam != 0);
        EnableWindow(cb->list, wparam != 0);
        return 1;
    case WM_SETFOCUS:
        g_wheel = 0;
        if (!cb->has_edit)
            got_focus(cb);
        else
            SetFocus(cb->edit);
        return 1;
    case WM_KILLFOCUS: {
        HWND to = (HWND)wparam;
        if (!to || (to != cb->hwnd && !IsChild(cb->hwnd, to) && to != cb->list))
            lost_focus(cb);
        if (cb->lb)
            cb->lb->typed_count = 0;
        return 1;
    }
    case WM_COMMAND:
        command(cb, wparam, lparam);
        return 0;
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        return SendMessageW(cb->parent ? cb->parent : GetParent(hwnd), msg, wparam, lparam);
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        dis->CtlType = ODT_COMBOBOX;
        dis->CtlID = (UINT)combo_id(cb);
        dis->hwndItem = hwnd;
        return SendMessageW(cb->parent, msg, (WPARAM)dis->CtlID, lparam);
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lparam;
        mis->CtlType = ODT_COMBOBOX;
        mis->CtlID = (UINT)combo_id(cb);
        return SendMessageW(cb->parent, msg, (WPARAM)mis->CtlID, lparam);
    }
    case WM_DELETEITEM: {
        DELETEITEMSTRUCT *dis = (DELETEITEMSTRUCT *)lparam;
        dis->CtlType = ODT_COMBOBOX;
        dis->CtlID = (UINT)combo_id(cb);
        dis->hwndItem = hwnd;
        return SendMessageW(cb->parent, msg, (WPARAM)dis->CtlID, lparam);
    }
    case WM_COMPAREITEM: {
        COMPAREITEMSTRUCT *cis = (COMPAREITEMSTRUCT *)lparam;
        cis->CtlType = ODT_COMBOBOX;
        cis->CtlID = (UINT)combo_id(cb);
        cis->hwndItem = hwnd;
        return SendMessageW(cb->parent, msg, (WPARAM)cis->CtlID, lparam);
    }
    case WM_PARENTNOTIFY:
        if (LOWORD(wparam) == WM_DESTROY) {
            HWND child = (HWND)lparam;
            if (cb->has_edit && child == cb->edit) {
                cb->has_edit = FALSE;
                cb->editable = FALSE;
                cb->edit = hwnd;
            } else if (child == cb->list) {
                cb->droppable = FALSE;
                cb->list = nullptr;
                cb->lb = nullptr;
            }
        }
        return 1;
    case WM_GETDLGCODE: {
        LRESULT code = DLGC_WANTARROWS | DLGC_WANTCHARS;
        MSG *m = (MSG *)lparam;
        if (m && m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE) && cb->list_visible)
            code |= DLGC_WANTMESSAGE;
        return code;
    }
    case WM_STYLECHANGED: {
        DWORD ex = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);
        cb->rtl = (ex & WS_EX_RTLREADING) != 0;
        cb->right = (ex & WS_EX_RIGHT) != 0;
        position(cb);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_SETTEXT:
    case WM_CUT:
    case WM_COPY:
    case WM_PASTE:
    case WM_CLEAR:
        return to_edit(cb, msg, wparam, lparam);
    case WM_GETTEXT:
        return get_text(cb, (int)wparam, (LPWSTR)lparam);
    case WM_GETTEXTLENGTH: {
        if (cb->has_edit)
            return SendMessageW(cb->edit, msg, wparam, lparam);
        int sel = cur_sel(cb);
        return sel < 0 ? 0 : to_list(cb, LB_GETTEXTLEN, (WPARAM)sel, 0);
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        button_down(cb, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_MOUSEMOVE:
        mouse_move(cb, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_LBUTTONUP:
        button_up(cb);
        return 0;
    case WM_CAPTURECHANGED:
        if (cb->mouse_down) {
            cb->mouse_down = FALSE;
            press_button(cb, FALSE);
            if (cb->list_visible)
                ComboHideList(cb, TRUE, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return wheel(cb, msg, wparam, lparam);
    case WM_KEYDOWN:
        key_down(cb, wparam, lparam);
        return 0;
    case WM_CHAR:
        if (cb->has_edit) {
            if ((wparam == L'\r' || wparam == 0x1B) && cb->list_visible)
                key_down(cb, wparam == L'\r' ? VK_RETURN : VK_ESCAPE, 0);
            return 0;
        }
        return to_list(cb, WM_CHAR, wparam, lparam);
    case WM_SYSKEYDOWN:
        if ((lparam & 0x20000000) && (wparam == VK_UP || wparam == VK_DOWN) && cb->droppable) {
            if (cb->list_visible)
                ComboHideList(cb, TRUE, TRUE);
            else
                ComboShowList(cb, TRUE);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case CB_GETEDITSEL:
        return to_edit(cb, EM_GETSEL, wparam, lparam);
    case CB_LIMITTEXT:
        return to_edit(cb, EM_LIMITTEXT, wparam, lparam);
    case CB_SETEDITSEL:
        return to_edit(cb, EM_SETSEL, (WPARAM)(short)LOWORD(lparam), (LPARAM)(short)HIWORD(lparam));
    case CB_ADDSTRING:
        return to_list(cb, cb->upper ? LB_ADDSTRINGUPPER : cb->lower ? LB_ADDSTRINGLOWER : LB_ADDSTRING, wparam, lparam);
    case CB_INSERTSTRING:
        return to_list(cb, cb->upper ? LB_INSERTSTRINGUPPER : cb->lower ? LB_INSERTSTRINGLOWER : LB_INSERTSTRING, wparam,
                       lparam);
    case CB_DELETESTRING:
        return to_list(cb, LB_DELETESTRING, wparam, lparam);
    case CB_DIR:
        return cb->lb ? ListBoxDir(cb->lb, (UINT)wparam, (LPCWSTR)lparam) : CB_ERR;
    case CB_GETCOUNT:
        return to_list(cb, LB_GETCOUNT, 0, 0);
    case CB_GETCURSEL:
        return to_list(cb, LB_GETCURSEL, 0, 0);
    case CB_GETLBTEXT:
        return to_list(cb, LB_GETTEXT, wparam, lparam);
    case CB_GETLBTEXTLEN:
        return to_list(cb, LB_GETTEXTLEN, wparam, lparam);
    case CB_RESETCONTENT:
        to_list(cb, LB_RESETCONTENT, 0, 0);
        ComboRefreshClosed(cb);
        return 1;
    case CB_FINDSTRING:
        return to_list(cb, LB_FINDSTRING, wparam, lparam);
    case CB_FINDSTRINGEXACT:
        return to_list(cb, LB_FINDSTRINGEXACT, wparam, lparam);
    case CB_SELECTSTRING: {
        LRESULT r = to_list(cb, LB_SELECTSTRING, wparam, lparam);
        ComboRefreshClosed(cb);
        return r;
    }
    case CB_SETCURSEL: {
        LRESULT r = to_list(cb, LB_SETCURSEL, wparam, 0);
        if (r != CB_ERR)
            to_list(cb, LB_SETTOPINDEX, wparam, 0);
        ComboRefreshClosed(cb);
        return r;
    }
    case CB_SHOWDROPDOWN:
        if (wparam && !cb->list_visible)
            ComboShowList(cb, TRUE);
        else if (!wparam && cb->list_visible)
            ComboHideList(cb, TRUE, FALSE);
        return TRUE;
    case CB_GETITEMDATA:
        return to_list(cb, LB_GETITEMDATA, wparam, 0);
    case CB_SETITEMDATA:
        return to_list(cb, LB_SETITEMDATA, wparam, lparam);
    case CB_GETDROPPEDCONTROLRECT: {
        RECT *r = (RECT *)lparam, w;
        GetWindowRect(hwnd, &w);
        *r = (RECT){w.left, w.top, w.left + drop_width(cb), w.top + cb->cy_combo + cb->cy_drop};
        return 1;
    }
    case CB_SETITEMHEIGHT:
        return set_item_height(cb, wparam, lparam);
    case CB_GETITEMHEIGHT:
        if ((int)wparam == -1)
            return cb->edit_rect.bottom - cb->edit_rect.top;
        return to_list(cb, LB_GETITEMHEIGHT, wparam, 0);
    case CB_SETEXTENDEDUI:
        if (!cb->droppable) {
            SetLastError(ERROR_INVALID_MESSAGE);
            return CB_ERR;
        }
        if (wparam > 1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return CB_ERR;
        }
        cb->extended_ui = wparam != 0;
        return CB_OKAY;
    case CB_GETEXTENDEDUI:
        return cb->droppable && cb->extended_ui;
    case CB_GETDROPPEDSTATE:
        return cb->list_visible;
    case CB_SETLOCALE:
        return to_list(cb, LB_SETLOCALE, wparam, 0);
    case CB_GETLOCALE:
        return to_list(cb, LB_GETLOCALE, 0, 0);
    case CB_GETTOPINDEX:
        return to_list(cb, LB_GETTOPINDEX, 0, 0);
    case CB_SETTOPINDEX:
        return to_list(cb, LB_SETTOPINDEX, wparam, 0);
    case CB_GETHORIZONTALEXTENT:
        return to_list(cb, LB_GETHORIZONTALEXTENT, 0, 0);
    case CB_SETHORIZONTALEXTENT:
        return to_list(cb, LB_SETHORIZONTALEXTENT, wparam, 0);
    case CB_GETDROPPEDWIDTH:
        return cb->droppable ? drop_width(cb) : CB_ERR;
    case CB_SETDROPPEDWIDTH:
        if (!cb->droppable)
            return CB_ERR;
        if (wparam) {
            int width = (int)wparam > cb->cx_combo ? (int)wparam : cb->cx_combo;
            if (width != cb->cx_drop) {
                cb->cx_drop = width;
                position(cb);
            }
        }
        return drop_width(cb);
    case CB_INITSTORAGE:
        return to_list(cb, LB_INITSTORAGE, wparam, lparam);
    case CB_GETCOMBOBOXINFO:
        return combo_info(cb, (COMBOBOXINFO *)lparam);
    case 0x0167:
        lost_focus(cb);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* a combo box or its list answers directly, any other window is asked. */
WINUSERAPI BOOL WINAPI GetComboBoxInfo(HWND hwnd, PCOMBOBOXINFO info)
{
    if (!info || info->cbSize != sizeof(COMBOBOXINFO)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    WCHAR name[16];
    if (GetClassNameW(hwnd, name, 16) && !lstrcmpiW(name, L"ComboLBox")) {
        LISTBOX *lb = ListBoxOf(hwnd);
        if (lb && lb->combo)
            hwnd = lb->combo->hwnd;
    }
    return (BOOL)SendMessageW(hwnd, CB_GETCOMBOBOXINFO, 0, (LPARAM)info);
}
