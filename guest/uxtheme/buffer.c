/*
 * buffered painting: an off screen dc over the target rectangle, in the
 * target's logical coordinates, copied or blended back by EndBufferedPaint.
 * animations never start; programs then paint the final frame directly.
 */
#include "uxtheme_private.h"

#define BUFFER_MAGIC 0x46425855 /* "UXBF" */

typedef struct UX_BUFFER {
    DWORD Magic;
    struct UX_BUFFER *Next;
    HDC Target, Dc;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    RGBQUAD *Bits;
    BOOL TopDown;
    RECT Rect;
    BP_BUFFERFORMAT Format;
    DWORD Flags;
    BOOL HasExclude, HasBlend;
    RECT Exclude;
    BLENDFUNCTION Blend;
} UX_BUFFER;

static struct {
    RTL_RUN_ONCE Once;
    RTL_CRITICAL_SECTION Lock;
    UX_BUFFER *List;
    LONG Inits;
} g_buffers;

static ULONG NTAPI init_buffers(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&g_buffers.Lock);
    return TRUE;
}

static void lock_buffers(void)
{
    RtlRunOnceExecuteOnce(&g_buffers.Once, init_buffers, nullptr, nullptr);
    RtlEnterCriticalSection(&g_buffers.Lock);
}

static UX_BUFFER *find_buffer(HPAINTBUFFER handle, BOOL unlink)
{
    lock_buffers();
    UX_BUFFER **link = &g_buffers.List, *b = nullptr;
    while (*link && *link != (UX_BUFFER *)handle)
        link = &(*link)->Next;
    if (*link) {
        b = *link;
        if (unlink)
            *link = b->Next;
    }
    RtlLeaveCriticalSection(&g_buffers.Lock);
    return b;
}

THEMEAPI BufferedPaintInit(void)
{
    InterlockedIncrement(&g_buffers.Inits);
    return S_OK;
}

THEMEAPI BufferedPaintUnInit(void)
{
    InterlockedDecrement(&g_buffers.Inits);
    return S_OK;
}

