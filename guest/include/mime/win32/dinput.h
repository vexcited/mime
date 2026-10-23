/*
 * directinput: dinput.dll versions 1 to 7 and dinput8.dll, with devices and effects.
 * methods follow windows' vtable order, structures have windows' x86 and x64 layouts.
 */
#ifndef MIME_WIN32_DINPUT_H
#define MIME_WIN32_DINPUT_H

#include <mime/win32/objbase.h>

typedef struct IDirectInputA IDirectInputA, *LPDIRECTINPUTA;
typedef struct IDirectInputW IDirectInputW, *LPDIRECTINPUTW;
typedef struct IDirectInput2A IDirectInput2A, *LPDIRECTINPUT2A;
typedef struct IDirectInput2W IDirectInput2W, *LPDIRECTINPUT2W;
typedef struct IDirectInput7A IDirectInput7A, *LPDIRECTINPUT7A;
typedef struct IDirectInput7W IDirectInput7W, *LPDIRECTINPUT7W;
typedef struct IDirectInput8A IDirectInput8A, *LPDIRECTINPUT8A;
typedef struct IDirectInput8W IDirectInput8W, *LPDIRECTINPUT8W;
typedef struct IDirectInputDeviceA IDirectInputDeviceA, *LPDIRECTINPUTDEVICEA;
typedef struct IDirectInputDeviceW IDirectInputDeviceW, *LPDIRECTINPUTDEVICEW;
typedef struct IDirectInputDevice2A IDirectInputDevice2A, *LPDIRECTINPUTDEVICE2A;
typedef struct IDirectInputDevice2W IDirectInputDevice2W, *LPDIRECTINPUTDEVICE2W;
typedef struct IDirectInputDevice7A IDirectInputDevice7A, *LPDIRECTINPUTDEVICE7A;
typedef struct IDirectInputDevice7W IDirectInputDevice7W, *LPDIRECTINPUTDEVICE7W;
typedef struct IDirectInputDevice8A IDirectInputDevice8A, *LPDIRECTINPUTDEVICE8A;
typedef struct IDirectInputDevice8W IDirectInputDevice8W, *LPDIRECTINPUTDEVICE8W;
typedef struct IDirectInputEffect IDirectInputEffect, *LPDIRECTINPUTEFFECT;

/* ------------------------------------------------------------------ guids */

