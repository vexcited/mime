/*
 * media foundation core objects: attributes, media types, buffers, samples, events, async results
 * and the dxgi device manager, plus mferror.h errors. methods follow windows' vtable order.
 */
#ifndef MIME_WIN32_MFOBJECTS_H
#define MIME_WIN32_MFOBJECTS_H

#include <mime/win32/objbase.h>
#include <mime/win32/objidl.h>
#include <mime/win32/propidl.h>
#include <mime/win32/mmreg.h>

typedef LONGLONG MFTIME;
typedef ULONGLONG QWORD;
typedef DWORD MediaEventType;

/* ------------------------------------------------------------------ errors */

#define MF_E_PLATFORM_NOT_INITIALIZED ((HRESULT)0xC00D36B0)
#define MF_E_BUFFERTOOSMALL ((HRESULT)0xC00D36B1)
#define MF_E_INVALIDREQUEST ((HRESULT)0xC00D36B2)
#define MF_E_INVALIDSTREAMNUMBER ((HRESULT)0xC00D36B3)
#define MF_E_INVALIDMEDIATYPE ((HRESULT)0xC00D36B4)
#define MF_E_NOTACCEPTING ((HRESULT)0xC00D36B5)
#define MF_E_NOT_INITIALIZED ((HRESULT)0xC00D36B6)
#define MF_E_UNSUPPORTED_REPRESENTATION ((HRESULT)0xC00D36B7)
#define MF_E_NO_MORE_TYPES ((HRESULT)0xC00D36B9)
#define MF_E_UNSUPPORTED_SERVICE ((HRESULT)0xC00D36BA)
#define MF_E_UNEXPECTED ((HRESULT)0xC00D36BB)
#define MF_E_INVALIDNAME ((HRESULT)0xC00D36BC)
#define MF_E_INVALIDTYPE ((HRESULT)0xC00D36BD)
#define MF_E_INVALID_FILE_FORMAT ((HRESULT)0xC00D36BE)
#define MF_E_INVALIDINDEX ((HRESULT)0xC00D36BF)
#define MF_E_INVALID_TIMESTAMP ((HRESULT)0xC00D36C0)
#define MF_E_UNSUPPORTED_SCHEME ((HRESULT)0xC00D36C3)
#define MF_E_UNSUPPORTED_BYTESTREAM_TYPE ((HRESULT)0xC00D36C4)
#define MF_E_UNSUPPORTED_TIME_FORMAT ((HRESULT)0xC00D36C5)
#define MF_E_NO_SAMPLE_TIMESTAMP ((HRESULT)0xC00D36C8)
#define MF_E_NO_SAMPLE_DURATION ((HRESULT)0xC00D36C9)
#define MF_E_INVALID_STREAM_DATA ((HRESULT)0xC00D36CB)
#define MF_E_NOT_FOUND ((HRESULT)0xC00D36D5)
#define MF_E_NOT_AVAILABLE ((HRESULT)0xC00D36D6)
#define MF_S_MULTIPLE_BEGIN ((HRESULT)0x000D36D8)
#define MF_E_MULTIPLE_BEGIN ((HRESULT)0xC00D36D9)
#define MF_E_MULTIPLE_SUBSCRIBERS ((HRESULT)0xC00D36DA)
#define MF_E_TIMER_ORPHANED ((HRESULT)0xC00D36DB)
#define MF_E_STATE_TRANSITION_PENDING ((HRESULT)0xC00D36DC)
#define MF_E_UNSUPPORTED_STATE_TRANSITION ((HRESULT)0xC00D36DD)
#define MF_E_UNRECOVERABLE_ERROR_OCCURRED ((HRESULT)0xC00D36DE)
#define MF_E_BAD_STARTUP_VERSION ((HRESULT)0xC00D36E3)
#define MF_E_INVALID_POSITION ((HRESULT)0xC00D36E5)
#define MF_E_ATTRIBUTENOTFOUND ((HRESULT)0xC00D36E6)
#define MF_E_PROPERTY_TYPE_NOT_ALLOWED ((HRESULT)0xC00D36E7)
#define MF_E_BYTESTREAM_NOT_SEEKABLE ((HRESULT)0xC00D36EE)
#define MF_E_NO_EVENTS_AVAILABLE ((HRESULT)0xC00D3E80)
#define MF_E_INVALID_STATE_TRANSITION ((HRESULT)0xC00D3E82)
#define MF_E_END_OF_STREAM ((HRESULT)0xC00D3E84)
#define MF_E_SHUTDOWN ((HRESULT)0xC00D3E85)
#define MF_E_INVALID_FORMAT ((HRESULT)0xC00D3E8C)
#define MF_E_ALREADY_INITIALIZED ((HRESULT)0xC00D4650)
#define MF_E_TOPO_CODEC_NOT_FOUND ((HRESULT)0xC00D5212)
#define MF_E_TRANSFORM_TYPE_NOT_SET ((HRESULT)0xC00D6D60)
#define MF_E_NO_CAPTURE_DEVICES_AVAILABLE ((HRESULT)0xC00DABE0)
#define MF_E_CAPTURE_SINK_OUTPUT_NOT_SET ((HRESULT)0xC00DABE1)
#define MF_E_CAPTURE_ENGINE_INVALID_OP ((HRESULT)0xC00DABE4)
#define MF_E_CAPTURE_SOURCE_NO_INDEPENDENT_PHOTO_STREAM_PRESENT ((HRESULT)0xC00DABE6)
#define MF_E_CAPTURE_SOURCE_NO_VIDEO_STREAM_PRESENT ((HRESULT)0xC00DABE7)
#define MF_E_CAPTURE_SOURCE_NO_AUDIO_STREAM_PRESENT ((HRESULT)0xC00DABE8)
#define MF_E_CAPTURE_NO_SAMPLES_IN_QUEUE ((HRESULT)0xC00DABEB)
#define MF_E_HW_MFT_FAILED_START_STREAMING ((HRESULT)0xC00D3704)
#define MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED ((HRESULT)0xC00D3EA2)
#define MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED ((HRESULT)0xC00D3EA3)
#define MF_E_DXGI_DEVICE_NOT_INITIALIZED ((HRESULT)0x80041000)
#define MF_E_DXGI_NEW_VIDEO_DEVICE ((HRESULT)0x80041001)
#define MF_E_DXGI_VIDEO_DEVICE_LOCKED ((HRESULT)0x80041002)

