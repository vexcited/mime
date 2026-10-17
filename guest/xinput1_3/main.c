/* xinput1_3.dll: the directx redistributable's xinput, on the engine the xinput dlls share. */
#include "../lib/xinput/xinput_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        XiRevision = 0x0103;
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

XINPUTAPI DWORD WINAPI XInputGetBatteryInformation(DWORD index, BYTE type, XINPUT_BATTERY_INFORMATION *battery)
{
    return XiGetBatteryInformation(index, type, battery);
}

XINPUTAPI DWORD WINAPI XInputGetKeystroke(DWORD index, DWORD reserved, XINPUT_KEYSTROKE *keystroke)
{
    return XiGetKeystroke(index, reserved, keystroke);
}

/* exported by ordinal alone. */
XINPUTAPI DWORD WINAPI XInputGetStateEx(DWORD index, XINPUT_STATE *state)
{
    return XiGetState(index, state, TRUE);
}

XINPUTAPI DWORD WINAPI XInputWaitForGuideButton(DWORD index, HANDLE event, XINPUT_LISTEN_STATE *listen)
{
    return XiWaitForGuideButton(index, event, listen);
}

XINPUTAPI DWORD WINAPI XInputCancelGuideButtonWait(DWORD index)
{
    return XiCancelGuideButtonWait(index);
}

XINPUTAPI DWORD WINAPI XInputPowerOffController(DWORD index)
{
    return XiPowerOffController(index);
}
