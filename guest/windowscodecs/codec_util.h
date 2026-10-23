/* shared codec helpers. */
#ifndef WIC_CODEC_UTIL_H
#define WIC_CODEC_UTIL_H

#include "wic_codec.h"

/* BMP or ICO bitmap header fields. */
typedef struct WicDib {
    UINT width;
    UINT height;
    BOOL top_down;
    UINT bpp;
    UINT compression;
    UINT masks[4];
    GUID format;
    WICColor palette[256];
    UINT palette_count;
    UINT bits_offset; /* from the header, past the palette. */
    double dpi_x;
    double dpi_y;
} WicDib;

/* icon bitmap heights include the mask. */
HRESULT WicDibParse(const BYTE *header, SIZE_T size, BOOL icon, WicDib *dib);
/* decodes the bits to rows in the format of the bitmap, top down. */
HRESULT WicDibDecode(const WicDib *dib, const BYTE *bits, SIZE_T bits_size, BYTE *dest, UINT stride);

/*
 * metadata resolution uses single-precision DPI. one inch is 25.4 millimeters: scale per-meter density by 0.0254 and
 * per-centimeter density by 2.54.
 */
static inline double WicDpiFromMeter(INT per_meter) { return (double)(float)((double)per_meter * 254.0 / 10000.0); }
static inline double WicDpiFromCentimeter(UINT per_cm) { return (double)(float)((double)per_cm * 2.54); }

#endif
