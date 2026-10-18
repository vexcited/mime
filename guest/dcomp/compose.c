/*
 * the compositor: walks the committed tree of a target, draws each visual's
 * content as a quad with the shaders of tools/shaders/dcomp.asm into the back
 * buffer of a swap chain on the window, and presents it. The drawing is
 * recorded on a deferred context and run on the device's immediate context
 * without disturbing the state the program left on it.
 */
#include "dcomp_private.h"
#include "dcomp_dxbc.h"

static const GUID IID_ID3D11Texture2D_ = {0x6F15AAF2, 0xD208, 0x4E89, {0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C}};
static const GUID IID_ID3D11Multithread_ = {0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};

#define CB_REGISTERS 32
#define MAX_CLIPS 4
#define MAX_DEPTH 64

enum { BLEND_OVER, BLEND_INVERT, BLEND_MIN, BLENDS };

struct DcGpu {
    DcGpu *next;
    ID3D11Device *device;
    ID3D11DeviceContext *immediate;
    ID3D11DeviceContext *deferred;
    ID3D11Multithread *multithread;
    IDXGIFactory2 *factory;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11SamplerState *samplers[2];
    ID3D11BlendState *blends[BLENDS];
    ID3D11RasterizerState *rasterizer;
    ID3D11Buffer *constants;
};

static void release(IUnknown *object)
{
    if (object)
        object->lpVtbl->Release(object);
}

static void gpu_free(DcGpu *gpu)
{
    release((IUnknown *)gpu->vs);
    release((IUnknown *)gpu->ps);
    for (UINT i = 0; i < 2; i++)
        release((IUnknown *)gpu->samplers[i]);
    for (UINT i = 0; i < BLENDS; i++)
        release((IUnknown *)gpu->blends[i]);
    release((IUnknown *)gpu->rasterizer);
    release((IUnknown *)gpu->constants);
    release((IUnknown *)gpu->factory);
    release((IUnknown *)gpu->multithread);
    release((IUnknown *)gpu->deferred);
    release((IUnknown *)gpu->immediate);
    release((IUnknown *)gpu->device);
    HeapFree(GetProcessHeap(), 0, gpu);
}

void DcGpuRelease(DcDevice *device)
{
    while (device->gpu) {
        DcGpu *gpu = device->gpu;
        device->gpu = gpu->next;
        gpu_free(gpu);
    }
}

