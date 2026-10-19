/*
 * the capture engine of media foundation: a source of video from a camera, the sinks it feeds and the events it
 * reports. methods are in windows' vtable order.
 */
#ifndef MIME_WIN32_MFCAPTUREENGINE_H
#define MIME_WIN32_MFCAPTUREENGINE_H

#include <mime/win32/mfidl.h>

typedef struct IMFMediaSink IMFMediaSink;
typedef struct IMFCaptureEngineOnEventCallback IMFCaptureEngineOnEventCallback;
typedef struct IMFCaptureEngineOnSampleCallback IMFCaptureEngineOnSampleCallback;
typedef struct IMFCaptureSource IMFCaptureSource;
typedef struct IMFCaptureSink IMFCaptureSink;
typedef struct IMFCapturePreviewSink IMFCapturePreviewSink;
typedef struct IMFCaptureRecordSink IMFCaptureRecordSink;
typedef struct IMFCapturePhotoSink IMFCapturePhotoSink;
typedef struct IMFCaptureEngine IMFCaptureEngine;
typedef struct IMFCaptureEngineClassFactory IMFCaptureEngineClassFactory;

typedef struct MFVideoNormalizedRect {
    float left, top, right, bottom;
} MFVideoNormalizedRect;

typedef enum MF_CAPTURE_ENGINE_DEVICE_TYPE {
    MF_CAPTURE_ENGINE_DEVICE_TYPE_AUDIO = 0,
    MF_CAPTURE_ENGINE_DEVICE_TYPE_VIDEO = 1,
} MF_CAPTURE_ENGINE_DEVICE_TYPE;

typedef enum MF_CAPTURE_ENGINE_SINK_TYPE {
    MF_CAPTURE_ENGINE_SINK_TYPE_RECORD = 0,
    MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW = 1,
    MF_CAPTURE_ENGINE_SINK_TYPE_PHOTO = 2,
} MF_CAPTURE_ENGINE_SINK_TYPE;

typedef enum MF_CAPTURE_ENGINE_STREAM_CATEGORY {
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW = 0,
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_CAPTURE = 1,
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_PHOTO_INDEPENDENT = 2,
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_PHOTO_DEPENDENT = 3,
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_AUDIO = 4,
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_UNSUPPORTED = 5,
    MF_CAPTURE_ENGINE_STREAM_CATEGORY_METADATA = 6,
} MF_CAPTURE_ENGINE_STREAM_CATEGORY;

/* the stream index that means the media source itself, for the services of a source. */
#define MF_CAPTURE_ENGINE_MEDIASOURCE 0xFFFFFFFFu
#define MF_CAPTURE_ENGINE_FIRST_SOURCE_INDEX 0xFFFFFFFCu
#define MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_RECORD 0xFFFFFFFAu
#define MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_PREVIEW 0xFFFFFFFBu
#define MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_PHOTO 0xFFFFFFF9u
#define MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_AUDIO 0xFFFFFFF8u
#define MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_METADATA 0xFFFFFFF7u

#define IMFCaptureEngineOnEventCallback_METHODS(T)                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnEvent)(T * This, IMFMediaEvent *event);

#define IMFCaptureEngineOnSampleCallback_METHODS(T)                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnSample)(T * This, IMFSample *sample);

#define IMFCaptureSink_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetOutputMediaType)(T * This, DWORD sink_stream, IMFMediaType **type);                  \
    HRESULT(STDMETHODCALLTYPE *GetService)(T * This, DWORD sink_stream, REFGUID service, REFIID riid,                  \
                                           IUnknown **object);                                                         \
    HRESULT(STDMETHODCALLTYPE *AddStream)(T * This, DWORD source_stream, IMFMediaType *type,                           \
                                          IMFAttributes *attributes, DWORD *sink_stream);                              \
    HRESULT(STDMETHODCALLTYPE *Prepare)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *RemoveAllStreams)(T * This);

#define IMFCaptureRecordSink_METHODS(T)                                                                                \
    IMFCaptureSink_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetOutputByteStream)(T * This, IMFByteStream *stream, REFGUID container);               \
    HRESULT(STDMETHODCALLTYPE *SetOutputFileName)(T * This, LPCWSTR name);                                             \
    HRESULT(STDMETHODCALLTYPE *SetSampleCallback)(T * This, DWORD sink_stream,                                         \
                                                  IMFCaptureEngineOnSampleCallback *callback);                         \
    HRESULT(STDMETHODCALLTYPE *SetCustomSink)(T * This, IMFMediaSink *sink);                                           \
    HRESULT(STDMETHODCALLTYPE *GetRotation)(T * This, DWORD stream, DWORD *rotation);                                  \
    HRESULT(STDMETHODCALLTYPE *SetRotation)(T * This, DWORD stream, DWORD rotation);

