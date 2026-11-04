#include "d2d_private.h"
#include <dxbc/dxbc.h>
#include <dxbc/tokens.h>

typedef struct GpuShader {
    GUID id;
    int kind;
    BYTE *code;
    UINT32 size;
    ID3D11PixelShader *ps;
    ID3D11ComputeShader *cs;
    /* the vertex shader that feeds a pixel shader its inputs, one float4 per register. */
    ID3D11VertexShader *vs;
    ID3D11InputLayout *layout;
    UINT32 registers;
    /* what each register of the pixel shader's inputs holds. */
    UINT8 reg_kind[32];
    UINT8 reg_input[32];
} GpuShader;

enum { REG_ZERO, REG_POSITION, REG_SCENE, REG_INPUT };

struct D2dGpuTexture {
    UINT32 dims;
    UINT32 extents[3];
    DXGI_FORMAT format;
    UINT32 texel;
    D2D1_FILTER filter;
    D2D1_EXTEND_MODE extend[3];
    BYTE *data;
    BOOL dirty;
    ID3D11Resource *resource;
    ID3D11ShaderResourceView *view;
};

/*
 * one device for every factory, made on first use, under one lock. every pass uploads its inputs as textures, draws
 * or dispatches into a texture of the output rectangle and reads the pixels back.
 */
static CRITICAL_SECTION g_lock;
static LONG g_lock_ready;
static ID3D11Device *g_device;
static ID3D11DeviceContext *g_context;
static ID3D11RasterizerState *g_raster;
static HRESULT g_device_hr;
static BOOL g_device_tried;
static D2dArray g_shaders;

static void gpu_enter(void)
{
    if (InterlockedCompareExchange(&g_lock_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_lock_ready, 2);
    }
    while (g_lock_ready != 2)
        Sleep(0);
    EnterCriticalSection(&g_lock);
}

static void gpu_leave(void) { LeaveCriticalSection(&g_lock); }

static HRESULT gpu_device(void)
{
    if (g_device_tried)
        return g_device_hr;
    g_device_tried = TRUE;
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    PFN_D3D11_CREATE_DEVICE create = d3d11 ? (PFN_D3D11_CREATE_DEVICE)(void *)GetProcAddress(d3d11, "D3D11CreateDevice") : nullptr;
    if (!create) {
        g_device_hr = D2DERR_NO_HARDWARE_DEVICE;
        return g_device_hr;
    }
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level;
    g_device_hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &g_device, &level, &g_context);
    if (FAILED(g_device_hr))
        return g_device_hr;
    D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE};
    g_device_hr = g_device->lpVtbl->CreateRasterizerState(g_device, &rd, &g_raster);
    return g_device_hr;
}

static GpuShader *shader_find(REFGUID id)
{
    for (UINT32 i = 0; i < g_shaders.count; i++) {
        GpuShader *s = ((GpuShader **)g_shaders.data)[i];
        if (IsEqualGUID(&s->id, id))
            return s;
    }
    return nullptr;
}

HRESULT D2dGpuLoadShader(REFGUID id, int kind, const BYTE *code, UINT32 size)
{
    if (!code || !size || dxbc_check(code, size))
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    gpu_enter();
    /* a shader loaded once keeps its first code, as windows does. */
    if (!shader_find(id)) {
        GpuShader *s = D2dAllocZero(sizeof(*s));
        BYTE *copy = D2dAlloc(size);
        if (!s || !copy || !D2dArrayGrow(&g_shaders, sizeof(GpuShader *), 1)) {
            D2dFree(s);
            D2dFree(copy);
            hr = E_OUTOFMEMORY;
        } else {
            memcpy(copy, code, size);
            s->id = *id;
            s->kind = kind;
            s->code = copy;
            s->size = size;
            ((GpuShader **)g_shaders.data)[g_shaders.count++] = s;
        }
    }
    gpu_leave();
    return hr;
}

BOOL D2dGpuShaderLoaded(REFGUID id)
{
    gpu_enter();
    BOOL found = shader_find(id) != nullptr;
    gpu_leave();
    return found;
}

/* ------------------------------------------------------------------ the vertex shader of a pixel shader */

typedef struct Writer {
    BYTE *data;
    SIZE_T size, capacity;
    BOOL failed;
} Writer;

