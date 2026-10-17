/*
 * outputs: a monitor and the modes of its adapter. the monitor's own size
 * and rate are the native mode; the usual smaller resolutions are offered
 * at the same rate, as a panel's scaler takes them, so games can list them.
 */
#include "dxgi_private.h"
#include <mime/display_modes.h>
#include <mime/ntuser.h>

static const GUID IID_D3D12Device = {0x189819F1, 0x1DB6, 0x4B57, {0xBE, 0x54, 0x18, 0x21, 0x33, 0x9B, 0x85, 0xF7}};

/* who owns an output, as dxgi.dll counts it: gamma works for exclusive owners alone. */
enum { OWNER_NONE, OWNER_SHARED, OWNER_EXCLUSIVE };

typedef struct DxgiOutput {
    IDXGIOutput6 iface;
    IMimeDXGIOutput mime;
    LONG refs;
    DxgiAdapter *adapter;
    HMONITOR monitor;
    UINT owner;
    /* the program's curve, its scale and offset kept apart as they are never applied. */
    DXGI_GAMMA_CONTROL *gamma;
    DXGI_RGB scale, offset;
    BOOL gamma_shown;
    MimePrivateData data;
} DxgiOutput;

static DxgiOutput *impl(IDXGIOutput6 *iface) { return CONTAINING_RECORD(iface, DxgiOutput, iface); }

static const GUID *const OUTPUT_IIDS[] = {
    &IID_IUnknown,     &IID_IDXGIObject,  &IID_IDXGIOutput,  &IID_IDXGIOutput1, &IID_IDXGIOutput2,
    &IID_IDXGIOutput3, &IID_IDXGIOutput4, &IID_IDXGIOutput5, &IID_IDXGIOutput6,
};

/* the formats a display can scan out, those swap chains take. */
static BOOL displayable(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return TRUE;
    default:
        return FALSE;
    }
}

/* the panel's own mode, which a program changing the display mode does not change. */
static void native_mode(DxgiOutput *output, UINT *width, UINT *height, UINT *rate)
{
    MONITORINFOEXW info = {sizeof(info)};
    DEVMODEW mode = {.dmSize = sizeof(mode)};
    GetMonitorInfoW(output->monitor, (MONITORINFO *)&info);
    *width = info.rcMonitor.right - info.rcMonitor.left;
    *height = info.rcMonitor.bottom - info.rcMonitor.top;
    *rate = 60;
    if (EnumDisplaySettingsW(info.szDevice, ENUM_REGISTRY_SETTINGS, &mode)) {
        *width = mode.dmPelsWidth;
        *height = mode.dmPelsHeight;
        if (mode.dmDisplayFrequency > 1)
            *rate = mode.dmDisplayFrequency;
    }
}

/* the modes in ascending order, the native one included, or their count with no array. */
static UINT list_modes(DxgiOutput *output, DXGI_FORMAT format, DXGI_MODE_DESC1 *modes, UINT capacity)
{
    UINT width, height, rate, n = 0;
    native_mode(output, &width, &height, &rate);
    for (UINT i = 0; i <= RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS); i++) {
        UINT w = width, h = height;
        if (i < RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS)) {
            w = MIME_DISPLAY_RESOLUTIONS[i].width;
            h = MIME_DISPLAY_RESOLUTIONS[i].height;
            if (w > width || h > height || (w == width && h == height))
                continue;
        }
        if (modes && n < capacity) {
            /* insertion keeps the order when the native mode comes last. */
            UINT at = n;
            while (at && (modes[at - 1].Width > w || (modes[at - 1].Width == w && modes[at - 1].Height > h))) {
                modes[at] = modes[at - 1];
                at--;
            }
            DXGI_MODE_DESC1 *m = &modes[at];
            memset(m, 0, sizeof(*m));
            m->Width = w;
            m->Height = h;
            m->RefreshRate.Numerator = rate;
            m->RefreshRate.Denominator = 1;
            m->Format = format;
            m->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
            m->Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        }
        n++;
    }
    return n;
}

