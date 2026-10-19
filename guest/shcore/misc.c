/* the rest of shcore: strings, thread references, IsOS, the process app id and COM helpers. */
#include "shcore_private.h"
#include <mime/shellclient.h>

#define STARTF_TITLEISLINKNAME 0x00000800
#define STARTF_TITLEISAPPID 0x00001000
/* marks a title shcore allocated itself. */
#define STARTF_SHCORE_APPID 0x00004000
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)

MIME_DEFINE_GUID(IID_IObjectWithSite, 0xfc4801a3, 0x2ba9, 0x11cf, 0xa2, 0x29, 0x00, 0xaa, 0x00, 0x3d, 0x73, 0x52);
MIME_DEFINE_GUID(IID_IInternetSecurityManager, 0x79eac9ee, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9,
                 0x0b);
MIME_DEFINE_GUID(IID_IInternetSecurityMgrSite, 0x79eac9ed, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9,
                 0x0b);
MIME_DEFINE_GUID(IID_IServiceProvider, 0x6d5140c1, 0x7436, 0x11ce, 0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa);

static DWORD g_thread_ref = TLS_OUT_OF_INDEXES;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_thread_ref = TlsAlloc();
    return TRUE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(void *class_id, void **factory)
{
    if (factory)
        *factory = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ------------------------------------------------------------------ strings */

HRESULT ScAllocString(LPCWSTR source, LPWSTR *out)
{
    SIZE_T size = (wcslen(source) + 1) * sizeof(WCHAR);
    *out = CoTaskMemAlloc(size);
    if (!*out)
        return E_OUTOFMEMORY;
    memcpy(*out, source, size);
    return S_OK;
}

HRESULT WINAPI SHStrDupW(LPCWSTR string, LPWSTR *copy)
{
    if (!copy)
        return E_INVALIDARG;
    *copy = nullptr;
    if (!string)
        return E_INVALIDARG;
    return ScAllocString(string, copy);
}

HRESULT WINAPI SHStrDupA(LPCSTR string, LPWSTR *copy)
{
    if (!copy)
        return E_INVALIDARG;
    *copy = nullptr;
    if (!string)
        return E_INVALIDARG;
    int length = MultiByteToWideChar(CP_ACP, 0, string, -1, nullptr, 0);
    *copy = CoTaskMemAlloc((SIZE_T)length * sizeof(WCHAR));
    if (!*copy)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, string, -1, *copy, length);
    return S_OK;
}

/* the characters written, the terminator included; a short buffer keeps what fits. */
int WINAPI SHAnsiToUnicode(LPCSTR source, LPWSTR buffer, int length)
{
    if (!buffer || length <= 0)
        return 0;
    int n = MultiByteToWideChar(CP_ACP, 0, source, -1, buffer, length);
    if (!n) {
        n = MultiByteToWideChar(CP_ACP, 0, source, length - 1, buffer, length - 1);
        buffer[n] = 0;
        n++;
    }
    return n;
}

int WINAPI SHUnicodeToAnsi(LPCWSTR source, LPSTR buffer, int length)
{
    if (!buffer || length <= 0)
        return 0;
    int n = WideCharToMultiByte(CP_ACP, 0, source, -1, buffer, length, nullptr, nullptr);
    if (!n) {
        n = WideCharToMultiByte(CP_ACP, 0, source, -1, nullptr, 0, nullptr, nullptr);
        int fit = 0;
        for (int count = 1; count <= (int)wcslen(source); count++) {
            if (WideCharToMultiByte(CP_ACP, 0, source, count, nullptr, 0, nullptr, nullptr) > length - 1)
                break;
            fit = count;
        }
        n = WideCharToMultiByte(CP_ACP, 0, source, fit, buffer, length - 1, nullptr, nullptr);
        buffer[n] = 0;
        n++;
    }
    return n;
}

int WINAPI SHAnsiToAnsi(LPCSTR source, LPSTR buffer, int length)
{
    if (!buffer || length <= 0)
        return 0;
    int n = 0;
    while (n < length - 1 && source[n]) {
        buffer[n] = source[n];
        n++;
    }
    buffer[n] = 0;
    return n + 1;
}

int WINAPI SHUnicodeToUnicode(LPCWSTR source, LPWSTR buffer, int length)
{
    if (!buffer || length <= 0)
        return 0;
    int n = 0;
    while (n < length - 1 && source[n]) {
        buffer[n] = source[n];
        n++;
    }
    buffer[n] = 0;
    return n + 1;
}

/* ------------------------------------------------------------------ the app id of the process */

