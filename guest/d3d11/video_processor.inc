/*
 * direct3d 11 video, the video processor (included by video.c): an enumerator that tells what a processor takes and gives, processors
 * with the state of their streams and output, the views of their textures, and VideoProcessorBlt, which draws
 * the streams into an output texture with one compute shader (tools/shaders/video.asm): the source resampled
 * with a bilinear filter to the destination rectangle, converted from the color space of the stream to the one
 * of the output. a processor takes planar 4:2:0 video and rgb textures and gives rgb textures. rotation and
 * mirroring are part of the resampling; filters, stereo, luma keys and palettes are not offered. the output is
 * made in a buffer, as raw words in the output's format, and copied into the texture, so nothing is asked of the
 * texture's binds but being a render target, as in direct3d.
 */
#define MAX_STREAMS 1

/* ------------------------------------------------------------------ formats and color spaces */

typedef enum VpFormat {
    VP_FORMAT_NONE = -1,
    VP_BGRA8 = 0,
    VP_RGBA8 = 1,
    VP_RGB10A2 = 2,
    VP_RGBA16F = 3,
} VpFormat;

/* the formats a processor writes, and how many bytes a texel of each takes. */
static VpFormat output_format(DXGI_FORMAT format, UINT *bytes)
{
    *bytes = 4;
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return VP_BGRA8;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return VP_RGBA8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return VP_RGB10A2;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        *bytes = 8;
        return VP_RGBA16F;
    default:
        return VP_FORMAT_NONE;
    }
}

/* the formats a processor reads: video planes, and the rgb formats it writes. */
static BOOL input_format(DXGI_FORMAT format)
{
    UINT bytes;
    return format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010 || format == DXGI_FORMAT_P016 ||
           output_format(format, &bytes) != VP_FORMAT_NONE;
}

typedef enum VpMatrix { VP_BT601, VP_BT709, VP_BT2020 } VpMatrix;
typedef enum VpTransfer { VP_G22, VP_G10, VP_G24, VP_PQ, VP_HLG } VpTransfer;

typedef struct VpSpace {
    BOOL rgb;
    BOOL full;
    VpMatrix matrix;
    VpTransfer transfer;
    /* chroma sited at the left, or at the top left too, of its luma samples. */
    BOOL left, top;
} VpSpace;

/* what a dxgi color space says: rgb or ycbcr, the range, the matrix, the transfer function and the chroma siting. */
static BOOL space_of(DXGI_COLOR_SPACE_TYPE type, VpSpace *s)
{
    *s = (VpSpace){};
    switch ((int)type) {
    case 0: *s = (VpSpace){TRUE, TRUE, VP_BT709, VP_G22}; break;
    case 1: *s = (VpSpace){TRUE, TRUE, VP_BT709, VP_G10}; break;
    case 2: *s = (VpSpace){TRUE, FALSE, VP_BT709, VP_G22}; break;
    case 3: *s = (VpSpace){TRUE, FALSE, VP_BT2020, VP_G22}; break;
    case 5: *s = (VpSpace){FALSE, TRUE, VP_BT601, VP_G22}; break;
    case 6: *s = (VpSpace){FALSE, FALSE, VP_BT601, VP_G22, TRUE}; break;
    case 7: *s = (VpSpace){FALSE, TRUE, VP_BT601, VP_G22, TRUE}; break;
    case 8: *s = (VpSpace){FALSE, FALSE, VP_BT709, VP_G22, TRUE}; break;
    case 9: *s = (VpSpace){FALSE, TRUE, VP_BT709, VP_G22, TRUE}; break;
    case 10: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_G22, TRUE}; break;
    case 11: *s = (VpSpace){FALSE, TRUE, VP_BT2020, VP_G22, TRUE}; break;
    case 12: *s = (VpSpace){TRUE, TRUE, VP_BT2020, VP_PQ}; break;
    case 13: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_PQ, TRUE}; break;
    case 14: *s = (VpSpace){TRUE, FALSE, VP_BT2020, VP_PQ}; break;
    case 15: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_G22, TRUE, TRUE}; break;
    case 16: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_PQ, TRUE, TRUE}; break;
    case 17: *s = (VpSpace){TRUE, TRUE, VP_BT2020, VP_G22}; break;
    case 18: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_HLG, TRUE, TRUE}; break;
    case 19: *s = (VpSpace){FALSE, TRUE, VP_BT2020, VP_HLG, TRUE, TRUE}; break;
    case 20: *s = (VpSpace){TRUE, FALSE, VP_BT709, VP_G24}; break;
    case 21: *s = (VpSpace){TRUE, FALSE, VP_BT2020, VP_G24}; break;
    case 22: *s = (VpSpace){FALSE, FALSE, VP_BT709, VP_G24, TRUE}; break;
    case 23: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_G24, TRUE}; break;
    case 24: *s = (VpSpace){FALSE, FALSE, VP_BT2020, VP_G24, TRUE, TRUE}; break;
    default: return FALSE;
    }
    return TRUE;
}

/* the legacy color space structure, as the space it names for a yuv stream or an rgb output. */
static VpSpace legacy_space(const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *cs, BOOL rgb)
{
    VpSpace s = {rgb, TRUE, cs->YCbCr_Matrix ? VP_BT709 : VP_BT601, VP_G22, TRUE};
    if (rgb)
        s.full = cs->RGB_Range == 0;
    else
        s.full = cs->Nominal_Range == 1;
    return s;
}

/* ------------------------------------------------------------------ objects */

typedef struct VpEnum {
    ID3D11VideoProcessorEnumerator1 iface;
    D3dChild child;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc;
} VpEnum;

typedef struct VpView {
    union {
        ID3D11VideoProcessorInputView input;
        ID3D11VideoProcessorOutputView output;
    } iface;
    D3dChild child;
    BOOL is_output;
    D3dResource *resource;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc;
    /* an input view's planes as shaders read them, made when first drawn from. */
    ID3D11ShaderResourceView *planes[2];
} VpView;

typedef struct VpStream {
    D3D11_VIDEO_FRAME_FORMAT frame_format;
    BOOL has_legacy_space;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE legacy_space;
    DXGI_COLOR_SPACE_TYPE space;
    BOOL has_space;
    D3D11_VIDEO_PROCESSOR_OUTPUT_RATE rate;
    BOOL repeat;
    DXGI_RATIONAL custom_rate;
    BOOL source_enabled, dest_enabled;
    RECT source, dest;
    BOOL alpha_enabled;
    FLOAT alpha;
    BOOL luma_key_enabled;
    FLOAT luma_lower, luma_upper;
    BOOL aspect_enabled;
    DXGI_RATIONAL aspect_source, aspect_dest;
    BOOL auto_processing;
    BOOL rotation_enabled;
    D3D11_VIDEO_PROCESSOR_ROTATION rotation;
    BOOL mirror, flip_horizontal, flip_vertical;
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT stereo_format;
    BOOL stereo_enabled, stereo_swap, stereo_flip_enabled, stereo_mono_offset;
    D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE stereo_flip;
    INT stereo_offset;
    BOOL stereo_base_view;
    BOOL filters_enabled[8];
    INT filters[8];
} VpStream;

