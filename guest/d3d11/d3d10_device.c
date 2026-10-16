/*
 * ID3D10Device1, as d3d11.dll's device carries it for the devices
 * d3d10.dll and d3d10_1.dll make: each method converts what direct3d 10
 * passes and calls the direct3d 11 device or its immediate context.
 * objects handed out are the direct3d 10 interfaces of direct3d 11
 * objects, asked for by iid as d3d11.dll does.
 */
#include "d3d10_private.h"

#define VTBL_FN(f) ((void *)(f))

static D3dDevice *impl(ID3D10Device1 *iface) { return D3D10_IMPL(D3dDevice, iface); }

static ID3D11Device5 *device11(ID3D10Device1 *iface) { return &impl(iface)->iface; }

static ID3D11DeviceContext4 *context(ID3D10Device1 *iface) { return &impl(iface)->immediate->iface; }

/* the direct3d 11 interfaces behind direct3d 10 ones. */
static ID3D11Resource *resource11(void *iface) { return iface ? D3D10_IMPL(D3dResource, iface)->iface : nullptr; }

static void *view11(void *iface) { return iface ? &D3D10_IMPL(D3dView, iface)->iface : nullptr; }

static void *shader11(void *iface) { return iface ? &D3D10_IMPL(D3dShader, iface)->iface : nullptr; }

static void *query11(void *iface) { return iface ? &D3D10_IMPL(D3dQuery, iface)->iface : nullptr; }

#define STATE11(type, state) ((state) ? (void *)&D3D10_IMPL(type, state)->iface : NULL)

/* what a creation method of direct3d 11 made, as the direct3d 10 interface asked. */
static HRESULT made(HRESULT hr, void *object, REFIID riid, void **out)
{
    if (out)
        *out = SUCCEEDED(hr) && object ? D3d10From11(object, riid) : nullptr;
    return hr;
}

