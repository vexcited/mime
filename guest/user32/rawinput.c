/* input currently arrives through window messages, without raw device handles. */
#include "user32_private.h"

typedef struct { HANDLE hDevice; DWORD dwType; } RAWINPUTDEVICELIST;
typedef struct { USHORT usUsagePage, usUsage; DWORD dwFlags; HWND hwndTarget; } RAWINPUTDEVICE;
typedef struct { DWORD dwType, dwSize; HANDLE hDevice; WPARAM wParam; } RAWINPUTHEADER;

WINUSERAPI UINT WINAPI GetRawInputDeviceList(RAWINPUTDEVICELIST *list, UINT *count, UINT size)
{
    if (!count || size != sizeof(*list)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    *count = 0;
    return 0;
}

WINUSERAPI UINT WINAPI GetRawInputDeviceInfoA(HANDLE device, UINT command, void *data, UINT *size)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return (UINT)-1;
}

WINUSERAPI UINT WINAPI GetRawInputData(HANDLE input, UINT command, void *data, UINT *size, UINT header_size)
{
    SetLastError(size && header_size == sizeof(RAWINPUTHEADER) ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
    return (UINT)-1;
}

WINUSERAPI BOOL WINAPI RegisterRawInputDevices(const RAWINPUTDEVICE *devices, UINT count, UINT size)
{
    if (!devices || !count || size != sizeof(*devices)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

WINUSERAPI BOOL WINAPI ClipCursor(const RECT *rect)
{
    if (!rect)
        return TRUE;
    if (rect->left > rect->right || rect->top > rect->bottom) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
