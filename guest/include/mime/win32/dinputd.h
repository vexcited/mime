/* directinput joystick configuration and the legacy joystick registry structures winmm shares. */
#ifndef MIME_WIN32_DINPUTD_H
#define MIME_WIN32_DINPUTD_H

#include <mime/win32/dinput.h>

#define MAX_JOYSTRING 256

typedef struct joypos_tag {
    DWORD dwX, dwY, dwZ, dwR, dwU, dwV;
} JOYPOS, *LPJOYPOS;

typedef struct joyrange_tag {
    JOYPOS jpMin;
    JOYPOS jpMax;
    JOYPOS jpCenter;
} JOYRANGE, *LPJOYRANGE;

typedef struct joyreghwsettings_tag {
    DWORD dwFlags;
    DWORD dwNumButtons;
} JOYREGHWSETTINGS, *LPJOYHWSETTINGS;

typedef struct joyreghwvalues_tag {
    JOYRANGE jrvHardware;
    DWORD dwPOVValues[4];
    DWORD dwCalFlags;
} JOYREGHWVALUES, *LPJOYREGHWVALUES;

typedef struct joyreghwconfig_tag {
    JOYREGHWSETTINGS hws;
    DWORD dwUsageSettings;
    JOYREGHWVALUES hwv;
    DWORD dwType;
    DWORD dwReserved;
} JOYREGHWCONFIG, *LPJOYREGHWCONFIG;

typedef struct joyreguservalues_tag {
    DWORD dwTimeOut;
    JOYRANGE jrvRanges;
    JOYPOS jpDeadZone;
} JOYREGUSERVALUES, *LPJOYREGUSERVALUES;

#define JOY_HWS_HASZ 0x00000001
#define JOY_HWS_HASPOV 0x00000002
#define JOY_HWS_ISGAMEPAD 0x00000020
#define JOY_HWS_ISCARCTRL 0x00000040
#define JOY_HWS_HASR 0x00080000
#define JOY_HWS_HASU 0x00800000
#define JOY_HWS_HASV 0x01000000

#define JOY_US_HASRUDDER 0x00000001
#define JOY_US_PRESENT 0x00000002
#define JOY_US_ISOEM 0x00000004
#define JOY_US_VOLATILE 0x00000008

#define JOY_HW_NONE 0
#define JOY_HW_LASTENTRY 13

typedef struct DIJOYCONFIG_DX5 {
    DWORD dwSize;
    GUID guidInstance;
    JOYREGHWCONFIG hwc;
    DWORD dwGain;
    WCHAR wszType[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTRING];
} DIJOYCONFIG_DX5, *LPDIJOYCONFIG_DX5;

typedef struct DIJOYCONFIG {
    DWORD dwSize;
    GUID guidInstance;
    JOYREGHWCONFIG hwc;
    DWORD dwGain;
    WCHAR wszType[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTRING];
    GUID guidGameport;
} DIJOYCONFIG, *LPDIJOYCONFIG;
typedef const DIJOYCONFIG *LPCDIJOYCONFIG;

typedef struct DIJOYUSERVALUES {
    DWORD dwSize;
    JOYREGUSERVALUES ruv;
    WCHAR wszGlobalDriver[MAX_JOYSTRING];
    WCHAR wszGameportEmulator[MAX_JOYSTRING];
} DIJOYUSERVALUES, *LPDIJOYUSERVALUES;
typedef const DIJOYUSERVALUES *LPCDIJOYUSERVALUES;

#define DIJC_GUIDINSTANCE 0x00000001
#define DIJC_REGHWCONFIGTYPE 0x00000002
#define DIJC_GAIN 0x00000004
#define DIJC_CALLOUT 0x00000008
#define DIJC_WDMGAMEPORT 0x00000010

#define DIJU_USERVALUES 0x00000001
#define DIJU_GLOBALDRIVER 0x00000002
#define DIJU_GAMEPORTEMULATOR 0x00000004

typedef BOOL(CALLBACK *LPDIJOYTYPECALLBACK)(LPCWSTR type, LPVOID context);

typedef struct IDirectInputJoyConfig IDirectInputJoyConfig, *LPDIRECTINPUTJOYCONFIG;

/* type information structures are passed through as is. */
#define IDirectInputJoyConfig_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Acquire)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Unacquire)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *SetCooperativeLevel)(T * This, HWND window, DWORD flags);                              \
    HRESULT(STDMETHODCALLTYPE *SendNotify)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *EnumTypes)(T * This, LPDIJOYTYPECALLBACK callback, LPVOID context);                    \
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(T * This, LPCWSTR type, void *info, DWORD flags);                         \
    HRESULT(STDMETHODCALLTYPE *SetTypeInfo)(T * This, LPCWSTR type, const void *info, DWORD flags);                   \
    HRESULT(STDMETHODCALLTYPE *DeleteType)(T * This, LPCWSTR type);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetConfig)(T * This, UINT id, LPDIJOYCONFIG config, DWORD flags);                      \
    HRESULT(STDMETHODCALLTYPE *SetConfig)(T * This, UINT id, LPCDIJOYCONFIG config, DWORD flags);                     \
    HRESULT(STDMETHODCALLTYPE *DeleteConfig)(T * This, UINT id);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetUserValues)(T * This, LPDIJOYUSERVALUES values, DWORD flags);                       \
    HRESULT(STDMETHODCALLTYPE *SetUserValues)(T * This, LPCDIJOYUSERVALUES values, DWORD flags);                      \
    HRESULT(STDMETHODCALLTYPE *AddNewHardware)(T * This, HWND owner, REFGUID klass);                                  \
    HRESULT(STDMETHODCALLTYPE *OpenTypeKey)(T * This, LPCWSTR type, DWORD access, HKEY *key);                         \
    HRESULT(STDMETHODCALLTYPE *OpenConfigKey)(T * This, UINT id, DWORD access, HKEY *key);

MIME_COM_INTERFACE(IDirectInputJoyConfig)

#endif
