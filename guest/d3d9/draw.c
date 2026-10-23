/*
 * draws translate direct3d 9 state to d3d11 when needed. state objects
 * are cached by description, constants go to dynamic buffers with each
 * shader's defs over the device's, and shader translations are looked up
 * by the state they depend on.
 */
#include "d3d9_private.h"

static float fbits(DWORD v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* ------------------------------------------------------------------ state objects */

/* a small cache of d3d11 state objects keyed by a few direct3d 9 state words. the oldest goes when full. */
static BOOL same_key(const DWORD *a, const DWORD *b, UINT words)
{
    for (UINT i = 0; i < words; i++)
        if (a[i] != b[i])
            return FALSE;
    return TRUE;
}

static void *cached(D9StateCache *cache, const DWORD *key, UINT words, void *(*make)(D9Device *, const DWORD *),
                    D9Device *device)
{
    UINT hash = 2166136261u;
    for (UINT i = 0; i < words; i++)
        hash = (hash ^ key[i]) * 16777619u;
    UINT size = words * sizeof(DWORD);
    D9StateEntry *last = &cache->entries[cache->last];
    if (cache->count && last->hash == hash && last->size == size && same_key(last->key, key, words))
        return last->object;
    for (UINT i = 0; i < cache->count; i++)
        if (cache->entries[i].hash == hash && cache->entries[i].size == size && same_key(cache->entries[i].key, key, words)) {
            cache->last = i;
            return cache->entries[i].object;
        }
    void *object = make(device, key);
    if (!object)
        return nullptr;
    UINT slot = cache->count < RTL_NUMBER_OF(cache->entries) ? cache->count++ : cache->next++ % RTL_NUMBER_OF(cache->entries);
    D9StateEntry *e = &cache->entries[slot];
    /* the context keeps its own reference to a state still bound. */
    if (e->object)
        ((IUnknown *)e->object)->lpVtbl->Release((IUnknown *)e->object);
    memcpy(e->key, key, size);
    e->size = size;
    e->hash = hash;
    e->object = object;
    cache->last = slot;
    return object;
}

void D9ClearStateCache(D9StateCache *cache)
{
    for (UINT i = 0; i < cache->count; i++)
        if (cache->entries[i].object)
            ((IUnknown *)cache->entries[i].object)->lpVtbl->Release((IUnknown *)cache->entries[i].object);
    memset(cache, 0, sizeof(*cache));
}

UINT D9RenderStateGroups(D3DRENDERSTATETYPE state)
{
    switch (state) {
    case D3DRS_ALPHABLENDENABLE:
    case D3DRS_SRCBLEND:
    case D3DRS_DESTBLEND:
    case D3DRS_BLENDOP:
    case D3DRS_SEPARATEALPHABLENDENABLE:
    case D3DRS_SRCBLENDALPHA:
    case D3DRS_DESTBLENDALPHA:
    case D3DRS_BLENDOPALPHA:
    case D3DRS_COLORWRITEENABLE:
    case D3DRS_COLORWRITEENABLE1:
    case D3DRS_COLORWRITEENABLE2:
    case D3DRS_COLORWRITEENABLE3:
    case D3DRS_BLENDFACTOR:
    case D3DRS_MULTISAMPLEMASK:
        return D9_STATES_BLEND;
    case D3DRS_ZENABLE:
    case D3DRS_ZWRITEENABLE:
    case D3DRS_ZFUNC:
    case D3DRS_STENCILENABLE:
    case D3DRS_STENCILMASK:
    case D3DRS_STENCILWRITEMASK:
    case D3DRS_STENCILFAIL:
    case D3DRS_STENCILZFAIL:
    case D3DRS_STENCILPASS:
    case D3DRS_STENCILFUNC:
    case D3DRS_STENCILREF:
    case D3DRS_TWOSIDEDSTENCILMODE:
    case D3DRS_CCW_STENCILFAIL:
    case D3DRS_CCW_STENCILZFAIL:
    case D3DRS_CCW_STENCILPASS:
    case D3DRS_CCW_STENCILFUNC:
        return D9_STATES_DEPTH;
    case D3DRS_FILLMODE:
    case D3DRS_CULLMODE:
    case D3DRS_DEPTHBIAS:
    case D3DRS_SLOPESCALEDEPTHBIAS:
    case D3DRS_SCISSORTESTENABLE:
    case D3DRS_MULTISAMPLEANTIALIAS:
    case D3DRS_ANTIALIASEDLINEENABLE:
        return D9_STATES_RASTER;
    case D3DRS_POINTSIZE:
    case D3DRS_POINTSIZE_MIN:
    case D3DRS_POINTSIZE_MAX:
    case D3DRS_ALPHAREF:
    case D3DRS_FOGCOLOR:
    case D3DRS_FOGSTART:
    case D3DRS_FOGEND:
    case D3DRS_FOGDENSITY:
        return D9_STATES_INTERNAL;
    default:
        return 0;
    }
}

/* d3d11 takes only alpha factors for alpha, so color factors become their alpha form. */
static D3D11_BLEND blend_factor(DWORD b, BOOL alpha)
{
    static const D3D11_BLEND COLOR[] = {D3D11_BLEND_ZERO,           D3D11_BLEND_ZERO,           D3D11_BLEND_ONE,
                                        D3D11_BLEND_SRC_COLOR,      D3D11_BLEND_INV_SRC_COLOR,  D3D11_BLEND_SRC_ALPHA,
                                        D3D11_BLEND_INV_SRC_ALPHA,  D3D11_BLEND_DEST_ALPHA,     D3D11_BLEND_INV_DEST_ALPHA,
                                        D3D11_BLEND_DEST_COLOR,     D3D11_BLEND_INV_DEST_COLOR, D3D11_BLEND_SRC_ALPHA_SAT,
                                        D3D11_BLEND_SRC_ALPHA,      D3D11_BLEND_INV_SRC_ALPHA,  D3D11_BLEND_BLEND_FACTOR,
                                        D3D11_BLEND_INV_BLEND_FACTOR, D3D11_BLEND_SRC1_COLOR,   D3D11_BLEND_INV_SRC1_COLOR};
    D3D11_BLEND f = b < RTL_NUMBER_OF(COLOR) ? COLOR[b] : D3D11_BLEND_ONE;
    if (alpha) {
        switch (f) {
        case D3D11_BLEND_SRC_COLOR:
            return D3D11_BLEND_SRC_ALPHA;
        case D3D11_BLEND_INV_SRC_COLOR:
            return D3D11_BLEND_INV_SRC_ALPHA;
        case D3D11_BLEND_DEST_COLOR:
            return D3D11_BLEND_DEST_ALPHA;
        case D3D11_BLEND_INV_DEST_COLOR:
            return D3D11_BLEND_INV_DEST_ALPHA;
        case D3D11_BLEND_SRC1_COLOR:
            return D3D11_BLEND_SRC1_ALPHA;
        case D3D11_BLEND_INV_SRC1_COLOR:
            return D3D11_BLEND_INV_SRC1_ALPHA;
        default:
            break;
        }
    }
    return f;
}

/* a source factor of BOTHSRCALPHA or BOTHINVSRCALPHA sets the destination's too. */
static void blend_pair(DWORD src, DWORD dst, BOOL alpha, D3D11_BLEND *s, D3D11_BLEND *d)
{
    if (src == D3DBLEND_BOTHSRCALPHA) {
        *s = D3D11_BLEND_SRC_ALPHA, *d = D3D11_BLEND_INV_SRC_ALPHA;
    } else if (src == D3DBLEND_BOTHINVSRCALPHA) {
        *s = D3D11_BLEND_INV_SRC_ALPHA, *d = D3D11_BLEND_SRC_ALPHA;
    } else {
        *s = blend_factor(src, alpha);
        *d = blend_factor(dst, alpha);
    }
}

static D3D11_BLEND_OP blend_op(DWORD op) { return op >= 1 && op <= 5 ? (D3D11_BLEND_OP)op : D3D11_BLEND_OP_ADD; }

enum { BK_ENABLE, BK_SRC, BK_DEST, BK_OP, BK_SEPARATE, BK_SRC_ALPHA, BK_DEST_ALPHA, BK_OP_ALPHA, BK_MASK, BK_WORDS = BK_MASK + 4 };

static void *make_blend(D9Device *d, const DWORD *k)
{
    D3D11_BLEND_DESC desc = {FALSE, TRUE};
    D3D11_RENDER_TARGET_BLEND_DESC t = {!!k[BK_ENABLE]};
    blend_pair(k[BK_SRC], k[BK_DEST], FALSE, &t.SrcBlend, &t.DestBlend);
    t.BlendOp = blend_op(k[BK_OP]);
    if (k[BK_SEPARATE]) {
        blend_pair(k[BK_SRC_ALPHA], k[BK_DEST_ALPHA], TRUE, &t.SrcBlendAlpha, &t.DestBlendAlpha);
        t.BlendOpAlpha = blend_op(k[BK_OP_ALPHA]);
    } else {
        blend_pair(k[BK_SRC], k[BK_DEST], TRUE, &t.SrcBlendAlpha, &t.DestBlendAlpha);
        t.BlendOpAlpha = t.BlendOp;
    }
    for (UINT i = 0; i < 4; i++) {
        desc.RenderTarget[i] = t;
        desc.RenderTarget[i].RenderTargetWriteMask = (UINT8)(k[BK_MASK + i] & 0xF);
    }
    ID3D11BlendState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateBlendState(d->d3d11, &desc, &s)) ? s : nullptr;
}

