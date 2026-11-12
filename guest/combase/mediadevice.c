/*
 * Windows.Media.Devices.MediaDevice: the selectors of audio and video devices, the ids of the default audio endpoints
 * and the events of their change. the ids are the device interface paths of the endpoints mmdevapi has.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>
#include <mime/win32/propsys.h>

const WCHAR CbMediaDeviceClass[] = L"Windows.Media.Devices.MediaDevice";

MIME_DEFINE_GUID(IID_IMediaDeviceStatics, 0xAA2D9A40, 0x909F, 0x4BBA, 0xBF, 0x8B, 0x0C, 0x0D, 0x29, 0x6F, 0x14, 0xF0);
MIME_DEFINE_GUID(CLSID_CbMMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
MIME_DEFINE_GUID(IID_CbMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
MIME_DEFINE_GUID(IID_CbClassFactory, 0x00000001, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

#define AUDIO_RENDER_CLASS L"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}"
#define AUDIO_CAPTURE_CLASS L"{2EEF81BE-33FA-4800-9670-1CD474972C3F}"
#define VIDEO_CAPTURE_CLASS L"{E5323777-F976-4F5B-9B55-B94699C46E44}"
#define SELECTOR(class)                                                                                                \
    L"System.Devices.InterfaceClassGuid:=\"" class L"\" AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True"

typedef struct IMediaDeviceStatics IMediaDeviceStatics;
#define IMediaDeviceStatics_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetAudioCaptureSelector)(T * This, HSTRING * value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetAudioRenderSelector)(T * This, HSTRING * value);                                     \
    HRESULT(STDMETHODCALLTYPE *GetVideoCaptureSelector)(T * This, HSTRING * value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetDefaultAudioCaptureId)(T * This, INT32 role, HSTRING * value);                       \
    HRESULT(STDMETHODCALLTYPE *GetDefaultAudioRenderId)(T * This, INT32 role, HSTRING * value);                        \
    HRESULT(STDMETHODCALLTYPE *add_DefaultAudioCaptureDeviceChanged)(T * This, IUnknown * handler,                     \
                                                                     EventRegistrationToken * token);                  \
    HRESULT(STDMETHODCALLTYPE *remove_DefaultAudioCaptureDeviceChanged)(T * This, EventRegistrationToken token);       \
    HRESULT(STDMETHODCALLTYPE *add_DefaultAudioRenderDeviceChanged)(T * This, IUnknown * handler,                      \
                                                                    EventRegistrationToken * token);                   \
    HRESULT(STDMETHODCALLTYPE *remove_DefaultAudioRenderDeviceChanged)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IMediaDeviceStatics)

WRT_INSPECTABLE_THUNKS(IMediaDeviceStatics)

typedef struct IMediaDeviceFactory IMediaDeviceFactory;
#define IMediaDeviceFactory_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(T * This, IInspectable * *instance);
MIME_COM_INTERFACE(IMediaDeviceFactory)

WRT_INSPECTABLE_THUNKS(IMediaDeviceFactory)

/* the slots of mmdevapi's enumerator and device that are used here. */
typedef struct COLLECTION {
    const struct {
        void *QueryInterface;
        void *AddRef;
        ULONG(STDMETHODCALLTYPE *Release)(void *This);
        HRESULT(STDMETHODCALLTYPE *GetCount)(void *This, UINT *count);
        HRESULT(STDMETHODCALLTYPE *Item)(void *This, UINT index, void **device);
    } *lpVtbl;
} COLLECTION;

typedef struct ENUMERATOR {
    const struct {
        void *QueryInterface;
        void *AddRef;
        ULONG(STDMETHODCALLTYPE *Release)(void *This);
        HRESULT(STDMETHODCALLTYPE *EnumAudioEndpoints)(void *This, INT32 flow, DWORD mask, COLLECTION **devices);
        HRESULT(STDMETHODCALLTYPE *GetDefaultAudioEndpoint)(void *This, INT32 flow, INT32 role, void **device);
    } *lpVtbl;
} ENUMERATOR;

typedef struct DEVICE {
    const struct {
        void *QueryInterface;
        void *AddRef;
        ULONG(STDMETHODCALLTYPE *Release)(void *This);
        void *Activate;
        HRESULT(STDMETHODCALLTYPE *OpenPropertyStore)(void *This, DWORD access, IPropertyStore **store);
        HRESULT(STDMETHODCALLTYPE *GetId)(void *This, LPWSTR *id);
    } *lpVtbl;
} DEVICE;

typedef struct CLASS_FACTORY {
    const struct {
        void *QueryInterface;
        void *AddRef;
        ULONG(STDMETHODCALLTYPE *Release)(void *This);
        HRESULT(STDMETHODCALLTYPE *CreateInstance)(void *This, IUnknown *outer, REFIID riid, void **object);
    } *lpVtbl;
} CLASS_FACTORY;

