/*
 * the ScrollBar control of the common controls 6, drawn with the visual style. a program whose manifest names the
 * assembly gets this class in place of the one of user32, which draws the classic bar.
 */
#include "comctl32_private.h"

enum { PART_NONE, PART_ARROW1, PART_PAGE1, PART_THUMB, PART_PAGE2, PART_ARROW2 };

typedef struct SCROLLBAR {
    int Min, Max;
    UINT Page;
    int Pos, TrackPos;
    UINT Arrows; /* ESB_DISABLE_* */
    int Held, Hot;
} SCROLLBAR;

typedef struct BAR {
    BOOL Vert, Enabled, HasThumb;
    RECT Arrow1, Arrow2, Track, Thumb;
    int Room, Size;
} BAR;

static SCROLLBAR *get_data(HWND hwnd)
{
    return (SCROLLBAR *)GetWindowLongPtrW(hwnd, 0);
}

static BOOL is_box(HWND hwnd)
{
    return (GetWindowLongW(hwnd, GWL_STYLE) & (SBS_SIZEBOX | SBS_SIZEGRIP)) != 0;
}

static void layout(HWND hwnd, const SCROLLBAR *d, BAR *b)
{
    RECT r;
    GetClientRect(hwnd, &r);
    memset(b, 0, sizeof(*b));
    b->Vert = (GetWindowLongW(hwnd, GWL_STYLE) & SBS_VERT) != 0;
    b->Enabled = IsWindowEnabled(hwnd) && d->Arrows != ESB_DISABLE_BOTH;
    int length = b->Vert ? r.bottom : r.right, thick = b->Vert ? r.right : r.bottom;
    UINT dpi = GetDpiForWindow(hwnd);
    int button = GetSystemMetricsForDpi(b->Vert ? SM_CYVSCROLL : SM_CXHSCROLL, dpi);
    int arrow = button * 2 <= length ? button : length / 2;
    b->Arrow1 = b->Arrow2 = b->Track = r;
    if (b->Vert) {
        b->Arrow1.bottom = b->Track.top = arrow;
        b->Arrow2.top = b->Track.bottom = r.bottom - arrow;
    } else {
        b->Arrow1.right = b->Track.left = arrow;
        b->Arrow2.left = b->Track.right = r.right - arrow;
    }
    LONGLONG span = (LONGLONG)d->Max - d->Min + 1;
    b->Room = b->Vert ? b->Track.bottom - b->Track.top : b->Track.right - b->Track.left;
    if (!b->Enabled || span <= 0 || b->Room <= 0 || (d->Page && d->Page >= span))
        return;
    int smallest = GetSystemMetricsForDpi(b->Vert ? SM_CYVTHUMB : SM_CXHTHUMB, dpi) / 2;
    int size = d->Page ? (int)((LONGLONG)b->Room * d->Page / span) : thick;
    if (size < smallest)
        size = smallest;
    if (size > b->Room)
        return;
    LONGLONG steps = span - (d->Page ? d->Page : 1);
    int pos = d->Held == PART_THUMB ? d->TrackPos : d->Pos;
    int offset = steps > 0 ? (int)((LONGLONG)(b->Room - size) * ((LONGLONG)pos - d->Min) / steps) : 0;
    b->Size = size;
    b->HasThumb = TRUE;
    b->Thumb = b->Track;
    if (b->Vert) {
        b->Thumb.top += offset;
        b->Thumb.bottom = b->Thumb.top + size;
    } else {
        b->Thumb.left += offset;
        b->Thumb.right = b->Thumb.left + size;
    }
}

static int part_at(const BAR *b, POINT pt)
{
    if (PtInRect(&b->Arrow1, pt))
        return PART_ARROW1;
    if (PtInRect(&b->Arrow2, pt))
        return PART_ARROW2;
    if (!PtInRect(&b->Track, pt) || !b->HasThumb)
        return PART_NONE;
    if (PtInRect(&b->Thumb, pt))
        return PART_THUMB;
    return (b->Vert ? pt.y < b->Thumb.top : pt.x < b->Thumb.left) ? PART_PAGE1 : PART_PAGE2;
}

