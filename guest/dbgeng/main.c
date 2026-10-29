/* dbgeng.dll: the debugger engine. there is none, so no debugger client can be made. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

HRESULT WINAPI DebugCreate(const GUID *interface_id, void **client)
{
    if (client)
        *client = nullptr;
    return ((HRESULT)0x80004001);
}
