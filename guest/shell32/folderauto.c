/*
 * the folders of the shell for scripts: the Folder that Shell.Application's NameSpace gives, its FolderItems and the
 * FolderItem of each. All three are dual interfaces over IShellFolder, so a file system directory, a zip file and any
 * other folder class come out the same. They answer IDispatch from the type library of this dll, and have their
 * methods at the places windows has them since programs call them through the vtable too.
 */
#include "shell32_private.h"
#include <mime/win32/shldisp.h>

static LONG g_folder_objects;

/* ------------------------------------------------------------------ helpers */

#define display_text ShDisplayText

static HRESULT text_bstr(LPCWSTR text, BSTR *bstr)
{
    *bstr = SysAllocString(text);
    return *bstr ? S_OK : E_OUTOFMEMORY;
}

static SFGAOF attributes_of(IShellFolder *shell, PCUITEMID_CHILD child, SFGAOF mask)
{
    SFGAOF attributes = mask;
    if (FAILED(shell->lpVtbl->GetAttributesOf(shell, 1, &child, &attributes)))
        return 0;
    return attributes & mask;
}

static HRESULT list_count(PCIDLIST_ABSOLUTE pidl, UINT *count)
{
    *count = 0;
    for (PCUIDLIST_RELATIVE p = pidl; p && p->mkid.cb; p = ILGetNext(p))
        (*count)++;
    return S_OK;
}

