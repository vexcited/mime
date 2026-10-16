/*
 * draws: direct3d 9's state turned into d3d11's when a draw needs it. the
 * state objects are cached by their descriptions; constants go to
 * dynamic buffers, each shader's own definitions laid over the device's,
 * as they override them; the translations of the bound shaders are found
 * by the keys of the state they depend on.
 */
#include "d3d9_private.h"

static float fbits(DWORD v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* ------------------------------------------------------------------ state objects */

/* a small cache of d3d11 state objects by their descriptions, the oldest replaced when full. */
static void *cached(D9StateCache *cache, const void *desc, UINT size, void *(*make)(D9Device *, const void *),
                    D9Device *device)
{
    for (UINT i = 0; i < cache->count; i++)
        if (cache->entries[i].size == size && !memcmp(cache->entries[i].desc, desc, size))
            return cache->entries[i].object;
    void *object = make(device, desc);
    if (!object)
        return NULL;
    UINT slot = cache->count < RTL_NUMBER_OF(cache->entries) ? cache->count++ : cache->next++ % RTL_NUMBER_OF(cache->entries);
    D9StateEntry *e = &cache->entries[slot];
    if (e->object)
        ((IUnknown *)e->object)->lpVtbl->Release((IUnknown *)e->object);
    memcpy(e->desc, desc, size);
    e->size = size;
    e->object = object;
    return object;
}

void D9ClearStateCache(D9StateCache *cache)
{
    for (UINT i = 0; i < cache->count; i++)
        if (cache->entries[i].object)
            ((IUnknown *)cache->entries[i].object)->lpVtbl->Release((IUnknown *)cache->entries[i].object);
    memset(cache, 0, sizeof(*cache));
}

static void *make_blend(D9Device *d, const void *desc)
{
    ID3D11BlendState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateBlendState(d->d3d11, desc, &s)) ? s : NULL;
}

static void *make_depth(D9Device *d, const void *desc)
{
    ID3D11DepthStencilState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateDepthStencilState(d->d3d11, desc, &s)) ? s : NULL;
}

static void *make_raster(D9Device *d, const void *desc)
{
    ID3D11RasterizerState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateRasterizerState(d->d3d11, desc, &s)) ? s : NULL;
}

static void *make_sampler(D9Device *d, const void *desc)
{
    ID3D11SamplerState *s;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateSamplerState(d->d3d11, desc, &s)) ? s : NULL;
}

/* d3d9's blend factors as d3d11's; the alpha's colors are their alphas, as d3d11 takes only those there. */
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

static ID3D11BlendState *blend_state(D9Device *d)
{
    const DWORD *rs = d->render_states;
    D3D11_BLEND_DESC desc = {FALSE, TRUE};
    D3D11_RENDER_TARGET_BLEND_DESC t = {!!rs[D3DRS_ALPHABLENDENABLE]};
    blend_pair(rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND], FALSE, &t.SrcBlend, &t.DestBlend);
    t.BlendOp = blend_op(rs[D3DRS_BLENDOP]);
    if (rs[D3DRS_SEPARATEALPHABLENDENABLE]) {
        blend_pair(rs[D3DRS_SRCBLENDALPHA], rs[D3DRS_DESTBLENDALPHA], TRUE, &t.SrcBlendAlpha, &t.DestBlendAlpha);
        t.BlendOpAlpha = blend_op(rs[D3DRS_BLENDOPALPHA]);
    } else {
        blend_pair(rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND], TRUE, &t.SrcBlendAlpha, &t.DestBlendAlpha);
        t.BlendOpAlpha = t.BlendOp;
    }
    static const D3DRENDERSTATETYPE MASKS[4] = {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1,
                                                D3DRS_COLORWRITEENABLE2, D3DRS_COLORWRITEENABLE3};
    for (UINT i = 0; i < 4; i++) {
        desc.RenderTarget[i] = t;
        desc.RenderTarget[i].RenderTargetWriteMask = (UINT8)(rs[MASKS[i]] & 0xF);
    }
    return cached(&d->blend_cache, &desc, sizeof(desc), make_blend, d);
}

