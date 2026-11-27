/*
 * cursor and icon objects as win32kfull keeps them, read from its 26100 build and the user32 of 26300 and measured on
 * windows. user32 decodes the resources, then hands the kernel a mask, a picture and their sizes. the kernel keeps the
 * bitmaps as public objects, finds shared objects again by module, resource and size, and draws them. mime also keeps a
 * premultiplied picture for the host.
 */
#include "win32k.h"
#include <png/png.h>
#include <mime/win32/winbase.h>

typedef struct CURICON {
    USER_OBJECT Header;
    W32PROCESS *Owner;      /* null for the objects of the session. */
    struct CURICON *Next;   /* in the shared list of the owner, or the global one. */
    struct CURICON *Root;   /* the cursor the dpi variants hang from, itself for a root. */
    struct CURICON *NextDpi;
    UNICODE_STRING Name;    /* the resource, an id in Buffer when Length is 0. */
    ATOM Module;
    USHORT Type;            /* 1 a cursor, 3 an icon. */
    ULONG DpiSize;          /* the size of a cursor at the dpi it was made for. */
    ULONG Flags;            /* CURSORF_*. */
    SHORT HotX, HotY;
    SURFACE *Mask, *Color, *Alpha;
    ULONG Bpp, Cx, Cy;      /* Cy is twice the height of the picture. */
    ULONG Extra;
    /* animated cursors. */
    ULONG FrameCount, StepCount, Step;
    struct CURICON **Frames;
    LONG *Sequence;
    ULONG *Rates;
    /* mime's own: the picture premultiplied for the host and the frames, and the host cursor of a system one. */
    SURFACE *Image;
    ULONG System;           /* MIME_CURSOR_* + 1. */
    UCHAR Awareness;
} CURICON;

static CURICON *g_global;

#define SYSTEM_CURSORS 18
#define SYSTEM_ICONS 7

/*
 * the objects the session loads from user32 as it starts: groups 100 to 118 of its cursors become the system
 * cursors, named by the OCR_ number, and groups 100 to 106 of its icons the OIC_ ones. measured on windows:
 * these 18 numbers are the cursors user32 finds without a resource. group 116 is not matched to a number yet. the
 * icons were matched by their pixels with cur.exe, the cursors by their shapes: windows draws them from the files
 * of the cursor scheme of the user when it has one.
 */
static const USHORT g_system_icon[SYSTEM_ICONS] = {32512, 32515, 32514, 32513, 32516, 32517, 32518};

static const struct {
    USHORT Id;
    USHORT Group;
} g_system_cursor[SYSTEM_CURSORS] = {
    {32512, 100}, {32513, 101}, {32514, 102}, {32515, 103}, {32516, 104}, {32642, 105},
    {32643, 106}, {32644, 107}, {32645, 108}, {32646, 109}, {32648, 110}, {32650, 111},
    {32651, 112}, {32631, 113}, {32649, 114}, {32663, 115}, {32671, 117}, {32672, 118},
};

static CURICON *g_system[SYSTEM_CURSORS + SYSTEM_ICONS];
static BOOLEAN g_system_made, g_system_filled;

static void system_objects(void);

static CURICON *lookup(HICON icon)
{
    return (CURICON *)UserLookup(icon, USER_TYPE_CURSOR);
}

/* the object or 0 with ERROR_INVALID_CURSOR_HANDLE, as HMValidateHandle does. */
static CURICON *validate(HICON icon)
{
    CURICON *c = lookup(icon);
    if (!c)
        W32SetLastError(ERROR_INVALID_CURSOR_HANDLE);
    return c;
}

static CURICON *frame_of(CURICON *c, LONG step)
{
    if (!(c->Flags & CURSORF_ACON))
        return c;
    if (!c->Sequence || !c->Frames || step < 0 || (ULONG)step >= c->StepCount)
        return nullptr;
    LONG frame = c->Sequence[step];
    if (frame < 0 || (ULONG)frame >= c->FrameCount || (c->Frames[frame]->Flags & CURSORF_ACON))
        return nullptr;
    return c->Frames[frame];
}

/* what a cursor shows now: a frame of an animated one. */
static CURICON *shown(CURICON *c)
{
    return c ? frame_of(c, (LONG)c->Step) : nullptr;
}

BOOLEAN CursorValid(HICON icon)
{
    return lookup(icon) != nullptr;
}

SURFACE *CursorIconImage(HICON icon)
{
    CURICON *c = shown(lookup(icon));
    return c && c->Type == 3 ? c->Image : nullptr;
}

SURFACE *CursorImage(HICON cursor)
{
    CURICON *c = shown(lookup(cursor));
    return c ? c->Image : nullptr;
}

/* the awareness of the pixels of a cursor, unaware when it is none. */
ULONG CursorAwareness(HICON cursor)
{
    CURICON *c = lookup(cursor);
    return c ? c->Awareness : DPI_AWARE_UNAWARE;
}

void CursorHotspot(HICON cursor, LONG *x, LONG *y)
{
    CURICON *c = shown(lookup(cursor));
    *x = c ? c->HotX : 0;
    *y = c ? c->HotY : 0;
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
        return MIME_CURSOR_WAIT;
    case 32650: /* IDC_APPSTARTING */
        return MIME_CURSOR_PROGRESS;
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

/* the size of a cursor at the system dpi, what GetCursorSizeFromIndex answers for it. */
static ULONG system_cursor_size(void)
{
    return (ULONG)MetricsGet(SM_CXCURSOR, W32SystemDpi());
}

/* rt 1 and the objects of user32 follow the size of the system's cursors, see ShouldVirtualizeIconCursorSize. */
static BOOLEAN virtualized(const CURICON *c)
{
    static ATOM user32;
    if (c->Type == 1)
        return TRUE;
    if (!user32)
        user32 = AtomFind(L"USER32");
    return user32 && c->Module == user32;
}

static void virtual_size(const CURICON *c, LONG *cx, LONG *cy)
{
    *cx = (LONG)c->Cx;
    *cy = (LONG)c->Cy;
    if (virtualized(c) && c->DpiSize) {
        LONG size = (LONG)system_cursor_size();
        *cx = W32MulDiv(*cx, size, (LONG)c->DpiSize);
        *cy = W32MulDiv(*cy, size, (LONG)c->DpiSize);
    }
}

/* the dpi variant of a cursor for the size of the system's, see GetVirtualizedDpiCursor. */
static CURICON *dpi_variant(CURICON *c)
{
    if (!virtualized(c))
        return c;
    ULONG size = system_cursor_size();
    for (CURICON *v = c->Root; v; v = v->NextDpi)
        if (v->DpiSize == size)
            return v;
    return c;
}

/* ------------------------------------------------------------------ bitmaps */

static void make_public(SURFACE *s)
{
    s->Header.Flags |= GDI_FLAG_SYSTEM;
    s->Header.OwnerPid = nullptr;
    GdiSharedSet(&s->Header);
}

static void free_public(SURFACE *s)
{
    if (s)
        SurfaceDestroy(s);
}

/* a new bitmap of a part of another, stretched to w by h, in its own depth or the given one. */
static SURFACE *copy_bits(SURFACE *from, LONG sx, LONG sy, LONG sw, LONG sh, LONG w, LONG h, UCHAR bpp)
{
    SURFACE *s = SurfaceCreate(w, h, bpp ? bpp : from->Bpp, 0);
    if (!s)
        return nullptr;
    if (s->Bpp <= 8 && from->Bpp == s->Bpp) {
        memcpy(s->Colors, from->Colors, sizeof(s->Colors));
        s->ColorCount = from->ColorCount;
    }
    for (LONG y = 0; y < h; y++) {
        LONG yy = sy + (LONG)(((LONG64)(2 * y + 1) * sh) / (2 * (LONG64)h));
        for (LONG x = 0; x < w; x++) {
            LONG xx = sx + (LONG)(((LONG64)(2 * x + 1) * sw) / (2 * (LONG64)w));
            ULONG px = yy < from->Height && xx < from->Width ? SurfaceGetPixel(from, xx, yy) : 0;
            SurfacePutPixel(s, x, y, px);
        }
    }
    return s;
}

/* the premultiplied picture of a 32 bpp bitmap that has alpha, else 0, see ProcessAlphaBitmap. */
static SURFACE *alpha_of(SURFACE *color)
{
    if (!color || color->Bpp != 32)
        return nullptr;
    BOOLEAN any = FALSE;
    for (LONG y = 0; y < color->Height && !any; y++)
        for (LONG x = 0; x < color->Width; x++)
            if (SurfaceGetPixel(color, x, y) >> 24) {
                any = TRUE;
                break;
            }
    if (!any)
        return nullptr;
    SURFACE *s = SurfaceCreate(color->Width, color->Height, 32, 0);
    if (!s)
        return nullptr;
    for (LONG y = 0; y < s->Height; y++)
        for (LONG x = 0; x < s->Width; x++) {
            ULONG px = SurfaceGetPixel(color, x, y), a = px >> 24;
            ULONG r = ((px >> 16) & 0xFF) * a / 255, g = ((px >> 8) & 0xFF) * a / 255, b = (px & 0xFF) * a / 255;
            SurfacePutPixel(s, x, y, a << 24 | r << 16 | g << 8 | b);
        }
    if (!GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP)) {
        SurfaceDestroy(s);
        return nullptr;
    }
    make_public(s);
    return s;
}

/* the premultiplied picture of a frame for the host and the frame of windows: alpha, or the mask over the picture. */
static SURFACE *build_image(CURICON *c)
{
    LONG w = (LONG)c->Cx, h = (LONG)c->Cy / 2;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || !c->Mask)
        return nullptr;
    SURFACE *img = SurfaceCreate(w, h, 32, 0);
    if (!img)
        return nullptr;
    for (LONG y = 0; y < h; y++) {
        PULONG row = (PULONG)(img->Bits + (LONG_PTR)y * img->Stride);
        for (LONG x = 0; x < w; x++) {
            if (c->Alpha) {
                row[x] = x < c->Alpha->Width && y < c->Alpha->Height ? SurfaceGetPixel(c->Alpha, x, y) : 0;
                continue;
            }
            BOOLEAN transparent = x < c->Mask->Width && y < c->Mask->Height &&
                                  (SurfaceGetPixel(c->Mask, x, y) & 0xFFFFFF) != 0;
            ULONG xor_px = 0;
            if (c->Color)
                xor_px = x < c->Color->Width && y < c->Color->Height ? SurfaceGetPixel(c->Color, x, y) & 0xFFFFFF : 0;
            else if (y + h < c->Mask->Height && x < c->Mask->Width)
                xor_px = SurfaceGetPixel(c->Mask, x, y + h) & 0xFFFFFF;
            row[x] = transparent ? 0 : 0xFF000000u | xor_px;
        }
    }
    return img;
}

/* the bitmap of a handle the kernel takes over: public, and its bits in the kernel. */
static SURFACE *take_bitmap(HBITMAP bitmap)
{
    SURFACE *s = bitmap ? SurfaceLookup(bitmap) : nullptr;
    if (!s)
        return nullptr;
    if (s->Flags & (SURF_USER_BITS | SURF_FOREIGN)) {
        /* a dib section lives in the memory of its process, which every process must be able to draw. */
        SURFACE *copy = copy_bits(s, 0, 0, s->Width, s->Height, s->Width, s->Height, 0);
        if (!copy || !GdiCreateHandle(&copy->Header, GDI_TYPE_BITMAP)) {
            SurfaceDestroy(copy);
            return nullptr;
        }
        GdiDeleteObject(&s->Header);
        s = copy;
    }
    make_public(s);
    return s;
}

