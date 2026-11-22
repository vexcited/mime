/*
 * nsisvc.dll: the network store interface service. on windows it serves the store over rpc to the callers that may
 * not open the device, and hands them the change notifications netio.sys reports to it from the kernel. here the
 * interface winnsi.dll calls is the same, see rpc.c, and the changes are found by the service itself: it waits on the
 * device for a table to change, as any program may, and compares the table before and after.
 */
#include "nsisvc_private.h"

WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);

typedef DWORD(WINAPI *REGISTER_STOP)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback, void *context,
                                     DWORD flags);

/* the host's block, from the first sid on, as svchost pushes it. */
static void **g_globals;
#define GLOBALS_REGISTER_STOP 24

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop_event, g_stop_wait;
static BOOL g_rpc_registered;

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

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface_spec, void *context)
{
    return RPC_S_OK;
}

/*
 * the endpoint is a dynamic one of ncalrpc that the endpoint mapper tells, with the annotation and the security
 * descriptor of windows' service: everyone and the restricted read, write and execute, the app containers read.
 */
static DWORD start_rpc(void)
{
    static const WCHAR sddl[] = L"D:(A;;GRGWGX;;;WD)(A;;GRGWGX;;;RC)(A;;GA;;;BA)(A;;GA;;;OW)(A;;GR;;;AC)(A;;GR;;;S-1-15-3-1)"
                                L"(A;;GR;;;S-1-15-3-2)(A;;GR;;;S-1-15-3-3)"
                                L"(A;;GR;;;S-1-15-3-4214768333-1334025770-122408079-3919188833)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, &sd, nullptr))
        return GetLastError();
    RPC_STATUS status = RpcServerUseProtseqW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, sd);
    if (status == RPC_S_OK) {
        status = RpcServerRegisterIf3(nsirpc_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN, 1234, 0, allow_local, sd);
        if (status == RPC_S_TYPE_ALREADY_REGISTERED)
            status = RPC_S_OK;
    }
    RPC_BINDING_VECTOR *bindings = nullptr;
    if (status == RPC_S_OK) {
        g_rpc_registered = TRUE;
        status = RpcServerInqBindings(&bindings);
    }
    if (status == RPC_S_OK)
        status = RpcEpRegisterW(nsirpc_s_ifspec, bindings, nullptr, (RPC_WSTR)L"NSI server endpoint");
    if (bindings)
        RpcBindingVectorFree(&bindings);
    LocalFree(sd);
    return status;
}

/* the stop event fired: the waiting calls end, the interface goes, then the service is down. */
static void WINAPI cleanup(void *context, BOOLEAN timed_out)
{
    NsiSvcStopNotifications();
    if (g_rpc_registered) {
        RpcServerUnregisterIf(nsirpc_s_ifspec, nullptr, 1);
        g_rpc_registered = FALSE;
    }
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
    g_handle = RegisterServiceCtrlHandlerExW(L"NSI", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    DWORD error = ERROR_SUCCESS;
    g_stop_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_stop_event)
        error = GetLastError();
    else if (!g_globals)
        error = ERROR_INVALID_DATA;
    if (!error)
        error = NsiSvcStartNotifications();
    if (!error)
        error = start_rpc();
    if (!error)
        error = ((REGISTER_STOP)g_globals[GLOBALS_REGISTER_STOP])(&g_stop_wait, L"NSI", g_stop_event, (void *)cleanup,
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
