/*
 * jump lists: the list of destinations a program gives the taskbar button of its windows (ICustomDestinationList), the
 * collections it builds them in (CLSID_EnumerableObjectCollection), and the two classes that remove destinations and
 * read the recent and frequent lists. there is no taskbar to show a list, so a committed list is kept for the
 * life of the process, by app id, and the document lists are empty. the errors of a list used out of order are
 * guesses.
 */
#include "windows_storage_private.h"
#include <mime/win32/shobjidl_ui.h>
#include <mime/win32/propsys.h>
#include <mime/shellclient.h>

#define MAX_APPS 16
#define DEFAULT_SLOTS 10

/* ------------------------------------------------------------------ the collection */

typedef struct _COLLECTION {
    IObjectCollection Interface;
    LONG Refs;
    IUnknown **Items;
    UINT Count, Capacity;
} COLLECTION;

static const IObjectCollectionVtbl g_collection_vtbl;

static COLLECTION *collection_of(IObjectCollection *This)
{
    return (COLLECTION *)This;
}

static HRESULT STDMETHODCALLTYPE col_query_interface(IObjectCollection *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IObjectArray) && !IsEqualIID(riid, &IID_IObjectCollection))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&collection_of(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE col_add_ref(IObjectCollection *This)
{
    return (ULONG)InterlockedIncrement(&collection_of(This)->Refs);
}

static void col_clear(COLLECTION *c)
{
    for (UINT i = 0; i < c->Count; i++)
        c->Items[i]->lpVtbl->Release(c->Items[i]);
    c->Count = 0;
}

static ULONG STDMETHODCALLTYPE col_release(IObjectCollection *This)
{
    COLLECTION *c = collection_of(This);
    LONG refs = InterlockedDecrement(&c->Refs);
    if (!refs) {
        col_clear(c);
        HeapFree(GetProcessHeap(), 0, c->Items);
        HeapFree(GetProcessHeap(), 0, c);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE col_get_count(IObjectCollection *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = collection_of(This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE col_get_at(IObjectCollection *This, UINT index, REFIID riid, void **object)
{
    COLLECTION *c = collection_of(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (index >= c->Count)
        return E_INVALIDARG;
    return c->Items[index]->lpVtbl->QueryInterface(c->Items[index], riid, object);
}

static HRESULT STDMETHODCALLTYPE col_add_object(IObjectCollection *This, IUnknown *object)
{
    COLLECTION *c = collection_of(This);
    if (!object)
        return E_INVALIDARG;
    if (c->Count == c->Capacity) {
        UINT capacity = c->Capacity ? c->Capacity * 2 : 8;
        IUnknown **grown = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*grown));
        if (!grown)
            return E_OUTOFMEMORY;
        if (c->Items) {
            memcpy(grown, c->Items, c->Count * sizeof(*grown));
            HeapFree(GetProcessHeap(), 0, c->Items);
        }
        c->Items = grown;
        c->Capacity = capacity;
    }
    object->lpVtbl->AddRef(object);
    c->Items[c->Count++] = object;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE col_add_from_array(IObjectCollection *This, IObjectArray *source)
{
    if (!source)
        return E_INVALIDARG;
    UINT count = 0;
    HRESULT hr = source->lpVtbl->GetCount(source, &count);
    for (UINT i = 0; SUCCEEDED(hr) && i < count; i++) {
        IUnknown *item = nullptr;
        hr = source->lpVtbl->GetAt(source, i, &IID_IUnknown, (void **)&item);
        if (SUCCEEDED(hr)) {
            hr = col_add_object(This, item);
            item->lpVtbl->Release(item);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE col_remove_at(IObjectCollection *This, UINT index)
{
    COLLECTION *c = collection_of(This);
    if (index >= c->Count)
        return E_INVALIDARG;
    c->Items[index]->lpVtbl->Release(c->Items[index]);
    memmove(c->Items + index, c->Items + index + 1, (c->Count - index - 1) * sizeof(*c->Items));
    c->Count--;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE col_clear_all(IObjectCollection *This)
{
    col_clear(collection_of(This));
    return S_OK;
}

static const IObjectCollectionVtbl g_collection_vtbl = {
    col_query_interface, col_add_ref, col_release, col_get_count, col_get_at, col_add_object, col_add_from_array,
    col_remove_at, col_clear_all,
};

HRESULT ObjectCollectionCreate(IUnknown *outer, REFIID riid, void **object)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    COLLECTION *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->Interface.lpVtbl = &g_collection_vtbl;
    c->Refs = 1;
    HRESULT hr = col_query_interface(&c->Interface, riid, object);
    col_release(&c->Interface);
    return hr;
}

/* an empty array for the destinations nothing removed, as the interface asked for. */
static HRESULT empty_array(REFIID riid, void **object)
{
    return ObjectCollectionCreate(nullptr, riid, object);
}

/* ------------------------------------------------------------------ the destination list */

typedef struct _CATEGORY {
    LPWSTR Name; /* null for a known category */
    KNOWNDESTCATEGORY Known;
    IObjectArray *Items;
} CATEGORY;

typedef struct _APP_LIST {
    WCHAR AppId[129];
    CATEGORY *Categories;
    UINT CategoryCount;
    IObjectArray *Tasks;
} APP_LIST;

/* the committed lists, kept by app id. */
static SRWLOCK g_lists_lock = SRWLOCK_INIT;
static APP_LIST g_lists[MAX_APPS];

typedef struct _DESTINATION_LIST {
    ICustomDestinationList Interface;
    LONG Refs;
    WCHAR AppId[129];
    BOOL InList;
    CATEGORY *Categories;
    UINT CategoryCount, CategoryCapacity;
    IObjectArray *Tasks;
} DESTINATION_LIST;

static DESTINATION_LIST *list_of(ICustomDestinationList *This)
{
    return (DESTINATION_LIST *)This;
}

static void free_categories(CATEGORY *categories, UINT count)
{
    for (UINT i = 0; i < count; i++) {
        HeapFree(GetProcessHeap(), 0, categories[i].Name);
        if (categories[i].Items)
            categories[i].Items->lpVtbl->Release(categories[i].Items);
    }
    HeapFree(GetProcessHeap(), 0, categories);
}

static void drop_pending(DESTINATION_LIST *l)
{
    free_categories(l->Categories, l->CategoryCount);
    l->Categories = nullptr;
    l->CategoryCount = l->CategoryCapacity = 0;
    if (l->Tasks)
        l->Tasks->lpVtbl->Release(l->Tasks);
    l->Tasks = nullptr;
    l->InList = FALSE;
}

static LPWSTR copy_string(LPCWSTR text)
{
    SIZE_T size = (wcslen(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = HeapAlloc(GetProcessHeap(), 0, size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static HRESULT STDMETHODCALLTYPE dl_query_interface(ICustomDestinationList *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ICustomDestinationList))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&list_of(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE dl_add_ref(ICustomDestinationList *This)
{
    return (ULONG)InterlockedIncrement(&list_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE dl_release(ICustomDestinationList *This)
{
    DESTINATION_LIST *l = list_of(This);
    LONG refs = InterlockedDecrement(&l->Refs);
    if (!refs) {
        drop_pending(l);
        HeapFree(GetProcessHeap(), 0, l);
    }
    return (ULONG)refs;
}

/* the app id the list is for: the one given, else the process's explicit one. */
static void resolve_app_id(DESTINATION_LIST *l, WCHAR *id)
{
    if (l->AppId[0]) {
        wcscpy(id, l->AppId);
        return;
    }
    id[0] = 0;
    typedef HRESULT(WINAPI * GET_ID)(LPWSTR *);
    HMODULE shell32 = LoadLibraryW(L"shell32.dll");
    GET_ID get = shell32 ? (GET_ID)(void *)GetProcAddress(shell32, "GetCurrentProcessExplicitAppUserModelID") : nullptr;
    LPWSTR explicit_id = nullptr;
    if (get && SUCCEEDED(get(&explicit_id)) && explicit_id) {
        if (wcslen(explicit_id) < 129)
            wcscpy(id, explicit_id);
        HeapFree(GetProcessHeap(), 0, explicit_id);
    }
    if (!id[0])
        wcscpy(id, L"Mime.Process");
}

static HRESULT STDMETHODCALLTYPE dl_set_app_id(ICustomDestinationList *This, LPCWSTR app_id)
{
    DESTINATION_LIST *l = list_of(This);
    if (!app_id || !*app_id || wcslen(app_id) >= 129)
        return E_INVALIDARG;
    if (l->InList)
        return E_UNEXPECTED;
    wcscpy(l->AppId, app_id);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dl_begin_list(ICustomDestinationList *This, UINT *min_slots, REFIID riid, void **removed)
{
    DESTINATION_LIST *l = list_of(This);
    if (!min_slots || !removed)
        return E_POINTER;
    *removed = nullptr;
    if (l->InList)
        return E_UNEXPECTED;
    HRESULT hr = empty_array(riid, removed);
    if (FAILED(hr))
        return hr;
    drop_pending(l);
    l->InList = TRUE;
    *min_slots = DEFAULT_SLOTS;
    return S_OK;
}

/* an item a list takes: a shell link or a shell item. */
static BOOL takes_item(IUnknown *item, BOOL tasks)
{
    void *found = nullptr;
    if (SUCCEEDED(item->lpVtbl->QueryInterface(item, &IID_IShellLinkW, &found))) {
        ((IUnknown *)found)->lpVtbl->Release((IUnknown *)found);
        return TRUE;
    }
    if (!tasks && SUCCEEDED(item->lpVtbl->QueryInterface(item, &IID_IShellItem, &found))) {
        ((IUnknown *)found)->lpVtbl->Release((IUnknown *)found);
        return TRUE;
    }
    return FALSE;
}

static BOOL array_takes(IObjectArray *items, BOOL tasks)
{
    UINT count = 0;
    if (FAILED(items->lpVtbl->GetCount(items, &count)))
        return FALSE;
    for (UINT i = 0; i < count; i++) {
        IUnknown *item = nullptr;
        if (FAILED(items->lpVtbl->GetAt(items, i, &IID_IUnknown, (void **)&item)))
            return FALSE;
        BOOL ok = takes_item(item, tasks);
        item->lpVtbl->Release(item);
        if (!ok)
            return FALSE;
    }
    return TRUE;
}

static CATEGORY *add_category(DESTINATION_LIST *l)
{
    if (l->CategoryCount == l->CategoryCapacity) {
        UINT capacity = l->CategoryCapacity ? l->CategoryCapacity * 2 : 4;
        CATEGORY *grown = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(*grown));
        if (!grown)
            return nullptr;
        if (l->Categories) {
            memcpy(grown, l->Categories, l->CategoryCount * sizeof(*grown));
            HeapFree(GetProcessHeap(), 0, l->Categories);
        }
        l->Categories = grown;
        l->CategoryCapacity = capacity;
    }
    return &l->Categories[l->CategoryCount++];
}

static HRESULT STDMETHODCALLTYPE dl_append_category(ICustomDestinationList *This, LPCWSTR name, IObjectArray *items)
{
    DESTINATION_LIST *l = list_of(This);
    if (!l->InList)
        return E_UNEXPECTED;
    if (!name || !*name || !items || !array_takes(items, FALSE))
        return E_INVALIDARG;
    CATEGORY *category = add_category(l);
    if (!category)
        return E_OUTOFMEMORY;
    category->Name = copy_string(name);
    category->Items = items;
    items->lpVtbl->AddRef(items);
    return category->Name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE dl_append_known(ICustomDestinationList *This, KNOWNDESTCATEGORY known)
{
    DESTINATION_LIST *l = list_of(This);
    if (!l->InList)
        return E_UNEXPECTED;
    if (known != KDC_FREQUENT && known != KDC_RECENT)
        return E_INVALIDARG;
    CATEGORY *category = add_category(l);
    if (!category)
        return E_OUTOFMEMORY;
    category->Known = known;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dl_add_user_tasks(ICustomDestinationList *This, IObjectArray *tasks)
{
    DESTINATION_LIST *l = list_of(This);
    if (!l->InList)
        return E_UNEXPECTED;
    if (!tasks || !array_takes(tasks, TRUE))
        return E_INVALIDARG;
    if (l->Tasks)
        l->Tasks->lpVtbl->Release(l->Tasks);
    l->Tasks = tasks;
    tasks->lpVtbl->AddRef(tasks);
    return S_OK;
}

/* the list of an app id, made when there is none, and the one that was kept replaced. */
static APP_LIST *app_list(const WCHAR *id, BOOL create)
{
    APP_LIST *free_slot = nullptr;
    for (int i = 0; i < MAX_APPS; i++) {
        if (!g_lists[i].AppId[0]) {
            if (!free_slot)
                free_slot = &g_lists[i];
        } else if (!_wcsicmp(g_lists[i].AppId, id)) {
            return &g_lists[i];
        }
    }
    if (create && free_slot) {
        wcscpy(free_slot->AppId, id);
        return free_slot;
    }
    return nullptr;
}

static void clear_app_list(APP_LIST *a)
{
    free_categories(a->Categories, a->CategoryCount);
    if (a->Tasks)
        a->Tasks->lpVtbl->Release(a->Tasks);
    memset(a, 0, sizeof(*a));
}

/*
 * the tasks of a committed list go to the host, which can show them in the menu of the launcher entry of the program:
 * the title of a task is its System.Title, else its description, and what is run is the shortcut's target with its
 * arguments. the categories of destinations (documents, frequent and recent) have no place in a desktop's menu.
 */
static void send_tasks(const WCHAR *id, IObjectArray *tasks)
{
    MimeShellJumpList *j = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*j));
    if (!j)
        return;
    j->op = MIME_SHELL_JUMPLIST;
    MimeShellCopyText(j->aumid, sizeof(j->aumid) / sizeof(j->aumid[0]), id);
    UINT count = 0;
    if (tasks)
        tasks->lpVtbl->GetCount(tasks, &count);
    for (UINT i = 0; i < count && j->task_count < MIME_JUMPLIST_TASKS; i++) {
        IShellLinkW *link;
        if (FAILED(tasks->lpVtbl->GetAt(tasks, i, &IID_IShellLinkW, (void **)&link)))
            continue;
        MimeJumpTask *t = &j->tasks[j->task_count];
        WCHAR text[260];
        text[0] = 0;
        link->lpVtbl->GetPath(link, text, 260, nullptr, SLGP_RAWPATH);
        MimeShellCopyText(t->path, 260, text);
        text[0] = 0;
        link->lpVtbl->GetArguments(link, text, 260);
        MimeShellCopyText(t->arguments, 260, text);
        text[0] = 0;
        link->lpVtbl->GetWorkingDirectory(link, text, 260);
        MimeShellCopyText(t->directory, 260, text);
        int index = 0;
        text[0] = 0;
        link->lpVtbl->GetIconLocation(link, text, 260, &index);
        MimeShellCopyText(t->icon, 260, text);
        t->icon_index = index;
        text[0] = 0;
        IPropertyStore *store;
        if (SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPropertyStore, (void **)&store))) {
            PROPVARIANT title;
            if (SUCCEEDED(store->lpVtbl->GetValue(store, &PKEY_Title, &title))) {
                if (title.vt == VT_LPWSTR && title.pwszVal)
                    MimeShellCopyText(t->title, 64, title.pwszVal);
                PropVariantClear(&title);
            }
            store->lpVtbl->Release(store);
        }
        if (!t->title[0]) {
            link->lpVtbl->GetDescription(link, text, 260);
            MimeShellCopyText(t->title, 64, text);
        }
        link->lpVtbl->Release(link);
        if (t->path[0] && t->title[0])
            j->task_count++;
    }
    NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)j, sizeof(*j));
    HeapFree(GetProcessHeap(), 0, j);
}

static HRESULT STDMETHODCALLTYPE dl_commit(ICustomDestinationList *This)
{
    DESTINATION_LIST *l = list_of(This);
    if (!l->InList)
        return E_UNEXPECTED;
    WCHAR id[129];
    resolve_app_id(l, id);
    AcquireSRWLockExclusive(&g_lists_lock);
    APP_LIST *a = app_list(id, TRUE);
    HRESULT hr = a ? S_OK : E_OUTOFMEMORY;
    if (a) {
        clear_app_list(a);
        wcscpy(a->AppId, id);
        /* what the list built goes to the app's entry, the list starts over. */
        a->Categories = l->Categories;
        a->CategoryCount = l->CategoryCount;
        a->Tasks = l->Tasks;
        l->Categories = nullptr;
        l->CategoryCount = l->CategoryCapacity = 0;
        l->Tasks = nullptr;
    }
    IObjectArray *tasks = a ? a->Tasks : nullptr;
    if (tasks)
        tasks->lpVtbl->AddRef(tasks);
    ReleaseSRWLockExclusive(&g_lists_lock);
    l->InList = FALSE;
    if (SUCCEEDED(hr))
        send_tasks(id, tasks);
    if (tasks)
        tasks->lpVtbl->Release(tasks);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dl_get_removed(ICustomDestinationList *This, REFIID riid, void **removed)
{
    if (!removed)
        return E_POINTER;
    *removed = nullptr;
    return empty_array(riid, removed);
}

static HRESULT STDMETHODCALLTYPE dl_delete_list(ICustomDestinationList *This, LPCWSTR app_id)
{
    DESTINATION_LIST *l = list_of(This);
    WCHAR id[129];
    if (app_id) {
        if (wcslen(app_id) >= 129)
            return E_INVALIDARG;
        wcscpy(id, app_id);
    } else {
        resolve_app_id(l, id);
    }
    AcquireSRWLockExclusive(&g_lists_lock);
    APP_LIST *a = app_list(id, FALSE);
    if (a)
        clear_app_list(a);
    ReleaseSRWLockExclusive(&g_lists_lock);
    send_tasks(id, nullptr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dl_abort(ICustomDestinationList *This)
{
    drop_pending(list_of(This));
    return S_OK;
}

static const ICustomDestinationListVtbl g_list_vtbl = {
    dl_query_interface, dl_add_ref, dl_release, dl_set_app_id, dl_begin_list, dl_append_category, dl_append_known,
    dl_add_user_tasks, dl_commit, dl_get_removed, dl_delete_list, dl_abort,
};

HRESULT DestinationListCreate(IUnknown *outer, REFIID riid, void **object)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    DESTINATION_LIST *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    l->Interface.lpVtbl = &g_list_vtbl;
    l->Refs = 1;
    HRESULT hr = dl_query_interface(&l->Interface, riid, object);
    dl_release(&l->Interface);
    return hr;
}

/* ------------------------------------------------------------------ the destinations of an app */

typedef struct _APP_OBJECT {
    union {
        IApplicationDestinations Destinations;
        IApplicationDocumentLists Documents;
    } Interface;
    LONG Refs;
    WCHAR AppId[129];
} APP_OBJECT;

static HRESULT app_set_id(APP_OBJECT *a, LPCWSTR id)
{
    if (!id || !*id || wcslen(id) >= 129)
        return E_INVALIDARG;
    wcscpy(a->AppId, id);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ad_query_interface(IApplicationDestinations *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IApplicationDestinations))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((APP_OBJECT *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ad_add_ref(IApplicationDestinations *This)
{
    return (ULONG)InterlockedIncrement(&((APP_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE ad_release(IApplicationDestinations *This)
{
    LONG refs = InterlockedDecrement(&((APP_OBJECT *)This)->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE ad_set_app_id(IApplicationDestinations *This, LPCWSTR id)
{
    return app_set_id((APP_OBJECT *)This, id);
}

static HRESULT STDMETHODCALLTYPE ad_remove(IApplicationDestinations *This, IUnknown *destination)
{
    return destination ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE ad_remove_all(IApplicationDestinations *This)
{
    return S_OK;
}

static const IApplicationDestinationsVtbl g_destinations_vtbl = {
    ad_query_interface, ad_add_ref, ad_release, ad_set_app_id, ad_remove, ad_remove_all,
};

HRESULT ApplicationDestinationsCreate(IUnknown *outer, REFIID riid, void **object)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    APP_OBJECT *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->Interface.Destinations.lpVtbl = &g_destinations_vtbl;
    a->Refs = 1;
    HRESULT hr = ad_query_interface(&a->Interface.Destinations, riid, object);
    ad_release(&a->Interface.Destinations);
    return hr;
}

static HRESULT STDMETHODCALLTYPE adl_query_interface(IApplicationDocumentLists *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IApplicationDocumentLists))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((APP_OBJECT *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE adl_add_ref(IApplicationDocumentLists *This)
{
    return (ULONG)InterlockedIncrement(&((APP_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE adl_release(IApplicationDocumentLists *This)
{
    LONG refs = InterlockedDecrement(&((APP_OBJECT *)This)->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE adl_set_app_id(IApplicationDocumentLists *This, LPCWSTR id)
{
    return app_set_id((APP_OBJECT *)This, id);
}

/* nothing is recorded, so the recent and frequent lists are empty. */
static HRESULT STDMETHODCALLTYPE adl_get_list(IApplicationDocumentLists *This, APPDOCLISTTYPE type, UINT items, REFIID riid,
                                              void **list)
{
    if (!list)
        return E_POINTER;
    *list = nullptr;
    if (type != ADLT_RECENT && type != ADLT_FREQUENT)
        return E_INVALIDARG;
    return empty_array(riid, list);
}

static const IApplicationDocumentListsVtbl g_documents_vtbl = {
    adl_query_interface, adl_add_ref, adl_release, adl_set_app_id, adl_get_list,
};

HRESULT ApplicationDocumentListsCreate(IUnknown *outer, REFIID riid, void **object)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    APP_OBJECT *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->Interface.Documents.lpVtbl = &g_documents_vtbl;
    a->Refs = 1;
    HRESULT hr = adl_query_interface(&a->Interface.Documents, riid, object);
    adl_release(&a->Interface.Documents);
    return hr;
}
