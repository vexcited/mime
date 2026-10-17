/*
 * the direct3d object of a directdraw object, made at the first query
 * for it and living as long as the directdraw object does, whose count
 * it shares, as d3dim700.dll of build 26300 has it. its devices are the
 * software rasterizer windows offers, "RGB Emulation", and the hardware
 * ones of a driver, all drawn on the gpu here.
 */
#include "d3d_private.h"

/* the floats of direct3d need no runtime of the compiler's. */
int _fltused = 0x9875;

static const IDirect3D7Vtbl d3d7_vtbl;

D3DOBJ *D3dObjFromIface7(IDirect3D7 *iface)
{
    return iface && iface->lpVtbl == &d3d7_vtbl ? CONTAINING_RECORD(iface, D3DOBJ, iface7) : nullptr;
}

/* directx 7 objects answer the seventh interface; the first three are d3dim's, for the other objects. */
HRESULT D3dObjQuery(DDRAW *dd, DWORD version, REFIID riid, void **out)
{
    if (!IsEqualGUID(riid, &IID_IDirect3D7) || !dd->dx7)
        return E_NOINTERFACE;
    if (!dd->d3d) {
        D3DOBJ *d3d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d3d));
        if (!d3d)
            return DDERR_OUTOFMEMORY;
        d3d->iface7.lpVtbl = &d3d7_vtbl;
        d3d->dd = dd;
        d3d->outer = DDV7;
        InitializeListHead(&d3d->devices);
        InitializeListHead(&d3d->buffers);
        dd->d3d = d3d;
    }
    IUnknown *outer = DdIface(dd, dd->d3d->outer);
    outer->lpVtbl->AddRef(outer);
    *out = &dd->d3d->iface7;
    return S_OK;
}

/* with its directdraw object: what is left of it is unreachable, as devices hold the object. */
void D3dObjDestroy(DDRAW *dd)
{
    D3DOBJ *d3d = dd->d3d;
    if (!d3d)
        return;
    while (!IsListEmpty(&d3d->buffers)) {
        D3DVB *vb = CONTAINING_RECORD(d3d->buffers.Flink, D3DVB, links);
        RemoveEntryList(&vb->links);
        InitializeListHead(&vb->links);
        vb->d3d = nullptr;
    }
    dd->d3d = nullptr;
    HeapFree(GetProcessHeap(), 0, d3d);
}

static IUnknown *outer_of(D3DOBJ *d3d)
{
    return DdIface(d3d->dd, d3d->outer);
}

/* ------------------------------------------------------------------ devices and their descriptions */

static DWORD registry_dword(const WCHAR *key, const WCHAR *name)
{
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size))
        return 0;
    return value;
}

BOOL D3dIsHalGuid(const GUID *guid)
{
    return IsEqualGUID(guid, &IID_IDirect3DHALDevice) || IsEqualGUID(guid, &IID_IDirect3DTnLHalDevice);
}

/* the guids CreateDevice and EnumZBufferFormats take, even those of devices windows 11 lacks. */
BOOL D3dIsDeviceGuid(const GUID *guid)
{
    return IsEqualGUID(guid, &IID_IDirect3DRampDevice) || IsEqualGUID(guid, &IID_IDirect3DRGBDevice) ||
           IsEqualGUID(guid, &IID_IDirect3DMMXDevice) || IsEqualGUID(guid, &IID_IDirect3DHALDevice) ||
           IsEqualGUID(guid, &IID_IDirect3DRefDevice) || IsEqualGUID(guid, &IID_IDirect3DNullDevice) ||
           IsEqualGUID(guid, &IID_IDirect3DTnLHalDevice);
}

