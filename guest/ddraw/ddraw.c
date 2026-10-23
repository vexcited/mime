/*
 * the directdraw object behind IDirectDraw, 2, 4 and 7. like on Windows, each
 * interface counts its own references, and the object goes when all reach
 * zero, taking what was created through it. every version calls the same
 * methods with its own structures.
 */
#include "ddraw_private.h"

static const IDirectDrawVtbl dd1_vtbl;
static const IDirectDraw2Vtbl dd2_vtbl;
static const IDirectDraw4Vtbl dd4_vtbl;
static const IDirectDraw7Vtbl dd7_vtbl;

DDRAW *DdFromIface(void *iface)
{
    if (!iface)
        return nullptr;
    const void *vtbl = *(const void **)iface;
    DDRAW *dd = nullptr;
    if (vtbl == &dd1_vtbl)
        dd = CONTAINING_RECORD(iface, DDRAW, iface1);
    else if (vtbl == &dd2_vtbl)
        dd = CONTAINING_RECORD(iface, DDRAW, iface2);
    else if (vtbl == &dd4_vtbl)
        dd = CONTAINING_RECORD(iface, DDRAW, iface4);
    else if (vtbl == &dd7_vtbl)
        dd = CONTAINING_RECORD(iface, DDRAW, iface7);
    return dd && dd->magic == DDRAW_MAGIC ? dd : nullptr;
}

static DWORD version_of(void *iface)
{
    const void *vtbl = *(const void **)iface;
    return vtbl == &dd1_vtbl ? DDV1 : vtbl == &dd2_vtbl ? DDV2 : vtbl == &dd4_vtbl ? DDV4 : DDV7;
}

void *DdIface(DDRAW *dd, DWORD version)
{
    switch (version) {
    case DDV1:
        return &dd->iface1;
    case DDV2:
        return &dd->iface2;
    case DDV4:
        return &dd->iface4;
    default:
        return &dd->iface7;
    }
}

static LONG *ref_of(DDRAW *dd, DWORD version)
{
    switch (version) {
    case DDV1:
        return &dd->ref1;
    case DDV2:
        return &dd->ref2;
    case DDV4:
        return &dd->ref4;
    default:
        return &dd->ref7;
    }
}

/* ------------------------------------------------------------------ lifetime */

HRESULT DdCreate(const GUID *guid, DWORD version, void **out, BOOL initialize)
{
    DDRAW *dd = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dd));
    if (!dd)
        return DDERR_OUTOFMEMORY;
    dd->iface1.lpVtbl = &dd1_vtbl;
    dd->iface2.lpVtbl = &dd2_vtbl;
    dd->iface4.lpVtbl = &dd4_vtbl;
    dd->iface7.lpVtbl = &dd7_vtbl;
    dd->magic = DDRAW_MAGIC;
    dd->initialized = initialize;
    dd->dx7 = version == DDV7;
    InitializeListHead(&dd->surfaces);
    InitializeListHead(&dd->palettes);
    InitializeListHead(&dd->clippers);
    *ref_of(dd, version) = 1;
    dd->ref = 1;
    DdEnter();
    InsertTailList(&DdObjects, &dd->links);
    DdLeave(DD_OK);
    *out = DdIface(dd, version);
    return DD_OK;
}

static void destroy(DDRAW *dd)
{
#ifndef _WIN64
    D3dObjDestroy(dd);
#endif
    /* objects made through it go too, and programs still holding them find them dead. */
    while (!IsListEmpty(&dd->surfaces))
        DdSurfKill(CONTAINING_RECORD(dd->surfaces.Flink, DDSURF, links));
    while (!IsListEmpty(&dd->palettes))
        DdPalKill(CONTAINING_RECORD(dd->palettes.Flink, DDPAL, links));
    while (!IsListEmpty(&dd->clippers))
        DdClipKill(CONTAINING_RECORD(dd->clippers.Flink, DDCLIP, links));
    DdPresentStop(dd);
    if (DdIsExclusiveOwner(dd) || !DdExclusiveOwner())
        DdRestoreDisplayMode(dd);
    DdReleaseExclusive(dd);
    DdSetCooperativeLevel(dd, DDV7, nullptr, DDSCL_NORMAL);
    if ((dd->flags & DDF_DEVICEWINDOW) && dd->device && IsWindow(dd->device))
        DestroyWindow(dd->device);
    RemoveEntryList(&dd->links);
    dd->magic = 0;
    HeapFree(GetProcessHeap(), 0, dd);
}

