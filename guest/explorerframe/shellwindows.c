/*
 * the list of the shell's windows, CLSID_ShellWindows, which programs ask for to find a shell that runs: a program that
 * wants to start another with the rights of the user, not its own, finds the desktop in this list and has the shell
 * start it, see FindWindowSW. The class is served by the local server shell32.dll,SHCreateLocalServerRunDll runs, whose
 * process has the limited token of the user.
 *
 * Mime has no explorer, so the list brings the desktop itself: a browser object for it, which answers IServiceProvider
 * and IShellBrowser, whose active view gives the automation view of the desktop. The only windows that are in the list
 * are the ones programs register.
 */
#include "explorerframe_private.h"
#include <mime/win32/shldisp.h>

extern HINSTANCE g_explorerframe;

#define SW_E_POINTER ((HRESULT)0x80004003L)
#define SVC_E_UNKNOWNSERVICE ((HRESULT)0x80040200L)
#define E_PENDING_ ((HRESULT)0x8000000AL)

static const BYTE g_desktop_idl[2];

/* ------------------------------------------------------------------ type library */

static SRWLOCK g_library_lock = SRWLOCK_INIT;
static ITypeLib *g_library;

static HRESULT window_list_info(ITypeInfo **info)
{
    *info = nullptr;
    AcquireSRWLockExclusive(&g_library_lock);
    HRESULT hr = S_OK;
    if (!g_library) {
        WCHAR path[MAX_PATH];
        if (!GetModuleFileNameW(g_explorerframe, path, MAX_PATH))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            hr = LoadTypeLibEx(path, REGKIND_NONE, &g_library);
    }
    if (SUCCEEDED(hr))
        hr = g_library->lpVtbl->GetTypeInfoOfGuid(g_library, &IID_IShellWindows, info);
    ReleaseSRWLockExclusive(&g_library_lock);
    return hr;
}

/* ------------------------------------------------------------------ the desktop */

/*
 * one object is the browser of the desktop, the thing a program gets for the window it finds: automation that answers
 * nothing of its own, the service provider that gives the browser for the top level service, and the browser. The view
 * it shows is another object.
 */
typedef struct DESKTOP {
    IDispatch Dispatch;
    IServiceProvider Service;
    IShellBrowser Browser;
    IShellView View;
    LONG Refs;
    HWND Window;
} DESKTOP;

static DESKTOP *desktop_from_dispatch(IDispatch *This)
{
    return (DESKTOP *)((PUCHAR)This - offsetof(DESKTOP, Dispatch));
}

static DESKTOP *desktop_from_service(IServiceProvider *This)
{
    return (DESKTOP *)((PUCHAR)This - offsetof(DESKTOP, Service));
}

static DESKTOP *desktop_from_browser(IShellBrowser *This)
{
    return (DESKTOP *)((PUCHAR)This - offsetof(DESKTOP, Browser));
}

static DESKTOP *desktop_from_view(IShellView *This)
{
    return (DESKTOP *)((PUCHAR)This - offsetof(DESKTOP, View));
}

static HRESULT desktop_query(DESKTOP *desktop, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch))
        *object = &desktop->Dispatch;
    else if (IsEqualIID(riid, &IID_IServiceProvider))
        *object = &desktop->Service;
    else if (IsEqualIID(riid, &IID_IShellBrowser) || IsEqualIID(riid, &IID_IOleWindow))
        *object = &desktop->Browser;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&desktop->Refs);
    return S_OK;
}

static ULONG desktop_release(DESKTOP *desktop)
{
    LONG refs = InterlockedDecrement(&desktop->Refs);
    if (!refs) {
        if (desktop->Window)
            DestroyWindow(desktop->Window);
        HeapFree(GetProcessHeap(), 0, desktop);
    }
    return (ULONG)refs;
}

/* IDispatch: the browser of the desktop has no members of its own. */
static HRESULT STDMETHODCALLTYPE dd_query_interface(IDispatch *This, REFIID riid, void **object)
{
    return desktop_query(desktop_from_dispatch(This), riid, object);
}

static ULONG STDMETHODCALLTYPE dd_add_ref(IDispatch *This)
{
    return (ULONG)InterlockedIncrement(&desktop_from_dispatch(This)->Refs);
}

static ULONG STDMETHODCALLTYPE dd_release(IDispatch *This)
{
    return desktop_release(desktop_from_dispatch(This));
}

