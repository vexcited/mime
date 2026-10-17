/*
 * direct3d 1 to 3, d3dim.dll's, read from build 26300: the direct3d
 * object of a directdraw object of the first versions, and its devices.
 * a device here is the seventh's core with what the first interfaces add:
 * viewports, light states and materials by handle, the legacy render
 * states mapped to texture stages as the rasterizers map them, vertex
 * types, Begin and End, and lighting with the old light models, done on
 * the cpu before the backend transforms and clips.
 */
#include "d3d_private.h"

static const IDirect3DVtbl d3d1_vtbl;
static const IDirect3D2Vtbl d3d2_vtbl;
static const IDirect3D3Vtbl d3d3_vtbl;

static void apply_blend(D3DDEV *dev);

static D3DOBJ *obj_from(void *iface)
{
    void **vtbl = iface;
    if (!vtbl)
        return nullptr;
    if (*vtbl == &d3d1_vtbl)
        return CONTAINING_RECORD(iface, D3DOBJ, iface1);
    if (*vtbl == &d3d2_vtbl)
        return CONTAINING_RECORD(iface, D3DOBJ, iface2);
    if (*vtbl == &d3d3_vtbl)
        return CONTAINING_RECORD(iface, D3DOBJ, iface3);
    return nullptr;
}

void D3dLegacyInitObj(D3DOBJ *d3d)
{
    d3d->iface1.lpVtbl = &d3d1_vtbl;
    d3d->iface2.lpVtbl = &d3d2_vtbl;
    d3d->iface3.lpVtbl = &d3d3_vtbl;
}

static BOOL is_mmx(const GUID *guid)
{
    return IsEqualGUID(guid, &IID_IDirect3DMMXDevice);
}

static const GUID IID_IDirect3DNewRGBDevice = {0x15B29400, 0x2ABF, 0x11D1, {0x89, 0xAA, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29}};

static BOOL legacy_guid(const GUID *guid)
{
    return IsEqualGUID(guid, &IID_IDirect3DRampDevice) || IsEqualGUID(guid, &IID_IDirect3DRGBDevice) ||
           IsEqualGUID(guid, &IID_IDirect3DMMXDevice) || IsEqualGUID(guid, &IID_IDirect3DHALDevice) ||
           IsEqualGUID(guid, &IID_IDirect3DRefDevice) || IsEqualGUID(guid, &IID_IDirect3DNullDevice) ||
           IsEqualGUID(guid, &IID_IDirect3DNewRGBDevice);
}

/* ------------------------------------------------------------------ descriptions */

static void null_desc(D3DDEVICEDESC *d)
{
    memset(d, 0, sizeof(*d));
    d->dwSize = sizeof(*d);
    d->dtcTransformCaps.dwSize = sizeof(D3DTRANSFORMCAPS);
    d->dlcLightingCaps.dwSize = sizeof(D3DLIGHTINGCAPS);
    d->dpcLineCaps.dwSize = d->dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
}

/*
 * the hardware and software descriptions of a device for an interface
 * version, d3dim's for its rasterizers and a current driver's through
 * ddraw's emulation for the hardware device, guessed as the driver's own.
 */
BOOL D3dLegacyDesc(const GUID *guid, DWORD version, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw)
{
    null_desc(hw);
    null_desc(sw);
    if (IsEqualGUID(guid, &IID_IDirect3DHALDevice)) {
        hw->dwFlags = 0x7FF;
        hw->dcmColorModel = D3DCOLOR_RGB;
        hw->dwDevCaps = 0x0001AE70;
        hw->dtcTransformCaps.dwCaps = D3DTRANSFORMCAPS_CLIP;
        hw->bClipping = TRUE;
        hw->dlcLightingCaps = (D3DLIGHTINGCAPS){sizeof(D3DLIGHTINGCAPS), 7, D3DLIGHTINGMODEL_RGB, 8};
        D3dPrimCaps(&hw->dpcTriCaps, TRUE);
        hw->dpcLineCaps = hw->dpcTriCaps;
        hw->dwDeviceRenderBitDepth = DDBD_16 | DDBD_32;
        hw->dwDeviceZBufferBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
        hw->dwMaxVertexCount = 0xFFFF;
        hw->dwMinTextureWidth = hw->dwMinTextureHeight = 1;
        hw->dwMaxTextureWidth = hw->dwMaxTextureHeight = 2048;
        hw->dwMaxTextureRepeat = 8192;
        hw->dwMaxTextureAspectRatio = 2048;
        hw->dwMaxAnisotropy = 16;
        hw->dvGuardBandLeft = hw->dvGuardBandTop = -32768.0f;
        hw->dvGuardBandRight = hw->dvGuardBandBottom = 32767.0f;
        hw->dwStencilCaps = 0xFF;
        hw->dwFVFCaps = 8;
        hw->dwTextureOpCaps = 0x00FEFFFF;
        hw->wMaxTextureBlendStages = hw->wMaxSimultaneousTextures = 8;
        /* the software side of a hardware device: transform and lighting alone. */
        sw->dwFlags = 0x1F;
        sw->dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX;
        sw->dtcTransformCaps.dwCaps = D3DTRANSFORMCAPS_CLIP;
        sw->bClipping = TRUE;
        sw->dlcLightingCaps = (D3DLIGHTINGCAPS){sizeof(D3DLIGHTINGCAPS), 0xF, D3DLIGHTINGMODEL_RGB, 0};
        sw->dwMaxVertexCount = hw->dwMaxVertexCount;
        return TRUE;
    }
    BOOL ramp = IsEqualGUID(guid, &IID_IDirect3DRampDevice);
    if (!ramp && !IsEqualGUID(guid, &IID_IDirect3DRGBDevice) && !is_mmx(guid) &&
        !IsEqualGUID(guid, &IID_IDirect3DNewRGBDevice))
        return FALSE;
    sw->dwFlags = 0x7DF;
    sw->dcmColorModel = ramp ? D3DCOLOR_MONO : D3DCOLOR_RGB;
    sw->dwDevCaps = 0x55B;
    sw->dtcTransformCaps.dwCaps = D3DTRANSFORMCAPS_CLIP;
    sw->bClipping = TRUE;
    sw->dlcLightingCaps = (D3DLIGHTINGCAPS){sizeof(D3DLIGHTINGCAPS), 0xF,
                                            ramp ? D3DLIGHTINGMODEL_MONO : D3DLIGHTINGMODEL_RGB, 0};
    D3DPRIMCAPS *c = &sw->dpcTriCaps;
    c->dwMiscCaps = 0x70;
    c->dwRasterCaps = ramp ? 0x51 : 0x1D1;
    c->dwZCmpCaps = 0xFF;
    c->dwShadeCaps = ramp ? 0xA145 : 0xCA28A;
    c->dwTextureCaps = 0xB;
    c->dwTextureFilterCaps = ramp ? 5 : 0x1F;
    c->dwTextureBlendCaps = 0x42;
    c->dwTextureAddressCaps = 1;
    c->dwStippleWidth = c->dwStippleHeight = 4;
    sw->dwDeviceRenderBitDepth = 0xF00;
    sw->dwDeviceZBufferBitDepth = DDBD_16;
    sw->dwMaxVertexCount = 2021;
    if (version == 3 && !ramp) {
        /* the description of directx 6's rasterizer. */
        sw->dwFlags = 0x7FF;
        sw->dwDevCaps = 0x551;
        c->dwRasterCaps = 0x121F7;
        c->dwSrcBlendCaps = 0x1FFF;
        c->dwDestBlendCaps = 0x7FF;
        c->dwAlphaCmpCaps = 0xFF;
        c->dwShadeCaps = 0xC528A;
        c->dwTextureCaps = 0xDF;
        c->dwTextureFilterCaps = 0x0303033F;
        c->dwTextureBlendCaps = 0xCF;
        c->dwTextureAddressCaps = 0x1F;
        sw->dwMinTextureWidth = sw->dwMinTextureHeight = 1;
        sw->dwMaxTextureWidth = sw->dwMaxTextureHeight = 1024;
        sw->dwMaxTextureRepeat = 256;
        sw->dwMaxAnisotropy = 1;
        sw->dvGuardBandLeft = sw->dvGuardBandTop = -32768.0f;
        sw->dvGuardBandRight = sw->dvGuardBandBottom = 32767.0f;
        sw->dwStencilCaps = 0xFF;
        sw->dwFVFCaps = 2;
        sw->dwTextureOpCaps = 0x78FF;
        sw->wMaxTextureBlendStages = sw->wMaxSimultaneousTextures = 2;
    }
    sw->dpcLineCaps = *c;
    return TRUE;
}