THEMEAPI_(HPAINTBUFFER) BeginBufferedPaint(HDC target, const RECT *rect, BP_BUFFERFORMAT format,
                                           BP_PAINTPARAMS *params, HDC *dc)
{
    if (dc)
        *dc = nullptr;
    if (!target || !rect || !dc || format > BPBF_TOPDOWNMONODIB ||
        (params && params->cbSize != sizeof(BP_PAINTPARAMS))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    int w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (w <= 0 || h <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    UX_BUFFER *b = UxAlloc(sizeof(*b));
    if (!b) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    b->Magic = BUFFER_MAGIC;
    b->Target = target;
    b->Rect = *rect;
    b->Format = format;
    b->Flags = params ? params->dwFlags : 0;
    if (params && params->prcExclude) {
        b->HasExclude = TRUE;
        b->Exclude = *params->prcExclude;
    }
    if (params && params->pBlendFunction) {
        b->HasBlend = TRUE;
        b->Blend = *params->pBlendFunction;
    }
    b->Dc = CreateCompatibleDC(target);
    if (format == BPBF_COMPATIBLEBITMAP) {
        b->Bitmap = CreateCompatibleBitmap(target, w, h);
    } else {
        struct {
            BITMAPINFOHEADER Header;
            RGBQUAD Colors[2];
        } bi;
        memset(&bi, 0, sizeof(bi));
        bi.Header.biSize = sizeof(BITMAPINFOHEADER);
        bi.Header.biWidth = w;
        b->TopDown = format != BPBF_DIB;
        bi.Header.biHeight = b->TopDown ? -h : h;
        bi.Header.biPlanes = 1;
        bi.Header.biBitCount = format == BPBF_TOPDOWNMONODIB ? 1 : 32;
        bi.Colors[1].rgbRed = bi.Colors[1].rgbGreen = bi.Colors[1].rgbBlue = 255;
        void *bits = nullptr;
        b->Bitmap = CreateDIBSection(target, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        b->Bits = format == BPBF_TOPDOWNMONODIB ? nullptr : bits;
    }
    if (!b->Dc || !b->Bitmap) {
        if (b->Dc)
            DeleteDC(b->Dc);
        if (b->Bitmap)
            DeleteObject(b->Bitmap);
        UxFree(b);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    b->OldBitmap = SelectObject(b->Dc, b->Bitmap);
    SetWindowOrgEx(b->Dc, rect->left, rect->top, nullptr);
    /* start with the target's drawing attributes. */
    SelectObject(b->Dc, GetCurrentObject(target, OBJ_FONT));
    SelectObject(b->Dc, GetCurrentObject(target, OBJ_BRUSH));
    SelectObject(b->Dc, GetCurrentObject(target, OBJ_PEN));
    SetTextColor(b->Dc, GetTextColor(target));
    SetBkColor(b->Dc, GetBkColor(target));
    SetBkMode(b->Dc, GetBkMode(target));
    if (b->Flags & BPPF_ERASE)
        PatBlt(b->Dc, rect->left, rect->top, w, h, BLACKNESS);
    lock_buffers();
    b->Next = g_buffers.List;
    g_buffers.List = b;
    RtlLeaveCriticalSection(&g_buffers.Lock);
    *dc = b->Dc;
    return (HPAINTBUFFER)b;
}

THEMEAPI EndBufferedPaint(HPAINTBUFFER handle, BOOL update_target)
{
    UX_BUFFER *b = find_buffer(handle, TRUE);
    if (!b)
        return E_INVALIDARG;
    int w = b->Rect.right - b->Rect.left, h = b->Rect.bottom - b->Rect.top;
    if (update_target) {
        int saved = SaveDC(b->Target);
        if (b->HasExclude)
            ExcludeClipRect(b->Target, b->Exclude.left, b->Exclude.top, b->Exclude.right, b->Exclude.bottom);
        SetWindowOrgEx(b->Dc, 0, 0, nullptr);
        if (b->HasBlend)
            GdiAlphaBlend(b->Target, b->Rect.left, b->Rect.top, w, h, b->Dc, 0, 0, w, h, b->Blend);
        else
            BitBlt(b->Target, b->Rect.left, b->Rect.top, w, h, b->Dc, 0, 0, SRCCOPY);
        RestoreDC(b->Target, saved);
    }
    SelectObject(b->Dc, b->OldBitmap);
    DeleteDC(b->Dc);
    DeleteObject(b->Bitmap);
    b->Magic = 0;
    UxFree(b);
    return S_OK;
}

THEMEAPI GetBufferedPaintTargetRect(HPAINTBUFFER handle, RECT *rect)
{
    UX_BUFFER *b = find_buffer(handle, FALSE);
    if (!b || !rect)
        return E_INVALIDARG;
    *rect = b->Rect;
    return S_OK;
}

THEMEAPI_(HDC) GetBufferedPaintTargetDC(HPAINTBUFFER handle)
{
    UX_BUFFER *b = find_buffer(handle, FALSE);
    return b ? b->Target : nullptr;
}

THEMEAPI_(HDC) GetBufferedPaintDC(HPAINTBUFFER handle)
{
    UX_BUFFER *b = find_buffer(handle, FALSE);
    return b ? b->Dc : nullptr;
}

THEMEAPI GetBufferedPaintBits(HPAINTBUFFER handle, RGBQUAD **bits, int *row_pixels)
{
    UX_BUFFER *b = find_buffer(handle, FALSE);
    if (!b || !bits || !row_pixels)
        return E_INVALIDARG;
    if (!b->Bits)
        return E_FAIL;
    GdiFlush();
    *bits = b->Bits;
    *row_pixels = b->Rect.right - b->Rect.left;
    return S_OK;
}

/* rect is in target coordinates. */
static HRESULT for_pixels(HPAINTBUFFER handle, const RECT *rect, BOOL clear, BYTE alpha)
{
    UX_BUFFER *b = find_buffer(handle, FALSE);
    if (!b)
        return E_INVALIDARG;
    RECT rc = b->Rect;
    if (rect && !IntersectRect(&rc, rect, &b->Rect))
        return S_OK;
    if (!b->Bits) {
        if (!clear)
            return E_FAIL;
        PatBlt(b->Dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, BLACKNESS);
        return S_OK;
    }
    GdiFlush();
    int w = b->Rect.right - b->Rect.left, h = b->Rect.bottom - b->Rect.top;
    for (int y = rc.top - b->Rect.top; y < rc.bottom - b->Rect.top; y++) {
        RGBQUAD *row = b->Bits + (SIZE_T)(b->TopDown ? y : h - 1 - y) * w;
        for (int x = rc.left - b->Rect.left; x < rc.right - b->Rect.left; x++) {
            if (clear)
                memset(&row[x], 0, sizeof(RGBQUAD));
            else
                row[x].rgbReserved = alpha;
        }
    }
    return S_OK;
}

THEMEAPI BufferedPaintClear(HPAINTBUFFER handle, const RECT *rect)
{
    return for_pixels(handle, rect, TRUE, 0);
}

THEMEAPI BufferedPaintSetAlpha(HPAINTBUFFER handle, const RECT *rect, BYTE alpha)
{
    return for_pixels(handle, rect, FALSE, alpha);
}

/* no animation ever runs: the buffer paints the final state, and *from is NULL as when windows skips one. */
THEMEAPI_(HANIMATIONBUFFER) BeginBufferedAnimation(HWND hwnd, HDC target, const RECT *rect,
                                                   BP_BUFFERFORMAT format, BP_PAINTPARAMS *paint_params,
                                                   BP_ANIMATIONPARAMS *animation_params, HDC *from, HDC *to)
{
    if (from)
        *from = nullptr;
    if (to)
        *to = nullptr;
    if (!from || !to) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return (HANIMATIONBUFFER)BeginBufferedPaint(target, rect, format, paint_params, to);
}

THEMEAPI EndBufferedAnimation(HANIMATIONBUFFER buffer, BOOL update_target)
{
    return EndBufferedPaint((HPAINTBUFFER)buffer, update_target);
}

THEMEAPI_(BOOL) BufferedPaintRenderAnimation(HWND hwnd, HDC target)
{
    return FALSE;
}

THEMEAPI BufferedPaintStopAllAnimations(HWND hwnd)
{
    return S_OK;
}
