#include "qmgr.h"

typedef SERVICE_STATUS_HANDLE(WINAPI *REGISTER_HANDLER)(LPCWSTR name, LPHANDLER_FUNCTION_EX handler, LPVOID context);

/* the module states of BitsComModule: 2 and 3 take calls, everything else answers CO_E_SERVER_STOPPING. */
enum { MODULE_NONE, MODULE_STARTING, MODULE_RUNNING, MODULE_REGISTERED, MODULE_STOPPING };

#define SERVICE_ACCEPT_INTERNAL_ 0x1000
#define SERVICE_CONTROL_USER_SESSION_ 0x21
#define STATUS_RETRIES 10

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop;
static volatile LONG g_module;
static volatile LONG g_calls;
static volatile LONG g_objects;
static volatile LONG g_threads;
static HANDLE g_threads_done;
static DWORD g_cookies[14];

MIME_DEFINE_GUID(CLSID_BackgroundCopyManager1_5_, 0xF087771F, 0xD74F, 0x4C1A, 0xBB, 0x8A, 0xE1, 0x6A, 0xCA, 0x91, 0x24,
                 0xEA);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager2_0_, 0x6D18AD12, 0xBDE3, 0x4393, 0xB3, 0x11, 0x09, 0x9C, 0x34, 0x6E, 0x6D,
                 0xF9);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager2_5_, 0x03CA98D6, 0xFF5D, 0x49B8, 0xAB, 0xC6, 0x03, 0xDD, 0x84, 0x12, 0x70,
                 0x20);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager3_0_, 0x659CDEA7, 0x489E, 0x11D9, 0xA9, 0xCD, 0x00, 0x0D, 0x56, 0x96, 0x52,
                 0x51);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager4_0_, 0xBB6DF56B, 0xCACE, 0x11DC, 0x99, 0x92, 0x00, 0x19, 0xB9, 0x3A, 0x3A,
                 0x84);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager5_0_, 0x1ECCA34C, 0xE88A, 0x44E3, 0x8D, 0x6A, 0x89, 0x21, 0xBD, 0xE9, 0xE4,
                 0x52);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager10_0_, 0x4D233817, 0xB456, 0x4E75, 0x83, 0xD2, 0xB1, 0x7D, 0xEC, 0x54, 0x4D,
                 0x12);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager10_1_, 0x4BD3E4E1, 0x7BD4, 0x4A2B, 0x99, 0x64, 0x49, 0x64, 0x00, 0xDE, 0x51,
                 0x93);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager10_2_, 0x4575438F, 0xA6C8, 0x4976, 0xB0, 0xFE, 0x2F, 0x26, 0xB8, 0x0D, 0x95,
                 0x9E);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager10_2i_, 0x77646F02, 0x8A5F, 0x4451, 0xB8, 0xEB, 0x42, 0x48, 0xF0, 0x4C, 0x38,
                 0xCE);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager10_3_, 0x5FD42AD5, 0xC04E, 0x4D36, 0xAD, 0xD7, 0xE0, 0x8F, 0xF1, 0x57, 0x37,
                 0xAD);
MIME_DEFINE_GUID(CLSID_BackgroundCopyManager10_3i_, 0x675259D2, 0xC8D5, 0x4AD7, 0x89, 0xAE, 0xE9, 0xE4, 0x4C, 0x06, 0x05,
                 0x68);
MIME_DEFINE_GUID(CLSID_GlobalOptions_, 0x0000034B, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IGlobalOptions_, 0x0000015B, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef struct IGlobalOptions_ IGlobalOptions_;
typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IGlobalOptions_ *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IGlobalOptions_ *This);
    ULONG(STDMETHODCALLTYPE *Release)(IGlobalOptions_ *This);
    HRESULT(STDMETHODCALLTYPE *Set)(IGlobalOptions_ *This, DWORD property, ULONG_PTR value);
    HRESULT(STDMETHODCALLTYPE *Query)(IGlobalOptions_ *This, DWORD property, ULONG_PTR *value);
} IGlobalOptionsVtbl_;
struct IGlobalOptions_ {
    const IGlobalOptionsVtbl_ *lpVtbl;
};

