/* the core audio interfaces mmdevapi serves, from mmdeviceapi.h and audioclient.h. */
#ifndef MMDEVAPI_PRIVATE_H
#define MMDEVAPI_PRIVATE_H

#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/objbase.h>
#include <mime/win32/propidl.h>

#include <mime/win32/mmreg.h>

typedef LONGLONG REFERENCE_TIME;

typedef struct {
    GUID fmtid;
    DWORD pid;
} PROPERTYKEY;
typedef const PROPERTYKEY *REFPROPERTYKEY;

typedef enum { eRender = 0, eCapture = 1, eAll = 2 } EDataFlow;
typedef enum { eConsole = 0, eMultimedia = 1, eCommunications = 2 } ERole;
typedef enum { AUDCLNT_SHAREMODE_SHARED = 0, AUDCLNT_SHAREMODE_EXCLUSIVE = 1 } AUDCLNT_SHAREMODE;

#define DEVICE_STATE_ACTIVE 1
#define DEVICE_STATE_NOTPRESENT 4
#define DEVICE_STATEMASK_ALL 0xF

#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#define AUDCLNT_STREAMFLAGS_EVENTCALLBACK 0x00040000
#define AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY 0x1
#define AUDCLNT_BUFFERFLAGS_SILENT 0x2

#define AUDCLNT_E_NOT_INITIALIZED ((HRESULT)0x88890001)
#define AUDCLNT_E_ALREADY_INITIALIZED ((HRESULT)0x88890002)
#define AUDCLNT_E_WRONG_ENDPOINT_TYPE ((HRESULT)0x88890003)
#define AUDCLNT_E_DEVICE_INVALIDATED ((HRESULT)0x88890004)
#define AUDCLNT_E_NOT_STOPPED ((HRESULT)0x88890005)
#define AUDCLNT_E_BUFFER_TOO_LARGE ((HRESULT)0x88890006)
#define AUDCLNT_E_OUT_OF_ORDER ((HRESULT)0x88890007)
#define AUDCLNT_E_UNSUPPORTED_FORMAT ((HRESULT)0x88890008)
#define AUDCLNT_E_INVALID_SIZE ((HRESULT)0x88890009)
#define AUDCLNT_E_DEVICE_IN_USE ((HRESULT)0x8889000A)
#define AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED ((HRESULT)0x88890011)
#define AUDCLNT_E_EVENTHANDLE_NOT_SET ((HRESULT)0x88890014)
#define AUDCLNT_E_SERVICE_NOT_RUNNING ((HRESULT)0x88890010)
#define AUDCLNT_S_BUFFER_EMPTY ((HRESULT)0x08890001)

typedef struct IMMDevice IMMDevice;
typedef struct IMMDeviceCollection IMMDeviceCollection;
typedef struct IMMNotificationClient IMMNotificationClient;
typedef struct IPropertyStore IPropertyStore;

typedef struct IMMNotificationClientVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IMMNotificationClient *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IMMNotificationClient *This);
    ULONG(STDMETHODCALLTYPE *Release)(IMMNotificationClient *This);
    HRESULT(STDMETHODCALLTYPE *OnDeviceStateChanged)(IMMNotificationClient *This, LPCWSTR id, DWORD state);
    HRESULT(STDMETHODCALLTYPE *OnDeviceAdded)(IMMNotificationClient *This, LPCWSTR id);
    HRESULT(STDMETHODCALLTYPE *OnDeviceRemoved)(IMMNotificationClient *This, LPCWSTR id);
    HRESULT(STDMETHODCALLTYPE *OnDefaultDeviceChanged)(IMMNotificationClient *This, EDataFlow flow, ERole role, LPCWSTR id);
    HRESULT(STDMETHODCALLTYPE *OnPropertyValueChanged)(IMMNotificationClient *This, LPCWSTR id, const PROPERTYKEY key);
} IMMNotificationClientVtbl;

struct IMMNotificationClient {
    const IMMNotificationClientVtbl *lpVtbl;
};

typedef struct IMMDeviceEnumeratorVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *EnumAudioEndpoints)(void *This, EDataFlow flow, DWORD mask, IMMDeviceCollection **devices);
    HRESULT(STDMETHODCALLTYPE *GetDefaultAudioEndpoint)(void *This, EDataFlow flow, ERole role, IMMDevice **device);
    HRESULT(STDMETHODCALLTYPE *GetDevice)(void *This, LPCWSTR id, IMMDevice **device);
    HRESULT(STDMETHODCALLTYPE *RegisterEndpointNotificationCallback)(void *This, IMMNotificationClient *client);
    HRESULT(STDMETHODCALLTYPE *UnregisterEndpointNotificationCallback)(void *This, IMMNotificationClient *client);
} IMMDeviceEnumeratorVtbl;

typedef struct IMMDeviceCollectionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(void *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *Item)(void *This, UINT index, IMMDevice **device);
} IMMDeviceCollectionVtbl;

typedef struct IMMDeviceVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *Activate)(void *This, REFIID iid, DWORD context, PROPVARIANT *params, void **object);
    HRESULT(STDMETHODCALLTYPE *OpenPropertyStore)(void *This, DWORD access, IPropertyStore **store);
    HRESULT(STDMETHODCALLTYPE *GetId)(void *This, LPWSTR *id);
    HRESULT(STDMETHODCALLTYPE *GetState)(void *This, DWORD *state);
} IMMDeviceVtbl;

typedef struct IMMEndpointVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetDataFlow)(void *This, EDataFlow *flow);
} IMMEndpointVtbl;

typedef struct IPropertyStoreVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(void *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(void *This, DWORD index, PROPERTYKEY *key);
    HRESULT(STDMETHODCALLTYPE *GetValue)(void *This, REFPROPERTYKEY key, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *SetValue)(void *This, REFPROPERTYKEY key, const PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *Commit)(void *This);
} IPropertyStoreVtbl;

typedef struct IAudioClient3Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *Initialize)(void *This, AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
                                           REFERENCE_TIME period, const WAVEFORMATEX *format, LPCGUID session);
    HRESULT(STDMETHODCALLTYPE *GetBufferSize)(void *This, UINT32 *frames);
    HRESULT(STDMETHODCALLTYPE *GetStreamLatency)(void *This, REFERENCE_TIME *latency);
    HRESULT(STDMETHODCALLTYPE *GetCurrentPadding)(void *This, UINT32 *frames);
    HRESULT(STDMETHODCALLTYPE *IsFormatSupported)(void *This, AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *format,
                                                  WAVEFORMATEX **closest);
    HRESULT(STDMETHODCALLTYPE *GetMixFormat)(void *This, WAVEFORMATEX **format);
    HRESULT(STDMETHODCALLTYPE *GetDevicePeriod)(void *This, REFERENCE_TIME *default_period, REFERENCE_TIME *minimum);
    HRESULT(STDMETHODCALLTYPE *Start)(void *This);
    HRESULT(STDMETHODCALLTYPE *Stop)(void *This);
    HRESULT(STDMETHODCALLTYPE *Reset)(void *This);
    HRESULT(STDMETHODCALLTYPE *SetEventHandle)(void *This, HANDLE event);
    HRESULT(STDMETHODCALLTYPE *GetService)(void *This, REFIID iid, void **object);
    /* IAudioClient2 */
    HRESULT(STDMETHODCALLTYPE *IsOffloadCapable)(void *This, int category, BOOL *capable);
    HRESULT(STDMETHODCALLTYPE *SetClientProperties)(void *This, const void *properties);
    HRESULT(STDMETHODCALLTYPE *GetBufferSizeLimits)(void *This, const WAVEFORMATEX *format, BOOL event_driven,
                                                    REFERENCE_TIME *minimum, REFERENCE_TIME *maximum);
    /* IAudioClient3 */
    HRESULT(STDMETHODCALLTYPE *GetSharedModeEnginePeriod)(void *This, const WAVEFORMATEX *format, UINT32 *default_period,
                                                          UINT32 *fundamental, UINT32 *minimum, UINT32 *maximum);
    HRESULT(STDMETHODCALLTYPE *GetCurrentSharedModeEnginePeriod)(void *This, WAVEFORMATEX **format, UINT32 *period);
    HRESULT(STDMETHODCALLTYPE *InitializeSharedAudioStream)(void *This, DWORD flags, UINT32 period,
                                                            const WAVEFORMATEX *format, LPCGUID session);
} IAudioClient3Vtbl;

