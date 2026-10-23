/*
 * the list view, SysListView32: items with subitems shown as large icons, small icons, a list or a report.
 * report columns live in a header control.
 */
#include "comctl32_private.h"

#define OWNERDATA_MAX 100000000
#define LVIF_ALL (LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM | LVIF_STATE | LVIF_INDENT)

typedef struct LV_SUBITEM {
    LPWSTR Text;
    int Image;
} LV_SUBITEM;

typedef struct LV_ITEM {
    LPWSTR Text;
    int Image;
    int Indent;
    UINT State;
    LPARAM Param;
    LV_SUBITEM *Sub;
    int SubCount;
    POINT Pos;
} LV_ITEM;

typedef struct LISTVIEW {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style, ExStyle;
    int View;
    LV_ITEM **Items;
    int Count, Capacity;
    /* owner data item states, one byte each. */
    BYTE *DataStates;
    int DataStatesSize;
    HWND Header;
    int HeaderHeight;
    HIMAGELIST Normal, Small, StateList;
    BOOL OwnStateList;
    COLORREF Bk, Text, TextBk;
    HFONT Font;
    BOOL DefaultFont;
    int FontHeight, AveChar;
    int Focus, Mark, Hot;
    UINT CallbackMask;
    /* report and icon views scroll vertically by pixels kept here, report by whole rows. */
    int ScrollX, ScrollY;
    int RowHeight, ColumnWidth;
    int SpacingX, SpacingY;
    HTHEME Theme;
    BOOL ThemedItems;
    BOOL Redraw;
    BOOL InScroll;
} LISTVIEW;

static LISTVIEW *get_data(HWND hwnd)
{
    return (LISTVIEW *)GetWindowLongPtrW(hwnd, 0);
}

static inline BOOL is_ownerdata(LISTVIEW *lv)
{
    return (lv->Style & LVS_OWNERDATA) != 0;
}

/* ------------------------------------------------------------------ item storage */

static UINT get_state(LISTVIEW *lv, int i)
{
    if (i < 0 || i >= lv->Count)
        return 0;
    if (!is_ownerdata(lv))
        return lv->Items[i]->State;
    UINT state = i < lv->DataStatesSize ? lv->DataStates[i] : 0;
    if (i == lv->Focus)
        state |= LVIS_FOCUSED;
    return state;
}

static void put_state(LISTVIEW *lv, int i, UINT state)
{
    if (!is_ownerdata(lv)) {
        lv->Items[i]->State = state;
        return;
    }
    if (i >= lv->DataStatesSize) {
        if (!(state & ~LVIS_FOCUSED))
            return;
        int size = i + 64 > lv->Count ? lv->Count : i + 64;
        BYTE *states = CcReAlloc(lv->DataStates, (SIZE_T)size);
        if (!states)
            return;
        lv->DataStates = states;
        lv->DataStatesSize = size;
    }
    lv->DataStates[i] = (BYTE)(state & (LVIS_SELECTED | LVIS_CUT | LVIS_DROPHILITED | LVIS_GLOW | LVIS_ACTIVATING));
}

static LPARAM get_param(LISTVIEW *lv, int i)
{
    return !is_ownerdata(lv) && i >= 0 && i < lv->Count ? lv->Items[i]->Param : 0;
}

static void free_item(LV_ITEM *it)
{
    if (it->Text != LPSTR_TEXTCALLBACKW)
        CcFree(it->Text);
    for (int k = 0; k < it->SubCount; k++)
        if (it->Sub[k].Text != LPSTR_TEXTCALLBACKW)
            CcFree(it->Sub[k].Text);
    CcFree(it->Sub);
    CcFree(it);
}

static int column_count(LISTVIEW *lv)
{
    return lv->Header ? (int)SendMessageW(lv->Header, HDM_GETITEMCOUNT, 0, 0) : 0;
}

/* created on demand. unset subitems are callbacks. */
static LV_SUBITEM *subitem(LV_ITEM *it, int sub, BOOL create)
{
    if (sub < 1)
        return nullptr;
    if (sub > it->SubCount) {
        if (!create)
            return nullptr;
        LV_SUBITEM *s = CcReAlloc(it->Sub, (SIZE_T)sub * sizeof(LV_SUBITEM));
        if (!s)
            return nullptr;
        for (int k = it->SubCount; k < sub; k++) {
            s[k].Text = LPSTR_TEXTCALLBACKW;
            s[k].Image = I_IMAGECALLBACK;
        }
        it->Sub = s;
        it->SubCount = sub;
    }
    return &it->Sub[sub - 1];
}

/* ------------------------------------------------------------------ metrics */

static int image_size(HIMAGELIST list, int *cx, int *cy)
{
    int w = 0, h = 0;
    if (list)
        ImageList_GetIconSize(list, &w, &h);
    if (cx)
        *cx = w;
    if (cy)
        *cy = h;
    return w;
}

