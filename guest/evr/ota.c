#include "evr_private.h"

#define ERROR_GRAPHICS_OPM_NOT_SUPPORTED_ ((HRESULT)0xC0262300)
#define MEPolicyChanged_ 401
#define MEPolicySet_ 402
#define PEACTION_PLAY_ 1
/* the attributes a video output reports to a policy. */
#define OPM_POLICY_ATTRIBUTES 21

MIME_DEFINE_GUID(IID_IVideoOTA, 0x33BD34D4, 0x7D76, 0x424A, 0x84, 0x5D, 0x92, 0xFE, 0x58, 0x13, 0xD1, 0xFD);
MIME_DEFINE_GUID(IID_IMFTrustedOutput_, 0xD19F8E95, 0xB126, 0x4446, 0x89, 0x0C, 0x5D, 0xCB, 0x7A, 0xD7, 0x14, 0x53);
MIME_DEFINE_GUID(IID_IMFOutputPolicy_, 0x7F00F10A, 0xDAED, 0x41AF, 0xAB, 0x26, 0x5F, 0xDF, 0xA4, 0xDF, 0xBA, 0x3C);
MIME_DEFINE_GUID(IID_IMFOutputSchema_, 0x7BE0FC5B, 0xABD9, 0x44FB, 0xA5, 0xC8, 0xF5, 0x01, 0x36, 0xE7, 0x15, 0x99);
MIME_DEFINE_GUID(MFCONNECTOR_UNKNOWN_, 0xAC3AEF5C, 0xCE43, 0x11D9, 0x92, 0xDB, 0x00, 0x0B, 0xDB, 0x28, 0xFF, 0x98);

/* IMFOutputPolicy is an attribute store with three methods after it; evr calls the first two. */
#define POLICY_GENERATE_REQUIRED_SCHEMAS 33
#define POLICY_GET_ORIGINATOR_ID 34
typedef HRESULT(STDMETHODCALLTYPE *GENERATE_REQUIRED_SCHEMAS)(IUnknown *policy, DWORD attributes, REFGUID subtype,
                                                              const GUID *supported, DWORD count, IUnknown **schemas);
typedef HRESULT(STDMETHODCALLTYPE *GET_ORIGINATOR_ID)(IUnknown *policy, GUID *id);

typedef struct VideoOta VideoOta;

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(VideoOta *This, REFIID riid, void **out);
    ULONG(STDMETHODCALLTYPE *AddRef)(VideoOta *This);
    ULONG(STDMETHODCALLTYPE *Release)(VideoOta *This);
    HRESULT(STDMETHODCALLTYPE *CreateTrustedOutput)(VideoOta *This, IUnknown **out, IUnknown *owner);
    HRESULT(STDMETHODCALLTYPE *ProcessPolicyEvent)(VideoOta *This, IUnknown *stream, IMFMediaEvent *event);
    HRESULT(STDMETHODCALLTYPE *SetDevice)(VideoOta *This, GUID *type, IUnknown *device);
    HRESULT(STDMETHODCALLTYPE *Heartbeat)(VideoOta *This);
} VideoOtaVtbl;

/*
 * evr's VideoOTAContainer: the output protection of the EVR's streams. it watches the outputs of the device through
 * OPM; Mime has no OPM, so its status stays E_UNEXPECTED and a policy only passes if it needs no protection.
 */
struct VideoOta {
    const VideoOtaVtbl *lpVtbl;
    LONG refs;
    CRITICAL_SECTION lock;
    IUnknown *callback;
    GUID device_type;
    IDirect3DDevice9 *device;
    BOOL watching;
};

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **out);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *First)(void *This, void *a);
    HRESULT(STDMETHODCALLTYPE *Second)(void *This, DWORD a, void *b);
    HRESULT(STDMETHODCALLTYPE *Third)(void *This, void *a);
} TrustedOutputVtbl;

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **out);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG(STDMETHODCALLTYPE *Release)(void *This);
    HRESULT(STDMETHODCALLTYPE *GetAction)(void *This, DWORD *action);
    HRESULT(STDMETHODCALLTYPE *SetPolicy)(void *This, IUnknown **policies, DWORD count, BYTE **blob, DWORD *size);
} TrustAuthorityVtbl;

