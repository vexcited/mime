/*
 * keyiso.dll: the key isolation service of cng. On Windows it keeps private keys in the process of the security authority
 * and hands them out over a port; here the keys of ncrypt live in the process that uses them, so this service only has the
 * states that others depend on, and a stop that goes through the same steps: it starts as the service does, runs taking a
 * stop and an interrogation, and answers every other control with the error of an unknown one. it is hosted by svchost
 * since this system has no process of the security authority.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>

typedef DWORD(WINAPI *REGISTER_STOP)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback, void *context,
                                     DWORD flags);

/* the host's block, from the first sid on, as svchost pushes it. */
static void **g_globals;
#define GLOBALS_REGISTER_STOP 24

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop_event, g_stop_wait;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void WINAPI SvchostPushServiceGlobals(void **globals)
{
    g_globals = globals;
}

static void update_status(void)
{
    SetServiceStatus(g_handle, &g_status);
}

static DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    if (control == SERVICE_CONTROL_INTERROGATE)
        return NO_ERROR;
    if (control != SERVICE_CONTROL_STOP)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (g_status.dwCurrentState != SERVICE_STOP_PENDING) {
        g_status.dwCurrentState = SERVICE_STOP_PENDING;
        g_status.dwCheckPoint = 1;
        g_status.dwWaitHint = 60000;
        update_status();
        SetEvent(g_stop_event);
    }
    return NO_ERROR;
}

/* the stop event fired: the service is down. */
static void WINAPI cleanup(void *context, BOOLEAN timed_out)
{
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwControlsAccepted = 0;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    update_status();
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwCheckPoint = 1;
    g_status.dwWaitHint = 5000;
    g_handle = RegisterServiceCtrlHandlerExW(L"KeyIso", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    DWORD error = ERROR_SUCCESS;
    g_stop_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_stop_event)
        error = GetLastError();
    else if (!g_globals)
        error = ERROR_INVALID_DATA;
    else
        error = ((REGISTER_STOP)g_globals[GLOBALS_REGISTER_STOP])(&g_stop_wait, L"KeyIso", g_stop_event, (void *)cleanup,
                                                                  nullptr, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION);
    if (error) {
        g_status.dwCurrentState = SERVICE_STOP_PENDING;
        g_status.dwCheckPoint = 1;
        g_status.dwWaitHint = 60000;
        g_status.dwWin32ExitCode = error;
        g_status.dwServiceSpecificExitCode = error;
        cleanup(nullptr, FALSE);
        return;
    }
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    update_status();
}
