/*
 * direct3d 11 video, the decoders of it: ID3D11VideoDevice and ID3D11VideoContext as a browser's hardware
 * decoder uses them, over the host's video decode queue (guest/vulkan-1/video.c, host/linux/video.c). a
 * decoder session is made on the host; the buffers a program gets from GetDecoderBuffer are guest memory,
 * copied together when it submits them, and the picture is decoded when its frame ends. the pictures are
 * decoded into textures of the device, which the next submission of the device waits for.
 *
 * video processors, crypto sessions and authenticated channels do not exist: their methods fail or do nothing.
 */
#include "d3d11_private.h"
#include "video_dxbc.h"

#define VTBL_FN(f) ((void *)(f))

/* ------------------------------------------------------------------ profiles */

/* what the wire carries of one picture's bitstream, a call being at most half the ring. */
#define MVK_MAX_BITSTREAM (6u << 20)

static const GUID ZERO_GUID;
static const GUID NO_ENCRYPT = {0x1B81BED0, 0xA0C7, 0x11D3, {0xB9, 0x84, 0x00, 0xC0, 0x4F, 0x2E, 0x73, 0xC5}};

typedef struct Profile {
    GUID guid;
    UINT codec, profile, depth;
    DXGI_FORMAT format;
    /* the slice format dxva configures it with: 2 for h.264's short slices, 1 for the whole frame of vp9. */
    UINT bitstream_raw;
    /* surfaces a decoder wants at least, a guess of the references and the picture being decoded. */
    USHORT min_surfaces;
} Profile;

static const Profile PROFILES[] = {
    {{0x1B81BE68, 0xA0C7, 0x11D3, {0xB9, 0x84, 0x00, 0xC0, 0x4F, 0x2E, 0x73, 0xC5}}, MVK_VIDEO_CODEC_H264,
     MVK_VIDEO_H264_HIGH, 8, DXGI_FORMAT_NV12, 2, 18},
    {{0x463707F8, 0xA1D0, 0x4585, {0x87, 0x6D, 0x83, 0xAA, 0x6D, 0x60, 0xB8, 0x9E}}, MVK_VIDEO_CODEC_VP9, 0, 8,
     DXGI_FORMAT_NV12, 1, 10},
    {{0xA4C749EF, 0x6ECF, 0x48AA, {0x84, 0x48, 0x50, 0xA7, 0xA1, 0x16, 0x5F, 0xF7}}, MVK_VIDEO_CODEC_VP9, 2, 10,
     DXGI_FORMAT_P010, 1, 10},
};

/* the picture buffers a decoder hands out, by D3D11_VIDEO_DECODER_BUFFER_TYPE. */
enum { BUFFER_PARAMS = 0, BUFFER_QMATRIX = 4, BUFFER_SLICES = 5, BUFFER_BITSTREAM = 6, BUFFER_TYPES = 9 };

static const UINT BUFFER_SIZES[BUFFER_TYPES] = {
    [BUFFER_PARAMS] = 0x2000,
    [BUFFER_QMATRIX] = 0x1000,
    [BUFFER_SLICES] = 0x10000,
    [BUFFER_BITSTREAM] = 0x400000,
};

/* the profiles the host decodes, in the order of the table. */
static const MvkVideoCaps *capability(D3dDevice *device, const Profile *p)
{
    for (UINT i = 0; i < device->video_count; i++)
        if (device->video_caps[i].codec == p->codec && device->video_caps[i].profile == p->profile &&
            device->video_caps[i].bit_depth == p->depth)
            return &device->video_caps[i];
    return nullptr;
}

static const Profile *profile_at(D3dDevice *device, UINT index)
{
    for (UINT i = 0; i < RTL_NUMBER_OF(PROFILES); i++)
        if (capability(device, &PROFILES[i]) && index-- == 0)
            return &PROFILES[i];
    return nullptr;
}

static const Profile *profile_of(D3dDevice *device, const GUID *guid)
{
    for (UINT i = 0; i < RTL_NUMBER_OF(PROFILES); i++)
        if (IsEqualGUID(guid, &PROFILES[i].guid) && capability(device, &PROFILES[i]))
            return &PROFILES[i];
    return nullptr;
}

/* asks the host, once, which profiles its gpu decodes. */
void D3dVideoProbe(D3dDevice *device)
{
    AcquireSRWLockExclusive(&device->video_lock);
    if (!device->video_probed) {
        device->video_probed = TRUE;
        UINT count = 0;
        if (device->has_video && device->vk.vkMimeVideoGetCaps &&
            device->vk.vkMimeVideoGetCaps(device->vk.device, &count, device->video_caps,
                                          RTL_NUMBER_OF(device->video_caps)) == VK_SUCCESS)
            device->video_count = D3D_MIN(count, RTL_NUMBER_OF(device->video_caps));
        for (UINT i = 0; i < RTL_NUMBER_OF(PROFILES); i++)
            if (capability(device, &PROFILES[i]))
                device->video_depths |= PROFILES[i].depth == 8 ? D3D_VIDEO_8BIT : D3D_VIDEO_10BIT;
    }
    ReleaseSRWLockExclusive(&device->video_lock);
}

