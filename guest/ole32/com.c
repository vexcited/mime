/*
 * the COM runtime: apartments, the task allocator, guid strings and activation of in process servers under HKCR\CLSID.
 * windows ole32 forwards these to combase.dll, here they live in ole32.
 */
#include "orpc.h"

static DWORD g_tls = TLS_OUT_OF_INDEXES;
static SRWLOCK g_lock = SRWLOCK_INIT;
/* threads in the multithreaded apartment, which other threads join implicitly. */
static LONG g_mta_threads;
/* holders of the multithreaded apartment with no thread in it, see CoIncrementMTAUsage. */
static LONG g_mta_usage;
/* the process's first single threaded apartment. */
static DWORD g_main_sta;

void OleProcessAttach(void)
{
    g_tls = TlsAlloc();
}

OLE_THREAD *OleThreadState(BOOL create)
{
    if (g_tls == TLS_OUT_OF_INDEXES)
        return nullptr;
    OLE_THREAD *t = TlsGetValue(g_tls);
    if (!t && create) {
        t = OleAllocZero(sizeof(*t));
        if (t) {
            TlsSetValue(g_tls, t);
            NtCurrentTeb()->ReservedForOle = t;
        }
    }
    return t;
}

static void release_call_state(OLE_THREAD *t)
{
    IUnknown *cancel = t->CancelObject;
    t->CancelObject = nullptr;
    if (cancel)
        cancel->lpVtbl->Release(cancel);
}

static void leave_apartment(OLE_THREAD *t)
{
    OleContextLeave(t);
    if (t->Sta)
        OrpcApartmentLeave(t);
    IMessageFilter *filter = t->MessageFilter;
    t->MessageFilter = nullptr;
    if (filter)
        filter->lpVtbl->Release(filter);
    if (!t->Sta) {
        InterlockedDecrement(&g_mta_threads);
        OleContextMtaUsers(-1);
    } else
        InterlockedCompareExchange((LONG volatile *)&g_main_sta, 0, (LONG)GetCurrentThreadId());
    t->ApartmentFlags &= ~(OLE_TLS_LOCALTID | OLE_TLS_APARTMENTTHREADED | OLE_TLS_MULTITHREADED);
    t->ApartmentId = 0;
}

void OleThreadDetach(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t)
        return;
    if (t->ComInits)
        leave_apartment(t);
    release_call_state(t);
    if (t->OrpcEvent)
        CloseHandle(t->OrpcEvent);
    TlsSetValue(g_tls, nullptr);
    NtCurrentTeb()->ReservedForOle = nullptr;
    OleFree(t);
}

HRESULT OleCheckApartment(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (t && t->ComInits)
        return S_OK;
    return g_mta_threads > 0 || g_mta_usage > 0 ? S_OK : CO_E_NOTINITIALIZED;
}

/* ------------------------------------------------------------------ apartments */

