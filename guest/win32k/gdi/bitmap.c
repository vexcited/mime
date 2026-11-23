/*
 * bitmaps: surfaces in every dib format, dib sections and the dib
 * transfer services.
 */
#include "../win32k.h"
#include <mime/win32/d3dkmthk.h>

static const RGBQUAD g_mono[2] = {{0, 0, 0, 0}, {255, 255, 255, 0}};

static LONG dib_stride(LONG width, UCHAR bpp)
{
    return ((width * bpp + 31) / 32) * 4;
}

/* device dependent bitmaps are word aligned like windows reports them. */
static LONG ddb_stride(LONG width, UCHAR bpp)
{
    return ((width * bpp + 15) / 16) * 2;
}

static void default_masks(SURFACE *s)
{
    if (s->Bpp == 16) {
        s->Masks[0] = 0x7C00;
        s->Masks[1] = 0x03E0;
        s->Masks[2] = 0x001F;
    } else if (s->Bpp == 32 || s->Bpp == 24) {
        s->Masks[0] = 0x00FF0000;
        s->Masks[1] = 0x0000FF00;
        s->Masks[2] = 0x000000FF;
    }
}

SURFACE *SurfaceCreate(LONG width, LONG height, UCHAR bpp, ULONG flags)
{
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
        return nullptr;
    SURFACE *s = W32Alloc(sizeof(SURFACE));
    if (!s)
        return nullptr;
    s->Width = width;
    s->Height = height;
    s->Bpp = bpp;
    s->Flags = (UCHAR)flags;
    s->Stride = (flags & SURF_DEVICE) ? width * 4 : ddb_stride(width, bpp);
    if (bpp == 32 || (flags & SURF_DEVICE))
        s->Stride = width * 4;
    SIZE_T size = (SIZE_T)s->Stride * (SIZE_T)height;
    if (flags & SURF_CONTIGUOUS) {
        PHYSICAL_ADDRESS highest;
        highest.QuadPart = -1;
        s->Bits = MmAllocateContiguousMemory(size, highest);
        if (s->Bits)
            memset(s->Bits, 0, size);
    } else {
        s->Bits = W32Alloc(size);
    }
    if (!s->Bits) {
        W32Free(s);
        return nullptr;
    }
    s->Flags |= SURF_OWNS_BITS;
    if (bpp == 1) {
        memcpy(s->Colors, g_mono, sizeof(g_mono));
        s->ColorCount = 2;
    }
    default_masks(s);
    s->Info.biSize = sizeof(BITMAPINFOHEADER);
    s->Info.biWidth = width;
    s->Info.biHeight = height;
    s->Info.biPlanes = 1;
    s->Info.biBitCount = bpp;
    return s;
}

/* a temporary surface over caller memory, typically dib bits captured from user mode. */
SURFACE *SurfaceWrap(LONG width, LONG height, UCHAR bpp, PVOID bits, LONG stride, const BITMAPINFO *info)
{
    SURFACE *s = W32Alloc(sizeof(SURFACE));
    if (!s)
        return nullptr;
    s->Width = width;
    s->Height = height;
    s->Bpp = bpp;
    s->Stride = stride;
    s->Bits = bits;
    default_masks(s);
    if (info) {
        s->Info = info->bmiHeader;
        if (bpp <= 8) {
            ULONG n = info->bmiHeader.biClrUsed ? info->bmiHeader.biClrUsed : 1u << bpp;
            if (n > 256)
                n = 256;
            memcpy(s->Colors, info->bmiColors, n * sizeof(RGBQUAD));
            s->ColorCount = (USHORT)n;
        } else if (info->bmiHeader.biCompression == BI_BITFIELDS) {
            memcpy(s->Masks, info->bmiColors, sizeof(s->Masks));
        }
    }
    return s;
}

void SurfaceDestroy(SURFACE *s)
{
    if (!s)
        return;
    if (s->Header.Handle)
        GdiFreeHandle(&s->Header);
    if (s->Flags & SURF_OWNS_BITS) {
        if (s->Flags & SURF_CONTIGUOUS)
            MmFreeContiguousMemory(s->Bits);
        else
            W32Free(s->Bits);
    } else if ((s->Flags & SURF_USER_BITS) && s->UserBits && !(s->Flags & SURF_FOREIGN)) {
        PKPROCESS state = nullptr;
        BOOLEAN attached = s->UserOwner && s->UserOwner != PsGetCurrentProcess();
        if (attached)
            KeStackAttachProcess((PRKPROCESS)s->UserOwner, &state);
        if (s->UserViewBase) {
            ZwUnmapViewOfSection(NtCurrentProcess(), s->UserViewBase);
        } else {
            PVOID base = s->UserBits;
            SIZE_T size = 0;
            ZwFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
        }
        if (attached)
            KeUnstackDetachProcess(&state);
        if (s->UserOwner)
            ObDereferenceObject(s->UserOwner);
    }
    W32Free(s);
}

/* the draw touched the part of r inside the clip, all of r without one. */
void SurfaceMarkDrawn(SURFACE *s, const RECT *r, const REGION *clip)
{
    RECT clipped, bounds = {0, 0, s->Width, s->Height};
    if (!RectIntersect(&clipped, r, &bounds))
        return;
    RectUnion(&s->Dirty, &s->Dirty, &clipped);
    if (ScreenHeldCount && s->Window) {
        RECT part;
        if (!clip)
            ScreenHeldDrawn(s, &clipped);
        for (ULONG i = 0; clip && i < clip->Count; i++)
            if (RectIntersect(&part, &clipped, &clip->Rects[i]))
                ScreenHeldDrawn(s, &part);
    }
    /* what a program draws into the screen is part of the screen. */
    if (s->Flags & SURF_VSCREEN)
        ScreenVirtualFlush(s, &clipped);
    else if (ScreenIsSurface(s))
        ScreenKeep(&clipped);
}

void SurfaceMarkDirty(SURFACE *s, const RECT *r)
{
    SurfaceMarkDrawn(s, r, nullptr);
}

SURFACE *SurfaceLookup(HBITMAP bitmap)
{
    return (SURFACE *)GdiLookup(bitmap, GDI_TYPE_BITMAP);
}

/* ------------------------------------------------------------------ pixel access */

static UCHAR mask_shift(DWORD mask)
{
    return mask ? (UCHAR)__builtin_ctz(mask) : 0;
}

static ULONG expand_channel(DWORD value, DWORD mask)
{
    if (!mask)
        return 0;
    ULONG v = (value & mask) >> mask_shift(mask);
    ULONG bits = (ULONG)__builtin_popcount(mask);
    if (bits >= 8)
        return v >> (bits - 8);
    return (v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1);
}

static DWORD pack_channel(ULONG v8, DWORD mask)
{
    if (!mask)
        return 0;
    ULONG bits = (ULONG)__builtin_popcount(mask);
    ULONG v = bits >= 8 ? v8 << (bits - 8) : (v8 * ((1u << bits) - 1) + 127) / 255;
    return (v << mask_shift(mask)) & mask;
}

static UCHAR nearest_color(const SURFACE *s, ULONG pixel)
{
    LONG r = (pixel >> 16) & 0xFF, g = (pixel >> 8) & 0xFF, b = pixel & 0xFF;
    ULONG best = 0, best_d = 0xFFFFFFFF;
    ULONG count = s->ColorCount ? s->ColorCount : (1u << s->Bpp);
    for (ULONG i = 0; i < count && i < 256; i++) {
        LONG dr = r - s->Colors[i].rgbRed, dg = g - s->Colors[i].rgbGreen, db = b - s->Colors[i].rgbBlue;
        ULONG d = (ULONG)(dr * dr + dg * dg + db * db);
        if (d < best_d) {
            best_d = d;
            best = i;
            if (!d)
                break;
        }
    }
    return (UCHAR)best;
}

