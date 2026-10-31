/*
 * pictures for thumbnails: the sized bitmap that holds a picture, and fitting a picture in it. a picture that has to
 * be scaled down with the sharpening filter, and the one that is dithered to the 256 colors of a palette, need
 * what is not here and are refused.
 */
#include "shlwapi_private.h"

void WINAPI CalculateAspectRatio(const SIZE *target, BOOL center, RECT *rect);

/* the header of the old bitmaps, whose size counts 12 bytes. */
typedef struct {
    DWORD size;
    WORD width, height, planes, bit_count;
} CORE_HEADER;

static DWORD bitmap_size(const BITMAPINFO *info)
{
    const BITMAPINFOHEADER *header = &info->bmiHeader;
    if (header->biSize == sizeof(CORE_HEADER)) {
        const CORE_HEADER *core = (const CORE_HEADER *)header;
        return core->height * (((core->width * core->planes * core->bit_count + 31) & ~31) / 8);
    }
    if (header->biCompression == BI_RGB || header->biCompression == BI_BITFIELDS || header->biCompression == 10) {
        LONG height = header->biHeight < 0 ? -header->biHeight : header->biHeight;
        return height * (((header->biWidth * header->biPlanes * header->biBitCount + 31) & ~31) / 8);
    }
    return header->biSizeImage;
}

/* the picture holder: a section of the size and depth, with the colors of the palette or of a sample header. */
BOOL WINAPI CreateSizedDIBSECTION(const SIZE *size, WORD bits_per_pixel, HPALETTE palette,
                                  const BITMAPINFOHEADER *sample, HBITMAP *bitmap, BITMAPINFO **info, void **bits);
BOOL WINAPI CreateSizedDIBSECTION(const SIZE *size, WORD bits_per_pixel, HPALETTE palette,
                                  const BITMAPINFOHEADER *sample, HBITMAP *bitmap, BITMAPINFO **info, void **bits)
{
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[256];
    } description = {};
    void *section_bits = nullptr;
    *bitmap = nullptr;
    HDC screen = GetDC(nullptr);
    if (!screen)
        return FALSE;
    HDC dc = CreateCompatibleDC(screen);
    if (dc) {
        BOOL usable = TRUE;
        description.header.biSize = sizeof(BITMAPINFOHEADER);
        description.header.biWidth = size->cx;
        description.header.biHeight = size->cy;
        description.header.biPlanes = 1;
        description.header.biBitCount = bits_per_pixel;
        description.header.biClrUsed = bits_per_pixel > 8 ? 0 : 1u << bits_per_pixel;
        description.header.biSizeImage = bitmap_size((BITMAPINFO *)&description);
        if (bits_per_pixel <= 8) {
            if (!sample || sample->biBitCount != bits_per_pixel) {
                PALETTEENTRY entries[256];
                SelectPalette(dc, palette, TRUE);
                RealizePalette(dc);
                GetPaletteEntries(palette, 0, 256, entries);
                for (DWORD i = 0; i < description.header.biClrUsed; i++) {
                    description.colors[i].rgbRed = entries[i].peRed;
                    description.colors[i].rgbGreen = entries[i].peGreen;
                    description.colors[i].rgbBlue = entries[i].peBlue;
                }
            } else {
                DWORD count = sample->biClrUsed ? sample->biClrUsed : description.header.biClrUsed;
                if (count > 256)
                    usable = FALSE;
                else
                    memcpy(description.colors, (const BYTE *)sample + sizeof(BITMAPINFOHEADER), count * sizeof(RGBQUAD));
            }
        }
        if (usable) {
            *bitmap = CreateDIBSection(dc, (BITMAPINFO *)&description, DIB_RGB_COLORS, &section_bits, nullptr, 0);
            if (*bitmap) {
                if (info) {
                    *info = LocalAlloc(LPTR, sizeof(description));
                    if (*info)
                        memcpy(*info, &description, sizeof(description));
                }
                if (bits)
                    *bits = section_bits;
            }
        }
        DeleteDC(dc);
    }
    ReleaseDC(nullptr, screen);
    return *bitmap != nullptr;
}

/* draws the picture in a rectangle of a new bitmap that is filled with the color around it. */
BOOL WINAPI FactorAspectRatio(BITMAPINFO *info, const BYTE *bits, const SIZE *size, RECT fit, int depth,
                              HPALETTE palette, int reserved, COLORREF background, HBITMAP *bitmap);
