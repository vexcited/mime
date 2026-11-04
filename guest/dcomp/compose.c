/*
 * the compositor: walks a target's committed tree, draws each visual's
 * content as a quad into the back buffer of a swap chain on the window, and
 * presents it. drawing is recorded on a deferred context and run on the
 * immediate context without disturbing the state the program left on it.
 * a tree whose device has no gpu is composed by the cpu into a bitmap that
 * gdi puts on the window.
 */
#include "dcomp_private.h"
#include "dcomp_dxbc.h"
#include <mime/ntuser.h>

static const GUID IID_ID3D11Texture2D_ = {0x6F15AAF2, 0xD208, 0x4E89, {0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C}};
static const GUID IID_ID3D11Multithread_ = {0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};

#define CB_REGISTERS 32
#define MAX_CLIPS 4
#define MAX_DEPTH 64
#define D_MIN(a, b) ((a) < (b) ? (a) : (b))

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
    /* one opaque white texel, which the color matrix turns into a solid color. */
    ID3D11ShaderResourceView *white;
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
    release((IUnknown *)gpu->white);
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
            /* inverts the destination where the source has alpha, ignoring the source color. */
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
    if (ok) {
        static const UINT texel = 0xFFFFFFFF;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {&texel, 4, 4};
        ID3D11Texture2D *texture;
        ok = SUCCEEDED(d3d->lpVtbl->CreateTexture2D(d3d, &desc, &data, &texture));
        if (ok) {
            ok = SUCCEEDED(
                d3d->lpVtbl->CreateShaderResourceView(d3d, (ID3D11Resource *)texture, nullptr, &gpu->white));
            texture->lpVtbl->Release(texture);
        }
    }
    /* window swap chains come from the factory that made the device's adapter. */
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
    if (output->dc) {
        SelectObject(output->dc, output->old_bitmap);
        DeleteDC(output->dc);
    }
    if (output->bitmap)
        DeleteObject(output->bitmap);
    memset(output, 0, sizeof(*output));
}

/* the swap chain matches the client area and is remade when the window resizes. */
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
        /* the tree has per-pixel alpha and the window shows what is behind it, so the host must too. */
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
    /* the bitmap the cpu composes into, instead of the gpu's draw list. */
    BYTE *bits;
    DcDraw *draws;
    UINT draw_count, draw_capacity;
    BOOL failed;
} DcBuild;

/* multiplies 3x2 affine matrices, a applies first. */
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
 * multiplies filter effect color transforms: five rows of four, applied to a row of red, green, blue, alpha and one.
 * a applies first.
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

/*
 * places content of a size in the box of a sprite's layout: scales and moves the content to visual matrix, a 3x2
 * affine one, and clips the walk to the box.
 */
static void apply_layout(const DcLayout *l, float width, float height, float into[6], DcWalk *w)
{
    float cw = width * into[0], ch = height * into[3], kx = 1.0f, ky = 1.0f;
    if (cw <= 0.0f || ch <= 0.0f)
        return;
    if (l->solid || l->stretch == 1) {
        kx = l->width / cw;
        ky = l->height / ch;
    } else if (l->stretch == 2 || l->stretch == 3) {
        float x = l->width / cw, y = l->height / ch;
        BOOL smaller = x < y;
        /* uniform fits the whole content in the box, uniform to fill covers the box. */
        kx = ky = l->stretch == 2 ? (smaller ? x : y) : (smaller ? y : x);
    }
    float ox = (l->width - cw * kx) * l->align_x, oy = (l->height - ch * ky) * l->align_y;
    into[0] *= kx;
    into[3] *= ky;
    into[4] = into[4] * kx + ox;
    into[5] = into[5] * ky + oy;
    if (w->clip_count < MAX_CLIPS) {
        DcClipState *clip = &w->clips[w->clip_count++];
        memset(clip, 0, sizeof(*clip));
        clip->m[0] = clip->m[3] = 1.0f;
        clip->rect[2] = l->width;
        clip->rect[3] = l->height;
    }
}

/* draws the shown part of a content's frame as a quad, with the clips and look gathered by the walk. */
static void draw_software(DcBuild *c, const DcVisualState *s, const DcWalk *w, DcContent *content);