/* ------------------------------------------------------------------ attributes */

typedef enum _MF_ATTRIBUTE_TYPE {
    MF_ATTRIBUTE_UINT32 = VT_UI4,
    MF_ATTRIBUTE_UINT64 = VT_UI8,
    MF_ATTRIBUTE_DOUBLE = VT_R8,
    MF_ATTRIBUTE_GUID = VT_CLSID,
    MF_ATTRIBUTE_STRING = VT_LPWSTR,
    MF_ATTRIBUTE_BLOB = VT_VECTOR | VT_UI1,
    MF_ATTRIBUTE_IUNKNOWN = VT_UNKNOWN,
} MF_ATTRIBUTE_TYPE;

typedef enum _MF_ATTRIBUTES_MATCH_TYPE {
    MF_ATTRIBUTES_MATCH_OUR_ITEMS = 0,
    MF_ATTRIBUTES_MATCH_THEIR_ITEMS = 1,
    MF_ATTRIBUTES_MATCH_ALL_ITEMS = 2,
    MF_ATTRIBUTES_MATCH_INTERSECTION = 3,
    MF_ATTRIBUTES_MATCH_SMALLER = 4,
} MF_ATTRIBUTES_MATCH_TYPE;

#define MF_ATTRIBUTE_SERIALIZE_UNKNOWN_BYREF 0x1

