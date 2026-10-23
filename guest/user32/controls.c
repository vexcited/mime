/* the ScrollBar control, drawn and tracked like window scroll bars. */
#include "user32_private.h"

LRESULT CALLBACK ScrollBarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LRESULT result;
    if (UserScrollBarMessage(hwnd, msg, wparam, lparam, &result))
        return result;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc)
            UserDrawScrollBar(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        ClientToScreen(hwnd, &pt);
        UserTrackScrollBar(hwnd, pt);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_ENABLE) {
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
