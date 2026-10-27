/*
 * a service whose work mime does elsewhere. it only holds the state the service control manager shows, so the
 * services that depend on it can start. like the services it stands for it cannot be stopped or paused.
 */
#pragma once
#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>

typedef struct SVC_STUB {
    SERVICE_STATUS_HANDLE Handle;
    SERVICE_STATUS Status;
} SVC_STUB;

static DWORD WINAPI svc_stub_control(DWORD control, DWORD type, LPVOID data, LPVOID context)
{
    SVC_STUB *s = context;
    if (control != SERVICE_CONTROL_INTERROGATE)
        return ERROR_CALL_NOT_IMPLEMENTED;
    SetServiceStatus(s->Handle, &s->Status);
    return NO_ERROR;
}

/* reports the service running and returns: svchost keeps it. */
static void svc_stub_run(DWORD argc, LPWSTR *argv)
{
    SVC_STUB *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s || !argc)
        return;
    s->Handle = RegisterServiceCtrlHandlerExW(argv[0], svc_stub_control, s);
    if (!s->Handle) {
        HeapFree(GetProcessHeap(), 0, s);
        return;
    }
    s->Status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    s->Status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(s->Handle, &s->Status);
}
