/* arranging windows: the minimized ones in rows, the others in a cascade or a tile. */
#include "user32_private.h"

#ifndef MDITILE_VERTICAL
#define MDITILE_VERTICAL 0x0000
#define MDITILE_HORIZONTAL 0x0001
#define MDITILE_SKIPDISABLED 0x0002
#define MDITILE_ZORDER 0x0004
#endif

#define ARRANGE_MAX 512

/* the area to arrange in: the given rectangle, the work area of the desktop or the client area. */
static BOOL arrange_area(HWND parent, const RECT *given, RECT *area)
{
    if (given) {
        *area = *given;
        return TRUE;
    }
    if (parent == GetDesktopWindow())
        return SystemParametersInfoW(SPI_GETWORKAREA, 0, area, 0);
    return GetClientRect(parent, area);
}

static void restore_maximized(HWND parent)
{
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        DWORD style = (DWORD)GetWindowLongW(child, GWL_STYLE);
        if ((style & (WS_VISIBLE | WS_MAXIMIZE)) == (WS_VISIBLE | WS_MAXIMIZE))
            ShowWindow(child, SW_RESTORE);
    }
}

/* visible windows that are not minimized, topmost or tool windows, in the order of the z axis. */
static int collect(HWND parent, UINT how, UINT count, const HWND *kids, HWND *out)
{
    int found = 0;
    HWND child = kids ? nullptr : GetWindow(parent, GW_CHILD);
    for (UINT i = 0; found < ARRANGE_MAX; i++) {
        HWND hwnd;
        if (kids) {
            if (i >= count)
                break;
            hwnd = kids[i];
        } else {
            hwnd = child;
            if (!hwnd)
                break;
            child = GetWindow(child, GW_HWNDNEXT);
        }
        if (!IsWindow(hwnd))
            continue;
        DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE), ex = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (!(style & WS_VISIBLE) || (style & WS_MINIMIZE) || (ex & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)))
            continue;
        if ((how & MDITILE_SKIPDISABLED) && (style & WS_DISABLED))
            continue;
        out[found++] = hwnd;
    }
    return found;
}

/* moves the window, which is only resized when it has a sizing border. */
static HDWP place(HDWP positions, HWND hwnd, UINT how, int x, int y, int cx, int cy)
{
    UINT flags = SWP_NOACTIVATE | SWP_NOCOPYBITS;
    if (!((DWORD)GetWindowLongW(hwnd, GWL_STYLE) & WS_THICKFRAME))
        flags |= SWP_NOSIZE;
    if (!(how & MDITILE_ZORDER))
        flags |= SWP_NOZORDER;
    return DeferWindowPos(positions, hwnd, nullptr, x, y, cx, cy, flags);
}

WINUSERAPI UINT WINAPI ArrangeIconicWindows(HWND parent)
{
    if (!parent)
        parent = GetDesktopWindow();
    RECT area;
    if (!IsWindow(parent) || !arrange_area(parent, nullptr, &area))
        return 0;
    int cx = GetSystemMetrics(SM_CXMINSPACING), cy = GetSystemMetrics(SM_CYMINSPACING);
    if (cx <= 0 || cy <= 0)
        return 0;
    int columns = (area.right - area.left) / cx;
    if (columns < 1)
        columns = 1;
    int placed = 0;
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        DWORD style = (DWORD)GetWindowLongW(child, GWL_STYLE);
        if ((style & (WS_VISIBLE | WS_MINIMIZE)) != (WS_VISIBLE | WS_MINIMIZE) || GetWindow(child, GW_OWNER))
            continue;
        int column = placed % columns, row = placed / columns;
        SetWindowPos(child, nullptr, area.left + column * cx, area.bottom - (row + 1) * cy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        placed++;
    }
    return placed ? (UINT)cy : 0;
}

/* each window of the list, the lowest first, a caption and a border further down and right of the one before. */
WINUSERAPI WORD WINAPI CascadeWindows(HWND parent, UINT how, const RECT *rect, UINT count, const HWND *kids)
{
    if (!parent)
        parent = GetDesktopWindow();
    RECT area;
    HWND list[ARRANGE_MAX];
    if (!IsWindow(parent) || !arrange_area(parent, rect, &area))
        return 0;
    restore_maximized(parent);
    int n = collect(parent, how, count, kids, list);
    if (!n)
        return 0;
    HDWP positions = BeginDeferWindowPos(n);
    if (!positions)
        return 0;
    int stepx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CYCAPTION);
    int stepy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CYCAPTION);
    int width = area.right - area.left, height = area.bottom - area.top;
    int shrink = stepy > 0 ? height / (3 * stepy) : 0;
    int cycle = shrink >= 0 ? shrink + 1 : 1;
    int done = 0;
    for (int i = n - 1; i >= 0 && positions; i--, done++)
        positions = place(positions, list[i], how, area.left + stepx * (done % cycle), area.top + stepy * (done % cycle),
                          width - shrink * stepx, height - shrink * stepy);
    if (positions)
        EndDeferWindowPos(positions);
    return (WORD)done;
}

/* a grid with as many columns and rows as the square root of the count asks for. */
WINUSERAPI WORD WINAPI TileWindows(HWND parent, UINT how, const RECT *rect, UINT count, const HWND *kids)
{
    if (!parent)
        parent = GetDesktopWindow();
    RECT area;
    HWND list[ARRANGE_MAX];
    if (!IsWindow(parent) || !arrange_area(parent, rect, &area))
        return 0;
    restore_maximized(parent);
    int n = collect(parent, how, count, kids, list);
    int width = area.right - area.left, height = area.bottom - area.top;
    if (!n || width <= 0 || height <= 0)
        return 0;
    int root = 2;
    if (n >= 4)
        while (root * root <= n)
            root++;
    int k = root - 1, columns, base;
    if (how & MDITILE_HORIZONTAL) {
        columns = k;
        base = n / k;
    } else {
        columns = n / k;
        base = k;
    }
    int extra = n % k;
    HDWP positions = BeginDeferWindowPos(n);
    if (!positions)
        return 0;
    int next = 0, done = 0;
    for (int c = 0; c < columns && positions; c++) {
        int rows = columns - c <= extra ? base + 1 : base;
        for (int r = 0; r < rows && next < n && positions; r++, next++, done++)
            positions = place(positions, list[next], how, area.left + c * (width / columns),
                              area.top + r * (height / rows), width / columns, height / rows);
    }
    if (positions)
        EndDeferWindowPos(positions);
    return (WORD)done;
}