#define IMFCapturePreviewSink_METHODS(T)                                                                               \
    IMFCaptureSink_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetRenderHandle)(T * This, HANDLE handle);                                              \
    HRESULT(STDMETHODCALLTYPE *SetRenderSurface)(T * This, IUnknown *surface);                                         \
    HRESULT(STDMETHODCALLTYPE *UpdateVideo)(T * This, const MFVideoNormalizedRect *source, const RECT *destination,    \
                                            const COLORREF *border);                                                   \
    HRESULT(STDMETHODCALLTYPE *SetSampleCallback)(T * This, DWORD sink_stream,                                         \
                                                  IMFCaptureEngineOnSampleCallback *callback);                         \
    HRESULT(STDMETHODCALLTYPE *GetMirrorState)(T * This, BOOL *mirror);                                                \
    HRESULT(STDMETHODCALLTYPE *SetMirrorState)(T * This, BOOL mirror);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetRotation)(T * This, DWORD stream, DWORD *rotation);                                  \
    HRESULT(STDMETHODCALLTYPE *SetRotation)(T * This, DWORD stream, DWORD rotation);                                   \
    HRESULT(STDMETHODCALLTYPE *SetCustomSink)(T * This, IMFMediaSink *sink);

#define IMFCapturePhotoSink_METHODS(T)                                                                                 \
    IMFCaptureSink_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetOutputFileName)(T * This, LPCWSTR name);                                             \
    HRESULT(STDMETHODCALLTYPE *SetSampleCallback)(T * This, IMFCaptureEngineOnSampleCallback *callback);               \
    HRESULT(STDMETHODCALLTYPE *SetOutputByteStream)(T * This, IMFByteStream *stream);

#define IMFCaptureSource_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCaptureDeviceSource)(T * This, MF_CAPTURE_ENGINE_DEVICE_TYPE type,                   \
                                                       IMFMediaSource **source);                                       \
    HRESULT(STDMETHODCALLTYPE *GetCaptureDeviceActivate)(T * This, MF_CAPTURE_ENGINE_DEVICE_TYPE type,                 \
                                                         IMFActivate **activate);                                      \
    HRESULT(STDMETHODCALLTYPE *GetService)(T * This, REFGUID service, REFIID riid, IUnknown **object);                 \
    HRESULT(STDMETHODCALLTYPE *AddEffect)(T * This, DWORD source_stream, IUnknown *effect);                            \
    HRESULT(STDMETHODCALLTYPE *RemoveEffect)(T * This, DWORD source_stream, IUnknown *effect);                         \
    HRESULT(STDMETHODCALLTYPE *RemoveAllEffects)(T * This, DWORD source_stream);                                       \
    HRESULT(STDMETHODCALLTYPE *GetAvailableDeviceMediaType)(T * This, DWORD source_stream, DWORD index,                \
                                                            IMFMediaType **type);                                      \
    HRESULT(STDMETHODCALLTYPE *SetCurrentDeviceMediaType)(T * This, DWORD source_stream, IMFMediaType *type);          \
    HRESULT(STDMETHODCALLTYPE *GetCurrentDeviceMediaType)(T * This, DWORD source_stream, IMFMediaType **type);         \
    HRESULT(STDMETHODCALLTYPE *GetDeviceStreamCount)(T * This, DWORD *count);                                          \
    HRESULT(STDMETHODCALLTYPE *GetDeviceStreamCategory)(T * This, DWORD source_stream,                                 \
                                                        MF_CAPTURE_ENGINE_STREAM_CATEGORY *category);                  \
    HRESULT(STDMETHODCALLTYPE *GetMirrorState)(T * This, DWORD stream, BOOL *mirror);                                  \
    HRESULT(STDMETHODCALLTYPE *SetMirrorState)(T * This, DWORD stream, BOOL mirror);                                   \
    HRESULT(STDMETHODCALLTYPE *GetStreamIndexFromFriendlyName)(T * This, UINT32 friendly_name, DWORD *stream);

