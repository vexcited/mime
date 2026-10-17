/*
 * IDirectDrawSurface, 2, 3, 4 and 7, and the gamma and color controls on
 * them. one object per surface holds an interface of every version, each
 * counting its own references as windows' do, so a pointer stays the
 * same for the surface's life. surfaces made together with a complex
 * one are implicit: they die with their root. see docs/ddraw.md.
 */
#include "ddraw_private.h"
#include <mime/ntuser.h>

static const IDirectDrawSurfaceVtbl surf1_vtbl;
static const IDirectDrawSurface2Vtbl surf2_vtbl;
static const IDirectDrawSurface3Vtbl surf3_vtbl;
static const IDirectDrawSurface4Vtbl surf4_vtbl;
static const IDirectDrawSurface7Vtbl surf7_vtbl;
static const IDirectDrawGammaControlVtbl gamma_vtbl;
static const IDirectDrawColorControlVtbl color_vtbl;

/* the code windows returns for DDSCAPS_OPTIMIZED surfaces, which have no public name. */
#define DDERR_OPTIMIZED MAKE_DDHRESULT(655)
#define DDVGAMMA 100
#define DDVCOLOR 101

DWORD DdSurfVersionOf(void *iface)
{
    const void *vtbl = iface ? *(const void **)iface : nullptr;
    if (vtbl == &surf1_vtbl)
        return DDV1;
    if (vtbl == &surf2_vtbl)
        return DDV2;
    if (vtbl == &surf3_vtbl)
        return DDV3;
    if (vtbl == &surf4_vtbl)
        return DDV4;
    if (vtbl == &surf7_vtbl)
        return DDV7;
    if (vtbl == &gamma_vtbl)
        return DDVGAMMA;
    if (vtbl == &color_vtbl)
        return DDVCOLOR;
    return 0;
}

DDSURF *DdSurfFromIface(void *iface)
{
    DDSURF *s;
    switch (DdSurfVersionOf(iface)) {
    case DDV1:
        s = CONTAINING_RECORD(iface, DDSURF, iface1);
        break;
    case DDV2:
        s = CONTAINING_RECORD(iface, DDSURF, iface2);
        break;
    case DDV3:
        s = CONTAINING_RECORD(iface, DDSURF, iface3);
        break;
    case DDV4:
        s = CONTAINING_RECORD(iface, DDSURF, iface4);
        break;
    case DDV7:
        s = CONTAINING_RECORD(iface, DDSURF, iface7);
        break;
    case DDVGAMMA:
        s = CONTAINING_RECORD(iface, DDSURF, gamma_iface);
        break;
    case DDVCOLOR:
        s = CONTAINING_RECORD(iface, DDSURF, color_iface);
        break;
    default:
        return nullptr;
    }
    return s->magic == SURF_MAGIC ? s : nullptr;
}

void *DdSurfIface(DDSURF *s, DWORD version)
{
    switch (version) {
    case DDV1:
        return &s->iface1;
    case DDV2:
        return &s->iface2;
    case DDV3:
        return &s->iface3;
    case DDV4:
        return &s->iface4;
    case DDVGAMMA:
        return &s->gamma_iface;
    case DDVCOLOR:
        return &s->color_iface;
    default:
        return &s->iface7;
    }
}

LONG *DdSurfRefOf(DDSURF *s, DWORD version)
{
    switch (version) {
    case DDV1:
        return &s->ref1;
    case DDV2:
        return &s->ref2;
    case DDV3:
        return &s->ref3;
    case DDV4:
        return &s->ref4;
    case DDVGAMMA:
        return &s->ref_gamma;
    case DDVCOLOR:
        return &s->ref_color;
    default:
        return &s->ref7;
    }
}

void DdSurfInitIfaces(DDSURF *s)
{
    s->iface1.lpVtbl = &surf1_vtbl;
    s->iface2.lpVtbl = &surf2_vtbl;
    s->iface3.lpVtbl = &surf3_vtbl;
    s->iface4.lpVtbl = &surf4_vtbl;
    s->iface7.lpVtbl = &surf7_vtbl;
    s->gamma_iface.lpVtbl = &gamma_vtbl;
    s->color_iface.lpVtbl = &color_vtbl;
    s->magic = SURF_MAGIC;
    InitializeListHead(&s->attached);
    InitializeListHead(&s->private_data);
    InitializeListHead(&s->links);
}

/* ------------------------------------------------------------------ the descs of the versions */

/* a version 1 desc is the version 2 one without caps 2 to 4 and the texture stage. */
void DdSurfDescFromV1(const DDSURFACEDESC *in, DDSURFACEDESC2 *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out, in, offsetof(DDSURFACEDESC, ddsCaps));
    out->ddsCaps.dwCaps = in->ddsCaps.dwCaps;
    out->dwSize = sizeof(DDSURFACEDESC2);
}

void DdSurfDescToV1(const DDSURFACEDESC2 *in, DDSURFACEDESC *out)
{
    memcpy(out, in, offsetof(DDSURFACEDESC, ddsCaps));
    out->ddsCaps.dwCaps = in->ddsCaps.dwCaps;
    out->dwSize = sizeof(DDSURFACEDESC);
}

static DWORD mip_levels(DDSURF *s);

/*
 * windows' FillEitherDDSurfaceDesc: caps, keys, sizes and the format, a
 * z buffer's depth in its own field for the first versions. lpSurface is
 * left alone.
 */
static void fill_desc(DDSURF *s, BOOL v2, DDSURFACEDESC2 *d)
{
    d->dwFlags = DDSD_CAPS;
    d->ddsCaps.dwCaps = s->desc.ddsCaps.dwCaps;
    if (v2) {
        d->ddsCaps.dwCaps2 = s->desc.ddsCaps.dwCaps2;
        d->ddsCaps.dwCaps3 = s->desc.ddsCaps.dwCaps3;
        d->ddsCaps.dwCaps4 = s->desc.ddsCaps.dwCaps4;
        d->dwTextureStage = s->desc.dwTextureStage;
        if (s->desc.dwFlags & DDSD_TEXTURESTAGE)
            d->dwFlags |= DDSD_TEXTURESTAGE;
    }
    if (s->ckeys & DDSD_CKDESTBLT) {
        d->dwFlags |= DDSD_CKDESTBLT;
        d->ddckCKDestBlt = s->desc.ddckCKDestBlt;
    }
    if (s->ckeys & DDSD_CKSRCBLT) {
        d->dwFlags |= DDSD_CKSRCBLT;
        d->ddckCKSrcBlt = s->desc.ddckCKSrcBlt;
    }
    d->ddckCKDestOverlay = (DDCOLORKEY){};
    d->ddckCKSrcOverlay = (DDCOLORKEY){};
    if ((s->desc.ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP)) == (DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP)) {
        d->dwFlags |= DDSD_BACKBUFFERCOUNT;
        d->dwBackBufferCount = s->desc.dwBackBufferCount;
    }
    if (s->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) {
        d->dwFlags |= DDSD_MIPMAPCOUNT;
        d->dwMipMapCount = mip_levels(s);
    }
    d->dwWidth = s->desc.dwWidth;
    d->dwHeight = s->desc.dwHeight;
    if (s->desc.ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) {
        d->dwFlags |= DDSD_WIDTH | DDSD_PITCH;
        d->dwHeight = 0;
        d->lPitch = s->desc.lPitch;
        memset(&d->ddpfPixelFormat, 0, sizeof(d->ddpfPixelFormat));
        d->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        return;
    }
    d->dwFlags |= DDSD_HEIGHT | DDSD_WIDTH | (s->format.block ? DDSD_LINEARSIZE : DDSD_PITCH);
    d->lPitch = s->desc.lPitch;
    d->ddpfPixelFormat = s->format.pf;
    if ((s->desc.ddsCaps.dwCaps & DDSCAPS_ZBUFFER) && !v2) {
        d->dwFlags |= DDSD_ZBUFFERBITDEPTH;
        d->dwZBufferBitDepth = s->format.pf.dwZBufferBitDepth;
    } else {
        d->dwFlags |= DDSD_PIXELFORMAT;
    }
}

/* a desc of the size of the version: 120 bytes for the first three interfaces and 136 after. */
void DdSurfFillDesc(DDSURF *s, DWORD version, DDSURFACEDESC2 *out)
{
    memset(out, 0, sizeof(*out));
    fill_desc(s, TRUE, out);
    if (version < DDV4) {
        DDSURFACEDESC old;
        memset(&old, 0, sizeof(old));
        DDSURFACEDESC2 v1;
        memset(&v1, 0, sizeof(v1));
        fill_desc(s, FALSE, &v1);
        DdSurfDescToV1(&v1, &old);
        memset(out, 0, sizeof(*out));
        memcpy(out, &old, sizeof(old));
        return;
    }
    out->dwSize = sizeof(DDSURFACEDESC2);
}