ULONG DdAddRef(DDRAW *dd, LONG *iface_ref)
{
    DdEnter();
    dd->ref++;
    ULONG ref = (ULONG)++*iface_ref;
    DdLeave(DD_OK);
    return ref;
}

/* an interface already at zero stays there, as on Windows. */
ULONG DdRelease(DDRAW *dd, LONG *iface_ref)
{
    DdEnter();
    if (*iface_ref <= 0)
        return (ULONG)DdLeave(0);
    ULONG ref = (ULONG)--*iface_ref;
    if (!--dd->ref)
        destroy(dd);
    DdLeave(DD_OK);
    return ref;
}

/* IUnknown is IDirectDraw. */
HRESULT DdQuery(DDRAW *dd, DWORD caller, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!riid)
        return DDERR_INVALIDPARAMS;
    DWORD version;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectDraw))
        version = DDV1;
    else if (IsEqualGUID(riid, &IID_IDirectDraw2))
        version = DDV2;
    else if (IsEqualGUID(riid, &IID_IDirectDraw4))
        version = DDV4;
    else if (IsEqualGUID(riid, &IID_IDirectDraw7))
        version = DDV7;
    else
#ifdef _WIN64
        return E_NOINTERFACE;
#else
        return D3dObjQuery(dd, caller, riid, out);
#endif
    DdAddRef(dd, ref_of(dd, version));
    *out = DdIface(dd, version);
    return S_OK;
}

/* the initialized object with the lock held, or NULL with the error to return. */
static DDRAW *enter(void *iface, HRESULT *hr)
{
    DDRAW *dd = DdFromIface(iface);
    if (!dd) {
        *hr = DDERR_INVALIDOBJECT;
        return nullptr;
    }
    DdEnter();
    if (!dd->initialized) {
        *hr = DdLeave(DDERR_NOTINITIALIZED);
        return nullptr;
    }
    *hr = DD_OK;
    return dd;
}

/* ------------------------------------------------------------------ methods */

static HRESULT STDMETHODCALLTYPE dd_query(void *This, REFIID riid, void **out)
{
    DDRAW *dd = DdFromIface(This);
    if (!dd)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    return DdLeave(DdQuery(dd, version_of(This), riid, out));
}

static ULONG STDMETHODCALLTYPE dd_add_ref(void *This)
{
    DDRAW *dd = DdFromIface(This);
    return dd ? DdAddRef(dd, ref_of(dd, version_of(This))) : 0;
}

static ULONG STDMETHODCALLTYPE dd_release(void *This)
{
    DDRAW *dd = DdFromIface(This);
    return dd ? DdRelease(dd, ref_of(dd, version_of(This))) : 0;
}

static HRESULT STDMETHODCALLTYPE dd_compact(void *This)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DdIsExclusiveOwner(dd) ? DD_OK : DDERR_NOEXCLUSIVEMODE);
}

