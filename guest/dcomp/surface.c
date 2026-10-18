/*
 * what a visual can show: surfaces a program draws into, textures it hands
 * over, and the frames of swap chains made for composition. A surface is a
 * texture of the rendering device; drawing hands the texture out and the
 * compositor reads it where it lies.
 */
#include "dcomp_private.h"

static const GUID IID_ID3D11Texture2D_ = {0x6F15AAF2, 0xD208, 0x4E89, {0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C}};

#define MAX_TEXTURE 16384
#define MAX_VIRTUAL 0x1000000

/* ------------------------------------------------------------------ surfaces */

static DcSurface *surf_impl(void *iface) { return CONTAINING_RECORD(iface, DcSurface, iface); }

static void surf_commit(DcObject *object) {}

static void surf_destroy(DcObject *object)
{
    DcSurface *s = CONTAINING_RECORD(object, DcSurface, base);
    if (s->texture)
        s->texture->lpVtbl->Release(s->texture);
    if (s->d3d)
        s->d3d->lpVtbl->Release(s->d3d);
    HeapFree(GetProcessHeap(), 0, s);
}

static const DcClass surf_class = {DC_SURFACE, surf_commit, surf_destroy};

static HRESULT STDMETHODCALLTYPE surf_query(void *This, REFIID riid, void **out)
{
    DcSurface *s = surf_impl(This);
    DcInterface interfaces[] = {{&IID_IDCompositionSurface, This}, {&IID_IDCompositionVirtualSurface, This}};
    return DcQuery(&s->base, interfaces, s->is_virtual ? 2 : 1, riid, out);
}

static ULONG STDMETHODCALLTYPE surf_add_ref(void *This) { return DcObjectAddRef(&surf_impl(This)->base); }
static ULONG STDMETHODCALLTYPE surf_release(void *This) { return DcObjectRelease(&surf_impl(This)->base); }

