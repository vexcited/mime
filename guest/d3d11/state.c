/*
 * state objects: blend, rasterizer, depth stencil and sampler states, one object per distinct description like
 * direct3d, and input layouts, matched to the vertex shader's inputs by semantic. states are only descriptions,
 * draws set them as vulkan dynamic state.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))

/* common header of every state object. */
typedef struct D3dObject {
    void *iface;
    D3dChild child;
    struct D3dObject *next;
} D3dObject;

static D3dObject *object(void *iface) { return CONTAINING_RECORD(iface, D3dObject, iface); }

static void STDMETHODCALLTYPE object_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = object(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE object_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&object(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE object_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&object(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE object_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&object(iface)->child.data, guid, data);
}

static ULONG STDMETHODCALLTYPE object_add_ref(void *iface) { return InterlockedIncrement(&object(iface)->child.refs); }

/* iids lists the object's own interfaces. d3d10 is returned for any other iid when not NULL. */
static HRESULT query(void *iface, const GUID *const *iids, UINT count, void *d3d10, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    BOOL match = IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild);
    for (UINT i = 0; i < count && !match; i++)
        match = IsEqualGUID(riid, iids[i]);
    if (!match && !d3d10) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    object_add_ref(iface);
    *out = match ? iface : d3d10;
    return S_OK;
}

/* unlinks the state at the last reference, under the device's state lock. */
static ULONG release_cached(void *iface, D3dObject **list, void (*destroy)(D3dObject *))
{
    D3dObject *o = object(iface);
    D3dDevice *device = o->child.device;
    EnterCriticalSection(&device->state_lock);
    ULONG refs = InterlockedDecrement(&o->child.refs);
    if (!refs)
        for (D3dObject **link = list; *link; link = &(*link)->next)
            if (*link == o) {
                *link = o->next;
                break;
            }
    LeaveCriticalSection(&device->state_lock);
    if (!refs) {
        if (destroy)
            destroy(o);
        D3dChildRelease(&o->child);
        HeapFree(GetProcessHeap(), 0, o);
    }
    return refs;
}

/* returns a referenced match, or NULL. */
static D3dObject *find_cached(D3dDevice *device, D3dObject *list, SIZE_T desc_offset, const void *desc, SIZE_T size)
{
    for (D3dObject *o = list; o; o = o->next)
        if (!memcmp((BYTE *)o + desc_offset, desc, size)) {
            object_add_ref(&o->iface);
            return o;
        }
    return nullptr;
}

/* ------------------------------------------------------------------ blend */

static const GUID *const BLEND_IIDS[] = {&IID_ID3D11BlendState, &IID_ID3D11BlendState1};

static HRESULT STDMETHODCALLTYPE blend_query(void *iface, REFIID riid, void **out)
{
    return query(iface, BLEND_IIDS, 2, D3d10BlendInterface(CONTAINING_RECORD(iface, D3dBlendState, iface), riid), riid,
                 out);
}

static ULONG STDMETHODCALLTYPE blend_release(void *iface)
{
    return release_cached(iface, (D3dObject **)&object(iface)->child.device->blend_states, nullptr);
}

static void STDMETHODCALLTYPE blend_get_desc1(void *iface, D3D11_BLEND_DESC1 *desc)
{
    *desc = CONTAINING_RECORD(iface, D3dBlendState, iface)->desc;
}

static void STDMETHODCALLTYPE blend_get_desc(void *iface, D3D11_BLEND_DESC *desc)
{
    const D3D11_BLEND_DESC1 *d = &CONTAINING_RECORD(iface, D3dBlendState, iface)->desc;
    desc->AlphaToCoverageEnable = d->AlphaToCoverageEnable;
    desc->IndependentBlendEnable = d->IndependentBlendEnable;
    for (UINT i = 0; i < 8; i++) {
        const D3D11_RENDER_TARGET_BLEND_DESC1 *s = &d->RenderTarget[i];
        desc->RenderTarget[i] = (D3D11_RENDER_TARGET_BLEND_DESC){
            s->BlendEnable,    s->SrcBlend,       s->DestBlend,    s->BlendOp,
            s->SrcBlendAlpha, s->DestBlendAlpha, s->BlendOpAlpha, s->RenderTargetWriteMask};
    }
}

