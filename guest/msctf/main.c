/*
 * msctf.dll: the text services framework. the thread manager, document managers and contexts are the program's
 * side of it; the text service is the host's input method, in service.c, which edits a document through the
 * program's own text store as a text service of Windows does. see docs/msctf.md.
 */
#include "msctf_private.h"

BOOL g_ctf_trace;
static LONG g_objects;
HMODULE g_ctf_module;

WINBASEAPI DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, PVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        char value[8];
        g_ctf_module = module;
        g_ctf_trace = GetEnvironmentVariableA("MIME_TSF_TRACE", value, sizeof(value)) != 0;
        break;
    }
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

void *CtfAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void CtfFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

void CtfTrace(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    wvsprintfA(text, format, args);
    va_end(args);
    OutputDebugStringA(text);
}

void CtfObjectCreated(void)
{
    InterlockedIncrement(&g_objects);
}

void CtfObjectDestroyed(void)
{
    InterlockedDecrement(&g_objects);
}

/* ------------------------------------------------------------------ sink lists */

HRESULT SinksAdvise(SINKS *sinks, const IID *const *allowed, ULONG allowed_count, REFIID iid, IUnknown *sink,
                    DWORD *cookie)
{
    if (!cookie)
        return E_INVALIDARG;
    *cookie = TF_INVALID_COOKIE;
    if (!sink)
        return E_INVALIDARG;

    ULONG position = 0;
    while (position < allowed_count && !IsEqualIID(allowed[position], iid))
        position++;
    if (position == allowed_count)
        return CONNECT_E_CANNOTCONNECT;

    /* the number of a new sink follows the highest one of its interface still advised. */
    DWORD highest = 0;
    for (ULONG i = 0; i < sinks->Count; i++) {
        if (sinks->Items[i].Index == position && (sinks->Items[i].Cookie & 0xFFFFFF) > highest)
            highest = sinks->Items[i].Cookie & 0xFFFFFF;
    }
    if (highest >= 0xFFFFFF)
        return CONNECT_E_ADVISELIMIT;

    void *as_iface;
    if (FAILED(sink->lpVtbl->QueryInterface(sink, iid, &as_iface)))
        return E_FAIL;

    if (sinks->Count == sinks->Capacity) {
        ULONG capacity = sinks->Capacity ? sinks->Capacity * 2 : 8;
        SINK *grown = CtfAlloc(capacity * sizeof(*grown));
        if (!grown) {
            ((IUnknown *)as_iface)->lpVtbl->Release(as_iface);
            return E_OUTOFMEMORY;
        }
        if (sinks->Count)
            memcpy(grown, sinks->Items, sinks->Count * sizeof(*grown));
        CtfFree(sinks->Items);
        sinks->Items = grown;
        sinks->Capacity = capacity;
    }

    SINK *entry = &sinks->Items[sinks->Count++];
    entry->Cookie = position << 24 | (highest + 1);
    entry->Index = position;
    entry->Iid = *iid;
    entry->Sink = as_iface;
    *cookie = entry->Cookie;
    return S_OK;
}

HRESULT SinksUnadvise(SINKS *sinks, DWORD cookie)
{
    if (cookie == TF_INVALID_COOKIE)
        return E_INVALIDARG;
    for (ULONG i = 0; i < sinks->Count; i++) {
        if (sinks->Items[i].Cookie != cookie)
            continue;
        IUnknown *sink = sinks->Items[i].Sink;
        memmove(&sinks->Items[i], &sinks->Items[i + 1], (sinks->Count - i - 1) * sizeof(SINK));
        sinks->Count--;
        sink->lpVtbl->Release(sink);
        return S_OK;
    }
    return CONNECT_E_NOCONNECTION;
}

void SinksFree(SINKS *sinks)
{
    for (ULONG i = 0; i < sinks->Count; i++)
        sinks->Items[i].Sink->lpVtbl->Release(sinks->Items[i].Sink);
    CtfFree(sinks->Items);
    memset(sinks, 0, sizeof(*sinks));
}

