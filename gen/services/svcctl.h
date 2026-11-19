#ifndef NDRGEN_SVCCTL_H
#define NDRGEN_SVCCTL_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct SC_SERVICE_STATUS SC_SERVICE_STATUS;
typedef struct SC_STRING_PTRW SC_STRING_PTRW;
typedef struct SC_CONTROL_RESPONSE SC_CONTROL_RESPONSE;
typedef struct SC_CONTROL_ARGUMENTS SC_CONTROL_ARGUMENTS;
typedef struct SC_CONTROL_EVENT SC_CONTROL_EVENT;
typedef union SC_CONTROL_PAYLOAD SC_CONTROL_PAYLOAD;
typedef struct SC_CONTROL_MESSAGE SC_CONTROL_MESSAGE;

typedef void *SC_RPC_HANDLE;
struct SC_SERVICE_STATUS {
    unsigned long dwServiceType;
    unsigned long dwCurrentState;
    unsigned long dwControlsAccepted;
    unsigned long dwWin32ExitCode;
    unsigned long dwServiceSpecificExitCode;
    unsigned long dwCheckPoint;
    unsigned long dwWaitHint;
};
struct SC_STRING_PTRW {
    unsigned short *StringPtr;
};
typedef void *SC_CHANNEL_HANDLE;
struct SC_CONTROL_RESPONSE {
    unsigned long Status;
    long long Id;
    unsigned long Result;
    unsigned long TagCount;
    unsigned long *Tags;
};
struct SC_CONTROL_ARGUMENTS {
    unsigned long Count;
    SC_STRING_PTRW Strings[1];
};
struct SC_CONTROL_EVENT {
    unsigned long Reserved;
    unsigned long Size;
    byte *Data;
};
union SC_CONTROL_PAYLOAD {
    unsigned long *Value;
    SC_CONTROL_ARGUMENTS *Arguments;
    SC_CONTROL_EVENT *Event;
};
struct SC_CONTROL_MESSAGE {
    unsigned long Control;
    long long Id;
    unsigned long Flags;
    unsigned long Reason;
    unsigned long Reserved1;
    unsigned long Reserved2;
    unsigned long Kind;
    SC_CONTROL_PAYLOAD Payload;
};

#ifdef _WIN64
_Static_assert(sizeof(SC_RPC_HANDLE) == 8, "size of SC_RPC_HANDLE");
_Static_assert(sizeof(SC_SERVICE_STATUS) == 28, "size of SC_SERVICE_STATUS");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwServiceType) == 0, "SC_SERVICE_STATUS.dwServiceType");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwCurrentState) == 4, "SC_SERVICE_STATUS.dwCurrentState");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwControlsAccepted) == 8, "SC_SERVICE_STATUS.dwControlsAccepted");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwWin32ExitCode) == 12, "SC_SERVICE_STATUS.dwWin32ExitCode");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwServiceSpecificExitCode) == 16, "SC_SERVICE_STATUS.dwServiceSpecificExitCode");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwCheckPoint) == 20, "SC_SERVICE_STATUS.dwCheckPoint");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwWaitHint) == 24, "SC_SERVICE_STATUS.dwWaitHint");
_Static_assert(sizeof(SC_STRING_PTRW) == 8, "size of SC_STRING_PTRW");
_Static_assert(__builtin_offsetof(SC_STRING_PTRW, StringPtr) == 0, "SC_STRING_PTRW.StringPtr");
_Static_assert(sizeof(SC_CHANNEL_HANDLE) == 8, "size of SC_CHANNEL_HANDLE");
_Static_assert(sizeof(SC_CONTROL_RESPONSE) == 32, "size of SC_CONTROL_RESPONSE");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Status) == 0, "SC_CONTROL_RESPONSE.Status");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Id) == 8, "SC_CONTROL_RESPONSE.Id");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Result) == 16, "SC_CONTROL_RESPONSE.Result");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, TagCount) == 20, "SC_CONTROL_RESPONSE.TagCount");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Tags) == 24, "SC_CONTROL_RESPONSE.Tags");
_Static_assert(sizeof(SC_CONTROL_ARGUMENTS) == 16, "size of SC_CONTROL_ARGUMENTS");
_Static_assert(__builtin_offsetof(SC_CONTROL_ARGUMENTS, Count) == 0, "SC_CONTROL_ARGUMENTS.Count");
_Static_assert(__builtin_offsetof(SC_CONTROL_ARGUMENTS, Strings) == 8, "SC_CONTROL_ARGUMENTS.Strings");
_Static_assert(sizeof(SC_CONTROL_EVENT) == 16, "size of SC_CONTROL_EVENT");
_Static_assert(__builtin_offsetof(SC_CONTROL_EVENT, Reserved) == 0, "SC_CONTROL_EVENT.Reserved");
_Static_assert(__builtin_offsetof(SC_CONTROL_EVENT, Size) == 4, "SC_CONTROL_EVENT.Size");
_Static_assert(__builtin_offsetof(SC_CONTROL_EVENT, Data) == 8, "SC_CONTROL_EVENT.Data");
_Static_assert(sizeof(SC_CONTROL_PAYLOAD) == 8, "size of SC_CONTROL_PAYLOAD");
_Static_assert(sizeof(SC_CONTROL_MESSAGE) == 48, "size of SC_CONTROL_MESSAGE");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Control) == 0, "SC_CONTROL_MESSAGE.Control");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Id) == 8, "SC_CONTROL_MESSAGE.Id");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Flags) == 16, "SC_CONTROL_MESSAGE.Flags");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Reason) == 20, "SC_CONTROL_MESSAGE.Reason");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Reserved1) == 24, "SC_CONTROL_MESSAGE.Reserved1");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Reserved2) == 28, "SC_CONTROL_MESSAGE.Reserved2");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Kind) == 32, "SC_CONTROL_MESSAGE.Kind");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Payload) == 40, "SC_CONTROL_MESSAGE.Payload");
#else
_Static_assert(sizeof(SC_RPC_HANDLE) == 4, "size of SC_RPC_HANDLE");
_Static_assert(sizeof(SC_SERVICE_STATUS) == 28, "size of SC_SERVICE_STATUS");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwServiceType) == 0, "SC_SERVICE_STATUS.dwServiceType");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwCurrentState) == 4, "SC_SERVICE_STATUS.dwCurrentState");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwControlsAccepted) == 8, "SC_SERVICE_STATUS.dwControlsAccepted");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwWin32ExitCode) == 12, "SC_SERVICE_STATUS.dwWin32ExitCode");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwServiceSpecificExitCode) == 16, "SC_SERVICE_STATUS.dwServiceSpecificExitCode");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwCheckPoint) == 20, "SC_SERVICE_STATUS.dwCheckPoint");
_Static_assert(__builtin_offsetof(SC_SERVICE_STATUS, dwWaitHint) == 24, "SC_SERVICE_STATUS.dwWaitHint");
_Static_assert(sizeof(SC_STRING_PTRW) == 4, "size of SC_STRING_PTRW");
_Static_assert(__builtin_offsetof(SC_STRING_PTRW, StringPtr) == 0, "SC_STRING_PTRW.StringPtr");
_Static_assert(sizeof(SC_CHANNEL_HANDLE) == 4, "size of SC_CHANNEL_HANDLE");
_Static_assert(sizeof(SC_CONTROL_RESPONSE) == 32, "size of SC_CONTROL_RESPONSE");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Status) == 0, "SC_CONTROL_RESPONSE.Status");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Id) == 8, "SC_CONTROL_RESPONSE.Id");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Result) == 16, "SC_CONTROL_RESPONSE.Result");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, TagCount) == 20, "SC_CONTROL_RESPONSE.TagCount");
_Static_assert(__builtin_offsetof(SC_CONTROL_RESPONSE, Tags) == 24, "SC_CONTROL_RESPONSE.Tags");
_Static_assert(sizeof(SC_CONTROL_ARGUMENTS) == 8, "size of SC_CONTROL_ARGUMENTS");
_Static_assert(__builtin_offsetof(SC_CONTROL_ARGUMENTS, Count) == 0, "SC_CONTROL_ARGUMENTS.Count");
_Static_assert(__builtin_offsetof(SC_CONTROL_ARGUMENTS, Strings) == 4, "SC_CONTROL_ARGUMENTS.Strings");
_Static_assert(sizeof(SC_CONTROL_EVENT) == 12, "size of SC_CONTROL_EVENT");
_Static_assert(__builtin_offsetof(SC_CONTROL_EVENT, Reserved) == 0, "SC_CONTROL_EVENT.Reserved");
_Static_assert(__builtin_offsetof(SC_CONTROL_EVENT, Size) == 4, "SC_CONTROL_EVENT.Size");
_Static_assert(__builtin_offsetof(SC_CONTROL_EVENT, Data) == 8, "SC_CONTROL_EVENT.Data");
_Static_assert(sizeof(SC_CONTROL_PAYLOAD) == 4, "size of SC_CONTROL_PAYLOAD");
_Static_assert(sizeof(SC_CONTROL_MESSAGE) == 40, "size of SC_CONTROL_MESSAGE");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Control) == 0, "SC_CONTROL_MESSAGE.Control");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Id) == 8, "SC_CONTROL_MESSAGE.Id");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Flags) == 16, "SC_CONTROL_MESSAGE.Flags");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Reason) == 20, "SC_CONTROL_MESSAGE.Reason");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Reserved1) == 24, "SC_CONTROL_MESSAGE.Reserved1");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Reserved2) == 28, "SC_CONTROL_MESSAGE.Reserved2");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Kind) == 32, "SC_CONTROL_MESSAGE.Kind");
_Static_assert(__builtin_offsetof(SC_CONTROL_MESSAGE, Payload) == 36, "SC_CONTROL_MESSAGE.Payload");
#endif

