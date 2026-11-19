/* the interfaces of the enhanced video renderer and of the samples and allocators it shares with mfplat. */
#ifndef MIME_WIN32_EVR_H
#define MIME_WIN32_EVR_H

#include <mime/win32/mfidl.h>
#include <mime/win32/d3d9.h>
#include <mime/win32/dxva2api.h>

typedef struct IMFTrackedSample IMFTrackedSample;
typedef struct IMFDesiredSample IMFDesiredSample;
typedef struct IMFVideoSampleAllocator IMFVideoSampleAllocator;
typedef struct IMFVideoSampleAllocatorNotify IMFVideoSampleAllocatorNotify;
typedef struct IMFVideoSampleAllocatorCallback IMFVideoSampleAllocatorCallback;
typedef struct IDirect3DDeviceManager9 IDirect3DDeviceManager9;

#define IMFTrackedSample_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetAllocator)(T * This, IMFAsyncCallback *allocator, IUnknown *state);

#define IMFDesiredSample_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDesiredSampleTimeAndDuration)(T * This, LONGLONG *time, LONGLONG *duration);         \
    void(STDMETHODCALLTYPE *SetDesiredSampleTimeAndDuration)(T * This, LONGLONG time, LONGLONG duration);              \
    void(STDMETHODCALLTYPE *Clear)(T * This);

#define IMFVideoSampleAllocator_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetDirectXManager)(T * This, IUnknown *manager);                                        \
    HRESULT(STDMETHODCALLTYPE *UninitializeSampleAllocator)(T * This);                                                 \
    HRESULT(STDMETHODCALLTYPE *InitializeSampleAllocator)(T * This, DWORD count, IMFMediaType *type);                  \
    HRESULT(STDMETHODCALLTYPE *AllocateSample)(T * This, IMFSample **sample);

#define IMFVideoSampleAllocatorNotify_METHODS(T)                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *NotifyRelease)(T * This);

#define IMFVideoSampleAllocatorCallback_METHODS(T)                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetCallback)(T * This, IMFVideoSampleAllocatorNotify *notify);                          \
    HRESULT(STDMETHODCALLTYPE *GetFreeSampleCount)(T * This, LONG *count);

#define IDirect3DDeviceManager9_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ResetDevice)(T * This, IDirect3DDevice9 *device, UINT token);                           \
    HRESULT(STDMETHODCALLTYPE *OpenDeviceHandle)(T * This, HANDLE *handle);                                            \
    HRESULT(STDMETHODCALLTYPE *CloseDeviceHandle)(T * This, HANDLE handle);                                            \
    HRESULT(STDMETHODCALLTYPE *TestDevice)(T * This, HANDLE handle);                                                   \
    HRESULT(STDMETHODCALLTYPE *LockDevice)(T * This, HANDLE handle, IDirect3DDevice9 **device, BOOL block);            \
    HRESULT(STDMETHODCALLTYPE *UnlockDevice)(T * This, HANDLE handle, BOOL save_state);                                \
    HRESULT(STDMETHODCALLTYPE *GetVideoService)(T * This, HANDLE handle, REFIID riid, void **service);

MIME_COM_INTERFACE(IMFTrackedSample)
MIME_COM_INTERFACE(IMFDesiredSample)
MIME_COM_INTERFACE(IMFVideoSampleAllocator)
MIME_COM_INTERFACE(IMFVideoSampleAllocatorNotify)
MIME_COM_INTERFACE(IMFVideoSampleAllocatorCallback)
MIME_COM_INTERFACE(IDirect3DDeviceManager9)

MIME_DEFINE_GUID(IID_IMFTrackedSample, 0x245BF8E9, 0x0755, 0x40F7, 0x88, 0xA5, 0xAE, 0x0F, 0x18, 0xD5, 0x5E, 0x17);
MIME_DEFINE_GUID(IID_IMFDesiredSample, 0x56C294D0, 0x753E, 0x4260, 0x8D, 0x61, 0xA3, 0xD8, 0x82, 0x0B, 0x1D, 0x54);
MIME_DEFINE_GUID(IID_IMFVideoSampleAllocator, 0x86CBC910, 0xE533, 0x4751, 0x8E, 0x3B, 0xF1, 0x9B, 0x5B, 0x80, 0x6A,
                 0x03);
MIME_DEFINE_GUID(IID_IMFVideoSampleAllocatorNotify, 0xA792CDBE, 0xC374, 0x4E89, 0x83, 0x35, 0x27, 0x8E, 0x7B, 0x99,
                 0x56, 0xA4);
MIME_DEFINE_GUID(IID_IMFVideoSampleAllocatorCallback, 0x992388B4, 0x3372, 0x4F67, 0x8B, 0x6F, 0xC8, 0x4C, 0x07, 0x1F,
                 0x47, 0x51);
