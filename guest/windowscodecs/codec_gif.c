/*
 * the gif codec: frames in the size of their rectangles with their palettes, and the descriptors, controls and
 * extensions of the file as metadata. The encoder collects frames and writes the file when it is committed.
 */
#include "wic_codec.h"
#include "codec_util.h"
#include "meta_codecs.h"
#include <gif/gif.h>

/* ------------------------------------------------------------------ the decoder */

typedef struct GifState {
    MimeGif *gif;
    const MimeGifInfo *info;
    UINT frames;
    WICColor **palettes; /* made when a frame is asked for. */
} GifState;

static HRESULT gif_error(int code)
{
    switch (code) {
    case MIME_GIF_ERR_NOMEM:
        return E_OUTOFMEMORY;
    case MIME_GIF_ERR_UNSUPPORTED:
        return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    case MIME_GIF_ERR_FORMAT:
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    case MIME_GIF_ERR_EMPTY:
        return WINCODEC_ERR_FRAMEMISSING;
    default:
        return WINCODEC_ERR_BADIMAGE;
    }
}

static HRESULT gif_open(const BYTE *data, SIZE_T size, void **out)
{
    if (!mime_gif_is_gif(data, size))
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    GifState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    int rc = mime_gif_open(data, size, &WicAllocator, &s->gif);
    if (rc < 0) {
        WicFree(s);
        return gif_error(rc);
    }
    s->info = mime_gif_info(s->gif);
    s->frames = s->info->frame_count;
    s->palettes = WicAllocZero(((SIZE_T)s->frames + 1) * sizeof(WICColor *));
    if (!s->palettes) {
        mime_gif_close(s->gif);
        WicFree(s);
        return E_OUTOFMEMORY;
    }
    *out = s;
    return S_OK;
}

static void gif_close(void *state)
{
    GifState *s = state;
    for (UINT i = 0; i < s->frames; i++)
        WicFree(s->palettes[i]);
    WicFree(s->palettes);
    mime_gif_close(s->gif);
    WicFree(s);
}

static UINT gif_frame_count(void *state) { return ((GifState *)state)->frames; }

/* the palette of a frame, the transparent color with no alpha, and long enough to hold the transparent index. */
static const WICColor *frame_palette(GifState *s, UINT index, const MimeGifFrame *f, UINT *count)
{
    if (!f->palette || !f->palette_size) {
        *count = 0;
        return nullptr;
    }
    if (!s->palettes[index]) {
        WICColor *p = WicAllocZero(256 * sizeof(WICColor));
        if (!p) {
            *count = 0;
            return nullptr;
        }
        UINT n = f->palette_size;
        for (UINT i = 0; i < n && i < 256; i++)
            p[i] = 0xff000000u | ((UINT)f->palette[i * 3] << 16) | ((UINT)f->palette[i * 3 + 1] << 8) | f->palette[i * 3 + 2];
        s->palettes[index] = p;
    }
    UINT n = f->palette_size;
    if (f->has_transparency) {
        if (f->transparent_index >= n) {
            /* the palette grows to hold it, with black. */
            for (UINT i = n; i <= f->transparent_index; i++)
                s->palettes[index][i] = 0xff000000u;
            n = (UINT)f->transparent_index + 1;
        }
        s->palettes[index][f->transparent_index] &= 0x00ffffffu;
    }
    *count = n;
    return s->palettes[index];
}

static HRESULT gif_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    GifState *s = state;
    const MimeGifFrame *f = mime_gif_frame(s->gif, index);
    if (!f || !f->width || !f->height)
        return WINCODEC_ERR_BADIMAGE;
    info->width = f->width;
    info->height = f->height;
    info->format = GUID_WICPixelFormat8bppIndexed;
    info->dpi_x = info->dpi_y = 96.0;
    UINT count;
    info->palette = frame_palette(s, index, f, &count);
    info->palette_count = count;
    info->has_palette = count != 0;
    return S_OK;
}

static HRESULT gif_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    GifState *s = state;
    int rc = mime_gif_decode_frame(s->gif, index, dest, stride);
    return rc >= 0 ? S_OK : gif_error(rc);
}

