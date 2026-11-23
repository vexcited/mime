#include "bthserv_private.h"

#define RPC_C_AUTHN_WINNT 10

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
    status = add ? RpcEpRegisterW(BthServ_s_ifspec, bindings, nullptr, nullptr)
                 : RpcEpUnregister(BthServ_s_ifspec, bindings, nullptr);
    if (!status)
        status = add ? RpcEpRegisterW(BthGatt_s_ifspec, bindings, nullptr, nullptr)
                     : RpcEpUnregister(BthGatt_s_ifspec, bindings, nullptr);
    RpcBindingVectorFree(&bindings);
    return status;
}

/*
 * as BthServStartRPC of windows 11 26300: the interface listens by itself, on a dynamic endpoint of ncalrpc the
 * endpoint mapper tells, and the clients authenticate with the security package of the machine. each call checks the
 * level itself.
 */
static RPC_STATUS start_rpc(void)
{
    RPC_STATUS status = RpcServerRegisterIfEx(BthServ_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN, 1234, nullptr);
    if (status)
        return status;
    g_rpc_registered = TRUE;
    /* the gatt interface of Microsoft.Bluetooth.Service.dll, which the service loads, on the same endpoint. */
    status = RpcServerRegisterIfEx(BthGatt_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN, 1234, nullptr);
    if (status)
        return status;
    status = RpcServerUseProtseqW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, nullptr);
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
    RpcServerUnregisterIf(BthGatt_s_ifspec, nullptr, 1);
    RpcServerUnregisterIf(BthServ_s_ifspec, nullptr, 1);
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
    case SERVICE_CONTROL_SESSIONCHANGE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/* as on windows the entry stays until the service is told to stop, so the host keeps the library that long. */
void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_handle = RegisterServiceCtrlHandlerExW(L"bthserv", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    BthServInitClients();
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    RPC_STATUS status = g_stop_event ? start_rpc() : GetLastError();
    if (status) {
        stopped(status);
        return;
    }
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
    update_status();
    WaitForSingleObject(g_stop_event, INFINITE);
    stopped(NO_ERROR);
    CloseHandle(g_stop_event);
    g_stop_event = nullptr;
}
