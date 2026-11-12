/*
 * IDirectSoundCapture: the host has no recording path, so there is no
 * capture device. creation fails like windows without one, and objects
 * from the class factory stay uninitialized.
 */
#include "dsound_private.h"

typedef struct CAPTURE {
    IDirectSoundCapture iface;
    LONG ref;
    BOOL v8;
} CAPTURE;

static CAPTURE *from_iface(IDirectSoundCapture *iface)
{
    return CONTAINING_RECORD(iface, CAPTURE, iface);
}

static HRESULT STDMETHODCALLTYPE capture_query(IDirectSoundCapture *This, REFIID riid, void **out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirectSoundCapture))
        return E_NOINTERFACE;
    InterlockedIncrement(&from_iface(This)->ref);
    *out = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE capture_add_ref(IDirectSoundCapture *This)
{
    return InterlockedIncrement(&from_iface(This)->ref);
}

static ULONG STDMETHODCALLTYPE capture_release(IDirectSoundCapture *This)
{
    CAPTURE *c = from_iface(This);
    LONG ref = InterlockedDecrement(&c->ref);
    if (!ref)
        HeapFree(GetProcessHeap(), 0, c);
    return ref;
}

static HRESULT STDMETHODCALLTYPE capture_create_buffer(IDirectSoundCapture *This, LPCDSCBUFFERDESC desc,
                                                       LPDIRECTSOUNDCAPTUREBUFFER *buffer, IUnknown *outer)
{
    if (!desc || !buffer)
        return DSERR_INVALIDPARAM;
    *buffer = nullptr;
    return outer ? DSERR_NOAGGREGATION : DSERR_UNINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE capture_get_caps(IDirectSoundCapture *This, LPDSCCAPS caps)
{
    if (!caps || caps->dwSize != sizeof(DSCCAPS))
        return DSERR_INVALIDPARAM;
    return DSERR_UNINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE capture_initialize(IDirectSoundCapture *This, LPCGUID device)
{
    GUID resolved;
    return DsResolveDevice(device, &resolved, TRUE, nullptr) ? DS_OK : DSERR_NODRIVER;
}

static const IDirectSoundCaptureVtbl capture_vtbl = {capture_query,    capture_add_ref, capture_release,
                                                      capture_create_buffer, capture_get_caps, capture_initialize};

HRESULT DsCreateCapture(LPCGUID guid, IDirectSoundCapture **out, BOOL v8, BOOL initialize)
{
    *out = nullptr;
    if (initialize) {
        GUID resolved;
        if (!DsResolveDevice(guid, &resolved, TRUE, nullptr))
            return DSERR_NODRIVER;
    }
    CAPTURE *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return DSERR_OUTOFMEMORY;
    c->iface.lpVtbl = &capture_vtbl;
    c->ref = 1;
    c->v8 = v8;
    *out = &c->iface;
    return DS_OK;
}
