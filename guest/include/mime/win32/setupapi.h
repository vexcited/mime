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

/* error codes of setupapi's own. */
#define ERROR_EXPECTED_SECTION_NAME 0x800F0000
#define ERROR_BAD_SECTION_NAME_LINE 0x800F0001
#define ERROR_SECTION_NAME_TOO_LONG 0x800F0002
#define ERROR_GENERAL_SYNTAX 0x800F0003
#define ERROR_WRONG_INF_STYLE 0x800F0100
#define ERROR_SECTION_NOT_FOUND 0x800F0101
#define ERROR_LINE_NOT_FOUND 0x800F0102


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
WINSETUPAPI BOOL WINAPI SetupDiSetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
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

/* ------------------------------------------------------------------ inf files, file queues, cabinets */

typedef PVOID HINF;
typedef PVOID HSPFILEQ;

/* windows packs these to 1 byte on x86, to 8 on x64. */
#ifndef _WIN64
#pragma pack(push, 1)
#endif

typedef struct _INFCONTEXT {
    PVOID Inf;
    PVOID CurrentInf;
    UINT Section;
    UINT Line;
} INFCONTEXT, *PINFCONTEXT;

typedef struct _SP_INF_INFORMATION {
    DWORD InfStyle;
    DWORD InfCount;
    BYTE VersionData[1];
} SP_INF_INFORMATION, *PSP_INF_INFORMATION;

typedef struct _FILEPATHS_A {
    PCSTR Target;
    PCSTR Source;
    UINT Win32Error;
    DWORD Flags;
} FILEPATHS_A, *PFILEPATHS_A;

typedef struct _FILEPATHS_W {
    PCWSTR Target;
    PCWSTR Source;
    UINT Win32Error;
    DWORD Flags;
} FILEPATHS_W, *PFILEPATHS_W;

typedef struct _CABINET_INFO_A {
    PCSTR CabinetPath;
    PCSTR CabinetFile;
    PCSTR DiskName;
    USHORT SetId;
    USHORT CabinetNumber;
} CABINET_INFO_A, *PCABINET_INFO_A;

typedef struct _CABINET_INFO_W {
    PCWSTR CabinetPath;
    PCWSTR CabinetFile;
    PCWSTR DiskName;
    USHORT SetId;
    USHORT CabinetNumber;
} CABINET_INFO_W, *PCABINET_INFO_W;

typedef struct _FILE_IN_CABINET_INFO_A {
    PCSTR NameInCabinet;
    DWORD FileSize;
    DWORD Win32Error;
    WORD DosDate;
    WORD DosTime;
    WORD DosAttribs;
    CHAR FullTargetName[MAX_PATH];
} FILE_IN_CABINET_INFO_A, *PFILE_IN_CABINET_INFO_A;

typedef struct _FILE_IN_CABINET_INFO_W {
    PCWSTR NameInCabinet;
    DWORD FileSize;
    DWORD Win32Error;
    WORD DosDate;
    WORD DosTime;
    WORD DosAttribs;
    WCHAR FullTargetName[MAX_PATH];
} FILE_IN_CABINET_INFO_W, *PFILE_IN_CABINET_INFO_W;

typedef struct _SETUP_OS_COMPONENT_DATA {
    DWORD Size;
    GUID ComponentGuid;
    DWORD VersionMajor;
    DWORD VersionMinor;
    DWORD BuildNumber;
    DWORD ServicePackMajor;
    DWORD ServicePackMinor;
} SETUP_OS_COMPONENT_DATA, *PSETUP_OS_COMPONENT_DATA;

typedef struct _SETUP_OS_EXCEPTION_DATA {
    DWORD Size;
    WCHAR ExceptionInfName[MAX_PATH];
    WCHAR CatalogFileName[MAX_PATH];
    DWORD NumberOfCatalogs;
} SETUP_OS_EXCEPTION_DATA, *PSETUP_OS_EXCEPTION_DATA;

#ifndef _WIN64
#pragma pack(pop)
#endif

typedef UINT(CALLBACK *PSP_FILE_CALLBACK_A)(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2);
typedef UINT(CALLBACK *PSP_FILE_CALLBACK_W)(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2);

