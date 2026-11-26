/*
 * device contexts: attributes, object selection, saved states, clipping
 * and coordinate mapping.
 */
#include "../win32k.h"

GDI_OBJECT *GdiStockObject(int index);

static void attr_pull(DC *dc);

DC *DcLookup(HDC hdc)
{
    DC *dc = (DC *)GdiLookup(hdc, GDI_TYPE_DC);
    if (dc && dc->Attr)
        attr_pull(dc);
    return dc;
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

void DcSelectPalette(DC *dc, PALETTE *palette)
{
    if (dc->Palette == palette)
        return;
    select_ref(palette ? &palette->Header : nullptr);
    select_unref(dc->Palette ? &dc->Palette->Header : nullptr);
    dc->Palette = palette;
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
    s->BrushOrg = (POINT){0, 0};
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
    DcSelectPalette(dc, nullptr);
    s->Brush = (BRUSH *)GdiStockObject(WHITE_BRUSH);
    s->Pen = (PEN *)GdiStockObject(BLACK_PEN);
    s->Font = (FONT *)GdiStockObject(SYSTEM_FONT);
    select_ref(&s->Brush->Header);
    select_ref(&s->Pen->Header);
    select_ref(&s->Font->Header);
}

/* ------------------------------------------------------------------ attributes in the program */

/*
 * gdi32 keeps the attributes of a device context in the memory of the program that owns it, 0x170 bytes the entry of
 * the handle points to, see gdi/shared.c. it reads them there, and writes most of them there without a call: the
 * colours, the modes, the brush, the pen and the font, the current position, the origins and the extents. measured on
 * windows 11 26100, and the offsets gdi32full 26300 reads agree:
 *
 *   0x10 the colour space          0x20 the transform, as floats  0x68 the mapping mode       0x6c the layout
 *   0x80 the size of the bitmap    0x98 what gdi32 changed        0xa0 the brush              0xa8 the pen
 *   0xb0 the background colour     0xb8 the text colour           0xc0 the colour of the brush of the context
 *   0xc8 the colour of its pen     0xd0 the graphics mode         0xd4 a byte each: rop2, background mode, fill
 *   mode, stretch mode             0xd8 the current position      0xe0 the background mode    0xe4 the fill mode
 *   0xe8 the stretch mode          0x110 the text alignment, twice 0x118 the extra of characters
 *   0x11c the extra of SetTextJustification and 0x120 its count of breaks
 *   0x128 the font                 0x130 the origin of the window: x, x as the layout mirrors it, y
 *   0x13c the extent of the window 0x144 the origin of the viewport 0x14c its extent           0x158 the origin of
 *   the brush
 *
 * each colour is there twice, as asked and as realized. the kernel takes what the program changed when a call
 * names the context, and writes what the call changed when it lets go of the lock of the window manager. the
 * transform stays the one of a fresh context, and the bits of what changed are left as gdi32 wrote them.
 */
#define ATTR_SIZE 0x170
#define ATTR_CHUNK 0x10000

static const UCHAR g_attr_fresh[ATTR_SIZE] = {
    [0x00] = 2,
    [0x20] = 0x00, [0x21] = 0x00, [0x22] = 0x80, [0x23] = 0x41, [0x2c] = 0x00, [0x2d] = 0x00, [0x2e] = 0x80, [0x2f] = 0x41,
    [0x40] = 0x4b, [0x44] = 0x00, [0x45] = 0x00, [0x46] = 0x80, [0x47] = 0x3d, [0x50] = 0x00, [0x51] = 0x00, [0x52] = 0x80, [0x53] = 0x3d,
    [0x64] = 0x53, [0x68] = 1,
    [0x88] = 1,
    [0x98] = 0x1f, [0x99] = 0x00, [0x9a] = 0x12,
    [0xd0] = 1,
    [0x100] = 0xff, [0x101] = 0xff, [0x102] = 0xff, [0x103] = 0xff, [0x104] = 0xff, [0x105] = 0xff, [0x106] = 0xff, [0x107] = 0xff,
    [0x13c] = 1, [0x140] = 1, [0x14c] = 1, [0x150] = 1,
    [0x154] = 0x02, [0x155] = 0x18,
};

static DC *g_attr_touched[32];
static ULONG g_attr_touched_count;

#define A32(block, at) (*(ULONG *)((block) + (at)))
#define A64(block, at) (*(ULONG64 *)((block) + (at)))

/* the attributes as the state of the context has them, over a block that keeps what the kernel does not hold. */
static void attr_image(const DC *dc, UCHAR *block)
{
    const DC_STATE *s = &dc->State;
    BOOLEAN memory = (dc->Flags & DC_MEMORY) != 0;
    /* bit 0 while a dib section is selected: gdi32 batches nothing for such a dc, its program reads the bits. */
    A32(block, 0x00) = (A32(block, 0x00) & ~1u) | (memory && dc->Surface && (dc->Surface->Flags & (SURF_USER_BITS | SURF_FOREIGN)));
    A32(block, 0x68) = (ULONG)s->MapMode;
    A32(block, 0x6c) = s->Layout;
    A32(block, 0x74) = memory ? 2 : 0;
    /* a memory context holds the stock bitmap of one pixel until another is selected. */
    A32(block, 0x80) = !memory ? 0 : dc->Surface ? (ULONG)dc->Surface->Width : 1;
    A32(block, 0x84) = !memory ? 0 : dc->Surface ? (ULONG)dc->Surface->Height : 1;
    A64(block, 0xa0) = (ULONG64)(ULONG_PTR)s->Brush->Header.Handle;
    A64(block, 0xa8) = (ULONG64)(ULONG_PTR)s->Pen->Header.Handle;
    A32(block, 0xb0) = A32(block, 0xb4) = s->BkColor;
    A32(block, 0xb8) = A32(block, 0xbc) = s->TextColor;
    A32(block, 0xc0) = A32(block, 0xc4) = s->BrushColor;
    A32(block, 0xc8) = A32(block, 0xcc) = s->PenColor;
    A32(block, 0xd0) = (ULONG)s->GraphicsMode;
    block[0xd4] = (UCHAR)s->Rop2;
    block[0xd5] = (UCHAR)s->BkMode;
    block[0xd6] = (UCHAR)s->PolyFillMode;
    block[0xd7] = (UCHAR)s->StretchMode;
    A32(block, 0xd8) = (ULONG)s->CurrentPos.x;
    A32(block, 0xdc) = (ULONG)s->CurrentPos.y;
    A32(block, 0xe0) = (ULONG)s->BkMode;
    A32(block, 0xe4) = (ULONG)s->PolyFillMode;
    A32(block, 0xe8) = (ULONG)s->StretchMode;
    A32(block, 0x110) = A32(block, 0x114) = s->TextAlign;
    A32(block, 0x118) = (ULONG)s->TextCharExtra;
    A32(block, 0x11c) = (ULONG)s->BreakExtra;
    A32(block, 0x120) = (ULONG)s->BreakCount;
    A64(block, 0x128) = (ULONG64)(ULONG_PTR)s->Font->Header.Handle;
    A32(block, 0x130) = A32(block, 0x134) = (ULONG)s->WindowOrg.x;
    A32(block, 0x138) = (ULONG)s->WindowOrg.y;
    A32(block, 0x13c) = (ULONG)s->WindowExt.cx;
    A32(block, 0x140) = (ULONG)s->WindowExt.cy;
    A32(block, 0x144) = (ULONG)s->ViewportOrg.x;
    A32(block, 0x148) = (ULONG)s->ViewportOrg.y;
    A32(block, 0x14c) = (ULONG)s->ViewportExt.cx;
    A32(block, 0x150) = (ULONG)s->ViewportExt.cy;
    A32(block, 0x158) = (ULONG)s->BrushOrg.x;
    A32(block, 0x15c) = (ULONG)s->BrushOrg.y;
    A32(block, 0xec) = s->AttrFlag;
    /*
     * the matrices at 0x20 and 0x44 stay those of a fresh dc, so a mapping other than the identity sets the bits of
     * 0x154 that send LPtoDP, DPtoLP and GetCurrentPositionEx of gdi32 to the kernel, as win32kfull's DC::iSetMapMode
     * and bModifyWorldTransform set them. 0x400 marks the isotropic and anisotropic modes and 0x800 clears with a mode
     * other than MM_TEXT, as measured on windows.
     */
    ULONG fl = A32(block, 0x154);
    fl = s->MapMode == MM_ISOTROPIC || s->MapMode == MM_ANISOTROPIC ? fl | 0x400 : fl & ~0x400u;
    if (s->MapMode != MM_TEXT)
        fl &= ~0x800u;
    if (s->MapMode != MM_TEXT || s->HasXform || s->HasPage || (s->Layout & LAYOUT_RTL) || s->WindowOrg.x ||
        s->WindowOrg.y || s->ViewportOrg.x || s->ViewportOrg.y)
        fl |= 0xE010;
    A32(block, 0x154) = fl;
}

static void update_page(DC_STATE *s);

/* what the program wrote since the kernel last looked becomes the state of the context. called with the lock held. */
static void attr_pull(DC *dc)
{
    if (dc->Header.OwnerPid != PsGetCurrentProcessId())
        return;
    UCHAR now[ATTR_SIZE];
    if (!NT_SUCCESS(MimeCopyFromUser(now, dc->Attr, ATTR_SIZE)))
        return;
    if (!dc->AttrTouched && g_attr_touched_count < RTL_NUMBER_OF(g_attr_touched)) {
        dc->AttrTouched = TRUE;
        g_attr_touched[g_attr_touched_count++] = dc;
    }
    const UCHAR *was = dc->AttrSeen;
    if (!memcmp(now, was, ATTR_SIZE))
        return;
    DC_STATE *s = &dc->State;
    if (A64(now, 0xa0) != A64(was, 0xa0)) {
        BRUSH *b = (BRUSH *)GdiLookup((HANDLE)(ULONG_PTR)A64(now, 0xa0), GDI_TYPE_BRUSH);
        if (b) {
            select_unref(&s->Brush->Header);
            s->Brush = b;
            select_ref(&b->Header);
        }
    }
    if (A64(now, 0xa8) != A64(was, 0xa8)) {
        PEN *pen = (PEN *)GdiLookup((HANDLE)(ULONG_PTR)A64(now, 0xa8), GDI_TYPE_PEN);
        if (!pen)
            pen = (PEN *)GdiLookup((HANDLE)(ULONG_PTR)A64(now, 0xa8), GDI_TYPE_EXTPEN);
        if (pen) {
            select_unref(&s->Pen->Header);
            s->Pen = pen;
            select_ref(&pen->Header);
        }
    }
    if (A64(now, 0x128) != A64(was, 0x128)) {
        FONT *f = (FONT *)GdiLookup((HANDLE)(ULONG_PTR)A64(now, 0x128), GDI_TYPE_FONT);
        if (f) {
            select_unref(&s->Font->Header);
            s->Font = f;
            dc->Realized = nullptr;
            dc->RealizedFor = nullptr;
            select_ref(&f->Header);
        }
    }
    if (A32(now, 0xb0) != A32(was, 0xb0))
        s->BkColor = A32(now, 0xb0);
    if (A32(now, 0xb8) != A32(was, 0xb8))
        s->TextColor = A32(now, 0xb8);
    if (A32(now, 0xc0) != A32(was, 0xc0))
        s->BrushColor = A32(now, 0xc0);
    if (A32(now, 0xc8) != A32(was, 0xc8))
        s->PenColor = A32(now, 0xc8);
    if (A32(now, 0xd0) != A32(was, 0xd0))
        s->GraphicsMode = (int)A32(now, 0xd0);
    if (now[0xd4] != was[0xd4])
        s->Rop2 = now[0xd4];
    if (A32(now, 0xe0) != A32(was, 0xe0))
        s->BkMode = (int)A32(now, 0xe0);
    if (A32(now, 0xe4) != A32(was, 0xe4))
        s->PolyFillMode = (int)A32(now, 0xe4);
    if (A32(now, 0xe8) != A32(was, 0xe8))
        s->StretchMode = (int)A32(now, 0xe8);
    if (A64(now, 0xd8) != A64(was, 0xd8))
        s->CurrentPos = (POINT){(LONG)A32(now, 0xd8), (LONG)A32(now, 0xdc)};
    if (A32(now, 0x110) != A32(was, 0x110))
        s->TextAlign = A32(now, 0x110);
    if (A32(now, 0x118) != A32(was, 0x118))
        s->TextCharExtra = (int)A32(now, 0x118);
    /* SetTextJustification writes them there and makes no call. */
    if (A64(now, 0x11c) != A64(was, 0x11c)) {
        s->BreakExtra = (LONG)A32(now, 0x11c);
        s->BreakCount = (LONG)A32(now, 0x120);
    }
    if (A32(now, 0xec) != A32(was, 0xec))
        s->AttrFlag = A32(now, 0xec);
    if (A64(now, 0x158) != A64(was, 0x158))
        s->BrushOrg = (POINT){(LONG)A32(now, 0x158), (LONG)A32(now, 0x15c)};
    if (A32(now, 0x68) != A32(was, 0x68))
        s->MapMode = (int)A32(now, 0x68);
    if (memcmp(now + 0x130, was + 0x130, 0x24)) {
        s->WindowOrg = (POINT){(LONG)A32(now, 0x130), (LONG)A32(now, 0x138)};
        s->WindowExt = (SIZE){(LONG)A32(now, 0x13c), (LONG)A32(now, 0x140)};
        s->ViewportOrg = (POINT){(LONG)A32(now, 0x144), (LONG)A32(now, 0x148)};
        s->ViewportExt = (SIZE){(LONG)A32(now, 0x14c), (LONG)A32(now, 0x150)};
        if (!s->WindowExt.cx || !s->WindowExt.cy || !s->ViewportExt.cx || !s->ViewportExt.cy) {
            s->WindowExt = (SIZE){1, 1};
            s->ViewportExt = (SIZE){1, 1};
        }
        update_page(s);
    }
    memcpy(dc->AttrSeen, now, ATTR_SIZE);
}

/* what the calls changed is written where the program reads it. called as the lock of the window manager is let go. */
void DcAttrFlush(void)
{
    HANDLE pid = PsGetCurrentProcessId();
    for (ULONG i = 0; i < g_attr_touched_count; i++) {
        DC *dc = g_attr_touched[i];
        dc->AttrTouched = FALSE;
        if (!dc->Attr || dc->Header.OwnerPid != pid)
            continue;
        UCHAR image[ATTR_SIZE];
        memcpy(image, dc->AttrSeen, ATTR_SIZE);
        attr_image(dc, image);
        /* NtGdiGetCharSet leaves its answer at 0x04 and takes back the bit 0x10 gdi32 sets when it selects a font. */
        if (dc->AttrCharSet) {
            A32(image, 0x04) = dc->AttrCharSet;
            A32(image, 0x98) &= ~0x10u;
            dc->AttrCharSet = 0;
        }
        if (memcmp(image, dc->AttrSeen, ATTR_SIZE) && NT_SUCCESS(MimeCopyToUser(dc->Attr, image, ATTR_SIZE)))
            memcpy(dc->AttrSeen, image, ATTR_SIZE);
    }
    g_attr_touched_count = 0;
    GdiObjectAttrFlush();
}

/* a new context of a program of gdi32 gets its attributes in the memory of that program. */
static void attr_attach(DC *dc)
{
    W32PROCESS *p = W32GetProcess();
    if (!p || !p->GdiBase || p->Process != PsGetCurrentProcess())
        return;
    PUCHAR slot = nullptr;
    if (p->DcAttrFreeCount) {
        slot = p->DcAttrFree[--p->DcAttrFreeCount];
    } else {
        if (!p->DcAttrChunk || p->DcAttrUsed + ATTR_SIZE > ATTR_CHUNK) {
            PVOID base = nullptr;
            SIZE_T size = ATTR_CHUNK;
            if (!NT_SUCCESS(ZwAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
                return;
            p->DcAttrChunk = base;
            p->DcAttrUsed = 0;
        }
        slot = p->DcAttrChunk + p->DcAttrUsed;
        p->DcAttrUsed += ATTR_SIZE;
    }
    UCHAR image[ATTR_SIZE];
    memcpy(image, g_attr_fresh, ATTR_SIZE);
    attr_image(dc, image);
    if (!NT_SUCCESS(MimeCopyToUser(slot, image, ATTR_SIZE)))
        return;
    memcpy(dc->AttrSeen, image, ATTR_SIZE);
    dc->Attr = slot;
    GdiSharedSetUser(dc->Header.Handle, slot);
}

static void attr_detach(DC *dc)
{
    for (ULONG i = 0; i < g_attr_touched_count; i++)
        if (g_attr_touched[i] == dc)
            g_attr_touched[i] = g_attr_touched[--g_attr_touched_count], i--;
    if (!dc->Attr)
        return;
    W32PROCESS *p = W32GetProcess();
    if (p && p->Process == PsGetCurrentProcess() && dc->Header.OwnerPid == PsGetCurrentProcessId() &&
        p->DcAttrFreeCount < RTL_NUMBER_OF(p->DcAttrFree))
        p->DcAttrFree[p->DcAttrFreeCount++] = dc->Attr;
    dc->Attr = nullptr;
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
    attr_attach(dc);
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
    /* the dcs of the screen have 4 in the flags of their entry, see gdi/shared.c. */
    if (!(dc->Flags & DC_MEMORY) && s && (s == ScreenSurface() || (s->Flags & SURF_VSCREEN)))
        GdiSharedSetFlags(dc->Header.Handle, 4);
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
    DcSelectPalette(dc, nullptr);
    if ((dc->Flags & DC_MEMORY) && dc->Surface)
        select_unref(&dc->Surface->Header);
    if (dc->DefaultBitmap)
        SurfaceDestroy(dc->DefaultBitmap);
    RgnFree(&dc->VisRgn);
    RgnFree(&dc->State.ClipRgn);
    RgnFree(&dc->State.MetaRgn);
    PathFree(&dc->Path);
    attr_detach(dc);
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
    /*
     * ANSI_VAR_FONT and ANSI_FIXED_FONT are the fonts of the display driver, 12 by 9, which win32kfull maps as device
     * fonts, without their width: MS Sans Serif 12 by 9 is 13 by 5 so, and 16 by 7 from CreateFont.
     */
    LOGFONTW lf = dc->State.Font->Logical;
    if (dc->State.Font->Header.Flags & GDI_FLAG_STOCK)
        lf.lfWidth = 0;
    dc->Realized = FontRealize(&lf, dc->Dpi);
    dc->RealizedFor = dc->State.Font;
    return dc->Realized;
}

/* ------------------------------------------------------------------ dc creation */

HDC NTAPI NtGdiCreateCompatibleDC(HDC hdc)
{
    UserEnterRead();
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
    UserEnterRead();
    DC *ref = DcLookup(hdc);
    DC *dc = ref ? DcCreate(DC_METAFILE) : nullptr;
    HDC result = nullptr;
    if (dc) {
        dc->Dpi = ref->Dpi;
        dc->Awareness = ref->Awareness;
        /* a printer measures the mapping modes in its own pixels. */
        dc->VirtualPixels = ref->VirtualPixels;
        dc->VirtualMm = ref->VirtualMm;
        RECT all = {0, 0, W32Display.Width, W32Display.Height};
        RgnSetRect(&dc->VisRgn, &all);
        result = (HDC)dc->Header.Handle;
    }
    UserLeave();
    return result;
}

/* the ninth argument is where the printer drivers of user mode get a handle back, which gdi32full keeps in their LDC. */
HDC NTAPI NtGdiOpenDCW(PUNICODE_STRING device, PVOID mode, PUNICODE_STRING port, ULONG type, BOOL display,
                       HANDLE pdev, PVOID driver_info, PVOID umpd, PVOID umpd_out)
{
    /*
     * the display dc reads the screen as composed from the windows, and what it draws is not shown. no name or
     * DISPLAY means the whole virtual screen, \\.\DISPLAYn one monitor, and a missing monitor gives no dc.
     */
    PWSTR name = nullptr;
    if (!NT_SUCCESS(W32CaptureString(device, &name, nullptr)))
        return nullptr;
    HDC h = nullptr;
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
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

/* as win32kfull's NtGdiGetAndSetDCDword, see the indices in ntuser.h: the old value goes to result. */
BOOL NTAPI NtGdiGetAndSetDCDword(HDC hdc, UINT index, DWORD value, DWORD *result)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    DWORD old = 0;
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        switch (index) {
        case 1:
        case 2:
        case 6:
            break;
        case MIME_DCDW_SET_RELABS:
            old = (DWORD)s->RelAbs;
            s->RelAbs = (int)value;
            break;
        case MIME_DCDW_SET_ATTRFLAG:
            old = s->AttrFlag;
            if (value > 1) {
                W32SetLastError(ERROR_INVALID_PARAMETER);
                old = (DWORD)-1;
            } else {
                s->AttrFlag = value;
            }
            break;
        case MIME_DCDW_SET_MAPMODE:
            if (!set_map_mode(dc, (int)value, &old))
                old = 0;
            break;
        case MIME_DCDW_SET_ARCDIRECTION:
            old = (DWORD)s->ArcDirection;
            if (value == AD_CLOCKWISE || value == AD_COUNTERCLOCKWISE) {
                s->ArcDirection = (int)value;
            } else {
                W32SetLastError(ERROR_INVALID_PARAMETER);
                old = 0;
            }
            break;
        default:
            ok = FALSE;
            break;
        }
    } else if (index == 5) {
        old = 0x80000000;
    }
    UserLeave();
    if (result)
        MimeCopyToUser(result, &old, sizeof(old));
    return ok;
}

/*
 * like win32kfull's NtGdiSetLayout, which gdi32full's SetLayout calls with a width of -1: the flags of the layout
 * are kept and the old ones given back, GDI_ERROR for no context. the mirroring itself is not drawn yet.
 */
DWORD NTAPI NtGdiSetLayout(HDC hdc, LONG width, DWORD layout)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    DWORD old = GDI_ERROR;
    if (dc) {
        old = dc->State.Layout;
        dc->State.Layout = layout;
    }
    UserLeave();
    (void)width;
    return old;
}

BOOL NTAPI NtGdiGetDCDword(HDC hdc, UINT index, DWORD *result)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    DWORD v = 0;
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        switch (index) {
        case 0:
            v = 1;
            break;
        case MIME_DCDW_RELABS:
            v = (DWORD)s->RelAbs;
            break;
        case MIME_DCDW_BREAKEXTRA:
            v = *(const ULONG *)(dc->AttrSeen + 0x11c);
            break;
        case MIME_DCDW_BREAKCOUNT:
            v = *(const ULONG *)(dc->AttrSeen + 0x120);
            break;
        case MIME_DCDW_ARCDIRECTION:
            v = (DWORD)s->ArcDirection;
            break;
        case MIME_DCDW_SAVELEVEL:
            v = (DWORD)dc->SaveLevel;
            break;
        case MIME_DCDW_FONTLANGUAGE: {
            /* like NtGdiGetCharSet, it also leaves the charset in the attributes. GCP_ERROR without a font. */
            FONT_INSTANCE *fi = DcGetFont(dc);
            v = 0x8000;
            if (fi) {
                v = FontLanguageInfo(fi);
                dc->AttrCharSet = (DWORD)fi->CharSet << 16 | FontCharSetCodePage(fi->CharSet);
            }
            break;
        }
        case MIME_DCDW_MEMORYDC:
            v = (dc->Flags & DC_MEMORY) != 0;
            break;
        case MIME_DCDW_MAPMODE:
            v = (DWORD)s->MapMode;
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
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    POINT p = {0, 0};
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        switch (index & ~(UINT)MIME_DCPT_SCREEN) {
        case MIME_DCPT_VIEWPORTEXT:
            if (s->MapMode == MM_ISOTROPIC)
                isotropic_adjust(s);
            p = (POINT){s->ViewportExt.cx, s->ViewportExt.cy};
            break;
        case MIME_DCPT_WINDOWEXT:
            p = (POINT){s->WindowExt.cx, s->WindowExt.cy};
            break;
        case MIME_DCPT_VIEWPORTORG:
            p = s->ViewportOrg;
            break;
        case MIME_DCPT_WINDOWORG:
            p = s->WindowOrg;
            break;
        case MIME_DCPT_ASPECTFILTER:
            break;
        case MIME_DCPT_DCORG:
            /* the surface is a top level window's: 0x40 asks for the place of the origin on the screen. */
            if (dc->Window) {
                p = dc->Origin;
                if ((index & MIME_DCPT_SCREEN) && dc->Surface && dc->Surface->Window) {
                    p.x += dc->Surface->Window->WindowRect.left;
                    p.y += dc->Surface->Window->WindowRect.top;
                }
            }
            break;
        default:
            ok = FALSE;
        }
    }
    UserLeave();
    return ok && NT_SUCCESS(MimeCopyToUser(point, &p, sizeof(p)));
}