WINOLEAPI HRESULT WINAPI CoInitializeEx(void *reserved, DWORD flags)
{
    if (reserved || (flags & ~(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY)))
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return E_OUTOFMEMORY;
    BOOL sta = (flags & COINIT_APARTMENTTHREADED) != 0;
    if (t->ComInits) {
        if (t->Sta != sta)
            return RPC_E_CHANGED_MODE;
        t->ComInits++;
        return S_FALSE;
    }
    t->Sta = sta;
    t->ComInits = 1;
    t->ApartmentFlags |= OLE_TLS_LOCALTID | (sta ? OLE_TLS_APARTMENTTHREADED : OLE_TLS_MULTITHREADED);
    t->ApartmentId = GetCurrentThreadId();
    if (sta)
        InterlockedCompareExchange((LONG volatile *)&g_main_sta, (LONG)GetCurrentThreadId(), 0);
    else {
        OleContextMtaUsers(1);
        InterlockedIncrement(&g_mta_threads);
    }
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoInitialize(void *reserved)
{
    return CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

WINOLEAPI void WINAPI CoUninitialize(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->ComInits)
        return;
    if (--t->ComInits == 0)
        leave_apartment(t);
}

WINOLEAPI HRESULT WINAPI CoGetApartmentType(APTTYPE *type, APTTYPEQUALIFIER *qualifier)
{
    if (!type || !qualifier)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(FALSE);
    *qualifier = APTTYPEQUALIFIER_NONE;
    if (t && t->ComInits) {
        if (!t->Sta)
            *type = APTTYPE_MTA;
        else
            *type = g_main_sta == GetCurrentThreadId() ? APTTYPE_MAINSTA : APTTYPE_STA;
        return S_OK;
    }
    if (g_mta_threads > 0 || g_mta_usage > 0) {
        *type = APTTYPE_MTA;
        *qualifier = APTTYPEQUALIFIER_IMPLICIT_MTA;
        return S_OK;
    }
    *type = APTTYPE_CURRENT;
    return CO_E_NOTINITIALIZED;
}

/* keeps the multithreaded apartment alive for code that has no thread to put in it. */
void OleMtaUsage(LONG delta)
{
    OleContextMtaUsers(delta);
    InterlockedExchangeAdd(&g_mta_usage, delta);
}

WINOLEAPI DWORD WINAPI CoGetCurrentProcess(void)
{
    static LONG counter;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return 0;
    if (!t->ProcessId)
        t->ProcessId = (DWORD)InterlockedIncrement(&counter);
    return t->ProcessId;
}

/* ------------------------------------------------------------------ task allocator */

WINOLEAPI void *WINAPI CoTaskMemAlloc(SIZE_T size)
{
    return OleAlloc(size);
}

WINOLEAPI void *WINAPI CoTaskMemRealloc(void *p, SIZE_T size)
{
    if (!p)
        return OleAlloc(size);
    if (!size) {
        OleFree(p);
        return nullptr;
    }
    return HeapReAlloc(GetProcessHeap(), 0, p, size);
}

WINOLEAPI void WINAPI CoTaskMemFree(void *p)
{
    OleFree(p);
}

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
    return CoTaskMemAlloc(size);
}

static void *STDMETHODCALLTYPE malloc_realloc(IMalloc *This, void *p, SIZE_T size)
{
    return CoTaskMemRealloc(p, size);
}

static void STDMETHODCALLTYPE malloc_free(IMalloc *This, void *p)
{
    CoTaskMemFree(p);
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

WINOLEAPI HRESULT WINAPI CoGetMalloc(DWORD context, IMalloc **malloc)
{
    if (!malloc)
        return E_INVALIDARG;
    if (context != MEMCTX_TASK) {
        *malloc = nullptr;
        return E_INVALIDARG;
    }
    *malloc = &g_malloc;
    return S_OK;
}

/* ------------------------------------------------------------------ guid strings */

void OleFormatGuid(REFGUID guid, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    BYTE bytes[16];
    bytes[0] = (BYTE)(guid->Data1 >> 24);
    bytes[1] = (BYTE)(guid->Data1 >> 16);
    bytes[2] = (BYTE)(guid->Data1 >> 8);
    bytes[3] = (BYTE)guid->Data1;
    bytes[4] = (BYTE)(guid->Data2 >> 8);
    bytes[5] = (BYTE)guid->Data2;
    bytes[6] = (BYTE)(guid->Data3 >> 8);
    bytes[7] = (BYTE)guid->Data3;
    memcpy(bytes + 8, guid->Data4, 8);
    WCHAR *d = out;
    *d++ = L'{';
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            *d++ = L'-';
        *d++ = hex[bytes[i] >> 4];
        *d++ = hex[bytes[i] & 15];
    }
    *d++ = L'}';
    *d = 0;
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

/* parses exactly "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}". */
BOOL OleParseGuid(LPCOLESTR s, GUID *guid)
{
    BYTE bytes[16];
    if (s[0] != L'{')
        return FALSE;
    const WCHAR *p = s + 1;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (*p++ != L'-')
                return FALSE;
        }
        int hi = hex_digit(p[0]), lo = hi < 0 ? -1 : hex_digit(p[1]);
        if (hi < 0 || lo < 0)
            return FALSE;
        bytes[i] = (BYTE)(hi << 4 | lo);
        p += 2;
    }
    if (p[0] != L'}' || p[1])
        return FALSE;
    guid->Data1 = (ULONG)bytes[0] << 24 | (ULONG)bytes[1] << 16 | (ULONG)bytes[2] << 8 | bytes[3];
    guid->Data2 = (USHORT)(bytes[4] << 8 | bytes[5]);
    guid->Data3 = (USHORT)(bytes[6] << 8 | bytes[7]);
    memcpy(guid->Data4, bytes + 8, 8);
    return TRUE;
}