static DcGpu *gpu_create(ID3D11Device *d3d)
{
    DcGpu *gpu = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*gpu));
    if (!gpu)
        return nullptr;
    gpu->device = d3d;
    d3d->lpVtbl->AddRef(d3d);
    d3d->lpVtbl->GetImmediateContext(d3d, &gpu->immediate);
    d3d->lpVtbl->QueryInterface(d3d, &IID_ID3D11Multithread_, (void **)&gpu->multithread);
    BOOL ok = SUCCEEDED(d3d->lpVtbl->CreateDeferredContext(d3d, 0, &gpu->deferred));
    ok = ok && SUCCEEDED(d3d->lpVtbl->CreateVertexShader(d3d, vs_dcomp, sizeof(vs_dcomp), nullptr, &gpu->vs));
    ok = ok && SUCCEEDED(d3d->lpVtbl->CreatePixelShader(d3d, ps_dcomp, sizeof(ps_dcomp), nullptr, &gpu->ps));
    for (UINT i = 0; i < 2 && ok; i++) {
        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = i ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxLOD = 1000.0f;
        ok = SUCCEEDED(d3d->lpVtbl->CreateSamplerState(d3d, &sampler, &gpu->samplers[i]));
    }
    for (UINT i = 0; i < BLENDS && ok; i++) {
        D3D11_BLEND_DESC blend = {};
        D3D11_RENDER_TARGET_BLEND_DESC *rt = &blend.RenderTarget[0];
        rt->BlendEnable = TRUE;
        rt->RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        rt->BlendOp = rt->BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (i == BLEND_OVER) {
            /* premultiplied source over. */
            rt->SrcBlend = rt->SrcBlendAlpha = D3D11_BLEND_ONE;
            rt->DestBlend = rt->DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        } else if (i == BLEND_INVERT) {
            /* the destination inverted where the source has alpha, the source's color ignored. */
            rt->SrcBlend = rt->SrcBlendAlpha = D3D11_BLEND_INV_DEST_COLOR;
            rt->DestBlend = rt->DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        } else {
            rt->SrcBlend = rt->SrcBlendAlpha = D3D11_BLEND_ONE;
            rt->DestBlend = rt->DestBlendAlpha = D3D11_BLEND_ONE;
            rt->BlendOp = rt->BlendOpAlpha = D3D11_BLEND_OP_MIN;
        }
        ok = SUCCEEDED(d3d->lpVtbl->CreateBlendState(d3d, &blend, &gpu->blends[i]));
    }
    if (ok) {
        D3D11_RASTERIZER_DESC rs = {};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        ok = SUCCEEDED(d3d->lpVtbl->CreateRasterizerState(d3d, &rs, &gpu->rasterizer));
    }
    if (ok) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = CB_REGISTERS * 16;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(d3d->lpVtbl->CreateBuffer(d3d, &desc, nullptr, &gpu->constants));
    }
    /* the factory that made the device's adapter makes the swap chains of windows. */
    IDXGIDevice *dxgi;
    if (ok && SUCCEEDED(d3d->lpVtbl->QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi))) {
        IDXGIAdapter *adapter;
        if (SUCCEEDED(dxgi->lpVtbl->GetAdapter(dxgi, &adapter))) {
            ok = SUCCEEDED(adapter->lpVtbl->GetParent(adapter, &IID_IDXGIFactory2, (void **)&gpu->factory));
            adapter->lpVtbl->Release(adapter);
        } else {
            ok = FALSE;
        }
        dxgi->lpVtbl->Release(dxgi);
    } else {
        ok = FALSE;
    }
    if (!ok) {
        gpu_free(gpu);
        return nullptr;
    }
    return gpu;
}

static DcGpu *gpu_for(DcDevice *device, ID3D11Device *d3d)
{
    for (DcGpu *gpu = device->gpu; gpu; gpu = gpu->next)
        if (gpu->device == d3d)
            return gpu;
    DcGpu *gpu = gpu_create(d3d);
    if (gpu) {
        gpu->next = device->gpu;
        device->gpu = gpu;
    }
    return gpu;
}

/* ------------------------------------------------------------------ outputs */

void DcOutputRelease(DcOutput *output)
{
    release((IUnknown *)output->rtv);
    release((IUnknown *)output->back);
    release((IUnknown *)output->swapchain);
    release((IUnknown *)output->device);
    memset(output, 0, sizeof(*output));
}