MIME_DEFINE_GUID(IID_IDirect3DDeviceManager9, 0xA0CADE0F, 0x06D5, 0x4CF4, 0xA1, 0xC7, 0xF3, 0xCD, 0xD7, 0x25, 0xAA,
                 0x75);
MIME_DEFINE_GUID(MR_BUFFER_SERVICE, 0xA562248C, 0x9AC6, 0x4FFC, 0x9F, 0xBA, 0x3A, 0xF8, 0xF8, 0xAD, 0x1A, 0x4D);
MIME_DEFINE_GUID(MR_VIDEO_RENDER_SERVICE, 0x1092A86C, 0xAB1A, 0x459A, 0xA3, 0x36, 0x83, 0x1F, 0xBC, 0x4D, 0x11, 0xFF);
MIME_DEFINE_GUID(MR_VIDEO_MIXER_SERVICE, 0x073CD2FC, 0x6CF4, 0x40B7, 0x88, 0x59, 0xE8, 0x95, 0x52, 0xC8, 0x41, 0xF8);
MIME_DEFINE_GUID(MR_VIDEO_ACCELERATION_SERVICE, 0xEFEF5175, 0x5C7D, 0x4CE2, 0xBB, 0xBD, 0x34, 0xFF, 0x8B, 0xCA, 0x65,
                 0x54);
MIME_DEFINE_GUID(MFSampleExtension_3DVideo, 0xF86F97A4, 0xDD54, 0x4E2E, 0x9A, 0x5E, 0x55, 0xFC, 0x2D, 0x74, 0xA0, 0x05);
MIME_DEFINE_GUID(MFSampleExtension_3DVideo_SampleFormat, 0x08671772, 0xE36F, 0x4CFF, 0x97, 0xB3, 0xD7, 0x2E, 0x20, 0x98,
                 0x7A, 0x48);


typedef LONGLONG MFTIME;

/* the transform interface the mixer serves, and the interfaces of the mixer and of the presenter. */
typedef struct IMFTransform IMFTransform;
typedef struct IMFVideoDeviceID IMFVideoDeviceID;
typedef struct IMFTopologyServiceLookup IMFTopologyServiceLookup;
typedef struct IMFTopologyServiceLookupClient IMFTopologyServiceLookupClient;
typedef struct IMFVideoMixerControl IMFVideoMixerControl;
typedef struct IMFVideoMixerControl2 IMFVideoMixerControl2;
typedef struct IMFVideoPositionMapper IMFVideoPositionMapper;
typedef struct IMFVideoProcessor IMFVideoProcessor;
typedef struct IMFVideoMixerBitmap IMFVideoMixerBitmap;
typedef struct IMFQualityAdvise IMFQualityAdvise;
typedef struct IMFQualityAdviseLimits IMFQualityAdviseLimits;
typedef struct IMFClockStateSink IMFClockStateSink;
typedef struct IEVRTrustedVideoPlugin IEVRTrustedVideoPlugin;
typedef struct IMediaEventSink IMediaEventSink;
typedef struct IMFVideoPresenter IMFVideoPresenter;
typedef struct IMFVideoDisplayControl IMFVideoDisplayControl;
typedef struct IMFRateSupport IMFRateSupport;
typedef struct IQualProp IQualProp;
typedef struct IMFClock IMFClock;

typedef struct _MFT_INPUT_STREAM_INFO {
    LONGLONG hnsMaxLatency;
    DWORD dwFlags, cbSize, cbMaxLookahead, cbAlignment;
} MFT_INPUT_STREAM_INFO;
typedef struct _MFT_OUTPUT_STREAM_INFO {
    DWORD dwFlags, cbSize, cbAlignment;
} MFT_OUTPUT_STREAM_INFO;
typedef struct _MFT_OUTPUT_DATA_BUFFER {
    DWORD dwStreamID;
    IMFSample *pSample;
    DWORD dwStatus;
    IMFCollection *pEvents;
} MFT_OUTPUT_DATA_BUFFER;
typedef enum _MFT_MESSAGE_TYPE {
    MFT_MESSAGE_COMMAND_FLUSH = 0,
    MFT_MESSAGE_COMMAND_DRAIN = 1,
    MFT_MESSAGE_SET_D3D_MANAGER = 2,
    MFT_MESSAGE_DROP_SAMPLES = 3,
    MFT_MESSAGE_COMMAND_TICK = 4,
    MFT_MESSAGE_NOTIFY_BEGIN_STREAMING = 0x10000000,
    MFT_MESSAGE_NOTIFY_END_STREAMING = 0x10000001,
    MFT_MESSAGE_NOTIFY_END_OF_STREAM = 0x10000002,
    MFT_MESSAGE_NOTIFY_START_OF_STREAM = 0x10000003,
    MFT_MESSAGE_COMMAND_MARKER = 0x20000000,
} MFT_MESSAGE_TYPE;
#define MFT_SET_TYPE_TEST_ONLY 0x1
#define MFT_OUTPUT_DATA_BUFFER_INCOMPLETE 0x01000000
#define MFT_INPUT_STATUS_ACCEPT_DATA 0x1
#define MFT_OUTPUT_STATUS_SAMPLE_READY 0x1

