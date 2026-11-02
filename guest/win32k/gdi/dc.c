/*
 * device contexts: attributes, object selection, saved states, clipping
 * and coordinate mapping.
 */
#include "../win32k.h"

GDI_OBJECT *GdiStockObject(int index);

DC *DcLookup(HDC hdc)
{
    return (DC *)GdiLookup(hdc, GDI_TYPE_DC);
}

static void select_ref(GDI_OBJECT *o)
{
    if (o)
        o->SelectCount++;
}

/* drops a selection and finishes a pending delete. */
static void select_unref(GDI_OBJECT *o)
{
    if (!o)
        return;
    if (o->SelectCount)
        o->SelectCount--;
    if (!o->SelectCount && (o->Flags & GDI_FLAG_DELETE_PENDING)) {
        o->Flags &= (UCHAR)~GDI_FLAG_DELETE_PENDING;
        GdiDeleteObject(o);
    }
}

void DcResetState(DC *dc)
{
    DC_STATE *s = &dc->State;
    s->TextColor = RGB(0, 0, 0);
    s->BkColor = RGB(255, 255, 255);
    s->BrushColor = RGB(255, 255, 255);
    s->PenColor = RGB(0, 0, 0);
    s->BkMode = OPAQUE;
    s->TextAlign = TA_LEFT | TA_TOP | TA_NOUPDATECP;
    s->Rop2 = R2_COPYPEN;
    s->StretchMode = BLACKONWHITE;
    s->GraphicsMode = GM_COMPATIBLE;
    s->MapMode = MM_TEXT;
    s->TextCharExtra = 0;
    s->Layout = 0;
    s->ViewportOrg = (POINT){0, 0};
    s->WindowOrg = (POINT){0, 0};
    s->CurrentPos = (POINT){0, 0};
    RgnClear(&s->ClipRgn);
    s->HasClip = FALSE;
    s->HasXform = FALSE;
    s->World = (XFORM){1, 0, 0, 1, 0, 0};
    s->PolyFillMode = ALTERNATE;
    s->ArcDirection = AD_COUNTERCLOCKWISE;
    s->WindowExt = (SIZE){1, 1};
    s->ViewportExt = (SIZE){1, 1};
    s->HasPage = FALSE;
    RgnClear(&s->MetaRgn);
    s->HasMeta = FALSE;
    s->MiterLimit = 10.0f;
}

void DcSelectDefaults(DC *dc)
{
    DC_STATE *s = &dc->State;
    select_unref(&s->Brush->Header);
    select_unref(&s->Pen->Header);
    select_unref(&s->Font->Header);
    s->Brush = (BRUSH *)GdiStockObject(WHITE_BRUSH);
    s->Pen = (PEN *)GdiStockObject(BLACK_PEN);
    s->Font = (FONT *)GdiStockObject(SYSTEM_FONT);
    select_ref(&s->Brush->Header);
    select_ref(&s->Pen->Header);
    select_ref(&s->Font->Header);
}

DC *DcCreate(ULONG flags)
{
    DC *dc = W32Alloc(sizeof(DC));
    if (!dc)
        return nullptr;
    dc->Flags = flags;
    dc->Awareness = (UCHAR)DpiCurrentAwareness();
    dc->Dpi = DpiScaleOf(dc->Awareness);
    RgnInit(&dc->VisRgn);
    RgnInit(&dc->State.ClipRgn);
    RgnInit(&dc->State.MetaRgn);
    DcResetState(dc);
    DcSelectDefaults(dc);
    if (!GdiCreateHandle(&dc->Header, GDI_TYPE_DC)) {
        RgnFree(&dc->VisRgn);
        W32Free(dc);
        return nullptr;
    }
    return dc;
}

void DcSetSurface(DC *dc, SURFACE *s)
{
    if (dc->Surface == s)
        return;
    if (dc->Surface && (dc->Flags & DC_MEMORY))
        select_unref(&dc->Surface->Header);
    dc->Surface = s;
    if (s && (dc->Flags & DC_MEMORY))
        select_ref(&s->Header);
    RECT bounds = {0, 0, s ? s->Width : 0, s ? s->Height : 0};
    if (dc->Flags & DC_MEMORY)
        RgnSetRect(&dc->VisRgn, &bounds);
}

static void free_saved(DC *dc)
{
    while (dc->Saved) {
        DC_SAVED *sv = dc->Saved;
        dc->Saved = sv->Next;
        select_unref(&sv->State.Brush->Header);
        select_unref(&sv->State.Pen->Header);
        select_unref(&sv->State.Font->Header);
        RgnFree(&sv->State.ClipRgn);
        RgnFree(&sv->State.MetaRgn);
        W32Free(sv);
    }
    dc->SaveLevel = 0;
}

void DcFree(DC *dc)
{
    free_saved(dc);
    select_unref(&dc->State.Brush->Header);
    select_unref(&dc->State.Pen->Header);
    select_unref(&dc->State.Font->Header);
    if ((dc->Flags & DC_MEMORY) && dc->Surface)
        select_unref(&dc->Surface->Header);
    if (dc->DefaultBitmap)
        SurfaceDestroy(dc->DefaultBitmap);
    RgnFree(&dc->VisRgn);
    RgnFree(&dc->State.ClipRgn);
    RgnFree(&dc->State.MetaRgn);
    PathFree(&dc->Path);
    GdiFreeHandle(&dc->Header);
    W32Free(dc);
}

BOOLEAN DcGetClip(DC *dc, REGION *out)
{
    if (!dc->Surface)
        return FALSE;
    if (!RgnCopy(out, &dc->VisRgn))
        return FALSE;
    if (dc->State.HasClip) {
        REGION app;
        RgnInit(&app);
        RgnCopy(&app, &dc->State.ClipRgn);
        RgnOffset(&app, dc->Origin.x, dc->Origin.y);
        RgnCombine(out, out, &app, RGN_AND);
        RgnFree(&app);
    }
    if (dc->State.HasMeta) {
        REGION meta;
        RgnInit(&meta);
        RgnCopy(&meta, &dc->State.MetaRgn);
        RgnOffset(&meta, dc->Origin.x, dc->Origin.y);
        RgnCombine(out, out, &meta, RGN_AND);
        RgnFree(&meta);
    }
    RECT bounds = {0, 0, dc->Surface->Width, dc->Surface->Height};
    RgnCombineRect(out, out, &bounds, RGN_AND);
    return !RgnIsEmpty(out);
}

