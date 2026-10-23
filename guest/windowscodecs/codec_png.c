/* PNG codec using the library's WIC pixel layouts. */
#include "wic_codec.h"
#include "codec_util.h"
#include "meta_codecs.h"

/* the decoder. */

typedef struct PngState {
    MimePngioDecoder *dec;
    const MimePngioInfo *info;
    GUID format;
    WICColor palette[256];
    UINT palette_count;
    BOOL indexed;
    void *iccp;
    size_t iccp_size;
    BOOL iccp_tried;
    const BYTE *data;
    SIZE_T size;
} PngState;

static HRESULT png_error(int code)
{
    switch (code) {
    case MIME_PNGIO_ERR_NOMEM:
        return E_OUTOFMEMORY;
    case MIME_PNGIO_ERR_UNSUPPORTED:
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    case MIME_PNGIO_ERR_FORMAT:
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    case MIME_PNGIO_ERR_PARAM:
        return E_INVALIDARG;
    case MIME_PNGIO_ERR_CRC:
    case MIME_PNGIO_ERR_DATA:
    default:
        return WINCODEC_ERR_BADIMAGE;
    }
}

static const GUID *png_pixel_format(int format)
{
    switch (format) {
    case MIME_PNGIO_FMT_GRAY1:
        return &GUID_WICPixelFormatBlackWhite;
    case MIME_PNGIO_FMT_GRAY2:
        return &GUID_WICPixelFormat2bppGray;
    case MIME_PNGIO_FMT_GRAY4:
        return &GUID_WICPixelFormat4bppGray;
    case MIME_PNGIO_FMT_GRAY8:
        return &GUID_WICPixelFormat8bppGray;
    case MIME_PNGIO_FMT_GRAY16:
        return &GUID_WICPixelFormat16bppGray;
    case MIME_PNGIO_FMT_INDEXED1:
        return &GUID_WICPixelFormat1bppIndexed;
    case MIME_PNGIO_FMT_INDEXED2:
        return &GUID_WICPixelFormat2bppIndexed;
    case MIME_PNGIO_FMT_INDEXED4:
        return &GUID_WICPixelFormat4bppIndexed;
    case MIME_PNGIO_FMT_INDEXED8:
        return &GUID_WICPixelFormat8bppIndexed;
    case MIME_PNGIO_FMT_BGR24:
        return &GUID_WICPixelFormat24bppBGR;
    case MIME_PNGIO_FMT_RGB48:
        return &GUID_WICPixelFormat48bppRGB;
    case MIME_PNGIO_FMT_BGRA32:
        return &GUID_WICPixelFormat32bppBGRA;
    case MIME_PNGIO_FMT_RGBA64:
        return &GUID_WICPixelFormat64bppRGBA;
    default:
        return &GUID_WICPixelFormatDontCare;
    }
}

static HRESULT png_open(const BYTE *data, SIZE_T size, void **out)
{
    if (!mime_pngio_is_png(data, size))
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    PngState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    int rc = mime_pngio_open(data, size, &WicAllocator, 0, &s->dec);
    if (rc != MIME_PNGIO_OK) {
        WicFree(s);
        return png_error(rc);
    }
    s->info = mime_pngio_info(s->dec);
    s->data = data;
    s->size = size;
    s->format = *png_pixel_format(mime_pngio_native_format(s->info));
    s->indexed = WicFormatIsIndexed(&s->format);
    if (s->indexed) {
        UINT count = s->info->palette_count;
        for (UINT i = 0; i < count; i++) {
            UINT a = s->info->palette_alpha[i];
            s->palette[i] = (a << 24) | ((UINT)s->info->palette[i][0] << 16) | ((UINT)s->info->palette[i][1] << 8) |
                            s->info->palette[i][2];
        }
        s->palette_count = count;
    }
    *out = s;
    return S_OK;
}

static void png_close(void *state)
{
    PngState *s = state;
    if (s->iccp)
        mime_pngio_free(s->dec, s->iccp);
    mime_pngio_close(s->dec);
    WicFree(s);
}

static UINT png_frame_count(void *state) { return 1; }

static HRESULT png_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    PngState *s = state;
    info->width = s->info->width;
    info->height = s->info->height;
    info->format = s->format;
    info->dpi_x = info->dpi_y = 96.0;
    /* the resolution counts only when it is per meter. */
    if (s->info->has_phys && s->info->phys_unit == 1) {
        info->dpi_x = WicDpiFromMeter((INT)s->info->phys_x);
        info->dpi_y = WicDpiFromMeter((INT)s->info->phys_y);
    }
    info->has_palette = s->indexed;
    info->palette = s->palette;
    info->palette_count = s->palette_count;
    return S_OK;
}

