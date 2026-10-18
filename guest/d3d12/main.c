/*
 * the exports of d3d12.dll: devices, one per adapter as windows hands them
 * out, root signature serialization and the configuration objects of
 * D3D12GetInterface. the debug layer does nothing, as nothing validates.
 */
#include "d3d12_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

void D12Log(const char *format, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

static void *d12_alloc(void *context, size_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void d12_free(void *context, void *p) { HeapFree(GetProcessHeap(), 0, p); }
const MimeAllocator g_d12_allocator = {d12_alloc, d12_free, nullptr};

/* ------------------------------------------------------------------ blobs */

typedef struct D12Blob {
    ID3DBlob iface;
    LONG refs;
    SIZE_T size;
    BYTE data[];
} D12Blob;

static HRESULT STDMETHODCALLTYPE blob_query(ID3DBlob *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D10Blob)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE blob_add_ref(ID3DBlob *iface) { return InterlockedIncrement(&((D12Blob *)iface)->refs); }

static ULONG STDMETHODCALLTYPE blob_release(ID3DBlob *iface)
{
    ULONG refs = InterlockedDecrement(&((D12Blob *)iface)->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, iface);
    return refs;
}

static void *STDMETHODCALLTYPE blob_pointer(ID3DBlob *iface) { return ((D12Blob *)iface)->data; }
static SIZE_T STDMETHODCALLTYPE blob_size(ID3DBlob *iface) { return ((D12Blob *)iface)->size; }

static const ID3D10BlobVtbl g_blob_vtbl = {blob_query, blob_add_ref, blob_release, blob_pointer, blob_size};

HRESULT D12CreateBlob(const void *data, SIZE_T size, ID3DBlob **out)
{
    D12Blob *blob = HeapAlloc(GetProcessHeap(), 0, sizeof(*blob) + size);
    if (!blob)
        return E_OUTOFMEMORY;
    blob->iface.lpVtbl = &g_blob_vtbl;
    blob->refs = 1;
    blob->size = size;
    if (data)
        memcpy(blob->data, data, size);
    *out = &blob->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ devices */

/* the devices made, one per adapter, as D3D12CreateDevice returns the adapter's device while it lives. */
static SRWLOCK g_devices_lock = SRWLOCK_INIT;
static D12Device *g_devices[16];

static HRESULT first_adapter(IDXGIAdapter **adapter)
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    HRESULT(WINAPI * create)(REFIID, void **) =
        dxgi ? (HRESULT(WINAPI *)(REFIID, void **))(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    if (!create)
        return E_FAIL;
    IDXGIFactory1 *factory;
    HRESULT hr = create(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr))
        return hr;
    hr = factory->lpVtbl->EnumAdapters(factory, 0, adapter);
    factory->lpVtbl->Release(factory);
    return hr == DXGI_ERROR_NOT_FOUND ? E_FAIL : hr;
}

/* called by the device when its last reference goes, under the lock so no creation finds it dying. */
BOOL D12DeviceUnlist(D12Device *device)
{
    AcquireSRWLockExclusive(&g_devices_lock);
    BOOL dead = device->refs == 0;
    if (dead)
        for (UINT i = 0; i < RTL_NUMBER_OF(g_devices); i++)
            if (g_devices[i] == device)
                g_devices[i] = nullptr;
    ReleaseSRWLockExclusive(&g_devices_lock);
    return dead;
}

static HRESULT create_device(IUnknown *unknown, D3D_FEATURE_LEVEL level, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    switch (level) {
    case D3D_FEATURE_LEVEL_1_0_GENERIC:
    case D3D_FEATURE_LEVEL_1_0_CORE:
    case D3D_FEATURE_LEVEL_11_0:
    case D3D_FEATURE_LEVEL_11_1:
    case D3D_FEATURE_LEVEL_12_0:
    case D3D_FEATURE_LEVEL_12_1:
    case D3D_FEATURE_LEVEL_12_2:
        break;
    default:
        return E_INVALIDARG;
    }
    if (level > D3D_FEATURE_LEVEL_12_2)
        return DXGI_ERROR_UNSUPPORTED;
    IDXGIAdapter *adapter = nullptr;
    HRESULT hr;
    if (unknown) {
        if (FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_IDXGIAdapter, (void **)&adapter)))
            return E_INVALIDARG;
    } else if (FAILED(hr = first_adapter(&adapter))) {
        return hr;
    }
    DXGI_ADAPTER_DESC desc;
    adapter->lpVtbl->GetDesc(adapter, &desc);

    D12Device *device = nullptr;
    AcquireSRWLockExclusive(&g_devices_lock);
    for (UINT i = 0; i < RTL_NUMBER_OF(g_devices) && !device; i++)
        if (g_devices[i] && g_devices[i]->adapter_luid.LowPart == desc.AdapterLuid.LowPart &&
            g_devices[i]->adapter_luid.HighPart == desc.AdapterLuid.HighPart && g_devices[i]->refs) {
            device = g_devices[i];
            InterlockedIncrement(&device->refs);
        }
    if (!device) {
        hr = D12CreateDevice(adapter, level, &device);
        if (SUCCEEDED(hr)) {
            device->adapter_luid = desc.AdapterLuid;
            for (UINT i = 0; i < RTL_NUMBER_OF(g_devices); i++)
                if (!g_devices[i]) {
                    g_devices[i] = device;
                    break;
                }
        }
    } else {
        hr = device->feature_level >= level ? S_OK : DXGI_ERROR_UNSUPPORTED;
        if (FAILED(hr))
            InterlockedDecrement(&device->refs);
    }
    ReleaseSRWLockExclusive(&g_devices_lock);
    adapter->lpVtbl->Release(adapter);
    if (FAILED(hr))
        return hr;
    if (!out) {
        device->iface.lpVtbl->Release(&device->iface);
        return S_FALSE;
    }
    hr = device->iface.lpVtbl->QueryInterface(&device->iface, riid, out);
    device->iface.lpVtbl->Release(&device->iface);
    return hr;
}

