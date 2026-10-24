/* IDirect3D9Ex: one adapter per monitor as dxgi lists outputs, their modes, formats and caps, and device creation. */
#include "d3d9_private.h"

typedef struct D9Direct3D {
    IDirect3D9Ex iface;
    LONG refs;
    BOOL ex;
} D9Direct3D;

static D9Direct3D *impl(IDirect3D9Ex *iface) { return CONTAINING_RECORD(iface, D9Direct3D, iface); }

/* ------------------------------------------------------------------ adapters */

static IDXGIFactory1 *g_factory;
static INIT_ONCE g_factory_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_factory(INIT_ONCE *once, void *param, void **context)
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    HRESULT(WINAPI * create)(REFIID, void **) =
        dxgi ? (HRESULT(WINAPI *)(REFIID, void **))(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    if (create && FAILED(create(&IID_IDXGIFactory1, (void **)&g_factory)))
        g_factory = nullptr;
    return TRUE;
}

IDXGIFactory1 *D9Factory(void)
{
    InitOnceExecuteOnce(&g_factory_once, make_factory, nullptr, nullptr);
    return g_factory;
}

/* walks every adapter's outputs in turn. an adapter without outputs counts as one. */
HRESULT D9GetOutput(UINT index, IDXGIAdapter1 **out_adapter, IDXGIOutput **out_output)
{
    IDXGIFactory1 *factory = D9Factory();
    if (!factory)
        return D3DERR_NOTAVAILABLE;
    IDXGIAdapter1 *adapter;
    UINT n = 0;
    for (UINT a = 0; SUCCEEDED(factory->lpVtbl->EnumAdapters1(factory, a, &adapter)); a++) {
        IDXGIOutput *output;
        UINT o = 0;
        for (; SUCCEEDED(adapter->lpVtbl->EnumOutputs(adapter, o, &output)); o++) {
            if (n++ == index) {
                *out_adapter = adapter;
                if (out_output)
                    *out_output = output;
                else
                    output->lpVtbl->Release(output);
                return D3D_OK;
            }
            output->lpVtbl->Release(output);
        }
        if (!o && n++ == index) {
            *out_adapter = adapter;
            if (out_output)
                *out_output = nullptr;
            return D3D_OK;
        }
        adapter->lpVtbl->Release(adapter);
    }
    return D3DERR_INVALIDCALL;
}

UINT D9AdapterCount(void)
{
    UINT n = 0;
    IDXGIAdapter1 *adapter;
    while (SUCCEEDED(D9GetOutput(n, &adapter, nullptr))) {
        adapter->lpVtbl->Release(adapter);
        n++;
    }
    return n;
}

static DXGI_FORMAT display_format(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A8R8G8B8:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_A2R10G10B10:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

/* merges the duplicates dxgi lists for each scaling and scanline order. */
static UINT list_modes(UINT adapter, D3DFORMAT format, D3DSCANLINEORDERING scanline, D3DDISPLAYMODEEX *out,
                       UINT max)
{
    DXGI_FORMAT dxgi = display_format(format);
    IDXGIAdapter1 *a;
    IDXGIOutput *output;
    if (dxgi == DXGI_FORMAT_UNKNOWN || FAILED(D9GetOutput(adapter, &a, &output)))
        return 0;
    a->lpVtbl->Release(a);
    if (!output)
        return 0;
    UINT count = 0;
    DXGI_MODE_DESC *modes = nullptr;
    if (SUCCEEDED(output->lpVtbl->GetDisplayModeList(output, dxgi, 0, &count, nullptr)) && count)
        modes = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*modes));
    if (modes && FAILED(output->lpVtbl->GetDisplayModeList(output, dxgi, 0, &count, modes)))
        count = 0;
    output->lpVtbl->Release(output);
    UINT n = 0;
    for (UINT i = 0; modes && i < count; i++) {
        D3DDISPLAYMODEEX m = {sizeof(m), modes[i].Width, modes[i].Height, 0, format,
                              modes[i].ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST ||
                                      modes[i].ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST
                                  ? D3DSCANLINEORDERING_INTERLACED
                                  : D3DSCANLINEORDERING_PROGRESSIVE};
        if (modes[i].RefreshRate.Denominator)
            m.RefreshRate = (modes[i].RefreshRate.Numerator + modes[i].RefreshRate.Denominator / 2) /
                            modes[i].RefreshRate.Denominator;
        if (scanline != D3DSCANLINEORDERING_UNKNOWN && m.ScanLineOrdering != scanline)
            continue;
        BOOL seen = FALSE;
        for (UINT j = 0; j < n && !seen && out; j++)
            seen = out[j].Width == m.Width && out[j].Height == m.Height && out[j].RefreshRate == m.RefreshRate &&
                   out[j].ScanLineOrdering == m.ScanLineOrdering;
        if (seen)
            continue;
        if (out && n < max)
            out[n] = m;
        n++;
        if (!out && n >= max)
            break;
    }
    HeapFree(GetProcessHeap(), 0, modes);
    return n;
}

