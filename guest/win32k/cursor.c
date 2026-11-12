/*
 * cursor and icon objects. user32 decodes the resources and hands win32k a
 * 32 bpp image that is kept premultiplied in a kernel surface.
 */
#include "win32k.h"

typedef struct CURICON {
    USER_OBJECT Header;
    BOOLEAN IsIcon;
    BOOLEAN Ready;
    LONG Width;
    LONG Height;
    POINT Hotspot;
    SURFACE *Image;  /* premultiplied 32 bpp. */
    ULONG System;    /* MIME_CURSOR_* + 1 for system cursors, 0 otherwise. */
    W32PROCESS *Process;
    UCHAR Awareness; /* of the thread that made it: the pixels its image is in. */
} CURICON;

static CURICON *lookup(HICON icon)
{
    return (CURICON *)UserLookup(icon, USER_TYPE_CURSOR);
}

BOOLEAN CursorValid(HICON icon)
{
    return lookup(icon) != nullptr;
}

SURFACE *CursorIconImage(HICON icon)
{
    CURICON *cursor = lookup(icon);
    return cursor && cursor->IsIcon && cursor->Ready ? cursor->Image : nullptr;
}

SURFACE *CursorImage(HICON cursor)
{
    CURICON *c = lookup(cursor);
    return c && c->Ready ? c->Image : nullptr;
}

/* the awareness of the pixels of a cursor, unaware when it is none. */
ULONG CursorAwareness(HICON cursor)
{
    CURICON *c = lookup(cursor);
    return c ? c->Awareness : DPI_AWARE_UNAWARE;
}

void CursorHotspot(HICON cursor, LONG *x, LONG *y)
{
    CURICON *c = lookup(cursor);
    *x = c ? c->Hotspot.x : 0;
    *y = c ? c->Hotspot.y : 0;
}

void CursorInitialize(void)
{
}

static ULONG host_cursor_for_idc(ULONG id)
{
    switch (id) {
    case 32513: /* IDC_IBEAM */
        return MIME_CURSOR_IBEAM;
    case 32514: /* IDC_WAIT */
    case 32650: /* IDC_APPSTARTING */
        return MIME_CURSOR_WAIT;
    case 32515: /* IDC_CROSS */
        return MIME_CURSOR_CROSS;
    case 32649: /* IDC_HAND */
        return MIME_CURSOR_HAND;
    case 32645: /* IDC_SIZENS */
    case 32652: /* the scroll cursors, as the host has them. */
    case 32655:
    case 32656:
        return MIME_CURSOR_SIZE_NS;
    case 32653:
    case 32657:
    case 32658:
        return MIME_CURSOR_SIZE_WE;
    case 32654:
        return MIME_CURSOR_SIZE_ALL;
    case 32659:
    case 32662:
        return MIME_CURSOR_SIZE_NWSE;
    case 32660:
    case 32661:
        return MIME_CURSOR_SIZE_NESW;
    case 32644: /* IDC_SIZEWE */
        return MIME_CURSOR_SIZE_WE;
    case 32642: /* IDC_SIZENWSE */
        return MIME_CURSOR_SIZE_NWSE;
    case 32643: /* IDC_SIZENESW */
        return MIME_CURSOR_SIZE_NESW;
    case 32646: /* IDC_SIZEALL */
        return MIME_CURSOR_SIZE_ALL;
    case 32648: /* IDC_NO */
        return MIME_CURSOR_NO;
    default:
        return MIME_CURSOR_ARROW;
    }
}

/* the cursor of a built in class: one of the system's, which no process owns and the host draws. */
HCURSOR CursorSystem(ULONG idc)
{
    static struct {
        ULONG Id;
        HCURSOR Cursor;
    } made[8];
    for (ULONG i = 0; i < RTL_NUMBER_OF(made); i++) {
        if (made[i].Cursor && made[i].Id == idc)
            return made[i].Cursor;
        if (made[i].Cursor)
            continue;
        CURICON *c = W32Alloc(sizeof(CURICON));
        if (!c)
            return nullptr;
        c->Width = c->Height = 32;
        c->System = host_cursor_for_idc(idc) + 1;
        c->Ready = TRUE;
        made[i].Cursor = UserCreateHandle(&c->Header, USER_TYPE_CURSOR);
        if (!made[i].Cursor) {
            W32Free(c);
            return nullptr;
        }
        made[i].Id = idc;
        return made[i].Cursor;
    }
    return nullptr;
}

