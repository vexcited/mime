#define MIME_BUILD_DXVA2
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <mime/win32/dxva2api.h>
#include <rtl.h>

int _fltused = 0x9875;

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)
#define D3DFMT_YUY2_ ((D3DFORMAT)FCC('Y', 'U', 'Y', '2'))
#define D3DFMT_AYUV_ ((D3DFORMAT)FCC('A', 'Y', 'U', 'V'))

MIME_DEFINE_GUID(DXVA2_SurfaceDxvaType_, 0xD0A9A445, 0x4FD4, 0x11DA, 0x9E, 0x55, 0x00, 0x0C, 0x6E, 0x39, 0xA7, 0x7A);

/* ------------------------------------------------------------------ the software device of dxva2 */

static BOOL progressive(UINT sample)
{
    return !(sample & ~2u) || sample == DXVA2_SampleSubStream;
}

static BOOL interleaved(UINT sample)
{
    return sample == 3 || sample == 4;
}

/* the inputs dxva2's own processor converts: rgb32 and the two packed yuv formats, only the yuv ones when interlaced. */
static BOOL input_supported(D3DFORMAT format, UINT sample)
{
    if (progressive(sample))
        return format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8 || format == D3DFMT_AYUV_ ||
               format == D3DFMT_YUY2_;
    return format == D3DFMT_AYUV_ || format == D3DFMT_YUY2_;
}

static BOOL target_supported(D3DFORMAT format)
{
    return format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8;
}

static const D3DFORMAT SW_TARGETS[2] = {D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8};
static const D3DFORMAT SW_SUBSTREAMS[4] = {D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_AYUV_, D3DFMT_YUY2_};

static HRESULT sw_caps(const DXVA2_VideoDesc *desc, D3DFORMAT target, DXVA2_VideoProcessorCaps *caps)
{
    UINT sample = desc->SampleFormat.value & 0xFF;
    if (!input_supported(desc->Format, sample) || !target_supported(target))
        return D3DERR_INVALIDCALL;
    memset(caps, 0, sizeof(*caps));
    caps->DeviceCaps = DXVA2_VPDev_SoftwareDevice;
    caps->InputPool = D3DPOOL_SYSTEMMEM;
    caps->VideoProcessorOperations = 0x10F7;
    if (interleaved(sample) || sample == 5 || sample == 6)
        caps->DeinterlaceTechnology = 2;
    return D3D_OK;
}

/* ------------------------------------------------------------------ the service */

typedef struct Service {
    IUnknown inner;
    IDirectXVideoDecoderService decoder;
    IDirectXVideoProcessorService processor;
    LONG refs;
    CRITICAL_SECTION lock;
    IDirect3DDevice9 *device;
} Service;

typedef struct Processor {
    IDirectXVideoProcessor iface;
    LONG refs;
    Service *service;
    GUID guid;
    DXVA2_VideoDesc desc;
    D3DFORMAT target;
    UINT substreams;
    DXVA2_VideoProcessorCaps caps;
    UINT *index;
    UINT index_from, index_to, index_count;
    UINT32 *convert;
    UINT convert_size;
} Processor;

#define SERVICE_OF(field, p) CONTAINING_RECORD(p, Service, field)

static BOOL is_software(REFGUID guid)
{
    return IsEqualGUID(guid, &DXVA2_VideoProcSoftwareDevice);
}

