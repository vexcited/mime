/*
 * scroll bars and window scrolling. win32k keeps no scroll bar state, so each
 * bar lives in a window property of this process, freed with the window.
 */
#include "user32_private.h"

typedef struct SCROLL_DATA {
    int Min;
    int Max;
    UINT Page;
    int Pos;
    int TrackPos;
    UINT Arrows; /* ESB_DISABLE_* flags. */
} SCROLL_DATA;

static const WCHAR *const g_scroll_prop[3] = {L"ScrollH", L"ScrollV", L"ScrollCtl"};

/* a fresh bar has the range 0 to 100, like on windows. */
static const SCROLL_DATA g_scroll_default = {0, 100, 0, 0, 0, ESB_ENABLE_BOTH};

static BOOL get_bar_info(HWND hwnd, int bar, SCROLLBARINFO *info);

static BOOL own_window(HWND hwnd)
{
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (pid != GetCurrentProcessId()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

static SCROLL_DATA *find_data(HWND hwnd, int bar)
{
    return GetPropW(hwnd, g_scroll_prop[bar]);
}

static SCROLL_DATA *create_data(HWND hwnd, int bar)
{
    SCROLL_DATA *d = find_data(hwnd, bar);
    if (d)
        return d;
    d = UserAlloc(sizeof(*d));
    if (!d)
        return nullptr;
    *d = g_scroll_default;
    if (!SetPropW(hwnd, g_scroll_prop[bar], d)) {
        UserFree(d);
        return nullptr;
    }
    return d;
}

void UserFreeScrollData(HWND hwnd)
{
    for (int bar = SB_HORZ; bar <= SB_CTL; bar++)
        if (GetPropW(hwnd, g_scroll_prop[bar]))
            UserFree(RemovePropW(hwnd, g_scroll_prop[bar]));
}

static BOOL valid_info_size(UINT size)
{
    return size == sizeof(SCROLLINFO) || size == offsetof(SCROLLINFO, nTrackPos);
}

/* win32k sizes the client area from WS_HSCROLL and WS_VSCROLL. */
static BOOL show_bars(HWND hwnd, int bar, BOOL show)
{
    DWORD bits = bar == SB_HORZ ? WS_HSCROLL : bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL | WS_VSCROLL;
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    DWORD next = show ? style | bits : style & ~bits;
    if (next == style)
        return TRUE;
    SetWindowLongW(hwnd, GWL_STYLE, (LONG)next);
    return SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                        SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void redraw_bar(HWND hwnd, int bar)
{
    if (bar == SB_CTL)
        InvalidateRect(hwnd, nullptr, TRUE);
    else
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

/* clamps the page and position, returns the position. */
static int set_info(HWND hwnd, int bar, const SCROLLINFO *info, BOOL redraw)
{
    SCROLL_DATA *d = create_data(hwnd, bar);
    if (!d)
        return 0;
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
    d->TrackPos = d->Pos;

    if (mask & (SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL)) {
        BOOL empty = d->Min >= last;
        if (empty && (mask & SIF_DISABLENOSCROLL)) {
            d->Arrows = ESB_DISABLE_BOTH;
            if (bar != SB_CTL)
                show_bars(hwnd, bar, TRUE);
        } else if (empty) {
            if (bar != SB_CTL)
                show_bars(hwnd, bar, FALSE);
        } else {
            if (d->Arrows == ESB_DISABLE_BOTH && (mask & SIF_DISABLENOSCROLL))
                d->Arrows = ESB_ENABLE_BOTH;
            if (bar != SB_CTL)
                show_bars(hwnd, bar, TRUE);
        }
    }
    int pos = d->Pos;
    if (redraw)
        redraw_bar(hwnd, bar);
    return pos;
}

/* a window with the scroll style but no stored bar reads the default bar. */
static BOOL get_info(HWND hwnd, int bar, SCROLLINFO *info)
{
    const SCROLL_DATA *d = find_data(hwnd, bar);
    if (!d) {
        DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        DWORD need = bar == SB_HORZ ? WS_HSCROLL : bar == SB_VERT ? WS_VSCROLL : 0;
        if (bar != SB_CTL && !(style & need)) {
            SetLastError(ERROR_NO_SCROLLBARS);
            return FALSE;
        }
        d = &g_scroll_default;
    }
    if (info->fMask & SIF_PAGE)
        info->nPage = d->Page;
    if (info->fMask & SIF_POS)
        info->nPos = d->Pos;
    if ((info->fMask & SIF_TRACKPOS) && info->cbSize == sizeof(SCROLLINFO))
        info->nTrackPos = d->TrackPos;
    if (info->fMask & SIF_RANGE) {
        info->nMin = d->Min;
        info->nMax = d->Max;
    }
    return (info->fMask & SIF_ALL) != 0;
}

static BOOL enable_arrows(HWND hwnd, int bar, UINT arrows)
{
    SCROLL_DATA *d = create_data(hwnd, bar);
    if (!d || d->Arrows == arrows)
        return FALSE;
    d->Arrows = arrows;
    redraw_bar(hwnd, bar);
    return TRUE;
}

/* the scroll bar control keeps its state as an SB_CTL bar. */
BOOL UserScrollBarMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    SCROLLINFO si = {sizeof(si)};
    switch (msg) {
    case SBM_SETPOS: {
        si.fMask = SIF_POS;
        get_info(hwnd, SB_CTL, &si);
        int old = si.nPos;
        si.fMask = SIF_POS;
        si.nPos = (int)wparam;
        set_info(hwnd, SB_CTL, &si, (BOOL)lparam);
        *result = old;
        return TRUE;
    }
    case SBM_GETPOS:
        si.fMask = SIF_POS;
        get_info(hwnd, SB_CTL, &si);
        *result = si.nPos;
        return TRUE;
    case SBM_SETRANGE:
    case SBM_SETRANGEREDRAW: {
        si.fMask = SIF_POS;
        get_info(hwnd, SB_CTL, &si);
        int old = si.nPos;
        si.fMask = SIF_RANGE;
        si.nMin = (int)wparam;
        si.nMax = (int)lparam;
        set_info(hwnd, SB_CTL, &si, msg == SBM_SETRANGEREDRAW);
        *result = old;
        return TRUE;
    }
    case SBM_GETRANGE:
        si.fMask = SIF_RANGE;
        get_info(hwnd, SB_CTL, &si);
        if (wparam)
            *(LPINT)wparam = si.nMin;
        if (lparam)
            *(LPINT)lparam = si.nMax;
        *result = 0;
        return TRUE;
    case SBM_ENABLE_ARROWS:
        *result = enable_arrows(hwnd, SB_CTL, (UINT)wparam);
        return TRUE;
    case SBM_SETSCROLLINFO: {
        const SCROLLINFO *in = (const SCROLLINFO *)lparam;
        *result = in && valid_info_size(in->cbSize) ? set_info(hwnd, SB_CTL, in, (BOOL)wparam) : 0;
        return TRUE;
    }
    case SBM_GETSCROLLINFO: {
        SCROLLINFO *out = (SCROLLINFO *)lparam;
        *result = out && valid_info_size(out->cbSize) && get_info(hwnd, SB_CTL, out);
        return TRUE;
    }
    case SBM_GETSCROLLBARINFO:
        *result = lparam && ((SCROLLBARINFO *)lparam)->cbSize == sizeof(SCROLLBARINFO) &&
                  get_bar_info(hwnd, SB_CTL, (SCROLLBARINFO *)lparam);
        return TRUE;
    case WM_NCDESTROY:
        UserFreeScrollData(hwnd);
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ public api */

static BOOL check_bar(HWND hwnd, int bar)
{
    if (bar < SB_HORZ || bar > SB_CTL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return bar == SB_CTL ? IsWindow(hwnd) : own_window(hwnd);
}

WINUSERAPI int WINAPI SetScrollInfo(HWND hwnd, int bar, LPCSCROLLINFO info, BOOL redraw)
{
    if (!info || !valid_info_size(info->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!check_bar(hwnd, bar))
        return 0;
    if (bar == SB_CTL)
        return (int)SendMessageW(hwnd, SBM_SETSCROLLINFO, (WPARAM)redraw, (LPARAM)info);
    return set_info(hwnd, bar, info, redraw);
}

WINUSERAPI BOOL WINAPI GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO info)
{
    if (!info || !valid_info_size(info->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!check_bar(hwnd, bar))
        return FALSE;
    if (bar == SB_CTL)
        return (BOOL)SendMessageW(hwnd, SBM_GETSCROLLINFO, 0, (LPARAM)info);
    return get_info(hwnd, bar, info);
}

WINUSERAPI int WINAPI GetScrollPos(HWND hwnd, int bar)
{
    SCROLLINFO si = {sizeof(si), SIF_POS};
    return GetScrollInfo(hwnd, bar, &si) ? si.nPos : 0;
}

WINUSERAPI int WINAPI SetScrollPos(HWND hwnd, int bar, int pos, BOOL redraw)
{
    SCROLLINFO si = {sizeof(si), SIF_POS};
    if (!GetScrollInfo(hwnd, bar, &si))
        return 0;
    int old = si.nPos;
    si.fMask = SIF_POS;
    si.nPos = pos;
    SetScrollInfo(hwnd, bar, &si, redraw);
    return old;
}

WINUSERAPI BOOL WINAPI GetScrollRange(HWND hwnd, int bar, LPINT min, LPINT max)
{
    SCROLLINFO si = {sizeof(si), SIF_RANGE};
    BOOL ok = GetScrollInfo(hwnd, bar, &si);
    if (!ok)
        si.nMin = si.nMax = 0;
    if (min)
        *min = si.nMin;
    if (max)
        *max = si.nMax;
    return ok;
}

WINUSERAPI BOOL WINAPI SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL redraw)
{
    if ((LONG64)max - min > 0x7FFFFFFF) {
        SetLastError(ERROR_INVALID_SCROLLBAR_RANGE);
        return FALSE;
    }
    SCROLLINFO si = {sizeof(si), SIF_RANGE, min, max};
    if (!check_bar(hwnd, bar))
        return FALSE;
    SetScrollInfo(hwnd, bar, &si, redraw);
    return TRUE;
}

WINUSERAPI BOOL WINAPI ShowScrollBar(HWND hwnd, int bar, BOOL show)
{
    if (bar == SB_CTL) {
        ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
        return IsWindow(hwnd);
    }
    if (bar != SB_HORZ && bar != SB_VERT && bar != SB_BOTH) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return show_bars(hwnd, bar, show);
}

WINUSERAPI BOOL WINAPI EnableScrollBar(HWND hwnd, UINT bar, UINT arrows)
{
    arrows &= ESB_DISABLE_BOTH;
    if (bar == SB_CTL)
        return (BOOL)SendMessageW(hwnd, SBM_ENABLE_ARROWS, arrows, 0);
    if (bar != SB_HORZ && bar != SB_VERT && bar != SB_BOTH) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!own_window(hwnd))
        return FALSE;
    BOOL changed = FALSE;
    if (bar != SB_VERT)
        changed |= enable_arrows(hwnd, SB_HORZ, arrows);
    if (bar != SB_HORZ)
        changed |= enable_arrows(hwnd, SB_VERT, arrows);
    return changed;
}

/* ------------------------------------------------------------------ window scrolling */

/*
 * the contents are not moved: the whole scrolled rectangle is invalidated, so
 * the application repaints it at the new offset. children move when asked to.
 */
WINUSERAPI int WINAPI ScrollWindowEx(HWND hwnd, int dx, int dy, const RECT *rect, const RECT *clip, HRGN update,
                                     LPRECT update_rect, UINT flags)
{
    RECT area;
    if (!GetClientRect(hwnd, &area)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return ERROR;
    }
    if (rect)
        area = *rect;
    RECT dirty = area;
    if (clip && !IntersectRect(&dirty, &area, clip))
        SetRectEmpty(&dirty);
    if (!dx && !dy)
        SetRectEmpty(&dirty);

    if ((flags & SW_SCROLLCHILDREN) && (dx || dy)) {
        for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            RECT r, overlap;
            GetWindowRect(c, &r);
            MapWindowPoints(nullptr, hwnd, (POINT *)&r, 2);
            if (rect && !IntersectRect(&overlap, &r, rect))
                continue;
            SetWindowPos(c, nullptr, r.left + dx, r.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    if (update_rect)
        *update_rect = dirty;
    if (update)
        SetRectRgn(update, dirty.left, dirty.top, dirty.right, dirty.bottom);
    if ((flags & SW_INVALIDATE) && !IsRectEmpty(&dirty)) {
        UINT rdw = RDW_INVALIDATE | ((flags & SW_ERASE) ? RDW_ERASE : 0) |
                   ((flags & SW_SCROLLCHILDREN) ? RDW_ALLCHILDREN : 0);
        RedrawWindow(hwnd, &dirty, nullptr, rdw);
    }
    return IsRectEmpty(&dirty) ? NULLREGION : SIMPLEREGION;
}

WINUSERAPI BOOL WINAPI ScrollWindow(HWND hwnd, int dx, int dy, const RECT *rect, const RECT *clip)
{
    UINT flags = SW_INVALIDATE | SW_ERASE | (rect ? 0 : SW_SCROLLCHILDREN);
    return ScrollWindowEx(hwnd, dx, dy, rect, clip, nullptr, nullptr, flags) != ERROR;
}

/* ------------------------------------------------------------------ drawing and tracking */

typedef struct BAR {
    BOOL vert, enabled;
    RECT arrow1, arrow2, track, thumb;
    BOOL has_thumb;
    int room, size, offset, thick;
    const SCROLL_DATA *data;
} BAR;

static const SCROLL_DATA *data_or_default(HWND hwnd, int bar)
{
    const SCROLL_DATA *d = find_data(hwnd, bar);
    return d ? d : &g_scroll_default;
}

static void layout(HWND hwnd, int bar, const RECT *r, BAR *b)
{
    memset(b, 0, sizeof(*b));
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    b->vert = bar == SB_VERT || (bar == SB_CTL && (style & SBS_VERT));
    b->data = data_or_default(hwnd, bar);
    b->enabled = IsWindowEnabled(hwnd) && b->data->Arrows != ESB_DISABLE_BOTH;
    int len = b->vert ? r->bottom - r->top : r->right - r->left;
    b->thick = b->vert ? r->right - r->left : r->bottom - r->top;
    int arrow = b->thick * 2 <= len ? b->thick : len / 2;
    b->arrow1 = b->arrow2 = b->track = *r;
    if (b->vert) {
        b->arrow1.bottom = r->top + arrow;
        b->arrow2.top = r->bottom - arrow;
        b->track.top = b->arrow1.bottom;
        b->track.bottom = b->arrow2.top;
    } else {
        b->arrow1.right = r->left + arrow;
        b->arrow2.left = r->right - arrow;
        b->track.left = b->arrow1.right;
        b->track.right = b->arrow2.left;
    }
    const SCROLL_DATA *d = b->data;
    int span = d->Max - d->Min + 1;
    b->room = b->vert ? b->track.bottom - b->track.top : b->track.right - b->track.left;
    if (!b->enabled || span <= 0 || b->room <= 0 || (d->Page && (int)d->Page >= span))
        return;
    int size = d->Page ? UserMulDiv(b->room, (int)d->Page, span) : b->thick;
    int min = GetSystemMetrics(b->vert ? SM_CYVTHUMB : SM_CXHTHUMB) / 2;
    if (size < min)
        size = min;
    if (size > b->room)
        size = b->room;
    int steps = span - (d->Page ? (int)d->Page : 1);
    int pos = d == find_data(hwnd, bar) && GetCapture() == hwnd ? d->TrackPos : d->Pos;
    b->offset = steps > 0 ? UserMulDiv(b->room - size, pos - d->Min, steps) : 0;
    b->size = size;
    b->has_thumb = TRUE;
    b->thumb = b->track;
    if (b->vert) {
        b->thumb.top += b->offset;
        b->thumb.bottom = b->thumb.top + size;
    } else {
        b->thumb.left += b->offset;
        b->thumb.right = b->thumb.left + size;
    }
}

enum { PART_NONE, PART_ARROW1, PART_PAGE1, PART_THUMB, PART_PAGE2, PART_ARROW2 };

/* the part the mouse holds down while a bar is tracked, and the one it is over. */
static struct {
    HWND Window;
    int Bar;
    int Part;
} g_held, g_hover;

/* the parts of the ScrollBar class. */
enum {
    SBP_ARROWBTN = 1,
    SBP_THUMBBTNHORZ = 2,
    SBP_THUMBBTNVERT = 3,
    SBP_LOWERTRACKHORZ = 4,
    SBP_UPPERTRACKHORZ = 5,
    SBP_LOWERTRACKVERT = 6,
    SBP_UPPERTRACKVERT = 7,
    SBP_SIZEBOXBKGND = 11,
};

/* a bar drawn with the visual style: arrows, the track in two pieces and the thumb. */
static void draw_themed(HWND hwnd, int bar, HDC dc, const BAR *b)
{
    UINT dpi = GetDpiForWindow(hwnd);
    int held = g_held.Window == hwnd && g_held.Bar == bar ? g_held.Part : PART_NONE;
    int over = g_hover.Window == hwnd && g_hover.Bar == bar && held == PART_NONE ? g_hover.Part : PART_NONE;
    UINT arrows = b->data->Arrows;
    /* up, down, left and right buttons have four states each: normal, hot, pressed and disabled. */
    int base1 = b->vert ? 0 : 8, base2 = b->vert ? 4 : 12;
    BOOL off1 = !b->enabled || (arrows & ESB_DISABLE_LTUP), off2 = !b->enabled || (arrows & ESB_DISABLE_RTDN);
    int s1 = base1 + (off1 ? 4 : held == PART_ARROW1 ? 3 : over == PART_ARROW1 ? 2 : 1);
    int s2 = base2 + (off2 ? 4 : held == PART_ARROW2 ? 3 : over == PART_ARROW2 ? 2 : 1);
    UserThemeDraw(L"ScrollBar", dc, SBP_ARROWBTN, s1, &b->arrow1, dpi);
    UserThemeDraw(L"ScrollBar", dc, SBP_ARROWBTN, s2, &b->arrow2, dpi);
    int lower = b->vert ? SBP_LOWERTRACKVERT : SBP_LOWERTRACKHORZ, upper = b->vert ? SBP_UPPERTRACKVERT : SBP_UPPERTRACKHORZ;
    if (!b->has_thumb) {
        UserThemeDraw(L"ScrollBar", dc, lower, b->enabled ? 1 : 4, &b->track, dpi);
        return;
    }
    RECT before = b->track, after = b->track;
    if (b->vert) {
        before.bottom = b->thumb.top;
        after.top = b->thumb.bottom;
    } else {
        before.right = b->thumb.left;
        after.left = b->thumb.right;
    }
    if (!IsRectEmpty(&before))
        UserThemeDraw(L"ScrollBar", dc, lower, held == PART_PAGE1 ? 3 : 1, &before, dpi);
    if (!IsRectEmpty(&after))
        UserThemeDraw(L"ScrollBar", dc, upper, held == PART_PAGE2 ? 3 : 1, &after, dpi);
    UserThemeDraw(L"ScrollBar", dc, b->vert ? SBP_THUMBBTNVERT : SBP_THUMBBTNHORZ,
                  held == PART_THUMB ? 3 : over == PART_THUMB ? 2 : 1, &b->thumb, dpi);
}

static void draw(HWND hwnd, int bar, HDC dc, const RECT *r)
{
    BAR b;
    layout(hwnd, bar, r, &b);
    if (UserThemeActive()) {
        draw_themed(hwnd, bar, dc, &b);
        return;
    }
    UINT arrows = b.data->Arrows;
    UINT first = !b.enabled || (arrows & ESB_DISABLE_LTUP) ? DFCS_INACTIVE : 0;
    UINT second = !b.enabled || (arrows & ESB_DISABLE_RTDN) ? DFCS_INACTIVE : 0;
    DrawFrameControl(dc, &b.arrow1, DFC_SCROLL, (b.vert ? DFCS_SCROLLUP : DFCS_SCROLLLEFT) | first);
    DrawFrameControl(dc, &b.arrow2, DFC_SCROLL, (b.vert ? DFCS_SCROLLDOWN : DFCS_SCROLLRIGHT) | second);
    FillRect(dc, &b.track, GetSysColorBrush(COLOR_SCROLLBAR));
    if (b.has_thumb)
        DrawEdge(dc, &b.thumb, EDGE_RAISED, BF_RECT | BF_MIDDLE);
}

void UserDrawScrollBar(HWND hwnd, HDC dc)
{
    RECT r;
    GetClientRect(hwnd, &r);
    draw(hwnd, SB_CTL, dc, &r);
}

/* in screen coordinates, beside or under the client area. */
static BOOL standard_rect(HWND hwnd, int bar, RECT *r)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    if (!(style & (bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL)))
        return FALSE;
    RECT c;
    GetClientRect(hwnd, &c);
    MapWindowPoints(hwnd, nullptr, (POINT *)&c, 2);
    UINT dpi = GetDpiForWindow(hwnd);
    if (bar == SB_VERT)
        *r = (RECT){c.right, c.top, c.right + GetSystemMetricsForDpi(SM_CXVSCROLL, dpi), c.bottom};
    else
        *r = (RECT){c.left, c.bottom, c.right, c.bottom + GetSystemMetricsForDpi(SM_CYHSCROLL, dpi)};
    return TRUE;
}

/*
 * where the bar is on the screen and how its parts are placed along it, from the start of the bar, with the states of
 * the accessibility interface: the arrows, the page areas and the thumb are unavailable when they are disabled, the
 * page area the thumb touches is invisible and so is every part of a bar the window does not have.
 */
static BOOL get_bar_info(HWND hwnd, int bar, SCROLLBARINFO *info)
{
    RECT screen;
    if (bar == SB_CTL) {
        GetClientRect(hwnd, &screen);
        MapWindowPoints(hwnd, nullptr, (POINT *)&screen, 2);
    } else if (!standard_rect(hwnd, bar, &screen)) {
        memset(&info->rcScrollBar, 0, sizeof(*info) - sizeof(info->cbSize));
        info->rgstate[0] = STATE_SYSTEM_INVISIBLE;
        return TRUE;
    }
    RECT local = {0, 0, screen.right - screen.left, screen.bottom - screen.top};
    BAR b;
    layout(hwnd, bar, &local, &b);
    memset(&info->rcScrollBar, 0, sizeof(*info) - sizeof(info->cbSize));
    info->rcScrollBar = screen;
    info->dxyLineButton = b.vert ? b.arrow1.bottom - b.arrow1.top : b.arrow1.right - b.arrow1.left;
    RECT thumb = b.has_thumb ? b.thumb : (RECT){b.track.left, b.track.top, b.track.left, b.track.top};
    info->xyThumbTop = b.vert ? thumb.top : thumb.left;
    info->xyThumbBottom = b.vert ? thumb.bottom : thumb.right;
    UINT arrows = b.enabled ? b.data->Arrows : ESB_DISABLE_BOTH;
    if (arrows & ESB_DISABLE_LTUP)
        info->rgstate[1] = info->rgstate[2] = STATE_SYSTEM_UNAVAILABLE;
    if (arrows & ESB_DISABLE_RTDN)
        info->rgstate[5] = info->rgstate[4] = STATE_SYSTEM_UNAVAILABLE;
    if ((arrows & ESB_DISABLE_BOTH) == ESB_DISABLE_BOTH)
        info->rgstate[0] = STATE_SYSTEM_UNAVAILABLE;
    /* the parts are numbered as the states are. */
    if (g_held.Window == hwnd && g_held.Bar == bar && g_held.Part >= PART_ARROW1 && g_held.Part <= PART_ARROW2)
        info->rgstate[g_held.Part] |= STATE_SYSTEM_PRESSED;
    int start = b.vert ? b.track.top : b.track.left, end = b.vert ? b.track.bottom : b.track.right;
    if (info->xyThumbTop == start)
        info->rgstate[2] |= STATE_SYSTEM_INVISIBLE;
    if (info->xyThumbBottom == end)
        info->rgstate[4] |= STATE_SYSTEM_INVISIBLE;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetScrollBarInfo(HWND hwnd, LONG object, PSCROLLBARINFO info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    switch (object) {
    case OBJID_HSCROLL:
        return get_bar_info(hwnd, SB_HORZ, info);
    case OBJID_VSCROLL:
        return get_bar_info(hwnd, SB_VERT, info);
    case OBJID_CLIENT:
        return (BOOL)SendMessageW(hwnd, SBM_GETSCROLLBARINFO, 0, (LPARAM)info);
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* drawn after the rest of the frame. */
static void draw_standard(HWND hwnd)
{
    RECT w, v, h;
    BOOL has_v = standard_rect(hwnd, SB_VERT, &v), has_h = standard_rect(hwnd, SB_HORZ, &h);
    if (!has_v && !has_h)
        return;
    GetWindowRect(hwnd, &w);
    HDC dc = GetWindowDC(hwnd);
    if (!dc)
        return;
    if (has_v) {
        OffsetRect(&v, -w.left, -w.top);
        draw(hwnd, SB_VERT, dc, &v);
    }
    if (has_h) {
        OffsetRect(&h, -w.left, -w.top);
        draw(hwnd, SB_HORZ, dc, &h);
    }
    if (has_v && has_h) {
        RECT box = {v.left, h.top, v.right, h.bottom};
        if (!UserThemeDraw(L"ScrollBar", dc, SBP_SIZEBOXBKGND, 1, &box, GetDpiForWindow(hwnd)))
            FillRect(dc, &box, GetSysColorBrush(COLOR_3DFACE));
    }
    ReleaseDC(hwnd, dc);
}

static int part_at(const BAR *b, POINT pt)
{
    if (PtInRect(&b->arrow1, pt))
        return PART_ARROW1;
    if (PtInRect(&b->arrow2, pt))
        return PART_ARROW2;
    if (!PtInRect(&b->track, pt) || !b->has_thumb)
        return PART_NONE;
    if (PtInRect(&b->thumb, pt))
        return PART_THUMB;
    int at = b->vert ? pt.y : pt.x, start = b->vert ? b->thumb.top : b->thumb.left;
    return at < start ? PART_PAGE1 : PART_PAGE2;
}

static int thumb_position(const BAR *b, POINT pt, int grab)
{
    const SCROLL_DATA *d = b->data;
    int steps = d->Max - d->Min + 1 - (d->Page ? (int)d->Page : 1);
    int free = b->room - b->size;
    int at = (b->vert ? pt.y - b->track.top : pt.x - b->track.left) - grab;
    if (at < 0)
        at = 0;
    if (at > free)
        at = free;
    return free > 0 ? d->Min + UserMulDiv(at, steps, free) : d->Min;
}

/* runs until the button is released: arrows and the track repeat, the thumb is dragged. */
static void track(HWND hwnd, int bar, POINT pt)
{
    RECT r;
    if (bar == SB_CTL) {
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, nullptr, (POINT *)&r, 2);
    } else if (!standard_rect(hwnd, bar, &r)) {
        return;
    }
    BAR b;
    layout(hwnd, bar, &r, &b);
    int part = part_at(&b, pt);
    if (!b.enabled || part == PART_NONE)
        return;
    if ((part == PART_ARROW1 && (b.data->Arrows & ESB_DISABLE_LTUP)) ||
        (part == PART_ARROW2 && (b.data->Arrows & ESB_DISABLE_RTDN)))
        return;
    UINT msg = b.vert ? WM_VSCROLL : WM_HSCROLL;
    HWND target = bar == SB_CTL ? GetParent(hwnd) : hwnd;
    LPARAM from = bar == SB_CTL ? (LPARAM)hwnd : 0;
    static const WORD CODES[] = {0, SB_LINEUP, SB_PAGEUP, SB_THUMBTRACK, SB_PAGEDOWN, SB_LINEDOWN};
    int grab = part == PART_THUMB ? (b.vert ? pt.y - b.thumb.top : pt.x - b.thumb.left) : 0;
    SCROLL_DATA *d = create_data(hwnd, bar);
    if (!d)
        return;
    SetCapture(hwnd);
    g_held.Window = hwnd;
    g_held.Bar = bar;
    g_held.Part = part;
    if (bar == SB_CTL)
        InvalidateRect(hwnd, nullptr, FALSE);
    else
        redraw_bar(hwnd, bar);
    if (part != PART_THUMB)
        SendMessageW(target, msg, CODES[part], from);
    DWORD next = GetTickCount() + 400;
    int last_pos = d->Pos;
    POINT cursor = pt;
    while (GetCapture() == hwnd) {
        MSG m;
        BOOL up = FALSE;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_LBUTTONUP || m.message == WM_NCLBUTTONUP) {
                up = TRUE;
                break;
            }
            if (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST)
                continue;
            if (m.message >= WM_NCMOUSEMOVE && m.message <= WM_NCMBUTTONDBLCLK)
                continue;
            if (m.message == WM_QUIT) {
                PostQuitMessage((int)m.wParam);
                up = TRUE;
                break;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (up || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            break;
        GetCursorPos(&cursor);
        if (part == PART_THUMB) {
            layout(hwnd, bar, &r, &b);
            int pos = thumb_position(&b, cursor, grab);
            if (pos != last_pos) {
                last_pos = pos;
                d->TrackPos = pos;
                SendMessageW(target, msg, MAKEWPARAM(SB_THUMBTRACK, (WORD)pos), from);
                if (bar == SB_CTL)
                    InvalidateRect(hwnd, nullptr, FALSE);
                else
                    redraw_bar(hwnd, bar);
            }
        } else if (GetTickCount() >= next) {
            layout(hwnd, bar, &r, &b);
            if (part_at(&b, cursor) == part)
                SendMessageW(target, msg, CODES[part], from);
            next = GetTickCount() + 50;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    if (GetCapture() == hwnd)
        ReleaseCapture();
    g_held.Window = nullptr;
    if (part == PART_THUMB)
        SendMessageW(target, msg, MAKEWPARAM(SB_THUMBPOSITION, (WORD)last_pos), from);
    SendMessageW(target, msg, SB_ENDSCROLL, from);
    if (bar == SB_CTL)
        InvalidateRect(hwnd, nullptr, FALSE);
    else
        redraw_bar(hwnd, bar);
}

void UserTrackScrollBar(HWND hwnd, POINT screen)
{
    track(hwnd, SB_CTL, screen);
}

static void hover_redraw(HWND hwnd, int bar)
{
    if (bar == SB_CTL)
        InvalidateRect(hwnd, nullptr, FALSE);
    else
        redraw_bar(hwnd, bar);
}

static void hover_clear(void)
{
    HWND old = g_hover.Window;
    int bar = g_hover.Bar;
    g_hover.Window = nullptr;
    if (old && IsWindow(old))
        hover_redraw(old, bar);
}

/* the mouse is over a bar of the window, or elsewhere when bar is -1. */
static void hover_move(HWND hwnd, int bar, POINT screen, BOOL client_area)
{
    if (bar < 0 || !UserThemeActive()) {
        if (g_hover.Window == hwnd)
            hover_clear();
        return;
    }
    RECT r;
    if (bar == SB_CTL) {
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, nullptr, (POINT *)&r, 2);
    } else if (!standard_rect(hwnd, bar, &r)) {
        return;
    }
    BAR b;
    layout(hwnd, bar, &r, &b);
    int part = part_at(&b, screen);
    if (g_hover.Window == hwnd && g_hover.Bar == bar && g_hover.Part == part)
        return;
    if (g_hover.Window && g_hover.Window != hwnd)
        hover_clear();
    g_hover.Window = hwnd;
    g_hover.Bar = bar;
    g_hover.Part = part;
    TRACKMOUSEEVENT track_leave = {sizeof(track_leave), TME_LEAVE | (client_area ? 0 : TME_NONCLIENT), hwnd, 0};
    TrackMouseEvent(&track_leave);
    hover_redraw(hwnd, bar);
}

/* the scroll bar control: the mouse moved over it, or left it. */
void UserScrollBarHover(HWND hwnd, BOOL leave, POINT client)
{
    if (leave) {
        if (g_hover.Window == hwnd)
            hover_clear();
        return;
    }
    ClientToScreen(hwnd, &client);
    hover_move(hwnd, SB_CTL, client, TRUE);
}

/* the DefWindowProc part for the standard bars. */
BOOL UserDefScrollBars(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    switch (msg) {
    case WM_NCPAINT:
        *result = NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_DEFWINDOWPROC, FALSE);
        draw_standard(hwnd);
        return TRUE;
    case WM_NCMOUSEMOVE:
        hover_move(hwnd, wparam == HTVSCROLL ? SB_VERT : wparam == HTHSCROLL ? SB_HORZ : -1,
                   (POINT){(short)LOWORD(lparam), (short)HIWORD(lparam)}, FALSE);
        return FALSE;
    case WM_NCMOUSELEAVE:
        if (g_hover.Window == hwnd)
            hover_clear();
        return FALSE;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (wparam != HTVSCROLL && wparam != HTHSCROLL)
            return FALSE;
        track(hwnd, wparam == HTVSCROLL ? SB_VERT : SB_HORZ, (POINT){(short)LOWORD(lparam), (short)HIWORD(lparam)});
        *result = 0;
        return TRUE;
    }
    return FALSE;
}