/* ------------------------------------------------------------------ interface svcctl */

#ifndef SVCCTL_CLIENT_ONLY
extern unsigned long WINAPI svcctl_RCloseServiceHandle_s(SC_RPC_HANDLE *hSCObject);
extern unsigned long WINAPI svcctl_RControlService_s(SC_RPC_HANDLE hService, unsigned long dwControl, SC_SERVICE_STATUS *lpServiceStatus);
extern unsigned long WINAPI svcctl_RDeleteService_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RLockServiceDatabase_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceObjectSecurity_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RSetServiceObjectSecurity_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceStatus_s(SC_RPC_HANDLE hService, SC_SERVICE_STATUS *lpServiceStatus);
extern unsigned long WINAPI svcctl_RSetServiceStatus_s(SC_RPC_HANDLE hServiceStatus, SC_SERVICE_STATUS *lpServiceStatus);
extern unsigned long WINAPI svcctl_RUnlockServiceDatabase_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RNotifyBootConfigStatus_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScSetServiceBitsW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RChangeServiceConfigW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RCreateServiceW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_REnumDependentServicesW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_REnumServicesStatusW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_ROpenSCManagerW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_ROpenServiceW_s(SC_RPC_HANDLE hSCManager, unsigned short *lpServiceName, unsigned long dwDesiredAccess, SC_RPC_HANDLE *lpServiceHandle);
extern unsigned long WINAPI svcctl_RQueryServiceConfigW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceLockStatusW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RStartServiceW_s(SC_RPC_HANDLE hService, unsigned long argc, SC_STRING_PTRW *argv);
extern unsigned long WINAPI svcctl_RGetServiceDisplayNameW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetServiceKeyNameW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScSetServiceBitsA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RChangeServiceConfigA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RCreateServiceA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_REnumDependentServicesA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_REnumServicesStatusA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_ROpenSCManagerA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_ROpenServiceA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceConfigA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceLockStatusA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RStartServiceA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetServiceDisplayNameA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetServiceKeyNameA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScGetCurrentGroupStateW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_REnumServiceGroupW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RChangeServiceConfig2A_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RChangeServiceConfig2W_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceConfig2A_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceConfig2W_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceStatusEx_s(SC_RPC_HANDLE hService, unsigned long InfoLevel, byte *lpBuffer, unsigned long cbBufSize, unsigned long *pcbBytesNeeded);
extern unsigned long WINAPI svcctl_REnumServicesStatusExA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_REnumServicesStatusExW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScQueryServiceTagInfo_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RCreateServiceWOW64A_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RCreateServiceWOW64W_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScQueryServiceTagInfoEx_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RNotifyServiceStatusChange_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetNotifyResults_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RCloseNotifyHandle_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RControlServiceExA_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RControlServiceExW_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScSendPnPMessage_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScValidatePnPService_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScOpenServiceStatusHandle_s(SC_RPC_HANDLE hSCManager, unsigned short *lpServiceName, long long StatusId, SC_RPC_HANDLE *lpStatusHandle);
extern unsigned long WINAPI svcctl_RI_ScQueryServiceConfig_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryServiceConfigEx_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScRegisterPreshutdownRestart_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScReparseServiceDatabase_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RQueryUserServiceName_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RCreateWowService_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetServiceRegistryStateKey_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetServiceDirectory_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetServiceProcessToken_s(handle_t Binding);
extern unsigned long WINAPI svcctl_ROpenSCManager2_s(handle_t BindingHandle, unsigned short *DatabaseName, unsigned long DesiredAccess, SC_RPC_HANDLE *ScmHandle);
extern unsigned long WINAPI svcctl_RGetSharedServiceRegistryStateKey_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RGetSharedServiceDirectory_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RReparseServiceConfig_s(handle_t Binding);
extern unsigned long WINAPI svcctl_RI_ScSendSynchronousPowerMessage_s(handle_t Binding);
#endif
void WINAPI SC_RPC_HANDLE_rundown(void *);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char svcctl_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x30, 0xe0, 0x00, 0x00, /* 2: context handle SC_RPC_HANDLE */
    0x30, 0x40, 0x00, 0x00, /* 6: context handle SC_RPC_HANDLE */
    0x15, 0x03, 0x1c, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x5b, /* 10: struct SC_SERVICE_STATUS (STRUCT) */
    0x11, 0x08, 0x25, 0x5c, /* 22: pointer */
    0x30, 0xa0, 0x00, 0x00, /* 26: context handle SC_RPC_HANDLE */
    0x12, 0x00, 0x02, 0x00, /* 30: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 34: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x36, 0x5b, 0x12, 0x08, /* 55: struct SC_STRING_PTRW (BOGUS) */
    0x25, 0x5c,
    0x11, 0x04, 0x02, 0x00, /* 69: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 73: array */
    0x11, 0x08, 0x25, 0x5c, /* 85: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 89: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x30, 0xe0, 0x00, 0x00, /* 2: context handle SC_RPC_HANDLE */
    0x30, 0x40, 0x00, 0x00, /* 6: context handle SC_RPC_HANDLE */
    0x15, 0x03, 0x1c, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x5b, /* 10: struct SC_SERVICE_STATUS (STRUCT) */
    0x11, 0x08, 0x25, 0x5c, /* 22: pointer */
    0x30, 0xa0, 0x00, 0x00, /* 26: context handle SC_RPC_HANDLE */
    0x12, 0x00, 0x02, 0x00, /* 30: pointer */
    0x1b, 0x03, 0x04, 0x00, 0x28, 0x00, 0x04, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 34: array */
    0x48, 0x49, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x04, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 68: struct SC_STRING_PTRW (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36, 0x5b,
    0x11, 0x04, 0x02, 0x00, /* 87: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 91: array */
    0x11, 0x08, 0x25, 0x5c, /* 103: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 107: pointer */
#endif
};

