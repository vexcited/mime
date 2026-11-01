/*
 * windows runtime asynchronous operations and the collection views that come with them. method lists are macros so
 * implementations and callers share the layouts. the parameterized interfaces are shown by the layout of the result
 * they carry: a flag, a pointer, or nothing.
 */
#ifndef MIME_WIN32_WINRT_ASYNC_H
#define MIME_WIN32_WINRT_ASYNC_H

#include <mime/win32/toast.h>

typedef enum AsyncStatus {
    AsyncStatus_Started = 0,
    AsyncStatus_Completed = 1,
    AsyncStatus_Canceled = 2,
    AsyncStatus_Error = 3,
} AsyncStatus;

#define E_ILLEGAL_METHOD_CALL ((HRESULT)0x8000000EL)
#define E_ILLEGAL_DELEGATE_ASSIGNMENT ((HRESULT)0x80000018L)
#define E_BOUNDS_ ((HRESULT)0x8000000BL)

MIME_DEFINE_GUID(IID_IAsyncInfo, 0x00000036, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IAsyncAction, 0x5A648006, 0x843A, 0x4DA9, 0x86, 0x5B, 0x9D, 0x26, 0xE5, 0xDF, 0xAD, 0x7B);
/* IAsyncOperation<bool>. */
MIME_DEFINE_GUID(IID_IAsyncOperationBool, 0xCDB5EFB3, 0x5788, 0x509D, 0x9B, 0xE1, 0x71, 0xCC, 0xB8, 0xA3, 0x36, 0x2A);
/* IAsyncOperation<KeyCredentialRetrievalResult>. */
MIME_DEFINE_GUID(IID_IAsyncOperationKeyRetrieval, 0x89D0AD1E, 0xBD4C, 0x55B4, 0x81, 0x0E, 0xBD, 0xDD, 0x4C, 0xEC, 0x7A,
                 0x2A);
/* IAsyncOperation<ConnectionProfile>. */
MIME_DEFINE_GUID(IID_IAsyncOperationConnectionProfile, 0x5BF519CA, 0x8ADB, 0x5AB5, 0xAB, 0xB8, 0xFF, 0x1B, 0xBE, 0x5D,
                 0x2D, 0xE8);

/* IVectorView<ConnectionProfile>, IVectorView<String> and their iterables and iterators. */
MIME_DEFINE_GUID(IID_IVectorViewConnectionProfile, 0x6DB1B492, 0x3852, 0x5DF8, 0xA2, 0x9D, 0x69, 0x44, 0x00, 0x2F, 0x58,
                 0xD4);
MIME_DEFINE_GUID(IID_IIterableConnectionProfile, 0x34DABEF9, 0x87D0, 0x5B1C, 0xA7, 0xAC, 0x9D, 0x29, 0x0A, 0xDE, 0xB0,
                 0xC8);
MIME_DEFINE_GUID(IID_IIteratorConnectionProfile, 0x89913732, 0xA08B, 0x5CB2, 0xAF, 0x16, 0xBB, 0xBB, 0x22, 0x23, 0x83,
                 0x9E);
MIME_DEFINE_GUID(IID_IVectorViewString, 0x2F13C006, 0xA03A, 0x5F69, 0xB0, 0x90, 0x75, 0xA4, 0x3E, 0x33, 0x42, 0x3E);
MIME_DEFINE_GUID(IID_IIterableString, 0xE2FCC7C1, 0x3BFC, 0x5A0B, 0xB2, 0xB0, 0x72, 0xE7, 0x69, 0xD1, 0xCB, 0x7E);
MIME_DEFINE_GUID(IID_IIteratorString, 0x8C304EBB, 0x6615, 0x50A4, 0x88, 0x29, 0x87, 0x9E, 0xCD, 0x44, 0x32, 0x36);

typedef struct IAsyncInfo IAsyncInfo;
#define IAsyncInfo_METHODS(T)                                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Id)(T * This, UINT32 * id);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_Status)(T * This, AsyncStatus * status);                                            \
    HRESULT(STDMETHODCALLTYPE *get_ErrorCode)(T * This, HRESULT * error);                                              \
    HRESULT(STDMETHODCALLTYPE *Cancel)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(IAsyncInfo)

/* a completed handler of any operation: the operation comes first, then how it ended. */
typedef struct IAsyncCompletedHandler IAsyncCompletedHandler;
#define IAsyncCompletedHandler_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, IInspectable * operation, AsyncStatus status);
MIME_COM_INTERFACE(IAsyncCompletedHandler)

typedef struct IAsyncAction IAsyncAction;
#define IAsyncAction_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *put_Completed)(T * This, IAsyncCompletedHandler * handler);                             \
    HRESULT(STDMETHODCALLTYPE *get_Completed)(T * This, IAsyncCompletedHandler * *handler);                            \
    HRESULT(STDMETHODCALLTYPE *GetResults)(T * This);
MIME_COM_INTERFACE(IAsyncAction)

/* an operation whose result is a flag or an interface pointer, which GetResults writes through result. */
typedef struct IAsyncOperation IAsyncOperation;
#define IAsyncOperation_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *put_Completed)(T * This, IAsyncCompletedHandler * handler);                             \
    HRESULT(STDMETHODCALLTYPE *get_Completed)(T * This, IAsyncCompletedHandler * *handler);                            \
    HRESULT(STDMETHODCALLTYPE *GetResults)(T * This, void *result);
MIME_COM_INTERFACE(IAsyncOperation)

typedef struct IIteratorAny IIteratorAny;
#define IIteratorAny_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Current)(T * This, void **item);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_HasCurrent)(T * This, BOOLEAN * value);                                             \
    HRESULT(STDMETHODCALLTYPE *MoveNext)(T * This, BOOLEAN * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetMany)(T * This, UINT32 capacity, void **items, UINT32 *actual);
MIME_COM_INTERFACE(IIteratorAny)

typedef struct IIterableAny IIterableAny;
#define IIterableAny_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *First)(T * This, IIteratorAny * *iterator);
MIME_COM_INTERFACE(IIterableAny)

#endif