typedef enum MF_SERVICE_LOOKUP_TYPE {
    MF_SERVICE_LOOKUP_UPSTREAM = 0,
    MF_SERVICE_LOOKUP_UPSTREAM_DIRECT,
    MF_SERVICE_LOOKUP_DOWNSTREAM,
    MF_SERVICE_LOOKUP_DOWNSTREAM_DIRECT,
    MF_SERVICE_LOOKUP_ALL,
    MF_SERVICE_LOOKUP_GLOBAL,
} MF_SERVICE_LOOKUP_TYPE;

typedef enum _MF_QUALITY_DROP_MODE { MF_DROP_MODE_NONE = 0, MF_DROP_MODE_1, MF_DROP_MODE_2, MF_DROP_MODE_3,
                                     MF_DROP_MODE_4, MF_DROP_MODE_5, MF_NUM_DROP_MODES } MF_QUALITY_DROP_MODE;
typedef enum _MF_QUALITY_LEVEL { MF_QUALITY_NORMAL = 0, MF_QUALITY_NORMAL_MINUS_1, MF_QUALITY_NORMAL_MINUS_2,
                                 MF_QUALITY_NORMAL_MINUS_3, MF_QUALITY_NORMAL_MINUS_4, MF_QUALITY_NORMAL_MINUS_5,
                                 MF_NUM_QUALITY_LEVELS } MF_QUALITY_LEVEL;

typedef struct MFVideoNormalizedRect {
    float left, top, right, bottom;
} MFVideoNormalizedRect;

typedef struct MFVideoAlphaBitmapParams {
    DWORD dwFlags;
    COLORREF clrSrcKey;
    RECT rcSrc;
    MFVideoNormalizedRect nrcDest;
    FLOAT fAlpha;
    DWORD dwFilterMode;
} MFVideoAlphaBitmapParams;
typedef struct MFVideoAlphaBitmap {
    BOOL GetBitmapFromDC;
    union {
        HDC hdc;
        IDirect3DSurface9 *pDDS;
    } bitmap;
    MFVideoAlphaBitmapParams params;
} MFVideoAlphaBitmap;
#define MFVideoAlphaBitmap_EntireDDS 0x1
#define MFVideoAlphaBitmap_SrcColorKey 0x2
#define MFVideoAlphaBitmap_SrcRect 0x4
#define MFVideoAlphaBitmap_DestRect 0x8
#define MFVideoAlphaBitmap_FilterMode 0x10
#define MFVideoAlphaBitmap_Alpha 0x20
#define MFVideoAlphaBitmap_BitMask 0x3F

typedef enum MFVP_MESSAGE_TYPE {
    MFVP_MESSAGE_FLUSH = 0,
    MFVP_MESSAGE_INVALIDATEMEDIATYPE = 1,
    MFVP_MESSAGE_PROCESSINPUTNOTIFY = 2,
    MFVP_MESSAGE_BEGINSTREAMING = 3,
    MFVP_MESSAGE_ENDSTREAMING = 4,
    MFVP_MESSAGE_ENDOFSTREAM = 5,
    MFVP_MESSAGE_STEP = 6,
    MFVP_MESSAGE_CANCELSTEP = 7,
} MFVP_MESSAGE_TYPE;

typedef enum MFVideoAspectRatioMode { MFVideoARMode_None = 0, MFVideoARMode_PreservePicture = 0x1,
                                      MFVideoARMode_PreservePixel = 0x2, MFVideoARMode_NonLinearStretch = 0x4,
                                      MFVideoARMode_Mask = 0x7 } MFVideoAspectRatioMode;
typedef enum MFVideoRenderPrefs { MFVideoRenderPrefs_DoNotRenderBorder = 0x1,
                                  MFVideoRenderPrefs_DoNotClipToDevice = 0x2,
                                  MFVideoRenderPrefs_AllowOutputThrottling = 0x4,
                                  MFVideoRenderPrefs_ForceOutputThrottling = 0x8,
                                  MFVideoRenderPrefs_ForceBatching = 0x10,
                                  MFVideoRenderPrefs_AllowBatching = 0x20,
                                  MFVideoRenderPrefs_ForceScaling = 0x40,
                                  MFVideoRenderPrefs_AllowScaling = 0x80,
                                  MFVideoRenderPrefs_DoNotRepaintOnStop = 0x100,
                                  MFVideoRenderPrefs_Mask = 0x1FF } MFVideoRenderPrefs;