static LONG round_xf(float v)
{
    return (LONG)(v >= 0 ? v + 0.5f : v - 0.5f);
}

/* world to page with the GM_ADVANCED transform. rotations move points but not rectangles or glyphs. */
void DcTransformPoint(DC *dc, LONG *x, LONG *y)
{
    if (!dc->State.HasXform)
        return;
    const XFORM *m = &dc->State.World;
    float fx = (float)*x, fy = (float)*y;
    *x = round_xf(fx * m->eM11 + fy * m->eM21 + m->eDx);
    *y = round_xf(fx * m->eM12 + fy * m->eM22 + m->eDy);
}

static LONG round_double(double v)
{
    if (v > 2147483647.0)
        return 0x7FFFFFFF;
    if (v < -2147483648.0)
        return (LONG)0x80000000;
    return (LONG)(v >= 0 ? v + 0.5 : v - 0.5);
}

static void page_scale(DC *dc, double *sx, double *sy)
{
    const DC_STATE *s = &dc->State;
    *sx = s->WindowExt.cx ? (double)s->ViewportExt.cx / s->WindowExt.cx : 1.0;
    *sy = s->WindowExt.cy ? (double)s->ViewportExt.cy / s->WindowExt.cy : 1.0;
}

void DcGetXform(DC *dc, XFORM *out)
{
    const DC_STATE *s = &dc->State;
    double sx, sy;
    page_scale(dc, &sx, &sy);
    const XFORM *w = &s->World;
    out->eM11 = (float)(w->eM11 * sx);
    out->eM12 = (float)(w->eM12 * sy);
    out->eM21 = (float)(w->eM21 * sx);
    out->eM22 = (float)(w->eM22 * sy);
    out->eDx = (float)((w->eDx - s->WindowOrg.x) * sx + s->ViewportOrg.x);
    out->eDy = (float)((w->eDy - s->WindowOrg.y) * sy + s->ViewportOrg.y);
}

BOOLEAN DcIsScaled(DC *dc)
{
    const XFORM *w = &dc->State.World;
    return dc->State.HasPage || (dc->State.HasXform && (w->eM11 != 1 || w->eM22 != 1 || w->eM12 || w->eM21));
}

void DcLogicalToDevice(DC *dc, LONG *x, LONG *y)
{
    if (!dc->State.HasPage) {
        DcTransformPoint(dc, x, y);
        *x = *x - dc->State.WindowOrg.x + dc->State.ViewportOrg.x;
        *y = *y - dc->State.WindowOrg.y + dc->State.ViewportOrg.y;
        return;
    }
    const DC_STATE *s = &dc->State;
    const XFORM *w = &s->World;
    double sx, sy;
    page_scale(dc, &sx, &sy);
    double fx = *x, fy = *y;
    double wx = fx * w->eM11 + fy * w->eM21 + w->eDx, wy = fx * w->eM12 + fy * w->eM22 + w->eDy;
    *x = round_double((wx - s->WindowOrg.x) * sx + s->ViewportOrg.x);
    *y = round_double((wy - s->WindowOrg.y) * sy + s->ViewportOrg.y);
}

void DcDeviceToLogical(DC *dc, LONG *x, LONG *y)
{
    const DC_STATE *s = &dc->State;
    double sx, sy;
    page_scale(dc, &sx, &sy);
    double px = (*x - s->ViewportOrg.x) / sx + s->WindowOrg.x, py = (*y - s->ViewportOrg.y) / sy + s->WindowOrg.y;
    if (s->HasXform) {
        const XFORM *m = &s->World;
        double det = (double)m->eM11 * m->eM22 - (double)m->eM12 * m->eM21;
        if (det != 0) {
            double fx = px - m->eDx, fy = py - m->eDy;
            px = (fx * m->eM22 - fy * m->eM21) / det;
            py = (fy * m->eM11 - fx * m->eM12) / det;
        }
    }
    *x = round_double(px);
    *y = round_double(py);
}

void DcLogicalToSurface(DC *dc, LONG *x, LONG *y)
{
    DcLogicalToDevice(dc, x, y);
    *x += dc->Origin.x;
    *y += dc->Origin.y;
}

/* a length through the transform, scaled by the square root of its determinant. */
LONG DcScaleToDevice(DC *dc, LONG v)
{
    if (!DcIsScaled(dc))
        return v;
    XFORM m;
    DcGetXform(dc, &m);
    double det = (double)m.eM11 * m.eM22 - (double)m.eM12 * m.eM21;
    if (det < 0)
        det = -det;
    double s = 1.0, r = det;
    /* newton steps for the square root, without the c library. */
    if (r > 0) {
        s = r > 1 ? r : 1;
        for (int i = 0; i < 40; i++)
            s = (s + r / s) / 2;
    }
    return round_double(v * s);
}

LONG DcPenWidth(DC *dc)
{
    const PEN *p = dc->State.Pen;
    if (p->Width <= 1 || ((p->Style & PS_TYPE_MASK) == PS_COSMETIC && p->Header.Type == GDI_TYPE_EXTPEN))
        return 1;
    LONG w = DcScaleToDevice(dc, p->Width);
    return w > 1 ? w : 1;
}

FONT_INSTANCE *DcGetFont(DC *dc)
{
    if (dc->Realized && dc->RealizedFor == dc->State.Font)
        return dc->Realized;
    dc->Realized = FontRealize(&dc->State.Font->Logical, dc->Dpi);
    dc->RealizedFor = dc->State.Font;
    return dc->Realized;
}

/* ------------------------------------------------------------------ dc creation */

HDC NTAPI NtGdiCreateCompatibleDC(HDC hdc)
{
    UserEnter();
    DC *dc = DcCreate(DC_MEMORY);
    HDC result = nullptr;
    if (dc) {
        DC *ref = hdc ? DcLookup(hdc) : nullptr;
        if (ref) {
            dc->Dpi = ref->Dpi;
            dc->Awareness = ref->Awareness;
        }
        /* memory dcs start with a 1x1 monochrome bitmap. */
        dc->DefaultBitmap = SurfaceCreate(1, 1, 1, 0);
        if (dc->DefaultBitmap) {
            DcSetSurface(dc, dc->DefaultBitmap);
            result = (HDC)dc->Header.Handle;
        } else {
            DcFree(dc);
        }
    }
    UserLeave();
    return result;
}

/*
 * the win32k half of an enhanced metafile dc: a reference device dc without a surface, so drawing only moves the
 * current position and builds paths while gdi32 records.
 */