ULONG SurfaceGetPixel(SURFACE *s, LONG x, LONG y)
{
    PUCHAR row = s->Bits + (LONG_PTR)y * s->Stride;
    switch (s->Bpp) {
    case 32:
        return ((PULONG)row)[x];
    case 24: {
        PUCHAR p = row + x * 3;
        return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16;
    }
    case 16: {
        USHORT v = ((PUSHORT)row)[x];
        return expand_channel(v, s->Masks[0]) << 16 | expand_channel(v, s->Masks[1]) << 8 | expand_channel(v, s->Masks[2]);
    }
    case 8:
    case 4:
    case 1: {
        ULONG index;
        if (s->Bpp == 8)
            index = row[x];
        else if (s->Bpp == 4)
            index = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xF;
        else
            index = (row[x >> 3] >> (7 - (x & 7))) & 1;
        RGBQUAD q = s->Colors[index];
        return (ULONG)q.rgbBlue | (ULONG)q.rgbGreen << 8 | (ULONG)q.rgbRed << 16;
    }
    }
    return 0;
}

/*
 * the bits of a pixel as the surface stores them: an index at 8 bits or less, the packed value at 16, all 32 bits
 * with alpha at 32. raster operations work on these, so WHITENESS gives index 0xff and alpha 0xff.
 */
ULONG SurfaceRawOf(SURFACE *s, ULONG pixel)
{
    switch (s->Bpp) {
    case 32:
        return pixel & 0xFFFFFF;
    case 24:
        return pixel & 0xFFFFFF;
    case 16:
        return pack_channel((pixel >> 16) & 0xFF, s->Masks[0]) | pack_channel((pixel >> 8) & 0xFF, s->Masks[1]) |
               pack_channel(pixel & 0xFF, s->Masks[2]);
    default:
        return nearest_color(s, pixel);
    }
}

ULONG SurfaceGetRaw(SURFACE *s, LONG x, LONG y)
{
    PUCHAR row = s->Bits + (LONG_PTR)y * s->Stride;
    switch (s->Bpp) {
    case 32:
        return ((PULONG)row)[x];
    case 24:
        return (ULONG)row[x * 3] | (ULONG)row[x * 3 + 1] << 8 | (ULONG)row[x * 3 + 2] << 16;
    case 16:
        return ((PUSHORT)row)[x];
    case 8:
        return row[x];
    case 4:
        return (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xF;
    case 1:
        return (row[x >> 3] >> (7 - (x & 7))) & 1;
    }
    return 0;
}

void SurfacePutRaw(SURFACE *s, LONG x, LONG y, ULONG raw)
{
    PUCHAR row = s->Bits + (LONG_PTR)y * s->Stride;
    switch (s->Bpp) {
    case 32:
        ((PULONG)row)[x] = (s->Flags & SURF_DEVICE) ? raw | 0xFF000000u : raw;
        break;
    case 24:
        row[x * 3] = (UCHAR)raw;
        row[x * 3 + 1] = (UCHAR)(raw >> 8);
        row[x * 3 + 2] = (UCHAR)(raw >> 16);
        break;
    case 16:
        ((PUSHORT)row)[x] = (USHORT)raw;
        break;
    case 8:
        row[x] = (UCHAR)raw;
        break;
    case 4:
        if (x & 1)
            row[x >> 1] = (UCHAR)((row[x >> 1] & 0xF0) | (raw & 0xF));
        else
            row[x >> 1] = (UCHAR)((row[x >> 1] & 0x0F) | (raw & 0xF) << 4);
        break;
    case 1:
        if (raw & 1)
            row[x >> 3] |= (UCHAR)(0x80 >> (x & 7));
        else
            row[x >> 3] &= (UCHAR)~(0x80 >> (x & 7));
        break;
    }
}

void SurfacePutPixel(SURFACE *s, LONG x, LONG y, ULONG pixel)
{
    PUCHAR row = s->Bits + (LONG_PTR)y * s->Stride;
    switch (s->Bpp) {
    case 32:
        ((PULONG)row)[x] = (s->Flags & SURF_DEVICE) ? pixel | 0xFF000000u : pixel;
        break;
    case 24: {
        PUCHAR p = row + x * 3;
        p[0] = (UCHAR)pixel;
        p[1] = (UCHAR)(pixel >> 8);
        p[2] = (UCHAR)(pixel >> 16);
        break;
    }
    case 16:
        ((PUSHORT)row)[x] = (USHORT)(pack_channel((pixel >> 16) & 0xFF, s->Masks[0]) |
                                     pack_channel((pixel >> 8) & 0xFF, s->Masks[1]) | pack_channel(pixel & 0xFF, s->Masks[2]));
        break;
    case 8:
        row[x] = nearest_color(s, pixel);
        break;
    case 4: {
        UCHAR v = nearest_color(s, pixel);
        if (x & 1)
            row[x >> 1] = (UCHAR)((row[x >> 1] & 0xF0) | v);
        else
            row[x >> 1] = (UCHAR)((row[x >> 1] & 0x0F) | (v << 4));
        break;
    }
    case 1: {
        UCHAR bit = (UCHAR)(0x80 >> (x & 7));
        if (nearest_color(s, pixel))
            row[x >> 3] |= bit;
        else
            row[x >> 3] &= (UCHAR)~bit;
        break;
    }
    }
}

/* ------------------------------------------------------------------ dib helpers */

typedef struct DIB_DESC {
    LONG Width;
    LONG Height; /* positive. */
    BOOLEAN TopDown;
    UCHAR Bpp;
    LONG Stride;
    ULONG Compression;
    ULONG Colors;
    ULONG HeaderSize; /* header plus color table or masks, in bytes. */
} DIB_DESC;

/* allow_rle accepts the run length formats, only valid for a source: 8 or 4 bits, bottom up, sized by biSizeImage. */
static BOOLEAN describe_dib_rle(const BITMAPINFO *info, UINT usage, DIB_DESC *d, BOOLEAN allow_rle)
{
    const BITMAPINFOHEADER *h = &info->bmiHeader;
    if (h->biSize < sizeof(BITMAPINFOHEADER) && h->biSize != 12)
        return FALSE;
    if (h->biSize == 12) /* BITMAPCOREHEADER is not supported. */
        return FALSE;
    if (h->biWidth <= 0 || h->biHeight == 0 || h->biWidth > 32768 || h->biHeight > 32768 || h->biHeight < -32768)
        return FALSE;
    d->Width = h->biWidth;
    d->TopDown = h->biHeight < 0;
    d->Height = d->TopDown ? -h->biHeight : h->biHeight;
    d->Bpp = (UCHAR)h->biBitCount;
    d->Compression = h->biCompression;
    if (d->Bpp != 1 && d->Bpp != 4 && d->Bpp != 8 && d->Bpp != 16 && d->Bpp != 24 && d->Bpp != 32)
        return FALSE;
    if (allow_rle && d->Compression == BI_RLE8 && d->Bpp == 8 && !d->TopDown && h->biSizeImage) {
        /* decoded as an 8 bit bitmap. */
    } else if (allow_rle && d->Compression == BI_RLE4 && d->Bpp == 4 && !d->TopDown && h->biSizeImage) {
        /* decoded as a 4 bit bitmap. */
    } else if (d->Compression != BI_RGB && d->Compression != BI_BITFIELDS) {
        return FALSE;
    }
    d->Stride = dib_stride(d->Width, d->Bpp);
    /* a count of colours past the format is cut to it, and DIB_PAL_INDICES comes with no table. */
    d->Colors = d->Bpp <= 8 ? (h->biClrUsed && h->biClrUsed < 1u << d->Bpp ? h->biClrUsed : 1u << d->Bpp) : 0;
    d->HeaderSize = h->biSize + (d->Compression == BI_BITFIELDS && h->biSize == sizeof(BITMAPINFOHEADER) ? 12 : 0) +
                    d->Colors * (usage == DIB_PAL_COLORS ? 2 : usage == 2 ? 0 : 4);
    return TRUE;
}

static BOOLEAN describe_dib(const BITMAPINFO *info, UINT usage, DIB_DESC *d)
{
    return describe_dib_rle(info, usage, d, FALSE);
}

static BITMAPINFO *capture_info(const BITMAPINFO *user, UINT usage, DIB_DESC *d, BOOLEAN allow_rle)
{
    BITMAPINFOHEADER h;
    if (!NT_SUCCESS(MimeCopyFromUser(&h, user, sizeof(h))))
        return nullptr;
    BITMAPINFO *info = W32Alloc(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD) + 16);
    if (!info)
        return nullptr;
    info->bmiHeader = h;
    if (h.biSize > 4096 || !describe_dib_rle(info, usage, d, allow_rle)) {
        W32Free(info);
        return nullptr;
    }
    SIZE_T table = d->HeaderSize - h.biSize;
    if (table && !NT_SUCCESS(MimeCopyFromUser(info->bmiColors, (const UCHAR *)user + h.biSize, table))) {
        W32Free(info);
        return nullptr;
    }
    info->bmiHeader.biClrUsed = d->Colors;
    /* a BITMAPV4HEADER or BITMAPV5HEADER has its masks inside, at 40. */
    if (d->Compression == BI_BITFIELDS && h.biSize >= 52 &&
        !NT_SUCCESS(MimeCopyFromUser(info->bmiColors, (const UCHAR *)user + sizeof(BITMAPINFOHEADER), 12))) {
        W32Free(info);
        return nullptr;
    }
    if (usage == DIB_PAL_COLORS) {
        /* palette indices: map to the default palette, which starts with the static colors. */
        for (ULONG i = 0; i < d->Colors; i++)
            info->bmiColors[i] = (RGBQUAD){0, 0, 0, 0};
    }
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    return info;
}

