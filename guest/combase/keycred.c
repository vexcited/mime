/*
 * Windows.Security.Credentials.KeyCredentialManager: keys that Windows Hello keeps for a program. windows keeps them
 * in the container of the user, made when the user sets up a PIN, face or fingerprint. mime has no container and no
 * way to make one, which windows treats as a user without Hello: each operation runs the steps of windows and fails
 * where they find no container.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>
#include <mime/win32/keycredential.h>

const WCHAR CbKeyCredentialManagerClass[] = L"Windows.Security.Credentials.KeyCredentialManager";

#define NTE_NO_KEY_ ((HRESULT)0x8009000DL)
#define NTE_EXISTS_ ((HRESULT)0x8009000FL)
#define NTE_BAD_KEYSET_ ((HRESULT)0x80090016L)
#define NTE_UI_REQUIRED_ ((HRESULT)0x8009002EL)
#define NTE_AUTHENTICATION_IGNORED_ ((HRESULT)0x80090031L)
#define NTE_USER_CANCELLED_ ((HRESULT)0x80090036L)

/*
 * what the container service answers a program that asks for a key in a container that does not exist. a key made
 * for the program has no container to go into, and a key looked up has no keyset.
 */
#define CONTAINER_MISSING_FOR_CREATE NTE_NO_KEY_
#define CONTAINER_MISSING_FOR_LOOKUP NTE_BAD_KEYSET_

/* how windows tells a failure of the key storage from the credential status the program gets. */
static KeyCredentialStatus status_of(HRESULT hr)
{
    switch (hr) {
    case NTE_NO_KEY_:
    case NTE_BAD_KEYSET_:
        return KeyCredentialStatus_NotFound;
    case NTE_EXISTS_:
        return KeyCredentialStatus_CredentialAlreadyExists;
    case NTE_UI_REQUIRED_:
        return KeyCredentialStatus_UserPrefersPassword;
    case NTE_AUTHENTICATION_IGNORED_:
        return KeyCredentialStatus_SecurityDeviceLocked;
    case NTE_USER_CANCELLED_:
        return KeyCredentialStatus_UserCanceled;
    default:
        return hr != S_OK ? KeyCredentialStatus_UnknownError : KeyCredentialStatus_Success;
    }
}

/* ------------------------------------------------------------------ the result of opening or creating */

typedef struct RETRIEVAL_RESULT {
    WRT_OBJECT Base;
    KeyCredentialStatus Status;
} RETRIEVAL_RESULT;

WRT_INSPECTABLE_THUNKS(IKeyCredentialRetrievalResult)

/* there is never a credential, as there is no key to hold one. */
static HRESULT STDMETHODCALLTYPE result_get_credential(IKeyCredentialRetrievalResult *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE result_get_status(IKeyCredentialRetrievalResult *This, KeyCredentialStatus *value)
{
    if (!value)
        return E_POINTER;
    *value = ((RETRIEVAL_RESULT *)((WRT_IFACE *)This)->Owner)->Status;
    return S_OK;
}

static const IKeyCredentialRetrievalResultVtbl result_vtbl = {WRT_INSPECTABLE_ENTRIES(IKeyCredentialRetrievalResult),
                                                              result_get_credential, result_get_status};

static void result_destroy(WRT_OBJECT *object)
{
    CbFree(object);
}

static const WRT_CLASS result_class = {L"Windows.Security.Credentials.KeyCredentialRetrievalResult",
                                       1,
                                       {&IID_IKeyCredentialRetrievalResult},
                                       0,
                                       result_destroy};

/* the operation of a key that failed to open or create ends well unless the status is unknown. */
static HRESULT finish_retrieval(HRESULT hr, void **operation)
{
    RETRIEVAL_RESULT *result = CbAlloc(sizeof(*result));
    if (!result)
        return E_OUTOFMEMORY;
    memset(result, 0, sizeof(*result));
    const void *vtables[1] = {&result_vtbl};
    WrtInit(&result->Base, &result_class, vtables);
    result->Status = status_of(hr);
    return CbAsyncFinished(CB_ASYNC_KEY_RETRIEVAL, result->Status == KeyCredentialStatus_UnknownError ? hr : S_OK, FALSE,
                           (IUnknown *)&result->Base.Ifaces[0], operation);
}

/* ------------------------------------------------------------------ the statics */

WRT_INSPECTABLE_THUNKS(IKeyCredentialManagerStatics)

typedef struct IKeyCredentialManagerFactory IKeyCredentialManagerFactory;
#define IKeyCredentialManagerFactory_METHODS(T)                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(T * This, IInspectable * *instance);
MIME_COM_INTERFACE(IKeyCredentialManagerFactory)

WRT_INSPECTABLE_THUNKS(IKeyCredentialManagerFactory)

/*
 * windows asks for a key container with a test account to learn whether it can keep keys. only a container that
 * holds the account already, or one made for it, means keys are supported. the answer is false and the operation
 * itself succeeds.
 */
static HRESULT STDMETHODCALLTYPE statics_is_supported(IKeyCredentialManagerStatics *This, IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    return CbAsyncFinished(CB_ASYNC_BOOL, S_OK, FALSE, nullptr, (void **)operation);
}

/* a program without a container has no attestation to renew, which windows counts as done. */
static HRESULT STDMETHODCALLTYPE statics_renew_attestation(IKeyCredentialManagerStatics *This, IAsyncAction **operation)
{
    if (!operation)
        return E_POINTER;
    return CbAsyncFinished(CB_ASYNC_ACTION, S_OK, FALSE, nullptr, (void **)operation);
}

static HRESULT STDMETHODCALLTYPE statics_request_create(IKeyCredentialManagerStatics *This, HSTRING name,
                                                        KeyCredentialCreationOption option,
                                                        IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    if (!name)
        return E_INVALIDARG;
    return finish_retrieval(CONTAINER_MISSING_FOR_CREATE, (void **)operation);
}

static HRESULT STDMETHODCALLTYPE statics_open(IKeyCredentialManagerStatics *This, HSTRING name,
                                              IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    if (!name)
        return E_INVALIDARG;
    return finish_retrieval(CONTAINER_MISSING_FOR_LOOKUP, (void **)operation);
}

/* deleting a key that is not there is an error of the operation, which carries the storage error as it is. */
static HRESULT STDMETHODCALLTYPE statics_delete(IKeyCredentialManagerStatics *This, HSTRING name,
                                                IAsyncAction **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    if (!name)
        return E_INVALIDARG;
    return CbAsyncFinished(CB_ASYNC_ACTION, CONTAINER_MISSING_FOR_LOOKUP, FALSE, nullptr, (void **)operation);
}

/* the class has statics and no instances. */
static HRESULT STDMETHODCALLTYPE factory_activate(IKeyCredentialManagerFactory *This, IInspectable **instance)
{
    *instance = nullptr;
    return E_NOTIMPL;
}

static const IKeyCredentialManagerFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IKeyCredentialManagerFactory),
                                                              factory_activate};
static const IKeyCredentialManagerStaticsVtbl statics_vtbl = {WRT_INSPECTABLE_ENTRIES(IKeyCredentialManagerStatics),
                                                              statics_is_supported,
                                                              statics_renew_attestation,
                                                              statics_request_create,
                                                              statics_open,
                                                              statics_delete};

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS factory_class = {CbKeyCredentialManagerClass,
                                        2,
                                        {&IID_IActivationFactory, &IID_IKeyCredentialManagerStatics},
                                        0,
                                        factory_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[2] = {&factory_vtbl, &statics_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbKeyCredentialManagerFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
