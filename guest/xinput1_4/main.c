/* xinput1_4.dll: windows' own xinput, on the engine the xinput dlls share. */
#include "../lib/xinput/xinput_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        XiRevision = 0x0104;
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

XINPUTAPI DWORD WINAPI XInputGetBatteryInformation(DWORD index, BYTE type, XINPUT_BATTERY_INFORMATION *battery)
{
    return XiGetBatteryInformation(index, type, battery);
}

XINPUTAPI DWORD WINAPI XInputGetKeystroke(DWORD index, DWORD reserved, XINPUT_KEYSTROKE *keystroke)
{
    return XiGetKeystroke(index, reserved, keystroke);
}

XINPUTAPI DWORD WINAPI XInputGetAudioDeviceIds(DWORD index, LPWSTR render, UINT *render_count, LPWSTR capture,
                                               UINT *capture_count)
{
    return XiGetAudioDeviceIds(index, render, render_count, capture, capture_count);
}

XINPUTAPI DWORD WINAPI XInputGetBaseBusInformation(DWORD bus, XINPUT_BASE_BUS_INFORMATION *info)
{
    return XiGetBaseBusInformation(bus, info);
}

XINPUTAPI DWORD WINAPI XInputGetCapabilitiesEx(DWORD one, DWORD index, DWORD flags, XINPUT_CAPABILITIES_EX *caps)
{
    return XiGetCapabilitiesEx(one, index, flags, caps);
}

XINPUTAPI DWORD WINAPI XInputGetSystemButtons(DWORD index, void *buttons)
{
    return XiGetSystemButtons(index, buttons);
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