/* a swap chain of the size of the window's client area, made again when the window changes size. */
static BOOL ensure_output(DcTarget *t, DcGpu *gpu, UINT width, UINT height)
{
    DcOutput *o = &t->output;
    if (o->swapchain && (o->width != width || o->height != height)) {
        release((IUnknown *)o->rtv);
        release((IUnknown *)o->back);
        o->rtv = nullptr;
        o->back = nullptr;
        if (FAILED(o->swapchain->lpVtbl->ResizeBuffers((IDXGISwapChain1 *)o->swapchain, 0, width, height,
                                                       DXGI_FORMAT_UNKNOWN, 0))) {
            release((IUnknown *)o->swapchain);
            o->swapchain = nullptr;
        }
        o->width = width;
        o->height = height;
    }
    if (!o->swapchain) {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        /* the tree has per pixel alpha and the window shows what is behind it, so the host must too. */
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(gpu->factory->lpVtbl->CreateSwapChainForHwnd(gpu->factory, (IUnknown *)gpu->device, t->hwnd, &desc,
                                                                nullptr, nullptr, &o->swapchain)))
            return FALSE;
        o->width = width;
        o->height = height;
    }
    if (!o->back) {
        if (FAILED(o->swapchain->lpVtbl->GetBuffer(o->swapchain, 0, &IID_ID3D11Texture2D_, (void **)&o->back)))
            return FALSE;
        if (FAILED(gpu->device->lpVtbl->CreateRenderTargetView(gpu->device, (ID3D11Resource *)o->back, nullptr,
                                                               &o->rtv)))
            return FALSE;
    }
    if (!o->device) {
        o->device = gpu->device;
        gpu->device->lpVtbl->AddRef(gpu->device);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the draw list */

typedef struct DcDraw {
    ID3D11ShaderResourceView *view;
    BOOL linear;
    int blend;
    float constants[CB_REGISTERS * 4];
} DcDraw;

typedef struct DcClipState {
    /* from the visual being walked to the space of the clip: x' = a x + c y + e, y' = b x + d y + f. */
    float m[6];
    float rect[4];
    float radii[8];
} DcClipState;

typedef struct DcWalk {
    float matrix[16];
    float opacity;
    int interpolation, composite, backface;
    BOOL has_color;
    float color[20];
    UINT clip_count;
    DcClipState clips[MAX_CLIPS];
} DcWalk;

typedef struct DcBuild {
    DcTarget *target;
    DcGpu *gpu;
    UINT64 now;
    UINT width, height;
    DcDraw *draws;
    UINT draw_count, draw_capacity;
    BOOL failed;
} DcBuild;

/* a matrix of 3 by 2 in the affine form of the clips, multiplied: first applies a. */
static void affine_multiply(float out[6], const float a[6], const float b[6])
{
    float r[6];
    r[0] = a[0] * b[0] + a[1] * b[2];
    r[1] = a[0] * b[1] + a[1] * b[3];
    r[2] = a[2] * b[0] + a[3] * b[2];
    r[3] = a[2] * b[1] + a[3] * b[3];
    r[4] = a[4] * b[0] + a[5] * b[2] + b[4];
    r[5] = a[4] * b[1] + a[5] * b[3] + b[5];
    memcpy(out, r, sizeof(r));
}

/*
 * pieces of the color transforms of filter effects, as five rows of four applied to a row of red, green, blue,
 * alpha and one: first applies a.
 */
static void color_multiply(float out[20], const float a[20], const float b[20])
{
    float r[20];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) {
            float sum = i == 4 ? b[16 + j] : 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = sum;
        }
    memcpy(out, r, sizeof(r));
}

static BOOL is_affine(const float m[16])
{
    return m[2] == 0.0f && m[3] == 0.0f && m[6] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 1.0f;
}

static DcDraw *add_draw(DcBuild *c)
{
    if (c->draw_count == c->draw_capacity) {
        UINT capacity = c->draw_capacity ? c->draw_capacity * 2 : 16;
        DcDraw *grown = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*grown));
        if (!grown) {
            c->failed = TRUE;
            return nullptr;
        }
        if (c->draw_count)
            memcpy(grown, c->draws, c->draw_count * sizeof(*grown));
        if (c->draws)
            HeapFree(GetProcessHeap(), 0, c->draws);
        c->draws = grown;
        c->draw_capacity = capacity;
    }
    return &c->draws[c->draw_count++];
}

