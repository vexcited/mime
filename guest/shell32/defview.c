#include "shell32_private.h"
#include <mime/win32/commctrl.h>
#include <mime/win32/shldisp.h>
#include <mime/win32/shlwapi.h>
#include <mime/win32/shobjidl_ui.h>

#define VIEW_CLASS L"SHELLDLL_DefView"

typedef struct _VIEW {
    IShellView View;
    IFolderView FolderView;
    LONG Refs;
    IShellFolder *Folder;
    /* the folder itself from the desktop, when it tells. */
    PIDLIST_ABSOLUTE Location;
    IShellBrowser *Browser;
    ICommDlgBrowser *Dialog;
    HWND Window, List;
    FOLDERSETTINGS Settings;
    /* nothing is told to the browser while the list fills or goes away. */
    int Quiet;
} VIEW;

static VIEW *view_of_folder_view(IFolderView *This)
{
    return (VIEW *)((BYTE *)This - offsetof(VIEW, FolderView));
}

/* ------------------------------------------------------------------ the data object of a selection */

typedef struct _ITEMS_DATA {
    IDataObject Data;
    LONG Refs;
    PIDLIST_ABSOLUTE Folder;
    UINT Count;
    PITEMID_CHILD *Items;
} ITEMS_DATA;

static UINT list_format(void)
{
    static UINT format;
    if (!format)
        format = RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    return format;
}

