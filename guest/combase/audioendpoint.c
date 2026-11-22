/*
 * the audio endpoints of mmdevapi as the device interfaces windows makes of them, and the changes mmdevapi tells. each
 * library that shows them compiles this file and so listens for itself, as the libraries of windows do: the one of
 * Windows.Media.Devices raises the events of MediaDevice, the one of Windows.Devices.Enumeration tells its watchers.
 */
#include "combase_private.h"
#include <mime/win32/propsys.h>

MIME_DEFINE_GUID(CLSID_CbMMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
MIME_DEFINE_GUID(IID_CbMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
MIME_DEFINE_GUID(IID_CbClassFactory, 0x00000001, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

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
        void *GetDevice;
        HRESULT(STDMETHODCALLTYPE *RegisterEndpointNotificationCallback)(void *This, void *client);
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

/* the device interface path windows gives an endpoint: its mmdevapi id and the interface class, in lower case. */
void CbAudioEndpointPath(INT32 flow, LPCWSTR id, WCHAR path[CB_AUDIO_PATH])
{
    LPCWSTR class = flow ? CB_AUDIO_CAPTURE_CLASS : CB_AUDIO_RENDER_CLASS;
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

/* the path of the default endpoint of a role of mmdevapi. false without one, and the path is empty when it does not fit. */
BOOLEAN CbAudioDefaultPath(INT32 flow, INT32 role, WCHAR path[CB_AUDIO_PATH])
{
    ENUMERATOR *enumerator = open_enumerator();
    LPWSTR id = nullptr;
    if (enumerator) {
        default_mmdevice(enumerator, flow, role, &id);
        enumerator->lpVtbl->Release(enumerator);
    }
    if (!id)
        return FALSE;
    CbAudioEndpointPath(flow, id, path);
    free_id(id);
    return TRUE;
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
                CbAudioEndpointPath(flow, id, e->Path);
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

/* ------------------------------------------------------------------ the changes mmdevapi tells */

typedef struct NOTIFICATION_CLIENT NOTIFICATION_CLIENT;
struct NOTIFICATION_CLIENT {
    const struct {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(NOTIFICATION_CLIENT *This, REFIID riid, void **object);
        ULONG(STDMETHODCALLTYPE *AddRef)(NOTIFICATION_CLIENT *This);
        ULONG(STDMETHODCALLTYPE *Release)(NOTIFICATION_CLIENT *This);
        HRESULT(STDMETHODCALLTYPE *OnDeviceStateChanged)(NOTIFICATION_CLIENT *This, LPCWSTR id, DWORD state);
        HRESULT(STDMETHODCALLTYPE *OnDeviceAdded)(NOTIFICATION_CLIENT *This, LPCWSTR id);
        HRESULT(STDMETHODCALLTYPE *OnDeviceRemoved)(NOTIFICATION_CLIENT *This, LPCWSTR id);
        HRESULT(STDMETHODCALLTYPE *OnDefaultDeviceChanged)(NOTIFICATION_CLIENT *This, INT32 flow, INT32 role,
                                                           LPCWSTR id);
        HRESULT(STDMETHODCALLTYPE *OnPropertyValueChanged)(NOTIFICATION_CLIENT *This, LPCWSTR id,
                                                           const PROPERTYKEY key);
    } *lpVtbl;
};

MIME_DEFINE_GUID(IID_CbMMNotificationClient, 0x7991EEC9, 0x7E89, 0x4D85, 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0);

static HRESULT STDMETHODCALLTYPE client_query(NOTIFICATION_CLIENT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_CbMMNotificationClient) ? This : nullptr;
    return *object ? S_OK : E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE client_ref(NOTIFICATION_CLIENT *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE client_state(NOTIFICATION_CLIENT *This, LPCWSTR id, DWORD state)
{
    CbDeviceWatchersRefresh();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_listed(NOTIFICATION_CLIENT *This, LPCWSTR id)
{
    return S_OK;
}

/* the multimedia role has no AudioDeviceRole. */
static HRESULT STDMETHODCALLTYPE client_default(NOTIFICATION_CLIENT *This, INT32 flow, INT32 role, LPCWSTR id)
{
    if ((flow == 0 || flow == 1) && role != 1)
        CbAudioDefaultChanged(flow, role ? 1 : 0, id);
    if (role == 0)
        CbDeviceWatchersRefresh();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_property(NOTIFICATION_CLIENT *This, LPCWSTR id, const PROPERTYKEY key)
{
    return S_OK;
}

static NOTIFICATION_CLIENT g_client = {&(const typeof(*g_client.lpVtbl)){
    client_query, client_ref, client_ref, client_state, client_listed, client_listed, client_default,
    client_property}};
static INIT_ONCE g_watch_once = INIT_ONCE_STATIC_INIT;

/* the enumerator is kept: mmdevapi tells its clients for the life of the process. */
static BOOL CALLBACK watch(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    ENUMERATOR *enumerator = open_enumerator();
    if (enumerator)
        enumerator->lpVtbl->RegisterEndpointNotificationCallback(enumerator, &g_client);
    return TRUE;
}

void CbAudioWatch(void)
{
    InitOnceExecuteOnce(&g_watch_once, watch, nullptr, nullptr);
}