HDC NTAPI NtGdiCreateMetafileDC(HDC hdc)
{
    UserEnter();
    DC *ref = DcLookup(hdc);
    DC *dc = ref ? DcCreate(DC_METAFILE) : nullptr;
    HDC result = nullptr;
    if (dc) {
        dc->Dpi = ref->Dpi;
        dc->Awareness = ref->Awareness;
        RECT all = {0, 0, W32Display.Width, W32Display.Height};
        RgnSetRect(&dc->VisRgn, &all);
        result = (HDC)dc->Header.Handle;
    }
    UserLeave();
    return result;
}

HDC NTAPI NtGdiOpenDCW(PUNICODE_STRING device, PVOID mode, PUNICODE_STRING port, ULONG type, BOOL display,
                       HANDLE pdev, PVOID driver_info, PVOID umpd)
{
    /*
     * the display dc reads the screen as composed from the windows, and what it draws is not shown. no name or
     * DISPLAY means the whole virtual screen, \\.\DISPLAYn one monitor, and a missing monitor gives no dc.
     */
    PWSTR name = nullptr;
    if (!NT_SUCCESS(W32CaptureString(device, &name, nullptr)))
        return nullptr;
    HDC h = nullptr;
    UserEnter();
    if (!name || !*name || !_wcsicmp(name, L"DISPLAY")) {
        h = PaintGetDC(WndDesktop(), nullptr, DCX_WINDOW);
    } else {
        LONG monitor = MonitorFromDeviceName(name);
        if (monitor >= 0)
            h = PaintGetMonitorDC((ULONG)monitor);
    }
    UserLeave();
    W32Free(name);
    return h;
}

/* a screen dc while a program has set an 8 bit display mode. */
static BOOLEAN palette_display(const DC *dc)
{
    return W32Display.Bits == 8 && !(dc && dc->Surface && (dc->Flags & DC_MEMORY));
}

int NTAPI NtGdiGetDeviceCaps(HDC hdc, int index)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int v = 0;
    ULONG awareness = dc ? dc->Awareness : DpiCurrentAwareness();
    UINT dpi = dc ? dc->Dpi : DpiScaleOf(awareness);
    /* size and refresh rate of the dc's monitor, else of the primary one. */
    LONG width = W32Display.Width, height = W32Display.Height;
    ULONG refresh = W32Display.RefreshHz;
    MIME_MONITOR_INFO monitor;
    if (dc && dc->OfMonitor && MonitorGet(dc->Monitor, &monitor)) {
        width = monitor.Rect.right - monitor.Rect.left;
        height = monitor.Rect.bottom - monitor.Rect.top;
        refresh = monitor.RefreshHz;
    }
    /* the desktop in the pixels of the dc, DESKTOPHORZRES keeps the real ones. */
    LONG desktop_width = width, desktop_height = height;
    width = DpiScale(width, W32SystemDpi(), DpiScaleOf(awareness));
    height = DpiScale(height, W32SystemDpi(), DpiScaleOf(awareness));
    switch (index) {
    case DRIVERVERSION:
        v = 0x4001;
        break;
    case TECHNOLOGY:
        v = DT_RASDISPLAY;
        break;
    case HORZSIZE:
        v = W32MulDiv(width, 254, (LONG)dpi * 10);
        break;
    case VERTSIZE:
        v = W32MulDiv(height, 254, (LONG)dpi * 10);
        break;
    case HORZRES:
        v = width;
        break;
    case DESKTOPHORZRES:
        v = desktop_width;
        break;
    case VERTRES:
        v = height;
        break;
    case DESKTOPVERTRES:
        v = desktop_height;
        break;
    case BITSPIXEL:
        v = dc && dc->Surface && (dc->Flags & DC_MEMORY) ? dc->Surface->Bpp : (int)W32Display.Bits;
        break;
    case PLANES:
        v = 1;
        break;
    case NUMBRUSHES:
    case NUMPENS:
        v = -1;
        break;
    case NUMCOLORS:
        /* a palette display has the twenty static colors, others more than an int holds. */
        v = palette_display(dc) ? 20 : -1;
        break;
    case ASPECTX:
    case ASPECTY:
        v = 36;
        break;
    case ASPECTXY:
        v = 51;
        break;
    case LOGPIXELSX:
    case LOGPIXELSY:
        v = (int)dpi;
        break;
    case SIZEPALETTE:
        v = palette_display(dc) ? 256 : 0;
        break;
    case NUMRESERVED:
        v = palette_display(dc) ? 20 : 0;
        break;
    case COLORRES:
        v = palette_display(dc) ? 18 : 24;
        break;
    case VREFRESH:
        v = (int)refresh;
        break;
    case BLTALIGNMENT:
        v = 1;
        break;
    case SHADEBLENDCAPS:
        v = 0x7F;
        break;
    case RASTERCAPS:
        v = RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP | RC_DIBTODEV | RC_STRETCHDIB | (palette_display(dc) ? RC_PALETTE : 0);
        break;
    case TEXTCAPS:
        v = 0x7807;
        break;
    case CLIPCAPS:
        v = 1;
        break;
    }
    UserLeave();
    return v;
}

/* ------------------------------------------------------------------ mapping modes */

static void update_page(DC_STATE *s)
{
    s->HasPage = s->WindowExt.cx != s->ViewportExt.cx || s->WindowExt.cy != s->ViewportExt.cy;
}

/* the isotropic mode keeps units square: the larger viewport extent shrinks to the smaller scale. */
static void isotropic_adjust(DC_STATE *s)
{
    LONG wx = s->WindowExt.cx < 0 ? -s->WindowExt.cx : s->WindowExt.cx;
    LONG wy = s->WindowExt.cy < 0 ? -s->WindowExt.cy : s->WindowExt.cy;
    LONG vx = s->ViewportExt.cx < 0 ? -s->ViewportExt.cx : s->ViewportExt.cx;
    LONG vy = s->ViewportExt.cy < 0 ? -s->ViewportExt.cy : s->ViewportExt.cy;
    if (!wx || !wy || !vx || !vy)
        return;
    LONG64 ax = (LONG64)vx * wy, ay = (LONG64)vy * wx;
    if (ax < ay) {
        LONG n = W32MulDiv(vx, wy, wx);
        s->ViewportExt.cy = s->ViewportExt.cy < 0 ? -n : n;
    } else if (ax > ay) {
        LONG n = W32MulDiv(vy, wx, wy);
        s->ViewportExt.cx = s->ViewportExt.cx < 0 ? -n : n;
    }
}

