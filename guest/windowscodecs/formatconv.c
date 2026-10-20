/*
 * IWICFormatConverter: a source seen in another pixel format, converted when its pixels are asked for.
 */
#include "wic_private.h"
#include "convert.h"

typedef struct WicFormatConverter {
    WicBase base;
    WicSlot converter;
    CRITICAL_SECTION lock;
    IWICBitmapSource *source;
    GUID src_format;
    GUID dst_format;
    UINT width;
    UINT height;
    WICColor src_palette[256];
    UINT src_palette_count;
    WICColor dst_palette[256];
    UINT dst_palette_count;
    IWICPalette *palette; /* the palette of an indexed destination. */
    WICBitmapDitherType dither;
    double threshold;
    BYTE *cache;          /* the whole converted image, for error diffusion. */
    UINT cache_stride;
    BOOL ready;
} WicFormatConverter;

static WicFormatConverter *conv_from(IWICFormatConverter *This)
{
    return WIC_FROM_SLOT(WicFormatConverter, converter, This);
}

static void conv_destroy(WicBase *base)
{
    WicFormatConverter *c = (WicFormatConverter *)base;
    if (c->source)
        c->source->lpVtbl->Release(c->source);
    if (c->palette)
        c->palette->lpVtbl->Release(c->palette);
    WicFree(c->cache);
    DeleteCriticalSection(&c->lock);
}

