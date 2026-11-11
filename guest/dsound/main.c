/*
 * dsound.dll exports: device and capture creation, enumeration, default
 * device ids, and the class objects CoCreateInstance reaches. there is one
 * render device, the host's default output, named like mmdevapi names it,
 * and no capture device.
 */
#include "dsound_private.h"
#include "amplitude.h"

#include "../lib/audio/devices.h"

int _fltused = 0x9875;

RTL_CRITICAL_SECTION DsLock;
HINSTANCE DsModule;

/* mmdevapi's endpoint {0.0.0.00000000}.{6d696d65-0000-4000-8000-000000000001}, whose guid is the device's. */
const GUID DsDeviceGuid = {0x6d696d65, 0x0000, 0x4000, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
#if defined(__aarch64__)
const WCHAR DsDeviceName[] = L"Speakers (Qualcomm Audio)";
#else
const WCHAR DsDeviceName[] = L"Speakers (Realtek(R) Audio)";
#endif
const WCHAR DsDeviceModule[] = L"{0.0.0.00000000}.{6d696d65-0000-4000-8000-000000000001}";

static LONG g_objects;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DsModule = module;
        RtlInitializeCriticalSection(&DsLock);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ formats and gains */

DWORD DsFormatSize(const WAVEFORMATEX *f)
{
    return f->wFormatTag == WAVE_FORMAT_PCM ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + f->cbSize;
}

void DsCopyFormat(WAVEFORMATEX *to, const WAVEFORMATEX *from)
{
    memcpy(to, from, DsFormatSize(from));
    if (from->wFormatTag == WAVE_FORMAT_PCM)
        to->cbSize = 0;
}

WORD DsFormatTag(const WAVEFORMATEX *f)
{
    if (f->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        return f->wFormatTag;
    static const BYTE base[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    const WAVEFORMATEXTENSIBLE *e = (const WAVEFORMATEXTENSIBLE *)f;
    if (f->cbSize < 22 || memcmp((const BYTE *)&e->SubFormat + 2, base, 14))
        return 0;
    return (WORD)e->SubFormat.Data1;
}

static BOOL rate_ok(const WAVEFORMATEX *f, DWORD align)
{
    if (f->nSamplesPerSec - 100 > 199900 || f->nBlockAlign != align)
        return FALSE;
    /* windows allows the average rate five percent off. */
    DWORD bytes = f->nSamplesPerSec * f->nBlockAlign;
    return f->nAvgBytesPerSec <= bytes + bytes / 20 && f->nAvgBytesPerSec >= bytes - bytes / 20;
}

static DWORD channel_bits(DWORD mask)
{
    DWORD n = 0;
    for (; mask; mask &= mask - 1)
        n++;
    return n;
}

/* like windows: pcm of 8 to 32 bits, 32-bit floats, and other tags taken as they are. */
BOOL DsValidFormat(const WAVEFORMATEX *f)
{
    WORD tag = DsFormatTag(f);
    BOOL ok = TRUE;
    if (tag == WAVE_FORMAT_PCM)
        ok = f->nChannels && !((f->wBitsPerSample - 8) & 0xFFE7) &&
             rate_ok(f, (DWORD)f->nChannels * (f->wBitsPerSample >> 3));
    else if (tag == WAVE_FORMAT_IEEE_FLOAT)
        ok = f->nChannels && f->wBitsPerSample == 32 && rate_ok(f, 4u * f->nChannels);
    if (ok && f->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *e = (const WAVEFORMATEXTENSIBLE *)f;
        ok = f->cbSize >= 22 && e->Samples.wValidBitsPerSample <= f->wBitsPerSample &&
             channel_bits(e->dwChannelMask) <= f->nChannels && !IsEqualGUID(&e->SubFormat, &GUID_NULL);
    }
    return ok;
}

/*
 * like windows: hundredths of a decibel rounded to tenths, the amplitude
 * truncated to 16 bits, 0xFFFF at full volume. the engine scales it by
 * 1/65535.
 */
float DsAmplitude(LONG hundredths)
{
    if (hundredths >= 0)
        return 1.0f;
    LONG tenths = (hundredths + 5) / 10;
    if (tenths > -1)
        tenths = -1;
    if (tenths < -964)
        tenths = -964;
    return (float)amplitude_table[-tenths - 1] * 0.000015259022f;
}

/* ------------------------------------------------------------------ devices */

/* null and the default ids name the host's default device, and the guid of another device of the host names it. */
BOOL DsResolveDevice(LPCGUID requested, GUID *out, BOOL capture)
{
    UINT32 host;
    if (capture)
        return FALSE;
    if (!requested || IsEqualGUID(requested, &GUID_NULL) || IsEqualGUID(requested, &DSDEVID_DefaultPlayback) ||
        IsEqualGUID(requested, &DSDEVID_DefaultVoicePlayback) || IsEqualGUID(requested, &DsDeviceGuid)) {
        *out = DsDeviceGuid;
        return TRUE;
    }
    if (!MimeAudioEndpointHost(FALSE, requested, &host))
        return FALSE;
    *out = *requested;
    return TRUE;
}

static HRESULT create(LPCGUID guid, DWORD version, REFIID iid, void **out, IUnknown *outer)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    if (outer)
        return DSERR_NOAGGREGATION;
    IDirectSound8 *ds;
    RtlEnterCriticalSection(&DsLock);
    HRESULT hr = DsCreateDevice(nullptr, version, &ds);
    if (SUCCEEDED(hr)) {
        hr = ds->lpVtbl->Initialize(ds, guid);
        if (SUCCEEDED(hr))
            hr = ds->lpVtbl->QueryInterface(ds, iid, out);
        ds->lpVtbl->Release(ds);
    }
    RtlLeaveCriticalSection(&DsLock);
    return hr;
}

DSOUNDAPI HRESULT WINAPI DirectSoundCreate(LPCGUID device, LPDIRECTSOUND *sound, IUnknown *outer)
{
    return create(device, 0x700, &IID_IDirectSound, (void **)sound, outer);
}

DSOUNDAPI HRESULT WINAPI DirectSoundCreate8(LPCGUID device, LPDIRECTSOUND8 *sound, IUnknown *outer)
{
    return create(device, 0x800, &IID_IDirectSound8, (void **)sound, outer);
}

DSOUNDAPI HRESULT WINAPI DirectSoundCaptureCreate(LPCGUID device, LPDIRECTSOUNDCAPTURE *capture, IUnknown *outer)
{
    if (!capture)
        return DSERR_INVALIDPARAM;
    if (outer)
        return DSERR_NOAGGREGATION;
    return DsCreateCapture(device, capture, FALSE, TRUE);
}

DSOUNDAPI HRESULT WINAPI DirectSoundCaptureCreate8(LPCGUID device, LPDIRECTSOUNDCAPTURE8 *capture, IUnknown *outer)
{
    if (!capture)
        return DSERR_INVALIDPARAM;
    if (outer)
        return DSERR_NOAGGREGATION;
    return DsCreateCapture(device, capture, TRUE, TRUE);
}

/* ------------------------------------------------------------------ enumeration */

static BOOL call_enum(LPDSENUMCALLBACKW w, LPDSENUMCALLBACKA a, LPGUID guid, const WCHAR *description,
                      const WCHAR *module, void *context)
{
    if (w)
        return w(guid, description, module, context);
    char description_a[256], module_a[256];
    WideCharToMultiByte(CP_ACP, 0, description, -1, description_a, sizeof(description_a), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, module, -1, module_a, sizeof(module_a), nullptr, nullptr);
    return a(guid, description_a, module_a, context);
}

/* the primary driver without a guid first, then each device, like windows. */
static HRESULT enumerate(LPDSENUMCALLBACKW w, LPDSENUMCALLBACKA a, void *context, BOOL capture)
{
    if (!w && !a)
        return DSERR_INVALIDPARAM;
    if (capture)
        return DS_OK;
    if (!call_enum(w, a, nullptr, L"Primary Sound Driver", L"", context))
        return DS_OK;
    GUID guid = DsDeviceGuid;
    if (!call_enum(w, a, &guid, DsDeviceName, DsDeviceModule, context))
        return DS_OK;
    MimeAudioEndpoint *all = HeapAlloc(GetProcessHeap(), 0, sizeof(*all) * MIME_AUDIO_ENDPOINTS);
    if (!all)
        return DS_OK;
    UINT32 count = MimeAudioEndpoints(FALSE, all);
    for (UINT32 i = 1; i < count; i++) {
        const GUID *g = &all[i].guid;
        WCHAR module[64];
        wsprintfW(module, L"{0.0.0.00000000}.{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2,
                  g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
                  g->Data4[7]);
        guid = *g;
        if (!call_enum(w, a, &guid, all[i].name, module, context))
            break;
    }
    HeapFree(GetProcessHeap(), 0, all);
    return DS_OK;
}

DSOUNDAPI HRESULT WINAPI DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, LPVOID context)
{
    return enumerate(callback, nullptr, context, FALSE);
}

DSOUNDAPI HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context)
{
    return enumerate(nullptr, callback, context, FALSE);
}

