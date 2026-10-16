/*
 * IDirect3DStateBlock9: a set of states and their values, made of the
 * device's (CreateStateBlock) or of what setters record between
 * BeginStateBlock and EndStateBlock, which then do not change the device.
 * Apply sets them through the device's own methods; Capture takes the
 * device's values of the states the block has.
 *
 * the states of the predefined blocks are those of windows' d3d9.dll,
 * read from its tables of them.
 */
#include "d3d9_private.h"

static const BYTE ALL_RS[] = {29,  7,   8,   9,   14,  15,  16,  19,  20,  22,  23,  24,  25,  26,  28,  33,  34,  35,
                              36,  37,  38,  40,  27,  195, 48,  52,  53,  54,  55,  56,  57,  58,  59,  60,  128, 129,
                              130, 131, 132, 133, 134, 135, 198, 199, 200, 201, 202, 203, 204, 205, 139, 141, 140, 136,
                              137, 143, 142, 148, 147, 145, 146, 151, 152, 154, 155, 156, 157, 158, 159, 160, 161, 162,
                              163, 166, 167, 168, 170, 171, 172, 173, 174, 175, 176, 178, 179, 180, 181, 182, 183, 184,
                              185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 206, 207, 208, 209};
static const BYTE PIXEL_RS[] = {7,   8,   9,   14,  15,  16,  19,  20,  23,  24,  25,  26,  33,  36,  37,  38,
                                40,  27,  195, 52,  53,  54,  55,  56,  57,  58,  59,  60,  128, 129, 130, 131,
                                132, 133, 134, 135, 198, 199, 200, 201, 202, 203, 204, 205, 168, 171, 174, 175,
                                176, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 206, 207, 208, 209};
static const BYTE VERTEX_RS[] = {9,   29,  22,  28,  34,  35,  36,  37,  38,  48,  139, 141, 140, 136, 137,
                                 143, 142, 148, 147, 145, 146, 151, 152, 154, 155, 156, 157, 158, 159, 160,
                                 161, 162, 163, 166, 167, 170, 172, 173, 178, 179, 180, 181, 182, 183, 184};
static const BYTE STAGE_STATES[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 22, 23, 24, 26, 27, 28};
static const BYTE VERTEX_STAGE_STATES[] = {D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS};

static D9StateBlock *impl(IDirect3DStateBlock9 *iface) { return CONTAINING_RECORD(iface, D9StateBlock, iface); }

static void set_bit(DWORD *bits, UINT i) { bits[i / 32] |= 1u << (i % 32); }

static BOOL has_bit(const DWORD *bits, UINT i) { return (bits[i / 32] >> (i % 32)) & 1; }

/* ------------------------------------------------------------------ bindings */

static void hold(D9Resource *r) { D9Bind(r); }

static void let_go(D9Resource *r) { D9Unbind(r); }

#define REPLACE(slot, object, base)                                                                                    \
    do {                                                                                                               \
        if ((object))                                                                                                  \
            hold(&(object)->base);                                                                                     \
        if ((slot))                                                                                                    \
            let_go(&(slot)->base);                                                                                     \
        (slot) = (object);                                                                                             \
    } while (0)

static void destroy(D9StateBlock *b)
{
    D9StateValues *v = &b->v;
    if (v->vs)
        let_go(&v->vs->base);
    if (v->ps)
        let_go(&v->ps->base);
    if (v->decl)
        let_go(&v->decl->base);
    for (UINT i = 0; i < D9_MAX_SAMPLERS; i++)
        if (v->textures[i])
            let_go(&v->textures[i]->base);
    for (UINT i = 0; i < D9_MAX_STREAMS; i++)
        if (v->streams[i].buffer)
            let_go(&v->streams[i].buffer->base);
    if (v->indices)
        let_go(&v->indices->base);
    HeapFree(GetProcessHeap(), 0, v->transforms);
    HeapFree(GetProcessHeap(), 0, v->lights);
    D9ResourceClear(&b->base);
    HeapFree(GetProcessHeap(), 0, b);
}