/*
 * the extents windows gives each mapping mode: the viewport is the device in pixels with y up, the window its
 * physical size in the mode's units, from the virtual resolution when one is set.
 */
static BOOL set_map_mode(DC *dc, int mode, DWORD *old)
{
    DC_STATE *s = &dc->State;
    *old = (DWORD)s->MapMode;
    if (mode < MM_TEXT || mode > MM_ANISOTROPIC)
        return FALSE;
    if (mode == s->MapMode && mode != MM_ISOTROPIC)
        return TRUE;
    s->MapMode = mode;
    if (mode == MM_TEXT) {
        s->WindowExt = s->ViewportExt = (SIZE){1, 1};
    } else if (mode != MM_ANISOTROPIC) {
        LONG px, py, mx, my;
        BOOLEAN virt = dc->VirtualPixels.cx != 0;
        if (virt) {
            px = dc->VirtualPixels.cx;
            py = dc->VirtualPixels.cy;
            mx = dc->VirtualMm.cx;
            my = dc->VirtualMm.cy;
        } else {
            px = DpiScale(W32Display.Width, W32SystemDpi(), DpiScaleOf(dc->Awareness));
            py = DpiScale(W32Display.Height, W32SystemDpi(), DpiScaleOf(dc->Awareness));
            /* the physical size in micrometers. */
            mx = W32MulDiv(px, 25400, (LONG)dc->Dpi);
            my = W32MulDiv(py, 25400, (LONG)dc->Dpi);
        }
        LONG wx, wy;
        switch (mode) {
        case MM_HIMETRIC:
            wx = virt ? mx * 100 : (mx + 5) / 10;
            wy = virt ? my * 100 : (my + 5) / 10;
            break;
        case MM_LOENGLISH:
            wx = virt ? W32MulDiv(mx, 1000, 254) : (mx + 127) / 254;
            wy = virt ? W32MulDiv(my, 1000, 254) : (my + 127) / 254;
            break;
        case MM_HIENGLISH:
            wx = W32MulDiv(mx, virt ? 10000 : 10, 254);
            wy = W32MulDiv(my, virt ? 10000 : 10, 254);
            break;
        case MM_TWIPS:
            wx = virt ? W32MulDiv(mx, 14400, 254) : W32MulDiv(mx, 144, 2540);
            wy = virt ? W32MulDiv(my, 14400, 254) : W32MulDiv(my, 144, 2540);
            break;
        default: /* MM_LOMETRIC and MM_ISOTROPIC. */
            wx = virt ? mx * 10 : (mx + 50) / 100;
            wy = virt ? my * 10 : (my + 50) / 100;
            break;
        }
        s->WindowExt = (SIZE){wx ? wx : 1, wy ? wy : 1};
        s->ViewportExt = (SIZE){px ? px : 1, py ? -py : -1};
        if (mode == MM_ISOTROPIC)
            isotropic_adjust(s);
    }
    update_page(s);
    return TRUE;
}

/* private. like windows, fixed mapping modes ignore it. */
BOOL NTAPI NtGdiMimeSetExtent(HDC hdc, UINT which, int x, int y, LPSIZE old)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    SIZE prev = {0, 0};
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        SIZE *ext = which == MIME_EXTENT_VIEWPORT ? &s->ViewportExt : &s->WindowExt;
        prev = *ext;
        if (s->MapMode >= MM_ISOTROPIC) {
            if (!x || !y) {
                ok = FALSE;
            } else {
                *ext = (SIZE){x, y};
                if (s->MapMode == MM_ISOTROPIC)
                    isotropic_adjust(s);
                update_page(s);
            }
        }
    }
    UserLeave();
    if (ok && old)
        ok = NT_SUCCESS(MimeCopyToUser(old, &prev, sizeof(prev)));
    return ok;
}

BOOL NTAPI NtGdiSetVirtualResolution(HDC hdc, int px, int py, int mx, int my)
{
    /* setting all four enables it, all four zero clears it. */
    if (px ? (!py || !mx || !my) : (py || mx || my))
        return FALSE;
    UserEnter();
    DC *dc = DcLookup(hdc);
    if (dc) {
        dc->VirtualPixels = (SIZE){px, py};
        dc->VirtualMm = (SIZE){mx, my};
    }
    UserLeave();
    return dc != nullptr;
}

BOOL NTAPI NtGdiSetMiterLimit(HDC hdc, DWORD limit, DWORD *old)
{
    float value, prev = 0;
    memcpy(&value, &limit, sizeof(value));
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc && value >= 1.0f;
    if (dc)
        prev = dc->State.MiterLimit;
    if (ok)
        dc->State.MiterLimit = value;
    UserLeave();
    if (!dc)
        W32SetLastError(ERROR_INVALID_HANDLE);
    else if (!ok)
        W32SetLastError(ERROR_INVALID_PARAMETER);
    if (ok && old)
        ok = NT_SUCCESS(MimeCopyToUser(old, &prev, sizeof(prev)));
    return ok;
}

BOOL NTAPI NtGdiGetMiterLimit(HDC hdc, DWORD *limit)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    float value = dc ? dc->State.MiterLimit : 0;
    UserLeave();
    if (!dc) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return limit && NT_SUCCESS(MimeCopyToUser(limit, &value, sizeof(value)));
}

/* ------------------------------------------------------------------ attributes */