static void delete_bitmap(HBITMAP bitmap)
{
    GDI_OBJECT *o = bitmap ? GdiLookup(bitmap, GDI_TYPE_BITMAP) : nullptr;
    if (o)
        GdiDeleteObject(o);
}

/* ------------------------------------------------------------------ lifetime */

static void unlink(CURICON *c)
{
    if (!(c->Flags & CURSORF_LINKED))
        return;
    CURICON **head = c->Owner ? &c->Owner->Cursors : &g_global;
    for (CURICON **p = head; *p; p = &(*p)->Next)
        if (*p == c) {
            *p = c->Next;
            break;
        }
    c->Next = nullptr;
    c->Flags &= ~CURSORF_LINKED;
}

static void destroy_now(CURICON *c, UINT how);
static void release(CURICON *c);

/* the parts of an object, see CleanupCursorObject. */
static void cleanup(CURICON *c)
{
    if (c->Name.Length && c->Name.Buffer)
        W32Free(c->Name.Buffer);
    c->Name = (UNICODE_STRING){};
    if (c->Module)
        AtomDelete(c->Module);
    c->Module = 0;
    if (c->Flags & CURSORF_ACON) {
        for (ULONG i = 0; c->Frames && i < c->FrameCount; i++) {
            CURICON *f = c->Frames[i];
            if (f->Header.Destroyed) {
                release(f);
            } else {
                f->Header.RefCount--;
                destroy_now(f, 0);
            }
        }
        if (c->Frames)
            W32Free(c->Frames);
        c->Frames = nullptr;
        c->Sequence = nullptr;
        c->Rates = nullptr;
        c->FrameCount = c->StepCount = 0;
    } else {
        free_public(c->Mask);
        free_public(c->Color);
        free_public(c->Alpha);
        c->Mask = c->Color = c->Alpha = nullptr;
    }
    if (c->Image)
        SurfaceDestroy(c->Image);
    c->Image = nullptr;
}

static void release(CURICON *c)
{
    if (--c->Header.RefCount > 0)
        return;
    HostForgetCursor((HCURSOR)c->Header.Handle);
    unlink(c);
    cleanup(c);
    UserFreeHandle(&c->Header);
    W32Free(c);
}

/* the dpi variants first, then the object; one a thread shows stays until it lets go. */
static void destroy_now(CURICON *c, UINT how)
{
    if (c->Header.Destroyed)
        return;
    if (c->Root == c) {
        for (CURICON *v = c->NextDpi; v;) {
            CURICON *next = v->NextDpi;
            v->Root = v;
            v->NextDpi = nullptr;
            destroy_now(v, how);
            v = next;
        }
        c->NextDpi = nullptr;
    } else if (c->Root) {
        for (CURICON *v = c->Root; v; v = v->NextDpi)
            if (v->NextDpi == c) {
                v->NextDpi = c->NextDpi;
                break;
            }
        c->NextDpi = nullptr;
        c->Root = c;
    }
    c->Header.Destroyed = TRUE;
    c->Flags |= CURSORF_DESTROYED;
    unlink(c);
    release(c);
}

static CURICON *create_object(W32PROCESS *owner)
{
    CURICON *c = W32Alloc(sizeof(CURICON));
    if (!c)
        return nullptr;
    c->Owner = owner;
    c->Root = c;
    c->Flags = CURSORF_INIT;
    c->Awareness = (UCHAR)DpiCurrentAwareness();
    if (!UserCreateHandle(&c->Header, USER_TYPE_CURSOR)) {
        W32Free(c);
        return nullptr;
    }
    return c;
}

/* ------------------------------------------------------------------ services */

/* a global object only for the process of the session, which mime does not have: every caller owns its own. */
HICON NTAPI NtUserCreateEmptyCursorObject(BOOL global)
{
    (void)global;
    UserEnter();
    CURICON *c = create_object(W32GetProcess());
    HICON h = c ? (HICON)c->Header.Handle : nullptr;
    UserLeave();
    return h;
}

static BOOLEAN valid_string(const UNICODE_STRING *s)
{
    ULONG_PTR b = (ULONG_PTR)s->Buffer;
    if (b & 1)
        return FALSE;
    if (b + s->Length + 2 <= b || b + s->Length + 2 >= (ULONG_PTR)MmUserProbeAddress)
        return FALSE;
    return s->Length <= s->MaximumLength && !(s->Length & 1);
}

/* the module name of a call, read to its end like RtlStringCchCopyW, at most 255 characters. */
static NTSTATUS read_module(PCWSTR user, WCHAR name[256])
{
    for (ULONG i = 0; i < 256; i++) {
        if (!NT_SUCCESS(MimeCopyFromUser(&name[i], user + i, sizeof(WCHAR))))
            return STATUS_ACCESS_VIOLATION;
        if (!name[i])
            return STATUS_SUCCESS;
    }
    name[255] = 0;
    return STATUS_BUFFER_OVERFLOW;
}

/* the resource names compare as in ResStrCmp: strings without case, "#n" as the number n. */
static BOOLEAN same_resource(const UNICODE_STRING *probe, const UNICODE_STRING *stored)
{
    if (!probe->Length)
        return probe->Buffer == stored->Buffer;
    if (stored->Length)
        return RtlEqualUnicodeString(probe, stored, TRUE);
    if (probe->Buffer[0] != L'#')
        return FALSE;
    ULONG value = 0;
    for (ULONG i = 1; i < probe->Length / sizeof(WCHAR); i++) {
        WCHAR ch = probe->Buffer[i];
        if (ch < L'0' || ch > L'9')
            break;
        value = value * 10 + (ch - L'0');
    }
    return value == (ULONG)(ULONG_PTR)stored->Buffer;
}

static CURICON *find_in(CURICON *list, ATOM module, const UNICODE_STRING *resource, CURICON *given,
                        const CURSORFIND *find, BOOLEAN *stop)
{
    for (CURICON *c = list; c; c = c->Next) {
        if (given && c == given) {
            *stop = TRUE;
            return given;
        }
        if (c->Module != module || (c->Flags & (CURSORF_LRSHARED | CURSORF_DPISCALED)) != CURSORF_LRSHARED ||
            c->Type != (USHORT)find->Type || !same_resource(resource, &c->Name))
            continue;
        if (c->Flags & CURSORF_ACON)
            return c;
        if ((!find->Cx || c->Cx == find->Cx) && (!find->Cy || c->Cy / 2 == find->Cy) && (!find->Bpp || c->Bpp == find->Bpp))
            return c;
    }
    return nullptr;
}

HICON NTAPI NtUserFindExistingCursorIcon(PUNICODE_STRING user_module, PUNICODE_STRING user_resource, CURSORFIND *user_find)
{
    CURSORFIND find;
    UNICODE_STRING module, resource;
    WCHAR name[256];
    WCHAR *text = nullptr;
    HICON result = nullptr;
    UserEnter();
    system_objects();
    if (!NT_SUCCESS(MimeCopyFromUser(&find, user_find, sizeof(find))))
        goto fault;
    CURICON *given = nullptr;
    if (find.Cursor && !(given = validate(find.Cursor)))
        goto done;
    if (!NT_SUCCESS(MimeCopyFromUser(&module, user_module, sizeof(module))) || !valid_string(&module))
        goto fault;
    if (!NT_SUCCESS(MimeCopyFromUser(&resource, user_resource, sizeof(resource))))
        goto fault;
    if (IS_INTRESOURCE(resource.Buffer)) {
        resource.Length = 0;
    } else {
        if (!valid_string(&resource))
            goto fault;
        text = W32Alloc(resource.Length + sizeof(WCHAR));
        if (!text || !NT_SUCCESS(MimeCopyFromUser(text, resource.Buffer, resource.Length)))
            goto fault;
        resource.Buffer = text;
    }
    NTSTATUS status = read_module(module.Buffer, name);
    if (status == STATUS_ACCESS_VIOLATION)
        goto fault;
    if (status == STATUS_BUFFER_OVERFLOW) {
        W32SetLastError(ERROR_MORE_DATA);
        goto done;
    }
    ATOM atom = AtomFind(name);
    if (!atom || !find.Type)
        goto done;
    W32PROCESS *p = W32GetProcess();
    BOOLEAN stop = FALSE;
    CURICON *c = p ? find_in(p->Cursors, atom, &resource, given, &find, &stop) : nullptr;
    if (!c && !stop)
        c = find_in(g_global, atom, &resource, given, &find, &stop);
    result = c ? (HICON)c->Header.Handle : nullptr;
    goto done;
fault:
    W32SetLastError(ERROR_NOACCESS);
done:
    UserLeave();
    if (text)
        W32Free(text);
    return result;
}

/* the frames of an animated cursor: frames of this process made for one, see _SetCursorIconDataEx. */
static BOOL set_animated(CURICON *c, const CURSORDATA *d, PVOID buffer, ULONG size)
{
    c->Frames = buffer;
    c->FrameCount = d->FrameCount;
    c->StepCount = d->StepCount;
    c->Sequence = (LONG *)((PUCHAR)buffer + d->SequenceOffset);
    c->Rates = (ULONG *)((PUCHAR)buffer + d->RateOffset);
    c->Step = 0;
    (void)size;
    ULONG error = 0;
    for (ULONG i = 0; i < c->StepCount && !error; i++)
        if (c->Sequence[i] < 0 || (ULONG)c->Sequence[i] >= c->FrameCount)
            error = ERROR_INVALID_PARAMETER;
    ULONG rated = 0;
    while (!error && rated < c->StepCount && c->Rates[rated] * 100 < 6)
        rated++;
    if (!error && rated == c->StepCount)
        error = ERROR_INVALID_PARAMETER;
    ULONG locked = 0;
    for (; !error && locked < c->FrameCount; locked++) {
        CURICON *f = lookup((HCURSOR)c->Frames[locked]);
        if (!f || (f->Flags & CURSORF_ACON) || !(f->Flags & CURSORF_ACONFRAME) || !f->Owner) {
            error = ERROR_INVALID_CURSOR_HANDLE;
            break;
        }
        f->Header.RefCount++;
        c->Frames[locked] = f;
    }
    if (error) {
        while (locked)
            c->Frames[--locked]->Header.RefCount--;
        W32SetLastError(error);
        W32Free(buffer);
        c->Frames = nullptr;
        c->Sequence = nullptr;
        c->Rates = nullptr;
        c->FrameCount = c->StepCount = 0;
        return FALSE;
    }
    return TRUE;
}

static void link(CURICON *c)
{
    if (c->Flags & CURSORF_ACONFRAME)
        return;
    if (!c->Owner) {
        c->Next = g_global;
        g_global = c;
    } else if (c->Flags & CURSORF_LRSHARED) {
        c->Next = c->Owner->Cursors;
        c->Owner->Cursors = c;
    } else {
        return;
    }
    c->Flags |= CURSORF_LINKED;
}

static void delete_given(CURICON *c, const CURSORDATA *d)
{
    if (!(c->Flags & CURSORF_ACON) && !(d->Flags & CURSORF_ACON)) {
        delete_bitmap(d->Mask);
        delete_bitmap(d->Color);
    }
}