static ID3D11DepthStencilState *depth_state(D9Device *d)
{
    const DWORD *rs = d->render_states;
    D3D11_DEPTH_STENCIL_DESC desc = {0};
    BOOL depth = d->depth_stencil && rs[D3DRS_ZENABLE] != D3DZB_FALSE;
    desc.DepthEnable = depth;
    desc.DepthWriteMask = depth && rs[D3DRS_ZWRITEENABLE] ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = rs[D3DRS_ZFUNC] >= 1 && rs[D3DRS_ZFUNC] <= 8 ? (D3D11_COMPARISON_FUNC)rs[D3DRS_ZFUNC]
                                                                 : D3D11_COMPARISON_LESS_EQUAL;
    desc.StencilEnable = d->depth_stencil && rs[D3DRS_STENCILENABLE];
    desc.StencilReadMask = (UINT8)rs[D3DRS_STENCILMASK];
    desc.StencilWriteMask = (UINT8)rs[D3DRS_STENCILWRITEMASK];
#define OP(v) ((v) >= 1 && (v) <= 8 ? (D3D11_STENCIL_OP)(v) : D3D11_STENCIL_OP_KEEP)
#define CMP(v) ((v) >= 1 && (v) <= 8 ? (D3D11_COMPARISON_FUNC)(v) : D3D11_COMPARISON_ALWAYS)
    desc.FrontFace = (D3D11_DEPTH_STENCILOP_DESC){OP(rs[D3DRS_STENCILFAIL]), OP(rs[D3DRS_STENCILZFAIL]),
                                                  OP(rs[D3DRS_STENCILPASS]), CMP(rs[D3DRS_STENCILFUNC])};
    if (rs[D3DRS_TWOSIDEDSTENCILMODE])
        desc.BackFace = (D3D11_DEPTH_STENCILOP_DESC){OP(rs[D3DRS_CCW_STENCILFAIL]), OP(rs[D3DRS_CCW_STENCILZFAIL]),
                                                     OP(rs[D3DRS_CCW_STENCILPASS]), CMP(rs[D3DRS_CCW_STENCILFUNC])};
    else
        desc.BackFace = desc.FrontFace;
#undef OP
#undef CMP
    return cached(&d->depth_cache, &desc, sizeof(desc), make_depth, d);
}

/* depth bias in units of the depth buffer's resolution, as d3d11 takes it. */
static INT depth_bias(D9Device *d)
{
    float bias = fbits(d->render_states[D3DRS_DEPTHBIAS]);
    if (bias == 0.0f || !d->depth_stencil)
        return 0;
    DXGI_FORMAT f = d->depth_stencil->base.format->depth;
    float scale = f == DXGI_FORMAT_D16_UNORM ? 65535.0f : f == DXGI_FORMAT_D24_UNORM_S8_UINT ? 16777215.0f : 8388608.0f;
    return (INT)(bias * scale);
}

static ID3D11RasterizerState *raster_state(D9Device *d)
{
    const DWORD *rs = d->render_states;
    D3D11_RASTERIZER_DESC desc = {0};
    desc.FillMode = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    /* direct3d 9's front faces are clockwise, as d3d11's are by default. */
    desc.CullMode = rs[D3DRS_CULLMODE] == D3DCULL_CW    ? D3D11_CULL_FRONT
                    : rs[D3DRS_CULLMODE] == D3DCULL_CCW ? D3D11_CULL_BACK
                                                        : D3D11_CULL_NONE;
    desc.DepthBias = depth_bias(d);
    desc.SlopeScaledDepthBias = fbits(rs[D3DRS_SLOPESCALEDEPTHBIAS]);
    desc.DepthClipEnable = TRUE;
    desc.ScissorEnable = !!rs[D3DRS_SCISSORTESTENABLE];
    desc.MultisampleEnable = !!rs[D3DRS_MULTISAMPLEANTIALIAS];
    desc.AntialiasedLineEnable = !!rs[D3DRS_ANTIALIASEDLINEENABLE];
    return cached(&d->raster_cache, &desc, sizeof(desc), make_raster, d);
}

