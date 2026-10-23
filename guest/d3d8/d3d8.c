/*
 * IDirect3D8 over Direct3D 9's adapters, with the display modes Direct3D 8 lists, its format checks and the caps of
 * a DirectX 8.1 card: the first part of Direct3D 9's, with the runtime's masks and shader versions.
 */
#include "d3d8_private.h"

static D8Direct3D *impl(IDirect3D8 *iface) { return CONTAINING_RECORD(iface, D8Direct3D, iface); }

D8Direct3D *D8Direct3DFrom(IDirect3D8 *iface) { return impl(iface); }

/* ------------------------------------------------------------------ formats */

UINT D8FormatBytes(D3DFORMAT format)
{
    switch ((UINT)format) {
    case D3DFMT_R3G3B2:
    case D3DFMT_A8:
    case D3DFMT_P8:
    case D3DFMT_L8:
    case D3DFMT_A4L4:
        return 1;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8P8:
    case D3DFMT_A8L8:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
    case D3DFMT_D16_LOCKABLE:
    case 72:
    case D3DFMT_D15S1:
    case D3DFMT_D16:
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
        return 2;
    case D3DFMT_R8G8B8:
        return 3;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_G16R16:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_V16U16:
    case 65:
    case D3DFMT_A2W10V10U10:
    case D3DFMT_D32:
    case 74:
    case D3DFMT_D24S8:
    case 76:
    case D3DFMT_D24X8:
    case 78:
    case D3DFMT_D24X4S4:
        return 4;
    case D3DFMT_DXT1:
        return 8;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return 16;
    default:
        return 0;
    }
}

BOOL D8IsDxt(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 || format == D3DFMT_DXT4 ||
           format == D3DFMT_DXT5;
}

BOOL D8IsDepthFormat(D3DFORMAT format) { return format >= D3DFMT_D16_LOCKABLE && format <= D3DFMT_D16; }

BOOL D8HasStencil(D3DFORMAT format)
{
    return format == 72 || format == D3DFMT_D15S1 || format == 74 || format == D3DFMT_D24S8 || format == 78 ||
           format == D3DFMT_D24X4S4;
}

/* rows padded to whole dwords, compressed formats by rows of blocks. */
UINT D8LevelSize(D3DFORMAT format, UINT width, UINT height, UINT depth)
{
    UINT bytes = D8FormatBytes(format);
    if (D8IsDxt(format))
        return D8_MAX(1, (width + 3) / 4) * bytes * D8_MAX(1, (height + 3) / 4) * depth;
    return ((width * bytes + 3) & ~3u) * height * depth;
}

D3DFORMAT D8NoAlpha(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8:
        return D3DFMT_X8R8G8B8;
    case D3DFMT_A1R5G5B5:
        return D3DFMT_X1R5G5B5;
    case D3DFMT_A4R4G4B4:
        return D3DFMT_X4R4G4B4;
    case D3DFMT_A8B8G8R8:
        return D3DFMT_X8B8G8R8;
    default:
        return format;
    }
}

D3DFORMAT D8DisplayFormat(D8Direct3D *d3d, UINT adapter)
{
    D3DDISPLAYMODE mode;
    if (FAILED(d3d->d9->lpVtbl->GetAdapterDisplayMode(d3d->d9, adapter, &mode)))
        return D3DFMT_X8R8G8B8;
    return mode.Format;
}

static BOOL valid_type(D3DDEVTYPE type)
{
    return type == D3DDEVTYPE_HAL || type == D3DDEVTYPE_REF || type == D3DDEVTYPE_SW;
}

static BOOL display_format(D3DFORMAT f)
{
    return f == D3DFMT_X8R8G8B8 || f == D3DFMT_R5G6B5 || f == D3DFMT_X1R5G5B5 || f == D3DFMT_R8G8B8;
}

/* ------------------------------------------------------------------ modes */