WINOLEAPI int WINAPI StringFromGUID2(REFGUID guid, LPOLESTR buffer, int size)
{
    if (!guid || !buffer || size < 39)
        return 0;
    OleFormatGuid(guid, buffer);
    return 39;
}

WINOLEAPI HRESULT WINAPI StringFromCLSID(REFCLSID clsid, LPOLESTR *string)
{
    if (!string)
        return E_INVALIDARG;
    *string = CoTaskMemAlloc(39 * sizeof(WCHAR));
    if (!*string)
        return E_OUTOFMEMORY;
    OleFormatGuid(clsid, *string);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI StringFromIID(REFIID iid, LPOLESTR *string)
{
    return StringFromCLSID(iid, string);
}

/*
 * the user's registrations come before the machine's, as in the merged HKCR view. per user installs such as browsers
 * register their servers below HKCU.
 */
LSTATUS OleOpenClassesKey(LPCWSTR subkey, REGSAM access, HKEY *key)
{
    if (!(access & (KEY_CREATE_SUB_KEY | KEY_SET_VALUE | DELETE))) {
        WCHAR path[MAX_PATH + 32];
        if (wcslen(subkey) + 18 < RTL_NUMBER_OF(path)) {
            wcscpy(path, L"Software\\Classes\\");
            wcscat(path, subkey);
            if (!RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access, key))
                return ERROR_SUCCESS;
        }
    }
    return RegOpenKeyExW(HKEY_CLASSES_ROOT, subkey, 0, access, key);
}

/* expands REG_EXPAND_SZ values. */
static BOOL read_class_string(LPCWSTR subkey, LPCWSTR name, WCHAR *out, DWORD chars)
{
    HKEY key;
    if (OleOpenClassesKey(subkey, KEY_READ, &key))
        return FALSE;
    WCHAR raw[MAX_PATH];
    DWORD type, bytes = sizeof(raw) - sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)raw, &bytes);
    RegCloseKey(key);
    if (status || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(WCHAR))
        return FALSE;
    raw[bytes / sizeof(WCHAR)] = 0;
    if (type == REG_EXPAND_SZ) {
        DWORD n = ExpandEnvironmentStringsW(raw, out, chars);
        return n && n <= chars;
    }
    if (wcslen(raw) >= chars)
        return FALSE;
    wcscpy(out, raw);
    return TRUE;
}

WINOLEAPI HRESULT WINAPI CLSIDFromProgID(LPCOLESTR progid, LPCLSID clsid)
{
    if (!progid || !clsid)
        return E_INVALIDARG;
    WCHAR subkey[MAX_PATH], value[64];
    if (wcslen(progid) > MAX_PATH - 8)
        return CO_E_CLASSSTRING;
    _snwprintf(subkey, MAX_PATH, L"%ls\\CLSID", progid);
    if (!read_class_string(subkey, nullptr, value, RTL_NUMBER_OF(value)) || !OleParseGuid(value, clsid)) {
        memset(clsid, 0, sizeof(*clsid));
        return CO_E_CLASSSTRING;
    }
    return S_OK;
}

