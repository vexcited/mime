/*
 * shell items over item id lists, shell item arrays and their enumerators, and small shell objects: the task
 * allocator, association handler enumeration and folder browsing.
 */
#include "shell32_private.h"

#ifndef MK_E_NOOBJECT
#define MK_E_NOOBJECT ((HRESULT)0x800401E5L)
#endif

SFGAOF ShAttributesFromFile(DWORD attributes)
{
    SFGAOF result = SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE |
                    SFGAO_HASPROPSHEET | SFGAO_DROPTARGET | SFGAO_FILESYSTEM;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        result |= SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSANCESTOR | SFGAO_STORAGEANCESTOR | SFGAO_STORAGE;
    else
        result |= SFGAO_STREAM;
    if (attributes & FILE_ATTRIBUTE_HIDDEN)
        result |= SFGAO_HIDDEN;
    if (attributes & FILE_ATTRIBUTE_READONLY)
        result |= SFGAO_READONLY;
    return result;
}

/* the length of the root, "x:\" or "\\server\share\", or 0. */
static SIZE_T root_length(LPCWSTR path)
{
    if (path[0] && path[1] == L':' && path[2] == L'\\')
        return 3;
    if (path[0] == L'\\' && path[1] == L'\\') {
        LPCWSTR p = path + 2;
        for (int parts = 0; *p && parts < 2; parts++) {
            while (*p && *p != L'\\')
                p++;
            if (*p)
                p++;
        }
        return (SIZE_T)(p - path);
    }
    return 0;
}

static LPCWSTR file_name(LPCWSTR path)
{
    LPCWSTR slash = wcsrchr(path, L'\\');
    return slash && slash[1] ? slash + 1 : path;
}

/* ------------------------------------------------------------------ IShellItem */

/*
 * an item is the absolute list it names. a file system item's path is worked out once, at creation.
 */
typedef struct _SH_ITEM {
    IShellItem Interface;
    IPersistIDList Persist;
    IShellItemImageFactory Factory;
    LONG Refs;
    PIDLIST_ABSOLUTE Pidl;
    /* the list is the desktop, "This PC" or a file system object. */
    BOOL Desktop, Computer, FileSystem;
    WCHAR Path[MAX_PATH];
} SH_ITEM;

static const IShellItemVtbl g_item_vtbl;
static const IPersistIDListVtbl g_persist_vtbl;
static const IShellItemImageFactoryVtbl g_factory_vtbl;

static SH_ITEM *item_from_persist(IPersistIDList *This)
{
    return (SH_ITEM *)((BYTE *)This - offsetof(SH_ITEM, Persist));
}

static SH_ITEM *item_from_factory(IShellItemImageFactory *This)
{
    return (SH_ITEM *)((BYTE *)This - offsetof(SH_ITEM, Factory));
}

