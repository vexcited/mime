/* the up down control, msctls_updown32: two arrows stepping a position, optionally shown in a buddy window. */
#include "comctl32_private.h"

#define TIMER_REPEAT 1
#define REPEAT_INTERVAL 100
#define PART_NONE 0
#define PART_UP 1
#define PART_DOWN 2

typedef struct UPDOWN {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style;
    int Lower, Upper, Pos;
    int Base;
    UDACCEL *Accel;
    int AccelCount;
    HWND Buddy;
    HTHEME Theme;
    int Pressed, Hot;
    DWORD PressStart;
    BOOL Repeating;
} UPDOWN;

static UPDOWN *get_data(HWND hwnd)
{
    return (UPDOWN *)GetWindowLongPtrW(hwnd, 0);
}

static int low_bound(UPDOWN *u)
{
    return u->Lower < u->Upper ? u->Lower : u->Upper;
}

static int high_bound(UPDOWN *u)
{
    return u->Lower < u->Upper ? u->Upper : u->Lower;
}

static BOOL buddy_is(UPDOWN *u, LPCWSTR cls)
{
    WCHAR name[32];
    return u->Buddy && GetClassNameW(u->Buddy, name, 32) && !_wcsicmp(name, cls);
}

/* writes the position to the buddy, with or without thousands separators. */
static void update_buddy(UPDOWN *u)
{
    if (!u->Buddy || !(u->Style & UDS_SETBUDDYINT))
        return;
    if (buddy_is(u, L"ListBox")) {
        SendMessageW(u->Buddy, LB_SETCURSEL, (WPARAM)u->Pos, 0);
        SendMessageW(GetParent(u->Buddy), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(u->Buddy), LBN_SELCHANGE),
                     (LPARAM)u->Buddy);
        return;
    }
    WCHAR text[32];
    if (u->Base == 16) {
        wsprintfW(text, high_bound(u) >= 0x10000 ? L"0x%08X" : L"0x%04X", u->Pos);
    } else {
        WCHAR digits[16];
        unsigned v = (unsigned)(u->Pos < 0 ? -u->Pos : u->Pos);
        int n = 0;
        do {
            digits[n++] = (WCHAR)(L'0' + v % 10);
            v /= 10;
        } while (v);
        int k = 0;
        if (u->Pos < 0)
            text[k++] = L'-';
        WCHAR sep = L',';
        WCHAR buffer[4];
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, buffer, 4) > 1)
            sep = buffer[0];
        BOOL thousands = !(u->Style & UDS_NOTHOUSANDS);
        for (int i = n - 1; i >= 0; i--) {
            text[k++] = digits[i];
            if (thousands && i && i % 3 == 0)
                text[k++] = sep;
        }
        text[k] = 0;
    }
    SetWindowTextW(u->Buddy, text);
}

/* FALSE when the buddy's text is not a number. */
static BOOL read_buddy(UPDOWN *u, int *value)
{
    if (!u->Buddy || !(u->Style & UDS_SETBUDDYINT)) {
        *value = u->Pos;
        return TRUE;
    }
    if (buddy_is(u, L"ListBox")) {
        *value = (int)SendMessageW(u->Buddy, LB_GETCURSEL, 0, 0);
        return TRUE;
    }
    WCHAR text[64];
    GetWindowTextW(u->Buddy, text, 64);
    LPCWSTR p = text;
    while (*p == L' ')
        p++;
    BOOL negative = FALSE;
    if (*p == L'-') {
        negative = TRUE;
        p++;
    }
    int base = u->Base;
    if (base == 16 && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))
        p += 2;
    long long v = 0;
    BOOL any = FALSE;
    for (; *p; p++) {
        int d;
        if (*p >= L'0' && *p <= L'9')
            d = *p - L'0';
        else if (base == 16 && *p >= L'a' && *p <= L'f')
            d = *p - L'a' + 10;
        else if (base == 16 && *p >= L'A' && *p <= L'F')
            d = *p - L'A' + 10;
        else if (base == 10 && (*p == L',' || *p == L'.' || *p == L' ' || *p == 0xA0))
            continue;
        else
            return FALSE;
        v = v * base + d;
        any = TRUE;
    }
    if (!any)
        return FALSE;
    *value = (int)(negative ? -v : v);
    return TRUE;
}

static void sync_from_buddy(UPDOWN *u)
{
    int v;
    if (read_buddy(u, &v))
        u->Pos = v;
}