/*
 * gdi32 passes 1 for LPtoDP, 2 for LPtoDP into 28.4 fixed point and 0 for DPtoLP, with the top bit when the dc
 * scales for dpi. win32kfull answers TRUE for no points.
 */
BOOL NTAPI NtGdiTransformPoints(HDC hdc, POINT *in, POINT *out, int count, int mode)
{
    if (count <= 0)
        return TRUE;
    if (count > 65536)
        return FALSE;
    mode &= 0x7FFFFFFF;
    POINT local[64];
    POINT *pts = count <= 64 ? local : W32Alloc((SIZE_T)count * sizeof(POINT));
    if (!pts || !NT_SUCCESS(MimeCopyFromUser(pts, in, (SIZE_T)count * sizeof(POINT)))) {
        if (pts != local)
            W32Free(pts);
        return FALSE;
    }
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc != nullptr;
    if (dc) {
        /* the map is affine, so 16 times a point lands on 16 times its image less 15 times the image of 0. */
        LONG ox = 0, oy = 0;
        DcLogicalToDevice(dc, &ox, &oy);
        for (int i = 0; i < count; i++) {
            if (mode == 1) {
                DcLogicalToDevice(dc, &pts[i].x, &pts[i].y);
            } else if (mode == 2) {
                pts[i].x *= 16;
                pts[i].y *= 16;
                DcLogicalToDevice(dc, &pts[i].x, &pts[i].y);
                pts[i].x += 15 * ox;
                pts[i].y += 15 * oy;
            } else {
                DcDeviceToLogical(dc, &pts[i].x, &pts[i].y);
            }
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
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    HANDLE h = nullptr;
    if (dc) {
        /* gdi32 asks with the type of the handle it wants, 0x80000 for a palette: the OBJ_ values are the same objects. */
        switch (type) {
        case 0x100000:
            type = OBJ_BRUSH;
            break;
        case 0x300000:
            type = OBJ_PEN;
            break;
        case 0xa0000:
            type = OBJ_FONT;
            break;
        case 0x50000:
            type = OBJ_BITMAP;
            break;
        case 0x80000:
            type = OBJ_PAL;
            break;
        }
        switch (type) {
        case OBJ_PAL: {
            GDI_OBJECT *p = dc->Palette ? &dc->Palette->Header : GdiStockObject(DEFAULT_PALETTE);
            h = p ? p->Handle : nullptr;
            break;
        }
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
            if (!(dc->Flags & DC_MEMORY) && GdiDisplayBitmap())
                h = GdiDisplayBitmap()->Header.Handle;
            break;
        }
    }
    UserLeave();
    return h;
}

HBITMAP NTAPI NtGdiSelectBitmap(HDC hdc, HBITMAP bitmap)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    SURFACE *s = SurfaceLookup(bitmap);
    HBITMAP old = nullptr;
    if (s == GdiDisplayBitmap())
        s = nullptr;
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

/* the memory dc a bitmap is selected into. the stock bitmap that every new memory dc holds belongs to none. */
HDC NTAPI NtGdiGetDCforBitmap(HBITMAP bitmap)
{
    UserEnterRead();
    SURFACE *s = SurfaceLookup(bitmap);
    HDC found = nullptr;
    if (s && s->Header.SelectCount && !(s->Header.Flags & (GDI_FLAG_STOCK | GDI_FLAG_SYSTEM))) {
        DC *dc = GdiSelectingDc(s);
        if (dc)
            found = (HDC)dc->Header.Handle;
    }
    UserLeave();
    return found;
}

HBRUSH NTAPI NtGdiSelectBrush(HDC hdc, HBRUSH brush)
{
    UserEnterRead();
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
    UserEnterRead();
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

/* the font of a dc the caller holds becomes another, and the old one is given back. */
HFONT DcSelectFont(DC *dc, FONT *f)
{
    HFONT old = (HFONT)dc->State.Font->Header.Handle;
    if (dc->State.Font == f)
        return old;
    select_unref(&dc->State.Font->Header);
    dc->State.Font = f;
    dc->Realized = nullptr;
    dc->RealizedFor = nullptr;
    select_ref(&f->Header);
    return old;
}

/*
 * gdi32 wrote the new font in the attributes before the call, so the old one is the font the kernel held before it
 * pulls them, as win32kfull returns the font of its own dc.
 */
HFONT NTAPI NtGdiSelectFont(HDC hdc, HFONT font)
{
    UserEnterRead();
    DC *dc = (DC *)GdiLookup(hdc, GDI_TYPE_DC);
    HFONT old = dc ? (HFONT)dc->State.Font->Header.Handle : nullptr;
    dc = DcLookup(hdc);
    FONT *f = (FONT *)GdiLookup(font, GDI_TYPE_FONT);
    if (dc && f)
        DcSelectFont(dc, f);
    else
        old = nullptr;
    UserLeave();
    return old;
}

/* ------------------------------------------------------------------ saved states */

int NTAPI NtGdiSaveDC(HDC hdc)
{
    UserEnterRead();
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
    UserEnterRead();
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

/* the application clip region is kept in device coordinates. a null region with RGN_COPY takes the clip away. */
int DcSelectClip(DC *dc, const REGION *region, int mode)
{
    if (!region && mode == RGN_COPY) {
        RgnClear(&dc->State.ClipRgn);
        dc->State.HasClip = FALSE;
        return SIMPLEREGION;
    }
    if (!region)
        return ERROR;
    if (!dc->State.HasClip || mode == RGN_COPY) {
        if (mode == RGN_COPY || mode == RGN_AND || mode == RGN_OR) {
            RgnCopy(&dc->State.ClipRgn, region);
        } else {
            /* combining with "no clip" means combining with the whole surface. */
            RECT all = {-0x4000000, -0x4000000, 0x4000000, 0x4000000};
            REGION full;
            RgnInit(&full);
            RgnSetRect(&full, &all);
            RgnCombine(&dc->State.ClipRgn, &full, region, mode);
            RgnFree(&full);
        }
    } else {
        RgnCombine(&dc->State.ClipRgn, &dc->State.ClipRgn, region, mode);
    }
    dc->State.HasClip = TRUE;
    return RgnComplexity(&dc->State.ClipRgn);
}

int NTAPI NtGdiExtSelectClipRgn(HDC hdc, HRGN region, int mode)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    int result = ERROR;
    if (dc) {
        GDI_REGION *r = region ? (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION) : nullptr;
        if (r || mode == RGN_COPY)
            result = DcSelectClip(dc, r ? &r->Region : nullptr, mode);
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
    UserEnterRead();
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
        if (result == NULLREGION) {
            box = (RECT){0, 0, 0, 0};
        } else {
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
    UserEnterRead();
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
    UserEnterRead();
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

/* the clip region moves by a logical distance, scaled to the device. without one there is nothing to move. */
int NTAPI NtGdiOffsetClipRgn(HDC hdc, int x, int y)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    int result = ERROR;
    if (dc) {
        LONG x0 = 0, y0 = 0, x1 = x, y1 = y;
        DcLogicalToDevice(dc, &x0, &y0);
        DcLogicalToDevice(dc, &x1, &y1);
        if (dc->State.HasClip) {
            RgnOffset(&dc->State.ClipRgn, x1 - x0, y1 - y0);
            result = RgnComplexity(&dc->State.ClipRgn);
        } else {
            result = SIMPLEREGION;
        }
    }
    UserLeave();
    return result;
}

/* whether a logical point or a part of a logical rectangle is inside the clip of the dc, -1 for no dc. */
BOOL NTAPI NtGdiPtVisible(HDC hdc, int x, int y)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    BOOL result = -1;
    if (dc) {
        REGION clip;
        RgnInit(&clip);
        LONG sx = x, sy = y;
        DcLogicalToSurface(dc, &sx, &sy);
        result = DcGetClip(dc, &clip) && RgnPtIn(&clip, sx, sy);
        RgnFree(&clip);
    }
    UserLeave();
    return result;
}

BOOL NTAPI NtGdiRectVisible(HDC hdc, const RECT *user_rect)
{
    RECT r;
    if (!user_rect || !NT_SUCCESS(MimeCopyFromUser(&r, user_rect, sizeof(r))))
        return FALSE;
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    BOOL result = FALSE;
    if (dc) {
        REGION clip;
        RgnInit(&clip);
        LONG l = r.left, t = r.top, rr = r.right, b = r.bottom;
        DcLogicalToSurface(dc, &l, &t);
        DcLogicalToSurface(dc, &rr, &b);
        RECT s = {W32Min(l, rr), W32Min(t, b), W32Max(l, rr), W32Max(t, b)};
        result = DcGetClip(dc, &clip) && RgnRectIn(&clip, &s);
        RgnFree(&clip);
    }
    UserLeave();
    return result;
}

/* SetBrushOrgEx when gdi32 cannot batch it: the old origin goes back and DC_ATTR+0x158 follows. */
BOOL NTAPI NtGdiSetBrushOrg(HDC hdc, int x, int y, POINT *old)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    POINT was = {0, 0};
    if (dc) {
        was = dc->State.BrushOrg;
        dc->State.BrushOrg = (POINT){x, y};
    }
    UserLeave();
    if (!dc)
        return FALSE;
    return !old || NT_SUCCESS(MimeCopyToUser(old, &was, sizeof(was)));
}

/* the clip region joins the meta region and the dc has no clip region left. */
int NTAPI NtGdiSetMetaRgn(HDC hdc)
{
    UserEnterRead();
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

/* ------------------------------------------------------------------ bounds */

static void bounds_union(DC *dc, const RECT *r)
{
    if (RectEmpty(r))
        return;
    if (RectEmpty(&dc->Bounds))
        dc->Bounds = *r;
    else
        RectUnion(&dc->Bounds, &dc->Bounds, r);
}

void DcAccumulateBounds(DC *dc, const RECT *r, const REGION *clip)
{
    if (!dc->BoundsOn)
        return;
    RECT d = *r;
    if (clip && !RectIntersect(&d, &d, &clip->Extents))
        return;
    RectOffset(&d, -dc->Origin.x, -dc->Origin.y);
    bounds_union(dc, &d);
}

void DcMarkDrawn(DC *dc, const RECT *r, const REGION *clip)
{
    if (dc->Surface)
        SurfaceMarkDrawn(dc->Surface, r, clip);
    DcAccumulateBounds(dc, r, clip);
}

static RECT ordered(LONG left, LONG top, LONG right, LONG bottom)
{
    return (RECT){W32Min(left, right), W32Min(top, bottom), W32Max(left, right), W32Max(top, bottom)};
}

/*
 * GrepSetBoundsRect of win32kfull 26300: the answer is the state before, enabled or not and empty or not, then
 * DCB_RESET empties the bounds, DCB_ACCUMULATE adds the rectangle in logical units, and DCB_ENABLE or DCB_DISABLE turn
 * the accumulation of what is drawn on or off. measured on windows.
 */
UINT NTAPI NtGdiSetBoundsRect(HDC hdc, const RECT *user_rect, UINT flags)
{
    RECT r = {};
    if (!user_rect)
        flags &= ~DCB_ACCUMULATE;
    else if (!NT_SUCCESS(MimeCopyFromUser(&r, user_rect, sizeof(r))))
        return 0;
    UserEnter();
    DC *dc = DcLookup(hdc);
    UINT old = 0;
    if (dc) {
        old = (dc->BoundsOn ? DCB_ENABLE : DCB_DISABLE) | (RectEmpty(&dc->Bounds) ? DCB_RESET : DCB_SET);
        if (flags & DCB_RESET)
            dc->Bounds = (RECT){};
        if (flags & DCB_ACCUMULATE) {
            DcLogicalToDevice(dc, &r.left, &r.top);
            DcLogicalToDevice(dc, &r.right, &r.bottom);
            RECT device = ordered(r.left, r.top, r.right, r.bottom);
            bounds_union(dc, &device);
        }
        if (flags & DCB_ENABLE)
            dc->BoundsOn = TRUE;
        else if (flags & DCB_DISABLE)
            dc->BoundsOn = FALSE;
    }
    UserLeave();
    return old;
}

/* GrepGetBoundsRect: the bounds inside the clip of the dc, in logical units, emptied after with DCB_RESET. */
UINT NTAPI NtGdiGetBoundsRect(HDC hdc, RECT *user_rect, UINT flags)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    UINT result = 0;
    RECT out = {};
    if (dc && RectEmpty(&dc->Bounds)) {
        result = DCB_RESET;
    } else if (dc) {
        out = dc->Bounds;
        REGION clip;
        RgnInit(&clip);
        if (DcGetClip(dc, &clip)) {
            RECT c = clip.Extents;
            RectOffset(&c, -dc->Origin.x, -dc->Origin.y);
            if (!RectIntersect(&out, &out, &c))
                out = (RECT){};
        }
        RgnFree(&clip);
        DcDeviceToLogical(dc, &out.left, &out.top);
        DcDeviceToLogical(dc, &out.right, &out.bottom);
        out = ordered(out.left, out.top, out.right, out.bottom);
        result = DCB_SET;
    }
    if (dc && (flags & DCB_RESET))
        dc->Bounds = (RECT){};
    UserLeave();
    if (result && !NT_SUCCESS(MimeCopyToUser(user_rect, &out, sizeof(out))))
        return 0;
    return result;
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
    UserEnterRead();
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
    UserEnterRead();
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
    UserEnterRead();
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

/* ------------------------------------------------------------------ transform queries */

/*
 * win32kfull refreshes its world to device matrix and answers whether it only scales, which gdi32full's font cache
 * needs. the matrices at 0x20 of the attributes stay those of a fresh dc, see attr_image.
 */
BOOL NTAPI NtGdiComputeXformCoefficients(HDC hdc)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    BOOL scale = dc && (!dc->State.HasXform || (dc->State.World.eM12 == 0 && dc->State.World.eM21 == 0));
    UserLeave();
    return scale;
}

BOOL NTAPI NtGdiUpdateTransform(HDC hdc)
{
    UserEnterRead();
    BOOL ok = DcLookup(hdc) != nullptr;
    UserLeave();
    return ok;
}

/*
 * as win32kfull: the old extent goes out first, the extents of the fixed mapping modes stay, and a zero denominator,
 * a zero result or an overflow fails.
 */
static BOOL scale_extent(HDC hdc, BOOLEAN viewport, int xn, int xd, int yn, int yd, SIZE *user_old)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    SIZE old = {0, 0};
    BOOL ok = dc != nullptr;
    if (dc) {
        DC_STATE *s = &dc->State;
        SIZE *ext = viewport ? &s->ViewportExt : &s->WindowExt;
        old = *ext;
        if (s->MapMode == MM_ISOTROPIC || s->MapMode == MM_ANISOTROPIC) {
            ok = xd && yd && !(xn == (int)0x80000000 && xd == -1) && !(yn == (int)0x80000000 && yd == -1);
            LONG64 cx = ok ? (LONG64)ext->cx * xn / xd : 0, cy = ok ? (LONG64)ext->cy * yn / yd : 0;
            ok = ok && cx && cy && cx == (LONG)cx && cy == (LONG)cy;
            if (ok) {
                *ext = (SIZE){(LONG)cx, (LONG)cy};
                if (s->MapMode == MM_ISOTROPIC)
                    isotropic_adjust(s);
                update_page(s);
            }
        }
    }
    UserLeave();
    if (dc && user_old && !NT_SUCCESS(MimeCopyToUser(user_old, &old, sizeof(old))))
        return FALSE;
    return ok;
}

BOOL NTAPI NtGdiScaleViewportExtEx(HDC hdc, int xn, int xd, int yn, int yd, SIZE *old)
{
    return scale_extent(hdc, TRUE, xn, xd, yn, yd, old);
}

BOOL NTAPI NtGdiScaleWindowExtEx(HDC hdc, int xn, int xd, int yn, int yd, SIZE *old)
{
    return scale_extent(hdc, FALSE, xn, xd, yn, yd, old);
}

BOOL NTAPI NtGdiCombineTransform(XFORM *user_out, const XFORM *user_a, const XFORM *user_b)
{
    XFORM a, b, out;
    if (!NT_SUCCESS(MimeCopyFromUser(&a, user_a, sizeof(a))) || !NT_SUCCESS(MimeCopyFromUser(&b, user_b, sizeof(b))))
        return FALSE;
    xform_multiply(&out, &a, &b);
    return NT_SUCCESS(MimeCopyToUser(user_out, &out, sizeof(out)));
}

/* the width of the window of the dc, or of its surface. */
LONG NTAPI NtGdiGetDeviceWidth(HDC hdc)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    LONG width = -1;
    if (dc && dc->Window)
        width = dc->Window->WindowRect.right - dc->Window->WindowRect.left;
    else if (dc && dc->Surface)
        width = dc->Surface->Width;
    UserLeave();
    return width;
}

/* gdi32full calls it only under LAYOUT_RTL, whose mirroring is not drawn yet, see NtGdiSetLayout. */
BOOL NTAPI NtGdiMirrorWindowOrg(HDC hdc)
{
    UserEnterRead();
    BOOL ok = DcLookup(hdc) != nullptr;
    UserLeave();
    return ok;
}
