/*
 * IWICBitmapScaler, IWICBitmapClipper and IWICBitmapFlipRotator: sources made from another source.
 */
#include "wic_private.h"

#ifdef __SSE2__
#include <emmintrin.h>
#endif

/* ------------------------------------------------------------------ a source over a source */

typedef struct WicTransform {
    WicBase base;
    WicSlot slot;
    CRITICAL_SECTION lock;
    IWICBitmapSource *source;
    GUID format;
    UINT src_width;
    UINT src_height;
    UINT width;
    UINT height;
    BOOL ready;
    /* the scaler. */
    WICBitmapInterpolationMode mode;
    IWICBitmapSource *original; /* the source before a conversion. */
    /* the clipper. */
    WICRect clip;
    /* the rotator. */
    WICBitmapTransformOptions options;
} WicTransform;

static WicTransform *transform_from(void *This) { return WIC_FROM_SLOT(WicTransform, slot, This); }

static void transform_destroy(WicBase *base)
{
    WicTransform *t = (WicTransform *)base;
    if (t->source)
        t->source->lpVtbl->Release(t->source);
    if (t->original)
        t->original->lpVtbl->Release(t->original);
    DeleteCriticalSection(&t->lock);
}

static HRESULT STDMETHODCALLTYPE transform_get_size(IWICBitmapSource *This, UINT *width, UINT *height)
{
    WicTransform *t = transform_from(This);
    if (!width || !height)
        return E_INVALIDARG;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    *width = t->width;
    *height = t->height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_get_format(IWICBitmapSource *This, WICPixelFormatGUID *format)
{
    WicTransform *t = transform_from(This);
    if (!format)
        return E_INVALIDARG;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    *format = t->format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_get_resolution(IWICBitmapSource *This, double *dpi_x, double *dpi_y)
{
    WicTransform *t = transform_from(This);
    if (!dpi_x || !dpi_y)
        return E_INVALIDARG;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    HRESULT hr = t->source->lpVtbl->GetResolution(t->source, dpi_x, dpi_y);
    if (SUCCEEDED(hr) && (t->options & 1)) {
        /* a quarter turn swaps the axes. */
        double x = *dpi_x;
        *dpi_x = *dpi_y;
        *dpi_y = x;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_copy_palette(IWICBitmapSource *This, IWICPalette *palette)
{
    WicTransform *t = transform_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!WicFormatIsIndexed(&t->format))
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    return t->source->lpVtbl->CopyPalette(t->source, palette);
}

#define TRANSFORM_SOURCE                                                                                               \
    VTBL_FN(transform_get_size), VTBL_FN(transform_get_format), VTBL_FN(transform_get_resolution),                      \
        VTBL_FN(transform_copy_palette)

/* ------------------------------------------------------------------ the scaler */

typedef struct Taps {
    INT start;
    UINT count;
    UINT offset; /* of the first weight in the table. */
} Taps;

typedef struct Axis {
    Taps *taps;
    float *weights;
} Axis;

static float cubic_weight(float x)
{
    if (x < 0)
        x = -x;
    if (x < 1.0f)
        return ((1.5f * x - 2.5f) * x) * x + 1.0f;
    if (x < 2.0f)
        return (((-0.5f * x + 2.5f) * x) - 4.0f) * x + 2.0f;
    return 0.0f;
}

static INT floor_i(float v)
{
    INT i = (INT)v;
    return (float)i > v ? i - 1 : i;
}

static INT clamp_i(INT v, INT lo, INT hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the taps of each destination position of a range, weights folded onto the edge pixels and normalized. */
static HRESULT axis_build(WICBitmapInterpolationMode mode, UINT src_n, UINT dst_n, UINT first, UINT count, Axis *axis)
{
    float scale = (float)src_n / (float)dst_n;
    float widen = scale > 1.0f ? scale : 1.0f;
    UINT max_taps;
    switch (mode) {
    case WICBitmapInterpolationModeLinear:
        max_taps = 2;
        break;
    case WICBitmapInterpolationModeCubic:
        max_taps = 4;
        break;
    case WICBitmapInterpolationModeFant:
        max_taps = (UINT)(scale + 1.0f) + 3;
        break;
    default:
        max_taps = (UINT)(4.0f * widen) + 3;
        break;
    }
    axis->taps = WicAlloc((SIZE_T)count * sizeof(Taps));
    axis->weights = WicAllocZero((SIZE_T)count * max_taps * sizeof(float));
    if (!axis->taps || !axis->weights) {
        WicFree(axis->taps);
        WicFree(axis->weights);
        axis->taps = nullptr;
        axis->weights = nullptr;
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++) {
        UINT d = first + i;
        float center = ((float)d + 0.5f) * scale;
        float *w = axis->weights + (SIZE_T)i * max_taps;
        INT start;
        UINT taps;
        float tmp[96];
        float *work = max_taps <= 96 ? tmp : w;
        switch (mode) {
        case WICBitmapInterpolationModeLinear: {
            float pos = center - 0.5f;
            INT base = floor_i(pos);
            float frac = pos - (float)base;
            /* the weights are in steps of 1/256 as the fixed point scaler has them. */
            INT w1 = (INT)(frac * 256.0f);
            start = base;
            taps = 2;
            work[0] = (float)(256 - w1) / 256.0f;
            work[1] = (float)w1 / 256.0f;
            break;
        }
        case WICBitmapInterpolationModeCubic: {
            float pos = center - 0.5f;
            INT base = floor_i(pos);
            float frac = pos - (float)base;
            start = base - 1;
            taps = 4;
            for (UINT k = 0; k < 4; k++)
                work[k] = cubic_weight(frac - (float)((INT)k - 1));
            break;
        }
        case WICBitmapInterpolationModeFant: {
            if (scale > 1.0f) {
                float lo = (float)d * scale, hi = lo + scale;
                start = floor_i(lo);
                INT last = floor_i(hi);
                if ((float)last == hi)
                    last--;
                taps = (UINT)(last - start + 1);
                for (UINT k = 0; k < taps; k++) {
                    float a = (float)(start + (INT)k), b = a + 1.0f;
                    float from = a < lo ? lo : a, to = b > hi ? hi : b;
                    work[k] = to > from ? to - from : 0.0f;
                }
            } else {
                float pos = center - 0.5f;
                INT base = floor_i(pos);
                float frac = pos - (float)base;
                start = base;
                taps = 2;
                work[0] = 1.0f - frac;
                work[1] = frac;
            }
            break;
        }
        default: {
            start = floor_i(center - 2.0f * widen);
            taps = (UINT)(4.0f * widen) + 2;
            if (taps > max_taps)
                taps = max_taps;
            for (UINT k = 0; k < taps; k++) {
                float dist = ((float)(start + (INT)k) + 0.5f) - center;
                work[k] = cubic_weight(dist / widen);
            }
            break;
        }
        }
        /* fold the taps outside the image onto its edge pixels. */
        INT lo_pixel = start, hi_pixel = start + (INT)taps - 1;
        INT first_in = clamp_i(lo_pixel, 0, (INT)src_n - 1), last_in = clamp_i(hi_pixel, 0, (INT)src_n - 1);
        UINT in_count = (UINT)(last_in - first_in + 1);
        for (UINT k = 0; k < in_count; k++)
            w[k] = 0.0f;
        float total = 0.0f;
        for (UINT k = 0; k < taps; k++) {
            INT p = clamp_i(start + (INT)k, 0, (INT)src_n - 1);
            w[p - first_in] += work[k];
            total += work[k];
        }
        if (total != 0.0f) {
            float inv = 1.0f / total;
            for (UINT k = 0; k < in_count; k++)
                w[k] *= inv;
        }
        axis->taps[i].start = first_in;
        axis->taps[i].count = in_count;
        axis->taps[i].offset = i * max_taps;
    }
    return S_OK;
}

static void axis_free(Axis *axis)
{
    WicFree(axis->taps);
    WicFree(axis->weights);
}

typedef struct ScaleFormat {
    UINT channels; /* 1, 3 or 4. */
    UINT bytes;    /* per channel: 1, 2 or 4. */
    BOOL is_float;
} ScaleFormat;

static BOOL scale_format(const GUID *format, ScaleFormat *out)
{
    const WicFormat *info = WicFindFormat(format);
    if (!info || info->numeric == WICPixelFormatNumericRepresentationIndexed)
        return FALSE;
    if (IsEqualGUID(format, &GUID_WICPixelFormat8bppGray) || IsEqualGUID(format, &GUID_WICPixelFormat8bppAlpha)) {
        *out = (ScaleFormat){1, 1, FALSE};
        return TRUE;
    }
    if (IsEqualGUID(format, &GUID_WICPixelFormat16bppGray)) {
        *out = (ScaleFormat){1, 2, FALSE};
        return TRUE;
    }
    if (IsEqualGUID(format, &GUID_WICPixelFormat32bppGrayFloat)) {
        *out = (ScaleFormat){1, 4, TRUE};
        return TRUE;
    }
    if (info->numeric == WICPixelFormatNumericRepresentationUnsignedInteger && info->bpp % 8 == 0) {
        UINT bits = info->bpp;
        if (bits == 24) {
            *out = (ScaleFormat){3, 1, FALSE};
            return TRUE;
        }
        if (bits == 32 && info->mask_size == 16) {
            *out = (ScaleFormat){4, 1, FALSE};
            return TRUE;
        }
        if (bits == 32 && info->mask_size == 12 && !IsEqualGUID(format, &GUID_WICPixelFormat32bppBGR101010)) {
            *out = (ScaleFormat){4, 1, FALSE};
            return TRUE;
        }
        if (bits == 48) {
            *out = (ScaleFormat){3, 2, FALSE};
            return TRUE;
        }
        if (bits == 64) {
            *out = (ScaleFormat){4, 2, FALSE};
            return TRUE;
        }
    }
    if (info->numeric == WICPixelFormatNumericRepresentationFloat && info->bpp == 128) {
        *out = (ScaleFormat){4, 4, TRUE};
        return TRUE;
    }
    if (info->numeric == WICPixelFormatNumericRepresentationFloat && info->bpp == 96) {
        *out = (ScaleFormat){3, 4, TRUE};
        return TRUE;
    }
    return FALSE;
}

/* a row of the source as floats, the columns first to first + count of it. */
static void row_to_float(const ScaleFormat *f, const BYTE *src, UINT count, float *out)
{
    UINT n = count * f->channels;
    if (f->bytes == 1) {
        UINT i = 0;
#ifdef __SSE2__
        __m128i zero = _mm_setzero_si128();
        for (; i + 8 <= n; i += 8) {
            __m128i v = _mm_loadl_epi64((const __m128i *)(src + i));
            __m128i w = _mm_unpacklo_epi8(v, zero);
            _mm_storeu_ps(out + i, _mm_cvtepi32_ps(_mm_unpacklo_epi16(w, zero)));
            _mm_storeu_ps(out + i + 4, _mm_cvtepi32_ps(_mm_unpackhi_epi16(w, zero)));
        }
#endif
        for (; i < n; i++)
            out[i] = (float)src[i];
    } else if (f->bytes == 2) {
        const UINT16 *p = (const UINT16 *)src;
        for (UINT i = 0; i < n; i++)
            out[i] = (float)p[i];
    } else {
        memcpy(out, src, (SIZE_T)n * 4);
    }
}

static void float_to_row(const ScaleFormat *f, const float *in, UINT count, BYTE *dst)
{
    UINT n = count * f->channels;
    if (f->bytes == 1) {
        for (UINT i = 0; i < n; i++) {
            float v = in[i] + 0.5f;
            dst[i] = v <= 0.0f ? 0 : v >= 255.0f ? 255 : (BYTE)(INT)v;
        }
    } else if (f->bytes == 2) {
        UINT16 *p = (UINT16 *)dst;
        for (UINT i = 0; i < n; i++) {
            float v = in[i] + 0.5f;
            p[i] = v <= 0.0f ? 0 : v >= 65535.0f ? 65535 : (UINT16)(INT)v;
        }
    } else {
        memcpy(dst, in, (SIZE_T)n * 4);
    }
}

/* one source row, scaled across: floats for the columns of the rectangle. */
static void scale_row_x(const ScaleFormat *f, const Axis *ax, UINT count, UINT src_first, const float *src,
                        float *out)
{
    UINT ch = f->channels;
    for (UINT i = 0; i < count; i++) {
        const Taps *t = &ax->taps[i];
        const float *w = ax->weights + t->offset;
        const float *p = src + (SIZE_T)(t->start - (INT)src_first) * ch;
        if (ch == 4) {
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            for (UINT k = 0; k < t->count; k++, p += 4) {
                float wk = w[k];
                a0 += p[0] * wk;
                a1 += p[1] * wk;
                a2 += p[2] * wk;
                a3 += p[3] * wk;
            }
            out[i * 4] = a0;
            out[i * 4 + 1] = a1;
            out[i * 4 + 2] = a2;
            out[i * 4 + 3] = a3;
        } else if (ch == 3) {
            float a0 = 0, a1 = 0, a2 = 0;
            for (UINT k = 0; k < t->count; k++, p += 3) {
                float wk = w[k];
                a0 += p[0] * wk;
                a1 += p[1] * wk;
                a2 += p[2] * wk;
            }
            out[i * 3] = a0;
            out[i * 3 + 1] = a1;
            out[i * 3 + 2] = a2;
        } else {
            float a0 = 0;
            for (UINT k = 0; k < t->count; k++)
                a0 += p[k] * w[k];
            out[i] = a0;
        }
    }
}

static HRESULT STDMETHODCALLTYPE scaler_copy_pixels(IWICBitmapScaler *This, const WICRect *rect, UINT stride,
                                                    UINT size, BYTE *buffer)
{
    WicTransform *t = transform_from(This);
    WICRect r;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    HRESULT hr = WicCheckRect(rect, t->width, t->height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &t->format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    IWICBitmapSource *src = t->source;
    UINT bits = WicFormatBits(&t->format);
    if (t->mode == WICBitmapInterpolationModeNearestNeighbor || (t->src_width == t->width && t->src_height == t->height)) {
        /* a pixel of the source for each, found from the middle of the pixel. */
        UINT src_row;
        if (!WicRowBytes(&t->format, t->src_width, &src_row))
            return INTSAFE_E_ARITHMETIC_OVERFLOW;
        BYTE *line = WicAlloc(src_row);
        UINT *xs = WicAlloc((SIZE_T)r.Width * sizeof(UINT));
        if (!line || !xs) {
            WicFree(line);
            WicFree(xs);
            return E_OUTOFMEMORY;
        }
        for (INT i = 0; i < r.Width; i++)
            xs[i] = (UINT)(((UINT64)(2 * (UINT)(r.X + i) + 1) * t->src_width) / ((UINT64)2 * t->width));
        UINT last_row = ~0u;
        for (INT j = 0; j < r.Height && SUCCEEDED(hr); j++) {
            UINT sy = (UINT)(((UINT64)(2 * (UINT)(r.Y + j) + 1) * t->src_height) / ((UINT64)2 * t->height));
            if (sy != last_row) {
                WICRect line_rect = {0, (INT)sy, (INT)t->src_width, 1};
                hr = src->lpVtbl->CopyPixels(src, &line_rect, src_row, src_row, line);
                last_row = sy;
                if (FAILED(hr))
                    break;
            }
            BYTE *out = buffer + (SIZE_T)j * stride;
            if (bits >= 8) {
                UINT bytes = bits / 8;
                for (INT i = 0; i < r.Width; i++)
                    memcpy(out + (SIZE_T)i * bytes, line + (SIZE_T)xs[i] * bytes, bytes);
            } else {
                memset(out, 0, (SIZE_T)(((UINT)r.Width * bits + 7) / 8));
                for (INT i = 0; i < r.Width; i++) {
                    UINT sx = xs[i], per = 8 / bits;
                    UINT v = (line[sx / per] >> (8 - bits - (sx % per) * bits)) & ((1u << bits) - 1);
                    UINT dper = (UINT)i % per;
                    out[(UINT)i / per] |= (BYTE)(v << (8 - bits - dper * bits));
                }
            }
        }
        WicFree(line);
        WicFree(xs);
        return hr;
    }
    ScaleFormat f;
    if (!scale_format(&t->format, &f))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    Axis ax = {0}, ay = {0};
    hr = axis_build(t->mode, t->src_width, t->width, (UINT)r.X, (UINT)r.Width, &ax);
    if (SUCCEEDED(hr))
        hr = axis_build(t->mode, t->src_height, t->height, (UINT)r.Y, (UINT)r.Height, &ay);
    if (FAILED(hr)) {
        axis_free(&ax);
        axis_free(&ay);
        return hr;
    }
    /* the source columns that matter. */
    INT col_lo = ax.taps[0].start, col_hi = ax.taps[0].start + (INT)ax.taps[0].count;
    for (INT i = 1; i < r.Width; i++) {
        if (ax.taps[i].start < col_lo)
            col_lo = ax.taps[i].start;
        if (ax.taps[i].start + (INT)ax.taps[i].count > col_hi)
            col_hi = ax.taps[i].start + (INT)ax.taps[i].count;
    }
    UINT cols = (UINT)(col_hi - col_lo);
    UINT src_bytes = cols * f.channels * f.bytes;
    UINT out_floats = (UINT)r.Width * f.channels;
    /* the rows of the source that the vertical taps use are scaled across once and kept while they are in use. */
    UINT max_rows = 0;
    for (INT j = 0; j < r.Height; j++) {
        if (ay.taps[j].count > max_rows)
            max_rows = ay.taps[j].count;
    }
    UINT ring_size = max_rows + 1;
    BYTE *raw = WicAlloc(src_bytes);
    float *srcf = WicAlloc((SIZE_T)cols * f.channels * sizeof(float));
    float *ring = WicAlloc((SIZE_T)ring_size * out_floats * sizeof(float));
    INT *ring_row = WicAlloc((SIZE_T)ring_size * sizeof(INT));
    float *acc = WicAlloc((SIZE_T)out_floats * sizeof(float));
    if (!raw || !srcf || !ring || !ring_row || !acc) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    for (UINT i = 0; i < ring_size; i++)
        ring_row[i] = -1;
    for (INT j = 0; j < r.Height && SUCCEEDED(hr); j++) {
        const Taps *vt = &ay.taps[j];
        const float *vw = ay.weights + vt->offset;
        for (UINT i = 0; i < out_floats; i++)
            acc[i] = 0.0f;
        for (UINT k = 0; k < vt->count; k++) {
            INT row = vt->start + (INT)k;
            UINT slot = (UINT)row % ring_size;
            if (ring_row[slot] != row) {
                WICRect line = {col_lo, row, (INT)cols, 1};
                hr = src->lpVtbl->CopyPixels(src, &line, src_bytes, src_bytes, raw);
                if (FAILED(hr))
                    break;
                row_to_float(&f, raw, cols * 1, srcf);
                scale_row_x(&f, &ax, (UINT)r.Width, (UINT)col_lo, srcf, ring + (SIZE_T)slot * out_floats);
                ring_row[slot] = row;
            }
            const float *rf = ring + (SIZE_T)slot * out_floats;
            float wk = vw[k];
            UINT i = 0;
#ifdef __SSE2__
            __m128 wv = _mm_set1_ps(wk);
            for (; i + 4 <= out_floats; i += 4)
                _mm_storeu_ps(acc + i, _mm_add_ps(_mm_loadu_ps(acc + i), _mm_mul_ps(_mm_loadu_ps(rf + i), wv)));
#endif
            for (; i < out_floats; i++)
                acc[i] += rf[i] * wk;
        }
        if (SUCCEEDED(hr))
            float_to_row(&f, acc, (UINT)r.Width, buffer + (SIZE_T)j * stride);
    }
done:
    WicFree(raw);
    WicFree(srcf);
    WicFree(ring);
    WicFree(ring_row);
    WicFree(acc);
    axis_free(&ax);
    axis_free(&ay);
    return hr;
}

static HRESULT STDMETHODCALLTYPE scaler_initialize(IWICBitmapScaler *This, IWICBitmapSource *source, UINT width,
                                                   UINT height, WICBitmapInterpolationMode mode)
{
    WicTransform *t = transform_from(This);
    if (!source || !width || !height)
        return E_INVALIDARG;
    if ((UINT)mode > WICBitmapInterpolationModeHighQualityCubic)
        return E_INVALIDARG;
    EnterCriticalSection(&t->lock);
    if (t->ready) {
        LeaveCriticalSection(&t->lock);
        return WINCODEC_ERR_WRONGSTATE;
    }
    GUID format;
    UINT sw, sh;
    HRESULT hr = source->lpVtbl->GetSize(source, &sw, &sh);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetPixelFormat(source, &format);
    if (SUCCEEDED(hr) && (!sw || !sh))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        ScaleFormat f;
        IWICBitmapSource *use = source;
        use->lpVtbl->AddRef(use);
        if (mode != WICBitmapInterpolationModeNearestNeighbor && !scale_format(&format, &f) &&
            (sw != width || sh != height)) {
            /* indexed, packed and small formats are interpolated as 32 bit color. */
            IWICBitmapSource *converted;
            hr = WicEnsureFormat(source, &GUID_WICPixelFormat32bppBGRA, &converted);
            use->lpVtbl->Release(use);
            use = converted;
            format = GUID_WICPixelFormat32bppBGRA;
        }
        if (SUCCEEDED(hr)) {
            t->source = use;
            t->format = format;
            t->src_width = sw;
            t->src_height = sh;
            t->width = width;
            t->height = height;
            t->mode = mode;
            t->ready = TRUE;
        }
    }
    LeaveCriticalSection(&t->lock);
    return hr;
}

static const IWICBitmapScalerVtbl g_scaler_vtbl = {
    WIC_IUNKNOWN_FUNCS, TRANSFORM_SOURCE,
    scaler_copy_pixels, scaler_initialize,
};

static const WicIfaceEntry g_scaler_ifaces[] = {
    WIC_ENTRY(WicTransform, IID_IWICBitmapScaler, slot),
    WIC_ENTRY(WicTransform, IID_IWICBitmapSource, slot),
};
static const WicClass g_scaler_class = {transform_destroy, g_scaler_ifaces, 2};

static void *transform_new(const WicClass *cls, const void *vtbl)
{
    WicTransform *t = WicNew(sizeof(*t), cls);
    if (!t)
        return nullptr;
    InitializeCriticalSection(&t->lock);
    WIC_SLOT(t, slot, vtbl);
    return &t->slot;
}

HRESULT WicScalerCreate(IWICBitmapScaler **out)
{
    *out = transform_new(&g_scaler_class, &g_scaler_vtbl);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ the clipper */

static HRESULT STDMETHODCALLTYPE clipper_copy_pixels(IWICBitmapClipper *This, const WICRect *rect, UINT stride,
                                                     UINT size, BYTE *buffer)
{
    WicTransform *t = transform_from(This);
    WICRect r;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    HRESULT hr = WicCheckRect(rect, t->width, t->height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &t->format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    r.X += t->clip.X;
    r.Y += t->clip.Y;
    return t->source->lpVtbl->CopyPixels(t->source, &r, stride, size, buffer);
}

static HRESULT STDMETHODCALLTYPE clipper_initialize(IWICBitmapClipper *This, IWICBitmapSource *source,
                                                    const WICRect *rect)
{
    WicTransform *t = transform_from(This);
    if (!source || !rect)
        return E_INVALIDARG;
    EnterCriticalSection(&t->lock);
    if (t->ready) {
        LeaveCriticalSection(&t->lock);
        return WINCODEC_ERR_WRONGSTATE;
    }
    UINT sw, sh;
    GUID format;
    HRESULT hr = source->lpVtbl->GetSize(source, &sw, &sh);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetPixelFormat(source, &format);
    if (SUCCEEDED(hr)) {
        if (rect->Width <= 0 || rect->Height <= 0 || rect->X < 0 || rect->Y < 0 ||
            (UINT64)rect->X + (UINT)rect->Width > sw || (UINT64)rect->Y + (UINT)rect->Height > sh) {
            hr = E_INVALIDARG;
        } else {
            source->lpVtbl->AddRef(source);
            t->source = source;
            t->format = format;
            t->src_width = sw;
            t->src_height = sh;
            t->clip = *rect;
            t->width = (UINT)rect->Width;
            t->height = (UINT)rect->Height;
            t->ready = TRUE;
        }
    }
    LeaveCriticalSection(&t->lock);
    return hr;
}

static const IWICBitmapClipperVtbl g_clipper_vtbl = {
    WIC_IUNKNOWN_FUNCS, TRANSFORM_SOURCE,
    clipper_copy_pixels, clipper_initialize,
};
static const WicIfaceEntry g_clipper_ifaces[] = {
    WIC_ENTRY(WicTransform, IID_IWICBitmapClipper, slot),
    WIC_ENTRY(WicTransform, IID_IWICBitmapSource, slot),
};
static const WicClass g_clipper_class = {transform_destroy, g_clipper_ifaces, 2};

HRESULT WicClipperCreate(IWICBitmapClipper **out)
{
    *out = transform_new(&g_clipper_class, &g_clipper_vtbl);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ the rotator */

static void copy_pixel(BYTE *d, const BYTE *s, UINT bytes)
{
    switch (bytes) {
    case 1:
        *d = *s;
        break;
    case 4:
        memcpy(d, s, 4);
        break;
    default:
        memcpy(d, s, bytes);
    }
}

static HRESULT STDMETHODCALLTYPE rotator_copy_pixels(IWICBitmapFlipRotator *This, const WICRect *rect, UINT stride,
                                                     UINT size, BYTE *buffer)
{
    WicTransform *t = transform_from(This);
    WICRect r;
    if (!t->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    HRESULT hr = WicCheckRect(rect, t->width, t->height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &t->format, stride, size, buffer);
    if (FAILED(hr) || !r.Width || !r.Height)
        return hr;
    UINT bits = WicFormatBits(&t->format);
    UINT turns = t->options & 3;
    BOOL flip_h = (t->options & WICBitmapTransformFlipHorizontal) != 0;
    BOOL flip_v = (t->options & WICBitmapTransformFlipVertical) != 0;
    INT W = (INT)t->width, H = (INT)t->height; /* the size after the turn. */
    INT sw = (INT)t->src_width, sh = (INT)t->src_height;
    /* the source pixel of an output pixel. */
    #define MAP(ox, oy, sx_out, sy_out)                                                                               \
        do {                                                                                                          \
            INT ax_ = flip_h ? W - 1 - (ox) : (ox), ay_ = flip_v ? H - 1 - (oy) : (oy);                               \
            switch (turns) {                                                                                          \
            case 0: sx_out = ax_; sy_out = ay_; break;                                                                \
            case 1: sx_out = ay_; sy_out = sh - 1 - ax_; break;                                                       \
            case 2: sx_out = sw - 1 - ax_; sy_out = sh - 1 - ay_; break;                                              \
            default: sx_out = sw - 1 - ay_; sy_out = ax_; break;                                                      \
            }                                                                                                         \
        } while (0)
    INT sx0 = sw, sy0 = sh, sx1 = -1, sy1 = -1;
    INT cx[4] = {r.X, r.X + r.Width - 1, r.X, r.X + r.Width - 1};
    INT cy[4] = {r.Y, r.Y, r.Y + r.Height - 1, r.Y + r.Height - 1};
    for (int i = 0; i < 4; i++) {
        INT sx, sy;
        MAP(cx[i], cy[i], sx, sy);
        if (sx < sx0) sx0 = sx;
        if (sy < sy0) sy0 = sy;
        if (sx > sx1) sx1 = sx;
        if (sy > sy1) sy1 = sy;
    }
    UINT cw = (UINT)(sx1 - sx0 + 1), ch = (UINT)(sy1 - sy0 + 1);
    UINT block_row;
    if (!WicRowBytes(&t->format, cw, &block_row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    BYTE *block = WicAlloc((SIZE_T)block_row * ch);
    if (!block)
        return E_OUTOFMEMORY;
    WICRect from = {sx0, sy0, (INT)cw, (INT)ch};
    hr = t->source->lpVtbl->CopyPixels(t->source, &from, block_row, block_row * ch, block);
    if (SUCCEEDED(hr)) {
        if (bits >= 8) {
            UINT bytes = bits / 8;
            if (turns == 0 || turns == 2) {
                for (INT j = 0; j < r.Height; j++) {
                    BYTE *out = buffer + (SIZE_T)j * stride;
                    for (INT i = 0; i < r.Width; i++) {
                        INT sx, sy;
                        MAP(r.X + i, r.Y + j, sx, sy);
                        copy_pixel(out + (SIZE_T)i * bytes, block + (SIZE_T)(sy - sy0) * block_row + (SIZE_T)(sx - sx0) * bytes, bytes);
                    }
                }
            } else {
                /* tiles keep both the reads and the writes in the cache. */
                for (INT tj = 0; tj < r.Height; tj += 16) {
                    for (INT ti = 0; ti < r.Width; ti += 16) {
                        INT je = tj + 16 < r.Height ? tj + 16 : r.Height, ie = ti + 16 < r.Width ? ti + 16 : r.Width;
                        for (INT j = tj; j < je; j++) {
                            BYTE *out = buffer + (SIZE_T)j * stride;
                            for (INT i = ti; i < ie; i++) {
                                INT sx, sy;
                                MAP(r.X + i, r.Y + j, sx, sy);
                                copy_pixel(out + (SIZE_T)i * bytes,
                                           block + (SIZE_T)(sy - sy0) * block_row + (SIZE_T)(sx - sx0) * bytes, bytes);
                            }
                        }
                    }
                }
            }
        } else {
            for (INT j = 0; j < r.Height; j++) {
                BYTE *out = buffer + (SIZE_T)j * stride;
                memset(out, 0, (((UINT)r.Width * bits) + 7) / 8);
                for (INT i = 0; i < r.Width; i++) {
                    INT sx, sy;
                    MAP(r.X + i, r.Y + j, sx, sy);
                    UINT px = (UINT)(sx - sx0), per = 8 / bits;
                    const BYTE *line = block + (SIZE_T)(sy - sy0) * block_row;
                    UINT v = (line[px / per] >> (8 - bits - (px % per) * bits)) & ((1u << bits) - 1);
                    UINT dper = (UINT)i % per;
                    out[(UINT)i / per] |= (BYTE)(v << (8 - bits - dper * bits));
                }
            }
        }
    }
    WicFree(block);
    #undef MAP
    return hr;
}

static HRESULT STDMETHODCALLTYPE rotator_initialize(IWICBitmapFlipRotator *This, IWICBitmapSource *source,
                                                    WICBitmapTransformOptions options)
{
    WicTransform *t = transform_from(This);
    if (!source)
        return E_INVALIDARG;
    if (options & ~(UINT)(3 | WICBitmapTransformFlipHorizontal | WICBitmapTransformFlipVertical))
        return E_INVALIDARG;
    EnterCriticalSection(&t->lock);
    if (t->ready) {
        LeaveCriticalSection(&t->lock);
        return WINCODEC_ERR_WRONGSTATE;
    }
    UINT sw, sh;
    GUID format;
    HRESULT hr = source->lpVtbl->GetSize(source, &sw, &sh);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetPixelFormat(source, &format);
    if (SUCCEEDED(hr)) {
        source->lpVtbl->AddRef(source);
        t->source = source;
        t->format = format;
        t->src_width = sw;
        t->src_height = sh;
        t->options = options;
        t->width = (options & 1) ? sh : sw;
        t->height = (options & 1) ? sw : sh;
        t->ready = TRUE;
    }
    LeaveCriticalSection(&t->lock);
    return hr;
}

static const IWICBitmapFlipRotatorVtbl g_rotator_vtbl = {
    WIC_IUNKNOWN_FUNCS, TRANSFORM_SOURCE,
    rotator_copy_pixels, rotator_initialize,
};
static const WicIfaceEntry g_rotator_ifaces[] = {
    WIC_ENTRY(WicTransform, IID_IWICBitmapFlipRotator, slot),
    WIC_ENTRY(WicTransform, IID_IWICBitmapSource, slot),
};
static const WicClass g_rotator_class = {transform_destroy, g_rotator_ifaces, 2};

HRESULT WicFlipRotatorCreate(IWICBitmapFlipRotator **out)
{
    *out = transform_new(&g_rotator_class, &g_rotator_vtbl);
    return *out ? S_OK : E_OUTOFMEMORY;
}
