/*
 * bitmaps for pictures: the files of the bmp format, checked against the rules of the format, and the images the gif
 * and jpeg decoders hand over. Both end as device independent sections, so a program can reach the pixels.
 */
#include "picture.h"

#define BMP_FILE_HEADER 14
#define BMP_CORE_HEADER 12
#define BMP_INFO_HEADER 40
#define BMP_MIN_READ 54
#define BMP_MAX_SIDE 0x7FFF
#define COLOR_LIMIT 256

#define COMPRESSION_NONE 0
#define COMPRESSION_RLE8 1
#define COMPRESSION_RLE4 2
#define COMPRESSION_MASKS 3

static ULONG get_u32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

static UINT get_u16(const BYTE *p)
{
    return p[0] | p[1] << 8;
}

static ULONG row_bytes(ULONG width, ULONG bits)
{
    return (ULONG)(((ULONGLONG)width * bits + 31) / 32 * 4);
}

/* a section of the given shape, with the colors of its table when it has one. */
static HBITMAP make_section(LONG width, LONG height, WORD bits, const RGBQUAD *colors, ULONG color_count, BYTE **pixels)
{
    BYTE storage[sizeof(BITMAPINFOHEADER) + COLOR_LIMIT * sizeof(RGBQUAD)] = {};
    BITMAPINFO *info = (BITMAPINFO *)storage;
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = width;
    info->bmiHeader.biHeight = height;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = bits;
    if (bits <= 8) {
        info->bmiHeader.biClrUsed = color_count;
        memcpy(info->bmiColors, colors, color_count * sizeof(RGBQUAD));
    }
    *pixels = nullptr;
    HBITMAP section = CreateDIBSection(nullptr, info, DIB_RGB_COLORS, (void **)pixels, nullptr, 0);
    return section && *pixels ? section : nullptr;
}

/* ------------------------------------------------------------------ run length encoding */

typedef struct RLE_TARGET {
    BYTE *Rows;
    ULONG Stride;
    LONG Width, Height;
    BOOL Nibbles;
} RLE_TARGET;

static void rle_put(RLE_TARGET *t, LONG x, LONG y, UINT index)
{
    if (x < 0 || x >= t->Width || y < 0 || y >= t->Height)
        return;
    BYTE *at = t->Rows + (ULONGLONG)y * t->Stride;
    if (!t->Nibbles)
        at[x] = (BYTE)index;
    else
        at[x / 2] = (BYTE)(x & 1 ? (at[x / 2] & 0xF0) | (index & 15) : (at[x / 2] & 0x0F) | (index << 4));
}

/* the pixels in file order: the first row of the data is row 0. */
static void rle_decode(RLE_TARGET *t, const BYTE *data, ULONG size)
{
    LONG x = 0, y = 0;
    ULONG at = 0;
    while (at + 1 < size) {
        UINT count = data[at], value = data[at + 1];
        at += 2;
        if (count) {
            for (UINT i = 0; i < count; i++)
                rle_put(t, x + (LONG)i, y, !t->Nibbles ? value : (i & 1 ? value & 15 : value >> 4));
            x += (LONG)count;
        } else if (value == 0) {
            x = 0;
            y++;
        } else if (value == 1) {
            return;
        } else if (value == 2) {
            if (at + 1 >= size)
                return;
            x += data[at];
            y += data[at + 1];
            at += 2;
        } else {
            ULONG bytes = t->Nibbles ? (value + 1) / 2 : value;
            if (at + bytes > size)
                bytes = size - at;
            for (UINT i = 0; i < value && i / (t->Nibbles ? 2 : 1) < bytes; i++) {
                BYTE b = data[at + (t->Nibbles ? i / 2 : i)];
                rle_put(t, x + (LONG)i, y, !t->Nibbles ? b : (i & 1 ? b & 15 : b >> 4));
            }
            x += (LONG)value;
            at += (bytes + 1) & ~1u;
        }
    }
}

/* ------------------------------------------------------------------ channels */

typedef struct CHANNEL {
    ULONG Mask;
    int Shift, Bits;
} CHANNEL;

