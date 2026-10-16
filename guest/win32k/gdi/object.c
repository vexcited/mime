/*
 * gdi object handles, stock objects, brushes, pens, logical fonts and
 * region objects.
 *
 * handles look like the windows ones: the low 16 bits index the handle
 * table, bits 16..23 hold the object type and bits 24..31 a reuse counter,
 * so gdi32 can tell the type of a handle without a system call.
 */
#include "../win32k.h"

#define GDI_MAX_HANDLES 16384

typedef struct GDI_ENTRY {
    GDI_OBJECT *Object;
    UCHAR Unique;
} GDI_ENTRY;

static GDI_ENTRY *g_table;
static ULONG g_next_free = 1;
static GDI_OBJECT *g_stock[STOCK_LAST + 1];

static HANDLE make_handle(ULONG index, UCHAR type, UCHAR unique)
{
    return (HANDLE)(ULONG_PTR)(((ULONG)unique << 24) | ((ULONG)type << 16) | index);
}

UCHAR GdiHandleType(HANDLE handle)
{
    return (UCHAR)(((ULONG_PTR)handle >> 16) & 0x7F);
}

HANDLE GdiCreateHandle(GDI_OBJECT *object, UCHAR type)
{
    if (!g_table) {
        g_table = W32Alloc(GDI_MAX_HANDLES * sizeof(GDI_ENTRY));
        if (!g_table)
            return nullptr;
    }
    for (ULONG n = 0; n < GDI_MAX_HANDLES; n++) {
        ULONG i = g_next_free;
        g_next_free = g_next_free + 1 >= GDI_MAX_HANDLES ? 1 : g_next_free + 1;
        if (!g_table[i].Object) {
            g_table[i].Object = object;
            g_table[i].Unique++;
            object->Type = type;
            object->Handle = make_handle(i, type, g_table[i].Unique);
            object->OwnerPid = PsGetCurrentProcessId();
            return object->Handle;
        }
    }
    W32Trace("win32k: gdi handle table full\n");
    return nullptr;
}

void GdiFreeHandle(GDI_OBJECT *object)
{
    ULONG i = (ULONG)(ULONG_PTR)object->Handle & 0xFFFF;
    if (i && i < GDI_MAX_HANDLES && g_table[i].Object == object)
        g_table[i].Object = nullptr;
    object->Handle = nullptr;
}

GDI_OBJECT *GdiLookup(HANDLE handle, UCHAR type)
{
    ULONG_PTR v = (ULONG_PTR)handle;
    ULONG i = (ULONG)(v & 0xFFFF);
    if (!g_table || !i || i >= GDI_MAX_HANDLES)
        return nullptr;
    GDI_OBJECT *o = g_table[i].Object;
    if (!o || o->Handle != handle)
        return nullptr;
    if (type && o->Type != type)
        return nullptr;
    return o;
}

static void destroy_object(GDI_OBJECT *o)
{
    switch (o->Type) {
    case GDI_TYPE_BITMAP:
        SurfaceDestroy((SURFACE *)o);
        return;
    case GDI_TYPE_REGION:
        RgnFree(&((GDI_REGION *)o)->Region);
        break;
    case GDI_TYPE_BRUSH:
        if (((BRUSH *)o)->Pattern)
            SurfaceDestroy(((BRUSH *)o)->Pattern);
        break;
    case GDI_TYPE_DC:
        DcFree((DC *)o);
        return;
    }
    GdiFreeHandle(o);
    W32Free(o);
}

/* objects selected into a dc are destroyed when the dc lets go of them. */
void GdiDeleteObject(GDI_OBJECT *o)
{
    if (o->Flags & (GDI_FLAG_STOCK | GDI_FLAG_SYSTEM))
        return;
    if (o->SelectCount) {
        o->Flags |= GDI_FLAG_DELETE_PENDING;
        return;
    }
    destroy_object(o);
}

