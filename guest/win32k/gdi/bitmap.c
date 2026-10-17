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
        PVOID base = s->UserBits;
        SIZE_T size = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
    }
    W32Free(s);
}

void SurfaceMarkDirty(SURFACE *s, const RECT *r)
{
    RECT clipped, bounds = {0, 0, s->Width, s->Height};
    if (!RectIntersect(&clipped, r, &bounds))
        return;
    RectUnion(&s->Dirty, &s->Dirty, &clipped);
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
    LONG Height; /* positive */
    BOOLEAN TopDown;
    UCHAR Bpp;
    LONG Stride;
    ULONG Compression;
    ULONG Colors;
    ULONG HeaderSize; /* bytes of header plus color table or masks */
} DIB_DESC;

static BOOLEAN describe_dib(const BITMAPINFO *info, UINT usage, DIB_DESC *d)
{
    const BITMAPINFOHEADER *h = &info->bmiHeader;
    if (h->biSize < sizeof(BITMAPINFOHEADER) && h->biSize != 12)
        return FALSE;
    if (h->biSize == 12) /* BITMAPCOREHEADER is not supported */
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
    if (d->Compression != BI_RGB && d->Compression != BI_BITFIELDS)
        return FALSE;
    d->Stride = dib_stride(d->Width, d->Bpp);
    d->Colors = d->Bpp <= 8 ? (h->biClrUsed ? h->biClrUsed : 1u << d->Bpp) : 0;
    if (d->Colors > 256)
        return FALSE;
    d->HeaderSize = h->biSize + (d->Compression == BI_BITFIELDS && h->biSize == sizeof(BITMAPINFOHEADER) ? 12 : 0) +
                    d->Colors * (usage == DIB_PAL_COLORS ? 2 : 4);
    return TRUE;
}

/* captures a BITMAPINFO with its color table from user mode. */
static BITMAPINFO *capture_info(const BITMAPINFO *user, UINT usage, DIB_DESC *d)
{
    BITMAPINFOHEADER h;
    if (!NT_SUCCESS(MimeCopyFromUser(&h, user, sizeof(h))))
        return nullptr;
    BITMAPINFO *info = W32Alloc(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD) + 16);
    if (!info)
        return nullptr;
    info->bmiHeader = h;
    if (h.biSize > 4096 || !describe_dib(info, usage, d)) {
        W32Free(info);
        return nullptr;
    }
    SIZE_T table = d->HeaderSize - h.biSize;
    if (table && !NT_SUCCESS(MimeCopyFromUser(info->bmiColors, (const UCHAR *)user + h.biSize, table))) {
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

/* ------------------------------------------------------------------ bitmap services */

HBITMAP NTAPI NtGdiCreateBitmap(int width, int height, UINT planes, UINT bpp, const void *bits)
{
    if (width <= 0 || height <= 0 || planes != 1)
        return nullptr;
    UCHAR depth = bpp <= 1 ? 1 : bpp <= 4 ? 4 : bpp <= 8 ? 8 : bpp <= 16 ? 16 : bpp <= 24 ? 24 : 32;
    UserEnter();
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
    UserEnter();
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
    DIB_DESC d;
    BITMAPINFO *info = capture_info(user_info, usage, &d);
    if (!info)
        return nullptr;
    if (section) {
        /* file mapping backed dib sections are not supported yet. */
        W32Free(info);
        return nullptr;
    }
    SIZE_T size = (SIZE_T)d.Stride * (SIZE_T)d.Height;
    PVOID user = nullptr;
    SIZE_T alloc = size;
    if (!NT_SUCCESS(ZwAllocateVirtualMemory(NtCurrentProcess(), &user, 0, &alloc, MEM_RESERVE | MEM_COMMIT,
                                            PAGE_READWRITE))) {
        W32Free(info);
        return nullptr;
    }
    UserEnter();
    SURFACE *s = W32Alloc(sizeof(SURFACE));
    HBITMAP h = nullptr;
    if (s) {
        s->Width = d.Width;
        s->Height = d.Height;
        s->Bpp = d.Bpp;
        s->Flags = SURF_USER_BITS | (d.TopDown ? SURF_TOPDOWN : 0);
        s->UserBits = user;
        s->UserSize = size;
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
        if (GdiCreateHandle(&s->Header, GDI_TYPE_BITMAP))
            h = (HBITMAP)s->Header.Handle;
        else
            W32Free(s), s = nullptr;
    }
    UserLeave();
    W32Free(info);
    if (!h) {
        SIZE_T zero = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &user, &zero, MEM_RELEASE);
        return nullptr;
    }
    if (bits_out && !NT_SUCCESS(MimeCopyToUser(bits_out, &user, sizeof(user)))) {
        NtGdiDeleteObjectApp(h);
        return nullptr;
    }
    return h;
}

/* ------------------------------------------------------------------ dcs over caller memory */

