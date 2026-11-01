/*
 * the Button control: push buttons, check boxes, radio buttons and group boxes, drawn with the visual style when it
 * is available.
 */
#include "user32_private.h"

typedef struct BUTTON {
    UINT State;      /* BST_* */
    BOOL Hot;
    BOOL Tracking;   /* the left button is held. */
    HFONT Font;
    HANDLE Image;
    UINT ImageType;
    UINT UiState;    /* UISF_HIDEFOCUS and UISF_HIDEACCEL */
} BUTTON;

static BUTTON *get_data(HWND hwnd)
{
    return (BUTTON *)GetWindowLongPtrW(hwnd, 0);
}

static UINT button_type(HWND hwnd)
{
    return (UINT)GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK;
}

static BOOL is_push(UINT type)
{
    return type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_SPLITBUTTON || type == BS_DEFSPLITBUTTON ||
           type == BS_COMMANDLINK || type == BS_DEFCOMMANDLINK;
}

static BOOL is_check(UINT type)
{
    return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE;
}

static BOOL is_radio(UINT type)
{
    return type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
}

static void notify(HWND hwnd, WORD code)
{
    HWND parent = GetParent(hwnd);
    if (parent)
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), code), (LPARAM)hwnd);
}

/* asks the parent for the brush and text colors with WM_CTLCOLORBTN or WM_CTLCOLORSTATIC. */
static HBRUSH parent_brush(HWND hwnd, HDC dc, UINT msg)
{
    HWND parent = GetParent(hwnd);
    HBRUSH b = parent ? (HBRUSH)SendMessageW(parent, msg, (WPARAM)dc, (LPARAM)hwnd) : nullptr;
    if (!b)
        b = (HBRUSH)DefWindowProcW(parent ? parent : hwnd, msg, (WPARAM)dc, (LPARAM)hwnd);
    return b;
}

static UINT text_flags(HWND hwnd, BUTTON *b, UINT base)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    UINT f = base;
    if (b->UiState & UISF_HIDEACCEL)
        f |= DT_HIDEPREFIX;
    if (!(style & BS_MULTILINE))
        f |= DT_SINGLELINE;
    else
        f |= DT_WORDBREAK;
    switch (style & BS_CENTER) {
    case BS_LEFT:
        f |= DT_LEFT;
        break;
    case BS_RIGHT:
        f |= DT_RIGHT;
        break;
    case BS_CENTER:
        f |= DT_CENTER;
        break;
    default:
        if (is_push(button_type(hwnd)))
            f |= DT_CENTER;
        break;
    }
    switch (style & BS_VCENTER) {
    case BS_TOP:
        f |= DT_TOP;
        break;
    case BS_BOTTOM:
        f |= DT_BOTTOM;
        break;
    default:
        f |= DT_VCENTER;
        break;
    }
    return f;
}

static LPWSTR window_text(HWND hwnd)
{
    int n = GetWindowTextLengthW(hwnd);
    LPWSTR text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (text)
        GetWindowTextW(hwnd, text, n + 1);
    return text;
}

static void draw_label(HWND hwnd, BUTTON *b, HDC dc, RECT *r, UINT flags, COLORREF color)
{
    LPWSTR text = window_text(hwnd);
    if (!text)
        return;
    if (b->Image && b->ImageType == IMAGE_ICON) {
        int cx = GetSystemMetricsForDpi(SM_CXSMICON, UserWindowDpi(hwnd));
        DrawIconEx(dc, r->left + (r->right - r->left - cx) / 2, r->top + (r->bottom - r->top - cx) / 2, b->Image, cx,
                   cx, 0, nullptr, DI_NORMAL);
    }
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    if ((flags & DT_SINGLELINE) == 0 && (flags & DT_VCENTER)) {
        /* DrawText only centers single lines vertically, so multiline text is centered by hand. */
        RECT calc = *r;
        int h = DrawTextW(dc, text, -1, &calc, (flags & ~DT_VCENTER) | DT_CALCRECT);
        RECT t = *r;
        t.top = r->top + (r->bottom - r->top - h) / 2;
        DrawTextW(dc, text, -1, &t, flags & ~DT_VCENTER);
    } else {
        DrawTextW(dc, text, -1, r, flags);
    }
    UserFree(text);
}

