#include "comctl32_private.h"
#include <mime/ntuser.h>

#define FIELD_HOT 1
#define FIELD_KNOWN 2

static WNDPROC g_edit_base, g_list_base, g_combo_base;

/*
 * the class is the one of user32 with this procedure in front. user32 registers its own control under the plain name
 * before it asks for this one, and looks a name up through the activation context, so win32k is asked for it.
 */
WNDPROC CcRegisterOverUser(LPCWSTR name, WNDPROC proc)
{
    UNICODE_STRING text;
    RtlInitUnicodeString(&text, name);
    WNDCLASSEXW base = {sizeof(base)};
    LPWSTR menu = nullptr;
    if (!NtUserGetClassInfoEx(nullptr, &text, &base, &menu, FALSE) || !base.lpfnWndProc || base.lpfnWndProc == proc)
        return nullptr;
    WNDCLASSW wc = {};
    wc.style = base.style | CS_GLOBALCLASS;
    wc.lpfnWndProc = proc;
    wc.cbClsExtra = base.cbClsExtra;
    wc.cbWndExtra = base.cbWndExtra;
    wc.hInstance = Comctl32Instance;
    wc.hCursor = base.hCursor;
    wc.hbrBackground = base.hbrBackground;
    wc.lpszClassName = name;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    return base.lpfnWndProc;
}

static void redraw_frame(HWND hwnd)
{
    if ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) && IsAppThemed())
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

/* the mouse entering and leaving a field, which its border shows. returns whether the state changed. */
static BOOL track_hot(HWND hwnd, UINT msg)
{
    UINT flags = CcWindowFlags(hwnd);
    if (msg == WM_MOUSEMOVE && !(flags & FIELD_HOT)) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        CcSetWindowFlags(hwnd, flags | FIELD_HOT | FIELD_KNOWN);
        return TRUE;
    }
    if (msg == WM_MOUSELEAVE && (flags & FIELD_HOT)) {
        CcSetWindowFlags(hwnd, FIELD_KNOWN);
        return TRUE;
    }
    return FALSE;
}

static BOOL is_hot(HWND hwnd)
{
    return (CcWindowFlags(hwnd) & FIELD_HOT) != 0;
}

static LRESULT field_proc(WNDPROC base, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LPCWSTR class_name, int part,
                          int state)
{
    switch (msg) {
    case WM_NCPAINT: {
        LRESULT r = CallWindowProcW(base, hwnd, msg, wparam, lparam);
        CcDrawThemedEdgePart(hwnd, CcClassTheme(hwnd, class_name), part, state);
        return r;
    }
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
        if (track_hot(hwnd, msg))
            redraw_frame(hwnd);
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE: {
        LRESULT r = CallWindowProcW(base, hwnd, msg, wparam, lparam);
        redraw_frame(hwnd);
        return r;
    }
    case WM_NCDESTROY:
        CcSetWindowFlags(hwnd, 0);
        break;
    }
    return CallWindowProcW(base, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    /* EP_EDITBORDER_NOSCROLL, then the ones with a horizontal, a vertical and both scroll bars. */
    int part = CC_EP_EDITBORDER_NOSCROLL + ((style & WS_HSCROLL) ? 1 : 0) + ((style & WS_VSCROLL) ? 2 : 0);
    int state = (style & WS_DISABLED) ? 4 : GetFocus() == hwnd ? 3 : is_hot(hwnd) ? 2 : 1;
    return field_proc(g_edit_base, hwnd, msg, wparam, lparam, L"Edit", part, state);
}

static LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    int part = (style & WS_VSCROLL) && (style & WS_HSCROLL) ? CC_LBCP_BORDER_HVSCROLL
               : (style & WS_VSCROLL)                       ? CC_LBCP_BORDER_VSCROLL
               : (style & WS_HSCROLL)                       ? CC_LBCP_BORDER_HSCROLL
                                                            : CC_LBCP_BORDER_NOSCROLL;
    int state = (style & WS_DISABLED) ? 4 : is_hot(hwnd) ? 3 : GetFocus() == hwnd ? 2 : 1;
    return field_proc(g_list_base, hwnd, msg, wparam, lparam, L"Listbox", part, state);
}

