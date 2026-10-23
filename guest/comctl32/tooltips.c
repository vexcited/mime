/*
 * tooltips, tooltips_class32: a popup showing the text of the tool under the mouse after a delay. a tool is a
 * window or a rectangle of one. the mouse reaches the control through TTM_RELAYEVENT or the subclass
 * TTF_SUBCLASS installs. tracking tooltips show where they are told.
 */
#include "comctl32_private.h"

#define TIMER_SHOW 1
#define TIMER_POP 2
#define TIMER_CHECK 3
#define TOOLINFO_V3_SIZE ((UINT)sizeof(TTTOOLINFOW))
#define TOOLINFO_V1_SIZE ((UINT)TTTOOLINFOW_V1_SIZE)

typedef struct TOOL {
    UINT Flags;
    HWND Hwnd;
    UINT_PTR Id;
    RECT Rect;
    HINSTANCE Inst;
    LPWSTR Text;
    LPARAM Param;
    BOOL Unicode;
} TOOL;

typedef struct TOOLTIPS {
    HWND Hwnd;
    DWORD Style;
    TOOL *Tools;
    int Count;
    BOOL Active;
    int Current, Tracking;
    BOOL TrackActive;
    POINT TrackPos;
    int Delay[4];
    int MaxWidth;
    RECT Margin;
    COLORREF Back, Fore;
    HFONT Font;
    HTHEME Theme;
    LPWSTR Title;
    UINT TitleIcon;
    WCHAR Shown[1024];
    POINT LastPos;
} TOOLTIPS;

static TOOLTIPS *get_data(HWND hwnd)
{
    return (TOOLTIPS *)GetWindowLongPtrW(hwnd, 0);
}

static void default_delays(TOOLTIPS *t)
{
    int dct = (int)GetDoubleClickTime();
    t->Delay[TTDT_INITIAL] = dct;
    t->Delay[TTDT_AUTOPOP] = 10 * dct;
    t->Delay[TTDT_RESHOW] = dct / 5;
}

static int find_tool(TOOLTIPS *t, HWND hwnd, UINT_PTR id)
{
    for (int k = 0; k < t->Count; k++)
        if (t->Tools[k].Hwnd == hwnd && t->Tools[k].Id == id)
            return k;
    return -1;
}

/* finds a tool by window and id. */
static int tool_of(TOOLTIPS *t, const TTTOOLINFOW *ti)
{
    return ti ? find_tool(t, ti->hwnd, ti->uId) : -1;
}

static HFONT tip_font(TOOLTIPS *t)
{
    if (t->Font)
        return t->Font;
    static HFONT font;
    if (!font) {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            font = CreateFontIndirectW(&ncm.lfStatusFont);
        if (!font)
            font = GetStockObject(DEFAULT_GUI_FONT);
    }
    return font;
}

static BOOL set_tool_text(TOOL *tool, LPCWSTR text, BOOL ansi)
{
    LPWSTR copy = nullptr;
    if (text == LPSTR_TEXTCALLBACKW || IS_INTRESOURCE(text)) {
        copy = (LPWSTR)text;
    } else if (text) {
        copy = ansi ? CcStrDupAtoW((LPCSTR)text) : CcStrDupW(text);
        if (!copy)
            return FALSE;
    }
    if (tool->Text != LPSTR_TEXTCALLBACKW && !IS_INTRESOURCE(tool->Text))
        CcFree(tool->Text);
    tool->Text = copy;
    return TRUE;
}

static void free_tool(TOOL *tool)
{
    set_tool_text(tool, nullptr, FALSE);
}

static LRESULT CALLBACK tool_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);