static HRESULT make_texture(DcSurface *s, UINT width, UINT height)
{
    if (!width || !height) {
        s->texture = nullptr;
        s->texture_width = s->texture_height = 0;
        return S_OK;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = s->format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = s->d3d->lpVtbl->CreateTexture2D(s->d3d, &desc, nullptr, &s->texture);
    if (SUCCEEDED(hr)) {
        s->texture_width = width;
        s->texture_height = height;
    }
    return hr;
}

static BOOL rect_valid(const DcSurface *s, const RECT *r)
{
    return r->left >= 0 && r->right > r->left && (UINT)r->right <= s->width && r->top >= 0 && r->bottom > r->top &&
           (UINT)r->bottom <= s->height;
}

/*
 * drawing hands out the texture and the place in it to draw, the corner of
 * the rectangle: the program draws there and ends the draw, which a
 * suspended one also may.
 */
static HRESULT STDMETHODCALLTYPE surf_begin_draw(void *This, const RECT *rect, REFIID riid, void **object, POINT *offset)
{
    DcSurface *s = surf_impl(This);
    if (!object || !offset)
        return E_INVALIDARG;
    DcDeviceLock(s->base.device);
    HRESULT hr = S_OK;
    RECT whole = {0, 0, (LONG)s->width, (LONG)s->height};
    if (s->state)
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else if (!s->width || !s->height)
        hr = E_ILLEGAL_METHOD_CALL;
    else if (rect && !rect_valid(s, rect))
        hr = E_INVALIDARG;
    else if (rect && !s->is_virtual && s->update.right == 0 && !EqualRect(rect, &whole))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        const RECT *r = rect ? rect : &whole;
        *object = nullptr;
        hr = s->texture->lpVtbl->QueryInterface(s->texture, riid, object);
        if (SUCCEEDED(hr)) {
            offset->x = r->left;
            offset->y = r->top;
            s->update = *r;
            s->state = 1;
        }
    }
    DcDeviceUnlock(s->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surf_end_draw(void *This)
{
    DcSurface *s = surf_impl(This);
    DcDeviceLock(s->base.device);
    HRESULT hr = S_OK;
    if (s->state != 1 && s->state != 2) {
        hr = DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    } else {
        s->state = 0;
        s->base.device->content_changed = TRUE;
    }
    DcDeviceUnlock(s->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surf_suspend_draw(void *This)
{
    DcSurface *s = surf_impl(This);
    DcDeviceLock(s->base.device);
    HRESULT hr = S_OK;
    if (!s->state)
        hr = DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    else if (s->state == 2)
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else
        s->state = 2;
    DcDeviceUnlock(s->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surf_resume_draw(void *This)
{
    DcSurface *s = surf_impl(This);
    DcDeviceLock(s->base.device);
    HRESULT hr = S_OK;
    if (!s->state)
        hr = DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    else if (s->state == 1)
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else
        s->state = 1;
    DcDeviceUnlock(s->base.device);
    return hr;
}

/* moves a part of the surface, through a copy since the two places may overlap. */
static HRESULT STDMETHODCALLTYPE surf_scroll(void *This, const RECT *scroll, const RECT *clip, int dx, int dy)
{
    DcSurface *s = surf_impl(This);
    DcDeviceLock(s->base.device);
    HRESULT hr = S_OK;
    RECT whole = {0, 0, (LONG)s->width, (LONG)s->height};
    if (s->state) {
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    } else if (!s->texture) {
        hr = E_ILLEGAL_METHOD_CALL;
    } else if ((scroll && !rect_valid(s, scroll)) || (clip && !rect_valid(s, clip))) {
        hr = E_INVALIDARG;
    } else {
        RECT from = scroll ? *scroll : whole, to = from, limit = clip ? *clip : whole;
        OffsetRect(&to, dx, dy);
        RECT drawn;
        if (IntersectRect(&drawn, &to, &limit) && IntersectRect(&drawn, &drawn, &whole)) {
            /* the part that stays in the clip, and where it came from. */
            D3D11_BOX box = {(UINT)(drawn.left - dx), (UINT)(drawn.top - dy), 0, (UINT)(drawn.right - dx),
                             (UINT)(drawn.bottom - dy), 1};
            D3D11_TEXTURE2D_DESC desc;
            s->texture->lpVtbl->GetDesc(s->texture, &desc);
            desc.Width = drawn.right - drawn.left;
            desc.Height = drawn.bottom - drawn.top;
            ID3D11Texture2D *temp;
            ID3D11DeviceContext *context;
            hr = s->d3d->lpVtbl->CreateTexture2D(s->d3d, &desc, nullptr, &temp);
            if (SUCCEEDED(hr)) {
                s->d3d->lpVtbl->GetImmediateContext(s->d3d, &context);
                context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource *)temp, 0, 0, 0, 0,
                                                       (ID3D11Resource *)s->texture, 0, &box);
                context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource *)s->texture, 0, drawn.left, drawn.top,
                                                       0, (ID3D11Resource *)temp, 0, nullptr);
                context->lpVtbl->Release(context);
                temp->lpVtbl->Release(temp);
                s->base.device->content_changed = TRUE;
            }
        }
    }
    DcDeviceUnlock(s->base.device);
    return hr;
}

/* a virtual surface grows or shrinks, keeping what both sizes hold. */
static HRESULT STDMETHODCALLTYPE surf_resize(void *This, UINT width, UINT height)
{
    DcSurface *s = surf_impl(This);
    DcDeviceLock(s->base.device);
    HRESULT hr = S_OK;
    if (s->state) {
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    } else if (width > MAX_VIRTUAL || height > MAX_VIRTUAL || (s->base.device->version < 1 && (!width || !height))) {
        hr = E_INVALIDARG;
    } else {
        ID3D11Texture2D *old = s->texture;
        UINT old_width = s->texture_width, old_height = s->texture_height;
        s->texture = nullptr;
        hr = make_texture(s, DC_MIN(width, MAX_TEXTURE), DC_MIN(height, MAX_TEXTURE));
        if (SUCCEEDED(hr)) {
            if (old && s->texture) {
                D3D11_BOX box = {0, 0, 0, DC_MIN(old_width, s->texture_width), DC_MIN(old_height, s->texture_height), 1};
                ID3D11DeviceContext *context;
                s->d3d->lpVtbl->GetImmediateContext(s->d3d, &context);
                context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource *)s->texture, 0, 0, 0, 0,
                                                       (ID3D11Resource *)old, 0, &box);
                context->lpVtbl->Release(context);
            }
            if (old)
                old->lpVtbl->Release(old);
            s->width = width;
            s->height = height;
            s->base.device->content_changed = TRUE;
        } else {
            s->texture = old;
        }
    }
    DcDeviceUnlock(s->base.device);
    return hr;
}

