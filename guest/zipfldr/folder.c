/*
 * the compressed folder: a zip file as an IShellFolder.
 *
 * the folder of the zip file itself is made by the shell with IPersistFolder::Initialize, given the list of the file.
 * the folders inside it are made by BindToObject and keep the archive of the first. an item is one id: a header with
 * the kind, size and time of the entry and the number of its entry, then its name. an implicit folder, one that only
 * the names of other entries lead through, has no entry. The ids of a folder are listed folders first, each by name.
 */
#include "zipfldr_private.h"

/* the id of an entry. */
#define ID_SIGNATURE 0x465A
#define ID_OFFSET_SIGNATURE 2
#define ID_OFFSET_FOLDER 4
#define ID_OFFSET_SIZE 6
#define ID_OFFSET_TIME 14
#define ID_OFFSET_ENTRY 22
#define ID_HEADER 26

#define SHELL_COMPARE_RESULT(value) ((HRESULT)(USHORT)(SHORT)(value))

/* the details of a file in the order Explorer shows them for a zip file. */
enum { COLUMN_NAME, COLUMN_TYPE, COLUMN_COMPRESSED, COLUMN_PASSWORD, COLUMN_SIZE, COLUMN_RATIO, COLUMN_MODIFIED, COLUMN_COUNT };

static const GUID g_system_key = {0xB725F130, 0x47EF, 0x101A, {0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC}};
#define KEY_TYPE 4
#define KEY_NAME 10
#define KEY_SIZE 12
#define KEY_MODIFIED 14

/* ------------------------------------------------------------------ ids */

