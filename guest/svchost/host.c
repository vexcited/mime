/*
 * the services of a group: loading the ServiceDll of one when the manager starts it, the functions a dll can take
 * from the host (SvchostPushServiceGlobals) and the unloading of the dll once the service is done.
 */
#include "svchost.h"

#define CSTR_EQUAL_ 2
#define ERROR_INVALID_DATA_ 13
#define SERVICE_WIN32_ 0x30

/* ------------------------------------------------------------------ the globals a dll may ask for */

typedef LONG(WINAPI *RPC_START_FUNCTION)(const WCHAR *endpoint, void *interface_spec);
typedef LONG(WINAPI *RPC_STOP_FUNCTION)(void *interface_spec);
typedef DWORD(WINAPI *REGISTER_STOP_FUNCTION)(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback,
                                              void *context, DWORD flags);
typedef DWORD(WINAPI *UNREGISTER_STOP_FUNCTION)(HANDLE wait);
typedef void(WINAPI *PUSH_GLOBALS)(void *globals);

/*
 * what SvchostPushServiceGlobals and SvchostPushServiceGlobalsEx of a service dll are given (read: SvchostBuildSharedGlobals):
 * the old function gets the pointer to the first sid, the new one the whole block. the three rpc functions are
 * the host's, which is a pointer to rpcrt4's server functions in a process that has them.
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

/* ------------------------------------------------------------------ the hosted services */

static HOSTED *find_hosted(const WCHAR *name)
{
    for (DWORD i = 0; i < g_hosted_count; i++)
        if (CompareStringOrdinal(name, -1, g_hosted[i].Name, -1, TRUE) == CSTR_EQUAL_)
            return &g_hosted[i];
    return nullptr;
}

/* the stop callback of a service: the event it waits for, the function to run once it is set and its argument. */
typedef struct STOP_CONTEXT {
    PVOID Context;
    HOSTED *Service;
} STOP_CONTEXT;

/* the dll leaves when the last of its users has: the service's main, and the stop callback it registered. */
static void release_dll(HOSTED *s)
{
    AcquireSRWLockExclusive(&s->Lock);
    LONG refs = --s->Refs;
    if (!s->UnloadOnStop || refs != 0 || !s->Dll || !s->Dll->Module) {
        ReleaseSRWLockExclusive(&s->Lock);
        return;
    }
    ULONG_PTR cookie = 0;
    if (s->Dll->ActCtx && !ActivateActCtx(s->Dll->ActCtx, &cookie)) {
        HostLog("svchost: %ls stays loaded, its context cannot be activated: %lu", s->Name, GetLastError());
        ReleaseSRWLockExclusive(&s->Lock);
        return;
    }
    FreeLibrary(s->Dll->Module);
    s->Dll->Module = nullptr;
    if (s->Dll->ActCtx)
        DeactivateActCtx(0, cookie);
    ReleaseSRWLockExclusive(&s->Lock);
    WakeAllConditionVariable(&s->Unloaded);
}

static void CALLBACK stop_fired(PVOID parameter, BOOLEAN timed_out)
{
    STOP_CONTEXT *c = parameter;
    HOSTED *s = c->Service;
    AcquireSRWLockExclusive(&s->Lock);
    SVCHOST_STOP_CALLBACK callback = s->StopCallback;
    s->StopCallback = nullptr;
    ReleaseSRWLockExclusive(&s->Lock);
    if (callback)
        callback(c->Context, timed_out);
    release_dll(s);
    HeapFree(GetProcessHeap(), 0, c);
}

/*
 * a service asks to be told when an event is set, which is how it learns that it must stop: only one wait for
 * each, and the dll is held until it has run. the old function was the same but for the flags of the wait.
 */
