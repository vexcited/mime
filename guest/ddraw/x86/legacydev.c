/*
 * IDirect3DDevice, IDirect3DDevice2 and IDirect3DDevice3 of d3dim.dll,
 * build 26300: one object behind three interfaces, whose methods behave
 * by the version that made it. a first device lives in the surface that
 * made it, its IUnknown being the surface's.
 */
#include "d3d_private.h"

double __cdecl sqrt(double x);

static const IDirect3DDeviceVtbl dev1_vtbl;
static const IDirect3DDevice2Vtbl dev2_vtbl;
static const IDirect3DDevice3Vtbl dev3_vtbl;
static const IUnknownVtbl inner_vtbl;

D3DDEV *D3dDevFromLegacy(void *iface)
{
    void **vtbl = iface;
    if (!vtbl)
        return nullptr;
    if (*vtbl == &dev1_vtbl)
        return CONTAINING_RECORD(iface, D3DDEV, iface1);
    if (*vtbl == &dev2_vtbl)
        return CONTAINING_RECORD(iface, D3DDEV, iface2);
    if (*vtbl == &dev3_vtbl)
        return CONTAINING_RECORD(iface, D3DDEV, iface3);
    return nullptr;
}

void D3dLegacyDeviceInit(D3DDEV *dev)
{
    dev->iface1.lpVtbl = &dev1_vtbl;
    dev->iface2.lpVtbl = &dev2_vtbl;
    dev->iface3.lpVtbl = &dev3_vtbl;
    dev->inner.lpVtbl = &inner_vtbl;
}

static D3DDEV *enter(void *iface, HRESULT *hr)
{
    DdEnter();
    D3DDEV *dev = D3dDevFromLegacy(iface);
    if (!dev) {
        DdLeave(D3D_OK);
        *hr = DDERR_INVALIDOBJECT;
    }
    return dev;
}

/* ------------------------------------------------------------------ the inner unknown */

static D3DDEV *from_inner(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, D3DDEV, inner);
}

/* a first device answers only for the guid that made it; the others for their interfaces. */
static HRESULT STDMETHODCALLTYPE inner_query(IUnknown *This, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    D3DDEV *dev = from_inner(This);
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &dev->inner;
    else if (dev->version == 1 && IsEqualGUID(riid, &dev->guid))
        *out = &dev->iface1;
    else if (dev->version > 1 && IsEqualGUID(riid, &IID_IDirect3DDevice))
        *out = &dev->iface1;
    else if (dev->version > 1 && IsEqualGUID(riid, &IID_IDirect3DDevice2))
        *out = &dev->iface2;
    else if (dev->version == 3 && IsEqualGUID(riid, &IID_IDirect3DDevice3))
        *out = &dev->iface3;
    else
        return E_NOINTERFACE;
    DdEnter();
    if (*out == &dev->inner)
        dev->ref++;
    else
        dev->outer->lpVtbl->AddRef(dev->outer);
    DdLeave(D3D_OK);
    return D3D_OK;
}

static ULONG STDMETHODCALLTYPE inner_add_ref(IUnknown *This)
{
    DdEnter();
    ULONG ref = (ULONG)++from_inner(This)->ref;
    DdLeave(D3D_OK);
    return ref;
}

static ULONG STDMETHODCALLTYPE inner_release(IUnknown *This)
{
    D3DDEV *dev = from_inner(This);
    DdEnter();
    ULONG ref = (ULONG)--dev->ref;
    if (!ref)
        D3dDevDestroy(dev);
    DdLeave(D3D_OK);
    return ref;
}

static const IUnknownVtbl inner_vtbl = {inner_query, inner_add_ref, inner_release};

/* ------------------------------------------------------------------ the methods, shared by the interfaces */

static HRESULT query(D3DDEV *dev, REFIID riid, void **out)
{
    return dev->outer->lpVtbl->QueryInterface(dev->outer, riid, out);
}

static ULONG add_ref(D3DDEV *dev)
{
    return dev->outer->lpVtbl->AddRef(dev->outer);
}

static ULONG release(D3DDEV *dev)
{
    return dev->outer->lpVtbl->Release(dev->outer);
}

static BOOL desc_size_valid(DWORD size)
{
    return size == sizeof(D3DDEVICEDESC) || size == D3DDEVICEDESCSIZE_V1 || size == D3DDEVICEDESCSIZE_V2;
}

/* the stored descriptions, their own size included, as far as the caller's sizes go. */
static HRESULT get_caps(D3DDEV *dev, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw)
{
    if (!hw || !sw || !desc_size_valid(hw->dwSize) || !desc_size_valid(sw->dwSize))
        return DDERR_INVALIDPARAMS;
    memcpy(hw, &dev->legacy->hw, hw->dwSize);
    memcpy(sw, &dev->legacy->sw, sw->dwSize);
    return D3D_OK;
}

static HRESULT get_stats(D3DDEV *dev, D3DSTATS *stats)
{
    if (dev->version == 3)
        return E_NOTIMPL;
    if (!stats || stats->dwSize != sizeof(*stats))
        return DDERR_INVALIDPARAMS;
    *stats = dev->legacy->stats;
    stats->dwSize = sizeof(*stats);
    return D3D_OK;
}

static D3DTEXHANDLE *handle_of(D3DDEV *dev, D3DTEXOBJ *t)
{
    for (LIST_ENTRY *e = t->handles.Flink; e != &t->handles; e = e->Flink) {
        D3DTEXHANDLE *h = CONTAINING_RECORD(e, D3DTEXHANDLE, tex_links);
        if (h->dev == dev)
            return h;
    }
    return nullptr;
}

/* the two handles name each other's textures from now on. */
static HRESULT swap_texture_handles(D3DDEV *dev, D3DTEXOBJ *a, D3DTEXOBJ *b)
{
    if (!a || !b)
        return DDERR_INVALIDOBJECT;
    if ((a->surface->desc.ddsCaps.dwCaps2 | b->surface->desc.ddsCaps.dwCaps2) & DDSCAPS2_TEXTUREMANAGE)
        return DDERR_INVALIDPARAMS;
    D3DTEXHANDLE *ha = handle_of(dev, a), *hb = handle_of(dev, b);
    if (!ha || !hb)
        return DDERR_INVALIDPARAMS;
    RemoveEntryList(&ha->tex_links);
    RemoveEntryList(&hb->tex_links);
    ha->texture = b;
    hb->texture = a;
    InsertTailList(&b->handles, &ha->tex_links);
    InsertTailList(&a->handles, &hb->tex_links);
    DWORD bound = dev->rs[D3DRENDERSTATE_TEXTUREHANDLE];
    if (bound == (DWORD)(ULONG_PTR)ha || bound == (DWORD)(ULONG_PTR)hb)
        D3dSetTextureWeak(dev, 0, D3dTextureHandle(dev, bound)->texture->surface);
    return D3D_OK;
}

/* ------------------------------------------------------------------ viewports */

static HRESULT add_viewport(D3DDEV *dev, D3DVIEWPORTOBJ *vp)
{
    if (!vp || vp->dev)
        return DDERR_INVALIDPARAMS;
    D3DLEGACY *l = dev->legacy;
    InsertTailList(&l->viewports, &vp->dev_links);
    vp->dev = dev;
    l->viewport_count++;
    vp->ref++;
    return D3D_OK;
}

static HRESULT delete_viewport(D3DDEV *dev, D3DVIEWPORTOBJ *vp)
{
    D3DLEGACY *l = dev->legacy;
    if (!vp)
        return DDERR_INVALIDPARAMS;
    if (l->in_begin)
        return D3DERR_INBEGIN;
    if (vp->dev != dev)
        return DDERR_INVALIDPARAMS;
    RemoveEntryList(&vp->dev_links);
    InitializeListHead(&vp->dev_links);
    vp->dev = nullptr;
    l->viewport_count--;
    if (l->downloaded == vp->id)
        l->downloaded = 0;
    if (l->current == vp) {
        l->current = nullptr;
        vp->iface.lpVtbl->Release(&vp->iface);
    }
    vp->iface.lpVtbl->Release(&vp->iface);
    return D3D_OK;
}

