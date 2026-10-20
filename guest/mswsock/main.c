/*
 * mswsock forwards every function to wsock32, which implements the extension functions and the name services that
 * Windows keeps here (wsock32 forwards the other way on Windows, to this library).
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
