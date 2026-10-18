/*
 * the common item dialogs, FileOpenDialog and FileSaveDialog: IFileDialog,
 * IFileOpenDialog or IFileSaveDialog, IFileDialog2, IFileDialogCustomize,
 * IObjectWithSite and IOleWindow over the file dialog window of filedlg.c.
 *
 * every interface of the object is a pointer to a small header holding its
 * table and the object, so one function serves each method of all of them.
 */
#include "filedlg.h"
#include <mime/win32/shlwapi.h>

#define MAX_LISTENERS 8
/* cookies of Advise count from here, as the slots of the shell's own listeners come first. */
#define FIRST_COOKIE 4

typedef struct _ITEMDLG ITEMDLG;

typedef struct _FACE {
    const void *Table;
    ITEMDLG *Owner;
} FACE;

struct _ITEMDLG {
    FACE Main;
    FACE Dialog2;
    FACE Customize;
    FACE Site;
    FACE Window;
    LONG Refs;
    BOOL Save;
    FILEDLG *Dialog;
    IFileDialogEvents *Listeners[MAX_LISTENERS];
    IShellItemFilter *Filter;
    IShellItemArray *Results;
    IShellItem *Result;
    IUnknown *SiteObject;
    IPropertyStore *Properties;
    UINT FileTypeIndex;
    BOOL TypesSet;
};

static const IFileOpenDialogVtbl g_open_vtbl;
static const IFileSaveDialogVtbl g_save_vtbl;
static const IFileDialog2Vtbl g_dialog2_vtbl;
static const IFileDialogCustomizeVtbl g_customize_vtbl;
static const IObjectWithSiteVtbl g_site_vtbl;
static const IOleWindowVtbl g_window_vtbl;

#define OWNER(This) (((FACE *)(This))->Owner)
#define IFACE(owner) ((IFileDialog *)&(owner)->Main)

#ifndef E_UNEXPECTED
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#endif

/* ------------------------------------------------------------------ items */

/* a shell item for a path, which need not exist yet. */
static HRESULT item_from_path(LPCWSTR path, IShellItem **item)
{
    *item = nullptr;
    PIDLIST_ABSOLUTE pidl = SHSimpleIDListFromPath(path);
    if (!pidl)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    HRESULT hr = SHCreateItemFromIDList(pidl, &IID_IShellItem, (void **)item);
    ILFree(pidl);
    return hr;
}

/* the file system path of an item. */
static HRESULT path_of_item(IShellItem *item, WCHAR *path)
{
    LPWSTR name;
    path[0] = 0;
    HRESULT hr = item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &name);
    if (FAILED(hr))
        return hr;
    if (wcslen(name) >= MAX_PATH)
        hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    else
        wcscpy(path, name);
    CoTaskMemFree(name);
    return hr;
}

static void release_results(ITEMDLG *d)
{
    if (d->Results)
        d->Results->lpVtbl->Release(d->Results);
    if (d->Result)
        d->Result->lpVtbl->Release(d->Result);
    d->Results = nullptr;
    d->Result = nullptr;
}

static HRESULT array_from_paths(LPWSTR *paths, UINT count, IShellItemArray **array)
{
    *array = nullptr;
    if (!count)
        return E_FAIL;
    PCIDLIST_ABSOLUTE *lists = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*lists));
    if (!lists)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        lists[i] = SHSimpleIDListFromPath(paths[i]);
        if (!lists[i])
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    }
    if (SUCCEEDED(hr))
        hr = SHCreateShellItemArrayFromIDLists(count, lists, array);
    for (UINT i = 0; i < count; i++)
        ILFree((PIDLIST_RELATIVE)lists[i]);
    HeapFree(GetProcessHeap(), 0, lists);
    return hr;
}

/* ------------------------------------------------------------------ events of the window */

#define FOR_LISTENERS(d, listener)                                                                                 \
    for (int i_ = 0; i_ < MAX_LISTENERS; i_++)                                                                     \
        if (((listener) = (d)->Listeners[i_]) != nullptr)

static void event_folder_change(FILEDLG *dialog, void *context)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    FOR_LISTENERS(d, sink)
        sink->lpVtbl->OnFolderChange(sink, IFACE(d));
}

