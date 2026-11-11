/* the configuration manager: device nodes, their registry and DEVPKEY properties, and device interface paths. */
#ifndef MIME_WIN32_CFGMGR32_H
#define MIME_WIN32_CFGMGR32_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_CFGMGR32
#define CMAPI DECLSPEC_EXPORT
#else
#define CMAPI DECLSPEC_IMPORT
#endif

typedef DWORD CONFIGRET;
typedef DWORD DEVINST, DEVNODE, *PDEVINST, *PDEVNODE;
typedef ULONG DEVPROPTYPE, *PDEVPROPTYPE;
typedef ULONG DEVPROPID;

typedef struct _DEVPROPKEY {
    GUID fmtid;
    DEVPROPID pid;
} DEVPROPKEY, *PDEVPROPKEY;
typedef const DEVPROPKEY *CONST_DEVPROPKEY_PTR;

#define CR_SUCCESS 0x00000000
#define CR_DEFAULT 0x00000001
#define CR_OUT_OF_MEMORY 0x00000002
#define CR_INVALID_POINTER 0x00000003
#define CR_INVALID_FLAG 0x00000004
#define CR_INVALID_DEVNODE 0x00000005
#define CR_INVALID_DEVINST CR_INVALID_DEVNODE
#define CR_NO_SUCH_DEVNODE 0x0000000D
#define CR_NO_MORE_LOG_CONF 0x0000000E
#define CR_INVALID_LOG_CONF 0x0000000F
#define CR_INVALID_RES_DES 0x00000010
#define CR_NO_SUCH_DEVINST CR_NO_SUCH_DEVNODE
#define CR_FAILURE 0x00000013
#define CR_REGISTRY_ERROR 0x0000001D
#define CR_NO_SUCH_VALUE 0x00000025
#define CR_BUFFER_SMALL 0x0000001A
#define CR_INVALID_DEVICE_ID 0x0000001E
#define CR_INVALID_DATA 0x0000001F
#define CR_INVALID_PROPERTY 0x00000035
#define CR_NO_SUCH_DEVICE_INTERFACE 0x00000037
#define CR_REMOVE_VETOED 0x00000017
#define CR_NOT_DISABLEABLE 0x00000028
#define CR_ACCESS_DENIED 0x00000033
#define CR_INVALID_REFERENCE_STRING 0x00000038
#define CR_INVALID_STRUCTURE_SIZE 0x0000003B
#define CR_WRONG_TYPE 0x00000026
#define CR_DEVICE_NOT_THERE 0x00000024
#define CR_CALLBACK_FAILURE 0x00000034
#define CR_NO_SUCH_REGISTRY_KEY 0x0000002E

#define CM_LOCATE_DEVNODE_NORMAL 0x00000000
#define CM_LOCATE_DEVNODE_PHANTOM 0x00000001
#define CM_LOCATE_DEVNODE_CANCELREMOVE 0x00000002
#define CM_LOCATE_DEVNODE_NOVALIDATION 0x00000004
#define CM_LOCATE_DEVNODE_BITS 0x00000007

#define CM_GET_DEVICE_INTERFACE_LIST_PRESENT 0x00000000
#define CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES 0x00000001

#define CM_DRP_DEVICEDESC 0x00000001
#define CM_DRP_HARDWAREID 0x00000002
#define CM_DRP_COMPATIBLEIDS 0x00000003
#define CM_DRP_SERVICE 0x00000005
#define CM_DRP_CLASS 0x00000008
#define CM_DRP_CLASSGUID 0x00000009
#define CM_DRP_DRIVER 0x0000000A
#define CM_DRP_CONFIGFLAGS 0x0000000B
#define CM_DRP_MFG 0x0000000C
#define CM_DRP_FRIENDLYNAME 0x0000000D
#define CM_DRP_LOCATION_INFORMATION 0x0000000E
#define CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME 0x0000000F
#define CM_DRP_ENUMERATOR_NAME 0x00000017
#define CM_DRP_UPPERFILTERS 0x00000012
#define CM_DRP_LOWERFILTERS 0x00000013
#define CM_DRP_SECURITY 0x00000018
#define CM_DRP_DEVTYPE 0x0000001A
#define CM_DRP_EXCLUSIVE 0x0000001B
#define CM_DRP_CHARACTERISTICS 0x0000001C
#define CM_CRP_UPPERFILTERS CM_DRP_UPPERFILTERS
#define CM_CRP_LOWERFILTERS CM_DRP_LOWERFILTERS
#define CM_CRP_SECURITY CM_DRP_SECURITY
#define CM_CRP_DEVTYPE CM_DRP_DEVTYPE
#define CM_CRP_EXCLUSIVE CM_DRP_EXCLUSIVE
#define CM_CRP_CHARACTERISTICS CM_DRP_CHARACTERISTICS
#define CM_DRP_MAX 0x00000025

