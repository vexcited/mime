/*
 * helpers shared by the codecs of windowscodecs.dll.
 */
#ifndef WIC_CODEC_UTIL_H
#define WIC_CODEC_UTIL_H

#include "wic_codec.h"

/* a bitmap of the bmp and ico files, parsed from its header. */
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

/* the header of a bitmap, as an icon holds it when icon is set: its height counts the mask too. */
HRESULT WicDibParse(const BYTE *header, SIZE_T size, BOOL icon, WicDib *dib);
/* decodes the bits to rows in the format of the bitmap, top down. */
HRESULT WicDibDecode(const WicDib *dib, const BYTE *bits, SIZE_T bits_size, BYTE *dest, UINT stride);

#endif