static HRESULT STDMETHODCALLTYPE dd_get_type_info_count(IDispatch *This, UINT *count)
{
    if (!count)
        return SW_E_POINTER;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dd_get_type_info(IDispatch *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dd_get_ids_of_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                                     DISPID *ids)
{
    for (UINT i = 0; i < count && ids; i++)
        ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

static HRESULT STDMETHODCALLTYPE dd_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument)
{
    return DISP_E_MEMBERNOTFOUND;
}

static const IDispatchVtbl g_dispatch_vtbl = {
    dd_query_interface, dd_add_ref, dd_release, dd_get_type_info_count, dd_get_type_info, dd_get_ids_of_names, dd_invoke,
};

/* IServiceProvider: the top level browser of the desktop is the browser object. */
static HRESULT STDMETHODCALLTYPE ds_query_interface(IServiceProvider *This, REFIID riid, void **object)
{
    return desktop_query(desktop_from_service(This), riid, object);
}

static ULONG STDMETHODCALLTYPE ds_add_ref(IServiceProvider *This)
{
    return (ULONG)InterlockedIncrement(&desktop_from_service(This)->Refs);
}

static ULONG STDMETHODCALLTYPE ds_release(IServiceProvider *This)
{
    return desktop_release(desktop_from_service(This));
}

static HRESULT STDMETHODCALLTYPE ds_query_service(IServiceProvider *This, REFGUID service, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(service, &SID_STopLevelBrowser))
        return SVC_E_UNKNOWNSERVICE;
    return desktop_query(desktop_from_service(This), riid, object);
}

static const IServiceProviderVtbl g_service_vtbl = {ds_query_interface, ds_add_ref, ds_release, ds_query_service};

/* IShellBrowser: the window and the one view. */
static HRESULT STDMETHODCALLTYPE db_query_interface(IShellBrowser *This, REFIID riid, void **object)
{
    return desktop_query(desktop_from_browser(This), riid, object);
}

static ULONG STDMETHODCALLTYPE db_add_ref(IShellBrowser *This)
{
    return (ULONG)InterlockedIncrement(&desktop_from_browser(This)->Refs);
}

static ULONG STDMETHODCALLTYPE db_release(IShellBrowser *This)
{
    return desktop_release(desktop_from_browser(This));
}

