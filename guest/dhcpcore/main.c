#include "dhcpcore_private.h"

#define RPC_C_AUTHN_WINNT 10
#define RPC_C_AUTHN_DEFAULT_LOCAL 20

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

/*
 * the dhcpv6 half of the service, which dhcpcore6.dll is on windows 11 26300: its interface on the endpoint dhcpcsvc6
 * of ncalrpc. dhcpcsvc6.dll authenticates with the security package of the machine, or with package 20 when a
 * feature of the build says so, and asks for privacy.
 */
static RPC_STATUS start_rpc(void)
{
    RPC_STATUS status = RpcServerRegisterIfEx(Dhcpv6_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN, 1234, nullptr);
    if (status)
        return status;
    g_rpc_registered = TRUE;
    status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)L"dhcpcsvc6", nullptr);
    RPC_WSTR principal = nullptr;
    if (!status)
        status = RpcServerInqDefaultPrincNameW(RPC_C_AUTHN_WINNT, &principal);
    if (!status)
        status = RpcServerRegisterAuthInfoW(principal, RPC_C_AUTHN_WINNT, nullptr, nullptr);
    if (!status)
        RpcServerRegisterAuthInfoW(principal, RPC_C_AUTHN_DEFAULT_LOCAL, nullptr, nullptr);
    if (principal)
        RpcStringFreeW(&principal);
    return status;
}

static void stopped(DWORD error)
{
    if (g_rpc_registered)
        RpcServerUnregisterIf(Dhcpv6_s_ifspec, nullptr, 1);
    g_rpc_registered = FALSE;
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

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_handle = RegisterServiceCtrlHandlerExW(L"Dhcp", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    Dhcpv6Init();
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
