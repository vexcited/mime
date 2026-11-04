#ifndef MIME_WIN32_WINRT_STREAMS_H
#define MIME_WIN32_WINRT_STREAMS_H

#include <mime/win32/winrt_async.h>

MIME_DEFINE_GUID(IID_IBuffer, 0x905A0FE0, 0xBC53, 0x11DF, 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA);
MIME_DEFINE_GUID(IID_IRandomAccessStream, 0x905A0FE1, 0xBC53, 0x11DF, 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA);
MIME_DEFINE_GUID(IID_IInputStream, 0x905A0FE2, 0xBC53, 0x11DF, 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA);
MIME_DEFINE_GUID(IID_IOutputStream, 0x905A0FE6, 0xBC53, 0x11DF, 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA);
MIME_DEFINE_GUID(IID_IBufferByteAccess, 0x905A0FEF, 0xBC53, 0x11DF, 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA);
/* IAsyncOperationWithProgress<IBuffer, UInt32>, what ReadAsync gives. */
MIME_DEFINE_GUID(IID_IAsyncOperationWithProgressBuffer, 0xD26B2819, 0x897F, 0x5C7D, 0x84, 0xD6, 0x56, 0xD7, 0x96,
                 0x56, 0x14, 0x31);
/* IAsyncOperationWithProgress<UInt32, UInt32>, what WriteAsync gives. */
MIME_DEFINE_GUID(IID_IAsyncOperationWithProgressUInt32, 0xECCB574A, 0xC684, 0x5572, 0xA6, 0x79, 0x6B, 0x08, 0x42,
                 0xCF, 0xB5, 0x7F);
/* IAsyncOperation<IRandomAccessStream>. */
MIME_DEFINE_GUID(IID_IAsyncOperationRandomAccessStream, 0x430ECECE, 0x1418, 0x5D19, 0x81, 0xB2, 0x5D, 0xDB, 0x38, 0x16,
                 0x03, 0xCC);

typedef enum InputStreamOptions {
    InputStreamOptions_None = 0,
    InputStreamOptions_Partial = 1,
    InputStreamOptions_ReadAhead = 2,
} InputStreamOptions;

typedef enum FileAccessMode {
    FileAccessMode_Read = 0,
    FileAccessMode_ReadWrite = 1,
} FileAccessMode;

typedef struct IBuffer IBuffer;
#define IBuffer_METHODS(T)                                                                                             \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Capacity)(T * This, UINT32 * value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_Length)(T * This, UINT32 * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *put_Length)(T * This, UINT32 value);
MIME_COM_INTERFACE(IBuffer)

typedef struct IBufferByteAccess IBufferByteAccess;
#define IBufferByteAccess_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Buffer)(T * This, BYTE * *value);
MIME_COM_INTERFACE(IBufferByteAccess)

typedef struct IInputStream IInputStream;
#define IInputStream_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ReadAsync)(T * This, IBuffer * buffer, UINT32 count, InputStreamOptions options,        \
                                          void **operation);
MIME_COM_INTERFACE(IInputStream)

typedef struct IOutputStream IOutputStream;
#define IOutputStream_METHODS(T)                                                                                       \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *WriteAsync)(T * This, IBuffer * buffer, void **operation);                              \
    HRESULT(STDMETHODCALLTYPE *FlushAsync)(T * This, void **operation);
MIME_COM_INTERFACE(IOutputStream)

typedef struct IRandomAccessStream IRandomAccessStream;
#define IRandomAccessStream_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, UINT64 * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *put_Size)(T * This, UINT64 value);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetInputStreamAt)(T * This, UINT64 position, IInputStream * *stream);                   \
    HRESULT(STDMETHODCALLTYPE *GetOutputStreamAt)(T * This, UINT64 position, IOutputStream * *stream);                 \
    HRESULT(STDMETHODCALLTYPE *get_Position)(T * This, UINT64 * value);                                                \
    HRESULT(STDMETHODCALLTYPE *Seek)(T * This, UINT64 position);                                                       \
    HRESULT(STDMETHODCALLTYPE *CloneStream)(T * This, IRandomAccessStream * *stream);                                  \
    HRESULT(STDMETHODCALLTYPE *get_CanRead)(T * This, BOOLEAN * value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_CanWrite)(T * This, BOOLEAN * value);
MIME_COM_INTERFACE(IRandomAccessStream)

#endif