typedef struct IMFAttributes IMFAttributes;
typedef struct IMFMediaType IMFMediaType;
typedef struct IMFMediaBuffer IMFMediaBuffer;
typedef struct IMFSample IMFSample;
typedef struct IMFMediaEvent IMFMediaEvent;
typedef struct IMFAsyncResult IMFAsyncResult;
typedef struct IMFAsyncCallback IMFAsyncCallback;
typedef struct IMFMediaEventGenerator IMFMediaEventGenerator;
typedef struct IMFMediaEventQueue IMFMediaEventQueue;
typedef struct IMFActivate IMFActivate;
typedef struct IMFCollection IMFCollection;
typedef struct IMF2DBuffer IMF2DBuffer;
typedef struct IMF2DBuffer2 IMF2DBuffer2;
typedef struct IMFDXGIBuffer IMFDXGIBuffer;
typedef struct IMFDXGIDeviceManager IMFDXGIDeviceManager;
typedef struct IMFByteStream IMFByteStream;

#define IMFAttributes_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetItem)(T * This, REFGUID key, PROPVARIANT *value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetItemType)(T * This, REFGUID key, MF_ATTRIBUTE_TYPE *type);                           \
    HRESULT(STDMETHODCALLTYPE *CompareItem)(T * This, REFGUID key, const PROPVARIANT *value, BOOL *result);            \
    HRESULT(STDMETHODCALLTYPE *Compare)(T * This, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL *result); \
    HRESULT(STDMETHODCALLTYPE *GetUINT32)(T * This, REFGUID key, UINT32 *value);                                       \
    HRESULT(STDMETHODCALLTYPE *GetUINT64)(T * This, REFGUID key, UINT64 *value);                                       \
    HRESULT(STDMETHODCALLTYPE *GetDouble)(T * This, REFGUID key, double *value);                                       \
    HRESULT(STDMETHODCALLTYPE *GetGUID)(T * This, REFGUID key, GUID *value);                                           \
    HRESULT(STDMETHODCALLTYPE *GetStringLength)(T * This, REFGUID key, UINT32 *length);                                \
    HRESULT(STDMETHODCALLTYPE *GetString)(T * This, REFGUID key, LPWSTR value, UINT32 size, UINT32 *length);           \
    HRESULT(STDMETHODCALLTYPE *GetAllocatedString)(T * This, REFGUID key, LPWSTR *value, UINT32 *length);              \
    HRESULT(STDMETHODCALLTYPE *GetBlobSize)(T * This, REFGUID key, UINT32 *size);                                      \
    HRESULT(STDMETHODCALLTYPE *GetBlob)(T * This, REFGUID key, UINT8 *buffer, UINT32 size, UINT32 *written);           \
    HRESULT(STDMETHODCALLTYPE *GetAllocatedBlob)(T * This, REFGUID key, UINT8 **buffer, UINT32 *size);                 \
    HRESULT(STDMETHODCALLTYPE *GetUnknown)(T * This, REFGUID key, REFIID riid, void **object);                         \
    HRESULT(STDMETHODCALLTYPE *SetItem)(T * This, REFGUID key, const PROPVARIANT *value);                              \
    HRESULT(STDMETHODCALLTYPE *DeleteItem)(T * This, REFGUID key);                                                     \
    HRESULT(STDMETHODCALLTYPE *DeleteAllItems)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *SetUINT32)(T * This, REFGUID key, UINT32 value);                                        \
    HRESULT(STDMETHODCALLTYPE *SetUINT64)(T * This, REFGUID key, UINT64 value);                                        \
    HRESULT(STDMETHODCALLTYPE *SetDouble)(T * This, REFGUID key, double value);                                        \
    HRESULT(STDMETHODCALLTYPE *SetGUID)(T * This, REFGUID key, REFGUID value);                                         \
    HRESULT(STDMETHODCALLTYPE *SetString)(T * This, REFGUID key, LPCWSTR value);                                       \
    HRESULT(STDMETHODCALLTYPE *SetBlob)(T * This, REFGUID key, const UINT8 *buffer, UINT32 size);                      \
    HRESULT(STDMETHODCALLTYPE *SetUnknown)(T * This, REFGUID key, IUnknown *object);                                   \
    HRESULT(STDMETHODCALLTYPE *LockStore)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *UnlockStore)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *GetCount)(T * This, UINT32 *count);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetItemByIndex)(T * This, UINT32 index, GUID *key, PROPVARIANT *value);                 \
    HRESULT(STDMETHODCALLTYPE *CopyAllItems)(T * This, IMFAttributes *dest);

