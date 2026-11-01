/*
 * render targets: the methods every target shares, and the targets of windows, device contexts, wic bitmaps, dxgi
 * surfaces and bitmaps.
 */
#include "d2d_private.h"
#include <float.h>

#define RT D2dRt *rt = (D2dRt *)This
#define KIND_NAME

extern HRESULT D2dTextureTransfer(IDXGISurface *surface, BYTE *native, UINT32 stride, UINT32 width, UINT32 height,
                                  BOOL write);

D2D_IIDS(g_rt_iids, &IID_ID2D1Resource, &IID_ID2D1RenderTarget);
D2D_IIDS(g_bitmap_rt_iids, &IID_ID2D1Resource, &IID_ID2D1RenderTarget, &IID_ID2D1BitmapRenderTarget);
D2D_IIDS(g_hwnd_iids, &IID_ID2D1Resource, &IID_ID2D1RenderTarget, &IID_ID2D1HwndRenderTarget);
D2D_IIDS(g_dc_iids, &IID_ID2D1Resource, &IID_ID2D1RenderTarget, &IID_ID2D1DCRenderTarget);

static HRESULT rt_query_extra(D2dObj *obj, REFIID riid, void **out)
{
    D2dRt *rt = (D2dRt *)obj;
    if (IsEqualGUID(riid, &IID_ID2D1GdiInteropRenderTarget) && (rt->kind == D2D_RT_HWND || rt->kind == D2D_RT_DC || rt->kind == D2D_RT_BITMAP)) {
        *out = &rt->gdi_vtbl;
        D2dAddRef(rt);
        return S_OK;
    }
    if (rt->kind != D2D_RT_CONTEXT)
        return D2dRtQueryContext(obj, riid, out);
    return E_NOINTERFACE;
}

static void rt_destroy_class(D2dObj *obj) { D2dRtDestroyContents((D2dRt *)obj); }

/* one class per kind of target, since the interface list differs. */
const D2dClass d2d_rt_class = {"render target", g_rt_iids, rt_destroy_class, rt_query_extra};
static const D2dClass g_bitmap_rt_class = {"bitmap render target", g_bitmap_rt_iids, rt_destroy_class, rt_query_extra};
static const D2dClass g_hwnd_class = {"hwnd render target", g_hwnd_iids, rt_destroy_class, rt_query_extra};
static const D2dClass g_dc_class = {"dc render target", g_dc_iids, rt_destroy_class, rt_query_extra};

D2dRt *D2dRtOf(void *p)
{
    if (!p)
        return nullptr;
    D2dRt *sec = D2dRtFromContextPtr(p);
    if (sec)
        return sec;
    const D2dClass *c = ((D2dObj *)p)->cls;
    if (c == &d2d_rt_class || c == &g_bitmap_rt_class || c == &g_hwnd_class || c == &g_dc_class || D2dIsContextClass(c))
        return (D2dRt *)p;
    return nullptr;
}

void D2dRtDestroyContents(D2dRt *rt)
{
    D2dClipReset(rt);
    while (rt->clips.count) {
        D2dClipRec *rec = (D2dClipRec *)rt->clips.data + --rt->clips.count;
        mime_vec_mask_free(&rec->saved);
        if (rec->layer) {
            D2D_RELEASE(rec->layer->brush);
            mime_vec_mask_free(&rec->layer->mask);
            D2dSurfaceFree(&rec->layer->surface);
            D2dFree(rec->layer);
        }
    }
    D2dArrayFree(&rt->clips);
    D2dArrayFree(&rt->state_stack);
    D2dSurfaceFree(&rt->surface);
    D2D_RELEASE(rt->wic);
    D2D_RELEASE(rt->dxgi_surface);
    D2D_RELEASE(rt->own_bitmap);
    D2D_RELEASE(rt->target_bitmap);
    D2D_RELEASE(rt->target_image);
    D2D_RELEASE(rt->device);
    D2D_RELEASE(rt->dwrite);
    D2D_RELEASE(rt->state.params);
    if (rt->interop_dc) {
        SelectObject(rt->interop_dc, rt->interop_old);
        DeleteObject(rt->interop_bitmap);
        DeleteDC(rt->interop_dc);
    }
}

void D2dRtInitState(D2dRt *rt)
{
    memset(&rt->state, 0, sizeof(rt->state));
    rt->state.transform = d2d_identity;
    rt->state.tag1 = rt->state.tag2 = 0;
}

static void desktop_dpi(D2dRt *rt)
{
    ID2D1Factory *f = (ID2D1Factory *)rt->obj.factory;
    f->lpVtbl->GetDesktopDpi(f, &rt->dpi_x, &rt->dpi_y);
}

/* ------------------------------------------------------------------ creating targets */

static BOOL props_supported(const D2D1_RENDER_TARGET_PROPERTIES *p, D2dFormat *fmt, BOOL *srgb, DXGI_FORMAT *dxgi,
                            D2D1_ALPHA_MODE *alpha)
{
    if (p->type > D2D1_RENDER_TARGET_TYPE_HARDWARE || p->usage & ~3u || (p->minLevel != 0 && p->minLevel != D2D1_FEATURE_LEVEL_9 && p->minLevel != D2D1_FEATURE_LEVEL_10))
        return FALSE;
    *dxgi = p->pixelFormat.format;
    *alpha = p->pixelFormat.alphaMode;
    if ((int)*dxgi == 0) {
        *dxgi = (DXGI_FORMAT)87;
        if (*alpha == D2D1_ALPHA_MODE_UNKNOWN)
            *alpha = D2D1_ALPHA_MODE_PREMULTIPLIED;
    }
    if (!D2dFormatFromDxgi(*dxgi, fmt, srgb))
        return FALSE;
    if (*alpha == D2D1_ALPHA_MODE_UNKNOWN)
        *alpha = D2dDefaultAlpha(*dxgi);
    return TRUE;
}

HRESULT D2dRtCreate(D2dFactory *f, int kind, const D2D1_RENDER_TARGET_PROPERTIES *props, D2dRt **out)
{
    D2dFormat fmt;
    BOOL srgb;
    DXGI_FORMAT dxgi;
    D2D1_ALPHA_MODE alpha;
    if (!props_supported(props, &fmt, &srgb, &dxgi, &alpha))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    if (alpha > D2D1_ALPHA_MODE_IGNORE)
        return E_INVALIDARG;
    const void *vtbl;
    const D2dClass *cls = &d2d_rt_class;
    extern const void *const d2d_rt_vtbl_plain[], *const d2d_rt_vtbl_bitmap_rt[], *const d2d_rt_vtbl_hwnd_rt[], *const d2d_rt_vtbl_dc_rt[];
    switch (kind) {
    case D2D_RT_HWND:
        vtbl = d2d_rt_vtbl_hwnd_rt;
        cls = &g_hwnd_class;
        break;
    case D2D_RT_DC:
        vtbl = d2d_rt_vtbl_dc_rt;
        cls = &g_dc_class;
        break;
    case D2D_RT_BITMAP:
        vtbl = d2d_rt_vtbl_bitmap_rt;
        cls = &g_bitmap_rt_class;
        break;
    default:
        vtbl = d2d_rt_vtbl_plain;
        break;
    }
    D2dRt *rt = D2dNew(sizeof(*rt), vtbl, cls, f);
    if (!rt)
        return E_OUTOFMEMORY;
    extern const void *const d2d_gdi_interop_vtbl[];
    rt->gdi_vtbl = d2d_gdi_interop_vtbl;
    rt->kind = kind;
    rt->props = *props;
    rt->props.pixelFormat.format = dxgi;
    rt->props.pixelFormat.alphaMode = alpha;
    rt->pixel_format.format = dxgi;
    rt->pixel_format.alphaMode = alpha;
    rt->format = fmt;
    rt->srgb = srgb;
    rt->gdi_compatible = (props->usage & D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE) != 0;
    rt->dpi_x = props->dpiX;
    rt->dpi_y = props->dpiY;
    if (!(rt->dpi_x > 0) || !(rt->dpi_y > 0))
        desktop_dpi(rt);
    D2dRtInitState(rt);
    mime_vec_mask_init(&rt->clip, &d2d_allocator);
    *out = rt;
    return S_OK;
}

