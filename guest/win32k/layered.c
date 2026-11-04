/*
 * layered windows and window regions. the host composites premultiplied window surfaces with per pixel alpha, so
 * constant alpha, color keys and window shapes are applied by composing the window surface into a second surface
 * that is presented instead. UpdateLayeredWindow writes the caller's premultiplied pixels straight into the window
 * surface.
 */
#include "win32k.h"

static inline ULONG premultiply(ULONG p, ULONG a)
{
    ULONG b = (p & 0xFF) * a, g = ((p >> 8) & 0xFF) * a, r = ((p >> 16) & 0xFF) * a, pa = (p >> 24) * a;
    b = (b + 1 + (b >> 8)) >> 8;
    g = (g + 1 + (g >> 8)) >> 8;
    r = (r + 1 + (r >> 8)) >> 8;
    pa = (pa + 1 + (pa >> 8)) >> 8;
    return (pa << 24) | (r << 16) | (g << 8) | b;
}

static BOOLEAN extended_frame(WND *w)
{
    REGION ext;
    if (!FrameExtension(w, &ext))
        return FALSE;
    RgnFree(&ext);
    return TRUE;
}

static BOOLEAN needs_compose(WND *w)
{
    if (w->Shape)
        return TRUE;
    if ((w->LayerFlags & (LWA_ALPHA | LWA_COLORKEY)) && !(w->LayerFlags & MIME_LAYER_UPDATE))
        return TRUE;
    return !(w->LayerFlags & MIME_LAYER_UPDATE) && extended_frame(w);
}

static void redraw_all(WND *w)
{
    SURFACE *s = w->Surface;
    if (!s)
        return;
    s->Dirty = (RECT){0, 0, s->Width, s->Height};
    if (w->Thread == W32GetThread())
        PaintPresent(w);
    else
        QueueWake(w->Thread, QS_PAINT);
}

/* the surface to hand to the host for a top level window's dirty rectangle. */
SURFACE *LayerPrepare(WND *w, const RECT *dirty)
{
    SURFACE *s = w->Surface;
    if (s && !(w->LayerFlags & MIME_LAYER_UPDATE))
        FrameSurfaceAlpha(w);
    if (!s || !needs_compose(w)) {
        if (w->Present) {
            PaintRetireSurface(w->Present);
            w->Present = nullptr;
        }
        return s;
    }
    if (!w->Present || w->Present->Width != s->Width || w->Present->Height != s->Height) {
        SURFACE *p = SurfaceCreate(s->Width, s->Height, 32, SURF_CONTIGUOUS | SURF_TOPDOWN);
        if (!p)
            return s;
        if (w->Present)
            PaintRetireSurface(w->Present);
        w->Present = p;
        dirty = nullptr;
    }
    RECT bounds = {0, 0, s->Width, s->Height}, r = bounds;
    if (dirty && !RectIntersect(&r, dirty, &bounds))
        return w->Present;
    BOOLEAN layered = (w->LayerFlags & (LWA_ALPHA | LWA_COLORKEY)) && !(w->LayerFlags & MIME_LAYER_UPDATE);
    ULONG alpha = layered && (w->LayerFlags & LWA_ALPHA) ? w->LayerAlpha : 255;
    BOOLEAN keyed = layered && (w->LayerFlags & LWA_COLORKEY);
    ULONG key = ColorToPixel(w->LayerKey) & 0xFFFFFF;
    for (LONG y = r.top; y < r.bottom; y++) {
        const ULONG *src = (const ULONG *)(s->Bits + (LONG_PTR)y * s->Stride);
        ULONG *dst = (ULONG *)(w->Present->Bits + (LONG_PTR)y * w->Present->Stride);
        for (LONG x = r.left; x < r.right; x++) {
            ULONG p = src[x];
            dst[x] = keyed && (p & 0xFFFFFF) == key ? 0 : p;
        }
    }
    /* the dwm frame goes under the window's pixels before the window's alpha applies to all of it. */
    if (!(w->LayerFlags & MIME_LAYER_UPDATE))
        FrameCompose(w, w->Present, &r);
    if (alpha != 255)
        for (LONG y = r.top; y < r.bottom; y++) {
            ULONG *dst = (ULONG *)(w->Present->Bits + (LONG_PTR)y * w->Present->Stride);
            for (LONG x = r.left; x < r.right; x++)
                dst[x] = premultiply(dst[x], alpha);
        }
    if (w->Shape) {
        /* pixels outside the window region are fully transparent. */
        REGION outside;
        RgnInit(&outside);
        RgnSetRect(&outside, &r);
        RgnCombine(&outside, &outside, w->Shape, RGN_DIFF);
        for (ULONG i = 0; i < outside.Count; i++) {
            const RECT *o = &outside.Rects[i];
            for (LONG y = o->top; y < o->bottom; y++) {
                ULONG *dst = (ULONG *)(w->Present->Bits + (LONG_PTR)y * w->Present->Stride);
                memset(dst + o->left, 0, (SIZE_T)(o->right - o->left) * 4);
            }
        }
        RgnFree(&outside);
    }
    return w->Present;
}

