/*
 * task dialogs. TaskDialogIndirect builds a modal dialog from its configuration: a primary panel with the main
 * icon, instruction, content, expanded information, progress bar, radio buttons and command links, over a
 * secondary panel with the expando, verification check box and push buttons, then a footer. windows draws it
 * with DirectUI, mime uses child windows laid out the same way.
 */
#include "comctl32_private.h"

#define BCM_SETSHIELD 0x160C
#define TD_TIMER 1
#define TD_TIMER_INTERVAL 200
/* child window ids, kept apart from the button ids the caller sees. */
#define ID_BUTTON_FIRST 0x8000
#define ID_RADIO_FIRST 0x9000
#define ID_VERIFY 0xA000
#define ID_EXPANDO 0xA001
#define ID_PROGRESS 0xA002
#define WM_TD_DEFERRED (WM_USER + 100)

typedef struct TD_BUTTON {
    int Id;
    LPWSTR Text;
    HWND Hwnd;
    BOOL CommandLink;
} TD_BUTTON;

typedef struct TD_STATE {
    TASKDIALOGCONFIG Config;
    HWND Hwnd;
    HRESULT Result;
    int *Button, *Radio;
    BOOL *Verification;
    TD_BUTTON *Buttons;
    int ButtonCount;
    TD_BUTTON *Radios;
    int RadioCount;
    int DefaultButton;
    int SelectedRadio;
    BOOL Checked, Expanded;
    BOOL CancelAllowed;
    HWND Instruction, Content, Expanded_, Footer, Verify, Expando, Progress, ExpandedFooter;
    HICON MainIcon, FooterIcon;
    BOOL OwnMainIcon, OwnFooterIcon;
    RECT MainIconRect, FooterIconRect;
    int SecondaryTop, FooterTop;
    HFONT InstructionFont, ContentFont;
    COLORREF InstructionColor;
    HTHEME Theme;
    DWORD TimerStart;
    LPWSTR Strings[8];
    int StringCount;
} TD_STATE;

/* a string or a resource id of the instance. the caller frees the copy. */
static LPWSTR resolve_string(TD_STATE *td, PCWSTR text)
{
    if (!text)
        return nullptr;
    LPWSTR copy = nullptr;
    if (IS_INTRESOURCE(text)) {
        const WCHAR *res = nullptr;
        int n = LoadStringW(td->Config.hInstance, (UINT)(ULONG_PTR)text, (LPWSTR)&res, 0);
        if (n <= 0 || !res)
            return nullptr;
        copy = CcAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
        if (copy)
            memcpy(copy, res, (SIZE_T)n * sizeof(WCHAR));
    } else {
        copy = CcStrDupW(text);
    }
    return copy;
}

/* reduces hyperlink markup to the link text. */
static LPWSTR strip_links(LPWSTR text, BOOL links)
{
    if (!text || !links)
        return text;
    LPWSTR out = text;
    for (LPCWSTR p = text; *p;) {
        if (p[0] == L'<' && (p[1] == L'a' || p[1] == L'A' || p[1] == L'/')) {
            while (*p && *p != L'>')
                p++;
            if (*p)
                p++;
            continue;
        }
        *out++ = *p++;
    }
    *out = 0;
    return text;
}

static HRESULT notify(TD_STATE *td, UINT code, WPARAM wparam, LPARAM lparam)
{
    if (!td->Config.pfCallback)
        return S_OK;
    return td->Config.pfCallback(td->Hwnd, code, wparam, lparam, td->Config.lpCallbackData);
}

static void close_with(TD_STATE *td, HRESULT hr)
{
    td->Result = hr;
    EndDialog(td->Hwnd, 0);
}

/* a failed callback answer closes the dialog with it. */
static BOOL check(TD_STATE *td, HRESULT hr)
{
    if (FAILED(hr)) {
        close_with(td, hr);
        return FALSE;
    }
    return TRUE;
}

static void click_button(TD_STATE *td, int id)
{
    HRESULT hr = notify(td, TDN_BUTTON_CLICKED, (WPARAM)id, 0);
    if (!check(td, hr))
        return;
    if (hr != S_OK)
        return;
    if (td->Button)
        *td->Button = id;
    if (td->Radio && td->SelectedRadio >= 0)
        *td->Radio = td->Radios[td->SelectedRadio].Id;
    if (td->Verification)
        *td->Verification = td->Checked;
    td->Result = S_OK;
    EndDialog(td->Hwnd, id);
}

/* ------------------------------------------------------------------ icons */

static HICON load_icon(TD_STATE *td, PCWSTR name, BOOL small)
{
    int cx = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON), cy = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
    WORD id = 0;
    if (IS_INTRESOURCE(name)) {
        switch ((WORD)(ULONG_PTR)name) {
        case 0xFFFF:
            id = 84;
            break;
        case 0xFFFE:
            id = 98;
            break;
        case 0xFFFD:
            id = 81;
            break;
        case 0xFFFC:
        case 0xFFFB:
        case 0xFFF7:
            id = 78;
            break;
        case 0xFFFA:
            id = 107;
            break;
        case 0xFFF9:
            id = 105;
            break;
        case 0xFFF8:
            id = 106;
            break;
        case 0xFF9C:
            id = 116;
            break;
        }
    }
    HICON icon = nullptr;
    if (id) {
        HMODULE res = LoadLibraryExW(L"imageres.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (res) {
            icon = LoadImageW(res, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, 0);
            FreeLibrary(res);
        }
        if (!icon) {
            LPCWSTR fallback = id == 84 ? IDI_WARNING : id == 98 ? IDI_ERROR : id == 81 ? IDI_INFORMATION : IDI_SHIELD;
            icon = CopyImage(LoadIconW(nullptr, fallback), IMAGE_ICON, cx, cy, 0);
        }
    } else if (name) {
        icon = LoadImageW(td->Config.hInstance, name, IMAGE_ICON, cx, cy, 0);
    }
    return icon;
}

