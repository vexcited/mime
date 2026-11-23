#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>

/*
 * The key isolation service of cng where ncrypt.dll is Mime's own: the keys live in the process that uses them, so the
 * service only has the states that others depend on. It is started by the process of the security authority, whose
 * services take a third argument: a block that gets the stop callback, the event that asks for the stop and the
 * callback's argument. The export returns once the service runs.
 */
typedef struct STOP_CONTEXT {
    LONG(WINAPI *callback)(void *argument);
    HANDLE event;
    void *argument;
} STOP_CONTEXT;

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

static DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    if (control != SERVICE_CONTROL_STOP)
        return ERROR_CALL_NOT_IMPLEMENTED;
    g_status.dwCurrentState = SERVICE_STOP_PENDING;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_handle, &g_status);
    if (context)
        SetEvent(context);
    return NO_ERROR;
}

static LONG WINAPI stopped(void *argument)
{
    CloseHandle(argument);
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_handle, &g_status);
    return 0;
}

LONG WINAPI KeyIsoServiceMain(DWORD argc, LPWSTR *argv, STOP_CONTEXT *stop)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event)
        return HRESULT_FROM_WIN32(GetLastError());
    g_handle = RegisterServiceCtrlHandlerExW(L"KeyIso", control_handler, event);
    if (!g_handle) {
        LONG status = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(event);
        return status;
    }
    stop->callback = stopped;
    stop->event = event;
    stop->argument = event;
    g_status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_handle, &g_status);
    return 0;
}