static DWORD desc_size(DWORD version)
{
    return version == 1 ? D3DDEVICEDESCSIZE_V1 : version == 2 ? D3DDEVICEDESCSIZE_V2 : sizeof(D3DDEVICEDESC);
}

static DWORD registry_dword(const WCHAR *name, BOOL *present)
{
    DWORD value = 0, size = sizeof(value);
    BOOL ok = !RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Direct3D\\Drivers", name, RRF_RT_REG_DWORD,
                            nullptr, &value, &size);
    if (present)
        *present = ok;
    return ok ? value : 0;
}

typedef HRESULT (*ENUM_FN)(const GUID *guid, char *description, char *name, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw,
                           void *context);

/* the devices of an interface version, in d3dim's order. */
static void enumerate(DWORD version, ENUM_FN fn, void *context)
{
    static const struct {
        const GUID *guid;
        char name[24], description[72];
    } DEVICES[] = {
        {&IID_IDirect3DRampDevice, "Ramp Emulation", "Microsoft Direct3D Mono(Ramp) Software Emulation"},
        {&IID_IDirect3DRGBDevice, "RGB Emulation", "Microsoft Direct3D RGB Software Emulation"},
        {&IID_IDirect3DHALDevice, "Direct3D HAL", "Microsoft Direct3D Hardware acceleration through Direct3D HAL"},
        {&IID_IDirect3DMMXDevice, "MMX Emulation", "Microsoft Direct3D MMX Software Emulation"},
    };
    BOOL software_only = registry_dword(L"SoftwareOnly", nullptr) != 0;
    BOOL separate_mmx = version < 3 || registry_dword(L"EnumSeparateMMX", nullptr) != 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(DEVICES); i++) {
        const GUID *guid = DEVICES[i].guid;
        if (is_mmx(guid) && (version == 1 || !separate_mmx))
            continue;
        if (IsEqualGUID(guid, &IID_IDirect3DRampDevice) && version == 3)
            continue;
        if (IsEqualGUID(guid, &IID_IDirect3DHALDevice) && software_only)
            continue;
        D3DDEVICEDESC hw, sw;
        D3dLegacyDesc(guid, version, &hw, &sw);
        hw.dwSize = sw.dwSize = desc_size(version);
        char name[24], description[72];
        strcpy(name, DEVICES[i].name);
        strcpy(description, DEVICES[i].description);
        if (fn(guid, description, name, &hw, &sw, context) != D3DENUMRET_OK)
            break;
    }
}

typedef struct ENUM_CONTEXT {
    LPD3DENUMDEVICESCALLBACK callback;
    void *context;
} ENUM_CONTEXT;

static HRESULT enum_forward(const GUID *guid, char *description, char *name, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw,
                            void *context)
{
    ENUM_CONTEXT *c = context;
    GUID copy = *guid;
    return c->callback(&copy, description, name, hw, sw, c->context);
}

static HRESULT enum_devices(DWORD version, LPD3DENUMDEVICESCALLBACK callback, LPVOID context)
{
    if (!callback)
        return DDERR_INVALIDPARAMS;
    ENUM_CONTEXT c = {callback, context};
    enumerate(version, enum_forward, &c);
    return D3D_OK;
}

typedef struct FIND_CONTEXT {
    const D3DFINDDEVICESEARCH *search;
    BOOL found, hardware;
    GUID guid;
    D3DDEVICEDESC hw, sw;
} FIND_CONTEXT;

static BOOL caps_match(const D3DFINDDEVICESEARCH *s, const D3DPRIMCAPS *c)
{
    static const struct {
        DWORD flag, offset;
    } FIELDS[] = {
        {D3DFDS_MISCCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwMiscCaps)},
        {D3DFDS_RASTERCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwRasterCaps)},
        {D3DFDS_ZCMPCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwZCmpCaps)},
        {D3DFDS_ALPHACMPCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwAlphaCmpCaps)},
        {D3DFDS_SRCBLENDCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwSrcBlendCaps)},
        {D3DFDS_DSTBLENDCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwDestBlendCaps)},
        {D3DFDS_SHADECAPS, FIELD_OFFSET(D3DPRIMCAPS, dwShadeCaps)},
        {D3DFDS_TEXTURECAPS, FIELD_OFFSET(D3DPRIMCAPS, dwTextureCaps)},
        {D3DFDS_TEXTUREFILTERCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwTextureFilterCaps)},
        {D3DFDS_TEXTUREBLENDCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwTextureBlendCaps)},
        {D3DFDS_TEXTUREADDRESSCAPS, FIELD_OFFSET(D3DPRIMCAPS, dwTextureAddressCaps)},
    };
    for (UINT i = 0; i < RTL_NUMBER_OF(FIELDS); i++) {
        if (!(s->dwFlags & FIELDS[i].flag))
            continue;
        DWORD want = *(const DWORD *)((const BYTE *)&s->dpcPrimCaps + FIELDS[i].offset);
        DWORD have = *(const DWORD *)((const BYTE *)c + FIELDS[i].offset);
        if ((have & want) != want)
            return FALSE;
    }
    return TRUE;
}