static SURFACE *wrap_dib_bits(const DIB_DESC *d, const BITMAPINFO *info, PVOID bits, ULONG lines, ULONG start)
{
    /* bits hold `lines` scanlines starting at scan `start` counted from the bottom. */
    LONG height = (LONG)lines;
    PUCHAR first;
    LONG stride;
    if (d->TopDown) {
        first = bits;
        stride = d->Stride;
    } else {
        first = (PUCHAR)bits + (SIZE_T)(height - 1) * (SIZE_T)d->Stride;
        stride = -d->Stride;
    }
    (void)start;
    return SurfaceWrap(d->Width, height, d->Bpp, first, stride, info);
}

/* ------------------------------------------------------------------ pattern brushes */

/* a copy of a surface in its own format, which a pattern brush keeps: the bitmap may go while the brush stays. */
static SURFACE *copy_surface(SURFACE *src)
{
    SURFACE *s = SurfaceCreate(src->Width, src->Height, src->Bpp, 0);
    if (!s)
        return nullptr;
    memcpy(s->Colors, src->Colors, sizeof(s->Colors));
    memcpy(s->Masks, src->Masks, sizeof(s->Masks));
    s->ColorCount = src->ColorCount;
    for (LONG y = 0; y < src->Height; y++)
        for (LONG x = 0; x < src->Width; x++)
            SurfacePutRaw(s, x, y, SurfaceGetRaw(src, x, y));
    return s;
}

/*
 * CreatePatternBrush passes the bitmap, whether a pen asks and whether it is BS_PATTERN8X8. a monochrome pattern
 * takes the text and background colours of the dc it draws in, any other its own colours.
 */
HBRUSH NTAPI NtGdiCreatePatternBrushInternal(HBITMAP bitmap, BOOL pen, BOOL b8x8)
{
    UserEnterRead();
    SURFACE *src = SurfaceLookup(bitmap);
    SURFACE *pattern = src ? copy_surface(src) : nullptr;
    BRUSH *b = pattern ? GdiCreateBrush(BS_PATTERN, 0, (ULONG_PTR)bitmap) : nullptr;
    if (b)
        b->Pattern = pattern;
    else
        SurfaceDestroy(pattern);
    UserLeave();
    return b ? (HBRUSH)b->Header.Handle : nullptr;
}

/*
 * CreateDIBPatternBrushPt passes the packed dib with its header made a BITMAPINFOHEADER, the usage, its size, whether
 * it is BS_DIBPATTERN8X8, whether a pen asks, and the program's own pointer, which GetObject gives back as lbHatch.
 * the pattern is kept in 32 bits with the colours of the dib, a monochrome one too.
 */
HBRUSH NTAPI NtGdiCreateDIBBrush(PVOID packed, ULONG usage, UINT size, BOOL b8x8, BOOL pen, PVOID client)
{
    DIB_DESC d;
    if (usage > DIB_PAL_COLORS || size < sizeof(BITMAPINFOHEADER))
        return nullptr;
    BITMAPINFO *info = capture_info(packed, usage, &d, FALSE);
    if (!info)
        return nullptr;
    SIZE_T bytes = (SIZE_T)d.Stride * (SIZE_T)d.Height;
    PUCHAR bits = d.HeaderSize <= size && bytes <= size - d.HeaderSize ? W32Alloc(bytes) : nullptr;
    HBRUSH h = nullptr;
    if (bits && NT_SUCCESS(MimeCopyFromUser(bits, (const UCHAR *)packed + d.HeaderSize, bytes))) {
        UserEnterRead();
        SURFACE *view = wrap_dib_bits(&d, info, bits, (ULONG)d.Height, 0);
        SURFACE *pattern = view ? SurfaceCreate(d.Width, d.Height, 32, 0) : nullptr;
        if (pattern)
            for (LONG y = 0; y < d.Height; y++)
                for (LONG x = 0; x < d.Width; x++)
                    SurfacePutPixel(pattern, x, y, SurfaceGetPixel(view, x, y) & 0xFFFFFF);
        BRUSH *b = pattern ? GdiCreateBrush(BS_DIBPATTERNPT, usage,
                                            (ULONG_PTR)client) : nullptr;
        if (b)
            b->Pattern = pattern;
        else
            SurfaceDestroy(pattern);
        if (view)
            W32Free(view);
        if (b)
            h = (HBRUSH)b->Header.Handle;
        UserLeave();
    }
    W32Free(bits);
    W32Free(info);
    return h;
}

/* ------------------------------------------------------------------ bitmap services */

HBITMAP NTAPI NtGdiCreateBitmap(int width, int height, UINT planes, UINT bpp, const void *bits)
{
    if (width <= 0 || height <= 0 || planes != 1)
        return nullptr;
    UCHAR depth = bpp <= 1 ? 1 : bpp <= 4 ? 4 : bpp <= 8 ? 8 : bpp <= 16 ? 16 : bpp <= 24 ? 24 : 32;
    UserEnterRead();
    SURFACE *s = SurfaceCreate(width, height, depth, 0);
    HBITMAP h = nullptr;
    if (s && GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP)) {
        h = (HBITMAP)s->Header.Handle;
        if (bits) {
            SIZE_T size = (SIZE_T)s->Stride * (SIZE_T)height;
            if (!NT_SUCCESS(MimeCopyFromUser(s->Bits, bits, size)))
                memset(s->Bits, 0, size);
        }
    } else if (s) {
        SurfaceDestroy(s);
    }
    UserLeave();
    return h;
}

