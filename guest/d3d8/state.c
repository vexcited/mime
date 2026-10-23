/* direct3d 8 fixed function state and state blocks, kept as direct3d 8 reports them and passed on to direct3d 9. */
#include "d3d8_private.h"

static D8Device *impl(IDirect3DDevice8 *iface) { return D8DeviceFrom(iface); }

static DWORD float_bits(float f)
{
    DWORD v;
    memcpy(&v, &f, sizeof(v));
    return v;
}

static const D3DMATRIX IDENTITY = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};

/* ------------------------------------------------------------------ render states */

/* directx 7 and older states, which direct3d 8 neither reads back nor records. */
static BOOL legacy_state(DWORD s)
{
    switch (s) {
    case 1 ... 6:
    case 11 ... 13:
    case 17:
    case 18:
    case 21:
    case 31 ... 33:
    case 39:
    case 41:
    case 43 ... 46:
    case 49:
    case 51:
    case 64 ... 95:
    case 144:
        return TRUE;
    default:
        return FALSE;
    }
}

/* states direct3d 9 kept with the same number and meaning. */
static BOOL shared_state(DWORD s)
{
    switch (s) {
    case 7 ... 9:
    case 14 ... 16:
    case 19:
    case 20:
    case 22 ... 29:
    case 34 ... 38:
    case 48:
    case 52 ... 60:
    case 128 ... 137:
    case 139 ... 143:
    case 145 ... 148:
    case 151:
    case 152:
    case 154 ... 163:
    case 165 ... 168:
    case 170 ... 173:
        return TRUE;
    default:
        return FALSE;
    }
}

/* direct3d 8 bias counts steps toward the viewer, in units of a 16-bit depth buffer. */
static float zbias(DWORD value) { return -(float)value / 65536.0f; }

static void apply_rs(D8Device *d, DWORD state, DWORD value)
{
    if (d->rs[state] == value)
        return;
    d->rs[state] = value;
    IDirect3DDevice9 *d9 = d->d9;
    switch (state) {
    case D3DRS_EDGEANTIALIAS:
        d9->lpVtbl->SetRenderState(d9, D3DRS_ANTIALIASEDLINEENABLE, value);
        break;
    case D3DRS_ZBIAS:
        d9->lpVtbl->SetRenderState(d9, D3DRS_DEPTHBIAS, float_bits(zbias(value)));
        break;
    case D3DRS_SOFTWAREVERTEXPROCESSING:
        if (d->flags & D3DCREATE_MIXED_VERTEXPROCESSING)
            D8SwitchProcessing(d, value != 0);
        break;
    default:
        if (shared_state(state))
            d9->lpVtbl->SetRenderState(d9, state, value);
        break;
    }
}

static HRESULT STDMETHODCALLTYPE device_set_render_state(IDirect3DDevice8 *iface, D3DRENDERSTATETYPE state,
                                                         DWORD value)
{
    D8Device *d = impl(iface);
    if ((UINT)state >= 256)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        if (legacy_state(state))
            return D3DERR_INVALIDCALL;
        D8Command *c = D8Record(d, D8_CMD_RS);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = state;
        c->u.value = value;
        return D3D_OK;
    }
    apply_rs(d, state, value);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_render_state(IDirect3DDevice8 *iface, D3DRENDERSTATETYPE state,
                                                         DWORD *value)
{
    D8Device *d = impl(iface);
    if ((UINT)state >= 256 || legacy_state(state))
        return D3DERR_INVALIDCALL;
    *value = d->rs[state];
    return D3D_OK;
}

/* ------------------------------------------------------------------ texture stage states */

/* direct3d 9 turned these into sampler states. */
static int sampler_state(DWORD type)
{
    switch (type) {
    case D3DTSS_ADDRESSU:
        return D3DSAMP_ADDRESSU;
    case D3DTSS_ADDRESSV:
        return D3DSAMP_ADDRESSV;
    case D3DTSS_ADDRESSW:
        return D3DSAMP_ADDRESSW;
    case D3DTSS_BORDERCOLOR:
        return D3DSAMP_BORDERCOLOR;
    case D3DTSS_MAGFILTER:
        return D3DSAMP_MAGFILTER;
    case D3DTSS_MINFILTER:
        return D3DSAMP_MINFILTER;
    case D3DTSS_MIPFILTER:
        return D3DSAMP_MIPFILTER;
    case D3DTSS_MIPMAPLODBIAS:
        return D3DSAMP_MIPMAPLODBIAS;
    case D3DTSS_MAXMIPLEVEL:
        return D3DSAMP_MAXMIPLEVEL;
    case D3DTSS_MAXANISOTROPY:
        return D3DSAMP_MAXANISOTROPY;
    default:
        return -1;
    }
}

static void apply_tss(D8Device *d, DWORD stage, DWORD type, DWORD value)
{
    if (d->tss[stage][type] == value)
        return;
    d->tss[stage][type] = value;
    IDirect3DDevice9 *d9 = d->d9;
    int sampler = sampler_state(type);
    if (sampler >= 0) {
        /* direct3d 8's cubic filters are gone, linear is the closest left. */
        if ((sampler == D3DSAMP_MAGFILTER || sampler == D3DSAMP_MINFILTER) && value > D3DTEXF_ANISOTROPIC)
            value = D3DTEXF_LINEAR;
        d9->lpVtbl->SetSamplerState(d9, stage, sampler, value);
    } else if ((type >= D3DTSS_COLOROP && type <= D3DTSS_TEXCOORDINDEX) ||
               (type >= D3DTSS_BUMPENVLSCALE && type <= D3DTSS_TEXTURETRANSFORMFLAGS) ||
               (type >= D3DTSS_COLORARG0 && type <= D3DTSS_RESULTARG)) {
        d9->lpVtbl->SetTextureStageState(d9, stage, type, value);
    }
}

