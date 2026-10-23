/*
 * svchost.exe hosts services that live in DLLs.
 *
 * `svchost.exe -k <group>` names a group of services in the registry. the process offers every service of the group
 * to the service control manager and loads a service's ServiceDll when the manager starts it.
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

/* the lock guards every field from `module` on. */
typedef struct HOST_SERVICE {
    WCHAR *name;
    SRWLOCK lock;
    CONDITION_VARIABLE unloaded; /* woken when the DLL is freed. */
    WCHAR *dll_path;             /* null until the first start, then kept. */
    WCHAR *manifest_path;
    CHAR *main_export;           /* the key's ServiceMain value, null for the default name. */
    HMODULE module;
    HANDLE context;              /* the manifest's activation context, or null. */
    LONG users;                  /* the running ServiceMain and a registered stop callback each hold the DLL. */
    BOOL unload_on_stop;
    HOST_STOP_FN stop_fn;        /* the one registered callback, or null. */
    void *stop_arg;
} HOST_SERVICE;

/* the strings point into the copy of the line. */
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
