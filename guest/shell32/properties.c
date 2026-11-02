/*
 * the process app user model id, kept where shcore keeps it, and the property store of a window.
 *
 * like on windows, a window's store lives in its window properties so other processes see it too: each value is a
 * property named "{format id} property id" holding the atom of the value's text. an empty value removes the property
 * and a null one is kept as a marker.
 */
#include "shell32_private.h"
#include <mime/shellclient.h>
#include <mime/win32/propidl.h>

/* ------------------------------------------------------------------ app id */

#define STARTF_TITLEISLINKNAME 0x00000800
#define STARTF_TITLEISAPPID 0x00001000
/* marks a title shcore allocated itself. */
#define STARTF_SHCORE_APPID 0x00004000

/* stored as the process parameters title marked STARTF_TITLEISAPPID, where GetStartupInfo shows it. */
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
    MimeShellProcessAppId(id);
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

/* ------------------------------------------------------------------ window property store */

/* stands for a property set to null. */
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

/* parses the "{format id} id" names this store gives its window properties. */
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

/* keys come in the order the window lists its properties. */
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
    /* values come back as strings whatever type they were set with. */
    SIZE_T size = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = ShAlloc(size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, text, size);
    value->vt = VT_LPWSTR;
    value->pwszVal = copy;
    return S_OK;
}

/* System.AppUserModel.ID groups a window's taskbar button with the program's others. */
static void notify_window_id(HWND window, REFPROPERTYKEY key, const WCHAR *text)
{
    static const GUID format = {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}};
    if (key->pid == 5 && IsEqualGUID(&key->fmtid, &format))
        MimeShellWindowAppId(window, text);
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
    if (value->vt == VT_EMPTY) {
        notify_window_id(store->Window, key, nullptr);
        return S_OK;
    }
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
    notify_window_id(store->Window, key, value->vt == VT_EMPTY || value->vt == VT_NULL ? nullptr : text);
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

/* ------------------------------------------------------------------ the property store of a file */

/*
 * the store SHGetPropertyStoreFromParsingName makes for a file: what the handler registered for its type has, then the
 * properties of the file system. The handler is made, given the file through the interface it takes (a stream, then the
 * path) and kept for the life of the store.
 */
typedef struct FILE_STORE {
    IPropertyStore Interface;
    LONG Refs;
    IPropertyStore *Handler;
    LPWSTR Path;
    BOOL ReadWrite, HandlerOnly;
    DWORD HandlerCount;
} FILE_STORE;

#define FILE_FMT_STORAGE {0xB725F130, 0x47EF, 0x101A, {0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC}}

/* System.ItemNameDisplay, FileName, FileExtension, Size, DateModified, DateCreated, FileAttributes, ItemPathDisplay. */
static const PROPERTYKEY g_file_keys[] = {
    {FILE_FMT_STORAGE, 10},
    {{0x41CF5AE0, 0xF75A, 0x4806, {0xBD, 0x87, 0x59, 0xC7, 0xD9, 0x24, 0x8E, 0xB9}}, 100},
    {{0xE4F10A3C, 0x49E6, 0x405D, {0x82, 0x88, 0xA2, 0x3B, 0xD4, 0xEE, 0xAA, 0x6C}}, 100},
    {FILE_FMT_STORAGE, 12},
    {FILE_FMT_STORAGE, 14},
    {FILE_FMT_STORAGE, 15},
    {FILE_FMT_STORAGE, 13},
    {{0xE3E0584C, 0xB788, 0x4A5A, {0xBB, 0x20, 0x7F, 0x5A, 0x44, 0xC9, 0xAC, 0xDD}}, 7},
};

static BOOL same_key(const PROPERTYKEY *a, const PROPERTYKEY *b)
{
    return a->pid == b->pid && !memcmp(&a->fmtid, &b->fmtid, sizeof(GUID));
}

static FILE_STORE *file_store(IPropertyStore *This)
{
    return (FILE_STORE *)This;
}

static HRESULT STDMETHODCALLTYPE file_query_interface(IPropertyStore *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IPropertyStore)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE file_add_ref(IPropertyStore *This)
{
    return (ULONG)InterlockedIncrement(&file_store(This)->Refs);
}

static ULONG STDMETHODCALLTYPE file_release(IPropertyStore *This)
{
    FILE_STORE *store = file_store(This);
    LONG refs = InterlockedDecrement(&store->Refs);
    if (!refs) {
        if (store->Handler)
            store->Handler->lpVtbl->Release(store->Handler);
        ShFree(store->Path);
        ShFree(store);
    }
    return (ULONG)refs;
}

static DWORD file_key_count(FILE_STORE *store)
{
    return store->HandlerOnly ? 0 : (DWORD)(sizeof(g_file_keys) / sizeof(g_file_keys[0]));
}

static HRESULT STDMETHODCALLTYPE file_get_count(IPropertyStore *This, DWORD *count)
{
    FILE_STORE *store = file_store(This);
    if (!count)
        return E_POINTER;
    *count = store->HandlerCount + file_key_count(store);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE file_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    FILE_STORE *store = file_store(This);
    if (!key)
        return E_POINTER;
    if (index < store->HandlerCount)
        return store->Handler->lpVtbl->GetAt(store->Handler, index, key);
    index -= store->HandlerCount;
    if (index >= file_key_count(store))
        return E_INVALIDARG;
    *key = g_file_keys[index];
    return S_OK;
}

