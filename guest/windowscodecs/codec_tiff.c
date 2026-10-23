/*
 * TIFF codec: one frame per image, with its directory as metadata. the encoder supports multiple frames and
 * application-selected compression.
 */
#include "wic_codec.h"
#include "codec_util.h"
#include "meta_codecs.h"

typedef struct TiffState {
    const BYTE *data;
    SIZE_T size;
    MimeTiff tiff;
    UINT frames;
    WICColor **palettes;
} TiffState;

typedef struct TiffKind {
    const GUID *format;
    UINT bits;    /* of the WIC pixel. */
    BOOL invert;  /* white is zero. */
    BOOL gray_alpha;
    BOOL rgb_swap;
    BOOL rgba;    /* RGB with alpha, 3 + 1. */
    BOOL expand_gray_alpha; /* gray + alpha to color. */
} TiffKind;

static HRESULT tiff_kind(const MimeTiffFrame *f, TiffKind *k)
{
    memset(k, 0, sizeof(*k));
    UINT ob = f->out_bits_per_sample, osp = f->out_samples_per_pixel, op = f->out_photometric;
    BOOL is_float = f->sample_format == MIME_TIFF_SAMPLE_FLOAT;
    BOOL premul = f->alpha == MIME_TIFF_ALPHA_ASSOCIATED;
    if (op == MIME_TIFF_PHOTOMETRIC_PALETTE && osp == 1) {
        k->bits = ob == 2 ? 2 : ob;
        k->format = ob == 1 ? &GUID_WICPixelFormat1bppIndexed : ob == 2 ? &GUID_WICPixelFormat2bppIndexed
                    : ob == 4 ? &GUID_WICPixelFormat4bppIndexed : ob == 8 ? &GUID_WICPixelFormat8bppIndexed : nullptr;
        return k->format ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (op == MIME_TIFF_PHOTOMETRIC_WHITE_IS_ZERO || op == MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO) {
        k->invert = op == MIME_TIFF_PHOTOMETRIC_WHITE_IS_ZERO;
        if (osp == 1) {
            k->bits = ob;
            if (is_float && ob == 32)
                k->format = &GUID_WICPixelFormat32bppGrayFloat;
            else if (ob == 1)
                k->format = &GUID_WICPixelFormatBlackWhite;
            else if (ob == 2)
                k->format = &GUID_WICPixelFormat2bppGray;
            else if (ob == 4)
                k->format = &GUID_WICPixelFormat4bppGray;
            else if (ob == 8)
                k->format = &GUID_WICPixelFormat8bppGray;
            else if (ob == 16)
                k->format = &GUID_WICPixelFormat16bppGray;
            return k->format ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        }
        if (osp == 2 && (ob == 8 || ob == 16) && !is_float) {
            k->gray_alpha = TRUE;
            k->bits = ob == 8 ? 32 : 64;
            k->format = ob == 8 ? (premul ? &GUID_WICPixelFormat32bppPBGRA : &GUID_WICPixelFormat32bppBGRA)
                                : (premul ? &GUID_WICPixelFormat64bppPRGBA : &GUID_WICPixelFormat64bppRGBA);
            return S_OK;
        }
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (op == MIME_TIFF_PHOTOMETRIC_RGB) {
        if (osp == 3) {
            if (is_float && ob == 32) {
                k->bits = 96;
                k->format = &GUID_WICPixelFormat96bppRGBFloat;
            } else if (ob == 8) {
                k->bits = 24;
                k->rgb_swap = TRUE;
                k->format = &GUID_WICPixelFormat24bppBGR;
            } else if (ob == 16) {
                k->bits = 48;
                k->format = &GUID_WICPixelFormat48bppRGB;
            }
            return k->format ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        }
        if (osp == 4) {
            k->rgba = TRUE;
            if (is_float && ob == 32) {
                k->bits = 128;
                k->format = premul ? &GUID_WICPixelFormat128bppPRGBAFloat : &GUID_WICPixelFormat128bppRGBAFloat;
            } else if (ob == 8) {
                k->bits = 32;
                k->rgb_swap = TRUE;
                k->format = premul ? &GUID_WICPixelFormat32bppPBGRA : &GUID_WICPixelFormat32bppBGRA;
            } else if (ob == 16) {
                k->bits = 64;
                k->format = premul ? &GUID_WICPixelFormat64bppPRGBA : &GUID_WICPixelFormat64bppRGBA;
            }
            return k->format ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        }
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (op == MIME_TIFF_PHOTOMETRIC_SEPARATED) {
        if (osp == 4 && ob == 8) {
            k->bits = 32;
            k->format = &GUID_WICPixelFormat32bppCMYK;
        } else if (osp == 4 && ob == 16) {
            k->bits = 64;
            k->format = &GUID_WICPixelFormat64bppCMYK;
        } else if (osp == 5 && ob == 8) {
            k->bits = 40;
            k->format = &GUID_WICPixelFormat40bppCMYKAlpha;
        } else if (osp == 5 && ob == 16) {
            k->bits = 80;
            k->format = &GUID_WICPixelFormat80bppCMYKAlpha;
        }
        return k->format ? S_OK : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
}

static HRESULT tiff_error(int code)
{
    switch (code) {
    case MIME_TIFF_ERR_NOMEM:
        return E_OUTOFMEMORY;
    case MIME_TIFF_ERR_UNSUPPORTED:
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    case MIME_TIFF_ERR_RANGE:
        return WINCODEC_ERR_FRAMEMISSING;
    case MIME_TIFF_ERR_PARAM:
        return E_INVALIDARG;
    case MIME_TIFF_ERR_FORMAT:
        return WINCODEC_ERR_BADHEADER;
    default:
        return WINCODEC_ERR_BADIMAGE;
    }
}

static HRESULT tiff_open(const BYTE *data, SIZE_T size, void **out)
{
    TiffState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->data = data;
    s->size = size;
    if (mime_tiff_open(&s->tiff, data, size) != 0) {
        WicFree(s);
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    }
    s->frames = mime_tiff_ifd_count(&s->tiff);
    if (!s->frames) {
        WicFree(s);
        return WINCODEC_ERR_BADIMAGE;
    }
    s->palettes = WicAllocZero(((SIZE_T)s->frames + 1) * sizeof(WICColor *));
    if (!s->palettes) {
        WicFree(s);
        return E_OUTOFMEMORY;
    }
    *out = s;
    return S_OK;
}

static void tiff_close(void *state)
{
    TiffState *s = state;
    for (UINT i = 0; i < s->frames; i++)
        WicFree(s->palettes[i]);
    WicFree(s->palettes);
    WicFree(s);
}

static UINT tiff_frame_count(void *state) { return ((TiffState *)state)->frames; }

static HRESULT tiff_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    TiffState *s = state;
    MimeTiffFrame f;
    int rc = mime_tiff_frame_open_index(&s->tiff, index, &f);
    if (rc != 0)
        return tiff_error(rc);
    TiffKind k;
    HRESULT hr = tiff_kind(&f, &k);
    if (FAILED(hr))
        return hr;
    info->width = f.width;
    info->height = f.height;
    info->format = *k.format;
    info->dpi_x = info->dpi_y = 96.0;
    if (f.x_resolution_den && f.x_resolution_num) {
        double v = (double)f.x_resolution_num / f.x_resolution_den;
        info->dpi_x = f.resolution_unit == 3 ? v * 2.54 : v;
    }
    if (f.y_resolution_den && f.y_resolution_num) {
        double v = (double)f.y_resolution_num / f.y_resolution_den;
        info->dpi_y = f.resolution_unit == 3 ? v * 2.54 : v;
    }
    if (WicFormatIsIndexed(k.format)) {
        if (!s->palettes[index]) {
            WICColor *p = WicAllocZero(256 * sizeof(WICColor));
            if (!p)
                return E_OUTOFMEMORY;
            UINT16 rgb[3 * 256];
            UINT n = mime_tiff_frame_palette(&f, rgb, 256);
            for (UINT i = 0; i < n; i++)
                p[i] = 0xff000000u | ((UINT)(rgb[i * 3] >> 8) << 16) | ((UINT)(rgb[i * 3 + 1] >> 8) << 8) | (rgb[i * 3 + 2] >> 8);
            s->palettes[index] = p;
        }
        info->palette = s->palettes[index];
        info->palette_count = f.palette_entries < 256 ? f.palette_entries : 256;
        info->has_palette = TRUE;
    }
    return S_OK;
}

static HRESULT tiff_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    TiffState *s = state;
    MimeTiffFrame f;
    int rc = mime_tiff_frame_open_index(&s->tiff, index, &f);
    if (rc != 0)
        return tiff_error(rc);
    TiffKind k;
    HRESULT hr = tiff_kind(&f, &k);
    if (FAILED(hr))
        return hr;
    SIZE_T native_stride = f.out_row_bytes;
    BYTE *native = WicAlloc(native_stride * f.height);
    if (!native)
        return E_OUTOFMEMORY;
    rc = mime_tiff_frame_decode(&f, &WicAllocator, 0, native, native_stride);
    if (rc != 0) {
        WicFree(native);
        return tiff_error(rc);
    }
    UINT ob = f.out_bits_per_sample;
    for (UINT y = 0; y < f.height; y++) {
        const BYTE *src = native + y * native_stride;
        BYTE *dst = dest + (SIZE_T)y * stride;
        UINT width = f.width;
        if (k.gray_alpha) {
            if (ob == 8) {
                for (UINT x = 0; x < width; x++) {
                    BYTE g = src[x * 2], a = src[x * 2 + 1];
                    if (k.invert)
                        g = (BYTE)~g;
                    dst[x * 4] = dst[x * 4 + 1] = dst[x * 4 + 2] = g;
                    dst[x * 4 + 3] = a;
                }
            } else {
                for (UINT x = 0; x < width; x++) {
                    UINT16 g = ((const UINT16 *)src)[x * 2], a = ((const UINT16 *)src)[x * 2 + 1];
                    if (k.invert)
                        g = (UINT16)~g;
                    ((UINT16 *)dst)[x * 4] = ((UINT16 *)dst)[x * 4 + 1] = ((UINT16 *)dst)[x * 4 + 2] = g;
                    ((UINT16 *)dst)[x * 4 + 3] = a;
                }
            }
        } else if (k.rgb_swap && ob == 8) {
            UINT n = k.rgba ? 4 : 3;
            for (UINT x = 0; x < width; x++) {
                dst[x * n] = src[x * n + 2];
                dst[x * n + 1] = src[x * n + 1];
                dst[x * n + 2] = src[x * n];
                if (n == 4)
                    dst[x * 4 + 3] = src[x * 4 + 3];
            }
        } else if (k.invert) {
            UINT bytes = (width * k.bits + 7) / 8;
            for (UINT i = 0; i < bytes; i++)
                dst[i] = (BYTE)~src[i];
            if (k.bits < 8 && (width * k.bits) % 8)
                dst[bytes - 1] &= (BYTE)(0xff << (8 - (width * k.bits) % 8));
        } else {
            memcpy(dst, src, (width * k.bits + 7) / 8);
        }
    }
    WicFree(native);
    return S_OK;
}

static HRESULT tiff_metadata(void *state, UINT index, IWICMetadataReader ***out, UINT *count)
{
    TiffState *s = state;
    *out = nullptr;
    *count = 0;
    if (index == ~0u)
        return S_OK;
    IWICMetadataReader *ifd;
    HRESULT hr = WicIfdFromTiffIndex(&GUID_MetadataFormatIfd, &s->tiff, index, &ifd);
    if (FAILED(hr))
        return hr;
    IWICMetadataReader **list = WicAlloc(2 * sizeof(*list));
    if (!list) {
        ifd->lpVtbl->Release(ifd);
        return E_OUTOFMEMORY;
    }
    list[0] = ifd;
    *out = list;
    *count = 1;
    return S_OK;
}

const WicDecoderOps wic_dec_tiff = {
    .open = tiff_open,
    .close = tiff_close,
    .frame_count = tiff_frame_count,
    .frame_info = tiff_frame_info,
    .frame_decode = tiff_frame_decode,
    .metadata = tiff_metadata,
};

/* the encoder. */

static const WicOptionDesc g_tiff_options[] = {
    {L"TiffCompressionMethod", VT_UI1, WICTiffCompressionDontCare, 0, 7},
};

typedef struct TiffEncoder {
    IStream *stream;
    MimeTiffEncoder enc;
    BOOL started;
} TiffEncoder;

static HRESULT tiff_begin(IStream *stream, void **state)
{
    TiffEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    if (mime_tiff_encoder_init(&e->enc, &WicAllocator, 0) != 0) {
        WicFree(e);
        return E_OUTOFMEMORY;
    }
    e->started = TRUE;
    *state = e;
    return S_OK;
}

static HRESULT tiff_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    TiffEncoder *e = state;
    MimeTiffFrameSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.width = frame->width;
    spec.height = frame->height;
    const GUID *fmt = &frame->format;
    UINT16 *map = nullptr;
    BYTE *pixels = frame->pixels;
    BYTE *copy = nullptr;
    UINT stride = frame->stride;
    BOOL swap = FALSE, bilevel = FALSE;
    if (IsEqualGUID(fmt, &GUID_WICPixelFormatBlackWhite)) {
        spec.bits_per_sample = 1;
        spec.samples_per_pixel = 1;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO;
        bilevel = TRUE;
    } else if (IsEqualGUID(fmt, &GUID_WICPixelFormat4bppGray) || IsEqualGUID(fmt, &GUID_WICPixelFormat8bppGray) ||
               IsEqualGUID(fmt, &GUID_WICPixelFormat16bppGray)) {
        spec.bits_per_sample = (uint16_t)WicFormatBits(fmt);
        spec.samples_per_pixel = 1;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO;
    } else if (WicFormatIsIndexed(fmt)) {
        spec.bits_per_sample = (uint16_t)WicFormatBits(fmt);
        spec.samples_per_pixel = 1;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_PALETTE;
        UINT entries = 1u << spec.bits_per_sample;
        map = WicAllocZero((SIZE_T)entries * 3 * sizeof(UINT16));
        if (!map)
            return E_OUTOFMEMORY;
        for (UINT i = 0; i < entries && i < frame->palette_count; i++) {
            WICColor c = frame->palette[i];
            map[i] = (UINT16)(((c >> 16) & 255) * 257);
            map[entries + i] = (UINT16)(((c >> 8) & 255) * 257);
            map[2 * entries + i] = (UINT16)((c & 255) * 257);
        }
        spec.color_map = map;
    } else if (IsEqualGUID(fmt, &GUID_WICPixelFormat24bppBGR)) {
        spec.bits_per_sample = 8;
        spec.samples_per_pixel = 3;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_RGB;
        swap = TRUE;
    } else if (IsEqualGUID(fmt, &GUID_WICPixelFormat32bppBGRA)) {
        spec.bits_per_sample = 8;
        spec.samples_per_pixel = 4;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_RGB;
        spec.extra_samples = 1;
        spec.alpha = MIME_TIFF_ALPHA_UNASSOCIATED;
        swap = TRUE;
    } else if (IsEqualGUID(fmt, &GUID_WICPixelFormat48bppRGB)) {
        spec.bits_per_sample = 16;
        spec.samples_per_pixel = 3;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_RGB;
    } else if (IsEqualGUID(fmt, &GUID_WICPixelFormat64bppRGBA)) {
        spec.bits_per_sample = 16;
        spec.samples_per_pixel = 4;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_RGB;
        spec.extra_samples = 1;
        spec.alpha = MIME_TIFF_ALPHA_UNASSOCIATED;
    } else if (IsEqualGUID(fmt, &GUID_WICPixelFormat32bppCMYK)) {
        spec.bits_per_sample = 8;
        spec.samples_per_pixel = 4;
        spec.photometric = MIME_TIFF_PHOTOMETRIC_SEPARATED;
    } else {
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (swap) {
        UINT n = spec.samples_per_pixel;
        copy = WicAlloc((SIZE_T)stride * frame->height);
        if (!copy) {
            WicFree(map);
            return E_OUTOFMEMORY;
        }
        for (UINT y = 0; y < frame->height; y++) {
            const BYTE *s = frame->pixels + (SIZE_T)y * stride;
            BYTE *d = copy + (SIZE_T)y * stride;
            for (UINT x = 0; x < frame->width; x++) {
                d[x * n] = s[x * n + 2];
                d[x * n + 1] = s[x * n + 1];
                d[x * n + 2] = s[x * n];
                if (n == 4)
                    d[x * 4 + 3] = s[x * 4 + 3];
            }
        }
        pixels = copy;
    }
    UINT method = (frame->options_set & 1) ? (UINT)frame->options[0] : WICTiffCompressionDontCare;
    switch (method) {
    case WICTiffCompressionNone:
        spec.compression = MIME_TIFF_COMPRESSION_NONE;
        break;
    case WICTiffCompressionRLE:
        spec.compression = MIME_TIFF_COMPRESSION_PACKBITS;
        break;
    case WICTiffCompressionZIP:
        spec.compression = MIME_TIFF_COMPRESSION_DEFLATE;
        break;
    case WICTiffCompressionLZWHDifferencing:
        spec.compression = MIME_TIFF_COMPRESSION_LZW;
        spec.predictor = 2;
        break;
    case WICTiffCompressionCCITT3:
    case WICTiffCompressionCCITT4:
        if (bilevel) {
            spec.compression = method == WICTiffCompressionCCITT3 ? MIME_TIFF_COMPRESSION_CCITT_G3 : MIME_TIFF_COMPRESSION_CCITT_G4;
            break;
        }
        spec.compression = MIME_TIFF_COMPRESSION_LZW;
        break;
    default:
        spec.compression = MIME_TIFF_COMPRESSION_LZW;
    }
    if (frame->dpi_x > 0 && frame->dpi_y > 0) {
        spec.resolution_unit = 2;
        spec.x_resolution_num = (uint32_t)(frame->dpi_x * 1000.0 + 0.5);
        spec.x_resolution_den = 1000;
        spec.y_resolution_num = (uint32_t)(frame->dpi_y * 1000.0 + 0.5);
        spec.y_resolution_den = 1000;
    }
    WicIfdSet *set = nullptr;
    IWICMetadataReader *ifd = WicBlocksFind(frame->metadata, &GUID_MetadataFormatIfd);
    if (ifd) {
        if (SUCCEEDED(WicIfdSetCreate(ifd, &set))) {
            spec.extra_tags = WicIfdSetEntries(set, &spec.extra_tag_count);
            spec.exif_ifd = WicIfdSetExif(set);
            spec.gps_ifd = WicIfdSetGps(set);
        }
        ifd->lpVtbl->Release(ifd);
    }
    int rc = mime_tiff_encoder_add_frame(&e->enc, &spec, pixels, stride);
    WicIfdSetFree(set);
    WicFree(copy);
    WicFree(map);
    if (rc != 0)
        return rc == MIME_TIFF_ERR_NOMEM ? E_OUTOFMEMORY : rc == MIME_TIFF_ERR_PARAM ? E_INVALIDARG : E_FAIL;
    return S_OK;
}

static HRESULT tiff_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                           IWICMetadataBlockReader *metadata)
{
    TiffEncoder *e = state;
    void *out = nullptr;
    size_t length = 0;
    e->started = FALSE;
    int rc = mime_tiff_encoder_finish(&e->enc, &out, &length);
    if (rc != 0)
        return rc == MIME_TIFF_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    HRESULT hr = WicWriteStream(e->stream, out, length);
    WicFree(out);
    return hr;
}

static void tiff_destroy(void *state)
{
    TiffEncoder *e = state;
    if (e->started)
        mime_tiff_encoder_abort(&e->enc);
    WicFree(e);
}

const WicEncoderOps wic_enc_tiff = {
    .options = g_tiff_options,
    .option_count = WIC_ARRAY_SIZE(g_tiff_options),
    .multiframe = TRUE,
    .begin = tiff_begin,
    .write_frame = tiff_write_frame,
    .finish = tiff_finish,
    .destroy = tiff_destroy,
    .frame_metadata = TRUE,
};