/* the texture stays whole; trimming says only that the program will not read the rectangles back. */
static HRESULT STDMETHODCALLTYPE surf_trim(void *This, const RECT *rectangles, UINT count)
{
    DcSurface *s = surf_impl(This);
    if (count && !rectangles)
        return E_INVALIDARG;
    DcDeviceLock(s->base.device);
    HRESULT hr = s->state ? DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED : S_OK;
    DcDeviceUnlock(s->base.device);
    return hr;
}

static const IDCompositionSurfaceVtbl surface_vtbl = {
    VTBL_FN(surf_query),      VTBL_FN(surf_add_ref),    VTBL_FN(surf_release), VTBL_FN(surf_begin_draw),
    VTBL_FN(surf_end_draw),   VTBL_FN(surf_suspend_draw), VTBL_FN(surf_resume_draw), VTBL_FN(surf_scroll),
};

static const IDCompositionVirtualSurfaceVtbl virtual_vtbl = {
    VTBL_FN(surf_query),      VTBL_FN(surf_add_ref),    VTBL_FN(surf_release), VTBL_FN(surf_begin_draw),
    VTBL_FN(surf_end_draw),   VTBL_FN(surf_suspend_draw), VTBL_FN(surf_resume_draw), VTBL_FN(surf_scroll),
    VTBL_FN(surf_resize),     VTBL_FN(surf_trim),
};

/*
 * the formats and alpha modes surfaces take are dcomp.dll's: unspecified
 * alpha is opaque, an alpha-only surface needs alpha, and a size is both
 * dimensions or, on the third device, none.
 */
HRESULT DcCreateSurface(DcDevice *device, ID3D11Device *d3d, BOOL is_virtual, UINT width, UINT height,
                        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha, void **out)
{
    if (format != DXGI_FORMAT_B8G8R8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        format != DXGI_FORMAT_R16G16B16A16_FLOAT && format != DXGI_FORMAT_A8_UNORM)
        return E_INVALIDARG;
    if (alpha == DXGI_ALPHA_MODE_UNSPECIFIED)
        alpha = DXGI_ALPHA_MODE_IGNORE;
    else if (alpha != DXGI_ALPHA_MODE_PREMULTIPLIED && alpha != DXGI_ALPHA_MODE_IGNORE)
        return E_INVALIDARG;
    if (alpha == DXGI_ALPHA_MODE_IGNORE && format == DXGI_FORMAT_A8_UNORM)
        return E_INVALIDARG;
    if (is_virtual) {
        if (device->version < 1 ? (!width || width > MAX_VIRTUAL || !height || height > MAX_VIRTUAL)
                                : (width > MAX_VIRTUAL || height > MAX_VIRTUAL))
            return E_INVALIDARG;
    } else {
        if ((!width) != (!height) || (!width && device->version < 2) || width > MAX_TEXTURE || height > MAX_TEXTURE)
            return E_INVALIDARG;
    }
    DcSurface *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->iface.lpVtbl = is_virtual ? &virtual_vtbl : (const IDCompositionVirtualSurfaceVtbl *)&surface_vtbl;
    s->is_virtual = is_virtual;
    s->width = width;
    s->height = height;
    s->format = format;
    s->alpha = alpha;
    s->d3d = d3d;
    d3d->lpVtbl->AddRef(d3d);
    HRESULT hr = make_texture(s, DC_MIN(width, MAX_TEXTURE), DC_MIN(height, MAX_TEXTURE));
    if (FAILED(hr)) {
        d3d->lpVtbl->Release(d3d);
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    DcObjectInit(&s->base, device, &surf_class);
    *out = &s->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ surface factories */

static DcFactory *fac_impl(IDCompositionSurfaceFactory *iface) { return CONTAINING_RECORD(iface, DcFactory, iface); }

static void fac_commit(DcObject *object) {}

static void fac_destroy(DcObject *object)
{
    DcFactory *f = CONTAINING_RECORD(object, DcFactory, base);
    f->d3d->lpVtbl->Release(f->d3d);
    HeapFree(GetProcessHeap(), 0, f);
}

static const DcClass fac_class = {DC_FACTORY, fac_commit, fac_destroy};

static HRESULT STDMETHODCALLTYPE fac_query(IDCompositionSurfaceFactory *iface, REFIID riid, void **out)
{
    DcFactory *f = fac_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionSurfaceFactory, iface}};
    return DcQuery(&f->base, interfaces, 1, riid, out);
}