/* the data of an empty object, once: see _SetCursorIconDataEx. module is a kernel copy or null. */
static BOOL set_data(CURICON *c, PCWSTR module, const UNICODE_STRING *resource, CURSORDATA *d, PVOID frames,
                     ULONG size, ULONG dpi_size)
{
    if (!dpi_size)
        dpi_size = system_cursor_size();
    c->Flags &= ~CURSORF_INIT;
    if ((d->Flags & CURSORF_ACON) && c->Frames) {
        if (frames)
            W32Free(frames);
        return FALSE;
    }
    if (!resource->Length) {
        c->Name = *resource;
    } else {
        PWSTR copy = W32Alloc(resource->Length + sizeof(WCHAR));
        if (!copy) {
            delete_given(c, d);
            if (frames)
                W32Free(frames);
            return FALSE;
        }
        memcpy(copy, resource->Buffer, resource->Length);
        c->Name = (UNICODE_STRING){resource->Length, (USHORT)(resource->Length + sizeof(WCHAR)), copy};
    }
    if (module) {
        c->Module = AtomAdd(module);
        if (!c->Module) {
            delete_given(c, d);
            if (c->Name.Length)
                W32Free(c->Name.Buffer);
            c->Name = (UNICODE_STRING){};
            if (frames)
                W32Free(frames);
            return FALSE;
        }
    }
    c->Flags |= d->Flags;
    c->Type = d->Type;
    c->DpiSize = dpi_size;
    if (c->Flags & CURSORF_ACON) {
        if (!frames) {
            if (c->Name.Length)
                W32Free(c->Name.Buffer);
            c->Name = (UNICODE_STRING){};
            if (c->Module)
                AtomDelete(c->Module);
            c->Module = 0;
            return FALSE;
        }
        if (!set_animated(c, d, frames, size))
            return FALSE;
        link(c);
        return TRUE;
    }
    c->HotX = d->HotspotX;
    c->HotY = d->HotspotY;
    c->Bpp = d->Bpp;
    c->Cx = d->Cx;
    c->Cy = d->Cy;
    c->Extra = d->Extra;
    SURFACE *mask = d->Mask ? SurfaceLookup(d->Mask) : nullptr;
    SURFACE *color = d->Color ? SurfaceLookup(d->Color) : nullptr;
    if ((d->Mask && !mask) || (d->Color && !color)) {
        delete_bitmap(d->Mask);
        delete_bitmap(d->Color);
        return FALSE;
    }
    /* a picture as tall as the mask or half of it, at least as wide. */
    if (!c->Cx || !c->Cy || !mask ||
        (color && ((color->Height != mask->Height / 2 && color->Height != mask->Height) || color->Width < mask->Width))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    c->Mask = take_bitmap(d->Mask);
    c->Color = color ? take_bitmap(d->Color) : nullptr;
    c->Alpha = alpha_of(c->Color);
    c->Image = build_image(c);
    link(c);
    return TRUE;
}

BOOL NTAPI NtUserSetCursorIconDataEx(HICON icon, PUNICODE_STRING user_module, PUNICODE_STRING user_resource,
                                     CURSORDATA *user_data, ULONG dpi_size)
{
    UNICODE_STRING module, resource;
    CURSORDATA d;
    WCHAR name[256];
    PWSTR text = nullptr;
    PVOID frames = nullptr;
    BOOL ok = FALSE;
    UserEnter();
    CURICON *c = validate(icon);
    if (!c)
        goto done;
    if (!(c->Flags & CURSORF_INIT)) {
        W32SetLastError(0);
        goto done;
    }
    if (!NT_SUCCESS(MimeCopyFromUser(&module, user_module, sizeof(module))) ||
        !NT_SUCCESS(MimeCopyFromUser(&resource, user_resource, sizeof(resource))) || !valid_string(&module))
        goto fault;
    if (!IS_INTRESOURCE(resource.Buffer) && !valid_string(&resource))
        goto fault;
    if (!NT_SUCCESS(MimeCopyFromUser(&d, user_data, sizeof(d))))
        goto fault;
    if (d.Flags & ~(CURSORF_FROMRESOURCE | CURSORF_GLOBAL | CURSORF_LRSHARED | CURSORF_ACON | CURSORF_ACONFRAME |
                    CURSORF_DPISCALED)) {
        W32SetLastError(ERROR_INVALID_FLAGS);
        goto done;
    }
    ULONG size = 0;
    if (d.Flags & CURSORF_ACON) {
        if ((d.FrameCount | d.StepCount) >> 16 || d.RateOffset != 8 * (ULONG_PTR)d.FrameCount ||
            d.SequenceOffset != d.RateOffset + 4 * (ULONG_PTR)d.StepCount)
            goto done;
        size = 8 * (d.FrameCount + d.StepCount);
        frames = size ? W32Alloc(size) : nullptr;
        if (size && (!frames || !NT_SUCCESS(MimeCopyFromUser(frames, d.Frames, size))))
            goto fault;
    }
    if (!IS_INTRESOURCE(resource.Buffer) && resource.Length) {
        text = W32Alloc(resource.Length + sizeof(WCHAR));
        if (!text || !NT_SUCCESS(MimeCopyFromUser(text, resource.Buffer, resource.Length)))
            goto fault;
        resource.Buffer = text;
    } else {
        resource.Length = 0;
    }
    PCWSTR module_name = nullptr;
    if (module.Buffer) {
        NTSTATUS status = read_module(module.Buffer, name);
        if (status == STATUS_ACCESS_VIOLATION)
            goto fault;
        /* a name past 255 characters makes no atom, which fails the call as an atom table that is full does. */
        if (status == STATUS_BUFFER_OVERFLOW) {
            c->Flags &= ~CURSORF_INIT;
            if (!(d.Flags & CURSORF_ACON)) {
                delete_bitmap(d.Mask);
                delete_bitmap(d.Color);
            }
            goto done;
        }
        module_name = name;
    }
    ok = set_data(c, module_name, &resource, &d, frames, size, dpi_size);
    frames = nullptr;
    goto done;
fault:
    W32SetLastError(ERROR_NOACCESS);
done:
    UserLeave();
    if (frames)
        W32Free(frames);
    if (text)
        W32Free(text);
    return ok;
}

BOOL NTAPI NtUserSetCursorIconData(HICON icon, PUNICODE_STRING module, PUNICODE_STRING resource, CURSORDATA *data)
{
    return NtUserSetCursorIconDataEx(icon, module, resource, data, 0);
}

/* how 2 destroys only what nobody owns, 1 is DestroyCursor, 0 the cleanup of user32. see _DestroyCursor. */
BOOL NTAPI NtUserDestroyCursor(HICON icon, UINT how)
{
    UserEnter();
    CURICON *c = validate(icon);
    BOOL ok = FALSE;
    W32PROCESS *p = W32GetProcess();
    if (!c) {
    } else if (how > 1) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else if ((how == 0 && c->Owner != p) || c->Root != c) {
        W32SetLastError(ERROR_ACCESS_DENIED);
    } else if (how == 1 && !c->Owner) {
        /* the objects of the session stay, those from a resource say they went. */
        ok = (c->Flags & CURSORF_FROMRESOURCE) != 0;
    } else if (how == 1 && (c->Flags & (CURSORF_LRSHARED | CURSORF_SECRET))) {
        ok = TRUE;
    } else if (how == 1 && c->Owner != p) {
        W32SetLastError(ERROR_DESTROY_OBJECT_OF_OTHER_THREAD);
    } else {
        /* one a thread shows is marked and goes when it stops showing it, and the call says it failed. */
        ok = c->Header.RefCount == 1;
        destroy_now(c, how);
    }
    UserLeave();
    return ok;
}

/* the system's objects and those of a process that ends. */
void CursorFreeProcess(W32PROCESS *p)
{
    ULONG index = 0;
    for (USER_OBJECT *o; (o = UserNextObject(&index, USER_TYPE_CURSOR)) != nullptr;) {
        CURICON *c = (CURICON *)o;
        if (c->Owner != p || o->Destroyed)
            continue;
        unlink(c);
        c->Owner = nullptr;
        c->Root = c;
        c->NextDpi = nullptr;
        o->Destroyed = TRUE;
        release(c);
    }
    p->Cursors = nullptr;
}

void CursorFreeThread(W32THREAD *t)
{
    CURICON *c = t->CursorLock;
    t->CursorLock = nullptr;
    if (c)
        release(c);
}

/* ------------------------------------------------------------------ the system's objects */

static const UCHAR *resource_entry(const UCHAR *base, ULONG size, ULONG directory, ULONG id, BOOLEAN first)
{
    if (directory + 16 > size)
        return nullptr;
    ULONG count = *(const USHORT *)(base + directory + 12) + *(const USHORT *)(base + directory + 14);
    if (directory + 16 + (ULONG64)count * 8 > size)
        return nullptr;
    for (ULONG i = 0; i < count; i++) {
        const UCHAR *e = base + directory + 16 + i * 8;
        if (first || *(const ULONG *)e == id)
            return e;
    }
    return nullptr;
}

/* the bytes of a resource of a file read whole, by the ids of its type and its name. */
static const UCHAR *file_resource(const UCHAR *file, SIZE_T size, ULONG type, ULONG id, ULONG *length)
{
    if (size < 0x200 || *(const USHORT *)file != 0x5A4D)
        return nullptr;
    ULONG pe = *(const ULONG *)(file + 0x3c);
    if (pe + 0x108 > size || *(const USHORT *)(file + pe + 24) != 0x20b)
        return nullptr;
    ULONG sections = *(const USHORT *)(file + pe + 6), optional = *(const USHORT *)(file + pe + 20);
    ULONG rva = *(const ULONG *)(file + pe + 24 + 112 + 2 * 8);
    const UCHAR *table = file + pe + 24 + optional;
    if ((SIZE_T)(table - file) + (SIZE_T)sections * 40 > size)
        return nullptr;
    /* the resources are one section, and what they name is inside it. */
    for (ULONG i = 0; i < sections; i++) {
        const UCHAR *sh = table + i * 40;
        ULONG va = *(const ULONG *)(sh + 12), raw_size = *(const ULONG *)(sh + 16), raw = *(const ULONG *)(sh + 20);
        if (rva < va || rva >= va + raw_size || (SIZE_T)raw + raw_size > size)
            continue;
        const UCHAR *base = file + raw + (rva - va);
        ULONG room = raw_size - (rva - va);
        const UCHAR *e = resource_entry(base, room, 0, type, FALSE);
        if (e && (*(const ULONG *)(e + 4) & 0x80000000))
            e = resource_entry(base, room, *(const ULONG *)(e + 4) & 0x7FFFFFFF, id, FALSE);
        if (e && (*(const ULONG *)(e + 4) & 0x80000000))
            e = resource_entry(base, room, *(const ULONG *)(e + 4) & 0x7FFFFFFF, 0, TRUE);
        if (!e || (*(const ULONG *)(e + 4) & 0x80000000) || *(const ULONG *)(e + 4) + 16 > room)
            return nullptr;
        const UCHAR *data = base + *(const ULONG *)(e + 4);
        ULONG data_rva = *(const ULONG *)data, data_size = *(const ULONG *)(data + 4);
        if (data_rva < va || data_rva - va > raw_size || data_size > raw_size - (data_rva - va))
            return nullptr;
        *length = data_size;
        return file + raw + (data_rva - va);
    }
    return nullptr;
}

static PUCHAR read_file(PCWSTR path, SIZE_T *size)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, nullptr, 0, FILE_SHARE_READ, FILE_OPEN,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, nullptr, 0)))
        return nullptr;
    FILE_STANDARD_INFORMATION info;
    PUCHAR file = nullptr;
    if (NT_SUCCESS(ZwQueryInformationFile(h, &iosb, &info, sizeof(info), FileStandardInformation)) &&
        info.EndOfFile.QuadPart > 0 && info.EndOfFile.QuadPart < 32 * 1024 * 1024) {
        *size = (SIZE_T)info.EndOfFile.QuadPart;
        file = ExAllocatePool2(POOL_FLAG_PAGED, *size, 'cIsU');
        LARGE_INTEGER off = {};
        if (file && (!NT_SUCCESS(ZwReadFile(h, nullptr, nullptr, nullptr, &iosb, file, (ULONG)*size, &off, nullptr)) ||
                     iosb.Information != *size)) {
            ExFreePool(file);
            file = nullptr;
        }
    }
    ZwClose(h);
    return file;
}

