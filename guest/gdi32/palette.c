/*
 * gdi32 palettes. the display is true color and win32k has no palette
 * objects, so logical palettes live here: a small handle table whose handles
 * carry the palette type byte, and a record of the palette selected in each
 * dc. realizing a palette maps nothing on a true color device.
 */
#include "gdi32_private.h"
#include <mime/stub.h>

#define PAL_MAX 256
#define PAL_HANDLE_BASE 0x40080000u
#define PAL_DC_MAX 64

typedef struct PALETTE {
    UINT Count;
    PALETTEENTRY Entries[1];
} PALETTE;

/* the 20 static colors of the windows default palette. */
static struct {
    UINT Count;
    PALETTEENTRY Entries[20];
} g_default = {20,
               {{0x00, 0x00, 0x00, 0}, {0x80, 0x00, 0x00, 0}, {0x00, 0x80, 0x00, 0}, {0x80, 0x80, 0x00, 0},
                {0x00, 0x00, 0x80, 0}, {0x80, 0x00, 0x80, 0}, {0x00, 0x80, 0x80, 0}, {0xC0, 0xC0, 0xC0, 0},
                {0xC0, 0xDC, 0xC0, 0}, {0xA6, 0xCA, 0xF0, 0}, {0xFF, 0xFB, 0xF0, 0}, {0xA0, 0xA0, 0xA4, 0},
                {0x80, 0x80, 0x80, 0}, {0xFF, 0x00, 0x00, 0}, {0x00, 0xFF, 0x00, 0}, {0xFF, 0xFF, 0x00, 0},
                {0x00, 0x00, 0xFF, 0}, {0xFF, 0x00, 0xFF, 0}, {0x00, 0xFF, 0xFF, 0}, {0xFF, 0xFF, 0xFF, 0}}};

static RTL_SRWLOCK g_lock;
static PALETTE *g_palettes[PAL_MAX];
static struct {
    HDC Dc;
    HPALETTE Palette;
} g_selected[PAL_DC_MAX];

static HPALETTE make_handle(UINT index)
{
    return (HPALETTE)(ULONG_PTR)(PAL_HANDLE_BASE | index);
}

/* the palette of a handle; the caller holds the lock. */
static PALETTE *lookup(HPALETTE h)
{
    ULONG_PTR v = (ULONG_PTR)h;
    if ((v & ~(ULONG_PTR)0xFFFF) != PAL_HANDLE_BASE || (v & 0xFFFF) >= PAL_MAX)
        return nullptr;
    if (!(v & 0xFFFF))
        return (PALETTE *)&g_default;
    return g_palettes[v & 0xFFFF];
}

static BOOL dc_valid(HDC dc)
{
    DWORD v;
    return NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v);
}

HPALETTE PalDefault(void)
{
    return make_handle(0);
}

BOOL PalIsPalette(HGDIOBJ h)
{
    RtlAcquireSRWLockShared(&g_lock);
    BOOL ok = lookup(h) != nullptr;
    RtlReleaseSRWLockShared(&g_lock);
    return ok;
}

BOOL PalDelete(HPALETTE h)
{
    if (h == PalDefault())
        return TRUE;
    RtlAcquireSRWLockExclusive(&g_lock);
    PALETTE *p = lookup(h);
    if (p) {
        g_palettes[(ULONG_PTR)h & 0xFFFF] = nullptr;
        for (UINT i = 0; i < PAL_DC_MAX; i++)
            if (g_selected[i].Palette == h)
                g_selected[i].Dc = nullptr, g_selected[i].Palette = nullptr;
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
    return p != nullptr;
}

/* like windows, a palette object reads as its entry count. */
int PalGetObject(HPALETTE h, int size, LPVOID buffer)
{
    RtlAcquireSRWLockShared(&g_lock);
    PALETTE *p = lookup(h);
    WORD count = p ? (WORD)p->Count : 0;
    RtlReleaseSRWLockShared(&g_lock);
    if (!p)
        return 0;
    if (!buffer)
        return sizeof(WORD);
    if (size > (int)sizeof(WORD))
        size = sizeof(WORD);
    if (size > 0)
        memcpy(buffer, &count, (size_t)size);
    return size > 0 ? size : 0;
}

HPALETTE PalCurrent(HDC dc)
{
    HPALETTE result = PalDefault();
    RtlAcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < PAL_DC_MAX; i++)
        if (g_selected[i].Dc == dc)
            result = g_selected[i].Palette;
    RtlReleaseSRWLockShared(&g_lock);
    return result;
}

void PalForgetDC(HDC dc)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < PAL_DC_MAX; i++)
        if (g_selected[i].Dc == dc)
            g_selected[i].Dc = nullptr, g_selected[i].Palette = nullptr;
    RtlReleaseSRWLockExclusive(&g_lock);
}

