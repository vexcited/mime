/*
 * the jpeg codec, over the jpeg library: gray, bgr and cmyk pictures, a decode at a fraction of the size, the
 * thumbnail of the exif block and the color profile.
 */
#include "wic_codec.h"
#include "codec_util.h"
#include "meta_codecs.h"

/* ------------------------------------------------------------------ the decoder */

typedef struct JpegState {
    const BYTE *data;
    SIZE_T size;
    MimeJpegInfo info;
    GUID format;
    BOOL profile_tried;
    BYTE *icc;
    size_t icc_size;
    UINT exif_space; /* 0 none, else the value for the exif color space context. */
} JpegState;

static HRESULT jpeg_error(int code)
{
    switch (code) {
    case MIME_JPEG_ERR_NOMEM:
        return E_OUTOFMEMORY;
    case MIME_JPEG_ERR_UNSUPPORTED:
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    case MIME_JPEG_ERR_TOO_LARGE:
        return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    case MIME_JPEG_ERR_PARAM:
        return E_INVALIDARG;
    case MIME_JPEG_ERR_FORMAT:
        return WINCODEC_ERR_BADHEADER;
    default:
        return WINCODEC_ERR_BADIMAGE;
    }
}

static HRESULT jpeg_open(const BYTE *data, SIZE_T size, void **out)
{
    if (!mime_jpeg_is_jpeg(data, size))
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    JpegState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->data = data;
    s->size = size;
    int rc = mime_jpeg_info(data, size, 0, &s->info);
    if (rc != MIME_JPEG_OK) {
        WicFree(s);
        return jpeg_error(rc);
    }
    s->format = s->info.components == 1 ? GUID_WICPixelFormat8bppGray
                : s->info.components == 4 ? GUID_WICPixelFormat32bppCMYK : GUID_WICPixelFormat24bppBGR;
    *out = s;
    return S_OK;
}

static void jpeg_close(void *state)
{
    JpegState *s = state;
    WicFree(s->icc);
    WicFree(s);
}

static UINT jpeg_frame_count(void *state) { return 1; }

static HRESULT jpeg_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    JpegState *s = state;
    info->width = s->info.width;
    info->height = s->info.height;
    info->format = s->format;
    info->dpi_x = info->dpi_y = 96.0;
    if (s->info.jfif && s->info.density_unit == 1) {
        info->dpi_x = (double)(float)s->info.x_density;
        info->dpi_y = (double)(float)s->info.y_density;
    } else if (s->info.jfif && s->info.density_unit == 2) {
        info->dpi_x = WicDpiFromCentimeter(s->info.x_density);
        info->dpi_y = WicDpiFromCentimeter(s->info.y_density);
    }
    return S_OK;
}

static HRESULT jpeg_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    JpegState *s = state;
    UINT bpp = s->info.components == 1 ? 1 : s->info.components == 4 ? 4 : 3;
    int rc = mime_jpeg_decode(s->data, s->size, &WicAllocator, MIME_JPEG_TOLERANT, dest, stride,
                              (size_t)stride * s->info.height, nullptr);
    (void)bpp;
    /* damaged entropy data leaves a neutral picture where the data ends, which windows shows as it is. */
    if (rc == MIME_JPEG_OK || rc == MIME_JPEG_TRUNCATED)
        return S_OK;
    return jpeg_error(rc);
}

static HRESULT jpeg_decode_scaled(void *state, UINT index, UINT scale, UINT *width, UINT *height, BYTE *dest,
                                  UINT stride, UINT dest_size)
{
    JpegState *s = state;
    unsigned flags = MIME_JPEG_TOLERANT | (scale == 2 ? MIME_JPEG_SCALE_HALF : scale == 4 ? MIME_JPEG_SCALE_QUARTER
                                                                                    : MIME_JPEG_SCALE_EIGHTH);
    MimeJpegInfo info;
    int rc = mime_jpeg_info(s->data, s->size, flags, &info);
    if (rc != MIME_JPEG_OK)
        return jpeg_error(rc);
    if (info.out_width != *width || info.out_height != *height)
        return WINCODEC_ERR_INVALIDPARAMETER;
    rc = mime_jpeg_decode(s->data, s->size, &WicAllocator, flags, dest, stride, dest_size, nullptr);
    return rc == MIME_JPEG_OK || rc == MIME_JPEG_TRUNCATED ? S_OK : jpeg_error(rc);
}