static ID3D11SamplerState *sampler_state(D9Device *d, UINT slot, BOOL compare)
{
    const DWORD *ss = d->sampler_states[slot];
    D3D11_SAMPLER_DESC desc = {0};
    DWORD min = ss[D3DSAMP_MINFILTER], mag = ss[D3DSAMP_MAGFILTER], mip = ss[D3DSAMP_MIPFILTER];
    BOOL aniso = min == D3DTEXF_ANISOTROPIC || mag == D3DTEXF_ANISOTROPIC;
    UINT filter = (min >= D3DTEXF_LINEAR) << 4 | (mag >= D3DTEXF_LINEAR) << 2 | (mip >= D3DTEXF_LINEAR);
    if (aniso && ss[D3DSAMP_MAXANISOTROPY] > 1)
        filter = 0x55;
    desc.Filter = (D3D11_FILTER)(filter | (compare ? 0x80 : 0));
    for (UINT i = 0; i < 3; i++) {
        DWORD a = ss[D3DSAMP_ADDRESSU + i];
        (&desc.AddressU)[i] = a >= 1 && a <= 5 ? (D3D11_TEXTURE_ADDRESS_MODE)a : D3D11_TEXTURE_ADDRESS_WRAP;
    }
    desc.MipLODBias = fbits(ss[D3DSAMP_MIPMAPLODBIAS]);
    desc.MaxAnisotropy = D9_MAX(D9_MIN(ss[D3DSAMP_MAXANISOTROPY], 16), 1);
    desc.ComparisonFunc = compare ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
    D3DCOLOR border = ss[D3DSAMP_BORDERCOLOR];
    desc.BorderColor[0] = ((border >> 16) & 0xFF) / 255.0f;
    desc.BorderColor[1] = ((border >> 8) & 0xFF) / 255.0f;
    desc.BorderColor[2] = (border & 0xFF) / 255.0f;
    desc.BorderColor[3] = (border >> 24) / 255.0f;
    /* the largest level is the most detailed one sampled; without mips only it. */
    desc.MinLOD = (float)ss[D3DSAMP_MAXMIPLEVEL];
    desc.MaxLOD = mip == D3DTEXF_NONE ? desc.MinLOD : D3D11_FLOAT32_MAX;
    return cached(&d->sampler_cache, &desc, sizeof(desc), make_sampler, d);
}

/* ------------------------------------------------------------------ constants */

static ID3D11Buffer *make_cb(D9Device *d, UINT size)
{
    D3D11_BUFFER_DESC desc = {size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
    ID3D11Buffer *b;
    return SUCCEEDED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &desc, NULL, &b)) ? b : NULL;
}

/* the float, integer and boolean constants of a stage, with the shader's own definitions over them. */
static void upload_constants(D9Device *d, BOOL pixel, const D9ShaderInfo *info, BOOL ff)
{
    ID3D11DeviceContext1 *ctx = d->context;
    ID3D11Buffer **cbs = d->cbs[pixel];
    const float(*f)[4] = ff ? (pixel ? d->ff_ps_consts : d->ff_vs_consts) : pixel ? d->ps_f : d->vs_f;
    const int(*ints)[4] = pixel ? d->ps_i : d->vs_i;
    const BOOL *bools = pixel ? d->ps_b : d->vs_b;
    D3D11_MAPPED_SUBRESOURCE m;
    UINT count = info->relative ? (pixel ? D9_PS_CONSTANTS : D9_VS_CONSTANTS) : D9_MAX(info->float_count, 1);
    if (ff)
        count = D9_MIN(count, pixel ? D9_FF_PS_CONSTANTS : D9_FF_VS_CONSTANTS);
    if (SUCCEEDED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)cbs[0], 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, f, count * 16);
        for (UINT i = 0; i < info->def_count; i++)
            if ((info->defs[i].reg >> 16) == 2 && (info->defs[i].reg & 0xFFFF) < count)
                memcpy((BYTE *)m.pData + (info->defs[i].reg & 0xFFFF) * 16, info->defs[i].v, 16);
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)cbs[0], 0);
    }
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