HRESULT D2dRtResize(D2dRt *rt, UINT32 w, UINT32 h)
{
    D2dSurface s;
    if (!D2dSurfaceInit(&s, (int)w, (int)h))
        return E_OUTOFMEMORY;
    D2dSurfaceFree(&rt->surface);
    rt->surface = s;
    rt->draw = &rt->surface;
    rt->pixel_w = w;
    rt->pixel_h = h;
    return S_OK;
}

HRESULT D2dRtCreateHwnd(D2dFactory *f, const D2D1_RENDER_TARGET_PROPERTIES *props,
                        const D2D1_HWND_RENDER_TARGET_PROPERTIES *hp, ID2D1HwndRenderTarget **out)
{
    if (!IsWindow(hp->hwnd))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (hp->presentOptions & ~3u)
        return E_INVALIDARG;
    D2dRt *rt;
    HRESULT hr = D2dRtCreate(f, D2D_RT_HWND, props, &rt);
    if (FAILED(hr))
        return hr;
    rt->hwnd = hp->hwnd;
    rt->present = hp->presentOptions;
    UINT32 w = hp->pixelSize.width, h = hp->pixelSize.height;
    if (!w && !h) {
        RECT r;
        GetClientRect(hp->hwnd, &r);
        w = (UINT32)(r.right - r.left);
        h = (UINT32)(r.bottom - r.top);
    }
    hr = D2dRtResize(rt, w, h);
    if (FAILED(hr)) {
        D2dRelease(rt);
        return hr;
    }
    *out = (ID2D1HwndRenderTarget *)rt;
    return S_OK;
}

HRESULT D2dRtCreateDC(D2dFactory *f, const D2D1_RENDER_TARGET_PROPERTIES *props, ID2D1DCRenderTarget **out)
{
    /* a dc target draws alpha only in the formats it can hand to gdi. */
    if ((int)props->pixelFormat.format != 87 || (props->pixelFormat.alphaMode != D2D1_ALPHA_MODE_PREMULTIPLIED &&
                                                  props->pixelFormat.alphaMode != D2D1_ALPHA_MODE_IGNORE))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    D2dRt *rt;
    HRESULT hr = D2dRtCreate(f, D2D_RT_DC, props, &rt);
    if (FAILED(hr))
        return hr;
    *out = (ID2D1DCRenderTarget *)rt;
    return S_OK;
}