static DWORD register_stop(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback, void *context, DWORD flags)
{
    if (!wait || !service || !event || !callback)
        return ERROR_INVALID_PARAMETER;
    HOSTED *s = find_hosted(service);
    if (!s)
        return ERROR_INVALID_DATA_;
    AcquireSRWLockExclusive(&s->Lock);
    if (s->StopCallback) {
        ReleaseSRWLockExclusive(&s->Lock);
        return ERROR_INVALID_DATA_;
    }
    STOP_CONTEXT *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) {
        ReleaseSRWLockExclusive(&s->Lock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    c->Context = context;
    c->Service = s;
    HANDLE registered = nullptr;
    if (!RegisterWaitForSingleObject(&registered, event, stop_fired, c, INFINITE, WT_EXECUTEONLYONCE | flags)) {
        DWORD error = GetLastError();
        ReleaseSRWLockExclusive(&s->Lock);
        HeapFree(GetProcessHeap(), 0, c);
        return error ? error : ERROR_INVALID_DATA_;
    }
    *wait = registered;
    s->Refs++;
    s->StopCallback = (SVCHOST_STOP_CALLBACK)callback;
    ReleaseSRWLockExclusive(&s->Lock);
    return ERROR_SUCCESS;
}

static DWORD WINAPI SvcRegisterStopCallback(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback,
                                            void *context, DWORD flags)
{
    return register_stop(wait, service, event, callback, context, flags & WT_EXECUTELONGFUNCTION);
}

static DWORD WINAPI SvcDeprecatedRegisterStopCallback(HANDLE *wait, const WCHAR *service, HANDLE event, void *callback,
                                                      void *context, DWORD flags)
{
    return register_stop(wait, service, event, callback, context, flags);
}

static DWORD WINAPI SvcUnregisterStopCallback(HANDLE wait)
{
    if (!wait)
        return ERROR_INVALID_PARAMETER;
    UnregisterWaitEx(wait, nullptr);
    return ERROR_SUCCESS;
}

/* the rpc functions need rpcrt4's servers, which a process takes from the library when it has them. */
static HMODULE rpc_module(void)
{
    static HMODULE module;
    static BOOL tried;
    if (!tried) {
        module = LoadLibraryW(L"rpcrt4.dll");
        tried = TRUE;
    }
    return module;
}

static LONG WINAPI start_rpc_server(const WCHAR *endpoint, void *interface_spec)
{
    typedef LONG(WINAPI * USE_PROTSEQ)(WCHAR *protocol, UINT max_calls, WCHAR *endpoint, void *sd);
    typedef LONG(WINAPI * REGISTER_IF)(void *spec, void *uuid, void *manager);
    typedef LONG(WINAPI * LISTEN)(UINT minimum, UINT maximum, UINT dont_wait);
    HMODULE rpc = rpc_module();
    USE_PROTSEQ use = rpc ? (USE_PROTSEQ)GetProcAddress(rpc, "RpcServerUseProtseqEpW") : nullptr;
    REGISTER_IF registerif = rpc ? (REGISTER_IF)GetProcAddress(rpc, "RpcServerRegisterIf") : nullptr;
    LISTEN listen = rpc ? (LISTEN)GetProcAddress(rpc, "RpcServerListen") : nullptr;
    if (!use || !registerif || !listen)
        return (LONG)0xC0000002; /* STATUS_NOT_IMPLEMENTED */
    WCHAR name[260];
    _snwprintf(name, RTL_NUMBER_OF(name) - 1, L"\\PIPE\\%ls", endpoint);
    name[RTL_NUMBER_OF(name) - 1] = 0;
    WCHAR protocol[] = L"ncacn_np";
    LONG status = use(protocol, 10, name, nullptr);
    if (status == 0 || status == 1740 /* RPC_S_DUPLICATE_ENDPOINT */)
        status = registerif(interface_spec, nullptr, nullptr);
    if (status == 0) {
        status = listen(1, 12345, 1);
        if (status == 1713 /* RPC_S_ALREADY_LISTENING */)
            status = 0;
    }
    return status;
}

static LONG WINAPI stop_rpc_server(void *interface_spec)
{
    typedef LONG(WINAPI * UNREGISTER_IF)(void *spec, void *uuid, UINT wait);
    HMODULE rpc = rpc_module();
    UNREGISTER_IF unregister = rpc ? (UNREGISTER_IF)GetProcAddress(rpc, "RpcServerUnregisterIf") : nullptr;
    return unregister ? unregister(interface_spec, nullptr, 1) : (LONG)0xC0000002;
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
    data->RegisterStopCallbackDeprecated = SvcDeprecatedRegisterStopCallback;
    data->RegisterStopCallback = SvcRegisterStopCallback;
    data->UnregisterStopCallback = SvcUnregisterStopCallback;
    g_globals = data;
    return TRUE;
}

/* ------------------------------------------------------------------ finding the dll of a service */

/* the key of the service's values: Parameters, or the key of the service when it has none. */
static DWORD open_parameters(const WCHAR *service, HKEY *out)
{
    HKEY services, key, parameters;
    DWORD error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services", 0, KEY_READ, &services);
    if (error)
        return error;
    error = RegOpenKeyExW(services, service, 0, KEY_READ, &key);
    RegCloseKey(services);
    if (error)
        return error;
    error = RegOpenKeyExW(key, L"Parameters", 0, KEY_READ, &parameters);
    if (error == ERROR_FILE_NOT_FOUND) {
        *out = key;
        return ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (!error)
        *out = parameters;
    return error;
}

static BOOL read_flag(HKEY key, const WCHAR *name)
{
    DWORD type = 0, size = sizeof(DWORD), value = 0;
    return RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &size) == ERROR_SUCCESS && type == REG_DWORD &&
           value != 0;
}

/* a path value that names a file: it has to be REG_EXPAND_SZ and not empty, and is expanded and put in lower case. */
static DWORD read_path(HKEY key, const WCHAR *name, WCHAR *out, BOOL optional)
{
    WCHAR raw[MAX_PATH + 1];
    DWORD type = 0, size = sizeof(raw);
    LONG status = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)raw, &size);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        return optional ? ERROR_SUCCESS : (DWORD)status;
    if (status)
        return (DWORD)status;
    if (type != REG_EXPAND_SZ || size < sizeof(WCHAR) || !raw[0])
        return ERROR_FILE_NOT_FOUND;
    raw[MAX_PATH] = 0;
    ExpandEnvironmentStringsW(raw, out, MAX_PATH);
    out[MAX_PATH - 1] = 0;
    LCMapStringW(0x400, LCMAP_LOWERCASE, out, -1, out, MAX_PATH);
    return ERROR_SUCCESS;
}

