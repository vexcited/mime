/*
 * the dds codec: the images of a texture, one frame for each array element, mip level and depth slice, in 32 bit
 * color. The encoder writes a single uncompressed image.
 */
#include "wic_codec.h"
#include <bcn/bcn.h>

typedef struct DdsState {
    const BYTE *data;
    SIZE_T size;
    MimeDdsInfo info;
    UINT per_element; /* frames in one array element. */
    UINT elements;    /* array elements, a cube counts its faces. */
} DdsState;

static UINT depth_at(const MimeDdsInfo *info, UINT mip)
{
    if (!info->volume)
        return 1;
    UINT d = info->depth >> mip;
    return d ? d : 1;
}

static HRESULT dds_open(const BYTE *data, SIZE_T size, void **out)
{
    if (!mime_dds_is_dds(data, size))
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    DdsState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->data = data;
    s->size = size;
    int rc = mime_dds_parse(data, size, &s->info);
    if (rc != MIME_BCN_OK) {
        WicFree(s);
        return rc == MIME_BCN_ERR_UNSUPPORTED ? WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT : WINCODEC_ERR_BADHEADER;
    }
    if (!s->info.complete) {
        WicFree(s);
        return WINCODEC_ERR_BADIMAGE;
    }
    for (UINT m = 0; m < s->info.mip_count; m++)
        s->per_element += depth_at(&s->info, m);
    s->elements = s->info.array_size * s->info.face_count;
    *out = s;
    return S_OK;
}

static void dds_close(void *state) { WicFree(state); }

static UINT dds_frame_count(void *state)
{
    DdsState *s = state;
    UINT64 n = (UINT64)s->per_element * s->elements;
    return n > 0xffff ? 0xffff : (UINT)n;
}

static void locate(const DdsState *s, UINT index, UINT *slice, UINT *mip, UINT *z)
{
    *slice = index / s->per_element;
    UINT rest = index % s->per_element;
    for (UINT m = 0; m < s->info.mip_count; m++) {
        UINT d = depth_at(&s->info, m);
        if (rest < d) {
            *mip = m;
            *z = rest;
            return;
        }
        rest -= d;
    }
    *mip = 0;
    *z = 0;
}

static HRESULT dds_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    DdsState *s = state;
    UINT slice, mip, z;
    locate(s, index, &slice, &mip, &z);
    info->width = mime_dds_mip_size(s->info.width, mip);
    info->height = mime_dds_mip_size(s->info.height, mip);
    info->format = GUID_WICPixelFormat32bppBGRA;
    info->dpi_x = info->dpi_y = 96.0;
    return S_OK;
}

static HRESULT dds_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    DdsState *s = state;
    UINT slice, mip, z;
    locate(s, index, &slice, &mip, &z);
    int rc = mime_dds_decode(&s->info, s->data, s->size, slice, mip, z, dest, stride, MIME_BCN_UNPREMULTIPLY);
    return rc == MIME_BCN_OK ? S_OK : WINCODEC_ERR_BADIMAGE;
}

const WicDecoderOps wic_dec_dds = {
    .open = dds_open,
    .close = dds_close,
    .frame_count = dds_frame_count,
    .frame_info = dds_frame_info,
    .frame_decode = dds_frame_decode,
};

/* ------------------------------------------------------------------ the encoder */

typedef struct DdsEncoder {
    IStream *stream;
} DdsEncoder;

static HRESULT dds_begin(IStream *stream, void **state)
{
    DdsEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    *state = e;
    return S_OK;
}

static HRESULT dds_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    DdsEncoder *e = state;
    uint8_t *out = nullptr;
    size_t length = 0;
    int rc = mime_dds_write(frame->pixels, frame->stride, frame->width, frame->height, MIME_DDS_WRITE_BGRA8,
                            MIME_DDS_WRITE_DX10, &WicAllocator, &out, &length);
    if (rc != MIME_BCN_OK)
        return rc == MIME_BCN_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    HRESULT hr = WicWriteStream(e->stream, out, length);
    WicFree(out);
    return hr;
}

static HRESULT dds_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                          IWICMetadataBlockReader *metadata)
{
    return S_OK;
}

static void dds_destroy(void *state) { WicFree(state); }

const WicEncoderOps wic_enc_dds = {
    .begin = dds_begin,
    .write_frame = dds_write_frame,
    .finish = dds_finish,
    .destroy = dds_destroy,
};