typedef HRESULT(WINAPI *DLL_GET_CLASS_OBJECT)(REFCLSID clsid, REFIID riid, void **object);

/* through the class object, so a thread that has not initialized com gets the ids too. */
static ENUMERATOR *open_enumerator(void)
{
    HMODULE mmdevapi = LoadLibraryW(L"mmdevapi.dll");
    DLL_GET_CLASS_OBJECT get = mmdevapi ? (DLL_GET_CLASS_OBJECT)(void *)GetProcAddress(mmdevapi, "DllGetClassObject")
                                        : nullptr;
    CLASS_FACTORY *factory = nullptr;
    if (!get || FAILED(get(&CLSID_CbMMDeviceEnumerator, &IID_CbClassFactory, (void **)&factory)) || !factory)
        return nullptr;
    ENUMERATOR *enumerator = nullptr;
    if (FAILED(factory->lpVtbl->CreateInstance(factory, nullptr, &IID_CbMMDeviceEnumerator, (void **)&enumerator)))
        enumerator = nullptr;
    factory->lpVtbl->Release(factory);
    return enumerator;
}

static HRESULT make_string(LPCWSTR text, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    return WindowsCreateString(text, (UINT32)wcslen(text), value);
}

/* the device interface path windows gives an endpoint: its mmdevapi id and the interface class, in lower case. */
static void endpoint_path(INT32 flow, LPCWSTR id, WCHAR path[CB_AUDIO_PATH])
{
    LPCWSTR class = flow ? AUDIO_CAPTURE_CLASS : AUDIO_RENDER_CLASS;
    int n = _snwprintf(path, CB_AUDIO_PATH, L"\\\\?\\SWD#MMDEVAPI#%ls#%ls", id, class);
    if (n < 0 || n >= CB_AUDIO_PATH) {
        path[0] = 0;
        return;
    }
    for (WCHAR *c = path + 4; *c; c++)
        *c = towlower(*c);
    memcpy(path, L"\\\\?\\SWD#MMDEVAPI#", 17 * sizeof(WCHAR));
}

static void free_id(LPWSTR id)
{
    ((void(WINAPI *)(PVOID))(void *)CbOleProc("CoTaskMemFree"))(id);
}

static BOOLEAN default_mmdevice(ENUMERATOR *enumerator, INT32 flow, INT32 role, LPWSTR *id)
{
    DEVICE *device = nullptr;
    *id = nullptr;
    if (FAILED(enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, flow, role, (void **)&device)) || !device)
        return FALSE;
    if (FAILED(device->lpVtbl->GetId(device, id)))
        *id = nullptr;
    device->lpVtbl->Release(device);
    return *id != nullptr;
}

