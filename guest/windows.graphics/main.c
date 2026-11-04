#include "wg_private.h"
#include <rtl.h>

#define ERROR_WINDOW_OF_OTHER_THREAD_ 1408L
#define ERROR_INVALID_MONITOR_HANDLE_ 1461L
#define ERROR_INVALID_WINDOW_STYLE_ 2002L

int _fltused = 0x9875;

HINSTANCE WgModule;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        WgModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the factory of DisplayInformation */

/* static and agile, without weak references, as WRL makes it. */
typedef struct FACTORY {
    WRT_AGILE Object;
    WRT_EVENT ContentsInvalidated;
} FACTORY;

static FACTORY g_factory;

WRT_AGILE_THUNKS(IActivationFactory)
WRT_AGILE_THUNKS(IDisplayInformationStatics)
WRT_AGILE_THUNKS(IDisplayInformationStaticsInterop)

static HRESULT STDMETHODCALLTYPE factory_activate(IActivationFactory *This, IInspectable **instance)
{
    if (instance)
        *instance = nullptr;
    return E_NOTIMPL;
}

/* the view of a thread is its CoreWindow, which desktop threads do not have. */
static HRESULT STDMETHODCALLTYPE statics_get_for_current_view(IDisplayInformationStatics *This,
                                                              IDisplayInformation **current)
{
    if (!current)
        return E_POINTER;
    *current = nullptr;
    return E_NOTFOUND_;
}

static HRESULT STDMETHODCALLTYPE statics_get_auto_rotation(IDisplayInformationStatics *This,
                                                           DisplayOrientations *value)
{
    if (!value)
        return E_POINTER;
    DWORD preference = 0;
    *value = DisplayOrientations_None;
    if (!GetDisplayAutoRotationPreferences(&preference))
        return HRESULT_FROM_WIN32(GetLastError());
    *value = (DisplayOrientations)(preference & 0xF);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE statics_put_auto_rotation(IDisplayInformationStatics *This,
                                                           DisplayOrientations value)
{
    if (!SetDisplayAutoRotationPreferences((DWORD)value & 0xF))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/* the event follows the CoreWindow of the thread, and without one it cannot be listened to. */
static HRESULT STDMETHODCALLTYPE statics_add_contents_invalidated(IDisplayInformationStatics *This,
                                                                  IWrtEventHandler *handler,
                                                                  EventRegistrationToken *token)
{
    if (!token)
        return E_POINTER;
    token->value = 0;
    return handler ? E_FAIL : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE statics_remove_contents_invalidated(IDisplayInformationStatics *This,
                                                                     EventRegistrationToken token)
{
    return WrtEventRemove(&g_factory.ContentsInvalidated, token);
}

/* the window must be a top level one of the calling thread. */
static HRESULT STDMETHODCALLTYPE interop_get_for_window(IDisplayInformationStaticsInterop *This, HWND window,
                                                        REFIID riid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsWindow(window))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    DWORD thread = GetWindowThreadProcessId(window, nullptr);
    if (!thread)
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (thread != GetCurrentThreadId())
        return HRESULT_FROM_WIN32(ERROR_WINDOW_OF_OTHER_THREAD_);
    if (GetAncestor(window, GA_ROOT) != window)
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_STYLE_);
    return WgDisplayCreate(window, nullptr, riid, result);
}

static HRESULT STDMETHODCALLTYPE interop_get_for_monitor(IDisplayInformationStaticsInterop *This, HMONITOR monitor,
                                                         REFIID riid, void **result)
{
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!GetMonitorInfoW(monitor, &info))
        return HRESULT_FROM_WIN32(ERROR_INVALID_MONITOR_HANDLE_);
    return WgDisplayCreate(nullptr, monitor, riid, result);
}

static const IActivationFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IActivationFactory), factory_activate};
static const IDisplayInformationStaticsVtbl statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDisplayInformationStatics), statics_get_for_current_view, statics_get_auto_rotation,
    statics_put_auto_rotation, statics_add_contents_invalidated, statics_remove_contents_invalidated};
static const IDisplayInformationStaticsInteropVtbl interop_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDisplayInformationStaticsInterop), interop_get_for_window, interop_get_for_monitor};

static void factory_destroy(WRT_OBJECT *object)
{
}

static const WRT_CLASS g_factory_class = {
    L"Windows.Graphics.Display.DisplayInformation",
    3,
    {&IID_IActivationFactory, &IID_IDisplayInformationStatics, &IID_IDisplayInformationStaticsInterop},
    0,
    factory_destroy};

static INIT_ONCE g_factory_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_factory(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[3] = {&factory_vtbl, &statics_vtbl, &interop_vtbl};
    WrtAgileInit(&g_factory.Object, &g_factory_class, vtables);
    return TRUE;
}

static BOOL name_is(HSTRING string, LPCWSTR name)
{
    UINT32 length;
    LPCWSTR text = WindowsGetStringRawBuffer(string, &length);
    return length == wcslen(name) && !memcmp(text, name, length * sizeof(WCHAR));
}

HRESULT WINAPI DllGetActivationFactory(HSTRING class_id, IActivationFactory **factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    if (!name_is(class_id, L"Windows.Graphics.Display.DisplayInformation"))
        return CLASS_E_CLASSNOTAVAILABLE;
    InitOnceExecuteOnce(&g_factory_once, init_factory, nullptr, nullptr);
    WrtAgileAddRef(&g_factory.Object.Base.Ifaces[0]);
    *factory = (IActivationFactory *)WrtAgileIface(&g_factory.Object);
    return S_OK;
}

/* the watcher thread and its hooks run code of the library. */
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