DSOUNDAPI HRESULT WINAPI DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, LPVOID context)
{
    return enumerate(callback, nullptr, context, TRUE);
}

DSOUNDAPI HRESULT WINAPI DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context)
{
    return enumerate(nullptr, callback, context, TRUE);
}

/* the device behind a default id, or the device's own id. */
DSOUNDAPI HRESULT WINAPI GetDeviceID(LPCGUID src, LPGUID dst)
{
    if (!src || !dst)
        return DSERR_INVALIDPARAM;
    if (IsEqualGUID(src, &DSDEVID_DefaultCapture) || IsEqualGUID(src, &DSDEVID_DefaultVoiceCapture))
        return DSERR_NODRIVER;
    GUID guid;
    if (!DsResolveDevice(src, &guid, FALSE))
        return DSERR_NODRIVER;
    *dst = guid;
    return DS_OK;
}

DSOUNDAPI HRESULT WINAPI DirectSoundFullDuplexCreate(LPCGUID capture_device, LPCGUID render_device,
                                                     LPCDSCBUFFERDESC capture_desc, LPCDSBUFFERDESC render_desc,
                                                     HWND window, DWORD level, LPDIRECTSOUNDFULLDUPLEX *duplex,
                                                     LPDIRECTSOUNDCAPTUREBUFFER8 *capture_buffer,
                                                     LPDIRECTSOUNDBUFFER8 *render_buffer, IUnknown *outer)
{
    if (duplex)
        *duplex = nullptr;
    if (capture_buffer)
        *capture_buffer = nullptr;
    if (render_buffer)
        *render_buffer = nullptr;
    if (!capture_desc || !render_desc || !duplex || !capture_buffer || !render_buffer)
        return DSERR_INVALIDPARAM;
    if (outer)
        return DSERR_NOAGGREGATION;
    /* full duplex needs a capture device, and there is none. */
    return DSERR_NODRIVER;
}