/* the exif block of the file, if it has one. */
static BOOL jpeg_exif(JpegState *s, MimeTiff *tiff)
{
    const uint8_t *payload;
    size_t length;
    if (mime_jpeg_find_exif(s->data, s->size, &payload, &length) != MIME_JPEG_OK)
        return FALSE;
    return mime_tiff_open_exif(tiff, payload, length) == 0;
}

static void jpeg_load_profile(JpegState *s)
{
    if (s->profile_tried)
        return;
    s->profile_tried = TRUE;
    if (mime_jpeg_get_icc(s->data, s->size, &WicAllocator, &s->icc, &s->icc_size) != MIME_JPEG_OK) {
        s->icc = nullptr;
        MimeTiff tiff;
        MimeTiffIfd ifd, exif, interop;
        if (jpeg_exif(s, &tiff) && mime_tiff_ifd_index(&tiff, 0, &ifd) == 0 &&
            mime_tiff_ifd_sub(&ifd, 0x8769, 0, &exif) == 0) {
            uint64_t space = 0;
            if (mime_tiff_ifd_uint(&exif, 0xa001, &space) && space == 1) {
                s->exif_space = 1;
            } else if (mime_tiff_ifd_sub(&exif, 0xa005, 0, &interop) == 0) {
                MimeTiffEntry entry;
                char text[8];
                if (mime_tiff_ifd_find(&interop, 1, &entry) == 0 && mime_tiff_entry_copy_string(&entry, text, sizeof(text)) >= 3 &&
                    text[0] == 'R' && text[1] == '0' && text[2] == '3')
                    s->exif_space = 2;
            }
        }
    }
}

static UINT jpeg_profile_count(void *state, UINT index)
{
    JpegState *s = state;
    jpeg_load_profile(s);
    return s->icc || s->exif_space ? 1 : 0;
}

static HRESULT jpeg_profile(void *state, UINT index, UINT n, WicProfile *out)
{
    JpegState *s = state;
    jpeg_load_profile(s);
    if (n || (!s->icc && !s->exif_space))
        return E_INVALIDARG;
    out->exif = s->icc == nullptr;
    out->exif_space = s->exif_space;
    out->data = s->icc;
    out->size = (UINT)s->icc_size;
    return S_OK;
}

static HRESULT jpeg_thumbnail(void *state, UINT index, IWICBitmapSource **out)
{
    JpegState *s = state;
    MimeTiff tiff;
    const uint8_t *thumb;
    size_t length;
    if (!jpeg_exif(s, &tiff) || mime_tiff_thumbnail(&tiff, &thumb, &length) != 0 || !mime_jpeg_is_jpeg(thumb, length))
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    MimeJpegInfo info;
    if (mime_jpeg_info(thumb, length, 0, &info) != MIME_JPEG_OK)
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    const GUID *format = info.components == 1 ? &GUID_WICPixelFormat8bppGray
                         : info.components == 4 ? &GUID_WICPixelFormat32bppCMYK : &GUID_WICPixelFormat24bppBGR;
    IWICBitmap *bitmap;
    HRESULT hr = WicBitmapCreate(info.width, info.height, format, nullptr, 0, 0, FALSE, &bitmap);
    if (FAILED(hr))
        return hr;
    UINT stride;
    BYTE *pixels = WicBitmapPixels(bitmap, &stride);
    int rc = mime_jpeg_decode(thumb, length, &WicAllocator, MIME_JPEG_TOLERANT, pixels, stride,
                              (size_t)stride * info.height, nullptr);
    if (rc != MIME_JPEG_OK && rc != MIME_JPEG_TRUNCATED) {
        bitmap->lpVtbl->Release(bitmap);
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    }
    *out = (IWICBitmapSource *)bitmap;
    return S_OK;
}

static HRESULT jpeg_metadata(void *state, UINT index, IWICMetadataReader ***readers, UINT *count)
{
    JpegState *s = state;
    return WicJpegReaders(s->data, s->size, readers, count);
}

const WicDecoderOps wic_dec_jpeg = {
    .open = jpeg_open,
    .close = jpeg_close,
    .frame_count = jpeg_frame_count,
    .frame_info = jpeg_frame_info,
    .frame_decode = jpeg_frame_decode,
    .profile_count = jpeg_profile_count,
    .profile = jpeg_profile,
    .thumbnail = jpeg_thumbnail,
    .frame_decode_scaled = jpeg_decode_scaled,
    .metadata = jpeg_metadata,
};

/* ------------------------------------------------------------------ the encoder */

static const WicOptionDesc g_jpeg_options[] = {
    {L"ImageQuality", VT_R4, 0.9, 0.0, 1.0},
    {L"SuppressApp0", VT_BOOL, 0, 0, 1},
    {L"JpegYCrCbSubsampling", VT_UI1, 0, 0, 4},
};

