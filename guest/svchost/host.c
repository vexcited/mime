/*
 * hosting a group's service DLLs: loading a ServiceDll when the manager starts its service, the functions the host
 * offers the DLL, and unloading the DLL once the service is done.
 */
#include "svchost.h"

#define STATUS_NOT_IMPLEMENTED_ ((LONG)0xC0000002)
#define RPC_S_DUPLICATE_ENDPOINT_ 1740
#define RPC_S_ALREADY_LISTENING_ 1713
#define UNLOAD_WAIT_MS 5000

/* ------------------------------------------------------------------ service globals */

typedef LONG(WINAPI *RPC_START_FUNCTION)(const WCHAR *endpoint, void *interface_spec);
typedef LONG(WINAPI *RPC_STOP_FUNCTION)(void *interface_spec);
typedef DWORD(WINAPI *REGISTER_STOP_FUNCTION)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback,
                                              void *context, DWORD flags);
typedef DWORD(WINAPI *UNREGISTER_STOP_FUNCTION)(HANDLE wait);
typedef void(WINAPI *PUSH_GLOBALS)(void *globals);

/*
 * passed to the DLL's SvchostPushServiceGlobals or SvchostPushServiceGlobalsEx: the old one gets a pointer to the
 * first sid, the new one the whole block. the DLL calls the function pointers at the end.
 */
typedef struct SVCHOST_GLOBAL_DATA {
    ULONG Version;
    PSID NullSid, WorldSid, LocalSid, NetworkSid, LocalSystemSid, LocalServiceSid, NetworkServiceSid, BuiltinDomainSid,
        AuthenticatedUserSid, AnonymousLogonSid, AliasAdminsSid, AliasUsersSid, AliasGuestsSid, AliasPowerUsersSid,
        AliasAccountOpsSid, AliasSystemOpsSid, AliasPrintOpsSid, AliasBackupOpsSid;
    RPC_START_FUNCTION StartRpcServer;
    RPC_STOP_FUNCTION StopRpcServer;
    RPC_STOP_FUNCTION StopRpcServerEx;
    PVOID Reserved[3];
    REGISTER_STOP_FUNCTION RegisterStopCallbackDeprecated;
    REGISTER_STOP_FUNCTION RegisterStopCallback;
    UNREGISTER_STOP_FUNCTION UnregisterStopCallback;
} SVCHOST_GLOBAL_DATA;

WINBASEAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR text, PSID *sid);

static SRWLOCK g_globals_lock = SRWLOCK_INIT;
static SVCHOST_GLOBAL_DATA *g_globals;

static HOST_SERVICE *find_service(const WCHAR *name)
{
    for (DWORD i = 0; i < g_service_count; i++)
        if (CompareStringOrdinal(name, -1, g_services[i].name, -1, TRUE) == CSTR_EQUAL)
            return &g_services[i];
    return nullptr;
}

/* ------------------------------------------------------------------ DLL use count */

/* the last use frees the DLL when the service asked for that. */
static void drop_use(HOST_SERVICE *svc)
{
    AcquireSRWLockExclusive(&svc->lock);
    svc->users--;
    if (svc->users > 0 || !svc->unload_on_stop || !svc->module) {
        ReleaseSRWLockExclusive(&svc->lock);
        return;
    }
    ULONG_PTR cookie = 0;
    if (svc->context && !ActivateActCtx(svc->context, &cookie)) {
        HostLog("svchost: %ls cannot activate its context to unload, error %lu", svc->name, GetLastError());
        ReleaseSRWLockExclusive(&svc->lock);
        return;
    }
    FreeLibrary(svc->module);
    svc->module = nullptr;
    if (svc->context)
        DeactivateActCtx(0, cookie);
    ReleaseSRWLockExclusive(&svc->lock);
    WakeAllConditionVariable(&svc->unloaded);
}

static void CALLBACK stop_event_fired(PVOID parameter, BOOLEAN timed_out)
{
    HOST_SERVICE *svc = parameter;
    AcquireSRWLockExclusive(&svc->lock);
    HOST_STOP_FN fn = svc->stop_fn;
    void *arg = svc->stop_arg;
    svc->stop_fn = nullptr;
    svc->stop_arg = nullptr;
    ReleaseSRWLockExclusive(&svc->lock);
    if (!fn)
        return;
    fn(arg, timed_out);
    drop_use(svc);
}