static CHANNEL channel_of(ULONG mask)
{
    CHANNEL c = {mask, 0, 0};
    if (!mask)
        return c;
    while (!((mask >> c.Shift) & 1))
        c.Shift++;
    while ((mask >> (c.Shift + c.Bits)) & 1 && c.Shift + c.Bits < 32)
        c.Bits++;
    return c;
}

/* the value of a channel scaled up to a byte. */
static BYTE channel_value(const CHANNEL *c, ULONG pixel)
{
    if (!c->Bits)
        return 0;
    ULONG v = (pixel & c->Mask) >> c->Shift;
    ULONG top = (1u << c->Bits) - 1;
    return (BYTE)((v * 255 + top / 2) / top);
}

/* ------------------------------------------------------------------ the file */

typedef struct BMP_SHAPE {
    LONG Width, Height;
    BOOL TopDown;
    WORD Bits;
    ULONG Compression;
    ULONG ColorCount;
    BOOL Core;
    ULONG BitsOffset;
    ULONG ImageSize;
} BMP_SHAPE;

/* the colors of the table that follows the header, as quads. */
static void read_table(const BYTE *data, const BMP_SHAPE *shape, ULONG header, RGBQUAD *colors)
{
    const BYTE *at = data + BMP_FILE_HEADER + header;
    for (ULONG i = 0; i < shape->ColorCount; i++) {
        colors[i].rgbBlue = at[0];
        colors[i].rgbGreen = at[1];
        colors[i].rgbRed = at[2];
        colors[i].rgbReserved = 0;
        at += shape->Core ? 3 : 4;
    }
}

/* the pixels of a file with 16 or 32 bits and masks, or 16 bits without, as a section of 24 or 32 bits. */
static HBITMAP convert_wide(const BYTE *rows, const BMP_SHAPE *shape, const BYTE *masks, LONG height_signed)
{
    ULONG source_stride = row_bytes(shape->Width, shape->Bits);
    WORD out_bits = shape->Bits == 32 ? 32 : 24;
    BYTE *pixels;
    HBITMAP section = make_section(shape->Width, height_signed, out_bits, nullptr, 0, &pixels);
    if (!section)
        return nullptr;
    CHANNEL red, green, blue;
    if (masks) {
        red = channel_of(get_u32(masks));
        green = channel_of(get_u32(masks + 4));
        blue = channel_of(get_u32(masks + 8));
    } else {
        red = channel_of(0x7C00);
        green = channel_of(0x03E0);
        blue = channel_of(0x001F);
    }
    ULONG out_stride = row_bytes(shape->Width, out_bits);
    for (LONG y = 0; y < shape->Height; y++) {
        const BYTE *from = rows + (ULONGLONG)y * source_stride;
        BYTE *to = pixels + (ULONGLONG)y * out_stride;
        for (LONG x = 0; x < shape->Width; x++) {
            if (shape->Bits == 32 && !masks) {
                memcpy(to + x * 4, from + x * 4, 4);
                continue;
            }
            ULONG pixel = shape->Bits == 16 ? get_u16(from + x * 2) : get_u32(from + x * 4);
            BYTE *out = to + x * (out_bits / 8);
            out[0] = channel_value(&blue, pixel);
            out[1] = channel_value(&green, pixel);
            out[2] = channel_value(&red, pixel);
            if (out_bits == 32)
                out[3] = 0;
        }
    }
    return section;
}