static void prim_caps(D3DPRIMCAPS *c, BOOL hal)
{
    c->dwSize = sizeof(*c);
    c->dwMiscCaps = hal ? 0x72 : 0x70;
    /* the rgb rasterizer's, and a modern driver's: dither, z test, fog of both kinds, bias and anisotropy. */
    c->dwRasterCaps = hal ? 0x003361B1 : 0x002121F7;
    c->dwZCmpCaps = 0xFF;
    c->dwSrcBlendCaps = 0x1FFF;
    c->dwDestBlendCaps = hal ? 0x1FFF : 0x07FF;
    c->dwAlphaCmpCaps = 0xFF;
    c->dwShadeCaps = 0x000C528A;
    c->dwTextureCaps = hal ? 0x00000C4F : 0xDF;
    c->dwTextureFilterCaps = hal ? 0x0703073F : 0x0303033F;
    c->dwTextureBlendCaps = hal ? 0xFF : 0xCF;
    c->dwTextureAddressCaps = 0x1F;
    c->dwStippleWidth = c->dwStippleHeight = hal ? 0 : 4;
}

/*
 * the rgb device as d3dim700 describes it, and a hardware device as a
 * current driver does through ddraw's emulation of the old driver model
 * guessed, the values being the driver's.
 */
BOOL D3dDeviceDesc7(const GUID *guid, D3DDEVICEDESC7 *d)
{
    memset(d, 0, sizeof(*d));
    BOOL tnl = IsEqualGUID(guid, &IID_IDirect3DTnLHalDevice), hal = tnl || IsEqualGUID(guid, &IID_IDirect3DHALDevice);
    if (!hal && !IsEqualGUID(guid, &IID_IDirect3DRGBDevice) && !IsEqualGUID(guid, &IID_IDirect3DMMXDevice))
        return FALSE;
    prim_caps(&d->dpcTriCaps, hal);
    d->dpcLineCaps = d->dpcTriCaps;
    d->dwMinTextureWidth = d->dwMinTextureHeight = 1;
    d->dwStencilCaps = 0xFF;
    d->wMaxTextureBlendStages = d->wMaxSimultaneousTextures = 8;
    d->deviceGUID = *guid;
    if (hal) {
        d->dwDevCaps = 0x0009AE70;
        d->dwDeviceRenderBitDepth = DDBD_16 | DDBD_32;
        d->dwDeviceZBufferBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
        d->dwMaxTextureWidth = d->dwMaxTextureHeight = 2048;
        d->dwMaxTextureRepeat = 8192;
        d->dwMaxTextureAspectRatio = 2048;
        d->dwMaxAnisotropy = 16;
        d->dvGuardBandLeft = d->dvGuardBandTop = -32768.0f;
        d->dvGuardBandRight = d->dvGuardBandBottom = 32767.0f;
        d->dwFVFCaps = 8;
        /* all of direct3d 7's operations but the premodulation nothing implements. */
        d->dwTextureOpCaps = 0x00FEFFFF;
        d->dvMaxVertexW = 1e10f;
        d->dwMaxActiveLights = tnl ? 8 : 0xFFFFFFFF;
    } else {
        d->dwDevCaps = 0x00000551;
        d->dwDeviceRenderBitDepth = DDBD_8 | DDBD_16 | DDBD_24 | DDBD_32;
        d->dwDeviceZBufferBitDepth = DDBD_16;
        d->dwMaxTextureWidth = d->dwMaxTextureHeight = 1024;
        d->dwMaxTextureRepeat = 256;
        d->dwMaxAnisotropy = 1;
        d->dvGuardBandLeft = d->dvGuardBandTop = -4096.0f;
        d->dvGuardBandRight = d->dvGuardBandBottom = 4095.0f;
        d->dwFVFCaps = 8;
        d->dwTextureOpCaps = 0x000078FF;
        d->dvMaxVertexW = 10000.0f;
        d->dwMaxActiveLights = 0xFFFFFFFF;
    }
    d->wMaxUserClipPlanes = 6;
    d->wMaxVertexBlendMatrices = 4;
    d->dwVertexProcessingCaps = 0x3F;
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE d3d7_enum_devices(IDirect3D7 *This, LPD3DENUMDEVICESCALLBACK7 callback,
                                                   LPVOID context)
{
    if (!callback)
        return DDERR_INVALIDPARAMS;
    static const struct {
        const GUID *guid;
        char name[32], description[96];
    } DEVICES[] = {
        {&IID_IDirect3DRGBDevice, "RGB Emulation", "Microsoft Direct3D RGB Software Emulation"},
        {&IID_IDirect3DHALDevice, "Direct3D HAL", "Microsoft Direct3D Hardware acceleration through Direct3D HAL"},
        {&IID_IDirect3DTnLHalDevice, "Direct3D T&L HAL",
         "Microsoft Direct3D Hardware Transform and Lighting acceleration capable device"},
    };
    BOOL software_only = registry_dword(L"Software\\Microsoft\\Direct3D\\Drivers", L"SoftwareOnly") != 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(DEVICES); i++) {
        if (software_only && D3dIsHalGuid(DEVICES[i].guid))
            continue;
        D3DDEVICEDESC7 desc;
        D3dDeviceDesc7(DEVICES[i].guid, &desc);
        /* the plain hardware device is described with the software pipeline's transform and lighting. */
        if (IsEqualGUID(DEVICES[i].guid, &IID_IDirect3DHALDevice))
            desc.dwDevCaps &= ~D3DDEVCAPS_HWTRANSFORMANDLIGHT;
        char name[32], description[96];
        strcpy(name, DEVICES[i].name);
        strcpy(description, DEVICES[i].description);
        if (callback(description, name, &desc, context) != D3DENUMRET_OK)
            break;
    }
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d7_create_device(IDirect3D7 *This, REFCLSID clsid, LPDIRECTDRAWSURFACE7 surface,
                                                    LPDIRECT3DDEVICE7 *device)
{
    D3DOBJ *d3d = D3dObjFromIface7(This);
    DdEnter();
    if (!d3d || !surface)
        return DdLeave(DDERR_INVALIDOBJECT);
    if (!device)
        return DdLeave(DDERR_INVALIDPARAMS);
    *device = nullptr;
    DDSURF *target = DdSurfFromIface(surface);
    if (!target)
        return DdLeave(DDERR_INVALIDOBJECT);
    return DdLeave(D3dCreateDevice7(d3d, clsid, target, device));
}

static HRESULT STDMETHODCALLTYPE d3d7_create_vertex_buffer(IDirect3D7 *This, LPD3DVERTEXBUFFERDESC desc,
                                                           LPDIRECT3DVERTEXBUFFER7 *buffer, DWORD flags)
{
    D3DOBJ *d3d = D3dObjFromIface7(This);
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    if (!desc || !buffer)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    return DdLeave(D3dCreateVertexBuffer7(d3d, desc, flags, buffer));
}

/* ------------------------------------------------------------------ formats */

#define RGB_FORMAT(bits, r, g, b, a)                                                                                   \
    {sizeof(DDPIXELFORMAT), DDPF_RGB | ((a) ? DDPF_ALPHAPIXELS : 0), 0, {bits}, {r}, {g}, {b}, {a}}

/* what a current driver offers as textures through ddraw's list, guessed as they are the driver's. */
const DDPIXELFORMAT D3dTextureFormats[] = {
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0),
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000),
    RGB_FORMAT(16, 0xF800, 0x7E0, 0x1F, 0),
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0),
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0x8000),
    RGB_FORMAT(16, 0xF00, 0xF0, 0xF, 0xF000),
    {sizeof(DDPIXELFORMAT), DDPF_LUMINANCE, 0, {8}, {0xFF}},
    {sizeof(DDPIXELFORMAT), DDPF_LUMINANCE | DDPF_ALPHAPIXELS, 0, {16}, {0xFF}, {0}, {0}, {0xFF00}},
    {sizeof(DDPIXELFORMAT), DDPF_BUMPDUDV, 0, {16}, {0xFF}, {0xFF00}},
    {sizeof(DDPIXELFORMAT), DDPF_ALPHA, 0, {8}},
    {sizeof(DDPIXELFORMAT), DDPF_FOURCC, MAKEFOURCC('D', 'X', 'T', '1')},
    {sizeof(DDPIXELFORMAT), DDPF_FOURCC, MAKEFOURCC('D', 'X', 'T', '2')},
    {sizeof(DDPIXELFORMAT), DDPF_FOURCC, MAKEFOURCC('D', 'X', 'T', '3')},
    {sizeof(DDPIXELFORMAT), DDPF_FOURCC, MAKEFOURCC('D', 'X', 'T', '4')},
    {sizeof(DDPIXELFORMAT), DDPF_FOURCC, MAKEFOURCC('D', 'X', 'T', '5')},
};
const DWORD D3dTextureFormatCount = RTL_NUMBER_OF(D3dTextureFormats);