#define INF_STYLE_NONE 0x00000000
#define INF_STYLE_OLDNT 0x00000001
#define INF_STYLE_WIN4 0x00000002
#define INF_STYLE_CACHE_ENABLE 0x00000010
#define INF_STYLE_CACHE_DISABLE 0x00000020
#define INF_STYLE_CACHE_IGNORE 0x00000040

#define INFINFO_INF_SPEC_IS_HINF 1
#define INFINFO_INF_NAME_IS_ABSOLUTE 2
#define INFINFO_DEFAULT_SEARCH 3
#define INFINFO_REVERSE_DEFAULT_SEARCH 4
#define INFINFO_INF_PATH_LIST_SEARCH 5

#define SPINST_LOGCONFIG 0x00000001
#define SPINST_INIFILES 0x00000002
#define SPINST_REGISTRY 0x00000004
#define SPINST_INI2REG 0x00000008
#define SPINST_FILES 0x00000010
#define SPINST_BITREG 0x00000020
#define SPINST_REGSVR 0x00000040
#define SPINST_UNREGSVR 0x00000080
#define SPINST_PROFILEITEMS 0x00000100
#define SPINST_COPYINF 0x00000200
#define SPINST_ALL 0x000003ff

#define SPFILENOTIFY_STARTQUEUE 0x00000001
#define SPFILENOTIFY_ENDQUEUE 0x00000002
#define SPFILENOTIFY_STARTSUBQUEUE 0x00000003
#define SPFILENOTIFY_ENDSUBQUEUE 0x00000004
#define SPFILENOTIFY_STARTDELETE 0x00000005
#define SPFILENOTIFY_ENDDELETE 0x00000006
#define SPFILENOTIFY_DELETEERROR 0x00000007
#define SPFILENOTIFY_STARTRENAME 0x00000008
#define SPFILENOTIFY_ENDRENAME 0x00000009
#define SPFILENOTIFY_RENAMEERROR 0x0000000a
#define SPFILENOTIFY_STARTCOPY 0x0000000b
#define SPFILENOTIFY_ENDCOPY 0x0000000c
#define SPFILENOTIFY_COPYERROR 0x0000000d
#define SPFILENOTIFY_NEEDMEDIA 0x0000000e
#define SPFILENOTIFY_QUEUESCAN 0x0000000f
#define SPFILENOTIFY_CABINETINFO 0x00000010
#define SPFILENOTIFY_FILEINCABINET 0x00000011
#define SPFILENOTIFY_NEEDNEWCABINET 0x00000012
#define SPFILENOTIFY_FILEEXTRACTED 0x00000013
#define SPFILENOTIFY_FILEOPDELAYED 0x00000014

#define FILEOP_ABORT 0
#define FILEOP_DOIT 1
#define FILEOP_SKIP 2
#define FILEOP_RETRY FILEOP_DOIT
#define FILEOP_NEWPATH 4

#define FILEOP_COPY 0
#define FILEOP_RENAME 1
#define FILEOP_DELETE 2

#define SP_COPY_DELETESOURCE 0x0000001
#define SP_COPY_REPLACEONLY 0x0000002
#define SP_COPY_NEWER_OR_SAME 0x0000004
#define SP_COPY_NEWER SP_COPY_NEWER_OR_SAME
#define SP_COPY_NOOVERWRITE 0x0000008
#define SP_COPY_NODECOMP 0x0000010
#define SP_COPY_LANGUAGEAWARE 0x0000020
#define SP_COPY_SOURCE_ABSOLUTE 0x0000040
#define SP_COPY_SOURCEPATH_ABSOLUTE 0x0000080
#define SP_COPY_IN_USE_NEEDS_REBOOT 0x0000100
#define SP_COPY_FORCE_IN_USE 0x0000200
#define SP_COPY_NOSKIP 0x0000400
#define SP_COPY_FORCE_NOOVERWRITE 0x0001000
#define SP_COPY_FORCE_NEWER 0x0002000
#define SP_COPY_WARNIFSKIP 0x0004000
#define SP_COPY_NOBROWSE 0x0008000
#define SP_COPY_NEWER_ONLY 0x0010000
#define SP_COPY_SOURCE_SIS_MASTER 0x0020000
#define SP_COPY_OEMINF_CATALOG_ONLY 0x0040000
#define SP_COPY_REPLACE_BOOT_FILE 0x0080000
#define SP_COPY_NOPRUNE 0x0100000