static ID3D11BlendState *blend_state(D9Device *d)
{
    const DWORD *rs = d->render_states;
    const DWORD k[BK_WORDS] = {rs[D3DRS_ALPHABLENDENABLE], rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND],
                               rs[D3DRS_BLENDOP], rs[D3DRS_SEPARATEALPHABLENDENABLE], rs[D3DRS_SRCBLENDALPHA],
                               rs[D3DRS_DESTBLENDALPHA], rs[D3DRS_BLENDOPALPHA], rs[D3DRS_COLORWRITEENABLE],
                               rs[D3DRS_COLORWRITEENABLE1], rs[D3DRS_COLORWRITEENABLE2], rs[D3DRS_COLORWRITEENABLE3]};
    return cached(&d->blend_cache, k, BK_WORDS, make_blend, d);
}

enum {
    DK_DEPTH, DK_WRITE, DK_FUNC, DK_STENCIL, DK_READ_MASK, DK_WRITE_MASK, DK_FRONT, DK_BACK = DK_FRONT + 4,
    DK_WORDS = DK_BACK + 4
};

static void *make_depth(D9Device *d, const DWORD *k)
{
    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = k[DK_DEPTH];
    desc.DepthWriteMask = k[DK_DEPTH] && k[DK_WRITE] ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = k[DK_FUNC] >= 1 && k[DK_FUNC] <= 8 ? (D3D11_COMPARISON_FUNC)k[DK_FUNC] : D3D11_COMPARISON_LESS_EQUAL;
    desc.StencilEnable = k[DK_STENCIL];
    desc.StencilReadMask = (UINT8)k[DK_READ_MASK];
    desc.StencilWriteMask = (UINT8)k[DK_WRITE_MASK];
#define OP(v) ((v) >= 1 && (v) <= 8 ? (D3D11_STENCIL_OP)(v) : D3D11_STENCIL_OP_KEEP)
#define CMP(v) ((v) >= 1 && (v) <= 8 ? (D3D11_COMPARISON_FUNC)(v) : D3D11_COMPARISON_ALWAYS)
    const DWORD *f = k + DK_FRONT, *b = k + DK_BACK;
    desc.FrontFace = (D3D11_DEPTH_STENCILOP_DESC){OP(f[0]), OP(f[1]), OP(f[2]), CMP(f[3])};
    desc.BackFace = (D3D11_DEPTH_STENCILOP_DESC){OP(b[0]), OP(b[1]), OP(b[2]), CMP(b[3])};
#undef OP
#undef CMP
    ID3D11DepthStencilState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateDepthStencilState(d->d3d11, &desc, &s)) ? s : nullptr;
}

