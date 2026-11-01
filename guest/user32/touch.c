/* touch and pen input a machine without them cannot have, the display rotation a machine without a sensor does not do. */
#include "user32_private.h"

#define ORIENTATION_PREFERENCE_NONE 0x0
#define ORIENTATION_PREFERENCE_MASK 0xF

/* no pointer device is a touch screen or a pen, so no pointer id is of those kinds and no device handle is valid. */
WINUSERAPI BOOL WINAPI GetPointerDeviceRects(HANDLE device, RECT *device_rect, RECT *display_rect)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerFrameTouchInfo(UINT id, UINT *count, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerFrameTouchInfoHistory(UINT id, UINT *entries, UINT *count, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerPenInfoHistory(UINT id, UINT *entries, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* a gesture handle comes with a WM_GESTURE message, and none is ever sent. */
WINUSERAPI BOOL WINAPI GetGestureInfo(HGESTUREINFO gesture, PGESTUREINFO info)
{
    SetLastError(info && info->cbSize == sizeof(GESTUREINFO) ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI CloseGestureInfoHandle(HGESTUREINFO gesture)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* the settings are accepted for a window and never used. */
WINUSERAPI BOOL WINAPI SetGestureConfig(HWND hwnd, DWORD reserved, UINT count, PGESTURECONFIG config, UINT size)
{
    if (reserved || size != sizeof(GESTURECONFIG) || (count && !config)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return TRUE;
}

static DWORD g_orientation = ORIENTATION_PREFERENCE_NONE;

/* the preference is kept for the process, nothing turns the screen. */
WINUSERAPI BOOL WINAPI SetDisplayAutoRotationPreferences(DWORD orientation)
{
    if (orientation & ~(DWORD)ORIENTATION_PREFERENCE_MASK) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    g_orientation = orientation;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetDisplayAutoRotationPreferences(DWORD *orientation)
{
    if (!orientation) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *orientation = g_orientation;
    return TRUE;
}

static BOOL g_ime_enabled = TRUE;

/* the old 16 bit switch of the input method: the state it was in. the input method itself stays as it is. */
WINUSERAPI BOOL WINAPI WINNLSEnableIME(HWND hwnd, BOOL enable)
{
    BOOL old = g_ime_enabled;
    g_ime_enabled = enable != FALSE;
    return old;
}