HICON NTAPI NtUserCreateEmptyCurObject(BOOL icon)
{
    UserEnter();
    CURICON *c = W32Alloc(sizeof(CURICON));
    HICON h = nullptr;
    if (c) {
        c->IsIcon = icon != 0;
        c->Process = W32GetProcess();
        c->Awareness = (UCHAR)DpiCurrentAwareness();
        h = UserCreateHandle(&c->Header, USER_TYPE_CURSOR);
        if (!h)
            W32Free(c);
    }
    UserLeave();
    return h;
}

static SURFACE *build_image(const MIME_CURSOR_DATA *d)
{
    LONG w = (LONG)d->Width, h = (LONG)d->Height;
    SURFACE *color = d->Color ? SurfaceLookup(d->Color) : nullptr;
    SURFACE *mask = d->Mask ? SurfaceLookup(d->Mask) : nullptr;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || (!color && !mask))
        return nullptr;
    SURFACE *img = SurfaceCreate(w, h, 32, 0);
    if (!img)
        return nullptr;
    BOOLEAN mono = !color;
    BOOLEAN has_alpha = FALSE;
    if (color && color->Bpp == 32) {
        for (LONG y = 0; y < h && y < color->Height && !has_alpha; y++)
            for (LONG x = 0; x < w && x < color->Width; x++)
                if (SurfaceGetPixel(color, x, y) >> 24) {
                    has_alpha = TRUE;
                    break;
                }
    }
    for (LONG y = 0; y < h; y++) {
        PULONG row = (PULONG)(img->Bits + (LONG_PTR)y * img->Stride);
        for (LONG x = 0; x < w; x++) {
            ULONG px;
            if (has_alpha) {
                px = SurfaceGetPixel(color, x, y);
            } else {
                BOOLEAN transparent = FALSE;
                ULONG xor_px = 0;
                if (mask) {
                    transparent = (SurfaceGetPixel(mask, x, y) & 0xFFFFFF) != 0;
                    if (mono)
                        xor_px = (SurfaceGetPixel(mask, x, y + h) & 0xFFFFFF) ? 0xFFFFFF : 0;
                }
                if (color && x < color->Width && y < color->Height)
                    xor_px = SurfaceGetPixel(color, x, y) & 0xFFFFFF;
                px = transparent ? 0 : (0xFF000000u | xor_px);
            }
            row[x] = px;
        }
    }
    return img;
}

BOOL NTAPI NtUserSetCursorIconData(HICON icon, PUNICODE_STRING module, PUNICODE_STRING resource,
                                   const MIME_CURSOR_DATA *user_data)
{
    MIME_CURSOR_DATA d;
    if (!user_data || !NT_SUCCESS(MimeCopyFromUser(&d, user_data, sizeof(d))))
        return FALSE;
    UserEnter();
    CURICON *c = lookup(icon);
    BOOL ok = FALSE;
    if (c) {
        c->IsIcon = d.IsIcon != 0;
        c->Width = (LONG)d.Width;
        c->Height = (LONG)d.Height;
        c->Hotspot = (POINT){(LONG)d.HotspotX, (LONG)d.HotspotY};
        if (d.Flags & MIME_CURSORF_SYSTEM)
            c->System = host_cursor_for_idc(d.Flags & 0xFFFF) + 1;
        SURFACE *img = build_image(&d);
        if (img || c->System) {
            HostForgetCursor(icon);
            if (c->Image)
                SurfaceDestroy(c->Image);
            c->Image = img;
            c->Ready = TRUE;
            ok = TRUE;
        }
    }
    UserLeave();
    (void)module;
    (void)resource;
    return ok;
}

BOOL NTAPI NtUserDestroyCursor(HICON icon, BOOL force)
{
    UserEnter();
    CURICON *c = lookup(icon);
    /* the system's own are never destroyed. */
    if (c && c->System && !c->Process)
        c = nullptr;
    if (c) {
        HostForgetCursor(icon);
        if (c->Image)
            SurfaceDestroy(c->Image);
        c->Image = nullptr;
        c->Header.Destroyed = TRUE;
        UserFreeHandle(&c->Header);
        UserDereference(&c->Header);
    }
    UserLeave();
    (void)force;
    return c != nullptr;
}

/* a 32 bpp bitmap copy owned by the process, for GetIconInfo. */
static HBITMAP export_bitmap(SURFACE *src, BOOLEAN mask)
{
    SURFACE *s = SurfaceCreate(src->Width, src->Height, mask ? 1 : 32, 0);
    if (!s)
        return nullptr;
    for (LONG y = 0; y < src->Height; y++)
        for (LONG x = 0; x < src->Width; x++) {
            ULONG px = SurfaceGetPixel(src, x, y);
            if (mask)
                SurfacePutPixel(s, x, y, (px >> 24) ? 0 : 0xFFFFFF);
            else
                SurfacePutPixel(s, x, y, px);
        }
    HANDLE h = GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP);
    if (!h)
        SurfaceDestroy(s);
    return (HBITMAP)h;
}