void GdiReleaseProcessObjects(HANDLE pid)
{
    if (!g_table)
        return;
    /* dcs first, so the objects they select become free. */
    for (int pass = 0; pass < 2; pass++) {
        for (ULONG i = 1; i < GDI_MAX_HANDLES; i++) {
            GDI_OBJECT *o = g_table[i].Object;
            if (!o || o->OwnerPid != pid || (o->Flags & (GDI_FLAG_STOCK | GDI_FLAG_SYSTEM)))
                continue;
            if ((pass == 0) != (o->Type == GDI_TYPE_DC))
                continue;
            o->SelectCount = 0;
            destroy_object(o);
        }
    }
}

/* ------------------------------------------------------------------ objects */

static BRUSH *new_brush(UINT style, COLORREF color, ULONG_PTR hatch)
{
    BRUSH *b = W32Alloc(sizeof(BRUSH));
    if (!b)
        return nullptr;
    b->Style = style;
    b->Color = color & 0xFFFFFF;
    b->Hatch = hatch;
    b->SysColor = -1;
    if (!GdiCreateHandle(&b->Header, GDI_TYPE_BRUSH)) {
        W32Free(b);
        return nullptr;
    }
    return b;
}

BRUSH *GdiCreateSystemBrush(COLORREF color, int sys_color)
{
    BRUSH *b = new_brush(BS_SOLID, color, 0);
    if (b) {
        b->Header.Flags |= GDI_FLAG_SYSTEM;
        b->SysColor = sys_color;
    }
    return b;
}

static PEN *new_pen(UINT style, LONG width, COLORREF color)
{
    PEN *p = W32Alloc(sizeof(PEN));
    if (!p)
        return nullptr;
    p->Style = style;
    p->Width = width;
    p->Color = color & 0xFFFFFF;
    if (!GdiCreateHandle(&p->Header, GDI_TYPE_PEN)) {
        W32Free(p);
        return nullptr;
    }
    return p;
}

static FONT *new_font(const LOGFONTW *lf)
{
    FONT *f = W32Alloc(sizeof(FONT));
    if (!f)
        return nullptr;
    f->Logical = *lf;
    f->Logical.lfFaceName[LF_FACESIZE - 1] = 0;
    if (!GdiCreateHandle(&f->Header, GDI_TYPE_FONT)) {
        W32Free(f);
        return nullptr;
    }
    return f;
}

GDI_REGION *GdiCreateRegion(void)
{
    GDI_REGION *r = W32Alloc(sizeof(GDI_REGION));
    if (!r)
        return nullptr;
    RgnInit(&r->Region);
    if (!GdiCreateHandle(&r->Header, GDI_TYPE_REGION)) {
        W32Free(r);
        return nullptr;
    }
    return r;
}

static void make_stock(int index, GDI_OBJECT *o)
{
    if (!o)
        return;
    o->Flags |= GDI_FLAG_STOCK;
    g_stock[index] = o;
}

static LOGFONTW make_logfont(LONG height, LONG weight, PCWSTR face)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    wcscpy(lf.lfFaceName, face);
    return lf;
}