static ID3D11DepthStencilState *depth_state(D9Device *d)
{
    const DWORD *rs = d->render_states;
    BOOL depth = d->depth_stencil && rs[D3DRS_ZENABLE] != D3DZB_FALSE;
    BOOL two = rs[D3DRS_TWOSIDEDSTENCILMODE];
    const DWORD k[DK_WORDS] = {depth,
                               rs[D3DRS_ZWRITEENABLE] != 0,
                               rs[D3DRS_ZFUNC],
                               d->depth_stencil && rs[D3DRS_STENCILENABLE],
                               rs[D3DRS_STENCILMASK] & 0xFF,
                               rs[D3DRS_STENCILWRITEMASK] & 0xFF,
                               rs[D3DRS_STENCILFAIL],
                               rs[D3DRS_STENCILZFAIL],
                               rs[D3DRS_STENCILPASS],
                               rs[D3DRS_STENCILFUNC],
                               rs[two ? D3DRS_CCW_STENCILFAIL : D3DRS_STENCILFAIL],
                               rs[two ? D3DRS_CCW_STENCILZFAIL : D3DRS_STENCILZFAIL],
                               rs[two ? D3DRS_CCW_STENCILPASS : D3DRS_STENCILPASS],
                               rs[two ? D3DRS_CCW_STENCILFUNC : D3DRS_STENCILFUNC]};
    return cached(&d->depth_cache, k, DK_WORDS, make_depth, d);
}

/* d3d11 takes depth bias in units of the depth buffer's resolution. */
static INT depth_bias(D9Device *d)
{
    float bias = fbits(d->render_states[D3DRS_DEPTHBIAS]);
    if (bias == 0.0f || !d->depth_stencil)
        return 0;
    DXGI_FORMAT f = d->depth_stencil->base.format->depth;
    float scale = f == DXGI_FORMAT_D16_UNORM ? 65535.0f : f == DXGI_FORMAT_D24_UNORM_S8_UINT ? 16777215.0f : 8388608.0f;
    return (INT)(bias * scale);
}

enum { RK_FILL, RK_CULL, RK_BIAS, RK_SLOPE, RK_SCISSOR, RK_MULTISAMPLE, RK_LINES, RK_WORDS };

static void *make_raster(D9Device *d, const DWORD *k)
{
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = k[RK_FILL] == D3DFILL_WIREFRAME ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    /* direct3d 9's front faces are clockwise, as d3d11's are by default. */
    desc.CullMode = k[RK_CULL] == D3DCULL_CW    ? D3D11_CULL_FRONT
                    : k[RK_CULL] == D3DCULL_CCW ? D3D11_CULL_BACK
                                                : D3D11_CULL_NONE;
    desc.DepthBias = (INT)k[RK_BIAS];
    desc.SlopeScaledDepthBias = fbits(k[RK_SLOPE]);
    desc.DepthClipEnable = TRUE;
    desc.ScissorEnable = k[RK_SCISSOR];
    desc.MultisampleEnable = k[RK_MULTISAMPLE];
    desc.AntialiasedLineEnable = k[RK_LINES];
    ID3D11RasterizerState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateRasterizerState(d->d3d11, &desc, &s)) ? s : nullptr;
}

static ID3D11RasterizerState *raster_state(D9Device *d)
{
    const DWORD *rs = d->render_states;
    const DWORD k[RK_WORDS] = {rs[D3DRS_FILLMODE],
                               rs[D3DRS_CULLMODE],
                               (DWORD)depth_bias(d),
                               rs[D3DRS_SLOPESCALEDEPTHBIAS],
                               rs[D3DRS_SCISSORTESTENABLE] != 0,
                               rs[D3DRS_MULTISAMPLEANTIALIAS] != 0,
                               rs[D3DRS_ANTIALIASEDLINEENABLE] != 0};
    return cached(&d->raster_cache, k, RK_WORDS, make_raster, d);
}

enum { SK_MIN, SK_MAG, SK_MIP, SK_ANISOTROPY, SK_ADDRESS, SK_BIAS = SK_ADDRESS + 3, SK_BORDER, SK_LEVEL, SK_COMPARE,
       SK_WORDS };

static void *make_sampler(D9Device *d, const DWORD *k)
{
    D3D11_SAMPLER_DESC desc = {};
    DWORD min = k[SK_MIN], mag = k[SK_MAG], mip = k[SK_MIP];
    BOOL aniso = min == D3DTEXF_ANISOTROPIC || mag == D3DTEXF_ANISOTROPIC;
    UINT filter = (min >= D3DTEXF_LINEAR) << 4 | (mag >= D3DTEXF_LINEAR) << 2 | (mip >= D3DTEXF_LINEAR);
    if (aniso && k[SK_ANISOTROPY] > 1)
        filter = 0x55;
    desc.Filter = (D3D11_FILTER)(filter | (k[SK_COMPARE] ? 0x80 : 0));
    for (UINT i = 0; i < 3; i++) {
        DWORD a = k[SK_ADDRESS + i];
        (&desc.AddressU)[i] = a >= 1 && a <= 5 ? (D3D11_TEXTURE_ADDRESS_MODE)a : D3D11_TEXTURE_ADDRESS_WRAP;
    }
    desc.MipLODBias = fbits(k[SK_BIAS]);
    desc.MaxAnisotropy = D9_MAX(D9_MIN(k[SK_ANISOTROPY], 16), 1);
    desc.ComparisonFunc = k[SK_COMPARE] ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
    D3DCOLOR border = k[SK_BORDER];
    desc.BorderColor[0] = ((border >> 16) & 0xFF) / 255.0f;
    desc.BorderColor[1] = ((border >> 8) & 0xFF) / 255.0f;
    desc.BorderColor[2] = (border & 0xFF) / 255.0f;
    desc.BorderColor[3] = (border >> 24) / 255.0f;
    /* the max mip level is the most detailed one sampled. without mips, only that one. */
    desc.MinLOD = (float)k[SK_LEVEL];
    desc.MaxLOD = mip == D3DTEXF_NONE ? desc.MinLOD : D3D11_FLOAT32_MAX;
    ID3D11SamplerState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateSamplerState(d->d3d11, &desc, &s)) ? s : nullptr;
}

