/*
 * wkssvc.dll: the workstation service, LanmanWorkstation. It answers the calls of wkscli.dll over the named pipe
 * \PIPE\wkssvc, the interface of [MS-WKST], for the name and workgroup of the machine, its users, transports and
 * connections. Windows' service also drives the redirector, which Mime has no use for; the states, the controls and
 * the order of the start and the stop are those of wkssvc.dll of build 26300.
 */
#include "wkssvc_private.h"

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

#define RPC_PIPE L"\\PIPE\\wkssvc"
#define ACCEPTED_CONTROLS 0x43 /* stop, pause and continue, and power events. */

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

static void pause_or_continue(BOOL resume)
{
    if (resume ? g_status.dwCurrentState == SERVICE_PAUSED : g_status.dwCurrentState == SERVICE_RUNNING)
        g_status.dwCurrentState = resume ? SERVICE_RUNNING : SERVICE_PAUSED;
}

static DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_status.dwCurrentState != SERVICE_STOP_PENDING) {
            g_status.dwCurrentState = SERVICE_STOP_PENDING;
            g_status.dwCheckPoint = 1;
            g_status.dwWaitHint = 90000;
            update_status();
            SetEvent(g_stop_event);
            return NO_ERROR;
        }
        break;
    case SERVICE_CONTROL_PAUSE:
        pause_or_continue(FALSE);
        break;
    case SERVICE_CONTROL_CONTINUE:
        pause_or_continue(TRUE);
        break;
    case SERVICE_CONTROL_INTERROGATE:
        break;
    case 12: /* hardware profile change. */
    case 14: /* session change. */
        return NO_ERROR;
    case 13: /* power event. */
        break;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
    update_status();
    return NO_ERROR;
}

/*
 * WsRpcSecurityCallback of windows' wkssvc.dll: a remote caller may not use the procedures 8 to 11 and 39 to 44, and
 * every call has to come over the named pipe.
 */
static RPC_STATUS RPC_ENTRY security_callback(RPC_IF_HANDLE interface, void *context)
{
    RPC_CALL_ATTRIBUTES_V2_W attributes = {2, RPC_QUERY_IS_CLIENT_LOCAL | RPC_QUERY_NO_AUTH_REQUIRED};
    RPC_BINDING_HANDLE server = nullptr;
    if (RpcServerInqCallAttributesW(context, &attributes) != RPC_S_OK ||
        (attributes.IsClientLocal != 1 && ((USHORT)(attributes.OpNum - 8) <= 3 || (USHORT)(attributes.OpNum - 39) <= 5)) ||
        RpcBindingServerFromClient(context, &server) != RPC_S_OK)
        return ERROR_ACCESS_DENIED;
    RPC_WSTR binding = nullptr, protseq = nullptr;
    RPC_STATUS status = ERROR_ACCESS_DENIED;
    if (RpcBindingToStringBindingW(server, &binding) == RPC_S_OK &&
        RpcStringBindingParseW(binding, nullptr, &protseq, nullptr, nullptr, nullptr) == RPC_S_OK)
        status = lstrcmpW((const WCHAR *)protseq, L"ncacn_np") ? ERROR_ACCESS_DENIED : RPC_S_OK;
    RpcBindingFree(&server);
    if (binding)
        RpcStringFreeW(&binding);
    if (protseq)
        RpcStringFreeW(&protseq);
    return status;
}

/*
 * the pipe's security is the service's, everyone reads the workstation, except that the two rights of everyone and
 * of the anonymous logon include the one to append: windows' clients reach the pipe through the redirector, which opens
 * it for them, and Mime's open it themselves with the rights of a write.
 */
static DWORD start_rpc(void)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:NSG:NSD:(A;;0x12019F;;;AN)(A;;0x12019F;;;WD)(A;;0x1F01FF;;;SY)(A;;0x1F01FF;;;NS)S:(ML;;NW;;;LW)", 1, &sd,
        nullptr);
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncacn_np", 10, (RPC_WSTR)RPC_PIPE, sd);
    if (sd)
        LocalFree(sd);
    if (status == RPC_S_DUPLICATE_ENDPOINT)
        status = RPC_S_OK;
    if (status != RPC_S_OK)
        return status;
    /* a cache of the security callback's answers for each process needs the callback. */
    status = RpcServerRegisterIfEx(wkssvc_s_ifspec, nullptr, nullptr, 0x91, 1234, security_callback);
    if (status == RPC_S_OK)
        g_rpc_registered = TRUE;
    return status;
}

/* the interface goes, then the service is down with the error that stopped it. */
static void stop(DWORD error)
{
    if (g_rpc_registered) {
        RpcServerUnregisterIf(wkssvc_s_ifspec, nullptr, 1);
        g_rpc_registered = FALSE;
    }
    g_status.dwCheckPoint++;
    update_status();
    g_status.dwControlsAccepted = 0;
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwWin32ExitCode = error;
    g_status.dwServiceSpecificExitCode = error;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    update_status();
}

/* the stop event fired. */
static void WINAPI terminated(void *context, BOOLEAN timed_out)
{
    stop(0);
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwCheckPoint = 1;
    g_status.dwWaitHint = 90000;
    g_handle = RegisterServiceCtrlHandlerExW(L"LanmanWorkstation", control_handler, nullptr);
    if (!g_handle)
        return;
    update_status();
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop_event) {
        stop(GetLastError());
        return;
    }
    DWORD error = WsLoadConfiguration();
    if (!error)
        error = start_rpc();
    if (error) {
        stop(error);
        return;
    }
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = ACCEPTED_CONTROLS;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    error = update_status();
    if (!error && g_globals)
        error = ((REGISTER_STOP)g_globals[GLOBALS_REGISTER_STOP])(&g_stop_wait, L"LanmanWorkstation", g_stop_event,
                                                                  (void *)terminated, nullptr,
                                                                  WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION);
    else if (!error)
        error = ERROR_INVALID_DATA;
    if (error)
        stop(error);
}