#define DN_DRIVER_LOADED 0x00000002
#define DN_STARTED 0x00000008
#define DN_DISABLEABLE 0x00002000
#define DN_NT_ENUMERATOR 0x00800000
#define DN_NT_DRIVER 0x01000000

#define RegDisposition_OpenAlways 0x00000000
#define RegDisposition_OpenExisting 0x00000001

#define CM_OPEN_CLASS_KEY_INSTALLER 0x00000000
#define CM_OPEN_CLASS_KEY_INTERFACE 0x00000001
#define CM_DELETE_CLASS_ONLY 0x00000000
#define CM_DELETE_CLASS_SUBKEYS 0x00000001
#define CM_SETUP_DEVNODE_READY 0x00000000
#define CM_SETUP_DEVNODE_RESET 0x00000001
#define CM_REGISTRY_USER 0x00000100
#define CM_REGISTRY_CONFIG 0x00000200
#define CM_REGISTRY_HARDWARE 0x00000000
#define CM_REGISTRY_SOFTWARE 0x00000001

#define MAX_DEVICE_ID_LEN 200

#define DEVPROP_TYPE_EMPTY 0x00000000
#define DEVPROP_TYPE_UINT32 0x00000007
#define DEVPROP_TYPE_GUID 0x0000000D
#define DEVPROP_TYPE_BOOLEAN 0x00000011
#define DEVPROP_TYPE_STRING 0x00000012
#define DEVPROP_TYPEMOD_LIST 0x00002000
#define DEVPROP_TYPE_STRING_LIST (DEVPROP_TYPE_STRING | DEVPROP_TYPEMOD_LIST)

#define MIME_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, id)                                         \
    const DEVPROPKEY __declspec(selectany) name = {{l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}, id}

MIME_DEVPROPKEY(DEVPKEY_NAME, 0xB725F130, 0x47EF, 0x101A, 0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC, 10);
MIME_DEVPROPKEY(DEVPKEY_Device_DeviceDesc, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 2);
MIME_DEVPROPKEY(DEVPKEY_Device_HardwareIds, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 3);
MIME_DEVPROPKEY(DEVPKEY_Device_CompatibleIds, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 4);
MIME_DEVPROPKEY(DEVPKEY_Device_Service, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 6);
MIME_DEVPROPKEY(DEVPKEY_Device_Class, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 9);
MIME_DEVPROPKEY(DEVPKEY_Device_ClassGuid, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 10);
MIME_DEVPROPKEY(DEVPKEY_Device_Manufacturer, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 13);
MIME_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 14);
MIME_DEVPROPKEY(DEVPKEY_Device_LocationInfo, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 15);
MIME_DEVPROPKEY(DEVPKEY_Device_InstanceId, 0x78C34FC8, 0x104A, 0x4ACA, 0x9E, 0xA4, 0x52, 0x4D, 0x52, 0x99, 0x6E, 0x57, 256);
MIME_DEVPROPKEY(DEVPKEY_Device_Parent, 0x4340A6C5, 0x93FA, 0x4706, 0x97, 0x2C, 0x7B, 0x64, 0x80, 0x08, 0xA5, 0xA7, 8);
MIME_DEVPROPKEY(DEVPKEY_Device_Children, 0x4340A6C5, 0x93FA, 0x4706, 0x97, 0x2C, 0x7B, 0x64, 0x80, 0x08, 0xA5, 0xA7, 9);
MIME_DEVPROPKEY(DEVPKEY_Device_BusReportedDeviceDesc, 0x540B947E, 0x8B40, 0x45BC, 0xA8, 0xA2, 0x6A, 0x0B, 0x89, 0x4C, 0xBD, 0xA2, 4);
MIME_DEVPROPKEY(DEVPKEY_Device_ContainerId, 0x8C7ED206, 0x3F8A, 0x4827, 0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C, 2);