static HRESULT STDMETHODCALLTYPE device_set_texture_stage_state(IDirect3DDevice8 *iface, DWORD stage,
                                                                D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    D8Device *d = impl(iface);
    if (stage >= D8_STAGES || (UINT)type >= 64)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_TSS);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = stage;
        c->b = type;
        c->u.value = value;
        return D3D_OK;
    }
    apply_tss(d, stage, type, value);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_texture_stage_state(IDirect3DDevice8 *iface, DWORD stage,
                                                                D3DTEXTURESTAGESTATETYPE type, DWORD *value)
{
    D8Device *d = impl(iface);
    if (stage >= D8_STAGES || (UINT)type >= 64)
        return D3DERR_INVALIDCALL;
    *value = d->tss[stage][type];
    return D3D_OK;
}

/* ------------------------------------------------------------------ textures */

static void apply_texture(D8Device *d, DWORD stage, D8Object *t)
{
    if (d->textures[stage] == t)
        return;
    D8Bind(&d->textures[stage], t);
    d->d9->lpVtbl->SetTexture(d->d9, stage, t ? (IDirect3DBaseTexture9 *)t->d9 : nullptr);
}

static HRESULT STDMETHODCALLTYPE device_set_texture(IDirect3DDevice8 *iface, DWORD stage,
                                                    IDirect3DBaseTexture8 *texture)
{
    D8Device *d = impl(iface);
    D8Object *t = D8Check(d, texture, D8_TEXTURES);
    if (stage >= D8_STAGES || (texture && !t))
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_TEXTURE);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = stage;
        D8Bind(&c->object, t);
        return D3D_OK;
    }
    apply_texture(d, stage, t);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_texture(IDirect3DDevice8 *iface, DWORD stage,
                                                    IDirect3DBaseTexture8 **out)
{
    D8Device *d = impl(iface);
    if (stage >= D8_STAGES)
        return D3DERR_INVALIDCALL;
    D8Object *t = d->textures[stage];
    *out = t ? D8_IFACE(t) : nullptr;
    if (t)
        D8ObjectAddRef(t);
    return D3D_OK;
}

/* ------------------------------------------------------------------ transforms */

UINT D8TransformIndex(D3DTRANSFORMSTATETYPE type)
{
    if (type == D3DTS_VIEW)
        return 0;
    if (type == D3DTS_PROJECTION)
        return 1;
    if (type >= D3DTS_TEXTURE0 && type <= D3DTS_TEXTURE7)
        return 2 + (type - D3DTS_TEXTURE0);
    if (type >= 256 && type < 512)
        return 10 + (type - 256);
    return D8_TRANSFORMS;
}

static void apply_transform(D8Device *d, DWORD type, const D3DMATRIX *m)
{
    UINT i = D8TransformIndex(type);
    if (i >= D8_TRANSFORMS)
        return;
    d->transforms[i] = *m;
    d->d9->lpVtbl->SetTransform(d->d9, type, m);
}

static HRESULT STDMETHODCALLTYPE device_set_transform(IDirect3DDevice8 *iface, D3DTRANSFORMSTATETYPE type,
                                                      const D3DMATRIX *matrix)
{
    D8Device *d = impl(iface);
    if (!matrix)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_TRANSFORM);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = type;
        c->u.matrix = *matrix;
        return D3D_OK;
    }
    apply_transform(d, type, matrix);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_transform(IDirect3DDevice8 *iface, D3DTRANSFORMSTATETYPE type,
                                                      D3DMATRIX *matrix)
{
    D8Device *d = impl(iface);
    UINT i = D8TransformIndex(type);
    if (i >= D8_TRANSFORMS)
        return D3DERR_INVALIDCALL;
    *matrix = d->transforms[i];
    return D3D_OK;
}

/* the product applies right away, even while a state block records. */
static HRESULT STDMETHODCALLTYPE device_multiply_transform(IDirect3DDevice8 *iface, D3DTRANSFORMSTATETYPE type,
                                                           const D3DMATRIX *matrix)
{
    D8Device *d = impl(iface);
    UINT i = D8TransformIndex(type);
    if (i >= D8_TRANSFORMS || !matrix)
        return D3DERR_INVALIDCALL;
    const D3DMATRIX *b = &d->transforms[i];
    D3DMATRIX r;
    for (UINT y = 0; y < 4; y++)
        for (UINT x = 0; x < 4; x++)
            r.m[y][x] = matrix->m[y][0] * b->m[0][x] + matrix->m[y][1] * b->m[1][x] + matrix->m[y][2] * b->m[2][x] +
                        matrix->m[y][3] * b->m[3][x];
    apply_transform(d, type, &r);
    return D3D_OK;
}

/* ------------------------------------------------------------------ viewport and material */

/* the viewport must fit the render target. a depth range thinner than a thousandth is widened. */
static HRESULT apply_viewport(D8Device *d, const D3DVIEWPORT8 *in)
{
    UINT width = d->target ? d->target->width : 0, height = d->target ? d->target->height : 0;
    if (in->X > width || in->Y > height || in->X + in->Width > width || in->Y + in->Height > height)
        return D3DERR_INVALIDCALL;
    D3DVIEWPORT8 vp = *in;
    if (vp.MaxZ - vp.MinZ < 0.001f) {
        if (vp.MaxZ < 0.5f)
            vp.MaxZ = vp.MinZ + 0.001f;
        else
            vp.MinZ = vp.MaxZ - 0.001f;
    }
    d->viewport = vp;
    d->d9->lpVtbl->SetViewport(d->d9, &vp);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_viewport(IDirect3DDevice8 *iface, const D3DVIEWPORT8 *viewport)
{
    D8Device *d = impl(iface);
    if (!viewport)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_VIEWPORT);
        if (!c)
            return E_OUTOFMEMORY;
        c->u.viewport = *viewport;
        return D3D_OK;
    }
    return apply_viewport(d, viewport);
}