HBITMAP NTAPI NtGdiCreateCompatibleBitmap(HDC hdc, int width, int height)
{
    if (width < 0 || height < 0)
        return nullptr;
    if (!width || !height)
        return NtGdiCreateBitmap(1, 1, 1, 1, nullptr);
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    UCHAR bpp = 32;
    if (dc && dc->Surface && (dc->Flags & DC_MEMORY))
        bpp = dc->Surface->Bpp;
    SURFACE *s = dc ? SurfaceCreate(width, height, bpp, 0) : nullptr;
    HBITMAP h = nullptr;
    if (s && GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP)) {
        h = (HBITMAP)s->Header.Handle;
        if (bpp <= 8 && dc->Surface) {
            memcpy(s->Colors, dc->Surface->Colors, sizeof(s->Colors));
            s->ColorCount = dc->Surface->ColorCount;
        }
    } else if (s) {
        SurfaceDestroy(s);
    }
    UserLeave();
    return h;
}

HBITMAP NTAPI NtGdiCreateDIBSection(HDC hdc, HANDLE section, DWORD offset, const BITMAPINFO *user_info, UINT usage,
                                     UINT header_size, ULONG flags, ULONG_PTR color_space, PVOID *bits_out)
{
    PVOID user = nullptr, view_base = nullptr;
    if (bits_out && !NT_SUCCESS(MimeCopyToUser(bits_out, &user, sizeof(user))))
        return nullptr;
    DIB_DESC d;
    BITMAPINFO *info = capture_info(user_info, usage, &d, FALSE);
    if (!info) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    SIZE_T size = (SIZE_T)d.Stride * (SIZE_T)d.Height;
    NTSTATUS status = STATUS_SUCCESS;
    if (section) {
        PVOID object = nullptr;
        ACCESS_MASK access = 0;
        status = offset & 3 ? STATUS_INVALID_PARAMETER :
            ObReferenceHandle(section, *MmSectionObjectType, KeGetPreviousMode(), &object, &access);
        if (NT_SUCCESS(status)) {
            if ((access & (SECTION_MAP_READ | SECTION_MAP_WRITE)) != (SECTION_MAP_READ | SECTION_MAP_WRITE)) {
                status = STATUS_ACCESS_DENIED;
            } else {
                SIZE_T view_size = size + (offset & 0xffff);
                status = MmMapWritableSection(object, PsGetCurrentProcess(), &view_base,
                                                   offset & ~0xffffull, &view_size);
                if (NT_SUCCESS(status))
                    user = (PUCHAR)view_base + (offset & 0xffff);
            }
            ObDereferenceObject(object);
        }
    } else {
        SIZE_T alloc = size;
        status = ZwAllocateVirtualMemory(NtCurrentProcess(), &user, 0, &alloc, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    if (!NT_SUCCESS(status)) {
        W32SetLastError(section ? ERROR_INVALID_PARAMETER : ERROR_NOT_ENOUGH_MEMORY);
        W32Free(info);
        return nullptr;
    }
    UserEnterRead();
    SURFACE *s = W32Alloc(sizeof(SURFACE));
    HBITMAP h = nullptr;
    if (s) {
        s->Width = d.Width;
        s->Height = d.Height;
        s->Bpp = d.Bpp;
        s->Flags = SURF_USER_BITS | (d.TopDown ? SURF_TOPDOWN : 0);
        s->UserBits = user;
        s->UserSize = size;
        s->UserViewBase = view_base;
        s->UserSection = section;
        s->UserSectionOffset = section ? offset : 0;
        s->Stride = d.TopDown ? d.Stride : -d.Stride;
        s->Bits = d.TopDown ? (PUCHAR)user : (PUCHAR)user + (SIZE_T)(d.Height - 1) * (SIZE_T)d.Stride;
        default_masks(s);
        s->Info = info->bmiHeader;
        s->Info.biSizeImage = (DWORD)size;
        if (d.Bpp <= 8) {
            memcpy(s->Colors, info->bmiColors, d.Colors * sizeof(RGBQUAD));
            s->ColorCount = (USHORT)d.Colors;
        } else if (d.Compression == BI_BITFIELDS) {
            memcpy(s->Masks, info->bmiColors, sizeof(s->Masks));
        }
        if (GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP)) {
            h = (HBITMAP)s->Header.Handle;
            s->UserOwner = PsGetCurrentProcess();
            ObReferenceObject(s->UserOwner);
        } else {
            W32Free(s);
        }
    }
    UserLeave();
    W32Free(info);
    if (!h) {
        if (view_base) {
            ZwUnmapViewOfSection(NtCurrentProcess(), view_base);
        } else {
            SIZE_T zero = 0;
            ZwFreeVirtualMemory(NtCurrentProcess(), &user, &zero, MEM_RELEASE);
        }
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    if (bits_out && !NT_SUCCESS(MimeCopyToUser(bits_out, &user, sizeof(user)))) {
        NtGdiDeleteObjectApp(h);
        return nullptr;
    }
    return h;
}

/*
 * for GetDIBColorTable and SetDIBColorTable. only a dib section of 8 bits a pixel or fewer has a color table.
 * returns the number of entries read or written, 0 for anything else or for entries past the table.
 */
/* the color table of the dib section a dc holds, NtGdiDoPalette 4 and 5. */
ULONG GdiDibColorTable(HDC hdc, WORD start, WORD count, PVOID user_entries, BOOLEAN set)
{
    DWORD function = set ? MIME_PAL_SETDIBCOLORTABLE : MIME_PAL_GETDIBCOLORTABLE;
    HGDIOBJ object = hdc;
    if (!user_entries || !count)
        return 0;
    RGBQUAD entries[256];
    ULONG n = 0;
    UserEnterRead();
    DC *dc = DcLookup((HDC)object);
    SURFACE *s = dc ? dc->Surface : nullptr;
    if (s && (s->Flags & SURF_USER_BITS) && s->Bpp <= 8 && start < s->ColorCount) {
        n = s->ColorCount - start < count ? s->ColorCount - start : count;
        if (function == MIME_PAL_GETDIBCOLORTABLE) {
            memcpy(entries, s->Colors + start, n * sizeof(RGBQUAD));
        } else if (NT_SUCCESS(MimeCopyFromUser(entries, user_entries, n * sizeof(RGBQUAD)))) {
            memcpy(s->Colors + start, entries, n * sizeof(RGBQUAD));
            RECT all = {0, 0, s->Width, s->Height};
            SurfaceMarkDirty(s, &all);
        } else {
            n = 0;
        }
    }
    UserLeave();
    if (n && function == MIME_PAL_GETDIBCOLORTABLE && !NT_SUCCESS(MimeCopyToUser(user_entries, entries, n * sizeof(RGBQUAD))))
        n = 0;
    return n;
}

/* ------------------------------------------------------------------ dcs over caller memory */

/*
 * D3DKMTCreateDCFromMemory: a memory dc over the caller's memory, rows top down at the caller's pitch. ddraw draws
 * on its surfaces with gdi this way. the memory stays the caller's.
 */
NTSTATUS NTAPI NtGdiDdDDICreateDCFromMemory(D3DKMT_CREATEDCFROMMEMORY *user_desc)
{
    D3DKMT_CREATEDCFROMMEMORY d;
    if (!user_desc || !NT_SUCCESS(MimeCopyFromUser(&d, user_desc, sizeof(d))))
        return STATUS_INVALID_PARAMETER;
    UCHAR bpp;
    DWORD masks[3] = {};
    switch (d.Format) {
    case D3DDDIFMT_A8R8G8B8:
    case D3DDDIFMT_X8R8G8B8:
        bpp = 32;
        masks[0] = 0x00FF0000, masks[1] = 0x0000FF00, masks[2] = 0x000000FF;
        break;
    case D3DDDIFMT_R8G8B8:
        bpp = 24;
        masks[0] = 0x00FF0000, masks[1] = 0x0000FF00, masks[2] = 0x000000FF;
        break;
    case D3DDDIFMT_R5G6B5:
        bpp = 16;
        masks[0] = 0xF800, masks[1] = 0x07E0, masks[2] = 0x001F;
        break;
    case D3DDDIFMT_X1R5G5B5:
    case D3DDDIFMT_A1R5G5B5:
        bpp = 16;
        masks[0] = 0x7C00, masks[1] = 0x03E0, masks[2] = 0x001F;
        break;
    case D3DDDIFMT_P8:
        bpp = 8;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    if (!d.pMemory || !d.Width || !d.Height || d.Width > 32768 || d.Height > 32768 ||
        (ULONG64)d.Pitch < ((ULONG64)d.Width * bpp + 7) / 8 || (bpp == 8 && !d.pColorTable))
        return STATUS_INVALID_PARAMETER;
    PALETTEENTRY table[256];
    if (bpp == 8 && !NT_SUCCESS(MimeCopyFromUser(table, d.pColorTable, sizeof(table))))
        return STATUS_INVALID_PARAMETER;
    UserEnterRead();
    NTSTATUS status = STATUS_NO_MEMORY;
    SURFACE *s = W32Alloc(sizeof(SURFACE));
    DC *dc = s ? DcCreate(DC_MEMORY) : nullptr;
    if (dc) {
        s->Width = (LONG)d.Width;
        s->Height = (LONG)d.Height;
        s->Bpp = bpp;
        s->Flags = SURF_USER_BITS | SURF_FOREIGN | SURF_TOPDOWN;
        s->UserBits = d.pMemory;
        s->UserSize = (SIZE_T)d.Pitch * d.Height;
        s->Stride = (LONG)d.Pitch;
        s->Bits = d.pMemory;
        memcpy(s->Masks, masks, sizeof(masks));
        s->Info = (BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER), (LONG)d.Width, -(LONG)d.Height, 1, bpp,
                                     bpp == 16 ? BI_BITFIELDS : BI_RGB};
        s->Info.biSizeImage = (DWORD)s->UserSize;
        if (bpp == 8) {
            for (ULONG i = 0; i < 256; i++)
                s->Colors[i] = (RGBQUAD){table[i].peBlue, table[i].peGreen, table[i].peRed, 0};
            s->ColorCount = 256;
        }
        DC *ref = d.hDeviceDc ? DcLookup(d.hDeviceDc) : nullptr;
        if (ref) {
            dc->Dpi = ref->Dpi;
            dc->Awareness = ref->Awareness;
        }
        if (GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP)) {
            DcSetSurface(dc, s);
            d.hDc = (HDC)dc->Header.Handle;
            d.hBitmap = s->Header.Handle;
            status = STATUS_SUCCESS;
        }
    }
    if (!NT_SUCCESS(status)) {
        if (dc)
            DcFree(dc);
        W32Free(s);
    }
    UserLeave();
    if (NT_SUCCESS(status) && !NT_SUCCESS(MimeCopyToUser(&user_desc->hDc, &d.hDc, sizeof(d.hDc) + sizeof(d.hBitmap))))
        status = STATUS_INVALID_PARAMETER;
    return status;
}

NTSTATUS NTAPI NtGdiDdDDIDestroyDCFromMemory(const D3DKMT_DESTROYDCFROMMEMORY *user_desc)
{
    D3DKMT_DESTROYDCFROMMEMORY d;
    if (!user_desc || !NT_SUCCESS(MimeCopyFromUser(&d, user_desc, sizeof(d))))
        return STATUS_INVALID_PARAMETER;
    UserEnterRead();
    DC *dc = DcLookup(d.hDc);
    SURFACE *s = SurfaceLookup(d.hBitmap);
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (dc && s && dc->Surface == s && (s->Flags & SURF_FOREIGN)) {
        GdiDeleteObject(&dc->Header);
        GdiDeleteObject(&s->Header);
        status = STATUS_SUCCESS;
    }
    UserLeave();
    return status;
}

/*
 * the table GetDIBits gives a bitmap of more than 8 bits read at 8 bits or less, measured with dibpal.exe of
 * scratch/dlls/gdibatch: black and white, the 16 colours of vga, or at 8 bits the 20 static colours at both ends and
 * between them 8 levels of red and green and 4 of blue, as the index i makes them.
 */
static void default_dib_table(UCHAR bpp, RGBQUAD *out)
{
    static const ULONG low[10] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
                                  0xc0dcc0, 0xa6caf0};
    static const ULONG high[10] = {0xfffbf0, 0xa0a0a4, 0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff,
                                   0x00ffff, 0xffffff};
    static const ULONG vga[16] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0x808080,
                                  0xc0c0c0, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff};
    ULONG n = 1u << bpp;
    for (ULONG i = 0; i < n && i < 256; i++) {
        ULONG v;
        if (bpp == 1)
            v = i ? 0xffffff : 0;
        else if (bpp == 4)
            v = vga[i];
        else if (i < 10)
            v = low[i];
        else if (i >= 246)
            v = high[i - 246];
        else
            v = (i % 8) * 0x20 << 16 | ((i / 8) % 8) * 0x20 << 8 | (i / 64) * 0x40;
        out[i] = (RGBQUAD){(BYTE)v, (BYTE)(v >> 8), (BYTE)(v >> 16), 0};
    }
}

