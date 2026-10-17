/*
 * the viewports, lights and materials of direct3d 1 to 3, as d3dim.dll of
 * build 26300 has them. a viewport holds the lights a device of the first
 * interfaces lights with and the clip volume its projection ends in; a
 * material reaches a device through the handle it gets there.
 */
#include "d3d_private.h"

static const IDirect3DViewport3Vtbl viewport_vtbl;
static const IDirect3DLightVtbl light_vtbl;
static const IDirect3DMaterialVtbl material1_vtbl;
static const IDirect3DMaterial2Vtbl material2_vtbl;
static const IDirect3DMaterial3Vtbl material3_vtbl;

D3DVIEWPORTOBJ *D3dViewportFrom(void *iface)
{
    IDirect3DViewport3 *i = iface;
    return i && i->lpVtbl == &viewport_vtbl ? CONTAINING_RECORD(i, D3DVIEWPORTOBJ, iface) : nullptr;
}

D3DLIGHTOBJ *D3dLightFrom(void *iface)
{
    IDirect3DLight *i = iface;
    return i && i->lpVtbl == &light_vtbl ? CONTAINING_RECORD(i, D3DLIGHTOBJ, iface) : nullptr;
}

D3DMATERIALOBJ *D3dMaterialFrom(void *iface)
{
    void **vtbl = iface;
    if (!vtbl)
        return nullptr;
    if (*vtbl == &material1_vtbl)
        return CONTAINING_RECORD(iface, D3DMATERIALOBJ, iface1);
    if (*vtbl == &material2_vtbl)
        return CONTAINING_RECORD(iface, D3DMATERIALOBJ, iface2);
    if (*vtbl == &material3_vtbl)
        return CONTAINING_RECORD(iface, D3DMATERIALOBJ, iface3);
    return nullptr;
}

/* ------------------------------------------------------------------ viewports */

HRESULT D3dCreateViewport(D3DOBJ *d3d, D3DVIEWPORTOBJ **out)
{
    D3DVIEWPORTOBJ *vp = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vp));
    if (!vp)
        return DDERR_OUTOFMEMORY;
    vp->iface.lpVtbl = &viewport_vtbl;
    vp->ref = 1;
    vp->d3d = d3d;
    vp->id = ++d3d->next_viewport;
    InitializeListHead(&vp->lights);
    InitializeListHead(&vp->dev_links);
    vp->lights_dirty = TRUE;
    InsertTailList(&d3d->viewports, &vp->links);
    *out = vp;
    return D3D_OK;
}

static D3DVIEWPORTOBJ *vp_enter(void *iface, HRESULT *hr)
{
    DdEnter();
    D3DVIEWPORTOBJ *vp = D3dViewportFrom(iface);
    if (!vp) {
        DdLeave(D3D_OK);
        *hr = DDERR_INVALIDOBJECT;
    }
    return vp;
}

static HRESULT STDMETHODCALLTYPE vp_query(IDirect3DViewport3 *This, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirect3DViewport) &&
        !IsEqualGUID(riid, &IID_IDirect3DViewport2) && !IsEqualGUID(riid, &IID_IDirect3DViewport3))
        return DdLeave(E_NOINTERFACE);
    vp->ref++;
    *out = &vp->iface;
    return DdLeave(D3D_OK);
}

static ULONG STDMETHODCALLTYPE vp_add_ref(IDirect3DViewport3 *This)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return 0;
    ULONG ref = (ULONG)++vp->ref;
    DdLeave(D3D_OK);
    return ref;
}

static HRESULT delete_light(D3DVIEWPORTOBJ *vp, D3DLIGHTOBJ *l);

static ULONG STDMETHODCALLTYPE vp_release(IDirect3DViewport3 *This)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return 0;
    ULONG ref = (ULONG)--vp->ref;
    if (!ref) {
        while (!IsListEmpty(&vp->lights))
            delete_light(vp, CONTAINING_RECORD(vp->lights.Flink, D3DLIGHTOBJ, vp_links));
        if (vp->dev)
            vp->dev->iface3.lpVtbl->DeleteViewport(&vp->dev->iface3, &vp->iface);
        if (vp->background_depth)
            vp->background_depth->lpVtbl->Release(vp->background_depth);
        RemoveEntryList(&vp->links);
        vp->iface.lpVtbl = nullptr;
        HeapFree(GetProcessHeap(), 0, vp);
    }
    DdLeave(D3D_OK);
    return ref;
}