static void event_selection_change(FILEDLG *dialog, void *context)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    FOR_LISTENERS(d, sink)
        sink->lpVtbl->OnSelectionChange(sink, IFACE(d));
}

static void event_type_change(FILEDLG *dialog, void *context)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    FOR_LISTENERS(d, sink)
        sink->lpVtbl->OnTypeChange(sink, IFACE(d));
}

static BOOL event_folder_changing(FILEDLG *dialog, void *context, LPCWSTR folder)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    IShellItem *item = nullptr;
    BOOL allowed = TRUE;
    if (FAILED(item_from_path(folder, &item)))
        return TRUE;
    FOR_LISTENERS(d, sink) {
        if (sink->lpVtbl->OnFolderChanging(sink, IFACE(d), item) == S_FALSE)
            allowed = FALSE;
    }
    item->lpVtbl->Release(item);
    return allowed;
}

/* a sink that refuses with S_FALSE or an error other than "not implemented" keeps the dialog open. */
static BOOL event_file_ok(FILEDLG *dialog, void *context)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    BOOL ok = TRUE;
    FOR_LISTENERS(d, sink) {
        HRESULT hr = sink->lpVtbl->OnFileOk(sink, IFACE(d));
        if (hr == S_FALSE || (FAILED(hr) && hr != E_NOTIMPL))
            ok = FALSE;
    }
    return ok;
}

static int event_overwrite(FILEDLG *dialog, void *context, LPCWSTR path)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    IShellItem *item = nullptr;
    int answer = FDEOR_DEFAULT;
    if (FAILED(item_from_path(path, &item)))
        return answer;
    FOR_LISTENERS(d, sink) {
        FDE_OVERWRITE_RESPONSE response = FDEOR_DEFAULT;
        if (SUCCEEDED(sink->lpVtbl->OnOverwrite(sink, IFACE(d), item, &response)) && response != FDEOR_DEFAULT)
            answer = response;
    }
    item->lpVtbl->Release(item);
    return answer;
}

static void event_control(FILEDLG *dialog, void *context, int event, DWORD id, DWORD item, BOOL checked)
{
    ITEMDLG *d = context;
    IFileDialogEvents *sink;
    FOR_LISTENERS(d, sink) {
        IFileDialogControlEvents *events;
        if (FAILED(sink->lpVtbl->QueryInterface(sink, &IID_IFileDialogControlEvents, (void **)&events)))
            continue;
        IFileDialogCustomize *customize = (IFileDialogCustomize *)&d->Customize;
        switch (event) {
        case 0:
            events->lpVtbl->OnButtonClicked(events, customize, id);
            break;
        case 1:
            events->lpVtbl->OnCheckButtonToggled(events, customize, id, checked);
            break;
        default:
            events->lpVtbl->OnItemSelected(events, customize, id, item);
            break;
        }
        events->lpVtbl->Release(events);
    }
}

static BOOL custom_filter(void *context, LPCWSTR path, BOOL folder)
{
    ITEMDLG *d = context;
    IShellItem *item;
    if (!d->Filter || FAILED(item_from_path(path, &item)))
        return TRUE;
    HRESULT hr = d->Filter->lpVtbl->IncludeItem(d->Filter, item);
    item->lpVtbl->Release(item);
    return hr != S_FALSE;
}

static const FILEDLG_EVENTS g_events = {
    .FileOk = event_file_ok,
    .FolderChanging = event_folder_changing,
    .FolderChange = event_folder_change,
    .SelectionChange = event_selection_change,
    .TypeChange = event_type_change,
    .Overwrite = event_overwrite,
};

/* ------------------------------------------------------------------ the object */