static void paint_push(HWND hwnd, BUTTON *b, HDC dc, RECT *client, BOOL focused, BOOL disabled)
{
    UINT dpi = UserWindowDpi(hwnd);
    UINT type = button_type(hwnd);
    /* a push-like check box or radio button looks pressed while it is checked. */
    BOOL pushlike = (GetWindowLongW(hwnd, GWL_STYLE) & BS_PUSHLIKE) && (is_check(type) || is_radio(type));
    BOOL pressed = (b->State & BST_PUSHED) != 0 || (pushlike && (b->State & BST_CHECKED));
    /* windows 11 shows the default button only while its window is active. */
    BOOL active = GetActiveWindow() == GetAncestor(hwnd, GA_ROOT);
    int state = disabled ? PBS_DISABLED : pressed ? PBS_PRESSED : b->Hot ? PBS_HOT
                : (active && (type == BS_DEFPUSHBUTTON || focused)) ? PBS_DEFAULTED : PBS_NORMAL;
    HBRUSH bg = parent_brush(hwnd, dc, WM_CTLCOLORBTN);
    FillRect(dc, client, bg);
    RECT content = *client;
    COLORREF color;
    if (UserThemeDrawBackground(dc, THEME_PUSHBUTTON, state, client, dpi)) {
        UserThemeContentRect(THEME_PUSHBUTTON, state, client, &content, dpi);
        color = UserThemeTextColor(THEME_PUSHBUTTON, state, disabled ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_BTNTEXT));
    } else {
        RECT r = *client;
        if (type == BS_DEFPUSHBUTTON || focused) {
            FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME));
            InflateRect(&r, -1, -1);
        }
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(dc, &r, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        content = r;
        InflateRect(&content, -3, -3);
        if (pressed)
            OffsetRect(&content, 1, 1);
        color = GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
    }
    draw_label(hwnd, b, dc, &content, text_flags(hwnd, b, 0), color);
    if (focused && !(b->UiState & UISF_HIDEFOCUS))
        UserDrawFocusRect(dc, &content);
}

static void paint_check(HWND hwnd, BUTTON *b, HDC dc, RECT *client, BOOL focused, BOOL disabled)
{
    UINT dpi = UserWindowDpi(hwnd);
    UINT type = button_type(hwnd);
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    BOOL radio = is_radio(type);
    HBRUSH bg = parent_brush(hwnd, dc, WM_CTLCOLORSTATIC);
    FillRect(dc, client, bg);
    int part = radio ? THEME_RADIOBUTTON : THEME_CHECKBOX;
    int check = (b->State & BST_INDETERMINATE) && !radio ? 2 : (b->State & BST_CHECKED) ? 1 : 0;
    int sub = disabled ? 3 : (b->State & BST_PUSHED) ? 2 : b->Hot ? 1 : 0;
    int state = check * 4 + sub + 1;
    SIZE box = {GetSystemMetricsForDpi(SM_CXMENUCHECK, dpi), GetSystemMetricsForDpi(SM_CYMENUCHECK, dpi)};
    BOOL themed = UserThemePartSize(part, state, dpi, &box);
    RECT br;
    BOOL right = (style & BS_LEFTTEXT) != 0;
    br.left = right ? client->right - box.cx : client->left;
    br.right = br.left + box.cx;
    br.top = client->top + (client->bottom - client->top - box.cy) / 2;
    br.bottom = br.top + box.cy;
    if (!themed || !UserThemeDrawBackground(dc, part, state, &br, dpi)) {
        FillRect(dc, &br, GetSysColorBrush(disabled ? COLOR_3DFACE : COLOR_WINDOW));
        DrawEdge(dc, &br, EDGE_SUNKEN, BF_RECT);
        if (check) {
            RECT in = br;
            InflateRect(&in, -box.cx / 4, -box.cy / 4);
            FillRect(dc, &in, GetSysColorBrush(COLOR_WINDOWTEXT));
        }
    }
    /* the label starts a theme defined gap after the box, 3 pixels at 96 dpi. */
    RECT text = *client;
    int gap = UserMulDiv(3, (int)dpi, 96);
    if (right)
        text.right = br.left - gap;
    else
        text.left = br.right + gap;
    COLORREF color = disabled ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_BTNTEXT);
    UINT flags = text_flags(hwnd, b, 0);
    if (!(style & BS_CENTER))
        flags = (flags & ~(DT_CENTER | DT_RIGHT)) | DT_LEFT;
    draw_label(hwnd, b, dc, &text, flags, color);
    if (focused && !(b->UiState & UISF_HIDEFOCUS)) {
        LPWSTR label = window_text(hwnd);
        if (label) {
            RECT calc = text;
            DrawTextW(dc, label, -1, &calc, (flags & ~(DT_VCENTER | DT_CENTER | DT_RIGHT)) | DT_CALCRECT);
            int h = calc.bottom - calc.top;
            calc.top = text.top + (text.bottom - text.top - h) / 2;
            calc.bottom = calc.top + h;
            InflateRect(&calc, 1, 0);
            if (calc.right > client->right)
                calc.right = client->right;
            UserDrawFocusRect(dc, &calc);
            UserFree(label);
        }
    }
}