ULONG SinksSnapshot(SINKS *sinks, REFIID iid, IUnknown ***out)
{
    *out = nullptr;
    ULONG n = 0;
    for (ULONG i = 0; i < sinks->Count; i++)
        if (IsEqualIID(&sinks->Items[i].Iid, iid))
            n++;
    if (!n)
        return 0;
    IUnknown **items = CtfAlloc(n * sizeof(*items));
    if (!items)
        return 0;
    ULONG k = 0;
    for (ULONG i = 0; i < sinks->Count && k < n; i++) {
        if (!IsEqualIID(&sinks->Items[i].Iid, iid))
            continue;
        items[k] = sinks->Items[i].Sink;
        items[k]->lpVtbl->AddRef(items[k]);
        k++;
    }
    *out = items;
    return k;
}

void SinksSnapshotFree(IUnknown **items, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        items[i]->lpVtbl->Release(items[i]);
    CtfFree(items);
}

/* ------------------------------------------------------------------ class objects */

typedef struct {
    IClassFactory iface;
    CLSID clsid;
} FACTORY;

static HRESULT create_instance(REFCLSID clsid, REFIID riid, void **object)
{
    if (IsEqualCLSID(clsid, &CLSID_TF_ThreadMgr))
        return TmCreateObject(riid, object);
    if (IsEqualCLSID(clsid, &CLSID_TF_InputProcessorProfiles))
        return ProfilesCreate(riid, object);
    if (IsEqualCLSID(clsid, &CLSID_TF_CategoryMgr))
        return CategoryMgrCreate(riid, object);
    if (IsEqualCLSID(clsid, &CLSID_TF_DisplayAttributeMgr))
        return DisplayAttributeMgrCreate(riid, object);
    return CLASS_E_CLASSNOTAVAILABLE;
}

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

/* the factories are static: nothing to count. */
static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    FACTORY *factory = CONTAINING_RECORD(This, FACTORY, iface);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return create_instance(&factory->clsid, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&g_objects);
    else
        InterlockedDecrement(&g_objects);
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create,
                                               factory_lock};
static FACTORY g_factories[4];

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    const CLSID *known[4] = {&CLSID_TF_ThreadMgr, &CLSID_TF_InputProcessorProfiles, &CLSID_TF_CategoryMgr,
                             &CLSID_TF_DisplayAttributeMgr};
    for (int i = 0; i < 4; i++) {
        if (!IsEqualCLSID(clsid, known[i]))
            continue;
        g_factories[i].iface.lpVtbl = &factory_vtbl;
        g_factories[i].clsid = *known[i];
        return factory_query(&g_factories[i].iface, riid, object);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_objects ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ creation functions */

HRESULT WINAPI TF_CreateThreadMgr(ITfThreadMgr **thread_mgr)
{
    if (!thread_mgr)
        return E_INVALIDARG;
    return TmCreateObject(&IID_ITfThreadMgr, (void **)thread_mgr);
}

HRESULT WINAPI TF_GetThreadMgr(ITfThreadMgr **thread_mgr)
{
    if (!thread_mgr)
        return E_INVALIDARG;
    *thread_mgr = nullptr;
    if (!TmCurrent())
        return E_FAIL;
    return TmCreateObject(&IID_ITfThreadMgr, (void **)thread_mgr);
}

HRESULT WINAPI TF_CreateInputProcessorProfiles(ITfInputProcessorProfiles **profiles)
{
    if (!profiles)
        return E_INVALIDARG;
    return ProfilesCreate(&IID_ITfInputProcessorProfiles, (void **)profiles);
}

HRESULT WINAPI TF_CreateCategoryMgr(ITfCategoryMgr **category_mgr)
{
    if (!category_mgr)
        return E_INVALIDARG;
    return CategoryMgrCreate(&IID_ITfCategoryMgr, (void **)category_mgr);
}

HRESULT WINAPI TF_CreateDisplayAttributeMgr(ITfDisplayAttributeMgr **display_attribute_mgr)
{
    if (!display_attribute_mgr)
        return E_INVALIDARG;
    return DisplayAttributeMgrCreate(&IID_ITfDisplayAttributeMgr, (void **)display_attribute_mgr);
}
