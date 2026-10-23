/* MessageBox builds an in memory dialog template sized like windows 11's and runs it with its own dialog procedure. */
#include "user32_private.h"

#define MB_HELP_FLAG 0x00004000u
#define MB_TIMEDOUT 32000
#ifndef HELPINFO_WINDOW
#define HELPINFO_WINDOW 1
#endif
#ifndef ERROR_INVALID_MSGBOX_STYLE
#define ERROR_INVALID_MSGBOX_STYLE 1438L
#endif
#define MB_ICON_STATIC_ID 20

/* the session button strings: user32.dll.mui string 800 + i, control id i + 1. */
#define MB_STRING_COUNT 11
#define MB_STRING_BASE 800

static constexpr BYTE g_type_count[7] = {1, 2, 3, 3, 2, 2, 3};
static constexpr BYTE g_type_first[7] = {0, 2, 5, 12, 9, 16, 19};
/* indexes into the string table, each group followed by the help button. */
static constexpr BYTE g_buttons[] = {0, 8, 0, 1, 8, 2, 3, 4, 8, 5, 6, 8, 5, 6, 1, 8, 3, 1, 8, 1, 9, 10, 8};

typedef struct MSGBOX {
    MSGBOXPARAMSW Params;
    UINT Count;
    UINT Default;
    UINT CancelKind; /* 1: MB_OK, 2: an IDCANCEL button exists, 0: none. */
    int Ids[4];
    WCHAR Texts[4][16];
    DWORD Timeout;
    RECT Command;    /* the gray band, in client coordinates. */
    HICON UserIcon;
} MSGBOX;

static LONG g_open_boxes;

static void load_button_string(UINT index, LPWSTR buffer)
{
    if (!UserLoadSystemString(MB_STRING_BASE + index, buffer, 15)) {
        static const PCWSTR fallback[MB_STRING_COUNT] = {L"OK",      L"Cancel", L"&Abort",     L"&Retry",
                                                         L"&Ignore", L"&Yes",   L"&No",        L"&Close",
                                                         L"Help",    L"&Try Again", L"&Continue"};
        wcscpy(buffer, fallback[index]);
    }
}

static int text_extent(HDC dc, LPCWSTR text, int count)
{
    SIZE size = {0, 0};
    if (count)
        GetTextExtentPoint32W(dc, text, count, &size);
    return size.cx;
}

/* the width shared by all buttons: 50 dialog units, or the longest session string and two "0". */
static int max_button_width(HDC dc, int cx)
{
    WCHAR longest[16] = L"";
    int longest_len = -1;
    for (UINT i = 0; i < MB_STRING_COUNT; i++) {
        WCHAR s[16];
        load_button_string(i, s);
        int n = (int)wcslen(s);
        if (n > longest_len) {
            longest_len = n;
            wcscpy(longest, s);
        }
    }
    WCHAR stripped[16];
    int n = 0;
    for (int i = 0; longest[i]; i++)
        if (longest[i] != L'&')
            stripped[n++] = longest[i];
    int width = text_extent(dc, stripped, n) + 2 * text_extent(dc, L"0", 1);
    int minimum = (50 * cx + 2) / 4;
    return width > minimum ? width : minimum;
}

/* ------------------------------------------------------------------ template */

typedef struct TEMPLATE_BUFFER {
    PUCHAR Data;
    SIZE_T Size;
    SIZE_T Capacity;
} TEMPLATE_BUFFER;

static void put(TEMPLATE_BUFFER *t, const void *p, SIZE_T n)
{
    if (t->Size + n > t->Capacity)
        return;
    memcpy(t->Data + t->Size, p, n);
    t->Size += n;
}

static void put_word(TEMPLATE_BUFFER *t, WORD w)
{
    put(t, &w, sizeof(w));
}

static void put_string(TEMPLATE_BUFFER *t, LPCWSTR s)
{
    put(t, s, (wcslen(s) + 1) * sizeof(WCHAR));
}

static void align(TEMPLATE_BUFFER *t)
{
    while (t->Size & 3)
        t->Data[t->Size++] = 0;
}

/* pixels to dialog units, rounded like windows. */
static short to_dlu_x(int px, int cx)
{
    return (short)((4 * px + cx / 2) / cx);
}

static short to_dlu_y(int py, int cy)
{
    return (short)((8 * py + cy / 2) / cy);
}