/* windows' tryMatch: only the fields the desc flags, caps exactly. */
BOOL DdSurfMatches(DDSURF *s, const DDSURFACEDESC2 *d)
{
    if (!d)
        return TRUE;
    if ((d->dwFlags & DDSD_CAPS) &&
        (d->ddsCaps.dwCaps != s->desc.ddsCaps.dwCaps || d->ddsCaps.dwCaps2 != s->desc.ddsCaps.dwCaps2 ||
         d->ddsCaps.dwCaps3 != s->desc.ddsCaps.dwCaps3 || d->ddsCaps.dwCaps4 != s->desc.ddsCaps.dwCaps4))
        return FALSE;
    if ((d->dwFlags & DDSD_HEIGHT) && d->dwHeight != s->desc.dwHeight)
        return FALSE;
    if ((d->dwFlags & DDSD_WIDTH) && d->dwWidth != s->desc.dwWidth)
        return FALSE;
    if ((d->dwFlags & DDSD_CKDESTBLT) &&
        (!(s->ckeys & DDSD_CKDESTBLT) || memcmp(&d->ddckCKDestBlt, &s->desc.ddckCKDestBlt, sizeof(DDCOLORKEY))))
        return FALSE;
    if ((d->dwFlags & DDSD_CKSRCBLT) &&
        (!(s->ckeys & DDSD_CKSRCBLT) || memcmp(&d->ddckCKSrcBlt, &s->desc.ddckCKSrcBlt, sizeof(DDCOLORKEY))))
        return FALSE;
    if ((d->dwFlags & DDSD_BACKBUFFERCOUNT) && d->dwBackBufferCount != s->desc.dwBackBufferCount)
        return FALSE;
    if ((d->dwFlags & DDSD_MIPMAPCOUNT) && d->dwMipMapCount != mip_levels(s))
        return FALSE;
    if ((d->dwFlags & DDSD_PIXELFORMAT) &&
        (!s->explicit_format || memcmp(&d->ddpfPixelFormat, &s->format.pf, sizeof(DDPIXELFORMAT))))
        return FALSE;
    if (d->dwFlags & (DDSD_CKDESTOVERLAY | DDSD_CKSRCOVERLAY))
        return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ attachments */

static DDATTACH *find_attachment(DDSURF *s, DDSURF *a)
{
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DDATTACH *at = CONTAINING_RECORD(e, DDATTACH, links);
        if (at->surface == a)
            return at;
    }
    return nullptr;
}

/* prepends, as windows' lists go; an explicit attachment references the interface given. */
static BOOL attach(DDSURF *s, DDSURF *a, DWORD version, BOOL implicit)
{
    DDATTACH *at = HeapAlloc(GetProcessHeap(), 0, sizeof(*at));
    if (!at)
        return FALSE;
    at->surface = a;
    at->version = version;
    at->implicit = implicit;
    InsertHeadList(&s->attached, &at->links);
    if (!implicit)
        DdSurfAddRef(a, DdSurfRefOf(a, version));
    return TRUE;
}

/* removes the attachment; the reference it held, if any, goes too. */
static void detach(DDSURF *s, DDATTACH *at)
{
    RemoveEntryList(&at->links);
    DDSURF *a = at->surface;
    BOOL implicit = at->implicit;
    DWORD version = at->version;
    HeapFree(GetProcessHeap(), 0, at);
    if (!implicit)
        DdSurfRelease(a, DdSurfRefOf(a, version));
}

/* the next surface of a flip chain: the attachment that flips with it. */
static DDSURF *next_flip(DDSURF *s)
{
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
        if ((a->desc.ddsCaps.dwCaps & DDSCAPS_FLIP) &&
            !((a->desc.ddsCaps.dwCaps ^ s->desc.ddsCaps.dwCaps) & (DDSCAPS_ZBUFFER | DDSCAPS_TEXTURE)) &&
            !(a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP && s->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP &&
              a->desc.dwWidth != s->desc.dwWidth))
            return a;
    }
    return nullptr;
}

/* the levels of a mipmap from this one down. */
static DWORD mip_levels(DDSURF *s)
{
    DWORD n = 1;
    for (DDSURF *level = s;;) {
        DDSURF *next = nullptr;
        for (LIST_ENTRY *e = level->attached.Flink; e != &level->attached; e = e->Flink) {
            DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
            if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth <= level->desc.dwWidth &&
                a->desc.dwHeight <= level->desc.dwHeight && a != level && !(a->desc.ddsCaps.dwCaps & DDSCAPS_FLIP)) {
                next = a;
                break;
            }
        }
        if (!next || n > 32)
            return n;
        n++;
        level = next;
    }
}

/* ------------------------------------------------------------------ lifetime */

static void free_private(DDSURF *s)
{
    while (!IsListEmpty(&s->private_data)) {
        DDPRIVATE *p = CONTAINING_RECORD(s->private_data.Flink, DDPRIVATE, links);
        RemoveEntryList(&p->links);
        if (p->flags & DDSPD_IUNKNOWNPOINTER) {
            IUnknown *unknown = *(IUnknown **)p->data;
            unknown->lpVtbl->Release(unknown);
        }
        HeapFree(GetProcessHeap(), 0, p);
    }
}

static void release_dc(DDSURF *s)
{
    if (!s->dc)
        return;
    D3DKMT_DESTROYDCFROMMEMORY d = {s->dc, s->dc_bitmap};
    D3DKMTDestroyDCFromMemory(&d);
    s->dc = nullptr;
    s->dc_bitmap = nullptr;
}

static void free_memory(DDSURF *s)
{
    if (s->mem && !--s->mem->ref) {
        HeapFree(GetProcessHeap(), 0, s->mem->base);
        HeapFree(GetProcessHeap(), 0, s->mem);
    }
    s->mem = nullptr;
    s->memory = nullptr;
}

static void set_palette(DDSURF *s, DDPAL *p);

/*
 * the last reference of a surface went: its implicit surfaces go with
 * it, released as many times as they are referenced, then what it holds.
 */
static void destroy(DDSURF *s)
{
    s->freeing = TRUE;
    DDRAW *dd = s->dd;
    if (dd && dd->primary == s) {
        DdSurfFlipToGdi(s);
        dd->primary = nullptr;
        DdPresentStop(dd);
    }
    while (!IsListEmpty(&s->attached)) {
        DDATTACH *at = CONTAINING_RECORD(s->attached.Flink, DDATTACH, links);
        DDSURF *a = at->surface;
        BOOL implicit = at->implicit;
        detach(s, at);
        if (implicit && a->implicit && !a->freeing) {
            a->implicit = FALSE;
            /* the whole chain dies with its root: every reference the child still has goes. */
            for (DWORD v = DDV1; v <= DDV7; v++)
                while (*DdSurfRefOf(a, v) > 0)
                    DdSurfRelease(a, DdSurfRefOf(a, v));
            while (a->ref_gamma > 0)
                DdSurfRelease(a, &a->ref_gamma);
            if (a->ref > 0)
                DdSurfRelease(a, &a->ref1);
        }
    }
    if (s->palette)
        set_palette(s, nullptr);
    if (s->clipper) {
        DDCLIP *c = s->clipper;
        s->clipper = nullptr;
        c->iface.lpVtbl->Release(&c->iface);
    }
    /* attachments to it from elsewhere go: they referenced it, so only implicit ones can be left. */
    if (dd) {
        for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink) {
            DDSURF *other = CONTAINING_RECORD(e, DDSURF, links);
            DDATTACH *at = find_attachment(other, s);
            if (at) {
                RemoveEntryList(&at->links);
                HeapFree(GetProcessHeap(), 0, at);
            }
        }
    }
    release_dc(s);
    free_private(s);
    D3dSurfFree(s);
    DdHideGamma(s);
    HeapFree(GetProcessHeap(), 0, s->gamma);
    free_memory(s);
    RemoveEntryList(&s->links);
    void *creator = s->creator;
    s->magic = 0;
    HeapFree(GetProcessHeap(), 0, s);
    if (creator) {
        RtlLeaveCriticalSection(&DdLock);
        ((IUnknown *)creator)->lpVtbl->Release(creator);
        RtlEnterCriticalSection(&DdLock);
    }
}

ULONG DdSurfAddRef(DDSURF *s, LONG *iface_ref)
{
    DdEnter();
    if (s->freeing) {
        DdLeave(DD_OK);
        return 0;
    }
    s->ref++;
    ULONG ref = (ULONG)++*iface_ref;
    DdLeave(DD_OK);
    return ref;
}

/* an implicit surface's last reference belongs to its root: releasing it does nothing. */
ULONG DdSurfRelease(DDSURF *s, LONG *iface_ref)
{
    DdEnter();
    if (s->freeing && s->magic != SURF_MAGIC) {
        DdLeave(DD_OK);
        return 0;
    }
    if (*iface_ref <= 0 || (s->ref == 1 && s->implicit)) {
        DdLeave(DD_OK);
        return 0;
    }
    ULONG ref = (ULONG)--*iface_ref;
    if (!--s->ref && !s->freeing)
        destroy(s);
    DdLeave(DD_OK);
    return ref;
}

/* the directdraw object went: its surfaces are released until gone, as windows does. */
void DdSurfKill(DDSURF *s)
{
    s->implicit = FALSE;
    s->creator = nullptr;
    for (DWORD v = DDV1; v <= DDV7; v++)
        while (s->magic == SURF_MAGIC && *DdSurfRefOf(s, v) > 0)
            DdSurfRelease(s, DdSurfRefOf(s, v));
    while (s->magic == SURF_MAGIC && s->ref_gamma > 0)
        DdSurfRelease(s, &s->ref_gamma);
    if (s->magic == SURF_MAGIC) {
        RemoveEntryList(&s->links);
        InitializeListHead(&s->links);
        s->dd = nullptr;
    }
}