typedef struct VideoProcessor {
    ID3D11VideoProcessor iface;
    D3dChild child;
    VpEnum *enumerator;
    UINT rate_index;
    BOOL target_enabled;
    RECT target;
    BOOL background_ycbcr;
    D3D11_VIDEO_COLOR background;
    BOOL has_legacy_output_space;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE legacy_output_space;
    DXGI_COLOR_SPACE_TYPE output_space;
    BOOL has_output_space;
    D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE alpha_fill;
    UINT alpha_fill_stream;
    BOOL constriction_enabled;
    SIZE constriction;
    BOOL stereo_output;
    BOOL shader_usage;
    VpStream streams[MAX_STREAMS];
    /* the buffer the shader writes, grown as the target does. */
    ID3D11Buffer *buffer;
    ID3D11UnorderedAccessView *buffer_view;
    UINT buffer_size;
    ID3D11Buffer *constants;
} VideoProcessor;

static VpEnum *enum_impl(void *iface) { return CONTAINING_RECORD(iface, VpEnum, iface); }
static VideoProcessor *proc_impl(ID3D11VideoProcessor *iface) { return CONTAINING_RECORD(iface, VideoProcessor, iface); }
static VpView *vpview_impl(void *iface) { return CONTAINING_RECORD(iface, VpView, iface); }

/* ------------------------------------------------------------------ the enumerator */

