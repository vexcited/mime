/*
 * the app user model id of the process, kept where shcore keeps it, and the property store of a window.
 *
 * the store of a window lives in the window's own properties, as windows' does: every value is a window
 * property named after its key, "{format id} property id", holding the atom of the value as text, so another
 * process finds it too. an empty value removes the property and a null one is kept as a marker.
 */
#include "shell32_private.h"
#include <mime/win32/propidl.h>

/* ------------------------------------------------------------------ the app id */

#define STARTF_TITLEISLINKNAME 0x00000800
#define STARTF_TITLEISAPPID 0x00001000
/* marks a title shcore allocated itself. */
#define STARTF_SHCORE_APPID 0x00004000

/* kept in the process parameters as a title marked STARTF_TITLEISAPPID, where GetStartupInfo shows it. */
SHSTDAPI HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(LPCWSTR id)
{
    if (id && wcslen(id) >= 128)
        return E_INVALIDARG;
    PRTL_USER_PROCESS_PARAMETERS params = NtCurrentPeb()->ProcessParameters;
    if (!params)
        return E_FAIL;
    if ((params->WindowFlags & (STARTF_TITLEISAPPID | STARTF_SHCORE_APPID)) ==
        (STARTF_TITLEISAPPID | STARTF_SHCORE_APPID)) {
        LocalFree(params->WindowTitle.Buffer);
        RtlInitUnicodeString(&params->WindowTitle, nullptr);
        params->WindowFlags &= ~(STARTF_TITLEISAPPID | STARTF_SHCORE_APPID);
    }
    if (!id || !*id)
        return E_INVALIDARG;
    SIZE_T size = (wcslen(id) + 1) * sizeof(WCHAR);
    WCHAR *copy = LocalAlloc(LMEM_FIXED, size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, id, size);
    params->WindowFlags = (params->WindowFlags & ~STARTF_TITLEISLINKNAME) | STARTF_TITLEISAPPID | STARTF_SHCORE_APPID;
    RtlInitUnicodeString(&params->WindowTitle, copy);
    return S_OK;
}

SHSTDAPI HRESULT WINAPI GetCurrentProcessExplicitAppUserModelID(LPWSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    STARTUPINFOW info = {.cb = sizeof(info)};
    GetStartupInfoW(&info);
    if (!(info.dwFlags & STARTF_TITLEISAPPID) || !info.lpTitle || !*info.lpTitle)
        return E_FAIL;
    *out = ShDuplicate(info.lpTitle);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ the property store of a window */

/* the value of a property set to null. */
#define NULL_MARKER ((HANDLE)(ULONG_PTR)0xF0000000)
#define KEY_CHARS 50

typedef struct _WINDOW_STORE {
    IPropertyStore Interface;
    LONG Refs;
    HWND Window;
} WINDOW_STORE;

typedef HRESULT(WINAPI *TO_STRING)(const PROPVARIANT *, LPWSTR, UINT);

static void key_to_string(const PROPERTYKEY *key, WCHAR *text)
{
    wsprintfW(text, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} %lu", key->fmtid.Data1, key->fmtid.Data2,
              key->fmtid.Data3, key->fmtid.Data4[0], key->fmtid.Data4[1], key->fmtid.Data4[2], key->fmtid.Data4[3],
              key->fmtid.Data4[4], key->fmtid.Data4[5], key->fmtid.Data4[6], key->fmtid.Data4[7], key->pid);
}