WINGDIAPI HPALETTE WINAPI CreatePalette(const LOGPALETTE *logical)
{
    if (!logical) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    UINT count = logical->palNumEntries;
    PALETTE *p = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0,
                                 offsetof(PALETTE, Entries) + (count ? count : 1) * sizeof(PALETTEENTRY));
    if (!p) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    p->Count = count;
    memcpy(p->Entries, logical->palPalEntry, count * sizeof(PALETTEENTRY));
    HPALETTE h = nullptr;
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 1; i < PAL_MAX; i++) {
        if (!g_palettes[i]) {
            g_palettes[i] = p;
            h = make_handle(i);
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!h) {
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return h;
}

WINGDIAPI HPALETTE WINAPI SelectPalette(HDC dc, HPALETTE palette, BOOL background)
{
    if (MfActive(dc)) {
        if (MfIsWmfDc(dc)) {
            HGDIOBJ previous = nullptr;
            return WmfSelectObject(dc, palette, &previous) ? previous : nullptr;
        }
        if (!PalIsPalette(palette) || !MfSelectPalette(dc, palette))
            return nullptr;
    }
    if (!dc_valid(dc) || !PalIsPalette(palette)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    HPALETTE old = PalDefault();
    int free_slot = -1;
    BOOL stored = palette == PalDefault();
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < PAL_DC_MAX; i++) {
        if (g_selected[i].Dc == dc) {
            old = g_selected[i].Palette;
            if (palette == PalDefault())
                g_selected[i].Dc = nullptr, g_selected[i].Palette = nullptr;
            else
                g_selected[i].Palette = palette;
            stored = TRUE;
        } else if (!g_selected[i].Dc && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (!stored && free_slot >= 0) {
        g_selected[free_slot].Dc = dc;
        g_selected[free_slot].Palette = palette;
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    return old;
}

/* a true color device has no hardware palette to map entries into. */
WINGDIAPI UINT WINAPI RealizePalette(HDC dc)
{
    MF_RECORD(dc, MfRecordEmpty(dc, EMR_REALIZEPALETTE, META_REALIZEPALETTE), GDI_ERROR, 0);
    if (!dc_valid(dc)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GDI_ERROR;
    }
    return 0;
}

WINGDIAPI UINT WINAPI GetPaletteEntries(HPALETTE palette, UINT start, UINT count, LPPALETTEENTRY entries)
{
    UINT n = 0;
    RtlAcquireSRWLockShared(&g_lock);
    PALETTE *p = lookup(palette);
    if (p) {
        if (!entries) {
            n = p->Count;
        } else if (start < p->Count) {
            n = count < p->Count - start ? count : p->Count - start;
            memcpy(entries, p->Entries + start, n * sizeof(PALETTEENTRY));
        }
    }
    RtlReleaseSRWLockShared(&g_lock);
    if (!p)
        SetLastError(ERROR_INVALID_HANDLE);
    return n;
}

WINGDIAPI UINT WINAPI SetPaletteEntries(HPALETTE palette, UINT start, UINT count, const PALETTEENTRY *entries)
{
    if (palette == PalDefault() || !entries)
        return 0;
    UINT n = 0;
    RtlAcquireSRWLockExclusive(&g_lock);
    PALETTE *p = lookup(palette);
    if (p && start < p->Count) {
        n = count < p->Count - start ? count : p->Count - start;
        memcpy(p->Entries + start, entries, n * sizeof(PALETTEENTRY));
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!p)
        SetLastError(ERROR_INVALID_HANDLE);
    return n;
}

static UINT nearest_index(const PALETTE *p, COLORREF color)
{
    int r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    UINT best = 0;
    UINT best_distance = 0xFFFFFFFF;
    for (UINT i = 0; i < p->Count; i++) {
        int dr = p->Entries[i].peRed - r, dg = p->Entries[i].peGreen - g, db = p->Entries[i].peBlue - b;
        UINT d = (UINT)(dr * dr + dg * dg + db * db);
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    return best;
}

WINGDIAPI UINT WINAPI GetNearestPaletteIndex(HPALETTE palette, COLORREF color)
{
    UINT index = CLR_INVALID;
    RtlAcquireSRWLockShared(&g_lock);
    PALETTE *p = lookup(palette);
    if (p && p->Count)
        index = nearest_index(p, color);
    RtlReleaseSRWLockShared(&g_lock);
    return index;
}

/* a true color device has no system palette, as on windows. */
WINGDIAPI UINT WINAPI GetSystemPaletteEntries(HDC dc, UINT start, UINT count, LPPALETTEENTRY entries)
{
    if (!dc_valid(dc))
        SetLastError(ERROR_INVALID_HANDLE);
    return 0;
}

/* every color is exact on a true color device; palette indices read the selected palette. */
WINGDIAPI COLORREF WINAPI GetNearestColor(HDC dc, COLORREF color)
{
    if (!dc_valid(dc)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return CLR_INVALID;
    }
    if ((color >> 24) == 0x01) {
        HPALETTE h = PalCurrent(dc);
        UINT index = color & 0xFFFF;
        RtlAcquireSRWLockShared(&g_lock);
        PALETTE *p = lookup(h);
        if (!p || index >= p->Count)
            p = (PALETTE *)&g_default, index = 0;
        PALETTEENTRY e = p->Entries[index];
        RtlReleaseSRWLockShared(&g_lock);
        return RGB(e.peRed, e.peGreen, e.peBlue);
    }
    return color & 0x00FFFFFF;
}

/* dib color tables live in win32k, which offers no call to change them. */
WINGDIAPI UINT WINAPI SetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD *colors)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}
