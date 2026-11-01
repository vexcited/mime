/*
 * the object base every direct2d object shares, and the factory.
 */
#include "d2d_private.h"
#include <stddef.h>

/* ------------------------------------------------------------------ object base */

static LONG g_object_count;

void *D2dNew(SIZE_T size, const void *vtbl, const D2dClass *cls, D2dFactory *factory)
{
    D2dObj *o = D2dAllocZero(size);
    if (!o)
        return nullptr;
    o->vtbl = vtbl;
    o->refs = 1;
    o->cls = cls;
    o->factory = factory;
    if (factory)
        D2dAddRef(factory);
    InterlockedIncrement(&g_object_count);
    return o;
}

HRESULT STDMETHODCALLTYPE D2dQueryInterface(void *self, REFIID riid, void **out)
{
    D2dObj *o = self;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!riid)
        return E_INVALIDARG;
    for (const GUID *const *p = o->cls->iids; *p; p++) {
        if (IsEqualGUID(riid, *p)) {
            *out = o;
            D2dAddRef(o);
            return S_OK;
        }
    }
    if (o->cls->query)
        return o->cls->query(o, riid, out);
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE D2dAddRef(void *self) { return (ULONG)InterlockedIncrement(&((D2dObj *)self)->refs); }

ULONG STDMETHODCALLTYPE D2dRelease(void *self)
{
    D2dObj *o = self;
    LONG refs = InterlockedDecrement(&o->refs);
    if (refs == 0) {
        D2dFactory *factory = o->factory;
        o->refs = 0x40000000;
        if (o->cls->destroy)
            o->cls->destroy(o);
        D2dFree(o);
        InterlockedDecrement(&g_object_count);
        if (factory)
            D2dRelease(factory);
    }
    return (ULONG)refs;
}

void STDMETHODCALLTYPE D2dResourceGetFactory(void *self, ID2D1Factory **factory)
{
    D2dObj *o = self;
    *factory = (ID2D1Factory *)o->factory;
    if (o->factory)
        D2dAddRef(o->factory);
}

BOOL D2dIsOfClass(const void *p, const D2dClass *cls) { return p && ((const D2dObj *)p)->cls == cls; }

BOOL D2dSameFactory(const void *a, const void *b)
{
    return a && b && ((const D2dObj *)a)->factory == ((const D2dObj *)b)->factory;
}

/* ------------------------------------------------------------------ the factory */

D2D_IIDS(g_factory_iids, &IID_ID2D1Factory, &IID_ID2D1Factory1, &IID_ID2D1Factory2, &IID_ID2D1Factory3,
         &IID_ID2D1Factory4, &IID_ID2D1Factory5, &IID_ID2D1Factory6, &IID_ID2D1Factory7);