static HRESULT STDMETHODCALLTYPE device_get_viewport(IDirect3DDevice8 *iface, D3DVIEWPORT8 *viewport)
{
    *viewport = impl(iface)->viewport;
    return D3D_OK;
}

static void apply_material(D8Device *d, const D3DMATERIAL8 *m)
{
    d->material = *m;
    d->d9->lpVtbl->SetMaterial(d->d9, m);
}

static HRESULT STDMETHODCALLTYPE device_set_material(IDirect3DDevice8 *iface, const D3DMATERIAL8 *material)
{
    D8Device *d = impl(iface);
    if (!material)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_MATERIAL);
        if (!c)
            return E_OUTOFMEMORY;
        c->u.material = *material;
        return D3D_OK;
    }
    apply_material(d, material);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_material(IDirect3DDevice8 *iface, D3DMATERIAL8 *material)
{
    *material = impl(iface)->material;
    return D3D_OK;
}

/* ------------------------------------------------------------------ lights */

static const D3DLIGHT8 DEFAULT_LIGHT = {
    .Type = D3DLIGHT_DIRECTIONAL,
    .Diffuse = {1.0f, 1.0f, 1.0f, 0.0f},
    .Direction = {0.0f, 0.0f, 1.0f},
};

static D8Light *find_light(D8Device *d, DWORD index) { return index < d->light_count ? d->lights[index] : nullptr; }

/* a new slot starts as the default light. any index memory allows is valid. */
static HRESULT make_light(D8Device *d, DWORD index, D8Light **out)
{
    if ((*out = find_light(d, index)))
        return D3D_OK;
    if (index >= d->light_count) {
        if (index >= 0x10000000)
            return E_OUTOFMEMORY;
        UINT count = D8_MAX(index + 1, d->light_count * 2);
        D8Light **lights = d->lights ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, d->lights, count * sizeof(*lights))
                                     : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*lights));
        if (!lights)
            return E_OUTOFMEMORY;
        d->lights = lights;
        d->light_count = count;
    }
    D8Light *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    l->light = DEFAULT_LIGHT;
    d->lights[index] = l;
    d->d9->lpVtbl->SetLight(d->d9, index, &l->light);
    *out = l;
    return D3D_OK;
}

static HRESULT apply_light(D8Device *d, DWORD index, const D3DLIGHT8 *light)
{
    D8Light *l;
    HRESULT hr = make_light(d, index, &l);
    if (FAILED(hr))
        return hr;
    l->light = *light;
    d->d9->lpVtbl->SetLight(d->d9, index, light);
    return D3D_OK;
}

