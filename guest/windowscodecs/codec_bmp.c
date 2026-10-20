/*
 * the bmp codec, and the device independent bitmaps that icons share: decoding of the bits of a bitmap in any of the
 * depths and compressions, and a bmp encoder.
 */
#include "wic_codec.h"
#include "codec_util.h"

#define BI_RGB_ 0
#define BI_RLE8_ 1
#define BI_RLE4_ 2
#define BI_BITFIELDS_ 3

/* ------------------------------------------------------------------ the header of a bitmap */

HRESULT WicDibParse(const BYTE *p, SIZE_T size, BOOL icon, WicDib *dib)
{
    memset(dib, 0, sizeof(*dib));
    if (size < 12)
        return WINCODEC_ERR_BADHEADER;
    UINT header = WicLe32(p);
    INT width, height;
    UINT planes, bpp, compression = BI_RGB_, colors_used = 0, entry = 4;
    UINT masks[4] = {0, 0, 0, 0};
    BOOL has_alpha_mask = FALSE;
    double dpi_x = 96.0, dpi_y = 96.0;
    if (header == 12) {
        width = (INT)WicLe16(p + 4);
        height = (INT)WicLe16(p + 6);
        planes = WicLe16(p + 8);
        bpp = WicLe16(p + 10);
        entry = 3;
    } else if (header >= 40 && header <= 124 && header != 44 && (header == 40 || header >= 52)) {
        if (size < header)
            return WINCODEC_ERR_BADHEADER;
        width = (INT)WicLe32(p + 4);
        height = (INT)WicLe32(p + 8);
        planes = WicLe16(p + 12);
        bpp = WicLe16(p + 14);
        compression = WicLe32(p + 16);
        colors_used = WicLe32(p + 32);
        INT xppm = (INT)WicLe32(p + 24), yppm = (INT)WicLe32(p + 28);
        if (xppm > 0)
            dpi_x = (double)xppm * 0.0254;
        if (yppm > 0)
            dpi_y = (double)yppm * 0.0254;
        if (header >= 52) {
            masks[0] = WicLe32(p + 40);
            masks[1] = WicLe32(p + 44);
            masks[2] = WicLe32(p + 48);
        }
        if (header >= 56) {
            masks[3] = WicLe32(p + 52);
            has_alpha_mask = masks[3] != 0;
        }
    } else {
        return WINCODEC_ERR_BADHEADER;
    }
    if (planes != 1 || width <= 0 || height == 0)
        return WINCODEC_ERR_BADHEADER;
    if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return WINCODEC_ERR_BADHEADER;
    if (bpp == 2)
        return WINCODEC_ERR_BADHEADER;
    BOOL top_down = height < 0;
    UINT abs_height = top_down ? (UINT)(-(INT64)height) : (UINT)height;
    if (icon) {
        abs_height /= 2;
        top_down = FALSE;
    }
    if (!abs_height || (UINT)width > 0x10000 * 2 || abs_height > 0x10000 * 2)
        return WINCODEC_ERR_BADHEADER;
    if (compression == BI_RLE8_ && bpp != 8)
        return WINCODEC_ERR_BADHEADER;
    if (compression == BI_RLE4_ && bpp != 4)
        return WINCODEC_ERR_BADHEADER;
    if (compression > BI_BITFIELDS_)
        return WINCODEC_ERR_BADHEADER;
    if (compression == BI_BITFIELDS_ && bpp != 16 && bpp != 32)
        return WINCODEC_ERR_BADHEADER;
    UINT offset = header;
    if (compression == BI_BITFIELDS_ && header == 40) {
        if (size < 52)
            return WINCODEC_ERR_BADHEADER;
        masks[0] = WicLe32(p + 40);
        masks[1] = WicLe32(p + 44);
        masks[2] = WicLe32(p + 48);
        offset = 52;
        /* the masks of a header of 40 bytes may carry alpha as a fourth, when the palette area has room. */
    }
    dib->width = (UINT)width;
    dib->height = abs_height;
    dib->top_down = top_down;
    dib->bpp = bpp;
    dib->compression = compression;
    dib->dpi_x = dpi_x;
    dib->dpi_y = dpi_y;
    memcpy(dib->masks, masks, sizeof(masks));
    UINT max_colors = bpp <= 8 ? 1u << bpp : 0;
    if (bpp <= 8) {
        UINT count = colors_used ? colors_used : max_colors;
        if (count > max_colors)
            count = max_colors;
        dib->palette_count = count;
        UINT palette_bytes = count * entry;
        if (offset + palette_bytes > size)
            return WINCODEC_ERR_BADIMAGE;
        for (UINT i = 0; i < count; i++) {
            const BYTE *e = p + offset + i * entry;
            dib->palette[i] = 0xff000000u | ((UINT)e[2] << 16) | ((UINT)e[1] << 8) | e[0];
        }
        dib->bits_offset = offset + palette_bytes;
    } else {
        dib->bits_offset = offset + (compression == BI_BITFIELDS_ && header == 40 ? 0 : 0);
        if (header == 40 && compression == BI_BITFIELDS_)
            dib->bits_offset = 52;
    }
    /* the pixel format the bits are given in. */
    const GUID *format;
    if (bpp == 1)
        format = &GUID_WICPixelFormat1bppIndexed;
    else if (bpp == 4)
        format = &GUID_WICPixelFormat4bppIndexed;
    else if (bpp == 8)
        format = &GUID_WICPixelFormat8bppIndexed;
    else if (bpp == 24)
        format = &GUID_WICPixelFormat24bppBGR;
    else if (bpp == 16) {
        if (compression == BI_RGB_ || (masks[0] == 0x7c00 && masks[1] == 0x3e0 && masks[2] == 0x1f))
            format = &GUID_WICPixelFormat16bppBGR555;
        else if (masks[0] == 0xf800 && masks[1] == 0x7e0 && masks[2] == 0x1f)
            format = &GUID_WICPixelFormat16bppBGR565;
        else
            format = has_alpha_mask ? &GUID_WICPixelFormat32bppBGRA : &GUID_WICPixelFormat32bppBGR;
    } else {
        if (has_alpha_mask && compression == BI_BITFIELDS_)
            format = &GUID_WICPixelFormat32bppBGRA;
        else
            format = &GUID_WICPixelFormat32bppBGR;
    }
    dib->format = *format;
    if (compression == BI_BITFIELDS_ && bpp == 32 && !masks[0] && !masks[1] && !masks[2])
        return WINCODEC_ERR_BADHEADER;
    return S_OK;
}

