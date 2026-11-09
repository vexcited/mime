/*
 * Windows.Devices.Enumeration.DeviceInformation: the device interfaces of the audio endpoints, found at once or by a
 * watcher. other device classes have no interfaces here, so their watchers complete without devices.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>

const WCHAR CbDeviceInformationClass[] = L"Windows.Devices.Enumeration.DeviceInformation";
static const WCHAR watcher_class_name[] = L"Windows.Devices.Enumeration.DeviceWatcher";

MIME_DEFINE_GUID(IID_IDeviceInformationStatics, 0xC17F100E, 0x3A46, 0x4A78, 0x80, 0x13, 0x76, 0x9D, 0xC9, 0xB9, 0x73, 0x90);
MIME_DEFINE_GUID(IID_IDeviceInformationStatics2, 0x493B4F34, 0xA84F, 0x45FD, 0x91, 0x67, 0x15, 0xD1, 0xCB, 0x1B, 0xD1, 0xF9);
MIME_DEFINE_GUID(IID_IDeviceInformation, 0xABA0FB95, 0x4398, 0x489D, 0x8E, 0x44, 0xE6, 0x13, 0x09, 0x27, 0x01, 0x1F);
MIME_DEFINE_GUID(IID_IDeviceWatcher, 0xC9EAB97D, 0x8F6B, 0x4F96, 0xA9, 0xF4, 0xAB, 0xC8, 0x14, 0xE2, 0x22, 0x71);
MIME_DEFINE_GUID(IID_IMapView_HSTRING_IInspectable, 0xBB78502A, 0xF79D, 0x54FA, 0x92, 0xC9, 0x90, 0xC5, 0x03, 0x9F, 0xDF, 0x7E);
MIME_DEFINE_GUID(IID_IIterable_KeyValuePair_HSTRING_IInspectable, 0xFE2F3D47, 0x5D47, 0x5499, 0x83, 0x74, 0x43, 0x0C, 0x7C,
                 0xDA, 0x02, 0x04);
MIME_DEFINE_GUID(IID_IIterator_KeyValuePair_HSTRING_IInspectable, 0x5DB5FA32, 0x707C, 0x5849, 0xA0, 0x6B, 0x91, 0xC8, 0xEB,
                 0x9D, 0x10, 0xE8);

enum {
    DEVICE_CLASS_ALL,
    DEVICE_CLASS_AUDIO_CAPTURE,
    DEVICE_CLASS_AUDIO_RENDER,
    DEVICE_CLASS_PORTABLE_STORAGE,
    DEVICE_CLASS_VIDEO_CAPTURE,
};

enum {
    STATUS_CREATED,
    STATUS_STARTED,
    STATUS_ENUMERATION_COMPLETED,
    STATUS_STOPPING,
    STATUS_STOPPED,
    STATUS_ABORTED,
};

/* the flows a device class or a selector covers, bit 0 render and bit 1 capture. */
#define FLOW_RENDER 1
#define FLOW_CAPTURE 2

static ULONG flows_of_class(INT32 device_class)
{
    switch (device_class) {
    case DEVICE_CLASS_ALL:
        return FLOW_RENDER | FLOW_CAPTURE;
    case DEVICE_CLASS_AUDIO_CAPTURE:
        return FLOW_CAPTURE;
    case DEVICE_CLASS_AUDIO_RENDER:
        return FLOW_RENDER;
    default:
        return 0;
    }
}

/* a selector names its interface class guid, which is all looked at. */
static ULONG flows_of_filter(HSTRING filter)
{
    UINT32 length = 0;
    LPCWSTR text = filter ? WindowsGetStringRawBuffer(filter, &length) : nullptr;
    if (!text || !length)
        return FLOW_RENDER | FLOW_CAPTURE;
    ULONG flows = 0;
    for (UINT32 i = 0; i + 36 <= length; i++) {
        if (!_wcsnicmp(text + i, L"E6327CAD-DCEC-4949-AE8A-991E976A79D2", 36))
            flows |= FLOW_RENDER;
        if (!_wcsnicmp(text + i, L"2EEF81BE-33FA-4800-9670-1CD474972C3F", 36))
            flows |= FLOW_CAPTURE;
    }
    return flows;
}

/* ------------------------------------------------------------------ the empty property map */

