/* ICO and CUR decoder: each directory entry becomes a 32-bit frame from a masked bitmap or PNG. */
#include "wic_codec.h"
#include "codec_util.h"
#include <pngio/pngio.h>

typedef struct IconEntry {
    UINT width, height;
    UINT offset, size;
    BOOL png;
} IconEntry;

typedef struct IconState {
    const BYTE *data;
    SIZE_T size;
    UINT count;
    IconEntry *entries;
} IconState;

static HRESULT icon_open_type(const BYTE *data, SIZE_T size, UINT type, void **out)
{
    if (size < 6 || WicLe16(data) != 0 || WicLe16(data + 2) != type)
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    UINT count = WicLe16(data + 4);
    if (!count || 6 + (SIZE_T)count * 16 > size)
        return WINCODEC_ERR_BADHEADER;
    IconState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->data = data;
    s->size = size;
    s->entries = WicAllocZero((SIZE_T)count * sizeof(IconEntry));
    if (!s->entries) {
        WicFree(s);
        return E_OUTOFMEMORY;
    }
    UINT kept = 0;
    for (UINT i = 0; i < count; i++) {
        const BYTE *e = data + 6 + (SIZE_T)i * 16;
        UINT bytes = WicLe32(e + 8), offset = WicLe32(e + 12);
        if (offset >= size || bytes > size - offset || bytes < 8)
            continue;
        IconEntry *entry = &s->entries[kept];
        entry->offset = offset;
        entry->size = bytes;
        entry->png = mime_pngio_is_png(data + offset, bytes);
        if (entry->png) {
            MimePngioDecoder *dec;
            if (mime_pngio_open(data + offset, bytes, &WicAllocator, 0, &dec) != MIME_PNGIO_OK)
                continue;
            entry->width = mime_pngio_info(dec)->width;
            entry->height = mime_pngio_info(dec)->height;
            mime_pngio_close(dec);
        } else {
            WicDib dib;
            if (FAILED(WicDibParse(data + offset, bytes, TRUE, &dib)))
                continue;
            entry->width = dib.width;
            entry->height = dib.height;
        }
        kept++;
    }
    if (!kept) {
        WicFree(s->entries);
        WicFree(s);
        return WINCODEC_ERR_BADIMAGE;
    }
    s->count = kept;
    *out = s;
    return S_OK;
}

static HRESULT ico_open(const BYTE *data, SIZE_T size, void **out) { return icon_open_type(data, size, 1, out); }
static HRESULT cur_open(const BYTE *data, SIZE_T size, void **out) { return icon_open_type(data, size, 2, out); }

static void icon_close(void *state)
{
    IconState *s = state;
    WicFree(s->entries);
    WicFree(s);
}

static UINT icon_frame_count(void *state) { return ((IconState *)state)->count; }

static HRESULT icon_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    IconState *s = state;
    if (index >= s->count)
        return WINCODEC_ERR_FRAMEMISSING;
    info->width = s->entries[index].width;
    info->height = s->entries[index].height;
    info->format = GUID_WICPixelFormat32bppBGRA;
    info->dpi_x = info->dpi_y = 96.0;
    return S_OK;
}

static HRESULT decode_png(const IconEntry *e, const BYTE *data, BYTE *dest, UINT stride)
{
    MimePngioDecoder *dec;
    if (mime_pngio_open(data, e->size, &WicAllocator, 0, &dec) != MIME_PNGIO_OK)
        return WINCODEC_ERR_BADIMAGE;
    const MimePngioInfo *info = mime_pngio_info(dec);
    int format = mime_pngio_format(dec);
    UINT row = (UINT)mime_pngio_row_bytes(format, info->width);
    BYTE *native = WicAlloc((SIZE_T)row * info->height);
    HRESULT hr = native ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && mime_pngio_decode(dec, native, row) != MIME_PNGIO_OK)
        hr = WINCODEC_ERR_BADIMAGE;
    if (SUCCEEDED(hr)) {
        static const struct { int png; const GUID *wic; } map[] = {
            {MIME_PNGIO_FMT_GRAY1, &GUID_WICPixelFormatBlackWhite},   {MIME_PNGIO_FMT_GRAY2, &GUID_WICPixelFormat2bppGray},
            {MIME_PNGIO_FMT_GRAY4, &GUID_WICPixelFormat4bppGray},     {MIME_PNGIO_FMT_GRAY8, &GUID_WICPixelFormat8bppGray},
            {MIME_PNGIO_FMT_GRAY16, &GUID_WICPixelFormat16bppGray},   {MIME_PNGIO_FMT_INDEXED1, &GUID_WICPixelFormat1bppIndexed},
            {MIME_PNGIO_FMT_INDEXED2, &GUID_WICPixelFormat2bppIndexed}, {MIME_PNGIO_FMT_INDEXED4, &GUID_WICPixelFormat4bppIndexed},
            {MIME_PNGIO_FMT_INDEXED8, &GUID_WICPixelFormat8bppIndexed}, {MIME_PNGIO_FMT_BGR24, &GUID_WICPixelFormat24bppBGR},
            {MIME_PNGIO_FMT_RGB48, &GUID_WICPixelFormat48bppRGB},     {MIME_PNGIO_FMT_BGRA32, &GUID_WICPixelFormat32bppBGRA},
            {MIME_PNGIO_FMT_RGBA64, &GUID_WICPixelFormat64bppRGBA},
        };
        const GUID *from = nullptr;
        for (UINT i = 0; i < WIC_ARRAY_SIZE(map); i++) {
            if (map[i].png == format)
                from = map[i].wic;
        }
        WICColor palette[256];
        UINT count = 0;
        if (info->palette_count) {
            count = info->palette_count;
            for (UINT i = 0; i < count; i++)
                palette[i] = ((UINT)info->palette_alpha[i] << 24) | ((UINT)info->palette[i][0] << 16) |
                             ((UINT)info->palette[i][1] << 8) | info->palette[i][2];
        }
        hr = from ? WicConvertRows(from, &GUID_WICPixelFormat32bppBGRA, native, row, dest, stride, info->width,
                                   info->height, palette, count, nullptr, 0, WICBitmapDitherTypeNone, 0, 0, 0.0)
                  : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    WicFree(native);
    mime_pngio_close(dec);
    return hr;
}