static HRESULT next_viewport(D3DDEV *dev, D3DVIEWPORTOBJ *ref, IDirect3DViewport3 **next, DWORD flags)
{
    D3DLEGACY *l = dev->legacy;
    if (flags == D3DNEXT_NEXT && (!ref || ref->dev != dev))
        return DDERR_INVALIDPARAMS;
    if (IsListEmpty(&l->viewports))
        return D3DERR_NOVIEWPORTS;
    if (l->in_begin)
        return D3DERR_INBEGIN;
    LIST_ENTRY *e;
    switch (flags) {
    case D3DNEXT_NEXT:
        e = ref->dev_links.Flink;
        break;
    case D3DNEXT_HEAD:
        e = l->viewports.Flink;
        break;
    case D3DNEXT_TAIL:
        e = l->viewports.Blink;
        break;
    default:
        return DDERR_INVALIDPARAMS;
    }
    if (e != &l->viewports) {
        D3DVIEWPORTOBJ *vp = CONTAINING_RECORD(e, D3DVIEWPORTOBJ, dev_links);
        vp->ref++;
        *next = &vp->iface;
    }
    return D3D_OK;
}

static HRESULT set_current_viewport(D3DDEV *dev, D3DVIEWPORTOBJ *vp)
{
    D3DLEGACY *l = dev->legacy;
    if (!vp)
        return DDERR_INVALIDPARAMS;
    if (l->in_begin)
        return D3DERR_INBEGIN;
    if (vp->dev != dev)
        return DDERR_INVALIDPARAMS;
    if (vp->set && l->downloaded != vp->id) {
        HRESULT hr = D3dLegacyDownload(dev, vp);
        if (FAILED(hr))
            return hr;
    }
    vp->ref++;
    if (l->current)
        l->current->iface.lpVtbl->Release(&l->current->iface);
    l->current = vp;
    return D3D_OK;
}

static HRESULT get_current_viewport(D3DDEV *dev, IDirect3DViewport3 **out)
{
    D3DLEGACY *l = dev->legacy;
    if (!out)
        return DDERR_INVALIDPARAMS;
    if (l->in_begin)
        return D3DERR_INBEGIN;
    if (!l->current)
        return D3DERR_NOCURRENTVIEWPORT;
    l->current->ref++;
    *out = &l->current->iface;
    return D3D_OK;
}

/* ------------------------------------------------------------------ texture formats */

#define RGB_FORMAT(bits, r, g, b, a)                                                                                   \
    {sizeof(DDPIXELFORMAT), DDPF_RGB | ((a) ? DDPF_ALPHAPIXELS : 0), 0, {bits}, {r}, {g}, {b}, {a}}

/* the rgb rasterizer's, the last two for the third interface only. */
static const DDPIXELFORMAT SOFTWARE_FORMATS[] = {
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0),
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000),
    RGB_FORMAT(16, 0xF800, 0x7E0, 0x1F, 0),
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0),
    {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED4, 0, {4}},
    {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED8, 0, {8}},
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0x8000),
    RGB_FORMAT(16, 0xF00, 0xF0, 0xF, 0xF000),
};

/* what d3dim enumerates of a driver without registry lists: the formats of directx 5. */
static const DDPIXELFORMAT SAFE_FORMATS[] = {
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0),
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0x8000),
    RGB_FORMAT(16, 0xF800, 0x7E0, 0x1F, 0),
    RGB_FORMAT(16, 0xF00, 0xF0, 0xF, 0xF000),
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000),
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0),
    RGB_FORMAT(8, 0xE0, 0x1C, 0x3, 0),
    {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED4, 0, {4}},
    {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED8, 0, {8}},
};

static BOOL same_format(const DDPIXELFORMAT *a, const DDPIXELFORMAT *b)
{
    return a->dwFlags == b->dwFlags && a->dwFourCC == b->dwFourCC && a->dwRGBBitCount == b->dwRGBBitCount &&
           a->dwRBitMask == b->dwRBitMask && a->dwGBitMask == b->dwGBitMask && a->dwBBitMask == b->dwBBitMask &&
           a->dwRGBAlphaBitMask == b->dwRGBAlphaBitMask;
}

typedef HRESULT (*FORMAT_FN)(DDSURFACEDESC *desc, void *context);

static HRESULT enum_formats(D3DDEV *dev, FORMAT_FN fn, void *context)
{
    static const DDPIXELFORMAT PAL8 = {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED8, 0, {8}};
    const DDPIXELFORMAT *list = SOFTWARE_FORMATS;
    DWORD count = dev->version == 3 ? 8 : 6;
    BOOL mono = IsEqualGUID(&dev->guid, &IID_IDirect3DRampDevice);
    if (dev->hal) {
        list = D3dTextureFormats;
        count = D3dTextureFormatCount;
    } else if (mono || (IsEqualGUID(&dev->guid, &IID_IDirect3DMMXDevice) && dev->version < 3)) {
        list = &PAL8;
        count = 1;
    }
    DWORD sent = 0;
    for (DWORD i = 0; i < count; i++) {
        const DDPIXELFORMAT *pf = &list[i];
        BOOL fourcc = (pf->dwFlags & DDPF_FOURCC) != 0;
        if (!fourcc) {
            BOOL safe = FALSE;
            for (UINT j = 0; j < RTL_NUMBER_OF(SAFE_FORMATS) && !safe; j++)
                safe = same_format(pf, &SAFE_FORMATS[j]);
            if (!safe)
                continue;
        } else if (dev->version < 3) {
            continue;
        }
        if (dev->version < 3 && sent == 10)
            break;
        DDSURFACEDESC desc = {sizeof(desc), DDSD_CAPS | DDSD_PIXELFORMAT};
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
        desc.ddpfPixelFormat = *pf;
        sent++;
        if (fn(&desc, context) != D3DENUMRET_OK)
            break;
    }
    return D3D_OK;
}

typedef struct FORMAT_CONTEXT {
    void *callback, *context;
} FORMAT_CONTEXT;

static HRESULT desc_forward(DDSURFACEDESC *desc, void *context)
{
    FORMAT_CONTEXT *c = context;
    return ((LPD3DENUMTEXTUREFORMATSCALLBACK)c->callback)(desc, c->context);
}

static HRESULT format_forward(DDSURFACEDESC *desc, void *context)
{
    FORMAT_CONTEXT *c = context;
    return ((LPD3DENUMPIXELFORMATSCALLBACK)c->callback)(&desc->ddpfPixelFormat, c->context);
}

/* ------------------------------------------------------------------ scenes, targets and the direct3d object */

static HRESULT begin_scene(D3DDEV *dev)
{
    if (dev->in_scene)
        return D3DERR_SCENE_IN_SCENE;
    if (dev->target->lost || (dev->depth && dev->depth->lost))
        return DDERR_SURFACELOST;
    if (dev->target->usage || (dev->depth && dev->depth->usage))
        return DDERR_SURFACEBUSY;
    dev->in_scene = TRUE;
    return D3D_OK;
}

static HRESULT end_scene(D3DDEV *dev)
{
    if (!dev->in_scene)
        return D3DERR_SCENE_NOT_IN_SCENE;
    dev->in_scene = FALSE;
    return D3D_OK;
}

static HRESULT get_direct3d(D3DDEV *dev, DWORD version, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    D3DOBJ *d3d = dev->d3d;
    if (!d3d)
        return DDERR_INVALIDOBJECT;
    D3dObjAddRef(d3d);
    *out = version == 1 ? (void *)&d3d->iface1 : version == 2 ? (void *)&d3d->iface2 : (void *)&d3d->iface3;
    return D3D_OK;
}