#ifndef PRESENTATION_CURRENT_POSITION
#define PRESENTATION_CURRENT_POSITION 0x7FFFFFFFFFFFFFFFLL
#endif
typedef enum _MFVideoMixPrefs {
    MFVideoMixPrefs_ForceHalfInterlace = 0x1,
    MFVideoMixPrefs_AllowDropToHalfInterlace = 0x2,
    MFVideoMixPrefs_AllowDropToBob = 0x4,
    MFVideoMixPrefs_ForceBob = 0x8,
    MFVideoMixPrefs_EnableRotation = 0x10,
    MFVideoMixPrefs_Mask = 0x1F
} MFVideoMixPrefs;
typedef enum _MFRATE_DIRECTION { MFRATE_FORWARD = 0, MFRATE_REVERSE } MFRATE_DIRECTION;
typedef enum _MFCLOCK_STATE { MFCLOCK_STATE_INVALID = 0, MFCLOCK_STATE_RUNNING, MFCLOCK_STATE_STOPPED,
                              MFCLOCK_STATE_PAUSED } MFCLOCK_STATE;
typedef struct _MFCLOCK_PROPERTIES {
    unsigned __int64 qwCorrelationRate;
    GUID guidClockId;
    DWORD dwClockFlags;
    unsigned __int64 qwClockFrequency;
    DWORD dwClockTolerance, dwClockJitter;
} MFCLOCK_PROPERTIES;

#define IMFTransform_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetStreamLimits)(T * This, DWORD *in_min, DWORD *in_max, DWORD *out_min,               \
                                                DWORD *out_max);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetStreamCount)(T * This, DWORD *in, DWORD *out);                                       \
    HRESULT(STDMETHODCALLTYPE *GetStreamIDs)(T * This, DWORD in_size, DWORD *in, DWORD out_size, DWORD *out);          \
    HRESULT(STDMETHODCALLTYPE *GetInputStreamInfo)(T * This, DWORD id, MFT_INPUT_STREAM_INFO *info);                   \
    HRESULT(STDMETHODCALLTYPE *GetOutputStreamInfo)(T * This, DWORD id, MFT_OUTPUT_STREAM_INFO *info);                 \
    HRESULT(STDMETHODCALLTYPE *GetAttributes)(T * This, IMFAttributes **attributes);                                   \
    HRESULT(STDMETHODCALLTYPE *GetInputStreamAttributes)(T * This, DWORD id, IMFAttributes **attributes);              \
    HRESULT(STDMETHODCALLTYPE *GetOutputStreamAttributes)(T * This, DWORD id, IMFAttributes **attributes);             \
    HRESULT(STDMETHODCALLTYPE *DeleteInputStream)(T * This, DWORD id);                                                 \
    HRESULT(STDMETHODCALLTYPE *AddInputStreams)(T * This, DWORD count, DWORD *ids);                                    \
    HRESULT(STDMETHODCALLTYPE *GetInputAvailableType)(T * This, DWORD id, DWORD index, IMFMediaType **type);          \
    HRESULT(STDMETHODCALLTYPE *GetOutputAvailableType)(T * This, DWORD id, DWORD index, IMFMediaType **type);         \
    HRESULT(STDMETHODCALLTYPE *SetInputType)(T * This, DWORD id, IMFMediaType *type, DWORD flags);                    \
    HRESULT(STDMETHODCALLTYPE *SetOutputType)(T * This, DWORD id, IMFMediaType *type, DWORD flags);                   \
    HRESULT(STDMETHODCALLTYPE *GetInputCurrentType)(T * This, DWORD id, IMFMediaType **type);                         \
    HRESULT(STDMETHODCALLTYPE *GetOutputCurrentType)(T * This, DWORD id, IMFMediaType **type);                        \
    HRESULT(STDMETHODCALLTYPE *GetInputStatus)(T * This, DWORD id, DWORD *flags);                                      \
    HRESULT(STDMETHODCALLTYPE *GetOutputStatus)(T * This, DWORD *flags);                                               \
    HRESULT(STDMETHODCALLTYPE *SetOutputBounds)(T * This, LONGLONG lower, LONGLONG upper);                             \
    HRESULT(STDMETHODCALLTYPE *ProcessEvent)(T * This, DWORD id, IMFMediaEvent *event);                                \
    HRESULT(STDMETHODCALLTYPE *ProcessMessage)(T * This, MFT_MESSAGE_TYPE message, ULONG_PTR param);                   \
    HRESULT(STDMETHODCALLTYPE *ProcessInput)(T * This, DWORD id, IMFSample *sample, DWORD flags);                     \
    HRESULT(STDMETHODCALLTYPE *ProcessOutput)(T * This, DWORD flags, DWORD count, MFT_OUTPUT_DATA_BUFFER *buffers,    \
                                              DWORD *status);