/* ------------------------------------------------------------------ media types */

#define MF_MEDIATYPE_EQUAL_MAJOR_TYPES 0x00000001
#define MF_MEDIATYPE_EQUAL_FORMAT_TYPES 0x00000002
#define MF_MEDIATYPE_EQUAL_FORMAT_DATA 0x00000004
#define MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA 0x00000008

#define IMFMediaType_METHODS(T)                                                                                        \
    IMFAttributes_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetMajorType)(T * This, GUID *major);                                                   \
    HRESULT(STDMETHODCALLTYPE *IsCompressedFormat)(T * This, BOOL *compressed);                                        \
    HRESULT(STDMETHODCALLTYPE *IsEqual)(T * This, IMFMediaType *type, DWORD *flags);                                   \
    HRESULT(STDMETHODCALLTYPE *GetRepresentation)(T * This, GUID representation, void **data);                         \
    HRESULT(STDMETHODCALLTYPE *FreeRepresentation)(T * This, GUID representation, void *data);

/* ------------------------------------------------------------------ buffers and samples */

#define IMFMediaBuffer_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, BYTE **data, DWORD *max_length, DWORD *current_length);                 \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetCurrentLength)(T * This, DWORD *length);                                             \
    HRESULT(STDMETHODCALLTYPE *SetCurrentLength)(T * This, DWORD length);                                              \
    HRESULT(STDMETHODCALLTYPE *GetMaxLength)(T * This, DWORD *length);

#define IMFSample_METHODS(T)                                                                                           \
    IMFAttributes_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetSampleFlags)(T * This, DWORD *flags);                                                \
    HRESULT(STDMETHODCALLTYPE *SetSampleFlags)(T * This, DWORD flags);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetSampleTime)(T * This, LONGLONG *time);                                               \
    HRESULT(STDMETHODCALLTYPE *SetSampleTime)(T * This, LONGLONG time);                                                \
    HRESULT(STDMETHODCALLTYPE *GetSampleDuration)(T * This, LONGLONG *duration);                                       \
    HRESULT(STDMETHODCALLTYPE *SetSampleDuration)(T * This, LONGLONG duration);                                        \
    HRESULT(STDMETHODCALLTYPE *GetBufferCount)(T * This, DWORD *count);                                                \
    HRESULT(STDMETHODCALLTYPE *GetBufferByIndex)(T * This, DWORD index, IMFMediaBuffer **buffer);                      \
    HRESULT(STDMETHODCALLTYPE *ConvertToContiguousBuffer)(T * This, IMFMediaBuffer **buffer);                          \
    HRESULT(STDMETHODCALLTYPE *AddBuffer)(T * This, IMFMediaBuffer *buffer);                                           \
    HRESULT(STDMETHODCALLTYPE *RemoveBufferByIndex)(T * This, DWORD index);                                            \
    HRESULT(STDMETHODCALLTYPE *RemoveAllBuffers)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *GetTotalLength)(T * This, DWORD *length);                                               \
    HRESULT(STDMETHODCALLTYPE *CopyToBuffer)(T * This, IMFMediaBuffer *buffer);

#define IMF2DBuffer_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Lock2D)(T * This, BYTE **scanline0, LONG *pitch);                                       \
    HRESULT(STDMETHODCALLTYPE *Unlock2D)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetScanline0AndPitch)(T * This, BYTE **scanline0, LONG *pitch);                         \
    HRESULT(STDMETHODCALLTYPE *IsContiguousFormat)(T * This, BOOL *contiguous);                                        \
    HRESULT(STDMETHODCALLTYPE *GetContiguousLength)(T * This, DWORD *length);                                          \
    HRESULT(STDMETHODCALLTYPE *ContiguousCopyTo)(T * This, BYTE *dest, DWORD size);                                    \
    HRESULT(STDMETHODCALLTYPE *ContiguousCopyFrom)(T * This, const BYTE *source, DWORD size);