/* a new target keeps a depth buffer if the old one had one; texture handles go to the new target. */
static HRESULT set_render_target(D3DDEV *dev, DDSURF *target)
{
    if (!target)
        return DDERR_INVALIDOBJECT;
    if (dev->version == 1)
        return D3DERR_DEVICEAGGREGATED;
    D3DLEGACY *l = dev->legacy;
    if (!(target->desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE) ||
        !(D3dDdbd(target->format.bpp) & (l->hw.dwFlags ? l->hw.dwDeviceRenderBitDepth : l->sw.dwDeviceRenderBitDepth)))
        return DDERR_INVALIDCAPS;
    if (target->desc.dwWidth > 2048 || target->desc.dwHeight > 2048)
        return DDERR_INVALIDPARAMS;
    HRESULT hr = D3dCheckPalette(target);
    if (FAILED(hr))
        return hr;
    DDSURF *depth = D3dAttachedDepth(target);
    if (!depth && dev->depth)
        return DDERR_INVALIDPARAMS;
    D3dHoldTargets(dev, target, depth);
    return D3D_OK;
}

static HRESULT get_render_target(D3DDEV *dev, DWORD version, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    DdSurfAddRef(dev->target, DdSurfRefOf(dev->target, version));
    *out = DdSurfIface(dev->target, version);
    return D3D_OK;
}

/* ------------------------------------------------------------------ states */

static HRESULT get_render_state(D3DDEV *dev, DWORD type, DWORD *value)
{
    if (!type || type > 255 || !value)
        return DDERR_INVALIDPARAMS;
    *value = dev->rs[type];
    return D3D_OK;
}

static HRESULT set_light_state(D3DDEV *dev, DWORD type, DWORD value)
{
    if (dev->legacy->in_begin)
        return D3DERR_INBEGIN;
    return D3dLegacySetLightState(dev, type, value);
}

static HRESULT get_light_state(D3DDEV *dev, DWORD type, DWORD *value)
{
    if (!type || type > (dev->version == 3 ? D3DLIGHTSTATE_COLORVERTEX : D3DLIGHTSTATE_FOGDENSITY) || !value)
        return DDERR_INVALIDPARAMS;
    *value = dev->legacy->light_states[type];
    if (type == D3DLIGHTSTATE_COLORVERTEX)
        *value = *value != 0;
    return D3D_OK;
}

static BOOL legacy_transform(DWORD type)
{
    return type >= D3DTRANSFORMSTATE_WORLD && type <= D3DTRANSFORMSTATE_PROJECTION;
}

static HRESULT set_transform(D3DDEV *dev, DWORD type, const D3DMATRIX *m)
{
    if (!legacy_transform(type) || !m)
        return DDERR_INVALIDPARAMS;
    D3dSetTransform(dev, type, m);
    return D3D_OK;
}

static HRESULT get_transform(D3DDEV *dev, DWORD type, D3DMATRIX *m)
{
    if (!legacy_transform(type) || !m)
        return DDERR_INVALIDPARAMS;
    *m = dev->transforms[type];
    return D3D_OK;
}

static HRESULT multiply_transform(D3DDEV *dev, DWORD type, const D3DMATRIX *m)
{
    if (!legacy_transform(type) || !m)
        return DDERR_INVALIDPARAMS;
    D3DMATRIX r = D3dMultiply(m, &dev->transforms[type]);
    D3dSetTransform(dev, type, &r);
    return D3D_OK;
}

static HRESULT set_clip_status(D3DDEV *dev, const D3DCLIPSTATUS *status)
{
    D3DLEGACY *l = dev->legacy;
    if (!status || (dev->version == 3 && (status->dwFlags & D3DCLIPSTATUS_EXTENTS3)))
        return DDERR_INVALIDPARAMS;
    if (status->dwFlags & D3DCLIPSTATUS_STATUS)
        l->status = status->dwStatus;
    if (status->dwFlags & (D3DCLIPSTATUS_EXTENTS2 | D3DCLIPSTATUS_EXTENTS3)) {
        l->extents[0] = status->minx;
        l->extents[1] = status->miny;
        l->extents[2] = status->maxx;
        l->extents[3] = status->maxy;
    }
    return D3D_OK;
}

static HRESULT get_clip_status(D3DDEV *dev, D3DCLIPSTATUS *status)
{
    D3DLEGACY *l = dev->legacy;
    if (!status)
        return DDERR_INVALIDPARAMS;
    status->dwFlags = D3DCLIPSTATUS_EXTENTS2;
    status->dwStatus = l->status;
    status->minx = l->extents[0];
    status->miny = l->extents[1];
    status->maxx = l->extents[2];
    status->maxy = l->extents[3];
    return D3D_OK;
}

/* ------------------------------------------------------------------ drawing */

/* the vertex types of the second interface as formats; others draw nothing. */
static DWORD fvf_of_type(DWORD type)
{
    switch (type) {
    case D3DVT_VERTEX:
        return D3DFVF_VERTEX;
    case D3DVT_LVERTEX:
        return D3DFVF_LVERTEX;
    case D3DVT_TLVERTEX:
        return D3DFVF_TLVERTEX;
    default:
        return 0;
    }
}

static void count_primitives(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD n, DWORD vertices)
{
    D3DSTATS *s = &dev->legacy->stats;
    DWORD primitives = D3dPrimitiveCount(type, n);
    if (type == D3DPT_POINTLIST)
        s->dwPointsDrawn += primitives;
    else if (type == D3DPT_LINELIST || type == D3DPT_LINESTRIP)
        s->dwLinesDrawn += primitives;
    else
        s->dwTrianglesDrawn += primitives;
    s->dwVerticesProcessed += vertices;
}

/*
 * the rasterizers' MODULATE of a texture without alpha takes the diffuse
 * alpha, for these exact stage states only.
 */
static BOOL diffuse_alpha(D3DDEV *dev)
{
    const DWORD *t = dev->tss[0];
    DDSURF *s = dev->textures[0];
    if (!s || dev->tss[1][D3DTSS_COLOROP] != D3DTOP_DISABLE || t[D3DTSS_COLOROP] != D3DTOP_MODULATE ||
        t[D3DTSS_ALPHAOP] != D3DTOP_SELECTARG1 || t[D3DTSS_COLORARG1] != D3DTA_TEXTURE ||
        t[D3DTSS_ALPHAARG1] != D3DTA_TEXTURE || t[D3DTSS_COLORARG2] != D3DTA_DIFFUSE ||
        t[D3DTSS_ALPHAARG2] != D3DTA_DIFFUSE)
        return FALSE;
    if (s->format.pf.dwFlags & DDPF_ALPHAPIXELS)
        return FALSE;
    return !(D3dPalettized(s) && s->palette && (s->palette->caps & DDPCAPS_ALPHA));
}

HRESULT D3dLegacyDraw(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD vertex_count,
                      const WORD *indices, DWORD index_count, DWORD flags)
{
    HRESULT hr = D3dLegacyDownloadCurrent(dev);
    if (FAILED(hr))
        return hr;
    count_primitives(dev, type, indices ? index_count : vertex_count, vertex_count);
    return D3dLegacyRender(dev, type, fvf, vertices, vertex_count, indices, index_count, flags);
}