/* a hardware match beats a software one; among each kind the last matching wins. */
static HRESULT find_one(const GUID *guid, char *description, char *name, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw,
                        void *context)
{
    FIND_CONTEXT *c = context;
    const D3DFINDDEVICESEARCH *s = c->search;
    BOOL hardware = hw->dcmColorModel != 0;
    if ((s->dwFlags & D3DFDS_GUID) && !IsEqualGUID(guid, &s->guid))
        return D3DENUMRET_OK;
    if ((s->dwFlags & D3DFDS_HARDWARE) && !s->bHardware != !hardware)
        return D3DENUMRET_OK;
    if ((s->dwFlags & D3DFDS_COLORMODEL) && !(s->dcmColorModel & (hw->dcmColorModel | sw->dcmColorModel)))
        return D3DENUMRET_OK;
    if ((s->dwFlags & D3DFDS_TRIANGLES) && !caps_match(s, &hw->dpcTriCaps) && !caps_match(s, &sw->dpcTriCaps))
        return D3DENUMRET_OK;
    if ((s->dwFlags & D3DFDS_LINES) && !caps_match(s, &hw->dpcLineCaps) && !caps_match(s, &sw->dpcLineCaps))
        return D3DENUMRET_OK;
    if (c->found && c->hardware && !hardware)
        return D3DENUMRET_OK;
    c->found = TRUE;
    c->hardware = hardware;
    c->guid = *guid;
    c->hw = *hw;
    c->sw = *sw;
    return D3DENUMRET_OK;
}

static HRESULT find_device(DWORD version, LPD3DFINDDEVICESEARCH search, LPD3DFINDDEVICERESULT result)
{
    if (!search || search->dwSize != sizeof(*search) || !result ||
        (result->dwSize != sizeof(*result) && result->dwSize != 364 && result->dwSize != 428))
        return DDERR_INVALIDPARAMS;
    FIND_CONTEXT c = {search};
    enumerate(version, find_one, &c);
    if (!c.found)
        return DDERR_NOTFOUND;
    result->guid = c.guid;
    if (result->dwSize == sizeof(*result)) {
        result->ddHwDesc = c.hw;
        result->ddSwDesc = c.sw;
    } else {
        DWORD each = (result->dwSize - 20) / 2;
        memcpy(&result->ddHwDesc, &c.hw, each);
        memcpy((BYTE *)&result->ddHwDesc + each, &c.sw, each);
    }
    return D3D_OK;
}

/* ------------------------------------------------------------------ the direct3d object */

static IUnknown *outer_of(D3DOBJ *d3d)
{
    return DdIface(d3d->dd, d3d->outer);
}

static HRESULT obj_query(D3DOBJ *d3d, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    IUnknown *outer = outer_of(d3d);
    return outer->lpVtbl->QueryInterface(outer, riid, out);
}

static ULONG obj_add_ref(D3DOBJ *d3d)
{
    IUnknown *outer = outer_of(d3d);
    return outer->lpVtbl->AddRef(outer);
}

static ULONG obj_release(D3DOBJ *d3d)
{
    IUnknown *outer = outer_of(d3d);
    return outer->lpVtbl->Release(outer);
}

static HRESULT create_light(D3DOBJ *d3d, LPDIRECT3DLIGHT *out, IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    DdEnter();
    D3DLIGHTOBJ *l;
    HRESULT hr = D3dCreateLightObj(d3d, &l);
    if (SUCCEEDED(hr))
        *out = &l->iface;
    return DdLeave(hr);
}

static HRESULT create_material(D3DOBJ *d3d, D3DMATERIALOBJ **material, IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    DdEnter();
    return DdLeave(D3dCreateMaterialObj(d3d, material));
}

static HRESULT create_viewport(D3DOBJ *d3d, LPDIRECT3DVIEWPORT3 *out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    DdEnter();
    D3DVIEWPORTOBJ *vp;
    HRESULT hr = D3dCreateViewport(d3d, &vp);
    if (SUCCEEDED(hr))
        *out = &vp->iface;
    return DdLeave(hr);
}

static HRESULT create_device(D3DOBJ *d3d, const GUID *guid, DDSURF *target, DWORD version, IUnknown *outer,
                             D3DDEV **out);

static HRESULT STDMETHODCALLTYPE d3d1_query(IDirect3D *This, REFIID riid, void **out)
{
    return obj_query(obj_from(This), riid, out);
}

static ULONG STDMETHODCALLTYPE d3d1_add_ref(IDirect3D *This)
{
    return obj_add_ref(obj_from(This));
}

static ULONG STDMETHODCALLTYPE d3d1_release(IDirect3D *This)
{
    return obj_release(obj_from(This));
}

static HRESULT STDMETHODCALLTYPE d3d1_initialize(IDirect3D *This, REFCLSID clsid)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE d3d1_enum_devices(IDirect3D *This, LPD3DENUMDEVICESCALLBACK callback, LPVOID context)
{
    return enum_devices(1, callback, context);
}

static HRESULT STDMETHODCALLTYPE d3d1_create_light(IDirect3D *This, LPDIRECT3DLIGHT *light, IUnknown *outer)
{
    return create_light(obj_from(This), light, outer);
}