typedef struct IAudioRenderClientVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetBuffer)(void *This, UINT32 frames, BYTE **data);
    HRESULT(STDMETHODCALLTYPE *ReleaseBuffer)(void *This, UINT32 frames, DWORD flags);
} IAudioRenderClientVtbl;

typedef struct IAudioCaptureClientVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetBuffer)(void *This, BYTE **data, UINT32 *frames, DWORD *flags, UINT64 *device_position,
                                          UINT64 *qpc_position);
    HRESULT(STDMETHODCALLTYPE *ReleaseBuffer)(void *This, UINT32 frames);
    HRESULT(STDMETHODCALLTYPE *GetNextPacketSize)(void *This, UINT32 *frames);
} IAudioCaptureClientVtbl;

typedef struct IAudioClockVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetFrequency)(void *This, UINT64 *frequency);
    HRESULT(STDMETHODCALLTYPE *GetPosition)(void *This, UINT64 *position, UINT64 *qpc);
    HRESULT(STDMETHODCALLTYPE *GetCharacteristics)(void *This, DWORD *characteristics);
} IAudioClockVtbl;

typedef struct ISimpleAudioVolumeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *SetMasterVolume)(void *This, float level, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *GetMasterVolume)(void *This, float *level);
    HRESULT(STDMETHODCALLTYPE *SetMute)(void *This, BOOL mute, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *GetMute)(void *This, BOOL *mute);
} ISimpleAudioVolumeVtbl;

typedef struct IAudioStreamVolumeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetChannelCount)(void *This, UINT32 *count);
    HRESULT(STDMETHODCALLTYPE *SetChannelVolume)(void *This, UINT32 index, float level);
    HRESULT(STDMETHODCALLTYPE *GetChannelVolume)(void *This, UINT32 index, float *level);
    HRESULT(STDMETHODCALLTYPE *SetAllVolumes)(void *This, UINT32 count, const float *levels);
    HRESULT(STDMETHODCALLTYPE *GetAllVolumes)(void *This, UINT32 count, float *levels);
} IAudioStreamVolumeVtbl;

typedef struct IAudioEndpointVolumeCallbackVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *OnNotify)(void *This, const void *data);
} IAudioEndpointVolumeCallbackVtbl;

typedef struct IAudioEndpointVolumeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *RegisterControlChangeNotify)(void *This, void *callback);
    HRESULT(STDMETHODCALLTYPE *UnregisterControlChangeNotify)(void *This, void *callback);
    HRESULT(STDMETHODCALLTYPE *GetChannelCount)(void *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *SetMasterVolumeLevel)(void *This, float db, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *SetMasterVolumeLevelScalar)(void *This, float level, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *GetMasterVolumeLevel)(void *This, float *db);
    HRESULT(STDMETHODCALLTYPE *GetMasterVolumeLevelScalar)(void *This, float *level);
    HRESULT(STDMETHODCALLTYPE *SetChannelVolumeLevel)(void *This, UINT channel, float db, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *SetChannelVolumeLevelScalar)(void *This, UINT channel, float level, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *GetChannelVolumeLevel)(void *This, UINT channel, float *db);
    HRESULT(STDMETHODCALLTYPE *GetChannelVolumeLevelScalar)(void *This, UINT channel, float *level);
    HRESULT(STDMETHODCALLTYPE *SetMute)(void *This, BOOL mute, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *GetMute)(void *This, BOOL *mute);
    HRESULT(STDMETHODCALLTYPE *GetVolumeStepInfo)(void *This, UINT *step, UINT *count);
    HRESULT(STDMETHODCALLTYPE *VolumeStepUp)(void *This, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *VolumeStepDown)(void *This, LPCGUID context);
    HRESULT(STDMETHODCALLTYPE *QueryHardwareSupport)(void *This, DWORD *mask);
    HRESULT(STDMETHODCALLTYPE *GetVolumeRange)(void *This, float *minimum, float *maximum, float *increment);
} IAudioEndpointVolumeVtbl;