MIME_DEFINE_GUID(CLSID_DirectInput, 0x25E609E0, 0xB259, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(CLSID_DirectInputDevice, 0x25E609E1, 0xB259, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(CLSID_DirectInput8, 0x25E609E4, 0xB259, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(CLSID_DirectInputDevice8, 0x25E609E5, 0xB259, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

MIME_DEFINE_GUID(IID_IDirectInputA, 0x89521360, 0xAA8A, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputW, 0x89521361, 0xAA8A, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInput2A, 0x5944E662, 0xAA8A, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInput2W, 0x5944E663, 0xAA8A, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInput7A, 0x9A4CB684, 0x236D, 0x11D3, 0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE);
MIME_DEFINE_GUID(IID_IDirectInput7W, 0x9A4CB685, 0x236D, 0x11D3, 0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE);
MIME_DEFINE_GUID(IID_IDirectInput8A, 0xBF798030, 0x483A, 0x4DA2, 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00);
MIME_DEFINE_GUID(IID_IDirectInput8W, 0xBF798031, 0x483A, 0x4DA2, 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputDeviceA, 0x5944E680, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputDeviceW, 0x5944E681, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputDevice2A, 0x5944E682, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputDevice2W, 0x5944E683, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputDevice7A, 0x57D7C6BC, 0x2356, 0x11D3, 0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE);
MIME_DEFINE_GUID(IID_IDirectInputDevice7W, 0x57D7C6BD, 0x2356, 0x11D3, 0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE);
MIME_DEFINE_GUID(IID_IDirectInputDevice8A, 0x54D41080, 0xDC15, 0x4833, 0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79);
MIME_DEFINE_GUID(IID_IDirectInputDevice8W, 0x54D41081, 0xDC15, 0x4833, 0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79);
MIME_DEFINE_GUID(IID_IDirectInputEffect, 0xE7E1F7C0, 0x88D2, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(IID_IDirectInputJoyConfig, 0x1DE12AB1, 0xC9F5, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IDirectInputJoyConfig8, 0xEB0D7DFA, 0x1990, 0x4F27, 0xB4, 0xD6, 0xED, 0xF2, 0xEE, 0xC4, 0xA4, 0x4C);

MIME_DEFINE_GUID(GUID_XAxis, 0xA36D02E0, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_YAxis, 0xA36D02E1, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_ZAxis, 0xA36D02E2, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_RxAxis, 0xA36D02F4, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_RyAxis, 0xA36D02F5, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_RzAxis, 0xA36D02E3, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_Slider, 0xA36D02E4, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_Button, 0xA36D02F0, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_Key, 0x55728220, 0xD33C, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_POV, 0xA36D02F2, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_Unknown, 0xA36D02F3, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

MIME_DEFINE_GUID(GUID_SysMouse, 0x6F1D2B60, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_SysKeyboard, 0x6F1D2B61, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_Joystick, 0x6F1D2B70, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_SysMouseEm, 0x6F1D2B80, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_SysMouseEm2, 0x6F1D2B81, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_SysKeyboardEm, 0x6F1D2B82, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_SysKeyboardEm2, 0x6F1D2B83, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(GUID_HIDClass, 0x745A17A0, 0x74D3, 0x11D0, 0xB6, 0xFE, 0x00, 0xA0, 0xC9, 0x0F, 0x57, 0xDA);

MIME_DEFINE_GUID(GUID_ConstantForce, 0x13541C20, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_RampForce, 0x13541C21, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Square, 0x13541C22, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Sine, 0x13541C23, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Triangle, 0x13541C24, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_SawtoothUp, 0x13541C25, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_SawtoothDown, 0x13541C26, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Spring, 0x13541C27, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Damper, 0x13541C28, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Inertia, 0x13541C29, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_Friction, 0x13541C2A, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);
MIME_DEFINE_GUID(GUID_CustomForce, 0x13541C2B, 0x8E33, 0x11D0, 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35);

/* ------------------------------------------------------------------ results */

#define DI_OK S_OK
#define DI_NOTATTACHED S_FALSE
#define DI_BUFFEROVERFLOW S_FALSE
#define DI_PROPNOEFFECT S_FALSE
#define DI_NOEFFECT S_FALSE
#define DI_POLLEDDEVICE ((HRESULT)0x00000002L)
#define DI_DOWNLOADSKIPPED ((HRESULT)0x00000003L)
#define DI_EFFECTRESTARTED ((HRESULT)0x00000004L)
#define DI_TRUNCATED ((HRESULT)0x00000008L)
#define DI_SETTINGSNOTSAVED ((HRESULT)0x0000000BL)
#define DI_TRUNCATEDANDRESTARTED ((HRESULT)0x0000000CL)
#define DI_WRITEPROTECT ((HRESULT)0x00000013L)

#define DIERR_OLDDIRECTINPUTVERSION ((HRESULT)0x8007047EL)
#define DIERR_BETADIRECTINPUTVERSION ((HRESULT)0x80070481L)
#define DIERR_BADDRIVERVER ((HRESULT)0x80070077L)
#define DIERR_DEVICENOTREG ((HRESULT)0x80040154L)
#define DIERR_NOTFOUND ((HRESULT)0x80070002L)
#define DIERR_OBJECTNOTFOUND ((HRESULT)0x80070002L)
#define DIERR_INVALIDPARAM E_INVALIDARG
#define DIERR_NOINTERFACE E_NOINTERFACE
#define DIERR_GENERIC E_FAIL
#define DIERR_OUTOFMEMORY E_OUTOFMEMORY
#define DIERR_UNSUPPORTED E_NOTIMPL
#define DIERR_NOTINITIALIZED ((HRESULT)0x80070015L)
#define DIERR_ALREADYINITIALIZED ((HRESULT)0x800704DFL)
#define DIERR_NOAGGREGATION CLASS_E_NOAGGREGATION
#define DIERR_OTHERAPPHASPRIO E_ACCESSDENIED
#define DIERR_INPUTLOST ((HRESULT)0x8007001EL)
#define DIERR_ACQUIRED ((HRESULT)0x800700AAL)
#define DIERR_NOTACQUIRED ((HRESULT)0x8007000CL)
#define DIERR_READONLY E_ACCESSDENIED
#define DIERR_HANDLEEXISTS E_ACCESSDENIED
#define DIERR_INSUFFICIENTPRIVS ((HRESULT)0x80040200L)
#define DIERR_DEVICEFULL ((HRESULT)0x80040201L)
#define DIERR_MOREDATA ((HRESULT)0x80040202L)
#define DIERR_NOTDOWNLOADED ((HRESULT)0x80040203L)
#define DIERR_HASEFFECTS ((HRESULT)0x80040204L)
#define DIERR_NOTEXCLUSIVEACQUIRED ((HRESULT)0x80040205L)
#define DIERR_INCOMPLETEEFFECT ((HRESULT)0x80040206L)
#define DIERR_NOTBUFFERED ((HRESULT)0x80040207L)
#define DIERR_EFFECTPLAYING ((HRESULT)0x80040208L)
#define DIERR_UNPLUGGED ((HRESULT)0x80040209L)
#define DIERR_REPORTFULL ((HRESULT)0x8004020AL)
#define DIERR_MAPFILEFAIL ((HRESULT)0x8004020BL)

#define DIENUM_STOP 0
#define DIENUM_CONTINUE 1

/* ------------------------------------------------------------------ device types */

#define DIDEVTYPE_DEVICE 1
#define DIDEVTYPE_MOUSE 2
#define DIDEVTYPE_KEYBOARD 3
#define DIDEVTYPE_JOYSTICK 4
#define DIDEVTYPE_HID 0x00010000

#define DIDEVTYPEMOUSE_UNKNOWN 1
#define DIDEVTYPEMOUSE_TRADITIONAL 2
#define DIDEVTYPEMOUSE_FINGERSTICK 3
#define DIDEVTYPEMOUSE_TOUCHPAD 4
#define DIDEVTYPEMOUSE_TRACKBALL 5
#define DIDEVTYPEKEYBOARD_UNKNOWN 0
#define DIDEVTYPEKEYBOARD_PCXT 1
#define DIDEVTYPEKEYBOARD_OLIVETTI 2
#define DIDEVTYPEKEYBOARD_PCAT 3
#define DIDEVTYPEKEYBOARD_PCENH 4
#define DIDEVTYPEJOYSTICK_UNKNOWN 1
#define DIDEVTYPEJOYSTICK_TRADITIONAL 2
#define DIDEVTYPEJOYSTICK_FLIGHTSTICK 3
#define DIDEVTYPEJOYSTICK_GAMEPAD 4
#define DIDEVTYPEJOYSTICK_RUDDER 5
#define DIDEVTYPEJOYSTICK_WHEEL 6
#define DIDEVTYPEJOYSTICK_HEADTRACKER 7

#define DI8DEVCLASS_ALL 0
#define DI8DEVCLASS_DEVICE 1
#define DI8DEVCLASS_POINTER 2
#define DI8DEVCLASS_KEYBOARD 3
#define DI8DEVCLASS_GAMECTRL 4

#define DI8DEVTYPE_DEVICE 0x11
#define DI8DEVTYPE_MOUSE 0x12
#define DI8DEVTYPE_KEYBOARD 0x13
#define DI8DEVTYPE_JOYSTICK 0x14
#define DI8DEVTYPE_GAMEPAD 0x15
#define DI8DEVTYPE_DRIVING 0x16
#define DI8DEVTYPE_FLIGHT 0x17
#define DI8DEVTYPE_1STPERSON 0x18
#define DI8DEVTYPE_DEVICECTRL 0x19
#define DI8DEVTYPE_SCREENPOINTER 0x1A
#define DI8DEVTYPE_REMOTE 0x1B
#define DI8DEVTYPE_SUPPLEMENTAL 0x1C

#define DI8DEVTYPEMOUSE_UNKNOWN 1
#define DI8DEVTYPEMOUSE_TRADITIONAL 2
#define DI8DEVTYPEKEYBOARD_UNKNOWN 0
#define DI8DEVTYPEKEYBOARD_PCXT 1
#define DI8DEVTYPEKEYBOARD_PCAT 3
#define DI8DEVTYPEKEYBOARD_PCENH 4
#define DI8DEVTYPEJOYSTICK_LIMITED 1
#define DI8DEVTYPEJOYSTICK_STANDARD 2
#define DI8DEVTYPEGAMEPAD_LIMITED 1
#define DI8DEVTYPEGAMEPAD_STANDARD 2
#define DI8DEVTYPEGAMEPAD_TILT 3

#define GET_DIDEVICE_TYPE(type) LOBYTE(type)
#define GET_DIDEVICE_SUBTYPE(type) HIBYTE(type)

/* ------------------------------------------------------------------ flags */

#define DIEDFL_ALLDEVICES 0x00000000
#define DIEDFL_ATTACHEDONLY 0x00000001
#define DIEDFL_FORCEFEEDBACK 0x00000100
#define DIEDFL_INCLUDEALIASES 0x00010000
#define DIEDFL_INCLUDEPHANTOMS 0x00020000
#define DIEDFL_INCLUDEHIDDEN 0x00040000

#define DIDC_ATTACHED 0x00000001
#define DIDC_POLLEDDEVICE 0x00000002
#define DIDC_EMULATED 0x00000004
#define DIDC_POLLEDDATAFORMAT 0x00000008
#define DIDC_FORCEFEEDBACK 0x00000100
#define DIDC_FFATTACK 0x00000200
#define DIDC_FFFADE 0x00000400
#define DIDC_SATURATION 0x00000800
#define DIDC_POSNEGCOEFFICIENTS 0x00001000
#define DIDC_POSNEGSATURATION 0x00002000
#define DIDC_DEADBAND 0x00004000
#define DIDC_STARTDELAY 0x00008000
#define DIDC_ALIAS 0x00010000
#define DIDC_PHANTOM 0x00020000
#define DIDC_HIDDEN 0x00040000

#define DIDFT_ALL 0x00000000
#define DIDFT_RELAXIS 0x00000001
#define DIDFT_ABSAXIS 0x00000002
#define DIDFT_AXIS 0x00000003
#define DIDFT_PSHBUTTON 0x00000004
#define DIDFT_TGLBUTTON 0x00000008
#define DIDFT_BUTTON 0x0000000C
#define DIDFT_POV 0x00000010
#define DIDFT_COLLECTION 0x00000040
#define DIDFT_NODATA 0x00000080
#define DIDFT_ANYINSTANCE 0x00FFFF00
#define DIDFT_INSTANCEMASK DIDFT_ANYINSTANCE
#define DIDFT_MAKEINSTANCE(n) ((WORD)(n) << 8)
#define DIDFT_GETTYPE(n) LOBYTE(n)
#define DIDFT_GETINSTANCE(n) LOWORD((n) >> 8)
#define DIDFT_FFACTUATOR 0x01000000
#define DIDFT_FFEFFECTTRIGGER 0x02000000
#define DIDFT_OUTPUT 0x10000000
#define DIDFT_VENDORDEFINED 0x04000000
#define DIDFT_ALIAS 0x08000000
#define DIDFT_OPTIONAL 0x80000000
#define DIDFT_ENUMCOLLECTION(n) ((WORD)(n) << 8)
#define DIDFT_NOCOLLECTION 0x00FFFF00

#define DIDF_ABSAXIS 0x00000001
#define DIDF_RELAXIS 0x00000002

#define DIDOI_FFACTUATOR 0x00000001
#define DIDOI_FFEFFECTTRIGGER 0x00000002
#define DIDOI_POLLED 0x00008000
#define DIDOI_ASPECTPOSITION 0x00000100
#define DIDOI_ASPECTVELOCITY 0x00000200
#define DIDOI_ASPECTACCEL 0x00000300
#define DIDOI_ASPECTFORCE 0x00000400
#define DIDOI_ASPECTMASK 0x00000F00
#define DIDOI_GUIDISUSAGE 0x00010000

#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DIPH_BYID 2
#define DIPH_BYUSAGE 3
#define DIMAKEUSAGEDWORD(page, usage) (DWORD) MAKELONG(usage, page)

#define DISCL_EXCLUSIVE 0x00000001
#define DISCL_NONEXCLUSIVE 0x00000002
#define DISCL_FOREGROUND 0x00000004
#define DISCL_BACKGROUND 0x00000008
#define DISCL_NOWINKEY 0x00000010

#define DIGDD_PEEK 0x00000001
#define DISDD_CONTINUE 0x00000001

#define DISEQUENCE_COMPARE(a, cmp, b) ((int)((a) - (b)) cmp 0)

#define DIPROPAXISMODE_ABS 0
#define DIPROPAXISMODE_REL 1
#define DIPROPAUTOCENTER_OFF 0
#define DIPROPAUTOCENTER_ON 1
#define DIPROPCALIBRATIONMODE_COOKED 0
#define DIPROPCALIBRATIONMODE_RAW 1

/* properties are small numbers cast to guid pointers. */
#define MAKEDIPROP(prop) ((REFGUID)(ULONG_PTR)(prop))
#define DIPROP_BUFFERSIZE MAKEDIPROP(1)
#define DIPROP_AXISMODE MAKEDIPROP(2)
#define DIPROP_GRANULARITY MAKEDIPROP(3)
#define DIPROP_RANGE MAKEDIPROP(4)
#define DIPROP_DEADZONE MAKEDIPROP(5)
#define DIPROP_SATURATION MAKEDIPROP(6)
#define DIPROP_FFGAIN MAKEDIPROP(7)
#define DIPROP_FFLOAD MAKEDIPROP(8)
#define DIPROP_AUTOCENTER MAKEDIPROP(9)
#define DIPROP_CALIBRATIONMODE MAKEDIPROP(10)
#define DIPROP_CALIBRATION MAKEDIPROP(11)
#define DIPROP_GUIDANDPATH MAKEDIPROP(12)
#define DIPROP_INSTANCENAME MAKEDIPROP(13)
#define DIPROP_PRODUCTNAME MAKEDIPROP(14)
#define DIPROP_JOYSTICKID MAKEDIPROP(15)
#define DIPROP_GETPORTDISPLAYNAME MAKEDIPROP(16)
#define DIPROP_PHYSICALRANGE MAKEDIPROP(18)
#define DIPROP_LOGICALRANGE MAKEDIPROP(19)
#define DIPROP_KEYNAME MAKEDIPROP(20)
#define DIPROP_CPOINTS MAKEDIPROP(21)
#define DIPROP_APPDATA MAKEDIPROP(22)
#define DIPROP_SCANCODE MAKEDIPROP(23)
#define DIPROP_VIDPID MAKEDIPROP(24)
#define DIPROP_USERNAME MAKEDIPROP(25)
#define DIPROP_TYPENAME MAKEDIPROP(26)
#define DIPROPRANGE_NOMIN ((LONG)0x80000000)
#define DIPROPRANGE_NOMAX ((LONG)0x7FFFFFFF)
#define MAXCPOINTSNUM 8

/* ------------------------------------------------------------------ force feedback flags */

#define DIEFT_ALL 0x00000000
#define DIEFT_CONSTANTFORCE 0x00000001
#define DIEFT_RAMPFORCE 0x00000002
#define DIEFT_PERIODIC 0x00000003
#define DIEFT_CONDITION 0x00000004
#define DIEFT_CUSTOMFORCE 0x00000005
#define DIEFT_HARDWARE 0x000000FF
#define DIEFT_FFATTACK 0x00000200
#define DIEFT_FFFADE 0x00000400
#define DIEFT_SATURATION 0x00000800
#define DIEFT_POSNEGCOEFFICIENTS 0x00001000
#define DIEFT_POSNEGSATURATION 0x00002000
#define DIEFT_DEADBAND 0x00004000
#define DIEFT_STARTDELAY 0x00008000
#define DIEFT_GETTYPE(n) LOBYTE(n)

#define DI_DEGREES 100
#define DI_FFNOMINALMAX 10000
#define DI_SECONDS 1000000

#define DIEFF_OBJECTIDS 0x00000001
#define DIEFF_OBJECTOFFSETS 0x00000002
#define DIEFF_CARTESIAN 0x00000010
#define DIEFF_POLAR 0x00000020
#define DIEFF_SPHERICAL 0x00000040

#define DIEP_DURATION 0x00000001
#define DIEP_SAMPLEPERIOD 0x00000002
#define DIEP_GAIN 0x00000004
#define DIEP_TRIGGERBUTTON 0x00000008
#define DIEP_TRIGGERREPEATINTERVAL 0x00000010
#define DIEP_AXES 0x00000020
#define DIEP_DIRECTION 0x00000040
#define DIEP_ENVELOPE 0x00000080
#define DIEP_TYPESPECIFICPARAMS 0x00000100
#define DIEP_STARTDELAY 0x00000200
#define DIEP_ALLPARAMS_DX5 0x000001FF
#define DIEP_ALLPARAMS 0x000003FF
#define DIEP_START 0x20000000
#define DIEP_NORESTART 0x40000000
#define DIEP_NODOWNLOAD 0x80000000
#define DIEB_NOTRIGGER 0xFFFFFFFF

#define DIES_SOLO 0x00000001
#define DIES_NODOWNLOAD 0x80000000
#define DIEGES_PLAYING 0x00000001
#define DIEGES_EMULATED 0x00000002

#define DISFFC_RESET 0x00000001
#define DISFFC_STOPALL 0x00000002
#define DISFFC_PAUSE 0x00000004
#define DISFFC_CONTINUE 0x00000008
#define DISFFC_SETACTUATORSON 0x00000010
#define DISFFC_SETACTUATORSOFF 0x00000020

#define DIGFFS_EMPTY 0x00000001
#define DIGFFS_STOPPED 0x00000002
#define DIGFFS_PAUSED 0x00000004
#define DIGFFS_ACTUATORSON 0x00000010
#define DIGFFS_ACTUATORSOFF 0x00000020
#define DIGFFS_POWERON 0x00000040
#define DIGFFS_POWEROFF 0x00000080
#define DIGFFS_SAFETYSWITCHON 0x00000100
#define DIGFFS_SAFETYSWITCHOFF 0x00000200
#define DIGFFS_USERFFSWITCHON 0x00000400
#define DIGFFS_USERFFSWITCHOFF 0x00000800
#define DIGFFS_DEVICELOST 0x80000000

#define DIFEF_DEFAULT 0x00000000
#define DIFEF_INCLUDENONSTANDARD 0x00000001
#define DIFEF_MODIFYIFNEEDED 0x00000010

/* ------------------------------------------------------------------ action mapping flags */

#define DIA_FORCEFEEDBACK 0x00000001
#define DIA_APPMAPPED 0x00000002
#define DIA_APPNOMAP 0x00000004
#define DIA_NORANGE 0x00000008
#define DIA_APPFIXED 0x00000010

#define DIAH_UNMAPPED 0x00000000
#define DIAH_USERCONFIG 0x00000001
#define DIAH_APPREQUESTED 0x00000002
#define DIAH_HWAPP 0x00000004
#define DIAH_HWDEFAULT 0x00000008
#define DIAH_DEFAULT 0x00000020
#define DIAH_ERROR 0x80000000

#define DIAFTS_NEWDEVICELOW 0xFFFFFFFF
#define DIAFTS_NEWDEVICEHIGH 0xFFFFFFFF
#define DIAFTS_UNUSEDDEVICELOW 0x00000000
#define DIAFTS_UNUSEDDEVICEHIGH 0x00000000

#define DIDBAM_DEFAULT 0x00000000
#define DIDBAM_PRESERVE 0x00000001
#define DIDBAM_INITIALIZE 0x00000002
#define DIDBAM_HWDEFAULTS 0x00000004

#define DIDSAM_DEFAULT 0x00000000
#define DIDSAM_NOUSER 0x00000001
#define DIDSAM_FORCESAVE 0x00000002

#define DICD_DEFAULT 0x00000000
#define DICD_EDIT 0x00000001

#define DIEDBS_MAPPEDPRI1 0x00000001
#define DIEDBS_MAPPEDPRI2 0x00000002
#define DIEDBS_RECENTDEVICE 0x00000010
#define DIEDBS_NEWDEVICE 0x00000020

#define DIEDBSFL_ATTACHEDONLY 0x00000000
#define DIEDBSFL_THISUSER 0x00000010
#define DIEDBSFL_FORCEFEEDBACK DIEDFL_FORCEFEEDBACK
#define DIEDBSFL_AVAILABLEDEVICES 0x00001000
#define DIEDBSFL_MULTIMICEKEYBOARDS 0x00002000
#define DIEDBSFL_NONGAMINGDEVICES 0x00004000
#define DIEDBSFL_VALID 0x00007110

#define DIVIRTUAL_DRIVING_RACE 0x01000000
#define DIKEYBOARD_GENRE 0x81000000
#define DIMOUSE_GENRE 0x82000000
#define DIVOICE_GENRE 0x83000000

/* ------------------------------------------------------------------ structures */

typedef struct DIDEVCAPS_DX3 {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDevType;
    DWORD dwAxes;
    DWORD dwButtons;
    DWORD dwPOVs;
} DIDEVCAPS_DX3, *LPDIDEVCAPS_DX3;

typedef struct DIDEVCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDevType;
    DWORD dwAxes;
    DWORD dwButtons;
    DWORD dwPOVs;
    DWORD dwFFSamplePeriod;
    DWORD dwFFMinTimeResolution;
    DWORD dwFirmwareRevision;
    DWORD dwHardwareRevision;
    DWORD dwFFDriverVersion;
} DIDEVCAPS, *LPDIDEVCAPS;

typedef struct _DIOBJECTDATAFORMAT {
    const GUID *pguid;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
} DIOBJECTDATAFORMAT, *LPDIOBJECTDATAFORMAT;
typedef const DIOBJECTDATAFORMAT *LPCDIOBJECTDATAFORMAT;

typedef struct _DIDATAFORMAT {
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    LPDIOBJECTDATAFORMAT rgodf;
} DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT *LPCDIDATAFORMAT;

typedef struct DIDEVICEOBJECTDATA_DX3 {
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
} DIDEVICEOBJECTDATA_DX3, *LPDIDEVICEOBJECTDATA_DX3;
typedef const DIDEVICEOBJECTDATA_DX3 *LPCDIDEVICEOBJECTDATA_DX3;

typedef struct DIDEVICEOBJECTDATA {
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
    UINT_PTR uAppData;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;
typedef const DIDEVICEOBJECTDATA *LPCDIDEVICEOBJECTDATA;

typedef struct DIDEVICEOBJECTINSTANCE_DX3A {
    DWORD dwSize;
    GUID guidType;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
    CHAR tszName[MAX_PATH];
} DIDEVICEOBJECTINSTANCE_DX3A, *LPDIDEVICEOBJECTINSTANCE_DX3A;

typedef struct DIDEVICEOBJECTINSTANCE_DX3W {
    DWORD dwSize;
    GUID guidType;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
    WCHAR tszName[MAX_PATH];
} DIDEVICEOBJECTINSTANCE_DX3W, *LPDIDEVICEOBJECTINSTANCE_DX3W;

typedef struct DIDEVICEOBJECTINSTANCEA {
    DWORD dwSize;
    GUID guidType;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
    CHAR tszName[MAX_PATH];
    DWORD dwFFMaxForce;
    DWORD dwFFForceResolution;
    WORD wCollectionNumber;
    WORD wDesignatorIndex;
    WORD wUsagePage;
    WORD wUsage;
    DWORD dwDimension;
    WORD wExponent;
    WORD wReportId;
} DIDEVICEOBJECTINSTANCEA, *LPDIDEVICEOBJECTINSTANCEA;
typedef const DIDEVICEOBJECTINSTANCEA *LPCDIDEVICEOBJECTINSTANCEA;

typedef struct DIDEVICEOBJECTINSTANCEW {
    DWORD dwSize;
    GUID guidType;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
    WCHAR tszName[MAX_PATH];
    DWORD dwFFMaxForce;
    DWORD dwFFForceResolution;
    WORD wCollectionNumber;
    WORD wDesignatorIndex;
    WORD wUsagePage;
    WORD wUsage;
    DWORD dwDimension;
    WORD wExponent;
    WORD wReportId;
} DIDEVICEOBJECTINSTANCEW, *LPDIDEVICEOBJECTINSTANCEW;
typedef const DIDEVICEOBJECTINSTANCEW *LPCDIDEVICEOBJECTINSTANCEW;

typedef struct DIDEVICEINSTANCE_DX3A {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH];
    CHAR tszProductName[MAX_PATH];
} DIDEVICEINSTANCE_DX3A, *LPDIDEVICEINSTANCE_DX3A;

typedef struct DIDEVICEINSTANCE_DX3W {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    WCHAR tszInstanceName[MAX_PATH];
    WCHAR tszProductName[MAX_PATH];
} DIDEVICEINSTANCE_DX3W, *LPDIDEVICEINSTANCE_DX3W;

typedef struct DIDEVICEINSTANCEA {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH];
    CHAR tszProductName[MAX_PATH];
    GUID guidFFDriver;
    WORD wUsagePage;
    WORD wUsage;
} DIDEVICEINSTANCEA, *LPDIDEVICEINSTANCEA;
typedef const DIDEVICEINSTANCEA *LPCDIDEVICEINSTANCEA;

typedef struct DIDEVICEINSTANCEW {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    WCHAR tszInstanceName[MAX_PATH];
    WCHAR tszProductName[MAX_PATH];
    GUID guidFFDriver;
    WORD wUsagePage;
    WORD wUsage;
} DIDEVICEINSTANCEW, *LPDIDEVICEINSTANCEW;
typedef const DIDEVICEINSTANCEW *LPCDIDEVICEINSTANCEW;

typedef struct DIPROPHEADER {
    DWORD dwSize;
    DWORD dwHeaderSize;
    DWORD dwObj;
    DWORD dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER *LPCDIPROPHEADER;

typedef struct DIPROPDWORD {
    DIPROPHEADER diph;
    DWORD dwData;
} DIPROPDWORD, *LPDIPROPDWORD;
typedef const DIPROPDWORD *LPCDIPROPDWORD;

typedef struct DIPROPPOINTER {
    DIPROPHEADER diph;
    UINT_PTR uData;
} DIPROPPOINTER, *LPDIPROPPOINTER;
typedef const DIPROPPOINTER *LPCDIPROPPOINTER;

typedef struct DIPROPRANGE {
    DIPROPHEADER diph;
    LONG lMin;
    LONG lMax;
} DIPROPRANGE, *LPDIPROPRANGE;
typedef const DIPROPRANGE *LPCDIPROPRANGE;

typedef struct DIPROPCAL {
    DIPROPHEADER diph;
    LONG lMin;
    LONG lCenter;
    LONG lMax;
} DIPROPCAL, *LPDIPROPCAL;
typedef const DIPROPCAL *LPCDIPROPCAL;

typedef struct DIPROPCALPOV {
    DIPROPHEADER diph;
    LONG lMin[5];
    LONG lMax[5];
} DIPROPCALPOV, *LPDIPROPCALPOV;
typedef const DIPROPCALPOV *LPCDIPROPCALPOV;

typedef struct DIPROPGUIDANDPATH {
    DIPROPHEADER diph;
    GUID guidClass;
    WCHAR wszPath[MAX_PATH];
} DIPROPGUIDANDPATH, *LPDIPROPGUIDANDPATH;
typedef const DIPROPGUIDANDPATH *LPCDIPROPGUIDANDPATH;

typedef struct DIPROPSTRING {
    DIPROPHEADER diph;
    WCHAR wsz[MAX_PATH];
} DIPROPSTRING, *LPDIPROPSTRING;
typedef const DIPROPSTRING *LPCDIPROPSTRING;

typedef struct _CPOINT {
    LONG lP;
    DWORD dwLog;
} CPOINT, *PCPOINT;

typedef struct DIPROPCPOINTS {
    DIPROPHEADER diph;
    DWORD dwCPointsNum;
    CPOINT cp[MAXCPOINTSNUM];
} DIPROPCPOINTS, *LPDIPROPCPOINTS;
typedef const DIPROPCPOINTS *LPCDIPROPCPOINTS;

typedef struct _DIMOUSESTATE {
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[4];
} DIMOUSESTATE, *LPDIMOUSESTATE;

typedef struct _DIMOUSESTATE2 {
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[8];
} DIMOUSESTATE2, *LPDIMOUSESTATE2;

typedef struct DIJOYSTATE {
    LONG lX;
    LONG lY;
    LONG lZ;
    LONG lRx;
    LONG lRy;
    LONG lRz;
    LONG rglSlider[2];
    DWORD rgdwPOV[4];
    BYTE rgbButtons[32];
} DIJOYSTATE, *LPDIJOYSTATE;

typedef struct DIJOYSTATE2 {
    LONG lX;
    LONG lY;
    LONG lZ;
    LONG lRx;
    LONG lRy;
    LONG lRz;
    LONG rglSlider[2];
    DWORD rgdwPOV[4];
    BYTE rgbButtons[128];
    LONG lVX;
    LONG lVY;
    LONG lVZ;
    LONG lVRx;
    LONG lVRy;
    LONG lVRz;
    LONG rglVSlider[2];
    LONG lAX;
    LONG lAY;
    LONG lAZ;
    LONG lARx;
    LONG lARy;
    LONG lARz;
    LONG rglASlider[2];
    LONG lFX;
    LONG lFY;
    LONG lFZ;
    LONG lFRx;
    LONG lFRy;
    LONG lFRz;
    LONG rglFSlider[2];
} DIJOYSTATE2, *LPDIJOYSTATE2;

#define DIMOFS_X 0
#define DIMOFS_Y 4
#define DIMOFS_Z 8
#define DIMOFS_BUTTON0 12
#define DIMOFS_BUTTON1 13
#define DIMOFS_BUTTON2 14
#define DIMOFS_BUTTON3 15
#define DIMOFS_BUTTON4 16
#define DIMOFS_BUTTON5 17
#define DIMOFS_BUTTON6 18
#define DIMOFS_BUTTON7 19

#define DIJOFS_X 0
#define DIJOFS_Y 4
#define DIJOFS_Z 8
#define DIJOFS_RX 12
#define DIJOFS_RY 16
#define DIJOFS_RZ 20
#define DIJOFS_SLIDER(n) (24 + (n) * 4)
#define DIJOFS_POV(n) (32 + (n) * 4)
#define DIJOFS_BUTTON(n) (48 + (n))

typedef struct DIENVELOPE {
    DWORD dwSize;
    DWORD dwAttackLevel;
    DWORD dwAttackTime;
    DWORD dwFadeLevel;
    DWORD dwFadeTime;
} DIENVELOPE, *LPDIENVELOPE;
typedef const DIENVELOPE *LPCDIENVELOPE;

typedef struct DIEFFECT_DX5 {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDuration;
    DWORD dwSamplePeriod;
    DWORD dwGain;
    DWORD dwTriggerButton;
    DWORD dwTriggerRepeatInterval;
    DWORD cAxes;
    LPDWORD rgdwAxes;
    LPLONG rglDirection;
    LPDIENVELOPE lpEnvelope;
    DWORD cbTypeSpecificParams;
    LPVOID lpvTypeSpecificParams;
} DIEFFECT_DX5, *LPDIEFFECT_DX5;

typedef struct DIEFFECT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDuration;
    DWORD dwSamplePeriod;
    DWORD dwGain;
    DWORD dwTriggerButton;
    DWORD dwTriggerRepeatInterval;
    DWORD cAxes;
    LPDWORD rgdwAxes;
    LPLONG rglDirection;
    LPDIENVELOPE lpEnvelope;
    DWORD cbTypeSpecificParams;
    LPVOID lpvTypeSpecificParams;
    DWORD dwStartDelay;
} DIEFFECT, *LPDIEFFECT;
typedef const DIEFFECT *LPCDIEFFECT;

typedef struct DICONSTANTFORCE {
    LONG lMagnitude;
} DICONSTANTFORCE, *LPDICONSTANTFORCE;

typedef struct DIRAMPFORCE {
    LONG lStart;
    LONG lEnd;
} DIRAMPFORCE, *LPDIRAMPFORCE;

typedef struct DIPERIODIC {
    DWORD dwMagnitude;
    LONG lOffset;
    DWORD dwPhase;
    DWORD dwPeriod;
} DIPERIODIC, *LPDIPERIODIC;

typedef struct DICONDITION {
    LONG lOffset;
    LONG lPositiveCoefficient;
    LONG lNegativeCoefficient;
    DWORD dwPositiveSaturation;
    DWORD dwNegativeSaturation;
    LONG lDeadBand;
} DICONDITION, *LPDICONDITION;

typedef struct DICUSTOMFORCE {
    DWORD cChannels;
    DWORD dwSamplePeriod;
    DWORD cSamples;
    LPLONG rglForceData;
} DICUSTOMFORCE, *LPDICUSTOMFORCE;

typedef struct DIEFFESCAPE {
    DWORD dwSize;
    DWORD dwCommand;
    LPVOID lpvInBuffer;
    DWORD cbInBuffer;
    LPVOID lpvOutBuffer;
    DWORD cbOutBuffer;
} DIEFFESCAPE, *LPDIEFFESCAPE;

typedef struct DIEFFECTINFOA {
    DWORD dwSize;
    GUID guid;
    DWORD dwEffType;
    DWORD dwStaticParams;
    DWORD dwDynamicParams;
    CHAR tszName[MAX_PATH];
} DIEFFECTINFOA, *LPDIEFFECTINFOA;
typedef const DIEFFECTINFOA *LPCDIEFFECTINFOA;

typedef struct DIEFFECTINFOW {
    DWORD dwSize;
    GUID guid;
    DWORD dwEffType;
    DWORD dwStaticParams;
    DWORD dwDynamicParams;
    WCHAR tszName[MAX_PATH];
} DIEFFECTINFOW, *LPDIEFFECTINFOW;
typedef const DIEFFECTINFOW *LPCDIEFFECTINFOW;

typedef struct DIFILEEFFECT {
    DWORD dwSize;
    GUID GuidEffect;
    LPCDIEFFECT lpDiEffect;
    CHAR szFriendlyName[MAX_PATH];
} DIFILEEFFECT, *LPDIFILEEFFECT;
typedef const DIFILEEFFECT *LPCDIFILEEFFECT;

typedef struct _DIACTIONA {
    UINT_PTR uAppData;
    DWORD dwSemantic;
    DWORD dwFlags;
    union {
        LPCSTR lptszActionName;
        UINT uResIdString;
    };
    GUID guidInstance;
    DWORD dwObjID;
    DWORD dwHow;
} DIACTIONA, *LPDIACTIONA;
typedef const DIACTIONA *LPCDIACTIONA;

typedef struct _DIACTIONW {
    UINT_PTR uAppData;
    DWORD dwSemantic;
    DWORD dwFlags;
    union {
        LPCWSTR lptszActionName;
        UINT uResIdString;
    };
    GUID guidInstance;
    DWORD dwObjID;
    DWORD dwHow;
} DIACTIONW, *LPDIACTIONW;
typedef const DIACTIONW *LPCDIACTIONW;

typedef struct _DIACTIONFORMATA {
    DWORD dwSize;
    DWORD dwActionSize;
    DWORD dwDataSize;
    DWORD dwNumActions;
    LPDIACTIONA rgoAction;
    GUID guidActionMap;
    DWORD dwGenre;
    DWORD dwBufferSize;
    LONG lAxisMin;
    LONG lAxisMax;
    HINSTANCE hInstString;
    FILETIME ftTimeStamp;
    DWORD dwCRC;
    CHAR tszActionMap[MAX_PATH];
} DIACTIONFORMATA, *LPDIACTIONFORMATA;
typedef const DIACTIONFORMATA *LPCDIACTIONFORMATA;

typedef struct _DIACTIONFORMATW {
    DWORD dwSize;
    DWORD dwActionSize;
    DWORD dwDataSize;
    DWORD dwNumActions;
    LPDIACTIONW rgoAction;
    GUID guidActionMap;
    DWORD dwGenre;
    DWORD dwBufferSize;
    LONG lAxisMin;
    LONG lAxisMax;
    HINSTANCE hInstString;
    FILETIME ftTimeStamp;
    DWORD dwCRC;
    WCHAR tszActionMap[MAX_PATH];
} DIACTIONFORMATW, *LPDIACTIONFORMATW;
typedef const DIACTIONFORMATW *LPCDIACTIONFORMATW;

typedef DWORD D3DCOLOR;

typedef struct _DICOLORSET {
    DWORD dwSize;
    D3DCOLOR cTextFore;
    D3DCOLOR cTextHighlight;
    D3DCOLOR cCalloutLine;
    D3DCOLOR cCalloutHighlight;
    D3DCOLOR cBorder;
    D3DCOLOR cControlFill;
    D3DCOLOR cHighlightFill;
    D3DCOLOR cAreaFill;
} DICOLORSET, *LPDICOLORSET;
typedef const DICOLORSET *LPCDICOLORSET;

typedef struct _DICONFIGUREDEVICESPARAMSA {
    DWORD dwSize;
    DWORD dwcUsers;
    LPSTR lptszUserNames;
    DWORD dwcFormats;
    LPDIACTIONFORMATA lprgFormats;
    HWND hwnd;
    DICOLORSET dics;
    IUnknown *lpUnkDDSTarget;
} DICONFIGUREDEVICESPARAMSA, *LPDICONFIGUREDEVICESPARAMSA;
typedef const DICONFIGUREDEVICESPARAMSA *LPCDICONFIGUREDEVICESPARAMSA;

typedef struct _DICONFIGUREDEVICESPARAMSW {
    DWORD dwSize;
    DWORD dwcUsers;
    LPWSTR lptszUserNames;
    DWORD dwcFormats;
    LPDIACTIONFORMATW lprgFormats;
    HWND hwnd;
    DICOLORSET dics;
    IUnknown *lpUnkDDSTarget;
} DICONFIGUREDEVICESPARAMSW, *LPDICONFIGUREDEVICESPARAMSW;
typedef const DICONFIGUREDEVICESPARAMSW *LPCDICONFIGUREDEVICESPARAMSW;

typedef struct _DIDEVICEIMAGEINFOA {
    CHAR tszImagePath[MAX_PATH];
    DWORD dwFlags;
    DWORD dwViewID;
    RECT rcOverlay;
    DWORD dwObjID;
    DWORD dwcValidPts;
    POINT rgptCalloutLine[5];
    RECT rcCalloutRect;
    DWORD dwTextAlign;
} DIDEVICEIMAGEINFOA, *LPDIDEVICEIMAGEINFOA;

typedef struct _DIDEVICEIMAGEINFOW {
    WCHAR tszImagePath[MAX_PATH];
    DWORD dwFlags;
    DWORD dwViewID;
    RECT rcOverlay;
    DWORD dwObjID;
    DWORD dwcValidPts;
    POINT rgptCalloutLine[5];
    RECT rcCalloutRect;
    DWORD dwTextAlign;
} DIDEVICEIMAGEINFOW, *LPDIDEVICEIMAGEINFOW;

typedef struct _DIDEVICEIMAGEINFOHEADERA {
    DWORD dwSize;
    DWORD dwSizeImageInfo;
    DWORD dwcViews;
    DWORD dwcButtons;
    DWORD dwcAxes;
    DWORD dwcPOVs;
    DWORD dwBufferSize;
    DWORD dwBufferUsed;
    LPDIDEVICEIMAGEINFOA lprgImageInfoArray;
} DIDEVICEIMAGEINFOHEADERA, *LPDIDEVICEIMAGEINFOHEADERA;

typedef struct _DIDEVICEIMAGEINFOHEADERW {
    DWORD dwSize;
    DWORD dwSizeImageInfo;
    DWORD dwcViews;
    DWORD dwcButtons;
    DWORD dwcAxes;
    DWORD dwcPOVs;
    DWORD dwBufferSize;
    DWORD dwBufferUsed;
    LPDIDEVICEIMAGEINFOW lprgImageInfoArray;
} DIDEVICEIMAGEINFOHEADERW, *LPDIDEVICEIMAGEINFOHEADERW;

/* ------------------------------------------------------------------ callbacks */

typedef BOOL(WINAPI *LPDIENUMDEVICESCALLBACKA)(LPCDIDEVICEINSTANCEA instance, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMDEVICESCALLBACKW)(LPCDIDEVICEINSTANCEW instance, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMDEVICEOBJECTSCALLBACKA)(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMDEVICEOBJECTSCALLBACKW)(LPCDIDEVICEOBJECTINSTANCEW object, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMEFFECTSCALLBACKA)(LPCDIEFFECTINFOA info, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMEFFECTSCALLBACKW)(LPCDIEFFECTINFOW info, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMCREATEDEFFECTOBJECTSCALLBACK)(LPDIRECTINPUTEFFECT effect, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMEFFECTSINFILECALLBACK)(LPCDIFILEEFFECT effect, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMDEVICESBYSEMANTICSCBA)(LPCDIDEVICEINSTANCEA instance, LPDIRECTINPUTDEVICE8A device,
                                                    DWORD flags, DWORD remaining, LPVOID context);
typedef BOOL(WINAPI *LPDIENUMDEVICESBYSEMANTICSCBW)(LPCDIDEVICEINSTANCEW instance, LPDIRECTINPUTDEVICE8W device,
                                                    DWORD flags, DWORD remaining, LPVOID context);
typedef BOOL(WINAPI *LPDICONFIGUREDEVICESCALLBACK)(IUnknown *target, LPVOID context);

/* ------------------------------------------------------------------ interfaces */

#define IDirectInputEffect_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, HINSTANCE instance, DWORD version, REFGUID guid);                 \
    HRESULT(STDMETHODCALLTYPE *GetEffectGuid)(T * This, LPGUID guid);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetParameters)(T * This, LPDIEFFECT effect, DWORD flags);                               \
    HRESULT(STDMETHODCALLTYPE *SetParameters)(T * This, LPCDIEFFECT effect, DWORD flags);                              \
    HRESULT(STDMETHODCALLTYPE *Start)(T * This, DWORD iterations, DWORD flags);                                        \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetEffectStatus)(T * This, LPDWORD status);                                             \
    HRESULT(STDMETHODCALLTYPE *Download)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Unload)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *Escape)(T * This, LPDIEFFESCAPE escape);

/* IDirectInputDevice methods for one character width. */
#define IDirectInputDevice_METHODS_FOR(T, OBJCB, OBJINST, DEVINST)                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCapabilities)(T * This, LPDIDEVCAPS caps);                                           \
    HRESULT(STDMETHODCALLTYPE *EnumObjects)(T * This, OBJCB callback, LPVOID context, DWORD flags);                    \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, REFGUID property, LPDIPROPHEADER header);                        \
    HRESULT(STDMETHODCALLTYPE *SetProperty)(T * This, REFGUID property, LPCDIPROPHEADER header);                       \
    HRESULT(STDMETHODCALLTYPE *Acquire)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Unacquire)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetDeviceState)(T * This, DWORD size, LPVOID data);                                     \
    HRESULT(STDMETHODCALLTYPE *GetDeviceData)(T * This, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count,         \
                                              DWORD flags);                                                            \
    HRESULT(STDMETHODCALLTYPE *SetDataFormat)(T * This, LPCDIDATAFORMAT format);                                      \
    HRESULT(STDMETHODCALLTYPE *SetEventNotification)(T * This, HANDLE event);                                          \
    HRESULT(STDMETHODCALLTYPE *SetCooperativeLevel)(T * This, HWND window, DWORD flags);                              \
    HRESULT(STDMETHODCALLTYPE *GetObjectInfo)(T * This, OBJINST object, DWORD id, DWORD how);                         \
    HRESULT(STDMETHODCALLTYPE *GetDeviceInfo)(T * This, DEVINST instance);                                             \
    HRESULT(STDMETHODCALLTYPE *RunControlPanel)(T * This, HWND owner, DWORD flags);                                   \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, HINSTANCE instance, DWORD version, REFGUID guid);