static HRESULT png_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    PngState *s = state;
    int rc = mime_pngio_decode(s->dec, dest, stride);
    return rc == MIME_PNGIO_OK ? S_OK : png_error(rc);
}

static void png_load_profile(PngState *s)
{
    if (s->iccp_tried)
        return;
    s->iccp_tried = TRUE;
    if (s->info->has_iccp && mime_pngio_iccp_data(s->dec, &s->iccp, &s->iccp_size) != MIME_PNGIO_OK)
        s->iccp = nullptr;
}

static UINT png_profile_count(void *state, UINT index)
{
    PngState *s = state;
    png_load_profile(s);
    return s->iccp ? 1 : 0;
}

static HRESULT png_profile(void *state, UINT index, UINT n, WicProfile *out)
{
    PngState *s = state;
    png_load_profile(s);
    if (!s->iccp || n)
        return E_INVALIDARG;
    out->exif = FALSE;
    out->data = s->iccp;
    out->size = (UINT)s->iccp_size;
    return S_OK;
}

static HRESULT png_metadata(void *state, UINT index, IWICMetadataReader ***readers, UINT *count)
{
    PngState *s = state;
    return WicPngReaders(s->dec, s->data, s->size, readers, count);
}

const WicDecoderOps wic_dec_png = {
    .open = png_open,
    .close = png_close,
    .frame_count = png_frame_count,
    .frame_info = png_frame_info,
    .frame_decode = png_frame_decode,
    .profile_count = png_profile_count,
    .profile = png_profile,
    .metadata = png_metadata,
};

const WicDecoderOps wic_dec_png1 = {
    .open = png_open,
    .close = png_close,
    .frame_count = png_frame_count,
    .frame_info = png_frame_info,
    .frame_decode = png_frame_decode,
    .profile_count = png_profile_count,
    .profile = png_profile,
    .metadata = png_metadata,
};

/* the encoder. */

static const WicOptionDesc g_png_options[] = {
    {L"InterlaceOption", VT_BOOL, 0, 0, 1},
    {L"FilterOption", VT_UI1, 0, 0, 6},
};

typedef struct PngEncoder {
    IStream *stream;
    BYTE *file;
    SIZE_T size;
} PngEncoder;

static HRESULT png_begin(IStream *stream, void **state)
{
    PngEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    *state = e;
    return S_OK;
}

static int png_encoder_format(const GUID *format, BOOL *swap_rb)
{
    *swap_rb = FALSE;
    if (IsEqualGUID(format, &GUID_WICPixelFormatBlackWhite))
        return MIME_PNGIO_FMT_GRAY1;
    if (IsEqualGUID(format, &GUID_WICPixelFormat2bppGray))
        return MIME_PNGIO_FMT_GRAY2;
    if (IsEqualGUID(format, &GUID_WICPixelFormat4bppGray))
        return MIME_PNGIO_FMT_GRAY4;
    if (IsEqualGUID(format, &GUID_WICPixelFormat8bppGray))
        return MIME_PNGIO_FMT_GRAY8;
    if (IsEqualGUID(format, &GUID_WICPixelFormat16bppGray))
        return MIME_PNGIO_FMT_GRAY16;
    if (IsEqualGUID(format, &GUID_WICPixelFormat1bppIndexed))
        return MIME_PNGIO_FMT_INDEXED1;
    if (IsEqualGUID(format, &GUID_WICPixelFormat2bppIndexed))
        return MIME_PNGIO_FMT_INDEXED2;
    if (IsEqualGUID(format, &GUID_WICPixelFormat4bppIndexed))
        return MIME_PNGIO_FMT_INDEXED4;
    if (IsEqualGUID(format, &GUID_WICPixelFormat8bppIndexed))
        return MIME_PNGIO_FMT_INDEXED8;
    if (IsEqualGUID(format, &GUID_WICPixelFormat24bppBGR))
        return MIME_PNGIO_FMT_BGR24;
    if (IsEqualGUID(format, &GUID_WICPixelFormat32bppBGRA))
        return MIME_PNGIO_FMT_BGRA32;
    if (IsEqualGUID(format, &GUID_WICPixelFormat48bppRGB))
        return MIME_PNGIO_FMT_RGB48;
    if (IsEqualGUID(format, &GUID_WICPixelFormat48bppBGR)) {
        *swap_rb = TRUE;
        return MIME_PNGIO_FMT_RGB48;
    }
    if (IsEqualGUID(format, &GUID_WICPixelFormat64bppRGBA))
        return MIME_PNGIO_FMT_RGBA64;
    if (IsEqualGUID(format, &GUID_WICPixelFormat64bppBGRA)) {
        *swap_rb = TRUE;
        return MIME_PNGIO_FMT_RGBA64;
    }
    return MIME_PNGIO_FMT_NONE;
}