/* ------------------------------------------------------------------ the bits */

static UINT mask_shift(UINT mask)
{
    UINT shift = 0;
    while (mask && !(mask & 1)) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static UINT mask_bits(UINT mask)
{
    UINT bits = 0;
    mask >>= mask_shift(mask);
    while (mask & 1) {
        mask >>= 1;
        bits++;
    }
    return bits;
}

static BYTE scale_to_8(UINT value, UINT bits)
{
    if (!bits)
        return 0;
    if (bits >= 8)
        return (BYTE)(value >> (bits - 8));
    UINT v = value << (8 - bits);
    /* repeat the bits to fill the byte. */
    for (UINT filled = bits; filled < 8; filled += bits)
        v |= v >> filled;
    return (BYTE)v;
}

static HRESULT decode_rle(const WicDib *dib, const BYTE *data, SIZE_T size, BYTE *dest, UINT stride)
{
    UINT width = dib->width, height = dib->height;
    BOOL four = dib->compression == BI_RLE4_;
    UINT x = 0, y = 0;
    SIZE_T i = 0;
    memset(dest, 0, (SIZE_T)stride * height);
    /* rows come in bottom up unless the bitmap says otherwise. */
    #define ROW(yy) (dest + (SIZE_T)(dib->top_down ? (yy) : height - 1 - (yy)) * stride)
    #define PUT(xx, yy, v)                                                                                             \
        do {                                                                                                           \
            if ((xx) < width && (yy) < height) {                                                                       \
                BYTE *row_ = ROW(yy);                                                                                  \
                if (four) {                                                                                            \
                    if ((xx) & 1)                                                                                      \
                        row_[(xx) / 2] = (BYTE)((row_[(xx) / 2] & 0xf0) | ((v) & 15));                                 \
                    else                                                                                               \
                        row_[(xx) / 2] = (BYTE)((row_[(xx) / 2] & 0x0f) | ((v) << 4));                                 \
                } else {                                                                                               \
                    row_[xx] = (BYTE)(v);                                                                              \
                }                                                                                                      \
            }                                                                                                          \
        } while (0)
    while (i + 1 < size && y < height) {
        UINT count = data[i], value = data[i + 1];
        i += 2;
        if (count) {
            for (UINT k = 0; k < count; k++, x++) {
                UINT v = four ? ((k & 1) ? (value & 15) : (value >> 4)) : value;
                PUT(x, y, v);
            }
            continue;
        }
        if (value == 0) {
            x = 0;
            y++;
        } else if (value == 1) {
            break;
        } else if (value == 2) {
            if (i + 1 >= size)
                break;
            x += data[i];
            y += data[i + 1];
            i += 2;
        } else {
            UINT n = value;
            UINT bytes = four ? (n + 1) / 2 : n;
            if (i + bytes > size)
                return WINCODEC_ERR_BADIMAGE;
            for (UINT k = 0; k < n; k++, x++) {
                UINT v = four ? ((k & 1) ? (data[i + k / 2] & 15) : (data[i + k / 2] >> 4)) : data[i + k];
                PUT(x, y, v);
            }
            i += bytes;
            if (bytes & 1)
                i++;
        }
    }
    #undef PUT
    #undef ROW
    return S_OK;
}

HRESULT WicDibDecode(const WicDib *dib, const BYTE *bits, SIZE_T bits_size, BYTE *dest, UINT stride)
{
    UINT width = dib->width, height = dib->height;
    if (dib->compression == BI_RLE8_ || dib->compression == BI_RLE4_)
        return decode_rle(dib, bits, bits_size, dest, stride);
    UINT src_stride = (UINT)(((UINT64)width * dib->bpp + 31) / 32 * 4);
    if ((UINT64)src_stride * height > bits_size)
        return WINCODEC_ERR_BADIMAGE;
    UINT dest_bits = WicFormatBits(&dib->format);
    UINT out_row = (width * dest_bits + 7) / 8;
    for (UINT y = 0; y < height; y++) {
        const BYTE *src = bits + (SIZE_T)(dib->top_down ? y : height - 1 - y) * src_stride;
        BYTE *dst = dest + (SIZE_T)y * stride;
        if (dib->bpp <= 8 || dib->bpp == 24 || (dib->bpp == 16 && dest_bits == 16)) {
            memcpy(dst, src, out_row);
            continue;
        }
        if (dib->bpp == 32 && IsEqualGUID(&dib->format, &GUID_WICPixelFormat32bppBGR) && dib->compression == BI_RGB_) {
            memcpy(dst, src, out_row);
            continue;
        }
        /* masks: a 16 or 32 bit pixel to four bytes. */
        UINT rm = dib->masks[0], gm = dib->masks[1], bm = dib->masks[2], am = dib->masks[3];
        if (dib->compression == BI_RGB_) {
            rm = 0xff0000;
            gm = 0xff00;
            bm = 0xff;
            am = 0;
        }
        UINT rs = mask_shift(rm), gs = mask_shift(gm), bs = mask_shift(bm), as = mask_shift(am);
        UINT rb = mask_bits(rm), gb = mask_bits(gm), bb = mask_bits(bm), ab = mask_bits(am);
        BOOL alpha = IsEqualGUID(&dib->format, &GUID_WICPixelFormat32bppBGRA);
        if (dib->bpp == 32 && rm == 0xff0000 && gm == 0xff00 && bm == 0xff && (!alpha || am == 0xff000000)) {
            memcpy(dst, src, out_row);
            if (!alpha) {
                for (UINT x = 0; x < width; x++)
                    dst[x * 4 + 3] = 255;
            }
            continue;
        }
        for (UINT x = 0; x < width; x++) {
            UINT v = dib->bpp == 16 ? WicLe16(src + x * 2) : WicLe32(src + x * 4);
            dst[x * 4] = scale_to_8((v & bm) >> bs, bb);
            dst[x * 4 + 1] = scale_to_8((v & gm) >> gs, gb);
            dst[x * 4 + 2] = scale_to_8((v & rm) >> rs, rb);
            dst[x * 4 + 3] = alpha ? scale_to_8((v & am) >> as, ab) : 255;
        }
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the decoder */

typedef struct BmpState {
    const BYTE *data;
    SIZE_T size;
    WicDib dib;
    const BYTE *bits;
    SIZE_T bits_size;
} BmpState;

static HRESULT bmp_open(const BYTE *data, SIZE_T size, void **out)
{
    if (size < 2 || data[0] != 'B' || data[1] != 'M')
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    if (size < 14 + 12)
        return WINCODEC_ERR_BADHEADER;
    UINT file_size = WicLe32(data + 2), off_bits = WicLe32(data + 10);
    (void)file_size;
    BmpState *s = WicAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->data = data;
    s->size = size;
    HRESULT hr = WicDibParse(data + 14, size - 14, FALSE, &s->dib);
    if (FAILED(hr)) {
        WicFree(s);
        return hr;
    }
    UINT start = 14 + s->dib.bits_offset;
    if (off_bits >= start && off_bits <= size)
        start = off_bits;
    else if (off_bits > size) {
        WicFree(s);
        return WINCODEC_ERR_BADIMAGE;
    }
    s->bits = data + start;
    s->bits_size = size - start;
    UINT src_stride = (UINT)(((UINT64)s->dib.width * s->dib.bpp + 31) / 32 * 4);
    if (s->dib.compression <= BI_BITFIELDS_ && s->dib.compression != BI_RLE8_ && s->dib.compression != BI_RLE4_ &&
        (UINT64)src_stride * s->dib.height > s->bits_size) {
        WicFree(s);
        return WINCODEC_ERR_BADIMAGE;
    }
    *out = s;
    return S_OK;
}

static void bmp_close(void *state) { WicFree(state); }
static UINT bmp_frame_count(void *state) { return 1; }

static HRESULT bmp_frame_info(void *state, UINT index, WicFrameInfo *info)
{
    BmpState *s = state;
    info->width = s->dib.width;
    info->height = s->dib.height;
    info->format = s->dib.format;
    info->dpi_x = s->dib.dpi_x;
    info->dpi_y = s->dib.dpi_y;
    info->has_palette = s->dib.bpp <= 8;
    info->palette = s->dib.palette;
    info->palette_count = s->dib.palette_count;
    return S_OK;
}

static HRESULT bmp_frame_decode(void *state, UINT index, BYTE *dest, UINT stride)
{
    BmpState *s = state;
    return WicDibDecode(&s->dib, s->bits, s->bits_size, dest, stride);
}

const WicDecoderOps wic_dec_bmp = {
    .open = bmp_open,
    .close = bmp_close,
    .frame_count = bmp_frame_count,
    .frame_info = bmp_frame_info,
    .frame_decode = bmp_frame_decode,
};

/* ------------------------------------------------------------------ the encoder */

static const WicOptionDesc g_bmp_options[] = {
    {L"EnableV5Header32bppBGRA", VT_BOOL, 0, 0, 1},
};

typedef struct BmpEncoder {
    IStream *stream;
} BmpEncoder;

static HRESULT bmp_begin(IStream *stream, void **state)
{
    BmpEncoder *e = WicAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->stream = stream;
    *state = e;
    return S_OK;
}

static void put32(BYTE *p, UINT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    p[2] = (BYTE)(v >> 16);
    p[3] = (BYTE)(v >> 24);
}

static void put16(BYTE *p, UINT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static HRESULT bmp_write_frame(void *state, UINT index, WicEncFrame *frame)
{
    BmpEncoder *e = state;
    const GUID *fmt = &frame->format;
    UINT bpp = WicFormatBits(fmt);
    BOOL v5 = IsEqualGUID(fmt, &GUID_WICPixelFormat32bppBGRA) && frame->options_set && frame->options[0] != 0;
    BOOL bitfields = IsEqualGUID(fmt, &GUID_WICPixelFormat16bppBGR565);
    BYTE *pixels = frame->pixels;
    BOOL owned = FALSE;
    if (IsEqualGUID(fmt, &GUID_WICPixelFormat32bppPBGRA)) {
        /* straight alpha is what the file holds. */
        UINT row = frame->width * 4;
        pixels = WicAlloc((SIZE_T)row * frame->height);
        if (!pixels)
            return E_OUTOFMEMORY;
        owned = TRUE;
        WicConvertRows(fmt, &GUID_WICPixelFormat32bppBGRA, frame->pixels, frame->stride, pixels, row, frame->width,
                       frame->height, nullptr, 0, nullptr, 0, WICBitmapDitherTypeNone, 0, 0, 0.0);
        frame->stride = row;
        bpp = 32;
        v5 = FALSE;
    }
    UINT header = v5 ? 124 : 40;
    UINT palette = bpp <= 8 ? 4 * frame->palette_count : 0;
    UINT masks = bitfields ? 12 : 0;
    UINT row_bytes = (frame->width * bpp + 7) / 8;
    UINT out_stride = (row_bytes + 3) & ~3u;
    UINT64 data_size = (UINT64)out_stride * frame->height;
    UINT64 total = 14 + (UINT64)header + masks + palette + data_size;
    if (total > 0xFFFFFFFFull) {
        if (owned)
            WicFree(pixels);
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    }
    BYTE *file = WicAllocZero((SIZE_T)total);
    if (!file) {
        if (owned)
            WicFree(pixels);
        return E_OUTOFMEMORY;
    }
    file[0] = 'B';
    file[1] = 'M';
    put32(file + 2, (UINT)total);
    put32(file + 10, 14 + header + masks + palette);
    BYTE *h = file + 14;
    put32(h, header);
    put32(h + 4, frame->width);
    put32(h + 8, frame->height);
    put16(h + 12, 1);
    put16(h + 14, bpp);
    put32(h + 16, (bitfields || v5) ? BI_BITFIELDS_ : BI_RGB_);
    put32(h + 20, (UINT)data_size);
    put32(h + 24, (UINT)(frame->dpi_x / 0.0254 + 0.5));
    put32(h + 28, (UINT)(frame->dpi_y / 0.0254 + 0.5));
    put32(h + 32, bpp <= 8 ? frame->palette_count : 0);
    if (v5) {
        put32(h + 40, 0x00ff0000);
        put32(h + 44, 0x0000ff00);
        put32(h + 48, 0x000000ff);
        put32(h + 52, 0xff000000);
        put32(h + 56, 0x73524742); /* 'sRGB'. */
    }
    BYTE *p = h + header;
    if (bitfields) {
        put32(p, 0xf800);
        put32(p + 4, 0x07e0);
        put32(p + 8, 0x001f);
        p += 12;
    }
    for (UINT i = 0; bpp <= 8 && i < frame->palette_count; i++, p += 4) {
        WICColor c = frame->palette[i];
        p[0] = (BYTE)c;
        p[1] = (BYTE)(c >> 8);
        p[2] = (BYTE)(c >> 16);
        p[3] = 0;
    }
    for (UINT y = 0; y < frame->height; y++)
        memcpy(p + (SIZE_T)(frame->height - 1 - y) * out_stride, pixels + (SIZE_T)y * frame->stride, row_bytes);
    HRESULT hr = WicWriteStream(e->stream, file, (SIZE_T)total);
    WicFree(file);
    if (owned)
        WicFree(pixels);
    return hr;
}

static HRESULT bmp_finish(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                           IWICMetadataBlockReader *metadata) { return S_OK; }
static void bmp_destroy(void *state) { WicFree(state); }

const WicEncoderOps wic_enc_bmp = {
    .options = g_bmp_options,
    .option_count = WIC_ARRAY_SIZE(g_bmp_options),
    .begin = bmp_begin,
    .write_frame = bmp_write_frame,
    .finish = bmp_finish,
    .destroy = bmp_destroy,
};
