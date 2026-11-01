/*
 * Windows.Security.Credentials: the key credential manager and the result of opening or creating a key credential.
 * method lists are macros so implementations and callers share the layouts.
 */
#ifndef MIME_WIN32_KEYCREDENTIAL_H
#define MIME_WIN32_KEYCREDENTIAL_H

#include <mime/win32/winrt_async.h>

typedef enum KeyCredentialStatus {
    KeyCredentialStatus_Success = 0,
    KeyCredentialStatus_UnknownError = 1,
    KeyCredentialStatus_NotFound = 2,
    KeyCredentialStatus_UserCanceled = 3,
    KeyCredentialStatus_UserPrefersPassword = 4,
    KeyCredentialStatus_CredentialAlreadyExists = 5,
    KeyCredentialStatus_SecurityDeviceLocked = 6,
} KeyCredentialStatus;

typedef enum KeyCredentialCreationOption {
    KeyCredentialCreationOption_ReplaceExisting = 0,
    KeyCredentialCreationOption_FailIfExists = 1,
} KeyCredentialCreationOption;

MIME_DEFINE_GUID(IID_IKeyCredentialManagerStatics, 0x6AAC468B, 0x0EF1, 0x4CE0, 0x82, 0x90, 0x41, 0x06, 0xDA, 0x6A, 0x63,
                 0xB5);
MIME_DEFINE_GUID(IID_IKeyCredentialRetrievalResult, 0x58CD7703, 0x8D87, 0x4249, 0x9B, 0x58, 0xF6, 0x59, 0x8C, 0xC9, 0x64,
                 0x4E);

typedef struct IKeyCredentialRetrievalResult IKeyCredentialRetrievalResult;
#define IKeyCredentialRetrievalResult_METHODS(T)                                                                       \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Credential)(T * This, IInspectable * *value);                                       \
    HRESULT(STDMETHODCALLTYPE *get_Status)(T * This, KeyCredentialStatus * value);
MIME_COM_INTERFACE(IKeyCredentialRetrievalResult)

typedef struct IKeyCredentialManagerStatics IKeyCredentialManagerStatics;
#define IKeyCredentialManagerStatics_METHODS(T)                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *IsSupportedAsync)(T * This, IAsyncOperation * *operation);                              \
    HRESULT(STDMETHODCALLTYPE *RenewAttestationAsync)(T * This, IAsyncAction * *operation);                            \
    HRESULT(STDMETHODCALLTYPE *RequestCreateAsync)(T * This, HSTRING name, KeyCredentialCreationOption option,         \
                                                   IAsyncOperation * *operation);                                      \
    HRESULT(STDMETHODCALLTYPE *OpenAsync)(T * This, HSTRING name, IAsyncOperation * *operation);                       \
    HRESULT(STDMETHODCALLTYPE *DeleteAsync)(T * This, HSTRING name, IAsyncAction * *operation);
MIME_COM_INTERFACE(IKeyCredentialManagerStatics)

#endif