static HRESULT factory_query_extra(D2dObj *obj, REFIID riid, void **out)
{
    D2dFactory *f = (D2dFactory *)obj;
    if (IsEqualGUID(riid, &IID_ID2D1Multithread)) {
        *out = &f->multithread_vtbl;
        D2dAddRef(f);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static void factory_destroy(D2dObj *obj)
{
    D2dFactory *f = (D2dFactory *)obj;
    D2dArrayFree(&f->effects);
    DeleteCriticalSection(&f->lock);
}

const D2dClass d2d_factory_class = {"factory", g_factory_iids, factory_destroy, factory_query_extra};

D2dFactory *D2dFactoryOf(ID2D1Factory *f)
{
    return D2dIsOfClass(f, &d2d_factory_class) ? (D2dFactory *)f : nullptr;
}

void D2dFactoryEnter(D2dFactory *f)
{
    if (f->type == D2D1_FACTORY_TYPE_MULTI_THREADED)
        EnterCriticalSection(&f->lock);
}

void D2dFactoryLeave(D2dFactory *f)
{
    if (f->type == D2D1_FACTORY_TYPE_MULTI_THREADED)
        LeaveCriticalSection(&f->lock);
}

/* the multithread interface lives inside the factory. */
static D2dFactory *mt_factory(void *self) { return (D2dFactory *)((char *)self - offsetof(D2dFactory, multithread_vtbl)); }

static HRESULT STDMETHODCALLTYPE mt_query(ID2D1Multithread *This, REFIID riid, void **out)
{
    return D2dQueryInterface(mt_factory(This), riid, out);
}
static ULONG STDMETHODCALLTYPE mt_addref(ID2D1Multithread *This) { return D2dAddRef(mt_factory(This)); }
static ULONG STDMETHODCALLTYPE mt_release(ID2D1Multithread *This) { return D2dRelease(mt_factory(This)); }
static BOOL STDMETHODCALLTYPE mt_protected(ID2D1Multithread *This)
{
    return mt_factory(This)->type == D2D1_FACTORY_TYPE_MULTI_THREADED;
}
static void STDMETHODCALLTYPE mt_enter(ID2D1Multithread *This) { D2dFactoryEnter(mt_factory(This)); }
static void STDMETHODCALLTYPE mt_leave(ID2D1Multithread *This) { D2dFactoryLeave(mt_factory(This)); }

static const ID2D1MultithreadVtbl g_mt_vtbl = {mt_query, mt_addref, mt_release, mt_protected, mt_enter, mt_leave};

/* ------------------------------------------------------------------ factory methods */

#define SELF D2dFactory *f = (D2dFactory *)This

static HRESULT STDMETHODCALLTYPE factory_reload(ID2D1Factory7 *This) { return S_OK; }

static void STDMETHODCALLTYPE factory_dpi(ID2D1Factory7 *This, FLOAT *x, FLOAT *y)
{
    SELF;
    if (!f->dpi_x) {
        HDC dc = GetDC(nullptr);
        int dx = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        int dy = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc)
            ReleaseDC(nullptr, dc);
        f->dpi_x = dx ? (float)dx : 96.0f;
        f->dpi_y = dy ? (float)dy : 96.0f;
    }
    *x = f->dpi_x;
    *y = f->dpi_y;
}

static HRESULT STDMETHODCALLTYPE factory_rect(ID2D1Factory7 *This, const D2D1_RECT_F *rect, ID2D1RectangleGeometry **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!rect)
        return E_INVALIDARG;
    return D2dCreateRectGeometry((D2dFactory *)This, rect, out);
}

static HRESULT STDMETHODCALLTYPE factory_rounded(ID2D1Factory7 *This, const D2D1_ROUNDED_RECT *rect,
                                                 ID2D1RoundedRectangleGeometry **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!rect)
        return E_INVALIDARG;
    return D2dCreateRoundedGeometry((D2dFactory *)This, rect, out);
}

static HRESULT STDMETHODCALLTYPE factory_ellipse(ID2D1Factory7 *This, const D2D1_ELLIPSE *e, ID2D1EllipseGeometry **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!e)
        return E_INVALIDARG;
    return D2dCreateEllipseGeometry((D2dFactory *)This, e, out);
}

static HRESULT STDMETHODCALLTYPE factory_group(ID2D1Factory7 *This, D2D1_FILL_MODE mode, ID2D1Geometry **list,
                                               UINT32 count, ID2D1GeometryGroup **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if ((count && !list) || (mode != D2D1_FILL_MODE_ALTERNATE && mode != D2D1_FILL_MODE_WINDING))
        return E_INVALIDARG;
    return D2dCreateGroupGeometry((D2dFactory *)This, mode, list, count, out);
}

static HRESULT STDMETHODCALLTYPE factory_transformed(ID2D1Factory7 *This, ID2D1Geometry *source,
                                                     const D2D1_MATRIX_3X2_F *m, ID2D1TransformedGeometry **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!source || !m)
        return E_INVALIDARG;
    return D2dCreateTransformedGeometry((D2dFactory *)This, source, m, out);
}

static HRESULT STDMETHODCALLTYPE factory_path(ID2D1Factory7 *This, ID2D1PathGeometry **out)
{
    if (!out)
        return E_POINTER;
    return D2dCreatePathGeometry((D2dFactory *)This, (ID2D1PathGeometry1 **)out);
}

static HRESULT STDMETHODCALLTYPE factory_path1(ID2D1Factory7 *This, ID2D1PathGeometry1 **out)
{
    if (!out)
        return E_POINTER;
    return D2dCreatePathGeometry((D2dFactory *)This, out);
}

