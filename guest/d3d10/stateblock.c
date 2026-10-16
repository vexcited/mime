/*
 * state blocks: a mask of the device's states and slots, captured through
 * the device's getters and applied through its setters, each run of slots
 * the mask enables at once, in the order of the kinds of state.
 */
#include "d3d10_private.h"

/* each kind of state: how many slots, and the byte of the mask their bits start at. */
static const struct {
    BYTE count;
    BYTE offset;
} STATES[] = {
    [D3D10_DST_SO_BUFFERS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, SOBuffers)},
    [D3D10_DST_OM_RENDER_TARGETS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, OMRenderTargets)},
    [D3D10_DST_OM_DEPTH_STENCIL_STATE] = {1, offsetof(D3D10_STATE_BLOCK_MASK, OMDepthStencilState)},
    [D3D10_DST_OM_BLEND_STATE] = {1, offsetof(D3D10_STATE_BLOCK_MASK, OMBlendState)},
    [D3D10_DST_VS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, VS)},
    [D3D10_DST_VS_SAMPLERS] = {16, offsetof(D3D10_STATE_BLOCK_MASK, VSSamplers)},
    [D3D10_DST_VS_SHADER_RESOURCES] = {128, offsetof(D3D10_STATE_BLOCK_MASK, VSShaderResources)},
    [D3D10_DST_VS_CONSTANT_BUFFERS] = {14, offsetof(D3D10_STATE_BLOCK_MASK, VSConstantBuffers)},
    [D3D10_DST_GS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, GS)},
    [D3D10_DST_GS_SAMPLERS] = {16, offsetof(D3D10_STATE_BLOCK_MASK, GSSamplers)},
    [D3D10_DST_GS_SHADER_RESOURCES] = {128, offsetof(D3D10_STATE_BLOCK_MASK, GSShaderResources)},
    [D3D10_DST_GS_CONSTANT_BUFFERS] = {14, offsetof(D3D10_STATE_BLOCK_MASK, GSConstantBuffers)},
    [D3D10_DST_PS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, PS)},
    [D3D10_DST_PS_SAMPLERS] = {16, offsetof(D3D10_STATE_BLOCK_MASK, PSSamplers)},
    [D3D10_DST_PS_SHADER_RESOURCES] = {128, offsetof(D3D10_STATE_BLOCK_MASK, PSShaderResources)},
    [D3D10_DST_PS_CONSTANT_BUFFERS] = {14, offsetof(D3D10_STATE_BLOCK_MASK, PSConstantBuffers)},
    [D3D10_DST_IA_VERTEX_BUFFERS] = {16, offsetof(D3D10_STATE_BLOCK_MASK, IAVertexBuffers)},
    [D3D10_DST_IA_INDEX_BUFFER] = {1, offsetof(D3D10_STATE_BLOCK_MASK, IAIndexBuffer)},
    [D3D10_DST_IA_INPUT_LAYOUT] = {1, offsetof(D3D10_STATE_BLOCK_MASK, IAInputLayout)},
    [D3D10_DST_IA_PRIMITIVE_TOPOLOGY] = {1, offsetof(D3D10_STATE_BLOCK_MASK, IAPrimitiveTopology)},
    [D3D10_DST_RS_VIEWPORTS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, RSViewports)},
    [D3D10_DST_RS_SCISSOR_RECTS] = {1, offsetof(D3D10_STATE_BLOCK_MASK, RSScissorRects)},
    [D3D10_DST_RS_RASTERIZER_STATE] = {1, offsetof(D3D10_STATE_BLOCK_MASK, RSRasterizerState)},
    [D3D10_DST_PREDICATION] = {1, offsetof(D3D10_STATE_BLOCK_MASK, Predication)},
};

#define STATE_KINDS ((UINT)RTL_NUMBER_OF(STATES))

/* ------------------------------------------------------------------ masks */

D3D10API HRESULT WINAPI D3D10StateBlockMaskUnion(D3D10_STATE_BLOCK_MASK *a, D3D10_STATE_BLOCK_MASK *b,
                                                 D3D10_STATE_BLOCK_MASK *result)
{
    if (!a || !b || !result)
        return E_INVALIDARG;
    for (UINT i = 0; i < sizeof(*result); i++)
        ((BYTE *)result)[i] = ((BYTE *)a)[i] | ((BYTE *)b)[i];
    return S_OK;
}