#define IDirectInputDevice2_METHODS_FOR(T, OBJCB, OBJINST, DEVINST, EFFCB, EFFINFO)                                    \
    IDirectInputDevice_METHODS_FOR(T, OBJCB, OBJINST, DEVINST)                                                         \
    HRESULT(STDMETHODCALLTYPE *CreateEffect)(T * This, REFGUID guid, LPCDIEFFECT effect, LPDIRECTINPUTEFFECT * out,   \
                                             LPUNKNOWN outer);                                                         \
    HRESULT(STDMETHODCALLTYPE *EnumEffects)(T * This, EFFCB callback, LPVOID context, DWORD type);                     \
    HRESULT(STDMETHODCALLTYPE *GetEffectInfo)(T * This, EFFINFO info, REFGUID guid);                                   \
    HRESULT(STDMETHODCALLTYPE *GetForceFeedbackState)(T * This, LPDWORD state);                                        \
    HRESULT(STDMETHODCALLTYPE *SendForceFeedbackCommand)(T * This, DWORD flags);                                       \
    HRESULT(STDMETHODCALLTYPE *EnumCreatedEffectObjects)(T * This, LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,      \
                                                         LPVOID context, DWORD flags);                                 \
    HRESULT(STDMETHODCALLTYPE *Escape)(T * This, LPDIEFFESCAPE escape);                                                \
    HRESULT(STDMETHODCALLTYPE *Poll)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *SendDeviceData)(T * This, DWORD size, LPCDIDEVICEOBJECTDATA data, LPDWORD count,       \
                                               DWORD flags);