/* draws one content: a quad of the shown part of its frame, with the clips and look the walk has gathered. */
static void draw_content(DcBuild *c, const DcVisualState *s, const DcWalk *w, DcContent *content)
{
    DcFrame frame;
    if (!DcContentFrame(content, c->gpu->device, &frame))
        return;
    float width = (float)(frame.source.right - frame.source.left);
    float height = (float)(frame.source.bottom - frame.source.top);
    if (width <= 0 || height <= 0 || !frame.width || !frame.height)
        return;
    /* the quad's matrix: the unit square to the content's pixels, the swap chain's own scale, the walk, then clip space. */
    float m[16], step[16];
    DcMatrixIdentity(m);
    m[0] = width;
    m[5] = height;
    if (frame.has_matrix) {
        DcMatrixIdentity(step);
        step[0] = frame.matrix[0];
        step[5] = frame.matrix[3];
        step[12] = frame.matrix[4];
        step[13] = frame.matrix[5];
        DcMatrixMultiply(m, m, step);
    }
    DcMatrixMultiply(m, m, w->matrix);
    DcMatrixIdentity(step);
    step[0] = 2.0f / (float)c->width;
    step[5] = -2.0f / (float)c->height;
    step[12] = -1.0f;
    step[13] = 1.0f;
    DcMatrixMultiply(m, m, step);
    /* a face turned away is not drawn when the visual says so. */
    if (w->backface == DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN) {
        float x0 = m[12] / m[15], y0 = m[13] / m[15];
        float x1 = (m[0] + m[12]) / (m[3] + m[15]), y1 = (m[1] + m[13]) / (m[3] + m[15]);
        float x2 = (m[4] + m[12]) / (m[7] + m[15]), y2 = (m[5] + m[13]) / (m[7] + m[15]);
        if ((x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0) > 0.0f)
            return;
    }
    DcDraw *d = add_draw(c);
    if (!d)
        return;
    memset(d->constants, 0, sizeof(d->constants));
    float *cb = d->constants;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            cb[col * 4 + row] = m[row * 4 + col];
    cb[16] = (float)frame.source.left / (float)frame.width;
    cb[17] = (float)frame.source.top / (float)frame.height;
    cb[18] = width / (float)frame.width;
    cb[19] = height / (float)frame.height;
    cb[20] = width;
    cb[21] = height;
    cb[22] = w->opacity;
    cb[23] = frame.alpha == DXGI_ALPHA_MODE_STRAIGHT ? 1.0f : frame.alpha == DXGI_ALPHA_MODE_IGNORE ? 2.0f : 0.0f;
    cb[24] = w->composite == DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT ? 1.0f : 0.0f;
    cb[25] = w->has_color ? 1.0f : 0.0f;
    /* the clips are in the visual's space; the content is moved into it by the swap chain's scale first. */
    float into[6] = {1, 0, 0, 1, 0, 0};
    if (frame.has_matrix) {
        into[0] = frame.matrix[0];
        into[3] = frame.matrix[3];
        into[4] = frame.matrix[4];
        into[5] = frame.matrix[5];
    }
    for (UINT i = 0; i < w->clip_count; i++) {
        float t[6];
        affine_multiply(t, into, w->clips[i].m);
        float *r = cb + 28 + 20 * i;
        r[0] = t[0];
        r[1] = t[2];
        r[2] = t[4];
        r[3] = 1.0f;
        r[4] = t[1];
        r[5] = t[3];
        r[6] = t[5];
        memcpy(r + 8, w->clips[i].rect, sizeof(w->clips[i].rect));
        memcpy(r + 12, w->clips[i].radii, sizeof(w->clips[i].radii));
    }
    const float identity[20] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    const float *color = w->has_color ? w->color : identity;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            cb[108 + col * 4 + row] = color[row * 4 + col];
    for (int j = 0; j < 4; j++)
        cb[124 + j] = color[16 + j];
    d->view = frame.view;
    d->linear = w->interpolation != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    d->blend = w->composite == DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT ? BLEND_INVERT
               : w->composite == DCOMPOSITION_COMPOSITE_MODE_MIN_BLEND        ? BLEND_MIN
                                                                              : BLEND_OVER;
}

