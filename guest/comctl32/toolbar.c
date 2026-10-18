/*
 * the toolbar, ToolbarWindow32: a row of buttons with images from image
 * lists and optional text, sending WM_COMMAND when clicked.
 */
#include "comctl32_private.h"

typedef struct TB_ITEM {
    int Bitmap;
    int Command;
    BYTE State, Style;
    DWORD_PTR Data;
    INT_PTR String;
    LPWSTR Text;
    RECT Rect;
} TB_ITEM;

typedef struct TOOLBAR {
    HWND Hwnd;
    CC_NOTIFY Notify;
    HWND Owner;
    DWORD Style, ExStyle;
    TB_ITEM *Items;
    int Count;
    UINT StructSize;
    BOOL StructSet;
    int BitmapCx, BitmapCy;
    int ButtonCx, ButtonCy;
    BOOL FixedButtonSize;
    int PadX, PadY, Indent;
    int MaxTextRows;
    HIMAGELIST Images, Hot, Disabled, OwnImages;
    LPWSTR *Strings;
    int StringCount;
    HFONT Font;
    HTHEME Theme;
    int HotItem, Pressed;
    BOOL PressedInside;
    HWND Tooltips;
} TOOLBAR;

static TOOLBAR *get_data(HWND hwnd)
{
    return (TOOLBAR *)GetWindowLongPtrW(hwnd, 0);
}

static HFONT tb_font(TOOLBAR *t)
{
    return t->Font ? t->Font : CcIconTitleFont();
}

static int command_to_index(TOOLBAR *t, int command)
{
    for (int k = 0; k < t->Count; k++)
        if (t->Items[k].Command == command)
            return k;
    return -1;
}

static LPCWSTR button_text(TOOLBAR *t, TB_ITEM *it)
{
    if (it->Text)
        return it->Text;
    if (it->String >= 0 && it->String < t->StringCount)
        return t->Strings[it->String];
    return nullptr;
}

static int text_extent(TOOLBAR *t, LPCWSTR text, int *cy)
{
    HDC dc = GetDC(t->Hwnd);
    HGDIOBJ old = SelectObject(dc, tb_font(t));
    SIZE size = {};
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    if (text && *text)
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    ReleaseDC(t->Hwnd, dc);
    if (cy)
        *cy = tm.tmHeight;
    return size.cx;
}

/* the button size: the bitmap and padding, with room for text below it, or beside it with TBSTYLE_LIST. */
static void compute_button_size(TOOLBAR *t)
{
    if (t->FixedButtonSize)
        return;
    int cx = t->BitmapCx + t->PadX, cy = t->BitmapCy + t->PadY;
    int widest = 0, text_cy = 0;
    BOOL any_text = FALSE;
    if (t->MaxTextRows)
        for (int k = 0; k < t->Count; k++) {
            LPCWSTR text = button_text(t, &t->Items[k]);
            if (text && *text) {
                any_text = TRUE;
                int w = text_extent(t, text, &text_cy);
                if (w > widest)
                    widest = w;
            }
        }
    if (any_text) {
        if (t->Style & TBSTYLE_LIST) {
            if (text_cy + t->PadY > cy)
                cy = text_cy + t->PadY;
        } else {
            cy += text_cy;
            if (widest + t->PadX > cx)
                cx = widest + t->PadX;
        }
    }
    t->ButtonCx = cx;
    t->ButtonCy = cy;
}

static void layout(TOOLBAR *t)
{
    compute_button_size(t);
    RECT client;
    GetClientRect(t->Hwnd, &client);
    int x = t->Indent, y = 0;
    for (int k = 0; k < t->Count; k++) {
        TB_ITEM *it = &t->Items[k];
        if (it->State & TBSTATE_HIDDEN) {
            SetRectEmpty(&it->Rect);
            continue;
        }
        int w;
        if (it->Style & BTNS_SEP)
            w = it->Bitmap > 0 ? it->Bitmap : 8;
        else if ((it->Style & BTNS_AUTOSIZE) || (t->Style & TBSTYLE_LIST)) {
            LPCWSTR text = button_text(t, it);
            w = t->BitmapCx + t->PadX + (text && *text && t->MaxTextRows ? text_extent(t, text, nullptr) + 4 : 0);
            if (!(it->Style & BTNS_AUTOSIZE) && w < t->ButtonCx)
                w = t->ButtonCx;
        } else {
            w = t->ButtonCx;
        }
        if ((t->Style & TBSTYLE_WRAPABLE) && x + w > client.right && x > t->Indent) {
            x = t->Indent;
            y += t->ButtonCy;
        }
        SetRect(&it->Rect, x, y, x + w, y + t->ButtonCy);
        x += w;
        if ((it->State & TBSTATE_WRAP) && !(t->Style & TBSTYLE_WRAPABLE)) {
            x = t->Indent;
            y += t->ButtonCy;
        }
    }
}