static HRESULT STDMETHODCALLTYPE d_query_interface(IFileDialog *This, REFIID riid, void **object)
{
    ITEMDLG *d = OWNER(This);
    void *found = nullptr;
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IModalWindow) || IsEqualIID(riid, &IID_IFileDialog) ||
        (d->Save ? IsEqualIID(riid, &IID_IFileSaveDialog) : IsEqualIID(riid, &IID_IFileOpenDialog)))
        found = &d->Main;
    else if (IsEqualIID(riid, &IID_IFileDialog2))
        found = &d->Dialog2;
    else if (IsEqualIID(riid, &IID_IFileDialogCustomize))
        found = &d->Customize;
    else if (IsEqualIID(riid, &IID_IObjectWithSite))
        found = &d->Site;
    else if (IsEqualIID(riid, &IID_IOleWindow))
        found = &d->Window;
    *object = found;
    if (!found)
        return E_NOINTERFACE;
    InterlockedIncrement(&d->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE d_add_ref(IFileDialog *This)
{
    return (ULONG)InterlockedIncrement(&OWNER(This)->Refs);
}

static ULONG STDMETHODCALLTYPE d_release(IFileDialog *This)
{
    ITEMDLG *d = OWNER(This);
    LONG refs = InterlockedDecrement(&d->Refs);
    if (!refs) {
        for (int i = 0; i < MAX_LISTENERS; i++)
            if (d->Listeners[i])
                d->Listeners[i]->lpVtbl->Release(d->Listeners[i]);
        if (d->Filter)
            d->Filter->lpVtbl->Release(d->Filter);
        if (d->SiteObject)
            d->SiteObject->lpVtbl->Release(d->SiteObject);
        if (d->Properties)
            d->Properties->lpVtbl->Release(d->Properties);
        release_results(d);
        FdFree(d->Dialog);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return (ULONG)refs;
}

/* ------------------------------------------------------------------ IFileDialog */

static HRESULT STDMETHODCALLTYPE d_show(IFileDialog *This, HWND owner)
{
    ITEMDLG *d = OWNER(This);
    if (FdIsShowing(d->Dialog))
        return E_UNEXPECTED;
    release_results(d);
    InterlockedIncrement(&d->Refs);
    HRESULT hr = FdShow(d->Dialog, owner);
    d->FileTypeIndex = FdTypeIndex(d->Dialog);
    if (SUCCEEDED(hr) && FdResultCount(d->Dialog)) {
        UINT count = FdResultCount(d->Dialog);
        LPWSTR *paths = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(LPWSTR));
        if (paths) {
            for (UINT i = 0; i < count; i++)
                paths[i] = (LPWSTR)FdResultPath(d->Dialog, i);
            hr = array_from_paths(paths, count, &d->Results);
            HeapFree(GetProcessHeap(), 0, paths);
        } else {
            hr = E_OUTOFMEMORY;
        }
        if (SUCCEEDED(hr))
            hr = item_from_path(FdResultPath(d->Dialog, 0), &d->Result);
        if (FAILED(hr))
            release_results(d);
    }
    d_release(This);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d_set_file_types(IFileDialog *This, UINT count, const COMDLG_FILTERSPEC *types)
{
    ITEMDLG *d = OWNER(This);
    if (!types)
        return E_INVALIDARG;
    if (d->TypesSet || FdIsShowing(d->Dialog))
        return E_UNEXPECTED;
    if (!count)
        return S_OK;
    for (UINT i = 0; i < count; i++)
        if (!types[i].pszName || !types[i].pszSpec)
            return E_INVALIDARG;
    HRESULT hr = FdSetTypes(d->Dialog, count, types);
    if (SUCCEEDED(hr))
        d->TypesSet = TRUE;
    return hr;
}

static HRESULT STDMETHODCALLTYPE d_set_file_type_index(IFileDialog *This, UINT index)
{
    ITEMDLG *d = OWNER(This);
    if (FdIsShowing(d->Dialog))
        return E_UNEXPECTED;
    if (!FdTypeCount(d->Dialog))
        return E_FAIL;
    FdSetTypeIndex(d->Dialog, index);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_file_type_index(IFileDialog *This, UINT *index)
{
    ITEMDLG *d = OWNER(This);
    if (!index)
        return E_INVALIDARG;
    *index = FdIsShowing(d->Dialog) ? FdTypeIndex(d->Dialog) : (d->FileTypeIndex ? d->FileTypeIndex : FdTypeIndex(d->Dialog));
    return S_OK;
}

/* a sink gets the first free slot, and its cookie is the slot number plus one past the shell's own. */
static HRESULT STDMETHODCALLTYPE d_advise(IFileDialog *This, IFileDialogEvents *events, DWORD *cookie)
{
    ITEMDLG *d = OWNER(This);
    if (cookie)
        *cookie = (DWORD)-1;
    if (!events || !cookie)
        return E_INVALIDARG;
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (d->Listeners[i])
            continue;
        events->lpVtbl->AddRef(events);
        d->Listeners[i] = events;
        *cookie = FIRST_COOKIE + i;
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE d_unadvise(IFileDialog *This, DWORD cookie)
{
    ITEMDLG *d = OWNER(This);
    if (cookie < FIRST_COOKIE || cookie >= FIRST_COOKIE + MAX_LISTENERS || !d->Listeners[cookie - FIRST_COOKIE])
        return E_INVALIDARG;
    IFileDialogEvents *events = d->Listeners[cookie - FIRST_COOKIE];
    d->Listeners[cookie - FIRST_COOKIE] = nullptr;
    events->lpVtbl->Release(events);
    return S_OK;
}

/* the option bits no dialog has, a folder dialog that saves or takes many, and the two ways to ask for files that conflict. */
static HRESULT STDMETHODCALLTYPE d_set_options(IFileDialog *This, FILEOPENDIALOGOPTIONS options)
{
    ITEMDLG *d = OWNER(This);
    if (FdIsShowing(d->Dialog))
        return E_UNEXPECTED;
    if ((options & 0x0DC80411) || (options & 0xC0) == 0xC0)
        return E_INVALIDARG;
    if (d->Save && ((options & FOS_ALLOWMULTISELECT) || (options & FOS_PICKFOLDERS)))
        return E_INVALIDARG;
    if (options & FOS_PICKFOLDERS)
        options |= FOS_FILEMUSTEXIST;
    FdSetOptions(d->Dialog, options);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_options(IFileDialog *This, FILEOPENDIALOGOPTIONS *options)
{
    if (!options)
        return E_INVALIDARG;
    *options = FdOptions(OWNER(This)->Dialog);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_set_default_folder(IFileDialog *This, IShellItem *folder)
{
    WCHAR path[MAX_PATH];
    if (!folder)
        return E_INVALIDARG;
    if (FAILED(path_of_item(folder, path)))
        return S_OK;
    return FdSetDefaultFolder(OWNER(This)->Dialog, path);
}

static HRESULT STDMETHODCALLTYPE d_set_folder(IFileDialog *This, IShellItem *folder)
{
    WCHAR path[MAX_PATH];
    if (!folder)
        return E_INVALIDARG;
    if (FAILED(path_of_item(folder, path)))
        return S_OK;
    return FdSetFolder(OWNER(This)->Dialog, path);
}

static HRESULT STDMETHODCALLTYPE d_get_folder(IFileDialog *This, IShellItem **folder)
{
    ITEMDLG *d = OWNER(This);
    WCHAR path[MAX_PATH];
    if (!folder)
        return E_INVALIDARG;
    *folder = nullptr;
    if (!FdGetFolder(d->Dialog, path))
        return E_FAIL;
    return item_from_path(path, folder);
}

static HRESULT STDMETHODCALLTYPE d_get_current_selection(IFileDialog *This, IShellItem **item)
{
    ITEMDLG *d = OWNER(This);
    if (!item)
        return E_INVALIDARG;
    *item = nullptr;
    LPWSTR *paths;
    UINT count = FdGetSelection(d->Dialog, &paths);
    if (!count)
        return E_FAIL;
    HRESULT hr = item_from_path(paths[0], item);
    FdFreePaths(paths, count);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d_set_file_name(IFileDialog *This, LPCWSTR name)
{
    return FdSetFileName(OWNER(This)->Dialog, name);
}

static HRESULT STDMETHODCALLTYPE d_get_file_name(IFileDialog *This, LPWSTR *name)
{
    if (!name)
        return E_INVALIDARG;
    return FdGetFileName(OWNER(This)->Dialog, name);
}

static HRESULT STDMETHODCALLTYPE d_set_title(IFileDialog *This, LPCWSTR title)
{
    return FdSetTitle(OWNER(This)->Dialog, title);
}

static HRESULT STDMETHODCALLTYPE d_set_ok_button_label(IFileDialog *This, LPCWSTR text)
{
    return FdSetOkLabel(OWNER(This)->Dialog, text);
}

static HRESULT STDMETHODCALLTYPE d_set_file_name_label(IFileDialog *This, LPCWSTR text)
{
    return FdSetNameLabel(OWNER(This)->Dialog, text);
}

/* the result is there once the dialog was accepted, and until it is shown again. */
static HRESULT STDMETHODCALLTYPE d_get_result(IFileDialog *This, IShellItem **item)
{
    ITEMDLG *d = OWNER(This);
    if (!item)
        return E_INVALIDARG;
    *item = nullptr;
    if (!d->Result)
        return E_UNEXPECTED;
    *item = d->Result;
    d->Result->lpVtbl->AddRef(d->Result);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_add_place(IFileDialog *This, IShellItem *place, FDAP position)
{
    WCHAR path[MAX_PATH];
    if (!place)
        return E_INVALIDARG;
    if (FAILED(path_of_item(place, path)))
        return S_OK;
    return FdAddPlace(OWNER(This)->Dialog, path, position == FDAP_TOP);
}

static HRESULT STDMETHODCALLTYPE d_set_default_extension(IFileDialog *This, LPCWSTR extension)
{
    return FdSetDefaultExtension(OWNER(This)->Dialog, extension);
}

/* ends the dialog, from any thread, and makes Show return the result given. */
static HRESULT STDMETHODCALLTYPE d_close(IFileDialog *This, HRESULT result)
{
    FdClose(OWNER(This)->Dialog, result);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_set_client_guid(IFileDialog *This, REFGUID guid)
{
    return FdSetClientGuid(OWNER(This)->Dialog, guid);
}

static HRESULT STDMETHODCALLTYPE d_clear_client_data(IFileDialog *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_set_filter(IFileDialog *This, IShellItemFilter *filter)
{
    ITEMDLG *d = OWNER(This);
    if (filter)
        filter->lpVtbl->AddRef(filter);
    if (d->Filter)
        d->Filter->lpVtbl->Release(d->Filter);
    d->Filter = filter;
    FdSetCustomFilter(d->Dialog, filter ? custom_filter : nullptr, d);
    return S_OK;
}

/* ------------------------------------------------------------------ IFileOpenDialog */

static HRESULT STDMETHODCALLTYPE d_get_results(IFileOpenDialog *This, IShellItemArray **items)
{
    ITEMDLG *d = OWNER(This);
    if (!items)
        return E_INVALIDARG;
    *items = nullptr;
    if (!d->Results)
        return E_FAIL;
    *items = d->Results;
    d->Results->lpVtbl->AddRef(d->Results);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_selected_items(IFileOpenDialog *This, IShellItemArray **items)
{
    ITEMDLG *d = OWNER(This);
    if (!items)
        return E_INVALIDARG;
    *items = nullptr;
    LPWSTR *paths;
    UINT count = FdGetSelection(d->Dialog, &paths);
    if (!count)
        return E_FAIL;
    HRESULT hr = array_from_paths(paths, count, items);
    FdFreePaths(paths, count);
    return hr;
}

/* ------------------------------------------------------------------ IFileSaveDialog */

/* an item to save over: its folder and its name. */
static HRESULT STDMETHODCALLTYPE d_set_save_as_item(IFileSaveDialog *This, IShellItem *item)
{
    ITEMDLG *d = OWNER(This);
    WCHAR path[MAX_PATH];
    if (!item)
        return E_INVALIDARG;
    HRESULT hr = path_of_item(item, path);
    if (FAILED(hr))
        return hr;
    FdSetFileName(d->Dialog, PathFindFileNameW(path));
    PathRemoveFileSpecW(path);
    return FdSetFolder(d->Dialog, path);
}

static HRESULT STDMETHODCALLTYPE d_set_properties(IFileSaveDialog *This, IPropertyStore *store)
{
    ITEMDLG *d = OWNER(This);
    if (store)
        store->lpVtbl->AddRef(store);
    if (d->Properties)
        d->Properties->lpVtbl->Release(d->Properties);
    d->Properties = store;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_set_collected_properties(IFileSaveDialog *This, IPropertyDescriptionList *list, BOOL append)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_properties(IFileSaveDialog *This, IPropertyStore **store)
{
    ITEMDLG *d = OWNER(This);
    if (!store)
        return E_INVALIDARG;
    *store = d->Properties;
    if (!*store)
        return E_FAIL;
    (*store)->lpVtbl->AddRef(*store);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_apply_properties(IFileSaveDialog *This, IShellItem *item, IPropertyStore *store, HWND owner,
                                  IFileOperationProgressSink *sink)
{
    return S_OK;
}

/* ------------------------------------------------------------------ IFileDialog2 */

static HRESULT STDMETHODCALLTYPE d2_set_cancel_button_label(IFileDialog2 *This, LPCWSTR text)
{
    return FdSetCancelLabel(OWNER(This)->Dialog, text);
}

static HRESULT STDMETHODCALLTYPE d2_set_navigation_root(IFileDialog2 *This, IShellItem *root)
{
    return S_OK;
}

/* ------------------------------------------------------------------ IFileDialogCustomize */

#define CUSTOM_DIALOG(This) (OWNER(This)->Dialog)

static HRESULT STDMETHODCALLTYPE c_enable_open_drop_down(IFileDialogCustomize *This, DWORD id)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_add_menu(IFileDialogCustomize *This, DWORD id, LPCWSTR label)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_BUTTON, id, label);
}

static HRESULT STDMETHODCALLTYPE c_add_push_button(IFileDialogCustomize *This, DWORD id, LPCWSTR label)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_BUTTON, id, label);
}

static HRESULT STDMETHODCALLTYPE c_add_combo_box(IFileDialogCustomize *This, DWORD id)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_COMBO, id, nullptr);
}

static HRESULT STDMETHODCALLTYPE c_add_radio_button_list(IFileDialogCustomize *This, DWORD id)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_RADIO, id, nullptr);
}

static HRESULT STDMETHODCALLTYPE c_add_check_button(IFileDialogCustomize *This, DWORD id, LPCWSTR label, BOOL checked)
{
    HRESULT hr = FdAddControl(CUSTOM_DIALOG(This), FDC_CHECK, id, label);
    if (SUCCEEDED(hr))
        FdSetControlChecked(CUSTOM_DIALOG(This), id, checked);
    return hr;
}

static HRESULT STDMETHODCALLTYPE c_add_edit_box(IFileDialogCustomize *This, DWORD id, LPCWSTR text)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_EDIT, id, text);
}

static HRESULT STDMETHODCALLTYPE c_add_separator(IFileDialogCustomize *This, DWORD id)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_SEPARATOR, id, nullptr);
}

static HRESULT STDMETHODCALLTYPE c_add_text(IFileDialogCustomize *This, DWORD id, LPCWSTR text)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_TEXT, id, text);
}

