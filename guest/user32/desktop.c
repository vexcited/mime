/*
 * desktop-level helpers: mouse input as pointer input, message input source, drag detection
 * and a few window helpers.
 */
#include "user32_private.h"
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>

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

/* the mouse is the only pointer: its position, buttons and modifier keys. */
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

/* a mouse frame holds one pointer. count is in and out. */
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

/* pointer coordinates are screen coordinates. */
WINUSERAPI BOOL WINAPI GetPointerInputTransform(UINT id, UINT count, INPUT_TRANSFORM *transform)
{
    if (id != MOUSE_POINTER_ID || count != 1 || !transform) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(transform, 0, sizeof(*transform));
    /* the identity, with 1.0 written as bits so no floating point gets linked in. */
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

/* the device and origin of the input message the thread retrieved last, unavailable for other messages. */
WINUSERAPI BOOL WINAPI GetCurrentInputMessageSource(INPUT_MESSAGE_SOURCE *source)
{
    if (!source) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG_PTR r = NtUserCallNoParam(MIME_NOPARAM_GETINPUTSOURCE);
    source->deviceType = (INPUT_MESSAGE_DEVICE_TYPE)(r & 0xFF);
    source->originId = (INPUT_MESSAGE_ORIGIN_ID)((r >> 8) & 0xFF);
    return TRUE;
}

WINUSERAPI DWORD WINAPI GetUnpredictedMessagePos(void)
{
    return GetMessagePos();
}

/* ------------------------------------------------------------------ drag detection */

/*
 * a drag starts when the mouse leaves the drag rectangle around the point with the left button down. releasing the
 * button or pressing Escape cancels it, and so does a button that was never down.
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

/* TRUE while handling a message sent by another thread that was not replied to yet. */
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

/* brings the window forward like a click on its taskbar button. */
WINUSERAPI void WINAPI SwitchToThisWindow(HWND hwnd, BOOL alt_tab)
{
    if (!IsWindow(hwnd))
        return;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

/* no animation: the window shows or hides at once. */
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

/* no program may block the user's input. */
WINUSERAPI BOOL WINAPI BlockInput(BOOL block)
{
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

/* ------------------------------------------------------------------ clipboard formats */

WINUSERAPI BOOL WINAPI GetUpdatedClipboardFormats(PUINT formats, UINT count, PUINT out)
{
    return NtUserGetUpdatedClipboardFormats(formats, count, out);
}