static HRESULT STDMETHODCALLTYPE conv_get_size(IWICFormatConverter *This, UINT *width, UINT *height)
{
    WicFormatConverter *c = conv_from(This);
    if (!width || !height)
        return E_INVALIDARG;
    if (!c->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    *width = c->width;
    *height = c->height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE conv_get_format(IWICFormatConverter *This, WICPixelFormatGUID *format)
{
    WicFormatConverter *c = conv_from(This);
    if (!format)
        return E_INVALIDARG;
    if (!c->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    *format = c->dst_format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE conv_get_resolution(IWICFormatConverter *This, double *dpi_x, double *dpi_y)
{
    WicFormatConverter *c = conv_from(This);
    if (!dpi_x || !dpi_y)
        return E_INVALIDARG;
    if (!c->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    return c->source->lpVtbl->GetResolution(c->source, dpi_x, dpi_y);
}

static HRESULT STDMETHODCALLTYPE conv_copy_palette(IWICFormatConverter *This, IWICPalette *palette)
{
    WicFormatConverter *c = conv_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!c->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!c->palette) {
        if (WicFormatIsIndexed(&c->dst_format))
            return WINCODEC_ERR_PALETTEUNAVAILABLE;
        /* the fixed gray and black and white formats have palettes of their own. */
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    return palette->lpVtbl->InitializeFromPalette(palette, c->palette);
}

static UINT band_rows(UINT row_bytes)
{
    UINT rows = row_bytes ? 262144 / row_bytes : 64;
    return rows < 1 ? 1 : rows > 256 ? 256 : rows;
}

static HRESULT convert_rect(WicFormatConverter *c, const WICRect *r, UINT stride, BYTE *buffer, BOOL whole)
{
    UINT src_row;
    if (!WicRowBytes(&c->src_format, (UINT)r->Width, &src_row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    UINT rows = band_rows(src_row);
    if (rows > (UINT)r->Height)
        rows = (UINT)r->Height;
    BYTE *band = WicAlloc((SIZE_T)src_row * rows);
    if (!band)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    UINT done = 0;
    WicIndexer *indexer = nullptr;
    BOOL indexed = WicFormatIsIndexed(&c->dst_format);
    BOOL diffuse = indexed && c->dither == WICBitmapDitherTypeErrorDiffusion;
    if (diffuse) {
        indexer = WicIndexerNew(c->dst_palette, c->dst_palette_count, c->dither, c->threshold, (UINT)r->Width);
        if (!indexer) {
            WicFree(band);
            return E_OUTOFMEMORY;
        }
    }
    (void)whole;
    while (done < (UINT)r->Height) {
        UINT n = (UINT)r->Height - done < rows ? (UINT)r->Height - done : rows;
        WICRect piece = {r->X, r->Y + (INT)done, r->Width, (INT)n};
        hr = c->source->lpVtbl->CopyPixels(c->source, &piece, src_row, src_row * n, band);
        if (FAILED(hr))
            break;
        if (diffuse) {
            for (UINT y = 0; y < n; y++) {
                BYTE *bgra = WicAlloc((SIZE_T)r->Width * 4);
                if (!bgra) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                hr = WicConvertRows(&c->src_format, &GUID_WICPixelFormat32bppBGRA, band + (SIZE_T)y * src_row, src_row,
                                    bgra, (UINT)r->Width * 4, (UINT)r->Width, 1, c->src_palette, c->src_palette_count,
                                    nullptr, 0, WICBitmapDitherTypeNone, 0, 0, 0.0);
                if (SUCCEEDED(hr))
                    WicIndexerRow(indexer, bgra, buffer + (SIZE_T)(done + y) * stride, 0, (UINT)r->Width,
                                  WicFormatBits(&c->dst_format), 0, (UINT)r->Y + done + y);
                WicFree(bgra);
                if (FAILED(hr))
                    break;
            }
        } else {
            hr = WicConvertRows(&c->src_format, &c->dst_format, band, src_row, buffer + (SIZE_T)done * stride, stride,
                                (UINT)r->Width, n, c->src_palette, c->src_palette_count, c->dst_palette,
                                c->dst_palette_count, c->dither, (UINT)r->X, (UINT)r->Y + done, c->threshold);
        }
        if (FAILED(hr))
            break;
        done += n;
    }
    WicIndexerFree(indexer);
    WicFree(band);
    return hr;
}

static HRESULT STDMETHODCALLTYPE conv_copy_pixels(IWICFormatConverter *This, const WICRect *rect, UINT stride,
                                                  UINT size, BYTE *buffer)
{
    WicFormatConverter *c = conv_from(This);
    WICRect r;
    if (!c->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    HRESULT hr = WicCheckRect(rect, c->width, c->height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &c->dst_format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    if (IsEqualGUID(&c->src_format, &c->dst_format))
        return c->source->lpVtbl->CopyPixels(c->source, &r, stride, size, buffer);
    BOOL indexed = WicFormatIsIndexed(&c->dst_format);
    if (indexed && c->dither == WICBitmapDitherTypeErrorDiffusion && (r.X || r.Y || (UINT)r.Width != c->width)) {
        /* the error carries from the top left, so the whole image is done once and cut. */
        EnterCriticalSection(&c->lock);
        if (!c->cache) {
            UINT row;
            WicRowBytes(&c->dst_format, c->width, &row);
            c->cache_stride = (row + 3) & ~3u;
            c->cache = WicAlloc((SIZE_T)c->cache_stride * c->height);
            if (!c->cache) {
                LeaveCriticalSection(&c->lock);
                return E_OUTOFMEMORY;
            }
            WICRect all = {0, 0, (INT)c->width, (INT)c->height};
            hr = convert_rect(c, &all, c->cache_stride, c->cache, TRUE);
            if (FAILED(hr)) {
                WicFree(c->cache);
                c->cache = nullptr;
            }
        }
        if (SUCCEEDED(hr))
            WicCopyBits(buffer, stride, c->cache, c->cache_stride, (UINT)r.X, (UINT)r.Y, (UINT)r.Width,
                        (UINT)r.Height, WicFormatBits(&c->dst_format));
        LeaveCriticalSection(&c->lock);
        return hr;
    }
    return convert_rect(c, &r, stride, buffer, FALSE);
}

static HRESULT STDMETHODCALLTYPE conv_initialize(IWICFormatConverter *This, IWICBitmapSource *source,
                                                 REFWICPixelFormatGUID dst_format, WICBitmapDitherType dither,
                                                 IWICPalette *palette, double threshold,
                                                 WICBitmapPaletteType palette_type)
{
    WicFormatConverter *c = conv_from(This);
    if (!source || !dst_format)
        return E_INVALIDARG;
    if (!(threshold >= 0.0 && threshold <= 100.0))
        return E_INVALIDARG;
    if ((UINT)dither > WICBitmapDitherTypeErrorDiffusion || (UINT)palette_type > WICBitmapPaletteTypeFixedGray256)
        return E_INVALIDARG;
    EnterCriticalSection(&c->lock);
    HRESULT hr = S_OK;
    if (c->ready) {
        LeaveCriticalSection(&c->lock);
        return WINCODEC_ERR_WRONGSTATE;
    }
    GUID src_format;
    UINT width, height;
    hr = source->lpVtbl->GetPixelFormat(source, &src_format);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetSize(source, &width, &height);
    if (FAILED(hr))
        goto fail;
    if (!WicCanConvert(&src_format, dst_format)) {
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        goto fail;
    }
    c->src_palette_count = 0;
    if (WicFormatIsIndexed(&src_format)) {
        IWICPalette *source_palette;
        hr = WicPaletteCreate(&source_palette);
        if (SUCCEEDED(hr)) {
            hr = source->lpVtbl->CopyPalette(source, source_palette);
            UINT got = 0;
            if (SUCCEEDED(hr))
                hr = source_palette->lpVtbl->GetColors(source_palette, 256, c->src_palette, &got);
            c->src_palette_count = got;
            source_palette->lpVtbl->Release(source_palette);
        }
        if (FAILED(hr))
            goto fail;
    }
    c->dst_palette_count = 0;
    if (WicFormatIsIndexed(dst_format)) {
        IWICPalette *dst = nullptr;
        hr = WicPaletteCreate(&dst);
        if (FAILED(hr))
            goto fail;
        UINT bits = WicFormatBits(dst_format);
        if (palette) {
            hr = dst->lpVtbl->InitializeFromPalette(dst, palette);
        } else if (palette_type == WICBitmapPaletteTypeMedianCut) {
            hr = dst->lpVtbl->InitializeFromBitmap(dst, source, 1u << bits, FALSE);
        } else if (palette_type != WICBitmapPaletteTypeCustom) {
            hr = dst->lpVtbl->InitializePredefined(dst, palette_type, FALSE);
        } else {
            hr = dst->lpVtbl->InitializePredefined(dst, bits == 1 ? WICBitmapPaletteTypeFixedBW
                                                                   : bits == 2 ? WICBitmapPaletteTypeFixedGray4
                                                                   : bits == 4 ? WICBitmapPaletteTypeFixedHalftone8
                                                                               : WICBitmapPaletteTypeFixedHalftone256,
                                                   FALSE);
        }
        UINT got = 0, maximum = 1u << bits;
        if (SUCCEEDED(hr))
            hr = dst->lpVtbl->GetColors(dst, maximum < 256 ? maximum : 256, c->dst_palette, &got);
        if (FAILED(hr) || !got) {
            dst->lpVtbl->Release(dst);
            if (SUCCEEDED(hr))
                hr = WINCODEC_ERR_PALETTEUNAVAILABLE;
            goto fail;
        }
        c->dst_palette_count = got;
        c->palette = dst;
    }
    source->lpVtbl->AddRef(source);
    c->source = source;
    c->src_format = src_format;
    c->dst_format = *dst_format;
    c->width = width;
    c->height = height;
    c->dither = dither;
    c->threshold = threshold;
    c->ready = TRUE;
    LeaveCriticalSection(&c->lock);
    return S_OK;
fail:
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE conv_can_convert(IWICFormatConverter *This, REFWICPixelFormatGUID src,
                                                  REFWICPixelFormatGUID dst, BOOL *can)
{
    if (!src || !dst || !can)
        return E_INVALIDARG;
    *can = WicCanConvert(src, dst);
    return S_OK;
}

static const IWICFormatConverterVtbl g_conv_vtbl = {
    WIC_IUNKNOWN_FUNCS, conv_get_size, conv_get_format, conv_get_resolution, conv_copy_palette, conv_copy_pixels,
    conv_initialize,    conv_can_convert,
};

static const WicIfaceEntry g_conv_ifaces[] = {
    WIC_ENTRY(WicFormatConverter, IID_IWICFormatConverter, converter),
    WIC_ENTRY(WicFormatConverter, IID_IWICBitmapSource, converter),
};
static const WicClass g_conv_class = {conv_destroy, g_conv_ifaces, 2};

HRESULT WicFormatConverterCreate(IWICFormatConverter **out)
{
    WicFormatConverter *c = WicNew(sizeof(*c), &g_conv_class);
    if (!c)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&c->lock);
    WIC_SLOT(c, converter, &g_conv_vtbl);
    *out = (IWICFormatConverter *)&c->converter;
    return S_OK;
}

HRESULT WicCreateConverterObject(REFIID riid, void **object)
{
    IWICFormatConverter *converter;
    HRESULT hr = WicFormatConverterCreate(&converter);
    if (FAILED(hr))
        return hr;
    hr = converter->lpVtbl->QueryInterface(converter, riid, object);
    converter->lpVtbl->Release(converter);
    return hr;
}

HRESULT WicEnsureFormat(IWICBitmapSource *source, const GUID *format, IWICBitmapSource **result)
{
    GUID have;
    *result = nullptr;
    HRESULT hr = source->lpVtbl->GetPixelFormat(source, &have);
    if (FAILED(hr))
        return hr;
    if (IsEqualGUID(&have, format)) {
        source->lpVtbl->AddRef(source);
        *result = source;
        return S_OK;
    }
    IWICFormatConverter *converter;
    hr = WicFormatConverterCreate(&converter);
    if (FAILED(hr))
        return hr;
    hr = converter->lpVtbl->Initialize(converter, source, format, WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->lpVtbl->Release(converter);
        return hr;
    }
    *result = (IWICBitmapSource *)converter;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WICConvertBitmapSource(REFWICPixelFormatGUID dst_format, IWICBitmapSource *source,
                                                      IWICBitmapSource **result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (!dst_format || !source)
        return E_INVALIDARG;
    return WicEnsureFormat(source, dst_format, result);
}