static int hex_digit(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* a property named "{format id} id", the names this store gives its window properties. */
static BOOL key_from_string(const WCHAR *text, PROPERTYKEY *key)
{
    if (text[0] != L'{' || wcslen(text) < 40 || text[37] != L'}' || text[38] != L' ')
        return FALSE;
    BYTE bytes[16];
    static const int groups[5][2] = {{1, 4}, {10, 2}, {15, 2}, {20, 2}, {25, 6}};
    int at = 0;
    for (int g = 0; g < 5; g++) {
        for (int i = 0; i < groups[g][1]; i++) {
            int hi = hex_digit(text[groups[g][0] + i * 2]), lo = hex_digit(text[groups[g][0] + i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return FALSE;
            bytes[at++] = (BYTE)(hi << 4 | lo);
        }
        if (g < 4 && text[groups[g][0] + groups[g][1] * 2] != L'-')
            return FALSE;
    }
    key->fmtid.Data1 = (DWORD)bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
    key->fmtid.Data2 = (WORD)(bytes[4] << 8 | bytes[5]);
    key->fmtid.Data3 = (WORD)(bytes[6] << 8 | bytes[7]);
    memcpy(key->fmtid.Data4, bytes + 8, 8);
    key->pid = 0;
    for (const WCHAR *p = text + 39; *p; p++) {
        if (*p < L'0' || *p > L'9')
            return FALSE;
        key->pid = key->pid * 10 + (DWORD)(*p - L'0');
    }
    return TRUE;
}

static HRESULT error_result(void)
{
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE store_query_interface(IPropertyStore *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertyStore))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE store_add_ref(IPropertyStore *This)
{
    return (ULONG)InterlockedIncrement(&((WINDOW_STORE *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE store_release(IPropertyStore *This)
{
    LONG refs = InterlockedDecrement(&((WINDOW_STORE *)This)->Refs);
    if (!refs)
        ShFree(This);
    return (ULONG)refs;
}

typedef struct _ENUM_KEYS {
    PROPERTYKEY *Keys;
    DWORD Count, Capacity;
} ENUM_KEYS;

static BOOL CALLBACK collect_key(HWND window, LPWSTR name, HANDLE data, ULONG_PTR parameter)
{
    ENUM_KEYS *keys = (ENUM_KEYS *)parameter;
    PROPERTYKEY key;
    /* a string name is a property of this store; an atom name is somebody else's. */
    if ((ULONG_PTR)name <= 0xFFFF || !key_from_string(name, &key))
        return TRUE;
    if (keys->Count == keys->Capacity) {
        DWORD capacity = keys->Capacity ? keys->Capacity * 2 : 8;
        PROPERTYKEY *grown = ShAlloc(capacity * sizeof(*grown));
        if (!grown)
            return FALSE;
        if (keys->Keys)
            memcpy(grown, keys->Keys, keys->Count * sizeof(*grown));
        ShFree(keys->Keys);
        keys->Keys = grown;
        keys->Capacity = capacity;
    }
    keys->Keys[keys->Count++] = key;
    return TRUE;
}

/* the keys the window has values for, in the order its properties list them. */
static HRESULT list_keys(WINDOW_STORE *store, ENUM_KEYS *keys)
{
    memset(keys, 0, sizeof(*keys));
    EnumPropsExW(store->Window, (PROPENUMPROCEXW)collect_key, (LPARAM)keys);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_get_count(IPropertyStore *This, DWORD *count)
{
    if (!count)
        return E_POINTER;
    ENUM_KEYS keys;
    list_keys((WINDOW_STORE *)This, &keys);
    *count = keys.Count;
    ShFree(keys.Keys);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    if (!key)
        return E_POINTER;
    ENUM_KEYS keys;
    list_keys((WINDOW_STORE *)This, &keys);
    HRESULT hr = E_INVALIDARG;
    if (index < keys.Count) {
        *key = keys.Keys[index];
        hr = S_OK;
    }
    ShFree(keys.Keys);
    return hr;
}

static HRESULT STDMETHODCALLTYPE store_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    WINDOW_STORE *store = (WINDOW_STORE *)This;
    if (!value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    WCHAR name[KEY_CHARS], text[264];
    key_to_string(key, name);
    HANDLE data = GetPropW(store->Window, name);
    if (data == NULL_MARKER) {
        value->vt = VT_NULL;
        return S_OK;
    }
    if (!(ULONG_PTR)data || !GlobalGetAtomNameW((ATOM)(ULONG_PTR)data, text, 260))
        return S_OK;
    /* the stored text is the value as a string. */
    SIZE_T size = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = ShAlloc(size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, text, size);
    value->vt = VT_LPWSTR;
    value->pwszVal = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_set_value(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    WINDOW_STORE *store = (WINDOW_STORE *)This;
    WCHAR name[KEY_CHARS], text[264] = L"";
    key_to_string(key, name);
    if (value->vt != VT_EMPTY && value->vt != VT_NULL) {
        HMODULE propsys = LoadLibraryW(L"propsys.dll");
        TO_STRING to_string = propsys ? (TO_STRING)(void *)GetProcAddress(propsys, "PropVariantToString") : nullptr;
        if (!to_string)
            return E_NOTIMPL;
        HRESULT hr = to_string(value, text, 260);
        if (FAILED(hr))
            return hr;
    }
    HANDLE old = RemovePropW(store->Window, name);
    if (old && old != NULL_MARKER && (ATOM)(ULONG_PTR)old)
        GlobalDeleteAtom((ATOM)(ULONG_PTR)old);
    if (value->vt == VT_EMPTY)
        return S_OK;
    HANDLE data = NULL_MARKER;
    if (value->vt != VT_NULL) {
        ATOM atom = GlobalAddAtomW(text);
        if (!atom)
            return error_result();
        data = (HANDLE)(ULONG_PTR)atom;
    }
    if (!SetPropW(store->Window, name, data)) {
        HRESULT hr = error_result();
        if (data != NULL_MARKER)
            GlobalDeleteAtom((ATOM)(ULONG_PTR)data);
        return hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_commit(IPropertyStore *This)
{
    return S_OK;
}

static const IPropertyStoreVtbl g_store_vtbl = {
    store_query_interface, store_add_ref, store_release, store_get_count, store_get_at, store_get_value,
    store_set_value, store_commit,
};

SHSTDAPI HRESULT WINAPI SHGetPropertyStoreForWindow(HWND window, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!window)
        return E_INVALIDARG;
    WINDOW_STORE *store = ShAllocZero(sizeof(*store));
    if (!store)
        return E_OUTOFMEMORY;
    store->Interface.lpVtbl = &g_store_vtbl;
    store->Refs = 1;
    store->Window = window;
    HRESULT hr = store->Interface.lpVtbl->QueryInterface(&store->Interface, iid, out);
    store->Interface.lpVtbl->Release(&store->Interface);
    return hr;
}