void LayerWindowDestroyed(WND *w)
{
    if (w->Present) {
        PaintRetireSurface(w->Present);
        w->Present = nullptr;
    }
    if (w->Shape) {
        RgnFree(w->Shape);
        W32Free(w->Shape);
        w->Shape = nullptr;
    }
    w->LayerFlags = 0;
}

BOOLEAN LayerHitTest(WND *w, POINT pt)
{
    LONG x = pt.x - w->WindowRect.left, y = pt.y - w->WindowRect.top;
    if (w->Shape && !RgnPtIn(w->Shape, x, y))
        return FALSE;
    /* a pixel with no alpha, or of the color key, lets the mouse through to the window below. */
    SURFACE *s = w->Surface;
    if (!(w->ExStyle & WS_EX_LAYERED) || !w->LayerFlags || !s || s->Bpp != 32 || x < 0 || y < 0 || x >= s->Width ||
        y >= s->Height)
        return TRUE;
    ULONG p = ((const ULONG *)(s->Bits + (LONG_PTR)y * s->Stride))[x];
    if (w->LayerFlags & MIME_LAYER_UPDATE)
        return (p >> 24) != 0;
    if ((w->LayerFlags & LWA_COLORKEY) && (p & 0xFFFFFF) == (ColorToPixel(w->LayerKey) & 0xFFFFFF))
        return FALSE;
    return TRUE;
}

void LayerStyleChanged(WND *w)
{
    if (w->ExStyle & WS_EX_LAYERED)
        return;
    if (!w->LayerFlags)
        return;
    w->LayerFlags = 0;
    if (w->Present) {
        PaintRetireSurface(w->Present);
        w->Present = nullptr;
    }
    WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ALLCHILDREN);
}

/* ------------------------------------------------------------------ services */

BOOL NTAPI NtUserSetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG error = 0;
    if (!w)
        error = ERROR_INVALID_WINDOW_HANDLE;
    else if (!(w->ExStyle & WS_EX_LAYERED) || (flags & ~(LWA_ALPHA | LWA_COLORKEY)))
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        BOOLEAN was_update = (w->LayerFlags & MIME_LAYER_UPDATE) != 0;
        BOOLEAN first = !w->LayerFlags;
        w->LayerFlags = flags ? flags : LWA_ALPHA;
        w->LayerAlpha = flags & LWA_ALPHA ? alpha : 255;
        w->LayerKey = key & 0xFFFFFF;
        ScreenSerial++;
        if (was_update || first) {
            /* the window paints itself again, and becomes visible once it has attributes. */
            WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ALLCHILDREN);
            if (WndIsTopLevel(w))
                WndUpdateHostConfig(w, FALSE);
        }
        if (WndIsTopLevel(w))
            redraw_all(w);
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return !error;
}

