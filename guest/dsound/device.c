/*
 * IDirectSound8: a device's cooperative level, its buffers and their
 * duplicates, its caps and speaker configuration. the primary buffer is
 * one object per device, handed out again by every creation.
 */
#include "dsound_private.h"

DSDEVICE *DsDevices;

DSDEVICE *DsDeviceFrom(void *iface)
{
    if (!iface)
        return nullptr;
    DSDEVICE *d = CONTAINING_RECORD(iface, DSDEVICE, iface);
    return d->magic == DEVICE_MAGIC ? d : nullptr;
}

static DSDEVICE *device_from_unknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, DSDEVICE, unknown);
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT query(DSDEVICE *d, REFIID riid, void **out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &d->unknown;
    else if (IsEqualGUID(riid, &IID_IDirectSound))
        *out = &d->iface;
    else if (IsEqualGUID(riid, &IID_IDirectSound8) && d->version >= 0x800)
        *out = &d->iface;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&d->ref);
    return S_OK;
}

static ULONG release(DSDEVICE *d)
{
    ULONG ref = InterlockedDecrement(&d->ref);
    if (!ref) {
        RtlEnterCriticalSection(&DsLock);
        DsDeviceDestroy(d);
        RtlLeaveCriticalSection(&DsLock);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE unknown_query(IUnknown *This, REFIID riid, void **out)
{
    return query(device_from_unknown(This), riid, out);
}

static ULONG STDMETHODCALLTYPE unknown_add_ref(IUnknown *This)
{
    return InterlockedIncrement(&device_from_unknown(This)->ref);
}

static ULONG STDMETHODCALLTYPE unknown_release(IUnknown *This)
{
    return release(device_from_unknown(This));
}

static const IUnknownVtbl unknown_vtbl = {unknown_query, unknown_add_ref, unknown_release};

static HRESULT STDMETHODCALLTYPE ds_query(IDirectSound8 *This, REFIID riid, void **out)
{
    DSDEVICE *d = DsDeviceFrom(This);
    return d ? query(d, riid, out) : DSERR_INVALIDPARAM;
}

static ULONG STDMETHODCALLTYPE ds_add_ref(IDirectSound8 *This)
{
    DSDEVICE *d = DsDeviceFrom(This);
    return d ? InterlockedIncrement(&d->ref) : 0;
}

static ULONG STDMETHODCALLTYPE ds_release(IDirectSound8 *This)
{
    DSDEVICE *d = DsDeviceFrom(This);
    return d ? release(d) : 0;
}

/* ------------------------------------------------------------------ the device */

void DsDeviceDestroy(DSDEVICE *d)
{
    /* the buffers go with the device, as in windows; their interfaces stop working. */
    for (DSBUFFER *b = d->buffers, *next; b; b = next) {
        next = b->next;
        DsBufferKill(b);
    }
    d->buffers = nullptr;
    if (d->primary)
        DsBufferKill(d->primary);
    for (DSDEVICE **p = &DsDevices; *p; p = &(*p)->next)
        if (*p == d) {
            *p = d->next;
            break;
        }
    d->magic = 0;
    HeapFree(GetProcessHeap(), 0, d);
}

/* the checks before every method: a live device, initialized. */
static HRESULT check(DSDEVICE *d)
{
    if (!d)
        return DSERR_INVALIDPARAM;
    return d->initialized ? DS_OK : DSERR_UNINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ds_create_buffer(IDirectSound8 *This, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *out,
                                                  IUnknown *outer)
{
    DSDEVICE *d = DsDeviceFrom(This);
    RtlEnterCriticalSection(&DsLock);
    HRESULT hr = check(d);
    if (SUCCEEDED(hr)) {
        if (!desc)
            hr = DSERR_INVALIDPARAM;
        else if (outer)
            hr = DSERR_NOAGGREGATION;
        else if (!out)
            hr = DSERR_INVALIDPARAM;
        else {
            *out = nullptr;
            DSBUFFER *b;
            hr = DsCreateBuffer(d, desc, &b);
            if (SUCCEEDED(hr))
                *out = (LPDIRECTSOUNDBUFFER)&b->iface;
        }
    }
    RtlLeaveCriticalSection(&DsLock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ds_get_caps(IDirectSound8 *This, LPDSCAPS caps)
{
    DSDEVICE *d = DsDeviceFrom(This);
    HRESULT hr = check(d);
    if (FAILED(hr))
        return hr;
    if (!caps || caps->dwSize != sizeof(DSCAPS))
        return DSERR_INVALIDPARAM;
    /* windows' software device, as its engine reports it. */
    memset(&caps->dwFlags, 0, sizeof(*caps) - sizeof(DWORD));
    caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
                    DSCAPS_CONTINUOUSRATE | DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT |
                    DSCAPS_SECONDARY16BIT;
    caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
    caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
    caps->dwPrimaryBuffers = 1;
    caps->dwMaxHwMixingAllBuffers = 1;
    caps->dwMaxHwMixingStaticBuffers = 1;
    caps->dwMaxHwMixingStreamingBuffers = 1;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE ds_duplicate(IDirectSound8 *This, LPDIRECTSOUNDBUFFER original,
                                              LPDIRECTSOUNDBUFFER *duplicate)
{
    DSDEVICE *d = DsDeviceFrom(This);
    RtlEnterCriticalSection(&DsLock);
    HRESULT hr = check(d);
    DSBUFFER *source = DsBufferFrom(original);
    if (SUCCEEDED(hr)) {
        if (!source || !duplicate)
            hr = DSERR_INVALIDPARAM;
        else if (source->primary)
            hr = DSERR_INVALIDCALL;
        else if (source->device != d)
            hr = DSERR_INVALIDPARAM;
        else {
            DSBUFFER *b;
            *duplicate = nullptr;
            hr = DsBufferDuplicate(source, &b);
            if (SUCCEEDED(hr))
                *duplicate = (LPDIRECTSOUNDBUFFER)&b->iface;
        }
    }
    RtlLeaveCriticalSection(&DsLock);
    return hr;
}

/* the level belongs to the root window's thread; focus is judged against it. */
static HRESULT STDMETHODCALLTYPE ds_set_cooperative_level(IDirectSound8 *This, HWND window, DWORD level)
{
    DSDEVICE *d = DsDeviceFrom(This);
    HRESULT hr = check(d);
    if (FAILED(hr))
        return hr;
    if (!window || level - 1 > 3)
        return DSERR_INVALIDPARAM;
    RtlEnterCriticalSection(&DsLock);
    HWND root = GetAncestor(window, GA_ROOT);
    d->window = window;
    d->thread = GetWindowThreadProcessId(root ? root : window, nullptr);
    DWORD old = d->level;
    d->level = level;
    /* leaving or entering write primary silences the primary buffer and stops it. */
    if (d->primary && (old == DSSCL_WRITEPRIMARY) != (level == DSSCL_WRITEPRIMARY)) {
        DsMixerStop(d->primary);
        if (d->primary->data)
            memset(d->primary->data, d->primary->format->wBitsPerSample == 8 ? 0x80 : 0, d->primary->size);
        d->primary->lost = FALSE;
    }
    RtlLeaveCriticalSection(&DsLock);
    DsUpdateFocus();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE ds_compact(IDirectSound8 *This)
{
    DSDEVICE *d = DsDeviceFrom(This);
    HRESULT hr = check(d);
    if (FAILED(hr))
        return hr;
    return d->level < DSSCL_PRIORITY ? DSERR_PRIOLEVELNEEDED : DS_OK;
}

/* the speakers the endpoint has, stereo here, with windows' wide geometry. */
static HRESULT STDMETHODCALLTYPE ds_get_speaker_config(IDirectSound8 *This, LPDWORD config)
{
    DSDEVICE *d = DsDeviceFrom(This);
    HRESULT hr = check(d);
    if (FAILED(hr))
        return hr;
    if (!config)
        return DSERR_INVALIDPARAM;
    *config = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE);
    return DS_OK;
}

/* windows keeps the setting in the device's registry key but plays to what the endpoint is. */
static HRESULT STDMETHODCALLTYPE ds_set_speaker_config(IDirectSound8 *This, DWORD config)
{
    DSDEVICE *d = DsDeviceFrom(This);
    HRESULT hr = check(d);
    if (FAILED(hr))
        return hr;
    BYTE speakers = DSSPEAKER_CONFIG(config), geometry = DSSPEAKER_GEOMETRY(config);
    if ((config & 0xFF00FF00) || speakers > DSSPEAKER_5POINT1_SURROUND)
        return DSERR_INVALIDPARAM;
    if (speakers != DSSPEAKER_STEREO && geometry)
        return DSERR_INVALIDPARAM;
    if ((BYTE)(geometry - 1) <= 3 || geometry > DSSPEAKER_GEOMETRY_MAX)
        return DSERR_INVALIDPARAM;
    d->speaker_config = speakers == DSSPEAKER_STEREO && !geometry
                            ? DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE)
                            : config;
    return DS_OK;
}

static HRESULT initialize(DSDEVICE *d, LPCGUID guid)
{
    if (d->initialized)
        return DSERR_ALREADYINITIALIZED;
    if (!DsResolveDevice(guid, &d->guid, FALSE))
        return DSERR_NODRIVER;
    /* the primary buffer, 22050 hz stereo 8 bit until a program sets its format. */
    DSBUFFERDESC desc = {sizeof(desc), DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D};
    DSBUFFER *primary = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*primary));
    if (!primary)
        return DSERR_OUTOFMEMORY;
    HRESULT hr = DsBufferInit(primary, d, &desc);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, primary);
        return hr;
    }
    d->primary = primary;
    d->speaker_config = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE);
    DsListenerDefaults(&d->listener);
    d->initialized = TRUE;
    DsMixerStart();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE ds_initialize(IDirectSound8 *This, LPCGUID guid)
{
    DSDEVICE *d = DsDeviceFrom(This);
    if (!d)
        return DSERR_INVALIDPARAM;
    RtlEnterCriticalSection(&DsLock);
    HRESULT hr = initialize(d, guid);
    RtlLeaveCriticalSection(&DsLock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ds_verify_certification(IDirectSound8 *This, LPDWORD certified)
{
    DSDEVICE *d = DsDeviceFrom(This);
    HRESULT hr = check(d);
    if (FAILED(hr))
        return hr;
    if (!certified)
        return DSERR_INVALIDPARAM;
    *certified = 1; /* DS_CERTIFIED */
    return DS_OK;
}

const IDirectSound8Vtbl DsDeviceVtbl = {ds_query,          ds_add_ref,
                                         ds_release,        ds_create_buffer,
                                         ds_get_caps,       ds_duplicate,
                                         ds_set_cooperative_level, ds_compact,
                                         ds_get_speaker_config, ds_set_speaker_config,
                                         ds_initialize,     ds_verify_certification};

HRESULT DsCreateDevice(LPCGUID guid, DWORD version, IDirectSound8 **out)
{
    DSDEVICE *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return DSERR_OUTOFMEMORY;
    d->iface.lpVtbl = &DsDeviceVtbl;
    d->unknown.lpVtbl = &unknown_vtbl;
    d->magic = DEVICE_MAGIC;
    d->ref = 1;
    d->version = version;
    RtlEnterCriticalSection(&DsLock);
    d->next = DsDevices;
    DsDevices = d;
    RtlLeaveCriticalSection(&DsLock);
    *out = &d->iface;
    return DS_OK;
}

/* ------------------------------------------------------------------ buffers */

/* windows' IsValidDsBufferFlags: the flags a version allows and the combinations it refuses. */
static BOOL valid_flags(DWORD flags, DWORD allowed)
{
    if (flags & ~allowed)
        return FALSE;
    DWORD location = flags & (DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE);
    if (location == (DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE))
        return FALSE;
    if (location && (flags & DSBCAPS_LOCDEFER))
        return FALSE;
    if ((flags & (DSBCAPS_MUTE3DATMAXDISTANCE | DSBCAPS_CTRL3D)) == DSBCAPS_MUTE3DATMAXDISTANCE)
        return FALSE;
    if ((flags & (DSBCAPS_CTRLFX | DSBCAPS_STATIC)) == (DSBCAPS_CTRLFX | DSBCAPS_STATIC))
        return FALSE;
    if ((flags & 0x2002) == 0x2002 || (flags & 0x1002) == 0x1002)
        return FALSE;
    return !(flags & DSBCAPS_PRIMARYBUFFER) || !(flags & 0xA322);
}

/* windows' BuildValidDsBufferDesc and IsValidDsBufferDesc. */
static HRESULT validate(DSDEVICE *d, LPCDSBUFFERDESC in, DSBUFFERDESC *desc)
{
    if (in->dwSize != sizeof(DSBUFFERDESC) && in->dwSize != sizeof(DSBUFFERDESC1))
        return DSERR_INVALIDPARAM;
    DWORD version = in->dwSize == sizeof(DSBUFFERDESC1) ? 0 : d->version;
    memset(desc, 0, sizeof(*desc));
    memcpy(desc, in, in->dwSize);
    desc->dwSize = sizeof(*desc);
    /* the obsolete itd algorithm and unknown ones fall back to the default. */
    if (!IsEqualGUID(&desc->guid3DAlgorithm, &GUID_NULL) &&
        !IsEqualGUID(&desc->guid3DAlgorithm, &DS3DALG_NO_VIRTUALIZATION) &&
        !IsEqualGUID(&desc->guid3DAlgorithm, &DS3DALG_HRTF_FULL) &&
        !IsEqualGUID(&desc->guid3DAlgorithm, &DS3DALG_HRTF_LIGHT))
        desc->guid3DAlgorithm = GUID_NULL;
    if (desc->dwReserved)
        return DSERR_INVALIDPARAM;
    if (!valid_flags(desc->dwFlags, 0xFE3FF))
        return DSERR_INVALIDPARAM;
    if (version < 0x700 && (desc->dwFlags & DSBCAPS_LOCDEFER))
        return DSERR_INVALIDPARAM;
    if (version < 0x800 && (desc->dwFlags & (DSBCAPS_CTRLFX | 0x2000)))
        return DSERR_DS8_REQUIRED;
    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
        if (desc->dwBufferBytes || desc->lpwfxFormat || !IsEqualGUID(&desc->guid3DAlgorithm, &GUID_NULL))
            return DSERR_INVALIDPARAM;
        return DS_OK;
    }
    const WAVEFORMATEX *f = desc->lpwfxFormat;
    if (!f || !DsValidFormat(f))
        return DSERR_INVALIDPARAM;
    if (desc->dwBufferBytes - 4 > 0xFFFFFFB)
        return DSERR_INVALIDPARAM;
    if (desc->dwFlags & DSBCAPS_CTRLFX) {
        DWORD minimum = 150 * f->nAvgBytesPerSec / 1000;
        minimum -= minimum % f->nBlockAlign;
        if (desc->dwBufferBytes < minimum)
            return DSERR_BUFFERTOOSMALL;
        if (f->nChannels - 1u > 1)
            return DSERR_INVALIDPARAM;
        WORD tag = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                       ? (WORD)((const WAVEFORMATEXTENSIBLE *)f)->SubFormat.Data1
                       : f->wFormatTag;
        if (tag == WAVE_FORMAT_PCM ? (f->wBitsPerSample != 8 && f->wBitsPerSample != 16)
                                   : tag != WAVE_FORMAT_IEEE_FLOAT)
            return DSERR_INVALIDPARAM;
    }
    if (!(desc->dwFlags & DSBCAPS_CTRL3D)) {
        if (!IsEqualGUID(&desc->guid3DAlgorithm, &GUID_NULL))
            return DSERR_INVALIDPARAM;
        return DS_OK;
    }
    if (f->nChannels > 2)
        return DSERR_INVALIDPARAM;
    if (version >= 0x800 && (f->nChannels != 1 || (desc->dwFlags & DSBCAPS_CTRLPAN)))
        return DSERR_INVALIDPARAM;
    return DS_OK;
}

HRESULT DsCreateBuffer(DSDEVICE *d, LPCDSBUFFERDESC in, DSBUFFER **out)
{
    DSBUFFERDESC desc;
    HRESULT hr = validate(d, in, &desc);
    if (FAILED(hr))
        return hr;
    if (desc.dwFlags & DSBCAPS_PRIMARYBUFFER) {
        DSBUFFER *p = d->primary;
        /* the first creation sets the controls, later ones get the same object. */
        if (!d->primary_users) {
            p->flags = (desc.dwFlags & ~(DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE)) | DSBCAPS_LOCSOFTWARE;
        }
        d->primary_users++;
        InterlockedIncrement(&p->ref);
        *out = p;
        return DS_OK;
    }
    /* there is no hardware mixing, and the engine mixes only linear formats. */
    if (desc.dwFlags & DSBCAPS_LOCHARDWARE)
        return E_NOTIMPL;
    WORD tag = DsFormatTag(desc.lpwfxFormat);
    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT)
        return desc.dwFlags & 0x3F0 ? E_NOTIMPL : DSERR_BADFORMAT;
    DSBUFFER *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return DSERR_OUTOFMEMORY;
    hr = DsBufferInit(b, d, &desc);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, b);
        return hr;
    }
    b->next = d->buffers;
    d->buffers = b;
    *out = b;
    return DS_OK;
}