static void destroy_resource(D9Resource *r) { destroy(CONTAINING_RECORD(r, D9StateBlock, base)); }

/* ------------------------------------------------------------------ recording */

void D9RecordRenderState(D9StateBlock *b, DWORD state, DWORD value)
{
    if (state < 256) {
        b->v.render_states[state] = value;
        set_bit(b->mask.render_states, state);
    }
}

void D9RecordStageState(D9StateBlock *b, DWORD stage, DWORD type, DWORD value)
{
    if (stage < D9_MAX_STAGES && type < 33) {
        b->v.stage_states[stage][type] = value;
        b->mask.stage_states[stage] |= 1ull << type;
    }
}

void D9RecordSamplerState(D9StateBlock *b, UINT slot, DWORD type, DWORD value)
{
    if (slot < D9_MAX_SAMPLERS && type < 14) {
        b->v.sampler_states[slot][type] = value;
        b->mask.sampler_states[slot] |= 1u << type;
    }
}

void D9RecordTransform(D9StateBlock *b, UINT index, const D3DMATRIX *m)
{
    if (index >= 512)
        return;
    if (!b->v.transforms && !(b->v.transforms = HeapAlloc(GetProcessHeap(), 0, 512 * sizeof(D3DMATRIX))))
        return;
    b->v.transforms[index] = *m;
    set_bit(b->mask.transforms, index);
}

void D9RecordMaterial(D9StateBlock *b, const D3DMATERIAL9 *m)
{
    b->v.material = *m;
    b->mask.material = TRUE;
}

static D9BlockLight *block_light(D9StateBlock *b, DWORD index)
{
    for (UINT i = 0; i < b->v.light_count; i++)
        if (b->v.lights[i].index == index)
            return &b->v.lights[i];
    D9BlockLight *lights = b->v.lights ? HeapReAlloc(GetProcessHeap(), 0, b->v.lights, (b->v.light_count + 1) * sizeof(*lights))
                                       : HeapAlloc(GetProcessHeap(), 0, sizeof(*lights));
    if (!lights)
        return NULL;
    b->v.lights = lights;
    D9BlockLight *l = &lights[b->v.light_count++];
    memset(l, 0, sizeof(*l));
    l->index = index;
    return l;
}

void D9RecordLight(D9StateBlock *b, DWORD index, const D3DLIGHT9 *light)
{
    D9BlockLight *l = block_light(b, index);
    if (l)
        l->light = *light, l->has_light = TRUE;
}

void D9RecordLightEnable(D9StateBlock *b, DWORD index, BOOL enable)
{
    D9BlockLight *l = block_light(b, index);
    if (l)
        l->enabled = enable, l->has_enable = TRUE;
}

void D9RecordClipPlane(D9StateBlock *b, DWORD index, const float *plane)
{
    if (index < 6) {
        memcpy(b->v.clip_planes[index], plane, 16);
        b->mask.clip_planes |= 1u << index;
    }
}

void D9RecordViewport(D9StateBlock *b, const D3DVIEWPORT9 *viewport)
{
    b->v.viewport = *viewport;
    b->mask.viewport = TRUE;
}

void D9RecordScissor(D9StateBlock *b, const RECT *rect)
{
    b->v.scissor = *rect;
    b->mask.scissor = TRUE;
}

void D9RecordShader(D9StateBlock *b, BOOL pixel, D9Shader *s)
{
    if (pixel) {
        REPLACE(b->v.ps, s, base);
        b->mask.ps = TRUE;
    } else {
        REPLACE(b->v.vs, s, base);
        b->mask.vs = TRUE;
    }
}

void D9RecordDecl(D9StateBlock *b, D9VertexDecl *decl)
{
    REPLACE(b->v.decl, decl, base);
    b->mask.decl = TRUE;
}