static ULONG STDMETHODCALLTYPE fac_add_ref(IDCompositionSurfaceFactory *iface)
{
    return DcObjectAddRef(&fac_impl(iface)->base);
}

static ULONG STDMETHODCALLTYPE fac_release(IDCompositionSurfaceFactory *iface)
{
    return DcObjectRelease(&fac_impl(iface)->base);
}

static HRESULT STDMETHODCALLTYPE fac_create_surface(IDCompositionSurfaceFactory *iface, UINT width, UINT height,
                                                    DXGI_FORMAT format, DXGI_ALPHA_MODE alpha,
                                                    IDCompositionSurface **out)
{
    DcFactory *f = fac_impl(iface);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateSurface(f->base.device, f->d3d, FALSE, width, height, format, alpha, (void **)out);
}

static HRESULT STDMETHODCALLTYPE fac_create_virtual_surface(IDCompositionSurfaceFactory *iface, UINT width,
                                                            UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha,
                                                            IDCompositionVirtualSurface **out)
{
    DcFactory *f = fac_impl(iface);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return DcCreateSurface(f->base.device, f->d3d, TRUE, width, height, format, alpha, (void **)out);
}

static const IDCompositionSurfaceFactoryVtbl fac_vtbl = {
    fac_query, fac_add_ref, fac_release, fac_create_surface, fac_create_virtual_surface,
};

HRESULT DcCreateSurfaceFactory(DcDevice *device, ID3D11Device *d3d, IDCompositionSurfaceFactory **out)
{
    DcFactory *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->iface.lpVtbl = &fac_vtbl;
    f->d3d = d3d;
    d3d->lpVtbl->AddRef(d3d);
    DcObjectInit(&f->base, device, &fac_class);
    *out = &f->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ textures of the program */

typedef struct DcTexture {
    IDCompositionTexture iface;
    DcObject base;
    ID3D11Texture2D *texture;
    D2D_RECT_U source;
    DXGI_COLOR_SPACE_TYPE color_space;
    DXGI_ALPHA_MODE alpha;
} DcTexture;

static DcTexture *tex_impl(IDCompositionTexture *iface) { return CONTAINING_RECORD(iface, DcTexture, iface); }

static void tex_commit(DcObject *object) {}

static void tex_destroy(DcObject *object)
{
    DcTexture *t = CONTAINING_RECORD(object, DcTexture, base);
    t->texture->lpVtbl->Release(t->texture);
    HeapFree(GetProcessHeap(), 0, t);
}

static const DcClass tex_class = {DC_TEXTURE, tex_commit, tex_destroy};

static HRESULT STDMETHODCALLTYPE tex_query(IDCompositionTexture *iface, REFIID riid, void **out)
{
    DcTexture *t = tex_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionTexture, iface}};
    return DcQuery(&t->base, interfaces, 1, riid, out);
}