static HRESULT STDMETHODCALLTYPE dd_create_clipper(void *This, DWORD flags, LPDIRECTDRAWCLIPPER *out,
                                                   IUnknown *outer)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (outer)
        return DdLeave(CLASS_E_NOAGGREGATION);
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDCLIP *c;
    hr = DdCreateClipper(dd, flags, &c, TRUE);
    *out = SUCCEEDED(hr) ? &c->iface : nullptr;
    if (SUCCEEDED(hr)) {
        DWORD version = version_of(This);
        c->version = version;
        if (version >= DDV4) {
            c->owner = This;
            DdAddRef(dd, ref_of(dd, version));
        }
    }
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE dd_create_palette(void *This, DWORD flags, LPPALETTEENTRY entries,
                                                   LPDIRECTDRAWPALETTE *out, IUnknown *outer)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (outer)
        return DdLeave(CLASS_E_NOAGGREGATION);
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    if (flags & ~0x7FF)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!(dd->flags & DDF_LEVELSET))
        return DdLeave(DDERR_NOCOOPERATIVELEVELSET);
    DDPAL *p;
    hr = DdCreatePalette(dd, flags, entries, &p);
    if (SUCCEEDED(hr)) {
        *out = &p->iface;
        /* a palette of a later object keeps the interface it came from, as surfaces do. */
        DWORD version = version_of(This);
        if (version >= DDV4) {
            p->owner = This;
            DdAddRef(dd, ref_of(dd, version));
        }
    }
    return DdLeave(hr);
}

/* version 1 and 2 descs are converted both ways. */
static HRESULT STDMETHODCALLTYPE dd_create_surface(void *This, void *desc_in, void **out, IUnknown *outer)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DWORD version = version_of(This);
    if (outer)
        return DdLeave(CLASS_E_NOAGGREGATION);
    if (!out || !desc_in)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!(dd->flags & DDF_LEVELSET))
        return DdLeave(DDERR_NOCOOPERATIVELEVELSET);
    DDSURFACEDESC2 desc;
    if (version < DDV4) {
        const DDSURFACEDESC *d1 = desc_in;
        if (d1->dwSize != sizeof(DDSURFACEDESC))
            return DdLeave(DDERR_INVALIDPARAMS);
        DdSurfDescFromV1(d1, &desc);
    } else {
        const DDSURFACEDESC2 *d2 = desc_in;
        if (d2->dwSize != sizeof(DDSURFACEDESC2))
            return DdLeave(DDERR_INVALIDPARAMS);
        desc = *d2;
    }
    *out = nullptr;
    DDSURF *s;
    hr = DdCreateSurface(dd, version, &desc, &s);
    if (SUCCEEDED(hr)) {
        *out = DdSurfIface(s, version);
        /* surfaces of IDirectDraw4 and 7 hold the interface that made them. */
        if (version >= DDV4) {
            s->creator = This;
            DdAddRef(dd, ref_of(dd, version));
        }
    }
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE dd_enum_display_modes(void *This, DWORD flags, void *desc, LPVOID context,
                                                       void *callback)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DWORD version = version_of(This);
    DDSURFACEDESC2 filter, *f = nullptr;
    if (desc) {
        if (version < DDV4) {
            if (((DDSURFACEDESC *)desc)->dwSize != sizeof(DDSURFACEDESC))
                return DdLeave(DDERR_INVALIDPARAMS);
            DdSurfDescFromV1(desc, &filter);
        } else {
            if (((DDSURFACEDESC2 *)desc)->dwSize != sizeof(DDSURFACEDESC2))
                return DdLeave(DDERR_INVALIDPARAMS);
            filter = *(DDSURFACEDESC2 *)desc;
        }
        f = &filter;
    }
    return DdLeave(DdEnumDisplayModes(dd, version, flags, f, context, callback));
}

/* takes either desc size, and writes the size of the interface's desc. */
static HRESULT STDMETHODCALLTYPE dd_get_display_mode(void *This, void *desc)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DWORD version = version_of(This);
    if (!desc || (((DDSURFACEDESC *)desc)->dwSize != sizeof(DDSURFACEDESC) &&
                  ((DDSURFACEDESC *)desc)->dwSize != sizeof(DDSURFACEDESC2)))
        return DdLeave(DDERR_INVALIDPARAMS);
    DDMODE mode;
    DdCurrentMode(&mode);
    DDSURFACEDESC2 d;
    DdFillModeDesc(version, &d, mode.width, mode.height, mode.bpp, mode.refresh);
    DWORD size = ((DDSURFACEDESC *)desc)->dwSize;
    memset(desc, 0, size);
    if (version < DDV4)
        DdSurfDescToV1(&d, desc);
    else
        memcpy(desc, &d, sizeof(DDSURFACEDESC2) < size ? sizeof(DDSURFACEDESC2) : size);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE dd_get_monitor_frequency(void *This, LPDWORD frequency)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!frequency)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDMODE mode;
    DdCurrentMode(&mode);
    *frequency = mode.refresh;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE dd_get_scan_line(void *This, LPDWORD line)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!line)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(DdGetScanLine(line));
}