#define IDirectInputDevice7_METHODS_FOR(T, OBJCB, OBJINST, DEVINST, EFFCB, EFFINFO, STR)                               \
    IDirectInputDevice2_METHODS_FOR(T, OBJCB, OBJINST, DEVINST, EFFCB, EFFINFO)                                        \
    HRESULT(STDMETHODCALLTYPE *EnumEffectsInFile)(T * This, STR file, LPDIENUMEFFECTSINFILECALLBACK callback,          \
                                                  LPVOID context, DWORD flags);                                        \
    HRESULT(STDMETHODCALLTYPE *WriteEffectToFile)(T * This, STR file, DWORD count, LPDIFILEEFFECT effects,            \
                                                  DWORD flags);

#define IDirectInputDevice8_METHODS_FOR(T, OBJCB, OBJINST, DEVINST, EFFCB, EFFINFO, STR, FORMAT, IMAGE)                \
    IDirectInputDevice7_METHODS_FOR(T, OBJCB, OBJINST, DEVINST, EFFCB, EFFINFO, STR)                                   \
    HRESULT(STDMETHODCALLTYPE *BuildActionMap)(T * This, FORMAT format, STR user, DWORD flags);                        \
    HRESULT(STDMETHODCALLTYPE *SetActionMap)(T * This, FORMAT format, STR user, DWORD flags);                          \
    HRESULT(STDMETHODCALLTYPE *GetImageInfo)(T * This, IMAGE header);

