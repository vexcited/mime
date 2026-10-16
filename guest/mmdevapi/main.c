/*
 * the multimedia device api: one render endpoint, the host's default output,
 * whose audio clients play through the host (client.c). there is no capture
 * endpoint.
 */
#include "mmdevapi_private.h"

static constexpr WCHAR DEVICE_ID[] = L"{0.0.0.00000000}.{6d696d65-0000-4000-8000-000000000001}";
static constexpr WCHAR DEVICE_NAME[] = L"Speakers (Mime Audio)";
static constexpr WCHAR DEVICE_DESC[] = L"Speakers";
static constexpr WCHAR INTERFACE_NAME[] = L"Mime Audio";

/* the compiler references it from code using floating point, without a crt to define it. */
int _fltused = 0x9875;

static void *(WINAPI *g_task_alloc)(SIZE_T size);

/* CoTaskMemAlloc from ole32, which owns what the callers free. */
void *MmTaskAlloc(SIZE_T size)
{
    if (!g_task_alloc) {
        HMODULE ole32 = LoadLibraryW(L"ole32.dll");
        g_task_alloc = ole32 ? (void *(WINAPI *)(SIZE_T))(void *)GetProcAddress(ole32, "CoTaskMemAlloc") : nullptr;
        if (!g_task_alloc)
            return nullptr;
    }
    return g_task_alloc(size);
}

void MmMixFormat(WAVEFORMATEXTENSIBLE *f)
{
    memset(f, 0, sizeof(*f));
    f->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f->Format.nChannels = 2;
    f->Format.nSamplesPerSec = 48000;
    f->Format.wBitsPerSample = 32;
    f->Format.nBlockAlign = 8;
    f->Format.nAvgBytesPerSec = 48000 * 8;
    f->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f->Samples.wValidBitsPerSample = 32;
    f->dwChannelMask = 3; /* front left and right */
    f->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

static HRESULT copy_string(const WCHAR *s, LPWSTR *out)
{
    SIZE_T bytes = (wcslen(s) + 1) * sizeof(WCHAR);
    *out = MmTaskAlloc(bytes);
    if (!*out)
        return E_OUTOFMEMORY;
    memcpy(*out, s, bytes);
    return S_OK;
}

/* ------------------------------------------------------------------ property store */

static HRESULT STDMETHODCALLTYPE store_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IPropertyStore)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE static_ref(void *This) { return 2; }

static const struct {
    const GUID *fmtid;
    DWORD pid;
} STORE_KEYS[] = {
    {&FMTID_Device, 14},         /* PKEY_Device_FriendlyName */
    {&FMTID_Device, 2},          /* PKEY_Device_DeviceDesc */
    {&FMTID_DeviceInterface, 2}, /* PKEY_DeviceInterface_FriendlyName */
    {&FMTID_AudioEngine, 0},     /* PKEY_AudioEngine_DeviceFormat */
};

