#include "wic_codec.h"
#include "codec_util.h"
#include "meta_codecs.h"
#include <jxr/jxr.h>

enum {
    WMP_TAG_ICC = 0x8773,
    WMP_TAG_PIXEL_FORMAT = 0xbc01,
    WMP_TAG_TRANSFORM = 0xbc02,
    WMP_TAG_WIDTH = 0xbc80,
    WMP_TAG_HEIGHT = 0xbc81,
    WMP_TAG_DPI_X = 0xbc82,
    WMP_TAG_DPI_Y = 0xbc83,
    WMP_TAG_IMAGE_OFFSET = 0xbcc0,
    WMP_TAG_IMAGE_SIZE = 0xbcc1,
    WMP_TAG_ALPHA_OFFSET = 0xbcc2,
    WMP_TAG_ALPHA_SIZE = 0xbcc3,
    WMP_TAG_IMAGE_BANDS = 0xbcc4,
    WMP_TAG_ALPHA_BANDS = 0xbcc5,
    WMP_TAG_PADDING = 0xea1c,
};

/* the pixel format guids of the container are the wic ones, the last byte names the layout. */
static const BYTE g_wmp_guid_prefix[15] = {0x24, 0xc3, 0xdd, 0x6f, 0x03, 0x4e, 0xfe, 0x4b,
                                           0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9};

static BOOL wmp_layout(const GUID *format, BYTE *layout)
{
    const BYTE *b = (const BYTE *)format;
    if (memcmp(b, g_wmp_guid_prefix, 15) || !mime_jxr_layout(b[15], nullptr, nullptr, nullptr))
        return FALSE;
    *layout = b[15];
    return TRUE;
}

static HRESULT wmp_error(int code)
{
    switch (code) {
    case MIME_JXR_ERR_NOMEM:
        return E_OUTOFMEMORY;
    case MIME_JXR_ERR_UNSUPPORTED:
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    case MIME_JXR_ERR_PARAM:
        return E_INVALIDARG;
    case MIME_JXR_ERR_FORMAT:
        return WINCODEC_ERR_BADHEADER;
    default:
        return WINCODEC_ERR_BADIMAGE;
    }
}

/* the decoder. */

typedef struct WmpState {
    const BYTE *data;
    SIZE_T size;
    MimeTiffIfd ifd;
    GUID format;
    BYTE layout;
    UINT width, height;
    double dpi_x, dpi_y;
    const BYTE *image, *alpha;
    SIZE_T image_size, alpha_size;
    const BYTE *icc;
    UINT icc_size;
} WmpState;

static BOOL wmp_range(const WmpState *s, const MimeTiffIfd *ifd, UINT16 offset_tag, UINT16 size_tag,
                      const BYTE **data, SIZE_T *size)
{
    uint64_t offset, length;
    if (!mime_tiff_ifd_uint(ifd, offset_tag, &offset) || !mime_tiff_ifd_uint(ifd, size_tag, &length))
        return FALSE;
    if (offset >= s->size || length > s->size - offset || !length)
        return FALSE;
    *data = s->data + offset;
    *size = (SIZE_T)length;
    return TRUE;
}

static double wmp_dpi(const MimeTiffIfd *ifd, UINT16 tag)
{
    MimeTiffEntry e;
    if (mime_tiff_ifd_find(ifd, tag, &e) != 0 || e.type != MIME_TIFF_TYPE_FLOAT || e.count != 1 || e.size < 4)
        return 96.0;
    float f;
    memcpy(&f, e.data, 4);
    return f > 0 && f < 1e6f ? f : 96.0;
}

