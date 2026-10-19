/*
 * media foundation's pipeline objects: presentation and stream
 * descriptors, media type handlers, media sources and their streams, and
 * the capture devices of mf.dll.
 */
#ifndef MIME_WIN32_MFIDL_H
#define MIME_WIN32_MFIDL_H

#include <mime/win32/mfobjects.h>

#ifdef MIME_BUILD_MF
#define MFIDLAPI
#else
#define MFIDLAPI DECLSPEC_IMPORT
#endif

typedef struct IMFMediaTypeHandler IMFMediaTypeHandler;
typedef struct IMFStreamDescriptor IMFStreamDescriptor;
typedef struct IMFPresentationDescriptor IMFPresentationDescriptor;
typedef struct IMFMediaSource IMFMediaSource;
typedef struct IMFMediaStream IMFMediaStream;
typedef struct IMFGetService IMFGetService;
typedef struct IMFMediaSourceEx IMFMediaSourceEx;

#define IMFMediaTypeHandler_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *IsMediaTypeSupported)(T * This, IMFMediaType *type, IMFMediaType **closest);            \
    HRESULT(STDMETHODCALLTYPE *GetMediaTypeCount)(T * This, DWORD *count);                                             \
    HRESULT(STDMETHODCALLTYPE *GetMediaTypeByIndex)(T * This, DWORD index, IMFMediaType **type);                       \
    HRESULT(STDMETHODCALLTYPE *SetCurrentMediaType)(T * This, IMFMediaType *type);                                     \
    HRESULT(STDMETHODCALLTYPE *GetCurrentMediaType)(T * This, IMFMediaType **type);                                    \
    HRESULT(STDMETHODCALLTYPE *GetMajorType)(T * This, GUID *major);

#define IMFStreamDescriptor_METHODS(T)                                                                                 \
    IMFAttributes_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetStreamIdentifier)(T * This, DWORD *identifier);                                      \
    HRESULT(STDMETHODCALLTYPE *GetMediaTypeHandler)(T * This, IMFMediaTypeHandler **handler);

#define IMFPresentationDescriptor_METHODS(T)                                                                           \
    IMFAttributes_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetStreamDescriptorCount)(T * This, DWORD *count);                                      \
    HRESULT(STDMETHODCALLTYPE *GetStreamDescriptorByIndex)(T * This, DWORD index, BOOL *selected,                      \
                                                           IMFStreamDescriptor **descriptor);                          \
    HRESULT(STDMETHODCALLTYPE *SelectStream)(T * This, DWORD index);                                                   \
    HRESULT(STDMETHODCALLTYPE *DeselectStream)(T * This, DWORD index);                                                 \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IMFPresentationDescriptor **copy);

#define MFMEDIASOURCE_IS_LIVE 0x1
#define MFMEDIASOURCE_CAN_SEEK 0x2
#define MFMEDIASOURCE_CAN_PAUSE 0x4
#define MFMEDIASOURCE_HAS_SLOW_SEEK 0x8

#define IMFMediaSource_METHODS(T)                                                                                      \
    IMFMediaEventGenerator_METHODS(T)                                                                                  \
    HRESULT(STDMETHODCALLTYPE *GetCharacteristics)(T * This, DWORD *characteristics);                                  \
    HRESULT(STDMETHODCALLTYPE *CreatePresentationDescriptor)(T * This, IMFPresentationDescriptor **descriptor);        \
    HRESULT(STDMETHODCALLTYPE *Start)(T * This, IMFPresentationDescriptor *descriptor, const GUID *time_format,       \
                                      const PROPVARIANT *start);                                                       \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *Pause)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Shutdown)(T * This);

#define IMFMediaStream_METHODS(T)                                                                                      \
    IMFMediaEventGenerator_METHODS(T)                                                                                  \
    HRESULT(STDMETHODCALLTYPE *GetMediaSource)(T * This, IMFMediaSource **source);                                     \
    HRESULT(STDMETHODCALLTYPE *GetStreamDescriptor)(T * This, IMFStreamDescriptor **descriptor);                       \
    HRESULT(STDMETHODCALLTYPE *RequestSample)(T * This, IUnknown *token);

#define IMFMediaSourceEx_METHODS(T)                                                                                    \
    IMFMediaSource_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *GetSourceAttributes)(T * This, IMFAttributes **attributes);                             \
    HRESULT(STDMETHODCALLTYPE *GetStreamAttributes)(T * This, DWORD stream, IMFAttributes **attributes);               \
    HRESULT(STDMETHODCALLTYPE *SetD3DManager)(T * This, IUnknown *manager);

#define IMFGetService_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetService)(T * This, REFGUID service, REFIID riid, void **object);

MIME_COM_INTERFACE(IMFMediaTypeHandler)
MIME_COM_INTERFACE(IMFStreamDescriptor)
MIME_COM_INTERFACE(IMFPresentationDescriptor)
MIME_COM_INTERFACE(IMFMediaSource)
MIME_COM_INTERFACE(IMFMediaStream)
MIME_COM_INTERFACE(IMFMediaSourceEx)
MIME_COM_INTERFACE(IMFGetService)