/*
 * as win32kfull's GreGetDIBitsInternalWorker. gdi32 passes the header as the program gave it, a BITMAPCOREHEADER
 * too. a query, biBitCount 0, zeroes the header and fills a BITMAPINFOHEADER of the bitmap, with BI_BITFIELDS at 16 and
 * 32 bits and biClrUsed the size of the table. otherwise the header becomes a BITMAPINFOHEADER with biSizeImage,
 * the masks or the table follow it at 40, and without bits the call answers 1 with the width and height left as the
 * caller gave them. bits narrower or wider than the bitmap are clipped to it.
 */
int NTAPI NtGdiGetDIBitsInternal(HDC hdc, HBITMAP bitmap, UINT start, UINT lines, PVOID bits, BITMAPINFO *user_info,
                                 UINT usage, UINT max_bits, UINT max_info)
{
    UCHAR raw[124];
    ULONG size;
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&size, user_info, sizeof(size))))
        return 0;
    BOOLEAN core = size == sizeof(BITMAPCOREHEADER);
    if (!core && (size < sizeof(BITMAPINFOHEADER) || size > sizeof(raw)))
        return 0;
    if (!NT_SUCCESS(MimeCopyFromUser(raw, user_info, size)))
        return 0;
    BITMAPINFOHEADER h;
    if (core) {
        const BITMAPCOREHEADER *c = (const BITMAPCOREHEADER *)raw;
        h = (BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER), c->bcWidth, c->bcHeight, c->bcPlanes, c->bcBitCount, BI_RGB};
    } else {
        memcpy(&h, raw, sizeof(h));
    }
    if (!lines)
        bits = nullptr;
    UserEnterRead();
    SURFACE *s = SurfaceLookup(bitmap);
    if (!s || usage > DIB_PAL_COLORS) {
        UserLeave();
        return 0;
    }
    if (h.biBitCount == 0) {
        UCHAR bpp = s->Bpp;
        if (core) {
            BITMAPCOREHEADER out = {sizeof(BITMAPCOREHEADER), (WORD)s->Width, (WORD)s->Height, 1, bpp >= 16 ? 24 : bpp};
            UserLeave();
            return NT_SUCCESS(MimeCopyToUser(user_info, &out, sizeof(out))) ? 1 : 0;
        }
        /* only the 40 bytes of a BITMAPINFOHEADER change, what a longer header has after them stays. */
        memset(raw, 0, sizeof(BITMAPINFOHEADER));
        BITMAPINFOHEADER *out = (BITMAPINFOHEADER *)raw;
        *out = (BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER), s->Width, s->Height, 1, bpp};
        out->biCompression = bpp == 16 || bpp == 32 ? BI_BITFIELDS : BI_RGB;
        out->biSizeImage = (DWORD)(dib_stride(s->Width, bpp) * s->Height);
        out->biClrUsed = out->biClrImportant = bpp <= 8 ? 1u << bpp : 0;
        UserLeave();
        return NT_SUCCESS(MimeCopyToUser(user_info, raw, sizeof(BITMAPINFOHEADER))) ? 1 : 0;
    }
    DIB_DESC d;
    BITMAPINFO tmp_info;
    tmp_info.bmiHeader = h;
    tmp_info.bmiHeader.biClrUsed = 0;
    if (!describe_dib(&tmp_info, DIB_RGB_COLORS, &d)) {
        UserLeave();
        return 0;
    }
    /* the header as windows leaves it: 40 bytes, the size of the image, no count of colours. */
    if (!core) {
        memset(raw + sizeof(BITMAPINFOHEADER), 0, size - sizeof(BITMAPINFOHEADER));
        h.biSize = sizeof(BITMAPINFOHEADER);
        h.biPlanes = 1;
        if (h.biCompression == BI_RGB || h.biCompression == BI_BITFIELDS)
            h.biSizeImage = (DWORD)((SIZE_T)d.Stride * d.Height);
        h.biClrUsed = h.biClrImportant = 0;
        memcpy(raw, &h, sizeof(h));
    }
    /* the masks of BI_BITFIELDS or the table, after the header. */
    union {
        DWORD Masks[3];
        RGBQUAD Quads[256];
        WORD Indices[256];
        RGBTRIPLE Triples[256];
    } table;
    SIZE_T table_size = 0;
    RGBQUAD out_colors[256];
    if (h.biCompression == BI_BITFIELDS && (d.Bpp == 16 || d.Bpp == 32)) {
        if (d.Bpp == 32)
            table.Masks[0] = 0xFF0000, table.Masks[1] = 0xFF00, table.Masks[2] = 0xFF;
        else if (s->Bpp == 16)
            memcpy(table.Masks, s->Masks, sizeof(table.Masks));
        else
            table.Masks[0] = 0x7C00, table.Masks[1] = 0x3E0, table.Masks[2] = 0x1F;
        table_size = sizeof(table.Masks);
    } else if (d.Bpp <= 8) {
        ULONG n = 1u << d.Bpp;
        RGBQUAD *colors = out_colors;
        if (s->Bpp > 8 || s->Bpp != d.Bpp)
            default_dib_table(d.Bpp, colors);
        else
            memcpy(colors, s->Colors, n * sizeof(RGBQUAD));
        for (ULONG i = 0; i < n; i++) {
            if (usage == DIB_PAL_COLORS)
                table.Indices[i] = (WORD)i;
            else if (core)
                table.Triples[i] = (RGBTRIPLE){colors[i].rgbBlue, colors[i].rgbGreen, colors[i].rgbRed};
            else
                table.Quads[i] = colors[i];
        }
        table_size = n * (usage == DIB_PAL_COLORS ? sizeof(WORD) : core ? sizeof(RGBTRIPLE) : sizeof(RGBQUAD));
    }
    int result = 0;
    if (!bits) {
        result = 1;
    } else if (start < (UINT)s->Height) {
        if (lines > (UINT)s->Height - start)
            lines = (UINT)s->Height - start;
        if (lines > (UINT)d.Height)
            lines = (UINT)d.Height;
        LONG width = d.Width < s->Width ? d.Width : s->Width;
        PUCHAR buffer = W32Alloc((SIZE_T)d.Stride * lines);
        SURFACE *dst = buffer ? SurfaceWrap(d.Width, (LONG)lines, d.Bpp, buffer, d.Stride, nullptr) : nullptr;
        if (dst) {
            if (d.Bpp <= 8) {
                memcpy(dst->Colors, out_colors, (SIZE_T)(1u << d.Bpp) * sizeof(RGBQUAD));
                dst->ColorCount = (USHORT)(1u << d.Bpp);
            } else if (h.biCompression == BI_BITFIELDS) {
                memcpy(dst->Masks, table.Masks, sizeof(table.Masks));
            }
            /* scan `start` counts from the bottom; a bottom up buffer starts with it, a top down one ends with it. */
            for (UINT i = 0; i < lines; i++) {
                LONG src_y = s->Height - 1 - (LONG)(start + i);
                LONG dst_y = d.TopDown ? (LONG)(lines - 1 - i) : (LONG)i;
                for (LONG x = 0; x < width; x++)
                    SurfacePutPixel(dst, x, dst_y, SurfaceGetPixel(s, x, src_y));
            }
            if (NT_SUCCESS(MimeCopyToUser(bits, buffer, (SIZE_T)d.Stride * lines)))
                result = (int)lines;
        }
        W32Free(dst);
        W32Free(buffer);
    }
    UserLeave();
    if (result) {
        if (core) {
            if (table_size)
                MimeCopyToUser((PUCHAR)user_info + sizeof(BITMAPCOREHEADER), &table, table_size);
        } else if (!NT_SUCCESS(MimeCopyToUser(user_info, raw, size)) ||
                   (table_size && !NT_SUCCESS(MimeCopyToUser((PUCHAR)user_info + sizeof(BITMAPINFOHEADER), &table,
                                                             table_size)))) {
            result = 0;
        }
    }
    return result;
}