static HRESULT stroke_style(D2dFactory *f, const D2D1_STROKE_STYLE_PROPERTIES1 *p, BOOL v1, const FLOAT *dashes,
                            UINT32 count, ID2D1StrokeStyle1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!p)
        return E_INVALIDARG;
    if (p->startCap > D2D1_CAP_STYLE_TRIANGLE || p->endCap > D2D1_CAP_STYLE_TRIANGLE ||
        p->dashCap > D2D1_CAP_STYLE_TRIANGLE || p->lineJoin > D2D1_LINE_JOIN_MITER_OR_BEVEL ||
        p->dashStyle > D2D1_DASH_STYLE_CUSTOM || (v1 && p->transformType > D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE))
        return E_INVALIDARG;
    if (p->dashStyle == D2D1_DASH_STYLE_CUSTOM) {
        if (!dashes || !count)
            return E_INVALIDARG;
        for (UINT32 i = 0; i < count; i++) {
            if (dashes[i] < 0 || !D2dFinite(dashes[i]))
                return E_INVALIDARG;
        }
    } else if (dashes || count) {
        return E_INVALIDARG;
    }
    if (p->miterLimit < 1.0f && p->miterLimit == p->miterLimit && false)
        return E_INVALIDARG;
    return D2dCreateStrokeStyle(f, p, v1, dashes, count, out);
}

static HRESULT STDMETHODCALLTYPE factory_stroke(ID2D1Factory7 *This, const D2D1_STROKE_STYLE_PROPERTIES *p,
                                                const FLOAT *dashes, UINT32 count, ID2D1StrokeStyle **out)
{
    D2D1_STROKE_STYLE_PROPERTIES1 q = {};
    if (p) {
        q.startCap = p->startCap;
        q.endCap = p->endCap;
        q.dashCap = p->dashCap;
        q.lineJoin = p->lineJoin;
        q.miterLimit = p->miterLimit;
        q.dashStyle = p->dashStyle;
        q.dashOffset = p->dashOffset;
    }
    return stroke_style((D2dFactory *)This, p ? &q : nullptr, FALSE, dashes, count, (ID2D1StrokeStyle1 **)out);
}

static HRESULT STDMETHODCALLTYPE factory_stroke1(ID2D1Factory7 *This, const D2D1_STROKE_STYLE_PROPERTIES1 *p,
                                                 const FLOAT *dashes, UINT32 count, ID2D1StrokeStyle1 **out)
{
    return stroke_style((D2dFactory *)This, p, TRUE, dashes, count, out);
}

HRESULT D2dStateBlockCreate(D2dFactory *f, const D2D1_DRAWING_STATE_DESCRIPTION1 *desc, IDWriteRenderingParams *params,
                            BOOL v1, ID2D1DrawingStateBlock1 **out);

static HRESULT STDMETHODCALLTYPE factory_state(ID2D1Factory7 *This, const D2D1_DRAWING_STATE_DESCRIPTION *desc,
                                               IDWriteRenderingParams *params, ID2D1DrawingStateBlock **out)
{
    if (!out)
        return E_POINTER;
    D2D1_DRAWING_STATE_DESCRIPTION1 d = {};
    d.transform = d2d_identity;
    if (desc) {
        d.antialiasMode = desc->antialiasMode;
        d.textAntialiasMode = desc->textAntialiasMode;
        d.tag1 = desc->tag1;
        d.tag2 = desc->tag2;
        d.transform = desc->transform;
    }
    return D2dStateBlockCreate((D2dFactory *)This, &d, params, FALSE, (ID2D1DrawingStateBlock1 **)out);
}

static HRESULT STDMETHODCALLTYPE factory_state1(ID2D1Factory7 *This, const D2D1_DRAWING_STATE_DESCRIPTION1 *desc,
                                                IDWriteRenderingParams *params, ID2D1DrawingStateBlock1 **out)
{
    if (!out)
        return E_POINTER;
    D2D1_DRAWING_STATE_DESCRIPTION1 d = {};
    d.transform = d2d_identity;
    if (desc)
        d = *desc;
    return D2dStateBlockCreate((D2dFactory *)This, &d, params, TRUE, out);
}

HRESULT D2dRtCreateWic(D2dFactory *f, IWICBitmap *bitmap, const D2D1_RENDER_TARGET_PROPERTIES *props,
                       ID2D1RenderTarget **out);
HRESULT D2dRtCreateHwnd(D2dFactory *f, const D2D1_RENDER_TARGET_PROPERTIES *props,
                        const D2D1_HWND_RENDER_TARGET_PROPERTIES *hwnd_props, ID2D1HwndRenderTarget **out);
HRESULT D2dRtCreateDxgi(D2dFactory *f, IDXGISurface *surface, const D2D1_RENDER_TARGET_PROPERTIES *props,
                        ID2D1RenderTarget **out);
HRESULT D2dRtCreateDC(D2dFactory *f, const D2D1_RENDER_TARGET_PROPERTIES *props, ID2D1DCRenderTarget **out);
HRESULT D2dDeviceCreate(D2dFactory *f, IDXGIDevice *dxgi, ID2D1Device **out);