static HRESULT STDMETHODCALLTYPE db_get_window(IShellBrowser *This, HWND *window)
{
    if (!window)
        return SW_E_POINTER;
    *window = desktop_from_browser(This)->Window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE db_context_sensitive_help(IShellBrowser *This, BOOL enter)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_insert_menus(IShellBrowser *This, HMENU shared, LPOLEMENUGROUPWIDTHS widths)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_set_menu(IShellBrowser *This, HMENU shared, HOLEMENU descriptor, HWND object)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_remove_menus(IShellBrowser *This, HMENU shared)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_succeed_text(IShellBrowser *This, LPCOLESTR text)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE db_enable_modeless(IShellBrowser *This, BOOL enable)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE db_translate_accelerator(IShellBrowser *This, MSG *message, WORD id)
{
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE db_browse_object(IShellBrowser *This, PCUIDLIST_RELATIVE pidl, UINT flags)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_get_view_state_stream(IShellBrowser *This, DWORD mode, IStream **stream)
{
    if (stream)
        *stream = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_get_control_window(IShellBrowser *This, UINT id, HWND *window)
{
    if (window)
        *window = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_send_control_msg(IShellBrowser *This, UINT id, UINT message, WPARAM wparam,
                                                     LPARAM lparam, LRESULT *result)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE db_query_active_shell_view(IShellBrowser *This, IShellView **view)
{
    if (!view)
        return SW_E_POINTER;
    DESKTOP *desktop = desktop_from_browser(This);
    *view = &desktop->View;
    InterlockedIncrement(&desktop->Refs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE db_on_view_window_active(IShellBrowser *This, IShellView *view)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE db_set_toolbar_items(IShellBrowser *This, void *buttons, UINT count, UINT flags)
{
    return S_OK;
}

static const IShellBrowserVtbl g_browser_vtbl = {
    db_query_interface,
    db_add_ref,
    db_release,
    db_get_window,
    db_context_sensitive_help,
    db_insert_menus,
    db_set_menu,
    db_remove_menus,
    db_succeed_text,
    db_enable_modeless,
    db_translate_accelerator,
    db_browse_object,
    db_get_view_state_stream,
    db_get_control_window,
    db_send_control_msg,
    db_query_active_shell_view,
    db_on_view_window_active,
    db_set_toolbar_items,
};

/* IShellView: the desktop has the background, which scripts get as the view of a folder. */
static HRESULT STDMETHODCALLTYPE dv_query_interface(IShellView *This, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IShellView) && !IsEqualIID(riid, &IID_IOleWindow))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&desktop_from_view(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE dv_add_ref(IShellView *This)
{
    return (ULONG)InterlockedIncrement(&desktop_from_view(This)->Refs);
}

static ULONG STDMETHODCALLTYPE dv_release(IShellView *This)
{
    return desktop_release(desktop_from_view(This));
}

static HRESULT STDMETHODCALLTYPE dv_get_window(IShellView *This, HWND *window)
{
    if (!window)
        return SW_E_POINTER;
    *window = desktop_from_view(This)->Window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dv_not_implemented_bool(IShellView *This, BOOL value)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dv_translate_accelerator(IShellView *This, MSG *message)
{
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE dv_enable_modeless(IShellView *This, BOOL enable)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dv_ui_activate(IShellView *This, UINT state)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dv_not_implemented(IShellView *This)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dv_refresh(IShellView *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dv_create_view_window(IShellView *This, IShellView *previous, const void *settings,
                                                       IShellBrowser *owner, RECT *bounds, HWND *window)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dv_get_current_info(IShellView *This, void *settings)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dv_add_property_sheet_pages(IShellView *This, DWORD reserved, void *add_page, LPARAM lparam)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dv_select_item(IShellView *This, PCUITEMID_CHILD pidl, UINT flags)
{
    return E_NOTIMPL;
}

/* the background of the view is the automation view of the folder, which shell32 makes. */
static HRESULT STDMETHODCALLTYPE dv_get_item_object(IShellView *This, UINT item, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if ((item & SVGIO_TYPE_MASK) != SVGIO_BACKGROUND)
        return E_NOINTERFACE;
    return CoCreateInstance(&CLSID_ShellFolderView, nullptr, CLSCTX_INPROC_SERVER, riid, object);
}

static const IShellViewVtbl g_view_vtbl = {
    dv_query_interface,
    dv_add_ref,
    dv_release,
    dv_get_window,
    dv_not_implemented_bool,
    dv_translate_accelerator,
    dv_enable_modeless,
    dv_ui_activate,
    dv_refresh,
    dv_create_view_window,
    dv_not_implemented,
    dv_get_current_info,
    dv_add_property_sheet_pages,
    dv_not_implemented,
    dv_select_item,
    dv_get_item_object,
};

static LRESULT CALLBACK desktop_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

/* the window of the desktop is hidden and belongs to the thread that made the list. */
static HWND create_desktop_window(void)
{
    WNDCLASSW class = {};
    class.lpfnWndProc = desktop_window_proc;
    class.hInstance = g_explorerframe;
    class.lpszClassName = L"Progman";
    RegisterClassW(&class);
    return CreateWindowExW(WS_EX_TOOLWINDOW, L"Progman", L"Program Manager", WS_POPUP, 0, 0, 0, 0, nullptr,
                           nullptr, g_explorerframe, nullptr);
}

static DESKTOP *create_desktop(void)
{
    DESKTOP *desktop = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*desktop));
    if (!desktop)
        return nullptr;
    desktop->Dispatch.lpVtbl = &g_dispatch_vtbl;
    desktop->Service.lpVtbl = &g_service_vtbl;
    desktop->Browser.lpVtbl = &g_browser_vtbl;
    desktop->View.lpVtbl = &g_view_vtbl;
    desktop->Refs = 1;
    desktop->Window = create_desktop_window();
    return desktop;
}

/* ------------------------------------------------------------------ the list */

typedef struct WINDOW_DATA {
    LONG Refs;
    PIDLIST_ABSOLUTE Idl;
    IDispatch *Window;
    LONG Cookie;
    HWND Handle;
    LONG Thread;
    int Kind;
    BOOL Pending;
} WINDOW_DATA;

typedef struct WINDOW_LIST {
    IShellWindows Windows;
    IServiceProvider Service;
    LONG Refs;
    CRITICAL_SECTION Lock;
    WINDOW_DATA **Items;
    int Count, Capacity;
    WINDOW_DATA **Pending;
    int PendingCount, PendingCapacity;
    LONG NextCookie;
    DESKTOP *Desktop;
} WINDOW_LIST;

static WINDOW_LIST *list_from_service(IServiceProvider *This)
{
    return (WINDOW_LIST *)((PUCHAR)This - offsetof(WINDOW_LIST, Service));
}

static void data_release(WINDOW_DATA *data)
{
    if (InterlockedDecrement(&data->Refs))
        return;
    if (data->Idl)
        ILFree(data->Idl);
    if (data->Window)
        data->Window->lpVtbl->Release(data->Window);
    HeapFree(GetProcessHeap(), 0, data);
}

static WINDOW_DATA *data_new(void)
{
    WINDOW_DATA *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*data));
    if (data)
        data->Refs = 1;
    return data;
}

static BOOL array_add(WINDOW_DATA ***array, int *count, int *capacity, WINDOW_DATA *data)
{
    if (*count == *capacity) {
        int grown = *capacity ? *capacity * 2 : 8;
        WINDOW_DATA **bigger = *array ? HeapReAlloc(GetProcessHeap(), 0, *array, grown * sizeof(**array))
                                      : HeapAlloc(GetProcessHeap(), 0, grown * sizeof(**array));
        if (!bigger)
            return FALSE;
        *array = bigger;
        *capacity = grown;
    }
    (*array)[(*count)++] = data;
    return TRUE;
}

static void array_remove(WINDOW_DATA **array, int *count, int index)
{
    memmove(array + index, array + index + 1, (*count - index - 1) * sizeof(*array));
    (*count)--;
}

static LONG new_cookie(WINDOW_LIST *list)
{
    /* never 0, which means no window. */
    LONG cookie = InterlockedIncrement(&list->NextCookie);
    return cookie;
}

/* the finished ITEMIDLIST of the place a variant names: the desktop for a missing or unreadable one. */
static PIDLIST_ABSOLUTE variant_idl(const VARIANT *value)
{
    while (value && value->vt == (VT_VARIANT | VT_BYREF) && value->pvarVal)
        value = value->pvarVal;
    if (!value)
        return nullptr;
    if (value->vt == VT_BSTR && value->bstrVal && value->bstrVal[0])
        return ILCreateFromPathW(value->bstrVal);
    if (value->vt == VT_I4 || value->vt == VT_I2) {
        int id = value->vt == VT_I4 ? value->lVal : value->iVal;
        WCHAR path[MAX_PATH];
        if (id && SUCCEEDED(SHGetFolderPathW(nullptr, id, nullptr, SHGFP_TYPE_CURRENT, path)))
            return ILCreateFromPathW(path);
    }
    if (value->vt == (VT_ARRAY | VT_UI1) && value->parray && value->parray->cDims == 1 && value->parray->pvData) {
        PIDLIST_ABSOLUTE copy = ILClone((PCUIDLIST_RELATIVE)value->parray->pvData);
        return copy;
    }
    return nullptr;
}

static PIDLIST_ABSOLUTE desktop_idl(void)
{
    return ILClone((PCUIDLIST_RELATIVE)g_desktop_idl);
}

/* ------------------------------------------------------------------ IShellWindows */

static WINDOW_LIST *list_from_windows(IShellWindows *This)
{
    return (WINDOW_LIST *)This;
}

static HRESULT list_query(WINDOW_LIST *list, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IShellWindows))
        *object = &list->Windows;
    else if (IsEqualIID(riid, &IID_IServiceProvider))
        *object = &list->Service;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&list->Refs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_query_interface(IShellWindows *This, REFIID riid, void **object)
{
    return list_query(list_from_windows(This), riid, object);
}

static ULONG STDMETHODCALLTYPE sw_add_ref(IShellWindows *This)
{
    return (ULONG)InterlockedIncrement(&list_from_windows(This)->Refs);
}

static ULONG STDMETHODCALLTYPE sw_release(IShellWindows *This)
{
    WINDOW_LIST *list = list_from_windows(This);
    LONG refs = InterlockedDecrement(&list->Refs);
    if (!refs) {
        for (int i = 0; i < list->Count; i++)
            data_release(list->Items[i]);
        for (int i = 0; i < list->PendingCount; i++)
            data_release(list->Pending[i]);
        if (list->Desktop)
            desktop_release(list->Desktop);
        DeleteCriticalSection(&list->Lock);
        HeapFree(GetProcessHeap(), 0, list->Items);
        HeapFree(GetProcessHeap(), 0, list->Pending);
        HeapFree(GetProcessHeap(), 0, list);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE sw_get_type_info_count(IShellWindows *This, UINT *count)
{
    if (!count)
        return SW_E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_get_type_info(IShellWindows *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return SW_E_POINTER;
    *info = nullptr;
    if (index)
        return DISP_E_BADINDEX;
    return window_list_info(info);
}

static HRESULT STDMETHODCALLTYPE sw_get_ids_of_names(IShellWindows *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                                     DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr = window_list_info(&info);
    if (FAILED(hr))
        return hr;
    hr = DispGetIDsOfNames(info, names, count, ids);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sw_invoke(IShellWindows *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument)
{
    ITypeInfo *info;
    HRESULT hr = window_list_info(&info);
    if (FAILED(hr))
        return hr;
    hr = DispInvoke(This, info, id, flags, params, result, exception, argument);
    info->lpVtbl->Release(info);
    return hr;
}

/* the windows a script sees: those with a window that are not the desktop. */
static BOOL scripted(const WINDOW_DATA *data)
{
    return data->Handle && data->Kind != SWC_DESKTOP;
}

static HRESULT STDMETHODCALLTYPE sw_get_count(IShellWindows *This, LONG *count)
{
    WINDOW_LIST *list = list_from_windows(This);
    if (!count)
        return SW_E_POINTER;
    *count = 0;
    EnterCriticalSection(&list->Lock);
    for (int i = 0; i < list->Count; i++)
        if (scripted(list->Items[i]))
            ++*count;
    LeaveCriticalSection(&list->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_item(IShellWindows *This, VARIANT index, IDispatch **window)
{
    WINDOW_LIST *list = list_from_windows(This);
    if (!window)
        return SW_E_POINTER;
    *window = nullptr;
    const VARIANT *value = &index;
    while (value->vt == (VT_VARIANT | VT_BYREF) && value->pvarVal)
        value = value->pvarVal;
    if (value->vt != VT_I2 && value->vt != VT_I4 && value->vt != VT_ERROR && value->vt != VT_UI4)
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    if (value->vt == VT_ERROR || value->vt == VT_UI4)
        return S_FALSE;
    LONG wanted = value->vt == VT_I2 ? value->iVal : value->lVal;
    if (wanted < 0)
        return S_FALSE;
    HRESULT hr = S_FALSE;
    EnterCriticalSection(&list->Lock);
    for (int i = 0, seen = 0; i < list->Count; i++) {
        WINDOW_DATA *data = list->Items[i];
        if (!scripted(data))
            continue;
        if (seen++ != wanted)
            continue;
        if (data->Window && IsWindow(data->Handle)) {
            *window = data->Window;
            data->Window->lpVtbl->AddRef(data->Window);
            hr = S_OK;
        }
        break;
    }
    LeaveCriticalSection(&list->Lock);
    return hr;
}

/* the enumerator of the windows: a snapshot of what the list held. */
typedef struct WINDOW_ENUM {
    IEnumVARIANT Iface;
    LONG Refs;
    IDispatch **Windows;
    ULONG Count, At;
} WINDOW_ENUM;

static HRESULT STDMETHODCALLTYPE we_query_interface(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumVARIANT))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((WINDOW_ENUM *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE we_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&((WINDOW_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE we_release(IEnumVARIANT *This)
{
    WINDOW_ENUM *e = (WINDOW_ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        for (ULONG i = 0; i < e->Count; i++)
            e->Windows[i]->lpVtbl->Release(e->Windows[i]);
        HeapFree(GetProcessHeap(), 0, e->Windows);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE we_next(IEnumVARIANT *This, ULONG count, VARIANT *items, ULONG *fetched)
{
    WINDOW_ENUM *e = (WINDOW_ENUM *)This;
    ULONG n = 0;
    while (n < count && e->At < e->Count) {
        VariantInit(&items[n]);
        items[n].vt = VT_DISPATCH;
        items[n].pdispVal = e->Windows[e->At++];
        items[n].pdispVal->lpVtbl->AddRef(items[n].pdispVal);
        n++;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE we_skip(IEnumVARIANT *This, ULONG count)
{
    WINDOW_ENUM *e = (WINDOW_ENUM *)This;
    e->At = e->At + count > e->Count ? e->Count : e->At + count;
    return e->At == e->Count && count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE we_reset(IEnumVARIANT *This)
{
    ((WINDOW_ENUM *)This)->At = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE we_clone(IEnumVARIANT *This, IEnumVARIANT **clone)
{
    return E_NOTIMPL;
}

static const IEnumVARIANTVtbl g_enum_vtbl = {we_query_interface, we_add_ref, we_release, we_next, we_skip, we_reset, we_clone};

static HRESULT STDMETHODCALLTYPE sw_new_enum(IShellWindows *This, IUnknown **enumerator)
{
    WINDOW_LIST *list = list_from_windows(This);
    if (!enumerator)
        return SW_E_POINTER;
    *enumerator = nullptr;
    WINDOW_ENUM *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&list->Lock);
    e->Windows = HeapAlloc(GetProcessHeap(), 0, (list->Count + 1) * sizeof(*e->Windows));
    for (int i = 0; e->Windows && i < list->Count; i++) {
        WINDOW_DATA *data = list->Items[i];
        if (scripted(data) && data->Window) {
            e->Windows[e->Count++] = data->Window;
            data->Window->lpVtbl->AddRef(data->Window);
        }
    }
    LeaveCriticalSection(&list->Lock);
    e->Iface.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    *enumerator = (IUnknown *)&e->Iface;
    return S_OK;
}

/* a pending window of this window handle, taken out of the list of pending windows. */
static WINDOW_DATA *take_pending(WINDOW_LIST *list, HWND handle, LONG cookie)
{
    for (int i = 0; i < list->PendingCount; i++) {
        WINDOW_DATA *data = list->Pending[i];
        if ((handle && data->Handle == handle) || (cookie && data->Cookie == cookie)) {
            array_remove(list->Pending, &list->PendingCount, i);
            return data;
        }
    }
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE sw_register(IShellWindows *This, IDispatch *window, LONG hwnd, int kind, LONG *cookie)
{
    WINDOW_LIST *list = list_from_windows(This);
    if (!cookie || (!hwnd && kind != SWC_CALLBACK) || (kind == SWC_CALLBACK && !window))
        return SW_E_POINTER;
    if (window)
        window->lpVtbl->AddRef(window);
    EnterCriticalSection(&list->Lock);
    WINDOW_DATA *data = take_pending(list, (HWND)(LONG_PTR)hwnd, 0);
    if (!data) {
        data = data_new();
        if (!data) {
            LeaveCriticalSection(&list->Lock);
            if (window)
                window->lpVtbl->Release(window);
            return E_OUTOFMEMORY;
        }
        data->Cookie = new_cookie(list);
    }
    data->Window = window;
    data->Kind = kind;
    data->Handle = (HWND)(LONG_PTR)hwnd;
    *cookie = data->Cookie;
    if (!array_add(&list->Items, &list->Count, &list->Capacity, data)) {
        data_release(data);
        *cookie = 0;
        LeaveCriticalSection(&list->Lock);
        return E_OUTOFMEMORY;
    }
    LeaveCriticalSection(&list->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_register_pending(IShellWindows *This, LONG thread, VARIANT *location, VARIANT *root,
                                                     int kind, LONG *cookie)
{
    WINDOW_LIST *list = list_from_windows(This);
    if (cookie)
        *cookie = 0;
    WINDOW_DATA *data = data_new();
    if (!data)
        return E_OUTOFMEMORY;
    data->Kind = kind;
    data->Thread = thread;
    data->Pending = TRUE;
    data->Idl = location ? variant_idl(location) : nullptr;
    if (!data->Idl) {
        data_release(data);
        return location ? E_FAIL : E_OUTOFMEMORY;
    }
    EnterCriticalSection(&list->Lock);
    data->Cookie = new_cookie(list);
    if (cookie)
        *cookie = data->Cookie;
    if (!array_add(&list->Pending, &list->PendingCount, &list->PendingCapacity, data))
        data_release(data);
    LeaveCriticalSection(&list->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_revoke(IShellWindows *This, LONG cookie)
{
    WINDOW_LIST *list = list_from_windows(This);
    WINDOW_DATA *found = nullptr;
    EnterCriticalSection(&list->Lock);
    for (int i = list->Count - 1; i >= 0 && !found; i--)
        if (list->Items[i]->Cookie == cookie) {
            found = list->Items[i];
            array_remove(list->Items, &list->Count, i);
        }
    if (!found)
        found = take_pending(list, nullptr, cookie);
    LeaveCriticalSection(&list->Lock);
    if (!found)
        return S_FALSE;
    data_release(found);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_on_navigate(IShellWindows *This, LONG cookie, VARIANT *location)
{
    WINDOW_LIST *list = list_from_windows(This);
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    EnterCriticalSection(&list->Lock);
    for (int i = 0; i < list->Count; i++) {
        WINDOW_DATA *data = list->Items[i];
        if (data->Cookie != cookie)
            continue;
        if (data->Idl)
            ILFree(data->Idl);
        data->Idl = location ? variant_idl(location) : nullptr;
        hr = data->Idl ? S_OK : E_FAIL;
        break;
    }
    LeaveCriticalSection(&list->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sw_on_activated(IShellWindows *This, LONG cookie, VARIANT_BOOL active)
{
    return S_OK;
}

/*
 * the window of a kind that is at a place. The place is a path or a folder number, the desktop when it is not one. With
 * SWFO_COOKIEPASSED the number in the location is the cookie of the window wanted. A window whose handle is gone is
 * dropped and the search goes on. The answer is S_OK with the handle, with the dispatch interface when asked, S_FALSE when
 * nothing is there. A window that is pending is E_PENDING with its cookie, when pending windows were asked for. Without a
 * place for the handle the answer is S_FALSE even for a window that is there, as the shell windows of windows answer.
 */
static HRESULT STDMETHODCALLTYPE sw_find_window(IShellWindows *This, VARIANT *location, VARIANT *root, int kind, LONG *hwnd,
                                                int options, IDispatch **window)
{
    WINDOW_LIST *list = list_from_windows(This);
    LONG want_cookie = 0;
    PIDLIST_ABSOLUTE place = nullptr;
    if (location) {
        const VARIANT *value = location;
        while (value->vt == (VT_VARIANT | VT_BYREF) && value->pvarVal)
            value = value->pvarVal;
        place = variant_idl(value);
        if ((options & SWFO_COOKIEPASSED) && value->vt == VT_I4)
            want_cookie = value->lVal;
        else if ((options & SWFO_COOKIEPASSED) && value->vt == VT_I2)
            want_cookie = value->iVal;
    }
    BOOL own_place = place != nullptr;
    if (!place)
        place = (PIDLIST_ABSOLUTE)g_desktop_idl;
    if (window)
        *window = nullptr;
    if (hwnd)
        *hwnd = 0;
    HRESULT hr = S_FALSE;
    if ((options & SWFO_NEEDDISPATCH) && !window) {
        hr = SW_E_POINTER;
        goto done;
    }

    for (;;) {
        WINDOW_DATA *match = nullptr;
        EnterCriticalSection(&list->Lock);
        for (int i = 0; i < list->Count && !match; i++) {
            WINDOW_DATA *data = list->Items[i];
            if (data->Kind != kind || (want_cookie && want_cookie != data->Cookie) || !data->Idl ||
                !ILIsEqual(data->Idl, place))
                continue;
            match = data;
            InterlockedIncrement(&match->Refs);
        }
        LeaveCriticalSection(&list->Lock);
        if (!match)
            break;
        if (match->Handle && !IsWindow(match->Handle)) {
            /* the window is gone, so its entry is. */
            sw_revoke(This, match->Cookie);
            data_release(match);
            continue;
        }
        if (hwnd) {
            *hwnd = (LONG)(LONG_PTR)match->Handle;
            hr = S_OK;
        }
        if (options & SWFO_NEEDDISPATCH)
            hr = match->Window ? match->Window->lpVtbl->QueryInterface(match->Window, &IID_IDispatch, (void **)window)
                               : E_NOINTERFACE;
        data_release(match);
        goto done;
    }
    if (options & SWFO_INCLUDEPENDING) {
        EnterCriticalSection(&list->Lock);
        for (int i = 0; i < list->PendingCount; i++) {
            WINDOW_DATA *data = list->Pending[i];
            if (data->Kind == kind && (!want_cookie || want_cookie == data->Cookie) && data->Idl &&
                ILIsEqual(data->Idl, place)) {
                if (hwnd)
                    *hwnd = data->Cookie;
                hr = E_PENDING_;
                break;
            }
        }
        LeaveCriticalSection(&list->Lock);
    }
done:
    if (own_place)
        ILFree(place);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sw_on_created(IShellWindows *This, LONG cookie, IUnknown *object)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sw_process_attach_detach(IShellWindows *This, VARIANT_BOOL attach)
{
    return S_OK;
}

static const IShellWindowsVtbl g_windows_vtbl = {
    sw_query_interface,
    sw_add_ref,
    sw_release,
    sw_get_type_info_count,
    sw_get_type_info,
    sw_get_ids_of_names,
    sw_invoke,
    sw_get_count,
    sw_item,
    sw_new_enum,
    sw_register,
    sw_register_pending,
    sw_revoke,
    sw_on_navigate,
    sw_on_activated,
    sw_find_window,
    sw_on_created,
    sw_process_attach_detach,
};

/* the services of the list are only enumerations of its windows, which nobody asks Mime for. */
static HRESULT STDMETHODCALLTYPE sp_query_interface(IServiceProvider *This, REFIID riid, void **object)
{
    return list_query(list_from_service(This), riid, object);
}

static ULONG STDMETHODCALLTYPE sp_add_ref(IServiceProvider *This)
{
    return (ULONG)InterlockedIncrement(&list_from_service(This)->Refs);
}

static ULONG STDMETHODCALLTYPE sp_release(IServiceProvider *This)
{
    return sw_release(&list_from_service(This)->Windows);
}

static HRESULT STDMETHODCALLTYPE sp_query_service(IServiceProvider *This, REFGUID service, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl g_provider_vtbl = {sp_query_interface, sp_add_ref, sp_release, sp_query_service};

static WINDOW_LIST *create_list(void)
{
    WINDOW_LIST *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
    if (!list)
        return nullptr;
    list->Windows.lpVtbl = &g_windows_vtbl;
    list->Service.lpVtbl = &g_provider_vtbl;
    list->Refs = 1;
    InitializeCriticalSection(&list->Lock);

    /* the desktop is in the list from the start, as the window of the shell. */
    list->Desktop = create_desktop();
    if (list->Desktop) {
        LONG cookie;
        if (SUCCEEDED(sw_register(&list->Windows, &list->Desktop->Dispatch, (LONG)(LONG_PTR)list->Desktop->Window,
                                  SWC_DESKTOP, &cookie))) {
            list->Items[list->Count - 1]->Idl = desktop_idl();
        }
    }
    return list;
}

/* ------------------------------------------------------------------ the class factories */

/*
 * the class the registry names for the shell's windows is a factory of factories: asked for IClassFactory it makes the
 * one that serves CLSID_ShellWindows, which holds the list and gives it to everybody who creates an instance.
 */
typedef struct LIST_FACTORY {
    IClassFactory Iface;
    LONG Refs;
    WINDOW_LIST *List;
} LIST_FACTORY;

static HRESULT STDMETHODCALLTYPE lf_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((LIST_FACTORY *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE lf_add_ref(IClassFactory *This)
{
    return (ULONG)InterlockedIncrement(&((LIST_FACTORY *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE lf_release(IClassFactory *This)
{
    LIST_FACTORY *factory = (LIST_FACTORY *)This;
    LONG refs = InterlockedDecrement(&factory->Refs);
    if (!refs) {
        if (factory->List)
            factory->List->Windows.lpVtbl->Release(&factory->List->Windows);
        HeapFree(GetProcessHeap(), 0, factory);
    }
    return (ULONG)refs;
}

/* every program that asks gets the same list. */
static HRESULT STDMETHODCALLTYPE lf_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return list_query(((LIST_FACTORY *)This)->List, riid, object);
}

static HRESULT STDMETHODCALLTYPE lf_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_list_factory_vtbl = {
    lf_query_interface, lf_add_ref, lf_release, lf_create_instance, lf_lock_server,
};

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return SW_E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    LIST_FACTORY *factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;
    factory->Iface.lpVtbl = &g_list_factory_vtbl;
    factory->Refs = 1;
    factory->List = create_list();
    HRESULT hr = factory->List ? lf_query_interface(&factory->Iface, riid, object) : E_OUTOFMEMORY;
    lf_release(&factory->Iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static IClassFactory g_factory = {&g_factory_vtbl};

IClassFactory *ShellWindowsFactory(void)
{
    return &g_factory;
}
