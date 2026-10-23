/*
 * icons for pictures: picks the image of an icon or cursor file that best fits the display and requested size, makes
 * the icon, and gets the two bitmaps it is drawn with.
 */
#include "picture.h"

__declspec(dllimport) HICON WINAPI CreateIconFromResourceEx(PBYTE bits, DWORD size, BOOL icon, DWORD version, int cx,
                                                            int cy, UINT flags);

#define DIR_HEADER 6
#define DIR_ENTRY 16
#define RESOURCE_VERSION 0x30000
#define IMAGE_INFO_HEADER 40
#define IMAGE_PNG_SIGNATURE 0x474E5089
#define FILE_KIND_ICON 1
#define FILE_KIND_CURSOR 2
#define COLORS_UNKNOWN_MAX 255
#define COLOR_BITS_HUGE 31

static UINT get_u16(const BYTE *p)
{
    return p[0] | p[1] << 8;
}

static ULONG get_u32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

/* the requested color count, else what the display can show. */
static ULONG color_limit(DWORD flags)
{
    if (flags & (LP_MONOCHROME | LP_VGACOLOR | LP_COLOR)) {
        if (flags & LP_MONOCHROME)
            return 2;
        return flags & LP_VGACOLOR ? 16 : COLORS_UNKNOWN_MAX;
    }
    HDC screen = GetDC(nullptr);
    int bits = screen ? GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES) : 0;
    if (screen)
        ReleaseDC(nullptr, screen);
    return bits >= COLOR_BITS_HUGE ? 0x7FFFFFFF : 1u << bits;
}

/* the directory entry closest to what is wanted. */
static UINT choose_entry(const BYTE *entries, UINT count, ULONG limit, int want_cx, int want_cy)
{
    UINT best = 0;
    ULONG best_under = 0, exact_under = 0, best_over = 0;
    for (UINT i = 0; i < count; i++) {
        const BYTE *e = entries + (SIZE_T)i * DIR_ENTRY;
        ULONG colors = e[2] ? e[2] : 255;
        if (!best_over && !exact_under && colors <= limit && colors > best_under) {
            best = i;
            best_under = colors;
        }
        if (e[0] != want_cx || e[1] != want_cy)
            continue;
        if (colors == limit)
            return i;
        if (colors < limit && colors > exact_under) {
            best = i;
            exact_under = colors;
        }
        if (colors > limit && !exact_under && (!best_over || colors < best_over)) {
            best = i;
            best_over = colors;
        }
    }
    return best;
}

/* both bitmaps are copies the picture deletes. */
HRESULT PicIconPlanes(HICON icon, HBITMAP *mask, HBITMAP *colors)
{
    ICONINFO info = {};
    *mask = *colors = nullptr;
    BOOL has_info = GetIconInfo(icon, &info);
    if (has_info && info.hbmMask && info.hbmColor) {
        *mask = info.hbmMask;
        *colors = info.hbmColor;
        return S_OK;
    }
    int width = GetSystemMetrics(SM_CXICON), height = GetSystemMetrics(SM_CYICON);
    BITMAP shape = {};
    if (has_info && info.hbmMask && GetObjectW(info.hbmMask, sizeof(shape), &shape) && shape.bmWidth && shape.bmHeight >= 2) {
        width = shape.bmWidth;
        height = shape.bmHeight / 2;
    }
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    if (info.hbmColor)
        DeleteObject(info.hbmColor);

    HDC screen = GetDC(nullptr);
    HDC color_dc = CreateCompatibleDC(screen), mask_dc = CreateCompatibleDC(screen);
    HBITMAP color_bitmap = screen ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    HBITMAP mask_bitmap = CreateBitmap(width, height, 1, 1, nullptr);
    BOOL ok = color_dc && mask_dc && color_bitmap && mask_bitmap;
    if (ok) {
        HGDIOBJ color_old = SelectObject(color_dc, color_bitmap), mask_old = SelectObject(mask_dc, mask_bitmap);
        ok = PatBlt(color_dc, 0, 0, width, height, BLACKNESS) && DrawIcon(color_dc, 0, 0, icon) &&
             PatBlt(mask_dc, 0, 0, width, height, WHITENESS) && DrawIcon(mask_dc, 0, 0, icon) &&
             BitBlt(mask_dc, 0, 0, width, height, color_dc, 0, 0, SRCINVERT);
        SelectObject(color_dc, color_old);
        SelectObject(mask_dc, mask_old);
    }
    if (color_dc)
        DeleteDC(color_dc);
    if (mask_dc)
        DeleteDC(mask_dc);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (!ok) {
        if (color_bitmap)
            DeleteObject(color_bitmap);
        if (mask_bitmap)
            DeleteObject(mask_bitmap);
        return E_FAIL;
    }
    *mask = mask_bitmap;
    *colors = color_bitmap;
    return S_OK;
}

HRESULT PicFromIconFile(const BYTE *data, ULONG size, DWORD cx, DWORD cy, DWORD flags, PIC_IMAGE *image)
{
    if (size < DIR_HEADER)
        return E_UNEXPECTED;
    UINT kind = get_u16(data + 2), count = get_u16(data + 4);
    if (!count || (kind != FILE_KIND_ICON && kind != FILE_KIND_CURSOR))
        return CTL_E_INVALIDPICTURE;
    int want_cx = (int)cx, want_cy = (int)cy;
    if (!cx && !cy) {
        want_cx = GetSystemMetrics(SM_CXICON);
        want_cy = GetSystemMetrics(SM_CYICON);
    }
    if ((ULONGLONG)DIR_HEADER + (ULONGLONG)count * DIR_ENTRY > size)
        return E_UNEXPECTED;

    const BYTE *entries = data + DIR_HEADER;
    UINT chosen = choose_entry(entries, count, color_limit(flags), want_cx, want_cy);
    const BYTE *entry = entries + (SIZE_T)chosen * DIR_ENTRY;
    ULONG length = get_u32(entry + 8), offset = get_u32(entry + 12);
    if ((ULONGLONG)offset + length > size || length < 4)
        return CTL_E_INVALIDPICTURE;
    /* only a BITMAPINFOHEADER image or a png makes an icon. */
    ULONG head = get_u32(data + offset);
    if (head != IMAGE_INFO_HEADER && head != IMAGE_PNG_SIGNATURE)
        return CTL_E_INVALIDPICTURE;

    /* a cursor resource starts with its hot spot, which the file keeps in the directory. */
    BYTE *resource = AutAlloc((SIZE_T)length + 4);
    if (!resource)
        return E_OUTOFMEMORY;
    ULONG resource_size = length;
    if (kind == FILE_KIND_CURSOR) {
        memcpy(resource, entry + 4, 4);
        memcpy(resource + 4, data + offset, length);
        resource_size += 4;
    } else {
        memcpy(resource, data + offset, length);
    }
    HICON icon = CreateIconFromResourceEx(resource, resource_size, kind == FILE_KIND_ICON, RESOURCE_VERSION, (int)cx,
                                          (int)cy, 0);
    AutFree(resource);
    if (!icon)
        return CTL_E_INVALIDPICTURE;

    HBITMAP mask, colors;
    if (FAILED(PicIconPlanes(icon, &mask, &colors))) {
        DestroyIcon(icon);
        return E_FAIL;
    }
    image->Kind = PICTYPE_ICON;
    image->Icon = icon;
    image->IconAnd = mask;
    image->IconXor = colors;
    return S_OK;
}
