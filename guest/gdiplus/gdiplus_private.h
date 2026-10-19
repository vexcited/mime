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
#define GP_MATRIX_MAGIC 0x7274614du   /* 'Matr'. */
#define GP_BRUSH_MAGIC 0x68737242u    /* 'Brsh'. */
#define GP_PEN_MAGIC 0x206e6550u      /* 'Pen '. */

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
typedef struct GpProperty {
    ULONG Id;
    WORD Type;
    ULONG Length;
    BYTE *Value;
} GpProperty;

struct GpBitmap {
    GpImage Image;
    INT Width;
    INT Height;
    PixelFormat Format;
    INT Stride;
    BYTE *Bits;
    BYTE *Owned;
    /* the resolution in dots per inch, and the format the pixels came from, memory bitmap when made here. */
    float DpiX;
    float DpiY;
    GUID RawFormat;
    BOOL Decoded;
    GpProperty *Properties;
    UINT PropertyCount;
    /* the state of the current GdipBitmapLockBits. */
    BOOL Locked;
    GpRect LockRect;
    UINT LockFlags;
    PixelFormat LockFormat;
    BYTE *LockScan0;
    INT LockStride;
    BYTE *LockBuffer;
};

struct GpMatrix {
    DWORD Magic;
    float M[6]; /* m11 m12 m21 m22 dx dy, points are rows. */
};

struct GpBrush {
    DWORD Magic;
    BrushType Type;
    ARGB Color;
};

struct GpPen {
    DWORD Magic;
    GpBrush *Brush;
    float Width;
    GpUnit Unit;
    DashStyle Dash;
};

struct GpGraphics {
    DWORD Magic;
    HDC Hdc;          /* the target device context, or NULL. */
    GpBitmap *Target; /* the target bitmap of GdipGetImageGraphicsContext, or NULL. */
    InterpolationMode Interpolation;
    SmoothingMode Smoothing;
    CompositingMode Compositing;
    CompositingQuality CompositingQuality;
    PixelOffsetMode PixelOffset;
    GpUnit PageUnit;
    float PageScale;
    float World[6];
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

/* graphics.c: the part of a graphics that fills with a color, in device pixels. */
GpStatus gp_fill_rect(GpGraphics *graphics, ARGB color, INT left, INT top, INT right, INT bottom);

/* imageinfo.c */
GpStatus gp_bitmap_copy(const GpBitmap *source, GpBitmap **copy);
extern const GUID gp_format_memory_bmp;
extern const GUID gp_format_bmp;
extern const GUID gp_format_png;

/* encode.c */
typedef struct GpBuffer {
    BYTE *Data;
    SIZE_T Size;
    SIZE_T Capacity;
} GpBuffer;
BOOL gp_buffer_append(GpBuffer *buffer, const void *data, SIZE_T size);
GpStatus gp_encode_bmp(const GpBitmap *bitmap, GpBuffer *out);
GpStatus gp_encode_png(const GpBitmap *bitmap, GpBuffer *out);

/* matrix.c */
void gp_matrix_multiply(const float *a, const float *b, float *out);
void gp_matrix_point(const float *m, float x, float y, float *ox, float *oy);

#endif
