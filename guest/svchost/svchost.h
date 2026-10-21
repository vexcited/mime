/*
 * svchost.exe, the process that hosts services which live in DLLs. docs/services.md says what it does.
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

typedef void(WINAPI *HOST_STOP_FN)(void *context, BOOLEAN timed_out);

/* one service of the group, with the DLL that runs it. the lock guards every field from `module` on. */
typedef struct HOST_SERVICE {
    WCHAR *name;
    SRWLOCK lock;
    CONDITION_VARIABLE unloaded; /* woken when the DLL has been freed */
    WCHAR *dll_path;             /* null until the first start, then kept */
    WCHAR *manifest_path;        /* null when the service has none */
    CHAR *main_export;           /* the ServiceMain value of the key, null for the default name */
    HMODULE module;              /* null while the DLL is not loaded */
    HANDLE context;              /* activation context of the manifest, null for none */
    LONG users;                  /* the running ServiceMain and a registered stop callback each hold the DLL */
    BOOL unload_on_stop;
    HOST_STOP_FN stop_fn;        /* the one registered callback, null for none */
    void *stop_arg;
} HOST_SERVICE;

/* what the command line gives. the strings point into the copy of the line. */
typedef struct HOST_OPTIONS {
    WCHAR *Line;
    WCHAR *Group;
    WCHAR *Service;
    BOOL Mitigation;
} HOST_OPTIONS;

/* main.c */
extern HOST_SERVICE *g_services;
extern DWORD g_service_count;
extern BOOL g_wait_for_unloads;
void HostLog(const char *format, ...);
WCHAR *HostDup(const WCHAR *s);

/* host.c */
VOID WINAPI HostService(DWORD argc, LPWSTR *argv);
void HostWaitForUnloads(void);

#endif