static HRESULT gif_container_palette(void *state, WICColor *colors, UINT *count)
{
    GifState *s = state;
    *count = 0;
    if (!s->info->has_global_palette || !s->info->global_palette)
        return WINCODEC_ERR_FRAMEMISSING;
    UINT n = s->info->global_palette_size;
    const MimeGifFrame *f0 = mime_gif_frame(s->gif, 0);
    for (UINT i = 0; i < n; i++)
        colors[i] = 0xff000000u | ((UINT)s->info->global_palette[i * 3] << 16) |
                    ((UINT)s->info->global_palette[i * 3 + 1] << 8) | s->info->global_palette[i * 3 + 2];
    if (f0 && f0->has_transparency) {
        if (f0->transparent_index >= n) {
            for (UINT i = n; i <= f0->transparent_index && i < 256; i++)
                colors[i] = 0xff000000u;
            n = (UINT)f0->transparent_index + 1;
        }
        colors[f0->transparent_index] &= 0x00ffffffu;
    }
    *count = n;
    return S_OK;
}

/* ------------------------------------------------------------------ metadata */

static HRESULT put_u8(IWICMetadataReader *r, const WCHAR *name, BYTE v)
{
    return WicMetaPutNamed(r, name, VT_UI1, v);
}

static HRESULT add_extension(GifState *s, const MimeGifExtension *ext, IWICMetadataReader **list, UINT *n)
{
    IWICMetadataReader *r = nullptr;
    HRESULT hr;
    if (ext->kind == MIME_GIF_EXT_COMMENT) {
        hr = WicMetaReaderCreate(&GUID_MetadataFormatGifComment, &r);
        if (SUCCEEDED(hr)) {
            size_t length = ext->data_size;
            char *text = WicAlloc(length + 1);
            if (text) {
                length = mime_gif_extension_copy(ext, text, length);
                hr = WicMetaPutAnsi(r, L"TextEntry", text, length);
                WicFree(text);
            } else {
                hr = E_OUTOFMEMORY;
            }
        }
    } else if (ext->kind == MIME_GIF_EXT_APPLICATION) {
        hr = WicMetaReaderCreate(&GUID_MetadataFormatAPE, &r);
        if (SUCCEEDED(hr)) {
            BYTE id[11];
            memset(id, 0, sizeof(id));
            memcpy(id, ext->header, ext->header_size < 11 ? ext->header_size : 11);
            hr = WicMetaPutBytes(r, L"Application", id, 11);
            if (SUCCEEDED(hr)) {
                /* the data is the run of sub blocks, each with its length byte, without the terminator. */
                const BYTE *p = ext->data;
                SIZE_T total = 0;
                if (ext->chained && p) {
                    SIZE_T at = 0;
                    while (at < ext->data_size + 256 && p[at]) {
                        at += (SIZE_T)p[at] + 1;
                        if (at > ext->data_size + 4096)
                            break;
                    }
                    total = at;
                } else if (p) {
                    total = ext->data_size;
                }
                hr = WicMetaPutBytes(r, L"Data", p ? p : (const BYTE *)"", (UINT)total);
            }
        }
    } else {
        return S_FALSE;
    }
    if (FAILED(hr)) {
        if (r)
            r->lpVtbl->Release(r);
        return hr;
    }
    list[(*n)++] = r;
    return S_OK;
}