/* the modes windows reports: no 8 or 24 bit ones, and 16 bit ones only in the adapter's one format. */
static void build_modes(D8Adapter *a)
{
    DEVMODEA dm;
    UINT capacity = 0;
    for (DWORD i = 0;; i++) {
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsA(a->device, i, &dm))
            break;
        D3DFORMAT format;
        if (dm.dmBitsPerPel == 15 || dm.dmBitsPerPel == 16)
            format = D3DFMT_R5G6B5;
        else if (dm.dmBitsPerPel == 32)
            format = D3DFMT_X8R8G8B8;
        else
            continue;
        if (a->mode_count == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            D8Mode *modes = a->modes ? HeapReAlloc(GetProcessHeap(), 0, a->modes, capacity * sizeof(*modes))
                                     : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*modes));
            if (!modes)
                return;
            a->modes = modes;
        }
        a->modes[a->mode_count++] = (D8Mode){dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, format};
    }
}

static BOOL duplicate(const D8Adapter *a, UINT index)
{
    const D8Mode *m = &a->modes[index];
    for (UINT i = 0; i < index; i++)
        if (!memcmp(&a->modes[i], m, sizeof(*m)))
            return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ caps */

HRESULT D8GetCaps(D8Direct3D *d3d, UINT adapter, D3DDEVTYPE type, D3DCAPS8 *caps)
{
    if (adapter >= d3d->adapter_count)
        return D3DERR_INVALIDCALL;
    if (type != D3DDEVTYPE_HAL && type != D3DDEVTYPE_REF) {
        memset(caps, 0, sizeof(*caps));
        return type == D3DDEVTYPE_SW ? D3DERR_INVALIDCALL : D3DERR_INVALIDDEVICE;
    }
    D3DCAPS9 c9;
    HRESULT hr = d3d->d9->lpVtbl->GetDeviceCaps(d3d->d9, adapter, D3DDEVTYPE_HAL, &c9);
    if (FAILED(hr)) {
        memset(caps, 0, sizeof(*caps));
        return D3DERR_NOTAVAILABLE;
    }
    memcpy(caps, &c9, sizeof(*caps));
    caps->DeviceType = type;
    caps->AdapterOrdinal = adapter;
    caps->Caps &= 0x02020000;
    /* a DirectX 8 driver renders into windows; the runtime never lets the driver manage resources. */
    caps->Caps2 = (c9.Caps2 | D3DCAPS2_CANRENDERWINDOWED) & 0x220A0002;
    caps->Caps3 &= D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE |
                                  D3DPRESENT_INTERVAL_TWO | D3DPRESENT_INTERVAL_THREE | D3DPRESENT_INTERVAL_FOUR;
    if (d3d->sdk == 120)
        caps->CursorCaps = 0;
    caps->DevCaps = (caps->DevCaps & ~0x06000000u) | D3DDEVCAPS_HWRASTERIZATION;
    caps->PrimitiveMiscCaps &= 0x0FFE;
    /* Direct3D 9's depth bias is Direct3D 8's z bias. */
    caps->RasterCaps &= 0x00FFFFFF;
    if (c9.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS)
        caps->RasterCaps |= D3DPRASTERCAPS_ZBIAS;
    caps->SrcBlendCaps &= 0x1FFF;
    caps->DestBlendCaps &= 0x1FFF;
    caps->TextureCaps &= 0x1FFFFF;
    caps->LineCaps &= 0x1F;
    caps->StencilCaps &= 0xFF;
    caps->VertexProcessingCaps &= 0x7B;
    caps->VertexShaderVersion = D8_MIN(caps->VertexShaderVersion, D3DVS_VERSION(1, 1));
    caps->PixelShaderVersion = D8_MIN(caps->PixelShaderVersion, D3DPS_VERSION(1, 4));
    caps->MaxVertexShaderConst = D8_MIN(caps->MaxVertexShaderConst, D8_VS_CONSTANTS);
    caps->MaxStreams = D8_MIN(caps->MaxStreams, D8_STREAMS);
    caps->MaxPixelShaderValue = c9.PixelShader1xMaxValue;
    if (caps->MaxPointSize == 0.0f)
        caps->MaxPointSize = 1.0f;
    return D3D_OK;
}

/* ------------------------------------------------------------------ methods */

static HRESULT STDMETHODCALLTYPE d3d_query(IDirect3D8 *iface, REFIID riid, void **out)
{
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3D8)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return D3D_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d_add_ref(IDirect3D8 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE d3d_release(IDirect3D8 *iface)
{
    D8Direct3D *d3d = impl(iface);
    ULONG refs = InterlockedDecrement(&d3d->refs);
    if (!refs) {
        for (UINT i = 0; i < d3d->adapter_count; i++)
            HeapFree(GetProcessHeap(), 0, d3d->adapters[i].modes);
        HeapFree(GetProcessHeap(), 0, d3d->adapters);
        d3d->d9->lpVtbl->Release(d3d->d9);
        DeleteCriticalSection(&d3d->cs);
        HeapFree(GetProcessHeap(), 0, d3d);
    }
    return refs;
}

/* no software rasterizer plugs into this runtime. */
static HRESULT STDMETHODCALLTYPE d3d_register_software_device(IDirect3D8 *iface, void *initialize)
{
    return initialize ? D3DERR_NOTAVAILABLE : D3DERR_INVALIDCALL;
}

static UINT STDMETHODCALLTYPE d3d_get_adapter_count(IDirect3D8 *iface) { return impl(iface)->adapter_count; }

static HRESULT STDMETHODCALLTYPE d3d_get_adapter_identifier(IDirect3D8 *iface, UINT adapter, DWORD flags,
                                                           D3DADAPTER_IDENTIFIER8 *out)
{
    D8Direct3D *d3d = impl(iface);
    memset(out, 0, sizeof(*out));
    if ((flags & ~D3DENUM_NO_WHQL_LEVEL) || adapter >= d3d->adapter_count)
        return D3DERR_INVALIDCALL;
    D3DADAPTER_IDENTIFIER9 id;
    HRESULT hr = d3d->d9->lpVtbl->GetAdapterIdentifier(d3d->d9, adapter,
                                                       flags & D3DENUM_NO_WHQL_LEVEL ? 0 : D3DENUM_WHQL_LEVEL, &id);
    if (FAILED(hr))
        return hr;
    memcpy(out->Driver, id.Driver, sizeof(out->Driver));
    memcpy(out->Description, id.Description, sizeof(out->Description));
    out->DriverVersion = id.DriverVersion;
    out->VendorId = id.VendorId;
    out->DeviceId = id.DeviceId;
    out->SubSysId = id.SubSysId;
    out->Revision = id.Revision;
    out->DeviceIdentifier = id.DeviceIdentifier;
    out->WHQLLevel = flags & D3DENUM_NO_WHQL_LEVEL ? 0 : id.WHQLLevel;
    return D3D_OK;
}

static UINT STDMETHODCALLTYPE d3d_get_adapter_mode_count(IDirect3D8 *iface, UINT adapter)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count)
        return 0;
    D8Adapter *a = &d3d->adapters[adapter];
    UINT count = 0;
    for (UINT i = 0; i < a->mode_count; i++)
        if (!duplicate(a, i))
            count++;
    return count;
}