static HRESULT STDMETHODCALLTYPE vp_initialize(IDirect3DViewport3 *This, LPDIRECT3D d3d)
{
    return DDERR_ALREADYINITIALIZED;
}

/* a device of the second or third interfaces checks the data against its render target. */
static HRESULT set_data(D3DVIEWPORTOBJ *vp, const D3DVIEWPORT2 *data)
{
    D3DDEV *dev = vp->dev;
    if (!dev)
        return D3DERR_VIEWPORTHASNODEVICE;
    if (dev->version >= 2) {
        DWORD w = dev->target->desc.dwWidth, h = dev->target->desc.dwHeight;
        if (data->dvClipWidth == 0.0f || data->dvClipHeight == 0.0f || data->dvMinZ == data->dvMaxZ ||
            data->dwX > w || data->dwY > h || data->dwX + data->dwWidth > w || data->dwY + data->dwHeight > h)
            return DDERR_INVALIDPARAMS;
    }
    vp->data = *data;
    vp->set = TRUE;
    if (dev->legacy->downloaded == vp->id)
        return D3dLegacyDownload(dev, vp);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE vp_set_viewport2(IDirect3DViewport3 *This, LPD3DVIEWPORT2 data)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!data || data->dwSize != sizeof(D3DVIEWPORT2))
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(set_data(vp, data));
}

/* the scale form, as a clip volume centered on the origin. */
static HRESULT STDMETHODCALLTYPE vp_set_viewport(IDirect3DViewport3 *This, LPD3DVIEWPORT data)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!data || data->dwSize != sizeof(D3DVIEWPORT))
        return DdLeave(DDERR_INVALIDPARAMS);
    D3DVIEWPORT2 v = {sizeof(v), data->dwX, data->dwY, data->dwWidth, data->dwHeight};
    if (data->dvScaleX != 0.0f && data->dvScaleY != 0.0f) {
        v.dvClipWidth = (float)data->dwWidth / data->dvScaleX;
        v.dvClipHeight = (float)data->dwHeight / data->dvScaleY;
    }
    v.dvClipX = -v.dvClipWidth / 2.0f;
    v.dvClipY = v.dvClipHeight / 2.0f;
    v.dvMinZ = 0.0f;
    v.dvMaxZ = 1.0f;
    return DdLeave(set_data(vp, &v));
}

static HRESULT STDMETHODCALLTYPE vp_get_viewport2(IDirect3DViewport3 *This, LPD3DVIEWPORT2 data)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!data || data->dwSize != sizeof(D3DVIEWPORT2))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!vp->set)
        return DdLeave(D3DERR_VIEWPORTDATANOTSET);
    *data = vp->data;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE vp_get_viewport(IDirect3DViewport3 *This, LPD3DVIEWPORT data)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!data || data->dwSize != sizeof(D3DVIEWPORT))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!vp->set)
        return DdLeave(D3DERR_VIEWPORTDATANOTSET);
    const D3DVIEWPORT2 *v = &vp->data;
    *data = (D3DVIEWPORT){sizeof(*data), v->dwX, v->dwY, v->dwWidth, v->dwHeight};
    data->dvScaleX = (float)v->dwWidth / v->dvClipWidth;
    data->dvScaleY = (float)v->dwHeight / v->dvClipHeight;
    data->dvMaxX = v->dvClipX + v->dvClipWidth;
    data->dvMaxY = v->dvClipY;
    data->dvMinZ = 0.0f;
    data->dvMaxZ = 1.0f;
    return DdLeave(D3D_OK);
}