static HRESULT STDMETHODCALLTYPE d3d7_enum_zbuffer_formats(IDirect3D7 *This, REFCLSID device,
                                                           LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    D3DOBJ *d3d = D3dObjFromIface7(This);
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    if (!callback || !device || !D3dIsDeviceGuid(device))
        return DDERR_INVALIDPARAMS;
    static const DDPIXELFORMAT HAL[] = {
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, {16}, {0}, {0xFFFF}},
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, {32}, {0}, {0xFFFFFF00}},
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER | DDPF_STENCILBUFFER, 0, {32}, {8}, {0xFFFFFF00}, {0xFF}},
    };
    static const DDPIXELFORMAT RGB[] = {
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, {16}, {0}, {0xFFFF}},
        {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER | DDPF_STENCILBUFFER, 0, {32}, {8}, {0xFFFFFF00}, {0xFF}},
    };
    const DDPIXELFORMAT *list = nullptr;
    UINT count = 0;
    if (D3dIsHalGuid(device)) {
        list = HAL;
        count = RTL_NUMBER_OF(HAL);
    } else if (IsEqualGUID(device, &IID_IDirect3DRGBDevice) || IsEqualGUID(device, &IID_IDirect3DMMXDevice)) {
        list = RGB;
        count = RTL_NUMBER_OF(RGB);
    }
    for (UINT i = 0; i < count; i++) {
        DDPIXELFORMAT copy = list[i];
        if (callback(&copy, context) != D3DENUMRET_OK)
            break;
    }
    return D3D_OK;
}

