#include "ngcsvc_private.h"

#define RPC_C_AUTHN_WINNT 10
#define RPC_IF_ALLOW_SECURE_ONLY_ 0x0008
#define RPC_IF_ALLOW_LOCAL_ONLY_ 0x0020

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static BOOL g_rpc_registered;
static HANDLE g_stop_event;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

static void update_status(void)
{
    if (g_handle)
        SetServiceStatus(g_handle, &g_status);
}

static RPC_STATUS endpoints(BOOL add)
{
    RPC_BINDING_VECTOR *bindings = nullptr;
    RPC_STATUS status = RpcServerInqBindings(&bindings);
    if (status)
        return status;
    status = add ? RpcEpRegisterW(NgcKsp_s_ifspec, bindings, nullptr, (RPC_WSTR)L"NGC Service KSP RPC Interface")
                 : RpcEpUnregister(NgcKsp_s_ifspec, bindings, nullptr);
    RpcBindingVectorFree(&bindings);
    return status;
}

/*
 * as NgcServiceStartRpcServer of windows 11 26300 for the key storage interface: local callers that authenticated,
 * 1234 calls at most, a dynamic endpoint of ncalrpc the endpoint mapper tells, and a descriptor that lets everyone
 * and the application packages call. windows makes an interface group of this and seven other interfaces, which
 * mime does not serve.
 */
static RPC_STATUS start_rpc(void)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:SYD:(A;;GRGWGX;;;WD)(A;;GRGWGX;;;AC)(A;;GRGWGX;;;S-1-15-3-9)", SDDL_REVISION_1, &sd, nullptr))
        return GetLastError();
    RPC_STATUS status = RpcServerRegisterIf3(NgcKsp_s_ifspec, nullptr, nullptr,
                                             RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_SECURE_ONLY_ | RPC_IF_ALLOW_LOCAL_ONLY_,
                                             1234, (unsigned int)-1, nullptr, sd);
    if (!status) {
        g_rpc_registered = TRUE;
        status = RpcServerUseProtseqW((RPC_WSTR)L"ncalrpc", 10, sd);
    }
    LocalFree(sd);
    if (!status)
        status = endpoints(TRUE);
    RPC_WSTR principal = nullptr;
    if (!status)
        status = RpcServerInqDefaultPrincNameW(RPC_C_AUTHN_WINNT, &principal);
    if (!status)
        status = RpcServerRegisterAuthInfoW(principal, RPC_C_AUTHN_WINNT, nullptr, nullptr);
    if (principal)
        RpcStringFreeW(&principal);
    return status;
}

static void stop_rpc(void)
{
    if (!g_rpc_registered)
        return;
    endpoints(FALSE);
    RpcServerUnregisterIf(NgcKsp_s_ifspec, nullptr, 1);
    g_rpc_registered = FALSE;
}

static void stopped(DWORD error)
{
    stop_rpc();
    g_status.dwControlsAccepted = 0;
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwWin32ExitCode = error;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    update_status();
}

static DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        g_status.dwCurrentState = SERVICE_STOP_PENDING;
        update_status();
        SetEvent(g_stop_event);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        update_status();
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI NgcServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_handle = RegisterServiceCtrlHandlerExW(L"NgcSvc", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    NgcKspInit();
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    RPC_STATUS status = g_stop_event ? start_rpc() : GetLastError();
    if (status) {
        stopped(status);
        return;
    }
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    update_status();
    WaitForSingleObject(g_stop_event, INFINITE);
    stopped(NO_ERROR);
    CloseHandle(g_stop_event);
    g_stop_event = nullptr;
}