#define SPOST_NONE 0
#define SPOST_PATH 1
#define SPOST_URL 2

#define SRCINFO_PATH 1
#define SRCINFO_TAGFILE 2
#define SRCINFO_DESCRIPTION 3
#define SRCINFO_FLAGS 4
#define SRCINFO_TAGFILE2 5

#define DIRID_ABSOLUTE ((UINT)-1)
#define DIRID_NULL 0
#define DIRID_SRCPATH 1
#define DIRID_WINDOWS 10
#define DIRID_SYSTEM 11
#define DIRID_DRIVERS 12
#define DIRID_INF 17
#define DIRID_USER 0x8000

#define SETUP_OS_COMPONENT_DATA_SIZE 40

WINSETUPAPI HINF WINAPI SetupOpenInfFileW(PCWSTR name, PCWSTR inf_class, DWORD style, PUINT error_line);
WINSETUPAPI HINF WINAPI SetupOpenInfFileA(PCSTR name, PCSTR inf_class, DWORD style, PUINT error_line);
WINSETUPAPI HINF WINAPI SetupOpenMasterInf(void);
WINSETUPAPI BOOL WINAPI SetupOpenAppendInfFileW(PCWSTR name, HINF inf, PUINT error_line);
WINSETUPAPI BOOL WINAPI SetupOpenAppendInfFileA(PCSTR name, HINF inf, PUINT error_line);
WINSETUPAPI VOID WINAPI SetupCloseInfFile(HINF inf);
WINSETUPAPI BOOL WINAPI SetupGetInfInformationW(LPCVOID spec, DWORD search, PSP_INF_INFORMATION info, DWORD size,
                                                PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetInfInformationA(LPCVOID spec, DWORD search, PSP_INF_INFORMATION info, DWORD size,
                                                PDWORD required);
WINSETUPAPI BOOL WINAPI SetupQueryInfFileInformationW(PSP_INF_INFORMATION info, UINT index, PWSTR buffer, DWORD size,
                                                      PDWORD required);
WINSETUPAPI BOOL WINAPI SetupQueryInfFileInformationA(PSP_INF_INFORMATION info, UINT index, PSTR buffer, DWORD size,
                                                      PDWORD required);
WINSETUPAPI BOOL WINAPI SetupQueryInfVersionInformationW(PSP_INF_INFORMATION info, UINT index, PCWSTR key, PWSTR buffer,
                                                         DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupQueryInfVersionInformationA(PSP_INF_INFORMATION info, UINT index, PCSTR key, PSTR buffer,
                                                         DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetInfFileListW(PCWSTR directory, DWORD style, PWSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetInfFileListA(PCSTR directory, DWORD style, PSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupFindFirstLineW(HINF inf, PCWSTR section, PCWSTR key, PINFCONTEXT context);
WINSETUPAPI BOOL WINAPI SetupFindFirstLineA(HINF inf, PCSTR section, PCSTR key, PINFCONTEXT context);
WINSETUPAPI BOOL WINAPI SetupFindNextLine(PINFCONTEXT in, PINFCONTEXT out);
WINSETUPAPI BOOL WINAPI SetupFindNextMatchLineW(PINFCONTEXT in, PCWSTR key, PINFCONTEXT out);
WINSETUPAPI BOOL WINAPI SetupFindNextMatchLineA(PINFCONTEXT in, PCSTR key, PINFCONTEXT out);
WINSETUPAPI BOOL WINAPI SetupGetLineByIndexW(HINF inf, PCWSTR section, DWORD index, PINFCONTEXT context);
WINSETUPAPI BOOL WINAPI SetupGetLineByIndexA(HINF inf, PCSTR section, DWORD index, PINFCONTEXT context);
WINSETUPAPI LONG WINAPI SetupGetLineCountW(HINF inf, PCWSTR section);
WINSETUPAPI LONG WINAPI SetupGetLineCountA(HINF inf, PCSTR section);
WINSETUPAPI BOOL WINAPI SetupGetLineTextW(PINFCONTEXT context, HINF inf, PCWSTR section, PCWSTR key, PWSTR buffer,
                                          DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetLineTextA(PINFCONTEXT context, HINF inf, PCSTR section, PCSTR key, PSTR buffer,
                                          DWORD size, PDWORD required);
WINSETUPAPI DWORD WINAPI SetupGetFieldCount(PINFCONTEXT context);
WINSETUPAPI BOOL WINAPI SetupGetStringFieldW(PINFCONTEXT context, DWORD field, PWSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetStringFieldA(PINFCONTEXT context, DWORD field, PSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetIntField(PINFCONTEXT context, DWORD field, int *value);
WINSETUPAPI BOOL WINAPI SetupGetMultiSzFieldW(PINFCONTEXT context, DWORD field, PWSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetMultiSzFieldA(PINFCONTEXT context, DWORD field, PSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetBinaryField(PINFCONTEXT context, DWORD field, PBYTE buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupInstallFromInfSectionW(HWND owner, HINF inf, PCWSTR section, UINT flags, HKEY key_root,
                                                    PCWSTR source_root, UINT copy_flags, PSP_FILE_CALLBACK_W handler,
                                                    PVOID context, HDEVINFO devices, PSP_DEVINFO_DATA device);
WINSETUPAPI BOOL WINAPI SetupInstallFromInfSectionA(HWND owner, HINF inf, PCSTR section, UINT flags, HKEY key_root,
                                                    PCSTR source_root, UINT copy_flags, PSP_FILE_CALLBACK_A handler,
                                                    PVOID context, HDEVINFO devices, PSP_DEVINFO_DATA device);
WINSETUPAPI BOOL WINAPI SetupInstallFilesFromInfSectionW(HINF inf, HINF layout, HSPFILEQ queue, PCWSTR section,
                                                         PCWSTR source_root, UINT copy_flags);
WINSETUPAPI BOOL WINAPI SetupInstallFilesFromInfSectionA(HINF inf, HINF layout, HSPFILEQ queue, PCSTR section,
                                                         PCSTR source_root, UINT copy_flags);
WINSETUPAPI HSPFILEQ WINAPI SetupOpenFileQueue(void);
WINSETUPAPI BOOL WINAPI SetupCloseFileQueue(HSPFILEQ queue);
WINSETUPAPI BOOL WINAPI SetupQueueCopyW(HSPFILEQ queue, PCWSTR source_root, PCWSTR source_path, PCWSTR source_file,
                                        PCWSTR description, PCWSTR tag_file, PCWSTR target_dir, PCWSTR target_file,
                                        DWORD style);
WINSETUPAPI BOOL WINAPI SetupQueueCopyA(HSPFILEQ queue, PCSTR source_root, PCSTR source_path, PCSTR source_file,
                                        PCSTR description, PCSTR tag_file, PCSTR target_dir, PCSTR target_file,
                                        DWORD style);
WINSETUPAPI BOOL WINAPI SetupQueueCopySectionW(HSPFILEQ queue, PCWSTR source_root, HINF inf, HINF layout, PCWSTR section,
                                               DWORD style);
WINSETUPAPI BOOL WINAPI SetupQueueCopySectionA(HSPFILEQ queue, PCSTR source_root, HINF inf, HINF layout, PCSTR section,
                                               DWORD style);
WINSETUPAPI BOOL WINAPI SetupQueueDeleteW(HSPFILEQ queue, PCWSTR path, PCWSTR file);
WINSETUPAPI BOOL WINAPI SetupQueueDeleteA(HSPFILEQ queue, PCSTR path, PCSTR file);
WINSETUPAPI BOOL WINAPI SetupQueueDeleteSectionW(HSPFILEQ queue, HINF inf, HINF layout, PCWSTR section);
WINSETUPAPI BOOL WINAPI SetupQueueDeleteSectionA(HSPFILEQ queue, HINF inf, HINF layout, PCSTR section);
WINSETUPAPI BOOL WINAPI SetupQueueRenameW(HSPFILEQ queue, PCWSTR source_path, PCWSTR source_file, PCWSTR target_path,
                                          PCWSTR target_file);
WINSETUPAPI BOOL WINAPI SetupQueueRenameA(HSPFILEQ queue, PCSTR source_path, PCSTR source_file, PCSTR target_path,
                                          PCSTR target_file);
WINSETUPAPI BOOL WINAPI SetupQueueRenameSectionW(HSPFILEQ queue, HINF inf, HINF layout, PCWSTR section);
WINSETUPAPI BOOL WINAPI SetupQueueRenameSectionA(HSPFILEQ queue, HINF inf, HINF layout, PCSTR section);
WINSETUPAPI BOOL WINAPI SetupCommitFileQueueW(HWND owner, HSPFILEQ queue, PSP_FILE_CALLBACK_W handler, PVOID context);
WINSETUPAPI BOOL WINAPI SetupCommitFileQueueA(HWND owner, HSPFILEQ queue, PSP_FILE_CALLBACK_A handler, PVOID context);
WINSETUPAPI PVOID WINAPI SetupInitDefaultQueueCallback(HWND owner);
WINSETUPAPI PVOID WINAPI SetupInitDefaultQueueCallbackEx(HWND owner, HWND progress, UINT message, DWORD reserved1,
                                                         PVOID reserved2);
WINSETUPAPI VOID WINAPI SetupTermDefaultQueueCallback(PVOID context);
WINSETUPAPI UINT WINAPI SetupDefaultQueueCallbackW(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2);
WINSETUPAPI UINT WINAPI SetupDefaultQueueCallbackA(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2);
WINSETUPAPI BOOL WINAPI SetupIterateCabinetW(PCWSTR cabinet, DWORD reserved, PSP_FILE_CALLBACK_W handler, PVOID context);
WINSETUPAPI BOOL WINAPI SetupIterateCabinetA(PCSTR cabinet, DWORD reserved, PSP_FILE_CALLBACK_A handler, PVOID context);
WINSETUPAPI BOOL WINAPI SetupGetTargetPathW(HINF inf, PINFCONTEXT context, PCWSTR section, PWSTR buffer, DWORD size,
                                            PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetTargetPathA(HINF inf, PINFCONTEXT context, PCSTR section, PSTR buffer, DWORD size,
                                            PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetSourceFileLocationW(HINF inf, PINFCONTEXT context, PCWSTR file, PUINT source_id,
                                                    PWSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetSourceFileLocationA(HINF inf, PINFCONTEXT context, PCSTR file, PUINT source_id,
                                                    PSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetSourceInfoW(HINF inf, UINT source_id, UINT info, PWSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupGetSourceInfoA(HINF inf, UINT source_id, UINT info, PSTR buffer, DWORD size, PDWORD required);
WINSETUPAPI BOOL WINAPI SetupSetDirectoryIdW(HINF inf, DWORD id, PCWSTR directory);
WINSETUPAPI BOOL WINAPI SetupSetDirectoryIdA(HINF inf, DWORD id, PCSTR directory);
WINSETUPAPI BOOL WINAPI SetupCopyOEMInfW(PCWSTR source, PCWSTR location, DWORD media_type, DWORD style, PWSTR dest,
                                         DWORD size, PDWORD required, PWSTR *component);
WINSETUPAPI BOOL WINAPI SetupCopyOEMInfA(PCSTR source, PCSTR location, DWORD media_type, DWORD style, PSTR dest,
                                         DWORD size, PDWORD required, PSTR *component);
WINSETUPAPI DWORD WINAPI SetupDecompressOrCopyFileW(PCWSTR source, PCWSTR target, PUINT compression);
WINSETUPAPI DWORD WINAPI SetupDecompressOrCopyFileA(PCSTR source, PCSTR target, PUINT compression);
WINSETUPAPI BOOL WINAPI SetupRegisterOsComponent(PSETUP_OS_COMPONENT_DATA component, PSETUP_OS_EXCEPTION_DATA exception);
WINSETUPAPI BOOL WINAPI SetupQueryRegisteredOsComponent(const GUID *guid, PSETUP_OS_COMPONENT_DATA component,
                                                        PSETUP_OS_EXCEPTION_DATA exception);
WINSETUPAPI BOOL WINAPI SetupUnRegisterOsComponent(const GUID *guid);

#endif
