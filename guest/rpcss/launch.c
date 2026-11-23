#include "rpcss.h"

#include "rpcss/launch.h"

#define CO_E_SERVER_EXEC_FAILURE ((HRESULT)0x80080005L)
#define TOKEN_ALL_ACCESS_ 0xF01FF
#define SECURITY_IMPERSONATION_ 2
#define TOKEN_PRIMARY_ 1
#define CREATE_DEFAULT_ERROR_MODE_ 0x04000000
#define RPC_C_QOS_IDENTITY_DYNAMIC_ 1
#define LAUNCH_ENDPOINT L"DcomLaunch"
WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINBASEAPI BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD level, DWORD type,
                                        PHANDLE out);
NTSYSAPI NTSTATUS NTAPI RtlAdjustPrivilege(ULONG privilege, BOOLEAN enable, BOOLEAN thread, PBOOLEAN was);
#define SE_ASSIGNPRIMARYTOKEN_PRIVILEGE_ 3
#define SE_INCREASE_QUOTA_PRIVILEGE_ 5

/* ------------------------------------------------------------------ DcomLaunch */

/* the process RpcSs runs in, the one caller DcomLaunch serves. */
static DWORD rpcss_pid(void)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    SC_HANDLE service = manager ? OpenServiceW(manager, L"RpcSs", SERVICE_QUERY_STATUS) : nullptr;
    SERVICE_STATUS_PROCESS status = {};
    DWORD bytes = 0;
    if (service)
        QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&status, sizeof(status), &bytes);
    if (service)
        CloseServiceHandle(service);
    if (manager)
        CloseServiceHandle(manager);
    return status.dwProcessId;
}

static HRESULT start_service(const WCHAR *name, const WCHAR *parameters, DWORD *pid)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    SC_HANDLE service = manager ? OpenServiceW(manager, name, SERVICE_START | SERVICE_QUERY_STATUS) : nullptr;
    const WCHAR *args[1] = {parameters};
    BOOL given = parameters && parameters[0];
    DWORD error = service && StartServiceW(service, given ? 1 : 0, given ? args : nullptr) ? 0 : GetLastError();
    SERVICE_STATUS_PROCESS status = {};
    DWORD bytes = 0;
    if (service && (!error || error == ERROR_SERVICE_ALREADY_RUNNING))
        QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&status, sizeof(status), &bytes);
    if (service)
        CloseServiceHandle(service);
    if (manager)
        CloseServiceHandle(manager);
    *pid = status.dwProcessId;
    if (error && error != ERROR_SERVICE_ALREADY_RUNNING) {
        RsLog("launch: service %ls did not start: %lu", name, error);
        return CO_E_SERVER_EXEC_FAILURE;
    }
    return S_OK;
}

/* a program serving a class runs as the process that activates it, in its session, told by -Embedding why it runs. */
static HRESULT start_program(DWORD client, const WCHAR *command, DWORD *pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, client);
    HANDLE token = nullptr, primary = nullptr;
    BOOL opened = process && OpenProcessToken(process, TOKEN_ALL_ACCESS_, &token);
    if (process)
        CloseHandle(process);
    if (!opened || !DuplicateTokenEx(token, TOKEN_ALL_ACCESS_, nullptr, SECURITY_IMPERSONATION_, TOKEN_PRIMARY_,
                                     &primary)) {
        if (token)
            CloseHandle(token);
        return E_ACCESSDENIED;
    }
    CloseHandle(token);
    WCHAR line[1100];
    _snwprintf(line, RTL_NUMBER_OF(line), L"%ls -Embedding", command);
    line[RTL_NUMBER_OF(line) - 1] = 0;
    HMODULE userenv = LoadLibraryW(L"userenv.dll");
    BOOL(WINAPI * create_environment)(LPVOID *, HANDLE, BOOL) =
        userenv ? (void *)GetProcAddress(userenv, "CreateEnvironmentBlock") : nullptr;
    BOOL(WINAPI * destroy_environment)(LPVOID) = userenv ? (void *)GetProcAddress(userenv, "DestroyEnvironmentBlock") : nullptr;
    LPVOID environment = nullptr;
    BOOL made = create_environment && destroy_environment && create_environment(&environment, primary, FALSE);
    STARTUPINFOW si = {sizeof(si)};
    si.lpDesktop = (LPWSTR)L"winsta0\\default";
    PROCESS_INFORMATION pi = {};
    /* the service token holds both disabled, as on windows. */
    BOOLEAN was;
    RtlAdjustPrivilege(SE_ASSIGNPRIMARYTOKEN_PRIVILEGE_, TRUE, FALSE, &was);
    RtlAdjustPrivilege(SE_INCREASE_QUOTA_PRIVILEGE_, TRUE, FALSE, &was);
    BOOL ok = CreateProcessAsUserW(primary, nullptr, line, nullptr, nullptr, FALSE,
                                   CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE_, made ? environment : nullptr,
                                   nullptr, &si, &pi);
    DWORD error = ok ? 0 : GetLastError();
    if (made)
        destroy_environment(environment);
    CloseHandle(primary);
    if (!ok) {
        RsLog("launch: %ls did not start: %lu", line, error);
        return CO_E_SERVER_EXEC_FAILURE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    *pid = pi.dwProcessId;
    return S_OK;
}