HRESULT D2dRtCreateWic(D2dFactory *f, IWICBitmap *bitmap, const D2D1_RENDER_TARGET_PROPERTIES *props,
                       ID2D1RenderTarget **out)
{
    WICPixelFormatGUID pf;
    HRESULT hr = bitmap->lpVtbl->GetPixelFormat(bitmap, &pf);
    if (FAILED(hr))
        return hr;
    DXGI_FORMAT dxgi;
    D2D1_ALPHA_MODE alpha;
    if (IsEqualGUID(&pf, &GUID_WICPixelFormat32bppPBGRA)) {
        dxgi = (DXGI_FORMAT)87;
        alpha = D2D1_ALPHA_MODE_PREMULTIPLIED;
    } else if (IsEqualGUID(&pf, &GUID_WICPixelFormat32bppBGR)) {
        dxgi = (DXGI_FORMAT)87;
        alpha = D2D1_ALPHA_MODE_IGNORE;
    } else if (IsEqualGUID(&pf, &GUID_WICPixelFormat32bppPRGBA)) {
        dxgi = (DXGI_FORMAT)28;
        alpha = D2D1_ALPHA_MODE_PREMULTIPLIED;
    } else if (IsEqualGUID(&pf, &GUID_WICPixelFormat32bppRGB)) {
        dxgi = (DXGI_FORMAT)28;
        alpha = D2D1_ALPHA_MODE_IGNORE;
    } else if (IsEqualGUID(&pf, &GUID_WICPixelFormat8bppAlpha)) {
        dxgi = (DXGI_FORMAT)65;
        alpha = D2D1_ALPHA_MODE_PREMULTIPLIED;
    } else {
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    }
    D2D1_RENDER_TARGET_PROPERTIES p = *props;
    if ((int)p.pixelFormat.format == 0)
        p.pixelFormat.format = dxgi;
    if (p.pixelFormat.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
        p.pixelFormat.alphaMode = alpha;
    if (p.pixelFormat.format != dxgi || p.pixelFormat.alphaMode != alpha)
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    D2dRt *rt;
    hr = D2dRtCreate(f, D2D_RT_WIC, &p, &rt);
    if (FAILED(hr))
        return hr;
    UINT w, h;
    bitmap->lpVtbl->GetSize(bitmap, &w, &h);
    hr = D2dRtResize(rt, w, h);
    if (FAILED(hr)) {
        D2dRelease(rt);
        return hr;
    }
    rt->wic = bitmap;
    D2D_ADDREF(bitmap);
    *out = (ID2D1RenderTarget *)rt;
    return S_OK;
}

HRESULT D2dRtCreateDxgi(D2dFactory *f, IDXGISurface *surface, const D2D1_RENDER_TARGET_PROPERTIES *props,
                        ID2D1RenderTarget **out)
{
    DXGI_SURFACE_DESC desc;
    HRESULT hr = surface->lpVtbl->GetDesc(surface, &desc);
    if (FAILED(hr))
        return hr;
    D2D1_RENDER_TARGET_PROPERTIES p = *props;
    D2dFormat fmt;
    BOOL srgb;
    if (!D2dFormatFromDxgi(desc.Format, &fmt, &srgb))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    if ((int)p.pixelFormat.format == 0)
        p.pixelFormat.format = desc.Format;
    if (p.pixelFormat.format != desc.Format)
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    D2dRt *rt;
    hr = D2dRtCreate(f, D2D_RT_DXGI, &p, &rt);
    if (FAILED(hr))
        return hr;
    hr = D2dRtResize(rt, desc.Width, desc.Height);
    if (FAILED(hr)) {
        D2dRelease(rt);
        return hr;
    }
    rt->dxgi_surface = surface;
    D2D_ADDREF(surface);
    *out = (ID2D1RenderTarget *)rt;
    return S_OK;
}

/* ------------------------------------------------------------------ begin and end */

/* the pixels of the window's or dc's current contents in a memory dc of the same size. */
static BOOL dib_dc(int w, int h, HDC *dc, HBITMAP *bmp, void **bits, HGDIOBJ *old)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    *dc = CreateCompatibleDC(screen);
    *bmp = CreateDIBSection(*dc, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (!*dc || !*bmp) {
        if (*bmp)
            DeleteObject(*bmp);
        if (*dc)
            DeleteDC(*dc);
        return FALSE;
    }
    *old = SelectObject(*dc, *bmp);
    return TRUE;
}

static void begin_target(D2dRt *rt)
{
    switch (rt->kind) {
    case D2D_RT_DC: {
        if (!rt->dc || !rt->surface.px)
            return;
        if (rt->pixel_format.alphaMode == D2D1_ALPHA_MODE_IGNORE) {
            HDC mem;
            HBITMAP bmp;
            void *bits;
            HGDIOBJ old;
            if (dib_dc(rt->surface.w, rt->surface.h, &mem, &bmp, &bits, &old)) {
                BitBlt(mem, 0, 0, rt->surface.w, rt->surface.h, rt->dc, rt->dc_rect.left, rt->dc_rect.top, SRCCOPY);
                const uint32_t *src = bits;
                for (int i = 0; i < rt->surface.w * rt->surface.h; i++)
                    rt->surface.px[i] = src[i] | 0xff000000u;
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
        } else {
            memset(rt->surface.px, 0, (SIZE_T)rt->surface.w * rt->surface.h * 4);
        }
        break;
    }
    case D2D_RT_WIC: {
        IWICBitmapLock *lock;
        WICRect r = {0, 0, rt->surface.w, rt->surface.h};
        if (SUCCEEDED(rt->wic->lpVtbl->Lock(rt->wic, &r, WICBitmapLockRead, &lock))) {
            UINT stride, size;
            BYTE *data;
            lock->lpVtbl->GetStride(lock, &stride);
            lock->lpVtbl->GetDataPointer(lock, &size, &data);
            for (int y = 0; y < rt->surface.h; y++)
                D2dFormatLoad(rt->format, rt->pixel_format.alphaMode, rt->srgb, data + (SIZE_T)y * stride, D2dSurfaceRow(&rt->surface, y), (UINT32)rt->surface.w);
            lock->lpVtbl->Release(lock);
        }
        break;
    }
    case D2D_RT_DXGI: {
        UINT32 stride = (UINT32)rt->surface.w * D2dFormatBytes(rt->format);
        BYTE *native = D2dAllocZero((SIZE_T)stride * rt->surface.h + 4);
        if (native && SUCCEEDED(D2dTextureTransfer(rt->dxgi_surface, native, stride, (UINT32)rt->surface.w, (UINT32)rt->surface.h, FALSE))) {
            for (int y = 0; y < rt->surface.h; y++)
                D2dFormatLoad(rt->format, rt->pixel_format.alphaMode, rt->srgb, native + (SIZE_T)y * stride, D2dSurfaceRow(&rt->surface, y), (UINT32)rt->surface.w);
        }
        D2dFree(native);
        break;
    }
    case D2D_RT_CONTEXT:
        if (rt->target_bitmap) {
            rt->target_bitmap->bound_as_target = 1;
            if (rt->target_bitmap->surface)
                D2dBitmapReadSurface(rt->target_bitmap);
            const D2dSurface *v = D2dBitmapView(rt->target_bitmap);
            rt->draw = v ? (D2dSurface *)v : nullptr;
        }
        break;
    default:
        break;
    }
}

static void present_target(D2dRt *rt)
{
    switch (rt->kind) {
    case D2D_RT_HWND: {
        HDC dc = GetDC(rt->hwnd);
        if (!dc)
            return;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = rt->surface.w;
        bi.bmiHeader.biHeight = -rt->surface.h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, 0, 0, (DWORD)rt->surface.w, (DWORD)rt->surface.h, 0, 0, 0, (UINT)rt->surface.h, rt->surface.px, &bi, DIB_RGB_COLORS);
        ReleaseDC(rt->hwnd, dc);
        break;
    }
    case D2D_RT_DC: {
        if (!rt->dc || !rt->surface.px)
            return;
        HDC mem;
        HBITMAP bmp;
        void *bits;
        HGDIOBJ old;
        if (!dib_dc(rt->surface.w, rt->surface.h, &mem, &bmp, &bits, &old))
            return;
        uint32_t *dst = bits;
        int n = rt->surface.w * rt->surface.h;
        if (rt->pixel_format.alphaMode == D2D1_ALPHA_MODE_IGNORE) {
            for (int i = 0; i < n; i++)
                dst[i] = rt->surface.px[i];
            BitBlt(rt->dc, rt->dc_rect.left, rt->dc_rect.top, rt->surface.w, rt->surface.h, mem, 0, 0, SRCCOPY);
        } else {
            /* premultiplied pixels are composed over the dc by hand. */
            BitBlt(mem, 0, 0, rt->surface.w, rt->surface.h, rt->dc, rt->dc_rect.left, rt->dc_rect.top, SRCCOPY);
            for (int i = 0; i < n; i++) {
                uint32_t s = rt->surface.px[i], d = dst[i] | 0xff000000u;
                if (!(s >> 24))
                    continue;
                uint32_t ia = 255 - (s >> 24);
                uint32_t r = mime_vec_div255(((d >> 16) & 255) * ia) + ((s >> 16) & 255);
                uint32_t g = mime_vec_div255(((d >> 8) & 255) * ia) + ((s >> 8) & 255);
                uint32_t b = mime_vec_div255((d & 255) * ia) + (s & 255);
                dst[i] = 0xff000000u | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
            }
            BitBlt(rt->dc, rt->dc_rect.left, rt->dc_rect.top, rt->surface.w, rt->surface.h, mem, 0, 0, SRCCOPY);
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        break;
    }
    case D2D_RT_WIC: {
        IWICBitmapLock *lock;
        WICRect r = {0, 0, rt->surface.w, rt->surface.h};
        if (SUCCEEDED(rt->wic->lpVtbl->Lock(rt->wic, &r, WICBitmapLockWrite, &lock))) {
            UINT stride, size;
            BYTE *data;
            lock->lpVtbl->GetStride(lock, &stride);
            lock->lpVtbl->GetDataPointer(lock, &size, &data);
            for (int y = 0; y < rt->surface.h; y++)
                D2dFormatStore(rt->format, rt->pixel_format.alphaMode, rt->srgb, D2dSurfaceRow(&rt->surface, y), data + (SIZE_T)y * stride, (UINT32)rt->surface.w);
            lock->lpVtbl->Release(lock);
        }
        break;
    }
    case D2D_RT_DXGI: {
        UINT32 stride = (UINT32)rt->surface.w * D2dFormatBytes(rt->format);
        BYTE *native = D2dAllocZero((SIZE_T)stride * rt->surface.h + 4);
        if (native) {
            for (int y = 0; y < rt->surface.h; y++)
                D2dFormatStore(rt->format, rt->pixel_format.alphaMode, rt->srgb, D2dSurfaceRow(&rt->surface, y), native + (SIZE_T)y * stride, (UINT32)rt->surface.w);
            D2dTextureTransfer(rt->dxgi_surface, native, stride, (UINT32)rt->surface.w, (UINT32)rt->surface.h, TRUE);
            D2dFree(native);
        }
        break;
    }
    case D2D_RT_CONTEXT:
        if (rt->target_bitmap) {
            D2dBitmapCommit(rt->target_bitmap);
            D2dBitmapWriteSurface(rt->target_bitmap);
        }
        break;
    case D2D_RT_BITMAP:
        if (rt->own_bitmap)
            D2dBitmapCommit(rt->own_bitmap);
        break;
    }
}

void D2dBeginDrawCore(D2dRt *rt)
{
    rt->drawing = TRUE;
    rt->error = S_OK;
    if (rt->kind == D2D_RT_CONTEXT && !rt->target_bitmap) {
        rt->draw = nullptr;
        return;
    }
    begin_target(rt);
}

HRESULT D2dEndDrawCore(D2dRt *rt, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    if (tag1)
        *tag1 = rt->state.tag1;
    if (tag2)
        *tag2 = rt->state.tag2;
    HRESULT hr = rt->error;
    if (rt->clips.count) {
        if (!hr)
            hr = D2DERR_PUSH_POP_UNBALANCED;
        while (rt->clips.count) {
            D2dClipRec *rec = (D2dClipRec *)rt->clips.data + rt->clips.count - 1;
            if (rec->layer)
                D2dPopLayer(rt);
            else
                D2dPopAxisAlignedClip(rt);
        }
    }
    rt->error = S_OK;
    if (rt->kind == D2D_RT_CONTEXT && !rt->target_bitmap) {
        rt->drawing = FALSE;
        return hr ? hr : D2DERR_WRONG_STATE;
    }
    present_target(rt);
    if (rt->kind == D2D_RT_CONTEXT && rt->target_bitmap)
        rt->target_bitmap->bound_as_target = 0;
    rt->drawing = FALSE;
    D2dClipReset(rt);
    return hr;
}

/* ------------------------------------------------------------------ bitmaps and brushes */

static D2D1_BITMAP_PROPERTIES1 default_bitmap_props(D2dRt *rt, const D2D1_BITMAP_PROPERTIES *p)
{
    D2D1_BITMAP_PROPERTIES1 q = {};
    q.pixelFormat = rt->pixel_format;
    q.dpiX = rt->dpi_x;
    q.dpiY = rt->dpi_y;
    if (p) {
        q.pixelFormat = p->pixelFormat;
        if ((int)q.pixelFormat.format == 0) {
            q.pixelFormat.format = rt->pixel_format.format;
            if (q.pixelFormat.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
                q.pixelFormat.alphaMode = rt->pixel_format.alphaMode;
        }
        if (p->dpiX > 0)
            q.dpiX = p->dpiX;
        if (p->dpiY > 0)
            q.dpiY = p->dpiY;
    }
    return q;
}

HRESULT D2dRtCreateBitmap(D2dRt *rt, D2D1_SIZE_U size, const void *data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1 *p1,
                          const D2D1_BITMAP_PROPERTIES *p0, BOOL v1, ID2D1Bitmap1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    D2D1_BITMAP_PROPERTIES1 q;
    if (p1)
        q = *p1, (void)0;
    else
        q = default_bitmap_props(rt, p0);
    if (p1) {
        if ((int)q.pixelFormat.format == 0)
            q.pixelFormat = rt->pixel_format;
        if (!(q.dpiX > 0))
            q.dpiX = rt->dpi_x;
        if (!(q.dpiY > 0))
            q.dpiY = rt->dpi_y;
    }
    D2dBitmap *b;
    HRESULT hr = D2dBitmapCreate(rt->obj.factory, size.width, size.height, q.pixelFormat.format, q.pixelFormat.alphaMode, q.dpiX, q.dpiY,
                                 q.bitmapOptions, v1, &b);
    if (FAILED(hr))
        return hr;
    if (data)
        D2dBitmapCopyIn(b, 0, 0, data, pitch, size.width, size.height);
    if (q.colorContext) {
        b->color_context = q.colorContext;
        D2D_ADDREF(b->color_context);
    }
    *out = (ID2D1Bitmap1 *)b;
    return S_OK;
}

HRESULT D2dRtBitmapFromWic(D2dRt *rt, IWICBitmapSource *source, const D2D1_BITMAP_PROPERTIES1 *p1, const D2D1_BITMAP_PROPERTIES *p0,
                           ID2D1Bitmap1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!source)
        return E_INVALIDARG;
    UINT w, h;
    HRESULT hr = source->lpVtbl->GetSize(source, &w, &h);
    if (FAILED(hr))
        return hr;
    WICPixelFormatGUID pf;
    source->lpVtbl->GetPixelFormat(source, &pf);
    IWICBitmapSource *use = source;
    IWICFormatConverter *conv = nullptr;
    BOOL straight = IsEqualGUID(&pf, &GUID_WICPixelFormat32bppBGRA);
    BOOL opaque = IsEqualGUID(&pf, &GUID_WICPixelFormat32bppBGR);
    if (!straight && !opaque && !IsEqualGUID(&pf, &GUID_WICPixelFormat32bppPBGRA)) {
        IWICImagingFactory *wf = nullptr;
        hr = CoCreateInstance(&CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wf);
        if (FAILED(hr))
            return hr;
        hr = wf->lpVtbl->CreateFormatConverter(wf, &conv);
        wf->lpVtbl->Release(wf);
        if (SUCCEEDED(hr))
            hr = conv->lpVtbl->Initialize(conv, source, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            if (conv)
                conv->lpVtbl->Release(conv);
            return hr == WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR ? hr : hr;
        }
        use = (IWICBitmapSource *)conv;
        straight = FALSE;
        opaque = FALSE;
    }
    D2D1_BITMAP_PROPERTIES1 q;
    if (p1)
        q = *p1;
    else
        q = default_bitmap_props(rt, p0);
    if (!p0 && !p1) {
        double dx = 0, dy = 0;
        if (SUCCEEDED(source->lpVtbl->GetResolution(source, &dx, &dy)) && dx > 0 && dy > 0) {
            q.dpiX = (float)dx;
            q.dpiY = (float)dy;
        }
    }
    q.pixelFormat.format = (DXGI_FORMAT)87;
    q.pixelFormat.alphaMode = opaque ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED;
    D2dBitmap *b;
    hr = D2dBitmapCreate(rt->obj.factory, w, h, q.pixelFormat.format, q.pixelFormat.alphaMode, q.dpiX, q.dpiY, q.bitmapOptions, TRUE, &b);
    if (SUCCEEDED(hr)) {
        hr = use->lpVtbl->CopyPixels(use, nullptr, b->stride, b->stride * h, b->native);
        if (SUCCEEDED(hr) && straight) {
            uint32_t *px = (uint32_t *)b->native;
            for (UINT32 y = 0; y < h; y++)
                for (UINT32 x = 0; x < w; x++)
                    px[y * (b->stride / 4) + x] = D2dPremulPixel(px[y * (b->stride / 4) + x]);
        }
        if (FAILED(hr))
            D2dRelease(b);
        else
            *out = (ID2D1Bitmap1 *)b;
    }
    if (conv)
        conv->lpVtbl->Release(conv);
    return hr;
}

static HRESULT STDMETHODCALLTYPE rt_create_bitmap(ID2D1RenderTarget *This, D2D1_SIZE_U size, const void *data, UINT32 pitch,
                                                  const D2D1_BITMAP_PROPERTIES *p, ID2D1Bitmap **out)
{
    RT;
    return D2dRtCreateBitmap(rt, size, data, pitch, nullptr, p, FALSE, (ID2D1Bitmap1 **)out);
}

static HRESULT STDMETHODCALLTYPE rt_bitmap_from_wic(ID2D1RenderTarget *This, IWICBitmapSource *s, const D2D1_BITMAP_PROPERTIES *p,
                                                    ID2D1Bitmap **out)
{
    RT;
    return D2dRtBitmapFromWic(rt, s, nullptr, p, (ID2D1Bitmap1 **)out);
}

static HRESULT STDMETHODCALLTYPE rt_shared_bitmap(ID2D1RenderTarget *This, REFIID riid, void *data, const D2D1_BITMAP_PROPERTIES *p,
                                                  ID2D1Bitmap **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!riid || !data)
        return E_INVALIDARG;
    if (IsEqualGUID(riid, &IID_ID2D1Bitmap)) {
        D2dBitmap *b = D2dBitmapOf(data);
        if (!b)
            return E_INVALIDARG;
        if (b->obj.factory != rt->obj.factory)
            return D2DERR_WRONG_FACTORY;
        D2dAddRef(b);
        *out = (ID2D1Bitmap *)b;
        return S_OK;
    }
    if (IsEqualGUID(riid, &IID_IWICBitmapSource) || IsEqualGUID(riid, &IID_IWICBitmap))
        return D2dRtBitmapFromWic(rt, data, nullptr, p, (ID2D1Bitmap1 **)out);
    if (IsEqualGUID(riid, &IID_IDXGISurface)) {
        D2D1_BITMAP_PROPERTIES1 q = default_bitmap_props(rt, p);
        extern HRESULT D2dBitmapFromSurface(D2dRt *, IDXGISurface *, const D2D1_BITMAP_PROPERTIES1 *, ID2D1Bitmap1 **);
        return D2dBitmapFromSurface(rt, data, &q, (ID2D1Bitmap1 **)out);
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE rt_bitmap_brush(ID2D1RenderTarget *This, ID2D1Bitmap *bm, const D2D1_BITMAP_BRUSH_PROPERTIES *bp,
                                                 const D2D1_BRUSH_PROPERTIES *props, ID2D1BitmapBrush **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!bm)
        return E_INVALIDARG;
    D2dBitmap *b = D2dBitmapOf(bm);
    if (b && b->obj.factory != rt->obj.factory)
        return D2DERR_WRONG_FACTORY;
    D2D1_BITMAP_BRUSH_PROPERTIES1 q;
    if (bp) {
        q.extendModeX = bp->extendModeX;
        q.extendModeY = bp->extendModeY;
        q.interpolationMode = bp->interpolationMode == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    } else {
        q.extendModeX = q.extendModeY = D2D1_EXTEND_MODE_CLAMP;
        q.interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR;
    }
    return D2dCreateBitmapBrush(rt->obj.factory, bm, &q, props, FALSE, (ID2D1BitmapBrush1 **)out);
}

static HRESULT STDMETHODCALLTYPE rt_solid_brush(ID2D1RenderTarget *This, const D2D1_COLOR_F *c, const D2D1_BRUSH_PROPERTIES *p,
                                                ID2D1SolidColorBrush **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!c)
        return E_INVALIDARG;
    return D2dCreateSolidBrush(rt->obj.factory, c, p, out);
}

static HRESULT STDMETHODCALLTYPE rt_gradient_stops(ID2D1RenderTarget *This, const D2D1_GRADIENT_STOP *stops, UINT32 count,
                                                   D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend, ID2D1GradientStopCollection **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    D2dGradient *g;
    HRESULT hr = D2dCreateGradient(rt->obj.factory, stops, count, gamma, extend, FALSE, &g);
    if (SUCCEEDED(hr))
        *out = (ID2D1GradientStopCollection *)g;
    return hr;
}

static HRESULT STDMETHODCALLTYPE rt_linear_brush(ID2D1RenderTarget *This, const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gp,
                                                 const D2D1_BRUSH_PROPERTIES *p, ID2D1GradientStopCollection *stops,
                                                 ID2D1LinearGradientBrush **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (stops && !D2dSameFactory(stops, rt))
        return D2DERR_WRONG_FACTORY;
    return D2dCreateLinearBrush(rt->obj.factory, gp, p, stops, out);
}

static HRESULT STDMETHODCALLTYPE rt_radial_brush(ID2D1RenderTarget *This, const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gp,
                                                 const D2D1_BRUSH_PROPERTIES *p, ID2D1GradientStopCollection *stops,
                                                 ID2D1RadialGradientBrush **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (stops && !D2dSameFactory(stops, rt))
        return D2DERR_WRONG_FACTORY;
    return D2dCreateRadialBrush(rt->obj.factory, gp, p, stops, out);
}

static HRESULT STDMETHODCALLTYPE rt_compatible(ID2D1RenderTarget *This, const D2D1_SIZE_F *size, const D2D1_SIZE_U *psize,
                                               const D2D1_PIXEL_FORMAT *format, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options,
                                               ID2D1BitmapRenderTarget **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (options & ~1u)
        return E_INVALIDARG;
    UINT32 w, h;
    if (psize) {
        w = psize->width;
        h = psize->height;
    } else if (size) {
        w = (UINT32)(size->width * rt->dpi_x / 96.0f + 0.5f);
        h = (UINT32)(size->height * rt->dpi_y / 96.0f + 0.5f);
    } else {
        w = rt->pixel_w;
        h = rt->pixel_h;
    }
    D2D1_RENDER_TARGET_PROPERTIES p = rt->props;
    p.dpiX = rt->dpi_x;
    p.dpiY = rt->dpi_y;
    p.usage = (options & 1) ? D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE : D2D1_RENDER_TARGET_USAGE_NONE;
    if (format && ((int)format->format != 0 || format->alphaMode != D2D1_ALPHA_MODE_UNKNOWN)) {
        if ((int)format->format)
            p.pixelFormat.format = format->format;
        if (format->alphaMode != D2D1_ALPHA_MODE_UNKNOWN)
            p.pixelFormat.alphaMode = format->alphaMode;
        else
            p.pixelFormat.alphaMode = D2dDefaultAlpha(p.pixelFormat.format);
    }
    if (size && !psize) {
        /* the dpi follows the sizes when both are given. */
    } else if (size && psize && size->width > 0 && size->height > 0) {
        p.dpiX = psize->width * 96.0f / size->width;
        p.dpiY = psize->height * 96.0f / size->height;
    }
    D2dRt *n;
    HRESULT hr = D2dRtCreate(rt->obj.factory, D2D_RT_BITMAP, &p, &n);
    if (FAILED(hr))
        return hr;
    D2dBitmap *b;
    hr = D2dBitmapCreate(rt->obj.factory, w, h, n->pixel_format.format, n->pixel_format.alphaMode, n->dpi_x, n->dpi_y,
                         D2D1_BITMAP_OPTIONS_NONE, FALSE, &b);
    if (FAILED(hr)) {
        D2dRelease(n);
        return hr;
    }
    const D2dSurface *view = D2dBitmapView(b);
    n->own_bitmap = b;
    n->draw = (D2dSurface *)view;
    n->pixel_w = w;
    n->pixel_h = h;
    *out = (ID2D1BitmapRenderTarget *)n;
    return S_OK;
}

/* a layer is only a size. */
typedef struct D2dLayer {
    D2dObj obj;
    D2D1_SIZE_F size;
} D2dLayer;

static D2D1_SIZE_F STDMETHODCALLTYPE layer_size(ID2D1Layer *This) { return ((D2dLayer *)This)->size; }
static const void *const g_layer_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, layer_size};
D2D_IIDS(g_layer_iids, &IID_ID2D1Resource, &IID_ID2D1Layer);
static const D2dClass g_layer_class = {"layer", g_layer_iids, nullptr, nullptr};