#define IMFCaptureEngine_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, IMFCaptureEngineOnEventCallback *callback,                        \
                                           IMFAttributes *attributes, IUnknown *audio_source,                          \
                                           IUnknown *video_source);                                                    \
    HRESULT(STDMETHODCALLTYPE *StartPreview)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *StopPreview)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *StartRecord)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *StopRecord)(T * This, BOOL finalize, BOOL flush_unprocessed);                           \
    HRESULT(STDMETHODCALLTYPE *TakePhoto)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetSink)(T * This, MF_CAPTURE_ENGINE_SINK_TYPE type, IMFCaptureSink **sink);            \
    HRESULT(STDMETHODCALLTYPE *GetSource)(T * This, IMFCaptureSource **source);

#define IMFCaptureEngineClassFactory_METHODS(T)                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateInstance)(T * This, REFCLSID clsid, REFIID riid, void **object);

MIME_COM_INTERFACE(IMFCaptureEngineOnEventCallback)
MIME_COM_INTERFACE(IMFCaptureEngineOnSampleCallback)
MIME_COM_INTERFACE(IMFCaptureSink)
MIME_COM_INTERFACE(IMFCaptureRecordSink)
MIME_COM_INTERFACE(IMFCapturePreviewSink)
MIME_COM_INTERFACE(IMFCapturePhotoSink)
MIME_COM_INTERFACE(IMFCaptureSource)
MIME_COM_INTERFACE(IMFCaptureEngine)
MIME_COM_INTERFACE(IMFCaptureEngineClassFactory)

MIME_DEFINE_GUID(IID_IMFCaptureEngineOnEventCallback, 0xAEDA51C0, 0x9025, 0x4983, 0x90, 0x12, 0xDE, 0x59, 0x7B, 0x88,
                 0xB0, 0x89);
MIME_DEFINE_GUID(IID_IMFCaptureEngineOnSampleCallback, 0x52150B82, 0xAB39, 0x4467, 0x98, 0x0F, 0xE4, 0x8B, 0xF0, 0x82,
                 0x2E, 0xCD);
MIME_DEFINE_GUID(IID_IMFCaptureSink, 0x72D6135B, 0x35E9, 0x412C, 0xB9, 0x26, 0xFD, 0x52, 0x65, 0xF2, 0xA8, 0x85);
MIME_DEFINE_GUID(IID_IMFCaptureRecordSink, 0x3323B55A, 0xF92A, 0x4FE2, 0x8E, 0xDC, 0xE9, 0xBF, 0xC0, 0x63, 0x4D, 0x77);
MIME_DEFINE_GUID(IID_IMFCapturePreviewSink, 0x77346CFD, 0x5B49, 0x4D73, 0xAC, 0xE0, 0x5B, 0x52, 0xA8, 0x59, 0xF2, 0xE0);
MIME_DEFINE_GUID(IID_IMFCapturePhotoSink, 0xD2D43CC8, 0x48BB, 0x4AA7, 0x95, 0xDB, 0x10, 0xC0, 0x69, 0x77, 0xE7, 0x77);
MIME_DEFINE_GUID(IID_IMFCaptureSource, 0x439A42A8, 0x0D2C, 0x4505, 0xBE, 0x83, 0xF7, 0x9B, 0x2A, 0x05, 0xD5, 0xC4);
MIME_DEFINE_GUID(IID_IMFCaptureEngine, 0xA6BBA433, 0x176B, 0x48B2, 0xB3, 0x75, 0x53, 0xAA, 0x03, 0x47, 0x32, 0x07);
MIME_DEFINE_GUID(IID_IMFCaptureEngineClassFactory, 0x8F02D140, 0x56FC, 0x4302, 0xA7, 0x05, 0x3A, 0x97, 0xC7, 0x8B,
                 0xE7, 0x79);

/* the class factory and the engine have one class id. */
MIME_DEFINE_GUID(CLSID_MFCaptureEngine, 0xEFCE38D3, 0x8914, 0x4674, 0xA7, 0xDF, 0xAE, 0x1B, 0x3D, 0x65, 0x4B, 0x8A);
MIME_DEFINE_GUID(CLSID_MFCaptureEngineClassFactory, 0xEFCE38D3, 0x8914, 0x4674, 0xA7, 0xDF, 0xAE, 0x1B, 0x3D, 0x65, 0x4B,
                 0x8A);