BOOL NTAPI NtUserGetIconInfo(HICON icon, ICONINFO *user_info, PUNICODE_STRING module, PUNICODE_STRING resource,
                             LPDWORD bpp, BOOL internal)
{
    UserEnter();
    CURICON *c = lookup(icon);
    BOOL ok = FALSE;
    if (c && c->Image) {
        ICONINFO info;
        memset(&info, 0, sizeof(info));
        info.fIcon = c->IsIcon;
        info.xHotspot = (DWORD)c->Hotspot.x;
        info.yHotspot = (DWORD)c->Hotspot.y;
        info.hbmColor = export_bitmap(c->Image, FALSE);
        info.hbmMask = export_bitmap(c->Image, TRUE);
        ok = NT_SUCCESS(MimeCopyToUser(user_info, &info, sizeof(info)));
        if (bpp) {
            DWORD v = 32;
            MimeCopyToUser(bpp, &v, sizeof(v));
        }
    }
    UserLeave();
    (void)module;
    (void)resource;
    (void)internal;
    return ok;
}

void CursorDrawIcon(DC *dc, LONG x, LONG y, HICON icon, LONG cx, LONG cy, UINT flags)
{
    CURICON *c = lookup(icon);
    if (!c || !c->Image || !dc->Surface)
        return;
    if (!cx)
        cx = (flags & DI_DEFAULTSIZE) ? MetricsGet(c->IsIcon ? SM_CXICON : SM_CXCURSOR, dc->Dpi) : c->Width;
    if (!cy)
        cy = (flags & DI_DEFAULTSIZE) ? MetricsGet(c->IsIcon ? SM_CYICON : SM_CYCURSOR, dc->Dpi) : c->Height;
    LONG l = x, t = y, r = x + cx, b = y + cy;
    DcLogicalToSurface(dc, &l, &t);
    DcLogicalToSurface(dc, &r, &b);
    RECT dst = {l, t, r, b};
    RECT src = {0, 0, c->Image->Width, c->Image->Height};
    REGION clip;
    RgnInit(&clip);
    if (DcGetClip(dc, &clip)) {
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlendSurface(dc->Surface, &clip, &dst, c->Image, &src, blend);
    }
    RgnFree(&clip);
}

BOOL NTAPI NtUserDrawIconEx(HDC hdc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH brush, UINT flags,
                            BOOL meta, PVOID reserved)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = FALSE;
    if (dc && lookup(icon)) {
        if (brush) {
            BRUSH *b = (BRUSH *)GdiLookup(brush, GDI_TYPE_BRUSH);
            CURICON *c = lookup(icon);
            LONG w = cx ? cx : c->Width, h = cy ? cy : c->Height;
            LONG l = x, t = y, r = x + w, bt = y + h;
            DcLogicalToSurface(dc, &l, &t);
            DcLogicalToSurface(dc, &r, &bt);
            RECT rect = {l, t, r, bt};
            REGION clip;
            RgnInit(&clip);
            if (b && DcGetClip(dc, &clip))
                DrawBrushRect(dc, &clip, &rect, b, PATCOPY);
            RgnFree(&clip);
        }
        CursorDrawIcon(dc, x, y, icon, cx, cy, flags);
        ok = TRUE;
    }
    UserLeave();
    (void)step;
    (void)meta;
    (void)reserved;
    return ok;
}

void CursorSet(HCURSOR cursor)
{
    W32THREAD *t = W32GetThread();
    if (t)
        t->Cursor = cursor;
    if (!cursor) {
        HostSetCursor(MIME_CURSOR_HIDDEN);
        return;
    }
    if (t && t->CursorShowCount < 0)
        return;
    CURICON *c = lookup(cursor);
    /* a cursor the program made is shown as it is, the system's are the host's own. */
    if (c && !c->System && c->Image) {
        CaptureCursorShown(cursor);
        if (HostSetCursorImage(cursor))
            return;
    }
    HostSetCursor(c && c->System ? c->System - 1 : MIME_CURSOR_ARROW);
    CaptureCursorShown(c && !c->System && c->Image ? cursor : nullptr);
}

HCURSOR NTAPI NtUserSetCursor(HCURSOR cursor)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HCURSOR old = t ? t->Cursor : nullptr;
    CursorSet(cursor);
    UserLeave();
    return old;
}
