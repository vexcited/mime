/*
 * devices and device contexts: bitmaps with options, color contexts, effects and images, the later interfaces of the
 * context that this library answers or fails.
 */
#include "d2d_private.h"
#include <float.h>
#include <stddef.h>

#define RT D2dRt *rt = ctx_rt(This)

extern const void *const d2d_rt_thunks[];
static const void *g_ctx_vtbl[140];
static const void *g_ctx_sec_vtbl[140];

extern const void *const d2d_rt_base[];
extern const void *const d2d_gdi_interop_vtbl[];
extern HRESULT D2dRtCreateBitmap(D2dRt *rt, D2D1_SIZE_U size, const void *data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1 *p1,
                                 const D2D1_BITMAP_PROPERTIES *p0, BOOL v1, ID2D1Bitmap1 **out);
extern HRESULT D2dRtBitmapFromWic(D2dRt *rt, IWICBitmapSource *source, const D2D1_BITMAP_PROPERTIES1 *p1,
                                  const D2D1_BITMAP_PROPERTIES *p0, ID2D1Bitmap1 **out);
extern HRESULT D2dCommandListSurface(ID2D1CommandList *list, D2dSurface **surface, float *dpi);

/* the object of an interface pointer, which is either the object itself or its second vtable. */
static D2dRt *ctx_rt(void *This)
{
    if (*(const void *const *)This == (const void *)g_ctx_sec_vtbl)
        return (D2dRt *)((char *)This - offsetof(D2dRt, ctx_vtbl));
    return (D2dRt *)This;
}

/* the object behind a pointer to the second vtable of a target, or nullptr for any other pointer. */
D2dRt *D2dRtFromContextPtr(void *p)
{
    if (p && *(const void *const *)p == (const void *)g_ctx_sec_vtbl)
        return (D2dRt *)((char *)p - offsetof(D2dRt, ctx_vtbl));
    return nullptr;
}

/* ------------------------------------------------------------------ the device */

typedef struct D2dDevice {
    D2dObj obj;
    IDXGIDevice *dxgi;
    UINT64 max_texture_memory;
    D2D1_RENDERING_PRIORITY priority;
    UINT64 glyph_cache;
} D2dDevice;

HRESULT D2dContextCreate(D2dDevice *device, D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext **out);

#define DEV D2dDevice *d = (D2dDevice *)This

static HRESULT STDMETHODCALLTYPE dev_create_context(ID2D1Device *This, D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext **out)
{
    DEV;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (options & ~1u)
        return E_INVALIDARG;
    return D2dContextCreate(d, options, out);
}
static HRESULT STDMETHODCALLTYPE dev_print_control(ID2D1Device *This, IWICImagingFactory *f, void *job, const D2D1_PRINT_CONTROL_PROPERTIES *p,
                                                   ID2D1PrintControl **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static void STDMETHODCALLTYPE dev_set_memory(ID2D1Device *This, UINT64 m) { DEV; d->max_texture_memory = m; }
static UINT64 STDMETHODCALLTYPE dev_get_memory(ID2D1Device *This) { DEV; return d->max_texture_memory; }
static void STDMETHODCALLTYPE dev_clear(ID2D1Device *This, UINT32 ms) {}
static D2D1_RENDERING_PRIORITY STDMETHODCALLTYPE dev_get_priority(ID2D1Device1 *This) { DEV; return d->priority; }
static void STDMETHODCALLTYPE dev_set_priority(ID2D1Device1 *This, D2D1_RENDERING_PRIORITY p) { DEV; d->priority = p; }
static void STDMETHODCALLTYPE dev_flush(ID2D1Device2 *This, ID2D1Bitmap *bitmap) {}
static HRESULT STDMETHODCALLTYPE dev_get_dxgi(ID2D1Device2 *This, IDXGIDevice **out)
{
    DEV;
    *out = d->dxgi;
    D2D_ADDREF(*out);
    return S_OK;
}
static void STDMETHODCALLTYPE dev_set_glyph_cache(ID2D1Device4 *This, UINT64 s) { DEV; d->glyph_cache = s; }
static UINT64 STDMETHODCALLTYPE dev_get_glyph_cache(ID2D1Device4 *This) { DEV; return d->glyph_cache; }

static void dev_destroy(D2dObj *o) { D2D_RELEASE(((D2dDevice *)o)->dxgi); }

/* each device generation only changes the context it makes, and the context answers every query. */
static const void *const g_dev_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, dev_create_context,
                                         dev_print_control, dev_set_memory, dev_get_memory, dev_clear, dev_get_priority,
                                         dev_set_priority, dev_create_context, dev_create_context, dev_flush, dev_get_dxgi,
                                         dev_create_context, dev_create_context, dev_set_glyph_cache, dev_get_glyph_cache,
                                         dev_create_context, dev_create_context};
D2D_IIDS(g_dev_iids, &IID_ID2D1Resource, &IID_ID2D1Device, &IID_ID2D1Device1, &IID_ID2D1Device2, &IID_ID2D1Device3,
         &IID_ID2D1Device4, &IID_ID2D1Device5, &IID_ID2D1Device6);