static ID3D11SamplerState *sampler_state(D9Device *d, UINT slot, BOOL compare)
{
    const DWORD *ss = d->sampler_states[slot];
    const DWORD k[SK_WORDS] = {ss[D3DSAMP_MINFILTER],    ss[D3DSAMP_MAGFILTER],     ss[D3DSAMP_MIPFILTER],
                               ss[D3DSAMP_MAXANISOTROPY], ss[D3DSAMP_ADDRESSU],      ss[D3DSAMP_ADDRESSV],
                               ss[D3DSAMP_ADDRESSW],      ss[D3DSAMP_MIPMAPLODBIAS], ss[D3DSAMP_BORDERCOLOR],
                               ss[D3DSAMP_MAXMIPLEVEL],   compare};
    return cached(&d->sampler_cache, k, SK_WORDS, make_sampler, d);
}

/* ------------------------------------------------------------------ constants */

static ID3D11Buffer *make_cb(D9Device *d, UINT size)
{
    D3D11_BUFFER_DESC desc = {size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
    ID3D11Buffer *b;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &desc, nullptr, &b)) ? b : nullptr;
}

/* 16 to 256 float registers, or all of them with relative addressing. */
static UINT bucket_of(const D9ShaderInfo *info)
{
    UINT count = info->relative ? 256 : info->float_count;
    UINT bucket = 0;
    while (bucket < 4 && (16u << bucket) < count)
        bucket++;
    return bucket;
}

UINT D9FloatBucketSize(const D9ShaderInfo *info)
{
    UINT max = info->pixel ? D9_PS_CONSTANTS : D9_VS_CONSTANTS;
    return D9_MIN(16u << bucket_of(info), max);
}

/*
 * the buffer is no larger than what the shader reads: each upload renames
 * it, so small ones keep d3d11's ring of dynamic ranges from filling.
 */
static void upload_constants(D9Device *d, BOOL pixel, const D9ShaderInfo *info, BOOL ff)
{
    ID3D11DeviceContext1 *ctx = d->context;
    ID3D11Buffer **cbs = d->cbs[pixel];
    const float(*f)[4] = ff ? (pixel ? d->ff_ps_consts : d->ff_vs_consts) : pixel ? d->ps_f : d->vs_f;
    const int(*ints)[4] = pixel ? d->ps_i : d->vs_i;
    const BOOL *bools = pixel ? d->ps_b : d->vs_b;
    D3D11_MAPPED_SUBRESOURCE m;
    UINT bucket = bucket_of(info);
    UINT count = D9FloatBucketSize(info);
    UINT available = ff ? (pixel ? D9_FF_PS_CONSTANTS : D9_FF_VS_CONSTANTS) : count;
    ID3D11Buffer *floats = d->float_cbs[pixel][bucket];
    if (bucket != d->bound_bucket[pixel]) {
        if (pixel)
            ctx->lpVtbl->PSSetConstantBuffers(ctx, 0, 1, &floats);
        else
            ctx->lpVtbl->VSSetConstantBuffers(ctx, 0, 1, &floats);
        d->bound_bucket[pixel] = bucket;
    }
    if (SUCCEEDED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)floats, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, f, D9_MIN(count, available) * 16);
        if (available < count)
            memset((BYTE *)m.pData + available * 16, 0, (count - available) * 16);
        for (UINT i = 0; i < info->def_count; i++)
            if ((info->defs[i].reg >> 16) == 2 && (info->defs[i].reg & 0xFFFF) < count)
                memcpy((BYTE *)m.pData + (info->defs[i].reg & 0xFFFF) * 16, info->defs[i].v, 16);
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)floats, 0);
    }
    if (!info->uses_ints)
        return;
    if (SUCCEEDED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)cbs[1], 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        UINT *words = m.pData;
        memcpy(words, ints, 16 * 16);
        for (UINT i = 0; i < 16; i++)
            words[64 + i] = bools[i] ? 1 : 0;
        for (UINT i = 0; i < info->def_count; i++) {
            UINT type = info->defs[i].reg >> 16, reg = info->defs[i].reg & 0xFFFF;
            if (type == 7 && reg < 16)
                memcpy(words + reg * 4, info->defs[i].v, 16);
            else if (type == 14 && reg < 16)
                words[64 + reg] = info->defs[i].v[0] ? 1 : 0;
        }
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)cbs[1], 0);
    }
}

/* internal constants: the half pixel, clip planes, points, fog and alpha test. */
static void upload_internal(D9Device *d, BOOL pixel, BOOL ff)
{
    ID3D11DeviceContext1 *ctx = d->context;
    float v[24][4] = {{0}};
    const DWORD *rs = d->render_states;
    if (!pixel) {
        /* half a pixel right and down: direct3d 9's pixel center i is direct3d 10's i + 0.5. */
        v[0][0] = d->viewport.Width ? 1.0f / d->viewport.Width : 0.0f;
        v[0][1] = d->viewport.Height ? -1.0f / d->viewport.Height : 0.0f;
        v[0][2] = fbits(rs[D3DRS_POINTSIZE]);
        if (ff)
            D9FFClipPlanes(d, (float(*)[4])v[1]);
        else
            memcpy(v[1], d->clip_planes, sizeof(d->clip_planes));
        v[7][0] = fbits(rs[D3DRS_POINTSIZE_MIN]);
        v[7][1] = fbits(rs[D3DRS_POINTSIZE_MAX]);
    } else {
        v[0][0] = (float)(rs[D3DRS_ALPHAREF] & 0xFF);
        D3DCOLOR fog = rs[D3DRS_FOGCOLOR];
        v[1][0] = ((fog >> 16) & 0xFF) / 255.0f;
        v[1][1] = ((fog >> 8) & 0xFF) / 255.0f;
        v[1][2] = (fog & 0xFF) / 255.0f;
        v[1][3] = (fog >> 24) / 255.0f;
        float start = fbits(rs[D3DRS_FOGSTART]), end = fbits(rs[D3DRS_FOGEND]);
        v[2][0] = start;
        v[2][1] = end;
        v[2][2] = fbits(rs[D3DRS_FOGDENSITY]);
        v[2][3] = end != start ? 1.0f / (end - start) : 0.0f;
        /* bump matrices and luminance of each stage, for model 1 pixel shaders. */
        for (UINT i = 0; i < D9_MAX_STAGES; i++) {
            const DWORD *ts = d->stage_states[i];
            v[4 + i][0] = fbits(ts[D3DTSS_BUMPENVMAT00]);
            v[4 + i][1] = fbits(ts[D3DTSS_BUMPENVMAT01]);
            v[4 + i][2] = fbits(ts[D3DTSS_BUMPENVMAT10]);
            v[4 + i][3] = fbits(ts[D3DTSS_BUMPENVMAT11]);
            v[12 + i][0] = fbits(ts[D3DTSS_BUMPENVLSCALE]);
            v[12 + i][1] = fbits(ts[D3DTSS_BUMPENVLOFFSET]);
        }
    }
    if (d->internal_valid[pixel] && !memcmp(d->internal[pixel], v, sizeof(v)))
        return;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)d->cbs[pixel][2], 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    memcpy(m.pData, v, sizeof(v));
    ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)d->cbs[pixel][2], 0);
    memcpy(d->internal[pixel], v, sizeof(v));
    d->internal_valid[pixel] = TRUE;
}

