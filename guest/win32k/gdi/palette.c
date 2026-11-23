/*
 * palette objects, as gdi32 of 26300 asks for them and measured with pal.exe of scratch/dlls/cursor in the
 * research repository. the display is true color: a palette selected into a dc only decides what PALETTEINDEX
 * colors are, realizing maps nothing and the system palette is not the device's.
 */
#include "../win32k.h"

/* CreateHalftonePalette on any dc, measured with paldump.exe. its first and last ten are the stock palette's. */
static const ULONG g_halftone[256] = {
    0x00000000, 0x00000080, 0x00008000, 0x00008080, 0x00800000, 0x00800080,
    0x00808000, 0x00c0c0c0, 0x00c0dcc0, 0x00f0caa6, 0x04330000, 0x04000033,
    0x04330033, 0x04333300, 0x04161616, 0x041c1c1c, 0x04222222, 0x04292929,
    0x04555555, 0x044d4d4d, 0x04424242, 0x04393939, 0x04807cff, 0x045050ff,
    0x049300d6, 0x04ffeccc, 0x04c6d6ef, 0x04d6e7e7, 0x0490a9ad, 0x0400ff33,
    0x04000066, 0x04000099, 0x040000cc, 0x04003300, 0x04003333, 0x04003366,
    0x04003399, 0x040033cc, 0x040033ff, 0x04006600, 0x04006633, 0x04006666,
    0x04006699, 0x040066cc, 0x040066ff, 0x04009900, 0x04009933, 0x04009966,
    0x04009999, 0x040099cc, 0x040099ff, 0x0400cc00, 0x0400cc33, 0x0400cc66,
    0x0400cc99, 0x0400cccc, 0x0400ccff, 0x0400ff66, 0x0400ff99, 0x0400ffcc,
    0x0433ff00, 0x04ff0033, 0x04330066, 0x04330099, 0x043300cc, 0x043300ff,
    0x04ff3300, 0x04333333, 0x04333366, 0x04333399, 0x043333cc, 0x043333ff,
    0x04336600, 0x04336633, 0x04336666, 0x04336699, 0x043366cc, 0x043366ff,
    0x04339900, 0x04339933, 0x04339966, 0x04339999, 0x043399cc, 0x043399ff,
    0x0433cc00, 0x0433cc33, 0x0433cc66, 0x0433cc99, 0x0433cccc, 0x0433ccff,
    0x0433ff33, 0x0433ff66, 0x0433ff99, 0x0433ffcc, 0x0433ffff, 0x04660000,
    0x04660033, 0x04660066, 0x04660099, 0x046600cc, 0x046600ff, 0x04663300,
    0x04663333, 0x04663366, 0x04663399, 0x046633cc, 0x046633ff, 0x04666600,
    0x04666633, 0x04666666, 0x04666699, 0x046666cc, 0x04669900, 0x04669933,
    0x04669966, 0x04669999, 0x046699cc, 0x046699ff, 0x0466cc00, 0x0466cc33,
    0x0466cc99, 0x0466cccc, 0x0466ccff, 0x0466ff00, 0x0466ff33, 0x0466ff99,
    0x0466ffcc, 0x04cc00ff, 0x04ff00cc, 0x04999900, 0x04993399, 0x04990099,
    0x049900cc, 0x04990000, 0x04993333, 0x04990066, 0x049933cc, 0x049900ff,
    0x04996600, 0x04996633, 0x04993366, 0x04996699, 0x049966cc, 0x049933ff,
    0x04999933, 0x04999966, 0x04999999, 0x049999cc, 0x049999ff, 0x0499cc00,
    0x0499cc33, 0x0466cc66, 0x0499cc99, 0x0499cccc, 0x0499ccff, 0x0499ff00,
    0x0499ff33, 0x0499cc66, 0x0499ff99, 0x0499ffcc, 0x0499ffff, 0x04cc0000,
    0x04990033, 0x04cc0066, 0x04cc0099, 0x04cc00cc, 0x04993300, 0x04cc3333,
    0x04cc3366, 0x04cc3399, 0x04cc33cc, 0x04cc33ff, 0x04cc6600, 0x04cc6633,
    0x04996666, 0x04cc6699, 0x04cc66cc, 0x049966ff, 0x04cc9900, 0x04cc9933,
    0x04cc9966, 0x04cc9999, 0x04cc99cc, 0x04cc99ff, 0x04cccc00, 0x04cccc33,
    0x04cccc66, 0x04cccc99, 0x04cccccc, 0x04ccccff, 0x04ccff00, 0x04ccff33,
    0x0499ff66, 0x04ccff99, 0x04ccffcc, 0x04ccffff, 0x04cc0033, 0x04ff0066,
    0x04ff0099, 0x04cc3300, 0x04ff3333, 0x04ff3366, 0x04ff3399, 0x04ff33cc,
    0x04ff33ff, 0x04ff6600, 0x04ff6633, 0x04cc6666, 0x04ff6699, 0x04ff66cc,
    0x04cc66ff, 0x04ff9900, 0x04ff9933, 0x04ff9966, 0x04ff9999, 0x04ff99cc,
    0x04ff99ff, 0x04ffcc00, 0x04ffcc33, 0x04ffcc66, 0x04ffcc99, 0x04ffcccc,
    0x04ffccff, 0x04ffff33, 0x04ccff66, 0x04ffff99, 0x04ffffcc, 0x046666ff,
    0x0466ff66, 0x0466ffff, 0x04ff6666, 0x04ff66ff, 0x04ffff66, 0x042100a5,
    0x045f5f5f, 0x04777777, 0x04868686, 0x04969696, 0x04cbcbcb, 0x04b2b2b2,
    0x04d7d7d7, 0x04dddddd, 0x04e3e3e3, 0x04eaeaea, 0x04f1f1f1, 0x04f8f8f8,
    0x00f0fbff, 0x00a4a0a0, 0x00808080, 0x000000ff, 0x0000ff00, 0x0000ffff,
    0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00ffffff,
};