/*
 * decodes run length coded rows into a 32 bit bottom up buffer. pixels a code covers get alpha 255, the others stay
 * 0, so blending the result leaves whatever a skip or an early end of line did not cover untouched, as drawing a
 * coded bitmap does. rows count from the bottom, like the codes. truncated or out of range codes stop the decoding
 * and keep what was drawn.
 */
static void decode_rle(const UCHAR *code, SIZE_T size, BOOLEAN four, LONG width, LONG height, const RGBQUAD *colors,
                       ULONG color_count, PULONG out)
{
    LONG x = 0, y = 0;
    SIZE_T i = 0;
    ULONG stride = (ULONG)width;
    while (i + 1 < size) {
        UCHAR count = code[i], value = code[i + 1];
        i += 2;
        if (count) {
            /* a run of one color, or in the 4 bit form of two colors in turn. */
            for (UCHAR n = 0; n < count; n++) {
                UCHAR index = four ? ((n & 1) ? (value & 15) : (value >> 4)) : value;
                if (x < width && y < height) {
                    RGBQUAD c = index < color_count ? colors[index] : (RGBQUAD){0, 0, 0, 0};
                    out[(ULONG)y * stride + (ULONG)x] = 0xFF000000u | (ULONG)c.rgbRed << 16 | (ULONG)c.rgbGreen << 8 |
                                                         c.rgbBlue;
                }
                x++;
            }
            continue;
        }
        if (value == 0) {
            x = 0;
            y++;
        } else if (value == 1) {
            return;
        } else if (value == 2) {
            if (i + 1 >= size)
                return;
            x += code[i];
            y += code[i + 1];
            i += 2;
        } else {
            /* absolute mode: value pixels follow, the bytes padded to a word. */
            SIZE_T bytes = four ? ((SIZE_T)value + 1) / 2 : value;
            if (i + bytes > size)
                bytes = size - i;
            for (UCHAR n = 0; n < value; n++) {
                SIZE_T at = four ? (SIZE_T)(n >> 1) : (SIZE_T)n;
                if (at >= bytes)
                    break;
                UCHAR index = four ? ((n & 1) ? (code[i + at] & 15) : (code[i + at] >> 4)) : code[i + at];
                if (x < width && y < height) {
                    RGBQUAD c = index < color_count ? colors[index] : (RGBQUAD){0, 0, 0, 0};
                    out[(ULONG)y * stride + (ULONG)x] = 0xFF000000u | (ULONG)c.rgbRed << 16 | (ULONG)c.rgbGreen << 8 |
                                                         c.rgbBlue;
                }
                x++;
            }
            i += bytes;
            if (bytes & 1)
                i++;
        }
        if (y >= height)
            return;
    }
}

