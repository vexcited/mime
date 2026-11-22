#ifndef MIME_COMBASE_PRIVATE_H
#define MIME_COMBASE_PRIVATE_H

#define ROAPI DECLSPEC_EXPORT
#include <mime/win32/roapi.h>
#include <rtl.h>

FORCEINLINE void *CbAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

FORCEINLINE void CbFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* the entry of a function of ole32, which is loaded at run time. */
FARPROC CbOleProc(LPCSTR name);

/* a static string that needs no release, for runtime class names. */
HSTRING CbStaticString(HSTRING_HEADER *header, LPCWSTR text);

/* uisettings.c */
extern const WCHAR CbUISettingsClass[];
HRESULT CbUISettingsFactory(REFIID riid, void **factory);


/* apiinfo.c */
extern const WCHAR CbApiInformationClass[];
HRESULT CbApiInformationFactory(REFIID riid, void **factory);

/* apartment.c: factories a program registers for itself. */
HRESULT CbRegisteredFactory(HSTRING class_id, REFIID riid, void **factory);
/* apartment.c: in process classes of the registry. */
HRESULT CbRegistryFactory(HSTRING class_id, REFIID riid, void **factory);

/* regionpolicy.c */
extern const WCHAR CbRegionPolicyClass[];
HRESULT CbRegionPolicyFactory(REFIID riid, void **factory);

/* capture.c: Windows.Graphics.Capture. */
extern const WCHAR CbCaptureItemClass[], CbCaptureSessionClass[], CbCapturePoolClass[];
HRESULT CbCaptureItemFactory(REFIID riid, void **factory);
HRESULT CbCaptureSessionFactory(REFIID riid, void **factory);
HRESULT CbCapturePoolFactory(REFIID riid, void **factory);

/* async.c: operations that are finished before they are handed out. */
typedef enum CB_ASYNC_KIND {
    CB_ASYNC_ACTION,
    CB_ASYNC_BOOL,
    CB_ASYNC_KEY_RETRIEVAL,
    CB_ASYNC_PROFILE,
    CB_ASYNC_KINDS,
} CB_ASYNC_KIND;
/* a failed operation carries the error, a finished one the flag or the object, which the operation then owns. */
HRESULT CbAsyncFinished(CB_ASYNC_KIND kind, HRESULT error, BOOLEAN flag, IUnknown *object, void **operation);

/* collections.c: views of items fixed at creation. the view owns the items it is given. */
typedef enum CB_VECTOR_KIND {
    CB_VECTOR_PROFILES,
    CB_VECTOR_STRINGS,
    CB_VECTOR_KINDS,
} CB_VECTOR_KIND;
HRESULT CbNewVectorView(CB_VECTOR_KIND kind, void **items, UINT32 count, void **view);

/* netinfo.c: Windows.Networking.Connectivity.NetworkInformation. */
extern const WCHAR CbNetworkInformationClass[];
HRESULT CbNetworkInformationFactory(REFIID riid, void **factory);

/* mediadevice.c: Windows.Media.Devices.MediaDevice, and the audio endpoints as device interfaces. */
extern const WCHAR CbMediaDeviceClass[];
HRESULT CbMediaDeviceFactory(REFIID riid, void **factory);
#define CB_AUDIO_PATH 160
typedef struct CB_AUDIO_ENDPOINT {
    WCHAR Path[CB_AUDIO_PATH];
    WCHAR Name[128];
    BOOLEAN IsDefault;
} CB_AUDIO_ENDPOINT;
#define CB_AUDIO_MAX 32
/* audioendpoint.c: the endpoints of mmdevapi, for each library that compiles it. */
#define CB_AUDIO_RENDER_CLASS L"{e6327cad-dcec-4949-ae8a-991e976a79d2}"
#define CB_AUDIO_CAPTURE_CLASS L"{2eef81be-33fa-4800-9670-1cd474972c3f}"
/* the device interface path of an endpoint of a data flow, 0 render and 1 capture, from its mmdevapi id. */
void CbAudioEndpointPath(INT32 flow, LPCWSTR id, WCHAR path[CB_AUDIO_PATH]);
BOOLEAN CbAudioDefaultPath(INT32 flow, INT32 role, WCHAR path[CB_AUDIO_PATH]);
/* the active endpoints of a data flow. */
UINT32 CbAudioEndpoints(INT32 flow, CB_AUDIO_ENDPOINT *endpoints, UINT32 capacity);
/* starts listening to mmdevapi, which then calls the two functions below of the library. */
void CbAudioWatch(void);
/* the default endpoint of a role of AudioDeviceRole changed, the id is null when the direction is left without one. */
void CbAudioDefaultChanged(INT32 flow, INT32 role, LPCWSTR id);

/* a TypedEventHandler, whichever its arguments. */
typedef struct CB_EVENT_HANDLER {
    const struct {
        void *QueryInterface;
        ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG(STDMETHODCALLTYPE *Release)(void *This);
        HRESULT(STDMETHODCALLTYPE *Invoke)(void *This, IInspectable *sender, IInspectable *args);
    } *lpVtbl;
} CB_EVENT_HANDLER;

/* devinfo.c: Windows.Devices.Enumeration.DeviceInformation. */
extern const WCHAR CbDeviceInformationClass[];
HRESULT CbDeviceInformationFactory(REFIID riid, void **factory);
/* the endpoints changed: the started watchers look at them again, from a pool thread. */
void CbDeviceWatchersRefresh(void);

/* keycred.c: Windows.Security.Credentials.KeyCredentialManager. */
extern const WCHAR CbKeyCredentialManagerClass[];
HRESULT CbKeyCredentialManagerFactory(REFIID riid, void **factory);

#endif