static HRESULT STDMETHODCALLTYPE data_query_interface(IDataObject *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDataObject))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((ITEMS_DATA *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE data_add_ref(IDataObject *This)
{
    return (ULONG)InterlockedIncrement(&((ITEMS_DATA *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE data_release(IDataObject *This)
{
    ITEMS_DATA *d = (ITEMS_DATA *)This;
    LONG refs = InterlockedDecrement(&d->Refs);
    if (!refs) {
        for (UINT i = 0; i < d->Count; i++)
            ILFree(d->Items[i]);
        ShFree(d->Items);
        ILFree(d->Folder);
        ShFree(d);
    }
    return (ULONG)refs;
}

static HGLOBAL data_lists(const ITEMS_DATA *d)
{
    UINT header = sizeof(UINT) * (d->Count + 2);
    UINT size = header + ILGetSize(d->Folder);
    for (UINT i = 0; i < d->Count; i++)
        size += ILGetSize(d->Items[i]);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    CIDA *lists = global ? GlobalLock(global) : nullptr;
    if (!lists) {
        if (global)
            GlobalFree(global);
        return nullptr;
    }
    UINT offset = header;
    lists->cidl = d->Count;
    lists->aoffset[0] = offset;
    memcpy((BYTE *)lists + offset, d->Folder, ILGetSize(d->Folder));
    offset += ILGetSize(d->Folder);
    for (UINT i = 0; i < d->Count; i++) {
        lists->aoffset[i + 1] = offset;
        memcpy((BYTE *)lists + offset, d->Items[i], ILGetSize(d->Items[i]));
        offset += ILGetSize(d->Items[i]);
    }
    GlobalUnlock(global);
    return global;
}

/* the paths of the items as a list of dropped files, when every item has one. */
static HGLOBAL data_paths(const ITEMS_DATA *d)
{
    SIZE_T used = sizeof(DROPFILES), capacity = used + d->Count * MAX_PATH * sizeof(WCHAR) + sizeof(WCHAR);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, capacity);
    DROPFILES *files = global ? GlobalLock(global) : nullptr;
    if (!files) {
        if (global)
            GlobalFree(global);
        return nullptr;
    }
    files->pFiles = sizeof(DROPFILES);
    files->fWide = TRUE;
    BOOL ok = TRUE;
    for (UINT i = 0; ok && i < d->Count; i++) {
        WCHAR path[MAX_PATH];
        PIDLIST_ABSOLUTE full = ILCombine(d->Folder, d->Items[i]);
        ok = full && ShPathFromIdList(full, path, MAX_PATH);
        ILFree(full);
        if (ok) {
            SIZE_T bytes = (wcslen(path) + 1) * sizeof(WCHAR);
            memcpy((BYTE *)files + used, path, bytes);
            used += bytes;
        }
    }
    GlobalUnlock(global);
    if (!ok) {
        GlobalFree(global);
        return nullptr;
    }
    return global;
}

static HRESULT STDMETHODCALLTYPE data_query_get_data(IDataObject *This, FORMATETC *format)
{
    if (!format)
        return E_INVALIDARG;
    if (format->cfFormat != list_format() && format->cfFormat != CF_HDROP)
        return DV_E_FORMATETC;
    return (format->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
}

static HRESULT STDMETHODCALLTYPE data_get_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    ITEMS_DATA *d = (ITEMS_DATA *)This;
    if (!medium)
        return E_INVALIDARG;
    HRESULT hr = data_query_get_data(This, format);
    if (FAILED(hr))
        return hr;
    HGLOBAL global = format->cfFormat == CF_HDROP ? data_paths(d) : data_lists(d);
    if (!global)
        return format->cfFormat == CF_HDROP ? DV_E_FORMATETC : E_OUTOFMEMORY;
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    medium->pUnkForRelease = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE data_get_data_here(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE data_canonical(IDataObject *This, FORMATETC *in, FORMATETC *out)
{
    if (out)
        out->ptd = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE data_set_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium, BOOL release)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE data_enum_format(IDataObject *This, DWORD direction, IEnumFORMATETC **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    FORMATETC formats[] = {
        {(CLIPFORMAT)list_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
        {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
    };
    return SHCreateStdEnumFmtEtc(RTL_NUMBER_OF(formats), formats, enumerator);
}

static HRESULT STDMETHODCALLTYPE data_advise(IDataObject *This, FORMATETC *format, DWORD flags, IAdviseSink *sink,
                                             DWORD *connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE data_unadvise(IDataObject *This, DWORD connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE data_enum_advise(IDataObject *This, IEnumSTATDATA **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl g_data_vtbl = {
    data_query_interface, data_add_ref,  data_release,     data_get_data, data_get_data_here, data_query_get_data,
    data_canonical,       data_set_data, data_enum_format, data_advise,   data_unadvise,      data_enum_advise,
};

/* ------------------------------------------------------------------ the list */

static PCUITEMID_CHILD row_item(const VIEW *v, int row)
{
    LVITEMW item = {.mask = LVIF_PARAM, .iItem = row};
    if (!v->List || !SendMessageW(v->List, LVM_GETITEMW, 0, (LPARAM)&item))
        return nullptr;
    return (PCUITEMID_CHILD)item.lParam;
}

static int row_count(const VIEW *v)
{
    return v->List ? (int)SendMessageW(v->List, LVM_GETITEMCOUNT, 0, 0) : 0;
}

static int find_row(const VIEW *v, PCUITEMID_CHILD pidl)
{
    int count = row_count(v);
    UINT size = ILGetSize(pidl);
    for (int i = 0; i < count; i++) {
        PCUITEMID_CHILD row = row_item(v, i);
        if (row && ILGetSize(row) == size && !memcmp(row, pidl, size))
            return i;
    }
    for (int i = 0; i < count; i++) {
        PCUITEMID_CHILD row = row_item(v, i);
        if (row && (short)v->Folder->lpVtbl->CompareIDs(v->Folder, 0, row, pidl) == 0)
            return i;
    }
    return -1;
}

static SFGAOF row_attributes(const VIEW *v, PCUITEMID_CHILD pidl, SFGAOF mask)
{
    SFGAOF attributes = mask;
    if (FAILED(v->Folder->lpVtbl->GetAttributesOf(v->Folder, 1, &pidl, &attributes)))
        return 0;
    return attributes & mask;
}

static int row_image(const VIEW *v, PCUITEMID_CHILD pidl)
{
    SHFILEINFOW info = {};
    PIDLIST_ABSOLUTE full = v->Location ? ILCombine(v->Location, pidl) : nullptr;
    BOOL found = full && SHGetFileInfoW((LPCWSTR)full, 0, &info, sizeof(info),
                                        SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    ILFree(full);
    if (found)
        return info.iIcon;
    SH_ICON_SOURCE source;
    ShIconSourceStock(row_attributes(v, pidl, SFGAO_FOLDER) ? SIID_FOLDER : SIID_DOCNOASSOC, &source);
    return ShSystemIconIndex(&source, TRUE);
}

static void row_text(const VIEW *v, PCUITEMID_CHILD pidl, int column, LPWSTR text, int chars)
{
    text[0] = 0;
    if (column == 0) {
        LPWSTR name;
        if (SUCCEEDED(ShDisplayText(v->Folder, pidl, SHGDN_INFOLDER, &name))) {
            lstrcpynW(text, name, chars);
            ShFree(name);
        }
        return;
    }
    if (column == 2) {
        SHFILEINFOW info = {};
        PIDLIST_ABSOLUTE full = v->Location ? ILCombine(v->Location, pidl) : nullptr;
        if (full && SHGetFileInfoW((LPCWSTR)full, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_TYPENAME))
            lstrcpynW(text, info.szTypeName, chars);
        ILFree(full);
        return;
    }
    ULONGLONG size;
    FILETIME time;
    if (FAILED(ShItemDetails(v->Folder, pidl, &size, &time)))
        return;
    if (column == 3) {
        if (!row_attributes(v, pidl, SFGAO_FOLDER)) {
            WCHAR number[40];
            _snwprintf(number, RTL_NUMBER_OF(number), L"%llu KB", (size + 1023) / 1024);
            lstrcpynW(text, number, chars);
        }
        return;
    }
    FILETIME local;
    SYSTEMTIME moment;
    if ((!time.dwLowDateTime && !time.dwHighDateTime) || !FileTimeToLocalFileTime(&time, &local) ||
        !FileTimeToSystemTime(&local, &moment))
        return;
    WCHAR date[80], clock[80];
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &moment, nullptr, date, RTL_NUMBER_OF(date)) &&
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &moment, nullptr, clock, RTL_NUMBER_OF(clock))) {
        WCHAR both[170];
        _snwprintf(both, RTL_NUMBER_OF(both), L"%ls %ls", date, clock);
        lstrcpynW(text, both, chars);
    }
}

static int CALLBACK compare_rows(LPARAM a, LPARAM b, LPARAM context)
{
    VIEW *v = (VIEW *)context;
    HRESULT hr = v->Folder->lpVtbl->CompareIDs(v->Folder, 0, (PCUIDLIST_RELATIVE)a, (PCUIDLIST_RELATIVE)b);
    return FAILED(hr) ? 0 : (short)hr;
}

static void fill(VIEW *v)
{
    v->Quiet++;
    SendMessageW(v->List, WM_SETREDRAW, FALSE, 0);
    SendMessageW(v->List, LVM_DELETEALLITEMS, 0, 0);
    IEnumIDList *enumerator = nullptr;
    HRESULT hr =
        v->Folder->lpVtbl->EnumObjects(v->Folder, v->Window, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumerator);
    if (hr == S_OK && enumerator) {
        PITEMID_CHILD child;
        int row = 0;
        while (enumerator->lpVtbl->Next(enumerator, 1, &child, nullptr) == S_OK) {
            if (v->Dialog && v->Dialog->lpVtbl->IncludeObject(v->Dialog, &v->View, child) != S_OK) {
                ILFree(child);
                continue;
            }
            LVITEMW item = {.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM, .iItem = row};
            item.pszText = LPSTR_TEXTCALLBACKW;
            item.iImage = I_IMAGECALLBACK;
            item.lParam = (LPARAM)child;
            if (SendMessageW(v->List, LVM_INSERTITEMW, 0, (LPARAM)&item) < 0)
                ILFree(child);
            else
                row++;
        }
        enumerator->lpVtbl->Release(enumerator);
    }
    SendMessageW(v->List, LVM_SORTITEMS, (WPARAM)v, (LPARAM)compare_rows);
    SendMessageW(v->List, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(v->List, nullptr, TRUE);
    v->Quiet--;
}

static DWORD list_style(const FOLDERSETTINGS *settings)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_SHAREIMAGELISTS | LVS_SHOWSELALWAYS |
                  LVS_AUTOARRANGE;
    switch (settings->ViewMode) {
    case FVM_SMALLICON:
        style |= LVS_SMALLICON;
        break;
    case FVM_LIST:
        style |= LVS_LIST;
        break;
    case FVM_DETAILS:
        style |= LVS_REPORT;
        break;
    default:
        style |= LVS_ICON;
        break;
    }
    if (settings->fFlags & FWF_SINGLESEL)
        style |= LVS_SINGLESEL;
    if (settings->fFlags & FWF_ALIGNLEFT)
        style |= LVS_ALIGNLEFT;
    return style;
}

static BOOL create_list(VIEW *v)
{
    static const struct {
        LPCWSTR Title;
        int Width, Format;
    } columns[] = {
        {L"Name", 250, LVCFMT_LEFT},
        {L"Date modified", 130, LVCFMT_LEFT},
        {L"Type", 130, LVCFMT_LEFT},
        {L"Size", 80, LVCFMT_RIGHT},
    };
    /* the list class is registered by comctl32 when it loads. */
    LoadLibraryW(L"comctl32.dll");
    RECT client;
    GetClientRect(v->Window, &client);
    v->List = CreateWindowExW((v->Settings.fFlags & FWF_NOCLIENTEDGE) ? 0 : WS_EX_CLIENTEDGE, L"SysListView32", nullptr,
                              list_style(&v->Settings), 0, 0, client.right, client.bottom, v->Window, (HMENU)1,
                              g_shell32, nullptr);
    if (!v->List)
        return FALSE;
    SendMessageW(v->List, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)ShSystemList(SHIL_LARGE));
    SendMessageW(v->List, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ShSystemList(SHIL_SYSSMALL));
    UINT dpi = GetDpiForWindow(v->Window);
    if (!dpi)
        dpi = 96;
    for (int i = 0; i < (int)RTL_NUMBER_OF(columns); i++) {
        LVCOLUMNW column = {.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM};
        column.pszText = (LPWSTR)columns[i].Title;
        column.cx = MulDiv(columns[i].Width, (int)dpi, 96);
        column.fmt = columns[i].Format;
        column.iSubItem = i;
        SendMessageW(v->List, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&column);
    }
    fill(v);
    return TRUE;
}

static void state_change(VIEW *v, ULONG change)
{
    if (v->Dialog && !v->Quiet)
        v->Dialog->lpVtbl->OnStateChange(v->Dialog, &v->View, change);
}

/* the browser has the first word on an item. a folder it leaves alone is browsed into. */
static void default_command(VIEW *v)
{
    if (v->Dialog && v->Dialog->lpVtbl->OnDefaultCommand(v->Dialog, &v->View) == S_OK)
        return;
    int row = (int)SendMessageW(v->List, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    PCUITEMID_CHILD pidl = row >= 0 ? row_item(v, row) : nullptr;
    if (!pidl || !v->Browser || !row_attributes(v, pidl, SFGAO_FOLDER))
        return;
    PITEMID_CHILD copy = ILClone(pidl);
    if (!copy)
        return;
    /* the browser replaces this view, so it is held until the call is back. */
    v->View.lpVtbl->AddRef(&v->View);
    v->Browser->lpVtbl->BrowseObject(v->Browser, copy, SBSP_SAMEBROWSER | SBSP_RELATIVE);
    v->View.lpVtbl->Release(&v->View);
    ILFree(copy);
}

static LRESULT list_notify(VIEW *v, NMHDR *header)
{
    switch (header->code) {
    case LVN_GETDISPINFOW: {
        NMLVDISPINFOW *info = (NMLVDISPINFOW *)header;
        PCUITEMID_CHILD pidl = (PCUITEMID_CHILD)info->item.lParam;
        if (!pidl)
            pidl = row_item(v, info->item.iItem);
        if (!pidl)
            return 0;
        if ((info->item.mask & LVIF_TEXT) && info->item.pszText && info->item.cchTextMax > 0)
            row_text(v, pidl, info->item.iSubItem, info->item.pszText, info->item.cchTextMax);
        if ((info->item.mask & LVIF_IMAGE) && !info->item.iSubItem)
            info->item.iImage = row_image(v, pidl);
        if (!info->item.iSubItem)
            info->item.mask |= LVIF_DI_SETITEM;
        return 0;
    }
    case LVN_DELETEITEM:
        ILFree((PITEMID_CHILD)((NMLISTVIEW *)header)->lParam);
        return 0;
    case LVN_ITEMCHANGED: {
        NMLISTVIEW *change = (NMLISTVIEW *)header;
        if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED))
            state_change(v, CDBOSC_SELCHANGE);
        return 0;
    }
    case LVN_ITEMACTIVATE:
        default_command(v);
        return 0;
    case NM_SETFOCUS:
        if (v->Browser && !v->Quiet)
            v->Browser->lpVtbl->OnViewWindowActive(v->Browser, &v->View);
        state_change(v, CDBOSC_SETFOCUS);
        return 0;
    case NM_KILLFOCUS:
        state_change(v, CDBOSC_KILLFOCUS);
        return 0;
    }
    return 0;
}

static LRESULT CALLBACK view_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    VIEW *v = (VIEW *)GetWindowLongPtrW(window, 0);
    switch (message) {
    case WM_NCCREATE:
        v = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(window, 0, (LONG_PTR)v);
        v->Window = window;
        break;
    case WM_CREATE:
        return v && create_list(v) ? 0 : -1;
    case WM_SIZE:
        if (v && v->List)
            MoveWindow(v->List, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return 0;
    case WM_SETFOCUS:
        if (v && v->List)
            SetFocus(v->List);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NOTIFY:
        if (v && v->List && ((NMHDR *)lparam)->hwndFrom == v->List)
            return list_notify(v, (NMHDR *)lparam);
        break;
    case WM_DESTROY:
        if (v && v->List) {
            v->Quiet++;
            SendMessageW(v->List, LVM_DELETEALLITEMS, 0, 0);
        }
        return 0;
    case WM_NCDESTROY:
        if (v) {
            v->Window = v->List = nullptr;
            v->Quiet = 0;
            SetWindowLongPtrW(window, 0, 0);
        }
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void register_class(void)
{
    static BOOL done;
    if (done)
        return;
    done = TRUE;
    WNDCLASSW class = {};
    class.lpfnWndProc = view_proc;
    class.cbWndExtra = sizeof(void *);
    class.hInstance = g_shell32;
    class.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    class.lpszClassName = VIEW_CLASS;
    RegisterClassW(&class);
}

/* ------------------------------------------------------------------ IShellView */

static HRESULT STDMETHODCALLTYPE view_query_interface(IShellView *This, REFIID riid, void **object)
{
    VIEW *v = (VIEW *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IOleWindow) || IsEqualIID(riid, &IID_IShellView))
        *object = &v->View;
    else if (IsEqualIID(riid, &IID_IFolderView))
        *object = &v->FolderView;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&v->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE view_add_ref(IShellView *This)
{
    return (ULONG)InterlockedIncrement(&((VIEW *)This)->Refs);
}

static void drop_browser(VIEW *v)
{
    if (v->Dialog)
        v->Dialog->lpVtbl->Release(v->Dialog);
    if (v->Browser)
        v->Browser->lpVtbl->Release(v->Browser);
    v->Dialog = nullptr;
    v->Browser = nullptr;
}

static ULONG STDMETHODCALLTYPE view_release(IShellView *This)
{
    VIEW *v = (VIEW *)This;
    LONG refs = InterlockedDecrement(&v->Refs);
    if (!refs) {
        /* the window goes with the view, and nothing it says on its way reaches a browser. */
        v->Refs = 1;
        drop_browser(v);
        if (v->Window)
            DestroyWindow(v->Window);
        v->Folder->lpVtbl->Release(v->Folder);
        ILFree(v->Location);
        ShFree(v);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE view_get_window(IShellView *This, HWND *window)
{
    if (!window)
        return E_POINTER;
    *window = ((VIEW *)This)->Window;
    return *window ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE view_help(IShellView *This, BOOL enter)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE view_translate(IShellView *This, MSG *message)
{
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE view_enable_modeless(IShellView *This, BOOL enable)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE view_ui_activate(IShellView *This, UINT state)
{
    VIEW *v = (VIEW *)This;
    if (state == SVUIA_ACTIVATE_FOCUS && v->List)
        SetFocus(v->List);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_refresh(IShellView *This)
{
    VIEW *v = (VIEW *)This;
    if (!v->List)
        return E_FAIL;
    fill(v);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_create_window(IShellView *This, IShellView *previous, const void *settings,
                                                    IShellBrowser *browser, RECT *bounds, HWND *window)
{
    VIEW *v = (VIEW *)This;
    if (!window)
        return E_POINTER;
    *window = nullptr;
    if (!browser || !bounds || v->Window)
        return E_UNEXPECTED;
    HWND parent = nullptr;
    if (FAILED(browser->lpVtbl->GetWindow(browser, &parent)) || !parent)
        return E_FAIL;
    if (settings)
        v->Settings = *(const FOLDERSETTINGS *)settings;
    drop_browser(v);
    v->Browser = browser;
    browser->lpVtbl->AddRef(browser);
    if (FAILED(browser->lpVtbl->QueryInterface(browser, &IID_ICommDlgBrowser, (void **)&v->Dialog)))
        v->Dialog = nullptr;
    register_class();
    CreateWindowExW(0, VIEW_CLASS, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP, bounds->left,
                    bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top, parent, nullptr, g_shell32,
                    v);
    if (!v->Window) {
        drop_browser(v);
        return E_FAIL;
    }
    SetWindowPos(v->Window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    *window = v->Window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_destroy_window(IShellView *This)
{
    VIEW *v = (VIEW *)This;
    view_add_ref(This);
    if (v->Window) {
        v->Quiet++;
        DestroyWindow(v->Window);
    }
    drop_browser(v);
    view_release(This);
    return S_OK;
}

static UINT current_mode(const VIEW *v)
{
    if (!v->List)
        return v->Settings.ViewMode;
    switch (SendMessageW(v->List, LVM_GETVIEW, 0, 0)) {
    case LV_VIEW_DETAILS:
        return FVM_DETAILS;
    case LV_VIEW_SMALLICON:
        return FVM_SMALLICON;
    case LV_VIEW_LIST:
        return FVM_LIST;
    case LV_VIEW_TILE:
        return FVM_TILE;
    }
    return v->Settings.ViewMode == FVM_THUMBNAIL ? FVM_THUMBNAIL : FVM_ICON;
}

static HRESULT STDMETHODCALLTYPE view_get_current_info(IShellView *This, void *settings)
{
    VIEW *v = (VIEW *)This;
    if (!settings)
        return E_POINTER;
    FOLDERSETTINGS *out = settings;
    *out = v->Settings;
    out->ViewMode = current_mode(v);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_add_pages(IShellView *This, DWORD reserved, void *add_page, LPARAM lparam)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE view_save_state(IShellView *This)
{
    return S_OK;
}

static void select_row(VIEW *v, int row, UINT flags)
{
    LVITEMW item = {.stateMask = LVIS_SELECTED};
    if (flags & SVSI_DESELECTOTHERS)
        SendMessageW(v->List, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
    if (row < 0)
        return;
    item.state = (flags & SVSI_SELECT) ? LVIS_SELECTED : 0;
    if (flags & SVSI_FOCUSED) {
        item.stateMask |= LVIS_FOCUSED;
        item.state |= LVIS_FOCUSED;
    }
    SendMessageW(v->List, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&item);
    if (flags & SVSI_ENSUREVISIBLE)
        SendMessageW(v->List, LVM_ENSUREVISIBLE, (WPARAM)row, FALSE);
}

static HRESULT STDMETHODCALLTYPE view_select_item(IShellView *This, PCUITEMID_CHILD pidl, UINT flags)
{
    VIEW *v = (VIEW *)This;
    if (!v->List)
        return E_FAIL;
    if (!pidl) {
        select_row(v, -1, flags);
        return S_OK;
    }
    int row = find_row(v, pidl);
    if (row < 0)
        return E_INVALIDARG;
    select_row(v, row, flags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_get_item_object(IShellView *This, UINT what, REFIID riid, void **object)
{
    VIEW *v = (VIEW *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    UINT type = what & SVGIO_TYPE_MASK;
    if (type != SVGIO_SELECTION && type != SVGIO_ALLVIEW)
        return E_NOINTERFACE;
    if (!IsEqualIID(riid, &IID_IDataObject) && !IsEqualIID(riid, &IID_IUnknown))
        return E_NOINTERFACE;
    if (!v->List || !v->Location)
        return E_FAIL;
    int total = row_count(v);
    ITEMS_DATA *d = ShAllocZero(sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->Data.lpVtbl = &g_data_vtbl;
    d->Refs = 1;
    d->Folder = ILClone(v->Location);
    d->Items = ShAllocZero((total ? total : 1) * sizeof(*d->Items));
    BOOL ok = d->Folder && d->Items;
    for (int row = -1; ok;) {
        row = (int)SendMessageW(v->List, LVM_GETNEXTITEM, (WPARAM)row,
                                type == SVGIO_SELECTION ? LVNI_SELECTED : LVNI_ALL);
        if (row < 0)
            break;
        PCUITEMID_CHILD pidl = row_item(v, row);
        if (!pidl || (int)d->Count >= total)
            continue;
        d->Items[d->Count] = ILClone(pidl);
        ok = d->Items[d->Count] != nullptr;
        d->Count += ok;
    }
    HRESULT hr = !ok ? E_OUTOFMEMORY : !d->Count ? E_FAIL : S_OK;
    if (SUCCEEDED(hr))
        *object = &d->Data;
    else
        data_release(&d->Data);
    return hr;
}

static const IShellViewVtbl g_view_vtbl = {
    view_query_interface, view_add_ref,          view_release,     view_get_window, view_help,
    view_translate,       view_enable_modeless,  view_ui_activate, view_refresh,    view_create_window,
    view_destroy_window,  view_get_current_info, view_add_pages,   view_save_state, view_select_item,
    view_get_item_object,
};

/* ------------------------------------------------------------------ IFolderView */

static HRESULT STDMETHODCALLTYPE fv_query_interface(IFolderView *This, REFIID riid, void **object)
{
    return view_query_interface(&view_of_folder_view(This)->View, riid, object);
}

static ULONG STDMETHODCALLTYPE fv_add_ref(IFolderView *This)
{
    return view_add_ref(&view_of_folder_view(This)->View);
}

static ULONG STDMETHODCALLTYPE fv_release(IFolderView *This)
{
    return view_release(&view_of_folder_view(This)->View);
}

static HRESULT STDMETHODCALLTYPE fv_get_mode(IFolderView *This, UINT *mode)
{
    if (!mode)
        return E_POINTER;
    *mode = current_mode(view_of_folder_view(This));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_set_mode(IFolderView *This, UINT mode)
{
    VIEW *v = view_of_folder_view(This);
    static const int views[] = {LV_VIEW_ICON,    LV_VIEW_ICON, LV_VIEW_SMALLICON, LV_VIEW_LIST,
                                LV_VIEW_DETAILS, LV_VIEW_ICON, LV_VIEW_TILE};
    if (mode < FVM_ICON || mode > FVM_TILE)
        return E_INVALIDARG;
    v->Settings.ViewMode = mode;
    if (v->List)
        SendMessageW(v->List, LVM_SETVIEW, (WPARAM)views[mode], 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_get_folder(IFolderView *This, REFIID riid, void **folder)
{
    VIEW *v = view_of_folder_view(This);
    return v->Folder->lpVtbl->QueryInterface(v->Folder, riid, folder);
}

static HRESULT STDMETHODCALLTYPE fv_item(IFolderView *This, int index, PITEMID_CHILD *pidl)
{
    if (!pidl)
        return E_POINTER;
    PCUITEMID_CHILD row = row_item(view_of_folder_view(This), index);
    *pidl = row ? ILClone(row) : nullptr;
    return *pidl ? S_OK : row ? E_OUTOFMEMORY : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE fv_item_count(IFolderView *This, UINT flags, int *count)
{
    VIEW *v = view_of_folder_view(This);
    if (!count)
        return E_POINTER;
    *count = 0;
    if (!v->List)
        return E_FAIL;
    *count = (flags & SVGIO_TYPE_MASK) == SVGIO_SELECTION ? (int)SendMessageW(v->List, LVM_GETSELECTEDCOUNT, 0, 0)
                                                          : row_count(v);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_items(IFolderView *This, UINT flags, REFIID riid, void **items)
{
    VIEW *v = view_of_folder_view(This);
    return view_get_item_object(&v->View, flags, riid, items);
}

static HRESULT fv_next(IFolderView *This, WPARAM message, LPARAM flags, int *index)
{
    VIEW *v = view_of_folder_view(This);
    if (!index)
        return E_POINTER;
    *index = -1;
    if (!v->List)
        return E_FAIL;
    *index = (int)SendMessageW(v->List, (UINT)message, message == LVM_GETNEXTITEM ? (WPARAM)-1 : 0, flags);
    return *index < 0 ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_selection_mark(IFolderView *This, int *index)
{
    return fv_next(This, LVM_GETSELECTIONMARK, 0, index);
}

static HRESULT STDMETHODCALLTYPE fv_focused(IFolderView *This, int *index)
{
    return fv_next(This, LVM_GETNEXTITEM, LVNI_FOCUSED, index);
}

static HRESULT STDMETHODCALLTYPE fv_item_position(IFolderView *This, PCUITEMID_CHILD pidl, POINT *point)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fv_spacing(IFolderView *This, POINT *spacing)
{
    VIEW *v = view_of_folder_view(This);
    if (!spacing)
        return S_OK;
    if (!v->List)
        return E_FAIL;
    DWORD value = (DWORD)SendMessageW(v->List, LVM_GETITEMSPACING, current_mode(v) != FVM_ICON, 0);
    spacing->x = LOWORD(value);
    spacing->y = HIWORD(value);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_auto_arrange(IFolderView *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_select_item(IFolderView *This, int index, DWORD flags)
{
    VIEW *v = view_of_folder_view(This);
    if (!v->List)
        return E_FAIL;
    if (index < -1 || index >= row_count(v))
        return E_INVALIDARG;
    select_row(v, index, flags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fv_select_and_position(IFolderView *This, UINT count, PCUITEMID_CHILD *items,
                                                        POINT *points, DWORD flags)
{
    return E_NOTIMPL;
}

static const IFolderViewVtbl g_folder_view_vtbl = {
    fv_query_interface,
    fv_add_ref,
    fv_release,
    fv_get_mode,
    fv_set_mode,
    fv_get_folder,
    fv_item,
    fv_item_count,
    fv_items,
    fv_selection_mark,
    fv_focused,
    fv_item_position,
    fv_spacing,
    fv_spacing,
    fv_auto_arrange,
    fv_select_item,
    fv_select_and_position,
};

HRESULT ShCreateView(IShellFolder *folder, REFIID riid, void **object)
{
    VIEW *v = ShAllocZero(sizeof(*v));
    if (!v)
        return E_OUTOFMEMORY;
    v->View.lpVtbl = &g_view_vtbl;
    v->FolderView.lpVtbl = &g_folder_view_vtbl;
    v->Refs = 1;
    v->Folder = folder;
    v->Settings.ViewMode = FVM_LIST;
    folder->lpVtbl->AddRef(folder);
    IPersistFolder2 *persist;
    if (SUCCEEDED(folder->lpVtbl->QueryInterface(folder, &IID_IPersistFolder2, (void **)&persist))) {
        if (FAILED(persist->lpVtbl->GetCurFolder(persist, &v->Location)))
            v->Location = nullptr;
        persist->lpVtbl->Release(persist);
    }
    HRESULT hr = view_query_interface(&v->View, riid, object);
    view_release(&v->View);
    return hr;
}
