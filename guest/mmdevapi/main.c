/*
 * the multimedia device api: an endpoint for each audio device of the host, named and formatted like it, the
 * default one being the host's. their audio clients, in client.c, play and record through the host.
 */
#include "mmdevapi_private.h"

#include <mime/ntapi.h>
#include <mime/pnp.h>

#include "../lib/audio/devices.h"

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

static void wave_format(WAVEFORMATEXTENSIBLE *f, UINT32 rate, UINT32 channels, DWORD mask, UINT32 bits, UINT32 valid_bits,
                        BOOL is_float)
{
    memset(f, 0, sizeof(*f));
    f->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f->Format.nChannels = (WORD)channels;
    f->Format.nSamplesPerSec = rate;
    f->Format.wBitsPerSample = (WORD)bits;
    f->Format.nBlockAlign = (WORD)(channels * bits / 8);
    f->Format.nAvgBytesPerSec = rate * f->Format.nBlockAlign;
    f->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f->Samples.wValidBitsPerSample = (WORD)valid_bits;
    f->dwChannelMask = mask;
    f->SubFormat = is_float ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
}

void MmMixFormat(WAVEFORMATEXTENSIBLE *f, UINT32 rate, UINT32 channels, DWORD mask)
{
    wave_format(f, rate, channels, mask, 32, 32, TRUE);
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

/*
 * one object per endpoint, acting as device, endpoint and property store. it lasts as long as the process, so a
 * device the host lost keeps the object programs may hold, and gets it back when the host has the device again.
 */
typedef struct DEVICE {
    const IMMDeviceVtbl *device;
    const IMMEndpointVtbl *endpoint;
    const IPropertyStoreVtbl *store;
    struct DEVICE *next;
    BOOL capture;
    BOOL present;
    BOOL announced; /* the notification clients were told of it. */
    GUID guid;
    UINT32 rate, channels, bits, valid_bits, form;
    DWORD channel_mask;
    BOOL is_float;
    WCHAR id[56];
    WCHAR name[112];
    WCHAR adapter[64];
    WCHAR port[48];
} DEVICE;

#define DEVICE_OF(field, This) ((DEVICE *)((BYTE *)(This) - offsetof(DEVICE, field)))

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
    {&FMTID_AudioEndpoint, 0},   /* PKEY_AudioEndpoint_FormFactor */
    {&FMTID_AudioEngine, 0},     /* PKEY_AudioEngine_DeviceFormat */
    {&FMTID_AudioEngine, 3},     /* PKEY_AudioEngine_OEMFormat */
    {&FMTID_AudioEndpoint, 3},   /* PKEY_AudioEndpoint_PhysicalSpeakers */
    {&FMTID_AudioEndpoint, 4},   /* PKEY_AudioEndpoint_GUID */
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

/*
 * windows names an endpoint "Speakers (Realtek(R) Audio)", from its description and its adapter. the host's own name
 * is kept whole as the friendly one, with the connector in use as description and the card as adapter.
 */
static HRESULT STDMETHODCALLTYPE store_get_value(void *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    DEVICE *d = DEVICE_OF(store, This);
    if (!key || !value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    const WCHAR *text = nullptr;
    if (IsEqualGUID(&key->fmtid, &FMTID_Device) && key->pid == 14)
        text = d->name;
    else if (IsEqualGUID(&key->fmtid, &FMTID_Device) && key->pid == 2)
        text = d->port;
    else if (IsEqualGUID(&key->fmtid, &FMTID_DeviceInterface) && key->pid == 2)
        text = d->adapter;
    if (text) {
        value->vt = VT_LPWSTR;
        return copy_string(text, &value->pwszVal);
    }
    if (IsEqualGUID(&key->fmtid, &FMTID_AudioEndpoint) && key->pid == 0) {
        value->vt = VT_UI4;
        value->ulVal = d->form;
        return S_OK;
    }
    if (IsEqualGUID(&key->fmtid, &FMTID_AudioEngine) && (key->pid == 0 || key->pid == 3)) {
        WAVEFORMATEXTENSIBLE *f = MmTaskAlloc(sizeof(*f));
        if (!f)
            return E_OUTOFMEMORY;
        wave_format(f, d->rate, d->channels, d->channel_mask, d->bits, d->valid_bits, d->is_float);
        value->vt = VT_BLOB;
        value->blob.cbSize = sizeof(*f);
        value->blob.pBlobData = (BYTE *)f;
        return S_OK;
    }
    if (IsEqualGUID(&key->fmtid, &FMTID_AudioEndpoint) && key->pid == 3) {
        value->vt = VT_UI4;
        value->ulVal = d->channel_mask;
        return S_OK;
    }
    if (IsEqualGUID(&key->fmtid, &FMTID_AudioEndpoint) && key->pid == 4) {
        value->vt = VT_LPWSTR;
        /* the id ends with the guid in braces. */
        return copy_string(wcsrchr(d->id, L'{'), &value->pwszVal);
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

/* ------------------------------------------------------------------ device */

static const IMMDeviceVtbl device_vtbl;
static const IMMEndpointVtbl endpoint_vtbl;

static DEVICE *g_devices[2];
static SRWLOCK g_devices_lock;

/*
 * reads the host's devices into the objects and returns the present ones, the default one first, in listed, which
 * holds MIME_AUDIO_ENDPOINTS of them.
 */
static UINT list_devices(BOOL capture, DEVICE **listed)
{
    MimeAudioEndpoint *all = HeapAlloc(GetProcessHeap(), 0, sizeof(*all) * MIME_AUDIO_ENDPOINTS);
    UINT count = all ? MimeAudioEndpoints(capture, all) : 0, found = 0;
    DEVICE **pool = &g_devices[capture ? 1 : 0];
    AcquireSRWLockExclusive(&g_devices_lock);
    for (DEVICE *d = *pool; d && all; d = d->next)
        d->present = FALSE;
    for (UINT i = 0; i < count; i++) {
        DEVICE *d = *pool;
        while (d && !IsEqualGUID(&d->guid, &all[i].guid))
            d = d->next;
        if (!d) {
            d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
            if (!d)
                continue;
            d->device = &device_vtbl;
            d->endpoint = &endpoint_vtbl;
            d->store = &store_vtbl;
            d->capture = capture;
            d->guid = all[i].guid;
            MimeAudioEndpointId(capture, &d->guid, d->id);
            d->next = *pool;
            *pool = d;
        }
        d->present = TRUE;
        d->rate = all[i].rate;
        d->channels = all[i].channels;
        d->bits = all[i].bits;
        d->valid_bits = all[i].valid_bits;
        d->channel_mask = all[i].channel_mask;
        d->form = all[i].form;
        d->is_float = all[i].is_float;
        lstrcpynW(d->name, all[i].name, RTL_NUMBER_OF(d->name));
        lstrcpynW(d->adapter, all[i].adapter, RTL_NUMBER_OF(d->adapter));
        lstrcpynW(d->port, all[i].port, RTL_NUMBER_OF(d->port));
        listed[found++] = d;
    }
    ReleaseSRWLockExclusive(&g_devices_lock);
    if (all)
        HeapFree(GetProcessHeap(), 0, all);
    return found;
}

/* by its id, or by the path of its device interface, which holds the id: \\?\SWD#MMDEVAPI#id#{class}. */
static DEVICE *find_device(LPCWSTR id)
{
    for (LPCWSTR at = id; *at; at++) {
        if (!wcsncmp(at, L"{0.0.", 5)) {
            id = at;
            break;
        }
    }
    if (wcslen(id) < 55)
        return nullptr;
    DEVICE *listed[MIME_AUDIO_ENDPOINTS];
    DEVICE *found = nullptr;
    for (int capture = 0; capture < 2 && !found; capture++) {
        list_devices(capture, listed);
        AcquireSRWLockShared(&g_devices_lock);
        for (DEVICE *d = g_devices[capture]; d && !found; d = d->next) {
            if (!_wcsnicmp(id, d->id, 55) && (!id[55] || id[55] == L'#'))
                found = d;
        }
        ReleaseSRWLockShared(&g_devices_lock);
    }
    return found;
}

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
        return MmCreateAudioClient(d->capture, &d->guid, d->rate, d->channels, d->channel_mask, object);
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
    *store = (IPropertyStore *)&d->store;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_id(void *This, LPWSTR *id)
{
    if (!id)
        return E_POINTER;
    return copy_string(DEVICE_OF(device, This)->id, id);
}

static HRESULT STDMETHODCALLTYPE device_get_state(void *This, DWORD *state)
{
    DEVICE *d = DEVICE_OF(device, This), *listed[MIME_AUDIO_ENDPOINTS];
    if (!state)
        return E_POINTER;
    list_devices(d->capture, listed);
    *state = d->present ? DEVICE_STATE_ACTIVE : DEVICE_STATE_NOTPRESENT;
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
    LONG refs;
    UINT count;
    DEVICE *devices[MIME_AUDIO_ENDPOINTS * 8];
} COLLECTION;

static HRESULT STDMETHODCALLTYPE collection_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMMDeviceCollection)) {
        *object = This;
        InterlockedIncrement(&((COLLECTION *)This)->refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE collection_add_ref(void *This)
{
    return (ULONG)InterlockedIncrement(&((COLLECTION *)This)->refs);
}

static ULONG STDMETHODCALLTYPE collection_release(void *This)
{
    LONG refs = InterlockedDecrement(&((COLLECTION *)This)->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, This);
    return (ULONG)refs;
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
    collection_query, collection_add_ref, collection_release, collection_count, collection_item,
};

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
    COLLECTION *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    *devices = (IMMDeviceCollection *)c;
    if (!c)
        return E_OUTOFMEMORY;
    c->vtbl = &collection_vtbl;
    c->refs = 1;
    for (int capture = 0; capture < 2; capture++) {
        DEVICE *listed[MIME_AUDIO_ENDPOINTS];
        if (flow == (capture ? eRender : eCapture))
            continue;
        UINT count = list_devices(capture, listed);
        if (mask & DEVICE_STATE_ACTIVE) {
            memcpy(c->devices + c->count, listed, count * sizeof(listed[0]));
            c->count += count;
        }
        if (mask & DEVICE_STATE_NOTPRESENT) {
            AcquireSRWLockShared(&g_devices_lock);
            for (DEVICE *d = g_devices[capture]; d && c->count < RTL_NUMBER_OF(c->devices) - MIME_AUDIO_ENDPOINTS; d = d->next) {
                if (!d->present)
                    c->devices[c->count++] = d;
            }
            ReleaseSRWLockShared(&g_devices_lock);
        }
    }
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
    DEVICE *listed[MIME_AUDIO_ENDPOINTS];
    if (!list_devices(flow == eCapture, listed))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    *device = (IMMDevice *)&listed[0]->device;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_get_device(void *This, LPCWSTR id, IMMDevice **device)
{
    if (!id || !device)
        return E_POINTER;
    DEVICE *d = find_device(id);
    *device = d ? (IMMDevice *)&d->device : nullptr;
    return d ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

/* ------------------------------------------------------------------ notifications */

#define MAX_CLIENTS 32

static IMMNotificationClient *g_clients[MAX_CLIENTS];
static SRWLOCK g_clients_lock;
static BOOL g_watching;

typedef struct CHANGE {
    int kind;
    DEVICE *device; /* none when a direction is left without a default device. */
    BOOL capture;
    DWORD state;
} CHANGE;

enum { CHANGE_ADDED, CHANGE_STATE, CHANGE_FORMAT, CHANGE_DEFAULT };

/*
 * windows takes no reference on a client. FL Studio's driver registers an object whose count starts at zero, so a
 * reference given back would free it. a client is called without the list lock, which a callback takes when it
 * registers or unregisters, and unregistering waits for the call under way.
 */
static SRWLOCK g_telling;
static DWORD g_teller;

static void tell(const CHANGE *c)
{
    static const PROPERTYKEY format = {{0xF19F064D, 0x082C, 0x4E27, {0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C}}, 0};
    LPCWSTR id = c->device ? c->device->id : nullptr;
    for (UINT i = 0; i < MAX_CLIENTS; i++) {
        AcquireSRWLockExclusive(&g_telling);
        AcquireSRWLockShared(&g_clients_lock);
        IMMNotificationClient *client = g_clients[i];
        ReleaseSRWLockShared(&g_clients_lock);
        g_teller = GetCurrentThreadId();
        if (client) {
            switch (c->kind) {
            case CHANGE_ADDED:
                client->lpVtbl->OnDeviceAdded(client, id);
                break;
            case CHANGE_STATE:
                client->lpVtbl->OnDeviceStateChanged(client, id, c->state);
                break;
            case CHANGE_FORMAT:
                client->lpVtbl->OnPropertyValueChanged(client, id, format);
                break;
            case CHANGE_DEFAULT:
                for (ERole role = eConsole; role <= eCommunications; role++)
                    client->lpVtbl->OnDefaultDeviceChanged(client, c->capture ? eCapture : eRender, role, id);
                break;
            }
        }
        g_teller = 0;
        ReleaseSRWLockExclusive(&g_telling);
    }
}

typedef struct SEEN {
    DEVICE *device;
    UINT32 rate, channels, bits, valid_bits;
    DWORD channel_mask;
} SEEN;

static BOOL seen_in(const SEEN *list, UINT count, const DEVICE *d, const SEEN **at)
{
    for (UINT i = 0; i < count; i++) {
        if (list[i].device == d) {
            if (at)
                *at = &list[i];
            return TRUE;
        }
    }
    return FALSE;
}

static UINT look(BOOL capture, SEEN *out)
{
    DEVICE *listed[MIME_AUDIO_ENDPOINTS];
    UINT count = list_devices(capture, listed);
    for (UINT i = 0; i < count; i++)
        out[i] = (SEEN){listed[i], listed[i]->rate, listed[i]->channels, listed[i]->bits, listed[i]->valid_bits,
                        listed[i]->channel_mask};
    return count;
}

/* the kernel wakes plug and play waiters when the host's audio devices change. */
static BOOL wait_change(ULONG *generation)
{
    MIME_PNP_REQUEST r;
    for (;;) {
        memset(&r, 0, sizeof(r));
        r.Flags = *generation;
        NTSTATUS status = NtPlugPlayControl(MIME_PNP_WAIT_CHANGE, &r, sizeof(r));
        if (status == STATUS_ALERTED || status == STATUS_USER_APC)
            continue;
        *generation = r.Flags;
        return NT_SUCCESS(status);
    }
}

/* the list is compared with the one before, the default device being the first of each. */
static DWORD WINAPI watch_devices(void *arg)
{
    static SEEN seen[2][MIME_AUDIO_ENDPOINTS], now[MIME_AUDIO_ENDPOINTS];
    UINT seen_count[2];
    ULONG generation = 0xFFFFFFFF;
    if (!wait_change(&generation))
        return 0;
    for (int capture = 0; capture < 2; capture++) {
        seen_count[capture] = look(capture, seen[capture]);
        for (UINT i = 0; i < seen_count[capture]; i++)
            seen[capture][i].device->announced = TRUE;
    }
    while (wait_change(&generation)) {
        for (int capture = 0; capture < 2; capture++) {
            SEEN *before = seen[capture];
            UINT before_count = seen_count[capture], count = look(capture, now);
            for (UINT i = 0; i < before_count; i++) {
                if (!seen_in(now, count, before[i].device, nullptr))
                    tell(&(CHANGE){CHANGE_STATE, before[i].device, capture, DEVICE_STATE_NOTPRESENT});
            }
            for (UINT i = 0; i < count; i++) {
                const SEEN *old;
                DEVICE *d = now[i].device;
                if (seen_in(before, before_count, d, &old)) {
                    if (old->rate != now[i].rate || old->channels != now[i].channels || old->bits != now[i].bits ||
                        old->valid_bits != now[i].valid_bits || old->channel_mask != now[i].channel_mask)
                        tell(&(CHANGE){CHANGE_FORMAT, d, capture, 0});
                    continue;
                }
                if (!d->announced)
                    tell(&(CHANGE){CHANGE_ADDED, d, capture, 0});
                d->announced = TRUE;
                tell(&(CHANGE){CHANGE_STATE, d, capture, DEVICE_STATE_ACTIVE});
            }
            DEVICE *was = before_count ? before[0].device : nullptr, *is = count ? now[0].device : nullptr;
            if (was != is)
                tell(&(CHANGE){CHANGE_DEFAULT, is, capture, 0});
            memcpy(before, now, count * sizeof(now[0]));
            seen_count[capture] = count;
        }
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE enum_register(void *This, IMMNotificationClient *client)
{
    if (!client)
        return E_POINTER;
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_clients_lock);
    for (UINT i = 0; i < MAX_CLIENTS && hr != S_OK; i++) {
        if (!g_clients[i]) {
            g_clients[i] = client;
            hr = S_OK;
        }
    }
    BOOL start = hr == S_OK && !g_watching;
    if (start)
        g_watching = TRUE;
    ReleaseSRWLockExclusive(&g_clients_lock);
    if (start) {
        HANDLE thread = CreateThread(nullptr, 0, watch_devices, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE enum_unregister(void *This, IMMNotificationClient *client)
{
    if (!client)
        return E_POINTER;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    AcquireSRWLockExclusive(&g_clients_lock);
    for (UINT i = 0; i < MAX_CLIENTS && hr != S_OK; i++) {
        if (g_clients[i] == client) {
            g_clients[i] = nullptr;
            hr = S_OK;
        }
    }
    ReleaseSRWLockExclusive(&g_clients_lock);
    if (hr == S_OK && g_teller != GetCurrentThreadId()) {
        AcquireSRWLockExclusive(&g_telling);
        ReleaseSRWLockExclusive(&g_telling);
    }
    return hr;
}

static const IMMDeviceEnumeratorVtbl enumerator_vtbl = {
    enum_query, static_ref, static_ref, enum_endpoints, enum_default, enum_get_device, enum_register, enum_unregister,
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
    if (reason == DLL_PROCESS_ATTACH)
        g_enumerator.vtbl = &enumerator_vtbl;
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
    BOOL by_class = !_wcsicmp(path, render) || !_wcsicmp(path, capture);
    DEVICE *listed[MIME_AUDIO_ENDPOINTS];
    DEVICE *d = !by_class ? find_device(path) : list_devices(!_wcsicmp(path, capture), listed) ? listed[0] : nullptr;
    BOOL client = IsEqualGUID(riid, &IID_IAudioClient) || IsEqualGUID(riid, &IID_IAudioClient2) || IsEqualGUID(riid, &IID_IAudioClient3);
    if (!d)
        op->Result = E_NOTIMPL;
    else if (by_class && client)
        op->Result = MmCreateAudioClient(d->capture, nullptr, d->rate, d->channels, d->channel_mask, (void **)&op->Interface);
    else
        op->Result = device_activate(&d->device, riid, CLSCTX_ALL, params, (void **)&op->Interface);
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