UINT32 CbAudioEndpoints(INT32 flow, CB_AUDIO_ENDPOINT *endpoints, UINT32 capacity)
{
    static const PROPERTYKEY friendly_name = {
        {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
    ENUMERATOR *enumerator = open_enumerator();
    if (!enumerator)
        return 0;
    LPWSTR default_id = nullptr;
    default_mmdevice(enumerator, flow, 0, &default_id);
    COLLECTION *devices = nullptr;
    UINT count = 0, found = 0;
    if (SUCCEEDED(enumerator->lpVtbl->EnumAudioEndpoints(enumerator, flow, 1 /* DEVICE_STATE_ACTIVE */, &devices)) &&
        devices && SUCCEEDED(devices->lpVtbl->GetCount(devices, &count))) {
        for (UINT i = 0; i < count && found < capacity; i++) {
            DEVICE *device = nullptr;
            LPWSTR id = nullptr;
            if (FAILED(devices->lpVtbl->Item(devices, i, (void **)&device)) || !device)
                continue;
            if (SUCCEEDED(device->lpVtbl->GetId(device, &id)) && id) {
                CB_AUDIO_ENDPOINT *e = &endpoints[found++];
                endpoint_path(flow, id, e->Path);
                e->IsDefault = default_id && !_wcsicmp(id, default_id);
                e->Name[0] = 0;
                IPropertyStore *store = nullptr;
                if (SUCCEEDED(device->lpVtbl->OpenPropertyStore(device, 0 /* STGM_READ */, &store)) && store) {
                    PROPVARIANT value;
                    memset(&value, 0, sizeof(value));
                    if (SUCCEEDED(store->lpVtbl->GetValue(store, &friendly_name, &value)) && value.vt == VT_LPWSTR &&
                        value.pwszVal)
                        lstrcpynW(e->Name, value.pwszVal, RTL_NUMBER_OF(e->Name));
                    ((HRESULT(WINAPI *)(PROPVARIANT *))(void *)CbOleProc("PropVariantClear"))(&value);
                    store->lpVtbl->Release(store);
                }
                free_id(id);
            }
            device->lpVtbl->Release(device);
        }
        devices->lpVtbl->Release(devices);
    }
    if (default_id)
        free_id(default_id);
    enumerator->lpVtbl->Release(enumerator);
    return found;
}

/* AudioDeviceRole Default is the console role and Communications the communications one. without the endpoint the id is empty. */
static HRESULT default_id(INT32 flow, INT32 role, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (role != 0 && role != 1)
        return E_INVALIDARG;
    ENUMERATOR *enumerator = open_enumerator();
    LPWSTR id = nullptr;
    if (enumerator) {
        default_mmdevice(enumerator, flow, role ? 2 : 0, &id);
        enumerator->lpVtbl->Release(enumerator);
    }
    if (!id)
        return S_OK;
    WCHAR path[CB_AUDIO_PATH];
    endpoint_path(flow, id, path);
    free_id(id);
    return path[0] ? make_string(path, value) : E_UNEXPECTED;
}

static HRESULT STDMETHODCALLTYPE capture_selector(IMediaDeviceStatics *This, HSTRING *value)
{
    return make_string(SELECTOR(AUDIO_CAPTURE_CLASS), value);
}

static HRESULT STDMETHODCALLTYPE render_selector(IMediaDeviceStatics *This, HSTRING *value)
{
    return make_string(SELECTOR(AUDIO_RENDER_CLASS), value);
}

static HRESULT STDMETHODCALLTYPE video_selector(IMediaDeviceStatics *This, HSTRING *value)
{
    return make_string(SELECTOR(VIDEO_CAPTURE_CLASS), value);
}

static HRESULT STDMETHODCALLTYPE default_capture(IMediaDeviceStatics *This, INT32 role, HSTRING *value)
{
    return default_id(1, role, value);
}

static HRESULT STDMETHODCALLTYPE default_render(IMediaDeviceStatics *This, INT32 role, HSTRING *value)
{
    return default_id(0, role, value);
}

/* the handlers are kept for their tokens. a change of the host's default device does not call them yet. */
typedef struct HANDLER {
    INT64 Token;
    IUnknown *Handler;
} HANDLER;

static SRWLOCK g_handler_lock = SRWLOCK_INIT;
static HANDLER g_handlers[64];
static INT64 g_next_token;

static HRESULT add_handler(IUnknown *handler, EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_POINTER;
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_handler_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_handlers); i++)
        if (!g_handlers[i].Handler) {
            handler->lpVtbl->AddRef(handler);
            g_handlers[i].Handler = handler;
            g_handlers[i].Token = ++g_next_token;
            token->value = g_next_token;
            hr = S_OK;
            break;
        }
    ReleaseSRWLockExclusive(&g_handler_lock);
    return hr;
}

static HRESULT remove_handler(EventRegistrationToken token)
{
    IUnknown *removed = nullptr;
    AcquireSRWLockExclusive(&g_handler_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_handlers); i++)
        if (g_handlers[i].Handler && g_handlers[i].Token == token.value) {
            removed = g_handlers[i].Handler;
            g_handlers[i].Handler = nullptr;
            break;
        }
    ReleaseSRWLockExclusive(&g_handler_lock);
    if (removed)
        removed->lpVtbl->Release(removed);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE add_capture_changed(IMediaDeviceStatics *This, IUnknown *handler,
                                                     EventRegistrationToken *token)
{
    return add_handler(handler, token);
}

static HRESULT STDMETHODCALLTYPE remove_capture_changed(IMediaDeviceStatics *This, EventRegistrationToken token)
{
    return remove_handler(token);
}

static HRESULT STDMETHODCALLTYPE add_render_changed(IMediaDeviceStatics *This, IUnknown *handler,
                                                    EventRegistrationToken *token)
{
    return add_handler(handler, token);
}

static HRESULT STDMETHODCALLTYPE remove_render_changed(IMediaDeviceStatics *This, EventRegistrationToken token)
{
    return remove_handler(token);
}

/* the class has statics and no instances. */
static HRESULT STDMETHODCALLTYPE activate(IMediaDeviceFactory *This, IInspectable **instance)
{
    *instance = nullptr;
    return E_NOTIMPL;
}

static const IMediaDeviceFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IMediaDeviceFactory), activate};
static const IMediaDeviceStaticsVtbl statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IMediaDeviceStatics), capture_selector, render_selector, video_selector, default_capture,
    default_render, add_capture_changed, remove_capture_changed, add_render_changed, remove_render_changed};

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS factory_class = {CbMediaDeviceClass, 2, {&IID_IActivationFactory, &IID_IMediaDeviceStatics}, 0,
                                        factory_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[2] = {&factory_vtbl, &statics_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbMediaDeviceFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