typedef enum _MF2DBuffer_LockFlags {
    MF2DBuffer_LockFlags_LockTypeMask = 0x3,
    MF2DBuffer_LockFlags_Read = 0x1,
    MF2DBuffer_LockFlags_Write = 0x2,
    MF2DBuffer_LockFlags_ReadWrite = 0x3,
} MF2DBuffer_LockFlags;

#define IMF2DBuffer2_METHODS(T)                                                                                        \
    IMF2DBuffer_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *Lock2DSize)(T * This, MF2DBuffer_LockFlags flags, BYTE **scanline0, LONG *pitch,        \
                                           BYTE **start, DWORD *length);                                               \
    HRESULT(STDMETHODCALLTYPE *Copy2DTo)(T * This, IMF2DBuffer2 *dest);

#define IMFDXGIBuffer_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetResource)(T * This, REFIID riid, void **object);                                     \
    HRESULT(STDMETHODCALLTYPE *GetSubresourceIndex)(T * This, UINT *index);                                            \
    HRESULT(STDMETHODCALLTYPE *GetUnknown)(T * This, REFIID guid, REFIID riid, void **object);                         \
    HRESULT(STDMETHODCALLTYPE *SetUnknown)(T * This, REFIID guid, IUnknown *object);

#define IMFDXGIDeviceManager_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CloseDeviceHandle)(T * This, HANDLE device);                                            \
    HRESULT(STDMETHODCALLTYPE *GetVideoService)(T * This, HANDLE device, REFIID riid, void **service);                 \
    HRESULT(STDMETHODCALLTYPE *LockDevice)(T * This, HANDLE device, REFIID riid, void **object, BOOL block);           \
    HRESULT(STDMETHODCALLTYPE *OpenDeviceHandle)(T * This, HANDLE *device);                                            \
    HRESULT(STDMETHODCALLTYPE *ResetDevice)(T * This, IUnknown *device, UINT token);                                   \
    HRESULT(STDMETHODCALLTYPE *TestDevice)(T * This, HANDLE device);                                                   \
    HRESULT(STDMETHODCALLTYPE *UnlockDevice)(T * This, HANDLE device, BOOL save_state);

/* ------------------------------------------------------------------ asynchronous calls and events */

#define MFASYNC_FAST_IO_PROCESSING_CALLBACK 0x00000001
#define MFASYNC_SIGNAL_CALLBACK 0x00000002
#define MFASYNC_BLOCKING_CALLBACK 0x00000004
#define MFASYNC_REPLY_CALLBACK 0x00000008
#define MFASYNC_LOCALIZE_REMOTE_CALLBACK 0x00000010

#define MFASYNC_CALLBACK_QUEUE_UNDEFINED 0x00000000
#define MFASYNC_CALLBACK_QUEUE_STANDARD 0x00000001
#define MFASYNC_CALLBACK_QUEUE_RT 0x00000002
#define MFASYNC_CALLBACK_QUEUE_IO 0x00000003
#define MFASYNC_CALLBACK_QUEUE_TIMER 0x00000004
#define MFASYNC_CALLBACK_QUEUE_MULTITHREADED 0x00000005
#define MFASYNC_CALLBACK_QUEUE_LONG_FUNCTION 0x00000007
#define MFASYNC_CALLBACK_QUEUE_PRIVATE_MASK 0xFFFF0000
#define MFASYNC_CALLBACK_QUEUE_ALL 0xFFFFFFFF

#define MF_EVENT_FLAG_NO_WAIT 0x00000001

#define IMFAsyncResult_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetState)(T * This, IUnknown **state);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *SetStatus)(T * This, HRESULT status);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetObject)(T * This, IUnknown **object);                                                \
    IUnknown *(STDMETHODCALLTYPE *GetStateNoAddRef)(T * This);

#define IMFAsyncCallback_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetParameters)(T * This, DWORD *flags, DWORD *queue);                                   \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, IMFAsyncResult *result);