void D9RecordTexture(D9StateBlock *b, UINT slot, D9Texture *t)
{
    if (slot < D9_MAX_SAMPLERS) {
        REPLACE(b->v.textures[slot], t, base);
        b->mask.textures |= 1u << slot;
    }
}

void D9RecordStream(D9StateBlock *b, UINT stream, D9Buffer *buffer, UINT offset, UINT stride)
{
    REPLACE(b->v.streams[stream].buffer, buffer, base);
    b->v.streams[stream].offset = offset;
    b->v.streams[stream].stride = stride;
    b->mask.streams |= 1u << stream;
}

void D9RecordStreamFreq(D9StateBlock *b, UINT stream, UINT setting)
{
    b->v.stream_freq[stream] = setting;
    b->mask.stream_freq |= 1u << stream;
}

void D9RecordIndices(D9StateBlock *b, D9Buffer *buffer)
{
    REPLACE(b->v.indices, buffer, base);
    b->mask.indices = TRUE;
}

/* constants: the registers set, as setters take them. */
void D9RecordConstants(D9StateBlock *b, BOOL pixel, UINT kind, UINT start, const void *data, UINT count)
{
    void *bank;
    DWORD *mask;
    UINT size;
    if (kind == 0) {
        bank = pixel ? (void *)b->v.ps_f : (void *)b->v.vs_f;
        mask = pixel ? b->mask.ps_f : b->mask.vs_f;
        size = 16;
    } else if (kind == 1) {
        bank = pixel ? (void *)b->v.ps_i : (void *)b->v.vs_i;
        mask = pixel ? &b->mask.ps_i : &b->mask.vs_i;
        size = 16;
    } else {
        bank = pixel ? (void *)b->v.ps_b : (void *)b->v.vs_b;
        mask = pixel ? &b->mask.ps_b : &b->mask.vs_b;
        size = 4;
    }
    memcpy((BYTE *)bank + (SIZE_T)start * size, data, (SIZE_T)count * size);
    for (UINT i = 0; i < count; i++)
        set_bit(mask, start + i);
}

void D9RecordPalette(D9StateBlock *b, UINT palette)
{
    b->v.palette = palette;
    b->mask.palette = TRUE;
}

void D9RecordNPatch(D9StateBlock *b, float segments)
{
    b->v.npatch = segments;
    b->mask.npatch = TRUE;
}

/* ------------------------------------------------------------------ capture and apply */