static const D2dClass g_dev_class = {"device", g_dev_iids, dev_destroy, nullptr};

HRESULT D2dDeviceCreate(D2dFactory *f, IDXGIDevice *dxgi, ID2D1Device **out)
{
    ID3D11Device *d3d = nullptr;
    HRESULT hr = dxgi->lpVtbl->QueryInterface(dxgi, &IID_ID3D11Device, (void **)&d3d);
    if (FAILED(hr))
        return hr;
    d3d->lpVtbl->Release(d3d);
    D2dDevice *d = D2dNew(sizeof(*d), g_dev_vtbl, &g_dev_class, f);
    if (!d)
        return E_OUTOFMEMORY;
    d->dxgi = dxgi;
    D2D_ADDREF(dxgi);
    d->max_texture_memory = 64ull << 20;
    *out = (ID2D1Device *)d;
    return S_OK;
}

D2D1API HRESULT WINAPI D2D1CreateDevice(IDXGIDevice *dxgi, const D2D1_CREATION_PROPERTIES *props, ID2D1Device **device)
{
    if (!device)
        return E_POINTER;
    *device = nullptr;
    if (!dxgi)
        return E_INVALIDARG;
    D2D1_FACTORY_TYPE type;
    D2D1_FACTORY_OPTIONS options = {D2D1_DEBUG_LEVEL_NONE};
    if (props) {
        if (props->threadingMode > D2D1_THREADING_MODE_MULTI_THREADED)
            return E_INVALIDARG;
        options.debugLevel = props->debugLevel;
        type = props->threadingMode == D2D1_THREADING_MODE_MULTI_THREADED ? D2D1_FACTORY_TYPE_MULTI_THREADED : D2D1_FACTORY_TYPE_SINGLE_THREADED;
    } else {
        ID3D11Device *d3d = nullptr;
        HRESULT hr = dxgi->lpVtbl->QueryInterface(dxgi, &IID_ID3D11Device, (void **)&d3d);
        if (FAILED(hr))
            return hr;
        UINT flags = d3d->lpVtbl->GetCreationFlags(d3d);
        d3d->lpVtbl->Release(d3d);
        type = (flags & 1) ? D2D1_FACTORY_TYPE_SINGLE_THREADED : D2D1_FACTORY_TYPE_MULTI_THREADED;
    }
    ID2D1Factory1 *f = nullptr;
    HRESULT hr = D2D1CreateFactory(type, &IID_ID2D1Factory1, &options, (void **)&f);
    if (FAILED(hr))
        return hr;
    hr = f->lpVtbl->CreateDevice(f, dxgi, device);
    f->lpVtbl->Release(f);
    return hr;
}

D2D1API HRESULT WINAPI D2D1CreateDeviceContext(IDXGISurface *surface, const D2D1_CREATION_PROPERTIES *props, ID2D1DeviceContext **context)
{
    if (!context)
        return E_POINTER;
    *context = nullptr;
    if (!surface)
        return E_INVALIDARG;
    IDXGIDevice *dxgi = nullptr;
    HRESULT hr = surface->lpVtbl->GetDevice(surface, &IID_IDXGIDevice, (void **)&dxgi);
    if (FAILED(hr))
        return hr;
    ID2D1Device *device = nullptr;
    hr = D2D1CreateDevice(dxgi, props, &device);
    dxgi->lpVtbl->Release(dxgi);
    if (FAILED(hr))
        return hr;
    ID2D1DeviceContext *ctx = nullptr;
    hr = device->lpVtbl->CreateDeviceContext(device, props ? props->options : 0, &ctx);
    device->lpVtbl->Release(device);
    if (FAILED(hr))
        return hr;
    ID2D1Bitmap1 *bitmap = nullptr;
    hr = ctx->lpVtbl->CreateBitmapFromDxgiSurface(ctx, surface, nullptr, &bitmap);
    if (SUCCEEDED(hr)) {
        ctx->lpVtbl->SetTarget(ctx, (ID2D1Image *)bitmap);
        bitmap->lpVtbl->Release(bitmap);
        *context = ctx;
        return S_OK;
    }
    ctx->lpVtbl->Release(ctx);
    return hr;
}

/* ------------------------------------------------------------------ the context */

D2D_IIDS(g_ctx_iids, &IID_ID2D1Resource, &IID_ID2D1RenderTarget, &IID_ID2D1DeviceContext, &IID_ID2D1DeviceContext1,
         &IID_ID2D1DeviceContext2, &IID_ID2D1DeviceContext3, &IID_ID2D1DeviceContext4, &IID_ID2D1DeviceContext5,
         &IID_ID2D1DeviceContext6);

static void ctx_destroy(D2dObj *o) { D2dRtDestroyContents((D2dRt *)o); }
static const D2dClass g_ctx_class = {"device context", g_ctx_iids, ctx_destroy, nullptr};

BOOL D2dIsContextClass(const D2dClass *c) { return c == &g_ctx_class; }