#define IMFVideoDeviceID_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDeviceID)(T * This, IID *device);

#define IMFTopologyServiceLookup_METHODS(T)                                                                            \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *LookupService)(T * This, MF_SERVICE_LOOKUP_TYPE type, DWORD index, REFGUID service,   \
                                              REFIID riid, void **objects, DWORD *count);

#define IMFTopologyServiceLookupClient_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *InitServicePointers)(T * This, IMFTopologyServiceLookup *lookup);                       \
    HRESULT(STDMETHODCALLTYPE *ReleaseServicePointers)(T * This);

#define IMFVideoMixerControl_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetStreamZOrder)(T * This, DWORD id, DWORD z);                                          \
    HRESULT(STDMETHODCALLTYPE *GetStreamZOrder)(T * This, DWORD id, DWORD *z);                                         \
    HRESULT(STDMETHODCALLTYPE *SetStreamOutputRect)(T * This, DWORD id, const MFVideoNormalizedRect *rect);           \
    HRESULT(STDMETHODCALLTYPE *GetStreamOutputRect)(T * This, DWORD id, MFVideoNormalizedRect *rect);

#define IMFVideoMixerControl2_METHODS(T)                                                                               \
    IMFVideoMixerControl_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *SetMixingPrefs)(T * This, DWORD prefs);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetMixingPrefs)(T * This, DWORD *prefs);

#define IMFVideoPositionMapper_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *MapOutputCoordinateToInputStream)(T * This, float x, float y, DWORD out_id,           \
                                                                 DWORD in_id, float *in_x, float *in_y);

#define IMFVideoProcessor_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetAvailableVideoProcessorModes)(T * This, UINT *count, GUID **modes);                  \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorCaps)(T * This, GUID *mode, DXVA2_VideoProcessorCaps *caps);           \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorMode)(T * This, GUID *mode);                                           \
    HRESULT(STDMETHODCALLTYPE *SetVideoProcessorMode)(T * This, GUID *mode);                                           \
    HRESULT(STDMETHODCALLTYPE *GetProcAmpRange)(T * This, DWORD property, DXVA2_ValueRange *range);                    \
    HRESULT(STDMETHODCALLTYPE *GetProcAmpValues)(T * This, DWORD flags, DXVA2_ProcAmpValues *values);                  \
    HRESULT(STDMETHODCALLTYPE *SetProcAmpValues)(T * This, DWORD flags, DXVA2_ProcAmpValues *values);                  \
    HRESULT(STDMETHODCALLTYPE *GetFilteringRange)(T * This, DWORD setting, DXVA2_ValueRange *range);                   \
    HRESULT(STDMETHODCALLTYPE *GetFilteringValue)(T * This, DWORD setting, DXVA2_Fixed32 *value);                      \
    HRESULT(STDMETHODCALLTYPE *SetFilteringValue)(T * This, DWORD setting, DXVA2_Fixed32 *value);                      \
    HRESULT(STDMETHODCALLTYPE *GetBackgroundColor)(T * This, COLORREF *color);                                         \
    HRESULT(STDMETHODCALLTYPE *SetBackgroundColor)(T * This, COLORREF color);

#define IMFVideoMixerBitmap_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetAlphaBitmap)(T * This, const MFVideoAlphaBitmap *bitmap);                            \
    HRESULT(STDMETHODCALLTYPE *ClearAlphaBitmap)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *UpdateAlphaBitmapParameters)(T * This, const MFVideoAlphaBitmapParams *params);         \
    HRESULT(STDMETHODCALLTYPE *GetAlphaBitmapParameters)(T * This, MFVideoAlphaBitmapParams *params);

#define IMFQualityAdvise_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetDropMode)(T * This, MF_QUALITY_DROP_MODE mode);                                      \
    HRESULT(STDMETHODCALLTYPE *SetQualityLevel)(T * This, MF_QUALITY_LEVEL level);                                     \
    HRESULT(STDMETHODCALLTYPE *GetDropMode)(T * This, MF_QUALITY_DROP_MODE *mode);                                     \
    HRESULT(STDMETHODCALLTYPE *GetQualityLevel)(T * This, MF_QUALITY_LEVEL *level);                                    \
    HRESULT(STDMETHODCALLTYPE *DropTime)(T * This, LONGLONG amount);

#define IMFQualityAdviseLimits_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetMaximumDropMode)(T * This, MF_QUALITY_DROP_MODE *mode);                              \
    HRESULT(STDMETHODCALLTYPE *GetMinimumQualityLevel)(T * This, MF_QUALITY_LEVEL *level);