void HostWaitForUnloads(void)
{
    for (DWORD i = 0; i < g_service_count; i++) {
        HOST_SERVICE *svc = &g_services[i];
        AcquireSRWLockExclusive(&svc->lock);
        while (svc->module && svc->unload_on_stop) {
            if (!SleepConditionVariableSRW(&svc->unloaded, &svc->lock, UNLOAD_WAIT_MS, 0)) {
                HostLog("svchost: %ls did not unload, error %lu", svc->name, GetLastError());
                break;
            }
        }
        ReleaseSRWLockExclusive(&svc->lock);
    }
}

/* ------------------------------------------------------------------ stop callbacks */

/* long_only: the newer function honors only the long function flag of the wait flags. */
static DWORD register_stop(HANDLE *wait, const WCHAR *name, HANDLE event, void *callback, void *context, DWORD flags,
                           BOOL long_only)
{
    if (!wait || !name || !event || !callback)
        return ERROR_INVALID_PARAMETER;
    HOST_SERVICE *svc = find_service(name);
    if (!svc)
        return ERROR_INVALID_DATA;
    if (long_only)
        flags &= WT_EXECUTELONGFUNCTION;
    flags |= WT_EXECUTEONLYONCE;

    DWORD error = ERROR_SUCCESS;
    AcquireSRWLockExclusive(&svc->lock);
    if (svc->stop_fn) {
        error = ERROR_INVALID_DATA;
    } else if (!RegisterWaitForSingleObject(wait, event, stop_event_fired, svc, INFINITE, flags)) {
        error = GetLastError();
    } else {
        svc->users++;
        svc->stop_fn = (HOST_STOP_FN)callback;
        svc->stop_arg = context;
    }
    ReleaseSRWLockExclusive(&svc->lock);
    return error;
}

static DWORD WINAPI register_stop_old(HANDLE *wait, const WCHAR *name, HANDLE event, void *callback, void *context,
                                      DWORD flags)
{
    return register_stop(wait, name, event, callback, context, flags, FALSE);
}

static DWORD WINAPI register_stop_new(HANDLE *wait, const WCHAR *name, HANDLE event, void *callback, void *context,
                                      DWORD flags)
{
    return register_stop(wait, name, event, callback, context, flags, TRUE);
}