/*
 * evr's CStreamOTA: its own IUnknown counts its own references; the trusted output and the output trust authority
 * it hands out count on the owner given to CreateTrustedOutput, as the EVR's stream sink owns them.
 */
typedef struct {
    const IUnknownVtbl *lpVtbl;
    const TrustedOutputVtbl *trusted;
    const TrustAuthorityVtbl *authority;
    LONG refs;
    IUnknown *owner;
    VideoOta *ota;
    IUnknown **policies;
    DWORD count;
} Stream;

/* ------------------------------------------------------------------ the stream */

static HRESULT STDMETHODCALLTYPE stream_QueryInterface(IUnknown *This, REFIID riid, void **out)
{
    Stream *s = (Stream *)This;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = s;
    else if (IsEqualGUID(riid, &IID_IMFTrustedOutput_))
        *out = &s->trusted;
    else
        return E_NOINTERFACE;
    ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE stream_AddRef(IUnknown *This)
{
    return InterlockedIncrement(&((Stream *)This)->refs);
}

static void drop_policies(Stream *s)
{
    for (DWORD i = 0; i < s->count; i++)
        s->policies[i]->lpVtbl->Release(s->policies[i]);
    HeapFree(GetProcessHeap(), 0, s->policies);
    s->policies = nullptr;
    s->count = 0;
}

static ULONG STDMETHODCALLTYPE stream_Release(IUnknown *This)
{
    Stream *s = (Stream *)This;
    ULONG refs = InterlockedDecrement(&s->refs);
    if (refs)
        return refs;
    drop_policies(s);
    s->ota->lpVtbl->Release(s->ota);
    HeapFree(GetProcessHeap(), 0, s);
    return 0;
}

static const IUnknownVtbl stream_vtbl = {stream_QueryInterface, stream_AddRef, stream_Release};

#define TRUSTED(This) CONTAINING_RECORD(This, Stream, trusted)
#define AUTHORITY(This) CONTAINING_RECORD(This, Stream, authority)

static HRESULT STDMETHODCALLTYPE trusted_QueryInterface(void *This, REFIID riid, void **out)
{
    IUnknown *owner = TRUSTED(This)->owner;
    return owner->lpVtbl->QueryInterface(owner, riid, out);
}

static ULONG STDMETHODCALLTYPE trusted_AddRef(void *This)
{
    IUnknown *owner = TRUSTED(This)->owner;
    return owner->lpVtbl->AddRef(owner);
}

static ULONG STDMETHODCALLTYPE trusted_Release(void *This)
{
    IUnknown *owner = TRUSTED(This)->owner;
    return owner->lpVtbl->Release(owner);
}

/* one authority, and the output is final. */
static HRESULT STDMETHODCALLTYPE trusted_GetOutputTrustAuthorityCount(void *This, void *count)
{
    if (!count)
        return E_POINTER;
    *(DWORD *)count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trusted_GetOutputTrustAuthorityByIndex(void *This, DWORD index, void *out)
{
    Stream *s = TRUSTED(This);
    if (index)
        return E_INVALIDARG;
    if (!out)
        return E_POINTER;
    *(void **)out = &s->authority;
    s->owner->lpVtbl->AddRef(s->owner);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trusted_IsFinal(void *This, void *final)
{
    if (!final)
        return E_POINTER;
    *(BOOL *)final = TRUE;
    return S_OK;
}

static const TrustedOutputVtbl trusted_vtbl = {
    trusted_QueryInterface,
    trusted_AddRef,
    trusted_Release,
    trusted_GetOutputTrustAuthorityCount,
    trusted_GetOutputTrustAuthorityByIndex,
    trusted_IsFinal,
};

static HRESULT STDMETHODCALLTYPE authority_QueryInterface(void *This, REFIID riid, void **out)
{
    IUnknown *owner = AUTHORITY(This)->owner;
    return owner->lpVtbl->QueryInterface(owner, riid, out);
}

static ULONG STDMETHODCALLTYPE authority_AddRef(void *This)
{
    IUnknown *owner = AUTHORITY(This)->owner;
    return owner->lpVtbl->AddRef(owner);
}

static ULONG STDMETHODCALLTYPE authority_Release(void *This)
{
    IUnknown *owner = AUTHORITY(This)->owner;
    return owner->lpVtbl->Release(owner);
}

static HRESULT STDMETHODCALLTYPE authority_GetAction(void *This, DWORD *action)
{
    if (!action)
        return E_POINTER;
    *action = PEACTION_PLAY_;
    return S_OK;
}

/*
 * evr's SetPolicy: no policy where there was none is nothing to do. without OPM every policy must allow the
 * picture to go out unprotected, which GenerateRequiredSchemas tells; one that does not fails the whole set.
 */
static HRESULT STDMETHODCALLTYPE authority_SetPolicy(void *This, IUnknown **policies, DWORD count, BYTE **blob,
                                                     DWORD *size)
{
    Stream *s = AUTHORITY(This);
    if (blob)
        *blob = nullptr;
    if (size)
        *size = 0;
    EnterCriticalSection(&s->ota->lock);
    HRESULT hr = S_OK;
    if (!s->count && (!policies || !count))
        goto done;
    IUnknown **kept = nullptr;
    if (count && !(kept = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*kept)))) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    for (DWORD i = 0; i < count; i++) {
        GUID originator;
        kept[i] = policies[i];
        kept[i]->lpVtbl->AddRef(kept[i]);
        ((GET_ORIGINATOR_ID)((void **)kept[i]->lpVtbl)[POLICY_GET_ORIGINATOR_ID])(kept[i], &originator);
    }
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++) {
        IUnknown *schemas = nullptr;
        if (FAILED(((GENERATE_REQUIRED_SCHEMAS)((void **)kept[i]->lpVtbl)[POLICY_GENERATE_REQUIRED_SCHEMAS])(
                kept[i], OPM_POLICY_ATTRIBUTES, &MFCONNECTOR_UNKNOWN_, nullptr, 0, &schemas)))
            hr = ERROR_GRAPHICS_OPM_NOT_SUPPORTED_;
        if (schemas)
            schemas->lpVtbl->Release(schemas);
    }
    if (FAILED(hr)) {
        for (DWORD i = 0; i < count; i++)
            kept[i]->lpVtbl->Release(kept[i]);
        HeapFree(GetProcessHeap(), 0, kept);
        goto done;
    }
    drop_policies(s);
    s->policies = kept;
    s->count = count;
done:
    LeaveCriticalSection(&s->ota->lock);
    return hr;
}

