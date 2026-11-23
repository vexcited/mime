#include "cryptsvc_private.h"

WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);

typedef DWORD(WINAPI *REGISTER_STOP)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback, void *context,
                                     DWORD flags);

/* the block svchost pushes, from the first sid on, and the places the service reads in it. */
#define GLOBALS_REGISTER_STOP 24
static void **g_globals;

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop_event, g_stop_wait;
static BOOL g_rpc_registered;

#define ACCEPTED_CONTROLS (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void WINAPI SvchostPushServiceGlobals(void **globals)
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
        }
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        update_status();
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/*
 * crypt32 binds to ncalrpc:[keysvc] and only to a server that runs as the network service, which is why the
 * service lives in svchost -k NetworkService as on windows. every local caller connects, app containers too.
 */
static DWORD start_rpc(void)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)(A;;GA;;;AC)", 1, &sd, nullptr);
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"keysvc", sd);
    if (status == RPC_S_DUPLICATE_ENDPOINT)
        status = RPC_S_OK;
    if (status == RPC_S_OK)
        status = RpcServerRegisterIf3(keysvc_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local, sd);
    if (sd)
        LocalFree(sd);
    if (status == RPC_S_OK)
        g_rpc_registered = TRUE;
    return status;
}

/* the stop event fired: the interface goes, then the service is down. */
static void WINAPI terminated(void *context, BOOLEAN timed_out)
{
    if (g_rpc_registered) {
        RpcServerUnregisterIf(keysvc_s_ifspec, nullptr, 1);
        g_rpc_registered = FALSE;
    }
    g_status.dwControlsAccepted = 0;
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    update_status();
}

static void fail(DWORD error)
{
    g_status.dwWin32ExitCode = error;
    g_status.dwServiceSpecificExitCode = error;
    terminated(nullptr, FALSE);
}

void WINAPI CryptServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwCheckPoint = 1;
    g_status.dwWaitHint = 30000;
    g_handle = RegisterServiceCtrlHandlerExW(L"CryptSvc", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop_event) {
        fail(GetLastError());
        return;
    }
    DWORD error = start_rpc();
    if (error) {
        fail(error);
        return;
    }
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = ACCEPTED_CONTROLS;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    error = update_status();
    if (!error && g_globals)
        error = ((REGISTER_STOP)g_globals[GLOBALS_REGISTER_STOP])(&g_stop_wait, L"CryptSvc", g_stop_event,
                                                                  (void *)terminated, nullptr,
                                                                  WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION);
    else if (!error)
        error = ERROR_INVALID_DATA;
    if (error)
        fail(error);
}