static HRESULT gif_metadata(void *state, UINT index, IWICMetadataReader ***out, UINT *count)
{
    GifState *s = state;
    *out = nullptr;
    *count = 0;
    UINT capacity = 8 + s->info->extension_count;
    IWICMetadataReader **list = WicAllocZero((SIZE_T)capacity * sizeof(*list));
    if (!list)
        return E_OUTOFMEMORY;
    UINT n = 0;
    HRESULT hr = S_OK;
    if (index == ~0u) {
        const MimeGifInfo *g = s->info;
        IWICMetadataReader *r;
        hr = WicMetaReaderCreate(&GUID_MetadataFormatLSD, &r);
        if (SUCCEEDED(hr)) {
            BYTE signature[6] = {'G', 'I', 'F', '8', (BYTE)(g->version == 87 ? '7' : '9'), 'a'};
            hr = WicMetaPutBytes(r, L"Signature", signature, 6);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"Width", VT_UI2, g->width);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"Height", VT_UI2, g->height);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"GlobalColorTableFlag", VT_BOOL, g->has_global_palette);
            if (SUCCEEDED(hr))
                hr = put_u8(r, L"ColorResolution", (BYTE)(g->color_resolution ? g->color_resolution - 1 : 0));
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"SortFlag", VT_BOOL, g->global_palette_sorted);
            if (SUCCEEDED(hr)) {
                BYTE bits = 0;
                for (UINT size = g->global_palette_size; size > 2; size >>= 1)
                    bits++;
                hr = put_u8(r, L"GlobalColorTableSize", g->has_global_palette ? bits : 0);
            }
            if (SUCCEEDED(hr))
                hr = put_u8(r, L"BackgroundColorIndex", g->background_index);
            if (SUCCEEDED(hr))
                hr = put_u8(r, L"PixelAspectRatio", g->pixel_aspect_ratio);
            if (SUCCEEDED(hr))
                list[n++] = r;
            else
                r->lpVtbl->Release(r);
        }
        for (UINT i = 0; i < g->extension_count && SUCCEEDED(hr); i++) {
            const MimeGifExtension *ext = mime_gif_extension(s->gif, i);
            if (ext && ext->frame_index == 0 && n < capacity) {
                HRESULT e = add_extension(s, ext, list, &n);
                if (FAILED(e))
                    hr = e;
            }
        }
    } else {
        const MimeGifFrame *f = mime_gif_frame(s->gif, index);
        if (!f) {
            WicFree(list);
            return WINCODEC_ERR_FRAMEMISSING;
        }
        IWICMetadataReader *r;
        hr = WicMetaReaderCreate(&GUID_MetadataFormatIMD, &r);
        if (SUCCEEDED(hr)) {
            hr = WicMetaPutNamed(r, L"Left", VT_UI2, f->left);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"Top", VT_UI2, f->top);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"Width", VT_UI2, f->width);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"Height", VT_UI2, f->height);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"LocalColorTableFlag", VT_BOOL, f->has_local_palette);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"InterlaceFlag", VT_BOOL, f->interlaced);
            if (SUCCEEDED(hr))
                hr = WicMetaPutNamed(r, L"SortFlag", VT_BOOL, f->local_palette_sorted);
            if (SUCCEEDED(hr)) {
                BYTE bits = 0;
                for (UINT size = f->has_local_palette ? f->palette_size : 0; size > 2; size >>= 1)
                    bits++;
                hr = put_u8(r, L"LocalColorTableSize", bits);
            }
            if (SUCCEEDED(hr))
                list[n++] = r;
            else
                r->lpVtbl->Release(r);
        }
        if (SUCCEEDED(hr) && f->has_gce) {
            hr = WicMetaReaderCreate(&GUID_MetadataFormatGCE, &r);
            if (SUCCEEDED(hr)) {
                hr = put_u8(r, L"Disposal", f->disposal_raw & 7);
                if (SUCCEEDED(hr))
                    hr = WicMetaPutNamed(r, L"UserInputFlag", VT_BOOL, f->user_input);
                if (SUCCEEDED(hr))
                    hr = WicMetaPutNamed(r, L"TransparencyFlag", VT_BOOL, f->has_transparency);
                if (SUCCEEDED(hr))
                    hr = WicMetaPutNamed(r, L"Delay", VT_UI2, f->delay);
                if (SUCCEEDED(hr))
                    hr = put_u8(r, L"TransparentColorIndex", f->transparent_index);
                if (SUCCEEDED(hr))
                    list[n++] = r;
                else
                    r->lpVtbl->Release(r);
            }
        }
        for (UINT i = 0; i < s->info->extension_count && SUCCEEDED(hr); i++) {
            const MimeGifExtension *ext = mime_gif_extension(s->gif, i);
            if (ext && ext->frame_index == index && index != 0 && n < capacity) {
                HRESULT e = add_extension(s, ext, list, &n);
                if (FAILED(e))
                    hr = e;
            }
        }
    }
    if (FAILED(hr)) {
        for (UINT i = 0; i < n; i++)
            list[i]->lpVtbl->Release(list[i]);
        WicFree(list);
        return hr;
    }
    *out = list;
    *count = n;
    return S_OK;
}

const WicDecoderOps wic_dec_gif = {
    .open = gif_open,
    .close = gif_close,
    .frame_count = gif_frame_count,
    .frame_info = gif_frame_info,
    .frame_decode = gif_frame_decode,
    .container_palette = gif_container_palette,
    .metadata = gif_metadata,
};

/* ------------------------------------------------------------------ the encoder */

typedef struct GifFrameData {
    UINT width, height;
    BYTE *indices;
    BYTE palette[768];
    UINT palette_size;
    BOOL transparent;
    BYTE transparent_index;
    UINT left, top;
    UINT delay;
    UINT disposal;
    BOOL user_input;
    BOOL interlace;
} GifFrameData;

