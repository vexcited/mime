/*
 * The page of a web browser control: the WebView2 runtime, when the machine has it, in a child window of the control.
 * ieframe.dll of Windows renders with the engine of Internet Explorer; here the runtime that programs like the
 * SolidWorks eDrawings ship their own pages with does the work. A machine with no runtime shows a blank page.
 */
#include "ieframe_private.h"

typedef LONG64 WV_TOKEN;

enum { HOST_NONE, HOST_ENVIRONMENT, HOST_CONTROLLER, HOST_READY, HOST_FAILED };

struct HOST {
    BROWSER *Browser;
    HWND Parent;
    LONG Refs;
    int State;
    BOOL Dead;
    int Width, Height;
    void *Environment;
    void *Controller;
    void *View;
    WV_TOKEN Tokens[4];
    LPWSTR PendingUrl;
    LPWSTR PendingHtml;
};

static void *slot(void *object, int index)
{
    return (*(void ***)object)[index];
}

#define WV_CALL(type, object, index, ...) ((type)slot(object, index))(object, ##__VA_ARGS__)

typedef HRESULT(STDMETHODCALLTYPE *WV_FN_PTR)(void *, void *);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_STR)(void *, LPCWSTR);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_OUTSTR)(void *, LPWSTR *);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_OUTBOOL)(void *, BOOL *);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_BOOL)(void *, BOOL);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_RECT)(void *, RECT);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_INT)(void *, int);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_HWND_PTR)(void *, HWND, void *);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_EVENT)(void *, void *, WV_TOKEN *);
typedef HRESULT(STDMETHODCALLTYPE *WV_FN_VOID)(void *);
typedef ULONG(STDMETHODCALLTYPE *WV_FN_REF)(void *);

/* the slots of ICoreWebView2, its controller and its environment. */
enum {
    ENV_CREATE_CONTROLLER = 3,
    CTL_PUT_IS_VISIBLE = 4,
    CTL_PUT_BOUNDS = 6,
    CTL_MOVE_FOCUS = 12,
    CTL_CLOSE = 24,
    CTL_GET_CORE = 25,
    VIEW_NAVIGATE = 5,
    VIEW_NAVIGATE_TO_STRING = 6,
    VIEW_ADD_NAVIGATION_STARTING = 7,
    VIEW_ADD_NAVIGATION_COMPLETED = 15,
    VIEW_RELOAD = 31,
    VIEW_GO_BACK = 40,
    VIEW_GO_FORWARD = 41,
    VIEW_STOP = 43,
    VIEW_ADD_NEW_WINDOW = 44,
    VIEW_ADD_TITLE_CHANGED = 46,
    VIEW_GET_TITLE = 48,
};

IF_GUID(IID_EnvironmentHandler, 0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d);
IF_GUID(IID_ControllerHandler, 0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c);
IF_GUID(IID_CompletedHandler, 0xd33a35bf, 0x1c49, 0x4f98, 0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c);
IF_GUID(IID_StartingHandler, 0x9adbe429, 0xf36d, 0x432b, 0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3);
IF_GUID(IID_TitleHandler, 0xf5f2b923, 0x953e, 0x4042, 0x9f, 0x95, 0xf3, 0xa1, 0x18, 0xaf, 0x1f, 0xd4);
IF_GUID(IID_NewWindowHandler, 0xd4c185fe, 0xc81c, 0x4989, 0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51);
IF_GUID(IID_EnvironmentOptions, 0x2fde08a8, 0x1e9a, 0x4766, 0x8c, 0x05, 0x95, 0xa9, 0xce, 0xb9, 0xd1, 0xc5);

/* ------------------------------------------------------------------ the host's life */

static void host_release(HOST *host)
{
    if (InterlockedDecrement(&host->Refs) == 0) {
        HeapFree(GetProcessHeap(), 0, host->PendingUrl);
        HeapFree(GetProcessHeap(), 0, host->PendingHtml);
        HeapFree(GetProcessHeap(), 0, host);
    }
}