static void put_item(TEMPLATE_BUFFER *t, DWORD style, DWORD ex, int x, int y, int w, int h, WORD id, WORD cls,
                     LPCWSTR text, WORD ordinal, int cx, int cy, BOOL grow)
{
    align(t);
    DLGITEMTEMPLATE it;
    it.style = style;
    it.dwExtendedStyle = ex;
    it.x = to_dlu_x(x, cx);
    it.y = to_dlu_y(y, cy);
    it.cx = (short)(to_dlu_x(w, cx) + (grow ? 1 : 0));
    it.cy = (short)(to_dlu_y(h, cy) + (grow ? 1 : 0));
    it.id = id;
    put(t, &it, sizeof(it));
    put_word(t, 0xFFFF);
    put_word(t, cls);
    if (text) {
        put_string(t, text);
    } else {
        put_word(t, 0xFFFF);
        put_word(t, ordinal);
    }
    put_word(t, 0);
}

/* ------------------------------------------------------------------ the dialog procedure */

static void end_box(HWND hwnd, MSGBOX *mb, INT_PTR result)
{
    if (mb && mb->Timeout != (DWORD)-1)
        KillTimer(hwnd, 0);
    EndDialog(hwnd, result);
}

static void send_help(HWND hwnd, MSGBOX *mb)
{
    HELPINFO hi;
    memset(&hi, 0, sizeof(hi));
    hi.cbSize = sizeof(hi);
    hi.iContextType = HELPINFO_WINDOW;
    hi.iCtrlId = IDHELP;
    hi.hItemHandle = hwnd;
    hi.dwContextId = mb->Params.dwContextHelpId;
    DWORD pos = GetMessagePos();
    hi.MousePos = (POINT){(SHORT)LOWORD(pos), (SHORT)HIWORD(pos)};
    if (mb->Params.lpfnMsgBoxCallback) {
        mb->Params.lpfnMsgBoxCallback(&hi);
        return;
    }
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner)
        SendMessageW(owner, WM_HELP, 0, (LPARAM)&hi);
}

static INT_PTR CALLBACK msgbox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    MSGBOX *mb = (MSGBOX *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_INITDIALOG: {
        mb = (MSGBOX *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)mb);
        InterlockedIncrement(&g_open_boxes);
        if (mb->Params.dwContextHelpId)
            SetWindowContextHelpId(hwnd, (DWORD)mb->Params.dwContextHelpId);
        if (mb->Params.dwStyle & MB_TOPMOST)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        if (mb->UserIcon)
            SendDlgItemMessageW(hwnd, MB_ICON_STATIC_ID, STM_SETICON, (WPARAM)mb->UserIcon, 0);
        /* the buttons are the first children. */
        HWND button = GetWindow(hwnd, GW_CHILD);
        for (UINT i = 0; i < mb->Default && button; i++)
            button = GetWindow(button, GW_HWNDNEXT);
        if (button)
            SetFocus(button);
        if (mb->CancelKind == 0)
            DeleteMenu(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
        if (mb->CancelKind == 1) {
            /* the single OK button answers Escape and the close box. */
            HWND ok = GetDlgItem(hwnd, IDOK);
            if (ok)
                SetWindowLongPtrW(ok, GWLP_ID, IDCANCEL);
        }
        if (mb->Timeout != (DWORD)-1 && !SetTimer(hwnd, 0, mb->Timeout, nullptr))
            mb->Timeout = (DWORD)-1;
        return FALSE;
    }
    case WM_CTLCOLORDLG:
        return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
        SetBkMode((HDC)wparam, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc && mb) {
            /* the gray band runs from its top to the bottom of the client, full width, no separator. */
            RECT client, band;
            GetClientRect(hwnd, &client);
            band = (RECT){0, mb->Command.top, client.right, client.bottom};
            HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_MENU));
            FillRect(dc, &band, brush);
            DeleteObject(brush);
        }
        EndPaint(hwnd, &ps);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDOK:
        case IDCANCEL:
            if (!GetDlgItem(hwnd, LOWORD(wparam)))
                return FALSE;
            end_box(hwnd, mb, LOWORD(wparam));
            return TRUE;
        case IDABORT:
        case IDRETRY:
        case IDIGNORE:
        case IDYES:
        case IDNO:
        case IDTRYAGAIN:
        case IDCONTINUE:
            end_box(hwnd, mb, LOWORD(wparam));
            return TRUE;
        case IDHELP:
            if (mb)
                send_help(hwnd, mb);
            return TRUE;
        }
        return FALSE;
    case WM_HELP:
        if (mb)
            send_help(hwnd, mb);
        return TRUE;
    case WM_TIMER:
        end_box(hwnd, mb, MB_TIMEDOUT);
        return TRUE;
    case WM_NCDESTROY:
        if (mb)
            InterlockedDecrement(&g_open_boxes);
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ layout */