/* ------------------------------------------------------------------ layout */

static HFONT message_font(int scale_percent, int weight)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = lf.lfHeight * scale_percent / 100;
    if (weight)
        lf.lfWeight = weight;
    return CreateFontIndirectW(&lf);
}

static void make_fonts(TD_STATE *td)
{
    HTHEME style = OpenThemeData(td->Hwnd, L"TaskDialogStyle");
    LOGFONTW lf;
    HDC dc = GetDC(td->Hwnd);
    td->InstructionColor = RGB(0x00, 0x33, 0x99);
    if (style && SUCCEEDED(GetThemeFont(style, dc, 2, 0, TMT_FONT, &lf)))
        td->InstructionFont = CreateFontIndirectW(&lf);
    if (style)
        GetThemeColor(style, 2, 0, TMT_TEXTCOLOR, &td->InstructionColor);
    if (style && SUCCEEDED(GetThemeFont(style, dc, 4, 0, TMT_FONT, &lf)))
        td->ContentFont = CreateFontIndirectW(&lf);
    ReleaseDC(td->Hwnd, dc);
    if (style)
        CloseThemeData(style);
    if (!td->InstructionFont)
        td->InstructionFont = message_font(133, 0);
    if (!td->ContentFont)
        td->ContentFont = message_font(100, 0);
}

static int text_height(HWND hwnd, HFONT font, int width)
{
    int n = GetWindowTextLengthW(hwnd);
    if (!n || width <= 0)
        return 0;
    LPWSTR text = CcAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return 0;
    GetWindowTextW(hwnd, text, n + 1);
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, font);
    RECT r = {0, 0, width, 0};
    DrawTextW(dc, text, n, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    CcFree(text);
    return r.bottom;
}

static int text_width(HWND hwnd, HFONT font, LPCWSTR text)
{
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size = {};
    if (text && *text)
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return size.cx;
}

static int dlu_x(TD_STATE *td, int dlu)
{
    TEXTMETRICW tm;
    HDC dc = GetDC(td->Hwnd);
    HGDIOBJ old = SelectObject(dc, td->ContentFont);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(td->Hwnd, dc);
    SIZE size;
    dc = GetDC(td->Hwnd);
    old = SelectObject(dc, td->ContentFont);
    GetTextExtentPoint32W(dc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size);
    SelectObject(dc, old);
    ReleaseDC(td->Hwnd, dc);
    int base = (size.cx / 26 + 1) / 2;
    return MulDiv(dlu, base, 4);
}

/* push buttons are equally wide, at least 50 dlu, and the row grows from the right. */
static int button_metrics(TD_STATE *td, int *width, int *height)
{
    int w = dlu_x(td, 50), count = 0;
    for (int k = 0; k < td->ButtonCount; k++) {
        if (td->Buttons[k].CommandLink)
            continue;
        int tw = text_width(td->Hwnd, td->ContentFont, td->Buttons[k].Text) + 2 * dlu_x(td, 6);
        if (tw > w)
            w = tw;
        count++;
    }
    *width = w;
    *height = MulDiv(23, (int)GetDpiForWindow(td->Hwnd), 96);
    return count;
}

/* places every child for a client width and returns the client height. */
static int layout(TD_STATE *td, int width, BOOL move)
{
    int pad = 10, gap = 10;
    int y = pad;
    int x = pad;
    if (td->MainIcon) {
        int cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);
        SetRect(&td->MainIconRect, pad, pad, pad + cx, pad + cy);
        x = pad + cx + gap;
    } else {
        SetRectEmpty(&td->MainIconRect);
    }
    int text_w = width - x - pad;
    int top = y;
