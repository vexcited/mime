/*
 * mswsock forwards everything to wsock32, which implements the extension functions and name services. on Windows the
 * forwarding goes the other way.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