#define COMGLB_UNMARSHALING_POLICY_ 5
#define COMGLB_UNMARSHALING_POLICY_STRONG_ 1

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* ------------------------------------------------------------------ calls, objects and threads */

HRESULT QmEnter(void)
{
    InterlockedIncrement(&g_calls);
    LONG state = g_module;
    if (state != MODULE_RUNNING && state != MODULE_REGISTERED) {
        InterlockedDecrement(&g_calls);
        return CO_E_SERVER_STOPPING_;
    }
    return S_OK;
}

void QmLeave(void)
{
    InterlockedDecrement(&g_calls);
}

BOOL QmRunning(void)
{
    return g_module == MODULE_RUNNING || g_module == MODULE_REGISTERED;
}

/* every live object counts, as each holds a shared service reference on windows, and the service stays while one is out. */
void QmObjectAdd(void)
{
    InterlockedIncrement(&g_objects);
}

void QmObjectRelease(void)
{
    if (!InterlockedDecrement(&g_objects))
        QmWake();
}

BOOL ScheduleIdle(void);

static BOOL idle(void)
{
    return !g_objects && !g_calls && ScheduleIdle();
}

void QmThreadStart(void)
{
    if (InterlockedIncrement(&g_threads) == 1)
        ResetEvent(g_threads_done);
}

void QmThreadEnd(void)
{
    if (!InterlockedDecrement(&g_threads))
        SetEvent(g_threads_done);
}

typedef struct START {
    LPTHREAD_START_ROUTINE routine;
    void *argument;
} START;

static DWORD WINAPI thread_start(void *argument)
{
    START start = *(START *)argument;
    LocalFree(argument);
    DWORD result = start.routine(start.argument);
    QmThreadEnd();
    return result;
}

/* the threads of the service are counted, so the dll is not left while one of them still runs in it. */
HRESULT QmStartThread(LPTHREAD_START_ROUTINE routine, void *argument, HANDLE *out)
{
    START *start = LocalAlloc(LMEM_FIXED, sizeof(*start));
    if (!start)
        return E_OUTOFMEMORY;
    start->routine = routine;
    start->argument = argument;
    QmThreadStart();
    HANDLE thread = CreateThread(nullptr, 0, thread_start, start, 0, nullptr);
    if (!thread) {
        DWORD error = GetLastError();
        LocalFree(start);
        QmThreadEnd();
        return HRESULT_FROM_WIN32(error);
    }
    if (out)
        *out = thread;
    else
        CloseHandle(thread);
    return S_OK;
}

/* ------------------------------------------------------------------ the class factories */

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

/* BackgroundCopyManagerExternalFactory::CreateInstance: any other interface than the manager's is E_NOTIMPL. */
static HRESULT STDMETHODCALLTYPE manager_create(IClassFactory *self, IUnknown *outer, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION_;
    if (!IsEqualGUID(iid, &IID_IBackgroundCopyManager) && !IsEqualGUID(iid, &IID_IUnknown))
        return E_NOTIMPL;
    if (g_config.setup_in_progress)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY_);
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    hr = ManagerCreate(iid, out);
    QmLeave();
    return hr;
}

/* OldQmgrInterfaceFactory::CreateInstance: an interface other than IBackgroundCopyQMgr is E_OUTOFMEMORY there. */
static HRESULT STDMETHODCALLTYPE legacy_create(IClassFactory *self, IUnknown *outer, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (g_module != MODULE_RUNNING && g_module != MODULE_REGISTERED)
        return CO_E_SERVER_STOPPING_;
    if (outer)
        return CLASS_E_NOAGGREGATION_;
    if (!IsEqualGUID(iid, &IID_IBackgroundCopyQMgr) && !IsEqualGUID(iid, &IID_IUnknown))
        return E_OUTOFMEMORY;
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    hr = LegacyManagerCreate(iid, out);
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *self, BOOL lock)
{
    if (lock)
        QmObjectAdd();
    else
        QmObjectRelease();
    return S_OK;
}

static const IClassFactoryVtbl g_manager_factory_vtbl = {factory_qi, factory_addref, factory_release, manager_create,
                                                          factory_lock};
static const IClassFactoryVtbl g_legacy_factory_vtbl = {factory_qi, factory_addref, factory_release, legacy_create,
                                                         factory_lock};