/*
 * the bitmaps of an icon or cursor resource, as ConvertDIBIcon makes them: a monochrome one keeps its mask over its
 * picture in one bitmap twice as tall, any other has a mask and a 32 bpp picture of the screen's depth.
 */
static BOOLEAN decode_dib(const UCHAR *dib, ULONG length, LONG w, LONG h, SURFACE **mask_out, SURFACE **color_out,
                          ULONG *bpp_out)
{
    if (length < 40 || *(const ULONG *)dib != 40)
        return FALSE;
    LONG dw = *(const LONG *)(dib + 4), dh = *(const LONG *)(dib + 8) / 2;
    ULONG bpp = *(const USHORT *)(dib + 14), compression = *(const ULONG *)(dib + 16);
    ULONG colors = *(const ULONG *)(dib + 32);
    if (dw <= 0 || dh <= 0 || dw > 256 || dh > 256 || compression || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 32))
        return FALSE;
    if (bpp <= 8 && !colors)
        colors = 1u << bpp;
    ULONG stride = ((dw * bpp + 31) / 32) * 4, mstride = ((dw + 31) / 32) * 4;
    const UCHAR *palette = dib + 40, *xor_bits = palette + (bpp <= 8 ? colors * 4 : 0);
    const UCHAR *and_bits = xor_bits + stride * dh;
    if ((SIZE_T)(and_bits - dib) + (SIZE_T)mstride * dh > length)
        return FALSE;
    BOOLEAN mono = bpp == 1;
    SURFACE *mask = SurfaceCreate(w, mono ? 2 * h : h, 1, 0);
    SURFACE *color = mono ? nullptr : SurfaceCreate(w, h, 32, 0);
    if (!mask || (!mono && !color)) {
        SurfaceDestroy(mask);
        SurfaceDestroy(color);
        return FALSE;
    }
    for (LONG y = 0; y < h; y++) {
        LONG sy = (LONG)(((LONG64)(2 * y + 1) * dh) / (2 * (LONG64)h)), row = dh - 1 - sy;
        for (LONG x = 0; x < w; x++) {
            LONG sx = (LONG)(((LONG64)(2 * x + 1) * dw) / (2 * (LONG64)w));
            ULONG a = and_bits[row * mstride + sx / 8] >> (7 - sx % 8) & 1;
            SurfacePutPixel(mask, x, y, a ? 0xFFFFFF : 0);
            const UCHAR *p = xor_bits + row * stride;
            ULONG px;
            if (bpp == 32) {
                px = *(const ULONG *)(p + sx * 4);
            } else {
                ULONG index = bpp == 8 ? p[sx] : bpp == 4 ? (p[sx / 2] >> (sx % 2 ? 0 : 4)) & 15 : p[sx / 8] >> (7 - sx % 8) & 1;
                px = index < colors ? *(const ULONG *)(palette + index * 4) & 0xFFFFFF : 0;
            }
            if (mono)
                SurfacePutPixel(mask, x, y + h, px & 0xFFFFFF ? 0xFFFFFF : 0);
            else
                SurfacePutPixel(color, x, y, px);
        }
    }
    *mask_out = mask;
    *color_out = color;
    *bpp_out = bpp;
    return TRUE;
}

/* the picture of a group nearest the size: the same width first, then the deepest that is not a png. */
static const UCHAR *pick_entry(const UCHAR *file, SIZE_T size, ULONG group_type, ULONG group, LONG want,
                               ULONG *length, SHORT *hx, SHORT *hy)
{
    ULONG group_size = 0;
    const UCHAR *g = file_resource(file, size, group_type, group, &group_size);
    ULONG count = g && group_size >= 6 ? *(const USHORT *)(g + 4) : 0;
    LONG best = -1, best_score = 0x7FFFFFFF;
    for (ULONG i = 0; i < count && 6 + (i + 1) * 14 <= group_size; i++) {
        const UCHAR *e = g + 6 + i * 14;
        LONG width = group_type == 12 ? *(const USHORT *)e : (e[0] ? e[0] : 256);
        ULONG bits = *(const USHORT *)(e + 6);
        LONG score = (width == want ? 0 : width > want ? (width - want) * 64 : (want - width) * 128) - (LONG)bits;
        if (score < best_score) {
            best_score = score;
            best = (LONG)i;
        }
    }
    if (best < 0)
        return nullptr;
    const UCHAR *e = g + 6 + best * 14;
    ULONG id = *(const USHORT *)(e + 12);
    const UCHAR *data = file_resource(file, size, group_type == 12 ? 1 : 3, id, length);
    if (!data)
        return nullptr;
    if (group_type == 12) {
        if (*length < 4)
            return nullptr;
        *hx = *(const SHORT *)data;
        *hy = *(const SHORT *)(data + 2);
        *length -= 4;
        return data + 4;
    }
    return data;
}

static BOOLEAN set_picture(CURICON *c, const UCHAR *dib, ULONG length, LONG want, SHORT hx, SHORT hy);
static void fill_scheme(void);

static void fill_system(const UCHAR *file, SIZE_T size)
{
    LONG cursor_size = (LONG)system_cursor_size(), icon_size = MetricsGet(SM_CXICON, W32SystemDpi());
    for (ULONG i = 0; i < SYSTEM_CURSORS + SYSTEM_ICONS; i++) {
        CURICON *c = g_system[i];
        if (!c || c->Mask)
            continue;
        BOOLEAN cursor = i < SYSTEM_CURSORS;
        LONG want = cursor ? cursor_size : icon_size;
        ULONG length = 0;
        SHORT hx = 0, hy = 0;
        const UCHAR *dib = pick_entry(file, size, cursor ? 12 : 14, cursor ? g_system_cursor[i].Group : 100 + (i - SYSTEM_CURSORS),
                                      want, &length, &hx, &hy);
        if (!dib)
            continue;
        if (!cursor) {
            hx = (SHORT)(*(const LONG *)(dib + 4) / 2);
            hy = hx;
        }
        set_picture(c, dib, length, want, hx, hy);
        c->DpiSize = (ULONG)cursor_size;
    }
}

/* the pictures of an object from a dib of an icon or cursor, at want pixels, its hot spot scaled with it. */
static BOOLEAN set_picture(CURICON *c, const UCHAR *dib, ULONG length, LONG want, SHORT hx, SHORT hy)
{
    SURFACE *mask = nullptr, *color = nullptr;
    ULONG bpp = 0;
    if (!decode_dib(dib, length, want, want, &mask, &color, &bpp))
        return FALSE;
    LONG dw = *(const LONG *)(dib + 4);
    c->HotX = (SHORT)W32MulDiv(hx, want, dw);
    c->HotY = (SHORT)W32MulDiv(hy, want, dw);
    c->Cx = (ULONG)want;
    c->Cy = (ULONG)want * 2;
    c->Bpp = bpp;
    if (!GdiCreateHandle(&mask->Header, GDI_TYPE_BITMAP) || (color && !GdiCreateHandle(&color->Header, GDI_TYPE_BITMAP))) {
        SurfaceDestroy(mask);
        SurfaceDestroy(color);
        return FALSE;
    }
    make_public(mask);
    c->Mask = mask;
    if (color) {
        make_public(color);
        c->Color = color;
    }
    c->Alpha = alpha_of(c->Color);
    c->Image = build_image(c);
    return TRUE;
}

/*
 * the icon of a program for a window that has none: the first icon group of its file, as the taskbar of windows
 * shows. only the headers and the resource section are read, a program can be far larger than its resources.
 */
static void *png_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return ExAllocatePool2(POOL_FLAG_PAGED, size ? size : 1, 'cIsU');
}

static void png_free(void *ctx, void *p)
{
    (void)ctx;
    if (p)
        ExFreePool(p);
}

static const MimeAllocator g_png_alloc = {png_alloc, png_free, nullptr};

static BOOLEAN read_part(HANDLE h, ULONG64 offset, PVOID buffer, ULONG length)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = (LONGLONG)offset;
    return NT_SUCCESS(ZwReadFile(h, nullptr, nullptr, nullptr, &iosb, buffer, length, &off, nullptr)) && iosb.Information == length;
}