static void measure_font(LISTVIEW *lv)
{
    HDC dc = GetDC(lv->Hwnd);
    HGDIOBJ old = SelectObject(dc, lv->Font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    lv->FontHeight = tm.tmHeight;
    lv->AveChar = tm.tmAveCharWidth;
    SelectObject(dc, old);
    ReleaseDC(lv->Hwnd, dc);
}

/* the row height: font, images and a border, at least the font plus the in place edit's padding. */
static void compute_metrics(LISTVIEW *lv)
{
    int image_cy = 0, state_cy = 0;
    image_size(lv->View == LV_VIEW_ICON ? lv->Normal : lv->Small, nullptr, &image_cy);
    image_size(lv->StateList, nullptr, &state_cy);
    int cy = lv->FontHeight;
    if (image_cy > cy)
        cy = image_cy;
    if (state_cy > cy)
        cy = state_cy;
    cy += GetSystemMetrics(SM_CYBORDER);
    if ((lv->View == LV_VIEW_DETAILS || lv->View == LV_VIEW_SMALLICON) && cy < lv->FontHeight + 2)
        cy = lv->FontHeight + 2;
    lv->RowHeight = cy > 0 ? cy : 1;
    if (!lv->ColumnWidth || lv->View != LV_VIEW_LIST) {
        int state_cx = image_size(lv->StateList, nullptr, nullptr);
        int w = GetSystemMetrics(SM_CXSMICON) + 16 * lv->AveChar + state_cx;
        lv->ColumnWidth = w > 0 ? w : GetSystemMetrics(SM_CXBORDER);
    }
}

static void client_rect(LISTVIEW *lv, RECT *r)
{
    GetClientRect(lv->Hwnd, r);
}

static int header_bottom(LISTVIEW *lv)
{
    return lv->View == LV_VIEW_DETAILS ? lv->HeaderHeight : 0;
}

static int visible_rows(LISTVIEW *lv)
{
    RECT r;
    client_rect(lv, &r);
    int rows = (r.bottom - header_bottom(lv)) / lv->RowHeight;
    return rows > 0 ? rows : 0;
}

static int rows_per_column(LISTVIEW *lv)
{
    RECT r;
    client_rect(lv, &r);
    int rows = r.bottom / lv->RowHeight;
    return rows > 0 ? rows : 1;
}

/* the left of each column in display order, and the total width. */
static int columns_width(LISTVIEW *lv)
{
    int total = 0, n = column_count(lv);
    for (int c = 0; c < n; c++) {
        RECT r;
        if (SendMessageW(lv->Header, HDM_GETITEMRECT, (WPARAM)c, (LPARAM)&r))
            total += r.right - r.left;
    }
    return total;
}

static BOOL column_rect(LISTVIEW *lv, int column, RECT *r)
{
    if (!lv->Header || !SendMessageW(lv->Header, HDM_GETITEMRECT, (WPARAM)column, (LPARAM)r))
        return FALSE;
    OffsetRect(r, -lv->ScrollX, 0);
    return TRUE;
}

static int string_width(LISTVIEW *lv, LPCWSTR text)
{
    if (!text || CcIsCallback(text) || !*text)
        return 0;
    HDC dc = GetDC(lv->Hwnd);
    HGDIOBJ old = SelectObject(dc, lv->Font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    ReleaseDC(lv->Hwnd, dc);
    return size.cx;
}

/* ------------------------------------------------------------------ notifications */

static LRESULT notify_listview(LISTVIEW *lv, UINT code, int item, int sub, UINT new_state, UINT old_state,
                               UINT changed, LPARAM param)
{
    NMLISTVIEW nm = {};
    nm.iItem = item;
    nm.iSubItem = sub;
    nm.uNewState = new_state;
    nm.uOldState = old_state;
    nm.uChanged = changed;
    nm.lParam = param;
    return CcNotify(&lv->Notify, code, &nm.hdr);
}

static void get_dispinfo(LISTVIEW *lv, int i, int sub, UINT mask, LPWSTR text, int max, int *image, int *indent,
                         UINT *state)
{
    LV_ITEM *it = !is_ownerdata(lv) ? lv->Items[i] : nullptr;
    if (lv->Notify.Unicode) {
        NMLVDISPINFOW nm = {};
        nm.item.mask = mask;
        nm.item.iItem = i;
        nm.item.iSubItem = sub;
        nm.item.state = state ? *state : 0;
        nm.item.stateMask = lv->CallbackMask & ((mask & LVIF_STATE) ? ~0u : 0u);
        nm.item.pszText = text;
        nm.item.cchTextMax = max;
        nm.item.iImage = image ? *image : -1;
        nm.item.iIndent = 0;
        nm.item.lParam = get_param(lv, i);
        if (text && max > 0)
            text[0] = 0;
        CcNotify(&lv->Notify, LVN_GETDISPINFOW, &nm.hdr);
        if ((mask & LVIF_TEXT) && text && nm.item.pszText && nm.item.pszText != text &&
            !CcIsCallback(nm.item.pszText))
            CcCopyText(text, max, nm.item.pszText);
        if (image && (mask & LVIF_IMAGE))
            *image = nm.item.iImage;
        if (indent && (mask & LVIF_INDENT))
            *indent = nm.item.iIndent;
        if (state && (mask & LVIF_STATE))
            *state = (*state & ~nm.item.stateMask) | (nm.item.state & nm.item.stateMask);
        if (it && (nm.item.mask & LVIF_DI_SETITEM)) {
            if (sub == 0) {
                if (mask & LVIF_TEXT)
                    CcSetText(&it->Text, text);
                if (mask & LVIF_IMAGE)
                    it->Image = nm.item.iImage;
            } else {
                LV_SUBITEM *s = subitem(it, sub, TRUE);
                if (s && (mask & LVIF_TEXT))
                    CcSetText(&s->Text, text);
                if (s && (mask & LVIF_IMAGE))
                    s->Image = nm.item.iImage;
            }
        }
    } else {
        NMLVDISPINFOA nm = {};
        int amax = max > 0 ? max * 2 : 0;
        char *buffer = amax ? CcAlloc((SIZE_T)amax) : nullptr;
        nm.item.mask = mask;
        nm.item.iItem = i;
        nm.item.iSubItem = sub;
        nm.item.state = state ? *state : 0;
        nm.item.stateMask = lv->CallbackMask & ((mask & LVIF_STATE) ? ~0u : 0u);
        nm.item.pszText = buffer;
        nm.item.cchTextMax = buffer ? max : 0;
        nm.item.iImage = image ? *image : -1;
        nm.item.lParam = get_param(lv, i);
        CcNotify(&lv->Notify, LVN_GETDISPINFOW, &nm.hdr);
        if ((mask & LVIF_TEXT) && text && max > 0) {
            text[0] = 0;
            if (nm.item.pszText && nm.item.pszText != LPSTR_TEXTCALLBACKA)
                MultiByteToWideChar(CP_ACP, 0, nm.item.pszText, -1, text, max);
            text[max - 1] = 0;
        }
        if (image && (mask & LVIF_IMAGE))
            *image = nm.item.iImage;
        if (indent && (mask & LVIF_INDENT))
            *indent = nm.item.iIndent;
        if (state && (mask & LVIF_STATE))
            *state = (*state & ~nm.item.stateMask) | (nm.item.state & nm.item.stateMask);
        if (it && (nm.item.mask & LVIF_DI_SETITEM) && sub == 0) {
            if (mask & LVIF_TEXT)
                CcSetText(&it->Text, text);
            if (mask & LVIF_IMAGE)
                it->Image = nm.item.iImage;
        }
        CcFree(buffer);
    }
}

/* what an item or subitem shows, with callbacks resolved. */
static void display_info(LISTVIEW *lv, int i, int sub, LPWSTR text, int max, int *image)
{
    UINT mask = 0;
    LPCWSTR stored = nullptr;
    int img = I_IMAGECALLBACK;
    if (is_ownerdata(lv)) {
        mask = LVIF_TEXT | LVIF_IMAGE;
    } else {
        LV_ITEM *it = lv->Items[i];
        if (sub == 0) {
            stored = it->Text;
            img = it->Image;
        } else {
            LV_SUBITEM *s = subitem(it, sub, FALSE);
            stored = s ? s->Text : LPSTR_TEXTCALLBACKW;
            img = s ? s->Image : I_IMAGECALLBACK;
        }
        if (CcIsCallback(stored))
            mask |= LVIF_TEXT;
        if (img == I_IMAGECALLBACK && (sub == 0 || (lv->ExStyle & LVS_EX_SUBITEMIMAGES)))
            mask |= LVIF_IMAGE;
    }
    if (mask)
        get_dispinfo(lv, i, sub, mask, text, max, &img, nullptr, nullptr);
    if (!(mask & LVIF_TEXT))
        CcCopyText(text, max, stored);
    if (image)
        *image = img;
}

/* ------------------------------------------------------------------ geometry */

static void ensure_header(LISTVIEW *lv);

static void layout_header(LISTVIEW *lv)
{
    if (!lv->Header)
        return;
    RECT r;
    client_rect(lv, &r);
    if (lv->View != LV_VIEW_DETAILS && !(lv->ExStyle & LVS_EX_HEADERINALLVIEWS)) {
        ShowWindow(lv->Header, SW_HIDE);
        lv->HeaderHeight = 0;
        return;
    }
    WINDOWPOS wp = {};
    HDLAYOUT layout = {&r, &wp};
    SendMessageW(lv->Header, HDM_LAYOUT, 0, (LPARAM)&layout);
    SetWindowPos(lv->Header, wp.hwndInsertAfter, wp.x - lv->ScrollX, wp.y, wp.cx + lv->ScrollX, wp.cy,
                 wp.flags | SWP_SHOWWINDOW);
    lv->HeaderHeight = wp.cy;
}

/* the item's cell in the icon views, its column and row in list view. */
static void item_origin(LISTVIEW *lv, int i, POINT *pt)
{
    RECT client;
    client_rect(lv, &client);
    switch (lv->View) {
    case LV_VIEW_DETAILS:
        pt->x = -lv->ScrollX;
        pt->y = header_bottom(lv) + (i - lv->ScrollY) * lv->RowHeight;
        break;
    case LV_VIEW_LIST: {
        int rows = rows_per_column(lv);
        pt->x = (i / rows) * lv->ColumnWidth - lv->ScrollX;
        pt->y = (i % rows) * lv->RowHeight;
        break;
    }
    default: {
        int cell_w = lv->View == LV_VIEW_ICON ? lv->SpacingX : lv->ColumnWidth;
        int cell_h = lv->View == LV_VIEW_ICON ? lv->SpacingY : lv->RowHeight;
        int per_row = (client.right - client.left) / (cell_w > 0 ? cell_w : 1);
        if (per_row < 1)
            per_row = 1;
        pt->x = (i % per_row) * cell_w - lv->ScrollX;
        pt->y = (i / per_row) * cell_h - lv->ScrollY;
        break;
    }
    }
}

typedef struct ITEM_RECTS {
    RECT Bounds, State, Icon, Label, Select;
} ITEM_RECTS;

static void item_rects(LISTVIEW *lv, int i, ITEM_RECTS *r)
{
    memset(r, 0, sizeof(*r));
    POINT pt;
    item_origin(lv, i, &pt);
    int small_cx = image_size(lv->Small, nullptr, nullptr);
    int state_cx = image_size(lv->StateList, nullptr, nullptr);
    int edge = GetSystemMetrics(SM_CXEDGE);
    WCHAR text[260];
    switch (lv->View) {
    case LV_VIEW_DETAILS: {
        RECT col0 = {};
        if (!column_rect(lv, 0, &col0))
            SetRect(&col0, -lv->ScrollX, 0, -lv->ScrollX, 0);
        int top = pt.y, bottom = pt.y + lv->RowHeight;
        int indent = 0;
        if (!is_ownerdata(lv))
            indent = lv->Items[i]->Indent;
        int x0 = col0.left;
        SetRect(&r->State, x0 + edge, top, x0 + edge + state_cx, bottom);
        int icon_left = x0 + edge + state_cx + indent * small_cx + (state_cx ? 5 : 2) - edge;
        SetRect(&r->Icon, icon_left, top, icon_left + (lv->Small ? small_cx : 0), bottom);
        SetRect(&r->Label, r->Icon.right, top, col0.right, bottom);
        if (r->Label.right < r->Label.left)
            r->Label.right = r->Label.left;
        SetRect(&r->Bounds, -lv->ScrollX, top, -lv->ScrollX + columns_width(lv), bottom);
        if (lv->ExStyle & LVS_EX_FULLROWSELECT) {
            r->Select = r->Bounds;
            r->Select.left = r->Icon.left;
        } else {
            display_info(lv, i, 0, text, 260, nullptr);
            int right = r->Label.left + string_width(lv, text) + 2 * edge;
            if (right > r->Label.right)
                right = r->Label.right;
            SetRect(&r->Select, r->Icon.left, top, right, bottom);
        }
        break;
    }
    case LV_VIEW_LIST:
    case LV_VIEW_SMALLICON: {
        int width = lv->ColumnWidth;
        SetRect(&r->State, pt.x, pt.y, pt.x + state_cx, pt.y + lv->RowHeight);
        int icon_left = pt.x + state_cx + (state_cx ? 3 : 0);
        SetRect(&r->Icon, icon_left, pt.y, icon_left + (lv->Small ? small_cx : 0), pt.y + lv->RowHeight);
        display_info(lv, i, 0, text, 260, nullptr);
        int label_right = lv->View == LV_VIEW_LIST ? pt.x + width
                                                   : r->Icon.right + string_width(lv, text) + 2 * edge;
        if (lv->View == LV_VIEW_SMALLICON && label_right > pt.x + width)
            label_right = pt.x + width;
        SetRect(&r->Label, r->Icon.right, pt.y, label_right, pt.y + lv->RowHeight);
        UnionRect(&r->Bounds, &r->State, &r->Label);
        r->Select = r->Bounds;
        if (lv->View == LV_VIEW_LIST) {
            r->Select.right = r->Icon.right + string_width(lv, text) + 2 * edge;
            if (r->Select.right > r->Label.right)
                r->Select.right = r->Label.right;
        }
        break;
    }
    default: {
        int icon_cx = 0, icon_cy = 0;
        image_size(lv->Normal, &icon_cx, &icon_cy);
        int cell_w = lv->SpacingX;
        int icon_left = pt.x + (cell_w - icon_cx) / 2;
        SetRect(&r->Icon, icon_left, pt.y + 2, icon_left + icon_cx, pt.y + 2 + icon_cy);
        SetRect(&r->State, r->Icon.left - state_cx, r->Icon.bottom - lv->RowHeight, r->Icon.left, r->Icon.bottom);
        display_info(lv, i, 0, text, 260, nullptr);
        int tw = string_width(lv, text) + 2 * edge;
        if (tw > cell_w - 2)
            tw = cell_w - 2;
        int label_left = pt.x + (cell_w - tw) / 2;
        SetRect(&r->Label, label_left, r->Icon.bottom + 2, label_left + tw, r->Icon.bottom + 2 + lv->FontHeight + 2);
        UnionRect(&r->Bounds, &r->Icon, &r->Label);
        r->Select = r->Bounds;
        break;
    }
    }
}

static void subitem_rect(LISTVIEW *lv, int i, int sub, RECT *out, BOOL icon)
{
    ITEM_RECTS r;
    item_rects(lv, i, &r);
    RECT col;
    if (!column_rect(lv, sub, &col)) {
        *out = r.Bounds;
        return;
    }
    *out = col;
    out->top = r.Bounds.top;
    out->bottom = r.Bounds.bottom;
    if (icon) {
        int small_cx = (lv->ExStyle & LVS_EX_SUBITEMIMAGES) ? image_size(lv->Small, nullptr, nullptr) : 0;
        out->left += GetSystemMetrics(SM_CXEDGE);
        out->right = out->left + small_cx;
    }
}

/* ------------------------------------------------------------------ scrolling */

static void invalidate_item(LISTVIEW *lv, int i)
{
    if (i < 0 || i >= lv->Count || !lv->Redraw)
        return;
    ITEM_RECTS r;
    item_rects(lv, i, &r);
    RECT row = r.Bounds;
    if (lv->View == LV_VIEW_DETAILS) {
        RECT client;
        client_rect(lv, &client);
        row.left = client.left;
        row.right = client.right;
    }
    InflateRect(&row, 1, 1);
    InvalidateRect(lv->Hwnd, &row, TRUE);
}

static void invalidate_all(LISTVIEW *lv)
{
    if (lv->Redraw)
        InvalidateRect(lv->Hwnd, nullptr, TRUE);
}

static int content_height(LISTVIEW *lv)
{
    RECT client;
    client_rect(lv, &client);
    if (lv->View == LV_VIEW_ICON || lv->View == LV_VIEW_SMALLICON) {
        int cell_w = lv->View == LV_VIEW_ICON ? lv->SpacingX : lv->ColumnWidth;
        int cell_h = lv->View == LV_VIEW_ICON ? lv->SpacingY : lv->RowHeight;
        int per_row = client.right / (cell_w > 0 ? cell_w : 1);
        if (per_row < 1)
            per_row = 1;
        return (lv->Count + per_row - 1) / per_row * cell_h;
    }
    return 0;
}

static void update_scrollbars(LISTVIEW *lv)
{
    if (lv->Style & LVS_NOSCROLL)
        return;
    RECT client;
    client_rect(lv, &client);
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    switch (lv->View) {
    case LV_VIEW_DETAILS: {
        int rows = visible_rows(lv);
        si.nMin = 0;
        si.nMax = lv->Count > 0 ? lv->Count - 1 : 0;
        si.nPage = (UINT)rows;
        si.nPos = lv->ScrollY;
        SetScrollInfo(lv->Hwnd, SB_VERT, &si, TRUE);
        int width = columns_width(lv);
        si.nMax = width > 0 ? width - 1 : 0;
        si.nPage = (UINT)(client.right - client.left);
        si.nPos = lv->ScrollX;
        SetScrollInfo(lv->Hwnd, SB_HORZ, &si, TRUE);
        break;
    }
    case LV_VIEW_LIST: {
        int rows = rows_per_column(lv);
        int columns = (lv->Count + rows - 1) / rows;
        int per_page = client.right / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        si.nMin = 0;
        si.nMax = columns > 0 ? columns - 1 : 0;
        si.nPage = (UINT)(per_page > 0 ? per_page : 1);
        si.nPos = lv->ScrollX / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        SetScrollInfo(lv->Hwnd, SB_HORZ, &si, TRUE);
        ShowScrollBar(lv->Hwnd, SB_VERT, FALSE);
        break;
    }
    default: {
        int height = content_height(lv);
        si.nMin = 0;
        si.nMax = height > 0 ? height - 1 : 0;
        si.nPage = (UINT)(client.bottom - client.top);
        si.nPos = lv->ScrollY;
        SetScrollInfo(lv->Hwnd, SB_VERT, &si, TRUE);
        ShowScrollBar(lv->Hwnd, SB_HORZ, FALSE);
        break;
    }
    }
}

static int clamp(int v, int low, int high)
{
    if (v > high)
        v = high;
    if (v < low)
        v = low;
    return v;
}

static void scroll_to(LISTVIEW *lv, int x, int y)
{
    RECT client;
    client_rect(lv, &client);
    int old_x = lv->ScrollX, old_y = lv->ScrollY;
    switch (lv->View) {
    case LV_VIEW_DETAILS: {
        int max_y = lv->Count - visible_rows(lv);
        lv->ScrollY = clamp(y, 0, max_y > 0 ? max_y : 0);
        int max_x = columns_width(lv) - (client.right - client.left);
        lv->ScrollX = clamp(x, 0, max_x > 0 ? max_x : 0);
        break;
    }
    case LV_VIEW_LIST: {
        int rows = rows_per_column(lv);
        int columns = (lv->Count + rows - 1) / rows;
        int per_page = client.right / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        int max_col = columns - (per_page > 0 ? per_page : 1);
        int col = x / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        lv->ScrollX = clamp(col, 0, max_col > 0 ? max_col : 0) * lv->ColumnWidth;
        break;
    }
    default: {
        int max_y = content_height(lv) - (client.bottom - client.top);
        lv->ScrollY = clamp(y, 0, max_y > 0 ? max_y : 0);
        break;
    }
    }
    if (old_x != lv->ScrollX || old_y != lv->ScrollY) {
        if (lv->View == LV_VIEW_DETAILS && old_x != lv->ScrollX)
            layout_header(lv);
        update_scrollbars(lv);
        invalidate_all(lv);
    }
}

static void ensure_visible(LISTVIEW *lv, int i, BOOL partial)
{
    RECT client;
    client_rect(lv, &client);
    switch (lv->View) {
    case LV_VIEW_DETAILS: {
        int rows = visible_rows(lv);
        int top = lv->ScrollY;
        if (i < top)
            top = i;
        else if (rows > 0 && i >= top + rows)
            top = i - rows + 1;
        else if (!rows)
            top = i;
        scroll_to(lv, lv->ScrollX, top);
        break;
    }
    case LV_VIEW_LIST: {
        int rows = rows_per_column(lv);
        int col = i / rows, first = lv->ScrollX / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        int per_page = client.right / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        if (per_page < 1)
            per_page = 1;
        if (col < first)
            first = col;
        else if (col >= first + per_page)
            first = col - per_page + 1;
        scroll_to(lv, first * lv->ColumnWidth, 0);
        break;
    }
    default: {
        ITEM_RECTS r;
        item_rects(lv, i, &r);
        int y = lv->ScrollY;
        if (r.Bounds.top < 0)
            y += r.Bounds.top;
        else if (r.Bounds.bottom > client.bottom && !(partial && r.Bounds.top < client.bottom))
            y += r.Bounds.bottom - client.bottom;
        scroll_to(lv, lv->ScrollX, y);
        break;
    }
    }
}

/* ------------------------------------------------------------------ item changes */

static void set_state(LISTVIEW *lv, int i, UINT state, UINT mask);

static void deselect_all_but(LISTVIEW *lv, int keep)
{
    for (int k = 0; k < lv->Count; k++)
        if (k != keep && (get_state(lv, k) & LVIS_SELECTED))
            set_state(lv, k, 0, LVIS_SELECTED);
}

/* LVN_ITEMCHANGING always, then the change, then LVN_ITEMCHANGED with the fields that really changed. */
static BOOL change_item(LISTVIEW *lv, int i, const LVITEMW *src, BOOL ansi)
{
    UINT mask = src->mask & ~(LVIF_DI_SETITEM | LVIF_NORECOMPUTE);
    if (!mask)
        return TRUE;
    if (i < 0 || i >= lv->Count)
        return FALSE;
    UINT state_mask = (mask & LVIF_STATE) ? src->stateMask : 0;
    UINT new_bits = src->state & state_mask;
    if ((lv->Style & LVS_SINGLESEL) && (new_bits & LVIS_SELECTED))
        deselect_all_but(lv, i);
    UINT old_state = get_state(lv, i);
    if (notify_listview(lv, LVN_ITEMCHANGING, i, 0, new_bits, old_state & state_mask, mask, get_param(lv, i)))
        return FALSE;
    if ((new_bits & LVIS_FOCUSED) && lv->Focus >= 0 && lv->Focus != i)
        set_state(lv, lv->Focus, 0, LVIS_FOCUSED);
    UINT changed = 0;
    if (!is_ownerdata(lv)) {
        LV_ITEM *it = lv->Items[i];
        if (mask & LVIF_TEXT) {
            if (ansi)
                CcSetTextA(&it->Text, (LPCSTR)src->pszText);
            else
                CcSetText(&it->Text, src->pszText);
            changed |= LVIF_TEXT;
        }
        if ((mask & LVIF_IMAGE) && it->Image != src->iImage) {
            it->Image = src->iImage;
            changed |= LVIF_IMAGE;
        }
        if ((mask & LVIF_INDENT) && it->Indent != src->iIndent) {
            it->Indent = src->iIndent;
            changed |= LVIF_INDENT;
        }
        if ((mask & LVIF_PARAM) && it->Param != src->lParam) {
            it->Param = src->lParam;
            changed |= LVIF_PARAM;
        }
    }
    UINT state = (old_state & ~state_mask) | new_bits;
    if ((state ^ old_state) & state_mask) {
        changed |= LVIF_STATE;
        put_state(lv, i, state);
        if ((state ^ old_state) & LVIS_FOCUSED) {
            if (state & LVIS_FOCUSED) {
                lv->Focus = i;
                if (lv->Mark == -1)
                    lv->Mark = i;
            } else if (lv->Focus == i) {
                lv->Focus = -1;
            }
        }
    }
    if (!changed)
        return TRUE;
    invalidate_item(lv, i);
    notify_listview(lv, LVN_ITEMCHANGED, i, 0, new_bits, old_state & state_mask, changed, get_param(lv, i));
    if (changed & LVIF_STATE) {
        if ((state ^ old_state) & LVIS_FOCUSED && (state & LVIS_FOCUSED))
            NotifyWinEvent(EVENT_OBJECT_FOCUS, lv->Hwnd, OBJID_CLIENT, i + 1);
        if ((state ^ old_state) & LVIS_SELECTED)
            NotifyWinEvent((state & LVIS_SELECTED) ? EVENT_OBJECT_SELECTIONADD : EVENT_OBJECT_SELECTIONREMOVE,
                           lv->Hwnd, OBJID_CLIENT, i + 1);
    }
    return TRUE;
}

static void set_state(LISTVIEW *lv, int i, UINT state, UINT mask)
{
    LVITEMW item = {};
    item.mask = LVIF_STATE;
    item.state = state;
    item.stateMask = mask;
    change_item(lv, i, &item, FALSE);
}

static BOOL set_subitem(LISTVIEW *lv, const LVITEMW *src, BOOL ansi)
{
    if (src->mask & ~(LVIF_TEXT | LVIF_IMAGE | LVIF_STATE | LVIF_DI_SETITEM))
        return FALSE;
    if (src->iItem < 0 || src->iItem >= lv->Count || src->iSubItem >= column_count(lv))
        return FALSE;
    if (!(src->mask & (LVIF_TEXT | LVIF_IMAGE | LVIF_STATE)))
        return TRUE;
    if (is_ownerdata(lv))
        return FALSE;
    LV_SUBITEM *s = subitem(lv->Items[src->iItem], src->iSubItem, TRUE);
    if (!s)
        return FALSE;
    if (src->mask & LVIF_TEXT) {
        if (ansi)
            CcSetTextA(&s->Text, (LPCSTR)src->pszText);
        else
            CcSetText(&s->Text, src->pszText);
    }
    if (src->mask & LVIF_IMAGE)
        s->Image = src->iImage;
    invalidate_item(lv, src->iItem);
    return TRUE;
}

static BOOL set_item(LISTVIEW *lv, const LVITEMW *src, BOOL ansi)
{
    if (!src)
        return FALSE;
    if (src->iSubItem > 0)
        return set_subitem(lv, src, ansi);
    if (!(src->mask & ~(LVIF_DI_SETITEM | LVIF_NORECOMPUTE)))
        return TRUE;
    if (src->iItem < 0 || src->iItem >= lv->Count)
        return FALSE;
    return change_item(lv, src->iItem, src, ansi);
}

static void ensure_checkbox_list(LISTVIEW *lv);

static int compare_text(LISTVIEW *lv, LPCWSTR a, int j)
{
    LPCWSTR b = lv->Items[j]->Text;
    int r = lstrcmpiW(a ? a : L"", b && !CcIsCallback(b) ? b : L"");
    return (lv->Style & LVS_SORTDESCENDING) ? -r : r;
}

static int insert_item(LISTVIEW *lv, const LVITEMW *src, BOOL ansi)
{
    if (!src || src->iSubItem != 0 || src->iItem < 0)
        return -1;
    if ((src->mask & LVIF_STATE) && (src->state & 0xFFFF0000))
        return -1;
    int index = src->iItem > lv->Count ? lv->Count : src->iItem;
    if (is_ownerdata(lv)) {
        if (index > OWNERDATA_MAX)
            return -1;
        lv->Count++;
        update_scrollbars(lv);
        invalidate_all(lv);
        return index;
    }
    LV_ITEM *it = CcAlloc(sizeof(*it));
    if (!it)
        return -1;
    BOOL sorted = (lv->Style & (LVS_SORTASCENDING | LVS_SORTDESCENDING)) != 0;
    if (src->mask & LVIF_TEXT) {
        if (sorted && (ansi ? (LPCSTR)src->pszText == LPSTR_TEXTCALLBACKA : CcIsCallback(src->pszText))) {
            CcFree(it);
            return -1;
        }
        BOOL ok = ansi ? CcSetTextA(&it->Text, (LPCSTR)src->pszText) : CcSetText(&it->Text, src->pszText);
        if (!ok) {
            CcFree(it);
            return -1;
        }
    }
    if (src->mask & LVIF_IMAGE)
        it->Image = src->iImage;
    if (src->mask & LVIF_INDENT)
        it->Indent = src->iIndent;
    if (src->mask & LVIF_PARAM)
        it->Param = src->lParam;
    UINT state = (src->mask & LVIF_STATE) ? src->state & src->stateMask : 0;
    it->State = state & ~(LVIS_FOCUSED | LVIS_SELECTED);
    if (lv->ExStyle & LVS_EX_CHECKBOXES) {
        BOOL checked = (lv->ExStyle & LVS_EX_AUTOCHECKSELECT) && (state & LVIS_SELECTED);
        it->State = (it->State & ~LVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
    }
    if (sorted) {
        index = 0;
        while (index < lv->Count && compare_text(lv, it->Text, index) >= 0)
            index++;
    }
    if (lv->Count == lv->Capacity) {
        int capacity = lv->Capacity ? lv->Capacity * 2 : 16;
        LV_ITEM **items = CcReAlloc(lv->Items, (SIZE_T)capacity * sizeof(LV_ITEM *));
        if (!items) {
            free_item(it);
            return -1;
        }
        lv->Items = items;
        lv->Capacity = capacity;
    }
    memmove(&lv->Items[index + 1], &lv->Items[index], (SIZE_T)(lv->Count - index) * sizeof(LV_ITEM *));
    lv->Items[index] = it;
    lv->Count++;
    if (lv->Focus >= index)
        lv->Focus++;
    if (lv->Mark >= index)
        lv->Mark++;
    if ((lv->Style & LVS_SINGLESEL) && (state & LVIS_SELECTED))
        deselect_all_but(lv, index);
    if (state & (LVIS_FOCUSED | LVIS_SELECTED))
        set_state(lv, index, state & 3, state & 3);
    notify_listview(lv, LVN_INSERTITEM, index, 0, 0, 0, 0, 0);
    update_scrollbars(lv);
    invalidate_all(lv);
    NotifyWinEvent(EVENT_OBJECT_CREATE, lv->Hwnd, OBJID_CLIENT, index + 1);
    return index;
}

static BOOL get_item(LISTVIEW *lv, LVITEMW *dst, BOOL ansi)
{
    if (!dst || (UINT)dst->iItem >= (UINT)lv->Count)
        return FALSE;
    int i = dst->iItem;
    int sub = dst->iSubItem < 0 ? 0 : dst->iSubItem;
    UINT mask = dst->mask;
    LV_ITEM *it = is_ownerdata(lv) ? nullptr : lv->Items[i];
    LPCWSTR stored = nullptr;
    int image = I_IMAGECALLBACK, indent = 0;
    if (it) {
        if (sub == 0) {
            stored = it->Text;
            image = it->Image;
            indent = it->Indent;
        } else {
            LV_SUBITEM *s = subitem(it, sub, FALSE);
            stored = s ? s->Text : LPSTR_TEXTCALLBACKW;
            image = s ? s->Image : I_IMAGECALLBACK;
        }
    }
    UINT state = get_state(lv, i);
    UINT callback = 0;
    if (mask & LVIF_TEXT) {
        if (!it || CcIsCallback(stored))
            callback |= LVIF_TEXT;
    }
    if (mask & LVIF_IMAGE) {
        if (sub == 0 || (lv->ExStyle & LVS_EX_SUBITEMIMAGES))
            if (!it || image == I_IMAGECALLBACK)
                callback |= LVIF_IMAGE;
    }
    if ((mask & LVIF_INDENT) && (!it || indent == I_INDENTCALLBACK))
        callback |= LVIF_INDENT;
    if ((mask & LVIF_STATE) && (lv->CallbackMask & dst->stateMask))
        callback |= LVIF_STATE;
    if ((mask & LVIF_NORECOMPUTE) && (callback & LVIF_TEXT)) {
        callback &= ~LVIF_TEXT;
        if (ansi)
            ((LVITEMA *)dst)->pszText = LPSTR_TEXTCALLBACKA;
        else
            dst->pszText = LPSTR_TEXTCALLBACKW;
        mask &= ~LVIF_TEXT;
    }
    if (callback) {
        if (lv->Notify.Unicode && !ansi) {
            /* the parent writes into the caller's buffer and may hand back its own pointer. */
            NMLVDISPINFOW nm = {};
            nm.item.mask = callback;
            nm.item.iItem = i;
            nm.item.iSubItem = sub;
            nm.item.state = state;
            nm.item.stateMask = lv->CallbackMask & dst->stateMask;
            nm.item.pszText = (callback & LVIF_TEXT) ? dst->pszText : nullptr;
            nm.item.cchTextMax = (callback & LVIF_TEXT) ? dst->cchTextMax : 0;
            if ((callback & LVIF_TEXT) && dst->pszText && dst->cchTextMax > 0)
                dst->pszText[0] = 0;
            nm.item.iImage = -1;
            nm.item.lParam = get_param(lv, i);
            CcNotify(&lv->Notify, LVN_GETDISPINFOW, &nm.hdr);
            if (callback & LVIF_TEXT) {
                if (nm.item.pszText && nm.item.pszText != dst->pszText && !CcIsCallback(nm.item.pszText)) {
                    CcCopyText(dst->pszText, dst->cchTextMax, nm.item.pszText);
                    dst->pszText = nm.item.pszText;
                }
            }
            if (callback & LVIF_IMAGE)
                image = nm.item.iImage;
            if (callback & LVIF_INDENT)
                indent = nm.item.iIndent;
            if (callback & LVIF_STATE)
                state = (state & ~nm.item.stateMask) | (nm.item.state & nm.item.stateMask);
            if (it && (nm.item.mask & LVIF_DI_SETITEM)) {
                if ((callback & LVIF_TEXT) && sub == 0)
                    CcSetText(&it->Text, dst->pszText);
                if ((callback & LVIF_IMAGE) && sub == 0)
                    it->Image = image;
            }
            callback &= ~LVIF_TEXT;
            mask &= ~LVIF_TEXT;
        } else {
            WCHAR buffer[1024];
            get_dispinfo(lv, i, sub, callback, buffer, 1024, &image, &indent, &state);
            if (callback & LVIF_TEXT) {
                if (ansi)
                    CcCopyTextA((LPSTR)dst->pszText, dst->cchTextMax, buffer);
                else
                    CcCopyText(dst->pszText, dst->cchTextMax, buffer);
                mask &= ~LVIF_TEXT;
            }
        }
    }
    if (mask & LVIF_TEXT) {
        if (ansi)
            CcCopyTextA((LPSTR)dst->pszText, dst->cchTextMax, stored);
        else
            CcCopyText(dst->pszText, dst->cchTextMax, stored);
    }
    if (mask & LVIF_IMAGE)
        dst->iImage = image;
    if ((mask & LVIF_INDENT) && sub == 0)
        dst->iIndent = indent;
    if (mask & LVIF_PARAM)
        dst->lParam = get_param(lv, i);
    if (mask & LVIF_STATE) {
        UINT s = state;
        if (sub > 0 && !(lv->ExStyle & LVS_EX_FULLROWSELECT))
            s &= ~(LVIS_FOCUSED | LVIS_SELECTED | LVIS_DROPHILITED);
        dst->state = (dst->state & ~dst->stateMask) | (s & dst->stateMask);
    }
    return TRUE;
}

static BOOL delete_item(LISTVIEW *lv, int i)
{
    if (i < 0 || i >= lv->Count)
        return FALSE;
    set_state(lv, i, 0, LVIS_SELECTED);
    if (lv->Focus == i) {
        int next = i + 1 < lv->Count ? i + 1 : i - 1;
        if (next >= 0)
            set_state(lv, next, LVIS_FOCUSED, LVIS_FOCUSED);
        else
            set_state(lv, i, 0, LVIS_FOCUSED);
    }
    notify_listview(lv, LVN_DELETEITEM, i, 0, 0, 0, 0, get_param(lv, i));
    if (is_ownerdata(lv)) {
        if (i < lv->DataStatesSize) {
            memmove(&lv->DataStates[i], &lv->DataStates[i + 1], (SIZE_T)(lv->DataStatesSize - i - 1));
            lv->DataStatesSize--;
        }
    } else {
        free_item(lv->Items[i]);
        memmove(&lv->Items[i], &lv->Items[i + 1], (SIZE_T)(lv->Count - i - 1) * sizeof(LV_ITEM *));
    }
    lv->Count--;
    if (lv->Focus >= i) {
        int focus = lv->Focus < lv->Count ? lv->Focus : lv->Count;
        lv->Focus = focus - 1;
        if (lv->Focus >= 0 && !(get_state(lv, lv->Focus) & LVIS_FOCUSED) && !is_ownerdata(lv))
            lv->Focus = -1;
    }
    if (lv->Mark > i)
        lv->Mark--;
    else if (lv->Mark == i && lv->Mark >= lv->Count)
        lv->Mark = lv->Count - 1;
    if (lv->Hot >= lv->Count)
        lv->Hot = -1;
    scroll_to(lv, lv->ScrollX, lv->ScrollY);
    update_scrollbars(lv);
    invalidate_all(lv);
    NotifyWinEvent(EVENT_OBJECT_DESTROY, lv->Hwnd, OBJID_CLIENT, i + 1);
    return TRUE;
}

static BOOL delete_all(LISTVIEW *lv)
{
    lv->Focus = lv->Mark = lv->Hot = -1;
    BOOL quiet = notify_listview(lv, LVN_DELETEALLITEMS, -1, 0, 0, 0, 0, 0) != 0;
    for (int i = lv->Count - 1; i >= 0; i--) {
        if (!quiet)
            notify_listview(lv, LVN_DELETEITEM, i, 0, 0, 0, 0, get_param(lv, i));
        if (!is_ownerdata(lv))
            free_item(lv->Items[i]);
    }
    lv->Count = 0;
    CcFree(lv->DataStates);
    lv->DataStates = nullptr;
    lv->DataStatesSize = 0;
    lv->ScrollY = 0;
    lv->ScrollX = lv->View == LV_VIEW_DETAILS ? lv->ScrollX : 0;
    update_scrollbars(lv);
    invalidate_all(lv);
    return TRUE;
}

static BOOL set_item_state(LISTVIEW *lv, int i, const LVITEMW *src)
{
    if (!src)
        return FALSE;
    if (i != -1)
        return i >= 0 && i < lv->Count && change_item(lv, i, &(LVITEMW){.mask = LVIF_STATE, .state = src->state,
                                                                             .stateMask = src->stateMask}, FALSE);
    UINT bits = src->state & src->stateMask;
    if (is_ownerdata(lv)) {
        if (bits & LVIS_FOCUSED)
            return FALSE;
        for (int k = 0; k < lv->Count; k++)
            put_state(lv, k, (get_state(lv, k) & ~src->stateMask) | bits);
        notify_listview(lv, LVN_ITEMCHANGED, -1, 0, bits, 0, LVIF_STATE, 0);
        invalidate_all(lv);
        return TRUE;
    }
    if ((bits & LVIS_FOCUSED) || ((lv->Style & LVS_SINGLESEL) && src->stateMask == LVIS_SELECTED && bits))
        return FALSE;
    UINT only = 0;
    if (!bits && src->stateMask == LVIS_SELECTED)
        only = LVIS_SELECTED;
    else if (!bits && src->stateMask == LVIS_CUT)
        only = LVIS_CUT;
    for (int k = 0; k < lv->Count; k++)
        if (!only || (get_state(lv, k) & only))
            set_state(lv, k, src->state, src->stateMask);
    return TRUE;
}

static int get_next_item(LISTVIEW *lv, int start, UINT flags)
{
    if (start < -1 || start >= lv->Count)
        return -1;
    if ((flags & (LVNI_PREVIOUS | LVNI_VISIBLEORDER)) && (flags & (LVNI_STATEMASK | LVNI_DIRECTIONMASK)))
        return -1;
    if (flags & LVNI_PREVIOUS)
        return start == -1 ? lv->Count - 1 : start - 1;
    if (flags & LVNI_DIRECTIONMASK) {
        if (start == -1)
            return -1;
        int target = start;
        ITEM_RECTS r;
        item_rects(lv, start, &r);
        if (lv->View == LV_VIEW_DETAILS) {
            if (flags & LVNI_ABOVE)
                target = start - 1;
            else if (flags & LVNI_BELOW)
                target = start + 1;
            else
                return -1;
        } else if (lv->View == LV_VIEW_LIST) {
            int rows = rows_per_column(lv);
            if (flags & LVNI_ABOVE)
                target = start % rows ? start - 1 : -1;
            else if (flags & LVNI_BELOW)
                target = (start + 1) % rows ? start + 1 : -1;
            else if (flags & LVNI_TOLEFT)
                target = start - rows;
            else
                target = start + rows;
        } else {
            RECT client;
            client_rect(lv, &client);
            int cell_w = lv->View == LV_VIEW_ICON ? lv->SpacingX : lv->ColumnWidth;
            int per_row = client.right / (cell_w > 0 ? cell_w : 1);
            if (per_row < 1)
                per_row = 1;
            if (flags & LVNI_ABOVE)
                target = start - per_row;
            else if (flags & LVNI_BELOW)
                target = start + per_row;
            else if (flags & LVNI_TOLEFT)
                target = start % per_row ? start - 1 : -1;
            else
                target = (start + 1) % per_row ? start + 1 : -1;
        }
        return target >= 0 && target < lv->Count ? target : -1;
    }
    UINT want = flags & LVNI_STATEMASK;
    if (!want)
        return start + 1 < lv->Count ? start + 1 : -1;
    if ((want & LVNI_FOCUSED)) {
        if (lv->Focus > start && (get_state(lv, lv->Focus) & want) == want)
            return lv->Focus;
        return -1;
    }
    for (int k = start + 1; k < lv->Count; k++)
        if ((get_state(lv, k) & want) == want)
            return k;
    return -1;
}

/* ------------------------------------------------------------------ columns */

static void ensure_header(LISTVIEW *lv)
{
    if (lv->Header)
        return;
    DWORD style = WS_CHILD | HDS_DRAGDROP | HDS_FULLDRAG;
    if (!(lv->Style & LVS_NOSORTHEADER))
        style |= HDS_BUTTONS;
    if (lv->Style & LVS_NOCOLUMNHEADER)
        style |= HDS_HIDDEN;
    lv->Header = CreateWindowExW(0, WC_HEADERW, nullptr, style, 0, 0, 0, 0, lv->Hwnd, nullptr,
                                 (HINSTANCE)GetWindowLongPtrW(lv->Hwnd, GWLP_HINSTANCE), nullptr);
    if (!lv->Header)
        return;
    SendMessageW(lv->Header, CCM_SETNOTIFYWINDOW, (WPARAM)lv->Hwnd, 0);
    SendMessageW(lv->Header, WM_SETFONT, (WPARAM)lv->Font, 0);
    SendMessageW(lv->Header, HDM_SETIMAGELIST, HDSIL_NORMAL, (LPARAM)lv->Small);
    layout_header(lv);
}

static void column_to_header(const LVCOLUMNW *col, HDITEMW *hd, BOOL first, BOOL ansi)
{
    memset(hd, 0, sizeof(*hd));
    hd->mask = HDI_WIDTH | HDI_FORMAT | HDI_LPARAM;
    hd->cxy = (col->mask & LVCF_WIDTH) ? col->cx : 10;
    int fmt = (col->mask & LVCF_FMT) ? col->fmt : LVCFMT_LEFT;
    if (first)
        fmt &= ~LVCFMT_JUSTIFYMASK;
    hd->fmt = (fmt & (LVCFMT_JUSTIFYMASK | LVCFMT_IMAGE | LVCFMT_BITMAP_ON_RIGHT | LVCFMT_FIXED_WIDTH |
                      LVCFMT_SPLITBUTTON)) |
              HDF_STRING;
    hd->lParam = (col->mask & LVCF_SUBITEM) ? col->iSubItem : 0;
    if (col->mask & LVCF_TEXT) {
        hd->mask |= HDI_TEXT;
        hd->pszText = col->pszText;
    }
    if (col->mask & LVCF_IMAGE) {
        hd->mask |= HDI_IMAGE;
        hd->iImage = col->iImage;
    }
    if (col->mask & LVCF_ORDER) {
        hd->mask |= HDI_ORDER;
        hd->iOrder = col->iOrder;
    }
    (void)ansi;
}

static int insert_column(LISTVIEW *lv, int index, const LVCOLUMNW *col, BOOL ansi)
{
    if (!col || index < 0)
        return -1;
    ensure_header(lv);
    if (!lv->Header)
        return -1;
    int count = column_count(lv);
    if (index > count)
        index = count;
    HDITEMW hd;
    column_to_header(col, &hd, index == 0, ansi);
    int r = (int)SendMessageW(lv->Header, ansi ? HDM_INSERTITEMA : HDM_INSERTITEMW, (WPARAM)index, (LPARAM)&hd);
    if (r < 0)
        return -1;
    /* the subitems at and after the new column move one to the right. */
    if (!is_ownerdata(lv) && r > 0)
        for (int i = 0; i < lv->Count; i++) {
            LV_ITEM *it = lv->Items[i];
            if (it->SubCount >= r && subitem(it, it->SubCount + 1, TRUE)) {
                memmove(&it->Sub[r], &it->Sub[r - 1], (SIZE_T)(it->SubCount - r) * sizeof(LV_SUBITEM));
                it->Sub[r - 1].Text = LPSTR_TEXTCALLBACKW;
                it->Sub[r - 1].Image = I_IMAGECALLBACK;
            }
        }
    update_scrollbars(lv);
    invalidate_all(lv);
    return r;
}

static BOOL set_column(LISTVIEW *lv, int index, const LVCOLUMNW *col, BOOL ansi)
{
    if (!col)
        return FALSE;
    if (!col->mask)
        return TRUE;
    if (!lv->Header || index < 0 || index >= column_count(lv))
        return FALSE;
    HDITEMW hd = {};
    if (col->mask & LVCF_WIDTH) {
        hd.mask |= HDI_WIDTH;
        hd.cxy = col->cx;
    }
    if (col->mask & LVCF_FMT) {
        hd.mask |= HDI_FORMAT;
        int fmt = col->fmt;
        if (index == 0)
            fmt &= ~LVCFMT_JUSTIFYMASK;
        hd.fmt = (fmt & (LVCFMT_JUSTIFYMASK | LVCFMT_IMAGE | LVCFMT_BITMAP_ON_RIGHT | LVCFMT_FIXED_WIDTH |
                         LVCFMT_SPLITBUTTON)) |
                 HDF_STRING;
    }
    if (col->mask & LVCF_TEXT) {
        hd.mask |= HDI_TEXT;
        hd.pszText = col->pszText;
    }
    if (col->mask & LVCF_IMAGE) {
        hd.mask |= HDI_IMAGE;
        hd.iImage = col->iImage;
    }
    if (col->mask & LVCF_ORDER) {
        hd.mask |= HDI_ORDER;
        hd.iOrder = col->iOrder;
    }
    if (col->mask & LVCF_SUBITEM) {
        /* the column index, whatever iSubItem says. */
        hd.mask |= HDI_LPARAM;
        hd.lParam = index;
    }
    BOOL r = (BOOL)SendMessageW(lv->Header, ansi ? HDM_SETITEMA : HDM_SETITEMW, (WPARAM)index, (LPARAM)&hd);
    invalidate_all(lv);
    return r;
}

static BOOL get_column(LISTVIEW *lv, int index, LVCOLUMNW *col, BOOL ansi)
{
    if (!col)
        return FALSE;
    if (!col->mask)
        return TRUE;
    if (!lv->Header || index < 0 || index >= column_count(lv))
        return FALSE;
    HDITEMW hd = {};
    hd.mask = HDI_WIDTH | HDI_FORMAT | HDI_LPARAM | HDI_IMAGE | HDI_ORDER;
    if (col->mask & LVCF_TEXT) {
        hd.mask |= HDI_TEXT;
        hd.pszText = col->pszText;
        hd.cchTextMax = col->cchTextMax;
    }
    if (!SendMessageW(lv->Header, ansi ? HDM_GETITEMA : HDM_GETITEMW, (WPARAM)index, (LPARAM)&hd))
        return FALSE;
    if (col->mask & LVCF_WIDTH)
        col->cx = hd.cxy;
    if (col->mask & LVCF_FMT)
        col->fmt = hd.fmt & (LVCFMT_JUSTIFYMASK | LVCFMT_IMAGE | LVCFMT_BITMAP_ON_RIGHT | LVCFMT_FIXED_WIDTH |
                             LVCFMT_SPLITBUTTON | LVCFMT_COL_HAS_IMAGES);
    if (col->mask & LVCF_SUBITEM)
        col->iSubItem = (int)hd.lParam;
    if (col->mask & LVCF_IMAGE)
        col->iImage = hd.iImage;
    if (col->mask & LVCF_ORDER)
        col->iOrder = hd.iOrder;
    return TRUE;
}

static BOOL delete_column(LISTVIEW *lv, int index)
{
    int count = column_count(lv);
    if (index < 0 || index >= count)
        return FALSE;
    if (!is_ownerdata(lv)) {
        if (index == 0 && count >= 2) {
            /* column 1 becomes the item itself, through SETITEM and its notifications. */
            for (int i = 0; i < lv->Count; i++) {
                LV_ITEM *it = lv->Items[i];
                LV_SUBITEM *s = subitem(it, 1, FALSE);
                LVITEMW item = {};
                item.mask = LVIF_TEXT | LVIF_IMAGE;
                item.iItem = i;
                item.pszText = s ? s->Text : LPSTR_TEXTCALLBACKW;
                item.iImage = s ? s->Image : I_IMAGECALLBACK;
                change_item(lv, i, &item, FALSE);
            }
        }
        int slot = index == 0 ? 1 : index;
        for (int i = 0; i < lv->Count; i++) {
            LV_ITEM *it = lv->Items[i];
            if (slot <= it->SubCount) {
                if (it->Sub[slot - 1].Text != LPSTR_TEXTCALLBACKW)
                    CcFree(it->Sub[slot - 1].Text);
                memmove(&it->Sub[slot - 1], &it->Sub[slot], (SIZE_T)(it->SubCount - slot) * sizeof(LV_SUBITEM));
                it->SubCount--;
            }
        }
    }
    SendMessageW(lv->Header, HDM_DELETEITEM, (WPARAM)index, 0);
    scroll_to(lv, lv->ScrollX, lv->ScrollY);
    update_scrollbars(lv);
    invalidate_all(lv);
    return TRUE;
}

/* LVSCW_AUTOSIZE: the width the column's items need. */
static int autosize_width(LISTVIEW *lv, int column)
{
    int edge = GetSystemMetrics(SM_CXEDGE);
    int small_cx = image_size(lv->Small, nullptr, nullptr);
    int state_cx = image_size(lv->StateList, nullptr, nullptr);
    int widest = 0;
    int first = 0, last = lv->Count;
    if (is_ownerdata(lv)) {
        first = lv->ScrollY;
        last = first + visible_rows(lv) + 1;
        if (last > lv->Count)
            last = lv->Count;
        NMLVCACHEHINT hint = {};
        hint.iFrom = first;
        hint.iTo = last - 1;
        if (last > first)
            CcNotify(&lv->Notify, LVN_ODCACHEHINT, &hint.hdr);
    }
    WCHAR text[1024];
    HDC dc = GetDC(lv->Hwnd);
    HGDIOBJ old = SelectObject(dc, lv->Font);
    for (int i = first; i < last; i++) {
        display_info(lv, i, column, text, 1024, nullptr);
        SIZE size = {};
        if (text[0])
            GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
        int w = size.cx;
        if (column == 0) {
            int indent = is_ownerdata(lv) ? 0 : lv->Items[i]->Indent;
            w += 2 * edge + (lv->Small ? small_cx : 0) + indent * small_cx + state_cx + (state_cx ? 3 : 0) + 2;
        } else {
            w += 6 * edge;
            if ((lv->ExStyle & LVS_EX_SUBITEMIMAGES) && lv->Small)
                w += small_cx + edge;
        }
        if (w > widest)
            widest = w;
    }
    SelectObject(dc, old);
    ReleaseDC(lv->Hwnd, dc);
    return widest;
}

static BOOL set_column_width(LISTVIEW *lv, int column, int cx)
{
    if (lv->View == LV_VIEW_LIST) {
        if (column != 0 || cx <= 0)
            return FALSE;
        lv->ColumnWidth = cx;
        scroll_to(lv, lv->ScrollX, 0);
        update_scrollbars(lv);
        invalidate_all(lv);
        return TRUE;
    }
    if (!lv->Header || column < 0 || column >= column_count(lv))
        return FALSE;
    HDITEMW hd = {};
    hd.mask = HDI_FORMAT;
    SendMessageW(lv->Header, HDM_GETITEMW, (WPARAM)column, (LPARAM)&hd);
    if (hd.fmt & HDF_FIXEDWIDTH)
        return FALSE;
    int width = cx;
    if (cx == LVSCW_AUTOSIZE || cx == LVSCW_AUTOSIZE_USEHEADER) {
        width = autosize_width(lv, column);
        if (cx == LVSCW_AUTOSIZE_USEHEADER) {
            int header = HeaderItemWidth(lv->Header, column);
            if (header > width)
                width = header;
            int count = column_count(lv);
            int last = (int)SendMessageW(lv->Header, HDM_ORDERTOINDEX, (WPARAM)(count - 1), 0);
            if (column == last && !(lv->ExStyle & (LVS_EX_AUTOSIZECOLUMNS | LVS_EX_COLUMNSNAPPOINTS))) {
                /* the last column also fills the client area to its right edge. */
                RECT client, col;
                GetClientRect(lv->Hwnd, &client);
                SendMessageW(lv->Header, HDM_GETITEMRECT, (WPARAM)column, (LPARAM)&col);
                int fill = client.right + lv->ScrollX - col.left;
                if (fill > width)
                    width = fill;
            }
        }
        int extras = (hd.fmt & HDF_IMAGE) ? image_size(lv->Small, nullptr, nullptr) : 0;
        if (width < 25 + extras)
            width = 25 + extras;
    }
    hd.mask = HDI_WIDTH;
    hd.cxy = width;
    SendMessageW(lv->Header, HDM_SETITEMW, (WPARAM)column, (LPARAM)&hd);
    return TRUE;
}

/* ------------------------------------------------------------------ hit testing */

static int hit_test(LISTVIEW *lv, LVHITTESTINFO *info, BOOL subitems, BOOL outside_test)
{
    RECT client;
    client_rect(lv, &client);
    POINT pt = info->pt;
    info->flags = 0;
    info->iItem = -1;
    info->iSubItem = 0;
    if (outside_test) {
        if (pt.x < client.left)
            info->flags |= LVHT_TOLEFT;
        else if (pt.x >= client.right)
            info->flags |= LVHT_TORIGHT;
        if (pt.y < client.top)
            info->flags |= LVHT_ABOVE;
        else if (pt.y >= client.bottom)
            info->flags |= LVHT_BELOW;
        if (info->flags)
            return -1;
    }
    int candidate = -1;
    if (lv->View == LV_VIEW_DETAILS) {
        if (pt.y >= header_bottom(lv)) {
            int row = (pt.y - header_bottom(lv)) / lv->RowHeight + lv->ScrollY;
            if (row >= 0 && row < lv->Count)
                candidate = row;
        }
    } else if (lv->View == LV_VIEW_LIST) {
        int rows = rows_per_column(lv);
        int col = (pt.x + lv->ScrollX) / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1);
        int row = pt.y / lv->RowHeight;
        if (pt.x + lv->ScrollX >= 0 && row < rows && col * rows + row < lv->Count)
            candidate = col * rows + row;
    } else {
        for (int i = 0; i < lv->Count; i++) {
            ITEM_RECTS r;
            item_rects(lv, i, &r);
            if (PtInRect(&r.Bounds, pt) || PtInRect(&r.State, pt)) {
                candidate = i;
                break;
            }
        }
    }
    if (candidate >= 0) {
        ITEM_RECTS r;
        item_rects(lv, candidate, &r);
        UINT flags = 0;
        if (lv->StateList && PtInRect(&r.State, pt))
            flags = LVHT_ONITEMSTATEICON;
        else if (PtInRect(&r.Icon, pt))
            flags = LVHT_ONITEMICON;
        else if (lv->View == LV_VIEW_DETAILS) {
            if (pt.x < r.Icon.left || pt.x < r.Select.right)
                flags = LVHT_ONITEMLABEL;
            else if (lv->ExStyle & LVS_EX_FULLROWSELECT)
                flags = pt.x < r.Bounds.right ? LVHT_ONITEMLABEL : LVHT_ONITEM;
            else
                flags = LVHT_NOWHERE;
        } else if (PtInRect(&r.Label, pt) || PtInRect(&r.Select, pt))
            flags = LVHT_ONITEMLABEL;
        else
            flags = LVHT_NOWHERE;
        info->flags = flags;
        if (flags != LVHT_NOWHERE)
            info->iItem = candidate;
        if (subitems && lv->View == LV_VIEW_DETAILS) {
            info->iItem = candidate;
            int count = column_count(lv);
            for (int c = 0; c < count; c++) {
                RECT col;
                if (column_rect(lv, c, &col) && pt.x >= col.left && pt.x < col.right) {
                    info->iSubItem = c;
                    if (c > 0 && info->flags == LVHT_NOWHERE)
                        info->flags = LVHT_ONITEMLABEL;
                    break;
                }
            }
        }
    } else {
        info->flags = LVHT_NOWHERE;
    }
    return info->iItem;
}

/* ------------------------------------------------------------------ painting */

static COLORREF text_bk_color(LISTVIEW *lv)
{
    if (lv->TextBk == CLR_DEFAULT)
        return lv->Bk == CLR_NONE ? CLR_NONE : GetSysColor(COLOR_WINDOW);
    return lv->TextBk;
}

static LRESULT custom_draw(LISTVIEW *lv, NMLVCUSTOMDRAW *cd, DWORD stage)
{
    cd->nmcd.dwDrawStage = stage;
    LRESULT r = CcNotify(&lv->Notify, NM_CUSTOMDRAW, &cd->nmcd.hdr);
    return (r & 0xFE01) && stage == CDDS_PREPAINT ? CDRF_DODEFAULT : r;
}

static void draw_text(LISTVIEW *lv, HDC dc, LPCWSTR text, RECT *r, int justify, BOOL single)
{
    UINT flags = DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_VCENTER | DT_SINGLELINE;
    if (justify == LVCFMT_RIGHT)
        flags |= DT_RIGHT;
    else if (justify == LVCFMT_CENTER)
        flags |= DT_CENTER;
    (void)single;
    DrawTextW(dc, text, -1, r, flags);
}

static void draw_report_item(LISTVIEW *lv, HDC dc, int i, BOOL focused_control, LRESULT pre)
{
    ITEM_RECTS r;
    item_rects(lv, i, &r);
    UINT state = get_state(lv, i);
    BOOL selected = (state & LVIS_SELECTED) && (focused_control || (lv->Style & LVS_SHOWSELALWAYS));
    BOOL full_row = (lv->ExStyle & LVS_EX_FULLROWSELECT) != 0;
    BOOL show_focus = focused_control && (state & LVIS_FOCUSED) && !CcFocusCuesHidden(lv->Hwnd);
    NMLVCUSTOMDRAW cd = {};
    cd.nmcd.hdc = dc;
    cd.nmcd.rc = r.Bounds;
    cd.nmcd.dwItemSpec = (DWORD_PTR)i;
    cd.nmcd.uItemState = (selected ? CDIS_SELECTED : 0) | (show_focus ? CDIS_FOCUS : 0) |
                         ((state & LVIS_DROPHILITED) ? CDIS_DROPHILITED : 0) | (i == lv->Hot ? CDIS_HOT : 0) |
                         (CcFocusCuesHidden(lv->Hwnd) ? 0 : CDIS_SHOWKEYBOARDCUES);
    cd.nmcd.lItemlParam = get_param(lv, i);
    cd.clrText = lv->Text;
    cd.clrTextBk = lv->TextBk;
    LRESULT item = CDRF_DODEFAULT;
    if (pre & CDRF_NOTIFYITEMDRAW) {
        item = custom_draw(lv, &cd, CDDS_ITEMPREPAINT);
        if (item & CDRF_SKIPDEFAULT)
            return;
    }
    COLORREF text_color = cd.clrText == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : cd.clrText;
    COLORREF back = cd.clrTextBk == CLR_DEFAULT ? text_bk_color(lv) : cd.clrTextBk;
    COLORREF sel_text = GetSysColor(COLOR_HIGHLIGHTTEXT), sel_back = GetSysColor(COLOR_HIGHLIGHT);
    if ((state & LVIS_SELECTED) && !focused_control && (lv->Style & LVS_SHOWSELALWAYS) &&
        GetSysColor(COLOR_BTNFACE) != (lv->Bk == CLR_NONE ? GetSysColor(COLOR_WINDOW) : lv->Bk)) {
        sel_text = GetSysColor(COLOR_BTNTEXT);
        sel_back = GetSysColor(COLOR_BTNFACE);
    }
    int count = column_count(lv);
    if (!count)
        count = 1;
    int edge = GetSystemMetrics(SM_CXEDGE);
    WCHAR text[1024];
    BOOL themed = lv->ThemedItems && (selected || i == lv->Hot);
    if (themed) {
        RECT sr = full_row ? r.Bounds : r.Select;
        int s = selected ? (focused_control ? 3 : 5) : 2;
        if (selected && i == lv->Hot)
            s = 6;
        DrawThemeBackground(lv->Theme, dc, CC_LVP_LISTITEM, s, &sr, nullptr);
    } else if (selected && full_row) {
        CcFillSolid(dc, &r.Bounds, sel_back);
    }
    for (int k = 0; k < count; k++) {
        int c = lv->Header ? (int)SendMessageW(lv->Header, HDM_ORDERTOINDEX, (WPARAM)k, 0) : 0;
        RECT cell;
        if (!column_rect(lv, c, &cell)) {
            cell = r.Bounds;
        }
        cell.top = r.Bounds.top;
        cell.bottom = r.Bounds.bottom;
        int image = -1;
        display_info(lv, i, c, text, 1024, &image);
        HDITEMW hd = {};
        hd.mask = HDI_FORMAT;
        if (lv->Header)
            SendMessageW(lv->Header, HDM_GETITEMW, (WPARAM)c, (LPARAM)&hd);
        int justify = c == 0 ? LVCFMT_LEFT : hd.fmt & HDF_JUSTIFYMASK;
        BOOL cell_selected = selected && (full_row || c == 0) && !themed;
        if (pre & CDRF_NOTIFYITEMDRAW && item & CDRF_NOTIFYSUBITEMDRAW) {
            cd.iSubItem = c;
            cd.nmcd.rc = cell;
            cd.clrText = lv->Text;
            cd.clrTextBk = lv->TextBk;
            LRESULT sub = custom_draw(lv, &cd, CDDS_SUBITEM | CDDS_ITEMPREPAINT);
            if (sub & CDRF_SKIPDEFAULT)
                continue;
            text_color = cd.clrText == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : cd.clrText;
            back = cd.clrTextBk == CLR_DEFAULT ? text_bk_color(lv) : cd.clrTextBk;
        }
        RECT text_rect = cell;
        if (c == 0) {
            if (lv->StateList) {
                int si = (int)((state & LVIS_STATEIMAGEMASK) >> 12) - 1;
                if (si >= 0) {
                    int scx, scy;
                    image_size(lv->StateList, &scx, &scy);
                    ImageList_Draw(lv->StateList, si, dc, r.State.left, r.State.top + (lv->RowHeight - scy) / 2,
                                   ILD_NORMAL);
                }
            }
            if (lv->Small && image >= 0) {
                int scx, scy;
                image_size(lv->Small, &scx, &scy);
                UINT style = ILD_NORMAL | (state & LVIS_OVERLAYMASK);
                if (selected && !full_row && !themed)
                    style |= ILD_SELECTED;
                if (state & LVIS_CUT)
                    style |= ILD_BLEND50;
                ImageList_Draw(lv->Small, image, dc, r.Icon.left, r.Icon.top + (lv->RowHeight - scy) / 2, style);
            }
            text_rect.left = r.Label.left;
            text_rect.right = cell.right;
            if (cell_selected && !full_row) {
                RECT sr = r.Select;
                sr.left = r.Label.left;
                CcFillSolid(dc, &sr, sel_back);
            } else if (!cell_selected && back != CLR_NONE && !themed) {
                CcFillSolid(dc, &text_rect, back);
            }
            InflateRect(&text_rect, -edge, 0);
        } else {
            if (!cell_selected && back != CLR_NONE && !themed && !(selected && full_row))
                CcFillSolid(dc, &cell, back);
            if ((lv->ExStyle & LVS_EX_SUBITEMIMAGES) && lv->Small && image >= 0) {
                int scx, scy;
                image_size(lv->Small, &scx, &scy);
                ImageList_Draw(lv->Small, image, dc, cell.left + edge, cell.top + (lv->RowHeight - scy) / 2,
                               ILD_NORMAL);
                text_rect.left += scx + edge;
            }
            InflateRect(&text_rect, -3 * edge, 0);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, cell_selected ? sel_text : text_color);
        if (text[0] && text_rect.right > text_rect.left)
            draw_text(lv, dc, text, &text_rect, justify, TRUE);
        if (pre & CDRF_NOTIFYITEMDRAW && item & CDRF_NOTIFYSUBITEMDRAW && item & CDRF_NOTIFYPOSTPAINT)
            custom_draw(lv, &cd, CDDS_SUBITEM | CDDS_ITEMPOSTPAINT);
    }
    if (show_focus && !themed) {
        RECT fr = full_row ? r.Bounds : r.Select;
        if (!full_row)
            fr.left = r.Label.left;
        DrawFocusRect(dc, &fr);
    }
    if (pre & CDRF_NOTIFYITEMDRAW && item & CDRF_NOTIFYPOSTPAINT) {
        cd.nmcd.rc = r.Bounds;
        cd.iSubItem = 0;
        custom_draw(lv, &cd, CDDS_ITEMPOSTPAINT);
    }
}

static void draw_icon_item(LISTVIEW *lv, HDC dc, int i, BOOL focused_control, LRESULT pre)
{
    ITEM_RECTS r;
    item_rects(lv, i, &r);
    UINT state = get_state(lv, i);
    BOOL selected = (state & LVIS_SELECTED) && (focused_control || (lv->Style & LVS_SHOWSELALWAYS));
    BOOL show_focus = focused_control && (state & LVIS_FOCUSED) && !CcFocusCuesHidden(lv->Hwnd);
    NMLVCUSTOMDRAW cd = {};
    cd.nmcd.hdc = dc;
    cd.nmcd.rc = r.Bounds;
    cd.nmcd.dwItemSpec = (DWORD_PTR)i;
    cd.nmcd.uItemState = (selected ? CDIS_SELECTED : 0) | (show_focus ? CDIS_FOCUS : 0);
    cd.nmcd.lItemlParam = get_param(lv, i);
    cd.clrText = lv->Text;
    cd.clrTextBk = lv->TextBk;
    LRESULT item = CDRF_DODEFAULT;
    if (pre & CDRF_NOTIFYITEMDRAW) {
        item = custom_draw(lv, &cd, CDDS_ITEMPREPAINT);
        if (item & CDRF_SKIPDEFAULT)
            return;
    }
    WCHAR text[1024];
    int image = -1;
    display_info(lv, i, 0, text, 1024, &image);
    HIMAGELIST list = lv->View == LV_VIEW_ICON ? lv->Normal : lv->Small;
    if (lv->StateList) {
        int si = (int)((state & LVIS_STATEIMAGEMASK) >> 12) - 1;
        if (si >= 0)
            ImageList_Draw(lv->StateList, si, dc, r.State.left, r.State.top, ILD_NORMAL);
    }
    if (list && image >= 0) {
        int cx, cy;
        image_size(list, &cx, &cy);
        UINT style = ILD_NORMAL | (state & LVIS_OVERLAYMASK) | (selected ? ILD_SELECTED : 0);
        ImageList_Draw(list, image, dc, r.Icon.left, r.Icon.top + (r.Icon.bottom - r.Icon.top - cy) / 2, style);
    }
    COLORREF text_color = cd.clrText == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : cd.clrText;
    RECT label = lv->View == LV_VIEW_ICON ? r.Label : r.Select;
    if (lv->View != LV_VIEW_ICON)
        label.left = r.Label.left;
    if (selected) {
        CcFillSolid(dc, &label, GetSysColor(COLOR_HIGHLIGHT));
        text_color = GetSysColor(COLOR_HIGHLIGHTTEXT);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_color);
    RECT tr = label;
    InflateRect(&tr, -GetSystemMetrics(SM_CXEDGE), 0);
    if (lv->View == LV_VIEW_ICON)
        DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS | DT_SINGLELINE | DT_TOP);
    else
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
    if (show_focus)
        DrawFocusRect(dc, &label);
    if (pre & CDRF_NOTIFYITEMDRAW && item & CDRF_NOTIFYPOSTPAINT)
        custom_draw(lv, &cd, CDDS_ITEMPOSTPAINT);
}

static void owner_draw_item(LISTVIEW *lv, HDC dc, int i, BOOL focused_control)
{
    ITEM_RECTS r;
    item_rects(lv, i, &r);
    UINT state = get_state(lv, i);
    DRAWITEMSTRUCT dis = {};
    dis.CtlType = ODT_LISTVIEW;
    dis.CtlID = (UINT)GetDlgCtrlID(lv->Hwnd);
    dis.itemID = (UINT)i;
    dis.itemAction = ODA_DRAWENTIRE;
    dis.itemState = ((state & LVIS_SELECTED) ? ODS_SELECTED : 0) |
                    (focused_control && (state & LVIS_FOCUSED) ? ODS_FOCUS : 0);
    dis.hwndItem = lv->Hwnd;
    dis.hDC = dc;
    dis.rcItem = r.Bounds;
    dis.itemData = (ULONG_PTR)get_param(lv, i);
    SendMessageW(lv->Notify.Parent, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
}

static void paint(LISTVIEW *lv, HDC dc, const RECT *update)
{
    RECT client;
    client_rect(lv, &client);
    HGDIOBJ old_font = SelectObject(dc, lv->Font);
    if (lv->Bk != CLR_NONE)
        CcFillSolid(dc, update, lv->Bk);
    BOOL focused = GetFocus() == lv->Hwnd;
    NMLVCUSTOMDRAW cd = {};
    cd.nmcd.hdc = dc;
    cd.nmcd.rc = *update;
    LRESULT pre = custom_draw(lv, &cd, CDDS_PREPAINT);
    if (!(pre & CDRF_SKIPDEFAULT) && lv->Count) {
        int first = 0, last = lv->Count - 1;
        if (lv->View == LV_VIEW_DETAILS) {
            first = lv->ScrollY + (update->top - header_bottom(lv)) / lv->RowHeight;
            last = lv->ScrollY + (update->bottom - header_bottom(lv)) / lv->RowHeight;
            if (first < lv->ScrollY)
                first = lv->ScrollY;
            if (last >= lv->Count)
                last = lv->Count - 1;
        }
        if (is_ownerdata(lv) && last >= first) {
            NMLVCACHEHINT hint = {};
            hint.iFrom = first;
            hint.iTo = last;
            CcNotify(&lv->Notify, LVN_ODCACHEHINT, &hint.hdr);
        }
        /* the focused item paints last, so its focus rectangle stays on top. */
        for (int pass = 0; pass < 2; pass++)
            for (int i = first; i <= last; i++) {
                if ((i == lv->Focus) != (pass == 1))
                    continue;
                if (lv->View != LV_VIEW_DETAILS) {
                    ITEM_RECTS r;
                    item_rects(lv, i, &r);
                    RECT dummy;
                    if (!IntersectRect(&dummy, &r.Bounds, update) && !IntersectRect(&dummy, &r.State, update))
                        continue;
                    draw_icon_item(lv, dc, i, focused, pre);
                } else if (lv->Style & LVS_OWNERDRAWFIXED) {
                    owner_draw_item(lv, dc, i, focused);
                } else {
                    draw_report_item(lv, dc, i, focused, pre);
                }
            }
    }
    if (lv->View == LV_VIEW_DETAILS && (lv->ExStyle & LVS_EX_GRIDLINES)) {
        HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNFACE));
        HGDIOBJ old = SelectObject(dc, pen);
        int count = column_count(lv);
        for (int c = 0; c < count; c++) {
            RECT col;
            if (column_rect(lv, c, &col)) {
                MoveToEx(dc, col.right - 1, header_bottom(lv), nullptr);
                LineTo(dc, col.right - 1, client.bottom);
            }
        }
        int width = columns_width(lv) - lv->ScrollX;
        for (int y = header_bottom(lv) - 1 + lv->RowHeight; y < client.bottom; y += lv->RowHeight) {
            MoveToEx(dc, -lv->ScrollX, y, nullptr);
            LineTo(dc, width, y);
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    if (pre & CDRF_NOTIFYPOSTPAINT) {
        cd.nmcd.rc = *update;
        custom_draw(lv, &cd, CDDS_POSTPAINT);
    }
    SelectObject(dc, old_font);
}

/* ------------------------------------------------------------------ image lists */

static void ensure_checkbox_list(LISTVIEW *lv)
{
    if (lv->StateList && lv->OwnStateList)
        return;
    int cx = GetSystemMetrics(SM_CXMENUCHECK), cy = GetSystemMetrics(SM_CYMENUCHECK);
    HTHEME theme = OpenThemeData(lv->Hwnd, L"Button");
    HDC screen = GetDC(lv->Hwnd);
    if (theme) {
        SIZE size;
        if (SUCCEEDED(GetThemePartSize(theme, screen, CC_BP_CHECKBOX, 1, nullptr, TS_DRAW, &size))) {
            cx = size.cx;
            cy = size.cy;
        }
    }
    HIMAGELIST list = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 2, 2);
    if (list) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = cx * 2;
        bi.bmiHeader.biHeight = -cy;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void *bits;
        HBITMAP bm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC mem = CreateCompatibleDC(screen);
        if (bm && mem) {
            HGDIOBJ old = SelectObject(mem, bm);
            for (int k = 0; k < 2; k++) {
                RECT r = {k * cx, 0, (k + 1) * cx, cy};
                if (theme)
                    DrawThemeBackground(theme, mem, CC_BP_CHECKBOX, k ? 5 : 1, &r, nullptr);
                else {
                    CcFillSolid(mem, &r, GetSysColor(COLOR_WINDOW));
                    DrawFrameControl(mem, &r, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_FLAT | (k ? DFCS_CHECKED : 0));
                    DWORD *px = bits;
                    for (int y = 0; y < cy; y++)
                        for (int x = k * cx; x < (k + 1) * cx; x++)
                            px[y * cx * 2 + x] |= 0xFF000000;
                }
            }
            SelectObject(mem, old);
            ImageList_Add(list, bm, nullptr);
        }
        if (mem)
            DeleteDC(mem);
        if (bm)
            DeleteObject(bm);
    }
    ReleaseDC(lv->Hwnd, screen);
    if (theme)
        CloseThemeData(theme);
    if (lv->StateList && lv->OwnStateList)
        ImageList_Destroy(lv->StateList);
    lv->StateList = list;
    lv->OwnStateList = TRUE;
}

