/*
 * the device installation functions programs enumerate devices with.
 */
#ifndef MIME_WIN32_SETUPAPI_H
#define MIME_WIN32_SETUPAPI_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_SETUPAPI
#define WINSETUPAPI __declspec(dllexport)
#else
#define WINSETUPAPI DECLSPEC_IMPORT
#endif

typedef PVOID HDEVINFO;

#define DIGCF_DEFAULT 0x00000001
#define DIGCF_PRESENT 0x00000002
#define DIGCF_ALLCLASSES 0x00000004
#define DIGCF_PROFILE 0x00000008
#define DIGCF_DEVICEINTERFACE 0x00000010

typedef struct _SP_DEVINFO_DATA {
    DWORD cbSize;
    GUID ClassGuid;
    DWORD DevInst;
    ULONG_PTR Reserved;
} SP_DEVINFO_DATA, *PSP_DEVINFO_DATA;

typedef struct _SP_DEVICE_INTERFACE_DATA {
    DWORD cbSize;
    GUID InterfaceClassGuid;
    DWORD Flags;
    ULONG_PTR Reserved;
} SP_DEVICE_INTERFACE_DATA, *PSP_DEVICE_INTERFACE_DATA;

typedef struct _SP_DEVICE_INTERFACE_DETAIL_DATA_W {
    DWORD cbSize;
    WCHAR DevicePath[1];
} SP_DEVICE_INTERFACE_DETAIL_DATA_W, *PSP_DEVICE_INTERFACE_DETAIL_DATA_W;

typedef struct _SP_DEVICE_INTERFACE_DETAIL_DATA_A {
    DWORD cbSize;
    CHAR DevicePath[1];
} SP_DEVICE_INTERFACE_DETAIL_DATA_A, *PSP_DEVICE_INTERFACE_DETAIL_DATA_A;

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsW(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags);
WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsA(const GUID *guid, PCSTR enumerator, HWND parent, DWORD flags);
WINSETUPAPI BOOL WINAPI SetupDiDestroyDeviceInfoList(HDEVINFO set);
WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, PSP_DEVINFO_DATA data);
WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA device, const GUID *guid,
                                                    DWORD index, PSP_DEVICE_INTERFACE_DATA data);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required);

#endif
