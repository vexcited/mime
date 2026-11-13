/*
 * the shell namespace through IShellFolder: the desktop, which takes the paths of the file system and lists "This PC"
 * with the entries of its directory, "This PC", which lists the drives, and the folders of the file system below.
 *
 * a file whose type names a folder class, as the ProgId of .zip names the compressed folder, binds to that class like
 * a directory does: the class is created in process and told where the file is with IPersistFolder::Initialize, as
 * windows does for the files of such types.
 */
#include "shell32_private.h"
#include <mime/win32/shldisp.h>

#define SHELL_COMPARE_RESULT(value) ((HRESULT)(USHORT)(SHORT)(value))

/* ------------------------------------------------------------------ the folder class of a file */

/* the class id a key names in its CLSID subkey, as the value of that subkey. */
static BOOL class_of_key(LPCWSTR name, GUID *clsid)
{
    WCHAR key[300], text[80];
    if (wcslen(name) > 200)
        return FALSE;
    _snwprintf(key, RTL_NUMBER_OF(key), L"%ls\\CLSID", name);
    key[RTL_NUMBER_OF(key) - 1] = 0;
    HKEY handle;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &handle))
        return FALSE;
    DWORD type, size = sizeof(text) - sizeof(WCHAR);
    LONG error = RegQueryValueExW(handle, nullptr, nullptr, &type, (BYTE *)text, &size);
    RegCloseKey(handle);
    if (error || type != REG_SZ)
        return FALSE;
    text[size / sizeof(WCHAR)] = 0;
    return SUCCEEDED(CLSIDFromString(text, clsid));
}

/* the class that serves a file as a folder, from the ProgId of its extension. */
static BOOL file_class(LPCWSTR path, GUID *clsid)
{
    LPCWSTR extension = wcsrchr(path, L'.');
    LPCWSTR slash = wcsrchr(path, L'\\');
    if (!extension || (slash && extension < slash) || wcslen(extension) > 32)
        return FALSE;
    WCHAR progid[200];
    HKEY handle;
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, extension, 0, KEY_READ, &handle)) {
        DWORD type, size = sizeof(progid) - sizeof(WCHAR);
        LONG error = RegQueryValueExW(handle, nullptr, nullptr, &type, (BYTE *)progid, &size);
        RegCloseKey(handle);
        if (!error && type == REG_SZ) {
            progid[size / sizeof(WCHAR)] = 0;
            if (progid[0] && class_of_key(progid, clsid))
                return TRUE;
        }
    }
    return class_of_key(extension, clsid);
}

static HRESULT bind_class(const GUID *clsid, PCIDLIST_ABSOLUTE pidl, REFIID riid, void **object)
{
    IPersistFolder *persist;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IPersistFolder, (void **)&persist);
    if (FAILED(hr))
        return hr;
    hr = persist->lpVtbl->Initialize(persist, pidl);
    if (SUCCEEDED(hr))
        hr = persist->lpVtbl->QueryInterface(persist, riid, object);
    persist->lpVtbl->Release(persist);
    return hr;
}

/* ------------------------------------------------------------------ enumerators */

typedef struct _ID_ENUM {
    IEnumIDList Interface;
    LONG Refs;
    PITEMID_CHILD *Items;
    ULONG Count, Position;
} ID_ENUM;