/* kept in the process parameters as a title marked STARTF_TITLEISAPPID, where GetStartupInfo shows it. */
HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(LPCWSTR id)
{
    if (id && wcslen(id) >= 128)
        return E_INVALIDARG;
    PRTL_USER_PROCESS_PARAMETERS params = NtCurrentPeb()->ProcessParameters;
    if (!params)
        return E_FAIL;
    if ((params->WindowFlags & (STARTF_TITLEISAPPID | STARTF_SHCORE_APPID)) ==
        (STARTF_TITLEISAPPID | STARTF_SHCORE_APPID)) {
        LocalFree(params->WindowTitle.Buffer);
        RtlInitUnicodeString(&params->WindowTitle, nullptr);
        params->WindowFlags &= ~(STARTF_TITLEISAPPID | STARTF_SHCORE_APPID);
    }
    if (!id)
        return E_INVALIDARG;
    SIZE_T size = (wcslen(id) + 1) * sizeof(WCHAR);
    WCHAR *copy = LocalAlloc(LMEM_FIXED, size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, id, size);
    params->WindowFlags = (params->WindowFlags & ~STARTF_TITLEISLINKNAME) | STARTF_TITLEISAPPID | STARTF_SHCORE_APPID;
    RtlInitUnicodeString(&params->WindowTitle, copy);
    MimeShellProcessAppId(id);
    return S_OK;
}

HRESULT WINAPI GetCurrentProcessExplicitAppUserModelID(LPWSTR *id)
{
    *id = nullptr;
    STARTUPINFOW info = {.cb = sizeof(info)};
    GetStartupInfoW(&info);
    if (!(info.dwFlags & STARTF_TITLEISAPPID) || !info.lpTitle || !*info.lpTitle)
        return E_FAIL;
    return ScAllocString(info.lpTitle, id);
}

/* ------------------------------------------------------------------ thread and process references */

HRESULT WINAPI SHGetThreadRef(IUnknown **unknown)
{
    *unknown = TlsGetValue(g_thread_ref);
    if (!*unknown)
        return E_NOINTERFACE;
    (*unknown)->lpVtbl->AddRef(*unknown);
    return S_OK;
}

HRESULT WINAPI SHSetThreadRef(IUnknown *unknown)
{
    return TlsSetValue(g_thread_ref, unknown) ? S_OK : E_FAIL;
}

/* the reference SHCreateThread took for a CTF_REF_COUNTED thread. */
static DWORD g_created_ref = TLS_OUT_OF_INDEXES;

HRESULT WINAPI SHReleaseThreadRef(void)
{
    if (g_created_ref == TLS_OUT_OF_INDEXES)
        return S_FALSE;
    IUnknown *unknown = TlsGetValue(g_created_ref);
    if (!unknown)
        return S_FALSE;
    TlsSetValue(g_created_ref, nullptr);
    unknown->lpVtbl->Release(unknown);
    return S_OK;
}

typedef struct {
    IUnknown iface;
    LONG *refs;
} THREADREF;

