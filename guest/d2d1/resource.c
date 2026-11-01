/*
 * pixel formats, bitmaps, gradients and brushes.
 */
#include "d2d_private.h"

#define F_UNKNOWN 0
#define F_R32G32B32A32_FLOAT 2
#define F_R16G16B16A16_FLOAT 10
#define F_R16G16B16A16_UNORM 11
#define F_R10G10B10A2_UNORM 24
#define F_R8G8B8A8_UNORM 28
#define F_R8G8B8A8_UNORM_SRGB 29
#define F_R8_UNORM 61
#define F_A8_UNORM 65
#define F_B8G8R8A8_UNORM 87
#define F_B8G8R8X8_UNORM 88
#define F_B8G8R8A8_UNORM_SRGB 91
#define F_B8G8R8X8_UNORM_SRGB 93

/* ------------------------------------------------------------------ pixel helpers */

uint32_t D2dPremulPixel(uint32_t p)
{
    uint32_t a = p >> 24;
    if (a == 255)
        return p;
    if (!a)
        return 0;
    uint32_t r = mime_vec_div255(((p >> 16) & 255) * a);
    uint32_t g = mime_vec_div255(((p >> 8) & 255) * a);
    uint32_t b = mime_vec_div255((p & 255) * a);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t D2dUnpremulPixel(uint32_t p)
{
    uint32_t a = p >> 24;
    if (a == 255 || !a)
        return a ? p : 0;
    uint32_t r = ((p >> 16) & 255) * 255 / a, g = ((p >> 8) & 255) * 255 / a, b = (p & 255) * 255 / a;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t D2dScalePixel(uint32_t p, uint32_t alpha)
{
    if (alpha >= 255)
        return p;
    uint32_t a = mime_vec_div255((p >> 24) * alpha);
    uint32_t r = mime_vec_div255(((p >> 16) & 255) * alpha);
    uint32_t g = mime_vec_div255(((p >> 8) & 255) * alpha);
    uint32_t b = mime_vec_div255((p & 255) * alpha);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

BOOL D2dSurfaceInit(D2dSurface *s, int w, int h)
{
    s->w = w;
    s->h = h;
    s->px = nullptr;
    if (w < 0 || h < 0 || (SIZE_T)w * h > (SIZE_T)(256 << 20) / 4)
        return FALSE;
    s->px = D2dAllocZero(((SIZE_T)w * h + 1) * 4);
    return s->px != nullptr;
}

void D2dSurfaceFree(D2dSurface *s)
{
    D2dFree(s->px);
    s->px = nullptr;
    s->w = s->h = 0;
}

/* ------------------------------------------------------------------ formats */

BOOL D2dFormatFromDxgi(DXGI_FORMAT f, D2dFormat *out, BOOL *srgb)
{
    *srgb = FALSE;
    switch ((int)f) {
    case F_B8G8R8A8_UNORM:
        *out = D2D_FMT_BGRA8;
        return TRUE;
    case F_B8G8R8A8_UNORM_SRGB:
        *out = D2D_FMT_BGRA8;
        *srgb = TRUE;
        return TRUE;
    case F_B8G8R8X8_UNORM:
        *out = D2D_FMT_BGRX8;
        return TRUE;
    case F_B8G8R8X8_UNORM_SRGB:
        *out = D2D_FMT_BGRX8;
        *srgb = TRUE;
        return TRUE;
    case F_R8G8B8A8_UNORM:
        *out = D2D_FMT_RGBA8;
        return TRUE;
    case F_R8G8B8A8_UNORM_SRGB:
        *out = D2D_FMT_RGBA8;
        *srgb = TRUE;
        return TRUE;
    case F_A8_UNORM:
        *out = D2D_FMT_A8;
        return TRUE;
    case F_R8_UNORM:
        *out = D2D_FMT_R8;
        return TRUE;
    case F_R16G16B16A16_UNORM:
        *out = D2D_FMT_RGBA16;
        return TRUE;
    case F_R16G16B16A16_FLOAT:
        *out = D2D_FMT_RGBA16F;
        return TRUE;
    case F_R32G32B32A32_FLOAT:
        *out = D2D_FMT_RGBA32F;
        return TRUE;
    case F_R10G10B10A2_UNORM:
        *out = D2D_FMT_RGB10A2;
        return TRUE;
    default:
        return FALSE;
    }
}

UINT32 D2dFormatBytes(D2dFormat f)
{
    switch (f) {
    case D2D_FMT_A8:
    case D2D_FMT_R8:
        return 1;
    case D2D_FMT_RGBA16:
    case D2D_FMT_RGBA16F:
        return 8;
    case D2D_FMT_RGBA32F:
        return 16;
    default:
        return 4;
    }
}

D2D1_ALPHA_MODE D2dDefaultAlpha(DXGI_FORMAT f)
{
    switch ((int)f) {
    case F_A8_UNORM:
    case F_B8G8R8A8_UNORM:
    case F_B8G8R8A8_UNORM_SRGB:
    case F_R8G8B8A8_UNORM:
    case F_R8G8B8A8_UNORM_SRGB:
    case F_R16G16B16A16_UNORM:
    case F_R16G16B16A16_FLOAT:
    case F_R32G32B32A32_FLOAT:
    case F_R10G10B10A2_UNORM:
        return D2D1_ALPHA_MODE_PREMULTIPLIED;
    default:
        return D2D1_ALPHA_MODE_IGNORE;
    }
}

static float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000) << 16, exp = (h >> 10) & 31, man = h & 1023;
    union {
        uint32_t u;
        float f;
    } v;
    if (!exp) {
        if (!man) {
            v.u = sign;
            return v.f;
        }
        float f = (float)man / 1024.0f * 6.103515625e-5f;
        return sign ? -f : f;
    }
    if (exp == 31) {
        v.u = sign | 0x7f800000u | (man << 13);
        return v.f;
    }
    v.u = sign | ((exp + 112) << 23) | (man << 13);
    return v.f;
}

static uint16_t float_to_half(float f)
{
    union {
        uint32_t u;
        float f;
    } v = {.f = f};
    uint32_t sign = (v.u >> 16) & 0x8000, e = (v.u >> 23) & 255, man = v.u & 0x7fffff;
    if (e == 255)
        return (uint16_t)(sign | 0x7c00 | (man ? 0x200 : 0));
    int exp = (int)e - 127 + 15;
    if (exp >= 31)
        return (uint16_t)(sign | 0x7c00);
    if (exp <= 0) {
        if (exp < -10)
            return (uint16_t)sign;
        man |= 0x800000;
        uint32_t shift = (uint32_t)(14 - exp);
        return (uint16_t)(sign | ((man + (1u << (shift - 1))) >> shift));
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | ((man + 0x1000) >> 13));
}

static uint32_t pack_f(float r, float g, float b, float a, D2D1_ALPHA_MODE alpha, BOOL scrgb)
{
    if (alpha == D2D1_ALPHA_MODE_IGNORE)
        a = 1.0f;
    if (scrgb) {
        r = D2dLinearToSrgb(r);
        g = D2dLinearToSrgb(g);
        b = D2dLinearToSrgb(b);
    }
    if (alpha == D2D1_ALPHA_MODE_PREMULTIPLIED) {
        if (a > 0.0f) {
            r /= a;
            g /= a;
            b /= a;
        }
        if (scrgb) {
            /* premultiplied linear values were divided before the transfer curve. */
        }
    }
    return D2dPackPremul(r, g, b, a);
}

void D2dFormatLoad(D2dFormat f, D2D1_ALPHA_MODE alpha, BOOL srgb, const void *src, uint32_t *dst, UINT32 n)
{
    const uint8_t *s = src;
    switch (f) {
    case D2D_FMT_BGRA8:
        if (alpha == D2D1_ALPHA_MODE_STRAIGHT) {
            for (UINT32 i = 0; i < n; i++)
                dst[i] = D2dPremulPixel(((const uint32_t *)src)[i]);
        } else if (alpha == D2D1_ALPHA_MODE_IGNORE) {
            for (UINT32 i = 0; i < n; i++)
                dst[i] = ((const uint32_t *)src)[i] | 0xff000000u;
        } else {
            memcpy(dst, src, (SIZE_T)n * 4);
        }
        break;
    case D2D_FMT_BGRX8:
        for (UINT32 i = 0; i < n; i++)
            dst[i] = ((const uint32_t *)src)[i] | 0xff000000u;
        break;
    case D2D_FMT_RGBA8:
        for (UINT32 i = 0; i < n; i++) {
            uint32_t p = ((const uint32_t *)src)[i];
            uint32_t v = (p & 0xff00ff00u) | ((p & 0xff) << 16) | ((p >> 16) & 0xff);
            if (alpha == D2D1_ALPHA_MODE_STRAIGHT)
                v = D2dPremulPixel(v);
            else if (alpha == D2D1_ALPHA_MODE_IGNORE)
                v |= 0xff000000u;
            dst[i] = v;
        }
        break;
    case D2D_FMT_A8:
        for (UINT32 i = 0; i < n; i++)
            dst[i] = (uint32_t)s[i] << 24;
        break;
    case D2D_FMT_R8:
        for (UINT32 i = 0; i < n; i++)
            dst[i] = 0xff000000u | ((uint32_t)s[i] << 16) | ((uint32_t)s[i] << 8) | s[i];
        break;
    case D2D_FMT_RGBA16:
        for (UINT32 i = 0; i < n; i++) {
            const uint16_t *p = (const uint16_t *)src + i * 4;
            dst[i] = pack_f(p[0] / 65535.0f, p[1] / 65535.0f, p[2] / 65535.0f, p[3] / 65535.0f, alpha, FALSE);
        }
        break;
    case D2D_FMT_RGBA16F:
        for (UINT32 i = 0; i < n; i++) {
            const uint16_t *p = (const uint16_t *)src + i * 4;
            dst[i] = pack_f(half_to_float(p[0]), half_to_float(p[1]), half_to_float(p[2]), half_to_float(p[3]), alpha,
                            !srgb);
        }
        break;
    case D2D_FMT_RGBA32F:
        for (UINT32 i = 0; i < n; i++) {
            const float *p = (const float *)src + i * 4;
            dst[i] = pack_f(p[0], p[1], p[2], p[3], alpha, !srgb);
        }
        break;
    case D2D_FMT_RGB10A2:
        for (UINT32 i = 0; i < n; i++) {
            uint32_t p = ((const uint32_t *)src)[i];
            dst[i] = pack_f((p & 1023) / 1023.0f, ((p >> 10) & 1023) / 1023.0f, ((p >> 20) & 1023) / 1023.0f,
                            (p >> 30) / 3.0f, alpha, FALSE);
        }
        break;
    default:
        break;
    }
}

void D2dFormatStore(D2dFormat f, D2D1_ALPHA_MODE alpha, BOOL srgb, const uint32_t *src, void *dst, UINT32 n)
{
    uint8_t *d = dst;
    switch (f) {
    case D2D_FMT_BGRA8:
        if (alpha == D2D1_ALPHA_MODE_STRAIGHT) {
            for (UINT32 i = 0; i < n; i++)
                ((uint32_t *)dst)[i] = D2dUnpremulPixel(src[i]);
        } else {
            memcpy(dst, src, (SIZE_T)n * 4);
        }
        break;
    case D2D_FMT_BGRX8:
        for (UINT32 i = 0; i < n; i++)
            ((uint32_t *)dst)[i] = src[i] | 0xff000000u;
        break;
    case D2D_FMT_RGBA8:
        for (UINT32 i = 0; i < n; i++) {
            uint32_t p = alpha == D2D1_ALPHA_MODE_STRAIGHT ? D2dUnpremulPixel(src[i]) : src[i];
            ((uint32_t *)dst)[i] = (p & 0xff00ff00u) | ((p & 0xff) << 16) | ((p >> 16) & 0xff);
        }
        break;
    case D2D_FMT_A8:
        for (UINT32 i = 0; i < n; i++)
            d[i] = (uint8_t)(src[i] >> 24);
        break;
    case D2D_FMT_R8:
        for (UINT32 i = 0; i < n; i++)
            d[i] = (uint8_t)((src[i] >> 16) & 255);
        break;
    case D2D_FMT_RGBA16:
    case D2D_FMT_RGBA16F:
    case D2D_FMT_RGBA32F:
    case D2D_FMT_RGB10A2:
        for (UINT32 i = 0; i < n; i++) {
            uint32_t p = src[i];
            float a = (p >> 24) / 255.0f;
            float r = ((p >> 16) & 255) / 255.0f, g = ((p >> 8) & 255) / 255.0f, b = (p & 255) / 255.0f;
            /* r, g and b are premultiplied, bring them to straight for formats that want it. */
            if (alpha != D2D1_ALPHA_MODE_PREMULTIPLIED && a > 0.0f) {
                r /= a;
                g /= a;
                b /= a;
            }
            if (f == D2D_FMT_RGBA16F || f == D2D_FMT_RGBA32F) {
                if (!srgb) {
                    if (alpha == D2D1_ALPHA_MODE_PREMULTIPLIED && a > 0.0f) {
                        r = D2dSrgbToLinear(r / a) * a;
                        g = D2dSrgbToLinear(g / a) * a;
                        b = D2dSrgbToLinear(b / a) * a;
                    } else {
                        r = D2dSrgbToLinear(r);
                        g = D2dSrgbToLinear(g);
                        b = D2dSrgbToLinear(b);
                    }
                }
                if (f == D2D_FMT_RGBA16F) {
                    uint16_t *o = (uint16_t *)dst + i * 4;
                    o[0] = float_to_half(r);
                    o[1] = float_to_half(g);
                    o[2] = float_to_half(b);
                    o[3] = float_to_half(a);
                } else {
                    float *o = (float *)dst + i * 4;
                    o[0] = r;
                    o[1] = g;
                    o[2] = b;
                    o[3] = a;
                }
            } else if (f == D2D_FMT_RGBA16) {
                uint16_t *o = (uint16_t *)dst + i * 4;
                o[0] = (uint16_t)(D2dClampF(r, 0, 1) * 65535.0f + 0.5f);
                o[1] = (uint16_t)(D2dClampF(g, 0, 1) * 65535.0f + 0.5f);
                o[2] = (uint16_t)(D2dClampF(b, 0, 1) * 65535.0f + 0.5f);
                o[3] = (uint16_t)(a * 65535.0f + 0.5f);
            } else {
                ((uint32_t *)dst)[i] = (uint32_t)(D2dClampF(r, 0, 1) * 1023.0f + 0.5f) |
                                       ((uint32_t)(D2dClampF(g, 0, 1) * 1023.0f + 0.5f) << 10) |
                                       ((uint32_t)(D2dClampF(b, 0, 1) * 1023.0f + 0.5f) << 20) |
                                       ((uint32_t)(a * 3.0f + 0.5f) << 30);
            }
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ bitmaps */

static D2dClass g_bitmap_class;

D2dBitmap *D2dBitmapOf(void *p) { return D2dIsOfClass(p, &g_bitmap_class) ? (D2dBitmap *)p : nullptr; }

static void bitmap_destroy(D2dObj *o)
{
    D2dBitmap *b = (D2dBitmap *)o;
    D2dFree(b->native);
    D2dSurfaceFree(&b->view);
    D2D_RELEASE(b->surface);
    D2D_RELEASE(b->color_context);
}

const D2dSurface *D2dBitmapView(D2dBitmap *b)
{
    if (!b->view_valid) {
        if (!b->view.px && !D2dSurfaceInit(&b->view, (int)b->width, (int)b->height))
            return nullptr;
        for (UINT32 y = 0; y < b->height; y++)
            D2dFormatLoad(b->format, b->alpha, b->srgb, b->native + (SIZE_T)y * b->stride, D2dSurfaceRow(&b->view, (int)y),
                          b->width);
        b->view_valid = TRUE;
    }
    return &b->view;
}

void D2dBitmapCommit(D2dBitmap *b)
{
    if (!b->view_valid || !b->view.px)
        return;
    for (UINT32 y = 0; y < b->height; y++)
        D2dFormatStore(b->format, b->alpha, b->srgb, D2dSurfaceRow(&b->view, (int)y), b->native + (SIZE_T)y * b->stride,
                       b->width);
}

HRESULT D2dBitmapCreate(D2dFactory *f, UINT32 w, UINT32 h, DXGI_FORMAT format, D2D1_ALPHA_MODE alpha, float dpi_x,
                        float dpi_y, D2D1_BITMAP_OPTIONS options, BOOL v1, D2dBitmap **out)
{
    D2dFormat fmt;
    BOOL srgb;
    if ((int)format == F_UNKNOWN) {
        format = (DXGI_FORMAT)F_B8G8R8A8_UNORM;
        if (alpha == D2D1_ALPHA_MODE_UNKNOWN)
            alpha = D2D1_ALPHA_MODE_PREMULTIPLIED;
    }
    if (!D2dFormatFromDxgi(format, &fmt, &srgb))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR;
    if (alpha == D2D1_ALPHA_MODE_UNKNOWN)
        alpha = D2dDefaultAlpha(format);
    if (alpha > D2D1_ALPHA_MODE_IGNORE)
        return D2DERR_UNSUPPORTED_VERSION == 0 ? E_INVALIDARG : E_INVALIDARG;
    if (w > 16384 || h > 16384)
        return D2DERR_EXCEEDS_MAX_BITMAP_SIZE;
    D2dBitmap *b = D2dNew(sizeof(*b), nullptr, &g_bitmap_class, f);
    if (!b)
        return E_OUTOFMEMORY;
    extern const void *const d2d_bitmap_vtbl[];
    b->obj.vtbl = d2d_bitmap_vtbl;
    b->width = w;
    b->height = h;
    b->dxgi = format;
    b->format = fmt;
    b->srgb = srgb;
    b->alpha = alpha;
    b->dpi_x = dpi_x > 0 ? dpi_x : 96.0f;
    b->dpi_y = dpi_y > 0 ? dpi_y : 96.0f;
    b->options = options;
    b->v1 = v1;
    b->stride = (w * D2dFormatBytes(fmt) + 3) & ~3u;
    b->native = D2dAllocZero((SIZE_T)b->stride * (h ? h : 1));
    if (!b->native) {
        D2dRelease(b);
        return E_OUTOFMEMORY;
    }
    *out = b;
    return S_OK;
}

HRESULT D2dBitmapCopyIn(D2dBitmap *b, UINT32 x, UINT32 y, const void *src, UINT32 pitch, UINT32 w, UINT32 h)
{
    UINT32 bpp = D2dFormatBytes(b->format);
    for (UINT32 j = 0; j < h; j++)
        memcpy(b->native + (SIZE_T)(y + j) * b->stride + (SIZE_T)x * bpp, (const BYTE *)src + (SIZE_T)j * pitch,
               (SIZE_T)w * bpp);
    b->view_valid = FALSE;
    return S_OK;
}

static const GUID IID_Tex2D = {0x6F15AAF2, 0xD208, 0x4E89, {0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C}};

HRESULT D2dSurfaceTexture(IDXGISurface *surface, ID3D11Texture2D **tex)
{
    return surface->lpVtbl->QueryInterface(surface, &IID_Tex2D, (void **)tex);
}

/* the texture's pixels copied into native memory of the same format, or native memory copied into the texture. */
HRESULT D2dTextureTransfer(IDXGISurface *surface, BYTE *native, UINT32 stride, UINT32 width, UINT32 height, BOOL write)
{
    ID3D11Texture2D *tex;
    HRESULT hr = D2dSurfaceTexture(surface, &tex);
    if (FAILED(hr))
        return hr;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    ((ID3D11DeviceChild *)tex)->lpVtbl->GetDevice((ID3D11DeviceChild *)tex, &dev);
    D3D11_TEXTURE2D_DESC desc;
    tex->lpVtbl->GetDesc(tex, &desc);
    if (!dev) {
        tex->lpVtbl->Release(tex);
        return E_FAIL;
    }
    dev->lpVtbl->GetImmediateContext(dev, &ctx);
    UINT32 bpp = 0;
    D2dFormat fmt;
    BOOL srgb;
    if (D2dFormatFromDxgi(desc.Format, &fmt, &srgb))
        bpp = D2dFormatBytes(fmt);
    if (!bpp || !ctx) {
        hr = E_FAIL;
    } else if (write) {
        D3D11_BOX box = {0, 0, 0, width < desc.Width ? width : desc.Width, height < desc.Height ? height : desc.Height, 1};
        ctx->lpVtbl->UpdateSubresource(ctx, (ID3D11Resource *)tex, 0, &box, native, stride, 0);
    } else {
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        ID3D11Texture2D *stage;
        hr = dev->lpVtbl->CreateTexture2D(dev, &sd, nullptr, &stage);
        if (SUCCEEDED(hr)) {
            ctx->lpVtbl->CopyResource(ctx, (ID3D11Resource *)stage, (ID3D11Resource *)tex);
            D3D11_MAPPED_SUBRESOURCE mapped;
            hr = ctx->lpVtbl->Map(ctx, (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(hr)) {
                UINT32 w = width < desc.Width ? width : desc.Width, h = height < desc.Height ? height : desc.Height;
                for (UINT32 y = 0; y < h; y++)
                    memcpy(native + (SIZE_T)y * stride, (const BYTE *)mapped.pData + (SIZE_T)y * mapped.RowPitch,
                           (SIZE_T)w * bpp);
                ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)stage, 0);
            }
            stage->lpVtbl->Release(stage);
        }
    }
    if (ctx)
        ctx->lpVtbl->Release(ctx);
    dev->lpVtbl->Release(dev);
    tex->lpVtbl->Release(tex);
    return hr;
}

HRESULT D2dBitmapReadSurface(D2dBitmap *b)
{
    if (!b->surface)
        return S_OK;
    HRESULT hr = D2dTextureTransfer(b->surface, b->native, b->stride, b->width, b->height, FALSE);
    b->view_valid = FALSE;
    return hr;
}

HRESULT D2dBitmapWriteSurface(D2dBitmap *b)
{
    if (!b->surface)
        return S_OK;
    return D2dTextureTransfer(b->surface, b->native, b->stride, b->width, b->height, TRUE);
}

/* ------------------------------------------------------------------ bitmap methods */

#define BSELF D2dBitmap *b = (D2dBitmap *)This

static D2D1_SIZE_F STDMETHODCALLTYPE bm_size(ID2D1Bitmap1 *This)
{
    BSELF;
    D2D1_SIZE_F s = {b->width * 96.0f / b->dpi_x, b->height * 96.0f / b->dpi_y};
    return s;
}
static D2D1_SIZE_U STDMETHODCALLTYPE bm_pixel_size(ID2D1Bitmap1 *This)
{
    BSELF;
    D2D1_SIZE_U s = {b->width, b->height};
    return s;
}
static D2D1_PIXEL_FORMAT STDMETHODCALLTYPE bm_format(ID2D1Bitmap1 *This)
{
    BSELF;
    D2D1_PIXEL_FORMAT f = {b->dxgi, b->alpha};
    return f;
}
static void STDMETHODCALLTYPE bm_dpi(ID2D1Bitmap1 *This, FLOAT *x, FLOAT *y)
{
    BSELF;
    *x = b->dpi_x;
    *y = b->dpi_y;
}

static HRESULT clip_rect(const D2dBitmap *b, const D2D1_RECT_U *r, UINT32 *x, UINT32 *y, UINT32 *w, UINT32 *h)
{
    if (!r) {
        *x = *y = 0;
        *w = b->width;
        *h = b->height;
        return S_OK;
    }
    if (r->right < r->left || r->bottom < r->top || r->right > b->width || r->bottom > b->height)
        return E_INVALIDARG;
    *x = r->left;
    *y = r->top;
    *w = r->right - r->left;
    *h = r->bottom - r->top;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bm_copy_bitmap(ID2D1Bitmap1 *This, const D2D1_POINT_2U *dest, ID2D1Bitmap *source,
                                                const D2D1_RECT_U *src_rect)
{
    BSELF;
    D2dBitmap *s = D2dBitmapOf(source);
    if (!s)
        return E_INVALIDARG;
    UINT32 sx, sy, w, h;
    HRESULT hr = clip_rect(s, src_rect, &sx, &sy, &w, &h);
    if (FAILED(hr))
        return hr;
    UINT32 dx = dest ? dest->x : 0, dy = dest ? dest->y : 0;
    if (dx + w > b->width || dy + h > b->height)
        return E_INVALIDARG;
    const D2dSurface *view = D2dBitmapView(s);
    if (!view)
        return E_OUTOFMEMORY;
    const D2dSurface *dview = D2dBitmapView(b);
    if (!dview)
        return E_OUTOFMEMORY;
    for (UINT32 j = 0; j < h; j++)
        memcpy(D2dSurfaceRow(dview, (int)(dy + j)) + dx, D2dSurfaceRow(view, (int)(sy + j)) + sx, (SIZE_T)w * 4);
    D2dBitmapCommit(b);
    D2dBitmapWriteSurface(b);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bm_copy_rt(ID2D1Bitmap1 *This, const D2D1_POINT_2U *dest, ID2D1RenderTarget *target,
                                            const D2D1_RECT_U *src_rect)
{
    BSELF;
    D2dRt *rt = D2dRtOf(target);
    if (!rt)
        return E_INVALIDARG;
    if (rt->drawing)
        return D2DERR_WRONG_STATE;
    D2dSurface *s = &rt->surface;
    UINT32 sx = 0, sy = 0, w = (UINT32)s->w, h = (UINT32)s->h;
    if (src_rect) {
        if (src_rect->right > (UINT32)s->w || src_rect->bottom > (UINT32)s->h || src_rect->right < src_rect->left ||
            src_rect->bottom < src_rect->top)
            return E_INVALIDARG;
        sx = src_rect->left;
        sy = src_rect->top;
        w = src_rect->right - sx;
        h = src_rect->bottom - sy;
    }
    UINT32 dx = dest ? dest->x : 0, dy = dest ? dest->y : 0;
    if (dx + w > b->width || dy + h > b->height)
        return E_INVALIDARG;
    const D2dSurface *dview = D2dBitmapView(b);
    if (!dview)
        return E_OUTOFMEMORY;
    for (UINT32 j = 0; j < h; j++)
        memcpy(D2dSurfaceRow(dview, (int)(dy + j)) + dx, D2dSurfaceRow(s, (int)(sy + j)) + sx, (SIZE_T)w * 4);
    D2dBitmapCommit(b);
    D2dBitmapWriteSurface(b);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bm_copy_memory(ID2D1Bitmap1 *This, const D2D1_RECT_U *dest, const void *data,
                                                UINT32 pitch)
{
    BSELF;
    if (!data)
        return E_INVALIDARG;
    UINT32 x, y, w, h;
    HRESULT hr = clip_rect(b, dest, &x, &y, &w, &h);
    if (FAILED(hr))
        return hr;
    if (b->options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW && false)
        return D2DERR_BITMAP_CANNOT_DRAW;
    hr = D2dBitmapCopyIn(b, x, y, data, pitch, w, h);
    D2dBitmapWriteSurface(b);
    return hr;
}

static void STDMETHODCALLTYPE bm_color_context(ID2D1Bitmap1 *This, ID2D1ColorContext **ctx)
{
    BSELF;
    *ctx = b->color_context;
    D2D_ADDREF(*ctx);
}
static D2D1_BITMAP_OPTIONS STDMETHODCALLTYPE bm_options(ID2D1Bitmap1 *This)
{
    BSELF;
    return b->options;
}
static HRESULT STDMETHODCALLTYPE bm_surface(ID2D1Bitmap1 *This, IDXGISurface **surface)
{
    BSELF;
    *surface = b->surface;
    D2D_ADDREF(*surface);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_map(ID2D1Bitmap1 *This, D2D1_MAP_OPTIONS options, D2D1_MAPPED_RECT *mapped)
{
    BSELF;
    if (!mapped)
        return E_POINTER;
    if (!(b->options & D2D1_BITMAP_OPTIONS_CPU_READ) || b->mapped)
        return D2DERR_INVALID_CALL;
    if (!(options & (D2D1_MAP_OPTIONS_READ | D2D1_MAP_OPTIONS_WRITE)) || ((options & D2D1_MAP_OPTIONS_DISCARD) && !(options & D2D1_MAP_OPTIONS_WRITE)) ||
        ((options & D2D1_MAP_OPTIONS_DISCARD) && (options & D2D1_MAP_OPTIONS_READ)))
        return E_INVALIDARG;
    b->mapped = TRUE;
    mapped->pitch = b->stride;
    mapped->bits = b->native;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_unmap(ID2D1Bitmap1 *This)
{
    BSELF;
    if (!b->mapped)
        return D2DERR_WRONG_STATE;
    b->mapped = FALSE;
    b->view_valid = FALSE;
    return S_OK;
}

const void *const d2d_bitmap_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, bm_size,
                                       bm_pixel_size,     bm_format, bm_dpi,     bm_copy_bitmap,        bm_copy_rt,
                                       bm_copy_memory,    bm_color_context, bm_options, bm_surface,     bm_map,
                                       bm_unmap};

D2D_IIDS(g_bitmap_iids, &IID_ID2D1Resource, &IID_ID2D1Image, &IID_ID2D1Bitmap, &IID_ID2D1Bitmap1);
static D2dClass g_bitmap_class = {"bitmap", g_bitmap_iids, bitmap_destroy, nullptr};

/* ------------------------------------------------------------------ color contexts */

typedef struct D2dColorContext {
    D2dObj obj;
    D2D1_COLOR_SPACE space;
    BYTE *profile;
    UINT32 size;
} D2dColorContext;

static D2dClass g_cc_class;

static D2D1_COLOR_SPACE STDMETHODCALLTYPE cc_space(ID2D1ColorContext *This) { return ((D2dColorContext *)This)->space; }
static UINT32 STDMETHODCALLTYPE cc_size(ID2D1ColorContext *This) { return ((D2dColorContext *)This)->size; }
static HRESULT STDMETHODCALLTYPE cc_profile(ID2D1ColorContext *This, BYTE *profile, UINT32 size)
{
    D2dColorContext *c = (D2dColorContext *)This;
    if (size < c->size)
        return D2DERR_INSUFFICIENT_BUFFER_HR;
    memcpy(profile, c->profile, c->size);
    return S_OK;
}
static const void *const g_cc_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, cc_space, cc_size,
                                        cc_profile};
D2D_IIDS(g_cc_iids, &IID_ID2D1Resource, &IID_ID2D1ColorContext);
static void cc_destroy(D2dObj *o) { D2dFree(((D2dColorContext *)o)->profile); }
static D2dClass g_cc_class = {"color context", g_cc_iids, cc_destroy, nullptr};

HRESULT D2dCreateColorContext(D2dFactory *f, D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 size,
                              ID2D1ColorContext **out)
{
    if (space == D2D1_COLOR_SPACE_CUSTOM && (!profile || !size))
        return E_INVALIDARG;
    if (space > D2D1_COLOR_SPACE_SCRGB)
        return E_INVALIDARG;
    D2dColorContext *c = D2dNew(sizeof(*c), g_cc_vtbl, &g_cc_class, f);
    if (!c)
        return E_OUTOFMEMORY;
    c->space = space;
    if (space == D2D1_COLOR_SPACE_CUSTOM) {
        c->profile = D2dAlloc(size);
        if (!c->profile) {
            D2dRelease(c);
            return E_OUTOFMEMORY;
        }
        memcpy(c->profile, profile, size);
        c->size = size;
    }
    *out = (ID2D1ColorContext *)c;
    return S_OK;
}

/* ------------------------------------------------------------------ gradient stop collections */

static D2dClass g_gradient_class;

D2dGradient *D2dGradientOf(ID2D1GradientStopCollection *g)
{
    return D2dIsOfClass(g, &g_gradient_class) ? (D2dGradient *)g : nullptr;
}

static void gradient_destroy(D2dObj *o) { D2dFree(((D2dGradient *)o)->stops); }

#define GSELF D2dGradient *g = (D2dGradient *)This
static UINT32 STDMETHODCALLTYPE gr_count(ID2D1GradientStopCollection1 *This) { GSELF; return g->count; }
static void STDMETHODCALLTYPE gr_stops(ID2D1GradientStopCollection1 *This, D2D1_GRADIENT_STOP *stops, UINT32 count)
{
    GSELF;
    for (UINT32 i = 0; i < count && i < g->count; i++)
        stops[i] = g->stops[i];
}
static D2D1_GAMMA STDMETHODCALLTYPE gr_gamma(ID2D1GradientStopCollection1 *This) { GSELF; return g->gamma; }
static D2D1_EXTEND_MODE STDMETHODCALLTYPE gr_extend(ID2D1GradientStopCollection1 *This) { GSELF; return g->extend; }
static D2D1_COLOR_SPACE STDMETHODCALLTYPE gr_pre(ID2D1GradientStopCollection1 *This) { GSELF; return g->pre; }
static D2D1_COLOR_SPACE STDMETHODCALLTYPE gr_post(ID2D1GradientStopCollection1 *This) { GSELF; return g->post; }
static D2D1_BUFFER_PRECISION STDMETHODCALLTYPE gr_precision(ID2D1GradientStopCollection1 *This) { GSELF; return g->precision; }
static D2D1_COLOR_INTERPOLATION_MODE STDMETHODCALLTYPE gr_mode(ID2D1GradientStopCollection1 *This) { GSELF; return g->mode; }

static const void *const g_gradient_vtbl[] = {D2dQueryInterface, D2dAddRef,  D2dRelease,  D2dResourceGetFactory, gr_count,
                                              gr_stops,          gr_gamma,   gr_extend,   gr_stops,             gr_pre,
                                              gr_post,           gr_precision, gr_mode};
D2D_IIDS(g_gradient_iids, &IID_ID2D1Resource, &IID_ID2D1GradientStopCollection, &IID_ID2D1GradientStopCollection1);
static D2dClass g_gradient_class = {"gradient stops", g_gradient_iids, gradient_destroy, nullptr};

HRESULT D2dCreateGradient(D2dFactory *f, const D2D1_GRADIENT_STOP *stops, UINT32 count, D2D1_GAMMA gamma,
                          D2D1_EXTEND_MODE extend, BOOL v1, D2dGradient **out)
{
    if (!stops || !count || gamma > D2D1_GAMMA_1_0 || extend > D2D1_EXTEND_MODE_MIRROR)
        return E_INVALIDARG;
    float last = 0.0f;
    for (UINT32 i = 0; i < count; i++) {
        if (!(stops[i].position >= 0.0f && stops[i].position <= 1.0f) || stops[i].position < last)
            return E_INVALIDARG;
        last = stops[i].position;
    }
    D2dGradient *g = D2dNew(sizeof(*g), g_gradient_vtbl, &g_gradient_class, f);
    if (!g)
        return E_OUTOFMEMORY;
    g->stops = D2dAlloc(count * sizeof(*stops));
    if (!g->stops) {
        D2dRelease(g);
        return E_OUTOFMEMORY;
    }
    memcpy(g->stops, stops, count * sizeof(*stops));
    g->count = count;
    g->gamma = gamma;
    g->extend = extend;
    g->v1 = v1;
    g->pre = g->post = D2D1_COLOR_SPACE_SRGB;
    g->precision = D2D1_BUFFER_PRECISION_8BPC_UNORM;
    g->mode = D2D1_COLOR_INTERPOLATION_MODE_PREMULTIPLIED;
    *out = g;
    return S_OK;
}

void D2dGradientBuild(D2dGradient *g)
{
    if (g->lut_valid)
        return;
    for (int i = 0; i < 1024; i++) {
        float t = i / 1023.0f;
        UINT32 k = 0;
        while (k + 1 < g->count && g->stops[k + 1].position <= t)
            k++;
        const D2D1_GRADIENT_STOP *a = &g->stops[k];
        const D2D1_GRADIENT_STOP *b = k + 1 < g->count ? &g->stops[k + 1] : a;
        float u = 0.0f;
        if (t <= g->stops[0].position) {
            b = a = &g->stops[0];
        } else if (b != a && b->position > a->position) {
            u = (t - a->position) / (b->position - a->position);
        } else {
            a = b;
        }
        float c[4] = {a->color.r + (b->color.r - a->color.r) * u, a->color.g + (b->color.g - a->color.g) * u,
                      a->color.b + (b->color.b - a->color.b) * u, a->color.a + (b->color.a - a->color.a) * u};
        if (g->gamma == D2D1_GAMMA_1_0 && g->pre == D2D1_COLOR_SPACE_SRGB) {
            /* the stops are linear values, written to the 8 bit surface through the srgb curve. */
            c[0] = D2dLinearToSrgb(c[0]);
            c[1] = D2dLinearToSrgb(c[1]);
            c[2] = D2dLinearToSrgb(c[2]);
        }
        g->lut[i] = D2dPackPremul(c[0], c[1], c[2], c[3]);
    }
    g->lut_valid = TRUE;
}

/* ------------------------------------------------------------------ brushes */

static D2dClass g_solid_class, g_linear_class, g_radial_class, g_bitmap_brush_class, g_image_brush_class;

D2dBrush *D2dBrushOf(ID2D1Brush *b)
{
    if (!b)
        return nullptr;
    const D2dClass *c = ((D2dObj *)b)->cls;
    if (c == &g_solid_class || c == &g_linear_class || c == &g_radial_class || c == &g_bitmap_brush_class ||
        c == &g_image_brush_class)
        return (D2dBrush *)b;
    return nullptr;
}

static void brush_destroy(D2dObj *o)
{
    D2dBrush *b = (D2dBrush *)o;
    D2D_RELEASE(b->gradient);
    D2D_RELEASE(b->bitmap);
    D2D_RELEASE(b->image);
}

#define BR D2dBrush *b = (D2dBrush *)This
static void STDMETHODCALLTYPE br_set_opacity(ID2D1Brush *This, FLOAT o) { BR; b->opacity = o; }
static void STDMETHODCALLTYPE br_set_transform(ID2D1Brush *This, const D2D1_MATRIX_3X2_F *m) { BR; b->transform = m ? *m : d2d_identity; }
static FLOAT STDMETHODCALLTYPE br_opacity(ID2D1Brush *This) { BR; return b->opacity; }
static void STDMETHODCALLTYPE br_transform(ID2D1Brush *This, D2D1_MATRIX_3X2_F *m) { BR; *m = b->transform; }

#define BRUSH_BASE D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, br_set_opacity, br_set_transform, br_opacity, br_transform

static void STDMETHODCALLTYPE sb_set_color(ID2D1SolidColorBrush *This, const D2D1_COLOR_F *c) { BR; b->color = *c; }
static D2D1_COLOR_F *STDMETHODCALLTYPE sb_color(ID2D1SolidColorBrush *This, D2D1_COLOR_F *r) { BR; *r = b->color; return r; }

static void STDMETHODCALLTYPE lb_set_start(ID2D1LinearGradientBrush *This, D2D1_POINT_2F p) { BR; b->p0 = p; }
static void STDMETHODCALLTYPE lb_set_end(ID2D1LinearGradientBrush *This, D2D1_POINT_2F p) { BR; b->p1 = p; }
static D2D1_POINT_2F STDMETHODCALLTYPE lb_start(ID2D1LinearGradientBrush *This) { BR; return b->p0; }
static D2D1_POINT_2F STDMETHODCALLTYPE lb_end(ID2D1LinearGradientBrush *This) { BR; return b->p1; }
static void STDMETHODCALLTYPE gb_stops(ID2D1LinearGradientBrush *This, ID2D1GradientStopCollection **s)
{
    BR;
    *s = (ID2D1GradientStopCollection *)b->gradient;
    D2D_ADDREF(*s);
}

static void STDMETHODCALLTYPE rb_set_center(ID2D1RadialGradientBrush *This, D2D1_POINT_2F p) { BR; b->p0 = p; }
static void STDMETHODCALLTYPE rb_set_offset(ID2D1RadialGradientBrush *This, D2D1_POINT_2F p) { BR; b->origin = p; }
static void STDMETHODCALLTYPE rb_set_rx(ID2D1RadialGradientBrush *This, FLOAT v) { BR; b->rx = v; }
static void STDMETHODCALLTYPE rb_set_ry(ID2D1RadialGradientBrush *This, FLOAT v) { BR; b->ry = v; }
static D2D1_POINT_2F STDMETHODCALLTYPE rb_center(ID2D1RadialGradientBrush *This) { BR; return b->p0; }
static D2D1_POINT_2F STDMETHODCALLTYPE rb_offset(ID2D1RadialGradientBrush *This) { BR; return b->origin; }
static FLOAT STDMETHODCALLTYPE rb_rx(ID2D1RadialGradientBrush *This) { BR; return b->rx; }
static FLOAT STDMETHODCALLTYPE rb_ry(ID2D1RadialGradientBrush *This) { BR; return b->ry; }

static void STDMETHODCALLTYPE bb_set_x(ID2D1BitmapBrush1 *This, D2D1_EXTEND_MODE m) { BR; b->extend_x = m; }
static void STDMETHODCALLTYPE bb_set_y(ID2D1BitmapBrush1 *This, D2D1_EXTEND_MODE m) { BR; b->extend_y = m; }
static void STDMETHODCALLTYPE bb_set_interp(ID2D1BitmapBrush1 *This, D2D1_BITMAP_INTERPOLATION_MODE m)
{
    BR;
    b->interpolation = m == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
}
static void STDMETHODCALLTYPE bb_set_bitmap(ID2D1BitmapBrush1 *This, ID2D1Bitmap *bm)
{
    BR;
    D2dBitmap *n = D2dBitmapOf(bm);
    if (!n)
        return;
    D2D_ADDREF(n);
    D2D_RELEASE(b->bitmap);
    b->bitmap = n;
}
static D2D1_EXTEND_MODE STDMETHODCALLTYPE bb_x(ID2D1BitmapBrush1 *This) { BR; return b->extend_x; }
static D2D1_EXTEND_MODE STDMETHODCALLTYPE bb_y(ID2D1BitmapBrush1 *This) { BR; return b->extend_y; }
static D2D1_BITMAP_INTERPOLATION_MODE STDMETHODCALLTYPE bb_interp(ID2D1BitmapBrush1 *This)
{
    BR;
    return b->interpolation == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                                                          : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
}
static void STDMETHODCALLTYPE bb_bitmap(ID2D1BitmapBrush1 *This, ID2D1Bitmap **bm)
{
    BR;
    *bm = (ID2D1Bitmap *)b->bitmap;
    D2D_ADDREF(*bm);
}
static void STDMETHODCALLTYPE bb_set_interp1(ID2D1BitmapBrush1 *This, D2D1_INTERPOLATION_MODE m) { BR; b->interpolation = m; }
static D2D1_INTERPOLATION_MODE STDMETHODCALLTYPE bb_interp1(ID2D1BitmapBrush1 *This) { BR; return b->interpolation; }

static void STDMETHODCALLTYPE ib_set_image(ID2D1ImageBrush *This, ID2D1Image *img)
{
    BR;
    D2D_ADDREF(img);
    D2D_RELEASE(b->image);
    b->image = img;
}
static void STDMETHODCALLTYPE ib_set_x(ID2D1ImageBrush *This, D2D1_EXTEND_MODE m) { BR; b->extend_x = m; }
static void STDMETHODCALLTYPE ib_set_y(ID2D1ImageBrush *This, D2D1_EXTEND_MODE m) { BR; b->extend_y = m; }
static void STDMETHODCALLTYPE ib_set_interp(ID2D1ImageBrush *This, D2D1_INTERPOLATION_MODE m) { BR; b->interpolation = m; }
static void STDMETHODCALLTYPE ib_set_source(ID2D1ImageBrush *This, const D2D1_RECT_F *r) { BR; b->source = *r; }
static void STDMETHODCALLTYPE ib_image(ID2D1ImageBrush *This, ID2D1Image **img)
{
    BR;
    *img = b->image;
    D2D_ADDREF(*img);
}
static D2D1_EXTEND_MODE STDMETHODCALLTYPE ib_x(ID2D1ImageBrush *This) { BR; return b->extend_x; }
static D2D1_EXTEND_MODE STDMETHODCALLTYPE ib_y(ID2D1ImageBrush *This) { BR; return b->extend_y; }
static D2D1_INTERPOLATION_MODE STDMETHODCALLTYPE ib_interp(ID2D1ImageBrush *This) { BR; return b->interpolation; }
static void STDMETHODCALLTYPE ib_source(ID2D1ImageBrush *This, D2D1_RECT_F *r) { BR; *r = b->source; }

static const void *const g_solid_vtbl[] = {BRUSH_BASE, sb_set_color, sb_color};
static const void *const g_linear_vtbl[] = {BRUSH_BASE, lb_set_start, lb_set_end, lb_start, lb_end, gb_stops};
static const void *const g_radial_vtbl[] = {BRUSH_BASE, rb_set_center, rb_set_offset, rb_set_rx, rb_set_ry, rb_center,
                                            rb_offset,  rb_rx,         rb_ry,         gb_stops};
static const void *const g_bitmap_brush_vtbl[] = {BRUSH_BASE, bb_set_x, bb_set_y, bb_set_interp, bb_set_bitmap, bb_x,
                                                  bb_y,       bb_interp, bb_bitmap, bb_set_interp1, bb_interp1};
static const void *const g_image_brush_vtbl[] = {BRUSH_BASE, ib_set_image, ib_set_x, ib_set_y, ib_set_interp,
                                                 ib_set_source, ib_image, ib_x, ib_y, ib_interp, ib_source};

D2D_IIDS(g_solid_iids, &IID_ID2D1Resource, &IID_ID2D1Brush, &IID_ID2D1SolidColorBrush);
D2D_IIDS(g_linear_iids, &IID_ID2D1Resource, &IID_ID2D1Brush, &IID_ID2D1LinearGradientBrush);
D2D_IIDS(g_radial_iids, &IID_ID2D1Resource, &IID_ID2D1Brush, &IID_ID2D1RadialGradientBrush);
D2D_IIDS(g_bitmap_brush_iids, &IID_ID2D1Resource, &IID_ID2D1Brush, &IID_ID2D1BitmapBrush, &IID_ID2D1BitmapBrush1);
D2D_IIDS(g_image_brush_iids, &IID_ID2D1Resource, &IID_ID2D1Brush, &IID_ID2D1ImageBrush);

static D2dClass g_solid_class = {"solid brush", g_solid_iids, brush_destroy, nullptr};
static D2dClass g_linear_class = {"linear brush", g_linear_iids, brush_destroy, nullptr};
static D2dClass g_radial_class = {"radial brush", g_radial_iids, brush_destroy, nullptr};
static D2dClass g_bitmap_brush_class = {"bitmap brush", g_bitmap_brush_iids, brush_destroy, nullptr};
static D2dClass g_image_brush_class = {"image brush", g_image_brush_iids, brush_destroy, nullptr};

static D2dBrush *brush_new(D2dFactory *f, const void *vtbl, const D2dClass *cls, int kind, const D2D1_BRUSH_PROPERTIES *p)
{
    D2dBrush *b = D2dNew(sizeof(*b), vtbl, cls, f);
    if (!b)
        return nullptr;
    b->kind = kind;
    b->opacity = p ? p->opacity : 1.0f;
    b->transform = p ? p->transform : d2d_identity;
    return b;
}

HRESULT D2dCreateSolidBrush(D2dFactory *f, const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *props,
                            ID2D1SolidColorBrush **out)
{
    D2dBrush *b = brush_new(f, g_solid_vtbl, &g_solid_class, D2D_BRUSH_SOLID, props);
    if (!b)
        return E_OUTOFMEMORY;
    b->color = color ? *color : (D2D1_COLOR_F){0, 0, 0, 1};
    *out = (ID2D1SolidColorBrush *)b;
    return S_OK;
}

HRESULT D2dCreateLinearBrush(D2dFactory *f, const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gp,
                             const D2D1_BRUSH_PROPERTIES *props, ID2D1GradientStopCollection *stops,
                             ID2D1LinearGradientBrush **out)
{
    D2dGradient *g = D2dGradientOf(stops);
    if (!g || !gp)
        return E_INVALIDARG;
    D2dBrush *b = brush_new(f, g_linear_vtbl, &g_linear_class, D2D_BRUSH_LINEAR, props);
    if (!b)
        return E_OUTOFMEMORY;
    b->p0 = gp->startPoint;
    b->p1 = gp->endPoint;
    b->gradient = g;
    D2D_ADDREF(g);
    *out = (ID2D1LinearGradientBrush *)b;
    return S_OK;
}

HRESULT D2dCreateRadialBrush(D2dFactory *f, const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gp,
                             const D2D1_BRUSH_PROPERTIES *props, ID2D1GradientStopCollection *stops,
                             ID2D1RadialGradientBrush **out)
{
    D2dGradient *g = D2dGradientOf(stops);
    if (!g || !gp)
        return E_INVALIDARG;
    D2dBrush *b = brush_new(f, g_radial_vtbl, &g_radial_class, D2D_BRUSH_RADIAL, props);
    if (!b)
        return E_OUTOFMEMORY;
    b->p0 = gp->center;
    b->origin = gp->gradientOriginOffset;
    b->rx = gp->radiusX;
    b->ry = gp->radiusY;
    b->gradient = g;
    D2D_ADDREF(g);
    *out = (ID2D1RadialGradientBrush *)b;
    return S_OK;
}

HRESULT D2dCreateBitmapBrush(D2dFactory *f, ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES1 *bp,
                             const D2D1_BRUSH_PROPERTIES *props, BOOL v1, ID2D1BitmapBrush1 **out)
{
    D2dBitmap *bm = D2dBitmapOf(bitmap);
    if (!bm)
        return E_INVALIDARG;
    D2dBrush *b = brush_new(f, g_bitmap_brush_vtbl, &g_bitmap_brush_class, D2D_BRUSH_BITMAP, props);
    if (!b)
        return E_OUTOFMEMORY;
    b->bitmap = bm;
    D2D_ADDREF(bm);
    if (bp) {
        b->extend_x = bp->extendModeX;
        b->extend_y = bp->extendModeY;
        b->interpolation = bp->interpolationMode;
    } else {
        b->interpolation = D2D1_INTERPOLATION_MODE_LINEAR;
    }
    *out = (ID2D1BitmapBrush1 *)b;
    return S_OK;
}

HRESULT D2dCreateImageBrush(D2dFactory *f, ID2D1Image *image, const D2D1_IMAGE_BRUSH_PROPERTIES *ip,
                            const D2D1_BRUSH_PROPERTIES *props, ID2D1ImageBrush **out)
{
    if (!image || !ip)
        return E_INVALIDARG;
    D2dBrush *b = brush_new(f, g_image_brush_vtbl, &g_image_brush_class, D2D_BRUSH_IMAGE, props);
    if (!b)
        return E_OUTOFMEMORY;
    b->image = image;
    D2D_ADDREF(image);
    b->source = ip->sourceRectangle;
    b->extend_x = ip->extendModeX;
    b->extend_y = ip->extendModeY;
    b->interpolation = ip->interpolationMode;
    *out = (ID2D1ImageBrush *)b;
    return S_OK;
}