#define PALETTE_MAX 1024

PALETTE *PaletteLookup(HPALETTE handle)
{
    return (PALETTE *)GdiLookup(handle, GDI_TYPE_PALETTE);
}

static PALETTE *palette_create(ULONG count)
{
    PALETTE *p = W32Alloc(sizeof(PALETTE));
    if (!p)
        return nullptr;
    p->Entries = W32Alloc(count * sizeof(PALETTEENTRY));
    if (!p->Entries) {
        W32Free(p);
        return nullptr;
    }
    p->Count = count;
    if (!GdiCreateHandle(&p->Header, GDI_TYPE_PALETTE)) {
        W32Free(p->Entries);
        W32Free(p);
        return nullptr;
    }
    return p;
}

void PaletteFree(PALETTE *p)
{
    GdiFreeHandle(&p->Header);
    W32Free(p->Entries);
    W32Free(p);
}

/* DEFAULT_PALETTE: the twenty static colors. */
PALETTE *PaletteCreateStock(void)
{
    PALETTE *p = palette_create(20);
    if (!p)
        return nullptr;
    for (ULONG i = 0; i < 20; i++)
        memcpy(&p->Entries[i], &g_halftone[i < 10 ? i : 236 + i], sizeof(PALETTEENTRY));
    return p;
}

/* the palette a dc draws PALETTEINDEX colors with: the one selected, else the stock one. */
static PALETTE *dc_palette(DC *dc)
{
    return dc && dc->Palette ? dc->Palette : (PALETTE *)GdiStockObject(DEFAULT_PALETTE);
}

static ULONG entry_rgb(const PALETTEENTRY *e)
{
    return (ULONG)e->peRed | (ULONG)e->peGreen << 8 | (ULONG)e->peBlue << 16;
}

/* a COLORREF as plain rgb: PALETTEINDEX, any color with bit 24, is the entry of the palette, the first past its end. */
COLORREF PaletteResolve(DC *dc, COLORREF color)
{
    if (!(color & 0x01000000))
        return color & 0xFFFFFF;
    PALETTE *p = dc_palette(dc);
    if (!p || !p->Count)
        return 0;
    ULONG index = color & 0xFFFF;
    return entry_rgb(&p->Entries[index < p->Count ? index : 0]);
}

