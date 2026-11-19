/* video media types and the format block: each made from the other the way mfplat of windows 11 does. */
#include "mfplat_private.h"

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)

/*
 * the uncompressed formats mfplat knows, in its order, read from its table: the compression a bitmap header names
 * the format by, the bits of a group of pixels in the whole picture and in the first plane, the pixels of a group,
 * and for the first fifteen the DirectShow subtype that means the same format.
 */
static const struct {
    DWORD format, compression, bits, plane_bits, group;
    GUID directshow;
} FORMATS[] = {
    {118, 0, 8, 8, 8, {0xE436EB78, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB1 */
    {FCC('4', 'P', 'x', 'x'), 0, 8, 8, 2, {0xE436EB79, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB4 */
    {41, 0, 8, 8, 1, {0xE436EB7A, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB8 */
    {24, 0, 16, 16, 1, {0xE436EB7C, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB555 */
    {25, 0, 16, 16, 1, {0x297C55AF, 0xE209, 0x4CB3, {0xB7, 0x57, 0xC7, 0x6D, 0x6B, 0x9C, 0x88, 0xA8}}}, /* MEDIASUBTYPE_ARGB1555 */
    {24, 3, 16, 16, 1, {0xE436EB7C, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB555 */
    {22, 0, 32, 32, 1, {0xE436EB7E, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB32 */
    {21, 0, 32, 32, 1, {0x773C9AC0, 0x3274, 0x11D0, {0xB7, 0x24, 0x00, 0xAA, 0x00, 0x6C, 0x1A, 0x01}}}, /* MEDIASUBTYPE_ARGB32 */
    {21, 3, 32, 32, 1, {0x773C9AC0, 0x3274, 0x11D0, {0xB7, 0x24, 0x00, 0xAA, 0x00, 0x6C, 0x1A, 0x01}}}, /* MEDIASUBTYPE_ARGB32 */
    {23, 3, 16, 16, 1, {0xE436EB7B, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB565 */
    {20, 0, 24, 24, 1, {0xE436EB7D, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}}}, /* MEDIASUBTYPE_RGB24 */
    {26, 0xFFFFFFFF, 16, 16, 1, {0x6E6415E6, 0x5C24, 0x425F, {0x93, 0xCD, 0x80, 0x10, 0x2B, 0x3D, 0x1C, 0xCA}}}, /* MEDIASUBTYPE_ARGB4444 */
    {30, 0xFFFFFFFF, 16, 16, 1, {0x6E6415E6, 0x5C24, 0x425F, {0x93, 0xCD, 0x80, 0x10, 0x2B, 0x3D, 0x1C, 0xCA}}}, /* MEDIASUBTYPE_ARGB4444 */
    {35, 0xFFFFFFFF, 32, 32, 1, {0x2F8BB76D, 0xB644, 0x4550, {0xAC, 0xF3, 0xD3, 0x0C, 0xAA, 0x65, 0xD5, 0xC5}}}, /* MEDIASUBTYPE_A2R10G10B10 */
    {31, 0xFFFFFFFF, 32, 32, 1, {0x576F7893, 0xBDF6, 0x48C4, {0x87, 0x5F, 0xAE, 0x7B, 0x81, 0x83, 0x45, 0x67}}}, /* MEDIASUBTYPE_A2B10G10R10 */
    {36, 0xFFFFFFFF, 64, 64, 1, {0}},
    {113, 0xFFFFFFFF, 64, 64, 1, {0}},
    {116, 0xFFFFFFFF, 128, 128, 1, {0}},
    {50, 0xFFFFFFFF, 8, 8, 1, {0}},
    {81, 0xFFFFFFFF, 16, 16, 1, {0}},
    {80, 0xFFFFFFFF, 16, 16, 1, {0}},
    {32, 0xFFFFFFFF, 32, 32, 1, {0}},
    {102, 0xFFFFFFFF, 32, 32, 1, {0}},
    {FCC('C', 'R', 'A', 'M'), FCC('C', 'R', 'A', 'M'), 8, 8, 1, {0}},
    {FCC('C', 'R', 'A', 'N'), FCC('C', 'R', 'A', 'M'), 16, 16, 1, {0}},
    {FCC('M', 'J', 'P', 'G'), 0xFFFFFFFF, 24, 24, 1, {0}},
    {FCC('I', '4', '2', '0'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('I', 'Y', 'U', 'V'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('N', 'V', '1', '2'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('N', 'V', '2', '4'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('Y', 'V', '1', '2'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('Y', '4', '1', 'P'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('I', 'M', 'C', '2'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('I', 'M', 'C', '4'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('I', 'M', 'C', '1'), 0xFFFFFFFF, 32, 16, 2, {0}},
    {FCC('I', 'M', 'C', '3'), 0xFFFFFFFF, 32, 16, 2, {0}},
    {FCC('4', '2', '0', 'O'), 0xFFFFFFFF, 24, 16, 2, {0}},
    {FCC('S', '3', '4', '0'), 0xFFFFFFFF, 32, 32, 2, {0}},
    {FCC('P', '0', '1', '0'), 0xFFFFFFFF, 48, 32, 2, {0}},
    {FCC('P', '0', '1', '6'), 0xFFFFFFFF, 48, 32, 2, {0}},
    {FCC('N', 'V', '1', '1'), 0xFFFFFFFF, 48, 32, 4, {0}},
    {FCC('Y', '4', '1', '1'), 0xFFFFFFFF, 48, 48, 4, {0}},
    {FCC('P', '2', '0', '8'), 0xFFFFFFFF, 32, 16, 2, {0}},
    {FCC('Y', 'U', 'Y', '2'), 0xFFFFFFFF, 32, 32, 2, {0}},
    {FCC('U', 'Y', 'V', 'Y'), 0xFFFFFFFF, 32, 32, 2, {0}},
    {FCC('Y', 'V', 'Y', 'U'), 0xFFFFFFFF, 32, 32, 2, {0}},
    {FCC('v', '2', '1', '0'), 0xFFFFFFFF, 1024, 1024, 48, {0}},
    {FCC('H', 'D', '1', '0'), 0xFFFFFFFF, 320, 320, 16, {0}},
    {FCC('Y', '2', '1', '0'), 0xFFFFFFFF, 64, 64, 2, {0}},
    {FCC('Y', '2', '1', '6'), 0xFFFFFFFF, 64, 64, 2, {0}},
    {FCC('P', '2', '1', '0'), 0xFFFFFFFF, 64, 32, 2, {0}},
    {FCC('P', '2', '1', '6'), 0xFFFFFFFF, 64, 32, 2, {0}},
    {FCC('A', 'Y', 'U', 'V'), 0xFFFFFFFF, 32, 32, 1, {0}},
    {FCC('Y', '4', '1', '0'), 0xFFFFFFFF, 32, 32, 1, {0}},
    {FCC('Y', '4', '1', '6'), 0xFFFFFFFF, 64, 64, 1, {0}},
    {FCC('v', '4', '1', '0'), 0xFFFFFFFF, 32, 32, 1, {0}},
    {FCC('Y', 'V', 'U', '9'), 0xFFFFFFFF, 36, 32, 4, {0}},
    {FCC('Y', '2', '1', '1'), 0xFFFFFFFF, 32, 32, 4, {0}},
    {FCC('I', 'A', '4', '4'), 0xFFFFFFFF, 8, 8, 1, {0}},
    {FCC('A', 'I', '4', '4'), 0xFFFFFFFF, 8, 8, 1, {0}},
    {FCC('I', '4', '2', '2'), 0xFFFFFFFF, 32, 16, 2, {0}},
    {FCC('I', '4', '4', '4'), 0xFFFFFFFF, 24, 8, 1, {0}},
};
#define DIRECTSHOW_FORMATS 15

static const GUID FORMAT_BASE = {0, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const DWORD COMPRESSED[] = {FCC('C', 'R', 'A', 'M'), FCC('C', 'R', 'A', 'N'), FCC('M', 'J', 'P', 'G')};
/* the formats whose rows may run upwards. */
static const DWORD BOTTOM_UP[] = {118, FCC('4', 'P', 'x', 'x'), 41, 24, 23, 20, 22, 21};

static BOOL in_list(const DWORD *list, ULONG count, DWORD format)
{
    for (ULONG i = 0; i < count; i++)
        if (list[i] == format)
            return TRUE;
    return FALSE;
}

static LONG find_format(DWORD format)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(FORMATS); i++)
        if (FORMATS[i].format == format)
            return (LONG)i;
    return -1;
}

static BOOL is_base_guid(const GUID *guid)
{
    GUID base = FORMAT_BASE;
    base.Data1 = guid->Data1;
    return IsEqualGUID(&base, guid);
}

/* the format a subtype names: its first field when it is of the base guid, or the DirectShow subtype's format. */
static DWORD format_of_subtype(const GUID *subtype)
{
    if (is_base_guid(subtype))
        return subtype->Data1;
    for (ULONG i = 0; i < DIRECTSHOW_FORMATS; i++)
        if (IsEqualGUID(&FORMATS[i].directshow, subtype))
            return FORMATS[i].format;
    return 0;
}

static BOOL rgb_compression(DWORD compression)
{
    return compression == 0 || compression == 3;
}

static UINT32 round_to_group(UINT32 width, UINT32 group)
{
    return width % group ? width + group - width % group : width;
}

/* the last row of the table with the format decides, as the search of windows goes through all of them. */
static LONG default_stride(DWORD format, UINT32 width, BOOL upwards)
{
    LONG stride = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(FORMATS); i++) {
        if (FORMATS[i].format != format)
            continue;
        UINT32 wide = round_to_group(width, FORMATS[i].group);
        UINT32 bytes = format == FCC('Y', '4', '1', 'P') ? (UINT32)((LONG)(3 * wide) / 2)
                                                           : FORMATS[i].plane_bits * wide / (8 * FORMATS[i].group);
        if (rgb_compression(FORMATS[i].compression))
            bytes = (bytes + 3) & ~3u;
        stride = upwards ? -(LONG)bytes : (LONG)bytes;
    }
    return stride;
}

static HRESULT image_size(LONG row, UINT32 width, UINT32 height, UINT32 *size)
{
    const HRESULT overflow = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    UINT32 group = FORMATS[row].group, wide = width;
    if (width % group) {
        wide = width + group - width % group;
        if (wide < width)
            return overflow;
    }
    ULONGLONG bits = (ULONGLONG)wide * FORMATS[row].bits;
    if (bits > 0xFFFFFFFF)
        return overflow;
    UINT32 bytes = (UINT32)bits / (8 * group);
    if (rgb_compression(FORMATS[row].compression)) {
        if (bytes + 3 < bytes)
            return overflow;
        bytes = (bytes + 3) & ~3u;
    }
    ULONGLONG total = (ULONGLONG)height * bytes;
    if (total > 0xFFFFFFFF) {
        *size = 0xFFFFFFFF;
        return overflow;
    }
    *size = (UINT32)total;
    return S_OK;
}

static UINT64 pack(UINT32 high, UINT32 low)
{
    return (UINT64)high << 32 | low;
}

HRESULT WINAPI MFInitMediaTypeFromMFVideoFormat(IMFMediaType *type, const MFVIDEOFORMAT *format, UINT32 size)
{
    UINT32 fixed = sizeof(MFVIDEOFORMAT);
    if (size < fixed || format->dwSize < fixed || size < format->dwSize)
        return E_INVALIDARG;
    UINT32 palette = format->surfaceInfo.PaletteEntries;
    if (palette) {
        fixed = 4 * palette + 172;
        if (size < fixed || format->dwSize < fixed)
            return E_INVALIDARG;
    }
    UINT32 extra = format->dwSize - fixed;
    const UINT8 *extra_bytes = (const UINT8 *)format + fixed;
    const MFVIDEOINFO *info = &format->videoInfo;
    DWORD fourcc = format_of_subtype(&format->guidFormat);
    static const GUID NO_GUID;

#define CHECK(call)                                                                                                    \
    do {                                                                                                               \
        HRESULT check = (call);                                                                                        \
        if (FAILED(check))                                                                                             \
            return check;                                                                                              \
    } while (0)
    CHECK(type->lpVtbl->DeleteAllItems(type));
    CHECK(type->lpVtbl->SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
    if (!IsEqualGUID(&format->guidFormat, &NO_GUID))
        CHECK(type->lpVtbl->SetGUID(type, &MF_MT_SUBTYPE, &format->guidFormat));
    if (palette)
        CHECK(type->lpVtbl->SetBlob(type, &MF_MT_PALETTE, (const UINT8 *)format->surfaceInfo.Palette, 4 * palette));
    if (info->dwHeight && info->dwWidth)
        CHECK(type->lpVtbl->SetUINT64(type, &MF_MT_FRAME_SIZE, pack(info->dwWidth, info->dwHeight)));
    if (info->FramesPerSecond.Denominator && info->FramesPerSecond.Numerator)
        CHECK(type->lpVtbl->SetUINT64(type, &MF_MT_FRAME_RATE,
                                      pack(info->FramesPerSecond.Numerator, info->FramesPerSecond.Denominator)));
    if (info->GeometricAperture.Area.cx && info->GeometricAperture.Area.cy)
        CHECK(type->lpVtbl->SetBlob(type, &MF_MT_GEOMETRIC_APERTURE, (const UINT8 *)&info->GeometricAperture, 16));
    if (info->MinimumDisplayAperture.Area.cx && info->MinimumDisplayAperture.Area.cy)
        CHECK(type->lpVtbl->SetBlob(type, &MF_MT_MINIMUM_DISPLAY_APERTURE,
                                    (const UINT8 *)&info->MinimumDisplayAperture, 16));
    if (info->PanScanAperture.Area.cx && info->PanScanAperture.Area.cy)
        CHECK(type->lpVtbl->SetBlob(type, &MF_MT_PAN_SCAN_APERTURE, (const UINT8 *)&info->PanScanAperture, 16));
    if (info->PixelAspectRatio.Denominator && info->PixelAspectRatio.Numerator)
        CHECK(type->lpVtbl->SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO,
                                      pack(info->PixelAspectRatio.Numerator, info->PixelAspectRatio.Denominator)));
    if (info->ColorPrimaries)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_PRIMARIES, info->ColorPrimaries));
    if (info->InterlaceMode)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_INTERLACE_MODE, info->InterlaceMode));
    if (info->NominalRange)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_NOMINAL_RANGE, info->NominalRange));
    if (info->TransferFunction)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_TRANSFER_FUNCTION, info->TransferFunction));
    if (info->TransferMatrix)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_YUV_MATRIX, info->TransferMatrix));
    if (info->SourceChromaSubsampling)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_CHROMA_SITING, info->SourceChromaSubsampling));
    if (info->SourceLighting)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_LIGHTING, info->SourceLighting));
    if (info->VideoFlags & 3)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_PAD_CONTROL_FLAGS, (UINT32)(info->VideoFlags & 3)));
    if (info->VideoFlags & 0x1C)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_SOURCE_CONTENT_HINT, (UINT32)(info->VideoFlags >> 2) & 7));
    if (info->VideoFlags & 0x60)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_DRM_FLAGS, (UINT32)(info->VideoFlags >> 5) & 3));
    if (info->VideoFlags & 0x20000)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_PAN_SCAN_ENABLED, 1));
    if (format->compressedInfo.AvgBitErrorRate)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_AVG_BIT_ERROR_RATE, (UINT32)format->compressedInfo.AvgBitErrorRate));
    if (format->compressedInfo.AvgBitrate)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_AVG_BITRATE, (UINT32)format->compressedInfo.AvgBitrate));
    if (format->compressedInfo.MaxKeyFrameSpacing)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_MAX_KEYFRAME_SPACING, format->compressedInfo.MaxKeyFrameSpacing));

    BOOL compressed = in_list(COMPRESSED, RTL_NUMBER_OF(COMPRESSED), fourcc);
    LONG row = find_format(fourcc);
    if (info->dwWidth && info->dwHeight && fourcc && !compressed && row >= 0) {
        BOOL upwards = in_list(BOTTOM_UP, RTL_NUMBER_OF(BOTTOM_UP), fourcc) && (info->VideoFlags & 0x80000);
        LONG stride = default_stride(fourcc, info->dwWidth, upwards);
        if (stride)
            CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_DEFAULT_STRIDE, (UINT32)stride));
    }
    if (row >= 0)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1));
    if (row >= 0 && !compressed)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_FIXED_SIZE_SAMPLES, 1));
    if (compressed)
        CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_COMPRESSED, 1));
    if (row >= 0 && !compressed) {
        UINT32 bytes = 0;
        CHECK(image_size(row, info->dwWidth, info->dwHeight, &bytes));
        if (bytes)
            CHECK(type->lpVtbl->SetUINT32(type, &MF_MT_SAMPLE_SIZE, bytes));
    }
    if (extra)
        return type->lpVtbl->SetBlob(type, &MF_MT_USER_DATA, extra_bytes, extra);
    return S_OK;