static void walk(DcBuild *c, const DcVisual *visual, const DcWalk *parent, UINT depth)
{
    const DcVisualState *s = &visual->committed;
    if (!s->visible || depth > MAX_DEPTH)
        return;
    DcWalk w = *parent;
    float local[16];
    DcMatrixIdentity(local);
    if (s->transform_kind == 1) {
        DcTransformMatrix(s->transform, c->now, local);
    } else if (s->transform_kind == 2 || s->transform_kind == 3) {
        memcpy(local, s->matrix, sizeof(local));
    }
    float effect_opacity = 1.0f, effect_transform[16];
    BOOL has_transform = FALSE;
    if (s->effect && DcEffectOpacity(s->effect, c->now, &effect_opacity, effect_transform, &has_transform) &&
        has_transform)
        DcMatrixMultiply(local, local, effect_transform);
    float shift[16];
    DcMatrixIdentity(shift);
    shift[12] = DcFloatValue(&s->offset_x, c->now);
    shift[13] = DcFloatValue(&s->offset_y, c->now);
    shift[14] = DcFloatValue(&s->offset_z, c->now);
    DcMatrixMultiply(local, local, shift);
    DcMatrixMultiply(w.matrix, local, parent->matrix);
    w.opacity = parent->opacity * DcFloatValue(&s->opacity, c->now) * effect_opacity;
    if (s->interpolation != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT)
        w.interpolation = s->interpolation;
    if (s->composite_mode != DCOMPOSITION_COMPOSITE_MODE_INHERIT)
        w.composite = s->composite_mode;
    if (s->backface != DCOMPOSITION_BACKFACE_VISIBILITY_INHERIT)
        w.backface = s->backface;
    float color[20];
    if (s->effect && DcFilterColor(s->effect, c->now, color)) {
        if (w.has_color)
            color_multiply(w.color, color, parent->color);
        else
            memcpy(w.color, color, sizeof(color));
        w.has_color = TRUE;
    }
    /* the clips of the visual's ancestors move into its own space, and its own comes with it. */
    BOOL affine = is_affine(local);
    float to_parent[6] = {local[0], local[1], local[4], local[5], local[12], local[13]};
    UINT n = 0;
    for (UINT i = 0; i < parent->clip_count && affine; i++) {
        w.clips[n] = parent->clips[i];
        affine_multiply(w.clips[n].m, to_parent, parent->clips[i].m);
        n++;
    }
    w.clip_count = n;
    if (s->clip_kind && w.clip_count < MAX_CLIPS) {
        DcClipState *clip = &w.clips[w.clip_count];
        memset(clip, 0, sizeof(*clip));
        clip->m[0] = clip->m[3] = 1.0f;
        if (s->clip_kind == 1) {
            clip->rect[0] = s->clip_rect.left;
            clip->rect[1] = s->clip_rect.top;
            clip->rect[2] = s->clip_rect.right;
            clip->rect[3] = s->clip_rect.bottom;
            w.clip_count++;
        } else if (DcClipValues(s->clip, c->now, clip->rect, clip->radii)) {
            w.clip_count++;
        }
    }
    if (s->content)
        draw_content(c, s, &w, s->content);
    for (UINT i = 0; i < s->child_count; i++)
        walk(c, s->children[i], &w, depth + 1);
}

/* ------------------------------------------------------------------ composing */

/* the first device that can draw a content of the tree, for a device made without one. */
static ID3D11Device *tree_device(const DcVisual *visual, UINT depth)
{
    const DcVisualState *s = &visual->committed;
    if (depth > MAX_DEPTH)
        return nullptr;
    if (s->content) {
        DcContent *content = s->content;
        ID3D11Texture2D *texture = nullptr;
        if (content->kind == DCC_SWAPCHAIN) {
            MIME_COMPOSITION_FRAME frame;
            if (content->source->lpVtbl->GetFrame(content->source, &frame) == S_OK)
                texture = (ID3D11Texture2D *)frame.Texture;
        } else if (content->kind == DCC_SURFACE) {
            texture = CONTAINING_RECORD(content->object, DcSurface, base)->texture;
            if (texture)
                texture->lpVtbl->AddRef(texture);
        }
        if (texture) {
            ID3D11Device *device;
            texture->lpVtbl->GetDevice(texture, &device);
            device->lpVtbl->Release(device);
            texture->lpVtbl->Release(texture);
            return device;
        }
    }
    for (UINT i = 0; i < s->child_count; i++) {
        ID3D11Device *device = tree_device(s->children[i], depth + 1);
        if (device)
            return device;
    }
    return nullptr;
}