/* ------------------------------------------------------------------ preparing a draw */

static D3D11_PRIMITIVE_TOPOLOGY topology(D3DPRIMITIVETYPE type)
{
    switch (type) {
    case D3DPT_LINESTRIP:
    case D3DPT_TRIANGLESTRIP:
        return (D3D11_PRIMITIVE_TOPOLOGY)(MIME_TOPOLOGY_D3D9 | type);
    case D3DPT_TRIANGLEFAN:
        return (D3D11_PRIMITIVE_TOPOLOGY)MIME_TOPOLOGY_TRIANGLEFAN;
    default:
        return (D3D11_PRIMITIVE_TOPOLOGY)type;
    }
}

UINT D9VertexCount(D3DPRIMITIVETYPE type, UINT primitives)
{
    switch (type) {
    case D3DPT_POINTLIST:
        return primitives;
    case D3DPT_LINELIST:
        return primitives * 2;
    case D3DPT_LINESTRIP:
        return primitives + 1;
    case D3DPT_TRIANGLELIST:
        return primitives * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return primitives + 2;
    default:
        return 0;
    }
}

static void bind_targets(D9Device *d)
{
    ID3D11RenderTargetView *rtvs[D9_MAX_TARGETS] = {};
    UINT count = 0;
    BOOL srgb = !!d->render_states[D3DRS_SRGBWRITEENABLE];
    for (UINT i = 0; i < D9_MAX_TARGETS; i++)
        if (d->targets[i] && (rtvs[i] = D9SurfaceRtv(d->targets[i], srgb)))
            count = i + 1;
    ID3D11DepthStencilView *dsv = d->depth_stencil ? D9SurfaceDsv(d->depth_stencil) : nullptr;
    d->context->lpVtbl->OMSetRenderTargets(d->context, count, rtvs, dsv);
    d->bound_srgb = srgb;
    d->targets_dirty = FALSE;
}

static void bind_viewport(D9Device *d)
{
    const D3DVIEWPORT9 *v = &d->viewport;
    D3D11_VIEWPORT vp = {(FLOAT)v->X, (FLOAT)v->Y, (FLOAT)v->Width, (FLOAT)v->Height, v->MinZ, v->MaxZ};
    d->context->lpVtbl->RSSetViewports(d->context, 1, &vp);
    D3D11_RECT r = {d->scissor.left, d->scissor.top, d->scissor.right, d->scissor.bottom};
    d->context->lpVtbl->RSSetScissorRects(d->context, 1, &r);
    d->viewport_dirty = FALSE;
}

/* pixel samplers go to slots 0 to 15, vertex samplers 0 to 3 to slots 17 and up. */
static void bind_textures(D9Device *d, D9Variant *ps, D9Variant *vs)
{
    ID3D11DeviceContext1 *ctx = d->context;
    for (UINT slot = 0; slot < D9_MAX_SAMPLERS; slot++) {
        BOOL vertex = slot >= 17;
        if (slot == 16)
            continue;
        UINT unit = vertex ? slot - 17 : slot;
        D9Texture *t = d->textures[slot];
        if (t)
            D9TexturePrepare(t);
        BOOL compare = !vertex && ps && (ps->key.shadow & (1u << unit));
        ID3D11ShaderResourceView *srv = t ? D9TextureSrv(t, d->sampler_states[slot][D3DSAMP_SRGBTEXTURE]) : nullptr;
        ID3D11SamplerState *sampler = t ? sampler_state(d, slot, compare) : nullptr;
        if (srv != d->bound_srvs[slot]) {
            if (vertex)
                ctx->lpVtbl->VSSetShaderResources(ctx, unit, 1, &srv);
            else
                ctx->lpVtbl->PSSetShaderResources(ctx, unit, 1, &srv);
            d->bound_srvs[slot] = srv;
        }
        if (sampler && sampler != d->bound_samplers[slot]) {
            if (vertex)
                ctx->lpVtbl->VSSetSamplers(ctx, unit, 1, &sampler);
            else
                ctx->lpVtbl->PSSetSamplers(ctx, unit, 1, &sampler);
            d->bound_samplers[slot] = sampler;
        }
    }
    (void)vs;
}

static void vs_key(D9Device *d, D9Shader *shader, D3DPRIMITIVETYPE type, D9ShaderKey *key)
{
    memset(key, 0, sizeof(*key));
    key->clip_planes = (BYTE)(d->render_states[D3DRS_CLIPPLANEENABLE] & 0x3F);
    key->point_size = type == D3DPT_POINTLIST;
    if (d->decl)
        key->int_inputs = D9DeclIntegerInputs(d->decl, &shader->info);
}