D3D12API HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL level, REFIID riid,
                                                       void **out)
{
    ULONGLONG began = GetTickCount64();
    HRESULT hr = create_device(adapter, level, riid, out);
    ULONGLONG took = GetTickCount64() - began;
    if (took >= 20) {
        char text[64];
        _snprintf(text, sizeof(text), "d3d12: a device took %lu ms\n", (unsigned long)took);
        OutputDebugStringA(text);
    }
    return hr;
}

/* ------------------------------------------------------------------ the debug layer */

/* ID3D12Debug6 and what came before it; ID3D12Debug2 alone differs, its own interface. */
typedef struct D12Debug {
    ID3D12Debug6 iface;
    ID3D12Debug2 debug2;
    LONG refs;
} D12Debug;

static HRESULT STDMETHODCALLTYPE debug_query(ID3D12Debug6 *iface, REFIID riid, void **out)
{
    D12Debug *debug = CONTAINING_RECORD(iface, D12Debug, iface);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Debug) ||
        IsEqualGUID(riid, &IID_ID3D12Debug1) || IsEqualGUID(riid, &IID_ID3D12Debug3) ||
        IsEqualGUID(riid, &IID_ID3D12Debug4) || IsEqualGUID(riid, &IID_ID3D12Debug5) ||
        IsEqualGUID(riid, &IID_ID3D12Debug6))
        *out = &debug->iface;
    else if (IsEqualGUID(riid, &IID_ID3D12Debug2))
        *out = &debug->debug2;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&debug->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE debug_add_ref(ID3D12Debug6 *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, D12Debug, iface)->refs);
}

static ULONG STDMETHODCALLTYPE debug_release(ID3D12Debug6 *iface)
{
    D12Debug *debug = CONTAINING_RECORD(iface, D12Debug, iface);
    ULONG refs = InterlockedDecrement(&debug->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, debug);
    return refs;
}