typedef struct GifEncoder {
    IStream *stream;
    GifFrameData *frames;
    UINT count;
    UINT capacity;
} GifEncoder;

static HRESULT gif_begin(IStream *stream, void **state)
{
    GifEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    *state = e;
    return S_OK;
}

static HRESULT gif_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    GifEncoder *e = state;
    if (e->count == e->capacity) {
        UINT capacity = e->capacity ? e->capacity * 2 : 4;
        GifFrameData *bigger = WicRealloc(e->frames, (SIZE_T)capacity * sizeof(*bigger));
        if (!bigger)
            return E_OUTOFMEMORY;
        e->frames = bigger;
        e->capacity = capacity;
    }
    GifFrameData *f = &e->frames[e->count];
    memset(f, 0, sizeof(*f));
    f->width = frame->width;
    f->height = frame->height;
    if (frame->width > 65535 || frame->height > 65535)
        return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    f->indices = WicAlloc((SIZE_T)frame->width * frame->height);
    if (!f->indices)
        return E_OUTOFMEMORY;
    for (UINT y = 0; y < frame->height; y++)
        memcpy(f->indices + (SIZE_T)y * frame->width, frame->pixels + (SIZE_T)y * frame->stride, frame->width);
    UINT n = frame->palette_count;
    if (n > 256)
        n = 256;
    f->palette_size = n;
    f->transparent = FALSE;
    for (UINT i = 0; i < n; i++) {
        WICColor c = frame->palette[i];
        f->palette[i * 3] = (BYTE)(c >> 16);
        f->palette[i * 3 + 1] = (BYTE)(c >> 8);
        f->palette[i * 3 + 2] = (BYTE)c;
        if (!(c >> 24) && !f->transparent) {
            f->transparent = TRUE;
            f->transparent_index = (BYTE)i;
        }
    }
    /* the controls the program set win over what the palette says. */
    IWICMetadataReader *gce = WicBlocksFind(frame->metadata, &GUID_MetadataFormatGCE);
    if (gce) {
        UINT32 v;
        BOOL b;
        if (WicMetaGetU32(gce, L"Delay", &v))
            f->delay = v;
        if (WicMetaGetU32(gce, L"Disposal", &v))
            f->disposal = v & 7;
        if (WicMetaGetBool(gce, L"UserInputFlag", &b))
            f->user_input = b;
        if (WicMetaGetBool(gce, L"TransparencyFlag", &b)) {
            f->transparent = b;
            if (WicMetaGetU32(gce, L"TransparentColorIndex", &v))
                f->transparent_index = (BYTE)v;
        }
        gce->lpVtbl->Release(gce);
    }
    IWICMetadataReader *imd = WicBlocksFind(frame->metadata, &GUID_MetadataFormatIMD);
    if (imd) {
        UINT32 v;
        BOOL b;
        if (WicMetaGetU32(imd, L"Left", &v))
            f->left = v;
        if (WicMetaGetU32(imd, L"Top", &v))
            f->top = v;
        if (WicMetaGetBool(imd, L"InterlaceFlag", &b))
            f->interlace = b;
        imd->lpVtbl->Release(imd);
    }
    e->count++;
    return S_OK;
}