#define IMFClockStateSink_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnClockStart)(T * This, MFTIME system, LONGLONG offset);                               \
    HRESULT(STDMETHODCALLTYPE *OnClockStop)(T * This, MFTIME system);                                                  \
    HRESULT(STDMETHODCALLTYPE *OnClockPause)(T * This, MFTIME system);                                                 \
    HRESULT(STDMETHODCALLTYPE *OnClockRestart)(T * This, MFTIME system);                                               \
    HRESULT(STDMETHODCALLTYPE *OnClockSetRate)(T * This, MFTIME system, float rate);

#define IEVRTrustedVideoPlugin_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *IsInTrustedVideoMode)(T * This, BOOL *trusted);                                         \
    HRESULT(STDMETHODCALLTYPE *CanConstrict)(T * This, BOOL *can);                                                     \
    HRESULT(STDMETHODCALLTYPE *SetConstriction)(T * This, DWORD kpixels);                                              \
    HRESULT(STDMETHODCALLTYPE *DisableImageExport)(T * This, BOOL disable);

#define IMediaEventSink_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Notify)(T * This, LONG code, LONG_PTR param1, LONG_PTR param2);

#define IMFVideoPresenter_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnClockStart)(T * This, MFTIME system, LONGLONG offset);                               \
    HRESULT(STDMETHODCALLTYPE *OnClockStop)(T * This, MFTIME system);                                                  \
    HRESULT(STDMETHODCALLTYPE *OnClockPause)(T * This, MFTIME system);                                                 \
    HRESULT(STDMETHODCALLTYPE *OnClockRestart)(T * This, MFTIME system);                                               \
    HRESULT(STDMETHODCALLTYPE *OnClockSetRate)(T * This, MFTIME system, float rate);                                   \
    HRESULT(STDMETHODCALLTYPE *ProcessMessage)(T * This, MFVP_MESSAGE_TYPE message, ULONG_PTR param);                  \
    HRESULT(STDMETHODCALLTYPE *GetCurrentMediaType)(T * This, IUnknown **type);

#define IMFVideoDisplayControl_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetNativeVideoSize)(T * This, SIZE *video, SIZE *aspect);                               \
    HRESULT(STDMETHODCALLTYPE *GetIdealVideoSize)(T * This, SIZE *min, SIZE *max);                                     \
    HRESULT(STDMETHODCALLTYPE *SetVideoPosition)(T * This, const MFVideoNormalizedRect *source, const RECT *dest);    \
    HRESULT(STDMETHODCALLTYPE *GetVideoPosition)(T * This, MFVideoNormalizedRect *source, RECT *dest);                \
    HRESULT(STDMETHODCALLTYPE *SetAspectRatioMode)(T * This, DWORD mode);                                              \
    HRESULT(STDMETHODCALLTYPE *GetAspectRatioMode)(T * This, DWORD *mode);                                             \
    HRESULT(STDMETHODCALLTYPE *SetVideoWindow)(T * This, HWND window);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetVideoWindow)(T * This, HWND *window);                                                \
    HRESULT(STDMETHODCALLTYPE *RepaintVideo)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetCurrentImage)(T * This, BITMAPINFOHEADER *header, BYTE **dib, DWORD *size,          \
                                                LONGLONG *time);                                                       \
    HRESULT(STDMETHODCALLTYPE *SetBorderColor)(T * This, COLORREF color);                                              \
    HRESULT(STDMETHODCALLTYPE *GetBorderColor)(T * This, COLORREF *color);                                             \
    HRESULT(STDMETHODCALLTYPE *SetRenderingPrefs)(T * This, DWORD prefs);                                              \
    HRESULT(STDMETHODCALLTYPE *GetRenderingPrefs)(T * This, DWORD *prefs);                                             \
    HRESULT(STDMETHODCALLTYPE *SetFullscreen)(T * This, BOOL fullscreen);                                              \
    HRESULT(STDMETHODCALLTYPE *GetFullscreen)(T * This, BOOL *fullscreen);

#define IMFRateSupport_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetSlowestRate)(T * This, MFRATE_DIRECTION direction, BOOL thin, float *rate);          \
    HRESULT(STDMETHODCALLTYPE *GetFastestRate)(T * This, MFRATE_DIRECTION direction, BOOL thin, float *rate);          \
    HRESULT(STDMETHODCALLTYPE *IsRateSupported)(T * This, BOOL thin, float rate, float *nearest);

#define IQualProp_METHODS(T)                                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *get_FramesDroppedInRenderer)(T * This, int *frames);                                    \
    HRESULT(STDMETHODCALLTYPE *get_FramesDrawn)(T * This, int *frames);                                                \
    HRESULT(STDMETHODCALLTYPE *get_AvgFrameRate)(T * This, int *rate);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_Jitter)(T * This, int *jitter);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_AvgSyncOffset)(T * This, int *offset);                                              \
    HRESULT(STDMETHODCALLTYPE *get_DevSyncOffset)(T * This, int *offset);