static void STDMETHODCALLTYPE debug_enable(ID3D12Debug6 *iface) {}
static void STDMETHODCALLTYPE debug_set_bool(ID3D12Debug6 *iface, BOOL enable) {}
static void STDMETHODCALLTYPE debug_set_flags(ID3D12Debug6 *iface, UINT flags) {}

static const struct ID3D12Debug6Vtbl g_debug_vtbl = {
    debug_query,    debug_add_ref,   debug_release,  debug_enable,   debug_set_bool,
    debug_set_bool, debug_set_flags, debug_enable,   debug_set_bool, debug_set_bool,
};

static HRESULT STDMETHODCALLTYPE debug2_query(ID3D12Debug2 *iface, REFIID riid, void **out)
{
    return debug_query(&CONTAINING_RECORD(iface, D12Debug, debug2)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE debug2_add_ref(ID3D12Debug2 *iface)
{
    return debug_add_ref(&CONTAINING_RECORD(iface, D12Debug, debug2)->iface);
}

static ULONG STDMETHODCALLTYPE debug2_release(ID3D12Debug2 *iface)
{
    return debug_release(&CONTAINING_RECORD(iface, D12Debug, debug2)->iface);
}

static void STDMETHODCALLTYPE debug2_set_flags(ID3D12Debug2 *iface, UINT flags) {}

static const struct ID3D12Debug2Vtbl g_debug2_vtbl = {debug2_query, debug2_add_ref, debug2_release, debug2_set_flags};

static HRESULT create_debug(REFIID riid, void **out)
{
    D12Debug *debug = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*debug));
    if (!debug)
        return E_OUTOFMEMORY;
    debug->iface.lpVtbl = &g_debug_vtbl;
    debug->debug2.lpVtbl = &g_debug2_vtbl;
    debug->refs = 1;
    HRESULT hr = debug_query(&debug->iface, riid, out);
    debug_release(&debug->iface);
    return hr;
}

D3D12API HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return create_debug(riid, out);
}

/* ------------------------------------------------------------------ device removed extended data settings */

typedef struct D12Dred {
    ID3D12DeviceRemovedExtendedDataSettings iface;
    LONG refs;
} D12Dred;