/* the textures of every device are managed by the backend, which keeps them as long as their surfaces. */
static HRESULT STDMETHODCALLTYPE d3d7_evict_managed_textures(IDirect3D7 *This)
{
    return D3dObjFromIface7(This) ? D3D_OK : DDERR_INVALIDOBJECT;
}

/* ------------------------------------------------------------------ IUnknown, the directdraw object's */

static HRESULT STDMETHODCALLTYPE d3d7_query(IDirect3D7 *This, REFIID riid, void **out)
{
    D3DOBJ *d3d = D3dObjFromIface7(This);
    if (!out)
        return DDERR_INVALIDPARAMS;
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    IUnknown *outer = outer_of(d3d);
    return outer->lpVtbl->QueryInterface(outer, riid, out);
}

static ULONG STDMETHODCALLTYPE d3d7_add_ref(IDirect3D7 *This)
{
    D3DOBJ *d3d = D3dObjFromIface7(This);
    if (!d3d)
        return 0;
    IUnknown *outer = outer_of(d3d);
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE d3d7_release(IDirect3D7 *This)
{
    D3DOBJ *d3d = D3dObjFromIface7(This);
    if (!d3d)
        return 0;
    IUnknown *outer = outer_of(d3d);
    return outer->lpVtbl->Release(outer);
}

static const IDirect3D7Vtbl d3d7_vtbl = {
    d3d7_query,
    d3d7_add_ref,
    d3d7_release,
    d3d7_enum_devices,
    d3d7_create_device,
    d3d7_create_vertex_buffer,
    d3d7_enum_zbuffer_formats,
    d3d7_evict_managed_textures,
};