/* ------------------------------------------------------------------ objects */

typedef struct VideoView {
    ID3D11VideoDecoderOutputView iface;
    D3dChild child;
    D3dResource *resource;
    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC desc;
    VkImageView view;
} VideoView;

/* one entry of the slice control buffer: Windows' structure, 10 bytes as dxva.h packs its structures by one. */
typedef struct SliceEntry {
    UINT location;
    UINT bytes;
    USHORT chopping;
} __attribute__((packed)) SliceEntry;

typedef struct VideoDecoder {
    ID3D11VideoDecoder iface;
    D3dChild child;
    D3D11_VIDEO_DECODER_DESC desc;
    D3D11_VIDEO_DECODER_CONFIG config;
    const Profile *profile;
    UINT64 session;
    /* the memory GetDecoderBuffer gives out, and which of it a program holds. */
    struct {
        BYTE *data;
        BOOL held;
    } buffers[BUFFER_TYPES];
    /* the frame between begin and end: the view, and what was submitted, joined together. */
    BOOL in_frame;
    VideoView *view;
    BYTE *params;
    UINT params_size, params_capacity;
    BYTE *qmatrix;
    UINT qmatrix_size, qmatrix_capacity;
    SliceEntry *slices;
    UINT slice_count, slice_capacity;
    BYTE *bits;
    UINT bits_size, bits_capacity;
} VideoDecoder;

static VideoDecoder *decoder_impl(ID3D11VideoDecoder *iface) { return CONTAINING_RECORD(iface, VideoDecoder, iface); }
static VideoView *view_impl(void *iface) { return CONTAINING_RECORD(iface, VideoView, iface); }
static D3dDevice *device_of(ID3D11VideoDevice1 *iface) { return CONTAINING_RECORD(iface, D3dDevice, video); }
static D3dContext *context_of(ID3D11VideoContext1 *iface) { return CONTAINING_RECORD(iface, D3dContext, video); }