void GdiInitializeStockObjects(void)
{
    make_stock(WHITE_BRUSH, &new_brush(BS_SOLID, RGB(255, 255, 255), 0)->Header);
    make_stock(LTGRAY_BRUSH, &new_brush(BS_SOLID, RGB(192, 192, 192), 0)->Header);
    make_stock(GRAY_BRUSH, &new_brush(BS_SOLID, RGB(128, 128, 128), 0)->Header);
    make_stock(DKGRAY_BRUSH, &new_brush(BS_SOLID, RGB(64, 64, 64), 0)->Header);
    make_stock(BLACK_BRUSH, &new_brush(BS_SOLID, RGB(0, 0, 0), 0)->Header);
    make_stock(NULL_BRUSH, &new_brush(BS_NULL, 0, 0)->Header);
    make_stock(WHITE_PEN, &new_pen(PS_SOLID, 0, RGB(255, 255, 255))->Header);
    make_stock(BLACK_PEN, &new_pen(PS_SOLID, 0, RGB(0, 0, 0))->Header);
    make_stock(NULL_PEN, &new_pen(PS_NULL, 0, 0)->Header);
    make_stock(DC_BRUSH, &new_brush(BS_SOLID, RGB(255, 255, 255), 0)->Header);
    make_stock(DC_PEN, &new_pen(PS_SOLID, 0, RGB(0, 0, 0))->Header);
    UINT dpi = W32SystemDpi();
    /* bitmap fonts are not supported: the legacy stock fonts map to outline faces. */
    /* the System face is a bitmap font with a 16 pixel cell at 96 dpi. */
    LOGFONTW lf = make_logfont(16 * (LONG)dpi / 96, FW_BOLD, L"Segoe UI");
    make_stock(SYSTEM_FONT, &new_font(&lf)->Header);
    make_stock(DEVICE_DEFAULT_FONT, &new_font(&lf)->Header);
    lf = make_logfont(-13 * (LONG)dpi / 96, FW_NORMAL, L"Courier New");
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    make_stock(OEM_FIXED_FONT, &new_font(&lf)->Header);
    make_stock(ANSI_FIXED_FONT, &new_font(&lf)->Header);
    make_stock(SYSTEM_FIXED_FONT, &new_font(&lf)->Header);
    lf = make_logfont(-W32MulDiv(8, (LONG)dpi, 72), FW_NORMAL, L"Microsoft Sans Serif");
    make_stock(ANSI_VAR_FONT, &new_font(&lf)->Header);
    make_stock(DEFAULT_GUI_FONT, &new_font(&lf)->Header);
}

GDI_OBJECT *GdiStockObject(int index)
{
    return index >= 0 && index <= STOCK_LAST ? g_stock[index] : nullptr;
}

HANDLE GdiStockHandle(int index)
{
    GDI_OBJECT *o = GdiStockObject(index);
    return o ? o->Handle : nullptr;
}

/* ------------------------------------------------------------------ services */

HANDLE NTAPI NtGdiGetStockObject(int index)
{
    UserEnter();
    GDI_OBJECT *o = GdiStockObject(index);
    UserLeave();
    return o ? o->Handle : nullptr;
}

BOOL NTAPI NtGdiDeleteObjectApp(HANDLE handle)
{
    UserEnter();
    GDI_OBJECT *o = GdiLookup(handle, 0);
    BOOL ok = FALSE;
    if (o && !(o->Flags & GDI_FLAG_STOCK)) {
        GdiDeleteObject(o);
        ok = TRUE;
    } else if (o) {
        ok = TRUE; /* deleting stock objects is a harmless no-op on windows */
    }
    UserLeave();
    return ok;
}

HBRUSH NTAPI NtGdiCreateSolidBrush(COLORREF color, HBRUSH reuse)
{
    UserEnter();
    BRUSH *b = new_brush(BS_SOLID, color, 0);
    UserLeave();
    return b ? (HBRUSH)b->Header.Handle : nullptr;
}

HPEN NTAPI NtGdiCreatePen(int style, int width, COLORREF color, HBRUSH reuse)
{
    UserEnter();
    PEN *p = new_pen((UINT)style & PS_STYLE_MASK, width < 0 ? 0 : width, color);
    UserLeave();
    return p ? (HPEN)p->Header.Handle : nullptr;
}

HFONT NTAPI NtGdiHfontCreate(const LOGFONTW *font, ULONG size, ULONG type, ULONG flags, PVOID reserved)
{
    LOGFONTW lf;
    if (size < sizeof(LOGFONTW) || !NT_SUCCESS(MimeCopyFromUser(&lf, font, sizeof(lf))))
        return nullptr;
    UserEnter();
    FONT *f = new_font(&lf);
    UserLeave();
    return f ? (HFONT)f->Header.Handle : nullptr;
}