HRESULT D3dLegacyRender(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD vertex_count,
                        const WORD *indices, DWORD index_count, DWORD flags)
{
    D3DLEGACY *l = dev->legacy;
    if (!vertices || !vertex_count || !fvf)
        return D3D_OK;
    BOOL transformed = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    /* lit here, the backend only transforming. */
    if (!transformed && (fvf & D3DFVF_NORMAL) && !(flags & D3DDP_DONOTLIGHT) &&
        l->light_states[D3DLIGHTSTATE_MATERIAL]) {
        DWORD out_fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | (fvf & (D3DFVF_TEXCOUNT_MASK | 0xFFFF0000));
        DWORD in_stride = D3dFvfSize(fvf), out_stride = D3dFvfSize(out_fvf), tex = out_stride - 20;
        SIZE_T need = (SIZE_T)out_stride * vertex_count;
        if (need > l->lit_size) {
            BYTE *lit = l->lit ? HeapReAlloc(GetProcessHeap(), 0, l->lit, need) : HeapAlloc(GetProcessHeap(), 0, need);
            if (!lit)
                return DDERR_OUTOFMEMORY;
            l->lit = lit;
            l->lit_size = need;
        }
        const BYTE *in = vertices;
        for (DWORD i = 0; i < vertex_count; i++) {
            const BYTE *v = in + (SIZE_T)i * in_stride;
            BYTE *o = l->lit + (SIZE_T)i * out_stride;
            memcpy(o, v, 12);
            memcpy(o + 20, v + in_stride - tex, tex);
        }
        D3dLegacyLight(dev, vertices, fvf, vertex_count, l->lit + 12, out_stride);
        vertices = l->lit;
        fvf = out_fvf;
    } else if (IsEqualGUID(&dev->guid, &IID_IDirect3DRampDevice) && (fvf & D3DFVF_DIFFUSE)) {
        /* the mono model's colors as their ramps', in a copy. */
        DWORD stride = D3dFvfSize(fvf), at = D3dFvfSize(fvf & (D3DFVF_POSITION_MASK | D3DFVF_NORMAL | D3DFVF_RESERVED1));
        SIZE_T need = (SIZE_T)stride * vertex_count;
        if (need > l->lit_size) {
            BYTE *lit = l->lit ? HeapReAlloc(GetProcessHeap(), 0, l->lit, need) : HeapAlloc(GetProcessHeap(), 0, need);
            if (!lit)
                return DDERR_OUTOFMEMORY;
            l->lit = lit;
            l->lit_size = need;
        }
        memcpy(l->lit, vertices, need);
        for (DWORD i = 0; i < vertex_count; i++) {
            DWORD *c = (DWORD *)(l->lit + (SIZE_T)i * stride + at);
            DWORD specular = (fvf & D3DFVF_SPECULAR) ? c[1] : 0;
            c[0] = D3dRampConvert(dev, c[0], specular);
            if (fvf & D3DFVF_SPECULAR)
                c[1] &= 0xFF000000;
        }
        vertices = l->lit;
    }
    D3dDevActivate(dev);
    BOOL alpha = diffuse_alpha(dev);
    if (alpha)
        DbSetTextureStageState(dev->db, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    HRESULT hr = D3dDraw(dev, type, fvf, vertices, vertex_count, indices, index_count, 0);
    if (alpha)
        DbSetTextureStageState(dev->db, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    return hr;
}

static HRESULT draw_strided(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const D3DDRAWPRIMITIVESTRIDEDDATA *data,
                            DWORD count, const WORD *indices, DWORD index_count, DWORD flags)
{
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
        return D3DERR_INVALIDVERTEXTYPE;
    if (!data || !count)
        return D3D_OK;
    const BYTE *packed = D3dPackStrided(dev, fvf, data, count);
    if (!packed)
        return DDERR_OUTOFMEMORY;
    return D3dLegacyDraw(dev, type, fvf, packed, count, indices, index_count, flags);
}

/* ------------------------------------------------------------------ Begin and End */

static DWORD keep_count(D3DPRIMITIVETYPE type, DWORD n)
{
    switch (type) {
    case D3DPT_LINELIST:
        return n & 1;
    case D3DPT_LINESTRIP:
        return 1;
    case D3DPT_TRIANGLELIST:
        return n % 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return 2;
    default:
        return 0;
    }
}

/* what a batch drawn in the middle keeps for the primitives that continue it: a fan its first element. */
static void keep_tail(D3DPRIMITIVETYPE type, BYTE *items, DWORD *n, DWORD size)
{
    DWORD keep = keep_count(type, *n);
    if (keep > *n)
        keep = *n;
    if (type == D3DPT_TRIANGLEFAN && keep == 2)
        memmove(items + size, items + (SIZE_T)(*n - 1) * size, size);
    else
        memmove(items, items + (SIZE_T)(*n - keep) * size, (SIZE_T)keep * size);
    *n = keep;
}

static HRESULT flush_begin(D3DDEV *dev, BOOL partial)
{
    D3DLEGACY *l = dev->legacy;
    BOOL indexed = l->begin_user != nullptr;
    DWORD n = indexed ? l->begin_index_count : l->begin_count;
    if (!D3dPrimitiveCount(l->begin_type, n)) {
        l->in_begin = FALSE;
        return DDERR_INVALIDPARAMS;
    }
    HRESULT hr = indexed ? D3dLegacyDraw(dev, l->begin_type, l->begin_fvf, l->begin_user, l->begin_user_count,
                                         l->begin_indices, l->begin_index_count, l->begin_flags)
                         : D3dLegacyDraw(dev, l->begin_type, l->begin_fvf, l->begin_vertices, l->begin_count, nullptr,
                                         0, l->begin_flags);
    if (partial) {
        if (indexed)
            keep_tail(l->begin_type, (BYTE *)l->begin_indices, &l->begin_index_count, sizeof(WORD));
        else
            keep_tail(l->begin_type, l->begin_vertices, &l->begin_count, l->begin_stride);
    }
    return hr;
}

static HRESULT begin(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD count, DWORD flags)
{
    D3DLEGACY *l = dev->legacy;
    HRESULT hr = D3dLegacyDownloadCurrent(dev);
    if (FAILED(hr))
        return hr;
    if (!l->begin_vertices) {
        l->begin_vertices = HeapAlloc(GetProcessHeap(), 0, 0x8000);
        l->begin_indices = HeapAlloc(GetProcessHeap(), 0, 0x2000);
        if (!l->begin_vertices || !l->begin_indices)
            return DDERR_OUTOFMEMORY;
    }
    DWORD stride = D3dFvfSize(fvf);
    l->begin_type = type;
    l->begin_fvf = fvf;
    l->begin_flags = flags;
    l->begin_stride = stride;
    l->begin_capacity = (0x8000 / stride) & ~1u;
    l->begin_count = l->begin_index_count = 0;
    l->begin_user = vertices;
    l->begin_user_count = count;
    l->in_begin = TRUE;
    return D3D_OK;
}

static HRESULT vertex(D3DDEV *dev, const void *v)
{
    D3DLEGACY *l = dev->legacy;
    if (!l->in_begin)
        return D3DERR_NOTINBEGIN;
    if (!v || l->begin_user)
        return DDERR_INVALIDPARAMS;
    if (l->begin_count == l->begin_capacity) {
        HRESULT hr = flush_begin(dev, TRUE);
        if (FAILED(hr))
            return hr;
    }
    memcpy(l->begin_vertices + (SIZE_T)l->begin_count++ * l->begin_stride, v, l->begin_stride);
    return D3D_OK;
}

static HRESULT index(D3DDEV *dev, WORD i)
{
    D3DLEGACY *l = dev->legacy;
    if (!l->in_begin)
        return D3DERR_NOTINBEGIN;
    if (!l->begin_user)
        return DDERR_INVALIDPARAMS;
    if (l->begin_index_count == 0x2000 / sizeof(WORD)) {
        HRESULT hr = flush_begin(dev, TRUE);
        if (FAILED(hr))
            return hr;
    }
    l->begin_indices[l->begin_index_count++] = i;
    return D3D_OK;
}

static HRESULT end(D3DDEV *dev)
{
    D3DLEGACY *l = dev->legacy;
    if (!l->in_begin)
        return D3DERR_NOTINBEGIN;
    HRESULT hr = flush_begin(dev, FALSE);
    l->in_begin = FALSE;
    return hr;
}

/* ------------------------------------------------------------------ the third interface's own */

static HRESULT get_texture(D3DDEV *dev, DWORD stage, IDirect3DTexture2 **out)
{
    if (stage >= D3D_STAGES || !out)
        return DDERR_INVALIDPARAMS;
    D3DTEXOBJ *t = dev->legacy->stage_textures[stage];
    if (t)
        t->iface2.lpVtbl->AddRef(&t->iface2);
    *out = t ? &t->iface2 : nullptr;
    return D3D_OK;
}

static HRESULT set_texture(D3DDEV *dev, DWORD stage, IDirect3DTexture2 *iface)
{
    if (stage >= D3D_STAGES)
        return DDERR_INVALIDPARAMS;
    D3DTEXOBJ *t = D3dTextureFrom(iface);
    if (iface && !t)
        return DDERR_INVALIDPARAMS;
    if (dev->legacy->stage_textures[stage] != t)
        D3dLegacySetTexture(dev, stage, t);
    return D3D_OK;
}

static HRESULT get_tss(D3DDEV *dev, DWORD stage, DWORD type, DWORD *value)
{
    if (stage >= D3D_STAGES || type >= 32 || !value)
        return DDERR_INVALIDPARAMS;
    *value = dev->legacy->api_tss[stage][type];
    return D3D_OK;
}

static HRESULT set_tss(D3DDEV *dev, DWORD stage, DWORD type, DWORD value)
{
    if (stage >= D3D_STAGES || type < D3DTSS_COLOROP || type > D3DTSS_BUMPENVLOFFSET)
        return DDERR_INVALIDPARAMS;
    if (dev->legacy->api_tss[stage][type] != value)
        D3dLegacySetTss(dev, stage, type, value);
    return D3D_OK;
}

/* the software rasterizers blend two stages of a texture and a color; a driver does everything. */
static HRESULT validate(D3DDEV *dev, DWORD *passes)
{
    if (!passes)
        return DDERR_INVALIDPARAMS;
    *passes = 1;
    if (dev->hal)
        return D3D_OK;
    for (DWORD stage = 0; stage < 2; stage++) {
        const DWORD *t = dev->tss[stage];
        DWORD op = t[D3DTSS_COLOROP];
        if (op == D3DTOP_DISABLE)
            return D3D_OK;
        BOOL known = (op >= D3DTOP_SELECTARG1 && op <= D3DTOP_ADDSIGNED) ||
                     (op >= D3DTOP_BLENDDIFFUSEALPHA && op <= D3DTOP_BLENDTEXTUREALPHAPM);
        if (!known)
            return D3DERR_UNSUPPORTEDCOLOROPERATION;
        DWORD a1 = t[D3DTSS_COLORARG1] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE),
              a2 = t[D3DTSS_COLORARG2] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
        if (a1 != D3DTA_TEXTURE || (a2 != D3DTA_DIFFUSE && a2 != D3DTA_CURRENT && a2 != D3DTA_TFACTOR))
            return D3DERR_UNSUPPORTEDCOLORARG;
        DWORD aop = t[D3DTSS_ALPHAOP];
        if (aop == D3DTOP_DISABLE)
            continue;
        known = (aop >= D3DTOP_SELECTARG1 && aop <= D3DTOP_ADDSIGNED) ||
                (aop >= D3DTOP_BLENDDIFFUSEALPHA && aop <= D3DTOP_BLENDTEXTUREALPHAPM);
        if (!known)
            return D3DERR_UNSUPPORTEDALPHAOPERATION;
        a1 = t[D3DTSS_ALPHAARG1] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
        a2 = t[D3DTSS_ALPHAARG2] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
        if (a1 != D3DTA_TEXTURE || (a2 != D3DTA_DIFFUSE && a2 != D3DTA_CURRENT && a2 != D3DTA_TFACTOR))
            return D3DERR_UNSUPPORTEDALPHAARG;
    }
    DWORD op = dev->tss[2][D3DTSS_COLOROP];
    return op == D3DTOP_DISABLE || !op ? D3D_OK : D3DERR_TOOMANYOPERATIONS;
}

/*
 * the six planes of world, view, projection and the clip volume, and
 * each sphere's center against them: outside a plane past its radius,
 * crossing it within.
 */
static HRESULT sphere_visibility(D3DDEV *dev, const D3DVECTOR *centers, const float *radii, DWORD count,
                                 DWORD *results)
{
    if (!centers || !radii || !results)
        return DDERR_INVALIDPARAMS;
    D3DMATRIX m = D3dMultiply(&dev->transforms[D3DTRANSFORMSTATE_WORLD], &dev->transforms[D3DTRANSFORMSTATE_VIEW]);
    m = D3dMultiply(&m, &dev->transforms[D3DTRANSFORMSTATE_PROJECTION]);
    m = D3dMultiply(&m, &dev->legacy->clip_map);
    float col[4][4];
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            col[j][i] = (&m._11)[i * 4 + j];
    /* left, right, top, bottom, near and far, as the D3DVIS bits order them. */
    float planes[6][4];
    for (int i = 0; i < 4; i++) {
        planes[0][i] = col[3][i] + col[0][i];
        planes[1][i] = col[3][i] - col[0][i];
        planes[2][i] = col[3][i] - col[1][i];
        planes[3][i] = col[3][i] + col[1][i];
        planes[4][i] = col[2][i];
        planes[5][i] = col[3][i] - col[2][i];
    }
    for (int p = 0; p < 6; p++) {
        float *pl = planes[p];
        float len = pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2];
        if (len == 0.0f)
            return D3DERR_INVALIDMATRIX;
        float inv = 1.0f / (float)sqrt(len);
        for (int i = 0; i < 4; i++)
            pl[i] *= inv;
    }
    for (DWORD s = 0; s < count; s++) {
        DWORD result = 0, outside = 0, intersect = 0;
        for (int p = 0; p < 6; p++) {
            const float *pl = planes[p];
            float d = pl[0] * centers[s].x + pl[1] * centers[s].y + pl[2] * centers[s].z + pl[3];
            if (d < -radii[s]) {
                result |= 2u << (2 + 2 * p);
                outside = 1;
            } else if (d < radii[s]) {
                result |= 1u << (2 + 2 * p);
                intersect = 1;
            }
        }
        results[s] = result | (outside ? D3DVIS_OUTSIDE_FRUSTUM : intersect ? D3DVIS_INTERSECT_FRUSTUM : 0);
    }
    return D3D_OK;
}