static HRESULT STDMETHODCALLTYPE d3d_enum_adapter_modes(IDirect3D8 *iface, UINT adapter, UINT mode,
                                                       D3DDISPLAYMODE *out)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count || mode >= d3d->adapters[adapter].mode_count)
        return D3DERR_INVALIDCALL;
    D8Adapter *a = &d3d->adapters[adapter];
    for (UINT i = 0; i < a->mode_count; i++) {
        if (duplicate(a, i))
            continue;
        if (!mode--) {
            *out = (D3DDISPLAYMODE){a->modes[i].width, a->modes[i].height, a->modes[i].refresh, a->modes[i].format};
            return D3D_OK;
        }
    }
    return D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE d3d_get_adapter_display_mode(IDirect3D8 *iface, UINT adapter, D3DDISPLAYMODE *mode)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count)
        return D3DERR_INVALIDCALL;
    /* windows reports success when it cannot read the mode, leaving what was there. */
    D3DDISPLAYMODE m;
    if (SUCCEEDED(d3d->d9->lpVtbl->GetAdapterDisplayMode(d3d->d9, adapter, &m)))
        *mode = m;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_type(IDirect3D8 *iface, UINT adapter, D3DDEVTYPE type,
                                                      D3DFORMAT display, D3DFORMAT back_buffer, BOOL windowed)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count || !valid_type(type) || display == D3DFMT_UNKNOWN ||
        back_buffer == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    if (back_buffer != D3DFMT_X1R5G5B5 && back_buffer != D3DFMT_A1R5G5B5 && back_buffer != D3DFMT_R5G6B5 &&
        back_buffer != D3DFMT_X8R8G8B8 && back_buffer != D3DFMT_A8R8G8B8)
        return D3DERR_NOTAVAILABLE;
    if (!display_format(display))
        return D3DERR_NOTAVAILABLE;
    if (type == D3DDEVTYPE_SW)
        return D3DERR_INVALIDCALL;
    return d3d->d9->lpVtbl->CheckDeviceType(d3d->d9, adapter, D3DDEVTYPE_HAL, display, back_buffer, windowed);
}