static HIMAGELIST set_image_list(LISTVIEW *lv, int which, HIMAGELIST list)
{
    HIMAGELIST old;
    switch (which) {
    case LVSIL_NORMAL:
        old = lv->Normal;
        lv->Normal = list;
        break;
    case LVSIL_SMALL:
        old = lv->Small;
        lv->Small = list;
        if (lv->Header)
            SendMessageW(lv->Header, HDM_SETIMAGELIST, HDSIL_NORMAL, (LPARAM)list);
        break;
    case LVSIL_STATE:
        old = lv->StateList;
        if (lv->OwnStateList && old && old != list) {
            ImageList_Destroy(old);
            old = nullptr;
        }
        lv->StateList = list;
        lv->OwnStateList = FALSE;
        break;
    default:
        return nullptr;
    }
    if (list && !(lv->Style & LVS_SHAREIMAGELISTS))
        ImageList_SetBkColor(list, lv->Bk);
    compute_metrics(lv);
    update_scrollbars(lv);
    invalidate_all(lv);
    return old;
}

/* ------------------------------------------------------------------ input */

static UINT key_flags(void)
{
    return (GetKeyState(VK_MENU) < 0 ? LVKF_ALT : 0) | (GetKeyState(VK_CONTROL) < 0 ? LVKF_CONTROL : 0) |
           (GetKeyState(VK_SHIFT) < 0 ? LVKF_SHIFT : 0);
}