static IClassFactory g_manager_factory = {&g_manager_factory_vtbl};
static IClassFactory g_legacy_factory = {&g_legacy_factory_vtbl};

static const GUID *const g_classes[] = {
    &CLSID_BackgroundCopyManager,       &CLSID_BackgroundCopyManager1_5_,   &CLSID_BackgroundCopyManager2_0_,
    &CLSID_BackgroundCopyManager2_5_,   &CLSID_BackgroundCopyManager3_0_,   &CLSID_BackgroundCopyManager4_0_,
    &CLSID_BackgroundCopyManager5_0_,   &CLSID_BackgroundCopyManager10_0_,  &CLSID_BackgroundCopyManager10_1_,
    &CLSID_BackgroundCopyManager10_2_,  &CLSID_BackgroundCopyManager10_2i_, &CLSID_BackgroundCopyManager10_3_,
    &CLSID_BackgroundCopyManager10_3i_, &CLSID_BackgroundCopyQMgr,
};

static HRESULT register_classes(void)
{
    for (DWORD i = 0; i < ARRAYSIZE(g_classes); i++) {
        IClassFactory *factory = g_classes[i] == &CLSID_BackgroundCopyQMgr ? &g_legacy_factory : &g_manager_factory;
        HRESULT hr = CoRegisterClassObject(g_classes[i], (IUnknown *)factory, CLSCTX_LOCAL_SERVER,
                                           REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED, &g_cookies[i]);
        if (FAILED(hr))
            return hr;
    }
    return CoResumeClassObjects();
}

static void revoke_classes(void)
{
    for (DWORD i = 0; i < ARRAYSIZE(g_classes); i++) {
        if (g_cookies[i])
            CoRevokeClassObject(g_cookies[i]);
        g_cookies[i] = 0;
    }
}

/* ------------------------------------------------------------------ the service */

/* UpdateServiceState: a state change is retried ten times one second apart, as SetServiceStatus can fail at start. */
static void set_state(DWORD state)
{
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_TIMECHANGE | SERVICE_ACCEPT_INTERNAL_ | SERVICE_ACCEPT_SESSIONCHANGE;
    if (state == SERVICE_RUNNING)
        g_status.dwControlsAccepted |= SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN;
    if (state == SERVICE_STOPPED || state == SERVICE_STOP_PENDING)
        g_status.dwControlsAccepted = 0;
    g_status.dwCheckPoint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? g_status.dwCheckPoint + 1 : 0;
    g_status.dwWaitHint = g_status.dwCheckPoint ? 240000 : 0;
    if (!g_handle)
        return;
    for (int i = 0; i < STATUS_RETRIES && !SetServiceStatus(g_handle, &g_status); i++)
        Sleep(1000);
}

typedef struct WTS_SESSION_NOTIFICATION_ {
    DWORD cbSize;
    DWORD dwSessionId;
} WTS_SESSION_NOTIFICATION_;

static void session_event(DWORD type, DWORD session)
{
    if (type != 5 && type != 6)
        return;
    HANDLE token = nullptr;
    HMODULE wts = LoadLibraryExW(L"wtsapi32.dll", nullptr, 0x800);
    BOOL(WINAPI * query)(ULONG, PHANDLE) = wts ? (void *)GetProcAddress(wts, "WTSQueryUserToken") : nullptr;
    if (query && query(session, &token)) {
        BYTE buffer[sizeof(PSID) * 2 + SECURITY_MAX_SID_SIZE];
        DWORD size;
        if (GetTokenInformation(token, TOKEN_USER_CLASS_, buffer, sizeof(buffer), &size)) {
            if (type == 5)
                JobUserLogon(*(PSID *)buffer);
            else
                JobUserLogoff(*(PSID *)buffer);
        }
        CloseHandle(token);
    } else if (type == 5)
        JobUserLogon(nullptr);
    if (wts)
        FreeLibrary(wts);
}