static LPWSTR copy_string(LPCWSTR text)
{
    SIZE_T size = ((SIZE_T)lstrlenW(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = HeapAlloc(GetProcessHeap(), 0, size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static void apply_bounds(HOST *host)
{
    if (host->State == HOST_READY) {
        RECT bounds = {0, 0, host->Width, host->Height};
        WV_CALL(WV_FN_RECT, host->Controller, CTL_PUT_BOUNDS, bounds);
    }
}

/* ------------------------------------------------------------------ the handlers the runtime calls */

typedef struct HANDLER HANDLER;
typedef struct HANDLER_VTBL_RESULT {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(HANDLER *This, REFIID iid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(HANDLER *This);
    ULONG(STDMETHODCALLTYPE *Release)(HANDLER *This);
    HRESULT(STDMETHODCALLTYPE *Invoke)(HANDLER *This, HRESULT result, void *object);
} HANDLER_VTBL_RESULT;
typedef struct HANDLER_VTBL_EVENT {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(HANDLER *This, REFIID iid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(HANDLER *This);
    ULONG(STDMETHODCALLTYPE *Release)(HANDLER *This);
    HRESULT(STDMETHODCALLTYPE *Invoke)(HANDLER *This, void *sender, void *args);
} HANDLER_VTBL_EVENT;

struct HANDLER {
    const void *Vtbl;
    LONG Refs;
    HOST *Host;
    const GUID *Iid;
};

static HRESULT STDMETHODCALLTYPE handler_query(HANDLER *self, REFIID iid, void **object)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, self->Iid)) {
        InterlockedIncrement(&self->Refs);
        *object = self;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE handler_add_ref(HANDLER *self)
{
    return (ULONG)InterlockedIncrement(&self->Refs);
}

static ULONG STDMETHODCALLTYPE handler_release(HANDLER *self)
{
    LONG refs = InterlockedDecrement(&self->Refs);
    if (refs == 0) {
        host_release(self->Host);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)refs;
}

static HANDLER *handler_new(HOST *host, const void *vtbl, const GUID *iid)
{
    HANDLER *handler = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*handler));
    if (!handler)
        return nullptr;
    handler->Vtbl = vtbl;
    handler->Refs = 1;
    handler->Host = host;
    handler->Iid = iid;
    InterlockedIncrement(&host->Refs);
    return handler;
}

static void register_events(HOST *host);

static HRESULT STDMETHODCALLTYPE controller_done(HANDLER *self, HRESULT result, void *controller)
{
    HOST *host = self->Host;
    if (host->Dead)
        return S_OK;
    if (FAILED(result) || !controller) {
        host->State = HOST_FAILED;
        return S_OK;
    }
    WV_CALL(WV_FN_REF, controller, 1);
    host->Controller = controller;
    void *view = nullptr;
    WV_CALL(WV_FN_PTR, controller, CTL_GET_CORE, &view);
    if (!view) {
        host->State = HOST_FAILED;
        return S_OK;
    }
    WV_CALL(WV_FN_REF, view, 1);
    host->View = view;
    host->State = HOST_READY;
    WV_CALL(WV_FN_BOOL, controller, CTL_PUT_IS_VISIBLE, TRUE);
    apply_bounds(host);
    register_events(host);
    BrowserHostReady(host->Browser);
    if (host->PendingHtml) {
        LPWSTR html = host->PendingHtml;
        host->PendingHtml = nullptr;
        WV_CALL(WV_FN_STR, view, VIEW_NAVIGATE_TO_STRING, html);
        HeapFree(GetProcessHeap(), 0, html);
    } else if (host->PendingUrl) {
        LPWSTR url = host->PendingUrl;
        host->PendingUrl = nullptr;
        WV_CALL(WV_FN_STR, view, VIEW_NAVIGATE, url);
        HeapFree(GetProcessHeap(), 0, url);
    }
    return S_OK;
}

static const HANDLER_VTBL_RESULT g_controller_vtbl = {handler_query, handler_add_ref, handler_release, controller_done};

static void create_controller(HOST *host)
{
    HANDLER *handler = handler_new(host, &g_controller_vtbl, &IID_ControllerHandler);
    if (!handler) {
        host->State = HOST_FAILED;
        return;
    }
    host->State = HOST_CONTROLLER;
    typedef HRESULT(STDMETHODCALLTYPE * F)(void *, HWND, HANDLER *);
    HRESULT hr = ((F)slot(host->Environment, ENV_CREATE_CONTROLLER))(host->Environment, host->Parent, handler);
    if (FAILED(hr))
        host->State = HOST_FAILED;
    handler_release(handler);
}

static HRESULT STDMETHODCALLTYPE environment_done(HANDLER *self, HRESULT result, void *environment)
{
    HOST *host = self->Host;
    if (host->Dead)
        return S_OK;
    if (FAILED(result) || !environment) {
        host->State = HOST_FAILED;
        return S_OK;
    }
    WV_CALL(WV_FN_REF, environment, 1);
    host->Environment = environment;
    create_controller(host);
    return S_OK;
}

static const HANDLER_VTBL_RESULT g_environment_vtbl = {handler_query, handler_add_ref, handler_release,
                                                       environment_done};

static HRESULT STDMETHODCALLTYPE starting_event(HANDLER *self, void *sender, void *args)
{
    HOST *host = self->Host;
    LPWSTR url = nullptr;
    BOOL cancel = FALSE;
    if (host->Dead)
        return S_OK;
    WV_CALL(WV_FN_OUTSTR, args, 3, &url);
    BrowserNavigationStarting(host->Browser, url ? url : L"", &cancel);
    if (cancel)
        WV_CALL(WV_FN_BOOL, args, 8, TRUE);
    if (url)
        CoTaskMemFree(url);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE completed_event(HANDLER *self, void *sender, void *args)
{
    HOST *host = self->Host;
    LPWSTR url = nullptr;
    BOOL success = FALSE;
    if (host->Dead)
        return S_OK;
    WV_CALL(WV_FN_OUTBOOL, args, 3, &success);
    WV_CALL(WV_FN_OUTSTR, sender, 4, &url);
    BrowserNavigationCompleted(host->Browser, url ? url : L"about:blank", success);
    if (url)
        CoTaskMemFree(url);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE title_event(HANDLER *self, void *sender, void *args)
{
    HOST *host = self->Host;
    LPWSTR title = nullptr;
    if (host->Dead)
        return S_OK;
    WV_CALL(WV_FN_OUTSTR, sender, VIEW_GET_TITLE, &title);
    BrowserTitleChanged(host->Browser, title ? title : L"");
    if (title)
        CoTaskMemFree(title);
    return S_OK;
}

/* a page that wants a window of its own does not get one, the program is told as with Internet Explorer. */
static HRESULT STDMETHODCALLTYPE new_window_event(HANDLER *self, void *sender, void *args)
{
    WV_CALL(WV_FN_BOOL, args, 6, TRUE);
    return S_OK;
}

static const HANDLER_VTBL_EVENT g_starting_vtbl = {handler_query, handler_add_ref, handler_release, starting_event};
static const HANDLER_VTBL_EVENT g_completed_vtbl = {handler_query, handler_add_ref, handler_release, completed_event};
static const HANDLER_VTBL_EVENT g_title_vtbl = {handler_query, handler_add_ref, handler_release, title_event};
static const HANDLER_VTBL_EVENT g_new_window_vtbl = {handler_query, handler_add_ref, handler_release,
                                                     new_window_event};

static void add_event(HOST *host, int slot_index, const HANDLER_VTBL_EVENT *vtbl, const GUID *iid, int token)
{
    HANDLER *handler = handler_new(host, vtbl, iid);
    if (!handler)
        return;
    WV_CALL(WV_FN_EVENT, host->View, slot_index, handler, &host->Tokens[token]);
    handler_release(handler);
}

static void register_events(HOST *host)
{
    add_event(host, VIEW_ADD_NAVIGATION_STARTING, &g_starting_vtbl, &IID_StartingHandler, 0);
    add_event(host, VIEW_ADD_NAVIGATION_COMPLETED, &g_completed_vtbl, &IID_CompletedHandler, 1);
    add_event(host, VIEW_ADD_TITLE_CHANGED, &g_title_vtbl, &IID_TitleHandler, 2);
    add_event(host, VIEW_ADD_NEW_WINDOW, &g_new_window_vtbl, &IID_NewWindowHandler, 3);
}

/* ------------------------------------------------------------------ the options of the environment */

typedef struct OPTIONS OPTIONS;
typedef struct OPTIONS_VTBL {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(OPTIONS *This, REFIID iid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(OPTIONS *This);
    ULONG(STDMETHODCALLTYPE *Release)(OPTIONS *This);
    HRESULT(STDMETHODCALLTYPE *get_AdditionalBrowserArguments)(OPTIONS *This, LPWSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_AdditionalBrowserArguments)(OPTIONS *This, LPCWSTR value);
    HRESULT(STDMETHODCALLTYPE *get_Language)(OPTIONS *This, LPWSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_Language)(OPTIONS *This, LPCWSTR value);
    HRESULT(STDMETHODCALLTYPE *get_TargetCompatibleBrowserVersion)(OPTIONS *This, LPWSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_TargetCompatibleBrowserVersion)(OPTIONS *This, LPCWSTR value);
    HRESULT(STDMETHODCALLTYPE *get_AllowSingleSignOnUsingOSPrimaryAccount)(OPTIONS *This, BOOL *value);
    HRESULT(STDMETHODCALLTYPE *put_AllowSingleSignOnUsingOSPrimaryAccount)(OPTIONS *This, BOOL value);
} OPTIONS_VTBL;
struct OPTIONS {
    const OPTIONS_VTBL *Vtbl;
    LONG Refs;
};

static HRESULT STDMETHODCALLTYPE options_query(OPTIONS *self, REFIID iid, void **object)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_EnvironmentOptions)) {
        InterlockedIncrement(&self->Refs);
        *object = self;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE options_add_ref(OPTIONS *self)
{
    return (ULONG)InterlockedIncrement(&self->Refs);
}

static ULONG STDMETHODCALLTYPE options_release(OPTIONS *self)
{
    LONG refs = InterlockedDecrement(&self->Refs);
    if (refs == 0)
        HeapFree(GetProcessHeap(), 0, self);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE options_get_none(OPTIONS *self, LPWSTR *value)
{
    *value = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE options_put_ignored(OPTIONS *self, LPCWSTR value)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE options_get_version(OPTIONS *self, LPWSTR *value)
{
    static const WCHAR version[] = L"86.0.616.0";
    *value = CoTaskMemAlloc(sizeof(version));
    if (!*value)
        return E_OUTOFMEMORY;
    memcpy(*value, version, sizeof(version));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE options_get_sso(OPTIONS *self, BOOL *value)
{
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE options_put_sso(OPTIONS *self, BOOL value)
{
    return S_OK;
}

static const OPTIONS_VTBL g_options_vtbl = {options_query,       options_add_ref,    options_release,
                                            options_get_none,    options_put_ignored, options_get_none,
                                            options_put_ignored, options_get_version, options_put_ignored,
                                            options_get_sso,     options_put_sso};

/* ------------------------------------------------------------------ the runtime */

/* the folder of the runtime, where its updater writes it. */
static BOOL runtime_folder(WCHAR *folder, DWORD chars)
{
    static const LPCWSTR keys[] = {
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
    };
    static const HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    for (int r = 0; r < 2; r++)
        for (int k = 0; k < 2; k++) {
            HKEY key;
            if (RegOpenKeyExW(roots[r], keys[k], 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS)
                continue;
            DWORD size = chars * sizeof(WCHAR), type;
            LONG status = RegQueryValueExW(key, L"EBWebView", nullptr, &type, (BYTE *)folder, &size);
            RegCloseKey(key);
            if (status == ERROR_SUCCESS && type == REG_SZ && folder[0])
                return TRUE;
        }
    return FALSE;
}

typedef HRESULT(STDAPICALLTYPE *CREATE_ENVIRONMENT)(BOOL, int, LPCWSTR, void *, void *);

static BOOL create_environment(HOST *host)
{
    WCHAR path[MAX_PATH + 64];
    if (!runtime_folder(path, MAX_PATH))
        return FALSE;
#ifdef _WIN64
    lstrcatW(path, L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll");
#else
    lstrcatW(path, L"\\EBWebView\\x86\\EmbeddedBrowserWebView.dll");
#endif
    HMODULE runtime = LoadLibraryW(path);
    if (!runtime)
        return FALSE;
    CREATE_ENVIRONMENT create = (CREATE_ENVIRONMENT)(void *)GetProcAddress(runtime, "CreateWebViewEnvironmentWithOptionsInternal");
    if (!create)
        return FALSE;

    /* a folder for the pages' cache, one for each program. */
    WCHAR program[MAX_PATH], data[MAX_PATH + 64];
    DWORD n = GetModuleFileNameW(nullptr, program, MAX_PATH);
    LPCWSTR name = program;
    for (DWORD i = 0; i < n; i++)
        if (program[i] == L'\\')
            name = program + i + 1;
    DWORD root = GetEnvironmentVariableW(L"LOCALAPPDATA", data, MAX_PATH);
    if (!root || root >= MAX_PATH)
        return FALSE;
    lstrcatW(data, L"\\Microsoft");
    CreateDirectoryW(data, nullptr);
    lstrcatW(data, L"\\IEFrame");
    CreateDirectoryW(data, nullptr);
    lstrcatW(data, L"\\");
    lstrcatW(data, name);
    CreateDirectoryW(data, nullptr);

    OPTIONS *options = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*options));
    HANDLER *handler = handler_new(host, &g_environment_vtbl, &IID_EnvironmentHandler);
    if (!options || !handler) {
        HeapFree(GetProcessHeap(), 0, options);
        if (handler)
            handler_release(handler);
        return FALSE;
    }
    options->Vtbl = &g_options_vtbl;
    options->Refs = 1;
    host->State = HOST_ENVIRONMENT;
    HRESULT hr = create(TRUE, 0, data, options, handler);
    options_release(options);
    handler_release(handler);
    if (FAILED(hr)) {
        host->State = HOST_FAILED;
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ what the browser asks of the host */

HOST *HostCreate(BROWSER *browser, HWND parent)
{
    HOST *host = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*host));
    if (!host)
        return nullptr;
    host->Browser = browser;
    host->Parent = parent;
    host->Refs = 1;
    RECT client;
    GetClientRect(parent, &client);
    host->Width = client.right;
    host->Height = client.bottom;
    if (!create_environment(host)) {
        host->Dead = TRUE;
        host_release(host);
        return nullptr;
    }
    return host;
}

void HostDestroy(HOST *host)
{
    host->Dead = TRUE;
    if (host->View) {
        WV_CALL(WV_FN_REF, host->View, 2);
        host->View = nullptr;
    }
    if (host->Controller) {
        WV_CALL(WV_FN_VOID, host->Controller, CTL_CLOSE);
        WV_CALL(WV_FN_REF, host->Controller, 2);
        host->Controller = nullptr;
    }
    if (host->Environment) {
        WV_CALL(WV_FN_REF, host->Environment, 2);
        host->Environment = nullptr;
    }
    host_release(host);
}

void HostResize(HOST *host, int width, int height)
{
    host->Width = width;
    host->Height = height;
    apply_bounds(host);
}

static BOOL queue_or_run(HOST *host, LPCWSTR text, int slot_index, LPWSTR *pending)
{
    if (host->State == HOST_READY) {
        WV_CALL(WV_FN_STR, host->View, slot_index, text);
        return TRUE;
    }
    if (host->State == HOST_FAILED)
        return FALSE;
    HeapFree(GetProcessHeap(), 0, *pending);
    *pending = copy_string(text);
    return *pending != nullptr;
}

BOOL HostNavigate(HOST *host, LPCWSTR url)
{
    HeapFree(GetProcessHeap(), 0, host->PendingHtml);
    host->PendingHtml = nullptr;
    return queue_or_run(host, url, VIEW_NAVIGATE, &host->PendingUrl);
}

BOOL HostNavigateHtml(HOST *host, LPCWSTR html)
{
    HeapFree(GetProcessHeap(), 0, host->PendingUrl);
    host->PendingUrl = nullptr;
    return queue_or_run(host, html, VIEW_NAVIGATE_TO_STRING, &host->PendingHtml);
}

void HostCommand(HOST *host, int command)
{
    if (host->State != HOST_READY)
        return;
    WV_CALL(WV_FN_VOID, host->View, command);
}

void HostSetFocus(HOST *host)
{
    if (host->State == HOST_READY)
        WV_CALL(WV_FN_INT, host->Controller, CTL_MOVE_FOCUS, 0);
}
