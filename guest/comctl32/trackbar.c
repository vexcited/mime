/* the trackbar, msctls_trackbar32: a thumb on a channel, with ticks, an optional selection range and buddies. */
#include "comctl32_private.h"

typedef struct TRACKBAR {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style;
    int Min, Max, Pos;
    int Page, Line;
    int Freq;
    int *Tics;
    int TicCount;
    int SelStart, SelEnd;
    int ThumbLength;
    HWND Tooltips, BuddyLeft, BuddyRight;
    int TipSide;
    HTHEME Theme;
    BOOL Dragging, Hot;
    int DragOffset;
} TRACKBAR;

static TRACKBAR *get_data(HWND hwnd)
{
    return (TRACKBAR *)GetWindowLongPtrW(hwnd, 0);
}

static inline BOOL vertical(TRACKBAR *t)
{
    return (t->Style & TBS_VERT) != 0;
}

static int page_size(TRACKBAR *t)
{
    if (t->Page != -1)
        return t->Page;
    int p = (t->Max - t->Min) / 5;
    return p > 0 ? p : 1;
}

/* the channel inset by half a thumb at each end, and the thumb's size. */
static void channel_rect(TRACKBAR *t, RECT *r)
{
    RECT client;
    GetClientRect(t->Hwnd, &client);
    int thumb_w = t->ThumbLength / 2;
    int inset = thumb_w / 2 + 4;
    if (vertical(t)) {
        int x = (t->Style & TBS_LEFT) && !(t->Style & TBS_BOTH) ? client.right - 8 - t->ThumbLength / 2
                                                                  : 8 + t->ThumbLength / 2 - 2;
        if (t->Style & TBS_BOTH)
            x = (client.right - 4) / 2;
        SetRect(r, x, client.top + inset, x + 4, client.bottom - inset);
    } else {
        int y = (t->Style & TBS_TOP) && !(t->Style & TBS_BOTH) ? client.bottom - 8 - t->ThumbLength / 2
                                                                 : 8 + t->ThumbLength / 2 - 2;
        if (t->Style & TBS_BOTH)
            y = (client.bottom - 4) / 2;
        SetRect(r, client.left + inset, y, client.right - inset, y + 4);
    }
}

static int pos_to_pixel(TRACKBAR *t, int pos, const RECT *channel)
{
    int span = t->Max - t->Min;
    int length = vertical(t) ? channel->bottom - channel->top : channel->right - channel->left;
    int offset = span > 0 ? MulDiv(pos - t->Min, length, span) : 0;
    return (vertical(t) ? channel->top : channel->left) + offset;
}

static void thumb_rect(TRACKBAR *t, RECT *r)
{
    RECT ch;
    channel_rect(t, &ch);
    int center = pos_to_pixel(t, t->Pos, &ch);
    int half = t->ThumbLength / 4;
    if (vertical(t)) {
        int cx = (ch.left + ch.right) / 2;
        SetRect(r, cx - t->ThumbLength / 2, center - half, cx + t->ThumbLength / 2, center + half + 1);
    } else {
        int cy = (ch.top + ch.bottom) / 2;
        SetRect(r, center - half, cy - t->ThumbLength / 2, center + half + 1, cy + t->ThumbLength / 2);
    }
}

static int clamp_pos(TRACKBAR *t, int v)
{
    if (v > t->Max)
        v = t->Max;
    if (v < t->Min)
        v = t->Min;
    return v;
}

static void notify_scroll(TRACKBAR *t, int code)
{
    UINT msg = vertical(t) ? WM_VSCROLL : WM_HSCROLL;
    WORD pos = (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) ? (WORD)t->Pos : 0;
    SendMessageW(t->Notify.Parent, msg, MAKEWPARAM(code, pos), (LPARAM)t->Hwnd);
}

/* asks first when TBS_NOTIFYBEFOREMOVE is set. */
static BOOL move_to(TRACKBAR *t, int pos, int reason)
{
    pos = clamp_pos(t, pos);
    if (t->Style & TBS_NOTIFYBEFOREMOVE) {
        NMTRBTHUMBPOSCHANGING nm = {};
        nm.dwPos = (DWORD)pos;
        nm.nReason = reason;
        if (CcNotify(&t->Notify, TRBN_THUMBPOSCHANGING, &nm.hdr))
            return FALSE;
    }
    if (pos == t->Pos)
        return FALSE;
    t->Pos = pos;
    InvalidateRect(t->Hwnd, nullptr, TRUE);
    return TRUE;
}

static void set_auto_tics(TRACKBAR *t)
{
    CcFree(t->Tics);
    t->Tics = nullptr;
    t->TicCount = 0;
    if (!(t->Style & TBS_AUTOTICKS) || t->Freq <= 0)
        return;
    int n = (t->Max - t->Min) / t->Freq;
    if (n <= 1)
        return;
    t->Tics = CcAlloc((SIZE_T)n * sizeof(int));
    if (!t->Tics)
        return;
    for (int v = t->Min + t->Freq; v < t->Max; v += t->Freq)
        t->Tics[t->TicCount++] = v;
}