static LRESULT combo_base(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return CallWindowProcW(g_combo_base, hwnd, msg, wparam, lparam);
}

static HBRUSH combo_brush(HWND hwnd, HDC dc, UINT msg)
{
    HWND parent = GetParent(hwnd);
    HBRUSH brush = parent ? (HBRUSH)SendMessageW(parent, msg, (WPARAM)dc, (LPARAM)hwnd) : nullptr;
    if (!brush)
        brush = (HBRUSH)DefWindowProcW(parent ? parent : hwnd, msg, (WPARAM)dc, (LPARAM)hwnd);
    return brush;
}

/* the selected item in the field of a drop down list, which the control draws itself. */
static void combo_paint_item(HWND hwnd, HDC dc, HTHEME theme, const COMBOBOXINFO *info, int state, BOOL focused)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    BOOL enabled = !(style & WS_DISABLED);
    BOOL dropped = combo_base(hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0;
    BOOL cues_hidden = (SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) != 0;
    int sel = (int)combo_base(hwnd, CB_GETCURSEL, 0, 0);
    HFONT font = (HFONT)combo_base(hwnd, WM_GETFONT, 0, 0);
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    COLORREF color = GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
    GetThemeColor(theme, CC_CP_READONLY, state, TMT_TEXTCOLOR, &color);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    if (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
        if (sel >= 0) {
            DRAWITEMSTRUCT dis = {};
            dis.CtlType = ODT_COMBOBOX;
            dis.CtlID = (UINT)GetDlgCtrlID(hwnd);
            dis.itemID = (UINT)sel;
            dis.itemAction = ODA_DRAWENTIRE;
            dis.itemState = ODS_COMBOBOXEDIT | (enabled ? 0 : ODS_DISABLED) | (focused && !dropped ? ODS_FOCUS : 0) |
                            (cues_hidden ? ODS_NOFOCUSRECT : 0);
            dis.hwndItem = hwnd;
            dis.hDC = dc;
            dis.rcItem = info->rcItem;
            dis.itemData = (ULONG_PTR)combo_base(hwnd, CB_GETITEMDATA, (WPARAM)sel, 0);
            SendMessageW(GetParent(hwnd), WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
        }
    } else {
        LRESULT length = sel >= 0 ? combo_base(hwnd, CB_GETLBTEXTLEN, (WPARAM)sel, 0) : CB_ERR;
        LPWSTR text = length > 0 ? CcAlloc(((SIZE_T)length + 1) * sizeof(WCHAR)) : nullptr;
        if (text && combo_base(hwnd, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)text) > 0)
            ExtTextOutW(dc, info->rcItem.left + 1, info->rcItem.top + 1, ETO_CLIPPED, &info->rcItem, text,
                        (UINT)wcslen(text), nullptr);
        CcFree(text);
        if (focused && !dropped && !cues_hidden)
            DrawFocusRect(dc, &info->rcItem);
    }
    if (old)
        SelectObject(dc, old);
}