WINOLEAPI HRESULT WINAPI ProgIDFromCLSID(REFCLSID clsid, LPOLESTR *progid)
{
    if (!progid)
        return E_INVALIDARG;
    *progid = nullptr;
    WCHAR subkey[64], value[MAX_PATH];
    wcscpy(subkey, L"CLSID\\");
    OleFormatGuid(clsid, subkey + 6);
    wcscat(subkey, L"\\ProgID");
    if (!read_class_string(subkey, nullptr, value, RTL_NUMBER_OF(value)))
        return REGDB_E_CLASSNOTREG;
    SIZE_T bytes = (wcslen(value) + 1) * sizeof(WCHAR);
    *progid = CoTaskMemAlloc(bytes);
    if (!*progid)
        return E_OUTOFMEMORY;
    memcpy(*progid, value, bytes);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CLSIDFromString(LPCOLESTR string, LPCLSID clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    if (!string || !string[0]) {
        memset(clsid, 0, sizeof(*clsid));
        return S_OK;
    }
    if (string[0] == L'{') {
        if (OleParseGuid(string, clsid))
            return S_OK;
        memset(clsid, 0, sizeof(*clsid));
        return CO_E_CLASSSTRING;
    }
    return CLSIDFromProgID(string, clsid);
}

WINOLEAPI HRESULT WINAPI IIDFromString(LPCOLESTR string, LPIID iid)
{
    if (!iid)
        return E_INVALIDARG;
    if (!string) {
        memset(iid, 0, sizeof(*iid));
        return S_OK;
    }
    if (!OleParseGuid(string, iid)) {
        memset(iid, 0, sizeof(*iid));
        return E_INVALIDARG;
    }
    return S_OK;
}

typedef LONG(WINAPI *UUID_CREATE)(GUID *uuid);

WINOLEAPI HRESULT WINAPI CoCreateGuid(GUID *guid)
{
    static UUID_CREATE create;
    if (!guid)
        return E_INVALIDARG;
    if (!create) {
        HMODULE rpcrt4 = LoadLibraryW(L"rpcrt4.dll");
        create = rpcrt4 ? (UUID_CREATE)(void *)GetProcAddress(rpcrt4, "UuidCreate") : nullptr;
        if (!create)
            return HRESULT_FROM_WIN32(GetLastError());
    }
    LONG status = create(guid);
    /* RPC_S_UUID_LOCAL_ONLY still gives a valid uuid. */
    return status && status != 1824 ? HRESULT_FROM_WIN32(status) : S_OK;
}

/* ------------------------------------------------------------------ in process servers */

#define MAX_SERVERS 64

/* dlls loaded for activation, kept until CoFreeUnusedLibraries finds them idle. */
static HMODULE g_servers[MAX_SERVERS];

static void remember_server(HMODULE module)
{
    AcquireSRWLockExclusive(&g_lock);
    int free_slot = -1;
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (g_servers[i] == module) {
            ReleaseSRWLockExclusive(&g_lock);
            /* the cached load keeps it loaded. */
            FreeLibrary(module);
            return;
        }
        if (!g_servers[i] && free_slot < 0)
            free_slot = i;
    }
    if (free_slot >= 0)
        g_servers[free_slot] = module;
    ReleaseSRWLockExclusive(&g_lock);
}

static HRESULT get_inproc_class_object(REFCLSID clsid, REFIID riid, void **object)
{
    HRESULT own;
    if (OrpcBuiltinClassObject(clsid, riid, object, &own))
        return own;
    WCHAR subkey[64], path[MAX_PATH];
    wcscpy(subkey, L"CLSID\\");
    OleFormatGuid(clsid, subkey + 6);
    wcscat(subkey, L"\\InprocServer32");
    if (!read_class_string(subkey, nullptr, path, RTL_NUMBER_OF(path)) || !path[0]) {
        const WCHAR *builtin;
        if (!OrpcBuiltinServer(clsid, &builtin))
            return REGDB_E_CLASSNOTREG;
        wcscpy(path, builtin);
    }
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        DWORD error = GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : CO_E_DLLNOTFOUND;
    }
    LPFNGETCLASSOBJECT get = (LPFNGETCLASSOBJECT)(void *)GetProcAddress(module, "DllGetClassObject");
    if (!get) {
        FreeLibrary(module);
        return CO_E_ERRORINDLL;
    }
    HRESULT hr = get(clsid, riid, object);
    remember_server(module);
    return hr;
}

