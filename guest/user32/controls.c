/*
 * the Edit, ListBox, ComboBox and ScrollBar controls. only what simple
 * dialogs need for now: an edit control that shows and accepts text.
 */
#include "user32_private.h"

typedef struct EDIT {
    HFONT Font;
    int Caret;
} EDIT;

static void edit_paint(HWND hwnd, EDIT *e, HDC dc)
{
    RECT r;
    GetClientRect(hwnd, &r);
    HWND parent = GetParent(hwnd);
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    UINT msg = (style & ES_READONLY) || !IsWindowEnabled(hwnd) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT;
    HBRUSH brush = parent ? (HBRUSH)SendMessageW(parent, msg, (WPARAM)dc, (LPARAM)hwnd) : nullptr;
    if (!brush)
        brush = GetSysColorBrush(msg == WM_CTLCOLOREDIT ? COLOR_WINDOW : COLOR_3DFACE);
    FillRect(dc, &r, brush);
    int n = GetWindowTextLengthW(hwnd);
    LPWSTR text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return;
    GetWindowTextW(hwnd, text, n + 1);
    if (style & ES_PASSWORD)
        for (int i = 0; i < n; i++)
            text[i] = 0x25CF;
    HGDIOBJ old = SelectObject(dc, e->Font ? e->Font : GetStockObject(SYSTEM_FONT));
    SetBkMode(dc, TRANSPARENT);
    if (!IsWindowEnabled(hwnd))
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    InflateRect(&r, -1, 0);
    UINT flags = DT_NOPREFIX | DT_EXPANDTABS | ((style & ES_MULTILINE) ? DT_WORDBREAK | DT_EDITCONTROL : DT_SINGLELINE);
    if (style & ES_CENTER)
        flags |= DT_CENTER;
    else if (style & ES_RIGHT)
        flags |= DT_RIGHT;
    DrawTextW(dc, text, n, &r, flags);
    SelectObject(dc, old);
    UserFree(text);
}

static void edit_insert(HWND hwnd, LPCWSTR insert, int count, BOOL backspace)
{
    int n = GetWindowTextLengthW(hwnd);
    LPWSTR text = UserAlloc(((SIZE_T)n + (SIZE_T)count + 1) * sizeof(WCHAR));
    if (!text)
        return;
    GetWindowTextW(hwnd, text, n + 1);
    if (backspace) {
        if (n > 0)
            text[n - 1] = 0;
    } else {
        memcpy(text + n, insert, (SIZE_T)count * sizeof(WCHAR));
        text[n + count] = 0;
    }
    DefWindowProcW(hwnd, WM_SETTEXT, 0, (LPARAM)text);
    UserFree(text);
    InvalidateRect(hwnd, nullptr, TRUE);
    HWND parent = GetParent(hwnd);
    if (parent)
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_CHANGE), (LPARAM)hwnd);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    EDIT *e = (EDIT *)GetWindowLongPtrW(hwnd, 0);
    if (!e && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE:
        e = UserAlloc(sizeof(EDIT));
        if (!e)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)e);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, 0, 0);
        UserFree(e);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc)
            edit_paint(hwnd, e, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFONT:
        e->Font = (HFONT)wparam;
        if (lparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)e->Font;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wparam, lparam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return r;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS |
               ((GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) ? DLGC_WANTALLKEYS : 0);
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
    case WM_CHAR: {
        if (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY)
            return 0;
        WCHAR c = (WCHAR)wparam;
        if (c == L'\b')
            edit_insert(hwnd, nullptr, 0, TRUE);
        else if (c >= L' ' || c == L'\t')
            edit_insert(hwnd, &c, 1, FALSE);
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        HWND parent = GetParent(hwnd);
        if (parent)
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), msg == WM_SETFOCUS ? EN_SETFOCUS : EN_KILLFOCUS),
                         (LPARAM)hwnd);
        return 0;
    }
    case EM_SETSEL:
    case EM_SETLIMITTEXT:
    case EM_SETREADONLY:
        if (msg == EM_SETREADONLY) {
            DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
            SetWindowLongW(hwnd, GWL_STYLE, (LONG)(wparam ? style | ES_READONLY : style & ~(DWORD)ES_READONLY));
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 1;
    case EM_GETSEL:
        return MAKELONG(GetWindowTextLengthW(hwnd), GetWindowTextLengthW(hwnd));
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_GETDLGCODE)
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    if (msg >= LB_ADDSTRING && msg <= LB_MSGMAX)
        return LB_ERR;
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* the placeholder combo box has no list; the button is the arrow column at the right. */
static BOOL combo_info(HWND hwnd, COMBOBOXINFO *info)
{
    if (!info || info->cbSize != sizeof(COMBOBOXINFO))
        return FALSE;
    RECT r;
    GetClientRect(hwnd, &r);
    int button = GetSystemMetricsForDpi(SM_CXVSCROLL, UserWindowDpi(hwnd));
    LONG type = GetWindowLongW(hwnd, GWL_STYLE) & 3;
    info->rcButton = (RECT){r.right - button, r.top, r.right, r.bottom};
    info->rcItem = (RECT){r.left + 3, r.top + 3, r.right - button - 1, r.bottom - 3};
    info->stateButton = 0;
    if (type == CBS_SIMPLE) {
        info->rcButton = (RECT){0, 0, 0, 0};
        info->rcItem.right = r.right - 3;
        info->stateButton = STATE_SYSTEM_INVISIBLE;
    }
    info->hwndCombo = hwnd;
    info->hwndItem = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
    if (!info->hwndItem)
        info->hwndItem = hwnd;
    info->hwndList = nullptr;
    return TRUE;
}