/* checks the header and works out the shape. CTL_E_INVALIDPICTURE for what the format does not allow. */
static HRESULT read_shape(const BYTE *data, ULONG length, BMP_SHAPE *shape, ULONG *header_size, ULONG *table_bytes)
{
    ULONG hsize = length - BMP_FILE_HEADER;
    ULONG dsize = get_u32(data + BMP_FILE_HEADER);
    if (hsize < BMP_CORE_HEADER)
        return CTL_E_INVALIDPICTURE;
    const BYTE *h = data + BMP_FILE_HEADER;
    memset(shape, 0, sizeof(*shape));
    shape->BitsOffset = get_u32(data + 10);
    ULONG table = 0, colors = 0;

    if (dsize == BMP_INFO_HEADER && hsize >= BMP_INFO_HEADER) {
        LONG width = (LONG)get_u32(h + 4), height = (LONG)get_u32(h + 8);
        if (width < 0 || (ULONG)height == 0x80000000)
            return CTL_E_INVALIDPICTURE;
        if (get_u16(h + 12) != 1)
            return CTL_E_INVALIDPICTURE;
        WORD bits = (WORD)get_u16(h + 14);
        if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
            return CTL_E_INVALIDPICTURE;
        shape->Width = width;
        shape->TopDown = height < 0;
        shape->Height = height < 0 ? -height : height;
        shape->Bits = bits;
        shape->Compression = get_u32(h + 16);
        shape->ImageSize = get_u32(h + 20);
        ULONG used = get_u32(h + 32);
        if (bits == 16 || bits == 24)
            used = 0;
        if (bits <= 8) {
            if (used > (1u << bits))
                return CTL_E_INVALIDPICTURE;
            colors = used ? used : 1u << bits;
        } else if (bits == 32) {
            colors = used;
        }
        if (colors > COLOR_LIMIT)
            colors = COLOR_LIMIT;
        table = colors * 4;
        if (bits == 16 && shape->Compression == COMPRESSION_MASKS)
            table = 12;
        *header_size = BMP_INFO_HEADER;
    } else if (dsize == BMP_CORE_HEADER) {
        shape->Core = TRUE;
        shape->Width = (LONG)get_u16(h + 4);
        shape->Height = (LONG)get_u16(h + 6);
        if (get_u16(h + 8) != 1)
            return CTL_E_INVALIDPICTURE;
        WORD bits = (WORD)get_u16(h + 10);
        if (bits != 1 && bits != 4 && bits != 8 && bits != 24)
            return CTL_E_INVALIDPICTURE;
        shape->Bits = bits;
        if (bits <= 8) {
            colors = 1u << bits;
            table = colors * 3;
        }
        *header_size = BMP_CORE_HEADER;
    } else {
        return CTL_E_INVALIDPICTURE;
    }
    shape->ColorCount = colors;
    *table_bytes = table;

    if ((LONGLONG)table + *header_size > (LONGLONG)shape->BitsOffset - BMP_FILE_HEADER)
        return CTL_E_INVALIDPICTURE;

    if (!shape->Core) {
        switch (shape->Compression) {
        case COMPRESSION_NONE:
        case COMPRESSION_MASKS: break;
        case COMPRESSION_RLE8:
            if (shape->Bits != 8 || shape->TopDown)
                return CTL_E_INVALIDPICTURE;
            break;
        case COMPRESSION_RLE4:
            if (shape->Bits != 4 || shape->TopDown)
                return CTL_E_INVALIDPICTURE;
            break;
        default: return CTL_E_INVALIDPICTURE;
        }
    }
    if (shape->Compression == COMPRESSION_RLE8 || shape->Compression == COMPRESSION_RLE4) {
        if (!shape->ImageSize || shape->ImageSize > length - shape->BitsOffset)
            return CTL_E_INVALIDPICTURE;
    } else {
        ULONGLONG need = (ULONGLONG)row_bytes(shape->Width, shape->Bits) * shape->Height + (shape->BitsOffset - BMP_FILE_HEADER);
        if (need > hsize)
            return CTL_E_INVALIDPICTURE;
    }
    return S_OK;
}