static HRESULT STDMETHODCALLTYPE dd_get_vertical_blank_status(void *This, LPBOOL blank)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!blank)
        return DdLeave(DDERR_INVALIDPARAMS);
    DWORD line;
    *blank = DdGetScanLine(&line) == DDERR_VERTICALBLANKINPROGRESS;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE dd_initialize(void *This, GUID *guid)
{
    DDRAW *dd = DdFromIface(This);
    if (!dd)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    if (dd->initialized)
        return DdLeave(DDERR_ALREADYINITIALIZED);
    if (guid && guid != (GUID *)DDCREATE_HARDWAREONLY && guid != (GUID *)DDCREATE_EMULATIONONLY &&
        !IsEqualGUID(guid, &GUID_NULL))
        return DdLeave(DDERR_INVALIDDIRECTDRAWGUID);
    dd->initialized = TRUE;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE dd_restore_display_mode(void *This)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DdRestoreDisplayMode(dd));
}

static HRESULT STDMETHODCALLTYPE dd_set_cooperative_level(void *This, HWND window, DWORD flags)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DdSetCooperativeLevel(dd, version_of(This), window, flags));
}

static HRESULT STDMETHODCALLTYPE dd1_set_display_mode(IDirectDraw *This, DWORD width, DWORD height, DWORD bpp)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DdSetDisplayMode(dd, width, height, bpp, 0, 0));
}

static HRESULT STDMETHODCALLTYPE dd_set_display_mode(void *This, DWORD width, DWORD height, DWORD bpp, DWORD refresh,
                                                     DWORD flags)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DdSetDisplayMode(dd, width, height, bpp, refresh, flags));
}

static HRESULT STDMETHODCALLTYPE dd_wait_for_vertical_blank(void *This, DWORD flags, HANDLE event)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if ((flags & DDWAITVB_BLOCKBEGINEVENT) || event)
        return DdLeave(DDERR_UNSUPPORTED);
    if (flags != DDWAITVB_BLOCKBEGIN && flags != DDWAITVB_BLOCKEND)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDMODE mode;
    DdCurrentMode(&mode);
    if ((dd->flags & DDF_MODEX) && mode.width == 320)
        return DdLeave(DDERR_UNSUPPORTED);
    /* wait without the lock, since other threads keep drawing meanwhile. */
    DdLeave(DD_OK);
    return DdWaitForVerticalBlank(flags);
}

static HRESULT STDMETHODCALLTYPE dd_duplicate_surface(void *This, void *source, void **out)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DDSURF *src = DdSurfFromIface(source);
    if (!src || !out)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (src->lost)
        return DdLeave(DDERR_SURFACELOST);
    *out = nullptr;
    if ((src->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) || src->implicit)
        return DdLeave(DDERR_CANTDUPLICATE);
    DDSURF *copy;
    hr = DdSurfDuplicate(dd, src, &copy);
    if (SUCCEEDED(hr))
        *out = DdSurfIface(copy, DdSurfVersionOf(source));
    return DdLeave(hr);
}