static void paint_group(HWND hwnd, BUTTON *b, HDC dc, RECT *client, BOOL disabled)
{
    HBRUSH bg = parent_brush(hwnd, dc, WM_CTLCOLORSTATIC);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    RECT frame = *client;
    frame.top += tm.tmHeight / 2;
    UINT dpi = UserWindowDpi(hwnd);
    if (!UserThemeDrawBackground(dc, THEME_GROUPBOX, 1, &frame, dpi)) {
        FrameRect(dc, &frame, GetSysColorBrush(COLOR_3DSHADOW));
    }
    LPWSTR text = window_text(hwnd);
    if (text && *text) {
        RECT t = {client->left + UserMulDiv(8, (int)dpi, 96), client->top, client->right, client->top + tm.tmHeight};
        RECT calc = t;
        DrawTextW(dc, text, -1, &calc, DT_SINGLELINE | DT_CALCRECT | text_flags(hwnd, b, 0));
        calc.left -= 2;
        calc.right += 2;
        FillRect(dc, &calc, bg);
        SetTextColor(dc, GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &t, DT_SINGLELINE | DT_LEFT | DT_TOP | (b->UiState & UISF_HIDEACCEL ? DT_HIDEPREFIX : 0));
    }
    UserFree(text);
}

static void paint(HWND hwnd, BUTTON *b, HDC dc)
{
    RECT client;
    GetClientRect(hwnd, &client);
    UINT type = button_type(hwnd);
    BOOL focused = GetFocus() == hwnd;
    BOOL disabled = !IsWindowEnabled(hwnd);
    HGDIOBJ old_font = SelectObject(dc, b->Font ? b->Font : GetStockObject(SYSTEM_FONT));
    if (type == BS_OWNERDRAW) {
        DRAWITEMSTRUCT dis = {ODT_BUTTON, (UINT)GetDlgCtrlID(hwnd), 0, ODA_DRAWENTIRE, 0, hwnd, dc, client, 0};
        if (b->State & BST_PUSHED)
            dis.itemState |= ODS_SELECTED;
        if (focused)
            dis.itemState |= ODS_FOCUS;
        if (disabled)
            dis.itemState |= ODS_DISABLED;
        SendMessageW(GetParent(hwnd), WM_DRAWITEM, (WPARAM)dis.CtlID, (LPARAM)&dis);
    } else if (type == BS_GROUPBOX) {
        paint_group(hwnd, b, dc, &client, disabled);
    } else if ((is_check(type) || is_radio(type)) && !(GetWindowLongW(hwnd, GWL_STYLE) & BS_PUSHLIKE)) {
        paint_check(hwnd, b, dc, &client, focused, disabled);
    } else {
        paint_push(hwnd, b, dc, &client, focused, disabled);
    }
    SelectObject(dc, old_font);
}

