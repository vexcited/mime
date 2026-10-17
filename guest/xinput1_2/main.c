/* xinput1_2.dll: an older redistributable's xinput, on the engine the xinput dlls share. */
#include "../lib/xinput/xinput_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        XiRevision = 0x0102;
        XiAttach();
    } else if (reason == DLL_PROCESS_DETACH) {
        XiDetach();
    }
    return TRUE;
}

XINPUTAPI DWORD WINAPI XInputGetState(DWORD index, XINPUT_STATE *state)
{
    return XiGetState(index, state, FALSE);
}

XINPUTAPI DWORD WINAPI XInputSetState(DWORD index, XINPUT_VIBRATION *vibration)
{
    return XiSetState(index, vibration);
}

XINPUTAPI DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps)
{
    return XiGetCapabilities(index, flags, caps);
}

XINPUTAPI void WINAPI XInputEnable(BOOL enable)
{
    XiEnable(enable);
}

XINPUTAPI DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, GUID *render, GUID *capture)
{
    return XiGetDSoundAudioDeviceGuids(index, render, capture);
}