/* existing surfaces matched field by field, or creatable ones, found by creating them. */
static HRESULT STDMETHODCALLTYPE dd_enum_surfaces(void *This, DWORD flags, void *desc_in, LPVOID context,
                                                  void *callback)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DWORD version = version_of(This);
    DDSURFACEDESC2 desc, *d = nullptr;
    if (desc_in) {
        if (version < DDV4) {
            if (((DDSURFACEDESC *)desc_in)->dwSize != sizeof(DDSURFACEDESC))
                return DdLeave(DDERR_INVALIDPARAMS);
            DdSurfDescFromV1(desc_in, &desc);
        } else {
            if (((DDSURFACEDESC2 *)desc_in)->dwSize != sizeof(DDSURFACEDESC2))
                return DdLeave(DDERR_INVALIDPARAMS);
            desc = *(DDSURFACEDESC2 *)desc_in;
        }
        d = &desc;
    }
    if (!callback || (flags & ~0x1Fu))
        return DdLeave(DDERR_INVALIDPARAMS);
    DWORD match = flags & (DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH);
    if (flags & DDENUMSURFACES_ALL) {
        if (match)
            return DdLeave(DDERR_INVALIDPARAMS);
    } else if (!d || match == (DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH)) {
        return DdLeave(DDERR_INVALIDPARAMS);
    }
    if ((flags & DDENUMSURFACES_CANBECREATED) && (flags & 7) != DDENUMSURFACES_MATCH)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (d) {
        if ((d->dwFlags & DDSD_LPSURFACE) || (d->ddsCaps.dwCaps & 0x03000000))
            return DdLeave(DDERR_INVALIDPARAMS);
        if (d->ddsCaps.dwCaps & DDSCAPS_OPTIMIZED)
            return DdLeave(DD_OK);
        if ((d->ddsCaps.dwCaps2 & 1) || (d->ddsCaps.dwCaps3 & 0xFFFFE000) || d->ddsCaps.dwCaps4)
            return DdLeave(DDERR_INVALIDCAPS);
        if (d->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER)
            return DdLeave(DDERR_INVALIDPARAMS);
        if (!(d->dwFlags & DDSD_HEIGHT) != !(d->dwFlags & DDSD_WIDTH))
            return DdLeave(DDERR_INVALIDPARAMS);
        if ((flags & DDENUMSURFACES_CANBECREATED) &&
            (d->dwFlags & (DDSD_CKDESTOVERLAY | DDSD_CKDESTBLT | DDSD_CKSRCOVERLAY | DDSD_CKSRCBLT)))
            return DdLeave(DDERR_INVALIDPARAMS);
    }
    if (!(flags & (DDENUMSURFACES_CANBECREATED | DDENUMSURFACES_DOESEXIST)))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (flags & DDENUMSURFACES_CANBECREATED)
        return DdLeave(DdEnumCreatable(dd, version, d, context, callback));
    /* newest first, from a snapshot so callbacks may release what they get. */
    DWORD count = 0;
    for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink)
        count++;
    DDSURF **list = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(*list));
    if (!list)
        return DdLeave(DDERR_OUTOFMEMORY);
    DWORD n = 0;
    for (LIST_ENTRY *e = dd->surfaces.Blink; e != &dd->surfaces; e = e->Blink) {
        DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
        if (s->desc.ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER)
            continue;
        BOOL take = (flags & DDENUMSURFACES_ALL) || (DdSurfMatches(s, d) == !(flags & DDENUMSURFACES_NOMATCH));
        if (take) {
            list[n++] = s;
            DdSurfAddRef(s, DdSurfRefOf(s, version == DDV2 ? DDV1 : version));
        }
    }
    for (DWORD i = 0; i < n; i++) {
        DDSURF *s = list[i];
        BOOL stop = FALSE;
        if (!stop) {
            DDSURFACEDESC2 out;
            DdSurfFillDesc(s, version, &out);
            void *iface = DdSurfIface(s, version == DDV2 ? DDV1 : version);
            RtlLeaveCriticalSection(&DdLock);
            HRESULT r = version < DDV4 ? ((LPDDENUMSURFACESCALLBACK)callback)(iface, (DDSURFACEDESC *)&out, context)
                                       : ((LPDDENUMSURFACESCALLBACK7)callback)(iface, &out, context);
            RtlEnterCriticalSection(&DdLock);
            if (r == DDENUMRET_CANCEL) {
                for (DWORD j = i + 1; j < n; j++)
                    DdSurfRelease(list[j], DdSurfRefOf(list[j], version == DDV2 ? DDV1 : version));
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE dd_flip_to_gdi_surface(void *This)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DDSURF *p = dd->primary;
    if (!p)
        return DdLeave(DDERR_NOTFOUND);
    if (!(p->desc.ddsCaps.dwCaps & DDSCAPS_FLIP))
        return DdLeave(DDERR_NOTFLIPPABLE);
    if (p->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
        return DdLeave(DD_OK);
    return DdLeave(DdSurfFlipToGdi(p));
}

static HRESULT STDMETHODCALLTYPE dd_get_caps(void *This, LPDDCAPS driver, LPDDCAPS emulation)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DdGetCaps(dd, driver, emulation));
}

/* four character codes surfaces may have: block compressed and packed yuv. */
static const DWORD FOURCC_CODES[] = {FOURCC_DXT1, FOURCC_DXT2, FOURCC_DXT3, FOURCC_DXT4, FOURCC_DXT5,
                                     MAKEFOURCC('Y', 'U', 'Y', '2'), MAKEFOURCC('U', 'Y', 'V', 'Y')};

DWORD DdFourCCCount(void)
{
    return RTL_NUMBER_OF(FOURCC_CODES);
}

static HRESULT STDMETHODCALLTYPE dd_get_fourcc_codes(void *This, LPDWORD count, LPDWORD codes)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!count)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!codes) {
        *count = RTL_NUMBER_OF(FOURCC_CODES);
        return DdLeave(DD_OK);
    }
    DWORD n = *count < RTL_NUMBER_OF(FOURCC_CODES) ? *count : RTL_NUMBER_OF(FOURCC_CODES);
    memcpy(codes, FOURCC_CODES, n * sizeof(DWORD));
    *count = n;
    return DdLeave(DD_OK);
}