/* asks the tool's window when the text is a callback. */
static void tool_text(TOOLTIPS *t, int index, LPWSTR out, int max)
{
    TOOL *tool = &t->Tools[index];
    out[0] = 0;
    if (tool->Text == LPSTR_TEXTCALLBACKW) {
        if (tool->Unicode) {
            NMTTDISPINFOW nm = {};
            nm.hdr.hwndFrom = t->Hwnd;
            nm.hdr.idFrom = tool->Id;
            nm.hdr.code = TTN_GETDISPINFOW;
            nm.uFlags = tool->Flags;
            nm.lParam = tool->Param;
            SendMessageW(tool->Hwnd, WM_NOTIFY, tool->Id, (LPARAM)&nm);
            if (nm.lpszText && IS_INTRESOURCE(nm.lpszText))
                LoadStringW(nm.hinst, (UINT)(ULONG_PTR)nm.lpszText, out, max);
            else
                CcCopyText(out, max, nm.lpszText ? nm.lpszText : nm.szText);
            if (nm.uFlags & TTF_DI_SETITEM)
                set_tool_text(tool, out, FALSE);
        } else {
            NMTTDISPINFOA nm = {};
            nm.hdr.hwndFrom = t->Hwnd;
            nm.hdr.idFrom = tool->Id;
            nm.hdr.code = TTN_GETDISPINFOA;
            nm.uFlags = tool->Flags;
            nm.lParam = tool->Param;
            SendMessageW(tool->Hwnd, WM_NOTIFY, tool->Id, (LPARAM)&nm);
            if (nm.lpszText && IS_INTRESOURCE(nm.lpszText))
                LoadStringW(nm.hinst, (UINT)(ULONG_PTR)nm.lpszText, out, max);
            else
                MultiByteToWideChar(CP_ACP, 0, nm.lpszText ? nm.lpszText : nm.szText, -1, out, max);
            out[max - 1] = 0;
        }
    } else if (tool->Text && IS_INTRESOURCE(tool->Text)) {
        LoadStringW(tool->Inst, (UINT)(ULONG_PTR)tool->Text, out, max);
    } else {
        CcCopyText(out, max, tool->Text);
    }
}

/* ------------------------------------------------------------------ the bubble */

static void bubble_size(TOOLTIPS *t, LPCWSTR text, SIZE *size)
{
    HDC dc = GetDC(t->Hwnd);
    HGDIOBJ old = SelectObject(dc, tip_font(t));
    RECT r = {0, 0, t->MaxWidth > 0 ? t->MaxWidth : 10000, 0};
    UINT flags = DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS | (t->MaxWidth > 0 ? DT_WORDBREAK : DT_SINGLELINE);
    DrawTextW(dc, text, -1, &r, flags);
    SelectObject(dc, old);
    ReleaseDC(t->Hwnd, dc);
    size->cx = r.right + 2 * 4 + t->Margin.left + t->Margin.right;
    size->cy = r.bottom + 2 * 3 + t->Margin.top + t->Margin.bottom;
}

static void hide(TOOLTIPS *t)
{
    KillTimer(t->Hwnd, TIMER_POP);
    KillTimer(t->Hwnd, TIMER_CHECK);
    if (t->Current >= 0 && IsWindowVisible(t->Hwnd)) {
        NMHDR hdr = {t->Hwnd, t->Tools[t->Current].Id, TTN_POP};
        SendMessageW(t->Tools[t->Current].Hwnd, WM_NOTIFY, hdr.idFrom, (LPARAM)&hdr);
    }
    ShowWindow(t->Hwnd, SW_HIDE);
    t->Current = -1;
}