static HRESULT decode_dib(const IconEntry *e, const BYTE *data, BYTE *dest, UINT stride)
{
    WicDib dib;
    HRESULT hr = WicDibParse(data, e->size, TRUE, &dib);
    if (FAILED(hr))
        return hr;
    UINT w = dib.width, h = dib.height;
    UINT xor_stride = (UINT)(((UINT64)w * dib.bpp + 31) / 32 * 4);
    UINT64 xor_bytes = (UINT64)xor_stride * h;
    if (dib.bits_offset + xor_bytes > e->size)
        return WINCODEC_ERR_BADIMAGE;
    const BYTE *bits = data + dib.bits_offset;
    UINT row = (w * WicFormatBits(&dib.format) + 7) / 8;
    BYTE *native = WicAlloc((SIZE_T)row * h);
    if (!native)
        return E_OUTOFMEMORY;
    hr = WicDibDecode(&dib, bits, e->size - dib.bits_offset, native, row);
    if (FAILED(hr)) {
        WicFree(native);
        return hr;
    }
    hr = WicConvertRows(&dib.format, &GUID_WICPixelFormat32bppBGRA, native, row, dest, stride, w, h, dib.palette,
                        dib.palette_count, nullptr, 0, WICBitmapDitherTypeNone, 0, 0, 0.0);
    WicFree(native);
    if (FAILED(hr))
        return hr;
    BOOL has_alpha = FALSE;
    if (dib.bpp == 32) {
        /* use the high byte as alpha if any pixel has nonzero alpha. */
        for (UINT y = 0; y < h && !has_alpha; y++) {
            const BYTE *line = bits + (SIZE_T)y * xor_stride;
            for (UINT x = 0; x < w; x++) {
                if (line[x * 4 + 3]) {
                    has_alpha = TRUE;
                    break;
                }
            }
        }
        if (has_alpha) {
            for (UINT y = 0; y < h; y++) {
                const BYTE *line = bits + (SIZE_T)(h - 1 - y) * xor_stride;
                for (UINT x = 0; x < w; x++)
                    dest[(SIZE_T)y * stride + x * 4 + 3] = line[x * 4 + 3];
            }
        }
    }
    /* the mask follows the colors, a set bit is transparent. */
    UINT mask_stride = ((w + 31) / 32) * 4;
    UINT64 mask_end = dib.bits_offset + xor_bytes + (UINT64)mask_stride * h;
    BOOL has_mask = mask_end <= e->size;
    const BYTE *mask = bits + xor_bytes;
    for (UINT y = 0; y < h; y++) {
        BYTE *d = dest + (SIZE_T)y * stride;
        const BYTE *m = mask + (SIZE_T)(h - 1 - y) * mask_stride;
        for (UINT x = 0; x < w; x++) {
            if (has_alpha)
                continue;
            BOOL transparent = has_mask && (m[x / 8] & (0x80 >> (x % 8)));
            d[x * 4 + 3] = transparent ? 0 : 255;
            if (transparent)
                d[x * 4] = d[x * 4 + 1] = d[x * 4 + 2] = 0;
        }
    }
    return S_OK;
}

static HRESULT icon_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    IconState *s = state;
    const IconEntry *e = &s->entries[index];
    const BYTE *data = s->data + e->offset;
    return e->png ? decode_png(e, data, dest, stride) : decode_dib(e, data, dest, stride);
}

const WicDecoderOps wic_dec_ico = {
    .open = ico_open,
    .close = icon_close,
    .frame_count = icon_frame_count,
    .frame_info = icon_frame_info,
    .frame_decode = icon_frame_decode,
};

const WicDecoderOps wic_dec_cur = {
    .open = cur_open,
    .close = icon_close,
    .frame_count = icon_frame_count,
    .frame_info = icon_frame_info,
    .frame_decode = icon_frame_decode,
};
