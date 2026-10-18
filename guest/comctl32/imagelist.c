/*
 * image lists: same sized images in one vertical strip, image i starting
 * at row i * cy of a 32 bpp dib section, with an optional 1 bpp mask strip
 * where a set bit is a transparent pixel.
 *
 * every list stores 32 bpp pixels whatever depth it was created with.
 * images with alpha are kept premultiplied and drawn with GdiAlphaBlend;
 * the others keep alpha 0 and are drawn through their mask.
 */
#include "comctl32_private.h"

#define IMAGELIST_MAGIC 0x4C4D4948
#define ILC_VALID 0x0007A9FF
#define ILC_INVALID 0xFFF85600
#define ITEM_ALPHA 0x01
#define ITEM_RESIZED 0x02

typedef struct _IMAGELIST {
    DWORD Magic;
    struct _IMAGELIST *Next;
    LONG Refs;
    UINT Flags;
    int Cx, Cy;
    int Count, Capacity, Grow;
    HDC Dc, MaskDc;
    HBITMAP Image, Mask;
    HGDIOBJ OldImage, OldMask;
    BYTE *Bits, *MaskBits;
    int MaskStride;
    COLORREF Bk;
    int Overlay[15];
    RECT OverlayBox[15];
    BOOL OverlayOpaque[15];
    BYTE *Items;
} IMAGELIST;

static SRWLOCK g_lists_lock = SRWLOCK_INIT;
static IMAGELIST *g_lists;

static IMAGELIST *get_list(HIMAGELIST himl)
{
    if (!himl || ((ULONG_PTR)himl & 3))
        return nullptr;
    AcquireSRWLockShared(&g_lists_lock);
    IMAGELIST *il = g_lists;
    while (il && il != himl)
        il = il->Next;
    ReleaseSRWLockShared(&g_lists_lock);
    return il && il->Magic == IMAGELIST_MAGIC ? il : nullptr;
}