static int box_layout_and_run(MSGBOX *mb)
{
    MSGBOXPARAMSW *p = &mb->Params;
    UINT style = p->dwStyle;
    UINT dpi = GetDpiForSystem();
    LPCWSTR text = p->lpszText ? p->lpszText : L"";
    LPCWSTR caption = p->lpszCaption;
    WCHAR text_buf[1024], caption_buf[256];
    if (IS_INTRESOURCE(text)) {
        text_buf[0] = 0;
        if (p->hInstance)
            LoadStringW(p->hInstance, (UINT)(ULONG_PTR)text, text_buf, 1024);
        text = text_buf;
    }
    if (IS_INTRESOURCE(caption)) {
        caption_buf[0] = 0;
        if (p->hInstance)
            LoadStringW(p->hInstance, (UINT)(ULONG_PTR)caption, caption_buf, 256);
        caption = caption_buf;
    }

    DWORD ex_text = (style >> 7) & 0x1000; /* MB_RIGHT gives WS_EX_RIGHT. */
    BOOL right = (style & MB_RIGHT) != 0;
    if ((style & MB_RTLREADING) || (text[0] == 0x200F && text[1] == 0x200F)) {
        ex_text = WS_EX_LAYOUTRTL;
        if (style & MB_RTLREADING)
            right = !right;
    }

    /* icon choice: hand, question, exclamation, asterisk, or a user icon. */
    WORD icon_ordinal = 0;
    switch (style & MB_ICONMASK) {
    case MB_ICONHAND:
        icon_ordinal = 32513;
        break;
    case MB_ICONQUESTION:
        icon_ordinal = 32514;
        break;
    case MB_ICONEXCLAMATION:
        icon_ordinal = 32515;
        break;
    case MB_ICONASTERISK:
        icon_ordinal = 32516;
        break;
    case MB_USERICON:
        icon_ordinal = 32513;
        mb->UserIcon = LoadImageW(p->hInstance, p->lpszIcon, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
        break;
    }
    BOOL has_icon = icon_ordinal != 0;

    HDC dc = GetDCEx(nullptr, nullptr, DCX_WINDOW | DCX_CACHE);
    HFONT font = UserGetMessageBoxFont(dpi);
    HGDIOBJ old_font = SelectObject(dc, font);
    LONG cy = 0;
    LONG cx = GdiGetCharDimensions(dc, nullptr, &cy);
    if (!cx) {
        SelectObject(dc, old_font);
        ReleaseDC(nullptr, dc);
        DeleteObject(font);
        return 0;
    }

    int C = GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
    int ICX = GetSystemMetricsForDpi(SM_CXICON, dpi), ICY = GetSystemMetricsForDpi(SM_CYICON, dpi);
    int CXS = GetSystemMetricsForDpi(SM_CXSIZE, dpi), CYS = GetSystemMetricsForDpi(SM_CYSIZE, dpi);
    int M = (14 * cx + 2) >> 2, G = (5 * cx + 2) >> 2, T7 = (7 * cx + 2) >> 2;
    int V14 = (14 * cy + 4) >> 3, V9 = (9 * cy + 4) >> 3, V54 = (54 * cy + 4) >> 3;
    int BW = max_button_width(dc, cx);
    int n = (int)mb->Count;
    int BT = G * (n - 1) + BW * n;
    int IW = has_icon ? ICX + 3 * cx : 0, IH = has_icon ? ICY : 0;

    /* the caption must fit with room for the caption buttons. */
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
    HFONT caption_font = CreateFontIndirectW(&ncm.lfCaptionFont);
    SelectObject(dc, caption_font);
    int CM = text_extent(dc, caption, (int)wcslen(caption)) + 2 * CXS;
    SelectObject(dc, font);
    DeleteObject(caption_font);

    MONITORINFO mi = {sizeof(mi)};
    HMONITOR monitor = MonitorFromWindow(p->hwndOwner, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(monitor, &mi);
    RECT work = mi.rcWork;
    int WW = work.right - work.left, WH = work.bottom - work.top;

    /* try a width of 278 dialog units, then 5/8, 6/8 and 7/8 of the work area. */
    const UINT measure = DT_WORDBREAK | DT_EXPANDTABS | DT_EXTERNALLEADING | DT_CALCRECT | DT_NOPREFIX | DT_EDITCONTROL;
    int cand[4] = {(278 * cx + 2) >> 2, (5 * WW + 4) / 8, (6 * WW + 4) / 8, (7 * WW + 4) / 8};
    int W = 0, H = 0, TW = 0, TH = 0;
    for (int i = 0; i < 4; i++) {
        int w = cand[i] - 6;
        RECT r = {0, 0, w - 2 * M - IW, WH};
        DrawTextW(dc, text, -1, &r, measure);
        int need = (r.right - r.left) + 2 * M + IW;
        int fit = need > CM ? need : CM;
        if (w >= fit)
            w = fit;
        if (w < BT + 2 * M)
            w = BT + 2 * M;
        TW = w - 2 * M - IW;
        r = (RECT){0, 0, TW, WH};
        TH = DrawTextW(dc, text, -1, &r, measure);
        W = w + 6;
        H = C + V54 + (IH > TH ? IH : TH) + 6;
        if (W <= WW && H <= WH)
            break;
    }
    SelectObject(dc, old_font);
    ReleaseDC(nullptr, dc);

    int content = IH > TH ? IH : TH;
    int band_top = content + 2 * V14;
    mb->Command = (RECT){0, band_top, W, band_top + 2 * V9 + V14};

    /* centered on the work area, cascading over boxes already open. */
    LONG k = g_open_boxes;
    int x = CXS * k + (work.left + work.right - W) / 2;
    if (x < work.left)
        x = work.left;
    int y = CYS * k + (work.top + work.bottom - H) / 2;
    if (y < work.top)
        y = work.top;
    if (x + W > work.right)
        x = work.right - W - 2;
    if (y + H > work.bottom) {
        y = work.bottom - H - 2;
        if (y < work.top)
            y = mi.rcMonitor.bottom - H - 2;
    }

    /* the template, in dialog units. */
    SIZE_T text_len = wcslen(text), caption_len = wcslen(caption);
    TEMPLATE_BUFFER t = {nullptr, 0, 512 + (text_len + caption_len) * sizeof(WCHAR) + mb->Count * 64};
    t.Data = UserAlloc(t.Capacity);
    if (!t.Data) {
        DeleteObject(font);
        return 0;
    }
    DWORD dlg_style = (style & MB_SYSTEMMODAL) ? 0x80C80347u : 0x80C801C5u;
    if (style & MB_SETFOREGROUND)
        dlg_style |= DS_SETFOREGROUND;
    DLGTEMPLATE hdr;
    hdr.style = dlg_style;
    hdr.dwExtendedStyle = ex_text;
    hdr.cdit = (WORD)(mb->Count + (has_icon ? 1 : 0) + 1);
    hdr.x = (short)((4 * (x + 3) + cx / 2) / cx);
    hdr.y = (short)((8 * (y + C + 3) + cy / 2) / cy);
    hdr.cx = (short)((4 * (W - 6) + cx / 2) / cx);
    hdr.cy = (short)((8 * (H - C - 6) + cy / 2) / cy);
    put(&t, &hdr, sizeof(hdr));
    put_word(&t, 0); /* menu. */
    put_word(&t, 0); /* class. */
    put_string(&t, caption);
    put_word(&t, 0x7FFF); /* the message box font. */
    put_word(&t, 0);

    int bx = W - M - BT, by = H - V9 - C - V14;
    for (UINT i = 0; i < mb->Count; i++) {
        DWORD s = (i == 0 ? 0x50030000u : 0x50010000u) | (i == mb->Default ? BS_DEFPUSHBUTTON : 0);
        put_item(&t, s, 0, bx + (int)i * (BW + G), by, BW, V14, (WORD)mb->Ids[i], 0x80, mb->Texts[i], 0, cx, cy, FALSE);
    }
    if (has_icon)
        put_item(&t, 0x50020003u, 0, M, V14, 0, 0, MB_ICON_STATIC_ID, 0x82, nullptr, icon_ordinal, cx, cy, FALSE);
    int dy = TH < IH ? (IH - TH) / 2 : 0;
    int tx = right ? W - CXS - TW : IW + T7;
    put_item(&t, right ? 0x50022082u : 0x50022080u, ex_text, tx, V14 + dy, TW, TH, 0xFFFF, 0x82, text, 0, cx, cy,
             TRUE);

    /* no beep for a user icon or without icon. */
    if (!(style & MB_USERICON) && (style & MB_ICONMASK))
        MessageBeep(style & MB_ICONMASK);

    HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    HWND owner = p->hwndOwner;
    if (owner) {
        HWND root = GetAncestor(owner, GA_ROOT);
        if (root)
            owner = root;
    }
    HWND dialog = UserCreateDialog(User32Instance, (LPCDLGTEMPLATEW)t.Data, owner, msgbox_proc, (LPARAM)mb, FALSE, TRUE);
    INT_PTR result = dialog ? UserModalLoop(dialog, owner) : -1;
    SetCursor(old_cursor);
    UserFree(t.Data);
    DeleteObject(font);
    if (result == -1)
        return 0;
    if ((style & MB_TYPEMASK) == MB_OK && result == IDCANCEL)
        return IDOK;
    return (int)result;
}

static int message_box_worker(MSGBOX *mb, WORD language)
{
    UINT style = mb->Params.dwStyle;
    if (mb->Timeout == 0)
        mb->Timeout = (DWORD)-1;
    if ((style & MB_TYPEMASK) > 6) {
        SetLastError(ERROR_INVALID_MSGBOX_STYLE);
        return 0;
    }
    UINT type = style & MB_TYPEMASK;
    UINT n = g_type_count[type] + ((style & MB_HELP_FLAG) ? 1 : 0);
    UINT d = (style >> 8) & 0xF;
    if (d >= n)
        d = 0;
    if (!mb->Params.lpszCaption) {
        static WCHAR error[16];
        if (!error[0] && !UserLoadSystemString(2, error, 16))
            wcscpy(error, L"Error");
        mb->Params.lpszCaption = error;
    }
    if (mb->Params.hwndOwner && !IsWindow(mb->Params.hwndOwner)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    const BYTE *list = &g_buttons[g_type_first[type]];
    UINT count = 0;
    for (UINT i = 0; i < g_type_count[type]; i++, count++) {
        mb->Ids[count] = list[i] + 1;
        load_button_string(list[i], mb->Texts[count]);
    }
    if (style & MB_HELP_FLAG) {
        mb->Ids[count] = IDHELP;
        load_button_string(8, mb->Texts[count]);
        count++;
    }
    mb->Count = count;
    mb->Default = d;
    mb->CancelKind = type == MB_OK ? 1 : 0;
    for (UINT i = 0; i < count && type != MB_OK; i++)
        if (mb->Ids[i] == IDCANCEL)
            mb->CancelKind = 2;
    (void)language;
    return box_layout_and_run(mb);
}

WINUSERAPI int WINAPI MessageBoxTimeoutW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type, WORD language,
                                         DWORD timeout)
{
    MSGBOX mb;
    memset(&mb, 0, sizeof(mb));
    mb.Params.cbSize = sizeof(mb.Params);
    mb.Params.hwndOwner = owner;
    mb.Params.lpszText = text;
    mb.Params.lpszCaption = caption;
    mb.Params.dwStyle = type;
    mb.Timeout = timeout;
    return message_box_worker(&mb, language);
}

WINUSERAPI int WINAPI MessageBoxTimeoutA(HWND owner, LPCSTR text, LPCSTR caption, UINT type, WORD language,
                                         DWORD timeout)
{
    LPWSTR wtext = UserAtoW(text), wcaption = UserAtoW(caption);
    int r = MessageBoxTimeoutW(owner, wtext, wcaption, type, language, timeout);
    UserFreeConverted(wtext, text);
    UserFreeConverted(wcaption, caption);
    return r;
}

WINUSERAPI int WINAPI MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type)
{
    return MessageBoxTimeoutW(owner, text, caption, type, 0, (DWORD)-1);
}

WINUSERAPI int WINAPI MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type)
{
    return MessageBoxTimeoutA(owner, text, caption, type, 0, (DWORD)-1);
}

