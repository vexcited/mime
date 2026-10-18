/*
 * what programs ask of the desktop around a window: DPI awareness contexts, pointer input as a mouse gives it,
 * the input source of a message, drag detection and a few window helpers.
 */
#include "user32_private.h"
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>

/* ------------------------------------------------------------------ DPI awareness contexts */

/*
 * the context as windows keeps it: the pseudo handles -1 to -5 are the unaware, system aware, per monitor,
 * per monitor v2 and gdi scaled unaware contexts, and what the get functions return are the numbers behind them.
 * the low nibble is the awareness, and the system aware one carries the system dpi.
 */
static ULONG context_value(DPI_AWARENESS_CONTEXT context)
{
    LONG_PTR value = (LONG_PTR)context;
    switch (value) {
    case -1:
        return 0x6010;
    case -2:
        return ((GetDpiForSystem() & 0x1FF) << 8) | 0x11;
    case -3:
        return 0x12;
    case -4:
        return 0x22;
    case -5:
        return 0x40006010;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

WINUSERAPI BOOL WINAPI IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    return context_value(context) != 0;
}

WINUSERAPI BOOL WINAPI AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT a, DPI_AWARENESS_CONTEXT b)
{
    ULONG x = context_value(a), y = context_value(b);
    if (!x || !y)
        return FALSE;
    return ((x ^ y) & 0x7FFFFFFF) == 0;
}

WINUSERAPI DPI_AWARENESS WINAPI GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    ULONG value = context_value(context);
    return value ? (DPI_AWARENESS)(value & 0xF) : DPI_AWARENESS_INVALID;
}

/* the awareness of the window's thread: windows keeps it per window, mime per process. */
WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI GetWindowDpiAwarenessContext(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    return UserDpiAwarenessContext();
}