#undef CHECK
}

static UINT32 uint32_or_zero(IMFMediaType *type, REFGUID key)
{
    UINT32 value = 0;
    if (FAILED(type->lpVtbl->GetUINT32(type, key, &value)))
        value = 0;
    return value;
}

HRESULT WINAPI MFCreateMFVideoFormatFromMFMediaType(IMFMediaType *type, MFVIDEOFORMAT **out, UINT32 *out_size)
{
    *out = nullptr;
    UINT32 palette_bytes = 0, user = 0;
    type->lpVtbl->GetBlobSize(type, &MF_MT_PALETTE, &palette_bytes);
    UINT32 palette = palette_bytes >> 2;
    type->lpVtbl->GetBlobSize(type, &MF_MT_USER_DATA, &user);
    UINT32 fixed = palette ? 4 * palette + 172 : (UINT32)sizeof(MFVIDEOFORMAT), size = fixed + user;
    if (out_size)
        *out_size = size;
    MFVIDEOFORMAT *format = CoTaskMemAlloc(size);
    if (!format)
        return E_OUTOFMEMORY;
    memset(format, 0, size);
    format->dwSize = size;
    MFVIDEOINFO *info = &format->videoInfo;
    type->lpVtbl->GetGUID(type, &MF_MT_SUBTYPE, &format->guidFormat);
    format->surfaceInfo.Format = format->guidFormat.Data1;
    format->surfaceInfo.PaletteEntries = palette;
    HRESULT hr = S_OK;
    if (palette &&
        FAILED(type->lpVtbl->GetBlob(type, &MF_MT_PALETTE, (UINT8 *)format->surfaceInfo.Palette, 4 * palette, nullptr)))
        goto invalid;
    UINT64 pair = 0;
    if (SUCCEEDED(type->lpVtbl->GetUINT64(type, &MF_MT_FRAME_SIZE, &pair))) {
        info->dwWidth = (DWORD)(pair >> 32);
        info->dwHeight = (DWORD)pair;
    }
    pair = 0;
    if (SUCCEEDED(type->lpVtbl->GetUINT64(type, &MF_MT_FRAME_RATE, &pair))) {
        info->FramesPerSecond.Numerator = (DWORD)(pair >> 32);
        info->FramesPerSecond.Denominator = (DWORD)pair;
    }
    pair = 0;
    if (FAILED(type->lpVtbl->GetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, &pair))) {
        info->PixelAspectRatio.Numerator = info->PixelAspectRatio.Denominator = 1;
    } else {
        info->PixelAspectRatio.Numerator = (DWORD)(pair >> 32);
        info->PixelAspectRatio.Denominator = (DWORD)pair;
        if (!pair)
            goto invalid;
    }
    info->InterlaceMode = uint32_or_zero(type, &MF_MT_INTERLACE_MODE);
    info->ColorPrimaries = uint32_or_zero(type, &MF_MT_VIDEO_PRIMARIES);
    info->NominalRange = uint32_or_zero(type, &MF_MT_VIDEO_NOMINAL_RANGE);
    info->SourceChromaSubsampling = uint32_or_zero(type, &MF_MT_VIDEO_CHROMA_SITING);
    info->TransferFunction = uint32_or_zero(type, &MF_MT_TRANSFER_FUNCTION);
    info->SourceLighting = uint32_or_zero(type, &MF_MT_VIDEO_LIGHTING);
    info->TransferMatrix = uint32_or_zero(type, &MF_MT_YUV_MATRIX);
    type->lpVtbl->GetBlob(type, &MF_MT_GEOMETRIC_APERTURE, (UINT8 *)&info->GeometricAperture, 16, nullptr);
    type->lpVtbl->GetBlob(type, &MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8 *)&info->MinimumDisplayAperture, 16, nullptr);
    type->lpVtbl->GetBlob(type, &MF_MT_PAN_SCAN_APERTURE, (UINT8 *)&info->PanScanAperture, 16, nullptr);
    info->VideoFlags |= uint32_or_zero(type, &MF_MT_PAD_CONTROL_FLAGS) & 3;
    info->VideoFlags |= (ULONGLONG)(uint32_or_zero(type, &MF_MT_SOURCE_CONTENT_HINT) & 7) << 2;
    info->VideoFlags |= (ULONGLONG)(uint32_or_zero(type, &MF_MT_DRM_FLAGS) & 3) << 5;
    if (uint32_or_zero(type, &MF_MT_PAN_SCAN_ENABLED))
        info->VideoFlags |= 0x20000;
    if ((LONG)uint32_or_zero(type, &MF_MT_DEFAULT_STRIDE) < 0)
        info->VideoFlags |= 0x80000;
    format->compressedInfo.AvgBitErrorRate = uint32_or_zero(type, &MF_MT_AVG_BIT_ERROR_RATE);
    format->compressedInfo.AvgBitrate = uint32_or_zero(type, &MF_MT_AVG_BITRATE);
    format->compressedInfo.MaxKeyFrameSpacing = uint32_or_zero(type, &MF_MT_MAX_KEYFRAME_SPACING);
    if (user)
        hr = type->lpVtbl->GetBlob(type, &MF_MT_USER_DATA, (UINT8 *)format + size - user, user, nullptr);
    if (FAILED(hr))
        goto failed;
    *out = format;
    return hr;
