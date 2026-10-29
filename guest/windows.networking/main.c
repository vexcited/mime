/* windows.networking.dll: the networking classes of the Windows Runtime, of which only a hint to the network stack. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* a socket that streams media is not treated differently by a stack that treats every socket the same. */
HRESULT WINAPI SetSocketMediaStreamingMode(BOOL value)
{
    return 0;
}