/* state objects report direct3d 10's error for too many of them. */
static HRESULT state_error(HRESULT hr)
{
    if (hr == D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS)
        return D3D10_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS;
    if (hr == D3D11_ERROR_FILE_NOT_FOUND)
        return D3D10_ERROR_FILE_NOT_FOUND;
    return hr;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE device_query(ID3D10Device1 *iface, REFIID riid, void **out)
{
    return device11(iface)->lpVtbl->QueryInterface(device11(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE device_add_ref(ID3D10Device1 *iface)
{
    return device11(iface)->lpVtbl->AddRef(device11(iface));
}

static ULONG STDMETHODCALLTYPE device_release(ID3D10Device1 *iface)
{
    return device11(iface)->lpVtbl->Release(device11(iface));
}

/* ------------------------------------------------------------------ binding */

/* the slots a binding converts: d3d11.dll's buffers for each, the counts past them clamped. */
#define CB_SLOTS 15
#define SRV_SLOTS 128
#define SAMPLER_SLOTS 16
#define VB_SLOTS 32

static UINT convert_resources(ID3D11Buffer **out, ID3D10Buffer *const *in, UINT count, UINT max)
{
    count = D3D_MIN(count, max);
    for (UINT i = 0; i < count; i++)
        out[i] = in ? (ID3D11Buffer *)resource11(in[i]) : nullptr;
    return count;
}

static UINT convert_views(void **out, ID3D10ShaderResourceView *const *in, UINT count)
{
    count = D3D_MIN(count, SRV_SLOTS);
    for (UINT i = 0; i < count; i++)
        out[i] = in ? view11(in[i]) : nullptr;
    return count;
}

static UINT convert_samplers(ID3D11SamplerState **out, ID3D10SamplerState *const *in, UINT count)
{
    count = D3D_MIN(count, SAMPLER_SLOTS);
    for (UINT i = 0; i < count; i++)
        out[i] = in ? STATE11(D3dSamplerState, in[i]) : nullptr;
    return count;
}

static void STDMETHODCALLTYPE vs_set_constant_buffers(ID3D10Device1 *iface, UINT slot, UINT count,
                                                      ID3D10Buffer *const *buffers)
{
    ID3D11Buffer *b[CB_SLOTS];
    count = convert_resources(b, buffers, count, CB_SLOTS);
    context(iface)->lpVtbl->VSSetConstantBuffers(context(iface), slot, count, b);
}

static void STDMETHODCALLTYPE ps_set_constant_buffers(ID3D10Device1 *iface, UINT slot, UINT count,
                                                      ID3D10Buffer *const *buffers)
{
    ID3D11Buffer *b[CB_SLOTS];
    count = convert_resources(b, buffers, count, CB_SLOTS);
    context(iface)->lpVtbl->PSSetConstantBuffers(context(iface), slot, count, b);
}

static void STDMETHODCALLTYPE gs_set_constant_buffers(ID3D10Device1 *iface, UINT slot, UINT count,
                                                      ID3D10Buffer *const *buffers)
{
    ID3D11Buffer *b[CB_SLOTS];
    count = convert_resources(b, buffers, count, CB_SLOTS);
    context(iface)->lpVtbl->GSSetConstantBuffers(context(iface), slot, count, b);
}

static void STDMETHODCALLTYPE vs_set_shader_resources(ID3D10Device1 *iface, UINT slot, UINT count,
                                                      ID3D10ShaderResourceView *const *views)
{
    void *v[SRV_SLOTS];
    count = convert_views(v, views, count);
    context(iface)->lpVtbl->VSSetShaderResources(context(iface), slot, count, (ID3D11ShaderResourceView **)v);
}

static void STDMETHODCALLTYPE ps_set_shader_resources(ID3D10Device1 *iface, UINT slot, UINT count,
                                                      ID3D10ShaderResourceView *const *views)
{
    void *v[SRV_SLOTS];
    count = convert_views(v, views, count);
    context(iface)->lpVtbl->PSSetShaderResources(context(iface), slot, count, (ID3D11ShaderResourceView **)v);
}

static void STDMETHODCALLTYPE gs_set_shader_resources(ID3D10Device1 *iface, UINT slot, UINT count,
                                                      ID3D10ShaderResourceView *const *views)
{
    void *v[SRV_SLOTS];
    count = convert_views(v, views, count);
    context(iface)->lpVtbl->GSSetShaderResources(context(iface), slot, count, (ID3D11ShaderResourceView **)v);
}

static void STDMETHODCALLTYPE vs_set_samplers(ID3D10Device1 *iface, UINT slot, UINT count,
                                              ID3D10SamplerState *const *samplers)
{
    ID3D11SamplerState *s[SAMPLER_SLOTS];
    count = convert_samplers(s, samplers, count);
    context(iface)->lpVtbl->VSSetSamplers(context(iface), slot, count, s);
}

static void STDMETHODCALLTYPE ps_set_samplers(ID3D10Device1 *iface, UINT slot, UINT count,
                                              ID3D10SamplerState *const *samplers)
{
    ID3D11SamplerState *s[SAMPLER_SLOTS];
    count = convert_samplers(s, samplers, count);
    context(iface)->lpVtbl->PSSetSamplers(context(iface), slot, count, s);
}

static void STDMETHODCALLTYPE gs_set_samplers(ID3D10Device1 *iface, UINT slot, UINT count,
                                              ID3D10SamplerState *const *samplers)
{
    ID3D11SamplerState *s[SAMPLER_SLOTS];
    count = convert_samplers(s, samplers, count);
    context(iface)->lpVtbl->GSSetSamplers(context(iface), slot, count, s);
}

static void STDMETHODCALLTYPE vs_set_shader(ID3D10Device1 *iface, ID3D10VertexShader *shader)
{
    context(iface)->lpVtbl->VSSetShader(context(iface), shader11(shader), nullptr, 0);
}

static void STDMETHODCALLTYPE gs_set_shader(ID3D10Device1 *iface, ID3D10GeometryShader *shader)
{
    context(iface)->lpVtbl->GSSetShader(context(iface), shader11(shader), nullptr, 0);
}

static void STDMETHODCALLTYPE ps_set_shader(ID3D10Device1 *iface, ID3D10PixelShader *shader)
{
    context(iface)->lpVtbl->PSSetShader(context(iface), shader11(shader), nullptr, 0);
}

static void STDMETHODCALLTYPE draw_indexed(ID3D10Device1 *iface, UINT count, UINT start, INT base)
{
    context(iface)->lpVtbl->DrawIndexed(context(iface), count, start, base);
}

static void STDMETHODCALLTYPE draw(ID3D10Device1 *iface, UINT count, UINT start)
{
    context(iface)->lpVtbl->Draw(context(iface), count, start);
}

static void STDMETHODCALLTYPE draw_indexed_instanced(ID3D10Device1 *iface, UINT count, UINT instances, UINT start,
                                                     INT base, UINT start_instance)
{
    context(iface)->lpVtbl->DrawIndexedInstanced(context(iface), count, instances, start, base, start_instance);
}

static void STDMETHODCALLTYPE draw_instanced(ID3D10Device1 *iface, UINT count, UINT instances, UINT start,
                                             UINT start_instance)
{
    context(iface)->lpVtbl->DrawInstanced(context(iface), count, instances, start, start_instance);
}

static void STDMETHODCALLTYPE draw_auto(ID3D10Device1 *iface) { context(iface)->lpVtbl->DrawAuto(context(iface)); }

static void STDMETHODCALLTYPE ia_set_input_layout(ID3D10Device1 *iface, ID3D10InputLayout *layout)
{
    context(iface)->lpVtbl->IASetInputLayout(context(iface), STATE11(D3dInputLayout, layout));
}

static void STDMETHODCALLTYPE ia_set_vertex_buffers(ID3D10Device1 *iface, UINT slot, UINT count,
                                                    ID3D10Buffer *const *buffers, const UINT *strides,
                                                    const UINT *offsets)
{
    ID3D11Buffer *b[VB_SLOTS];
    count = convert_resources(b, buffers, count, VB_SLOTS);
    context(iface)->lpVtbl->IASetVertexBuffers(context(iface), slot, count, b, strides, offsets);
}

static void STDMETHODCALLTYPE ia_set_index_buffer(ID3D10Device1 *iface, ID3D10Buffer *buffer, DXGI_FORMAT format,
                                                  UINT offset)
{
    context(iface)->lpVtbl->IASetIndexBuffer(context(iface), (ID3D11Buffer *)resource11(buffer), format, offset);
}

static void STDMETHODCALLTYPE ia_set_primitive_topology(ID3D10Device1 *iface, D3D10_PRIMITIVE_TOPOLOGY topology)
{
    context(iface)->lpVtbl->IASetPrimitiveTopology(context(iface), topology);
}

static void STDMETHODCALLTYPE set_predication(ID3D10Device1 *iface, ID3D10Predicate *predicate, BOOL value)
{
    context(iface)->lpVtbl->SetPredication(context(iface), query11(predicate), value);
}

static void STDMETHODCALLTYPE om_set_render_targets(ID3D10Device1 *iface, UINT count,
                                                    ID3D10RenderTargetView *const *views,
                                                    ID3D10DepthStencilView *depth)
{
    void *v[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    count = D3D_MIN(count, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT);
    for (UINT i = 0; i < count; i++)
        v[i] = views ? view11(views[i]) : nullptr;
    context(iface)->lpVtbl->OMSetRenderTargets(context(iface), count, (ID3D11RenderTargetView **)v, view11(depth));
}

static void STDMETHODCALLTYPE om_set_blend_state(ID3D10Device1 *iface, ID3D10BlendState *state,
                                                 const FLOAT factor[4], UINT mask)
{
    context(iface)->lpVtbl->OMSetBlendState(context(iface), STATE11(D3dBlendState, state), factor, mask);
}

static void STDMETHODCALLTYPE om_set_depth_stencil_state(ID3D10Device1 *iface, ID3D10DepthStencilState *state,
                                                         UINT ref)
{
    context(iface)->lpVtbl->OMSetDepthStencilState(context(iface), STATE11(D3dDepthStencilState, state), ref);
}

static void STDMETHODCALLTYPE so_set_targets(ID3D10Device1 *iface, UINT count, ID3D10Buffer *const *buffers,
                                             const UINT *offsets)
{
    ID3D11Buffer *b[D3D10_SO_BUFFER_SLOT_COUNT];
    count = convert_resources(b, buffers, count, D3D10_SO_BUFFER_SLOT_COUNT);
    context(iface)->lpVtbl->SOSetTargets(context(iface), count, b, offsets);
}

static void STDMETHODCALLTYPE rs_set_state(ID3D10Device1 *iface, ID3D10RasterizerState *state)
{
    context(iface)->lpVtbl->RSSetState(context(iface), STATE11(D3dRasterizerState, state));
}

/* direct3d 10's viewports are whole pixels, made floats; more than there are slots sets nothing. */
static void STDMETHODCALLTYPE rs_set_viewports(ID3D10Device1 *iface, UINT count, const D3D10_VIEWPORT *viewports)
{
    D3D11_VIEWPORT v[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    if (count > D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
        return;
    for (UINT i = 0; i < count; i++)
        v[i] = (D3D11_VIEWPORT){(FLOAT)viewports[i].TopLeftX, (FLOAT)viewports[i].TopLeftY,
                                (FLOAT)viewports[i].Width,    (FLOAT)viewports[i].Height,
                                viewports[i].MinDepth,        viewports[i].MaxDepth};
    context(iface)->lpVtbl->RSSetViewports(context(iface), count, v);
}

static void STDMETHODCALLTYPE rs_set_scissor_rects(ID3D10Device1 *iface, UINT count, const D3D10_RECT *rects)
{
    context(iface)->lpVtbl->RSSetScissorRects(context(iface), count, rects);
}

/* ------------------------------------------------------------------ copies and clears */

static void STDMETHODCALLTYPE copy_subresource_region(ID3D10Device1 *iface, ID3D10Resource *dst, UINT dst_sub, UINT x,
                                                      UINT y, UINT z, ID3D10Resource *src, UINT src_sub,
                                                      const D3D10_BOX *box)
{
    context(iface)->lpVtbl->CopySubresourceRegion(context(iface), resource11(dst), dst_sub, x, y, z, resource11(src),
                                                  src_sub, (const D3D11_BOX *)box);
}

static void STDMETHODCALLTYPE copy_resource(ID3D10Device1 *iface, ID3D10Resource *dst, ID3D10Resource *src)
{
    context(iface)->lpVtbl->CopyResource(context(iface), resource11(dst), resource11(src));
}

static void STDMETHODCALLTYPE update_subresource(ID3D10Device1 *iface, ID3D10Resource *dst, UINT dst_sub,
                                                 const D3D10_BOX *box, const void *data, UINT row_pitch,
                                                 UINT depth_pitch)
{
    context(iface)->lpVtbl->UpdateSubresource(context(iface), resource11(dst), dst_sub, (const D3D11_BOX *)box, data,
                                              row_pitch, depth_pitch);
}

static void STDMETHODCALLTYPE clear_render_target_view(ID3D10Device1 *iface, ID3D10RenderTargetView *view,
                                                       const FLOAT color[4])
{
    context(iface)->lpVtbl->ClearRenderTargetView(context(iface), view11(view), color);
}

static void STDMETHODCALLTYPE clear_depth_stencil_view(ID3D10Device1 *iface, ID3D10DepthStencilView *view, UINT flags,
                                                       FLOAT depth, UINT8 stencil)
{
    context(iface)->lpVtbl->ClearDepthStencilView(context(iface), view11(view), flags, depth, stencil);
}

static void STDMETHODCALLTYPE generate_mips(ID3D10Device1 *iface, ID3D10ShaderResourceView *view)
{
    context(iface)->lpVtbl->GenerateMips(context(iface), view11(view));
}

static void STDMETHODCALLTYPE resolve_subresource(ID3D10Device1 *iface, ID3D10Resource *dst, UINT dst_sub,
                                                  ID3D10Resource *src, UINT src_sub, DXGI_FORMAT format)
{
    context(iface)->lpVtbl->ResolveSubresource(context(iface), resource11(dst), dst_sub, resource11(src), src_sub,
                                               format);
}

/* ------------------------------------------------------------------ getters */

/*
 * a slot at a time, as d3d11.dll does it: the direct3d 11 getter's object,
 * then its direct3d 10 interface.
 */
typedef void(STDMETHODCALLTYPE *SlotGetter)(ID3D11DeviceContext4 *This, UINT slot, UINT count, void **objects);

static void get_slots(ID3D10Device1 *iface, SlotGetter get, UINT slot, UINT count, void **out, REFIID riid)
{
    if (!out)
        return;
    for (UINT i = 0; i < count; i++) {
        void *object = nullptr;
        get(context(iface), slot + i, 1, &object);
        out[i] = D3d10From11(object, riid);
    }
}

#define GET_SLOTS(name, method, type, iid)                                                                             \
    static void STDMETHODCALLTYPE name(ID3D10Device1 *iface, UINT slot, UINT count, type **out)                        \
    {                                                                                                                  \
        get_slots(iface, (SlotGetter)context(iface)->lpVtbl->method, slot, count, (void **)out, &iid);                 \
    }

GET_SLOTS(vs_get_constant_buffers, VSGetConstantBuffers, ID3D10Buffer, IID_ID3D10Buffer)
GET_SLOTS(ps_get_constant_buffers, PSGetConstantBuffers, ID3D10Buffer, IID_ID3D10Buffer)
GET_SLOTS(gs_get_constant_buffers, GSGetConstantBuffers, ID3D10Buffer, IID_ID3D10Buffer)
GET_SLOTS(vs_get_shader_resources, VSGetShaderResources, ID3D10ShaderResourceView, IID_ID3D10ShaderResourceView)
GET_SLOTS(ps_get_shader_resources, PSGetShaderResources, ID3D10ShaderResourceView, IID_ID3D10ShaderResourceView)
GET_SLOTS(gs_get_shader_resources, GSGetShaderResources, ID3D10ShaderResourceView, IID_ID3D10ShaderResourceView)
GET_SLOTS(vs_get_samplers, VSGetSamplers, ID3D10SamplerState, IID_ID3D10SamplerState)
GET_SLOTS(ps_get_samplers, PSGetSamplers, ID3D10SamplerState, IID_ID3D10SamplerState)
GET_SLOTS(gs_get_samplers, GSGetSamplers, ID3D10SamplerState, IID_ID3D10SamplerState)

static void STDMETHODCALLTYPE vs_get_shader(ID3D10Device1 *iface, ID3D10VertexShader **shader)
{
    ID3D11VertexShader *s = nullptr;
    context(iface)->lpVtbl->VSGetShader(context(iface), &s, nullptr, nullptr);
    *shader = D3d10From11(s, &IID_ID3D10VertexShader);
}

static void STDMETHODCALLTYPE gs_get_shader(ID3D10Device1 *iface, ID3D10GeometryShader **shader)
{
    ID3D11GeometryShader *s = nullptr;
    context(iface)->lpVtbl->GSGetShader(context(iface), &s, nullptr, nullptr);
    *shader = D3d10From11(s, &IID_ID3D10GeometryShader);
}

static void STDMETHODCALLTYPE ps_get_shader(ID3D10Device1 *iface, ID3D10PixelShader **shader)
{
    ID3D11PixelShader *s = nullptr;
    context(iface)->lpVtbl->PSGetShader(context(iface), &s, nullptr, nullptr);
    *shader = D3d10From11(s, &IID_ID3D10PixelShader);
}

static void STDMETHODCALLTYPE ia_get_input_layout(ID3D10Device1 *iface, ID3D10InputLayout **layout)
{
    ID3D11InputLayout *l = nullptr;
    context(iface)->lpVtbl->IAGetInputLayout(context(iface), &l);
    *layout = D3d10From11(l, &IID_ID3D10InputLayout);
}

static void STDMETHODCALLTYPE ia_get_vertex_buffers(ID3D10Device1 *iface, UINT slot, UINT count,
                                                    ID3D10Buffer **buffers, UINT *strides, UINT *offsets)
{
    ID3D11Buffer *b[VB_SLOTS] = {};
    count = D3D_MIN(count, VB_SLOTS);
    context(iface)->lpVtbl->IAGetVertexBuffers(context(iface), slot, count, buffers ? b : nullptr, strides, offsets);
    for (UINT i = 0; buffers && i < count; i++)
        buffers[i] = D3d10From11(b[i], &IID_ID3D10Buffer);
}

static void STDMETHODCALLTYPE ia_get_index_buffer(ID3D10Device1 *iface, ID3D10Buffer **buffer, DXGI_FORMAT *format,
                                                  UINT *offset)
{
    ID3D11Buffer *b = nullptr;
    context(iface)->lpVtbl->IAGetIndexBuffer(context(iface), &b, format, offset);
    void *b10 = D3d10From11(b, &IID_ID3D10Buffer);
    if (buffer)
        *buffer = b10;
    else if (b10)
        ((ID3D10Buffer *)b10)->lpVtbl->Release(b10);
}

static void STDMETHODCALLTYPE ia_get_primitive_topology(ID3D10Device1 *iface, D3D10_PRIMITIVE_TOPOLOGY *topology)
{
    context(iface)->lpVtbl->IAGetPrimitiveTopology(context(iface), topology);
}

static void STDMETHODCALLTYPE get_predication(ID3D10Device1 *iface, ID3D10Predicate **predicate, BOOL *value)
{
    ID3D11Predicate *p = nullptr;
    context(iface)->lpVtbl->GetPredication(context(iface), &p, value);
    void *p10 = D3d10From11(p, &IID_ID3D10Predicate);
    if (predicate)
        *predicate = p10;
    else if (p10)
        ((ID3D10Predicate *)p10)->lpVtbl->Release(p10);
}

static void STDMETHODCALLTYPE om_get_render_targets(ID3D10Device1 *iface, UINT count, ID3D10RenderTargetView **views,
                                                    ID3D10DepthStencilView **depth)
{
    ID3D11RenderTargetView *v[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView *d = nullptr;
    count = D3D_MIN(count, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT);
    context(iface)->lpVtbl->OMGetRenderTargets(context(iface), count, views ? v : nullptr, depth ? &d : nullptr);
    for (UINT i = 0; views && i < count; i++)
        views[i] = D3d10From11(v[i], &IID_ID3D10RenderTargetView);
    if (depth)
        *depth = D3d10From11(d, &IID_ID3D10DepthStencilView);
}

static void STDMETHODCALLTYPE om_get_blend_state(ID3D10Device1 *iface, ID3D10BlendState **state, FLOAT factor[4],
                                                 UINT *mask)
{
    ID3D11BlendState *s = nullptr;
    context(iface)->lpVtbl->OMGetBlendState(context(iface), &s, factor, mask);
    void *s10 = D3d10From11(s, &IID_ID3D10BlendState);
    if (state)
        *state = s10;
    else if (s10)
        ((ID3D10BlendState *)s10)->lpVtbl->Release(s10);
}

static void STDMETHODCALLTYPE om_get_depth_stencil_state(ID3D10Device1 *iface, ID3D10DepthStencilState **state,
                                                         UINT *ref)
{
    ID3D11DepthStencilState *s = nullptr;
    context(iface)->lpVtbl->OMGetDepthStencilState(context(iface), &s, ref);
    void *s10 = D3d10From11(s, &IID_ID3D10DepthStencilState);
    if (state)
        *state = s10;
    else if (s10)
        ((ID3D10DepthStencilState *)s10)->lpVtbl->Release(s10);
}

/* d3d11.dll gives -1 for the offsets of the slots there are, 0 past them. */
static void STDMETHODCALLTYPE so_get_targets(ID3D10Device1 *iface, UINT count, ID3D10Buffer **buffers, UINT *offsets)
{
    if (buffers) {
        ID3D11Buffer *b[D3D10_SO_BUFFER_SLOT_COUNT] = {};
        UINT n = D3D_MIN(count, D3D10_SO_BUFFER_SLOT_COUNT);
        context(iface)->lpVtbl->SOGetTargets(context(iface), n, b);
        for (UINT i = 0; i < count; i++)
            buffers[i] = i < n ? D3d10From11(b[i], &IID_ID3D10Buffer) : nullptr;
    }
    for (UINT i = 0; offsets && i < count; i++)
        offsets[i] = i < D3D10_SO_BUFFER_SLOT_COUNT ? ~0u : 0;
}

static void STDMETHODCALLTYPE rs_get_state(ID3D10Device1 *iface, ID3D10RasterizerState **state)
{
    ID3D11RasterizerState *s = nullptr;
    context(iface)->lpVtbl->RSGetState(context(iface), &s);
    *state = D3d10From11(s, &IID_ID3D10RasterizerState);
}

/* read as direct3d 11's, then made whole pixels in place. */
static void STDMETHODCALLTYPE rs_get_viewports(ID3D10Device1 *iface, UINT *count, D3D10_VIEWPORT *viewports)
{
    context(iface)->lpVtbl->RSGetViewports(context(iface), count, (D3D11_VIEWPORT *)viewports);
    for (UINT i = 0; viewports && i < *count; i++) {
        D3D11_VIEWPORT v;
        memcpy(&v, &viewports[i], sizeof(v));
        viewports[i].TopLeftX = (INT)v.TopLeftX;
        viewports[i].TopLeftY = (INT)v.TopLeftY;
        viewports[i].Width = (UINT)(INT)v.Width;
        viewports[i].Height = (UINT)(INT)v.Height;
    }
}

static void STDMETHODCALLTYPE rs_get_scissor_rects(ID3D10Device1 *iface, UINT *count, D3D10_RECT *rects)
{
    context(iface)->lpVtbl->RSGetScissorRects(context(iface), count, rects);
}

/* ------------------------------------------------------------------ the device */

static HRESULT STDMETHODCALLTYPE get_device_removed_reason(ID3D10Device1 *iface)
{
    return device11(iface)->lpVtbl->GetDeviceRemovedReason(device11(iface));
}

static HRESULT STDMETHODCALLTYPE set_exception_mode(ID3D10Device1 *iface, UINT flags)
{
    return device11(iface)->lpVtbl->SetExceptionMode(device11(iface), flags);
}

static UINT STDMETHODCALLTYPE get_exception_mode(ID3D10Device1 *iface)
{
    return device11(iface)->lpVtbl->GetExceptionMode(device11(iface));
}

static HRESULT STDMETHODCALLTYPE get_private_data(ID3D10Device1 *iface, REFGUID guid, UINT *size, void *data)
{
    return device11(iface)->lpVtbl->GetPrivateData(device11(iface), guid, size, data);
}

static HRESULT STDMETHODCALLTYPE set_private_data(ID3D10Device1 *iface, REFGUID guid, UINT size, const void *data)
{
    return device11(iface)->lpVtbl->SetPrivateData(device11(iface), guid, size, data);
}

static HRESULT STDMETHODCALLTYPE set_private_data_interface(ID3D10Device1 *iface, REFGUID guid, const IUnknown *data)
{
    return device11(iface)->lpVtbl->SetPrivateDataInterface(device11(iface), guid, data);
}

static void STDMETHODCALLTYPE clear_state(ID3D10Device1 *iface) { context(iface)->lpVtbl->ClearState(context(iface)); }

static void STDMETHODCALLTYPE flush(ID3D10Device1 *iface) { context(iface)->lpVtbl->Flush(context(iface)); }

/* ------------------------------------------------------------------ creation */

/*
 * keyed mutexes and gdi compatibility move up to direct3d 11's bits; cube
 * maps only for 2d textures. other bits are invalid: d3d11.dll passes on a
 * flag it refuses, here refused at once.
 */
static BOOL misc11(UINT misc, BOOL cube, UINT *out)
{
    *out = (misc & 3) | (cube ? misc & 4 : 0) | (misc & 0x30) << 4;
    return !(misc & 0xFFFFFFC8);
}

static HRESULT STDMETHODCALLTYPE create_buffer(ID3D10Device1 *iface, const D3D10_BUFFER_DESC *desc,
                                               const D3D10_SUBRESOURCE_DATA *data, ID3D10Buffer **out)
{
    D3D11_BUFFER_DESC d = {};
    if (out)
        *out = nullptr;
    if (desc) {
        d = (D3D11_BUFFER_DESC){desc->ByteWidth, (D3D11_USAGE)desc->Usage, desc->BindFlags, desc->CPUAccessFlags};
        if (!misc11(desc->MiscFlags, FALSE, &d.MiscFlags))
            return E_INVALIDARG;
    }
    ID3D11Buffer *buffer = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateBuffer(device11(iface), desc ? &d : nullptr,
                                                       (const D3D11_SUBRESOURCE_DATA *)data, out ? &buffer : nullptr);
    return made(hr, buffer, &IID_ID3D10Buffer, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_texture1d(ID3D10Device1 *iface, const D3D10_TEXTURE1D_DESC *desc,
                                                  const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture1D **out)
{
    D3D11_TEXTURE1D_DESC d;
    if (out)
        *out = nullptr;
    if (desc) {
        memcpy(&d, desc, sizeof(d));
        if (!misc11(desc->MiscFlags, FALSE, &d.MiscFlags))
            return E_INVALIDARG;
    }
    ID3D11Texture1D *texture = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateTexture1D(device11(iface), desc ? &d : nullptr,
                                                          (const D3D11_SUBRESOURCE_DATA *)data, out ? &texture : nullptr);
    return made(hr, texture, &IID_ID3D10Texture1D, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_texture2d(ID3D10Device1 *iface, const D3D10_TEXTURE2D_DESC *desc,
                                                  const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture2D **out)
{
    D3D11_TEXTURE2D_DESC d;
    if (out)
        *out = nullptr;
    if (desc) {
        memcpy(&d, desc, sizeof(d));
        if (!misc11(desc->MiscFlags, TRUE, &d.MiscFlags))
            return E_INVALIDARG;
    }
    ID3D11Texture2D *texture = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateTexture2D(device11(iface), desc ? &d : nullptr,
                                                          (const D3D11_SUBRESOURCE_DATA *)data, out ? &texture : nullptr);
    return made(hr, texture, &IID_ID3D10Texture2D, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_texture3d(ID3D10Device1 *iface, const D3D10_TEXTURE3D_DESC *desc,
                                                  const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture3D **out)
{
    D3D11_TEXTURE3D_DESC d;
    if (out)
        *out = nullptr;
    if (desc) {
        memcpy(&d, desc, sizeof(d));
        if (!misc11(desc->MiscFlags, FALSE, &d.MiscFlags))
            return E_INVALIDARG;
    }
    ID3D11Texture3D *texture = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateTexture3D(device11(iface), desc ? &d : nullptr,
                                                          (const D3D11_SUBRESOURCE_DATA *)data, out ? &texture : nullptr);
    return made(hr, texture, &IID_ID3D10Texture3D, (void **)out);
}

/* direct3d 10's shader resource view descriptions are direct3d 11's, cube arrays and all. */
static HRESULT create_srv(ID3D10Device1 *iface, ID3D10Resource *resource, const void *desc, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    ID3D11ShaderResourceView *view = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateShaderResourceView(
        device11(iface), resource11(resource), (const D3D11_SHADER_RESOURCE_VIEW_DESC *)desc, out ? &view : nullptr);
    return made(hr, view, riid, out);
}

static HRESULT STDMETHODCALLTYPE create_shader_resource_view(ID3D10Device1 *iface, ID3D10Resource *resource,
                                                             const D3D10_SHADER_RESOURCE_VIEW_DESC *desc,
                                                             ID3D10ShaderResourceView **out)
{
    return create_srv(iface, resource, desc, &IID_ID3D10ShaderResourceView, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_shader_resource_view1(ID3D10Device1 *iface, ID3D10Resource *resource,
                                                              const D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc,
                                                              ID3D10ShaderResourceView1 **out)
{
    return create_srv(iface, resource, desc, &IID_ID3D10ShaderResourceView1, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_render_target_view(ID3D10Device1 *iface, ID3D10Resource *resource,
                                                           const D3D10_RENDER_TARGET_VIEW_DESC *desc,
                                                           ID3D10RenderTargetView **out)
{
    D3D11_RENDER_TARGET_VIEW_DESC d;
    if (out)
        *out = nullptr;
    if (desc)
        memcpy(&d, desc, sizeof(d));
    ID3D11RenderTargetView *view = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateRenderTargetView(device11(iface), resource11(resource),
                                                                 desc ? &d : nullptr, out ? &view : nullptr);
    return made(hr, view, &IID_ID3D10RenderTargetView, (void **)out);
}

/* the description gains direct3d 11's flags, none. */
static HRESULT STDMETHODCALLTYPE create_depth_stencil_view(ID3D10Device1 *iface, ID3D10Resource *resource,
                                                           const D3D10_DEPTH_STENCIL_VIEW_DESC *desc,
                                                           ID3D10DepthStencilView **out)
{
    D3D11_DEPTH_STENCIL_VIEW_DESC d = {};
    if (out)
        *out = nullptr;
    if (desc) {
        d.Format = desc->Format;
        d.ViewDimension = (D3D11_DSV_DIMENSION)desc->ViewDimension;
        memcpy(&d.Texture2DArray, &desc->Texture2DArray, sizeof(d.Texture2DArray));
    }
    ID3D11DepthStencilView *view = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateDepthStencilView(device11(iface), resource11(resource),
                                                                 desc ? &d : nullptr, out ? &view : nullptr);
    return made(hr, view, &IID_ID3D10DepthStencilView, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_input_layout(ID3D10Device1 *iface, const D3D10_INPUT_ELEMENT_DESC *elements,
                                                     UINT count, const void *bytecode, SIZE_T length,
                                                     ID3D10InputLayout **out)
{
    if (out)
        *out = nullptr;
    ID3D11InputLayout *layout = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateInputLayout(device11(iface), (const D3D11_INPUT_ELEMENT_DESC *)elements,
                                                            count, bytecode, length, out ? &layout : nullptr);
    return made(hr, layout, &IID_ID3D10InputLayout, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_vertex_shader(ID3D10Device1 *iface, const void *bytecode, SIZE_T length,
                                                      ID3D10VertexShader **out)
{
    if (out)
        *out = nullptr;
    ID3D11VertexShader *shader = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateVertexShader(device11(iface), bytecode, length, nullptr,
                                                             out ? &shader : nullptr);
    return made(hr, shader, &IID_ID3D10VertexShader, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_geometry_shader(ID3D10Device1 *iface, const void *bytecode, SIZE_T length,
                                                        ID3D10GeometryShader **out)
{
    if (out)
        *out = nullptr;
    ID3D11GeometryShader *shader = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateGeometryShader(device11(iface), bytecode, length, nullptr,
                                                               out ? &shader : nullptr);
    return made(hr, shader, &IID_ID3D10GeometryShader, (void **)out);
}

/*
 * the entries go to stream 0, which is rasterized. the stride is the
 * buffer's when every entry writes to slot 0, or when it is given; with
 * several buffers and none given, direct3d 11 packs them.
 */
static HRESULT STDMETHODCALLTYPE create_geometry_shader_with_stream_output(ID3D10Device1 *iface, const void *bytecode,
                                                                           SIZE_T length,
                                                                           const D3D10_SO_DECLARATION_ENTRY *entries,
                                                                           UINT count, UINT stride,
                                                                           ID3D10GeometryShader **out)
{
    D3D11_SO_DECLARATION_ENTRY d[129];
    BOOL slot0 = TRUE;
    if (out)
        *out = nullptr;
    count = D3D_MIN(count, RTL_NUMBER_OF(d));
    for (UINT i = 0; entries && i < count; i++) {
        d[i] = (D3D11_SO_DECLARATION_ENTRY){0,
                                            entries[i].SemanticName,
                                            entries[i].SemanticIndex,
                                            entries[i].StartComponent,
                                            entries[i].ComponentCount,
                                            entries[i].OutputSlot};
        slot0 &= !entries[i].OutputSlot;
    }
    BOOL strided = stride || (count && slot0);
    ID3D11GeometryShader *shader = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateGeometryShaderWithStreamOutput(
        device11(iface), bytecode, length, entries ? d : nullptr, count, strided ? &stride : nullptr, strided, 0, nullptr,
        out ? &shader : nullptr);
    return made(hr, shader, &IID_ID3D10GeometryShader, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_pixel_shader(ID3D10Device1 *iface, const void *bytecode, SIZE_T length,
                                                     ID3D10PixelShader **out)
{
    if (out)
        *out = nullptr;
    ID3D11PixelShader *shader = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreatePixelShader(device11(iface), bytecode, length, nullptr,
                                                            out ? &shader : nullptr);
    return made(hr, shader, &IID_ID3D10PixelShader, (void **)out);
}

/* one blend for every target: independent when the targets' enables or masks differ. */
static HRESULT STDMETHODCALLTYPE create_blend_state(ID3D10Device1 *iface, const D3D10_BLEND_DESC *desc,
                                                    ID3D10BlendState **out)
{
    D3D11_BLEND_DESC d = {};
    if (out)
        *out = nullptr;
    if (desc) {
        d.AlphaToCoverageEnable = desc->AlphaToCoverageEnable;
        for (UINT i = 0; i < 8; i++) {
            if (desc->BlendEnable[i] != desc->BlendEnable[0] ||
                desc->RenderTargetWriteMask[i] != desc->RenderTargetWriteMask[0])
                d.IndependentBlendEnable = TRUE;
            d.RenderTarget[i] = (D3D11_RENDER_TARGET_BLEND_DESC){
                desc->BlendEnable[i],          (D3D11_BLEND)desc->SrcBlend,       (D3D11_BLEND)desc->DestBlend,
                (D3D11_BLEND_OP)desc->BlendOp, (D3D11_BLEND)desc->SrcBlendAlpha, (D3D11_BLEND)desc->DestBlendAlpha,
                (D3D11_BLEND_OP)desc->BlendOpAlpha, desc->RenderTargetWriteMask[i]};
        }
    }
    ID3D11BlendState *state = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateBlendState(device11(iface), desc ? &d : nullptr, out ? &state : nullptr);
    return state_error(made(hr, state, &IID_ID3D10BlendState, (void **)out));
}

/* direct3d 10.1's blend description is direct3d 11's. */
static HRESULT STDMETHODCALLTYPE create_blend_state1(ID3D10Device1 *iface, const D3D10_BLEND_DESC1 *desc,
                                                     ID3D10BlendState1 **out)
{
    if (out)
        *out = nullptr;
    ID3D11BlendState *state = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateBlendState(device11(iface), (const D3D11_BLEND_DESC *)desc,
                                                           out ? &state : nullptr);
    return state_error(made(hr, state, &IID_ID3D10BlendState1, (void **)out));
}

static HRESULT STDMETHODCALLTYPE create_depth_stencil_state(ID3D10Device1 *iface, const D3D10_DEPTH_STENCIL_DESC *desc,
                                                            ID3D10DepthStencilState **out)
{
    if (out)
        *out = nullptr;
    ID3D11DepthStencilState *state = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateDepthStencilState(
        device11(iface), (const D3D11_DEPTH_STENCIL_DESC *)desc, out ? &state : nullptr);
    return state_error(made(hr, state, &IID_ID3D10DepthStencilState, (void **)out));
}

static HRESULT STDMETHODCALLTYPE create_rasterizer_state(ID3D10Device1 *iface, const D3D10_RASTERIZER_DESC *desc,
                                                         ID3D10RasterizerState **out)
{
    if (out)
        *out = nullptr;
    ID3D11RasterizerState *state = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateRasterizerState(device11(iface), (const D3D11_RASTERIZER_DESC *)desc,
                                                                out ? &state : nullptr);
    return state_error(made(hr, state, &IID_ID3D10RasterizerState, (void **)out));
}

static HRESULT STDMETHODCALLTYPE create_sampler_state(ID3D10Device1 *iface, const D3D10_SAMPLER_DESC *desc,
                                                      ID3D10SamplerState **out)
{
    if (out)
        *out = nullptr;
    ID3D11SamplerState *state = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateSamplerState(device11(iface), (const D3D11_SAMPLER_DESC *)desc,
                                                             out ? &state : nullptr);
    return made(hr, state, &IID_ID3D10SamplerState, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_query(ID3D10Device1 *iface, const D3D10_QUERY_DESC *desc, ID3D10Query **out)
{
    if (out)
        *out = nullptr;
    ID3D11Query *query = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateQuery(device11(iface), (const D3D11_QUERY_DESC *)desc,
                                                      out ? &query : nullptr);
    return made(hr, query, &IID_ID3D10Query, (void **)out);
}

static HRESULT STDMETHODCALLTYPE create_predicate(ID3D10Device1 *iface, const D3D10_QUERY_DESC *desc,
                                                  ID3D10Predicate **out)
{
    if (out)
        *out = nullptr;
    ID3D11Predicate *predicate = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreatePredicate(device11(iface), (const D3D11_QUERY_DESC *)desc,
                                                          out ? &predicate : nullptr);
    return made(hr, predicate, &IID_ID3D10Predicate, (void **)out);
}

/* direct3d 10's own counters, up to the last of them, 0x11, are not supported. */
static HRESULT STDMETHODCALLTYPE create_counter(ID3D10Device1 *iface, const D3D10_COUNTER_DESC *desc,
                                                ID3D10Counter **out)
{
    if (desc && desc->Counter <= 0x11)
        return DXGI_ERROR_UNSUPPORTED;
    if (out)
        *out = nullptr;
    ID3D11Counter *counter = nullptr;
    HRESULT hr = device11(iface)->lpVtbl->CreateCounter(device11(iface), (const D3D11_COUNTER_DESC *)desc,
                                                        out ? &counter : nullptr);
    return made(hr, counter, &IID_ID3D10Counter, (void **)out);
}

static HRESULT STDMETHODCALLTYPE check_format_support(ID3D10Device1 *iface, DXGI_FORMAT format, UINT *support)
{
    return device11(iface)->lpVtbl->CheckFormatSupport(device11(iface), format, support);
}

static HRESULT STDMETHODCALLTYPE check_multisample_quality_levels(ID3D10Device1 *iface, DXGI_FORMAT format,
                                                                  UINT samples, UINT *levels)
{
    return device11(iface)->lpVtbl->CheckMultisampleQualityLevels(device11(iface), format, samples, levels);
}

static void STDMETHODCALLTYPE check_counter_info(ID3D10Device1 *iface, D3D10_COUNTER_INFO *info)
{
    device11(iface)->lpVtbl->CheckCounterInfo(device11(iface), (D3D11_COUNTER_INFO *)info);
}

static HRESULT STDMETHODCALLTYPE check_counter(ID3D10Device1 *iface, const D3D10_COUNTER_DESC *desc,
                                               D3D10_COUNTER_TYPE *type, UINT *active, LPSTR name, UINT *name_length,
                                               LPSTR units, UINT *units_length, LPSTR description,
                                               UINT *description_length)
{
    return device11(iface)->lpVtbl->CheckCounter(device11(iface), (const D3D11_COUNTER_DESC *)desc,
                                                 (D3D11_COUNTER_TYPE *)type, active, name, name_length, units,
                                                 units_length, description, description_length);
}

/* the flags d3d10.dll created the device with, its own above direct3d 11's included. */
static UINT STDMETHODCALLTYPE get_creation_flags(ID3D10Device1 *iface)
{
    return device11(iface)->lpVtbl->GetCreationFlags(device11(iface));
}

static HRESULT STDMETHODCALLTYPE open_shared_resource(ID3D10Device1 *iface, HANDLE handle, REFIID riid, void **out)
{
    return device11(iface)->lpVtbl->OpenSharedResource(device11(iface), handle, riid, out);
}

/* text filters are gone: their size is kept nowhere and reads as 0. */
static void STDMETHODCALLTYPE set_text_filter_size(ID3D10Device1 *iface, UINT width, UINT height) {}

static void STDMETHODCALLTYPE get_text_filter_size(ID3D10Device1 *iface, UINT *width, UINT *height)
{
    *width = 0;
    *height = 0;
}

static D3D10_FEATURE_LEVEL1 STDMETHODCALLTYPE get_feature_level(ID3D10Device1 *iface)
{
    return (D3D10_FEATURE_LEVEL1)impl(iface)->feature_level;
}

static const ID3D10Device1Vtbl device_vtbl = {
    device_query,
    device_add_ref,
    device_release,
    vs_set_constant_buffers,
    ps_set_shader_resources,
    ps_set_shader,
    ps_set_samplers,
    vs_set_shader,
    draw_indexed,
    draw,
    ps_set_constant_buffers,
    ia_set_input_layout,
    ia_set_vertex_buffers,
    ia_set_index_buffer,
    draw_indexed_instanced,
    draw_instanced,
    gs_set_constant_buffers,
    gs_set_shader,
    ia_set_primitive_topology,
    vs_set_shader_resources,
    vs_set_samplers,
    set_predication,
    gs_set_shader_resources,
    gs_set_samplers,
    om_set_render_targets,
    om_set_blend_state,
    om_set_depth_stencil_state,
    so_set_targets,
    draw_auto,
    rs_set_state,
    rs_set_viewports,
    rs_set_scissor_rects,
    copy_subresource_region,
    copy_resource,
    update_subresource,
    clear_render_target_view,
    clear_depth_stencil_view,
    generate_mips,
    resolve_subresource,
    vs_get_constant_buffers,
    ps_get_shader_resources,
    ps_get_shader,
    ps_get_samplers,
    vs_get_shader,
    ps_get_constant_buffers,
    ia_get_input_layout,
    ia_get_vertex_buffers,
    ia_get_index_buffer,
    gs_get_constant_buffers,
    gs_get_shader,
    ia_get_primitive_topology,
    vs_get_shader_resources,
    vs_get_samplers,
    get_predication,
    gs_get_shader_resources,
    gs_get_samplers,
    om_get_render_targets,
    om_get_blend_state,
    om_get_depth_stencil_state,
    so_get_targets,
    rs_get_state,
    rs_get_viewports,
    rs_get_scissor_rects,
    get_device_removed_reason,
    set_exception_mode,
    get_exception_mode,
    get_private_data,
    set_private_data,
    set_private_data_interface,
    clear_state,
    flush,
    create_buffer,
    create_texture1d,
    create_texture2d,
    create_texture3d,
    create_shader_resource_view,
    create_render_target_view,
    create_depth_stencil_view,
    create_input_layout,
    create_vertex_shader,
    create_geometry_shader,
    create_geometry_shader_with_stream_output,
    create_pixel_shader,
    create_blend_state,
    create_depth_stencil_state,
    create_rasterizer_state,
    create_sampler_state,
    create_query,
    create_predicate,
    create_counter,
    check_format_support,
    check_multisample_quality_levels,
    check_counter_info,
    check_counter,
    get_creation_flags,
    open_shared_resource,
    set_text_filter_size,
    get_text_filter_size,
    create_shader_resource_view1,
    create_blend_state1,
    get_feature_level,
};

/* only devices d3d10.dll or d3d10_1.dll made are direct3d 10 devices. */
void *D3d10DeviceInterface(D3dDevice *device, REFIID riid)
{
    if (!(device->flags & (D3D_CREATE_DEVICE_D3D10 | D3D_CREATE_DEVICE_D3D10_1)) ||
        (!IsEqualGUID(riid, &IID_ID3D10Device) && !IsEqualGUID(riid, &IID_ID3D10Device1)))
        return nullptr;
    device->d3d10.lpVtbl = &device_vtbl;
    return &device->d3d10;
}