ULONG DcColorPixel(DC *dc, COLORREF color)
{
    return ColorToPixel(PaletteResolve(dc, color));
}

static ULONG distance(ULONG a, ULONG b)
{
    LONG dr = (LONG)(a & 0xFF) - (LONG)(b & 0xFF), dg = (LONG)((a >> 8) & 0xFF) - (LONG)((b >> 8) & 0xFF),
         db = (LONG)((a >> 16) & 0xFF) - (LONG)((b >> 16) & 0xFF);
    return (ULONG)(dr * dr + dg * dg + db * db);
}

/* the entry nearest a color, the first of equals. */
static ULONG nearest_entry(const PALETTE *p, COLORREF rgb)
{
    ULONG best = 0, best_distance = 0xFFFFFFFF;
    for (ULONG i = 0; i < p->Count; i++) {
        ULONG d = distance(entry_rgb(&p->Entries[i]), rgb);
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    return best;
}

HPALETTE NTAPI NtGdiCreatePaletteInternal(const LOGPALETTE *user_palette, UINT count)
{
    LOGPALETTE head;
    if (!count || !user_palette || !NT_SUCCESS(MimeCopyFromUser(&head, user_palette, FIELD_OFFSET(LOGPALETTE, palPalEntry))) ||
        head.palVersion != 0x300 || count > 0xFFFF)
        return nullptr;
    UserEnterRead();
    PALETTE *p = palette_create(count);
    HPALETTE h = nullptr;
    if (p) {
        if (NT_SUCCESS(MimeCopyFromUser(p->Entries, user_palette->palPalEntry, count * sizeof(PALETTEENTRY))))
            h = (HPALETTE)p->Header.Handle;
        else
            PaletteFree(p);
    }
    UserLeave();
    return h;
}

HPALETTE NTAPI NtGdiCreateHalftonePalette(HDC hdc)
{
    (void)hdc;
    UserEnterRead();
    PALETTE *p = palette_create(256);
    HPALETTE h = nullptr;
    if (p) {
        memcpy(p->Entries, g_halftone, sizeof(g_halftone));
        h = (HPALETTE)p->Header.Handle;
    }
    UserLeave();
    return h;
}

/* the functions of gdi32: 0 animate, 1 set, 2 get, 3 the system palette, 4 and 5 the color table of a dib. */
static ULONG do_palette(HGDIOBJ object, WORD start, WORD count, PVOID user_entries, DWORD function)
{
    PALETTE *p = PaletteLookup(object);
    if (!p) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (function == 2 && !user_entries)
        return p->Count;
    if (start >= p->Count)
        return 0;
    ULONG n = W32Min(count, (LONG)(p->Count - start));
    if (function == 2) {
        if (!NT_SUCCESS(MimeCopyToUser(user_entries, p->Entries + start, n * sizeof(PALETTEENTRY))))
            return 0;
        return n;
    }
    /* the stock palette never changes. */
    if (p->Header.Flags & GDI_FLAG_STOCK)
        return 0;
    PALETTEENTRY in[256];
    ULONG done = 0;
    while (done < n) {
        ULONG chunk = W32Min((LONG)(n - done), 256);
        if (!NT_SUCCESS(MimeCopyFromUser(in, (PALETTEENTRY *)user_entries + done, chunk * sizeof(PALETTEENTRY))))
            return 0;
        for (ULONG i = 0; i < chunk; i++) {
            PALETTEENTRY *e = &p->Entries[start + done + i];
            /* AnimatePalette changes only the entries made PC_RESERVED. */
            if (function == 1 || (e->peFlags & PC_RESERVED))
                *e = in[i];
        }
        done += chunk;
    }
    return function == 0 ? 1 : n;
}

ULONG NTAPI NtGdiDoPalette(HGDIOBJ object, WORD start, WORD count, PVOID user_entries, DWORD function, BOOL inbound)
{
    (void)inbound;
    if (function == MIME_PAL_GETDIBCOLORTABLE || function == MIME_PAL_SETDIBCOLORTABLE)
        return GdiDibColorTable((HDC)object, start, count, user_entries, function == MIME_PAL_SETDIBCOLORTABLE);
    /* the system palette exists only on a palette device, and gdi32 asks for it only there. */
    if (function > 2)
        return 0;
    UserEnterRead();
    ULONG n = do_palette(object, start, count, user_entries, function);
    UserLeave();
    return n;
}

BOOL NTAPI NtGdiResizePalette(HPALETTE palette, UINT count)
{
    UserEnterRead();
    PALETTE *p = PaletteLookup(palette);
    BOOL ok = FALSE;
    if (p && !(p->Header.Flags & GDI_FLAG_STOCK) && count && count <= PALETTE_MAX) {
        PALETTEENTRY *entries = W32Alloc(count * sizeof(PALETTEENTRY));
        if (entries) {
            memcpy(entries, p->Entries, W32Min((LONG)count, (LONG)p->Count) * sizeof(PALETTEENTRY));
            W32Free(p->Entries);
            p->Entries = entries;
            p->Count = count;
            ok = TRUE;
        }
    }
    UserLeave();
    return ok;
}

UINT NTAPI NtGdiGetNearestPaletteIndex(HPALETTE palette, COLORREF color)
{
    UserEnterRead();
    PALETTE *p = PaletteLookup(palette);
    UINT index = CLR_INVALID;
    if (!p)
        W32SetLastError(ERROR_INVALID_HANDLE);
    else if (color & 0x01000000)
        index = (color & 0xFFFF) < p->Count ? color & 0xFFFF : 0;
    else
        index = nearest_entry(p, color & 0xFFFFFF);
    UserLeave();
    return index;
}

/* the color a dc would draw: through its palette, then the nearest the surface holds. */
COLORREF NTAPI NtGdiGetNearestColor(HDC hdc, COLORREF color)
{
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    COLORREF result = CLR_INVALID;
    if (dc) {
        result = PaletteResolve(dc, color);
        SURFACE *s = dc->Surface;
        if (s && s->Bpp <= 8 && s->ColorCount) {
            ULONG best = 0, best_distance = 0xFFFFFFFF;
            for (ULONG i = 0; i < s->ColorCount; i++) {
                RGBQUAD q = s->Colors[i];
                ULONG rgb = (ULONG)q.rgbRed | (ULONG)q.rgbGreen << 8 | (ULONG)q.rgbBlue << 16, d = distance(rgb, result);
                if (d < best_distance) {
                    best_distance = d;
                    best = rgb;
                }
            }
            result = best;
        }
    } else {
        W32SetLastError(ERROR_INVALID_HANDLE);
    }
    UserLeave();
    return result;
}

/* the old palette, the stock one for a dc that has none of its own; 0 and no change for what is not a palette. */
HPALETTE NTAPI NtUserSelectPalette(HDC hdc, HPALETTE palette, BOOL background)
{
    (void)background;
    UserEnterRead();
    DC *dc = DcLookup(hdc);
    PALETTE *p = PaletteLookup(palette);
    HPALETTE old = nullptr;
    if (dc && p) {
        PALETTE *was = dc_palette(dc);
        old = was ? (HPALETTE)was->Header.Handle : nullptr;
        DcSelectPalette(dc, (p->Header.Flags & GDI_FLAG_STOCK) ? nullptr : p);
    }
    UserLeave();
    return old;
}

/* a true color display maps no entry. */
UINT NTAPI NtUserRealizePalette(HDC hdc)
{
    UserEnterRead();
    UINT n = DcLookup(hdc) ? 0 : GDI_ERROR;
    UserLeave();
    return n;
}

UINT NTAPI NtGdiGetSystemPaletteUse(HDC hdc)
{
    (void)hdc;
    return SYSPAL_ERROR;
}

UINT NTAPI NtGdiSetSystemPaletteUse(HDC hdc, UINT use)
{
    (void)hdc;
    (void)use;
    return SYSPAL_ERROR;
}

/* a palette is realized again on its next selection: nothing to forget on a true color display. */
BOOL NTAPI NtGdiUnrealizeObject(HGDIOBJ object)
{
    UserEnterRead();
    BOOL ok = GdiLookup(object, 0) != nullptr;
    UserLeave();
    return ok;
}
