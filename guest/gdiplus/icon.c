/*
 * icons from bitmaps. The color bitmap of the icon is a 32 bit dib section with the alpha of every pixel, bottom up, and
 * the mask is the bitmap of the pixels that are opaque. An icon of the whole bitmap, whatever its size.
 */
#include "gdiplus_private.h"

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateHICONFromBitmap(GpBitmap *bitmap, HICON *icon)
{
    GpBitmap *b = gp_bitmap_cast((GpImage *)bitmap);
    if (!b || !icon)
        return InvalidParameter;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = b->Width;
    info.bmiHeader.biHeight = b->Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (!color || !bits) {
        if (color)
            DeleteObject(color);
        return GenericError;
    }
    SIZE_T mask_stride = ((SIZE_T)b->Width + 15) / 16 * 2;
    BYTE *mask_bits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, mask_stride * (SIZE_T)b->Height);
    if (!mask_bits) {
        DeleteObject(color);
        return OutOfMemory;
    }
    for (INT y = 0; y < b->Height; y++) {
        const BYTE *source = b->Bits + (INT_PTR)y * b->Stride;
        BYTE *row = bits + (SIZE_T)(b->Height - 1 - y) * b->Width * 4;
        BYTE *mask_row = mask_bits + (SIZE_T)(b->Height - 1 - y) * mask_stride;
        for (INT x = 0; x < b->Width; x++) {
            ARGB c = gp_read_pixel(source, x, b->Format);
            row[x * 4] = (BYTE)c;
            row[x * 4 + 1] = (BYTE)(c >> 8);
            row[x * 4 + 2] = (BYTE)(c >> 16);
            row[x * 4 + 3] = (BYTE)(c >> 24);
            /* a pixel with no alpha is transparent through the mask, the others are drawn. */
            if (!(c >> 24))
                mask_row[x / 8] |= (BYTE)(0x80 >> (x % 8));
        }
    }
    HBITMAP mask = CreateBitmap(b->Width, b->Height, 1, 1, mask_bits);
    HeapFree(GetProcessHeap(), 0, mask_bits);
    if (!mask) {
        DeleteObject(color);
        return GenericError;
    }
    ICONINFO made = {TRUE, 0, 0, mask, color};
    *icon = CreateIconIndirect(&made);
    DeleteObject(color);
    DeleteObject(mask);
    return *icon ? Ok : GenericError;
}