static HRESULT STDMETHODCALLTYPE factory_wic(ID2D1Factory7 *This, IWICBitmap *bitmap,
                                             const D2D1_RENDER_TARGET_PROPERTIES *props, ID2D1RenderTarget **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!bitmap || !props)
        return E_INVALIDARG;
    return D2dRtCreateWic((D2dFactory *)This, bitmap, props, out);
}

static HRESULT STDMETHODCALLTYPE factory_hwnd(ID2D1Factory7 *This, const D2D1_RENDER_TARGET_PROPERTIES *props,
                                              const D2D1_HWND_RENDER_TARGET_PROPERTIES *hp,
                                              ID2D1HwndRenderTarget **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!props || !hp)
        return E_INVALIDARG;
    return D2dRtCreateHwnd((D2dFactory *)This, props, hp, out);
}

static HRESULT STDMETHODCALLTYPE factory_dxgi(ID2D1Factory7 *This, IDXGISurface *surface,
                                              const D2D1_RENDER_TARGET_PROPERTIES *props, ID2D1RenderTarget **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!surface || !props)
        return E_INVALIDARG;
    return D2dRtCreateDxgi((D2dFactory *)This, surface, props, out);
}

static HRESULT STDMETHODCALLTYPE factory_dc(ID2D1Factory7 *This, const D2D1_RENDER_TARGET_PROPERTIES *props,
                                            ID2D1DCRenderTarget **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!props)
        return E_INVALIDARG;
    return D2dRtCreateDC((D2dFactory *)This, props, out);
}

static HRESULT STDMETHODCALLTYPE factory_device(ID2D1Factory7 *This, IDXGIDevice *dxgi, ID2D1Device **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!dxgi)
        return E_INVALIDARG;
    return D2dDeviceCreate((D2dFactory *)This, dxgi, out);
}

static HRESULT STDMETHODCALLTYPE factory_metafile(ID2D1Factory7 *This, IStream *stream, ID2D1GdiMetafile **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE factory_register_stream(ID2D1Factory7 *This, REFCLSID clsid, IStream *xml,
                                                         const void *bindings, UINT32 count, void *factory)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE factory_register_string(ID2D1Factory7 *This, REFCLSID clsid, const WCHAR *xml,
                                                         const void *bindings, UINT32 count, void *factory)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE factory_unregister(ID2D1Factory7 *This, REFCLSID clsid)
{
    return D2dEffectUnregister((D2dFactory *)This, clsid);
}

static HRESULT STDMETHODCALLTYPE factory_registered(ID2D1Factory7 *This, CLSID *effects, UINT32 count,
                                                    UINT32 *returned, UINT32 *registered)
{
    return D2dEffectEnumerate((D2dFactory *)This, effects, count, returned, registered);
}

static HRESULT STDMETHODCALLTYPE factory_effect_props(ID2D1Factory7 *This, REFCLSID clsid, ID2D1Properties **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!clsid)
        return E_INVALIDARG;
    return D2dEffectProperties((D2dFactory *)This, clsid, out);
}

static const void *const g_factory_vtbl[] = {
    D2dQueryInterface,   D2dAddRef,           D2dRelease,         factory_reload,     factory_dpi,
    factory_rect,        factory_rounded,     factory_ellipse,    factory_group,      factory_transformed,
    factory_path,        factory_stroke,      factory_state,      factory_wic,        factory_hwnd,
    factory_dxgi,        factory_dc,          factory_device,     factory_stroke1,    factory_path1,
    factory_state1,      factory_metafile,    factory_register_stream, factory_register_string, factory_unregister,
    factory_registered,  factory_effect_props,
    /* each later factory only adds a CreateDevice returning a newer device. */
    factory_device,      factory_device,      factory_device,     factory_device,     factory_device,
    factory_device};

HRESULT D2dFactoryCreate(D2D1_FACTORY_TYPE type, const D2D1_FACTORY_OPTIONS *options, REFIID riid, void **out)
{
    D2dFactory *f = D2dNew(sizeof(*f), g_factory_vtbl, &d2d_factory_class, nullptr);
    if (!f)
        return E_OUTOFMEMORY;
    f->multithread_vtbl = &g_mt_vtbl;
    f->type = type;
    f->debug = options ? options->debugLevel : D2D1_DEBUG_LEVEL_NONE;
    InitializeCriticalSection(&f->lock);
    HRESULT hr = D2dQueryInterface(f, riid, out);
    D2dRelease(f);
    return hr;
}
