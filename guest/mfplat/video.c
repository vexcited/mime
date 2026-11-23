/* the video helpers evr.dll hands on to mfplat: strides, plane sizes, copies, half floats and the format block. */
#include "mfplat_private.h"

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)
#define F_ALIGNED 1
#define F_BOTTOM_UP 2
#define F_YUV 4

/*
 * every format the helpers of windows 11 know, read by trying all of them with evrtab.exe: the bits of a pixel,
 * whether a row is padded to four bytes, whether rows run upwards, and whether evr calls the format yuv. a format
 * with no bits is one evr calls yuv and the helpers do not know.
 */
static const struct {
    DWORD format;
    USHORT bits, flags;
} VIDEO_FORMATS[] = {
    {20, 24, F_ALIGNED | F_BOTTOM_UP},
    {21, 32, F_BOTTOM_UP},
    {22, 32, F_BOTTOM_UP},
    {23, 16, F_ALIGNED | F_BOTTOM_UP},
    {24, 16, F_ALIGNED | F_BOTTOM_UP},
    {25, 16, F_ALIGNED | F_BOTTOM_UP},
    {26, 16, F_ALIGNED | F_BOTTOM_UP},
    {27, 8, F_ALIGNED | F_BOTTOM_UP},
    {28, 8, F_ALIGNED | F_BOTTOM_UP},
    {29, 16, F_ALIGNED | F_BOTTOM_UP},
    {30, 16, F_ALIGNED | F_BOTTOM_UP},
    {31, 32, F_BOTTOM_UP},
    {32, 32, F_BOTTOM_UP},
    {33, 32, F_BOTTOM_UP},
    {34, 32, F_BOTTOM_UP},
    {35, 32, F_BOTTOM_UP},
    {36, 64, F_BOTTOM_UP},
    {40, 16, F_ALIGNED | F_BOTTOM_UP},
    {41, 8, F_ALIGNED | F_BOTTOM_UP},
    {50, 8, F_ALIGNED | F_BOTTOM_UP},
    {51, 16, F_ALIGNED | F_BOTTOM_UP},
    {52, 8, F_ALIGNED | F_BOTTOM_UP},
    {80, 16, F_ALIGNED},
    {81, 16, F_ALIGNED},
    {102, 32, F_BOTTOM_UP},
    {111, 16, F_ALIGNED},
    {112, 32, F_BOTTOM_UP},
    {113, 64, F_BOTTOM_UP},
    {114, 32, F_BOTTOM_UP},
    {115, 64, F_BOTTOM_UP},
    {116, 128, F_BOTTOM_UP},
    {FCC('4', '2', '0', 'O'), 12, F_YUV},
    {FCC('A', 'I', '4', '4'), 8, F_ALIGNED | F_YUV},
    {FCC('A', 'Y', 'U', 'V'), 32, F_YUV},
    {FCC('C', 'R', 'A', 'M'), 8, F_ALIGNED},
    {FCC('C', 'R', 'A', 'N'), 16, F_ALIGNED},
    {FCC('G', 'R', 'G', 'B'), 16, F_ALIGNED},
    {FCC('I', '4', '2', '0'), 12, 0},
    {FCC('I', '4', '2', '2'), 16, 0},
    {FCC('I', '4', '4', '4'), 24, 0},
    {FCC('I', 'A', '4', '4'), 8, F_ALIGNED | F_YUV},
    {FCC('I', 'M', 'C', '1'), 16, F_ALIGNED | F_YUV},
    {FCC('I', 'M', 'C', '2'), 12, F_YUV},
    {FCC('I', 'M', 'C', '3'), 16, F_ALIGNED | F_YUV},
    {FCC('I', 'M', 'C', '4'), 12, F_YUV},
    {FCC('I', 'Y', 'U', 'V'), 12, 0},
    {FCC('M', 'J', 'P', 'G'), 24, 0},
    {FCC('N', 'V', '1', '1'), 12, F_YUV},
    {FCC('N', 'V', '1', '2'), 12, F_YUV},
    {FCC('N', 'V', '2', '1'), 12, 0},
    {FCC('N', 'V', '2', '4'), 12, F_YUV},
    {FCC('P', '0', '1', '0'), 24, F_YUV},
    {FCC('P', '0', '1', '6'), 24, F_YUV},
    {FCC('P', '2', '0', '8'), 0, F_YUV},
    {FCC('P', '2', '1', '0'), 32, F_YUV},
    {FCC('P', '2', '1', '6'), 32, F_YUV},
    {FCC('P', '4', '0', '8'), 0, F_YUV},
    {FCC('R', 'G', 'B', 'G'), 16, F_ALIGNED},
    {FCC('S', '3', '4', '0'), 16, F_ALIGNED | F_YUV},
    {FCC('S', '3', '4', '2'), 0, F_YUV},
    {FCC('U', 'Y', 'V', 'Y'), 16, F_ALIGNED | F_YUV},
    {FCC('Y', '2', '1', '0'), 32, F_YUV},
    {FCC('Y', '2', '1', '1'), 8, F_ALIGNED},
    {FCC('Y', '2', '1', '6'), 32, F_YUV},
    {FCC('Y', '4', '1', '0'), 32, 0},
    {FCC('Y', '4', '1', '1'), 16, F_ALIGNED},
    {FCC('Y', '4', '1', '6'), 64, 0},
    {FCC('Y', '4', '1', 'P'), 12, 0},
    {FCC('Y', 'U', 'Y', '2'), 16, F_ALIGNED | F_YUV},
    {FCC('Y', 'V', '1', '2'), 12, F_YUV},
    {FCC('Y', 'V', 'U', '9'), 9, 0},
    {FCC('Y', 'V', 'Y', 'U'), 16, F_ALIGNED},
    {FCC('v', '2', '1', '0'), 21, 0},
    {FCC('v', '4', '1', '0'), 32, 0},
};