static int rows_height(TOOLBAR *t)
{
    int bottom = t->ButtonCy;
    for (int k = 0; k < t->Count; k++)
        if (t->Items[k].Rect.bottom > bottom)
            bottom = t->Items[k].Rect.bottom;
    return bottom;
}

/* along the top of the parent unless the CCS styles say otherwise. */
static void autosize(TOOLBAR *t)
{
    layout(t);
    HWND parent = GetParent(t->Hwnd);
    if (!parent || (t->Style & CCS_NOPARENTALIGN) || (t->Style & CCS_NORESIZE))
        return;
    RECT pr;
    GetClientRect(parent, &pr);
    int h = rows_height(t) + ((t->Style & CCS_NODIVIDER) ? 0 : 2) + 2;
    int y = (t->Style & 3) == CCS_BOTTOM ? pr.bottom - h : 0;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if ((t->Style & 3) == CCS_NOMOVEY)
        flags |= SWP_NOMOVE;
    SetWindowPos(t->Hwnd, nullptr, 0, y, pr.right, h, flags);
}

static void paint(TOOLBAR *t, HDC dc)
{
    RECT client;
    GetClientRect(t->Hwnd, &client);
    layout(t);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    if ((t->Style & TBSTYLE_TRANSPARENT) || t->Theme)
        DrawThemeParentBackground(t->Hwnd, dc, &client);
    if (!(t->Style & CCS_NODIVIDER) && !t->Theme) {
        RECT d = client;
        DrawEdge(dc, &d, EDGE_ETCHED, BF_TOP);
    }
    HGDIOBJ old = SelectObject(dc, tb_font(t));
    SetBkMode(dc, TRANSPARENT);
    BOOL flat = (t->Style & TBSTYLE_FLAT) != 0;
    for (int k = 0; k < t->Count; k++) {
        TB_ITEM *it = &t->Items[k];
        RECT r = it->Rect;
        if (IsRectEmpty(&r))
            continue;
        if (it->Style & BTNS_SEP) {
            RECT s = r;
            s.left = (r.left + r.right) / 2 - 1;
            if (t->Theme)
                DrawThemeBackground(t->Theme, dc, 5, 1, &s, nullptr);
            else
                DrawEdge(dc, &s, EDGE_ETCHED, BF_LEFT);
            continue;
        }
        BOOL enabled = (it->State & TBSTATE_ENABLED) != 0;
        BOOL pressed = (it->State & TBSTATE_PRESSED) || (k == t->Pressed && t->PressedInside);
        BOOL checked = (it->State & TBSTATE_CHECKED) != 0;
        BOOL hot = k == t->HotItem && enabled;
        if (t->Theme) {
            int state = !enabled ? 4 : pressed ? 3 : checked ? (hot ? 6 : 5) : hot ? 2 : 1;
            if (state != 1)
                DrawThemeBackground(t->Theme, dc, CC_TP_BUTTON, state, &r, nullptr);
        } else if (pressed || checked) {
            DrawEdge(dc, &r, flat ? BDR_SUNKENOUTER : EDGE_SUNKEN, BF_RECT);
        } else if (hot && flat) {
            DrawEdge(dc, &r, BDR_RAISEDINNER, BF_RECT);
        } else if (!flat) {
            DrawEdge(dc, &r, EDGE_RAISED, BF_RECT | BF_SOFT);
        }
        int shift = pressed && !t->Theme ? 1 : 0;
        LPCWSTR text = t->MaxTextRows ? button_text(t, it) : nullptr;
        BOOL list = (t->Style & TBSTYLE_LIST) != 0;
        int ix = list ? r.left + t->PadX / 2 : r.left + (r.right - r.left - t->BitmapCx) / 2;
        int iy = list ? r.top + (r.bottom - r.top - t->BitmapCy) / 2 : r.top + t->PadY / 2;
        HIMAGELIST images = !enabled && t->Disabled ? t->Disabled : hot && t->Hot ? t->Hot : t->Images;
        if (images && it->Bitmap >= 0)
            ImageList_DrawEx(images, it->Bitmap, dc, ix + shift, iy + shift, 0, 0, CLR_NONE, CLR_NONE,
                             ILD_TRANSPARENT | (!enabled && !t->Disabled ? ILD_BLEND50 : 0));
        if (text && *text) {
            RECT tr = r;
            if (list) {
                tr.left = ix + t->BitmapCx + 2;
            } else {
                tr.top = iy + t->BitmapCy + 1;
            }
            OffsetRect(&tr, shift, shift);
            SetTextColor(dc, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
            DrawTextW(dc, text, -1, &tr,
                      (list ? DT_LEFT | DT_VCENTER : DT_CENTER | DT_TOP) | DT_SINGLELINE | DT_END_ELLIPSIS |
                          ((it->Style & BTNS_NOPREFIX) ? DT_NOPREFIX : 0));
        }
    }
    SelectObject(dc, old);
}

/* ------------------------------------------------------------------ buttons */

static BOOL insert_buttons(TOOLBAR *t, int index, int count, const BYTE *buttons, BOOL ansi)
{
    if (count <= 0 || !buttons)
        return FALSE;
    if (index < 0 || index > t->Count)
        index = t->Count;
    TB_ITEM *items = CcReAlloc(t->Items, ((SIZE_T)t->Count + (SIZE_T)count) * sizeof(TB_ITEM));
    if (!items)
        return FALSE;
    t->Items = items;
    memmove(&t->Items[index + count], &t->Items[index], (SIZE_T)(t->Count - index) * sizeof(TB_ITEM));
    UINT size = t->StructSize ? t->StructSize : sizeof(TBBUTTON);
    for (int k = 0; k < count; k++) {
        TBBUTTON b = {};
        memcpy(&b, buttons + (SIZE_T)k * size, size < sizeof(b) ? size : sizeof(b));
        TB_ITEM *it = &t->Items[index + k];
        memset(it, 0, sizeof(*it));
        it->Bitmap = b.iBitmap;
        it->Command = b.idCommand;
        it->State = b.fsState;
        it->Style = b.fsStyle;
        it->Data = b.dwData;
        it->String = -1;
        if (size >= sizeof(TBBUTTON)) {
            if (IS_INTRESOURCE(b.iString) || b.iString == -1)
                it->String = b.iString;
            else
                it->Text = ansi ? CcStrDupAtoW((LPCSTR)b.iString) : CcStrDupW((LPCWSTR)b.iString);
        }
    }
    t->Count += count;
    autosize(t);
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return TRUE;
}

static BOOL delete_button(TOOLBAR *t, int index)
{
    if (index < 0 || index >= t->Count)
        return FALSE;
    CcFree(t->Items[index].Text);
    memmove(&t->Items[index], &t->Items[index + 1], (SIZE_T)(t->Count - index - 1) * sizeof(TB_ITEM));
    t->Count--;
    t->HotItem = -1;
    t->Pressed = -1;
    layout(t);
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return TRUE;
}

static void uncheck_group(TOOLBAR *t, int index)
{
    /* the group is the run of CHECKGROUP buttons around the index. */
    for (int k = index - 1; k >= 0 && (t->Items[k].Style & BTNS_CHECKGROUP) == BTNS_CHECKGROUP; k--)
        t->Items[k].State &= ~TBSTATE_CHECKED;
    for (int k = index + 1; k < t->Count && (t->Items[k].Style & BTNS_CHECKGROUP) == BTNS_CHECKGROUP; k++)
        t->Items[k].State &= ~TBSTATE_CHECKED;
}

static LRESULT set_state_bit(TOOLBAR *t, int command, BYTE bit, BOOL on)
{
    int k = command_to_index(t, command);
    if (k < 0)
        return FALSE;
    TB_ITEM *it = &t->Items[k];
    if (bit == TBSTATE_PRESSED && !(it->State & TBSTATE_ENABLED))
        return FALSE;
    if (on)
        it->State |= bit;
    else
        it->State &= (BYTE)~bit;
    if (bit == TBSTATE_CHECKED && on && (it->Style & BTNS_CHECKGROUP) == BTNS_CHECKGROUP)
        uncheck_group(t, k);
    if (bit == TBSTATE_ENABLED && !on)
        it->State &= (BYTE)~TBSTATE_PRESSED;
    if (bit == TBSTATE_HIDDEN)
        autosize(t);
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return TRUE;
}

static LRESULT get_state_bit(TOOLBAR *t, int command, BYTE bit)
{
    int k = command_to_index(t, command);
    if (k < 0)
        return -1;
    return (t->Items[k].State & bit) != 0;
}

static int hit_test(TOOLBAR *t, POINT pt)
{
    layout(t);
    for (int k = 0; k < t->Count; k++)
        if (PtInRect(&t->Items[k].Rect, pt))
            return k;
    return -1;
}

static int add_bitmap(TOOLBAR *t, int count, const TBADDBITMAP *ab)
{
    if (!ab)
        return -1;
    if (!t->Images) {
        t->OwnImages = ImageList_Create(t->BitmapCx, t->BitmapCy, ILC_COLOR32 | ILC_MASK, count > 0 ? count : 4, 4);
        t->Images = t->OwnImages;
    }
    if (!t->Images)
        return -1;
    int first = ImageList_GetImageCount(t->Images);
    if (ab->hInst == HINST_COMMCTRL)
        return first;
    HBITMAP bitmap = ab->hInst ? LoadImageW(ab->hInst, MAKEINTRESOURCEW(ab->nID), IMAGE_BITMAP, 0, 0,
                                             LR_CREATEDIBSECTION)
                               : (HBITMAP)ab->nID;
    if (!bitmap)
        return -1;
    int r = ImageList_AddMasked(t->Images, bitmap, RGB(192, 192, 192));
    if (ab->hInst)
        DeleteObject(bitmap);
    return r < 0 ? -1 : first;
}

static void click(TOOLBAR *t, int k, POINT pt)
{
    TB_ITEM *it = &t->Items[k];
    NMTOOLBARW nm = {};
    nm.iItem = it->Command;
    CcNotify(&t->Notify, TBN_ENDDRAG, &nm.hdr);
    if (it->Style & BTNS_CHECK) {
        if ((it->Style & BTNS_CHECKGROUP) == BTNS_CHECKGROUP) {
            it->State |= TBSTATE_CHECKED;
            uncheck_group(t, k);
        } else {
            it->State ^= TBSTATE_CHECKED;
        }
    }
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    SendMessageW(t->Owner, WM_COMMAND, MAKEWPARAM(it->Command, 0), (LPARAM)t->Hwnd);
    CcNotifyMouse(&t->Notify, NM_CLICK, (DWORD_PTR)(LONG_PTR)it->Command, it->Data, pt);
}

LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TOOLBAR *t = get_data(hwnd);
    if (!t && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    BOOL ansi = FALSE;
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        t = CcAlloc(sizeof(*t));
        if (!t)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)t);
        t->Hwnd = hwnd;
        t->Style = (DWORD)cs->style;
        if (t->Style & TBSTYLE_FLAT) {
            t->Style |= TBSTYLE_TRANSPARENT;
            SetWindowLongW(hwnd, GWL_STYLE, (LONG)t->Style);
        }
        t->Owner = cs->hwndParent;
        t->BitmapCx = 16;
        t->BitmapCy = 16;
        t->PadX = 7;
        t->PadY = 6;
        t->MaxTextRows = 1;
        t->HotItem = t->Pressed = -1;
        CcNotifyInit(&t->Notify, hwnd, cs->hwndParent);
        compute_button_size(t);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        t->Theme = OpenThemeData(hwnd, L"Toolbar");
        autosize(t);
        return 0;
    case WM_DESTROY:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        for (int k = 0; k < t->Count; k++)
            CcFree(t->Items[k].Text);
        for (int k = 0; k < t->StringCount; k++)
            CcFree(t->Strings[k]);
        CcFree(t->Strings);
        CcFree(t->Items);
        if (t->OwnImages)
            ImageList_Destroy(t->OwnImages);
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
    case WM_SIZE:
        layout(t);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETFONT:
        t->Font = (HFONT)wparam;
        autosize(t);
        if (lparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)t->Font;
    case WM_LBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int k = hit_test(t, pt);
        if (k >= 0 && !(t->Items[k].Style & BTNS_SEP) && (t->Items[k].State & TBSTATE_ENABLED)) {
            NMTOOLBARW nm = {};
            nm.iItem = t->Items[k].Command;
            CcNotify(&t->Notify, TBN_BEGINDRAG, &nm.hdr);
            t->Pressed = k;
            t->PressedInside = TRUE;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
        } else {
            CcNotifyMouse(&t->Notify, NM_CLICK, (DWORD_PTR)-1, 0, pt);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        if (t->Pressed >= 0) {
            BOOL inside = PtInRect(&t->Items[t->Pressed].Rect, pt);
            if (inside != t->PressedInside) {
                t->PressedInside = inside;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        int k = hit_test(t, pt);
        if (k >= 0 && (t->Items[k].Style & BTNS_SEP))
            k = -1;
        if (k != t->HotItem) {
            t->HotItem = k;
            InvalidateRect(hwnd, nullptr, TRUE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (t->HotItem >= 0) {
            t->HotItem = -1;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_LBUTTONUP: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int k = t->Pressed;
        BOOL inside = t->PressedInside;
        t->Pressed = -1;
        t->PressedInside = FALSE;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        if (k >= 0) {
            if (inside)
                click(t, k, pt);
            else {
                NMTOOLBARW nm = {};
                nm.iItem = t->Items[k].Command;
                CcNotify(&t->Notify, TBN_ENDDRAG, &nm.hdr);
            }
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int k = hit_test(t, pt);
        if (CcNotifyMouse(&t->Notify, NM_RCLICK, k >= 0 ? (DWORD_PTR)(LONG_PTR)t->Items[k].Command : (DWORD_PTR)-1,
                          k >= 0 ? t->Items[k].Data : 0, pt))
            return 0;
        break;
    }
    case WM_THEMECHANGED:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = OpenThemeData(hwnd, L"Toolbar");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            t->Style = ((STYLESTRUCT *)lparam)->styleNew;
            autosize(t);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&t->Notify, (HWND)wparam, lparam);
    case TB_BUTTONSTRUCTSIZE:
        if (!t->Count)
            t->StructSize = (UINT)wparam;
        t->StructSet = TRUE;
        return 0;
    case TB_SETIMAGELIST: {
        HIMAGELIST old = t->Images;
        t->Images = (HIMAGELIST)lparam;
        if (!wparam && t->Images)
            ImageList_GetIconSize(t->Images, &t->BitmapCx, &t->BitmapCy);
        if (!t->StructSet)
            t->StructSize = 32;
        autosize(t);
        InvalidateRect(hwnd, nullptr, TRUE);
        return (LRESULT)old;
    }
    case TB_GETIMAGELIST:
        return (LRESULT)t->Images;
    case TB_SETHOTIMAGELIST:
    case TB_SETDISABLEDIMAGELIST: {
        HIMAGELIST *slot = msg == TB_SETHOTIMAGELIST ? &t->Hot : &t->Disabled;
        HIMAGELIST old = *slot;
        *slot = (HIMAGELIST)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return (LRESULT)old;
    }
    case TB_GETHOTIMAGELIST:
        return (LRESULT)t->Hot;
    case TB_GETDISABLEDIMAGELIST:
        return (LRESULT)t->Disabled;
    case TB_ENABLEBUTTON:
        return set_state_bit(t, (int)wparam, TBSTATE_ENABLED, LOWORD(lparam));
    case TB_CHECKBUTTON:
        return set_state_bit(t, (int)wparam, TBSTATE_CHECKED, LOWORD(lparam));
    case TB_PRESSBUTTON:
        return set_state_bit(t, (int)wparam, TBSTATE_PRESSED, LOWORD(lparam));
    case TB_HIDEBUTTON:
        return set_state_bit(t, (int)wparam, TBSTATE_HIDDEN, LOWORD(lparam));
    case TB_INDETERMINATE:
        return set_state_bit(t, (int)wparam, TBSTATE_INDETERMINATE, LOWORD(lparam));
    case TB_MARKBUTTON:
        return set_state_bit(t, (int)wparam, TBSTATE_MARKED, LOWORD(lparam));
    case TB_ISBUTTONENABLED:
        return get_state_bit(t, (int)wparam, TBSTATE_ENABLED);
    case TB_ISBUTTONCHECKED:
        return get_state_bit(t, (int)wparam, TBSTATE_CHECKED);
    case TB_ISBUTTONPRESSED:
        return get_state_bit(t, (int)wparam, TBSTATE_PRESSED);
    case TB_ISBUTTONHIDDEN:
        return get_state_bit(t, (int)wparam, TBSTATE_HIDDEN);
    case TB_ISBUTTONINDETERMINATE:
        return get_state_bit(t, (int)wparam, TBSTATE_INDETERMINATE);
    case TB_ISBUTTONHIGHLIGHTED:
        return get_state_bit(t, (int)wparam, TBSTATE_MARKED);
    case TB_SETSTATE: {
        int k = command_to_index(t, (int)wparam);
        if (k < 0)
            return FALSE;
        t->Items[k].State = (BYTE)LOWORD(lparam);
        autosize(t);
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    }
    case TB_GETSTATE: {
        int k = command_to_index(t, (int)wparam);
        return k < 0 ? -1 : t->Items[k].State;
    }
    case TB_ADDBITMAP:
        return add_bitmap(t, (int)wparam, (const TBADDBITMAP *)lparam);
    case TB_ADDBUTTONSA:
        ansi = TRUE;
        [[fallthrough]];
    case TB_ADDBUTTONSW:
        return insert_buttons(t, -1, (int)wparam, (const BYTE *)lparam, ansi);
    case TB_INSERTBUTTONA:
        ansi = TRUE;
        [[fallthrough]];
    case TB_INSERTBUTTONW:
        return insert_buttons(t, (int)wparam, 1, (const BYTE *)lparam, ansi);
    case TB_DELETEBUTTON:
        return delete_button(t, (int)wparam);
    case TB_GETBUTTON: {
        if ((int)wparam < 0 || (int)wparam >= t->Count || !lparam)
            return FALSE;
        TB_ITEM *it = &t->Items[wparam];
        TBBUTTON *b = (TBBUTTON *)lparam;
        b->iBitmap = it->Bitmap;
        b->idCommand = it->Command;
        b->fsState = it->State;
        b->fsStyle = it->Style;
        b->dwData = it->Data;
        b->iString = it->Text ? (INT_PTR)it->Text : it->String;
        return TRUE;
    }
    case TB_BUTTONCOUNT:
        return t->Count;
    case TB_COMMANDTOINDEX:
        return command_to_index(t, (int)wparam);
    case TB_GETITEMRECT:
        if (!lparam || (int)wparam < 0 || (int)wparam >= t->Count)
            return FALSE;
        layout(t);
        *(RECT *)lparam = t->Items[wparam].Rect;
        return TRUE;
    case TB_GETRECT: {
        int k = command_to_index(t, (int)wparam);
        if (k < 0 || !lparam)
            return FALSE;
        layout(t);
        *(RECT *)lparam = t->Items[k].Rect;
        return TRUE;
    }
    case TB_SETBUTTONSIZE:
        t->ButtonCx = LOWORD(lparam) ? LOWORD(lparam) : t->ButtonCx;
        t->ButtonCy = HIWORD(lparam) ? HIWORD(lparam) : t->ButtonCy;
        t->FixedButtonSize = TRUE;
        autosize(t);
        return TRUE;
    case TB_SETBITMAPSIZE: {
        int cx = (short)LOWORD(lparam), cy = (short)HIWORD(lparam);
        if (cx != -1)
            t->BitmapCx = cx ? cx : 1;
        if (cy != -1)
            t->BitmapCy = cy ? cy : 1;
        if (t->OwnImages && ImageList_GetImageCount(t->OwnImages) == 0)
            ImageList_SetIconSize(t->OwnImages, t->BitmapCx, t->BitmapCy);
        autosize(t);
        return TRUE;
    }
    case TB_GETBUTTONSIZE:
        compute_button_size(t);
        return MAKELONG(t->ButtonCx, t->ButtonCy);
    case TB_AUTOSIZE:
        autosize(t);
        return 0;
    case TB_SETPARENT: {
        HWND old = t->Owner;
        t->Owner = (HWND)wparam;
        t->Notify.Parent = (HWND)wparam;
        return (LRESULT)old;
    }
    case TB_SETEXTENDEDSTYLE: {
        DWORD old = t->ExStyle;
        t->ExStyle = wparam ? (t->ExStyle & ~(DWORD)wparam) | ((DWORD)lparam & (DWORD)wparam) : (DWORD)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case TB_GETEXTENDEDSTYLE:
        return t->ExStyle;
    case TB_SETSTYLE:
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)lparam);
        return 0;
    case TB_GETSTYLE:
        return t->Style;
    case TB_SETMAXTEXTROWS:
        if ((int)wparam == t->MaxTextRows)
            return TRUE;
        t->MaxTextRows = (int)wparam;
        autosize(t);
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    case TB_GETTEXTROWS:
        return t->MaxTextRows;
    case TB_SETINDENT:
        t->Indent = (int)wparam;
        autosize(t);
        return TRUE;
    case TB_GETPADDING:
        return MAKELONG(t->PadX, t->PadY);
    case TB_SETPADDING: {
        LRESULT old = MAKELONG(t->PadX, t->PadY);
        t->PadX = LOWORD(lparam);
        t->PadY = HIWORD(lparam);
        autosize(t);
        return old;
    }
    case TB_GETROWS:
        layout(t);
        return t->ButtonCy ? rows_height(t) / t->ButtonCy : 1;
    case TB_GETHOTITEM:
        return t->HotItem;
    case TB_SETHOTITEM: {
        int old = t->HotItem;
        t->HotItem = (int)wparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case TB_HITTEST: {
        POINT *pt = (POINT *)lparam;
        if (!pt)
            return -1;
        int k = hit_test(t, *pt);
        return k >= 0 ? k : -1;
    }
    case TB_ADDSTRINGA:
    case TB_ADDSTRINGW: {
        int first = t->StringCount;
        if (!lparam)
            return -1;
        if (IS_INTRESOURCE(lparam)) {
            WCHAR buffer[512];
            int n = LoadStringW((HINSTANCE)wparam, (UINT)lparam, buffer, 512);
            if (n <= 0)
                return -1;
            /* the first character separates the strings of a resource. */
            WCHAR sep = buffer[0];
            LPWSTR p = buffer + 1;
            while (*p) {
                LPWSTR end = p;
                while (*end && *end != sep)
                    end++;
                WCHAR saved = *end;
                *end = 0;
                LPWSTR *strings = CcReAlloc(t->Strings, ((SIZE_T)t->StringCount + 1) * sizeof(LPWSTR));
                if (!strings)
                    break;
                t->Strings = strings;
                t->Strings[t->StringCount++] = CcStrDupW(p);
                if (!saved)
                    break;
                p = end + 1;
            }
        } else if (msg == TB_ADDSTRINGW) {
            for (LPCWSTR p = (LPCWSTR)lparam; *p; p += wcslen(p) + 1) {
                LPWSTR *strings = CcReAlloc(t->Strings, ((SIZE_T)t->StringCount + 1) * sizeof(LPWSTR));
                if (!strings)
                    break;
                t->Strings = strings;
                t->Strings[t->StringCount++] = CcStrDupW(p);
            }
        } else {
            for (LPCSTR p = (LPCSTR)lparam; *p; p += strlen(p) + 1) {
                LPWSTR *strings = CcReAlloc(t->Strings, ((SIZE_T)t->StringCount + 1) * sizeof(LPWSTR));
                if (!strings)
                    break;
                t->Strings = strings;
                t->Strings[t->StringCount++] = CcStrDupAtoW(p);
            }
        }
        autosize(t);
        return first;
    }
    case TB_GETBUTTONTEXTA:
    case TB_GETBUTTONTEXTW: {
        int k = command_to_index(t, (int)wparam);
        if (k < 0)
            return -1;
        LPCWSTR text = button_text(t, &t->Items[k]);
        if (!text)
            return -1;
        if (msg == TB_GETBUTTONTEXTA) {
            int n = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr) - 1;
            if (lparam)
                WideCharToMultiByte(CP_ACP, 0, text, -1, (LPSTR)lparam, n + 1, nullptr, nullptr);
            return n;
        }
        if (lparam)
            lstrcpyW((LPWSTR)lparam, text);
        return lstrlenW(text);
    }
    case TB_SETCMDID:
        if ((int)wparam < 0 || (int)wparam >= t->Count)
            return FALSE;
        t->Items[wparam].Command = (int)lparam;
        return TRUE;
    case TB_CHANGEBITMAP: {
        int k = command_to_index(t, (int)wparam);
        if (k < 0)
            return FALSE;
        t->Items[k].Bitmap = (int)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    }
    case TB_GETBITMAP: {
        int k = command_to_index(t, (int)wparam);
        return k < 0 ? 0 : t->Items[k].Bitmap;
    }
    case TB_GETMAXSIZE: {
        SIZE *size = (SIZE *)lparam;
        if (!size)
            return FALSE;
        layout(t);
        size->cx = 0;
        size->cy = rows_height(t);
        for (int k = 0; k < t->Count; k++)
            if (t->Items[k].Rect.right > size->cx)
                size->cx = t->Items[k].Rect.right;
        return TRUE;
    }
    case TB_SETTOOLTIPS:
        t->Tooltips = (HWND)wparam;
        return 0;
    case TB_GETTOOLTIPS:
        return (LRESULT)t->Tooltips;
    case TB_LOADIMAGES:
    case TB_CUSTOMIZE:
    case TB_SETROWS:
    case TB_REPLACEBITMAP:
    case TB_SETBUTTONWIDTH:
    case TB_SETDRAWTEXTFLAGS:
        return 0;
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

WINCOMMCTRLAPI HWND WINAPI CreateToolbarEx(HWND hwnd, DWORD style, UINT id, int bitmaps, HINSTANCE bm_inst,
                                           UINT_PTR bm_id, LPCTBBUTTON buttons, int count, int dx_button,
                                           int dy_button, int dx_bitmap, int dy_bitmap, UINT struct_size)
{
    HWND tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr, style | WS_CHILD, 0, 0, 100, 30, hwnd,
                              (HMENU)(UINT_PTR)id, Comctl32Instance, nullptr);
    if (!tb)
        return nullptr;
    SendMessageW(tb, TB_BUTTONSTRUCTSIZE, struct_size, 0);
    if (dx_bitmap || dy_bitmap)
        SendMessageW(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(dx_bitmap ? dx_bitmap : 16, dy_bitmap ? dy_bitmap : 15));
    if (dx_button || dy_button)
        SendMessageW(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(dx_button, dy_button));
    if (bitmaps > 0) {
        TBADDBITMAP ab = {bm_inst, bm_id};
        SendMessageW(tb, TB_ADDBITMAP, (WPARAM)bitmaps, (LPARAM)&ab);
    }
    if (buttons && count > 0)
        SendMessageW(tb, TB_ADDBUTTONSW, (WPARAM)count, (LPARAM)buttons);
    return tb;
}

/* the first toolbar api: buttons without data or strings. */
WINCOMMCTRLAPI HWND WINAPI CreateToolbar(HWND hwnd, DWORD style, UINT id, int bitmaps, HINSTANCE bm_inst,
                                         UINT_PTR bm_id, LPCTBBUTTON buttons, int count)
{
    return CreateToolbarEx(hwnd, style, id, bitmaps, bm_inst, bm_id, buttons, count, 0, 0, 0, 0,
                           (UINT)(2 * sizeof(int) + 4));
}