/* ------------------------------------------------------------------ class objects */

typedef struct FACTORY {
    IClassFactory iface;
    LONG ref;
    CLSID clsid;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return InterlockedIncrement(&((FACTORY *)This)->ref);
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    FACTORY *f = (FACTORY *)This;
    ULONG ref = InterlockedDecrement(&f->ref);
    if (!ref) {
        HeapFree(GetProcessHeap(), 0, f);
        InterlockedDecrement(&g_objects);
    }
    return ref;
}

/* objects made this way wait for Initialize, like windows. */
static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    FACTORY *f = (FACTORY *)This;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (IsEqualGUID(&f->clsid, &CLSID_DirectSound) || IsEqualGUID(&f->clsid, &CLSID_DirectSound8)) {
        IDirectSound8 *ds;
        HRESULT hr = DsCreateDevice(nullptr, IsEqualGUID(&f->clsid, &CLSID_DirectSound8) ? 0x800 : 0x700, &ds);
        if (FAILED(hr))
            return hr;
        hr = ds->lpVtbl->QueryInterface(ds, riid, out);
        ds->lpVtbl->Release(ds);
        return hr;
    }
    if (IsEqualGUID(&f->clsid, &CLSID_DirectSoundPrivate))
        return DsCreatePrivate(riid, out);
    if (IsEqualGUID(&f->clsid, &CLSID_DirectSoundCapture) || IsEqualGUID(&f->clsid, &CLSID_DirectSoundCapture8)) {
        IDirectSoundCapture *c;
        HRESULT hr = DsCreateCapture(nullptr, &c, IsEqualGUID(&f->clsid, &CLSID_DirectSoundCapture8), FALSE);
        if (FAILED(hr))
            return hr;
        hr = c->lpVtbl->QueryInterface(c, riid, out);
        c->lpVtbl->Release(c);
        return hr;
    }
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&g_objects);
    else
        InterlockedDecrement(&g_objects);
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create,
                                               factory_lock};

DSOUNDAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out || !clsid || !riid)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    if (!IsEqualGUID(clsid, &CLSID_DirectSound) && !IsEqualGUID(clsid, &CLSID_DirectSound8) &&
        !IsEqualGUID(clsid, &CLSID_DirectSoundCapture) && !IsEqualGUID(clsid, &CLSID_DirectSoundCapture8) &&
        !IsEqualGUID(clsid, &CLSID_DirectSoundFullDuplex) && !IsEqualGUID(clsid, &CLSID_DirectSoundPrivate))
        return CLASS_E_CLASSNOTAVAILABLE;
    FACTORY *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->iface.lpVtbl = &factory_vtbl;
    f->ref = 1;
    f->clsid = *clsid;
    InterlockedIncrement(&g_objects);
    HRESULT hr = factory_query(&f->iface, riid, out);
    factory_release(&f->iface);
    return hr;
}

DSOUNDAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_objects ? S_FALSE : S_OK;
}