/* ------------------------------------------------------------------ the first interface's own */

D3DMATRIXOBJ *D3dMatrixHandle(D3DDEV *dev, DWORD handle)
{
    if (!handle)
        return nullptr;
    LIST_ENTRY *list = &dev->legacy->matrices;
    for (LIST_ENTRY *e = list->Flink; e != list; e = e->Flink)
        if ((DWORD)(ULONG_PTR)CONTAINING_RECORD(e, D3DMATRIXOBJ, links) == handle)
            return CONTAINING_RECORD(e, D3DMATRIXOBJ, links);
    return nullptr;
}

/* a matrix bound to a transform by an execute buffer updates it. */
void D3dMatrixChanged(D3DDEV *dev, DWORD handle)
{
    D3DLEGACY *l = dev->legacy;
    D3DMATRIXOBJ *m = D3dMatrixHandle(dev, handle);
    for (DWORD t = D3DTRANSFORMSTATE_WORLD; t <= D3DTRANSFORMSTATE_PROJECTION; t++)
        if (m && l->bound[t] == handle)
            D3dSetTransform(dev, t, &m->m);
}

static HRESULT create_matrix(D3DDEV *dev, D3DMATRIXHANDLE *out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    D3DMATRIXOBJ *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return DDERR_OUTOFMEMORY;
    m->m._11 = m->m._22 = m->m._33 = m->m._44 = 1.0f;
    InsertTailList(&dev->legacy->matrices, &m->links);
    *out = (D3DMATRIXHANDLE)(ULONG_PTR)m;
    return D3D_OK;
}

