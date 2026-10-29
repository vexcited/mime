/* bitmaps with their colors mapped to the colors of the system, and the flat scroll bars, which are the plain ones. */
#include "comctl32_private.h"

#define CMB_MASKED 0x02
#define CMB_DIBSECTION 0x04

/* the colors a bitmap of a toolbar is drawn in, and the colors of the system they stand for. */
static const struct {
    COLORREF From;
    int System;
} g_default_map[] = {{RGB(0, 0, 0), COLOR_BTNTEXT},        {RGB(128, 128, 128), COLOR_BTNSHADOW},
                     {RGB(192, 192, 192), COLOR_BTNFACE},  {RGB(255, 255, 255), COLOR_BTNHIGHLIGHT},
                     {RGB(0, 0, 255), COLOR_HIGHLIGHT},    {RGB(255, 0, 255), COLOR_WINDOW}};

/* the entry of a color table for a color of the kind COLORREF is. */
static DWORD table_color(COLORREF color)
{
    return ((DWORD)GetRValue(color) << 16) | ((DWORD)GetGValue(color) << 8) | GetBValue(color);
}

/* the first 16 entries of the table are replaced, and the table of the mask makes the magenta ones white and the others black. */
static void map_colors(BITMAPINFOHEADER *header, const COLORMAP *map, int count, DWORD *mask, int entries, UINT flags)
{
    DWORD *table = (DWORD *)((BYTE *)header + header->biSize);
    if (map) {
        if (count > 16)
            count = 16;
    } else {
        count = RTL_NUMBER_OF(g_default_map);
    }
    DWORD from[16], to[16];
    for (int i = 0; i < count; i++) {
        from[i] = table_color(map ? map[i].from : g_default_map[i].From);
        to[i] = table_color(map ? map[i].to : GetSysColor(g_default_map[i].System));
    }
    if (entries > 16)
        entries = 16;
    if (flags & CMB_MASKED)
        for (int i = 0; i < entries; i++)
            mask[i] = table[i] == 0xFF00FF ? 0xFFFFFF : 0;
    for (int i = 0; i < entries; i++)
        for (int j = 0; j < count; j++)
            if ((table[i] & 0xFFFFFF) == from[j]) {
                table[i] = to[j];
                break;
            }
}

static HBITMAP make_bitmap(BITMAPINFO *info, const BYTE *bits, const DWORD *mask, int entries, UINT flags)
{
    HDC screen = GetDC(nullptr);
    if (!screen)
        return nullptr;
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = nullptr;
    if (!memory) {
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    LONG width = info->bmiHeader.biWidth, height = info->bmiHeader.biHeight;
    if (flags & CMB_DIBSECTION) {
        void *section;
        DWORD compression = info->bmiHeader.biCompression;
        if (compression != BI_BITFIELDS)
            info->bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(screen, info, DIB_RGB_COLORS, &section, nullptr, 0);
        info->bmiHeader.biCompression = compression;
    }
    if (!bitmap)
        bitmap = CreateCompatibleBitmap(screen, (flags & CMB_MASKED) ? 2 * width : width, height);
    if (bitmap) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        StretchDIBits(memory, 0, 0, width, height, 0, 0, width, height, bits, info, DIB_RGB_COLORS, SRCCOPY);
        if (flags & CMB_MASKED) {
            if (entries)
                memmove((BYTE *)info + info->bmiHeader.biSize, mask, 4 * (SIZE_T)entries);
            StretchDIBits(memory, width, 0, width, height, 0, 0, width, height, bits, info, DIB_RGB_COLORS, SRCCOPY);
            BitBlt(memory, 0, 0, width, height, memory, width, 0, 0x220326);
        }
        SelectObject(memory, old);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

/*
 * the bitmap resource, its colors replaced as the map says or, without a map, the colors of a toolbar bitmap by those of
 * the system. a masked bitmap is twice as wide, its right half the mask. only the first 16 colors of a table are mapped.
 */
WINCOMMCTRLAPI HBITMAP WINAPI CreateMappedBitmap(HINSTANCE instance, INT_PTR id, UINT flags, LPCOLORMAP map, int count)
{
    HRSRC resource = FindResourceW(instance, (LPCWSTR)(ULONG_PTR)(WORD)id, MAKEINTRESOURCEW(2));
    if (!resource)
        return nullptr;
    const BYTE *data = LockResource(LoadResource(instance, resource));
    HBITMAP result = nullptr;
    if (data) {
        const BITMAPINFOHEADER *header = (const BITMAPINFOHEADER *)data;
        BOOL palette = header->biBitCount <= 8 || header->biCompression != 0;
        SIZE_T size = header->biSize;
        if (palette)
            size += 4 * ((SIZE_T)1 << header->biBitCount);
        BITMAPINFO *copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (copy) {
            DWORD mask[16] = {};
            memcpy(copy, data, size);
            int entries = palette ? 16 : 0;
            if (palette && ((SIZE_T)1 << data[14]) >= 16)
                map_colors(&copy->bmiHeader, map, count, mask, entries, flags);
            result = make_bitmap(copy, data + size, mask, entries, flags);
            HeapFree(GetProcessHeap(), 0, copy);
        }
    }
    FreeResource(resource);
    return result;
}

/* ------------------------------------------------------------------ flat scroll bars */

/* windows 11 keeps no flat scroll bars: the functions are the scroll functions of user32, and the properties are not kept. */
WINCOMMCTRLAPI BOOL WINAPI InitializeFlatSB(HWND hwnd)
{
    return TRUE;
}

WINCOMMCTRLAPI HRESULT WINAPI UninitializeFlatSB(HWND hwnd)
{
    return S_OK;
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollProp(HWND hwnd, int property, LPINT value)
{
    return FALSE;
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_SetScrollProp(HWND hwnd, UINT property, INT_PTR value, BOOL redraw)
{
    return FALSE;
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollPropPtr(HWND hwnd, int property, INT_PTR *value)
{
    return FALSE;
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_EnableScrollBar(HWND hwnd, int bar, UINT arrows)
{
    return EnableScrollBar(hwnd, bar, arrows);
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_ShowScrollBar(HWND hwnd, int bar, BOOL show)
{
    return ShowScrollBar(hwnd, bar, show);
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollRange(HWND hwnd, int bar, LPINT min, LPINT max)
{
    return GetScrollRange(hwnd, bar, min, max);
}

WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO info)
{
    return GetScrollInfo(hwnd, bar, info);
}

WINCOMMCTRLAPI int WINAPI FlatSB_GetScrollPos(HWND hwnd, int bar)
{
    return GetScrollPos(hwnd, bar);
}

WINCOMMCTRLAPI int WINAPI FlatSB_SetScrollPos(HWND hwnd, int bar, int pos, BOOL redraw)
{
    return SetScrollPos(hwnd, bar, pos, redraw);
}

WINCOMMCTRLAPI int WINAPI FlatSB_SetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO info, BOOL redraw)
{
    return SetScrollInfo(hwnd, bar, info, redraw);
}

WINCOMMCTRLAPI int WINAPI FlatSB_SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL redraw)
{
    return SetScrollRange(hwnd, bar, min, max, redraw);
}
