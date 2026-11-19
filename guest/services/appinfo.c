/*
 * the appinfo interface, 201ef99a-7fa0-444c-9399-19ba84f12a1a, which genuine windows.storage calls for the runas verb
 * of ShellExecuteEx: RAiLaunchAdminProcess. windows serves it from the Application Information service, mime from
 * the manager, with the broker of elevate.c. the endpoint is a dynamic one of ncalrpc that the mapper names.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/appinfo.h"

/* everyone and all application packages connect and call, as for the lookup interface. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

long WINAPI appinfo_RAiLaunchAdminProcess_s(handle_t h, unsigned short *ExecutablePath, unsigned short *CommandLine,
                                            long StartFlags, long CreateFlags, unsigned short *CurrentDirectory,
                                            unsigned short *WindowStation, APP_STARTUP_INFO *StartupInfo,
                                            ULONG_PTR hWnd, long Timeout, APP_PROCESS_INFORMATION *ProcessInformation,
                                            long *ElevationType)
{
    (void)hWnd;
    (void)Timeout;
    memset(ProcessInformation, 0, sizeof(*ProcessInformation));
    *ElevationType = 0;
    ULONG pid = 0;
    RPC_STATUS status = I_RpcBindingInqLocalClientPID(h, &pid);
    if (status)
        return (long)status;
    status = RpcImpersonateClient(h);
    if (status)
        return (long)status;
    HANDLE caller = nullptr;
    BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &caller);
    DWORD error = opened ? ERROR_SUCCESS : GetLastError();
    RpcRevertToSelf();
    if (!opened)
        return (long)error;
    /* bit 0 of the start flags asks for the elevated token, as runas does. without it the caller's own would do. */
    ELEVATE_REQUEST q = {
        .Application = (WCHAR *)ExecutablePath,
        .CommandLine = (WCHAR *)(CommandLine ? CommandLine : ExecutablePath),
        .Directory = CurrentDirectory && CurrentDirectory[0] ? (WCHAR *)CurrentDirectory : nullptr,
        .Desktop = WindowStation && WindowStation[0] ? (WCHAR *)WindowStation : nullptr,
        .Title = StartupInfo ? (WCHAR *)StartupInfo->lpszTitle : nullptr,
        .CreationFlags = (DWORD)CreateFlags,
        .StartupFlags = StartupInfo ? (DWORD)StartupInfo->dwFlags : 0,
        .Show = StartupInfo ? (DWORD)(USHORT)StartupInfo->wShowWindow : 1,
        .Unelevated = !(StartFlags & 1),
    };
    if (!q.CommandLine) {
        CloseHandle(caller);
        return ERROR_INVALID_PARAMETER;
    }
    ELEVATE_RESULT result = {};
    error = ElevateLaunchAs(caller, pid, &q, &result);
    if (error)
        return (long)error;
    ProcessInformation->ProcessHandle = (ULONG_PTR)result.Process;
    ProcessInformation->ThreadHandle = (ULONG_PTR)result.Thread;
    ProcessInformation->ProcessId = (long)result.ProcessId;
    ProcessInformation->ThreadId = (long)result.ThreadId;
    *ElevationType = (long)result.Elevation;
    return ERROR_SUCCESS;
}

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    (void)interface;
    (void)context;
    return RPC_S_OK;
}

/* the mapper is a service the manager starts after itself, so the endpoint is told to it once it answers. */
static DWORD WINAPI register_endpoint(void *context)
{
    RPC_BINDING_VECTOR *bindings = context;
    RPC_STATUS status = RPC_S_OK;
    for (int attempt = 0; attempt < 240; attempt++) {
        status = RpcEpRegisterW(appinfo_s_ifspec, bindings, nullptr, (RPC_WSTR)L"AppInfo");
        if (!status)
            break;
        Sleep(250);
    }
    RpcBindingVectorFree(&bindings);
    if (status)
        SvcLog("appinfo: the endpoint mapper did not take the endpoint: %u", status);
    return 0;
}

void AppInfoStart(void)
{
    RPC_BINDING_VECTOR *bindings = nullptr;
    RPC_STATUS status = RpcServerUseProtseqW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(appinfo_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerInqBindings(&bindings);
    if (!status) {
        HANDLE thread = CreateThread(nullptr, 0, register_endpoint, bindings, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        else
            RpcBindingVectorFree(&bindings);
    }
    if (status)
        SvcLog("appinfo: the rpc server did not start: %u", status);
}