/* the item takes ownership of the list. */
static HRESULT create_item(PIDLIST_ABSOLUTE pidl, REFIID riid, void **object)
{
    SH_ITEM *item = ShAllocZero(sizeof(*item));
    if (!item) {
        ShFree(pidl);
        return E_OUTOFMEMORY;
    }
    item->Interface.lpVtbl = &g_item_vtbl;
    item->Persist.lpVtbl = &g_persist_vtbl;
    item->Factory.lpVtbl = &g_factory_vtbl;
    item->Refs = 1;
    item->Pidl = pidl;
    if (!pidl->mkid.cb) {
        item->Desktop = TRUE;
        item->FileSystem = ShPathFromIdList(pidl, item->Path, MAX_PATH);
    } else {
        item->Computer = ShIsComputerItem((const BYTE *)pidl) && !ILGetNext(pidl)->mkid.cb;
        item->FileSystem = ShPathFromIdList(pidl, item->Path, MAX_PATH);
    }
    HRESULT hr = item->Interface.lpVtbl->QueryInterface(&item->Interface, riid, object);
    item->Interface.lpVtbl->Release(&item->Interface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE item_query_interface(IShellItem *This, REFIID riid, void **object)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellItem)) {
        *object = &item->Interface;
    } else if (IsEqualIID(riid, &IID_IPersistIDList) || IsEqualIID(riid, &IID_IPersist)) {
        *object = &item->Persist;
    } else if (IsEqualIID(riid, &IID_IShellItemImageFactory)) {
        *object = &item->Factory;
    } else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE item_add_ref(IShellItem *This)
{
    return (ULONG)InterlockedIncrement(&((SH_ITEM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE item_release(IShellItem *This)
{
    SH_ITEM *item = (SH_ITEM *)This;
    LONG refs = InterlockedDecrement(&item->Refs);
    if (!refs) {
        ShFree(item->Pidl);
        ShFree(item);
    }
    return (ULONG)refs;
}

/* ------------------------------------------------------------------ folder enumeration */

typedef struct _ITEM_ENUM {
    IEnumShellItems Interface;
    LONG Refs;
    /* the folder whose children are listed, or the items of an array. */
    PIDLIST_ABSOLUTE Folder;
    HANDLE Find;
    BOOL Done;
    PIDLIST_ABSOLUTE *Lists;
    UINT Count, Position;
    WCHAR Pattern[MAX_PATH];
} ITEM_ENUM;

static const IEnumShellItemsVtbl g_enum_vtbl;

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumShellItems *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumShellItems)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumShellItems *This)
{
    return (ULONG)InterlockedIncrement(&((ITEM_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumShellItems *This)
{
    ITEM_ENUM *e = (ITEM_ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        if (e->Find != INVALID_HANDLE_VALUE)
            FindClose(e->Find);
        for (UINT i = 0; i < e->Count; i++)
            ShFree(e->Lists[i]);
        ShFree(e->Lists);
        ShFree(e->Folder);
        ShFree(e);
    }
    return (ULONG)refs;
}

/* hidden and system files are skipped. */
static BOOL enum_next_entry(ITEM_ENUM *e, WIN32_FIND_DATAW *data)
{
    while (!e->Done) {
        BOOL found;
        if (e->Find == INVALID_HANDLE_VALUE) {
            e->Find = FindFirstFileW(e->Pattern, data);
            found = e->Find != INVALID_HANDLE_VALUE;
        } else {
            found = FindNextFileW(e->Find, data);
        }
        if (!found) {
            e->Done = TRUE;
            return FALSE;
        }
        if (!wcscmp(data->cFileName, L".") || !wcscmp(data->cFileName, L".."))
            continue;
        if (data->dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            continue;
        return TRUE;
    }
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumShellItems *This, ULONG count, IShellItem **items, ULONG *fetched)
{
    ITEM_ENUM *e = (ITEM_ENUM *)This;
    ULONG done = 0;
    if (fetched)
        *fetched = 0;
    if (!items && count)
        return E_POINTER;
    if (!fetched && count > 1)
        return E_INVALIDARG;
    while (done < count) {
        PIDLIST_ABSOLUTE pidl;
        if (e->Lists) {
            if (e->Position >= e->Count)
                break;
            pidl = ILClone(e->Lists[e->Position++]);
        } else {
            WIN32_FIND_DATAW data;
            if (!enum_next_entry(e, &data))
                break;
            pidl = ShChildList(e->Folder, &data);
        }
        if (!pidl || FAILED(create_item(pidl, &IID_IShellItem, (void **)&items[done]))) {
            if (!pidl)
                return E_OUTOFMEMORY;
            break;
        }
        done++;
    }
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumShellItems *This, ULONG count)
{
    ITEM_ENUM *e = (ITEM_ENUM *)This;
    while (count--) {
        if (e->Lists) {
            if (e->Position >= e->Count)
                return S_FALSE;
            e->Position++;
        } else {
            WIN32_FIND_DATAW data;
            if (!enum_next_entry(e, &data))
                return S_FALSE;
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumShellItems *This)
{
    ITEM_ENUM *e = (ITEM_ENUM *)This;
    if (e->Find != INVALID_HANDLE_VALUE) {
        FindClose(e->Find);
        e->Find = INVALID_HANDLE_VALUE;
    }
    e->Done = FALSE;
    e->Position = 0;
    return S_OK;
}

static ITEM_ENUM *new_enum(void)
{
    ITEM_ENUM *e = ShAllocZero(sizeof(*e));
    if (e) {
        e->Interface.lpVtbl = &g_enum_vtbl;
        e->Refs = 1;
        e->Find = INVALID_HANDLE_VALUE;
    }
    return e;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumShellItems *This, IEnumShellItems **clone)
{
    ITEM_ENUM *e = (ITEM_ENUM *)This;
    if (!clone)
        return E_POINTER;
    ITEM_ENUM *copy = new_enum();
    *clone = nullptr;
    if (!copy)
        return E_OUTOFMEMORY;
    copy->Folder = e->Folder ? ILClone(e->Folder) : nullptr;
    wcscpy(copy->Pattern, e->Pattern);
    if (e->Lists) {
        copy->Lists = ShAllocZero((e->Count ? e->Count : 1) * sizeof(*copy->Lists));
        if (!copy->Lists) {
            enum_release(&copy->Interface);
            return E_OUTOFMEMORY;
        }
        for (UINT i = 0; i < e->Count; i++)
            copy->Lists[i] = ILClone(e->Lists[i]);
        copy->Count = e->Count;
        copy->Position = e->Position;
    }
    *clone = &copy->Interface;
    return S_OK;
}

static const IEnumShellItemsVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

static HRESULT STDMETHODCALLTYPE item_bind_to_handler(IShellItem *This, IBindCtx *context, REFGUID handler,
                                                      REFIID riid, void **object)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!object || !handler || !riid)
        return E_INVALIDARG;
    *object = nullptr;
    if (IsEqualGUID(handler, &BHID_EnumItems)) {
        if (!item->FileSystem)
            return E_NOINTERFACE;
        DWORD attributes = GetFileAttributesW(item->Path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return HRESULT_FROM_WIN32(GetLastError());
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return E_NOINTERFACE;
        SIZE_T length = wcslen(item->Path);
        if (length + 2 >= MAX_PATH)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        ITEM_ENUM *e = new_enum();
        if (!e)
            return E_OUTOFMEMORY;
        e->Folder = ILClone(item->Pidl);
        wcscpy(e->Pattern, item->Path);
        if (e->Pattern[length - 1] != L'\\')
            e->Pattern[length++] = L'\\';
        e->Pattern[length] = L'*';
        e->Pattern[length + 1] = 0;
        HRESULT hr = e->Interface.lpVtbl->QueryInterface(&e->Interface, riid, object);
        e->Interface.lpVtbl->Release(&e->Interface);
        return hr;
    }
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE item_get_parent(IShellItem *This, IShellItem **parent)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!parent)
        return E_INVALIDARG;
    *parent = nullptr;
    if (item->Desktop)
        return MK_E_NOOBJECT;
    PIDLIST_ABSOLUTE up = ILClone(item->Pidl);
    if (!up)
        return E_OUTOFMEMORY;
    ILRemoveLastID(up);
    return create_item(up, &IID_IShellItem, (void **)parent);
}

/* "file:///c:/a%20b", with the characters a url cannot hold escaped. */
static LPWSTR make_url(LPCWSTR path)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    SIZE_T length = wcslen(path);
    LPWSTR url = ShAlloc((8 + length * 3 + 1) * sizeof(WCHAR));
    if (!url)
        return nullptr;
    wcscpy(url, L"file:///");
    LPWSTR d = url + 8;
    if (path[0] == L'\\' && path[1] == L'\\') {
        wcscpy(url, L"file:");
        d = url + 5;
        *d++ = L'/';
        *d++ = L'/';
        path += 2;
    }
    for (; *path; path++) {
        WCHAR c = *path;
        if (c == L'\\')
            *d++ = L'/';
        else if (c == L' ' || c == L'%' || c == L'#' || c == L'?' || c < 0x20) {
            *d++ = L'%';
            *d++ = hex[(c >> 4) & 15];
            *d++ = hex[c & 15];
        } else
            *d++ = c;
    }
    *d = 0;
    return url;
}

static HRESULT STDMETHODCALLTYPE item_get_display_name(IShellItem *This, SIGDN form, LPWSTR *name)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    if (item->Desktop || item->Computer) {
        switch (form) {
        case SIGDN_NORMALDISPLAY:
        case SIGDN_PARENTRELATIVE:
        case SIGDN_PARENTRELATIVEFORUI:
        case SIGDN_PARENTRELATIVEFORADDRESSBAR:
        case SIGDN_PARENTRELATIVEEDITING:
        case SIGDN_DESKTOPABSOLUTEEDITING:
            *name = ShDuplicate(item->Desktop ? L"Desktop" : L"This PC");
            return *name ? S_OK : E_OUTOFMEMORY;
        case SIGDN_PARENTRELATIVEPARSING:
        case SIGDN_DESKTOPABSOLUTEPARSING:
            *name = ShDuplicate(item->Desktop ? L"Desktop" : L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
            return *name ? S_OK : E_OUTOFMEMORY;
        case SIGDN_FILESYSPATH:
            if (!item->FileSystem)
                return E_INVALIDARG;
            *name = ShDuplicate(item->Path);
            return *name ? S_OK : E_OUTOFMEMORY;
        default:
            return E_INVALIDARG;
        }
    }
    if (!item->FileSystem) {
        /* the shell's other folders are known by their class. */
        return form == SIGDN_FILESYSPATH ? E_INVALIDARG : E_NOTIMPL;
    }
    BOOL root = wcslen(item->Path) <= root_length(item->Path);
    switch (form) {
    case SIGDN_FILESYSPATH:
    case SIGDN_DESKTOPABSOLUTEPARSING:
    case SIGDN_DESKTOPABSOLUTEEDITING:
        *name = ShDuplicate(item->Path);
        break;
    case SIGDN_URL:
        *name = make_url(item->Path);
        break;
    case SIGDN_PARENTRELATIVEPARSING:
    case SIGDN_PARENTRELATIVEEDITING:
        *name = ShDuplicate(root ? item->Path : file_name(item->Path));
        break;
    case SIGDN_NORMALDISPLAY:
    case SIGDN_PARENTRELATIVE:
    case SIGDN_PARENTRELATIVEFORUI:
    case SIGDN_PARENTRELATIVEFORADDRESSBAR:
        if (root && item->Path[1] == L':') {
            WCHAR label[32];
            _snwprintf(label, RTL_NUMBER_OF(label), L"Local Disk (%lc:)", item->Path[0]);
            label[RTL_NUMBER_OF(label) - 1] = 0;
            *name = ShDuplicate(label);
        } else {
            *name = ShDuplicate(file_name(item->Path));
        }
        break;
    default:
        return E_INVALIDARG;
    }
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE item_get_attributes(IShellItem *This, SFGAOF mask, SFGAOF *attributes)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!attributes)
        return E_INVALIDARG;
    if (item->Computer || (item->Desktop && !item->FileSystem)) {
        *attributes = (SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_CANLINK | SFGAO_STORAGEANCESTOR) & mask;
        return *attributes == mask ? S_OK : S_FALSE;
    }
    if (!item->FileSystem) {
        *attributes = 0;
        return mask ? S_FALSE : S_OK;
    }
    DWORD file = GetFileAttributesW(item->Path);
    if (file == INVALID_FILE_ATTRIBUTES) {
        *attributes = 0;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *attributes = ShAttributesFromFile(file) & mask;
    return *attributes == mask ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE item_compare(IShellItem *This, IShellItem *other, SICHINTF hint, int *order)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!other || !order)
        return E_INVALIDARG;
    IPersistIDList *persist;
    PIDLIST_ABSOLUTE other_list;
    HRESULT hr = other->lpVtbl->QueryInterface(other, &IID_IPersistIDList, (void **)&persist);
    if (SUCCEEDED(hr)) {
        hr = persist->lpVtbl->GetIDList(persist, &other_list);
        persist->lpVtbl->Release(persist);
    }
    int result;
    if (SUCCEEDED(hr)) {
        WCHAR path[MAX_PATH];
        if (item->FileSystem && ShPathFromIdList(other_list, path, MAX_PATH))
            result = _wcsicmp(item->Path, path);
        else
            result = ILIsEqual(item->Pidl, other_list) ? 0 : 1;
        ShFree(other_list);
    } else {
        LPWSTR path;
        hr = other->lpVtbl->GetDisplayName(other, SIGDN_FILESYSPATH, &path);
        if (FAILED(hr))
            return hr;
        result = item->FileSystem ? _wcsicmp(item->Path, path) : 1;
        ShFree(path);
    }
    *order = result < 0 ? -1 : result > 0;
    return result ? S_FALSE : S_OK;
}

static const IShellItemVtbl g_item_vtbl = {
    item_query_interface, item_add_ref,          item_release,        item_bind_to_handler,
    item_get_parent,      item_get_display_name, item_get_attributes, item_compare,
};

static HRESULT STDMETHODCALLTYPE persist_query_interface(IPersistIDList *This, REFIID riid, void **object)
{
    return item_query_interface(&item_from_persist(This)->Interface, riid, object);
}

static ULONG STDMETHODCALLTYPE persist_add_ref(IPersistIDList *This)
{
    return item_add_ref(&item_from_persist(This)->Interface);
}

static ULONG STDMETHODCALLTYPE persist_release(IPersistIDList *This)
{
    return item_release(&item_from_persist(This)->Interface);
}

static HRESULT STDMETHODCALLTYPE persist_get_class_id(IPersistIDList *This, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_ShellItem;
    return S_OK;
}

/* an item keeps the list it was made with. */
static HRESULT STDMETHODCALLTYPE persist_set_id_list(IPersistIDList *This, PCIDLIST_ABSOLUTE pidl)
{
    return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
}

static HRESULT STDMETHODCALLTYPE persist_get_id_list(IPersistIDList *This, PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_POINTER;
    *pidl = ILClone(item_from_persist(This)->Pidl);
    return *pidl ? S_OK : E_OUTOFMEMORY;
}

static const IPersistIDListVtbl g_persist_vtbl = {
    persist_query_interface, persist_add_ref,       persist_release,
    persist_get_class_id,    persist_set_id_list, persist_get_id_list,
};

/* ------------------------------------------------------------------ IShellItemImageFactory */

static HRESULT STDMETHODCALLTYPE factory_query_interface(IShellItemImageFactory *This, REFIID riid, void **object)
{
    return item_query_interface(&item_from_factory(This)->Interface, riid, object);
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IShellItemImageFactory *This)
{
    return item_add_ref(&item_from_factory(This)->Interface);
}

static ULONG STDMETHODCALLTYPE factory_release(IShellItemImageFactory *This)
{
    return item_release(&item_from_factory(This)->Interface);
}

/* the thumbnail or the icon, see ShItemImage in thumbnail.c. */
static HRESULT STDMETHODCALLTYPE factory_get_image(IShellItemImageFactory *This, SIZE size, SIIGBF flags,
                                                   HBITMAP *bitmap)
{
    SH_ITEM *item = item_from_factory(This);
    /* the desktop and "This PC" have their own pictures, the other folders of the shell show a plain folder. */
    int kind = item->Desktop ? 1 : item->Computer ? 2 : item->FileSystem ? 0 : 3;
    return ShItemImage(kind ? nullptr : item->Path, kind, size, flags, bitmap);
}

static const IShellItemImageFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_get_image,
};

/* ------------------------------------------------------------------ making items */

/* whether the bind context carries find data for an item that need not exist. */
static BOOL has_bind_data(IBindCtx *context)
{
    if (!context)
        return FALSE;
    IUnknown *data;
    if (FAILED(context->lpVtbl->GetObjectParam(context, (LPOLESTR)STR_FILE_SYS_BIND_DATA, &data)))
        return FALSE;
    data->lpVtbl->Release(data);
    return TRUE;
}

/* a path, existing or described by bind data, or a virtual folder's class. */
HRESULT ShParseName(PCWSTR name, IBindCtx *context, PIDLIST_ABSOLUTE *pidl, DWORD *attributes)
{
    *pidl = nullptr;
    if (attributes)
        *attributes = 0;
    if (!name)
        return E_INVALIDARG;
    if (name[0] == L':' && name[1] == L':' && name[2] == L'{') {
        GUID clsid;
        if (_wcsicmp(name, L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}") == 0) {
            *pidl = ShComputerList();
            if (attributes)
                *attributes = FILE_ATTRIBUTE_DIRECTORY;
            return *pidl ? S_OK : E_OUTOFMEMORY;
        }
        (void)clsid;
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    WCHAR full[MAX_PATH];
    DWORD n = GetFullPathNameW(name, MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    DWORD file = GetFileAttributesW(full);
    if (file == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (!has_bind_data(context))
            return HRESULT_FROM_WIN32(error == ERROR_PATH_NOT_FOUND ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND);
        file = FILE_ATTRIBUTE_NORMAL;
    }
    /* a trailing separator is not part of the item's name, except at the root. */
    SIZE_T length = wcslen(full);
    if (length > root_length(full) && full[length - 1] == L'\\')
        full[length - 1] = 0;
    *pidl = ShIdListFromPath(full);
    if (!*pidl)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    if (attributes)
        *attributes = file;
    return S_OK;
}

SHSTDAPI HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR path, IBindCtx *context, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!path || !riid)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = ShParseName(path, context, &pidl, nullptr);
    return SUCCEEDED(hr) ? create_item(pidl, riid, object) : hr;
}

SHSTDAPI HRESULT WINAPI SHCreateItemFromIDList(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!pidl || !riid)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE copy = ILClone(pidl);
    return copy ? create_item(copy, riid, object) : E_OUTOFMEMORY;
}

SHSTDAPI HRESULT WINAPI SHGetNameFromIDList(PCIDLIST_ABSOLUTE pidl, SIGDN form, PWSTR *name)
{
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    IShellItem *item = nullptr;
    HRESULT hr = SHCreateItemFromIDList(pidl, &IID_IShellItem, (void **)&item);
    if (SUCCEEDED(hr)) {
        hr = item->lpVtbl->GetDisplayName(item, form, name);
        item->lpVtbl->Release(item);
    }
    return hr;
}

/* the caller gives up the list. */
HRESULT ShItemFromList(PIDLIST_ABSOLUTE pidl, REFIID riid, void **object)
{
    return create_item(pidl, riid, object);
}

SHSTDAPI HRESULT WINAPI SHCreateItemWithParent(PCIDLIST_ABSOLUTE parent_list, void *parent_folder, PCUITEMID_CHILD child,
                                               REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!child || !riid || !parent_list)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE pidl = ILCombine(parent_list, child);
    return pidl ? create_item(pidl, riid, object) : E_OUTOFMEMORY;
}

SHSTDAPI HRESULT WINAPI SHGetIDListFromObject(IUnknown *object, PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_INVALIDARG;
    *pidl = nullptr;
    if (!object)
        return E_NOINTERFACE;
    IPersistIDList *persist;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_IPersistIDList, (void **)&persist);
    if (SUCCEEDED(hr)) {
        hr = persist->lpVtbl->GetIDList(persist, pidl);
        persist->lpVtbl->Release(persist);
        return hr;
    }
    IShellItem *item;
    hr = object->lpVtbl->QueryInterface(object, &IID_IShellItem, (void **)&item);
    if (SUCCEEDED(hr)) {
        LPWSTR path;
        hr = item->lpVtbl->GetDisplayName(item, SIGDN_DESKTOPABSOLUTEPARSING, &path);
        if (SUCCEEDED(hr)) {
            hr = SHParseDisplayName(path, nullptr, pidl, 0, nullptr);
            ShFree(path);
        }
        item->lpVtbl->Release(item);
        return hr;
    }
    return E_NOINTERFACE;
}