/* the value ServiceMain names the function of a dll by, as an ansi name. */
static CHAR *read_main_name(HKEY key)
{
    WCHAR raw[MAX_PATH + 1];
    DWORD type = 0, size = sizeof(raw) - sizeof(WCHAR);
    if (RegQueryValueExW(key, L"ServiceMain", nullptr, &type, (BYTE *)raw, &size) || type != REG_SZ)
        return nullptr;
    raw[size / sizeof(WCHAR)] = 0;
    int bytes = WideCharToMultiByte(CP_ACP, 0x400 /* WC_NO_BEST_FIT_CHARS */, raw, -1, nullptr, 0, nullptr, nullptr);
    CHAR *name = bytes > 1 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)bytes) : nullptr;
    if (name && !WideCharToMultiByte(CP_ACP, 0x400, raw, -1, name, bytes, nullptr, nullptr)) {
        HeapFree(GetProcessHeap(), 0, name);
        name = nullptr;
    }
    return name;
}

/* the dll of a service the first time it starts, and whenever it was unloaded and needs its context again. */
static DWORD prepare_dll(HOSTED *s)
{
    if (s->Dll) {
        HOSTED_DLL *dll = s->Dll;
        if (!dll->Module && dll->Manifest && dll->Manifest[0]) {
            ACTCTXW context = {sizeof(context)};
            context.lpSource = dll->Manifest;
            HANDLE made = CreateActCtxW(&context);
            if (made == INVALID_HANDLE_VALUE)
                return GetLastError();
            if (dll->ActCtx)
                ReleaseActCtx(dll->ActCtx);
            dll->ActCtx = made;
        }
        return ERROR_SUCCESS;
    }
    HKEY key;
    DWORD error = open_parameters(s->Name, &key);
    if (error)
        return error;
    WCHAR path[MAX_PATH], manifest[MAX_PATH];
    manifest[0] = 0;
    error = read_path(key, L"ServiceDll", path, FALSE);
    if (!error)
        error = read_path(key, L"ServiceManifest", manifest, TRUE);
    CHAR *main_name = error ? nullptr : read_main_name(key);
    RegCloseKey(key);
    if (error)
        return error;
    HOSTED_DLL *dll = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dll));
    if (!dll) {
        HeapFree(GetProcessHeap(), 0, main_name);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    dll->Path = HostDup(path);
    dll->Manifest = HostDup(manifest);
    if (!dll->Path || !dll->Manifest) {
        HeapFree(GetProcessHeap(), 0, dll->Path);
        HeapFree(GetProcessHeap(), 0, dll->Manifest);
        HeapFree(GetProcessHeap(), 0, dll);
        HeapFree(GetProcessHeap(), 0, main_name);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (manifest[0]) {
        ACTCTXW context = {sizeof(context)};
        context.lpSource = dll->Manifest;
        dll->ActCtx = CreateActCtxW(&context);
        if (dll->ActCtx == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            HeapFree(GetProcessHeap(), 0, dll->Path);
            HeapFree(GetProcessHeap(), 0, dll->Manifest);
            HeapFree(GetProcessHeap(), 0, dll);
            HeapFree(GetProcessHeap(), 0, main_name);
            return error;
        }
    }
    s->Dll = dll;
    s->MainName = main_name;
    return ERROR_SUCCESS;
}

/* a function of the dll, loading it first: its error is the last one when it has none. */
static FARPROC dll_function(HOSTED *s, const CHAR *name, DWORD *error)
{
    HOSTED_DLL *dll = s->Dll;
    ULONG_PTR cookie = 0;
    FARPROC function = nullptr;
    if (error)
        *error = ERROR_SUCCESS;
    if (dll->ActCtx && !ActivateActCtx(dll->ActCtx, &cookie)) {
        if (error)
            *error = GetLastError();
        return nullptr;
    }
    if (!dll->Module)
        dll->Module = LoadLibraryExW(dll->Path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll->Module) {
        if (error)
            *error = GetLastError();
    } else {
        function = GetProcAddress(dll->Module, name);
        if (!function && error)
            *error = GetLastError();
    }
    if (dll->ActCtx)
        DeactivateActCtx(0, cookie);
    return function;
}

/* ------------------------------------------------------------------ starting a service */

static void WINAPI dummy_handler(DWORD control)
{
    (void)control;
}

/* a service that cannot be started says so in the only way the manager hears: it stops, with the error as its exit code. */
void HostAbort(const WCHAR *name, DWORD error)
{
    SERVICE_STATUS status = {SERVICE_WIN32_, SERVICE_STOPPED, SERVICE_ACCEPT_STOP, error, 0, 0, 0};
    HostLog("svchost: %ls does not start: %lu", name, error);
    SERVICE_STATUS_HANDLE handle = RegisterServiceCtrlHandlerW(name, dummy_handler);
    if (handle)
        SetServiceStatus(handle, &status);
}

/*
 * the dispatcher calls this for every service of the group, in a thread of its own, with the name of the one the
 * manager starts as the first argument.
 */
VOID WINAPI HostService(DWORD argc, LPWSTR *argv)
{
    if (!argc || !argv[0])
        return;
    HOSTED *s = find_hosted(argv[0]);
    if (!s)
        return;
    AcquireSRWLockExclusive(&s->Lock);
    DWORD error = prepare_dll(s);
    LPSERVICE_MAIN_FUNCTIONW service_main = nullptr;
    PUSH_GLOBALS push = nullptr, push_ex = nullptr;
    if (!error) {
        service_main = (LPSERVICE_MAIN_FUNCTIONW)dll_function(s, s->MainName ? s->MainName : "ServiceMain", &error);
        if (!error) {
            push_ex = (PUSH_GLOBALS)dll_function(s, "SvchostPushServiceGlobalsEx", nullptr);
            if (!push_ex)
                push = (PUSH_GLOBALS)dll_function(s, "SvchostPushServiceGlobals", nullptr);
        }
    }
    if (error || !service_main) {
        ReleaseSRWLockExclusive(&s->Lock);
        HostAbort(s->Name, error ? error : ERROR_PROC_NOT_FOUND);
        return;
    }
    if (push || push_ex) {
        AcquireSRWLockExclusive(&g_globals_lock);
        if (!g_globals)
            build_globals();
        ReleaseSRWLockExclusive(&g_globals_lock);
    }
    s->Refs++;
    HKEY key;
    if (!open_parameters(s->Name, &key)) {
        if (read_flag(key, L"ServiceDllUnloadOnStop"))
            s->UnloadOnStop = TRUE;
        if (read_flag(key, L"LegacyCOMBehavior"))
            g_com_uninit = FALSE;
        RegCloseKey(key);
    }
    ReleaseSRWLockExclusive(&s->Lock);
    if (g_globals) {
        if (push_ex)
            push_ex(g_globals);
        else if (push)
            push(&g_globals->NullSid);
    }
    service_main(argc, argv);
    release_dll(s);
}

/* once the dispatcher is done: the dlls that leave on a stop are given time to go before COM is shut down. */
void HostWaitForUnloads(void)
{
    for (DWORD i = 0; i < g_hosted_count; i++) {
        HOSTED *s = &g_hosted[i];
        AcquireSRWLockExclusive(&s->Lock);
        if (s->Dll && s->Dll->Module && s->UnloadOnStop) {
            while (s->Dll->Module) {
                if (!SleepConditionVariableSRW(&s->Unloaded, &s->Lock, 5000, 0)) {
                    HostLog("svchost: the dll of %ls is still loaded", s->Name);
                    break;
                }
            }
        }
        ReleaseSRWLockExclusive(&s->Lock);
    }
}