static HRESULT STDMETHODCALLTYPE d3d1_create_material(IDirect3D *This, LPDIRECT3DMATERIAL *material, IUnknown *outer)
{
    if (!material)
        return DDERR_INVALIDPARAMS;
    *material = nullptr;
    D3DMATERIALOBJ *m;
    HRESULT hr = create_material(obj_from(This), &m, outer);
    if (SUCCEEDED(hr))
        *material = &m->iface1;
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d1_create_viewport(IDirect3D *This, LPDIRECT3DVIEWPORT *viewport, IUnknown *outer)
{
    return create_viewport(obj_from(This), (LPDIRECT3DVIEWPORT3 *)viewport);
}

static HRESULT STDMETHODCALLTYPE d3d1_find_device(IDirect3D *This, LPD3DFINDDEVICESEARCH search,
                                                  LPD3DFINDDEVICERESULT result)
{
    return find_device(1, search, result);
}

static HRESULT STDMETHODCALLTYPE d3d2_query(IDirect3D2 *This, REFIID riid, void **out)
{
    return obj_query(obj_from(This), riid, out);
}

static ULONG STDMETHODCALLTYPE d3d2_add_ref(IDirect3D2 *This)
{
    return obj_add_ref(obj_from(This));
}

static ULONG STDMETHODCALLTYPE d3d2_release(IDirect3D2 *This)
{
    return obj_release(obj_from(This));
}

static HRESULT STDMETHODCALLTYPE d3d2_enum_devices(IDirect3D2 *This, LPD3DENUMDEVICESCALLBACK callback,
                                                   LPVOID context)
{
    return enum_devices(2, callback, context);
}

static HRESULT STDMETHODCALLTYPE d3d2_create_light(IDirect3D2 *This, LPDIRECT3DLIGHT *light, IUnknown *outer)
{
    return create_light(obj_from(This), light, outer);
}

static HRESULT STDMETHODCALLTYPE d3d2_create_material(IDirect3D2 *This, LPDIRECT3DMATERIAL2 *material,
                                                      IUnknown *outer)
{
    if (!material)
        return DDERR_INVALIDPARAMS;
    *material = nullptr;
    D3DMATERIALOBJ *m;
    HRESULT hr = create_material(obj_from(This), &m, outer);
    if (SUCCEEDED(hr))
        *material = &m->iface2;
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d2_create_viewport(IDirect3D2 *This, LPDIRECT3DVIEWPORT2 *viewport,
                                                      IUnknown *outer)
{
    return create_viewport(obj_from(This), (LPDIRECT3DVIEWPORT3 *)viewport);
}

static HRESULT STDMETHODCALLTYPE d3d2_find_device(IDirect3D2 *This, LPD3DFINDDEVICESEARCH search,
                                                  LPD3DFINDDEVICERESULT result)
{
    return find_device(2, search, result);
}

static HRESULT STDMETHODCALLTYPE d3d2_create_device(IDirect3D2 *This, REFCLSID clsid, LPDIRECTDRAWSURFACE surface,
                                                    LPDIRECT3DDEVICE2 *device)
{
    D3DOBJ *d3d = obj_from(This);
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    if (!device)
        return DDERR_INVALIDPARAMS;
    *device = nullptr;
    DDSURF *target = DdSurfFromIface(surface);
    if (!target)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    D3DDEV *dev;
    HRESULT hr = create_device(d3d, clsid, target, 2, nullptr, &dev);
    if (SUCCEEDED(hr))
        *device = &dev->iface2;
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE d3d3_query(IDirect3D3 *This, REFIID riid, void **out)
{
    return obj_query(obj_from(This), riid, out);
}

static ULONG STDMETHODCALLTYPE d3d3_add_ref(IDirect3D3 *This)
{
    return obj_add_ref(obj_from(This));
}

static ULONG STDMETHODCALLTYPE d3d3_release(IDirect3D3 *This)
{
    return obj_release(obj_from(This));
}

static HRESULT STDMETHODCALLTYPE d3d3_enum_devices(IDirect3D3 *This, LPD3DENUMDEVICESCALLBACK callback,
                                                   LPVOID context)
{
    return enum_devices(3, callback, context);
}

static HRESULT STDMETHODCALLTYPE d3d3_create_light(IDirect3D3 *This, LPDIRECT3DLIGHT *light, IUnknown *outer)
{
    return create_light(obj_from(This), light, outer);
}

static HRESULT STDMETHODCALLTYPE d3d3_create_material(IDirect3D3 *This, LPDIRECT3DMATERIAL3 *material,
                                                      IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!material)
        return DDERR_INVALIDPARAMS;
    *material = nullptr;
    D3DMATERIALOBJ *m;
    HRESULT hr = create_material(obj_from(This), &m, nullptr);
    if (SUCCEEDED(hr))
        *material = &m->iface3;
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d3_create_viewport(IDirect3D3 *This, LPDIRECT3DVIEWPORT3 *viewport,
                                                      IUnknown *outer)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return create_viewport(obj_from(This), viewport);
}

static HRESULT STDMETHODCALLTYPE d3d3_find_device(IDirect3D3 *This, LPD3DFINDDEVICESEARCH search,
                                                  LPD3DFINDDEVICERESULT result)
{
    return find_device(3, search, result);
}

static HRESULT STDMETHODCALLTYPE d3d3_create_device(IDirect3D3 *This, REFCLSID clsid, LPDIRECTDRAWSURFACE4 surface,
                                                    LPDIRECT3DDEVICE3 *device, IUnknown *outer)
{
    D3DOBJ *d3d = obj_from(This);
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    if (!device)
        return DDERR_INVALIDPARAMS;
    if (!surface)
        return DDERR_INVALIDOBJECT;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    *device = nullptr;
    DDSURF *target = DdSurfFromIface(surface);
    if (!target)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    D3DDEV *dev;
    HRESULT hr = create_device(d3d, clsid, target, 3, nullptr, &dev);
    if (SUCCEEDED(hr))
        *device = &dev->iface3;
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE d3d3_create_vertex_buffer(IDirect3D3 *This, LPD3DVERTEXBUFFERDESC desc,
                                                           LPDIRECT3DVERTEXBUFFER *buffer, DWORD flags,
                                                           IUnknown *outer)
{
    D3DOBJ *d3d = obj_from(This);
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!desc || !buffer)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    return DdLeave(D3dCreateVertexBuffer1(d3d, desc, flags, buffer));
}

/* the rasterizers' formats, or the three of a driver; the ramp device has a 16-bit one. */
static HRESULT STDMETHODCALLTYPE d3d3_enum_zbuffer_formats(IDirect3D3 *This, REFCLSID device,
                                                           LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    if (!obj_from(This))
        return DDERR_INVALIDOBJECT;
    if (!callback || !device || !legacy_guid(device))
        return DDERR_INVALIDPARAMS;
    static const DDPIXELFORMAT FORMATS[] = {
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, {16}, {0}, {0xFFFF}},
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER | DDPF_STENCILBUFFER, 0, {32}, {8}, {0xFFFFFF00}, {0xFF}},
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, {32}, {0}, {0xFFFFFF00}},
    };
    static const DWORD HAL[] = {0, 2, 1}, SOFTWARE[] = {0, 1};
    const DWORD *order = nullptr;
    DWORD count = 0;
    if (IsEqualGUID(device, &IID_IDirect3DHALDevice)) {
        order = HAL;
        count = 3;
    } else if (IsEqualGUID(device, &IID_IDirect3DRampDevice)) {
        order = SOFTWARE;
        count = 1;
    } else if (!IsEqualGUID(device, &IID_IDirect3DRefDevice) && !IsEqualGUID(device, &IID_IDirect3DNullDevice)) {
        order = SOFTWARE;
        count = 2;
    }
    for (DWORD i = 0; i < count; i++) {
        DDPIXELFORMAT copy = FORMATS[order[i]];
        if (callback(&copy, context) != D3DENUMRET_OK)
            break;
    }
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d3_evict_managed_textures(IDirect3D3 *This)
{
    return D3D_OK;
}

static const IDirect3DVtbl d3d1_vtbl = {
    d3d1_query,           d3d1_add_ref,         d3d1_release,         d3d1_initialize, d3d1_enum_devices,
    d3d1_create_light,    d3d1_create_material, d3d1_create_viewport, d3d1_find_device,
};