/* ------------------------------------------------------------------ registered class objects */

typedef struct _CLASS_ENTRY {
    struct _CLASS_ENTRY *Next;
    CLSID Clsid;
    IUnknown *Object;
    DWORD Context;
    DWORD Flags;
    DWORD Cookie;
    /* a single use local server class that was already handed out. */
    BOOL Used;
    /* offered to other processes. a suspended local server class waits for CoResumeClassObjects. */
    BOOL Local, Published;
} CLASS_ENTRY;

static SRWLOCK g_class_lock = SRWLOCK_INIT;
static CLASS_ENTRY *g_classes;
static DWORD g_next_cookie;

WINOLEAPI HRESULT WINAPI CoRegisterClassObject(REFCLSID clsid, IUnknown *object, DWORD context, DWORD flags,
                                               DWORD *cookie)
{
    if (!cookie)
        return E_INVALIDARG;
    *cookie = 0;
    if (!clsid || !object)
        return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    BOOL local = (context & CLSCTX_LOCAL_SERVER) != 0;
    /* a multiple use local server class also serves the process itself. */
    if ((flags & REGCLS_MULTIPLEUSE) && (context & CLSCTX_LOCAL_SERVER))
        context |= CLSCTX_INPROC_SERVER;
    CLASS_ENTRY *entry = OleAllocZero(sizeof(*entry));
    if (!entry)
        return E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_class_lock);
    entry->Clsid = *clsid;
    entry->Object = object;
    entry->Context = context;
    entry->Flags = flags;
    if (!++g_next_cookie)
        ++g_next_cookie;
    entry->Cookie = g_next_cookie;
    entry->Local = local;
    object->lpVtbl->AddRef(object);
    entry->Next = g_classes;
    g_classes = entry;
    *cookie = entry->Cookie;
    BOOL publish = local && !(flags & REGCLS_SUSPENDED);
    entry->Published = publish;
    ReleaseSRWLockExclusive(&g_class_lock);
    if (publish)
        OrpcClassPublish(clsid, object, flags, entry->Cookie);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoRevokeClassObject(DWORD cookie)
{
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&g_class_lock);
    CLASS_ENTRY **link = &g_classes, *entry;
    while ((entry = *link) && entry->Cookie != cookie)
        link = &entry->Next;
    if (entry)
        *link = entry->Next;
    ReleaseSRWLockExclusive(&g_class_lock);
    if (!entry)
        return E_INVALIDARG;
    if (entry->Published)
        OrpcClassRevoke(entry->Cookie);
    entry->Object->lpVtbl->Release(entry->Object);
    OleFree(entry);
    return S_OK;
}

/* returns a referenced object, or NULL. */
static IUnknown *find_registered_class(REFCLSID clsid, DWORD context)
{
    IUnknown *object = nullptr;
    AcquireSRWLockExclusive(&g_class_lock);
    for (CLASS_ENTRY *e = g_classes; e; e = e->Next) {
        DWORD match = e->Context & context;
        if (!match || e->Used || !IsEqualCLSID(&e->Clsid, clsid))
            continue;
        if (!(e->Flags & (REGCLS_MULTIPLEUSE | REGCLS_MULTI_SEPARATE)) && match == CLSCTX_LOCAL_SERVER)
            e->Used = TRUE;
        object = e->Object;
        object->lpVtbl->AddRef(object);
        break;
    }
    ReleaseSRWLockExclusive(&g_class_lock);
    return object;
}

/* ------------------------------------------------------------------ class emulation */

LSTATUS OleOpenClassKey(REFCLSID clsid, LPCWSTR subkey, REGSAM access, HKEY *key)
{
    WCHAR path[MAX_PATH];
    wcscpy(path, L"CLSID\\");
    OleFormatGuid(clsid, path + 6);
    if (subkey) {
        if (wcslen(subkey) > MAX_PATH - 46)
            return ERROR_INVALID_PARAMETER;
        wcscat(path, L"\\");
        wcscat(path, subkey);
    }
    return OleOpenClassesKey(path, access, key);
}

