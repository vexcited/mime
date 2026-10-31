/* the property store of a window: its values are window properties named by the keys, holding atoms of the text. */
#include "propsys_private.h"

#define NULL_MARKER ((HANDLE)(ULONG_PTR)0xF0000000)

typedef BOOL(CALLBACK *PROPENUMPROCEXW)(HWND, LPWSTR, HANDLE, ULONG_PTR);
__declspec(dllimport) int WINAPI EnumPropsExW(HWND window, PROPENUMPROCEXW proc, LPARAM param);

static HRESULT value_from_string(LPCWSTR text, PROPVARIANT *value)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = CoTaskMemAlloc(bytes);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, text, bytes);
    value->vt = VT_LPWSTR;
    value->pwszVal = copy;
    return S_OK;
}

MIME_DEFINE_GUID(IID_IObjectWithWindow, 0xb482cd93, 0x28d0, 0x4f19, 0xb0, 0x18, 0xd0, 0xbd, 0x02, 0x10, 0x2d, 0xcc);

typedef struct IObjectWithWindow IObjectWithWindow;
typedef struct IObjectWithWindowVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IObjectWithWindow *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IObjectWithWindow *This);
    ULONG(STDMETHODCALLTYPE *Release)(IObjectWithWindow *This);
    HRESULT(STDMETHODCALLTYPE *SetWindow)(IObjectWithWindow *This, HWND window);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IObjectWithWindow *This, HWND *window);
} IObjectWithWindowVtbl;
struct IObjectWithWindow {
    const IObjectWithWindowVtbl *lpVtbl;
};

typedef struct {
    IPropertyStore store;
    IObjectWithWindow window_object;
    LONG refs;
    HWND window;
    PROPERTYKEY *keys;
    UINT key_count;
    BOOL keys_valid;
    CRITICAL_SECTION lock;
} WINDOW_STORE;

#define WS_FROM(iface, member) ((WINDOW_STORE *)((BYTE *)(iface) - offsetof(WINDOW_STORE, member)))

static HRESULT ws_query(WINDOW_STORE *w, REFIID riid, void **object)
{
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore))
        *object = &w->store;
    else if (IsEqualIID(riid, &IID_IObjectWithWindow))
        *object = &w->window_object;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&w->refs);
    return S_OK;
}

