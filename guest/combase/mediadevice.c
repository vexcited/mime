/*
 * Windows.Media.Devices.MediaDevice: the selectors of audio and video devices, the ids of the default audio endpoints
 * and the events of their change. the ids are the device interface paths of the endpoints mmdevapi has.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>

const WCHAR CbMediaDeviceClass[] = L"Windows.Media.Devices.MediaDevice";

MIME_DEFINE_GUID(IID_IMediaDeviceStatics, 0xAA2D9A40, 0x909F, 0x4BBA, 0xBF, 0x8B, 0x0C, 0x0D, 0x29, 0x6F, 0x14, 0xF0);

/* the selectors are those of windows 11, letter for letter. */
#define VIDEO_CAPTURE_CLASS L"{e5323777-f976-4f5b-9b55-b94699c46e44}"
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

static HRESULT make_string(LPCWSTR text, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    return WindowsCreateString(text, (UINT32)wcslen(text), value);
}

/* AudioDeviceRole Default is the console role and Communications the communications one. without the endpoint the id is empty. */
static HRESULT default_id(INT32 flow, INT32 role, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (role != 0 && role != 1)
        return E_INVALIDARG;
    WCHAR path[CB_AUDIO_PATH];
    if (!CbAudioDefaultPath(flow, role ? 2 : 0, path))
        return S_OK;
    return path[0] ? make_string(path, value) : E_UNEXPECTED;
}

static HRESULT STDMETHODCALLTYPE capture_selector(IMediaDeviceStatics *This, HSTRING *value)
{
    return make_string(SELECTOR(CB_AUDIO_CAPTURE_CLASS), value);
}

static HRESULT STDMETHODCALLTYPE render_selector(IMediaDeviceStatics *This, HSTRING *value)
{
    return make_string(SELECTOR(CB_AUDIO_RENDER_CLASS), value);
}

static HRESULT STDMETHODCALLTYPE video_selector(IMediaDeviceStatics *This, HSTRING *value)
{
    return make_string(SELECTOR(VIDEO_CAPTURE_CLASS)
                       L" AND (System.Devices.WinPhone8CameraFlags:=[] OR System.Devices.WinPhone8CameraFlags:<4096)",
                       value);
}

static HRESULT STDMETHODCALLTYPE default_capture(IMediaDeviceStatics *This, INT32 role, HSTRING *value)
{
    return default_id(1, role, value);
}

static HRESULT STDMETHODCALLTYPE default_render(IMediaDeviceStatics *This, INT32 role, HSTRING *value)
{
    return default_id(0, role, value);
}

typedef struct HANDLER {
    INT64 Token;
    CB_EVENT_HANDLER *Handler;
} HANDLER;

#define MAX_HANDLERS 32

static SRWLOCK g_handler_lock = SRWLOCK_INIT;
static HANDLER g_handlers[2][MAX_HANDLERS];
static INT64 g_next_token;

/* ------------------------------------------------------------------ the arguments of a default device change */

MIME_DEFINE_GUID(IID_IDefaultAudioDeviceChangedEventArgs, 0x110F882F, 0x1C05, 0x4657, 0xA1, 0x8E, 0x47, 0xC9, 0xB6, 0x9F,
                 0x07, 0xAB);

typedef struct IDefaultAudioDeviceChangedEventArgs IDefaultAudioDeviceChangedEventArgs;
#define IDefaultAudioDeviceChangedEventArgs_METHODS(T)                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Id)(T * This, HSTRING * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_Role)(T * This, INT32 * value);
MIME_COM_INTERFACE(IDefaultAudioDeviceChangedEventArgs)

WRT_INSPECTABLE_THUNKS(IDefaultAudioDeviceChangedEventArgs)

typedef struct CHANGED_ARGS {
    WRT_OBJECT Base;
    INT32 Role;
    WCHAR Path[CB_AUDIO_PATH];
} CHANGED_ARGS;

#define ARGS_OF(This) ((CHANGED_ARGS *)((WRT_IFACE *)(This))->Owner)

static HRESULT STDMETHODCALLTYPE args_id(IDefaultAudioDeviceChangedEventArgs *This, HSTRING *value)
{
    return make_string(ARGS_OF(This)->Path, value);
}

