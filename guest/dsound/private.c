/*
 * CLSID_DirectSoundPrivate: DSPROPSETID_DirectSoundDevice, which dsound's
 * tools and xinput read devices with. the host's outputs are described
 * the way windows describes wdm ones.
 */
#include "dsound_private.h"

#include "../lib/audio/devices.h"

typedef struct PRIVATE {
    IKsPropertySet iface;
    LONG ref;
    /* the descriptions point here, so these live as long as the object. */
    WCHAR description[0x100], module[MAX_PATH], interface[MAX_PATH];
    CHAR description_a[0x100], module_a[MAX_PATH], interface_a[MAX_PATH];
} PRIVATE;

static const IKsPropertySetVtbl private_vtbl;

/* the interface path follows how the audio stack names a render endpoint. */
static void fill_strings(PRIVATE *p, const MimeAudioEndpoint *e)
{
    lstrcpynW(p->description, e->name, RTL_NUMBER_OF(p->description));
    MimeAudioEndpointId(FALSE, &e->guid, p->module);
    wsprintfW(p->interface, L"\\\\?\\SWD#MMDEVAPI#%s#{e6327cad-dcec-4949-ae8a-991e976a79d2}", p->module);
    WideCharToMultiByte(CP_ACP, 0, p->description, -1, p->description_a, sizeof(p->description_a), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, p->module, -1, p->module_a, sizeof(p->module_a), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, p->interface, -1, p->interface_a, sizeof(p->interface_a), nullptr, nullptr);
}

HRESULT DsCreatePrivate(REFIID riid, void **out)
{
    PRIVATE *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->iface.lpVtbl = &private_vtbl;
    p->ref = 1;
    HRESULT hr = p->iface.lpVtbl->QueryInterface(&p->iface, riid, out);
    p->iface.lpVtbl->Release(&p->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE private_query(IKsPropertySet *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IKsPropertySet)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE private_add_ref(IKsPropertySet *This)
{
    return InterlockedIncrement(&((PRIVATE *)This)->ref);
}

static ULONG STDMETHODCALLTYPE private_release(IKsPropertySet *This)
{
    PRIVATE *p = (PRIVATE *)This;
    ULONG ref = InterlockedDecrement(&p->ref);
    if (!ref)
        HeapFree(GetProcessHeap(), 0, p);
    return ref;
}

/* the host's outputs, the default one first, which is also winmm's order. the caller frees the list. */
static UINT32 outputs(MimeAudioEndpoint **all)
{
    *all = HeapAlloc(GetProcessHeap(), 0, sizeof(**all) * MIME_AUDIO_ENDPOINTS);
    return *all ? MimeAudioEndpoints(FALSE, *all) : 0;
}

/* the device a default id or its own id names, and its place in the list. */
static BOOL find_device(const GUID *id, MimeAudioEndpoint *e, ULONG *wave)
{
    GUID resolved;
    if (!DsResolveDevice(id, &resolved, FALSE, nullptr))
        return FALSE;
    MimeAudioEndpoint *all;
    UINT32 count = outputs(&all);
    BOOL found = FALSE;
    for (UINT32 i = 0; i < count && !found; i++) {
        if (IsEqualGUID(&all[i].guid, &resolved)) {
            *e = all[i];
            *wave = i;
            found = TRUE;
        }
    }
    if (all)
        HeapFree(GetProcessHeap(), 0, all);
    return found;
}

/* winmm cuts its device names to 31 characters. */
static BOOL find_wave_device(const WCHAR *name, GUID *guid)
{
    MimeAudioEndpoint *all;
    UINT32 count = outputs(&all);
    BOOL found = FALSE;
    for (UINT32 i = 0; i < count && !found; i++) {
        WCHAR cut[32];
        lstrcpynW(cut, all[i].name, RTL_NUMBER_OF(cut));
        if (!lstrcmpW(name, all[i].name) || !lstrcmpW(name, cut)) {
            *guid = all[i].guid;
            found = TRUE;
        }
    }
    if (all)
        HeapFree(GetProcessHeap(), 0, all);
    return found;
}

static void describe_w(PRIVATE *p, PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d, const MimeAudioEndpoint *e,
                       ULONG wave)
{
    fill_strings(p, e);
    d->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    d->DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    d->DeviceId = e->guid;
    d->Description = p->description;
    d->Module = p->module;
    d->Interface = p->interface;
    d->WaveDeviceId = wave;
}

static void describe_a(PRIVATE *p, PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA d, const MimeAudioEndpoint *e,
                       ULONG wave)
{
    fill_strings(p, e);
    d->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    d->DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    d->DeviceId = e->guid;
    d->Description = p->description_a;
    d->Module = p->module_a;
    d->Interface = p->interface_a;
    d->WaveDeviceId = wave;
}

static void describe_1(PRIVATE *p, PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA d, const MimeAudioEndpoint *e,
                       ULONG wave)
{
    fill_strings(p, e);
    d->DeviceId = e->guid;
    lstrcpynA(d->DescriptionA, p->description_a, sizeof(d->DescriptionA));
    lstrcpynW(d->DescriptionW, p->description, RTL_NUMBER_OF(d->DescriptionW));
    lstrcpynA(d->ModuleA, p->module_a, sizeof(d->ModuleA));
    lstrcpynW(d->ModuleW, p->module, RTL_NUMBER_OF(d->ModuleW));
    d->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    d->DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    d->WaveDeviceId = wave;
    d->Devnode = 0;
}

static const ULONG g_sizes[9] = {
    0,
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA),
    sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA),
};