MIME_DEFINE_GUID(IID_IMFMediaTypeHandler, 0xE93DCF6C, 0x4B07, 0x4E1E, 0x81, 0x23, 0xAA, 0x16, 0xED, 0x6E, 0xAD, 0xF5);
MIME_DEFINE_GUID(IID_IMFStreamDescriptor, 0x56C03D9C, 0x9DBB, 0x45F5, 0xAB, 0x4B, 0xD8, 0x0F, 0x47, 0xC0, 0x59, 0x38);
MIME_DEFINE_GUID(IID_IMFPresentationDescriptor, 0x03CB2711, 0x24D7, 0x4DB6, 0xA1, 0x7F, 0xF3, 0xA7, 0xA4, 0x79, 0xA5,
                 0x36);
MIME_DEFINE_GUID(IID_IMFMediaSource, 0x279A808D, 0xAEC7, 0x40C8, 0x9C, 0x6B, 0xA6, 0xB4, 0x92, 0xC7, 0x8A, 0x66);
MIME_DEFINE_GUID(IID_IMFMediaStream, 0xD182108F, 0x4EC6, 0x443F, 0xAA, 0x42, 0xA7, 0x11, 0x06, 0xEC, 0x82, 0x5F);
MIME_DEFINE_GUID(IID_IMFMediaSourceEx, 0x3C9B2EB9, 0x86D5, 0x4514, 0xA3, 0x94, 0xF5, 0x66, 0x64, 0xF9, 0xF0, 0xD8);
MIME_DEFINE_GUID(IID_IMFGetService, 0xFA993888, 0x4383, 0x415A, 0xA9, 0x30, 0xDD, 0x47, 0x2A, 0x8C, 0xF6, 0xF7);

MIME_DEFINE_GUID(MF_PD_DURATION, 0x6C990D33, 0xBB8E, 0x477A, 0x85, 0x98, 0x0D, 0x5D, 0x96, 0xFC, 0xD8, 0x8A);
MIME_DEFINE_GUID(MF_SD_LANGUAGE, 0x00AF2180, 0xBDC2, 0x423C, 0xAB, 0xCA, 0xF5, 0x03, 0x59, 0x3B, 0xC1, 0x21);
MIME_DEFINE_GUID(MF_SD_PROTECTED, 0x00AF2181, 0xBDC2, 0x423C, 0xAB, 0xCA, 0xF5, 0x03, 0x59, 0x3B, 0xC1, 0x21);
MIME_DEFINE_GUID(MF_EVENT_SOURCE_ACTUAL_START, 0xA8CC55A9, 0x6B31, 0x419F, 0x84, 0x5D, 0xFF, 0xB3, 0x51, 0xA2, 0x43,
                 0x4B);

/* the capture devices of mf.dll. */
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 0xC60AC5FE, 0x252A, 0x478F, 0xA0, 0xEF, 0xBC, 0x8F, 0xA5, 0xF7,
                 0xCA, 0xD3);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID, 0x8AC3587A, 0x4AE7, 0x42D8, 0x99, 0xE0, 0x0A, 0x60,
                 0x13, 0xEE, 0xF9, 0x0F);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID, 0x14DD9A1C, 0x7CFF, 0x41BE, 0xB1, 0xB9, 0xBA, 0x1A,
                 0xC6, 0xEC, 0xB5, 0x71);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, 0x58F0AAD8, 0x22BF, 0x4F8A, 0xBB, 0x3D, 0xD2,
                 0xC4, 0x97, 0x8C, 0x6E, 0x2F);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, 0x30DA9258, 0xFEB9, 0x47A7, 0xA4, 0x53, 0x76,
                 0x3A, 0x7A, 0x8E, 0x1C, 0x5F);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, 0x77F0AE69, 0xC3BD, 0x4509, 0x94, 0x1D, 0x46,
                 0x7E, 0x4D, 0x24, 0x89, 0x9E);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_MEDIA_TYPE, 0x56A819CA, 0x0C78, 0x4DE4, 0xA0, 0xA7, 0x3D, 0xDA, 0xBA, 0x0F,
                 0x24, 0xD4);
MIME_DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, 0x60D0E559, 0x52F8, 0x4FA2, 0xBB, 0xCE, 0xAC, 0xDB, 0x34, 0xA8,
                 0xEC, 0x01);

MFIDLAPI HRESULT WINAPI MFEnumDeviceSources(IMFAttributes *attributes, IMFActivate ***sources, UINT32 *count);
MFIDLAPI HRESULT WINAPI MFCreateDeviceSource(IMFAttributes *attributes, IMFMediaSource **source);
MFIDLAPI HRESULT WINAPI MFCreateDeviceSourceActivate(IMFAttributes *attributes, IMFActivate **activate);
MFIDLAPI HRESULT WINAPI MFGetService(IUnknown *object, REFGUID service, REFIID riid, void **result);
MFIDLAPI HRESULT WINAPI MFShutdownObject(IUnknown *object);

#endif