static inline DWORD to_pixel(COLORREF c)
{
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

static inline int strip_height(const IMAGELIST *il, int capacity)
{
    return il->Cy * capacity;
}

/* pixel row y of the strip, counted from the top. */
static inline DWORD *image_row(IMAGELIST *il, int y)
{
    return (DWORD *)(il->Bits + (SIZE_T)(strip_height(il, il->Capacity) - 1 - y) * (SIZE_T)il->Cx * 4);
}

static inline BYTE *mask_row(IMAGELIST *il, int y)
{
    return il->MaskBits + (SIZE_T)(strip_height(il, il->Capacity) - 1 - y) * (SIZE_T)il->MaskStride;
}

static inline BOOL mask_get(const BYTE *row, int x)
{
    return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

static inline void mask_set(BYTE *row, int x, BOOL on)
{
    if (on)
        row[x >> 3] |= (BYTE)(0x80 >> (x & 7));
    else
        row[x >> 3] &= (BYTE)~(0x80 >> (x & 7));
}

static HBITMAP create_dib(int width, int height, int bpp, void **bits)
{
    struct {
        BITMAPINFOHEADER h;
        RGBQUAD colors[2];
    } bi = {};
    bi.h.biSize = sizeof(bi.h);
    bi.h.biWidth = width;
    bi.h.biHeight = height;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = (WORD)bpp;
    if (bpp == 1) {
        bi.h.biClrUsed = 2;
        bi.colors[1].rgbRed = bi.colors[1].rgbGreen = bi.colors[1].rgbBlue = 255;
    }
    *bits = nullptr;
    if (width <= 0 || height <= 0)
        return nullptr;
    return CreateDIBSection(nullptr, (BITMAPINFO *)&bi, DIB_RGB_COLORS, bits, nullptr, 0);
}

/* moves the strip to a new capacity, keeping the images that still fit. */
static BOOL set_capacity(IMAGELIST *il, int capacity)
{
    void *bits = nullptr, *mask_bits = nullptr;
    HBITMAP image = nullptr, mask = nullptr;
    int height = strip_height(il, capacity);
    if (il->Cx > 0 && height > 0) {
        image = create_dib(il->Cx, height, 32, &bits);
        if (!image)
            return FALSE;
        if (il->Flags & ILC_MASK) {
            mask = create_dib(il->Cx, height, 1, &mask_bits);
            if (!mask) {
                DeleteObject(image);
                return FALSE;
            }
        }
    }
    BYTE *items = CcAlloc((SIZE_T)capacity);
    if (!items) {
        DeleteObject(image);
        DeleteObject(mask);
        return FALSE;
    }
    int keep = il->Count < capacity ? il->Count : capacity;
    int stride = ((il->Cx + 31) / 32) * 4;
    if (bits && il->Bits)
        for (int y = 0; y < keep * il->Cy; y++)
            memcpy((BYTE *)bits + (SIZE_T)(height - 1 - y) * (SIZE_T)il->Cx * 4, image_row(il, y), (SIZE_T)il->Cx * 4);
    if (mask_bits && il->MaskBits)
        for (int y = 0; y < keep * il->Cy; y++)
            memcpy((BYTE *)mask_bits + (SIZE_T)(height - 1 - y) * (SIZE_T)stride, mask_row(il, y), (SIZE_T)stride);
    if (il->Items)
        memcpy(items, il->Items, (SIZE_T)keep);
    if (il->Image) {
        SelectObject(il->Dc, il->OldImage);
        DeleteObject(il->Image);
    }
    if (il->Mask) {
        SelectObject(il->MaskDc, il->OldMask);
        DeleteObject(il->Mask);
    }
    CcFree(il->Items);
    il->Image = image;
    il->Mask = mask;
    il->Bits = bits;
    il->MaskBits = mask_bits;
    il->MaskStride = stride;
    il->Items = items;
    il->Capacity = capacity;
    if (image)
        il->OldImage = SelectObject(il->Dc, image);
    if (mask)
        il->OldMask = SelectObject(il->MaskDc, mask);
    return TRUE;
}

static int round_up(int n, int step)
{
    return (n + step - 1) / step * step;
}

static BOOL ensure_capacity(IMAGELIST *il, int needed)
{
    if (needed <= il->Capacity)
        return TRUE;
    int more = il->Grow > needed - il->Capacity ? il->Grow : needed - il->Capacity;
    return set_capacity(il, round_up(il->Capacity + more + 1, il->Grow));
}

static int grow_step(int grow)
{
    int g = (grow + 5) & ~3;
    return g < 4 ? 4 : g;
}

static IMAGELIST *create_list(int cx, int cy, UINT flags, int initial, int grow)
{
    if (cx < 0 || cy < 0 || ((flags & ~ILC_MIRROR) & ILC_INVALID))
        return nullptr;
    IMAGELIST *il = CcAlloc(sizeof(*il));
    if (!il)
        return nullptr;
    il->Magic = IMAGELIST_MAGIC;
    il->Refs = 1;
    if (!(flags & 0xFE))
        flags |= ILC_COLOR4;
    il->Flags = flags & ~ILC_MIRROR;
    il->Cx = cx;
    il->Cy = cy;
    il->Grow = grow_step(grow);
    il->Bk = CLR_NONE;
    for (int i = 0; i < 15; i++)
        il->Overlay[i] = -1;
    il->Dc = CreateCompatibleDC(nullptr);
    il->MaskDc = CreateCompatibleDC(nullptr);
    if (initial < 0)
        initial = 0;
    if (!il->Dc || !il->MaskDc ||
        (!set_capacity(il, round_up(initial + 1, il->Grow)) && !set_capacity(il, 2))) {
        if (il->Dc)
            DeleteDC(il->Dc);
        if (il->MaskDc)
            DeleteDC(il->MaskDc);
        CcFree(il);
        return nullptr;
    }
    AcquireSRWLockExclusive(&g_lists_lock);
    il->Next = g_lists;
    g_lists = il;
    ReleaseSRWLockExclusive(&g_lists_lock);
    return il;
}

static void free_list(IMAGELIST *il)
{
    AcquireSRWLockExclusive(&g_lists_lock);
    for (IMAGELIST **link = &g_lists; *link; link = &(*link)->Next)
        if (*link == il) {
            *link = il->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lists_lock);
    il->Magic = 0;
    if (il->Image) {
        SelectObject(il->Dc, il->OldImage);
        DeleteObject(il->Image);
    }
    if (il->Mask) {
        SelectObject(il->MaskDc, il->OldMask);
        DeleteObject(il->Mask);
    }
    DeleteDC(il->Dc);
    DeleteDC(il->MaskDc);
    CcFree(il->Items);
    CcFree(il);
}

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int initial, int grow)
{
    if (cx <= 0 || cy <= 0)
        return nullptr;
    return create_list(cx, cy, flags, initial, grow);
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_Destroy(HIMAGELIST himl)
{
    IMAGELIST *il = get_list(himl);
    if (!il || (il->Flags & 0x100))
        return FALSE;
    if (InterlockedDecrement(&il->Refs) == 0)
        free_list(il);
    return TRUE;
}

/* ordinal 53: also frees lists created shared. */
WINCOMMCTRLAPI BOOL WINAPI ImageList_DestroyShared(HIMAGELIST himl)
{
    IMAGELIST *il = get_list(himl);
    if (!il)
        return FALSE;
    if (InterlockedDecrement(&il->Refs) == 0)
        free_list(il);
    return TRUE;
}

WINCOMMCTRLAPI int WINAPI ImageList_GetImageCount(HIMAGELIST himl)
{
    IMAGELIST *il = get_list(himl);
    return il ? il->Count : 0;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_SetImageCount(HIMAGELIST himl, UINT count)
{
    IMAGELIST *il = get_list(himl);
    if (!il || count > 0x7FFFFFFD)
        return FALSE;
    if (!set_capacity(il, round_up((int)count + 2, il->Grow)))
        return FALSE;
    for (int i = il->Count; i < (int)count; i++)
        il->Items[i] = 0;
    il->Count = (int)count;
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_GetIconSize(HIMAGELIST himl, int *cx, int *cy)
{
    IMAGELIST *il = get_list(himl);
    if (cx)
        *cx = il ? il->Cx : 0;
    if (cy)
        *cy = il ? il->Cy : 0;
    return il != nullptr;
}

static void reset_overlays(IMAGELIST *il)
{
    for (int i = 0; i < 15; i++) {
        il->Overlay[i] = -1;
        SetRectEmpty(&il->OverlayBox[i]);
    }
}

static BOOL remove_all(IMAGELIST *il)
{
    il->Count = 0;
    reset_overlays(il);
    return set_capacity(il, il->Grow);
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_SetIconSize(HIMAGELIST himl, int cx, int cy)
{
    IMAGELIST *il = get_list(himl);
    if (!il || cx < 0 || cy < 0)
        return FALSE;
    /* the same size is S_FALSE, which leaves the images alone. */
    if (cx == il->Cx && cy == il->Cy)
        return FALSE;
    il->Count = 0;
    il->Cx = cx;
    il->Cy = cy;
    if (il->Image) {
        SelectObject(il->Dc, il->OldImage);
        DeleteObject(il->Image);
        il->Image = nullptr;
        il->Bits = nullptr;
    }
    if (il->Mask) {
        SelectObject(il->MaskDc, il->OldMask);
        DeleteObject(il->Mask);
        il->Mask = nullptr;
        il->MaskBits = nullptr;
    }
    return remove_all(il);
}

WINCOMMCTRLAPI DWORD WINAPI ImageList_GetFlags(HIMAGELIST himl)
{
    IMAGELIST *il = get_list(himl);
    return il ? il->Flags & ILC_VALID : 0;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_SetFlags(HIMAGELIST himl, DWORD flags)
{
    IMAGELIST *il = get_list(himl);
    if (!il || (flags & ILC_INVALID))
        return FALSE;
    if (!(flags & 0xFE))
        flags |= ILC_COLOR4;
    if ((flags & ~ILC_MIRROR) == il->Flags)
        return TRUE;
    il->Count = 0;
    if (il->Mask && !(flags & ILC_MASK)) {
        SelectObject(il->MaskDc, il->OldMask);
        DeleteObject(il->Mask);
        il->Mask = nullptr;
        il->MaskBits = nullptr;
    }
    il->Flags = flags & ~ILC_MIRROR;
    return remove_all(il);
}

/* the masked pixels of an image without alpha show the background color. */
static void apply_background(IMAGELIST *il, int index)
{
    if (!il->MaskBits || (il->Items[index] & ITEM_ALPHA))
        return;
    DWORD fill = il->Bk == CLR_NONE ? 0 : to_pixel(il->Bk);
    for (int y = index * il->Cy; y < (index + 1) * il->Cy; y++) {
        DWORD *row = image_row(il, y);
        BYTE *mrow = mask_row(il, y);
        for (int x = 0; x < il->Cx; x++)
            if (mask_get(mrow, x))
                row[x] = fill;
    }
}

/* a bitmap read as 32 bpp pixels, top down rows. */
typedef struct PIXELS {
    DWORD *Data;
    int Width, Height, Bpp;
} PIXELS;

/* blits the bitmap into a dib of ours, which keeps the alpha of 32 bpp sources. */
static BOOL read_bitmap(HBITMAP bitmap, PIXELS *p)
{
    BITMAP bm;
    p->Data = nullptr;
    if (!bitmap || !GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return FALSE;
    p->Width = bm.bmWidth;
    p->Height = bm.bmHeight;
    p->Bpp = bm.bmBitsPixel * bm.bmPlanes;
    BITMAPINFOHEADER h = {};
    h.biSize = sizeof(h);
    h.biWidth = p->Width;
    h.biHeight = -p->Height;
    h.biPlanes = 1;
    h.biBitCount = 32;
    void *bits;
    HBITMAP dib = CreateDIBSection(nullptr, (BITMAPINFO *)&h, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC src = CreateCompatibleDC(nullptr), dst = CreateCompatibleDC(nullptr);
    BOOL ok = FALSE;
    if (dib && src && dst) {
        HGDIOBJ old_src = SelectObject(src, bitmap);
        HGDIOBJ old_dst = SelectObject(dst, dib);
        if (old_src) {
            SetTextColor(dst, RGB(0, 0, 0));
            SetBkColor(dst, RGB(255, 255, 255));
            ok = BitBlt(dst, 0, 0, p->Width, p->Height, src, 0, 0, SRCCOPY);
            SelectObject(src, old_src);
        }
        SelectObject(dst, old_dst);
        if (ok && (p->Data = CcAlloc((SIZE_T)p->Width * (SIZE_T)p->Height * 4)))
            memcpy(p->Data, bits, (SIZE_T)p->Width * (SIZE_T)p->Height * 4);
    }
    if (src)
        DeleteDC(src);
    if (dst)
        DeleteDC(dst);
    if (dib)
        DeleteObject(dib);
    return p->Data != nullptr;
}

static inline DWORD pixel_at(const PIXELS *p, int x, int y)
{
    return p->Data[(SIZE_T)y * (SIZE_T)p->Width + (SIZE_T)x];
}

/*
 * stores one image from column sx of the pixels. mask pixels that are not
 * black are transparent; without a mask the image is opaque.
 */
static void store_image(IMAGELIST *il, int index, const PIXELS *src, int sx, const PIXELS *mask)
{
    if (!il->Bits)
        return;
    BOOL alpha = FALSE;
    if ((il->Flags & 0xFE) == ILC_COLOR32 && src->Bpp == 32)
        for (int y = 0; y < il->Cy && y < src->Height && !alpha; y++)
            for (int x = 0; x < il->Cx && sx + x < src->Width; x++)
                if (pixel_at(src, sx + x, y) >> 24) {
                    alpha = TRUE;
                    break;
                }
    for (int y = 0; y < il->Cy; y++) {
        DWORD *row = image_row(il, index * il->Cy + y);
        BYTE *mrow = il->MaskBits ? mask_row(il, index * il->Cy + y) : nullptr;
        for (int x = 0; x < il->Cx; x++) {
            DWORD v = 0;
            BOOL inside = sx + x < src->Width && y < src->Height;
            if (inside) {
                v = pixel_at(src, sx + x, y);
                if (alpha) {
                    DWORD a = v >> 24;
                    if (!a)
                        v = 0;
                    else if (a < 255)
                        v = (a << 24) | ((((v >> 16) & 0xFF) * a + 128) / 255 << 16) |
                            ((((v >> 8) & 0xFF) * a + 128) / 255 << 8) | (((v & 0xFF) * a + 128) / 255);
                } else {
                    v &= 0xFFFFFF;
                }
            }
            row[x] = v;
            if (mrow) {
                BOOL clear = FALSE;
                if (mask && sx + x < mask->Width && y < mask->Height)
                    clear = (pixel_at(mask, sx + x, y) & 0xFFFFFF) != 0;
                mask_set(mrow, x, clear);
            }
        }
    }
    il->Items[index] = alpha ? ITEM_ALPHA : 0;
    apply_background(il, index);
}

static int add_pixels(IMAGELIST *il, const PIXELS *image, const PIXELS *mask, int count)
{
    if (!ensure_capacity(il, il->Count + count + 1))
        return -1;
    int first = il->Count;
    for (int k = 0; k < count; k++)
        store_image(il, first + k, image, k * il->Cx, mask);
    il->Count += count;
    return first;
}

WINCOMMCTRLAPI int WINAPI ImageList_Add(HIMAGELIST himl, HBITMAP image, HBITMAP mask)
{
    IMAGELIST *il = get_list(himl);
    PIXELS pixels, mask_pixels = {};
    if (!il || !read_bitmap(image, &pixels))
        return -1;
    int count = il->Cx ? pixels.Width / il->Cx : 0;
    if (pixels.Width < il->Cx || !count) {
        CcFree(pixels.Data);
        return -1;
    }
    if (mask)
        read_bitmap(mask, &mask_pixels);
    int index = add_pixels(il, &pixels, mask_pixels.Data ? &mask_pixels : nullptr, count);
    CcFree(pixels.Data);
    CcFree(mask_pixels.Data);
    return index;
}

/* the caller's bitmap gets its masked pixels blackened, as windows does with DSna. */
static void blacken_source(HBITMAP bitmap, PIXELS *pixels)
{
    BITMAPINFOHEADER h = {};
    h.biSize = sizeof(h);
    h.biWidth = pixels->Width;
    h.biHeight = -pixels->Height;
    h.biPlanes = 1;
    h.biBitCount = 32;
    HDC dc = GetDC(nullptr);
    SetDIBits(dc, bitmap, 0, (UINT)pixels->Height, pixels->Data, (BITMAPINFO *)&h, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
}

WINCOMMCTRLAPI int WINAPI ImageList_AddMasked(HIMAGELIST himl, HBITMAP image, COLORREF key)
{
    IMAGELIST *il = get_list(himl);
    PIXELS pixels;
    if (!il || !read_bitmap(image, &pixels))
        return -1;
    if (key == CLR_DEFAULT)
        key = pixel_at(&pixels, 0, 0) & 0xFFFFFF;
    else
        key = to_pixel(key);
    PIXELS mask = pixels;
    mask.Data = CcAlloc((SIZE_T)pixels.Width * (SIZE_T)pixels.Height * 4);
    if (!mask.Data) {
        CcFree(pixels.Data);
        return -1;
    }
    BOOL changed = FALSE;
    for (SIZE_T i = 0; i < (SIZE_T)pixels.Width * (SIZE_T)pixels.Height; i++)
        if ((pixels.Data[i] & 0xFFFFFF) == key) {
            mask.Data[i] = 0xFFFFFF;
            if (pixels.Data[i] & 0xFFFFFF)
                changed = TRUE;
            pixels.Data[i] &= 0xFF000000;
        }
    if (changed)
        blacken_source(image, &pixels);
    /* no width check here, unlike Add: a narrow bitmap adds nothing and returns the count. */
    int count = il->Cx ? pixels.Width / il->Cx : 0;
    int index = count ? add_pixels(il, &pixels, il->MaskBits ? &mask : nullptr, count) : il->Count;
    CcFree(pixels.Data);
    CcFree(mask.Data);
    return index;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_Replace(HIMAGELIST himl, int i, HBITMAP image, HBITMAP mask)
{
    IMAGELIST *il = get_list(himl);
    PIXELS pixels, mask_pixels = {};
    if (!il || i < 0 || i >= il->Count || !read_bitmap(image, &pixels))
        return FALSE;
    if (mask)
        read_bitmap(mask, &mask_pixels);
    store_image(il, i, &pixels, 0, mask_pixels.Data ? &mask_pixels : nullptr);
    CcFree(pixels.Data);
    CcFree(mask_pixels.Data);
    return TRUE;
}

WINCOMMCTRLAPI int WINAPI ImageList_ReplaceIcon(HIMAGELIST himl, int i, HICON icon)
{
    IMAGELIST *il = get_list(himl);
    if (!il || i < -1 || !icon)
        return -1;
    if (i >= il->Count)
        return -1;
    ICONINFO info;
    HICON sized = nullptr;
    if (!GetIconInfo(icon, &info))
        return -1;
    BITMAP bm = {};
    GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm);
    int icon_cy = info.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    if (bm.bmWidth != il->Cx || icon_cy != il->Cy) {
        sized = CopyImage(icon, IMAGE_ICON, il->Cx, il->Cy, LR_COPYFROMRESOURCE);
        if (sized) {
            DeleteObject(info.hbmColor);
            DeleteObject(info.hbmMask);
            if (!GetIconInfo(sized, &info)) {
                DestroyIcon(sized);
                return -1;
            }
        }
    }
    if (i == -1) {
        if (!ensure_capacity(il, il->Count + 2))
            i = -2;
        else
            i = il->Count++;
    }
    if (i >= 0) {
        PIXELS color = {}, mask = {};
        BOOL raw = (il->Flags & 0xFE) == ILC_COLOR32 && info.hbmColor && read_bitmap(info.hbmColor, &color) &&
                   color.Width >= il->Cx && color.Height >= il->Cy;
        if (raw && read_bitmap(info.hbmMask, &mask) && il->Bits) {
            BOOL has_alpha = FALSE;
            if (color.Bpp == 32)
                for (int y = 0; y < il->Cy && !has_alpha; y++)
                    for (int x = 0; x < il->Cx; x++)
                        if (pixel_at(&color, x, y) >> 24) {
                            has_alpha = TRUE;
                            break;
                        }
            if (!has_alpha) {
                /* an icon without alpha takes its alpha from the mask. */
                for (int y = 0; y < il->Cy; y++)
                    for (int x = 0; x < il->Cx; x++) {
                        DWORD *p = &color.Data[(SIZE_T)y * (SIZE_T)color.Width + (SIZE_T)x];
                        *p = (pixel_at(&mask, x, y) & 0xFFFFFF) ? 0 : (*p | 0xFF000000);
                    }
            }
            color.Bpp = 32;
            store_image(il, i, &color, 0, &mask);
        } else if (il->Bits) {
            RECT r = {0, i * il->Cy, il->Cx, (i + 1) * il->Cy};
            CcFillSolid(il->Dc, &r, il->Bk == CLR_NONE ? 0 : il->Bk);
            DrawIconEx(il->Dc, 0, i * il->Cy, sized ? sized : icon, il->Cx, il->Cy, 0, nullptr, DI_NORMAL);
            for (int y = i * il->Cy; y < (i + 1) * il->Cy; y++) {
                DWORD *row = image_row(il, y);
                for (int x = 0; x < il->Cx; x++)
                    row[x] &= 0xFFFFFF;
            }
            if (il->MaskBits) {
                PatBlt(il->MaskDc, 0, i * il->Cy, il->Cx, il->Cy, WHITENESS);
                DrawIconEx(il->MaskDc, 0, i * il->Cy, sized ? sized : icon, il->Cx, il->Cy, 0, nullptr, DI_MASK);
            }
            il->Items[i] = 0;
        }
        CcFree(color.Data);
        CcFree(mask.Data);
    }
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    if (sized)
        DestroyIcon(sized);
    return i >= 0 ? i : -1;
}

static void copy_slot(IMAGELIST *il, int dst, int src)
{
    if (il->Bits)
        for (int y = 0; y < il->Cy; y++)
            memcpy(image_row(il, dst * il->Cy + y), image_row(il, src * il->Cy + y), (SIZE_T)il->Cx * 4);
    if (il->MaskBits)
        for (int y = 0; y < il->Cy; y++)
            memcpy(mask_row(il, dst * il->Cy + y), mask_row(il, src * il->Cy + y), (SIZE_T)il->MaskStride);
    il->Items[dst] = il->Items[src];
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_Remove(HIMAGELIST himl, int i)
{
    IMAGELIST *il = get_list(himl);
    if (!il)
        return FALSE;
    if (i == -1)
        return remove_all(il);
    if (i < 0 || i >= il->Count)
        return FALSE;
    for (int j = i; j < il->Count - 1; j++)
        copy_slot(il, j, j + 1);
    il->Count--;
    if (il->Capacity - il->Count > il->Grow)
        set_capacity(il, il->Capacity - il->Grow);
    return TRUE;
}

WINCOMMCTRLAPI COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST himl, COLORREF color)
{
    IMAGELIST *il = get_list(himl);
    if (!il)
        return color;
    COLORREF old = il->Bk;
    if (old == color)
        return old;
    il->Bk = color;
    for (int i = 0; i < il->Count; i++)
        apply_background(il, i);
    return old;
}

WINCOMMCTRLAPI COLORREF WINAPI ImageList_GetBkColor(HIMAGELIST himl)
{
    IMAGELIST *il = get_list(himl);
    return il ? il->Bk : 0;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_SetOverlayImage(HIMAGELIST himl, int image, int overlay)
{
    IMAGELIST *il = get_list(himl);
    if (!il || !(il->Flags & ILC_MASK) || overlay < 1 || overlay > 15 || image < 0 || image >= il->Count)
        return FALSE;
    int slot = overlay - 1;
    if (il->Overlay[slot] == image)
        return TRUE;
    il->Overlay[slot] = image;
    RECT box = {il->Cx, il->Cy, 0, 0};
    for (int y = 0; y < il->Cy; y++) {
        BYTE *row = mask_row(il, image * il->Cy + y);
        for (int x = 0; x < il->Cx; x++)
            if (!mask_get(row, x)) {
                if (x < box.left)
                    box.left = x;
                if (y < box.top)
                    box.top = y;
                if (x + 1 > box.right)
                    box.right = x + 1;
                if (y + 1 > box.bottom)
                    box.bottom = y + 1;
            }
    }
    if (box.right <= box.left)
        SetRectEmpty(&box);
    BOOL opaque = !IsRectEmpty(&box);
    for (int y = box.top; y < box.bottom && opaque; y++) {
        BYTE *row = mask_row(il, image * il->Cy + y);
        for (int x = box.left; x < box.right; x++)
            if (mask_get(row, x)) {
                opaque = FALSE;
                break;
            }
    }
    il->OverlayBox[slot] = box;
    il->OverlayOpaque[slot] = opaque;
    return TRUE;
}

/* ------------------------------------------------------------------ drawing */

typedef struct DRAW_SOURCE {
    HDC Dc, MaskDc;
    int X, Y, Width, Height;
    BOOL Alpha;
    /* the masked pixels of the image are black. */
    BOOL MaskedBlack;
} DRAW_SOURCE;

static void blit(HDC dst, int x, int y, int cx, int cy, HDC src, int sx, int sy, int sw, int sh, DWORD rop)
{
    if (cx == sw && cy == sh)
        BitBlt(dst, x, y, cx, cy, src, sx, sy, rop);
    else
        StretchBlt(dst, x, y, cx, cy, src, sx, sy, sw, sh, rop);
}

/* blits a mask with its set bits white and clear bits black, whatever the target's colors. */
static void blit_mask(HDC dst, int x, int y, int cx, int cy, const DRAW_SOURCE *s, DWORD rop)
{
    COLORREF text = SetTextColor(dst, RGB(0, 0, 0)), bk = SetBkColor(dst, RGB(255, 255, 255));
    blit(dst, x, y, cx, cy, s->MaskDc, s->X, s->Y, s->Width, s->Height, rop);
    SetTextColor(dst, text);
    SetBkColor(dst, bk);
}

static void render(HDC dst, int x, int y, int cx, int cy, const DRAW_SOURCE *s, UINT style, DWORD rop, COLORREF bk)
{
    BOOL transparent = (style & ILD_TRANSPARENT) != 0;
    if (style & ILD_MASK) {
        DWORD mrop = (style & ILD_ROP) ? rop : transparent ? SRCAND : SRCCOPY;
        if (s->MaskDc)
            blit_mask(dst, x, y, cx, cy, s, mrop);
        else if (mrop == SRCCOPY)
            PatBlt(dst, x, y, cx, cy, BLACKNESS);
        return;
    }
    if (s->Alpha && !(style & ILD_PRESERVEALPHA)) {
        if (!transparent && bk != CLR_NONE) {
            RECT r = {x, y, x + cx, y + cy};
            CcFillSolid(dst, &r, bk);
        }
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        GdiAlphaBlend(dst, x, y, cx, cy, s->Dc, s->X, s->Y, s->Width, s->Height, bf);
        return;
    }
    if ((style & ILD_IMAGE) || !s->MaskDc || !transparent) {
        blit(dst, x, y, cx, cy, s->Dc, s->X, s->Y, s->Width, s->Height, (style & ILD_ROP) ? rop : SRCCOPY);
        return;
    }
    blit_mask(dst, x, y, cx, cy, s, SRCAND);
    blit(dst, x, y, cx, cy, s->Dc, s->X, s->Y, s->Width, s->Height, SRCPAINT);
}

/* a private copy of one image, for drawing it changed. */
typedef struct SCRATCH {
    HDC Dc, MaskDc;
    HBITMAP Image, Mask;
    HGDIOBJ OldImage, OldMask;
    DWORD *Bits;
    BYTE *MaskBits;
    int Width, Height, MaskStride;
} SCRATCH;

static BOOL scratch_create(SCRATCH *s, IMAGELIST *il, int index, int sx, int sy, int w, int h)
{
    memset(s, 0, sizeof(*s));
    s->Width = w;
    s->Height = h;
    s->MaskStride = ((w + 31) / 32) * 4;
    void *bits, *mask_bits;
    s->Image = create_dib(w, h, 32, &bits);
    s->Mask = create_dib(w, h, 1, &mask_bits);
    s->Dc = CreateCompatibleDC(nullptr);
    s->MaskDc = CreateCompatibleDC(nullptr);
    if (!s->Image || !s->Mask || !s->Dc || !s->MaskDc)
        return FALSE;
    s->Bits = bits;
    s->MaskBits = mask_bits;
    s->OldImage = SelectObject(s->Dc, s->Image);
    s->OldMask = SelectObject(s->MaskDc, s->Mask);
    for (int y = 0; y < h; y++) {
        int iy = sy + y;
        DWORD *row = s->Bits + (SIZE_T)(h - 1 - y) * (SIZE_T)w;
        BYTE *mrow = s->MaskBits + (SIZE_T)(h - 1 - y) * (SIZE_T)s->MaskStride;
        for (int x = 0; x < w; x++) {
            int ix = sx + x;
            BOOL in = ix >= 0 && ix < il->Cx && iy >= 0 && iy < il->Cy;
            row[x] = in ? image_row(il, index * il->Cy + iy)[ix] : 0;
            mask_set(mrow, x, in && il->MaskBits ? mask_get(mask_row(il, index * il->Cy + iy), ix) : !in);
        }
    }
    return TRUE;
}

static void scratch_free(SCRATCH *s)
{
    if (s->Dc) {
        if (s->Image)
            SelectObject(s->Dc, s->OldImage);
        DeleteDC(s->Dc);
    }
    if (s->MaskDc) {
        if (s->Mask)
            SelectObject(s->MaskDc, s->OldMask);
        DeleteDC(s->MaskDc);
    }
    if (s->Image)
        DeleteObject(s->Image);
    if (s->Mask)
        DeleteObject(s->Mask);
}

static inline BOOL scratch_masked(const SCRATCH *s, int x, int y)
{
    return mask_get(s->MaskBits + (SIZE_T)(s->Height - 1 - y) * (SIZE_T)s->MaskStride, x);
}

static inline DWORD *scratch_pixel(SCRATCH *s, int x, int y)
{
    return s->Bits + (SIZE_T)(s->Height - 1 - y) * (SIZE_T)s->Width + (SIZE_T)x;
}

/* turns an image drawn through its mask into one with alpha. */
static void scratch_to_alpha(SCRATCH *s, BOOL has_mask)
{
    for (int y = 0; y < s->Height; y++)
        for (int x = 0; x < s->Width; x++) {
            DWORD *p = scratch_pixel(s, x, y);
            *p = has_mask && scratch_masked(s, x, y) ? 0 : (*p | 0xFF000000);
        }
}

static BOOL draw_image(IMAGELIST *il, const IMAGELISTDRAWPARAMS *p, BOOL full, const RECT *box)
{
    int i = p->i;
    if (i < 0 || i >= il->Count || !p->hdcDst)
        return FALSE;
    UINT style = p->fStyle;
    int cx = p->cx ? p->cx : il->Cx, cy = p->cy ? p->cy : il->Cy;
    int sx = p->xBitmap, sy = p->yBitmap, sw = cx, sh = cy;
    BOOL scale = (style & (ILD_SCALE | ILD_DPISCALE)) != 0;
    if (scale) {
        sx = box ? box->left : 0;
        sy = box ? box->top : 0;
        sw = box ? box->right - box->left : il->Cx;
        sh = box ? box->bottom - box->top : il->Cy;
    }
    COLORREF bk = p->rgbBk;
    if (bk == CLR_DEFAULT)
        bk = il->Bk;
    if (bk == CLR_NONE)
        style |= ILD_TRANSPARENT;
    BOOL alpha = (il->Items[i] & ITEM_ALPHA) != 0;
    BOOL blend = (style & (ILD_BLEND25 | ILD_BLEND50)) != 0 && !(style & ILD_MASK);
    DWORD state = full ? p->fState : 0;
    BOOL effects = (state & (ILS_ALPHA | ILS_SATURATE)) != 0 && !(style & ILD_MASK);
    COLORREF stored_bg = il->Bk == CLR_NONE ? RGB(0, 0, 0) : il->Bk;
    BOOL needs_scratch = blend || effects;
    if (!alpha && il->MaskBits && !(style & (ILD_MASK | ILD_IMAGE))) {
        if (style & ILD_TRANSPARENT)
            needs_scratch |= il->Bk != CLR_NONE && il->Bk != RGB(0, 0, 0);
        else
            needs_scratch |= bk != stored_bg;
    }
    if (!needs_scratch) {
        DRAW_SOURCE src = {il->Dc, il->MaskBits ? il->MaskDc : nullptr, sx, i * il->Cy + sy, sw, sh, alpha, TRUE};
        render(p->hdcDst, p->x, p->y, cx, cy, &src, style, p->dwRop, bk);
    } else {
        SCRATCH s;
        if (!scratch_create(&s, il, i, sx, sy, sw, sh)) {
            scratch_free(&s);
            return FALSE;
        }
        BOOL has_mask = il->MaskBits != nullptr;
        BOOL salpha = alpha;
        if (blend || effects) {
            if (!salpha) {
                scratch_to_alpha(&s, has_mask);
                salpha = TRUE;
            }
            int d = (style & ILD_BLEND50) || p->rgbFg == CLR_NONE ? 2 : 4;
            for (int y = 0; y < s.Height; y++)
                for (int x = 0; x < s.Width; x++) {
                    DWORD *px = scratch_pixel(&s, x, y), v = *px, a = v >> 24;
                    DWORD r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                    if (blend && p->rgbFg == CLR_DEFAULT) {
                        if (a > 128) {
                            DWORD hl = to_pixel(GetSysColor(COLOR_HIGHLIGHT));
                            DWORD part = 256 / (DWORD)d;
                            r = (r * (256 - part) + ((hl >> 16) & 0xFF) * part) >> 8;
                            g = (g * (256 - part) + ((hl >> 8) & 0xFF) * part) >> 8;
                            b = (b * (256 - part) + (hl & 0xFF) * part) >> 8;
                        }
                    } else if (blend) {
                        a /= (DWORD)d;
                        r /= (DWORD)d;
                        g /= (DWORD)d;
                        b /= (DWORD)d;
                    }
                    if (state & ILS_SATURATE) {
                        DWORD grey = (r * 77 + g * 151 + b * 28) >> 8;
                        r = g = b = grey;
                    }
                    if (state & ILS_ALPHA) {
                        DWORD f = p->Frame > 255 ? 255 : p->Frame;
                        a = a * f / 255;
                        r = r * f / 255;
                        g = g * f / 255;
                        b = b * f / 255;
                    }
                    *px = (a << 24) | (r << 16) | (g << 8) | b;
                }
        } else {
            DWORD fill = (style & ILD_TRANSPARENT) ? 0 : to_pixel(bk);
            for (int y = 0; y < s.Height; y++)
                for (int x = 0; x < s.Width; x++)
                    if (scratch_masked(&s, x, y))
                        *scratch_pixel(&s, x, y) = fill;
        }
        DRAW_SOURCE src = {s.Dc, has_mask ? s.MaskDc : nullptr, 0, 0, sw, sh, salpha, TRUE};
        render(p->hdcDst, p->x, p->y, cx, cy, &src, style, p->dwRop, bk);
        scratch_free(&s);
    }
    int overlay = (int)((p->fStyle & ILD_OVERLAYMASK) >> 8);
    if (overlay >= 1 && overlay <= 15 && il->Overlay[overlay - 1] >= 0 && il->Overlay[overlay - 1] < il->Count) {
        RECT box = il->OverlayBox[overlay - 1];
        if (!IsRectEmpty(&box)) {
            IMAGELISTDRAWPARAMS o = *p;
            o.i = il->Overlay[overlay - 1];
            o.fStyle = (p->fStyle & (ILD_MASK | ILD_SCALE | ILD_DPISCALE | ILD_PRESERVEALPHA)) | ILD_TRANSPARENT;
            if (il->OverlayOpaque[overlay - 1])
                o.fStyle |= ILD_IMAGE;
            o.xBitmap = box.left;
            o.yBitmap = box.top;
            o.cx = box.right - box.left;
            o.cy = box.bottom - box.top;
            o.x = p->x + box.left;
            o.y = p->y + box.top;
            if (scale) {
                o.x = p->x + MulDiv(box.left, cx, il->Cx);
                o.y = p->y + MulDiv(box.top, cy, il->Cy);
                o.cx = MulDiv(o.cx, cx, il->Cx);
                o.cy = MulDiv(o.cy, cy, il->Cy);
            }
            draw_image(il, &o, FALSE, &box);
        }
    }
    return !(il->Items[i] & ITEM_RESIZED);
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_DrawIndirect(IMAGELISTDRAWPARAMS *params)
{
    if (!params)
        return FALSE;
    if (params->cbSize != sizeof(IMAGELISTDRAWPARAMS) && params->cbSize != IMAGELISTDRAWPARAMS_V3_SIZE)
        return FALSE;
    IMAGELIST *il = get_list(params->himl);
    if (!il)
        return FALSE;
    return draw_image(il, params, params->cbSize == sizeof(IMAGELISTDRAWPARAMS), nullptr);
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_DrawEx(HIMAGELIST himl, int i, HDC dc, int x, int y, int dx, int dy,
                                            COLORREF bk, COLORREF fg, UINT style)
{
    IMAGELISTDRAWPARAMS p = {};
    p.cbSize = IMAGELISTDRAWPARAMS_V3_SIZE;
    p.himl = himl;
    p.i = i;
    p.hdcDst = dc;
    p.x = x;
    p.y = y;
    p.cx = dx;
    p.cy = dy;
    p.rgbBk = bk;
    p.rgbFg = fg;
    p.fStyle = style;
    p.dwRop = SRCCOPY;
    return ImageList_DrawIndirect(&p);
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_Draw(HIMAGELIST himl, int i, HDC dc, int x, int y, UINT style)
{
    return ImageList_DrawEx(himl, i, dc, x, y, 0, 0, CLR_DEFAULT, CLR_DEFAULT, style);
}

WINCOMMCTRLAPI HICON WINAPI ImageList_GetIcon(HIMAGELIST himl, int i, UINT flags)
{
    IMAGELIST *il = get_list(himl);
    if (!il || i < 0 || i >= il->Count || !il->Bits)
        return nullptr;
    void *bits, *mask_bits;
    HBITMAP color = create_dib(il->Cx, il->Cy, 32, &bits);
    HBITMAP mask = create_dib(il->Cx, il->Cy, 1, &mask_bits);
    HDC dc = CreateCompatibleDC(nullptr);
    HICON icon = nullptr;
    if (color && mask && dc) {
        BOOL alpha = (il->Items[i] & ITEM_ALPHA) != 0;
        HGDIOBJ old = SelectObject(dc, mask);
        if (il->MaskBits) {
            PatBlt(dc, 0, 0, il->Cx, il->Cy, WHITENESS);
            ImageList_Draw(himl, i, dc, 0, 0, (flags & ~ILD_TRANSPARENT) | ILD_MASK);
        } else {
            PatBlt(dc, 0, 0, il->Cx, il->Cy, alpha ? WHITENESS : BLACKNESS);
        }
        SelectObject(dc, color);
        if (alpha && !(flags & (ILD_BLEND25 | ILD_BLEND50 | ILD_OVERLAYMASK))) {
            /* the premultiplied pixels go back to straight alpha. */
            DWORD *out = bits;
            for (int y = 0; y < il->Cy; y++) {
                DWORD *row = image_row(il, i * il->Cy + y);
                for (int x = 0; x < il->Cx; x++) {
                    DWORD v = row[x], a = v >> 24;
                    if (a && a < 255)
                        v = (a << 24) | ((((v >> 16) & 0xFF) * 255 / a) << 16) | ((((v >> 8) & 0xFF) * 255 / a) << 8) |
                            ((v & 0xFF) * 255 / a);
                    out[(SIZE_T)(il->Cy - 1 - y) * (SIZE_T)il->Cx + (SIZE_T)x] = v;
                }
            }
        } else {
            PatBlt(dc, 0, 0, il->Cx, il->Cy, BLACKNESS);
            ImageList_Draw(himl, i, dc, 0, 0, flags | ILD_TRANSPARENT);
            DWORD *out = bits;
            for (SIZE_T k = 0; k < (SIZE_T)il->Cx * (SIZE_T)il->Cy; k++)
                out[k] &= 0xFFFFFF;
        }
        SelectObject(dc, old);
        ICONINFO info = {TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&info);
    }
    if (dc)
        DeleteDC(dc);
    if (color)
        DeleteObject(color);
    if (mask)
        DeleteObject(mask);
    return icon;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST himl, int i, IMAGEINFO *info)
{
    IMAGELIST *il = get_list(himl);
    if (!il || !info || i < 0 || i >= il->Count)
        return FALSE;
    info->hbmImage = il->Image;
    info->hbmMask = il->Mask;
    info->Unused1 = 0;
    info->Unused2 = 0;
    SetRect(&info->rcImage, 0, i * il->Cy, il->Cx, (i + 1) * il->Cy);
    return TRUE;
}

/* ordinal 70. */
WINCOMMCTRLAPI BOOL WINAPI ImageList_GetImageRect(HIMAGELIST himl, int i, RECT *rect)
{
    IMAGELIST *il = get_list(himl);
    if (!il || !rect || i < 0 || i >= il->Count)
        return FALSE;
    SetRect(rect, 0, i * il->Cy, il->Cx, (i + 1) * il->Cy);
    return TRUE;
}

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST himl)
{
    IMAGELIST *il = get_list(himl);
    if (!il)
        return nullptr;
    /* the grow step goes through the rounding again, as windows does. */
    IMAGELIST *copy = create_list(il->Cx, il->Cy, il->Flags, il->Capacity - 1, il->Grow);
    if (!copy)
        return nullptr;
    if (copy->Capacity < il->Capacity && !set_capacity(copy, il->Capacity)) {
        free_list(copy);
        return nullptr;
    }
    if (il->Bits && copy->Bits)
        for (int y = 0; y < il->Count * il->Cy; y++)
            memcpy(image_row(copy, y), image_row(il, y), (SIZE_T)il->Cx * 4);
    if (il->MaskBits && copy->MaskBits)
        for (int y = 0; y < il->Count * il->Cy; y++)
            memcpy(mask_row(copy, y), mask_row(il, y), (SIZE_T)il->MaskStride);
    memcpy(copy->Items, il->Items, (SIZE_T)il->Count);
    copy->Count = il->Count;
    copy->Bk = il->Bk;
    memcpy(copy->Overlay, il->Overlay, sizeof(il->Overlay));
    memcpy(copy->OverlayBox, il->OverlayBox, sizeof(il->OverlayBox));
    memcpy(copy->OverlayOpaque, il->OverlayOpaque, sizeof(il->OverlayOpaque));
    return copy;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_Copy(HIMAGELIST dst, int i_dst, HIMAGELIST src, int i_src, UINT flags)
{
    IMAGELIST *il = get_list(dst);
    if (!il || dst != src || (flags & ~ILCF_SWAP) || i_dst < 0 || i_dst >= il->Count || i_src < 0 ||
        i_src >= il->Count)
        return FALSE;
    if (flags & ILCF_SWAP) {
        /* the spare slot past the last image holds one side of the swap. */
        if (!ensure_capacity(il, il->Count + 1))
            return FALSE;
        copy_slot(il, il->Count, i_dst);
        copy_slot(il, i_dst, i_src);
        copy_slot(il, i_src, il->Count);
    } else {
        copy_slot(il, i_dst, i_src);
    }
    return TRUE;
}

/* draws image i of a list over a merged image: colors where it is opaque, the masks anded. */
static void merge_into(IMAGELIST *dst, IMAGELIST *src, int i, int ox, int oy)
{
    if (i < 0 || i >= src->Count || !src->Bits || !dst->Bits)
        return;
    BOOL alpha = (src->Items[i] & ITEM_ALPHA) != 0;
    for (int y = 0; y < src->Cy; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= dst->Cy)
            continue;
        DWORD *srow = image_row(src, i * src->Cy + y), *drow = image_row(dst, dy);
        BYTE *smask = src->MaskBits ? mask_row(src, i * src->Cy + y) : nullptr;
        BYTE *dmask = dst->MaskBits ? mask_row(dst, dy) : nullptr;
        for (int x = 0; x < src->Cx; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= dst->Cx)
                continue;
            BOOL clear = smask && mask_get(smask, x);
            DWORD v = srow[x];
            if (alpha) {
                DWORD a = v >> 24, d = drow[dx];
                DWORD inv = 255 - a;
                drow[dx] = ((((d >> 16) & 0xFF) * inv / 255 + ((v >> 16) & 0xFF)) << 16) |
                           ((((d >> 8) & 0xFF) * inv / 255 + ((v >> 8) & 0xFF)) << 8) |
                           ((d & 0xFF) * inv / 255 + (v & 0xFF));
            } else if (!clear) {
                drow[dx] = v & 0xFFFFFF;
            }
            if (dmask && !clear)
                mask_set(dmask, dx, FALSE);
        }
    }
}

static IMAGELIST *merge(IMAGELIST *a, int ia, IMAGELIST *b, int ib, int dx, int dy, POINT *origin)
{
    RECT ra = {0, 0, a->Cx, a->Cy}, rb = {dx, dy, dx + b->Cx, dy + b->Cy}, u;
    UnionRect(&u, &ra, &rb);
    UINT depth_a = a->Flags & 0xFE, depth_b = b->Flags & 0xFE;
    UINT depth = depth_a == ILC_COLORDDB ? depth_b : depth_b == ILC_COLORDDB ? depth_a : (depth_a > depth_b ? depth_a : depth_b);
    UINT flags = ((a->Flags | b->Flags) & 0xFFFFFF01 & ~0xFE) | depth | ILC_MASK;
    IMAGELIST *m = create_list(u.right - u.left, u.bottom - u.top, flags & ~ILC_INVALID, 1, 0);
    if (!m)
        return nullptr;
    m->Count = 1;
    if (m->MaskBits)
        for (int y = 0; y < m->Cy; y++)
            memset(mask_row(m, y), 0xFF, (SIZE_T)m->MaskStride);
    merge_into(m, a, ia, -u.left, -u.top);
    merge_into(m, b, ib, dx - u.left, dy - u.top);
    if (origin) {
        origin->x = u.left;
        origin->y = u.top;
    }
    return m;
}

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Merge(HIMAGELIST himl1, int i1, HIMAGELIST himl2, int i2, int dx, int dy)
{
    IMAGELIST *a = get_list(himl1), *b = get_list(himl2);
    if (!a || !b)
        return nullptr;
    return merge(a, i1, b, i2, dx, dy, nullptr);
}

/* ordinal 84: stretches every image to a new size. */
WINCOMMCTRLAPI BOOL WINAPI ImageList_Resize(HIMAGELIST himl, int cx, int cy)
{
    IMAGELIST *il = get_list(himl);
    if (!il || cx <= 0 || cy <= 0 || (cx == il->Cx && cy == il->Cy))
        return FALSE;
    IMAGELIST *n = create_list(cx, cy, il->Flags, il->Capacity - 1, 0);
    if (!n)
        return FALSE;
    n->Grow = il->Grow;
    if (n->Capacity < il->Capacity)
        set_capacity(n, il->Capacity);
    SetStretchBltMode(n->Dc, COLORONCOLOR);
    SetStretchBltMode(n->MaskDc, COLORONCOLOR);
    for (int i = 0; i < il->Count; i++) {
        StretchBlt(n->Dc, 0, i * cy, cx, cy, il->Dc, 0, i * il->Cy, il->Cx, il->Cy, SRCCOPY);
        if (il->Mask && n->Mask)
            StretchBlt(n->MaskDc, 0, i * cy, cx, cy, il->MaskDc, 0, i * il->Cy, il->Cx, il->Cy, SRCCOPY);
        n->Items[i] = il->Items[i] | ITEM_RESIZED;
    }
    for (int k = 0; k < 15; k++) {
        RECT *b = &il->OverlayBox[k];
        SetRect(b, MulDiv(b->left, cx, il->Cx), MulDiv(b->top, cy, il->Cy), MulDiv(b->right, cx, il->Cx),
                MulDiv(b->bottom, cy, il->Cy));
    }
    /* the new strip moves into the old list, whose handle stays valid. */
    IMAGELIST tmp = *il;
    il->Cx = cx;
    il->Cy = cy;
    il->Image = n->Image;
    il->Mask = n->Mask;
    il->Bits = n->Bits;
    il->MaskBits = n->MaskBits;
    il->MaskStride = n->MaskStride;
    il->Items = n->Items;
    il->Capacity = n->Capacity;
    HDC dc = il->Dc, mdc = il->MaskDc;
    il->Dc = n->Dc;
    il->MaskDc = n->MaskDc;
    il->OldImage = n->OldImage;
    il->OldMask = n->OldMask;
    n->Image = tmp.Image;
    n->Mask = tmp.Mask;
    n->Items = tmp.Items;
    n->Dc = dc;
    n->MaskDc = mdc;
    n->OldImage = tmp.OldImage;
    n->OldMask = tmp.OldMask;
    free_list(n);
    return TRUE;
}

static HIMAGELIST load_image(HINSTANCE inst, LPCWSTR name, int cx, int grow, COLORREF key, UINT type, UINT flags)
{
    HBITMAP bitmap = LoadImageW(inst, name, type, 0, 0, flags);
    if (!bitmap)
        return nullptr;
    DIBSECTION ds;
    int size = GetObjectW(bitmap, sizeof(ds), &ds);
    if (size < (int)sizeof(BITMAP) || ds.dsBm.bmHeight <= 0) {
        DeleteObject(bitmap);
        return nullptr;
    }
    if (!cx)
        cx = ds.dsBm.bmHeight;
    UINT ilc = key != CLR_NONE ? ILC_MASK : 0;
    if (size == (int)sizeof(DIBSECTION))
        ilc |= ds.dsBm.bmBitsPixel & 0xFE;
    HIMAGELIST himl = ImageList_Create(cx, ds.dsBm.bmHeight, ilc, ds.dsBm.bmWidth / cx, grow);
    if (himl) {
        int r = key == CLR_NONE ? ImageList_Add(himl, bitmap, nullptr) : ImageList_AddMasked(himl, bitmap, key);
        if (r < 0) {
            ImageList_Destroy(himl);
            himl = nullptr;
        }
    }
    DeleteObject(bitmap);
    return himl;
}

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_LoadImageW(HINSTANCE inst, LPCWSTR name, int cx, int grow, COLORREF key,
                                                      UINT type, UINT flags)
{
    return load_image(inst, name, cx, grow, key, type, flags);
}

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_LoadImageA(HINSTANCE inst, LPCSTR name, int cx, int grow, COLORREF key,
                                                      UINT type, UINT flags)
{
    if (IS_INTRESOURCE(name))
        return load_image(inst, (LPCWSTR)name, cx, grow, key, type, flags);
    LPWSTR w = CcStrDupAtoW(name);
    if (!w)
        return nullptr;
    HIMAGELIST himl = load_image(inst, w, cx, grow, key, type, flags);
    CcFree(w);
    return himl;
}

/* ordinal 87 has nothing to filter in this implementation. */
WINCOMMCTRLAPI BOOL WINAPI ImageList_SetFilter(HIMAGELIST himl, int i, DWORD filter)
{
    return FALSE;
}

/* ordinal 390. */
WINCOMMCTRLAPI int WINAPI ImageList_SetColorTable(HIMAGELIST himl, int start, int count, RGBQUAD *colors)
{
    return get_list(himl) ? 0 : -1;
}

/* the stream format is not implemented; these fail cleanly. */
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Read(struct IStream *stream)
{
    return nullptr;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_Write(HIMAGELIST himl, struct IStream *stream)
{
    return FALSE;
}

WINCOMMCTRLAPI HRESULT WINAPI ImageList_ReadEx(DWORD flags, struct IStream *stream, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOTIMPL;
}

WINCOMMCTRLAPI HRESULT WINAPI ImageList_WriteEx(HIMAGELIST himl, DWORD flags, struct IStream *stream)
{
    return get_list(himl) ? E_NOTIMPL : E_INVALIDARG;
}

WINCOMMCTRLAPI HRESULT WINAPI HIMAGELIST_QueryInterface(HIMAGELIST himl, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!get_list(himl))
        return E_POINTER;
    return E_NOINTERFACE;
}

WINCOMMCTRLAPI HRESULT WINAPI ImageList_CoCreateInstance(REFCLSID clsid, const IUnknown *outer, REFIID riid,
                                                         void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ dragging */

static struct {
    IMAGELIST *List;
    POINT Hot;
    HWND Window;
    BOOL Entered, Shown;
    POINT Pos;
    HDC SaveDc;
    HBITMAP Save;
    HGDIOBJ OldSave;
    RECT Saved;
} g_drag;

static void drag_hide(void)
{
    if (!g_drag.Shown)
        return;
    HDC dc = GetDCEx(g_drag.Window, nullptr, DCX_WINDOW | DCX_CACHE);
    if (dc) {
        BitBlt(dc, g_drag.Saved.left, g_drag.Saved.top, g_drag.Saved.right - g_drag.Saved.left,
               g_drag.Saved.bottom - g_drag.Saved.top, g_drag.SaveDc, 0, 0, SRCCOPY);
        ReleaseDC(g_drag.Window, dc);
    }
    g_drag.Shown = FALSE;
}

static void drag_show(void)
{
    if (g_drag.Shown || !g_drag.List)
        return;
    HDC dc = GetDCEx(g_drag.Window, nullptr, DCX_WINDOW | DCX_CACHE);
    if (!dc)
        return;
    int w = g_drag.List->Cx, h = g_drag.List->Cy;
    if (!g_drag.SaveDc || g_drag.Saved.right - g_drag.Saved.left != w || g_drag.Saved.bottom - g_drag.Saved.top != h) {
        if (g_drag.SaveDc) {
            SelectObject(g_drag.SaveDc, g_drag.OldSave);
            DeleteObject(g_drag.Save);
            DeleteDC(g_drag.SaveDc);
        }
        g_drag.SaveDc = CreateCompatibleDC(dc);
        g_drag.Save = CreateCompatibleBitmap(dc, w, h);
        g_drag.OldSave = SelectObject(g_drag.SaveDc, g_drag.Save);
    }
    SetRect(&g_drag.Saved, g_drag.Pos.x - g_drag.Hot.x, g_drag.Pos.y - g_drag.Hot.y, g_drag.Pos.x - g_drag.Hot.x + w,
            g_drag.Pos.y - g_drag.Hot.y + h);
    BitBlt(g_drag.SaveDc, 0, 0, w, h, dc, g_drag.Saved.left, g_drag.Saved.top, SRCCOPY);
    ImageList_Draw(g_drag.List, 0, dc, g_drag.Saved.left, g_drag.Saved.top, ILD_TRANSPARENT);
    ReleaseDC(g_drag.Window, dc);
    g_drag.Shown = TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_BeginDrag(HIMAGELIST track, int i, int hot_x, int hot_y)
{
    IMAGELIST *il = get_list(track);
    if (!il || g_drag.List || i < 0 || i >= il->Count)
        return FALSE;
    IMAGELIST *copy = create_list(il->Cx, il->Cy, (il->Flags & ~0xFE) | ILC_COLOR32 | ILC_MASK, 1, 0);
    if (!copy)
        return FALSE;
    copy->Count = 1;
    if (copy->Bits)
        for (int y = 0; y < il->Cy; y++)
            memcpy(image_row(copy, y), image_row(il, i * il->Cy + y), (SIZE_T)il->Cx * 4);
    if (copy->MaskBits)
        for (int y = 0; y < il->Cy; y++) {
            if (il->MaskBits)
                memcpy(mask_row(copy, y), mask_row(il, i * il->Cy + y), (SIZE_T)il->MaskStride);
            else
                memset(mask_row(copy, y), 0, (SIZE_T)copy->MaskStride);
        }
    copy->Items[0] = il->Items[i] & ITEM_ALPHA;
    g_drag.List = copy;
    g_drag.Hot.x = hot_x;
    g_drag.Hot.y = hot_y;
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_DragEnter(HWND lock, int x, int y)
{
    if (!g_drag.List || g_drag.Entered)
        return FALSE;
    g_drag.Window = lock ? lock : GetDesktopWindow();
    g_drag.Entered = TRUE;
    g_drag.Pos.x = x;
    g_drag.Pos.y = y;
    drag_show();
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_DragMove(int x, int y)
{
    if (!g_drag.Entered || !g_drag.Shown)
        return FALSE;
    if (x == g_drag.Pos.x && y == g_drag.Pos.y)
        return TRUE;
    drag_hide();
    g_drag.Pos.x = x;
    g_drag.Pos.y = y;
    drag_show();
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_DragShowNolock(BOOL show)
{
    if (!g_drag.Entered)
        return FALSE;
    if (show)
        drag_show();
    else
        drag_hide();
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_DragLeave(HWND lock)
{
    if (!g_drag.Entered || (lock ? lock : GetDesktopWindow()) != g_drag.Window)
        return FALSE;
    drag_hide();
    g_drag.Entered = FALSE;
    return TRUE;
}

WINCOMMCTRLAPI void WINAPI ImageList_EndDrag(void)
{
    drag_hide();
    g_drag.Entered = FALSE;
    if (g_drag.List)
        free_list(g_drag.List);
    g_drag.List = nullptr;
    if (g_drag.SaveDc) {
        SelectObject(g_drag.SaveDc, g_drag.OldSave);
        DeleteObject(g_drag.Save);
        DeleteDC(g_drag.SaveDc);
        g_drag.SaveDc = nullptr;
    }
}

WINCOMMCTRLAPI BOOL WINAPI ImageList_SetDragCursorImage(HIMAGELIST drag, int i, int hot_x, int hot_y)
{
    IMAGELIST *il = get_list(drag);
    if (!il || !g_drag.List)
        return FALSE;
    POINT origin;
    IMAGELIST *m = merge(g_drag.List, 0, il, i, hot_x, hot_y, &origin);
    if (!m)
        return FALSE;
    BOOL shown = g_drag.Shown;
    drag_hide();
    free_list(g_drag.List);
    g_drag.List = m;
    g_drag.Hot.x -= origin.x;
    g_drag.Hot.y -= origin.y;
    if (shown)
        drag_show();
    return TRUE;
}

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_GetDragImage(POINT *pt, POINT *hot)
{
    if (!g_drag.List)
        return nullptr;
    if (pt)
        *pt = g_drag.Pos;
    if (hot)
        *hot = g_drag.Hot;
    return g_drag.List;
}
