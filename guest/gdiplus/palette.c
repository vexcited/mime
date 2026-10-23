/* predefined palettes and palettes made from a picture, through the WIC palette object. */
#include "gp_private.h"
#include <mime/win32/wincodec.h>

GpStatus gp_wic_factory(IWICImagingFactory **factory);

/* returns the entry count. */
static UINT wic_palette_entries(WICBitmapPaletteType type, BOOL transparent, GBitmap *source, UINT count, ARGB *out)
{
    IWICImagingFactory *factory;
    if (gp_wic_factory(&factory) != Ok)
        return 0;
    UINT n = 0;
    IWICPalette *pal = nullptr;
    if (SUCCEEDED(factory->lpVtbl->CreatePalette(factory, &pal))) {
        HRESULT hr;
        if (source) {
            IWICBitmap *bitmap = nullptr;
            UINT stride = (UINT)source->width * 4;
            BYTE *rows = gp_alloc((SIZE_T)stride * source->height);
            if (rows) {
                for (INT y = 0; y < source->height; y++) {
                    UINT32 *row = (UINT32 *)(rows + (SIZE_T)y * stride);
                    gp_bitmap_read_argb(source, 0, y, source->width, row);
                }
                hr = factory->lpVtbl->CreateBitmapFromMemory(factory, source->width, source->height,
                                                             &GUID_WICPixelFormat32bppBGRA, stride,
                                                             stride * source->height, rows, &bitmap);
                if (SUCCEEDED(hr)) {
                    hr = pal->lpVtbl->InitializeFromBitmap(pal, (IWICBitmapSource *)bitmap, count, transparent);
                    bitmap->lpVtbl->Release(bitmap);
                } else {
                    hr = E_FAIL;
                }
                gp_free(rows);
            } else {
                hr = E_OUTOFMEMORY;
            }
        } else {
            hr = pal->lpVtbl->InitializePredefined(pal, type, transparent);
        }
        if (SUCCEEDED(hr)) {
            WICColor colors[256];
            UINT count_got = 0, got = 0;
            pal->lpVtbl->GetColorCount(pal, &count_got);
            if (count_got > 256)
                count_got = 256;
            if (SUCCEEDED(pal->lpVtbl->GetColors(pal, count_got, colors, &got))) {
                for (UINT i = 0; i < got; i++)
                    out[i] = colors[i];
                n = got;
            }
        }
        pal->lpVtbl->Release(pal);
    }
    factory->lpVtbl->Release(factory);
    return n;
}

UINT gp_palette_halftone(PaletteType type, ARGB *entries, BOOL transparent)
{
    return wic_palette_entries((WICBitmapPaletteType)type, transparent, nullptr, 0, entries);
}

void gp_palette_halftone256(ARGB *entries)
{
    UINT n = wic_palette_entries(WICBitmapPaletteTypeFixedHalftone256, FALSE, nullptr, 0, entries);
    if (n < 256) {
        /* without WIC a gray ramp stands in. */
        for (UINT i = n; i < 256; i++)
            entries[i] = 0xff000000u | i << 16 | i << 8 | i;
    }
}

UINT gp_palette_optimal(GBitmap *b, UINT count, BOOL transparent, ARGB *entries)
{
    return wic_palette_entries(WICBitmapPaletteTypeMedianCut, transparent, b, count, entries);
}