/*
 * reads HKCR\CLSID\{clsid}\AutoConvertTo. without the key the class itself comes back with REGDB_E_KEYMISSING, an
 * unregistered class fails with REGDB_E_CLASSNOTREG.
 */
WINOLEAPI HRESULT WINAPI OleGetAutoConvert(REFCLSID old_clsid, LPCLSID new_clsid)
{
    if (!old_clsid || !new_clsid)
        return E_INVALIDARG;
    *new_clsid = *old_clsid;
    HKEY class_key;
    if (OleOpenClassKey(old_clsid, nullptr, KEY_READ, &class_key))
        return REGDB_E_CLASSNOTREG;
    RegCloseKey(class_key);
    WCHAR subkey[64], value[64];
    wcscpy(subkey, L"CLSID\\");
    OleFormatGuid(old_clsid, subkey + 6);
    wcscat(subkey, L"\\AutoConvertTo");
    CLSID target;
    if (!read_class_string(subkey, nullptr, value, RTL_NUMBER_OF(value)))
        return REGDB_E_KEYMISSING;
    if (!OleParseGuid(value, &target))
        return REGDB_E_INVALIDVALUE;
    *new_clsid = target;
    return S_OK;
}

/* CLSID_NULL removes the key. */
WINOLEAPI HRESULT WINAPI OleSetAutoConvert(REFCLSID old_clsid, REFCLSID new_clsid)
{
    if (!old_clsid || !new_clsid)
        return E_INVALIDARG;
    HKEY key;
    if (OleOpenClassKey(old_clsid, nullptr, KEY_READ | KEY_WRITE, &key))
        return REGDB_E_CLASSNOTREG;
    HRESULT hr = S_OK;
    if (IsEqualCLSID(new_clsid, &CLSID_NULL)) {
        LSTATUS status = RegDeleteKeyExW(key, L"AutoConvertTo", 0, 0);
        if (status && status != ERROR_FILE_NOT_FOUND)
            hr = REGDB_E_WRITEREGDB;
    } else {
        WCHAR value[39];
        OleFormatGuid(new_clsid, value);
        HKEY convert;
        if (RegCreateKeyExW(key, L"AutoConvertTo", 0, nullptr, 0, KEY_WRITE, nullptr, &convert, nullptr))
            hr = REGDB_E_WRITEREGDB;
        else {
            if (RegSetValueExW(convert, nullptr, 0, REG_SZ, (const BYTE *)value, sizeof(value)))
                hr = REGDB_E_WRITEREGDB;
            RegCloseKey(convert);
        }
    }
    RegCloseKey(key);
    return hr;
}

/* reads HKCR\CLSID\{clsid}\TreatAs. */
static BOOL get_treat_as(REFCLSID clsid, CLSID *treat)
{
    WCHAR subkey[64], value[64];
    wcscpy(subkey, L"CLSID\\");
    OleFormatGuid(clsid, subkey + 6);
    wcscat(subkey, L"\\TreatAs");
    return read_class_string(subkey, nullptr, value, RTL_NUMBER_OF(value)) && OleParseGuid(value, treat) &&
           !IsEqualCLSID(treat, &CLSID_NULL);
}

/* without a TreatAs class, returns the class itself with S_FALSE. */
WINOLEAPI HRESULT WINAPI CoGetTreatAsClass(REFCLSID clsid, CLSID *treat)
{
    if (!clsid || !treat)
        return E_INVALIDARG;
    CLSID found;
    if (get_treat_as(clsid, &found)) {
        *treat = found;
        return S_OK;
    }
    *treat = *clsid;
    return S_FALSE;
}