static HRESULT set_matrix(D3DDEV *dev, D3DMATRIXHANDLE handle, const D3DMATRIX *matrix)
{
    D3DMATRIXOBJ *m = D3dMatrixHandle(dev, handle);
    if (!m || !matrix)
        return DDERR_INVALIDPARAMS;
    m->m = *matrix;
    D3dMatrixChanged(dev, handle);
    return D3D_OK;
}

static HRESULT get_matrix(D3DDEV *dev, D3DMATRIXHANDLE handle, D3DMATRIX *matrix)
{
    D3DMATRIXOBJ *m = D3dMatrixHandle(dev, handle);
    if (!m || !matrix)
        return DDERR_INVALIDPARAMS;
    *matrix = m->m;
    return D3D_OK;
}

static HRESULT delete_matrix(D3DDEV *dev, D3DMATRIXHANDLE handle)
{
    D3DMATRIXOBJ *m = D3dMatrixHandle(dev, handle);
    if (!m)
        return DDERR_INVALIDPARAMS;
    for (DWORD t = 0; t < RTL_NUMBER_OF(dev->legacy->bound); t++)
        if (dev->legacy->bound[t] == handle)
            dev->legacy->bound[t] = 0;
    RemoveEntryList(&m->links);
    HeapFree(GetProcessHeap(), 0, m);
    return D3D_OK;
}

/* the viewport made current for one Execute or Pick, downloaded, and the old one back after. */
static HRESULT run_buffer(D3DDEV *dev, IDirect3DExecuteBuffer *iface, IDirect3DViewport *vp_iface, DWORD flags,
                          const D3DRECT *rect)
{
    D3DLEGACY *l = dev->legacy;
    D3DEXECBUF *b = D3dExecuteBufferFrom(iface);
    if (!b)
        return DDERR_INVALIDOBJECT;
    D3DVIEWPORTOBJ *vp = vp_iface ? D3dViewportFrom(vp_iface) : l->current;
    if (!vp)
        return D3DERR_NOCURRENTVIEWPORT;
    if (vp->dev != dev)
        return DDERR_INVALIDPARAMS;
    D3DVIEWPORTOBJ *old = l->current;
    l->current = vp;
    HRESULT hr = D3dLegacyDownloadCurrent(dev);
    if (SUCCEEDED(hr)) {
        if (b->dev != dev)
            hr = DDERR_INVALIDPARAMS;
        else if (b->locked)
            hr = D3DERR_EXECUTE_LOCKED;
        else if (rect)
            hr = D3dPick(dev, b, rect);
        else
            hr = D3dExecute(dev, b, flags);
    }
    l->current = old;
    return hr;
}

static HRESULT get_pick_records(D3DDEV *dev, DWORD *count, D3DPICKRECORD *records)
{
    D3DLEGACY *l = dev->legacy;
    if (!count)
        return DDERR_INVALIDPARAMS;
    if (records && *count >= l->pick_count)
        memcpy(records, l->picks, l->pick_count * sizeof(*records));
    *count = l->pick_count;
    return D3D_OK;
}

/* ------------------------------------------------------------------ the interfaces */

#define CALL(EXPR)                                                                                                     \
    HRESULT hr;                                                                                                        \
    D3DDEV *dev = enter(This, &hr);                                                                                    \
    if (!dev)                                                                                                          \
        return hr;                                                                                                     \
    return DdLeave(EXPR)