invalid:
    hr = E_INVALIDARG;
failed:
    CoTaskMemFree(format);
    return hr;
}

HRESULT WINAPI MFCreateVideoMediaTypeFromSubtype(const GUID *subtype, IMFMediaType **out)
{
    *out = nullptr;
    IMFMediaType *type;
    HRESULT hr = MfCreateVideoMediaType(&type);
    if (FAILED(hr))
        return hr;
    hr = type->lpVtbl->SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->SetGUID(type, &MF_MT_SUBTYPE, subtype);
    if (FAILED(hr)) {
        type->lpVtbl->Release(type);
        return hr;
    }
    *out = type;
    return hr;
}

HRESULT WINAPI MFCreateVideoMediaType(const MFVIDEOFORMAT *format, IMFMediaType **out)
{
    *out = nullptr;
    IMFMediaType *type;
    HRESULT hr = MfCreateVideoMediaType(&type);
    if (FAILED(hr))
        return hr;
    hr = format ? MFInitMediaTypeFromMFVideoFormat(type, format, format->dwSize)
                : type->lpVtbl->SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (FAILED(hr)) {
        type->lpVtbl->Release(type);
        return hr;
    }
    *out = type;
    return hr;
}

#ifndef MF_E_INVALIDMEDIATYPE
#define MF_E_INVALIDMEDIATYPE ((HRESULT)0xC00D36B4L)
#endif

