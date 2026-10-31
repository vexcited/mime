/* security.dll: the security support provider calls of old programs, which are the ones of secur32. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}