static HRESULT STDMETHODCALLTYPE output_query(IDXGIOutput6 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    for (UINT i = 0; i < RTL_NUMBER_OF(OUTPUT_IIDS); i++)
        if (IsEqualGUID(riid, OUTPUT_IIDS[i])) {
            InterlockedIncrement(&impl(iface)->refs);
            *out = iface;
            return S_OK;
        }
    if (IsEqualGUID(riid, &IID_IMimeDXGIOutput)) {
        InterlockedIncrement(&impl(iface)->refs);
        *out = &impl(iface)->mime;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* the 1025 points of a curve sampled for the display's 256 entries. */
static void show_gamma(DxgiOutput *output)
{
    USHORT ramp[3][256];
    const DXGI_RGB *curve = output->gamma->GammaCurve;
    for (UINT i = 0; i < 256; i++) {
        UINT at = i * 1024 / 255;
        float t = i * 1024 / 255.0f - at;
        UINT next = at < 1024 ? at + 1 : at;
        float rgb[3] = {curve[at].Red + (curve[next].Red - curve[at].Red) * t,
                        curve[at].Green + (curve[next].Green - curve[at].Green) * t,
                        curve[at].Blue + (curve[next].Blue - curve[at].Blue) * t};
        for (UINT c = 0; c < 3; c++) {
            float v = rgb[c] > 0.0f ? rgb[c] < 1.0f ? rgb[c] : 1.0f : 0.0f;
            ramp[c][i] = (USHORT)(v * 65535.0f + 0.5f);
        }
    }
    NtGdiMimeGamma(MIME_GAMMA_DEVICE, &ramp[0][0]);
    output->gamma_shown = TRUE;
}

/* an owner gone: the display shows gdi's ramp again, as windows' kernel switches back. */
static void hide_gamma(DxgiOutput *output)
{
    if (output->gamma_shown)
        NtGdiMimeGamma(MIME_GAMMA_DEVICE, nullptr);
    output->gamma_shown = FALSE;
}

static void set_owner(DxgiOutput *output, UINT owner)
{
    output->owner = owner;
    if (owner == OWNER_EXCLUSIVE && output->gamma)
        show_gamma(output);
    else
        hide_gamma(output);
}

static ULONG STDMETHODCALLTYPE output_add_ref(IDXGIOutput6 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE output_release(IDXGIOutput6 *iface)
{
    DxgiOutput *output = impl(iface);
    ULONG refs = InterlockedDecrement(&output->refs);
    if (!refs) {
        hide_gamma(output);
        MimePrivateClear(&output->data);
        output->adapter->iface.lpVtbl->Release(&output->adapter->iface);
        HeapFree(GetProcessHeap(), 0, output->gamma);
        HeapFree(GetProcessHeap(), 0, output);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE output_set_private_data(IDXGIOutput6 *iface, REFGUID guid, UINT size,
                                                         const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE output_set_private_data_interface(IDXGIOutput6 *iface, REFGUID guid,
                                                                   const IUnknown *unknown)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE output_get_private_data(IDXGIOutput6 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE output_get_parent(IDXGIOutput6 *iface, REFIID riid, void **parent)
{
    if (!parent)
        return DXGI_ERROR_INVALID_CALL;
    IDXGIAdapter4 *adapter = &impl(iface)->adapter->iface;
    return adapter->lpVtbl->QueryInterface(adapter, riid, parent);
}

static HRESULT STDMETHODCALLTYPE output_get_desc(IDXGIOutput6 *iface, DXGI_OUTPUT_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(impl(iface)->monitor, (MONITORINFO *)&info))
        return DXGI_ERROR_NOT_FOUND;
    memset(desc, 0, sizeof(*desc));
    lstrcpynW(desc->DeviceName, info.szDevice, RTL_NUMBER_OF(desc->DeviceName));
    desc->DesktopCoordinates = info.rcMonitor;
    desc->AttachedToDesktop = TRUE;
    desc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
    desc->Monitor = impl(iface)->monitor;
    return S_OK;
}

static HRESULT get_modes1(IDXGIOutput6 *iface, DXGI_FORMAT format, UINT flags, UINT *count, DXGI_MODE_DESC1 *modes)
{
    if (!count)
        return DXGI_ERROR_INVALID_CALL;
    if (!displayable(format)) {
        *count = 0;
        return S_OK;
    }
    UINT n = list_modes(impl(iface), format, modes, modes ? *count : 0);
    if (modes && n > *count)
        return DXGI_ERROR_MORE_DATA;
    *count = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_get_display_mode_list1(IDXGIOutput6 *iface, DXGI_FORMAT format, UINT flags,
                                                               UINT *count, DXGI_MODE_DESC1 *modes)
{
    return get_modes1(iface, format, flags, count, modes);
}

static HRESULT STDMETHODCALLTYPE output_get_display_mode_list(IDXGIOutput6 *iface, DXGI_FORMAT format, UINT flags,
                                                              UINT *count, DXGI_MODE_DESC *modes)
{
    if (!count)
        return DXGI_ERROR_INVALID_CALL;
    if (!modes)
        return get_modes1(iface, format, flags, count, nullptr);
    DXGI_MODE_DESC1 *all = HeapAlloc(GetProcessHeap(), 0, (RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS) + 1) * sizeof(*all));
    if (!all)
        return E_OUTOFMEMORY;
    UINT n = RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS) + 1;
    HRESULT hr = get_modes1(iface, format, flags, &n, all);
    if (SUCCEEDED(hr)) {
        for (UINT i = 0; i < n && i < *count; i++)
            memcpy(&modes[i], &all[i], sizeof(modes[i]));
        if (n > *count)
            hr = DXGI_ERROR_MORE_DATA;
        else
            *count = n;
    }
    HeapFree(GetProcessHeap(), 0, all);
    return hr;
}

/* the listed mode nearest in size, then in rate, of the format asked or the first displayable one. */
static HRESULT STDMETHODCALLTYPE output_find_closest_matching_mode1(IDXGIOutput6 *iface, const DXGI_MODE_DESC1 *mode,
                                                                    DXGI_MODE_DESC1 *closest, IUnknown *device)
{
    if (!mode || !closest)
        return DXGI_ERROR_INVALID_CALL;
    if ((!mode->Width) != (!mode->Height) || (!mode->RefreshRate.Numerator) != (!mode->RefreshRate.Denominator))
        return DXGI_ERROR_INVALID_CALL;
    if (mode->Format == DXGI_FORMAT_UNKNOWN && !device)
        return DXGI_ERROR_INVALID_CALL;
    DXGI_FORMAT format = mode->Format == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_R8G8B8A8_UNORM : mode->Format;
    DXGI_MODE_DESC1 modes[RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS) + 1];
    UINT n = RTL_NUMBER_OF(modes);
    get_modes1(iface, format, 0, &n, modes);
    if (!n)
        return DXGI_ERROR_NOT_FOUND;
    UINT width, height, rate;
    native_mode(impl(iface), &width, &height, &rate);
    UINT64 want_w = mode->Width ? mode->Width : width, want_h = mode->Height ? mode->Height : height;
    UINT best = 0;
    UINT64 best_score = ~0ull;
    for (UINT i = 0; i < n; i++) {
        INT64 dw = (INT64)modes[i].Width - (INT64)want_w, dh = (INT64)modes[i].Height - (INT64)want_h;
        UINT64 score = (UINT64)(dw * dw + dh * dh);
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    *closest = modes[best];
    if (mode->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED)
        closest->ScanlineOrdering = mode->ScanlineOrdering;
    if (mode->Scaling != DXGI_MODE_SCALING_UNSPECIFIED)
        closest->Scaling = mode->Scaling;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_find_closest_matching_mode(IDXGIOutput6 *iface, const DXGI_MODE_DESC *mode,
                                                                   DXGI_MODE_DESC *closest, IUnknown *device)
{
    if (!mode || !closest)
        return DXGI_ERROR_INVALID_CALL;
    DXGI_MODE_DESC1 in, out;
    memcpy(&in, mode, sizeof(*mode));
    in.Stereo = FALSE;
    HRESULT hr = output_find_closest_matching_mode1(iface, &in, &out, device);
    if (SUCCEEDED(hr))
        memcpy(closest, &out, sizeof(*closest));
    return hr;
}

/* sleeps to the next refresh of the panel, on the performance counter's clock. */
static HRESULT STDMETHODCALLTYPE output_wait_for_vblank(IDXGIOutput6 *iface)
{
    UINT width, height, rate;
    native_mode(impl(iface), &width, &height, &rate);
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    LONGLONG period = frequency.QuadPart / rate;
    LONGLONG left = period - now.QuadPart % period;
    DWORD ms = (DWORD)(left * 1000 / frequency.QuadPart);
    if (ms)
        Sleep(ms);
    return S_OK;
}

/* direct3d 12 devices cannot take an output, their swap chains alone own it. */
static HRESULT STDMETHODCALLTYPE output_take_ownership(IDXGIOutput6 *iface, IUnknown *device, BOOL exclusive)
{
    if (!device)
        return DXGI_ERROR_INVALID_CALL;
    IUnknown *d3d12;
    if (SUCCEEDED(device->lpVtbl->QueryInterface(device, &IID_D3D12Device, (void **)&d3d12))) {
        d3d12->lpVtbl->Release(d3d12);
        return DXGI_ERROR_INVALID_CALL;
    }
    set_owner(impl(iface), exclusive ? OWNER_EXCLUSIVE : OWNER_SHARED);
    return S_OK;
}

static void STDMETHODCALLTYPE output_release_ownership(IDXGIOutput6 *iface)
{
    set_owner(impl(iface), OWNER_NONE);
}

static HRESULT STDMETHODCALLTYPE output_get_gamma_control_capabilities(IDXGIOutput6 *iface,
                                                                       DXGI_GAMMA_CONTROL_CAPABILITIES *caps)
{
    if (impl(iface)->owner != OWNER_EXCLUSIVE)
        return DXGI_ERROR_INVALID_CALL;
    caps->ScaleAndOffsetSupported = FALSE;
    caps->MaxConvertedValue = 1.0f;
    caps->MinConvertedValue = 0.0f;
    caps->NumGammaControlPoints = 1025;
    for (UINT i = 0; i < 1025; i++)
        caps->ControlPointPositions[i] = i / 1024.0f;
    return S_OK;
}

/* the curve shows as it is while the output is owned exclusively; scale and offset are not supported. */
static HRESULT STDMETHODCALLTYPE output_set_gamma_control(IDXGIOutput6 *iface, const DXGI_GAMMA_CONTROL *control)
{
    DxgiOutput *output = impl(iface);
    if (output->owner != OWNER_EXCLUSIVE || !control)
        return DXGI_ERROR_INVALID_CALL;
    if (!output->gamma && !(output->gamma = HeapAlloc(GetProcessHeap(), 0, sizeof(*output->gamma))))
        return E_OUTOFMEMORY;
    *output->gamma = *control;
    output->scale = control->Scale;
    output->offset = control->Offset;
    output->gamma->Scale = (DXGI_RGB){1.0f, 1.0f, 1.0f};
    output->gamma->Offset = (DXGI_RGB){};
    show_gamma(output);
    return S_OK;
}

/* what the program set, which an output never set has not. */
static HRESULT STDMETHODCALLTYPE output_get_gamma_control(IDXGIOutput6 *iface, DXGI_GAMMA_CONTROL *control)
{
    DxgiOutput *output = impl(iface);
    if (output->owner != OWNER_EXCLUSIVE || !control || !output->gamma)
        return DXGI_ERROR_INVALID_CALL;
    *control = *output->gamma;
    control->Scale = output->scale;
    control->Offset = output->offset;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_set_display_surface(IDXGIOutput6 *iface, IDXGISurface *surface)
{
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE output_get_display_surface_data(IDXGIOutput6 *iface, IDXGISurface *surface)
{
    return DXGI_ERROR_UNSUPPORTED;
}

/* the count of refreshes since boot, at the time of the last one. */
static HRESULT STDMETHODCALLTYPE output_get_frame_statistics(IDXGIOutput6 *iface, DXGI_FRAME_STATISTICS *stats)
{
    if (!stats)
        return E_INVALIDARG;
    UINT width, height, rate;
    native_mode(impl(iface), &width, &height, &rate);
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    LONGLONG period = frequency.QuadPart / rate;
    memset(stats, 0, sizeof(*stats));
    stats->SyncRefreshCount = (UINT)(now.QuadPart / period);
    stats->SyncQPCTime.QuadPart = now.QuadPart - now.QuadPart % period;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_get_display_surface_data1(IDXGIOutput6 *iface, IDXGIResource *resource)
{
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE output_duplicate_output(IDXGIOutput6 *iface, IUnknown *device,
                                                         IDXGIOutputDuplication **duplication)
{
    if (duplication)
        *duplication = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
}

static BOOL STDMETHODCALLTYPE output_supports_overlays(IDXGIOutput6 *iface) { return FALSE; }

static HRESULT STDMETHODCALLTYPE output_check_overlay_support(IDXGIOutput6 *iface, DXGI_FORMAT format,
                                                              IUnknown *device, UINT *flags)
{
    if (!device || !flags)
        return E_INVALIDARG;
    *flags = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_check_overlay_color_space_support(IDXGIOutput6 *iface, DXGI_FORMAT format,
                                                                          DXGI_COLOR_SPACE_TYPE space,
                                                                          IUnknown *device, UINT *flags)
{
    if (!device || !flags)
        return E_INVALIDARG;
    *flags = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_duplicate_output1(IDXGIOutput6 *iface, IUnknown *device, UINT flags,
                                                          UINT count, const DXGI_FORMAT *formats,
                                                          IDXGIOutputDuplication **duplication)
{
    return output_duplicate_output(iface, device, duplication);
}

/* an sdr panel with srgb primaries. */
static HRESULT STDMETHODCALLTYPE output_get_desc1(IDXGIOutput6 *iface, DXGI_OUTPUT_DESC1 *desc)
{
    if (!desc)
        return E_INVALIDARG;
    DXGI_OUTPUT_DESC d;
    HRESULT hr = output_get_desc(iface, &d);
    if (FAILED(hr))
        return hr;
    memset(desc, 0, sizeof(*desc));
    memcpy(desc, &d, sizeof(d));
    desc->BitsPerColor = 8;
    desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    desc->RedPrimary[0] = 0.640f, desc->RedPrimary[1] = 0.330f;
    desc->GreenPrimary[0] = 0.300f, desc->GreenPrimary[1] = 0.600f;
    desc->BluePrimary[0] = 0.150f, desc->BluePrimary[1] = 0.060f;
    desc->WhitePoint[0] = 0.3127f, desc->WhitePoint[1] = 0.3290f;
    desc->MinLuminance = 0.5f;
    desc->MaxLuminance = 270.0f;
    desc->MaxFullFrameLuminance = 270.0f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE output_check_hardware_composition_support(IDXGIOutput6 *iface, UINT *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = 0;
    return S_OK;
}

static const IDXGIOutput6Vtbl output_vtbl = {
    output_query,
    output_add_ref,
    output_release,
    output_set_private_data,
    output_set_private_data_interface,
    output_get_private_data,
    output_get_parent,
    output_get_desc,
    output_get_display_mode_list,
    output_find_closest_matching_mode,
    output_wait_for_vblank,
    output_take_ownership,
    output_release_ownership,
    output_get_gamma_control_capabilities,
    output_set_gamma_control,
    output_get_gamma_control,
    output_set_display_surface,
    output_get_display_surface_data,
    output_get_frame_statistics,
    output_get_display_mode_list1,
    output_find_closest_matching_mode1,
    output_get_display_surface_data1,
    output_duplicate_output,
    output_supports_overlays,
    output_check_overlay_support,
    output_check_overlay_color_space_support,
    output_duplicate_output1,
    output_get_desc1,
    output_check_hardware_composition_support,
};

static DxgiOutput *from_mime(IMimeDXGIOutput *iface) { return CONTAINING_RECORD(iface, DxgiOutput, mime); }

static HRESULT STDMETHODCALLTYPE mime_query(IMimeDXGIOutput *iface, REFIID riid, void **out)
{
    return output_query(&from_mime(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE mime_add_ref(IMimeDXGIOutput *iface)
{
    return output_add_ref(&from_mime(iface)->iface);
}

static ULONG STDMETHODCALLTYPE mime_release(IMimeDXGIOutput *iface)
{
    return output_release(&from_mime(iface)->iface);
}

static void STDMETHODCALLTYPE mime_set_fullscreen_owner(IMimeDXGIOutput *iface, BOOL owned)
{
    set_owner(from_mime(iface), owned ? OWNER_EXCLUSIVE : OWNER_NONE);
}

static const IMimeDXGIOutputVtbl mime_vtbl = {mime_query, mime_add_ref, mime_release, mime_set_fullscreen_owner};

HRESULT DxgiCreateOutput(DxgiAdapter *adapter, HMONITOR monitor, IDXGIOutput **out)
{
    DxgiOutput *output = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*output));
    if (!output)
        return E_OUTOFMEMORY;
    output->iface.lpVtbl = &output_vtbl;
    output->mime.lpVtbl = &mime_vtbl;
    output->refs = 1;
    output->adapter = adapter;
    adapter->iface.lpVtbl->AddRef(&adapter->iface);
    output->monitor = monitor;
    *out = (IDXGIOutput *)&output->iface;
    return S_OK;
}