LRESULT CALLBACK ComboBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == CB_GETCOMBOBOXINFO)
        return combo_info(hwnd, (COMBOBOXINFO *)lparam);
    if (msg >= CB_GETEDITSEL && msg <= CB_MSGMAX)
        return CB_ERR;
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

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
    if (!SendMessageW(hwnd, CB_GETCOMBOBOXINFO, 0, (LPARAM)info)) {
        SetLastError(ERROR_INVALID_COMBOBOX_MESSAGE);
        return FALSE;
    }
    return TRUE;
}

/* the arrows, the track and a thumb sized by the page. */
static void scrollbar_paint(HWND hwnd, HDC dc)
{
    RECT r;
    GetClientRect(hwnd, &r);
    BOOL vert = (GetWindowLongW(hwnd, GWL_STYLE) & SBS_VERT) != 0;
    int len = vert ? r.bottom - r.top : r.right - r.left;
    int thick = vert ? r.right - r.left : r.bottom - r.top;
    int arrow = thick * 2 <= len ? thick : len / 2;
    BOOL enabled = IsWindowEnabled(hwnd);
    RECT a = r, b = r, track = r;
    if (vert) {
        a.bottom = a.top + arrow;
        b.top = b.bottom - arrow;
        track.top = a.bottom;
        track.bottom = b.top;
    } else {
        a.right = a.left + arrow;
        b.left = b.right - arrow;
        track.left = a.right;
        track.right = b.left;
    }
    UINT state = enabled ? 0 : DFCS_INACTIVE;
    DrawFrameControl(dc, &a, DFC_SCROLL, (vert ? DFCS_SCROLLUP : DFCS_SCROLLLEFT) | state);
    DrawFrameControl(dc, &b, DFC_SCROLL, (vert ? DFCS_SCROLLDOWN : DFCS_SCROLLRIGHT) | state);
    FillRect(dc, &track, GetSysColorBrush(COLOR_SCROLLBAR));

    SCROLLINFO si = {sizeof(si), SIF_ALL};
    GetScrollInfo(hwnd, SB_CTL, &si);
    int span = si.nMax - si.nMin + 1;
    int room = vert ? track.bottom - track.top : track.right - track.left;
    if (!enabled || span <= 0 || room <= 0 || (si.nPage && (int)si.nPage >= span))
        return;
    int size = si.nPage ? UserMulDiv(room, (int)si.nPage, span) : thick;
    if (size < thick / 2)
        size = thick / 2;
    if (size > room)
        size = room;
    int steps = span - (si.nPage ? (int)si.nPage : 1);
    int offset = steps > 0 ? UserMulDiv(room - size, si.nPos - si.nMin, steps) : 0;
    RECT thumb = track;
    if (vert) {
        thumb.top += offset;
        thumb.bottom = thumb.top + size;
    } else {
        thumb.left += offset;
        thumb.right = thumb.left + size;
    }
    DrawEdge(dc, &thumb, EDGE_RAISED, BF_RECT | BF_MIDDLE);
}

LRESULT CALLBACK ScrollBarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LRESULT result;
    if (UserScrollBarMessage(hwnd, msg, wparam, lparam, &result))
        return result;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc)
            scrollbar_paint(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_ENABLE) {
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