static DWORD WINAPI unregister_stop(HANDLE wait)
{
    if (!wait)
        return ERROR_INVALID_PARAMETER;
    UnregisterWaitEx(wait, nullptr);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ RPC servers */

typedef LONG(WINAPI *USE_PROTSEQ_EP)(const WCHAR *protseq, UINT max_calls, const WCHAR *endpoint, void *sd);
typedef LONG(WINAPI *REGISTER_IF)(void *interface_spec, void *type_uuid, void *manager_epv);
typedef LONG(WINAPI *UNREGISTER_IF)(void *interface_spec, void *type_uuid, UINT wait_for_calls);
typedef LONG(WINAPI *LISTEN)(UINT min_threads, UINT max_calls, UINT dont_wait);
typedef LONG(WINAPI *STOP_LISTENING)(void *binding);
typedef LONG(WINAPI *WAIT_LISTEN)(void);

static LONG g_rpc_interfaces;

static HMODULE rpc_runtime(void)
{
    HMODULE module = GetModuleHandleW(L"rpcrt4.dll");
    return module ? module : LoadLibraryW(L"rpcrt4.dll");
}

static LONG WINAPI start_rpc_server(const WCHAR *endpoint, void *interface_spec)
{
    HMODULE rpc = rpc_runtime();
    USE_PROTSEQ_EP use_protseq = rpc ? (USE_PROTSEQ_EP)GetProcAddress(rpc, "RpcServerUseProtseqEpW") : nullptr;
    REGISTER_IF register_if = rpc ? (REGISTER_IF)GetProcAddress(rpc, "RpcServerRegisterIf") : nullptr;
    LISTEN listen = rpc ? (LISTEN)GetProcAddress(rpc, "RpcServerListen") : nullptr;
    if (!use_protseq || !register_if || !listen)
        return STATUS_NOT_IMPLEMENTED_;

    WCHAR pipe[MAX_PATH];
    if (!endpoint || wcslen(endpoint) + 7 > RTL_NUMBER_OF(pipe))
        return ERROR_INVALID_PARAMETER;
    wcscpy(pipe, L"\\PIPE\\");
    wcscat(pipe, endpoint);

    LONG status = use_protseq(L"ncacn_np", 10, pipe, nullptr);
    if (status && status != RPC_S_DUPLICATE_ENDPOINT_)
        return status;
    status = register_if(interface_spec, nullptr, nullptr);
    if (status)
        return status;
    if (InterlockedIncrement(&g_rpc_interfaces) == 1) {
        status = listen(1, 12345, 1);
        if (status == RPC_S_ALREADY_LISTENING_)
            status = 0;
    }
    return status;
}

static LONG WINAPI stop_rpc_server(void *interface_spec)
{
    HMODULE rpc = rpc_runtime();
    UNREGISTER_IF unregister = rpc ? (UNREGISTER_IF)GetProcAddress(rpc, "RpcServerUnregisterIf") : nullptr;
    STOP_LISTENING stop = rpc ? (STOP_LISTENING)GetProcAddress(rpc, "RpcMgmtStopServerListening") : nullptr;
    WAIT_LISTEN wait = rpc ? (WAIT_LISTEN)GetProcAddress(rpc, "RpcMgmtWaitServerListen") : nullptr;
    if (!unregister || !stop || !wait)
        return STATUS_NOT_IMPLEMENTED_;

    LONG status = unregister(interface_spec, nullptr, 1);
    if (status)
        return status;
    if (g_rpc_interfaces > 0 && InterlockedDecrement(&g_rpc_interfaces) == 0) {
        status = stop(nullptr);
        if (!status)
            status = wait();
    }
    return status;
}

static BOOL build_globals(void)
{
    static const WCHAR *const sids[] = {L"S-1-0-0", L"S-1-1-0", L"S-1-2-0", L"S-1-5-2", L"S-1-5-18", L"S-1-5-19",
                                        L"S-1-5-20", L"S-1-5-32", L"S-1-5-11", L"S-1-5-7", L"S-1-5-32-544",
                                        L"S-1-5-32-545", L"S-1-5-32-546", L"S-1-5-32-547", L"S-1-5-32-548",
                                        L"S-1-5-32-549", L"S-1-5-32-550", L"S-1-5-32-551"};
    SVCHOST_GLOBAL_DATA *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*data));
    if (!data)
        return FALSE;
    data->Version = 1;
    PSID *slot = &data->NullSid;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(sids); i++)
        if (!ConvertStringSidToSidW(sids[i], &slot[i])) {
            HeapFree(GetProcessHeap(), 0, data);
            return FALSE;
        }
    data->StartRpcServer = start_rpc_server;
    data->StopRpcServer = stop_rpc_server;
    data->StopRpcServerEx = stop_rpc_server;
    data->RegisterStopCallbackDeprecated = register_stop_old;
    data->RegisterStopCallback = register_stop_new;
    data->UnregisterStopCallback = unregister_stop;
    g_globals = data;
    return TRUE;
}

/* the first service that needs the block builds it. */
static BOOL globals_ready(void)
{
    AcquireSRWLockExclusive(&g_globals_lock);
    BOOL ok = g_globals != nullptr || build_globals();
    ReleaseSRWLockExclusive(&g_globals_lock);
    return ok;
}

/* ------------------------------------------------------------------ finding a service's DLL */

#define ROOT_OF_SERVICES L"System\\CurrentControlSet\\Services\\"
#define DLL_PATH_CHARS 260