static HRESULT file_text(const WCHAR *text, PROPVARIANT *value)
{
    value->pwszVal = ShDuplicate(text);
    if (!value->pwszVal)
        return E_OUTOFMEMORY;
    value->vt = VT_LPWSTR;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE file_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    FILE_STORE *store = file_store(This);
    if (!value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    DWORD which = 0;
    while (which < file_key_count(store) && !same_key(key, &g_file_keys[which]))
        which++;
    if (which == file_key_count(store))
        return store->Handler ? store->Handler->lpVtbl->GetValue(store->Handler, key, value) : S_OK;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(store->Path, GetFileExInfoStandard, &data))
        return HRESULT_FROM_WIN32(GetLastError());
    const WCHAR *name = wcsrchr(store->Path, L'\\');
    name = name ? name + 1 : store->Path;
    const WCHAR *dot = wcsrchr(name, L'.');
    switch (which) {
    case 0:
    case 1:
        return file_text(name, value);
    case 2:
        return dot ? file_text(dot, value) : S_OK;
    case 3:
        value->vt = VT_UI8;
        value->uhVal.QuadPart = ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        return S_OK;
    case 4:
        value->vt = VT_FILETIME;
        value->filetime = data.ftLastWriteTime;
        return S_OK;
    case 5:
        value->vt = VT_FILETIME;
        value->filetime = data.ftCreationTime;
        return S_OK;
    case 6:
        value->vt = VT_UI4;
        value->ulVal = data.dwFileAttributes;
        return S_OK;
    default:
        return file_text(store->Path, value);
    }
}

static HRESULT STDMETHODCALLTYPE file_set_value(IPropertyStore *This, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    FILE_STORE *store = file_store(This);
    if (!store->ReadWrite || !store->Handler)
        return STG_E_ACCESSDENIED;
    return store->Handler->lpVtbl->SetValue(store->Handler, key, value);
}

static HRESULT STDMETHODCALLTYPE file_commit(IPropertyStore *This)
{
    FILE_STORE *store = file_store(This);
    return store->ReadWrite && store->Handler ? store->Handler->lpVtbl->Commit(store->Handler) : S_OK;
}

static const IPropertyStoreVtbl g_file_store_vtbl = {
    file_query_interface, file_add_ref, file_release, file_get_count, file_get_at, file_get_value, file_set_value,
    file_commit,
};

MIME_DEFINE_GUID(IID_ShInitializeWithStream, 0xB824B49D, 0x22AC, 0x4161, 0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F);
MIME_DEFINE_GUID(IID_ShInitializeWithFile, 0xB7D14566, 0x0509, 0x4CCE, 0xA7, 0x1F, 0x0A, 0x55, 0x42, 0x33, 0xBD, 0x9B);

typedef HRESULT(WINAPI *LOOKUP_HANDLER)(LPCWSTR path, CLSID *clsid);
typedef HRESULT(WINAPI *STREAM_ON_FILE)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **);

/* the handler of the file's type, made and given the file. A type with none is not an error. */
static HRESULT make_handler(FILE_STORE *store)
{
    HMODULE propsys = LoadLibraryW(L"propsys.dll");
    LOOKUP_HANDLER lookup = propsys ? (LOOKUP_HANDLER)GetProcAddress(propsys, "PSLookupPropertyHandlerCLSID") : nullptr;
    CLSID clsid;
    if (!lookup || FAILED(lookup(store->Path, &clsid)))
        return S_OK;
    IUnknown *object = nullptr;
    HRESULT hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&object);
    if (FAILED(hr))
        return S_OK;
    IInitializeWithStream *with_stream;
    IInitializeWithFile *with_file;
    hr = E_NOINTERFACE;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_ShInitializeWithStream, (void **)&with_stream))) {
        HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll");
        STREAM_ON_FILE open_stream = shlwapi ? (STREAM_ON_FILE)GetProcAddress(shlwapi, "SHCreateStreamOnFileEx") : nullptr;
        IStream *stream = nullptr;
        hr = open_stream ? open_stream(store->Path, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)
                         : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        if (SUCCEEDED(hr)) {
            hr = with_stream->lpVtbl->Initialize(with_stream, stream, STGM_READ);
            stream->lpVtbl->Release(stream);
        }
        with_stream->lpVtbl->Release(with_stream);
    } else if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_ShInitializeWithFile, (void **)&with_file))) {
        hr = with_file->lpVtbl->Initialize(with_file, store->Path, store->ReadWrite ? STGM_READWRITE : STGM_READ);
        with_file->lpVtbl->Release(with_file);
    }
    if (SUCCEEDED(hr))
        hr = object->lpVtbl->QueryInterface(object, &IID_IPropertyStore, (void **)&store->Handler);
    object->lpVtbl->Release(object);
    if (FAILED(hr))
        return S_OK;
    DWORD count = 0;
    if (SUCCEEDED(store->Handler->lpVtbl->GetCount(store->Handler, &count)))
        store->HandlerCount = count;
    return S_OK;
}

#define GPS_HANDLERPROPERTIESONLY_FLAG 0x1
#define GPS_READWRITE_FLAG 0x2

SHSTDAPI HRESULT WINAPI SHGetPropertyStoreFromParsingName(PCWSTR path, IBindCtx *context, DWORD flags, REFIID riid,
                                                          void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!path || !*path)
        return E_INVALIDARG;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(GetLastError());
    FILE_STORE *store = ShAllocZero(sizeof(*store));
    if (!store)
        return E_OUTOFMEMORY;
    store->Interface.lpVtbl = &g_file_store_vtbl;
    store->Refs = 1;
    store->Path = ShDuplicate(path);
    store->ReadWrite = (flags & GPS_READWRITE_FLAG) != 0;
    store->HandlerOnly = (flags & GPS_HANDLERPROPERTIESONLY_FLAG) != 0;
    HRESULT hr = store->Path ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        hr = make_handler(store);
    if (SUCCEEDED(hr))
        hr = store->Interface.lpVtbl->QueryInterface(&store->Interface, riid, out);
    store->Interface.lpVtbl->Release(&store->Interface);
    return hr;
}