/* a mode change or a lost focus: surfaces in video memory lose their contents. */
void DdLoseSurfaces(DDRAW *dd)
{
    for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink) {
        DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
        if (!(s->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
            s->lost = TRUE;
    }
}

BOOL DdSurfBusy(const DDSURF *s)
{
    return s->usage != 0;
}

static void bump(DDSURF *s)
{
    if (s->uniqueness && !++s->uniqueness)
        s->uniqueness = 1;
    s->generation++;
}

/* pixels changed: a new uniqueness value, and the screen shows the primary's change. */
void DdSurfWritten(DDSURF *s, const RECT *rect)
{
    bump(s);
    if (s->dd && s->dd->primary && (s == s->dd->primary || s->gdi) && !IsRectEmpty(rect))
        DdPresentDirty(s->dd->primary, rect);
}

/* ------------------------------------------------------------------ IUnknown */

static BOOL color_control_allowed(DDSURF *s)
{
    return FALSE;
}

HRESULT DdSurfQuery(DDSURF *s, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!riid)
        return DDERR_INVALIDPARAMS;
    DWORD version;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectDrawSurface))
        version = DDV1;
    else if (IsEqualGUID(riid, &IID_IDirectDrawSurface2))
        version = DDV2;
    else if (IsEqualGUID(riid, &IID_IDirectDrawSurface3))
        version = DDV3;
    else if (IsEqualGUID(riid, &IID_IDirectDrawSurface4))
        version = DDV4;
    else if (IsEqualGUID(riid, &IID_IDirectDrawSurface7))
        version = DDV7;
    else if (IsEqualGUID(riid, &IID_IDirectDrawGammaControl))
        version = DDVGAMMA;
    else if (IsEqualGUID(riid, &IID_IDirectDrawColorControl) && color_control_allowed(s))
        version = DDVCOLOR;
    else
        return DdSurfQueryD3D(s, riid, out);
    DdSurfAddRef(s, DdSurfRefOf(s, version));
    *out = DdSurfIface(s, version);
    return S_OK;
}

/* ------------------------------------------------------------------ method helpers */

/* the surface of an interface with the lock held, and the version it was called through. */
static DDSURF *enter(void *iface, DWORD *version, HRESULT *hr)
{
    DDSURF *s = DdSurfFromIface(iface);
    if (!s) {
        *hr = DDERR_INVALIDOBJECT;
        return nullptr;
    }
    if (version)
        *version = DdSurfVersionOf(iface);
    DdEnter();
    *hr = DD_OK;
    return s;
}

static BOOL optimized(const DDSURF *s)
{
    return (s->desc.ddsCaps.dwCaps & DDSCAPS_OPTIMIZED) != 0;
}

static BOOL execute_buffer(const DDSURF *s)
{
    return (s->desc.ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) != 0;
}

static HRESULT STDMETHODCALLTYPE surf_query(void *This, REFIID riid, void **out)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    return DdLeave(DdSurfQuery(s, riid, out));
}

static ULONG STDMETHODCALLTYPE surf_add_ref(void *This)
{
    DDSURF *s = DdSurfFromIface(This);
    return s ? DdSurfAddRef(s, DdSurfRefOf(s, DdSurfVersionOf(This))) : 0;
}

static ULONG STDMETHODCALLTYPE surf_release(void *This)
{
    DDSURF *s = DdSurfFromIface(This);
    return s ? DdSurfRelease(s, DdSurfRefOf(s, DdSurfVersionOf(This))) : 0;
}

/* ------------------------------------------------------------------ attachment methods */

/* windows' rule of what can be attached to what, and the flip pairs attaching makes. */
static HRESULT add_attached(DDSURF *s, DWORD version, DDSURF *a, DWORD a_version)
{
    DWORD sc = s->desc.ddsCaps.dwCaps, ac = a->desc.ddsCaps.dwCaps;
    if ((version == DDV7 && !((sc ^ ac) & DDSCAPS_ZBUFFER)) || ((sc | ac) & DDSCAPS_EXECUTEBUFFER) ||
        ((s->desc.ddsCaps.dwCaps2 & DDSCAPS2_CUBEMAP) && !(ac & DDSCAPS_ZBUFFER)) || ((sc | ac) & DDSCAPS_OPTIMIZED))
        return DDERR_CANNOTATTACHSURFACE;
    if ((sc & DDSCAPS_PRIMARYSURFACE) && (!s->dd || !DdIsExclusiveOwner(s->dd) || !(s->dd->flags & DDF_FULLSCREEN)))
        return DDERR_NOEXCLUSIVEMODE;
    if (s == a)
        return DDERR_CANNOTATTACHSURFACE;
    if (s->lost || a->lost)
        return DDERR_SURFACELOST;
    DDATTACH *existing = find_attachment(s, a);
    if (existing && existing->version == a_version)
        return DDERR_SURFACEALREADYATTACHED;
    if (s->dd && a->dd && s->dd != a->dd)
        return DDERR_DEVICEDOESNTOWNSURFACE;
    if ((sc & (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE)) == (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE) &&
        (ac & (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE)) == (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE)) {
        if (a->desc.dwWidth > s->desc.dwWidth || a->desc.dwHeight > s->desc.dwHeight)
            return DDERR_CANNOTATTACHSURFACE;
    } else if (!((sc ^ ac) & DDSCAPS_TEXTURE)) {
        if (a->desc.dwWidth != s->desc.dwWidth || a->desc.dwHeight != s->desc.dwHeight)
            return DDERR_CANNOTATTACHSURFACE;
    }
    DWORD kind = ac & (DDSCAPS_MIPMAP | DDSCAPS_ZBUFFER | DDSCAPS_TEXTURE | DDSCAPS_ALPHA);
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DWORD other = CONTAINING_RECORD(e, DDATTACH, links)->surface->desc.ddsCaps.dwCaps;
        DWORD common = kind & other;
        if (common && (!((ac ^ other) & DDSCAPS_MIPMAP) || !(common & DDSCAPS_TEXTURE)))
            return DDERR_CANNOTATTACHSURFACE;
    }
    BOOL flip_compatible = !((sc ^ ac) & 0x21082) &&
                           !((sc & ac & (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE)) == (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE));
    if ((ac & DDSCAPS_FLIP) && flip_compatible)
        return DDERR_CANNOTATTACHSURFACE;
    if (((sc ^ ac) & DDSCAPS_SYSTEMMEMORY) && (ac & 0x20800) != 0x20800)
        return DDERR_CANNOTATTACHSURFACE;
    if (!flip_compatible) {
        if (!attach(s, a, a_version, FALSE))
            return DDERR_OUTOFMEMORY;
        return DD_OK;
    }
    if (sc & DDSCAPS_FLIP) {
        /* into the chain right after this surface. */
        DDSURF *next = next_flip(s);
        DDATTACH *link = next ? find_attachment(s, next) : nullptr;
        if (!link)
            return DDERR_CANNOTATTACHSURFACE;
        BOOL implicit = link->implicit;
        DWORD link_version = link->version;
        RemoveEntryList(&link->links);
        HeapFree(GetProcessHeap(), 0, link);
        if (!attach(s, a, a_version, FALSE) || !attach(a, next, link_version, implicit))
            return DDERR_CANNOTATTACHSURFACE;
        if (!implicit)
            DdSurfRelease(next, DdSurfRefOf(next, link_version));
        a->desc.ddsCaps.dwCaps = (ac & ~(DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER)) | DDSCAPS_FLIP;
        DDSURF *front = s;
        while (!(front->desc.ddsCaps.dwCaps & DDSCAPS_FRONTBUFFER) && (front = next_flip(front)) && front != s)
            ;
        if (sc & DDSCAPS_FRONTBUFFER) {
            a->desc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
            next->desc.ddsCaps.dwCaps &= ~DDSCAPS_BACKBUFFER;
        }
        if (front && (front->desc.ddsCaps.dwCaps & DDSCAPS_FRONTBUFFER))
            front->desc.dwBackBufferCount++;
        return DD_OK;
    }
    /* a new pair: the surface and the attached one flip between each other. */
    if (!attach(s, a, a_version, FALSE) || !attach(a, s, version, TRUE))
        return DDERR_OUTOFMEMORY;
    if (sc & DDSCAPS_BACKBUFFER) {
        a->desc.ddsCaps.dwCaps |= DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP;
        a->desc.dwBackBufferCount = 1;
        s->desc.ddsCaps.dwCaps |= DDSCAPS_FLIP;
    } else {
        a->desc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER | DDSCAPS_FLIP;
        s->desc.ddsCaps.dwCaps |= DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP;
        s->desc.dwBackBufferCount = 1;
    }
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE surf_add_attached_surface(void *This, void *attached)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    DDSURF *a = DdSurfFromIface(attached);
    if (!a)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(add_attached(s, version, a, DdSurfVersionOf(attached)));
}

/* the front of a chain a surface is in, null for none. */
static DDSURF *chain_front(DDSURF *s)
{
    DDSURF *f = s;
    for (int i = 0; i < 64 && f; i++) {
        if (f->desc.ddsCaps.dwCaps & DDSCAPS_FRONTBUFFER)
            return f;
        f = next_flip(f);
        if (f == s)
            break;
    }
    return nullptr;
}

