/* the xinput engine the xinput dlls share, each a revision of it. */
#ifndef MIME_XINPUT_PRIVATE_H
#define MIME_XINPUT_PRIVATE_H

#define MIME_BUILD_XINPUT
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/setupapi.h>
#include <mime/win32/xinput.h>

/* which dll this is: 0x0101 for xinput1_1 up to 0x0104 for xinput1_4. */
extern USHORT XiRevision;

void XiAttach(void);
void XiDetach(void);

DWORD XiGetState(DWORD index, XINPUT_STATE *state, BOOL guide);
DWORD XiSetState(DWORD index, XINPUT_VIBRATION *vibration);
DWORD XiGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps);
DWORD XiGetCapabilitiesEx(DWORD one, DWORD index, DWORD flags, XINPUT_CAPABILITIES_EX *caps);
void XiEnable(BOOL enable);
DWORD XiGetBatteryInformation(DWORD index, BYTE type, XINPUT_BATTERY_INFORMATION *battery);
DWORD XiGetKeystroke(DWORD index, DWORD reserved, XINPUT_KEYSTROKE *keystroke);
DWORD XiGetDSoundAudioDeviceGuids(DWORD index, GUID *render, GUID *capture);
DWORD XiGetAudioDeviceIds(DWORD index, LPWSTR render, UINT *render_count, LPWSTR capture, UINT *capture_count);
DWORD XiWaitForGuideButton(DWORD index, HANDLE event, XINPUT_LISTEN_STATE *listen);
DWORD XiCancelGuideButtonWait(DWORD index);
DWORD XiPowerOffController(DWORD index);
DWORD XiGetBaseBusInformation(DWORD bus, XINPUT_BASE_BUS_INFORMATION *info);
DWORD XiGetSystemButtons(DWORD index, void *out);

#endif