int NTAPI NtGdiExtGetObjectW(HANDLE handle, int size, PVOID buffer)
{
    UserEnter();
    GDI_OBJECT *o = GdiLookup(handle, 0);
    union {
        LOGBRUSH brush;
        LOGPEN pen;
        LOGFONTW font;
        DIBSECTION dib;
    } out;
    int needed = 0;
    memset(&out, 0, sizeof(out));
    if (o) {
        switch (o->Type) {
        case GDI_TYPE_BRUSH: {
            BRUSH *b = (BRUSH *)o;
            out.brush.lbStyle = b->Style;
            out.brush.lbColor = b->SysColor >= 0 ? MetricsSysColor(b->SysColor) : b->Color;
            out.brush.lbHatch = b->Hatch;
            needed = sizeof(LOGBRUSH);
            break;
        }
        case GDI_TYPE_PEN: {
            PEN *p = (PEN *)o;
            out.pen.lopnStyle = p->Style;
            out.pen.lopnWidth.x = p->Width;
            out.pen.lopnColor = p->Color;
            needed = sizeof(LOGPEN);
            break;
        }
        case GDI_TYPE_FONT:
            out.font = ((FONT *)o)->Logical;
            needed = sizeof(LOGFONTW);
            break;
        case GDI_TYPE_BITMAP: {
            SURFACE *s = (SURFACE *)o;
            out.dib.dsBm.bmWidth = s->Width;
            out.dib.dsBm.bmHeight = s->Height;
            out.dib.dsBm.bmWidthBytes = ((s->Width * s->Bpp + 15) / 16) * 2;
            out.dib.dsBm.bmPlanes = 1;
            out.dib.dsBm.bmBitsPixel = s->Bpp;
            needed = sizeof(BITMAP);
            if (s->UserBits) {
                out.dib.dsBm.bmBits = s->UserBits;
                out.dib.dsBm.bmWidthBytes = s->Stride;
                out.dib.dsBmih = s->Info;
                memcpy(out.dib.dsBitfields, s->Masks, sizeof(s->Masks));
                if (size >= (int)sizeof(DIBSECTION))
                    needed = sizeof(DIBSECTION);
            }
            break;
        }
        case GDI_TYPE_REGION:
            needed = 0;
            break;
        }
    }
    UserLeave();
    if (!buffer)
        return needed;
    int n = size < needed ? size : needed;
    if (n > 0 && !NT_SUCCESS(MimeCopyToUser(buffer, &out, (SIZE_T)n)))
        return 0;
    return n;
}

/* ------------------------------------------------------------------ region services */

static GDI_REGION *region_of(HRGN h)
{
    return (GDI_REGION *)GdiLookup(h, GDI_TYPE_REGION);
}

HRGN NTAPI NtGdiCreateRectRgn(int left, int top, int right, int bottom)
{
    UserEnter();
    GDI_REGION *r = GdiCreateRegion();
    if (r) {
        RECT rc = {W32Min(left, right), W32Min(top, bottom), W32Max(left, right), W32Max(top, bottom)};
        RgnSetRect(&r->Region, &rc);
    }
    UserLeave();
    return r ? (HRGN)r->Header.Handle : nullptr;
}

HRGN NTAPI NtGdiCreateRoundRectRgn(int left, int top, int right, int bottom, int width, int height)
{
    UserEnter();
    GDI_REGION *r = GdiCreateRegion();
    if (r)
        RgnRoundRect(&r->Region, left, top, right, bottom, width, height);
    UserLeave();
    return r ? (HRGN)r->Header.Handle : nullptr;
}

HRGN NTAPI NtGdiExtCreateRegion(const XFORM *xform, DWORD size, const RGNDATA *data)
{
    RGNDATAHEADER header;
    if (size < sizeof(header) || !NT_SUCCESS(MimeCopyFromUser(&header, data, sizeof(header))))
        return nullptr;
    if (header.nCount > 100000 || size < sizeof(header) + header.nCount * sizeof(RECT))
        return nullptr;
    RECT *rects = W32Alloc(header.nCount * sizeof(RECT) + 1);
    if (!rects)
        return nullptr;
    HRGN result = nullptr;
    if (NT_SUCCESS(MimeCopyFromUser(rects, (const UCHAR *)data + header.dwSize, header.nCount * sizeof(RECT)))) {
        UserEnter();
        GDI_REGION *r = GdiCreateRegion();
        if (r) {
            RgnFromRects(&r->Region, rects, header.nCount);
            result = (HRGN)r->Header.Handle;
        }
        UserLeave();
    }
    W32Free(rects);
    return result;
}

