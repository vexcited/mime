/*
 * the generic decoder: IWICBitmapDecoder and IWICBitmapFrameDecode over the backend of one image format. The file is read
 * into memory, the backend parses it, and a frame is decoded whole when its pixels are first asked for.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"

#define MAX_FILE 0x60000000u

typedef struct FrameCache {
    BYTE *pixels;
    UINT stride;
    int state; /* 0 not decoded, 1 decoded, -1 failed. */
    HRESULT error;
} FrameCache;

typedef struct WicDecoder {
    WicBase base;
    WicSlot decoder;
    CRITICAL_SECTION lock;
    const WicCodec *codec;
    const WicDecoderOps *ops;
    void *state;
    BYTE *data;
    SIZE_T size;
    BOOL ready;
    UINT frame_count;
    FrameCache *cache;
    WICDecodeOptions options;
} WicDecoder;

typedef struct WicFrame {
    WicBase base;
    WicSlot frame;
    WicSlot blocks;
    WicSlot transform;
    WicDecoder *decoder;
    UINT index;
    WicFrameInfo info;
} WicFrame;

static WicDecoder *decoder_from(IWICBitmapDecoder *This) { return WIC_FROM_SLOT(WicDecoder, decoder, This); }
static WicFrame *frame_from(IWICBitmapFrameDecode *This) { return WIC_FROM_SLOT(WicFrame, frame, This); }

/* ------------------------------------------------------------------ decoding a frame */

static HRESULT frame_cache(WicDecoder *d, UINT index, const WicFrameInfo *info, BYTE **pixels, UINT *stride)
{
    EnterCriticalSection(&d->lock);
    FrameCache *c = &d->cache[index];
    if (c->state == 0) {
        UINT row;
        UINT total;
        if (!WicRowBytes(&info->format, info->width, &row) || !WicMul32(row, info->height, &total)) {
            c->state = -1;
            c->error = INTSAFE_E_ARITHMETIC_OVERFLOW;
        } else {
            c->pixels = WicAllocZero(total ? total : 1);
            c->stride = row;
            if (!c->pixels) {
                c->state = -1;
                c->error = E_OUTOFMEMORY;
            } else {
                HRESULT hr = d->ops->frame_decode(d->state, index, c->pixels, row);
                if (FAILED(hr)) {
                    WicFree(c->pixels);
                    c->pixels = nullptr;
                    c->state = -1;
                    c->error = hr;
                } else {
                    c->state = 1;
                }
            }
        }
    }
    HRESULT hr = c->state > 0 ? S_OK : c->error;
    *pixels = c->pixels;
    *stride = c->stride;
    LeaveCriticalSection(&d->lock);
    return hr;
}

/* ------------------------------------------------------------------ the frame */

static void frame_destroy(WicBase *base)
{
    WicFrame *f = (WicFrame *)base;
    if (f->decoder)
        WicRelease(&f->decoder->decoder);
}