HICON CursorProcessIcon(W32PROCESS *p)
{
    if (!p || p->ExeIconTried || !p->ImageChars)
        return p ? p->ExeIcon : nullptr;
    p->ExeIconTried = TRUE;
    WCHAR path[300] = L"\\??\\";
    if (p->ImageChars + 5 > RTL_NUMBER_OF(path))
        return nullptr;
    memcpy(path + 4, p->ImagePath, p->ImageChars * sizeof(WCHAR));
    path[4 + p->ImageChars] = 0;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, nullptr, 0, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, nullptr, 0)))
        return nullptr;
    enum { HEAD = 0x1000 };
    PUCHAR head = ExAllocatePool2(POOL_FLAG_PAGED, HEAD, 'cIsU'), rs = nullptr;
    ULONG pe, va = 0, raw_size = 0, raw = 0;
    if (!head || !read_part(h, 0, head, HEAD) || *(const USHORT *)head != 0x5A4D || (pe = *(const ULONG *)(head + 0x3c)) > 0xC00)
        goto done;
    USHORT magic = *(const USHORT *)(head + pe + 24);
    if (magic != 0x20b && magic != 0x10b)
        goto done;
    ULONG sections = *(const USHORT *)(head + pe + 6), optional = *(const USHORT *)(head + pe + 20);
    ULONG rva = *(const ULONG *)(head + pe + 24 + (magic == 0x20b ? 112 : 96) + 2 * 8);
    const UCHAR *table = head + pe + 24 + optional;
    for (ULONG i = 0; rva && i < sections && table + (i + 1) * 40 <= head + HEAD; i++) {
        const UCHAR *sh = table + i * 40;
        ULONG sva = *(const ULONG *)(sh + 12), ssize = *(const ULONG *)(sh + 16);
        if (rva >= sva && rva < sva + ssize) {
            va = sva;
            raw_size = ssize;
            raw = *(const ULONG *)(sh + 20);
        }
    }
    if (!raw_size || raw_size > 64 * 1024 * 1024 || !(rs = ExAllocatePool2(POOL_FLAG_PAGED, raw_size, 'cIsU')) ||
        !read_part(h, raw, rs, raw_size))
        goto done;
    const UCHAR *base = rs + (rva - va);
    ULONG room = raw_size - (rva - va);
    /* the first group, whatever its name and language. */
    const UCHAR *e = resource_entry(base, room, 0, 14, FALSE);
    for (int level = 0; level < 2 && e && (*(const ULONG *)(e + 4) & 0x80000000); level++)
        e = resource_entry(base, room, *(const ULONG *)(e + 4) & 0x7FFFFFFF, 0, TRUE);
    if (!e || (*(const ULONG *)(e + 4) & 0x80000000) || *(const ULONG *)(e + 4) + 16 > room)
        goto done;
    const UCHAR *data = base + *(const ULONG *)(e + 4);
    ULONG group_rva = *(const ULONG *)data, group_size = *(const ULONG *)(data + 4);
    if (group_rva < va || group_rva - va > raw_size || group_size > raw_size - (group_rva - va) || group_size < 6)
        goto done;
    const UCHAR *g = rs + (group_rva - va);
    ULONG count = *(const USHORT *)(g + 4);
    LONG want = 64, best = -1, best_score = 0x7FFFFFFF;
    for (ULONG i = 0; i < count && 6 + (i + 1) * 14 <= group_size; i++) {
        const UCHAR *entry = g + 6 + i * 14;
        LONG width = entry[0] ? entry[0] : 256;
        ULONG bits = *(const USHORT *)(entry + 6);
        LONG score = (width == want ? 0 : width > want ? (width - want) * 64 : (want - width) * 128) - (LONG)bits;
        if (score < best_score) {
            best_score = score;
            best = (LONG)i;
        }
    }
    if (best < 0)
        goto done;
    ULONG id = *(const USHORT *)(g + 6 + best * 14 + 12);
    e = resource_entry(base, room, 0, 3, FALSE);
    if (e && (*(const ULONG *)(e + 4) & 0x80000000))
        e = resource_entry(base, room, *(const ULONG *)(e + 4) & 0x7FFFFFFF, id, FALSE);
    if (e && (*(const ULONG *)(e + 4) & 0x80000000))
        e = resource_entry(base, room, *(const ULONG *)(e + 4) & 0x7FFFFFFF, 0, TRUE);
    if (!e || (*(const ULONG *)(e + 4) & 0x80000000) || *(const ULONG *)(e + 4) + 16 > room)
        goto done;
    data = base + *(const ULONG *)(e + 4);
    ULONG icon_rva = *(const ULONG *)data, icon_size = *(const ULONG *)(data + 4);
    if (icon_rva < va || icon_rva - va > raw_size || icon_size > raw_size - (icon_rva - va) || icon_size < 40)
        goto done;
    const UCHAR *dib = rs + (icon_rva - va);
    CURICON *c = create_object(p);
    if (!c)
        goto done;
    c->Type = 3;
    MimePngInfo info;
    if (mime_png_is_png(dib, icon_size)) {
        /* a png picture: the mask comes from its alpha, as user32 makes it. */
        SURFACE *color = nullptr, *mask = nullptr;
        if (mime_png_info(dib, icon_size, &info) && info.width && info.width <= 256 && info.height == info.width &&
            (color = SurfaceCreate((LONG)info.width, (LONG)info.height, 32, 0)) &&
            (mask = SurfaceCreate((LONG)info.width, (LONG)info.height, 1, 0)) &&
            mime_png_decode(dib, icon_size, &g_png_alloc, 0, color->Bits, (size_t)color->Stride) == MIME_PNG_OK) {
            for (LONG y = 0; y < (LONG)info.height; y++)
                for (LONG x = 0; x < (LONG)info.width; x++)
                    SurfacePutPixel(mask, x, y, SurfaceGetPixel(color, x, y) >> 24 ? 0 : 0xFFFFFF);
            c->Cx = info.width;
            c->Cy = info.width * 2;
            c->HotX = c->HotY = (SHORT)(info.width / 2);
            c->Bpp = 32;
            if (GdiCreateHandle(&mask->Header, GDI_TYPE_BITMAP) && GdiCreateHandle(&color->Header, GDI_TYPE_BITMAP)) {
                make_public(mask);
                make_public(color);
                c->Mask = mask;
                c->Color = color;
                c->Alpha = alpha_of(c->Color);
                c->Image = build_image(c);
                p->ExeIcon = (HICON)c->Header.Handle;
                color = mask = nullptr;
            }
        }
        SurfaceDestroy(color);
        SurfaceDestroy(mask);
    } else {
        LONG side = *(const LONG *)(dib + 4);
        if (side > 0 && side <= 256 && set_picture(c, dib, icon_size, side, (SHORT)(side / 2), (SHORT)(side / 2)))
            p->ExeIcon = (HICON)c->Header.Handle;
    }
done:
    if (rs)
        ExFreePool(rs);
    if (head)
        ExFreePool(head);
    ZwClose(h);
    return p->ExeIcon;
}

/* ------------------------------------------------------------------ the cursor scheme of the user */

/*
 * the values of HKCU\Control Panel\Cursors that name a file for each system cursor, in the order of g_system_cursor.
 * win32kfull reads them at a logon in xxxUpdateSystemCursorsFromRegistry and loads each file with LoadImage through
 * user32. an empty or missing value keeps the cursor of user32, as on windows, where IBeam and Crosshair are empty.
 */
static const PCWSTR g_scheme_name[SYSTEM_CURSORS] = {
    L"Arrow", L"IBeam", L"Wait", L"Crosshair", L"UpArrow", L"SizeNWSE", L"SizeNESW", L"SizeWE", L"SizeNS",
    L"SizeAll", L"No", L"AppStarting", L"Help", L"NWPen", L"Hand", nullptr, L"Pin", L"Person",
};

NTKERNELAPI extern MimeBootInfo *HalBootInfo;
NTKERNELAPI NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTKERNELAPI NTSTATUS NTAPI ZwQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length,
                                           PULONG returned);

/* the file of a value as an nt path. %SystemRoot% and %windir% are the only variables a scheme of windows uses. */
static BOOLEAN scheme_file(PCWSTR value, WCHAR *path, ULONG chars)
{
    WCHAR key[160];
    _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"\\Registry\\User\\S-1-5-21-%u-%u-%u-1001\\Control Panel\\Cursors",
               HalBootInfo->user_sid[0], HalBootInfo->user_sid[1], HalBootInfo->user_sid[2]);
    key[RTL_NUMBER_OF(key) - 1] = 0;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle;
    RtlInitUnicodeString(&name, key);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    if (!NT_SUCCESS(ZwOpenKey(&handle, KEY_QUERY_VALUE, &oa)))
        return FALSE;
    struct {
        ULONG title, type, size;
        WCHAR data[MAX_PATH];
    } info;
    ULONG got = 0;
    RtlInitUnicodeString(&name, value);
    NTSTATUS status = ZwQueryValueKey(handle, &name, 2, &info, sizeof(info) - sizeof(WCHAR), &got);
    ZwClose(handle);
    if (!NT_SUCCESS(status) || (info.type != REG_SZ && info.type != REG_EXPAND_SZ) || info.size > sizeof(info.data) - 2)
        return FALSE;
    info.data[info.size / sizeof(WCHAR)] = 0;
    PCWSTR rest = nullptr;
    if (!_wcsnicmp(info.data, L"%SystemRoot%", 12))
        rest = info.data + 12;
    else if (!_wcsnicmp(info.data, L"%windir%", 8))
        rest = info.data + 8;
    else if ((info.data[0] | 0x20) == L'c' && info.data[1] == L':' && !_wcsnicmp(info.data + 2, L"\\Windows\\", 9))
        rest = info.data + 10;
    if (!rest || !*rest || wcschr(rest, L'%'))
        return FALSE;
    _snwprintf(path, chars - 1, L"\\SystemRoot%ls", rest);
    path[chars - 1] = 0;
    return TRUE;
}

/* the image of a .cur file nearest want pixels wide, as pick_entry chooses among the images of a resource. */
static BOOLEAN load_cur(CURICON *c, const UCHAR *file, SIZE_T size, LONG want)
{
    if (size < 6 || *(const USHORT *)file != 0 || *(const USHORT *)(file + 2) != 2)
        return FALSE;
    ULONG count = *(const USHORT *)(file + 4);
    LONG best = -1, best_score = 0x7FFFFFFF;
    for (ULONG i = 0; i < count && 6 + (i + 1) * 16 <= size; i++) {
        const UCHAR *e = file + 6 + i * 16;
        ULONG length = *(const ULONG *)(e + 8), offset = *(const ULONG *)(e + 12);
        if (offset > size || length > size - offset || length < 40 || *(const ULONG *)(file + offset) != 40)
            continue; /* a png image. */
        LONG width = e[0] ? e[0] : 256;
        LONG score = width == want ? 0 : width > want ? (width - want) * 64 : (want - width) * 128;
        if (score < best_score) {
            best_score = score;
            best = (LONG)i;
        }
    }
    if (best < 0)
        return FALSE;
    const UCHAR *e = file + 6 + best * 16;
    return set_picture(c, file + *(const ULONG *)(e + 12), *(const ULONG *)(e + 8), want, *(const SHORT *)(e + 4),
                       *(const SHORT *)(e + 6));
}

static const UCHAR *riff_find(const UCHAR *at, const UCHAR *end, const char *tag, ULONG *length)
{
    while (end - at >= 8) {
        ULONG n = *(const ULONG *)(at + 4);
        if ((ULONG)(end - at - 8) < n)
            return nullptr;
        if (!memcmp(at, tag, 4)) {
            *length = n;
            return at + 8;
        }
        at += 8 + n + (n & 1);
    }
    return nullptr;
}

/* an animated cursor: its frames are .cur files of their own, shown in the sequence for the rates in jiffies. */
static BOOLEAN load_ani(CURICON *c, const UCHAR *file, SIZE_T size, LONG want)
{
    if (size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "ACON", 4))
        return FALSE;
    const UCHAR *end = file + 8 + (*(const ULONG *)(file + 4) < size - 8 ? *(const ULONG *)(file + 4) : size - 8);
    ULONG length = 0, rate_length = 0, seq_length = 0;
    const UCHAR *anih = riff_find(file + 12, end, "anih", &length);
    if (!anih || length < 36)
        return FALSE;
    ULONG frames = *(const ULONG *)(anih + 4), steps = *(const ULONG *)(anih + 8), jiffies = *(const ULONG *)(anih + 28);
    if (!frames || !steps || frames > 256 || steps > 1024)
        return FALSE;
    const UCHAR *rates = riff_find(file + 12, end, "rate", &rate_length);
    const UCHAR *sequence = riff_find(file + 12, end, "seq ", &seq_length);
    const UCHAR *list = file + 12;
    ULONG list_length = 0;
    while ((list = riff_find(list, end, "LIST", &list_length)) && (list_length < 4 || memcmp(list, "fram", 4)))
        list += list_length + (list_length & 1);
    if (!list)
        return FALSE;
    PUCHAR buffer = W32Alloc(8 * frames + 8 * steps);
    if (!buffer)
        return FALSE;
    CURICON **objects = (CURICON **)buffer;
    ULONG *rate = (ULONG *)(buffer + 8 * frames);
    LONG *seq = (LONG *)(buffer + 8 * frames + 4 * steps);
    for (ULONG i = 0; i < steps; i++) {
        rate[i] = rates && rate_length >= 4 * steps ? ((const ULONG *)rates)[i] : jiffies;
        seq[i] = sequence && seq_length >= 4 * steps ? ((const LONG *)sequence)[i] : (LONG)(i % frames);
        if ((ULONG)seq[i] >= frames)
            seq[i] = 0;
    }
    const UCHAR *at = list + 4, *list_end = list + list_length;
    ULONG made = 0;
    for (; made < frames; made++) {
        ULONG n = 0;
        const UCHAR *icon = riff_find(at, list_end, "icon", &n);
        CURICON *f = icon ? create_object(nullptr) : nullptr;
        if (!f)
            break;
        f->Flags = CURSORF_ACONFRAME | CURSORF_GLOBAL;
        f->Type = 1;
        f->Awareness = DPI_AWARE_UNAWARE;
        f->DpiSize = (ULONG)want;
        f->Header.RefCount++; /* the reference of the animated cursor, as set_animated takes it. */
        objects[made] = f;
        if (!load_cur(f, icon, n, want)) {
            made++;
            break;
        }
        at = icon + n + (n & 1);
    }
    if (made < frames || !objects[frames - 1]->Mask) {
        for (ULONG i = 0; i < made; i++) {
            objects[i]->Header.RefCount--;
            destroy_now(objects[i], 0);
        }
        W32Free(buffer);
        return FALSE;
    }
    c->Flags |= CURSORF_ACON;
    c->Frames = objects;
    c->FrameCount = frames;
    c->StepCount = steps;
    c->Rates = rate;
    c->Sequence = seq;
    c->Step = 0;
    c->HotX = objects[0]->HotX;
    c->HotY = objects[0]->HotY;
    c->Cx = objects[0]->Cx;
    c->Cy = objects[0]->Cy;
    c->Bpp = objects[0]->Bpp;
    return TRUE;
}

