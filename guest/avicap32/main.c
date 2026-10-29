/* avicap32.dll: video capture windows. no capture driver is installed, so there is none to list and no window to make. */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* a capture window without a driver to connect to has nothing to do, so none is made. */
HWND WINAPI capCreateCaptureWindowW(LPCWSTR name, DWORD style, int x, int y, int width, int height, HWND parent, int id)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

HWND WINAPI capCreateCaptureWindowA(LPCSTR name, DWORD style, int x, int y, int width, int height, HWND parent, int id)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

/* the drivers are numbered from zero, and none has a number. */
BOOL WINAPI capGetDriverDescriptionW(UINT index, LPWSTR name, int name_size, LPWSTR version, int version_size)
{
    if (name && name_size > 0)
        name[0] = 0;
    if (version && version_size > 0)
        version[0] = 0;
    return FALSE;
}

BOOL WINAPI capGetDriverDescriptionA(UINT index, LPSTR name, int name_size, LPSTR version, int version_size)
{
    if (name && name_size > 0)
        name[0] = 0;
    if (version && version_size > 0)
        version[0] = 0;
    return FALSE;
}