static void capture(D9StateBlock *b)
{
    D9Device *d = b->base.device;
    const D9StateMask *m = &b->mask;
    D9StateValues *v = &b->v;
    for (UINT i = 0; i < 256; i++)
        if (has_bit(m->render_states, i))
            v->render_states[i] = d->render_states[i];
    for (UINT s = 0; s < D9_MAX_STAGES; s++)
        for (UINT i = 0; i < 33; i++)
            if (m->stage_states[s] >> i & 1)
                v->stage_states[s][i] = d->stage_states[s][i];
    for (UINT s = 0; s < D9_MAX_SAMPLERS; s++)
        for (UINT i = 0; i < 14; i++)
            if (m->sampler_states[s] >> i & 1)
                v->sampler_states[s][i] = d->sampler_states[s][i];
    for (UINT i = 0; i < 512; i++)
        if (has_bit(m->transforms, i))
            D9RecordTransform(b, i, &d->transforms[i]);
    if (m->material)
        v->material = d->material;
    if (m->all_lights)
        for (UINT i = 0; i < d->light_count; i++) {
            D9RecordLight(b, d->lights[i].index, &d->lights[i].light);
            D9RecordLightEnable(b, d->lights[i].index, d->lights[i].enabled);
        }
    else
        for (UINT i = 0; i < v->light_count; i++)
            for (UINT j = 0; j < d->light_count; j++)
                if (d->lights[j].index == v->lights[i].index) {
                    if (v->lights[i].has_light)
                        v->lights[i].light = d->lights[j].light;
                    if (v->lights[i].has_enable)
                        v->lights[i].enabled = d->lights[j].enabled;
                }
    for (UINT i = 0; i < 6; i++)
        if (m->clip_planes & (1u << i))
            memcpy(v->clip_planes[i], d->clip_planes[i], 16);
    if (m->viewport)
        v->viewport = d->viewport;
    if (m->scissor)
        v->scissor = d->scissor;
    if (m->vs)
        REPLACE(v->vs, d->vs, base);
    if (m->ps)
        REPLACE(v->ps, d->ps, base);
    if (m->decl)
        REPLACE(v->decl, d->decl, base);
    for (UINT i = 0; i < D9_MAX_SAMPLERS; i++)
        if (m->textures & (1u << i))
            REPLACE(v->textures[i], d->textures[i], base);
    for (UINT i = 0; i < D9_MAX_STREAMS; i++) {
        if (m->streams & (1u << i)) {
            REPLACE(v->streams[i].buffer, d->streams[i].buffer, base);
            v->streams[i].offset = d->streams[i].offset;
            v->streams[i].stride = d->streams[i].stride;
        }
        if (m->stream_freq & (1u << i))
            v->stream_freq[i] = d->stream_freq[i];
    }
    if (m->indices)
        REPLACE(v->indices, d->indices, base);
    for (UINT i = 0; i < D9_VS_CONSTANTS; i++)
        if (has_bit(m->vs_f, i))
            memcpy(v->vs_f[i], d->vs_f[i], 16);
    for (UINT i = 0; i < D9_PS_CONSTANTS; i++)
        if (has_bit(m->ps_f, i))
            memcpy(v->ps_f[i], d->ps_f[i], 16);
    for (UINT i = 0; i < 16; i++) {
        if (m->vs_i & (1u << i))
            memcpy(v->vs_i[i], d->vs_i[i], 16);
        if (m->ps_i & (1u << i))
            memcpy(v->ps_i[i], d->ps_i[i], 16);
        if (m->vs_b & (1u << i))
            v->vs_b[i] = d->vs_b[i];
        if (m->ps_b & (1u << i))
            v->ps_b[i] = d->ps_b[i];
    }
    if (m->palette)
        v->palette = d->palette;
    if (m->npatch)
        v->npatch = d->npatch_segments;
}

/* sampler slots back to the numbers setters take: vertex samplers from D3DVERTEXTEXTURESAMPLER0. */
static DWORD sampler_number(UINT slot) { return slot < 16 ? slot : D3DDMAPSAMPLER + slot - 16; }