static void paint(HWND hwnd, SCROLLBAR *d, HDC dc)
{
    HTHEME theme = CcClassTheme(hwnd, L"ScrollBar");
    if (is_box(hwnd)) {
        RECT r;
        GetClientRect(hwnd, &r);
        BOOL grip = (GetWindowLongW(hwnd, GWL_STYLE) & SBS_SIZEGRIP) != 0;
        if (!theme || FAILED(DrawThemeBackground(theme, dc, CC_SBP_SIZEBOX, 1, &r, nullptr))) {
            FillRect(dc, &r, GetSysColorBrush(COLOR_3DFACE));
            if (grip)
                DrawFrameControl(dc, &r, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
        }
        return;
    }
    BAR b;
    layout(hwnd, d, &b);
    BOOL off1 = !b.Enabled || (d->Arrows & ESB_DISABLE_LTUP), off2 = !b.Enabled || (d->Arrows & ESB_DISABLE_RTDN);
    RECT before = b.Track, after = b.Track;
    if (!b.HasThumb) {
        after.left = after.right;
    } else if (b.Vert) {
        before.bottom = b.Thumb.top;
        after.top = b.Thumb.bottom;
    } else {
        before.right = b.Thumb.left;
        after.left = b.Thumb.right;
    }
    if (!theme) {
        DrawFrameControl(dc, &b.Arrow1, DFC_SCROLL,
                         (b.Vert ? DFCS_SCROLLUP : DFCS_SCROLLLEFT) | (off1 ? DFCS_INACTIVE : 0) |
                             (d->Held == PART_ARROW1 ? DFCS_PUSHED | DFCS_FLAT : 0));
        DrawFrameControl(dc, &b.Arrow2, DFC_SCROLL,
                         (b.Vert ? DFCS_SCROLLDOWN : DFCS_SCROLLRIGHT) | (off2 ? DFCS_INACTIVE : 0) |
                             (d->Held == PART_ARROW2 ? DFCS_PUSHED | DFCS_FLAT : 0));
        FillRect(dc, &b.Track, GetSysColorBrush(COLOR_SCROLLBAR));
        if (b.HasThumb)
            DrawEdge(dc, &b.Thumb, EDGE_RAISED, BF_RECT | BF_MIDDLE);
        return;
    }
    int hot = d->Held == PART_NONE ? d->Hot : PART_NONE;
    /* the buttons up, down, left and right have four states each: normal, hot, pressed and disabled. */
    int state1 = (b.Vert ? 0 : 8) + (off1 ? 4 : d->Held == PART_ARROW1 ? 3 : hot == PART_ARROW1 ? 2 : 1);
    int state2 = (b.Vert ? 4 : 12) + (off2 ? 4 : d->Held == PART_ARROW2 ? 3 : hot == PART_ARROW2 ? 2 : 1);
    DrawThemeBackground(theme, dc, CC_SBP_ARROWBTN, state1, &b.Arrow1, nullptr);
    DrawThemeBackground(theme, dc, CC_SBP_ARROWBTN, state2, &b.Arrow2, nullptr);
    if (!IsRectEmpty(&before))
        DrawThemeBackground(theme, dc, b.Vert ? CC_SBP_LOWERTRACKVERT : CC_SBP_LOWERTRACKHORZ,
                            !b.Enabled ? 4 : d->Held == PART_PAGE1 ? 3 : 1, &before, nullptr);
    if (!IsRectEmpty(&after))
        DrawThemeBackground(theme, dc, b.Vert ? CC_SBP_UPPERTRACKVERT : CC_SBP_UPPERTRACKHORZ,
                            d->Held == PART_PAGE2 ? 3 : 1, &after, nullptr);
    if (b.HasThumb)
        DrawThemeBackground(theme, dc, b.Vert ? CC_SBP_THUMBBTNVERT : CC_SBP_THUMBBTNHORZ,
                            d->Held == PART_THUMB ? 3 : hot == PART_THUMB ? 2 : 1, &b.Thumb, nullptr);
}

static void redraw(HWND hwnd)
{
    InvalidateRect(hwnd, nullptr, FALSE);
}

/* clamps the page and the position as windows does, and answers the position. */
static int set_info(HWND hwnd, SCROLLBAR *d, const SCROLLINFO *info, BOOL draw)
{
    UINT mask = info->fMask;
    if (mask & SIF_PAGE)
        d->Page = info->nPage;
    if (mask & SIF_RANGE) {
        /* an invalid range becomes 0 to 0. */
        if (info->nMin > info->nMax || (UINT)(info->nMax - info->nMin) >= 0x80000000u) {
            d->Min = d->Max = 0;
        } else {
            d->Min = info->nMin;
            d->Max = info->nMax;
        }
    }
    if (mask & SIF_POS)
        d->Pos = info->nPos;
    UINT span = (UINT)(d->Max - d->Min) + 1;
    if (d->Page > span)
        d->Page = span;
    int last = d->Max - (d->Page ? (int)d->Page - 1 : 0);
    if (d->Pos > last)
        d->Pos = last;
    if (d->Pos < d->Min)
        d->Pos = d->Min;
    if (d->Held != PART_THUMB)
        d->TrackPos = d->Pos;
    if (mask & (SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL)) {
        BOOL empty = d->Min >= last;
        if (empty && (mask & SIF_DISABLENOSCROLL))
            d->Arrows = ESB_DISABLE_BOTH;
        else if (!empty && d->Arrows == ESB_DISABLE_BOTH && (mask & SIF_DISABLENOSCROLL))
            d->Arrows = ESB_ENABLE_BOTH;
    }
    if (draw)
        redraw(hwnd);
    return d->Pos;
}

static BOOL get_info(const SCROLLBAR *d, SCROLLINFO *info)
{
    if (info->fMask & SIF_RANGE) {
        info->nMin = d->Min;
        info->nMax = d->Max;
    }
    if (info->fMask & SIF_PAGE)
        info->nPage = d->Page;
    if (info->fMask & SIF_POS)
        info->nPos = d->Pos;
    if ((info->fMask & SIF_TRACKPOS) && info->cbSize == sizeof(SCROLLINFO))
        info->nTrackPos = d->Held == PART_THUMB ? d->TrackPos : d->Pos;
    return (info->fMask & SIF_ALL) != 0;
}

static BOOL valid_size(UINT size)
{
    return size == sizeof(SCROLLINFO) || size == offsetof(SCROLLINFO, nTrackPos);
}

/*
 * the accessibility interface: the arrows, the page areas and the thumb are unavailable when they are disabled, and
 * the page area the thumb touches is invisible.
 */
static BOOL bar_info(HWND hwnd, const SCROLLBAR *d, SCROLLBARINFO *info)
{
    BAR b;
    layout(hwnd, d, &b);
    memset(&info->rcScrollBar, 0, sizeof(*info) - sizeof(info->cbSize));
    GetClientRect(hwnd, &info->rcScrollBar);
    MapWindowPoints(hwnd, nullptr, (POINT *)&info->rcScrollBar, 2);
    info->dxyLineButton = b.Vert ? b.Arrow1.bottom - b.Arrow1.top : b.Arrow1.right - b.Arrow1.left;
    RECT thumb = b.HasThumb ? b.Thumb : (RECT){b.Track.left, b.Track.top, b.Track.left, b.Track.top};
    info->xyThumbTop = b.Vert ? thumb.top : thumb.left;
    info->xyThumbBottom = b.Vert ? thumb.bottom : thumb.right;
    UINT arrows = b.Enabled ? d->Arrows : ESB_DISABLE_BOTH;
    if (arrows & ESB_DISABLE_LTUP)
        info->rgstate[1] = info->rgstate[2] = STATE_SYSTEM_UNAVAILABLE;
    if (arrows & ESB_DISABLE_RTDN)
        info->rgstate[5] = info->rgstate[4] = STATE_SYSTEM_UNAVAILABLE;
    if (arrows == ESB_DISABLE_BOTH)
        info->rgstate[0] = STATE_SYSTEM_UNAVAILABLE;
    /* the parts are numbered as the states are. */
    if (d->Held == PART_ARROW1 || d->Held == PART_ARROW2)
        info->rgstate[d->Held] |= STATE_SYSTEM_PRESSED;
    if (info->xyThumbTop == (b.Vert ? b.Track.top : b.Track.left))
        info->rgstate[2] |= STATE_SYSTEM_INVISIBLE;
    if (info->xyThumbBottom == (b.Vert ? b.Track.bottom : b.Track.right))
        info->rgstate[4] |= STATE_SYSTEM_INVISIBLE;
    return TRUE;
}

static void notify(HWND hwnd, UINT code, int pos)
{
    HWND parent = GetParent(hwnd);
    if (parent)
        SendMessageW(parent, (GetWindowLongW(hwnd, GWL_STYLE) & SBS_VERT) ? WM_VSCROLL : WM_HSCROLL,
                     MAKEWPARAM(code, (WORD)pos), (LPARAM)hwnd);
}

static int thumb_position(const SCROLLBAR *d, const BAR *b, POINT pt, int grab)
{
    LONGLONG steps = (LONGLONG)d->Max - d->Min + 1 - (d->Page ? d->Page : 1);
    int room = b->Room - b->Size;
    int at = (b->Vert ? pt.y - b->Track.top : pt.x - b->Track.left) - grab;
    if (at < 0)
        at = 0;
    if (at > room)
        at = room;
    return room > 0 && steps > 0 ? (int)(d->Min + ((LONGLONG)at * steps + room / 2) / room) : d->Min;
}

/* a press on the bar, until the button is released: arrows and the track repeat, the thumb follows the mouse. */
static void track(HWND hwnd, POINT pt)
{
    static const WORD codes[] = {0, SB_LINEUP, SB_PAGEUP, SB_THUMBTRACK, SB_PAGEDOWN, SB_LINEDOWN};
    SCROLLBAR *d = get_data(hwnd);
    BAR b;
    layout(hwnd, d, &b);
    int part = part_at(&b, pt);
    if (!b.Enabled || part == PART_NONE || (part == PART_ARROW1 && (d->Arrows & ESB_DISABLE_LTUP)) ||
        (part == PART_ARROW2 && (d->Arrows & ESB_DISABLE_RTDN)))
        return;
    int grab = part == PART_THUMB ? (b.Vert ? pt.y - b.Thumb.top : pt.x - b.Thumb.left) : 0;
    d->Held = part;
    d->TrackPos = d->Pos;
    SetCapture(hwnd);
    redraw(hwnd);
    UpdateWindow(hwnd);
    if (part != PART_THUMB)
        notify(hwnd, codes[part], 0);
    DWORD next = GetTickCount() + 400;
    BOOL cancelled = FALSE;
    while (IsWindow(hwnd) && GetCapture() == hwnd && (d = get_data(hwnd)) != nullptr) {
        MSG m;
        BOOL up = FALSE;
        while (!up && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_LBUTTONUP) {
                up = TRUE;
            } else if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
                up = cancelled = TRUE;
            } else if (m.message == WM_QUIT) {
                PostQuitMessage((int)m.wParam);
                up = TRUE;
            } else if ((m.message < WM_MOUSEFIRST || m.message > WM_MOUSELAST) &&
                       (m.message < WM_KEYFIRST || m.message > WM_KEYLAST)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        if (up || !IsWindow(hwnd) || !(d = get_data(hwnd)))
            break;
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);
        layout(hwnd, d, &b);
        if (part == PART_THUMB) {
            int pos = thumb_position(d, &b, cursor, grab);
            if (pos != d->TrackPos) {
                d->TrackPos = pos;
                redraw(hwnd);
                UpdateWindow(hwnd);
                notify(hwnd, SB_THUMBTRACK, pos);
            }
        } else if ((LONG)(GetTickCount() - next) >= 0) {
            if (part_at(&b, cursor) == part)
                notify(hwnd, codes[part], 0);
            next = GetTickCount() + 50;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    if (!IsWindow(hwnd) || !(d = get_data(hwnd)))
        return;
    if (GetCapture() == hwnd)
        ReleaseCapture();
    int pos = cancelled ? d->Pos : d->TrackPos;
    d->Held = PART_NONE;
    redraw(hwnd);
    if (part == PART_THUMB)
        notify(hwnd, SB_THUMBPOSITION, pos);
    notify(hwnd, SB_ENDSCROLL, 0);
}

/* a bar made with an alignment style keeps the standard thickness at an edge of the rectangle it was given. */
static void align(HWND hwnd, const CREATESTRUCTW *cs)
{
    DWORD style = (DWORD)cs->style;
    UINT dpi = GetDpiForWindow(hwnd);
    int x = cs->x, y = cs->y, cx = cs->cx, cy = cs->cy;
    if (style & (SBS_SIZEBOX | SBS_SIZEGRIP)) {
        int w = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi), h = GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
        if (style & SBS_SIZEBOXTOPLEFTALIGN) {
            cx = w, cy = h;
        } else if (style & SBS_SIZEBOXBOTTOMRIGHTALIGN) {
            x += cx - w, y += cy - h, cx = w, cy = h;
        }
    } else if (style & SBS_VERT) {
        int w = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
        if (style & SBS_LEFTALIGN)
            cx = w;
        else if (style & SBS_RIGHTALIGN)
            x += cx - w, cx = w;
    } else {
        int h = GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
        if (style & SBS_TOPALIGN)
            cy = h;
        else if (style & SBS_BOTTOMALIGN)
            y += cy - h, cy = h;
    }
    if (x != cs->x || y != cs->y || cx != cs->cx || cy != cs->cy)
        MoveWindow(hwnd, x, y, cx, cy, FALSE);
}

LRESULT CALLBACK ScrollBarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SCROLLBAR *d = get_data(hwnd);
    if (!d && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    SCROLLINFO si = {sizeof(si)};
    switch (msg) {
    case WM_NCCREATE:
        d = CcAlloc(sizeof(*d));
        if (!d)
            return FALSE;
        /* a fresh bar has the range 0 to 100, like on windows. */
        *d = (SCROLLBAR){.Max = 100};
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)d);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(d);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CREATE:
        align(hwnd, (const CREATESTRUCTW *)lparam);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : (HDC)wparam;
        if (dc)
            paint(hwnd, d, dc);
        if (msg == WM_PAINT)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ENABLE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        redraw(hwnd);
        return 0;
    case WM_NCHITTEST:
        if (GetWindowLongW(hwnd, GWL_STYLE) & SBS_SIZEGRIP)
            return HTBOTTOMRIGHT;
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!is_box(hwnd))
            track(hwnd, (POINT){(short)LOWORD(lparam), (short)HIWORD(lparam)});
        return 0;
    case WM_MOUSEMOVE: {
        if (is_box(hwnd) || d->Held != PART_NONE)
            return 0;
        BAR b;
        layout(hwnd, d, &b);
        int part = part_at(&b, (POINT){(short)LOWORD(lparam), (short)HIWORD(lparam)});
        if (part != d->Hot) {
            TRACKMOUSEEVENT leave = {sizeof(leave), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&leave);
            d->Hot = part;
            redraw(hwnd);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (d->Hot != PART_NONE) {
            d->Hot = PART_NONE;
            redraw(hwnd);
        }
        return 0;
    case WM_KEYDOWN: {
        static const struct {
            WPARAM Key;
            WORD Code;
        } keys[] = {{VK_UP, SB_LINEUP},    {VK_LEFT, SB_LINEUP},  {VK_DOWN, SB_LINEDOWN}, {VK_RIGHT, SB_LINEDOWN},
                    {VK_PRIOR, SB_PAGEUP}, {VK_NEXT, SB_PAGEDOWN}, {VK_HOME, SB_TOP},      {VK_END, SB_BOTTOM}};
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(keys); i++)
            if (keys[i].Key == wparam) {
                notify(hwnd, keys[i].Code, 0);
                if (IsWindow(hwnd))
                    notify(hwnd, SB_ENDSCROLL, 0);
                break;
            }
        return 0;
    }
    case SBM_SETPOS: {
        int old = d->Pos;
        si.fMask = SIF_POS;
        si.nPos = (int)wparam;
        set_info(hwnd, d, &si, (BOOL)lparam);
        return old;
    }
    case SBM_GETPOS:
        return d->Pos;
    case SBM_SETRANGE:
    case SBM_SETRANGEREDRAW: {
        int old = d->Pos;
        si.fMask = SIF_RANGE;
        si.nMin = (int)wparam;
        si.nMax = (int)lparam;
        set_info(hwnd, d, &si, msg == SBM_SETRANGEREDRAW);
        return old;
    }
    case SBM_GETRANGE:
        if (wparam)
            *(LPINT)wparam = d->Min;
        if (lparam)
            *(LPINT)lparam = d->Max;
        return 0;
    case SBM_ENABLE_ARROWS: {
        UINT arrows = (UINT)wparam & ESB_DISABLE_BOTH;
        if (arrows == d->Arrows)
            return FALSE;
        d->Arrows = arrows;
        redraw(hwnd);
        return TRUE;
    }
    case SBM_SETSCROLLINFO: {
        const SCROLLINFO *in = (const SCROLLINFO *)lparam;
        return in && valid_size(in->cbSize) ? set_info(hwnd, d, in, (BOOL)wparam) : 0;
    }
    case SBM_GETSCROLLINFO: {
        SCROLLINFO *out = (SCROLLINFO *)lparam;
        return out && valid_size(out->cbSize) && get_info(d, out);
    }
    case SBM_GETSCROLLBARINFO:
        return lparam && ((SCROLLBARINFO *)lparam)->cbSize == sizeof(SCROLLBARINFO) &&
               bar_info(hwnd, d, (SCROLLBARINFO *)lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