D3D10API HRESULT WINAPI D3D10StateBlockMaskIntersect(D3D10_STATE_BLOCK_MASK *a, D3D10_STATE_BLOCK_MASK *b,
                                                     D3D10_STATE_BLOCK_MASK *result)
{
    if (!a || !b || !result)
        return E_INVALIDARG;
    for (UINT i = 0; i < sizeof(*result); i++)
        ((BYTE *)result)[i] = ((BYTE *)a)[i] & ((BYTE *)b)[i];
    return S_OK;
}

/* what is in one mask and not both. */
D3D10API HRESULT WINAPI D3D10StateBlockMaskDifference(D3D10_STATE_BLOCK_MASK *a, D3D10_STATE_BLOCK_MASK *b,
                                                      D3D10_STATE_BLOCK_MASK *result)
{
    if (!a || !b || !result)
        return E_INVALIDARG;
    for (UINT i = 0; i < sizeof(*result); i++)
        ((BYTE *)result)[i] = ((BYTE *)a)[i] ^ ((BYTE *)b)[i];
    return S_OK;
}

D3D10API HRESULT WINAPI D3D10StateBlockMaskEnableAll(D3D10_STATE_BLOCK_MASK *mask)
{
    if (!mask)
        return E_INVALIDARG;
    memset(mask, 0xFF, sizeof(*mask));
    return S_OK;
}

D3D10API HRESULT WINAPI D3D10StateBlockMaskDisableAll(D3D10_STATE_BLOCK_MASK *mask)
{
    if (!mask)
        return E_INVALIDARG;
    memset(mask, 0, sizeof(*mask));
    return S_OK;
}

/* a range of slots of a kind, empty or past its slots refused. */
static HRESULT set_capture(D3D10_STATE_BLOCK_MASK *mask, D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count,
                           BOOL enable)
{
    if ((UINT)type - 1 >= STATE_KINDS - 1 || !mask || start + count <= start || start + count > STATES[type].count)
        return E_INVALIDARG;
    BYTE *bits = (BYTE *)mask + STATES[type].offset;
    for (UINT i = start; i < start + count; i++) {
        if (enable)
            bits[i / 8] |= 1 << (i % 8);
        else
            bits[i / 8] &= ~(1 << (i % 8));
    }
    return S_OK;
}

D3D10API HRESULT WINAPI D3D10StateBlockMaskEnableCapture(D3D10_STATE_BLOCK_MASK *mask, D3D10_DEVICE_STATE_TYPES type,
                                                         UINT start, UINT count)
{
    return set_capture(mask, type, start, count, TRUE);
}

D3D10API HRESULT WINAPI D3D10StateBlockMaskDisableCapture(D3D10_STATE_BLOCK_MASK *mask,
                                                          D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count)
{
    return set_capture(mask, type, start, count, FALSE);
}

/* the slot's bit as it is in its byte, not 1: windows returns it so. */
D3D10API BOOL WINAPI D3D10StateBlockMaskGetSetting(D3D10_STATE_BLOCK_MASK *mask, D3D10_DEVICE_STATE_TYPES type,
                                                   UINT entry)
{
    if ((UINT)type - 1 >= STATE_KINDS - 1 || !mask || entry >= STATES[type].count)
        return FALSE;
    return ((BYTE *)mask)[STATES[type].offset + entry / 8] & (1 << (entry % 8));
}

/* ------------------------------------------------------------------ state blocks */