static USHORT id_get16(const BYTE *p)
{
    USHORT value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static BOOL id_valid(PCUITEMID_CHILD pidl)
{
    const BYTE *id = (const BYTE *)pidl;
    if (!id)
        return FALSE;
    USHORT size = id_get16(id);
    if (size < ID_HEADER + sizeof(WCHAR) || id_get16(id + ID_OFFSET_SIGNATURE) != ID_SIGNATURE)
        return FALSE;
    const WCHAR *name = (const WCHAR *)(id + ID_HEADER);
    for (SIZE_T i = 0; i < (size - ID_HEADER) / sizeof(WCHAR); i++)
        if (!name[i])
            return TRUE;
    return FALSE;
}

static LPCWSTR id_name(PCUITEMID_CHILD pidl)
{
    return (LPCWSTR)((const BYTE *)pidl + ID_HEADER);
}

static BOOL id_folder(PCUITEMID_CHILD pidl)
{
    return ((const BYTE *)pidl)[ID_OFFSET_FOLDER] != 0;
}

static ULONGLONG id_size(PCUITEMID_CHILD pidl)
{
    ULONGLONG value;
    memcpy(&value, (const BYTE *)pidl + ID_OFFSET_SIZE, sizeof(value));
    return value;
}

static FILETIME id_time(PCUITEMID_CHILD pidl)
{
    FILETIME value;
    memcpy(&value, (const BYTE *)pidl + ID_OFFSET_TIME, sizeof(value));
    return value;
}

static LONG id_entry(PCUITEMID_CHILD pidl)
{
    LONG value;
    memcpy(&value, (const BYTE *)pidl + ID_OFFSET_ENTRY, sizeof(value));
    return value;
}

static PITEMID_CHILD make_id(LPCWSTR name, SIZE_T length, BOOL folder, ULONGLONG size, FILETIME time, LONG entry)
{
    SIZE_T bytes = (ID_HEADER + (length + 1) * sizeof(WCHAR) + 1) & ~(SIZE_T)1;
    if (bytes > 0xFFFF)
        return nullptr;
    BYTE *id = CoTaskMemAlloc(bytes + sizeof(USHORT));
    if (!id)
        return nullptr;
    memset(id, 0, bytes + sizeof(USHORT));
    USHORT cb = (USHORT)bytes, signature = ID_SIGNATURE;
    memcpy(id, &cb, sizeof(cb));
    memcpy(id + ID_OFFSET_SIGNATURE, &signature, sizeof(signature));
    id[ID_OFFSET_FOLDER] = folder ? 1 : 0;
    memcpy(id + ID_OFFSET_SIZE, &size, sizeof(size));
    memcpy(id + ID_OFFSET_TIME, &time, sizeof(time));
    memcpy(id + ID_OFFSET_ENTRY, &entry, sizeof(entry));
    memcpy(id + ID_HEADER, name, length * sizeof(WCHAR));
    return (PITEMID_CHILD)id;
}

/* ------------------------------------------------------------------ the folder */

typedef struct _ZIP_FOLDER {
    IShellFolder2 Folder;
    IPersistFolder2 Persist;
    LONG Refs;
    ZIP_ARCHIVE *Archive;
    /* the list of this folder, the file for the first. */
    PIDLIST_ABSOLUTE Pidl;
    WCHAR Path[MAX_PATH];
    /* the names of the folders above this one with a backslash after each, empty for the first. */
    LPWSTR Prefix;
    SIZE_T PrefixLength;
} ZIP_FOLDER;

static const IShellFolder2Vtbl g_folder_vtbl;
static const IPersistFolder2Vtbl g_persist_vtbl;

static ZIP_FOLDER *folder_of(IShellFolder2 *This)
{
    return (ZIP_FOLDER *)This;
}

static ZIP_FOLDER *folder_of_persist(IPersistFolder2 *This)
{
    return (ZIP_FOLDER *)((BYTE *)This - offsetof(ZIP_FOLDER, Persist));
}

static HRESULT STDMETHODCALLTYPE folder_query_interface(IShellFolder2 *This, REFIID riid, void **object)
{
    ZIP_FOLDER *f = folder_of(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellFolder) || IsEqualIID(riid, &IID_IShellFolder2))
        *object = &f->Folder;
    else if (IsEqualIID(riid, &IID_IPersist) || IsEqualIID(riid, &IID_IPersistFolder) ||
             IsEqualIID(riid, &IID_IPersistFolder2))
        *object = &f->Persist;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&f->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE folder_add_ref(IShellFolder2 *This)
{
    return (ULONG)InterlockedIncrement(&folder_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE folder_release(IShellFolder2 *This)
{
    ZIP_FOLDER *f = folder_of(This);
    LONG refs = InterlockedDecrement(&f->Refs);
    if (!refs) {
        if (f->Archive)
            ZipRelease(f->Archive);
        ILFree(f->Pidl);
        HeapFree(GetProcessHeap(), 0, f->Prefix);
        HeapFree(GetProcessHeap(), 0, f);
    }
    return (ULONG)refs;
}

static ZIP_FOLDER *new_folder(void)
{
    ZIP_FOLDER *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
        return nullptr;
    f->Folder.lpVtbl = &g_folder_vtbl;
    f->Persist.lpVtbl = &g_persist_vtbl;
    f->Refs = 1;
    return f;
}

static BOOL set_prefix(ZIP_FOLDER *f, LPCWSTR above, SIZE_T above_length, LPCWSTR name)
{
    SIZE_T name_length = name ? wcslen(name) + 1 : 0;
    LPWSTR prefix = HeapAlloc(GetProcessHeap(), 0, (above_length + name_length + 1) * sizeof(WCHAR));
    if (!prefix)
        return FALSE;
    memcpy(prefix, above, above_length * sizeof(WCHAR));
    if (name) {
        memcpy(prefix + above_length, name, (name_length - 1) * sizeof(WCHAR));
        prefix[above_length + name_length - 1] = L'\\';
    }
    prefix[above_length + name_length] = 0;
    HeapFree(GetProcessHeap(), 0, f->Prefix);
    f->Prefix = prefix;
    f->PrefixLength = above_length + name_length;
    return TRUE;
}

/* the archive is read when the folder is first used. */
static HRESULT load_archive(ZIP_FOLDER *f)
{
    if (f->Archive)
        return S_OK;
    if (!f->Path[0])
        return E_UNEXPECTED;
    return ZipOpen(f->Path, &f->Archive);
}

/* ------------------------------------------------------------------ IPersistFolder */

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
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_CompressedFolder;
    return S_OK;
}

/* the file is the root of the folder; it is read when the folder is used. */
static HRESULT STDMETHODCALLTYPE persist_initialize(IPersistFolder2 *This, PCIDLIST_ABSOLUTE pidl)
{
    ZIP_FOLDER *f = folder_of_persist(This);
    WCHAR path[MAX_PATH];
    if (!pidl)
        return E_INVALIDARG;
    if (!SHGetPathFromIDListW(pidl, path))
        return E_FAIL;
    PIDLIST_ABSOLUTE copy = ILClone(pidl);
    if (!copy)
        return E_OUTOFMEMORY;
    if (!set_prefix(f, L"", 0, nullptr)) {
        ILFree(copy);
        return E_OUTOFMEMORY;
    }
    ILFree(f->Pidl);
    f->Pidl = copy;
    wcscpy(f->Path, path);
    if (f->Archive) {
        ZipRelease(f->Archive);
        f->Archive = nullptr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE persist_get_cur_folder(IPersistFolder2 *This, PIDLIST_ABSOLUTE *pidl)
{
    ZIP_FOLDER *f = folder_of_persist(This);
    if (!pidl)
        return E_POINTER;
    *pidl = nullptr;
    if (!f->Pidl)
        return E_UNEXPECTED;
    *pidl = ILClone(f->Pidl);
    return *pidl ? S_OK : E_OUTOFMEMORY;
}

static const IPersistFolder2Vtbl g_persist_vtbl = {
    persist_query_interface, persist_add_ref,    persist_release, persist_get_class_id, persist_initialize,
    persist_get_cur_folder,
};

/* ------------------------------------------------------------------ listing */

typedef struct _ID_LIST {
    PITEMID_CHILD *Items;
    ULONG Count, Capacity;
} ID_LIST;

static BOOL list_add(ID_LIST *list, PITEMID_CHILD id)
{
    if (!id)
        return FALSE;
    if (list->Count == list->Capacity) {
        ULONG grown = list->Capacity ? list->Capacity * 2 : 64;
        PITEMID_CHILD *larger = list->Items ? HeapReAlloc(GetProcessHeap(), 0, list->Items, grown * sizeof(*larger))
                                            : HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*larger));
        if (!larger) {
            CoTaskMemFree(id);
            return FALSE;
        }
        list->Items = larger;
        list->Capacity = grown;
    }
    list->Items[list->Count++] = id;
    return TRUE;
}

static void list_free(ID_LIST *list)
{
    for (ULONG i = 0; i < list->Count; i++)
        CoTaskMemFree(list->Items[i]);
    HeapFree(GetProcessHeap(), 0, list->Items);
}

/* the entries one level below the prefix; the folders come first. */
static HRESULT list_children(ZIP_FOLDER *f, SHCONTF flags, ID_LIST *list)
{
    ZIP_ARCHIVE *a = f->Archive;
    SIZE_T plen = f->PrefixLength;
    for (int pass = 0; pass < 2; pass++) {
        BOOL folders = pass == 0;
        if (!(flags & (folders ? SHCONTF_FOLDERS : SHCONTF_NONFOLDERS)))
            continue;
        ULONG i = ZipLowerBound(a, f->Prefix, plen);
        while (i < a->Count) {
            const ZIP_ENTRY *e = &a->Entries[a->Order[i]];
            if (!ZipHasPrefix(e->Name, f->Prefix, plen))
                break;
            LPCWSTR rest = e->Name + plen;
            if (!*rest) {
                i++;
                continue;
            }
            LPCWSTR separator = wcschr(rest, L'\\');
            if (separator) {
                /* a folder that the names below it lead through, with all of its entries passed. */
                SIZE_T length = (SIZE_T)(separator - rest), full = plen + length;
                ULONG next = i + 1;
                while (next < a->Count) {
                    const ZIP_ENTRY *n = &a->Entries[a->Order[next]];
                    if (n->Length <= full || n->Name[full] != L'\\' || !ZipHasPrefix(n->Name, e->Name, full))
                        break;
                    next++;
                }
                LONG explicit_entry = ZipFind(a, e->Name, full);
                if (folders && !(explicit_entry >= 0 && a->Entries[explicit_entry].Folder)) {
                    FILETIME none = {};
                    if (!list_add(list, make_id(rest, length, TRUE, 0, none, -1)))
                        return E_OUTOFMEMORY;
                }
                i = next;
                continue;
            }
            if (e->Folder == folders &&
                !list_add(list, make_id(rest, e->Length - plen, e->Folder, e->Size, e->Time, (LONG)a->Order[i])))
                return E_OUTOFMEMORY;
            i++;
        }
    }
    return S_OK;
}

/* ------------------------------------------------------------------ enumerator */

typedef struct _ZIP_ENUM {
    IEnumIDList Interface;
    LONG Refs;
    ID_LIST List;
    ULONG Position;
} ZIP_ENUM;

static const IEnumIDListVtbl g_enum_vtbl;

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumIDList *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumIDList))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((ZIP_ENUM *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumIDList *This)
{
    return (ULONG)InterlockedIncrement(&((ZIP_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumIDList *This)
{
    ZIP_ENUM *e = (ZIP_ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        list_free(&e->List);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return (ULONG)refs;
}

static PITEMID_CHILD copy_id(PCUITEMID_CHILD pidl)
{
    USHORT size = id_get16((const BYTE *)pidl);
    BYTE *copy = CoTaskMemAlloc((SIZE_T)size + sizeof(USHORT));
    if (copy) {
        memcpy(copy, pidl, size);
        memset(copy + size, 0, sizeof(USHORT));
    }
    return (PITEMID_CHILD)copy;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumIDList *This, ULONG count, PITEMID_CHILD *items, ULONG *fetched)
{
    ZIP_ENUM *e = (ZIP_ENUM *)This;
    ULONG done = 0;
    if (!items && count)
        return E_POINTER;
    while (done < count && e->Position < e->List.Count) {
        items[done] = copy_id(e->List.Items[e->Position]);
        if (!items[done]) {
            while (done)
                CoTaskMemFree(items[--done]);
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
    ZIP_ENUM *e = (ZIP_ENUM *)This;
    ULONG left = e->List.Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumIDList *This)
{
    ((ZIP_ENUM *)This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumIDList *This, IEnumIDList **clone)
{
    ZIP_ENUM *e = (ZIP_ENUM *)This;
    if (!clone)
        return E_POINTER;
    *clone = nullptr;
    ZIP_ENUM *copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*copy));
    if (!copy)
        return E_OUTOFMEMORY;
    copy->Interface.lpVtbl = &g_enum_vtbl;
    copy->Refs = 1;
    for (ULONG i = 0; i < e->List.Count; i++) {
        if (!list_add(&copy->List, copy_id(e->List.Items[i]))) {
            enum_release(&copy->Interface);
            return E_OUTOFMEMORY;
        }
    }
    copy->Position = e->Position;
    *clone = &copy->Interface;
    return S_OK;
}

static const IEnumIDListVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

/* ------------------------------------------------------------------ IShellFolder */

static HRESULT STDMETHODCALLTYPE folder_enum_objects(IShellFolder2 *This, HWND hwnd, SHCONTF flags,
                                                     IEnumIDList **enumerator)
{
    ZIP_FOLDER *f = folder_of(This);
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    HRESULT hr = load_archive(f);
    if (FAILED(hr))
        return hr;
    ZIP_ENUM *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Interface.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    hr = list_children(f, flags, &e->List);
    if (FAILED(hr)) {
        enum_release(&e->Interface);
        return hr;
    }
    *enumerator = &e->Interface;
    return S_OK;
}

/* whether any entry is below the folder of a path. */
static BOOL folder_has_children(const ZIP_ARCHIVE *a, LPCWSTR path, SIZE_T length)
{
    WCHAR *prefix = HeapAlloc(GetProcessHeap(), 0, (length + 2) * sizeof(WCHAR));
    if (!prefix)
        return FALSE;
    memcpy(prefix, path, length * sizeof(WCHAR));
    prefix[length] = L'\\';
    prefix[length + 1] = 0;
    ULONG at = ZipLowerBound(a, prefix, length + 1);
    BOOL found = at < a->Count && ZipHasPrefix(a->Entries[a->Order[at]].Name, prefix, length + 1);
    HeapFree(GetProcessHeap(), 0, prefix);
    return found;
}

/* a name inside the folder, through any folders in it, as the ids that lead to it. */
static HRESULT STDMETHODCALLTYPE folder_parse_display_name(IShellFolder2 *This, HWND hwnd, IBindCtx *context,
                                                           LPWSTR name, ULONG *eaten, PIDLIST_RELATIVE *pidl,
                                                           ULONG *attributes)
{
    ZIP_FOLDER *f = folder_of(This);
    if (!pidl)
        return E_POINTER;
    *pidl = nullptr;
    if (eaten)
        *eaten = 0;
    if (!name || !name[0])
        return E_INVALIDARG;
    HRESULT hr = load_archive(f);
    if (FAILED(hr))
        return hr;
    SIZE_T name_length = wcslen(name), full_size = f->PrefixLength + name_length + 1;
    WCHAR *full = HeapAlloc(GetProcessHeap(), 0, (full_size + 1) * sizeof(WCHAR));
    if (!full)
        return E_OUTOFMEMORY;
    memcpy(full, f->Prefix, f->PrefixLength * sizeof(WCHAR));
    SIZE_T length = f->PrefixLength;
    SIZE_T i = 0;
    PIDLIST_RELATIVE result = nullptr;
    hr = S_OK;
    while (SUCCEEDED(hr) && i < name_length) {
        while (i < name_length && (name[i] == L'\\' || name[i] == L'/'))
            i++;
        SIZE_T start = i;
        while (i < name_length && name[i] != L'\\' && name[i] != L'/')
            i++;
        if (i == start)
            break;
        BOOL last = TRUE;
        for (SIZE_T j = i; j < name_length; j++)
            if (name[j] != L'\\' && name[j] != L'/')
                last = FALSE;
        SIZE_T part = i - start;
        if (length > f->PrefixLength)
            full[length++] = L'\\';
        SIZE_T at = length;
        memcpy(full + length, name + start, part * sizeof(WCHAR));
        length += part;
        full[length] = 0;
        LONG number = ZipFind(f->Archive, full, length);
        BOOL folder = !last;
        ULONGLONG size = 0;
        FILETIME time = {};
        if (number >= 0) {
            const ZIP_ENTRY *e = &f->Archive->Entries[number];
            folder = folder || e->Folder;
            size = e->Size;
            time = e->Time;
        } else if (folder_has_children(f->Archive, full, length)) {
            folder = TRUE;
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            break;
        }
        PITEMID_CHILD id = make_id(full + at, part, folder, size, time, number);
        if (!id) {
            hr = E_OUTOFMEMORY;
            break;
        }
        PIDLIST_RELATIVE grown = ILAppendID(result, (const SHITEMID *)id, TRUE);
        CoTaskMemFree(id);
        if (!grown) {
            hr = E_OUTOFMEMORY;
            break;
        }
        result = grown;
        if (last)
            break;
    }
    HeapFree(GetProcessHeap(), 0, full);
    if (SUCCEEDED(hr) && !result)
        hr = E_INVALIDARG;
    if (FAILED(hr)) {
        ILFree(result);
        return hr;
    }
    if (eaten)
        *eaten = (ULONG)name_length;
    if (attributes) {
        const ITEMIDLIST *last = ILFindLastID(result);
        *attributes &= id_folder(last) ? (SFGAO_FOLDER | SFGAO_CANCOPY) : (SFGAO_STREAM | SFGAO_CANCOPY);
    }
    *pidl = result;
    return S_OK;
}

/* a folder in this one, for the id of a folder. */
static HRESULT bind_folder(ZIP_FOLDER *f, PCUITEMID_CHILD id, REFIID riid, void **object)
{
    ZIP_FOLDER *child = new_folder();
    if (!child)
        return E_OUTOFMEMORY;
    child->Pidl = ILCombine(f->Pidl, id);
    wcscpy(child->Path, f->Path);
    child->Archive = f->Archive;
    ZipAddRef(child->Archive);
    HRESULT hr = child->Pidl && set_prefix(child, f->Prefix, f->PrefixLength, id_name(id)) ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
        hr = folder_query_interface(&child->Folder, riid, object);
    folder_release(&child->Folder);
    return hr;
}

static HRESULT STDMETHODCALLTYPE folder_bind_to_object(IShellFolder2 *This, PCUIDLIST_RELATIVE pidl, IBindCtx *context,
                                                       REFIID riid, void **object)
{
    ZIP_FOLDER *f = folder_of(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!pidl || !id_valid(pidl))
        return E_INVALIDARG;
    HRESULT hr = load_archive(f);
    if (FAILED(hr))
        return hr;
    PCUIDLIST_RELATIVE rest = ILGetNext(pidl);
    if (rest && rest->mkid.cb) {
        if (!id_folder(pidl))
            return E_INVALIDARG;
        IShellFolder2 *inner;
        hr = bind_folder(f, pidl, &IID_IShellFolder2, (void **)&inner);
        if (SUCCEEDED(hr)) {
            hr = inner->lpVtbl->BindToObject(inner, rest, context, riid, object);
            inner->lpVtbl->Release(inner);
        }
        return hr;
    }
    if (id_folder(pidl))
        return bind_folder(f, pidl, riid, object);
    if (!IsEqualIID(riid, &IID_IStream))
        return E_NOINTERFACE;
    LONG number = id_entry(pidl);
    if (number < 0)
        return E_INVALIDARG;
    return ZipOpenStream(f->Archive, (ULONG)number, (IStream **)object);
}

/* the name of an id order: folders first, then by the column asked. */
static int compare_ids(PCUITEMID_CHILD a, PCUITEMID_CHILD b, UINT column)
{
    if (id_folder(a) != id_folder(b))
        return id_folder(a) ? -1 : 1;
    if (column == COLUMN_SIZE) {
        ULONGLONG x = id_size(a), y = id_size(b);
        if (x != y)
            return x < y ? -1 : 1;
    } else if (column == COLUMN_MODIFIED) {
        FILETIME x = id_time(a), y = id_time(b);
        int d = CompareFileTime(&x, &y);
        if (d)
            return d;
    }
    LPCWSTR x = id_name(a), y = id_name(b);
    return ZipCompare(x, wcslen(x), y, wcslen(y));
}

static HRESULT STDMETHODCALLTYPE folder_compare_ids(IShellFolder2 *This, LPARAM column, PCUIDLIST_RELATIVE a,
                                                    PCUIDLIST_RELATIVE b)
{
    if (!a || !b || !id_valid(a) || !id_valid(b))
        return E_INVALIDARG;
    for (;;) {
        int d = compare_ids(a, b, (UINT)(column & 0xFFFF));
        if (d)
            return SHELL_COMPARE_RESULT(d);
        PCUIDLIST_RELATIVE next_a = ILGetNext(a), next_b = ILGetNext(b);
        BOOL more_a = next_a && next_a->mkid.cb, more_b = next_b && next_b->mkid.cb;
        if (!more_a || !more_b)
            return SHELL_COMPARE_RESULT(more_a ? 1 : (more_b ? -1 : 0));
        a = next_a;
        b = next_b;
        if (!id_valid(a) || !id_valid(b))
            return E_INVALIDARG;
    }
}

static HRESULT STDMETHODCALLTYPE folder_create_view_object(IShellFolder2 *This, HWND hwnd, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE folder_get_attributes_of(IShellFolder2 *This, UINT count, PCUITEMID_CHILD const *items,
                                                          SFGAOF *attributes)
{
    if (!attributes)
        return E_POINTER;
    SFGAOF common = *attributes;
    for (UINT i = 0; i < count; i++) {
        if (!id_valid(items[i]))
            return E_INVALIDARG;
        common &= id_folder(items[i]) ? (SFGAO_FOLDER | SFGAO_CANCOPY) : (SFGAO_STREAM | SFGAO_CANCOPY);
    }
    *attributes &= common;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_get_ui_object_of(IShellFolder2 *This, HWND hwnd, UINT count,
                                                         PCUITEMID_CHILD const *items, REFIID riid, UINT *reserved,
                                                         void **object)
{
    if (object)
        *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE folder_get_display_name_of(IShellFolder2 *This, PCUITEMID_CHILD pidl, SHGDNF flags,
                                                            STRRET *name)
{
    ZIP_FOLDER *f = folder_of(This);
    if (!name)
        return E_POINTER;
    if (!id_valid(pidl))
        return E_INVALIDARG;
    LPCWSTR own = id_name(pidl);
    SIZE_T own_length = wcslen(own), full = 0;
    BOOL path = (flags & SHGDN_FORPARSING) && !(flags & SHGDN_INFOLDER);
    if (path)
        full = wcslen(f->Path) + 1 + f->PrefixLength;
    LPWSTR text = CoTaskMemAlloc((full + own_length + 1) * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    if (path) {
        wcscpy(text, f->Path);
        text[wcslen(f->Path)] = L'\\';
        memcpy(text + wcslen(f->Path) + 1, f->Prefix, f->PrefixLength * sizeof(WCHAR));
    }
    wcscpy(text + full, own);
    name->uType = STRRET_WSTR;
    name->pOleStr = text;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_set_name_of(IShellFolder2 *This, HWND hwnd, PCUITEMID_CHILD pidl, LPCWSTR name,
                                                    SHGDNF flags, PITEMID_CHILD *result)
{
    if (result)
        *result = nullptr;
    return E_ACCESSDENIED;
}

/* ------------------------------------------------------------------ IShellFolder2 */

static HRESULT STDMETHODCALLTYPE folder_get_default_search_guid(IShellFolder2 *This, GUID *guid)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE folder_enum_searches(IShellFolder2 *This, IUnknown **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE folder_get_default_column(IShellFolder2 *This, DWORD reserved, ULONG *sort,
                                                           ULONG *display)
{
    if (sort)
        *sort = COLUMN_NAME;
    if (display)
        *display = COLUMN_NAME;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_get_default_column_state(IShellFolder2 *This, UINT column, SHCOLSTATEF *state)
{
    if (!state)
        return E_POINTER;
    if (column >= COLUMN_COUNT)
        return E_INVALIDARG;
    *state = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT;
    return S_OK;
}

static DATE id_date(PCUITEMID_CHILD pidl)
{
    FILETIME time = id_time(pidl), local;
    SYSTEMTIME system;
    DATE date = 0;
    if (!FileTimeToLocalFileTime(&time, &local) || !FileTimeToSystemTime(&local, &system) ||
        !SystemTimeToVariantTime(&system, &date))
        date = 0;
    return date;
}

static HRESULT STDMETHODCALLTYPE folder_get_details_ex(IShellFolder2 *This, PCUITEMID_CHILD pidl,
                                                       const SHCOLUMNID *column, VARIANT *value)
{
    if (!column || !value)
        return E_POINTER;
    VariantInit(value);
    if (!id_valid(pidl))
        return E_INVALIDARG;
    if (!IsEqualGUID(&column->fmtid, &g_system_key))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    switch (column->pid) {
    case KEY_NAME:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(id_name(pidl));
        return value->bstrVal ? S_OK : E_OUTOFMEMORY;
    case KEY_SIZE:
        value->vt = VT_UI8;
        value->ullVal = id_folder(pidl) ? 0 : id_size(pidl);
        return S_OK;
    case KEY_MODIFIED:
        value->vt = VT_DATE;
        value->date = id_date(pidl);
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static HRESULT details_text(SHELLDETAILS *details, LPCWSTR text)
{
    details->str.uType = STRRET_WSTR;
    details->str.pOleStr = CoTaskMemAlloc((wcslen(text) + 1) * sizeof(WCHAR));
    if (!details->str.pOleStr)
        return E_OUTOFMEMORY;
    wcscpy(details->str.pOleStr, text);
    return S_OK;
}

static void type_text(PCUITEMID_CHILD pidl, WCHAR *text, SIZE_T chars)
{
    LPCWSTR name = id_name(pidl), dot = wcsrchr(name, L'.');
    if (id_folder(pidl)) {
        _snwprintf(text, chars, L"File folder");
    } else if (dot && dot[1] && wcslen(dot) < 16) {
        WCHAR upper[16];
        wcscpy(upper, dot + 1);
        for (WCHAR *p = upper; *p; p++)
            *p = towupper(*p);
        _snwprintf(text, chars, L"%ls File", upper);
    } else {
        _snwprintf(text, chars, L"File");
    }
    text[chars - 1] = 0;
}

/* the percent a file was made smaller, worked out on halved sizes while they are too large for 32 bits. */
static ULONG saved_percent(ULONGLONG size, ULONGLONG compressed)
{
    if (!size || compressed >= size)
        return 0;
    ULONGLONG saved = size - compressed;
    while (size > 0x28F5C28u) {
        size >>= 1;
        saved >>= 1;
    }
    return size ? (ULONG)saved * 100 / (ULONG)size : 0;
}

static HRESULT STDMETHODCALLTYPE folder_get_details_of(IShellFolder2 *This, PCUITEMID_CHILD pidl, UINT column,
                                                       SHELLDETAILS *details)
{
    static const LPCWSTR titles[COLUMN_COUNT] = {L"Name",  L"Type", L"Compressed size", L"Password protected",
                                                 L"Size",  L"Ratio", L"Date modified"};
    static const int widths[COLUMN_COUNT] = {30, 20, 16, 16, 12, 8, 20};
    ZIP_FOLDER *f = folder_of(This);
    WCHAR text[96] = L"";
    if (!details)
        return E_POINTER;
    memset(details, 0, sizeof(*details));
    if (column >= COLUMN_COUNT)
        return E_INVALIDARG;
    details->fmt = column == COLUMN_COMPRESSED || column == COLUMN_SIZE || column == COLUMN_RATIO ? 1 : 0;
    details->cxChar = widths[column];
    if (!pidl)
        return details_text(details, titles[column]);
    if (!id_valid(pidl))
        return E_INVALIDARG;
    LONG number = id_entry(pidl);
    const ZIP_ENTRY *entry = f->Archive && number >= 0 && (ULONG)number < f->Archive->Count
                                 ? &f->Archive->Entries[number]
                                 : nullptr;
    BOOL folder = id_folder(pidl);
    switch (column) {
    case COLUMN_NAME:
        _snwprintf(text, RTL_NUMBER_OF(text), L"%ls", id_name(pidl));
        break;
    case COLUMN_TYPE:
        type_text(pidl, text, RTL_NUMBER_OF(text));
        break;
    case COLUMN_COMPRESSED:
        if (entry && !folder)
            _snwprintf(text, RTL_NUMBER_OF(text), L"%lu KB", (ULONG)((entry->CompressedSize + 1023) >> 10));
        break;
    case COLUMN_PASSWORD:
        if (!folder)
            _snwprintf(text, RTL_NUMBER_OF(text), L"%ls", entry && entry->Encrypted ? L"Yes" : L"No");
        break;
    case COLUMN_SIZE:
        if (!folder)
            _snwprintf(text, RTL_NUMBER_OF(text), L"%lu KB", (ULONG)((id_size(pidl) + 1023) >> 10));
        break;
    case COLUMN_RATIO:
        if (entry && !folder)
            _snwprintf(text, RTL_NUMBER_OF(text), L"%lu%%", saved_percent(entry->Size, entry->CompressedSize));
        break;
    case COLUMN_MODIFIED: {
        FILETIME time = id_time(pidl), local;
        SYSTEMTIME system;
        WCHAR date[32], clock[32];
        if ((time.dwLowDateTime || time.dwHighDateTime) && FileTimeToLocalFileTime(&time, &local) &&
            FileTimeToSystemTime(&local, &system) &&
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &system, nullptr, date, RTL_NUMBER_OF(date)) &&
            GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &system, nullptr, clock, RTL_NUMBER_OF(clock)))
            _snwprintf(text, RTL_NUMBER_OF(text), L"%ls %ls", date, clock);
        break;
    }
    }
    text[RTL_NUMBER_OF(text) - 1] = 0;
    return details_text(details, text);
}

static HRESULT STDMETHODCALLTYPE folder_map_column_to_scid(IShellFolder2 *This, UINT column, SHCOLUMNID *id)
{
    if (!id)
        return E_POINTER;
    id->fmtid = g_system_key;
    switch (column) {
    case COLUMN_NAME:
        id->pid = KEY_NAME;
        return S_OK;
    case COLUMN_TYPE:
        id->pid = KEY_TYPE;
        return S_OK;
    case COLUMN_SIZE:
        id->pid = KEY_SIZE;
        return S_OK;
    case COLUMN_MODIFIED:
        id->pid = KEY_MODIFIED;
        return S_OK;
    }
    return E_INVALIDARG;
}

static const IShellFolder2Vtbl g_folder_vtbl = {
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
    folder_get_default_search_guid,
    folder_enum_searches,
    folder_get_default_column,
    folder_get_default_column_state,
    folder_get_details_ex,
    folder_get_details_of,
    folder_map_column_to_scid,
};

HRESULT ZipFolderCreate(REFIID riid, void **object)
{
    ZIP_FOLDER *f = new_folder();
    if (!f)
        return E_OUTOFMEMORY;
    HRESULT hr = set_prefix(f, L"", 0, nullptr) ? folder_query_interface(&f->Folder, riid, object) : E_OUTOFMEMORY;
    folder_release(&f->Folder);
    return hr;
}