static HRESULT delete_one(DDSURF *s, DDATTACH *at)
{
    if (at->implicit)
        return DDERR_CANNOTDETACHSURFACE;
    DDSURF *a = at->surface;
    DWORD version = at->version;
    BOOL in_chain = (s->desc.ddsCaps.dwCaps & DDSCAPS_FLIP) && (a->desc.ddsCaps.dwCaps & DDSCAPS_FLIP) &&
                    next_flip(s) == a;
    if (in_chain) {
        DDSURF *front = chain_front(s);
        DDSURF *after = next_flip(a);
        DDATTACH *link = after ? find_attachment(a, after) : nullptr;
        RemoveEntryList(&at->links);
        HeapFree(GetProcessHeap(), 0, at);
        if (link && after != s) {
            /* the chain closes around it. */
            attach(s, after, link->version, link->implicit);
            if (!link->implicit)
                DdSurfRelease(after, DdSurfRefOf(after, link->version));
            RemoveEntryList(&link->links);
            HeapFree(GetProcessHeap(), 0, link);
        } else if (link) {
            RemoveEntryList(&link->links);
            HeapFree(GetProcessHeap(), 0, link);
        }
        if (a->desc.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER) {
            DDSURF *new_back = next_flip(s);
            if (new_back && new_back != s)
                new_back->desc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
        }
        if (front && front->desc.dwBackBufferCount)
            front->desc.dwBackBufferCount--;
        if (front && !front->desc.dwBackBufferCount) {
            front->desc.ddsCaps.dwCaps &= ~(DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER);
            if (front->created_front)
                front->desc.ddsCaps.dwCaps |= DDSCAPS_FRONTBUFFER;
            /* the pair's back link from the other side goes as well. */
            DDSURF *other = next_flip(front);
            if (other) {
                DDATTACH *back = find_attachment(front, other);
                if (back && back->implicit) {
                    RemoveEntryList(&back->links);
                    HeapFree(GetProcessHeap(), 0, back);
                }
            }
        }
        a->desc.ddsCaps.dwCaps &= ~(DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER);
        if (a->created_back)
            a->desc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
        if (a->created_front)
            a->desc.ddsCaps.dwCaps |= DDSCAPS_FRONTBUFFER;
        DdSurfRelease(a, DdSurfRefOf(a, version));
        return DD_OK;
    }
    detach(s, at);
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE surf_delete_attached_surface(void *This, DWORD flags, void *attached)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (flags)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDSURF *a = attached ? DdSurfFromIface(attached) : nullptr;
    if (attached && !a)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost || (a && a->lost))
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    if (a) {
        DDATTACH *at = find_attachment(s, a);
        if (!at)
            return DdLeave(DDERR_SURFACENOTATTACHED);
        return DdLeave(delete_one(s, at));
    }
    while (!IsListEmpty(&s->attached)) {
        hr = delete_one(s, CONTAINING_RECORD(s->attached.Flink, DDATTACH, links));
        if (FAILED(hr))
            break;
    }
    return DdLeave(hr);
}

/* the version of the interfaces a version's callbacks and attached surfaces get. */
static DWORD out_version(DWORD version)
{
    return version < DDV4 ? DDV1 : version;
}

static HRESULT enum_list(DDSURF **list, DWORD n, DWORD version, void *context, void *callback)
{
    DWORD ov = out_version(version);
    HRESULT result = DD_OK;
    DWORD i = 0;
    for (; i < n; i++) {
        DDSURFACEDESC2 desc;
        memset(&desc, 0, sizeof(desc));
        fill_desc(list[i], TRUE, &desc);
        desc.dwSize = version < DDV4 ? sizeof(DDSURFACEDESC) : sizeof(DDSURFACEDESC2);
        void *iface = DdSurfIface(list[i], ov);
        RtlLeaveCriticalSection(&DdLock);
        HRESULT r = version < DDV4 ? ((LPDDENUMSURFACESCALLBACK)callback)(iface, (DDSURFACEDESC *)&desc, context)
                                   : ((LPDDENUMSURFACESCALLBACK7)callback)(iface, &desc, context);
        RtlEnterCriticalSection(&DdLock);
        if (r == DDENUMRET_CANCEL)
            break;
    }
    for (i++; i < n; i++)
        DdSurfRelease(list[i], DdSurfRefOf(list[i], ov));
    return result;
}

static HRESULT STDMETHODCALLTYPE surf_enum_attached_surfaces(void *This, LPVOID context, void *callback)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (!callback)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    DDSURF *list[64];
    DWORD n = 0;
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached && n < RTL_NUMBER_OF(list); e = e->Flink) {
        list[n] = CONTAINING_RECORD(e, DDATTACH, links)->surface;
        DdSurfAddRef(list[n], DdSurfRefOf(list[n], out_version(version)));
        n++;
    }
    return DdLeave(enum_list(list, n, version, context, callback));
}

static HRESULT STDMETHODCALLTYPE surf_enum_overlay_z_orders(void *This, DWORD flags, LPVOID context, void *callback)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!callback)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    if (flags > DDENUMOVERLAYZ_FRONTTOBACK)
        return DdLeave(DDERR_INVALIDPARAMS);
    /* there are no overlays. */
    return DdLeave(DD_OK);
}

/* windows' GetAttachedSurface: the newest attachment with every bit asked; the first versions get version 1. */
static HRESULT get_attached(DDSURF *s, DWORD version, DDSCAPS2 caps, void **out)
{
    if (s->lost)
        return DDERR_SURFACELOST;
    if (version >= DDV4 && (caps.dwCaps & 0x03000000))
        return DDERR_INVALIDCAPS;
    *out = nullptr;
    if (version != DDV7)
        caps.dwCaps2 = caps.dwCaps3 = caps.dwCaps4 = 0;
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
        const DDSCAPS2 *c = &a->desc.ddsCaps;
        BOOL any = (caps.dwCaps & c->dwCaps) || (caps.dwCaps2 & c->dwCaps2) || (caps.dwCaps3 & c->dwCaps3) ||
                   (caps.dwCaps4 & c->dwCaps4);
        BOOL all = (caps.dwCaps & c->dwCaps) == caps.dwCaps && (caps.dwCaps2 & c->dwCaps2) == caps.dwCaps2 &&
                   (caps.dwCaps3 & c->dwCaps3) == caps.dwCaps3 && (caps.dwCaps4 & c->dwCaps4) == caps.dwCaps4;
        if (any && all) {
            DWORD ov = out_version(version);
            DdSurfAddRef(a, DdSurfRefOf(a, ov));
            *out = DdSurfIface(a, ov);
            return DD_OK;
        }
    }
    return DDERR_NOTFOUND;
}

static HRESULT STDMETHODCALLTYPE surf_get_attached_surface(void *This, void *caps_in, void **out)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (!caps_in || !out)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDSCAPS2 caps = {};
    if (version < DDV4)
        caps.dwCaps = ((DDSCAPS *)caps_in)->dwCaps;
    else
        caps = *(DDSCAPS2 *)caps_in;
    return DdLeave(get_attached(s, version, caps, out));
}

/* ------------------------------------------------------------------ flip */

/* what flips: the memory and with it the pixels gdi shows; everything else stays with its surface. */
static void swap_memory(DDSURF *a, DDSURF *b)
{
    BYTE *memory = a->memory;
    DDMEM *mem = a->mem;
    BOOL gdi = a->gdi;
    struct D3DSURFX *d3d = a->d3d;
    DWORD generation = a->generation;
    a->memory = b->memory;
    a->mem = b->mem;
    a->gdi = b->gdi;
    a->d3d = b->d3d;
    a->generation = b->generation;
    b->memory = memory;
    b->mem = mem;
    b->gdi = gdi;
    b->d3d = d3d;
    b->generation = generation;
}

/* a mipmap flips level by level, each to the same place in its chain. */
static HRESULT flip(DDSURF *s, DWORD version, DDSURF *override, DWORD flags)
{
    if (flags & 0xF8FFFFC0)
        return DDERR_INVALIDPARAMS;
    if (version == DDV7) {
        if (flags & DDFLIP_DONOTWAIT)
            flags &= ~DDFLIP_WAIT;
        else
            flags |= DDFLIP_WAIT;
    }
    if (flags & (DDFLIP_EVEN | DDFLIP_ODD))
        return DDERR_INVALIDPARAMS;
    if ((flags & 0x07000000) && (flags & DDFLIP_NOVSYNC))
        return DDERR_INVALIDPARAMS;
    if (s->lost || (override && override->lost))
        return DDERR_SURFACELOST;
    if (flags & DDFLIP_STEREO)
        return DDERR_INVALIDPARAMS;
    if ((s->desc.ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP)) != (DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP))
        return DDERR_NOTFLIPPABLE;
    if (DdSurfBusy(s))
        return DDERR_SURFACEBUSY;
    BOOL primary = (s->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    if (primary && (!s->dd || !DdIsExclusiveOwner(s->dd)))
        return DDERR_NOEXCLUSIVEMODE;
    DDSURF *chain[64];
    DWORD n = 0;
    for (DDSURF *b = next_flip(s); b && b != s && n < RTL_NUMBER_OF(chain); b = next_flip(b))
        chain[n++] = b;
    if (!n)
        return DDERR_NOTFLIPPABLE;
    BOOL found = FALSE;
    for (DWORD i = 0; i < n; i++) {
        if (chain[i]->lost)
            return DDERR_SURFACELOST;
        if (DdSurfBusy(chain[i]) && (!override || chain[i] == override))
            return DDERR_SURFACEBUSY;
        if (chain[i] == override)
            found = TRUE;
    }
    if (override && !found)
        return DDERR_NOTFLIPPABLE;
    DDSURF *target = override ? override : chain[0];
    if ((target->desc.ddsCaps.dwCaps ^ s->desc.ddsCaps.dwCaps) & DDSCAPS_SYSTEMMEMORY)
        return DDERR_INVALIDPARAMS;
    if ((s->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && !override) {
        /* every level of a flipping mipmap turns together. */
        DDSURF *level = s;
        while (level) {
            DDSURF *levels[64];
            DWORD m = 0;
            for (DDSURF *b = next_flip(level); b && b != level && m < RTL_NUMBER_OF(levels); b = next_flip(b))
                levels[m++] = b;
            for (DWORD i = m; i-- > 0;)
                swap_memory(level, levels[i]);
            DDSURF *next = nullptr;
            for (LIST_ENTRY *e = level->attached.Flink; e != &level->attached; e = e->Flink) {
                DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
                if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth < level->desc.dwWidth) {
                    next = a;
                    break;
                }
            }
            level = next;
        }
        return DD_OK;
    }
    bump(s);
    bump(target);
    if (override) {
        swap_memory(s, target);
    } else {
        /* back 1 shows next: each back buffer takes the next one's memory, the last the front's. */
        for (DWORD i = n; i-- > 0;)
            swap_memory(s, chain[i]);
    }
    if (primary && s->dd) {
        /* the flip waits for the display's refresh, as a driver's does unless told not to. */
        if (!(flags & DDFLIP_NOVSYNC))
            DdWaitForVerticalBlank(DDWAITVB_BLOCKBEGIN);
        DdPresentFlip(s);
    }
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE surf_flip(void *This, void *override_iface, DWORD flags)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    DDSURF *override = override_iface ? DdSurfFromIface(override_iface) : nullptr;
    if (override_iface && !override)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(flip(s, version, override, flags));
}

