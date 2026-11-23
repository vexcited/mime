#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>

/*
 * The services of the security authority's process are not started with the two arguments of a service main. The
 * process calls the export with a third one, a block the library fills with its stop callback, the event that asks
 * for the stop and the callback's argument, and the export returns as soon as the service runs. The process waits on
 * the event and calls the callback, which reports the service stopped and closes the event itself.
 */
typedef struct STOP_CONTEXT {
    LONG(WINAPI *callback)(void *argument);
    HANDLE event;
    void *argument;
} STOP_CONTEXT;

typedef LONG(WINAPI *AUTHORITY_SERVICE_MAIN)(DWORD argc, LPWSTR *argv, STOP_CONTEXT *stop);

typedef struct AUTHORITY_SERVICE {
    const WCHAR *name;
    const CHAR *main;
    STOP_CONTEXT stop;
    HANDLE wait;
} AUTHORITY_SERVICE;

static AUTHORITY_SERVICE g_services[] = {
    {L"KeyIso", "KeyIsoServiceMain"},
};

static WCHAR *service_dll(const WCHAR *name)
{
    WCHAR path[300], raw[MAX_PATH];
    wcscpy(path, L"SYSTEM\\CurrentControlSet\\Services\\");
    wcscat(path, name);
    wcscat(path, L"\\Parameters");
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return nullptr;
    DWORD type = 0, size = sizeof(raw) - sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(key, L"ServiceDll", nullptr, &type, (BYTE *)raw, &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return nullptr;
    raw[size / sizeof(WCHAR)] = 0;
    WCHAR *expanded = HeapAlloc(GetProcessHeap(), 0, MAX_PATH * sizeof(WCHAR));
    if (expanded && !ExpandEnvironmentStringsW(raw, expanded, MAX_PATH)) {
        HeapFree(GetProcessHeap(), 0, expanded);
        expanded = nullptr;
    }
    return expanded;
}

static DWORD WINAPI no_control(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
    return control == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void report_stopped(const WCHAR *name, DWORD error)
{
    SERVICE_STATUS_HANDLE handle = RegisterServiceCtrlHandlerExW(name, no_control, nullptr);
    if (!handle)
        return;
    SERVICE_STATUS status = {};
    status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    status.dwCurrentState = SERVICE_STOPPED;
    status.dwWin32ExitCode = error;
    SetServiceStatus(handle, &status);
}

static void CALLBACK stop_asked(PVOID parameter, BOOLEAN timed_out)
{
    AUTHORITY_SERVICE *service = parameter;
    HANDLE wait = InterlockedExchangePointer(&service->wait, nullptr);
    STOP_CONTEXT stop = service->stop;
    memset(&service->stop, 0, sizeof(service->stop));
    if (stop.callback)
        stop.callback(stop.argument);
    if (wait)
        UnregisterWaitEx(wait, nullptr);
}

static VOID WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    AUTHORITY_SERVICE *service = nullptr;
    for (SIZE_T i = 0; argc && i < RTL_NUMBER_OF(g_services); i++)
        if (!_wcsicmp(argv[0], g_services[i].name))
            service = &g_services[i];
    if (!service)
        return;
    WCHAR *path = service_dll(service->name);
    HMODULE module = path ? LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
    DWORD error = module ? ERROR_SUCCESS : path ? GetLastError() : ERROR_FILE_NOT_FOUND;
    if (path)
        HeapFree(GetProcessHeap(), 0, path);
    AUTHORITY_SERVICE_MAIN main = module ? (AUTHORITY_SERVICE_MAIN)GetProcAddress(module, service->main) : nullptr;
    if (module && !main)
        error = ERROR_PROC_NOT_FOUND;
    if (!main) {
        report_stopped(service->name, error);
        return;
    }
    LONG status = main(argc, argv, &service->stop);
    if (status < 0) {
        report_stopped(service->name, (DWORD)status);
        return;
    }
    if (service->stop.event &&
        !RegisterWaitForSingleObject(&service->wait, service->stop.event, stop_asked, service, INFINITE,
                                     WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION))
        service->wait = nullptr;
}

void entry(void)
{
    SERVICE_TABLE_ENTRYW table[RTL_NUMBER_OF(g_services) + 1] = {};
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_services); i++) {
        table[i].lpServiceName = (WCHAR *)g_services[i].name;
        table[i].lpServiceProc = service_main;
    }
    StartServiceCtrlDispatcherW(table);
    ExitProcess(0);
}
