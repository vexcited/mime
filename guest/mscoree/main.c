#include <mime/win32/winbase.h>
#include <mime/win32/objidl.h>

#define CLR_E_SHIM_RUNTIMELOAD ((INT)0x80131701)

typedef INT(WINAPI *COR_EXE_MAIN)(void);
typedef INT(WINAPI *COR_EXE_MAIN_RET_ADDR)(PVOID address);
typedef BOOL(WINAPI *SHIM_DLL_MAIN)(HINSTANCE instance, DWORD reason, PVOID reserved);
typedef void(WINAPI *SHIM_CLEANUP)(void);
typedef void(WINAPI *SHIM_SET_VERSION)(PWSTR version);
typedef void(WINAPI *REGISTER_SHIM)(SHIM_CLEANUP cleanup, SHIM_SET_VERSION set_version);
typedef void(WINAPI *REGISTER_CLEANUP)(SHIM_CLEANUP cleanup);
typedef void(WINAPI *SET_SHELL_INSTANCE)(HINSTANCE instance);

static RTL_CRITICAL_SECTION g_initialize_lock = RTL_CRITICAL_SECTION_INIT;
static LONG g_initialize_state;
static HINSTANCE g_instance;
static HMODULE g_provider;
static COR_EXE_MAIN g_main;
static COR_EXE_MAIN_RET_ADDR g_main_ret_addr;
static SHIM_DLL_MAIN g_dll_main;
static volatile LONG g_unavailable;

static BOOL append(PWSTR path, PCWSTR suffix)
{
    SIZE_T length = wcslen(path), extra = wcslen(suffix);
    if (extra >= MAX_PATH - length)
        return FALSE;
    memcpy(path + length, suffix, (extra + 1) * sizeof(WCHAR));
    return TRUE;
}

static INT environment(PCWSTR name, PWSTR value)
{
    DWORD count = GetEnvironmentVariableW(name, value, MAX_PATH);
    return count >= MAX_PATH ? -1 : count != 0;
}