static const TrustAuthorityVtbl authority_vtbl = {
    authority_QueryInterface, authority_AddRef, authority_Release, authority_GetAction, authority_SetPolicy,
};

/* ------------------------------------------------------------------ the container */

static HRESULT STDMETHODCALLTYPE ota_QueryInterface(VideoOta *This, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IVideoOTA) && !IsEqualGUID(riid, &IID_IUnknown))
        return E_NOINTERFACE;
    *out = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ota_AddRef(VideoOta *This)
{
    return InterlockedIncrement(&This->refs);
}

static ULONG STDMETHODCALLTYPE ota_Release(VideoOta *This)
{
    ULONG refs = InterlockedDecrement(&This->refs);
    if (refs)
        return refs;
    if (This->device)
        This->device->lpVtbl->Release(This->device);
    if (This->callback)
        This->callback->lpVtbl->Release(This->callback);
    DeleteCriticalSection(&This->lock);
    HeapFree(GetProcessHeap(), 0, This);
    return 0;
}

static HRESULT STDMETHODCALLTYPE ota_CreateTrustedOutput(VideoOta *This, IUnknown **out, IUnknown *owner)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    Stream *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->lpVtbl = &stream_vtbl;
    s->trusted = &trusted_vtbl;
    s->authority = &authority_vtbl;
    s->owner = owner ? owner : (IUnknown *)s;
    s->ota = This;
    This->lpVtbl->AddRef(This);
    return stream_QueryInterface((IUnknown *)s, &IID_IUnknown, (void **)out);
}

