/* netutils.dll: the buffers of the network management calls. programs linked with a recent netapi32.lib import it directly. netapi32 answers. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}