static const IDirect3D2Vtbl d3d2_vtbl = {
    d3d2_query,           d3d2_add_ref,         d3d2_release,     d3d2_enum_devices, d3d2_create_light,
    d3d2_create_material, d3d2_create_viewport, d3d2_find_device, d3d2_create_device,
};

static const IDirect3D3Vtbl d3d3_vtbl = {
    d3d3_query,
    d3d3_add_ref,
    d3d3_release,
    d3d3_enum_devices,
    d3d3_create_light,
    d3d3_create_material,
    d3d3_create_viewport,
    d3d3_find_device,
    d3d3_create_device,
    d3d3_create_vertex_buffer,
    d3d3_enum_zbuffer_formats,
    d3d3_evict_managed_textures,
};

/* ------------------------------------------------------------------ device creation */

static void legacy_state(D3DDEV *dev)
{
    D3DLEGACY *l = dev->legacy;
    memset(dev->rs, 0xFF, sizeof(dev->rs));
    memset(dev->tss, 0xFF, sizeof(dev->tss));
    BOOL old = dev->version < 3;
    static const D3DMATRIX IDENTITY = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (DWORD i = 0; i < RTL_NUMBER_OF(dev->transforms); i++)
        dev->transforms[i] = IDENTITY;
    l->clip_map = IDENTITY;
    for (DWORD stage = 0; stage < D3D_STAGES; stage++) {
        DWORD *t = dev->tss[stage];
        t[D3DTSS_COLOROP] = stage ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        t[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = stage ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        for (DWORD i = D3DTSS_BUMPENVMAT00; i <= D3DTSS_BUMPENVMAT11; i++)
            t[i] = 0;
        t[D3DTSS_TEXCOORDINDEX] = 0;
        t[D3DTSS_ADDRESS] = t[D3DTSS_ADDRESSU] = t[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        t[D3DTSS_BORDERCOLOR] = 0;
        t[D3DTSS_MAGFILTER] = D3DTFG_POINT;
        t[D3DTSS_MINFILTER] = D3DTFN_POINT;
        t[D3DTSS_MIPFILTER] = D3DTFP_NONE;
        t[D3DTSS_MIPMAPLODBIAS] = t[D3DTSS_MAXMIPLEVEL] = 0;
        t[D3DTSS_MAXANISOTROPY] = 1;
        t[D3DTSS_BUMPENVLSCALE] = t[D3DTSS_BUMPENVLOFFSET] = 0;
    }
    BOOL mono = !((l->hw.dwFlags & D3DDD_COLORMODEL ? l->hw.dcmColorModel : l->sw.dcmColorModel) & D3DCOLOR_RGB);
    const DWORD DEFAULTS[][2] = {
        {D3DRENDERSTATE_TEXTUREHANDLE, 0},
        {D3DRENDERSTATE_ANTIALIAS, 0},
        {D3DRENDERSTATE_TEXTUREADDRESS, D3DTADDRESS_WRAP},
        {D3DRENDERSTATE_TEXTUREPERSPECTIVE, old ? 0 : 1},
        {D3DRENDERSTATE_SPECULARENABLE, old ? 1 : 0},
        {D3DRENDERSTATE_WRAPU, 0},
        {D3DRENDERSTATE_WRAPV, 0},
        {D3DRENDERSTATE_ZENABLE, dev->depth ? 1 : 0},
        {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID},
        {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
        {D3DRENDERSTATE_LINEPATTERN, 0},
        {D3DRENDERSTATE_ROP2, 13},
        {D3DRENDERSTATE_PLANEMASK, 0xFFFFFFFF},
        {D3DRENDERSTATE_ZWRITEENABLE, 1},
        {D3DRENDERSTATE_ALPHATESTENABLE, 0},
        {D3DRENDERSTATE_LASTPIXEL, 1},
        {D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE},
        {D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO},
        {D3DRENDERSTATE_CULLMODE, D3DCULL_CCW},
        {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRENDERSTATE_ALPHAREF, 0},
        {D3DRENDERSTATE_ALPHAFUNC, D3DCMP_ALWAYS},
        {D3DRENDERSTATE_DITHERENABLE, 0},
        {D3DRENDERSTATE_ALPHABLENDENABLE, 0},
        {D3DRENDERSTATE_FOGENABLE, 0},
        {D3DRENDERSTATE_ZVISIBLE, 0},
        {D3DRENDERSTATE_SUBPIXEL, 0},
        {D3DRENDERSTATE_SUBPIXELX, 0},
        {D3DRENDERSTATE_STIPPLEDALPHA, 0},
        {D3DRENDERSTATE_FOGCOLOR, 0},
        {D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_NONE},
        {D3DRENDERSTATE_FOGTABLESTART, 0},
        {D3DRENDERSTATE_FOGTABLEEND, 0x3F800000},
        {D3DRENDERSTATE_FOGTABLEDENSITY, 0x3F800000},
        {D3DRENDERSTATE_STIPPLEENABLE, 0},
        {D3DRENDERSTATE_EDGEANTIALIAS, 0},
        {D3DRENDERSTATE_COLORKEYENABLE, dev->version == 1 && dev->hal},
        {D3DRENDERSTATE_BORDERCOLOR, 0},
        {D3DRENDERSTATE_TEXTUREADDRESSU, D3DTADDRESS_WRAP},
        {D3DRENDERSTATE_TEXTUREADDRESSV, D3DTADDRESS_WRAP},
        {D3DRENDERSTATE_MIPMAPLODBIAS, 0},
        {D3DRENDERSTATE_ZBIAS, 0},
        {D3DRENDERSTATE_RANGEFOGENABLE, 0},
        {D3DRENDERSTATE_ANISOTROPY, 1},
        {D3DRENDERSTATE_STENCILENABLE, 0},
        {D3DRENDERSTATE_STENCILFAIL, D3DSTENCILOP_KEEP},
        {D3DRENDERSTATE_STENCILZFAIL, D3DSTENCILOP_KEEP},
        {D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP},
        {D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS},
        {D3DRENDERSTATE_STENCILREF, 0},
        {D3DRENDERSTATE_STENCILMASK, 0xFFFFFFFF},
        {D3DRENDERSTATE_STENCILWRITEMASK, 0xFFFFFFFF},
        {D3DRENDERSTATE_TEXTUREFACTOR, 0xFFFFFFFF},
        {D3DRENDERSTATE_MONOENABLE, mono},
        {D3DRENDERSTATE_TEXTUREMAG, D3DFILTER_NEAREST},
        {D3DRENDERSTATE_TEXTUREMIN, D3DFILTER_NEAREST},
        {D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATE},
    };
    for (UINT i = 0; i < RTL_NUMBER_OF(DEFAULTS); i++)
        dev->rs[DEFAULTS[i][0]] = DEFAULTS[i][1];
    for (DWORD i = D3DRENDERSTATE_STIPPLEPATTERN00; i <= D3DRENDERSTATE_STIPPLEPATTERN31; i++)
        dev->rs[i] = 0;
    for (DWORD i = D3DRENDERSTATE_WRAP0; i <= D3DRENDERSTATE_WRAP7; i++)
        dev->rs[i] = 0;
    /* what the stage states read back stays apart from the mapping of the legacy states, made now. */
    memcpy(l->api_tss, dev->tss, sizeof(l->api_tss));
    for (DWORD stage = 0; stage < D3D_STAGES; stage++)
        dev->tss[stage][D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
    apply_blend(dev);
    l->light_states[D3DLIGHTSTATE_COLORVERTEX] = TRUE;
    l->status = D3DSTATUS_DEFAULT;
    l->extents[0] = l->extents[1] = 2048.0f;
    dev->viewport = (D3DVIEWPORT7){0, 0, dev->target->desc.dwWidth, dev->target->desc.dwHeight, 0.0f, 1.0f};
    dev->stage_end = D3D_STAGES;
}

/* d3dim's checks of the surfaces, the hardware device needing video memory and the software system memory. */
static HRESULT check_surfaces(D3DDEV *dev, const GUID *guid, DDSURF *target, DDSURF *depth)
{
    D3DLEGACY *l = dev->legacy;
    if (l->hw.dwFlags && !(target->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY))
        return D3DERR_SURFACENOTINVIDMEM;
    if (target->format.pf.dwFlags & DDPF_RGB) {
        DWORD ddbd = D3dDdbd(target->format.bpp);
        if (!ddbd || !(ddbd & (l->hw.dwDeviceRenderBitDepth | l->sw.dwDeviceRenderBitDepth)))
            return DDERR_INVALIDPIXELFORMAT;
    }
    if (depth) {
        if (l->hw.dwFlags && !(depth->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY))
            return D3DERR_ZBUFF_NEEDS_VIDEOMEMORY;
        if (!l->hw.dwFlags && !(depth->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
            return D3DERR_ZBUFF_NEEDS_SYSTEMMEMORY;
        if (IsEqualGUID(guid, &IID_IDirect3DRampDevice) && (depth->format.pf.dwFlags & DDPF_ZPIXELS))
            return DDERR_INVALIDPARAMS;
        if ((depth->format.pf.dwFlags & DDPF_ZBUFFER) && !D3dDdbd(depth->format.pf.dwZBufferBitDepth))
            return DDERR_INVALIDPIXELFORMAT;
    }
    return D3D_OK;
}

static HRESULT create_device(D3DOBJ *d3d, const GUID *guid, DDSURF *target, DWORD version, IUnknown *outer,
                             D3DDEV **out)
{
    *out = nullptr;
    if (!guid || !legacy_guid(guid))
        return DDERR_INVALIDPARAMS;
    if (IsEqualGUID(guid, &IID_IDirect3DRampDevice) && version >= 3)
        return DDERR_INVALIDPARAMS;
    DWORD caps = target->desc.ddsCaps.dwCaps;
    if (!(caps & DDSCAPS_3DDEVICE) || (caps & DDSCAPS_ZBUFFER))
        return DDERR_INVALIDCAPS;
    if (target->desc.dwWidth > 2048 || target->desc.dwHeight > 2048)
        return DDERR_INVALIDOBJECT;
    HRESULT hr = D3dCheckPalette(target);
    if (FAILED(hr))
        return hr;
    DDSURF *depth = D3dAttachedDepth(target);
    D3DDEVICEDESC hw, sw;
    /* the reference and null devices are d3dref.dll's, which windows 11 lacks. */
    if (!D3dLegacyDesc(guid, version, &hw, &sw))
        return E_NOINTERFACE;
    DbDevice *db = DbDeviceGet();
    if (!db)
        return D3DERR_INITFAILED;
    D3DDEV *dev = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dev));
    D3DLEGACY *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*l));
    if (!dev || !l) {
        HeapFree(GetProcessHeap(), 0, dev);
        HeapFree(GetProcessHeap(), 0, l);
        return DDERR_OUTOFMEMORY;
    }
    D3dLegacyDeviceInit(dev);
    dev->outer = outer ? outer : &dev->inner;
    dev->ref = 1;
    dev->version = version;
    dev->surface_version = version == 1 ? 0 : version == 2 ? DDV1 : DDV4;
    dev->legacy = l;
    dev->d3d = d3d;
    dev->guid = *guid;
    dev->hal = IsEqualGUID(guid, &IID_IDirect3DHALDevice);
    dev->db = db;
    l->hw = hw;
    l->sw = sw;
    InitializeListHead(&l->viewports);
    InitializeListHead(&l->materials);
    InitializeListHead(&l->textures);
    InitializeListHead(&l->matrices);
    InitializeListHead(&l->buffers);
    InitializeListHead(&dev->links);
    if (FAILED(hr = check_surfaces(dev, guid, target, depth))) {
        HeapFree(GetProcessHeap(), 0, l);
        HeapFree(GetProcessHeap(), 0, dev);
        return hr;
    }
    D3dHoldTargets(dev, target, depth);
    legacy_state(dev);
    InsertTailList(&d3d->devices, &dev->links);
    if (version != 1)
        D3dObjAddRef(d3d);
    *out = dev;
    return D3D_OK;
}

/* a surface of a directdraw object of the first versions answers for its textures and first devices. */
HRESULT D3dSurfQuery(DDSURF *s, REFIID riid, void **out)
{
    for (D3DAGG *a = s->aggregates; a; a = a->next)
        if (SUCCEEDED(a->inner->lpVtbl->QueryInterface(a->inner, riid, out)))
            return DD_OK;
    if (!s->dd || s->dd->dx7)
        return E_NOINTERFACE;
    IUnknown *inner = nullptr;
    HRESULT hr = D3dCreateTextureForSurface(s, riid, &inner);
    if (hr == E_NOINTERFACE) {
        if (!legacy_guid(riid))
            return DDERR_INVALIDPARAMS;
        DDRAW *dd = s->dd;
        if (!dd->d3d) {
            void *unused;
            if (FAILED(hr = D3dObjQuery(dd, DDV1, &IID_IDirect3D, &unused)))
                return hr;
            /* the directdraw object holds its direct3d object, not the device. */
            IUnknown *outer = DdIface(dd, dd->d3d->outer);
            outer->lpVtbl->Release(outer);
        }
        D3DDEV *dev;
        hr = create_device(dd->d3d, riid, s, 1, DdSurfIface(s, DDV1), &dev);
        if (SUCCEEDED(hr))
            inner = &dev->inner;
    }
    if (FAILED(hr))
        return hr;
    D3DAGG *a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    if (!a) {
        inner->lpVtbl->Release(inner);
        return E_OUTOFMEMORY;
    }
    a->inner = inner;
    a->next = s->aggregates;
    s->aggregates = a;
    return SUCCEEDED(inner->lpVtbl->QueryInterface(inner, riid, out)) ? DD_OK : E_NOINTERFACE;
}

void D3dLegacyDestroy(D3DDEV *dev)
{
    D3DLEGACY *l = dev->legacy;
    l->in_begin = FALSE;
    while (!IsListEmpty(&l->viewports)) {
        D3DVIEWPORTOBJ *vp = CONTAINING_RECORD(l->viewports.Flink, D3DVIEWPORTOBJ, dev_links);
        dev->iface3.lpVtbl->DeleteViewport(&dev->iface3, &vp->iface);
    }
    if (l->current)
        l->current->iface.lpVtbl->Release(&l->current->iface);
    l->current = nullptr;
    while (!IsListEmpty(&l->buffers))
        D3dExecuteBufferDeviceGone(CONTAINING_RECORD(l->buffers.Flink, D3DEXECBUF, links));
    while (!IsListEmpty(&l->textures))
        D3dTextureDeviceGone(CONTAINING_RECORD(l->textures.Flink, D3DTEXHANDLE, dev_links));
    while (!IsListEmpty(&l->materials))
        D3dMaterialDeviceGone(CONTAINING_RECORD(l->materials.Flink, D3DMATHANDLE, dev_links));
    while (!IsListEmpty(&l->matrices)) {
        D3DMATRIXOBJ *m = CONTAINING_RECORD(l->matrices.Flink, D3DMATRIXOBJ, links);
        RemoveEntryList(&m->links);
        HeapFree(GetProcessHeap(), 0, m);
    }
    HeapFree(GetProcessHeap(), 0, l->begin_vertices);
    HeapFree(GetProcessHeap(), 0, l->begin_indices);
    HeapFree(GetProcessHeap(), 0, l->tl);
    HeapFree(GetProcessHeap(), 0, l->tl_clip);
    HeapFree(GetProcessHeap(), 0, l->tl_codes);
    HeapFree(GetProcessHeap(), 0, l->picks);
    HeapFree(GetProcessHeap(), 0, l->lit);
    HeapFree(GetProcessHeap(), 0, l);
    dev->legacy = nullptr;
}

/* ------------------------------------------------------------------ the backend's view of a first device */

const D3DMATRIX *D3dLegacyClipMap(D3DDEV *dev)
{
    return &dev->legacy->clip_map;
}

/* table fog takes the render states, vertex fog the light states, which direct3d 9 shares. */
static void push_fog(D3DDEV *dev)
{
    D3DLEGACY *l = dev->legacy;
    BOOL table = dev->rs[D3DRENDERSTATE_FOGTABLEMODE] != D3DFOG_NONE && dev->rs[D3DRENDERSTATE_FOGTABLEMODE] != 0xFFFFFFFF;
    DbSetRenderState(dev->db, D3DRENDERSTATE_FOGVERTEXMODE, table ? D3DFOG_NONE : l->light_states[D3DLIGHTSTATE_FOGMODE]);
    DbSetRenderState(dev->db, D3DRENDERSTATE_FOGSTART,
                     table ? dev->rs[D3DRENDERSTATE_FOGSTART] : l->light_states[D3DLIGHTSTATE_FOGSTART]);
    DbSetRenderState(dev->db, D3DRENDERSTATE_FOGEND,
                     table ? dev->rs[D3DRENDERSTATE_FOGEND] : l->light_states[D3DLIGHTSTATE_FOGEND]);
    DbSetRenderState(dev->db, D3DRENDERSTATE_FOGDENSITY,
                     table ? dev->rs[D3DRENDERSTATE_FOGDENSITY] : l->light_states[D3DLIGHTSTATE_FOGDENSITY]);
}

/* the first devices light on the cpu, so the backend never does. */
void D3dLegacyActivate(D3DDEV *dev)
{
    DbSetRenderState(dev->db, D3DRENDERSTATE_LIGHTING, FALSE);
    DbSetRenderState(dev->db, D3DRENDERSTATE_CLIPPING, TRUE);
    push_fog(dev);
}

/* the viewport's clip volume after the projection, and its rectangle on the target. */
HRESULT D3dLegacyDownload(D3DDEV *dev, D3DVIEWPORTOBJ *vp)
{
    if (!vp)
        return D3DERR_NOCURRENTVIEWPORT;
    if (!vp->set)
        return D3DERR_VIEWPORTDATANOTSET;
    D3DLEGACY *l = dev->legacy;
    const D3DVIEWPORT2 *v = &vp->data;
    l->downloaded = vp->id;
    vp->lights_dirty = TRUE;
    if (!v->dwWidth || !v->dwHeight || v->dvClipWidth == 0.0f || v->dvClipHeight == 0.0f ||
        v->dvMaxZ - v->dvMinZ == 0.0f)
        return D3D_OK;
    float cx = v->dvClipX, cy = v->dvClipY, cw = v->dvClipWidth, ch = v->dvClipHeight, zn = v->dvMinZ,
          zf = v->dvMaxZ;
    /* the clip volume to direct3d 9's: x from -w to w left to right, y from w to -w top to bottom, z from 0 to w. */
    l->clip_map = (D3DMATRIX){2.0f / cw, 0, 0, 0, 0, 2.0f / ch, 0, 0, 0, 0, 1.0f / (zf - zn), 0,
                              -2.0f * cx / cw - 1.0f, -2.0f * cy / ch + 1.0f, -zn / (zf - zn), 1};
    D3DVIEWPORT7 vp7 = {v->dwX, v->dwY, v->dwWidth, v->dwHeight, 0.0f, 1.0f};
    D3dSetViewport(dev, &vp7);
    D3dPushProjection(dev);
    return D3D_OK;
}

HRESULT D3dLegacyDownloadCurrent(D3DDEV *dev)
{
    D3DLEGACY *l = dev->legacy;
    if (!l->current)
        return D3DERR_NOCURRENTVIEWPORT;
    if (l->downloaded != l->current->id)
        return D3dLegacyDownload(dev, l->current);
    return D3D_OK;
}

/* ------------------------------------------------------------------ legacy states, mapped as the rasterizers map them */

/* the texture blend of the first interfaces as the stage states of stage 0. */
static void apply_blend(D3DDEV *dev)
{
    DWORD color = D3DTOP_SELECTARG1, alpha = D3DTOP_SELECTARG1, arg2 = 0xFFFFFFFF;
    switch (dev->rs[D3DRENDERSTATE_TEXTUREMAPBLEND]) {
    case D3DTBLEND_MODULATE:
    case D3DTBLEND_MODULATEMASK:
        color = D3DTOP_MODULATE;
        arg2 = D3DTA_DIFFUSE;
        break;
    case D3DTBLEND_DECALALPHA:
        color = D3DTOP_BLENDTEXTUREALPHA;
        alpha = D3DTOP_SELECTARG2;
        arg2 = D3DTA_DIFFUSE;
        break;
    case D3DTBLEND_MODULATEALPHA:
        color = alpha = D3DTOP_MODULATE;
        arg2 = D3DTA_DIFFUSE;
        break;
    case D3DTBLEND_ADD:
        color = D3DTOP_ADD;
        alpha = D3DTOP_SELECTARG2;
        arg2 = D3DTA_DIFFUSE;
        break;
    }
    D3dSetTss(dev, 0, D3DTSS_COLOROP, color);
    D3dSetTss(dev, 0, D3DTSS_ALPHAOP, alpha);
    D3dSetTss(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    D3dSetTss(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    if (arg2 != 0xFFFFFFFF) {
        D3dSetTss(dev, 0, D3DTSS_COLORARG2, arg2);
        D3dSetTss(dev, 0, D3DTSS_ALPHAARG2, arg2);
    }
    D3dSetTss(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

void D3dLegacySetTss(D3DDEV *dev, DWORD stage, DWORD type, DWORD value)
{
    dev->legacy->api_tss[stage][type] = value;
    D3dSetTss(dev, stage, type, value);
}

void D3dLegacySetTexture(D3DDEV *dev, DWORD stage, D3DTEXOBJ *texture)
{
    dev->legacy->stage_textures[stage] = texture;
    D3dSetTexture(dev, stage, texture ? texture->surface : nullptr);
}

static void apply_min_filter(D3DDEV *dev, DWORD value)
{
    static const DWORD MIN[][2] = {
        {D3DTFN_POINT, D3DTFP_NONE},  {D3DTFN_LINEAR, D3DTFP_NONE}, {D3DTFN_POINT, D3DTFP_POINT},
        {D3DTFN_LINEAR, D3DTFP_POINT}, {D3DTFN_POINT, D3DTFP_LINEAR}, {D3DTFN_LINEAR, D3DTFP_LINEAR},
    };
    if (value < D3DFILTER_NEAREST || value > D3DFILTER_LINEARMIPLINEAR)
        return;
    D3dSetTss(dev, 0, D3DTSS_MINFILTER, MIN[value - 1][0]);
    D3dSetTss(dev, 0, D3DTSS_MIPFILTER, MIN[value - 1][1]);
}

HRESULT D3dLegacySetRenderState(D3DDEV *dev, DWORD type, DWORD value)
{
    if (!type || type > 255)
        return DDERR_INVALIDPARAMS;
    if (type == D3DRENDERSTATE_FLUSHBATCH)
        return D3D_OK;
    /* the first texture coordinates' wrap flags live in WRAP0 too. */
    if (type == D3DRENDERSTATE_WRAPU || type == D3DRENDERSTATE_WRAPV) {
        DWORD bit = type == D3DRENDERSTATE_WRAPU ? D3DWRAP_U : D3DWRAP_V;
        DWORD wrap0 = value ? dev->rs[D3DRENDERSTATE_WRAP0] | bit : dev->rs[D3DRENDERSTATE_WRAP0] & ~bit;
        if (wrap0 != dev->rs[D3DRENDERSTATE_WRAP0])
            D3dSetRs(dev, D3DRENDERSTATE_WRAP0, wrap0);
    }
    if (dev->rs[type] == value)
        return D3D_OK;
    D3dSetRs(dev, type, value);
    switch (type) {
    case D3DRENDERSTATE_TEXTUREHANDLE: {
        D3DTEXHANDLE *h = D3dTextureHandle(dev, value);
        D3dSetTextureWeak(dev, 0, h ? h->texture->surface : nullptr);
        D3dSetTexture(dev, 1, nullptr);
        apply_blend(dev);
        break;
    }
    case D3DRENDERSTATE_TEXTUREADDRESS:
        D3dSetTss(dev, 0, D3DTSS_ADDRESS, value);
        D3dSetTss(dev, 0, D3DTSS_ADDRESSU, value);
        D3dSetTss(dev, 0, D3DTSS_ADDRESSV, value);
        break;
    case D3DRENDERSTATE_TEXTUREADDRESSU:
        D3dSetTss(dev, 0, D3DTSS_ADDRESSU, value);
        break;
    case D3DRENDERSTATE_TEXTUREADDRESSV:
        D3dSetTss(dev, 0, D3DTSS_ADDRESSV, value);
        break;
    case D3DRENDERSTATE_BORDERCOLOR:
        D3dSetTss(dev, 0, D3DTSS_BORDERCOLOR, value);
        break;
    case D3DRENDERSTATE_MIPMAPLODBIAS:
        D3dSetTss(dev, 0, D3DTSS_MIPMAPLODBIAS, value);
        break;
    case D3DRENDERSTATE_ANISOTROPY:
        D3dSetTss(dev, 0, D3DTSS_MAXANISOTROPY, value);
        break;
    case D3DRENDERSTATE_TEXTUREMAG:
        D3dSetTss(dev, 0, D3DTSS_MAGFILTER, value == D3DFILTER_LINEAR ? D3DTFG_LINEAR : D3DTFG_POINT);
        break;
    case D3DRENDERSTATE_TEXTUREMIN:
        apply_min_filter(dev, value);
        break;
    case D3DRENDERSTATE_TEXTUREMAPBLEND:
        apply_blend(dev);
        break;
    case D3DRENDERSTATE_FOGTABLEMODE:
    case D3DRENDERSTATE_FOGTABLESTART:
    case D3DRENDERSTATE_FOGTABLEEND:
    case D3DRENDERSTATE_FOGTABLEDENSITY:
        if (D3dDevIsActive(dev))
            push_fog(dev);
        break;
    }
    return D3D_OK;
}

/* the light states of the first interfaces, and the override bits of execute buffers. */
HRESULT D3dLegacySetLightState(D3DDEV *dev, DWORD type, DWORD value)
{
    D3DLEGACY *l = dev->legacy;
    if (type > D3DSTATE_OVERRIDE_BIAS && type <= D3DSTATE_OVERRIDE_BIAS + D3DLIGHTSTATE_COLORVERTEX) {
        DWORD bit = 1u << (type - D3DSTATE_OVERRIDE_BIAS);
        l->light_locks = value ? l->light_locks | bit : l->light_locks & ~bit;
        return D3D_OK;
    }
    if (!type || type > D3DLIGHTSTATE_COLORVERTEX)
        return DDERR_INVALIDPARAMS;
    if (l->light_locks >> type & 1)
        return D3D_OK;
    if (type == D3DLIGHTSTATE_MATERIAL) {
        if (value == l->light_states[type])
            return D3D_OK;
        D3DMATHANDLE *h = D3dMaterialHandle(dev, value);
        if (h)
            l->material = h->data;
    }
    l->light_states[type] = value;
    if (type >= D3DLIGHTSTATE_FOGMODE && type <= D3DLIGHTSTATE_FOGDENSITY && D3dDevIsActive(dev))
        push_fog(dev);
    return D3D_OK;
}