static void draw_content(DcBuild *c, const DcVisualState *s, const DcWalk *walk, DcContent *content)
{
    if (c->bits) {
        draw_software(c, s, walk, content);
        return;
    }
    const DcLayout *layout = &s->layout;
    BOOL solid = layout->on && layout->solid;
    DcFrame frame;
    if (solid) {
        memset(&frame, 0, sizeof(frame));
        frame.view = c->gpu->white;
        frame.width = frame.height = 1;
        frame.source = (RECT){0, 0, 1, 1};
        frame.alpha = DXGI_ALPHA_MODE_PREMULTIPLIED;
    } else if (!content || !DcContentFrame(content, c->gpu->device, &frame)) {
        return;
    }
    float width = (float)(frame.source.right - frame.source.left);
    float height = (float)(frame.source.bottom - frame.source.top);
    if (width <= 0 || height <= 0 || !frame.width || !frame.height)
        return;
    /* content pixels to the visual: the swap chain's own scale, then the layout of a sprite. */
    float into[6] = {1, 0, 0, 1, 0, 0};
    if (frame.has_matrix) {
        into[0] = frame.matrix[0];
        into[3] = frame.matrix[3];
        into[4] = frame.matrix[4];
        into[5] = frame.matrix[5];
    }
    DcWalk local = *walk;
    const DcWalk *w = &local;
    if (layout->on)
        apply_layout(layout, width, height, into, &local);
    /* unit square to content pixels, then into the visual, the walk, and clip space. */
    float m[16], step[16];
    DcMatrixIdentity(m);
    m[0] = width;
    m[5] = height;
    DcMatrixIdentity(step);
    step[0] = into[0];
    step[5] = into[3];
    step[12] = into[4];
    step[13] = into[5];
    DcMatrixMultiply(m, m, step);
    DcMatrixMultiply(m, m, w->matrix);
    DcMatrixIdentity(step);
    step[0] = 2.0f / (float)c->width;
    step[5] = -2.0f / (float)c->height;
    step[12] = -1.0f;
    step[13] = 1.0f;
    DcMatrixMultiply(m, m, step);
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
    cb[25] = w->has_color || solid ? 1.0f : 0.0f;
    /* clips are in the visual's space, which the content goes into first. */
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
    float fill[20] = {};
    if (solid) {
        /* the white texel times nothing, plus the color. */
        memcpy(fill + 16, layout->color, sizeof(layout->color));
        if (w->has_color)
            color_multiply(fill, fill, w->color);
        color = fill;
    }
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            cb[108 + col * 4 + row] = color[row * 4 + col];
    for (int j = 0; j < 4; j++)
        cb[124 + j] = color[16 + j];
    d->view = frame.view;
    d->linear = w->interpolation != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    if (layout->on && !solid)
        d->linear = layout->interpolation != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
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
    /* ancestor clips move into this visual's space, then its own clip is added. */
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
    if (s->content || (s->layout.on && s->layout.solid))
        draw_content(c, s, &w, s->content);
    for (UINT i = 0; i < s->child_count; i++)
        walk(c, s->children[i], &w, depth + 1);
}

/* ------------------------------------------------------------------ the cpu */

/* the destination pixels of an axis-aligned clip, from its space through the visual's matrix; false for others. */
static BOOL clip_pixels(const DcClipState *clip, const float m[16], float r[4])
{
    if (clip->m[1] != 0.0f || clip->m[2] != 0.0f || clip->m[0] == 0.0f || clip->m[3] == 0.0f)
        return FALSE;
    float x0 = (clip->rect[0] - clip->m[4]) / clip->m[0], x1 = (clip->rect[2] - clip->m[4]) / clip->m[0];
    float y0 = (clip->rect[1] - clip->m[5]) / clip->m[3], y1 = (clip->rect[3] - clip->m[5]) / clip->m[3];
    x0 = m[0] * x0 + m[12];
    x1 = m[0] * x1 + m[12];
    y0 = m[5] * y0 + m[13];
    y1 = m[5] * y1 + m[13];
    r[0] = x0 < x1 ? x0 : x1;
    r[2] = x0 < x1 ? x1 : x0;
    r[1] = y0 < y1 ? y0 : y1;
    r[3] = y0 < y1 ? y1 : y0;
    return TRUE;
}