#define IDirectInputDeviceA_METHODS(T)                                                                                 \
    IDirectInputDevice_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKA, LPDIDEVICEOBJECTINSTANCEA, LPDIDEVICEINSTANCEA)
#define IDirectInputDeviceW_METHODS(T)                                                                                 \
    IDirectInputDevice_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKW, LPDIDEVICEOBJECTINSTANCEW, LPDIDEVICEINSTANCEW)
#define IDirectInputDevice2A_METHODS(T)                                                                                \
    IDirectInputDevice2_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKA, LPDIDEVICEOBJECTINSTANCEA,                      \
                                    LPDIDEVICEINSTANCEA, LPDIENUMEFFECTSCALLBACKA, LPDIEFFECTINFOA)
#define IDirectInputDevice2W_METHODS(T)                                                                                \
    IDirectInputDevice2_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKW, LPDIDEVICEOBJECTINSTANCEW,                      \
                                    LPDIDEVICEINSTANCEW, LPDIENUMEFFECTSCALLBACKW, LPDIEFFECTINFOW)
#define IDirectInputDevice7A_METHODS(T)                                                                                \
    IDirectInputDevice7_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKA, LPDIDEVICEOBJECTINSTANCEA,                      \
                                    LPDIDEVICEINSTANCEA, LPDIENUMEFFECTSCALLBACKA, LPDIEFFECTINFOA, LPCSTR)