typedef struct IPropertyMapView IPropertyMapView;
#define IPropertyMapView_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Lookup)(T * This, HSTRING key, IInspectable * *value);                                  \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, UINT32 * size);                                                     \
    HRESULT(STDMETHODCALLTYPE *HasKey)(T * This, HSTRING key, BOOLEAN * found);                                        \
    HRESULT(STDMETHODCALLTYPE *Split)(T * This, IPropertyMapView * *first, IPropertyMapView * *second);
MIME_COM_INTERFACE(IPropertyMapView)

WRT_INSPECTABLE_THUNKS(IPropertyMapView)

typedef struct IPropertyIterable IPropertyIterable;
#define IPropertyIterable_METHODS(T)                                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *First)(T * This, IInspectable * *iterator);
MIME_COM_INTERFACE(IPropertyIterable)

WRT_INSPECTABLE_THUNKS(IPropertyIterable)

typedef struct IPropertyIterator IPropertyIterator;
#define IPropertyIterator_METHODS(T)                                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Current)(T * This, IInspectable * *current);                                        \
    HRESULT(STDMETHODCALLTYPE *get_HasCurrent)(T * This, BOOLEAN * has);                                               \
    HRESULT(STDMETHODCALLTYPE *MoveNext)(T * This, BOOLEAN * has);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetMany)(T * This, UINT32 capacity, IInspectable * *items, UINT32 * count);
MIME_COM_INTERFACE(IPropertyIterator)

WRT_INSPECTABLE_THUNKS(IPropertyIterator)

static HRESULT STDMETHODCALLTYPE map_lookup(IPropertyMapView *This, HSTRING key, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return E_BOUNDS;
}