static const unsigned char svcctl_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x30, 0xe0, 0x00, 0x00, 0x00, 0x00, /* 2: svcctl.RCloseServiceHandle */
    0x17, 0x00, 0x1f, 0x00, 0x44, 0x02, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x20, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 42: svcctl.RControlService */
    0x1f, 0x00, 0x27, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x00,
    0x08, 0x00, 0x08, 0x5c, 0x12, 0x81, 0x10, 0x00, 0x0a, 0x00, 0x70, 0x00,
    0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 94: svcctl.RDeleteService */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 126: svcctl.RLockServiceDatabase */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 158: svcctl.RQueryServiceObjectSecurity */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 190: svcctl.RSetServiceObjectSecurity */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x18, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 222: svcctl.RQueryServiceStatus */
    0x17, 0x00, 0x27, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x12, 0x81,
    0x08, 0x00, 0x0a, 0x00, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x18, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 268: svcctl.RSetServiceStatus */
    0x37, 0x00, 0x07, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0a, 0x01,
    0x08, 0x00, 0x0a, 0x00, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 314: svcctl.RUnlockServiceDatabase */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 346: svcctl.RNotifyBootConfigStatus */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 378: svcctl.RI_ScSetServiceBitsW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 410: svcctl.RChangeServiceConfigW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 442: svcctl.RCreateServiceW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 474: svcctl.REnumDependentServicesW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 506: svcctl.REnumServicesStatusW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 538: svcctl.ROpenSCManagerW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x28, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 570: svcctl.ROpenServiceW */
    0x1f, 0x00, 0x1f, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0x16, 0x00, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c, 0x10, 0x01,
    0x18, 0x00, 0x1a, 0x00, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 628: svcctl.RQueryServiceConfigW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 660: svcctl.RQueryServiceLockStatusW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x20, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 692: svcctl.RStartServiceW */
    0x1f, 0x00, 0x07, 0x00, 0x46, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x00,
    0x08, 0x00, 0x08, 0x5c, 0x0b, 0x00, 0x10, 0x00, 0x1e, 0x00, 0x70, 0x00,
    0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 744: svcctl.RGetServiceDisplayNameW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x15, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 776: svcctl.RGetServiceKeyNameW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x16, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 808: svcctl.RI_ScSetServiceBitsA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x17, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 840: svcctl.RChangeServiceConfigA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x18, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 872: svcctl.RCreateServiceA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x19, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 904: svcctl.REnumDependentServicesA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 936: svcctl.REnumServicesStatusA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1b, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 968: svcctl.ROpenSCManagerA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1c, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1000: svcctl.ROpenServiceA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1d, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1032: svcctl.RQueryServiceConfigA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1e, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1064: svcctl.RQueryServiceLockStatusA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1f, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1096: svcctl.RStartServiceA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x20, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1128: svcctl.RGetServiceDisplayNameA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x21, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1160: svcctl.RGetServiceKeyNameA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x22, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1192: svcctl.RI_ScGetCurrentGroupStateW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x23, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1224: svcctl.REnumServiceGroupW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x24, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1256: svcctl.RChangeServiceConfig2A */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x25, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1288: svcctl.RChangeServiceConfig2W */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x26, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1320: svcctl.RQueryServiceConfig2A */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x27, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1352: svcctl.RQueryServiceConfig2W */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x28, 0x00, 0x30, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 1384: svcctl.RQueryServiceStatusEx */
    0x27, 0x00, 0x0f, 0x00, 0x45, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x00,
    0x08, 0x00, 0x08, 0x5c, 0x13, 0x00, 0x10, 0x00, 0x45, 0x00, 0x48, 0x00,
    0x18, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x20, 0x00, 0x08, 0x5c, 0x70, 0x00,
    0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x29, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1448: svcctl.REnumServicesStatusExA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1480: svcctl.REnumServicesStatusExW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2b, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1512: svcctl.RI_ScQueryServiceTagInfo */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2c, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1544: svcctl.RCreateServiceWOW64A */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2d, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1576: svcctl.RCreateServiceWOW64W */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2e, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1608: svcctl.RI_ScQueryServiceTagInfoEx */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2f, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1640: svcctl.RNotifyServiceStatusChange */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x30, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1672: svcctl.RGetNotifyResults */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x31, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1704: svcctl.RCloseNotifyHandle */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x32, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1736: svcctl.RControlServiceExA */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x33, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1768: svcctl.RControlServiceExW */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x34, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1800: svcctl.RI_ScSendPnPMessage */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x35, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1832: svcctl.RI_ScValidatePnPService */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x36, 0x00, 0x28, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 1864: svcctl.RI_ScOpenServiceStatusHandle */
    0x27, 0x00, 0x1f, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0x55, 0x00, 0x48, 0x00, 0x10, 0x00, 0x0b, 0x5c, 0x10, 0x01,
    0x18, 0x00, 0x1a, 0x00, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x37, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1922: svcctl.RI_ScQueryServiceConfig */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x38, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1954: svcctl.RQueryServiceConfigEx */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x39, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1986: svcctl.RI_ScRegisterPreshutdownRestart */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2018: svcctl.RI_ScReparseServiceDatabase */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3b, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2050: svcctl.RQueryUserServiceName */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3c, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2082: svcctl.RCreateWowService */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3d, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2114: svcctl.RGetServiceRegistryStateKey */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3e, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2146: svcctl.RGetServiceDirectory */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3f, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2178: svcctl.RGetServiceProcessToken */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x40, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 2210: svcctl.ROpenSCManager2 */
    0x1f, 0x00, 0x46, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x59, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x10, 0x01, 0x18, 0x00, 0x1a, 0x00, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x41, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2260: svcctl.RGetSharedServiceRegistryStateKey */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x42, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2292: svcctl.RGetSharedServiceDirectory */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x43, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2324: svcctl.RReparseServiceConfig */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x44, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2356: svcctl.RI_ScSendSynchronousPowerMessage */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x08, 0x00, 0x30, 0xe0, 0x00, 0x00, 0x00, 0x00, /* 2: svcctl.RCloseServiceHandle */
    0x17, 0x00, 0x1f, 0x00, 0x44, 0x02, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 40: svcctl.RControlService */
    0x1f, 0x00, 0x27, 0x00, 0x44, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x08, 0x5c, 0x12, 0x81, 0x08, 0x00, 0x0a, 0x00, 0x70, 0x00, 0x0c, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 90: svcctl.RDeleteService */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 120: svcctl.RLockServiceDatabase */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 150: svcctl.RQueryServiceObjectSecurity */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 180: svcctl.RSetServiceObjectSecurity */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x0c, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 210: svcctl.RQueryServiceStatus */
    0x17, 0x00, 0x27, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x12, 0x81, 0x04, 0x00,
    0x0a, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x0c, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 254: svcctl.RSetServiceStatus */
    0x37, 0x00, 0x07, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0a, 0x01, 0x04, 0x00,
    0x0a, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 298: svcctl.RUnlockServiceDatabase */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 328: svcctl.RNotifyBootConfigStatus */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 358: svcctl.RI_ScSetServiceBitsW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 388: svcctl.RChangeServiceConfigW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 418: svcctl.RCreateServiceW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 448: svcctl.REnumDependentServicesW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 478: svcctl.REnumServicesStatusW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 508: svcctl.ROpenSCManagerW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x14, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 538: svcctl.ROpenServiceW */
    0x1f, 0x00, 0x1f, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x04, 0x00,
    0x16, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c, 0x10, 0x01, 0x0c, 0x00,
    0x1a, 0x00, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 594: svcctl.RQueryServiceConfigW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 624: svcctl.RQueryServiceLockStatusW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x10, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 654: svcctl.RStartServiceW */
    0x1f, 0x00, 0x07, 0x00, 0x46, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x08, 0x5c, 0x0b, 0x00, 0x08, 0x00, 0x1e, 0x00, 0x70, 0x00, 0x0c, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 704: svcctl.RGetServiceDisplayNameW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x15, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 734: svcctl.RGetServiceKeyNameW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x16, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 764: svcctl.RI_ScSetServiceBitsA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x17, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 794: svcctl.RChangeServiceConfigA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x18, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 824: svcctl.RCreateServiceA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x19, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 854: svcctl.REnumDependentServicesA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1a, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 884: svcctl.REnumServicesStatusA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1b, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 914: svcctl.ROpenSCManagerA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1c, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 944: svcctl.ROpenServiceA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1d, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 974: svcctl.RQueryServiceConfigA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1e, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1004: svcctl.RQueryServiceLockStatusA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1f, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1034: svcctl.RStartServiceA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x20, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1064: svcctl.RGetServiceDisplayNameA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x21, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1094: svcctl.RGetServiceKeyNameA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x22, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1124: svcctl.RI_ScGetCurrentGroupStateW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x23, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1154: svcctl.REnumServiceGroupW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x24, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1184: svcctl.RChangeServiceConfig2A */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x25, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1214: svcctl.RChangeServiceConfig2W */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x26, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1244: svcctl.RQueryServiceConfig2A */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x27, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1274: svcctl.RQueryServiceConfig2W */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x28, 0x00, 0x18, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 1304: svcctl.RQueryServiceStatusEx */
    0x27, 0x00, 0x0f, 0x00, 0x45, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x08, 0x5c, 0x13, 0x00, 0x08, 0x00, 0x57, 0x00, 0x48, 0x00, 0x0c, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x10, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x14, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x29, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1366: svcctl.REnumServicesStatusExA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2a, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1396: svcctl.REnumServicesStatusExW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2b, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1426: svcctl.RI_ScQueryServiceTagInfo */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2c, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1456: svcctl.RCreateServiceWOW64A */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2d, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1486: svcctl.RCreateServiceWOW64W */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2e, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1516: svcctl.RI_ScQueryServiceTagInfoEx */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x2f, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1546: svcctl.RNotifyServiceStatusChange */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x30, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1576: svcctl.RGetNotifyResults */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x31, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1606: svcctl.RCloseNotifyHandle */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x32, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1636: svcctl.RControlServiceExA */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x33, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1666: svcctl.RControlServiceExW */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x34, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1696: svcctl.RI_ScSendPnPMessage */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x35, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1726: svcctl.RI_ScValidatePnPService */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x36, 0x00, 0x18, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 1756: svcctl.RI_ScOpenServiceStatusHandle */
    0x27, 0x00, 0x1f, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x04, 0x00,
    0x67, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c, 0x10, 0x01, 0x10, 0x00,
    0x1a, 0x00, 0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x37, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1812: svcctl.RI_ScQueryServiceConfig */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x38, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1842: svcctl.RQueryServiceConfigEx */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x39, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1872: svcctl.RI_ScRegisterPreshutdownRestart */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3a, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1902: svcctl.RI_ScReparseServiceDatabase */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3b, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1932: svcctl.RQueryUserServiceName */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3c, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1962: svcctl.RCreateWowService */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3d, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1992: svcctl.RGetServiceRegistryStateKey */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3e, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2022: svcctl.RGetServiceDirectory */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x3f, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2052: svcctl.RGetServiceProcessToken */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x40, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 2082: svcctl.ROpenSCManager2 */
    0x1f, 0x00, 0x46, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x04, 0x00, 0x6b, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x10, 0x01, 0x0c, 0x00, 0x1a, 0x00, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x41, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2130: svcctl.RGetSharedServiceRegistryStateKey */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x42, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2160: svcctl.RGetSharedServiceDirectory */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x43, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2190: svcctl.RReparseServiceConfig */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x44, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2220: svcctl.RI_ScSendSynchronousPowerMessage */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short svcctl_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 42, 94, 126, 158, 190, 222, 268, 314, 346, 378, 410, 442, 474, 506, 538, 570, 628, 660, 692, 744, 776, 808, 840, 872, 904, 936, 968, 1000, 1032, 1064, 1096, 1128, 1160, 1192, 1224, 1256, 1288, 1320, 1352, 1384, 1448, 1480, 1512, 1544, 1576, 1608, 1640, 1672, 1704, 1736, 1768, 1800, 1832, 1864, 1922, 1954, 1986, 2018, 2050, 2082, 2114, 2146, 2178, 2210, 2260, 2292, 2324, 2356