static BOOL exists(PCWSTR path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL local_provider(PWSTR path)
{
    DWORD count = GetModuleFileNameW(g_instance, path, MAX_PATH);
    if (!count || count >= MAX_PATH || !append(path, L".local") ||
        GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    path[count] = 0;
    while (count && path[count - 1] != L'\\')
        count--;
    path[count] = 0;
    return append(path, L"mscoreei.dll");
}

static BOOL install_root(PWSTR root)
{
    INT present = environment(L"COMPLUS_InstallRoot", root);
    if (present)
        return present > 0;
    DWORD bytes = MAX_PATH * sizeof(WCHAR);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\.NETFramework", L"InstallRoot",
                        RRF_RT_REG_SZ, nullptr, root, &bytes) == ERROR_SUCCESS && root[0];
}

static BOOL provider_path(PCWSTR version, PWSTR path)
{
    if (local_provider(path))
        return TRUE;
    WCHAR root[MAX_PATH], canonical[MAX_PATH];
    if (!version[0] || !install_root(root))
        return FALSE;
    DWORD count = GetFullPathNameW(root, MAX_PATH, path, nullptr);
    if (!count || count >= MAX_PATH)
        return FALSE;
    if (path[count - 1] != L'\\' && !append(path, L"\\"))
        return FALSE;
    if (!append(path, version))
        return FALSE;
    count = GetFullPathNameW(path, MAX_PATH, canonical, nullptr);
    if (!count || count >= MAX_PATH || lstrcmpiW(path, canonical))
        return FALSE;
    return append(path, L"\\mscoreei.dll");
}

static LONG build_number(PCWSTR text)
{
    while (*text == L' ' || *text == L'\t' || *text == L'\n' || *text == L'\r')
        text++;
    BOOL negative = *text == L'-';
    if (*text == L'+' || *text == L'-')
        text++;
    ULONG value = 0;
    while (*text >= L'0' && *text <= L'9') {
        ULONG digit = (ULONG)(*text++ - L'0');
        /* overflow is clamped; windows behavior for malformed policy values is unverified. */
        if (value > (0x7FFFFFFFu - digit) / 10)
            return negative ? (LONG)0x80000000u : 0x7FFFFFFF;
        value = value * 10 + digit;
    }
    return negative ? -(LONG)value : (LONG)value;
}

static BOOL latest_build(HKEY policy, PCWSTR version, PWSTR build)
{
    HKEY key;
    if (RegOpenKeyExW(policy, version, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    LONG best = -1;
    for (DWORD index = 0;; index++) {
        WCHAR name[MAX_PATH];
        DWORD count = MAX_PATH;
        LSTATUS status = RegEnumValueW(key, index, name, &count, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status == ERROR_MORE_DATA)
            continue;
        if (status)
            break;
        if (!count)
            continue;
        LONG value = build_number(name);
        if (value > best) {
            best = value;
            memcpy(build, name, (count + 1) * sizeof(WCHAR));
        }
    }
    RegCloseKey(key);
    return best >= 0;
}

static BOOL policy_version(PWSTR version)
{
    HKEY policy;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\.NETFramework\\Policy", 0,
                      KEY_ENUMERATE_SUB_KEYS, &policy))
        return FALSE;
    WCHAR previous[MAX_PATH] = {};
    BOOL found = FALSE;
    for (;;) {
        WCHAR selected[MAX_PATH] = {};
        for (DWORD index = 0;; index++) {
            WCHAR name[MAX_PATH];
            DWORD count = MAX_PATH;
            LSTATUS status = RegEnumKeyExW(policy, index, name, &count, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status == ERROR_MORE_DATA)
                continue;
            if (status)
                break;
            if ((!previous[0] || wcscmp(name, previous) < 0) && wcscmp(name, selected) > 0)
                memcpy(selected, name, (count + 1) * sizeof(WCHAR));
        }
        if (!selected[0])
            break;
        wcscpy(previous, selected);
        WCHAR build[MAX_PATH], path[MAX_PATH];
        if (!latest_build(policy, selected, build) || !append(selected, L".") || !append(selected, build))
            continue;
        if (provider_path(selected, path) && exists(path)) {
            wcscpy(version, selected);
            found = TRUE;
            break;
        }
    }
    RegCloseKey(policy);
    return found;
}

static BOOL select_provider(PWSTR path)
{
    if (local_provider(path))
        return TRUE;
    WCHAR version[MAX_PATH] = {}, root[MAX_PATH];
    INT have_version = environment(L"COMPLUS_Version", version);
    INT have_root = environment(L"COMPLUS_InstallRoot", root);
    if (have_version < 0 || have_root < 0)
        return FALSE;
    if (have_version && have_root && provider_path(version, path) && exists(path))
        return TRUE;
    BOOL selected = policy_version(version);
    WCHAR override[MAX_PATH];
    INT have_override = environment(L"COMPLUS_DefaultVersion", override);
    if (have_override < 0)
        return FALSE;
    if (have_override) {
        wcscpy(version, override);
        selected = TRUE;
    }
    if (!selected || !provider_path(version, path))
        return FALSE;
    return TRUE;
}

static void WINAPI provider_cleanup(void)
{
    InterlockedExchange(&g_unavailable, 1);
}

static void WINAPI provider_version(PWSTR version)
{
}

static void initialize_failed(void)
{
    RtlEnterCriticalSection(&g_initialize_lock);
    if (!__atomic_load_n(&g_initialize_state, __ATOMIC_RELAXED))
        __atomic_store_n(&g_initialize_state, 1, __ATOMIC_RELEASE);
    RtlLeaveCriticalSection(&g_initialize_lock);
}

static void initialize_provider(void)
{
    WCHAR path[MAX_PATH];
    if (!select_provider(path)) {
        initialize_failed();
        return;
    }
    HMODULE provider = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!provider) {
        initialize_failed();
        return;
    }
    REGISTER_SHIM register_shim = (REGISTER_SHIM)GetProcAddress(provider, "RegisterShimImplCallback");
    REGISTER_CLEANUP register_cleanup = (REGISTER_CLEANUP)GetProcAddress(provider, "RegisterShimImplCleanupCallback");
    if (!register_shim && !register_cleanup) {
        FreeLibrary(provider);
        initialize_failed();
        return;
    }
    SET_SHELL_INSTANCE set_instance = (SET_SHELL_INSTANCE)GetProcAddress(provider, "SetShellShimInstance");
    COR_EXE_MAIN_RET_ADDR main_ret_addr = (COR_EXE_MAIN_RET_ADDR)GetProcAddress(provider, "_CorExeMain_RetAddr");
    COR_EXE_MAIN main = (COR_EXE_MAIN)GetProcAddress(provider, "_CorExeMain");
    SHIM_DLL_MAIN dll_main = (SHIM_DLL_MAIN)GetProcAddress(provider, "OnShimDllMainCalled");
    RtlEnterCriticalSection(&g_initialize_lock);
    if (!__atomic_load_n(&g_initialize_state, __ATOMIC_RELAXED)) {
        if (set_instance)
            set_instance(g_instance);
        if (register_shim)
            register_shim(provider_cleanup, provider_version);
        else
            register_cleanup(provider_cleanup);
        /* a callback can initialize the provider recursively. */
        if (!__atomic_load_n(&g_initialize_state, __ATOMIC_RELAXED)) {
            g_provider = provider;
            g_main_ret_addr = main_ret_addr;
            g_main = main;
            __atomic_store_n(&g_dll_main, dll_main, __ATOMIC_RELEASE);
            __atomic_store_n(&g_initialize_state, 2, __ATOMIC_RELEASE);
            provider = nullptr;
        }
    }
    RtlLeaveCriticalSection(&g_initialize_lock);
    if (provider)
        FreeLibrary(provider);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_instance = instance;
    SHIM_DLL_MAIN callback = __atomic_load_n(&g_dll_main, __ATOMIC_ACQUIRE);
    if (callback && !InterlockedCompareExchange(&g_unavailable, 0, 0))
        return callback(instance, reason, reserved);
    return TRUE;
}

