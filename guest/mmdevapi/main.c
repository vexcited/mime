/*
 * the multimedia device api: a render endpoint for the host's default output and a capture endpoint for its
 * default input. their audio clients, in client.c, play and record through the host.
 */
#include "mmdevapi_private.h"

static constexpr WCHAR DEVICE_ID[] = L"{0.0.0.00000000}.{2c9f1a4b-8d3e-4f6a-9b1c-7e5d2a8c4f01}";
static constexpr WCHAR CAPTURE_ID[] = L"{0.0.1.00000000}.{2c9f1a4b-8d3e-4f6a-9b1c-7e5d2a8c4f02}";
static constexpr WCHAR DEVICE_NAME[] = L"Speakers (Qualcomm Audio)";
static constexpr WCHAR DEVICE_DESC[] = L"Speakers";
static constexpr WCHAR CAPTURE_NAME[] = L"Microphone (Qualcomm Audio)";
static constexpr WCHAR CAPTURE_DESC[] = L"Microphone";
static constexpr WCHAR INTERFACE_NAME[] = L"Qualcomm Audio";

/* the compiler references it from code using floating point, without a crt to define it. */
int _fltused = 0x9875;

static void *(WINAPI *g_task_alloc)(SIZE_T size);

/* CoTaskMemAlloc from ole32, which owns what callers free. */
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
    f->dwChannelMask = 3; /* front left and right. */
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