static void redraw(HWND hwnd)
{
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void click(HWND hwnd, BUTTON *b)
{
    UINT type = button_type(hwnd);
    switch (type) {
    case BS_AUTOCHECKBOX:
        SendMessageW(hwnd, BM_SETCHECK, (b->State & BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED, 0);
        break;
    case BS_AUTO3STATE:
        SendMessageW(hwnd, BM_SETCHECK, (b->State & BST_INDETERMINATE) ? BST_UNCHECKED : (b->State & BST_CHECKED) ? BST_INDETERMINATE : BST_CHECKED, 0);
        break;
    case BS_AUTORADIOBUTTON: {
        /* uncheck the other automatic radio buttons of the group. */
        HWND parent = GetParent(hwnd);
        HWND start = hwnd;
        HWND w = hwnd;
        while (w && !(GetWindowLongW(w, GWL_STYLE) & WS_GROUP)) {
            HWND prev = GetWindow(w, GW_HWNDPREV);
            if (!prev)
                break;
            w = prev;
        }
        for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
            if (w != start && (GetWindowLongW(w, GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON)
                SendMessageW(w, BM_SETCHECK, BST_UNCHECKED, 0);
            HWND next = GetWindow(w, GW_HWNDNEXT);
            if (next && (GetWindowLongW(next, GWL_STYLE) & WS_GROUP))
                break;
        }
        SendMessageW(hwnd, BM_SETCHECK, BST_CHECKED, 0);
        (void)parent;
        break;
    }
    }
    notify(hwnd, BN_CLICKED);
}

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    BUTTON *b = get_data(hwnd);
    if (!b && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE:
        b = UserAlloc(sizeof(BUTTON));
        if (!b)
            return FALSE;
        /* windows 11 hides focus rectangles and mnemonics until the keyboard is used. */
        b->UiState = UISF_HIDEFOCUS | UISF_HIDEACCEL;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)b);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, 0, 0);
        UserFree(b);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CREATE: {
        UINT type = button_type(hwnd);
        if (type == BS_GROUPBOX)
            SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : (HDC)wparam;
        if (dc)
            paint(hwnd, b, dc);
        if (msg == WM_PAINT)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        if (button_type(hwnd) == BS_GROUPBOX)
            return HTTRANSPARENT;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETFONT:
        b->Font = (HFONT)wparam;
        if (lparam)
            redraw(hwnd);
        return 0;
    case WM_GETFONT:
        return (LRESULT)b->Font;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wparam, lparam);
        redraw(hwnd);
        return r;
    }
    case WM_ENABLE:
    case WM_SYSCOLORCHANGE:
        redraw(hwnd);
        return 0;
    case WM_SETFOCUS:
        redraw(hwnd);
        if (GetWindowLongW(hwnd, GWL_STYLE) & BS_NOTIFY)
            notify(hwnd, BN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        if (b->State & BST_PUSHED) {
            b->State &= ~BST_PUSHED;
            if (GetCapture() == hwnd)
                ReleaseCapture();
        }
        b->Tracking = FALSE;
        redraw(hwnd);
        if (GetWindowLongW(hwnd, GWL_STYLE) & BS_NOTIFY)
            notify(hwnd, BN_KILLFOCUS);
        return 0;
    case WM_UPDATEUISTATE:
    case WM_CHANGEUISTATE: {
        UINT action = LOWORD(wparam), bits = HIWORD(wparam);
        UINT old = b->UiState;
        if (action == UIS_SET)
            b->UiState |= bits;
        else if (action == UIS_CLEAR)
            b->UiState &= ~bits;
        if (old != b->UiState)
            redraw(hwnd);
        return 0;
    }
    case WM_QUERYUISTATE:
        return b->UiState;
    case WM_GETDLGCODE: {
        UINT type = button_type(hwnd);
        if (type == BS_DEFPUSHBUTTON || type == BS_DEFSPLITBUTTON || type == BS_DEFCOMMANDLINK)
            return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
        if (is_push(type))
            return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
        if (is_radio(type))
            return DLGC_BUTTON | DLGC_RADIOBUTTON;
        if (type == BS_GROUPBOX)
            return DLGC_STATIC;
        return DLGC_BUTTON;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (button_type(hwnd) == BS_GROUPBOX)
            return 0;
        SetCapture(hwnd);
        SetFocus(hwnd);
        b->Tracking = TRUE;
        b->State |= BST_PUSHED;
        redraw(hwnd);
        return 0;
    case WM_MOUSEMOVE: {
        if (!b->Hot) {
            b->Hot = TRUE;
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            redraw(hwnd);
        }
        if (b->Tracking) {
            RECT r;
            GetClientRect(hwnd, &r);
            POINT pt = {(SHORT)LOWORD(lparam), (SHORT)HIWORD(lparam)};
            BOOL inside = PtInRect(&r, pt);
            if (inside != ((b->State & BST_PUSHED) != 0)) {
                b->State ^= BST_PUSHED;
                redraw(hwnd);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (b->Hot) {
            b->Hot = FALSE;
            redraw(hwnd);
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!b->Tracking)
            return 0;
        b->Tracking = FALSE;
        BOOL pushed = (b->State & BST_PUSHED) != 0;
        b->State &= ~BST_PUSHED;
        ReleaseCapture();
        redraw(hwnd);
        if (pushed)
            click(hwnd, b);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (b->Tracking && (HWND)lparam != hwnd) {
            b->Tracking = FALSE;
            b->State &= ~BST_PUSHED;
            redraw(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_SPACE && !(lparam & (1 << 30))) {
            b->State |= BST_PUSHED;
            redraw(hwnd);
        }
        return 0;
    case WM_KEYUP:
        if (wparam == VK_SPACE && (b->State & BST_PUSHED)) {
            b->State &= ~BST_PUSHED;
            redraw(hwnd);
            click(hwnd, b);
        }
        return 0;
    case WM_SYSKEYUP:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case BM_CLICK:
        if (IsWindowEnabled(hwnd)) {
            b->State |= BST_PUSHED;
            redraw(hwnd);
            UpdateWindow(hwnd);
            b->State &= ~BST_PUSHED;
            redraw(hwnd);
            click(hwnd, b);
        }
        return 0;
    case BM_GETCHECK:
        return b->State & (BST_CHECKED | BST_INDETERMINATE);
    case BM_SETCHECK: {
        UINT old = b->State;
        UINT type = button_type(hwnd);
        /* a button of three states keeps up to the third, any other check box or radio is checked by any value, and
           the rest have no check at all. a radio button is a tab stop while it is checked. */
        if (type == BS_3STATE || type == BS_AUTO3STATE) {
            if (wparam > BST_INDETERMINATE)
                wparam = BST_INDETERMINATE;
        } else if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || is_radio(type)) {
            wparam = wparam ? BST_CHECKED : BST_UNCHECKED;
        } else
            return 0;
        b->State = (b->State & ~(BST_CHECKED | BST_INDETERMINATE)) | (UINT)wparam;
        if (is_radio(type)) {
            DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
            style = wparam ? style | WS_TABSTOP : style & ~(DWORD)WS_TABSTOP;
            SetWindowLongW(hwnd, GWL_STYLE, (LONG)style);
        }
        if (old != b->State)
            redraw(hwnd);
        return 0;
    }
    case BM_GETSTATE:
        return b->State | (GetFocus() == hwnd ? BST_FOCUS : 0) | (b->Hot ? BST_HOT : 0);
    case BM_SETSTATE:
        if (wparam)
            b->State |= BST_PUSHED;
        else
            b->State &= ~BST_PUSHED;
        redraw(hwnd);
        return 0;
    case BM_SETSTYLE: {
        DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        DWORD next = (style & ~(DWORD)BS_TYPEMASK) | ((DWORD)wparam & BS_TYPEMASK);
        if (next != style) {
            SetWindowLongW(hwnd, GWL_STYLE, (LONG)next);
            if (lparam)
                redraw(hwnd);
        }
        return 0;
    }
    case BM_SETIMAGE: {
        HANDLE old = b->Image;
        b->Image = (HANDLE)lparam;
        b->ImageType = (UINT)wparam;
        redraw(hwnd);
        return (LRESULT)old;
    }
    case BM_GETIMAGE:
        return (LRESULT)b->Image;
    case BM_SETDONTCLICK:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