static void apply(D9StateBlock *b)
{
    D9Device *d = b->base.device;
    IDirect3DDevice9Ex *dev = &d->iface;
    const D9StateMask *m = &b->mask;
    const D9StateValues *v = &b->v;
    for (UINT i = 0; i < 256; i++)
        if (has_bit(m->render_states, i))
            dev->lpVtbl->SetRenderState(dev, i, v->render_states[i]);
    for (UINT s = 0; s < D9_MAX_STAGES; s++)
        for (UINT i = 0; i < 33; i++)
            if (m->stage_states[s] >> i & 1)
                dev->lpVtbl->SetTextureStageState(dev, s, i, v->stage_states[s][i]);
    for (UINT s = 0; s < D9_MAX_SAMPLERS; s++)
        for (UINT i = 0; i < 14; i++)
            if (m->sampler_states[s] >> i & 1)
                dev->lpVtbl->SetSamplerState(dev, sampler_number(s), i, v->sampler_states[s][i]);
    for (UINT i = 0; i < 512; i++)
        if (has_bit(m->transforms, i) && v->transforms)
            dev->lpVtbl->SetTransform(dev, i, &v->transforms[i]);
    if (m->material)
        dev->lpVtbl->SetMaterial(dev, &v->material);
    for (UINT i = 0; i < v->light_count; i++) {
        if (v->lights[i].has_light)
            dev->lpVtbl->SetLight(dev, v->lights[i].index, &v->lights[i].light);
        if (v->lights[i].has_enable)
            dev->lpVtbl->LightEnable(dev, v->lights[i].index, v->lights[i].enabled);
    }
    for (UINT i = 0; i < 6; i++)
        if (m->clip_planes & (1u << i))
            dev->lpVtbl->SetClipPlane(dev, i, v->clip_planes[i]);
    if (m->viewport)
        dev->lpVtbl->SetViewport(dev, &v->viewport);
    if (m->scissor)
        dev->lpVtbl->SetScissorRect(dev, &v->scissor);
    if (m->vs)
        dev->lpVtbl->SetVertexShader(dev, v->vs ? &v->vs->iface.vs : NULL);
    if (m->ps)
        dev->lpVtbl->SetPixelShader(dev, v->ps ? &v->ps->iface.ps : NULL);
    if (m->decl)
        dev->lpVtbl->SetVertexDeclaration(dev, v->decl ? &v->decl->iface : NULL);
    for (UINT i = 0; i < D9_MAX_SAMPLERS; i++)
        if (m->textures & (1u << i))
            dev->lpVtbl->SetTexture(dev, sampler_number(i), v->textures[i] ? &v->textures[i]->iface.base : NULL);
    for (UINT i = 0; i < D9_MAX_STREAMS; i++) {
        if (m->streams & (1u << i))
            dev->lpVtbl->SetStreamSource(dev, i, v->streams[i].buffer ? &v->streams[i].buffer->iface.vb : NULL,
                                         v->streams[i].offset, v->streams[i].stride);
        if (m->stream_freq & (1u << i))
            dev->lpVtbl->SetStreamSourceFreq(dev, i, v->stream_freq[i]);
    }
    if (m->indices)
        dev->lpVtbl->SetIndices(dev, v->indices ? &v->indices->iface.ib : NULL);
    for (UINT i = 0; i < D9_VS_CONSTANTS; i++)
        if (has_bit(m->vs_f, i))
            dev->lpVtbl->SetVertexShaderConstantF(dev, i, v->vs_f[i], 1);
    for (UINT i = 0; i < D9_PS_CONSTANTS; i++)
        if (has_bit(m->ps_f, i))
            dev->lpVtbl->SetPixelShaderConstantF(dev, i, v->ps_f[i], 1);
    for (UINT i = 0; i < 16; i++) {
        if (m->vs_i & (1u << i))
            dev->lpVtbl->SetVertexShaderConstantI(dev, i, v->vs_i[i], 1);
        if (m->ps_i & (1u << i))
            dev->lpVtbl->SetPixelShaderConstantI(dev, i, v->ps_i[i], 1);
        if (m->vs_b & (1u << i))
            dev->lpVtbl->SetVertexShaderConstantB(dev, i, &v->vs_b[i], 1);
        if (m->ps_b & (1u << i))
            dev->lpVtbl->SetPixelShaderConstantB(dev, i, &v->ps_b[i], 1);
    }
    if (m->palette)
        dev->lpVtbl->SetCurrentTexturePalette(dev, v->palette);
    if (m->npatch)
        dev->lpVtbl->SetNPatchMode(dev, v->npatch);
}

/* ------------------------------------------------------------------ IDirect3DStateBlock9 */

static HRESULT STDMETHODCALLTYPE block_query(IDirect3DStateBlock9 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DStateBlock9)) {
        *out = iface;
        D9ResourceAddRef(&impl(iface)->base);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE block_add_ref(IDirect3DStateBlock9 *iface) { return D9ResourceAddRef(&impl(iface)->base); }

static ULONG STDMETHODCALLTYPE block_release(IDirect3DStateBlock9 *iface)
{
    return D9ResourceRelease(&impl(iface)->base);
}

static HRESULT STDMETHODCALLTYPE block_get_device(IDirect3DStateBlock9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&impl(iface)->base, device);
}

