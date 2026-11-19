#include <mime/win32/winbase.h>

BOOL WINAPI GetWindowBand(HWND window, DWORD *band);

/* as twinapi.appcore of windows 11: bands 8 to 11 are the ones of immersive windows. */
BOOL WINAPI IsImmersiveWindow(HWND window)
{
    DWORD band = 0;
    BOOL result = GetWindowBand(window, &band);
    if (!result)
        return FALSE;
    if (band - 8 <= 3)
        return TRUE;
    SetLastError(ERROR_SUCCESS);
    return FALSE;
}