/* values live in the service's Parameters key, or in the service key itself when there is none. */
static DWORD open_values(const WCHAR *name, HKEY *key)
{
    WCHAR path[MAX_PATH];
    if (wcslen(name) + RTL_NUMBER_OF(ROOT_OF_SERVICES) + RTL_NUMBER_OF(L"\\Parameters") > RTL_NUMBER_OF(path))
        return ERROR_FILENAME_EXCED_RANGE;
    wcscpy(path, ROOT_OF_SERVICES);
    wcscat(path, name);
    SIZE_T base = wcslen(path);
    wcscat(path, L"\\Parameters");
    DWORD error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, key);
    if (error == ERROR_FILE_NOT_FOUND) {
        path[base] = 0;
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, key);
    }
    return error;
}

static BOOL read_nonzero_dword(HKEY key, const WCHAR *value)
{
    DWORD type = 0, data = 0, size = sizeof(data);
    if (RegQueryValueExW(key, value, nullptr, &type, (BYTE *)&data, &size) != ERROR_SUCCESS)
        return FALSE;
    return type == REG_DWORD && data != 0;
}

/*
 * the value must be a non empty REG_EXPAND_SZ. it comes back expanded with the process environment and lowercased, on
 * the heap. `optional` lets a missing value give no path and no error.
 */
static DWORD read_path_value(HKEY key, const WCHAR *value, BOOL optional, WCHAR **path)
{
    WCHAR raw[DLL_PATH_CHARS + 2];
    DWORD type = 0, size = DLL_PATH_CHARS * sizeof(WCHAR) + sizeof(WCHAR);
    *path = nullptr;
    DWORD error = RegQueryValueExW(key, value, nullptr, &type, (BYTE *)raw, &size);
    if (optional && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
        return ERROR_SUCCESS;
    if (error)
        return error;
    DWORD chars = size / sizeof(WCHAR);
    raw[chars < RTL_NUMBER_OF(raw) ? chars : RTL_NUMBER_OF(raw) - 1] = 0;
    if (type != REG_EXPAND_SZ || !raw[0])
        return ERROR_FILE_NOT_FOUND;

    DWORD need = ExpandEnvironmentStringsW(raw, nullptr, 0);
    if (!need)
        return GetLastError();
    WCHAR *expanded = HeapAlloc(GetProcessHeap(), 0, need * sizeof(WCHAR));
    if (!expanded)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!ExpandEnvironmentStringsW(raw, expanded, need)) {
        error = GetLastError();
        HeapFree(GetProcessHeap(), 0, expanded);
        return error;
    }
    LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, expanded, -1, expanded, (int)need);
    *path = expanded;
    return ERROR_SUCCESS;
}

/* the ServiceMain value can name a different entry point. */
static CHAR *read_main_export(HKEY key)
{
    WCHAR text[128];
    DWORD type = 0, size = sizeof(text) - sizeof(WCHAR);
    if (RegQueryValueExW(key, L"ServiceMain", nullptr, &type, (BYTE *)text, &size) != ERROR_SUCCESS || type != REG_SZ)
        return nullptr;
    text[size / sizeof(WCHAR)] = 0;
    if (!text[0])
        return nullptr;
    int bytes = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    CHAR *name = bytes > 0 ? HeapAlloc(GetProcessHeap(), 0, bytes) : nullptr;
    if (name && !WideCharToMultiByte(CP_ACP, 0, text, -1, name, bytes, nullptr, nullptr)) {
        HeapFree(GetProcessHeap(), 0, name);
        name = nullptr;
    }
    return name;
}