/* vertices through the device's transforms and this viewport's clip volume, with their clip codes. */
static HRESULT STDMETHODCALLTYPE vp_transform_vertices(IDirect3DViewport3 *This, DWORD count, LPD3DTRANSFORMDATA data,
                                                       DWORD flags, LPDWORD offscreen)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!data || data->dwSize != sizeof(D3DTRANSFORMDATA) || !(flags & 3))
        return DdLeave(DDERR_INVALIDPARAMS);
    D3DDEV *dev = vp->dev;
    if (!dev)
        return DdLeave(D3DERR_VIEWPORTHASNODEVICE);
    if (FAILED(hr = D3dLegacyDownload(dev, vp)))
        return DdLeave(hr);
    BOOL clipped = (flags & 3) == D3DTRANSFORM_CLIPPED;
    D3DMATRIX m = D3dMultiply(&dev->transforms[D3DTRANSFORMSTATE_WORLD], &dev->transforms[D3DTRANSFORMSTATE_VIEW]);
    m = D3dMultiply(&m, &dev->transforms[D3DTRANSFORMSTATE_PROJECTION]);
    m = D3dMultiply(&m, &dev->legacy->clip_map);
    const D3DVIEWPORT2 *v = &vp->data;
    float X = (float)v->dwX, Y = (float)v->dwY, W = (float)v->dwWidth, H = (float)v->dwHeight;
    float ext[4] = {(float)data->drExtent.x1, (float)data->drExtent.y1, (float)data->drExtent.x2,
                    (float)data->drExtent.y2};
    DWORD intersection = 0xFFFFFFFF, uni = 0;
    for (DWORD i = 0; i < count; i++) {
        const float *in = (const float *)((const BYTE *)data->lpIn + (SIZE_T)i * data->dwInSize);
        float *out = (float *)((BYTE *)data->lpOut + (SIZE_T)i * data->dwOutSize);
        float x = in[0] * m._11 + in[1] * m._21 + in[2] * m._31 + m._41;
        float y = in[0] * m._12 + in[1] * m._22 + in[2] * m._32 + m._42;
        float z = in[0] * m._13 + in[1] * m._23 + in[2] * m._33 + m._43;
        float w = in[0] * m._14 + in[1] * m._24 + in[2] * m._34 + m._44;
        memcpy(out + 4, in + 4, 16);
        /* canonical d3d9 clip space back to d3dim's, 0 to w on every axis. */
        float cx = (x + w) * 0.5f, cy = (y + w) * 0.5f;
        DWORD code = 0;
        if (clipped) {
            if (cx < 0.0f)
                code |= D3DCLIP_LEFT;
            if (cx > w)
                code |= D3DCLIP_RIGHT;
            if (cy > w)
                code |= D3DCLIP_TOP;
            if (cy < 0.0f)
                code |= D3DCLIP_BOTTOM;
            if (z < 0.0f)
                code |= D3DCLIP_FRONT;
            if (z > w)
                code |= D3DCLIP_BACK;
            if (data->lpHOut) {
                float cw = v->dvClipWidth, ch = v->dvClipHeight;
                data->lpHOut[i] = (D3DHVERTEX){code, {cx * cw + w * v->dvClipX}, {cy * ch + w * (v->dvClipY - ch)},
                                               {z * (v->dvMaxZ - v->dvMinZ) - w * v->dvMinZ}};
            }
            intersection = code ? intersection & code : 0;
            uni |= code;
        }
        float rhw = w != 0.0f ? 1.0f / w : 2097152.0f;
        out[3] = rhw;
        if (code)
            continue;
        out[0] = X + W * cx * rhw;
        out[1] = Y + H - H * cy * rhw;
        out[2] = z * rhw;
        ext[0] = out[0] < ext[0] ? out[0] : ext[0];
        ext[1] = out[1] < ext[1] ? out[1] : ext[1];
        ext[2] = out[0] > ext[2] ? out[0] : ext[2];
        ext[3] = out[1] > ext[3] ? out[1] : ext[3];
    }
    if (clipped) {
        data->dwClipIntersection = intersection;
        data->dwClipUnion = uni;
    }
    if (offscreen)
        *offscreen = clipped ? intersection : 0;
    /* the extent grows by a pixel each way, within the viewport. */
    LONG vx = (LONG)v->dwX, vy = (LONG)v->dwY, vr = vx + (LONG)v->dwWidth, vb = vy + (LONG)v->dwHeight;
    LONG x1 = (LONG)ext[0] - 1, y1 = (LONG)ext[1] - 1, x2 = (LONG)ext[2] + 1, y2 = (LONG)ext[3] + 1;
    data->drExtent.x1 = x1 < vx ? vx : x1;
    data->drExtent.y1 = y1 < vy ? vy : y1;
    data->drExtent.x2 = x2 > vr ? vr : x2;
    data->drExtent.y2 = y2 > vb ? vb : y2;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE vp_light_elements(IDirect3DViewport3 *This, DWORD count, LPD3DLIGHTDATA data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vp_set_background(IDirect3DViewport3 *This, D3DMATERIALHANDLE material)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!material)
        return DdLeave(DDERR_INVALIDPARAMS);
    vp->background = material;
    vp->background_set = TRUE;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE vp_get_background(IDirect3DViewport3 *This, LPD3DMATERIALHANDLE material,
                                                   LPBOOL valid)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!material)
        return DdLeave(DDERR_INVALIDPARAMS);
    *material = vp->background_set ? vp->background : 0;
    if (valid)
        *valid = vp->background_set;
    return DdLeave(D3D_OK);
}