typedef enum {
    MEUnknown = 0,
    MEError = 1,
    MEExtendedType = 2,
    MENonFatalError = 3,
    MESessionUnknown = 100,
    MESessionStarted = 103,
    MESourceUnknown = 200,
    MESourceStarted = 201,
    MEStreamStarted = 202,
    MESourceSeeked = 203,
    MEStreamSeeked = 204,
    MENewStream = 205,
    MEUpdatedStream = 206,
    MESourceStopped = 207,
    MEStreamStopped = 208,
    MESourcePaused = 209,
    MEStreamPaused = 210,
    MEEndOfPresentation = 211,
    MEEndOfStream = 212,
    MEMediaSample = 213,
    MEStreamTick = 214,
    MEStreamThinMode = 215,
    MEStreamFormatChanged = 216,
    MESourceRateChanged = 217,
    MEEndOfPresentationSegment = 218,
    MESourceCharacteristicsChanged = 219,
} MediaEventTypes;

#define IMFMediaEvent_METHODS(T)                                                                                       \
    IMFAttributes_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetType)(T * This, MediaEventType *type);                                               \
    HRESULT(STDMETHODCALLTYPE *GetExtendedType)(T * This, GUID *type);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, HRESULT *status);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetValue)(T * This, PROPVARIANT *value);

#define IMFMediaEventGenerator_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetEvent)(T * This, DWORD flags, IMFMediaEvent **event);                                \
    HRESULT(STDMETHODCALLTYPE *BeginGetEvent)(T * This, IMFAsyncCallback *callback, IUnknown *state);                  \
    HRESULT(STDMETHODCALLTYPE *EndGetEvent)(T * This, IMFAsyncResult *result, IMFMediaEvent **event);                  \
    HRESULT(STDMETHODCALLTYPE *QueueEvent)(T * This, MediaEventType type, REFGUID extended, HRESULT status,            \
                                           const PROPVARIANT *value);

#define IMFMediaEventQueue_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetEvent)(T * This, DWORD flags, IMFMediaEvent **event);                                \
    HRESULT(STDMETHODCALLTYPE *BeginGetEvent)(T * This, IMFAsyncCallback *callback, IUnknown *state);                  \
    HRESULT(STDMETHODCALLTYPE *EndGetEvent)(T * This, IMFAsyncResult *result, IMFMediaEvent **event);                  \
    HRESULT(STDMETHODCALLTYPE *QueueEvent)(T * This, IMFMediaEvent *event);                                            \
    HRESULT(STDMETHODCALLTYPE *QueueEventParamVar)(T * This, MediaEventType type, REFGUID extended, HRESULT status,    \
                                                   const PROPVARIANT *value);                                          \
    HRESULT(STDMETHODCALLTYPE *QueueEventParamUnk)(T * This, MediaEventType type, REFGUID extended, HRESULT status,    \
                                                   IUnknown *object);                                                  \
    HRESULT(STDMETHODCALLTYPE *Shutdown)(T * This);

/* ------------------------------------------------------------------ activation, collections and byte streams */

#define IMFActivate_METHODS(T)                                                                                         \
    IMFAttributes_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ActivateObject)(T * This, REFIID riid, void **object);                                  \
    HRESULT(STDMETHODCALLTYPE *ShutdownObject)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *DetachObject)(T * This);

#define IMFCollection_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetElementCount)(T * This, DWORD *count);                                               \
    HRESULT(STDMETHODCALLTYPE *GetElement)(T * This, DWORD index, IUnknown **element);                                 \
    HRESULT(STDMETHODCALLTYPE *AddElement)(T * This, IUnknown *element);                                               \
    HRESULT(STDMETHODCALLTYPE *RemoveElement)(T * This, DWORD index, IUnknown **element);                              \
    HRESULT(STDMETHODCALLTYPE *InsertElementAt)(T * This, DWORD index, IUnknown *element);                             \
    HRESULT(STDMETHODCALLTYPE *RemoveAllElements)(T * This);