static HRESULT WINAPI ref_query(IUnknown *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *object = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI ref_add_ref(IUnknown *iface)
{
    return (ULONG)InterlockedIncrement(((THREADREF *)iface)->refs);
}

static ULONG WINAPI ref_release(IUnknown *iface)
{
    THREADREF *ref = (THREADREF *)iface;
    LONG refs = InterlockedDecrement(ref->refs);
    if (!refs)
        LocalFree(ref);
    return (ULONG)refs;
}

static const IUnknownVtbl ref_vtbl = {ref_query, ref_add_ref, ref_release};

/* an object counting into the caller's *refs, which starts at one. */
HRESULT WINAPI SHCreateThreadRef(LONG *refs, IUnknown **unknown)
{
    if (!refs || !unknown)
        return E_INVALIDARG;
    *unknown = nullptr;
    THREADREF *ref = LocalAlloc(LMEM_ZEROINIT, sizeof(*ref));
    if (!ref)
        return E_OUTOFMEMORY;
    ref->iface.lpVtbl = &ref_vtbl;
    ref->refs = refs;
    *refs = 1;
    *unknown = &ref->iface;
    return S_OK;
}

#define CTF_INSECURE 0x00000001
#define CTF_THREAD_REF 0x00000002
#define CTF_PROCESS_REF 0x00000004
#define CTF_COINIT_STA 0x00000008
#define CTF_FREELIBANDEXIT 0x00000010
#define CTF_REF_COUNTED 0x00000020
#define CTF_WAIT_ALLOWCOM 0x00000040
#define CTF_UNUSED 0x00000080
#define CTF_INHERITWOW64 0x00000100
#define CTF_WAIT_NO_REENTRANCY 0x00000200
#define CTF_KEYBOARD_LOCALE 0x00000400
#define CTF_OLEINITIALIZE 0x00000800
#define CTF_COINIT_MTA 0x00001000
#define CTF_NOADDREFLIB 0x00002000

typedef DWORD(WINAPI *THREAD_START)(void *);

typedef struct {
    THREAD_START start, callback;
    void *data;
    DWORD flags;
    HANDLE ready;
    IUnknown *thread_ref, *process_ref;
    HMODULE library;
} THREADSTART;

static IUnknown *g_process_ref;

static DWORD WINAPI thread_main(void *param)
{
    THREADSTART *start = param;
    THREADSTART copy = *start;
    if (copy.flags & (CTF_COINIT_STA | CTF_COINIT_MTA))
        CoInitializeEx(nullptr, (copy.flags & CTF_COINIT_MTA) ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (copy.thread_ref)
        SHSetThreadRef(copy.thread_ref);
    if (copy.callback)
        copy.callback(copy.data);
    SetEvent(copy.ready);
    DWORD result = copy.start ? copy.start(copy.data) : 0;
    if (copy.thread_ref) {
        SHSetThreadRef(nullptr);
        copy.thread_ref->lpVtbl->Release(copy.thread_ref);
    }
    if (copy.process_ref)
        copy.process_ref->lpVtbl->Release(copy.process_ref);
    if (copy.flags & (CTF_COINIT_STA | CTF_COINIT_MTA))
        CoUninitialize();
    if (copy.library)
        FreeLibraryAndExitThread(copy.library, result);
    return result;
}

/* a callback runs on the new thread before this returns; a thread that cannot start runs the work here when
   CTF_INSECURE asks for it. */
BOOL WINAPI SHCreateThreadWithHandle(THREAD_START start, void *data, DWORD flags, THREAD_START callback,
                                     HANDLE *handle)
{
    if (handle)
        *handle = nullptr;
    THREADSTART info = {start, callback, data, flags, CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!info.ready)
        return FALSE;
    if (flags & CTF_THREAD_REF)
        SHGetThreadRef(&info.thread_ref);
    if ((flags & CTF_PROCESS_REF) && g_process_ref) {
        info.process_ref = g_process_ref;
        info.process_ref->lpVtbl->AddRef(info.process_ref);
    }
    if ((flags & CTF_FREELIBANDEXIT) && !(flags & CTF_NOADDREFLIB)) {
        MEMORY_BASIC_INFORMATION mbi;
        WCHAR path[MAX_PATH];
        if (VirtualQuery((void *)start, &mbi, sizeof(mbi)) &&
            GetModuleFileNameW((HMODULE)mbi.AllocationBase, path, MAX_PATH))
            info.library = LoadLibraryW(path);
    }
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0, thread_main, &info, 0, &id);
    if (thread) {
        WaitForSingleObject(info.ready, INFINITE);
        CloseHandle(info.ready);
        if (handle)
            *handle = thread;
        else
            CloseHandle(thread);
        return TRUE;
    }
    CloseHandle(info.ready);
    if (info.thread_ref)
        info.thread_ref->lpVtbl->Release(info.thread_ref);
    if (info.process_ref)
        info.process_ref->lpVtbl->Release(info.process_ref);
    if (info.library)
        FreeLibrary(info.library);
    if (flags & CTF_INSECURE) {
        if (callback)
            callback(data);
        if (start)
            start(data);
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI SHCreateThread(THREAD_START start, void *data, DWORD flags, THREAD_START callback)
{
    return SHCreateThreadWithHandle(start, data, flags, callback, nullptr);
}

void WINAPI SetProcessReference(IUnknown *unknown)
{
    g_process_ref = unknown;
}

HRESULT WINAPI GetProcessReference(IUnknown **unknown)
{
    *unknown = g_process_ref;
    if (!g_process_ref)
        return E_FAIL;
    g_process_ref->lpVtbl->AddRef(g_process_ref);
    return S_OK;
}

/* ------------------------------------------------------------------ COM helpers */

void WINAPI IUnknown_AtomicRelease(void **unknown)
{
    if (unknown && *unknown) {
        IUnknown *object = *unknown;
        *unknown = nullptr;
        object->lpVtbl->Release(object);
    }
}

void WINAPI IUnknown_Set(IUnknown **slot, IUnknown *unknown)
{
    IUnknown *old = *slot;
    if (old == unknown)
        return;
    *slot = nullptr;
    if (old)
        old->lpVtbl->Release(old);
    if (unknown) {
        unknown->lpVtbl->AddRef(unknown);
        *slot = unknown;
    }
}

typedef struct IObjectWithSiteVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG(STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT(STDMETHODCALLTYPE *SetSite)(IUnknown *, IUnknown *);
    HRESULT(STDMETHODCALLTYPE *GetSite)(IUnknown *, REFIID, void **);
} IObjectWithSiteVtbl;

typedef struct IServiceProviderVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG(STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT(STDMETHODCALLTYPE *QueryService)(IUnknown *, REFGUID, REFIID, void **);
} IServiceProviderVtbl;

#define VTBL(object, type) ((const type *)((IUnknown *)(object))->lpVtbl)

HRESULT WINAPI IUnknown_GetSite(IUnknown *unknown, REFIID riid, void **site)
{
    *site = nullptr;
    if (!unknown)
        return E_FAIL;
    IUnknown *object;
    HRESULT hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IObjectWithSite, (void **)&object);
    if (SUCCEEDED(hr)) {
        hr = VTBL(object, IObjectWithSiteVtbl)->GetSite(object, riid, site);
        object->lpVtbl->Release(object);
    }
    return hr;
}

/* IObjectWithSite, or an internet security manager's site. */
HRESULT WINAPI IUnknown_SetSite(IUnknown *unknown, IUnknown *site)
{
    if (!unknown)
        return E_FAIL;
    IUnknown *object;
    HRESULT hr;
    if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, &IID_IObjectWithSite, (void **)&object))) {
        hr = VTBL(object, IObjectWithSiteVtbl)->SetSite(object, site);
        object->lpVtbl->Release(object);
        return hr;
    }
    hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IInternetSecurityManager, (void **)&object);
    if (FAILED(hr))
        return hr;
    IUnknown *manager_site = nullptr;
    if (site)
        hr = site->lpVtbl->QueryInterface(site, &IID_IInternetSecurityMgrSite, (void **)&manager_site);
    if (SUCCEEDED(hr)) {
        hr = VTBL(object, IObjectWithSiteVtbl)->SetSite(object, manager_site);
        if (manager_site)
            manager_site->lpVtbl->Release(manager_site);
    }
    object->lpVtbl->Release(object);
    return hr;
}

