/* mfreadwrite.dll: the source reader. */
#ifndef MIME_WIN32_MFREADWRITE_H
#define MIME_WIN32_MFREADWRITE_H

#include <mime/win32/mfidl.h>

#ifdef MIME_BUILD_MFREADWRITE
#define MFREADWRITEAPI
#else
#define MFREADWRITEAPI DECLSPEC_IMPORT
#endif

#define MF_SOURCE_READER_INVALID_STREAM_INDEX 0xFFFFFFFF
#define MF_SOURCE_READER_ALL_STREAMS 0xFFFFFFFE
#define MF_SOURCE_READER_ANY_STREAM 0xFFFFFFFE
#define MF_SOURCE_READER_FIRST_AUDIO_STREAM 0xFFFFFFFD
#define MF_SOURCE_READER_FIRST_VIDEO_STREAM 0xFFFFFFFC
#define MF_SOURCE_READER_MEDIASOURCE 0xFFFFFFFF

#define MF_SOURCE_READER_CONTROLF_DRAIN 0x1

#define MF_SOURCE_READERF_ERROR 0x1
#define MF_SOURCE_READERF_ENDOFSTREAM 0x2
#define MF_SOURCE_READERF_NEWSTREAM 0x4
#define MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED 0x10
#define MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED 0x20
#define MF_SOURCE_READERF_STREAMTICK 0x100
#define MF_SOURCE_READERF_ALLEFFECTSREMOVED 0x200

typedef struct IMFSourceReader IMFSourceReader;
typedef struct IMFSourceReaderCallback IMFSourceReaderCallback;

#define IMFSourceReader_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetStreamSelection)(T * This, DWORD index, BOOL *selected);                             \
    HRESULT(STDMETHODCALLTYPE *SetStreamSelection)(T * This, DWORD index, BOOL selected);                              \
    HRESULT(STDMETHODCALLTYPE *GetNativeMediaType)(T * This, DWORD index, DWORD type_index, IMFMediaType **type);      \
    HRESULT(STDMETHODCALLTYPE *GetCurrentMediaType)(T * This, DWORD index, IMFMediaType **type);                       \
    HRESULT(STDMETHODCALLTYPE *SetCurrentMediaType)(T * This, DWORD index, DWORD *reserved, IMFMediaType *type);       \
    HRESULT(STDMETHODCALLTYPE *SetCurrentPosition)(T * This, REFGUID format, const PROPVARIANT *position);             \
    HRESULT(STDMETHODCALLTYPE *ReadSample)(T * This, DWORD index, DWORD control, DWORD *actual_index, DWORD *flags,    \
                                           LONGLONG *timestamp, IMFSample **sample);                                   \
    HRESULT(STDMETHODCALLTYPE *Flush)(T * This, DWORD index);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetServiceForStream)(T * This, DWORD index, REFGUID service, REFIID riid,               \
                                                    void **object);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetPresentationAttribute)(T * This, DWORD index, REFGUID attribute,                     \
                                                         PROPVARIANT *value);

#define IMFSourceReaderCallback_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnReadSample)(T * This, HRESULT status, DWORD index, DWORD flags, LONGLONG timestamp,   \
                                             IMFSample *sample);                                                       \
    HRESULT(STDMETHODCALLTYPE *OnFlush)(T * This, DWORD index);                                                        \
    HRESULT(STDMETHODCALLTYPE *OnEvent)(T * This, DWORD index, IMFMediaEvent *event);

MIME_COM_INTERFACE(IMFSourceReader)
MIME_COM_INTERFACE(IMFSourceReaderCallback)

MIME_DEFINE_GUID(IID_IMFSourceReader, 0x70AE66F2, 0xC809, 0x4E4F, 0x89, 0x15, 0xBD, 0xCB, 0x40, 0x6B, 0x79, 0x93);
MIME_DEFINE_GUID(IID_IMFSourceReaderCallback, 0xDEEC8D99, 0xFA1D, 0x4D82, 0x84, 0xC2, 0x2C, 0x89, 0x69, 0x94, 0x48,
                 0x67);
MIME_DEFINE_GUID(MF_SOURCE_READER_ASYNC_CALLBACK, 0x1E3DBEAC, 0xBB43, 0x4C35, 0xB5, 0x07, 0xCD, 0x64, 0x44, 0x64, 0xC9,
                 0x65);
MIME_DEFINE_GUID(MF_SOURCE_READER_D3D_MANAGER, 0xEC822DA2, 0xE1E9, 0x4B29, 0xA0, 0xD8, 0x56, 0x3C, 0x71, 0x9F, 0x52,
                 0x69);
MIME_DEFINE_GUID(MF_SOURCE_READER_DISABLE_DXVA, 0xAA456CFD, 0x3943, 0x4A1E, 0xA7, 0x7D, 0x18, 0x38, 0xC0, 0xEA, 0x2E,
                 0x35);
MIME_DEFINE_GUID(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 0xFB394F3D, 0xCCF1, 0x42EE, 0xBB, 0xB3, 0xF9, 0xB8, 0x45,
                 0xD5, 0x68, 0x1D);
MIME_DEFINE_GUID(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, 0x0F81DA2C, 0xB537, 0x4672, 0xA8, 0xB2, 0xA6, 0x81,
                 0xB1, 0x73, 0x07, 0xA3);
MIME_DEFINE_GUID(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, 0x56B67165, 0x219E, 0x456D, 0xA2, 0x2E, 0x2D,
                 0x30, 0x04, 0xC7, 0xFE, 0x56);
MIME_DEFINE_GUID(MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS, 0x6D23F5C8, 0xC5D7, 0x4A9B, 0x99, 0x71, 0x5D, 0x11,
                 0xF8, 0xBC, 0xA8, 0x80);
MIME_DEFINE_GUID(MF_READWRITE_DISABLE_CONVERTERS, 0x98D5B065, 0x1374, 0x4847, 0x8D, 0x5D, 0x31, 0x52, 0x0F, 0xEE, 0x71,
                 0x56);

MFREADWRITEAPI HRESULT WINAPI MFCreateSourceReaderFromMediaSource(IMFMediaSource *source, IMFAttributes *attributes,
                                                                  IMFSourceReader **reader);

#endif