/* the events, their extended types. */
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_INITIALIZED, 0x219992BC, 0xCF92, 0x4531, 0xA1, 0xAE, 0x96, 0xE1, 0xE8, 0x86, 0xC8,
                 0xF1);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_PREVIEW_STARTED, 0xA416DF21, 0xF9D3, 0x4A74, 0x99, 0x1B, 0xB8, 0x17, 0x29, 0x89, 0x52,
                 0xC4);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_PREVIEW_STOPPED, 0x13D5143C, 0x1EDD, 0x4E50, 0xA2, 0xEF, 0x35, 0x0A, 0x47, 0x67, 0x80,
                 0x60);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_RECORD_STARTED, 0xAC2B027B, 0xDDF9, 0x48A0, 0x89, 0xBE, 0x38, 0xAB, 0x35, 0xEF, 0x45,
                 0xC0);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_RECORD_STOPPED, 0x55E5200A, 0xF98F, 0x4C0D, 0xA9, 0xEC, 0x9E, 0xB2, 0x5E, 0xD3, 0xD7,
                 0x73);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_PHOTO_TAKEN, 0x3C50C445, 0x7304, 0x48EB, 0x86, 0x5D, 0xBB, 0xA1, 0x9B, 0xA3, 0xAF,
                 0x5C);
MIME_DEFINE_GUID(MF_CAPTURE_SOURCE_CURRENT_DEVICE_MEDIA_TYPE_SET, 0xE7E75E4C, 0x039C, 0x4410, 0x81, 0x5B, 0x87, 0x41,
                 0x30, 0x7B, 0x63, 0xAA);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_ERROR, 0x46B89FC6, 0x33CC, 0x4399, 0x9D, 0xAD, 0x78, 0x4D, 0xE7, 0x7D, 0x58, 0x7C);
MIME_DEFINE_GUID(MF_CAPTURE_SINK_PREPARED, 0x7BFCE257, 0x12B1, 0x4409, 0x8C, 0x34, 0xD4, 0x45, 0xDA, 0xAB, 0x75, 0x78);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_OUTPUT_MEDIA_TYPE_SET, 0xCAAAD994, 0x83EC, 0x45E9, 0xA3, 0x0A, 0x1F, 0x20, 0xAA, 0xDB,
                 0x98, 0x31);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_ALL_EFFECTS_REMOVED, 0xFDED7521, 0x8ED8, 0x431A, 0xA9, 0x6B, 0xF3, 0xE2, 0x56, 0x5E,
                 0x98, 0x1C);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_EFFECT_ADDED, 0xAA8DC7B5, 0xA048, 0x4E13, 0x8E, 0xBE, 0xF2, 0x3C, 0x46, 0xC8, 0x30,
                 0xC1);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_EFFECT_REMOVED, 0xC6E8DB07, 0xFB09, 0x4A48, 0x89, 0xC6, 0xBF, 0x92, 0xA0, 0x42, 0x22,
                 0xC9);

/* the attributes the engine is initialized with. */
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_D3D_MANAGER, 0x76E25E7B, 0xD595, 0x4283, 0x96, 0x2C, 0xC5, 0x94, 0xAF, 0xD7, 0x8D,
                 0xDF);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_USE_AUDIO_DEVICE_ONLY, 0x1C8077DA, 0x8466, 0x4DC4, 0x8B, 0x8E, 0x27, 0x6B, 0x3F, 0x85,
                 0x92, 0x3B);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_USE_VIDEO_DEVICE_ONLY, 0x7E025171, 0xCF32, 0x4F2E, 0x8F, 0x19, 0x41, 0x05, 0x77, 0xB7,
                 0x3A, 0x66);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_DISABLE_HARDWARE_TRANSFORMS, 0xB7C42A6B, 0x3207, 0x4495, 0xB4, 0xE7, 0x81, 0xF9, 0xC3,
                 0x5D, 0x59, 0x91);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_DISABLE_DXVA, 0xF9818862, 0x179D, 0x433F, 0xA3, 0x2F, 0x74, 0xCB, 0xCF, 0x74, 0x46,
                 0x6D);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_EVENT_GENERATOR_GUID, 0xABFA8AD5, 0xFC6D, 0x4911, 0x87, 0xE0, 0x96, 0x19, 0x45, 0xF8,
                 0xF7, 0xCE);
MIME_DEFINE_GUID(MF_CAPTURE_ENGINE_EVENT_STREAM_INDEX, 0x82697F44, 0xB1CF, 0x42EB, 0x97, 0x53, 0xF8, 0x6D, 0x64, 0x9C,
                 0x88, 0x65);

#ifdef MIME_BUILD_MFCAPTUREENGINE
#define MFCEAPI
#else
#define MFCEAPI DECLSPEC_IMPORT
#endif
MFCEAPI HRESULT WINAPI MFCreateCaptureEngine(IMFCaptureEngine **engine);

#endif