/* an object that is no service provider fails with E_NOTIMPL. */
HRESULT WINAPI IUnknown_QueryService(IUnknown *unknown, REFGUID service, REFIID riid, void **object)
{
    *object = nullptr;
    IUnknown *provider;
    if (!unknown || FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_IServiceProvider, (void **)&provider)))
        return E_NOTIMPL;
    HRESULT hr = VTBL(provider, IServiceProviderVtbl)->QueryService(provider, service, riid, object);
    provider->lpVtbl->Release(provider);
    return hr;
}

/* ------------------------------------------------------------------ IsOS */

/* a workstation of the nt line, newer than every version the OS_ constants name. */
BOOL WINAPI IsOS(DWORD os)
{
    switch (os) {
    case 1:  /* OS_NT */
    case 3:  /* OS_NT4ORGREATER */
    case 7:  /* OS_WIN2000ORGREATER */
    case 18: /* OS_XPORGREATER */
    case 20: /* OS_PROFESSIONAL */
    case 26: /* OS_FASTUSERSWITCHING */
        return TRUE;
    case 30: { /* OS_WOW6432 */
        BOOL wow64 = FALSE;
        return IsWow64Process(GetCurrentProcess(), &wow64) && wow64;
    }
    }
    return FALSE;
}

BOOL WINAPI IsProcessInIsolatedContainer(BOOL *isolated)
{
    if (isolated)
        *isolated = FALSE;
    return TRUE;
}

HRESULT WINAPI IsProcessInWDAGContainer(void *reserved, BOOL *isolated)
{
    if (!isolated)
        return E_INVALIDARG;
    *isolated = FALSE;
    return S_OK;
}

/* ------------------------------------------------------------------ feature staging */

/* no feature is staged, every one keeps its default. */
UINT WINAPI GetFeatureEnabledState(UINT feature, UINT change_time)
{
    return 0;
}

UINT WINAPI GetFeatureVariant(UINT feature, UINT change_time, UINT *payload, BOOL *has_notification)
{
    if (payload)
        *payload = 0;
    if (has_notification)
        *has_notification = FALSE;
    return 0;
}

void WINAPI SubscribeFeatureStateChangeNotification(void **subscription, void *callback, void *context)
{
    if (subscription)
        *subscription = nullptr;
}
