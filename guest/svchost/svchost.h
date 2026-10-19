/*
 * svchost.exe, the process that hosts services which live in DLLs. see docs/services.md for what it does and
 * where it follows windows' svchost.exe of build 26300 and where it is a guess.
 *
 * a command line `svchost.exe -k <group>` names a group of services in the registry. the process offers every
 * service of the group to the service control manager and loads the ServiceDll of one when the manager starts it.
 */
#ifndef MIME_SVCHOST_H
#define MIME_SVCHOST_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/winactctx.h>

WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);

typedef struct HOSTED_DLL {
    WCHAR *Path;     /* the ServiceDll, expanded and in lower case */
    WCHAR *Manifest; /* the ServiceManifest, empty when there is none */
    HMODULE Module;  /* null while the dll is not loaded */
    HANDLE ActCtx;   /* the activation context of the manifest, null for none */
} HOSTED_DLL;

typedef void(WINAPI *SVCHOST_STOP_CALLBACK)(void *context, BOOLEAN timed_out);

typedef struct HOSTED {
    WCHAR *Name;
    HOSTED_DLL *Dll;
    CHAR *MainName;       /* the ServiceMain value, null for the default */
    SRWLOCK Lock;         /* guards everything below and the dll */
    CONDITION_VARIABLE Unloaded;
    SVCHOST_STOP_CALLBACK StopCallback;
    LONG Refs;            /* the services main, and a registered stop callback, each hold the dll */
    BOOL UnloadOnStop;
} HOSTED;

/* what the command line and the group key give. */
typedef struct HOST_OPTIONS {
    WCHAR *Line;    /* the writable copy the pieces below point into */
    WCHAR *Group;   /* -k */
    WCHAR *Service; /* -s, one service of the group */
    BOOL Mitigation; /* -p */
    /* values of the key of the group. */
    BOOL SecurityParam, SystemCritical;
    DWORD AuthenticationLevel, ImpersonationLevel, AuthenticationCapabilities;
    DWORD Flags;
    PSECURITY_DESCRIPTOR AccessSd;
    DWORD DynamicCode, BinarySignature, RedirectionTrust, ExtensionPoints;
    WCHAR *Names; /* a multi sz, the services of the group */
} HOST_OPTIONS;

#define HOST_ALLOW_LOWBOX 0x02
#define HOST_NO_GUI 0x08
#define HOST_ALLOW_INTERACTIVE 0x10
#define HOST_ALLOW_COM_CAPABILITY 0x20
#define HOST_ALLOW_CROSS_CONTAINER 0x40

/* main.c */
extern HOSTED *g_hosted;
extern DWORD g_hosted_count;
extern BOOL g_com_uninit;
void HostLog(const char *format, ...);
WCHAR *HostDup(const WCHAR *s);

/* host.c */
VOID WINAPI HostService(DWORD argc, LPWSTR *argv);
void HostWaitForUnloads(void);
void HostAbort(const WCHAR *name, DWORD error);

#endif
