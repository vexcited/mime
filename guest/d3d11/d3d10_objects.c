/*
 * the direct3d 10 interfaces of resources, views, states, shaders, input
 * layouts and queries, as d3d11.dll's C10and11 classes give them: one
 * object answering both versions, its references and private data shared.
 * resources and queries map, begin, end and read back through the
 * immediate context.
 */
#include "d3d10_private.h"

#define VTBL_FN(f) ((void *)(f))

void *D3d10From11(void *iface, REFIID riid)
{
    if (!iface)
        return NULL;
    IUnknown *unknown = iface;
    void *out = NULL;
    unknown->lpVtbl->QueryInterface(unknown, riid, &out);
    unknown->lpVtbl->Release(unknown);
    return out;
}

static ID3D11DeviceContext4 *immediate(D3dDevice *device) { return &device->immediate->iface; }

static void get_device(D3dDevice *device, ID3D10Device **out)
{
    *out = NULL;
    device->iface.lpVtbl->QueryInterface(&device->iface, &IID_ID3D10Device, (void **)out);
}

/* what every object shares: its identity, references and private data are the direct3d 11 object's. */
#define CHILD_METHODS(prefix, type, unknown)                                                                           \
    static type *prefix##_impl(void *iface) { return D3D10_IMPL(type, iface); }                                       \
    static IUnknown *prefix##_unknown(void *iface)                                                                     \
    {                                                                                                                  \
        type *o = prefix##_impl(iface);                                                                                \
        return (IUnknown *)(unknown);                                                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_query(void *iface, REFIID riid, void **out)                              \
    {                                                                                                                  \
        IUnknown *u = prefix##_unknown(iface);                                                                         \
        return u->lpVtbl->QueryInterface(u, riid, out);                                                                \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_add_ref(void *iface)                                                       \
    {                                                                                                                  \
        IUnknown *u = prefix##_unknown(iface);                                                                         \
        return u->lpVtbl->AddRef(u);                                                                                   \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_release(void *iface)                                                       \
    {                                                                                                                  \
        IUnknown *u = prefix##_unknown(iface);                                                                         \
        return u->lpVtbl->Release(u);                                                                                  \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_get_device(void *iface, ID3D10Device **device)                              \
    {                                                                                                                  \
        get_device(prefix##_impl(iface)->child.device, device);                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)       \
    {                                                                                                                  \
        return MimePrivateGet(&prefix##_impl(iface)->child.data, guid, size, data);                                    \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)  \
    {                                                                                                                  \
        return MimePrivateSet(&prefix##_impl(iface)->child.data, guid, size, data);                                    \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_set_private_data_interface(void *iface, REFGUID guid,                    \
                                                                         const IUnknown *data)                         \
    {                                                                                                                  \
        return MimePrivateSetInterface(&prefix##_impl(iface)->child.data, guid, data);                                 \
    }

#define CHILD_VTBL(prefix)                                                                                             \
    VTBL_FN(prefix##_query), VTBL_FN(prefix##_add_ref), VTBL_FN(prefix##_release), VTBL_FN(prefix##_get_device),     \
        VTBL_FN(prefix##_get_private_data), VTBL_FN(prefix##_set_private_data),                                        \
        VTBL_FN(prefix##_set_private_data_interface)

/* ------------------------------------------------------------------ resources */

CHILD_METHODS(resource, D3dResource, o->iface)

static void STDMETHODCALLTYPE resource_get_type(void *iface, D3D10_RESOURCE_DIMENSION *dimension)
{
    *dimension = (D3D10_RESOURCE_DIMENSION)resource_impl(iface)->dimension;
}

static void STDMETHODCALLTYPE resource_set_eviction_priority(void *iface, UINT priority)
{
    resource_impl(iface)->eviction_priority = priority;
}

static UINT STDMETHODCALLTYPE resource_get_eviction_priority(void *iface)
{
    return resource_impl(iface)->eviction_priority;
}

/* keyed mutexes and gdi compatibility move back down to direct3d 10's bits. */
static UINT misc10(UINT misc) { return (misc & 7) | ((misc >> 4) & 0x30); }

/* a subresource mapped through the immediate context, NULL when it fails. */
static HRESULT map(void *iface, UINT sub, D3D10_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    D3dResource *r = resource_impl(iface);
    ID3D11DeviceContext4 *c = immediate(r->child.device);
    HRESULT hr = c->lpVtbl->Map(c, r->iface, sub, (D3D11_MAP)type, flags, mapped);
    if (FAILED(hr))
        memset(mapped, 0, sizeof(*mapped));
    return hr;
}

static void unmap(void *iface, UINT sub)
{
    D3dResource *r = resource_impl(iface);
    ID3D11DeviceContext4 *c = immediate(r->child.device);
    c->lpVtbl->Unmap(c, r->iface, sub);
}

static HRESULT STDMETHODCALLTYPE buffer_map(void *iface, D3D10_MAP type, UINT flags, void **data)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = map(iface, 0, type, flags, &mapped);
    *data = mapped.pData;
    return hr;
}

static void STDMETHODCALLTYPE buffer_unmap(void *iface) { unmap(iface, 0); }

static void STDMETHODCALLTYPE buffer_get_desc(void *iface, D3D10_BUFFER_DESC *desc)
{
    const D3D11_BUFFER_DESC *d = &resource_impl(iface)->desc.buffer;
    desc->ByteWidth = d->ByteWidth;
    desc->Usage = (D3D10_USAGE)d->Usage;
    desc->BindFlags = d->BindFlags;
    desc->CPUAccessFlags = d->CPUAccessFlags;
    desc->MiscFlags = misc10(d->MiscFlags);
}

static HRESULT STDMETHODCALLTYPE texture1d_map(void *iface, UINT sub, D3D10_MAP type, UINT flags, void **data)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = map(iface, sub, type, flags, &mapped);
    *data = mapped.pData;
    return hr;
}

static void STDMETHODCALLTYPE texture_unmap(void *iface, UINT sub) { unmap(iface, sub); }

static void STDMETHODCALLTYPE texture1d_get_desc(void *iface, D3D10_TEXTURE1D_DESC *desc)
{
    memcpy(desc, &resource_impl(iface)->desc.texture1d, sizeof(*desc));
    desc->MiscFlags = misc10(desc->MiscFlags);
}

static HRESULT STDMETHODCALLTYPE texture2d_map(void *iface, UINT sub, D3D10_MAP type, UINT flags,
                                               D3D10_MAPPED_TEXTURE2D *out)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = map(iface, sub, type, flags, &mapped);
    out->pData = mapped.pData;
    out->RowPitch = mapped.RowPitch;
    return hr;
}

static void STDMETHODCALLTYPE texture2d_get_desc(void *iface, D3D10_TEXTURE2D_DESC *desc)
{
    memcpy(desc, &resource_impl(iface)->desc.texture2d, sizeof(*desc));
    desc->MiscFlags = misc10(desc->MiscFlags);
}

static HRESULT STDMETHODCALLTYPE texture3d_map(void *iface, UINT sub, D3D10_MAP type, UINT flags,
                                               D3D10_MAPPED_TEXTURE3D *out)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = map(iface, sub, type, flags, &mapped);
    out->pData = mapped.pData;
    out->RowPitch = mapped.RowPitch;
    out->DepthPitch = mapped.DepthPitch;
    return hr;
}

static void STDMETHODCALLTYPE texture3d_get_desc(void *iface, D3D10_TEXTURE3D_DESC *desc)
{
    memcpy(desc, &resource_impl(iface)->desc.texture3d, sizeof(*desc));
    desc->MiscFlags = misc10(desc->MiscFlags);
}

#define RESOURCE_VTBL                                                                                                  \
    CHILD_VTBL(resource), VTBL_FN(resource_get_type), VTBL_FN(resource_set_eviction_priority),                       \
        VTBL_FN(resource_get_eviction_priority)

static const ID3D10BufferVtbl buffer_vtbl = {RESOURCE_VTBL, VTBL_FN(buffer_map), VTBL_FN(buffer_unmap),
                                             VTBL_FN(buffer_get_desc)};
static const ID3D10Texture1DVtbl texture1d_vtbl = {RESOURCE_VTBL, VTBL_FN(texture1d_map), VTBL_FN(texture_unmap),
                                                   VTBL_FN(texture1d_get_desc)};
static const ID3D10Texture2DVtbl texture2d_vtbl = {RESOURCE_VTBL, VTBL_FN(texture2d_map), VTBL_FN(texture_unmap),
                                                   VTBL_FN(texture2d_get_desc)};
static const ID3D10Texture3DVtbl texture3d_vtbl = {RESOURCE_VTBL, VTBL_FN(texture3d_map), VTBL_FN(texture_unmap),
                                                   VTBL_FN(texture3d_get_desc)};

/* structured buffers and clamped textures, which direct3d 10 cannot express, have no direct3d 10 interface. */
void *D3d10ResourceInterface(D3dResource *r, REFIID riid)
{
    static const void *const vtbls[] = {NULL, &buffer_vtbl, &texture1d_vtbl, &texture2d_vtbl, &texture3d_vtbl};
    static const GUID *const iids[] = {NULL, &IID_ID3D10Buffer, &IID_ID3D10Texture1D, &IID_ID3D10Texture2D,
                                       &IID_ID3D10Texture3D};
    if (!IsEqualGUID(riid, &IID_ID3D10DeviceChild) && !IsEqualGUID(riid, &IID_ID3D10Resource) &&
        !IsEqualGUID(riid, iids[r->dimension]))
        return NULL;
    if (r->misc & (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED
                                                                   : D3D11_RESOURCE_MISC_RESOURCE_CLAMP))
        return NULL;
    r->d3d10.lpVtbl = vtbls[r->dimension];
    return &r->d3d10;
}

/* ------------------------------------------------------------------ views */

CHILD_METHODS(view, D3dView, &o->iface)

static void STDMETHODCALLTYPE view_get_resource(void *iface, ID3D10Resource **resource)
{
    ID3D11Resource *r = view_impl(iface)->resource->iface;
    *resource = NULL;
    r->lpVtbl->QueryInterface(r, &IID_ID3D10Resource, (void **)resource);
}

/* direct3d 10's descriptions of shader resource and render target views are direct3d 11's, by layout. */
static void STDMETHODCALLTYPE srv_get_desc(void *iface, D3D10_SHADER_RESOURCE_VIEW_DESC *desc)
{
    ID3D11ShaderResourceView *srv = (ID3D11ShaderResourceView *)&view_impl(iface)->iface;
    D3D11_SHADER_RESOURCE_VIEW_DESC d;
    srv->lpVtbl->GetDesc(srv, &d);
    memcpy(desc, &d, sizeof(*desc));
}

static void STDMETHODCALLTYPE srv_get_desc1(void *iface, D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc)
{
    ID3D11ShaderResourceView *srv = (ID3D11ShaderResourceView *)&view_impl(iface)->iface;
    D3D11_SHADER_RESOURCE_VIEW_DESC d;
    srv->lpVtbl->GetDesc(srv, &d);
    memcpy(desc, &d, sizeof(*desc));
}

static void STDMETHODCALLTYPE rtv_get_desc(void *iface, D3D10_RENDER_TARGET_VIEW_DESC *desc)
{
    ID3D11RenderTargetView *rtv = (ID3D11RenderTargetView *)&view_impl(iface)->iface;
    D3D11_RENDER_TARGET_VIEW_DESC d;
    rtv->lpVtbl->GetDesc(rtv, &d);
    memcpy(desc, &d, sizeof(*desc));
}

static void STDMETHODCALLTYPE dsv_get_desc(void *iface, D3D10_DEPTH_STENCIL_VIEW_DESC *desc)
{
    const D3D11_DEPTH_STENCIL_VIEW_DESC *d = &view_impl(iface)->desc.dsv;
    desc->Format = d->Format;
    desc->ViewDimension = (D3D10_DSV_DIMENSION)d->ViewDimension;
    memcpy(&desc->Texture2DArray, &d->Texture2DArray, sizeof(desc->Texture2DArray));
}

#define VIEW_VTBL CHILD_VTBL(view), VTBL_FN(view_get_resource)

static const ID3D10ShaderResourceView1Vtbl srv_vtbl = {VIEW_VTBL, VTBL_FN(srv_get_desc), VTBL_FN(srv_get_desc1)};
static const ID3D10RenderTargetViewVtbl rtv_vtbl = {VIEW_VTBL, VTBL_FN(rtv_get_desc)};
static const ID3D10DepthStencilViewVtbl dsv_vtbl = {VIEW_VTBL, VTBL_FN(dsv_get_desc)};

/*
 * raw buffer views, views of structured buffers and read only depth views
 * have no direct3d 10 interface, nor have unordered access views.
 */
void *D3d10ViewInterface(D3dView *view, REFIID riid)
{
    const void *vtbl = NULL;
    BOOL match = IsEqualGUID(riid, &IID_ID3D10DeviceChild) || IsEqualGUID(riid, &IID_ID3D10View);
    switch (view->kind) {
    case D3D_VIEW_SRV:
        vtbl = &srv_vtbl;
        match |= IsEqualGUID(riid, &IID_ID3D10ShaderResourceView) || IsEqualGUID(riid, &IID_ID3D10ShaderResourceView1);
        if (view->desc.srv.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX &&
            (view->desc.srv.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW))
            return NULL;
        if (view->desc.srv.ViewDimension == D3D11_SRV_DIMENSION_BUFFER &&
            (view->resource->misc & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED))
            return NULL;
        break;
    case D3D_VIEW_RTV:
        vtbl = &rtv_vtbl;
        match |= IsEqualGUID(riid, &IID_ID3D10RenderTargetView);
        break;
    case D3D_VIEW_DSV:
        vtbl = &dsv_vtbl;
        match |= IsEqualGUID(riid, &IID_ID3D10DepthStencilView);
        if (view->desc.dsv.Flags)
            return NULL;
        break;
    case D3D_VIEW_UAV:
        return NULL;
    }
    if (!match)
        return NULL;
    view->d3d10.lpVtbl = vtbl;
    return &view->d3d10;
}

/* ------------------------------------------------------------------ states */

CHILD_METHODS(blend, D3dBlendState, &o->iface)
CHILD_METHODS(rs, D3dRasterizerState, &o->iface)
CHILD_METHODS(ds, D3dDepthStencilState, &o->iface)
CHILD_METHODS(sampler, D3dSamplerState, &o->iface)
CHILD_METHODS(layout, D3dInputLayout, &o->iface)

/* each target's enable and mask, and the blend of the first target blending, or of the first. */
static void STDMETHODCALLTYPE blend_get_desc(void *iface, D3D10_BLEND_DESC *desc)
{
    const D3D11_BLEND_DESC1 *d = &blend_impl(iface)->desc;
    UINT first = 0;
    desc->AlphaToCoverageEnable = d->AlphaToCoverageEnable;
    for (UINT i = 0; i < 8; i++) {
        desc->BlendEnable[i] = d->RenderTarget[i].BlendEnable;
        desc->RenderTargetWriteMask[i] = d->RenderTarget[i].RenderTargetWriteMask;
    }
    for (UINT i = 0; i < 8; i++)
        if (d->RenderTarget[i].BlendEnable) {
            first = i;
            break;
        }
    const D3D11_RENDER_TARGET_BLEND_DESC1 *t = &d->RenderTarget[first];
    desc->SrcBlend = (D3D10_BLEND)t->SrcBlend;
    desc->DestBlend = (D3D10_BLEND)t->DestBlend;
    desc->BlendOp = (D3D10_BLEND_OP)t->BlendOp;
    desc->SrcBlendAlpha = (D3D10_BLEND)t->SrcBlendAlpha;
    desc->DestBlendAlpha = (D3D10_BLEND)t->DestBlendAlpha;
    desc->BlendOpAlpha = (D3D10_BLEND_OP)t->BlendOpAlpha;
}

static void STDMETHODCALLTYPE blend_get_desc1(void *iface, D3D10_BLEND_DESC1 *desc)
{
    ID3D11BlendState *state = (ID3D11BlendState *)&blend_impl(iface)->iface;
    D3D11_BLEND_DESC d;
    state->lpVtbl->GetDesc(state, &d);
    memcpy(desc, &d, sizeof(*desc));
}

static void STDMETHODCALLTYPE rs_get_desc(void *iface, D3D10_RASTERIZER_DESC *desc)
{
    memcpy(desc, &rs_impl(iface)->desc, sizeof(*desc));
}

static void STDMETHODCALLTYPE ds_get_desc(void *iface, D3D10_DEPTH_STENCIL_DESC *desc)
{
    memcpy(desc, &ds_impl(iface)->desc, sizeof(*desc));
}

static void STDMETHODCALLTYPE sampler_get_desc(void *iface, D3D10_SAMPLER_DESC *desc)
{
    memcpy(desc, &sampler_impl(iface)->desc, sizeof(*desc));
}

static const ID3D10BlendState1Vtbl blend_vtbl = {CHILD_VTBL(blend), VTBL_FN(blend_get_desc), VTBL_FN(blend_get_desc1)};
static const ID3D10RasterizerStateVtbl rs_vtbl = {CHILD_VTBL(rs), VTBL_FN(rs_get_desc)};
static const ID3D10DepthStencilStateVtbl ds_vtbl = {CHILD_VTBL(ds), VTBL_FN(ds_get_desc)};
static const ID3D10SamplerStateVtbl sampler_vtbl = {CHILD_VTBL(sampler), VTBL_FN(sampler_get_desc)};
static const ID3D10InputLayoutVtbl layout_vtbl = {CHILD_VTBL(layout)};

static void *child_interface(D3d10Iface *d3d10, const void *vtbl, REFIID riid, const GUID *iid, const GUID *iid1)
{
    if (!IsEqualGUID(riid, &IID_ID3D10DeviceChild) && !IsEqualGUID(riid, iid) && !(iid1 && IsEqualGUID(riid, iid1)))
        return NULL;
    d3d10->lpVtbl = vtbl;
    return d3d10;
}

void *D3d10BlendInterface(D3dBlendState *state, REFIID riid)
{
    return child_interface(&state->d3d10, &blend_vtbl, riid, &IID_ID3D10BlendState, &IID_ID3D10BlendState1);
}

void *D3d10RasterizerInterface(D3dRasterizerState *state, REFIID riid)
{
    return child_interface(&state->d3d10, &rs_vtbl, riid, &IID_ID3D10RasterizerState, NULL);
}

void *D3d10DepthStencilInterface(D3dDepthStencilState *state, REFIID riid)
{
    return child_interface(&state->d3d10, &ds_vtbl, riid, &IID_ID3D10DepthStencilState, NULL);
}

void *D3d10SamplerInterface(D3dSamplerState *state, REFIID riid)
{
    return child_interface(&state->d3d10, &sampler_vtbl, riid, &IID_ID3D10SamplerState, NULL);
}

void *D3d10InputLayoutInterface(D3dInputLayout *layout, REFIID riid)
{
    return child_interface(&layout->d3d10, &layout_vtbl, riid, &IID_ID3D10InputLayout, NULL);
}

/* ------------------------------------------------------------------ shaders */

CHILD_METHODS(shader, D3dShader, &o->iface)

/* the vertex, geometry and pixel shader interfaces are a device child's. */
static const ID3D10VertexShaderVtbl shader_vtbl = {CHILD_VTBL(shader)};

void *D3d10ShaderInterface(D3dShader *shader, REFIID riid)
{
    const GUID *iid = shader->stage == D3D_VS   ? &IID_ID3D10VertexShader
                      : shader->stage == D3D_GS ? &IID_ID3D10GeometryShader
                      : shader->stage == D3D_PS ? &IID_ID3D10PixelShader
                                                : NULL;
    return iid ? child_interface(&shader->d3d10, &shader_vtbl, riid, iid, NULL) : NULL;
}

/* ------------------------------------------------------------------ queries */

CHILD_METHODS(async, D3dQuery, &o->iface)

static void STDMETHODCALLTYPE async_begin(void *iface)
{
    D3dQuery *q = async_impl(iface);
    ID3D11DeviceContext4 *c = immediate(q->child.device);
    c->lpVtbl->Begin(c, (ID3D11Asynchronous *)&q->iface);
}

static void STDMETHODCALLTYPE async_end(void *iface)
{
    D3dQuery *q = async_impl(iface);
    ID3D11DeviceContext4 *c = immediate(q->child.device);
    c->lpVtbl->End(c, (ID3D11Asynchronous *)&q->iface);
}

/* direct3d 10's sizes, its pipeline statistics without the stages direct3d 11 added. */
static UINT STDMETHODCALLTYPE async_get_data_size(void *iface)
{
    D3dQuery *q = async_impl(iface);
    switch (q->desc.Query) {
    case D3D11_QUERY_EVENT:
    case D3D11_QUERY_OCCLUSION_PREDICATE:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
        return sizeof(BOOL);
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_TIMESTAMP:
        return sizeof(UINT64);
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
        return sizeof(D3D10_QUERY_DATA_TIMESTAMP_DISJOINT);
    case D3D11_QUERY_PIPELINE_STATISTICS:
        return sizeof(D3D10_QUERY_DATA_PIPELINE_STATISTICS);
    case D3D11_QUERY_SO_STATISTICS:
        return sizeof(D3D10_QUERY_DATA_SO_STATISTICS);
    default:
        return q->iface.lpVtbl->GetDataSize(&q->iface);
    }
}

/*
 * read as direct3d 11 reads it, into a buffer of direct3d 11's size, then
 * copied out at direct3d 10's, which the size asked must be.
 */
static HRESULT STDMETHODCALLTYPE async_get_data(void *iface, void *data, UINT size, UINT flags)
{
    D3dQuery *q = async_impl(iface);
    ID3D11DeviceContext4 *c = immediate(q->child.device);
    ID3D11Asynchronous *async = (ID3D11Asynchronous *)&q->iface;
    if (!data || !size)
        return c->lpVtbl->GetData(c, async, data, size, flags);
    if (size != async_get_data_size(iface))
        return E_INVALIDARG;
    BYTE result[128] = {0};
    HRESULT hr = c->lpVtbl->GetData(c, async, result, D3D_MIN(q->iface.lpVtbl->GetDataSize(&q->iface), 120), flags);
    memcpy(data, result, D3D_MIN(size, 120));
    return hr;
}

static void STDMETHODCALLTYPE async_get_desc(void *iface, D3D10_QUERY_DESC *desc)
{
    D3dQuery *q = async_impl(iface);
    desc->Query = (D3D10_QUERY)q->desc.Query;
    desc->MiscFlags = q->desc.MiscFlags;
}

static const ID3D10QueryVtbl query_vtbl = {CHILD_VTBL(async),          VTBL_FN(async_begin),
                                           VTBL_FN(async_end),         VTBL_FN(async_get_data),
                                           VTBL_FN(async_get_data_size), VTBL_FN(async_get_desc)};

/* every query is an ID3D10Query, predicates ID3D10Predicate too. there are no counters. */
void *D3d10AsyncInterface(D3dQuery *q, REFIID riid)
{
    D3D11_QUERY query = q->desc.Query;
    BOOL predicate = query == D3D11_QUERY_OCCLUSION_PREDICATE || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE ||
                     (query >= D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0 && (query & 1));
    if (!IsEqualGUID(riid, &IID_ID3D10DeviceChild) && !IsEqualGUID(riid, &IID_ID3D10Asynchronous) &&
        !IsEqualGUID(riid, &IID_ID3D10Query) && !(predicate && IsEqualGUID(riid, &IID_ID3D10Predicate)))
        return NULL;
    q->d3d10.lpVtbl = &query_vtbl;
    return &q->d3d10;
}