static const IEnumIDListVtbl g_enum_vtbl;

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumIDList *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumIDList))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((ID_ENUM *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumIDList *This)
{
    return (ULONG)InterlockedIncrement(&((ID_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumIDList *This)
{
    ID_ENUM *e = (ID_ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        for (ULONG i = 0; i < e->Count; i++)
            ILFree(e->Items[i]);
        ShFree(e->Items);
        ShFree(e);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumIDList *This, ULONG count, PITEMID_CHILD *items, ULONG *fetched)
{
    ID_ENUM *e = (ID_ENUM *)This;
    ULONG done = 0;
    if (!items && count)
        return E_POINTER;
    while (done < count && e->Position < e->Count) {
        items[done] = ILClone(e->Items[e->Position]);
        if (!items[done]) {
            while (done)
                ILFree(items[--done]);
            return E_OUTOFMEMORY;
        }
        e->Position++;
        done++;
    }
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumIDList *This, ULONG count)
{
    ID_ENUM *e = (ID_ENUM *)This;
    ULONG left = e->Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumIDList *This)
{
    ((ID_ENUM *)This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumIDList *This, IEnumIDList **clone)
{
    ID_ENUM *e = (ID_ENUM *)This;
    if (!clone)
        return E_POINTER;
    *clone = nullptr;
    ID_ENUM *copy = ShAllocZero(sizeof(*copy));
    if (!copy)
        return E_OUTOFMEMORY;
    copy->Interface.lpVtbl = &g_enum_vtbl;
    copy->Refs = 1;
    copy->Items = ShAllocZero((e->Count ? e->Count : 1) * sizeof(*copy->Items));
    if (!copy->Items) {
        ShFree(copy);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < e->Count; i++) {
        copy->Items[i] = ILClone(e->Items[i]);
        if (!copy->Items[i]) {
            enum_release(&copy->Interface);
            return E_OUTOFMEMORY;
        }
        copy->Count++;
    }
    copy->Position = e->Position;
    *clone = &copy->Interface;
    return S_OK;
}

static const IEnumIDListVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

/* the enumerator takes the list and the items in it. */
static HRESULT make_enum(PITEMID_CHILD *items, ULONG count, IEnumIDList **enumerator)
{
    ID_ENUM *e = ShAllocZero(sizeof(*e));
    if (!e) {
        for (ULONG i = 0; i < count; i++)
            ILFree(items[i]);
        ShFree(items);
        return E_OUTOFMEMORY;
    }
    e->Interface.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    e->Items = items;
    e->Count = count;
    *enumerator = &e->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ folders */

enum {
    FOLDER_FILES,
    FOLDER_DESKTOP,
    FOLDER_COMPUTER,
    FOLDER_VIRTUAL,
};

typedef struct _FS_FOLDER {
    IShellFolder Folder;
    IPersistFolder2 Persist;
    LONG Refs;
    PIDLIST_ABSOLUTE Pidl;
    /* the desktop takes absolute lists. "This PC" and the virtual folders have no path. */
    int Kind;
    WCHAR Path[MAX_PATH];
} FS_FOLDER;

static const IShellFolderVtbl g_folder_vtbl;
static const IPersistFolder2Vtbl g_persist_vtbl;

static FS_FOLDER *folder_of(IShellFolder *This)
{
    return (FS_FOLDER *)This;
}

static FS_FOLDER *folder_of_persist(IPersistFolder2 *This)
{
    return (FS_FOLDER *)((BYTE *)This - offsetof(FS_FOLDER, Persist));
}

static HRESULT STDMETHODCALLTYPE folder_query_interface(IShellFolder *This, REFIID riid, void **object)
{
    FS_FOLDER *f = folder_of(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellFolder))
        *object = &f->Folder;
    else if (IsEqualIID(riid, &IID_IPersist) || IsEqualIID(riid, &IID_IPersistFolder) ||
             IsEqualIID(riid, &IID_IPersistFolder2))
        *object = &f->Persist;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&f->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE folder_add_ref(IShellFolder *This)
{
    return (ULONG)InterlockedIncrement(&folder_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE folder_release(IShellFolder *This)
{
    FS_FOLDER *f = folder_of(This);
    LONG refs = InterlockedDecrement(&f->Refs);
    if (!refs) {
        ILFree(f->Pidl);
        ShFree(f);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE persist_query_interface(IPersistFolder2 *This, REFIID riid, void **object)
{
    return folder_query_interface(&folder_of_persist(This)->Folder, riid, object);
}

static ULONG STDMETHODCALLTYPE persist_add_ref(IPersistFolder2 *This)
{
    return folder_add_ref(&folder_of_persist(This)->Folder);
}

static ULONG STDMETHODCALLTYPE persist_release(IPersistFolder2 *This)
{
    return folder_release(&folder_of_persist(This)->Folder);
}

static HRESULT STDMETHODCALLTYPE persist_get_class_id(IPersistFolder2 *This, CLSID *clsid)
{
    static const CLSID file_system = {0xF3364BA0, 0x65B9, 0x11CE, {0xA9, 0xBA, 0x00, 0xAA, 0x00, 0x4A, 0xE8, 0x37}};
    if (!clsid)
        return E_POINTER;
    *clsid = file_system;
    return S_OK;
}

/* a folder keeps the place it was made for. */
static HRESULT STDMETHODCALLTYPE persist_initialize(IPersistFolder2 *This, PCIDLIST_ABSOLUTE pidl)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE persist_get_cur_folder(IPersistFolder2 *This, PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_POINTER;
    *pidl = ILClone(folder_of_persist(This)->Pidl);
    return *pidl ? S_OK : E_OUTOFMEMORY;
}

static const IPersistFolder2Vtbl g_persist_vtbl = {
    persist_query_interface, persist_add_ref,    persist_release, persist_get_class_id, persist_initialize,
    persist_get_cur_folder,
};

/* the folder of a directory, with the list that leads to it. the list is kept. */
static HRESULT make_folder(PCIDLIST_ABSOLUTE pidl, LPCWSTR path, int kind, REFIID riid, void **object)
{
    FS_FOLDER *f = ShAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->Folder.lpVtbl = &g_folder_vtbl;
    f->Persist.lpVtbl = &g_persist_vtbl;
    f->Refs = 1;
    f->Kind = kind;
    f->Pidl = ILClone(pidl);
    if (!f->Pidl) {
        ShFree(f);
        return E_OUTOFMEMORY;
    }
    wcscpy(f->Path, path);
    HRESULT hr = folder_query_interface(&f->Folder, riid, object);
    folder_release(&f->Folder);
    return hr;
}

/* the attributes of a file as an item of its folder, with the folder class of its type taken into account. */
static SFGAOF file_attributes(LPCWSTR path, DWORD attributes)
{
    SFGAOF result = ShAttributesFromFile(attributes);
    GUID clsid;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && file_class(path, &clsid))
        result |= SFGAO_FOLDER | SFGAO_STORAGEANCESTOR;
    SIZE_T length = wcslen(path);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && length > 4 && !_wcsicmp(path + length - 4, L".lnk"))
        result |= SFGAO_LINK;
    return result;
}

/* the path of an absolute list that names something on a volume, bound to the folder or class it is. */
static HRESULT bind_path(PCIDLIST_ABSOLUTE pidl, LPCWSTR path, REFIID riid, void **object)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(GetLastError());
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        return make_folder(pidl, path, FOLDER_FILES, riid, object);
    GUID clsid;
    if (file_class(path, &clsid))
        return bind_class(&clsid, pidl, riid, object);
    return E_NOINTERFACE;
}

/* the items of a list as far as the first count of them. */
static PIDLIST_ABSOLUTE list_head(PCIDLIST_ABSOLUTE pidl, UINT count)
{
    PIDLIST_ABSOLUTE head = ILClone(pidl);
    if (!head)
        return nullptr;
    BYTE *p = (BYTE *)head;
    while (count-- && *(USHORT *)p)
        p += *(USHORT *)p;
    *(USHORT *)p = 0;
    return head;
}

/*
 * binds a list from the desktop. the items that name volumes and directories bind as the file system, the rest of the
 * list is left to the folder the file system part ends in, like a zip file.
 */
HRESULT ShBindAbsolute(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **object)
{
    if (!pidl || !object)
        return E_INVALIDARG;
    *object = nullptr;
    UINT count = 0;
    for (PCUIDLIST_RELATIVE p = pidl; p && p->mkid.cb; p = ILGetNext(p))
        count++;
    if (!count) {
        WCHAR path[MAX_PATH];
        if (!ShPathFromIdList(pidl, path, MAX_PATH))
            return E_FAIL;
        return make_folder(pidl, path, FOLDER_DESKTOP, riid, object);
    }
    if (count == 1) {
        WCHAR path[MAX_PATH];
        int kind = ShItemKind((const BYTE *)pidl, nullptr);
        if (kind == SH_ITEM_COMPUTER)
            return make_folder(pidl, L"", FOLDER_COMPUTER, riid, object);
        if ((kind == SH_ITEM_NETWORK || kind == SH_ITEM_CLASS) && !ShPathFromIdList(pidl, path, MAX_PATH))
            return make_folder(pidl, L"", FOLDER_VIRTUAL, riid, object);
    }
    for (UINT n = count; n; n--) {
        PIDLIST_ABSOLUTE head = list_head(pidl, n);
        WCHAR path[MAX_PATH];
        if (!head)
            return E_OUTOFMEMORY;
        if (!ShPathFromIdList(head, path, MAX_PATH)) {
            ILFree(head);
            continue;
        }
        PCUIDLIST_RELATIVE rest = pidl;
        for (UINT i = 0; i < n; i++)
            rest = ILGetNext(rest);
        HRESULT hr;
        if (!rest->mkid.cb) {
            hr = bind_path(head, path, riid, object);
        } else {
            IShellFolder *folder;
            hr = bind_path(head, path, &IID_IShellFolder, (void **)&folder);
            if (SUCCEEDED(hr)) {
                hr = folder->lpVtbl->BindToObject(folder, rest, nullptr, riid, object);
                folder->lpVtbl->Release(folder);
            }
        }
        ILFree(head);
        return hr;
    }
    return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

/* the path of a child of this folder. */
static BOOL child_path(const FS_FOLDER *f, PCUITEMID_CHILD child, LPWSTR path)
{
    WCHAR name[MAX_PATH];
    if (!child || !ShIdListLastName(child, name, MAX_PATH))
        return FALSE;
    SIZE_T length = wcslen(f->Path);
    if (length + wcslen(name) + 2 > MAX_PATH)
        return FALSE;
    wcscpy(path, f->Path);
    if (length && path[length - 1] != L'\\')
        path[length++] = L'\\';
    wcscpy(path + length, name);
    return TRUE;
}

/* the path of an item of this folder, which the desktop takes as an absolute list. */
static BOOL item_path(const FS_FOLDER *f, PCUIDLIST_RELATIVE pidl, LPWSTR path)
{
    if (f->Kind == FOLDER_DESKTOP)
        return ShPathFromIdList(pidl, path, MAX_PATH);
    if (f->Kind == FOLDER_VIRTUAL)
        return FALSE;
    return child_path(f, pidl, path);
}

static SFGAOF item_attributes(const FS_FOLDER *f, PCUIDLIST_RELATIVE pidl)
{
    WCHAR path[MAX_PATH];
    /* the empty list is the desktop itself. */
    if (!pidl->mkid.cb)
        return f->Kind == FOLDER_DESKTOP
                   ? SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSANCESTOR | SFGAO_STORAGEANCESTOR | SFGAO_CANLINK
                   : 0;
    if (item_path(f, pidl, path)) {
        DWORD file = GetFileAttributesW(path);
        if (file != INVALID_FILE_ATTRIBUTES)
            return file_attributes(path, file);
    }
    switch (ShItemKind((const BYTE *)ILFindLastID(pidl), nullptr)) {
    case SH_ITEM_COMPUTER:
        return SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSANCESTOR | SFGAO_STORAGEANCESTOR | SFGAO_CANLINK;
    case SH_ITEM_NETWORK:
    case SH_ITEM_CLASS:
        return SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_CANLINK;
    }
    return 0;
}

static BOOL item_display_name(const FS_FOLDER *f, PCUIDLIST_RELATIVE pidl, SHGDNF flags, LPWSTR text)
{
    BOOL parsing = (flags & SHGDN_FORPARSING) != 0;
    if (!pidl->mkid.cb) {
        if (f->Kind != FOLDER_DESKTOP)
            return FALSE;
        wcscpy(text, parsing ? f->Path : L"Desktop");
        return TRUE;
    }
    GUID clsid;
    WCHAR path[MAX_PATH];
    BOOL has_path = item_path(f, pidl, path);
    switch (ShItemKind((const BYTE *)ILFindLastID(pidl), &clsid)) {
    case SH_ITEM_COMPUTER:
    case SH_ITEM_NETWORK:
    case SH_ITEM_CLASS:
        if (parsing) {
            if (has_path) {
                wcscpy(text, path);
                return TRUE;
            }
            text[0] = text[1] = L':';
            return StringFromGUID2(&clsid, text + 2, 40) != 0;
        }
        if (ShClassName(&clsid, text, MAX_PATH))
            return TRUE;
        if (!has_path)
            return FALSE;
        LPCWSTR slash = wcsrchr(path, L'\\');
        wcscpy(text, slash && slash[1] ? slash + 1 : path);
        return TRUE;
    case SH_ITEM_DRIVE:
        if (!ShIdListLastName(pidl, text, MAX_PATH))
            return FALSE;
        if (!parsing)
            _snwprintf(text, MAX_PATH, L"Local Disk (%lc:)", text[0]);
        return TRUE;
    }
    if (parsing && !(flags & SHGDN_INFOLDER)) {
        if (has_path)
            wcscpy(text, path);
        return has_path;
    }
    return ShIdListLastName(pidl, text, MAX_PATH);
}

static BOOL add_item(PITEMID_CHILD **items, ULONG *count, ULONG *capacity, PITEMID_CHILD child)
{
    if (!child)
        return FALSE;
    if (*count == *capacity) {
        ULONG grown = *capacity ? *capacity * 2 : 64;
        PITEMID_CHILD *larger = *items ? HeapReAlloc(GetProcessHeap(), 0, *items, grown * sizeof(**items))
                                       : ShAlloc(grown * sizeof(**items));
        if (!larger) {
            ILFree(child);
            return FALSE;
        }
        *items = larger;
        *capacity = grown;
    }
    (*items)[(*count)++] = child;
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE folder_parse_display_name(IShellFolder *This, HWND hwnd, IBindCtx *context,
                                                           LPWSTR name, ULONG *eaten, PIDLIST_RELATIVE *pidl,
                                                           ULONG *attributes)
{
    FS_FOLDER *f = folder_of(This);
    if (!pidl)
        return E_POINTER;
    *pidl = nullptr;
    if (eaten)
        *eaten = 0;
    if (!name)
        return E_INVALIDARG;
    WCHAR full[MAX_PATH];
    LPCWSTR parse = name;
    if (f->Kind != FOLDER_DESKTOP && !(name[0] && name[1] == L':') && !(name[0] == L'\\' && name[1] == L'\\')) {
        if (f->Kind != FOLDER_FILES)
            return E_INVALIDARG;
        if (wcslen(f->Path) + wcslen(name) + 2 > MAX_PATH)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        _snwprintf(full, RTL_NUMBER_OF(full), L"%ls\\%ls", f->Path, name);
        parse = full;
    }
    PIDLIST_ABSOLUTE absolute;
    DWORD file;
    HRESULT hr = ShParseName(parse, context, &absolute, &file);
    if (FAILED(hr))
        return hr;
    if (f->Kind == FOLDER_DESKTOP) {
        *pidl = absolute;
    } else {
        PUIDLIST_RELATIVE rest = ILFindChild(f->Pidl, absolute);
        if (rest)
            *pidl = ILClone(rest);
        ILFree(absolute);
        if (!*pidl)
            return rest ? E_OUTOFMEMORY : E_INVALIDARG;
    }
    if (eaten)
        *eaten = (ULONG)wcslen(name);
    if (attributes)
        *attributes &= file_attributes(parse, file);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_enum_objects(IShellFolder *This, HWND hwnd, SHCONTF flags,
                                                     IEnumIDList **enumerator)
{
    FS_FOLDER *f = folder_of(This);
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    PITEMID_CHILD *items = nullptr;
    ULONG count = 0, capacity = 0;
    WCHAR pattern[MAX_PATH];
    SIZE_T length = wcslen(f->Path);
    if (f->Kind == FOLDER_DESKTOP && (flags & SHCONTF_FOLDERS))
        add_item(&items, &count, &capacity, ShComputerList());
    if (f->Kind == FOLDER_COMPUTER && (flags & SHCONTF_FOLDERS)) {
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++)
            if (drives & (1u << i))
                add_item(&items, &count, &capacity, ShDriveItem((WCHAR)(L'A' + i)));
    }
    if ((f->Kind == FOLDER_FILES || f->Kind == FOLDER_DESKTOP) && length && length + 3 <= MAX_PATH) {
        wcscpy(pattern, f->Path);
        if (length && pattern[length - 1] != L'\\')
            pattern[length++] = L'\\';
        pattern[length] = L'*';
        pattern[length + 1] = 0;
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES && f->Kind == FOLDER_FILES)
                return HRESULT_FROM_WIN32(error);
        } else {
            do {
                if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
                    continue;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !(flags & SHCONTF_INCLUDEHIDDEN))
                    continue;
                BOOL directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (directory ? !(flags & SHCONTF_FOLDERS) : !(flags & SHCONTF_NONFOLDERS))
                    continue;
                PIDLIST_ABSOLUTE absolute = ShChildList(f->Pidl, &data);
                if (!absolute)
                    continue;
                PITEMID_CHILD child = ILClone(ILFindLastID(absolute));
                ILFree(absolute);
                if (child && !add_item(&items, &count, &capacity, child))
                    break;
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
    if (!items) {
        items = ShAlloc(sizeof(*items));
        if (!items)
            return E_OUTOFMEMORY;
    }
    return make_enum(items, count, enumerator);
}

static HRESULT STDMETHODCALLTYPE folder_bind_to_object(IShellFolder *This, PCUIDLIST_RELATIVE pidl, IBindCtx *context,
                                                       REFIID riid, void **object)
{
    FS_FOLDER *f = folder_of(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!pidl || !pidl->mkid.cb)
        return E_INVALIDARG;
    PIDLIST_ABSOLUTE absolute = ILCombine(f->Pidl, pidl);
    if (!absolute)
        return E_OUTOFMEMORY;
    HRESULT hr = ShBindAbsolute(absolute, riid, object);
    ILFree(absolute);
    return hr;
}

static HRESULT STDMETHODCALLTYPE folder_compare_ids(IShellFolder *This, LPARAM column, PCUIDLIST_RELATIVE a,
                                                    PCUIDLIST_RELATIVE b)
{
    FS_FOLDER *f = folder_of(This);
    WCHAR name_a[MAX_PATH], name_b[MAX_PATH];
    if (!a || !b)
        return E_INVALIDARG;
    /* virtual folders come first, then drives, folders and files, each by name. */
    int kind_a = ShItemKind((const BYTE *)ILFindLastID(a), nullptr);
    int kind_b = ShItemKind((const BYTE *)ILFindLastID(b), nullptr);
    if (kind_a != kind_b)
        return SHELL_COMPARE_RESULT(kind_a < kind_b ? -1 : 1);
    if (!item_display_name(f, a, SHGDN_INFOLDER, name_a) || !item_display_name(f, b, SHGDN_INFOLDER, name_b))
        return E_INVALIDARG;
    return SHELL_COMPARE_RESULT(_wcsicmp(name_a, name_b));
}

static HRESULT STDMETHODCALLTYPE folder_create_view_object(IShellFolder *This, HWND hwnd, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IShellView))
        return ShCreateView(This, riid, object);
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE folder_get_attributes_of(IShellFolder *This, UINT count, PCUITEMID_CHILD const *items,
                                                          SFGAOF *attributes)
{
    FS_FOLDER *f = folder_of(This);
    if (!attributes)
        return E_POINTER;
    SFGAOF common = *attributes;
    for (UINT i = 0; i < count; i++) {
        if (!items[i])
            return E_INVALIDARG;
        SFGAOF item = item_attributes(f, items[i]);
        if (!item)
            return items[i]->mkid.cb ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : E_INVALIDARG;
        common &= item;
    }
    *attributes &= common;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_get_ui_object_of(IShellFolder *This, HWND hwnd, UINT count,
                                                         PCUITEMID_CHILD const *items, REFIID riid, UINT *reserved,
                                                         void **object)
{
    if (object)
        *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE folder_get_display_name_of(IShellFolder *This, PCUITEMID_CHILD pidl, SHGDNF flags,
                                                            STRRET *name)
{
    FS_FOLDER *f = folder_of(This);
    WCHAR text[MAX_PATH];
    if (!name)
        return E_POINTER;
    if (!pidl || !item_display_name(f, pidl, flags, text))
        return E_INVALIDARG;
    name->uType = STRRET_WSTR;
    name->pOleStr = ShDuplicate(text);
    return name->pOleStr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE folder_set_name_of(IShellFolder *This, HWND hwnd, PCUITEMID_CHILD pidl, LPCWSTR name,
                                                    SHGDNF flags, PITEMID_CHILD *result)
{
    if (result)
        *result = nullptr;
    return E_NOTIMPL;
}

static const IShellFolderVtbl g_folder_vtbl = {
    folder_query_interface,
    folder_add_ref,
    folder_release,
    folder_parse_display_name,
    folder_enum_objects,
    folder_bind_to_object,
    folder_bind_to_object,
    folder_compare_ids,
    folder_create_view_object,
    folder_get_attributes_of,
    folder_get_ui_object_of,
    folder_get_display_name_of,
    folder_set_name_of,
};

/*
 * the folder that holds the last item of a list, bound through the desktop, and the item itself. a list of one item
 * is in the desktop folder.
 */
SHSTDAPI HRESULT WINAPI SHBindToParent(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **result, PCUITEMID_CHILD *last)
{
    if (result)
        *result = nullptr;
    if (last)
        *last = nullptr;
    if (!pidl || !result)
        return E_INVALIDARG;
    IShellFolder *desktop;
    HRESULT hr = SHGetDesktopFolder(&desktop);
    if (FAILED(hr))
        return hr;
    PIDLIST_ABSOLUTE parent = ILClone(pidl);
    if (!parent) {
        desktop->lpVtbl->Release(desktop);
        return E_OUTOFMEMORY;
    }
    if (!ILRemoveLastID(parent))
        hr = desktop->lpVtbl->QueryInterface(desktop, riid, result);
    else if (!parent->mkid.cb)
        hr = desktop->lpVtbl->QueryInterface(desktop, riid, result);
    else
        hr = desktop->lpVtbl->BindToObject(desktop, parent, nullptr, riid, result);
    ILFree(parent);
    desktop->lpVtbl->Release(desktop);
    if (SUCCEEDED(hr) && last)
        *last = ILFindLastID(pidl);
    return hr;
}

/*
 * the handle of the old and new names that SHFileOperation hands back with FOF_WANTMAPPINGHANDLE. Mime's
 * SHFileOperation makes none, so there is no handle to release.
 */
SHSTDAPI void WINAPI SHFreeNameMappings(HANDLE mappings)
{
}

/* the printer user interface of the shell is printui.dll, which is not here. */
SHSTDAPI BOOL WINAPI SHInvokePrinterCommandW(HWND owner, UINT action, LPCWSTR parameters, LPCWSTR name, BOOL modal)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

SHSTDAPI HRESULT WINAPI SHGetDesktopFolder(IShellFolder **folder)
{
    if (!folder)
        return E_INVALIDARG;
    *folder = nullptr;
    WCHAR path[MAX_PATH];
    PIDLIST_ABSOLUTE desktop = ShAllocZero(sizeof(USHORT));
    if (!desktop)
        return E_OUTOFMEMORY;
    if (!ShPathFromIdList(desktop, path, MAX_PATH))
        path[0] = 0;
    HRESULT hr = make_folder(desktop, path, FOLDER_DESKTOP, &IID_IShellFolder, (void **)folder);
    ILFree(desktop);
    return hr;
}

/* the name a list shows, as the desktop gives it. */
BOOL ShListDisplayName(PCIDLIST_ABSOLUTE pidl, LPWSTR name)
{
    IShellFolder *desktop;
    if (FAILED(SHGetDesktopFolder(&desktop)))
        return FALSE;
    BOOL ok = item_display_name(folder_of(desktop), pidl, SHGDN_NORMAL, name);
    desktop->lpVtbl->Release(desktop);
    return ok;
}