static HMODULE provider_module(void)
{
    LONG state = __atomic_load_n(&g_initialize_state, __ATOMIC_ACQUIRE);
    if (!state) {
        initialize_provider();
        state = __atomic_load_n(&g_initialize_state, __ATOMIC_ACQUIRE);
    }
    return state == 2 && !InterlockedCompareExchange(&g_unavailable, 0, 0) ? g_provider : nullptr;
}

HRESULT WINAPI GetCLRUEFManager(REFIID iid, IUnknown **manager)
{
    HMODULE provider = provider_module();
    if (!provider)
        return CLR_E_SHIM_RUNTIMELOAD;
    HRESULT(WINAPI *function)(REFIID, IUnknown **) = (void *)GetProcAddress(provider, (PCSTR)24);
    return function ? function(iid, manager) : CLR_E_SHIM_RUNTIMELOAD;
}

BOOL WINAPI GetVersionExWrapper(LPOSVERSIONINFOW version)
{
    return GetVersionExW(version);
}

HRESULT WINAPI CreateConfigStream(PCWSTR filename, IStream **stream)
{
    HMODULE provider = provider_module();
    if (!provider)
        return CLR_E_SHIM_RUNTIMELOAD;
    HRESULT(WINAPI *with_caller)(PCWSTR, IStream **, PVOID) =
        (void *)GetProcAddress(provider, "CreateConfigStream_RetAddr");
    if (with_caller)
        return with_caller(filename, stream, __builtin_return_address(0));
    HRESULT(WINAPI *function)(PCWSTR, IStream **) = (void *)GetProcAddress(provider, "CreateConfigStream");
    return function ? function(filename, stream) : CLR_E_SHIM_RUNTIMELOAD;
}

HRESULT WINAPI CLRCreateInstance(REFCLSID clsid, REFIID iid, PVOID *object)
{
    HMODULE provider = provider_module();
    if (!provider)
        return CLR_E_SHIM_RUNTIMELOAD;
    HRESULT(WINAPI *function)(REFCLSID, REFIID, PVOID *) = (void *)GetProcAddress(provider, "CLRCreateInstance");
    return function ? function(clsid, iid, object) : CLR_E_SHIM_RUNTIMELOAD;
}

HANDLE WINAPI GetProcessExecutableHeap(void)
{
    HMODULE provider = provider_module();
    if (!provider)
        return INVALID_HANDLE_VALUE;
    HANDLE(WINAPI *with_caller)(PVOID) = (void *)GetProcAddress(provider, "GetProcessExecutableHeap_RetAddr");
    if (with_caller)
        return with_caller(__builtin_return_address(0));
    HANDLE(WINAPI *function)(void) = (void *)GetProcAddress(provider, "GetProcessExecutableHeap");
    return function ? function() : INVALID_HANDLE_VALUE;
}

