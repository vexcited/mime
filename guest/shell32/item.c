/*
 * shell items over file system paths, and the other small shell objects:
 * the task allocator, association handler enumeration and folder browsing.
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

/* ------------------------------------------------------------------ IShellItem */

typedef struct _SH_ITEM {
    IShellItem Interface;
    LONG Refs;
    WCHAR Path[];
} SH_ITEM;

static const IShellItemVtbl g_item_vtbl;

static HRESULT create_item(LPCWSTR path, REFIID riid, void **object)
{
    SIZE_T bytes = (wcslen(path) + 1) * sizeof(WCHAR);
    SH_ITEM *item = ShAlloc(sizeof(*item) + bytes);
    if (!item)
        return E_OUTOFMEMORY;
    item->Interface.lpVtbl = &g_item_vtbl;
    item->Refs = 1;
    memcpy(item->Path, path, bytes);
    HRESULT hr = item->Interface.lpVtbl->QueryInterface(&item->Interface, riid, object);
    item->Interface.lpVtbl->Release(&item->Interface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE item_query_interface(IShellItem *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellItem)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE item_add_ref(IShellItem *This)
{
    return (ULONG)InterlockedIncrement(&((SH_ITEM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE item_release(IShellItem *This)
{
    LONG refs = InterlockedDecrement(&((SH_ITEM *)This)->Refs);
    if (!refs)
        ShFree(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE item_bind_to_handler(IShellItem *This, IBindCtx *context, REFGUID handler,
                                                      REFIID riid, void **object)
{
    if (object)
        *object = NULL;
    return E_NOTIMPL;
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

static HRESULT STDMETHODCALLTYPE item_get_parent(IShellItem *This, IShellItem **parent)
{
    SH_ITEM *item = (SH_ITEM *)This;
    if (!parent)
        return E_INVALIDARG;
    *parent = NULL;
    SIZE_T root = root_length(item->Path), length = wcslen(item->Path);
    if (length <= root)
        return MK_E_NOOBJECT;
    WCHAR path[MAX_PATH];
    if (length >= MAX_PATH)
        return E_FAIL;
    wcscpy(path, item->Path);
    LPWSTR slash = wcsrchr(path, L'\\');
    if (!slash)
        return MK_E_NOOBJECT;
    /* the root keeps its separator. */
    SIZE_T cut = (SIZE_T)(slash - path);
    path[cut < root ? root : cut] = 0;
    return create_item(path, &IID_IShellItem, (void **)parent);
}

/* "file:///c:/a%20b", with the characters a url cannot hold escaped. */
static LPWSTR make_url(LPCWSTR path)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    SIZE_T length = wcslen(path);
    LPWSTR url = ShAlloc((8 + length * 3 + 1) * sizeof(WCHAR));
    if (!url)
        return NULL;
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
    *name = NULL;
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
    LPWSTR path;
    HRESULT hr = other->lpVtbl->GetDisplayName(other, SIGDN_FILESYSPATH, &path);
    if (FAILED(hr))
        return hr;
    int result = _wcsicmp(item->Path, path);
    ShFree(path);
    *order = result < 0 ? -1 : result > 0;
    return result ? S_FALSE : S_OK;
}

static const IShellItemVtbl g_item_vtbl = {
    item_query_interface, item_add_ref,          item_release,        item_bind_to_handler,
    item_get_parent,      item_get_display_name, item_get_attributes, item_compare,
};

SHSTDAPI HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR path, IBindCtx *context, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = NULL;
    if (!path || !riid)
        return E_INVALIDARG;
    WCHAR full[MAX_PATH];
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, NULL);
    if (!n || n >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    /* a trailing separator is not part of the item's name, except at the root. */
    SIZE_T length = wcslen(full);
    if (length > root_length(full) && full[length - 1] == L'\\')
        full[length - 1] = 0;
    return create_item(full, riid, object);
}

SHSTDAPI HRESULT WINAPI SHCreateItemFromIDList(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = NULL;
    WCHAR path[MAX_PATH];
    if (!pidl || !riid || !ShPathFromIdList(pidl, path, MAX_PATH))
        return E_INVALIDARG;
    return create_item(path, riid, object);
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
    *object = NULL;
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
        return NULL;
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
    *object = NULL;
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
    *enumerator = NULL;
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
    return NULL;
}

SHSTDAPI void WINAPI SHChangeNotify(LONG event, UINT flags, LPCVOID item1, LPCVOID item2)
{
}