/* what d3d9.dll's code reads besides the program's constants: the half pixel, clip planes, points, fog, alpha. */
static void upload_internal(D9Device *d, BOOL pixel, BOOL ff)
{
    ID3D11DeviceContext1 *ctx = d->context;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)d->cbs[pixel][2], 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    float(*v)[4] = m.pData;
    memset(v, 0, 16 * 16);
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
    }
    ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)d->cbs[pixel][2], 0);
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
    ID3D11RenderTargetView *rtvs[D9_MAX_TARGETS] = {0};
    UINT count = 0;
    BOOL srgb = !!d->render_states[D3DRS_SRGBWRITEENABLE];
    for (UINT i = 0; i < D9_MAX_TARGETS; i++)
        if (d->targets[i] && (rtvs[i] = D9SurfaceRtv(d->targets[i], srgb)))
            count = i + 1;
    ID3D11DepthStencilView *dsv = d->depth_stencil ? D9SurfaceDsv(d->depth_stencil) : NULL;
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

/* the samplers of a stage and the views of their textures: pixel samplers 0 to 15, vertex 0 to 3 at slots 17 on. */
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
        ID3D11ShaderResourceView *srv = t ? D9TextureSrv(t, d->sampler_states[slot][D3DSAMP_SRGBTEXTURE]) : NULL;
        ID3D11SamplerState *sampler = t ? sampler_state(d, slot, compare) : NULL;
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

/* the keys of the bound shaders under the device's state. */
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
    key->point_sprite = type == D3DPT_POINTLIST && rs[D3DRS_POINTSPRITEENABLE];
}

/* every state a draw reads, set on the context; FALSE when the draw cannot be made. */
static BOOL prepare(D9Device *d, D3DPRIMITIVETYPE type)
{
    ID3D11DeviceContext1 *ctx = d->context;
    if (!d->decl)
        return FALSE;
    /* without shaders, fixed function's programs of the state. */
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
        ctx->lpVtbl->VSSetShader(ctx, (ID3D11VertexShader *)vs->shader, NULL, 0);
        d->bound_vs = vs;
        d->vs_consts_dirty = TRUE;
    }
    if (ps != d->bound_ps) {
        ctx->lpVtbl->PSSetShader(ctx, (ID3D11PixelShader *)ps->shader, NULL, 0);
        d->bound_ps = ps;
        d->ps_consts_dirty = TRUE;
    }
    /* instanced streams, as SetStreamSourceFreq marks them, step once each count of instances. */
    UINT instanced = 0, rates[16] = {0};
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
            ID3D11Buffer *buffer = b ? b->buffer : NULL;
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
        ctx->lpVtbl->IASetIndexBuffer(ctx, b ? b->buffer : NULL,
                                      b && b->base.d3d_format == D3DFMT_INDEX32 ? DXGI_FORMAT_R32_UINT
                                                                                 : DXGI_FORMAT_R16_UINT,
                                      0);
        d->indices_dirty = FALSE;
    }
    if (d->indices)
        d->indices->used = TRUE;
    if (d->targets_dirty || d->bound_srgb != !!d->render_states[D3DRS_SRGBWRITEENABLE])
        bind_targets(d);
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
    if (d->states_dirty || d->internal_dirty || d->ff_dirty || vs_shader->ff != d->bound_vs_ff) {
        upload_internal(d, FALSE, vs_shader->ff);
        upload_internal(d, TRUE, FALSE);
        d->bound_vs_ff = vs_shader->ff;
        d->internal_dirty = FALSE;
        d->ff_dirty = FALSE;
    }
    if (d->states_dirty) {
        D3DCOLOR f = d->render_states[D3DRS_BLENDFACTOR];
        FLOAT factor[4] = {((f >> 16) & 0xFF) / 255.0f, ((f >> 8) & 0xFF) / 255.0f, (f & 0xFF) / 255.0f,
                           (f >> 24) / 255.0f};
        ctx->lpVtbl->OMSetBlendState(ctx, blend_state(d), factor, d->render_states[D3DRS_MULTISAMPLEMASK]);
        ctx->lpVtbl->OMSetDepthStencilState(ctx, depth_state(d), d->render_states[D3DRS_STENCILREF] & 0xFF);
        ctx->lpVtbl->RSSetState(ctx, raster_state(d));
        d->states_dirty = FALSE;
    }
    bind_textures(d, ps, vs);
    return TRUE;
}

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