#define PLACE(hwnd, font, extra_gap)                                                                              \
    do {                                                                                                          \
        if (hwnd && (GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE)) {                                            \
            int h_ = text_height(hwnd, font, text_w);                                                             \
            if (y > top)                                                                                          \
                y += extra_gap;                                                                                   \
            if (move)                                                                                             \
                MoveWindow(hwnd, x, y, text_w, h_, TRUE);                                                         \
            y += h_;                                                                                              \
        }                                                                                                         \
    } while (0)
    PLACE(td->Instruction, td->InstructionFont, 0);
    PLACE(td->Content, td->ContentFont, gap);
    if (td->Expanded_ && td->Expanded && !(td->Config.dwFlags & TDF_EXPAND_FOOTER_AREA))
        PLACE(td->Expanded_, td->ContentFont, gap);
    if (td->MainIcon && y < td->MainIconRect.bottom)
        y = td->MainIconRect.bottom;
    if (td->Progress) {
        y += gap;
        if (move)
            MoveWindow(td->Progress, x, y, text_w, 15, TRUE);
        y += 15;
    }
    for (int k = 0; k < td->RadioCount; k++) {
        int h = text_height(td->Radios[k].Hwnd, td->ContentFont, text_w - 20) + 4;
        y += k ? 5 : gap;
        if (move)
            MoveWindow(td->Radios[k].Hwnd, x, y, text_w, h, TRUE);
        y += h;
    }
    for (int k = 0; k < td->ButtonCount; k++) {
        if (!td->Buttons[k].CommandLink)
            continue;
        int h = text_height(td->Buttons[k].Hwnd, td->InstructionFont, text_w - 40) + 20;
        y += gap;
        if (move)
            MoveWindow(td->Buttons[k].Hwnd, x, y, text_w, h, TRUE);
        y += h;
    }
    y += pad;
    td->SecondaryTop = y;
    /* the secondary panel: expando and verification on the left, push buttons on the right. */
    int bw, bh;
    int push = button_metrics(td, &bw, &bh);
    int row_top = y + 9;
    int buttons_w = push ? push * bw + (push - 1) * 10 : 0;
    /* the left column: the expando, then the check box. */
    int left_w = 0, verify_w = 0;
    if (td->Expando)
        left_w = text_width(td->Hwnd, td->ContentFont, L"Hide details") + 40;
    if (td->Verify) {
        int n = GetWindowTextLengthW(td->Verify);
        LPWSTR text = CcAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
        if (text) {
            GetWindowTextW(td->Verify, text, n + 1);
            verify_w = text_width(td->Hwnd, td->ContentFont, text) + 24;
            CcFree(text);
        }
        if (verify_w > left_w)
            left_w = verify_w;
    }
    /* side by side when both fit, otherwise the buttons go above the left column. */
    BOOL stacked = left_w && push && left_w + 20 + buttons_w > width - 2 * pad;
    int left_y = stacked ? row_top + bh + 9 : row_top;
    if (td->Expando) {
        int w = text_width(td->Hwnd, td->ContentFont, L"Hide details") + 40;
        if (move)
            MoveWindow(td->Expando, pad, left_y, w, bh, TRUE);
        left_y += bh + 5;
    }
    if (td->Verify) {
        int vw = stacked ? width - 2 * pad : verify_w;
        if (vw > width - 2 * pad)
            vw = width - 2 * pad;
        int h = text_height(td->Verify, td->ContentFont, vw - 24) + 4;
        if (h < bh && !stacked)
            h = bh;
        if (move)
            MoveWindow(td->Verify, pad, left_y, vw, h, TRUE);
        left_y += h + 5;
    }
    int bx = width - pad;
    for (int k = td->ButtonCount - 1; k >= 0; k--) {
        if (td->Buttons[k].CommandLink)
            continue;
        bx -= bw;
        if (move)
            MoveWindow(td->Buttons[k].Hwnd, bx, row_top, bw, bh, TRUE);
        bx -= 10;
    }
    int bottom = push ? row_top + bh : row_top;
    if (left_y - 5 > bottom)
        bottom = left_y - 5;
    if (!push && !td->Expando && !td->Verify)
        bottom = y;
    else
        bottom += 9;
    y = bottom;
    td->FooterTop = -1;
    if (td->Footer || (td->ExpandedFooter && td->Expanded)) {
        td->FooterTop = y;
        y += 9;
        int fx = pad;
        if (td->FooterIcon) {
            int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
            SetRect(&td->FooterIconRect, pad, y, pad + cx, y + cy);
            fx = pad + cx + 6;
        } else {
            SetRectEmpty(&td->FooterIconRect);
        }
        int fw = width - fx - pad;
        if (td->Footer) {
            int h = text_height(td->Footer, td->ContentFont, fw);
            if (move)
                MoveWindow(td->Footer, fx, y, fw, h, TRUE);
            y += h;
        }
        if (td->ExpandedFooter && td->Expanded) {
            int h = text_height(td->ExpandedFooter, td->ContentFont, width - 2 * pad);
            y += td->Footer ? gap : 0;
            if (move)
                MoveWindow(td->ExpandedFooter, pad, y, width - 2 * pad, h, TRUE);
            y += h;
        }
        y += 9;
    }
#undef PLACE
    return y;
}