/* the front buffer gets back the memory gdi shows. */
HRESULT DdSurfFlipToGdi(DDSURF *primary)
{
    if (primary->gdi || !(primary->desc.ddsCaps.dwCaps & DDSCAPS_FLIP))
        return DD_OK;
    for (DDSURF *b = next_flip(primary); b && b != primary; b = next_flip(b))
        if (b->gdi) {
            swap_memory(primary, b);
            DdPresentFlip(primary);
            return DD_OK;
        }
    return DDERR_NOTFOUND;
}

DDSURF *DdSurfGdiSurface(DDSURF *primary)
{
    if (primary->gdi)
        return primary;
    DDSURF *b = next_flip(primary);
    return b && b != primary && b->gdi ? b : nullptr;
}

static HRESULT STDMETHODCALLTYPE surf_get_flip_status(void *This, DWORD flags)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (flags != DDGFS_CANFLIP && flags != DDGFS_ISFLIPDONE)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (execute_buffer(s))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_blt_status(void *This, DWORD flags)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (flags != DDGBS_CANBLT && flags != DDGBS_ISBLTDONE)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    return DdLeave(DD_OK);
}

/* ------------------------------------------------------------------ blts */

static HRESULT STDMETHODCALLTYPE surf_blt(void *This, LPRECT dst_rect, void *source, LPRECT src_rect, DWORD flags,
                                          LPDDBLTFX fx)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    DDSURF *src = source ? DdSurfFromIface(source) : nullptr;
    if (source && !src)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(DdBlt(s, dst_rect, src, src ? DdSurfVersionOf(source) : 0, src_rect, flags, fx, version));
}

static HRESULT STDMETHODCALLTYPE surf_blt_batch(void *This, LPDDBLTBATCH batch, DWORD count, DWORD flags)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!batch && count)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(DdBltBatch(s, batch, count));
}

static HRESULT STDMETHODCALLTYPE surf_blt_fast(void *This, DWORD x, DWORD y, void *source, LPRECT src_rect,
                                               DWORD trans)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    DDSURF *src = DdSurfFromIface(source);
    if (!src)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(DdBltFast(s, x, y, src, src_rect, trans, version));
}

/* ------------------------------------------------------------------ queries */

static HRESULT STDMETHODCALLTYPE surf_get_caps(void *This, void *caps)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (!caps)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (version < DDV4)
        ((DDSCAPS *)caps)->dwCaps = s->desc.ddsCaps.dwCaps;
    else
        *(DDSCAPS2 *)caps = s->desc.ddsCaps;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_clipper(void *This, LPDIRECTDRAWCLIPPER *out)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    if (!s->clipper)
        return DdLeave(DDERR_NOCLIPPERATTACHED);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    s->clipper->iface.lpVtbl->AddRef(&s->clipper->iface);
    *out = &s->clipper->iface;
    return DdLeave(DD_OK);
}

/* the key slot a flag picks, in windows' order; zero for an invalid combination. */
static DWORD key_slot(DWORD flags, DDSURF *s, BOOL get)
{
    if (flags & DDCKEY_DESTOVERLAY)
        return (flags & (DDCKEY_DESTBLT | DDCKEY_SRCBLT | DDCKEY_SRCOVERLAY)) ? 0 : DDCKEY_DESTOVERLAY;
    if (flags & DDCKEY_DESTBLT)
        return (flags & (DDCKEY_SRCBLT | DDCKEY_SRCOVERLAY)) ? 0 : DDCKEY_DESTBLT;
    if (flags & DDCKEY_SRCOVERLAY) {
        if (flags & DDCKEY_SRCBLT)
            return 0;
        if (get && !(s->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
            return 0;
        return DDCKEY_SRCOVERLAY;
    }
    return (flags & DDCKEY_SRCBLT) ? DDCKEY_SRCBLT : 0;
}

static DWORD key_flag(DWORD slot)
{
    switch (slot) {
    case DDCKEY_DESTOVERLAY:
        return DDSD_CKDESTOVERLAY;
    case DDCKEY_DESTBLT:
        return DDSD_CKDESTBLT;
    case DDCKEY_SRCOVERLAY:
        return DDSD_CKSRCOVERLAY;
    default:
        return DDSD_CKSRCBLT;
    }
}

static DDCOLORKEY *key_of(DDSURF *s, DWORD slot)
{
    switch (slot) {
    case DDCKEY_DESTOVERLAY:
        return &s->desc.ddckCKDestOverlay;
    case DDCKEY_DESTBLT:
        return &s->desc.ddckCKDestBlt;
    case DDCKEY_SRCOVERLAY:
        return &s->desc.ddckCKSrcOverlay;
    default:
        return &s->desc.ddckCKSrcBlt;
    }
}

static HRESULT STDMETHODCALLTYPE surf_get_color_key(void *This, DWORD flags, LPDDCOLORKEY key)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!key)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (s->desc.ddsCaps.dwCaps & (DDSCAPS_ZBUFFER | DDSCAPS_EXECUTEBUFFER))
        return DdLeave(DDERR_NOCOLORKEY);
    if (flags & ~0x1Fu)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    DWORD slot = key_slot(flags, s, TRUE);
    if (!slot)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!(s->ckeys & key_flag(slot)))
        return DdLeave(DDERR_NOCOLORKEY);
    *key = *key_of(s, slot);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_palette(void *This, LPDIRECTDRAWPALETTE *out)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    if (!s->palette)
        return DdLeave(DDERR_NOPALETTEATTACHED);
    s->palette->iface.lpVtbl->AddRef(&s->palette->iface);
    *out = &s->palette->iface;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_pixel_format(void *This, LPDDPIXELFORMAT format)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!format || format->dwSize != sizeof(DDPIXELFORMAT))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (execute_buffer(s))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    *format = s->format.pf;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_surface_desc(void *This, void *desc)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (!desc)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (version < DDV4) {
        if (((DDSURFACEDESC *)desc)->dwSize != sizeof(DDSURFACEDESC))
            return DdLeave(DDERR_INVALIDPARAMS);
        DDSURFACEDESC2 d;
        DdSurfFillDesc(s, DDV1, &d);
        memcpy(desc, &d, sizeof(DDSURFACEDESC));
    } else {
        if (((DDSURFACEDESC2 *)desc)->dwSize != sizeof(DDSURFACEDESC2))
            return DdLeave(DDERR_INVALIDPARAMS);
        DdSurfFillDesc(s, DDV7, desc);
    }
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_initialize(void *This, LPDIRECTDRAW ddraw, void *desc)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE surf_is_lost(void *This)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    return DdLeave(s->lost ? DDERR_SURFACELOST : DD_OK);
}

/* ------------------------------------------------------------------ lock */

static BOOL valid_rect(const RECT *r, const DDSURF *s)
{
    return r->left >= 0 && r->top >= 0 && r->left <= r->right && r->top <= r->bottom &&
           r->bottom <= (LONG)s->desc.dwHeight && r->right <= (LONG)s->desc.dwWidth;
}

/* the address of a rectangle's first pixel: whole pixels, two at a time for packed yuv. */
static BYTE *rect_pointer(DDSURF *s, const RECT *r)
{
    if (!r)
        return s->memory;
    LONG x = r->left;
    DWORD fourcc = s->format.pf.dwFourCC;
    if ((s->format.pf.dwFlags & DDPF_FOURCC) && !s->format.block)
        return s->memory + (SSIZE_T)r->top * s->desc.lPitch + 2 * (x & ~1);
    if (s->format.block)
        return s->memory + (SSIZE_T)(r->top / 4) * s->desc.lPitch + (x / 4) * s->format.block;
    (void)fourcc;
    return s->memory + (SSIZE_T)r->top * s->desc.lPitch + (SIZE_T)x * s->format.bpp / 8;
}