static HRESULT validate_desc(const DXVA2_VideoDesc *desc)
{
    if (desc->InputSampleFreq.Numerator && !desc->InputSampleFreq.Denominator)
        return E_INVALIDARG;
    if (desc->OutputFrameFreq.Numerator && !desc->OutputFrameFreq.Denominator)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT service_query(Service *s, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IDirectXVideoAccelerationService) ||
        IsEqualGUID(riid, &IID_IDirectXVideoProcessorService)) {
        if (!out)
            return E_POINTER;
        *out = &s->processor;
    } else if (IsEqualGUID(riid, &IID_IDirectXVideoDecoderService)) {
        if (!out)
            return E_POINTER;
        *out = &s->decoder;
    } else {
        if (!out)
            return E_POINTER;
        if (!IsEqualGUID(riid, &IID_IUnknown)) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        *out = &s->inner;
    }
    InterlockedIncrement(&s->refs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE inner_QueryInterface(IUnknown *This, REFIID riid, void **out)
{
    return service_query(SERVICE_OF(inner, This), riid, out);
}

static ULONG STDMETHODCALLTYPE inner_AddRef(IUnknown *This)
{
    return InterlockedIncrement(&SERVICE_OF(inner, This)->refs);
}

static ULONG STDMETHODCALLTYPE inner_Release(IUnknown *This)
{
    Service *s = SERVICE_OF(inner, This);
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        s->device->lpVtbl->Release(s->device);
        DeleteCriticalSection(&s->lock);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return refs;
}

static const IUnknownVtbl inner_vtbl = {inner_QueryInterface, inner_AddRef, inner_Release};

/*
 * one plain surface per buffer, tagged with the kind dxva2 records. a software target falls back to system memory when
 * the device has no such surface in its own pool.
 */
static HRESULT create_surfaces(Service *s, UINT width, UINT height, UINT back_buffers, D3DFORMAT format, D3DPOOL pool,
                               DWORD usage, DWORD type, IDirect3DSurface9 **surfaces, HANDLE *shared)
{
    DWORD kind;
    if (type == DXVA2_VideoDecoderRenderTarget)
        kind = 2;
    else if (type == DXVA2_VideoProcessorRenderTarget)
        kind = 3;
    else if (type == DXVA2_VideoSoftwareRenderTarget)
        kind = 0;
    else
        return E_INVALIDARG;
    if (!surfaces)
        return E_POINTER;
    if (format == D3DFMT_UNKNOWN || usage)
        return D3DERR_INVALIDCALL;
    HRESULT hr = S_OK;
    EnterCriticalSection(&s->lock);
    for (UINT i = 0; i <= back_buffers; i++) {
        hr = s->device->lpVtbl->CreateOffscreenPlainSurface(s->device, width, height, format, pool, &surfaces[i],
                                                           shared);
        if (FAILED(hr) && pool != D3DPOOL_DEFAULT)
            hr = s->device->lpVtbl->CreateOffscreenPlainSurface(s->device, width, height, format, D3DPOOL_DEFAULT,
                                                               &surfaces[i], shared);
        if (SUCCEEDED(hr))
            hr = surfaces[i]->lpVtbl->SetPrivateData(surfaces[i], &DXVA2_SurfaceDxvaType_, &kind, 4, 0);
        if (FAILED(hr)) {
            if (surfaces[i])
                surfaces[i]->lpVtbl->Release(surfaces[i]);
            while (i--)
                surfaces[i]->lpVtbl->Release(surfaces[i]);
            break;
        }
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

/* --- the decoder side: no decoder is served, as on a machine whose driver offers none */

static HRESULT STDMETHODCALLTYPE decoder_QueryInterface(IDirectXVideoDecoderService *This, REFIID riid, void **out)
{
    return service_query(SERVICE_OF(decoder, This), riid, out);
}

static ULONG STDMETHODCALLTYPE decoder_AddRef(IDirectXVideoDecoderService *This)
{
    return InterlockedIncrement(&SERVICE_OF(decoder, This)->refs);
}

static ULONG STDMETHODCALLTYPE decoder_Release(IDirectXVideoDecoderService *This)
{
    return inner_Release(&SERVICE_OF(decoder, This)->inner);
}

static HRESULT STDMETHODCALLTYPE decoder_CreateSurface(IDirectXVideoDecoderService *This, UINT width, UINT height,
                                                       UINT back_buffers, D3DFORMAT format, D3DPOOL pool, DWORD usage,
                                                       DWORD type, IDirect3DSurface9 **surfaces, HANDLE *shared)
{
    return create_surfaces(SERVICE_OF(decoder, This), width, height, back_buffers, format, pool, usage, type, surfaces,
                           shared);
}

static HRESULT STDMETHODCALLTYPE decoder_GetDecoderDeviceGuids(IDirectXVideoDecoderService *This, UINT *count,
                                                               GUID **guids)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    if (guids) {
        *guids = nullptr;
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_GetDecoderRenderTargets(IDirectXVideoDecoderService *This, REFGUID guid,
                                                                 UINT *count, D3DFORMAT **formats)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    if (formats)
        *formats = nullptr;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE decoder_GetDecoderConfigurations(IDirectXVideoDecoderService *This, REFGUID guid,
                                                                  const DXVA2_VideoDesc *desc, void *reserved,
                                                                  UINT *count, void **configs)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    if (configs)
        *configs = nullptr;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE decoder_CreateVideoDecoder(IDirectXVideoDecoderService *This, REFGUID guid,
                                                            const DXVA2_VideoDesc *desc, const void *config,
                                                            IDirect3DSurface9 **targets, UINT count, void **decoder)
{
    if (decoder)
        *decoder = nullptr;
    return E_INVALIDARG;
}

static const IDirectXVideoDecoderServiceVtbl decoder_vtbl = {
    decoder_QueryInterface,          decoder_AddRef,
    decoder_Release,                 decoder_CreateSurface,
    decoder_GetDecoderDeviceGuids,   decoder_GetDecoderRenderTargets,
    decoder_GetDecoderConfigurations, decoder_CreateVideoDecoder,
};

/* --- the processor side */

static HRESULT STDMETHODCALLTYPE processor_QueryInterface(IDirectXVideoProcessorService *This, REFIID riid,
                                                          void **out)
{
    return service_query(SERVICE_OF(processor, This), riid, out);
}

static ULONG STDMETHODCALLTYPE processor_AddRef(IDirectXVideoProcessorService *This)
{
    return InterlockedIncrement(&SERVICE_OF(processor, This)->refs);
}

static ULONG STDMETHODCALLTYPE processor_Release(IDirectXVideoProcessorService *This)
{
    return inner_Release(&SERVICE_OF(processor, This)->inner);
}

static HRESULT STDMETHODCALLTYPE processor_CreateSurface(IDirectXVideoProcessorService *This, UINT width, UINT height,
                                                         UINT back_buffers, D3DFORMAT format, D3DPOOL pool,
                                                         DWORD usage, DWORD type, IDirect3DSurface9 **surfaces,
                                                         HANDLE *shared)
{
    return create_surfaces(SERVICE_OF(processor, This), width, height, back_buffers, format, pool, usage, type,
                           surfaces, shared);
}

/* a program's own software device is not taken: the table would have to call back into its code. */
static HRESULT STDMETHODCALLTYPE processor_RegisterVideoProcessorSoftwareDevice(IDirectXVideoProcessorService *This,
                                                                                void *callbacks)
{
    if (callbacks && *(const DWORD *)callbacks != 112)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE processor_GetVideoProcessorDeviceGuids(IDirectXVideoProcessorService *This,
                                                                        const DXVA2_VideoDesc *desc, UINT *count,
                                                                        GUID **guids)
{
    Service *s = SERVICE_OF(processor, This);
    EnterCriticalSection(&s->lock);
    HRESULT hr = validate_desc(desc);
    UINT n = 0;
    GUID *out = nullptr;
    if (SUCCEEDED(hr)) {
        n = input_supported(desc->Format, desc->SampleFormat.value & 0xFF) ? 1 : 0;
        if (guids) {
            if (!n) {
                hr = E_FAIL;
            } else if (!(out = CoTaskMemAlloc(sizeof(GUID)))) {
                hr = E_OUTOFMEMORY;
            } else {
                out[0] = DXVA2_VideoProcSoftwareDevice;
            }
        }
    }
    if (guids)
        *guids = out;
    *count = n;
    LeaveCriticalSection(&s->lock);
    return hr;
}

/*
 * a list the caller frees, or only its length when no list is asked for. a device other than dxva2's own answers as a
 * driver without video processing does, with no formats.
 */
static HRESULT format_list(BOOL software, BOOL supported, const D3DFORMAT *list, UINT length, UINT *count,
                           D3DFORMAT **formats)
{
    UINT n = software && supported ? length : 0;
    *count = n;
    if (!formats)
        return S_OK;
    *formats = nullptr;
    if (!n)
        return E_FAIL;
    if (!(*formats = CoTaskMemAlloc(n * sizeof(D3DFORMAT))))
        return E_OUTOFMEMORY;
    memcpy(*formats, list, n * sizeof(D3DFORMAT));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE processor_GetVideoProcessorRenderTargets(IDirectXVideoProcessorService *This,
                                                                          REFGUID device, const DXVA2_VideoDesc *desc,
                                                                          UINT *count, D3DFORMAT **formats)
{
    Service *s = SERVICE_OF(processor, This);
    EnterCriticalSection(&s->lock);
    HRESULT hr = validate_desc(desc);
    if (SUCCEEDED(hr)) {
        *count = 0;
        hr = format_list(is_software(device), input_supported(desc->Format, desc->SampleFormat.value & 0xFF),
                         SW_TARGETS, 2, count, formats);
    } else if (formats) {
        *formats = nullptr;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetVideoProcessorSubStreamFormats(IDirectXVideoProcessorService *This,
                                                                             REFGUID device,
                                                                             const DXVA2_VideoDesc *desc,
                                                                             D3DFORMAT target, UINT *count,
                                                                             D3DFORMAT **formats)
{
    Service *s = SERVICE_OF(processor, This);
    EnterCriticalSection(&s->lock);
    HRESULT hr = validate_desc(desc);
    if (SUCCEEDED(hr)) {
        *count = 0;
        BOOL supported =
            input_supported(desc->Format, desc->SampleFormat.value & 0xFF) && target_supported(target);
        /* the software device counts no formats rather than failing. */
        hr = format_list(is_software(device), TRUE, SW_SUBSTREAMS, supported ? 4 : 0, count, formats);
    } else if (formats) {
        *formats = nullptr;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetVideoProcessorCaps(IDirectXVideoProcessorService *This, REFGUID device,
                                                                 const DXVA2_VideoDesc *desc, D3DFORMAT target,
                                                                 DXVA2_VideoProcessorCaps *caps)
{
    Service *s = SERVICE_OF(processor, This);
    EnterCriticalSection(&s->lock);
    HRESULT hr = validate_desc(desc);
    if (SUCCEEDED(hr)) {
        if (is_software(device)) {
            hr = sw_caps(desc, target, caps);
        } else {
            memset(caps, 0, sizeof(*caps));
            caps->DeviceCaps = DXVA2_VPDev_HardwareDevice;
        }
        if (SUCCEEDED(hr)) {
            caps->Reserved = 0;
            UINT refs = caps->NumForwardRefSamples + caps->NumBackwardRefSamples;
            if (refs < caps->NumForwardRefSamples || refs > 31)
                hr = (HRESULT)0x88760827;
        }
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

/* the software device has neither procamp nor filters. */
static HRESULT STDMETHODCALLTYPE processor_GetProcAmpRange(IDirectXVideoProcessorService *This, REFGUID device,
                                                           const DXVA2_VideoDesc *desc, D3DFORMAT target,
                                                           UINT property, DXVA2_ValueRange *range)
{
    if (property != 1 && property != 2 && property != 4 && property != 8)
        return E_INVALIDARG;
    HRESULT hr = validate_desc(desc);
    if (FAILED(hr))
        return hr;
    if (is_software(device))
        return D3DERR_INVALIDCALL;
    memset(range, 0, sizeof(*range));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE processor_GetFilterPropertyRange(IDirectXVideoProcessorService *This, REFGUID device,
                                                                  const DXVA2_VideoDesc *desc, D3DFORMAT target,
                                                                  UINT setting, DXVA2_ValueRange *range)
{
    if (setting - 1 > 11)
        return E_INVALIDARG;
    HRESULT hr = validate_desc(desc);
    if (FAILED(hr))
        return hr;
    if (is_software(device))
        return D3DERR_INVALIDCALL;
    memset(range, 0, sizeof(*range));
    return S_OK;
}

static const IDirectXVideoProcessorVtbl device_vtbl;

static HRESULT STDMETHODCALLTYPE processor_CreateVideoProcessor(IDirectXVideoProcessorService *This, REFGUID device,
                                                                const DXVA2_VideoDesc *desc, D3DFORMAT target,
                                                                UINT substreams, IDirectXVideoProcessor **out)
{
    Service *s = SERVICE_OF(processor, This);
    EnterCriticalSection(&s->lock);
    *out = nullptr;
    HRESULT hr = validate_desc(desc);
    Processor *p = nullptr;
    if (FAILED(hr))
        goto done;
    if (substreams > 15) {
        hr = E_INVALIDARG;
        goto done;
    }
    if (!is_software(device)) {
        hr = E_NOTIMPL;
        goto done;
    }
    DXVA2_VideoProcessorCaps caps;
    hr = sw_caps(desc, target, &caps);
    if (FAILED(hr))
        goto done;
    p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    p->iface.lpVtbl = &device_vtbl;
    p->refs = 1;
    p->service = s;
    p->guid = *device;
    p->desc = *desc;
    p->target = target;
    p->substreams = substreams;
    p->caps = caps;
    InterlockedIncrement(&s->refs);
    *out = &p->iface;
done:
    LeaveCriticalSection(&s->lock);
    return hr;
}

static const IDirectXVideoProcessorServiceVtbl processor_vtbl = {
    processor_QueryInterface,
    processor_AddRef,
    processor_Release,
    processor_CreateSurface,
    processor_RegisterVideoProcessorSoftwareDevice,
    processor_GetVideoProcessorDeviceGuids,
    processor_GetVideoProcessorRenderTargets,
    processor_GetVideoProcessorSubStreamFormats,
    processor_GetVideoProcessorCaps,
    processor_GetProcAmpRange,
    processor_GetFilterPropertyRange,
    processor_CreateVideoProcessor,
};

/* ------------------------------------------------------------------ the conversions of dxva2's software device */

/*
 * the tables dxva2 builds once, from its constants, truncated toward zero: luma scaled to full range or kept, and the
 * chroma terms of bt.601 and bt.709 for full and studio range output.
 */
typedef struct {
    SHORT y255[256], y235[256];
    SHORT r601[256], gu601[256], gv601[256], b601[256];
    SHORT r601s[256], gu601s[256], gv601s[256], b601s[256];
    SHORT r709[256], gu709[256], gv709[256], b709[256];
    SHORT r709s[256], gu709s[256], gv709s[256], b709s[256];
} Tables;

static Tables g_tables;
static INIT_ONCE g_tables_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_tables(INIT_ONCE *once, void *param, void **context)
{
    Tables *t = &g_tables;
    for (int i = 0; i < 256; i++) {
        double c = i - 128, y = i - 16;
        t->y255[i] = (SHORT)(int)(y * 298.082 * 0.00390625);
        t->y235[i] = (SHORT)(16 - (int)(y * -256.0 * 0.00390625));
        t->r601[i] = (SHORT)(int)(c * 408.583 * 0.00390625);
        t->gu601[i] = (SHORT)(int)(c * -100.291 * 0.00390625);
        t->gv601[i] = (SHORT)(int)(c * -208.12 * 0.00390625);
        t->b601[i] = (SHORT)(int)(c * 516.4109999999999 * 0.00390625);
        t->r601s[i] = (SHORT)(int)(c * 350.901 * 0.00390625);
        t->gu601s[i] = (SHORT)(int)(c * -86.13200000000001 * 0.00390625);
        t->gv601s[i] = (SHORT)(int)(c * -178.738 * 0.00390625);
        t->b601s[i] = (SHORT)(int)(c * 443.506 * 0.00390625);
        t->r709[i] = (SHORT)(int)(c * 458.942 * 0.00390625);
        t->gu709[i] = (SHORT)(int)(c * -54.592 * 0.00390625);
        t->gv709[i] = (SHORT)(int)(c * -136.425 * 0.00390625);
        t->b709[i] = (SHORT)(int)(c * 540.775 * 0.00390625);
        t->r709s[i] = (SHORT)(int)(c * 394.15 * 0.00390625);
        t->gu709s[i] = (SHORT)(int)(c * -46.885 * 0.00390625);
        t->gv709s[i] = (SHORT)(int)(c * -117.165 * 0.00390625);
        t->b709s[i] = (SHORT)(int)(c * 464.43 * 0.00390625);
    }
    return TRUE;
}

typedef struct {
    const SHORT *y, *r, *gu, *gv, *b;
} Conversion;

/* evr's GetColorSpaceConversionParameters: the source matrix, guessed from the height, and the output range. */
static Conversion conversion(DXVA2_ExtendedFormat source, DXVA2_ExtendedFormat dest, UINT height)
{
    InitOnceExecuteOnce(&g_tables_once, make_tables, nullptr, nullptr);
    if ((source.value & 0xFF) - 5 <= 1)
        height *= 2;
    UINT matrix = source.value & 0x38000;
    if (matrix != 0x10000 && matrix != 0x8000)
        matrix = height <= 576 ? 0x10000 : 0x8000;
    BOOL bt601 = matrix == 0x10000, full = (dest.value & 0x7000) != 0x2000;
    const Tables *t = &g_tables;
    if (bt601)
        return full ? (Conversion){t->y255, t->r601, t->gu601, t->gv601, t->b601}
                    : (Conversion){t->y235, t->r601s, t->gu601s, t->gv601s, t->b601s};
    return full ? (Conversion){t->y255, t->r709, t->gu709, t->gv709, t->b709}
                : (Conversion){t->y235, t->r709s, t->gu709s, t->gv709s, t->b709s};
}

static BYTE clamp(int v)
{
    return v > 255 ? 255 : v <= 0 ? 0 : (BYTE)v;
}

static UINT32 to_rgb(const Conversion *c, BYTE a, BYTE y, BYTE u, BYTE v)
{
    BYTE r = clamp(c->y[y] + c->r[v]);
    BYTE g = clamp(c->y[y] + c->gv[v] + c->gu[u]);
    BYTE b = clamp(c->y[y] + c->b[u]);
    return (UINT32)a << 24 | (UINT32)r << 16 | (UINT32)g << 8 | b;
}

static UINT32 blend_pixel(UINT32 src, UINT32 dst)
{
    UINT a = src >> 24;
    if (!a)
        return dst | 0xFF000000;
    if (a == 255)
        return src | 0xFF000000;
    UINT32 out = 0xFF000000;
    for (int c = 0; c < 24; c += 8)
        out |= (((src >> c & 0xFF) * a + (255 - a) * (dst >> c & 0xFF)) / 255) << c;
    return out;
}

static UINT32 blend_planar(UINT32 src, UINT32 dst, UINT planar)
{
    UINT32 out = 0xFF000000;
    for (int c = 0; c < 24; c += 8)
        out |= ((planar * (src >> c & 0xFF) + (255 - planar) * (dst >> c & 0xFF)) / 255) << c;
    return out;
}

static UINT32 blend_pixel_planar(UINT32 src, UINT32 dst, UINT planar)
{
    if (!(src >> 24))
        return dst | 0xFF000000;
    UINT w = (USHORT)(planar * (src >> 24));
    UINT32 out = 0xFF000000;
    for (int c = 0; c < 24; c += 8)
        out |= (((dst >> c & 0xFF) * (65025 - w) + w * (src >> c & 0xFF)) / 65025) << c;
    return out;
}

/*
 * dxva2's RGB copy with point sampling: rows and columns of the source picked by truncating the scale from the whole
 * destination rectangle, the part outside the target cut off. alpha selects how a pixel lands on what is there.
 */
static void copy_rgb(Processor *p, const BYTE *src, LONG src_pitch, UINT src_width, UINT src_height, BYTE *dst,
                     LONG dst_pitch, const RECT *full, const RECT *clip, int mode, UINT planar)
{
    double sx = (double)src_width / (double)(full->right - full->left);
    double sy = (double)src_height / (double)(full->bottom - full->top);
    UINT start_x = (UINT)(int)((double)(clip->left - full->left) * sx);
    UINT start_y = (UINT)(int)((double)(clip->top - full->top) * sy);
    const BYTE *base = src + (UINT)(src_pitch * (int)start_y) + 4ull * start_x;
    UINT width = clip->right - clip->left, height = clip->bottom - clip->top;
    if (p->index_from != src_width || p->index_to != (UINT)(full->right - full->left) || p->index_count != width) {
        HeapFree(GetProcessHeap(), 0, p->index);
        p->index = HeapAlloc(GetProcessHeap(), 0, 4ull * (width ? width : 1));
        p->index_from = src_width;
        p->index_to = full->right - full->left;
        p->index_count = width;
        for (UINT i = 0; p->index && i < width; i++)
            p->index[i] = (UINT)(int)((double)i * ((double)(int)src_width / (double)(full->right - full->left)));
    }
    for (UINT y = 0; y < height; y++) {
        const UINT32 *in = (const UINT32 *)(base + (UINT)(src_pitch * (int)((double)y * sy)));
        UINT32 *out = (UINT32 *)(dst + (LONG_PTR)(clip->top + (int)y) * dst_pitch) + clip->left;
        for (UINT x = 0; x < width; x++) {
            UINT32 s = in[p->index ? p->index[x] : (UINT)(int)((double)x * sx)];
            switch (mode) {
            case 0:
                out[x] = s | 0xFF000000;
                break;
            case 1:
                out[x] = blend_pixel(s, out[x]);
                break;
            case 2:
                out[x] = blend_planar(s, out[x], planar);
                break;
            default:
                out[x] = blend_pixel_planar(s, out[x], planar);
                break;
            }
        }
    }
}

typedef struct {
    BOOL active, translucent;
    D3DFORMAT format;
    RECT src, dst;
    UINT height;
} StreamState;

static BOOL rect_empty(const RECT *r)
{
    return r->right <= r->left || r->bottom <= r->top;
}

static BOOL intersect(RECT *out, const RECT *a, const RECT *b)
{
    out->left = a->left > b->left ? a->left : b->left;
    out->top = a->top > b->top ? a->top : b->top;
    out->right = a->right < b->right ? a->right : b->right;
    out->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (rect_empty(out)) {
        memset(out, 0, sizeof(*out));
        return FALSE;
    }
    return TRUE;
}

static void fill(BYTE *bits, LONG pitch, const RECT *r, UINT32 color)
{
    if (rect_empty(r))
        return;
    for (LONG y = r->top; y < r->bottom; y++) {
        UINT32 *row = (UINT32 *)(bits + (LONG_PTR)y * pitch);
        for (LONG x = r->left; x < r->right; x++)
            row[x] = color;
    }
}

static float fixed_value(DXVA2_Fixed32 v)
{
    return (float)v.Value + (float)v.Fraction * 0.000015258789f;
}

/* evr's software blit: the background, then each stream converted to rgb and placed, in the order given. */
static HRESULT software_blit(Processor *p, IDirect3DSurface9 *target, const DXVA2_VideoProcessBltParams *params,
                             const DXVA2_VideoSample *samples, UINT count)
{
    if (!count || count > 32)
        return D3DERR_INVALIDCALL;
    D3DSURFACE_DESC td;
    HRESULT hr = target->lpVtbl->GetDesc(target, &td);
    if (FAILED(hr))
        return hr;
    StreamState st[32];
    memset(st, 0, sizeof(st));
    for (UINT i = 0; i < count; i++) {
        D3DSURFACE_DESC sd;
        hr = samples[i].SrcSurface->lpVtbl->GetDesc(samples[i].SrcSurface, &sd);
        if (FAILED(hr))
            return hr;
        UINT sample = samples[i].SampleFormat.value & 0xFF;
        st[i].format = sd.Format;
        st[i].height = sd.Height;
        st[i].src = samples[i].SrcRect;
        BOOL ok = i ? sd.Format == D3DFMT_X8R8G8B8 || sd.Format == D3DFMT_A8R8G8B8 || sd.Format == D3DFMT_AYUV_ ||
                          sd.Format == D3DFMT_YUY2_
                    : input_supported(sd.Format, sample);
        if (!ok)
            return D3DERR_INVALIDCALL;
        if (sd.Format == D3DFMT_YUY2_) {
            st[i].src.left &= ~1;
            st[i].src.right &= ~1;
        }
        if (rect_empty(&st[i].src) || (!progressive(sample) && st[i].src.bottom - st[i].src.top < 2))
            continue;
        if (sample - 5 <= 1) {
            st[i].src.top /= 2;
            st[i].src.bottom /= 2;
        }
        if (!intersect(&st[i].dst, &params->TargetRect, &samples[i].DstRect))
            continue;
        float alpha = fixed_value(samples[i].PlanarAlpha);
        if (alpha > 0.0f) {
            st[i].translucent = alpha < 1.0f;
            st[i].active = TRUE;
        }
    }
    if (rect_empty(&params->TargetRect))
        return S_OK;

    /* the conversion buffer of yuv streams, as large as the largest source rectangle. */
    UINT largest = 0;
    for (UINT i = 0; i < count; i++)
        if (st[i].active && (st[i].format == D3DFMT_YUY2_ || st[i].format == D3DFMT_AYUV_)) {
            UINT area = (st[i].src.right - st[i].src.left) * (st[i].src.bottom - st[i].src.top);
            if (area > largest)
                largest = area;
        }
    if (largest > p->convert_size) {
        HeapFree(GetProcessHeap(), 0, p->convert);
        p->convert = HeapAlloc(GetProcessHeap(), 0, 4ull * largest);
        p->convert_size = p->convert ? largest : 0;
        if (!p->convert)
            return E_OUTOFMEMORY;
    }

    D3DLOCKED_RECT lr;
    IDirect3DSurface9 *shadow = nullptr, *locked = target;
    hr = target->lpVtbl->LockRect(target, &lr, nullptr, D3DLOCK_NOSYSLOCK);
    if (FAILED(hr)) {
        IDirect3DDevice9 *device = p->service->device;
        hr = device->lpVtbl->CreateOffscreenPlainSurface(device, td.Width, td.Height, td.Format, D3DPOOL_SYSTEMMEM,
                                                        &shadow, nullptr);
        if (SUCCEEDED(hr))
            hr = shadow->lpVtbl->LockRect(shadow, &lr, nullptr, D3DLOCK_NOSYSLOCK);
        if (FAILED(hr)) {
            if (shadow)
                shadow->lpVtbl->Release(shadow);
            return hr;
        }
        locked = shadow;
    }
    BYTE *bits = lr.pBits;

    /* the background around the largest opaque rgb32 or yuy2 picture. */
    RECT cover = {0};
    for (UINT i = 0; i < count; i++)
        if (st[i].active && st[i].format != D3DFMT_A8R8G8B8 && st[i].format != D3DFMT_AYUV_ && !st[i].translucent &&
            (cover.bottom - cover.top) * (cover.right - cover.left) <
                (UINT)((st[i].dst.right - st[i].dst.left) * (st[i].dst.bottom - st[i].dst.top)))
            cover = st[i].dst;
    const RECT *t = &params->TargetRect;
    if (memcmp(&cover, t, sizeof(cover))) {
        Conversion c = conversion(samples[0].SampleFormat, params->DestFormat, st[0].height);
        UINT32 color = to_rgb(&c, 0xFF, params->BackgroundColor.Y >> 8, params->BackgroundColor.Cb >> 8,
                              params->BackgroundColor.Cr >> 8);
        if (rect_empty(&cover)) {
            fill(bits, lr.Pitch, t, color);
        } else {
            RECT r[4] = {{t->left, t->top, t->right, cover.top},
                         {t->left, cover.top, cover.left, cover.bottom},
                         {cover.right, cover.top, t->right, cover.bottom},
                         {t->left, cover.bottom, t->right, t->bottom}};
            for (int k = 0; k < 4; k++)
                fill(bits, lr.Pitch, &r[k], color);
        }
    }

    for (UINT i = 0; i < count; i++) {
        if (!st[i].active)
            continue;
        IDirect3DSurface9 *src = samples[i].SrcSurface;
        D3DLOCKED_RECT sl;
        if (FAILED(src->lpVtbl->LockRect(src, &sl, &st[i].src, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK)))
            continue;
        UINT w = st[i].src.right - st[i].src.left, h = st[i].src.bottom - st[i].src.top;
        const BYTE *rgb = sl.pBits;
        LONG rgb_pitch = sl.Pitch;
        if (st[i].format == D3DFMT_YUY2_ || st[i].format == D3DFMT_AYUV_) {
            Conversion c = conversion(samples[i].SampleFormat, params->DestFormat, st[i].height);
            for (UINT y = 0; y < h; y++) {
                const BYTE *in = (const BYTE *)sl.pBits + (LONG_PTR)y * sl.Pitch;
                UINT32 *out = p->convert + y * w;
                if (st[i].format == D3DFMT_YUY2_) {
                    for (UINT x = 0; x + 1 < w; x += 2, in += 4) {
                        out[x] = to_rgb(&c, 0xFF, in[0], in[1], in[3]);
                        out[x + 1] = to_rgb(&c, 0xFF, in[2], in[1], in[3]);
                    }
                } else {
                    for (UINT x = 0; x < w; x++, in += 4)
                        out[x] = to_rgb(&c, in[3], in[2], in[1], in[0]);
                }
            }
            rgb = (const BYTE *)p->convert;
            rgb_pitch = 4 * w;
        }
        float a = fixed_value(samples[i].PlanarAlpha);
        if (a > 1.0f)
            a = 1.0f;
        if (a <= 0.0f)
            a = 0.0f;
        UINT planar = (UINT)(int)(a * 255.0f);
        BOOL with_alpha = st[i].format == D3DFMT_A8R8G8B8 || st[i].format == D3DFMT_AYUV_;
        int mode = with_alpha ? (st[i].translucent ? 3 : 1) : (st[i].translucent ? 2 : 0);
        copy_rgb(p, rgb, rgb_pitch, w, h, bits, lr.Pitch, &samples[i].DstRect, &st[i].dst, mode, planar);
        src->lpVtbl->UnlockRect(src);
    }
    hr = locked->lpVtbl->UnlockRect(locked);
    if (shadow) {
        if (SUCCEEDED(hr))
            hr = p->service->device->lpVtbl->UpdateSurface(p->service->device, shadow, nullptr, target, nullptr);
        shadow->lpVtbl->Release(shadow);
    }
    return hr;
}

/* ------------------------------------------------------------------ IDirectXVideoProcessor */

#define PROCESSOR_OF(p) CONTAINING_RECORD(p, Processor, iface)

static HRESULT STDMETHODCALLTYPE device_QueryInterface(IDirectXVideoProcessor *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirectXVideoProcessor)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    *out = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE device_AddRef(IDirectXVideoProcessor *This)
{
    return InterlockedIncrement(&PROCESSOR_OF(This)->refs);
}

static ULONG STDMETHODCALLTYPE device_Release(IDirectXVideoProcessor *This)
{
    Processor *p = PROCESSOR_OF(This);
    ULONG refs = InterlockedDecrement(&p->refs);
    if (!refs) {
        inner_Release(&p->service->inner);
        HeapFree(GetProcessHeap(), 0, p->index);
        HeapFree(GetProcessHeap(), 0, p->convert);
        HeapFree(GetProcessHeap(), 0, p);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE device_GetVideoProcessorService(IDirectXVideoProcessor *This,
                                                                 IDirectXVideoProcessorService **service)
{
    Processor *p = PROCESSOR_OF(This);
    if (!service)
        return E_POINTER;
    *service = &p->service->processor;
    InterlockedIncrement(&p->service->refs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_GetCreationParameters(IDirectXVideoProcessor *This, GUID *guid,
                                                              DXVA2_VideoDesc *desc, D3DFORMAT *target,
                                                              UINT *substreams)
{
    Processor *p = PROCESSOR_OF(This);
    if (!guid && !desc && !target && !substreams)
        return E_INVALIDARG;
    if (guid)
        *guid = p->guid;
    if (desc)
        *desc = p->desc;
    if (target)
        *target = p->target;
    if (substreams)
        *substreams = p->substreams;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_GetVideoProcessorCaps(IDirectXVideoProcessor *This,
                                                              DXVA2_VideoProcessorCaps *caps)
{
    if (!caps)
        return E_POINTER;
    *caps = PROCESSOR_OF(This)->caps;
    return S_OK;
}

/* only the properties the caps name have a range. */
static HRESULT STDMETHODCALLTYPE device_GetProcAmpRange(IDirectXVideoProcessor *This, UINT property,
                                                        DXVA2_ValueRange *range)
{
    if (!range)
        return E_POINTER;
    if (!(PROCESSOR_OF(This)->caps.ProcAmpControlCaps & property))
        return E_INVALIDARG;
    memset(range, 0, sizeof(*range));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_GetFilterPropertyRange(IDirectXVideoProcessor *This, UINT setting,
                                                               DXVA2_ValueRange *range)
{
    if (!range)
        return E_POINTER;
    return E_INVALIDARG;
}

/* dxva2's ValidateProcessBltParams for a device without procamp and filters. */
static HRESULT validate_blit(Processor *p, IDirect3DSurface9 *target, const DXVA2_VideoProcessBltParams *params,
                             const DXVA2_VideoSample *samples, UINT count)
{
    D3DSURFACE_DESC td;
    HRESULT hr = target->lpVtbl->GetDesc(target, &td);
    if (FAILED(hr))
        return hr;
    if (count > 32)
        return E_INVALIDARG;
    if (!(td.Usage & D3DUSAGE_RENDERTARGET)) {
        DWORD kind = 0, size = 4;
        if (FAILED(target->lpVtbl->GetPrivateData(target, &DXVA2_SurfaceDxvaType_, &kind, &size)) ||
            (kind != 3 && !(kind == 0 && is_software(&p->guid))))
            return E_INVALIDARG;
    }
    const RECT *t = &params->TargetRect;
    if (t->left < 0 || t->top < 0 || t->left > t->right || t->top > t->bottom || t->right > (LONG)td.Width ||
        t->bottom > (LONG)td.Height)
        return E_INVALIDARG;
    DWORD ops = p->caps.VideoProcessorOperations;
    if ((ops & DXVA2_VideoProcess_Constriction) &&
        (params->ConstrictionSize.cx < 0 || params->ConstrictionSize.cy < 0 ||
         params->ConstrictionSize.cx > t->right - t->left || params->ConstrictionSize.cy > t->bottom - t->top))
        return E_INVALIDARG;
    if (params->BackgroundColor.Alpha != 0xFFFF)
        return E_INVALIDARG;
    float alpha = fixed_value(params->Alpha);
    if ((ops & 0x108) && (alpha < 0.0f || alpha > 1.0f))
        return E_INVALIDARG;
    for (UINT i = 0; i < count; i++) {
        const DXVA2_VideoSample *s = &samples[i];
        if (s->Start > s->End || !s->SrcSurface)
            return E_INVALIDARG;
        D3DSURFACE_DESC sd;
        hr = s->SrcSurface->lpVtbl->GetDesc(s->SrcSurface, &sd);
        if (FAILED(hr))
            return hr;
        LONG height = (LONG)sd.Height;
        if ((s->SampleFormat.value & 0xFF) - 5 <= 1)
            height *= 2;
        const RECT *r = &s->SrcRect, *d = &s->DstRect;
        if (r->left < 0 || r->top < 0 || r->left > r->right || r->top > r->bottom || r->right > (LONG)sd.Width ||
            r->bottom > height)
            return E_INVALIDARG;
        if (d->left < 0 || d->top < 0 || d->left > d->right || d->top > d->bottom || d->right > (LONG)td.Width ||
            d->bottom > (LONG)td.Height)
            return E_INVALIDARG;
        if (ops & DXVA2_VideoProcess_PlanarAlpha) {
            float a = fixed_value(s->PlanarAlpha);
            if (a < 0.0f || a > 1.0f)
                return E_INVALIDARG;
        }
    }
    UINT back = p->caps.NumBackwardRefSamples;
    if (back >= count)
        return S_OK;
    if (params->TargetFrame == samples[back].Start ||
        (params->TargetFrame >= samples[back].Start && params->TargetFrame < samples[back].End))
        return S_OK;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE device_VideoProcessBlt(IDirectXVideoProcessor *This, IDirect3DSurface9 *target,
                                                        const DXVA2_VideoProcessBltParams *params,
                                                        const DXVA2_VideoSample *samples, UINT count,
                                                        HANDLE *complete)
{
    Processor *p = PROCESSOR_OF(This);
    if (!target || !params || (count && !samples))
        return E_INVALIDARG;
    EnterCriticalSection(&p->service->lock);
    HRESULT hr = validate_blit(p, target, params, samples, count);
    if (SUCCEEDED(hr))
        hr = software_blit(p, target, params, samples, count);
    LeaveCriticalSection(&p->service->lock);
    return hr;
}

static const IDirectXVideoProcessorVtbl device_vtbl = {
    device_QueryInterface,          device_AddRef,
    device_Release,                 device_GetVideoProcessorService,
    device_GetCreationParameters,   device_GetVideoProcessorCaps,
    device_GetProcAmpRange,         device_GetFilterPropertyRange,
    device_VideoProcessBlt,
};

/* ------------------------------------------------------------------ creation */

DECLSPEC_EXPORT HRESULT WINAPI DXVA2CreateVideoService(IDirect3DDevice9 *device, REFIID riid, void **service)
{
    if (!device || !service)
        return E_POINTER;
    *service = nullptr;
    Service *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->inner.lpVtbl = &inner_vtbl;
    s->decoder.lpVtbl = &decoder_vtbl;
    s->processor.lpVtbl = &processor_vtbl;
    s->refs = 1;
    InitializeCriticalSection(&s->lock);
    HRESULT hr = device->lpVtbl->QueryInterface(device, &IID_IDirect3DDevice9, (void **)&s->device);
    if (SUCCEEDED(hr)) {
        D3DDEVICE_CREATION_PARAMETERS params;
        hr = s->device->lpVtbl->GetCreationParameters(s->device, &params);
    }
    if (FAILED(hr)) {
        if (s->device)
            s->device->lpVtbl->Release(s->device);
        DeleteCriticalSection(&s->lock);
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    hr = service_query(s, riid, service);
    inner_Release(&s->inner);
    return hr;
}