/* kept without a reference, as windows does, while the one held before is released. */
static HRESULT STDMETHODCALLTYPE vp_set_background_depth(IDirect3DViewport3 *This, LPDIRECTDRAWSURFACE surface)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (vp->background_depth)
        vp->background_depth->lpVtbl->Release(vp->background_depth);
    vp->background_depth = surface;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE vp_get_background_depth(IDirect3DViewport3 *This, LPDIRECTDRAWSURFACE *surface,
                                                         LPBOOL valid)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!surface)
        return DdLeave(DDERR_INVALIDPARAMS);
    *surface = vp->background_depth;
    if (valid)
        *valid = vp->background_depth != nullptr;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE vp_set_background_depth2(IDirect3DViewport3 *This, LPDIRECTDRAWSURFACE4 surface)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    IDirectDrawSurface *s = nullptr;
    if (surface && FAILED(hr = surface->lpVtbl->QueryInterface(surface, &IID_IDirectDrawSurface, (void **)&s)))
        return DdLeave(hr);
    if (vp->background_depth)
        vp->background_depth->lpVtbl->Release(vp->background_depth);
    vp->background_depth = s;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE vp_get_background_depth2(IDirect3DViewport3 *This, LPDIRECTDRAWSURFACE4 *surface,
                                                          LPBOOL valid)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    if (!surface)
        return DdLeave(DDERR_INVALIDPARAMS);
    *surface = nullptr;
    hr = D3D_OK;
    if (vp->background_depth)
        hr = vp->background_depth->lpVtbl->QueryInterface(vp->background_depth, &IID_IDirectDrawSurface4,
                                                          (void **)surface);
    if (valid)
        *valid = *surface != nullptr;
    return DdLeave(hr);
}

/* the rectangles within the target for a first device, within the viewport otherwise. */
HRESULT D3dViewportClear(D3DVIEWPORTOBJ *vp, DWORD count, const D3DRECT *rects, DWORD flags, DWORD color, float z,
                         DWORD stencil, BOOL second)
{
    D3DDEV *dev = vp->dev;
    if (!dev)
        return D3DERR_VIEWPORTHASNODEVICE;
    if (!second) {
        if (!dev->depth)
            flags &= ~D3DCLEAR_ZBUFFER;
        if (!flags)
            return D3D_OK;
        if (!(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER)))
            return DDERR_INVALIDPARAMS;
        flags &= D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
        /* the background material's diffuse color, black without one. */
        color = 0;
        if (vp->background_set) {
            D3DMATHANDLE *h = D3dMaterialHandle(dev, vp->background);
            if (h && IsEqualGUID(&dev->guid, &IID_IDirect3DRampDevice)) {
                color = D3dRampBackground(dev, &h->data);
            } else if (h) {
                const D3DCOLORVALUE *c = &h->data.diffuse;
                color = ((DWORD)(c->a * 255.0f) & 0xFF) << 24 | ((DWORD)(c->r * 255.0f) & 0xFF) << 16 |
                        ((DWORD)(c->g * 255.0f) & 0xFF) << 8 | ((DWORD)(c->b * 255.0f) & 0xFF);
            }
        }
        z = 1.0f;
        stencil = 0;
    } else {
        if ((flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) && !dev->depth)
            return D3DERR_ZBUFFER_NOTPRESENT;
        if ((flags & D3DCLEAR_STENCIL) && !(dev->depth->format.pf.dwFlags & DDPF_STENCILBUFFER))
            return D3DERR_STENCILBUFFER_NOTPRESENT;
        if (!(flags & 7) || ((flags & D3DCLEAR_ZBUFFER) && vp->background_depth))
            return DDERR_INVALIDPARAMS;
        flags &= 7;
    }
    if (!count)
        return D3D_OK;
    if (!rects)
        return DDERR_INVALIDPARAMS;
    HRESULT hr = D3dLegacyDownload(dev, vp);
    if (FAILED(hr))
        return hr;
    RECT bound = {0, 0, (LONG)dev->target->desc.dwWidth, (LONG)dev->target->desc.dwHeight};
    if (dev->version >= 2)
        bound = (RECT){(LONG)vp->data.dwX, (LONG)vp->data.dwY, (LONG)(vp->data.dwX + vp->data.dwWidth),
                       (LONG)(vp->data.dwY + vp->data.dwHeight)};
    D3DRECT *list = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*list));
    if (!list)
        return DDERR_OUTOFMEMORY;
    DWORD n = 0;
    for (DWORD i = 0; i < count; i++) {
        RECT r = {rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2}, c;
        if (IntersectRect(&c, &r, &bound))
            list[n++] = (D3DRECT){{c.left}, {c.top}, {c.right}, {c.bottom}};
    }
    hr = D3dClearRects(dev, n, list, flags, color, z, stencil);
    HeapFree(GetProcessHeap(), 0, list);
    return hr;
}