static BOOL listed_depth(D3DFORMAT f)
{
    return f == D3DFMT_D16 || f == D3DFMT_D16_LOCKABLE || f == D3DFMT_D15S1 || f == D3DFMT_D24X8 ||
           f == D3DFMT_D24X4S4 || f == D3DFMT_D24S8 || f == D3DFMT_D32 || !D8FormatBytes(f);
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_format(IDirect3D8 *iface, UINT adapter, D3DDEVTYPE type,
                                                        D3DFORMAT display, DWORD usage, D3DRESOURCETYPE resource,
                                                        D3DFORMAT format)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count || !valid_type(type) || display == D3DFMT_UNKNOWN || format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    if (!display_format(display))
        return D3DERR_NOTAVAILABLE;
    BOOL texture = resource == D3DRTYPE_TEXTURE || resource == D3DRTYPE_CUBETEXTURE ||
                   resource == D3DRTYPE_VOLUMETEXTURE;
    if (resource == D3DRTYPE_SURFACE && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DEPTHSTENCIL) && !listed_depth(format))
        return D3DERR_INVALIDCALL;
    if (resource < D3DRTYPE_SURFACE || resource > D3DRTYPE_CUBETEXTURE || (usage & 0x03FFFC04))
        return D3DERR_INVALIDCALL;
    if (type == D3DDEVTYPE_SW)
        return D3DERR_INVALIDCALL;
    if (texture && (usage & D3DUSAGE_DYNAMIC) && (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
        return D3DERR_INVALIDCALL;
    return d3d->d9->lpVtbl->CheckDeviceFormat(d3d->d9, adapter, D3DDEVTYPE_HAL, display,
                                              usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL |
                                                       D3DUSAGE_DYNAMIC),
                                              resource, format);
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_multisample_type(IDirect3D8 *iface, UINT adapter,
                                                                  D3DDEVTYPE type, D3DFORMAT format, BOOL windowed,
                                                                  D3DMULTISAMPLE_TYPE samples)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count || !valid_type(type) || format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    if (type == D3DDEVTYPE_SW)
        return D3DERR_INVALIDCALL;
    if (samples == D3DMULTISAMPLE_NONE)
        return D3D_OK;
    if (samples == 1 || samples > 16)
        return D3DERR_INVALIDCALL;
    return d3d->d9->lpVtbl->CheckDeviceMultiSampleType(d3d->d9, adapter, D3DDEVTYPE_HAL, format,
                                                       windowed || type == D3DDEVTYPE_REF, samples, nullptr);
}

