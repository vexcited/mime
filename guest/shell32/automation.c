/*
 * the shell's automation objects, which scripts and programs ask for by class: Shell.Application, the object that opens
 * folders and starts programs for them, and the view of a folder that gives scripts the same application back. Both are
 * dual interfaces that answer IDispatch from the type library of this dll, a resource of it.
 *
 * Shell.Application is also what a program asks for to start a process without the rights it holds itself: the
 * desktop's shell is a server that runs with the limited token of the user, and ShellExecute on its application object
 * starts the program there, see localserver.c and explorerframe.
 */
#include "shell32_private.h"
#include <mime/win32/shldisp.h>

/* ------------------------------------------------------------------ the type library */

static SRWLOCK g_library_lock = SRWLOCK_INIT;
static ITypeLib *g_library;

/* the type information of one of the interfaces of the library. */
HRESULT ShTypeInfo(REFIID iid, ITypeInfo **info)
{
    *info = nullptr;
    AcquireSRWLockExclusive(&g_library_lock);
    HRESULT hr = S_OK;
    if (!g_library) {
        WCHAR path[MAX_PATH];
        if (!GetModuleFileNameW(g_shell32, path, MAX_PATH))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            hr = LoadTypeLibEx(path, REGKIND_NONE, &g_library);
    }
    if (SUCCEEDED(hr))
        hr = g_library->lpVtbl->GetTypeInfoOfGuid(g_library, iid, info);
    ReleaseSRWLockExclusive(&g_library_lock);
    return hr;
}

/* ------------------------------------------------------------------ variants */

/* a variant that holds a value, with the reference to one followed. */
const VARIANT *ShPlainVariant(const VARIANT *value)
{
    while (value && value->vt == (VT_VARIANT | VT_BYREF) && value->pvarVal)
        value = value->pvarVal;
    return value;
}

/* the text of a variant, a string or a number written out, a task memory string or NULL when it holds none. */
LPWSTR ShVariantText(const VARIANT *value)
{
    value = ShPlainVariant(value);
    if (!value || value->vt == VT_EMPTY || value->vt == VT_NULL || value->vt == VT_ERROR)
        return nullptr;
    VARIANT text;
    VariantInit(&text);
    if (FAILED(VariantChangeType(&text, (VARIANT *)value, 0, VT_BSTR)) || !text.bstrVal)
        return nullptr;
    LPWSTR copy = ShDuplicate(text.bstrVal);
    VariantClear(&text);
    return copy;
}

/* ------------------------------------------------------------------ Shell.Application */

typedef struct SHELL_APPLICATION {
    IShellDispatch2 Iface;
    LONG Refs;
} SHELL_APPLICATION;

static LONG g_objects;