typedef struct JpegEncoder {
    IStream *stream;
} JpegEncoder;

static HRESULT jpeg_begin(IStream *stream, void **state)
{
    JpegEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    *state = e;
    return S_OK;
}

static BOOL jpeg_closest(GUID *format)
{
    /* gray goes to gray, cmyk stays, everything else is color. */
    const WicFormat *f = WicFindFormat(format);
    if (!f)
        return FALSE;
    if (f->channels == 1 && f->numeric != WICPixelFormatNumericRepresentationIndexed) {
        *format = GUID_WICPixelFormat8bppGray;
        return TRUE;
    }
    if (IsEqualGUID(format, &GUID_WICPixelFormat64bppCMYK) || IsEqualGUID(format, &GUID_WICPixelFormat40bppCMYKAlpha)) {
        *format = GUID_WICPixelFormat32bppCMYK;
        return TRUE;
    }
    *format = GUID_WICPixelFormat24bppBGR;
    return TRUE;
}

static HRESULT jpeg_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    JpegEncoder *e = state;
    int pixel;
    if (IsEqualGUID(&frame->format, &GUID_WICPixelFormat8bppGray))
        pixel = MIME_JPEG_PIXEL_GRAY8;
    else if (IsEqualGUID(&frame->format, &GUID_WICPixelFormat24bppBGR))
        pixel = MIME_JPEG_PIXEL_BGR24;
    else if (IsEqualGUID(&frame->format, &GUID_WICPixelFormat32bppCMYK))
        pixel = MIME_JPEG_PIXEL_CMYK32;
    else
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    MimeJpegEncodeParams p;
    memset(&p, 0, sizeof(p));
    double quality = (frame->options_set & 1) ? frame->options[0] : 0.9;
    int q = (int)(float)(quality * 100.0);
    p.quality = q < 1 ? 1 : q > 100 ? 100 : q;
    p.flags = MIME_JPEG_ENC_BASELINE;
    if (pixel == MIME_JPEG_PIXEL_CMYK32)
        p.flags |= MIME_JPEG_ENC_YCCK;
    if ((frame->options_set & 2) && frame->options[1] != 0)
        p.flags |= MIME_JPEG_ENC_NO_JFIF;
    static const int subsampling[] = {MIME_JPEG_SUBSAMPLE_420, MIME_JPEG_SUBSAMPLE_420, MIME_JPEG_SUBSAMPLE_422,
                                      MIME_JPEG_SUBSAMPLE_444, MIME_JPEG_SUBSAMPLE_440};
    if (frame->options_set & 4)
        p.subsampling = subsampling[(UINT)frame->options[2] < 5 ? (UINT)frame->options[2] : 0];
    double dx = frame->dpi_x > 0 ? frame->dpi_x : 96.0, dy = frame->dpi_y > 0 ? frame->dpi_y : 96.0;
    p.density_unit = 1;
    p.x_density = (uint16_t)(dx >= 65535.0 ? 65535 : dx < 1.0 ? 1 : (UINT)(dx + 0.5));
    p.y_density = (uint16_t)(dy >= 65535.0 ? 65535 : dy < 1.0 ? 1 : (UINT)(dy + 0.5));
    JpegSegmentList *segments = nullptr;
    WicJpegSegments(frame->metadata, &segments);
    if (segments)
        p.segments = WicJpegSegmentsData(segments, &p.segment_count);
    BYTE *out = nullptr;
    size_t length = 0;
    int rc = mime_jpeg_encode(frame->pixels, frame->width, frame->height, frame->stride, pixel, &p, &WicAllocator,
                              &out, &length);
    WicJpegSegmentsFree(segments);
    if (rc != MIME_JPEG_OK)
        return rc == MIME_JPEG_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    HRESULT hr = WicWriteStream(e->stream, out, length);
    WicFree(out);
    return hr;
}

static HRESULT jpeg_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                           IWICMetadataBlockReader *metadata) { return S_OK; }
static void jpeg_destroy(void *state) { WicFree(state); }

const WicEncoderOps wic_enc_jpeg = {
    .options = g_jpeg_options,
    .option_count = WIC_ARRAY_SIZE(g_jpeg_options),
    .closest_format = jpeg_closest,
    .begin = jpeg_begin,
    .frame_metadata = TRUE,
    .write_frame = jpeg_write_frame,
    .finish = jpeg_finish,
    .destroy = jpeg_destroy,
};