static BOOL find_format(DWORD format, DWORD *bits, DWORD *flags)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(VIDEO_FORMATS); i++) {
        if (VIDEO_FORMATS[i].format == format) {
            *bits = VIDEO_FORMATS[i].bits;
            *flags = VIDEO_FORMATS[i].flags;
            return TRUE;
        }
    }
    *bits = 0;
    *flags = 0;
    return FALSE;
}

/* the stride of a row, negative for rows that run upwards, and the bytes of the whole picture. */
static LONG stride_and_size(DWORD format, DWORD width, DWORD height, DWORD *size)
{
    DWORD bits, flags, stride;
    find_format(format, &bits, &flags);
    BOOL whole_picture = TRUE;
    if (bits & 7) {
        stride = format == FCC('Y', '4', '1', 'P') ? (3 * width) >> 1 : (width * (bits & ~7u)) >> 3;
    } else if (format == FCC('P', '0', '1', '0') || format == FCC('P', '0', '1', '6') ||
               format == FCC('P', '2', '1', '0') || format == FCC('P', '2', '1', '6')) {
        stride = 2 * width;
    } else if (format == FCC('I', '4', '2', '2') || format == FCC('I', '4', '4', '4')) {
        stride = width;
    } else {
        stride = flags & F_ALIGNED ? (width * (bits >> 3) + 3) & ~3u : (width * bits) >> 3;
        whole_picture = FALSE;
    }
    if (size)
        *size = whole_picture ? (height * width * bits) >> 3 : height * stride;
    return flags & F_BOTTOM_UP ? -(LONG)stride : (LONG)stride;
}

HRESULT WINAPI MFGetStrideForBitmapInfoHeader(DWORD format, DWORD width, LONG *stride)
{
    if (!stride)
        return E_POINTER;
    *stride = stride_and_size(format, width, 0, nullptr);
    return *stride ? S_OK : MF_E_INVALIDMEDIATYPE;
}

HRESULT WINAPI MFGetPlaneSize(DWORD format, DWORD width, DWORD height, DWORD *size)
{
    if (!size)
        return E_POINTER;
    stride_and_size(format, width, height, size);
    return S_OK;
}

