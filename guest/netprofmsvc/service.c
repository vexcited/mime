#include "netprofmsvc.h"

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop;
static DWORD g_cookie;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE factory_qi(IClassFactory *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory)) {
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_addref(IClassFactory *self)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *self)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *self, IUnknown *outer, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return NpManagerCreate(iid, out);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *self, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_qi, factory_addref, factory_release, factory_create, factory_lock,
};
static IClassFactory g_factory = {&g_factory_vtbl};

static void set_state(DWORD state)
{
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
    g_status.dwCheckPoint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? 1 : 0;
    g_status.dwWaitHint = g_status.dwCheckPoint ? 60000 : 0;
    if (g_handle)
        SetServiceStatus(g_handle, &g_status);
}

static DWORD WINAPI control(DWORD code, DWORD type, void *data, void *context)
{
    switch (code) {
    case SERVICE_CONTROL_STOP:
        if (g_status.dwCurrentState == SERVICE_RUNNING) {
            set_state(SERVICE_STOP_PENDING);
            SetEvent(g_stop);
        }
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static void fail(HRESULT hr)
{
    g_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
    g_status.dwServiceSpecificExitCode = (DWORD)hr;
    set_state(SERVICE_STOPPED);
}

/* The service stays once started, as on Windows, and serves its class from the threads of com. */
DECLSPEC_EXPORT void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_handle = RegisterServiceCtrlHandlerExW(L"netprofm", control, nullptr);
    if (!g_handle)
        return;
    set_state(SERVICE_START_PENDING);
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT hr = g_stop ? CoInitializeEx(nullptr, COINIT_MULTITHREADED) : HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr)) {
        fail(hr);
        return;
    }
    hr = CoRegisterClassObject(&CLSID_NetworkListManagerService, (IUnknown *)&g_factory, CLSCTX_LOCAL_SERVER,
                               REGCLS_MULTIPLEUSE, &g_cookie);
    if (FAILED(hr)) {
        fail(hr);
        CoUninitialize();
        return;
    }
    set_state(SERVICE_RUNNING);
    WaitForSingleObject(g_stop, INFINITE);
    CoRevokeClassObject(g_cookie);
    NpDisconnectObjects();
    CoUninitialize();
    set_state(SERVICE_STOPPED);
    CloseHandle(g_stop);
    g_stop = nullptr;
}