static HRESULT wmp_open(const BYTE *data, SIZE_T size, void **out)
{
    if (size < 8 || data[0] != 'I' || data[1] != 'I' || data[2] != 0xbc || data[3] > 1)
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    WmpState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->data = data;
    s->size = size;
    MimeTiff tiff = {data, size, WicLe32(data + 4), false, false};
    MimeTiffEntry e;
    uint64_t w, h;
    HRESULT hr = WINCODEC_ERR_BADHEADER;
    if (mime_tiff_ifd_at(&tiff, tiff.first_ifd, &s->ifd) != 0 ||
        mime_tiff_ifd_find(&s->ifd, WMP_TAG_PIXEL_FORMAT, &e) != 0 || e.size != 16 ||
        !mime_tiff_ifd_uint(&s->ifd, WMP_TAG_WIDTH, &w) || !mime_tiff_ifd_uint(&s->ifd, WMP_TAG_HEIGHT, &h) || !w ||
        !h || w > 0x10000000u || h > 0x10000000u ||
        !wmp_range(s, &s->ifd, WMP_TAG_IMAGE_OFFSET, WMP_TAG_IMAGE_SIZE, &s->image, &s->image_size))
        goto fail;
    memcpy(&s->format, e.data, 16);
    if (!wmp_layout(&s->format, &s->layout)) {
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        goto fail;
    }
    s->width = (UINT)w;
    s->height = (UINT)h;
    wmp_range(s, &s->ifd, WMP_TAG_ALPHA_OFFSET, WMP_TAG_ALPHA_SIZE, &s->alpha, &s->alpha_size);
    s->dpi_x = wmp_dpi(&s->ifd, WMP_TAG_DPI_X);
    s->dpi_y = wmp_dpi(&s->ifd, WMP_TAG_DPI_Y);
    if (mime_tiff_ifd_find(&s->ifd, WMP_TAG_ICC, &e) == 0 && e.size && e.size < 0x10000000u) {
        s->icc = e.data;
        s->icc_size = (UINT)e.size;
    }
    MimeJxrHeader hd;
    int rc = mime_jxr_header(s->image, s->image_size, &WicAllocator, &hd);
    if (rc != 0) {
        hr = wmp_error(rc);
        goto fail;
    }
    if (hd.width != s->width || hd.height != s->height)
        goto fail;
    *out = s;
    return S_OK;
fail:
    WicFree(s);
    return hr;
}

static void wmp_close(void *state) { WicFree(state); }

static UINT wmp_frame_count(void *state) { return 1; }

static HRESULT wmp_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    WmpState *s = state;
    if (index)
        return WINCODEC_ERR_FRAMEMISSING;
    memset(info, 0, sizeof(*info));
    info->width = s->width;
    info->height = s->height;
    info->format = s->format;
    info->dpi_x = s->dpi_x;
    info->dpi_y = s->dpi_y;
    return S_OK;
}

static HRESULT wmp_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    WmpState *s = state;
    if (index)
        return WINCODEC_ERR_FRAMEMISSING;
    int rc = mime_jxr_decode(s->image, s->image_size, s->alpha, s->alpha_size, s->layout, &WicAllocator, s->width,
                             s->height, dest, stride);
    return rc ? wmp_error(rc) : S_OK;
}

static UINT wmp_profile_count(void *state, UINT index) { return !index && ((WmpState *)state)->icc ? 1 : 0; }

static HRESULT wmp_profile(void *state, UINT index, UINT n, WicProfile *out)
{
    WmpState *s = state;
    if (index || n || !s->icc)
        return E_INVALIDARG;
    memset(out, 0, sizeof(*out));
    out->data = s->icc;
    out->size = s->icc_size;
    return S_OK;
}

static HRESULT wmp_metadata(void *state, UINT index, IWICMetadataReader ***out, UINT *count)
{
    WmpState *s = state;
    *out = nullptr;
    *count = 0;
    if (index == ~0u)
        return S_OK;
    IWICMetadataReader *ifd;
    HRESULT hr = WicIfdFromParsed(&GUID_MetadataFormatIfd, &s->ifd, &ifd);
    if (FAILED(hr))
        return hr;
    IWICMetadataReader **list = WicAlloc(sizeof(*list));
    if (!list) {
        ifd->lpVtbl->Release(ifd);
        return E_OUTOFMEMORY;
    }
    list[0] = ifd;
    *out = list;
    *count = 1;
    return S_OK;
}

const WicDecoderOps wic_dec_wmp = {
    .open = wmp_open,
    .close = wmp_close,
    .frame_count = wmp_frame_count,
    .frame_info = wmp_frame_info,
    .frame_decode = wmp_frame_decode,
    .profile_count = wmp_profile_count,
    .profile = wmp_profile,
    .metadata = wmp_metadata,
};

/* the encoder, with the property bag of windows 11's wmphoto.dll in its order. */