/* the surface gdi shows: the primary, unless a flip moved its memory to the first back buffer. */
static HRESULT STDMETHODCALLTYPE dd_get_gdi_surface(void *This, void **out)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    DDSURF *p = dd->primary;
    if (!p)
        return DdLeave(DDERR_NOTFOUND);
    DDSURF *s = DdSurfGdiSurface(p);
    if (!s)
        return DdLeave(DDERR_NOTFOUND);
    DWORD version = version_of(This);
    DWORD sv = version == DDV7 ? DDV7 : version == DDV4 ? DDV4 : s->version < DDV4 ? s->version : DDV1;
    DdSurfAddRef(s, DdSurfRefOf(s, sv));
    *out = DdSurfIface(s, sv);
    return DdLeave(DD_OK);
}

/* mime's video memory: a fixed local pool minus what surfaces use, and as much again nonlocal. */
#define VIDEO_MEMORY (512u << 20)

static HRESULT available_vid_mem(DDRAW *dd, const DDSCAPS2 *caps, LPDWORD total, LPDWORD free_bytes)
{
    if ((caps->dwCaps & 0x03000000) || (caps->dwCaps2 & 1) || (caps->dwCaps3 & 0xFFFFE000) || caps->dwCaps4)
        return DDERR_INVALIDCAPS;
    if (caps->dwCaps & 0x5893C)
        return DDERR_INVALIDPARAMS;
    if (!total && !free_bytes)
        return DDERR_INVALIDPARAMS;
    DWORD local = caps->dwCaps & DDSCAPS_LOCALVIDMEM ? 1 : 0, nonlocal = caps->dwCaps & DDSCAPS_NONLOCALVIDMEM ? 1 : 0;
    if (!local && !nonlocal) {
        local = 1;
        nonlocal = (caps->dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_EXECUTEBUFFER | DDSCAPS_VIDEOMEMORY)) != 0;
    }
    DDMODE mode;
    DdCurrentMode(&mode);
    ULONG64 used = DdVideoMemoryUsed();
    if (!used)
        used = (ULONG64)mode.width * mode.height * (mode.bpp == 32 ? 4 : mode.bpp == 24 ? 3 : mode.bpp >= 15 ? 2 : 1);
    ULONG64 t = 0, f = 0;
    if (local) {
        t += VIDEO_MEMORY;
        f += used < VIDEO_MEMORY ? VIDEO_MEMORY - used : 0;
    }
    if (nonlocal) {
        t += VIDEO_MEMORY;
        f += VIDEO_MEMORY;
    }
    if (total)
        *total = (DWORD)t;
    if (free_bytes)
        *free_bytes = (DWORD)f;
    return DD_OK;
}