static void put(Writer *w, const void *bytes, SIZE_T n)
{
    if (w->failed)
        return;
    if (w->size + n > w->capacity) {
        SIZE_T cap = w->capacity ? w->capacity * 2 : 1024;
        while (cap < w->size + n)
            cap *= 2;
        BYTE *data = D2dRealloc(w->data, cap);
        if (!data) {
            w->failed = TRUE;
            return;
        }
        w->data = data;
        w->capacity = cap;
    }
    memcpy(w->data + w->size, bytes, n);
    w->size += n;
}
static void put32(Writer *w, UINT32 v) { put(w, &v, 4); }
static void set32(Writer *w, SIZE_T at, UINT32 v)
{
    if (!w->failed)
        memcpy(w->data + at, &v, 4);
}

typedef struct SigElem {
    const char *name;
    UINT32 index, system_value, reg;
} SigElem;

static void put_signature(Writer *w, const SigElem *e, UINT32 count)
{
    SIZE_T start = w->size;
    put32(w, count);
    put32(w, 8);
    UINT32 names = 8 + count * 24;
    for (UINT32 i = 0; i < count; i++) {
        UINT32 words[6] = {names, e[i].index, e[i].system_value, 3, e[i].reg, 0xf | 0xf << 8};
        put(w, words, sizeof(words));
        names += (UINT32)strlen(e[i].name) + 1;
    }
    for (UINT32 i = 0; i < count; i++)
        put(w, e[i].name, strlen(e[i].name) + 1);
    while ((w->size - start) & 3)
        put(w, "", 1);
}

#define VS_INPUT(mask) (2u | SELECT_MASK << 2 | (UINT32)(mask) << 4 | OPERAND_INPUT << 12 | 1u << 20)
#define VS_INPUT_XYZW (2u | SELECT_SWIZZLE << 2 | 0xE4u << 4 | OPERAND_INPUT << 12 | 1u << 20)
#define VS_OUTPUT(mask) (2u | SELECT_MASK << 2 | (UINT32)(mask) << 4 | OPERAND_OUTPUT << 12 | 1u << 20)

/* vs_4_0 that copies input register r to output register r, its outputs named as the pixel shader's inputs. */
static BYTE *build_vs(const SigElem *outs, UINT32 count, UINT32 registers, SIZE_T *length)
{
    static const UINT32 TAGS[3] = {DXBC_FOURCC('I', 'S', 'G', 'N'), DXBC_FOURCC('O', 'S', 'G', 'N'),
                                   DXBC_FOURCC('S', 'H', 'D', 'R')};
    SigElem ins[32];
    for (UINT32 r = 0; r < registers; r++)
        ins[r] = (SigElem){"A", r, 0, r};
    Writer w = {};
    put(&w, "DXBC", 4);
    for (int i = 0; i < 4; i++)
        put32(&w, 0);
    put32(&w, 1);
    put32(&w, 0);
    put32(&w, 3);
    SIZE_T offsets = w.size;
    for (int i = 0; i < 3; i++)
        put32(&w, 0);
    for (int i = 0; i < 3; i++) {
        set32(&w, offsets + i * 4, (UINT32)w.size);
        put32(&w, TAGS[i]);
        SIZE_T size_at = w.size;
        put32(&w, 0);
        if (i == 0) {
            put_signature(&w, ins, registers);
        } else if (i == 1) {
            put_signature(&w, outs, count);
        } else {
            SIZE_T start = w.size;
            put32(&w, (UINT32)DXBC_VERTEX << 16 | 4 << 4);
            put32(&w, 0);
            for (UINT32 r = 0; r < registers; r++) {
                put32(&w, OP_DCL_INPUT | 3 << 24);
                put32(&w, VS_INPUT(0xf));
                put32(&w, r);
            }
            for (UINT32 r = 0; r < registers; r++) {
                UINT32 sv = 0;
                for (UINT32 k = 0; k < count; k++) {
                    if (outs[k].reg == r)
                        sv = outs[k].system_value;
                }
                put32(&w, (sv ? OP_DCL_OUTPUT_SIV : OP_DCL_OUTPUT) | (sv ? 4u : 3u) << 24);
                put32(&w, VS_OUTPUT(0xf));
                put32(&w, r);
                if (sv)
                    put32(&w, sv);
            }
            for (UINT32 r = 0; r < registers; r++) {
                put32(&w, OP_MOV | 5 << 24);
                put32(&w, VS_OUTPUT(0xf));
                put32(&w, r);
                put32(&w, VS_INPUT_XYZW);
                put32(&w, r);
            }
            put32(&w, OP_RET | 1 << 24);
            set32(&w, start + 4, (UINT32)(w.size - start) / 4);
        }
        set32(&w, size_at, (UINT32)(w.size - size_at - 4));
    }
    if (w.failed) {
        D2dFree(w.data);
        return nullptr;
    }
    set32(&w, 24, (UINT32)w.size);
    uint32_t sum[4];
    dxbc_checksum(w.data, w.size, sum);
    memcpy(w.data + 4, sum, 16);
    *length = w.size;
    return w.data;
}

