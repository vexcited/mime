/* IDirectDrawPalette: 2, 4, 16 or 256 entries, stored as given. */
#include "ddraw_private.h"

static const IDirectDrawPaletteVtbl palette_vtbl;

DDPAL *DdPalFromIface(void *iface)
{
    if (!iface || *(const void **)iface != &palette_vtbl)
        return nullptr;
    DDPAL *p = CONTAINING_RECORD(iface, DDPAL, iface);
    return p->magic == PAL_MAGIC ? p : nullptr;
}

/* entries per palette caps, and bytes per entry: 1 for indices into another palette. */
static DWORD entry_count(DWORD caps)
{
    return (caps & DDPCAPS_1BIT) ? 2 : (caps & DDPCAPS_2BIT) ? 4 : (caps & DDPCAPS_4BIT) ? 16 : 256;
}

static DWORD entry_size(DWORD caps)
{
    return (caps & DDPCAPS_8BITENTRIES) ? 1 : 4;
}

/* the emulation layer keeps entries 0 and 255 of a plain 8 bit palette black and white. */
static void force_ends(DDPAL *p)
{
    if ((p->caps & (DDPCAPS_8BIT | DDPCAPS_ALLOW256)) != DDPCAPS_8BIT)
        return;
    p->entries[0] = (PALETTEENTRY){0, 0, 0, p->entries[0].peFlags};
    p->entries[255] = (PALETTEENTRY){255, 255, 255, p->entries[255].peFlags};
}

HRESULT DdCreatePalette(DDRAW *dd, DWORD flags, const PALETTEENTRY *entries, DDPAL **out)
{
    *out = nullptr;
    if (flags & ~0x7FF)
        return DDERR_INVALIDPARAMS;
    if (flags & (DDPCAPS_PRIMARYSURFACE | DDPCAPS_PRIMARYSURFACELEFT | DDPCAPS_VSYNC))
        return DDERR_INVALIDPARAMS;
    DWORD sizes = flags & (DDPCAPS_1BIT | DDPCAPS_2BIT | DDPCAPS_4BIT | DDPCAPS_8BIT);
    if (!sizes || (sizes & (sizes - 1)))
        return DDERR_INVALIDPARAMS;
    if ((flags & (DDPCAPS_ALPHA | DDPCAPS_8BITENTRIES)) == (DDPCAPS_ALPHA | DDPCAPS_8BITENTRIES) ||
        (flags & (DDPCAPS_8BIT | DDPCAPS_8BITENTRIES)) == (DDPCAPS_8BIT | DDPCAPS_8BITENTRIES) ||
        ((flags & DDPCAPS_ALLOW256) && !(flags & DDPCAPS_8BIT)))
        return DDERR_INVALIDPARAMS;
    if (!entries)
        return DDERR_INVALIDPARAMS;
    DDPAL *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return DDERR_OUTOFMEMORY;
    p->iface.lpVtbl = &palette_vtbl;
    p->ref = 1;
    p->magic = PAL_MAGIC;
    p->dd = dd;
    p->caps = flags & ~DDPCAPS_INITIALIZE;
    p->count = entry_count(flags);
    p->uniqueness = 1;
    memcpy(p->entries, entries, p->count * entry_size(flags));
    force_ends(p);
    InsertTailList(&dd->palettes, &p->links);
    *out = p;
    return DD_OK;
}

/* the directdraw object is gone: the palette lives on for outstanding references, detached from everything. */
void DdPalKill(DDPAL *p)
{
    RemoveEntryList(&p->links);
    InitializeListHead(&p->links);
    p->dd = nullptr;
}

static HRESULT STDMETHODCALLTYPE palette_query(IDirectDrawPalette *This, REFIID riid, void **out)
{
    DDPAL *p = DdPalFromIface(This);
    if (!p || !riid)
        return DDERR_INVALIDPARAMS;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirectDrawPalette))
        return DDERR_GENERIC;
    if (!out)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    p->ref++;
    *out = This;
    return DdLeave(DD_OK);
}