enum {
    OPT_IMAGE_QUALITY,
    OPT_LOSSLESS,
    OPT_TRANSFORM,
    OPT_USE_CODEC_OPTIONS,
    OPT_QUALITY,
    OPT_OVERLAP,
    OPT_SUBSAMPLING,
    OPT_HTILES,
    OPT_VTILES,
    OPT_FREQUENCY,
    OPT_INTERLEAVED_ALPHA,
    OPT_ALPHA_QUALITY,
    OPT_TRANSCODE,
    OPT_IMAGE_DISCARD,
    OPT_ALPHA_DISCARD,
    OPT_IGNORE_OVERLAP,
    OPT_PROGRESSIVE,
    OPT_STREAM_ONLY,
};

static const WicOptionDesc g_wmp_options[] = {
    {L"ImageQuality", VT_R4, 0.9, 0.0, 1.0},
    {L"Lossless", VT_BOOL, 0, 0, 1},
    {L"BitmapTransform", VT_UI1, 0, 0, 255},
    {L"UseCodecOptions", VT_BOOL, 0, 0, 1},
    {L"Quality", VT_UI1, 1, 1, 255},
    {L"Overlap", VT_UI1, 1, 0, 2},
    {L"Subsampling", VT_UI1, 3, 0, 3},
    {L"HorizontalTileSlices", VT_UI2, 0, 0, 4095},
    {L"VerticalTileSlices", VT_UI2, 0, 0, 4095},
    {L"FrequencyOrder", VT_BOOL, 1, 0, 1},
    {L"InterleavedAlpha", VT_BOOL, 0, 0, 1},
    {L"AlphaQuality", VT_UI1, 1, 1, 255},
    {L"CompressedDomainTranscode", VT_BOOL, 1, 0, 1},
    {L"ImageDataDiscard", VT_UI1, 0, 0, 3},
    {L"AlphaDataDiscard", VT_UI1, 0, 0, 4},
    {L"IgnoreOverlap", VT_BOOL, 0, 0, 1},
    {L"ProgressiveMode", VT_BOOL, 0, 0, 1},
    {L"StreamOnly", VT_BOOL, 0, 0, 1},
};

typedef struct WmpEncoder {
    IStream *stream;
    BYTE *file;
    SIZE_T file_size;
} WmpEncoder;

static HRESULT wmp_begin(IStream *stream, void **state)
{
    WmpEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    *state = e;
    return S_OK;
}

/* wic transform options to the codestream's spatial transform. */
static BYTE wmp_transform(UINT t)
{
    switch (t) {
    case WICBitmapTransformFlipVertical:
        return 1;
    case WICBitmapTransformFlipHorizontal:
        return 2;
    case WICBitmapTransformRotate180:
        return 3;
    case WICBitmapTransformRotate90:
        return 4;
    case WICBitmapTransformRotate90 | WICBitmapTransformFlipVertical:
        return 5;
    case WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal:
        return 6;
    case WICBitmapTransformRotate270:
        return 7;
    default:
        return 0;
    }
}

static BOOL wmp_set(const WicEncFrame *f, UINT option) { return (f->options_set >> option) & 1; }

static void wmp_params(const WicEncFrame *f, BYTE layout, MimeJxrEncodeParams *p)
{
    const double *o = f->options;
    unsigned bits, channels;
    bool alpha;
    mime_jxr_layout(layout, &bits, &channels, &alpha);
    BOOL wide = bits / (channels + alpha) > 8;
    p->quality = (uint8_t)o[OPT_QUALITY];
    p->overlap = (uint8_t)o[OPT_OVERLAP];
    p->subsampling = (uint8_t)o[OPT_SUBSAMPLING];
    if (!o[OPT_USE_CODEC_OPTIONS]) {
        /* windows maps the quality to a quantizer, with 4:2:0 and a second overlap level below one half. */
        double q = o[OPT_IMAGE_QUALITY];
        BOOL high = q >= 0.5 || wide;
        p->quality = q >= 1.0 ? 1 : (uint8_t)(2 + (1.0 - q) * 80.0);
        p->subsampling = high ? 3 : 1;
        p->overlap = q >= 1.0 ? 1 : high ? 1 : 2;
    }
    if (o[OPT_LOSSLESS]) {
        p->quality = 1;
        p->subsampling = 3;
    }
    if (o[OPT_IGNORE_OVERLAP])
        p->overlap = 0;
    p->alpha_quality = (uint8_t)o[OPT_ALPHA_QUALITY];
    if (o[OPT_LOSSLESS])
        p->alpha_quality = 1;
    p->bands = (uint8_t)o[OPT_IMAGE_DISCARD];
    p->alpha_bands = o[OPT_ALPHA_DISCARD] > 3 ? 3 : (uint8_t)o[OPT_ALPHA_DISCARD];
    p->spatial_xfrm = wmp_transform((UINT)o[OPT_TRANSFORM]);
    p->frequency_mode = o[OPT_FREQUENCY] != 0;
    p->planar_alpha = !o[OPT_INTERLEAVED_ALPHA];
    UINT h = wmp_set(f, OPT_HTILES) ? (UINT)o[OPT_HTILES] : (f->width - 1) >> 8;
    UINT v = wmp_set(f, OPT_VTILES) ? (UINT)o[OPT_VTILES] : (f->height - 1) >> 8;
    p->tile_cols = (uint16_t)(h + 1);
    p->tile_rows = (uint16_t)(v + 1);
}