static void ps_key(D9Device *d, D9Shader *shader, D3DPRIMITIVETYPE type, const D9Variant *vs, D9ShaderKey *key)
{
    const DWORD *rs = d->render_states;
    const D9ShaderInfo *info = &shader->info;
    memset(key, 0, sizeof(*key));
    key->ps_inputs = vs ? vs->outputs : 0;
    for (UINT i = 0; i < 16; i++) {
        D9Texture *t = d->textures[i];
        if (!t)
            continue;
        if (t->base.format->flags & D9_FORMAT_DEPTH && !t->base.format->swizzle)
            key->shadow |= 1u << i;
        if (!info->sampler_type[i] && t->base.type != D3DRTYPE_TEXTURE)
            key->sampler_types |= (t->base.type == D3DRTYPE_CUBETEXTURE ? 1u : 2u) << (i * 2);
    }
    if (rs[D3DRS_FOGENABLE] && info->major < 3) {
        if (rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE)
            key->fog = (BYTE)(1 + rs[D3DRS_FOGTABLEMODE]);
        else
            key->fog = 1;
    }
    if (rs[D3DRS_ALPHATESTENABLE])
        key->alpha_func = (BYTE)rs[D3DRS_ALPHAFUNC];
    key->flat = rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    if (info->major < 2 && info->minor < 4)
        for (UINT i = 0; i < D9_MAX_STAGES; i++)
            if (d->stage_states[i][D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED)
                key->projected |= 1u << i;
    key->point_sprite = type == D3DPT_POINTLIST && rs[D3DRS_POINTSPRITEENABLE];
}

/* FALSE when the draw cannot be made. */
static BOOL prepare(D9Device *d, D3DPRIMITIVETYPE type)
{
    ID3D11DeviceContext1 *ctx = d->context;
    if (!d->decl)
        return FALSE;
    /* without shaders, use the fixed function programs. */
    D9Shader *vs_shader = d->vs, *ps_shader = d->ps;
    if (!vs_shader) {
        D9FFVSKey k;
        D9FFVSKeyOf(d, &k);
        vs_shader = D9FFShader(d, FALSE, &k, sizeof(k));
    }
    if (!ps_shader) {
        D9FFPSKey k;
        D9FFPSKeyOf(d, &k);
        ps_shader = D9FFShader(d, TRUE, &k, sizeof(k));
    }
    if (!vs_shader || !ps_shader)
        return FALSE;
    if ((vs_shader->ff || ps_shader->ff) && (d->ff_dirty || d->states_dirty || d->viewport_dirty)) {
        D9UpdateFFConstants(d);
        if (vs_shader->ff)
            d->vs_consts_dirty = TRUE;
        if (ps_shader->ff)
            d->ps_consts_dirty = TRUE;
    }
    D9ShaderKey key;
    vs_key(d, vs_shader, type, &key);
    D9Variant *vs = D9ShaderVariant(vs_shader, &key);
    if (!vs || !vs->shader)
        return FALSE;
    ps_key(d, ps_shader, type, vs, &key);
    D9Variant *ps = D9ShaderVariant(ps_shader, &key);
    if (!ps || !ps->shader)
        return FALSE;
    if (vs != d->bound_vs) {
        ctx->lpVtbl->VSSetShader(ctx, (ID3D11VertexShader *)vs->shader, nullptr, 0);
        d->bound_vs = vs;
        d->vs_consts_dirty = TRUE;
    }
    if (ps != d->bound_ps) {
        ctx->lpVtbl->PSSetShader(ctx, (ID3D11PixelShader *)ps->shader, nullptr, 0);
        d->bound_ps = ps;
        d->ps_consts_dirty = TRUE;
    }
    /* streams SetStreamSourceFreq marks as instanced step once every count instances. */
    UINT instanced = 0, rates[16] = {};
    for (UINT i = 1; i < D9_MAX_STREAMS; i++)
        if (d->stream_freq[i] & D3DSTREAMSOURCE_INSTANCEDATA) {
            instanced |= 1u << i;
            rates[i] = d->stream_freq[i] & 0x3FFFFFFF;
        }
    if (!(d->stream_freq[0] & D3DSTREAMSOURCE_INDEXEDDATA))
        instanced = 0;
    ID3D11InputLayout *layout = D9DeclLayout(d->decl, d, vs, instanced, rates);
    if (!layout)
        return FALSE;
    if (layout != d->bound_layout) {
        ctx->lpVtbl->IASetInputLayout(ctx, layout);
        d->bound_layout = layout;
    }
    D3D11_PRIMITIVE_TOPOLOGY topo = topology(type);
    if (topo != d->bound_topology) {
        ctx->lpVtbl->IASetPrimitiveTopology(ctx, topo);
        d->bound_topology = topo;
    }
    if (d->streams_dirty) {
        for (UINT i = 0; i < D9_MAX_STREAMS; i++) {
            if (!(d->streams_dirty & (1u << i)))
                continue;
            D9Buffer *b = d->streams[i].buffer;
            ID3D11Buffer *buffer = b ? b->buffer : nullptr;
            UINT stride = d->streams[i].stride, offset = d->streams[i].offset;
            ctx->lpVtbl->IASetVertexBuffers(ctx, i, 1, &buffer, &stride, &offset);
        }
        d->streams_dirty = 0;
    }
    for (UINT i = 0; i < D9_MAX_STREAMS; i++)
        if (d->streams[i].buffer)
            d->streams[i].buffer->used = TRUE;
    if (d->indices_dirty) {
        D9Buffer *b = d->indices;
        ctx->lpVtbl->IASetIndexBuffer(ctx, b ? b->buffer : nullptr,
                                      b && b->base.d3d_format == D3DFMT_INDEX32 ? DXGI_FORMAT_R32_UINT
                                                                                 : DXGI_FORMAT_R16_UINT,
                                      0);
        d->indices_dirty = FALSE;
    }
    if (d->indices)
        d->indices->used = TRUE;
    if (d->targets_dirty || d->bound_srgb != !!d->render_states[D3DRS_SRGBWRITEENABLE]) {
        bind_targets(d);
        /* depth tests and bias depend on the depth buffer. */
        d->state_groups |= D9_STATES_DEPTH | D9_STATES_RASTER;
    }
    if (d->viewport_dirty) {
        bind_viewport(d);
        d->internal_dirty = TRUE;
    }
    if (d->vs_consts_dirty) {
        upload_constants(d, FALSE, vs->info, vs_shader->ff);
        d->vs_consts_dirty = FALSE;
    }
    if (d->ps_consts_dirty) {
        upload_constants(d, TRUE, ps->info, ps_shader->ff);
        d->ps_consts_dirty = FALSE;
    }
    if ((d->state_groups & D9_STATES_INTERNAL) || d->internal_dirty || d->ff_dirty || vs_shader->ff != d->bound_vs_ff) {
        upload_internal(d, FALSE, vs_shader->ff);
        upload_internal(d, TRUE, FALSE);
        d->bound_vs_ff = vs_shader->ff;
        d->internal_dirty = FALSE;
        d->ff_dirty = FALSE;
    }
    if (d->state_groups & D9_STATES_BLEND) {
        D3DCOLOR f = d->render_states[D3DRS_BLENDFACTOR];
        FLOAT factor[4] = {((f >> 16) & 0xFF) / 255.0f, ((f >> 8) & 0xFF) / 255.0f, (f & 0xFF) / 255.0f,
                           (f >> 24) / 255.0f};
        ID3D11BlendState *blend = blend_state(d);
        if (blend != d->bound_blend || f != d->bound_blend_factor ||
            d->render_states[D3DRS_MULTISAMPLEMASK] != d->bound_sample_mask) {
            ctx->lpVtbl->OMSetBlendState(ctx, blend, factor, d->render_states[D3DRS_MULTISAMPLEMASK]);
            d->bound_blend = blend;
            d->bound_blend_factor = f;
            d->bound_sample_mask = d->render_states[D3DRS_MULTISAMPLEMASK];
        }
    }
    if (d->state_groups & D9_STATES_DEPTH) {
        ID3D11DepthStencilState *depth = depth_state(d);
        if (depth != d->bound_depth || (d->render_states[D3DRS_STENCILREF] & 0xFF) != d->bound_stencil_ref) {
            ctx->lpVtbl->OMSetDepthStencilState(ctx, depth, d->render_states[D3DRS_STENCILREF] & 0xFF);
            d->bound_depth = depth;
            d->bound_stencil_ref = d->render_states[D3DRS_STENCILREF] & 0xFF;
        }
    }
    if (d->state_groups & D9_STATES_RASTER) {
        ID3D11RasterizerState *raster = raster_state(d);
        if (raster != d->bound_raster) {
            ctx->lpVtbl->RSSetState(ctx, raster);
            d->bound_raster = raster;
        }
    }
    d->state_groups = 0;
    d->states_dirty = FALSE;
    bind_textures(d, ps, vs);
    return TRUE;
}

BOOL D9PrepareDraw(D9Device *d, D3DPRIMITIVETYPE type) { return prepare(d, type); }

/* ------------------------------------------------------------------ the draws */

HRESULT D9Draw(D9Device *d, D3DPRIMITIVETYPE type, UINT start, UINT primitives)
{
    UINT vertices = D9VertexCount(type, primitives);
    if (!vertices)
        return type < D3DPT_POINTLIST || type > D3DPT_TRIANGLEFAN ? D3DERR_INVALIDCALL : D3D_OK;
    if (prepare(d, type))
        d->context->lpVtbl->Draw(d->context, vertices, start);
    return D3D_OK;
}

HRESULT D9DrawIndexed(D9Device *d, D3DPRIMITIVETYPE type, INT base, UINT start, UINT primitives)
{
    UINT indices = D9VertexCount(type, primitives);
    if (!d->indices)
        return D3DERR_INVALIDCALL;
    if (!indices)
        return type < D3DPT_POINTLIST || type > D3DPT_TRIANGLEFAN ? D3DERR_INVALIDCALL : D3D_OK;
    if (!prepare(d, type))
        return D3D_OK;
    UINT instances = d->stream_freq[0] & D3DSTREAMSOURCE_INDEXEDDATA ? d->stream_freq[0] & 0x3FFFFFFF : 1;
    if (instances > 1 || (d->stream_freq[0] & D3DSTREAMSOURCE_INDEXEDDATA))
        d->context->lpVtbl->DrawIndexedInstanced(d->context, indices, D9_MAX(instances, 1), start, base, 0);
    else
        d->context->lpVtbl->DrawIndexed(d->context, indices, start, base);
    return D3D_OK;
}

/* draws from memory write to a ring the gpu reads, discarded when it wraps. */
static BOOL ring_write(D9Device *d, D9Ring *ring, UINT bind, const void *data, UINT size, UINT *offset)
{
    ID3D11DeviceContext1 *ctx = d->context;
    if (!ring->buffer || size > ring->size) {
        if (ring->buffer)
            ring->buffer->lpVtbl->Release(ring->buffer);
        ring->size = D9_MAX(D9_ALIGN(size, 65536), 4u << 20);
        D3D11_BUFFER_DESC desc = {ring->size, D3D11_USAGE_DYNAMIC, bind, D3D11_CPU_ACCESS_WRITE};
        if (FAILED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &desc, nullptr, &ring->buffer))) {
            ring->buffer = nullptr;
            return FALSE;
        }
        ring->used = ring->size;
    }
    D3D11_MAP map = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (ring->used + size > ring->size) {
        map = D3D11_MAP_WRITE_DISCARD;
        ring->used = 0;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)ring->buffer, 0, map, 0, &m)))
        return FALSE;
    memcpy((BYTE *)m.pData + ring->used, data, size);
    ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)ring->buffer, 0);
    *offset = ring->used;
    ring->used = D9_ALIGN(ring->used + size, 16);
    return TRUE;
}