static BOOL combo_paint(HWND hwnd, HDC dc)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    UINT type = style & 3;
    HTHEME theme = CcClassTheme(hwnd, L"Combobox");
    COMBOBOXINFO info = {sizeof(info)};
    if (!theme || type == CBS_SIMPLE || !combo_base(hwnd, CB_GETCOMBOBOXINFO, 0, (LPARAM)&info))
        return FALSE;
    RECT client;
    GetClientRect(hwnd, &client);
    BOOL enabled = !(style & WS_DISABLED);
    BOOL pressed = info.stateButton == STATE_SYSTEM_PRESSED || combo_base(hwnd, CB_GETDROPPEDSTATE, 0, 0);
    HWND focus = GetFocus();
    BOOL focused = focus && (focus == hwnd || focus == info.hwndItem);
    BOOL hot = is_hot(hwnd);
    int saved = SaveDC(dc);
    if (type == CBS_DROPDOWNLIST) {
        int state = !enabled ? 4 : pressed ? 3 : hot ? 2 : 1;
        DrawThemeParentBackground(hwnd, dc, &client);
        DrawThemeBackground(theme, dc, CC_CP_READONLY, state, &client, nullptr);
        DrawThemeBackground(theme, dc, CC_CP_DROPDOWNBUTTONRIGHT, enabled ? 1 : 4, &info.rcButton, nullptr);
        combo_paint_item(hwnd, dc, theme, &info, state, focused);
    } else {
        if (info.hwndItem) {
            RECT edit;
            GetWindowRect(info.hwndItem, &edit);
            MapWindowPoints(nullptr, hwnd, (POINT *)&edit, 2);
            ExcludeClipRect(dc, edit.left, edit.top, edit.right, edit.bottom);
        }
        FillRect(dc, &client, combo_brush(hwnd, dc, enabled ? WM_CTLCOLOREDIT : WM_CTLCOLORSTATIC));
        DrawThemeBackground(theme, dc, CC_CP_BORDER, !enabled ? 4 : focused ? 3 : hot ? 2 : 1, &client, nullptr);
        DrawThemeBackground(theme, dc, CC_CP_DROPDOWNBUTTONRIGHT, !enabled ? 4 : pressed ? 3 : hot ? 2 : 1,
                            &info.rcButton, nullptr);
    }
    RestoreDC(dc, saved);
    return TRUE;
}

/*
 * the combo box of user32 draws its field and its button from more places than WM_PAINT, with a dc of its own: when
 * the selection, the focus or the button changes. the field is drawn again in the visual style after each of them.
 */
static BOOL combo_draws_directly(UINT msg)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_CHAR:
    case WM_MOUSEWHEEL:
    case WM_COMMAND:
    case WM_SETTEXT:
    case WM_SETFONT:
    case CB_SETCURSEL:
    case CB_SELECTSTRING:
    case CB_SHOWDROPDOWN:
    case CB_RESETCONTENT:
    case CB_DELETESTRING:
    case CB_INSERTSTRING:
    case CB_ADDSTRING:
        return TRUE;
    }
    return FALSE;
}

static LRESULT CALLBACK ComboBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        if ((GetWindowLongW(hwnd, GWL_STYLE) & 3) == CBS_SIMPLE || !CcClassTheme(hwnd, L"Combobox"))
            break;
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc)
            combo_paint(hwnd, dc);
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
        if (track_hot(hwnd, msg))
            InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        CcSetWindowFlags(hwnd, 0);
        break;
    }
    LRESULT r = combo_base(hwnd, msg, wparam, lparam);
    if (combo_draws_directly(msg) && IsWindow(hwnd) && IsWindowVisible(hwnd) &&
        (GetWindowLongW(hwnd, GWL_STYLE) & 3) != CBS_SIMPLE && CcClassTheme(hwnd, L"Combobox"))
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOCHILDREN);
    return r;
}

BOOL RegisterFieldClass(LPCWSTR name)
{
    static const struct {
        LPCWSTR Name;
        WNDPROC Proc;
        WNDPROC *Base;
    } fields[] = {
        {L"Edit", EditProc, &g_edit_base},
        {L"ListBox", ListBoxProc, &g_list_base},
        {L"ComboBox", ComboBoxProc, &g_combo_base},
    };
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(fields); i++) {
        if (_wcsicmp(name, fields[i].Name))
            continue;
        if (*fields[i].Base)
            return TRUE;
        WNDPROC base = CcRegisterOverUser(fields[i].Name, fields[i].Proc);
        if (base)
            *fields[i].Base = base;
        return base != nullptr;
    }
    return FALSE;
}