HRESULT WINAPI CorBindToRuntime(PCWSTR version, PCWSTR flavor, REFCLSID clsid, REFIID iid, PVOID *object)
{
    HMODULE provider = provider_module();
    if (!provider)
        return CLR_E_SHIM_RUNTIMELOAD;
    HRESULT(WINAPI *with_caller)(PCWSTR, PCWSTR, REFCLSID, REFIID, PVOID *, PVOID) =
        (void *)GetProcAddress(provider, "CorBindToRuntime_RetAddr");
    if (with_caller)
        return with_caller(version, flavor, clsid, iid, object, __builtin_return_address(0));
    HRESULT(WINAPI *function)(PCWSTR, PCWSTR, REFCLSID, REFIID, PVOID *) =
        (void *)GetProcAddress(provider, "CorBindToRuntime");
    return function ? function(version, flavor, clsid, iid, object) : CLR_E_SHIM_RUNTIMELOAD;
}

HRESULT WINAPI GetCORVersion(PWSTR version, DWORD capacity, DWORD *length)
{
    HMODULE provider = provider_module();
    if (!provider)
        return CLR_E_SHIM_RUNTIMELOAD;
    HRESULT(WINAPI *with_caller)(PWSTR, DWORD, DWORD *, PVOID) =
        (void *)GetProcAddress(provider, "GetCORVersion_RetAddr");
    if (with_caller)
        return with_caller(version, capacity, length, __builtin_return_address(0));
    HRESULT(WINAPI *function)(PWSTR, DWORD, DWORD *) = (void *)GetProcAddress(provider, "GetCORVersion");
    return function ? function(version, capacity, length) : CLR_E_SHIM_RUNTIMELOAD;
}

HRESULT WINAPI GetRequestedRuntimeInfo(PCWSTR executable, PCWSTR version, PCWSTR configuration,
                                      DWORD startup_flags, DWORD runtime_flags, PWSTR directory,
                                      DWORD directory_capacity, DWORD *directory_length, PWSTR runtime_version,
                                      DWORD version_capacity, DWORD *version_length)
{
    HMODULE provider = provider_module();
    if (!provider)
        return CLR_E_SHIM_RUNTIMELOAD;
    HRESULT(WINAPI *with_caller)(PCWSTR, PCWSTR, PCWSTR, DWORD, DWORD, PWSTR, DWORD, DWORD *, PWSTR,
                                DWORD, DWORD *, PVOID) =
        (void *)GetProcAddress(provider, "GetRequestedRuntimeInfo_RetAddr");
    if (with_caller)
        return with_caller(executable, version, configuration, startup_flags, runtime_flags, directory,
                           directory_capacity, directory_length, runtime_version, version_capacity,
                           version_length, __builtin_return_address(0));
    HRESULT(WINAPI *function)(PCWSTR, PCWSTR, PCWSTR, DWORD, DWORD, PWSTR, DWORD, DWORD *, PWSTR,
                             DWORD, DWORD *) = (void *)GetProcAddress(provider, "GetRequestedRuntimeInfo");
    return function ? function(executable, version, configuration, startup_flags, runtime_flags, directory,
                               directory_capacity, directory_length, runtime_version, version_capacity,
                               version_length) : CLR_E_SHIM_RUNTIMELOAD;
}

INT WINAPI _CorExeMain(void)
{
    PVOID caller = __builtin_return_address(0);
    LONG state = __atomic_load_n(&g_initialize_state, __ATOMIC_ACQUIRE);
    if (!state) {
        initialize_provider();
        state = __atomic_load_n(&g_initialize_state, __ATOMIC_ACQUIRE);
    }
    if (state == 2 && !InterlockedCompareExchange(&g_unavailable, 0, 0)) {
        if (g_main_ret_addr)
            return g_main_ret_addr(caller);
        if (g_main)
            return g_main();
    }
    DbgPrint("mscoree: .NET runtime provider unavailable (%08x)\n", (ULONG)CLR_E_SHIM_RUNTIMELOAD);
    return CLR_E_SHIM_RUNTIMELOAD;
}