static HRESULT STDMETHODCALLTYPE vp_clear(IDirect3DViewport3 *This, DWORD count, LPD3DRECT rects, DWORD flags)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    return DdLeave(D3dViewportClear(vp, count, rects, flags, 0, 1.0f, 0, FALSE));
}

static HRESULT STDMETHODCALLTYPE vp_clear2(IDirect3DViewport3 *This, DWORD count, LPD3DRECT rects, DWORD flags,
                                           D3DCOLOR color, D3DVALUE z, DWORD stencil)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    return DdLeave(D3dViewportClear(vp, count, rects, flags, color, z, stencil, TRUE));
}

static HRESULT STDMETHODCALLTYPE vp_add_light(IDirect3DViewport3 *This, LPDIRECT3DLIGHT iface)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    D3DLIGHTOBJ *l = D3dLightFrom(iface);
    if (!l)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (l->viewport)
        return DdLeave(D3DERR_LIGHTHASVIEWPORT);
    InsertTailList(&vp->lights, &l->vp_links);
    l->viewport = vp;
    vp->light_count++;
    vp->lights_dirty = TRUE;
    l->ref++;
    return DdLeave(D3D_OK);
}

static HRESULT delete_light(D3DVIEWPORTOBJ *vp, D3DLIGHTOBJ *l)
{
    if (l->viewport != vp)
        return D3DERR_LIGHTNOTINTHISVIEWPORT;
    RemoveEntryList(&l->vp_links);
    InitializeListHead(&l->vp_links);
    l->viewport = nullptr;
    vp->light_count--;
    vp->lights_dirty = TRUE;
    l->iface.lpVtbl->Release(&l->iface);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE vp_delete_light(IDirect3DViewport3 *This, LPDIRECT3DLIGHT iface)
{
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    D3DLIGHTOBJ *l = D3dLightFrom(iface);
    if (!l)
        return DdLeave(DDERR_INVALIDPARAMS);
    return DdLeave(delete_light(vp, l));
}

static HRESULT STDMETHODCALLTYPE vp_next_light(IDirect3DViewport3 *This, LPDIRECT3DLIGHT ref_iface,
                                               LPDIRECT3DLIGHT *next, DWORD flags)
{
    if (!next)
        return DDERR_INVALIDPARAMS;
    *next = nullptr;
    HRESULT hr;
    D3DVIEWPORTOBJ *vp = vp_enter(This, &hr);
    if (!vp)
        return hr;
    D3DLIGHTOBJ *l = D3dLightFrom(ref_iface), *found = nullptr;
    if (flags & D3DNEXT_NEXT) {
        if (!l)
            return DdLeave(DDERR_INVALIDPARAMS);
        if (l->viewport != vp)
            return DdLeave(D3DERR_LIGHTNOTINTHISVIEWPORT);
    }
    switch (flags) {
    case D3DNEXT_NEXT:
        if (l->vp_links.Flink != &vp->lights)
            found = CONTAINING_RECORD(l->vp_links.Flink, D3DLIGHTOBJ, vp_links);
        break;
    case D3DNEXT_HEAD:
        if (!IsListEmpty(&vp->lights))
            found = CONTAINING_RECORD(vp->lights.Flink, D3DLIGHTOBJ, vp_links);
        break;
    case D3DNEXT_TAIL:
        if (!IsListEmpty(&vp->lights))
            found = CONTAINING_RECORD(vp->lights.Blink, D3DLIGHTOBJ, vp_links);
        break;
    default:
        return DdLeave(DDERR_INVALIDPARAMS);
    }
    if (found) {
        found->ref++;
        *next = &found->iface;
    }
    return DdLeave(D3D_OK);
}

static const IDirect3DViewport3Vtbl viewport_vtbl = {
    vp_query,
    vp_add_ref,
    vp_release,
    vp_initialize,
    vp_get_viewport,
    vp_set_viewport,
    vp_transform_vertices,
    vp_light_elements,
    vp_set_background,
    vp_get_background,
    vp_set_background_depth,
    vp_get_background_depth,
    vp_clear,
    vp_add_light,
    vp_delete_light,
    vp_next_light,
    vp_get_viewport2,
    vp_set_viewport2,
    vp_set_background_depth2,
    vp_get_background_depth2,
    vp_clear2,
};

/* ------------------------------------------------------------------ lights */

HRESULT D3dCreateLightObj(D3DOBJ *d3d, D3DLIGHTOBJ **out)
{
    D3DLIGHTOBJ *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*l));
    if (!l)
        return DDERR_OUTOFMEMORY;
    l->iface.lpVtbl = &light_vtbl;
    l->ref = 1;
    l->d3d = d3d;
    InitializeListHead(&l->vp_links);
    InsertTailList(&d3d->lights, &l->links);
    *out = l;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE light_query(IDirect3DLight *This, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    D3DLIGHTOBJ *l = D3dLightFrom(This);
    if (!l)
        return DDERR_INVALIDOBJECT;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirect3DLight))
        return E_NOINTERFACE;
    DdEnter();
    l->ref++;
    DdLeave(D3D_OK);
    *out = &l->iface;
    return D3D_OK;
}