#define IDirectInputDevice7W_METHODS(T)                                                                                \
    IDirectInputDevice7_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKW, LPDIDEVICEOBJECTINSTANCEW,                      \
                                    LPDIDEVICEINSTANCEW, LPDIENUMEFFECTSCALLBACKW, LPDIEFFECTINFOW, LPCWSTR)
#define IDirectInputDevice8A_METHODS(T)                                                                                \
    IDirectInputDevice8_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKA, LPDIDEVICEOBJECTINSTANCEA,                      \
                                    LPDIDEVICEINSTANCEA, LPDIENUMEFFECTSCALLBACKA, LPDIEFFECTINFOA, LPCSTR,            \
                                    LPDIACTIONFORMATA, LPDIDEVICEIMAGEINFOHEADERA)
#define IDirectInputDevice8W_METHODS(T)                                                                                \
    IDirectInputDevice8_METHODS_FOR(T, LPDIENUMDEVICEOBJECTSCALLBACKW, LPDIDEVICEOBJECTINSTANCEW,                      \
                                    LPDIDEVICEINSTANCEW, LPDIENUMEFFECTSCALLBACKW, LPDIEFFECTINFOW, LPCWSTR,           \
                                    LPDIACTIONFORMATW, LPDIDEVICEIMAGEINFOHEADERW)