/* bytes for a draw from memory, in a ring the gpu reads, discarded when it wraps. */
static BOOL ring_write(D9Device *d, D9Ring *ring, UINT bind, const void *data, UINT size, UINT *offset)
{
    ID3D11DeviceContext1 *ctx = d->context;
    if (!ring->buffer || size > ring->size) {
        if (ring->buffer)
            ring->buffer->lpVtbl->Release(ring->buffer);
        ring->size = D9_MAX(D9_ALIGN(size, 65536), 4u << 20);
        D3D11_BUFFER_DESC desc = {ring->size, D3D11_USAGE_DYNAMIC, bind, D3D11_CPU_ACCESS_WRITE};
        if (FAILED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &desc, NULL, &ring->buffer))) {
            ring->buffer = NULL;
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

/* draws from memory leave stream 0, and the indices of indexed ones, unbound, as direct3d 9 does. */
static void unbind_up(D9Device *d, BOOL indexed)
{
    D9DeviceSetStream(d, 0, NULL, 0, 0);
    if (indexed)
        D9DeviceSetIndices(d, NULL);
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
    /* the vertices from the lowest index on, the indices made relative to it. */
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

/* the d3d11 objects draws use, made with the device. */
HRESULT D9InitDraws(D9Device *d)
{
    for (UINT p = 0; p < 2; p++) {
        d->cbs[p][0] = make_cb(d, (p ? D9_PS_CONSTANTS : D9_VS_CONSTANTS) * 16);
        d->cbs[p][1] = make_cb(d, 20 * 16);
        d->cbs[p][2] = make_cb(d, 16 * 16);
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

/* what the context has bound is forgotten: the next draw sets it all. */
void D9ResetBindings(D9Device *d)
{
    d->bound_vs = d->bound_ps = NULL;
    d->bound_layout = NULL;
    d->bound_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    memset(d->bound_srvs, 0, sizeof(d->bound_srvs));
    memset(d->bound_samplers, 0, sizeof(d->bound_samplers));
    d->streams_dirty = ~0u;
    d->indices_dirty = TRUE;
    d->targets_dirty = TRUE;
    d->viewport_dirty = TRUE;
    d->states_dirty = TRUE;
    d->internal_dirty = TRUE;
    d->vs_consts_dirty = d->ps_consts_dirty = TRUE;
}

void D9FreeDraws(D9Device *d)
{
    for (UINT p = 0; p < 2; p++)
        for (UINT i = 0; i < 3; i++)
            if (d->cbs[p][i])
                d->cbs[p][i]->lpVtbl->Release(d->cbs[p][i]);
    if (d->zeros)
        d->zeros->lpVtbl->Release(d->zeros);
    if (d->up_vertices.buffer)
        d->up_vertices.buffer->lpVtbl->Release(d->up_vertices.buffer);
    if (d->up_indices.buffer)
        d->up_indices.buffer->lpVtbl->Release(d->up_indices.buffer);
    D9ClearStateCache(&d->blend_cache);
    D9ClearStateCache(&d->depth_cache);
    D9ClearStateCache(&d->raster_cache);
    D9ClearStateCache(&d->sampler_cache);
    D9FreeFFShaders(d);
}
