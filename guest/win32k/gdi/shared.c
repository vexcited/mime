/*
 * the memory gdi32 shares with win32k, as windows 11 26300 lays it out, measured there. the peb of a process names a
 * read-only view of 0x181000 bytes in GdiSharedHandleTable:
 *
 *   65536 entries of 0x18 bytes, one for each index of a handle. the first 8 bytes stand for the object in the
 *   kernel, which windows hides as ff000000 over the reuse count and the index. then the process that owns it, 0
 *   for a stock object, the high half of the handle, the type in its low 5 bits and a flag byte, 1 on the stock
 *   brushes, pens and colour space. the last 8 bytes are the attributes the client may read without a call, mixed
 *   with the cookie NtGdiInit2 returns: gdi32 takes cookie ^ ror(value, 64 - (cookie & 63)), and 0 is none.
 *
 *   at 0x180000 the capabilities of the display, at 0x1800b0 the handles of the stock objects by their index.
 *
 * the attributes of device contexts are in gdi/dc.c. those of brushes and regions are here: windows gives them to the
 * solid brushes of NtGdiCreateSolidBrush and to the regions of NtGdiCreateRectRgn only, not to hatched brushes, pens,
 * stock objects or elliptic regions. a font takes the block gdi32 allocates itself and passes as the last argument of
 * NtGdiHfontCreate. measured on windows.
 *
 * a 32-bit program reads the same table, through the 64-bit peb, and takes the attributes out with 32 bits of the
 * cookie: cookie ^ ror32(value, 32 - (cookie & 31)), as win32kbase's GreEncodeUserModePointer writes them.
 *
 * the capabilities are the reference machine's, with the mode of the primary display laid over them.
 */
#include "../win32k.h"

typedef struct GDI_TAIL_CELL {
    USHORT Offset;
    ULONG64 Value;
} GDI_TAIL_CELL;

#include "shared.inc"

#define GDI_SHARED_SIZE 0x181000
#define GDI_SHARED_TAIL 0x180000
#define GDI_SHARED_STOCK 0x1800b0

typedef struct GDI_SHARED_ENTRY {
    ULONG64 Kernel;
    ULONG Process;
    USHORT Upper;
    UCHAR Type, Flags;
    ULONG64 User;
} GDI_SHARED_ENTRY;
MIME_STATIC_ASSERT(sizeof(GDI_SHARED_ENTRY) == 0x18, "gdi entry");

static PVOID g_section;
static PUCHAR g_base;
static ULONG64 g_cookie;

/*
 * the capabilities of the display at 0x180000, which gdi32 answers GetDeviceCaps of a display dc from for VREFRESH,
 * DESKTOPHORZRES and DESKTOPVERTRES without a call: the reference machine's, then the primary monitor's mode laid
 * over them. a ULONG each, in the order the reference holds them: version, technology, the size in millimetres,
 * in micrometres, in pixels, bits, planes, pens, fonts, colours, raster caps, three aspects, two logical dpis,
 * palette size, colour resolution, four physical values, text caps, refresh rate, the desktop in pixels.
 */
#define CAPS_HORZSIZE 0x08
#define CAPS_VERTSIZE 0x0c
#define CAPS_HORZSIZEM 0x10
#define CAPS_VERTSIZEM 0x14
#define CAPS_HORZRES 0x18
#define CAPS_VERTRES 0x1c
#define CAPS_BITSPIXEL 0x20
#define CAPS_NUMCOLORS 0x30
#define CAPS_RASTERCAPS 0x34
#define CAPS_LOGPIXELSX 0x44
#define CAPS_LOGPIXELSY 0x48
#define CAPS_SIZEPALETTE 0x4c
#define CAPS_COLORRES 0x50
#define CAPS_VREFRESH 0x68
#define CAPS_DESKTOPHORZRES 0x6c
#define CAPS_DESKTOPVERTRES 0x70

static void caps_put(ULONG offset, LONG value)
{
    *(PLONG)(g_base + GDI_SHARED_TAIL + offset) = value;
}