static UINT mode_count(UINT adapter, D3DFORMAT format, D3DSCANLINEORDERING scanline)
{
    D3DDISPLAYMODEEX *modes = HeapAlloc(GetProcessHeap(), 0, 4096 * sizeof(*modes));
    UINT n = modes ? list_modes(adapter, format, scanline, modes, 4096) : 0;
    HeapFree(GetProcessHeap(), 0, modes);
    return D9_MIN(n, 4096);
}

static HRESULT mode_at(UINT adapter, D3DFORMAT format, D3DSCANLINEORDERING scanline, UINT index,
                       D3DDISPLAYMODEEX *mode)
{
    D3DDISPLAYMODEEX *modes = HeapAlloc(GetProcessHeap(), 0, 4096 * sizeof(*modes));
    UINT n = modes ? D9_MIN(list_modes(adapter, format, scanline, modes, 4096), 4096) : 0;
    HRESULT hr = index < n ? D3D_OK : D3DERR_INVALIDCALL;
    if (index < n)
        *mode = modes[index];
    HeapFree(GetProcessHeap(), 0, modes);
    return hr;
}

static HRESULT current_mode(UINT adapter, D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation)
{
    IDXGIAdapter1 *a;
    IDXGIOutput *output;
    if (FAILED(D9GetOutput(adapter, &a, &output)))
        return D3DERR_INVALIDCALL;
    a->lpVtbl->Release(a);
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    DXGI_OUTPUT_DESC desc = {};
    if (output) {
        output->lpVtbl->GetDesc(output, &desc);
        output->lpVtbl->Release(output);
    }
    if (!EnumDisplaySettingsW(output ? desc.DeviceName : nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        dm.dmPelsWidth = GetSystemMetrics(SM_CXSCREEN);
        dm.dmPelsHeight = GetSystemMetrics(SM_CYSCREEN);
        dm.dmDisplayFrequency = 60;
    }
    if (mode) {
        mode->Width = dm.dmPelsWidth;
        mode->Height = dm.dmPelsHeight;
        mode->RefreshRate = dm.dmDisplayFrequency > 1 ? dm.dmDisplayFrequency : 60;
        mode->Format = D3DFMT_X8R8G8B8;
        mode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    }
    if (rotation)
        *rotation = desc.Rotation >= DXGI_MODE_ROTATION_ROTATE90 && desc.Rotation <= DXGI_MODE_ROTATION_ROTATE270
                        ? (D3DDISPLAYROTATION)(desc.Rotation)
                        : D3DDISPLAYROTATION_IDENTITY;
    return D3D_OK;
}

/* ------------------------------------------------------------------ caps */

void D9FillCaps(UINT adapter, D3DCAPS9 *caps)
{
    memset(caps, 0, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->AdapterOrdinal = adapter;
    caps->Caps = D3DCAPS_READ_SCANLINE;
    caps->Caps2 = D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_CANMANAGERESOURCE | D3DCAPS2_DYNAMICTEXTURES |
                  D3DCAPS2_CANAUTOGENMIPMAP | D3DCAPS2_CANSHARERESOURCE;
    caps->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD | D3DCAPS3_LINEAR_TO_SRGB_PRESENTATION |
                  D3DCAPS3_COPY_TO_VIDMEM | D3DCAPS3_COPY_TO_SYSTEMMEM;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE |
                                  D3DPRESENT_INTERVAL_TWO | D3DPRESENT_INTERVAL_THREE | D3DPRESENT_INTERVAL_FOUR;
    caps->CursorCaps = D3DCURSORCAPS_COLOR | D3DCURSORCAPS_LOWRES;
    caps->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY |
                    D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY |
                    D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
                    D3DDEVCAPS_TEXTURENONLOCALVIDMEM | D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX |
                    D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_CANBLTSYSTONONLOCAL | D3DDEVCAPS_HWRASTERIZATION |
                    D3DDEVCAPS_PUREDEVICE;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW |
                              D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE |
                              D3DPMISCCAPS_CLIPPLANESCALEDPOINTS | D3DPMISCCAPS_CLIPTLVERTS |
                              D3DPMISCCAPS_TSSARGTEMP | D3DPMISCCAPS_BLENDOP | D3DPMISCCAPS_INDEPENDENTWRITEMASKS |
                              D3DPMISCCAPS_PERSTAGECONSTANT | D3DPMISCCAPS_FOGANDSPECULARALPHA |
                              D3DPMISCCAPS_SEPARATEALPHABLEND | D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS |
                              D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING | D3DPMISCCAPS_FOGVERTEXCLAMPED |
                              D3DPMISCCAPS_POSTBLENDSRGBCONVERT;
    caps->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX |
                       D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_FOGRANGE |
                       D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_ZFOG |
                       D3DPRASTERCAPS_COLORPERSPECTIVE | D3DPRASTERCAPS_SCISSORTEST |
                       D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_DEPTHBIAS |
                       D3DPRASTERCAPS_MULTISAMPLE_TOGGLE;
    caps->ZCmpCaps = caps->AlphaCmpCaps = 0xFF;
    caps->SrcBlendCaps = caps->DestBlendCaps = 0x3FFF;
    caps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB |
                      D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
    caps->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_PROJECTED |
                        D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPMAP |
                        D3DPTEXTURECAPS_MIPVOLUMEMAP | D3DPTEXTURECAPS_MIPCUBEMAP;
    caps->TextureFilterCaps = caps->CubeTextureFilterCaps = caps->VolumeTextureFilterCaps =
        D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
        D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
        D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    caps->TextureAddressCaps = caps->VolumeTextureAddressCaps =
        D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_BORDER |
        D3DPTADDRESSCAPS_INDEPENDENTUV | D3DPTADDRESSCAPS_MIRRORONCE;
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP |
                     D3DLINECAPS_FOG | D3DLINECAPS_ANTIALIAS;
    caps->MaxTextureWidth = caps->MaxTextureHeight = 16384;
    caps->MaxVolumeExtent = 2048;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 16384;
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1e10f;
    caps->GuardBandLeft = caps->GuardBandTop = -32768.0f;
    caps->GuardBandRight = caps->GuardBandBottom = 32768.0f;
    caps->StencilCaps = 0x1FF;
    caps->FVFCaps = D3DFVFCAPS_PSIZE | 8;
    caps->TextureOpCaps = 0x03FFFFFF;
    caps->MaxTextureBlendStages = caps->MaxSimultaneousTextures = 8;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                                 D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER |
                                 D3DVTXPCAPS_TEXGEN_SPHEREMAP;
    caps->MaxActiveLights = 8;
    caps->MaxUserClipPlanes = 6;
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 8;
    caps->MaxPointSize = 256.0f;
    caps->MaxPrimitiveCount = 0x00FFFFFF;
    caps->MaxVertexIndex = 0x00FFFFFF;
    caps->MaxStreams = D9_MAX_STREAMS;
    caps->MaxStreamStride = 508;
    caps->VertexShaderVersion = D3DVS_VERSION(3, 0);
    caps->MaxVertexShaderConst = D9_VS_CONSTANTS;
    caps->PixelShaderVersion = D3DPS_VERSION(3, 0);
    caps->PixelShader1xMaxValue = 65504.0f;
    caps->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET | D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES |
                     D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET;
    caps->MasterAdapterOrdinal = adapter;
    caps->NumberOfAdaptersInGroup = 1;
    caps->DeclTypes = D3DDTCAPS_UBYTE4 | D3DDTCAPS_UBYTE4N | D3DDTCAPS_SHORT2N | D3DDTCAPS_SHORT4N |
                      D3DDTCAPS_USHORT2N | D3DDTCAPS_USHORT4N | D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4;
    caps->NumSimultaneousRTs = D9_MAX_TARGETS;
    caps->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
                                  D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    caps->VS20Caps = (D3DVSHADERCAPS2_0){D3DVS20CAPS_PREDICATION, D3DVS20_MAX_DYNAMICFLOWCONTROLDEPTH,
                                         D3DVS20_MAX_NUMTEMPS, D3DVS20_MAX_STATICFLOWCONTROLDEPTH};
    caps->PS20Caps = (D3DPSHADERCAPS2_0){0x1F, D3DPS20_MAX_DYNAMICFLOWCONTROLDEPTH, D3DPS20_MAX_NUMTEMPS,
                                         D3DPS20_MAX_STATICFLOWCONTROLDEPTH, D3DPS20_MAX_NUMINSTRUCTIONSLOTS};
    caps->VertexTextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
                                    D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    caps->MaxVShaderInstructionsExecuted = caps->MaxPShaderInstructionsExecuted = 65535;
    caps->MaxVertexShader30InstructionSlots = caps->MaxPixelShader30InstructionSlots = 32768;
}

/* ------------------------------------------------------------------ formats */

static BOOL is_display_format(D3DFORMAT f)
{
    return f == D3DFMT_X8R8G8B8 || f == D3DFMT_R5G6B5 || f == D3DFMT_X1R5G5B5 || f == D3DFMT_A2R10G10B10;
}

static HRESULT check_device_format(D3DFORMAT display, DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format)
{
    if (!is_display_format(display))
        return D3DERR_INVALIDCALL;
    if (type == D3DRTYPE_VERTEXBUFFER)
        return format == D3DFMT_VERTEXDATA ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (type == D3DRTYPE_INDEXBUFFER)
        return format == D3DFMT_INDEX16 || format == D3DFMT_INDEX32 ? D3D_OK : D3DERR_NOTAVAILABLE;
    const D9Format *f = D9GetFormat(format);
    if (f->d3d == D3DFMT_UNKNOWN)
        return D3DERR_NOTAVAILABLE;
    BOOL depth = f->flags & D9_FORMAT_DEPTH;
    if (f->flags & D9_FORMAT_NULL)
        return type == D3DRTYPE_SURFACE && (usage & D3DUSAGE_RENDERTARGET) ? D3D_OK : D3DERR_NOTAVAILABLE;
    if ((usage & D3DUSAGE_RENDERTARGET) && !(f->flags & D9_FORMAT_RENDER))
        return D3DERR_NOTAVAILABLE;
    if ((usage & D3DUSAGE_DEPTHSTENCIL) && !depth)
        return D3DERR_NOTAVAILABLE;
    if (depth && !(usage & D3DUSAGE_DEPTHSTENCIL) && type != D3DRTYPE_SURFACE)
        return D3DERR_NOTAVAILABLE;
    if (depth && type == D3DRTYPE_VOLUMETEXTURE)
        return D3DERR_NOTAVAILABLE;
    if ((usage & (D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_SRGBWRITE)) && !(f->flags & D9_FORMAT_SRGB))
        return D3DERR_NOTAVAILABLE;
    if ((usage & D3DUSAGE_DMAP) || (usage & D3DUSAGE_QUERY_LEGACYBUMPMAP && f->view != DXGI_FORMAT_R8G8_SNORM &&
                                    f->view != DXGI_FORMAT_R16G16_SNORM))
        return D3DERR_NOTAVAILABLE;
    if (f->block > 1 && type == D3DRTYPE_VOLUMETEXTURE)
        return D3D_OK;
    if ((usage & D3DUSAGE_AUTOGENMIPMAP) && (!(f->flags & D9_FORMAT_RENDER) || depth))
        return D3DOK_NOAUTOGEN;
    return D3D_OK;
}

/* ------------------------------------------------------------------ IDirect3D9Ex */

static HRESULT STDMETHODCALLTYPE d3d_query(IDirect3D9Ex *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3D9) ||
        (impl(iface)->ex && IsEqualGUID(riid, &IID_IDirect3D9Ex))) {
        *out = iface;
        InterlockedIncrement(&impl(iface)->refs);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d_add_ref(IDirect3D9Ex *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE d3d_release(IDirect3D9Ex *iface)
{
    ULONG refs = InterlockedDecrement(&impl(iface)->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, impl(iface));
    return refs;
}

static HRESULT STDMETHODCALLTYPE d3d_register_software_device(IDirect3D9Ex *iface, void *initialize)
{
    return D3D_OK;
}

static UINT STDMETHODCALLTYPE d3d_get_adapter_count(IDirect3D9Ex *iface) { return D9AdapterCount(); }

/* derived from the adapter ids, so it stays the same for a gpu and driver, like windows'. */
static GUID device_identifier(const DXGI_ADAPTER_DESC1 *desc, LARGE_INTEGER version)
{
    GUID g = {0xD7B71E3E, 0x4340, 0x11CF, {0}};
    UINT h = 2166136261u;
    const UINT words[] = {desc->VendorId, desc->DeviceId, desc->SubSysId, desc->Revision, version.LowPart,
                          version.HighPart};
    for (UINT i = 0; i < RTL_NUMBER_OF(words); i++)
        h = (h ^ words[i]) * 16777619u;
    g.Data1 ^= desc->DeviceId;
    g.Data2 ^= (WORD)desc->VendorId;
    for (UINT i = 0; i < 8; i++) {
        h = (h ^ i) * 16777619u;
        g.Data4[i] = (BYTE)(h >> 24);
    }
    return g;
}

static HRESULT STDMETHODCALLTYPE d3d_get_adapter_identifier(IDirect3D9Ex *iface, UINT adapter, DWORD flags,
                                                            D3DADAPTER_IDENTIFIER9 *id)
{
    IDXGIAdapter1 *a;
    IDXGIOutput *output;
    if (!id || FAILED(D9GetOutput(adapter, &a, &output)))
        return D3DERR_INVALIDCALL;
    DXGI_ADAPTER_DESC1 desc = {};
    a->lpVtbl->GetDesc1(a, &desc);
    LARGE_INTEGER version = {};
    a->lpVtbl->CheckInterfaceSupport(a, &IID_IDXGIDevice, &version);
    a->lpVtbl->Release(a);
    memset(id, 0, sizeof(*id));
    const char *driver = desc.VendorId == 0x10DE   ? (sizeof(void *) == 8 ? "nvldumdx.dll" : "nvldumd.dll")
                         : desc.VendorId == 0x1002 ? (sizeof(void *) == 8 ? "aticfx64.dll" : "aticfx32.dll")
                         : desc.VendorId == 0x8086 ? (sizeof(void *) == 8 ? "igdumdim64.dll" : "igdumdim32.dll")
                         : desc.VendorId == 0x5143 ? "d3d9on12.dll"
                                                   : "d3d9.dll";
    lstrcpynA(id->Driver, driver, sizeof(id->Driver));
    WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, id->Description, sizeof(id->Description), nullptr, nullptr);
    if (output) {
        DXGI_OUTPUT_DESC out = {};
        output->lpVtbl->GetDesc(output, &out);
        output->lpVtbl->Release(output);
        WideCharToMultiByte(CP_ACP, 0, out.DeviceName, -1, id->DeviceName, sizeof(id->DeviceName), nullptr, nullptr);
    }
    if (!(flags & D3DENUM_NO_DRIVERVERSION))
        id->DriverVersion = version;
    id->VendorId = desc.VendorId;
    id->DeviceId = desc.DeviceId;
    id->SubSysId = desc.SubSysId;
    id->Revision = desc.Revision;
    id->DeviceIdentifier = device_identifier(&desc, version);
    id->WHQLLevel = flags & D3DENUM_WHQL_LEVEL ? 1 : 0;
    return D3D_OK;
}

static UINT STDMETHODCALLTYPE d3d_get_adapter_mode_count(IDirect3D9Ex *iface, UINT adapter, D3DFORMAT format)
{
    return mode_count(adapter, format, D3DSCANLINEORDERING_UNKNOWN);
}

static HRESULT STDMETHODCALLTYPE d3d_enum_adapter_modes(IDirect3D9Ex *iface, UINT adapter, D3DFORMAT format,
                                                        UINT index, D3DDISPLAYMODE *mode)
{
    D3DDISPLAYMODEEX m;
    if (!mode)
        return D3DERR_INVALIDCALL;
    HRESULT hr = mode_at(adapter, format, D3DSCANLINEORDERING_UNKNOWN, index, &m);
    if (SUCCEEDED(hr))
        *mode = (D3DDISPLAYMODE){m.Width, m.Height, m.RefreshRate, m.Format};
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d_get_adapter_display_mode(IDirect3D9Ex *iface, UINT adapter,
                                                              D3DDISPLAYMODE *mode)
{
    D3DDISPLAYMODEEX m;
    if (!mode)
        return D3DERR_INVALIDCALL;
    HRESULT hr = current_mode(adapter, &m, nullptr);
    if (SUCCEEDED(hr))
        *mode = (D3DDISPLAYMODE){m.Width, m.Height, m.RefreshRate, m.Format};
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_type(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type,
                                                       D3DFORMAT display, D3DFORMAT back_buffer, BOOL windowed)
{
    if (adapter >= D9AdapterCount())
        return D3DERR_INVALIDCALL;
    if (type != D3DDEVTYPE_HAL)
        return D3DERR_NOTAVAILABLE;
    if (!is_display_format(display) || (display == D3DFMT_A2R10G10B10 && windowed))
        return D3DERR_NOTAVAILABLE;
    if (back_buffer == D3DFMT_UNKNOWN)
        return windowed ? D3D_OK : D3DERR_INVALIDCALL;
    BOOL match = back_buffer == display || (display == D3DFMT_X8R8G8B8 && back_buffer == D3DFMT_A8R8G8B8) ||
                 (display == D3DFMT_X1R5G5B5 && back_buffer == D3DFMT_A1R5G5B5);
    return match ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_format(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type,
                                                         D3DFORMAT display, DWORD usage, D3DRESOURCETYPE resource,
                                                         D3DFORMAT format)
{
    if (adapter >= D9AdapterCount())
        return D3DERR_INVALIDCALL;
    if (type != D3DDEVTYPE_HAL)
        return D3DERR_NOTAVAILABLE;
    return check_device_format(display, usage, resource, format);
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_multi_sample_type(IDirect3D9Ex *iface, UINT adapter,
                                                                    D3DDEVTYPE type, D3DFORMAT format,
                                                                    BOOL windowed, D3DMULTISAMPLE_TYPE samples,
                                                                    DWORD *qualities)
{
    if (adapter >= D9AdapterCount() || samples > D3DMULTISAMPLE_16_SAMPLES)
        return D3DERR_INVALIDCALL;
    if (type != D3DDEVTYPE_HAL)
        return D3DERR_NOTAVAILABLE;
    const D9Format *f = D9GetFormat(format);
    BOOL ok = f->d3d != D3DFMT_UNKNOWN && (f->flags & (D9_FORMAT_RENDER | D9_FORMAT_DEPTH)) && f->block == 1;
    if (samples == D3DMULTISAMPLE_NONE || samples == D3DMULTISAMPLE_NONMASKABLE)
        ok = ok || f->d3d != D3DFMT_UNKNOWN;
    else
        ok = ok && (samples == 2 || samples == 4 || samples == 8);
    if (!ok)
        return D3DERR_NOTAVAILABLE;
    /* nonmaskable quality levels are the counts of 2, 4 and 8 samples. */
    if (qualities)
        *qualities = samples == D3DMULTISAMPLE_NONMASKABLE ? 4 : 1;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_check_depth_stencil_match(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type,
                                                               D3DFORMAT display, D3DFORMAT target,
                                                               D3DFORMAT depth)
{
    if (adapter >= D9AdapterCount())
        return D3DERR_INVALIDCALL;
    const D9Format *t = D9GetFormat(target), *d = D9GetFormat(depth);
    if (!(t->flags & D9_FORMAT_RENDER) || !(d->flags & D9_FORMAT_DEPTH))
        return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_check_device_format_conversion(IDirect3D9Ex *iface, UINT adapter,
                                                                    D3DDEVTYPE type, D3DFORMAT source,
                                                                    D3DFORMAT target)
{
    if (adapter >= D9AdapterCount())
        return D3DERR_INVALIDCALL;
    const D9Format *s = D9GetFormat(source), *t = D9GetFormat(target);
    if (!(s->flags & D9_FORMAT_RENDER) || !(t->flags & D9_FORMAT_RENDER) || (s->flags | t->flags) & D9_FORMAT_NULL)
        return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_get_device_caps(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type,
                                                     D3DCAPS9 *caps)
{
    if (!caps || adapter >= D9AdapterCount())
        return D3DERR_INVALIDCALL;
    if (type != D3DDEVTYPE_HAL)
        return D3DERR_NOTAVAILABLE;
    D9FillCaps(adapter, caps);
    return D3D_OK;
}

static HMONITOR STDMETHODCALLTYPE d3d_get_adapter_monitor(IDirect3D9Ex *iface, UINT adapter)
{
    IDXGIAdapter1 *a;
    IDXGIOutput *output;
    if (FAILED(D9GetOutput(adapter, &a, &output)))
        return nullptr;
    a->lpVtbl->Release(a);
    if (!output)
        return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    DXGI_OUTPUT_DESC desc = {};
    output->lpVtbl->GetDesc(output, &desc);
    output->lpVtbl->Release(output);
    return desc.Monitor;
}

static HRESULT create_device(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                             D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *mode, IDirect3DDevice9Ex **out)
{
    if (!out || !params || adapter >= D9AdapterCount())
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (type != D3DDEVTYPE_HAL)
        return D3DERR_NOTAVAILABLE;
    D9Device *device;
    HRESULT hr = D9CreateDevice(iface, impl(iface)->ex, adapter, type, focus, flags, params, mode, &device);
    if (SUCCEEDED(hr))
        *out = &device->iface;
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d_create_device(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type, HWND focus,
                                                   DWORD flags, D3DPRESENT_PARAMETERS *params,
                                                   IDirect3DDevice9 **out)
{
    return create_device(iface, adapter, type, focus, flags, params, nullptr, (IDirect3DDevice9Ex **)out);
}

static UINT STDMETHODCALLTYPE d3d_get_adapter_mode_count_ex(IDirect3D9Ex *iface, UINT adapter,
                                                            const D3DDISPLAYMODEFILTER *filter)
{
    if (!filter)
        return 0;
    return mode_count(adapter, filter->Format, filter->ScanLineOrdering);
}

static HRESULT STDMETHODCALLTYPE d3d_enum_adapter_modes_ex(IDirect3D9Ex *iface, UINT adapter,
                                                           const D3DDISPLAYMODEFILTER *filter, UINT index,
                                                           D3DDISPLAYMODEEX *mode)
{
    if (!filter || !mode)
        return D3DERR_INVALIDCALL;
    return mode_at(adapter, filter->Format, filter->ScanLineOrdering, index, mode);
}

static HRESULT STDMETHODCALLTYPE d3d_get_adapter_display_mode_ex(IDirect3D9Ex *iface, UINT adapter,
                                                                 D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation)
{
    if (mode && mode->Size != sizeof(*mode))
        return D3DERR_INVALIDCALL;
    return current_mode(adapter, mode, rotation);
}

static HRESULT STDMETHODCALLTYPE d3d_create_device_ex(IDirect3D9Ex *iface, UINT adapter, D3DDEVTYPE type,
                                                      HWND focus, DWORD flags, D3DPRESENT_PARAMETERS *params,
                                                      D3DDISPLAYMODEEX *mode, IDirect3DDevice9Ex **out)
{
    return create_device(iface, adapter, type, focus, flags, params, mode, out);
}

static HRESULT STDMETHODCALLTYPE d3d_get_adapter_luid(IDirect3D9Ex *iface, UINT adapter, LUID *luid)
{
    IDXGIAdapter1 *a;
    if (!luid || FAILED(D9GetOutput(adapter, &a, nullptr)))
        return D3DERR_INVALIDCALL;
    DXGI_ADAPTER_DESC1 desc = {};
    a->lpVtbl->GetDesc1(a, &desc);
    a->lpVtbl->Release(a);
    *luid = desc.AdapterLuid;
    return D3D_OK;
}

static const IDirect3D9ExVtbl d3d_vtbl = {
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
    d3d_check_device_multi_sample_type,
    d3d_check_depth_stencil_match,
    d3d_check_device_format_conversion,
    d3d_get_device_caps,
    d3d_get_adapter_monitor,
    d3d_create_device,
    d3d_get_adapter_mode_count_ex,
    d3d_enum_adapter_modes_ex,
    d3d_get_adapter_display_mode_ex,
    d3d_create_device_ex,
    d3d_get_adapter_luid,
};

HRESULT D9CreateDirect3D(UINT sdk, BOOL ex, IDirect3D9Ex **out)
{
    if ((sdk & 0x7FFFFFFF) != D3D_SDK_VERSION && (sdk & 0x7FFFFFFF) != D3D9b_SDK_VERSION)
        return D3DERR_NOTAVAILABLE;
    if (!D9Factory())
        return D3DERR_NOTAVAILABLE;
    D9Direct3D *d3d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d3d));
    if (!d3d)
        return E_OUTOFMEMORY;
    d3d->iface.lpVtbl = &d3d_vtbl;
    d3d->refs = 1;
    d3d->ex = ex;
    *out = &d3d->iface;
    return D3D_OK;
}