static HRESULT STDMETHODCALLTYPE private_get(IKsPropertySet *This, REFGUID set, ULONG id, LPVOID instance,
                                             ULONG instance_size, LPVOID data, ULONG data_size, PULONG returned)
{
    PRIVATE *p = (PRIVATE *)This;
    if (!set || !data)
        return DSERR_INVALIDPARAM;
    if (returned)
        *returned = 0;
    if (!IsEqualGUID(set, &DSPROPSETID_DirectSoundDevice))
        return E_PROP_SET_UNSUPPORTED;
    if (!id || id >= RTL_NUMBER_OF(g_sizes))
        return E_PROP_ID_UNSUPPORTED;
    if (data_size < g_sizes[id])
        return DSERR_INVALIDPARAM;
    MimeAudioEndpoint e;
    ULONG wave;
    switch (id) {
    case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA m = data;
        WCHAR name[0x100];
        if (m->DataFlow != DIRECTSOUNDDEVICE_DATAFLOW_RENDER || !m->DeviceName ||
            !MultiByteToWideChar(CP_ACP, 0, m->DeviceName, -1, name, RTL_NUMBER_OF(name)) ||
            !find_wave_device(name, &m->DeviceId))
            return DSERR_NODRIVER;
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA m = data;
        if (m->DataFlow != DIRECTSOUNDDEVICE_DATAFLOW_RENDER || !m->DeviceName || !find_wave_device(m->DeviceName, &m->DeviceId))
            return DSERR_NODRIVER;
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA d = data;
        if (!find_device(&d->DeviceId, &e, &wave))
            return DSERR_NODRIVER;
        describe_1(p, d, &e, wave);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA d = data;
        if (!find_device(&d->DeviceId, &e, &wave))
            return DSERR_NODRIVER;
        describe_a(p, d, &e, wave);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d = data;
        if (!find_device(&d->DeviceId, &e, &wave))
            return DSERR_NODRIVER;
        describe_w(p, d, &e, wave);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA request = data;
        if (!request->Callback)
            return DSERR_INVALIDPARAM;
        MimeAudioEndpoint *all;
        UINT32 count = outputs(&all);
        for (UINT32 i = 0; i < count; i++) {
            DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA d = {};
            describe_1(p, &d, &all[i], i);
            if (!request->Callback(&d, request->Context))
                break;
        }
        if (all)
            HeapFree(GetProcessHeap(), 0, all);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA request = data;
        if (!request->Callback)
            return DSERR_INVALIDPARAM;
        MimeAudioEndpoint *all;
        UINT32 count = outputs(&all);
        for (UINT32 i = 0; i < count; i++) {
            DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA d = {};
            describe_a(p, &d, &all[i], i);
            if (!request->Callback(&d, request->Context))
                break;
        }
        if (all)
            HeapFree(GetProcessHeap(), 0, all);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA request = data;
        if (!request->Callback)
            return DSERR_INVALIDPARAM;
        MimeAudioEndpoint *all;
        UINT32 count = outputs(&all);
        for (UINT32 i = 0; i < count; i++) {
            DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d = {};
            describe_w(p, &d, &all[i], i);
            if (!request->Callback(&d, request->Context))
                break;
        }
        if (all)
            HeapFree(GetProcessHeap(), 0, all);
        break;
    }
    }
    if (returned)
        *returned = g_sizes[id];
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE private_set(IKsPropertySet *This, REFGUID set, ULONG id, LPVOID instance,
                                             ULONG instance_size, LPVOID data, ULONG data_size)
{
    if (!set)
        return DSERR_INVALIDPARAM;
    if (!IsEqualGUID(set, &DSPROPSETID_DirectSoundDevice))
        return E_PROP_SET_UNSUPPORTED;
    return E_PROP_ID_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE private_support(IKsPropertySet *This, REFGUID set, ULONG id, PULONG support)
{
    if (!set || !support)
        return DSERR_INVALIDPARAM;
    *support = 0;
    if (!IsEqualGUID(set, &DSPROPSETID_DirectSoundDevice))
        return E_PROP_SET_UNSUPPORTED;
    if (!id || id >= RTL_NUMBER_OF(g_sizes))
        return E_PROP_ID_UNSUPPORTED;
    *support = KSPROPERTY_SUPPORT_GET;
    return DS_OK;
}

static const IKsPropertySetVtbl private_vtbl = {private_query, private_add_ref, private_release,
                                                private_get,   private_set,     private_support};