static ULONG STDMETHODCALLTYPE tex_add_ref(IDCompositionTexture *iface) { return DcObjectAddRef(&tex_impl(iface)->base); }
static ULONG STDMETHODCALLTYPE tex_release(IDCompositionTexture *iface) { return DcObjectRelease(&tex_impl(iface)->base); }

static HRESULT STDMETHODCALLTYPE tex_set_source_rect(IDCompositionTexture *iface, const D2D_RECT_U *rect)
{
    DcTexture *t = tex_impl(iface);
    D3D11_TEXTURE2D_DESC desc;
    t->texture->lpVtbl->GetDesc(t->texture, &desc);
    if (!rect || rect->left >= rect->right || rect->top >= rect->bottom || rect->right > desc.Width ||
        rect->bottom > desc.Height)
        return E_INVALIDARG;
    DcDeviceLock(t->base.device);
    t->source = *rect;
    t->base.device->content_changed = TRUE;
    DcDeviceUnlock(t->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tex_set_color_space(IDCompositionTexture *iface, DXGI_COLOR_SPACE_TYPE color_space)
{
    DcTexture *t = tex_impl(iface);
    DcDeviceLock(t->base.device);
    t->color_space = color_space;
    DcDeviceUnlock(t->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tex_set_alpha_mode(IDCompositionTexture *iface, DXGI_ALPHA_MODE alpha)
{
    DcTexture *t = tex_impl(iface);
    if (alpha > DXGI_ALPHA_MODE_IGNORE)
        return E_INVALIDARG;
    DcDeviceLock(t->base.device);
    t->alpha = alpha;
    t->base.device->content_changed = TRUE;
    DcDeviceUnlock(t->base.device);
    return S_OK;
}

/* fences are not offered. */
static HRESULT STDMETHODCALLTYPE tex_get_available_fence(IDCompositionTexture *iface, UINT64 *value, REFIID riid,
                                                         void **fence)
{
    if (fence)
        *fence = nullptr;
    return E_NOTIMPL;
}

static const IDCompositionTextureVtbl tex_vtbl = {
    tex_query,          tex_add_ref,         tex_release,
    tex_set_source_rect, tex_set_color_space, tex_set_alpha_mode,
    tex_get_available_fence,
};

HRESULT DcCreateTexture(DcDevice *device, IUnknown *texture, IDCompositionTexture **out)
{
    ID3D11Texture2D *d3d;
    if (FAILED(texture->lpVtbl->QueryInterface(texture, &IID_ID3D11Texture2D_, (void **)&d3d)))
        return E_INVALIDARG;
    DcTexture *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t) {
        d3d->lpVtbl->Release(d3d);
        return E_OUTOFMEMORY;
    }
    D3D11_TEXTURE2D_DESC desc;
    d3d->lpVtbl->GetDesc(d3d, &desc);
    t->iface.lpVtbl = &tex_vtbl;
    t->texture = d3d;
    t->source = (D2D_RECT_U){0, 0, desc.Width, desc.Height};
    t->alpha = DXGI_ALPHA_MODE_PREMULTIPLIED;
    DcObjectInit(&t->base, device, &tex_class);
    *out = &t->iface;
    return S_OK;
}

/* a dynamic texture shows whichever texture it was last given. */
typedef struct DcDynamicTexture {
    IDCompositionDynamicTexture iface;
    DcObject base;
    DcObject *texture;
} DcDynamicTexture;

static DcDynamicTexture *dyn_impl(IDCompositionDynamicTexture *iface)
{
    return CONTAINING_RECORD(iface, DcDynamicTexture, iface);
}

static void dyn_commit(DcObject *object) {}

static void dyn_destroy(DcObject *object)
{
    DcDynamicTexture *d = CONTAINING_RECORD(object, DcDynamicTexture, base);
    if (d->texture)
        DcObjectRelease(d->texture);
    HeapFree(GetProcessHeap(), 0, d);
}

static const DcClass dyn_class = {DC_DYNAMIC_TEXTURE, dyn_commit, dyn_destroy};

static HRESULT STDMETHODCALLTYPE dyn_query(IDCompositionDynamicTexture *iface, REFIID riid, void **out)
{
    DcDynamicTexture *d = dyn_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionDynamicTexture, iface}};
    return DcQuery(&d->base, interfaces, 1, riid, out);
}

static ULONG STDMETHODCALLTYPE dyn_add_ref(IDCompositionDynamicTexture *iface)
{
    return DcObjectAddRef(&dyn_impl(iface)->base);
}

static ULONG STDMETHODCALLTYPE dyn_release(IDCompositionDynamicTexture *iface)
{
    return DcObjectRelease(&dyn_impl(iface)->base);
}

static HRESULT dyn_set(DcDynamicTexture *d, IDCompositionTexture *texture)
{
    DcObject *object = nullptr;
    if (texture) {
        object = DcObjectFrom((IUnknown *)texture);
        if (!object || object->cls->kind != DC_TEXTURE || object->device != d->base.device) {
            if (object)
                DcObjectRelease(object);
            return E_INVALIDARG;
        }
    }
    DcDeviceLock(d->base.device);
    if (d->texture)
        DcObjectRelease(d->texture);
    d->texture = object;
    d->base.device->content_changed = TRUE;
    DcDeviceUnlock(d->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dyn_set_texture_with_rects(IDCompositionDynamicTexture *iface,
                                                            IDCompositionTexture *texture, const D2D_RECT_L *rects,
                                                            SIZE_T count)
{
    if (count && !rects)
        return E_INVALIDARG;
    return dyn_set(dyn_impl(iface), texture);
}

static HRESULT STDMETHODCALLTYPE dyn_set_texture(IDCompositionDynamicTexture *iface, IDCompositionTexture *texture)
{
    return dyn_set(dyn_impl(iface), texture);
}

static const IDCompositionDynamicTextureVtbl dyn_vtbl = {
    dyn_query, dyn_add_ref, dyn_release, dyn_set_texture_with_rects, dyn_set_texture,
};

HRESULT DcCreateDynamicTexture(DcDevice *device, IDCompositionDynamicTexture **out)
{
    DcDynamicTexture *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->iface.lpVtbl = &dyn_vtbl;
    DcObjectInit(&d->base, device, &dyn_class);
    *out = &d->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ surfaces named by handles */

typedef struct DcHandleSurface {
    IUnknown iface;
    DcObject base;
    HANDLE handle;
} DcHandleSurface;

static void hs_commit(DcObject *object) {}

static void hs_destroy(DcObject *object)
{
    DcHandleSurface *h = CONTAINING_RECORD(object, DcHandleSurface, base);
    CloseHandle(h->handle);
    HeapFree(GetProcessHeap(), 0, h);
}

static const DcClass hs_class = {DC_HANDLE_SURFACE, hs_commit, hs_destroy};

static HRESULT STDMETHODCALLTYPE hs_query(IUnknown *iface, REFIID riid, void **out)
{
    DcHandleSurface *h = CONTAINING_RECORD(iface, DcHandleSurface, iface);
    DcInterface interfaces[] = {{&IID_IUnknown, iface}};
    return DcQuery(&h->base, interfaces, 1, riid, out);
}

static ULONG STDMETHODCALLTYPE hs_add_ref(IUnknown *iface)
{
    return DcObjectAddRef(&CONTAINING_RECORD(iface, DcHandleSurface, iface)->base);
}

static ULONG STDMETHODCALLTYPE hs_release(IUnknown *iface)
{
    return DcObjectRelease(&CONTAINING_RECORD(iface, DcHandleSurface, iface)->base);
}

static const IUnknownVtbl hs_vtbl = {hs_query, hs_add_ref, hs_release};

/* the section of DCompositionCreateSurfaceHandle: other handles are objects of another type. */
HRESULT DcCreateSurfaceFromHandle(DcDevice *device, HANDLE handle, IUnknown **out)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return E_HANDLE_HR;
    UINT32 *view = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 16);
    if (!view)
        return E_HANDLE_HR;
    BOOL ours = view[0] == 0x53434D44u;
    UnmapViewOfFile(view);
    if (!ours)
        return E_HANDLE_HR;
    DcHandleSurface *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h)
        return E_OUTOFMEMORY;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &h->handle, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        HeapFree(GetProcessHeap(), 0, h);
        return E_HANDLE_HR;
    }
    h->iface.lpVtbl = &hs_vtbl;
    DcObjectInit(&h->base, device, &hs_class);
    *out = &h->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ content */

DcContent *DcContentRef(DcContent *content)
{
    InterlockedIncrement(&content->refs);
    return content;
}

void DcContentUnref(DcContent *content)
{
    if (InterlockedDecrement(&content->refs))
        return;
    DcDevice *device = content->device;
    DcDeviceLock(device);
    for (DcContent **p = &device->contents; *p; p = &(*p)->next)
        if (*p == content) {
            *p = content->next;
            break;
        }
    DcDeviceUnlock(device);
    if (content->source) {
        content->source->lpVtbl->RemoveListener(content->source, device);
        content->source->lpVtbl->Release(content->source);
    }
    if (content->object)
        DcObjectRelease(content->object);
    if (content->view)
        content->view->lpVtbl->Release(content->view);
    if (content->texture)
        content->texture->lpVtbl->Release(content->texture);
    HeapFree(GetProcessHeap(), 0, content);
}

/*
 * the content a program's object stands for: a surface, texture or dynamic
 * texture of this device, or a swap chain made for composition. Other swap
 * chains and surfaces of handles are taken and show nothing, which S_FALSE
 * says; anything else is not content.
 */
HRESULT DcContentForObject(DcDevice *device, IUnknown *object, DcContent **out)
{
    *out = nullptr;
    DcObject *own = DcObjectFrom(object);
    DcContentKind kind;
    IMimeDXGICompositionSource *source = nullptr;
    void *identity;
    if (own) {
        DcKind k = own->cls->kind;
        if (own->device != device || (k != DC_SURFACE && k != DC_TEXTURE && k != DC_DYNAMIC_TEXTURE &&
                                      k != DC_HANDLE_SURFACE)) {
            DcObjectRelease(own);
            return E_INVALIDARG;
        }
        if (k == DC_HANDLE_SURFACE) {
            DcObjectRelease(own);
            return S_FALSE;
        }
        kind = k == DC_SURFACE ? DCC_SURFACE : k == DC_TEXTURE ? DCC_TEXTURE : DCC_DYNAMIC;
        identity = own;
    } else {
        IDXGISwapChain1 *chain;
        if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IDXGISwapChain1, (void **)&chain)))
            return E_INVALIDARG;
        HRESULT hr = chain->lpVtbl->QueryInterface(chain, &IID_IMimeDXGICompositionSource, (void **)&source);
        chain->lpVtbl->Release(chain);
        if (FAILED(hr))
            return S_FALSE;
        kind = DCC_SWAPCHAIN;
        identity = source;
    }
    for (DcContent *c = device->contents; c; c = c->next)
        if (c->identity == identity) {
            if (own)
                DcObjectRelease(own);
            if (source)
                source->lpVtbl->Release(source);
            *out = DcContentRef(c);
            return S_OK;
        }
    DcContent *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) {
        if (own)
            DcObjectRelease(own);
        if (source)
            source->lpVtbl->Release(source);
        return E_OUTOFMEMORY;
    }
    c->refs = 1;
    c->device = device;
    c->kind = kind;
    c->identity = identity;
    c->source = source;
    c->object = own;
    c->next = device->contents;
    device->contents = c;
    if (source)
        source->lpVtbl->AddListener(source, DcSourceNotify, device);
    *out = c;
    return S_OK;
}