#define IMFClock_METHODS(T)                                                                                            \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClockCharacteristics)(T * This, DWORD *characteristics);                             \
    HRESULT(STDMETHODCALLTYPE *GetCorrelatedTime)(T * This, DWORD reserved, LONGLONG *clock, MFTIME *system);          \
    HRESULT(STDMETHODCALLTYPE *GetContinuityKey)(T * This, DWORD *key);                                                \
    HRESULT(STDMETHODCALLTYPE *GetState)(T * This, DWORD reserved, MFCLOCK_STATE *state);                              \
    HRESULT(STDMETHODCALLTYPE *GetProperties)(T * This, MFCLOCK_PROPERTIES *properties);

MIME_COM_INTERFACE(IMFTransform)
MIME_COM_INTERFACE(IMFVideoDeviceID)
MIME_COM_INTERFACE(IMFTopologyServiceLookup)
MIME_COM_INTERFACE(IMFTopologyServiceLookupClient)
MIME_COM_INTERFACE(IMFVideoMixerControl)
MIME_COM_INTERFACE(IMFVideoMixerControl2)
MIME_COM_INTERFACE(IMFVideoPositionMapper)
MIME_COM_INTERFACE(IMFVideoProcessor)
MIME_COM_INTERFACE(IMFVideoMixerBitmap)
MIME_COM_INTERFACE(IMFQualityAdvise)
MIME_COM_INTERFACE(IMFQualityAdviseLimits)
MIME_COM_INTERFACE(IMFClockStateSink)
MIME_COM_INTERFACE(IEVRTrustedVideoPlugin)
MIME_COM_INTERFACE(IMediaEventSink)
MIME_COM_INTERFACE(IMFVideoPresenter)
MIME_COM_INTERFACE(IMFVideoDisplayControl)
MIME_COM_INTERFACE(IMFRateSupport)
MIME_COM_INTERFACE(IQualProp)
MIME_COM_INTERFACE(IMFClock)

MIME_DEFINE_GUID(IID_IMFTransform, 0xBF94C121, 0x5B05, 0x4E6F, 0x80, 0x00, 0xBA, 0x59, 0x89, 0x61, 0x41, 0x4D);
MIME_DEFINE_GUID(IID_IMFVideoDeviceID, 0xA38D9567, 0x5A9C, 0x4F3C, 0xB2, 0x93, 0x8E, 0xB4, 0x15, 0xB2, 0x79, 0xBA);
MIME_DEFINE_GUID(IID_IMFTopologyServiceLookup, 0xFA993889, 0x4383, 0x415A, 0xA9, 0x30, 0xDD, 0x47, 0x2A, 0x8C, 0xF6,
                 0xF7);
MIME_DEFINE_GUID(IID_IMFTopologyServiceLookupClient, 0xFA99388A, 0x4383, 0x415A, 0xA9, 0x30, 0xDD, 0x47, 0x2A, 0x8C,
                 0xF6, 0xF7);
MIME_DEFINE_GUID(IID_IMFVideoMixerControl, 0xA5C6C53F, 0xC202, 0x4AA5, 0x96, 0x95, 0x17, 0x5B, 0xA8, 0xC5, 0x08, 0xA5);
MIME_DEFINE_GUID(IID_IMFVideoMixerControl2, 0x8459616D, 0x966E, 0x4930, 0xB6, 0x58, 0x54, 0xFA, 0x7E, 0x5A, 0x16,
                 0xD3);
MIME_DEFINE_GUID(IID_IMFVideoPositionMapper, 0x1F6A9F17, 0xE70B, 0x4E24, 0x8A, 0xE4, 0x0B, 0x2C, 0x3B, 0xA7, 0xA4,
                 0xAE);
MIME_DEFINE_GUID(IID_IMFVideoProcessor, 0x6AB0000C, 0xFECE, 0x4D1F, 0xA2, 0xAC, 0xA9, 0x57, 0x35, 0x30, 0x65, 0x6E);
MIME_DEFINE_GUID(IID_IMFVideoMixerBitmap, 0x814C7B20, 0x0FDB, 0x4EEC, 0xAF, 0x8F, 0xF9, 0x57, 0xC8, 0xF6, 0x9E, 0xDC);
MIME_DEFINE_GUID(IID_IMFQualityAdvise, 0xEC15E2E9, 0xE36B, 0x4F7C, 0x87, 0x58, 0x77, 0xD4, 0x52, 0xEF, 0x4C, 0xE7);
MIME_DEFINE_GUID(IID_IMFQualityAdviseLimits, 0xDFCD8E4D, 0x30B5, 0x4567, 0xAC, 0xAA, 0x8E, 0xB5, 0xB7, 0x85, 0x3D,
                 0xC9);