static BOOL name_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y)
            return FALSE;
    }
    return *a == *b;
}

static HRESULT shader_prepare_ps(GpuShader *s)
{
    if (s->ps)
        return S_OK;
    HRESULT hr = g_device->lpVtbl->CreatePixelShader(g_device, s->code, s->size, nullptr, &s->ps);
    if (FAILED(hr))
        return hr;
    DxbcSignature sig;
    if (dxbc_parse_io(s->code, s->size, 0, &sig, &d2d_allocator))
        return E_INVALIDARG;
    SigElem outs[32];
    UINT32 count = 0, registers = 0;
    for (UINT32 i = 0; i < sig.count && count < 32; i++) {
        const DxbcSignatureElement *e = &sig.elements[i];
        if (e->reg >= 32)
            continue;
        UINT8 kind = REG_ZERO, input = 0;
        if (e->system_value == 1 || name_is(e->name, "SV_POSITION"))
            kind = REG_POSITION;
        else if (name_is(e->name, "SCENE_POSITION"))
            kind = REG_SCENE;
        else if (name_is(e->name, "TEXCOORD"))
            kind = REG_INPUT, input = (UINT8)e->semantic_index;
        else if (e->system_value)
            continue;
        s->reg_kind[e->reg] = kind;
        s->reg_input[e->reg] = input;
        outs[count++] = (SigElem){e->name, e->semantic_index, kind == REG_POSITION ? 1u : 0u, e->reg};
        if (e->reg + 1 > registers)
            registers = e->reg + 1;
    }
    /* position must come out of the vertex shader even when the pixel shader does not read it. */
    BOOL has_position = FALSE;
    for (UINT32 i = 0; i < count; i++)
        has_position |= outs[i].system_value == 1;
    if (!has_position && count < 32 && registers < 32) {
        outs[count++] = (SigElem){"SV_POSITION", 0, 1, registers};
        s->reg_kind[registers++] = REG_POSITION;
    }
    SIZE_T length;
    BYTE *vs = build_vs(outs, count, registers, &length);
    mime_free(&d2d_allocator, sig.elements);
    if (!vs)
        return E_OUTOFMEMORY;
    hr = g_device->lpVtbl->CreateVertexShader(g_device, vs, length, nullptr, &s->vs);
    D3D11_INPUT_ELEMENT_DESC layout[32];
    for (UINT32 r = 0; r < registers; r++)
        layout[r] = (D3D11_INPUT_ELEMENT_DESC){"A", r, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, r * 16, D3D11_INPUT_PER_VERTEX_DATA, 0};
    if (SUCCEEDED(hr))
        hr = g_device->lpVtbl->CreateInputLayout(g_device, layout, registers, vs, length, &s->layout);
    D2dFree(vs);
    s->registers = registers;
    return hr;
}

/* ------------------------------------------------------------------ resource textures */