static ULONG ws_release(WINDOW_STORE *w)
{
    LONG refs = InterlockedDecrement(&w->refs);
    if (!refs) {
        HeapFree(GetProcessHeap(), 0, w->keys);
        DeleteCriticalSection(&w->lock);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE s_query_interface(IPropertyStore *This, REFIID riid, void **object)
{
    return ws_query(WS_FROM(This, store), riid, object);
}
static ULONG STDMETHODCALLTYPE s_add_ref(IPropertyStore *This)
{
    return InterlockedIncrement(&WS_FROM(This, store)->refs);
}
static ULONG STDMETHODCALLTYPE s_release(IPropertyStore *This)
{
    return ws_release(WS_FROM(This, store));
}

typedef struct {
    PROPERTYKEY *keys;
    UINT count, capacity;
} KEYLIST;

static BOOL CALLBACK enum_prop(HWND window, LPWSTR name, HANDLE data, ULONG_PTR param)
{
    KEYLIST *list = (KEYLIST *)param;
    WCHAR buffer[260];
    PROPERTYKEY key;
    /* an atom as the name is not a key. */
    if (HIWORD((ULONG_PTR)name) == 0 || GlobalGetAtomNameW((ATOM)(ULONG_PTR)name, buffer, 260))
        return TRUE;
    if (FAILED(PSPropertyKeyFromString(name, &key)))
        return TRUE;
    if (list->count == list->capacity) {
        UINT grown = list->capacity ? list->capacity * 2 : 8;
        PROPERTYKEY *more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(PROPERTYKEY));
        if (!more)
            return FALSE;
        if (list->keys) {
            memcpy(more, list->keys, list->count * sizeof(PROPERTYKEY));
            HeapFree(GetProcessHeap(), 0, list->keys);
        }
        list->keys = more;
        list->capacity = grown;
    }
    list->keys[list->count++] = key;
    return TRUE;
}

static HRESULT ws_build(WINDOW_STORE *w)
{
    if (w->keys_valid)
        return S_OK;
    KEYLIST list = {};
    EnumPropsExW(w->window, enum_prop, (LPARAM)&list);
    HeapFree(GetProcessHeap(), 0, w->keys);
    w->keys = list.keys;
    w->key_count = list.count;
    w->keys_valid = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_get_count(IPropertyStore *This, DWORD *count)
{
    WINDOW_STORE *w = WS_FROM(This, store);
    *count = 0;
    EnterCriticalSection(&w->lock);
    HRESULT hr = ws_build(w);
    *count = w->key_count;
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE s_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    WINDOW_STORE *w = WS_FROM(This, store);
    EnterCriticalSection(&w->lock);
    HRESULT hr = ws_build(w);
    if (SUCCEEDED(hr)) {
        if (index < w->key_count)
            *key = w->keys[index];
        else
            hr = E_INVALIDARG;
    }
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE s_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    WINDOW_STORE *w = WS_FROM(This, store);
    if (!value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    WCHAR name[PKEYSTR_MAX + 1];
    HRESULT hr = PSStringFromPropertyKey(key, name, 50);
    if (FAILED(hr))
        return hr;
    HANDLE data = GetPropW(w->window, name);
    if (data == NULL_MARKER) {
        value->vt = VT_NULL;
        return S_OK;
    }
    WCHAR text[260];
    if (!(ULONG_PTR)data || !GlobalGetAtomNameW((ATOM)(ULONG_PTR)data, text, 260))
        return S_OK;
    SCHEMA *schema;
    DWORD index;
    BOOL multiple = FALSE;
    if (SUCCEEDED(SchemaFindKey(key, &schema, &index))) {
        multiple = (schema->descs[index].type_flags & PDTF_MULTIPLEVALUES) != 0;
        SchemaRelease(schema);
    }
    hr = multiple ? InitPropVariantFromStringAsVector(text, value) : value_from_string(text, value);
    if (SUCCEEDED(hr) && value->vt != VT_NULL)
        PSCoerceToCanonicalValue(key, value);
    return SUCCEEDED(hr) ? S_OK : hr;
}

static HRESULT STDMETHODCALLTYPE s_set_value(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    WINDOW_STORE *w = WS_FROM(This, store);
    EnterCriticalSection(&w->lock);
    w->keys_valid = FALSE;
    LeaveCriticalSection(&w->lock);
    WCHAR name[PKEYSTR_MAX + 1], text[260];
    HRESULT hr = PSStringFromPropertyKey(key, name, 50);
    if (FAILED(hr))
        return hr;
    hr = PropVariantToString(value, text, 260);
    if (FAILED(hr))
        return hr;
    HANDLE old = RemovePropW(w->window, name);
    if (old && old != NULL_MARKER && (ATOM)(ULONG_PTR)old)
        GlobalDeleteAtom((ATOM)(ULONG_PTR)old);
    if (value->vt == VT_EMPTY)
        return S_OK;
    HANDLE data = NULL_MARKER;
    if (value->vt != VT_NULL) {
        ATOM atom = GlobalAddAtomW(text);
        if (!atom)
            return HRESULT_FROM_WIN32(GetLastError());
        data = (HANDLE)(ULONG_PTR)atom;
    }
    if (!SetPropW(w->window, name, data)) {
        HRESULT failed = HRESULT_FROM_WIN32(GetLastError());
        if (data != NULL_MARKER)
            GlobalDeleteAtom((ATOM)(ULONG_PTR)data);
        return failed;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_commit(IPropertyStore *This)
{
    return S_OK;
}

static const IPropertyStoreVtbl store_vtbl = {s_query_interface, s_add_ref, s_release, s_get_count, s_get_at,
                                              s_get_value, s_set_value, s_commit};

static HRESULT STDMETHODCALLTYPE o_query_interface(IObjectWithWindow *This, REFIID riid, void **object)
{
    return ws_query(WS_FROM(This, window_object), riid, object);
}
static ULONG STDMETHODCALLTYPE o_add_ref(IObjectWithWindow *This)
{
    return InterlockedIncrement(&WS_FROM(This, window_object)->refs);
}
static ULONG STDMETHODCALLTYPE o_release(IObjectWithWindow *This)
{
    return ws_release(WS_FROM(This, window_object));
}
static HRESULT STDMETHODCALLTYPE o_set_window(IObjectWithWindow *This, HWND window)
{
    WINDOW_STORE *w = WS_FROM(This, window_object);
    if (w->window)
        return E_UNEXPECTED;
    if (!window)
        return E_INVALIDARG;
    w->window = window;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE o_get_window(IObjectWithWindow *This, HWND *window)
{
    *window = WS_FROM(This, window_object)->window;
    return S_OK;
}

static const IObjectWithWindowVtbl window_vtbl = {o_query_interface, o_add_ref, o_release, o_set_window, o_get_window};

/* a store whose window is set afterwards, as the class of the property system makes it. */
HRESULT WindowStoreCreate(HWND window, REFIID riid, void **object)
{
    *object = nullptr;
    WINDOW_STORE *w = PsAllocZero(sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    w->store.lpVtbl = &store_vtbl;
    w->window_object.lpVtbl = &window_vtbl;
    w->refs = 1;
    InitializeCriticalSection(&w->lock);
    w->window = window;
    HRESULT hr = ws_query(w, riid, object);
    ws_release(w);
    return hr;
}

HRESULT WINAPI SHGetPropertyStoreForWindow(HWND window, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    HRESULT hr = window ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
        hr = WindowStoreCreate(window, riid, object);
    return hr;
}