static LRESULT notify_activate(LISTVIEW *lv, UINT code, int item, int sub, POINT pt)
{
    NMITEMACTIVATE nm = {};
    nm.iItem = item;
    nm.iSubItem = sub;
    nm.ptAction = pt;
    nm.uKeyFlags = key_flags();
    if (item >= 0)
        nm.lParam = get_param(lv, item);
    return CcNotify(&lv->Notify, code, &nm.hdr);
}

static void select_range(LISTVIEW *lv, int from, int to, BOOL keep_others)
{
    int low = from < to ? from : to, high = from < to ? to : from;
    if (!keep_others)
        for (int k = 0; k < lv->Count; k++)
            if ((k < low || k > high) && (get_state(lv, k) & LVIS_SELECTED))
                set_state(lv, k, 0, LVIS_SELECTED);
    for (int k = low; k <= high; k++)
        set_state(lv, k, LVIS_SELECTED, LVIS_SELECTED);
}

static void toggle_check(LISTVIEW *lv, int i)
{
    UINT state = get_state(lv, i);
    int image = (int)((state & LVIS_STATEIMAGEMASK) >> 12);
    set_state(lv, i, INDEXTOSTATEIMAGEMASK(image == 2 ? 1 : 2), LVIS_STATEIMAGEMASK);
}

