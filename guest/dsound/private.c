/*
 * CLSID_DirectSoundPrivate: the property set dsound's own tools and
 * xinput read the devices with, DSPROPSETID_DirectSoundDevice. the one
 * render device mime has is described as windows describes a wdm one.
 */
#include "dsound_private.h"

typedef struct PRIVATE {
    IKsPropertySet iface;
    LONG ref;
    /* what the descriptions point at, alive as long as the object. */
    WCHAR description[0x100], module[MAX_PATH], interface[MAX_PATH];
    CHAR description_a[0x100], module_a[MAX_PATH], interface_a[MAX_PATH];
} PRIVATE;

static const IKsPropertySetVtbl private_vtbl;

/* the endpoint's interface path, as the audio stack names a render endpoint. */
static void fill_strings(PRIVATE *p)
{
    lstrcpynW(p->description, DsDeviceName, RTL_NUMBER_OF(p->description));
    lstrcpynW(p->module, DsDeviceModule, RTL_NUMBER_OF(p->module));
    wsprintfW(p->interface, L"\\\\?\\SWD#MMDEVAPI#%s#{e6327cad-dcec-4949-ae8a-991e976a79d2}", DsDeviceModule);
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
    fill_strings(p);
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

/* the render device, the only one: a default id or its own names it. */
static BOOL is_device(const GUID *id)
{
    GUID resolved;
    return IsEqualGUID(id, &GUID_NULL) || (DsResolveDevice(id, &resolved, FALSE) && IsEqualGUID(&resolved, &DsDeviceGuid));
}

static void describe_w(PRIVATE *p, PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d)
{
    d->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    d->DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    d->DeviceId = DsDeviceGuid;
    d->Description = p->description;
    d->Module = p->module;
    d->Interface = p->interface;
    d->WaveDeviceId = 0;
}

static void describe_a(PRIVATE *p, PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA d)
{
    d->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    d->DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    d->DeviceId = DsDeviceGuid;
    d->Description = p->description_a;
    d->Module = p->module_a;
    d->Interface = p->interface_a;
    d->WaveDeviceId = 0;
}

static void describe_1(PRIVATE *p, PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA d)
{
    d->DeviceId = DsDeviceGuid;
    lstrcpynA(d->DescriptionA, p->description_a, sizeof(d->DescriptionA));
    lstrcpynW(d->DescriptionW, p->description, RTL_NUMBER_OF(d->DescriptionW));
    lstrcpynA(d->ModuleA, p->module_a, sizeof(d->ModuleA));
    lstrcpynW(d->ModuleW, p->module, RTL_NUMBER_OF(d->ModuleW));
    d->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    d->DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    d->WaveDeviceId = 0;
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
    switch (id) {
    case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA m = data;
        if (m->DataFlow != DIRECTSOUNDDEVICE_DATAFLOW_RENDER || !m->DeviceName || lstrcmpA(m->DeviceName, p->description_a))
            return DSERR_NODRIVER;
        m->DeviceId = DsDeviceGuid;
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA m = data;
        if (m->DataFlow != DIRECTSOUNDDEVICE_DATAFLOW_RENDER || !m->DeviceName || lstrcmpW(m->DeviceName, p->description))
            return DSERR_NODRIVER;
        m->DeviceId = DsDeviceGuid;
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA d = data;
        if (!is_device(&d->DeviceId))
            return DSERR_NODRIVER;
        describe_1(p, d);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA d = data;
        if (!is_device(&d->DeviceId))
            return DSERR_NODRIVER;
        describe_a(p, d);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d = data;
        if (!is_device(&d->DeviceId))
            return DSERR_NODRIVER;
        describe_w(p, d);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA e = data;
        if (!e->Callback)
            return DSERR_INVALIDPARAM;
        DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA d = {};
        describe_1(p, &d);
        e->Callback(&d, e->Context);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA e = data;
        if (!e->Callback)
            return DSERR_INVALIDPARAM;
        DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA d = {};
        describe_a(p, &d);
        e->Callback(&d, e->Context);
        break;
    }
    case DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W: {
        PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA e = data;
        if (!e->Callback)
            return DSERR_INVALIDPARAM;
        DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d = {};
        describe_w(p, &d);
        e->Callback(&d, e->Context);
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