BOOL NTAPI NtGdiGetAndSetDCDword(HDC hdc, UINT index, DWORD value, DWORD *result)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    DWORD old = CLR_INVALID;
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        switch (index) {
        case MIME_DCDW_TEXTCOLOR:
            old = s->TextColor;
            s->TextColor = value & 0xFFFFFF;
            break;
        case MIME_DCDW_BKCOLOR:
            old = s->BkColor;
            s->BkColor = value & 0xFFFFFF;
            break;
        case MIME_DCDW_BKMODE:
            old = (DWORD)s->BkMode;
            if (value == TRANSPARENT || value == OPAQUE)
                s->BkMode = (int)value;
            else
                ok = FALSE, old = 0;
            break;
        case MIME_DCDW_TEXTALIGN:
            old = s->TextAlign;
            s->TextAlign = value;
            break;
        case MIME_DCDW_ROP2:
            old = (DWORD)s->Rop2;
            if (value >= R2_BLACK && value <= R2_WHITE)
                s->Rop2 = (int)value;
            else
                ok = FALSE, old = 0;
            break;
        case MIME_DCDW_BRUSHCOLOR:
            old = s->BrushColor;
            s->BrushColor = value & 0xFFFFFF;
            break;
        case MIME_DCDW_PENCOLOR:
            old = s->PenColor;
            s->PenColor = value & 0xFFFFFF;
            break;
        case MIME_DCDW_STRETCHMODE:
            old = (DWORD)s->StretchMode;
            s->StretchMode = (int)value;
            break;
        case MIME_DCDW_GRAPHICSMODE:
            old = (DWORD)s->GraphicsMode;
            /* leaving the advanced mode needs the identity transform. */
            if ((value != GM_COMPATIBLE && value != GM_ADVANCED) || (value == GM_COMPATIBLE && s->HasXform))
                ok = FALSE, old = 0;
            else
                s->GraphicsMode = (int)value;
            break;
        case MIME_DCDW_POLYFILLMODE:
            old = (DWORD)s->PolyFillMode;
            if (value == ALTERNATE || value == WINDING)
                s->PolyFillMode = (int)value;
            else
                ok = FALSE, old = 0;
            break;
        case MIME_DCDW_ARCDIRECTION:
            old = (DWORD)s->ArcDirection;
            if (value == AD_CLOCKWISE || value == AD_COUNTERCLOCKWISE)
                s->ArcDirection = (int)value;
            else
                ok = FALSE, old = 0;
            break;
        case MIME_DCDW_MAPMODE:
            ok = set_map_mode(dc, (int)value, &old);
            if (!ok)
                old = 0;
            break;
        case MIME_DCDW_TEXTCHAREXTRA:
            old = (DWORD)s->TextCharExtra;
            s->TextCharExtra = (int)value;
            break;
        case MIME_DCDW_LAYOUT:
            /* the orientation flags and LAYOUT_BITMAPORIENTATIONPRESERVED, nothing else. */
            old = s->Layout;
            if (value & ~0xFu)
                ok = FALSE, old = GDI_ERROR;
            else
                s->Layout = value;
            break;
        default:
            ok = FALSE;
            break;
        }
    }
    UserLeave();
    if (result)
        MimeCopyToUser(result, &old, sizeof(old));
    return ok;
}

BOOL NTAPI NtGdiGetDCDword(HDC hdc, UINT index, DWORD *result)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    DWORD v = 0;
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        switch (index) {
        case MIME_DCDW_TEXTCOLOR:
            v = s->TextColor;
            break;
        case MIME_DCDW_BKCOLOR:
            v = s->BkColor;
            break;
        case MIME_DCDW_BKMODE:
            v = (DWORD)s->BkMode;
            break;
        case MIME_DCDW_TEXTALIGN:
            v = s->TextAlign;
            break;
        case MIME_DCDW_ROP2:
            v = (DWORD)s->Rop2;
            break;
        case MIME_DCDW_BRUSHCOLOR:
            v = s->BrushColor;
            break;
        case MIME_DCDW_PENCOLOR:
            v = s->PenColor;
            break;
        case MIME_DCDW_STRETCHMODE:
            v = (DWORD)s->StretchMode;
            break;
        case MIME_DCDW_GRAPHICSMODE:
            v = (DWORD)s->GraphicsMode;
            break;
        case MIME_DCDW_MAPMODE:
            v = (DWORD)s->MapMode;
            break;
        case MIME_DCDW_TEXTCHAREXTRA:
            v = (DWORD)s->TextCharExtra;
            break;
        case MIME_DCDW_LAYOUT:
            v = s->Layout;
            break;
        case MIME_DCDW_POLYFILLMODE:
            v = (DWORD)s->PolyFillMode;
            break;
        case MIME_DCDW_ARCDIRECTION:
            v = (DWORD)s->ArcDirection;
            break;
        case MIME_DCDW_PIXELFORMAT:
            v = (DWORD)(dc->Window ? dc->Window->PixelFormat : dc->PixelFormat);
            break;
        case MIME_DCDW_PATHSTATE:
            v = dc->PathState;
            break;
        case MIME_DCDW_WINDOW:
            v = (DWORD)(ULONG_PTR)WndHandle(dc->Window);
            break;
        default:
            ok = FALSE;
            break;
        }
    }
    UserLeave();
    if (ok && result)
        ok = NT_SUCCESS(MimeCopyToUser(result, &v, sizeof(v)));
    return ok;
}

BOOL NTAPI NtGdiGetDCPoint(HDC hdc, UINT index, POINT *point)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    POINT p = {0, 0};
    BOOL ok = dc != nullptr;
    if (dc) {
        switch (index) {
        case MIME_DCPT_VIEWPORTORG:
            p = dc->State.ViewportOrg;
            break;
        case MIME_DCPT_WINDOWORG:
            p = dc->State.WindowOrg;
            break;
        case MIME_DCPT_CURRENTPOSITION:
            p = dc->State.CurrentPos;
            break;
        case MIME_DCPT_WINDOWEXT:
            p = (POINT){dc->State.WindowExt.cx, dc->State.WindowExt.cy};
            break;
        case MIME_DCPT_VIEWPORTEXT:
            p = (POINT){dc->State.ViewportExt.cx, dc->State.ViewportExt.cy};
            break;
        case MIME_DCPT_DCORG:
            p = dc->Origin;
            if (dc->Window && dc->Surface && dc->Surface->Window) {
                p.x += dc->Surface->Window->WindowRect.left;
                p.y += dc->Surface->Window->WindowRect.top;
            }
            break;
        default:
            ok = FALSE;
        }
    }
    UserLeave();
    return ok && NT_SUCCESS(MimeCopyToUser(point, &p, sizeof(p)));
}

BOOL NTAPI NtGdiTransformPoints(HDC hdc, POINT *in, POINT *out, int count, int mode)
{
    if (count < 0 || count > 65536)
        return FALSE;
    POINT local[64];
    POINT *pts = count <= 64 ? local : W32Alloc((SIZE_T)count * sizeof(POINT));
    if (!pts || !NT_SUCCESS(MimeCopyFromUser(pts, in, (SIZE_T)count * sizeof(POINT)))) {
        if (pts != local)
            W32Free(pts);
        return FALSE;
    }
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc != nullptr;
    if (dc) {
        for (int i = 0; i < count; i++) {
            if (mode == 1) /* logical to device. */
                DcLogicalToDevice(dc, &pts[i].x, &pts[i].y);
            else
                DcDeviceToLogical(dc, &pts[i].x, &pts[i].y);
        }
    }
    UserLeave();
    ok = ok && NT_SUCCESS(MimeCopyToUser(out, pts, (SIZE_T)count * sizeof(POINT)));
    if (pts != local)
        W32Free(pts);
    return ok;
}