static HRESULT STDMETHODCALLTYPE d3d_check_depth_stencil_match(IDirect3D8 *iface, UINT adapter, D3DDEVTYPE type,
                                                              D3DFORMAT display, D3DFORMAT target, D3DFORMAT depth)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count || !valid_type(type) || display == D3DFMT_UNKNOWN ||
        target == D3DFMT_UNKNOWN || depth == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    if (type == D3DDEVTYPE_SW)
        return D3DERR_INVALIDCALL;
    if (!display_format(display))
        return D3DERR_NOTAVAILABLE;
    /* a target or depth format the device cannot use is a bad call here, not an unavailable one. */
    IDirect3D9 *d9 = d3d->d9;
    if (FAILED(d9->lpVtbl->CheckDeviceFormat(d9, adapter, D3DDEVTYPE_HAL, display, D3DUSAGE_RENDERTARGET,
                                             D3DRTYPE_SURFACE, target)) ||
        FAILED(d9->lpVtbl->CheckDeviceFormat(d9, adapter, D3DDEVTYPE_HAL, display, D3DUSAGE_DEPTHSTENCIL,
                                             D3DRTYPE_SURFACE, depth)))
        return D3DERR_INVALIDCALL;
    BOOL match = depth == D3DFMT_D16_LOCKABLE || D8HasStencil(depth) ||
                 (d3d->sdk > 120 && (depth == D3DFMT_D32 || depth == D3DFMT_D24X8));
    if (match && D8FormatBytes(target) != D8FormatBytes(depth))
        return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_get_device_caps(IDirect3D8 *iface, UINT adapter, D3DDEVTYPE type,
                                                    D3DCAPS8 *caps)
{
    return D8GetCaps(impl(iface), adapter, type, caps);
}

static HMONITOR STDMETHODCALLTYPE d3d_get_adapter_monitor(IDirect3D8 *iface, UINT adapter)
{
    D8Direct3D *d3d = impl(iface);
    if (adapter >= d3d->adapter_count)
        return nullptr;
    return d3d->d9->lpVtbl->GetAdapterMonitor(d3d->d9, adapter);
}

static HRESULT STDMETHODCALLTYPE d3d_create_device(IDirect3D8 *iface, UINT adapter, D3DDEVTYPE type, HWND focus,
                                                  DWORD flags, D3DPRESENT_PARAMETERS8 *params,
                                                  IDirect3DDevice8 **device)
{
    return D8CreateDevice(impl(iface), adapter, type, focus, flags, params, device);
}

static const IDirect3D8Vtbl d3d_vtbl = {
    d3d_query,
    d3d_add_ref,
    d3d_release,
    d3d_register_software_device,
    d3d_get_adapter_count,
    d3d_get_adapter_identifier,
    d3d_get_adapter_mode_count,
    d3d_enum_adapter_modes,
    d3d_get_adapter_display_mode,
    d3d_check_device_type,
    d3d_check_device_format,
    d3d_check_device_multisample_type,
    d3d_check_depth_stencil_match,
    d3d_get_device_caps,
    d3d_get_adapter_monitor,
    d3d_create_device,
};

HRESULT D8CreateDirect3D(UINT sdk, IDirect3D8 **out)
{
    IDirect3D9 *d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d9)
        return E_FAIL;
    D8Direct3D *d3d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d3d));
    if (!d3d) {
        d9->lpVtbl->Release(d9);
        return E_OUTOFMEMORY;
    }
    d3d->iface.lpVtbl = &d3d_vtbl;
    d3d->refs = 1;
    d3d->d9 = d9;
    d3d->sdk = sdk;
    InitializeCriticalSection(&d3d->cs);
    UINT count = D8_MIN(d9->lpVtbl->GetAdapterCount(d9), 12);
    d3d->adapters = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, D8_MAX(count, 1) * sizeof(*d3d->adapters));
    if (!d3d->adapters) {
        d3d_release(&d3d->iface);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++) {
        D3DADAPTER_IDENTIFIER9 id;
        if (SUCCEEDED(d9->lpVtbl->GetAdapterIdentifier(d9, i, 0, &id)))
            memcpy(d3d->adapters[i].device, id.DeviceName, sizeof(d3d->adapters[i].device));
        build_modes(&d3d->adapters[i]);
    }
    d3d->adapter_count = count;
    *out = &d3d->iface;
    return D3D_OK;
}
