/* pixel formats and their IWICPixelFormatInfo properties. */
#include "wic_private.h"

static const WicFormat g_formats[] = {
#include "pixfmt_table.inc"
};

const WicFormat *WicFindFormat(const GUID *guid)
{
    if (!guid)
        return nullptr;
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_formats); i++) {
        if (IsEqualGUID(g_formats[i].guid, guid))
            return &g_formats[i];
    }
    return nullptr;
}

UINT WicFormatCount(void) { return WIC_ARRAY_SIZE(g_formats); }

const WicFormat *WicFormatAt(UINT index) { return index < WIC_ARRAY_SIZE(g_formats) ? &g_formats[index] : nullptr; }

UINT WicFormatBits(const GUID *format)
{
    const WicFormat *info = WicFindFormat(format);
    return info ? info->bpp : 0;
}

BOOL WicRowBytes(const GUID *format, UINT width, UINT *bytes)
{
    UINT bits = WicFormatBits(format), total;
    if (!bits || !WicMul32(width, bits, &total) || total > 0xFFFFFFF8u)
        return FALSE;
    *bytes = (total + 7) / 8;
    return TRUE;
}

BOOL WicFormatIsIndexed(const GUID *format)
{
    const WicFormat *info = WicFindFormat(format);
    return info && info->numeric == WICPixelFormatNumericRepresentationIndexed;
}

BOOL WicFormatHasAlpha(const GUID *format)
{
    const WicFormat *info = WicFindFormat(format);
    return info && info->alpha;
}

UINT WicFormatPaletteBits(const GUID *format)
{
    return WicFormatIsIndexed(format) ? WicFormatBits(format) : 0;
}