static ULONG STDMETHODCALLTYPE palette_add_ref(IDirectDrawPalette *This)
{
    DDPAL *p = DdPalFromIface(This);
    if (!p)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)++p->ref;
    DdLeave(DD_OK);
    return ref;
}

static ULONG STDMETHODCALLTYPE palette_release(IDirectDrawPalette *This)
{
    DDPAL *p = DdPalFromIface(This);
    if (!p)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)--p->ref;
    if (!ref) {
        void *owner = p->owner;
        if (p->dd && p->dd->primary && p->dd->primary->palette == p)
            p->dd->primary->palette = nullptr;
        RemoveEntryList(&p->links);
        p->magic = 0;
        HeapFree(GetProcessHeap(), 0, p);
        DdLeave(DD_OK);
        if (owner)
            ((IUnknown *)owner)->lpVtbl->Release(owner);
        return 0;
    }
    DdLeave(DD_OK);
    return ref;
}

static HRESULT STDMETHODCALLTYPE palette_get_caps(IDirectDrawPalette *This, LPDWORD caps)
{
    DDPAL *p = DdPalFromIface(This);
    if (!p || !caps)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    DWORD c = (p->caps & DDPCAPS_1BIT)   ? DDPCAPS_1BIT
              : (p->caps & DDPCAPS_2BIT) ? DDPCAPS_2BIT
              : (p->caps & DDPCAPS_4BIT) ? DDPCAPS_4BIT
                                         : DDPCAPS_8BIT;
    if (p->dd && p->dd->primary && p->dd->primary->palette == p)
        c |= DDPCAPS_PRIMARYSURFACE;
    c |= p->caps & (DDPCAPS_ALLOW256 | DDPCAPS_8BITENTRIES);
    *caps = c;
    return DdLeave(DD_OK);
}

static BOOL range_ok(DDPAL *p, DWORD flags, DWORD start, DWORD count)
{
    return !flags && count && count <= p->count && start < p->count && start + count <= p->count;
}

static HRESULT STDMETHODCALLTYPE palette_get_entries(IDirectDrawPalette *This, DWORD flags, DWORD start, DWORD count,
                                                     LPPALETTEENTRY entries)
{
    DDPAL *p = DdPalFromIface(This);
    if (!p || !entries)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    if (!range_ok(p, flags, start, count))
        return DdLeave(DDERR_INVALIDPARAMS);
    DWORD size = entry_size(p->caps);
    memcpy(entries, (const BYTE *)p->entries + start * size, count * size);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE palette_initialize(IDirectDrawPalette *This, LPDIRECTDRAW ddraw, DWORD flags,
                                                    LPPALETTEENTRY entries)
{
    return DDERR_ALREADYINITIALIZED;
}

/* a palette on the primary shows at once, as a palette display would. */
static HRESULT STDMETHODCALLTYPE palette_set_entries(IDirectDrawPalette *This, DWORD flags, DWORD start, DWORD count,
                                                     LPPALETTEENTRY entries)
{
    DDPAL *p = DdPalFromIface(This);
    if (!p || !entries)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    if (!range_ok(p, flags, start, count))
        return DdLeave(DDERR_INVALIDPARAMS);
    DWORD size = entry_size(p->caps);
    memcpy((BYTE *)p->entries + start * size, entries, count * size);
    if (start == 0 || start + count == 256)
        force_ends(p);
    if (!++p->uniqueness)
        p->uniqueness = 1;
    if (p->dd && p->dd->primary && p->dd->primary->palette == p)
        DdPresentDirty(p->dd->primary, nullptr);
    return DdLeave(DD_OK);
}

static const IDirectDrawPaletteVtbl palette_vtbl = {palette_query,      palette_add_ref,     palette_release,
                                                    palette_get_caps,   palette_get_entries, palette_initialize,
                                                    palette_set_entries};