static void size_dialog(TD_STATE *td, BOOL position)
{
    HMONITOR monitor = MonitorFromWindow(td->Config.hwndParent ? td->Config.hwndParent : td->Hwnd,
                                         MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    RECT work = mi.rcWork;
    int max_w = dlu_x(td, 278), min_w = dlu_x(td, 183);
    if (max_w > work.right - work.left)
        max_w = work.right - work.left;
    int bw, bh;
    int push = button_metrics(td, &bw, &bh);
    int row = push * bw + (push > 0 ? push - 1 : 0) * 10 + 20;
    int width;
    if (td->Config.cxWidth) {
        width = dlu_x(td, (int)td->Config.cxWidth);
    } else {
        width = row > min_w ? row : min_w;
        int h = layout(td, max_w, FALSE);
        int guess;
        if (td->Config.dwFlags & TDF_SIZE_TO_CONTENT) {
            guess = text_width(td->Hwnd, td->InstructionFont, nullptr);
            int n = td->Instruction ? GetWindowTextLengthW(td->Instruction) : 0;
            if (n) {
                LPWSTR text = CcAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
                if (text) {
                    GetWindowTextW(td->Instruction, text, n + 1);
                    guess = text_width(td->Hwnd, td->InstructionFont, text);
                    CcFree(text);
                }
            }
            guess += (td->MainIcon ? GetSystemMetrics(SM_CXICON) : 0) + dlu_x(td, 5) + 30;
            if (guess > dlu_x(td, 278))
                guess = dlu_x(td, 278);
        } else {
            /* a pleasant ratio from the area the content needs, as windows computes it. */
            LONGLONG area = (LONGLONG)h * (row > 0 ? row : 1) * 6 / 10;
            LONGLONG s = 0;
            while ((s + 1) * (s + 1) <= area)
                s++;
            guess = (int)(s * 16667 / 10000);
        }
        if (guess > width)
            width = guess;
    }
    if (width < min_w)
        width = min_w;
    if (width > max_w)
        width = max_w;
    int height = layout(td, width, TRUE);
    RECT r = {0, 0, width, height};
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(td->Hwnd, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongW(td->Hwnd, GWL_EXSTYLE));
    int w = r.right - r.left, h = r.bottom - r.top;
    if (position) {
        RECT around = work;
        if ((td->Config.dwFlags & TDF_POSITION_RELATIVE_TO_WINDOW) && td->Config.hwndParent)
            GetWindowRect(td->Config.hwndParent, &around);
        int x = around.left + (around.right - around.left - w) / 2;
        int y = around.top + (around.bottom - around.top - h) / 2;
        if (x < work.left)
            x = work.left;
        if (y < work.top)
            y = work.top;
        SetWindowPos(td->Hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(td->Hwnd, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    }
    InvalidateRect(td->Hwnd, nullptr, TRUE);
}

/* ------------------------------------------------------------------ building */

static HWND child(TD_STATE *td, LPCWSTR cls, LPCWSTR text, DWORD style, int id, HFONT font)
{
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, td->Hwnd,
                                (HMENU)(INT_PTR)id, Comctl32Instance, nullptr);
    if (hwnd)
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, FALSE);
    return hwnd;
}

static LPWSTR keep(TD_STATE *td, LPWSTR s)
{
    if (s && td->StringCount < 8)
        td->Strings[td->StringCount++] = s;
    return s;
}

static void add_button(TD_STATE *td, int id, LPCWSTR text, BOOL link)
{
    TD_BUTTON *b = &td->Buttons[td->ButtonCount];
    b->Id = id;
    b->Text = CcStrDupW(text ? text : L"");
    b->CommandLink = link;
    if (link && b->Text)
        for (LPWSTR p = b->Text; *p; p++)
            if (*p == L'\n') {
                *p = 0;
                break;
            }
    DWORD style = WS_TABSTOP | (link ? BS_COMMANDLINK : BS_PUSHBUTTON);
    b->Hwnd = child(td, L"Button", b->Text, style, ID_BUTTON_FIRST + td->ButtonCount, td->ContentFont);
    td->ButtonCount++;
}

static const struct {
    DWORD Flag;
    int Id;
    LPCWSTR Text;
} g_common[] = {
    {TDCBF_OK_BUTTON, IDOK, L"OK"},          {TDCBF_YES_BUTTON, IDYES, L"Yes"},
    {TDCBF_NO_BUTTON, IDNO, L"No"},          {0x10000, IDABORT, L"Abort"},
    {TDCBF_RETRY_BUTTON, IDRETRY, L"Retry"}, {TDCBF_CANCEL_BUTTON, IDCANCEL, L"Cancel"},
    {0x20000, IDIGNORE, L"Ignore"},          {0x40000, IDTRYAGAIN, L"Try Again"},
    {0x80000, IDCONTINUE, L"Continue"},      {TDCBF_CLOSE_BUTTON, IDCLOSE, L"Close"},
    {0x100000, IDHELP, L"Help"},
};

static void destroy_page(TD_STATE *td)
{
    KillTimer(td->Hwnd, TD_TIMER);
    for (HWND c = GetWindow(td->Hwnd, GW_CHILD); c;) {
        HWND next = GetWindow(c, GW_HWNDNEXT);
        DestroyWindow(c);
        c = next;
    }
    for (int k = 0; k < td->ButtonCount; k++)
        CcFree(td->Buttons[k].Text);
    for (int k = 0; k < td->RadioCount; k++)
        CcFree(td->Radios[k].Text);
    CcFree(td->Buttons);
    CcFree(td->Radios);
    td->Buttons = td->Radios = nullptr;
    td->ButtonCount = td->RadioCount = 0;
    for (int k = 0; k < td->StringCount; k++)
        CcFree(td->Strings[k]);
    td->StringCount = 0;
    if (td->OwnMainIcon && td->MainIcon)
        DestroyIcon(td->MainIcon);
    if (td->OwnFooterIcon && td->FooterIcon)
        DestroyIcon(td->FooterIcon);
    td->MainIcon = td->FooterIcon = nullptr;
    td->Instruction = td->Content = td->Expanded_ = td->Footer = td->Verify = td->Expando = td->Progress = nullptr;
    td->ExpandedFooter = nullptr;
}

static BOOL cancel_allowed(const TASKDIALOGCONFIG *c)
{
    if ((c->dwCommonButtons & TDCBF_CANCEL_BUTTON) || (c->dwFlags & TDF_ALLOW_DIALOG_CANCELLATION))
        return TRUE;
    if (!c->hwndParent && (c->dwFlags & TDF_CAN_BE_MINIMIZED))
        return TRUE;
    for (UINT k = 0; k < c->cButtons; k++)
        if (c->pButtons[k].nButtonID == IDCANCEL)
            return TRUE;
    return FALSE;
}

static void build_page(TD_STATE *td)
{
    TASKDIALOGCONFIG *c = &td->Config;
    BOOL links = (c->dwFlags & TDF_ENABLE_HYPERLINKS) != 0;
    td->CancelAllowed = cancel_allowed(c);
    DWORD style = (DWORD)GetWindowLongW(td->Hwnd, GWL_STYLE);
    style = td->CancelAllowed ? style | WS_SYSMENU : style & ~WS_SYSMENU;
    if (!c->hwndParent && (c->dwFlags & TDF_CAN_BE_MINIMIZED))
        style |= WS_MINIMIZEBOX | WS_SYSMENU;
    SetWindowLongW(td->Hwnd, GWL_STYLE, (LONG)style);
    SetWindowPos(td->Hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    LPWSTR title = resolve_string(td, c->pszWindowTitle);
    if (!title) {
        WCHAR path[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        LPCWSTR name = path;
        for (DWORD k = 0; k < n; k++)
            if (path[k] == L'\\' || path[k] == L'/')
                name = path + k + 1;
        title = CcStrDupW(n ? name : L"");
    }
    SetWindowTextW(td->Hwnd, title);
    CcFree(title);

    if (c->dwFlags & TDF_USE_HICON_MAIN) {
        td->MainIcon = c->hMainIcon;
        td->OwnMainIcon = FALSE;
    } else if (c->pszMainIcon) {
        td->MainIcon = load_icon(td, c->pszMainIcon, FALSE);
        td->OwnMainIcon = TRUE;
    }
    if (c->dwFlags & TDF_USE_HICON_FOOTER) {
        td->FooterIcon = c->hFooterIcon;
        td->OwnFooterIcon = FALSE;
    } else if (c->pszFooterIcon) {
        td->FooterIcon = load_icon(td, c->pszFooterIcon, TRUE);
        td->OwnFooterIcon = TRUE;
    }
    LPWSTR text = keep(td, resolve_string(td, c->pszMainInstruction));
    if (text)
        td->Instruction = child(td, L"Static", text, SS_LEFT | SS_NOPREFIX, -1, td->InstructionFont);
    text = keep(td, strip_links(resolve_string(td, c->pszContent), links));
    if (text)
        td->Content = child(td, L"Static", text, SS_LEFT | SS_NOPREFIX, -1, td->ContentFont);
    text = keep(td, strip_links(resolve_string(td, c->pszExpandedInformation), links));
    td->Expanded = (c->dwFlags & TDF_EXPANDED_BY_DEFAULT) != 0;
    if (text) {
        HWND info = child(td, L"Static", text, SS_LEFT | SS_NOPREFIX, -1, td->ContentFont);
        if (c->dwFlags & TDF_EXPAND_FOOTER_AREA)
            td->ExpandedFooter = info;
        else
            td->Expanded_ = info;
        ShowWindow(info, td->Expanded ? SW_SHOW : SW_HIDE);
        td->Expando = child(td, L"Button", nullptr, BS_PUSHBUTTON | WS_TABSTOP, ID_EXPANDO, td->ContentFont);
    }
    if (c->dwFlags & (TDF_SHOW_PROGRESS_BAR | TDF_SHOW_MARQUEE_PROGRESS_BAR)) {
        td->Progress = child(td, PROGRESS_CLASSW, nullptr, (c->dwFlags & TDF_SHOW_MARQUEE_PROGRESS_BAR) ? PBS_MARQUEE : 0,
                             ID_PROGRESS, td->ContentFont);
    }
    td->Radios = c->cRadioButtons ? CcAlloc(c->cRadioButtons * sizeof(TD_BUTTON)) : nullptr;
    td->SelectedRadio = -1;
    for (UINT k = 0; k < c->cRadioButtons && td->Radios; k++) {
        TD_BUTTON *r = &td->Radios[td->RadioCount];
        r->Id = c->pRadioButtons[k].nButtonID;
        r->Text = resolve_string(td, c->pRadioButtons[k].pszButtonText);
        r->Hwnd = child(td, L"Button", r->Text, BS_RADIOBUTTON | BS_MULTILINE | WS_TABSTOP,
                        ID_RADIO_FIRST + td->RadioCount, td->ContentFont);
        if (r->Id == c->nDefaultRadioButton && td->SelectedRadio < 0)
            td->SelectedRadio = td->RadioCount;
        td->RadioCount++;
    }
    if (td->SelectedRadio < 0 && td->RadioCount && !(c->dwFlags & TDF_NO_DEFAULT_RADIO_BUTTON))
        td->SelectedRadio = 0;
    if (td->SelectedRadio >= 0)
        SendMessageW(td->Radios[td->SelectedRadio].Hwnd, BM_SETCHECK, BST_CHECKED, 0);

    int common = 0;
    for (SIZE_T k = 0; k < sizeof(g_common) / sizeof(g_common[0]); k++)
        if (c->dwCommonButtons & g_common[k].Flag)
            common++;
    td->Buttons = CcAlloc((c->cButtons + (SIZE_T)common + 1) * sizeof(TD_BUTTON));
    BOOL use_links = (c->dwFlags & (TDF_USE_COMMAND_LINKS | TDF_USE_COMMAND_LINKS_NO_ICON)) != 0;
    for (UINT k = 0; k < c->cButtons && td->Buttons; k++) {
        LPWSTR label = resolve_string(td, c->pButtons[k].pszButtonText);
        add_button(td, c->pButtons[k].nButtonID, label, use_links);
        CcFree(label);
    }
    for (SIZE_T k = 0; k < sizeof(g_common) / sizeof(g_common[0]) && td->Buttons; k++)
        if (c->dwCommonButtons & g_common[k].Flag)
            add_button(td, g_common[k].Id, g_common[k].Text, FALSE);
    if (!c->cButtons && !common && td->Buttons)
        add_button(td, IDOK, L"OK", FALSE);
    td->DefaultButton = td->ButtonCount ? 0 : -1;
    for (int k = 0; k < td->ButtonCount; k++)
        if (td->Buttons[k].Id == c->nDefaultButton) {
            td->DefaultButton = k;
            break;
        }
    if (td->DefaultButton >= 0) {
        HWND b = td->Buttons[td->DefaultButton].Hwnd;
        if (!td->Buttons[td->DefaultButton].CommandLink)
            SendMessageW(b, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        SendMessageW(td->Hwnd, DM_SETDEFID, (WPARAM)(ID_BUTTON_FIRST + td->DefaultButton), 0);
    }
    text = keep(td, resolve_string(td, c->pszVerificationText));
    td->Checked = (c->dwFlags & TDF_VERIFICATION_FLAG_CHECKED) != 0;
    if (text) {
        td->Verify = child(td, L"Button", text, BS_CHECKBOX | BS_MULTILINE | WS_TABSTOP, ID_VERIFY, td->ContentFont);
        SendMessageW(td->Verify, BM_SETCHECK, td->Checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    text = keep(td, strip_links(resolve_string(td, c->pszFooter), links));
    if (text)
        td->Footer = child(td, L"Static", text, SS_LEFT | SS_NOPREFIX, -1, td->ContentFont);
    if (td->Expando) {
        LPWSTR collapsed = keep(td, resolve_string(td, c->pszCollapsedControlText));
        LPWSTR expanded = keep(td, resolve_string(td, c->pszExpandedControlText));
        if (!collapsed && !expanded) {
            collapsed = keep(td, CcStrDupW(L"See details"));
            expanded = keep(td, CcStrDupW(L"Hide details"));
        } else if (!collapsed) {
            collapsed = expanded;
        } else if (!expanded) {
            expanded = collapsed;
        }
        SetWindowTextW(td->Expando, td->Expanded ? expanded : collapsed);
        SetPropW(td->Expando, L"TdCollapsed", collapsed);
        SetPropW(td->Expando, L"TdExpanded", expanded);
    }
    if (c->dwFlags & TDF_CALLBACK_TIMER) {
        td->TimerStart = GetTickCount();
        SetTimer(td->Hwnd, TD_TIMER, TD_TIMER_INTERVAL, nullptr);
    }
}

static void toggle_expando(TD_STATE *td)
{
    td->Expanded = !td->Expanded;
    HWND info = td->Expanded_ ? td->Expanded_ : td->ExpandedFooter;
    if (info)
        ShowWindow(info, td->Expanded ? SW_SHOW : SW_HIDE);
    SetWindowTextW(td->Expando, GetPropW(td->Expando, td->Expanded ? L"TdExpanded" : L"TdCollapsed"));
    size_dialog(td, FALSE);
    check(td, notify(td, TDN_EXPANDO_BUTTON_CLICKED, (WPARAM)td->Expanded, 0));
}

static void select_radio(TD_STATE *td, int index)
{
    for (int k = 0; k < td->RadioCount; k++)
        SendMessageW(td->Radios[k].Hwnd, BM_SETCHECK, k == index ? BST_CHECKED : BST_UNCHECKED, 0);
    td->SelectedRadio = index;
}

static int find_button(TD_STATE *td, int id)
{
    for (int k = 0; k < td->ButtonCount; k++)
        if (td->Buttons[k].Id == id)
            return k;
    return -1;
}

static int find_radio(TD_STATE *td, int id)
{
    for (int k = 0; k < td->RadioCount; k++)
        if (td->Radios[k].Id == id)
            return k;
    return -1;
}

static void set_element_text(TD_STATE *td, int element, LPCWSTR text)
{
    HWND target = nullptr;
    switch (element) {
    case TDE_CONTENT:
        target = td->Content;
        break;
    case TDE_EXPANDED_INFORMATION:
        target = td->Expanded_ ? td->Expanded_ : td->ExpandedFooter;
        break;
    case TDE_FOOTER:
        target = td->Footer;
        break;
    case TDE_MAIN_INSTRUCTION:
        target = td->Instruction;
        break;
    }
    if (!target)
        return;
    LPWSTR copy = strip_links(resolve_string(td, text), (td->Config.dwFlags & TDF_ENABLE_HYPERLINKS) != 0);
    SetWindowTextW(target, copy ? copy : L"");
    CcFree(copy);
}

/* ------------------------------------------------------------------ painting */

static void paint(TD_STATE *td, HDC dc)
{
    RECT client;
    GetClientRect(td->Hwnd, &client);
    RECT primary = client, secondary = client;
    primary.bottom = td->SecondaryTop;
    secondary.top = td->SecondaryTop;
    if (!td->Theme || FAILED(DrawThemeBackground(td->Theme, dc, 1, 0, &primary, nullptr)))
        FillRect(dc, &primary, GetSysColorBrush(COLOR_WINDOW));
    if (!td->Theme || FAILED(DrawThemeBackground(td->Theme, dc, 8, 0, &secondary, nullptr)))
        FillRect(dc, &secondary, GetSysColorBrush(COLOR_3DFACE));
    if (td->FooterTop >= 0) {
        RECT line = {client.left, td->FooterTop, client.right, td->FooterTop + 1};
        FillRect(dc, &line, GetSysColorBrush(COLOR_3DLIGHT));
    }
    if (td->MainIcon)
        DrawIconEx(dc, td->MainIconRect.left, td->MainIconRect.top, td->MainIcon,
                   td->MainIconRect.right - td->MainIconRect.left, td->MainIconRect.bottom - td->MainIconRect.top, 0,
                   nullptr, DI_NORMAL);
    if (td->FooterIcon && td->FooterTop >= 0)
        DrawIconEx(dc, td->FooterIconRect.left, td->FooterIconRect.top, td->FooterIcon,
                   td->FooterIconRect.right - td->FooterIconRect.left,
                   td->FooterIconRect.bottom - td->FooterIconRect.top, 0, nullptr, DI_NORMAL);
}

/* ------------------------------------------------------------------ dialog procedure */

static INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TD_STATE *td = (TD_STATE *)GetWindowLongPtrW(hwnd, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        td = (TD_STATE *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)td);
        td->Hwnd = hwnd;
        SendMessageW(hwnd, WM_SETICON, 0, 0);
        td->Theme = OpenThemeData(hwnd, L"TaskDialog");
        make_fonts(td);
        build_page(td);
        size_dialog(td, TRUE);
        if (td->Button)
            *td->Button = IDCANCEL;
        if (td->Verification)
            *td->Verification = td->Checked;
        if (!check(td, notify(td, TDN_DIALOG_CONSTRUCTED, 0, 0)))
            return TRUE;
        ShowWindow(hwnd, SW_SHOW);
        if (!(td->Config.dwFlags & TDF_NO_SET_FOREGROUND))
            SetForegroundWindow(hwnd);
        if (td->DefaultButton >= 0)
            SetFocus(td->Buttons[td->DefaultButton].Hwnd);
        check(td, notify(td, TDN_CREATED, 0, 0));
        return FALSE;
    }
    case WM_ERASEBKGND:
        if (td) {
            paint(td, (HDC)wparam);
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (td) {
            HWND control = (HWND)lparam;
            RECT r;
            GetWindowRect(control, &r);
            MapWindowPoints(nullptr, hwnd, (POINT *)&r, 2);
            BOOL in_primary = r.top < td->SecondaryTop;
            SetBkMode((HDC)wparam, TRANSPARENT);
            if (control == td->Instruction)
                SetTextColor((HDC)wparam, td->InstructionColor);
            return (INT_PTR)GetSysColorBrush(in_primary ? COLOR_WINDOW : COLOR_3DFACE);
        }
        break;
    case WM_COMMAND: {
        if (!td)
            break;
        int id = LOWORD(wparam);
        if (id >= ID_BUTTON_FIRST && id < ID_BUTTON_FIRST + td->ButtonCount) {
            click_button(td, td->Buttons[id - ID_BUTTON_FIRST].Id);
        } else if (id >= ID_RADIO_FIRST && id < ID_RADIO_FIRST + td->RadioCount) {
            select_radio(td, id - ID_RADIO_FIRST);
            check(td, notify(td, TDN_RADIO_BUTTON_CLICKED, (WPARAM)td->Radios[id - ID_RADIO_FIRST].Id, 0));
        } else if (id == ID_VERIFY) {
            td->Checked = !td->Checked;
            SendMessageW(td->Verify, BM_SETCHECK, td->Checked ? BST_CHECKED : BST_UNCHECKED, 0);
            check(td, notify(td, TDN_VERIFICATION_CLICKED, (WPARAM)td->Checked, 0));
        } else if (id == ID_EXPANDO) {
            toggle_expando(td);
        } else if (id == IDCANCEL && !lparam) {
            if (td->CancelAllowed)
                PostMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
        } else if ((id == IDOK || id == 0) && !lparam && td->DefaultButton >= 0) {
            click_button(td, td->Buttons[td->DefaultButton].Id);
        }
        return TRUE;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_CLOSE && td) {
            if (td->CancelAllowed)
                PostMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        if (td && td->CancelAllowed)
            PostMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
        return TRUE;
    case WM_TIMER:
        if (td && wparam == TD_TIMER) {
            HRESULT hr = notify(td, TDN_TIMER, (WPARAM)(GetTickCount() - td->TimerStart), 0);
            if (check(td, hr) && hr == S_FALSE)
                td->TimerStart = GetTickCount();
            return TRUE;
        }
        break;
    case WM_HELP:
        if (td && notify(td, TDN_HELP, 0, 0) == S_OK && !td->Config.pfCallback) {
            HWND owner = GetWindow(hwnd, GW_OWNER);
            if (owner) {
                HELPINFO hi = {sizeof(hi), HELPINFO_WINDOW, 9, hwnd, 0};
                DWORD pos = GetMessagePos();
                hi.MousePos.x = (short)LOWORD(pos);
                hi.MousePos.y = (short)HIWORD(pos);
                SendMessageW(owner, WM_HELP, 0, (LPARAM)&hi);
            }
        }
        return TRUE;
    case WM_DESTROY:
        if (td) {
            KillTimer(hwnd, TD_TIMER);
            notify(td, TDN_DESTROYED, 0, 0);
        }
        break;
    case TDM_CLICK_BUTTON:
        if (td)
            click_button(td, (int)wparam);
        return TRUE;
    case TDM_NAVIGATE_PAGE:
        if (td && lparam) {
            const TASKDIALOGCONFIG *next = (const TASKDIALOGCONFIG *)lparam;
            if (next->cbSize != sizeof(TASKDIALOGCONFIG)) {
                close_with(td, E_INVALIDARG);
                return TRUE;
            }
            destroy_page(td);
            td->Config = *next;
            build_page(td);
            size_dialog(td, FALSE);
            check(td, notify(td, TDN_NAVIGATED, 0, 0));
        }
        return TRUE;
    case TDM_SET_MARQUEE_PROGRESS_BAR:
        if (td && td->Progress) {
            DWORD s = (DWORD)GetWindowLongW(td->Progress, GWL_STYLE);
            SetWindowLongW(td->Progress, GWL_STYLE, (LONG)(wparam ? s | PBS_MARQUEE : s & ~PBS_MARQUEE));
        }
        return TRUE;
    case TDM_SET_PROGRESS_BAR_STATE:
    case TDM_SET_PROGRESS_BAR_RANGE:
    case TDM_SET_PROGRESS_BAR_POS:
    case TDM_SET_PROGRESS_BAR_MARQUEE:
        if (td && td->Progress) {
            UINT forward = msg == TDM_SET_PROGRESS_BAR_STATE   ? PBM_SETSTATE
                           : msg == TDM_SET_PROGRESS_BAR_RANGE ? PBM_SETRANGE
                           : msg == TDM_SET_PROGRESS_BAR_POS   ? PBM_SETPOS
                                                               : PBM_SETMARQUEE;
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, SendMessageW(td->Progress, forward, wparam, lparam));
        }
        return TRUE;
    case TDM_SET_ELEMENT_TEXT:
    case TDM_UPDATE_ELEMENT_TEXT:
        if (td) {
            set_element_text(td, (int)wparam, (LPCWSTR)lparam);
            if (msg == TDM_SET_ELEMENT_TEXT)
                size_dialog(td, FALSE);
        }
        return TRUE;
    case TDM_CLICK_RADIO_BUTTON:
        if (td) {
            int k = find_radio(td, (int)wparam);
            if (k >= 0 && check(td, notify(td, TDN_RADIO_BUTTON_CLICKED, wparam, 0)))
                select_radio(td, k);
        }
        return TRUE;
    case TDM_ENABLE_BUTTON:
        if (td) {
            int k = find_button(td, (int)wparam);
            if (k >= 0)
                EnableWindow(td->Buttons[k].Hwnd, lparam != 0);
        }
        return TRUE;
    case TDM_ENABLE_RADIO_BUTTON:
        if (td) {
            int k = find_radio(td, (int)wparam);
            if (k >= 0)
                EnableWindow(td->Radios[k].Hwnd, lparam != 0);
        }
        return TRUE;
    case TDM_CLICK_VERIFICATION:
        if (td) {
            td->Checked = wparam != 0;
            if (td->Verify) {
                SendMessageW(td->Verify, BM_SETCHECK, td->Checked ? BST_CHECKED : BST_UNCHECKED, 0);
                if (lparam)
                    SetFocus(td->Verify);
            }
            check(td, notify(td, TDN_VERIFICATION_CLICKED, (WPARAM)td->Checked, 0));
        }
        return TRUE;
    case TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE:
        if (td) {
            int k = find_button(td, (int)wparam);
            if (k >= 0)
                SendMessageW(td->Buttons[k].Hwnd, BCM_SETSHIELD, 0, lparam);
        }
        return TRUE;
    case TDM_UPDATE_ICON:
        if (td) {
            BOOL footer = wparam == TDIE_ICON_FOOTER;
            BOOL handle = (td->Config.dwFlags & (footer ? TDF_USE_HICON_FOOTER : TDF_USE_HICON_MAIN)) != 0;
            HICON *slot = footer ? &td->FooterIcon : &td->MainIcon;
            BOOL *own = footer ? &td->OwnFooterIcon : &td->OwnMainIcon;
            if (*own && *slot)
                DestroyIcon(*slot);
            *slot = handle ? (HICON)lparam : load_icon(td, (PCWSTR)lparam, footer);
            *own = !handle;
            size_dialog(td, FALSE);
        }
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ entry points */

WINCOMMCTRLAPI HRESULT WINAPI TaskDialogIndirect(const TASKDIALOGCONFIG *config, int *button, int *radio_button,
                                                 BOOL *verification)
{
    if (button)
        *button = 0;
    if (!config || config->cbSize != sizeof(TASKDIALOGCONFIG))
        return E_INVALIDARG;
    if ((config->cButtons && !config->pButtons) || (config->cRadioButtons && !config->pRadioButtons))
        return E_INVALIDARG;
    if ((config->dwFlags & TDF_USE_COMMAND_LINKS) && !config->pButtons)
        return E_INVALIDARG;
    TD_STATE *td = CcAlloc(sizeof(*td));
    if (!td)
        return E_OUTOFMEMORY;
    td->Config = *config;
    td->Button = button;
    td->Radio = radio_button;
    td->Verification = verification;
    td->Result = S_OK;
    /* an empty template: the controls come at WM_INITDIALOG. */
    struct {
        DLGTEMPLATE t;
        WORD menu, cls, title;
    } tmpl = {};
    tmpl.t.style = WS_POPUP | WS_CLIPCHILDREN | WS_CAPTION | DS_MODALFRAME;
    if (!(config->dwFlags & TDF_NO_SET_FOREGROUND))
        tmpl.t.style |= DS_SETFOREGROUND;
    if (cancel_allowed(config))
        tmpl.t.style |= WS_SYSMENU;
    if (config->dwFlags & TDF_RTL_LAYOUT)
        tmpl.t.dwExtendedStyle |= WS_EX_LAYOUTRTL;
    tmpl.t.cx = 10;
    tmpl.t.cy = 10;
    INT_PTR r = DialogBoxIndirectParamW(config->hInstance ? config->hInstance : Comctl32Instance, &tmpl.t,
                                        config->hwndParent, dialog_proc, (LPARAM)td);
    HRESULT hr = td->Result;
    if (r == -1)
        hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_INVALID_PARAMETER);
    if (td->InstructionFont)
        DeleteObject(td->InstructionFont);
    if (td->ContentFont)
        DeleteObject(td->ContentFont);
    if (td->Theme)
        CloseThemeData(td->Theme);
    destroy_page(td);
    CcFree(td);
    return hr;
}

WINCOMMCTRLAPI HRESULT WINAPI TaskDialog(HWND parent, HINSTANCE inst, PCWSTR title, PCWSTR instruction,
                                         PCWSTR content, TASKDIALOG_COMMON_BUTTON_FLAGS buttons, PCWSTR icon,
                                         int *button)
{
    TASKDIALOGCONFIG config = {};
    config.cbSize = sizeof(config);
    config.hwndParent = parent;
    config.hInstance = inst;
    config.dwCommonButtons = buttons;
    config.pszWindowTitle = title;
    config.pszMainInstruction = instruction;
    config.pszContent = content;
    config.pszMainIcon = icon;
    return TaskDialogIndirect(&config, button, nullptr, nullptr);
}