#else
    2, 40, 90, 120, 150, 180, 210, 254, 298, 328, 358, 388, 418, 448, 478, 508, 538, 594, 624, 654, 704, 734, 764, 794, 824, 854, 884, 914, 944, 974, 1004, 1034, 1064, 1094, 1124, 1154, 1184, 1214, 1244, 1274, 1304, 1366, 1396, 1426, 1456, 1486, 1516, 1546, 1576, 1606, 1636, 1666, 1696, 1726, 1756, 1812, 1842, 1872, 1902, 1932, 1962, 1992, 2022, 2052, 2082, 2130, 2160, 2190, 2220
#endif
};

static const NDR_RUNDOWN svcctl_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)SC_RPC_HANDLE_rundown
};

static RPC_CLIENT_INTERFACE svcctl_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x367abb81, 0x9844, 0x35f1, {0xad, 0x32, 0x98, 0xf0, 0x38, 0x00, 0x10, 0x03}}, {2, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE svcctl_c_ifspec = (RPC_IF_HANDLE)&svcctl_c_ifspec_data;

static MIDL_STUB_DESC svcctl_StubDesc = {
    .RpcInterfaceInformation = &svcctl_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = svcctl_RundownRoutines,
    .pFormatTypes = svcctl_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef SVCCTL_CLIENT_ONLY
static const SERVER_ROUTINE svcctl_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)svcctl_RCloseServiceHandle_s,
    (SERVER_ROUTINE)(void *)svcctl_RControlService_s,
    (SERVER_ROUTINE)(void *)svcctl_RDeleteService_s,
    (SERVER_ROUTINE)(void *)svcctl_RLockServiceDatabase_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceObjectSecurity_s,
    (SERVER_ROUTINE)(void *)svcctl_RSetServiceObjectSecurity_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceStatus_s,
    (SERVER_ROUTINE)(void *)svcctl_RSetServiceStatus_s,
    (SERVER_ROUTINE)(void *)svcctl_RUnlockServiceDatabase_s,
    (SERVER_ROUTINE)(void *)svcctl_RNotifyBootConfigStatus_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScSetServiceBitsW_s,
    (SERVER_ROUTINE)(void *)svcctl_RChangeServiceConfigW_s,
    (SERVER_ROUTINE)(void *)svcctl_RCreateServiceW_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumDependentServicesW_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumServicesStatusW_s,
    (SERVER_ROUTINE)(void *)svcctl_ROpenSCManagerW_s,
    (SERVER_ROUTINE)(void *)svcctl_ROpenServiceW_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceConfigW_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceLockStatusW_s,
    (SERVER_ROUTINE)(void *)svcctl_RStartServiceW_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceDisplayNameW_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceKeyNameW_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScSetServiceBitsA_s,
    (SERVER_ROUTINE)(void *)svcctl_RChangeServiceConfigA_s,
    (SERVER_ROUTINE)(void *)svcctl_RCreateServiceA_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumDependentServicesA_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumServicesStatusA_s,
    (SERVER_ROUTINE)(void *)svcctl_ROpenSCManagerA_s,
    (SERVER_ROUTINE)(void *)svcctl_ROpenServiceA_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceConfigA_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceLockStatusA_s,
    (SERVER_ROUTINE)(void *)svcctl_RStartServiceA_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceDisplayNameA_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceKeyNameA_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScGetCurrentGroupStateW_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumServiceGroupW_s,
    (SERVER_ROUTINE)(void *)svcctl_RChangeServiceConfig2A_s,
    (SERVER_ROUTINE)(void *)svcctl_RChangeServiceConfig2W_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceConfig2A_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceConfig2W_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceStatusEx_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumServicesStatusExA_s,
    (SERVER_ROUTINE)(void *)svcctl_REnumServicesStatusExW_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScQueryServiceTagInfo_s,
    (SERVER_ROUTINE)(void *)svcctl_RCreateServiceWOW64A_s,
    (SERVER_ROUTINE)(void *)svcctl_RCreateServiceWOW64W_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScQueryServiceTagInfoEx_s,
    (SERVER_ROUTINE)(void *)svcctl_RNotifyServiceStatusChange_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetNotifyResults_s,
    (SERVER_ROUTINE)(void *)svcctl_RCloseNotifyHandle_s,
    (SERVER_ROUTINE)(void *)svcctl_RControlServiceExA_s,
    (SERVER_ROUTINE)(void *)svcctl_RControlServiceExW_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScSendPnPMessage_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScValidatePnPService_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScOpenServiceStatusHandle_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScQueryServiceConfig_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryServiceConfigEx_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScRegisterPreshutdownRestart_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScReparseServiceDatabase_s,
    (SERVER_ROUTINE)(void *)svcctl_RQueryUserServiceName_s,
    (SERVER_ROUTINE)(void *)svcctl_RCreateWowService_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceRegistryStateKey_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceDirectory_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetServiceProcessToken_s,
    (SERVER_ROUTINE)(void *)svcctl_ROpenSCManager2_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetSharedServiceRegistryStateKey_s,
    (SERVER_ROUTINE)(void *)svcctl_RGetSharedServiceDirectory_s,
    (SERVER_ROUTINE)(void *)svcctl_RReparseServiceConfig_s,
    (SERVER_ROUTINE)(void *)svcctl_RI_ScSendSynchronousPowerMessage_s
};