/* like direct3d 9, draws from memory leave stream 0 unbound, and the indices for indexed ones. */
static void unbind_up(D9Device *d, BOOL indexed)
{
    D9DeviceSetStream(d, 0, nullptr, 0, 0);
    if (indexed)
        D9DeviceSetIndices(d, nullptr);
}

HRESULT D9DrawUP(D9Device *d, D3DPRIMITIVETYPE type, UINT primitives, const void *data, UINT stride)
{
    UINT vertices = D9VertexCount(type, primitives);
    if (!data || !stride)
        return D3DERR_INVALIDCALL;
    unbind_up(d, FALSE);
    if (!vertices)
        return D3D_OK;
    UINT offset;
    if (!ring_write(d, &d->up_vertices, D3D11_BIND_VERTEX_BUFFER, data, vertices * stride, &offset))
        return E_OUTOFMEMORY;
    ID3D11DeviceContext1 *ctx = d->context;
    ctx->lpVtbl->IASetVertexBuffers(ctx, 0, 1, &d->up_vertices.buffer, &stride, &offset);
    d->streams_dirty |= 1;
    if (prepare(d, type)) {
        ctx->lpVtbl->IASetVertexBuffers(ctx, 0, 1, &d->up_vertices.buffer, &stride, &offset);
        ctx->lpVtbl->Draw(ctx, vertices, 0);
    }
    d->streams_dirty |= 1;
    return D3D_OK;
}