static HRESULT lock(DDSURF *s, DWORD version, const RECT *rect, void *desc, DWORD flags)
{
    if (s->lost)
        return DDERR_SURFACELOST;
    if (optimized(s))
        return DDERR_OPTIMIZED;
    if (flags & ~(DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK | DDLOCK_NOOVERWRITE |
                  DDLOCK_DISCARDCONTENTS | DDLOCK_DONOTWAIT | DDLOCK_NODIRTYUPDATE))
        return DDERR_INVALIDPARAMS;
    if (!desc)
        return DDERR_INVALIDPARAMS;
    DWORD size = ((DDSURFACEDESC *)desc)->dwSize;
    if (size != sizeof(DDSURFACEDESC) && size != sizeof(DDSURFACEDESC2))
        return DDERR_INVALIDPARAMS;
    ((DDSURFACEDESC *)desc)->lpSurface = nullptr;
    if (rect && (execute_buffer(s) || !valid_rect(rect, s)))
        return DDERR_INVALIDPARAMS;
    if (rect) {
        RECT overlap;
        for (DWORD i = 0; i < s->rect_locks; i++)
            if (IntersectRect(&overlap, rect, &s->locks[i].rect))
                return DDERR_SURFACEBUSY;
        if ((!s->rect_locks && s->usage) || s->rect_locks >= RTL_NUMBER_OF(s->locks))
            return DDERR_SURFACEBUSY;
    } else if (s->usage) {
        return DDERR_SURFACEBUSY;
    }
    if (!s->memory)
        return DDERR_SURFACELOST;
    DdSurfSync(s);
    BYTE *pointer = rect_pointer(s, rect);
    if (rect)
        s->locks[s->rect_locks++] = (DDLOCKED){*rect, pointer};
    s->usage++;
    s->readonly_lock = (flags & DDLOCK_READONLY) != 0;
    DDSURFACEDESC2 filled = {};
    memcpy(&filled, desc, size);
    fill_desc(s, size == sizeof(DDSURFACEDESC2), &filled);
    filled.lpSurface = pointer;
    if (size == sizeof(DDSURFACEDESC)) {
        DDSURFACEDESC *out = desc;
        DdSurfDescToV1(&filled, out);
        out->dwSize = sizeof(DDSURFACEDESC);
        out->lpSurface = pointer;
    } else {
        memcpy(desc, &filled, sizeof(DDSURFACEDESC2));
    }
    (void)version;
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE surf_lock(void *This, LPRECT rect, void *desc, DWORD flags, HANDLE event)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    return DdLeave(lock(s, version, rect, desc, flags));
}

/* by the pointer Lock gave for the first interfaces, by the rectangle after. */
static HRESULT unlock(DDSURF *s, DWORD version, void *arg)
{
    if (optimized(s))
        return DDERR_OPTIMIZED;
    if (!s->usage)
        return DDERR_NOTLOCKED;
    if (s->usage > 1 && !arg)
        return DDERR_INVALIDRECT;
    if (s->dc && !(s->desc.ddsCaps.dwCaps & DDSCAPS_OWNDC) && version >= DDV3)
        return DDERR_GENERIC;
    RECT written = {0, 0, (LONG)s->desc.dwWidth, (LONG)s->desc.dwHeight};
    if (s->rect_locks) {
        DWORD found = s->rect_locks;
        if (!arg) {
            if (s->rect_locks > 1)
                return DDERR_INVALIDRECT;
            found = 0;
        } else {
            for (DWORD i = 0; i < s->rect_locks; i++) {
                BOOL match = version < DDV4 ? s->locks[i].pointer == arg
                                            : EqualRect(&s->locks[i].rect, (const RECT *)arg);
                if (match) {
                    found = i;
                    break;
                }
            }
            if (found == s->rect_locks)
                return DDERR_NOTLOCKED;
        }
        written = s->locks[found].rect;
        s->locks[found] = s->locks[--s->rect_locks];
    }
    s->usage--;
    if (!s->readonly_lock)
        DdSurfWritten(s, &written);
    s->readonly_lock = FALSE;
    return s->lost ? DDERR_SURFACELOST : DD_OK;
}

static HRESULT STDMETHODCALLTYPE surf_unlock(void *This, void *arg)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    return DdLeave(unlock(s, version, arg));
}

/* ------------------------------------------------------------------ dcs */

/* a dc of an 8 bit surface takes its palette, else the primary's, else gray. */
static void dc_colors(DDSURF *s, PALETTEENTRY *table)
{
    DDPAL *p = s->palette ? s->palette : s->dd && s->dd->primary ? s->dd->primary->palette : nullptr;
    for (int i = 0; i < 256; i++)
        table[i] = p && !(p->caps & DDPCAPS_8BITENTRIES) && i < (int)p->count ? p->entries[i]
                                                                              : (PALETTEENTRY){(BYTE)i, (BYTE)i, (BYTE)i, 0};
}

static HRESULT STDMETHODCALLTYPE surf_get_dc(void *This, HDC *out)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (s->desc.ddsCaps.dwCaps & (DDSCAPS_OPTIMIZED | DDSCAPS_EXECUTEBUFFER))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    BOOL own = (s->desc.ddsCaps.dwCaps & DDSCAPS_OWNDC) != 0;
    if (s->dc && !own)
        return DdLeave(DDERR_DCALREADYCREATED);
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    if (s->dc) {
        *out = s->dc;
        return DdLeave(DD_OK);
    }
    if (s->format.gdi == D3DDDIFMT_UNKNOWN || !s->memory)
        return DdLeave(DDERR_CANTCREATEDC);
    DdSurfSync(s);
    PALETTEENTRY table[256];
    dc_colors(s, table);
    D3DKMT_CREATEDCFROMMEMORY d = {
        .pMemory = s->memory,
        .Format = s->format.gdi,
        .Width = s->desc.dwWidth,
        .Height = s->desc.dwHeight,
        .Pitch = (UINT)s->desc.lPitch,
        .pColorTable = table,
    };
    if (!NT_SUCCESS(D3DKMTCreateDCFromMemory(&d)))
        return DdLeave(DDERR_CANTCREATEDC);
    s->dc = d.hDc;
    s->dc_bitmap = d.hBitmap;
    if (!own)
        s->usage++;
    *out = s->dc;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_release_dc(void *This, HDC dc)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (!s->dc)
        return DdLeave(DDERR_NODC);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    if (dc != s->dc && version >= DDV3)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->desc.ddsCaps.dwCaps & DDSCAPS_OWNDC)
        return DdLeave(dc == s->dc ? DD_OK : DDERR_INVALIDPARAMS);
    GdiFlush();
    release_dc(s);
    if (s->usage)
        s->usage--;
    DdSurfWritten(s, &(RECT){0, 0, (LONG)s->desc.dwWidth, (LONG)s->desc.dwHeight});
    return DdLeave(DD_OK);
}

/* ------------------------------------------------------------------ state */

static HRESULT STDMETHODCALLTYPE surf_restore(void *This)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    return DdLeave(DdSurfRestore(s));
}

/* windows' Restore: back in the mode it was made in; implicit surfaces come back with their root. */
HRESULT DdSurfRestore(DDSURF *s)
{
    if (!s->lost)
        return DD_OK;
    if (optimized(s))
        return DDERR_OPTIMIZED;
    if (s->implicit)
        return DDERR_IMPLICITLYCREATED;
    DDMODE mode;
    DdCurrentMode(&mode);
    if (!(s->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) &&
        (mode.width != s->mode.width || mode.height != s->mode.height || mode.bpp != s->mode.bpp))
        return (s->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) ? DDERR_WRONGMODE : DDERR_WRONGMODE;
    DDRAW *owner = DdExclusiveOwner();
    if (s->dd && ((owner && owner != s->dd) || (!owner && (s->dd->flags & DDF_EXCLUSIVE))))
        return DDERR_WRONGMODE;
    s->lost = FALSE;
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DDATTACH *at = CONTAINING_RECORD(e, DDATTACH, links);
        if (at->implicit)
            at->surface->lost = FALSE;
    }
    /* a chain's members are attached to each other: every member of the root's complex surface returns. */
    for (DDSURF *b = next_flip(s); b && b != s; b = next_flip(b))
        b->lost = FALSE;
    return DD_OK;
}

static BOOL keys_supported(DDSURF *s, DWORD slot, BOOL range)
{
    /* system memory uses windows' emulation, which keys source blts only, single colors. */
    BOOL hel = (s->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) != 0;
    switch (slot) {
    case DDCKEY_DESTBLT:
        return !hel && !range;
    case DDCKEY_SRCBLT:
        return !range;
    default:
        return FALSE;
    }
}

static HRESULT STDMETHODCALLTYPE surf_set_color_key(void *This, DWORD flags, LPDDCOLORKEY key)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    if (s->desc.ddsCaps.dwCaps & (DDSCAPS_ZBUFFER | DDSCAPS_EXECUTEBUFFER))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    if (version == DDV7 && (s->desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL))
        return DdLeave(DDERR_NOTONMIPMAPSUBLEVEL);
    if (flags & ~0x1Fu)
        return DdLeave(DDERR_INVALIDPARAMS);
    DWORD slot = key_slot(flags, s, FALSE);
    if (!slot)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (slot == DDCKEY_DESTOVERLAY)
        return DdLeave(DDERR_NOOVERLAYHW);
    if (!key) {
        s->ckeys &= ~key_flag(slot);
        return DdLeave(DD_OK);
    }
    DDCOLORKEY k = *key;
    if (!(flags & DDCKEY_COLORSPACE))
        k.dwColorSpaceHighValue = k.dwColorSpaceLowValue;
    if (slot != DDCKEY_SRCOVERLAY && !keys_supported(s, slot, k.dwColorSpaceHighValue != k.dwColorSpaceLowValue))
        return DdLeave(DDERR_NOCOLORKEYHW);
    *key_of(s, slot) = k;
    s->ckeys |= key_flag(slot);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_set_clipper(void *This, LPDIRECTDRAWCLIPPER clipper)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (s->desc.ddsCaps.dwCaps & (DDSCAPS_OPTIMIZED | DDSCAPS_EXECUTEBUFFER))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    DDCLIP *c = DdClipFromIface(clipper);
    if (clipper && !c)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!c) {
        if (!s->clipper)
            return DdLeave(DDERR_NOCLIPPERATTACHED);
        DDCLIP *old = s->clipper;
        s->clipper = nullptr;
        old->iface.lpVtbl->Release(&old->iface);
        return DdLeave(DD_OK);
    }
    if (c == s->clipper)
        return DdLeave(DD_OK);
    DDCLIP *old = s->clipper;
    c->iface.lpVtbl->AddRef(&c->iface);
    s->clipper = c;
    if (old)
        old->iface.lpVtbl->Release(&old->iface);
    return DdLeave(DD_OK);
}