/* the color context as icc bytes, or nothing for an exif color space. */
static BYTE *wmp_icc(IWICColorContext *context, UINT *size)
{
    WICColorContextType type;
    *size = 0;
    if (FAILED(context->lpVtbl->GetType(context, &type)) || type != WICColorContextProfile ||
        FAILED(context->lpVtbl->GetProfileBytes(context, 0, nullptr, size)) || !*size)
        return nullptr;
    BYTE *bytes = WicAlloc(*size);
    if (bytes && FAILED(context->lpVtbl->GetProfileBytes(context, *size, bytes, size))) {
        WicFree(bytes);
        bytes = nullptr;
    }
    return bytes;
}

static BOOL wmp_own_tag(UINT16 tag) { return (tag >= 0xbc00 && tag <= 0xbcff) || tag == WMP_TAG_PADDING || tag == WMP_TAG_ICC; }

#define WMP_MAX_ENTRIES 512

static HRESULT wmp_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    WmpEncoder *e = state;
    BYTE layout;
    if (index || e->file)
        return WINCODEC_ERR_WRONGSTATE;
    if (!wmp_layout(&frame->format, &layout))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    MimeJxrEncodeParams p;
    memset(&p, 0, sizeof(p));
    p.width = frame->width;
    p.height = frame->height;
    p.layout = layout;
    p.pixels = frame->pixels;
    p.stride = frame->stride;
    wmp_params(frame, layout, &p);
    unsigned bits, channels;
    bool has_alpha;
    mime_jxr_layout(layout, &bits, &channels, &has_alpha);
    BOOL stream_only = frame->options[OPT_STREAM_ONLY] != 0;
    if (stream_only)
        p.planar_alpha = false;
    uint8_t *image = nullptr, *alpha = nullptr;
    size_t image_size = 0, alpha_size = 0;
    int rc = mime_jxr_encode(&p, &WicAllocator, &image, &image_size, &alpha, &alpha_size);
    if (rc)
        return wmp_error(rc);
    if (stream_only) {
        e->file = image;
        e->file_size = image_size;
        return S_OK;
    }
    HRESULT hr = S_OK;
    void *dir = nullptr;
    size_t dir_size = 0;
    UINT icc_size = 0;
    BYTE *icc = frame->profile_count ? wmp_icc(frame->profiles[0], &icc_size) : nullptr;
    WicIfdSet *set = nullptr;
    IWICMetadataReader *ifd = WicBlocksFind(frame->metadata, &GUID_MetadataFormatIfd);
    if (ifd) {
        if (FAILED(WicIfdSetCreate(ifd, &set)))
            set = nullptr;
        ifd->lpVtbl->Release(ifd);
    }
    MimeTiffWEntry *entries = WicAlloc(WMP_MAX_ENTRIES * sizeof(*entries));
    if (!entries) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    /* the offsets are fixed size, so the directory length is known before they are. */
    uint32_t width = frame->width, height = frame->height, offsets[4] = {0};
    float dpi[2] = {frame->dpi_x > 0 ? (float)frame->dpi_x : 96.0f, frame->dpi_y > 0 ? (float)frame->dpi_y : 96.0f};
    BYTE transform = p.spatial_xfrm, bands[2] = {p.bands, p.alpha_bands};
    UINT n = 0;
    entries[n++] = (MimeTiffWEntry){WMP_TAG_PIXEL_FORMAT, MIME_TIFF_TYPE_BYTE, 16, &frame->format};
    if (transform)
        entries[n++] = (MimeTiffWEntry){WMP_TAG_TRANSFORM, MIME_TIFF_TYPE_BYTE, 1, &transform};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_WIDTH, MIME_TIFF_TYPE_LONG, 1, &width};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_HEIGHT, MIME_TIFF_TYPE_LONG, 1, &height};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_DPI_X, MIME_TIFF_TYPE_FLOAT, 1, &dpi[0]};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_DPI_Y, MIME_TIFF_TYPE_FLOAT, 1, &dpi[1]};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_IMAGE_OFFSET, MIME_TIFF_TYPE_LONG, 1, &offsets[0]};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_IMAGE_SIZE, MIME_TIFF_TYPE_LONG, 1, &offsets[1]};
    entries[n++] = (MimeTiffWEntry){WMP_TAG_IMAGE_BANDS, MIME_TIFF_TYPE_BYTE, 1, &bands[0]};
    if (alpha) {
        entries[n++] = (MimeTiffWEntry){WMP_TAG_ALPHA_OFFSET, MIME_TIFF_TYPE_LONG, 1, &offsets[2]};
        entries[n++] = (MimeTiffWEntry){WMP_TAG_ALPHA_SIZE, MIME_TIFF_TYPE_LONG, 1, &offsets[3]};
        entries[n++] = (MimeTiffWEntry){WMP_TAG_ALPHA_BANDS, MIME_TIFF_TYPE_BYTE, 1, &bands[1]};
    }
    if (icc)
        entries[n++] = (MimeTiffWEntry){WMP_TAG_ICC, MIME_TIFF_TYPE_UNDEFINED, icc_size, icc};
    MimeTiffWEntry subs[2];
    UINT nsub = 0;
    if (set) {
        UINT count;
        const MimeTiffWEntry *extra = WicIfdSetEntries(set, &count);
        for (UINT i = 0; i < count && n < WMP_MAX_ENTRIES - 2; i++)
            if (!wmp_own_tag(extra[i].tag))
                entries[n++] = extra[i];
        if (WicIfdSetExif(set))
            subs[nsub++] = (MimeTiffWEntry){.tag = MIME_TIFF_TAG_EXIF_IFD, .sub = WicIfdSetExif(set)};
        if (WicIfdSetGps(set))
            subs[nsub++] = (MimeTiffWEntry){.tag = MIME_TIFF_TAG_GPS_IFD, .sub = WicIfdSetGps(set)};
        for (UINT i = 0; i < nsub; i++)
            entries[n++] = subs[i];
    }
    MimeTiffWIfd root = {entries, n};
    for (int pass = 0; pass < 2; pass++) {
        mime_free(&WicAllocator, dir);
        dir = nullptr;
        if (mime_tiff_write(&root, 0, &WicAllocator, &dir, &dir_size) != 0) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        size_t start = (dir_size + 1) & ~(size_t)1;
        if (start + image_size + alpha_size > 0xffffffffu) {
            hr = WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
            goto done;
        }
        offsets[0] = (uint32_t)start;
        offsets[1] = (uint32_t)image_size;
        offsets[2] = (uint32_t)(start + image_size);
        offsets[3] = (uint32_t)alpha_size;
    }
    size_t start = offsets[0];
    e->file_size = start + image_size + alpha_size;
    e->file = WicAllocZero(e->file_size);
    if (!e->file) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    memcpy(e->file, dir, dir_size);
    e->file[2] = 0xbc;
    e->file[3] = 0x01;
    memcpy(e->file + start, image, image_size);
    if (alpha)
        memcpy(e->file + start + image_size, alpha, alpha_size);
done:
    mime_free(&WicAllocator, dir);
    WicFree(entries);
    WicIfdSetFree(set);
    WicFree(icc);
    mime_free(&WicAllocator, image);
    mime_free(&WicAllocator, alpha);
    return hr;
}

static HRESULT wmp_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                          IWICMetadataBlockReader *metadata)
{
    WmpEncoder *e = state;
    if (!e->file)
        return WINCODEC_ERR_FRAMEMISSING;
    return WicWriteStream(e->stream, e->file, e->file_size);
}

static void wmp_destroy(void *state)
{
    WmpEncoder *e = state;
    if (e->file)
        mime_free(&WicAllocator, e->file);
    WicFree(e);
}

const WicEncoderOps wic_enc_wmp = {
    .options = g_wmp_options,
    .option_count = WIC_ARRAY_SIZE(g_wmp_options),
    .begin = wmp_begin,
    .write_frame = wmp_write_frame,
    .finish = wmp_finish,
    .destroy = wmp_destroy,
    .frame_metadata = TRUE,
};