BOOL NTAPI NtUserGetLayeredWindowAttributes(HWND hwnd, COLORREF *user_key, BYTE *user_alpha, DWORD *user_flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = w && (w->ExStyle & WS_EX_LAYERED) && w->LayerFlags && !(w->LayerFlags & MIME_LAYER_UPDATE);
    COLORREF key = ok ? w->LayerKey : 0;
    BYTE alpha = ok ? w->LayerAlpha : 0;
    DWORD flags = ok ? w->LayerFlags : 0;
    UserLeave();
    if (!ok) {
        W32SetLastError(w ? ERROR_INVALID_PARAMETER : ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if ((user_key && !NT_SUCCESS(MimeCopyToUser(user_key, &key, sizeof(key)))) ||
        (user_alpha && !NT_SUCCESS(MimeCopyToUser(user_alpha, &alpha, sizeof(alpha)))) ||
        (user_flags && !NT_SUCCESS(MimeCopyToUser(user_flags, &flags, sizeof(flags))))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

/* copies the source dc into the window surface as premultiplied pixels. */
static void copy_layer(WND *w, DC *src, POINT src_pos, const RECT *area, COLORREF key, const BLENDFUNCTION *blend,
                       DWORD flags)
{
    SURFACE *d = w->Surface, *s = src->Surface;
    LONG sx = src_pos.x, sy = src_pos.y;
    DcLogicalToSurface(src, &sx, &sy);
    BOOLEAN use_alpha = (flags & ULW_ALPHA) && blend;
    ULONG constant = use_alpha ? blend->SourceConstantAlpha : 255;
    BOOLEAN per_pixel = use_alpha && (blend->AlphaFormat & AC_SRC_ALPHA) && s->Bpp == 32;
    BOOLEAN keyed = (flags & ULW_COLORKEY) != 0;
    ULONG key_px = ColorToPixel(key) & 0xFFFFFF;
    for (LONG y = area->top; y < area->bottom; y++) {
        ULONG *dst = (ULONG *)(d->Bits + (LONG_PTR)y * d->Stride);
        LONG yy = sy + y;
        for (LONG x = area->left; x < area->right; x++) {
            LONG xx = sx + x;
            ULONG p = 0;
            if (xx >= 0 && yy >= 0 && xx < s->Width && yy < s->Height) {
                p = SurfaceGetPixel(s, xx, yy);
                if (keyed && (p & 0xFFFFFF) == key_px) {
                    p = 0;
                } else if (per_pixel) {
                    if (constant != 255)
                        p = premultiply(p, constant);
                } else {
                    p = (p & 0xFFFFFF) | 0xFF000000u;
                    if (constant != 255)
                        p = premultiply(p, constant);
                }
            }
            dst[x] = p;
        }
    }
}

BOOL NTAPI NtUserUpdateLayeredWindow(HWND hwnd, HDC hdst, POINT *user_dst_pos, SIZE *user_size, HDC hsrc,
                                     POINT *user_src_pos, COLORREF key, BLENDFUNCTION *user_blend, DWORD flags,
                                     RECT *user_dirty)
{
    POINT dst_pos, src_pos = {0, 0};
    SIZE size;
    BLENDFUNCTION blend;
    RECT dirty;
    if ((user_dst_pos && !NT_SUCCESS(MimeCopyFromUser(&dst_pos, user_dst_pos, sizeof(dst_pos)))) ||
        (user_size && !NT_SUCCESS(MimeCopyFromUser(&size, user_size, sizeof(size)))) ||
        (user_src_pos && !NT_SUCCESS(MimeCopyFromUser(&src_pos, user_src_pos, sizeof(src_pos)))) ||
        (user_blend && !NT_SUCCESS(MimeCopyFromUser(&blend, user_blend, sizeof(blend)))) ||
        (user_dirty && !NT_SUCCESS(MimeCopyFromUser(&dirty, user_dirty, sizeof(dirty))))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    if ((flags & ~(ULW_ALPHA | ULW_COLORKEY | ULW_OPAQUE | ULW_EX_NORESIZE)) ||
        (user_size && (size.cx < 0 || size.cy < 0)) || (hsrc && !user_src_pos && !user_size)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG error = 0;
    if (!w)
        error = ERROR_INVALID_WINDOW_HANDLE;
    else if (!(w->ExStyle & WS_EX_LAYERED) || (w->LayerFlags && !(w->LayerFlags & MIME_LAYER_UPDATE)))
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        LONG cx = w->WindowRect.right - w->WindowRect.left, cy = w->WindowRect.bottom - w->WindowRect.top;
        if (user_size && (size.cx != cx || size.cy != cy) && (flags & ULW_EX_NORESIZE))
            error = ERROR_INCORRECT_SIZE;
    }
    if (!error) {
        BOOLEAN first = !(w->LayerFlags & MIME_LAYER_UPDATE);
        w->LayerFlags = MIME_LAYER_UPDATE;
        BOOLEAN changed = WndLayerRects(w, user_dst_pos ? &dst_pos : nullptr, user_size ? &size : nullptr);
        /* the window no longer paints: its pixels come from the caller. */
        WndValidate(w, nullptr);
        w->Flags &= ~(WNDF_FRAME_DIRTY | WNDF_INTERNAL_PAINT);
        if (WndIsTopLevel(w))
            WndEnsureSurface(w);
        /* what the program painted before its first update is not part of the layer. */
        if (first && w->Surface)
            memset(w->Surface->Bits, 0, (SIZE_T)w->Surface->Stride * (SIZE_T)w->Surface->Height);
        DC *src = hsrc ? DcLookup(hsrc) : nullptr;
        if (src && src->Surface && w->Surface) {
            RECT area = {0, 0, w->Surface->Width, w->Surface->Height};
            if (user_dirty) {
                RECT clipped;
                if (!RectIntersect(&clipped, &area, &dirty))
                    clipped = (RECT){0, 0, 0, 0};
                area = clipped;
            }
            copy_layer(w, src, src_pos, &area, key, user_blend ? &blend : nullptr, flags);
            SurfaceMarkDirty(w->Surface, &area);
        }
        if ((first || changed) && WndIsTopLevel(w))
            WndUpdateHostConfig(w, FALSE);
        if (WndIsTopLevel(w))
            PaintPresent(w);
        (void)hdst;
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return !error;
}

int NTAPI NtUserSetWindowRgn(HWND hwnd, HRGN region, BOOL redraw)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    GDI_REGION *r = region ? (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION) : nullptr;
    int ok = 0;
    if (w && (!region || r)) {
        REGION *shape = nullptr;
        if (r) {
            shape = W32Alloc(sizeof(REGION));
            if (shape) {
                RgnInit(shape);
                RgnCopy(shape, &r->Region);
            }
        }
        if (!region || shape) {
            if (w->Shape) {
                RgnFree(w->Shape);
                W32Free(w->Shape);
            }
            w->Shape = shape;
            ScreenSerial++;
            ScreenLayersChanged(w);
            /* the system owns the region from now on. */
            if (r)
                GdiDeleteObject(&r->Header);
            if (WndIsTopLevel(w) && w->Surface) {
                if (redraw)
                    WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ALLCHILDREN);
                redraw_all(w);
            } else if (redraw && w->Parent) {
                WndInvalidateRegion(w->Parent, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            ok = 1;
        }
    }
    UserLeave();
    if (!ok)
        W32SetLastError(w ? ERROR_INVALID_PARAMETER : ERROR_INVALID_WINDOW_HANDLE);
    return ok;
}

int NTAPI NtUserGetWindowRgnEx(HWND hwnd, HRGN region, UINT reserved)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    GDI_REGION *r = region ? (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION) : nullptr;
    int result = ERROR;
    if (w && r && w->Shape && RgnCopy(&r->Region, w->Shape))
        result = RgnComplexity(&r->Region);
    UserLeave();
    (void)reserved;
    return result;
}
