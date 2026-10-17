/*
 * the primary surface is the screen. what programs write to it reaches
 * their windows the way a display's scanout would show it: a thread
 * presents the parts written since the last refresh, a flip presents at
 * once. pixels go through gdi, which converts every format, 8 bit ones
 * with the primary's palette, into the window's surface; mode x's 320
 * pixel wide primary is stretched over the 640 by 480 mode it runs in.
 * a gamma ramp of the exclusive owner's primary is the display's, which
 * the host applies.
 */
#include "ddraw_private.h"

static HANDLE g_wake, g_thread;

typedef struct DIBINFO {
    BITMAPINFOHEADER header;
    union {
        RGBQUAD colors[256];
        DWORD masks[3];
    };
} DIBINFO;

/* the dib header of a surface's pixels; false for formats gdi cannot read. */
static BOOL describe(DDSURF *s, DIBINFO *info)
{
    memset(&info->header, 0, sizeof(info->header));
    info->header.biSize = sizeof(info->header);
    info->header.biWidth = s->desc.lPitch * 8 / (LONG)(s->format.bpp ? s->format.bpp : 8);
    info->header.biHeight = -(LONG)s->desc.dwHeight;
    info->header.biPlanes = 1;
    info->header.biBitCount = (WORD)s->format.bpp;
    info->header.biCompression = BI_RGB;
    const DDPIXELFORMAT *pf = &s->format.pf;
    switch (s->format.bpp) {
    case 8:
        if (!(pf->dwFlags & DDPF_PALETTEINDEXED8))
            return FALSE;
        for (int i = 0; i < 256; i++) {
            PALETTEENTRY e = s->palette ? s->palette->entries[i] : (PALETTEENTRY){(BYTE)i, (BYTE)i, (BYTE)i, 0};
            info->colors[i] = (RGBQUAD){e.peBlue, e.peGreen, e.peRed, 0};
        }
        info->header.biClrUsed = 256;
        return TRUE;
    case 16:
    case 32:
        if (!(pf->dwFlags & DDPF_RGB))
            return FALSE;
        info->header.biCompression = BI_BITFIELDS;
        info->masks[0] = pf->dwRBitMask;
        info->masks[1] = pf->dwGBitMask;
        info->masks[2] = pf->dwBBitMask;
        return TRUE;
    case 24:
        return (pf->dwFlags & DDPF_RGB) && pf->dwRBitMask == 0xFF0000;
    default:
        return FALSE;
    }
}

/* the window the primary shows in: the clipper's, else the device window, else the focus window. */
static HWND target_window(DDSURF *primary)
{
    DDRAW *dd = primary->dd;
    if (primary->clipper && primary->clipper->window)
        return primary->clipper->window;
    if (dd->device)
        return dd->device;
    return dd->focus;
}

/* draws a rectangle of the primary, in screen coordinates, onto its window. */
static void present_rect(DDSURF *primary, RECT rect)
{
    DDRAW *dd = primary->dd;
    if (!dd || !primary->memory || primary->lost)
        return;
    HWND window = target_window(primary);
    if (!window || !IsWindow(window) || IsIconic(window))
        return;
    RECT bounds = {0, 0, (LONG)primary->desc.dwWidth, (LONG)primary->desc.dwHeight};
    if (!IntersectRect(&rect, &rect, &bounds))
        return;
    DIBINFO info;
    if (!describe(primary, &info))
        return;
    HDC dc = GetDC(window);
    if (!dc)
        return;
    /* mode x: the primary's pixels are twice their size on the screen. */
    LONG scale = (dd->flags & DDF_MODEX) && primary->desc.dwWidth == 320 ? 2 : 1;
    POINT origin = {rect.left * scale, rect.top * scale};
    ScreenToClient(window, &origin);
    /* clipped to the clipper's region when there is one, as the screen would clip the window. */
    if (primary->clipper && !primary->clipper->window) {
        RGNDATA *region = DdClipRegion(primary->clipper, nullptr);
        if (region) {
            HRGN clip = ExtCreateRegion(nullptr, sizeof(RGNDATAHEADER) + region->rdh.nRgnSize, region);
            if (clip) {
                POINT offset = {0, 0};
                ScreenToClient(window, &offset);
                OffsetRgn(clip, offset.x, offset.y);
                SelectClipRgn(dc, clip);
                DeleteObject(clip);
            }
            HeapFree(GetProcessHeap(), 0, region);
        }
    }
    LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
    /* the rows of the rectangle alone, so no source origin convention matters. */
    info.header.biHeight = -height;
    const BYTE *pixels = primary->memory + (SIZE_T)rect.top * primary->desc.lPitch;
    LONG src_x = rect.left;
    StretchDIBits(dc, origin.x, origin.y, width * scale, height * scale, src_x, 0, width, height, pixels,
                  (BITMAPINFO *)&info, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(window, dc);
}

static DWORD WINAPI scanout(void *arg)
{
    (void)arg;
    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        /* what a refresh shows: everything written since the last one. */
        Sleep(8);
        RtlEnterCriticalSection(&DdLock);
        for (LIST_ENTRY *e = DdObjects.Flink; e != &DdObjects; e = e->Flink) {
            DDRAW *dd = CONTAINING_RECORD(e, DDRAW, links);
            DDSURF *p = dd->primary;
            if (!p || IsRectEmpty(&dd->dirty))
                continue;
            RECT rect = dd->dirty;
            SetRectEmpty(&dd->dirty);
            present_rect(p, rect);
        }
        RtlLeaveCriticalSection(&DdLock);
    }
    return 0;
}

/* a rectangle of the primary, in its own coordinates, changed. */
void DdPresentDirty(DDSURF *primary, const RECT *rect)
{
    DDRAW *dd = primary->dd;
    if (!dd)
        return;
    RECT r = rect ? *rect : (RECT){0, 0, (LONG)primary->desc.dwWidth, (LONG)primary->desc.dwHeight};
    UnionRect(&dd->dirty, &dd->dirty, &r);
    if (!g_thread) {
        g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_thread = CreateThread(nullptr, 0, scanout, nullptr, 0, nullptr);
    }
    if (g_wake)
        SetEvent(g_wake);
}

/* a flip shows the new front buffer whole, now. */
void DdPresentFlip(DDSURF *primary)
{
    DDRAW *dd = primary->dd;
    if (!dd)
        return;
    SetRectEmpty(&dd->dirty);
    present_rect(primary, (RECT){0, 0, (LONG)primary->desc.dwWidth, (LONG)primary->desc.dwHeight});
}

void DdPresentStop(DDRAW *dd)
{
    SetRectEmpty(&dd->dirty);
}
