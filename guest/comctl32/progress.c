/* the progress bar, msctls_progress32, with the PBS_MARQUEE block that slides on a timer. */
#include "comctl32_private.h"

#define TIMER_MARQUEE 1
#define TIMER_ANIMATE 2

typedef struct PROGRESS {
    HWND Hwnd;
    DWORD Style;
    int Low, High, Pos, Step;
    /* the drawn position, which catches up with Pos when animated. */
    int Shown, AnimateFrom;
    DWORD AnimateStart, AnimateDuration;
    int State;
    COLORREF Bar, Bk;
    HFONT Font;
    HTHEME Theme;
    BOOL Marquee;
    int MarqueeOffset;
    UINT MarqueeInterval;
} PROGRESS;

static PROGRESS *get_data(HWND hwnd)
{
    return (PROGRESS *)GetWindowLongPtrW(hwnd, 0);
}

static void open_theme(PROGRESS *p)
{
    if (p->Theme)
        CloseThemeData(p->Theme);
    p->Theme = OpenThemeData(p->Hwnd, (p->Style & PBS_MARQUEE) ? L"Indeterminate::Progress" : L"Progress");
}

/* the theme's border, or a static edge without a theme. */
static void adjust_ex_style(PROGRESS *p)
{
    DWORD ex = (DWORD)GetWindowLongW(p->Hwnd, GWL_EXSTYLE);
    DWORD want = ex & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    if (!p->Theme)
        want |= WS_EX_STATICEDGE;
    if (want != ex) {
        SetWindowLongW(p->Hwnd, GWL_EXSTYLE, (LONG)want);
        SetWindowPos(p->Hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void update_marquee_timer(PROGRESS *p)
{
    if ((p->Style & PBS_MARQUEE) && p->Marquee && p->State == PBST_NORMAL)
        SetTimer(p->Hwnd, TIMER_MARQUEE, p->MarqueeInterval, nullptr);
    else
        KillTimer(p->Hwnd, TIMER_MARQUEE);
}

/* the theme may ease the fill toward a new position. */
static BOOL can_animate(PROGRESS *p, int *interval)
{
    int fps = 0;
    BOOL client_animation = FALSE;
    if (!p->Theme || p->State != PBST_NORMAL)
        return FALSE;
    if (FAILED(GetThemeInt(p->Theme, 0, 0, TMT_FRAMESPERSECOND, &fps)) || fps <= 0)
        return FALSE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &client_animation, 0) || !client_animation)
        return FALSE;
    *interval = 1000 / fps;
    return TRUE;
}

static void stop_animation(PROGRESS *p)
{
    KillTimer(p->Hwnd, TIMER_ANIMATE);
    p->Shown = p->Pos;
}

static void redraw(PROGRESS *p)
{
    InvalidateRect(p->Hwnd, nullptr, TRUE);
}

static int update_position(PROGRESS *p, int value, BOOL wrap)
{
    int old = p->Pos;
    if (p->Style & PBS_MARQUEE) {
        RedrawWindow(p->Hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        return old;
    }
    int span = p->High - p->Low;
    if (p->Low == p->High)
        value = p->Low;
    else if (wrap && span > 0 && value < p->Low)
        value = p->High - (p->Low - value) % span;
    else if (wrap && span > 0 && value > p->High)
        value = p->Low + (value - p->High) % span;
    else if (value < p->Low)
        value = p->Low;
    else if (value > p->High)
        value = p->High;
    if (value == old)
        return old;
    p->Pos = value;
    int interval;
    if (value < old && p->State == PBST_NORMAL && !(p->Style & PBS_SMOOTHREVERSE)) {
        stop_animation(p);
    } else if (can_animate(p, &interval)) {
        int duration = 0;
        if (FAILED(GetThemeInt(p->Theme, 0, 0, TMT_ANIMATIONDURATION, &duration)) || duration <= 0)
            duration = 500;
        p->AnimateFrom = p->Shown;
        p->AnimateStart = GetTickCount();
        p->AnimateDuration = (DWORD)duration;
        SetTimer(p->Hwnd, TIMER_ANIMATE, (UINT)interval, nullptr);
    } else {
        stop_animation(p);
    }
    redraw(p);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, p->Hwnd, OBJID_CLIENT, 0);
    return old;
}

static void animate_step(PROGRESS *p)
{
    DWORD elapsed = GetTickCount() - p->AnimateStart;
    if (elapsed >= p->AnimateDuration || !p->AnimateDuration) {
        stop_animation(p);
    } else {
        p->Shown = p->AnimateFrom + MulDiv(p->Pos - p->AnimateFrom, (int)elapsed, (int)p->AnimateDuration);
    }
    redraw(p);
}

static LRESULT set_range(PROGRESS *p, int low, int high)
{
    LRESULT old = MAKELONG((WORD)p->Low, (WORD)p->High);
    if (low != p->Low || high != p->High) {
        p->Low = low;
        p->High = high;
        RedrawWindow(p->Hwnd, nullptr, nullptr, RDW_INVALIDATE);
        int pos = p->Pos;
        int clamped = pos < low ? low : pos > high ? high : pos;
        if (low == high)
            clamped = low;
        if (!(p->Style & PBS_MARQUEE) && clamped != pos) {
            p->Pos = clamped;
            stop_animation(p);
        }
    }
    return old;
}

static int fill_extent(PROGRESS *p, int length)
{
    if (p->Style & PBS_MARQUEE)
        return length;
    if (p->High <= p->Low)
        return 0;
    int v = p->Shown < p->Low ? p->Low : p->Shown > p->High ? p->High : p->Shown;
    return MulDiv(length, v - p->Low, p->High - p->Low);
}

static void paint_themed(PROGRESS *p, HDC dc, const RECT *client)
{
    BOOL vertical = (p->Style & PBS_VERTICAL) != 0;
    int bar = vertical ? CC_PP_TRANSPARENTBARVERT : CC_PP_TRANSPARENTBAR;
    if (IsThemeBackgroundPartiallyTransparent(p->Theme, bar, 0) &&
        FAILED(DrawThemeParentBackground(p->Hwnd, dc, client)))
        FillRect(dc, client, GetSysColorBrush(COLOR_BTNFACE));
    DrawThemeBackground(p->Theme, dc, bar, 0, client, nullptr);
    RECT content;
    if (FAILED(GetThemeBackgroundContentRect(p->Theme, dc, bar, 0, client, &content)))
        content = *client;
    RECT fill = content;
    if (vertical)
        fill.top = fill.bottom - fill_extent(p, content.bottom - content.top);
    else
        fill.right = fill.left + fill_extent(p, content.right - content.left);
    int state = p->State == PBST_ERROR ? 2 : p->State == PBST_PAUSED ? 3 : 1;
    if (!IsRectEmpty(&fill))
        DrawThemeBackground(p->Theme, dc, vertical ? CC_PP_FILLVERT : CC_PP_FILL, state, &fill, nullptr);
    if ((p->Style & PBS_MARQUEE) && p->State == PBST_NORMAL && !IsRectEmpty(&fill)) {
        int part = vertical ? CC_PP_MOVEOVERLAYVERT : CC_PP_MOVEOVERLAY;
        SIZE size = {};
        GetThemePartSize(p->Theme, dc, part, 1, nullptr, TS_TRUE, &size);
        int step = 0;
        if (FAILED(GetThemeInt(p->Theme, 0, 0, TMT_PIXELSPERFRAME, &step)) || step <= 0)
            step = 6;
        int length = vertical ? fill.bottom - fill.top : fill.right - fill.left;
        int overlay = vertical ? size.cy : size.cx;
        if (overlay <= 0)
            overlay = length / 3 > 0 ? length / 3 : 1;
        p->MarqueeOffset += step;
        if (p->MarqueeOffset > length + overlay)
            p->MarqueeOffset = 0;
        RECT r = fill;
        if (vertical) {
            r.bottom = fill.bottom - p->MarqueeOffset + overlay;
            r.top = r.bottom - overlay;
        } else {
            r.right = fill.left + p->MarqueeOffset;
            r.left = r.right - overlay;
        }
        DrawThemeBackground(p->Theme, dc, part, 1, &r, &fill);
    }
}

static void paint_classic(PROGRESS *p, HDC dc, const RECT *client)
{
    CcFillSolid(dc, client, p->Bk == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : p->Bk);
    RECT area = *client;
    InflateRect(&area, -1, -1);
    if (IsRectEmpty(&area))
        return;
    COLORREF color = p->Bar == CLR_DEFAULT ? GetSysColor(COLOR_HIGHLIGHT) : p->Bar;
    if (p->State == PBST_ERROR)
        color = RGB(255, 0, 0);
    else if (p->State == PBST_PAUSED)
        color = RGB(255, 255, 0);
    BOOL vertical = (p->Style & PBS_VERTICAL) != 0;
    int length = vertical ? area.bottom - area.top : area.right - area.left;
    if (!(p->Style & PBS_MARQUEE)) {
        RECT fill = area;
        if (vertical)
            fill.top = fill.bottom - fill_extent(p, length);
        else
            fill.right = fill.left + fill_extent(p, length);
        CcFillSolid(dc, &fill, color);
        return;
    }
    int block = length > 114 ? 38 : (length / 3 > 0 ? length / 3 : 1);
    p->MarqueeOffset += 8;
    if (p->MarqueeOffset > length + 38)
        p->MarqueeOffset -= length;
    /* the block, then the part past the end drawn again at the start. */
    for (int pass = 0; pass < 2; pass++) {
        int end = p->MarqueeOffset - pass * length, start = end - block;
        if (end <= 0 || start >= length)
            continue;
        if (start < 0)
            start = 0;
        if (end > length)
            end = length;
        RECT r = area;
        if (vertical) {
            r.bottom = area.bottom - start;
            r.top = area.bottom - end;
        } else {
            r.left = area.left + start;
            r.right = area.left + end;
        }
        CcFillSolid(dc, &r, color);
    }
}

static void paint(PROGRESS *p, HDC dc)
{
    RECT client;
    GetClientRect(p->Hwnd, &client);
    if (p->Theme)
        paint_themed(p, dc, &client);
    else
        paint_classic(p, dc, &client);
}

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    PROGRESS *p = get_data(hwnd);
    if (!p && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        p = CcAlloc(sizeof(*p));
        if (!p)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)p);
        p->Hwnd = hwnd;
        p->Style = (DWORD)((CREATESTRUCTW *)lparam)->style;
        p->High = 100;
        p->Step = 10;
        p->State = PBST_NORMAL;
        p->Bar = CLR_DEFAULT;
        p->Bk = CLR_DEFAULT;
        p->MarqueeInterval = 30;
        open_theme(p);
        adjust_ex_style(p);
        return TRUE;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_MARQUEE);
        KillTimer(hwnd, TIMER_ANIMATE);
        if (p->Theme)
            CloseThemeData(p->Theme);
        p->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(p);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc) {
            if (wparam) {
                paint(p, dc);
            } else {
                CC_DRAW_BUFFER b;
                RECT client;
                GetClientRect(hwnd, &client);
                paint(p, CcBeginBuffer(&b, dc, &client));
                CcEndBuffer(&b);
            }
        }
        if (!wparam) {
            EndPaint(hwnd, &ps);
            update_marquee_timer(p);
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == TIMER_MARQUEE) {
            /* one frame per paint, and the paint arms the timer again. */
            KillTimer(hwnd, TIMER_MARQUEE);
            redraw(p);
        } else if (wparam == TIMER_ANIMATE) {
            animate_step(p);
        }
        return 0;
    case WM_SETFONT: {
        HFONT old = p->Font;
        p->Font = (HFONT)wparam;
        return (LRESULT)old;
    }
    case WM_GETFONT:
        return (LRESULT)p->Font;
    case WM_SYSCOLORCHANGE:
        redraw(p);
        return 0;
    case WM_THEMECHANGED:
    case WM_DPICHANGED_AFTERPARENT:
        open_theme(p);
        adjust_ex_style(p);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        return 0;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            DWORD old = p->Style;
            p->Style = ((STYLESTRUCT *)lparam)->styleNew;
            p->MarqueeOffset = 0;
            stop_animation(p);
            if ((old ^ p->Style) & PBS_MARQUEE)
                open_theme(p);
            update_marquee_timer(p);
            redraw(p);
        }
        return 0;
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x1000D;
        break;
    case PBM_SETRANGE:
        return set_range(p, LOWORD(lparam), HIWORD(lparam));
    case PBM_SETRANGE32:
        return set_range(p, (int)wparam, (int)lparam);
    case PBM_SETPOS:
        return update_position(p, (int)wparam, FALSE);
    case PBM_DELTAPOS:
        return update_position(p, p->Pos + (int)wparam, FALSE);
    case PBM_STEPIT:
        return update_position(p, p->Pos + p->Step, TRUE);
    case PBM_SETSTEP: {
        int old = p->Step;
        p->Step = (int)wparam;
        return old;
    }
    case PBM_GETSTEP:
        return p->Step;
    case PBM_GETRANGE:
        if (lparam) {
            ((PBRANGE *)lparam)->iLow = p->Low;
            ((PBRANGE *)lparam)->iHigh = p->High;
        }
        return wparam ? p->Low : p->High;
    case PBM_GETPOS:
        return p->Pos;
    case PBM_SETBARCOLOR:
    case PBM_SETBKCOLOR: {
        COLORREF *slot = msg == PBM_SETBARCOLOR ? &p->Bar : &p->Bk;
        COLORREF old = *slot;
        *slot = (COLORREF)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old;
    }
    case PBM_GETBKCOLOR:
        return p->Bk;
    case PBM_GETBARCOLOR:
        return p->Bar;
    case PBM_SETMARQUEE:
        if (wparam) {
            p->Marquee = TRUE;
            p->MarqueeOffset = 0;
            p->MarqueeInterval = lparam ? (UINT)lparam : 30;
        } else {
            p->Marquee = FALSE;
        }
        update_marquee_timer(p);
        return TRUE;
    case PBM_SETSTATE: {
        int state = (int)wparam;
        if (state < PBST_NORMAL || state > PBST_PAUSED)
            return 0;
        int old = p->State;
        if (state != old) {
            p->State = state;
            stop_animation(p);
            update_marquee_timer(p);
            redraw(p);
            NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, 0);
        }
        return old;
    }
    case PBM_GETSTATE:
        return p->State;
    case CCM_GETVERSION:
    case CCM_SETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