WINUSERAPI int WINAPI MessageBoxExW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type, WORD language)
{
    return MessageBoxTimeoutW(owner, text, caption, type, language, (DWORD)-1);
}

WINUSERAPI int WINAPI MessageBoxExA(HWND owner, LPCSTR text, LPCSTR caption, UINT type, WORD language)
{
    return MessageBoxTimeoutA(owner, text, caption, type, language, (DWORD)-1);
}

WINUSERAPI int WINAPI MessageBoxIndirectW(const MSGBOXPARAMSW *params)
{
    MSGBOX mb;
    memset(&mb, 0, sizeof(mb));
    mb.Params = *params;
    mb.Timeout = (DWORD)-1;
    return message_box_worker(&mb, 0);
}

WINUSERAPI int WINAPI MessageBoxIndirectA(const void *params)
{
    /* MSGBOXPARAMSA has the same layout with ansi strings; the icon name is not converted, like windows. */
    MSGBOXPARAMSW w;
    memcpy(&w, params, sizeof(w));
    LPCSTR text = (LPCSTR)w.lpszText, caption = (LPCSTR)w.lpszCaption;
    w.lpszText = UserAtoW(text);
    w.lpszCaption = UserAtoW(caption);
    int r = MessageBoxIndirectW(&w);
    UserFreeConverted(w.lpszText, text);
    UserFreeConverted(w.lpszCaption, caption);
    return r;
}