WINOLEAPI HRESULT WINAPI CoTreatAsClass(REFCLSID old_clsid, REFCLSID new_clsid)
{
    if (!old_clsid || !new_clsid)
        return E_INVALIDARG;
    HKEY key;
    if (OleOpenClassKey(old_clsid, nullptr, KEY_READ | KEY_WRITE, &key))
        return REGDB_E_CLASSNOTREG;
    HRESULT hr = S_OK;
    if (IsEqualCLSID(old_clsid, new_clsid) || IsEqualCLSID(new_clsid, &CLSID_NULL)) {
        LSTATUS status = RegDeleteKeyExW(key, L"TreatAs", 0, 0);
        if (status && status != ERROR_FILE_NOT_FOUND)
            hr = REGDB_E_WRITEREGDB;
    } else {
        WCHAR value[39];
        OleFormatGuid(new_clsid, value);
        HKEY treat;
        if (RegCreateKeyExW(key, L"TreatAs", 0, nullptr, 0, KEY_WRITE, nullptr, &treat, nullptr))
            hr = REGDB_E_WRITEREGDB;
        else {
            if (RegSetValueExW(treat, nullptr, 0, REG_SZ, (const BYTE *)value, sizeof(value)))
                hr = REGDB_E_WRITEREGDB;
            RegCloseKey(treat);
        }
    }
    RegCloseKey(key);
    return hr;
}

WINOLEAPI HRESULT WINAPI CoGetClassObject(REFCLSID clsid, DWORD context, void *server, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    CLSID treat;
    if (get_treat_as(clsid, &treat))
        clsid = &treat;
    IUnknown *registered = find_registered_class(clsid, context & (CLSCTX_INPROC | CLSCTX_LOCAL_SERVER));
    if (registered) {
        hr = registered->lpVtbl->QueryInterface(registered, riid, object);
        registered->lpVtbl->Release(registered);
        return hr;
    }
    if (context & CLSCTX_INPROC) {
        hr = get_inproc_class_object(clsid, riid, object);
        if (SUCCEEDED(hr) || !(context & CLSCTX_LOCAL_SERVER))
            return hr;
    }
    if (!(context & CLSCTX_LOCAL_SERVER))
        return REGDB_E_CLASSNOTREG;
    return OrpcClassGet(clsid, riid, object);
}

void OleLocalServersResume(void)
{
    for (;;) {
        CLASS_ENTRY *pending = nullptr;
        AcquireSRWLockExclusive(&g_class_lock);
        for (CLASS_ENTRY *e = g_classes; e; e = e->Next)
            if (e->Local && !e->Published) {
                e->Published = TRUE;
                pending = e;
                break;
            }
        ReleaseSRWLockExclusive(&g_class_lock);
        if (!pending)
            return;
        OrpcClassPublish(&pending->Clsid, pending->Object, pending->Flags, pending->Cookie);
    }
}

WINOLEAPI HRESULT WINAPI CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!riid)
        return E_INVALIDARG;
    IClassFactory *factory;
    HRESULT hr = CoGetClassObject(clsid, context, nullptr, &IID_IClassFactory, (void **)&factory);
    if (FAILED(hr))
        return hr;
    hr = factory->lpVtbl->CreateInstance(factory, outer, riid, object);
    factory->lpVtbl->Release(factory);
    if (FAILED(hr))
        *object = nullptr;
    return hr;
}

WINOLEAPI HRESULT WINAPI CoCreateInstanceEx(REFCLSID clsid, IUnknown *outer, DWORD context, COSERVERINFO *server,
                                            DWORD count, MULTI_QI *results)
{
    if (!count || !results)
        return E_INVALIDARG;
    for (DWORD i = 0; i < count; i++) {
        results[i].pItf = nullptr;
        results[i].hr = E_NOINTERFACE;
    }
    IUnknown *unknown;
    HRESULT hr = CoCreateInstance(clsid, outer, context, &IID_IUnknown, (void **)&unknown);
    if (FAILED(hr)) {
        for (DWORD i = 0; i < count; i++)
            results[i].hr = hr;
        return hr;
    }
    DWORD found = 0;
    for (DWORD i = 0; i < count; i++) {
        results[i].hr = unknown->lpVtbl->QueryInterface(unknown, results[i].pIID, (void **)&results[i].pItf);
        if (SUCCEEDED(results[i].hr))
            found++;
    }
    unknown->lpVtbl->Release(unknown);
    if (!found)
        return E_NOINTERFACE;
    return found == count ? S_OK : CO_S_NOTALLINTERFACES;
}