/* the format a texture is read with: typeless ones are read as their plain kind. */
static DXGI_FORMAT plain_format(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:
        return format;
    }
}

/*
 * the frame a content shows now, for the direct3d device that draws: its
 * shader view, which stays good until the next call for the content, and
 * how it is placed. A texture of another device cannot be read.
 */
BOOL DcContentFrame(DcContent *c, ID3D11Device *d3d, DcFrame *f)
{
    memset(f, 0, sizeof(*f));
    ID3D11Texture2D *texture = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    switch (c->kind) {
    case DCC_SWAPCHAIN: {
        MIME_COMPOSITION_FRAME frame;
        if (c->source->lpVtbl->GetFrame(c->source, &frame) != S_OK)
            return FALSE;
        texture = (ID3D11Texture2D *)frame.Texture;
        format = frame.ViewFormat;
        f->width = frame.Width;
        f->height = frame.Height;
        f->source = frame.Source;
        f->alpha = frame.AlphaMode;
        memcpy(f->matrix, &frame.Matrix, sizeof(f->matrix));
        f->has_matrix = frame.Matrix._11 != 1.0f || frame.Matrix._22 != 1.0f || frame.Matrix._31 != 0.0f ||
                        frame.Matrix._32 != 0.0f;
        break;
    }
    case DCC_SURFACE: {
        DcSurface *s = CONTAINING_RECORD(c->object, DcSurface, base);
        if (!s->texture)
            return FALSE;
        texture = s->texture;
        texture->lpVtbl->AddRef(texture);
        format = s->format;
        f->width = s->texture_width;
        f->height = s->texture_height;
        f->source = (RECT){0, 0, (LONG)f->width, (LONG)f->height};
        f->alpha = s->alpha;
        break;
    }
    case DCC_TEXTURE:
    case DCC_DYNAMIC: {
        DcObject *object = c->object;
        if (c->kind == DCC_DYNAMIC) {
            object = CONTAINING_RECORD(object, DcDynamicTexture, base)->texture;
            if (!object)
                return FALSE;
        }
        DcTexture *t = CONTAINING_RECORD(object, DcTexture, base);
        texture = t->texture;
        texture->lpVtbl->AddRef(texture);
        D3D11_TEXTURE2D_DESC desc;
        texture->lpVtbl->GetDesc(texture, &desc);
        format = plain_format(desc.Format);
        f->width = desc.Width;
        f->height = desc.Height;
        f->source = (RECT){(LONG)t->source.left, (LONG)t->source.top, (LONG)t->source.right, (LONG)t->source.bottom};
        f->alpha = t->alpha;
        break;
    }
    }
    ID3D11Device *owner;
    texture->lpVtbl->GetDevice(texture, &owner);
    owner->lpVtbl->Release(owner);
    BOOL ok = owner == d3d;
    if (ok && (c->texture != (IUnknown *)texture || !c->view || c->view_format != format)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView *view;
        if (SUCCEEDED(d3d->lpVtbl->CreateShaderResourceView(d3d, (ID3D11Resource *)texture, &desc, &view))) {
            if (c->view)
                c->view->lpVtbl->Release(c->view);
            if (c->texture)
                c->texture->lpVtbl->Release(c->texture);
            c->view = view;
            c->texture = (IUnknown *)texture;
            c->view_format = format;
            texture->lpVtbl->AddRef(texture);
        } else {
            ok = FALSE;
        }
    }
    texture->lpVtbl->Release(texture);
    if (ok)
        f->view = c->view;
    return ok;
}