static HRESULT STDMETHODCALLTYPE block_capture(IDirect3DStateBlock9 *iface)
{
    D9StateBlock *b = impl(iface);
    D9Device *d = b->base.device;
    D9Lock(d);
    HRESULT hr = d->recording ? D3DERR_INVALIDCALL : D3D_OK;
    if (SUCCEEDED(hr))
        capture(b);
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE block_apply(IDirect3DStateBlock9 *iface)
{
    D9StateBlock *b = impl(iface);
    D9Device *d = b->base.device;
    D9Lock(d);
    HRESULT hr = d->recording ? D3DERR_INVALIDCALL : D3D_OK;
    if (SUCCEEDED(hr))
        apply(b);
    D9Unlock(d);
    return hr;
}

static const IDirect3DStateBlock9Vtbl block_vtbl = {
    block_query, block_add_ref, block_release, block_get_device, block_capture, block_apply,
};

HRESULT D9NewStateBlock(D9Device *d, D9StateBlock **out)
{
    D9StateBlock *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return E_OUTOFMEMORY;
    b->iface.lpVtbl = &block_vtbl;
    D9ResourceInit(&b->base, d, 0, D3DPOOL_DEFAULT, 0, D3DFMT_UNKNOWN, destroy_resource);
    *out = b;
    return D3D_OK;
}

static void mark_list(DWORD *bits, const BYTE *list, UINT count)
{
    for (UINT i = 0; i < count; i++)
        set_bit(bits, list[i]);
}

HRESULT D9CreateStateBlock(D9Device *d, D3DSTATEBLOCKTYPE type, D9StateBlock **out)
{
    if (type < D3DSBT_ALL || type > D3DSBT_VERTEXSTATE)
        return D3DERR_INVALIDCALL;
    D9StateBlock *b;
    HRESULT hr = D9NewStateBlock(d, &b);
    if (FAILED(hr))
        return hr;
    D9StateMask *m = &b->mask;
    BOOL all = type == D3DSBT_ALL, pixel = type == D3DSBT_PIXELSTATE, vertex = type == D3DSBT_VERTEXSTATE;
    if (all)
        mark_list(m->render_states, ALL_RS, sizeof(ALL_RS));
    if (pixel)
        mark_list(m->render_states, PIXEL_RS, sizeof(PIXEL_RS));
    if (vertex)
        mark_list(m->render_states, VERTEX_RS, sizeof(VERTEX_RS));
    for (UINT s = 0; s < D9_MAX_STAGES; s++) {
        const BYTE *list = vertex ? VERTEX_STAGE_STATES : STAGE_STATES;
        UINT count = vertex ? sizeof(VERTEX_STAGE_STATES) : sizeof(STAGE_STATES);
        for (UINT i = 0; i < count; i++)
            m->stage_states[s] |= 1ull << list[i];
    }
    if (!vertex)
        for (UINT s = 0; s < D9_MAX_SAMPLERS; s++)
            m->sampler_states[s] = all ? 0x3FFE : 0x1FFE;
    if (all) {
        m->textures = (1u << D9_MAX_SAMPLERS) - 1;
        m->streams = (1u << D9_MAX_STREAMS) - 1;
        m->indices = m->viewport = m->scissor = m->material = TRUE;
        m->clip_planes = 0x3F;
        m->palette = TRUE;
        for (UINT i = 0; i < 512; i++)
            if (i == D3DTS_VIEW || i == D3DTS_PROJECTION || (i >= D3DTS_TEXTURE0 && i <= D3DTS_TEXTURE7) || i >= 256)
                set_bit(m->transforms, i);
    }
    if (all || vertex) {
        m->npatch = m->all_lights = m->vs = m->decl = TRUE;
        m->stream_freq = (1u << D9_MAX_STREAMS) - 1;
        memset(m->vs_f, 0xFF, sizeof(m->vs_f));
        m->vs_i = m->vs_b = 0xFFFF;
    }
    if (all || pixel) {
        m->ps = TRUE;
        memset(m->ps_f, 0xFF, sizeof(m->ps_f));
        m->ps_i = m->ps_b = 0xFFFF;
    }
    capture(b);
    *out = b;
    return D3D_OK;
}
