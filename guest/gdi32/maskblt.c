/*
 * MaskBlt and PlgBlt, made of the copies win32k does: a mask picks per pixel between the foreground and the
 * background raster operation, and a parallelogram is drawn by taking each of its pixels from the source rectangle.
 */
#include "gdi32_private.h"
#include <mime/win32/wingdi.h>

#define ROP_DSNA 0x00220326 /* the destination and not the source. */

static BOOL is_monochrome(HBITMAP bitmap)
{
    BITMAP info;
    return GetObjectW(bitmap, sizeof(info), &info) && info.bmPlanes == 1 && info.bmBitsPixel == 1;
}

/* a raster operation with a foreground in bits 16 to 23 and a background in bits 24 to 31, as a plain one. */
static DWORD fore_rop(DWORD rop4)
{
    return rop4 & 0x00FF0000u;
}

static DWORD back_rop(DWORD rop4)
{
    return (rop4 >> 8) & 0x00FF0000u;
}

/* a copy of the destination to draw on, with the brush the destination draws patterns with. */
static HDC scratch(HDC like, int width, int height, HBITMAP *bitmap, HBITMAP *old, HDC dst, int x, int y)
{
    HDC dc = CreateCompatibleDC(like);
    if (!dc)
        return nullptr;
    *bitmap = CreateCompatibleBitmap(like, width, height);
    if (!*bitmap) {
        DeleteDC(dc);
        return nullptr;
    }
    *old = SelectObject(dc, *bitmap);
    SelectObject(dc, GetCurrentObject(dst, OBJ_BRUSH));
    POINT origin;
    if (GetBrushOrgEx(dst, &origin))
        SetBrushOrgEx(dc, origin.x - x, origin.y - y, nullptr);
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkColor(dc, RGB(255, 255, 255));
    return dc;
}

static void drop(HDC dc, HBITMAP bitmap, HBITMAP old)
{
    if (dc) {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
}

WINGDIAPI BOOL WINAPI MaskBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                              HBITMAP mask, int mask_x, int mask_y, DWORD rop)
{
    if (MfActive(dst)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!mask)
        return BitBlt(dst, x, y, width, height, src, src_x, src_y, fore_rop(rop));
    if (!is_monochrome(mask) || width <= 0 || height <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HBITMAP fore_bitmap = nullptr, back_bitmap = nullptr, fore_old = nullptr, back_old = nullptr;
    HDC fore = scratch(dst, width, height, &fore_bitmap, &fore_old, dst, x, y);
    HDC back = fore ? scratch(dst, width, height, &back_bitmap, &back_old, dst, x, y) : nullptr;
    HDC pattern = CreateCompatibleDC(dst);
    HBITMAP pattern_old = pattern ? SelectObject(pattern, mask) : nullptr;
    BOOL ok = fore && back && pattern;
    /* each starts as the destination, takes its operation, and keeps its pixels of the mask. */
    ok = ok && BitBlt(fore, 0, 0, width, height, dst, x, y, SRCCOPY) && BitBlt(back, 0, 0, width, height, dst, x, y, SRCCOPY);
    ok = ok && BitBlt(fore, 0, 0, width, height, src, src_x, src_y, fore_rop(rop)) &&
         BitBlt(back, 0, 0, width, height, src, src_x, src_y, back_rop(rop));
    ok = ok && BitBlt(fore, 0, 0, width, height, pattern, mask_x, mask_y, SRCAND) &&
         BitBlt(back, 0, 0, width, height, pattern, mask_x, mask_y, ROP_DSNA) &&
         BitBlt(fore, 0, 0, width, height, back, 0, 0, SRCPAINT) && BitBlt(dst, x, y, width, height, fore, 0, 0, SRCCOPY);
    DWORD error = GetLastError();
    if (pattern) {
        SelectObject(pattern, pattern_old);
        DeleteDC(pattern);
    }
    drop(back, back_bitmap, back_old);
    drop(fore, fore_bitmap, fore_old);
    if (!ok)
        SetLastError(error);
    return ok;
}

/* a 32 bit top down bitmap whose pixels can be read and written. */
static HBITMAP make_dib(HDC like, int width, int height, ULONG **bits)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(like, &info, DIB_RGB_COLORS, (void **)bits, nullptr, 0);
}

#define PLG_LIMIT 16384

/*
 * the corners are the destination's upper left, upper right and lower left, in logical units, which are used as the
 * pixels they are in the default mapping mode. a pixel takes the source pixel its center falls on, and keeps its own
 * where the mask is clear or the center is outside the parallelogram.
 */