static void display_caps(void)
{
    LONG width = W32Display.Width, height = W32Display.Height, dpi = (LONG)W32SystemDpi();
    BOOLEAN palette = W32Display.Bits == 8;
    caps_put(CAPS_HORZSIZE, W32MulDiv(width, 254, dpi * 10));
    caps_put(CAPS_VERTSIZE, W32MulDiv(height, 254, dpi * 10));
    caps_put(CAPS_HORZSIZEM, (LONG)((LONG64)width * 25400 / dpi));
    caps_put(CAPS_VERTSIZEM, (LONG)((LONG64)height * 25400 / dpi));
    caps_put(CAPS_HORZRES, width);
    caps_put(CAPS_VERTRES, height);
    caps_put(CAPS_BITSPIXEL, (LONG)(W32Display.Bits ? W32Display.Bits : 32));
    caps_put(CAPS_NUMCOLORS, palette ? 20 : -1);
    LONG raster = *(PLONG)(g_base + GDI_SHARED_TAIL + CAPS_RASTERCAPS);
    caps_put(CAPS_RASTERCAPS, palette ? raster | RC_PALETTE : raster & ~RC_PALETTE);
    caps_put(CAPS_LOGPIXELSX, dpi);
    caps_put(CAPS_LOGPIXELSY, dpi);
    caps_put(CAPS_SIZEPALETTE, palette ? 256 : 0);
    caps_put(CAPS_COLORRES, palette ? 18 : 24);
    caps_put(CAPS_VREFRESH, (LONG)(W32Display.RefreshHz ? W32Display.RefreshHz : 60));
    caps_put(CAPS_DESKTOPHORZRES, width);
    caps_put(CAPS_DESKTOPVERTRES, height);
}

/* the mode of the display changed. */
void GdiSharedDisplayCaps(void)
{
    if (g_base)
        display_caps();
}

static BOOLEAN ready(void)
{
    if (g_base)
        return TRUE;
    PVOID base;
    if (!NT_SUCCESS(MmCreateSharedSection(GDI_SHARED_SIZE, &base, &g_section)))
        return FALSE;
    memset(base, 0, GDI_SHARED_SIZE);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_gdi_tail); i++)
        *(PULONG64)((PUCHAR)base + GDI_SHARED_TAIL + g_gdi_tail[i].Offset) = g_gdi_tail[i].Value;
    g_base = base;
    display_caps();
    return TRUE;
}

void GdiSharedSet(const GDI_OBJECT *object)
{
    ULONG_PTR handle = (ULONG_PTR)object->Handle;
    if (!ready() || !handle)
        return;
    BOOLEAN stock = (object->Flags & GDI_FLAG_STOCK) != 0;
    GDI_SHARED_ENTRY *e = (GDI_SHARED_ENTRY *)g_base + (handle & 0xffff);
    e->Kernel = 0xff00000000000000ull | ((handle >> 24) & 0xff) << 16 | (handle & 0xffff);
    /* an object of win32k itself, as a brush of a system color, is public like a stock one: gdi32 refuses to select
       an object of another process. */
    e->Process = stock || (object->Flags & GDI_FLAG_SYSTEM) ? 0 : (ULONG)(ULONG_PTR)object->OwnerPid;
    e->Upper = (USHORT)(handle >> 16);
    e->Type = (UCHAR)((handle >> 16) & 0x1f);
    e->Flags = stock && (e->Type == 0x10 || e->Type == 0x09);
    e->User = 0;
}

static ULONG64 cookie(void)
{
    if (!g_cookie) {
        LARGE_INTEGER counter = KeQueryPerformanceCounter(nullptr);
        g_cookie = ((ULONG64)counter.QuadPart ^ KeQueryInterruptTime()) * 0x9E3779B97F4A7C15ull | 1;
    }
    return g_cookie;
}

/* where the attributes of an object are in the program that owns it, as gdi32 takes them out of the cookie. */
void GdiSharedSetUser(HANDLE handle, PVOID user)
{
    if (!ready() || !handle)
        return;
    GDI_SHARED_ENTRY *e = (GDI_SHARED_ENTRY *)g_base + ((ULONG_PTR)handle & 0xffff);
    ULONG64 c = cookie();
    W32PROCESS *p = W32GetProcess();
    if (!user) {
        e->User = 0;
    } else if (p && p->Wow) {
        ULONG v = (ULONG)(ULONG_PTR)user ^ (ULONG)c, r = (ULONG)(c & 31);
        e->User = r ? (v >> r) | (v << (32 - r)) : v;
    } else {
        ULONG64 v = (ULONG64)(ULONG_PTR)user ^ c;
        ULONG r = (ULONG)(c & 63);
        e->User = r ? (v >> r) | (v << (64 - r)) : v;
    }
}

/* the byte of flags of an entry: 4 on the dcs of the display, which gdi32 does not batch a clip region for. */
void GdiSharedSetFlags(HANDLE handle, UCHAR flags)
{
    if (ready() && handle)
        ((GDI_SHARED_ENTRY *)g_base + ((ULONG_PTR)handle & 0xffff))->Flags = flags;
}

/* ------------------------------------------------------------------ attributes of brushes and regions */

#define SMALL_ATTR 0x20
#define ATTR_CHUNK 0x10000

/*
 * a block of the memory of the calling program for attributes, from chunks of 64 KiB. a 32-bit program gets it
 * below 2 GiB, where its pointers reach.
 */