/*
 * captures caller dib bits into a temporary kernel surface. a run length coded source comes back as 32 bits with
 * alpha and sets *rle: it has to be blended, not copied.
 */
static SURFACE *capture_dib(const void *bits, const BITMAPINFO *user_info, UINT usage, ULONG lines, PVOID *buffer,
                            BITMAPINFO **info_out, BOOLEAN *rle)
{
    DIB_DESC d;
    *rle = FALSE;
    BITMAPINFO *info = capture_info(user_info, usage, &d, TRUE);
    if (!info)
        return nullptr;
    if (d.Compression == BI_RLE8 || d.Compression == BI_RLE4) {
        SIZE_T size = info->bmiHeader.biSizeImage;
        SIZE_T pixels = (SIZE_T)d.Width * (SIZE_T)d.Height * sizeof(ULONG);
        PUCHAR code = size <= 0x4000000 ? W32Alloc(size) : nullptr;
        PULONG decoded = pixels <= 0x20000000 ? W32Alloc(pixels) : nullptr;
        if (!code || !decoded || !NT_SUCCESS(MimeCopyFromUser(code, bits, size))) {
            W32Free(code);
            W32Free(decoded);
            W32Free(info);
            return nullptr;
        }
        decode_rle(code, size, d.Compression == BI_RLE4, d.Width, d.Height, info->bmiColors, d.Colors, decoded);
        W32Free(code);
        DIB_DESC wide = d;
        wide.Bpp = 32;
        wide.Stride = d.Width * 4;
        wide.TopDown = FALSE;
        wide.Compression = BI_RGB;
        SURFACE *s = wrap_dib_bits(&wide, nullptr, decoded, (ULONG)d.Height, 0);
        if (!s) {
            W32Free(decoded);
            W32Free(info);
            return nullptr;
        }
        *buffer = decoded;
        *info_out = info;
        *rle = TRUE;
        return s;
    }
    if (lines == 0 || lines > (ULONG)d.Height)
        lines = (ULONG)d.Height;
    SIZE_T size = (SIZE_T)d.Stride * lines;
    PUCHAR copy = W32Alloc(size);
    if (!copy || !NT_SUCCESS(MimeCopyFromUser(copy, bits, size))) {
        W32Free(copy);
        W32Free(info);
        return nullptr;
    }
    SURFACE *s = wrap_dib_bits(&d, info, copy, lines, 0);
    if (!s) {
        W32Free(copy);
        W32Free(info);
        return nullptr;
    }
    *buffer = copy;
    *info_out = info;
    return s;
}

/*
 * CreateDIBitmap. a dib of two colors, black and white, makes a monochrome bitmap, CBM_CREATEDIB one of the format of
 * the dib, and any other one of the depth of the display, as user32 needs for the masks and pictures of icons.
 */
HBITMAP NTAPI NtGdiCreateDIBitmapInternal(HDC hdc, int width, int height, DWORD init, const void *bits,
                                          const BITMAPINFO *user_info, DWORD usage, UINT info_size, UINT bits_size,
                                          ULONG flags, HANDLE xform)
{
    (void)info_size;
    (void)bits_size;
    (void)flags;
    (void)xform;
    DIB_DESC d = {};
    BITMAPINFO *info = user_info ? capture_info(user_info, usage, &d, FALSE) : nullptr;
    /* a top down header gives a negative height, which the kernel takes as its size. */
    if (height < 0)
        height = -height;
    if ((user_info && !info) || ((init & 2) && !info) || width <= 0 || height <= 0 || usage > DIB_PAL_COLORS) {
        if (info)
            W32Free(info);
        return nullptr;
    }
    BOOLEAN mono = info && d.Bpp == 1 && usage == DIB_RGB_COLORS && d.Colors == 2;
    for (ULONG i = 0; mono && i < 2; i++) {
        RGBQUAD q = info->bmiColors[i];
        ULONG v = (ULONG)q.rgbRed | (ULONG)q.rgbGreen << 8 | (ULONG)q.rgbBlue << 16;
        mono = v == 0 || v == 0xFFFFFF;
    }
    if (mono && info->bmiColors[0].rgbRed == info->bmiColors[1].rgbRed)
        mono = FALSE;
    UCHAR bpp = (init & 2) ? d.Bpp : mono ? 1 : 32;
    /* without CBM_CREATEDIB windows makes a bitmap compatible with the dc, monochrome for a fresh memory dc. */
    HBITMAP compatible = !(init & 2) && hdc ? NtGdiCreateCompatibleBitmap(hdc, width, height) : nullptr;
    UserEnterRead();
    if (hdc && !DcLookup(hdc)) {
        UserLeave();
        W32Free(info);
        return nullptr;
    }
    SURFACE *s = compatible ? SurfaceLookup(compatible) : SurfaceCreate(width, height, bpp, 0);
    HBITMAP h = compatible;
    if (compatible) {
    } else if (s && GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP)) {
        h = (HBITMAP)s->Header.Handle;
        if ((init & 2) && bpp <= 8) {
            memcpy(s->Colors, info->bmiColors, d.Colors * sizeof(RGBQUAD));
            s->ColorCount = (USHORT)d.Colors;
        }
    } else if (s) {
        SurfaceDestroy(s);
        s = nullptr;
    }
    UserLeave();
    if (h && (init & CBM_INIT) && bits && info) {
        PVOID buffer;
        BITMAPINFO *captured;
        BOOLEAN coded;
        SURFACE *src = capture_dib(bits, user_info, usage, 0, &buffer, &captured, &coded);
        if (src) {
            UserEnterRead();
            if (SurfaceLookup(h) == s)
                for (LONG y = 0; y < s->Height && y < src->Height; y++)
                    for (LONG x = 0; x < s->Width && x < src->Width; x++)
                        SurfacePutPixel(s, x, y, SurfaceGetPixel(src, x, y));
            UserLeave();
            W32Free(src);
            W32Free(buffer);
            W32Free(captured);
        }
    }
    W32Free(info);
    return h;
}