static DWORD make_context(HOST_SERVICE *svc)
{
    ACTCTXW request = {sizeof(request)};
    request.lpSource = svc->manifest_path;
    HANDLE context = CreateActCtxW(&request);
    if (context == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (svc->context)
        ReleaseActCtx(svc->context);
    svc->context = context;
    return ERROR_SUCCESS;
}

/* the first start finds the DLL. later starts only renew the activation context of an unloaded DLL. */
static DWORD locate_dll(HOST_SERVICE *svc, HKEY values)
{
    if (!svc->dll_path) {
        WCHAR *dll = nullptr, *manifest = nullptr;
        DWORD error = read_path_value(values, L"ServiceDll", FALSE, &dll);
        if (!error)
            error = read_path_value(values, L"ServiceManifest", TRUE, &manifest);
        if (error) {
            HeapFree(GetProcessHeap(), 0, dll);
            return error;
        }
        svc->dll_path = dll;
        svc->manifest_path = manifest;
        svc->main_export = read_main_export(values);
        return manifest ? make_context(svc) : ERROR_SUCCESS;
    }
    if (!svc->module && svc->manifest_path)
        return make_context(svc);
    return ERROR_SUCCESS;
}

typedef struct ENTRY_POINTS {
    LPSERVICE_MAIN_FUNCTIONW main;
    PUSH_GLOBALS push;
    PUSH_GLOBALS push_ex;
} ENTRY_POINTS;

/* loading and lookups run inside the manifest's activation context. */
static DWORD find_entry_points(HOST_SERVICE *svc, ENTRY_POINTS *found)
{
    ULONG_PTR cookie = 0;
    if (svc->context && !ActivateActCtx(svc->context, &cookie))
        return GetLastError();

    DWORD error = ERROR_SUCCESS;
    if (!svc->module)
        svc->module = LoadLibraryExW(svc->dll_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!svc->module) {
        error = GetLastError();
    } else {
        found->main = (LPSERVICE_MAIN_FUNCTIONW)GetProcAddress(svc->module,
                                                               svc->main_export ? svc->main_export : "ServiceMain");
        if (!found->main) {
            error = GetLastError();
        } else {
            found->push_ex = (PUSH_GLOBALS)GetProcAddress(svc->module, "SvchostPushServiceGlobalsEx");
            if (!found->push_ex)
                found->push = (PUSH_GLOBALS)GetProcAddress(svc->module, "SvchostPushServiceGlobals");
        }
    }
    if (svc->context)
        DeactivateActCtx(0, cookie);
    return error;
}

/* ------------------------------------------------------------------ starting a service */

static void WINAPI ignore_control(DWORD control)
{
    (void)control;
}

/* a service that cannot start is reported as stopped, with the reason as its exit code. */
static void report_stopped(const WCHAR *name, DWORD error)
{
    SERVICE_STATUS_HANDLE handle = RegisterServiceCtrlHandlerW(name, ignore_control);
    if (!handle)
        return;
    SERVICE_STATUS status = {};
    status.dwServiceType = SERVICE_WIN32;
    status.dwCurrentState = SERVICE_STOPPED;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    status.dwWin32ExitCode = error;
    SetServiceStatus(handle, &status);
}

/* once set, these two switches stay set. */
static void read_switches(HOST_SERVICE *svc, HKEY values)
{
    if (read_nonzero_dword(values, L"ServiceDllUnloadOnStop"))
        svc->unload_on_stop = TRUE;
    if (read_nonzero_dword(values, L"LegacyCOMBehavior"))
        g_wait_for_unloads = FALSE;
}

VOID WINAPI HostService(DWORD argc, LPWSTR *argv)
{
    if (!argc)
        return;
    HOST_SERVICE *svc = find_service(argv[0]);
    if (!svc)
        return;

    ENTRY_POINTS found = {};
    HKEY values = nullptr;
    AcquireSRWLockExclusive(&svc->lock);
    DWORD error = open_values(svc->name, &values);
    if (!error)
        error = locate_dll(svc, values);
    if (!error)
        error = find_entry_points(svc, &found);
    if (!error && !found.main)
        error = ERROR_PROC_NOT_FOUND;
    if (!error && (found.push || found.push_ex) && !globals_ready())
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (error) {
        if (values)
            RegCloseKey(values);
        ReleaseSRWLockExclusive(&svc->lock);
        HostLog("svchost: %ls cannot start, error %lu", svc->name, error);
        report_stopped(svc->name, error);
        return;
    }
    svc->users++;
    read_switches(svc, values);
    RegCloseKey(values);
    ReleaseSRWLockExclusive(&svc->lock);

    if (found.push_ex)
        found.push_ex(g_globals);
    else if (found.push)
        found.push(&g_globals->NullSid);
    found.main(argc, argv);
    drop_use(svc);
}
