#ifndef MIME_D2D_MIME_H
#define MIME_D2D_MIME_H

#include <mime/win32/d2d1.h>

/*
 * implemented by bitmaps: the pixels in the bitmap's own format, current once the context that draws to it ended its
 * draw. dcomp.dll uploads from them the part of a surface the program drew with direct2d.
 */
typedef struct IMimeD2DBitmap IMimeD2DBitmap;
#define IMimeD2DBitmap_METHODS(T)                                                                                      \
    ID2D1Bitmap1_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *GetPixels)(T * This, BYTE * *bits, UINT32 * pitch);
MIME_COM_INTERFACE(IMimeD2DBitmap)
MIME_DEFINE_GUID(IID_IMimeD2DBitmap, 0x4D7A2C19, 0x8E36, 0x4A5F, 0xB1, 0x0D, 0x63, 0xF2, 0x9A, 0x47, 0xC8, 0x5E);

#endif