/* auto_free is ignored: CoFreeUnusedLibraries never frees these, assuming nothing relies on it. */
WINOLEAPI HINSTANCE WINAPI CoLoadLibrary(LPOLESTR file, BOOL auto_free)
{
    if (!file) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return LoadLibraryExW(file, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

WINOLEAPI void WINAPI CoFreeLibrary(HINSTANCE library)
{
    FreeLibrary(library);
}

WINOLEAPI void WINAPI CoFreeUnusedLibrariesEx(DWORD delay, DWORD reserved)
{
    HMODULE idle[MAX_SERVERS];
    int count = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (!g_servers[i])
            continue;
        LPFNCANUNLOADNOW can = (LPFNCANUNLOADNOW)(void *)GetProcAddress(g_servers[i], "DllCanUnloadNow");
        if (can && can() == S_OK) {
            idle[count++] = g_servers[i];
            g_servers[i] = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    for (int i = 0; i < count; i++)
        FreeLibrary(idle[i]);
}

WINOLEAPI void WINAPI CoFreeUnusedLibraries(void)
{
    CoFreeUnusedLibrariesEx(INFINITE, 0);
}

WINOLEAPI HRESULT WINAPI CoLockObjectExternal(IUnknown *object, BOOL lock, BOOL last_unlock_releases)
{
    if (!object)
        return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    if (lock)
        object->lpVtbl->AddRef(object);
    else
        object->lpVtbl->Release(object);
    return S_OK;
}

/*
 * the tables of classes and of the running objects are named objects every program of the machine opens, the processes
 * of the same user with and without the rights of an administrator among them: the one that made them must not keep the
 * other out. The descriptor gives what the mask says to the user of the process, to the system and to the administrators,
 * which is all the processes that have a right to the objects, and not to the restricted processes of a sandbox, which
 * hold a token of the user but are not asked for one of his own sids. It is absolute: the header, then an acl.
 */
PSECURITY_DESCRIPTOR OleUserDescriptor(ULONG mask)
{
    static const BYTE system_sid[12] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
    static const BYTE admins_sid[16] = {1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 0x20, 0x02, 0, 0};
    BYTE *sd = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 512);
    if (!sd)
        return nullptr;
    struct {
        BYTE Revision, Sbz1;
        USHORT Control;
        PVOID Owner, Group, Sacl, Dacl;
    } *header = (void *)sd;
    BYTE *acl = sd + 64, *ace = acl + 8;
    BYTE user[256];
    HANDLE token;
    ULONG returned;
    const BYTE *user_sid = nullptr;
    if (NtOpenProcessToken(NtCurrentProcess(), 0x8, &token) >= 0) {
        if (NtQueryInformationToken(token, 1, user, sizeof(user), &returned) >= 0)
            user_sid = *(BYTE **)user;
        NtClose(token);
    }
    const BYTE *sids[3] = {user_sid, system_sid, admins_sid};
    USHORT count = 0;
    for (int i = 0; i < 3; i++) {
        if (!sids[i])
            continue;
        USHORT length = (USHORT)(8 + 8 + sids[i][1] * 4);
        ace[0] = 0;
        ace[1] = 0;
        *(USHORT *)(ace + 2) = length;
        *(ULONG *)(ace + 4) = mask;
        memcpy(ace + 8, sids[i], 8 + sids[i][1] * 4);
        ace += length;
        count++;
    }
    header->Revision = 1;
    header->Control = 0x0004; /* a dacl is present. */
    header->Dacl = acl;
    acl[0] = 2;
    *(USHORT *)(acl + 2) = (USHORT)(ace - acl);
    *(USHORT *)(acl + 4) = count;
    return sd;
}