static void show(TOOLTIPS *t, int index, POINT at)
{
    tool_text(t, index, t->Shown, 1024);
    if (!t->Shown[0])
        return;
    t->Current = index;
    SIZE size;
    bubble_size(t, t->Shown, &size);
    NMHDR hdr = {t->Hwnd, t->Tools[index].Id, TTN_SHOW};
    if (!SendMessageW(t->Tools[index].Hwnd, WM_NOTIFY, hdr.idFrom, (LPARAM)&hdr)) {
        int x = at.x, y = at.y;
        if (!(t->Tools[index].Flags & TTF_TRACK)) {
            y += GetSystemMetrics(SM_CYCURSOR) * 3 / 4;
            if (t->Tools[index].Flags & TTF_CENTERTIP) {
                RECT r = t->Tools[index].Rect;
                if (t->Tools[index].Flags & TTF_IDISHWND)
                    GetClientRect((HWND)t->Tools[index].Id, &r);
                MapWindowPoints((t->Tools[index].Flags & TTF_IDISHWND) ? (HWND)t->Tools[index].Id
                                                                       : t->Tools[index].Hwnd,
                                nullptr, (POINT *)&r, 2);
                x = (r.left + r.right - size.cx) / 2;
                y = r.bottom;
            }
        }
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        if (x + size.cx > work.right)
            x = work.right - size.cx;
        if (y + size.cy > work.bottom)
            y = at.y - size.cy - 2;
        if (x < work.left)
            x = work.left;
        SetWindowPos(t->Hwnd, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    if (!(t->Tools[index].Flags & TTF_TRACK)) {
        SetTimer(t->Hwnd, TIMER_POP, (UINT)t->Delay[TTDT_AUTOPOP], nullptr);
        SetTimer(t->Hwnd, TIMER_CHECK, 100, nullptr);
    }
}

static int tool_at(TOOLTIPS *t, POINT screen)
{
    HWND under = WindowFromPoint(screen);
    for (int k = 0; k < t->Count; k++) {
        TOOL *tool = &t->Tools[k];
        if (tool->Flags & TTF_TRACK)
            continue;
        if (tool->Flags & TTF_IDISHWND) {
            if ((HWND)tool->Id == under)
                return k;
            continue;
        }
        if (tool->Hwnd != under)
            continue;
        POINT pt = screen;
        ScreenToClient(tool->Hwnd, &pt);
        if (PtInRect(&tool->Rect, pt))
            return k;
    }
    return -1;
}

static void relay(TOOLTIPS *t, const MSG *m)
{
    if (!m || (!t->Active && !(t->Style & TTS_ALWAYSTIP)))
        return;
    switch (m->message) {
    case WM_MOUSEMOVE: {
        POINT pt;
        GetCursorPos(&pt);
        if (pt.x == t->LastPos.x && pt.y == t->LastPos.y)
            return;
        t->LastPos = pt;
        int k = tool_at(t, pt);
        if (k != t->Current) {
            BOOL was = t->Current >= 0;
            hide(t);
            if (k >= 0)
                SetTimer(t->Hwnd, TIMER_SHOW, (UINT)(was ? t->Delay[TTDT_RESHOW] : t->Delay[TTDT_INITIAL]), nullptr);
            else
                KillTimer(t->Hwnd, TIMER_SHOW);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        KillTimer(t->Hwnd, TIMER_SHOW);
        hide(t);
        break;
    }
}

static LRESULT CALLBACK tool_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    HWND tips = (HWND)data;
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && IsWindow(tips)) {
        MSG m = {hwnd, msg, wparam, lparam};
        SendMessageW(tips, TTM_RELAYEVENT, 0, (LPARAM)&m);
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static void paint(TOOLTIPS *t, HDC dc)
{
    RECT r;
    GetClientRect(t->Hwnd, &r);
    if (t->Theme && t->Back == CLR_DEFAULT) {
        DrawThemeBackground(t->Theme, dc, CC_TTP_STANDARD, 1, &r, nullptr);
    } else {
        CcFillSolid(dc, &r, t->Back == CLR_DEFAULT ? GetSysColor(COLOR_INFOBK) : t->Back);
        FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME));
    }
    HGDIOBJ old = SelectObject(dc, tip_font(t));
    SetBkMode(dc, TRANSPARENT);
    InflateRect(&r, -4, -3);
    r.left += t->Margin.left;
    r.top += t->Margin.top;
    r.right -= t->Margin.right;
    r.bottom -= t->Margin.bottom;
    UINT flags = DT_NOPREFIX | DT_EXPANDTABS | (t->MaxWidth > 0 ? DT_WORDBREAK : DT_SINGLELINE);
    if (t->Theme && t->Fore == CLR_DEFAULT) {
        DrawThemeText(t->Theme, dc, CC_TTP_STANDARD, 1, t->Shown, -1, flags, 0, &r);
    } else {
        SetTextColor(dc, t->Fore == CLR_DEFAULT ? GetSysColor(COLOR_INFOTEXT) : t->Fore);
        DrawTextW(dc, t->Shown, -1, &r, flags);
    }
    SelectObject(dc, old);
}

/* ------------------------------------------------------------------ tool messages */

static LRESULT add_tool(TOOLTIPS *t, const TTTOOLINFOW *ti, BOOL ansi)
{
    if (!ti || ti->cbSize > TOOLINFO_V3_SIZE)
        return FALSE;
    TOOL *tools = CcReAlloc(t->Tools, ((SIZE_T)t->Count + 1) * sizeof(TOOL));
    if (!tools)
        return FALSE;
    t->Tools = tools;
    TOOL *tool = &t->Tools[t->Count];
    memset(tool, 0, sizeof(*tool));
    tool->Flags = ti->uFlags;
    tool->Hwnd = ti->hwnd;
    tool->Id = ti->uId;
    tool->Rect = ti->rect;
    tool->Inst = ti->hinst;
    if (ti->cbSize >= TTTOOLINFOW_V2_SIZE)
        tool->Param = ti->lParam;
    if (!set_tool_text(tool, ti->lpszText, ansi))
        return FALSE;
    tool->Unicode = ti->hwnd && SendMessageW(ti->hwnd, WM_NOTIFYFORMAT, (WPARAM)t->Hwnd, NF_QUERY) == NFR_UNICODE;
    t->Count++;
    if (ti->uFlags & TTF_SUBCLASS) {
        HWND target = (ti->uFlags & TTF_IDISHWND) ? (HWND)ti->uId : ti->hwnd;
        if (target)
            SetWindowSubclass(target, tool_subclass, (UINT_PTR)t->Hwnd, (DWORD_PTR)t->Hwnd);
    }
    return TRUE;
}

static void delete_tool(TOOLTIPS *t, int k)
{
    if (k < 0)
        return;
    if (t->Current == k)
        hide(t);
    TOOL *tool = &t->Tools[k];
    if (tool->Flags & TTF_SUBCLASS) {
        HWND target = (tool->Flags & TTF_IDISHWND) ? (HWND)tool->Id : tool->Hwnd;
        BOOL used = FALSE;
        for (int j = 0; j < t->Count; j++)
            if (j != k && (t->Tools[j].Flags & TTF_SUBCLASS) &&
                ((t->Tools[j].Flags & TTF_IDISHWND) ? (HWND)t->Tools[j].Id : t->Tools[j].Hwnd) == target)
                used = TRUE;
        if (!used && target)
            RemoveWindowSubclass(target, tool_subclass, (UINT_PTR)t->Hwnd);
    }
    free_tool(tool);
    memmove(tool, tool + 1, (SIZE_T)(t->Count - k - 1) * sizeof(TOOL));
    t->Count--;
    if (t->Current > k)
        t->Current--;
    if (t->Tracking == k)
        t->Tracking = -1;
    else if (t->Tracking > k)
        t->Tracking--;
}

/* nothing below the first structure size, everything up to the caller's size. */
static LRESULT copy_tool(TOOLTIPS *t, int k, TTTOOLINFOW *ti, BOOL ansi)
{
    if (!ti || ti->cbSize > TOOLINFO_V3_SIZE)
        return FALSE;
    if (ti->cbSize < TOOLINFO_V1_SIZE)
        return TRUE;
    TOOL *tool = &t->Tools[k];
    ti->uFlags = tool->Flags;
    ti->hwnd = tool->Hwnd;
    ti->uId = tool->Id;
    ti->rect = tool->Rect;
    ti->hinst = tool->Inst;
    if (ti->cbSize >= TTTOOLINFOW_V2_SIZE)
        ti->lParam = tool->Param;
    if (tool->Text == LPSTR_TEXTCALLBACKW || IS_INTRESOURCE(tool->Text))
        ti->lpszText = tool->Text;
    else if (ti->lpszText) {
        if (ansi)
            CcCopyTextA((LPSTR)ti->lpszText, 80, tool->Text);
        else
            CcCopyText(ti->lpszText, 80, tool->Text);
    }
    return TRUE;
}

LRESULT CALLBACK TooltipsProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TOOLTIPS *t = get_data(hwnd);
    if (!t && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    BOOL ansi = FALSE;
    switch (msg) {
    case WM_NCCREATE: {
        t = CcAlloc(sizeof(*t));
        if (!t)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)t);
        t->Hwnd = hwnd;
        t->Style = (DWORD)((CREATESTRUCTW *)lparam)->style;
        t->Active = TRUE;
        t->Current = t->Tracking = -1;
        t->MaxWidth = -1;
        t->Back = CLR_DEFAULT;
        t->Fore = CLR_DEFAULT;
        default_delays(t);
        /* a tooltip is a popup with no border of its own. */
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)((t->Style & ~(WS_BORDER | WS_DLGFRAME | WS_CHILD)) | WS_POPUP));
        SetWindowLongW(hwnd, GWL_EXSTYLE,
                       (LONG)((DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE) | WS_EX_TOOLWINDOW);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        t->Theme = OpenThemeData(hwnd, L"Tooltip");
        return 0;
    case WM_DESTROY:
        while (t->Count)
            delete_tool(t, t->Count - 1);
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        CcFree(t->Tools);
        CcFree(t->Title);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(t);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc)
            paint(t, dc);
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wparam == TIMER_SHOW) {
            KillTimer(hwnd, TIMER_SHOW);
            POINT pt;
            GetCursorPos(&pt);
            int k = tool_at(t, pt);
            if (k >= 0)
                show(t, k, pt);
        } else if (wparam == TIMER_POP) {
            hide(t);
        } else if (wparam == TIMER_CHECK) {
            POINT pt;
            GetCursorPos(&pt);
            if (tool_at(t, pt) != t->Current)
                hide(t);
        }
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
        hide(t);
        return 0;
    case WM_NCHITTEST:
        if (t->Current >= 0 && (t->Tools[t->Current].Flags & TTF_TRANSPARENT))
            return HTTRANSPARENT;
        break;
    case WM_SETFONT:
        t->Font = (HFONT)wparam;
        return 0;
    case WM_GETFONT:
        return (LRESULT)t->Font;
    case WM_GETTEXT:
        return t->Current >= 0 ? CcCopyText((LPWSTR)lparam, (int)wparam, t->Shown) : 0;
    case WM_GETTEXTLENGTH:
        return t->Current >= 0 ? (LRESULT)wcslen(t->Shown) : 0;
    case WM_THEMECHANGED:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = OpenThemeData(hwnd, L"Tooltip");
        return 0;
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x10018;
        break;
    case TTM_ACTIVATE:
        t->Active = wparam != 0;
        if (!t->Active)
            hide(t);
        return 0;
    case TTM_ADDTOOLA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_ADDTOOLW:
        return add_tool(t, (const TTTOOLINFOW *)lparam, ansi);
    case TTM_DELTOOLA:
    case TTM_DELTOOLW:
        delete_tool(t, tool_of(t, (const TTTOOLINFOW *)lparam));
        return 0;
    case TTM_NEWTOOLRECTA:
    case TTM_NEWTOOLRECTW: {
        int k = tool_of(t, (const TTTOOLINFOW *)lparam);
        if (k >= 0)
            t->Tools[k].Rect = ((const TTTOOLINFOW *)lparam)->rect;
        return 0;
    }
    case TTM_GETTOOLINFOA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_GETTOOLINFOW: {
        int k = tool_of(t, (const TTTOOLINFOW *)lparam);
        return k >= 0 ? copy_tool(t, k, (TTTOOLINFOW *)lparam, ansi) : FALSE;
    }
    case TTM_SETTOOLINFOA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_SETTOOLINFOW: {
        const TTTOOLINFOW *ti = (const TTTOOLINFOW *)lparam;
        int k = tool_of(t, ti);
        if (k >= 0) {
            TOOL *tool = &t->Tools[k];
            tool->Flags = ti->uFlags;
            tool->Rect = ti->rect;
            tool->Inst = ti->hinst;
            if (ti->cbSize >= TTTOOLINFOW_V2_SIZE)
                tool->Param = ti->lParam;
            set_tool_text(tool, ti->lpszText, ansi);
            if (t->Current == k)
                tool_text(t, k, t->Shown, 1024);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case TTM_UPDATETIPTEXTA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_UPDATETIPTEXTW: {
        const TTTOOLINFOW *ti = (const TTTOOLINFOW *)lparam;
        int k = tool_of(t, ti);
        if (k >= 0) {
            t->Tools[k].Inst = ti->hinst;
            set_tool_text(&t->Tools[k], ti->lpszText, ansi);
            if (t->Current == k) {
                tool_text(t, k, t->Shown, 1024);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        return 0;
    }
    case TTM_GETTEXTA:
    case TTM_GETTEXTW: {
        TTTOOLINFOW *ti = (TTTOOLINFOW *)lparam;
        int k = tool_of(t, ti);
        if (k >= 0 && ti->lpszText) {
            WCHAR text[1024];
            tool_text(t, k, text, 1024);
            int max = wparam ? (int)wparam : 80;
            if (msg == TTM_GETTEXTA)
                CcCopyTextA((LPSTR)ti->lpszText, max, text);
            else
                CcCopyText(ti->lpszText, max, text);
        }
        return 0;
    }
    case TTM_GETTOOLCOUNT:
        return t->Count;
    case TTM_ENUMTOOLSA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_ENUMTOOLSW:
        if ((int)wparam < 0 || (int)wparam >= t->Count)
            return FALSE;
        return copy_tool(t, (int)wparam, (TTTOOLINFOW *)lparam, ansi);
    case TTM_GETCURRENTTOOLA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_GETCURRENTTOOLW:
        if (!lparam)
            return t->Current >= 0;
        return t->Current >= 0 ? copy_tool(t, t->Current, (TTTOOLINFOW *)lparam, ansi) : FALSE;
    case TTM_HITTESTA:
        ansi = TRUE;
        [[fallthrough]];
    case TTM_HITTESTW: {
        TTHITTESTINFOW *hit = (TTHITTESTINFOW *)lparam;
        if (!hit)
            return FALSE;
        POINT pt = hit->pt;
        ClientToScreen(hit->hwnd, &pt);
        int k = tool_at(t, pt);
        if (k < 0 || t->Tools[k].Hwnd != hit->hwnd)
            return FALSE;
        if (hit->ti.cbSize >= TOOLINFO_V1_SIZE)
            copy_tool(t, k, &hit->ti, ansi);
        return TRUE;
    }
    case TTM_RELAYEVENT:
        relay(t, (const MSG *)lparam);
        return 0;
    case TTM_WINDOWFROMPOINT:
        return (LRESULT)WindowFromPoint(*(POINT *)lparam);
    case TTM_TRACKACTIVATE: {
        int k = tool_of(t, (const TTTOOLINFOW *)lparam);
        if (wparam && k >= 0) {
            t->Tracking = k;
            t->TrackActive = TRUE;
            show(t, k, t->TrackPos);
        } else {
            t->TrackActive = FALSE;
            if (t->Current == t->Tracking)
                hide(t);
            t->Tracking = -1;
        }
        return TRUE;
    }
    case TTM_TRACKPOSITION:
        t->TrackPos.x = (short)LOWORD(lparam);
        t->TrackPos.y = (short)HIWORD(lparam);
        if (t->TrackActive && t->Tracking >= 0)
            SetWindowPos(hwnd, HWND_TOPMOST, t->TrackPos.x, t->TrackPos.y, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    case TTM_SETTIPBKCOLOR:
        t->Back = (COLORREF)wparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TTM_SETTIPTEXTCOLOR:
        t->Fore = (COLORREF)wparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TTM_GETTIPBKCOLOR:
        return t->Back == CLR_DEFAULT ? GetSysColor(COLOR_INFOBK) : t->Back;
    case TTM_GETTIPTEXTCOLOR:
        return t->Fore == CLR_DEFAULT ? GetSysColor(COLOR_INFOTEXT) : t->Fore;
    case TTM_SETDELAYTIME:
        if (wparam == TTDT_AUTOMATIC) {
            int v = (short)LOWORD(lparam);
            if (v < 0)
                default_delays(t);
            else {
                t->Delay[TTDT_INITIAL] = v;
                t->Delay[TTDT_AUTOPOP] = 10 * v;
                t->Delay[TTDT_RESHOW] = v / 5;
            }
        } else if (wparam <= TTDT_INITIAL) {
            int v = (short)LOWORD(lparam);
            if (v < 0) {
                int saved[4];
                memcpy(saved, t->Delay, sizeof(saved));
                default_delays(t);
                int d = t->Delay[wparam];
                memcpy(t->Delay, saved, sizeof(saved));
                t->Delay[wparam] = d;
            } else {
                t->Delay[wparam] = v;
            }
        }
        return 0;
    case TTM_GETDELAYTIME:
        if (wparam == TTDT_AUTOMATIC)
            return t->Delay[TTDT_INITIAL];
        return wparam <= TTDT_INITIAL ? t->Delay[wparam] : -1;
    case TTM_SETMAXTIPWIDTH: {
        int old = t->MaxWidth;
        t->MaxWidth = (int)lparam;
        return old;
    }
    case TTM_GETMAXTIPWIDTH:
        return t->MaxWidth;
    case TTM_SETMARGIN:
        if (lparam)
            t->Margin = *(RECT *)lparam;
        return 0;
    case TTM_GETMARGIN:
        if (lparam)
            *(RECT *)lparam = t->Margin;
        return 0;
    case TTM_POP:
        hide(t);
        return 0;
    case TTM_POPUP: {
        POINT pt;
        GetCursorPos(&pt);
        int k = tool_at(t, pt);
        if (k >= 0)
            show(t, k, pt);
        return 0;
    }
    case TTM_UPDATE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TTM_GETBUBBLESIZE: {
        int k = tool_of(t, (const TTTOOLINFOW *)lparam);
        if (k < 0)
            return 0;
        WCHAR text[1024];
        tool_text(t, k, text, 1024);
        SIZE size;
        bubble_size(t, text, &size);
        return MAKELONG(size.cx, size.cy);
    }
    case TTM_ADJUSTRECT: {
        RECT *r = (RECT *)lparam;
        if (!r)
            return FALSE;
        int dx = 4 + t->Margin.left, dy = 3 + t->Margin.top;
        if (wparam)
            InflateRect(r, dx, dy);
        else
            InflateRect(r, -dx, -dy);
        return TRUE;
    }
    case TTM_SETTITLEA:
    case TTM_SETTITLEW: {
        CcFree(t->Title);
        t->Title = nullptr;
        if (lparam) {
            t->Title = msg == TTM_SETTITLEA ? CcStrDupAtoW((LPCSTR)lparam) : CcStrDupW((LPCWSTR)lparam);
            if (t->Title && msg == TTM_SETTITLEA && wcslen(t->Title) > 100)
                t->Title[100] = 0;
        }
        t->TitleIcon = (UINT)wparam;
        return TRUE;
    }
    case TTM_GETTITLE: {
        TTGETTITLE *g = (TTGETTITLE *)lparam;
        if (wparam || !g || g->dwSize != sizeof(*g) || !g->cch || !g->pszTitle || !t->Title)
            return 0;
        g->uTitleBitmap = t->TitleIcon;
        CcCopyText(g->pszTitle, (int)g->cch, t->Title);
        return TRUE;
    }
    case TTM_SETWINDOWTHEME:
        SetWindowTheme(hwnd, (LPCWSTR)lparam, nullptr);
        return 0;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