static DWORD WINAPI control(DWORD code, DWORD type, void *data, void *context)
{
    switch (code) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_PRESHUTDOWN:
        set_state(SERVICE_STOP_PENDING);
        SetEvent(g_stop);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    case SERVICE_CONTROL_TIMECHANGE:
        if (g_module == MODULE_RUNNING || g_module == MODULE_REGISTERED)
            QmWake();
        return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE:
        if (data && (g_status.dwCurrentState == SERVICE_RUNNING || g_status.dwCurrentState == SERVICE_PAUSED))
            session_event(type, ((WTS_SESSION_NOTIFICATION_ *)data)->dwSessionId);
        return NO_ERROR;
    case SERVICE_CONTROL_USER_SESSION_:
        /* the user manager session event: 5 logon, 6 logoff, the session id in the second dword. */
        if (data && (g_status.dwCurrentState == SERVICE_RUNNING || g_status.dwCurrentState == SERVICE_PAUSED))
            session_event(type, ((DWORD *)data)[1]);
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

typedef struct {
    DWORD Flags;
} REDIRECTION_POLICY_;

static void redirection_guard(void)
{
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\BITS", L"RedirectionGuard", RRF_RT_REG_DWORD,
                 nullptr, &value, &size);
    if (!value)
        return;
    REDIRECTION_POLICY_ policy = {1};
    SetProcessMitigationPolicy(ProcessRedirectionTrustPolicy, &policy, sizeof(policy));
}

static void strong_unmarshaling(void)
{
    IGlobalOptions_ *options = nullptr;
    if (SUCCEEDED(CoCreateInstance(&CLSID_GlobalOptions_, nullptr, CLSCTX_INPROC_SERVER, &IID_IGlobalOptions_,
                                   (void **)&options))) {
        options->lpVtbl->Set(options, COMGLB_UNMARSHALING_POLICY_, COMGLB_UNMARSHALING_POLICY_STRONG_);
        options->lpVtbl->Release(options);
    }
}

/*
 * the service runs on the thread svchost gives it and returns when it stops. windows lets com stop it through
 * CoRegisterServerShutdownDelay once the shared service references are gone; mime has no such contract, so the
 * service counts its objects and runnable jobs itself and stops IdleStopDelayMilliseconds after the last went.
 */
static HRESULT run_service(void)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return hr;
    strong_unmarshaling();
    ConfigLoad();
    g_module = MODULE_STARTING;
    StoreOpen();
    if (!g_config.setup_in_progress)
        StoreLoadJobs();
    NotifyStart();
    ScheduleStart();
    g_module = MODULE_RUNNING;
    hr = register_classes();
    if (FAILED(hr)) {
        g_module = MODULE_STOPPING;
        revoke_classes();
    } else {
        g_module = MODULE_REGISTERED;
        set_state(SERVICE_RUNNING);
        ULONGLONG idle_since = 0;
        while (WaitForSingleObject(g_stop, 1000) == WAIT_TIMEOUT) {
            if (!idle()) {
                idle_since = 0;
                continue;
            }
            if (!idle_since)
                idle_since = GetTickCount64();
            else if (GetTickCount64() - idle_since >= g_config.idle_stop_ms)
                break;
        }
        set_state(SERVICE_STOP_PENDING);
        g_module = MODULE_STOPPING;
        revoke_classes();
    }
    ScheduleStop();
    JobShutdown();
    NotifyStop();
    while (g_calls)
        Sleep(10);
    WaitForSingleObject(g_threads_done, INFINITE);
    TransferCloseSession();
    StoreClose();
    SecurityShutdown();
    CoUninitialize();
    g_module = MODULE_NONE;
    return hr;
}

void WINAPI BITSServiceMain(DWORD argc, LPWSTR *argv, void *register_handler)
{
    REGISTER_HANDLER reg = register_handler ? (REGISTER_HANDLER)register_handler : RegisterServiceCtrlHandlerExW;
    memset(&g_status, 0, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_handle = reg(L"BITS", control, nullptr);
    if (!g_handle)
        return;
    set_state(SERVICE_START_PENDING);
    redirection_guard();
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_threads_done = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    HRESULT hr = g_stop && g_threads_done ? run_service() : HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr)) {
        g_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_status.dwServiceSpecificExitCode = (DWORD)hr;
    }
    if (g_stop)
        CloseHandle(g_stop);
    if (g_threads_done)
        CloseHandle(g_threads_done);
    g_stop = g_threads_done = nullptr;
    set_state(SERVICE_STOPPED);
    g_handle = nullptr;
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    BITSServiceMain(argc, argv, (void *)RegisterServiceCtrlHandlerExW);
}