static ULONG STDMETHODCALLTYPE light_add_ref(IDirect3DLight *This)
{
    D3DLIGHTOBJ *l = D3dLightFrom(This);
    if (!l)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)++l->ref;
    DdLeave(D3D_OK);
    return ref;
}

static ULONG STDMETHODCALLTYPE light_release(IDirect3DLight *This)
{
    D3DLIGHTOBJ *l = D3dLightFrom(This);
    if (!l)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)--l->ref;
    if (!ref) {
        if (l->viewport) {
            RemoveEntryList(&l->vp_links);
            l->viewport->light_count--;
            l->viewport->lights_dirty = TRUE;
        }
        RemoveEntryList(&l->links);
        l->iface.lpVtbl = nullptr;
        HeapFree(GetProcessHeap(), 0, l);
    }
    DdLeave(D3D_OK);
    return ref;
}

static HRESULT STDMETHODCALLTYPE light_initialize(IDirect3DLight *This, LPDIRECT3D d3d)
{
    return DDERR_ALREADYINITIALIZED;
}

/* a D3DLIGHT is taken as it is; a D3DLIGHT2 is checked as d3dim700 checks its lights. */
static HRESULT STDMETHODCALLTYPE light_set(IDirect3DLight *This, LPD3DLIGHT data)
{
    D3DLIGHTOBJ *l = D3dLightFrom(This);
    if (!l)
        return DDERR_INVALIDOBJECT;
    if (!data || (data->dwSize != sizeof(D3DLIGHT) && data->dwSize != sizeof(D3DLIGHT2)))
        return DDERR_INVALIDPARAMS;
    if (data->dwSize == sizeof(D3DLIGHT2)) {
        if (data->dltType < D3DLIGHT_POINT || data->dltType > D3DLIGHT_PARALLELPOINT)
            return DDERR_INVALIDPARAMS;
        if (data->dvRange < 0.0f || data->dvRange > 1.8446743e19f)
            return DDERR_INVALIDPARAMS;
        if (data->dltType == D3DLIGHT_SPOT || data->dltType == D3DLIGHT_DIRECTIONAL) {
            const D3DVECTOR *d = &data->dvDirection;
            if (d->x * d->x + d->y * d->y + d->z * d->z < 0.00001f)
                return DDERR_INVALIDPARAMS;
        }
        if (data->dltType == D3DLIGHT_SPOT &&
            (data->dvPhi < 0.0f || data->dvPhi > 3.1415927f || data->dvTheta < 0.0f || data->dvTheta > data->dvPhi))
            return DDERR_INVALIDPARAMS;
    }
    DdEnter();
    memset(&l->data, 0, sizeof(l->data));
    memcpy(&l->data, data, data->dwSize);
    l->set = TRUE;
    if (l->viewport)
        l->viewport->lights_dirty = TRUE;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE light_get(IDirect3DLight *This, LPD3DLIGHT data)
{
    D3DLIGHTOBJ *l = D3dLightFrom(This);
    if (!l)
        return DDERR_INVALIDOBJECT;
    if (!data || (data->dwSize != sizeof(D3DLIGHT) && data->dwSize != sizeof(D3DLIGHT2)))
        return DDERR_INVALIDPARAMS;
    /* as much of what was set as the caller has room for. */
    DWORD size = l->data.dwSize < data->dwSize ? l->data.dwSize : data->dwSize;
    memcpy(data, &l->data, size);
    return D3D_OK;
}

static const IDirect3DLightVtbl light_vtbl = {
    light_query, light_add_ref, light_release, light_initialize, light_set, light_get,
};

/* ------------------------------------------------------------------ materials */

HRESULT D3dCreateMaterialObj(D3DOBJ *d3d, D3DMATERIALOBJ **out)
{
    D3DMATERIALOBJ *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return DDERR_OUTOFMEMORY;
    m->iface1.lpVtbl = &material1_vtbl;
    m->iface2.lpVtbl = &material2_vtbl;
    m->iface3.lpVtbl = &material3_vtbl;
    m->ref = 1;
    m->d3d = d3d;
    m->data.dwSize = sizeof(D3DMATERIAL);
    InitializeListHead(&m->handles);
    InsertTailList(&d3d->materials, &m->links);
    *out = m;
    return D3D_OK;
}

/* the material of a handle on a device; null for one it never gave. */
D3DMATHANDLE *D3dMaterialHandle(D3DDEV *dev, DWORD handle)
{
    if (!handle || !dev->legacy)
        return nullptr;
    for (LIST_ENTRY *e = dev->legacy->materials.Flink; e != &dev->legacy->materials; e = e->Flink) {
        D3DMATHANDLE *h = CONTAINING_RECORD(e, D3DMATHANDLE, dev_links);
        if ((DWORD)(ULONG_PTR)h == handle)
            return h;
    }
    return nullptr;
}

static void free_handle(D3DMATHANDLE *h)
{
    D3DLEGACY *legacy = h->dev->legacy;
    if (legacy->light_states[D3DLIGHTSTATE_MATERIAL] == (DWORD)(ULONG_PTR)h)
        legacy->light_states[D3DLIGHTSTATE_MATERIAL] = 0;
    RemoveEntryList(&h->mat_links);
    RemoveEntryList(&h->dev_links);
    HeapFree(GetProcessHeap(), 0, h);
}

void D3dMaterialDeviceGone(D3DMATHANDLE *h)
{
    free_handle(h);
}

static HRESULT STDMETHODCALLTYPE mat_query(D3DMATERIALOBJ *m, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DMaterial3))
        *out = &m->iface3;
    else if (IsEqualGUID(riid, &IID_IDirect3DMaterial2))
        *out = &m->iface2;
    else if (IsEqualGUID(riid, &IID_IDirect3DMaterial))
        *out = &m->iface1;
    else
        return E_NOINTERFACE;
    DdEnter();
    m->ref++;
    DdLeave(D3D_OK);
    return D3D_OK;
}