int NTAPI NtGdiCombineRgn(HRGN dst, HRGN a, HRGN b, int mode)
{
    UserEnter();
    GDI_REGION *d = region_of(dst), *ra = region_of(a), *rb = mode == RGN_COPY ? nullptr : region_of(b);
    int result = ERROR;
    if (d && ra && (mode == RGN_COPY || rb) && mode >= RGN_AND && mode <= RGN_COPY) {
        REGION empty;
        RgnInit(&empty);
        if (RgnCombine(&d->Region, &ra->Region, rb ? &rb->Region : &empty, mode))
            result = RgnComplexity(&d->Region);
    }
    UserLeave();
    return result;
}

BOOL NTAPI NtGdiSetRectRgn(HRGN region, int left, int top, int right, int bottom)
{
    UserEnter();
    GDI_REGION *r = region_of(region);
    RECT rc = {W32Min(left, right), W32Min(top, bottom), W32Max(left, right), W32Max(top, bottom)};
    BOOL ok = r && RgnSetRect(&r->Region, &rc);
    UserLeave();
    return ok;
}

int NTAPI NtGdiOffsetRgn(HRGN region, int x, int y)
{
    UserEnter();
    GDI_REGION *r = region_of(region);
    int result = ERROR;
    if (r) {
        RgnOffset(&r->Region, x, y);
        result = RgnComplexity(&r->Region);
    }
    UserLeave();
    return result;
}

int NTAPI NtGdiGetRgnBox(HRGN region, LPRECT rect)
{
    UserEnter();
    GDI_REGION *r = region_of(region);
    RECT box = {0, 0, 0, 0};
    int result = ERROR;
    if (r) {
        box = r->Region.Extents;
        result = RgnComplexity(&r->Region);
    }
    UserLeave();
    if (result != ERROR && !NT_SUCCESS(MimeCopyToUser(rect, &box, sizeof(box))))
        return ERROR;
    return result;
}

BOOL NTAPI NtGdiPtInRegion(HRGN region, int x, int y)
{
    UserEnter();
    GDI_REGION *r = region_of(region);
    BOOL in = r && RgnPtIn(&r->Region, x, y);
    UserLeave();
    return in;
}

BOOL NTAPI NtGdiRectInRegion(HRGN region, LPRECT rect)
{
    RECT rc;
    if (!NT_SUCCESS(MimeCopyFromUser(&rc, rect, sizeof(rc))))
        return FALSE;
    UserEnter();
    GDI_REGION *r = region_of(region);
    BOOL in = r && RgnRectIn(&r->Region, &rc);
    UserLeave();
    return in;
}

BOOL NTAPI NtGdiEqualRgn(HRGN a, HRGN b)
{
    UserEnter();
    GDI_REGION *ra = region_of(a), *rb = region_of(b);
    BOOL eq = ra && rb && RgnEqual(&ra->Region, &rb->Region);
    UserLeave();
    return eq;
}

DWORD NTAPI NtGdiGetRegionData(HRGN region, DWORD size, LPRGNDATA data)
{
    UserEnter();
    GDI_REGION *r = region_of(region);
    DWORD needed = 0;
    if (r) {
        needed = (DWORD)(sizeof(RGNDATAHEADER) + r->Region.Count * sizeof(RECT));
        if (data && size >= needed) {
            RGNDATAHEADER h = {sizeof(RGNDATAHEADER), RDH_RECTANGLES, r->Region.Count,
                               (DWORD)(r->Region.Count * sizeof(RECT)), r->Region.Extents};
            if (!NT_SUCCESS(MimeCopyToUser(data, &h, sizeof(h))) ||
                !NT_SUCCESS(MimeCopyToUser((UCHAR *)data + sizeof(h), r->Region.Rects, r->Region.Count * sizeof(RECT))))
                needed = 0;
        } else if (data) {
            needed = 0;
        }
    }
    UserLeave();
    return needed;
}