static HRESULT STDMETHODCALLTYPE c_set_control_label(IFileDialogCustomize *This, DWORD id, LPCWSTR label)
{
    return FdSetControlText(CUSTOM_DIALOG(This), id, 0, label, TRUE);
}

static HRESULT STDMETHODCALLTYPE c_get_control_state(IFileDialogCustomize *This, DWORD id, CDCONTROLSTATEF *state)
{
    int value = 0;
    HRESULT hr = FdGetControlState(CUSTOM_DIALOG(This), id, &value);
    if (state)
        *state = value;
    return hr;
}

static HRESULT STDMETHODCALLTYPE c_set_control_state(IFileDialogCustomize *This, DWORD id, CDCONTROLSTATEF state)
{
    return FdSetControlState(CUSTOM_DIALOG(This), id, state);
}

static HRESULT STDMETHODCALLTYPE c_get_edit_box_text(IFileDialogCustomize *This, DWORD id, WCHAR **text)
{
    return FdGetControlText(CUSTOM_DIALOG(This), id, text);
}

static HRESULT STDMETHODCALLTYPE c_set_edit_box_text(IFileDialogCustomize *This, DWORD id, LPCWSTR text)
{
    return FdSetControlText(CUSTOM_DIALOG(This), id, 0, text, FALSE) == S_OK ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE c_get_check_button_state(IFileDialogCustomize *This, DWORD id, BOOL *checked)
{
    return FdGetControlChecked(CUSTOM_DIALOG(This), id, checked);
}

static HRESULT STDMETHODCALLTYPE c_set_check_button_state(IFileDialogCustomize *This, DWORD id, BOOL checked)
{
    return FdSetControlChecked(CUSTOM_DIALOG(This), id, checked);
}

static HRESULT STDMETHODCALLTYPE c_add_control_item(IFileDialogCustomize *This, DWORD id, DWORD item, LPCWSTR label)
{
    return FdAddControlItem(CUSTOM_DIALOG(This), id, item, label);
}

static HRESULT STDMETHODCALLTYPE c_remove_control_item(IFileDialogCustomize *This, DWORD id, DWORD item)
{
    return FdRemoveControlItem(CUSTOM_DIALOG(This), id, item, FALSE);
}

static HRESULT STDMETHODCALLTYPE c_remove_all_control_items(IFileDialogCustomize *This, DWORD id)
{
    return FdRemoveControlItem(CUSTOM_DIALOG(This), id, 0, TRUE);
}

static HRESULT STDMETHODCALLTYPE c_get_control_item_state(IFileDialogCustomize *This, DWORD id, DWORD item, CDCONTROLSTATEF *state)
{
    if (state)
        *state = CDCS_ENABLEDVISIBLE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_set_control_item_state(IFileDialogCustomize *This, DWORD id, DWORD item, CDCONTROLSTATEF state)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_get_selected_control_item(IFileDialogCustomize *This, DWORD id, DWORD *item)
{
    return FdGetControlSelection(CUSTOM_DIALOG(This), id, item);
}

static HRESULT STDMETHODCALLTYPE c_set_selected_control_item(IFileDialogCustomize *This, DWORD id, DWORD item)
{
    return FdSetControlSelection(CUSTOM_DIALOG(This), id, item);
}

static HRESULT STDMETHODCALLTYPE c_start_visual_group(IFileDialogCustomize *This, DWORD id, LPCWSTR label)
{
    return FdAddControl(CUSTOM_DIALOG(This), FDC_GROUP, id, label);
}

static HRESULT STDMETHODCALLTYPE c_end_visual_group(IFileDialogCustomize *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_make_prominent(IFileDialogCustomize *This, DWORD id)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_set_control_item_text(IFileDialogCustomize *This, DWORD id, DWORD item, LPCWSTR text)
{
    return FdSetControlText(CUSTOM_DIALOG(This), id, item, text, FALSE);
}

/* ------------------------------------------------------------------ IObjectWithSite and IOleWindow */

static HRESULT STDMETHODCALLTYPE s_set_site(IObjectWithSite *This, IUnknown *site)
{
    ITEMDLG *d = OWNER(This);
    if (site)
        site->lpVtbl->AddRef(site);
    if (d->SiteObject)
        d->SiteObject->lpVtbl->Release(d->SiteObject);
    d->SiteObject = site;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_get_site(IObjectWithSite *This, REFIID riid, void **site)
{
    ITEMDLG *d = OWNER(This);
    if (!site)
        return E_POINTER;
    *site = nullptr;
    if (!d->SiteObject)
        return E_FAIL;
    return d->SiteObject->lpVtbl->QueryInterface(d->SiteObject, riid, site);
}

static HRESULT STDMETHODCALLTYPE w_get_window(IOleWindow *This, HWND *window)
{
    ITEMDLG *d = OWNER(This);
    if (!window)
        return E_POINTER;
    *window = FdWindow(d->Dialog);
    return *window ? S_OK : E_HANDLE;
}

static HRESULT STDMETHODCALLTYPE w_context_sensitive_help(IOleWindow *This, BOOL enter)
{
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ vtables */

#define BASE_METHODS(show)                                                                                         \
    {                                                                                                              \
        d_query_interface, d_add_ref, d_release, show, d_set_file_types, d_set_file_type_index,                    \
            d_get_file_type_index, d_advise, d_unadvise, d_set_options, d_get_options, d_set_default_folder,       \
            d_set_folder, d_get_folder, d_get_current_selection, d_set_file_name, d_get_file_name, d_set_title,    \
            d_set_ok_button_label, d_set_file_name_label, d_get_result, d_add_place, d_set_default_extension,      \
            d_close, d_set_client_guid, d_clear_client_data, d_set_filter                                          \
    }

static const IFileOpenDialogVtbl g_open_vtbl = {BASE_METHODS(d_show), d_get_results, d_get_selected_items};
static const IFileSaveDialogVtbl g_save_vtbl = {BASE_METHODS(d_show), d_set_save_as_item, d_set_properties,
                                                d_set_collected_properties, d_get_properties, d_apply_properties};
static const IFileDialog2Vtbl g_dialog2_vtbl = {BASE_METHODS(d_show), d2_set_cancel_button_label,
                                                d2_set_navigation_root};

static HRESULT STDMETHODCALLTYPE custom_query_interface(IFileDialogCustomize *This, REFIID riid, void **object)
{
    return d_query_interface((IFileDialog *)This, riid, object);
}

static ULONG STDMETHODCALLTYPE custom_add_ref(IFileDialogCustomize *This)
{
    return d_add_ref((IFileDialog *)This);
}

static ULONG STDMETHODCALLTYPE custom_release(IFileDialogCustomize *This)
{
    return d_release((IFileDialog *)This);
}

static const IFileDialogCustomizeVtbl g_customize_vtbl = {
    custom_query_interface, custom_add_ref, custom_release, c_enable_open_drop_down, c_add_menu, c_add_push_button,
    c_add_combo_box, c_add_radio_button_list, c_add_check_button, c_add_edit_box, c_add_separator, c_add_text,
    c_set_control_label, c_get_control_state, c_set_control_state, c_get_edit_box_text, c_set_edit_box_text,
    c_get_check_button_state, c_set_check_button_state, c_add_control_item, c_remove_control_item,
    c_remove_all_control_items, c_get_control_item_state, c_set_control_item_state, c_get_selected_control_item,
    c_set_selected_control_item, c_start_visual_group, c_end_visual_group, c_make_prominent, c_set_control_item_text,
};

static HRESULT STDMETHODCALLTYPE site_query_interface(IObjectWithSite *This, REFIID riid, void **object)
{
    return d_query_interface((IFileDialog *)This, riid, object);
}

static ULONG STDMETHODCALLTYPE site_add_ref(IObjectWithSite *This)
{
    return d_add_ref((IFileDialog *)This);
}

static ULONG STDMETHODCALLTYPE site_release(IObjectWithSite *This)
{
    return d_release((IFileDialog *)This);
}

static const IObjectWithSiteVtbl g_site_vtbl = {site_query_interface, site_add_ref, site_release, s_set_site, s_get_site};

static HRESULT STDMETHODCALLTYPE window_query_interface(IOleWindow *This, REFIID riid, void **object)
{
    return d_query_interface((IFileDialog *)This, riid, object);
}

static ULONG STDMETHODCALLTYPE window_add_ref(IOleWindow *This)
{
    return d_add_ref((IFileDialog *)This);
}

static ULONG STDMETHODCALLTYPE window_release(IOleWindow *This)
{
    return d_release((IFileDialog *)This);
}

static const IOleWindowVtbl g_window_vtbl = {window_query_interface, window_add_ref, window_release, w_get_window,
                                             w_context_sensitive_help};

/* ------------------------------------------------------------------ creating one */

HRESULT ItemDialogCreate(BOOL save, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    ITEMDLG *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->Dialog = FdCreate(save);
    if (!d->Dialog) {
        HeapFree(GetProcessHeap(), 0, d);
        return E_OUTOFMEMORY;
    }
    d->Save = save;
    d->Refs = 1;
    d->Main.Table = save ? (const void *)&g_save_vtbl : (const void *)&g_open_vtbl;
    d->Dialog2.Table = &g_dialog2_vtbl;
    d->Customize.Table = &g_customize_vtbl;
    d->Site.Table = &g_site_vtbl;
    d->Window.Table = &g_window_vtbl;
    d->Main.Owner = d->Dialog2.Owner = d->Customize.Owner = d->Site.Owner = d->Window.Owner = d;
    FdSetEvents(d->Dialog, &g_events, d);
    FdSetControlHandler(d->Dialog, event_control, d);
    HRESULT hr = d_query_interface(IFACE(d), riid, object);
    d_release(IFACE(d));
    return hr;
}