long WINAPI IDcomLaunch_LaunchServer_s(handle_t hRpc, unsigned short *service, unsigned short *parameters,
                                       unsigned short *command, unsigned long client, unsigned long *pid)
{
    *pid = 0;
    ULONG caller = 0;
    if (I_RpcBindingInqLocalClientPID(hRpc, &caller) != RPC_S_OK || !caller || caller != rpcss_pid())
        return E_ACCESSDENIED;
    DWORD served = 0;
    HRESULT hr = service && service[0] ? start_service((const WCHAR *)service, (const WCHAR *)parameters, &served)
                 : command && command[0] ? start_program(client, (const WCHAR *)command, &served)
                                         : E_INVALIDARG;
    *pid = served;
    return hr;
}

/* the system and the network service RpcSs runs as reach the endpoint, nobody else. */
DWORD RsStartLaunch(void)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;NS)", 1, &sd, nullptr))
        return GetLastError();
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)LAUNCH_ENDPOINT, sd);
    if (status == RPC_S_DUPLICATE_ENDPOINT)
        status = RPC_S_OK;
    if (!status)
        status = RpcServerRegisterIf3(IDcomLaunch_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, nullptr, sd);
    return status;
}

/* ------------------------------------------------------------------ RpcSs */

/*
 * RpcSs runs as the network service and starts nothing itself: DcomLaunch starts the server, a program as the process
 * that activates. RpcSs calls as itself, the token of an activation is only good to identify the client. the process of
 * the server comes back, to wait for it.
 */
HRESULT RsLaunch(DWORD client, const WCHAR *service, const WCHAR *parameters, const WCHAR *command, HANDLE *process)
{
    *process = nullptr;
    RPC_BINDING_HANDLE b = nullptr;
    RPC_STATUS status = RpcBindingFromStringBindingW((RPC_WSTR)L"ncalrpc:[" LAUNCH_ENDPOINT L"]", &b);
    RPC_SECURITY_QOS qos = {RPC_C_SECURITY_QOS_VERSION, 0, RPC_C_QOS_IDENTITY_DYNAMIC_, RPC_C_IMP_LEVEL_IMPERSONATE};
    if (!status)
        status = RpcBindingSetAuthInfoExW(b, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_AUTHN_WINNT, nullptr,
                                          RPC_C_AUTHZ_NONE, &qos);
    HRESULT hr = status ? HRESULT_FROM_WIN32(status) : S_OK;
    unsigned long pid = 0;
    if (!status) {
        __try {
            hr = IDcomLaunch_LaunchServer(b, (unsigned short *)service, (unsigned short *)parameters,
                                          (unsigned short *)command, client, &pid);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            hr = HRESULT_FROM_WIN32(__exception_code());
        }
    }
    if (b)
        RpcBindingFree(&b);
    if (SUCCEEDED(hr) && pid)
        *process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (FAILED(hr))
        RsLog("launch: DcomLaunch answers %08lx", hr);
    return hr;
}