/* attaching takes a reference, detaching gives it back; the screen shows a primary's change. */
static void set_palette(DDSURF *s, DDPAL *p)
{
    DDPAL *old = s->palette;
    if (old == p)
        return;
    if (p)
        p->iface.lpVtbl->AddRef(&p->iface);
    s->palette = p;
    bump(s);
    if (old)
        old->iface.lpVtbl->Release(&old->iface);
    if (s->dd && s->dd->primary == s)
        DdPresentDirty(s, nullptr);
}

static HRESULT STDMETHODCALLTYPE surf_set_palette(void *This, LPDIRECTDRAWPALETTE palette)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    if (s->desc.ddsCaps.dwCaps & (DDSCAPS_ZBUFFER | DDSCAPS_EXECUTEBUFFER))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    if (version == DDV7 && (s->desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL))
        return DdLeave(DDERR_NOTONMIPMAPSUBLEVEL);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    DDPAL *p = DdPalFromIface(palette);
    if (palette && !p)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (p) {
        if ((p->caps & DDPCAPS_ALPHA) && !(s->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))
            return DdLeave(DDERR_INVALIDSURFACETYPE);
        if (p->dd && s->dd && p->dd != s->dd)
            return DdLeave(DDERR_INVALIDPARAMS);
    }
    DDRAW *owner = DdExclusiveOwner();
    if ((s->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) && owner && owner != s->dd)
        return DdLeave(DDERR_NOEXCLUSIVEMODE);
    if (!p) {
        if (!s->palette)
            return DdLeave(DDERR_NOPALETTEATTACHED);
        set_palette(s, nullptr);
        return DdLeave(DD_OK);
    }
    DWORD pf = s->format.pf.dwFlags, need;
    if (p->caps & DDPCAPS_1BIT)
        need = DDPF_PALETTEINDEXED1;
    else if (p->caps & DDPCAPS_2BIT)
        need = DDPF_PALETTEINDEXED2;
    else if (p->caps & DDPCAPS_4BIT)
        need = DDPF_PALETTEINDEXED4;
    else
        need = DDPF_PALETTEINDEXED8;
    if (!(pf & need) || !(p->caps & DDPCAPS_8BITENTRIES) != !(pf & DDPF_PALETTEINDEXEDTO8))
        return DdLeave(DDERR_INVALIDPIXELFORMAT);
    set_palette(s, p);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_dd_interface(void *This, LPVOID *out)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!out)
        return DdLeave(DDERR_INVALIDPARAMS);
    *out = nullptr;
    if (!s->dd || !s->creator_iface)
        return DdLeave(DDERR_INVALIDOBJECT);
    IUnknown *dd = s->creator_iface;
    dd->lpVtbl->AddRef(dd);
    *out = dd;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_page_lock(void *This, DWORD flags)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (flags)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    return DdLeave(DD_OK);
}

/* system memory surfaces whose memory a program may give: the size, pitch and format change with it. */
static HRESULT set_surface_desc(DDSURF *s, const DDSURFACEDESC2 *d, DWORD flags)
{
    if (optimized(s))
        return DDERR_OPTIMIZED;
    if (flags || d->dwSize != sizeof(DDSURFACEDESC2))
        return DDERR_INVALIDPARAMS;
    if (s->lost)
        return DDERR_SURFACELOST;
    if (DdSurfBusy(s))
        return DDERR_SURFACEBUSY;
    DWORD caps = s->desc.ddsCaps.dwCaps;
    if (!s->sysmem_requested || (caps & (DDSCAPS_ALLOCONLOAD | DDSCAPS_PRIMARYSURFACE | DDSCAPS_OVERLAY)) ||
        (s->desc.ddsCaps.dwCaps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)) || s->implicit)
        return DDERR_INVALIDSURFACETYPE;
    if (d->dwFlags & ~(DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_LPSURFACE | DDSD_PIXELFORMAT))
        return DDERR_INVALIDPARAMS;
    if (((d->dwFlags & DDSD_WIDTH) && !d->dwWidth) || ((d->dwFlags & DDSD_HEIGHT) && !d->dwHeight) ||
        ((d->dwFlags & DDSD_PITCH) && (d->lPitch <= 0 || (d->lPitch & 3))) ||
        ((d->dwFlags & DDSD_PIXELFORMAT) && (!s->explicit_format || d->ddpfPixelFormat.dwSize != sizeof(DDPIXELFORMAT))) ||
        !(d->dwFlags & DDSD_WIDTH) != !(d->dwFlags & DDSD_PITCH) || !(d->dwFlags & DDSD_LPSURFACE) || !d->lpSurface)
        return DDERR_INVALIDPARAMS;
    if ((d->dwFlags & DDSD_CAPS) && (d->ddsCaps.dwCaps || d->ddsCaps.dwCaps3 || d->ddsCaps.dwCaps4))
        return DDERR_INVALIDCAPS;
    DDFORMAT format = s->format;
    if ((d->dwFlags & DDSD_PIXELFORMAT) && !DdFormatFromPixelFormat(&d->ddpfPixelFormat, &format))
        return DDERR_INVALIDPIXELFORMAT;
    DWORD width = d->dwFlags & DDSD_WIDTH ? d->dwWidth : s->desc.dwWidth;
    DWORD height = d->dwFlags & DDSD_HEIGHT ? d->dwHeight : s->desc.dwHeight;
    LONG pitch = d->dwFlags & DDSD_PITCH ? d->lPitch : s->desc.lPitch;
    if (!format.block && (ULONG64)pitch * 8 < (ULONG64)width * format.bpp)
        return DDERR_INVALIDPARAMS;
    release_dc(s);
    free_memory(s);
    s->format = format;
    s->desc.dwWidth = width;
    s->desc.dwHeight = height;
    s->desc.lPitch = pitch;
    s->memory = d->lpSurface;
    s->lost = FALSE;
    s->uniqueness = 0;
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE surf_set_surface_desc(void *This, void *desc, DWORD flags)
{
    HRESULT hr;
    DWORD version;
    DDSURF *s = enter(This, &version, &hr);
    if (!s)
        return hr;
    if (!desc)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDSURFACEDESC2 d;
    if (version == DDV3) {
        if (flags || ((DDSURFACEDESC *)desc)->dwSize != sizeof(DDSURFACEDESC))
            return DdLeave(DDERR_INVALIDPARAMS);
        DdSurfDescFromV1(desc, &d);
    } else {
        d = *(DDSURFACEDESC2 *)desc;
    }
    return DdLeave(set_surface_desc(s, &d, flags));
}

/* ------------------------------------------------------------------ private data */

static DDPRIVATE *find_private(DDSURF *s, REFGUID tag)
{
    for (LIST_ENTRY *e = s->private_data.Flink; e != &s->private_data; e = e->Flink) {
        DDPRIVATE *p = CONTAINING_RECORD(e, DDPRIVATE, links);
        if (IsEqualGUID(&p->tag, tag))
            return p;
    }
    return nullptr;
}

static void free_one_private(DDPRIVATE *p)
{
    RemoveEntryList(&p->links);
    if (p->flags & DDSPD_IUNKNOWNPOINTER) {
        IUnknown *unknown = *(IUnknown **)p->data;
        unknown->lpVtbl->Release(unknown);
    }
    HeapFree(GetProcessHeap(), 0, p);
}

static HRESULT STDMETHODCALLTYPE surf_set_private_data(void *This, REFGUID tag, LPVOID data, DWORD size, DWORD flags)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!tag || !size || (flags & ~(DDSPD_IUNKNOWNPOINTER | DDSPD_VOLATILE)))
        return DdLeave(DDERR_INVALIDPARAMS);
    DDPRIVATE *old = find_private(s, tag);
    if (old)
        free_one_private(old);
    if ((flags & DDSPD_IUNKNOWNPOINTER) && size != sizeof(IUnknown *))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!data)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDPRIVATE *p = HeapAlloc(GetProcessHeap(), 0, sizeof(*p) + size);
    if (!p)
        return DdLeave(DDERR_OUTOFMEMORY);
    p->tag = *tag;
    p->flags = flags;
    p->size = size;
    p->uniqueness = s->uniqueness;
    if (flags & DDSPD_IUNKNOWNPOINTER) {
        IUnknown *unknown = data;
        unknown->lpVtbl->AddRef(unknown);
        memcpy(p->data, &unknown, sizeof(unknown));
    } else {
        memcpy(p->data, data, size);
    }
    InsertHeadList(&s->private_data, &p->links);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_private_data(void *This, REFGUID tag, LPVOID data, LPDWORD size)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!tag || !size)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDPRIVATE *p = find_private(s, tag);
    if (!p)
        return DdLeave(DDERR_NOTFOUND);
    if ((p->flags & DDSPD_VOLATILE) && (!s->uniqueness || s->uniqueness != p->uniqueness)) {
        *size = 0;
        return DdLeave(DDERR_EXPIRED);
    }
    if (*size < p->size) {
        *size = p->size;
        return DdLeave(DDERR_MOREDATA);
    }
    if (!data)
        return DdLeave(DDERR_INVALIDPARAMS);
    memcpy(data, p->data, p->size);
    *size = p->size;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_free_private_data(void *This, REFGUID tag)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    DDPRIVATE *p = tag ? find_private(s, tag) : nullptr;
    if (!p)
        return DdLeave(DDERR_NOTFOUND);
    free_one_private(p);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_uniqueness_value(void *This, LPDWORD value)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!value)
        return DdLeave(DDERR_INVALIDPARAMS);
    *value = s->uniqueness;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_change_uniqueness_value(void *This)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    bump(s);
    return DdLeave(DD_OK);
}