BOOL WINAPI FactorAspectRatio(BITMAPINFO *info, const BYTE *bits, const SIZE *size, RECT fit, int depth,
                              HPALETTE palette, int reserved, COLORREF background, HBITMAP *bitmap)
{
    int result = -1;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return FALSE;
    /* an 8 bit result of a picture of another depth is a dither to the palette, which is not done here. */
    if (depth == 8) {
        switch (info->bmiHeader.biBitCount) {
        case 8:
        case 16:
        case 24:
        case 32:
            DeleteDC(dc);
            return FALSE;
        }
    }
    if (CreateSizedDIBSECTION(size, (WORD)depth, palette, nullptr, bitmap, nullptr, nullptr)) {
        HGDIOBJ old_bitmap = SelectObject(dc, *bitmap);
        SetStretchBltMode(dc, COLORONCOLOR);
        HBRUSH brush = CreateSolidBrush(background);
        HGDIOBJ old_brush = SelectObject(dc, brush);
        HGDIOBJ old_pen = SelectObject(dc, GetStockObject(WHITE_PEN));
        HPALETTE old_palette = nullptr;
        if (palette) {
            old_palette = SelectPalette(dc, palette, TRUE);
            RealizePalette(dc);
        }
        SetMapMode(dc, MM_TEXT);
        Rectangle(dc, 0, 0, size->cx, size->cy);
        int top = fit.top, height = fit.bottom - fit.top, source_top = 0;
        if (info->bmiHeader.biHeight < 0) {
            top = fit.bottom;
            height = -height;
            source_top = -info->bmiHeader.biHeight;
        }
        result = StretchDIBits(dc, fit.left, top, fit.right - fit.left, height, 0, source_top, info->bmiHeader.biWidth,
                               info->bmiHeader.biHeight, bits, info, DIB_RGB_COLORS, SRCCOPY);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
        SelectObject(dc, old_pen);
        if (palette) {
            SelectPalette(dc, old_palette, TRUE);
            RealizePalette(dc);
        }
        SelectObject(dc, old_bitmap);
    }
    DeleteDC(dc);
    return result != -1;
}

/* a picture that already fits is placed in the bitmap of the size; any other would be scaled and sharpened. */
BOOL WINAPI ConvertDIBSECTIONToThumbnail(BITMAPINFO *info, const BYTE *bits, HBITMAP *bitmap, const SIZE *target,
                                         UINT depth, HPALETTE palette, UINT sharpen, BOOL keep_format);
BOOL WINAPI ConvertDIBSECTIONToThumbnail(BITMAPINFO *info, const BYTE *bits, HBITMAP *bitmap, const SIZE *target,
                                         UINT depth, HPALETTE palette, UINT sharpen, BOOL keep_format)
{
    BOOL flipped = FALSE, result = FALSE;
    if (info->bmiHeader.biHeight < 0) {
        info->bmiHeader.biHeight = -info->bmiHeader.biHeight;
        flipped = TRUE;
    }
    RECT fit = {0, 0, info->bmiHeader.biWidth, info->bmiHeader.biHeight};
    CalculateAspectRatio(target, TRUE, &fit);
    if (fit.right - fit.left != info->bmiHeader.biWidth || fit.bottom - fit.top != info->bmiHeader.biHeight) {
        if (flipped)
            info->bmiHeader.biHeight = -info->bmiHeader.biHeight;
        return FALSE;
    }
    if (flipped)
        info->bmiHeader.biHeight = -info->bmiHeader.biHeight;
    if (keep_format && info->bmiHeader.biBitCount <= depth) {
        SIZE size = {info->bmiHeader.biWidth, info->bmiHeader.biHeight};
        BITMAPINFO *copy = nullptr;
        void *section_bits = nullptr;
        result = CreateSizedDIBSECTION(&size, info->bmiHeader.biBitCount, nullptr, &info->bmiHeader, bitmap, &copy,
                                       &section_bits);
        if (result) {
            DWORD bytes = bitmap_size(info);
            DIBSECTION section;
            if (copy && GetObjectW(*bitmap, sizeof(section), &section) && section.dsBmih.biSizeImage >= bytes) {
                memcpy(section_bits, bits, bytes);
            } else {
                result = FALSE;
                DeleteObject(*bitmap);
            }
        }
        LocalFree(copy);
    } else {
        result = FactorAspectRatio(info, bits, target, fit, depth, palette, keep_format, GetSysColor(COLOR_WINDOW),
                                   bitmap);
    }
    return result;
}

/* the table that maps colors to the entries of the shell palette is not kept. */
HRESULT WINAPI SHGetInverseCMAP(BYTE *map, ULONG size);
HRESULT WINAPI SHGetInverseCMAP(BYTE *map, ULONG size)
{
    if (!map)
        return E_POINTER;
    if (size != 0x8000 && size != sizeof(void *))
        return E_INVALIDARG;
    return E_NOTIMPL;
}
