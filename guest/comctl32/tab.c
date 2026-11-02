/* the tab control, SysTabControl32. */
#include "comctl32_private.h"

typedef struct TAB_ITEM {
    LPWSTR Text;
    int Image;
    DWORD State;
    BYTE *Extra;
    RECT Rect;
} TAB_ITEM;

typedef struct TAB {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style, ExStyle;
    TAB_ITEM *Items;
    int Count;
    int Selected, Focus, Hot;
    int ItemExtra;
    int PadX, PadY, MinWidth;
    int FixedWidth, FixedHeight;
    HIMAGELIST Images;
    HFONT Font;
    HTHEME Theme;
    HWND Tooltips;
    BOOL Magic; /* told to scale its magic numbers, message 0x200D. */
} TAB;

static TAB *get_data(HWND hwnd)
{
    return (TAB *)GetWindowLongPtrW(hwnd, 0);
}

static HFONT tab_font(TAB *t)
{
    return t->Font ? t->Font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

/* the edges of the tab, which scale at 192 dpi and above for a themed control. */
static int edge_x(TAB *t)
{
    return CcMagicScale(t->Hwnd, t->Magic, t->Theme != nullptr, GetSystemMetrics(SM_CXEDGE));
}

static int edge_y(TAB *t)
{
    return CcMagicScale(t->Hwnd, t->Magic, t->Theme != nullptr, GetSystemMetrics(SM_CYEDGE));
}

static int item_height(TAB *t)
{
    if (t->FixedHeight)
        return t->FixedHeight;
    HDC dc = GetDC(t->Hwnd);
    HGDIOBJ old = SelectObject(dc, tab_font(t));
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(t->Hwnd, dc);
    int edge = edge_y(t);
    int icon = 0;
    if (t->Images)
        ImageList_GetIconSize(t->Images, nullptr, &icon);
    int content = tm.tmHeight + edge > icon ? tm.tmHeight + edge : icon;
    int pad = t->PadY > edge ? t->PadY : edge;
    int h = 2 * pad + content;
    if (!(t->Style & TCS_BUTTONS))
        h -= 1 + edge;
    return h;
}

static int min_width(TAB *t)
{
    if (t->MinWidth >= 0)
        return t->MinWidth;
    HDC dc = GetDC(t->Hwnd);
    HGDIOBJ old = SelectObject(dc, tab_font(t));
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(t->Hwnd, dc);
    return 2 * (3 * tm.tmAveCharWidth + t->PadX);
}

/* one row along the top, or the bottom with TCS_BOTTOM. */
static void layout(TAB *t)
{
    RECT client;
    GetClientRect(t->Hwnd, &client);
    int h = item_height(t), edge = edge_x(t);
    int x = edge;
    int y = (t->Style & TCS_BOTTOM) ? client.bottom - h - edge : edge;
    HDC dc = GetDC(t->Hwnd);
    HGDIOBJ old = SelectObject(dc, tab_font(t));
    int icon_cx = 0;
    if (t->Images)
        ImageList_GetIconSize(t->Images, &icon_cx, nullptr);
    for (int k = 0; k < t->Count; k++) {
        TAB_ITEM *it = &t->Items[k];
        int w;
        /* a width given with TCM_SETITEMSIZE is for tabs of one width, the others are as wide as their text. */
        if (t->FixedWidth && (t->Style & TCS_FIXEDWIDTH)) {
            w = t->FixedWidth;
        } else {
            SIZE size = {};
            if (it->Text && *it->Text)
                GetTextExtentPoint32W(dc, it->Text, (int)wcslen(it->Text), &size);
            w = size.cx + 2 * t->PadX;
            if (t->Images && it->Image >= 0)
                w += icon_cx + t->PadX;
            if (w < min_width(t))
                w = min_width(t);
        }
        SetRect(&it->Rect, x, y, x + w, y + h);
        x += w;
        if (t->Style & TCS_BUTTONS)
            x += 3;
    }
    SelectObject(dc, old);
    ReleaseDC(t->Hwnd, dc);
}

static void notify_change(TAB *t, int index)
{
    if (index == t->Selected || index < 0 || index >= t->Count)
        return;
    if (CcNotifyHdr(&t->Notify, TCN_SELCHANGING))
        return;
    t->Selected = index;
    t->Focus = index;
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    CcNotifyHdr(&t->Notify, TCN_SELCHANGE);
}

static int hit_test(TAB *t, POINT pt, UINT *flags)
{
    for (int k = 0; k < t->Count; k++)
        if (PtInRect(&t->Items[k].Rect, pt)) {
            if (flags)
                *flags = TCHT_ONITEMLABEL;
            return k;
        }
    if (flags)
        *flags = TCHT_NOWHERE;
    return -1;
}

static void paint(TAB *t, HDC dc)
{
    RECT client;
    GetClientRect(t->Hwnd, &client);
    layout(t);
    if (t->Theme && IsThemeBackgroundPartiallyTransparent(t->Theme, CC_TABP_TABITEM, 1))
        DrawThemeParentBackground(t->Hwnd, dc, &client);
    else
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    if (!(t->Style & TCS_BUTTONS)) {
        RECT pane = client;
        int h = item_height(t);
        if (t->Style & TCS_BOTTOM)
            pane.bottom -= h + edge_y(t) - 1;
        else
            pane.top += h + edge_y(t) - 1;
        if (t->Theme)
            DrawThemeBackground(t->Theme, dc, CC_TABP_PANE, 0, &pane, nullptr);
        else
            DrawEdge(dc, &pane, EDGE_RAISED, BF_RECT | BF_SOFT);
    }
    HGDIOBJ old = SelectObject(dc, tab_font(t));
    SetBkMode(dc, TRANSPARENT);
    /* the selected tab paints last, over its neighbors. */
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < t->Count; k++) {
            if ((k == t->Selected) != (pass == 1))
                continue;
            TAB_ITEM *it = &t->Items[k];
            RECT r = it->Rect;
            BOOL selected = k == t->Selected;
            if (selected && !(t->Style & TCS_BUTTONS))
                InflateRect(&r, 2, 2);
            if (t->Style & TCS_OWNERDRAWFIXED) {
                DRAWITEMSTRUCT dis = {};
                dis.CtlType = ODT_TAB;
                dis.CtlID = (UINT)GetDlgCtrlID(t->Hwnd);
                dis.itemID = (UINT)k;
                dis.itemAction = ODA_DRAWENTIRE;
                dis.itemState = selected ? ODS_SELECTED : 0;
                dis.hwndItem = t->Hwnd;
                dis.hDC = dc;
                dis.rcItem = r;
                if (it->Extra && t->ItemExtra >= (int)sizeof(LPARAM))
                    memcpy(&dis.itemData, it->Extra, sizeof(dis.itemData));
                SendMessageW(t->Notify.Parent, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
                continue;
            }
            if (t->Theme) {
                int state = !IsWindowEnabled(t->Hwnd) ? 4 : selected ? 3 : k == t->Hot ? 2 : 1;
                DrawThemeBackground(t->Theme, dc, CC_TABP_TABITEM, state, &r, nullptr);
            } else if (t->Style & TCS_BUTTONS) {
                DrawEdge(dc, &r, selected ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT | BF_SOFT);
            } else {
                FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
                DrawEdge(dc, &r, EDGE_RAISED, BF_LEFT | BF_TOP | BF_RIGHT | BF_SOFT);
            }
            RECT tr = r;
            tr.left += t->PadX;
            if (t->Images && it->Image >= 0) {
                int cx, cy;
                ImageList_GetIconSize(t->Images, &cx, &cy);
                ImageList_Draw(t->Images, it->Image, dc, tr.left, (tr.top + tr.bottom - cy) / 2, ILD_TRANSPARENT);
                tr.left += cx + t->PadX;
            }
            SetTextColor(dc, GetSysColor(IsWindowEnabled(t->Hwnd) ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
            if (it->Text)
                DrawTextW(dc, it->Text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            if (selected && GetFocus() == t->Hwnd && !CcFocusCuesHidden(t->Hwnd)) {
                RECT fr = r;
                InflateRect(&fr, -3, -3);
                DrawFocusRect(dc, &fr);
            }
        }
    SelectObject(dc, old);
}

static int insert_item(TAB *t, int index, const TCITEMW *src, BOOL ansi)
{
    if (!src || index < 0)
        return -1;
    if (index > t->Count)
        index = t->Count;
    TAB_ITEM *items = CcReAlloc(t->Items, ((SIZE_T)t->Count + 1) * sizeof(TAB_ITEM));
    if (!items)
        return -1;
    t->Items = items;
    TAB_ITEM it = {};
    it.Image = -1;
    if (src->mask & TCIF_TEXT) {
        BOOL ok = ansi ? CcSetTextA(&it.Text, (LPCSTR)src->pszText) : CcSetText(&it.Text, src->pszText);
        if (!ok)
            return -1;
    }
    if (src->mask & TCIF_IMAGE)
        it.Image = src->iImage;
    if (t->ItemExtra) {
        it.Extra = CcAlloc((SIZE_T)t->ItemExtra);
        if (it.Extra && (src->mask & TCIF_PARAM))
            memcpy(it.Extra, &src->lParam, (SIZE_T)t->ItemExtra);
    }
    if (src->mask & TCIF_STATE)
        it.State = src->dwState & src->dwStateMask;
    memmove(&t->Items[index + 1], &t->Items[index], (SIZE_T)(t->Count - index) * sizeof(TAB_ITEM));
    t->Items[index] = it;
    t->Count++;
    if (t->Count == 1) {
        t->Selected = 0;
        t->Focus = 0;
    } else if (t->Selected >= index) {
        t->Selected++;
        if (t->Focus >= index)
            t->Focus++;
    }
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return index;
}

static BOOL get_item(TAB *t, int index, TCITEMW *dst, BOOL ansi)
{
    if (!dst || index < 0 || index >= t->Count)
        return FALSE;
    TAB_ITEM *it = &t->Items[index];
    if (dst->mask & TCIF_TEXT) {
        if (ansi)
            CcCopyTextA((LPSTR)dst->pszText, dst->cchTextMax, it->Text);
        else
            CcCopyText(dst->pszText, dst->cchTextMax, it->Text);
    }
    if (dst->mask & TCIF_IMAGE)
        dst->iImage = it->Image;
    if ((dst->mask & TCIF_PARAM) && it->Extra)
        memcpy(&dst->lParam, it->Extra, (SIZE_T)t->ItemExtra);
    if (dst->mask & TCIF_STATE)
        dst->dwState = it->State & dst->dwStateMask;
    return TRUE;
}

static BOOL set_item(TAB *t, int index, const TCITEMW *src, BOOL ansi)
{
    if (!src || index < 0 || index >= t->Count)
        return FALSE;
    TAB_ITEM *it = &t->Items[index];
    if (src->mask & TCIF_TEXT) {
        if (ansi)
            CcSetTextA(&it->Text, (LPCSTR)src->pszText);
        else
            CcSetText(&it->Text, src->pszText);
    }
    if (src->mask & TCIF_IMAGE)
        it->Image = src->iImage;
    if ((src->mask & TCIF_PARAM) && it->Extra)
        memcpy(it->Extra, &src->lParam, (SIZE_T)t->ItemExtra);
    if (src->mask & TCIF_STATE)
        it->State = (it->State & ~src->dwStateMask) | (src->dwState & src->dwStateMask);
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return TRUE;
}

static void free_item(TAB_ITEM *it)
{
    if (it->Text != LPSTR_TEXTCALLBACKW)
        CcFree(it->Text);
    CcFree(it->Extra);
}

static BOOL delete_item(TAB *t, int index)
{
    if (index < 0 || index >= t->Count)
        return FALSE;
    free_item(&t->Items[index]);
    memmove(&t->Items[index], &t->Items[index + 1], (SIZE_T)(t->Count - index - 1) * sizeof(TAB_ITEM));
    t->Count--;
    if (t->Selected == index)
        t->Selected = -1;
    else if (t->Selected > index)
        t->Selected--;
    if (t->Focus == index)
        t->Focus = -1;
    else if (t->Focus > index)
        t->Focus--;
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return TRUE;
}

LRESULT CALLBACK TabProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TAB *t = get_data(hwnd);
    if (!t && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        t = CcAlloc(sizeof(*t));
        if (!t)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)t);
        t->Hwnd = hwnd;
        t->Style = (DWORD)cs->style;
        t->Selected = t->Focus = t->Hot = -1;
        t->ItemExtra = sizeof(LPARAM);
        t->PadX = 3 * GetSystemMetrics(SM_CXEDGE);
        t->PadY = 3 * GetSystemMetrics(SM_CYEDGE) / 2;
        t->MinWidth = -1;
        CcNotifyInit(&t->Notify, hwnd, cs->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        t->Theme = OpenThemeData(hwnd, L"Tab");
        return 0;
    case WM_DESTROY:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        for (int k = 0; k < t->Count; k++)
            free_item(&t->Items[k]);
        CcFree(t->Items);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(t);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc) {
            RECT client;
            GetClientRect(hwnd, &client);
            CC_DRAW_BUFFER b;
            paint(t, wparam ? dc : CcBeginBuffer(&b, dc, &client));
            if (!wparam)
                CcEndBuffer(&b);
        }
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case 0x200D: /* scale the magic numbers or not. */
        t->Magic = wparam != 0;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETFONT:
        t->Font = (HFONT)wparam;
        if (lparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)t->Font;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        layout(t);
        int k = hit_test(t, pt, nullptr);
        if (k >= 0) {
            if (!(t->Style & TCS_FOCUSNEVER))
                SetFocus(hwnd);
            notify_change(t, k);
        }
        return 0;
    }
    case WM_RBUTTONUP:
        if (CcNotifyHdr(&t->Notify, NM_RCLICK))
            return 0;
        break;
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        layout(t);
        int k = hit_test(t, pt, nullptr);
        if (k != t->Hot) {
            t->Hot = k;
            InvalidateRect(hwnd, nullptr, TRUE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        t->Hot = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_KEYDOWN: {
        NMKEY nm = {};
        nm.nVKey = (UINT)wparam;
        nm.uFlags = HIWORD(lparam);
        CcNotify(&t->Notify, TCN_KEYDOWN, &nm.hdr);
        if (wparam == VK_LEFT && t->Selected > 0)
            notify_change(t, t->Selected - 1);
        else if (wparam == VK_RIGHT && t->Selected < t->Count - 1)
            notify_change(t, t->Selected + 1);
        return 0;
    }
    case WM_THEMECHANGED:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = OpenThemeData(hwnd, L"Tab");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            t->Style = ((STYLESTRUCT *)lparam)->styleNew;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&t->Notify, (HWND)wparam, lparam);
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x1000F;
        break;
    case TCM_GETIMAGELIST:
        return (LRESULT)t->Images;
    case TCM_SETIMAGELIST: {
        HIMAGELIST old = t->Images;
        t->Images = (HIMAGELIST)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return (LRESULT)old;
    }
    case TCM_GETITEMCOUNT:
        return t->Count;
    case TCM_INSERTITEMA:
    case TCM_INSERTITEMW:
        return insert_item(t, (int)wparam, (const TCITEMW *)lparam, msg == TCM_INSERTITEMA);
    case TCM_GETITEMA:
    case TCM_GETITEMW:
        return get_item(t, (int)wparam, (TCITEMW *)lparam, msg == TCM_GETITEMA);
    case TCM_SETITEMA:
    case TCM_SETITEMW:
        return set_item(t, (int)wparam, (const TCITEMW *)lparam, msg == TCM_SETITEMA);
    case TCM_DELETEITEM:
        return delete_item(t, (int)wparam);
    case TCM_DELETEALLITEMS:
        while (t->Count)
            delete_item(t, t->Count - 1);
        t->Selected = t->Focus = -1;
        return TRUE;
    case TCM_GETITEMRECT:
        if ((int)wparam < 0 || (int)wparam >= t->Count || !lparam)
            return FALSE;
        layout(t);
        *(RECT *)lparam = t->Items[wparam].Rect;
        return TRUE;
    case TCM_GETCURSEL:
        return t->Selected;
    case TCM_SETCURSEL: {
        int index = (int)wparam, old = t->Selected;
        if (index >= t->Count)
            return -1;
        if (index == old)
            return old;
        t->Selected = index < 0 ? -1 : index;
        t->Focus = t->Selected;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case TCM_GETCURFOCUS:
        return t->Focus >= 0 ? t->Focus : t->Selected;
    case TCM_SETCURFOCUS:
        if ((int)wparam >= 0 && (int)wparam < t->Count) {
            if (t->Style & TCS_BUTTONS)
                t->Focus = (int)wparam;
            else
                notify_change(t, (int)wparam);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case TCM_HITTEST: {
        TCHITTESTINFO *info = (TCHITTESTINFO *)lparam;
        if (!info)
            return -1;
        layout(t);
        return hit_test(t, info->pt, &info->flags);
    }
    case TCM_SETITEMEXTRA:
        if (t->Count || (int)wparam < 0)
            return FALSE;
        t->ItemExtra = (int)wparam;
        return TRUE;
    case TCM_ADJUSTRECT: {
        RECT *r = (RECT *)lparam;
        if (!r)
            return -1;
        int h = item_height(t), edge = 2 * edge_x(t);
        if (wparam) {
            InflateRect(r, edge, edge);
            if (t->Style & TCS_BOTTOM)
                r->bottom += h;
            else
                r->top -= h;
        } else {
            if (t->Style & TCS_BOTTOM)
                r->bottom -= h;
            else
                r->top += h;
            InflateRect(r, -edge, -edge);
        }
        return 0;
    }
    case TCM_SETITEMSIZE: {
        LRESULT old = MAKELONG(t->FixedWidth, t->FixedHeight ? t->FixedHeight : item_height(t));
        int cx = (short)LOWORD(lparam), cy = (short)HIWORD(lparam);
        int icon = 0;
        if (t->Images)
            ImageList_GetIconSize(t->Images, &icon, nullptr);
        if (cx < icon + 2 * edge_x(t))
            cx = icon + 2 * edge_x(t);
        t->FixedWidth = cx;
        t->FixedHeight = cy;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case TCM_REMOVEIMAGE:
        if (t->Images)
            ImageList_Remove(t->Images, (int)wparam);
        for (int k = 0; k < t->Count; k++) {
            if (t->Items[k].Image == (int)wparam)
                t->Items[k].Image = -1;
            else if (t->Items[k].Image > (int)wparam)
                t->Items[k].Image--;
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TCM_SETPADDING:
        t->PadX = (short)LOWORD(lparam);
        t->PadY = (short)HIWORD(lparam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TCM_GETROWCOUNT:
        return t->Count ? 1 : 0;
    case TCM_GETTOOLTIPS:
        return (LRESULT)t->Tooltips;
    case TCM_SETTOOLTIPS:
        t->Tooltips = (HWND)wparam;
        return 0;
    case TCM_SETMINTABWIDTH: {
        int old = min_width(t);
        t->MinWidth = (int)lparam < 0 ? -1 : (int)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case TCM_DESELECTALL:
        for (int k = 0; k < t->Count; k++)
            if (!wparam || k != t->Selected)
                t->Items[k].State &= ~TCIS_BUTTONPRESSED;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TCM_HIGHLIGHTITEM:
        if ((int)wparam < 0 || (int)wparam >= t->Count)
            return FALSE;
        if (LOWORD(lparam))
            t->Items[wparam].State |= TCIS_HIGHLIGHTED;
        else
            t->Items[wparam].State &= ~TCIS_HIGHLIGHTED;
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    case TCM_SETEXTENDEDSTYLE: {
        DWORD old = t->ExStyle;
        t->ExStyle = wparam ? (t->ExStyle & ~(DWORD)wparam) | ((DWORD)lparam & (DWORD)wparam) : (DWORD)lparam;
        return old;
    }
    case TCM_GETEXTENDEDSTYLE:
        return t->ExStyle;
    case CCM_SETUNICODEFORMAT: {
        BOOL old = t->Notify.Unicode;
        t->Notify.Unicode = wparam != 0;
        return old;
    }
    case CCM_GETUNICODEFORMAT:
        return t->Notify.Unicode;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