static ULONG mat_add_ref(D3DMATERIALOBJ *m)
{
    DdEnter();
    ULONG ref = (ULONG)++m->ref;
    DdLeave(D3D_OK);
    return ref;
}

static ULONG mat_release(D3DMATERIALOBJ *m)
{
    DdEnter();
    ULONG ref = (ULONG)--m->ref;
    if (!ref) {
        while (!IsListEmpty(&m->handles))
            free_handle(CONTAINING_RECORD(m->handles.Flink, D3DMATHANDLE, mat_links));
        RemoveEntryList(&m->links);
        m->iface1.lpVtbl = nullptr;
        m->iface2.lpVtbl = nullptr;
        m->iface3.lpVtbl = nullptr;
        HeapFree(GetProcessHeap(), 0, m);
    }
    DdLeave(D3D_OK);
    return ref;
}

/* new values reach every device holding a handle, and its lighting when current there. */
static HRESULT mat_set(D3DMATERIALOBJ *m, const D3DMATERIAL *data)
{
    if (!data || data->dwSize != sizeof(D3DMATERIAL))
        return DDERR_INVALIDPARAMS;
    DdEnter();
    if (!memcmp(&m->data, data, sizeof(*data)))
        return DdLeave(D3D_OK);
    m->data = *data;
    for (LIST_ENTRY *e = m->handles.Flink; e != &m->handles; e = e->Flink) {
        D3DMATHANDLE *h = CONTAINING_RECORD(e, D3DMATHANDLE, mat_links);
        h->data = *data;
        if (h->dev->legacy->light_states[D3DLIGHTSTATE_MATERIAL] == (DWORD)(ULONG_PTR)h)
            h->dev->legacy->material = *data;
    }
    return DdLeave(D3D_OK);
}