PUCHAR GdiAttrAlloc(ULONG size)
{
    W32PROCESS *p = W32GetProcess();
    if (!p || !p->GdiBase || p->Process != PsGetCurrentProcess())
        return nullptr;
    size = (size + 15) & ~15u;
    if (size == SMALL_ATTR && p->SmallAttrFreeCount)
        return p->SmallAttrFree[--p->SmallAttrFreeCount];
    if (size != SMALL_ATTR && p->DcAttrFreeCount)
        return p->DcAttrFree[--p->DcAttrFreeCount];
    if (!p->DcAttrChunk || p->DcAttrUsed + size > ATTR_CHUNK) {
        PVOID base = nullptr;
        SIZE_T bytes = ATTR_CHUNK;
        if (!NT_SUCCESS(ZwAllocateVirtualMemory(NtCurrentProcess(), &base, p->Wow ? 0x7FFFFFFF : 0, &bytes,
                                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
            return nullptr;
        p->DcAttrChunk = base;
        p->DcAttrUsed = 0;
    }
    PUCHAR block = p->DcAttrChunk + p->DcAttrUsed;
    p->DcAttrUsed += size;
    return block;
}

void GdiAttrFree(GDI_OBJECT *object, ULONG size)
{
    PUCHAR block = object->User;
    object->User = nullptr;
    W32PROCESS *p = W32GetProcess();
    if (!block || !p || p->Process != PsGetCurrentProcess() || object->OwnerPid != PsGetCurrentProcessId())
        return;
    size = (size + 15) & ~15u;
    if (size == SMALL_ATTR && p->SmallAttrFreeCount < RTL_NUMBER_OF(p->SmallAttrFree))
        p->SmallAttrFree[p->SmallAttrFreeCount++] = block;
    else if (size != SMALL_ATTR && p->DcAttrFreeCount < RTL_NUMBER_OF(p->DcAttrFree))
        p->DcAttrFree[p->DcAttrFreeCount++] = block;
}

/*
 * a region attribute: flags, the complexity, then the bounds. gdi32 sets 0x20 in the flags when it changed the
 * rectangle itself, through CreateRectRgn, SetRectRgn, OffsetRgn or a CombineRgn of rectangles, and leaves a region
 * of a complexity above 2 to the kernel. a brush attribute: flags, then the colour, which gdi32 changed when 4 is
 * set, and 8 once its deletion is in the batch.
 */
#define RGN_ATTR_FRESH 0x10
#define RGN_ATTR_DIRTY 0x20
#define BRUSH_ATTR_COLOR 0x04

static GDI_OBJECT *g_touched[64];
static ULONG g_touched_count;
static BOOLEAN g_touched_lost;

static BOOLEAN owned_here(const GDI_OBJECT *o)
{
    return o->User && o->OwnerPid == PsGetCurrentProcessId();
}

void GdiRegionAttrAttach(GDI_OBJECT *object)
{
    PUCHAR block = GdiAttrAlloc(SMALL_ATTR);
    if (!block)
        return;
    const REGION *r = &((GDI_REGION *)object)->Region;
    ULONG image[6] = {RGN_ATTR_FRESH, (ULONG)RgnComplexity(r)};
    if (image[1] != NULLREGION)
        memcpy(&image[2], &r->Extents, sizeof(RECT));
    if (!NT_SUCCESS(MimeCopyToUser(block, image, sizeof(image))))
        return;
    object->User = block;
    GdiSharedSetUser(object->Handle, block);
}

void GdiBrushAttrAttach(GDI_OBJECT *object)
{
    PUCHAR block = GdiAttrAlloc(SMALL_ATTR);
    if (!block)
        return;
    ULONG image[4] = {0, ((BRUSH *)object)->Color};
    if (!NT_SUCCESS(MimeCopyToUser(block, image, sizeof(image))))
        return;
    object->User = block;
    GdiSharedSetUser(object->Handle, block);
}

/* what gdi32 changed of a brush or a region becomes the object, as the kernel looks it up. */
void GdiObjectAttrPull(GDI_OBJECT *o)
{
    if (!owned_here(o) || (o->Type != GDI_TYPE_REGION && o->Type != GDI_TYPE_BRUSH))
        return;
    ULONG image[6];
    if (!NT_SUCCESS(MimeCopyFromUser(image, o->User, sizeof(image))))
        return;
    if (o->Type == GDI_TYPE_BRUSH) {
        if (image[0] & BRUSH_ATTR_COLOR) {
            ((BRUSH *)o)->Color = image[1] & 0xFFFFFF;
            image[0] &= ~BRUSH_ATTR_COLOR;
            MimeCopyToUser(o->User, image, sizeof(ULONG));
        }
        return;
    }
    if (image[0] & RGN_ATTR_DIRTY) {
        REGION *r = &((GDI_REGION *)o)->Region;
        if (image[1] == SIMPLEREGION)
            RgnSetRect(r, (const RECT *)&image[2]);
        else
            RgnClear(r);
        image[0] &= ~RGN_ATTR_DIRTY;
        MimeCopyToUser(o->User, image, sizeof(ULONG));
    }
    for (ULONG i = 0; i < g_touched_count; i++)
        if (g_touched[i] == o)
            return;
    if (g_touched_count < RTL_NUMBER_OF(g_touched))
        g_touched[g_touched_count++] = o;
    else
        g_touched_lost = TRUE;
}

static void region_push(GDI_OBJECT *o)
{
    ULONG now[6], image[6];
    if (!owned_here(o) || !NT_SUCCESS(MimeCopyFromUser(now, o->User, sizeof(now))))
        return;
    const REGION *r = &((GDI_REGION *)o)->Region;
    memcpy(image, now, sizeof(image));
    image[1] = (ULONG)RgnComplexity(r);
    memset(&image[2], 0, sizeof(RECT));
    if (image[1] != NULLREGION)
        memcpy(&image[2], &r->Extents, sizeof(RECT));
    if (memcmp(now, image, sizeof(image)))
        MimeCopyToUser(o->User, image, sizeof(image));
}

/* the regions the kernel looked at get their complexity and bounds back, as the lock is let go. */
void GdiObjectAttrFlush(void)
{
    for (ULONG i = 0; i < g_touched_count; i++)
        region_push(g_touched[i]);
    g_touched_count = 0;
    if (g_touched_lost) {
        g_touched_lost = FALSE;
        GdiForEachRegionOf(PsGetCurrentProcessId(), region_push);
    }
}

void GdiObjectAttrForget(GDI_OBJECT *o)
{
    for (ULONG i = 0; i < g_touched_count; i++)
        if (g_touched[i] == o)
            g_touched[i] = g_touched[--g_touched_count], i--;
}

/*
 * a freed entry keeps the high word of its handle and its type: gdi32's GetObjectType compares only the high word and
 * the process, 0 once freed, so a deleted palette still answers OBJ_PAL on windows. measured on windows.
 */
void GdiSharedClear(ULONG index)
{
    if (!g_base || index >= 0x10000)
        return;
    GDI_SHARED_ENTRY *e = (GDI_SHARED_ENTRY *)g_base + index;
    e->Kernel = 0;
    e->Process = 0;
    e->Flags = 0;
    e->User = 0;
}

/* the entry of an index, which gdi32 asks for when the index is past the 65536 of the table. */
NTSTATUS NTAPI NtGdiGetEntry(ULONG index, PVOID entry)
{
    if (!ready() || index >= 0x10000)
        return STATUS_INVALID_PARAMETER;
    GDI_SHARED_ENTRY copy = ((GDI_SHARED_ENTRY *)g_base)[index];
    return MimeCopyToUser(entry, &copy, sizeof(copy));
}

/*
 * the handles of the stock objects by their index, up to 21, DEFAULT_BITMAP, which gdi32 hands out for a bitmap of size
 * 0. gdi32full's GetStockObject reads 22 more at 0x180160, the fonts of a program that is not dpi aware, and 16 at
 * 0x180210, the gui fonts of the dpis from 120 to 480, so they are slots 22 to 59 here.
 */
void GdiSharedStock(int index, HANDLE handle)
{
    if (ready() && index >= 0 && index < 60)
        *(PULONG64)(g_base + GDI_SHARED_STOCK + index * 8) = (ULONG64)(ULONG_PTR)handle;
}

/* the view of a process, made once, and told in its peb. */
NTSTATUS GdiSharedMap(W32PROCESS *p)
{
    if (p->GdiBase)
        return STATUS_SUCCESS;
    if (!ready())
        return STATUS_NO_MEMORY;
    NTSTATUS status = MmMapReadOnlySection(g_section, p->Process, &p->GdiBase);
    PPEB peb = NT_SUCCESS(status) ? PsGetProcessPeb(p->Process) : nullptr;
    if (peb)
        status = MimeCopyToUser(&peb->GdiSharedHandleTable, &p->GdiBase, sizeof(p->GdiBase));
    return status;
}

/* the cookie of the attributes, one for the session, never 0. gdi32 calls it once as it loads. */
ULONG64 NTAPI NtGdiInit2(void)
{
    UserEnterRead();
    W32PROCESS *p = W32GetProcess();
    if (p)
        GdiSharedMap(p);
    ULONG64 value = cookie();
    UserLeave();
    return value;
}