WINGDIAPI BOOL WINAPI PlgBlt(HDC dst, const POINT *corners, HDC src, int src_x, int src_y, int width, int height,
                             HBITMAP mask, int mask_x, int mask_y)
{
    if (MfActive(dst)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!corners || !src || width <= 0 || height <= 0 || (mask && !is_monochrome(mask))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG ax = corners[1].x - corners[0].x, ay = corners[1].y - corners[0].y;
    LONG bx = corners[2].x - corners[0].x, by = corners[2].y - corners[0].y;
    LONG left = corners[0].x, top = corners[0].y, right = left, bottom = top;
    POINT fourth = {corners[1].x + corners[2].x - corners[0].x, corners[1].y + corners[2].y - corners[0].y};
    const POINT *all[4] = {&corners[0], &corners[1], &corners[2], &fourth};
    for (int i = 1; i < 4; i++) {
        left = all[i]->x < left ? all[i]->x : left;
        right = all[i]->x > right ? all[i]->x : right;
        top = all[i]->y < top ? all[i]->y : top;
        bottom = all[i]->y > bottom ? all[i]->y : bottom;
    }
    LONG box_width = right - left, box_height = bottom - top;
    LONGLONG det = (LONGLONG)ax * by - (LONGLONG)ay * bx;
    if (!det || box_width <= 0 || box_height <= 0)
        return TRUE;
    if (box_width > PLG_LIMIT || box_height > PLG_LIMIT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    HDC from = CreateCompatibleDC(dst), to = CreateCompatibleDC(dst), by_mask = mask ? CreateCompatibleDC(dst) : nullptr;
    ULONG *source_bits = nullptr, *out_bits = nullptr, *mask_bits = nullptr;
    HBITMAP source_dib = from ? make_dib(dst, width, height, &source_bits) : nullptr;
    HBITMAP out_dib = to ? make_dib(dst, (int)box_width, (int)box_height, &out_bits) : nullptr;
    HBITMAP mask_dib = by_mask ? make_dib(dst, width, height, &mask_bits) : nullptr;
    HBITMAP from_old = source_dib ? SelectObject(from, source_dib) : nullptr;
    HBITMAP to_old = out_dib ? SelectObject(to, out_dib) : nullptr;
    HBITMAP mask_old = mask_dib ? SelectObject(by_mask, mask_dib) : nullptr;
    BOOL ok = source_dib && out_dib && (!mask || mask_dib);
    if (ok && mask) {
        HDC pattern = CreateCompatibleDC(dst);
        HBITMAP pattern_old = pattern ? SelectObject(pattern, mask) : nullptr;
        ok = pattern && BitBlt(by_mask, 0, 0, width, height, pattern, mask_x, mask_y, SRCCOPY);
        if (pattern) {
            SelectObject(pattern, pattern_old);
            DeleteDC(pattern);
        }
    }
    ok = ok && BitBlt(from, 0, 0, width, height, src, src_x, src_y, SRCCOPY) &&
         BitBlt(to, 0, 0, box_width, box_height, dst, left, top, SRCCOPY);
    if (ok) {
        GdiFlush();
        for (LONG py = 0; py < box_height; py++) {
            for (LONG px = 0; px < box_width; px++) {
                /* twice the offset of the pixel's center from the first corner. */
                LONGLONG dx = 2LL * (left + px - corners[0].x) + 1, dy = 2LL * (top + py - corners[0].y) + 1;
                LONGLONG s = dx * by - dy * bx, t = (LONGLONG)ax * dy - (LONGLONG)ay * dx, den = 2 * det;
                if (den < 0) {
                    s = -s;
                    t = -t;
                    den = -den;
                }
                if (s < 0 || t < 0 || s >= den || t >= den)
                    continue;
                LONG u = (LONG)(s * width / den), v = (LONG)(t * height / den);
                if (mask_bits && !(mask_bits[v * width + u] & 0xFFFFFF))
                    continue;
                out_bits[py * box_width + px] = source_bits[v * width + u];
            }
        }
        ok = BitBlt(dst, left, top, box_width, box_height, to, 0, 0, SRCCOPY);
    }
    DWORD error = GetLastError();
    for (int i = 0; i < 3; i++) {
        HDC dc = i == 0 ? from : i == 1 ? to : by_mask;
        HBITMAP old = i == 0 ? from_old : i == 1 ? to_old : mask_old;
        HBITMAP dib = i == 0 ? source_dib : i == 1 ? out_dib : mask_dib;
        if (dc) {
            if (old)
                SelectObject(dc, old);
            DeleteDC(dc);
        }
        if (dib)
            DeleteObject(dib);
    }
    if (!ok)
        SetLastError(error);
    return ok;
}