static HRESULT STDMETHODCALLTYPE app_query_interface(IShellDispatch2 *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_IShellDispatch) &&
        !IsEqualIID(riid, &IID_IShellDispatch2))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((SHELL_APPLICATION *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE app_add_ref(IShellDispatch2 *This)
{
    return (ULONG)InterlockedIncrement(&((SHELL_APPLICATION *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE app_release(IShellDispatch2 *This)
{
    LONG refs = InterlockedDecrement(&((SHELL_APPLICATION *)This)->Refs);
    if (!refs) {
        InterlockedDecrement(&g_objects);
        ShFree(This);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE app_get_type_info_count(IShellDispatch2 *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE app_get_type_info(IShellDispatch2 *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    if (index)
        return DISP_E_BADINDEX;
    return ShTypeInfo(&IID_IShellDispatch2, info);
}

static HRESULT STDMETHODCALLTYPE app_get_ids_of_names(IShellDispatch2 *This, REFIID riid, LPOLESTR *names, UINT count,
                                                      LCID lcid, DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr = ShTypeInfo(&IID_IShellDispatch2, &info);
    if (FAILED(hr))
        return hr;
    hr = DispGetIDsOfNames(info, names, count, ids);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE app_invoke(IShellDispatch2 *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                            DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument)
{
    ITypeInfo *info;
    HRESULT hr = ShTypeInfo(&IID_IShellDispatch2, &info);
    if (FAILED(hr))
        return hr;
    hr = DispInvoke(This, info, id, flags, params, result, exception, argument);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE app_get_self(IShellDispatch2 *This, IDispatch **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = (IDispatch *)This;
    app_add_ref(This);
    return S_OK;
}

#define APP_UNSUPPORTED(name)                                                                                          \
    static HRESULT STDMETHODCALLTYPE app_##name(IShellDispatch2 *This)                                                 \
    {                                                                                                                  \
        return E_NOTIMPL;                                                                                              \
    }

APP_UNSUPPORTED(windows_none)

static HRESULT STDMETHODCALLTYPE app_name_space(IShellDispatch2 *This, VARIANT directory, IDispatch **folder)
{
    if (!folder)
        return E_POINTER;
    return ShNameSpace(&directory, folder);
}

static HRESULT STDMETHODCALLTYPE app_browse_for_folder(IShellDispatch2 *This, LONG hwnd, BSTR title, LONG options,
                                                       VARIANT root, IDispatch **folder)
{
    if (folder)
        *folder = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE app_windows(IShellDispatch2 *This, IDispatch **windows)
{
    if (windows)
        *windows = nullptr;
    return E_NOTIMPL;
}

/* a folder given by its path or by the number of a special folder, opened with the verb. */
static HRESULT open_folder(const VARIANT *directory, LPCWSTR verb)
{
    directory = ShPlainVariant(directory);
    WCHAR path[MAX_PATH];
    path[0] = 0;
    if (directory && (directory->vt == VT_I2 || directory->vt == VT_I4)) {
        int id = directory->vt == VT_I2 ? directory->iVal : directory->lVal;
        if (FAILED(SHGetFolderPathW(nullptr, id, nullptr, SHGFP_TYPE_CURRENT, path)))
            return S_FALSE;
    } else {
        LPWSTR text = ShVariantText(directory);
        if (!text)
            return S_FALSE;
        if (wcslen(text) >= MAX_PATH) {
            ShFree(text);
            return S_FALSE;
        }
        wcscpy(path, text);
        ShFree(text);
    }
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.lpFile = path;
    info.lpVerb = verb;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE app_open(IShellDispatch2 *This, VARIANT directory)
{
    return open_folder(&directory, nullptr);
}

static HRESULT STDMETHODCALLTYPE app_explore(IShellDispatch2 *This, VARIANT directory)
{
    return open_folder(&directory, L"explore");
}

static HRESULT STDMETHODCALLTYPE app_control_panel_item(IShellDispatch2 *This, BSTR directory)
{
    return E_NOTIMPL;
}

/* the policy of a group, its value in the keys of the user and of the machine. */
static HRESULT STDMETHODCALLTYPE app_is_restricted(IShellDispatch2 *This, BSTR group, BSTR restriction, LONG *value)
{
    if (!value)
        return E_POINTER;
    *value = 0;
    if (!group || !restriction)
        return S_OK;
    WCHAR path[MAX_PATH];
    if (wcslen(group) > 128)
        return S_OK;
    _snwprintf(path, MAX_PATH, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\%ls", group);
    path[MAX_PATH - 1] = 0;
    HKEY roots[2] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (int i = 0; i < 2 && !*value; i++) {
        HKEY key;
        if (RegOpenKeyExW(roots[i], path, 0, KEY_READ, &key))
            continue;
        DWORD data = 0, type, size = sizeof(data);
        if (!RegQueryValueExW(key, restriction, nullptr, &type, (BYTE *)&data, &size) && type == REG_DWORD && data)
            *value = 1;
        RegCloseKey(key);
    }
    return S_OK;
}

/*
 * starts a program through ShellExecuteEx as the process that serves the object. the arguments, directory and verb are
 * what a variant can be written out as, the show command a number and 1 when it is not one. The answer is S_OK when the
 * program started and S_FALSE when it did not, not an error, as the object of windows answers.
 */
static HRESULT STDMETHODCALLTYPE app_shell_execute(IShellDispatch2 *This, BSTR file, VARIANT arguments, VARIANT directory,
                                                   VARIANT operation, VARIANT show)
{
    LPWSTR parameters = ShVariantText(&arguments);
    LPWSTR folder = ShVariantText(&directory);
    LPWSTR verb = ShVariantText(&operation);
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.lpFile = file;
    info.lpParameters = parameters;
    info.lpDirectory = folder;
    info.lpVerb = verb;
    info.nShow = SW_SHOWNORMAL;
    const VARIANT *number = ShPlainVariant(&show);
    if (number && number->vt == VT_I2)
        info.nShow = number->iVal;
    else if (number && number->vt == VT_I4)
        info.nShow = number->lVal;
    BOOL started = ShellExecuteExW(&info);
    ShFree(parameters);
    ShFree(folder);
    ShFree(verb);
    return started ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE app_find_printer(IShellDispatch2 *This, BSTR name, BSTR location, BSTR model)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE app_get_system_information(IShellDispatch2 *This, BSTR name, VARIANT *value)
{
    if (value)
        VariantInit(value);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE app_service_start(IShellDispatch2 *This, BSTR service, VARIANT persistent, VARIANT *result)
{
    if (result)
        VariantInit(result);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE app_service_query(IShellDispatch2 *This, BSTR service, VARIANT *result)
{
    if (result)
        VariantInit(result);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE app_show_browser_bar(IShellDispatch2 *This, BSTR clsid, VARIANT show, VARIANT *result)
{
    if (result)
        VariantInit(result);
    return E_NOTIMPL;
}

static const IShellDispatch2Vtbl g_app_vtbl = {
    app_query_interface,
    app_add_ref,
    app_release,
    app_get_type_info_count,
    app_get_type_info,
    app_get_ids_of_names,
    app_invoke,
    app_get_self,
    app_get_self,
    app_name_space,
    app_browse_for_folder,
    app_windows,
    app_open,
    app_explore,
    app_windows_none, /* MinimizeAll */
    app_windows_none, /* UndoMinimizeALL */
    app_windows_none, /* FileRun */
    app_windows_none, /* CascadeWindows */
    app_windows_none, /* TileVertically */
    app_windows_none, /* TileHorizontally */
    app_windows_none, /* ShutdownWindows */
    app_windows_none, /* Suspend */
    app_windows_none, /* EjectPC */
    app_windows_none, /* SetTime */
    app_windows_none, /* TrayProperties */
    app_windows_none, /* Help */
    app_windows_none, /* FindFiles */
    app_windows_none, /* FindComputer */
    app_windows_none, /* RefreshMenu */
    app_control_panel_item,
    app_is_restricted,
    app_shell_execute,
    app_find_printer,
    app_get_system_information,
    app_service_start,
    app_service_start,
    app_service_query,
    app_service_query,
    app_show_browser_bar,
};

HRESULT ShCreateApplication(REFIID riid, void **object)
{
    SHELL_APPLICATION *app = ShAllocZero(sizeof(*app));
    if (!app)
        return E_OUTOFMEMORY;
    app->Iface.lpVtbl = &g_app_vtbl;
    app->Refs = 1;
    InterlockedIncrement(&g_objects);
    HRESULT hr = app_query_interface(&app->Iface, riid, object);
    app_release(&app->Iface);
    return hr;
}

/* ------------------------------------------------------------------ the view of a folder, for scripts */

typedef struct FOLDER_VIEW {
    IShellFolderViewDual Iface;
    LONG Refs;
} FOLDER_VIEW;

static HRESULT STDMETHODCALLTYPE view_query_interface(IShellFolderViewDual *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) &&
        !IsEqualIID(riid, &IID_IShellFolderViewDual))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((FOLDER_VIEW *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE view_add_ref(IShellFolderViewDual *This)
{
    return (ULONG)InterlockedIncrement(&((FOLDER_VIEW *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE view_release(IShellFolderViewDual *This)
{
    LONG refs = InterlockedDecrement(&((FOLDER_VIEW *)This)->Refs);
    if (!refs) {
        InterlockedDecrement(&g_objects);
        ShFree(This);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE view_get_type_info_count(IShellFolderViewDual *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_get_type_info(IShellFolderViewDual *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    if (index)
        return DISP_E_BADINDEX;
    return ShTypeInfo(&IID_IShellFolderViewDual, info);
}

static HRESULT STDMETHODCALLTYPE view_get_ids_of_names(IShellFolderViewDual *This, REFIID riid, LPOLESTR *names, UINT count,
                                                       LCID lcid, DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr = ShTypeInfo(&IID_IShellFolderViewDual, &info);
    if (FAILED(hr))
        return hr;
    hr = DispGetIDsOfNames(info, names, count, ids);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE view_invoke(IShellFolderViewDual *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                             DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument)
{
    ITypeInfo *info;
    HRESULT hr = ShTypeInfo(&IID_IShellFolderViewDual, &info);
    if (FAILED(hr))
        return hr;
    hr = DispInvoke(This, info, id, flags, params, result, exception, argument);
    info->lpVtbl->Release(info);
    return hr;
}

/* the application of a view is Shell.Application, made in this process. */
static HRESULT STDMETHODCALLTYPE view_get_application(IShellFolderViewDual *This, IDispatch **application)
{
    if (!application)
        return E_INVALIDARG;
    return ShCreateApplication(&IID_IDispatch, (void **)application);
}

/* a view of the desktop has no folder, items or script to give. */
static HRESULT STDMETHODCALLTYPE view_get_dispatch(IShellFolderViewDual *This, IDispatch **object)
{
    if (object)
        *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE view_select_item(IShellFolderViewDual *This, VARIANT *item, int flags)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE view_popup_item_menu(IShellFolderViewDual *This, IDispatch *item, VARIANT x, VARIANT y,
                                                      BSTR *command)
{
    if (command)
        *command = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE view_get_view_options(IShellFolderViewDual *This, LONG *options)
{
    if (!options)
        return E_POINTER;
    *options = 0;
    return S_OK;
}

static const IShellFolderViewDualVtbl g_view_vtbl = {
    view_query_interface,
    view_add_ref,
    view_release,
    view_get_type_info_count,
    view_get_type_info,
    view_get_ids_of_names,
    view_invoke,
    view_get_application,
    view_get_dispatch,
    view_get_dispatch,
    view_get_dispatch,
    view_get_dispatch,
    view_select_item,
    view_popup_item_menu,
    view_get_dispatch,
    view_get_view_options,
};

static HRESULT create_folder_view(REFIID riid, void **object)
{
    FOLDER_VIEW *view = ShAllocZero(sizeof(*view));
    if (!view)
        return E_OUTOFMEMORY;
    view->Iface.lpVtbl = &g_view_vtbl;
    view->Refs = 1;
    InterlockedIncrement(&g_objects);
    HRESULT hr = view_query_interface(&view->Iface, riid, object);
    view_release(&view->Iface);
    return hr;
}

/* ------------------------------------------------------------------ the classes */

typedef struct CLASS_FACTORY {
    IClassFactory Iface;
    HRESULT (*Create)(REFIID riid, void **object);
} CLASS_FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
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
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return ((CLASS_FACTORY *)This)->Create(riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static CLASS_FACTORY g_application_factory = {{&g_factory_vtbl}, ShCreateApplication};
static CLASS_FACTORY g_view_factory = {{&g_factory_vtbl}, create_folder_view};

HRESULT ShAutomationClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    CLASS_FACTORY *factory;
    if (IsEqualCLSID(clsid, &CLSID_Shell))
        factory = &g_application_factory;
    else if (IsEqualCLSID(clsid, &CLSID_ShellFolderView))
        factory = &g_view_factory;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->Iface.lpVtbl->QueryInterface(&factory->Iface, riid, object);
}

SHSTDAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    HRESULT hr = ShDragDropClassObject(clsid, riid, object);
    if (hr == CLASS_E_CLASSNOTAVAILABLE)
        hr = ShAutomationClassObject(clsid, riid, object);
    return hr;
}

/* the automation objects live as long as somebody holds one; the helper of drag and drop never goes. */
SHSTDAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}