static void fill_scheme(void)
{
    LONG want = (LONG)system_cursor_size();
    for (ULONG i = 0; i < SYSTEM_CURSORS; i++) {
        CURICON *c = g_system[i];
        WCHAR path[MAX_PATH + 16];
        if (!c || c->Mask || (c->Flags & CURSORF_ACON) || !g_scheme_name[i] || !scheme_file(g_scheme_name[i], path, RTL_NUMBER_OF(path)))
            continue;
        SIZE_T size = 0;
        PUCHAR file = read_file(path, &size);
        if (!file)
            continue;
        if (load_ani(c, file, size, want) || load_cur(c, file, size, want))
            c->DpiSize = (ULONG)want;
        ExFreePool(file);
    }
}

/* the objects of the session exist from the first call that needs them, their pictures from when user32 can be read. */
static void system_objects(void)
{
    if (!g_system_made) {
        g_system_made = TRUE;
        for (ULONG i = 0; i < SYSTEM_CURSORS + SYSTEM_ICONS; i++) {
            BOOLEAN cursor = i < SYSTEM_CURSORS;
            CURICON *c = create_object(nullptr);
            if (!c)
                continue;
            ULONG id = cursor ? g_system_cursor[i].Id : g_system_icon[i - SYSTEM_CURSORS];
            c->Flags = CURSORF_FROMRESOURCE | CURSORF_GLOBAL | CURSORF_LRSHARED | (cursor ? CURSORF_SYSTEM : 0);
            c->Type = cursor ? 1 : 3;
            c->Name.Buffer = (PWSTR)(ULONG_PTR)id;
            c->Module = AtomAdd(L"USER32");
            c->Awareness = DPI_AWARE_UNAWARE;
            c->DpiSize = system_cursor_size();
            if (cursor)
                c->System = host_cursor_for_idc(id) + 1;
            link(c);
            g_system[i] = c;
        }
    }
    if (!g_system_filled) {
        fill_scheme();
        SIZE_T size = 0;
        PUCHAR file = read_file(L"\\SystemRoot\\System32\\user32.dll", &size);
        if (file) {
            g_system_filled = TRUE;
            fill_system(file, size);
            ExFreePool(file);
        }
    }
}

/* the cursor of a built in class: one of the system's, which no process owns and the host draws. */
HCURSOR CursorSystem(ULONG idc)
{
    system_objects();
    for (ULONG i = 0; i < SYSTEM_CURSORS; i++)
        if (g_system_cursor[i].Id == idc && g_system[i])
            return (HCURSOR)g_system[i]->Header.Handle;
    return g_system[0] ? (HCURSOR)g_system[0]->Header.Handle : nullptr;
}

/* an icon of the system by its IDI_ number, the one LoadIcon gives with no module. */
HICON CursorSystemIcon(ULONG idi)
{
    system_objects();
    for (ULONG i = 0; i < SYSTEM_ICONS; i++)
        if (g_system_icon[i] == idi && g_system[SYSTEM_CURSORS + i])
            return (HICON)g_system[SYSTEM_CURSORS + i]->Header.Handle;
    return nullptr;
}

/*
 * xxxCreateClassSmIcon and xxxCreateWindowSmIcon of win32kfull 26300: user32 copies an icon at the small size of the
 * system dpi, from its resource when it has one. the copy of a window is marked as the one win32k made.
 */
HICON CursorSmallCopy(HICON icon, BOOLEAN window)
{
    CURICON *c = lookup(icon);
    if (!c)
        return nullptr;
    UINT dpi = W32SystemDpi();
    HICON copy = ClientCopyImage(icon, c->Type != 3 ? IMAGE_CURSOR : IMAGE_ICON, MetricsGet(SM_CXSMICON, dpi),
                                 MetricsGet(SM_CYSMICON, dpi), LR_COPYFROMRESOURCE);
    CURICON *made = lookup(copy);
    if (!made)
        return nullptr;
    if (window)
        made->Flags |= CURSORF_SECRET;
    return copy;
}

/* a copy win32k made for a window, which nobody else may destroy. */
BOOLEAN CursorIsSmallCopy(HICON icon)
{
    CURICON *c = lookup(icon);
    return c && (c->Flags & CURSORF_SECRET);
}

void CursorDestroySmallCopy(HICON icon)
{
    CURICON *c = lookup(icon);
    if (c)
        destroy_now(c, 0);
}

/* ------------------------------------------------------------------ reading them */