HRESULT PicFromBitmapFile(const BYTE *data, ULONG got, ULONG length, PIC_IMAGE *image)
{
    if (got < BMP_MIN_READ)
        return E_UNEXPECTED;
    if (length < BMP_FILE_HEADER)
        return E_INVALIDARG;
    if (got < length)
        return E_UNEXPECTED;
    if (get_u32(data + 10) > length)
        return E_INVALIDARG;

    BMP_SHAPE shape;
    ULONG header_size, table_bytes;
    HRESULT hr = read_shape(data, length, &shape, &header_size, &table_bytes);
    if (FAILED(hr))
        return hr;
    if (shape.Width > BMP_MAX_SIDE || shape.Height > BMP_MAX_SIDE)
        return CTL_E_INVALIDPICTURE;

    RGBQUAD colors[COLOR_LIMIT] = {};
    if (shape.Bits <= 8 || (shape.Bits == 32 && shape.ColorCount))
        read_table(data, &shape, header_size, colors);
    const BYTE *rows = data + shape.BitsOffset;
    LONG signed_height = shape.TopDown ? -shape.Height : shape.Height;
    BYTE *pixels = nullptr;
    HBITMAP section;

    if (shape.Compression == COMPRESSION_RLE8 || shape.Compression == COMPRESSION_RLE4) {
        section = make_section(shape.Width, shape.Height, shape.Bits, colors, shape.ColorCount, &pixels);
        if (section) {
            RLE_TARGET target = {pixels, row_bytes(shape.Width, shape.Bits), shape.Width, shape.Height,
                                 shape.Bits == 4};
            rle_decode(&target, rows, shape.ImageSize);
        }
    } else if (shape.Bits == 16 || (shape.Bits == 32 && shape.Compression == COMPRESSION_MASKS)) {
        const BYTE *masks = shape.Compression == COMPRESSION_MASKS && shape.BitsOffset >= BMP_FILE_HEADER + header_size + 12
                                ? data + BMP_FILE_HEADER + header_size
                                : nullptr;
        section = convert_wide(rows, &shape, masks, signed_height);
    } else {
        WORD bits = shape.Bits;
        section = make_section(shape.Width, signed_height, bits, colors, bits <= 8 ? shape.ColorCount : 0, &pixels);
        if (section)
            memcpy(pixels, rows, (SIZE_T)row_bytes(shape.Width, bits) * shape.Height);
    }
    if (!section)
        return E_OUTOFMEMORY;
    image->Kind = PICTYPE_BITMAP;
    image->Bitmap = section;
    return S_OK;
}

/* ------------------------------------------------------------------ decoded images */

/* a mask of one bit a pixel, set where the pixel shows what is behind. Rows are top down and padded to words. */
static HBITMAP make_mask(const AUT_IMAGE *decoded)
{
    ULONG stride = (ULONG)((decoded->width + 15) / 16 * 2);
    BYTE *bits = AutAllocZero((SIZE_T)stride * decoded->height);
    if (!bits)
        return nullptr;
    for (int y = 0; y < decoded->height; y++) {
        const BYTE *row = decoded->pixels + (SIZE_T)y * decoded->width;
        for (int x = 0; x < decoded->width; x++) {
            if (row[x] == decoded->transparent)
                bits[(SIZE_T)y * stride + x / 8] |= (BYTE)(0x80 >> (x & 7));
        }
    }
    HBITMAP mask = CreateBitmap(decoded->width, decoded->height, 1, 1, bits);
    AutFree(bits);
    return mask;
}

HRESULT PicFromDecoded(const AUT_IMAGE *decoded, PIC_IMAGE *image)
{
    WORD bits = (WORD)decoded->bpp;
    RGBQUAD colors[COLOR_LIMIT] = {};
    ULONG count = 0;
    if (bits <= 8) {
        count = (ULONG)decoded->colors;
        if (count > COLOR_LIMIT)
            count = COLOR_LIMIT;
        for (ULONG i = 0; i < count; i++) {
            colors[i].rgbBlue = decoded->palette[i][0];
            colors[i].rgbGreen = decoded->palette[i][1];
            colors[i].rgbRed = decoded->palette[i][2];
        }
    }
    BYTE *pixels;
    HBITMAP section = make_section(decoded->width, decoded->height, bits, colors, count, &pixels);
    if (!section)
        return E_OUTOFMEMORY;
    ULONG stride = row_bytes(decoded->width, bits);
    SIZE_T source = (SIZE_T)decoded->width * (bits / 8);
    for (int y = 0; y < decoded->height; y++)
        memcpy(pixels + (SIZE_T)(decoded->height - 1 - y) * stride, decoded->pixels + (SIZE_T)y * source, source);
    image->Kind = PICTYPE_BITMAP;
    image->Bitmap = section;
    if (bits == 8 && decoded->transparent >= 0) {
        image->Mask = make_mask(decoded);
        if (!image->Mask) {
            DeleteObject(section);
            image->Bitmap = nullptr;
            return E_OUTOFMEMORY;
        }
    }
    return S_OK;
}