/* what evr.dll answers for MFIsFormatYUV, which mfplat does not export. */
BOOL MfIsFormatYuv(DWORD format)
{
    DWORD bits, flags;
    find_format(format, &bits, &flags);
    return (flags & F_YUV) != 0;
}

HRESULT WINAPI MFCopyImage(BYTE *dest, LONG dest_stride, const BYTE *src, LONG src_stride, DWORD width, DWORD lines)
{
    if (!dest || !src)
        return E_POINTER;
    if (width == (DWORD)dest_stride && width == (DWORD)src_stride) {
        memcpy(dest, src, (SIZE_T)lines * width);
        return S_OK;
    }
    for (DWORD i = 0; i < lines; i++) {
        memcpy(dest, src, width);
        dest += dest_stride;
        src += src_stride;
    }
    return S_OK;
}

/* a value too large for a half float becomes 7fff, which is no infinity, as windows writes it. */
HRESULT WINAPI MFConvertToFP16Array(WORD *dest, const float *src, DWORD count)
{
    if (!dest || !src)
        return E_POINTER;
    for (DWORD i = 0; i < count; i++) {
        DWORD bits;
        memcpy(&bits, &src[i], sizeof(bits));
        DWORD sign = (bits >> 16) & 0x8000, magnitude = bits & 0x7FFFFFFF, value;
        if (magnitude > 0x47FFEFFF) {
            dest[i] = (WORD)(sign | 0x7FFF);
            continue;
        }
        if (magnitude >= 0x38800000) {
            value = magnitude + ((magnitude & 0x2000) != 0) - 0x37FFF001;
        } else {
            DWORD shift = 113 - (magnitude >> 23);
            DWORD mantissa = shift >= 24 ? 0 : ((magnitude & 0x7FFFFF) | 0x800000) >> shift;
            value = ((mantissa & 0x2000) != 0) + mantissa + 4095;
        }
        dest[i] = (WORD)((value >> 13) | sign);
    }
    return S_OK;
}

/* infinities and nans are read as ordinary large numbers, as windows reads them. */
HRESULT WINAPI MFConvertFromFP16Array(float *dest, const WORD *src, DWORD count)
{
    if (!dest || !src)
        return E_POINTER;
    for (DWORD i = 0; i < count; i++) {
        DWORD half = src[i], bits;
        if (half & 0x7C00) {
            bits = (((half & 0x7C00) << 13) + 0x38000000) | (((half & 0x3FF) | ((half & 0x8000) << 3)) << 13);
        } else if (half & 0x3FF) {
            LONG exponent = -14;
            DWORD mantissa = half & 0x3FF;
            do {
                exponent--;
                mantissa <<= 1;
            } while (!(mantissa & 0x400));
            bits = (((mantissa | ((half & 0x8000) << 3)) & ~0x400u) << 13) | ((DWORD)(exponent + 127) << 23);
        } else {
            bits = (half & 0x8000) << 16;
        }
        memcpy(&dest[i], &bits, sizeof(bits));
    }
    return S_OK;
}

HRESULT WINAPI MFConvertColorInfoToDXVA(DWORD *dxva, const MFVIDEOFORMAT *format)
{
    if (!dxva || !format)
        return E_POINTER;
    const MFVIDEOINFO *info = &format->videoInfo;
    DWORD value = (*dxva & 0x7F) | 0x80;
    if (info->SourceChromaSubsampling < 8)
        value |= info->SourceChromaSubsampling << 8;
    if (info->NominalRange < 5)
        value |= info->NominalRange << 12;
    if (info->SourceLighting < 5)
        value |= info->SourceLighting << 18;
    if (info->ColorPrimaries < 14)
        value |= info->ColorPrimaries << 22;
    if (info->TransferFunction < 20)
        value |= info->TransferFunction << 27;
    if (info->TransferMatrix < 8)
        value |= info->TransferMatrix << 15;
    *dxva = value;
    return S_OK;
}