#define OBJECT_METHODS(prefix)                                                                                         \
    VTBL_FN(prefix##_query), VTBL_FN(object_add_ref), VTBL_FN(prefix##_release), VTBL_FN(object_get_device),         \
        VTBL_FN(object_get_private_data), VTBL_FN(object_set_private_data), VTBL_FN(object_set_private_data_interface)

static const ID3D11BlendState1Vtbl blend_vtbl = {OBJECT_METHODS(blend), VTBL_FN(blend_get_desc),
                                                 VTBL_FN(blend_get_desc1)};

D3dBlendState *D3dBlendFrom(ID3D11BlendState *iface)
{
    return iface ? CONTAINING_RECORD((void *)iface, D3dBlendState, iface) : nullptr;
}

static BOOL valid_blend(D3D11_BLEND b) { return b >= D3D11_BLEND_ZERO && b <= D3D11_BLEND_INV_SRC1_ALPHA && b != 12 && b != 13; }

HRESULT D3dCreateBlendState(D3dDevice *device, const D3D11_BLEND_DESC1 *desc, D3dBlendState **out)
{
    D3D11_BLEND_DESC1 d;
    memset(&d, 0, sizeof(d));
    d.AlphaToCoverageEnable = !!desc->AlphaToCoverageEnable;
    d.IndependentBlendEnable = !!desc->IndependentBlendEnable;
    for (UINT i = 0; i < 8; i++) {
        /* without independent blending the first target's state applies to all. */
        const D3D11_RENDER_TARGET_BLEND_DESC1 *s = &desc->RenderTarget[d.IndependentBlendEnable ? i : 0];
        D3D11_RENDER_TARGET_BLEND_DESC1 *t = &d.RenderTarget[i];
        t->BlendEnable = !!s->BlendEnable;
        t->LogicOpEnable = !!s->LogicOpEnable;
        t->RenderTargetWriteMask = s->RenderTargetWriteMask;
        if (t->BlendEnable) {
            if (!valid_blend(s->SrcBlend) || !valid_blend(s->DestBlend) || !valid_blend(s->SrcBlendAlpha) ||
                !valid_blend(s->DestBlendAlpha) || s->BlendOp < 1 || s->BlendOp > 5 || s->BlendOpAlpha < 1 ||
                s->BlendOpAlpha > 5)
                return E_INVALIDARG;
            t->SrcBlend = s->SrcBlend, t->DestBlend = s->DestBlend, t->BlendOp = s->BlendOp;
            t->SrcBlendAlpha = s->SrcBlendAlpha, t->DestBlendAlpha = s->DestBlendAlpha;
            t->BlendOpAlpha = s->BlendOpAlpha;
        } else {
            t->SrcBlend = t->SrcBlendAlpha = D3D11_BLEND_ONE;
            t->DestBlend = t->DestBlendAlpha = D3D11_BLEND_ZERO;
            t->BlendOp = t->BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        t->LogicOp = t->LogicOpEnable ? s->LogicOp : D3D11_LOGIC_OP_NOOP;
        if (t->LogicOpEnable && (t->BlendEnable || !device->has_logic_op))
            return E_INVALIDARG;
    }
    if (!out)
        return S_FALSE;
    EnterCriticalSection(&device->state_lock);
    D3dObject *found =
        find_cached(device, (D3dObject *)device->blend_states, offsetof(D3dBlendState, desc), &d, sizeof(d));
    if (!found) {
        D3dBlendState *state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (state) {
            state->iface.lpVtbl = &blend_vtbl;
            D3dChildInit(&state->child, device);
            state->desc = d;
            state->next = device->blend_states;
            device->blend_states = state;
            found = (D3dObject *)state;
        }
    }
    LeaveCriticalSection(&device->state_lock);
    if (!found)
        return E_OUTOFMEMORY;
    *out = (D3dBlendState *)found;
    return S_OK;
}

/* ------------------------------------------------------------------ rasterizer */

static const GUID *const RS_IIDS[] = {&IID_ID3D11RasterizerState, &IID_ID3D11RasterizerState1,
                                      &IID_ID3D11RasterizerState2};

static HRESULT STDMETHODCALLTYPE rs_query(void *iface, REFIID riid, void **out)
{
    return query(iface, RS_IIDS, 3, D3d10RasterizerInterface(CONTAINING_RECORD(iface, D3dRasterizerState, iface), riid),
                 riid, out);
}

static ULONG STDMETHODCALLTYPE rs_release(void *iface)
{
    return release_cached(iface, (D3dObject **)&object(iface)->child.device->rasterizer_states, nullptr);
}

static void STDMETHODCALLTYPE rs_get_desc2(void *iface, D3D11_RASTERIZER_DESC2 *desc)
{
    *desc = CONTAINING_RECORD(iface, D3dRasterizerState, iface)->desc;
}

static void STDMETHODCALLTYPE rs_get_desc1(void *iface, D3D11_RASTERIZER_DESC1 *desc)
{
    memcpy(desc, &CONTAINING_RECORD(iface, D3dRasterizerState, iface)->desc, sizeof(*desc));
}

static void STDMETHODCALLTYPE rs_get_desc(void *iface, D3D11_RASTERIZER_DESC *desc)
{
    memcpy(desc, &CONTAINING_RECORD(iface, D3dRasterizerState, iface)->desc, sizeof(*desc));
}

static const ID3D11RasterizerState2Vtbl rs_vtbl = {OBJECT_METHODS(rs), VTBL_FN(rs_get_desc), VTBL_FN(rs_get_desc1),
                                                   VTBL_FN(rs_get_desc2)};

D3dRasterizerState *D3dRasterizerFrom(ID3D11RasterizerState *iface)
{
    return iface ? CONTAINING_RECORD((void *)iface, D3dRasterizerState, iface) : nullptr;
}

HRESULT D3dCreateRasterizerState(D3dDevice *device, const D3D11_RASTERIZER_DESC2 *desc, D3dRasterizerState **out)
{
    D3D11_RASTERIZER_DESC2 d = *desc;
    if (d.FillMode < D3D11_FILL_WIREFRAME || d.FillMode > D3D11_FILL_SOLID || d.CullMode < D3D11_CULL_NONE ||
        d.CullMode > D3D11_CULL_BACK)
        return E_INVALIDARG;
    d.FrontCounterClockwise = !!d.FrontCounterClockwise;
    d.DepthClipEnable = !!d.DepthClipEnable;
    d.ScissorEnable = !!d.ScissorEnable;
    d.MultisampleEnable = !!d.MultisampleEnable;
    d.AntialiasedLineEnable = !!d.AntialiasedLineEnable;
    if (d.ConservativeRaster)
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;
    EnterCriticalSection(&device->state_lock);
    D3dObject *found =
        find_cached(device, (D3dObject *)device->rasterizer_states, offsetof(D3dRasterizerState, desc), &d, sizeof(d));
    if (!found) {
        D3dRasterizerState *state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (state) {
            state->iface.lpVtbl = &rs_vtbl;
            D3dChildInit(&state->child, device);
            state->desc = d;
            state->next = device->rasterizer_states;
            device->rasterizer_states = state;
            found = (D3dObject *)state;
        }
    }
    LeaveCriticalSection(&device->state_lock);
    if (!found)
        return E_OUTOFMEMORY;
    *out = (D3dRasterizerState *)found;
    return S_OK;
}

/* ------------------------------------------------------------------ depth stencil */

static const GUID *const DS_IIDS[] = {&IID_ID3D11DepthStencilState};

static HRESULT STDMETHODCALLTYPE ds_query(void *iface, REFIID riid, void **out)
{
    return query(iface, DS_IIDS, 1,
                 D3d10DepthStencilInterface(CONTAINING_RECORD(iface, D3dDepthStencilState, iface), riid), riid, out);
}

static ULONG STDMETHODCALLTYPE ds_release(void *iface)
{
    return release_cached(iface, (D3dObject **)&object(iface)->child.device->depth_stencil_states, nullptr);
}

static void STDMETHODCALLTYPE ds_get_desc(void *iface, D3D11_DEPTH_STENCIL_DESC *desc)
{
    *desc = CONTAINING_RECORD(iface, D3dDepthStencilState, iface)->desc;
}

static const ID3D11DepthStencilStateVtbl ds_vtbl = {OBJECT_METHODS(ds), VTBL_FN(ds_get_desc)};

D3dDepthStencilState *D3dDepthStencilFrom(ID3D11DepthStencilState *iface)
{
    return iface ? CONTAINING_RECORD((void *)iface, D3dDepthStencilState, iface) : nullptr;
}

static BOOL valid_stencil_op(const D3D11_DEPTH_STENCILOP_DESC *op)
{
    return op->StencilFailOp >= 1 && op->StencilFailOp <= 8 && op->StencilDepthFailOp >= 1 &&
           op->StencilDepthFailOp <= 8 && op->StencilPassOp >= 1 && op->StencilPassOp <= 8 && op->StencilFunc >= 1 &&
           op->StencilFunc <= 8;
}

HRESULT D3dCreateDepthStencilState(D3dDevice *device, const D3D11_DEPTH_STENCIL_DESC *desc,
                                   D3dDepthStencilState **out)
{
    D3D11_DEPTH_STENCIL_DESC d = *desc;
    if (d.DepthWriteMask > D3D11_DEPTH_WRITE_MASK_ALL || (d.DepthEnable && (d.DepthFunc < 1 || d.DepthFunc > 8)))
        return E_INVALIDARG;
    if (d.StencilEnable && (!valid_stencil_op(&d.FrontFace) || !valid_stencil_op(&d.BackFace)))
        return E_INVALIDARG;
    d.DepthEnable = !!d.DepthEnable;
    d.StencilEnable = !!d.StencilEnable;
    /* settings of disabled tests do not make states distinct. */
    if (!d.DepthEnable) {
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = D3D11_COMPARISON_LESS;
    }
    if (!d.StencilEnable) {
        d.StencilReadMask = d.StencilWriteMask = 0xFF;
        d.FrontFace = d.BackFace = (D3D11_DEPTH_STENCILOP_DESC){D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                                                D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    }
    if (!out)
        return S_FALSE;
    EnterCriticalSection(&device->state_lock);
    D3dObject *found = find_cached(device, (D3dObject *)device->depth_stencil_states,
                                   offsetof(D3dDepthStencilState, desc), &d, sizeof(d));
    if (!found) {
        D3dDepthStencilState *state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (state) {
            state->iface.lpVtbl = &ds_vtbl;
            D3dChildInit(&state->child, device);
            state->desc = d;
            state->next = device->depth_stencil_states;
            device->depth_stencil_states = state;
            found = (D3dObject *)state;
        }
    }
    LeaveCriticalSection(&device->state_lock);
    if (!found)
        return E_OUTOFMEMORY;
    *out = (D3dDepthStencilState *)found;
    return S_OK;
}

/* ------------------------------------------------------------------ samplers */

static const GUID *const SAMPLER_IIDS[] = {&IID_ID3D11SamplerState};

static HRESULT STDMETHODCALLTYPE sampler_query(void *iface, REFIID riid, void **out)
{
    return query(iface, SAMPLER_IIDS, 1, D3d10SamplerInterface(CONTAINING_RECORD(iface, D3dSamplerState, iface), riid),
                 riid, out);
}

static void sampler_destroy(D3dObject *o)
{
    D3dSamplerState *state = (D3dSamplerState *)o;
    D3dDevice *device = o->child.device;
    D3dDefer(device, device->next_serial, D3D_GARBAGE_SAMPLER, (UINT64)state->sampler);
    D3dHeapFree(device, D3D_HEAP_SAMPLER, state->heap_index);
}

static ULONG STDMETHODCALLTYPE sampler_release(void *iface)
{
    return release_cached(iface, (D3dObject **)&object(iface)->child.device->sampler_states, sampler_destroy);
}

static void STDMETHODCALLTYPE sampler_get_desc(void *iface, D3D11_SAMPLER_DESC *desc)
{
    *desc = CONTAINING_RECORD(iface, D3dSamplerState, iface)->desc;
}

static const ID3D11SamplerStateVtbl sampler_vtbl = {OBJECT_METHODS(sampler), VTBL_FN(sampler_get_desc)};

D3dSamplerState *D3dSamplerFrom(ID3D11SamplerState *iface)
{
    return iface ? CONTAINING_RECORD((void *)iface, D3dSamplerState, iface) : nullptr;
}

static VkSamplerAddressMode address_mode(D3D11_TEXTURE_ADDRESS_MODE mode)
{
    switch (mode) {
    case D3D11_TEXTURE_ADDRESS_MIRROR:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case D3D11_TEXTURE_ADDRESS_CLAMP:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case D3D11_TEXTURE_ADDRESS_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkSampler create_sampler(D3dDevice *device, const D3D11_SAMPLER_DESC *d)
{
    VkSamplerCustomBorderColorCreateInfoEXT border = {VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    VkSamplerReductionModeCreateInfo reduction = {VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO};
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    UINT filter = d->Filter;
    info.minFilter = filter & 0x10 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.magFilter = filter & 0x04 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = filter & 0x01 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address_mode(d->AddressU);
    info.addressModeV = address_mode(d->AddressV);
    info.addressModeW = address_mode(d->AddressW);
    info.mipLodBias = d->MipLODBias;
    if (filter & 0x40) {
        info.anisotropyEnable = TRUE;
        info.maxAnisotropy = (float)D3D_MAX(1, D3D_MIN(d->MaxAnisotropy, 16));
        info.minFilter = info.magFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    UINT kind = (filter >> 7) & 3;
    if (kind == 1) {
        info.compareEnable = TRUE;
        info.compareOp = (VkCompareOp)(d->ComparisonFunc - 1);
    } else if (kind >= 2) {
        reduction.reductionMode = kind == 2 ? VK_SAMPLER_REDUCTION_MODE_MIN : VK_SAMPLER_REDUCTION_MODE_MAX;
        info.pNext = &reduction;
    }
    info.minLod = d->MinLOD;
    info.maxLod = d->MaxLOD >= D3D11_FLOAT32_MAX ? VK_LOD_CLAMP_NONE : d->MaxLOD;
    const FLOAT *c = d->BorderColor;
    if (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0)
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    else if (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 1)
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    else if (c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1)
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    else if (device->has_custom_border) {
        info.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
        memcpy(border.customBorderColor.float32, c, sizeof(FLOAT) * 4);
        border.pNext = (void *)info.pNext;
        info.pNext = &border;
    } else {
        info.borderColor = c[3] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
    VkSampler sampler = VK_NULL_HANDLE;
    device->vk.vkCreateSampler(device->vk.device, &info, nullptr, &sampler);
    return sampler;
}

HRESULT D3dCreateSamplerState(D3dDevice *device, const D3D11_SAMPLER_DESC *desc, D3dSamplerState **out)
{
    D3D11_SAMPLER_DESC d = *desc;
    if (d.AddressU < 1 || d.AddressU > 5 || d.AddressV < 1 || d.AddressV > 5 || d.AddressW < 1 || d.AddressW > 5 ||
        d.MaxAnisotropy > 16 || ((d.Filter >> 7) == 1 && (d.ComparisonFunc < 1 || d.ComparisonFunc > 8)))
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;
    EnterCriticalSection(&device->state_lock);
    D3dObject *found =
        find_cached(device, (D3dObject *)device->sampler_states, offsetof(D3dSamplerState, desc), &d, sizeof(d));
    if (!found) {
        D3dSamplerState *state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (state) {
            state->iface.lpVtbl = &sampler_vtbl;
            state->desc = d;
            state->sampler = create_sampler(device, &d);
            state->heap_index = state->sampler ? D3dHeapAlloc(device, D3D_HEAP_SAMPLER) : 0;
            if (!state->heap_index) {
                if (state->sampler)
                    device->vk.vkDestroySampler(device->vk.device, state->sampler, nullptr);
                HeapFree(GetProcessHeap(), 0, state);
                state = nullptr;
            } else {
                D3dChildInit(&state->child, device);
                D3dHeapWriteSampler(device, state->heap_index, state->sampler);
                state->next = device->sampler_states;
                device->sampler_states = state;
                found = (D3dObject *)state;
            }
        }
    }
    LeaveCriticalSection(&device->state_lock);
    if (!found)
        return E_OUTOFMEMORY;
    *out = (D3dSamplerState *)found;
    return S_OK;
}

/* ------------------------------------------------------------------ input layouts */

static const GUID *const LAYOUT_IIDS[] = {&IID_ID3D11InputLayout};

static HRESULT STDMETHODCALLTYPE layout_query(void *iface, REFIID riid, void **out)
{
    return query(iface, LAYOUT_IIDS, 1, D3d10InputLayoutInterface(CONTAINING_RECORD(iface, D3dInputLayout, iface), riid),
                 riid, out);
}

static ULONG STDMETHODCALLTYPE layout_release(void *iface)
{
    D3dObject *o = object(iface);
    ULONG refs = InterlockedDecrement(&o->child.refs);
    if (!refs) {
        D3dChildRelease(&o->child);
        HeapFree(GetProcessHeap(), 0, CONTAINING_RECORD(iface, D3dInputLayout, iface));
    }
    return refs;
}

static const ID3D11InputLayoutVtbl layout_vtbl = {OBJECT_METHODS(layout)};

D3dInputLayout *D3dInputLayoutFrom(ID3D11InputLayout *iface)
{
    return iface ? CONTAINING_RECORD((void *)iface, D3dInputLayout, iface) : nullptr;
}

static void *heap_alloc(void *ctx, size_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void heap_free(void *ctx, void *p) { HeapFree(GetProcessHeap(), 0, p); }
const MimeAllocator g_d3d_allocator = {heap_alloc, heap_free, nullptr};

HRESULT D3dCreateInputLayout(D3dDevice *device, const D3D11_INPUT_ELEMENT_DESC *elements, UINT count,
                             const void *bytecode, SIZE_T length, D3dInputLayout **out)
{
    if (count > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT || (count && !elements) || !bytecode)
        return E_INVALIDARG;
    DxbcSignature inputs = {};
    if (dxbc_parse_io(bytecode, length, 0, &inputs, &g_d3d_allocator))
        return E_INVALIDARG;
    D3dInputLayout *layout = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*layout));
    if (!layout) {
        g_d3d_allocator.free(nullptr, inputs.elements);
        return E_OUTOFMEMORY;
    }
    UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        const D3D11_INPUT_ELEMENT_DESC *e = &elements[i];
        const D3dFormat *f = D3dGetFormat(e->Format);
        if (e->InputSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT || f->vk == VK_FORMAT_UNDEFINED ||
            f->block != 1 || !e->SemanticName) {
            hr = E_INVALIDARG;
            break;
        }
        UINT offset = e->AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? offsets[e->InputSlot]
                                                                           : e->AlignedByteOffset;
        offsets[e->InputSlot] = offset + f->block_bytes;
        layout->slot_mask |= 1u << e->InputSlot;
        layout->instanced[e->InputSlot] = e->InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA;
        layout->step_rate[e->InputSlot] = e->InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA ? e->InstanceDataStepRate
                                                                                              : 1;
        /* elements feed the shader input of the same semantic, the rest are dropped. */
        const DxbcSignatureElement *input = nullptr;
        for (UINT k = 0; k < inputs.count && !input; k++)
            if (!_stricmp(inputs.elements[k].name, e->SemanticName) &&
                inputs.elements[k].semantic_index == e->SemanticIndex)
                input = &inputs.elements[k];
        if (!input)
            continue;
        VkVertexInputAttributeDescription2EXT *a = &layout->attributes[layout->count++];
        a->sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
        a->location = input->reg;
        a->binding = e->InputSlot;
        a->format = f->vk;
        a->offset = offset;
    }
    /* an unfed shader input other than a system value makes the layout invalid. */
    for (UINT k = 0; k < inputs.count && SUCCEEDED(hr); k++) {
        if (inputs.elements[k].system_value)
            continue;
        BOOL fed = FALSE;
        for (UINT i = 0; i < layout->count && !fed; i++)
            fed = layout->attributes[i].location == inputs.elements[k].reg;
        if (!fed) {
            D3dLog("d3d11: input layout lacks %s%u", inputs.elements[k].name, inputs.elements[k].semantic_index);
            hr = E_INVALIDARG;
        }
    }
    g_d3d_allocator.free(nullptr, inputs.elements);
    if (FAILED(hr) || !out) {
        HeapFree(GetProcessHeap(), 0, layout);
        return FAILED(hr) ? hr : S_FALSE;
    }
    layout->iface.lpVtbl = &layout_vtbl;
    D3dChildInit(&layout->child, device);
    *out = layout;
    return S_OK;
}

/* ------------------------------------------------------------------ class linkage and context states */

/* class linkage for dynamic shader linking: creatable, but no instance is ever found. */
typedef struct D3dClassLinkage {
    ID3D11ClassLinkage iface;
    D3dChild child;
} D3dClassLinkage;

static const GUID *const LINKAGE_IIDS[] = {&IID_ID3D11ClassLinkage};

static HRESULT STDMETHODCALLTYPE linkage_query(void *iface, REFIID riid, void **out)
{
    return query(iface, LINKAGE_IIDS, 1, nullptr, riid, out);
}

static ULONG STDMETHODCALLTYPE linkage_release(void *iface) { return layout_release(iface); }

static HRESULT STDMETHODCALLTYPE linkage_get_class_instance(void *iface, LPCSTR name, UINT index,
                                                            ID3D11ClassInstance **instance)
{
    if (instance)
        *instance = nullptr;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE linkage_create_class_instance(void *iface, LPCSTR type, UINT cb_offset,
                                                               UINT cv_offset, UINT texture_offset,
                                                               UINT sampler_offset, ID3D11ClassInstance **instance)
{
    if (instance)
        *instance = nullptr;
    D3dLog("d3d11: class instances are not implemented");
    return E_NOTIMPL;
}

static const ID3D11ClassLinkageVtbl linkage_vtbl = {OBJECT_METHODS(linkage), VTBL_FN(linkage_get_class_instance),
                                                    VTBL_FN(linkage_create_class_instance)};

HRESULT D3dCreateClassLinkage(D3dDevice *device, ID3D11ClassLinkage **out)
{
    D3dClassLinkage *linkage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*linkage));
    if (!linkage)
        return E_OUTOFMEMORY;
    linkage->iface.lpVtbl = &linkage_vtbl;
    D3dChildInit(&linkage->child, device);
    *out = &linkage->iface;
    return S_OK;
}

/* CreateDeviceContextState: there is only one state, the device's own. */
typedef struct D3dContextState {
    ID3DDeviceContextState iface;
    D3dChild child;
} D3dContextState;

static const GUID *const STATE_IIDS[] = {&IID_ID3DDeviceContextState};

static HRESULT STDMETHODCALLTYPE state_query(void *iface, REFIID riid, void **out)
{
    return query(iface, STATE_IIDS, 1, nullptr, riid, out);
}

static ULONG STDMETHODCALLTYPE state_release(void *iface) { return layout_release(iface); }

static const ID3DDeviceContextStateVtbl state_vtbl = {OBJECT_METHODS(state)};

HRESULT D3dCreateContextState(D3dDevice *device, ID3DDeviceContextState **out)
{
    D3dContextState *state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    if (!state)
        return E_OUTOFMEMORY;
    state->iface.lpVtbl = &state_vtbl;
    D3dChildInit(&state->child, device);
    *out = &state->iface;
    return S_OK;
}