static HRESULT STDMETHODCALLTYPE rt_create_layer(ID2D1RenderTarget *This, const D2D1_SIZE_F *size, ID2D1Layer **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    D2dLayer *l = D2dNew(sizeof(*l), g_layer_vtbl, &g_layer_class, rt->obj.factory);
    if (!l)
        return E_OUTOFMEMORY;
    if (size)
        l->size = *size;
    else {
        l->size.width = rt->pixel_w * 96.0f / rt->dpi_x;
        l->size.height = rt->pixel_h * 96.0f / rt->dpi_y;
    }
    *out = (ID2D1Layer *)l;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_create_mesh(ID2D1RenderTarget *This, ID2D1Mesh **out)
{
    RT;
    if (!out)
        return E_POINTER;
    return D2dCreateMesh(rt->obj.factory, out);
}

/* ------------------------------------------------------------------ drawing */

static D2dGeometry *tmp_geometry(D2dGeometry *g, int kind)
{
    D2dInitTempGeometry(g, kind);
    return g;
}

/* a stack geometry stands in for the object of a shape that is drawn once. */
static void draw_shape(D2dRt *rt, D2dGeometry *g, ID2D1Brush *brush, float width, ID2D1StrokeStyle *style, BOOL fill)
{
    g->obj.factory = rt->obj.factory;
    if (fill)
        D2dFillGeometry(rt, (ID2D1Geometry *)g, brush, nullptr);
    else
        D2dDrawGeometry(rt, (ID2D1Geometry *)g, brush, width, style);
}

static void STDMETHODCALLTYPE rt_draw_line(ID2D1RenderTarget *This, D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush *brush, FLOAT width,
                                           ID2D1StrokeStyle *style)
{
    RT;
    D2dGeometry g;
    tmp_geometry(&g, D2D_GEO_PATH);
    D2dPathBegin(&g.path, p0, D2D1_FIGURE_BEGIN_HOLLOW);
    D2dPathLine(&g.path, p1);
    D2dPathEnd(&g.path, D2D1_FIGURE_END_OPEN);
    draw_shape(rt, &g, brush, width, style, FALSE);
    D2dPathFree(&g.path);
}

static void STDMETHODCALLTYPE rt_draw_rect(ID2D1RenderTarget *This, const D2D1_RECT_F *r, ID2D1Brush *brush, FLOAT width,
                                           ID2D1StrokeStyle *style)
{
    RT;
    D2dGeometry g;
    tmp_geometry(&g, D2D_GEO_RECT);
    if (r)
        g.shape.rect = *r;
    draw_shape(rt, &g, brush, width, style, FALSE);
}

static void STDMETHODCALLTYPE rt_fill_rect(ID2D1RenderTarget *This, const D2D1_RECT_F *r, ID2D1Brush *brush)
{
    RT;
    if (!r)
        return;
    D2dFillRect(rt, r, brush);
}

static void STDMETHODCALLTYPE rt_draw_rounded(ID2D1RenderTarget *This, const D2D1_ROUNDED_RECT *r, ID2D1Brush *brush, FLOAT width,
                                              ID2D1StrokeStyle *style)
{
    RT;
    D2dGeometry g;
    tmp_geometry(&g, D2D_GEO_ROUNDED);
    if (r)
        g.shape.rounded = *r;
    draw_shape(rt, &g, brush, width, style, FALSE);
}

static void STDMETHODCALLTYPE rt_fill_rounded(ID2D1RenderTarget *This, const D2D1_ROUNDED_RECT *r, ID2D1Brush *brush)
{
    RT;
    D2dGeometry g;
    tmp_geometry(&g, D2D_GEO_ROUNDED);
    if (r)
        g.shape.rounded = *r;
    draw_shape(rt, &g, brush, 0, nullptr, TRUE);
}

static void STDMETHODCALLTYPE rt_draw_ellipse(ID2D1RenderTarget *This, const D2D1_ELLIPSE *e, ID2D1Brush *brush, FLOAT width,
                                              ID2D1StrokeStyle *style)
{
    RT;
    D2dGeometry g;
    tmp_geometry(&g, D2D_GEO_ELLIPSE);
    if (e)
        g.shape.ellipse = *e;
    draw_shape(rt, &g, brush, width, style, FALSE);
}

static void STDMETHODCALLTYPE rt_fill_ellipse(ID2D1RenderTarget *This, const D2D1_ELLIPSE *e, ID2D1Brush *brush)
{
    RT;
    D2dGeometry g;
    tmp_geometry(&g, D2D_GEO_ELLIPSE);
    if (e)
        g.shape.ellipse = *e;
    draw_shape(rt, &g, brush, 0, nullptr, TRUE);
}

static void STDMETHODCALLTYPE rt_draw_geometry(ID2D1RenderTarget *This, ID2D1Geometry *g, ID2D1Brush *brush, FLOAT width,
                                               ID2D1StrokeStyle *style)
{
    RT;
    D2dDrawGeometry(rt, g, brush, width, style);
}

static void STDMETHODCALLTYPE rt_fill_geometry(ID2D1RenderTarget *This, ID2D1Geometry *g, ID2D1Brush *brush, ID2D1Brush *opacity)
{
    RT;
    D2dFillGeometry(rt, g, brush, opacity);
}

static void STDMETHODCALLTYPE rt_fill_mesh(ID2D1RenderTarget *This, ID2D1Mesh *mesh, ID2D1Brush *brush)
{
    RT;
    if (!rt->drawing) {
        rt->error = D2DERR_WRONG_STATE;
        return;
    }
    D2dPaint paint;
    if (!D2dPaintSetup(rt, &paint, brush, nullptr))
        return;
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dMeshPolys(mesh, &world, &polys);
    if (FAILED(hr))
        rt->error = rt->error ? rt->error : hr;
    else
        D2dFillPolys(rt, &polys, TRUE, rt->state.aa == D2D1_ANTIALIAS_MODE_ALIASED, &paint);
    mime_vec_polys_free(&polys);
    D2dPaintRelease(&paint);
}

static void STDMETHODCALLTYPE rt_fill_opacity_mask(ID2D1RenderTarget *This, ID2D1Bitmap *mask, ID2D1Brush *brush,
                                                   D2D1_OPACITY_MASK_CONTENT content, const D2D1_RECT_F *dest, const D2D1_RECT_F *source)
{
    RT;
    D2dBitmap *m = D2dBitmapOf(mask);
    if (!m) {
        if (!rt->error)
            rt->error = E_INVALIDARG;
        return;
    }
    D2dFillOpacityMask(rt, m, brush, dest, source);
}

static void STDMETHODCALLTYPE rt_draw_bitmap(ID2D1RenderTarget *This, ID2D1Bitmap *bm, const D2D1_RECT_F *dest, FLOAT opacity,
                                             D2D1_BITMAP_INTERPOLATION_MODE mode, const D2D1_RECT_F *source)
{
    RT;
    D2dBitmap *b = D2dBitmapOf(bm);
    if (!b) {
        if (!rt->error)
            rt->error = E_INVALIDARG;
        return;
    }
    D2dDrawBitmapRect(rt, b, dest, opacity, mode == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                      source, nullptr);
}

static void STDMETHODCALLTYPE rt_draw_text(ID2D1RenderTarget *This, const WCHAR *s, UINT32 len, IDWriteTextFormat *fmt,
                                           const D2D1_RECT_F *rect, ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options,
                                           DWRITE_MEASURING_MODE measuring)
{
    RT;
    D2dDrawText(rt, s, len, fmt, rect, brush, options, measuring);
}

static void STDMETHODCALLTYPE rt_draw_text_layout(ID2D1RenderTarget *This, D2D1_POINT_2F origin, IDWriteTextLayout *layout,
                                                  ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options)
{
    RT;
    D2dDrawTextLayout(rt, origin, layout, brush, options);
}

static void STDMETHODCALLTYPE rt_draw_glyph_run(ID2D1RenderTarget *This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,
                                                ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring)
{
    RT;
    D2dDrawGlyphRun(rt, origin, run, brush, measuring);
}

/* ------------------------------------------------------------------ state */

static void STDMETHODCALLTYPE rt_set_transform(ID2D1RenderTarget *This, const D2D1_MATRIX_3X2_F *m)
{
    RT;
    if (m)
        rt->state.transform = *m;
}
static void STDMETHODCALLTYPE rt_get_transform(ID2D1RenderTarget *This, D2D1_MATRIX_3X2_F *m)
{
    RT;
    *m = rt->state.transform;
}
static void STDMETHODCALLTYPE rt_set_aa(ID2D1RenderTarget *This, D2D1_ANTIALIAS_MODE mode)
{
    RT;
    if (mode <= D2D1_ANTIALIAS_MODE_ALIASED)
        rt->state.aa = mode;
}
static D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE rt_get_aa(ID2D1RenderTarget *This)
{
    RT;
    return rt->state.aa;
}
static void STDMETHODCALLTYPE rt_set_text_aa(ID2D1RenderTarget *This, D2D1_TEXT_ANTIALIAS_MODE mode)
{
    RT;
    if (mode <= D2D1_TEXT_ANTIALIAS_MODE_ALIASED)
        rt->state.text_aa = mode;
}
static D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE rt_get_text_aa(ID2D1RenderTarget *This)
{
    RT;
    return rt->state.text_aa;
}
static void STDMETHODCALLTYPE rt_set_params(ID2D1RenderTarget *This, IDWriteRenderingParams *p)
{
    RT;
    D2D_ADDREF(p);
    D2D_RELEASE(rt->state.params);
    rt->state.params = p;
}
static void STDMETHODCALLTYPE rt_get_params(ID2D1RenderTarget *This, IDWriteRenderingParams **p)
{
    RT;
    *p = rt->state.params;
    D2D_ADDREF(*p);
}
static void STDMETHODCALLTYPE rt_set_tags(ID2D1RenderTarget *This, D2D1_TAG t1, D2D1_TAG t2)
{
    RT;
    rt->state.tag1 = t1;
    rt->state.tag2 = t2;
}
static void STDMETHODCALLTYPE rt_get_tags(ID2D1RenderTarget *This, D2D1_TAG *t1, D2D1_TAG *t2)
{
    RT;
    if (t1)
        *t1 = rt->state.tag1;
    if (t2)
        *t2 = rt->state.tag2;
}

static void STDMETHODCALLTYPE rt_push_layer(ID2D1RenderTarget *This, const D2D1_LAYER_PARAMETERS *p, ID2D1Layer *layer)
{
    RT;
    D2D1_LAYER_PARAMETERS1 q = {p->contentBounds, p->geometricMask, p->maskAntialiasMode, p->maskTransform, p->opacity,
                                p->opacityBrush, D2D1_LAYER_OPTIONS1_NONE};
    D2dPushLayer(rt, &q);
}
static void STDMETHODCALLTYPE rt_pop_layer(ID2D1RenderTarget *This)
{
    RT;
    D2dPopLayer(rt);
}
static HRESULT STDMETHODCALLTYPE rt_flush(ID2D1RenderTarget *This, D2D1_TAG *t1, D2D1_TAG *t2)
{
    RT;
    if (t1)
        *t1 = rt->state.tag1;
    if (t2)
        *t2 = rt->state.tag2;
    return rt->drawing ? S_OK : S_OK;
}

HRESULT D2dStateBlockCreate(D2dFactory *f, const D2D1_DRAWING_STATE_DESCRIPTION1 *desc, IDWriteRenderingParams *params, BOOL v1,
                            ID2D1DrawingStateBlock1 **out);

static D2dClass g_block_class;

static void STDMETHODCALLTYPE rt_save_state(ID2D1RenderTarget *This, ID2D1DrawingStateBlock *block)
{
    RT;
    if (!D2dIsOfClass(block, &g_block_class)) {
        if (!rt->error)
            rt->error = E_INVALIDARG;
        return;
    }
    D2dStateBlock *b = (D2dStateBlock *)block;
    D2D_RELEASE(b->state.params);
    b->state = rt->state;
    D2D_ADDREF(b->state.params);
}
static void STDMETHODCALLTYPE rt_restore_state(ID2D1RenderTarget *This, ID2D1DrawingStateBlock *block)
{
    RT;
    if (!D2dIsOfClass(block, &g_block_class)) {
        if (!rt->error)
            rt->error = E_INVALIDARG;
        return;
    }
    D2dStateBlock *b = (D2dStateBlock *)block;
    D2D_RELEASE(rt->state.params);
    rt->state = b->state;
    D2D_ADDREF(rt->state.params);
}
static void STDMETHODCALLTYPE rt_push_clip(ID2D1RenderTarget *This, const D2D1_RECT_F *r, D2D1_ANTIALIAS_MODE mode)
{
    RT;
    D2dPushAxisAlignedClip(rt, r, mode);
}
static void STDMETHODCALLTYPE rt_pop_clip(ID2D1RenderTarget *This)
{
    RT;
    D2dPopAxisAlignedClip(rt);
}
static void STDMETHODCALLTYPE rt_clear(ID2D1RenderTarget *This, const D2D1_COLOR_F *c)
{
    RT;
    if (!rt->drawing) {
        rt->error = D2DERR_WRONG_STATE;
        return;
    }
    if (rt->draw)
        D2dClearSurface(rt, c);
}
static void STDMETHODCALLTYPE rt_begin_draw(ID2D1RenderTarget *This)
{
    RT;
    if (rt->drawing) {
        /* a second begin leaves the first one standing and reports at the end. */
        rt->error = rt->error ? rt->error : D2DERR_WRONG_STATE;
        return;
    }
    D2dBeginDrawCore(rt);
}
static HRESULT STDMETHODCALLTYPE rt_end_draw(ID2D1RenderTarget *This, D2D1_TAG *t1, D2D1_TAG *t2)
{
    RT;
    if (!rt->drawing)
        return D2DERR_WRONG_STATE;
    return D2dEndDrawCore(rt, t1, t2);
}
static D2D1_PIXEL_FORMAT STDMETHODCALLTYPE rt_get_format(ID2D1RenderTarget *This)
{
    RT;
    return rt->pixel_format;
}
static void STDMETHODCALLTYPE rt_set_dpi(ID2D1RenderTarget *This, FLOAT x, FLOAT y)
{
    RT;
    if (x == 0.0f && y == 0.0f) {
        desktop_dpi(rt);
    } else if (x > 0.0f && y > 0.0f) {
        rt->dpi_x = x;
        rt->dpi_y = y;
    } else {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
    }
}
static void STDMETHODCALLTYPE rt_get_dpi(ID2D1RenderTarget *This, FLOAT *x, FLOAT *y)
{
    RT;
    *x = rt->dpi_x;
    *y = rt->dpi_y;
}
static D2D1_SIZE_F STDMETHODCALLTYPE rt_get_size(ID2D1RenderTarget *This)
{
    RT;
    D2D1_SIZE_F s = {rt->pixel_w * 96.0f / rt->dpi_x, rt->pixel_h * 96.0f / rt->dpi_y};
    return s;
}
static D2D1_SIZE_U STDMETHODCALLTYPE rt_get_pixel_size(ID2D1RenderTarget *This)
{
    RT;
    D2D1_SIZE_U s = {rt->pixel_w, rt->pixel_h};
    return s;
}
static UINT32 STDMETHODCALLTYPE rt_max_bitmap(ID2D1RenderTarget *This) { return 16384; }
static BOOL STDMETHODCALLTYPE rt_is_supported(ID2D1RenderTarget *This, const D2D1_RENDER_TARGET_PROPERTIES *p)
{
    D2dFormat f;
    BOOL s;
    DXGI_FORMAT d;
    D2D1_ALPHA_MODE a;
    return p && props_supported(p, &f, &s, &d, &a) && p->type != D2D1_RENDER_TARGET_TYPE_HARDWARE;
}

const void *const d2d_rt_base[] = {
    D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, rt_create_bitmap, rt_bitmap_from_wic, rt_shared_bitmap,
    rt_bitmap_brush, rt_solid_brush, rt_gradient_stops, rt_linear_brush, rt_radial_brush, rt_compatible, rt_create_layer,
    rt_create_mesh, rt_draw_line, rt_draw_rect, rt_fill_rect, rt_draw_rounded, rt_fill_rounded, rt_draw_ellipse, rt_fill_ellipse,
    rt_draw_geometry, rt_fill_geometry, rt_fill_mesh, rt_fill_opacity_mask, rt_draw_bitmap, rt_draw_text, rt_draw_text_layout,
    rt_draw_glyph_run, rt_set_transform, rt_get_transform, rt_set_aa, rt_get_aa, rt_set_text_aa, rt_get_text_aa, rt_set_params,
    rt_get_params, rt_set_tags, rt_get_tags, rt_push_layer, rt_pop_layer, rt_flush, rt_save_state, rt_restore_state,
    rt_push_clip, rt_pop_clip, rt_clear, rt_begin_draw, rt_end_draw, rt_get_format, rt_set_dpi, rt_get_dpi, rt_get_size,
    rt_get_pixel_size, rt_max_bitmap, rt_is_supported};

#define RT_BASE_LIST                                                                                                   \
    D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, rt_create_bitmap, rt_bitmap_from_wic,             \
        rt_shared_bitmap, rt_bitmap_brush, rt_solid_brush, rt_gradient_stops, rt_linear_brush, rt_radial_brush,        \
        rt_compatible, rt_create_layer, rt_create_mesh, rt_draw_line, rt_draw_rect, rt_fill_rect, rt_draw_rounded,     \
        rt_fill_rounded, rt_draw_ellipse, rt_fill_ellipse, rt_draw_geometry, rt_fill_geometry, rt_fill_mesh,           \
        rt_fill_opacity_mask, rt_draw_bitmap, rt_draw_text, rt_draw_text_layout, rt_draw_glyph_run, rt_set_transform,  \
        rt_get_transform, rt_set_aa, rt_get_aa, rt_set_text_aa, rt_get_text_aa, rt_set_params, rt_get_params,          \
        rt_set_tags, rt_get_tags, rt_push_layer, rt_pop_layer, rt_flush, rt_save_state, rt_restore_state,              \
        rt_push_clip, rt_pop_clip, rt_clear, rt_begin_draw, rt_end_draw, rt_get_format, rt_set_dpi, rt_get_dpi,        \
        rt_get_size, rt_get_pixel_size, rt_max_bitmap, rt_is_supported

/* ------------------------------------------------------------------ the target specific methods */

static HRESULT STDMETHODCALLTYPE brt_get_bitmap(ID2D1BitmapRenderTarget *This, ID2D1Bitmap **out)
{
    RT;
    if (!out)
        return E_POINTER;
    *out = (ID2D1Bitmap *)rt->own_bitmap;
    D2D_ADDREF(*out);
    return S_OK;
}

static D2D1_WINDOW_STATE STDMETHODCALLTYPE hwnd_check(ID2D1HwndRenderTarget *This)
{
    RT;
    if (!IsWindowVisible(rt->hwnd) || IsIconic(rt->hwnd))
        return D2D1_WINDOW_STATE_OCCLUDED;
    return D2D1_WINDOW_STATE_NONE;
}

static HRESULT STDMETHODCALLTYPE hwnd_resize(ID2D1HwndRenderTarget *This, const D2D1_SIZE_U *size)
{
    RT;
    if (!size)
        return E_INVALIDARG;
    if (size->width > 16384 || size->height > 16384)
        return D2DERR_EXCEEDS_MAX_BITMAP_SIZE;
    return D2dRtResize(rt, size->width, size->height);
}

static HWND STDMETHODCALLTYPE hwnd_get(ID2D1HwndRenderTarget *This)
{
    RT;
    return rt->hwnd;
}

static HRESULT STDMETHODCALLTYPE dc_bind(ID2D1DCRenderTarget *This, const HDC dc, const RECT *rect)
{
    RT;
    if (!dc || !rect)
        return E_INVALIDARG;
    if (rt->drawing)
        return D2DERR_WRONG_STATE;
    int w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (w < 0 || h < 0)
        return E_INVALIDARG;
    HRESULT hr = D2dRtResize(rt, (UINT32)w, (UINT32)h);
    if (FAILED(hr))
        return hr;
    rt->dc = dc;
    rt->dc_rect = *rect;
    return S_OK;
}

const void *const d2d_rt_vtbl_plain[] = {RT_BASE_LIST};
const void *const d2d_rt_vtbl_bitmap_rt[] = {RT_BASE_LIST, brt_get_bitmap};
const void *const d2d_rt_vtbl_hwnd_rt[] = {RT_BASE_LIST, hwnd_check, hwnd_resize, hwnd_get};
const void *const d2d_rt_vtbl_dc_rt[] = {RT_BASE_LIST, dc_bind};

/* ------------------------------------------------------------------ gdi interop */

static D2dRt *gdi_rt(void *self) { return (D2dRt *)((char *)self - offsetof(D2dRt, gdi_vtbl)); }

static HRESULT STDMETHODCALLTYPE gdi_query(void *This, REFIID riid, void **out) { return D2dQueryInterface(gdi_rt(This), riid, out); }
static ULONG STDMETHODCALLTYPE gdi_addref(void *This) { return D2dAddRef(gdi_rt(This)); }
static ULONG STDMETHODCALLTYPE gdi_release(void *This) { return D2dRelease(gdi_rt(This)); }

static HRESULT STDMETHODCALLTYPE gdi_get_dc(void *This, D2D1_DC_INITIALIZE_MODE mode, HDC *out)
{
    D2dRt *rt = gdi_rt(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (mode > D2D1_DC_INITIALIZE_MODE_CLEAR)
        return E_INVALIDARG;
    if (!rt->gdi_compatible)
        return D2DERR_TARGET_NOT_GDI_COMPATIBLE;
    if (!rt->drawing || rt->interop_dc || !rt->draw)
        return D2DERR_WRONG_STATE;
    if (!dib_dc(rt->draw->w, rt->draw->h, &rt->interop_dc, &rt->interop_bitmap, &rt->interop_bits, &rt->interop_old))
        return E_OUTOFMEMORY;
    if (mode == D2D1_DC_INITIALIZE_MODE_COPY)
        memcpy(rt->interop_bits, rt->draw->px, (SIZE_T)rt->draw->w * rt->draw->h * 4);
    else
        memset(rt->interop_bits, 0, (SIZE_T)rt->draw->w * rt->draw->h * 4);
    rt->interop_mode = mode;
    *out = rt->interop_dc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gdi_release_dc(void *This, const RECT *update)
{
    D2dRt *rt = gdi_rt(This);
    if (!rt->interop_dc)
        return D2DERR_WRONG_STATE;
    GdiFlush();
    int w = rt->draw->w, h = rt->draw->h;
    RECT r = {0, 0, w, h};
    if (update) {
        r = *update;
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.right > w) r.right = w;
        if (r.bottom > h) r.bottom = h;
    }
    const uint32_t *src = rt->interop_bits;
    for (int y = r.top; y < r.bottom; y++) {
        for (int x = r.left; x < r.right; x++) {
            uint32_t p = src[(SIZE_T)y * w + x];
            if (rt->pixel_format.alphaMode == D2D1_ALPHA_MODE_IGNORE || ((p >> 24) == 0 && (p & 0xffffff)))
                p |= 0xff000000u;
            D2dSurfaceRow(rt->draw, y)[x] = p;
        }
    }
    SelectObject(rt->interop_dc, rt->interop_old);
    DeleteObject(rt->interop_bitmap);
    DeleteDC(rt->interop_dc);
    rt->interop_dc = nullptr;
    rt->interop_bitmap = nullptr;
    return S_OK;
}

const void *const d2d_gdi_interop_vtbl[] = {gdi_query, gdi_addref, gdi_release, gdi_get_dc, gdi_release_dc};

/* ------------------------------------------------------------------ drawing state blocks */

#define BLK D2dStateBlock *b = (D2dStateBlock *)This

static void blk_destroy(D2dObj *o) { D2D_RELEASE(((D2dStateBlock *)o)->state.params); }

static void STDMETHODCALLTYPE blk_get(ID2D1DrawingStateBlock1 *This, D2D1_DRAWING_STATE_DESCRIPTION *d)
{
    BLK;
    d->antialiasMode = b->state.aa;
    d->textAntialiasMode = b->state.text_aa;
    d->tag1 = b->state.tag1;
    d->tag2 = b->state.tag2;
    d->transform = b->state.transform;
}
static void STDMETHODCALLTYPE blk_set(ID2D1DrawingStateBlock1 *This, const D2D1_DRAWING_STATE_DESCRIPTION *d)
{
    BLK;
    b->state.aa = d->antialiasMode;
    b->state.text_aa = d->textAntialiasMode;
    b->state.tag1 = d->tag1;
    b->state.tag2 = d->tag2;
    b->state.transform = d->transform;
}
static void STDMETHODCALLTYPE blk_set_params(ID2D1DrawingStateBlock1 *This, IDWriteRenderingParams *p)
{
    BLK;
    D2D_ADDREF(p);
    D2D_RELEASE(b->state.params);
    b->state.params = p;
}
static void STDMETHODCALLTYPE blk_get_params(ID2D1DrawingStateBlock1 *This, IDWriteRenderingParams **p)
{
    BLK;
    *p = b->state.params;
    D2D_ADDREF(*p);
}
static void STDMETHODCALLTYPE blk_get1(ID2D1DrawingStateBlock1 *This, D2D1_DRAWING_STATE_DESCRIPTION1 *d)
{
    BLK;
    d->antialiasMode = b->state.aa;
    d->textAntialiasMode = b->state.text_aa;
    d->tag1 = b->state.tag1;
    d->tag2 = b->state.tag2;
    d->transform = b->state.transform;
    d->primitiveBlend = b->state.blend;
    d->unitMode = b->state.units;
}
static void STDMETHODCALLTYPE blk_set1(ID2D1DrawingStateBlock1 *This, const D2D1_DRAWING_STATE_DESCRIPTION1 *d)
{
    BLK;
    b->state.aa = d->antialiasMode;
    b->state.text_aa = d->textAntialiasMode;
    b->state.tag1 = d->tag1;
    b->state.tag2 = d->tag2;
    b->state.transform = d->transform;
    b->state.blend = d->primitiveBlend;
    b->state.units = d->unitMode;
}

static const void *const g_block_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, blk_get, blk_set,
                                           blk_set_params, blk_get_params, blk_get1, blk_set1};
D2D_IIDS(g_block_iids, &IID_ID2D1Resource, &IID_ID2D1DrawingStateBlock, &IID_ID2D1DrawingStateBlock1);
static D2dClass g_block_class = {"drawing state block", g_block_iids, blk_destroy, nullptr};

HRESULT D2dStateBlockCreate(D2dFactory *f, const D2D1_DRAWING_STATE_DESCRIPTION1 *desc, IDWriteRenderingParams *params, BOOL v1,
                            ID2D1DrawingStateBlock1 **out)
{
    D2dStateBlock *b = D2dNew(sizeof(*b), g_block_vtbl, &g_block_class, f);
    if (!b)
        return E_OUTOFMEMORY;
    b->v1 = v1;
    b->state.aa = desc->antialiasMode;
    b->state.text_aa = desc->textAntialiasMode;
    b->state.tag1 = desc->tag1;
    b->state.tag2 = desc->tag2;
    b->state.transform = desc->transform;
    b->state.blend = desc->primitiveBlend;
    b->state.units = desc->unitMode;
    b->state.params = params;
    D2D_ADDREF(params);
    *out = (ID2D1DrawingStateBlock1 *)b;
    return S_OK;
}