/*
 * draws a swap chain frame of the software device over the bitmap: scaled and moved, with opacity and
 * axis-aligned clips. rotations, color effects and blend modes other than source over are not drawn.
 */
static void draw_software(DcBuild *c, const DcVisualState *s, const DcWalk *walk, DcContent *content)
{
    const DcLayout *layout = &s->layout;
    BOOL solid = layout->on && layout->solid;
    MIME_COMPOSITION_FRAME frame = {};
    IMimeSoftwareTexture *texture = nullptr;
    if (solid) {
        frame.Source = (RECT){0, 0, 1, 1};
        frame.Matrix._11 = frame.Matrix._22 = 1.0f;
    } else {
        if (!content || content->kind != DCC_SWAPCHAIN ||
            content->source->lpVtbl->GetFrame(content->source, &frame) != S_OK)
            return;
        HRESULT hr =
            frame.Texture->lpVtbl->QueryInterface(frame.Texture, &IID_IMimeSoftwareTexture, (void **)&texture);
        frame.Texture->lpVtbl->Release(frame.Texture);
        if (FAILED(hr))
            return;
    }
    float width = (float)(frame.Source.right - frame.Source.left);
    float height = (float)(frame.Source.bottom - frame.Source.top);
    float into[6] = {frame.Matrix._11, 0, 0, frame.Matrix._22, frame.Matrix._31, frame.Matrix._32};
    DcWalk local = *walk;
    const DcWalk *w = &local;
    if (layout->on)
        apply_layout(layout, width, height, into, &local);
    float m[16], step[16];
    DcMatrixIdentity(m);
    m[0] = width;
    m[5] = height;
    DcMatrixIdentity(step);
    step[0] = into[0];
    step[5] = into[3];
    step[12] = into[4];
    step[13] = into[5];
    DcMatrixMultiply(m, m, step);
    DcMatrixMultiply(m, m, w->matrix);
    UINT opacity = w->opacity >= 1.0f ? 255 : w->opacity <= 0.0f ? 0 : (UINT)(w->opacity * 255.0f + 0.5f);
    if (width <= 0 || height <= 0 || !opacity || !is_affine(m) || m[1] != 0.0f || m[4] != 0.0f || m[0] <= 0.0f ||
        m[5] <= 0.0f) {
        if (texture)
            texture->lpVtbl->Release(texture);
        return;
    }
    float x0 = m[12], y0 = m[13], sx = width / m[0], sy = height / m[5];
    float r[4] = {x0, y0, x0 + m[0], y0 + m[5]};
    for (UINT i = 0; i < w->clip_count; i++) {
        float clip[4];
        if (!clip_pixels(&w->clips[i], w->matrix, clip))
            continue;
        r[0] = r[0] > clip[0] ? r[0] : clip[0];
        r[1] = r[1] > clip[1] ? r[1] : clip[1];
        r[2] = r[2] < clip[2] ? r[2] : clip[2];
        r[3] = r[3] < clip[3] ? r[3] : clip[3];
    }
    int left = (int)(r[0] + 0.5f), top = (int)(r[1] + 0.5f), right = (int)(r[2] + 0.5f), bottom = (int)(r[3] + 0.5f);
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > (int)c->width ? (int)c->width : right;
    bottom = bottom > (int)c->height ? (int)c->height : bottom;
    if (solid) {
        UINT b = (UINT)(layout->color[2] * opacity + 0.5f), g = (UINT)(layout->color[1] * opacity + 0.5f);
        UINT red = (UINT)(layout->color[0] * opacity + 0.5f), a = (UINT)(layout->color[3] * opacity + 0.5f);
        for (int y = top; y < bottom; y++) {
            BYTE *d = c->bits + ((SIZE_T)y * c->width + (SIZE_T)left) * 4;
            for (int x = left; x < right; x++, d += 4) {
                UINT keep = 255 - a;
                d[0] = (BYTE)D_MIN(255u, b + d[0] * keep / 255);
                d[1] = (BYTE)D_MIN(255u, g + d[1] * keep / 255);
                d[2] = (BYTE)D_MIN(255u, red + d[2] * keep / 255);
                d[3] = (BYTE)D_MIN(255u, a + d[3] * keep / 255);
            }
        }
        return;
    }
    BOOL rgba = frame.ViewFormat == DXGI_FORMAT_R8G8B8A8_UNORM;
    BOOL opaque = frame.AlphaMode == DXGI_ALPHA_MODE_IGNORE || frame.ViewFormat == DXGI_FORMAT_B8G8R8X8_UNORM;
    const BYTE *bits;
    UINT pitch;
    if (left < right && top < bottom && SUCCEEDED(texture->lpVtbl->Lock(texture, &bits, &pitch))) {
        for (int y = top; y < bottom; y++) {
            int v = frame.Source.top + (int)(((float)y + 0.5f - y0) * sy);
            v = v < frame.Source.top ? frame.Source.top : v >= frame.Source.bottom ? frame.Source.bottom - 1 : v;
            const BYTE *row = bits + (SIZE_T)v * pitch;
            BYTE *d = c->bits + ((SIZE_T)y * c->width + (SIZE_T)left) * 4;
            for (int x = left; x < right; x++, d += 4) {
                int u = frame.Source.left + (int)(((float)x + 0.5f - x0) * sx);
                u = u < frame.Source.left ? frame.Source.left : u >= frame.Source.right ? frame.Source.right - 1 : u;
                const BYTE *p = row + (SIZE_T)u * 4;
                UINT b = rgba ? p[2] : p[0], g = p[1], red = rgba ? p[0] : p[2], a = opaque ? 255 : p[3];
                if (opacity != 255) {
                    b = b * opacity / 255;
                    g = g * opacity / 255;
                    red = red * opacity / 255;
                    a = a * opacity / 255;
                }
                if (a == 255) {
                    d[0] = (BYTE)b;
                    d[1] = (BYTE)g;
                    d[2] = (BYTE)red;
                    d[3] = 255;
                } else if (a) {
                    /* premultiplied source over. */
                    UINT keep = 255 - a;
                    d[0] = (BYTE)D_MIN(255u, b + d[0] * keep / 255);
                    d[1] = (BYTE)D_MIN(255u, g + d[1] * keep / 255);
                    d[2] = (BYTE)D_MIN(255u, red + d[2] * keep / 255);
                    d[3] = (BYTE)D_MIN(255u, a + d[3] * keep / 255);
                }
            }
        }
        texture->lpVtbl->Unlock(texture);
    }
    texture->lpVtbl->Release(texture);
}