static const GUID MF_MT_BITCOUNT = {0xC496F370, 0x2F8B, 0x4F51, {0xAE, 0x46, 0x9C, 0xFC, 0x1B, 0xC8, 0x2A, 0x47}};

/* the bitfield masks of the three rows of the table that have them. */
static const DWORD *row_masks(ULONG row)
{
    static const DWORD NONE[3], RGB555[3] = {0x7C00, 0x3E0, 0x1F}, ARGB32[3] = {0xFF0000, 0xFF00, 0xFF},
                                RGB565[3] = {0xF800, 0x7E0, 0x1F};
    return row == 5 ? RGB555 : row == 8 ? ARGB32 : row == 9 ? RGB565 : NONE;
}

/* the format of an image that is not compressed comes from its depth, and its masks for bitfields. */
static HRESULT format_of_header(const BITMAPINFOHEADER *header, UINT32 size, DWORD *format)
{
    if (size < 40)
        return E_INVALIDARG;
    DWORD compression = header->biCompression;
    if (compression != BI_RGB && compression != BI_BITFIELDS && compression != FCC('C', 'R', 'A', 'M')) {
        *format = compression;
        return S_OK;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(FORMATS); i++) {
        if (FORMATS[i].compression == 0xFFFFFFFF || FORMATS[i].compression != compression ||
            header->biBitCount != FORMATS[i].bits / FORMATS[i].group)
            continue;
        if (compression == BI_BITFIELDS) {
            if (size < 52)
                return E_INVALIDARG;
            const DWORD *masks = (const DWORD *)(header + 1), *want = row_masks(i);
            if (masks[0] != want[0] || masks[1] != want[1] || masks[2] != want[2])
                continue;
        }
        *format = FORMATS[i].format;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT image_size_of(DWORD format, UINT32 width, UINT32 height, UINT32 *size)
{
    *size = 0;
    LONG row = find_format(format);
    if (in_list(COMPRESSED, RTL_NUMBER_OF(COMPRESSED), format) || row < 0)
        return S_OK;
    return image_size(row, width, height, size);
}

HRESULT WINAPI MFCalculateImageSize(REFGUID subtype, UINT32 width, UINT32 height, UINT32 *size)
{
    DWORD format = format_of_subtype(subtype);
    *size = 0;
    if (format) {
        HRESULT hr = image_size_of(format, width, height, size);
        if (FAILED(hr))
            return hr;
    }
    return *size ? S_OK : E_INVALIDARG;
}

/* a subtype that is given wins over the header, and is kept as it is when it names no format mfplat knows. */
static HRESULT add_header_fields(IMFMediaType *type, const RECT *source, const BITMAPINFOHEADER *header, UINT32 size,
                                 const GUID *subtype)
{
    HRESULT hr;
    DWORD format = 0;
    BOOL bitfields = FALSE;
    GUID sub = FORMAT_BASE;
    if (header->biSize > size)
        return E_INVALIDARG;
    if (subtype) {
        format = format_of_subtype(subtype);
    } else {
        hr = format_of_header(header, size, &format);
        if (FAILED(hr))
            return hr;
        if (!format)
            return E_INVALIDARG;
    }
    if (format) {
        sub.Data1 = format;
        if (header->biCompression == BI_BITFIELDS) {
            if (header->biBitCount != 16 && header->biBitCount != 32)
                return MF_E_INVALIDMEDIATYPE;
            bitfields = TRUE;
        }
    } else {
        sub = *subtype;
    }
    hr = type->lpVtbl->SetGUID(type, &MF_MT_SUBTYPE, &sub);
    if (FAILED(hr))
        return hr;

    const BYTE *palette = nullptr, *user = nullptr;
    UINT32 palette_size = 0, user_size = 0;
    WORD bits = header->biBitCount;
    if (bits >= 1 && bits <= 8) {
        UINT32 colours = 1u << bits, stated = 0;
        if (header->biCompression || header->biClrUsed)
            colours = stated = header->biClrUsed;
        if (colours > 256)
            return MF_E_INVALIDMEDIATYPE;
        if (colours) {
            palette_size = 4 * colours;
            palette = (const BYTE *)header + header->biSize;
            if (palette_size + header->biSize > size) {
                if (stated)
                    return MF_E_INVALIDMEDIATYPE;
                palette_size = 0;
                palette = nullptr;
            }
        }
    }
    /* the bytes after the header are kept as user data, unless they hold a palette or masks. */
    if (!palette_size && !bitfields) {
        if (header->biSize > 40) {
            user_size = header->biSize - 40;
            user = (const BYTE *)header + 40;
        } else if (size > 40) {
            user_size = size - 40;
            user = (const BYTE *)header + 40;
        }
    }

    UINT32 width = 0, height = 0;
    LONG header_width = header->biWidth, header_height = header->biHeight;
    if (header_width && header_height) {
        LONG source_width = source->right - source->left, source_height = source->bottom - source->top;
        height = header_height < 0 ? (UINT32)-header_height : (UINT32)header_height;
        width = (UINT32)header_width;
        if (source_width && source_height) {
            if (source->left || source->top || source_width != header_width || source_height != header_height) {
                hr = type->lpVtbl->SetUINT32(type, &MF_MT_PAN_SCAN_ENABLED, 1);
                if (FAILED(hr))
                    return hr;
                MFVideoArea area = {0};
                area.OffsetX.value = (SHORT)source->left;
                area.OffsetY.value = (SHORT)source->top;
                area.Area.cx = source_width;
                area.Area.cy = source_height;
                hr = type->lpVtbl->SetBlob(type, &MF_MT_PAN_SCAN_APERTURE, (const UINT8 *)&area, sizeof(area));
                if (FAILED(hr))
                    return hr;
                hr = type->lpVtbl->SetBlob(type, &MF_MT_MINIMUM_DISPLAY_APERTURE, (const UINT8 *)&area, sizeof(area));
                if (FAILED(hr))
                    return hr;
                /* read from mfplat: a 540 wide source in a 768 wide frame is called 720 wide. */
                if (header_width == 768 && source_width == 540)
                    width = 720;
            } else {
                height = (UINT32)source_height;
            }
        }
        hr = type->lpVtbl->SetUINT64(type, &MF_MT_FRAME_SIZE, pack(width, height));
        if (FAILED(hr))
            return hr;
        if (format && !in_list(COMPRESSED, RTL_NUMBER_OF(COMPRESSED), format) && find_format(format) >= 0) {
            BOOL upwards = in_list(BOTTOM_UP, RTL_NUMBER_OF(BOTTOM_UP), format) && header_height >= 0;
            LONG stride = default_stride(format, (UINT32)header_width, upwards);
            if (stride) {
                hr = type->lpVtbl->SetUINT32(type, &MF_MT_DEFAULT_STRIDE, (UINT32)stride);
                if (FAILED(hr))
                    return hr;
            }
        }
    }

    LONG row = find_format(format);
    if (row >= 0) {
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
        if (FAILED(hr))
            return hr;
    }
    UINT32 sample = header->biSizeImage;
    if (row >= 0 && !in_list(COMPRESSED, RTL_NUMBER_OF(COMPRESSED), format)) {
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_FIXED_SIZE_SAMPLES, 1);
        if (FAILED(hr))
            return hr;
        if (!sample) {
            hr = image_size_of(format, width, height, &sample);
            if (FAILED(hr))
                return hr;
        }
    }
    if (sample) {
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_SAMPLE_SIZE, sample);
        if (FAILED(hr))
            return hr;
    }
    if (palette_size) {
        hr = type->lpVtbl->SetBlob(type, &MF_MT_PALETTE, palette, palette_size);
        if (FAILED(hr))
            return hr;
    }
    if (user_size)
        return type->lpVtbl->SetBlob(type, &MF_MT_USER_DATA, user, user_size);
    return S_OK;
}