HRESULT DdGetVidMem(DDRAW *dd, DWORD caps, DWORD *total, DWORD *free_bytes)
{
    DDSCAPS2 c = {.dwCaps = caps};
    return available_vid_mem(dd, &c, total, free_bytes);
}

static HRESULT STDMETHODCALLTYPE dd_get_available_vid_mem(void *This, void *caps_in, LPDWORD total, LPDWORD free_bytes)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!caps_in)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDSCAPS2 caps = {};
    if (version_of(This) == DDV2)
        caps.dwCaps = ((DDSCAPS *)caps_in)->dwCaps;
    else
        caps = *(DDSCAPS2 *)caps_in;
    return DdLeave(available_vid_mem(dd, &caps, total, free_bytes));
}

/* the surface owning a dc from IDirectDrawSurface::GetDC. IDirectDraw4 gets the first surface interface. */
static HRESULT STDMETHODCALLTYPE dd_get_surface_from_dc(void *This, HDC dc, void **out)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    DWORD sv = version_of(This) == DDV7 ? DDV7 : DDV1;
    for (LIST_ENTRY *o = DdObjects.Flink; o != &DdObjects; o = o->Flink) {
        DDRAW *owner = CONTAINING_RECORD(o, DDRAW, links);
        for (LIST_ENTRY *e = owner->surfaces.Flink; e != &owner->surfaces; e = e->Flink) {
            DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
            if (s->dc && s->dc == dc) {
                DdSurfAddRef(s, DdSurfRefOf(s, sv));
                *out = DdSurfIface(s, sv);
                return DdLeave(DD_OK);
            }
        }
    }
    return DdLeave(DDERR_NOTFOUND);
}

/* oldest first, implicit surfaces are restored with their root. */
static HRESULT STDMETHODCALLTYPE dd_restore_all_surfaces(void *This)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink) {
        DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
        if (s->implicit)
            continue;
        hr = DdSurfRestore(s);
        if (FAILED(hr))
            break;
    }
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE dd_test_cooperative_level(void *This)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (dd->flags & DDF_EXCLUSIVE)
        return DdLeave(DdIsExclusiveOwner(dd) ? DD_OK : DDERR_NOEXCLUSIVEMODE);
    if (DdExclusiveOwner() && !DdIsExclusiveOwner(dd))
        return DdLeave(DDERR_EXCLUSIVEMODEALREADYSET);
    DDMODE mode;
    DdCurrentMode(&mode);
    if (dd->snapshot_width && (mode.width != dd->snapshot_width || mode.height != dd->snapshot_height ||
                               mode.bpp != dd->snapshot_bpp))
        return DdLeave(DDERR_WRONGMODE);
    return DdLeave(DD_OK);
}

/*
 * the adapter as dxgi sees it. like Windows, the guid is CLSID_DirectDraw
 * xored with the vendor, device and subsystem. the driver version stays 0,
 * as with WDDM.
 */