/* the whole tree into the bitmap, which then goes to the window. */
static HRESULT compose_software(DcTarget *t, UINT width, UINT height)
{
    DcOutput *o = &t->output;
    if (o->bits && (o->width != width || o->height != height))
        DcOutputRelease(o);
    if (!o->bits) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = (LONG)width;
        info.bmiHeader.biHeight = -(LONG)height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        o->dc = CreateCompatibleDC(nullptr);
        o->bitmap = o->dc ? CreateDIBSection(o->dc, &info, DIB_RGB_COLORS, (void **)&o->bits, nullptr, 0) : nullptr;
        if (!o->bitmap) {
            DcOutputRelease(o);
            return E_OUTOFMEMORY;
        }
        o->old_bitmap = SelectObject(o->dc, o->bitmap);
        o->width = width;
        o->height = height;
    }
    memset(o->bits, 0, (SIZE_T)width * height * 4);
    DcBuild c = {t, nullptr, DcNow(), width, height, o->bits};
    if (t->root_committed) {
        DcWalk w = {};
        DcMatrixIdentity(w.matrix);
        w.opacity = 1.0f;
        w.interpolation = DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR;
        w.composite = DCOMPOSITION_COMPOSITE_MODE_SOURCE_OVER;
        w.backface = DCOMPOSITION_BACKFACE_VISIBILITY_VISIBLE;
        walk(&c, t->root_committed, &w, 0);
    }
    GdiFlush();
    HDC dc = GetDC(t->hwnd);
    if (!dc)
        return E_FAIL;
    BitBlt(dc, 0, 0, (int)width, (int)height, o->dc, 0, 0, SRCCOPY);
    ReleaseDC(t->hwnd, dc);
    /* a window without a redirection bitmap shows its surface once it is known to hold the composition. */
    HWND root = GetAncestor(t->hwnd, GA_ROOT);
    if (root && !GetPropW(root, MAKEINTATOM(MIME_PROP_COMPOSITION)))
        SetPropW(root, MAKEINTATOM(MIME_PROP_COMPOSITION), (HANDLE)1);
    o->composed = TRUE;
    return S_OK;
}