static HRESULT STDMETHODCALLTYPE store_count(void *This, DWORD *count)
{
    if (!count)
        return E_POINTER;
    *count = RTL_NUMBER_OF(STORE_KEYS);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_get_at(void *This, DWORD index, PROPERTYKEY *key)
{
    if (!key)
        return E_POINTER;
    if (index >= RTL_NUMBER_OF(STORE_KEYS))
        return E_INVALIDARG;
    key->fmtid = *STORE_KEYS[index].fmtid;
    key->pid = STORE_KEYS[index].pid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_get_value(void *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    if (!key || !value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    const WCHAR *text = nullptr;
    if (IsEqualGUID(&key->fmtid, &FMTID_Device) && key->pid == 14)
        text = DEVICE_NAME;
    else if (IsEqualGUID(&key->fmtid, &FMTID_Device) && key->pid == 2)
        text = DEVICE_DESC;
    else if (IsEqualGUID(&key->fmtid, &FMTID_DeviceInterface) && key->pid == 2)
        text = INTERFACE_NAME;
    if (text) {
        value->vt = VT_LPWSTR;
        return copy_string(text, &value->pwszVal);
    }
    if (IsEqualGUID(&key->fmtid, &FMTID_AudioEngine) && key->pid == 0) {
        WAVEFORMATEXTENSIBLE *f = MmTaskAlloc(sizeof(*f));
        if (!f)
            return E_OUTOFMEMORY;
        MmMixFormat(f);
        value->vt = VT_BLOB;
        value->blob.cbSize = sizeof(*f);
        value->blob.pBlobData = (BYTE *)f;
        return S_OK;
    }
    /* VT_EMPTY: the property is not set. */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_set_value(void *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE store_commit(void *This) { return S_OK; }

static const IPropertyStoreVtbl store_vtbl = {
    store_query, static_ref, static_ref, store_count, store_get_at, store_get_value, store_set_value, store_commit,
};
static struct { const IPropertyStoreVtbl *vtbl; } g_store = {&store_vtbl};

/* ------------------------------------------------------------------ device */

static struct { const IMMDeviceVtbl *vtbl; } g_device;
static struct { const IMMEndpointVtbl *vtbl; } g_endpoint;

static HRESULT STDMETHODCALLTYPE device_query(void *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMMDevice)) {
        *object = &g_device;
        return S_OK;
    }
    if (IsEqualGUID(riid, &IID_IMMEndpoint)) {
        *object = &g_endpoint;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE device_activate(void *This, REFIID iid, DWORD context, PROPVARIANT *params, void **object)
{
    if (!iid || !object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualGUID(iid, &IID_IAudioClient) || IsEqualGUID(iid, &IID_IAudioClient2) || IsEqualGUID(iid, &IID_IAudioClient3))
        return MmCreateAudioClient(object);
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE device_open_store(void *This, DWORD access, IPropertyStore **store)
{
    if (!store)
        return E_POINTER;
    *store = (IPropertyStore *)&g_store;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_id(void *This, LPWSTR *id)
{
    if (!id)
        return E_POINTER;
    return copy_string(DEVICE_ID, id);
}

static HRESULT STDMETHODCALLTYPE device_get_state(void *This, DWORD *state)
{
    if (!state)
        return E_POINTER;
    *state = DEVICE_STATE_ACTIVE;
    return S_OK;
}

static const IMMDeviceVtbl device_vtbl = {
    device_query, static_ref, static_ref, device_activate, device_open_store, device_get_id, device_get_state,
};

static HRESULT STDMETHODCALLTYPE endpoint_flow(void *This, EDataFlow *flow)
{
    if (!flow)
        return E_POINTER;
    *flow = eRender;
    return S_OK;
}

static const IMMEndpointVtbl endpoint_vtbl = {device_query, static_ref, static_ref, endpoint_flow};

/* ------------------------------------------------------------------ device collections */

typedef struct {
    const IMMDeviceCollectionVtbl *vtbl;
    UINT count;
} COLLECTION;

static HRESULT STDMETHODCALLTYPE collection_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMMDeviceCollection)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE collection_count(void *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = ((COLLECTION *)This)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_item(void *This, UINT index, IMMDevice **device)
{
    if (!device)
        return E_POINTER;
    if (index >= ((COLLECTION *)This)->count) {
        *device = nullptr;
        return E_INVALIDARG;
    }
    *device = (IMMDevice *)&g_device;
    return S_OK;
}

static const IMMDeviceCollectionVtbl collection_vtbl = {
    collection_query, static_ref, static_ref, collection_count, collection_item,
};
static COLLECTION g_renderers = {&collection_vtbl, 1}, g_none = {&collection_vtbl, 0};

/* ------------------------------------------------------------------ enumerator */

static struct { const IMMDeviceEnumeratorVtbl *vtbl; } g_enumerator;

static HRESULT STDMETHODCALLTYPE enum_query(void *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMMDeviceEnumerator)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE enum_endpoints(void *This, EDataFlow flow, DWORD mask, IMMDeviceCollection **devices)
{
    if (!devices)
        return E_POINTER;
    if (flow > eAll || !mask || (mask & ~DEVICE_STATEMASK_ALL)) {
        *devices = nullptr;
        return E_INVALIDARG;
    }
    BOOL render = (flow == eRender || flow == eAll) && (mask & DEVICE_STATE_ACTIVE);
    *devices = (IMMDeviceCollection *)(render ? &g_renderers : &g_none);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_default(void *This, EDataFlow flow, ERole role, IMMDevice **device)
{
    if (!device)
        return E_POINTER;
    *device = nullptr;
    if (flow > eAll || role > eCommunications)
        return E_INVALIDARG;
    if (flow != eRender)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    *device = (IMMDevice *)&g_device;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_get_device(void *This, LPCWSTR id, IMMDevice **device)
{
    if (!id || !device)
        return E_POINTER;
    if (wcscmp(id, DEVICE_ID)) {
        *device = nullptr;
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    *device = (IMMDevice *)&g_device;
    return S_OK;
}

/* the endpoint never changes, so there is nothing to notify. */
static HRESULT STDMETHODCALLTYPE enum_register(void *This, IMMNotificationClient *client)
{
    return client ? S_OK : E_POINTER;
}

static const IMMDeviceEnumeratorVtbl enumerator_vtbl = {
    enum_query, static_ref, static_ref, enum_endpoints, enum_default, enum_get_device, enum_register, enum_register,
};

/* ------------------------------------------------------------------ class factory */

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_ref(IClassFactory *This) { return 2; }

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return enum_query(&g_enumerator, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock) { return S_OK; }

static const IClassFactoryVtbl factory_vtbl = {factory_query, factory_ref, factory_ref, factory_create, factory_lock};
static IClassFactory g_factory = {&factory_vtbl};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(clsid, &CLSID_MMDeviceEnumerator))
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory_query(&g_factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_device.vtbl = &device_vtbl;
        g_endpoint.vtbl = &endpoint_vtbl;
        g_enumerator.vtbl = &enumerator_vtbl;
    }
    return TRUE;
}