int (*svcctl_hook)(PRPC_MESSAGE);

static void __RPC_STUB svcctl_dispatch_0(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_1(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_2(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_3(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_4(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_5(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_6(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_7(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_8(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_9(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_10(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_11(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_12(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_13(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_14(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_15(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_16(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_17(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_18(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_19(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_20(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_21(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_22(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_23(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_24(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_25(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_26(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_27(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_28(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_29(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_30(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_31(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_32(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_33(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_34(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_35(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_36(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_37(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_38(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_39(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_40(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_41(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_42(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_43(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_44(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_45(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_46(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_47(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_48(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_49(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_50(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_51(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_52(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_53(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_54(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_55(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_56(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_57(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_58(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_59(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_60(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_61(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_62(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_63(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_64(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_65(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_66(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_67(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB svcctl_dispatch_68(PRPC_MESSAGE m)
{
    if (svcctl_hook && svcctl_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION svcctl_DispatchFunctions[] = {
svcctl_dispatch_0, svcctl_dispatch_1, svcctl_dispatch_2, svcctl_dispatch_3, svcctl_dispatch_4, svcctl_dispatch_5, svcctl_dispatch_6, svcctl_dispatch_7, svcctl_dispatch_8, svcctl_dispatch_9, svcctl_dispatch_10, svcctl_dispatch_11, svcctl_dispatch_12, svcctl_dispatch_13, svcctl_dispatch_14, svcctl_dispatch_15, svcctl_dispatch_16, svcctl_dispatch_17, svcctl_dispatch_18, svcctl_dispatch_19, svcctl_dispatch_20, svcctl_dispatch_21, svcctl_dispatch_22, svcctl_dispatch_23, svcctl_dispatch_24, svcctl_dispatch_25, svcctl_dispatch_26, svcctl_dispatch_27, svcctl_dispatch_28, svcctl_dispatch_29, svcctl_dispatch_30, svcctl_dispatch_31, svcctl_dispatch_32, svcctl_dispatch_33, svcctl_dispatch_34, svcctl_dispatch_35, svcctl_dispatch_36, svcctl_dispatch_37, svcctl_dispatch_38, svcctl_dispatch_39, svcctl_dispatch_40, svcctl_dispatch_41, svcctl_dispatch_42, svcctl_dispatch_43, svcctl_dispatch_44, svcctl_dispatch_45, svcctl_dispatch_46, svcctl_dispatch_47, svcctl_dispatch_48, svcctl_dispatch_49, svcctl_dispatch_50, svcctl_dispatch_51, svcctl_dispatch_52, svcctl_dispatch_53, svcctl_dispatch_54, svcctl_dispatch_55, svcctl_dispatch_56, svcctl_dispatch_57, svcctl_dispatch_58, svcctl_dispatch_59, svcctl_dispatch_60, svcctl_dispatch_61, svcctl_dispatch_62, svcctl_dispatch_63, svcctl_dispatch_64, svcctl_dispatch_65, svcctl_dispatch_66, svcctl_dispatch_67, svcctl_dispatch_68
};
static RPC_DISPATCH_TABLE svcctl_DispatchTable = { 69, svcctl_DispatchFunctions, 0 };
static MIDL_SERVER_INFO svcctl_ServerInfo = {
    .pStubDesc = &svcctl_StubDesc,
    .DispatchTable = svcctl_ServerRoutineTable,
    .ProcString = svcctl_ProcFormat,
    .FmtStringOffset = svcctl_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE svcctl_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x367abb81, 0x9844, 0x35f1, {0xad, 0x32, 0x98, 0xf0, 0x38, 0x00, 0x10, 0x03}}, {2, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &svcctl_DispatchTable, 0, 0, 0, &svcctl_ServerInfo, 0 };
RPC_IF_HANDLE svcctl_s_ifspec = (RPC_IF_HANDLE)&svcctl_s_ifspec_data;
#endif

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCloseServiceHandle(SC_RPC_HANDLE *hSCObject)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[0], hSCObject).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE *hSCObject;
    } frame = { hSCObject };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RControlService(SC_RPC_HANDLE hService, unsigned long dwControl, SC_SERVICE_STATUS *lpServiceStatus)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[1], hService, dwControl, lpServiceStatus).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hService;
        unsigned long dwControl;
        SC_SERVICE_STATUS *lpServiceStatus;
    } frame = { hService, dwControl, lpServiceStatus };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RDeleteService(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[2], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RLockServiceDatabase(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[3], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceObjectSecurity(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[4], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RSetServiceObjectSecurity(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[5], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceStatus(SC_RPC_HANDLE hService, SC_SERVICE_STATUS *lpServiceStatus)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[6], hService, lpServiceStatus).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hService;
        SC_SERVICE_STATUS *lpServiceStatus;
    } frame = { hService, lpServiceStatus };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RSetServiceStatus(SC_RPC_HANDLE hServiceStatus, SC_SERVICE_STATUS *lpServiceStatus)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[7], hServiceStatus, lpServiceStatus).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hServiceStatus;
        SC_SERVICE_STATUS *lpServiceStatus;
    } frame = { hServiceStatus, lpServiceStatus };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[7], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RUnlockServiceDatabase(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[8], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RNotifyBootConfigStatus(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[9], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[9], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScSetServiceBitsW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[10], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[10], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RChangeServiceConfigW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[11], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[11], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCreateServiceW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[12], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[12], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumDependentServicesW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[13], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[13], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumServicesStatusW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[14], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[14], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_ROpenSCManagerW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[15], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[15], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_ROpenServiceW(SC_RPC_HANDLE hSCManager, unsigned short *lpServiceName, unsigned long dwDesiredAccess, SC_RPC_HANDLE *lpServiceHandle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[16], hSCManager, lpServiceName, dwDesiredAccess, lpServiceHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hSCManager;
        unsigned short *lpServiceName;
        unsigned long dwDesiredAccess;
        SC_RPC_HANDLE *lpServiceHandle;
    } frame = { hSCManager, lpServiceName, dwDesiredAccess, lpServiceHandle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[16], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceConfigW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[17], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[17], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceLockStatusW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[18], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[18], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RStartServiceW(SC_RPC_HANDLE hService, unsigned long argc, SC_STRING_PTRW *argv)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[19], hService, argc, argv).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hService;
        unsigned long argc;
        SC_STRING_PTRW *argv;
    } frame = { hService, argc, argv };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[19], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceDisplayNameW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[20], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[20], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceKeyNameW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[21], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[21], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScSetServiceBitsA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[22], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[22], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RChangeServiceConfigA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[23], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[23], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCreateServiceA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[24], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[24], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumDependentServicesA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[25], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[25], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumServicesStatusA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[26], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[26], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_ROpenSCManagerA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[27], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[27], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_ROpenServiceA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[28], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[28], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceConfigA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[29], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[29], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceLockStatusA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[30], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[30], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RStartServiceA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[31], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[31], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceDisplayNameA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[32], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[32], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceKeyNameA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[33], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[33], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScGetCurrentGroupStateW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[34], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[34], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumServiceGroupW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[35], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[35], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RChangeServiceConfig2A(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[36], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[36], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RChangeServiceConfig2W(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[37], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[37], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceConfig2A(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[38], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[38], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceConfig2W(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[39], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[39], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceStatusEx(SC_RPC_HANDLE hService, unsigned long InfoLevel, byte *lpBuffer, unsigned long cbBufSize, unsigned long *pcbBytesNeeded)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[40], hService, InfoLevel, lpBuffer, cbBufSize, pcbBytesNeeded).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hService;
        unsigned long InfoLevel;
        byte *lpBuffer;
        unsigned long cbBufSize;
        unsigned long *pcbBytesNeeded;
    } frame = { hService, InfoLevel, lpBuffer, cbBufSize, pcbBytesNeeded };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[40], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumServicesStatusExA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[41], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[41], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_REnumServicesStatusExW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[42], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[42], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScQueryServiceTagInfo(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[43], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[43], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCreateServiceWOW64A(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[44], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[44], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCreateServiceWOW64W(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[45], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[45], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScQueryServiceTagInfoEx(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[46], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[46], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RNotifyServiceStatusChange(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[47], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[47], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetNotifyResults(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[48], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[48], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCloseNotifyHandle(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[49], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[49], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RControlServiceExA(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[50], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[50], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RControlServiceExW(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[51], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[51], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScSendPnPMessage(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[52], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[52], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScValidatePnPService(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[53], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[53], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScOpenServiceStatusHandle(SC_RPC_HANDLE hSCManager, unsigned short *lpServiceName, long long StatusId, SC_RPC_HANDLE *lpStatusHandle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[54], hSCManager, lpServiceName, StatusId, lpStatusHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_RPC_HANDLE hSCManager;
        unsigned short *lpServiceName;
        long long StatusId;
        SC_RPC_HANDLE *lpStatusHandle;
    } frame = { hSCManager, lpServiceName, StatusId, lpStatusHandle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[54], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScQueryServiceConfig(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[55], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[55], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryServiceConfigEx(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[56], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[56], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScRegisterPreshutdownRestart(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[57], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[57], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScReparseServiceDatabase(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[58], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[58], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RQueryUserServiceName(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[59], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[59], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RCreateWowService(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[60], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[60], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceRegistryStateKey(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[61], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[61], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceDirectory(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[62], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[62], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetServiceProcessToken(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[63], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[63], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_ROpenSCManager2(handle_t BindingHandle, unsigned short *DatabaseName, unsigned long DesiredAccess, SC_RPC_HANDLE *ScmHandle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[64], BindingHandle, DatabaseName, DesiredAccess, ScmHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t BindingHandle;
        unsigned short *DatabaseName;
        unsigned long DesiredAccess;
        SC_RPC_HANDLE *ScmHandle;
    } frame = { BindingHandle, DatabaseName, DesiredAccess, ScmHandle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[64], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetSharedServiceRegistryStateKey(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[65], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[65], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RGetSharedServiceDirectory(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[66], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[66], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RReparseServiceConfig(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[67], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[67], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI svcctl_RI_ScSendSynchronousPowerMessage(handle_t Binding)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[68], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&svcctl_StubDesc, svcctl_ProcFormat + svcctl_FormatStringOffsetTable[68], (unsigned char *)&frame).Simple;
#endif
}

/* ------------------------------------------------------------------ interface scchannel */

#ifndef SCCHANNEL_CLIENT_ONLY
extern long WINAPI scchannel_ROpenServiceChannelHandle_s(handle_t Binding, char Reserved, SC_CHANNEL_HANDLE *Channel);
extern error_status_t WINAPI scchannel_RSendResponseReceiveControls_s(SC_CHANNEL_HANDLE Channel, SC_CONTROL_RESPONSE *Response, unsigned short **ServiceName, SC_CONTROL_MESSAGE *Message);
extern long WINAPI scchannel_RCloseServiceChannelHandle_s(SC_CHANNEL_HANDLE *Channel);
#endif
void WINAPI SC_CHANNEL_HANDLE_rundown(void *);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char scchannel_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x30, 0xa0, 0x00, 0x00, /* 2: context handle SC_CHANNEL_HANDLE */
    0x30, 0x40, 0x00, 0x00, /* 6: context handle SC_CHANNEL_HANDLE */
    0x1a, 0x07, 0x20, 0x00, 0x00, 0x00, 0x09, 0x00, 0x08, 0x40, 0x0b, 0x08, /* 10: struct SC_CONTROL_RESPONSE (BOGUS) */
    0x08, 0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1b, 0x03, 0x04, 0x00, 0x18, 0x00, 0x14, 0x00, 0x01, 0x00, 0x08, 0x5b, /* 29: array */
    0x11, 0x14, 0x02, 0x00, /* 41: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 45: pointer */
    0x1a, 0x07, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x40, 0x0b, 0x08, /* 49: struct SC_CONTROL_MESSAGE (BOGUS) */
    0x08, 0x08, 0x08, 0x08, 0x4c, 0x04, 0x03, 0x00, 0x5b,
    0x2b, 0x08, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0x02, 0x00, 0x08, 0x00, /* 70: union SC_CONTROL_PAYLOAD */
    0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x02, 0x00, 0x00, 0x00, 0x3a, 0x00, 0xff, 0xff,
    0x12, 0x08, 0x08, 0x5c, /* 104: pointer */
    0x12, 0x00, 0x02, 0x00, /* 108: pointer */
    0x1a, 0x03, 0x08, 0x00, 0x07, 0x00, 0x00, 0x00, 0x08, 0x40, 0x5b, /* 112: struct SC_CONTROL_ARGUMENTS (BOGUS) */
    0x21, 0x03, 0x00, 0x00, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0xff, 0xff, /* 123: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x36, 0x5b, 0x12, 0x08, /* 144: struct SC_STRING_PTRW (BOGUS) */
    0x25, 0x5c,
    0x12, 0x00, 0x02, 0x00, /* 158: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 162: struct SC_CONTROL_EVENT (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 178: array */
    0x30, 0xe0, 0x00, 0x00, /* 190: context handle SC_CHANNEL_HANDLE */
#else
    0x00, 0x00, /* 0: pad */
    0x30, 0xa0, 0x00, 0x00, /* 2: context handle SC_CHANNEL_HANDLE */
    0x30, 0x40, 0x00, 0x00, /* 6: context handle SC_CHANNEL_HANDLE */
    0x16, 0x07, 0x20, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x18, 0x00, 0x18, 0x00, /* 10: struct SC_CONTROL_RESPONSE (PSTRUCT) */
    0x12, 0x00, 0x0b, 0x00, 0x5b, 0x08, 0x40, 0x0b, 0x08, 0x08, 0x36, 0x40,
    0x5b,
    0x1b, 0x03, 0x04, 0x00, 0x18, 0x00, 0x14, 0x00, 0x01, 0x00, 0x08, 0x5b, /* 35: array */
    0x11, 0x14, 0x02, 0x00, /* 47: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 51: pointer */
    0x1a, 0x07, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x40, 0x0b, 0x08, /* 55: struct SC_CONTROL_MESSAGE (BOGUS) */
    0x08, 0x08, 0x08, 0x08, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x2b, 0x08, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00, /* 76: union SC_CONTROL_PAYLOAD */
    0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x02, 0x00, 0x00, 0x00, 0x4b, 0x00, 0xff, 0xff,
    0x12, 0x08, 0x08, 0x5c, /* 110: pointer */
    0x12, 0x00, 0x02, 0x00, /* 114: pointer */
    0x1a, 0x03, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x08, 0x5b, /* 118: struct SC_CONTROL_ARGUMENTS (BOGUS) */
    0x1b, 0x03, 0x04, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x4b, 0x5c, /* 128: array */
    0x48, 0x49, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x04, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 162: struct SC_STRING_PTRW (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 181: pointer */
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 185: struct SC_CONTROL_EVENT (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 206: array */
    0x30, 0xe0, 0x00, 0x00, /* 218: context handle SC_CHANNEL_HANDLE */
#endif
};

static const unsigned char scchannel_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x01, 0x00, /* 2: scchannel.ROpenServiceChannelHandle */
    0x1f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x00, 0x08, 0x00, 0x02, 0x5c, 0x10, 0x01, 0x10, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x28, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 46: scchannel.RSendResponseReceiveControls */
    0x17, 0x00, 0x07, 0x00, 0x47, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0b, 0x01,
    0x08, 0x00, 0x0a, 0x00, 0x13, 0x20, 0x10, 0x00, 0x29, 0x00, 0x13, 0xc1,
    0x18, 0x00, 0x31, 0x00, 0x70, 0x00, 0x20, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x10, 0x00, 0x30, 0xe0, 0x00, 0x00, 0x00, 0x00, /* 104: scchannel.RCloseServiceChannelHandle */
    0x17, 0x00, 0x1f, 0x00, 0x44, 0x02, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0xbe, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x01, 0x00, /* 2: scchannel.ROpenServiceChannelHandle */
    0x1f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x04, 0x00, 0x02, 0x5c, 0x10, 0x01, 0x08, 0x00, 0x02, 0x00,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x14, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 44: scchannel.RSendResponseReceiveControls */
    0x17, 0x00, 0x07, 0x00, 0x47, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0b, 0x01, 0x04, 0x00,
    0x0a, 0x00, 0x13, 0x20, 0x08, 0x00, 0x2f, 0x00, 0x13, 0xa1, 0x0c, 0x00,
    0x37, 0x00, 0x70, 0x00, 0x10, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x08, 0x00, 0x30, 0xe0, 0x00, 0x00, 0x00, 0x00, /* 100: scchannel.RCloseServiceChannelHandle */
    0x17, 0x00, 0x1f, 0x00, 0x44, 0x02, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0xda, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short scchannel_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 46, 104
#else
    2, 44, 100
#endif
};

static const NDR_RUNDOWN scchannel_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)SC_CHANNEL_HANDLE_rundown
};

static RPC_CLIENT_INTERFACE scchannel_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0xa2c45f7c, 0x7d32, 0x46ad, {0x96, 0xf5, 0xad, 0xaf, 0xb4, 0x86, 0xbe, 0x74}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE scchannel_c_ifspec = (RPC_IF_HANDLE)&scchannel_c_ifspec_data;

static MIDL_STUB_DESC scchannel_StubDesc = {
    .RpcInterfaceInformation = &scchannel_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = scchannel_RundownRoutines,
    .pFormatTypes = scchannel_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef SCCHANNEL_CLIENT_ONLY
static const SERVER_ROUTINE scchannel_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)scchannel_ROpenServiceChannelHandle_s,
    (SERVER_ROUTINE)(void *)scchannel_RSendResponseReceiveControls_s,
    (SERVER_ROUTINE)(void *)scchannel_RCloseServiceChannelHandle_s
};

int (*scchannel_hook)(PRPC_MESSAGE);

static void __RPC_STUB scchannel_dispatch_0(PRPC_MESSAGE m)
{
    if (scchannel_hook && scchannel_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB scchannel_dispatch_1(PRPC_MESSAGE m)
{
    if (scchannel_hook && scchannel_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB scchannel_dispatch_2(PRPC_MESSAGE m)
{
    if (scchannel_hook && scchannel_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION scchannel_DispatchFunctions[] = {
scchannel_dispatch_0, scchannel_dispatch_1, scchannel_dispatch_2
};
static RPC_DISPATCH_TABLE scchannel_DispatchTable = { 3, scchannel_DispatchFunctions, 0 };
static MIDL_SERVER_INFO scchannel_ServerInfo = {
    .pStubDesc = &scchannel_StubDesc,
    .DispatchTable = scchannel_ServerRoutineTable,
    .ProcString = scchannel_ProcFormat,
    .FmtStringOffset = scchannel_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE scchannel_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0xa2c45f7c, 0x7d32, 0x46ad, {0x96, 0xf5, 0xad, 0xaf, 0xb4, 0x86, 0xbe, 0x74}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &scchannel_DispatchTable, 0, 0, 0, &scchannel_ServerInfo, 0 };
RPC_IF_HANDLE scchannel_s_ifspec = (RPC_IF_HANDLE)&scchannel_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI scchannel_ROpenServiceChannelHandle(handle_t Binding, char Reserved, SC_CHANNEL_HANDLE *Channel)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&scchannel_StubDesc, scchannel_ProcFormat + scchannel_FormatStringOffsetTable[0], Binding, Reserved, Channel).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
        long Reserved;
        SC_CHANNEL_HANDLE *Channel;
    } frame = { Binding, Reserved, Channel };
#pragma pack(pop)
    return (long)NdrClientCall2(&scchannel_StubDesc, scchannel_ProcFormat + scchannel_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI scchannel_RSendResponseReceiveControls(SC_CHANNEL_HANDLE Channel, SC_CONTROL_RESPONSE *Response, unsigned short **ServiceName, SC_CONTROL_MESSAGE *Message)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&scchannel_StubDesc, scchannel_ProcFormat + scchannel_FormatStringOffsetTable[1], Channel, Response, ServiceName, Message).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_CHANNEL_HANDLE Channel;
        SC_CONTROL_RESPONSE *Response;
        unsigned short **ServiceName;
        SC_CONTROL_MESSAGE *Message;
    } frame = { Channel, Response, ServiceName, Message };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&scchannel_StubDesc, scchannel_ProcFormat + scchannel_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI scchannel_RCloseServiceChannelHandle(SC_CHANNEL_HANDLE *Channel)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&scchannel_StubDesc, scchannel_ProcFormat + scchannel_FormatStringOffsetTable[2], Channel).Simple;
#else
#pragma pack(push, 4)
    struct {
        SC_CHANNEL_HANDLE *Channel;
    } frame = { Channel };
#pragma pack(pop)
    return (long)NdrClientCall2(&scchannel_StubDesc, scchannel_ProcFormat + scchannel_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

#endif