/* the common frame rates are matched within 3 microseconds, the others are reduced from the frame time. */
static UINT64 frame_rate(ULONGLONG time)
{
    static const struct {
        ULONGLONG time;
        UINT32 numerator, denominator;
    } COMMON[] = {
        {166833, 60000, 1001}, {333667, 30000, 1001}, {417188, 24000, 1001}, {166667, 60, 1}, {333333, 30, 1},
        {200000, 50, 1},       {400000, 25, 1},       {416667, 24, 1},       {666667, 15, 1},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(COMMON); i++)
        if (time >= COMMON[i].time - 30 && time <= COMMON[i].time + 30)
            return pack(COMMON[i].numerator, COMMON[i].denominator);
    UINT32 a = (UINT32)time > 10000000 ? (UINT32)time : 10000000, b = (UINT32)time > 10000000 ? 10000000 : (UINT32)time;
    while (b) {
        UINT32 r = a % b;
        a = b;
        b = r;
    }
    return pack(10000000 / a, (UINT32)time / a);
}

static HRESULT init_from_vih_fields(IMFMediaType *type, const BITMAPINFOHEADER *header, UINT32 size,
                                    const RECT *source, DWORD bitrate, DWORD errors, ULONGLONG time,
                                    const GUID *subtype)
{
    HRESULT hr = type->lpVtbl->DeleteAllItems(type);
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(hr))
        hr = add_header_fields(type, source, header, size, subtype);
    if (SUCCEEDED(hr) && errors)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AVG_BIT_ERROR_RATE, errors);
    if (SUCCEEDED(hr) && bitrate)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AVG_BITRATE, bitrate);
    if (SUCCEEDED(hr) && time - 1 <= 99999998)
        hr = type->lpVtbl->SetUINT64(type, &MF_MT_FRAME_RATE, frame_rate(time));
    return hr;
}

