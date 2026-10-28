/* drawing on a device context for a window: scrolling pixels, clipping out what is invalid, the caption and the desktop. */
#include "user32_private.h"

#ifndef DC_ACTIVE
#define DC_ACTIVE 0x0001
#define DC_SMALLCAP 0x0002
#define DC_ICON 0x0004
#define DC_TEXT 0x0008
#define DC_INBUTTON 0x0010
#define DC_GRADIENT 0x0020
#define DC_BUTTONS 0x1000
#endif

/*
 * the pixels of the scroll rectangle move by dx and dy inside it, within the clip rectangle. what is left of the scroll
 * rectangle that the move did not cover is the update region.
 */
WINUSERAPI BOOL WINAPI ScrollDC(HDC dc, int dx, int dy, const RECT *scroll, const RECT *clip, HRGN update,
                                LPRECT update_rect)
{
    if (!dc)
        return FALSE;
    RECT area, target;
    if (scroll)
        area = *scroll;
    else if (GetClipBox(dc, &area) == ERROR)
        return FALSE;
    SetRectEmpty(&target);
    if (!dx && !dy) {
        if (update)
            SetRectRgn(update, 0, 0, 0, 0);
        if (update_rect)
            SetRectEmpty(update_rect);
        return TRUE;
    }
    RECT limit = area;
    if (clip && !IntersectRect(&limit, &area, clip))
        SetRectEmpty(&limit);
    RECT moved = area;
    OffsetRect(&moved, dx, dy);
    if (IntersectRect(&target, &moved, &limit) && !IsRectEmpty(&target))
        BitBlt(dc, target.left, target.top, target.right - target.left, target.bottom - target.top, dc,
               target.left - dx, target.top - dy, SRCCOPY);
    else
        SetRectEmpty(&target);
    HRGN all = CreateRectRgnIndirect(&limit);
    HRGN done = CreateRectRgnIndirect(&target);
    HRGN rest = update ? update : CreateRectRgn(0, 0, 0, 0);
    if (!all || !done || !rest) {
        DeleteObject(all);
        DeleteObject(done);
        if (!update)
            DeleteObject(rest);
        return FALSE;
    }
    CombineRgn(rest, all, done, RGN_DIFF);
    if (update_rect && GetRgnBox(rest, update_rect) == ERROR)
        SetRectEmpty(update_rect);
    DeleteObject(all);
    DeleteObject(done);
    if (!update)
        DeleteObject(rest);
    return TRUE;
}

/* the update region of the window, in the coordinates of the device context, comes out of its clipping region. */
WINUSERAPI int WINAPI ExcludeUpdateRgn(HDC dc, HWND hwnd)
{
    if (!dc || !IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return ERROR;
    }
    HRGN invalid = CreateRectRgn(0, 0, 0, 0);
    if (!invalid)
        return ERROR;
    int result = GetUpdateRgn(hwnd, invalid, FALSE);
    if (result == ERROR) {
        DeleteObject(invalid);
        return ERROR;
    }
    POINT client = {0, 0}, origin = {0, 0};
    ClientToScreen(hwnd, &client);
    GetDCOrgEx(dc, &origin);
    OffsetRgn(invalid, client.x - origin.x, client.y - origin.y);
    result = ExtSelectClipRgn(dc, invalid, RGN_DIFF);
    DeleteObject(invalid);
    return result;
}

/* the classic caption: a bar in the caption colors with the small icon, the title and the system buttons. */
WINUSERAPI BOOL WINAPI DrawCaption(HWND hwnd, HDC dc, const RECT *rect, UINT flags)
{
    if (!dc || !rect)
        return FALSE;
    BOOL active = (flags & DC_ACTIVE) != 0;
    RECT r = *rect;
    FillRect(dc, &r, GetSysColorBrush(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
    int height = r.bottom - r.top;
    int left = r.left + 2;
    if ((flags & DC_ICON) && hwnd) {
        HICON icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
        if (!icon)
            icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
        int size = GetSystemMetrics(SM_CYSMICON);
        if (icon && size > 0) {
            DrawIconEx(dc, left, r.top + (height - size) / 2, icon, size, size, 0, nullptr, DI_NORMAL);
            left += size + 2;
        }
    }
    if ((flags & DC_BUTTONS) && height > 4) {
        int button = height - 4;
        RECT close = {r.right - button - 2, r.top + 2, r.right - 2, r.top + 2 + button};
        DrawFrameControl(dc, &close, DFC_CAPTION, DFCS_CAPTIONCLOSE);
        r.right = close.left - 2;
    }
    if ((flags & DC_TEXT) && hwnd) {
        WCHAR text[256];
        int length = GetWindowTextW(hwnd, text, ARRAYSIZE(text));
        if (length > 0) {
            NONCLIENTMETRICSW metrics = {sizeof(metrics)};
            HFONT font = nullptr, old = nullptr;
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
                font = CreateFontIndirectW((flags & DC_SMALLCAP) ? &metrics.lfSmCaptionFont : &metrics.lfCaptionFont);
            if (font)
                old = SelectObject(dc, font);
            COLORREF color = SetTextColor(dc, GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
            int mode = SetBkMode(dc, TRANSPARENT);
            RECT area = {left, r.top, r.right, r.bottom};
            DrawTextW(dc, text, length, &area, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SetBkMode(dc, mode);
            SetTextColor(dc, color);
            if (font) {
                SelectObject(dc, old);
                DeleteObject(font);
            }
        }
    }
    return TRUE;
}

/* the background of the desktop fills the clipping box of the device context. */
WINUSERAPI BOOL WINAPI PaintDesktop(HDC dc)
{
    RECT box;
    if (!dc || GetClipBox(dc, &box) == ERROR)
        return FALSE;
    return FillRect(dc, &box, GetSysColorBrush(COLOR_DESKTOP)) != 0;
}
