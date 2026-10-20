/*
 * the device installation functions programs enumerate devices with.
 */
#ifndef MIME_WIN32_SETUPAPI_H
#define MIME_WIN32_SETUPAPI_H

#include <mime/win32/winbase.h>
#include <mime/win32/cfgmgr32.h>

#ifdef MIME_BUILD_SETUPAPI
#define WINSETUPAPI DECLSPEC_EXPORT
#else
#define WINSETUPAPI DECLSPEC_IMPORT
#endif

typedef PVOID HDEVINFO;
typedef UINT DI_FUNCTION;

#define DIGCF_DEFAULT 0x00000001
#define DIGCF_PRESENT 0x00000002
#define DIGCF_ALLCLASSES 0x00000004
#define DIGCF_PROFILE 0x00000008
#define DIGCF_DEVICEINTERFACE 0x00000010

#define SPINT_ACTIVE 0x00000001
#define SPINT_DEFAULT 0x00000002
#define SPINT_REMOVED 0x00000004

#define SPDRP_DEVICEDESC 0x00000000
#define SPDRP_HARDWAREID 0x00000001
#define SPDRP_COMPATIBLEIDS 0x00000002
#define SPDRP_SERVICE 0x00000004
#define SPDRP_CLASS 0x00000007
#define SPDRP_CLASSGUID 0x00000008
#define SPDRP_DRIVER 0x00000009
#define SPDRP_CONFIGFLAGS 0x0000000A
#define SPDRP_MFG 0x0000000B
#define SPDRP_FRIENDLYNAME 0x0000000C
#define SPDRP_LOCATION_INFORMATION 0x0000000D
#define SPDRP_PHYSICAL_DEVICE_OBJECT_NAME 0x0000000E
#define SPDRP_ENUMERATOR_NAME 0x00000016
#define SPDRP_MAXIMUM_PROPERTY 0x00000025

#define DICS_FLAG_GLOBAL 0x00000001
#define DICS_FLAG_CONFIGSPECIFIC 0x00000002
#define DIREG_DEV 0x00000001
#define DIREG_DRV 0x00000002
#define DIREG_BOTH 0x00000004

#define ERROR_INVALID_MACHINENAME 1210
#define ERROR_KEY_DOES_NOT_EXIST 0xE0000204
#define ERROR_INVALID_DEVINST_NAME 0xE0000205
#define ERROR_INVALID_REG_PROPERTY 0xE0000209
#define ERROR_NO_SUCH_DEVINST 0xE000020B
#define ERROR_NO_SUCH_DEVICE_INTERFACE 0xE0000225
#define ERROR_CLASS_MISMATCH 0xE0000201
#define DIODI_NO_ADD 0x00000001

/* windows packs these to 1 byte on x86, to 8 on x64. */
#ifndef _WIN64
#pragma pack(push, 1)
#endif

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

#ifndef _WIN64
#pragma pack(pop)
#endif

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsW(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags);
WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsA(const GUID *guid, PCSTR enumerator, HWND parent, DWORD flags);
WINSETUPAPI BOOL WINAPI SetupDiClassGuidsFromNameA(PCSTR name, GUID *list, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiClassGuidsFromNameW(PCWSTR name, GUID *list, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiClassGuidsFromNameExA(PCSTR name, GUID *list, DWORD size, PDWORD required, PCSTR machine,
                                                    PVOID reserved);
WINSETUPAPI BOOL WINAPI SetupDiClassGuidsFromNameExW(PCWSTR name, GUID *list, DWORD size, PDWORD required, PCWSTR machine,
                                                    PVOID reserved);
WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsExW(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags,
                                                   HDEVINFO set, PCWSTR machine, PVOID reserved);
WINSETUPAPI HDEVINFO WINAPI SetupDiCreateDeviceInfoList(const GUID *guid, HWND parent);
WINSETUPAPI BOOL WINAPI SetupDiDestroyDeviceInfoList(HDEVINFO set);
WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, PSP_DEVINFO_DATA data);
WINSETUPAPI BOOL WINAPI SetupDiOpenDeviceInfoW(HDEVINFO set, PCWSTR instance, HWND parent, DWORD flags,
                                               PSP_DEVINFO_DATA data);
WINSETUPAPI BOOL WINAPI SetupDiOpenDeviceInfoA(HDEVINFO set, PCSTR instance, HWND parent, DWORD flags,
                                               PSP_DEVINFO_DATA data);
WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA device, const GUID *guid,
                                                    DWORD index, PSP_DEVICE_INTERFACE_DATA data);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInstanceIdW(HDEVINFO set, PSP_DEVINFO_DATA device, PWSTR buffer, DWORD size,
                                                    PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInstanceIdA(HDEVINFO set, PSP_DEVINFO_DATA device, PSTR buffer, DWORD size,
                                                    PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupDiSetDeviceRegistryPropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          const BYTE *buffer, DWORD size);
WINSETUPAPI BOOL WINAPI SetupDiGetDevicePropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, const DEVPROPKEY *key,
                                                  DEVPROPTYPE *type, PBYTE buffer, DWORD size, PDWORD required,
                                                  DWORD flags);
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfacePropertyW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                           const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer,
                                                           DWORD size, PDWORD required, DWORD flags);
WINSETUPAPI HKEY WINAPI SetupDiOpenDevRegKey(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD scope, DWORD profile,
                                             DWORD type, REGSAM access);
WINSETUPAPI HKEY WINAPI SetupDiCreateDevRegKeyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD scope, DWORD profile,
                                                DWORD type, PVOID inf, PCWSTR section);
WINSETUPAPI HKEY WINAPI SetupDiCreateDeviceInterfaceRegKeyW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                            DWORD reserved, REGSAM access, PVOID inf, PCWSTR section);
WINSETUPAPI HKEY WINAPI SetupDiOpenDeviceInterfaceRegKey(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface, DWORD reserved,
                                                         REGSAM access);
WINSETUPAPI BOOL WINAPI SetupDiCallClassInstaller(DI_FUNCTION function, HDEVINFO set, PSP_DEVINFO_DATA device);

#endif
