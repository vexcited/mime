/*
 * scroll bars and window scrolling. win32k keeps no scroll bar state, so
 * the information of each bar lives in a window property owned by this
 * process and is freed with the window. the frame does not draw the
 * standard bars yet; showing and hiding them changes the WS_HSCROLL and
 * WS_VSCROLL styles, which win32k uses to size the client area.
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

static const WCHAR *const g_scroll_prop[3] = {L"MimeScrollH", L"MimeScrollV", L"MimeScrollCtl"};

/* a fresh bar has the range 0 to 100 like on windows. */
static const SCROLL_DATA g_scroll_default = {0, 100, 0, 0, 0, ESB_ENABLE_BOTH};

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

/* toggles the scroll styles and lets win32k recompute the client area. */
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

/* stores the fields of info, clamps the page and position, returns the position. */
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

/* reads the bar, the default bar of a window with the scroll style when never set. */
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

/* the SBM_ messages of the scroll bar control, which keeps its state as an SB_CTL bar. */
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
 * the window contents are not moved: the whole scrolled rectangle is
 * reported and invalidated, so the application repaints it at the new
 * offset. child windows move when asked to.
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