typedef struct StageState {
    ID3D10Buffer *cb[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    ID3D10ShaderResourceView *srv[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D10SamplerState *sampler[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
} StageState;

typedef struct StateBlock {
    ID3D10StateBlock iface;
    LONG refs;
    ID3D10Device *device;
    D3D10_STATE_BLOCK_MASK mask;
    BOOL captured;
    /* what was captured: the vertex, geometry and pixel shader stages, then the rest. */
    ID3D10DeviceChild *shader[3];
    StageState stage[3];
    ID3D10Buffer *vb[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT vb_stride[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT], vb_offset[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    ID3D10Buffer *ib;
    DXGI_FORMAT ib_format;
    UINT ib_offset;
    ID3D10InputLayout *layout;
    D3D10_PRIMITIVE_TOPOLOGY topology;
    ID3D10RenderTargetView *rtv[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D10DepthStencilView *dsv;
    ID3D10DepthStencilState *depth_stencil;
    UINT stencil_ref;
    ID3D10BlendState *blend;
    FLOAT blend_factor[4];
    UINT sample_mask;
    D3D10_VIEWPORT viewports[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT viewport_count;
    D3D10_RECT scissors[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT scissor_count;
    ID3D10RasterizerState *rasterizer;
    ID3D10Buffer *so[D3D10_SO_BUFFER_SLOT_COUNT];
    UINT so_offset[D3D10_SO_BUFFER_SLOT_COUNT];
    ID3D10Predicate *predicate;
    BOOL predicate_value;
} StateBlock;

static StateBlock *impl(ID3D10StateBlock *iface) { return CONTAINING_RECORD(iface, StateBlock, iface); }

/* the kind of state a stage's kinds start at, and the stage of a kind. */
static const D3D10_DEVICE_STATE_TYPES STAGE_KINDS[3] = {D3D10_DST_VS, D3D10_DST_GS, D3D10_DST_PS};

static void release_range(void *objects, UINT start, UINT count)
{
    IUnknown **list = objects;
    for (UINT i = start; i < start + count; i++)
        if (list[i]) {
            list[i]->lpVtbl->Release(list[i]);
            list[i] = nullptr;
        }
}

/* every object the block holds, all ranges of all kinds. */
static void release_all(StateBlock *sb)
{
    release_range(sb->shader, 0, 3);
    for (UINT s = 0; s < 3; s++) {
        release_range(sb->stage[s].cb, 0, RTL_NUMBER_OF(sb->stage[s].cb));
        release_range(sb->stage[s].srv, 0, RTL_NUMBER_OF(sb->stage[s].srv));
        release_range(sb->stage[s].sampler, 0, RTL_NUMBER_OF(sb->stage[s].sampler));
    }
    release_range(sb->vb, 0, RTL_NUMBER_OF(sb->vb));
    release_range(&sb->ib, 0, 1);
    release_range(&sb->layout, 0, 1);
    release_range(sb->rtv, 0, RTL_NUMBER_OF(sb->rtv));
    release_range(&sb->dsv, 0, 1);
    release_range(&sb->depth_stencil, 0, 1);
    release_range(&sb->blend, 0, 1);
    release_range(&sb->rasterizer, 0, 1);
    release_range(sb->so, 0, RTL_NUMBER_OF(sb->so));
    release_range(&sb->predicate, 0, 1);
}

static void capture(StateBlock *sb, D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count)
{
    ID3D10Device *d = sb->device;
    for (UINT s = 0; s < 3; s++) {
        StageState *st = &sb->stage[s];
        if (type == STAGE_KINDS[s]) {
            if (s == 0)
                d->lpVtbl->VSGetShader(d, (ID3D10VertexShader **)&sb->shader[0]);
            else if (s == 1)
                d->lpVtbl->GSGetShader(d, (ID3D10GeometryShader **)&sb->shader[1]);
            else
                d->lpVtbl->PSGetShader(d, (ID3D10PixelShader **)&sb->shader[2]);
        } else if (type == STAGE_KINDS[s] + 1) {
            (s == 0   ? d->lpVtbl->VSGetSamplers
             : s == 1 ? d->lpVtbl->GSGetSamplers
                      : d->lpVtbl->PSGetSamplers)(d, start, count, st->sampler + start);
        } else if (type == STAGE_KINDS[s] + 2) {
            (s == 0   ? d->lpVtbl->VSGetShaderResources
             : s == 1 ? d->lpVtbl->GSGetShaderResources
                      : d->lpVtbl->PSGetShaderResources)(d, start, count, st->srv + start);
        } else if (type == STAGE_KINDS[s] + 3) {
            (s == 0   ? d->lpVtbl->VSGetConstantBuffers
             : s == 1 ? d->lpVtbl->GSGetConstantBuffers
                      : d->lpVtbl->PSGetConstantBuffers)(d, start, count, st->cb + start);
        }
    }
    switch (type) {
    case D3D10_DST_SO_BUFFERS:
        d->lpVtbl->SOGetTargets(d, D3D10_SO_BUFFER_SLOT_COUNT, sb->so, sb->so_offset);
        break;
    case D3D10_DST_OM_RENDER_TARGETS:
        d->lpVtbl->OMGetRenderTargets(d, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, sb->rtv, &sb->dsv);
        break;
    case D3D10_DST_OM_DEPTH_STENCIL_STATE:
        d->lpVtbl->OMGetDepthStencilState(d, &sb->depth_stencil, &sb->stencil_ref);
        break;
    case D3D10_DST_OM_BLEND_STATE:
        d->lpVtbl->OMGetBlendState(d, &sb->blend, sb->blend_factor, &sb->sample_mask);
        break;
    case D3D10_DST_IA_VERTEX_BUFFERS:
        d->lpVtbl->IAGetVertexBuffers(d, start, count, sb->vb + start, sb->vb_stride + start, sb->vb_offset + start);
        break;
    case D3D10_DST_IA_INDEX_BUFFER:
        d->lpVtbl->IAGetIndexBuffer(d, &sb->ib, &sb->ib_format, &sb->ib_offset);
        break;
    case D3D10_DST_IA_INPUT_LAYOUT:
        d->lpVtbl->IAGetInputLayout(d, &sb->layout);
        break;
    case D3D10_DST_IA_PRIMITIVE_TOPOLOGY:
        d->lpVtbl->IAGetPrimitiveTopology(d, &sb->topology);
        break;
    case D3D10_DST_RS_VIEWPORTS:
        sb->viewport_count = RTL_NUMBER_OF(sb->viewports);
        d->lpVtbl->RSGetViewports(d, &sb->viewport_count, sb->viewports);
        break;
    case D3D10_DST_RS_SCISSOR_RECTS:
        sb->scissor_count = RTL_NUMBER_OF(sb->scissors);
        d->lpVtbl->RSGetScissorRects(d, &sb->scissor_count, sb->scissors);
        break;
    case D3D10_DST_RS_RASTERIZER_STATE:
        d->lpVtbl->RSGetState(d, &sb->rasterizer);
        break;
    case D3D10_DST_PREDICATION:
        d->lpVtbl->GetPredication(d, &sb->predicate, &sb->predicate_value);
        break;
    default:
        break;
    }
}

static void apply(StateBlock *sb, D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count)
{
    ID3D10Device *d = sb->device;
    for (UINT s = 0; s < 3; s++) {
        StageState *st = &sb->stage[s];
        if (type == STAGE_KINDS[s]) {
            if (s == 0)
                d->lpVtbl->VSSetShader(d, (ID3D10VertexShader *)sb->shader[0]);
            else if (s == 1)
                d->lpVtbl->GSSetShader(d, (ID3D10GeometryShader *)sb->shader[1]);
            else
                d->lpVtbl->PSSetShader(d, (ID3D10PixelShader *)sb->shader[2]);
        } else if (type == STAGE_KINDS[s] + 1) {
            (s == 0   ? d->lpVtbl->VSSetSamplers
             : s == 1 ? d->lpVtbl->GSSetSamplers
                      : d->lpVtbl->PSSetSamplers)(d, start, count, st->sampler + start);
        } else if (type == STAGE_KINDS[s] + 2) {
            (s == 0   ? d->lpVtbl->VSSetShaderResources
             : s == 1 ? d->lpVtbl->GSSetShaderResources
                      : d->lpVtbl->PSSetShaderResources)(d, start, count, st->srv + start);
        } else if (type == STAGE_KINDS[s] + 3) {
            (s == 0   ? d->lpVtbl->VSSetConstantBuffers
             : s == 1 ? d->lpVtbl->GSSetConstantBuffers
                      : d->lpVtbl->PSSetConstantBuffers)(d, start, count, st->cb + start);
        }
    }
    switch (type) {
    case D3D10_DST_SO_BUFFERS:
        d->lpVtbl->SOSetTargets(d, D3D10_SO_BUFFER_SLOT_COUNT, sb->so, sb->so_offset);
        break;
    case D3D10_DST_OM_RENDER_TARGETS:
        d->lpVtbl->OMSetRenderTargets(d, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, sb->rtv, sb->dsv);
        break;
    case D3D10_DST_OM_DEPTH_STENCIL_STATE:
        d->lpVtbl->OMSetDepthStencilState(d, sb->depth_stencil, sb->stencil_ref);
        break;
    case D3D10_DST_OM_BLEND_STATE:
        d->lpVtbl->OMSetBlendState(d, sb->blend, sb->blend_factor, sb->sample_mask);
        break;
    case D3D10_DST_IA_VERTEX_BUFFERS:
        d->lpVtbl->IASetVertexBuffers(d, start, count, sb->vb + start, sb->vb_stride + start, sb->vb_offset + start);
        break;
    case D3D10_DST_IA_INDEX_BUFFER:
        d->lpVtbl->IASetIndexBuffer(d, sb->ib, sb->ib_format, sb->ib_offset);
        break;
    case D3D10_DST_IA_INPUT_LAYOUT:
        d->lpVtbl->IASetInputLayout(d, sb->layout);
        break;
    case D3D10_DST_IA_PRIMITIVE_TOPOLOGY:
        d->lpVtbl->IASetPrimitiveTopology(d, sb->topology);
        break;
    case D3D10_DST_RS_VIEWPORTS:
        d->lpVtbl->RSSetViewports(d, sb->viewport_count, sb->viewports);
        break;
    case D3D10_DST_RS_SCISSOR_RECTS:
        d->lpVtbl->RSSetScissorRects(d, sb->scissor_count, sb->scissors);
        break;
    case D3D10_DST_RS_RASTERIZER_STATE:
        d->lpVtbl->RSSetState(d, sb->rasterizer);
        break;
    case D3D10_DST_PREDICATION:
        d->lpVtbl->SetPredication(d, sb->predicate, sb->predicate_value);
        break;
    default:
        break;
    }
}

/* calls the function for each run of slots the mask enables, kind after kind. */
static void for_each_run(StateBlock *sb, void (*fn)(StateBlock *, D3D10_DEVICE_STATE_TYPES, UINT, UINT))
{
    const BYTE *mask = (const BYTE *)&sb->mask;
    for (UINT type = 1; type < STATE_KINDS; type++) {
        const BYTE *bits = mask + STATES[type].offset;
        UINT start = 0;
        BOOL in_run = FALSE;
        for (UINT i = 0; i <= STATES[type].count; i++) {
            BOOL set = i < STATES[type].count && (bits[i / 8] & (1 << (i % 8)));
            if (set && !in_run) {
                start = i;
                in_run = TRUE;
            } else if (!set && in_run) {
                fn(sb, (D3D10_DEVICE_STATE_TYPES)type, start, i - start);
                in_run = FALSE;
            }
        }
    }
}

/*
 * d3d10.dll answers the iid of ID3D10EffectPool and nothing else, neither
 * IUnknown nor ID3D10StateBlock, and so does this.
 */
static HRESULT STDMETHODCALLTYPE sb_query(ID3D10StateBlock *iface, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    if (!IsEqualGUID(riid, &IID_ID3D10EffectPool)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    iface->lpVtbl->AddRef(iface);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE sb_add_ref(ID3D10StateBlock *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE sb_release(ID3D10StateBlock *iface)
{
    StateBlock *sb = impl(iface);
    ULONG refs = InterlockedDecrement(&sb->refs);
    if (!refs) {
        release_all(sb);
        sb->device->lpVtbl->Release(sb->device);
        HeapFree(GetProcessHeap(), 0, sb);
    }
    return refs;
}

/* what an earlier capture holds is let go of first. */
static HRESULT STDMETHODCALLTYPE sb_capture(ID3D10StateBlock *iface)
{
    StateBlock *sb = impl(iface);
    if (sb->captured)
        release_all(sb);
    for_each_run(sb, capture);
    sb->captured = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sb_apply(ID3D10StateBlock *iface)
{
    StateBlock *sb = impl(iface);
    if (!sb->captured)
        return E_FAIL;
    for_each_run(sb, apply);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sb_release_all_device_objects(ID3D10StateBlock *iface)
{
    StateBlock *sb = impl(iface);
    if (sb->captured) {
        release_all(sb);
        sb->captured = FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sb_get_device(ID3D10StateBlock *iface, ID3D10Device **device)
{
    StateBlock *sb = impl(iface);
    if (!device)
        return E_INVALIDARG;
    sb->device->lpVtbl->AddRef(sb->device);
    *device = sb->device;
    return S_OK;
}

static const ID3D10StateBlockVtbl sb_vtbl = {
    sb_query, sb_add_ref, sb_release, sb_capture, sb_apply, sb_release_all_device_objects, sb_get_device,
};

/* a block of the states the mask enables, holding the device. */
D3D10API HRESULT WINAPI D3D10CreateStateBlock(ID3D10Device *device, D3D10_STATE_BLOCK_MASK *mask,
                                              ID3D10StateBlock **block)
{
    if (!device || !block)
        return E_INVALIDARG;
    StateBlock *sb = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sb));
    if (!sb)
        return E_OUTOFMEMORY;
    sb->iface.lpVtbl = &sb_vtbl;
    sb->refs = 1;
    sb->device = device;
    device->lpVtbl->AddRef(device);
    if (mask)
        sb->mask = *mask;
    *block = &sb->iface;
    return S_OK;
}