HRESULT WINAPI MFConvertColorInfoFromDXVA(MFVIDEOFORMAT *format, DWORD dxva)
{
    if (!format)
        return E_POINTER;
    MFVIDEOINFO *info = &format->videoInfo;
    info->SourceChromaSubsampling = (dxva & 0xF00) < 0x800 ? (dxva >> 8) & 0xF : 0;
    info->NominalRange = (dxva & 0x7000) < 0x5000 ? (dxva >> 12) & 7 : 0;
    info->SourceLighting = (dxva & 0x3C0000) < 0x140000 ? (dxva >> 18) & 0xF : 0;
    info->ColorPrimaries = (dxva & 0x7C00000) < 0x3800000 ? (dxva >> 22) & 0x1F : 0;
    info->TransferFunction = dxva >> 27 < 0x14 ? dxva >> 27 : 0;
    info->TransferMatrix = (dxva >> 15) & 7;
    return S_OK;
}

static const GUID VIDEO_FORMAT_BASE = {0, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static void set_area(MFVideoArea *area, LONG width, LONG height)
{
    memset(area, 0, sizeof(*area));
    area->Area.cx = width;
    area->Area.cy = height;
}

static void finish_format(MFVIDEOFORMAT *format, DWORD surface)
{
    format->surfaceInfo.Format = surface;
    format->guidFormat = VIDEO_FORMAT_BASE;
    format->guidFormat.Data1 = surface;
}

/* the surface formats windows takes for gamma 2.2, which get the sRGB transfer function. */
static BOOL is_gamma22(DWORD format)
{
    return (format >= 20 && format <= 33) || format == 35 || format == 36 || format == 41 || format == 118 ||
           format == 0x78783474;
}

HRESULT WINAPI MFInitVideoFormat_RGB(MFVIDEOFORMAT *format, DWORD width, DWORD height, DWORD d3dfmt)
{
    if (!format)
        return E_INVALIDARG;
    memset(format, 0, sizeof(*format));
    format->dwSize = sizeof(*format);
    if (!d3dfmt)
        d3dfmt = 22;
    MFVIDEOINFO *info = &format->videoInfo;
    info->dwWidth = width;
    info->dwHeight = height;
    info->PixelAspectRatio.Numerator = info->PixelAspectRatio.Denominator = 1;
    info->InterlaceMode = 2;
    info->TransferFunction = is_gamma22(d3dfmt) ? 7 : 1;
    info->ColorPrimaries = 2;
    info->SourceLighting = 2;
    info->FramesPerSecond.Numerator = 60;
    info->FramesPerSecond.Denominator = 1;
    info->NominalRange = 1;
    set_area(&info->GeometricAperture, (LONG)width, (LONG)height);
    set_area(&info->MinimumDisplayAperture, (LONG)width, (LONG)height);
    finish_format(format, d3dfmt);
    return S_OK;
}

/* the television formats: size, pixel shape, frame rate and the picture inside the frame. */
static void init_broadcast(MFVIDEOINFO *info, DWORD width, DWORD height, DWORD par_x, DWORD par_y, DWORD interlace,
                           DWORD primaries, DWORD matrix, DWORD rate, DWORD scale, LONG shown_x, LONG shown_width,
                           LONG shown_y, LONG shown_height)
{
    info->dwWidth = width;
    info->dwHeight = height;
    info->PixelAspectRatio.Numerator = par_x;
    info->PixelAspectRatio.Denominator = par_y;
    info->InterlaceMode = interlace;
    info->TransferFunction = 5;
    info->ColorPrimaries = primaries;
    info->TransferMatrix = matrix;
    info->SourceLighting = 3;
    info->FramesPerSecond.Numerator = rate;
    info->FramesPerSecond.Denominator = scale;
    info->NominalRange = 2;
    set_area(&info->GeometricAperture, shown_width, shown_height);
    info->GeometricAperture.OffsetX.value = (SHORT)shown_x;
    info->GeometricAperture.OffsetY.value = (SHORT)shown_y;
    set_area(&info->MinimumDisplayAperture, (LONG)width, (LONG)height);
    /* only the 480 line formats show no more than the picture. */
    if (height == 480)
        info->MinimumDisplayAperture = info->GeometricAperture;
}

HRESULT WINAPI MFInitVideoFormat(MFVIDEOFORMAT *format, DWORD type)
{
    if (!format)
        return E_INVALIDARG;
    memset(format, 0, sizeof(*format));
    format->dwSize = sizeof(*format);
    MFVIDEOINFO *info = &format->videoInfo;
    DWORD surface = FCC('N', 'V', '1', '2');
    switch (type) {
    case 1: case 3: case 7:
        init_broadcast(info, 720, 480, 10, 11, 4, 5, 2, 30000, 1001, 8, 704, 0, 480);
        info->SourceChromaSubsampling = 5;
        break;
    case 2: case 4:
        init_broadcast(info, 720, 576, 12, 11, 3, 7, 2, 25, 1, 8, 704, 0, 576);
        info->SourceChromaSubsampling = 5;
        break;
    case 5:
        init_broadcast(info, 720, 576, 12, 11, 3, 7, 2, 25, 1, 8, 704, 0, 576);
        info->SourceChromaSubsampling = 6;
        break;
    case 6:
        init_broadcast(info, 720, 480, 10, 11, 4, 5, 2, 30000, 1001, 8, 704, 0, 480);
        info->SourceChromaSubsampling = 7;
        surface = FCC('Y', '4', '1', '1');
        break;
    case 8:
        init_broadcast(info, 1920, 1080, 1, 1, 4, 2, 1, 30000, 1001, 16, 1888, 9, 1062);
        info->SourceChromaSubsampling = 5;
        break;
    case 9:
        init_broadcast(info, 1280, 720, 1, 1, 2, 2, 1, 60000, 1001, 16, 1248, 9, 702);
        info->SourceChromaSubsampling = 5;
        break;
    default:
        return E_INVALIDARG;
    }
    finish_format(format, surface);
    return S_OK;
}

/* the surface format of a format block that names an uncompressed format the helpers know. */
DWORD WINAPI MFGetUncompressedVideoFormat(const MFVIDEOFORMAT *format)
{
    DWORD fourcc = format->guidFormat.Data1;
    if (fourcc == FCC('C', 'R', 'A', 'M') || fourcc == FCC('C', 'R', 'A', 'N') || fourcc == FCC('M', 'J', 'P', 'G'))
        return 0;
    static const DWORD KNOWN[] = {
        118, FCC('4', 'P', 'x', 'x'), 41, 24, 25, 22, 21, 23, 20, 26, 30, 35, 31, 36, 113, 116, 50, 81, 80, 32, 102,
        FCC('I', '4', '2', '0'), FCC('I', 'Y', 'U', 'V'), FCC('N', 'V', '1', '2'), FCC('N', 'V', '2', '4'),
        FCC('Y', 'V', '1', '2'), FCC('Y', '4', '1', 'P'), FCC('I', 'M', 'C', '2'), FCC('I', 'M', 'C', '4'),
        FCC('I', 'M', 'C', '1'), FCC('I', 'M', 'C', '3'), FCC('4', '2', '0', 'O'), FCC('S', '3', '4', '0'),
        FCC('P', '0', '1', '0'), FCC('P', '0', '1', '6'), FCC('N', 'V', '1', '1'), FCC('Y', '4', '1', '1'),
        FCC('P', '2', '0', '8'), FCC('Y', 'U', 'Y', '2'), FCC('U', 'Y', 'V', 'Y'), FCC('Y', 'V', 'Y', 'U'),
        FCC('v', '2', '1', '0'), FCC('H', 'D', '1', '0'), FCC('Y', '2', '1', '0'), FCC('Y', '2', '1', '6'),
        FCC('P', '2', '1', '0'), FCC('P', '2', '1', '6'), FCC('A', 'Y', 'U', 'V'), FCC('Y', '4', '1', '0'),
        FCC('Y', '4', '1', '6'), FCC('v', '4', '1', '0'), FCC('Y', 'V', 'U', '9'), FCC('Y', '2', '1', '1'),
        FCC('I', 'A', '4', '4'), FCC('A', 'I', '4', '4'), FCC('I', '4', '2', '2'), FCC('I', '4', '4', '4'),
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(KNOWN); i++)
        if (KNOWN[i] == fourcc)
            return format->surfaceInfo.Format;
    return 0;
}
