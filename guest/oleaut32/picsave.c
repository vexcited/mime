/*
 * serializing pictures: a bmp file of the current bitmap, an icon file, a placeable metafile or an enhanced
 * metafile. Pictures that kept their original file write that instead, see olepic.c.
 */
#include "picture.h"

#define BMP_FILE_HEADER 14
#define BMP_INFO_HEADER 40
#define COLOR_LIMIT 256
#define ICON_DIR_BYTES 22

static void put_u16(BYTE *p, UINT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static void put_u32(BYTE *p, ULONG v)
{
    put_u16(p, v & 0xFFFF);
    put_u16(p + 2, v >> 16);
}

static ULONG stride_of(LONG width, WORD bits)
{
    return (ULONG)(((ULONGLONG)width * bits + 31) / 32 * 4);
}

typedef struct DIB_COPY {
    BITMAPINFOHEADER Info;
    RGBQUAD Colors[COLOR_LIMIT];
} DIB_COPY;

/* pixels at the given depth plus the color table. The bitmap must not be selected into a dc. */
static BYTE *read_pixels(HBITMAP bitmap, LONG width, LONG height, WORD bits, DIB_COPY *dib)
{
    SIZE_T size = (SIZE_T)stride_of(width, bits) * height;
    BYTE *pixels = AutAllocZero(size ? size : 1);
    if (!pixels)
        return nullptr;
    memset(dib, 0, sizeof(*dib));
    dib->Info.biSize = sizeof(BITMAPINFOHEADER);
    dib->Info.biWidth = width;
    dib->Info.biHeight = height;
    dib->Info.biPlanes = 1;
    dib->Info.biBitCount = bits;
    HDC dc = PicDc();
    if (!dc || !GetDIBits(dc, bitmap, 0, height, pixels, (BITMAPINFO *)dib, DIB_RGB_COLORS)) {
        AutFree(pixels);
        return nullptr;
    }
    return pixels;
}

static HRESULT bitmap_file(PIC_IMAGE *image, BYTE **bytes, ULONG *size)
{
    DIBSECTION section = {};
    int got = image->Bitmap ? GetObjectW(image->Bitmap, sizeof(section), &section) : 0;
    if (!got)
        return E_FAIL;
    BOOL is_section = got == sizeof(section);
    LONG width = section.dsBm.bmWidth, height = section.dsBm.bmHeight;
    WORD source_bits = is_section ? section.dsBmih.biBitCount : section.dsBm.bmBitsPixel;
    WORD bits = source_bits == 1 || source_bits == 4 || source_bits == 8 ? source_bits : 24;
    if (!is_section && source_bits != 1)
        bits = 24;

    PicLock();
    HRESULT withdrawn = image->Home ? PicTakeOut(image) : E_FAIL;
    DIB_COPY dib;
    BYTE *pixels = read_pixels(image->Bitmap, width, height, bits, &dib);
    if (SUCCEEDED(withdrawn))
        SelectObject(image->Home, image->Bitmap);
    PicUnlock();
    if (!pixels)
        return E_FAIL;

    ULONG colors = 0;
    if (bits <= 8) {
        colors = is_section && section.dsBmih.biClrUsed ? section.dsBmih.biClrUsed : 1u << bits;
        if (colors > COLOR_LIMIT)
            colors = COLOR_LIMIT;
    }
    ULONG image_bytes = stride_of(width, bits) * height;
    ULONG offset = BMP_FILE_HEADER + BMP_INFO_HEADER + colors * 4;
    BYTE *file = AutAllocZero(offset + image_bytes);
    if (!file) {
        AutFree(pixels);
        return E_OUTOFMEMORY;
    }
    file[0] = 'B';
    file[1] = 'M';
    put_u32(file + 2, offset + image_bytes);
    put_u32(file + 10, offset);
    BYTE *info = file + BMP_FILE_HEADER;
    put_u32(info, BMP_INFO_HEADER);
    put_u32(info + 4, width);
    put_u32(info + 8, height);
    put_u16(info + 12, 1);
    put_u16(info + 14, bits);
    put_u32(info + 20, image_bytes);
    put_u32(info + 32, colors);
    memcpy(file + BMP_FILE_HEADER + BMP_INFO_HEADER, dib.Colors, colors * 4);
    memcpy(file + offset, pixels, image_bytes);
    AutFree(pixels);
    *bytes = file;
    *size = offset + image_bytes;
    return S_OK;
}

static HRESULT icon_file(PIC_IMAGE *image, BYTE **bytes, ULONG *size)
{
    BITMAP shape = {};
    if (!image->IconAnd || !image->IconXor || !GetObjectW(image->IconAnd, sizeof(shape), &shape))
        return E_FAIL;
    LONG width = shape.bmWidth, height = shape.bmHeight;
    PicLock();
    DIB_COPY color_dib, mask_dib;
    BYTE *colors = read_pixels(image->IconXor, width, height, 24, &color_dib);
    BYTE *mask = read_pixels(image->IconAnd, width, height, 1, &mask_dib);
    PicUnlock();
    if (!colors || !mask) {
        AutFree(colors);
        AutFree(mask);
        return E_FAIL;
    }
    ULONG color_bytes = stride_of(width, 24) * height, mask_bytes = stride_of(width, 1) * height;
    ULONG resource = BMP_INFO_HEADER + color_bytes + mask_bytes;
    BYTE *file = AutAllocZero(ICON_DIR_BYTES + resource);
    if (!file) {
        AutFree(colors);
        AutFree(mask);
        return E_OUTOFMEMORY;
    }
    put_u16(file + 2, 1);
    put_u16(file + 4, 1);
    file[6] = width >= 256 ? 0 : (BYTE)width;
    file[7] = height >= 256 ? 0 : (BYTE)height;
    put_u16(file + 10, 1);
    put_u16(file + 12, 24);
    put_u32(file + 14, resource);
    put_u32(file + 18, ICON_DIR_BYTES);
    BYTE *info = file + ICON_DIR_BYTES;
    put_u32(info, BMP_INFO_HEADER);
    put_u32(info + 4, width);
    put_u32(info + 8, height * 2);
    put_u16(info + 12, 1);
    put_u16(info + 14, 24);
    put_u32(info + 20, color_bytes + mask_bytes);
    memcpy(info + BMP_INFO_HEADER, colors, color_bytes);
    memcpy(info + BMP_INFO_HEADER + color_bytes, mask, mask_bytes);
    AutFree(colors);
    AutFree(mask);
    *bytes = file;
    *size = ICON_DIR_BYTES + resource;
    return S_OK;
}

/* xor of the ten words before the checksum. */
static UINT placeable_checksum(const BYTE *header)
{
    UINT sum = 0;
    for (int i = 0; i < 10; i++)
        sum ^= header[2 * i] | header[2 * i + 1] << 8;
    return sum;
}

static HRESULT metafile_file(PIC_IMAGE *image, BYTE **bytes, ULONG *size)
{
    UINT count = GetMetaFileBitsEx(image->Wmf, 0, nullptr);
    if (!count)
        return E_OUTOFMEMORY;
    BYTE *file = AutAllocZero(PLACEABLE_HEADER + count);
    if (!file)
        return E_OUTOFMEMORY;
    if (GetMetaFileBitsEx(image->Wmf, count, file + PLACEABLE_HEADER) != count) {
        AutFree(file);
        return E_FAIL;
    }
    put_u32(file, PLACEABLE_KEY);
    put_u16(file + 10, (UINT)image->WmfCx);
    put_u16(file + 12, (UINT)image->WmfCy);
    put_u16(file + 14, TWIPS_PER_INCH);
    put_u16(file + 20, placeable_checksum(file));
    *bytes = file;
    *size = PLACEABLE_HEADER + count;
    return S_OK;
}

static HRESULT enhanced_file(PIC_IMAGE *image, BYTE **bytes, ULONG *size)
{
    UINT count = GetEnhMetaFileBits(image->Emf, 0, nullptr);
    if (!count)
        return E_OUTOFMEMORY;
    BYTE *file = AutAlloc(count);
    if (!file)
        return E_OUTOFMEMORY;
    if (GetEnhMetaFileBits(image->Emf, count, file) != count) {
        AutFree(file);
        return E_FAIL;
    }
    *bytes = file;
    *size = count;
    return S_OK;
}

HRESULT PicSerialize(PIC_IMAGE *image, BYTE **bytes, ULONG *size)
{
    *bytes = nullptr;
    *size = 0;
    switch (image->Kind) {
    case PIC_UNSET:
    case PICTYPE_NONE: return S_OK;
    case PICTYPE_BITMAP: return bitmap_file(image, bytes, size);
    case PICTYPE_ICON: return icon_file(image, bytes, size);
    case PICTYPE_METAFILE: return metafile_file(image, bytes, size);
    case PICTYPE_ENHMETAFILE: return enhanced_file(image, bytes, size);
    default: return E_FAIL;
    }
}