static HRESULT set_from_video_flags(IMFMediaType *type, ULONGLONG flags)
{
    HRESULT hr = S_OK;
    if (flags & 3)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_PAD_CONTROL_FLAGS, (UINT32)(flags & 3));
    if (SUCCEEDED(hr) && (flags & 0x1C))
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_SOURCE_CONTENT_HINT, (UINT32)(flags >> 2) & 7);
    if (SUCCEEDED(hr) && (flags & 0x60))
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_DRM_FLAGS, (UINT32)(flags >> 5) & 3);
    if (SUCCEEDED(hr) && (flags & 0x20000))
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_PAN_SCAN_ENABLED, 1);
    return hr;
}

HRESULT WINAPI MFInitMediaTypeFromVideoInfoHeader(IMFMediaType *type, const VIDEOINFOHEADER *vih, UINT32 size,
                                                  const GUID *subtype)
{
    if (size < sizeof(VIDEOINFOHEADER))
        return E_INVALIDARG;
    HRESULT hr = init_from_vih_fields(type, &vih->bmiHeader, size - offsetof(VIDEOINFOHEADER, bmiHeader),
                                      &vih->rcSource, vih->dwBitRate, vih->dwBitErrorRate, vih->AvgTimePerFrame,
                                      subtype);
    if (SUCCEEDED(hr) && vih->bmiHeader.biWidth && vih->bmiHeader.biHeight) {
        hr = type->lpVtbl->SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, pack(1, 1));
        if (SUCCEEDED(hr))
            hr = type->lpVtbl->SetUINT32(type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    }
    return hr;
}