/* the bitmaps GetIconInfo hands out, owned by the caller: see _InternalGetIconInfo. */
static BOOL icon_info(CURICON *c, ICONINFO *info, UNICODE_STRING *module, PWSTR module_buffer,
                      UNICODE_STRING *resource, PWSTR resource_buffer, ULONG *bpp, BOOLEAN internal)
{
    if (c->Flags & CURSORF_ACON) {
        if (!c->Frames || !c->FrameCount)
            return FALSE;
        c = c->Frames[0];
    }
    LONG cx, cy;
    virtual_size(c, &cx, &cy);
    BOOLEAN halve = c->Color && !internal;
    LONG mask_h = halve ? cy / 2 : cy;
    if (cx <= 0 || mask_h <= 0 || !c->Mask) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG src_h = halve ? (LONG)c->Cy / 2 : (LONG)c->Cy;
    SURFACE *mask = copy_bits(c->Mask, 0, 0, (LONG)c->Cx, W32Min(src_h, c->Mask->Height), cx, mask_h, 1);
    SURFACE *color = nullptr;
    if (mask && c->Color)
        color = copy_bits(c->Color, 0, 0, (LONG)c->Cx, (LONG)c->Cy / 2, cx, cy / 2, 32);
    if (!mask || (c->Color && !color) || !GdiCreateHandle(&mask->Header, GDI_TYPE_BITMAP) ||
        (color && !GdiCreateHandle(&color->Header, GDI_TYPE_BITMAP))) {
        SurfaceDestroy(mask);
        SurfaceDestroy(color);
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    info->fIcon = c->Type == 3;
    info->xHotspot = (DWORD)W32MulDiv(c->HotX, cx, (LONG)c->Cx);
    info->yHotspot = (DWORD)W32MulDiv(c->HotY, cx, (LONG)c->Cx);
    info->hbmMask = (HBITMAP)mask->Header.Handle;
    info->hbmColor = color ? (HBITMAP)color->Header.Handle : nullptr;
    /* the module comes back in characters, like UserGetAtomName answers. */
    if (module) {
        ULONG chars = module->MaximumLength / sizeof(WCHAR);
        module->Length = c->Module && chars ? (USHORT)AtomGetName(c->Module, module_buffer, chars) : 0;
    }
    if (resource) {
        if (!IS_INTRESOURCE(c->Name.Buffer)) {
            USHORT n = W32Min(c->Name.Length, resource->MaximumLength);
            memcpy(resource_buffer, c->Name.Buffer, n);
            if (n + sizeof(WCHAR) <= resource->MaximumLength)
                resource_buffer[n / sizeof(WCHAR)] = 0;
            resource->Length = n;
        } else {
            *resource = c->Name;
        }
    }
    if (bpp)
        *bpp = c->Bpp;
    return TRUE;
}

BOOL NTAPI NtUserGetIconInfo(HICON icon, ICONINFO *user_info, PUNICODE_STRING user_module, PUNICODE_STRING user_resource,
                             LPDWORD user_bpp, BOOL internal)
{
    UNICODE_STRING module = {}, resource = {};
    PWSTR module_buffer = nullptr, resource_buffer = nullptr;
    ICONINFO info = {};
    ULONG bpp = 0;
    BOOL ok = FALSE;
    UserEnter();
    system_objects();
    CURICON *c = validate(icon);
    if (!c)
        goto done;
    c = dpi_variant(c);
    PVOID module_user = nullptr, resource_user = nullptr;
    if (user_module) {
        if (!NT_SUCCESS(MimeCopyFromUser(&module, user_module, sizeof(module))))
            goto fault;
        module_user = module.Buffer;
        module_buffer = W32Alloc(module.MaximumLength + sizeof(WCHAR));
        if (!module_buffer)
            goto fault;
    }
    if (user_resource) {
        if (!NT_SUCCESS(MimeCopyFromUser(&resource, user_resource, sizeof(resource))))
            goto fault;
        resource_user = resource.Buffer;
        resource_buffer = W32Alloc(resource.MaximumLength + sizeof(WCHAR));
        if (!resource_buffer)
            goto fault;
    }
    if ((ULONG_PTR)user_info & 7)
        goto fault;
    module.Buffer = module_buffer;
    resource.Buffer = resource_buffer;
    if (!icon_info(c, &info, user_module ? &module : nullptr, module_buffer, user_resource ? &resource : nullptr,
                   resource_buffer, user_bpp ? &bpp : nullptr, internal != 0))
        goto done;
    ok = TRUE;
    BOOLEAN fault = !NT_SUCCESS(MimeCopyToUser(user_info, &info, sizeof(info)));
    if (user_module) {
        if (module.Length)
            fault |= !NT_SUCCESS(MimeCopyToUser(module_user, module_buffer, (module.Length + 1) * sizeof(WCHAR)));
        module.Buffer = module_user;
        fault |= !NT_SUCCESS(MimeCopyToUser(user_module, &module, sizeof(module)));
    }
    if (user_resource) {
        if (resource.Buffer == resource_buffer) {
            if (resource.Length)
                fault |= !NT_SUCCESS(MimeCopyToUser(resource_user, resource_buffer,
                                                    W32Min(resource.Length + sizeof(WCHAR), resource.MaximumLength)));
            resource.Buffer = resource_user;
        }
        fault |= !NT_SUCCESS(MimeCopyToUser(user_resource, &resource, sizeof(resource)));
    }
    if (user_bpp)
        fault |= !NT_SUCCESS(MimeCopyToUser(user_bpp, &bpp, sizeof(bpp)));
    if (fault) {
        ok = FALSE;
        goto fault;
    }
    goto done;
fault:
    W32SetLastError(ERROR_NOACCESS);
done:
    UserLeave();
    if (module_buffer)
        W32Free(module_buffer);
    if (resource_buffer)
        W32Free(resource_buffer);
    return ok;
}

/* the size, the height twice that of the picture, of the frame of a step. */
BOOL NTAPI NtUserGetIconSize(HICON icon, UINT step, LONG *user_cx, LONG *user_cy)
{
    UserEnter();
    CURICON *c = validate(icon);
    BOOL ok = FALSE;
    if (c && (c = frame_of(dpi_variant(c), (LONG)step)) != nullptr) {
        LONG cx, cy;
        virtual_size(c, &cx, &cy);
        if (NT_SUCCESS(MimeCopyToUser(user_cx, &cx, sizeof(cx))) && NT_SUCCESS(MimeCopyToUser(user_cy, &cy, sizeof(cy))))
            ok = TRUE;
        else
            W32SetLastError(ERROR_NOACCESS);
    }
    UserLeave();
    return ok;
}

/* the frame of a step, its rate in jiffies and the number of steps; one step of rate 0 for a still one. */
HCURSOR NTAPI NtUserGetCursorFrameInfo(HCURSOR cursor, UINT step, DWORD *user_rate, DWORD *user_steps)
{
    UserEnter();
    CURICON *c = validate(cursor);
    HCURSOR result = nullptr;
    if (c) {
        c = dpi_variant(c);
        DWORD rate = 0, steps = 1;
        CURICON *f = c;
        if (c->Flags & CURSORF_ACON) {
            f = frame_of(c, (LONG)step);
            if (f) {
                steps = c->StepCount;
                rate = c->Rates[step];
            }
        }
        if (f) {
            if (NT_SUCCESS(MimeCopyToUser(user_rate, &rate, sizeof(rate))) &&
                NT_SUCCESS(MimeCopyToUser(user_steps, &steps, sizeof(steps))))
                result = (HCURSOR)f->Header.Handle;
            else
                W32SetLastError(ERROR_NOACCESS);
        }
    }
    UserLeave();
    return result;
}

/* the sizes a cursor needs variants for: only the size of the system's at the system dpi, which it has. */
BOOL NTAPI NtUserGetRequiredCursorSizes(HCURSOR cursor, PVOID user_sizes)
{
    UserEnter();
    CURICON *c = validate(cursor);
    BOOL ok = FALSE;
    if (c && c->Root == c) {
        ULONG sizes[10] = {1, system_cursor_size()};
        if (NT_SUCCESS(MimeCopyToUser(user_sizes, sizes, sizeof(sizes)))) {
            c->Flags |= CURSORF_SIZES;
            ok = TRUE;
        } else {
            W32SetLastError(ERROR_NOACCESS);
        }
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserLinkDpiCursor(HCURSOR root_handle, HCURSOR variant_handle, UINT size)
{
    UserEnter();
    BOOL ok = FALSE;
    CURICON *root = size ? lookup(root_handle) : nullptr, *v = root ? lookup(variant_handle) : nullptr;
    if (size && root && !v)
        W32SetLastError(ERROR_INVALID_CURSOR_HANDLE);
    else if (!size || !root || !(v->Flags & CURSORF_DPISCALED) || (root->Flags & (CURSORF_INIT | CURSORF_DPISCALED)) ||
             v->Root != v || v->NextDpi) {
        if (!size || v)
            W32SetLastError(ERROR_INVALID_PARAMETER);
    } else {
        BOOLEAN found = FALSE;
        for (CURICON *e = root->Root; e && !found; e = e->NextDpi)
            found = e->DpiSize == size;
        if (found) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
        } else {
            v->Root = root;
            v->NextDpi = root->NextDpi;
            root->NextDpi = v;
            v->DpiSize = size;
            ok = TRUE;
        }
    }
    UserLeave();
    return ok;
}

/* ------------------------------------------------------------------ copies for the window manager */

/* a copy that the calling process owns, without name or module, see _DuplicateCursor. called with the lock held. */
static HICON duplicate(CURICON *from)
{
    if (from->Flags & CURSORF_ACON)
        from = from->FrameCount ? from->Frames[0] : nullptr;
    if (!from || !from->Mask)
        return nullptr;
    CURICON *c = create_object(W32GetProcess());
    if (!c)
        return nullptr;
    c->Flags &= ~CURSORF_INIT;
    c->Type = from->Type;
    c->HotX = from->HotX;
    c->HotY = from->HotY;
    c->Bpp = from->Bpp;
    c->Cx = from->Cx;
    c->Cy = from->Cy;
    c->DpiSize = system_cursor_size();
    c->Awareness = from->Awareness;
    c->Mask = copy_bits(from->Mask, 0, 0, from->Mask->Width, from->Mask->Height, from->Mask->Width, from->Mask->Height, 1);
    c->Color = from->Color ? copy_bits(from->Color, 0, 0, from->Color->Width, from->Color->Height, from->Color->Width,
                                       from->Color->Height, 32)
                           : nullptr;
    if (!c->Mask || (from->Color && !c->Color) || !GdiCreateHandle(&c->Mask->Header, GDI_TYPE_BITMAP) ||
        (c->Color && !GdiCreateHandle(&c->Color->Header, GDI_TYPE_BITMAP))) {
        destroy_now(c, 0);
        return nullptr;
    }
    make_public(c->Mask);
    if (c->Color)
        make_public(c->Color);
    c->Alpha = alpha_of(c->Color);
    c->Image = build_image(c);
    return (HICON)c->Header.Handle;
}

/*
 * a copy of the icon of a window, which the caller owns: 0 the small one, 1 the big one, 2 the small one or the one
 * of the class. measured on windows 11 26100: every call gives a new handle, another kind is
 * ERROR_INVALID_PARAMETER and no window ERROR_INVALID_WINDOW_HANDLE. windows gives a copy of the icon of programs
 * to a window that has none, the desktop too.
 */
HICON NTAPI NtUserInternalGetWindowIcon(HWND hwnd, UINT kind)
{
    UserEnter();
    system_objects();
    WND *w = WndLookup(hwnd);
    HICON icon = nullptr;
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (kind > 2) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else {
        HICON small = w->IconSmall ? w->IconSmall : w->Class ? w->Class->IconSmall : nullptr;
        HICON big = w->Icon ? w->Icon : w->Class ? w->Class->Icon : nullptr;
        CURICON *from = lookup(kind == 1 ? (big ? big : small) : (small ? small : big));
        if (from)
            icon = duplicate(from);
        /* measured on windows: the picture of 32 pixels at each kind. */
        else if (!big && !small && g_system[SYSTEM_CURSORS])
            icon = duplicate(g_system[SYSTEM_CURSORS]);
    }
    UserLeave();
    return icon;
}

ULONG_PTR NTAPI NtUserGetClassIcoCur(HWND hwnd, int index)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    HANDLE h = nullptr;
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    else if (w->Class)
        h = index == GCLP_HICON ? w->Class->Icon : index == GCLP_HCURSOR ? w->Class->Cursor
                                                 : index == GCLP_HICONSM ? w->Class->IconSmall : nullptr;
    /* the class holds a handle; windows holds the object, so a destroyed one reads as none. */
    if (h && !lookup(h))
        h = nullptr;
    UserLeave();
    return (ULONG_PTR)h;
}

/* ------------------------------------------------------------------ drawing */

static void stretch(DC *dc, const REGION *clip, const RECT *dst, SURFACE *src, LONG sy, LONG sw, LONG sh, DWORD rop)
{
    RECT src_rect = {0, sy, sw, sy + sh};
    StretchSurface(dc->Surface, clip, dst, src, &src_rect, rop, COLORONCOLOR);
}

/* DI_MASK and DI_IMAGE as _DrawIconEx does them: the mask with SRCAND, the picture with SRCINVERT, or alpha. */
static void draw_frame(DC *dc, const REGION *clip, const RECT *dst, CURICON *c, UINT flags)
{
    LONG w = (LONG)c->Cx, h = (LONG)c->Cy / 2;
    if ((flags & DI_NORMAL) == DI_NORMAL && c->Alpha) {
        RECT src = {0, 0, w, h};
        AlphaBlendSurface(dc->Surface, clip, dst, c->Alpha, &src, (BLENDFUNCTION){AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
        return;
    }
    if (flags & DI_MASK)
        stretch(dc, clip, dst, c->Mask, 0, w, h, (flags & DI_IMAGE) ? SRCAND : SRCCOPY);
    if (flags & DI_IMAGE) {
        DWORD rop = (flags & DI_MASK) ? SRCINVERT : SRCCOPY;
        if (c->Color)
            stretch(dc, clip, dst, c->Color, 0, w, h, rop);
        else
            stretch(dc, clip, dst, c->Mask, h, w, h, rop);
    }
}

static BOOL draw(DC *dc, LONG x, LONG y, CURICON *c, LONG cx, LONG cy, UINT step, BRUSH *brush, UINT flags)
{
    CURICON *f = frame_of(c, (LONG)step);
    if (!f || !f->Mask) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG vx, vy;
    virtual_size(f, &vx, &vy);
    if (!cx)
        cx = (flags & DI_DEFAULTSIZE) ? MetricsGet(SM_CXICON, W32SystemDpi()) : vx;
    if (!cy)
        cy = (flags & DI_DEFAULTSIZE) ? MetricsGet(SM_CYICON, W32SystemDpi()) : vy / 2;
    if (!dc->Surface)
        return TRUE;
    LONG l = x, t = y, r = x + cx, b = y + cy;
    DcLogicalToSurface(dc, &l, &t);
    DcLogicalToSurface(dc, &r, &b);
    RECT dst = {l, t, r, b};
    REGION clip;
    RgnInit(&clip);
    if (DcGetClip(dc, &clip)) {
        if (brush)
            DrawBrushRect(dc, &clip, &dst, brush, PATCOPY);
        draw_frame(dc, &clip, &dst, f, flags);
        SurfaceMarkDrawn(dc->Surface, &dst, &clip);
    }
    RgnFree(&clip);
    return TRUE;
}

void CursorDrawIcon(DC *dc, LONG x, LONG y, HICON icon, LONG cx, LONG cy, UINT flags)
{
    CURICON *c = lookup(icon);
    if (c)
        draw(dc, x, y, dpi_variant(c), cx, cy, 0, nullptr, flags);
}

/* meta set, the caller draws into a metafile itself: it gets the bitmaps and the sizes of the first frame. */
BOOL NTAPI NtUserDrawIconEx(HDC hdc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH brush, UINT flags,
                            BOOL meta, PVOID user_out)
{
    UserEnter();
    BOOL ok = FALSE;
    CURICON *c = validate(icon);
    if (c) {
        c = dpi_variant(c);
        if (!meta) {
            DC *dc = DcLookup(hdc);
            BRUSH *b = brush ? (BRUSH *)GdiLookup(brush, GDI_TYPE_BRUSH) : nullptr;
            if (dc)
                ok = draw(dc, x, y, c, cx, cy, step, b, flags);
        } else {
            CURICON *f = frame_of(c, 0);
            if (f) {
                struct {
                    HBITMAP Mask, Color;
                    ULONG Cx, Cy;
                    LONG VirtualCx, VirtualCy;
                    HBITMAP Alpha;
                } out = {f->Mask ? (HBITMAP)f->Mask->Header.Handle : nullptr,
                         f->Color ? (HBITMAP)f->Color->Header.Handle : nullptr, f->Cx, f->Cy, 0, 0,
                         f->Alpha ? (HBITMAP)f->Alpha->Header.Handle : nullptr};
                virtual_size(f, &out.VirtualCx, &out.VirtualCy);
                if ((ULONG_PTR)user_out & 7 || !NT_SUCCESS(MimeCopyToUser(user_out, &out, sizeof(out))))
                    W32SetLastError(ERROR_NOACCESS);
                else
                    ok = TRUE;
            }
        }
    }
    UserLeave();
    return ok;
}

/* ------------------------------------------------------------------ the cursor of a thread */

W32THREAD *InputCursorQueue;
static HCURSOR g_screen_cursor;

/* the cursor of a queue, kept by the thread that holds its input. a queue starts with the wait cursor, as on windows. */
static HCURSOR queue_cursor(W32THREAD *q)
{
    if (!q->CursorMade) {
        q->CursorMade = TRUE;
        q->Cursor = CursorSystem(32514 /* IDC_WAIT */);
    }
    return q->Cursor;
}

static void queue_set_cursor(W32THREAD *q, HCURSOR cursor)
{
    CURICON *c = cursor ? lookup(cursor) : nullptr, *old = q->CursorLock;
    q->CursorMade = TRUE;
    q->Cursor = cursor;
    q->CursorLock = c;
    if (c)
        c->Header.RefCount++;
    if (old)
        release(old);
}

/*
 * the processes that start, the list of zzzCalcStartCursorHide of win32kfull 26300. CreateProcess asks for the feedback
 * of the start, which basesrv hands to UserNotifyProcessCreate: the screen then shows the starting cursor in place of
 * the arrow for 2 s, for 5 s from the first gui thread of the process and from a window it shows before it activates
 * one. the process reading its messages, or ending, ends it. measured on windows, where a daemon timer of a second ends
 * it after its time and mime ends it at the time.
 */
#define STARTING_SLOTS 16

static struct STARTING {
    PEPROCESS Process; /* referenced. */
    LONG64 Due;        /* the tick in ms the start shows until, 0 once it ended. */
    BOOLEAN Shows;     /* a window shown starts it again, until the process activates one: the AppStarting flag. */
} g_starting[STARTING_SLOTS];
static LONG64 g_starting_due;
volatile LONG CursorStarting; /* how many processes show the starting cursor, read without the lock. */

static LONG64 tick(void)
{
    return (LONG64)(KeQueryInterruptTime() / 10000);
}

static struct STARTING *starting_of(PEPROCESS process)
{
    for (ULONG i = 0; i < STARTING_SLOTS; i++)
        if (g_starting[i].Process == process)
            return &g_starting[i];
    return nullptr;
}

/* the time the screen shows the starting cursor until, from the processes whose time has not passed. */
static void starting_changed(void)
{
    LONG64 now = tick(), due = 0;
    LONG live = 0;
    for (ULONG i = 0; i < STARTING_SLOTS; i++) {
        struct STARTING *s = &g_starting[i];
        if (s->Due && s->Due <= now)
            s->Due = 0;
        if (s->Due) {
            live++;
            if (s->Due > due)
                due = s->Due;
        }
    }
    CursorStarting = live;
    if (due != g_starting_due) {
        g_starting_due = due;
        InputWakeAt(due);
        CursorUpdateImage();
    }
}

static void starting_for(struct STARTING *s, LONG64 ms)
{
    s->Due = tick() + ms;
    starting_changed();
}

/* UserNotifyProcessCreate, before the process runs: with the feedback on it starts for 2 s, with it off never. */
void CursorStartProcess(PEPROCESS process, BOOLEAN feedback)
{
    struct STARTING *s = starting_of(process);
    if (!feedback) {
        if (s)
            CursorStartGone(process);
        return;
    }
    for (ULONG i = 0; i < STARTING_SLOTS && !s; i++)
        if (!g_starting[i].Process)
            s = &g_starting[i];
    /* with every slot taken, one whose start ended and only waits for a window gives way. */
    for (ULONG i = 0; i < STARTING_SLOTS && !s; i++)
        if (!g_starting[i].Due) {
            s = &g_starting[i];
            ObDereferenceObject(s->Process);
            s->Process = nullptr;
        }
    if (!s)
        return;
    if (!s->Process) {
        ObReferenceObject(process);
        s->Process = process;
    }
    s->Shows = TRUE;
    starting_for(s, 2000);
}

/* the first gui thread of the process, xxxCreateThreadInfo. */
void CursorStartThread(PEPROCESS process)
{
    struct STARTING *s = starting_of(process);
    if (s)
        starting_for(s, 5000);
}

/* a window of the process becomes visible, zzzChangeStates. */
void CursorStartShown(PEPROCESS process)
{
    struct STARTING *s = starting_of(process);
    if (s && s->Shows)
        starting_for(s, 5000);
}

/*
 * the process activates a window: a window it shows no longer starts it again, as OnFirstActivationAttempted of
 * CForegroundLaunch clears its flag. a window with a caption also ends the start, measured on windows with overlapped,
 * tool and popup windows, from a path not found in win32kfull. a popup without one, even an app window, does not.
 */
void CursorStartActivated(PEPROCESS process, BOOLEAN caption)
{
    struct STARTING *s = starting_of(process);
    if (!s)
        return;
    s->Shows = FALSE;
    if (caption && s->Due) {
        s->Due = 0;
        starting_changed();
    }
}

/* a thread of the process reads its messages or waits for them, xxxRealInternalGetMessage and xxxRealSleepThread. */
void CursorStartRead(PEPROCESS process)
{
    struct STARTING *s = starting_of(process);
    if (s && s->Due) {
        s->Due = 0;
        starting_changed();
    }
}

void CursorStartGone(PEPROCESS process)
{
    struct STARTING *s = starting_of(process);
    if (!s)
        return;
    ObDereferenceObject(s->Process);
    *s = (struct STARTING){};
    starting_changed();
}

void CursorStartReading(void)
{
    if (!CursorStarting)
        return;
    UserEnter();
    CursorStartRead(PsGetCurrentProcess());
    UserLeave();
}

/* the time of the start passed, from the input thread. */
void CursorStartTick(void)
{
    starting_changed();
}

/* the count of ShowCursor of a queue: the sum of the counts of its threads. */
static LONG queue_level(W32THREAD *q)
{
    LONG level = 0;
    for (W32THREAD *x = W32NextThread(nullptr); x; x = W32NextThread(x))
        if (!x->Destroying && W32InputOwner(x) == q)
            level += x->CursorShowCount;
    return level;
}

static void show(HCURSOR cursor)
{
    CURICON *c = cursor ? lookup(cursor) : nullptr;
    g_screen_cursor = cursor;
    if (!cursor) {
        HostSetCursor(MIME_CURSOR_HIDDEN);
        return;
    }
    CURICON *f = shown(c);
    /* a cursor the program made is shown as it is, the system's are the host's own. */
    if (f && !c->System && f->Image) {
        CaptureCursorShown(cursor);
        if (HostSetCursorImage(cursor))
            return;
    }
    HostSetCursor(c && c->System ? c->System - 1 : MIME_CURSOR_ARROW);
    CaptureCursorShown(f && !c->System && f->Image ? cursor : nullptr);
}

/*
 * zzzUpdateCursorImage of win32kfull 26300: the screen has the cursor of the queue the mouse goes to, and none while
 * the count of that queue is below zero. the foreground queue hides it whatever the mouse is over when its program
 * registered the mouse for raw input with RIDEV_CAPTUREMOUSE. with no queue yet nothing changes. while a process
 * starts, the arrow and the starting cursor are shown as the starting cursor.
 */
void CursorUpdateImage(void)
{
    W32THREAD *fg = InputForeground ? InputForeground->Thread : nullptr;
    HCURSOR cursor = nullptr;
    if (fg && RawInputCapturesMouse(fg->Process) && queue_level(W32InputOwner(fg)) < 0)
        cursor = nullptr;
    else if (!InputCursorQueue)
        return;
    else if (queue_level(InputCursorQueue) >= 0)
        cursor = queue_cursor(InputCursorQueue);
    HCURSOR starting = CursorSystem(32650 /* IDC_APPSTARTING */);
    if (g_starting_due && cursor && (cursor == CursorSystem(32512 /* IDC_ARROW */) || cursor == starting))
        cursor = starting;
    show(cursor);
}

/* what GetCursorInfo tells: the cursor on the screen, whoever asks, and whether one shows. */
BOOL CursorOnScreen(HCURSOR *cursor)
{
    *cursor = g_screen_cursor;
    return g_screen_cursor != nullptr;
}

/*
 * zzzSetCursor: the cursor is the queue's, and reaches the screen when the mouse goes to that queue. the first queue
 * that sets one takes the screen while no queue has it.
 */
void CursorSet(HCURSOR cursor)
{
    W32THREAD *t = W32GetThread();
    if (!t)
        return;
    W32THREAD *q = W32InputOwner(t);
    if (queue_cursor(q) == cursor)
        return;
    queue_set_cursor(q, cursor);
    if (!InputCursorQueue)
        InputCursorQueue = q;
    if (q == InputCursorQueue)
        CursorUpdateImage();
}

/* zzzShowCursor: the thread keeps its count, the answer is the queue's, and the screen follows at 0 and at -1. */
int CursorShow(W32THREAD *t, BOOL show_it)
{
    W32THREAD *q = W32InputOwner(t);
    t->CursorShowCount += show_it ? 1 : -1;
    LONG level = queue_level(q);
    if (q == InputCursorQueue && level == (show_it ? 0 : -1))
        CursorUpdateImage();
    return level;
}

/*
 * a queue joins another, before its threads move: the cursor of the one joined stays, unless the mouse goes to the
 * one that joins, whose cursor it takes. measured on windows.
 */
void CursorAttached(W32THREAD *from, W32THREAD *to)
{
    if (InputCursorQueue != from)
        return;
    queue_set_cursor(to, queue_cursor(from));
    InputCursorQueue = to;
}

/*
 * a thread left the queue it shared and has a new one, with the wait cursor. windows gives the new queue to the
 * thread the call names first: when that one holds the input here and the other was all it shared with, the two
 * cursors change places.
 */
void CursorDetached(W32THREAD *gone, W32THREAD *asked)
{
    queue_set_cursor(gone, nullptr);
    gone->CursorMade = FALSE;
    if (gone != asked) {
        BOOLEAN shared = FALSE;
        for (W32THREAD *x = W32NextThread(nullptr); x && !shared; x = W32NextThread(x))
            shared = x->InputShare == asked;
        if (!shared) {
            queue_set_cursor(gone, queue_cursor(asked));
            queue_set_cursor(asked, nullptr);
            asked->CursorMade = FALSE;
            if (InputCursorQueue == asked)
                InputCursorQueue = gone;
        }
    }
    CursorUpdateImage();
}

/* the thread that held the input of a queue ends and another of the queue holds it from now on. */
void CursorHeir(W32THREAD *from, W32THREAD *heir)
{
    queue_set_cursor(heir, queue_cursor(from));
    if (InputCursorQueue == from)
        InputCursorQueue = heir;
}

HCURSOR NTAPI NtUserGetCursor(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HCURSOR h = t ? queue_cursor(W32InputOwner(t)) : nullptr;
    UserLeave();
    return h;
}

HCURSOR NTAPI NtUserSetCursor(HCURSOR cursor)
{
    UserEnterRead();
    W32THREAD *t = W32GetThread();
    HCURSOR old = nullptr;
    if (cursor && !lookup(cursor)) {
        W32SetLastError(ERROR_INVALID_CURSOR_HANDLE);
    } else {
        old = t ? queue_cursor(W32InputOwner(t)) : nullptr;
        CursorSet(cursor);
    }
    UserLeave();
    return old;
}
