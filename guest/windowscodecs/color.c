/*
 * IWICColorContext stores profiles and color spaces. IWICColorTransform converts colors from one context to another
 * with the icc engine, then to the pixel format asked for.
 */
#include "wic_private.h"
#include <icc/icc.h>

typedef struct WicColorContext {
    WicBase base;
    WicSlot context;
    CRITICAL_SECTION lock;
    WICColorContextType type;
    BYTE *profile;
    UINT size;
    UINT exif_space;
} WicColorContext;

static WicColorContext *context_from(IWICColorContext *This)
{
    return WIC_FROM_SLOT(WicColorContext, context, This);
}

static void context_destroy(WicBase *base)
{
    WicColorContext *c = (WicColorContext *)base;
    WicFree(c->profile);
    DeleteCriticalSection(&c->lock);
}

static HRESULT set_profile(WicColorContext *c, const BYTE *data, UINT size)
{
    BYTE *copy = WicAlloc(size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, data, size);
    EnterCriticalSection(&c->lock);
    WicFree(c->profile);
    c->profile = copy;
    c->size = size;
    c->type = WICColorContextProfile;
    c->exif_space = 0;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE context_init_filename(IWICColorContext *This, LPCWSTR name)
{
    WicColorContext *c = context_from(This);
    if (!name)
        return E_INVALIDARG;
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    HRESULT hr = E_FAIL;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 0x10000000) {
        BYTE *data = WicAlloc((SIZE_T)size.QuadPart);
        DWORD got = 0;
        if (data && ReadFile(file, data, (DWORD)size.QuadPart, &got, nullptr) && got == (DWORD)size.QuadPart)
            hr = set_profile(c, data, got);
        else if (!data)
            hr = E_OUTOFMEMORY;
        WicFree(data);
    }
    CloseHandle(file);
    return hr;
}

static HRESULT STDMETHODCALLTYPE context_init_memory(IWICColorContext *This, const BYTE *buffer, UINT size)
{
    WicColorContext *c = context_from(This);
    if (!buffer || !size)
        return E_INVALIDARG;
    return set_profile(c, buffer, size);
}