typedef enum _MFBYTESTREAM_SEEK_ORIGIN {
    msoBegin = 0,
    msoCurrent = 1,
} MFBYTESTREAM_SEEK_ORIGIN;

#define MFBYTESTREAM_IS_READABLE 0x00000001
#define MFBYTESTREAM_IS_WRITABLE 0x00000002
#define MFBYTESTREAM_IS_SEEKABLE 0x00000004
#define MFBYTESTREAM_IS_REMOTE 0x00000008
#define MFBYTESTREAM_IS_DIRECTORY 0x00000080
#define MFBYTESTREAM_HAS_SLOW_SEEK 0x00000100
#define MFBYTESTREAM_IS_PARTIALLY_DOWNLOADED 0x00000200
#define MFBYTESTREAM_SHARE_WRITE 0x00000400
#define MFBYTESTREAM_DOES_NOT_USE_NETWORK 0x00000800
#define MFBYTESTREAM_SEEK_FLAG_CANCEL_PENDING_IO 0x00000001

#define IMFByteStream_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCapabilities)(T * This, DWORD *capabilities);                                        \
    HRESULT(STDMETHODCALLTYPE *GetLength)(T * This, QWORD *length);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetLength)(T * This, QWORD length);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetCurrentPosition)(T * This, QWORD *position);                                         \
    HRESULT(STDMETHODCALLTYPE *SetCurrentPosition)(T * This, QWORD position);                                          \
    HRESULT(STDMETHODCALLTYPE *IsEndOfStream)(T * This, BOOL *end);                                                    \
    HRESULT(STDMETHODCALLTYPE *Read)(T * This, BYTE *buffer, ULONG size, ULONG *read);                                 \
    HRESULT(STDMETHODCALLTYPE *BeginRead)(T * This, BYTE *buffer, ULONG size, IMFAsyncCallback *callback,              \
                                          IUnknown *state);                                                            \
    HRESULT(STDMETHODCALLTYPE *EndRead)(T * This, IMFAsyncResult *result, ULONG *read);                                \
    HRESULT(STDMETHODCALLTYPE *Write)(T * This, const BYTE *buffer, ULONG size, ULONG *written);                       \
    HRESULT(STDMETHODCALLTYPE *BeginWrite)(T * This, const BYTE *buffer, ULONG size, IMFAsyncCallback *callback,       \
                                           IUnknown *state);                                                           \
    HRESULT(STDMETHODCALLTYPE *EndWrite)(T * This, IMFAsyncResult *result, ULONG *written);                            \
    HRESULT(STDMETHODCALLTYPE *Seek)(T * This, MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD flags,          \
                                     QWORD *position);                                                                 \
    HRESULT(STDMETHODCALLTYPE *Flush)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);

MIME_COM_INTERFACE(IMFAttributes)
MIME_COM_INTERFACE(IMFMediaType)
MIME_COM_INTERFACE(IMFMediaBuffer)
MIME_COM_INTERFACE(IMFSample)
MIME_COM_INTERFACE(IMF2DBuffer)
MIME_COM_INTERFACE(IMF2DBuffer2)
MIME_COM_INTERFACE(IMFDXGIBuffer)
MIME_COM_INTERFACE(IMFDXGIDeviceManager)
MIME_COM_INTERFACE(IMFAsyncResult)
MIME_COM_INTERFACE(IMFAsyncCallback)
MIME_COM_INTERFACE(IMFMediaEvent)
MIME_COM_INTERFACE(IMFMediaEventGenerator)
MIME_COM_INTERFACE(IMFMediaEventQueue)
MIME_COM_INTERFACE(IMFActivate)
MIME_COM_INTERFACE(IMFCollection)
MIME_COM_INTERFACE(IMFByteStream)