static HRESULT STDMETHODCALLTYPE enum_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D11DeviceChild) &&
        !IsEqualGUID(riid, &IID_ID3D11VideoProcessorEnumerator) &&
        !IsEqualGUID(riid, &IID_ID3D11VideoProcessorEnumerator1))
        return E_NOINTERFACE;
    InterlockedIncrement(&enum_impl(iface)->child.refs);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(void *iface) { return InterlockedIncrement(&enum_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE enum_release(void *iface)
{
    VpEnum *e = enum_impl(iface);
    ULONG refs = InterlockedDecrement(&e->child.refs);
    if (refs)
        return refs;
    D3dChildRelease(&e->child);
    HeapFree(GetProcessHeap(), 0, e);
    return 0;
}

static void STDMETHODCALLTYPE enum_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = enum_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE enum_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&enum_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE enum_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&enum_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE enum_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&enum_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE enum_get_content_desc(void *iface, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    *desc = enum_impl(iface)->desc;
    return S_OK;
}

/* the flags of a format: bit 0 read as a stream, bit 1 written as an output. */
static UINT format_flags(D3dDevice *device, DXGI_FORMAT format)
{
    UINT support = 0, bytes;
    if (FAILED(device->iface.lpVtbl->CheckFormatSupport(&device->iface, format, &support)))
        return 0;
    return (input_format(format) ? 1 : 0) | (output_format(format, &bytes) != VP_FORMAT_NONE ? 2 : 0);
}

static HRESULT STDMETHODCALLTYPE enum_check_format(void *iface, DXGI_FORMAT format, UINT *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = format_flags(enum_impl(iface)->child.device, format);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_get_caps(void *iface, D3D11_VIDEO_PROCESSOR_CAPS *caps)
{
    if (!caps)
        return E_INVALIDARG;
    memset(caps, 0, sizeof(*caps));
    caps->DeviceCaps = D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_RGB_RANGE_CONVERSION |
                       D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_NOMINAL_RANGE;
    caps->FeatureCaps = D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ROTATION | D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_MIRROR;
    caps->RateConversionCapsCount = 1;
    caps->MaxInputStreams = MAX_STREAMS;
    caps->MaxStreamStates = MAX_STREAMS;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_get_rate_conversion_caps(void *iface, UINT index,
                                                               D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    if (!caps || index >= 1)
        return E_INVALIDARG;
    memset(caps, 0, sizeof(*caps));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_get_custom_rate(void *iface, UINT type, UINT index,
                                                      D3D11_VIDEO_PROCESSOR_CUSTOM_RATE *rate)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE enum_get_filter_range(void *iface, D3D11_VIDEO_PROCESSOR_FILTER filter,
                                                       D3D11_VIDEO_PROCESSOR_FILTER_RANGE *range)
{
    if (!range)
        return E_INVALIDARG;
    memset(range, 0, sizeof(*range));
    /* no filter is offered: the caps carry none of the bits a filter needs. */
    return E_INVALIDARG;
}

/* whether the processor converts a format and color space to another: planes and rgb to rgb, in the spaces it knows. */
static HRESULT STDMETHODCALLTYPE enum_check_conversion(void *iface, DXGI_FORMAT in_format,
                                                       DXGI_COLOR_SPACE_TYPE in_space, DXGI_FORMAT out_format,
                                                       DXGI_COLOR_SPACE_TYPE out_space, BOOL *supported)
{
    if (!supported)
        return E_INVALIDARG;
    *supported = FALSE;
    D3dDevice *device = enum_impl(iface)->child.device;
    VpSpace in, out;
    if (!(format_flags(device, in_format) & 1) || !(format_flags(device, out_format) & 2) ||
        !space_of(in_space, &in) || !space_of(out_space, &out))
        return S_OK;
    /* a color space of rgb for rgb formats and of ycbcr for the planes, and no tone mapping of high dynamic range. */
    BOOL planar = in_format == DXGI_FORMAT_NV12 || in_format == DXGI_FORMAT_P010 || in_format == DXGI_FORMAT_P016;
    if (in.rgb == planar || !out.rgb || in.transfer == VP_PQ || in.transfer == VP_HLG || out.transfer == VP_PQ ||
        out.transfer == VP_HLG)
        return S_OK;
    *supported = TRUE;
    return S_OK;
}

static const ID3D11VideoProcessorEnumerator1Vtbl enum_vtbl = {
    VTBL_FN(enum_query),
    VTBL_FN(enum_add_ref),
    VTBL_FN(enum_release),
    VTBL_FN(enum_get_device),
    VTBL_FN(enum_get_private_data),
    VTBL_FN(enum_set_private_data),
    VTBL_FN(enum_set_private_data_interface),
    VTBL_FN(enum_get_content_desc),
    VTBL_FN(enum_check_format),
    VTBL_FN(enum_get_caps),
    VTBL_FN(enum_get_rate_conversion_caps),
    VTBL_FN(enum_get_custom_rate),
    VTBL_FN(enum_get_filter_range),
    VTBL_FN(enum_check_conversion),
};

/* ------------------------------------------------------------------ the processor */

static HRESULT STDMETHODCALLTYPE proc_query(ID3D11VideoProcessor *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D11DeviceChild) &&
        !IsEqualGUID(riid, &IID_ID3D11VideoProcessor))
        return E_NOINTERFACE;
    InterlockedIncrement(&proc_impl(iface)->child.refs);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE proc_add_ref(ID3D11VideoProcessor *iface)
{
    return InterlockedIncrement(&proc_impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE proc_release(ID3D11VideoProcessor *iface)
{
    VideoProcessor *p = proc_impl(iface);
    ULONG refs = InterlockedDecrement(&p->child.refs);
    if (refs)
        return refs;
    if (p->buffer_view)
        p->buffer_view->lpVtbl->Release(p->buffer_view);
    if (p->buffer)
        p->buffer->lpVtbl->Release(p->buffer);
    if (p->constants)
        p->constants->lpVtbl->Release(p->constants);
    p->enumerator->iface.lpVtbl->Release(&p->enumerator->iface);
    D3dChildRelease(&p->child);
    HeapFree(GetProcessHeap(), 0, p);
    return 0;
}

static void STDMETHODCALLTYPE proc_get_device(ID3D11VideoProcessor *iface, ID3D11Device **device)
{
    D3dDevice *d = proc_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE proc_get_private_data(ID3D11VideoProcessor *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&proc_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE proc_set_private_data(ID3D11VideoProcessor *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&proc_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE proc_set_private_data_interface(ID3D11VideoProcessor *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&proc_impl(iface)->child.data, guid, data);
}

static void STDMETHODCALLTYPE proc_get_content_desc(ID3D11VideoProcessor *iface, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    *desc = proc_impl(iface)->enumerator->desc;
}

static void STDMETHODCALLTYPE proc_get_rate_conversion_caps(ID3D11VideoProcessor *iface,
                                                            D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    memset(caps, 0, sizeof(*caps));
}

static const ID3D11VideoProcessorVtbl proc_vtbl = {
    proc_query,
    proc_add_ref,
    proc_release,
    proc_get_device,
    proc_get_private_data,
    proc_set_private_data,
    proc_set_private_data_interface,
    proc_get_content_desc,
    proc_get_rate_conversion_caps,
};

static void init_processor(VideoProcessor *p)
{
    p->background.RGBA = (D3D11_VIDEO_COLOR_RGBA){0, 0, 0, 1.0f};
    p->alpha_fill = D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE;
    for (UINT i = 0; i < MAX_STREAMS; i++) {
        VpStream *s = &p->streams[i];
        s->frame_format = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        s->rate = D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL;
        s->alpha = 1.0f;
        s->auto_processing = TRUE;
    }
}

/* ------------------------------------------------------------------ views */

static HRESULT STDMETHODCALLTYPE vpview_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    VpView *v = vpview_impl(iface);
    *out = nullptr;
    BOOL match = IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
                 IsEqualGUID(riid, &IID_ID3D11View) ||
                 IsEqualGUID(riid, v->is_output ? &IID_ID3D11VideoProcessorOutputView
                                                : &IID_ID3D11VideoProcessorInputView);
    if (!match)
        return E_NOINTERFACE;
    InterlockedIncrement(&v->child.refs);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE vpview_add_ref(void *iface) { return InterlockedIncrement(&vpview_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE vpview_release(void *iface)
{
    VpView *v = vpview_impl(iface);
    ULONG refs = InterlockedDecrement(&v->child.refs);
    if (refs)
        return refs;
    for (UINT i = 0; i < 2; i++)
        if (v->planes[i])
            v->planes[i]->lpVtbl->Release(v->planes[i]);
    D3dResourceRelease(v->resource);
    D3dChildRelease(&v->child);
    HeapFree(GetProcessHeap(), 0, v);
    return 0;
}

static void STDMETHODCALLTYPE vpview_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = vpview_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE vpview_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&vpview_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE vpview_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&vpview_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE vpview_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&vpview_impl(iface)->child.data, guid, data);
}

static void STDMETHODCALLTYPE vpview_get_resource(void *iface, ID3D11Resource **resource)
{
    D3dResource *r = vpview_impl(iface)->resource;
    D3dResourceAddRef(r);
    *resource = r->iface;
}

static void STDMETHODCALLTYPE vpview_get_input_desc(void *iface, D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *desc)
{
    *desc = vpview_impl(iface)->input_desc;
}

static void STDMETHODCALLTYPE vpview_get_output_desc(void *iface, D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *desc)
{
    *desc = vpview_impl(iface)->output_desc;
}

static const ID3D11VideoProcessorInputViewVtbl input_view_vtbl = {
    VTBL_FN(vpview_query),
    VTBL_FN(vpview_add_ref),
    VTBL_FN(vpview_release),
    VTBL_FN(vpview_get_device),
    VTBL_FN(vpview_get_private_data),
    VTBL_FN(vpview_set_private_data),
    VTBL_FN(vpview_set_private_data_interface),
    VTBL_FN(vpview_get_resource),
    VTBL_FN(vpview_get_input_desc),
};

static const ID3D11VideoProcessorOutputViewVtbl output_view_vtbl = {
    VTBL_FN(vpview_query),
    VTBL_FN(vpview_add_ref),
    VTBL_FN(vpview_release),
    VTBL_FN(vpview_get_device),
    VTBL_FN(vpview_get_private_data),
    VTBL_FN(vpview_set_private_data),
    VTBL_FN(vpview_set_private_data_interface),
    VTBL_FN(vpview_get_resource),
    VTBL_FN(vpview_get_output_desc),
};

/* ------------------------------------------------------------------ the device's methods */

static VpEnum *enum_of(ID3D11VideoProcessorEnumerator *iface) { return iface ? enum_impl(iface) : nullptr; }

static HRESULT vp_create_enumerator(D3dDevice *device, const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc,
                                    ID3D11VideoProcessorEnumerator **out)
{
    if (!out || !desc)
        return E_INVALIDARG;
    *out = nullptr;
    if (desc->InputFrameFormat > D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST ||
        desc->Usage > D3D11_VIDEO_USAGE_OPTIMAL_QUALITY ||
        (desc->InputFrameRate.Numerator && !desc->InputFrameRate.Denominator) ||
        (desc->OutputFrameRate.Numerator && !desc->OutputFrameRate.Denominator) || !desc->InputWidth ||
        !desc->InputHeight || !desc->OutputWidth || !desc->OutputHeight)
        return E_INVALIDARG;
    VpEnum *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->iface.lpVtbl = &enum_vtbl;
    D3dChildInit(&e->child, device);
    e->desc = *desc;
    *out = (ID3D11VideoProcessorEnumerator *)&e->iface;
    return S_OK;
}

static HRESULT vp_create_processor(D3dDevice *device, ID3D11VideoProcessorEnumerator *enumerator, UINT rate_index,
                                   ID3D11VideoProcessor **out)
{
    if (!enumerator || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (rate_index >= 1)
        return E_INVALIDARG;
    VideoProcessor *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->iface.lpVtbl = &proc_vtbl;
    D3dChildInit(&p->child, device);
    p->enumerator = enum_of(enumerator);
    p->enumerator->iface.lpVtbl->AddRef(&p->enumerator->iface);
    p->rate_index = rate_index;
    init_processor(p);
    *out = &p->iface;
    return S_OK;
}

static HRESULT vp_create_view(D3dDevice *device, BOOL is_output, ID3D11Resource *resource,
                              ID3D11VideoProcessorEnumerator *enumerator, const void *desc, void **out)
{
    if (out)
        *out = nullptr;
    if (!resource || !desc || !enumerator)
        return E_INVALIDARG;
    D3dResource *r = D3dResourceFrom(resource);
    if (r->dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        return E_INVALIDARG;
    UINT flags = 0;
    enumerator->lpVtbl->CheckVideoProcessorFormat(enumerator, r->tex.format, &flags);
    if (r->tex.samples > 1)
        return E_INVALIDARG;
    if (is_output) {
        const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *d = desc;
        if (!(r->bind & D3D11_BIND_RENDER_TARGET) || !(flags & 2))
            return E_INVALIDARG;
        if (d->ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D) {
            if (d->Texture2D.MipSlice >= r->tex.mips)
                return E_INVALIDARG;
        } else if (d->ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2DARRAY) {
            UINT size = d->Texture2DArray.ArraySize == ~0u ? r->tex.layers : d->Texture2DArray.ArraySize;
            if (d->Texture2DArray.MipSlice >= r->tex.mips || d->Texture2DArray.FirstArraySlice >= r->tex.layers ||
                size != 1 || d->Texture2DArray.FirstArraySlice + size > r->tex.layers)
                return E_INVALIDARG;
        } else {
            return E_INVALIDARG;
        }
    } else {
        const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *d = desc;
        /* what a stream may be: a decoder's picture, or a texture a program renders to or writes. */
        if (!(r->bind & (D3D11_BIND_DECODER | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS | 0x400 | 0x800)) &&
            r->bind)
            return E_INVALIDARG;
        if (r->usage == D3D11_USAGE_STAGING || !(flags & 1))
            return E_INVALIDARG;
        if (d->ViewDimension != D3D11_VPIV_DIMENSION_TEXTURE2D || d->Texture2D.ArraySlice >= r->tex.layers ||
            d->Texture2D.MipSlice >= r->tex.mips)
            return E_INVALIDARG;
    }
    if (!out)
        return S_FALSE;
    VpView *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v)
        return E_OUTOFMEMORY;
    v->is_output = is_output;
    *(const void **)&v->iface = is_output ? (const void *)&output_view_vtbl : (const void *)&input_view_vtbl;
    D3dChildInit(&v->child, device);
    v->resource = r;
    D3dResourceAddRef(r);
    if (is_output)
        v->output_desc = *(const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *)desc;
    else
        v->input_desc = *(const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *)desc;
    *out = is_output ? (void *)&v->iface.output : (void *)&v->iface.input;
    return S_OK;
}

/* ------------------------------------------------------------------ the state of a processor */

static BOOL stream_ok(VideoProcessor *p, UINT stream) { return p && stream < MAX_STREAMS; }

#define PROCESSOR(iface) VideoProcessor *p = (iface) ? proc_impl(iface) : nullptr

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputTargetRect(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     BOOL enable, const RECT *rect)
{
    PROCESSOR(processor);
    if (!p || (enable && !rect))
        return;
    p->target_enabled = enable;
    if (enable)
        p->target = *rect;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputTargetRect(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     BOOL *enabled, RECT *rect)
{
    PROCESSOR(processor);
    if (!p || !enabled || !rect)
        return;
    *enabled = p->target_enabled;
    *rect = p->target;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputBackgroundColor(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                          BOOL ycbcr, const D3D11_VIDEO_COLOR *color)
{
    PROCESSOR(processor);
    if (!p || !color)
        return;
    p->background_ycbcr = ycbcr;
    p->background = *color;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputBackgroundColor(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                          BOOL *ycbcr, D3D11_VIDEO_COLOR *color)
{
    PROCESSOR(processor);
    if (!p || !ycbcr || !color)
        return;
    *ycbcr = p->background_ycbcr;
    *color = p->background;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputColorSpace(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *space)
{
    PROCESSOR(processor);
    if (!p || !space)
        return;
    p->legacy_output_space = *space;
    p->has_legacy_output_space = TRUE;
    p->has_output_space = FALSE;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputColorSpace(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     D3D11_VIDEO_PROCESSOR_COLOR_SPACE *space)
{
    PROCESSOR(processor);
    if (!p || !space)
        return;
    memset(space, 0, sizeof(*space));
    if (p->has_legacy_output_space)
        *space = p->legacy_output_space;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputColorSpace1(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      DXGI_COLOR_SPACE_TYPE space)
{
    PROCESSOR(processor);
    VpSpace s;
    if (!p || !space_of(space, &s))
        return;
    p->output_space = space;
    p->has_output_space = TRUE;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputColorSpace1(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      DXGI_COLOR_SPACE_TYPE *space)
{
    PROCESSOR(processor);
    if (p && space)
        *space = p->has_output_space ? p->output_space : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputAlphaFillMode(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                        D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE mode, UINT stream)
{
    PROCESSOR(processor);
    if (!p)
        return;
    p->alpha_fill = mode;
    p->alpha_fill_stream = stream;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputAlphaFillMode(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                        D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE *mode, UINT *stream)
{
    PROCESSOR(processor);
    if (!p || !mode || !stream)
        return;
    *mode = p->alpha_fill;
    *stream = p->alpha_fill_stream;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputConstriction(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                       BOOL enable, SIZE size)
{
    PROCESSOR(processor);
    if (!p)
        return;
    p->constriction_enabled = enable;
    p->constriction = size;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputConstriction(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                       BOOL *enabled, SIZE *size)
{
    PROCESSOR(processor);
    if (!p || !enabled || !size)
        return;
    *enabled = p->constriction_enabled;
    *size = p->constriction;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputStereoMode(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     BOOL enable)
{
    PROCESSOR(processor);
    if (p)
        p->stereo_output = enable;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputStereoMode(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     BOOL *enabled)
{
    PROCESSOR(processor);
    if (p && enabled)
        *enabled = p->stereo_output;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetOutputShaderUsage(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      BOOL shader_usage)
{
    PROCESSOR(processor);
    if (p)
        p->shader_usage = shader_usage;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetOutputShaderUsage(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      BOOL *shader_usage)
{
    PROCESSOR(processor);
    if (p && shader_usage)
        *shader_usage = p->shader_usage;
}

/* the state of a stream: setters and getters of each member, as direct3d names them. */
static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamFrameFormat(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      UINT stream, D3D11_VIDEO_FRAME_FORMAT format)
{
    PROCESSOR(processor);
    if (stream_ok(p, stream) && format <= D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST)
        p->streams[stream].frame_format = format;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamFrameFormat(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      UINT stream, D3D11_VIDEO_FRAME_FORMAT *format)
{
    PROCESSOR(processor);
    if (stream_ok(p, stream) && format)
        *format = p->streams[stream].frame_format;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamColorSpace(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     UINT stream, const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *space)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !space)
        return;
    p->streams[stream].legacy_space = *space;
    p->streams[stream].has_legacy_space = TRUE;
    p->streams[stream].has_space = FALSE;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamColorSpace(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     UINT stream, D3D11_VIDEO_PROCESSOR_COLOR_SPACE *space)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !space)
        return;
    memset(space, 0, sizeof(*space));
    if (p->streams[stream].has_legacy_space)
        *space = p->streams[stream].legacy_space;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamColorSpace1(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      UINT stream, DXGI_COLOR_SPACE_TYPE space)
{
    PROCESSOR(processor);
    VpSpace s;
    if (!stream_ok(p, stream) || !space_of(space, &s))
        return;
    p->streams[stream].space = space;
    p->streams[stream].has_space = TRUE;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamColorSpace1(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      UINT stream, DXGI_COLOR_SPACE_TYPE *space)
{
    PROCESSOR(processor);
    if (stream_ok(p, stream) && space)
        *space = p->streams[stream].has_space ? p->streams[stream].space : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamOutputRate(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     UINT stream, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE rate,
                                                     BOOL repeat, const DXGI_RATIONAL *custom)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream))
        return;
    p->streams[stream].rate = rate;
    p->streams[stream].repeat = repeat;
    if (custom)
        p->streams[stream].custom_rate = *custom;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamOutputRate(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     UINT stream, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE *rate,
                                                     BOOL *repeat, DXGI_RATIONAL *custom)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !rate || !repeat || !custom)
        return;
    *rate = p->streams[stream].rate;
    *repeat = p->streams[stream].repeat;
    *custom = p->streams[stream].custom_rate;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamSourceRect(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     UINT stream, BOOL enable, const RECT *rect)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || (enable && !rect))
        return;
    p->streams[stream].source_enabled = enable;
    if (enable)
        p->streams[stream].source = *rect;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamSourceRect(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                     UINT stream, BOOL *enabled, RECT *rect)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !rect)
        return;
    *enabled = p->streams[stream].source_enabled;
    *rect = p->streams[stream].source;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamDestRect(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                   UINT stream, BOOL enable, const RECT *rect)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || (enable && !rect))
        return;
    p->streams[stream].dest_enabled = enable;
    if (enable)
        p->streams[stream].dest = *rect;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamDestRect(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                   UINT stream, BOOL *enabled, RECT *rect)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !rect)
        return;
    *enabled = p->streams[stream].dest_enabled;
    *rect = p->streams[stream].dest;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamAlpha(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                UINT stream, BOOL enable, FLOAT alpha)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream))
        return;
    p->streams[stream].alpha_enabled = enable;
    p->streams[stream].alpha = alpha;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamAlpha(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                UINT stream, BOOL *enabled, FLOAT *alpha)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !alpha)
        return;
    *enabled = p->streams[stream].alpha_enabled;
    *alpha = p->streams[stream].alpha;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamLumaKey(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                  UINT stream, BOOL enable, FLOAT lower, FLOAT upper)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream))
        return;
    p->streams[stream].luma_key_enabled = enable;
    p->streams[stream].luma_lower = lower;
    p->streams[stream].luma_upper = upper;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamLumaKey(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                  UINT stream, BOOL *enabled, FLOAT *lower, FLOAT *upper)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !lower || !upper)
        return;
    *enabled = p->streams[stream].luma_key_enabled;
    *lower = p->streams[stream].luma_lower;
    *upper = p->streams[stream].luma_upper;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamPixelAspectRatio(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                           UINT stream, BOOL enable, const DXGI_RATIONAL *source,
                                                           const DXGI_RATIONAL *dest)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || (enable && (!source || !dest)))
        return;
    p->streams[stream].aspect_enabled = enable;
    if (enable) {
        p->streams[stream].aspect_source = *source;
        p->streams[stream].aspect_dest = *dest;
    }
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamPixelAspectRatio(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                           UINT stream, BOOL *enabled, DXGI_RATIONAL *source,
                                                           DXGI_RATIONAL *dest)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !source || !dest)
        return;
    *enabled = p->streams[stream].aspect_enabled;
    *source = p->streams[stream].aspect_source;
    *dest = p->streams[stream].aspect_dest;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamAutoProcessingMode(ID3D11VideoContext1 *iface,
                                                             ID3D11VideoProcessor *processor, UINT stream,
                                                             BOOL enable)
{
    PROCESSOR(processor);
    if (stream_ok(p, stream))
        p->streams[stream].auto_processing = enable;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamAutoProcessingMode(ID3D11VideoContext1 *iface,
                                                             ID3D11VideoProcessor *processor, UINT stream,
                                                             BOOL *enabled)
{
    PROCESSOR(processor);
    if (stream_ok(p, stream) && enabled)
        *enabled = p->streams[stream].auto_processing;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamRotation(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                   UINT stream, BOOL enable, D3D11_VIDEO_PROCESSOR_ROTATION rotation)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream))
        return;
    p->streams[stream].rotation_enabled = enable;
    p->streams[stream].rotation = rotation;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamRotation(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                   UINT stream, BOOL *enabled, D3D11_VIDEO_PROCESSOR_ROTATION *rotation)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !rotation)
        return;
    *enabled = p->streams[stream].rotation_enabled;
    *rotation = p->streams[stream].rotation;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamMirror(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                 UINT stream, BOOL enable, BOOL flip_horizontal, BOOL flip_vertical)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream))
        return;
    p->streams[stream].mirror = enable;
    p->streams[stream].flip_horizontal = flip_horizontal;
    p->streams[stream].flip_vertical = flip_vertical;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamMirror(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                 UINT stream, BOOL *enabled, BOOL *flip_horizontal,
                                                 BOOL *flip_vertical)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || !enabled || !flip_horizontal || !flip_vertical)
        return;
    *enabled = p->streams[stream].mirror;
    *flip_horizontal = p->streams[stream].flip_horizontal;
    *flip_vertical = p->streams[stream].flip_vertical;
}

static void STDMETHODCALLTYPE vc_VideoProcessorSetStreamFilter(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                 UINT stream, D3D11_VIDEO_PROCESSOR_FILTER filter, BOOL enable,
                                                 INT level)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || (UINT)filter >= 8)
        return;
    p->streams[stream].filters_enabled[filter] = enable;
    p->streams[stream].filters[filter] = level;
}

static void STDMETHODCALLTYPE vc_VideoProcessorGetStreamFilter(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                 UINT stream, D3D11_VIDEO_PROCESSOR_FILTER filter, BOOL *enabled,
                                                 INT *level)
{
    PROCESSOR(processor);
    if (!stream_ok(p, stream) || (UINT)filter >= 8 || !enabled || !level)
        return;
    *enabled = p->streams[stream].filters_enabled[filter];
    *level = p->streams[stream].filters[filter];
}

/* ------------------------------------------------------------------ the draw */

/* what a processor draws with: the shader and the sampler, made once for the device. */
typedef struct VpShared {
    ID3D11ComputeShader *shader;
    ID3D11SamplerState *sampler;
} VpShared;

static VpShared *shared_of(D3dDevice *device)
{
    AcquireSRWLockExclusive(&device->video_lock);
    VpShared *s = device->video_processor;
    if (!s) {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (s) {
            D3D11_SAMPLER_DESC sd = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP,
                                     D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP};
            sd.MaxAnisotropy = 1;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(device->iface.lpVtbl->CreateComputeShader(&device->iface, cs_video, sizeof(cs_video), nullptr,
                                                                 &s->shader)) ||
                FAILED(device->iface.lpVtbl->CreateSamplerState(&device->iface, &sd, &s->sampler))) {
                if (s->shader)
                    s->shader->lpVtbl->Release(s->shader);
                HeapFree(GetProcessHeap(), 0, s);
                s = nullptr;
            }
        }
        device->video_processor = s;
    }
    ReleaseSRWLockExclusive(&device->video_lock);
    return s;
}

void D3dVideoProcessorDestroy(D3dDevice *device)
{
    VpShared *s = device->video_processor;
    if (!s)
        return;
    s->shader->lpVtbl->Release(s->shader);
    s->sampler->lpVtbl->Release(s->sampler);
    HeapFree(GetProcessHeap(), 0, s);
    device->video_processor = nullptr;
}

typedef struct VpParams {
    UINT target[4];
    UINT dest[4];
    UINT pitch, format, bytes;
    FLOAT slice;
    FLOAT u[4], v[4];
    FLOAT matrix[3][4];
    FLOAT range[4];
    UINT mode, gamma, pad[2];
    FLOAT background[4];
} VpParams;

/* a matrix of 2 by 3, as rows of an affine map. */
typedef struct Affine {
    FLOAT m[2][3];
} Affine;

static Affine affine_multiply(Affine a, Affine b)
{
    Affine r;
    for (UINT i = 0; i < 2; i++) {
        r.m[i][0] = a.m[i][0] * b.m[0][0] + a.m[i][1] * b.m[1][0];
        r.m[i][1] = a.m[i][0] * b.m[0][1] + a.m[i][1] * b.m[1][1];
        r.m[i][2] = a.m[i][0] * b.m[0][2] + a.m[i][1] * b.m[1][2] + a.m[i][2];
    }
    return r;
}

/* the matrix from a (y, cb, cr) or (r, g, b) to the output's rgb: ranges, the luma coefficients, bit depth. */
static void color_matrix(VpSpace in, VpSpace out, UINT depth, FLOAT matrix[3][4], FLOAT range[4], UINT *gamma)
{
    FLOAT scale = depth == 10 ? 65535.0f / 65472.0f : 1.0f;
    FLOAT white = depth == 10 ? 1023.0f : 255.0f;
    FLOAT shift = depth == 10 ? 4.0f : 1.0f;
    memset(matrix, 0, 12 * sizeof(FLOAT));
    if (in.rgb) {
        /* an rgb source: its range expanded to full. */
        FLOAT a = in.full ? 1.0f : 255.0f / 219.0f, b = in.full ? 0.0f : -16.0f / 219.0f;
        for (UINT i = 0; i < 3; i++) {
            matrix[i][i] = a * scale;
            matrix[i][3] = b;
        }
    } else {
        FLOAT kr = in.matrix == VP_BT601 ? 0.299f : in.matrix == VP_BT709 ? 0.2126f : 0.2627f;
        FLOAT kb = in.matrix == VP_BT601 ? 0.114f : in.matrix == VP_BT709 ? 0.0722f : 0.0593f;
        FLOAT kg = 1.0f - kr - kb;
        FLOAT ya = in.full ? 1.0f : white / (219.0f * shift), yo = in.full ? 0.0f : 16.0f * shift / white;
        FLOAT ca = in.full ? 1.0f : white / (224.0f * shift), mid = 128.0f * shift / white;
        FLOAT rv = 2.0f * (1.0f - kr) * ca, bu = 2.0f * (1.0f - kb) * ca;
        FLOAT gu = -2.0f * (1.0f - kb) * kb / kg * ca, gv = -2.0f * (1.0f - kr) * kr / kg * ca;
        matrix[0][0] = ya * scale, matrix[0][2] = rv * scale;
        matrix[1][0] = ya * scale, matrix[1][1] = gu * scale, matrix[1][2] = gv * scale;
        matrix[2][0] = ya * scale, matrix[2][1] = bu * scale;
        matrix[0][3] = -ya * yo - rv * mid;
        matrix[1][3] = -ya * yo - gu * mid - gv * mid;
        matrix[2][3] = -ya * yo - bu * mid;
    }
    /* the output's range, and the transfer function when it differs. */
    range[0] = 0;
    range[1] = out.full ? 1.0f : 219.0f / 255.0f;
    range[2] = out.full ? 0.0f : 16.0f / 255.0f;
    range[3] = 0;
    *gamma = 0;
    if (in.transfer != out.transfer) {
        BOOL in_linear = in.transfer == VP_G10, out_linear = out.transfer == VP_G10;
        if (!in_linear && out_linear)
            *gamma = 1;
        else if (in_linear && !out_linear)
            *gamma = 2;
    }
}

/* a video color in rgb, from the ycbcr of the background as the legacy matrix says. */
static void background_rgb(const VideoProcessor *p, FLOAT rgba[4])
{
    if (!p->background_ycbcr) {
        rgba[0] = p->background.RGBA.R, rgba[1] = p->background.RGBA.G, rgba[2] = p->background.RGBA.B;
        rgba[3] = p->background.RGBA.A;
        return;
    }
    const D3D11_VIDEO_COLOR_YCbCrA *c = &p->background.YCbCr;
    FLOAT y = c->Y, cb = c->Cb - 0.5f, cr = c->Cr - 0.5f;
    rgba[0] = y + 1.5748f * cr;
    rgba[1] = y - 0.1873f * cb - 0.4681f * cr;
    rgba[2] = y + 1.8556f * cb;
    rgba[3] = c->A;
}

static ID3D11ShaderResourceView *plane_view(D3dDevice *device, VpView *v, UINT plane)
{
    if (v->planes[plane])
        return v->planes[plane];
    D3dResource *r = v->resource;
    BOOL planar = r->tex.fmt->flags & D3D_FORMAT_PLANAR;
    D3D11_SHADER_RESOURCE_VIEW_DESC1 d = {};
    if (planar) {
        BOOL eight = r->tex.format == DXGI_FORMAT_NV12;
        d.Format = plane ? (eight ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R16G16_UNORM)
                         : (eight ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R16_UNORM);
    } else {
        d.Format = r->tex.format;
    }
    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    d.Texture2DArray.MostDetailedMip = v->input_desc.Texture2D.MipSlice;
    d.Texture2DArray.MipLevels = 1;
    d.Texture2DArray.FirstArraySlice = v->input_desc.Texture2D.ArraySlice;
    d.Texture2DArray.ArraySize = 1;
    D3dView *view;
    if (FAILED(D3dCreateView(device, D3D_VIEW_SRV, r->iface, &d, 0, &view)))
        return nullptr;
    v->planes[plane] = (ID3D11ShaderResourceView *)&view->iface.srv;
    return v->planes[plane];
}

/* the buffer the shader writes, at least bytes long. */
static BOOL ensure_buffer(D3dDevice *device, VideoProcessor *p, UINT bytes)
{
    if (p->buffer && p->buffer_size >= bytes)
        return TRUE;
    if (p->buffer_view)
        p->buffer_view->lpVtbl->Release(p->buffer_view);
    if (p->buffer)
        p->buffer->lpVtbl->Release(p->buffer);
    p->buffer_view = nullptr;
    p->buffer = nullptr;
    UINT size = 1u << 20;
    while (size < bytes)
        size *= 2;
    D3D11_BUFFER_DESC bd = {size, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                            D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {DXGI_FORMAT_R32_TYPELESS, D3D11_UAV_DIMENSION_BUFFER};
    ud.Buffer.NumElements = size / 4;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (FAILED(device->iface.lpVtbl->CreateBuffer(&device->iface, &bd, nullptr, &p->buffer)) ||
        FAILED(device->iface.lpVtbl->CreateUnorderedAccessView(&device->iface, (ID3D11Resource *)p->buffer, &ud,
                                                               &p->buffer_view)))
        return FALSE;
    p->buffer_size = size;
    return TRUE;
}

static RECT whole(UINT width, UINT height) { return (RECT){0, 0, (LONG)width, (LONG)height}; }

static LONG clamp_long(LONG v, LONG lo, LONG hi) { return v < lo ? lo : v > hi ? hi : v; }

/*
 * the draw of one stream: the compute shader over the target rectangle, then the buffer copied into the output's
 * texture. what the program had bound for compute is bound again after.
 */
static HRESULT draw_stream(D3dContext *c, VideoProcessor *p, VpView *out, VpView *in, const VpStream *stream, BOOL first)
{
    D3dDevice *device = c->child.device;
    D3dResource *target = out->resource;
    UINT bytes;
    VpFormat format = output_format(target->tex.format, &bytes);
    if (format == VP_FORMAT_NONE)
        return E_INVALIDARG;
    VpShared *shared = shared_of(device);
    if (!shared)
        return E_FAIL;

    UINT mip = out->output_desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D ? out->output_desc.Texture2D.MipSlice
                                                                                  : out->output_desc.Texture2DArray.MipSlice;
    UINT layer = out->output_desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D
                     ? 0
                     : out->output_desc.Texture2DArray.FirstArraySlice;
    VkExtent3D extent = D3dMipExtent(target, mip);
    RECT surface = whole(extent.width, extent.height);
    RECT target_rect = p->target_enabled ? p->target : surface;
    target_rect.left = clamp_long(target_rect.left, 0, surface.right);
    target_rect.top = clamp_long(target_rect.top, 0, surface.bottom);
    target_rect.right = clamp_long(target_rect.right, target_rect.left, surface.right);
    target_rect.bottom = clamp_long(target_rect.bottom, target_rect.top, surface.bottom);
    UINT tw = target_rect.right - target_rect.left, th = target_rect.bottom - target_rect.top;
    if (!tw || !th)
        return S_OK;
    UINT pitch = tw * bytes;
    if (!ensure_buffer(device, p, pitch * th))
        return E_OUTOFMEMORY;

    VpParams params;
    memset(&params, 0, sizeof(params));
    params.target[0] = target_rect.left, params.target[1] = target_rect.top, params.target[2] = tw,
    params.target[3] = th;
    params.pitch = pitch;
    params.format = format;
    params.bytes = bytes;
    background_rgb(p, params.background);
    params.background[3] = 1.0f;
    ID3D11ShaderResourceView *srv[2] = {nullptr, nullptr};
    if (in) {
        D3dResource *src = in->resource;
        BOOL planar = src->tex.fmt->flags & D3D_FORMAT_PLANAR;
        UINT depth = src->tex.format == DXGI_FORMAT_NV12 ? 8 : 10;
        VkExtent3D se = D3dMipExtent(src, in->input_desc.Texture2D.MipSlice);
        RECT source = stream->source_enabled ? stream->source : whole(se.width, se.height);
        source.left = clamp_long(source.left, 0, se.width), source.right = clamp_long(source.right, 0, se.width);
        source.top = clamp_long(source.top, 0, se.height), source.bottom = clamp_long(source.bottom, 0, se.height);
        RECT dest = stream->dest_enabled ? stream->dest : target_rect;
        dest.left = clamp_long(dest.left, 0, surface.right), dest.right = clamp_long(dest.right, 0, surface.right);
        dest.top = clamp_long(dest.top, 0, surface.bottom), dest.bottom = clamp_long(dest.bottom, 0, surface.bottom);
        if (source.right <= source.left || source.bottom <= source.top || dest.right <= dest.left ||
            dest.bottom <= dest.top) {
            /* nothing of the source shows: the background alone. */
            params.dest[0] = params.dest[1] = params.dest[2] = params.dest[3] = 0;
        } else {
            params.dest[0] = dest.left, params.dest[1] = dest.top, params.dest[2] = dest.right,
            params.dest[3] = dest.bottom;
        }
        /* the output texel to the unit square of the destination, mirrored and turned, to the source's texture. */
        FLOAT dw = (FLOAT)(dest.right - dest.left), dh = (FLOAT)(dest.bottom - dest.top);
        Affine to_unit = {{{dw ? 1.0f / dw : 0, 0, dw ? -(FLOAT)dest.left / dw : 0},
                           {0, dh ? 1.0f / dh : 0, dh ? -(FLOAT)dest.top / dh : 0}}};
        Affine mirror = {{{1, 0, 0}, {0, 1, 0}}};
        if (stream->mirror && stream->flip_horizontal)
            mirror.m[0][0] = -1, mirror.m[0][2] = 1;
        if (stream->mirror && stream->flip_vertical)
            mirror.m[1][1] = -1, mirror.m[1][2] = 1;
        Affine turn = {{{1, 0, 0}, {0, 1, 0}}};
        if (stream->rotation_enabled) {
            switch (stream->rotation) {
            case D3D11_VIDEO_PROCESSOR_ROTATION_90:
                turn = (Affine){{{0, 1, 0}, {-1, 0, 1}}};
                break;
            case D3D11_VIDEO_PROCESSOR_ROTATION_180:
                turn = (Affine){{{-1, 0, 1}, {0, -1, 1}}};
                break;
            case D3D11_VIDEO_PROCESSOR_ROTATION_270:
                turn = (Affine){{{0, -1, 1}, {1, 0, 0}}};
                break;
            default:
                break;
            }
        }
        FLOAT sw = (FLOAT)(source.right - source.left), sh = (FLOAT)(source.bottom - source.top);
        Affine to_source = {{{sw / se.width, 0, (FLOAT)source.left / se.width},
                             {0, sh / se.height, (FLOAT)source.top / se.height}}};
        Affine map = affine_multiply(to_source, affine_multiply(turn, affine_multiply(mirror, to_unit)));
        params.u[0] = map.m[0][0], params.u[1] = map.m[0][1], params.u[2] = map.m[0][2];
        params.v[0] = map.m[1][0], params.v[1] = map.m[1][1], params.v[2] = map.m[1][2];
        params.slice = (FLOAT)in->input_desc.Texture2D.ArraySlice;

        VpSpace in_space, out_space;
        DXGI_COLOR_SPACE_TYPE legacy = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        if (stream->has_space) {
            space_of(stream->space, &in_space);
        } else if (stream->has_legacy_space) {
            in_space = legacy_space(&stream->legacy_space, !planar);
        } else {
            space_of(planar ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 : legacy, &in_space);
        }
        if (p->has_output_space)
            space_of(p->output_space, &out_space);
        else if (p->has_legacy_output_space)
            out_space = legacy_space(&p->legacy_output_space, TRUE);
        else
            space_of(legacy, &out_space);
        in_space.rgb = !planar;
        color_matrix(in_space, out_space, depth, params.matrix, params.range, &params.gamma);
        params.mode = !planar;
        /* chroma sited at the left of its luma samples is half a luma texel to the left of where the plane's texels say. */
        if (planar && in_space.left)
            params.u[3] = 0.5f / (FLOAT)se.width;
        if (planar && in_space.top)
            params.v[3] = 0.5f / (FLOAT)se.height;
        srv[0] = plane_view(device, in, 0);
        srv[1] = planar ? plane_view(device, in, 1) : srv[0];
        if (!srv[0] || !srv[1])
            return E_FAIL;
        D3dContextUse(c, src);
    } else {
        /* no stream: the background over the target, the color matrix not read. */
        params.mode = 1;
        for (UINT i = 0; i < 3; i++)
            params.matrix[i][i] = 1.0f;
        params.range[1] = 1.0f;
    }
    (void)first;

    ID3D11DeviceContext *ctx = (ID3D11DeviceContext *)&c->iface;
    if (!p->constants) {
        D3D11_BUFFER_DESC cd = {(sizeof(VpParams) + 15) & ~15u, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
        if (FAILED(device->iface.lpVtbl->CreateBuffer(&device->iface, &cd, nullptr, &p->constants)))
            return E_OUTOFMEMORY;
    }
    ctx->lpVtbl->UpdateSubresource(ctx, (ID3D11Resource *)p->constants, 0, nullptr, &params, 0, 0);

    /* the compute state of the program is kept and bound again. */
    ID3D11ComputeShader *saved_shader;
    ID3D11ShaderResourceView *saved_srv[2];
    ID3D11SamplerState *saved_sampler;
    ID3D11UnorderedAccessView *saved_uav;
    ID3D11Buffer *saved_buffer;
    ctx->lpVtbl->CSGetShader(ctx, &saved_shader, nullptr, nullptr);
    ctx->lpVtbl->CSGetShaderResources(ctx, 0, 2, saved_srv);
    ctx->lpVtbl->CSGetSamplers(ctx, 0, 1, &saved_sampler);
    ctx->lpVtbl->CSGetUnorderedAccessViews(ctx, 0, 1, &saved_uav);
    ctx->lpVtbl->CSGetConstantBuffers(ctx, 0, 1, &saved_buffer);

    ctx->lpVtbl->CSSetShader(ctx, shared->shader, nullptr, 0);
    ctx->lpVtbl->CSSetShaderResources(ctx, 0, 2, srv);
    ctx->lpVtbl->CSSetSamplers(ctx, 0, 1, &shared->sampler);
    ctx->lpVtbl->CSSetConstantBuffers(ctx, 0, 1, &p->constants);
    ctx->lpVtbl->CSSetUnorderedAccessViews(ctx, 0, 1, &p->buffer_view, nullptr);
    ctx->lpVtbl->Dispatch(ctx, (tw + 7) / 8, (th + 7) / 8, 1);

    ctx->lpVtbl->CSSetShader(ctx, saved_shader, nullptr, 0);
    ctx->lpVtbl->CSSetShaderResources(ctx, 0, 2, saved_srv);
    ctx->lpVtbl->CSSetSamplers(ctx, 0, 1, &saved_sampler);
    ctx->lpVtbl->CSSetConstantBuffers(ctx, 0, 1, &saved_buffer);
    ctx->lpVtbl->CSSetUnorderedAccessViews(ctx, 0, 1, &saved_uav, nullptr);
    if (saved_shader)
        saved_shader->lpVtbl->Release(saved_shader);
    for (UINT i = 0; i < 2; i++)
        if (saved_srv[i])
            saved_srv[i]->lpVtbl->Release(saved_srv[i]);
    if (saved_sampler)
        saved_sampler->lpVtbl->Release(saved_sampler);
    if (saved_uav)
        saved_uav->lpVtbl->Release(saved_uav);
    if (saved_buffer)
        saved_buffer->lpVtbl->Release(saved_buffer);

    /* the words the shader wrote, into the texture. */
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return E_FAIL;
    D3dResource *buffer = D3dResourceFrom((ID3D11Resource *)p->buffer);
    VkBufferImageCopy copy = {};
    copy.bufferOffset = buffer->buf.offset;
    copy.bufferRowLength = pitch / bytes;
    copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
    copy.imageOffset = (VkOffset3D){target_rect.left, target_rect.top, 0};
    copy.imageExtent = (VkExtent3D){tw, th, 1};
    device->vk.vkCmdCopyBufferToImage(cmd, buffer->buf.buffer, target->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    c->pending_barrier = TRUE;
    D3dContextUse(c, buffer);
    D3dContextUse(c, target);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vc_VideoProcessorBlt(ID3D11VideoContext1 *iface, ID3D11VideoProcessor *processor,
                                                      ID3D11VideoProcessorOutputView *output, UINT frame, UINT count,
                                                      const D3D11_VIDEO_PROCESSOR_STREAM *streams)
{
    D3dContext *c = CONTAINING_RECORD(iface, D3dContext, video);
    PROCESSOR(processor);
    if (!p || !output || count > MAX_STREAMS || (count && !streams))
        return E_INVALIDARG;
    VpView *out = vpview_impl(output);
    if (!out->is_output || out->child.device != c->child.device || p->child.device != c->child.device)
        return E_INVALIDARG;
    VkExtent3D surface = D3dMipExtent(out->resource, out->output_desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D
                                                          ? out->output_desc.Texture2D.MipSlice
                                                          : out->output_desc.Texture2DArray.MipSlice);
    if (p->target_enabled && (p->target.right > (LONG)surface.width || p->target.bottom > (LONG)surface.height))
        return E_INVALIDARG;
    for (UINT i = 0; i < count; i++) {
        /* a progressive stream makes one output frame, an interlaced one two. */
        UINT frames = p->streams[i].frame_format == D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE ? 1 : 2;
        if (streams[i].OutputIndex >= frames)
            return E_INVALIDARG;
        if (streams[i].Enable && !streams[i].pInputSurface)
            return E_INVALIDARG;
        if (streams[i].Enable) {
            VpView *in = vpview_impl(streams[i].pInputSurface);
            if (in->is_output || in->child.device != c->child.device)
                return E_INVALIDARG;
        }
    }
    BOOL drawn = FALSE;
    for (UINT i = 0; i < count; i++) {
        if (!streams[i].Enable)
            continue;
        HRESULT hr = draw_stream(c, p, out, vpview_impl(streams[i].pInputSurface), &p->streams[i], !drawn);
        if (FAILED(hr))
            return hr;
        drawn = TRUE;
    }
    /* no stream enabled: the background fills the target. */
    if (!drawn)
        return draw_stream(c, p, out, nullptr, &p->streams[0], TRUE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vc_VideoProcessorSetStreamExtension(ID3D11VideoContext1 *iface,
                                                                     ID3D11VideoProcessor *processor, UINT stream,
                                                                     const GUID *guid, UINT size, void *data)
{
    return E_NOTIMPL;
}

/* the methods of the device and context, the rest of video.c's tables. */
HRESULT D3dVideoCreateEnumerator(D3dDevice *device, const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc,
                                 ID3D11VideoProcessorEnumerator **out)
{
    return vp_create_enumerator(device, desc, out);
}

HRESULT D3dVideoCreateProcessor(D3dDevice *device, ID3D11VideoProcessorEnumerator *enumerator, UINT rate_index,
                                ID3D11VideoProcessor **out)
{
    return vp_create_processor(device, enumerator, rate_index, out);
}

HRESULT D3dVideoCreateInputView(D3dDevice *device, ID3D11Resource *resource, ID3D11VideoProcessorEnumerator *enumerator,
                                const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *desc, ID3D11VideoProcessorInputView **out)
{
    return vp_create_view(device, FALSE, resource, enumerator, desc, (void **)out);
}

HRESULT D3dVideoCreateOutputView(D3dDevice *device, ID3D11Resource *resource,
                                 ID3D11VideoProcessorEnumerator *enumerator,
                                 const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *desc,
                                 ID3D11VideoProcessorOutputView **out)
{
    return vp_create_view(device, TRUE, resource, enumerator, desc, (void **)out);
}