static UINT32 gcd(UINT32 a, UINT32 b)
{
    if (a < b) {
        UINT32 t = a;
        a = b;
        b = t;
    }
    while (b) {
        UINT32 r = a % b;
        a = b;
        b = r;
    }
    return a;
}

/* the pixel aspect ratio that makes a picture of width by height look as wide as ax by ay. */
static void picture_to_pixel_aspect(UINT32 width, UINT32 height, UINT32 ax, UINT32 ay, UINT32 *px, UINT32 *py)
{
    UINT32 g = gcd(width, height);
    if (g) {
        width /= g;
        height /= g;
    }
    g = gcd(ax, ay);
    if (g) {
        ax /= g;
        ay /= g;
    }
    while (width > 0xFFFF || height > 0xFFFF) {
        width >>= 1;
        height >>= 1;
    }
    while (ax > 0xFFFF || ay > 0xFFFF) {
        ax >>= 1;
        ay >>= 1;
    }
    g = gcd(ay * width, ax * height);
    if (g) {
        *px = ax * height / g;
        *py = ay * width / g;
    }
    if (!ax || !ay || !width || !height)
        *px = *py = 0;
}

HRESULT WINAPI MFInitMediaTypeFromVideoInfoHeader2(IMFMediaType *type, const VIDEOINFOHEADER2 *vih, UINT32 size,
                                                   const GUID *subtype)
{
    if (size < sizeof(VIDEOINFOHEADER2))
        return E_INVALIDARG;
    HRESULT hr = init_from_vih_fields(type, &vih->bmiHeader, size - offsetof(VIDEOINFOHEADER2, bmiHeader),
                                      &vih->rcSource, vih->dwBitRate, vih->dwBitErrorRate, vih->AvgTimePerFrame,
                                      subtype);
    if (FAILED(hr))
        return hr;
    UINT64 frame = 0;
    HRESULT have_size = type->lpVtbl->GetUINT64(type, &MF_MT_FRAME_SIZE, &frame);
    UINT32 width = 0, height = 0;
    if (SUCCEEDED(have_size)) {
        width = (UINT32)(frame >> 32);
        height = (UINT32)frame;
    }
    hr = SUCCEEDED(have_size) ? have_size : S_OK;
    if (vih->dwPictAspectRatioX && vih->dwPictAspectRatioY && width && height) {
        UINT32 target_width = vih->rcTarget.right - vih->rcTarget.left;
        UINT32 target_height = vih->rcTarget.bottom - vih->rcTarget.top;
        UINT32 px = 0, py = 0;
        if (!target_width || !target_height) {
            target_width = width;
            target_height = height;
        }
        picture_to_pixel_aspect(target_width, target_height, vih->dwPictAspectRatioX, vih->dwPictAspectRatioY, &px,
                                &py);
        if (px && py) {
            hr = type->lpVtbl->SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, pack(px, py));
            if (FAILED(hr))
                return hr;
        }
    }
    DWORD interlace = vih->dwInterlaceFlags;
    if ((interlace & AMINTERLACE_IsInterlaced) || SUCCEEDED(have_size)) {
        UINT32 mode = MFVideoInterlace_Progressive;
        if (interlace & AMINTERLACE_IsInterlaced) {
            BOOL either = (interlace & AMINTERLACE_DisplayModeMask) == AMINTERLACE_DisplayModeBobOrWeave;
            if (interlace & AMINTERLACE_1FieldPerSample)
                mode = (interlace & AMINTERLACE_Field1First) ? MFVideoInterlace_SingleFieldUpper
                       : either                              ? MFVideoInterlace_MixedInterlaceOrProgressive
                                                             : MFVideoInterlace_SingleFieldLower;
            else
                mode = (interlace & AMINTERLACE_Field1First) ? MFVideoInterlace_FieldInterleavedUpperFirst
                       : either                              ? MFVideoInterlace_MixedInterlaceOrProgressive
                                                             : MFVideoInterlace_FieldInterleavedLowerFirst;
        }
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_INTERLACE_MODE, mode);
        if (FAILED(hr))
            return hr;
    }
    DWORD control = vih->dwControlFlags;
    if (control & (AMCONTROL_PAD_TO_16x9 | AMCONTROL_PAD_TO_4x3)) {
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_PAD_CONTROL_FLAGS, (control & AMCONTROL_PAD_TO_16x9) ? 2 : 1);
        if (FAILED(hr))
            return hr;
    }
    if (!(control & AMCONTROL_COLORINFO_PRESENT))
        return hr;
    /* the fields of DXVA_ExtendedFormat in the high bits, each dropped when out of its range. */
    UINT32 chroma = (control >> 8) & 0xF, range = (control >> 12) & 7, matrix = (control >> 15) & 7;
    UINT32 lighting = (control >> 18) & 0xF, primaries = (control >> 22) & 0x1F, transfer = control >> 27;
    if (chroma >= 8)
        chroma = 0;
    if (range >= 5)
        range = 0;
    if (lighting >= 5)
        lighting = 0;
    if (primaries >= 14)
        primaries = 0;
    if (transfer >= 20)
        transfer = 0;
    if (chroma && FAILED(hr = type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_CHROMA_SITING, chroma)))
        return hr;
    if (range && FAILED(hr = type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_NOMINAL_RANGE, range)))
        return hr;
    if (lighting && FAILED(hr = type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_LIGHTING, lighting)))
        return hr;
    if (primaries && FAILED(hr = type->lpVtbl->SetUINT32(type, &MF_MT_VIDEO_PRIMARIES, primaries)))
        return hr;
    if (transfer && FAILED(hr = type->lpVtbl->SetUINT32(type, &MF_MT_TRANSFER_FUNCTION, transfer)))
        return hr;
    if (matrix)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_YUV_MATRIX, matrix);
    return hr;
}