/* ------------------------------------------------------------------ composing */

/* for a device made without one: the first device that can draw some content of the tree. */
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

static BOOL tree_solid(const DcVisual *visual, UINT depth)
{
    const DcVisualState *s = &visual->committed;
    if (depth > MAX_DEPTH)
        return FALSE;
    if (s->layout.on && s->layout.solid)
        return TRUE;
    for (UINT i = 0; i < s->child_count; i++)
        if (tree_solid(s->children[i], depth + 1))
            return TRUE;
    return FALSE;
}

/* a device of the compositor's own for a tree with nothing of a program's device to draw. */
static ID3D11Device *own_device(DcDevice *d)
{
    if (!d->own_d3d) {
        HMODULE module = LoadLibraryW(L"d3d11.dll");
        PFN_D3D11_CREATE_DEVICE create =
            module ? (PFN_D3D11_CREATE_DEVICE)(void *)GetProcAddress(module, "D3D11CreateDevice") : nullptr;
        if (!create || FAILED(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, &d->own_d3d, nullptr, nullptr)))
            d->own_d3d = nullptr;
    }
    return d->own_d3d;
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
    /* a device made without one draws with the device of its content, the last one when there is none. */
    ID3D11Device *d3d = d->d3d;
    if (!d3d && t->root_committed)
        d3d = tree_device(t->root_committed, 0);
    if (!d3d)
        d3d = t->output.device;
    if (!d3d && t->root_committed && tree_solid(t->root_committed, 0)) {
        d3d = own_device(d);
        if (!d3d)
            return compose_software(t, width, height);
    }
    if (!d3d)
        return S_FALSE;
    if (t->output.device && t->output.device != d3d)
        DcOutputRelease(&t->output);
    IDXGIDevice *dxgi;
    if (FAILED(d3d->lpVtbl->QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi)))
        return compose_software(t, width, height);
    dxgi->lpVtbl->Release(dxgi);
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
        /* present without waiting for the display. */
        t->output.swapchain->lpVtbl->Present(t->output.swapchain, 0, 0);
        t->output.composed = TRUE;
    }
    if (gpu->multithread)
        gpu->multithread->lpVtbl->Leave(gpu->multithread);
    if (c.draws)
        HeapFree(GetProcessHeap(), 0, c.draws);
    return hr;
}

void DcComposeAll(DcDevice *device, BOOL only_flagged)
{
    for (DcTarget *t = device->targets; t; t = t->next_in_device)
        if (!only_flagged || t->needs_compose)
            DcCompose(t);
}

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

/* a swap chain presented, so redraw the targets that show it. */
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

/* reads a rectangle of the last frame through a staging copy of the back buffer. */
HRESULT DcReadTarget(DcTarget *t, const RECT *rect, void *pixels, UINT pitch)
{
    DcOutput *o = &t->output;
    if (!o->composed || (!o->back && !o->bits))
        return S_FALSE;
    RECT whole = {0, 0, (LONG)o->width, (LONG)o->height}, r = rect ? *rect : whole, clipped;
    if (!IntersectRect(&clipped, &r, &whole) || !EqualRect(&clipped, &r))
        return E_INVALIDARG;
    if (o->bits) {
        for (LONG y = r.top; y < r.bottom; y++)
            memcpy((BYTE *)pixels + (SIZE_T)(y - r.top) * pitch, o->bits + ((SIZE_T)y * o->width + (SIZE_T)r.left) * 4,
                   (SIZE_T)(r.right - r.left) * 4);
        return S_OK;
    }
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