#define IDirectInput_METHODS_FOR(T, DEVICE, DEVCB)                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, REFGUID guid, DEVICE * device, LPUNKNOWN outer);               \
    HRESULT(STDMETHODCALLTYPE *EnumDevices)(T * This, DWORD type, DEVCB callback, LPVOID context, DWORD flags);       \
    HRESULT(STDMETHODCALLTYPE *GetDeviceStatus)(T * This, REFGUID guid);                                               \
    HRESULT(STDMETHODCALLTYPE *RunControlPanel)(T * This, HWND owner, DWORD flags);                                   \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, HINSTANCE instance, DWORD version);

#define IDirectInput2_METHODS_FOR(T, DEVICE, DEVCB, STR)                                                               \
    IDirectInput_METHODS_FOR(T, DEVICE, DEVCB)                                                                         \
    HRESULT(STDMETHODCALLTYPE *FindDevice)(T * This, REFGUID class_guid, STR name, LPGUID instance);

#define IDirectInputA_METHODS(T) IDirectInput_METHODS_FOR(T, LPDIRECTINPUTDEVICEA, LPDIENUMDEVICESCALLBACKA)
#define IDirectInputW_METHODS(T) IDirectInput_METHODS_FOR(T, LPDIRECTINPUTDEVICEW, LPDIENUMDEVICESCALLBACKW)
#define IDirectInput2A_METHODS(T) IDirectInput2_METHODS_FOR(T, LPDIRECTINPUTDEVICEA, LPDIENUMDEVICESCALLBACKA, LPCSTR)
#define IDirectInput2W_METHODS(T) IDirectInput2_METHODS_FOR(T, LPDIRECTINPUTDEVICEW, LPDIENUMDEVICESCALLBACKW, LPCWSTR)
#define IDirectInput7A_METHODS(T)                                                                                      \
    IDirectInput2A_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceEx)(T * This, REFGUID guid, REFIID riid, LPVOID * out, LPUNKNOWN outer);
#define IDirectInput7W_METHODS(T)                                                                                      \
    IDirectInput2W_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceEx)(T * This, REFGUID guid, REFIID riid, LPVOID * out, LPUNKNOWN outer);
#define IDirectInput8A_METHODS(T)                                                                                      \
    IDirectInput2_METHODS_FOR(T, LPDIRECTINPUTDEVICE8A, LPDIENUMDEVICESCALLBACKA, LPCSTR)                              \
    HRESULT(STDMETHODCALLTYPE *EnumDevicesBySemantics)(T * This, LPCSTR user, LPDIACTIONFORMATA format,               \
                                                       LPDIENUMDEVICESBYSEMANTICSCBA callback, LPVOID context,         \
                                                       DWORD flags);                                                   \
    HRESULT(STDMETHODCALLTYPE *ConfigureDevices)(T * This, LPDICONFIGUREDEVICESCALLBACK callback,                      \
                                                 LPDICONFIGUREDEVICESPARAMSA params, DWORD flags, LPVOID context);