/* what all three interfaces have, by the names the vtables give them. */
#define COMMON_METHODS(N, IFACE, VIEWPORT, TEXTURE)                                                                    \
    static HRESULT STDMETHODCALLTYPE dev##N##_query(IFACE *This, REFIID riid, void **out)                              \
    {                                                                                                                  \
        D3DDEV *dev = D3dDevFromLegacy(This);                                                                          \
        return dev ? query(dev, riid, out) : DDERR_INVALIDOBJECT;                                                      \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE dev##N##_add_ref(IFACE *This)                                                       \
    {                                                                                                                  \
        D3DDEV *dev = D3dDevFromLegacy(This);                                                                          \
        return dev ? add_ref(dev) : 0;                                                                                 \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE dev##N##_release(IFACE *This)                                                       \
    {                                                                                                                  \
        D3DDEV *dev = D3dDevFromLegacy(This);                                                                          \
        return dev ? release(dev) : 0;                                                                                 \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_caps(IFACE *This, LPD3DDEVICEDESC hw, LPD3DDEVICEDESC sw)            \
    {                                                                                                                  \
        CALL(get_caps(dev, hw, sw));                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_stats(IFACE *This, LPD3DSTATS stats)                                 \
    {                                                                                                                  \
        CALL(get_stats(dev, stats));                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_add_viewport(IFACE *This, VIEWPORT vp)                                   \
    {                                                                                                                  \
        CALL(add_viewport(dev, D3dViewportFrom(vp)));                                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_delete_viewport(IFACE *This, VIEWPORT vp)                                \
    {                                                                                                                  \
        CALL(delete_viewport(dev, D3dViewportFrom(vp)));                                                               \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_next_viewport(IFACE *This, VIEWPORT ref, VIEWPORT *next, DWORD flags)    \
    {                                                                                                                  \
        if (!next)                                                                                                     \
            return DDERR_INVALIDPARAMS;                                                                                \
        *next = nullptr;                                                                                               \
        CALL(next_viewport(dev, D3dViewportFrom(ref), (IDirect3DViewport3 **)next, flags));                            \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_begin_scene(IFACE *This)                                                 \
    {                                                                                                                  \
        CALL(begin_scene(dev));                                                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_end_scene(IFACE *This)                                                   \
    {                                                                                                                  \
        CALL(end_scene(dev));                                                                                          \
    }

#define STATE_METHODS(N, IFACE, VIEWPORT, SURFACE, SURFACE_VERSION)                                                    \
    static HRESULT STDMETHODCALLTYPE dev##N##_set_current_viewport(IFACE *This, VIEWPORT vp)                           \
    {                                                                                                                  \
        CALL(set_current_viewport(dev, D3dViewportFrom(vp)));                                                          \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_current_viewport(IFACE *This, VIEWPORT *vp)                          \
    {                                                                                                                  \
        CALL(get_current_viewport(dev, (IDirect3DViewport3 **)vp));                                                    \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_set_render_target(IFACE *This, SURFACE surface, DWORD flags)             \
    {                                                                                                                  \
        CALL(set_render_target(dev, DdSurfFromIface(surface)));                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_render_target(IFACE *This, SURFACE *surface)                         \
    {                                                                                                                  \
        CALL(get_render_target(dev, SURFACE_VERSION, (void **)surface));                                               \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_vertex(IFACE *This, LPVOID v)                                            \
    {                                                                                                                  \
        CALL(vertex(dev, v));                                                                                          \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_index(IFACE *This, WORD i)                                               \
    {                                                                                                                  \
        CALL(index(dev, i));                                                                                           \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_end(IFACE *This, DWORD flags)                                            \
    {                                                                                                                  \
        CALL(end(dev));                                                                                                \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_render_state(IFACE *This, D3DRENDERSTATETYPE type, LPDWORD value)    \
    {                                                                                                                  \
        CALL(get_render_state(dev, type, value));                                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_set_render_state(IFACE *This, D3DRENDERSTATETYPE type, DWORD value)      \
    {                                                                                                                  \
        CALL(D3dLegacySetRenderState(dev, type, value));                                                               \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_light_state(IFACE *This, D3DLIGHTSTATETYPE type, LPDWORD value)      \
    {                                                                                                                  \
        CALL(get_light_state(dev, type, value));                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_set_light_state(IFACE *This, D3DLIGHTSTATETYPE type, DWORD value)        \
    {                                                                                                                  \
        CALL(set_light_state(dev, type, value));                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_set_transform(IFACE *This, D3DTRANSFORMSTATETYPE type, LPD3DMATRIX m)    \
    {                                                                                                                  \
        CALL(set_transform(dev, type, m));                                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_transform(IFACE *This, D3DTRANSFORMSTATETYPE type, LPD3DMATRIX m)    \
    {                                                                                                                  \
        CALL(get_transform(dev, type, m));                                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_multiply_transform(IFACE *This, D3DTRANSFORMSTATETYPE type,              \
                                                                 LPD3DMATRIX m)                                        \
    {                                                                                                                  \
        CALL(multiply_transform(dev, type, m));                                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_set_clip_status(IFACE *This, LPD3DCLIPSTATUS status)                     \
    {                                                                                                                  \
        CALL(set_clip_status(dev, status));                                                                            \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE dev##N##_get_clip_status(IFACE *This, LPD3DCLIPSTATUS status)                     \
    {                                                                                                                  \
        CALL(get_clip_status(dev, status));                                                                            \
    }

COMMON_METHODS(1, IDirect3DDevice, LPDIRECT3DVIEWPORT, LPDIRECT3DTEXTURE)
COMMON_METHODS(2, IDirect3DDevice2, LPDIRECT3DVIEWPORT2, LPDIRECT3DTEXTURE2)
COMMON_METHODS(3, IDirect3DDevice3, LPDIRECT3DVIEWPORT3, LPDIRECT3DTEXTURE2)
STATE_METHODS(2, IDirect3DDevice2, LPDIRECT3DVIEWPORT2, LPDIRECTDRAWSURFACE, DDV1)
STATE_METHODS(3, IDirect3DDevice3, LPDIRECT3DVIEWPORT3, LPDIRECTDRAWSURFACE4, DDV4)

static HRESULT STDMETHODCALLTYPE dev1_initialize(IDirect3DDevice *This, LPDIRECT3D d3d, LPGUID guid,
                                                 LPD3DDEVICEDESC desc)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev1_swap_texture_handles(IDirect3DDevice *This, LPDIRECT3DTEXTURE a,
                                                           LPDIRECT3DTEXTURE b)
{
    CALL(swap_texture_handles(dev, D3dTextureFrom(a), D3dTextureFrom(b)));
}

static HRESULT STDMETHODCALLTYPE dev1_create_execute_buffer(IDirect3DDevice *This, LPD3DEXECUTEBUFFERDESC desc,
                                                            LPDIRECT3DEXECUTEBUFFER *buffer, IUnknown *outer)
{
    if (!D3dDevFromLegacy(This))
        return DDERR_INVALIDOBJECT;
    if (!desc || desc->dwSize != sizeof(*desc) || !buffer)
        return DDERR_INVALIDPARAMS;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    *buffer = nullptr;
    CALL(D3dCreateExecuteBuffer(dev, desc, buffer));
}

static HRESULT STDMETHODCALLTYPE dev1_execute(IDirect3DDevice *This, LPDIRECT3DEXECUTEBUFFER buffer,
                                              LPDIRECT3DVIEWPORT vp, DWORD flags)
{
    CALL(run_buffer(dev, buffer, vp, flags, nullptr));
}

static HRESULT STDMETHODCALLTYPE dev1_pick(IDirect3DDevice *This, LPDIRECT3DEXECUTEBUFFER buffer,
                                           LPDIRECT3DVIEWPORT vp, DWORD flags, LPD3DRECT rect)
{
    if (!buffer || !vp || !rect)
        return DDERR_INVALIDOBJECT;
    CALL(run_buffer(dev, buffer, vp, flags, rect));
}

static HRESULT STDMETHODCALLTYPE dev1_get_pick_records(IDirect3DDevice *This, LPDWORD count, LPD3DPICKRECORD records)
{
    CALL(get_pick_records(dev, count, records));
}

static HRESULT STDMETHODCALLTYPE dev1_enum_texture_formats(IDirect3DDevice *This,
                                                           LPD3DENUMTEXTUREFORMATSCALLBACK callback, LPVOID context)
{
    if (!callback)
        return DDERR_INVALIDPARAMS;
    FORMAT_CONTEXT c = {callback, context};
    CALL(enum_formats(dev, desc_forward, &c));
}

static HRESULT STDMETHODCALLTYPE dev1_create_matrix(IDirect3DDevice *This, LPD3DMATRIXHANDLE handle)
{
    CALL(create_matrix(dev, handle));
}

static HRESULT STDMETHODCALLTYPE dev1_set_matrix(IDirect3DDevice *This, D3DMATRIXHANDLE handle,
                                                 const LPD3DMATRIX matrix)
{
    CALL(set_matrix(dev, handle, matrix));
}

static HRESULT STDMETHODCALLTYPE dev1_get_matrix(IDirect3DDevice *This, D3DMATRIXHANDLE handle, LPD3DMATRIX matrix)
{
    CALL(get_matrix(dev, handle, matrix));
}

static HRESULT STDMETHODCALLTYPE dev1_delete_matrix(IDirect3DDevice *This, D3DMATRIXHANDLE handle)
{
    CALL(delete_matrix(dev, handle));
}

static HRESULT STDMETHODCALLTYPE dev1_get_direct3d(IDirect3DDevice *This, LPDIRECT3D *d3d)
{
    CALL(get_direct3d(dev, 1, (void **)d3d));
}

static HRESULT STDMETHODCALLTYPE dev2_swap_texture_handles(IDirect3DDevice2 *This, LPDIRECT3DTEXTURE2 a,
                                                           LPDIRECT3DTEXTURE2 b)
{
    CALL(swap_texture_handles(dev, D3dTextureFrom(a), D3dTextureFrom(b)));
}

static HRESULT STDMETHODCALLTYPE dev2_enum_texture_formats(IDirect3DDevice2 *This,
                                                           LPD3DENUMTEXTUREFORMATSCALLBACK callback, LPVOID context)
{
    if (!callback)
        return DDERR_INVALIDPARAMS;
    FORMAT_CONTEXT c = {callback, context};
    CALL(enum_formats(dev, desc_forward, &c));
}

static HRESULT STDMETHODCALLTYPE dev2_get_direct3d(IDirect3DDevice2 *This, LPDIRECT3D2 *d3d)
{
    CALL(get_direct3d(dev, 2, (void **)d3d));
}

static HRESULT STDMETHODCALLTYPE dev2_begin(IDirect3DDevice2 *This, D3DPRIMITIVETYPE type, D3DVERTEXTYPE vertex_type,
                                            DWORD flags)
{
    CALL(begin(dev, type, fvf_of_type(vertex_type), nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev2_begin_indexed(IDirect3DDevice2 *This, D3DPRIMITIVETYPE type,
                                                    D3DVERTEXTYPE vertex_type, LPVOID vertices, DWORD count,
                                                    DWORD flags)
{
    CALL(begin(dev, type, fvf_of_type(vertex_type), vertices, count, flags));
}

static HRESULT STDMETHODCALLTYPE dev2_draw_primitive(IDirect3DDevice2 *This, D3DPRIMITIVETYPE type,
                                                     D3DVERTEXTYPE vertex_type, LPVOID vertices, DWORD count,
                                                     DWORD flags)
{
    CALL(D3dLegacyDraw(dev, type, fvf_of_type(vertex_type), vertices, count, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev2_draw_indexed_primitive(IDirect3DDevice2 *This, D3DPRIMITIVETYPE type,
                                                             D3DVERTEXTYPE vertex_type, LPVOID vertices,
                                                             DWORD vertex_count, LPWORD indices, DWORD index_count,
                                                             DWORD flags)
{
    if (!indices)
        return D3D_OK;
    CALL(D3dLegacyDraw(dev, type, fvf_of_type(vertex_type), vertices, vertex_count, indices, index_count, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_enum_texture_formats(IDirect3DDevice3 *This,
                                                           LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    if (!callback)
        return DDERR_INVALIDPARAMS;
    FORMAT_CONTEXT c = {callback, context};
    CALL(enum_formats(dev, format_forward, &c));
}

static HRESULT STDMETHODCALLTYPE dev3_get_direct3d(IDirect3DDevice3 *This, LPDIRECT3D3 *d3d)
{
    CALL(get_direct3d(dev, 3, (void **)d3d));
}

static HRESULT STDMETHODCALLTYPE dev3_begin(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type, DWORD fvf, DWORD flags)
{
    CALL(begin(dev, type, fvf, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_begin_indexed(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type, DWORD fvf,
                                                    LPVOID vertices, DWORD count, DWORD flags)
{
    CALL(begin(dev, type, fvf, vertices, count, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_draw_primitive(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type, DWORD fvf,
                                                     LPVOID vertices, DWORD count, DWORD flags)
{
    CALL(D3dLegacyDraw(dev, type, fvf, vertices, count, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_draw_indexed_primitive(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type, DWORD fvf,
                                                             LPVOID vertices, DWORD vertex_count, LPWORD indices,
                                                             DWORD index_count, DWORD flags)
{
    if (!indices)
        return D3D_OK;
    CALL(D3dLegacyDraw(dev, type, fvf, vertices, vertex_count, indices, index_count, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_draw_primitive_strided(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type, DWORD fvf,
                                                             LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD count,
                                                             DWORD flags)
{
    CALL(draw_strided(dev, type, fvf, data, count, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_draw_indexed_primitive_strided(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type,
                                                                     DWORD fvf, LPD3DDRAWPRIMITIVESTRIDEDDATA data,
                                                                     DWORD vertex_count, LPWORD indices,
                                                                     DWORD index_count, DWORD flags)
{
    if (!indices)
        return D3D_OK;
    CALL(draw_strided(dev, type, fvf, data, vertex_count, indices, index_count, flags));
}

/* a buffer's whole vertices, which the indices of the indexed draw count from. */
static HRESULT STDMETHODCALLTYPE dev3_draw_primitive_vb(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type,
                                                        LPDIRECT3DVERTEXBUFFER buffer, DWORD start, DWORD count,
                                                        DWORD flags)
{
    D3DVB *vb = D3dVbFromIface1(buffer);
    if (!vb)
        return DDERR_INVALIDPARAMS;
    if (start >= vb->desc.dwNumVertices)
        return D3D_OK;
    count = count < vb->desc.dwNumVertices - start ? count : vb->desc.dwNumVertices - start;
    CALL(D3dLegacyDraw(dev, type, vb->desc.dwFVF, vb->data + (SIZE_T)start * vb->stride, count, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_draw_indexed_primitive_vb(IDirect3DDevice3 *This, D3DPRIMITIVETYPE type,
                                                                LPDIRECT3DVERTEXBUFFER buffer, LPWORD indices,
                                                                DWORD index_count, DWORD flags)
{
    D3DVB *vb = D3dVbFromIface1(buffer);
    if (!vb)
        return DDERR_INVALIDPARAMS;
    if (!indices)
        return D3D_OK;
    CALL(D3dLegacyDraw(dev, type, vb->desc.dwFVF, vb->data, vb->desc.dwNumVertices, indices, index_count, flags));
}

static HRESULT STDMETHODCALLTYPE dev3_compute_sphere_visibility(IDirect3DDevice3 *This, LPD3DVECTOR centers,
                                                                LPD3DVALUE radii, DWORD count, DWORD flags,
                                                                LPDWORD results)
{
    CALL(sphere_visibility(dev, centers, radii, count, results));
}

static HRESULT STDMETHODCALLTYPE dev3_get_texture(IDirect3DDevice3 *This, DWORD stage, LPDIRECT3DTEXTURE2 *texture)
{
    CALL(get_texture(dev, stage, texture));
}

static HRESULT STDMETHODCALLTYPE dev3_set_texture(IDirect3DDevice3 *This, DWORD stage, LPDIRECT3DTEXTURE2 texture)
{
    CALL(set_texture(dev, stage, texture));
}

static HRESULT STDMETHODCALLTYPE dev3_get_texture_stage_state(IDirect3DDevice3 *This, DWORD stage,
                                                              D3DTEXTURESTAGESTATETYPE type, LPDWORD value)
{
    CALL(get_tss(dev, stage, type, value));
}

static HRESULT STDMETHODCALLTYPE dev3_set_texture_stage_state(IDirect3DDevice3 *This, DWORD stage,
                                                              D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    CALL(set_tss(dev, stage, type, value));
}

static HRESULT STDMETHODCALLTYPE dev3_validate_device(IDirect3DDevice3 *This, LPDWORD passes)
{
    CALL(validate(dev, passes));
}

static const IDirect3DDeviceVtbl dev1_vtbl = {
    dev1_query,
    dev1_add_ref,
    dev1_release,
    dev1_initialize,
    dev1_get_caps,
    dev1_swap_texture_handles,
    dev1_create_execute_buffer,
    dev1_get_stats,
    dev1_execute,
    dev1_add_viewport,
    dev1_delete_viewport,
    dev1_next_viewport,
    dev1_pick,
    dev1_get_pick_records,
    dev1_enum_texture_formats,
    dev1_create_matrix,
    dev1_set_matrix,
    dev1_get_matrix,
    dev1_delete_matrix,
    dev1_begin_scene,
    dev1_end_scene,
    dev1_get_direct3d,
};

static const IDirect3DDevice2Vtbl dev2_vtbl = {
    dev2_query,
    dev2_add_ref,
    dev2_release,
    dev2_get_caps,
    dev2_swap_texture_handles,
    dev2_get_stats,
    dev2_add_viewport,
    dev2_delete_viewport,
    dev2_next_viewport,
    dev2_enum_texture_formats,
    dev2_begin_scene,
    dev2_end_scene,
    dev2_get_direct3d,
    dev2_set_current_viewport,
    dev2_get_current_viewport,
    dev2_set_render_target,
    dev2_get_render_target,
    dev2_begin,
    dev2_begin_indexed,
    dev2_vertex,
    dev2_index,
    dev2_end,
    dev2_get_render_state,
    dev2_set_render_state,
    dev2_get_light_state,
    dev2_set_light_state,
    dev2_set_transform,
    dev2_get_transform,
    dev2_multiply_transform,
    dev2_draw_primitive,
    dev2_draw_indexed_primitive,
    dev2_set_clip_status,
    dev2_get_clip_status,
};

static const IDirect3DDevice3Vtbl dev3_vtbl = {
    dev3_query,
    dev3_add_ref,
    dev3_release,
    dev3_get_caps,
    dev3_get_stats,
    dev3_add_viewport,
    dev3_delete_viewport,
    dev3_next_viewport,
    dev3_enum_texture_formats,
    dev3_begin_scene,
    dev3_end_scene,
    dev3_get_direct3d,
    dev3_set_current_viewport,
    dev3_get_current_viewport,
    dev3_set_render_target,
    dev3_get_render_target,
    dev3_begin,
    dev3_begin_indexed,
    dev3_vertex,
    dev3_index,
    dev3_end,
    dev3_get_render_state,
    dev3_set_render_state,
    dev3_get_light_state,
    dev3_set_light_state,
    dev3_set_transform,
    dev3_get_transform,
    dev3_multiply_transform,
    dev3_draw_primitive,
    dev3_draw_indexed_primitive,
    dev3_set_clip_status,
    dev3_get_clip_status,
    dev3_draw_primitive_strided,
    dev3_draw_indexed_primitive_strided,
    dev3_draw_primitive_vb,
    dev3_draw_indexed_primitive_vb,
    dev3_compute_sphere_visibility,
    dev3_get_texture,
    dev3_set_texture,
    dev3_get_texture_stage_state,
    dev3_set_texture_stage_state,
    dev3_validate_device,
};