static DXGI_FORMAT texture_format(D2D1_BUFFER_PRECISION p, D2D1_CHANNEL_DEPTH depth, UINT32 *texel)
{
    BOOL one = depth == D2D1_CHANNEL_DEPTH_1;
    switch (p) {
    case D2D1_BUFFER_PRECISION_16BPC_UNORM:
        *texel = one ? 2 : 8;
        return one ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
    case D2D1_BUFFER_PRECISION_16BPC_FLOAT:
        *texel = one ? 2 : 8;
        return one ? DXGI_FORMAT_R16_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
    case D2D1_BUFFER_PRECISION_32BPC_FLOAT:
        *texel = one ? 4 : 16;
        return one ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
    case D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB:
        *texel = one ? 1 : 4;
        return one ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    default:
        *texel = one ? 1 : 4;
        return one ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

static UINT32 texture_bytes(const D2dGpuTexture *t)
{
    UINT32 n = t->texel;
    for (UINT32 d = 0; d < t->dims; d++)
        n *= t->extents[d];
    return n;
}

/* copies a box of texels from rows given by strides into the dense copy of the texture. */
static void texture_copy(D2dGpuTexture *t, const UINT32 *lo, const UINT32 *hi, const UINT32 *strides, const BYTE *data, UINT32 count)
{
    UINT32 w = hi[0] - lo[0], h = t->dims > 1 ? hi[1] - lo[1] : 1, d = t->dims > 2 ? hi[2] - lo[2] : 1;
    UINT32 y0 = t->dims > 1 ? lo[1] : 0, z0 = t->dims > 2 ? lo[2] : 0;
    UINT32 row = w * t->texel;
    UINT32 sy = t->dims > 1 && strides ? strides[0] : row, sz = t->dims > 2 && strides ? strides[1] : sy * h;
    UINT32 dy = t->extents[0] * t->texel, dz = dy * (t->dims > 1 ? t->extents[1] : 1);
    for (UINT32 z = 0; z < d; z++) {
        for (UINT32 y = 0; y < h; y++) {
            SIZE_T from = (SIZE_T)z * sz + (SIZE_T)y * sy;
            if (from + row > count)
                return;
            memcpy(t->data + (SIZE_T)(z + z0) * dz + (SIZE_T)(y + y0) * dy + (SIZE_T)lo[0] * t->texel, data + from, row);
        }
    }
}

HRESULT D2dGpuTextureCreate(const D2D1_RESOURCE_TEXTURE_PROPERTIES *p, const BYTE *data, const UINT32 *strides, UINT32 size,
                            D2dGpuTexture **out)
{
    if (!p || !p->extents || p->dimensions < 1 || p->dimensions > 3)
        return E_INVALIDARG;
    D2dGpuTexture *t = D2dAllocZero(sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->dims = p->dimensions;
    for (UINT32 d = 0; d < 3; d++) {
        t->extents[d] = d < t->dims ? p->extents[d] : 1;
        t->extend[d] = p->extendModes && d < t->dims ? p->extendModes[d] : D2D1_EXTEND_MODE_CLAMP;
        if (!t->extents[d]) {
            D2dFree(t);
            return E_INVALIDARG;
        }
    }
    t->format = texture_format(p->bufferPrecision, p->channelDepth, &t->texel);
    t->filter = p->filter;
    t->data = D2dAllocZero(texture_bytes(t));
    if (!t->data) {
        D2dFree(t);
        return E_OUTOFMEMORY;
    }
    if (data) {
        UINT32 lo[3] = {}, hi[3] = {t->extents[0], t->extents[1], t->extents[2]};
        texture_copy(t, lo, hi, strides, data, size);
    }
    t->dirty = TRUE;
    *out = t;
    return S_OK;
}

HRESULT D2dGpuTextureUpdate(D2dGpuTexture *t, const UINT32 *lo, const UINT32 *hi, const UINT32 *strides, UINT32 dims, const BYTE *data,
                            UINT32 count)
{
    if (!lo || !hi || !data || dims != t->dims)
        return E_INVALIDARG;
    for (UINT32 d = 0; d < dims; d++) {
        if (lo[d] >= hi[d] || hi[d] > t->extents[d])
            return E_INVALIDARG;
    }
    gpu_enter();
    texture_copy(t, lo, hi, strides, data, count);
    t->dirty = TRUE;
    gpu_leave();
    return S_OK;
}

void D2dGpuTextureFree(D2dGpuTexture *t)
{
    if (!t)
        return;
    gpu_enter();
    D2D_RELEASE(t->view);
    D2D_RELEASE(t->resource);
    gpu_leave();
    D2dFree(t->data);
    D2dFree(t);
}

static HRESULT texture_view(D2dGpuTexture *t)
{
    if (t->view && !t->dirty)
        return S_OK;
    D2D_RELEASE(t->view);
    D2D_RELEASE(t->resource);
    HRESULT hr;
    UINT32 pitch = t->extents[0] * t->texel;
    D3D11_SUBRESOURCE_DATA init = {t->data, pitch, pitch * t->extents[1]};
    if (t->dims == 1) {
        D3D11_TEXTURE1D_DESC d = {t->extents[0], 1, 1, t->format, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        hr = g_device->lpVtbl->CreateTexture1D(g_device, &d, &init, (ID3D11Texture1D **)&t->resource);
    } else if (t->dims == 2) {
        D3D11_TEXTURE2D_DESC d = {t->extents[0], t->extents[1], 1, 1, t->format, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        hr = g_device->lpVtbl->CreateTexture2D(g_device, &d, &init, (ID3D11Texture2D **)&t->resource);
    } else {
        D3D11_TEXTURE3D_DESC d = {t->extents[0], t->extents[1], t->extents[2], 1, t->format, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        hr = g_device->lpVtbl->CreateTexture3D(g_device, &d, &init, (ID3D11Texture3D **)&t->resource);
    }
    if (SUCCEEDED(hr))
        hr = g_device->lpVtbl->CreateShaderResourceView(g_device, t->resource, nullptr, &t->view);
    if (SUCCEEDED(hr))
        t->dirty = FALSE;
    return hr;
}

/* ------------------------------------------------------------------ passes */

static D3D11_TEXTURE_ADDRESS_MODE address_of(D2D1_EXTEND_MODE m)
{
    return m == D2D1_EXTEND_MODE_WRAP ? D3D11_TEXTURE_ADDRESS_WRAP : m == D2D1_EXTEND_MODE_MIRROR ? D3D11_TEXTURE_ADDRESS_MIRROR : D3D11_TEXTURE_ADDRESS_CLAMP;
}

/* inputs are transparent outside the pixels they were given. */
static HRESULT make_sampler(D2D1_FILTER filter, const D3D11_TEXTURE_ADDRESS_MODE *address, ID3D11SamplerState **out)
{
    D3D11_SAMPLER_DESC d = {};
    d.Filter = (D3D11_FILTER)filter;
    d.AddressU = address ? address[0] : D3D11_TEXTURE_ADDRESS_BORDER;
    d.AddressV = address ? address[1] : D3D11_TEXTURE_ADDRESS_BORDER;
    d.AddressW = address ? address[2] : D3D11_TEXTURE_ADDRESS_BORDER;
    d.MaxAnisotropy = 1;
    d.ComparisonFunc = D3D11_COMPARISON_NEVER;
    d.MaxLOD = 3.402823466e+38f;
    return g_device->lpVtbl->CreateSamplerState(g_device, &d, out);
}

typedef struct PassObjects {
    ID3D11Texture2D *tex[32];
    ID3D11ShaderResourceView *srv[32];
    ID3D11SamplerState *smp[32];
    UINT32 slots;
    ID3D11Buffer *cb[2];
    ID3D11Texture2D *target, *staging;
    ID3D11Buffer *vb;
    ID3D11RenderTargetView *rtv;
    ID3D11UnorderedAccessView *uav;
} PassObjects;

static void pass_release(PassObjects *o)
{
    for (UINT32 i = 0; i < 32; i++) {
        D2D_RELEASE(o->tex[i]);
        D2D_RELEASE(o->srv[i]);
        D2D_RELEASE(o->smp[i]);
    }
    D2D_RELEASE(o->cb[0]);
    D2D_RELEASE(o->cb[1]);
    D2D_RELEASE(o->target);
    D2D_RELEASE(o->staging);
    D2D_RELEASE(o->vb);
    D2D_RELEASE(o->rtv);
    D2D_RELEASE(o->uav);
}

static HRESULT upload_input(const D2dImageData *d, ID3D11Texture2D **tex, ID3D11ShaderResourceView **srv)
{
    static const uint32_t clear = 0;
    BOOL empty = !d || d->surface.w <= 0 || d->surface.h <= 0;
    UINT32 w = empty ? 1 : (UINT32)d->surface.w, h = empty ? 1 : (UINT32)d->surface.h;
    D3D11_TEXTURE2D_DESC desc = {w, h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA init = {empty ? (const void *)&clear : (const void *)d->surface.px, w * 4, w * h * 4};
    HRESULT hr = g_device->lpVtbl->CreateTexture2D(g_device, &desc, &init, tex);
    if (SUCCEEDED(hr))
        hr = g_device->lpVtbl->CreateShaderResourceView(g_device, (ID3D11Resource *)*tex, nullptr, srv);
    return hr;
}

static HRESULT make_cb(const BYTE *data, UINT32 size, ID3D11Buffer **out)
{
    UINT32 width = (size + 15) & ~15u;
    BYTE *copy = D2dAllocZero(width);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, data, size);
    D3D11_BUFFER_DESC d = {width, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA init = {copy, 0, 0};
    HRESULT hr = g_device->lpVtbl->CreateBuffer(g_device, &d, &init, out);
    D2dFree(copy);
    return hr;
}

static HRESULT bind_textures(const D2dGpuPass *p, PassObjects *o)
{
    HRESULT hr = S_OK;
    for (UINT32 i = 0; i < p->input_count && i < 32 && SUCCEEDED(hr); i++) {
        hr = upload_input(p->inputs[i], &o->tex[i], &o->srv[i]);
        if (SUCCEEDED(hr))
            hr = make_sampler((D2D1_FILTER)(p->filters ? p->filters[i] : D2D1_FILTER_MIN_MAG_MIP_LINEAR), nullptr, &o->smp[i]);
        if (i + 1 > o->slots)
            o->slots = i + 1;
    }
    for (UINT32 i = 0; i < p->res_count && SUCCEEDED(hr); i++) {
        UINT32 slot = p->res_slot[i];
        D2dGpuTexture *t = p->res[i];
        if (!t || slot >= 32)
            continue;
        hr = texture_view(t);
        if (FAILED(hr))
            break;
        D2D_RELEASE(o->srv[slot]);
        D2D_RELEASE(o->smp[slot]);
        o->srv[slot] = t->view;
        t->view->lpVtbl->AddRef(t->view);
        D3D11_TEXTURE_ADDRESS_MODE address[3] = {address_of(t->extend[0]), address_of(t->extend[1]), address_of(t->extend[2])};
        hr = make_sampler(t->filter, address, &o->smp[slot]);
        if (slot + 1 > o->slots)
            o->slots = slot + 1;
    }
    return hr;
}

/* reads the target back as premultiplied bgra, swapping red and blue when it holds rgba. */
static HRESULT read_back(PassObjects *o, UINT32 w, UINT32 h, DXGI_FORMAT format, D2dIRect r, D2dImageData *out)
{
    D3D11_TEXTURE2D_DESC sd = {w, h, 1, 1, format, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
    HRESULT hr = g_device->lpVtbl->CreateTexture2D(g_device, &sd, nullptr, &o->staging);
    if (FAILED(hr))
        return hr;
    g_context->lpVtbl->CopyResource(g_context, (ID3D11Resource *)o->staging, (ID3D11Resource *)o->target);
    D3D11_MAPPED_SUBRESOURCE map;
    hr = g_context->lpVtbl->Map(g_context, (ID3D11Resource *)o->staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr))
        return hr;
    out->x = r.x0;
    out->y = r.y0;
    if (!D2dSurfaceInit(&out->surface, (int)w, (int)h)) {
        g_context->lpVtbl->Unmap(g_context, (ID3D11Resource *)o->staging, 0);
        return E_OUTOFMEMORY;
    }
    for (UINT32 y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)((const BYTE *)map.pData + (SIZE_T)y * map.RowPitch);
        uint32_t *dst = D2dSurfaceRow(&out->surface, (int)y);
        if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
            memcpy(dst, src, (SIZE_T)w * 4);
        } else {
            for (UINT32 x = 0; x < w; x++) {
                uint32_t p = src[x];
                dst[x] = (p & 0xff00ff00u) | (p & 0xff) << 16 | (p >> 16 & 0xff);
            }
        }
    }
    g_context->lpVtbl->Unmap(g_context, (ID3D11Resource *)o->staging, 0);
    return S_OK;
}

static void unbind(void)
{
    ID3D11ShaderResourceView *none[32] = {};
    g_context->lpVtbl->PSSetShaderResources(g_context, 0, 32, none);
    g_context->lpVtbl->CSSetShaderResources(g_context, 0, 32, none);
    ID3D11UnorderedAccessView *nouav = nullptr;
    g_context->lpVtbl->CSSetUnorderedAccessViews(g_context, 0, 1, &nouav, nullptr);
    g_context->lpVtbl->OMSetRenderTargets(g_context, 0, nullptr, nullptr);
}

HRESULT D2dGpuDraw(const D2dGpuPass *p, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    D2dIRect r = p->out;
    UINT32 w = (UINT32)(r.x1 - r.x0), h = (UINT32)(r.y1 - r.y0);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) {
        out->x = r.x0;
        out->y = r.y0;
        return S_OK;
    }
    if (w > 16384 || h > 16384)
        return D2DERR_INTERMEDIATE_TOO_LARGE;
    gpu_enter();
    PassObjects o = {};
    HRESULT hr = gpu_device();
    GpuShader *s = SUCCEEDED(hr) ? shader_find(p->shader) : nullptr;
    if (SUCCEEDED(hr) && !s)
        hr = D2DERR_INVALID_GRAPH_CONFIGURATION;
    if (SUCCEEDED(hr))
        hr = shader_prepare_ps(s);
    if (SUCCEEDED(hr))
        hr = bind_textures(p, &o);
    if (SUCCEEDED(hr) && p->cb_size)
        hr = make_cb(p->cb, p->cb_size, &o.cb[0]);
    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC d = {w, h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
        hr = g_device->lpVtbl->CreateTexture2D(g_device, &d, nullptr, &o.target);
    }
    if (SUCCEEDED(hr))
        hr = g_device->lpVtbl->CreateRenderTargetView(g_device, (ID3D11Resource *)o.target, nullptr, &o.rtv);
    if (SUCCEEDED(hr)) {
        /* the four corners of the output rectangle as a strip, each register of the pixel shader's inputs given. */
        float *v = D2dAllocZero((SIZE_T)4 * s->registers * 16);
        if (!v) {
            hr = E_OUTOFMEMORY;
        } else {
            static const float cx[4] = {0, 1, 0, 1}, cy[4] = {0, 0, 1, 1};
            for (int k = 0; k < 4; k++) {
                float px = cx[k] * (float)w, py = cy[k] * (float)h, sx = (float)r.x0 + px, sy = (float)r.y0 + py;
                for (UINT32 reg = 0; reg < s->registers; reg++) {
                    float *e = v + ((SIZE_T)k * s->registers + reg) * 4;
                    switch (s->reg_kind[reg]) {
                    case REG_POSITION:
                        e[0] = cx[k] * 2.0f - 1.0f;
                        e[1] = 1.0f - cy[k] * 2.0f;
                        e[3] = 1.0f;
                        break;
                    case REG_SCENE:
                        e[0] = sx;
                        e[1] = sy;
                        e[3] = 1.0f;
                        break;
                    case REG_INPUT: {
                        UINT32 i = s->reg_input[reg];
                        const D2dImageData *in = i < p->input_count ? p->inputs[i] : nullptr;
                        float iw = in && in->surface.w > 0 ? (float)in->surface.w : 1.0f, ih = in && in->surface.h > 0 ? (float)in->surface.h : 1.0f;
                        float ix = in ? (float)in->x : 0.0f, iy = in ? (float)in->y : 0.0f;
                        e[0] = (sx - ix) / iw;
                        e[1] = (sy - iy) / ih;
                        e[2] = 1.0f / iw;
                        e[3] = 1.0f / ih;
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
            D3D11_BUFFER_DESC bd = {4 * s->registers * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
            D3D11_SUBRESOURCE_DATA init = {v, 0, 0};
            hr = g_device->lpVtbl->CreateBuffer(g_device, &bd, &init, &o.vb);
            D2dFree(v);
        }
    }
    if (SUCCEEDED(hr)) {
        ID3D11DeviceContext *c = g_context;
        static const float zero[4] = {};
        c->lpVtbl->ClearRenderTargetView(c, o.rtv, zero);
        UINT stride = s->registers * 16, offset = 0;
        c->lpVtbl->IASetInputLayout(c, s->layout);
        c->lpVtbl->IASetVertexBuffers(c, 0, 1, &o.vb, &stride, &offset);
        c->lpVtbl->IASetPrimitiveTopology(c, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        c->lpVtbl->VSSetShader(c, s->vs, nullptr, 0);
        c->lpVtbl->GSSetShader(c, nullptr, nullptr, 0);
        c->lpVtbl->PSSetShader(c, s->ps, nullptr, 0);
        c->lpVtbl->PSSetConstantBuffers(c, 0, 1, &o.cb[0]);
        if (o.slots) {
            c->lpVtbl->PSSetShaderResources(c, 0, o.slots, o.srv);
            c->lpVtbl->PSSetSamplers(c, 0, o.slots, o.smp);
        }
        c->lpVtbl->RSSetState(c, g_raster);
        D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
        c->lpVtbl->RSSetViewports(c, 1, &vp);
        c->lpVtbl->OMSetBlendState(c, nullptr, zero, 0xffffffff);
        c->lpVtbl->OMSetDepthStencilState(c, nullptr, 0);
        c->lpVtbl->OMSetRenderTargets(c, 1, &o.rtv, nullptr);
        c->lpVtbl->Draw(c, 4, 0);
        unbind();
        hr = read_back(&o, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, r, out);
    }
    pass_release(&o);
    gpu_leave();
    return hr;
}

HRESULT D2dGpuCompute(const D2dGpuPass *p, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    D2dIRect r = p->out;
    UINT32 w = (UINT32)(r.x1 - r.x0), h = (UINT32)(r.y1 - r.y0);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) {
        out->x = r.x0;
        out->y = r.y0;
        return S_OK;
    }
    if (w > 16384 || h > 16384)
        return D2DERR_INTERMEDIATE_TOO_LARGE;
    gpu_enter();
    PassObjects o = {};
    HRESULT hr = gpu_device();
    GpuShader *s = SUCCEEDED(hr) ? shader_find(p->shader) : nullptr;
    if (SUCCEEDED(hr) && !s)
        hr = D2DERR_INVALID_GRAPH_CONFIGURATION;
    if (SUCCEEDED(hr) && !s->cs)
        hr = g_device->lpVtbl->CreateComputeShader(g_device, s->code, s->size, nullptr, &s->cs);
    if (SUCCEEDED(hr))
        hr = bind_textures(p, &o);
    if (SUCCEEDED(hr)) {
        /*
         * the system constants: the rectangle to fill in the output texture, then for each input how an output
         * texel maps to its texture coordinates, as scale and offset in x and then in y.
         */
        UINT32 size = 16 + p->input_count * 16;
        BYTE *sys = D2dAllocZero(size);
        if (!sys) {
            hr = E_OUTOFMEMORY;
        } else {
            INT32 rect[4] = {0, 0, (INT32)w, (INT32)h};
            memcpy(sys, rect, 16);
            for (UINT32 i = 0; i < p->input_count; i++) {
                const D2dImageData *in = p->inputs[i];
                float iw = in && in->surface.w > 0 ? (float)in->surface.w : 1.0f, ih = in && in->surface.h > 0 ? (float)in->surface.h : 1.0f;
                float ix = in ? (float)in->x : 0.0f, iy = in ? (float)in->y : 0.0f;
                float k[4] = {1.0f / iw, ((float)r.x0 - ix) / iw, 1.0f / ih, ((float)r.y0 - iy) / ih};
                memcpy(sys + 16 + i * 16, k, 16);
            }
            hr = make_cb(sys, size, &o.cb[0]);
            D2dFree(sys);
        }
    }
    if (SUCCEEDED(hr) && p->cb_size)
        hr = make_cb(p->cb, p->cb_size, &o.cb[1]);
    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC d = {w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0};
        hr = g_device->lpVtbl->CreateTexture2D(g_device, &d, nullptr, &o.target);
    }
    if (SUCCEEDED(hr))
        hr = g_device->lpVtbl->CreateUnorderedAccessView(g_device, (ID3D11Resource *)o.target, nullptr, &o.uav);
    if (SUCCEEDED(hr)) {
        ID3D11DeviceContext *c = g_context;
        static const UINT zero[4] = {};
        c->lpVtbl->ClearUnorderedAccessViewUint(c, o.uav, zero);
        c->lpVtbl->CSSetShader(c, s->cs, nullptr, 0);
        c->lpVtbl->CSSetConstantBuffers(c, 0, 2, o.cb);
        if (o.slots) {
            c->lpVtbl->CSSetShaderResources(c, 0, o.slots, o.srv);
            c->lpVtbl->CSSetSamplers(c, 0, o.slots, o.smp);
        }
        c->lpVtbl->CSSetUnorderedAccessViews(c, 0, 1, &o.uav, nullptr);
        c->lpVtbl->Dispatch(c, p->groups[0], p->groups[1], p->groups[2]);
        unbind();
        hr = read_back(&o, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, r, out);
    }
    pass_release(&o);
    gpu_leave();
    return hr;
}
