#include "effects.h"
#include "d2d1builtin_dxbc.h"

static const GUID g_ps_matrix = {0x5c2f0a11, 0x7d3e, 0x4b8a, {0x9e, 0x41, 0x6d, 0x32, 0x0b, 0x1f, 0x80, 0x01}};
static const GUID g_ps_blur = {0x5c2f0a11, 0x7d3e, 0x4b8a, {0x9e, 0x41, 0x6d, 0x32, 0x0b, 0x1f, 0x80, 0x02}};
static const GUID g_ps_composite = {0x5c2f0a11, 0x7d3e, 0x4b8a, {0x9e, 0x41, 0x6d, 0x32, 0x0b, 0x1f, 0x80, 0x03}};

static LONG g_loaded;

static BOOL shaders(void)
{
    if (g_loaded)
        return g_loaded > 0;
    BOOL ok = SUCCEEDED(D2dGpuLoadShader(&g_ps_matrix, D2D_SHADER_PIXEL, ps_d2d_colormatrix, sizeof(ps_d2d_colormatrix))) &&
              SUCCEEDED(D2dGpuLoadShader(&g_ps_blur, D2D_SHADER_PIXEL, ps_d2d_blur, sizeof(ps_d2d_blur))) &&
              SUCCEEDED(D2dGpuLoadShader(&g_ps_composite, D2D_SHADER_PIXEL, ps_d2d_composite, sizeof(ps_d2d_composite)));
    InterlockedExchange(&g_loaded, ok ? 1 : -1);
    return ok;
}

/*
 * a pass costs an upload and a read back, so the gpu takes an effect when its work is large: MIME_D2D_GPU set to 0
 * keeps everything on the cpu, set to force sends every effect it can to the gpu.
 */
static BOOL wanted(UINT64 work)
{
    char v[8];
    DWORD n = GetEnvironmentVariableA("MIME_D2D_GPU", v, sizeof(v));
    if (n == 1 && v[0] == '0')
        return FALSE;
    if (n == 5 && !memcmp(v, "force", 5))
        return TRUE;
    return work >= (UINT64)1 << 24;
}

static UINT64 pixels(FxRect r) { return (UINT64)(r.x1 - r.x0) * (UINT64)(r.y1 - r.y0); }

/* an input as 8 bit pixels, S_FALSE when it carries floats the gpu would lose. */
static HRESULT input8(FxCtx *c, UINT32 i, FxRect want, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    D2dEffect *e = c->e;
    if (i >= e->input_count || !e->inputs[i]) {
        out->x = want.x0;
        out->y = want.y0;
        return S_OK;
    }
    HRESULT hr = D2dImageGet(c->rt, e->inputs[i], want, out, c->depth + 1);
    if (SUCCEEDED(hr) && out->fpx) {
        D2dImageDataFree(out);
        return S_FALSE;
    }
    return hr;
}

static HRESULT run(const GUID *shader, const void *cb, UINT32 size, const D2dImageData *const *in, UINT32 count, FxRect out_r, D2dImageData *out)
{
    static const UINT32 point[2] = {0, 0};
    D2dGpuPass p = {};
    p.shader = shader;
    p.cb = cb;
    p.cb_size = size;
    p.inputs = in;
    p.filters = point;
    p.input_count = count;
    p.out = out_r;
    return D2dGpuDraw(&p, out);
}

static HRESULT matrix_pass(const FxMatrix *k, const D2dImageData *in, FxRect area, D2dImageData *out)
{
    float cb[24];
    memcpy(cb, k->m, 80);
    cb[20] = k->straight ? 1.0f : 0.0f;
    cb[21] = k->clamp ? 1.0f : 0.0f;
    cb[22] = cb[23] = 0.0f;
    const D2dImageData *ins[1] = {in};
    return run(&g_ps_matrix, cb, sizeof(cb), ins, 1, area, out);
}

static HRESULT blur_pass(const float *k, int r, BOOL vertical, BOOL hard, const D2dImageData *in, FxRect area, D2dImageData *out)
{
    float cb[128 * 4] = {};
    int taps = 2 * r + 1, pairs = (taps + 1) / 2;
    cb[0] = (float)pairs;
    cb[1] = vertical ? 0.0f : 1.0f;
    cb[2] = vertical ? 1.0f : 0.0f;
    cb[3] = hard ? 1.0f : 0.0f;
    for (int t = 0; t < taps; t++) {
        int o = t - r;
        cb[4 + t * 2] = (float)o;
        cb[4 + t * 2 + 1] = k[o < 0 ? -o : o];
    }
    const D2dImageData *ins[1] = {in};
    return run(&g_ps_blur, cb, (UINT32)(4 + pairs * 4) * 4, ins, 1, area, out);
}