static HRESULT STDMETHODCALLTYPE map_size(IPropertyMapView *This, UINT32 *size)
{
    if (!size)
        return E_POINTER;
    *size = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE map_has_key(IPropertyMapView *This, HSTRING key, BOOLEAN *found)
{
    if (!found)
        return E_POINTER;
    *found = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE map_split(IPropertyMapView *This, IPropertyMapView **first, IPropertyMapView **second)
{
    if (!first || !second)
        return E_POINTER;
    *first = *second = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE iterator_current(IPropertyIterator *This, IInspectable **current)
{
    if (!current)
        return E_POINTER;
    *current = nullptr;
    return E_BOUNDS;
}

static HRESULT STDMETHODCALLTYPE iterator_has_current(IPropertyIterator *This, BOOLEAN *has)
{
    if (!has)
        return E_POINTER;
    *has = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE iterator_move_next(IPropertyIterator *This, BOOLEAN *has)
{
    if (!has)
        return E_POINTER;
    *has = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE iterator_get_many(IPropertyIterator *This, UINT32 capacity, IInspectable **items,
                                                   UINT32 *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    return S_OK;
}

static const IPropertyIteratorVtbl iterator_vtbl = {WRT_INSPECTABLE_ENTRIES(IPropertyIterator), iterator_current,
                                                    iterator_has_current, iterator_move_next, iterator_get_many};

static void static_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS iterator_class = {L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<String, Object>>",
                                         1, {&IID_IIterator_KeyValuePair_HSTRING_IInspectable}, 0, static_destroy};

static WRT_OBJECT g_iterator, g_map;
static INIT_ONCE g_map_once = INIT_ONCE_STATIC_INIT;

static HRESULT STDMETHODCALLTYPE map_first(IPropertyIterable *This, IInspectable **iterator)
{
    if (!iterator)
        return E_POINTER;
    return WrtQuery(&g_iterator.Ifaces[0], &IID_IIterator_KeyValuePair_HSTRING_IInspectable, (void **)iterator);
}

static const IPropertyMapViewVtbl map_vtbl = {WRT_INSPECTABLE_ENTRIES(IPropertyMapView), map_lookup, map_size,
                                              map_has_key, map_split};
static const IPropertyIterableVtbl iterable_vtbl = {WRT_INSPECTABLE_ENTRIES(IPropertyIterable), map_first};

static const WRT_CLASS map_class = {L"Windows.Foundation.Collections.IMapView`2<String, Object>", 2,
                                    {&IID_IMapView_HSTRING_IInspectable, &IID_IIterable_KeyValuePair_HSTRING_IInspectable},
                                    0, static_destroy};

static BOOL CALLBACK make_map(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *iterator_vtables[1] = {&iterator_vtbl};
    WrtInit(&g_iterator, &iterator_class, iterator_vtables);
    const void *map_vtables[2] = {&map_vtbl, &iterable_vtbl};
    WrtInit(&g_map, &map_class, map_vtables);
    return TRUE;
}

/* ------------------------------------------------------------------ a device */

typedef struct IDeviceInformation IDeviceInformation;
#define IDeviceInformation_METHODS(T)                                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Id)(T * This, HSTRING * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, HSTRING * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_IsEnabled)(T * This, BOOLEAN * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_IsDefault)(T * This, BOOLEAN * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_EnclosureLocation)(T * This, IInspectable * *value);                                \
    HRESULT(STDMETHODCALLTYPE *get_Properties)(T * This, IInspectable * *value);                                       \
    HRESULT(STDMETHODCALLTYPE *Update)(T * This, IInspectable * update);                                               \
    HRESULT(STDMETHODCALLTYPE *GetThumbnailAsync)(T * This, IInspectable * *operation);                                \
    HRESULT(STDMETHODCALLTYPE *GetGlyphThumbnailAsync)(T * This, IInspectable * *operation);
MIME_COM_INTERFACE(IDeviceInformation)

WRT_INSPECTABLE_THUNKS(IDeviceInformation)

typedef struct DEVICE_INFORMATION {
    WRT_OBJECT Base;
    CB_AUDIO_ENDPOINT Endpoint;
} DEVICE_INFORMATION;

#define DEVICE_OF(This) ((DEVICE_INFORMATION *)((WRT_IFACE *)(This))->Owner)

static HRESULT copy_string(LPCWSTR text, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    return WindowsCreateString(text, (UINT32)wcslen(text), value);
}

static HRESULT STDMETHODCALLTYPE device_id(IDeviceInformation *This, HSTRING *value)
{
    return copy_string(DEVICE_OF(This)->Endpoint.Path, value);
}

static HRESULT STDMETHODCALLTYPE device_name(IDeviceInformation *This, HSTRING *value)
{
    return copy_string(DEVICE_OF(This)->Endpoint.Name, value);
}

static HRESULT STDMETHODCALLTYPE device_enabled(IDeviceInformation *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_default(IDeviceInformation *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = DEVICE_OF(This)->Endpoint.IsDefault;
    return S_OK;
}

/* an endpoint has no enclosure, which windows gives as null. */
static HRESULT STDMETHODCALLTYPE device_enclosure(IDeviceInformation *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return S_OK;
}

/* no additional property was asked for, so the map is empty. */
static HRESULT STDMETHODCALLTYPE device_properties(IDeviceInformation *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    InitOnceExecuteOnce(&g_map_once, make_map, nullptr, nullptr);
    return WrtQuery(&g_map.Ifaces[0], &IID_IMapView_HSTRING_IInspectable, (void **)value);
}

static HRESULT STDMETHODCALLTYPE device_update(IDeviceInformation *This, IInspectable *update)
{
    return update ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE device_thumbnail(IDeviceInformation *This, IInspectable **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static const IDeviceInformationVtbl device_vtbl = {WRT_INSPECTABLE_ENTRIES(IDeviceInformation), device_id, device_name,
                                                   device_enabled, device_default, device_enclosure, device_properties,
                                                   device_update, device_thumbnail, device_thumbnail};

static void device_destroy(WRT_OBJECT *object)
{
    CbFree(object);
}

static const WRT_CLASS device_class = {CbDeviceInformationClass, 1, {&IID_IDeviceInformation}, 0, device_destroy};

static IDeviceInformation *new_device(const CB_AUDIO_ENDPOINT *endpoint)
{
    DEVICE_INFORMATION *device = CbAlloc(sizeof(*device));
    if (!device)
        return nullptr;
    const void *vtables[1] = {&device_vtbl};
    WrtInit(&device->Base, &device_class, vtables);
    device->Endpoint = *endpoint;
    return (IDeviceInformation *)&device->Base.Ifaces[0];
}

/* ------------------------------------------------------------------ the watcher */

typedef struct IDeviceWatcher IDeviceWatcher;
#define IDeviceWatcher_METHODS(T)                                                                                      \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *add_Added)(T * This, IUnknown * handler, EventRegistrationToken * token);               \
    HRESULT(STDMETHODCALLTYPE *remove_Added)(T * This, EventRegistrationToken token);                                  \
    HRESULT(STDMETHODCALLTYPE *add_Updated)(T * This, IUnknown * handler, EventRegistrationToken * token);             \
    HRESULT(STDMETHODCALLTYPE *remove_Updated)(T * This, EventRegistrationToken token);                                \
    HRESULT(STDMETHODCALLTYPE *add_Removed)(T * This, IUnknown * handler, EventRegistrationToken * token);             \
    HRESULT(STDMETHODCALLTYPE *remove_Removed)(T * This, EventRegistrationToken token);                                \
    HRESULT(STDMETHODCALLTYPE *add_EnumerationCompleted)(T * This, IUnknown * handler, EventRegistrationToken * token); \
    HRESULT(STDMETHODCALLTYPE *remove_EnumerationCompleted)(T * This, EventRegistrationToken token);                   \
    HRESULT(STDMETHODCALLTYPE *add_Stopped)(T * This, IUnknown * handler, EventRegistrationToken * token);             \
    HRESULT(STDMETHODCALLTYPE *remove_Stopped)(T * This, EventRegistrationToken token);                                \
    HRESULT(STDMETHODCALLTYPE *get_Status)(T * This, INT32 * status);                                                  \
    HRESULT(STDMETHODCALLTYPE *Start)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);
MIME_COM_INTERFACE(IDeviceWatcher)

WRT_INSPECTABLE_THUNKS(IDeviceWatcher)

/* a TypedEventHandler of the watcher, whichever its arguments. */
typedef struct EVENT_HANDLER {
    const struct {
        void *QueryInterface;
        ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG(STDMETHODCALLTYPE *Release)(void *This);
        HRESULT(STDMETHODCALLTYPE *Invoke)(void *This, IInspectable *sender, IInspectable *args);
    } *lpVtbl;
} EVENT_HANDLER;

enum { EVENT_ADDED, EVENT_UPDATED, EVENT_REMOVED, EVENT_COMPLETED, EVENT_STOPPED, EVENT_KINDS };

#define WATCHER_HANDLERS 16

typedef struct WATCHER {
    WRT_OBJECT Base;
    SRWLOCK Lock;
    ULONG Flows;
    volatile LONG Status;
    struct {
        INT64 Token;
        EVENT_HANDLER *Handler;
    } Handlers[EVENT_KINDS][WATCHER_HANDLERS];
} WATCHER;

#define WATCHER_OF(This) ((WATCHER *)((WRT_IFACE *)(This))->Owner)

static volatile LONG64 g_next_token;

static HRESULT watcher_add(IDeviceWatcher *This, ULONG kind, IUnknown *handler, EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_INVALIDARG;
    WATCHER *w = WATCHER_OF(This);
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&w->Lock);
    for (ULONG i = 0; i < WATCHER_HANDLERS; i++)
        if (!w->Handlers[kind][i].Handler) {
            handler->lpVtbl->AddRef(handler);
            w->Handlers[kind][i].Handler = (EVENT_HANDLER *)handler;
            w->Handlers[kind][i].Token = InterlockedIncrement64(&g_next_token);
            token->value = w->Handlers[kind][i].Token;
            hr = S_OK;
            break;
        }
    ReleaseSRWLockExclusive(&w->Lock);
    return hr;
}

static HRESULT watcher_remove(IDeviceWatcher *This, ULONG kind, EventRegistrationToken token)
{
    WATCHER *w = WATCHER_OF(This);
    EVENT_HANDLER *removed = nullptr;
    AcquireSRWLockExclusive(&w->Lock);
    for (ULONG i = 0; i < WATCHER_HANDLERS; i++)
        if (w->Handlers[kind][i].Handler && w->Handlers[kind][i].Token == token.value) {
            removed = w->Handlers[kind][i].Handler;
            w->Handlers[kind][i].Handler = nullptr;
            break;
        }
    ReleaseSRWLockExclusive(&w->Lock);
    if (removed)
        removed->lpVtbl->Release(removed);
    return S_OK;
}

/* the handlers are called outside the lock, so one may remove itself or stop the watcher. */
static void watcher_raise(WATCHER *w, ULONG kind, IInspectable *args)
{
    EVENT_HANDLER *handlers[WATCHER_HANDLERS];
    ULONG count = 0;
    AcquireSRWLockShared(&w->Lock);
    for (ULONG i = 0; i < WATCHER_HANDLERS; i++)
        if (w->Handlers[kind][i].Handler) {
            handlers[count] = w->Handlers[kind][i].Handler;
            handlers[count]->lpVtbl->AddRef(handlers[count]);
            count++;
        }
    ReleaseSRWLockShared(&w->Lock);
    IInspectable *sender = (IInspectable *)&w->Base.Ifaces[0];
    for (ULONG i = 0; i < count; i++) {
        handlers[i]->lpVtbl->Invoke(handlers[i], sender, args);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
}

#define WATCHER_EVENT(name, kind)                                                                                      \
    static HRESULT STDMETHODCALLTYPE add_##name(IDeviceWatcher *This, IUnknown *handler, EventRegistrationToken *token) \
    {                                                                                                                  \
        return watcher_add(This, kind, handler, token);                                                                \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE remove_##name(IDeviceWatcher *This, EventRegistrationToken token)                 \
    {                                                                                                                  \
        return watcher_remove(This, kind, token);                                                                      \
    }

WATCHER_EVENT(added, EVENT_ADDED)
WATCHER_EVENT(updated, EVENT_UPDATED)
WATCHER_EVENT(removed, EVENT_REMOVED)
WATCHER_EVENT(completed, EVENT_COMPLETED)
WATCHER_EVENT(stopped, EVENT_STOPPED)

static HRESULT STDMETHODCALLTYPE watcher_status(IDeviceWatcher *This, INT32 *status)
{
    if (!status)
        return E_POINTER;
    *status = WATCHER_OF(This)->Status;
    return S_OK;
}

/* a started watcher reports what is there, then that enumeration is complete, from a pool thread as on windows. */
static DWORD WINAPI watcher_enumerate(PVOID context)
{
    WATCHER *w = context;
    for (INT32 flow = 0; flow < 2; flow++) {
        if (!(w->Flows & (flow ? FLOW_CAPTURE : FLOW_RENDER)))
            continue;
        CB_AUDIO_ENDPOINT endpoints[8];
        UINT32 count = CbAudioEndpoints(flow, endpoints, RTL_NUMBER_OF(endpoints));
        for (UINT32 i = 0; i < count && w->Status == STATUS_STARTED; i++) {
            IDeviceInformation *device = new_device(&endpoints[i]);
            if (!device)
                continue;
            watcher_raise(w, EVENT_ADDED, (IInspectable *)device);
            device->lpVtbl->Release(device);
        }
    }
    if (InterlockedCompareExchange(&w->Status, STATUS_ENUMERATION_COMPLETED, STATUS_STARTED) == STATUS_STARTED)
        watcher_raise(w, EVENT_COMPLETED, nullptr);
    if (InterlockedCompareExchange(&w->Status, STATUS_STOPPED, STATUS_STOPPING) == STATUS_STOPPING)
        watcher_raise(w, EVENT_STOPPED, nullptr);
    WrtRelease(&w->Base.Ifaces[0]);
    return 0;
}

static DWORD WINAPI watcher_finish_stop(PVOID context)
{
    WATCHER *w = context;
    if (InterlockedCompareExchange(&w->Status, STATUS_STOPPED, STATUS_STOPPING) == STATUS_STOPPING)
        watcher_raise(w, EVENT_STOPPED, nullptr);
    WrtRelease(&w->Base.Ifaces[0]);
    return 0;
}

static HRESULT STDMETHODCALLTYPE watcher_start(IDeviceWatcher *This)
{
    WATCHER *w = WATCHER_OF(This);
    LONG status = w->Status;
    if ((status != STATUS_CREATED && status != STATUS_STOPPED && status != STATUS_ABORTED) ||
        InterlockedCompareExchange(&w->Status, STATUS_STARTED, status) != status)
        return E_ILLEGAL_METHOD_CALL;
    WrtAddRef(&w->Base.Ifaces[0]);
    if (!QueueUserWorkItem(watcher_enumerate, w, 0)) {
        w->Status = STATUS_ABORTED;
        WrtRelease(&w->Base.Ifaces[0]);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

/* the enumeration pass that is still running sends Stopped itself when it sees the stop. */
static HRESULT STDMETHODCALLTYPE watcher_stop(IDeviceWatcher *This)
{
    WATCHER *w = WATCHER_OF(This);
    if (InterlockedCompareExchange(&w->Status, STATUS_STOPPING, STATUS_ENUMERATION_COMPLETED) ==
        STATUS_ENUMERATION_COMPLETED) {
        WrtAddRef(&w->Base.Ifaces[0]);
        if (!QueueUserWorkItem(watcher_finish_stop, w, 0))
            watcher_finish_stop(w);
        return S_OK;
    }
    if (InterlockedCompareExchange(&w->Status, STATUS_STOPPING, STATUS_STARTED) == STATUS_STARTED)
        return S_OK;
    return E_ILLEGAL_METHOD_CALL;
}

static const IDeviceWatcherVtbl watcher_vtbl = {WRT_INSPECTABLE_ENTRIES(IDeviceWatcher),
                                                add_added,
                                                remove_added,
                                                add_updated,
                                                remove_updated,
                                                add_removed,
                                                remove_removed,
                                                add_completed,
                                                remove_completed,
                                                add_stopped,
                                                remove_stopped,
                                                watcher_status,
                                                watcher_start,
                                                watcher_stop};

static void watcher_destroy(WRT_OBJECT *object)
{
    WATCHER *w = (WATCHER *)object;
    for (ULONG kind = 0; kind < EVENT_KINDS; kind++)
        for (ULONG i = 0; i < WATCHER_HANDLERS; i++)
            if (w->Handlers[kind][i].Handler)
                w->Handlers[kind][i].Handler->lpVtbl->Release(w->Handlers[kind][i].Handler);
    CbFree(w);
}

static const WRT_CLASS watcher_class = {watcher_class_name, 1, {&IID_IDeviceWatcher}, 0, watcher_destroy};

static HRESULT new_watcher(ULONG flows, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WATCHER *w = CbAlloc(sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    memset(w, 0, sizeof(*w));
    const void *vtables[1] = {&watcher_vtbl};
    WrtInit(&w->Base, &watcher_class, vtables);
    InitializeSRWLock(&w->Lock);
    w->Flows = flows;
    w->Status = STATUS_CREATED;
    *out = &w->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the statics */

typedef struct IDeviceInformationStatics IDeviceInformationStatics;
#define IDeviceInformationStatics_METHODS(T)                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateFromIdAsync)(T * This, HSTRING id, void **op);                                    \
    HRESULT(STDMETHODCALLTYPE *CreateFromIdAsyncAdditionalProperties)(T * This, HSTRING id, void *props, void **op);   \
    HRESULT(STDMETHODCALLTYPE *FindAllAsync)(T * This, void **op);                                                     \
    HRESULT(STDMETHODCALLTYPE *FindAllAsyncDeviceClass)(T * This, INT32 device_class, void **op);                      \
    HRESULT(STDMETHODCALLTYPE *FindAllAsyncAqsFilter)(T * This, HSTRING filter, void **op);                            \
    HRESULT(STDMETHODCALLTYPE *FindAllAsyncAqsFilterAndAdditionalProperties)(T * This, HSTRING filter, void *props,     \
                                                                             void **op);                               \
    HRESULT(STDMETHODCALLTYPE *CreateWatcher)(T * This, void **watcher);                                               \
    HRESULT(STDMETHODCALLTYPE *CreateWatcherDeviceClass)(T * This, INT32 device_class, void **watcher);                \
    HRESULT(STDMETHODCALLTYPE *CreateWatcherAqsFilter)(T * This, HSTRING filter, void **watcher);                      \
    HRESULT(STDMETHODCALLTYPE *CreateWatcherAqsFilterAndAdditionalProperties)(T * This, HSTRING filter, void *props,    \
                                                                              void **watcher);
MIME_COM_INTERFACE(IDeviceInformationStatics)

WRT_INSPECTABLE_THUNKS(IDeviceInformationStatics)

typedef struct IDeviceInformationStatics2 IDeviceInformationStatics2;
#define IDeviceInformationStatics2_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetAqsFilterFromDeviceClass)(T * This, INT32 device_class, HSTRING * filter);           \
    HRESULT(STDMETHODCALLTYPE *CreateFromIdAsyncWithKind)(T * This, HSTRING id, void *props, INT32 kind, void **op);   \
    HRESULT(STDMETHODCALLTYPE *FindAllAsyncWithKind)(T * This, HSTRING filter, void *props, INT32 kind, void **op);    \
    HRESULT(STDMETHODCALLTYPE *CreateWatcherWithKind)(T * This, HSTRING filter, void *props, INT32 kind,               \
                                                      void **watcher);
MIME_COM_INTERFACE(IDeviceInformationStatics2)

WRT_INSPECTABLE_THUNKS(IDeviceInformationStatics2)

typedef struct IDeviceInformationFactory IDeviceInformationFactory;
#define IDeviceInformationFactory_METHODS(T)                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(T * This, IInspectable * *instance);
MIME_COM_INTERFACE(IDeviceInformationFactory)

WRT_INSPECTABLE_THUNKS(IDeviceInformationFactory)

/* the one shot lookups hand out async operations, which are not written yet. */
static HRESULT not_written(void **op)
{
    if (op)
        *op = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE from_id(IDeviceInformationStatics *This, HSTRING id, void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE from_id_props(IDeviceInformationStatics *This, HSTRING id, void *props, void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE find_all(IDeviceInformationStatics *This, void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE find_class(IDeviceInformationStatics *This, INT32 device_class, void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE find_aqs(IDeviceInformationStatics *This, HSTRING filter, void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE find_aqs_props(IDeviceInformationStatics *This, HSTRING filter, void *props, void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE watcher_all(IDeviceInformationStatics *This, void **out)
{
    return new_watcher(FLOW_RENDER | FLOW_CAPTURE, out);
}

static HRESULT STDMETHODCALLTYPE watcher_class_of(IDeviceInformationStatics *This, INT32 device_class, void **out)
{
    return new_watcher(flows_of_class(device_class), out);
}

static HRESULT STDMETHODCALLTYPE watcher_aqs(IDeviceInformationStatics *This, HSTRING filter, void **out)
{
    return new_watcher(flows_of_filter(filter), out);
}

static HRESULT STDMETHODCALLTYPE watcher_aqs_props(IDeviceInformationStatics *This, HSTRING filter, void *props,
                                                   void **out)
{
    return new_watcher(flows_of_filter(filter), out);
}

static HRESULT STDMETHODCALLTYPE aqs_from_class(IDeviceInformationStatics2 *This, INT32 device_class, HSTRING *filter)
{
    static const WCHAR *const filters[] = {
        L"",
        L"System.Devices.InterfaceClassGuid:=\"{2EEF81BE-33FA-4800-9670-1CD474972C3F}\" AND "
        L"System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True",
        L"System.Devices.InterfaceClassGuid:=\"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}\" AND "
        L"System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True",
    };
    if (!filter)
        return E_POINTER;
    *filter = nullptr;
    if (device_class < 0 || device_class >= (INT32)RTL_NUMBER_OF(filters))
        return E_NOTIMPL;
    return copy_string(filters[device_class], filter);
}

static HRESULT STDMETHODCALLTYPE from_id_kind(IDeviceInformationStatics2 *This, HSTRING id, void *props, INT32 kind,
                                              void **op)
{
    return not_written(op);
}

static HRESULT STDMETHODCALLTYPE find_kind(IDeviceInformationStatics2 *This, HSTRING filter, void *props, INT32 kind,
                                           void **op)
{
    return not_written(op);
}

/* audio endpoints are device interfaces, kind 1; other kinds have none here. */
static HRESULT STDMETHODCALLTYPE watcher_kind(IDeviceInformationStatics2 *This, HSTRING filter, void *props, INT32 kind,
                                              void **out)
{
    return new_watcher(kind == 1 ? flows_of_filter(filter) : 0, out);
}

static HRESULT STDMETHODCALLTYPE activate(IDeviceInformationFactory *This, IInspectable **instance)
{
    *instance = nullptr;
    return E_NOTIMPL;
}

static const IDeviceInformationFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IDeviceInformationFactory), activate};
static const IDeviceInformationStaticsVtbl statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDeviceInformationStatics), from_id, from_id_props, find_all, find_class, find_aqs,
    find_aqs_props, watcher_all, watcher_class_of, watcher_aqs, watcher_aqs_props};
static const IDeviceInformationStatics2Vtbl statics2_vtbl = {WRT_INSPECTABLE_ENTRIES(IDeviceInformationStatics2),
                                                             aqs_from_class, from_id_kind, find_kind, watcher_kind};

static const WRT_CLASS factory_class = {
    CbDeviceInformationClass, 3, {&IID_IActivationFactory, &IID_IDeviceInformationStatics, &IID_IDeviceInformationStatics2},
    0, static_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[3] = {&factory_vtbl, &statics_vtbl, &statics2_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbDeviceInformationFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