/* private: NtGdiSetViewportOrgEx and friends share one service through the window org slot. */
BOOL NTAPI NtGdiSetViewportOrgEx(HDC hdc, int x, int y, LPPOINT old)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    POINT prev = {0, 0};
    if (dc) {
        prev = dc->State.ViewportOrg;
        dc->State.ViewportOrg = (POINT){x, y};
    }
    UserLeave();
    if (dc && old)
        MimeCopyToUser(old, &prev, sizeof(prev));
    return dc != nullptr;
}

BOOL NTAPI NtGdiSetWindowOrgEx(HDC hdc, int x, int y, LPPOINT old)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    POINT prev = {0, 0};
    if (dc) {
        prev = dc->State.WindowOrg;
        dc->State.WindowOrg = (POINT){x, y};
    }
    UserLeave();
    if (dc && old)
        MimeCopyToUser(old, &prev, sizeof(prev));
    return dc != nullptr;
}

BOOL NTAPI NtGdiMoveTo(HDC hdc, int x, int y, POINT *old)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    POINT prev = {0, 0};
    if (dc && DcIsPathOpen(dc))
        PathMoveTo(dc, x, y);
    if (dc) {
        prev = dc->State.CurrentPos;
        dc->State.CurrentPos = (POINT){x, y};
    }
    UserLeave();
    if (dc && old)
        MimeCopyToUser(old, &prev, sizeof(prev));
    return dc != nullptr;
}

/* ------------------------------------------------------------------ selection */

HANDLE NTAPI NtGdiGetDCObject(HDC hdc, int type)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    HANDLE h = nullptr;
    if (dc) {
        switch (type) {
        case OBJ_BRUSH:
            h = dc->State.Brush->Header.Handle;
            break;
        case OBJ_PEN:
            h = dc->State.Pen->Header.Handle;
            break;
        case OBJ_FONT:
            h = dc->State.Font->Header.Handle;
            break;
        case OBJ_BITMAP:
            if (dc->Surface && dc->Surface == dc->DefaultBitmap && (dc->Flags & DC_MEMORY)) {
                /* a memory dc holds the stock bitmap until another is selected. */
                if (!dc->DefaultBitmap->Header.Handle)
                    GdiCreateHandle(&dc->DefaultBitmap->Header, GDI_TYPE_BITMAP);
                dc->DefaultBitmap->Header.Flags |= GDI_FLAG_SYSTEM;
            }
            h = dc->Surface && dc->Surface->Header.Handle ? dc->Surface->Header.Handle : nullptr;
            break;
        }
    }
    UserLeave();
    return h;
}

HBITMAP NTAPI NtGdiSelectBitmap(HDC hdc, HBITMAP bitmap)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    SURFACE *s = SurfaceLookup(bitmap);
    HBITMAP old = nullptr;
    if (dc && s && (dc->Flags & DC_MEMORY)) {
        /* a bitmap can be selected into one dc at a time. */
        if (s->Header.SelectCount && dc->Surface != s) {
            UserLeave();
            return nullptr;
        }
        old = dc->Surface && dc->Surface != dc->DefaultBitmap ? (HBITMAP)dc->Surface->Header.Handle : nullptr;
        if (!old && dc->DefaultBitmap) {
            /* windows hands back a stock 1x1 bitmap; the default one stands in for it. */
            if (!dc->DefaultBitmap->Header.Handle)
                GdiCreateHandle(&dc->DefaultBitmap->Header, GDI_TYPE_BITMAP);
            dc->DefaultBitmap->Header.Flags |= GDI_FLAG_SYSTEM;
            old = (HBITMAP)dc->DefaultBitmap->Header.Handle;
        }
        DcSetSurface(dc, s);
    }
    UserLeave();
    return old;
}

HBRUSH NTAPI NtGdiSelectBrush(HDC hdc, HBRUSH brush)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    BRUSH *b = (BRUSH *)GdiLookup(brush, GDI_TYPE_BRUSH);
    HBRUSH old = nullptr;
    if (dc && b) {
        old = (HBRUSH)dc->State.Brush->Header.Handle;
        select_unref(&dc->State.Brush->Header);
        dc->State.Brush = b;
        select_ref(&b->Header);
    }
    UserLeave();
    return old;
}

HPEN NTAPI NtGdiSelectPen(HDC hdc, HPEN pen)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    PEN *p = (PEN *)GdiLookup(pen, GDI_TYPE_PEN);
    if (!p)
        p = (PEN *)GdiLookup(pen, GDI_TYPE_EXTPEN);
    HPEN old = nullptr;
    if (dc && p) {
        old = (HPEN)dc->State.Pen->Header.Handle;
        select_unref(&dc->State.Pen->Header);
        dc->State.Pen = p;
        select_ref(&p->Header);
    }
    UserLeave();
    return old;
}

HFONT NTAPI NtGdiSelectFont(HDC hdc, HFONT font)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    FONT *f = (FONT *)GdiLookup(font, GDI_TYPE_FONT);
    HFONT old = nullptr;
    if (dc && f) {
        old = (HFONT)dc->State.Font->Header.Handle;
        select_unref(&dc->State.Font->Header);
        dc->State.Font = f;
        dc->Realized = nullptr;
        dc->RealizedFor = nullptr;
        select_ref(&f->Header);
    }
    UserLeave();
    return old;
}

/* ------------------------------------------------------------------ saved states */

int NTAPI NtGdiSaveDC(HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int level = 0;
    if (dc) {
        DC_SAVED *sv = W32Alloc(sizeof(DC_SAVED));
        if (sv) {
            sv->State = dc->State;
            RgnInit(&sv->State.ClipRgn);
            RgnCopy(&sv->State.ClipRgn, &dc->State.ClipRgn);
            RgnInit(&sv->State.MetaRgn);
            RgnCopy(&sv->State.MetaRgn, &dc->State.MetaRgn);
            sv->Surface = dc->Surface;
            select_ref(&sv->State.Brush->Header);
            select_ref(&sv->State.Pen->Header);
            select_ref(&sv->State.Font->Header);
            sv->Next = dc->Saved;
            dc->Saved = sv;
            level = ++dc->SaveLevel;
        }
    }
    UserLeave();
    return level;
}