/* priority and level of detail belong to managed textures, which d3d keeps. */
static BOOL managed_root(DDSURF *s)
{
    return (s->desc.ddsCaps.dwCaps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)) &&
           !(s->desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL) && !s->implicit;
}

static HRESULT STDMETHODCALLTYPE surf_set_priority(void *This, DWORD priority)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!managed_root(s))
        return DdLeave(DDERR_INVALIDOBJECT);
    s->priority = priority;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_priority(void *This, LPDWORD priority)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!priority)
        return DdLeave(DDERR_INVALIDPARAMS);
    if ((s->desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL) || s->implicit)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!managed_root(s))
        return DdLeave(DDERR_INVALIDOBJECT);
    *priority = s->priority;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_set_lod(void *This, DWORD lod)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if ((s->desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL) || s->implicit)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!managed_root(s))
        return DdLeave(DDERR_INVALIDOBJECT);
    if (lod >= mip_levels(s))
        return DdLeave(DDERR_INVALIDPARAMS);
    s->lod = lod;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_get_lod(void *This, LPDWORD lod)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!lod)
        return DdLeave(DDERR_INVALIDPARAMS);
    if ((s->desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL) || s->implicit)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!managed_root(s))
        return DdLeave(DDERR_INVALIDOBJECT);
    *lod = s->lod;
    return DdLeave(DD_OK);
}

/* ------------------------------------------------------------------ overlays, which there are none of */

static HRESULT STDMETHODCALLTYPE surf_add_overlay_dirty_rect(void *This, LPRECT rect)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    return DdLeave((s->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY) ? DDERR_UNSUPPORTED : DDERR_NOTAOVERLAYSURFACE);
}

static HRESULT STDMETHODCALLTYPE surf_get_overlay_position(void *This, LPLONG x, LPLONG y)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    return DdLeave(DDERR_NOTAOVERLAYSURFACE);
}

static HRESULT STDMETHODCALLTYPE surf_set_overlay_position(void *This, LONG x, LONG y)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    return DdLeave(DDERR_NOTAOVERLAYSURFACE);
}

static HRESULT STDMETHODCALLTYPE surf_update_overlay(void *This, LPRECT src_rect, void *dest, LPRECT dst_rect,
                                                     DWORD flags, LPDDOVERLAYFX fx)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    DDSURF *d = dest ? DdSurfFromIface(dest) : nullptr;
    if (s->lost || (d && d->lost))
        return DdLeave(DDERR_SURFACELOST);
    if (optimized(s) || (d && optimized(d)))
        return DdLeave(DDERR_OPTIMIZED);
    if ((flags & 0xF8000000) || (!fx && (flags & 0x80000)) || (fx && fx->dwSize != sizeof(DDOVERLAYFX)))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!(s->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
        return DdLeave(DDERR_NOTAOVERLAYSURFACE);
    return DdLeave(DDERR_NOOVERLAYHW);
}

static HRESULT STDMETHODCALLTYPE surf_update_overlay_display(void *This, DWORD flags)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (flags & ~0x30000u)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (execute_buffer(s))
        return DdLeave(DDERR_INVALIDSURFACETYPE);
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    return DdLeave((s->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) ? DDERR_UNSUPPORTED : DD_OK);
}

static HRESULT STDMETHODCALLTYPE surf_update_overlay_z_order(void *This, DWORD flags, void *reference)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (optimized(s))
        return DdLeave(DDERR_OPTIMIZED);
    return DdLeave(DDERR_NOTAOVERLAYSURFACE);
}

/* ------------------------------------------------------------------ gamma and color controls */

static BOOL is_primary(DDSURF *s)
{
    return (s->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
}

static HRESULT STDMETHODCALLTYPE gamma_get_ramp(IDirectDrawGammaControl *This, DWORD flags, LPDDGAMMARAMP ramp)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (!ramp || flags || !is_primary(s))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (s->gamma) {
        *ramp = *s->gamma;
        return DdLeave(DD_OK);
    }
    /* none set: gdi's ramp of the display. */
    HDC dc = GetDC(nullptr);
    BOOL ok = dc && GetDeviceGammaRamp(dc, ramp);
    if (dc)
        ReleaseDC(nullptr, dc);
    return DdLeave(ok ? DD_OK : DDERR_UNSUPPORTED);
}

void DdShowGamma(DDRAW *dd)
{
    DDSURF *s = dd->primary;
    if (s && s->gamma && DdIsExclusiveOwner(dd) && (dd->flags & DDF_ACTIVE)) {
        NtGdiMimeGamma(MIME_GAMMA_DEVICE, (PUSHORT)s->gamma);
        s->gamma_shown = TRUE;
    }
}

/* windows writes back the ramp it found, gdi's, which is what the display shows once the device ramp goes. */
void DdHideGamma(DDSURF *s)
{
    if (s->gamma_shown)
        NtGdiMimeGamma(MIME_GAMMA_DEVICE, nullptr);
    s->gamma_shown = FALSE;
}

/* the ramp shows while the object holds exclusive mode, else it is only kept. no calibrator is known. */
static HRESULT STDMETHODCALLTYPE gamma_set_ramp(IDirectDrawGammaControl *This, DWORD flags, LPDDGAMMARAMP ramp)
{
    HRESULT hr;
    DDSURF *s = enter(This, nullptr, &hr);
    if (!s)
        return hr;
    if (flags & ~DDSGR_CALIBRATE)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (flags & DDSGR_CALIBRATE)
        return DdLeave(DDERR_UNSUPPORTED);
    if (!is_primary(s))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s->lost)
        return DdLeave(DDERR_SURFACELOST);
    if (!ramp) {
        DdHideGamma(s);
        HeapFree(GetProcessHeap(), 0, s->gamma);
        s->gamma = nullptr;
        return DdLeave(DD_OK);
    }
    if (!s->gamma)
        s->gamma = HeapAlloc(GetProcessHeap(), 0, sizeof(*s->gamma));
    if (!s->gamma)
        return DdLeave(DDERR_OUTOFMEMORY);
    *s->gamma = *ramp;
    if (s->dd && s->dd->primary == s)
        DdShowGamma(s->dd);
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE color_get_controls(IDirectDrawColorControl *This, LPDDCOLORCONTROL controls)
{
    if (!controls || controls->dwSize != sizeof(DDCOLORCONTROL))
        return DDERR_INVALIDPARAMS;
    return DDERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE color_set_controls(IDirectDrawColorControl *This, LPDDCOLORCONTROL controls)
{
    if (!controls || controls->dwSize != sizeof(DDCOLORCONTROL))
        return DDERR_INVALIDPARAMS;
    return DDERR_UNSUPPORTED;
}

static const IDirectDrawGammaControlVtbl gamma_vtbl = {(void *)surf_query, (void *)surf_add_ref,
                                                       (void *)surf_release, gamma_get_ramp, gamma_set_ramp};

static const IDirectDrawColorControlVtbl color_vtbl = {(void *)surf_query, (void *)surf_add_ref,
                                                       (void *)surf_release, color_get_controls, color_set_controls};

/* ------------------------------------------------------------------ vtables */

#define SURFACE_COMMON                                                                                                \
    (void *)surf_query, (void *)surf_add_ref, (void *)surf_release, (void *)surf_add_attached_surface,                 \
        (void *)surf_add_overlay_dirty_rect, (void *)surf_blt, (void *)surf_blt_batch, (void *)surf_blt_fast,          \
        (void *)surf_delete_attached_surface, (void *)surf_enum_attached_surfaces,                                     \
        (void *)surf_enum_overlay_z_orders, (void *)surf_flip, (void *)surf_get_attached_surface,                      \
        (void *)surf_get_blt_status, (void *)surf_get_caps, (void *)surf_get_clipper, (void *)surf_get_color_key,      \
        (void *)surf_get_dc, (void *)surf_get_flip_status, (void *)surf_get_overlay_position,                          \
        (void *)surf_get_palette, (void *)surf_get_pixel_format, (void *)surf_get_surface_desc,                        \
        (void *)surf_initialize, (void *)surf_is_lost, (void *)surf_lock, (void *)surf_release_dc,                     \
        (void *)surf_restore, (void *)surf_set_clipper, (void *)surf_set_color_key,                                    \
        (void *)surf_set_overlay_position, (void *)surf_set_palette, (void *)surf_unlock,                              \
        (void *)surf_update_overlay, (void *)surf_update_overlay_display, (void *)surf_update_overlay_z_order

#define SURFACE_V2 (void *)surf_get_dd_interface, (void *)surf_page_lock, (void *)surf_page_lock
#define SURFACE_V4                                                                                                    \
    (void *)surf_set_private_data, (void *)surf_get_private_data, (void *)surf_free_private_data,                     \
        (void *)surf_get_uniqueness_value, (void *)surf_change_uniqueness_value

static const IDirectDrawSurfaceVtbl surf1_vtbl = {SURFACE_COMMON};
static const IDirectDrawSurface2Vtbl surf2_vtbl = {SURFACE_COMMON, SURFACE_V2};
static const IDirectDrawSurface3Vtbl surf3_vtbl = {SURFACE_COMMON, SURFACE_V2, (void *)surf_set_surface_desc};
static const IDirectDrawSurface4Vtbl surf4_vtbl = {SURFACE_COMMON, SURFACE_V2, (void *)surf_set_surface_desc,
                                                   SURFACE_V4};
static const IDirectDrawSurface7Vtbl surf7_vtbl = {SURFACE_COMMON,          SURFACE_V2,
                                                   (void *)surf_set_surface_desc, SURFACE_V4,
                                                   (void *)surf_set_priority, (void *)surf_get_priority,
                                                   (void *)surf_set_lod,     (void *)surf_get_lod};