/* the factors of the porter duff modes as a0 + a1 da for the source and b0 + b1 sa for the destination. */
static BOOL composite_factors(UINT32 mode, float *f)
{
    static const float table[12][4] = {
        {1, 0, 1, -1}, {1, -1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {1, -1, 0, 0}, {0, 0, 1, -1},
        {0, 1, 1, -1}, {1, -1, 0, 1}, {1, -1, 1, -1}, {1, 0, 1, 0}, {1, 0, 0, 0}, {1, 0, 0, 0},
    };
    if (mode >= 12)
        return FALSE;
    memcpy(f, table[mode], 16);
    return TRUE;
}

HRESULT FxGpuRender(FxCtx *c, FxRect area, D2dImageData *out)
{
    D2dEffect *e = c->e;
    FxMatrix k;
    HRESULT hr;
    if (FxColorMatrix(e, &k)) {
        if (!wanted(pixels(area) * 4) || !shaders())
            return S_FALSE;
        D2dImageData in;
        hr = input8(c, 0, area, &in);
        if (hr != S_OK)
            return hr;
        hr = matrix_pass(&k, &in, area, out);
        D2dImageDataFree(&in);
        return SUCCEEDED(hr) ? S_OK : S_FALSE;
    }
    switch (e->id) {
    case EFF_COMPOSITE:
    case EFF_ARITHMETIC:
    case EFF_CROSSFADE:
    case EFF_ALPHAMASK: {
        float cb[8] = {};
        if (e->input_count != 2)
            return S_FALSE;
        if (e->id == EFF_COMPOSITE && !composite_factors(*D2dFxU(e, 0), cb))
            return S_FALSE;
        if (e->id == EFF_ALPHAMASK)
            composite_factors(D2D1_COMPOSITE_MODE_DESTINATION_IN, cb);
        if (e->id == EFF_ARITHMETIC) {
            const float *a = D2dFxF(e, 0);
            cb[0] = a[1];
            cb[2] = a[2];
            cb[4] = a[0];
            cb[5] = a[3];
            cb[6] = *D2dFxU(e, 1) ? 1.0f : 0.0f;
        }
        if (e->id == EFF_CROSSFADE) {
            float w = D2dFxF(e, 0)[0];
            cb[0] = w;
            cb[2] = 1.0f - w;
        }
        if (!wanted(pixels(area) * 4) || !shaders())
            return S_FALSE;
        D2dImageData a, b;
        hr = input8(c, 0, area, &a);
        if (hr != S_OK)
            return hr;
        hr = input8(c, 1, area, &b);
        if (hr != S_OK) {
            D2dImageDataFree(&a);
            return hr;
        }
        const D2dImageData *ins[2] = {&a, &b};
        hr = run(&g_ps_composite, cb, sizeof(cb), ins, 2, area, out);
        D2dImageDataFree(&a);
        D2dImageDataFree(&b);
        return SUCCEEDED(hr) ? S_OK : S_FALSE;
    }
    case EFF_BLUR:
    case EFF_SHADOW: {
        float *kern;
        int r;
        BOOL hard;
        if (!FxBlurKernel(c, &kern, &r, &hard))
            return S_FALSE;
        if (r > 126 || !wanted(pixels(area) * (UINT64)(4 * r + 2)) || !shaders()) {
            D2dFree(kern);
            return S_FALSE;
        }
        FxRect in_b;
        hr = FxInputBounds(c, 0, &in_b);
        FxRect want = fx_inflate(area, r + 1, r + 1), rows = fx_inflate(area, 0, r + 1);
        if (hard) {
            want = fx_intersect(want, in_b);
            rows = fx_intersect(rows, in_b);
        }
        D2dImageData in = {}, mid = {}, colored = {};
        if (SUCCEEDED(hr))
            hr = input8(c, 0, want, &in);
        if (hr == S_OK && e->id == EFF_SHADOW) {
            const float *col = D2dFxF(e, 1);
            FxMatrix m = {};
            m.straight = TRUE;
            m.m[3][0] = col[0] * col[3];
            m.m[3][1] = col[1] * col[3];
            m.m[3][2] = col[2] * col[3];
            m.m[3][3] = col[3];
            hr = matrix_pass(&m, &in, (FxRect){in.x, in.y, in.x + in.surface.w, in.y + in.surface.h}, &colored);
            D2dImageDataFree(&in);
            in = colored;
        }
        if (hr == S_OK)
            hr = blur_pass(kern, r, FALSE, hard, &in, rows, &mid);
        if (hr == S_OK)
            hr = blur_pass(kern, r, TRUE, hard, &mid, area, out);
        D2dImageDataFree(&mid);
        D2dImageDataFree(&in);
        D2dFree(kern);
        return hr == S_OK ? S_OK : S_FALSE;
    }
    default:
        return S_FALSE;
    }
}