/* a changed policy or schema arrives as an event carrying the object. */
static HRESULT STDMETHODCALLTYPE ota_ProcessPolicyEvent(VideoOta *This, IUnknown *stream, IMFMediaEvent *event)
{
    if (!stream)
        return E_INVALIDARG;
    MediaEventType type = 0;
    HRESULT hr = event->lpVtbl->GetType(event, &type);
    if (FAILED(hr))
        return hr;
    if (type != MEPolicyChanged_ && type != MEPolicySet_)
        return E_UNEXPECTED;
    PROPVARIANT value;
    memset(&value, 0, sizeof(value));
    hr = event->lpVtbl->GetValue(event, &value);
    if (FAILED(hr))
        return hr;
    if (value.vt != VT_UNKNOWN || !value.punkVal) {
        PropVariantClear(&value);
        return E_INVALIDARG;
    }
    IUnknown *object = nullptr;
    hr = value.punkVal->lpVtbl->QueryInterface(
        value.punkVal, type == MEPolicyChanged_ ? &IID_IMFOutputPolicy_ : &IID_IMFOutputSchema_, (void **)&object);
    PropVariantClear(&value);
    if (SUCCEEDED(hr) && !object)
        hr = E_INVALIDARG;
    if (object)
        object->lpVtbl->Release(object);
    return hr;
}

/* nothing is watched without a callback to report to. */
static HRESULT STDMETHODCALLTYPE ota_SetDevice(VideoOta *This, GUID *type, IUnknown *device)
{
    HRESULT hr = S_OK;
    EnterCriticalSection(&This->lock);
    if (This->device) {
        This->device->lpVtbl->Release(This->device);
        This->device = nullptr;
    }
    if (!This->callback) {
        hr = E_UNEXPECTED;
    } else if (!type) {
        hr = E_POINTER;
    } else {
        This->device_type = *type;
        if (IsEqualGUID(type, &IID_IDirect3DDevice9) && device)
            hr = device->lpVtbl->QueryInterface(device, &IID_IDirect3DDevice9, (void **)&This->device);
        This->watching = SUCCEEDED(hr);
    }
    LeaveCriticalSection(&This->lock);
    return hr;
}

/* the watchdog of windows refreshes the heartbeat while it watches. */
static HRESULT STDMETHODCALLTYPE ota_Heartbeat(VideoOta *This)
{
    return This->watching ? S_OK : E_FAIL;
}

static const VideoOtaVtbl ota_vtbl = {
    ota_QueryInterface, ota_AddRef,    ota_Release,   ota_CreateTrustedOutput,
    ota_ProcessPolicyEvent, ota_SetDevice, ota_Heartbeat,
};

HRESULT WINAPI MFCreateVideoOTA(IUnknown *callback, IUnknown **out)
{
    *out = nullptr;
    VideoOta *ota = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ota));
    if (!ota)
        return E_OUTOFMEMORY;
    ota->lpVtbl = &ota_vtbl;
    ota->refs = 1;
    InitializeCriticalSection(&ota->lock);
    ota->callback = callback;
    if (callback)
        callback->lpVtbl->AddRef(callback);
    *out = (IUnknown *)ota;
    return S_OK;
}