BOOL NTAPI NtGdiRestoreDC(HDC hdc, int saved)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = FALSE;
    if (dc) {
        int target = saved < 0 ? dc->SaveLevel + saved + 1 : saved;
        if (target >= 1 && target <= dc->SaveLevel) {
            while (dc->SaveLevel >= target) {
                DC_SAVED *sv = dc->Saved;
                dc->Saved = sv->Next;
                dc->SaveLevel--;
                if (dc->SaveLevel + 1 == target) {
                    select_unref(&dc->State.Brush->Header);
                    select_unref(&dc->State.Pen->Header);
                    select_unref(&dc->State.Font->Header);
                    RgnFree(&dc->State.ClipRgn);
                    RgnFree(&dc->State.MetaRgn);
                    dc->State = sv->State;
                    if (dc->Flags & DC_MEMORY)
                        DcSetSurface(dc, sv->Surface);
                } else {
                    select_unref(&sv->State.Brush->Header);
                    select_unref(&sv->State.Pen->Header);
                    select_unref(&sv->State.Font->Header);
                    RgnFree(&sv->State.ClipRgn);
                    RgnFree(&sv->State.MetaRgn);
                }
                W32Free(sv);
            }
            ok = TRUE;
        }
    }
    UserLeave();
    return ok;
}

/* ------------------------------------------------------------------ clipping */

/* the application clip region is kept in device coordinates. */
int NTAPI NtGdiExtSelectClipRgn(HDC hdc, HRGN region, int mode)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int result = ERROR;
    if (dc) {
        GDI_REGION *r = region ? (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION) : nullptr;
        if (!r && mode == RGN_COPY) {
            RgnClear(&dc->State.ClipRgn);
            dc->State.HasClip = FALSE;
            result = SIMPLEREGION;
        } else if (r) {
            if (!dc->State.HasClip || mode == RGN_COPY) {
                if (mode == RGN_COPY || mode == RGN_AND || mode == RGN_OR) {
                    RgnCopy(&dc->State.ClipRgn, &r->Region);
                } else {
                    /* combining with "no clip" means combining with the whole surface. */
                    RECT all = {-0x4000000, -0x4000000, 0x4000000, 0x4000000};
                    REGION full;
                    RgnInit(&full);
                    RgnSetRect(&full, &all);
                    RgnCombine(&dc->State.ClipRgn, &full, &r->Region, mode);
                    RgnFree(&full);
                }
            } else {
                RgnCombine(&dc->State.ClipRgn, &dc->State.ClipRgn, &r->Region, mode);
            }
            dc->State.HasClip = TRUE;
            result = RgnComplexity(&dc->State.ClipRgn);
        }
    }
    UserLeave();
    return result;
}

/* a metafile dc has no surface: its visible region is the reference device. */
static void metafile_clip(DC *dc, REGION *out)
{
    RgnCopy(out, &dc->VisRgn);
    if (dc->State.HasClip)
        RgnCombine(out, out, &dc->State.ClipRgn, RGN_AND);
    if (dc->State.HasMeta)
        RgnCombine(out, out, &dc->State.MetaRgn, RGN_AND);
}

int NTAPI NtGdiGetAppClipBox(HDC hdc, LPRECT rect)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    RECT box = {0, 0, 0, 0};
    int result = ERROR;
    if (dc) {
        REGION clip;
        RgnInit(&clip);
        if (dc->Flags & DC_METAFILE)
            metafile_clip(dc, &clip);
        else
            DcGetClip(dc, &clip);
        box = clip.Extents;
        result = RgnComplexity(&clip);
        RgnFree(&clip);
        /* surface to logical coordinates, the corners through the inverse transform. */
        RectOffset(&box, -dc->Origin.x, -dc->Origin.y);
        if (result != NULLREGION) {
            LONG xs[4] = {box.left, box.right, box.left, box.right}, ys[4] = {box.top, box.top, box.bottom, box.bottom};
            for (int i = 0; i < 4; i++) {
                DcDeviceToLogical(dc, &xs[i], &ys[i]);
                if (!i || xs[i] < box.left)
                    box.left = xs[i];
                if (!i || xs[i] > box.right)
                    box.right = xs[i];
                if (!i || ys[i] < box.top)
                    box.top = ys[i];
                if (!i || ys[i] > box.bottom)
                    box.bottom = ys[i];
            }
        }
    }
    UserLeave();
    if (result != ERROR && !NT_SUCCESS(MimeCopyToUser(rect, &box, sizeof(box))))
        return ERROR;
    return result;
}

int NTAPI NtGdiGetRandomRgn(HDC hdc, HRGN region, int which)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    GDI_REGION *r = (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION);
    int result = -1;
    if (dc && r) {
        if (which == 1) { /* CLIPRGN */
            result = dc->State.HasClip ? 1 : 0;
            if (result)
                RgnCopy(&r->Region, &dc->State.ClipRgn);
        } else if (which == 2) { /* METARGN */
            result = dc->State.HasMeta ? 1 : 0;
            if (result)
                RgnCopy(&r->Region, &dc->State.MetaRgn);
        } else if (which == 3) { /* APIRGN: the meta and clip regions together. */
            result = dc->State.HasMeta || dc->State.HasClip ? 1 : 0;
            if (dc->State.HasMeta && dc->State.HasClip)
                RgnCombine(&r->Region, &dc->State.MetaRgn, &dc->State.ClipRgn, RGN_AND);
            else if (dc->State.HasMeta)
                RgnCopy(&r->Region, &dc->State.MetaRgn);
            else if (dc->State.HasClip)
                RgnCopy(&r->Region, &dc->State.ClipRgn);
        } else if (which == 4) { /* SYSRGN, in screen coordinates. */
            RgnCopy(&r->Region, &dc->VisRgn);
            if (dc->Surface && dc->Surface->Window)
                RgnOffset(&r->Region, dc->Surface->Window->WindowRect.left, dc->Surface->Window->WindowRect.top);
            result = 1;
        } else {
            result = 0;
        }
    }
    UserLeave();
    return result;
}