static HRESULT mat_get(D3DMATERIALOBJ *m, D3DMATERIAL *data)
{
    if (!data || data->dwSize != sizeof(D3DMATERIAL))
        return DDERR_INVALIDPARAMS;
    *data = m->data;
    return D3D_OK;
}

/* one handle a device, its copy of the material, the same on every call. */
static HRESULT mat_get_handle(D3DMATERIALOBJ *m, D3DDEV *dev, D3DMATERIALHANDLE *handle)
{
    if (!dev)
        return DDERR_INVALIDOBJECT;
    if (!handle)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    for (LIST_ENTRY *e = m->handles.Flink; e != &m->handles; e = e->Flink) {
        D3DMATHANDLE *h = CONTAINING_RECORD(e, D3DMATHANDLE, mat_links);
        if (h->dev == dev) {
            *handle = (DWORD)(ULONG_PTR)h;
            return DdLeave(D3D_OK);
        }
    }
    D3DMATHANDLE *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h)
        return DdLeave(D3DERR_MATERIAL_CREATE_FAILED);
    h->material = m;
    h->dev = dev;
    h->data = m->data;
    InsertTailList(&m->handles, &h->mat_links);
    InsertTailList(&dev->legacy->materials, &h->dev_links);
    *handle = (DWORD)(ULONG_PTR)h;
    return DdLeave(D3D_OK);
}

#define MATERIAL_METHODS(N, IFACE, DEVICE)                                                                             \
    static D3DMATERIALOBJ *m##N(IFACE *This)                                                                         \
    {                                                                                                                  \
        return CONTAINING_RECORD(This, D3DMATERIALOBJ, iface##N);                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE mat##N##_query(IFACE *This, REFIID riid, void **out)                              \
    {                                                                                                                  \
        return mat_query(m##N(This), riid, out);                                                                       \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE mat##N##_add_ref(IFACE *This)                                                       \
    {                                                                                                                  \
        return mat_add_ref(m##N(This));                                                                                \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE mat##N##_release(IFACE *This)                                                       \
    {                                                                                                                  \
        return mat_release(m##N(This));                                                                                \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE mat##N##_set(IFACE *This, LPD3DMATERIAL data)                                     \
    {                                                                                                                  \
        return mat_set(m##N(This), data);                                                                              \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE mat##N##_get(IFACE *This, LPD3DMATERIAL data)                                     \
    {                                                                                                                  \
        return mat_get(m##N(This), data);                                                                              \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE mat##N##_get_handle(IFACE *This, DEVICE device, LPD3DMATERIALHANDLE handle)       \
    {                                                                                                                  \
        return mat_get_handle(m##N(This), D3dDevFromLegacy(device), handle);                                           \
    }

MATERIAL_METHODS(1, IDirect3DMaterial, LPDIRECT3DDEVICE)
MATERIAL_METHODS(2, IDirect3DMaterial2, LPDIRECT3DDEVICE2)
MATERIAL_METHODS(3, IDirect3DMaterial3, LPDIRECT3DDEVICE3)

static HRESULT STDMETHODCALLTYPE mat1_initialize(IDirect3DMaterial *This, LPDIRECT3D d3d)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE mat1_reserve(IDirect3DMaterial *This)
{
    return E_NOTIMPL;
}

static const IDirect3DMaterialVtbl material1_vtbl = {
    mat1_query, mat1_add_ref, mat1_release, mat1_initialize, mat1_set, mat1_get, mat1_get_handle,
    mat1_reserve, mat1_reserve,
};

static const IDirect3DMaterial2Vtbl material2_vtbl = {
    mat2_query, mat2_add_ref, mat2_release, mat2_set, mat2_get, mat2_get_handle,
};

static const IDirect3DMaterial3Vtbl material3_vtbl = {
    mat3_query, mat3_add_ref, mat3_release, mat3_set, mat3_get, mat3_get_handle,
};
