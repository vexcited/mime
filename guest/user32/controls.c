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
    HBRUSH brush = parent ? (HBRUSH)SendMessageW(parent, msg, (WPARAM)dc, (LPARAM)hwnd) : NULL;
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
    InvalidateRect(hwnd, NULL, TRUE);
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
            InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)e->Font;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wparam, lparam);
        InvalidateRect(hwnd, NULL, TRUE);
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
            edit_insert(hwnd, NULL, 0, TRUE);
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
            InvalidateRect(hwnd, NULL, TRUE);
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

LRESULT CALLBACK ComboBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg >= CB_GETEDITSEL && msg <= CB_MSGMAX)
        return CB_ERR;
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK ScrollBarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(dc, &r, GetSysColorBrush(COLOR_SCROLLBAR));
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