HRESULT DcCompose(DcTarget *t)
{
    DcDevice *d = t->base.device;
    t->needs_compose = FALSE;
    RECT client;
    if (!IsWindow(t->hwnd) || !GetClientRect(t->hwnd, &client))
        return S_FALSE;
    UINT width = client.right - client.left, height = client.bottom - client.top;
    if (!width || !height)
        return S_FALSE;
    ID3D11Device *d3d = d->d3d ? d->d3d : t->output.device;
    if (!d3d && t->root_committed)
        d3d = tree_device(t->root_committed, 0);
    if (!d3d)
        return S_FALSE;
    DcGpu *gpu = gpu_for(d, d3d);
    if (!gpu || !ensure_output(t, gpu, width, height))
        return E_FAIL;

    DcBuild c = {t, gpu, DcNow(), width, height};
    if (t->root_committed) {
        DcWalk w = {};
        DcMatrixIdentity(w.matrix);
        w.opacity = 1.0f;
        w.interpolation = DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR;
        w.composite = DCOMPOSITION_COMPOSITE_MODE_SOURCE_OVER;
        w.backface = DCOMPOSITION_BACKFACE_VISIBILITY_VISIBLE;
        walk(&c, t->root_committed, &w, 0);
    }
    if (c.failed) {
        if (c.draws)
            HeapFree(GetProcessHeap(), 0, c.draws);
        return E_OUTOFMEMORY;
    }

    if (gpu->multithread)
        gpu->multithread->lpVtbl->Enter(gpu->multithread);
    ID3D11DeviceContext *ctx = gpu->deferred;
    const float clear[4] = {0, 0, 0, 0};
    D3D11_VIEWPORT viewport = {0, 0, (float)width, (float)height, 0.0f, 1.0f};
    ctx->lpVtbl->OMSetRenderTargets(ctx, 1, &t->output.rtv, nullptr);
    ctx->lpVtbl->RSSetViewports(ctx, 1, &viewport);
    ctx->lpVtbl->RSSetState(ctx, gpu->rasterizer);
    ctx->lpVtbl->ClearRenderTargetView(ctx, t->output.rtv, clear);
    ctx->lpVtbl->IASetPrimitiveTopology(ctx, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->lpVtbl->VSSetShader(ctx, gpu->vs, nullptr, 0);
    ctx->lpVtbl->PSSetShader(ctx, gpu->ps, nullptr, 0);
    ctx->lpVtbl->VSSetConstantBuffers(ctx, 0, 1, &gpu->constants);
    ctx->lpVtbl->PSSetConstantBuffers(ctx, 0, 1, &gpu->constants);
    for (UINT i = 0; i < c.draw_count; i++) {
        const DcDraw *draw = &c.draws[i];
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)gpu->constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            continue;
        memcpy(mapped.pData, draw->constants, sizeof(draw->constants));
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)gpu->constants, 0);
        ctx->lpVtbl->PSSetShaderResources(ctx, 0, 1, &draw->view);
        ctx->lpVtbl->PSSetSamplers(ctx, 0, 1, &gpu->samplers[draw->linear ? 1 : 0]);
        ctx->lpVtbl->OMSetBlendState(ctx, gpu->blends[draw->blend], nullptr, 0xFFFFFFFF);
        ctx->lpVtbl->Draw(ctx, 4, 0);
    }
    ID3D11ShaderResourceView *none = nullptr;
    ctx->lpVtbl->PSSetShaderResources(ctx, 0, 1, &none);
    ctx->lpVtbl->OMSetRenderTargets(ctx, 0, nullptr, nullptr);
    ID3D11CommandList *list;
    HRESULT hr = ctx->lpVtbl->FinishCommandList(ctx, FALSE, &list);
    if (SUCCEEDED(hr)) {
        gpu->immediate->lpVtbl->ExecuteCommandList(gpu->immediate, list, TRUE);
        list->lpVtbl->Release(list);
        /* the window shows the frame by the swap chain's own present, without waiting for the display. */
        t->output.swapchain->lpVtbl->Present(t->output.swapchain, 0, 0);
        t->output.composed = TRUE;
    }
    if (gpu->multithread)
        gpu->multithread->lpVtbl->Leave(gpu->multithread);
    if (c.draws)
        HeapFree(GetProcessHeap(), 0, c.draws);
    return hr;
}