MIME_DEFINE_GUID(IID_IMFAttributes, 0x2CD2D921, 0xC447, 0x44A7, 0xA1, 0x3C, 0x4A, 0xDA, 0xBF, 0xC2, 0x47, 0xE3);
MIME_DEFINE_GUID(IID_IMFMediaType, 0x44AE0FA8, 0xEA31, 0x4109, 0x8D, 0x2E, 0x4C, 0xAE, 0x49, 0x97, 0xC5, 0x55);
MIME_DEFINE_GUID(IID_IMFAudioMediaType, 0x26A0ADC3, 0xCE26, 0x4672, 0x93, 0x04, 0x69, 0x55, 0x2E, 0xDD, 0x3F, 0xAF);
MIME_DEFINE_GUID(IID_IMFVideoMediaType, 0xB99F381F, 0xA8F9, 0x47A2, 0xA5, 0xAF, 0xCA, 0x3A, 0x22, 0x5A, 0x38, 0x90);
MIME_DEFINE_GUID(IID_IMFMediaBuffer, 0x045FA593, 0x8799, 0x42B8, 0xBC, 0x8D, 0x89, 0x68, 0xC6, 0x45, 0x35, 0x07);
MIME_DEFINE_GUID(IID_IMFSample, 0xC40A00F2, 0xB93A, 0x4D80, 0xAE, 0x8C, 0x5A, 0x1C, 0x63, 0x4F, 0x58, 0xE4);
MIME_DEFINE_GUID(IID_IMF2DBuffer, 0x7DC9D5F9, 0x9ED9, 0x44EC, 0x9B, 0xBF, 0x06, 0x00, 0xBB, 0x58, 0x9F, 0xBB);
MIME_DEFINE_GUID(IID_IMF2DBuffer2, 0x33AE5EA6, 0x4316, 0x436F, 0x8D, 0xDD, 0xD7, 0x3D, 0x22, 0xF8, 0x29, 0xEC);
MIME_DEFINE_GUID(IID_IMFDXGIBuffer, 0xE7174CFA, 0x1C9E, 0x48B1, 0x88, 0x66, 0x62, 0x62, 0x26, 0xBF, 0xC2, 0x58);
MIME_DEFINE_GUID(IID_IMFDXGIDeviceManager, 0xEB533D5D, 0x2DB6, 0x40F8, 0x97, 0xA9, 0x49, 0x46, 0x92, 0x01, 0x4F, 0x07);
MIME_DEFINE_GUID(IID_IMFAsyncResult, 0xAC6B7889, 0x0740, 0x4D51, 0x86, 0x19, 0x90, 0x59, 0x94, 0xA5, 0x5C, 0xC6);
MIME_DEFINE_GUID(IID_IMFAsyncCallback, 0xA27003CF, 0x2354, 0x4F2A, 0x8D, 0x6A, 0xAB, 0x7C, 0xFF, 0x15, 0x43, 0x7E);
MIME_DEFINE_GUID(IID_IMFMediaEvent, 0xDF598932, 0xF10C, 0x4E39, 0xBB, 0xA2, 0xC3, 0x08, 0xF1, 0x01, 0xDA, 0xA3);
MIME_DEFINE_GUID(IID_IMFMediaEventGenerator, 0x2CD0BD52, 0xBCD5, 0x4B89, 0xB6, 0x2C, 0xEA, 0xDC, 0x0C, 0x03, 0x1E,
                 0x7D);
MIME_DEFINE_GUID(IID_IMFMediaEventQueue, 0x36F846FC, 0x2256, 0x48B6, 0xB5, 0x8E, 0xE2, 0xB6, 0x38, 0x31, 0x65, 0x81);
MIME_DEFINE_GUID(IID_IMFActivate, 0x7FEE9E9A, 0x4A89, 0x47A6, 0x89, 0x9C, 0xB6, 0xA5, 0x3A, 0x70, 0xFB, 0x67);
MIME_DEFINE_GUID(IID_IMFCollection, 0x5BC8A76B, 0x869A, 0x46A3, 0x9B, 0x03, 0xFA, 0x21, 0x8A, 0x66, 0xAE, 0xBE);
MIME_DEFINE_GUID(IID_IMFByteStream, 0xAD4C1B00, 0x4BF7, 0x422F, 0x91, 0x75, 0x75, 0x66, 0x93, 0xD9, 0x13, 0x0D);

#endif