/*
 * D3DKMTCreateDCFromMemory: a memory dc whose bitmap is the caller's
 * memory, rows top down at the caller's pitch, for the formats windows
 * takes. ddraw draws on its surfaces with gdi this way. the memory stays
 * the caller's.
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
    UserEnter();
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
        if (ref)
            dc->Dpi = ref->Dpi;
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
    UserEnter();
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

/* copies scanlines of a bitmap into a caller dib, converting formats. */
int NTAPI NtGdiGetDIBitsInternal(HDC hdc, HBITMAP bitmap, UINT start, UINT lines, PVOID bits, BITMAPINFO *user_info,
                                 UINT usage, UINT max_bits, UINT max_info)
{
    BITMAPINFOHEADER h;
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&h, user_info, sizeof(h))))
        return 0;
    UserEnter();
    SURFACE *s = SurfaceLookup(bitmap);
    int result = 0;
    if (!s) {
        UserLeave();
        return 0;
    }
    if (h.biBitCount == 0) {
        /* query: fill in the header from the bitmap. */
        BITMAPINFOHEADER out = {sizeof(BITMAPINFOHEADER), s->Width, s->Height, 1, s->Bpp, BI_RGB, 0, 0, 0, 0, 0};
        out.biSizeImage = (DWORD)(dib_stride(s->Width, s->Bpp) * s->Height);
        UserLeave();
        return NT_SUCCESS(MimeCopyToUser(user_info, &out, sizeof(out))) ? 1 : 0;
    }
    DIB_DESC d;
    BITMAPINFO tmp_info;
    tmp_info.bmiHeader = h;
    if (!describe_dib(&tmp_info, usage, &d) || d.Width != s->Width) {
        UserLeave();
        return 0;
    }
    if (start >= (UINT)s->Height) {
        UserLeave();
        return 0;
    }
    if (lines > (UINT)s->Height - start)
        lines = (UINT)s->Height - start;
    if (!bits) {
        UserLeave();
        return (int)lines;
    }
    PUCHAR buffer = W32Alloc((SIZE_T)d.Stride * lines);
    if (buffer) {
        SURFACE *dst = SurfaceWrap(d.Width, (LONG)lines, d.Bpp, buffer, d.Stride, nullptr);
        if (dst) {
            if (d.Bpp <= 8) {
                memcpy(dst->Colors, s->Colors, sizeof(s->Colors));
                dst->ColorCount = s->ColorCount ? s->ColorCount : (USHORT)(1u << d.Bpp);
            }
            /* scan `start` counts from the bottom of the bitmap. */
            for (UINT i = 0; i < lines; i++) {
                LONG src_y = s->Height - 1 - (LONG)(start + i);
                LONG dst_y = d.TopDown ? (LONG)i : (LONG)(lines - 1 - i);
                for (LONG x = 0; x < d.Width; x++)
                    SurfacePutPixel(dst, x, dst_y, SurfaceGetPixel(s, x, src_y));
            }
            W32Free(dst);
            if (NT_SUCCESS(MimeCopyToUser(bits, buffer, (SIZE_T)d.Stride * lines)))
                result = (int)lines;
            if (d.Bpp <= 8 && result)
                MimeCopyToUser((PUCHAR)user_info + h.biSize, s->Colors, d.Colors * sizeof(RGBQUAD));
        }
        W32Free(buffer);
    }
    UserLeave();
    return result;
}

/* captures caller dib bits and returns a temporary surface in kernel memory. */
static SURFACE *capture_dib(const void *bits, const BITMAPINFO *user_info, UINT usage, ULONG lines, PVOID *buffer,
                            BITMAPINFO **info_out)
{
    DIB_DESC d;
    BITMAPINFO *info = capture_info(user_info, usage, &d);
    if (!info)
        return nullptr;
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

int NTAPI NtGdiSetDIBitsToDeviceInternal(HDC hdc, int x, int y, DWORD width, DWORD height, int src_x, int src_y,
                                         DWORD start, DWORD lines, const void *bits, const BITMAPINFO *info,
                                         UINT usage, UINT max_bits, UINT max_info, BOOL transform, HANDLE xform)
{
    PVOID buffer;
    BITMAPINFO *captured;
    SURFACE *src = capture_dib(bits, info, usage, lines, &buffer, &captured);
    if (!src)
        return 0;
    UserEnter();
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
            BltSurface(dc->Surface, &clip, dx, dy, (LONG)width, (LONG)height, src, src_x, band_top, SRCCOPY, dc);
            RECT r = {dx, dy, dx + (LONG)width, dy + (LONG)height};
            SurfaceMarkDirty(dc->Surface, &r);
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
    SURFACE *src = capture_dib(bits, info, usage, 0, &buffer, &captured);
    if (!src)
        return 0;
    UserEnter();
    DC *dc = DcLookup(hdc);
    int result = 0;
    if (dc && dc->Surface) {
        LONG x0 = x, y0 = y, x1 = x + width, y1 = y + height;
        DcLogicalToSurface(dc, &x0, &y0);
        DcLogicalToSurface(dc, &x1, &y1);
        RECT dst_rect = {x0, y0, x1, y1};
        /* src_y is measured from the bottom of the dib. */
        RECT src_rect = {src_x, src->Height - src_y - src_height, src_x + src_width, src->Height - src_y};
        REGION clip;
        RgnInit(&clip);
        if (DcGetClip(dc, &clip)) {
            StretchSurface(dc->Surface, &clip, &dst_rect, src, &src_rect, rop, dc->State.StretchMode);
            RECT dirty = {W32Min(x0, x1), W32Min(y0, y1), W32Max(x0, x1), W32Max(y0, y1)};
            SurfaceMarkDirty(dc->Surface, &dirty);
        }
        RgnFree(&clip);
        result = src_height;
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
    UserEnter();
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
    UserEnter();
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