/* the folder an absolute list is in, and the last item of the list. */
static HRESULT bind_parent(PCIDLIST_ABSOLUTE pidl, IShellFolder **parent, PITEMID_CHILD *child)
{
    *parent = nullptr;
    *child = nullptr;
    UINT count;
    list_count(pidl, &count);
    if (!count)
        return E_FAIL;
    PIDLIST_ABSOLUTE up = ILClone(pidl);
    if (!up)
        return E_OUTOFMEMORY;
    ILRemoveLastID(up);
    HRESULT hr = ShBindAbsolute(up, &IID_IShellFolder, (void **)parent);
    ILFree(up);
    if (FAILED(hr))
        return hr;
    *child = ILClone(ILFindLastID(pidl));
    if (!*child) {
        (*parent)->lpVtbl->Release(*parent);
        *parent = nullptr;
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

static BOOL variant_number(const VARIANT *value, LONG *number)
{
    value = ShPlainVariant(value);
    if (!value || value->vt == VT_EMPTY || value->vt == VT_NULL || value->vt == VT_ERROR)
        return FALSE;
    VARIANT converted;
    VariantInit(&converted);
    if (FAILED(VariantChangeType(&converted, (VARIANT *)value, 0, VT_I4)))
        return FALSE;
    *number = converted.lVal;
    return TRUE;
}

/* ------------------------------------------------------------------ dual interface boilerplate */

#define DISPATCH_FUNCTIONS(name, Type, iid)                                                                            \
    static HRESULT STDMETHODCALLTYPE name##_get_type_info_count(Type *This, UINT *count)                               \
    {                                                                                                                  \
        if (!count)                                                                                                    \
            return E_POINTER;                                                                                          \
        *count = 1;                                                                                                    \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE name##_get_type_info(Type *This, UINT index, LCID lcid, ITypeInfo **info)         \
    {                                                                                                                  \
        if (!info)                                                                                                     \
            return E_POINTER;                                                                                          \
        if (index)                                                                                                     \
            return DISP_E_BADINDEX;                                                                                    \
        return ShTypeInfo(iid, info);                                                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE name##_get_ids_of_names(Type *This, REFIID riid, LPOLESTR *names, UINT count,    \
                                                             LCID lcid, DISPID *ids)                                   \
    {                                                                                                                  \
        ITypeInfo *info;                                                                                               \
        HRESULT hr = ShTypeInfo(iid, &info);                                                                           \
        if (FAILED(hr))                                                                                                \
            return hr;                                                                                                 \
        hr = DispGetIDsOfNames(info, names, count, ids);                                                               \
        info->lpVtbl->Release(info);                                                                                   \
        return hr;                                                                                                     \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE name##_invoke(Type *This, DISPID id, REFIID riid, LCID lcid, WORD flags,          \
                                                   DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception,          \
                                                   UINT *argument)                                                     \
    {                                                                                                                  \
        ITypeInfo *info;                                                                                               \
        HRESULT hr = ShTypeInfo(iid, &info);                                                                           \
        if (FAILED(hr))                                                                                                \
            return hr;                                                                                                 \
        hr = DispInvoke(This, info, id, flags, params, result, exception, argument);                                   \
        info->lpVtbl->Release(info);                                                                                   \
        return hr;                                                                                                     \
    }

static HRESULT application(IDispatch **object)
{
    if (!object)
        return E_INVALIDARG;
    return ShCreateApplication(&IID_IDispatch, (void **)object);
}

/* ------------------------------------------------------------------ objects */

typedef struct _FOLDER_OBJECT {
    Folder Iface;
    LONG Refs;
    IShellFolder *Shell;
    PIDLIST_ABSOLUTE Pidl;
} FOLDER_OBJECT;

typedef struct _ITEMS_OBJECT {
    FolderItems Iface;
    LONG Refs;
    IShellFolder *Shell;
    PIDLIST_ABSOLUTE Pidl;
    PITEMID_CHILD *Children;
    ULONG Count;
} ITEMS_OBJECT;

typedef struct _ITEM_OBJECT {
    FolderItem Iface;
    LONG Refs;
    IShellFolder *Shell;
    PIDLIST_ABSOLUTE Pidl;
    PITEMID_CHILD Child;
} ITEM_OBJECT;

typedef struct _ITEMS_ENUM {
    IEnumVARIANT Iface;
    LONG Refs;
    ITEMS_OBJECT *Items;
    ULONG Position;
} ITEMS_ENUM;

static const FolderVtbl g_folder_vtbl;
static const FolderItemsVtbl g_items_vtbl;
static const FolderItemVtbl g_item_vtbl;
static const IEnumVARIANTVtbl g_enum_vtbl;

/* the folder takes the reference to the shell folder. */
static HRESULT make_folder_object(IShellFolder *shell, PCIDLIST_ABSOLUTE pidl, IDispatch **object)
{
    FOLDER_OBJECT *folder = ShAllocZero(sizeof(*folder));
    if (folder)
        folder->Pidl = ILClone(pidl);
    if (!folder || !folder->Pidl) {
        ShFree(folder);
        shell->lpVtbl->Release(shell);
        return E_OUTOFMEMORY;
    }
    folder->Iface.lpVtbl = &g_folder_vtbl;
    folder->Refs = 1;
    folder->Shell = shell;
    InterlockedIncrement(&g_folder_objects);
    *object = (IDispatch *)&folder->Iface;
    return S_OK;
}

/* the item takes the reference to the shell folder and the child list. */
static HRESULT make_item_object(IShellFolder *shell, PCIDLIST_ABSOLUTE pidl, PITEMID_CHILD child, IDispatch **object)
{
    ITEM_OBJECT *item = ShAllocZero(sizeof(*item));
    if (item)
        item->Pidl = ILClone(pidl);
    if (!item || !item->Pidl) {
        ShFree(item);
        shell->lpVtbl->Release(shell);
        ILFree(child);
        return E_OUTOFMEMORY;
    }
    item->Iface.lpVtbl = &g_item_vtbl;
    item->Refs = 1;
    item->Shell = shell;
    item->Child = child;
    InterlockedIncrement(&g_folder_objects);
    *object = (IDispatch *)&item->Iface;
    return S_OK;
}

static HRESULT make_items_object(FOLDER_OBJECT *folder, ITEMS_OBJECT **object)
{
    ITEMS_OBJECT *items = ShAllocZero(sizeof(*items));
    if (!items)
        return E_OUTOFMEMORY;
    items->Pidl = ILClone(folder->Pidl);
    if (!items->Pidl) {
        ShFree(items);
        return E_OUTOFMEMORY;
    }
    items->Iface.lpVtbl = &g_items_vtbl;
    items->Refs = 1;
    items->Shell = folder->Shell;
    items->Shell->lpVtbl->AddRef(items->Shell);
    InterlockedIncrement(&g_folder_objects);
    IEnumIDList *list;
    HRESULT hr = items->Shell->lpVtbl->EnumObjects(items->Shell, nullptr,
                                                   SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &list);
    if (SUCCEEDED(hr)) {
        ULONG capacity = 0;
        PITEMID_CHILD child;
        ULONG fetched;
        while (list->lpVtbl->Next(list, 1, &child, &fetched) == S_OK && fetched) {
            if (items->Count == capacity) {
                ULONG grown = capacity ? capacity * 2 : 64;
                PITEMID_CHILD *larger = items->Children ? HeapReAlloc(GetProcessHeap(), 0, items->Children,
                                                                      grown * sizeof(*larger))
                                                        : ShAlloc(grown * sizeof(*larger));
                if (!larger) {
                    ILFree(child);
                    hr = E_OUTOFMEMORY;
                    break;
                }
                items->Children = larger;
                capacity = grown;
            }
            items->Children[items->Count++] = child;
        }
        list->lpVtbl->Release(list);
    }
    if (FAILED(hr)) {
        items->Iface.lpVtbl->Release(&items->Iface);
        return hr;
    }
    *object = items;
    return S_OK;
}

/* ------------------------------------------------------------------ FolderItem */

static ITEM_OBJECT *item_of(FolderItem *This)
{
    return (ITEM_OBJECT *)This;
}

static HRESULT STDMETHODCALLTYPE item_query_interface(FolderItem *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_FolderItem))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&item_of(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE item_add_ref(FolderItem *This)
{
    return (ULONG)InterlockedIncrement(&item_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE item_release(FolderItem *This)
{
    ITEM_OBJECT *item = item_of(This);
    LONG refs = InterlockedDecrement(&item->Refs);
    if (!refs) {
        item->Shell->lpVtbl->Release(item->Shell);
        ILFree(item->Pidl);
        ILFree(item->Child);
        InterlockedDecrement(&g_folder_objects);
        ShFree(item);
    }
    return (ULONG)refs;
}

DISPATCH_FUNCTIONS(item, FolderItem, &IID_FolderItem)

static HRESULT STDMETHODCALLTYPE item_get_application(FolderItem *This, IDispatch **object)
{
    return application(object);
}

/* the folder the item is in. */
static HRESULT STDMETHODCALLTYPE item_get_parent(FolderItem *This, IDispatch **parent)
{
    ITEM_OBJECT *item = item_of(This);
    if (!parent)
        return E_POINTER;
    item->Shell->lpVtbl->AddRef(item->Shell);
    return make_folder_object(item->Shell, item->Pidl, parent);
}

static HRESULT STDMETHODCALLTYPE item_get_name(FolderItem *This, BSTR *name)
{
    ITEM_OBJECT *item = item_of(This);
    LPWSTR text;
    if (!name)
        return E_POINTER;
    *name = nullptr;
    HRESULT hr = display_text(item->Shell, item->Child, SHGDN_INFOLDER, &text);
    if (FAILED(hr))
        return hr;
    hr = text_bstr(text, name);
    ShFree(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE item_put_name(FolderItem *This, BSTR name)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE item_get_path(FolderItem *This, BSTR *path)
{
    ITEM_OBJECT *item = item_of(This);
    LPWSTR text;
    if (!path)
        return E_POINTER;
    *path = nullptr;
    HRESULT hr = display_text(item->Shell, item->Child, SHGDN_FORPARSING, &text);
    if (FAILED(hr))
        return hr;
    hr = text_bstr(text, path);
    ShFree(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE item_get_link(FolderItem *This, IDispatch **link)
{
    if (link)
        *link = nullptr;
    return E_NOTIMPL;
}

/* the folder an item is, a directory or a file that is a folder of its class. */
static HRESULT STDMETHODCALLTYPE item_get_folder(FolderItem *This, IDispatch **folder)
{
    ITEM_OBJECT *item = item_of(This);
    if (!folder)
        return E_POINTER;
    *folder = nullptr;
    if (!(attributes_of(item->Shell, item->Child, SFGAO_FOLDER) & SFGAO_FOLDER))
        return S_OK;
    IShellFolder *shell;
    HRESULT hr = item->Shell->lpVtbl->BindToObject(item->Shell, item->Child, nullptr, &IID_IShellFolder,
                                                   (void **)&shell);
    if (FAILED(hr))
        return S_OK;
    PIDLIST_ABSOLUTE pidl = ILCombine(item->Pidl, item->Child);
    if (!pidl) {
        shell->lpVtbl->Release(shell);
        return E_OUTOFMEMORY;
    }
    hr = make_folder_object(shell, pidl, folder);
    ILFree(pidl);
    return hr;
}

static HRESULT item_flag(FolderItem *This, SFGAOF flag, VARIANT_BOOL *value)
{
    ITEM_OBJECT *item = item_of(This);
    if (!value)
        return E_POINTER;
    *value = attributes_of(item->Shell, item->Child, flag) & flag ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE item_get_is_link(FolderItem *This, VARIANT_BOOL *value)
{
    ITEM_OBJECT *item = item_of(This);
    LPWSTR name;
    if (!value)
        return E_POINTER;
    *value = VARIANT_FALSE;
    if (SUCCEEDED(display_text(item->Shell, item->Child, SHGDN_INFOLDER, &name))) {
        SIZE_T length = wcslen(name);
        if (length > 4 && !_wcsicmp(name + length - 4, L".lnk"))
            *value = VARIANT_TRUE;
        ShFree(name);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE item_get_is_folder(FolderItem *This, VARIANT_BOOL *value)
{
    return item_flag(This, SFGAO_FOLDER, value);
}

static HRESULT STDMETHODCALLTYPE item_get_is_file_system(FolderItem *This, VARIANT_BOOL *value)
{
    return item_flag(This, SFGAO_FILESYSTEM, value);
}

static HRESULT STDMETHODCALLTYPE item_get_is_browsable(FolderItem *This, VARIANT_BOOL *value)
{
    return item_flag(This, SFGAO_FOLDER, value);
}

static HRESULT STDMETHODCALLTYPE item_get_modify_date(FolderItem *This, DATE *date)
{
    ITEM_OBJECT *item = item_of(This);
    ULONGLONG size;
    FILETIME time, local;
    SYSTEMTIME system;
    if (!date)
        return E_POINTER;
    *date = 0;
    HRESULT hr = ShItemDetails(item->Shell, item->Child, &size, &time);
    if (FAILED(hr))
        return hr;
    if (!FileTimeToLocalFileTime(&time, &local) || !FileTimeToSystemTime(&local, &system) ||
        !SystemTimeToVariantTime(&system, date))
        *date = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE item_put_modify_date(FolderItem *This, DATE date)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE item_get_size(FolderItem *This, LONG *size)
{
    ITEM_OBJECT *item = item_of(This);
    ULONGLONG bytes;
    FILETIME time;
    if (!size)
        return E_POINTER;
    *size = 0;
    HRESULT hr = ShItemDetails(item->Shell, item->Child, &bytes, &time);
    if (FAILED(hr))
        return hr;
    *size = bytes > 0x7FFFFFFF ? 0x7FFFFFFF : (LONG)bytes;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE item_get_type(FolderItem *This, BSTR *type)
{
    ITEM_OBJECT *item = item_of(This);
    LPWSTR name;
    if (!type)
        return E_POINTER;
    *type = nullptr;
    HRESULT hr = display_text(item->Shell, item->Child, SHGDN_INFOLDER, &name);
    if (FAILED(hr))
        return hr;
    SHFILEINFOW info = {};
    BOOL folder = (attributes_of(item->Shell, item->Child, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
    SHGetFileInfoW(name, folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                   SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES);
    ShFree(name);
    return text_bstr(info.szTypeName, type);
}

static HRESULT STDMETHODCALLTYPE item_verbs(FolderItem *This, IDispatch **verbs)
{
    if (verbs)
        *verbs = nullptr;
    return E_NOTIMPL;
}

/* a verb runs on the file of a file system item, the default one without a name. */
static HRESULT STDMETHODCALLTYPE item_invoke_verb(FolderItem *This, VARIANT verb)
{
    ITEM_OBJECT *item = item_of(This);
    LPWSTR path;
    if (!(attributes_of(item->Shell, item->Child, SFGAO_FILESYSTEM) & SFGAO_FILESYSTEM))
        return E_NOTIMPL;
    HRESULT hr = display_text(item->Shell, item->Child, SHGDN_FORPARSING, &path);
    if (FAILED(hr))
        return hr;
    LPWSTR name = ShVariantText(&verb);
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.lpFile = path;
    info.lpVerb = name && name[0] ? name : nullptr;
    info.nShow = SW_SHOWNORMAL;
    hr = ShellExecuteExW(&info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    ShFree(name);
    ShFree(path);
    return hr;
}

static const FolderItemVtbl g_item_vtbl = {
    item_query_interface,
    item_add_ref,
    item_release,
    item_get_type_info_count,
    item_get_type_info,
    item_get_ids_of_names,
    item_invoke,
    item_get_application,
    item_get_parent,
    item_get_name,
    item_put_name,
    item_get_path,
    item_get_link,
    item_get_folder,
    item_get_is_link,
    item_get_is_folder,
    item_get_is_file_system,
    item_get_is_browsable,
    item_get_modify_date,
    item_put_modify_date,
    item_get_size,
    item_get_type,
    item_verbs,
    item_invoke_verb,
};

/* ------------------------------------------------------------------ FolderItems */

static ITEMS_OBJECT *items_of(FolderItems *This)
{
    return (ITEMS_OBJECT *)This;
}

static HRESULT STDMETHODCALLTYPE items_query_interface(FolderItems *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_FolderItems))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&items_of(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE items_add_ref(FolderItems *This)
{
    return (ULONG)InterlockedIncrement(&items_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE items_release(FolderItems *This)
{
    ITEMS_OBJECT *items = items_of(This);
    LONG refs = InterlockedDecrement(&items->Refs);
    if (!refs) {
        for (ULONG i = 0; i < items->Count; i++)
            ILFree(items->Children[i]);
        ShFree(items->Children);
        items->Shell->lpVtbl->Release(items->Shell);
        ILFree(items->Pidl);
        InterlockedDecrement(&g_folder_objects);
        ShFree(items);
    }
    return (ULONG)refs;
}

DISPATCH_FUNCTIONS(items, FolderItems, &IID_FolderItems)

static HRESULT STDMETHODCALLTYPE items_get_count(FolderItems *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = (LONG)items_of(This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE items_get_application(FolderItems *This, IDispatch **object)
{
    return application(object);
}

static HRESULT STDMETHODCALLTYPE items_get_parent(FolderItems *This, IDispatch **parent)
{
    ITEMS_OBJECT *items = items_of(This);
    if (!parent)
        return E_POINTER;
    items->Shell->lpVtbl->AddRef(items->Shell);
    return make_folder_object(items->Shell, items->Pidl, parent);
}

static HRESULT items_item_at(ITEMS_OBJECT *items, ULONG index, IDispatch **item)
{
    PITEMID_CHILD child = ILClone(items->Children[index]);
    if (!child)
        return E_OUTOFMEMORY;
    items->Shell->lpVtbl->AddRef(items->Shell);
    return make_item_object(items->Shell, items->Pidl, child, item);
}

/* an item by position or by name; a position out of the list or a name it lacks gives no item. */
static HRESULT STDMETHODCALLTYPE items_item(FolderItems *This, VARIANT index, IDispatch **item)
{
    ITEMS_OBJECT *items = items_of(This);
    if (!item)
        return E_POINTER;
    *item = nullptr;
    const VARIANT *value = ShPlainVariant(&index);
    if (value && value->vt == VT_BSTR) {
        for (ULONG i = 0; i < items->Count; i++) {
            LPWSTR name;
            if (FAILED(display_text(items->Shell, items->Children[i], SHGDN_INFOLDER, &name)))
                continue;
            BOOL match = value->bstrVal && !_wcsicmp(name, value->bstrVal);
            ShFree(name);
            if (match)
                return items_item_at(items, i, item);
        }
        return S_OK;
    }
    LONG position;
    if (!variant_number(&index, &position) || position < 0 || (ULONG)position >= items->Count)
        return S_OK;
    return items_item_at(items, (ULONG)position, item);
}

static ITEMS_ENUM *enum_of(IEnumVARIANT *This)
{
    return (ITEMS_ENUM *)This;
}

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumVARIANT))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&enum_of(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&enum_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumVARIANT *This)
{
    ITEMS_ENUM *e = enum_of(This);
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        items_release(&e->Items->Iface);
        ShFree(e);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    ITEMS_ENUM *e = enum_of(This);
    ULONG done = 0;
    HRESULT hr = S_OK;
    if (!values && count)
        return E_POINTER;
    while (done < count && e->Position < e->Items->Count) {
        IDispatch *item;
        hr = items_item_at(e->Items, e->Position, &item);
        if (FAILED(hr))
            break;
        VariantInit(&values[done]);
        values[done].vt = VT_DISPATCH;
        values[done].pdispVal = item;
        e->Position++;
        done++;
    }
    if (fetched)
        *fetched = done;
    if (FAILED(hr)) {
        while (done)
            VariantClear(&values[--done]);
        return hr;
    }
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumVARIANT *This, ULONG count)
{
    ITEMS_ENUM *e = enum_of(This);
    ULONG left = e->Items->Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumVARIANT *This)
{
    enum_of(This)->Position = 0;
    return S_OK;
}

static HRESULT make_enum(ITEMS_OBJECT *items, ULONG position, IEnumVARIANT **enumerator)
{
    ITEMS_ENUM *e = ShAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Iface.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    e->Items = items;
    e->Position = position;
    items_add_ref(&items->Iface);
    *enumerator = &e->Iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumVARIANT *This, IEnumVARIANT **enumerator)
{
    ITEMS_ENUM *e = enum_of(This);
    if (!enumerator)
        return E_POINTER;
    return make_enum(e->Items, e->Position, enumerator);
}

static const IEnumVARIANTVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

static HRESULT STDMETHODCALLTYPE items_new_enum(FolderItems *This, IUnknown **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    return make_enum(items_of(This), 0, (IEnumVARIANT **)enumerator);
}

static const FolderItemsVtbl g_items_vtbl = {
    items_query_interface,
    items_add_ref,
    items_release,
    items_get_type_info_count,
    items_get_type_info,
    items_get_ids_of_names,
    items_invoke,
    items_get_count,
    items_get_application,
    items_get_parent,
    items_item,
    items_new_enum,
};

/* ------------------------------------------------------------------ Folder */

static FOLDER_OBJECT *folder_of(Folder *This)
{
    return (FOLDER_OBJECT *)This;
}

static HRESULT STDMETHODCALLTYPE folder_query_interface(Folder *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_Folder))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&folder_of(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE folder_add_ref(Folder *This)
{
    return (ULONG)InterlockedIncrement(&folder_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE folder_release(Folder *This)
{
    FOLDER_OBJECT *folder = folder_of(This);
    LONG refs = InterlockedDecrement(&folder->Refs);
    if (!refs) {
        folder->Shell->lpVtbl->Release(folder->Shell);
        ILFree(folder->Pidl);
        InterlockedDecrement(&g_folder_objects);
        ShFree(folder);
    }
    return (ULONG)refs;
}

DISPATCH_FUNCTIONS(folder, Folder, &IID_Folder)

/* the name of the folder in the folder above it. */
static HRESULT STDMETHODCALLTYPE folder_get_title(Folder *This, BSTR *title)
{
    FOLDER_OBJECT *folder = folder_of(This);
    WCHAR name[MAX_PATH];
    if (!title)
        return E_POINTER;
    *title = nullptr;
    IShellFolder *parent;
    PITEMID_CHILD child;
    if (SUCCEEDED(bind_parent(folder->Pidl, &parent, &child))) {
        LPWSTR text;
        HRESULT hr = display_text(parent, child, SHGDN_INFOLDER, &text);
        parent->lpVtbl->Release(parent);
        ILFree(child);
        if (SUCCEEDED(hr)) {
            hr = text_bstr(text, title);
            ShFree(text);
            return hr;
        }
    }
    if (ShIdListLastName(folder->Pidl, name, MAX_PATH))
        return text_bstr(name, title);
    return text_bstr(L"Desktop", title);
}

static HRESULT STDMETHODCALLTYPE folder_get_application(Folder *This, IDispatch **object)
{
    return application(object);
}

static HRESULT STDMETHODCALLTYPE folder_get_parent_folder(Folder *This, IDispatch **parent)
{
    FOLDER_OBJECT *folder = folder_of(This);
    if (!parent)
        return E_POINTER;
    *parent = nullptr;
    UINT count;
    list_count(folder->Pidl, &count);
    if (!count)
        return S_OK;
    PIDLIST_ABSOLUTE up = ILClone(folder->Pidl);
    if (!up)
        return E_OUTOFMEMORY;
    ILRemoveLastID(up);
    IShellFolder *shell;
    HRESULT hr = ShBindAbsolute(up, &IID_IShellFolder, (void **)&shell);
    if (SUCCEEDED(hr))
        hr = make_folder_object(shell, up, parent);
    ILFree(up);
    return hr == E_OUTOFMEMORY ? hr : S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_items(Folder *This, IDispatch **items)
{
    ITEMS_OBJECT *object;
    if (!items)
        return E_POINTER;
    *items = nullptr;
    HRESULT hr = make_items_object(folder_of(This), &object);
    if (SUCCEEDED(hr))
        *items = (IDispatch *)&object->Iface;
    return hr;
}

/* an item by name, which may lead through folders. a name that is not there gives no item. */
static HRESULT STDMETHODCALLTYPE folder_parse_name(Folder *This, BSTR name, IDispatch **item)
{
    FOLDER_OBJECT *folder = folder_of(This);
    if (!item)
        return E_POINTER;
    *item = nullptr;
    if (!name)
        return S_OK;
    PIDLIST_RELATIVE relative;
    if (FAILED(folder->Shell->lpVtbl->ParseDisplayName(folder->Shell, nullptr, nullptr, name, nullptr, &relative,
                                                       nullptr)))
        return S_OK;
    PIDLIST_ABSOLUTE absolute = ILCombine(folder->Pidl, relative);
    ILFree(relative);
    if (!absolute)
        return E_OUTOFMEMORY;
    IShellFolder *parent;
    PITEMID_CHILD child;
    HRESULT hr = bind_parent(absolute, &parent, &child);
    if (SUCCEEDED(hr)) {
        PIDLIST_ABSOLUTE up = ILClone(absolute);
        if (up) {
            ILRemoveLastID(up);
            hr = make_item_object(parent, up, child, item);
            ILFree(up);
        } else {
            parent->lpVtbl->Release(parent);
            ILFree(child);
            hr = E_OUTOFMEMORY;
        }
    }
    ILFree(absolute);
    return hr;
}

/* the directory a folder is on the file system. */
static HRESULT folder_directory(FOLDER_OBJECT *folder, WCHAR *path)
{
    return ShPathFromIdList(folder->Pidl, path, MAX_PATH) ? S_OK : E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE folder_new_folder(Folder *This, BSTR name, VARIANT options)
{
    WCHAR path[MAX_PATH];
    if (!name || !name[0])
        return E_INVALIDARG;
    HRESULT hr = folder_directory(folder_of(This), path);
    if (FAILED(hr))
        return hr;
    SIZE_T length = wcslen(path);
    if (length + wcslen(name) + 2 > MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    if (length && path[length - 1] != L'\\')
        path[length++] = L'\\';
    wcscpy(path + length, name);
    if (!CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/* whether the object is an item or a list of them of this file, and so which. */
static BOOL is_object(IUnknown *object, REFIID iid, const void *vtbl, void **self)
{
    if (FAILED(object->lpVtbl->QueryInterface(object, iid, self)))
        return FALSE;
    if (*(const void *const *)*self == vtbl)
        return TRUE;
    ((IUnknown *)*self)->lpVtbl->Release((IUnknown *)*self);
    *self = nullptr;
    return FALSE;
}

/* what is copied or moved here is an item, a list of items or a path. */
static HRESULT transfer(Folder *This, VARIANT item, VARIANT options, BOOL move)
{
    FOLDER_OBJECT *folder = folder_of(This);
    WCHAR directory[MAX_PATH];
    HRESULT hr = folder_directory(folder, directory);
    if (FAILED(hr))
        return hr;
    LONG number = 0;
    DWORD flags = variant_number(&options, &number) ? (DWORD)number : 0;
    const VARIANT *what = ShPlainVariant(&item);
    if (!what)
        return E_INVALIDARG;
    if (what->vt == VT_BSTR) {
        if (!what->bstrVal || !what->bstrVal[0])
            return E_INVALIDARG;
        return ShCopyPath(what->bstrVal, directory, flags, move);
    }
    if (what->vt != VT_DISPATCH && what->vt != VT_UNKNOWN)
        return E_INVALIDARG;
    if (!what->punkVal)
        return E_INVALIDARG;
    void *self;
    if (is_object(what->punkVal, &IID_FolderItems, &g_items_vtbl, &self)) {
        ITEMS_OBJECT *items = self;
        hr = S_OK;
        for (ULONG i = 0; SUCCEEDED(hr) && i < items->Count; i++)
            hr = ShCopyItem(items->Shell, items->Children[i], directory, flags, move);
        items_release(&items->Iface);
        return hr;
    }
    if (is_object(what->punkVal, &IID_FolderItem, &g_item_vtbl, &self)) {
        ITEM_OBJECT *object = self;
        hr = ShCopyItem(object->Shell, object->Child, directory, flags, move);
        item_release(&object->Iface);
        return hr;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE folder_move_here(Folder *This, VARIANT item, VARIANT options)
{
    return transfer(This, item, options, TRUE);
}

static HRESULT STDMETHODCALLTYPE folder_copy_here(Folder *This, VARIANT item, VARIANT options)
{
    return transfer(This, item, options, FALSE);
}

/* the details of an item by the columns of the file system: name, size, type and the time of the last change. */
static HRESULT STDMETHODCALLTYPE folder_get_details_of(Folder *This, VARIANT item, int column, BSTR *details)
{
    static const LPCWSTR titles[] = {L"Name", L"Size", L"Type", L"Date modified"};
    if (!details)
        return E_POINTER;
    *details = nullptr;
    if (column < 0 || column >= (int)RTL_NUMBER_OF(titles))
        return S_OK;
    const VARIANT *what = ShPlainVariant(&item);
    void *self;
    if (!what || (what->vt != VT_DISPATCH && what->vt != VT_UNKNOWN) || !what->punkVal)
        return text_bstr(titles[column], details);
    if (!is_object(what->punkVal, &IID_FolderItem, &g_item_vtbl, &self))
        return E_INVALIDARG;
    ITEM_OBJECT *object = self;
    HRESULT hr;
    switch (column) {
    case 0:
        hr = item_get_name(&object->Iface, details);
        break;
    case 2:
        hr = item_get_type(&object->Iface, details);
        break;
    default: {
        ULONGLONG size;
        FILETIME time, local;
        SYSTEMTIME system;
        WCHAR text[64] = L"";
        hr = ShItemDetails(object->Shell, object->Child, &size, &time);
        if (SUCCEEDED(hr)) {
            if (column == 1) {
                BOOL directory = (attributes_of(object->Shell, object->Child, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
                if (!directory)
                    _snwprintf(text, RTL_NUMBER_OF(text), L"%llu bytes", size);
            } else if (FileTimeToLocalFileTime(&time, &local) && FileTimeToSystemTime(&local, &system)) {
                WCHAR date[32], clock[32];
                if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &system, nullptr, date, RTL_NUMBER_OF(date)) &&
                    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &system, nullptr, clock, RTL_NUMBER_OF(clock)))
                    _snwprintf(text, RTL_NUMBER_OF(text), L"%ls %ls", date, clock);
            }
            text[RTL_NUMBER_OF(text) - 1] = 0;
            hr = text_bstr(text, details);
        }
    }
    }
    item_release(&object->Iface);
    return hr;
}

static const FolderVtbl g_folder_vtbl = {
    folder_query_interface,
    folder_add_ref,
    folder_release,
    folder_get_type_info_count,
    folder_get_type_info,
    folder_get_ids_of_names,
    folder_invoke,
    folder_get_title,
    folder_get_application,
    folder_get_parent_folder,
    folder_get_parent_folder,
    folder_items,
    folder_parse_name,
    folder_new_folder,
    folder_move_here,
    folder_copy_here,
    folder_get_details_of,
};

/* ------------------------------------------------------------------ NameSpace */

/*
 * a path through a file that is a folder of its class, like "C:\data\a.zip\docs": the longest part of it that is a
 * file binds as the class, and the rest is parsed by that folder.
 */
static HRESULT parse_inside(LPCWSTR text, PIDLIST_ABSOLUTE *pidl)
{
    WCHAR full[MAX_PATH], original[MAX_PATH];
    *pidl = nullptr;
    DWORD n = GetFullPathNameW(text, MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    wcscpy(original, full);
    for (LPWSTR slash = wcsrchr(full, L'\\'); slash; slash = wcsrchr(full, L'\\')) {
        *slash = 0;
        DWORD attributes = GetFileAttributesW(full);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            continue;
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
            break;
        PIDLIST_ABSOLUTE file;
        if (FAILED(ShParseName(full, nullptr, &file, nullptr)))
            break;
        IShellFolder *shell;
        HRESULT hr = ShBindAbsolute(file, &IID_IShellFolder, (void **)&shell);
        if (SUCCEEDED(hr)) {
            PIDLIST_RELATIVE relative;
            hr = shell->lpVtbl->ParseDisplayName(shell, nullptr, nullptr, original + (slash - full) + 1, nullptr,
                                                 &relative, nullptr);
            shell->lpVtbl->Release(shell);
            if (SUCCEEDED(hr)) {
                *pidl = ILCombine(file, relative);
                ILFree(relative);
                hr = *pidl ? S_OK : E_OUTOFMEMORY;
            }
        }
        ILFree(file);
        return hr;
    }
    return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

/*
 * the folder of a path or of the number of a special folder. what is not a folder, or not there, gives no folder and
 * not an error, as the object of windows answers.
 */
HRESULT ShNameSpace(const VARIANT *directory, IDispatch **folder)
{
    *folder = nullptr;
    directory = ShPlainVariant(directory);
    PIDLIST_ABSOLUTE pidl = nullptr;
    LONG id;
    if (directory && (directory->vt == VT_I2 || directory->vt == VT_I4) && variant_number(directory, &id)) {
        WCHAR path[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, id, nullptr, SHGFP_TYPE_CURRENT, path)))
            return S_OK;
        pidl = ShIdListFromPath(path);
    } else {
        LPWSTR text = ShVariantText(directory);
        if (!text || !text[0]) {
            ShFree(text);
            return S_OK;
        }
        if (FAILED(ShParseName(text, nullptr, &pidl, nullptr)))
            parse_inside(text, &pidl);
        ShFree(text);
    }
    if (!pidl)
        return S_OK;
    IShellFolder *shell;
    HRESULT hr = ShBindAbsolute(pidl, &IID_IShellFolder, (void **)&shell);
    if (SUCCEEDED(hr))
        hr = make_folder_object(shell, pidl, folder);
    else
        hr = S_OK;
    ILFree(pidl);
    return hr;
}
