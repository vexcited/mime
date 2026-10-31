/*
 * the winmgmt service. the engine runs in the process of the program that asks, from the repository file all of them
 * share, so the service has no requests to answer: it is the state programs look for, started with the machine and
 * running until it is stopped.
 */
#include "wbemcore.h"
#include <mime/win32/winsvc.h>

typedef DWORD(WINAPI *REGISTER_STOP)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback, void *context,
                                     DWORD flags);

/* the block svchost pushes, from the first sid on, and the places the service reads in it. */
#define GLOBALS_REGISTER_STOP 24
static void **g_globals;

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop_event, g_stop_wait;

DECLSPEC_EXPORT void WINAPI SvchostPushServiceGlobals(void **globals)
{
    g_globals = globals;
}

static DWORD update_status(void)
{
    if (!g_handle)
        return ERROR_INVALID_HANDLE;
    return SetServiceStatus(g_handle, &g_status) ? ERROR_SUCCESS : GetLastError();
}

static DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_status.dwCurrentState != SERVICE_STOP_PENDING) {
            g_status.dwCurrentState = SERVICE_STOP_PENDING;
            g_status.dwCheckPoint = 1;
            g_status.dwWaitHint = 30000;
            update_status();
            SetEvent(g_stop_event);
            return NO_ERROR;
        }
        break;
    case SERVICE_CONTROL_INTERROGATE:
        break;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
    update_status();
    return NO_ERROR;
}

static void WINAPI terminated(void *context, BOOLEAN timed_out)
{
    g_status.dwControlsAccepted = 0;
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    update_status();
}

DECLSPEC_EXPORT void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwCheckPoint = 1;
    g_status.dwWaitHint = 30000;
    g_handle = RegisterServiceCtrlHandlerExW(L"Winmgmt", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop_event) {
        g_status.dwWin32ExitCode = GetLastError();
        terminated(nullptr, FALSE);
        return;
    }
    /* the repository is read once, so a damaged file shows when the service starts. */
    wr_lock();
    wr_unlock();
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    DWORD error = update_status();
    if (!error && g_globals)
        error = ((REGISTER_STOP)g_globals[GLOBALS_REGISTER_STOP])(&g_stop_wait, L"Winmgmt", g_stop_event, (void *)terminated,
                                                                  nullptr, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION);
    else if (!error)
        error = ERROR_INVALID_DATA;
    if (error) {
        g_status.dwWin32ExitCode = error;
        terminated(nullptr, FALSE);
    }
}