static HRESULT device_identifier(DDDEVICEIDENTIFIER2 *id, DWORD flags)
{
    if (flags & ~DDGDI_GETHOSTIDENTIFIER)
        return DDERR_INVALIDPARAMS;
    memset(id, 0, sizeof(*id));
    DISPLAY_DEVICEA display = {sizeof(display)};
    if (!EnumDisplayDevicesA(nullptr, 0, &display, 0)) {
        id->dwWHQLLevel = 1;
        return DDERR_NOTFOUND;
    }
    lstrcpynA(id->szDescription, display.DeviceString, 128);
    lstrcpynA(id->szDriver, "D3D9on12.dll", sizeof(id->szDriver));
    DdAdapterIds(&id->dwVendorId, &id->dwDeviceId, &id->dwSubSysId, &id->dwRevision);
    GUID g = CLSID_DirectDraw;
    g.Data1 ^= id->dwVendorId;
    DWORD word;
    memcpy(&word, &g.Data2, 4);
    word ^= id->dwDeviceId;
    memcpy(&g.Data2, &word, 4);
    ULONG64 tail;
    memcpy(&tail, g.Data4, 8);
    tail ^= (ULONG64)id->liDriverVersion.QuadPart ^ ((ULONG64)id->dwSubSysId | (ULONG64)id->dwRevision << 32);
    memcpy(g.Data4, &tail, 8);
    id->guidDeviceIdentifier = g;
    id->dwWHQLLevel = 1;
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE dd4_get_device_identifier(void *This, LPDDDEVICEIDENTIFIER id, DWORD flags)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    DDDEVICEIDENTIFIER2 full;
    hr = device_identifier(&full, flags);
    if (!id)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (hr != DDERR_INVALIDPARAMS)
        memcpy(id, &full, sizeof(*id));
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE dd7_get_device_identifier(void *This, LPDDDEVICEIDENTIFIER2 id, DWORD flags)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    if (!id)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDDEVICEIDENTIFIER2 full;
    hr = device_identifier(&full, flags);
    if (hr != DDERR_INVALIDPARAMS)
        *id = full;
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE dd_start_mode_test(void *This, LPSIZE modes, DWORD count, DWORD flags)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DDERR_TESTFINISHED);
}

static HRESULT STDMETHODCALLTYPE dd_evaluate_mode(void *This, DWORD flags, DWORD *timeout)
{
    HRESULT hr;
    DDRAW *dd = enter(This, &hr);
    if (!dd)
        return hr;
    return DdLeave(DDERR_INVALIDPARAMS);
}

/* ------------------------------------------------------------------ vtables */

#define COMMON(T)                                                                                                     \
    (void *)dd_query, (void *)dd_add_ref, (void *)dd_release, (void *)dd_compact, (void *)dd_create_clipper,           \
        (void *)dd_create_palette, (void *)dd_create_surface, (void *)dd_duplicate_surface,                            \
        (void *)dd_enum_display_modes, (void *)dd_enum_surfaces, (void *)dd_flip_to_gdi_surface,                       \
        (void *)dd_get_caps, (void *)dd_get_display_mode, (void *)dd_get_fourcc_codes, (void *)dd_get_gdi_surface,     \
        (void *)dd_get_monitor_frequency, (void *)dd_get_scan_line, (void *)dd_get_vertical_blank_status,               \
        (void *)dd_initialize, (void *)dd_restore_display_mode, (void *)dd_set_cooperative_level

static const IDirectDrawVtbl dd1_vtbl = {COMMON(IDirectDraw), (void *)dd1_set_display_mode,
                                         (void *)dd_wait_for_vertical_blank};

static const IDirectDraw2Vtbl dd2_vtbl = {COMMON(IDirectDraw2), (void *)dd_set_display_mode,
                                          (void *)dd_wait_for_vertical_blank, (void *)dd_get_available_vid_mem};

static const IDirectDraw4Vtbl dd4_vtbl = {COMMON(IDirectDraw4),
                                          (void *)dd_set_display_mode,
                                          (void *)dd_wait_for_vertical_blank,
                                          (void *)dd_get_available_vid_mem,
                                          (void *)dd_get_surface_from_dc,
                                          (void *)dd_restore_all_surfaces,
                                          (void *)dd_test_cooperative_level,
                                          (void *)dd4_get_device_identifier};

static const IDirectDraw7Vtbl dd7_vtbl = {COMMON(IDirectDraw7),
                                          (void *)dd_set_display_mode,
                                          (void *)dd_wait_for_vertical_blank,
                                          (void *)dd_get_available_vid_mem,
                                          (void *)dd_get_surface_from_dc,
                                          (void *)dd_restore_all_surfaces,
                                          (void *)dd_test_cooperative_level,
                                          (void *)dd7_get_device_identifier,
                                          (void *)dd_start_mode_test,
                                          (void *)dd_evaluate_mode};