static HRESULT STDMETHODCALLTYPE frame_get_size(IWICBitmapFrameDecode *This, UINT *width, UINT *height)
{
    WicFrame *f = frame_from(This);
    if (!width || !height)
        return E_INVALIDARG;
    *width = f->info.width;
    *height = f->info.height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_get_format(IWICBitmapFrameDecode *This, WICPixelFormatGUID *format)
{
    WicFrame *f = frame_from(This);
    if (!format)
        return E_INVALIDARG;
    *format = f->info.format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_get_resolution(IWICBitmapFrameDecode *This, double *dpi_x, double *dpi_y)
{
    WicFrame *f = frame_from(This);
    if (!dpi_x || !dpi_y)
        return E_INVALIDARG;
    *dpi_x = f->info.dpi_x;
    *dpi_y = f->info.dpi_y;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_copy_palette(IWICBitmapFrameDecode *This, IWICPalette *palette)
{
    WicFrame *f = frame_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!f->info.has_palette || !f->info.palette_count)
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    return WicPaletteSet(palette, f->info.palette, f->info.palette_count);
}

static HRESULT STDMETHODCALLTYPE frame_copy_pixels(IWICBitmapFrameDecode *This, const WICRect *rect, UINT stride,
                                                   UINT size, BYTE *buffer)
{
    WicFrame *f = frame_from(This);
    WICRect r;
    if (!buffer)
        return E_INVALIDARG;
    HRESULT hr = WicCheckRect(rect, f->info.width, f->info.height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &f->info.format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    BYTE *pixels;
    UINT src_stride;
    hr = frame_cache(f->decoder, f->index, &f->info, &pixels, &src_stride);
    if (FAILED(hr))
        return hr;
    WicCopyBits(buffer, stride, pixels, src_stride, (UINT)r.X, (UINT)r.Y, (UINT)r.Width, (UINT)r.Height,
                WicFormatBits(&f->info.format));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_get_metadata(IWICBitmapFrameDecode *This, IWICMetadataQueryReader **query)
{
    WicFrame *f = frame_from(This);
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    IWICMetadataBlockReader *blocks = (IWICMetadataBlockReader *)&f->blocks;
    HRESULT hr = S_OK;
    if (FAILED(hr))
        return hr;
    return WicQueryFromBlocks(blocks, query);
}

static HRESULT STDMETHODCALLTYPE frame_get_contexts(IWICBitmapFrameDecode *This, UINT count,
                                                    IWICColorContext **contexts, UINT *actual)
{
    WicFrame *f = frame_from(This);
    WicDecoder *d = f->decoder;
    if (!actual)
        return E_INVALIDARG;
    UINT total = d->ops->profile_count ? d->ops->profile_count(d->state, f->index) : 0;
    *actual = total;
    if (!contexts)
        return S_OK;
    if (count < total)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    for (UINT i = 0; i < total; i++) {
        WicProfile profile;
        HRESULT hr = d->ops->profile(d->state, f->index, i, &profile);
        if (FAILED(hr))
            return hr;
        if (!contexts[i])
            return E_INVALIDARG;
        hr = profile.exif ? contexts[i]->lpVtbl->InitializeFromExifColorSpace(contexts[i], profile.exif_space)
                          : contexts[i]->lpVtbl->InitializeFromMemory(contexts[i], profile.data, profile.size);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_get_thumbnail(IWICBitmapFrameDecode *This, IWICBitmapSource **thumbnail)
{
    WicFrame *f = frame_from(This);
    WicDecoder *d = f->decoder;
    if (!thumbnail)
        return E_INVALIDARG;
    *thumbnail = nullptr;
    if (!d->ops->thumbnail)
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    return d->ops->thumbnail(d->state, f->index, thumbnail);
}

static const IWICBitmapFrameDecodeVtbl g_frame_vtbl = {
    WIC_IUNKNOWN_FUNCS, frame_get_size, frame_get_format, frame_get_resolution, frame_copy_palette, frame_copy_pixels,
    frame_get_metadata, frame_get_contexts, frame_get_thumbnail,
};

/* the metadata block reader of a frame. */

static WicFrame *frame_of_blocks(void *This) { return WIC_FROM_SLOT(WicFrame, blocks, This); }

static HRESULT frame_blocks_reader(WicFrame *f, IWICMetadataBlockReader **reader)
{
    WicDecoder *d = f->decoder;
    *reader = nullptr;
    if (!d->ops->metadata)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    IWICMetadataReader **readers = nullptr;
    UINT count = 0;
    HRESULT hr = d->ops->metadata(d->state, f->index, &readers, &count);
    if (FAILED(hr))
        return hr;
    hr = WicBlockReaderCreate(d->codec->container, readers, count, reader);
    for (UINT i = 0; i < count; i++)
        readers[i]->lpVtbl->Release(readers[i]);
    WicFree(readers);
    return hr;
}

static HRESULT STDMETHODCALLTYPE blocks_get_container(IWICMetadataBlockReader *This, GUID *container)
{
    WicFrame *f = frame_of_blocks(This);
    if (!container)
        return E_INVALIDARG;
    *container = *f->decoder->codec->container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE blocks_get_count(IWICMetadataBlockReader *This, UINT *count)
{
    WicFrame *f = frame_of_blocks(This);
    IWICMetadataBlockReader *reader;
    if (!count)
        return E_INVALIDARG;
    HRESULT hr = frame_blocks_reader(f, &reader);
    if (FAILED(hr))
        return hr;
    hr = reader->lpVtbl->GetCount(reader, count);
    reader->lpVtbl->Release(reader);
    return hr;
}

static HRESULT STDMETHODCALLTYPE blocks_get_reader(IWICMetadataBlockReader *This, UINT index,
                                                   IWICMetadataReader **out)
{
    WicFrame *f = frame_of_blocks(This);
    IWICMetadataBlockReader *reader;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = frame_blocks_reader(f, &reader);
    if (FAILED(hr))
        return hr;
    hr = reader->lpVtbl->GetReaderByIndex(reader, index, out);
    reader->lpVtbl->Release(reader);
    return hr;
}

static HRESULT STDMETHODCALLTYPE blocks_get_enumerator(IWICMetadataBlockReader *This, IEnumUnknown **out)
{
    WicFrame *f = frame_of_blocks(This);
    IWICMetadataBlockReader *reader;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = frame_blocks_reader(f, &reader);
    if (FAILED(hr))
        return hr;
    hr = reader->lpVtbl->GetEnumerator(reader, out);
    reader->lpVtbl->Release(reader);
    return hr;
}

static const IWICMetadataBlockReaderVtbl g_blocks_vtbl = {
    WIC_IUNKNOWN_FUNCS, blocks_get_container, blocks_get_count, blocks_get_reader, blocks_get_enumerator,
};

/* the source transform of a frame that decodes at a fraction of its size. */

static WicFrame *frame_of_transform(void *This) { return WIC_FROM_SLOT(WicFrame, transform, This); }

static HRESULT STDMETHODCALLTYPE transform_copy_pixels(IWICBitmapSourceTransform *This, const WICRect *rect,
                                                       UINT width, UINT height, WICPixelFormatGUID *format,
                                                       WICBitmapTransformOptions options, UINT stride, UINT size,
                                                       BYTE *buffer)
{
    WicFrame *f = frame_of_transform(This);
    WicDecoder *d = f->decoder;
    if (!buffer || !width || !height)
        return E_INVALIDARG;
    if (options != WICBitmapTransformRotate0)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (format && !IsEqualGUID(format, &f->info.format))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    UINT scale = 1;
    while (scale < 8 && (f->info.width / (scale * 2) >= width || f->info.height / (scale * 2) >= height) &&
           (f->info.width + scale * 2 - 1) / (scale * 2) >= width && (f->info.height + scale * 2 - 1) / (scale * 2) >= height)
        scale *= 2;
    UINT sw = (f->info.width + scale - 1) / scale, sh = (f->info.height + scale - 1) / scale;
    if (scale == 1 || !d->ops->frame_decode_scaled) {
        if (width != f->info.width || height != f->info.height)
            return WINCODEC_ERR_INVALIDPARAMETER;
        return frame_copy_pixels((IWICBitmapFrameDecode *)&f->frame, rect, stride, size, buffer);
    }
    if (width != sw || height != sh)
        return WINCODEC_ERR_INVALIDPARAMETER;
    WICRect r;
    HRESULT hr = WicCheckRect(rect, sw, sh, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &f->info.format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    UINT row;
    WicRowBytes(&f->info.format, sw, &row);
    BYTE *full = WicAlloc((SIZE_T)row * sh);
    if (!full)
        return E_OUTOFMEMORY;
    UINT ow = sw, oh = sh;
    hr = d->ops->frame_decode_scaled(d->state, f->index, scale, &ow, &oh, full, row, row * sh);
    if (SUCCEEDED(hr))
        WicCopyBits(buffer, stride, full, row, (UINT)r.X, (UINT)r.Y, (UINT)r.Width, (UINT)r.Height,
                    WicFormatBits(&f->info.format));
    WicFree(full);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_closest_size(IWICBitmapSourceTransform *This, UINT *width, UINT *height)
{
    WicFrame *f = frame_of_transform(This);
    if (!width || !height || !*width || !*height)
        return E_INVALIDARG;
    UINT scale = 1;
    if (f->decoder->ops->frame_decode_scaled) {
        while (scale < 8 && (f->info.width + scale * 2 - 1) / (scale * 2) >= *width &&
               (f->info.height + scale * 2 - 1) / (scale * 2) >= *height)
            scale *= 2;
    }
    *width = (f->info.width + scale - 1) / scale;
    *height = (f->info.height + scale - 1) / scale;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_closest_format(IWICBitmapSourceTransform *This, WICPixelFormatGUID *format)
{
    WicFrame *f = frame_of_transform(This);
    if (!format)
        return E_INVALIDARG;
    *format = f->info.format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_does_support(IWICBitmapSourceTransform *This,
                                                        WICBitmapTransformOptions options, BOOL *supported)
{
    if (!supported)
        return E_INVALIDARG;
    *supported = options == WICBitmapTransformRotate0;
    return S_OK;
}

static const IWICBitmapSourceTransformVtbl g_transform_vtbl = {
    WIC_IUNKNOWN_FUNCS, transform_copy_pixels, transform_closest_size, transform_closest_format,
    transform_does_support,
};

static const WicIfaceEntry g_frame_ifaces[] = {
    WIC_ENTRY(WicFrame, IID_IWICBitmapFrameDecode, frame),
    WIC_ENTRY(WicFrame, IID_IWICBitmapSource, frame),
    WIC_ENTRY(WicFrame, IID_IWICMetadataBlockReader, blocks),
    WIC_ENTRY(WicFrame, IID_IWICBitmapSourceTransform, transform),
};
static const WicClass g_frame_class = {frame_destroy, g_frame_ifaces, 4};

/* ------------------------------------------------------------------ the decoder */

static void decoder_destroy(WicBase *base)
{
    WicDecoder *d = (WicDecoder *)base;
    if (d->state)
        d->ops->close(d->state);
    if (d->cache) {
        for (UINT i = 0; i < d->frame_count; i++)
            WicFree(d->cache[i].pixels);
        WicFree(d->cache);
    }
    WicFree(d->data);
    DeleteCriticalSection(&d->lock);
}

static HRESULT decoder_open(WicDecoder *d, IStream *stream)
{
    BYTE *data;
    SIZE_T size;
    HRESULT hr = WicReadStream(stream, &data, &size, MAX_FILE);
    if (FAILED(hr))
        return hr;
    void *state = nullptr;
    hr = d->ops->open(data, size, &state);
    if (FAILED(hr)) {
        WicFree(data);
        return hr;
    }
    d->data = data;
    d->size = size;
    d->state = state;
    d->frame_count = d->ops->frame_count(state);
    d->cache = WicAllocZero(((SIZE_T)d->frame_count + 1) * sizeof(FrameCache));
    if (!d->cache) {
        d->ops->close(state);
        d->state = nullptr;
        WicFree(d->data);
        d->data = nullptr;
        return E_OUTOFMEMORY;
    }
    d->ready = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_query_capability(IWICBitmapDecoder *This, IStream *stream, DWORD *capability)
{
    WicDecoder *d = decoder_from(This);
    if (!stream || !capability)
        return E_INVALIDARG;
    *capability = 0;
    if (d->ready)
        return WINCODEC_ERR_WRONGSTATE;
    LARGE_INTEGER zero;
    ULARGE_INTEGER start;
    zero.QuadPart = 0;
    HRESULT hr = stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &start);
    if (FAILED(hr))
        return hr;
    BYTE *data;
    SIZE_T size;
    hr = WicReadStream(stream, &data, &size, MAX_FILE);
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)start.QuadPart;
    stream->lpVtbl->Seek(stream, at, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr))
        return hr;
    void *state = nullptr;
    if (SUCCEEDED(d->ops->open(data, size, &state))) {
        *capability = WICBitmapDecoderCapabilityCanDecodeAllImages | WICBitmapDecoderCapabilityCanEnumerateMetadata;
        if (d->ops->thumbnail)
            *capability |= WICBitmapDecoderCapabilityCanDecodeThumbnail;
        d->ops->close(state);
    }
    WicFree(data);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_initialize(IWICBitmapDecoder *This, IStream *stream, WICDecodeOptions options)
{
    WicDecoder *d = decoder_from(This);
    if (!stream)
        return E_INVALIDARG;
    EnterCriticalSection(&d->lock);
    HRESULT hr;
    if (d->ready)
        hr = WINCODEC_ERR_WRONGSTATE;
    else
        hr = decoder_open(d, stream);
    d->options = options;
    LeaveCriticalSection(&d->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE decoder_get_container(IWICBitmapDecoder *This, GUID *container)
{
    WicDecoder *d = decoder_from(This);
    if (!container)
        return E_INVALIDARG;
    *container = *d->codec->container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_get_info(IWICBitmapDecoder *This, IWICBitmapDecoderInfo **info)
{
    WicDecoder *d = decoder_from(This);
    if (!info)
        return E_INVALIDARG;
    return WicDecoderInfoCreate(d->codec, info);
}

static HRESULT STDMETHODCALLTYPE decoder_copy_palette(IWICBitmapDecoder *This, IWICPalette *palette)
{
    WicDecoder *d = decoder_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!d->ops->container_palette)
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    WICColor colors[256];
    UINT count = 0;
    HRESULT hr = d->ops->container_palette(d->state, colors, &count);
    if (FAILED(hr))
        return hr;
    if (!count)
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    return WicPaletteSet(palette, colors, count);
}

static HRESULT STDMETHODCALLTYPE decoder_get_metadata(IWICBitmapDecoder *This, IWICMetadataQueryReader **query)
{
    WicDecoder *d = decoder_from(This);
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!d->ops->metadata)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    IWICMetadataReader **readers = nullptr;
    UINT count = 0;
    HRESULT hr = d->ops->metadata(d->state, ~0u, &readers, &count);
    if (FAILED(hr))
        return hr;
    if (!count) {
        WicFree(readers);
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    IWICMetadataBlockReader *reader;
    hr = WicBlockReaderCreate(d->codec->container, readers, count, &reader);
    for (UINT i = 0; i < count; i++)
        readers[i]->lpVtbl->Release(readers[i]);
    WicFree(readers);
    if (FAILED(hr))
        return hr;
    hr = WicQueryFromBlocks(reader, query);
    reader->lpVtbl->Release(reader);
    return hr;
}

static HRESULT STDMETHODCALLTYPE decoder_get_preview(IWICBitmapDecoder *This, IWICBitmapSource **preview)
{
    WicDecoder *d = decoder_from(This);
    if (!preview)
        return E_INVALIDARG;
    *preview = nullptr;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT STDMETHODCALLTYPE decoder_get_contexts(IWICBitmapDecoder *This, UINT count, IWICColorContext **contexts,
                                                      UINT *actual)
{
    WicDecoder *d = decoder_from(This);
    if (!actual)
        return E_INVALIDARG;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    /* the color contexts of a container are those of the first frame, for the codecs that have any. */
    if (!d->ops->profile_count)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    UINT total = d->ops->profile_count(d->state, ~0u);
    *actual = total;
    if (!contexts)
        return S_OK;
    if (count < total)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    for (UINT i = 0; i < total; i++) {
        WicProfile profile;
        HRESULT hr = d->ops->profile(d->state, ~0u, i, &profile);
        if (FAILED(hr))
            return hr;
        hr = profile.exif ? contexts[i]->lpVtbl->InitializeFromExifColorSpace(contexts[i], profile.exif_space)
                          : contexts[i]->lpVtbl->InitializeFromMemory(contexts[i], profile.data, profile.size);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_get_thumbnail(IWICBitmapDecoder *This, IWICBitmapSource **thumbnail)
{
    WicDecoder *d = decoder_from(This);
    if (!thumbnail)
        return E_INVALIDARG;
    *thumbnail = nullptr;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    return WINCODEC_ERR_CODECNOTHUMBNAIL;
}

static HRESULT STDMETHODCALLTYPE decoder_get_frame_count(IWICBitmapDecoder *This, UINT *count)
{
    WicDecoder *d = decoder_from(This);
    if (!count)
        return E_INVALIDARG;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    *count = d->frame_count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_get_frame(IWICBitmapDecoder *This, UINT index, IWICBitmapFrameDecode **out)
{
    WicDecoder *d = decoder_from(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!d->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (index >= d->frame_count)
        return WINCODEC_ERR_FRAMEMISSING;
    WicFrameInfo info;
    memset(&info, 0, sizeof(info));
    HRESULT hr = d->ops->frame_info(d->state, index, &info);
    if (FAILED(hr))
        return hr;
    WicFrame *f = WicNew(sizeof(*f), &g_frame_class);
    if (!f)
        return E_OUTOFMEMORY;
    WIC_SLOT(f, frame, &g_frame_vtbl);
    WIC_SLOT(f, blocks, &g_blocks_vtbl);
    WIC_SLOT(f, transform, &g_transform_vtbl);
    WicAddRef(&d->decoder);
    f->decoder = d;
    f->index = index;
    f->info = info;
    *out = (IWICBitmapFrameDecode *)&f->frame;
    return S_OK;
}

static const IWICBitmapDecoderVtbl g_decoder_vtbl = {
    WIC_IUNKNOWN_FUNCS, decoder_query_capability, decoder_initialize, decoder_get_container, decoder_get_info,
    decoder_copy_palette, decoder_get_metadata, decoder_get_preview, decoder_get_contexts, decoder_get_thumbnail,
    decoder_get_frame_count, decoder_get_frame,
};

static const WicIfaceEntry g_decoder_ifaces[] = {WIC_ENTRY(WicDecoder, IID_IWICBitmapDecoder, decoder)};
static const WicClass g_decoder_class = {decoder_destroy, g_decoder_ifaces, 1};

HRESULT WicDecoderCreate(const WicCodec *codec, IWICBitmapDecoder **out)
{
    WicDecoder *d = WicNew(sizeof(*d), &g_decoder_class);
    if (!d)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&d->lock);
    WIC_SLOT(d, decoder, &g_decoder_vtbl);
    d->codec = codec;
    d->ops = codec->ops;
    *out = (IWICBitmapDecoder *)&d->decoder;
    return S_OK;
}

/* the decoder for the content of the stream, which has been opened. */
HRESULT WicDecoderFromStream(IStream *stream, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out)
{
    *out = nullptr;
    if (vendor && !IsEqualGUID(vendor, &GUID_VendorMicrosoft) && !IsEqualGUID(vendor, &GUID_NULL))
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    LARGE_INTEGER zero;
    ULARGE_INTEGER start;
    zero.QuadPart = 0;
    HRESULT hr = stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &start);
    if (FAILED(hr))
        return WINCODEC_ERR_STREAMNOTAVAILABLE;
    BYTE head[64];
    ULONG got = 0;
    hr = stream->lpVtbl->Read(stream, head, sizeof(head), &got);
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)start.QuadPart;
    stream->lpVtbl->Seek(stream, at, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr))
        return WINCODEC_ERR_STREAMREAD;
    const WicCodec *codec = WicMatchDecoder(head, got);
    if (!codec)
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    IWICBitmapDecoder *decoder;
    hr = WicDecoderCreate(codec, &decoder);
    if (FAILED(hr))
        return hr;
    hr = decoder->lpVtbl->Initialize(decoder, stream, options);
    if (FAILED(hr)) {
        decoder->lpVtbl->Release(decoder);
        return hr;
    }
    *out = decoder;
    return S_OK;
}

HRESULT WicCreateCodecObject(REFCLSID clsid, REFIID riid, void **object)
{
    const WicCodec *codec = WicFindDecoder(clsid);
    if (codec) {
        IWICBitmapDecoder *decoder;
        HRESULT hr = WicDecoderCreate(codec, &decoder);
        if (FAILED(hr))
            return hr;
        hr = decoder->lpVtbl->QueryInterface(decoder, riid, object);
        decoder->lpVtbl->Release(decoder);
        return hr;
    }
    codec = WicFindEncoder(clsid);
    if (codec) {
        IWICBitmapEncoder *encoder;
        HRESULT hr = WicEncoderCreate(codec, &encoder);
        if (FAILED(hr))
            return hr;
        hr = encoder->lpVtbl->QueryInterface(encoder, riid, object);
        encoder->lpVtbl->Release(encoder);
        return hr;
    }
    const GUID *format = WicFormatOfClass(clsid);
    if (format) {
        IWICMetadataWriter *handler;
        HRESULT hr = WicMetaHandlerCreate(format, &handler);
        if (FAILED(hr))
            return hr;
        hr = handler->lpVtbl->QueryInterface(handler, riid, object);
        handler->lpVtbl->Release(handler);
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