static void paint(TRACKBAR *t, HDC dc)
{
    RECT client, ch, th;
    GetClientRect(t->Hwnd, &client);
    if (t->Theme && IsThemeBackgroundPartiallyTransparent(t->Theme, CC_TKP_TRACK, 1))
        DrawThemeParentBackground(t->Hwnd, dc, &client);
    else
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    channel_rect(t, &ch);
    thumb_rect(t, &th);
    if (t->Theme)
        DrawThemeBackground(t->Theme, dc, vertical(t) ? CC_TKP_TRACKVERT : CC_TKP_TRACK, 1, &ch, nullptr);
    else
        DrawEdge(dc, &ch, EDGE_SUNKEN, BF_RECT);
    if ((t->Style & TBS_ENABLESELRANGE) && t->SelStart >= 0 && t->SelEnd >= t->SelStart) {
        RECT sel = ch;
        if (vertical(t)) {
            sel.top = pos_to_pixel(t, t->SelStart, &ch);
            sel.bottom = pos_to_pixel(t, t->SelEnd, &ch);
        } else {
            sel.left = pos_to_pixel(t, t->SelStart, &ch);
            sel.right = pos_to_pixel(t, t->SelEnd, &ch);
        }
        InflateRect(&sel, vertical(t) ? -1 : 0, vertical(t) ? 0 : -1);
        FillRect(dc, &sel, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
    if (!(t->Style & TBS_NOTICKS)) {
        HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNTEXT));
        HGDIOBJ old = SelectObject(dc, pen);
        int count = t->TicCount + 2;
        for (int k = 0; k < count; k++) {
            int v = k == 0 ? t->Min : k == count - 1 ? t->Max : t->Tics[k - 1];
            int p = pos_to_pixel(t, v, &ch);
            int len = (k == 0 || k == count - 1) ? 4 : 3;
            BOOL before = (t->Style & TBS_TOP) || (t->Style & TBS_BOTH);
            BOOL after = !(t->Style & TBS_TOP) || (t->Style & TBS_BOTH);
            if (vertical(t)) {
                if (after) {
                    MoveToEx(dc, th.right + 2, p, nullptr);
                    LineTo(dc, th.right + 2 + len, p);
                }
                if (before) {
                    MoveToEx(dc, th.left - 3, p, nullptr);
                    LineTo(dc, th.left - 3 - len, p);
                }
            } else {
                if (after) {
                    MoveToEx(dc, p, th.bottom + 2, nullptr);
                    LineTo(dc, p, th.bottom + 2 + len);
                }
                if (before) {
                    MoveToEx(dc, p, th.top - 3, nullptr);
                    LineTo(dc, p, th.top - 3 - len);
                }
            }
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    if (t->Style & TBS_NOTHUMB)
        return;
    BOOL focused = GetFocus() == t->Hwnd;
    if (t->Theme) {
        int part = vertical(t) ? ((t->Style & TBS_BOTH) ? CC_TKP_THUMBVERT : (t->Style & TBS_LEFT) ? 7 : 8)
                               : ((t->Style & TBS_BOTH) ? CC_TKP_THUMB : (t->Style & TBS_TOP) ? 5 : CC_TKP_THUMBBOTTOM);
        int state = !IsWindowEnabled(t->Hwnd) ? 5 : t->Dragging ? 3 : t->Hot ? 2 : focused ? 4 : 1;
        DrawThemeBackground(t->Theme, dc, part, state, &th, nullptr);
    } else {
        FillRect(dc, &th, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(dc, &th, EDGE_RAISED, BF_RECT | BF_SOFT);
    }
    if (focused && !CcFocusCuesHidden(t->Hwnd)) {
        RECT fr = client;
        DrawFocusRect(dc, &fr);
    }
}

static int pixel_to_pos(TRACKBAR *t, int pixel)
{
    RECT ch;
    channel_rect(t, &ch);
    int start = vertical(t) ? ch.top : ch.left, length = vertical(t) ? ch.bottom - ch.top : ch.right - ch.left;
    if (length <= 0)
        return t->Min;
    return clamp_pos(t, t->Min + MulDiv(pixel - start, t->Max - t->Min, length));
}

static void place_buddies(TRACKBAR *t)
{
    RECT wr;
    GetWindowRect(t->Hwnd, &wr);
    HWND parent = GetParent(t->Hwnd);
    MapWindowPoints(nullptr, parent, (POINT *)&wr, 2);
    for (int k = 0; k < 2; k++) {
        HWND b = k ? t->BuddyRight : t->BuddyLeft;
        if (!b)
            continue;
        RECT br;
        GetWindowRect(b, &br);
        int w = br.right - br.left, h = br.bottom - br.top;
        int x, y;
        if (vertical(t)) {
            x = (wr.left + wr.right - w) / 2;
            y = k ? wr.bottom : wr.top - h;
        } else {
            x = k ? wr.right : wr.left - w;
            y = (wr.top + wr.bottom - h) / 2;
        }
        SetWindowPos(b, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK TrackbarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TRACKBAR *t = get_data(hwnd);
    if (!t && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        t = CcAlloc(sizeof(*t));
        if (!t)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)t);
        t->Hwnd = hwnd;
        t->Style = (DWORD)cs->style;
        t->Max = 100;
        t->Page = -1;
        t->Line = 1;
        t->Freq = 1;
        t->SelStart = t->SelEnd = -1;
        t->ThumbLength = 4 * GetSystemMetrics(SM_CXHSCROLL) / 3;
        CcNotifyInit(&t->Notify, hwnd, cs->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        t->Theme = OpenThemeData(hwnd, L"TrackBar");
        set_auto_tics(t);
        return 0;
    case WM_DESTROY:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        CcFree(t->Tics);
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
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SIZE:
    case WM_MOVE:
        place_buddies(t);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        SetFocus(hwnd);
        RECT th;
        thumb_rect(t, &th);
        if (PtInRect(&th, pt) && !(t->Style & TBS_NOTHUMB)) {
            t->Dragging = TRUE;
            t->DragOffset = (vertical(t) ? pt.y - (th.top + th.bottom) / 2 : pt.x - (th.left + th.right) / 2);
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
        } else {
            BOOL before = vertical(t) ? pt.y < th.top : pt.x < th.left;
            if (move_to(t, t->Pos + (before ? -page_size(t) : page_size(t)), before ? TB_PAGEUP : TB_PAGEDOWN))
                notify_scroll(t, before ? TB_PAGEUP : TB_PAGEDOWN);
            notify_scroll(t, TB_ENDTRACK);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        if (t->Dragging) {
            int pos = pixel_to_pos(t, (vertical(t) ? pt.y : pt.x) - t->DragOffset);
            if (move_to(t, pos, TB_THUMBTRACK))
                notify_scroll(t, TB_THUMBTRACK);
        } else {
            RECT th;
            thumb_rect(t, &th);
            BOOL hot = PtInRect(&th, pt);
            if (hot != t->Hot) {
                t->Hot = hot;
                InvalidateRect(hwnd, nullptr, TRUE);
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        t->Hot = FALSE;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (t->Dragging) {
            t->Dragging = FALSE;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            notify_scroll(t, TB_THUMBPOSITION);
            notify_scroll(t, TB_ENDTRACK);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_KEYDOWN: {
        int code = -1, pos = t->Pos;
        switch (wparam) {
        case VK_PRIOR:
            code = TB_PAGEUP;
            pos -= page_size(t);
            break;
        case VK_NEXT:
            code = TB_PAGEDOWN;
            pos += page_size(t);
            break;
        case VK_END:
            code = TB_BOTTOM;
            pos = t->Max;
            break;
        case VK_HOME:
            code = TB_TOP;
            pos = t->Min;
            break;
        case VK_LEFT:
        case VK_UP:
            code = TB_LINEUP;
            pos -= t->Line;
            break;
        case VK_RIGHT:
        case VK_DOWN:
            code = TB_LINEDOWN;
            pos += t->Line;
            break;
        }
        if (code >= 0) {
            if (move_to(t, pos, code))
                notify_scroll(t, code);
            return 0;
        }
        break;
    }
    case WM_KEYUP:
        switch (wparam) {
        case VK_PRIOR:
        case VK_NEXT:
        case VK_END:
        case VK_HOME:
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
            notify_scroll(t, TB_ENDTRACK);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL: {
        int step = (t->Max - t->Min) / 50;
        if (step < 1)
            step = 1;
        int delta = (short)HIWORD(wparam);
        if (move_to(t, t->Pos - delta / WHEEL_DELTA * step, TB_THUMBPOSITION)) {
            notify_scroll(t, TB_THUMBPOSITION);
            notify_scroll(t, TB_ENDTRACK);
        }
        return 0;
    }
    case WM_THEMECHANGED:
        if (t->Theme)
            CloseThemeData(t->Theme);
        t->Theme = OpenThemeData(hwnd, L"TrackBar");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            t->Style = ((STYLESTRUCT *)lparam)->styleNew;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&t->Notify, (HWND)wparam, lparam);
    case TBM_GETPOS:
        return t->Pos;
    case TBM_GETRANGEMIN:
        return t->Min;
    case TBM_GETRANGEMAX:
        return t->Max;
    case TBM_GETTIC:
        return (int)wparam >= 0 && (int)wparam < t->TicCount ? t->Tics[wparam] : -1;
    case TBM_SETTIC: {
        int v = (int)lparam;
        if (v <= t->Min || v >= t->Max)
            return FALSE;
        int *tics = CcReAlloc(t->Tics, ((SIZE_T)t->TicCount + 1) * sizeof(int));
        if (!tics)
            return FALSE;
        t->Tics = tics;
        t->Tics[t->TicCount++] = v;
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    }
    case TBM_SETPOS:
        t->Pos = clamp_pos(t, (int)lparam);
        if (wparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TBM_SETPOSNOTIFY:
        t->Pos = clamp_pos(t, (int)lparam);
        InvalidateRect(hwnd, nullptr, TRUE);
        SendMessageW(t->Notify.Parent, vertical(t) ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, (WORD)t->Pos),
                     (LPARAM)hwnd);
        return 0;
    case TBM_SETRANGE:
    case TBM_SETRANGEMIN:
    case TBM_SETRANGEMAX:
        if (msg == TBM_SETRANGE) {
            t->Min = (short)LOWORD(lparam);
            t->Max = (short)HIWORD(lparam);
        } else if (msg == TBM_SETRANGEMIN) {
            t->Min = (int)lparam;
        } else {
            t->Max = (int)lparam;
        }
        if (t->Max < t->Min)
            t->Max = t->Min;
        t->Pos = clamp_pos(t, t->Pos);
        if (t->SelStart >= 0) {
            t->SelStart = clamp_pos(t, t->SelStart);
            t->SelEnd = clamp_pos(t, t->SelEnd);
        }
        set_auto_tics(t);
        if (wparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TBM_CLEARTICS:
        CcFree(t->Tics);
        t->Tics = nullptr;
        t->TicCount = 0;
        if (wparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TBM_SETSEL:
    case TBM_SETSELSTART:
    case TBM_SETSELEND:
        if (!(t->Style & TBS_ENABLESELRANGE))
            return 0;
        if (msg == TBM_SETSEL) {
            t->SelStart = clamp_pos(t, (short)LOWORD(lparam));
            t->SelEnd = clamp_pos(t, (short)HIWORD(lparam));
        } else if (msg == TBM_SETSELSTART) {
            t->SelStart = clamp_pos(t, (int)lparam);
        } else {
            t->SelEnd = clamp_pos(t, (int)lparam);
        }
        if (wparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TBM_GETPTICS:
        return (LRESULT)t->Tics;
    case TBM_GETTICPOS: {
        if ((int)wparam < 0 || (int)wparam >= t->TicCount)
            return -1;
        RECT ch;
        channel_rect(t, &ch);
        return pos_to_pixel(t, t->Tics[wparam], &ch);
    }
    case TBM_GETNUMTICS:
        if (t->Style & TBS_NOTICKS)
            return 0;
        return t->TicCount + 2;
    case TBM_GETSELSTART:
        return t->SelStart;
    case TBM_GETSELEND:
        return t->SelEnd;
    case TBM_CLEARSEL:
        t->SelStart = t->SelEnd = -1;
        if (wparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TBM_SETTICFREQ:
        t->Freq = (int)wparam;
        set_auto_tics(t);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case TBM_SETPAGESIZE: {
        int old = page_size(t);
        t->Page = (int)lparam;
        return old;
    }
    case TBM_GETPAGESIZE:
        return page_size(t);
    case TBM_SETLINESIZE: {
        int old = t->Line;
        t->Line = (int)lparam;
        return old;
    }
    case TBM_GETLINESIZE:
        return t->Line;
    case TBM_GETTHUMBRECT:
        if (lparam)
            thumb_rect(t, (RECT *)lparam);
        return 0;
    case TBM_GETCHANNELRECT:
        if (lparam)
            channel_rect(t, (RECT *)lparam);
        return 0;
    case TBM_SETTHUMBLENGTH:
        if (t->Style & TBS_FIXEDLENGTH) {
            t->ThumbLength = (int)wparam;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case TBM_GETTHUMBLENGTH:
        return t->ThumbLength;
    case TBM_SETTOOLTIPS:
        t->Tooltips = (HWND)wparam;
        return 0;
    case TBM_GETTOOLTIPS:
        return (LRESULT)t->Tooltips;
    case TBM_SETTIPSIDE: {
        int old = t->TipSide;
        t->TipSide = (int)wparam;
        return old;
    }
    case TBM_SETBUDDY: {
        HWND *slot = wparam ? &t->BuddyLeft : &t->BuddyRight;
        HWND old = *slot;
        *slot = (HWND)lparam;
        place_buddies(t);
        return (LRESULT)old;
    }
    case TBM_GETBUDDY:
        return (LRESULT)(wparam ? t->BuddyLeft : t->BuddyRight);
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
