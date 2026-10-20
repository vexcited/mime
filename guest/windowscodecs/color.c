/*
 * IWICColorContext, which holds a profile or the name of a color space, and IWICColorTransform, which passes pixels
 * on in the format asked for: there is no color management here, so profiles are carried but not applied.
 */
#include "wic_private.h"

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
    /* 1 is sRGB, 2 is adobe rgb, and 0xffff is uncalibrated, which the exif tag also allows. */
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

/* ------------------------------------------------------------------ the transform */

typedef struct WicColorTransform {
    WicBase base;
    WicSlot slot;
    IWICBitmapSource *converted;
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
}

static HRESULT STDMETHODCALLTYPE ctrans_get_size(IWICColorTransform *This, UINT *width, UINT *height)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->GetSize(t->converted, width, height) : WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ctrans_get_format(IWICColorTransform *This, WICPixelFormatGUID *format)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->GetPixelFormat(t->converted, format) : WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ctrans_get_resolution(IWICColorTransform *This, double *x, double *y)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->GetResolution(t->converted, x, y) : WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ctrans_copy_palette(IWICColorTransform *This, IWICPalette *palette)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->CopyPalette(t->converted, palette) : WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE ctrans_copy_pixels(IWICColorTransform *This, const WICRect *rect, UINT stride,
                                                    UINT size, BYTE *buffer)
{
    WicColorTransform *t = ctrans_from(This);
    return t->ready ? t->converted->lpVtbl->CopyPixels(t->converted, rect, stride, size, buffer)
                    : WINCODEC_ERR_NOTINITIALIZED;
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
    HRESULT hr = WicEnsureFormat(source, dest_format, &t->converted);
    if (SUCCEEDED(hr))
        t->ready = TRUE;
    return hr;
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