MIME_DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
MIME_DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
MIME_DEFINE_GUID(IID_IMMDeviceCollection, 0x0BD7A1BE, 0x7A1A, 0x44DB, 0x83, 0x97, 0xCC, 0x53, 0x92, 0x38, 0x7B, 0x5E);
MIME_DEFINE_GUID(IID_IMMDevice, 0xD666063F, 0x1587, 0x4E43, 0x81, 0xF1, 0xB9, 0x48, 0xE8, 0x07, 0x36, 0x3F);
MIME_DEFINE_GUID(IID_IMMEndpoint, 0x1BE09788, 0x6894, 0x4089, 0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5);
MIME_DEFINE_GUID(IID_IPropertyStore, 0x886D8EEB, 0x8CF2, 0x4446, 0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99);
MIME_DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
MIME_DEFINE_GUID(IID_IAudioClient2, 0x726778CD, 0xF60A, 0x4EDA, 0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA);
MIME_DEFINE_GUID(IID_IAudioClient3, 0x7ED4EE07, 0x8E67, 0x4CD4, 0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42);
MIME_DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
MIME_DEFINE_GUID(IID_IAudioCaptureClient, 0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
MIME_DEFINE_GUID(IID_IAudioClock, 0xCD63314F, 0x3FBA, 0x4A1B, 0x81, 0x2C, 0xEF, 0x96, 0x35, 0x87, 0x28, 0xE7);
MIME_DEFINE_GUID(IID_ISimpleAudioVolume, 0x87CE5498, 0x68D6, 0x44E5, 0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8);
MIME_DEFINE_GUID(IID_IAudioStreamVolume, 0x93014887, 0x242D, 0x4068, 0x8A, 0x15, 0xCF, 0x5E, 0x93, 0xB9, 0x0F, 0xE3);
MIME_DEFINE_GUID(IID_IAudioEndpointVolume, 0x5CDF2C82, 0x841E, 0x4546, 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A);
MIME_DEFINE_GUID(IID_IAudioEndpointVolumeCallback, 0x657804FA, 0xD6AD, 0x4496, 0x8A, 0x60, 0x35, 0x27, 0x52, 0xAF, 0x4F, 0x89);
MIME_DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
MIME_DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B,
                 0x71);
/* PKEY_Device_FriendlyName and PKEY_Device_DeviceDesc share this format id. */
MIME_DEFINE_GUID(FMTID_Device, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0);
MIME_DEFINE_GUID(FMTID_DeviceInterface, 0x026E516E, 0xB814, 0x414B, 0x83, 0xCD, 0x85, 0x6D, 0x6F, 0xEF, 0x48, 0x22);
MIME_DEFINE_GUID(FMTID_AudioEngine, 0xF19F064D, 0x082C, 0x4E27, 0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C);
/* PKEY_AudioEndpoint_GUID and PKEY_AudioEndpoint_PhysicalSpeakers share this one. */
MIME_DEFINE_GUID(FMTID_AudioEndpoint, 0x1DA5D803, 0xD492, 0x4EDD, 0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E);

void *MmTaskAlloc(SIZE_T size);
/* the format the engine mixes an endpoint at: floats at the rate and channels the host runs its device at. */
void MmMixFormat(WAVEFORMATEXTENSIBLE *format, UINT32 rate, UINT32 channels);
/* the speakers windows gives a channel count. */
DWORD MmChannelMask(UINT32 channels);
/*
 * device names the endpoint the stream stays on. NULL is the default one of the direction as it changes, which is
 * what an activation by device interface class gets.
 */
HRESULT MmCreateAudioClient(BOOL capture, const GUID *device, UINT32 rate, UINT32 channels, void **client);
/* an endpoint's volume and mute, kept per process and not applied by its clients. */
void *MmEndpointVolume(BOOL capture);

#endif