static HRESULT STDMETHODCALLTYPE decoder_query(ID3D11VideoDecoder *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D11DeviceChild) &&
        !IsEqualGUID(riid, &IID_ID3D11VideoDecoder))
        return E_NOINTERFACE;
    InterlockedIncrement(&decoder_impl(iface)->child.refs);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE decoder_add_ref(ID3D11VideoDecoder *iface)
{
    return InterlockedIncrement(&decoder_impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE decoder_release(ID3D11VideoDecoder *iface)
{
    VideoDecoder *d = decoder_impl(iface);
    ULONG refs = InterlockedDecrement(&d->child.refs);
    if (refs)
        return refs;
    D3dDevice *device = d->child.device;
    /* what it decodes is done before the session goes: the host waits for its queue. */
    if (d->session && device->vk.vkMimeVideoDestroySession)
        device->vk.vkMimeVideoDestroySession(device->vk.device, d->session);
    for (UINT i = 0; i < BUFFER_TYPES; i++)
        if (d->buffers[i].data)
            VirtualFree(d->buffers[i].data, 0, MEM_RELEASE);
    if (d->view)
        d->view->iface.lpVtbl->Release(&d->view->iface);
    HeapFree(GetProcessHeap(), 0, d->params);
    HeapFree(GetProcessHeap(), 0, d->qmatrix);
    HeapFree(GetProcessHeap(), 0, d->slices);
    HeapFree(GetProcessHeap(), 0, d->bits);
    D3dChildRelease(&d->child);
    HeapFree(GetProcessHeap(), 0, d);
    return 0;
}

static void STDMETHODCALLTYPE decoder_get_device(ID3D11VideoDecoder *iface, ID3D11Device **device)
{
    D3dDevice *d = decoder_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE decoder_get_private_data(ID3D11VideoDecoder *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&decoder_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE decoder_set_private_data(ID3D11VideoDecoder *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&decoder_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE decoder_set_private_data_interface(ID3D11VideoDecoder *iface, REFGUID guid,
                                                                    const IUnknown *data)
{
    return MimePrivateSetInterface(&decoder_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE decoder_get_creation_parameters(ID3D11VideoDecoder *iface,
                                                                 D3D11_VIDEO_DECODER_DESC *desc,
                                                                 D3D11_VIDEO_DECODER_CONFIG *config)
{
    VideoDecoder *d = decoder_impl(iface);
    if (desc)
        *desc = d->desc;
    if (config)
        *config = d->config;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_get_driver_handle(ID3D11VideoDecoder *iface, HANDLE *handle)
{
    if (handle)
        *handle = nullptr;
    return E_NOTIMPL;
}

static const ID3D11VideoDecoderVtbl decoder_vtbl = {
    decoder_query,
    decoder_add_ref,
    decoder_release,
    decoder_get_device,
    decoder_get_private_data,
    decoder_set_private_data,
    decoder_set_private_data_interface,
    decoder_get_creation_parameters,
    decoder_get_driver_handle,
};

/* ------------------------------------------------------------------ output views */

static HRESULT STDMETHODCALLTYPE view_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D11DeviceChild) &&
        !IsEqualGUID(riid, &IID_ID3D11View) && !IsEqualGUID(riid, &IID_ID3D11VideoDecoderOutputView))
        return E_NOINTERFACE;
    InterlockedIncrement(&view_impl(iface)->child.refs);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE view_add_ref(void *iface) { return InterlockedIncrement(&view_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE view_release(void *iface)
{
    VideoView *v = view_impl(iface);
    ULONG refs = InterlockedDecrement(&v->child.refs);
    if (refs)
        return refs;
    D3dDevice *device = v->child.device;
    D3dDefer(device, D3D_MAX(v->resource->last_use, device->next_serial), D3D_GARBAGE_IMAGE_VIEW, (UINT64)v->view);
    D3dResourceRelease(v->resource);
    D3dChildRelease(&v->child);
    HeapFree(GetProcessHeap(), 0, v);
    return 0;
}

static void STDMETHODCALLTYPE view_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = view_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE view_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&view_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE view_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&view_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE view_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&view_impl(iface)->child.data, guid, data);
}

static void STDMETHODCALLTYPE view_get_resource(void *iface, ID3D11Resource **resource)
{
    D3dResource *r = view_impl(iface)->resource;
    D3dResourceAddRef(r);
    *resource = r->iface;
}

static void STDMETHODCALLTYPE view_get_desc(void *iface, D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC *desc)
{
    *desc = view_impl(iface)->desc;
}

static const ID3D11VideoDecoderOutputViewVtbl view_vtbl = {
    VTBL_FN(view_query),
    VTBL_FN(view_add_ref),
    VTBL_FN(view_release),
    VTBL_FN(view_get_device),
    VTBL_FN(view_get_private_data),
    VTBL_FN(view_set_private_data),
    VTBL_FN(view_set_private_data_interface),
    VTBL_FN(view_get_resource),
    VTBL_FN(view_get_desc),
};

/* ------------------------------------------------------------------ the device */

#include "video_stubs.inc"
#include "video_processor.inc"


static HRESULT STDMETHODCALLTYPE vd_QueryInterface(ID3D11VideoDevice1 *iface, REFIID riid, void **out)
{
    D3dDevice *device = device_of(iface);
    return device->iface.lpVtbl->QueryInterface(&device->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE vd_AddRef(ID3D11VideoDevice1 *iface)
{
    D3dDevice *device = device_of(iface);
    return device->iface.lpVtbl->AddRef(&device->iface);
}

static ULONG STDMETHODCALLTYPE vd_Release(ID3D11VideoDevice1 *iface)
{
    D3dDevice *device = device_of(iface);
    return device->iface.lpVtbl->Release(&device->iface);
}

static UINT STDMETHODCALLTYPE vd_GetVideoDecoderProfileCount(ID3D11VideoDevice1 *iface)
{
    D3dDevice *device = device_of(iface);
    D3dVideoProbe(device);
    UINT n = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(PROFILES); i++)
        n += capability(device, &PROFILES[i]) != nullptr;
    return n;
}

static HRESULT STDMETHODCALLTYPE vd_GetVideoDecoderProfile(ID3D11VideoDevice1 *iface, UINT index, GUID *guid)
{
    D3dDevice *device = device_of(iface);
    if (!guid)
        return E_INVALIDARG;
    D3dVideoProbe(device);
    const Profile *p = profile_at(device, index);
    if (!p)
        return E_INVALIDARG;
    *guid = p->guid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vd_CheckVideoDecoderFormat(ID3D11VideoDevice1 *iface, const GUID *guid,
                                                            DXGI_FORMAT format, BOOL *supported)
{
    D3dDevice *device = device_of(iface);
    if (!supported || !guid)
        return E_INVALIDARG;
    D3dVideoProbe(device);
    const Profile *p = profile_of(device, guid);
    if (!p)
        return E_INVALIDARG;
    *supported = format == p->format;
    return S_OK;
}

/* the checks every decoder description goes through before the host is asked: its format, then its size. */
static HRESULT validate_desc(D3dDevice *device, const D3D11_VIDEO_DECODER_DESC *desc, const Profile **profile)
{
    BOOL supported = FALSE;
    D3dVideoProbe(device);
    *profile = profile_of(device, &desc->Guid);
    if (!*profile)
        return E_INVALIDARG;
    supported = desc->OutputFormat == (*profile)->format;
    if (!supported || !desc->SampleWidth || !desc->SampleHeight)
        return E_INVALIDARG;
    return S_OK;
}

static BOOL size_supported(D3dDevice *device, const Profile *p, UINT width, UINT height)
{
    const MvkVideoCaps *caps = capability(device, p);
    return caps && width >= caps->min_width && height >= caps->min_height && width <= caps->max_width &&
           height <= caps->max_height;
}

static HRESULT STDMETHODCALLTYPE vd_GetVideoDecoderConfigCount(ID3D11VideoDevice1 *iface,
                                                               const D3D11_VIDEO_DECODER_DESC *desc, UINT *count)
{
    D3dDevice *device = device_of(iface);
    if (!desc || !count)
        return E_INVALIDARG;
    *count = 0;
    const Profile *p;
    HRESULT hr = validate_desc(device, desc, &p);
    if (FAILED(hr))
        return hr;
    *count = size_supported(device, p, desc->SampleWidth, desc->SampleHeight) ? 1 : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vd_GetVideoDecoderConfig(ID3D11VideoDevice1 *iface,
                                                          const D3D11_VIDEO_DECODER_DESC *desc, UINT index,
                                                          D3D11_VIDEO_DECODER_CONFIG *config)
{
    D3dDevice *device = device_of(iface);
    if (!desc || !config)
        return E_INVALIDARG;
    memset(config, 0, sizeof(*config));
    UINT count = 0;
    if (FAILED(vd_GetVideoDecoderConfigCount(iface, desc, &count)) || index >= count)
        return E_INVALIDARG;
    const Profile *p;
    if (FAILED(validate_desc(device, desc, &p)))
        return E_INVALIDARG;
    config->guidConfigBitstreamEncryption = NO_ENCRYPT;
    config->guidConfigMBcontrolEncryption = NO_ENCRYPT;
    config->guidConfigResidDiffEncryption = NO_ENCRYPT;
    config->ConfigBitstreamRaw = p->bitstream_raw;
    config->ConfigHostInverseScan = p->codec == MVK_VIDEO_CODEC_H264;
    config->ConfigMinRenderTargetBuffCount = p->min_surfaces;
    /* bit 14 clear: a texture array is what the decoder wants, bit 15 clear: a new decoder after a format change. */
    config->ConfigDecoderSpecific = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vd_GetVideoDecoderCaps(ID3D11VideoDevice1 *iface, const GUID *guid, UINT width,
                                                        UINT height, const DXGI_RATIONAL *rate, UINT bit_rate,
                                                        const GUID *crypto, UINT *caps)
{
    D3dDevice *device = device_of(iface);
    if (caps)
        *caps = 0;
    if (!guid || !caps || !rate)
        return E_INVALIDARG;
    D3dVideoProbe(device);
    const Profile *p = profile_of(device, guid);
    if (!p || !width || !height)
        return E_INVALIDARG;
    /* a protected decoder is not offered, a size the gpu does not decode is no decoder at all. */
    if (crypto || !size_supported(device, p, width, height))
        *caps = D3D11_VIDEO_DECODER_CAPS_UNSUPPORTED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vd_CheckVideoDecoderDownsampling(ID3D11VideoDevice1 *iface,
                                                                  const D3D11_VIDEO_DECODER_DESC *desc,
                                                                  DXGI_COLOR_SPACE_TYPE color_space,
                                                                  const D3D11_VIDEO_DECODER_CONFIG *config,
                                                                  const DXGI_RATIONAL *rate,
                                                                  const D3D11_VIDEO_SAMPLE_DESC *output,
                                                                  BOOL *supported, BOOL *real_time)
{
    D3dDevice *device = device_of(iface);
    if (!desc || !config || !output || !rate || !supported || !real_time)
        return E_INVALIDARG;
    *supported = *real_time = FALSE;
    const Profile *p;
    HRESULT hr = validate_desc(device, desc, &p);
    if (FAILED(hr))
        return hr;
    if (!output->Width || !output->Height)
        return E_INVALIDARG;
    /* downsampling is a capability the gpu's decoder does not have, which makes the call invalid as for any other. */
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE vd_SetPrivateData(ID3D11VideoDevice1 *iface, REFGUID guid, UINT size, const void *data)
{
    D3dDevice *device = device_of(iface);
    return MimePrivateSet(&device->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE vd_SetPrivateDataInterface(ID3D11VideoDevice1 *iface, REFGUID guid,
                                                            const IUnknown *data)
{
    D3dDevice *device = device_of(iface);
    return MimePrivateSetInterface(&device->data, guid, data);
}

static HRESULT STDMETHODCALLTYPE vd_CreateVideoDecoder(ID3D11VideoDevice1 *iface, const D3D11_VIDEO_DECODER_DESC *desc,
                                                       const D3D11_VIDEO_DECODER_CONFIG *config,
                                                       ID3D11VideoDecoder **out)
{
    D3dDevice *device = device_of(iface);
    if (!out || !config || !desc)
        return E_INVALIDARG;
    *out = nullptr;
    const Profile *p;
    HRESULT hr = validate_desc(device, desc, &p);
    if (FAILED(hr))
        return hr;
    if (config->ConfigBitstreamRaw != p->bitstream_raw ||
        (!IsEqualGUID(&config->guidConfigBitstreamEncryption, &NO_ENCRYPT) &&
         !IsEqualGUID(&config->guidConfigBitstreamEncryption, &ZERO_GUID)) ||
        !size_supported(device, p, desc->SampleWidth, desc->SampleHeight))
        return E_INVALIDARG;
    VideoDecoder *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->iface.lpVtbl = &decoder_vtbl;
    D3dChildInit(&d->child, device);
    d->desc = *desc;
    d->config = *config;
    d->profile = p;
    MvkVideoSessionInfo info = {p->codec, p->profile, p->depth, desc->SampleWidth, desc->SampleHeight};
    VkResult result = device->vk.vkMimeVideoCreateSession(device->vk.device, &info, &d->session);
    if (result != VK_SUCCESS) {
        D3dLog("d3d11: the host did not make a video decoder (%d)", result);
        d->session = 0;
        decoder_release(&d->iface);
        return E_FAIL;
    }
    *out = &d->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vd_CreateVideoDecoderOutputView(ID3D11VideoDevice1 *iface, ID3D11Resource *resource,
                                                                 const D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC *desc,
                                                                 ID3D11VideoDecoderOutputView **out)
{
    D3dDevice *device = device_of(iface);
    if (out)
        *out = nullptr;
    if (!resource || !desc)
        return E_INVALIDARG;
    D3dResource *r = D3dResourceFrom(resource);
    if (r->dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || !(r->bind & D3D11_BIND_DECODER))
        return E_INVALIDARG;
    D3dVideoProbe(device);
    const Profile *p = profile_of(device, &desc->DecodeProfile);
    if (!p || r->tex.format != p->format)
        return E_INVALIDARG;
    if (desc->ViewDimension != D3D11_VDOV_DIMENSION_TEXTURE2D || desc->Texture2D.ArraySlice >= r->tex.layers)
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;
    VideoView *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v)
        return E_OUTOFMEMORY;
    v->iface.lpVtbl = &view_vtbl;
    D3dChildInit(&v->child, device);
    v->resource = r;
    D3dResourceAddRef(r);
    v->desc = *desc;
    /* a view of the whole picture, both planes, the layer being decoded into. */
    VkImageViewUsageCreateInfo usage = {VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
    usage.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, &usage};
    info.image = r->tex.image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = r->tex.fmt->vk;
    info.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, desc->Texture2D.ArraySlice, 1};
    if (device->vk.vkCreateImageView(device->vk.device, &info, nullptr, &v->view) != VK_SUCCESS) {
        view_release(&v->iface);
        return E_FAIL;
    }
    *out = &v->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vd_CreateVideoProcessorEnumerator(ID3D11VideoDevice1 *iface,
                                                                   const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc,
                                                                   ID3D11VideoProcessorEnumerator **out)
{
    return D3dVideoCreateEnumerator(device_of(iface), desc, out);
}

static HRESULT STDMETHODCALLTYPE vd_CreateVideoProcessor(ID3D11VideoDevice1 *iface,
                                                         ID3D11VideoProcessorEnumerator *enumerator, UINT rate_index,
                                                         ID3D11VideoProcessor **out)
{
    return D3dVideoCreateProcessor(device_of(iface), enumerator, rate_index, out);
}

static HRESULT STDMETHODCALLTYPE vd_CreateVideoProcessorInputView(ID3D11VideoDevice1 *iface, ID3D11Resource *resource,
                                                                  ID3D11VideoProcessorEnumerator *enumerator,
                                                                  const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *desc,
                                                                  ID3D11VideoProcessorInputView **out)
{
    return D3dVideoCreateInputView(device_of(iface), resource, enumerator, desc, out);
}

static HRESULT STDMETHODCALLTYPE vd_CreateVideoProcessorOutputView(ID3D11VideoDevice1 *iface, ID3D11Resource *resource,
                                                                   ID3D11VideoProcessorEnumerator *enumerator,
                                                                   const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *desc,
                                                                   ID3D11VideoProcessorOutputView **out)
{
    return D3dVideoCreateOutputView(device_of(iface), resource, enumerator, desc, out);
}

static const ID3D11VideoDevice1Vtbl video_device_vtbl = {
    .QueryInterface = vd_QueryInterface,
    .AddRef = vd_AddRef,
    .Release = vd_Release,
#include "video_device.inc"
};

void D3dVideoDeviceInit(D3dDevice *device)
{
    device->video.lpVtbl = &video_device_vtbl;
    InitializeSRWLock(&device->video_lock);
}

void D3dVideoDeviceDestroy(D3dDevice *device)
{
    D3dVideoProcessorDestroy(device);
    if (device->video_timeline)
        device->vk.vkDestroySemaphore(device->vk.device, device->video_timeline, nullptr);
    device->video_timeline = VK_NULL_HANDLE;
}

void *D3dVideoDeviceInterface(D3dDevice *device, REFIID riid)
{
    if (!IsEqualGUID(riid, &IID_ID3D11VideoDevice) && !IsEqualGUID(riid, &IID_ID3D11VideoDevice1))
        return nullptr;
    D3dVideoProbe(device);
    return device->video_count ? &device->video : nullptr;
}

/* ------------------------------------------------------------------ the context */

static HRESULT STDMETHODCALLTYPE vc_QueryInterface(ID3D11VideoContext1 *iface, REFIID riid, void **out)
{
    D3dContext *c = context_of(iface);
    return c->iface.lpVtbl->QueryInterface(&c->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE vc_AddRef(ID3D11VideoContext1 *iface)
{
    D3dContext *c = context_of(iface);
    return c->iface.lpVtbl->AddRef(&c->iface);
}

static ULONG STDMETHODCALLTYPE vc_Release(ID3D11VideoContext1 *iface)
{
    D3dContext *c = context_of(iface);
    return c->iface.lpVtbl->Release(&c->iface);
}

static void STDMETHODCALLTYPE vc_GetDevice(ID3D11VideoContext1 *iface, ID3D11Device **device)
{
    D3dContext *c = context_of(iface);
    c->iface.lpVtbl->GetDevice(&c->iface, device);
}

static HRESULT STDMETHODCALLTYPE vc_GetPrivateData(ID3D11VideoContext1 *iface, REFGUID guid, UINT *size, void *data)
{
    D3dContext *c = context_of(iface);
    return c->iface.lpVtbl->GetPrivateData(&c->iface, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE vc_SetPrivateData(ID3D11VideoContext1 *iface, REFGUID guid, UINT size, const void *data)
{
    D3dContext *c = context_of(iface);
    return c->iface.lpVtbl->SetPrivateData(&c->iface, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE vc_SetPrivateDataInterface(ID3D11VideoContext1 *iface, REFGUID guid,
                                                            const IUnknown *data)
{
    D3dContext *c = context_of(iface);
    return c->iface.lpVtbl->SetPrivateDataInterface(&c->iface, guid, data);
}

/* a decoder of this context's device, or null. */
static VideoDecoder *decoder_of(D3dContext *c, ID3D11VideoDecoder *iface)
{
    if (!iface)
        return nullptr;
    VideoDecoder *d = decoder_impl(iface);
    return d->child.device == c->child.device ? d : nullptr;
}

static BOOL buffer_type_ok(UINT type) { return type < BUFFER_TYPES && BUFFER_SIZES[type]; }

static HRESULT STDMETHODCALLTYPE vc_GetDecoderBuffer(ID3D11VideoContext1 *iface, ID3D11VideoDecoder *decoder,
                                                     D3D11_VIDEO_DECODER_BUFFER_TYPE type, UINT *size, void **buffer)
{
    VideoDecoder *d = decoder_of(context_of(iface), decoder);
    if (!d || !size || !buffer || !buffer_type_ok(type))
        return E_INVALIDARG;
    if (d->buffers[type].held)
        return DXGI_ERROR_INVALID_CALL;
    if (!d->buffers[type].data) {
        d->buffers[type].data = VirtualAlloc(nullptr, BUFFER_SIZES[type], MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!d->buffers[type].data)
            return E_OUTOFMEMORY;
    }
    d->buffers[type].held = TRUE;
    *size = BUFFER_SIZES[type];
    *buffer = d->buffers[type].data;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vc_ReleaseDecoderBuffer(ID3D11VideoContext1 *iface, ID3D11VideoDecoder *decoder,
                                                         D3D11_VIDEO_DECODER_BUFFER_TYPE type)
{
    VideoDecoder *d = decoder_of(context_of(iface), decoder);
    if (!d || !buffer_type_ok(type))
        return E_INVALIDARG;
    if (!d->buffers[type].held)
        return DXGI_ERROR_INVALID_CALL;
    d->buffers[type].held = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vc_DecoderBeginFrame(ID3D11VideoContext1 *iface, ID3D11VideoDecoder *decoder,
                                                      ID3D11VideoDecoderOutputView *view, UINT key_size,
                                                      const void *key)
{
    D3dContext *c = context_of(iface);
    VideoDecoder *d = decoder_of(c, decoder);
    if (!d || !view || d->in_frame)
        return E_INVALIDARG;
    VideoView *v = view_impl(view);
    if (v->child.device != c->child.device || !IsEqualGUID(&v->desc.DecodeProfile, &d->desc.Guid))
        return E_INVALIDARG;
    if (d->view)
        d->view->iface.lpVtbl->Release(&d->view->iface);
    d->view = v;
    v->iface.lpVtbl->AddRef(&v->iface);
    d->params_size = d->qmatrix_size = d->slice_count = d->bits_size = 0;
    d->in_frame = TRUE;
    return S_OK;
}

/* room for more bytes in a growing buffer. */
static BOOL grow(BYTE **data, UINT *capacity, UINT need)
{
    if (need <= *capacity)
        return TRUE;
    UINT size = *capacity ? *capacity : 4096;
    while (size < need)
        size *= 2;
    BYTE *bytes = *data ? HeapReAlloc(GetProcessHeap(), 0, *data, size) : HeapAlloc(GetProcessHeap(), 0, size);
    if (!bytes)
        return FALSE;
    *data = bytes;
    *capacity = size;
    return TRUE;
}

/* a copy of a submitted buffer's data, which the program may fill again. */
static HRESULT keep(BYTE **data, UINT *size, UINT *capacity, const BYTE *from, UINT n)
{
    if (!grow(data, capacity, n ? n : 1))
        return E_OUTOFMEMORY;
    memcpy(*data, from, n);
    *size = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vc_SubmitDecoderBuffers(ID3D11VideoContext1 *iface, ID3D11VideoDecoder *decoder,
                                                         UINT count, const D3D11_VIDEO_DECODER_BUFFER_DESC *descs)
{
    D3dContext *c = context_of(iface);
    VideoDecoder *d = decoder_of(c, decoder);
    if (!d || !count || !descs || !d->in_frame)
        return E_INVALIDARG;
    for (UINT i = 0; i < count; i++) {
        UINT type = descs[i].BufferType;
        if (!buffer_type_ok(type) || !d->buffers[type].data || descs[i].DataOffset > BUFFER_SIZES[type] ||
            descs[i].DataSize > BUFFER_SIZES[type] - descs[i].DataOffset)
            return E_INVALIDARG;
    }
    /* the bitstream first: the slice entries that come with it are located in it. */
    UINT base = d->bits_size, offset = 0;
    BOOL has_bits = FALSE;
    for (UINT i = 0; i < count; i++) {
        if (descs[i].BufferType != BUFFER_BITSTREAM)
            continue;
        UINT size = descs[i].DataSize;
        if (size > ~0u - d->bits_size || d->bits_size + size > MVK_MAX_BITSTREAM)
            return E_INVALIDARG;
        if (!grow(&d->bits, &d->bits_capacity, d->bits_size + size))
            return E_OUTOFMEMORY;
        memcpy(d->bits + d->bits_size, d->buffers[BUFFER_BITSTREAM].data + descs[i].DataOffset, size);
        base = d->bits_size;
        offset = descs[i].DataOffset;
        d->bits_size += size;
        has_bits = TRUE;
    }
    for (UINT i = 0; i < count; i++) {
        const BYTE *data = d->buffers[descs[i].BufferType].data ? d->buffers[descs[i].BufferType].data + descs[i].DataOffset
                                                                  : nullptr;
        UINT size = descs[i].DataSize;
        HRESULT hr = S_OK;
        switch (descs[i].BufferType) {
        case BUFFER_PARAMS:
            hr = keep(&d->params, &d->params_size, &d->params_capacity, data, size);
            break;
        case BUFFER_QMATRIX:
            hr = keep(&d->qmatrix, &d->qmatrix_size, &d->qmatrix_capacity, data, size);
            break;
        case BUFFER_SLICES:
            if (!has_bits)
                return E_INVALIDARG;
            for (UINT at = 0; at + sizeof(SliceEntry) <= size; at += sizeof(SliceEntry)) {
                SliceEntry e;
                memcpy(&e, data + at, sizeof(e));
                if (e.location < offset || e.location - offset > ~0u - base)
                    return E_INVALIDARG;
                e.location = e.location - offset + base;
                /* the rest of a slice cut between buffers, which sits right behind its start in what was joined. */
                if ((e.chopping & 2) && d->slice_count) {
                    d->slices[d->slice_count - 1].bytes += e.bytes;
                    d->slices[d->slice_count - 1].chopping = e.chopping & 1;
                    continue;
                }
                if (d->slice_count == d->slice_capacity) {
                    UINT bytes = d->slice_capacity * sizeof(SliceEntry);
                    UINT capacity = bytes;
                    BYTE *list = (BYTE *)d->slices;
                    if (!grow(&list, &capacity, bytes + 16 * sizeof(SliceEntry)))
                        return E_OUTOFMEMORY;
                    d->slices = (SliceEntry *)list;
                    d->slice_capacity = capacity / sizeof(SliceEntry);
                }
                d->slices[d->slice_count++] = e;
            }
            break;
        default:
            break;
        }
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* the picture, decoded by the host once the graphics work submitted before it is done, which its texture may be in. */
static HRESULT STDMETHODCALLTYPE vc_DecoderEndFrame(ID3D11VideoContext1 *iface, ID3D11VideoDecoder *decoder)
{
    D3dContext *c = context_of(iface);
    VideoDecoder *d = decoder_of(c, decoder);
    if (!d || !d->in_frame)
        return E_INVALIDARG;
    d->in_frame = FALSE;
    D3dDevice *device = c->child.device;
    VideoView *v = d->view;
    d->view = nullptr;
    HRESULT hr = S_OK;
    if (!d->params_size || !d->bits_size || !d->slice_count) {
        D3dLog("d3d11: a video frame ended without its buffers");
        hr = E_INVALIDARG;
    } else {
        if (!device->video_timeline) {
            VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
            if (device->vk.vkCreateSemaphore(device->vk.device, &sem, nullptr, &device->video_timeline) != VK_SUCCESS)
                hr = E_FAIL;
        }
        if (SUCCEEDED(hr)) {
            /* what the device itself recorded, as the layouts of new textures, goes first as well. */
            D3dContextSubmit(c, nullptr, nullptr);
            MvkVideoDecodeInfo info = {0};
            info.session = d->session;
            info.picture_view = (UINT64)v->view;
            info.wait_semaphore = (UINT64)device->timeline;
            info.wait_value = device->next_serial - 1;
            info.signal_semaphore = (UINT64)device->video_timeline;
            info.signal_value = ++device->video_value;
            info.params = d->params;
            info.params_size = d->params_size;
            info.qmatrix = d->qmatrix_size ? d->qmatrix : nullptr;
            info.qmatrix_size = d->qmatrix_size;
            info.slices = d->slices;
            info.slices_size = d->slice_count * sizeof(SliceEntry);
            info.slice_count = d->slice_count;
            info.bitstream = d->bits;
            info.bitstream_size = d->bits_size;
            if (device->vk.vkMimeVideoDecode(device->vk.device, &info, (UINT64)v->resource->tex.image,
                                             v->desc.Texture2D.ArraySlice) != VK_SUCCESS) {
                D3dLog("d3d11: a video frame was not sent to the host");
                hr = E_FAIL;
            }
            D3dContextUse(c, v->resource);
        }
    }
    v->iface.lpVtbl->Release(&v->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE vc_DecoderExtension(ID3D11VideoContext1 *iface, ID3D11VideoDecoder *decoder,
                                                     const D3D11_VIDEO_DECODER_EXTENSION *extension)
{
    VideoDecoder *d = decoder_of(context_of(iface), decoder);
    if (!d || !extension)
        return E_INVALIDARG;
    /* the functions a driver offers through here, status reports among them, are its own: none is offered. */
    return E_INVALIDARG;
}

static const ID3D11VideoContext1Vtbl video_context_vtbl = {
    .QueryInterface = vc_QueryInterface,
    .AddRef = vc_AddRef,
    .Release = vc_Release,
    .GetDevice = vc_GetDevice,
    .GetPrivateData = vc_GetPrivateData,
    .SetPrivateData = vc_SetPrivateData,
    .SetPrivateDataInterface = vc_SetPrivateDataInterface,
#include "video_context.inc"
};

void D3dVideoContextInit(D3dContext *context) { context->video.lpVtbl = &video_context_vtbl; }

void *D3dVideoContextInterface(D3dContext *c, REFIID riid)
{
    if (c->type != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
        (!IsEqualGUID(riid, &IID_ID3D11VideoContext) && !IsEqualGUID(riid, &IID_ID3D11VideoContext1)))
        return nullptr;
    D3dVideoProbe(c->child.device);
    return c->child.device->video_count ? &c->video : nullptr;
}