static int clip_rect_op(HDC hdc, int left, int top, int right, int bottom, int mode)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int result = ERROR;
    if (dc) {
        LONG x0 = left, y0 = top, x1 = right, y1 = bottom;
        DcLogicalToDevice(dc, &x0, &y0);
        DcLogicalToDevice(dc, &x1, &y1);
        RECT r = {W32Min(x0, x1), W32Min(y0, y1), W32Max(x0, x1), W32Max(y0, y1)};
        if (!dc->State.HasClip) {
            RECT all = {-0x4000000, -0x4000000, 0x4000000, 0x4000000};
            RgnSetRect(&dc->State.ClipRgn, &all);
            dc->State.HasClip = TRUE;
        }
        RgnCombineRect(&dc->State.ClipRgn, &dc->State.ClipRgn, &r, mode);
        result = RgnComplexity(&dc->State.ClipRgn);
    }
    UserLeave();
    return result;
}

int NTAPI NtGdiIntersectClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    return clip_rect_op(hdc, left, top, right, bottom, RGN_AND);
}

int NTAPI NtGdiExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    return clip_rect_op(hdc, left, top, right, bottom, RGN_DIFF);
}

/* the clip region joins the meta region and the dc has no clip region left. */
int NTAPI NtGdiSetMetaRgn(HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int result = ERROR;
    if (dc) {
        DC_STATE *s = &dc->State;
        if (s->HasClip) {
            if (s->HasMeta)
                RgnCombine(&s->MetaRgn, &s->MetaRgn, &s->ClipRgn, RGN_AND);
            else
                RgnCopy(&s->MetaRgn, &s->ClipRgn);
            s->HasMeta = TRUE;
            RgnClear(&s->ClipRgn);
            s->HasClip = FALSE;
        }
        result = s->HasMeta ? RgnComplexity(&s->MetaRgn) : SIMPLEREGION;
    }
    UserLeave();
    return result;
}

BOOL NTAPI NtGdiFlush(void)
{
    return TRUE;
}

/* ------------------------------------------------------------------ world transform */

static void xform_multiply(XFORM *out, const XFORM *a, const XFORM *b)
{
    XFORM r;
    r.eM11 = a->eM11 * b->eM11 + a->eM12 * b->eM21;
    r.eM12 = a->eM11 * b->eM12 + a->eM12 * b->eM22;
    r.eM21 = a->eM21 * b->eM11 + a->eM22 * b->eM21;
    r.eM22 = a->eM21 * b->eM12 + a->eM22 * b->eM22;
    r.eDx = a->eDx * b->eM11 + a->eDy * b->eM21 + b->eDx;
    r.eDy = a->eDx * b->eM12 + a->eDy * b->eM22 + b->eDy;
    *out = r;
}

static BOOLEAN xform_identity(const XFORM *m)
{
    return m->eM11 == 1 && m->eM12 == 0 && m->eM21 == 0 && m->eM22 == 1 && m->eDx == 0 && m->eDy == 0;
}

BOOL NTAPI NtGdiModifyWorldTransform(HDC hdc, const XFORM *user_xform, DWORD mode)
{
    XFORM xf = {1, 0, 0, 1, 0, 0};
    if (mode != MWT_IDENTITY && (!user_xform || !NT_SUCCESS(MimeCopyFromUser(&xf, user_xform, sizeof(xf))))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc && dc->State.GraphicsMode == GM_ADVANCED;
    if (ok) {
        XFORM result = dc->State.World;
        switch (mode) {
        case MWT_IDENTITY:
            result = (XFORM){1, 0, 0, 1, 0, 0};
            break;
        case MWT_SET:
            result = xf;
            break;
        case MWT_LEFTMULTIPLY:
            xform_multiply(&result, &xf, &dc->State.World);
            break;
        case MWT_RIGHTMULTIPLY:
            xform_multiply(&result, &dc->State.World, &xf);
            break;
        default:
            ok = FALSE;
            break;
        }
        /* a degenerate transform is refused. */
        if (ok && result.eM11 * result.eM22 - result.eM12 * result.eM21 == 0)
            ok = FALSE;
        if (ok) {
            dc->State.World = result;
            dc->State.HasXform = !xform_identity(&result);
        }
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtGdiGetTransform(HDC hdc, DWORD which, XFORM *user_xform)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    XFORM xf = {1, 0, 0, 1, 0, 0};
    BOOL ok = dc != nullptr;
    if (dc) {
        switch (which) {
        case MIME_XFORM_WORLD:
            xf = dc->State.World;
            break;
        case MIME_XFORM_WORLD_TO_DEVICE:
            DcGetXform(dc, &xf);
            break;
        case MIME_XFORM_PAGE_TO_DEVICE: {
            double sx, sy;
            page_scale(dc, &sx, &sy);
            xf = (XFORM){(float)sx, 0, 0, (float)sy, (float)(dc->State.ViewportOrg.x - dc->State.WindowOrg.x * sx),
                         (float)(dc->State.ViewportOrg.y - dc->State.WindowOrg.y * sy)};
            break;
        }
        case MIME_XFORM_DEVICE_TO_WORLD: {
            XFORM m;
            DcGetXform(dc, &m);
            double det = (double)m.eM11 * m.eM22 - (double)m.eM12 * m.eM21;
            if (det == 0) {
                ok = FALSE;
                break;
            }
            xf.eM11 = (float)(m.eM22 / det);
            xf.eM12 = (float)(-m.eM12 / det);
            xf.eM21 = (float)(-m.eM21 / det);
            xf.eM22 = (float)(m.eM11 / det);
            xf.eDx = (float)(((double)m.eM21 * m.eDy - (double)m.eM22 * m.eDx) / det);
            xf.eDy = (float)(((double)m.eM12 * m.eDx - (double)m.eM11 * m.eDy) / det);
            break;
        }
        default:
            ok = FALSE;
            break;
        }
    }
    UserLeave();
    return ok && NT_SUCCESS(MimeCopyToUser(user_xform, &xf, sizeof(xf)));
}

/* ------------------------------------------------------------------ pixel formats */

/* the format is a property of the window, set once; memory dcs keep their own. */
BOOL NTAPI NtGdiSetPixelFormat(HDC hdc, int format)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    ULONG error = 0;
    if (!dc) {
        error = ERROR_INVALID_HANDLE;
    } else if (format <= 0) {
        error = ERROR_INVALID_PIXEL_FORMAT;
    } else {
        int *slot = dc->Window ? &dc->Window->PixelFormat : &dc->PixelFormat;
        if (*slot && *slot != format)
            error = ERROR_INVALID_PIXEL_FORMAT;
        else
            *slot = format;
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return !error;
}
