/*
 * gdiplus internals: the bitmap and graphics objects behind the flat api.
 */
#ifndef MIME_GDIPLUS_PRIVATE_H
#define MIME_GDIPLUS_PRIVATE_H

#define MIME_BUILD_GDIPLUS
#include <mime/win32/gdiplus.h>
#include <mime/win32/winuser.h>
#include <mime/stub.h>

#define GP_IMAGE_MAGIC 0x50696d47u    /* 'Gmip'. */
#define GP_GRAPHICS_MAGIC 0x50726747u /* 'Ggrp'. */

/* larger bitmaps fail with OutOfMemory, which keeps every size and offset in 32 bits. */
#define GP_MAX_DIMENSION 32767
#define GP_MAX_BYTES 0x40000000u

struct GpImage {
    DWORD Magic;
    ImageType Type;
};

/*
 * every bitmap keeps its pixels in its own pixel format, one of the formats
 * gp_format_supported accepts. Bits points at the top row, rows are Stride
 * bytes apart and Stride is negative for bottom-up memory. Owned is the
 * allocation to free, NULL when the pixels belong to the caller of
 * GdipCreateBitmapFromScan0.
 */
struct GpBitmap {
    GpImage Image;
    INT Width;
    INT Height;
    PixelFormat Format;
    INT Stride;
    BYTE *Bits;
    BYTE *Owned;
    /* the state of the current GdipBitmapLockBits. */
    BOOL Locked;
    GpRect LockRect;
    UINT LockFlags;
    PixelFormat LockFormat;
    BYTE *LockScan0;
    INT LockStride;
    BYTE *LockBuffer;
};

struct GpGraphics {
    DWORD Magic;
    HDC Hdc;          /* the target device context, or NULL. */
    GpBitmap *Target; /* the target bitmap of GdipGetImageGraphicsContext, or NULL. */
    InterpolationMode Interpolation;
};

/* main.c */
void *gp_alloc(SIZE_T size);
void *gp_alloc_zero(SIZE_T size);
void gp_free(void *p);

/* image.c */
BOOL gp_format_supported(PixelFormat format);
INT gp_default_stride(INT width, PixelFormat format);
GpBitmap *gp_bitmap_cast(GpImage *image);
GpStatus gp_bitmap_create(INT width, INT height, PixelFormat format, GpBitmap **bitmap);
void gp_convert(BYTE *dst, INT dst_stride, PixelFormat dst_format, const BYTE *src, INT src_stride,
                PixelFormat src_format, INT width, INT height);
ARGB gp_read_pixel(const BYTE *row, INT x, PixelFormat format);
void gp_write_pixel(BYTE *row, INT x, PixelFormat format, ARGB color);
ARGB gp_premultiply(ARGB color);
ARGB gp_unpremultiply(ARGB color);

/* decode.c */
GpStatus gp_decode(const BYTE *data, SIZE_T size, GpBitmap **bitmap);

#endif