static int clamp_pos(UPDOWN *u, int v)
{
    if (v < low_bound(u))
        return low_bound(u);
    if (v > high_bound(u))
        return high_bound(u);
    return v;
}

/* sizes and places the control next to the buddy, which gives up the room. */
static void align_to_buddy(UPDOWN *u)
{
    if (!u->Buddy || !(u->Style & (UDS_ALIGNLEFT | UDS_ALIGNRIGHT)))
        return;
    RECT br;
    GetWindowRect(u->Buddy, &br);
    HWND parent = GetParent(u->Hwnd);
    MapWindowPoints(nullptr, parent, (POINT *)&br, 2);
    int border = GetSystemMetrics(SM_CXBORDER);
    BOOL edit = buddy_is(u, L"Edit") || (GetWindowLongW(u->Buddy, GWL_EXSTYLE) & WS_EX_CLIENTEDGE);
    int overlap = edit ? 2 * border : border;
    int width = GetSystemMetrics(SM_CXVSCROLL) - border;
    int x;
    if (u->Style & UDS_ALIGNRIGHT) {
        br.right -= width - overlap;
        x = br.right - overlap;
    } else {
        br.left += width - overlap;
        x = br.left - width + overlap;
    }
    SetWindowPos(u->Buddy, nullptr, br.left, br.top, br.right - br.left, br.bottom - br.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowLongW(u->Hwnd, GWL_STYLE, (LONG)((DWORD)GetWindowLongW(u->Hwnd, GWL_STYLE) & ~WS_BORDER));
    SetWindowLongW(u->Hwnd, GWL_EXSTYLE, (LONG)((DWORD)GetWindowLongW(u->Hwnd, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE));
    SetWindowPos(u->Hwnd, nullptr, x, br.top, width, br.bottom - br.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static LRESULT CALLBACK buddy_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                                       DWORD_PTR data);

static HWND set_buddy(UPDOWN *u, HWND buddy)
{
    HWND old = u->Buddy;
    if (old && IsWindow(old))
        RemoveWindowSubclass(old, buddy_subclass, (UINT_PTR)u->Hwnd);
    u->Buddy = buddy;
    if (buddy) {
        if (u->Style & UDS_ARROWKEYS)
            SetWindowSubclass(buddy, buddy_subclass, (UINT_PTR)u->Hwnd, (DWORD_PTR)u->Hwnd);
        align_to_buddy(u);
        update_buddy(u);
    }
    return old;
}

static int increment(UPDOWN *u)
{
    int inc = u->AccelCount ? (int)u->Accel[0].nInc : 1;
    DWORD elapsed = (GetTickCount() - u->PressStart) / 1000;
    for (int k = 0; k < u->AccelCount; k++)
        if (elapsed >= u->Accel[k].nSec)
            inc = (int)u->Accel[k].nInc;
    return inc;
}

/* UDN_DELTAPOS may cancel the step, then the position, the buddy and the scroll message follow. */
static void step(UPDOWN *u, BOOL up, int inc)
{
    sync_from_buddy(u);
    int delta = up ? inc : -inc;
    if (u->Upper < u->Lower)
        delta = -delta;
    NMUPDOWN nm = {};
    nm.iPos = u->Pos;
    nm.iDelta = delta;
    if (CcNotify(&u->Notify, UDN_DELTAPOS, &nm.hdr))
        return;
    int v = u->Pos + nm.iDelta;
    if (inc > 1 && nm.iDelta)
        v = v / inc * inc;
    if (v < low_bound(u) || v > high_bound(u)) {
        if (u->Style & UDS_WRAP)
            v = v < low_bound(u) ? high_bound(u) : low_bound(u);
        else
            v = clamp_pos(u, v);
    }
    u->Pos = v;
    update_buddy(u);
    UINT msg = (u->Style & UDS_HORZ) ? WM_HSCROLL : WM_VSCROLL;
    SendMessageW(u->Notify.Parent, msg, MAKEWPARAM(SB_THUMBPOSITION, (WORD)u->Pos), (LPARAM)u->Hwnd);
}

static LRESULT CALLBACK buddy_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                                       DWORD_PTR data)
{
    if (msg == WM_KEYDOWN && (wparam == VK_UP || wparam == VK_DOWN)) {
        UPDOWN *u = get_data((HWND)data);
        if (u && IsWindowEnabled((HWND)data)) {
            u->PressStart = GetTickCount();
            step(u, wparam == VK_UP, increment(u));
            return 0;
        }
    }
    if (msg == WM_KEYUP && (wparam == VK_UP || wparam == VK_DOWN)) {
        UPDOWN *u = get_data((HWND)data);
        if (u) {
            UINT m = (u->Style & UDS_HORZ) ? WM_HSCROLL : WM_VSCROLL;
            SendMessageW(u->Notify.Parent, m, MAKEWPARAM(SB_ENDSCROLL, (WORD)u->Pos), (LPARAM)u->Hwnd);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static void part_rects(UPDOWN *u, RECT *up, RECT *down)
{
    RECT r;
    GetClientRect(u->Hwnd, &r);
    *up = *down = r;
    if (u->Style & UDS_HORZ) {
        down->right = (r.left + r.right) / 2;
        up->left = down->right;
    } else {
        up->bottom = (r.top + r.bottom) / 2;
        down->top = up->bottom;
    }
}

static int part_at(UPDOWN *u, POINT pt)
{
    RECT up, down;
    part_rects(u, &up, &down);
    if (PtInRect(&up, pt))
        return PART_UP;
    if (PtInRect(&down, pt))
        return PART_DOWN;
    return PART_NONE;
}

static void paint(UPDOWN *u, HDC dc)
{
    RECT up, down;
    part_rects(u, &up, &down);
    BOOL enabled = IsWindowEnabled(u->Hwnd);
    BOOL horz = (u->Style & UDS_HORZ) != 0;
    for (int k = PART_UP; k <= PART_DOWN; k++) {
        RECT *r = k == PART_UP ? &up : &down;
        int state = !enabled ? 4 : u->Pressed == k ? 3 : u->Hot == k ? 2 : 1;
        if (u->Theme) {
            int part = horz ? (k == PART_UP ? CC_SPNP_UPHORZ : CC_SPNP_DOWNHORZ) : (k == PART_UP ? CC_SPNP_UP : CC_SPNP_DOWN);
            DrawThemeBackground(u->Theme, dc, part, state, r, nullptr);
        } else {
            UINT kind = horz ? (k == PART_UP ? DFCS_SCROLLRIGHT : DFCS_SCROLLLEFT)
                             : (k == PART_UP ? DFCS_SCROLLUP : DFCS_SCROLLDOWN);
            if (u->Pressed == k)
                kind |= DFCS_PUSHED;
            if (!enabled)
                kind |= DFCS_INACTIVE;
            DrawFrameControl(dc, r, DFC_SCROLL, kind);
        }
    }
}

LRESULT CALLBACK UpDownProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    UPDOWN *u = get_data(hwnd);
    if (!u && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        u = CcAlloc(sizeof(*u));
        if (!u)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)u);
        u->Hwnd = hwnd;
        u->Style = (DWORD)cs->style;
        u->Lower = 100;
        u->Upper = 0;
        u->Base = 10;
        static const UDACCEL accel[] = {{0, 1}, {2, 5}, {5, 20}};
        u->Accel = CcAlloc(sizeof(accel));
        if (u->Accel) {
            memcpy(u->Accel, accel, sizeof(accel));
            u->AccelCount = 3;
        }
        CcNotifyInit(&u->Notify, hwnd, cs->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        u->Theme = OpenThemeData(hwnd, L"Spin");
        if (u->Style & UDS_AUTOBUDDY)
            set_buddy(u, GetWindow(hwnd, GW_HWNDPREV));
        return 0;
    case WM_DESTROY:
        if (u->Buddy && IsWindow(u->Buddy))
            RemoveWindowSubclass(u->Buddy, buddy_subclass, (UINT_PTR)hwnd);
        if (u->Theme)
            CloseThemeData(u->Theme);
        u->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        CcFree(u->Accel);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(u);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc)
            paint(u, dc);
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int part = part_at(u, pt);
        if (!part)
            return 0;
        u->Pressed = part;
        u->PressStart = GetTickCount();
        SetCapture(hwnd);
        if (u->Buddy && (u->Style & UDS_SETBUDDYINT))
            SetFocus(u->Buddy);
        InvalidateRect(hwnd, nullptr, FALSE);
        step(u, part == PART_UP, increment(u));
        UINT blink = GetCaretBlinkTime();
        SetTimer(hwnd, TIMER_REPEAT, blink < 1300 ? blink : 1300, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wparam == TIMER_REPEAT && u->Pressed) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (part_at(u, pt) == u->Pressed)
                step(u, u->Pressed == PART_UP, increment(u));
            SetTimer(hwnd, TIMER_REPEAT, REPEAT_INTERVAL, nullptr);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (u->Pressed) {
            u->Pressed = PART_NONE;
            KillTimer(hwnd, TIMER_REPEAT);
            if (GetCapture() == hwnd)
                ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            UINT m = (u->Style & UDS_HORZ) ? WM_HSCROLL : WM_VSCROLL;
            SendMessageW(u->Notify.Parent, m, MAKEWPARAM(SB_ENDSCROLL, (WORD)u->Pos), (LPARAM)hwnd);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int hot = part_at(u, pt);
        if (hot != u->Hot) {
            u->Hot = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        u->Hot = PART_NONE;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if ((wparam == VK_UP || wparam == VK_DOWN) && (u->Style & UDS_ARROWKEYS)) {
            u->PressStart = GetTickCount();
            step(u, wparam == VK_UP, increment(u));
            return 0;
        }
        break;
    case WM_THEMECHANGED:
        if (u->Theme)
            CloseThemeData(u->Theme);
        u->Theme = OpenThemeData(hwnd, L"Spin");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&u->Notify, (HWND)wparam, lparam);
    case UDM_SETRANGE:
        u->Upper = (short)LOWORD(lparam);
        u->Lower = (short)HIWORD(lparam);
        u->Pos = clamp_pos(u, u->Pos);
        return 0;
    case UDM_GETRANGE:
        return MAKELONG((WORD)u->Upper, (WORD)u->Lower);
    case UDM_SETRANGE32:
        u->Lower = (int)wparam;
        u->Upper = (int)lparam;
        u->Pos = clamp_pos(u, u->Pos);
        return 0;
    case UDM_GETRANGE32:
        if (wparam)
            *(int *)wparam = u->Lower;
        if (lparam)
            *(int *)lparam = u->Upper;
        return 0;
    case UDM_SETPOS:
    case UDM_SETPOS32: {
        int old = u->Pos;
        u->Pos = clamp_pos(u, msg == UDM_SETPOS ? (short)LOWORD(lparam) : (int)lparam);
        update_buddy(u);
        return old;
    }
    case UDM_GETPOS: {
        int v;
        BOOL ok = read_buddy(u, &v);
        if (ok)
            u->Pos = v;
        return MAKELONG((WORD)u->Pos, ok ? 0 : 1);
    }
    case UDM_GETPOS32: {
        int v;
        BOOL ok = read_buddy(u, &v);
        if (ok)
            u->Pos = v;
        if (lparam)
            *(BOOL *)lparam = !ok;
        return u->Pos;
    }
    case UDM_SETBUDDY:
        return (LRESULT)set_buddy(u, (HWND)wparam);
    case UDM_GETBUDDY:
        return (LRESULT)u->Buddy;
    case UDM_SETACCEL: {
        if (!wparam || !lparam)
            return FALSE;
        UDACCEL *accel = CcAlloc(wparam * sizeof(UDACCEL));
        if (!accel)
            return FALSE;
        memcpy(accel, (const UDACCEL *)lparam, wparam * sizeof(UDACCEL));
        CcFree(u->Accel);
        u->Accel = accel;
        u->AccelCount = (int)wparam;
        return TRUE;
    }
    case UDM_GETACCEL:
        if (lparam)
            for (int k = 0; k < u->AccelCount && k < (int)wparam; k++)
                ((UDACCEL *)lparam)[k] = u->Accel[k];
        return u->AccelCount;
    case UDM_SETBASE: {
        if (wparam != 10 && wparam != 16)
            return 0;
        int old = u->Base;
        u->Base = (int)wparam;
        update_buddy(u);
        return old;
    }
    case UDM_GETBASE:
        return u->Base;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINCOMMCTRLAPI HWND WINAPI CreateUpDownControl(DWORD style, int x, int y, int cx, int cy, HWND parent, int id,
                                               HINSTANCE inst, HWND buddy, int upper, int lower, int pos)
{
    HWND hwnd = CreateWindowExW(0, UPDOWN_CLASSW, nullptr, style, x, y, cx, cy, parent, (HMENU)(INT_PTR)id, inst,
                                nullptr);
    if (hwnd) {
        SendMessageW(hwnd, UDM_SETBUDDY, (WPARAM)buddy, 0);
        SendMessageW(hwnd, UDM_SETRANGE, 0, MAKELONG(upper, lower));
        SendMessageW(hwnd, UDM_SETPOS, 0, MAKELONG(pos, 0));
    }
    return hwnd;
}
