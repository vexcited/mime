/* the status bar, msctls_statusbar32: parts along the parent's bottom, a one part simple mode and a size grip. */
#include "comctl32_private.h"

#define SIMPLE_PART 255
#define MAX_PARTS 256

typedef struct SB_PART {
    int Edge;
    LPWSTR Text;
    UINT Type;
    LPARAM Data;
    HICON Icon;
    LPWSTR Tip;
} SB_PART;

typedef struct STATUS {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style;
    SB_PART *Parts;
    int Count;
    SB_PART Simple;
    BOOL IsSimple;
    int MinHeight;
    HFONT Font;
    HTHEME Theme;
    COLORREF Bk;
    BOOL Grip;
} STATUS;

static STATUS *get_data(HWND hwnd)
{
    return (STATUS *)GetWindowLongPtrW(hwnd, 0);
}

static HFONT status_font(STATUS *s)
{
    if (s->Font)
        return s->Font;
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

/* text or a small icon plus borders, at least the minimum asked for. */
static int bar_height(STATUS *s)
{
    HDC dc = GetDC(s->Hwnd);
    HGDIOBJ old = SelectObject(dc, status_font(s));
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(s->Hwnd, dc);
    int border = GetSystemMetrics(SM_CYBORDER);
    int text = tm.tmHeight + tm.tmInternalLeading;
    if (!tm.tmInternalLeading)
        text += 2 * border;
    int h = GetSystemMetrics(SM_CYSMICON);
    if (text > h)
        h = text;
    h += 2 * border;
    if (s->MinHeight > h)
        h = s->MinHeight;
    return h + GetSystemMetrics(SM_CYEDGE);
}

static int grip_width(STATUS *s)
{
    if (!s->Grip)
        return 0;
    int w = GetSystemMetrics(SM_CXVSCROLL);
    if (s->Theme) {
        SIZE size;
        HDC dc = GetDC(s->Hwnd);
        if (SUCCEEDED(GetThemePartSize(s->Theme, dc, CC_SP_GRIPPER, 0, nullptr, TS_DRAW, &size)))
            w = size.cx;
        ReleaseDC(s->Hwnd, dc);
    }
    return w;
}

/* along the parent's bottom, or top, as the CCS styles say. */
static void reposition(STATUS *s)
{
    HWND parent = GetParent(s->Hwnd);
    if (!parent || (s->Style & CCS_NOPARENTALIGN))
        return;
    RECT pr, wr;
    GetClientRect(parent, &pr);
    GetWindowRect(s->Hwnd, &wr);
    MapWindowPoints(nullptr, parent, (POINT *)&wr, 2);
    int h = (s->Style & CCS_NORESIZE) ? wr.bottom - wr.top : bar_height(s);
    int y = (s->Style & 3) == CCS_TOP ? pr.top : pr.bottom - h;
    if ((s->Style & 3) == CCS_NOMOVEY)
        y = wr.top;
    SetWindowPos(s->Hwnd, nullptr, pr.left, y, pr.right - pr.left, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static BOOL part_rect(STATUS *s, int index, RECT *r)
{
    RECT client;
    GetClientRect(s->Hwnd, &client);
    if (s->IsSimple) {
        *r = client;
        r->right -= grip_width(s);
        return TRUE;
    }
    if (index < 0 || index >= s->Count)
        return FALSE;
    int left = index ? s->Parts[index - 1].Edge : 0;
    if (index && left == -1)
        left = client.right;
    int right = s->Parts[index].Edge;
    if (!right)
        return FALSE;
    int limit = client.right - grip_width(s);
    if (right == -1 || right > limit)
        right = limit;
    SetRect(r, index ? left + GetSystemMetrics(SM_CXEDGE) : 0, client.top + GetSystemMetrics(SM_CYEDGE), right,
            client.bottom);
    return TRUE;
}

static void draw_text(HDC dc, LPCWSTR text, RECT *r)
{
    if (!text || !*text)
        return;
    /* tabs split the text into left, centered and right parts. */
    LPCWSTR parts[3] = {text, nullptr, nullptr};
    int lens[3] = {0, 0, 0};
    int k = 0;
    for (LPCWSTR p = text; *p; p++) {
        if (*p == L'\t' && k < 2) {
            parts[++k] = p + 1;
            continue;
        }
        lens[k]++;
    }
    static const UINT align[3] = {DT_LEFT, DT_CENTER, DT_RIGHT};
    for (int i = 0; i <= k; i++)
        if (lens[i])
            DrawTextW(dc, parts[i], lens[i], r, align[i] | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void draw_part(STATUS *s, HDC dc, SB_PART *part, int index, RECT *r, BOOL last)
{
    if (s->Theme) {
        DrawThemeBackground(s->Theme, dc, last && s->Grip ? CC_SP_GRIPPERPANE : CC_SP_PANE, 0, r, nullptr);
    } else if (!(part->Type & SBT_NOBORDERS)) {
        DrawEdge(dc, r, (part->Type & SBT_POPOUT) ? BDR_RAISEDINNER : BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
    }
    if (part->Type & SBT_OWNERDRAW) {
        DRAWITEMSTRUCT dis = {};
        dis.CtlID = (UINT)GetDlgCtrlID(s->Hwnd);
        dis.itemID = (UINT)index;
        dis.hwndItem = s->Hwnd;
        dis.hDC = dc;
        dis.rcItem = *r;
        InflateRect(&dis.rcItem, -GetSystemMetrics(SM_CXBORDER), -GetSystemMetrics(SM_CYBORDER));
        dis.itemData = (ULONG_PTR)part->Data;
        SendMessageW(s->Notify.Parent, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
        return;
    }
    RECT tr = *r;
    tr.left += GetSystemMetrics(SM_CXEDGE);
    if (part->Icon) {
        int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
        DrawIconEx(dc, tr.left, tr.top + (tr.bottom - tr.top - cy) / 2, part->Icon, cx, cy, 0, nullptr, DI_NORMAL);
        tr.left += cx + GetSystemMetrics(SM_CXEDGE);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    draw_text(dc, part->Text, &tr);
}

static void paint(STATUS *s, HDC dc)
{
    RECT client;
    GetClientRect(s->Hwnd, &client);
    HGDIOBJ old = SelectObject(dc, status_font(s));
    if (s->Theme && s->Bk == CLR_DEFAULT)
        DrawThemeBackground(s->Theme, dc, 0, 0, &client, nullptr);
    else
        CcFillSolid(dc, &client, s->Bk == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : s->Bk);
    if (s->IsSimple) {
        RECT r;
        part_rect(s, 0, &r);
        draw_part(s, dc, &s->Simple, SIMPLE_PART, &r, TRUE);
    } else {
        for (int k = 0; k < s->Count; k++) {
            RECT r;
            if (part_rect(s, k, &r))
                draw_part(s, dc, &s->Parts[k], k, &r, k == s->Count - 1);
        }
    }
    if (s->Grip) {
        RECT g = client;
        g.left = g.right - grip_width(s);
        if (s->Theme)
            DrawThemeBackground(s->Theme, dc, CC_SP_GRIPPER, 0, &g, nullptr);
        else
            DrawFrameControl(dc, &g, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
    }
    SelectObject(dc, old);
}

static void free_part(SB_PART *p)
{
    if (!(p->Type & SBT_OWNERDRAW))
        CcFree(p->Text);
    CcFree(p->Tip);
    p->Text = nullptr;
    p->Tip = nullptr;
}

static void invalidate_part(STATUS *s, int index)
{
    RECT r;
    if (s->IsSimple || index == SIMPLE_PART || !part_rect(s, index, &r))
        GetClientRect(s->Hwnd, &r);
    InvalidateRect(s->Hwnd, &r, TRUE);
}

static LRESULT set_text(STATUS *s, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    int index = LOBYTE(wparam);
    UINT type = (UINT)wparam & 0xFF00;
    if ((wparam & 0xF000) && (wparam & 0xF000) != SBT_OWNERDRAW)
        return FALSE;
    if (index == SIMPLE_PART && (type & SBT_OWNERDRAW))
        return FALSE;
    SB_PART *part = index == SIMPLE_PART ? &s->Simple : index < s->Count ? &s->Parts[index] : nullptr;
    if (!part)
        return FALSE;
    LPWSTR text = nullptr;
    if (type & SBT_OWNERDRAW) {
        free_part(part);
        part->Data = lparam;
    } else {
        if (lparam && lparam < 0x10000)
            return FALSE;
        if (lparam) {
            text = ansi ? CcStrDupAtoW((LPCSTR)lparam) : CcStrDupW((LPCWSTR)lparam);
            if (!text)
                return FALSE;
            for (LPWSTR p = text; *p; p++)
                if (*p < 0x20 && *p != L'\t')
                    *p = L' ';
        }
        if (!(part->Type & SBT_OWNERDRAW))
            CcFree(part->Text);
        part->Text = text;
    }
    part->Type = type;
    invalidate_part(s, index);
    UpdateWindow(s->Hwnd);
    return TRUE;
}

static LRESULT get_text(STATUS *s, int index, void *buffer, BOOL ansi, BOOL length_only)
{
    if (index >= s->Count && !(s->IsSimple && index == SIMPLE_PART))
        return 0;
    SB_PART *part = s->IsSimple ? &s->Simple : &s->Parts[index];
    if (part->Type & SBT_OWNERDRAW)
        return length_only ? MAKELONG(0, part->Type & 0x0FFF) : (LRESULT)part->Data;
    LPCWSTR text = part->Text ? part->Text : L"";
    int len;
    if (ansi) {
        len = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr) - 1;
        if (!length_only && buffer)
            WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, len + 1, nullptr, nullptr);
    } else {
        len = (int)wcslen(text);
        if (!length_only && buffer)
            memcpy(buffer, text, ((SIZE_T)len + 1) * sizeof(WCHAR));
    }
    if (len > 0xFFFE)
        len = 0xFFFE;
    return MAKELONG(len, part->Type & 0x0FFF);
}

static LRESULT set_parts(STATUS *s, int count, const int *edges)
{
    if (count < 1 || count > MAX_PARTS || !edges)
        return FALSE;
    if (count != s->Count) {
        for (int k = count; k < s->Count; k++)
            free_part(&s->Parts[k]);
        SB_PART *parts = CcReAlloc(s->Parts, (SIZE_T)count * sizeof(SB_PART));
        if (!parts)
            return FALSE;
        s->Parts = parts;
        s->Count = count;
    }
    int previous = 0;
    for (int k = 0; k < count; k++) {
        int e = edges[k];
        if (e == -1 && k == count - 1) {
            s->Parts[k].Edge = -1;
            continue;
        }
        if (e < previous)
            e = previous;
        s->Parts[k].Edge = e;
        previous = e;
    }
    InvalidateRect(s->Hwnd, nullptr, TRUE);
    return TRUE;
}

static int part_from_point(STATUS *s, POINT pt)
{
    if (s->IsSimple)
        return SIMPLE_PART;
    for (int k = 0; k < s->Count; k++) {
        RECT r;
        if (part_rect(s, k, &r) && pt.x >= r.left - GetSystemMetrics(SM_CXEDGE) && pt.x < r.right)
            return k;
    }
    return -1;
}

LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    STATUS *s = get_data(hwnd);
    if (!s && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        s = CcAlloc(sizeof(*s));
        if (!s)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)s);
        s->Hwnd = hwnd;
        s->Style = (DWORD)cs->style;
        s->Bk = CLR_DEFAULT;
        s->Parts = CcAlloc(sizeof(SB_PART));
        if (!s->Parts)
            return FALSE;
        s->Count = 1;
        s->Parts[0].Edge = -1;
        if (cs->lpszName && !IS_INTRESOURCE(cs->lpszName) && *cs->lpszName)
            s->Parts[0].Text = CcStrDupW(cs->lpszName);
        CcNotifyInit(&s->Notify, hwnd, cs->hwndParent);
        HWND parent = cs->hwndParent;
        s->Grip = (s->Style & SBARS_SIZEGRIP) ||
                  (parent && (GetWindowLongW(parent, GWL_STYLE) & WS_THICKFRAME) &&
                   !(s->Style & (CCS_TOP | CCS_NOMOVEY | CCS_NOPARENTALIGN)));
        return TRUE;
    }
    case WM_CREATE:
        s->Theme = OpenThemeData(hwnd, L"Status");
        reposition(s);
        return 0;
    case WM_DESTROY:
        if (s->Theme)
            CloseThemeData(s->Theme);
        s->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        for (int k = 0; k < s->Count; k++)
            free_part(&s->Parts[k]);
        free_part(&s->Simple);
        CcFree(s->Parts);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(s);
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
            paint(s, wparam ? dc : CcBeginBuffer(&b, dc, &client));
            if (!wparam)
                CcEndBuffer(&b);
        }
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        reposition(s);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETFONT:
        s->Font = (HFONT)wparam;
        if (lparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)s->Font;
    case WM_SETTEXT:
        return LOWORD(set_text(s, 0, lparam, FALSE));
    case WM_GETTEXT:
        if (!wparam || !lparam || (s->Parts[0].Type & SBT_OWNERDRAW))
            return 0;
        return CcCopyText((LPWSTR)lparam, (int)wparam, s->Parts[0].Text);
    case WM_GETTEXTLENGTH:
        return LOWORD(get_text(s, 0, nullptr, FALSE, TRUE));
    case WM_THEMECHANGED:
        if (s->Theme)
            CloseThemeData(s->Theme);
        s->Theme = OpenThemeData(hwnd, L"Status");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_NCHITTEST:
        if (s->Grip) {
            POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            RECT client;
            GetClientRect(hwnd, &client);
            ScreenToClient(hwnd, &pt);
            HWND parent = GetParent(hwnd);
            if (pt.x >= client.right - grip_width(s) && parent && !IsZoomed(parent))
                return (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ? HTBOTTOMLEFT : HTBOTTOMRIGHT;
        }
        break;
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        UINT code = msg == WM_LBUTTONUP ? NM_CLICK : msg == WM_LBUTTONDBLCLK ? NM_DBLCLK
                    : msg == WM_RBUTTONUP ? NM_RCLICK : NM_RDBLCLK;
        if (CcNotifyMouse(&s->Notify, code, (DWORD_PTR)(LONG_PTR)part_from_point(s, pt), 0, pt))
            return 0;
        break;
    }
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&s->Notify, (HWND)wparam, lparam);
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x1000B;
        break;
    case SB_SETTEXTA:
    case SB_SETTEXTW:
        return set_text(s, wparam, lparam, msg == SB_SETTEXTA);
    case SB_GETTEXTA:
    case SB_GETTEXTW:
        return get_text(s, LOBYTE(wparam), (void *)lparam, msg == SB_GETTEXTA, FALSE);
    case SB_GETTEXTLENGTHA:
    case SB_GETTEXTLENGTHW:
        return get_text(s, LOBYTE(wparam), nullptr, msg == SB_GETTEXTLENGTHA, TRUE);
    case SB_SETPARTS:
        return set_parts(s, (int)wparam, (const int *)lparam);
    case SB_GETPARTS: {
        int *edges = (int *)lparam;
        if (!edges)
            return 0;
        for (int k = 0; k < s->Count && k < (int)wparam; k++)
            edges[k] = s->Parts[k].Edge;
        return s->Count;
    }
    case SB_GETBORDERS: {
        int *b = (int *)lparam;
        if (!b)
            return FALSE;
        b[0] = 0;
        b[1] = GetSystemMetrics(SM_CYEDGE);
        b[2] = GetSystemMetrics(SM_CXEDGE);
        return TRUE;
    }
    case SB_SETMINHEIGHT:
        s->MinHeight = (int)wparam;
        return 0;
    case SB_SIMPLE:
        if ((wparam != 0) != s->IsSimple) {
            s->IsSimple = wparam != 0;
            CcNotifyHdr(&s->Notify, SBN_SIMPLEMODECHANGE);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case SB_ISSIMPLE:
        return s->IsSimple;
    case SB_GETRECT: {
        if (s->IsSimple || !lparam)
            return FALSE;
        RECT *r = (RECT *)lparam;
        if ((int)wparam < 0 || (int)wparam >= s->Count || !part_rect(s, (int)wparam, r))
            return FALSE;
        return TRUE;
    }
    case SB_SETICON: {
        SB_PART *part = (int)wparam == -1 ? &s->Simple : (int)wparam < s->Count ? &s->Parts[wparam] : nullptr;
        if (part) {
            part->Icon = (HICON)lparam;
            invalidate_part(s, (int)wparam == -1 ? SIMPLE_PART : (int)wparam);
        }
        return TRUE;
    }
    case SB_GETICON: {
        SB_PART *part = (int)wparam == -1 ? &s->Simple : (int)wparam < s->Count ? &s->Parts[wparam] : nullptr;
        return part ? (LRESULT)part->Icon : 0;
    }
    case SB_SETTIPTEXTA:
    case SB_SETTIPTEXTW:
        if ((int)wparam >= 0 && (int)wparam < s->Count) {
            CcFree(s->Parts[wparam].Tip);
            s->Parts[wparam].Tip = msg == SB_SETTIPTEXTA ? CcStrDupAtoW((LPCSTR)lparam) : CcStrDupW((LPCWSTR)lparam);
        }
        return 0;
    case SB_GETTIPTEXTA:
    case SB_GETTIPTEXTW: {
        int index = LOWORD(wparam), size = HIWORD(wparam);
        if (index >= s->Count || !lparam)
            return 0;
        LPCWSTR tip = s->Parts[index].Tip;
        if (msg == SB_GETTIPTEXTA)
            CcCopyTextA((LPSTR)lparam, size, tip);
        else
            CcCopyText((LPWSTR)lparam, size / (int)sizeof(WCHAR), tip);
        return 0;
    }
    case SB_SETBKCOLOR: {
        COLORREF old = s->Bk;
        s->Bk = (COLORREF)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case CCM_SETUNICODEFORMAT: {
        BOOL old = s->Notify.Unicode;
        s->Notify.Unicode = wparam != 0;
        return old;
    }
    case CCM_GETUNICODEFORMAT:
        return s->Notify.Unicode;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINCOMMCTRLAPI HWND WINAPI CreateStatusWindowW(LONG style, LPCWSTR text, HWND parent, UINT id)
{
    return CreateWindowExW(0, STATUSCLASSNAMEW, text, (DWORD)style, -100, -100, 10, 10, parent, (HMENU)(UINT_PTR)id,
                           Comctl32Instance, nullptr);
}

WINCOMMCTRLAPI HWND WINAPI CreateStatusWindowA(LONG style, LPCSTR text, HWND parent, UINT id)
{
    return CreateWindowExA(0, STATUSCLASSNAMEA, text, (DWORD)style, -100, -100, 10, 10, parent, (HMENU)(UINT_PTR)id,
                           Comctl32Instance, nullptr);
}

WINCOMMCTRLAPI void WINAPI DrawStatusTextW(HDC dc, LPCRECT rect, LPCWSTR text, UINT flags)
{
    RECT r = *rect;
    if (!(flags & SBT_NOBORDERS))
        DrawEdge(dc, &r, (flags & SBT_POPOUT) ? BDR_RAISEDINNER : BDR_SUNKENOUTER, BF_RECT | BF_ADJUST | BF_MIDDLE);
    else
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    r.left += GetSystemMetrics(SM_CXEDGE);
    int mode = SetBkMode(dc, TRANSPARENT);
    draw_text(dc, text, &r);
    SetBkMode(dc, mode);
}

WINCOMMCTRLAPI void WINAPI DrawStatusTextA(HDC dc, LPCRECT rect, LPCSTR text, UINT flags)
{
    LPWSTR w = CcStrDupAtoW(text);
    DrawStatusTextW(dc, rect, w, flags);
    CcFree(w);
}