static HRESULT finish_made(IMFMediaType **out, IMFMediaType *type, HRESULT hr)
{
    if (FAILED(hr)) {
        if (type)
            type->lpVtbl->Release(type);
        *out = nullptr;
        return hr;
    }
    *out = type;
    return hr;
}

HRESULT WINAPI MFCreateVideoMediaTypeFromVideoInfoHeader(const VIDEOINFOHEADER *vih, DWORD size, DWORD par_x,
                                                         DWORD par_y, MFVideoInterlaceMode interlace,
                                                         QWORD flags, const GUID *subtype, IMFMediaType **out)
{
    IMFMediaType *type = nullptr;
    *out = nullptr;
    HRESULT hr = MfCreateVideoMediaType(&type);
    if (SUCCEEDED(hr))
        hr = MFInitMediaTypeFromVideoInfoHeader(type, vih, size, subtype);
    if (SUCCEEDED(hr) && par_x && par_y)
        hr = type->lpVtbl->SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, pack(par_x, par_y));
    if (SUCCEEDED(hr) && interlace != MFVideoInterlace_Unknown)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_INTERLACE_MODE, interlace);
    if (SUCCEEDED(hr))
        hr = set_from_video_flags(type, flags);
    return finish_made(out, type, hr);
}

HRESULT WINAPI MFCreateVideoMediaTypeFromVideoInfoHeader2(const VIDEOINFOHEADER2 *vih, DWORD size, QWORD flags,
                                                          const GUID *subtype, IMFMediaType **out)
{
    IMFMediaType *type = nullptr;
    *out = nullptr;
    HRESULT hr = MfCreateVideoMediaType(&type);
    if (SUCCEEDED(hr))
        hr = MFInitMediaTypeFromVideoInfoHeader2(type, vih, size, subtype);
    if (SUCCEEDED(hr))
        hr = set_from_video_flags(type, flags);
    return finish_made(out, type, hr);
}

HRESULT WINAPI MFCreateVideoMediaTypeFromBitMapInfoHeaderEx(const BITMAPINFOHEADER *header, UINT32 size,
                                                            DWORD par_x, DWORD par_y,
                                                            MFVideoInterlaceMode interlace, QWORD flags,
                                                            DWORD fps_numerator, DWORD fps_denominator,
                                                            DWORD max_bitrate, IMFMediaType **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!header)
        return E_POINTER;
    IMFMediaType *type = nullptr;
    static const RECT NO_SOURCE;
    HRESULT hr = MfCreateVideoMediaType(&type);
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->DeleteAllItems(type);
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(hr))
        hr = add_header_fields(type, &NO_SOURCE, header, size, nullptr);
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_BITCOUNT, header->biBitCount);
    if (SUCCEEDED(hr) && par_x && par_y)
        hr = type->lpVtbl->SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, pack(par_x, par_y));
    if (SUCCEEDED(hr) && (interlace < MFVideoInterlace_Progressive || interlace > MFVideoInterlace_MixedInterlaceOrProgressive))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_INTERLACE_MODE, interlace);
    if (SUCCEEDED(hr) && max_bitrate)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AVG_BITRATE, max_bitrate);
    if (SUCCEEDED(hr) && fps_numerator && fps_denominator)
        hr = type->lpVtbl->SetUINT64(type, &MF_MT_FRAME_RATE, pack(fps_numerator, fps_denominator));
    if (SUCCEEDED(hr))
        hr = set_from_video_flags(type, flags);
    return finish_made(out, type, hr);
}