MIME_DEFINE_GUID(IID_IMFClockStateSink, 0xF6696E82, 0x74F7, 0x4F3D, 0xA1, 0x78, 0x8A, 0x5E, 0x09, 0xC3, 0x65, 0x9F);
MIME_DEFINE_GUID(IID_IEVRTrustedVideoPlugin, 0x83A4CE40, 0x7710, 0x494B, 0xA8, 0x93, 0xA4, 0x72, 0x04, 0x9A, 0xF6,
                 0x30);
MIME_DEFINE_GUID(IID_IMediaEventSink, 0x56A868A2, 0x0AD4, 0x11CE, 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70);
MIME_DEFINE_GUID(IID_IMFVideoPresenter, 0x29AFF080, 0x182A, 0x4A5D, 0xAF, 0x3B, 0x44, 0x8F, 0x3A, 0x63, 0x46, 0xCB);
MIME_DEFINE_GUID(IID_IMFVideoDisplayControl, 0xA490B1E4, 0xAB84, 0x4D31, 0xA1, 0xB2, 0x18, 0x1E, 0x03, 0xB1, 0x07,
                 0x7A);
MIME_DEFINE_GUID(IID_IMFRateSupport, 0x0A9CCDBC, 0xD797, 0x4563, 0x96, 0x67, 0x94, 0xEC, 0x5D, 0x79, 0x29, 0x2D);
MIME_DEFINE_GUID(IID_IQualProp, 0x1BD0ECB0, 0xF8E2, 0x11CE, 0xAA, 0xC6, 0x00, 0x20, 0xAF, 0x0B, 0x99, 0xA3);
MIME_DEFINE_GUID(IID_IMFClock, 0x2EB1E945, 0x18B8, 0x4139, 0x9B, 0x1A, 0xD5, 0xD5, 0x84, 0x81, 0x85, 0x30);
MIME_DEFINE_GUID(IID_IMFGetService_, 0xFA993888, 0x4383, 0x415A, 0xA9, 0x30, 0xDD, 0x47, 0x2A, 0x8C, 0xF6, 0xF7);
MIME_DEFINE_GUID(IID_IMFDesiredSample_, 0x56C294D0, 0x753E, 0x4260, 0x8D, 0x61, 0xA3, 0xD8, 0x82, 0x0B, 0x1D, 0x54);
MIME_DEFINE_GUID(MF_SA_D3D_AWARE, 0xEAA35C29, 0x775E, 0x488E, 0x9B, 0x61, 0xB3, 0x28, 0x3E, 0x49, 0x58, 0x3B);
MIME_DEFINE_GUID(MF_SA_REQUIRED_SAMPLE_COUNT, 0x18802C61, 0x324B, 0x4952, 0xAB, 0xD0, 0x17, 0x6F, 0xF5, 0xC6, 0x96,
                 0xFF);
MIME_DEFINE_GUID(VIDEO_ZOOM_RECT, 0x7AAA1638, 0x1B7F, 0x4C93, 0xBD, 0x89, 0x5B, 0x9C, 0x9F, 0xB6, 0xFC, 0xF0);
MIME_DEFINE_GUID(MFSampleExtension_BottomFieldFirst, 0x941CE0A3, 0x6AE3, 0x4DDA, 0x9A, 0x08, 0xA6, 0x42, 0x98, 0x34,
                 0x06, 0x17);
MIME_DEFINE_GUID(MFSampleExtension_RepeatFirstField, 0x304D257C, 0x7493, 0x4FBD, 0xB1, 0x49, 0x92, 0x28, 0xDE, 0x8D,
                 0x9A, 0x99);
MIME_DEFINE_GUID(MFSampleExtension_Interlaced, 0xB1D5830A, 0xDEB8, 0x40E3, 0x90, 0xFA, 0x38, 0x99, 0x43, 0x71, 0x64,
                 0x61);

#ifndef MF_E_TRANSFORM_STREAM_CHANGE
#define MF_E_TRANSFORM_STREAM_CHANGE ((HRESULT)0xC00D6D61)
#endif
#define MF_E_TRANSFORM_INPUT_REMAINING ((HRESULT)0xC00D6D62)
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72)
#define MF_E_DROPTIME_NOT_SUPPORTED_ ((HRESULT)0xC00DA028)
#define MF_E_QUALITYLEVEL_NOT_SUPPORTED_ ((HRESULT)0xC00DA029)
#define MF_E_NO_CLOCK_ ((HRESULT)0xC00D9D21)
#define MF_E_SHUTDOWN_ ((HRESULT)0xC00D3E85)
#define EC_SAMPLE_NEEDED 0x20

#endif