#define IDirectInput8W_METHODS(T)                                                                                      \
    IDirectInput2_METHODS_FOR(T, LPDIRECTINPUTDEVICE8W, LPDIENUMDEVICESCALLBACKW, LPCWSTR)                             \
    HRESULT(STDMETHODCALLTYPE *EnumDevicesBySemantics)(T * This, LPCWSTR user, LPDIACTIONFORMATW format,              \
                                                       LPDIENUMDEVICESBYSEMANTICSCBW callback, LPVOID context,         \
                                                       DWORD flags);                                                   \
    HRESULT(STDMETHODCALLTYPE *ConfigureDevices)(T * This, LPDICONFIGUREDEVICESCALLBACK callback,                      \
                                                 LPDICONFIGUREDEVICESPARAMSW params, DWORD flags, LPVOID context);

MIME_COM_INTERFACE(IDirectInputEffect)
MIME_COM_INTERFACE(IDirectInputDeviceA)
MIME_COM_INTERFACE(IDirectInputDeviceW)
MIME_COM_INTERFACE(IDirectInputDevice2A)
MIME_COM_INTERFACE(IDirectInputDevice2W)
MIME_COM_INTERFACE(IDirectInputDevice7A)
MIME_COM_INTERFACE(IDirectInputDevice7W)
MIME_COM_INTERFACE(IDirectInputDevice8A)
MIME_COM_INTERFACE(IDirectInputDevice8W)
MIME_COM_INTERFACE(IDirectInputA)
MIME_COM_INTERFACE(IDirectInputW)
MIME_COM_INTERFACE(IDirectInput2A)
MIME_COM_INTERFACE(IDirectInput2W)
MIME_COM_INTERFACE(IDirectInput7A)
MIME_COM_INTERFACE(IDirectInput7W)
MIME_COM_INTERFACE(IDirectInput8A)
MIME_COM_INTERFACE(IDirectInput8W)

/* ------------------------------------------------------------------ standard data formats, from the import libs */

extern const DIDATAFORMAT c_dfDIMouse;
extern const DIDATAFORMAT c_dfDIMouse2;
extern const DIDATAFORMAT c_dfDIKeyboard;
extern const DIDATAFORMAT c_dfDIJoystick;
extern const DIDATAFORMAT c_dfDIJoystick2;

/* ------------------------------------------------------------------ keyboard scan codes */

#define DIK_ESCAPE 0x01
#define DIK_1 0x02
#define DIK_2 0x03
#define DIK_3 0x04
#define DIK_4 0x05
#define DIK_5 0x06
#define DIK_6 0x07
#define DIK_7 0x08
#define DIK_8 0x09
#define DIK_9 0x0A
#define DIK_0 0x0B
#define DIK_MINUS 0x0C
#define DIK_EQUALS 0x0D
#define DIK_BACK 0x0E
#define DIK_TAB 0x0F
#define DIK_Q 0x10
#define DIK_W 0x11
#define DIK_E 0x12
#define DIK_R 0x13
#define DIK_T 0x14
#define DIK_Y 0x15
#define DIK_U 0x16
#define DIK_I 0x17
#define DIK_O 0x18
#define DIK_P 0x19
#define DIK_LBRACKET 0x1A
#define DIK_RBRACKET 0x1B
#define DIK_RETURN 0x1C
#define DIK_LCONTROL 0x1D
#define DIK_A 0x1E
#define DIK_S 0x1F
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_G 0x22
#define DIK_H 0x23
#define DIK_J 0x24
#define DIK_K 0x25
#define DIK_L 0x26
#define DIK_SEMICOLON 0x27
#define DIK_APOSTROPHE 0x28
#define DIK_GRAVE 0x29
#define DIK_LSHIFT 0x2A
#define DIK_BACKSLASH 0x2B
#define DIK_Z 0x2C
#define DIK_X 0x2D
#define DIK_C 0x2E
#define DIK_V 0x2F
#define DIK_B 0x30
#define DIK_N 0x31
#define DIK_M 0x32
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34
#define DIK_SLASH 0x35
#define DIK_RSHIFT 0x36
#define DIK_MULTIPLY 0x37
#define DIK_LMENU 0x38
#define DIK_SPACE 0x39
#define DIK_CAPITAL 0x3A
#define DIK_F1 0x3B
#define DIK_F2 0x3C
#define DIK_F3 0x3D
#define DIK_F4 0x3E
#define DIK_F5 0x3F
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_NUMLOCK 0x45
#define DIK_SCROLL 0x46
#define DIK_NUMPAD7 0x47
#define DIK_NUMPAD8 0x48
#define DIK_NUMPAD9 0x49
#define DIK_SUBTRACT 0x4A
#define DIK_NUMPAD4 0x4B
#define DIK_NUMPAD5 0x4C
#define DIK_NUMPAD6 0x4D
#define DIK_ADD 0x4E
#define DIK_NUMPAD1 0x4F
#define DIK_NUMPAD2 0x50
#define DIK_NUMPAD3 0x51
#define DIK_NUMPAD0 0x52
#define DIK_DECIMAL 0x53
#define DIK_OEM_102 0x56
#define DIK_F11 0x57
#define DIK_F12 0x58
#define DIK_F13 0x64
#define DIK_F14 0x65
#define DIK_F15 0x66
#define DIK_KANA 0x70
#define DIK_ABNT_C1 0x73
#define DIK_CONVERT 0x79
#define DIK_NOCONVERT 0x7B
#define DIK_YEN 0x7D
#define DIK_ABNT_C2 0x7E
#define DIK_NUMPADEQUALS 0x8D
#define DIK_PREVTRACK 0x90
#define DIK_AT 0x91
#define DIK_COLON 0x92
#define DIK_UNDERLINE 0x93
#define DIK_KANJI 0x94
#define DIK_STOP 0x95
#define DIK_AX 0x96
#define DIK_UNLABELED 0x97
#define DIK_NEXTTRACK 0x99
#define DIK_NUMPADENTER 0x9C
#define DIK_RCONTROL 0x9D
#define DIK_MUTE 0xA0
#define DIK_CALCULATOR 0xA1
#define DIK_PLAYPAUSE 0xA2
#define DIK_MEDIASTOP 0xA4
#define DIK_VOLUMEDOWN 0xAE
#define DIK_VOLUMEUP 0xB0
#define DIK_WEBHOME 0xB2
#define DIK_NUMPADCOMMA 0xB3
#define DIK_DIVIDE 0xB5
#define DIK_SYSRQ 0xB7
#define DIK_RMENU 0xB8
#define DIK_PAUSE 0xC5
#define DIK_HOME 0xC7
#define DIK_UP 0xC8
#define DIK_PRIOR 0xC9
#define DIK_LEFT 0xCB
#define DIK_RIGHT 0xCD
#define DIK_END 0xCF
#define DIK_DOWN 0xD0
#define DIK_NEXT 0xD1
#define DIK_INSERT 0xD2
#define DIK_DELETE 0xD3
#define DIK_LWIN 0xDB
#define DIK_RWIN 0xDC
#define DIK_APPS 0xDD
#define DIK_POWER 0xDE
#define DIK_SLEEP 0xDF
#define DIK_WAKE 0xE3
#define DIK_WEBSEARCH 0xE5
#define DIK_WEBFAVORITES 0xE6
#define DIK_WEBREFRESH 0xE7
#define DIK_WEBSTOP 0xE8
#define DIK_WEBFORWARD 0xE9
#define DIK_WEBBACK 0xEA
#define DIK_MYCOMPUTER 0xEB
#define DIK_MAIL 0xEC
#define DIK_MEDIASELECT 0xED
#define DIK_BACKSPACE DIK_BACK
#define DIK_NUMPADSTAR DIK_MULTIPLY
#define DIK_LALT DIK_LMENU
#define DIK_CAPSLOCK DIK_CAPITAL
#define DIK_NUMPADMINUS DIK_SUBTRACT
#define DIK_NUMPADPLUS DIK_ADD
#define DIK_NUMPADPERIOD DIK_DECIMAL
#define DIK_NUMPADSLASH DIK_DIVIDE
#define DIK_RALT DIK_RMENU
#define DIK_UPARROW DIK_UP
#define DIK_PGUP DIK_PRIOR
#define DIK_LEFTARROW DIK_LEFT
#define DIK_RIGHTARROW DIK_RIGHT
#define DIK_DOWNARROW DIK_DOWN
#define DIK_PGDN DIK_NEXT

#endif