int NTAPI NtGdiSetDIBitsToDeviceInternal(HDC hdc, int x, int y, DWORD width, DWORD height, int src_x, int src_y,
                                         DWORD start, DWORD lines, const void *bits, const BITMAPINFO *info,
                                         UINT usage, UINT max_bits, UINT max_info, BOOL transform, HANDLE xform)
{
    PVOID buffer;
    BITMAPINFO *captured;
    BOOLEAN coded;
    SURFACE *src = capture_dib(bits, info, usage, lines, &buffer, &captured, &coded);
    if (!src)
        return 0;
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    int result = 0;
    if (dc && dc->Surface) {
        /* src_y counts from the bottom of the full dib; this band starts at scan `start`. */
        LONG band_top = src->Height - 1 - ((LONG)src_y - (LONG)start) - (LONG)height + 1;
        LONG dx = x, dy = y;
        DcLogicalToSurface(dc, &dx, &dy);
        REGION clip;
        RgnInit(&clip);
        if (DcGetClip(dc, &clip)) {
            if (coded) {
                RECT to = {dx, dy, dx + (LONG)width, dy + (LONG)height};
                RECT from = {src_x, band_top, src_x + (LONG)width, band_top + (LONG)height};
                RECT bounds = {0, 0, src->Width, src->Height}, inside;
                if (RectIntersect(&inside, &from, &bounds) && inside.left == from.left && inside.right == from.right &&
                    inside.top == from.top && inside.bottom == from.bottom)
                    AlphaBlendSurface(dc->Surface, &clip, &to, src, &from, (BLENDFUNCTION){AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
            } else {
                BltSurface(dc->Surface, &clip, dx, dy, (LONG)width, (LONG)height, src, src_x, band_top, SRCCOPY, dc);
            }
            RECT r = {dx, dy, dx + (LONG)width, dy + (LONG)height};
            DcMarkDrawn(dc, &r, &clip);
        }
        RgnFree(&clip);
        result = (int)(lines ? lines : (DWORD)src->Height);
    }
    UserLeave();
    W32Free(src);
    W32Free(buffer);
    W32Free(captured);
    return result;
}

int NTAPI NtGdiStretchDIBitsInternal(HDC hdc, int x, int y, int width, int height, int src_x, int src_y,
                                     int src_width, int src_height, const void *bits, const BITMAPINFO *info,
                                     UINT usage, DWORD rop, UINT max_info, UINT max_bits, HANDLE xform)
{
    PVOID buffer;
    BITMAPINFO *captured;
    BOOLEAN coded;
    /* without bits or a header win32kfull only draws a rop that reads no source, as a PatBlt. */
    if (!bits || !info) {
        UCHAR code = (UCHAR)(rop >> 16);
        if ((code ^ (code << 2)) & 0xCC)
            return 0;
        UserEnterRead();
        DC *dc = DcLookup(hdc);
        BOOL ok = dc && GdiPatBltDc(dc, x, y, width, height, rop, nullptr);
        UserLeave();
        return ok;
    }
    SURFACE *src = capture_dib(bits, info, usage, 0, &buffer, &captured, &coded);
    if (!src)
        return 0;
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    int result = 0;
    if (dc && dc->Surface) {
        LONG x0 = x, y0 = y, x1 = x + width, y1 = y + height;
        DcLogicalToSurface(dc, &x0, &y0);
        DcLogicalToSurface(dc, &x1, &y1);
        RECT dst_rect = {x0, y0, x1, y1};
        /* src_y counts from the bottom of a bottom up dib and from the top of a top down one. */
        LONG top = captured && captured->bmiHeader.biHeight < 0 ? src_y : src->Height - src_y - src_height;
        RECT src_rect = {src_x, top, src_x + src_width, top + src_height};
        REGION clip;
        RgnInit(&clip);
        if (DcGetClip(dc, &clip)) {
            if (coded) {
                /* what the codes skip stays as it was, whatever the rop. a guess for the other rops. */
                if (src_rect.left >= 0 && src_rect.top >= 0 && src_rect.right <= src->Width &&
                    src_rect.bottom <= src->Height)
                    AlphaBlendSurface(dc->Surface, &clip, &dst_rect, src, &src_rect,
                                      (BLENDFUNCTION){AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
            } else {
                StretchSurface(dc->Surface, &clip, &dst_rect, src, &src_rect, rop, dc->State.StretchMode);
            }
            RECT dirty = {W32Min(x0, x1), W32Min(y0, y1), W32Max(x0, x1), W32Max(y0, y1)};
            DcMarkDrawn(dc, &dirty, &clip);
        }
        RgnFree(&clip);
        /* windows answers the height of the dib, whatever part of it was drawn. */
        result = src->Height;
    }
    UserLeave();
    W32Free(src);
    W32Free(buffer);
    W32Free(captured);
    return result;
}

/* ------------------------------------------------------------------ device dependent bits */

/* rows are top-down and padded to 16 bits, the layout of CreateBitmap. */
LONG NTAPI NtGdiGetBitmapBits(HBITMAP bitmap, ULONG size, PUCHAR user_bits)
{
    UserEnterRead();
    SURFACE *s = SurfaceLookup(bitmap);
    LONG result = 0;
    PUCHAR copy = nullptr;
    if (s) {
        LONG row = ddb_stride(s->Width, s->Bpp);
        ULONG total = (ULONG)row * (ULONG)s->Height;
        if (!user_bits) {
            result = (LONG)total;
        } else {
            ULONG n = size < total ? size : total;
            copy = n ? W32Alloc(n) : nullptr;
            if (copy) {
                LONG bytes = (s->Width * s->Bpp + 7) / 8;
                for (ULONG off = 0, y = 0; off < n; y++, off += (ULONG)row) {
                    ULONG chunk = n - off < (ULONG)row ? n - off : (ULONG)row;
                    memcpy(copy + off, s->Bits + (LONG_PTR)y * s->Stride, chunk < (ULONG)bytes ? chunk : (ULONG)bytes);
                }
                result = (LONG)n;
            }
        }
    }
    UserLeave();
    if (copy && !NT_SUCCESS(MimeCopyToUser(user_bits, copy, (SIZE_T)result)))
        result = 0;
    W32Free(copy);
    return result;
}

LONG NTAPI NtGdiSetBitmapBits(HBITMAP bitmap, ULONG size, const UCHAR *user_bits)
{
    if (!user_bits || !size || size > 256 * 1024 * 1024)
        return 0;
    PUCHAR copy = W32Alloc(size);
    if (!copy || !NT_SUCCESS(MimeCopyFromUser(copy, user_bits, size))) {
        W32Free(copy);
        return 0;
    }
    UserEnterRead();
    SURFACE *s = SurfaceLookup(bitmap);
    LONG result = 0;
    if (s) {
        LONG row = ddb_stride(s->Width, s->Bpp);
        ULONG total = (ULONG)row * (ULONG)s->Height;
        ULONG n = size < total ? size : total;
        LONG bytes = (s->Width * s->Bpp + 7) / 8;
        for (ULONG off = 0, y = 0; off < n; y++, off += (ULONG)row) {
            ULONG chunk = n - off < (ULONG)row ? n - off : (ULONG)row;
            memcpy(s->Bits + (LONG_PTR)y * s->Stride, copy + off, chunk < (ULONG)bytes ? chunk : (ULONG)bytes);
        }
        RECT all = {0, 0, s->Width, s->Height};
        SurfaceMarkDirty(s, &all);
        result = (LONG)n;
    }
    UserLeave();
    W32Free(copy);
    return result;
}