static HRESULT STDMETHODCALLTYPE dred_query(ID3D12DeviceRemovedExtendedDataSettings *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12DeviceRemovedExtendedDataSettings) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceRemovedExtendedDataSettings1)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dred_add_ref(ID3D12DeviceRemovedExtendedDataSettings *iface)
{
    return InterlockedIncrement(&((D12Dred *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE dred_release(ID3D12DeviceRemovedExtendedDataSettings *iface)
{
    ULONG refs = InterlockedDecrement(&((D12Dred *)iface)->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, iface);
    return refs;
}

static void STDMETHODCALLTYPE dred_set(ID3D12DeviceRemovedExtendedDataSettings *iface, UINT value) {}

static const struct ID3D12DeviceRemovedExtendedDataSettingsVtbl g_dred_vtbl = {
    dred_query, dred_add_ref, dred_release, dred_set, dred_set, dred_set, dred_set, (void *)dred_set,
};

/* what windows exports as D3D12DeviceRemovedExtendedData: its version first, the rest zero. */
__declspec(dllexport) UINT D3D12DeviceRemovedExtendedData[16] = {3};

/* ------------------------------------------------------------------ device factories and the sdk configuration */

typedef struct D12Factory {
    ID3D12DeviceFactory iface;
    LONG refs;
    UINT flags;
} D12Factory;

static HRESULT STDMETHODCALLTYPE factory_query(ID3D12DeviceFactory *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12DeviceFactory)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(ID3D12DeviceFactory *iface)
{
    return InterlockedIncrement(&((D12Factory *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE factory_release(ID3D12DeviceFactory *iface)
{
    ULONG refs = InterlockedDecrement(&((D12Factory *)iface)->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, iface);
    return refs;
}

static HRESULT STDMETHODCALLTYPE factory_state(ID3D12DeviceFactory *iface) { return S_OK; }

static HRESULT STDMETHODCALLTYPE factory_set_flags(ID3D12DeviceFactory *iface, UINT flags)
{
    ((D12Factory *)iface)->flags = flags;
    return S_OK;
}

static UINT STDMETHODCALLTYPE factory_get_flags(ID3D12DeviceFactory *iface) { return ((D12Factory *)iface)->flags; }

static HRESULT get_interface(REFCLSID clsid, REFIID riid, void **out);

static HRESULT STDMETHODCALLTYPE factory_configuration(ID3D12DeviceFactory *iface, REFCLSID clsid, REFIID riid,
                                                       void **out)
{
    return get_interface(clsid, riid, out);
}

static HRESULT STDMETHODCALLTYPE factory_experimental(ID3D12DeviceFactory *iface, UINT count, const IID *iids,
                                                      void *configs, UINT *sizes)
{
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE factory_create_device(ID3D12DeviceFactory *iface, IUnknown *adapter,
                                                       D3D_FEATURE_LEVEL level, REFIID riid, void **out)
{
    return create_device(adapter, level, riid, out);
}

static const struct ID3D12DeviceFactoryVtbl g_factory_vtbl = {
    factory_query,     factory_add_ref,       factory_release,      factory_state,         factory_state,
    factory_set_flags, factory_get_flags,     factory_configuration, factory_experimental, factory_create_device,
};

static HRESULT create_factory(REFIID riid, void **out)
{
    D12Factory *factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;
    factory->iface.lpVtbl = &g_factory_vtbl;
    factory->refs = 1;
    HRESULT hr = factory_query(&factory->iface, riid, out);
    factory_release(&factory->iface);
    return hr;
}

typedef struct D12SdkConfiguration {
    ID3D12SDKConfiguration1 iface;
    LONG refs;
} D12SdkConfiguration;

static HRESULT STDMETHODCALLTYPE sdk_query(ID3D12SDKConfiguration1 *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12SDKConfiguration) ||
        IsEqualGUID(riid, &IID_ID3D12SDKConfiguration1)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sdk_add_ref(ID3D12SDKConfiguration1 *iface)
{
    return InterlockedIncrement(&((D12SdkConfiguration *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE sdk_release(ID3D12SDKConfiguration1 *iface)
{
    ULONG refs = InterlockedDecrement(&((D12SdkConfiguration *)iface)->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, iface);
    return refs;
}

/* every sdk version is this dll's own: there is no d3d12core.dll to load. */
static HRESULT STDMETHODCALLTYPE sdk_set_version(ID3D12SDKConfiguration1 *iface, UINT version, LPCSTR path)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sdk_create_factory(ID3D12SDKConfiguration1 *iface, UINT version, LPCSTR path,
                                                    REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return create_factory(riid, out);
}

static void STDMETHODCALLTYPE sdk_free_unused(ID3D12SDKConfiguration1 *iface) {}

static const struct ID3D12SDKConfiguration1Vtbl g_sdk_vtbl = {
    sdk_query, sdk_add_ref, sdk_release, sdk_set_version, sdk_create_factory, sdk_free_unused,
};

static HRESULT get_interface(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(clsid, &CLSID_D3D12Debug))
        return create_debug(riid, out);
    if (IsEqualGUID(clsid, &CLSID_D3D12DeviceFactory))
        return create_factory(riid, out);
    if (IsEqualGUID(clsid, &CLSID_D3D12DeviceRemovedExtendedData)) {
        D12Dred *dred = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dred));
        if (!dred)
            return E_OUTOFMEMORY;
        dred->iface.lpVtbl = &g_dred_vtbl;
        dred->refs = 1;
        HRESULT hr = dred_query(&dred->iface, riid, out);
        dred_release(&dred->iface);
        return hr;
    }
    if (IsEqualGUID(clsid, &CLSID_D3D12SDKConfiguration)) {
        D12SdkConfiguration *sdk = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sdk));
        if (!sdk)
            return E_OUTOFMEMORY;
        sdk->iface.lpVtbl = &g_sdk_vtbl;
        sdk->refs = 1;
        HRESULT hr = sdk_query(&sdk->iface, riid, out);
        sdk_release(&sdk->iface);
        return hr;
    }
    return REGDB_E_CLASSNOTREG;
}

D3D12API HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID riid, void **out)
{
    return get_interface(clsid, riid, out);
}

/* experimental features need developer mode, which is off. */
D3D12API HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT count, const IID *iids, void *configs,
                                                                     UINT *sizes)
{
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ root signatures */

D3D12API HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *desc,
                                                                 D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob,
                                                                 ID3DBlob **error)
{
    if (!desc || version != D3D_ROOT_SIGNATURE_VERSION_1_0) {
        if (blob)
            *blob = nullptr;
        if (error)
            *error = nullptr;
        return E_INVALIDARG;
    }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {D3D_ROOT_SIGNATURE_VERSION_1_0};
    versioned.Desc_1_0 = *desc;
    return D12SerializeRootSignature(&versioned, blob, error);
}

D3D12API HRESULT WINAPI D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **error)
{
    return D12SerializeRootSignature(desc, blob, error);
}

/*
 * a deserializer keeps the description it parsed, first, and those it
 * converted it to by version, each one block, handed out as long as it
 * lives.
 */
typedef struct D12Deserializer {
    ID3D12VersionedRootSignatureDeserializer iface;
    ID3D12RootSignatureDeserializer v10;
    LONG refs;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC *descs[4];
} D12Deserializer;

static HRESULT STDMETHODCALLTYPE deserializer_query(ID3D12VersionedRootSignatureDeserializer *iface, REFIID riid,
                                                    void **out)
{
    D12Deserializer *d = CONTAINING_RECORD(iface, D12Deserializer, iface);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12VersionedRootSignatureDeserializer))
        *out = &d->iface;
    else if (IsEqualGUID(riid, &IID_ID3D12RootSignatureDeserializer))
        *out = &d->v10;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&d->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE deserializer_add_ref(ID3D12VersionedRootSignatureDeserializer *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, D12Deserializer, iface)->refs);
}

static ULONG STDMETHODCALLTYPE deserializer_release(ID3D12VersionedRootSignatureDeserializer *iface)
{
    D12Deserializer *d = CONTAINING_RECORD(iface, D12Deserializer, iface);
    ULONG refs = InterlockedDecrement(&d->refs);
    if (!refs) {
        for (UINT i = 0; i < RTL_NUMBER_OF(d->descs); i++)
            HeapFree(GetProcessHeap(), 0, d->descs[i]);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return refs;
}

HRESULT D12ConvertRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version,
                                D3D12_VERSIONED_ROOT_SIGNATURE_DESC **out);

static HRESULT STDMETHODCALLTYPE deserializer_at_version(ID3D12VersionedRootSignatureDeserializer *iface,
                                                         D3D_ROOT_SIGNATURE_VERSION version,
                                                         const D3D12_VERSIONED_ROOT_SIGNATURE_DESC **out)
{
    D12Deserializer *d = CONTAINING_RECORD(iface, D12Deserializer, iface);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (version < D3D_ROOT_SIGNATURE_VERSION_1_0 || version > D3D_ROOT_SIGNATURE_VERSION_1_2)
        return E_INVALIDARG;
    if (d->descs[0]->Version == version) {
        *out = d->descs[0];
        return S_OK;
    }
    if (!d->descs[version]) {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC *converted;
        HRESULT hr = D12ConvertRootSignature(d->descs[0], version, &converted);
        if (FAILED(hr))
            return hr;
        if (InterlockedCompareExchangePointer((void **)&d->descs[version], converted, nullptr))
            HeapFree(GetProcessHeap(), 0, converted);
    }
    *out = d->descs[version];
    return S_OK;
}

static const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *STDMETHODCALLTYPE
deserializer_unconverted(ID3D12VersionedRootSignatureDeserializer *iface)
{
    return CONTAINING_RECORD(iface, D12Deserializer, iface)->descs[0];
}

static const struct ID3D12VersionedRootSignatureDeserializerVtbl g_deserializer_vtbl = {
    deserializer_query, deserializer_add_ref, deserializer_release, deserializer_at_version, deserializer_unconverted,
};

static HRESULT STDMETHODCALLTYPE v10_query(ID3D12RootSignatureDeserializer *iface, REFIID riid, void **out)
{
    return deserializer_query(&CONTAINING_RECORD(iface, D12Deserializer, v10)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE v10_add_ref(ID3D12RootSignatureDeserializer *iface)
{
    return deserializer_add_ref(&CONTAINING_RECORD(iface, D12Deserializer, v10)->iface);
}

static ULONG STDMETHODCALLTYPE v10_release(ID3D12RootSignatureDeserializer *iface)
{
    return deserializer_release(&CONTAINING_RECORD(iface, D12Deserializer, v10)->iface);
}

static const D3D12_ROOT_SIGNATURE_DESC *STDMETHODCALLTYPE v10_desc(ID3D12RootSignatureDeserializer *iface)
{
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc;
    if (FAILED(deserializer_at_version(&CONTAINING_RECORD(iface, D12Deserializer, v10)->iface,
                                       D3D_ROOT_SIGNATURE_VERSION_1_0, &desc)))
        return nullptr;
    return &desc->Desc_1_0;
}

static const struct ID3D12RootSignatureDeserializerVtbl g_v10_vtbl = {v10_query, v10_add_ref, v10_release, v10_desc};

static HRESULT create_deserializer(const void *data, SIZE_T size, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!data || !size)
        return E_INVALIDARG;
    D12Deserializer *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->iface.lpVtbl = &g_deserializer_vtbl;
    d->v10.lpVtbl = &g_v10_vtbl;
    d->refs = 1;
    HRESULT hr = D12ParseRootSignature(data, size, &d->descs[0]);
    if (SUCCEEDED(hr))
        hr = deserializer_query(&d->iface, riid, out);
    deserializer_release(&d->iface);
    return hr;
}

D3D12API HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T size, REFIID riid,
                                                                          void **out)
{
    return create_deserializer(data, size, riid, out);
}

D3D12API HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T size,
                                                                                   REFIID riid, void **out)
{
    return create_deserializer(data, size, riid, out);
}

/* ------------------------------------------------------------------ the rest of windows' exports */

D3D12API HRESULT WINAPI D3D12CoreCreateLayeredDevice(const void *args) { return E_NOTIMPL; }
__declspec(dllexport) SIZE_T WINAPI D3D12CoreGetLayeredDeviceSize(const void *args, UINT count) { return 0; }
D3D12API HRESULT WINAPI D3D12CoreRegisterLayers(const void *layers, UINT count) { return E_NOTIMPL; }

/* pix events, which nothing records: the thread info leaves no room, so events ask for a block and get none. */
static UINT64 g_pix_info[4];
__declspec(dllexport) void *WINAPI D3D12PIXGetThreadInfo(void) { return g_pix_info; }
__declspec(dllexport) UINT64 WINAPI D3D12PIXEventsReplaceBlock(BOOL get_new) { return 0; }
__declspec(dllexport) void WINAPI D3D12PIXNotifyWakeFromFenceSignal(HANDLE event) {}
__declspec(dllexport) void WINAPI D3D12PIXReportCounter(const WCHAR *name, float value) {}

__declspec(dllexport) void WINAPI SetAppCompatStringPointer(SIZE_T size, const char *text) {}

__declspec(dllexport) BOOL WINAPI GetBehaviorValue(const char *name, UINT64 *value) { return FALSE; }

/* ordinal 99, D3D12CreateAdditionalDevice: a device beside the adapter's own, which windows' layers make. */
D3D12API HRESULT WINAPI D3D12CreateAdditionalDevice(IUnknown *adapter, D3D_FEATURE_LEVEL level,
                                                                 REFIID riid, void **out)
{
    return create_device(adapter, level, riid, out);
}