/* waits for the mouse to leave the drag rectangle or the button to come up. */
static BOOL detect_drag(LISTVIEW *lv, POINT pt, UINT up)
{
    int cx = GetSystemMetrics(SM_CXDRAG), cy = GetSystemMetrics(SM_CYDRAG);
    RECT r = {pt.x - cx, pt.y - cy, pt.x + cx + 1, pt.y + cy + 1};
    SetCapture(lv->Hwnd);
    BOOL drag = FALSE;
    for (;;) {
        MSG msg;
        if (GetCapture() != lv->Hwnd)
            break;
        if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            continue;
        }
        if (msg.message == WM_MOUSEMOVE && msg.hwnd == lv->Hwnd) {
            POINT p = {(short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam)};
            if (!PtInRect(&r, p)) {
                drag = TRUE;
                break;
            }
            continue;
        }
        if (msg.message == up || msg.message == WM_LBUTTONUP || msg.message == WM_RBUTTONUP)
            break;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            break;
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetCapture() == lv->Hwnd)
        ReleaseCapture();
    return drag;
}

static void button_down(LISTVIEW *lv, POINT pt, BOOL right, BOOL dblclk)
{
    BOOL had_focus = GetFocus() == lv->Hwnd;
    LVHITTESTINFO info = {pt};
    hit_test(lv, &info, TRUE, TRUE);
    int i = info.iItem;
    if (lv->View == LV_VIEW_DETAILS && i >= 0 && info.iSubItem > 0 && !(lv->ExStyle & LVS_EX_FULLROWSELECT)) {
        i = -1;
        info.flags = LVHT_NOWHERE;
    }
    UINT keys = key_flags();
    if (dblclk) {
        if (i < 0 && !keys)
            deselect_all_but(lv, -1);
        notify_activate(lv, right ? NM_RDBLCLK : NM_DBLCLK, i, info.iSubItem, pt);
        if (i >= 0 && !right && (info.flags & LVHT_ONITEMSTATEICON) && (lv->ExStyle & LVS_EX_CHECKBOXES))
            toggle_check(lv, i);
        else if (i >= 0 && !right && (info.flags & (LVHT_ONITEMICON | LVHT_ONITEMLABEL)))
            notify_activate(lv, LVN_ITEMACTIVATE, i, info.iSubItem, pt);
        return;
    }
    if (i < 0) {
        if (!had_focus && !keys && !(lv->Style & LVS_SHOWSELALWAYS)) {
            SetFocus(lv->Hwnd);
            return;
        }
        if (!(keys & (LVKF_CONTROL | LVKF_SHIFT)))
            deselect_all_but(lv, -1);
        SetFocus(lv->Hwnd);
        notify_activate(lv, right ? NM_RCLICK : NM_CLICK, -1, info.iSubItem, pt);
        return;
    }
    BOOL was_selected = (get_state(lv, i) & LVIS_SELECTED) != 0;
    if ((keys & LVKF_SHIFT) && !(lv->Style & LVS_SINGLESEL)) {
        int anchor = lv->Mark >= 0 ? lv->Mark : i;
        select_range(lv, anchor, i, (keys & LVKF_CONTROL) != 0);
        set_state(lv, i, LVIS_FOCUSED, LVIS_FOCUSED);
    } else if ((keys & LVKF_CONTROL) && !right) {
        set_state(lv, i, LVIS_FOCUSED | (was_selected ? 0 : LVIS_SELECTED), LVIS_FOCUSED | LVIS_SELECTED);
        lv->Mark = i;
    } else {
        if (!was_selected)
            deselect_all_but(lv, i);
        set_state(lv, i, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
        lv->Mark = i;
    }
    if (detect_drag(lv, pt, right ? WM_RBUTTONUP : WM_LBUTTONUP)) {
        NMLISTVIEW nm = {};
        nm.iItem = i;
        nm.ptAction = pt;
        nm.lParam = get_param(lv, i);
        CcNotify(&lv->Notify, right ? LVN_BEGINRDRAG : LVN_BEGINDRAG, &nm.hdr);
        return;
    }
    SetFocus(lv->Hwnd);
    if (!keys && was_selected)
        deselect_all_but(lv, i);
    LRESULT r = notify_activate(lv, right ? NM_RCLICK : NM_CLICK, i, info.iSubItem, pt);
    if (!right && (info.flags & LVHT_ONITEMSTATEICON) && (lv->ExStyle & LVS_EX_CHECKBOXES)) {
        toggle_check(lv, i);
        return;
    }
    if (!right && ((lv->ExStyle & LVS_EX_ONECLICKACTIVATE) || ((lv->ExStyle & LVS_EX_TWOCLICKACTIVATE) && was_selected)))
        notify_activate(lv, LVN_ITEMACTIVATE, i, info.iSubItem, pt);
    if (right && !r) {
        POINT screen = pt;
        ClientToScreen(lv->Hwnd, &screen);
        SendMessageW(lv->Hwnd, WM_CONTEXTMENU, (WPARAM)lv->Hwnd, MAKELPARAM(screen.x, screen.y));
    }
}

static int page_target(LISTVIEW *lv, int focus, BOOL down)
{
    int rows = lv->View == LV_VIEW_DETAILS ? visible_rows(lv) : rows_per_column(lv);
    if (rows < 1)
        rows = 1;
    if (down) {
        int last = lv->ScrollY + rows - 1;
        if (lv->View != LV_VIEW_DETAILS)
            last = focus + rows;
        int target = focus < last ? last : focus + (rows - 1 > 1 ? rows - 1 : 1);
        return target >= lv->Count ? lv->Count - 1 : target;
    }
    int first = lv->View == LV_VIEW_DETAILS ? lv->ScrollY : focus - rows;
    int target = focus > first ? first : focus - (rows - 1 > 1 ? rows - 1 : 1);
    return target < 0 ? 0 : target;
}

static void key_down(LISTVIEW *lv, WPARAM key)
{
    NMLVKEYDOWN nm = {};
    nm.wVKey = (WORD)key;
    if (CcNotify(&lv->Notify, LVN_KEYDOWN, &nm.hdr))
        return;
    if (!lv->Count)
        return;
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    int focus = lv->Focus >= 0 ? lv->Focus : 0, target = -1;
    switch (key) {
    case VK_RETURN:
        CcNotifyHdr(&lv->Notify, NM_RETURN);
        if (lv->Focus >= 0) {
            POINT pt = {};
            notify_activate(lv, LVN_ITEMACTIVATE, lv->Focus, 0, pt);
        }
        return;
    case VK_SPACE:
        if (lv->Focus < 0)
            return;
        if ((lv->ExStyle & LVS_EX_CHECKBOXES) && !ctrl && !shift) {
            toggle_check(lv, lv->Focus);
        } else if (ctrl) {
            set_state(lv, lv->Focus, (get_state(lv, lv->Focus) & LVIS_SELECTED) ? 0 : LVIS_SELECTED, LVIS_SELECTED);
        } else if (shift && !(lv->Style & LVS_SINGLESEL)) {
            select_range(lv, lv->Mark >= 0 ? lv->Mark : lv->Focus, lv->Focus, FALSE);
        } else {
            set_state(lv, lv->Focus, LVIS_SELECTED, LVIS_SELECTED);
        }
        return;
    case VK_UP:
        target = lv->View == LV_VIEW_DETAILS || lv->View == LV_VIEW_LIST ? focus - 1
                                                                          : get_next_item(lv, focus, LVNI_ABOVE);
        break;
    case VK_DOWN:
        target = lv->View == LV_VIEW_DETAILS || lv->View == LV_VIEW_LIST ? focus + 1
                                                                          : get_next_item(lv, focus, LVNI_BELOW);
        break;
    case VK_LEFT:
    case VK_RIGHT:
        if (lv->View == LV_VIEW_DETAILS) {
            RECT client;
            client_rect(lv, &client);
            int step = ctrl ? client.right : lv->AveChar;
            scroll_to(lv, lv->ScrollX + (key == VK_LEFT ? -step : step), lv->ScrollY);
            return;
        }
        target = get_next_item(lv, focus, key == VK_LEFT ? LVNI_TOLEFT : LVNI_TORIGHT);
        break;
    case VK_HOME:
        target = 0;
        break;
    case VK_END:
        target = lv->Count - 1;
        break;
    case VK_NEXT:
        target = page_target(lv, focus, TRUE);
        break;
    case VK_PRIOR:
        target = page_target(lv, focus, FALSE);
        break;
    default:
        return;
    }
    if (target < 0 || target >= lv->Count)
        return;
    if (ctrl && !shift) {
        set_state(lv, target, LVIS_FOCUSED, LVIS_FOCUSED);
    } else if (shift && !(lv->Style & LVS_SINGLESEL)) {
        select_range(lv, lv->Mark >= 0 ? lv->Mark : focus, target, ctrl);
        set_state(lv, target, LVIS_FOCUSED, LVIS_FOCUSED);
    } else {
        lv->Mark = target;
        deselect_all_but(lv, target);
        set_state(lv, target, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
    }
    ensure_visible(lv, target, FALSE);
}

static void on_scroll(LISTVIEW *lv, int bar, int code)
{
    SCROLLINFO si = {sizeof(si), SIF_ALL};
    GetScrollInfo(lv->Hwnd, bar, &si);
    int pos = si.nPos;
    int line = 1, page = (int)si.nPage > 1 ? (int)si.nPage : 1;
    if (bar == SB_HORZ && lv->View == LV_VIEW_DETAILS)
        line = lv->AveChar > 0 ? lv->AveChar : 8;
    if (bar == SB_VERT && lv->View != LV_VIEW_DETAILS)
        line = lv->RowHeight;
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
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        pos = si.nTrackPos;
        break;
    case SB_TOP:
        pos = si.nMin;
        break;
    case SB_BOTTOM:
        pos = si.nMax;
        break;
    default:
        return;
    }
    if (bar == SB_VERT)
        scroll_to(lv, lv->ScrollX, pos);
    else if (lv->View == LV_VIEW_LIST)
        scroll_to(lv, pos * lv->ColumnWidth, lv->ScrollY);
    else
        scroll_to(lv, pos, lv->ScrollY);
}

/* ------------------------------------------------------------------ header notifications */

static LRESULT header_notify(LISTVIEW *lv, NMHDR *hdr)
{
    NMHEADERW *nm = (NMHEADERW *)hdr;
    switch (hdr->code) {
    case HDN_ITEMCLICKW:
    case HDN_ITEMCLICKA: {
        NMLISTVIEW click = {};
        click.iItem = -1;
        click.iSubItem = nm->iItem;
        CcNotify(&lv->Notify, LVN_COLUMNCLICK, &click.hdr);
        break;
    }
    case HDN_DIVIDERDBLCLICKW:
    case HDN_DIVIDERDBLCLICKA:
        set_column_width(lv, nm->iItem, LVSCW_AUTOSIZE);
        break;
    case HDN_BEGINDRAG:
        if (!(lv->ExStyle & LVS_EX_HEADERDRAGDROP))
            return TRUE;
        break;
    case HDN_ITEMCHANGEDW:
    case HDN_ITEMCHANGEDA:
        update_scrollbars(lv);
        invalidate_all(lv);
        break;
    }
    /* the parent sees every header notification, as sent by the header. */
    LRESULT r = lv->Notify.Parent ? SendMessageW(lv->Notify.Parent, WM_NOTIFY, hdr->idFrom, (LPARAM)hdr) : 0;
    if (hdr->code == HDN_ITEMCHANGEDW || hdr->code == HDN_ITEMCHANGEDA || hdr->code == HDN_ENDDRAG)
        invalidate_all(lv);
    return r != 0;
}

/* ------------------------------------------------------------------ window procedure */

static void set_view(LISTVIEW *lv, int view)
{
    lv->View = view;
    if (view == LV_VIEW_DETAILS)
        ensure_header(lv);
    lv->ScrollX = lv->ScrollY = 0;
    compute_metrics(lv);
    layout_header(lv);
    update_scrollbars(lv);
    invalidate_all(lv);
}

static int find_item(LISTVIEW *lv, int start, const LVFINDINFOW *find, BOOL ansi)
{
    if (!find || start < -1 || start >= lv->Count)
        return -1;
    LPWSTR text = nullptr;
    if (find->flags & LVFI_PARAM) {
        for (int k = start + 1; k < lv->Count; k++)
            if (get_param(lv, k) == find->lParam)
                return k;
        if (find->flags & LVFI_WRAP)
            for (int k = 0; k <= start; k++)
                if (get_param(lv, k) == find->lParam)
                    return k;
        return -1;
    }
    if (!find->psz)
        return -1;
    text = ansi ? CcStrDupAtoW((LPCSTR)find->psz) : (LPWSTR)find->psz;
    if (!text)
        return -1;
    BOOL partial = (find->flags & (LVFI_PARTIAL | LVFI_SUBSTRING)) != 0;
    int n = (int)wcslen(text), found = -1;
    WCHAR buffer[1024];
    for (int pass = 0; pass < 2 && found < 0; pass++) {
        int from = pass ? 0 : start + 1, to = pass ? start + 1 : lv->Count;
        if (pass && !(find->flags & LVFI_WRAP))
            break;
        for (int k = from; k < to; k++) {
            display_info(lv, k, 0, buffer, 1024, nullptr);
            BOOL match = partial ? !_wcsnicmp(buffer, text, (SIZE_T)n) : !lstrcmpiW(buffer, text);
            if (match) {
                found = k;
                break;
            }
        }
    }
    if (ansi)
        CcFree(text);
    return found;
}

typedef struct SORT_CONTEXT {
    PFNLVCOMPARE Compare;
    LPARAM Param;
    BOOL Indices;
    LISTVIEW *List;
} SORT_CONTEXT;

static void sort_items(LV_ITEM **items, LV_ITEM **tmp, int *index, int *tmp_index, int n, SORT_CONTEXT *ctx)
{
    if (n < 2)
        return;
    int half = n / 2;
    sort_items(items, tmp, index, tmp_index, half, ctx);
    sort_items(items + half, tmp, index + half, tmp_index, n - half, ctx);
    int a = 0, b = half, k = 0;
    while (a < half && b < n) {
        LPARAM pa = ctx->Indices ? index[a] : items[a]->Param, pb = ctx->Indices ? index[b] : items[b]->Param;
        BOOL take_a = ctx->Compare(pa, pb, ctx->Param) <= 0;
        tmp_index[k] = take_a ? index[a] : index[b];
        tmp[k++] = take_a ? items[a++] : items[b++];
    }
    while (a < half) {
        tmp_index[k] = index[a];
        tmp[k++] = items[a++];
    }
    while (b < n) {
        tmp_index[k] = index[b];
        tmp[k++] = items[b++];
    }
    memcpy(items, tmp, (SIZE_T)n * sizeof(LV_ITEM *));
    memcpy(index, tmp_index, (SIZE_T)n * sizeof(int));
}

static BOOL sort(LISTVIEW *lv, PFNLVCOMPARE compare, LPARAM param, BOOL indices)
{
    if (!compare || is_ownerdata(lv))
        return FALSE;
    if (lv->Count < 2)
        return TRUE;
    LV_ITEM *focused = lv->Focus >= 0 ? lv->Items[lv->Focus] : nullptr;
    LV_ITEM **tmp = CcAlloc((SIZE_T)lv->Count * sizeof(LV_ITEM *));
    int *index = CcAlloc((SIZE_T)lv->Count * sizeof(int) * 2);
    if (!tmp || !index) {
        CcFree(tmp);
        CcFree(index);
        return FALSE;
    }
    for (int k = 0; k < lv->Count; k++)
        index[k] = k;
    SORT_CONTEXT ctx = {compare, param, indices, lv};
    sort_items(lv->Items, tmp, index, index + lv->Count, lv->Count, &ctx);
    CcFree(tmp);
    CcFree(index);
    if (focused)
        for (int k = 0; k < lv->Count; k++)
            if (lv->Items[k] == focused)
                lv->Focus = k;
    invalidate_all(lv);
    return TRUE;
}

static void destroy_all(LISTVIEW *lv)
{
    for (int i = 0; i < lv->Count && !is_ownerdata(lv); i++)
        free_item(lv->Items[i]);
    CcFree(lv->Items);
    CcFree(lv->DataStates);
    lv->Items = nullptr;
    lv->Count = 0;
}

static void set_ex_style(LISTVIEW *lv, DWORD mask, DWORD value)
{
    DWORD old = lv->ExStyle;
    DWORD style = mask ? (mask & value) | (old & ~mask) : value;
    style &= ~LVS_EX_REGIONAL;
    if (lv->View != LV_VIEW_ICON)
        style &= ~(LVS_EX_SINGLEROW | LVS_EX_MULTIWORKAREAS);
    if (style & LVS_EX_AUTOCHECKSELECT)
        style &= ~LVS_EX_SIMPLESELECT;
    lv->ExStyle = style;
    if ((style & ~old) & LVS_EX_TRANSPARENTBKGND) {
        lv->Bk = CLR_NONE;
        lv->TextBk = CLR_NONE;
    }
    if ((style & ~old) & LVS_EX_CHECKBOXES) {
        ensure_checkbox_list(lv);
        for (int i = 0; i < lv->Count && !is_ownerdata(lv); i++)
            lv->Items[i]->State = (lv->Items[i]->State & ~LVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(1);
        compute_metrics(lv);
    } else if ((old & ~style) & LVS_EX_CHECKBOXES) {
        if (lv->OwnStateList && lv->StateList)
            ImageList_Destroy(lv->StateList);
        lv->StateList = nullptr;
        lv->OwnStateList = FALSE;
        compute_metrics(lv);
    }
    if (lv->Header && ((style ^ old) & LVS_EX_HEADERINALLVIEWS))
        layout_header(lv);
    update_scrollbars(lv);
    invalidate_all(lv);
}

static void open_theme(LISTVIEW *lv)
{
    if (lv->Theme)
        CloseThemeData(lv->Theme);
    lv->Theme = OpenThemeData(lv->Hwnd, L"ListView");
    lv->ThemedItems = lv->Theme && IsThemePartDefined(lv->Theme, CC_LVP_LISTITEM, 0);
}

LRESULT CALLBACK ListViewProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LISTVIEW *lv = get_data(hwnd);
    if (!lv && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE:
        lv = CcAlloc(sizeof(*lv));
        if (!lv)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)lv);
        lv->Hwnd = hwnd;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        lv->Style = (DWORD)cs->style;
        lv->View = (int)(cs->style & LVS_TYPEMASK);
        lv->Focus = lv->Mark = lv->Hot = -1;
        lv->Bk = GetSysColor(COLOR_WINDOW);
        lv->Text = CLR_DEFAULT;
        lv->TextBk = CLR_DEFAULT;
        lv->Redraw = TRUE;
        lv->Font = CcIconTitleFont();
        lv->DefaultFont = TRUE;
        lv->SpacingX = GetSystemMetrics(SM_CXICONSPACING);
        lv->SpacingY = GetSystemMetrics(SM_CYICONSPACING);
        CcNotifyInit(&lv->Notify, hwnd, cs->hwndParent);
        SendMessageW(cs->hwndParent, WM_QUERYUISTATE, 0, 0);
        open_theme(lv);
        measure_font(lv);
        compute_metrics(lv);
        if (lv->View == LV_VIEW_DETAILS || (lv->ExStyle & LVS_EX_HEADERINALLVIEWS))
            ensure_header(lv);
        if (lv->View == LV_VIEW_DETAILS && (lv->Style & LVS_OWNERDRAWFIXED)) {
            MEASUREITEMSTRUCT mis = {};
            mis.CtlType = ODT_LISTVIEW;
            mis.CtlID = (UINT)GetDlgCtrlID(hwnd);
            mis.itemHeight = (UINT)lv->RowHeight;
            SendMessageW(cs->hwndParent, WM_MEASUREITEM, mis.CtlID, (LPARAM)&mis);
            lv->RowHeight = mis.itemHeight > 0 ? (int)mis.itemHeight : 1;
        }
        update_scrollbars(lv);
        return 0;
    }
    case WM_DESTROY:
        /* delete the items with their notifications while the parent is still there. */
        lv->Redraw = FALSE;
        delete_all(lv);
        if (lv->Theme)
            CloseThemeData(lv->Theme);
        lv->Theme = nullptr;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_NCDESTROY:
        destroy_all(lv);
        if (!(lv->Style & LVS_SHAREIMAGELISTS)) {
            if (lv->Normal)
                ImageList_Destroy(lv->Normal);
            if (lv->Small)
                ImageList_Destroy(lv->Small);
            if (lv->StateList && !lv->OwnStateList)
                ImageList_Destroy(lv->StateList);
        }
        if (lv->OwnStateList && lv->StateList)
            ImageList_Destroy(lv->StateList);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(lv);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND: {
        if (lv->Bk != CLR_NONE) {
            RECT r;
            GetClientRect(hwnd, &r);
            CcFillSolid((HDC)wparam, &r, lv->Bk);
        }
        return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc) {
            RECT update;
            if (wparam)
                GetClientRect(hwnd, &update);
            else
                update = ps.rcPaint;
            if (lv->ExStyle & LVS_EX_DOUBLEBUFFER) {
                CC_DRAW_BUFFER b;
                paint(lv, CcBeginBuffer(&b, dc, &update), &update);
                CcEndBuffer(&b);
            } else {
                paint(lv, dc, &update);
            }
        }
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        layout_header(lv);
        compute_metrics(lv);
        scroll_to(lv, lv->ScrollX, lv->ScrollY);
        update_scrollbars(lv);
        invalidate_all(lv);
        return 0;
    case WM_SETFONT:
        lv->Font = wparam ? (HFONT)wparam : CcIconTitleFont();
        lv->DefaultFont = !wparam;
        measure_font(lv);
        compute_metrics(lv);
        if (lv->Header) {
            SendMessageW(lv->Header, WM_SETFONT, (WPARAM)lv->Font, lparam);
            layout_header(lv);
        }
        update_scrollbars(lv);
        if (LOWORD(lparam))
            invalidate_all(lv);
        return 0;
    case WM_GETFONT:
        return (LRESULT)lv->Font;
    case WM_SETREDRAW:
        lv->Redraw = wparam != 0;
        if (lv->Redraw) {
            update_scrollbars(lv);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            DWORD old = lv->Style;
            lv->Style = ((STYLESTRUCT *)lparam)->styleNew;
            if ((old ^ lv->Style) & LVS_NOCOLUMNHEADER && lv->Header) {
                DWORD hs = (DWORD)GetWindowLongW(lv->Header, GWL_STYLE);
                SetWindowLongW(lv->Header, GWL_STYLE,
                               (LONG)((lv->Style & LVS_NOCOLUMNHEADER) ? hs | HDS_HIDDEN : hs & ~HDS_HIDDEN));
                layout_header(lv);
            }
            if ((old ^ lv->Style) & LVS_TYPEMASK)
                set_view(lv, (int)(lv->Style & LVS_TYPEMASK));
            invalidate_all(lv);
        }
        return 0;
    case WM_THEMECHANGED:
        open_theme(lv);
        if (lv->Header)
            SendMessageW(lv->Header, WM_THEMECHANGED, 0, 0);
        invalidate_all(lv);
        return 0;
    case WM_SYSCOLORCHANGE:
        invalidate_all(lv);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        CcNotifyHdr(&lv->Notify, msg == WM_SETFOCUS ? NM_SETFOCUS : NM_KILLFOCUS);
        invalidate_all(lv);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        button_down(lv, pt, msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK,
                    msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK);
        return 0;
    }
    case WM_KEYDOWN:
        key_down(lv, wparam);
        return 0;
    case WM_VSCROLL:
    case WM_HSCROLL:
        on_scroll(lv, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, LOWORD(wparam));
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = (short)HIWORD(wparam);
        int lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        int step = -delta * lines / WHEEL_DELTA;
        if (lv->View == LV_VIEW_DETAILS)
            scroll_to(lv, lv->ScrollX, lv->ScrollY + step);
        else if (lv->View == LV_VIEW_LIST)
            scroll_to(lv, lv->ScrollX + (step > 0 ? 1 : -1) * lv->ColumnWidth, 0);
        else
            scroll_to(lv, lv->ScrollX, lv->ScrollY + step * lv->RowHeight);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lparam;
        if (hdr && lv->Header && hdr->hwndFrom == lv->Header)
            return header_notify(lv, hdr);
        return 0;
    }
    case WM_NOTIFYFORMAT:
        if ((HWND)wparam == lv->Header)
            return NFR_UNICODE;
        return CcNotifyFormat(&lv->Notify, (HWND)wparam, lparam);
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x10012;
        break;
    case CCM_SETUNICODEFORMAT: {
        BOOL old = lv->Notify.Unicode;
        lv->Notify.Unicode = wparam != 0;
        return old;
    }
    case CCM_GETUNICODEFORMAT:
        return lv->Notify.Unicode;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    case LVM_GETITEMCOUNT:
        return lv->Count;
    case LVM_INSERTITEMW:
        return insert_item(lv, (LVITEMW *)lparam, FALSE);
    case LVM_INSERTITEMA:
        return insert_item(lv, (LVITEMW *)lparam, TRUE);
    case LVM_SETITEMW:
        return set_item(lv, (LVITEMW *)lparam, FALSE);
    case LVM_SETITEMA:
        return set_item(lv, (LVITEMW *)lparam, TRUE);
    case LVM_GETITEMW:
        return get_item(lv, (LVITEMW *)lparam, FALSE);
    case LVM_GETITEMA:
        return get_item(lv, (LVITEMW *)lparam, TRUE);
    case LVM_SETITEMTEXTW:
    case LVM_SETITEMTEXTA: {
        LVITEMW *src = (LVITEMW *)lparam;
        if (!src || is_ownerdata(lv))
            return FALSE;
        LVITEMW item = *src;
        item.mask = LVIF_TEXT;
        item.iItem = (int)wparam;
        return set_item(lv, &item, msg == LVM_SETITEMTEXTA);
    }
    case LVM_GETITEMTEXTW:
    case LVM_GETITEMTEXTA: {
        LVITEMW *dst = (LVITEMW *)lparam;
        if (!dst)
            return 0;
        dst->mask = LVIF_TEXT;
        dst->iItem = (int)wparam;
        if (!get_item(lv, dst, msg == LVM_GETITEMTEXTA))
            return 0;
        if (msg == LVM_GETITEMTEXTA)
            return ((LVITEMA *)dst)->pszText ? lstrlenA(((LVITEMA *)dst)->pszText) : 0;
        return dst->pszText && !CcIsCallback(dst->pszText) ? lstrlenW(dst->pszText) : 0;
    }
    case LVM_SETITEMSTATE:
        return set_item_state(lv, (int)wparam, (LVITEMW *)lparam);
    case LVM_GETITEMSTATE:
        if ((int)wparam < 0 || (int)wparam >= lv->Count)
            return 0;
        return get_state(lv, (int)wparam) & (UINT)lparam;
    case LVM_DELETEITEM:
        return delete_item(lv, (int)wparam);
    case LVM_DELETEALLITEMS:
        return delete_all(lv);
    case LVM_GETCALLBACKMASK:
        return lv->CallbackMask;
    case LVM_SETCALLBACKMASK:
        lv->CallbackMask = (UINT)wparam;
        return TRUE;
    case LVM_GETNEXTITEM:
        return get_next_item(lv, (int)wparam, LOWORD(lparam));
    case LVM_INSERTCOLUMNW:
    case LVM_INSERTCOLUMNA:
        return insert_column(lv, (int)wparam, (LVCOLUMNW *)lparam, msg == LVM_INSERTCOLUMNA);
    case LVM_SETCOLUMNW:
    case LVM_SETCOLUMNA:
        return set_column(lv, (int)wparam, (LVCOLUMNW *)lparam, msg == LVM_SETCOLUMNA);
    case LVM_GETCOLUMNW:
    case LVM_GETCOLUMNA:
        return get_column(lv, (int)wparam, (LVCOLUMNW *)lparam, msg == LVM_GETCOLUMNA);
    case LVM_DELETECOLUMN:
        return delete_column(lv, (int)wparam);
    case LVM_SETCOLUMNWIDTH:
        return set_column_width(lv, (int)wparam, (short)LOWORD(lparam));
    case LVM_GETCOLUMNWIDTH:
        if (lv->View == LV_VIEW_LIST)
            return lv->ColumnWidth;
        if (lv->View == LV_VIEW_DETAILS || lv->Header) {
            HDITEMW hd = {};
            hd.mask = HDI_WIDTH;
            if (lv->Header && SendMessageW(lv->Header, HDM_GETITEMW, wparam, (LPARAM)&hd))
                return hd.cxy;
        }
        return 0;
    case LVM_GETHEADER:
        return (LRESULT)lv->Header;
    case LVM_SETCOLUMNORDERARRAY:
        return lv->Header ? SendMessageW(lv->Header, HDM_SETORDERARRAY, wparam, lparam) : FALSE;
    case LVM_GETCOLUMNORDERARRAY:
        return lv->Header ? SendMessageW(lv->Header, HDM_GETORDERARRAY, wparam, lparam) : FALSE;
    case LVM_ENSUREVISIBLE:
        if ((int)wparam < 0 || (int)wparam >= lv->Count || (lv->Style & LVS_NOSCROLL))
            return FALSE;
        ensure_visible(lv, (int)wparam, lparam != 0);
        return TRUE;
    case LVM_GETTOPINDEX:
        if (lv->View == LV_VIEW_DETAILS)
            return lv->ScrollY;
        if (lv->View == LV_VIEW_LIST)
            return rows_per_column(lv) * (lv->ScrollX / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1));
        return 0;
    case LVM_GETCOUNTPERPAGE: {
        int n;
        if (lv->View == LV_VIEW_DETAILS)
            n = visible_rows(lv);
        else if (lv->View == LV_VIEW_LIST) {
            RECT r;
            client_rect(lv, &r);
            n = rows_per_column(lv) * (r.right / (lv->ColumnWidth > 0 ? lv->ColumnWidth : 1));
        } else
            n = lv->Count;
        return n > 0 ? n : 0;
    }
    case LVM_GETITEMRECT: {
        RECT *out = (RECT *)lparam;
        if (!out || (int)wparam == -1 || (int)wparam >= lv->Count || out->left > LVIR_SELECTBOUNDS ||
            out->left < 0 || (int)wparam < 0)
            return FALSE;
        ITEM_RECTS r;
        item_rects(lv, (int)wparam, &r);
        switch (out->left) {
        case LVIR_BOUNDS:
            *out = r.Bounds;
            break;
        case LVIR_ICON:
            *out = r.Icon;
            break;
        case LVIR_LABEL:
            *out = r.Label;
            break;
        default:
            *out = r.Select;
            break;
        }
        return TRUE;
    }
    case LVM_GETSUBITEMRECT: {
        RECT *out = (RECT *)lparam;
        if (!out || (int)wparam < 0 || (int)wparam >= lv->Count || lv->View != LV_VIEW_DETAILS)
            return FALSE;
        int sub = out->top, code = out->left;
        if (sub == 0) {
            ITEM_RECTS r;
            item_rects(lv, (int)wparam, &r);
            *out = code == LVIR_ICON ? r.Icon : code == LVIR_LABEL ? r.Label : r.Bounds;
            if (code == LVIR_BOUNDS) {
                RECT col;
                if (column_rect(lv, 0, &col)) {
                    out->left = col.left;
                    out->right = col.right;
                }
            }
            return TRUE;
        }
        if (sub < 0 || sub >= column_count(lv))
            return FALSE;
        subitem_rect(lv, (int)wparam, sub, out, code == LVIR_ICON);
        return TRUE;
    }
    case LVM_HITTEST: {
        LVHITTESTINFO *info = (LVHITTESTINFO *)lparam;
        if (!info)
            return -1;
        int r = hit_test(lv, info, FALSE, TRUE);
        info->flags &= (UINT)wparam | 0x7F;
        return r;
    }
    case LVM_SUBITEMHITTEST: {
        LVHITTESTINFO *info = (LVHITTESTINFO *)lparam;
        if (!info)
            return -1;
        hit_test(lv, info, TRUE, FALSE);
        return info->iItem;
    }
    case LVM_SETIMAGELIST:
        return (LRESULT)set_image_list(lv, (int)wparam, (HIMAGELIST)lparam);
    case LVM_GETIMAGELIST:
        switch (wparam) {
        case LVSIL_NORMAL:
            return (LRESULT)lv->Normal;
        case LVSIL_SMALL:
            return (LRESULT)lv->Small;
        case LVSIL_STATE:
            return (LRESULT)lv->StateList;
        }
        return 0;
    case LVM_SETEXTENDEDLISTVIEWSTYLE: {
        DWORD old = lv->ExStyle;
        set_ex_style(lv, (DWORD)wparam, (DWORD)lparam);
        return old;
    }
    case LVM_GETEXTENDEDLISTVIEWSTYLE:
        return lv->ExStyle;
    case LVM_SETBKCOLOR:
        lv->Bk = (COLORREF)lparam;
        if (!(lv->Style & LVS_SHAREIMAGELISTS)) {
            if (lv->Normal)
                ImageList_SetBkColor(lv->Normal, lv->Bk);
            if (lv->Small)
                ImageList_SetBkColor(lv->Small, lv->Bk);
            if (lv->StateList)
                ImageList_SetBkColor(lv->StateList, lv->Bk);
        }
        invalidate_all(lv);
        return TRUE;
    case LVM_GETBKCOLOR:
        return lv->Bk;
    case LVM_SETTEXTCOLOR:
        lv->Text = (COLORREF)lparam;
        invalidate_all(lv);
        return TRUE;
    case LVM_GETTEXTCOLOR:
        return lv->Text;
    case LVM_SETTEXTBKCOLOR:
        lv->TextBk = (COLORREF)lparam;
        invalidate_all(lv);
        return TRUE;
    case LVM_GETTEXTBKCOLOR:
        return lv->TextBk;
    case LVM_SCROLL: {
        if (lv->Style & LVS_NOSCROLL)
            return FALSE;
        int dx = (int)wparam, dy = (int)lparam;
        NMLVSCROLL nm = {};
        nm.dx = dx;
        nm.dy = dy;
        CcNotify(&lv->Notify, LVN_BEGINSCROLL, &nm.hdr);
        if (lv->View == LV_VIEW_DETAILS) {
            int rows = dy >= 0 ? (dy + lv->RowHeight / 2) / lv->RowHeight : -((-dy + lv->RowHeight / 2) / lv->RowHeight);
            scroll_to(lv, lv->ScrollX + dx, lv->ScrollY + rows);
        } else if (lv->View == LV_VIEW_LIST) {
            if (dy) {
                CcNotify(&lv->Notify, LVN_ENDSCROLL, &nm.hdr);
                return FALSE;
            }
            int w = lv->ColumnWidth > 0 ? lv->ColumnWidth : 1;
            int cols = dx >= 0 ? (dx + w - 1) / w : -((-dx + w - 1) / w);
            scroll_to(lv, lv->ScrollX + cols * w, 0);
        } else {
            scroll_to(lv, lv->ScrollX + dx, lv->ScrollY + dy);
        }
        CcNotify(&lv->Notify, LVN_ENDSCROLL, &nm.hdr);
        return TRUE;
    }
    case LVM_GETSTRINGWIDTHW:
        return lparam && !CcIsCallback((LPCWSTR)lparam) ? string_width(lv, (LPCWSTR)lparam) : 0;
    case LVM_GETSTRINGWIDTHA: {
        if (!lparam || (LPCSTR)lparam == LPSTR_TEXTCALLBACKA)
            return 0;
        LPWSTR w = CcStrDupAtoW((LPCSTR)lparam);
        int r = string_width(lv, w);
        CcFree(w);
        return r;
    }
    case LVM_SETITEMCOUNT:
        if (!is_ownerdata(lv)) {
            /* only a hint: reserve a reasonable amount, the rest grows on demand. */
            if ((int)wparam > lv->Capacity && wparam <= 0x100000) {
                LV_ITEM **items = CcReAlloc(lv->Items, wparam * sizeof(LV_ITEM *));
                if (items) {
                    lv->Items = items;
                    lv->Capacity = (int)wparam;
                }
            }
            return TRUE;
        }
        if (wparam > OWNERDATA_MAX)
            return FALSE;
        lv->Count = (int)wparam;
        if (lv->DataStatesSize > lv->Count)
            lv->DataStatesSize = lv->Count;
        if (lv->Focus >= lv->Count)
            lv->Focus = -1;
        if (lv->Mark >= lv->Count)
            lv->Mark = -1;
        if (lv->Hot >= lv->Count)
            lv->Hot = -1;
        scroll_to(lv, lv->ScrollX, lv->ScrollY);
        update_scrollbars(lv);
        invalidate_all(lv);
        return TRUE;
    case LVM_GETSELECTEDCOUNT: {
        int n = 0;
        for (int k = 0; k < lv->Count; k++)
            if (get_state(lv, k) & LVIS_SELECTED)
                n++;
        return n;
    }
    case LVM_GETSELECTIONMARK:
        return lv->Mark;
    case LVM_SETSELECTIONMARK: {
        int old = lv->Mark;
        if ((int)lparam < lv->Count)
            lv->Mark = (int)lparam;
        return old;
    }
    case LVM_GETHOTITEM:
        return lv->Hot;
    case LVM_SETHOTITEM: {
        int old = lv->Hot;
        lv->Hot = (int)wparam;
        invalidate_item(lv, old);
        invalidate_item(lv, lv->Hot);
        return old;
    }
    case LVM_GETVIEWRECT: {
        RECT *out = (RECT *)lparam;
        if (!out || lv->View == LV_VIEW_DETAILS)
            return FALSE;
        SetRectEmpty(out);
        for (int i = 0; i < lv->Count; i++) {
            ITEM_RECTS r;
            item_rects(lv, i, &r);
            UnionRect(out, out, &r.Bounds);
        }
        return TRUE;
    }
    case LVM_GETORIGIN: {
        POINT *pt = (POINT *)lparam;
        if (!pt)
            return FALSE;
        pt->x = lv->ScrollX;
        pt->y = lv->View == LV_VIEW_DETAILS ? lv->ScrollY * lv->RowHeight : lv->View == LV_VIEW_LIST ? 0 : lv->ScrollY;
        return TRUE;
    }
    case LVM_SETVIEW:
        if (wparam > LV_VIEW_MAX)
            return -1;
        if ((int)wparam != lv->View && wparam != LV_VIEW_TILE)
            set_view(lv, (int)wparam);
        return 1;
    case LVM_GETVIEW:
        return lv->View;
    case LVM_ARRANGE:
        invalidate_all(lv);
        return TRUE;
    case LVM_UPDATE:
        if ((int)wparam >= 0 && (int)wparam < lv->Count)
            invalidate_item(lv, (int)wparam);
        return TRUE;
    case LVM_REDRAWITEMS: {
        int first = (int)wparam, last = (int)lparam;
        if (first >= lv->Count)
            return TRUE;
        if (last >= lv->Count)
            last = lv->Count - 1;
        for (int i = first < 0 ? 0 : first; i <= last; i++)
            invalidate_item(lv, i);
        return TRUE;
    }
    case LVM_SORTITEMS:
        return sort(lv, (PFNLVCOMPARE)lparam, (LPARAM)wparam, FALSE);
    case LVM_SORTITEMSEX:
        return sort(lv, (PFNLVCOMPARE)lparam, (LPARAM)wparam, TRUE);
    case LVM_FINDITEMW:
    case LVM_FINDITEMA:
        return find_item(lv, (int)wparam, (LVFINDINFOW *)lparam, msg == LVM_FINDITEMA);
    case LVM_SETITEMPOSITION:
    case LVM_SETITEMPOSITION32:
        return (int)wparam >= 0 && (int)wparam < lv->Count && (lv->View == LV_VIEW_ICON || lv->View == LV_VIEW_SMALLICON);
    case LVM_GETITEMPOSITION: {
        POINT *pt = (POINT *)lparam;
        if (!pt || (int)wparam < 0 || (int)wparam >= lv->Count)
            return FALSE;
        ITEM_RECTS r;
        item_rects(lv, (int)wparam, &r);
        pt->x = r.Bounds.left;
        pt->y = r.Bounds.top;
        return TRUE;
    }
    case LVM_GETITEMSPACING:
        if (wparam) {
            int cx = lv->ColumnWidth;
            return MAKELONG(cx, lv->RowHeight);
        }
        return MAKELONG(lv->SpacingX, lv->SpacingY);
    case LVM_SETICONSPACING: {
        LONG old = MAKELONG(lv->SpacingX, lv->SpacingY);
        int cx = (short)LOWORD(lparam), cy = (short)HIWORD(lparam);
        int icon_cx = 0, icon_cy = 0;
        image_size(lv->Normal, &icon_cx, &icon_cy);
        if (lparam == -1 || !cx)
            cx = GetSystemMetrics(SM_CXICONSPACING) + icon_cx - (icon_cx ? GetSystemMetrics(SM_CXICON) : 0);
        if (lparam == -1 || !cy)
            cy = GetSystemMetrics(SM_CYICONSPACING) + icon_cy - (icon_cy ? GetSystemMetrics(SM_CYICON) : 0);
        lv->SpacingX = cx;
        lv->SpacingY = cy;
        invalidate_all(lv);
        return old;
    }
    case LVM_APPROXIMATEVIEWRECT: {
        int count = (int)wparam == -1 ? lv->Count : (int)wparam;
        if (lv->View == LV_VIEW_DETAILS)
            return MAKELONG(columns_width(lv), header_bottom(lv) + (count + 1) * lv->RowHeight);
        RECT client;
        client_rect(lv, &client);
        if (lv->View == LV_VIEW_LIST) {
            int rows = rows_per_column(lv);
            return MAKELONG(((count + rows - 1) / rows) * lv->ColumnWidth, rows * lv->RowHeight);
        }
        int cell_w = lv->View == LV_VIEW_ICON ? lv->SpacingX : lv->ColumnWidth;
        int cell_h = lv->View == LV_VIEW_ICON ? lv->SpacingY : lv->RowHeight;
        int per_row = client.right / (cell_w > 0 ? cell_w : 1);
        if (per_row < 1)
            per_row = 1;
        return MAKELONG(per_row * cell_w, (count + per_row - 1) / per_row * cell_h);
    }
    case LVM_GETEDITCONTROL:
        return 0;
    case LVM_EDITLABELW:
    case LVM_EDITLABELA:
        return 0;
    case LVM_SETSELECTEDCOLUMN:
        invalidate_all(lv);
        return 0;
    case LVM_ISITEMVISIBLE: {
        if ((int)wparam < 0 || (int)wparam >= lv->Count)
            return FALSE;
        ITEM_RECTS r;
        RECT client, dummy;
        item_rects(lv, (int)wparam, &r);
        client_rect(lv, &client);
        return IntersectRect(&dummy, &r.Bounds, &client);
    }
    case LVM_GETTOOLTIPS:
    case LVM_SETTOOLTIPS:
    case LVM_GETHOTCURSOR:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