typedef struct STORE {
    const IPropertyStoreVtbl *vtbl;
    BOOL capture;
} STORE;

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
    BOOL capture = ((STORE *)This)->capture;
    if (!key || !value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    const WCHAR *text = nullptr;
    if (IsEqualGUID(&key->fmtid, &FMTID_Device) && key->pid == 14)
        text = capture ? CAPTURE_NAME : DEVICE_NAME;
    else if (IsEqualGUID(&key->fmtid, &FMTID_Device) && key->pid == 2)
        text = capture ? CAPTURE_DESC : DEVICE_DESC;
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
static STORE g_render_store = {&store_vtbl, FALSE}, g_capture_store = {&store_vtbl, TRUE};

/* ------------------------------------------------------------------ device */

/* one object per direction, acting as both device and endpoint. */
typedef struct DEVICE {
    const IMMDeviceVtbl *device;
    const IMMEndpointVtbl *endpoint;
    BOOL capture;
} DEVICE;

static DEVICE g_render_device = {nullptr, nullptr, FALSE}, g_capture_device = {nullptr, nullptr, TRUE};

#define DEVICE_OF(field, This) ((DEVICE *)((BYTE *)(This) - offsetof(DEVICE, field)))

static HRESULT STDMETHODCALLTYPE device_query(void *This, REFIID riid, void **object)
{
    DEVICE *d = DEVICE_OF(device, This);
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMMDevice)) {
        *object = &d->device;
        return S_OK;
    }
    if (IsEqualGUID(riid, &IID_IMMEndpoint)) {
        *object = &d->endpoint;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE endpoint_query(void *This, REFIID riid, void **object)
{
    return device_query(&DEVICE_OF(endpoint, This)->device, riid, object);
}

static HRESULT STDMETHODCALLTYPE device_activate(void *This, REFIID iid, DWORD context, PROPVARIANT *params, void **object)
{
    DEVICE *d = DEVICE_OF(device, This);
    if (!iid || !object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualGUID(iid, &IID_IAudioClient) || IsEqualGUID(iid, &IID_IAudioClient2) || IsEqualGUID(iid, &IID_IAudioClient3))
        return MmCreateAudioClient(d->capture, object);
    if (IsEqualGUID(iid, &IID_IAudioEndpointVolume)) {
        *object = MmEndpointVolume(d->capture);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE device_open_store(void *This, DWORD access, IPropertyStore **store)
{
    DEVICE *d = DEVICE_OF(device, This);
    if (!store)
        return E_POINTER;
    *store = (IPropertyStore *)(d->capture ? &g_capture_store : &g_render_store);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_id(void *This, LPWSTR *id)
{
    if (!id)
        return E_POINTER;
    return copy_string(DEVICE_OF(device, This)->capture ? CAPTURE_ID : DEVICE_ID, id);
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
    *flow = DEVICE_OF(endpoint, This)->capture ? eCapture : eRender;
    return S_OK;
}

static const IMMEndpointVtbl endpoint_vtbl = {endpoint_query, static_ref, static_ref, endpoint_flow};

/* ------------------------------------------------------------------ device collections */

typedef struct {
    const IMMDeviceCollectionVtbl *vtbl;
    UINT count;
    DEVICE *devices[2];
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
    COLLECTION *c = This;
    if (!device)
        return E_POINTER;
    if (index >= c->count) {
        *device = nullptr;
        return E_INVALIDARG;
    }
    *device = (IMMDevice *)&c->devices[index]->device;
    return S_OK;
}

static const IMMDeviceCollectionVtbl collection_vtbl = {
    collection_query, static_ref, static_ref, collection_count, collection_item,
};
static COLLECTION g_renderers = {&collection_vtbl, 1, {&g_render_device, nullptr}},
                  g_capturers = {&collection_vtbl, 1, {&g_capture_device, nullptr}},
                  g_both = {&collection_vtbl, 2, {&g_render_device, &g_capture_device}},
                  g_none = {&collection_vtbl, 0, {nullptr, nullptr}};

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
    if (!(mask & DEVICE_STATE_ACTIVE)) {
        *devices = (IMMDeviceCollection *)&g_none;
        return S_OK;
    }
    *devices = (IMMDeviceCollection *)(flow == eRender ? &g_renderers : flow == eCapture ? &g_capturers : &g_both);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_default(void *This, EDataFlow flow, ERole role, IMMDevice **device)
{
    if (!device)
        return E_POINTER;
    *device = nullptr;
    if (flow > eAll || role > eCommunications)
        return E_INVALIDARG;
    if (flow == eAll)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    *device = (IMMDevice *)(flow == eRender ? &g_render_device.device : &g_capture_device.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_get_device(void *This, LPCWSTR id, IMMDevice **device)
{
    if (!id || !device)
        return E_POINTER;
    if (!wcscmp(id, DEVICE_ID))
        *device = (IMMDevice *)&g_render_device.device;
    else if (!wcscmp(id, CAPTURE_ID))
        *device = (IMMDevice *)&g_capture_device.device;
    else {
        *device = nullptr;
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return S_OK;
}

/* the endpoints never change, so there is nothing to notify. */
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
        g_render_device.device = g_capture_device.device = &device_vtbl;
        g_render_device.endpoint = g_capture_device.endpoint = &endpoint_vtbl;
        g_enumerator.vtbl = &enumerator_vtbl;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ asynchronous activation */

typedef struct IActivateAudioInterfaceAsyncOperation IActivateAudioInterfaceAsyncOperation;
typedef struct IActivateAudioInterfaceCompletionHandler IActivateAudioInterfaceCompletionHandler;

typedef struct IActivateAudioInterfaceCompletionHandlerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IActivateAudioInterfaceCompletionHandler *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IActivateAudioInterfaceCompletionHandler *);
    ULONG(STDMETHODCALLTYPE *Release)(IActivateAudioInterfaceCompletionHandler *);
    HRESULT(STDMETHODCALLTYPE *ActivateCompleted)(IActivateAudioInterfaceCompletionHandler *,
                                                  IActivateAudioInterfaceAsyncOperation *);
} IActivateAudioInterfaceCompletionHandlerVtbl;

struct IActivateAudioInterfaceCompletionHandler {
    const IActivateAudioInterfaceCompletionHandlerVtbl *lpVtbl;
};

typedef struct ACTIVATE_OPERATION {
    const struct ACTIVATE_OPERATION_VTBL *lpVtbl;
    LONG Refs;
    HRESULT Result;
    IUnknown *Interface;
    IActivateAudioInterfaceCompletionHandler *Handler;
} ACTIVATE_OPERATION;

typedef struct ACTIVATE_OPERATION_VTBL {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ACTIVATE_OPERATION *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(ACTIVATE_OPERATION *);
    ULONG(STDMETHODCALLTYPE *Release)(ACTIVATE_OPERATION *);
    HRESULT(STDMETHODCALLTYPE *GetActivateResult)(ACTIVATE_OPERATION *, HRESULT *, IUnknown **);
} ACTIVATE_OPERATION_VTBL;

static const IID IID_IActivateAudioInterfaceAsyncOperation = {
    0x72a22d78, 0xcde4, 0x431d, {0xb8, 0xcc, 0x84, 0x3a, 0x71, 0x19, 0x9b, 0x6d}};
static const IID IID_IAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

static HRESULT STDMETHODCALLTYPE operation_query(ACTIVATE_OPERATION *self, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IActivateAudioInterfaceAsyncOperation) ||
        IsEqualGUID(riid, &IID_IAgileObject)) {
        *object = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE operation_add_ref(ACTIVATE_OPERATION *self)
{
    return InterlockedIncrement(&self->Refs);
}

static ULONG STDMETHODCALLTYPE operation_release(ACTIVATE_OPERATION *self)
{
    ULONG refs = InterlockedDecrement(&self->Refs);
    if (!refs) {
        if (self->Interface)
            self->Interface->lpVtbl->Release(self->Interface);
        if (self->Handler)
            self->Handler->lpVtbl->Release(self->Handler);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return refs;
}

/* the activated interface is handed out once, with its reference. */
static HRESULT STDMETHODCALLTYPE operation_result(ACTIVATE_OPERATION *self, HRESULT *result, IUnknown **object)
{
    if (!result || !object)
        return E_POINTER;
    *result = self->Result;
    *object = self->Interface;
    if (self->Interface)
        self->Interface->lpVtbl->AddRef(self->Interface);
    return S_OK;
}

static const ACTIVATE_OPERATION_VTBL operation_vtbl = {operation_query, operation_add_ref, operation_release,
                                                       operation_result};

static DWORD WINAPI activate_completion(void *parameter)
{
    ACTIVATE_OPERATION *op = parameter;
    op->Handler->lpVtbl->ActivateCompleted(op->Handler, (IActivateAudioInterfaceAsyncOperation *)op);
    operation_release(op);
    return 0;
}

/*
 * the path names an endpoint by id, or a direction's default endpoint by its
 * device interface class. activation runs on the calling thread and the
 * handler is told on another one, as windows calls it from a worker.
 */
DECLSPEC_EXPORT HRESULT WINAPI ActivateAudioInterfaceAsync(LPCWSTR path, REFIID riid, PROPVARIANT *params,
                                                           IActivateAudioInterfaceCompletionHandler *handler,
                                                           IActivateAudioInterfaceAsyncOperation **operation)
{
    static const WCHAR render[] = L"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}";
    static const WCHAR capture[] = L"{2EEF81BE-33FA-4800-9670-1CD474972C3F}";
    if (!path || !riid || !handler || !operation)
        return E_INVALIDARG;
    *operation = nullptr;
    ACTIVATE_OPERATION *op = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*op));
    if (!op)
        return E_OUTOFMEMORY;
    op->lpVtbl = &operation_vtbl;
    op->Refs = 2;
    op->Handler = handler;
    handler->lpVtbl->AddRef(handler);
    if (!_wcsicmp(path, render) || !_wcsicmp(path, DEVICE_ID))
        op->Result = device_activate(&g_render_device.device, riid, CLSCTX_ALL, params, (void **)&op->Interface);
    else if (!_wcsicmp(path, capture) || !_wcsicmp(path, CAPTURE_ID))
        op->Result = device_activate(&g_capture_device.device, riid, CLSCTX_ALL, params, (void **)&op->Interface);
    else
        op->Result = E_NOTIMPL;
    HANDLE thread = CreateThread(nullptr, 0, activate_completion, op, 0, nullptr);
    if (!thread) {
        operation_release(op);
        operation_release(op);
        return E_OUTOFMEMORY;
    }
    CloseHandle(thread);
    *operation = (IActivateAudioInterfaceAsyncOperation *)op;
    return S_OK;
}