/* the targets that are flagged, or all of them. */
void DcComposeAll(DcDevice *device, BOOL only_flagged)
{
    for (DcTarget *t = device->targets; t; t = t->next_in_device)
        if (!only_flagged || t->needs_compose)
            DcCompose(t);
}

/* whether a visual or one under it shows a source. */
static BOOL uses_source(const DcVisual *visual, IUnknown *source, UINT depth)
{
    const DcVisualState *s = &visual->committed;
    if (depth > MAX_DEPTH)
        return FALSE;
    if (s->content && s->content->kind == DCC_SWAPCHAIN && (IUnknown *)s->content->source == source)
        return TRUE;
    for (UINT i = 0; i < s->child_count; i++)
        if (uses_source(s->children[i], source, depth + 1))
            return TRUE;
    return FALSE;
}

/* a swap chain presented: the targets that show it draw again. */
void DcSourceFrame(DcDevice *device, IUnknown *chain)
{
    IMimeDXGICompositionSource *source;
    if (FAILED(chain->lpVtbl->QueryInterface(chain, &IID_IMimeDXGICompositionSource, (void **)&source)))
        return;
    for (DcTarget *t = device->targets; t; t = t->next_in_device)
        if (t->root_committed && uses_source(t->root_committed, (IUnknown *)source, 0))
            DcCompose(t);
    source->lpVtbl->Release(source);
}

void WINAPI DcSourceNotify(void *context, IUnknown *source)
{
    DcDevice *device = DcDeviceTryRef(context);
    if (!device)
        return;
    DcDeviceLock(device);
    DcSourceFrame(device, source);
    DcDeviceUnlock(device);
    DcDeviceRelease(device);
}

/* ------------------------------------------------------------------ reading back */

/* the bytes of a rectangle of the last frame, through a staging copy of the back buffer. */
HRESULT DcReadTarget(DcTarget *t, const RECT *rect, void *pixels, UINT pitch)
{
    DcOutput *o = &t->output;
    if (!o->composed || !o->back)
        return S_FALSE;
    RECT whole = {0, 0, (LONG)o->width, (LONG)o->height}, r = rect ? *rect : whole, clipped;
    if (!IntersectRect(&clipped, &r, &whole) || !EqualRect(&clipped, &r))
        return E_INVALIDARG;
    DcGpu *gpu = gpu_for(t->base.device, o->device);
    if (!gpu)
        return E_FAIL;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = r.right - r.left;
    desc.Height = r.bottom - r.top;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging;
    HRESULT hr = gpu->device->lpVtbl->CreateTexture2D(gpu->device, &desc, nullptr, &staging);
    if (FAILED(hr))
        return hr;
    D3D11_BOX box = {(UINT)r.left, (UINT)r.top, 0, (UINT)r.right, (UINT)r.bottom, 1};
    if (gpu->multithread)
        gpu->multithread->lpVtbl->Enter(gpu->multithread);
    gpu->immediate->lpVtbl->CopySubresourceRegion(gpu->immediate, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                                  (ID3D11Resource *)o->back, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = gpu->immediate->lpVtbl->Map(gpu->immediate, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < desc.Height; y++)
            memcpy((BYTE *)pixels + (SIZE_T)y * pitch, (BYTE *)mapped.pData + (SIZE_T)y * mapped.RowPitch,
                   (SIZE_T)desc.Width * 4);
        gpu->immediate->lpVtbl->Unmap(gpu->immediate, (ID3D11Resource *)staging, 0);
    }
    if (gpu->multithread)
        gpu->multithread->lpVtbl->Leave(gpu->multithread);
    staging->lpVtbl->Release(staging);
    return hr;
}