static HRESULT STDMETHODCALLTYPE args_role(IDefaultAudioDeviceChangedEventArgs *This, INT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = ARGS_OF(This)->Role;
    return S_OK;
}

static const IDefaultAudioDeviceChangedEventArgsVtbl args_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDefaultAudioDeviceChangedEventArgs), args_id, args_role};

static void args_destroy(WRT_OBJECT *object)
{
    CbFree(object);
}

/* the two classes share one interface. */
static const WRT_CLASS args_classes[2] = {
    {L"Windows.Media.Devices.DefaultAudioRenderDeviceChangedEventArgs", 1, {&IID_IDefaultAudioDeviceChangedEventArgs},
     0, args_destroy},
    {L"Windows.Media.Devices.DefaultAudioCaptureDeviceChangedEventArgs", 1, {&IID_IDefaultAudioDeviceChangedEventArgs},
     0, args_destroy}};

/* the id is empty when the direction is left without a device. the sender of a static event is null. */
void CbAudioDefaultChanged(INT32 flow, INT32 role, LPCWSTR id)
{
    CHANGED_ARGS *args = CbAlloc(sizeof(*args));
    if (!args)
        return;
    const void *vtables[1] = {&args_vtbl};
    WrtInit(&args->Base, &args_classes[flow], vtables);
    args->Role = role;
    args->Path[0] = 0;
    if (id)
        CbAudioEndpointPath(flow, id, args->Path);
    CB_EVENT_HANDLER *handlers[MAX_HANDLERS];
    ULONG count = 0;
    AcquireSRWLockShared(&g_handler_lock);
    for (ULONG i = 0; i < MAX_HANDLERS; i++)
        if (g_handlers[flow][i].Handler) {
            handlers[count] = g_handlers[flow][i].Handler;
            handlers[count]->lpVtbl->AddRef(handlers[count]);
            count++;
        }
    ReleaseSRWLockShared(&g_handler_lock);
    for (ULONG i = 0; i < count; i++) {
        handlers[i]->lpVtbl->Invoke(handlers[i], nullptr, (IInspectable *)&args->Base.Ifaces[0]);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
    WrtRelease(&args->Base.Ifaces[0]);
}

static HRESULT add_handler(INT32 flow, IUnknown *handler, EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_POINTER;
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_handler_lock);
    for (ULONG i = 0; i < MAX_HANDLERS; i++)
        if (!g_handlers[flow][i].Handler) {
            handler->lpVtbl->AddRef(handler);
            g_handlers[flow][i].Handler = (CB_EVENT_HANDLER *)handler;
            g_handlers[flow][i].Token = ++g_next_token;
            token->value = g_next_token;
            hr = S_OK;
            break;
        }
    ReleaseSRWLockExclusive(&g_handler_lock);
    if (SUCCEEDED(hr))
        CbAudioWatch();
    return hr;
}

static HRESULT remove_handler(INT32 flow, EventRegistrationToken token)
{
    CB_EVENT_HANDLER *removed = nullptr;
    AcquireSRWLockExclusive(&g_handler_lock);
    for (ULONG i = 0; i < MAX_HANDLERS; i++)
        if (g_handlers[flow][i].Handler && g_handlers[flow][i].Token == token.value) {
            removed = g_handlers[flow][i].Handler;
            g_handlers[flow][i].Handler = nullptr;
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
    return add_handler(1, handler, token);
}

static HRESULT STDMETHODCALLTYPE remove_capture_changed(IMediaDeviceStatics *This, EventRegistrationToken token)
{
    return remove_handler(1, token);
}

static HRESULT STDMETHODCALLTYPE add_render_changed(IMediaDeviceStatics *This, IUnknown *handler,
                                                    EventRegistrationToken *token)
{
    return add_handler(0, handler, token);
}

static HRESULT STDMETHODCALLTYPE remove_render_changed(IMediaDeviceStatics *This, EventRegistrationToken token)
{
    return remove_handler(0, token);
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

static const WRT_CLASS factory_class = {nullptr, 2, {&IID_IActivationFactory, &IID_IMediaDeviceStatics}, 0,
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