SHSTDAPI HRESULT WINAPI SHGetKnownFolderItem(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, REFIID riid,
                                             void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHGetKnownFolderIDList(id, flags, token, &pidl);
    return SUCCEEDED(hr) ? create_item(pidl, riid, object) : hr;
}

/* ------------------------------------------------------------------ IShellItemArray */

typedef struct _ITEM_ARRAY {
    IShellItemArray Interface;
    LONG Refs;
    PIDLIST_ABSOLUTE *Lists;
    UINT Count;
} ITEM_ARRAY;

static const IShellItemArrayVtbl g_array_vtbl;

static HRESULT STDMETHODCALLTYPE array_query_interface(IShellItemArray *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellItemArray)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE array_add_ref(IShellItemArray *This)
{
    return (ULONG)InterlockedIncrement(&((ITEM_ARRAY *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE array_release(IShellItemArray *This)
{
    ITEM_ARRAY *array = (ITEM_ARRAY *)This;
    LONG refs = InterlockedDecrement(&array->Refs);
    if (!refs) {
        for (UINT i = 0; i < array->Count; i++)
            ShFree(array->Lists[i]);
        ShFree(array->Lists);
        ShFree(array);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE array_bind_to_handler(IShellItemArray *This, IBindCtx *context, REFGUID handler,
                                                       REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE array_get_property_store(IShellItemArray *This, GETPROPERTYSTOREFLAGS flags,
                                                          REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE array_get_property_description_list(IShellItemArray *This, REFPROPERTYKEY key,
                                                                     REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE array_get_attributes(IShellItemArray *This, SIATTRIBFLAGS how, SFGAOF mask,
                                                      SFGAOF *attributes)
{
    ITEM_ARRAY *array = (ITEM_ARRAY *)This;
    if (!attributes)
        return E_POINTER;
    if (how < SIATTRIBFLAGS_AND || how > SIATTRIBFLAGS_APPCOMPAT)
        return E_INVALIDARG;
    SFGAOF combined = how == SIATTRIBFLAGS_OR ? 0 : mask;
    for (UINT i = 0; i < array->Count; i++) {
        IShellItem *item;
        SFGAOF one = 0;
        HRESULT hr = create_item(ILClone(array->Lists[i]), &IID_IShellItem, (void **)&item);
        if (FAILED(hr))
            return hr;
        hr = item->lpVtbl->GetAttributes(item, mask, &one);
        item->lpVtbl->Release(item);
        if (FAILED(hr))
            return hr;
        if (how == SIATTRIBFLAGS_OR)
            combined |= one;
        else
            combined &= one;
    }
    *attributes = combined & mask;
    if (how == SIATTRIBFLAGS_OR)
        return *attributes ? S_OK : S_FALSE;
    return *attributes == mask ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE array_get_count(IShellItemArray *This, DWORD *count)
{
    if (!count)
        return E_POINTER;
    *count = ((ITEM_ARRAY *)This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE array_get_item_at(IShellItemArray *This, DWORD index, IShellItem **item)
{
    ITEM_ARRAY *array = (ITEM_ARRAY *)This;
    if (!item)
        return E_POINTER;
    *item = nullptr;
    if (index >= array->Count)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE pidl = ILClone(array->Lists[index]);
    return pidl ? create_item(pidl, &IID_IShellItem, (void **)item) : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE array_enum_items(IShellItemArray *This, IEnumShellItems **enumerator)
{
    ITEM_ARRAY *array = (ITEM_ARRAY *)This;
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    ITEM_ENUM *e = new_enum();
    if (!e)
        return E_OUTOFMEMORY;
    e->Lists = ShAllocZero((array->Count ? array->Count : 1) * sizeof(*e->Lists));
    if (!e->Lists) {
        enum_release(&e->Interface);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < array->Count; i++) {
        e->Lists[i] = ILClone(array->Lists[i]);
        e->Count = i + 1;
        if (!e->Lists[i]) {
            enum_release(&e->Interface);
            return E_OUTOFMEMORY;
        }
    }
    *enumerator = &e->Interface;
    return S_OK;
}

static const IShellItemArrayVtbl g_array_vtbl = {
    array_query_interface, array_add_ref,   array_release,          array_bind_to_handler,
    array_get_property_store, array_get_property_description_list, array_get_attributes, array_get_count,
    array_get_item_at,     array_enum_items,
};

/* copies the absolute lists. */
HRESULT ShArrayFromLists(UINT count, PCIDLIST_ABSOLUTE *lists, IShellItemArray **array)
{
    ITEM_ARRAY *a = ShAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->Interface.lpVtbl = &g_array_vtbl;
    a->Refs = 1;
    a->Lists = ShAllocZero((count ? count : 1) * sizeof(*a->Lists));
    if (!a->Lists) {
        ShFree(a);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++) {
        a->Lists[i] = ILClone(lists[i]);
        a->Count = i + 1;
        if (!a->Lists[i]) {
            array_release(&a->Interface);
            return E_OUTOFMEMORY;
        }
    }
    *array = &a->Interface;
    return S_OK;
}

SHSTDAPI HRESULT WINAPI SHCreateShellItemArrayFromIDLists(UINT count, PCIDLIST_ABSOLUTE *lists,
                                                          IShellItemArray **array)
{
    if (!array)
        return E_INVALIDARG;
    *array = nullptr;
    if (!count || !lists)
        return E_INVALIDARG;
    for (UINT i = 0; i < count; i++)
        if (!lists[i])
            return E_INVALIDARG;
    return ShArrayFromLists(count, lists, array);
}

SHSTDAPI HRESULT WINAPI SHCreateShellItemArray(PCIDLIST_ABSOLUTE parent, void *folder, UINT count,
                                               PCUITEMID_CHILD *items, IShellItemArray **array)
{
    if (!array)
        return E_INVALIDARG;
    *array = nullptr;
    if (!parent || !count || !items)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE *lists = ShAllocZero(count * sizeof(*lists));
    if (!lists)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        lists[i] = ILCombine(parent, items[i]);
        if (!lists[i])
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = ShArrayFromLists(count, (PCIDLIST_ABSOLUTE *)lists, array);
    for (UINT i = 0; i < count; i++)
        ShFree(lists[i]);
    ShFree(lists);
    return hr;
}

SHSTDAPI HRESULT WINAPI SHCreateShellItemArrayFromShellItem(IShellItem *item, REFIID riid, void **array)
{
    if (!array)
        return E_INVALIDARG;
    *array = nullptr;
    if (!item || !riid)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHGetIDListFromObject((IUnknown *)item, &pidl);
    if (FAILED(hr))
        return hr;
    IShellItemArray *made;
    hr = ShArrayFromLists(1, (PCIDLIST_ABSOLUTE *)&pidl, &made);
    ShFree(pidl);
    if (SUCCEEDED(hr)) {
        hr = made->lpVtbl->QueryInterface(made, riid, array);
        made->lpVtbl->Release(made);
    }
    return hr;
}

/* ------------------------------------------------------------------ the task allocator */

static HRESULT STDMETHODCALLTYPE malloc_query_interface(IMalloc *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMalloc)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE malloc_add_ref(IMalloc *This)
{
    return 1;
}

static ULONG STDMETHODCALLTYPE malloc_release(IMalloc *This)
{
    return 1;
}

static void *STDMETHODCALLTYPE malloc_alloc(IMalloc *This, SIZE_T size)
{
    return ShAlloc(size);
}

static void *STDMETHODCALLTYPE malloc_realloc(IMalloc *This, void *p, SIZE_T size)
{
    if (!p)
        return ShAlloc(size);
    if (!size) {
        ShFree(p);
        return nullptr;
    }
    return HeapReAlloc(GetProcessHeap(), 0, p, size);
}

static void STDMETHODCALLTYPE malloc_free(IMalloc *This, void *p)
{
    ShFree(p);
}

static SIZE_T STDMETHODCALLTYPE malloc_get_size(IMalloc *This, void *p)
{
    return p ? HeapSize(GetProcessHeap(), 0, p) : (SIZE_T)-1;
}

static int STDMETHODCALLTYPE malloc_did_alloc(IMalloc *This, void *p)
{
    return p ? -1 : 0;
}

static void STDMETHODCALLTYPE malloc_heap_minimize(IMalloc *This)
{
}

static const IMallocVtbl g_malloc_vtbl = {
    malloc_query_interface, malloc_add_ref, malloc_release,  malloc_alloc,
    malloc_realloc,         malloc_free,    malloc_get_size, malloc_did_alloc,
    malloc_heap_minimize,
};
static IMalloc g_malloc = {&g_malloc_vtbl};

SHSTDAPI HRESULT WINAPI SHGetMalloc(IMalloc **malloc)
{
    if (!malloc)
        return E_INVALIDARG;
    *malloc = &g_malloc;
    return S_OK;
}

/* ------------------------------------------------------------------ association handlers */

typedef struct _HANDLER_ENUM {
    IEnumAssocHandlers Interface;
    LONG Refs;
} HANDLER_ENUM;

static HRESULT STDMETHODCALLTYPE handlers_query_interface(IEnumAssocHandlers *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumAssocHandlers)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE handlers_add_ref(IEnumAssocHandlers *This)
{
    return (ULONG)InterlockedIncrement(&((HANDLER_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE handlers_release(IEnumAssocHandlers *This)
{
    LONG refs = InterlockedDecrement(&((HANDLER_ENUM *)This)->Refs);
    if (!refs)
        ShFree(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE handlers_next(IEnumAssocHandlers *This, ULONG count, IAssocHandler **items,
                                               ULONG *fetched)
{
    if (fetched)
        *fetched = 0;
    return count ? S_FALSE : S_OK;
}

static const IEnumAssocHandlersVtbl g_handlers_vtbl = {
    handlers_query_interface,
    handlers_add_ref,
    handlers_release,
    handlers_next,
};

/* no application handles anything yet, so the enumeration is empty. */
SHSTDAPI HRESULT WINAPI SHAssocEnumHandlers(PCWSTR extension, ASSOC_FILTER filter, IEnumAssocHandlers **enumerator)
{
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    if (!extension)
        return E_INVALIDARG;
    HANDLER_ENUM *e = ShAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Interface.lpVtbl = &g_handlers_vtbl;
    e->Refs = 1;
    *enumerator = &e->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ folder browsing and notifications */

/* there is no folder browser dialog yet; the result is the one of a cancelled dialog. */
SHSTDAPI PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW(LPBROWSEINFOW info)
{
    if (info && info->pszDisplayName)
        info->pszDisplayName[0] = 0;
    return nullptr;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderA(LPBROWSEINFOA info)
{
    if (info && info->pszDisplayName)
        info->pszDisplayName[0] = 0;
    return nullptr;
}

SHSTDAPI void WINAPI SHChangeNotify(LONG event, UINT flags, LPCVOID item1, LPCVOID item2)
{
}