static HRESULT png_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    PngEncoder *e = state;
    BOOL swap;
    int format = png_encoder_format(&frame->format, &swap);
    if (!format)
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    UINT channels = format == MIME_PNGIO_FMT_RGBA64 ? 4 : 3;
    BYTE *pixels = frame->pixels;
    BYTE *copy = nullptr;
    if (swap) {
        copy = WicAlloc((SIZE_T)frame->stride * frame->height);
        if (!copy)
            return E_OUTOFMEMORY;
        for (UINT y = 0; y < frame->height; y++) {
            const UINT16 *s = (const UINT16 *)(frame->pixels + (SIZE_T)y * frame->stride);
            UINT16 *d = (UINT16 *)(copy + (SIZE_T)y * frame->stride);
            for (UINT x = 0; x < frame->width; x++) {
                d[x * channels] = s[x * channels + 2];
                d[x * channels + 1] = s[x * channels + 1];
                d[x * channels + 2] = s[x * channels];
                if (channels == 4)
                    d[x * channels + 3] = s[x * channels + 3];
            }
        }
        pixels = copy;
    }
    MimePngioEncode p;
    memset(&p, 0, sizeof(p));
    p.alloc = &WicAllocator;
    p.width = frame->width;
    p.height = frame->height;
    p.format = format;
    p.pixels = pixels;
    p.stride = frame->stride;
    BYTE palette[768], alpha[256];
    if (WicFormatIsIndexed(&frame->format)) {
        UINT bits = WicFormatBits(&frame->format), maximum = 1u << bits;
        UINT n = frame->palette_count < maximum ? frame->palette_count : maximum;
        BOOL has_alpha = FALSE;
        for (UINT i = 0; i < n; i++) {
            WICColor c = frame->palette[i];
            palette[i * 3] = (BYTE)(c >> 16);
            palette[i * 3 + 1] = (BYTE)(c >> 8);
            palette[i * 3 + 2] = (BYTE)c;
            alpha[i] = (BYTE)(c >> 24);
            if (alpha[i] != 255)
                has_alpha = TRUE;
        }
        p.palette = palette;
        p.palette_count = n;
        if (has_alpha) {
            p.palette_alpha = alpha;
            p.palette_alpha_count = n;
        }
    }
    if (frame->options_set & 1)
        p.flags |= frame->options[0] != 0 ? MIME_PNGIO_ENC_INTERLACE : 0;
    p.filter = MIME_PNGIO_FILTER_ADAPTIVE;
    if (frame->options_set & 2) {
        static const int map[] = {MIME_PNGIO_FILTER_ADAPTIVE, MIME_PNGIO_FILTER_NONE, MIME_PNGIO_FILTER_SUB,
                                  MIME_PNGIO_FILTER_UP,       MIME_PNGIO_FILTER_AVERAGE, MIME_PNGIO_FILTER_PAETH,
                                  MIME_PNGIO_FILTER_ADAPTIVE};
        p.filter = map[(UINT)frame->options[1] < 7 ? (UINT)frame->options[1] : 0];
    }
    p.level = -1;
    if (frame->dpi_x > 0 && frame->dpi_y > 0) {
        p.has_phys = true;
        p.phys_x = (uint32_t)(frame->dpi_x / 0.0254 + 0.5);
        p.phys_y = (uint32_t)(frame->dpi_y / 0.0254 + 0.5);
        p.phys_unit = 1;
    }
    PngMeta *meta = nullptr;
    WicPngApplyMetadata(&meta, &p, frame->metadata);
    void *out = nullptr;
    size_t out_size = 0;
    int rc = mime_pngio_encode(&p, &out, &out_size);
    WicPngMetadataFree(meta);
    WicFree(copy);
    if (rc != MIME_PNGIO_OK)
        return rc == MIME_PNGIO_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    HRESULT hr = WicWriteStream(e->stream, out, out_size);
    WicFree(out);
    return hr;
}

static HRESULT png_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                           IWICMetadataBlockReader *metadata) { return S_OK; }
static void png_destroy(void *state) { WicFree(state); }

const WicEncoderOps wic_enc_png = {
    .options = g_png_options,
    .option_count = WIC_ARRAY_SIZE(g_png_options),
    .begin = png_begin,
    .frame_metadata = TRUE,
    .write_frame = png_write_frame,
    .finish = png_finish,
    .destroy = png_destroy,
};