static HRESULT STDMETHODCALLTYPE context_init_exif(IWICColorContext *This, UINT value)
{
    WicColorContext *c = context_from(This);
    /* EXIF color space: 1 is sRGB, 2 is Adobe RGB, 0xffff is uncalibrated. */
    if (value != 1 && value != 2)
        return E_INVALIDARG;
    EnterCriticalSection(&c->lock);
    WicFree(c->profile);
    c->profile = nullptr;
    c->size = 0;
    c->type = WICColorContextExifColorSpace;
    c->exif_space = value;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE context_get_type(IWICColorContext *This, WICColorContextType *type)
{
    WicColorContext *c = context_from(This);
    if (!type)
        return E_INVALIDARG;
    *type = c->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE context_get_profile(IWICColorContext *This, UINT size, BYTE *buffer, UINT *actual)
{
    WicColorContext *c = context_from(This);
    if (!actual)
        return E_INVALIDARG;
    if (c->type == WICColorContextUninitialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (c->type != WICColorContextProfile)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    EnterCriticalSection(&c->lock);
    HRESULT hr = S_OK;
    if (buffer) {
        if (size < c->size)
            hr = WINCODEC_ERR_INSUFFICIENTBUFFER;
        else
            memcpy(buffer, c->profile, c->size);
    }
    *actual = c->size;
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE context_get_exif(IWICColorContext *This, UINT *value)
{
    WicColorContext *c = context_from(This);
    if (!value)
        return E_INVALIDARG;
    if (c->type == WICColorContextUninitialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (c->type != WICColorContextExifColorSpace)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    *value = c->exif_space;
    return S_OK;
}

static const IWICColorContextVtbl g_context_vtbl = {
    WIC_IUNKNOWN_FUNCS, context_init_filename, context_init_memory, context_init_exif, context_get_type,
    context_get_profile, context_get_exif,
};
static const WicIfaceEntry g_context_ifaces[] = {WIC_ENTRY(WicColorContext, IID_IWICColorContext, context)};
static const WicClass g_context_class = {context_destroy, g_context_ifaces, 1};

HRESULT WicColorContextCreate(IWICColorContext **out)
{
    WicColorContext *c = WicNew(sizeof(*c), &g_context_class);
    if (!c)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&c->lock);
    WIC_SLOT(c, context, &g_context_vtbl);
    *out = (IWICColorContext *)&c->context;
    return S_OK;
}


typedef struct WicColorTransform {
    WicBase base;
    WicSlot slot;
    /* the source in the format the transform reads, rgb, cmyk or gray of 8 bits. */
    IWICBitmapSource *converted;
    IccXform *xform;
    GUID format;
    BOOL ready;
} WicColorTransform;

static WicColorTransform *ctrans_from(IWICColorTransform *This)
{
    return WIC_FROM_SLOT(WicColorTransform, slot, This);
}

static void ctrans_destroy(WicBase *base)
{
    WicColorTransform *t = (WicColorTransform *)base;
    if (t->converted)
        t->converted->lpVtbl->Release(t->converted);
    icc_xform_destroy(t->xform);
}

static HRESULT STDMETHODCALLTYPE ctrans_get_size(IWICColorTransform *This, UINT *width, UINT *height)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->GetSize(t->converted, width, height) : WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ctrans_get_format(IWICColorTransform *This, WICPixelFormatGUID *format)
{
    WicColorTransform *t = ctrans_from(This);
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!format)
        return E_INVALIDARG;
    *format = t->format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ctrans_get_resolution(IWICColorTransform *This, double *x, double *y)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->GetResolution(t->converted, x, y) : WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ctrans_copy_palette(IWICColorTransform *This, IWICPalette *palette)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? WINCODEC_ERR_PALETTEUNAVAILABLE : WINCODEC_ERR_NOTINITIALIZED;
}

static const GUID *format_of_channels(UINT n)
{
    return n == 4 ? &GUID_WICPixelFormat32bppCMYK : n == 1 ? &GUID_WICPixelFormat8bppGray : &GUID_WICPixelFormat32bppBGRA;
}

/*
 * reads the rectangle from the source in the middle format, converts the colors into a row of the format the profile
 * of the destination has, alpha kept, and that row to the format asked for.
 */
static HRESULT STDMETHODCALLTYPE ctrans_copy_pixels(IWICColorTransform *This, const WICRect *rect, UINT stride,
                                                    UINT size, BYTE *buffer)
{
    WicColorTransform *t = ctrans_from(This);
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    UINT width, height;
    HRESULT hr = t->converted->lpVtbl->GetSize(t->converted, &width, &height);
    WICRect r;
    if (SUCCEEDED(hr))
        hr = WicCheckRect(rect, width, height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &t->format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    UINT ni = icc_xform_inputs(t->xform), no = icc_xform_outputs(t->xform);
    UINT in_bpp = ni == 3 ? 4 : ni, out_bpp = no == 3 ? 4 : no, w = (UINT)r.Width;
    const GUID *out_format = format_of_channels(no);
    BYTE *row = WicAlloc((SIZE_T)w * (in_bpp + out_bpp + ni + no));
    if (!row)
        return E_OUTOFMEMORY;
    BYTE *src = row, *dst = src + (SIZE_T)w * in_bpp, *a = dst + (SIZE_T)w * out_bpp, *b = a + (SIZE_T)w * ni;
    for (INT y = 0; y < r.Height && SUCCEEDED(hr); y++) {
        WICRect line = {r.X, r.Y + y, r.Width, 1};
        hr = t->converted->lpVtbl->CopyPixels(t->converted, &line, w * in_bpp, w * in_bpp, src);
        if (FAILED(hr))
            break;
        for (UINT i = 0; i < w; i++) {
            if (ni == 3) {
                a[i * 3] = src[i * 4 + 2];
                a[i * 3 + 1] = src[i * 4 + 1];
                a[i * 3 + 2] = src[i * 4];
            } else {
                memcpy(a + i * ni, src + i * in_bpp, ni);
            }
        }
        icc_xform_eval8(t->xform, a, ni, b, no, w);
        for (UINT i = 0; i < w; i++) {
            if (no == 3) {
                dst[i * 4] = b[i * 3 + 2];
                dst[i * 4 + 1] = b[i * 3 + 1];
                dst[i * 4 + 2] = b[i * 3];
                dst[i * 4 + 3] = ni == 3 ? src[i * 4 + 3] : 255;
            } else {
                memcpy(dst + i * no, b + i * no, no);
            }
        }
        hr = WicConvertRows(out_format, &t->format, dst, w * out_bpp, buffer + (SIZE_T)y * stride, stride, w, 1, nullptr,
                            0, nullptr, 0, WICBitmapDitherTypeNone, 0, 0, 0.0);
    }
    WicFree(row);
    return hr;
}

static void *icc_alloc_fn(void *ctx, size_t size) { return WicAlloc(size ? size : 1); }
static void icc_free_fn(void *ctx, void *p) { WicFree(p); }
static const MimeAllocator g_icc_alloc = {icc_alloc_fn, icc_free_fn, nullptr};

/* the profile of a context: its own bytes, or srgb and adobe rgb for the exif spaces, made ones in *made. */
static const BYTE *context_bytes(WicColorContext *c, UINT *size, BYTE **made)
{
    *made = nullptr;
    if (c->type == WICColorContextProfile) {
        *size = c->size;
        return c->profile;
    }
    if (c->type != WICColorContextExifColorSpace)
        return nullptr;
    uint32_t n = 0;
    if (c->exif_space == 1) {
        *made = icc_build_srgb(&g_icc_alloc, &n);
    } else {
        static const double d65[3] = {0.9505, 1.0, 1.0891};
        IccRgbSpec spec = {.primaries = {{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}},
                           .white = {0.3127, 0.3290},
                           .media_white = d65,
                           .curve = ICC_CURVE_GAMMA,
                           .gamma = 563.0 / 256.0,
                           .version = 2,
                           .description = "Adobe RGB (1998)"};
        *made = icc_build_rgb(&g_icc_alloc, &spec, &n);
    }
    *size = n;
    return *made;
}

static HRESULT STDMETHODCALLTYPE ctrans_initialize(IWICColorTransform *This, IWICBitmapSource *source,
                                                   IWICColorContext *source_context, IWICColorContext *dest_context,
                                                   REFWICPixelFormatGUID dest_format)
{
    WicColorTransform *t = ctrans_from(This);
    if (!source || !source_context || !dest_context || !dest_format)
        return E_INVALIDARG;
    if (t->ready)
        return WINCODEC_ERR_WRONGSTATE;
    WicColorContext *sc = context_from(source_context), *dc = context_from(dest_context);
    EnterCriticalSection(&sc->lock);
    if (dc != sc)
        EnterCriticalSection(&dc->lock);
    BYTE *made_src, *made_dst;
    UINT src_size = 0, dst_size = 0;
    const BYTE *a = context_bytes(sc, &src_size, &made_src), *b = context_bytes(dc, &dst_size, &made_dst);
    int error = ICC_ERR_ARGUMENT;
    IccXform *x = nullptr;
    if (a && b) {
        IccSource sources[2] = {{a, src_size}, {b, dst_size}};
        uint32_t intent = ICC_INTENT_PERCEPTUAL;
        x = icc_xform_create(&g_icc_alloc, sources, 2, &intent, 1, 0, &error);
    }
    if (dc != sc)
        LeaveCriticalSection(&dc->lock);
    LeaveCriticalSection(&sc->lock);
    WicFree(made_src);
    WicFree(made_dst);
    if (!x)
        return error == ICC_ERR_MEMORY ? E_OUTOFMEMORY : a && b ? WINCODEC_ERR_BADIMAGE : WINCODEC_ERR_NOTINITIALIZED;
    UINT ni = icc_xform_inputs(x), no = icc_xform_outputs(x);
    if ((ni != 1 && ni != 3 && ni != 4) || (no != 1 && no != 3 && no != 4) || !icc_xform_prepare8(x) ||
        !WicCanConvert(format_of_channels(no), dest_format)) {
        icc_xform_destroy(x);
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT hr = WicEnsureFormat(source, format_of_channels(ni), &t->converted);
    if (FAILED(hr)) {
        icc_xform_destroy(x);
        return hr;
    }
    t->xform = x;
    t->format = *dest_format;
    t->ready = TRUE;
    return S_OK;
}

static const IWICColorTransformVtbl g_ctrans_vtbl = {
    WIC_IUNKNOWN_FUNCS, VTBL_FN(ctrans_get_size), VTBL_FN(ctrans_get_format), VTBL_FN(ctrans_get_resolution),
    VTBL_FN(ctrans_copy_palette), VTBL_FN(ctrans_copy_pixels), ctrans_initialize,
};
static const WicIfaceEntry g_ctrans_ifaces[] = {
    WIC_ENTRY(WicColorTransform, IID_IWICColorTransform, slot),
    WIC_ENTRY(WicColorTransform, IID_IWICBitmapSource, slot),
};
static const WicClass g_ctrans_class = {ctrans_destroy, g_ctrans_ifaces, 2};

HRESULT WicColorTransformCreate(IWICColorTransform **out)
{
    WicColorTransform *t = WicNew(sizeof(*t), &g_ctrans_class);
    if (!t)
        return E_OUTOFMEMORY;
    WIC_SLOT(t, slot, &g_ctrans_vtbl);
    *out = (IWICColorTransform *)&t->slot;
    return S_OK;
}