HRESULT D9DrawIndexedUP(D9Device *d, D3DPRIMITIVETYPE type, UINT min_index, UINT vertex_count, UINT primitives,
                        const void *indices, D3DFORMAT format, const void *data, UINT stride)
{
    UINT count = D9VertexCount(type, primitives);
    if (!data || !indices || !stride || (format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32))
        return D3DERR_INVALIDCALL;
    unbind_up(d, TRUE);
    if (!count)
        return D3D_OK;
    UINT index_size = format == D3DFMT_INDEX32 ? 4 : 2;
    UINT voffset, ioffset;
    /* only vertices from the lowest index on, indices made relative to it. */
    if (!ring_write(d, &d->up_vertices, D3D11_BIND_VERTEX_BUFFER, (const BYTE *)data + (SIZE_T)min_index * stride,
                    vertex_count * stride, &voffset) ||
        !ring_write(d, &d->up_indices, D3D11_BIND_INDEX_BUFFER, indices, count * index_size, &ioffset))
        return E_OUTOFMEMORY;
    ID3D11DeviceContext1 *ctx = d->context;
    d->streams_dirty |= 1;
    d->indices_dirty = TRUE;
    if (prepare(d, type)) {
        ctx->lpVtbl->IASetVertexBuffers(ctx, 0, 1, &d->up_vertices.buffer, &stride, &voffset);
        ctx->lpVtbl->IASetIndexBuffer(ctx, d->up_indices.buffer,
                                      index_size == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, ioffset);
        ctx->lpVtbl->DrawIndexed(ctx, count, 0, -(INT)min_index);
    }
    d->streams_dirty |= 1;
    d->indices_dirty = TRUE;
    return D3D_OK;
}

HRESULT D9InitDraws(D9Device *d)
{
    for (UINT p = 0; p < 2; p++) {
        for (UINT b = 0; b < 5; b++)
            if (!(d->float_cbs[p][b] = make_cb(d, D9_MIN(16u << b, p ? D9_PS_CONSTANTS : D9_VS_CONSTANTS) * 16)))
                return E_OUTOFMEMORY;
        d->cbs[p][0] = d->float_cbs[p][4];
        d->cbs[p][0]->lpVtbl->AddRef(d->cbs[p][0]);
        d->bound_bucket[p] = 4;
        d->cbs[p][1] = make_cb(d, 20 * 16);
        d->cbs[p][2] = make_cb(d, 24 * 16);
        for (UINT i = 0; i < 3; i++)
            if (!d->cbs[p][i])
                return E_OUTOFMEMORY;
    }
    ID3D11DeviceContext1 *ctx = d->context;
    ctx->lpVtbl->VSSetConstantBuffers(ctx, 0, 3, d->cbs[0]);
    ctx->lpVtbl->PSSetConstantBuffers(ctx, 0, 3, d->cbs[1]);
    static const float ZEROS[16];
    D3D11_BUFFER_DESC desc = {sizeof(ZEROS), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER};
    D3D11_SUBRESOURCE_DATA data = {ZEROS};
    if (FAILED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &desc, &data, &d->zeros)))
        return E_OUTOFMEMORY;
    UINT stride = 0, offset = 0;
    ctx->lpVtbl->IASetVertexBuffers(ctx, D9_ZERO_STREAM, 1, &d->zeros, &stride, &offset);
    D9ResetBindings(d);
    return D3D_OK;
}

/* forgets the context bindings so the next draw sets everything. */
void D9ResetBindings(D9Device *d)
{
    d->bound_vs = d->bound_ps = nullptr;
    d->bound_layout = nullptr;
    d->bound_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    d->bound_blend = nullptr;
    d->bound_depth = nullptr;
    d->bound_raster = nullptr;
    d->internal_valid[0] = d->internal_valid[1] = FALSE;
    memset(d->bound_srvs, 0, sizeof(d->bound_srvs));
    memset(d->bound_samplers, 0, sizeof(d->bound_samplers));
    d->streams_dirty = ~0u;
    d->indices_dirty = TRUE;
    d->targets_dirty = TRUE;
    d->viewport_dirty = TRUE;
    d->states_dirty = TRUE;
    d->state_groups = D9_STATES_ALL;
    d->internal_dirty = TRUE;
    d->vs_consts_dirty = d->ps_consts_dirty = TRUE;
}

void D9FreeDraws(D9Device *d)
{
    for (UINT p = 0; p < 2; p++) {
        for (UINT i = 0; i < 3; i++)
            if (d->cbs[p][i])
                d->cbs[p][i]->lpVtbl->Release(d->cbs[p][i]);
        for (UINT b = 0; b < 5; b++)
            if (d->float_cbs[p][b])
                d->float_cbs[p][b]->lpVtbl->Release(d->float_cbs[p][b]);
    }
    memset(d->cbs, 0, sizeof(d->cbs));
    memset(d->float_cbs, 0, sizeof(d->float_cbs));
    if (d->zeros)
        d->zeros->lpVtbl->Release(d->zeros);
    if (d->up_vertices.buffer)
        d->up_vertices.buffer->lpVtbl->Release(d->up_vertices.buffer);
    if (d->up_indices.buffer)
        d->up_indices.buffer->lpVtbl->Release(d->up_indices.buffer);
    d->zeros = nullptr;
    if (d->processed)
        d->processed->lpVtbl->Release(d->processed);
    if (d->processed_read)
        d->processed_read->lpVtbl->Release(d->processed_read);
    d->processed = d->processed_read = nullptr;
    d->processed_size = 0;
    memset(&d->up_vertices, 0, sizeof(d->up_vertices));
    memset(&d->up_indices, 0, sizeof(d->up_indices));
    D9ClearStateCache(&d->blend_cache);
    D9ClearStateCache(&d->depth_cache);
    D9ClearStateCache(&d->raster_cache);
    D9ClearStateCache(&d->sampler_cache);
    D9FreeFFShaders(d);
}