static HRESULT apply_light_enable(D8Device *d, DWORD index, BOOL enable)
{
    D8Light *l;
    HRESULT hr = make_light(d, index, &l);
    if (FAILED(hr))
        return hr;
    l->enabled = enable != 0;
    d->d9->lpVtbl->LightEnable(d->d9, index, l->enabled);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_light(IDirect3DDevice8 *iface, DWORD index, const D3DLIGHT8 *light)
{
    D8Device *d = impl(iface);
    if (!light)
        return D3DERR_INVALIDCALL;
    D8Enter(d);
    HRESULT hr;
    if (d->recording) {
        D8Light *l;
        hr = make_light(d, index, &l);
        D8Command *c = SUCCEEDED(hr) ? D8Record(d, D8_CMD_LIGHT) : nullptr;
        if (c) {
            c->a = index;
            c->u.light = *light;
        } else if (SUCCEEDED(hr)) {
            hr = E_OUTOFMEMORY;
        }
    } else {
        hr = apply_light(d, index, light);
    }
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_light(IDirect3DDevice8 *iface, DWORD index, D3DLIGHT8 *light)
{
    D8Light *l = find_light(impl(iface), index);
    if (!l)
        return D3DERR_INVALIDCALL;
    *light = l->light;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_light_enable(IDirect3DDevice8 *iface, DWORD index, BOOL enable)
{
    D8Device *d = impl(iface);
    D8Enter(d);
    HRESULT hr;
    if (d->recording) {
        D8Light *l;
        hr = make_light(d, index, &l);
        D8Command *c = SUCCEEDED(hr) ? D8Record(d, D8_CMD_LIGHT_ENABLE) : nullptr;
        if (c) {
            c->a = index;
            c->u.value = enable != 0;
        } else if (SUCCEEDED(hr)) {
            hr = E_OUTOFMEMORY;
        }
    } else {
        hr = apply_light_enable(d, index, enable);
    }
    D8Leave(d);
    return hr;
}

/* windows reports an enabled light as 128, not TRUE. */
static HRESULT STDMETHODCALLTYPE device_get_light_enable(IDirect3DDevice8 *iface, DWORD index, BOOL *enable)
{
    D8Light *l = find_light(impl(iface), index);
    if (!l)
        return D3DERR_INVALIDCALL;
    *enable = l->enabled ? 128 : 0;
    return D3D_OK;
}

/* ------------------------------------------------------------------ clip planes and status */

static void apply_plane(D8Device *d, DWORD index, const float *plane)
{
    memcpy(d->planes[index], plane, sizeof(d->planes[index]));
    if (index < d->caps.MaxUserClipPlanes)
        d->d9->lpVtbl->SetClipPlane(d->d9, index, plane);
}

static HRESULT STDMETHODCALLTYPE device_set_clip_plane(IDirect3DDevice8 *iface, DWORD index, const float *plane)
{
    D8Device *d = impl(iface);
    if (index >= D8_PLANES || !plane)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_PLANE);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = index;
        memcpy(c->u.plane, plane, sizeof(c->u.plane));
        return D3D_OK;
    }
    apply_plane(d, index, plane);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_clip_plane(IDirect3DDevice8 *iface, DWORD index, float *plane)
{
    D8Device *d = impl(iface);
    if (index >= D8_PLANES)
        return D3DERR_INVALIDCALL;
    memcpy(plane, d->planes[index], sizeof(d->planes[index]));
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_clip_status(IDirect3DDevice8 *iface, const D3DCLIPSTATUS8 *status)
{
    D8Device *d = impl(iface);
    d->clip_status = *status;
    d->d9->lpVtbl->SetClipStatus(d->d9, status);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_clip_status(IDirect3DDevice8 *iface, D3DCLIPSTATUS8 *status)
{
    *status = impl(iface)->clip_status;
    return D3D_OK;
}

/* ------------------------------------------------------------------ palettes */

/* directx 8.1 programs need the alpha palette cap for entries that are not opaque. */
static HRESULT STDMETHODCALLTYPE device_set_palette_entries(IDirect3DDevice8 *iface, UINT palette,
                                                            const PALETTEENTRY *entries)
{
    D8Device *d = impl(iface);
    if (!entries)
        return D3DERR_INVALIDCALL;
    if (d->d3d->sdk > 120 && !(d->caps.TextureCaps & D3DPTEXTURECAPS_ALPHAPALETTE))
        for (UINT i = 0; i < 256; i++)
            if (entries[i].peFlags != 0xFF)
                return D3DERR_INVALIDCALL;
    D8Enter(d);
    HRESULT hr = D3D_OK;
    if (palette >= d->palette_count) {
        UINT count = D8_MAX(palette + 1, d->palette_count * 2);
        D8Palette **p = palette >= 0x01000000 ? nullptr
                        : d->palettes ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, d->palettes, count * sizeof(*p))
                                      : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*p));
        if (p) {
            d->palettes = p;
            d->palette_count = count;
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    if (SUCCEEDED(hr) && !d->palettes[palette] &&
        !(d->palettes[palette] = HeapAlloc(GetProcessHeap(), 0, sizeof(D8Palette))))
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        memcpy(d->palettes[palette]->entries, entries, sizeof(d->palettes[palette]->entries));
        d->d9->lpVtbl->SetPaletteEntries(d->d9, palette, entries);
    }
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_palette_entries(IDirect3DDevice8 *iface, UINT palette,
                                                            PALETTEENTRY *entries)
{
    D8Device *d = impl(iface);
    if (palette >= d->palette_count || !d->palettes[palette])
        return D3DERR_INVALIDCALL;
    memcpy(entries, d->palettes[palette]->entries, sizeof(d->palettes[palette]->entries));
    return D3D_OK;
}

static void apply_palette(D8Device *d, UINT palette)
{
    d->palette = palette;
    d->d9->lpVtbl->SetCurrentTexturePalette(d->d9, palette);
}

static HRESULT STDMETHODCALLTYPE device_set_current_texture_palette(IDirect3DDevice8 *iface, UINT palette)
{
    D8Device *d = impl(iface);
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_PALETTE);
        if (!c)
            return E_OUTOFMEMORY;
        c->u.value = palette;
        return D3D_OK;
    }
    apply_palette(d, palette);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_current_texture_palette(IDirect3DDevice8 *iface, UINT *palette)
{
    *palette = impl(iface)->palette;
    return D3D_OK;
}

/* ------------------------------------------------------------------ validation */

/* fails on what the driver would refuse: missing operations, or more stages than it blends. */
static HRESULT STDMETHODCALLTYPE device_validate_device(IDirect3DDevice8 *iface, DWORD *passes)
{
    D8Device *d = impl(iface);
    *passes = 1;
    UINT stages = 0;
    for (UINT i = 0; i < D8_STAGES && d->tss[i][D3DTSS_COLOROP] != D3DTOP_DISABLE; i++) {
        DWORD color = d->tss[i][D3DTSS_COLOROP], alpha = d->tss[i][D3DTSS_ALPHAOP];
        if (!color || color > 26 || !(d->caps.TextureOpCaps & (1u << (color - 1))))
            return D3DERR_UNSUPPORTEDCOLOROPERATION;
        if (!alpha || alpha > 26 || !(d->caps.TextureOpCaps & (1u << (alpha - 1))))
            return D3DERR_UNSUPPORTEDALPHAOPERATION;
        stages++;
    }
    if (stages > d->caps.MaxTextureBlendStages)
        return D3DERR_TOOMANYOPERATIONS;
    DWORD n;
    HRESULT hr = d->d9->lpVtbl->ValidateDevice(d->d9, &n);
    if (SUCCEEDED(hr))
        *passes = n;
    return hr;
}

/* ------------------------------------------------------------------ defaults */

void D8DefaultState(D8Device *d)
{
    for (UINT i = 0; i < 256; i++)
        d->rs[i] = D8_UNSET;
    static const DWORD defaults[][2] = {
        {D3DRS_FILLMODE, D3DFILL_SOLID},
        {D3DRS_SHADEMODE, D3DSHADE_GOURAUD},
        {D3DRS_LINEPATTERN, 0},
        {D3DRS_ZWRITEENABLE, TRUE},
        {D3DRS_ALPHATESTENABLE, FALSE},
        {D3DRS_LASTPIXEL, TRUE},
        {D3DRS_SRCBLEND, D3DBLEND_ONE},
        {D3DRS_DESTBLEND, D3DBLEND_ZERO},
        {D3DRS_CULLMODE, D3DCULL_CCW},
        {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRS_ALPHAREF, 0},
        {D3DRS_ALPHAFUNC, D3DCMP_ALWAYS},
        {D3DRS_DITHERENABLE, FALSE},
        {D3DRS_ALPHABLENDENABLE, FALSE},
        {D3DRS_FOGENABLE, FALSE},
        {D3DRS_SPECULARENABLE, FALSE},
        {D3DRS_ZVISIBLE, 0},
        {D3DRS_FOGCOLOR, 0},
        {D3DRS_FOGTABLEMODE, D3DFOG_NONE},
        {D3DRS_FOGSTART, 0x00000000},
        {D3DRS_FOGEND, 0x3F800000},
        {D3DRS_FOGDENSITY, 0x3F800000},
        {D3DRS_EDGEANTIALIAS, FALSE},
        {D3DRS_ZBIAS, 0},
        {D3DRS_RANGEFOGENABLE, FALSE},
        {D3DRS_STENCILENABLE, FALSE},
        {D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP},
        {D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP},
        {D3DRS_STENCILPASS, D3DSTENCILOP_KEEP},
        {D3DRS_STENCILFUNC, D3DCMP_ALWAYS},
        {D3DRS_STENCILREF, 0},
        {D3DRS_STENCILMASK, 0xFFFFFFFF},
        {D3DRS_STENCILWRITEMASK, 0xFFFFFFFF},
        {D3DRS_TEXTUREFACTOR, 0xFFFFFFFF},
        {D3DRS_WRAP0, 0},
        {D3DRS_WRAP1, 0},
        {D3DRS_WRAP2, 0},
        {D3DRS_WRAP3, 0},
        {D3DRS_WRAP4, 0},
        {D3DRS_WRAP5, 0},
        {D3DRS_WRAP6, 0},
        {D3DRS_WRAP7, 0},
        {D3DRS_CLIPPING, TRUE},
        {D3DRS_LIGHTING, TRUE},
        {D3DRS_AMBIENT, 0},
        {D3DRS_FOGVERTEXMODE, D3DFOG_NONE},
        {D3DRS_COLORVERTEX, TRUE},
        {D3DRS_LOCALVIEWER, TRUE},
        {D3DRS_NORMALIZENORMALS, FALSE},
        {D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1},
        {D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2},
        {D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL},
        {D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL},
        {D3DRS_VERTEXBLEND, D3DVBF_DISABLE},
        {D3DRS_CLIPPLANEENABLE, 0},
        {D3DRS_POINTSIZE, 0x3F800000},
        {D3DRS_POINTSIZE_MIN, 0x00000000},
        {D3DRS_POINTSPRITEENABLE, FALSE},
        {D3DRS_POINTSCALEENABLE, FALSE},
        {D3DRS_POINTSCALE_A, 0x3F800000},
        {D3DRS_POINTSCALE_B, 0x00000000},
        {D3DRS_POINTSCALE_C, 0x00000000},
        {D3DRS_MULTISAMPLEANTIALIAS, TRUE},
        {D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF},
        {D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE},
        {D3DRS_PATCHSEGMENTS, 0x3F800000},
        {D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE},
        {D3DRS_COLORWRITEENABLE, 0x0000000F},
        {D3DRS_TWEENFACTOR, 0x00000000},
        {D3DRS_BLENDOP, D3DBLENDOP_ADD},
        {D3DRS_POSITIONORDER, D3DDEGREE_CUBIC},
        {D3DRS_NORMALORDER, D3DDEGREE_LINEAR},
    };
    /* these are what direct3d 9 starts from too, so nothing is passed on. */
    for (UINT i = 0; i < RTL_NUMBER_OF(defaults); i++)
        d->rs[defaults[i][0]] = defaults[i][1];
    d->rs[D3DRS_ZENABLE] = d->auto_depth ? D3DZB_TRUE : D3DZB_FALSE;
    d->rs[D3DRS_SOFTWAREVERTEXPROCESSING] = (d->flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0;
    d->rs[D3DRS_POINTSIZE_MAX] = float_bits(d->caps.MaxPointSize ? d->caps.MaxPointSize : 64.0f);
    d->d9->lpVtbl->SetRenderState(d->d9, D3DRS_POINTSIZE_MAX, d->rs[D3DRS_POINTSIZE_MAX]);
    d->software = (d->flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0;

    for (UINT s = 0; s < D8_STAGES; s++) {
        DWORD *t = d->tss[s];
        for (UINT i = 0; i < 64; i++)
            t[i] = D8_UNSET;
        t[D3DTSS_COLOROP] = s ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        t[D3DTSS_COLORARG1] = t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = s ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        for (UINT i = D3DTSS_BUMPENVMAT00; i <= D3DTSS_BUMPENVMAT11; i++)
            t[i] = 0;
        t[D3DTSS_TEXCOORDINDEX] = s;
        t[D3DTSS_ADDRESSU] = t[D3DTSS_ADDRESSV] = t[D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
        t[D3DTSS_BORDERCOLOR] = 0;
        t[D3DTSS_MAGFILTER] = t[D3DTSS_MINFILTER] = D3DTEXF_POINT;
        t[D3DTSS_MIPFILTER] = D3DTEXF_NONE;
        t[D3DTSS_MIPMAPLODBIAS] = t[D3DTSS_MAXMIPLEVEL] = 0;
        t[D3DTSS_MAXANISOTROPY] = 1;
        t[D3DTSS_BUMPENVLSCALE] = t[D3DTSS_BUMPENVLOFFSET] = 0;
        t[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
        t[D3DTSS_COLORARG0] = t[D3DTSS_ALPHAARG0] = t[D3DTSS_RESULTARG] = D3DTA_CURRENT;
    }
    for (UINT i = 0; i < D8_TRANSFORMS; i++)
        d->transforms[i] = IDENTITY;
    D8ResetViewport(d);
    memset(&d->material, 0, sizeof(d->material));
    d->clip_status = (D3DCLIPSTATUS8){0, 0xFFFFFFFF};
    d->palette = D8_NO_PALETTE;
    d->vs = d->ps = 0;
}

static void free_blocks(D8Device *d)
{
    for (UINT i = 1; i < d->blocks.count; i++) {
        D8StateBlock *b = D8TableGet(&d->blocks, i);
        if (b) {
            D8BlockFree(d, b);
            D8TableRemove(&d->blocks, i);
        }
    }
    D8TableFree(&d->blocks);
    d->recording = nullptr;
}

void D8ClearState(D8Device *d)
{
    for (UINT i = 0; i < D8_STAGES; i++)
        apply_texture(d, i, nullptr);
    for (UINT i = 0; i < D8_STREAMS; i++) {
        D8Bind(&d->streams[i].buffer, nullptr);
        d->streams[i].stride = 0;
        d->d9->lpVtbl->SetStreamSource(d->d9, i, nullptr, 0, 0);
    }
    D8Bind(&d->indices, nullptr);
    d->base_vertex = 0;
    d->d9->lpVtbl->SetIndices(d->d9, nullptr);
    free_blocks(d);
    for (UINT i = 0; i < d->light_count; i++) {
        if (d->lights[i])
            d->d9->lpVtbl->LightEnable(d->d9, i, FALSE);
        HeapFree(GetProcessHeap(), 0, d->lights[i]);
    }
    HeapFree(GetProcessHeap(), 0, d->lights);
    d->lights = nullptr;
    d->light_count = 0;
    for (UINT i = 0; i < d->palette_count; i++)
        HeapFree(GetProcessHeap(), 0, d->palettes[i]);
    HeapFree(GetProcessHeap(), 0, d->palettes);
    d->palettes = nullptr;
    d->palette_count = 0;
}

/* ------------------------------------------------------------------ state blocks */

D8Command *D8Record(D8Device *d, DWORD kind)
{
    D8StateBlock *b = d->recording;
    if (b->count == b->capacity) {
        UINT capacity = b->capacity ? b->capacity * 2 : 16;
        D8Command *c = b->commands ? HeapReAlloc(GetProcessHeap(), 0, b->commands, capacity * sizeof(*c))
                                   : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*c));
        if (!c)
            return nullptr;
        b->commands = c;
        b->capacity = capacity;
    }
    D8Command *c = &b->commands[b->count++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    return c;
}

void D8BlockFree(D8Device *d, D8StateBlock *b)
{
    for (UINT i = 0; i < b->count; i++) {
        D8Bind(&b->commands[i].object, nullptr);
        HeapFree(GetProcessHeap(), 0, b->commands[i].constants);
    }
    HeapFree(GetProcessHeap(), 0, b->commands);
    HeapFree(GetProcessHeap(), 0, b);
}

static HRESULT capture(D8Device *d, D8Command *c)
{
    switch (c->kind) {
    case D8_CMD_RS:
        c->u.value = d->rs[c->a];
        break;
    case D8_CMD_TSS:
        c->u.value = d->tss[c->a][c->b];
        break;
    case D8_CMD_TEXTURE:
        D8Bind(&c->object, d->textures[c->a]);
        break;
    case D8_CMD_PALETTE:
        c->u.value = d->palette;
        break;
    case D8_CMD_STREAM:
        D8Bind(&c->object, d->streams[c->a].buffer);
        c->b = d->streams[c->a].stride;
        break;
    case D8_CMD_INDICES:
        D8Bind(&c->object, d->indices);
        c->a = d->base_vertex;
        break;
    case D8_CMD_VIEWPORT:
        c->u.viewport = d->viewport;
        break;
    case D8_CMD_TRANSFORM: {
        UINT i = D8TransformIndex(c->a);
        if (i < D8_TRANSFORMS)
            c->u.matrix = d->transforms[i];
        break;
    }
    case D8_CMD_PLANE:
        memcpy(c->u.plane, d->planes[c->a], sizeof(c->u.plane));
        break;
    case D8_CMD_MATERIAL:
        c->u.material = d->material;
        break;
    case D8_CMD_LIGHT:
    case D8_CMD_LIGHT_ENABLE: {
        D8Light *l = find_light(d, c->a);
        if (!l)
            return D3DERR_INVALIDCALL;
        if (c->kind == D8_CMD_LIGHT)
            c->u.light = l->light;
        else
            c->u.value = l->enabled;
        break;
    }
    case D8_CMD_VS:
        c->u.value = d->vs;
        break;
    case D8_CMD_PS:
        c->u.value = d->ps;
        break;
    case D8_CMD_VS_CONSTANTS:
        memcpy(c->constants, d->vs_constants[c->a], c->b * sizeof(d->vs_constants[0]));
        break;
    case D8_CMD_PS_CONSTANTS:
        memcpy(c->constants, d->ps_constants[c->a], c->b * sizeof(d->ps_constants[0]));
        break;
    }
    return D3D_OK;
}

static HRESULT apply(D8Device *d, const D8Command *c)
{
    switch (c->kind) {
    case D8_CMD_RS:
        apply_rs(d, c->a, c->u.value);
        return D3D_OK;
    case D8_CMD_TSS:
        apply_tss(d, c->a, c->b, c->u.value);
        return D3D_OK;
    case D8_CMD_TEXTURE:
        apply_texture(d, c->a, c->object);
        return D3D_OK;
    case D8_CMD_PALETTE:
        if (c->u.value != D8_NO_PALETTE)
            apply_palette(d, c->u.value);
        return D3D_OK;
    case D8_CMD_STREAM:
        return D8SetStreamSource(d, c->a, c->object, c->b);
    case D8_CMD_INDICES:
        return D8SetIndices(d, c->object, c->a);
    case D8_CMD_VIEWPORT:
        return apply_viewport(d, &c->u.viewport);
    case D8_CMD_TRANSFORM:
        apply_transform(d, c->a, &c->u.matrix);
        return D3D_OK;
    case D8_CMD_PLANE:
        apply_plane(d, c->a, c->u.plane);
        return D3D_OK;
    case D8_CMD_MATERIAL:
        apply_material(d, &c->u.material);
        return D3D_OK;
    case D8_CMD_LIGHT:
        apply_light(d, c->a, &c->u.light);
        return D3D_OK;
    case D8_CMD_LIGHT_ENABLE:
        apply_light_enable(d, c->a, c->u.value);
        return D3D_OK;
    case D8_CMD_VS:
        D8SetVertexShader(d, c->u.value);
        return D3D_OK;
    case D8_CMD_PS:
        D8SetPixelShader(d, c->u.value);
        return D3D_OK;
    case D8_CMD_VS_CONSTANTS:
        D8SetVertexConstants(d, c->a, c->constants, c->b);
        return D3D_OK;
    case D8_CMD_PS_CONSTANTS:
        D8SetPixelConstants(d, c->a, c->constants, c->b);
        return D3D_OK;
    }
    return D3D_OK;
}

static D8StateBlock *finished_block(D8Device *d, DWORD token)
{
    D8StateBlock *b = D8TableGet(&d->blocks, token);
    return b && b != d->recording ? b : nullptr;
}

static HRESULT STDMETHODCALLTYPE device_begin_state_block(IDirect3DDevice8 *iface)
{
    D8Device *d = impl(iface);
    if (d->recording)
        return D3DERR_INVALIDCALL;
    D8StateBlock *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    UINT token = b ? D8TableAdd(&d->blocks, b) : 0;
    if (!token) {
        HeapFree(GetProcessHeap(), 0, b);
        return E_OUTOFMEMORY;
    }
    d->recording = b;
    d->recording_token = token;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_end_state_block(IDirect3DDevice8 *iface, DWORD *token)
{
    D8Device *d = impl(iface);
    if (!d->recording)
        return D3DERR_INVALIDCALL;
    d->recording = nullptr;
    *token = d->recording_token;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_apply_state_block(IDirect3DDevice8 *iface, DWORD token)
{
    D8Device *d = impl(iface);
    if (d->recording)
        return D3DERR_INVALIDCALL;
    D8StateBlock *b = finished_block(d, token);
    if (!b)
        return D3DERR_INVALIDCALL;
    D8Enter(d);
    HRESULT hr = D3D_OK;
    for (UINT i = 0; i < b->count && SUCCEEDED(hr); i++)
        hr = apply(d, &b->commands[i]);
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_capture_state_block(IDirect3DDevice8 *iface, DWORD token)
{
    D8Device *d = impl(iface);
    if (d->recording)
        return D3DERR_INVALIDCALL;
    D8StateBlock *b = finished_block(d, token);
    if (!b)
        return D3DERR_INVALIDCALL;
    D8Enter(d);
    HRESULT hr = D3D_OK;
    for (UINT i = 0; i < b->count && SUCCEEDED(hr); i++)
        hr = capture(d, &b->commands[i]);
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_delete_state_block(IDirect3DDevice8 *iface, DWORD token)
{
    D8Device *d = impl(iface);
    if (d->recording)
        return D3DERR_INVALIDCALL;
    D8StateBlock *b = finished_block(d, token);
    if (!b)
        return D3DERR_INVALIDCALL;
    D8Enter(d);
    D8BlockFree(d, b);
    D8TableRemove(&d->blocks, token);
    D8Leave(d);
    return D3D_OK;
}

static const BYTE ALL_STATES[] = {29,  7,   8,   9,   10,  14,  15,  16,  19,  20,  22,  23,  24,  25,  26,
                                  28,  33,  34,  35,  36,  37,  38,  40,  27,  47,  48,  52,  53,  54,  55,
                                  56,  57,  58,  59,  60,  128, 129, 130, 131, 132, 133, 134, 135, 139, 141,
                                  140, 136, 137, 143, 142, 148, 147, 145, 146, 151, 152, 153, 154, 155, 156,
                                  157, 158, 159, 160, 161, 162, 163, 164, 166, 167, 168, 170, 171, 172, 173};
static const BYTE PIXEL_STATES[] = {7,  8,  9,  10, 14, 15, 16,  19,  20,  23,  24,  25,  26,  33,  36,  37,  38,  40,  27,
                                    47, 52, 53, 54, 55, 56, 57, 58,  59,  60,  128, 129, 130, 131, 132, 133, 134, 135, 168, 171};
static const BYTE VERTEX_STATES[] = {9,   29,  22,  28,  34,  35,  36,  37,  38,  48,  139, 141, 140, 136,
                                     137, 143, 142, 148, 147, 145, 146, 151, 152, 153, 154, 155, 156, 157,
                                     158, 159, 160, 161, 162, 163, 164, 166, 167, 170, 172, 173};
static const BYTE STAGE_STATES[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 13, 14, 15,
                                    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28};

static BOOL add(D8Device *d, DWORD kind, DWORD a, DWORD b)
{
    D8Command *c = D8Record(d, kind);
    if (!c)
        return FALSE;
    c->a = a;
    c->b = b;
    if (kind == D8_CMD_VS_CONSTANTS || kind == D8_CMD_PS_CONSTANTS) {
        c->constants = HeapAlloc(GetProcessHeap(), 0, b * 4 * sizeof(float));
        if (!c->constants)
            return FALSE;
    }
    return TRUE;
}

static BOOL add_states(D8Device *d, const BYTE *states, UINT count)
{
    for (UINT i = 0; i < count; i++)
        if (!add(d, D8_CMD_RS, states[i], 0))
            return FALSE;
    return TRUE;
}

static BOOL add_lights(D8Device *d)
{
    for (UINT i = 0; i < d->light_count; i++)
        if (d->lights[i] && (!add(d, D8_CMD_LIGHT, i, 0) || !add(d, D8_CMD_LIGHT_ENABLE, i, 0)))
            return FALSE;
    return TRUE;
}

/* each block type's commands, in windows' order. capturing fills in the values. */
static BOOL fill_block(D8Device *d, D3DSTATEBLOCKTYPE type)
{
    UINT stages = D8_MIN(d->caps.MaxTextureBlendStages, D8_STAGES);
    if (type == D3DSBT_ALL) {
        if (!add_states(d, ALL_STATES, sizeof(ALL_STATES)))
            return FALSE;
        for (UINT s = 0; s < stages; s++)
            for (UINT i = 0; i < sizeof(STAGE_STATES); i++)
                if (!add(d, D8_CMD_TSS, s, STAGE_STATES[i]))
                    return FALSE;
        for (UINT s = 0; s < stages; s++)
            if (!add(d, D8_CMD_TEXTURE, s, 0))
                return FALSE;
        if (!add(d, D8_CMD_PALETTE, 0, 0))
            return FALSE;
        for (UINT i = 0; i < D8_MAX(1, D8_MIN(d->caps.MaxStreams, D8_STREAMS)); i++)
            if (!add(d, D8_CMD_STREAM, i, 0))
                return FALSE;
        if (!add(d, D8_CMD_INDICES, 0, 0) || !add(d, D8_CMD_VIEWPORT, 0, 0))
            return FALSE;
        for (UINT i = 0; i < 256; i++)
            if (!add(d, D8_CMD_TRANSFORM, 256 + i, 0))
                return FALSE;
        if (!add(d, D8_CMD_TRANSFORM, D3DTS_VIEW, 0) || !add(d, D8_CMD_TRANSFORM, D3DTS_PROJECTION, 0))
            return FALSE;
        for (UINT s = 0; s < stages; s++)
            if (!add(d, D8_CMD_TRANSFORM, D3DTS_TEXTURE0 + s, 0))
                return FALSE;
        UINT planes = d->software ? 6 : D8_MIN(d->caps.MaxUserClipPlanes, D8_PLANES);
        for (UINT i = 0; i < planes; i++)
            if (!add(d, D8_CMD_PLANE, i, 0))
                return FALSE;
        if (!add(d, D8_CMD_MATERIAL, 0, 0) || !add_lights(d) || !add(d, D8_CMD_VS, 0, 0) ||
            !add(d, D8_CMD_PS, 0, 0) || !add(d, D8_CMD_VS_CONSTANTS, 0, D8_VS_CONSTANTS) ||
            !add(d, D8_CMD_PS_CONSTANTS, 0, D8_PS_CONSTANTS))
            return FALSE;
    } else if (type == D3DSBT_PIXELSTATE) {
        if (!add_states(d, PIXEL_STATES, sizeof(PIXEL_STATES)))
            return FALSE;
        for (UINT s = 0; s < stages; s++)
            for (UINT i = 0; i < sizeof(STAGE_STATES); i++)
                if (!add(d, D8_CMD_TSS, s, STAGE_STATES[i]))
                    return FALSE;
        if (!add(d, D8_CMD_PS_CONSTANTS, 0, D8_PS_CONSTANTS) || !add(d, D8_CMD_PS, 0, 0))
            return FALSE;
    } else if (type == D3DSBT_VERTEXSTATE) {
        if (!add_states(d, VERTEX_STATES, sizeof(VERTEX_STATES)))
            return FALSE;
        for (UINT s = 0; s < stages; s++)
            if (!add(d, D8_CMD_TSS, s, D3DTSS_TEXCOORDINDEX) || !add(d, D8_CMD_TSS, s, D3DTSS_TEXTURETRANSFORMFLAGS))
                return FALSE;
        if (!add_lights(d) || !add(d, D8_CMD_VS_CONSTANTS, 0, D8_VS_CONSTANTS) || !add(d, D8_CMD_VS, 0, 0))
            return FALSE;
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE device_create_state_block(IDirect3DDevice8 *iface, D3DSTATEBLOCKTYPE type,
                                                           DWORD *token)
{
    D8Device *d = impl(iface);
    if (d->recording || (UINT)type > D3DSBT_VERTEXSTATE)
        return D3DERR_INVALIDCALL;
    D8StateBlock *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return E_OUTOFMEMORY;
    D8Enter(d);
    /* the block is built through the recording path, then given its values. */
    d->recording = b;
    BOOL ok = fill_block(d, type);
    d->recording = nullptr;
    for (UINT i = 0; ok && i < b->count; i++)
        capture(d, &b->commands[i]);
    UINT handle = ok ? D8TableAdd(&d->blocks, b) : 0;
    D8Leave(d);
    if (!handle) {
        D8BlockFree(d, b);
        return E_OUTOFMEMORY;
    }
    *token = handle;
    return D3D_OK;
}

/* ------------------------------------------------------------------ vtable */

void D8DeviceStateMethods(IDirect3DDevice8Vtbl *v)
{
    v->SetRenderState = device_set_render_state;
    v->GetRenderState = device_get_render_state;
    v->SetTextureStageState = device_set_texture_stage_state;
    v->GetTextureStageState = device_get_texture_stage_state;
    v->SetTexture = device_set_texture;
    v->GetTexture = device_get_texture;
    v->SetTransform = device_set_transform;
    v->GetTransform = device_get_transform;
    v->MultiplyTransform = device_multiply_transform;
    v->SetViewport = device_set_viewport;
    v->GetViewport = device_get_viewport;
    v->SetMaterial = device_set_material;
    v->GetMaterial = device_get_material;
    v->SetLight = device_set_light;
    v->GetLight = device_get_light;
    v->LightEnable = device_light_enable;
    v->GetLightEnable = device_get_light_enable;
    v->SetClipPlane = device_set_clip_plane;
    v->GetClipPlane = device_get_clip_plane;
    v->SetClipStatus = device_set_clip_status;
    v->GetClipStatus = device_get_clip_status;
    v->SetPaletteEntries = device_set_palette_entries;
    v->GetPaletteEntries = device_get_palette_entries;
    v->SetCurrentTexturePalette = device_set_current_texture_palette;
    v->GetCurrentTexturePalette = device_get_current_texture_palette;
    v->ValidateDevice = device_validate_device;
    v->BeginStateBlock = device_begin_state_block;
    v->EndStateBlock = device_end_state_block;
    v->ApplyStateBlock = device_apply_state_block;
    v->CaptureStateBlock = device_capture_state_block;
    v->DeleteStateBlock = device_delete_state_block;
    v->CreateStateBlock = device_create_state_block;
}