/* a window of a per monitor aware process scales its frame; for another process that is refused. */
WINUSERAPI BOOL WINAPI EnableNonClientDpiScaling(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (UserDpiAwareness < 2) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

/* with one scale for every monitor, the logical and the physical point are the same. */
WINUSERAPI BOOL WINAPI LogicalToPhysicalPointForPerMonitorDPI(HWND hwnd, LPPOINT point)
{
    if (!point || (hwnd && !IsWindow(hwnd))) {
        SetLastError(hwnd ? ERROR_INVALID_WINDOW_HANDLE : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI PhysicalToLogicalPointForPerMonitorDPI(HWND hwnd, LPPOINT point)
{
    return LogicalToPhysicalPointForPerMonitorDPI(hwnd, point);
}

/* ------------------------------------------------------------------ pointer input */

#define MOUSE_POINTER_ID 1

static BOOL g_mouse_in_pointer;

WINUSERAPI BOOL WINAPI EnableMouseInPointer(BOOL enable)
{
    g_mouse_in_pointer = enable != FALSE;
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsMouseInPointerEnabled(void)
{
    return g_mouse_in_pointer;
}

/* the mouse as a pointer: where it is, the buttons that are down and the keys that are. the only pointer there is. */
static BOOL mouse_pointer_info(UINT id, POINTER_INFO *info)
{
    if (id != MOUSE_POINTER_ID || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    POINT at;
    GetCursorPos(&at);
    UINT dpi = GetDpiForSystem();
    memset(info, 0, sizeof(*info));
    info->pointerType = PT_MOUSE;
    info->pointerId = MOUSE_POINTER_ID;
    UINT flags = POINTER_FLAG_INRANGE | POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE;
    BOOL first = (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
    BOOL second = (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON) & 0x8000) != 0;
    BOOL third = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    if (first)
        flags |= POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_INCONTACT;
    if (second)
        flags |= POINTER_FLAG_SECONDBUTTON | POINTER_FLAG_INCONTACT;
    if (third)
        flags |= POINTER_FLAG_THIRDBUTTON | POINTER_FLAG_INCONTACT;
    info->pointerFlags = flags;
    info->hwndTarget = WindowFromPoint(at);
    info->ptPixelLocation = info->ptPixelLocationRaw = at;
    /* hundredths of a millimeter, 2540 to the inch. */
    info->ptHimetricLocation.x = info->ptHimetricLocationRaw.x = MulDiv(at.x, 2540, (int)dpi);
    info->ptHimetricLocation.y = info->ptHimetricLocationRaw.y = MulDiv(at.y, 2540, (int)dpi);
    info->dwTime = GetTickCount();
    info->historyCount = 1;
    DWORD keys = 0;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        keys |= 0x4;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        keys |= 0x8;
    info->dwKeyStates = keys;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    info->PerformanceCount = (UINT64)counter.QuadPart;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetPointerInfo(UINT id, POINTER_INFO *info)
{
    return mouse_pointer_info(id, info);
}

/* a frame of the mouse is one pointer; the count is in and out. */
WINUSERAPI BOOL WINAPI GetPointerFrameInfo(UINT id, UINT *count, POINTER_INFO *info)
{
    if (!count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (id != MOUSE_POINTER_ID) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT room = *count;
    *count = 1;
    if (!info)
        return TRUE;
    if (room < 1) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    return mouse_pointer_info(id, info);
}

/* the pointer's coordinates are the screen's. */
WINUSERAPI BOOL WINAPI GetPointerInputTransform(UINT id, UINT count, INPUT_TRANSFORM *transform)
{
    if (id != MOUSE_POINTER_ID || count != 1 || !transform) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(transform, 0, sizeof(*transform));
    /* the identity, with 1.0 as its bits so no floating point is linked in. */
    for (int i = 0; i < 4; i++)
        ((UINT *)transform->m)[i * 5] = 0x3F800000;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SkipPointerFrameMessages(UINT id)
{
    if (id != MOUSE_POINTER_ID) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* the last message was typed or moved by hand: a mouse for the messages of one, else a keyboard. */
WINUSERAPI BOOL WINAPI GetCurrentInputMessageSource(INPUT_MESSAGE_SOURCE *source)
{
    if (!source) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    source->deviceType = IMDT_MOUSE;
    source->originId = IMO_HARDWARE;
    return TRUE;
}

WINUSERAPI DWORD WINAPI GetUnpredictedMessagePos(void)
{
    return GetMessagePos();
}

/* ------------------------------------------------------------------ drag detection */

/*
 * DragDetect: tracks the mouse from a point while the left button is down. it is a drag when the mouse leaves the
 * drag rectangle around the point, and not when the button comes up, Escape is pressed, or the button was never down.
 */
WINUSERAPI BOOL WINAPI DragDetect(HWND hwnd, POINT point)
{
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        return FALSE;
    int cx = GetSystemMetrics(SM_CXDRAG), cy = GetSystemMetrics(SM_CYDRAG);
    POINT origin = point;
    ClientToScreen(hwnd, &origin);
    RECT rect = {origin.x - cx, origin.y - cy, origin.x + cx, origin.y + cy};
    BOOL had_capture = GetCapture() == hwnd;
    if (!had_capture)
        SetCapture(hwnd);
    BOOL drag = FALSE;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE) ||
               PeekMessageW(&msg, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
                goto done;
            if (msg.message == WM_LBUTTONUP || msg.message == WM_NCLBUTTONUP)
                goto done;
        }
        POINT now;
        GetCursorPos(&now);
        if (!PtInRect(&rect, now)) {
            drag = TRUE;
            goto done;
        }
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
            goto done;
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_MOUSE | QS_KEY, 0);
    }
done:
    if (!had_capture)
        ReleaseCapture();
    return drag;
}

/* ------------------------------------------------------------------ sent messages */

WINUSERAPI DWORD WINAPI InSendMessageEx(LPVOID reserved)
{
    return (DWORD)NtUserCallNoParam(MIME_NOPARAM_INSENDMESSAGE);
}

/* whether a message from another thread is being answered and was not yet. */
WINUSERAPI BOOL WINAPI InSendMessage(void)
{
    DWORD flags = InSendMessageEx(nullptr);
    return (flags & ISMEX_SEND) && !(flags & ISMEX_REPLIED);
}

WINUSERAPI BOOL WINAPI ReplyMessage(LRESULT result)
{
    BOOL ok = (BOOL)(ULONG_PTR)NtUserCallOneParam((ULONG_PTR)result, MIME_ONEPARAM_REPLYMESSAGE);
    if (!ok)
        SetLastError(ERROR_INVALID_PARAMETER);
    return ok;
}

/* ------------------------------------------------------------------ window helpers */

/* the window comes forward as a click on its button would bring it. */
WINUSERAPI void WINAPI SwitchToThisWindow(HWND hwnd, BOOL alt_tab)
{
    if (!IsWindow(hwnd))
        return;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

/* no transition is drawn: the window comes or goes at once. */
WINUSERAPI BOOL WINAPI AnimateWindow(HWND hwnd, DWORD time, DWORD flags)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (flags & AW_HIDE) {
        ShowWindow(hwnd, SW_HIDE);
        return TRUE;
    }
    if (flags & AW_ACTIVATE) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    } else {
        ShowWindow(hwnd, SW_SHOWNA);
    }
    RedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_ERASE);
    return TRUE;
}

/* no program may take the input from the user. */
WINUSERAPI BOOL WINAPI BlockInput(BOOL block)
{
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

/* ------------------------------------------------------------------ the clipboard's formats */

WINUSERAPI BOOL WINAPI GetUpdatedClipboardFormats(PUINT formats, UINT count, PUINT out)
{
    return NtUserGetUpdatedClipboardFormats(formats, count, out);
}