HRESULT D2dBitmapFromSurface(D2dRt *rt, IDXGISurface *surface, const D2D1_BITMAP_PROPERTIES1 *props, ID2D1Bitmap1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!surface)
        return E_INVALIDARG;
    DXGI_SURFACE_DESC desc;
    HRESULT hr = surface->lpVtbl->GetDesc(surface, &desc);
    if (FAILED(hr))
        return hr;
    D2dFormat fmt;
    BOOL srgb;
    if (!D2dFormatFromDxgi(desc.Format, &fmt, &srgb))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    D2D1_BITMAP_OPTIONS options = D2D1_BITMAP_OPTIONS_NONE;
    float dx = rt->dpi_x, dy = rt->dpi_y;
    D2D1_ALPHA_MODE alpha = D2dDefaultAlpha(desc.Format);
    ID3D11Texture2D *tex = nullptr;
    if (SUCCEEDED(surface->lpVtbl->QueryInterface(surface, &IID_ID3D11Texture2D, (void **)&tex))) {
        D3D11_TEXTURE2D_DESC td;
        tex->lpVtbl->GetDesc(tex, &td);
        if (td.BindFlags & D3D11_BIND_RENDER_TARGET)
            options |= D2D1_BITMAP_OPTIONS_TARGET;
        tex->lpVtbl->Release(tex);
    }
    if (props) {
        options = props->bitmapOptions;
        if (props->dpiX > 0)
            dx = props->dpiX;
        if (props->dpiY > 0)
            dy = props->dpiY;
        if (props->pixelFormat.alphaMode != D2D1_ALPHA_MODE_UNKNOWN)
            alpha = props->pixelFormat.alphaMode;
        if ((int)props->pixelFormat.format != 0 && props->pixelFormat.format != desc.Format)
            return D2DERR_UNSUPPORTED_PIXEL_FORMAT_HR;
    }
    D2dBitmap *b;
    hr = D2dBitmapCreate(rt->obj.factory, desc.Width, desc.Height, desc.Format, alpha, dx, dy, options, TRUE, &b);
    if (FAILED(hr))
        return hr;
    b->surface = surface;
    D2D_ADDREF(surface);
    D2dBitmapReadSurface(b);
    *out = (ID2D1Bitmap1 *)b;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cx_create_bitmap(ID2D1DeviceContext6 *This, D2D1_SIZE_U size, const void *data, UINT32 pitch,
                                                  const D2D1_BITMAP_PROPERTIES1 *p, ID2D1Bitmap1 **out)
{
    RT;
    if (p && (p->bitmapOptions & ~15u))
        return E_INVALIDARG;
    if (p && (p->bitmapOptions & D2D1_BITMAP_OPTIONS_CPU_READ) && (p->bitmapOptions & (D2D1_BITMAP_OPTIONS_TARGET | 0)) &&
        (p->bitmapOptions & D2D1_BITMAP_OPTIONS_TARGET))
        return E_INVALIDARG;
    return D2dRtCreateBitmap(rt, size, data, pitch, p, nullptr, TRUE, out);
}
static HRESULT STDMETHODCALLTYPE cx_bitmap_from_wic(ID2D1DeviceContext6 *This, IWICBitmapSource *s, const D2D1_BITMAP_PROPERTIES1 *p,
                                                    ID2D1Bitmap1 **out)
{
    RT;
    return D2dRtBitmapFromWic(rt, s, p, nullptr, out);
}
static HRESULT STDMETHODCALLTYPE cx_color_context(ID2D1DeviceContext6 *This, D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 size,
                                                  ID2D1ColorContext **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return D2dCreateColorContext(rt->obj.factory, space, profile, size, out);
}
static HRESULT STDMETHODCALLTYPE cx_color_context_file(ID2D1DeviceContext6 *This, const WCHAR *path, ID2D1ColorContext **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!path)
        return E_INVALIDARG;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD size = GetFileSize(h, nullptr), got = 0;
    BYTE *data = D2dAlloc(size ? size : 1);
    HRESULT hr = E_OUTOFMEMORY;
    if (data) {
        hr = ReadFile(h, data, size, &got, nullptr) ? D2dCreateColorContext(rt->obj.factory, D2D1_COLOR_SPACE_CUSTOM, data, got, out) : HRESULT_FROM_WIN32(GetLastError());
        D2dFree(data);
    }
    CloseHandle(h);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cx_color_context_wic(ID2D1DeviceContext6 *This, IWICColorContext *wic, ID2D1ColorContext **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!wic)
        return E_INVALIDARG;
    WICColorContextType type;
    HRESULT hr = wic->lpVtbl->GetType(wic, &type);
    if (FAILED(hr))
        return hr;
    if (type == WICColorContextProfile) {
        UINT size = 0;
        wic->lpVtbl->GetProfileBytes(wic, 0, nullptr, &size);
        BYTE *data = D2dAlloc(size ? size : 1);
        if (!data)
            return E_OUTOFMEMORY;
        hr = wic->lpVtbl->GetProfileBytes(wic, size, data, &size);
        if (SUCCEEDED(hr))
            hr = D2dCreateColorContext(rt->obj.factory, D2D1_COLOR_SPACE_CUSTOM, data, size, out);
        D2dFree(data);
        return hr;
    }
    UINT exif = 0;
    wic->lpVtbl->GetExifColorSpace(wic, &exif);
    return D2dCreateColorContext(rt->obj.factory, exif == 1 ? D2D1_COLOR_SPACE_SRGB : D2D1_COLOR_SPACE_SCRGB, nullptr, 0, out);
}
static HRESULT STDMETHODCALLTYPE cx_bitmap_from_surface(ID2D1DeviceContext6 *This, IDXGISurface *s, const D2D1_BITMAP_PROPERTIES1 *p,
                                                        ID2D1Bitmap1 **out)
{
    RT;
    return D2dBitmapFromSurface(rt, s, p, out);
}
static HRESULT STDMETHODCALLTYPE cx_create_effect(ID2D1DeviceContext6 *This, REFCLSID clsid, ID2D1Effect **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!clsid)
        return E_INVALIDARG;
    return D2dCreateEffect(rt, clsid, out);
}
static HRESULT STDMETHODCALLTYPE cx_gradient1(ID2D1DeviceContext6 *This, const D2D1_GRADIENT_STOP *stops, UINT32 count,
                                              D2D1_COLOR_SPACE pre, D2D1_COLOR_SPACE post, D2D1_BUFFER_PRECISION precision,
                                              D2D1_EXTEND_MODE extend, D2D1_COLOR_INTERPOLATION_MODE mode,
                                              ID2D1GradientStopCollection1 **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (pre > D2D1_COLOR_SPACE_SCRGB || post > D2D1_COLOR_SPACE_SCRGB || pre == D2D1_COLOR_SPACE_CUSTOM || post == D2D1_COLOR_SPACE_CUSTOM || mode > D2D1_COLOR_INTERPOLATION_MODE_PREMULTIPLIED)
        return E_INVALIDARG;
    D2dGradient *g;
    HRESULT hr = D2dCreateGradient(rt->obj.factory, stops, count, D2D1_GAMMA_2_2, extend, TRUE, &g);
    if (FAILED(hr))
        return hr;
    g->pre = pre;
    g->post = post;
    g->precision = precision;
    g->mode = mode;
    *out = (ID2D1GradientStopCollection1 *)g;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_image_brush(ID2D1DeviceContext6 *This, ID2D1Image *image, const D2D1_IMAGE_BRUSH_PROPERTIES *ip,
                                                const D2D1_BRUSH_PROPERTIES *bp, ID2D1ImageBrush **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return D2dCreateImageBrush(rt->obj.factory, image, ip, bp, out);
}
static HRESULT STDMETHODCALLTYPE cx_bitmap_brush1(ID2D1DeviceContext6 *This, ID2D1Bitmap *bm, const D2D1_BITMAP_BRUSH_PROPERTIES1 *bp,
                                                  const D2D1_BRUSH_PROPERTIES *p, ID2D1BitmapBrush1 **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!bm)
        return E_INVALIDARG;
    return D2dCreateBitmapBrush(rt->obj.factory, bm, bp, p, TRUE, out);
}
static HRESULT STDMETHODCALLTYPE cx_command_list(ID2D1DeviceContext6 *This, ID2D1CommandList **out)
{
    RT;
    if (!out)
        return E_POINTER;
    return D2dCreateCommandList(rt, out);
}
static BOOL STDMETHODCALLTYPE cx_dxgi_supported(ID2D1DeviceContext6 *This, DXGI_FORMAT f)
{
    D2dFormat fmt;
    BOOL srgb;
    return D2dFormatFromDxgi(f, &fmt, &srgb);
}
static BOOL STDMETHODCALLTYPE cx_precision_supported(ID2D1DeviceContext6 *This, D2D1_BUFFER_PRECISION p)
{
    return p >= D2D1_BUFFER_PRECISION_8BPC_UNORM && p <= D2D1_BUFFER_PRECISION_32BPC_FLOAT;
}
static HRESULT STDMETHODCALLTYPE cx_local_bounds(ID2D1DeviceContext6 *This, ID2D1Image *image, D2D1_RECT_F *b)
{
    RT;
    if (!b)
        return E_POINTER;
    if (!image)
        return E_INVALIDARG;
    return D2dImageBounds(rt, image, b);
}
static HRESULT STDMETHODCALLTYPE cx_world_bounds(ID2D1DeviceContext6 *This, ID2D1Image *image, D2D1_RECT_F *b)
{
    RT;
    if (!b)
        return E_POINTER;
    if (!image)
        return E_INVALIDARG;
    HRESULT hr = D2dImageBounds(rt, image, b);
    if (FAILED(hr))
        return hr;
    D2dMatrix w = rt->state.transform;
    D2D1_POINT_2F c[4] = {D2dMatApply(&w, b->left, b->top), D2dMatApply(&w, b->right, b->top), D2dMatApply(&w, b->right, b->bottom),
                          D2dMatApply(&w, b->left, b->bottom)};
    D2D1_RECT_F r = {c[0].x, c[0].y, c[0].x, c[0].y};
    for (int i = 1; i < 4; i++) {
        r.left = D2dMinF(r.left, c[i].x);
        r.top = D2dMinF(r.top, c[i].y);
        r.right = D2dMaxF(r.right, c[i].x);
        r.bottom = D2dMaxF(r.bottom, c[i].y);
    }
    *b = r;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_glyph_bounds(ID2D1DeviceContext6 *This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,
                                                 DWRITE_MEASURING_MODE mm, D2D1_RECT_F *b)
{
    RT;
    if (!run || !b || !run->fontFace)
        return E_INVALIDARG;
    D2dPath path = {};
    HRESULT hr = D2dGlyphRunToPath(run->fontFace, run->fontEmSize, run->glyphIndices, run->glyphAdvances, run->glyphOffsets, run->glyphCount,
                                   run->isSideways, run->bidiLevel & 1, &path);
    if (FAILED(hr)) {
        D2dPathFree(&path);
        return hr;
    }
    D2dMatrix m = rt->state.transform;
    D2dMatrix t = {1, 0, 0, 1, origin.x, origin.y};
    D2dMatMul(&m, &t, &m);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    D2dPathFlatten(&path, &m, 0.25f, &polys, FALSE);
    D2D1_RECT_F r = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (UINT32 i = 0; i < polys.npts; i++) {
        r.left = D2dMinF(r.left, polys.pts[i].x);
        r.top = D2dMinF(r.top, polys.pts[i].y);
        r.right = D2dMaxF(r.right, polys.pts[i].x);
        r.bottom = D2dMaxF(r.bottom, polys.pts[i].y);
    }
    *b = r;
    mime_vec_polys_free(&polys);
    D2dPathFree(&path);
    return S_OK;
}
static void STDMETHODCALLTYPE cx_get_device(ID2D1DeviceContext6 *This, ID2D1Device **out)
{
    RT;
    *out = rt->device;
    D2D_ADDREF(*out);
}

static void set_target_state(D2dRt *rt)
{
    D2dBitmap *b = rt->target_bitmap;
    if (!b) {
        rt->draw = nullptr;
        rt->pixel_w = rt->pixel_h = 0;
        rt->pixel_format.format = (DXGI_FORMAT)0;
        rt->pixel_format.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
        return;
    }
    rt->dpi_x = b->dpi_x;
    rt->dpi_y = b->dpi_y;
    rt->pixel_w = b->width;
    rt->pixel_h = b->height;
    rt->pixel_format.format = b->dxgi;
    rt->pixel_format.alphaMode = b->alpha;
    rt->format = b->format;
    rt->srgb = b->srgb;
    rt->draw = (D2dSurface *)D2dBitmapView(b);
}

static void STDMETHODCALLTYPE cx_set_target(ID2D1DeviceContext6 *This, ID2D1Image *target)
{
    RT;
    if (rt->drawing) {
        rt->error = rt->error ? rt->error : D2DERR_WRONG_STATE;
        return;
    }
    D2dBitmap *b = nullptr;
    if (target) {
        b = D2dBitmapOf(target);
        if (b) {
            if (!(b->options & D2D1_BITMAP_OPTIONS_TARGET) || b->obj.factory != rt->obj.factory)
                return;
        } else {
            D2dSurface *s;
            float dpi;
            if (FAILED(D2dCommandListSurface((ID2D1CommandList *)target, &s, &dpi)))
                return;
            D2D_RELEASE(rt->target_bitmap);
            D2D_ADDREF(target);
            D2D_RELEASE(rt->target_image);
            rt->target_image = target;
            rt->draw = s;
            rt->pixel_w = (UINT32)s->w;
            rt->pixel_h = (UINT32)s->h;
            rt->dpi_x = rt->dpi_y = dpi;
            rt->pixel_format.format = (DXGI_FORMAT)87;
            rt->pixel_format.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            return;
        }
    }
    D2D_ADDREF(b);
    D2D_RELEASE(rt->target_bitmap);
    D2D_RELEASE(rt->target_image);
    rt->target_bitmap = b;
    set_target_state(rt);
}
static void STDMETHODCALLTYPE cx_get_target(ID2D1DeviceContext6 *This, ID2D1Image **out)
{
    RT;
    *out = rt->target_bitmap ? (ID2D1Image *)rt->target_bitmap : rt->target_image;
    D2D_ADDREF(*out);
}
static void STDMETHODCALLTYPE cx_set_controls(ID2D1DeviceContext6 *This, const D2D1_RENDERING_CONTROLS *c)
{
    RT;
    if (c)
        rt->controls = *c;
}
static void STDMETHODCALLTYPE cx_get_controls(ID2D1DeviceContext6 *This, D2D1_RENDERING_CONTROLS *c)
{
    RT;
    *c = rt->controls;
}
static void STDMETHODCALLTYPE cx_set_blend(ID2D1DeviceContext6 *This, D2D1_PRIMITIVE_BLEND b)
{
    RT;
    if (b <= D2D1_PRIMITIVE_BLEND_MAX)
        rt->state.blend = b;
    else
        rt->error = rt->error ? rt->error : E_INVALIDARG;
}
static D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE cx_get_blend(ID2D1DeviceContext6 *This)
{
    RT;
    return rt->state.blend;
}
static void STDMETHODCALLTYPE cx_set_unit(ID2D1DeviceContext6 *This, D2D1_UNIT_MODE m)
{
    RT;
    if (m <= D2D1_UNIT_MODE_PIXELS)
        rt->state.units = m;
}
static D2D1_UNIT_MODE STDMETHODCALLTYPE cx_get_unit(ID2D1DeviceContext6 *This)
{
    RT;
    return rt->state.units;
}
static void STDMETHODCALLTYPE cx_glyph_run1(ID2D1DeviceContext6 *This, D2D1_POINT_2F o, const DWRITE_GLYPH_RUN *run,
                                            const DWRITE_GLYPH_RUN_DESCRIPTION *d, ID2D1Brush *brush, DWRITE_MEASURING_MODE mm)
{
    RT;
    D2dDrawGlyphRun(rt, o, run, brush, mm);
}
static void STDMETHODCALLTYPE cx_draw_image(ID2D1DeviceContext6 *This, ID2D1Image *image, const D2D1_POINT_2F *offset,
                                            const D2D1_RECT_F *source, D2D1_INTERPOLATION_MODE mode, UINT32 composite)
{
    RT;
    if (!rt->drawing) {
        rt->error = D2DERR_WRONG_STATE;
        return;
    }
    if (!image) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    D2dDrawImageCore(rt, image, offset, source, mode, composite);
}
static void STDMETHODCALLTYPE cx_draw_metafile(ID2D1DeviceContext6 *This, ID2D1GdiMetafile *mf, const D2D1_POINT_2F *offset)
{
    RT;
    rt->error = rt->error ? rt->error : D2DERR_UNSUPPORTED_OPERATION;
}
static void STDMETHODCALLTYPE cx_draw_bitmap1(ID2D1DeviceContext6 *This, ID2D1Bitmap *bm, const D2D1_RECT_F *dest, FLOAT opacity,
                                              D2D1_INTERPOLATION_MODE mode, const D2D1_RECT_F *source, const D2D1_MATRIX_4X4_F *persp)
{
    RT;
    D2dBitmap *b = D2dBitmapOf(bm);
    if (!b) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    if (persp) {
        /* only an affine perspective matrix is applied, the rest cannot be drawn. */
        if (persp->_14 != 0 || persp->_24 != 0 || persp->_34 != 0 || persp->_44 != 1) {
            rt->error = rt->error ? rt->error : D2DERR_UNSUPPORTED_OPERATION;
            return;
        }
        D2dMatrix m = {persp->_11, persp->_12, persp->_21, persp->_22, persp->_41, persp->_42};
        D2dDrawBitmapRect(rt, b, dest, opacity, mode, source, &m);
        return;
    }
    D2dDrawBitmapRect(rt, b, dest, opacity, mode, source, nullptr);
}
static void STDMETHODCALLTYPE cx_push_layer1(ID2D1DeviceContext6 *This, const D2D1_LAYER_PARAMETERS1 *p, ID2D1Layer *layer)
{
    RT;
    if (!p) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    D2dPushLayer(rt, p);
}
static HRESULT STDMETHODCALLTYPE cx_invalidate(ID2D1DeviceContext6 *This, ID2D1Effect *e, UINT32 input, const D2D1_RECT_F *r) { return S_OK; }
static HRESULT STDMETHODCALLTYPE cx_invalid_count(ID2D1DeviceContext6 *This, ID2D1Effect *e, UINT32 *count)
{
    *count = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_invalid_rects(ID2D1DeviceContext6 *This, ID2D1Effect *e, D2D1_RECT_F *rects, UINT32 count)
{
    return count ? D2DERR_INSUFFICIENT_BUFFER_HR : S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_required_rects(ID2D1DeviceContext6 *This, ID2D1Effect *e, const D2D1_RECT_F *out_rect,
                                                   const D2D1_EFFECT_INPUT_DESCRIPTION *inputs, D2D1_RECT_F *required, UINT32 count)
{
    for (UINT32 i = 0; i < count; i++)
        required[i] = *out_rect;
    return S_OK;
}
static void STDMETHODCALLTYPE cx_fill_mask1(ID2D1DeviceContext6 *This, ID2D1Bitmap *mask, ID2D1Brush *brush, const D2D1_RECT_F *dest,
                                            const D2D1_RECT_F *source)
{
    RT;
    D2dBitmap *m = D2dBitmapOf(mask);
    if (!m) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    D2dFillOpacityMask(rt, m, brush, dest, source);
}

/* the geometry realizations are the geometry and its settings, drawn each time. */
typedef struct D2dRealization {
    D2dObj obj;
    ID2D1Geometry *geometry;
    BOOL stroked;
    float width;
    ID2D1StrokeStyle *style;
} D2dRealization;
static void real_destroy(D2dObj *o)
{
    D2dRealization *r = (D2dRealization *)o;
    D2D_RELEASE(r->geometry);
    D2D_RELEASE(r->style);
}
static const void *const g_real_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory};
D2D_IIDS(g_real_iids, &IID_ID2D1Resource, &IID_ID2D1GeometryRealization);
static const D2dClass g_real_class = {"geometry realization", g_real_iids, real_destroy, nullptr};

static HRESULT make_real(D2dRt *rt, ID2D1Geometry *g, BOOL stroked, float width, ID2D1StrokeStyle *style, ID2D1GeometryRealization **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!D2dGeometryOf(g))
        return E_INVALIDARG;
    D2dRealization *r = D2dNew(sizeof(*r), g_real_vtbl, &g_real_class, rt->obj.factory);
    if (!r)
        return E_OUTOFMEMORY;
    r->geometry = g;
    D2D_ADDREF(g);
    r->stroked = stroked;
    r->width = width;
    r->style = style;
    D2D_ADDREF(style);
    *out = (ID2D1GeometryRealization *)r;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_filled_real(ID2D1DeviceContext6 *This, ID2D1Geometry *g, FLOAT tol, ID2D1GeometryRealization **out)
{
    RT;
    return make_real(rt, g, FALSE, 0, nullptr, out);
}
static HRESULT STDMETHODCALLTYPE cx_stroked_real(ID2D1DeviceContext6 *This, ID2D1Geometry *g, FLOAT tol, FLOAT w, ID2D1StrokeStyle *s,
                                                 ID2D1GeometryRealization **out)
{
    RT;
    return make_real(rt, g, TRUE, w, s, out);
}
static void STDMETHODCALLTYPE cx_draw_real(ID2D1DeviceContext6 *This, ID2D1GeometryRealization *real, ID2D1Brush *brush)
{
    RT;
    if (!D2dIsOfClass(real, &g_real_class)) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    D2dRealization *r = (D2dRealization *)real;
    if (r->stroked)
        D2dDrawGeometry(rt, r->geometry, brush, r->width, r->style);
    else
        D2dFillGeometry(rt, r->geometry, brush, nullptr);
}

/* what has no renderer here. creating those objects fails and drawing them is an error of the target. */
static HRESULT STDMETHODCALLTYPE cx_nocreate(ID2D1DeviceContext6 *This, void *a, void *b, void *c) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE cx_nocreate1(ID2D1DeviceContext6 *This, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE cx_inks(ID2D1DeviceContext6 *This, const void *p, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE cx_mesh_bounds(ID2D1DeviceContext6 *This, void *mesh, D2D1_RECT_F *r) { return E_NOTIMPL; }
static void STDMETHODCALLTYPE cx_unsupported_draw(ID2D1DeviceContext6 *This)
{
    RT;
    rt->error = rt->error ? rt->error : D2DERR_UNSUPPORTED_OPERATION;
}
static HRESULT STDMETHODCALLTYPE cx_lookup(ID2D1DeviceContext6 *This, UINT32 p, const UINT32 *e, const BYTE *d, UINT32 s, const UINT32 *st, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE cx_image_dxgi(ID2D1DeviceContext6 *This, IDXGISurface *const *s, UINT32 n, D2D1_COLOR_SPACE sp, UINT32 o, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE cx_image_wic(ID2D1DeviceContext6 *This, IWICBitmapSource *s, UINT32 o, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE cx_nocreate_two(ID2D1DeviceContext6 *This, void *a, void *b, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static void STDMETHODCALLTYPE cx_draw_text1(ID2D1DeviceContext6 *This, const WCHAR *s, UINT32 len, IDWriteTextFormat *f, const D2D1_RECT_F *r,
                                            ID2D1Brush *brush, ID2D1SvgGlyphStyle *style, UINT32 palette, D2D1_DRAW_TEXT_OPTIONS o,
                                            DWRITE_MEASURING_MODE mm)
{
    RT;
    D2dDrawText(rt, s, len, f, r, brush, o, mm);
}
static void STDMETHODCALLTYPE cx_draw_layout1(ID2D1DeviceContext6 *This, D2D1_POINT_2F origin, IDWriteTextLayout *l, ID2D1Brush *brush,
                                              ID2D1SvgGlyphStyle *style, UINT32 palette, D2D1_DRAW_TEXT_OPTIONS o)
{
    RT;
    D2dDrawTextLayout(rt, origin, l, brush, o);
}
static void STDMETHODCALLTYPE cx_unsupported_glyphs(ID2D1DeviceContext6 *This)
{
    RT;
    rt->error = rt->error ? rt->error : D2DERR_UNSUPPORTED_OPERATION;
}
static HRESULT STDMETHODCALLTYPE cx_svg_document(ID2D1DeviceContext6 *This, IStream *stream, D2D1_SIZE_F size, ID2D1SvgDocument **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return D2dSvgCreate(rt, stream, size, out);
}
static void STDMETHODCALLTYPE cx_draw_svg(ID2D1DeviceContext6 *This, ID2D1SvgDocument *doc)
{
    RT;
    if (!rt->drawing) {
        rt->error = D2DERR_WRONG_STATE;
        return;
    }
    if (!doc) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    D2dSvgDraw(rt, doc);
}
static HRESULT STDMETHODCALLTYPE cx_color_dxgi(ID2D1DeviceContext6 *This, UINT32 space, ID2D1ColorContext1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    RT;
    return D2dCreateColorContextDxgi(rt->obj.factory, space, (ID2D1ColorContext **)out);
}
static HRESULT STDMETHODCALLTYPE cx_color_simple(ID2D1DeviceContext6 *This, const void *p, ID2D1ColorContext1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    RT;
    return D2dCreateColorContextSimple(rt->obj.factory, p, (ID2D1ColorContext **)out);
}
static void STDMETHODCALLTYPE cx_blend_image(ID2D1DeviceContext6 *This, ID2D1Image *image, UINT32 mode, const D2D1_POINT_2F *offset,
                                             const D2D1_RECT_F *source, D2D1_INTERPOLATION_MODE interp)
{
    RT;
    rt->error = rt->error ? rt->error : D2DERR_UNSUPPORTED_OPERATION;
}

#define CTX_COUNT (57 + 35 + 3 + 11 + 2 + 7 + 4 + 1)
_Static_assert(CTX_COUNT <= 140, "table size");
static LONG g_ctx_vtbl_ready;

static void build_ctx_vtbl(void)
{
    if (g_ctx_vtbl_ready)
        return;
    static const void *const extra[CTX_COUNT - 57] = {
        cx_create_bitmap, cx_bitmap_from_wic, cx_color_context, cx_color_context_file, cx_color_context_wic, cx_bitmap_from_surface,
        cx_create_effect, cx_gradient1, cx_image_brush, cx_bitmap_brush1, cx_command_list, cx_dxgi_supported, cx_precision_supported,
        cx_local_bounds, cx_world_bounds, cx_glyph_bounds, cx_get_device, cx_set_target, cx_get_target, cx_set_controls,
        cx_get_controls, cx_set_blend, cx_get_blend, cx_set_unit, cx_get_unit, cx_glyph_run1, cx_draw_image, cx_draw_metafile,
        cx_draw_bitmap1, cx_push_layer1, cx_invalidate, cx_invalid_count, cx_invalid_rects, cx_required_rects, cx_fill_mask1,
        /* context 1 */
        cx_filled_real, cx_stroked_real, cx_draw_real,
        /* context 2 */
        cx_inks, cx_inks, cx_nocreate, cx_image_wic, cx_lookup, cx_image_dxgi, cx_mesh_bounds, cx_unsupported_draw, cx_unsupported_draw,
        cx_unsupported_draw, cx_nocreate_two,
        /* context 3 */
        cx_nocreate1, cx_unsupported_draw,
        /* context 4 */
        cx_nocreate1, cx_draw_text1, cx_draw_layout1, cx_unsupported_glyphs, cx_unsupported_glyphs, cx_nocreate, cx_nocreate,
        /* context 5 */
        cx_svg_document, cx_draw_svg, cx_color_dxgi, cx_color_simple,
        /* context 6 */
        cx_blend_image};
    for (int i = 0; i < 57; i++) {
        g_ctx_vtbl[i] = d2d_rt_base[i];
        g_ctx_sec_vtbl[i] = d2d_rt_thunks[i];
    }
    for (int i = 0; i < CTX_COUNT - 57; i++)
        g_ctx_vtbl[57 + i] = g_ctx_sec_vtbl[57 + i] = extra[i];
    g_ctx_vtbl_ready = 1;
}

HRESULT D2dRtQueryContext(D2dObj *obj, REFIID riid, void **out)
{
    D2dRt *rt = (D2dRt *)obj;
    const GUID *const *p;
    for (p = g_ctx_iids; *p; p++) {
        if (IsEqualGUID(riid, *p) && !IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID2D1Resource) &&
            !IsEqualGUID(riid, &IID_ID2D1RenderTarget))
            break;
    }
    if (!*p)
        return E_NOINTERFACE;
    build_ctx_vtbl();
    rt->ctx_vtbl = g_ctx_sec_vtbl;
    *out = &rt->ctx_vtbl;
    D2dAddRef(rt);
    return S_OK;
}

HRESULT D2dContextCreate(D2dDevice *device, D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext **out)
{
    build_ctx_vtbl();
    D2dRt *rt = D2dNew(sizeof(*rt), g_ctx_vtbl, &g_ctx_class, device->obj.factory);
    if (!rt)
        return E_OUTOFMEMORY;
    rt->gdi_vtbl = d2d_gdi_interop_vtbl;
    rt->kind = D2D_RT_CONTEXT;
    rt->context = TRUE;
    rt->device = (ID2D1Device *)device;
    D2dAddRef(device);
    rt->context_options = options;
    ID2D1Factory *f = (ID2D1Factory *)rt->obj.factory;
    f->lpVtbl->GetDesktopDpi(f, &rt->dpi_x, &rt->dpi_y);
    D2dRtInitState(rt);
    mime_vec_mask_init(&rt->clip, &d2d_allocator);
    rt->controls.bufferPrecision = D2D1_BUFFER_PRECISION_UNKNOWN;
    rt->controls.tileSize.width = rt->controls.tileSize.height = 512;
    *out = (ID2D1DeviceContext *)rt;
    return S_OK;
}