static HRESULT gif_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                          IWICMetadataBlockReader *metadata)
{
    GifEncoder *e = state;
    UINT width = 0, height = 0;
    for (UINT i = 0; i < e->count; i++) {
        UINT w = e->frames[i].left + e->frames[i].width, h = e->frames[i].top + e->frames[i].height;
        if (w > width)
            width = w;
        if (h > height)
            height = h;
    }
    BYTE global[768];
    UINT global_size = 0;
    BYTE background = 0, aspect = 0;
    UINT loop = 0;
    BOOL has_loop = FALSE;
    IWICMetadataReader *lsd = WicBlocksFind(metadata, &GUID_MetadataFormatLSD);
    if (lsd) {
        UINT32 v;
        if (WicMetaGetU32(lsd, L"Width", &v) && v > width)
            width = v;
        if (WicMetaGetU32(lsd, L"Height", &v) && v > height)
            height = v;
        if (WicMetaGetU32(lsd, L"BackgroundColorIndex", &v))
            background = (BYTE)v;
        if (WicMetaGetU32(lsd, L"PixelAspectRatio", &v))
            aspect = (BYTE)v;
        lsd->lpVtbl->Release(lsd);
    }
    (void)aspect;
    if (palette) {
        WICColor colors[256];
        UINT got = 0;
        if (SUCCEEDED(palette->lpVtbl->GetColors(palette, 256, colors, &got)) && got) {
            for (UINT i = 0; i < got; i++) {
                global[i * 3] = (BYTE)(colors[i] >> 16);
                global[i * 3 + 1] = (BYTE)(colors[i] >> 8);
                global[i * 3 + 2] = (BYTE)colors[i];
            }
            global_size = got;
        }
    }
    IWICMetadataReader *ape = WicBlocksFind(metadata, &GUID_MetadataFormatAPE);
    if (ape) {
        BYTE *app, *data;
        UINT app_size, data_size;
        if (WicMetaGetBytes(ape, L"Application", &app, &app_size)) {
            if (app_size >= 8 && !memcmp(app, "NETSCAPE", 8) && WicMetaGetBytes(ape, L"Data", &data, &data_size)) {
                if (data_size >= 4 && data[0] == 3 && data[1] == 1) {
                    loop = data[2] | ((UINT)data[3] << 8);
                    has_loop = TRUE;
                }
                WicFree(data);
            }
            WicFree(app);
        }
        ape->lpVtbl->Release(ape);
    }
    MimeGifEncoder *enc = nullptr;
    int rc = mime_gif_encoder_create(&WicAllocator, width ? width : 1, height ? height : 1, global_size ? global : nullptr,
                                     global_size, background, &enc);
    if (rc != MIME_GIF_OK)
        return rc == MIME_GIF_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    if (has_loop)
        mime_gif_encoder_set_loop(enc, loop);
    IWICMetadataReader *com = WicBlocksFind(metadata, &GUID_MetadataFormatGifComment);
    if (com) {
        char *text;
        if (WicMetaGetAnsi(com, L"TextEntry", &text)) {
            mime_gif_encoder_add_comment(enc, text, strlen(text));
            WicFree(text);
        }
        com->lpVtbl->Release(com);
    }
    for (UINT i = 0; i < e->count && rc == MIME_GIF_OK; i++) {
        GifFrameData *f = &e->frames[i];
        MimeGifEncFrame ef;
        memset(&ef, 0, sizeof(ef));
        ef.indices = f->indices;
        ef.stride = f->width;
        ef.left = (uint16_t)f->left;
        ef.top = (uint16_t)f->top;
        ef.width = (uint16_t)f->width;
        ef.height = (uint16_t)f->height;
        /* a frame with the global palette does not repeat it. */
        BOOL same = global_size && f->palette_size == global_size && !memcmp(f->palette, global, (SIZE_T)global_size * 3);
        if (!same) {
            ef.palette = f->palette;
            ef.palette_size = f->palette_size ? f->palette_size : 2;
        }
        ef.delay = (uint16_t)f->delay;
        ef.disposal = (uint8_t)f->disposal;
        ef.user_input = f->user_input;
        ef.has_transparency = f->transparent;
        ef.transparent_index = f->transparent_index;
        ef.interlace = f->interlace;
        rc = mime_gif_encoder_add_frame(enc, &ef);
    }
    if (rc != MIME_GIF_OK) {
        mime_gif_encoder_free(enc);
        return rc == MIME_GIF_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    }
    uint8_t *out = nullptr;
    size_t out_size = 0;
    rc = mime_gif_encoder_finish(enc, &out, &out_size);
    if (rc != MIME_GIF_OK)
        return rc == MIME_GIF_ERR_NOMEM ? E_OUTOFMEMORY : E_FAIL;
    HRESULT hr = WicWriteStream(e->stream, out, out_size);
    WicFree(out);
    return hr;
}

static void gif_destroy(void *state)
{
    GifEncoder *e = state;
    for (UINT i = 0; i < e->count; i++)
        WicFree(e->frames[i].indices);
    WicFree(e->frames);
    WicFree(e);
}

static BOOL gif_closest(GUID *format)
{
    *format = GUID_WICPixelFormat8bppIndexed;
    return TRUE;
}

const WicEncoderOps wic_enc_gif = {
    .multiframe = TRUE,
    .closest_format = gif_closest,
    .begin = gif_begin,
    .write_frame = gif_write_frame,
    .finish = gif_finish,
    .destroy = gif_destroy,
    .container_palette = TRUE,
    .frame_metadata = TRUE,
    .container_metadata = TRUE,
};