CMAPI CONFIGRET WINAPI CM_Locate_DevNodeW(PDEVINST devinst, PCWSTR id, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Locate_DevNodeA(PDEVINST devinst, PCSTR id, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Parent(PDEVINST out, DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Child(PDEVINST out, DEVINST devinst, ULONG flags);

typedef DWORD_PTR LOG_CONF, *PLOG_CONF, RES_DES, *PRES_DES;
typedef ULONG RESOURCEID, *PRESOURCEID;
#define LOG_CONF_BITS 0x00000007
CMAPI CONFIGRET WINAPI CM_Get_First_Log_Conf(PLOG_CONF out, DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Next_Log_Conf(PLOG_CONF out, LOG_CONF conf, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Free_Log_Conf_Handle(LOG_CONF conf);
CMAPI CONFIGRET WINAPI CM_Get_Next_Res_Des(PRES_DES out, RES_DES from, RESOURCEID wanted, PRESOURCEID found, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Res_Des_Data_Size(PULONG size, RES_DES des, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Res_Des_Data(RES_DES des, PVOID buffer, ULONG length, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Free_Res_Des_Handle(RES_DES des);
CMAPI CONFIGRET WINAPI CM_Get_Sibling(PDEVINST out, DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_IDW(DEVINST devinst, PWSTR buffer, ULONG length, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_IDA(DEVINST devinst, PSTR buffer, ULONG length, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_ID_Size(PULONG length, DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DEVINST devinst, ULONG property, PULONG type, PVOID buffer,
                                                         PULONG length, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyA(DEVINST devinst, ULONG property, PULONG type, PVOID buffer,
                                                         PULONG length, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_DevNode_PropertyW(DEVINST devinst, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                PBYTE buffer, PULONG size, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG length, GUID *klass, PCWSTR device, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeA(PULONG length, GUID *klass, PCSTR device, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_ListW(GUID *klass, PCWSTR device, PWSTR buffer, ULONG length,
                                                     ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_ListA(GUID *klass, PCSTR device, PSTR buffer, ULONG length,
                                                     ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_PropertyW(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                         PBYTE buffer, PULONG size, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Open_DevNode_Key(DEVINST devinst, REGSAM access, ULONG profile, DWORD disposition,
                                           HKEY *key, ULONG flags);
CMAPI DWORD WINAPI CM_MapCrToWin32Err(CONFIGRET cr, DWORD fallback);

#define DEVPROP_TYPE_BYTE 0x00000003
#define DEVPROP_TYPE_UINT16 0x00000005
#define DEVPROP_TYPE_INT32 0x00000006
#define DEVPROP_TYPE_UINT64 0x00000009
#define DEVPROP_TYPE_FILETIME 0x00000010
#define DEVPROP_TYPE_SECURITY_DESCRIPTOR 0x00000013
#define DEVPROP_TYPE_BINARY (DEVPROP_TYPE_BYTE | DEVPROP_TYPEMOD_ARRAY)
#define DEVPROP_TYPEMOD_ARRAY 0x00001000
#define MAX_CLASS_NAME_LEN 32
#define MAX_GUID_STRING_LEN 39

typedef enum _CM_NOTIFY_FILTER_TYPE {
    CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE = 0,
    CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE = 1,
    CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE = 2,
    CM_NOTIFY_FILTER_TYPE_MAX = 3,
} CM_NOTIFY_FILTER_TYPE;

#define CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES 0x00000001
#define CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES 0x00000002

typedef struct _CM_NOTIFY_FILTER {
    DWORD cbSize;
    DWORD Flags;
    CM_NOTIFY_FILTER_TYPE FilterType;
    DWORD Reserved;
    union {
        struct {
            GUID ClassGuid;
        } DeviceInterface;
        struct {
            HANDLE hTarget;
        } DeviceHandle;
        struct {
            WCHAR InstanceId[MAX_DEVICE_ID_LEN];
        } DeviceInstance;
    } u;
} CM_NOTIFY_FILTER, *PCM_NOTIFY_FILTER;

typedef enum _CM_NOTIFY_ACTION {
    CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL = 0,
    CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL = 1,
    CM_NOTIFY_ACTION_DEVICEQUERYREMOVE = 2,
    CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED = 3,
    CM_NOTIFY_ACTION_DEVICEREMOVEPENDING = 4,
    CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE = 5,
    CM_NOTIFY_ACTION_DEVICECUSTOMEVENT = 6,
    CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED = 7,
    CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED = 8,
    CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED = 9,
    CM_NOTIFY_ACTION_MAX = 10,
} CM_NOTIFY_ACTION;

typedef struct _CM_NOTIFY_EVENT_DATA {
    CM_NOTIFY_FILTER_TYPE FilterType;
    DWORD Reserved;
    union {
        struct {
            GUID ClassGuid;
            WCHAR SymbolicLink[1];
        } DeviceInterface;
        struct {
            GUID EventGuid;
            LONG NameOffset;
            DWORD DataSize;
            BYTE Data[1];
        } DeviceHandle;
        struct {
            WCHAR InstanceId[1];
        } DeviceInstance;
    } u;
} CM_NOTIFY_EVENT_DATA, *PCM_NOTIFY_EVENT_DATA;

typedef PVOID HCMNOTIFICATION, *PHCMNOTIFICATION;
typedef DWORD(CALLBACK *PCM_NOTIFY_CALLBACK)(HCMNOTIFICATION notify, PVOID context, CM_NOTIFY_ACTION action,
                                              PCM_NOTIFY_EVENT_DATA data, DWORD size);

CMAPI CONFIGRET WINAPI CM_Register_Notification(PCM_NOTIFY_FILTER filter, PVOID context,
                                                PCM_NOTIFY_CALLBACK callback, PHCMNOTIFICATION notify);
CMAPI CONFIGRET WINAPI CM_Unregister_Notification(HCMNOTIFICATION notify);
CMAPI CONFIGRET WINAPI CM_Get_Depth(PULONG depth, DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Enable_DevNode(DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Disable_DevNode(DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Setup_DevNode(DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Uninstall_DevNode(DEVINST devinst, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Query_And_Remove_SubTreeW(DEVINST devinst, PULONG veto_type, PWSTR veto_name, ULONG length,
                                                    ULONG flags);
CMAPI CONFIGRET WINAPI CM_Delete_DevNode_Key(DEVINST devinst, ULONG profile, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_DevNode_Property_Keys(DEVINST devinst, DEVPROPKEY *keys, PULONG count, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Set_DevNode_PropertyW(DEVINST devinst, const DEVPROPKEY *key, DEVPROPTYPE type,
                                                PBYTE buffer, ULONG size, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Set_DevNode_Registry_PropertyW(DEVINST devinst, ULONG property, PVOID buffer, ULONG length,
                                                         ULONG flags);
CMAPI CONFIGRET WINAPI CM_Open_Class_KeyW(const GUID *klass, PCWSTR name, REGSAM access, DWORD disposition, HKEY *key,
                                          ULONG flags);
CMAPI CONFIGRET WINAPI CM_Delete_Class_Key(const GUID *klass, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Class_Registry_PropertyW(const GUID *klass, ULONG property, PULONG type, PVOID buffer,
                                                       PULONG length, ULONG flags, void *machine);
CMAPI CONFIGRET WINAPI CM_Set_Class_Registry_PropertyW(const GUID *klass, ULONG property, PVOID buffer, ULONG length,
                                                       ULONG flags, void *machine);
CMAPI CONFIGRET WINAPI CM_Get_Class_PropertyW(const GUID *klass, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                              PBYTE buffer, PULONG size, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Set_Class_PropertyW(const GUID *klass, const DEVPROPKEY *key, DEVPROPTYPE type,
                                              PBYTE buffer, ULONG size, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Class_Property_Keys(const GUID *klass, DEVPROPKEY *keys, PULONG count, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Open_Device_Interface_KeyW(PCWSTR path, REGSAM access, DWORD disposition, HKEY *key,
                                                     ULONG flags);
CMAPI CONFIGRET WINAPI CM_Delete_Device_Interface_KeyW(PCWSTR path, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_Property_KeysW(PCWSTR path, DEVPROPKEY *keys, PULONG count,
                                                              ULONG flags);
CMAPI CONFIGRET WINAPI CM_Set_Device_Interface_PropertyW(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE type,
                                                         PBYTE buffer, ULONG size, ULONG flags);
CMAPI CONFIGRET WINAPI CM_Get_Device_Interface_AliasW(PCWSTR path, GUID *alias_class, PWSTR alias, PULONG length,
                                                      ULONG flags);

#endif
