/*
 * IDirect3DDevice7, as d3dim700.dll of build 26300 implements it: the
 * state of the device is kept here, checked and defaulted as windows
 * does, and handed to the backend in direct3d 9's terms when this device
 * is the one drawing. drawing reaches the render target's copy in the
 * backend, read back when the cpu wants its pixels.
 */
#include "d3d_private.h"

static const IDirect3DDevice7Vtbl dev7_vtbl;
static D3DDEV *g_active;

D3DDEV *D3dDevFromIface7(void *iface)
{
    IDirect3DDevice7 *i = iface;
    return i && i->lpVtbl == &dev7_vtbl ? CONTAINING_RECORD(i, D3DDEV, iface7) : nullptr;
}

static DWORD float_bits(float f)
{
    DWORD d;
    memcpy(&d, &f, sizeof(d));
    return d;
}

/* ------------------------------------------------------------------ state in direct3d 9's terms */

static BOOL active(D3DDEV *dev)
{
    return g_active == dev;
}

BOOL D3dDevIsActive(D3DDEV *dev)
{
    return g_active == dev;
}

static void push_rs(D3DDEV *dev, DWORD type, DWORD value)
{
    switch (type) {
    case D3DRENDERSTATE_ZENABLE:
        /* w buffering is done as z. */
        DbSetRenderState(dev->db, type, value ? 1 : 0);
        break;
    case D3DRENDERSTATE_ZBIAS:
        /* 0 to 16 toward the viewer, a driver's step a unit guessed. */
        DbSetRenderState(dev->db, DB_RS_DEPTHBIAS, float_bits(-(float)value * (1.0f / 65536.0f)));
        break;
    case D3DRENDERSTATE_EDGEANTIALIAS:
        DbSetRenderState(dev->db, DB_RS_ANTIALIASEDLINEENABLE, value);
        break;
    case D3DRENDERSTATE_FILLMODE:
    case D3DRENDERSTATE_SHADEMODE:
    case D3DRENDERSTATE_ZWRITEENABLE:
    case D3DRENDERSTATE_ALPHATESTENABLE:
    case D3DRENDERSTATE_LASTPIXEL:
    case D3DRENDERSTATE_SRCBLEND:
    case D3DRENDERSTATE_DESTBLEND:
    case D3DRENDERSTATE_CULLMODE:
    case D3DRENDERSTATE_ZFUNC:
    case D3DRENDERSTATE_ALPHAREF:
    case D3DRENDERSTATE_ALPHAFUNC:
    case D3DRENDERSTATE_DITHERENABLE:
    case D3DRENDERSTATE_ALPHABLENDENABLE:
    case D3DRENDERSTATE_FOGENABLE:
    case D3DRENDERSTATE_SPECULARENABLE:
    case D3DRENDERSTATE_FOGCOLOR:
    case D3DRENDERSTATE_FOGTABLEMODE:
    case D3DRENDERSTATE_FOGSTART:
    case D3DRENDERSTATE_FOGEND:
    case D3DRENDERSTATE_FOGDENSITY:
    case D3DRENDERSTATE_RANGEFOGENABLE:
    case D3DRENDERSTATE_STENCILENABLE:
    case D3DRENDERSTATE_STENCILFAIL:
    case D3DRENDERSTATE_STENCILZFAIL:
    case D3DRENDERSTATE_STENCILPASS:
    case D3DRENDERSTATE_STENCILFUNC:
    case D3DRENDERSTATE_STENCILREF:
    case D3DRENDERSTATE_STENCILMASK:
    case D3DRENDERSTATE_STENCILWRITEMASK:
    case D3DRENDERSTATE_TEXTUREFACTOR:
    case D3DRENDERSTATE_CLIPPING:
    case D3DRENDERSTATE_LIGHTING:
    case D3DRENDERSTATE_AMBIENT:
    case D3DRENDERSTATE_FOGVERTEXMODE:
    case D3DRENDERSTATE_COLORVERTEX:
    case D3DRENDERSTATE_LOCALVIEWER:
    case D3DRENDERSTATE_NORMALIZENORMALS:
    case D3DRENDERSTATE_DIFFUSEMATERIALSOURCE:
    case D3DRENDERSTATE_SPECULARMATERIALSOURCE:
    case D3DRENDERSTATE_AMBIENTMATERIALSOURCE:
    case D3DRENDERSTATE_EMISSIVEMATERIALSOURCE:
    case D3DRENDERSTATE_VERTEXBLEND:
    case D3DRENDERSTATE_CLIPPLANEENABLE:
        DbSetRenderState(dev->db, type, value);
        break;
    default:
        if (type >= D3DRENDERSTATE_WRAP0 && type <= D3DRENDERSTATE_WRAP7)
            DbSetRenderState(dev->db, type, value);
        /* the others are the rasterizer's own business, or nothing a gpu does. */
        break;
    }
}

static DWORD mag_filter(DWORD v)
{
    switch (v) {
    case D3DTFG_POINT:
        return 1;
    case D3DTFG_ANISOTROPIC:
        return 3;
    default:
        return 2;
    }
}

static void push_tss(D3DDEV *dev, DWORD stage, DWORD type, DWORD value)
{
    DbDevice *db = dev->db;
    switch (type) {
    case D3DTSS_ADDRESS:
        DbSetSamplerState(db, stage, DB_SAMP_ADDRESSU, value);
        DbSetSamplerState(db, stage, DB_SAMP_ADDRESSV, value);
        break;
    case D3DTSS_ADDRESSU:
        DbSetSamplerState(db, stage, DB_SAMP_ADDRESSU, value);
        break;
    case D3DTSS_ADDRESSV:
        DbSetSamplerState(db, stage, DB_SAMP_ADDRESSV, value);
        break;
    case D3DTSS_BORDERCOLOR:
        DbSetSamplerState(db, stage, DB_SAMP_BORDERCOLOR, value);
        break;
    case D3DTSS_MAGFILTER:
        DbSetSamplerState(db, stage, DB_SAMP_MAGFILTER, mag_filter(value));
        break;
    case D3DTSS_MINFILTER:
        DbSetSamplerState(db, stage, DB_SAMP_MINFILTER, value == D3DTFN_POINT ? 1 : value == D3DTFN_ANISOTROPIC ? 3 : 2);
        break;
    case D3DTSS_MIPFILTER:
        DbSetSamplerState(db, stage, DB_SAMP_MIPFILTER, value == D3DTFP_POINT ? 1 : value == D3DTFP_LINEAR ? 2 : 0);
        break;
    case D3DTSS_MIPMAPLODBIAS:
        DbSetSamplerState(db, stage, DB_SAMP_MIPMAPLODBIAS, value);
        break;
    case D3DTSS_MAXMIPLEVEL:
        DbSetSamplerState(db, stage, DB_SAMP_MAXMIPLEVEL, value);
        break;
    case D3DTSS_MAXANISOTROPY:
        DbSetSamplerState(db, stage, DB_SAMP_MAXANISOTROPY, value);
        break;
    default:
        if (type >= D3DTSS_COLOROP && type <= D3DTSS_TEXTURETRANSFORMFLAGS)
            DbSetTextureStageState(db, stage, type, value);
        break;
    }
}

/* direct3d 7's transform types as direct3d 9's. */
static DWORD transform9(DWORD type)
{
    switch (type) {
    case D3DTRANSFORMSTATE_WORLD:
        return DB_TS_WORLD;
    case D3DTRANSFORMSTATE_WORLD1:
    case D3DTRANSFORMSTATE_WORLD2:
    case D3DTRANSFORMSTATE_WORLD3:
        return DB_TS_WORLD + type - D3DTRANSFORMSTATE_WORLD;
    default:
        return type;
    }
}

static BOOL valid_transform(DWORD type)
{
    return (type >= D3DTRANSFORMSTATE_WORLD && type <= D3DTRANSFORMSTATE_WORLD3) ||
           (type >= D3DTRANSFORMSTATE_TEXTURE0 && type <= D3DTRANSFORMSTATE_TEXTURE7);
}

/* the projection of the first interfaces ends with their viewport's clip volume. */
static void push_transform(D3DDEV *dev, DWORD type)
{
    D3DMATRIX m = dev->transforms[type];
    if (type == D3DTRANSFORMSTATE_PROJECTION && dev->legacy)
        m = D3dMultiply(&m, D3dLegacyClipMap(dev));
    DbSetTransform(dev->db, transform9(type), &m._11);
}

void D3dPushProjection(D3DDEV *dev)
{
    if (active(dev))
        push_transform(dev, D3DTRANSFORMSTATE_PROJECTION);
}

/* everything of the device to the backend, which another device's work changed. */
void D3dDevActivate(D3DDEV *dev)
{
    if (g_active == dev)
        return;
    g_active = dev;
    for (DWORD stage = 0; stage < D3D_STAGES; stage++)
        for (DWORD t = D3DTSS_COLOROP; t <= D3DTSS_TEXTURETRANSFORMFLAGS; t++)
            push_tss(dev, stage, t, dev->tss[stage][t]);
    for (DWORD i = 0; i < 256; i++)
        if (dev->rs[i] != 0xFFFFFFFF)
            push_rs(dev, i, dev->rs[i]);
    for (DWORD t = 1; t < RTL_NUMBER_OF(dev->transforms); t++)
        if (valid_transform(t))
            push_transform(dev, t);
    DbSetViewport(dev->db, dev->viewport.dwX, dev->viewport.dwY, dev->viewport.dwWidth, dev->viewport.dwHeight,
                  dev->viewport.dvMinZ, dev->viewport.dvMaxZ);
    DbSetMaterial(dev->db, &dev->material);
    for (DWORD i = 0; i < dev->light_count; i++)
        if (dev->lights[i].set) {
            DbSetLight(dev->db, i, &dev->lights[i].light);
            DbLightEnable(dev->db, i, dev->lights[i].enabled);
        }
    for (DWORD i = 0; i < D3D_CLIP_PLANES; i++)
        DbSetClipPlane(dev->db, i, dev->clip_planes[i]);
    dev->keyed = FALSE;
    dev->stage_end = D3D_STAGES;
    if (dev->legacy)
        D3dLegacyActivate(dev);
}

/* ------------------------------------------------------------------ immediate state changes */

void D3dSetRs(D3DDEV *dev, DWORD type, DWORD value)
{
    dev->rs[type] = value;
    if (active(dev))
        push_rs(dev, type, value);
}

void D3dSetTss(D3DDEV *dev, DWORD stage, DWORD type, DWORD value)
{
    dev->tss[stage][type] = value;
    if (active(dev) && !(type == D3DTSS_COLOROP && stage == dev->stage_end))
        push_tss(dev, stage, type, value);
}

void D3dSetTransform(D3DDEV *dev, DWORD type, const D3DMATRIX *m)
{
    dev->transforms[type] = *m;
    if (active(dev))
        push_transform(dev, type);
}

void D3dSetViewport(D3DDEV *dev, const D3DVIEWPORT7 *vp)
{
    dev->viewport = *vp;
    /* a depth range thinner than a thousandth is widened toward the middle. */
    if (dev->viewport.dvMaxZ - dev->viewport.dvMinZ < 0.001f) {
        if (dev->viewport.dvMaxZ < 0.5f)
            dev->viewport.dvMaxZ = dev->viewport.dvMinZ + 0.001f;
        else
            dev->viewport.dvMinZ = dev->viewport.dvMaxZ - 0.001f;
    }
    if (active(dev))
        DbSetViewport(dev->db, dev->viewport.dwX, dev->viewport.dwY, dev->viewport.dwWidth, dev->viewport.dwHeight,
                      dev->viewport.dvMinZ, dev->viewport.dvMaxZ);
}

void D3dSetMaterial(D3DDEV *dev, const D3DMATERIAL7 *m)
{
    dev->material = *m;
    if (active(dev))
        DbSetMaterial(dev->db, m);
}

D3DLIGHTSLOT *D3dLightSlot(D3DDEV *dev, DWORD index)
{
    if (index >= dev->light_count) {
        DWORD count = index + 10;
        D3DLIGHTSLOT *lights = dev->lights
                                   ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dev->lights, count * sizeof(*lights))
                                   : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*lights));
        if (!lights)
            return nullptr;
        dev->lights = lights;
        dev->light_count = count;
    }
    return &dev->lights[index];
}

HRESULT D3dSetLight(D3DDEV *dev, DWORD index, const D3DLIGHT7 *light)
{
    D3DLIGHTSLOT *slot = D3dLightSlot(dev, index);
    if (!slot)
        return DDERR_OUTOFMEMORY;
    slot->light = *light;
    slot->set = TRUE;
    if (active(dev))
        DbSetLight(dev->db, index, light);
    return D3D_OK;
}

HRESULT D3dLightEnable(D3DDEV *dev, DWORD index, BOOL enable)
{
    D3DLIGHTSLOT *slot = D3dLightSlot(dev, index);
    if (!slot)
        return DDERR_OUTOFMEMORY;
    if (!slot->set) {
        /* a light enabled before it was set is a white directional light along z. */
        D3DLIGHT7 light = {D3DLIGHT_DIRECTIONAL, {{1.0f}, {1.0f}, {1.0f}, {0.0f}}};
        light.dvDirection.z = 1.0f;
        D3dSetLight(dev, index, &light);
    }
    slot->enabled = enable != 0;
    if (active(dev))
        DbLightEnable(dev->db, index, slot->enabled);
    return D3D_OK;
}

static void set_clip_plane(D3DDEV *dev, DWORD index, const float *plane)
{
    memcpy(dev->clip_planes[index], plane, sizeof(dev->clip_planes[index]));
    if (active(dev))
        DbSetClipPlane(dev->db, index, plane);
}

/* the surface references a device holds, through the interface of its version; an aggregate holds none. */
static void hold(D3DDEV *dev, DDSURF *s)
{
    if (s && dev->surface_version)
        DdSurfAddRef(s, DdSurfRefOf(s, dev->surface_version));
}

static void unhold(D3DDEV *dev, DDSURF *s)
{
    if (s && dev->surface_version)
        DdSurfRelease(s, DdSurfRefOf(s, dev->surface_version));
}

/* a texture bound through a handle holds no reference: the surface going unbinds it. */
void D3dSetTexture(D3DDEV *dev, DWORD stage, DDSURF *s)
{
    if (dev->textures[stage] == s && !(dev->weak_textures >> stage & 1))
        return;
    if (s)
        hold(dev, s);
    DDSURF *old = dev->textures[stage];
    BOOL weak = dev->weak_textures >> stage & 1;
    dev->textures[stage] = s;
    dev->weak_textures &= ~(1u << stage);
    if (old && !weak)
        unhold(dev, old);
}

void D3dSetTextureWeak(D3DDEV *dev, DWORD stage, DDSURF *s)
{
    D3dSetTexture(dev, stage, nullptr);
    dev->textures[stage] = s;
    if (s)
        dev->weak_textures |= 1u << stage;
}

/* ------------------------------------------------------------------ the targets and textures of a draw */

/* the targets bound, and depth fills waiting for the depth buffer done now that it is. */
BOOL D3dBindTargets(D3DDEV *dev)
{
    D3dDevActivate(dev);
    DbImage *target = D3dSurfImage(dev, dev->target, DB_TARGET);
    if (!target)
        return FALSE;
    DbImage *depth = dev->depth ? D3dSurfImage(dev, dev->depth, DB_DEPTH) : nullptr;
    DbSetTargets(dev->db, target, depth);
    D3DSURFX *x = dev->depth ? dev->depth->d3d : nullptr;
    if (depth && x && x->fill) {
        x->fill = FALSE;
        DbSetViewport(dev->db, 0, 0, dev->target->desc.dwWidth, dev->target->desc.dwHeight, 0.0f, 1.0f);
        D3DRECT r = {{x->fill_rect.left}, {x->fill_rect.top}, {x->fill_rect.right}, {x->fill_rect.bottom}};
        DWORD flags = D3DCLEAR_ZBUFFER | ((dev->depth->format.pf.dwFlags & DDPF_STENCILBUFFER) ? D3DCLEAR_STENCIL : 0);
        DbClear(dev->db, 1, &r, flags, 0, x->fill_z, x->fill_stencil);
        DbSetViewport(dev->db, dev->viewport.dwX, dev->viewport.dwY, dev->viewport.dwWidth, dev->viewport.dwHeight,
                      dev->viewport.dvMinZ, dev->viewport.dvMaxZ);
    }
    return TRUE;
}

/* whether an operation reads its texture argument, as the stage's arguments ask. */
static BOOL reads_texture(DWORD op, DWORD arg1, DWORD arg2)
{
    BOOL one = (arg1 & D3DTA_SELECTMASK) == D3DTA_TEXTURE, two = (arg2 & D3DTA_SELECTMASK) == D3DTA_TEXTURE;
    switch (op) {
    case D3DTOP_DISABLE:
        return FALSE;
    case D3DTOP_SELECTARG1:
        return one;
    case D3DTOP_SELECTARG2:
        return two;
    default:
        return one || two;
    }
}

/*
 * the textures of the stages, and the color key of the first: its keyed
 * texels have alpha 0 and are not drawn, as the hardware drivers of
 * direct3d 7 did through the alpha test.
 */
static void bind_textures(D3DDEV *dev)
{
    BOOL keyed = FALSE;
    DWORD end = D3D_STAGES;
    for (DWORD stage = 0; stage < D3D_STAGES; stage++) {
        DDSURF *s = dev->textures[stage];
        DbImage *image = s && !s->lost ? D3dSurfImage(dev, s, DB_TEXTURE) : nullptr;
        DbSetTexture(dev->db, stage, image);
        if (!stage && image && s->d3d && s->d3d->keyed)
            keyed = TRUE;
        /* windows' emulation ends the stages at the first reading a texture it lacks. */
        const DWORD *t = dev->tss[stage];
        if (!image && end == D3D_STAGES &&
            (reads_texture(t[D3DTSS_COLOROP], t[D3DTSS_COLORARG1], t[D3DTSS_COLORARG2]) ||
             reads_texture(t[D3DTSS_ALPHAOP], t[D3DTSS_ALPHAARG1], t[D3DTSS_ALPHAARG2])))
            end = stage;
    }
    if (end != dev->stage_end) {
        if (dev->stage_end < D3D_STAGES)
            DbSetTextureStageState(dev->db, dev->stage_end, D3DTSS_COLOROP, dev->tss[dev->stage_end][D3DTSS_COLOROP]);
        if (end < D3D_STAGES)
            DbSetTextureStageState(dev->db, end, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->stage_end = end;
    }
    keyed = keyed && !dev->rs[D3DRENDERSTATE_ALPHATESTENABLE];
    if (keyed != dev->keyed) {
        dev->keyed = keyed;
        DbSetRenderState(dev->db, D3DRENDERSTATE_ALPHATESTENABLE, keyed || dev->rs[D3DRENDERSTATE_ALPHATESTENABLE]);
        DbSetRenderState(dev->db, D3DRENDERSTATE_ALPHAFUNC, keyed ? D3DCMP_GREATER : dev->rs[D3DRENDERSTATE_ALPHAFUNC]);
        DbSetRenderState(dev->db, D3DRENDERSTATE_ALPHAREF, keyed ? 0 : dev->rs[D3DRENDERSTATE_ALPHAREF]);
    }
}

/* ------------------------------------------------------------------ drawing */

HRESULT D3dClearRects(D3DDEV *dev, DWORD n, const D3DRECT *list, DWORD flags, DWORD color, float z, DWORD stencil)
{
    if (!n)
        return D3D_OK;
    BOOL whole = (flags & D3DCLEAR_TARGET) && n == 1 && !list[0].x1 && !list[0].y1 &&
                 list[0].x2 == (LONG)dev->target->desc.dwWidth && list[0].y2 == (LONG)dev->target->desc.dwHeight;
    /* a target cleared whole needs nothing of its memory. */
    if (whole)
        D3dSurfReplaced(dev->target);
    if (!D3dBindTargets(dev))
        return DDERR_SURFACELOST;
    /* the backend clears within its viewport, which the rectangles may pass. */
    DbSetViewport(dev->db, 0, 0, dev->target->desc.dwWidth, dev->target->desc.dwHeight, 0.0f, 1.0f);
    HRESULT hr = DbClear(dev->db, n, list, flags, color, z, stencil);
    DbSetViewport(dev->db, dev->viewport.dwX, dev->viewport.dwY, dev->viewport.dwWidth, dev->viewport.dwHeight,
                  dev->viewport.dvMinZ, dev->viewport.dvMaxZ);
    if (flags & D3DCLEAR_TARGET)
        D3dSurfDrawn(dev->target);
    return SUCCEEDED(hr) ? D3D_OK : hr;
}

DWORD D3dFvfSize(DWORD fvf)
{
    static const DWORD POSITION[] = {12, 12, 16, 16, 20, 24, 28, 32};
    DWORD size = POSITION[(fvf & D3DFVF_POSITION_MASK) >> 1];
    if (fvf & D3DFVF_NORMAL)
        size += 12;
    if (fvf & D3DFVF_RESERVED1)
        size += 4;
    if (fvf & D3DFVF_DIFFUSE)
        size += 4;
    if (fvf & D3DFVF_SPECULAR)
        size += 4;
    DWORD sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD i = 0; i < sets && i < 8; i++) {
        static const DWORD COORDS[] = {8, 12, 16, 4};
        size += fvf >> 16 ? COORDS[(fvf >> (16 + 2 * i)) & 3] : 8;
    }
    return size;
}

/* a remainder of a triangle list is not drawn, where windows rounds it up and reads past the end. */
DWORD D3dPrimitiveCount(D3DPRIMITIVETYPE type, DWORD n)
{
    switch (type) {
    case D3DPT_POINTLIST:
        return n;
    case D3DPT_LINELIST:
        return n / 2;
    case D3DPT_LINESTRIP:
        return n >= 2 ? n - 1 : 0;
    case D3DPT_TRIANGLELIST:
        return n / 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return n >= 3 ? n - 2 : 0;
    default:
        return 0;
    }
}

HRESULT D3dDraw(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD vertex_count,
                const WORD *indices, DWORD index_count, DWORD flags)
{
    DWORD primitives = D3dPrimitiveCount(type, indices ? index_count : vertex_count);
    if (!primitives || !vertices || !vertex_count)
        return D3D_OK;
    if (!D3dBindTargets(dev))
        return DDERR_SURFACELOST;
    bind_textures(dev);
    BOOL unlit = dev->version == 7 && (flags & D3DDP_DONOTLIGHT) && dev->rs[D3DRENDERSTATE_LIGHTING];
    if (unlit)
        DbSetRenderState(dev->db, D3DRENDERSTATE_LIGHTING, FALSE);
    DWORD stride = D3dFvfSize(fvf);
    HRESULT hr = indices ? DbDrawIndexed(dev->db, type, primitives, vertex_count, indices, vertices, stride, fvf)
                         : DbDraw(dev->db, type, primitives, vertices, stride, fvf);
    if (unlit)
        DbSetRenderState(dev->db, D3DRENDERSTATE_LIGHTING, TRUE);
    D3dSurfDrawn(dev->target);
    return SUCCEEDED(hr) ? D3D_OK : hr;
}

/* strided vertices packed as the format lays them out. */
const BYTE *D3dPackStrided(D3DDEV *dev, DWORD fvf, const D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD count)
{
    DWORD stride = D3dFvfSize(fvf);
    SIZE_T need = (SIZE_T)stride * count;
    if (need > dev->scratch_size) {
        BYTE *scratch = dev->scratch ? HeapReAlloc(GetProcessHeap(), 0, dev->scratch, need)
                                     : HeapAlloc(GetProcessHeap(), 0, need);
        if (!scratch)
            return nullptr;
        dev->scratch = scratch;
        dev->scratch_size = need;
    }
    static const DWORD POSITION[] = {12, 12, 16, 16, 20, 24, 28, 32};
    DWORD sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD v = 0; v < count; v++) {
        BYTE *out = dev->scratch + (SIZE_T)v * stride;
        DWORD size = POSITION[(fvf & D3DFVF_POSITION_MASK) >> 1];
        memcpy(out, (const BYTE *)data->position.lpvData + (SIZE_T)v * data->position.dwStride, size);
        out += size;
        if (fvf & D3DFVF_NORMAL) {
            memcpy(out, (const BYTE *)data->normal.lpvData + (SIZE_T)v * data->normal.dwStride, 12);
            out += 12;
        }
        if (fvf & D3DFVF_RESERVED1)
            out += 4;
        if (fvf & D3DFVF_DIFFUSE) {
            memcpy(out, (const BYTE *)data->diffuse.lpvData + (SIZE_T)v * data->diffuse.dwStride, 4);
            out += 4;
        }
        if (fvf & D3DFVF_SPECULAR) {
            memcpy(out, (const BYTE *)data->specular.lpvData + (SIZE_T)v * data->specular.dwStride, 4);
            out += 4;
        }
        for (DWORD i = 0; i < sets && i < D3DDP_MAXTEXCOORD; i++) {
            static const DWORD COORDS[] = {8, 12, 16, 4};
            DWORD n = fvf >> 16 ? COORDS[(fvf >> (16 + 2 * i)) & 3] : 8;
            memcpy(out, (const BYTE *)data->textureCoords[i].lpvData + (SIZE_T)v * data->textureCoords[i].dwStride, n);
            out += n;
        }
    }
    return dev->scratch;
}

/* ------------------------------------------------------------------ creation */

DDSURF *D3dAttachedDepth(DDSURF *s)
{
    for (LIST_ENTRY *e = s->attached.Flink; e != &s->attached; e = e->Flink) {
        DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
        if (a->desc.ddsCaps.dwCaps & DDSCAPS_ZBUFFER)
            return a;
    }
    return nullptr;
}

DWORD D3dDdbd(DWORD bits)
{
    switch (bits) {
    case 1:
        return DDBD_1;
    case 2:
        return DDBD_2;
    case 4:
        return DDBD_4;
    case 8:
        return DDBD_8;
    case 16:
        return DDBD_16;
    case 24:
        return DDBD_24;
    case 32:
        return DDBD_32;
    default:
        return 0;
    }
}

BOOL D3dPalettized(const DDSURF *s)
{
    return (s->format.pf.dwFlags & (DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) != 0;
}

/* the checks CreateDevice and SetRenderTarget share: the surface must take 3d drawing and a palette if indexed. */
HRESULT D3dCheckPalette(DDSURF *target)
{
    if (!target->palette && target->format.bpp < 16)
        return DDERR_NOPALETTEATTACHED;
    return D3D_OK;
}

void D3dHoldTargets(D3DDEV *dev, DDSURF *target, DDSURF *depth)
{
    hold(dev, target);
    hold(dev, depth);
    if (target->palette && dev->surface_version)
        target->palette->iface.lpVtbl->AddRef(&target->palette->iface);
    DDSURF *old_target = dev->target, *old_depth = dev->depth;
    DDPAL *old_palette = dev->palette;
    dev->target = target;
    dev->depth = depth;
    dev->palette = target->palette;
    if (old_palette && dev->surface_version)
        old_palette->iface.lpVtbl->Release(&old_palette->iface);
    unhold(dev, old_depth);
    unhold(dev, old_target);
    D3dPal8Target(dev);
}

static void default_state(D3DDEV *dev)
{
    memset(dev->rs, 0xFF, sizeof(dev->rs));
    memset(dev->tss, 0xFF, sizeof(dev->tss));
    static const DWORD DEFAULTS[][2] = {
        {D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1},
        {D3DRENDERSTATE_SPECULARENABLE, 0},
        {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID},
        {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
        {D3DRENDERSTATE_LINEPATTERN, 0},
        {D3DRENDERSTATE_ZWRITEENABLE, 1},
        {D3DRENDERSTATE_ALPHATESTENABLE, 0},
        {D3DRENDERSTATE_LASTPIXEL, 1},
        {D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE},
        {D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO},
        {D3DRENDERSTATE_CULLMODE, D3DCULL_CCW},
        {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRENDERSTATE_ALPHAREF, 0},
        {D3DRENDERSTATE_ALPHAFUNC, D3DCMP_ALWAYS},
        {D3DRENDERSTATE_DITHERENABLE, 0},
        {D3DRENDERSTATE_FOGENABLE, 0},
        {D3DRENDERSTATE_ZVISIBLE, 0},
        {D3DRENDERSTATE_STIPPLEDALPHA, 0},
        {D3DRENDERSTATE_FOGCOLOR, 0},
        {D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_NONE},
        {D3DRENDERSTATE_FOGSTART, 0},
        {D3DRENDERSTATE_FOGEND, 0x3F800000},
        {D3DRENDERSTATE_FOGDENSITY, 0x3F800000},
        {D3DRENDERSTATE_COLORKEYENABLE, 0},
        {D3DRENDERSTATE_ALPHABLENDENABLE, 0},
        {D3DRENDERSTATE_ZBIAS, 0},
        {D3DRENDERSTATE_RANGEFOGENABLE, 0},
        {D3DRENDERSTATE_STENCILENABLE, 0},
        {D3DRENDERSTATE_STENCILFAIL, D3DSTENCILOP_KEEP},
        {D3DRENDERSTATE_STENCILZFAIL, D3DSTENCILOP_KEEP},
        {D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP},
        {D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS},
        {D3DRENDERSTATE_STENCILREF, 0},
        {D3DRENDERSTATE_STENCILMASK, 0xFFFFFFFF},
        {D3DRENDERSTATE_STENCILWRITEMASK, 0xFFFFFFFF},
        {D3DRENDERSTATE_TEXTUREFACTOR, 0xFFFFFFFF},
        {D3DRENDERSTATE_AMBIENT, 0},
        {D3DRENDERSTATE_COLORVERTEX, 1},
        {D3DRENDERSTATE_FOGVERTEXMODE, D3DFOG_NONE},
        {D3DRENDERSTATE_CLIPPING, 1},
        {D3DRENDERSTATE_LIGHTING, 1},
        {D3DRENDERSTATE_EXTENTS, 0},
        {D3DRENDERSTATE_NORMALIZENORMALS, 0},
        {D3DRENDERSTATE_LOCALVIEWER, 1},
        {D3DRENDERSTATE_COLORKEYBLENDENABLE, 0},
        {D3DRENDERSTATE_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL},
        {D3DRENDERSTATE_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL},
        {D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1},
        {D3DRENDERSTATE_SPECULARMATERIALSOURCE, D3DMCS_COLOR2},
        {D3DRENDERSTATE_VERTEXBLEND, 0},
        {D3DRENDERSTATE_CLIPPLANEENABLE, 0},
    };
    for (UINT i = 0; i < RTL_NUMBER_OF(DEFAULTS); i++)
        dev->rs[DEFAULTS[i][0]] = DEFAULTS[i][1];
    dev->rs[D3DRENDERSTATE_ZENABLE] = dev->depth ? D3DZB_TRUE : D3DZB_FALSE;
    for (DWORD i = D3DRENDERSTATE_WRAP0; i <= D3DRENDERSTATE_WRAP7; i++)
        dev->rs[i] = 0;
    for (DWORD stage = 0; stage < D3D_STAGES; stage++) {
        DWORD *t = dev->tss[stage];
        t[D3DTSS_COLOROP] = stage ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        t[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = stage ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        for (DWORD i = D3DTSS_BUMPENVMAT00; i <= D3DTSS_BUMPENVMAT11; i++)
            t[i] = 0;
        t[D3DTSS_TEXCOORDINDEX] = 0;
        t[D3DTSS_ADDRESS] = t[D3DTSS_ADDRESSU] = t[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        t[D3DTSS_BORDERCOLOR] = 0;
        t[D3DTSS_MAGFILTER] = D3DTFG_POINT;
        t[D3DTSS_MINFILTER] = D3DTFN_POINT;
        t[D3DTSS_MIPFILTER] = D3DTFP_NONE;
        t[D3DTSS_MIPMAPLODBIAS] = 0;
        t[D3DTSS_MAXMIPLEVEL] = 0;
        t[D3DTSS_MAXANISOTROPY] = 1;
        t[D3DTSS_BUMPENVLSCALE] = t[D3DTSS_BUMPENVLOFFSET] = 0;
        t[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
    }
    static const D3DMATRIX IDENTITY = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (DWORD i = 0; i < RTL_NUMBER_OF(dev->transforms); i++)
        dev->transforms[i] = IDENTITY;
    dev->viewport = (D3DVIEWPORT7){0, 0, dev->target->desc.dwWidth, dev->target->desc.dwHeight, 0.0f, 1.0f};
    dev->stage_end = D3D_STAGES;
}

HRESULT D3dCreateDevice7(D3DOBJ *d3d, const GUID *guid, DDSURF *target, IDirect3DDevice7 **out)
{
    if (!guid || !D3dIsDeviceGuid(guid))
        return DDERR_INVALIDPARAMS;
    D3DDEVICEDESC7 desc;
    /* the ramp device and those of the reference rasterizer have no provider in windows 11. */
    if (!D3dDeviceDesc7(guid, &desc))
        return E_NOINTERFACE;
    BOOL hal = D3dIsHalGuid(guid);
    DWORD caps = target->desc.ddsCaps.dwCaps;
    if (!(caps & DDSCAPS_3DDEVICE) || (caps & DDSCAPS_ZBUFFER))
        return DDERR_INVALIDCAPS;
    if (target->desc.dwWidth > 2048 || target->desc.dwHeight > 2048)
        return DDERR_INVALIDOBJECT;
    HRESULT hr = D3dCheckPalette(target);
    if (FAILED(hr))
        return hr;
    DDSURF *depth = D3dAttachedDepth(target);
    if (hal && !(caps & DDSCAPS_VIDEOMEMORY))
        return D3DERR_SURFACENOTINVIDMEM;
    if ((target->format.pf.dwFlags & DDPF_RGB) && !(D3dDdbd(target->format.bpp) & desc.dwDeviceRenderBitDepth))
        return DDERR_INVALIDPIXELFORMAT;
    if (depth) {
        DWORD zcaps = depth->desc.ddsCaps.dwCaps;
        if (hal && !(zcaps & DDSCAPS_VIDEOMEMORY))
            return D3DERR_ZBUFF_NEEDS_VIDEOMEMORY;
        if (!hal && !(zcaps & DDSCAPS_SYSTEMMEMORY))
            return D3DERR_ZBUFF_NEEDS_SYSTEMMEMORY;
        if ((depth->format.pf.dwFlags & DDPF_ZBUFFER) && !D3dDdbd(depth->format.pf.dwZBufferBitDepth))
            return DDERR_INVALIDPIXELFORMAT;
    }
    if (!(D3dDdbd(target->format.bpp) & desc.dwDeviceRenderBitDepth))
        return DDERR_INVALIDOBJECT;
    if (!D3dPalettized(target) && (!target->format.pf.dwRBitMask || !target->format.pf.dwGBitMask ||
                                !target->format.pf.dwBBitMask))
        return DDERR_INVALIDPIXELFORMAT;
    DbDevice *db = DbDeviceGet();
    if (!db)
        return D3DERR_INITFAILED;
    D3DDEV *dev = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dev));
    if (!dev)
        return DDERR_OUTOFMEMORY;
    dev->iface7.lpVtbl = &dev7_vtbl;
    dev->ref = 1;
    dev->version = 7;
    dev->surface_version = DDV7;
    dev->d3d = d3d;
    dev->guid = *guid;
    dev->hal = hal;
    dev->desc = desc;
    dev->db = db;
    D3dHoldTargets(dev, target, depth);
    default_state(dev);
    InsertTailList(&d3d->devices, &dev->links);
    D3dObjAddRef(d3d);
    *out = &dev->iface7;
    return D3D_OK;
}

/* ------------------------------------------------------------------ state blocks */

static D3DITEM *append(D3DBLOCK *b, DWORD op, DWORD a, DWORD c)
{
    if (b->count == b->capacity) {
        DWORD capacity = b->capacity ? b->capacity * 2 : 16;
        D3DITEM *items = b->items ? HeapReAlloc(GetProcessHeap(), 0, b->items, capacity * sizeof(*items))
                                  : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*items));
        if (!items)
            return nullptr;
        b->items = items;
        b->capacity = capacity;
    }
    D3DITEM *item = &b->items[b->count++];
    memset(item, 0, sizeof(*item));
    item->op = op;
    item->a = a;
    item->b = c;
    return item;
}

static void free_block(D3DBLOCK *b)
{
    if (!b)
        return;
    for (DWORD i = 0; i < b->count; i++)
        if (b->items[i].op == D3DI_TEXTURE && b->items[i].texture)
            DdSurfRelease(b->items[i].texture, DdSurfRefOf(b->items[i].texture, DDV7));
    HeapFree(GetProcessHeap(), 0, b->items);
    HeapFree(GetProcessHeap(), 0, b);
}

static void record_texture(D3DITEM *item, DDSURF *s)
{
    if (item->texture)
        DdSurfRelease(item->texture, DdSurfRefOf(item->texture, DDV7));
    item->texture = s;
    if (s)
        DdSurfAddRef(s, DdSurfRefOf(s, DDV7));
}

/* a handle for a block: windows reserves 0 and reuses freed handles. */
static DWORD new_handle(D3DDEV *dev, D3DBLOCK *block)
{
    for (DWORD i = 1; i < dev->block_count; i++)
        if (!dev->blocks[i]) {
            dev->blocks[i] = block;
            return i;
        }
    DWORD count = dev->block_count ? dev->block_count + 10 : 11;
    D3DBLOCK **blocks = dev->blocks ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dev->blocks, count * sizeof(*blocks))
                                    : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*blocks));
    if (!blocks)
        return 0;
    DWORD handle = dev->block_count ? dev->block_count : 1;
    dev->blocks = blocks;
    dev->block_count = count;
    blocks[handle] = block;
    return handle;
}

static D3DBLOCK *block_of(D3DDEV *dev, DWORD handle)
{
    return handle && handle < dev->block_count ? dev->blocks[handle] : nullptr;
}

static HRESULT apply_block(D3DDEV *dev, D3DBLOCK *b)
{
    for (DWORD i = 0; i < b->count; i++) {
        D3DITEM *item = &b->items[i];
        switch (item->op) {
        case D3DI_RS:
            if (item->a < 256 && dev->rs[item->a] != item->b)
                D3dSetRs(dev, item->a, item->b);
            break;
        case D3DI_TSS:
            if (item->a < D3D_STAGES && item->b < 32 && dev->tss[item->a][item->b] != item->value)
                D3dSetTss(dev, item->a, item->b, item->value);
            break;
        case D3DI_TEXTURE:
            D3dSetTexture(dev, item->a, item->texture);
            break;
        case D3DI_VIEWPORT: {
            const D3DVIEWPORT7 *vp = &item->viewport;
            DWORD w = dev->target->desc.dwWidth, h = dev->target->desc.dwHeight;
            if (vp->dwX > w || vp->dwY > h || vp->dwX + vp->dwWidth > w || vp->dwY + vp->dwHeight > h)
                return DDERR_INVALIDPARAMS;
            D3dSetViewport(dev, vp);
            break;
        }
        case D3DI_MATERIAL:
            D3dSetMaterial(dev, &item->material);
            break;
        case D3DI_LIGHT:
            if (FAILED(D3dSetLight(dev, item->a, &item->light)))
                return DDERR_OUTOFMEMORY;
            break;
        case D3DI_LIGHTENABLE:
            D3dLightEnable(dev, item->a, item->b);
            break;
        case D3DI_TRANSFORM:
            if (valid_transform(item->a))
                D3dSetTransform(dev, item->a, &item->matrix);
            break;
        case D3DI_CLIPPLANE:
            if (item->a < D3D_CLIP_PLANES)
                set_clip_plane(dev, item->a, item->plane);
            break;
        }
    }
    return D3D_OK;
}

/* every recorded value replaced by the device's current one. */
static HRESULT capture_block(D3DDEV *dev, D3DBLOCK *b)
{
    for (DWORD i = 0; i < b->count; i++) {
        D3DITEM *item = &b->items[i];
        switch (item->op) {
        case D3DI_RS:
            if (item->a < 256)
                item->b = dev->rs[item->a];
            break;
        case D3DI_TSS:
            if (item->a < D3D_STAGES && item->b < 32)
                item->value = dev->tss[item->a][item->b];
            break;
        case D3DI_TEXTURE:
            record_texture(item, dev->textures[item->a]);
            break;
        case D3DI_VIEWPORT:
            item->viewport = dev->viewport;
            break;
        case D3DI_MATERIAL:
            item->material = dev->material;
            break;
        case D3DI_LIGHT:
        case D3DI_LIGHTENABLE:
            if (item->a >= dev->light_count)
                return D3DERR_LIGHT_SET_FAILED;
            if (item->op == D3DI_LIGHT)
                item->light = dev->lights[item->a].light;
            else
                item->b = dev->lights[item->a].enabled;
            break;
        case D3DI_TRANSFORM:
            if (valid_transform(item->a))
                item->matrix = dev->transforms[item->a];
            break;
        case D3DI_CLIPPLANE:
            if (item->a < D3D_CLIP_PLANES)
                memcpy(item->plane, dev->clip_planes[item->a], sizeof(item->plane));
            break;
        }
    }
    return D3D_OK;
}

static const DWORD ALL_RS[] = {4, 2, 29, 7, 8, 9, 10, 14, 15, 16, 19, 20, 22, 23, 24, 25, 26, 28, 33, 34, 35,
                               36, 37, 38, 40, 41, 27, 47, 48, 52, 53, 54, 55, 56, 57, 58, 59, 60, 128, 129, 130,
                               131, 132, 133, 134, 135, 139, 141, 140, 136, 137, 138, 143, 142, 148, 147, 145, 146,
                               151, 152, 144};
static const DWORD PIXEL_RS[] = {4, 2, 7, 8, 9, 10, 14, 15, 16, 19, 20, 23, 24, 25, 26, 33, 36, 37, 38, 40, 41,
                                 27, 47, 52, 53, 54, 55, 56, 57, 58, 59, 60, 128, 129, 130, 131, 132, 133, 134, 135,
                                 144};
static const DWORD VERTEX_RS[] = {9, 29, 22, 28, 34, 35, 36, 37, 38, 48, 139, 141, 140, 136, 137, 138, 143, 142,
                                  148, 147, 145, 146, 151, 152};

/* the predefined sets, filled with the current values. */
static BOOL predefined(D3DDEV *dev, D3DBLOCK *b, D3DSTATEBLOCKTYPE type)
{
    const DWORD *rs = type == D3DSBT_ALL ? ALL_RS : type == D3DSBT_PIXELSTATE ? PIXEL_RS : VERTEX_RS;
    UINT rs_count = type == D3DSBT_ALL     ? RTL_NUMBER_OF(ALL_RS)
                    : type == D3DSBT_PIXELSTATE ? RTL_NUMBER_OF(PIXEL_RS)
                                                : RTL_NUMBER_OF(VERTEX_RS);
    for (UINT i = 0; i < rs_count; i++)
        if (!append(b, D3DI_RS, rs[i], 0))
            return FALSE;
    DWORD stages = dev->desc.wMaxTextureBlendStages;
    for (DWORD s = 0; s < stages; s++)
        for (DWORD t = D3DTSS_COLOROP; t <= D3DTSS_TEXTURETRANSFORMFLAGS; t++)
            if ((type != D3DSBT_VERTEXSTATE || t == D3DTSS_TEXCOORDINDEX || t == D3DTSS_TEXTURETRANSFORMFLAGS) &&
                !append(b, D3DI_TSS, s, t))
                return FALSE;
    if (type == D3DSBT_ALL) {
        for (DWORD s = 0; s < stages; s++)
            if (!append(b, D3DI_TEXTURE, s, 0))
                return FALSE;
        if (!append(b, D3DI_VIEWPORT, 0, 0))
            return FALSE;
        static const DWORD TRANSFORMS[] = {1, 2, 3, 4, 5, 6};
        for (UINT i = 0; i < RTL_NUMBER_OF(TRANSFORMS); i++)
            if (!append(b, D3DI_TRANSFORM, TRANSFORMS[i], 0))
                return FALSE;
        for (DWORD s = 0; s < stages; s++)
            if (!append(b, D3DI_TRANSFORM, D3DTRANSFORMSTATE_TEXTURE0 + s, 0))
                return FALSE;
        for (DWORD i = 0; i < dev->desc.wMaxUserClipPlanes; i++)
            if (!append(b, D3DI_CLIPPLANE, i, 0))
                return FALSE;
        if (!append(b, D3DI_MATERIAL, 0, 0))
            return FALSE;
    }
    if (type == D3DSBT_ALL || type == D3DSBT_VERTEXSTATE)
        for (DWORD i = 0; i < dev->light_count; i++)
            if (dev->lights[i].set && ((type == D3DSBT_ALL && !append(b, D3DI_LIGHT, i, 0)) ||
                                       !append(b, D3DI_LIGHTENABLE, i, 0)))
                return FALSE;
    capture_block(dev, b);
    return TRUE;
}

/* ------------------------------------------------------------------ IUnknown */

static D3DDEV *enter(void *iface, HRESULT *hr)
{
    DdEnter();
    D3DDEV *dev = D3dDevFromIface7(iface);
    if (!dev) {
        DdLeave(D3D_OK);
        *hr = DDERR_INVALIDOBJECT;
    }
    return dev;
}

static HRESULT STDMETHODCALLTYPE dev7_query(IDirect3DDevice7 *This, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirect3DDevice7))
        return DdLeave(E_NOINTERFACE);
    dev->ref++;
    *out = &dev->iface7;
    return DdLeave(D3D_OK);
}

static ULONG STDMETHODCALLTYPE dev7_add_ref(IDirect3DDevice7 *This)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return 0;
    ULONG ref = (ULONG)++dev->ref;
    DdLeave(D3D_OK);
    return ref;
}

void D3dDevDestroy(D3DDEV *dev)
{
    if (g_active == dev)
        g_active = nullptr;
    if (dev->legacy)
        D3dLegacyDestroy(dev);
    for (DWORD i = 0; i < D3D_STAGES; i++)
        D3dSetTexture(dev, i, nullptr);
    for (DWORD i = 0; i < dev->block_count; i++)
        free_block(dev->blocks[i]);
    free_block(dev->recording);
    HeapFree(GetProcessHeap(), 0, dev->blocks);
    HeapFree(GetProcessHeap(), 0, dev->lights);
    HeapFree(GetProcessHeap(), 0, dev->scratch);
    D3dPal8Free(dev);
    RemoveEntryList(&dev->links);
    D3DOBJ *d3d = dev->d3d;
    BOOL held = dev->version != 1;
    if (dev->palette && dev->surface_version)
        dev->palette->iface.lpVtbl->Release(&dev->palette->iface);
    unhold(dev, dev->depth);
    unhold(dev, dev->target);
    HeapFree(GetProcessHeap(), 0, dev);
    /* a first device, which its surface owns, holds nothing of the direct3d object. */
    if (held)
        D3dObjRelease(d3d);
}

static ULONG STDMETHODCALLTYPE dev7_release(IDirect3DDevice7 *This)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return 0;
    ULONG ref = (ULONG)--dev->ref;
    if (!ref)
        D3dDevDestroy(dev);
    DdLeave(D3D_OK);
    return ref;
}

/* ------------------------------------------------------------------ description */

static HRESULT STDMETHODCALLTYPE dev7_get_caps(IDirect3DDevice7 *This, LPD3DDEVICEDESC7 desc)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!desc)
        return DdLeave(DDERR_INVALIDPARAMS);
    *desc = dev->desc;
    return DdLeave(D3D_OK);
}

#define RGB_FORMAT(bits, r, g, b, a)                                                                                   \
    {sizeof(DDPIXELFORMAT), DDPF_RGB | ((a) ? DDPF_ALPHAPIXELS : 0), 0, {bits}, {r}, {g}, {b}, {a}}

/* the rgb rasterizer's textures, in its order. */
static const DDPIXELFORMAT SOFTWARE_FORMATS[] = {
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0),
    RGB_FORMAT(32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000),
    RGB_FORMAT(16, 0xF800, 0x7E0, 0x1F, 0),
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0),
    {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED4, 0, {4}},
    {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_PALETTEINDEXED8, 0, {8}},
    RGB_FORMAT(16, 0x7C00, 0x3E0, 0x1F, 0x8000),
    RGB_FORMAT(16, 0xF00, 0xF0, 0xF, 0xF000),
    {sizeof(DDPIXELFORMAT), DDPF_LUMINANCE, 0, {8}, {0xFF}},
    {sizeof(DDPIXELFORMAT), DDPF_LUMINANCE | DDPF_ALPHAPIXELS, 0, {16}, {0xFF}, {0}, {0}, {0xFF00}},
};

static HRESULT STDMETHODCALLTYPE dev7_enum_texture_formats(IDirect3DDevice7 *This,
                                                           LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!callback)
        return DdLeave(DDERR_INVALIDPARAMS);
    const DDPIXELFORMAT *list = dev->hal ? D3dTextureFormats : SOFTWARE_FORMATS;
    DWORD count = dev->hal ? D3dTextureFormatCount : RTL_NUMBER_OF(SOFTWARE_FORMATS);
    for (DWORD i = 0; i < count; i++) {
        DDPIXELFORMAT copy = list[i];
        if (callback(&copy, context) != D3DENUMRET_OK)
            break;
    }
    return DdLeave(D3D_OK);
}

/* ------------------------------------------------------------------ scenes and targets */

static HRESULT STDMETHODCALLTYPE dev7_begin_scene(IDirect3DDevice7 *This)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->in_scene)
        return DdLeave(D3DERR_SCENE_IN_SCENE);
    if (dev->target->lost || (dev->depth && dev->depth->lost))
        return DdLeave(DDERR_SURFACELOST);
    if (dev->target->usage || (dev->depth && dev->depth->usage))
        return DdLeave(DDERR_SURFACEBUSY);
    if (dev->recording)
        return DdLeave(D3DERR_SCENE_END_FAILED);
    dev->in_scene = TRUE;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_end_scene(IDirect3DDevice7 *This)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!dev->in_scene)
        return DdLeave(D3DERR_SCENE_NOT_IN_SCENE);
    dev->in_scene = FALSE;
    if (dev->recording)
        return DdLeave(D3DERR_SCENE_END_FAILED);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_direct3d(IDirect3DDevice7 *This, LPDIRECT3D7 *d3d)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!d3d)
        return DdLeave(DDERR_INVALIDPARAMS);
    dev->d3d->iface7.lpVtbl->AddRef(&dev->d3d->iface7);
    *d3d = &dev->d3d->iface7;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_set_render_target(IDirect3DDevice7 *This, LPDIRECTDRAWSURFACE7 surface,
                                                        DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    DDSURF *target = DdSurfFromIface(surface);
    if (!target)
        return DdLeave(DDERR_INVALIDOBJECT);
    if (!(target->desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE) ||
        !(D3dDdbd(target->format.bpp) & dev->desc.dwDeviceRenderBitDepth))
        return DdLeave(DDERR_INVALIDCAPS);
    if (target->desc.dwWidth > 2048 || target->desc.dwHeight > 2048)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (FAILED(hr = D3dCheckPalette(target)))
        return DdLeave(hr);
    D3dHoldTargets(dev, target, D3dAttachedDepth(target));
    if (!D3dPalettized(target) &&
        (!target->format.pf.dwRBitMask || !target->format.pf.dwGBitMask || !target->format.pf.dwBBitMask))
        return DdLeave(DDERR_INVALIDPIXELFORMAT);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_render_target(IDirect3DDevice7 *This, LPDIRECTDRAWSURFACE7 *surface)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!surface)
        return DdLeave(DDERR_INVALIDPARAMS);
    DdSurfAddRef(dev->target, DdSurfRefOf(dev->target, DDV7));
    *surface = DdSurfIface(dev->target, DDV7);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_clear(IDirect3DDevice7 *This, DWORD count, LPD3DRECT rects, DWORD flags,
                                            D3DCOLOR color, D3DVALUE z, DWORD stencil)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (count > 4096)
        return DdLeave(DDERR_INVALIDPARAMS);
    if ((flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) && !dev->depth)
        return DdLeave(D3DERR_ZBUFFER_NOTPRESENT);
    if ((flags & D3DCLEAR_STENCIL) && !(dev->depth->format.pf.dwFlags & DDPF_STENCILBUFFER))
        return DdLeave(D3DERR_STENCILBUFFER_NOTPRESENT);
    if (!(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
        return DdLeave(DDERR_INVALIDPARAMS);
    flags &= D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
    /* the rectangles within the viewport, which a null list clears whole. */
    const D3DVIEWPORT7 *vp = &dev->viewport;
    RECT view = {(LONG)vp->dwX, (LONG)vp->dwY, (LONG)(vp->dwX + vp->dwWidth), (LONG)(vp->dwY + vp->dwHeight)};
    D3DRECT *list = HeapAlloc(GetProcessHeap(), 0, (rects ? count : 1) * sizeof(*list) + 1);
    if (!list)
        return DdLeave(DDERR_OUTOFMEMORY);
    DWORD n = 0;
    if (!rects) {
        list[n++] = (D3DRECT){{view.left}, {view.top}, {view.right}, {view.bottom}};
    } else {
        for (DWORD i = 0; i < count; i++) {
            RECT r = {rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2}, c;
            if (IntersectRect(&c, &r, &view))
                list[n++] = (D3DRECT){{c.left}, {c.top}, {c.right}, {c.bottom}};
        }
    }
    hr = D3dClearRects(dev, n, list, flags, color, z, stencil);
    HeapFree(GetProcessHeap(), 0, list);
    return DdLeave(hr);
}

/* ------------------------------------------------------------------ transforms, viewport, material, lights */

static HRESULT STDMETHODCALLTYPE dev7_set_transform(IDirect3DDevice7 *This, D3DTRANSFORMSTATETYPE type,
                                                    LPD3DMATRIX matrix)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!matrix)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_TRANSFORM, type, 0);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        item->matrix = *matrix;
    } else if (valid_transform(type)) {
        D3dSetTransform(dev, type, matrix);
    }
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_transform(IDirect3DDevice7 *This, D3DTRANSFORMSTATETYPE type,
                                                    LPD3DMATRIX matrix)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!valid_transform(type) || !matrix)
        return DdLeave(DDERR_INVALIDPARAMS);
    *matrix = dev->transforms[type];
    return DdLeave(D3D_OK);
}

D3DMATRIX D3dMultiply(const D3DMATRIX *a, const D3DMATRIX *b)
{
    D3DMATRIX r;
    const float *x = &a->_11, *y = &b->_11;
    float *o = &r._11;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            o[i * 4 + j] = x[i * 4] * y[j] + x[i * 4 + 1] * y[4 + j] + x[i * 4 + 2] * y[8 + j] + x[i * 4 + 3] * y[12 + j];
    return r;
}

/* the matrix given times the current one, at once even while a block records. */
static HRESULT STDMETHODCALLTYPE dev7_multiply_transform(IDirect3DDevice7 *This, D3DTRANSFORMSTATETYPE type,
                                                         LPD3DMATRIX matrix)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!valid_transform(type) || !matrix)
        return DdLeave(DDERR_INVALIDPARAMS);
    D3DMATRIX m = D3dMultiply(matrix, &dev->transforms[type]);
    D3dSetTransform(dev, type, &m);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_set_viewport(IDirect3DDevice7 *This, LPD3DVIEWPORT7 vp)
{
    if (!vp)
        return DDERR_INVALIDPARAMS;
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_VIEWPORT, 0, 0);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        item->viewport = *vp;
        return DdLeave(D3D_OK);
    }
    DWORD w = dev->target->desc.dwWidth, h = dev->target->desc.dwHeight;
    if (vp->dwX > w || vp->dwY > h || vp->dwX + vp->dwWidth > w || vp->dwY + vp->dwHeight > h)
        return DdLeave(DDERR_INVALIDPARAMS);
    D3dSetViewport(dev, vp);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_viewport(IDirect3DDevice7 *This, LPD3DVIEWPORT7 vp)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!vp)
        return DdLeave(DDERR_INVALIDPARAMS);
    *vp = dev->viewport;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_set_material(IDirect3DDevice7 *This, LPD3DMATERIAL7 material)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!material)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_MATERIAL, 0, 0);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        item->material = *material;
    } else {
        D3dSetMaterial(dev, material);
    }
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_material(IDirect3DDevice7 *This, LPD3DMATERIAL7 material)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!material)
        return DdLeave(DDERR_INVALIDPARAMS);
    *material = dev->material;
    return DdLeave(D3D_OK);
}

/* windows' checks of a light, the same while recording. */
static BOOL valid_light(const D3DLIGHT7 *l)
{
    if (l->dltType < D3DLIGHT_POINT || l->dltType > D3DLIGHT_DIRECTIONAL)
        return FALSE;
    if (l->dvRange < 0.0f || l->dvRange > 1.8446743e19f)
        return FALSE;
    if (l->dltType != D3DLIGHT_POINT) {
        const D3DVECTOR *d = &l->dvDirection;
        if (d->x * d->x + d->y * d->y + d->z * d->z < 0.0000099999997f)
            return FALSE;
    }
    if (l->dltType == D3DLIGHT_SPOT) {
        if (l->dvPhi < 0.0f || l->dvPhi > 3.1415927f || l->dvTheta < 0.0f || l->dvTheta > l->dvPhi)
            return FALSE;
        if (l->dvAttenuation0 < 0.0f || l->dvAttenuation1 < 0.0f || l->dvAttenuation2 < 0.0f)
            return FALSE;
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE dev7_set_light(IDirect3DDevice7 *This, DWORD index, LPD3DLIGHT7 light)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!light || !valid_light(light))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_LIGHT, index, 0);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        item->light = *light;
        return DdLeave(D3D_OK);
    }
    return DdLeave(D3dSetLight(dev, index, light));
}

static HRESULT STDMETHODCALLTYPE dev7_get_light(IDirect3DDevice7 *This, DWORD index, LPD3DLIGHT7 light)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!light || index >= dev->light_count || !dev->lights[index].set)
        return DdLeave(DDERR_INVALIDPARAMS);
    *light = dev->lights[index].light;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_light_enable(IDirect3DDevice7 *This, DWORD index, BOOL enable)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording)
        return DdLeave(append(dev->recording, D3DI_LIGHTENABLE, index, enable != 0) ? D3D_OK : DDERR_OUTOFMEMORY);
    return DdLeave(D3dLightEnable(dev, index, enable));
}

/* an enabled light reads back as 128, as windows' does. */
static HRESULT STDMETHODCALLTYPE dev7_get_light_enable(IDirect3DDevice7 *This, DWORD index, BOOL *enable)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!enable || index >= dev->light_count || !dev->lights[index].set)
        return DdLeave(DDERR_INVALIDPARAMS);
    *enable = dev->lights[index].enabled ? 128 : 0;
    return DdLeave(D3D_OK);
}

/* ------------------------------------------------------------------ render states */

static BOOL legacy_state(DWORD type)
{
    switch (type) {
    case D3DRENDERSTATE_TEXTUREHANDLE:
    case D3DRENDERSTATE_TEXTUREADDRESS:
    case D3DRENDERSTATE_WRAPU:
    case D3DRENDERSTATE_WRAPV:
    case D3DRENDERSTATE_MONOENABLE:
    case D3DRENDERSTATE_ROP2:
    case D3DRENDERSTATE_PLANEMASK:
    case D3DRENDERSTATE_TEXTUREMAG:
    case D3DRENDERSTATE_TEXTUREMIN:
    case D3DRENDERSTATE_TEXTUREMAPBLEND:
    case D3DRENDERSTATE_SUBPIXEL:
    case D3DRENDERSTATE_SUBPIXELX:
    case D3DRENDERSTATE_STIPPLEENABLE:
    case D3DRENDERSTATE_BORDERCOLOR:
    case D3DRENDERSTATE_TEXTUREADDRESSU:
    case D3DRENDERSTATE_TEXTUREADDRESSV:
    case D3DRENDERSTATE_MIPMAPLODBIAS:
    case D3DRENDERSTATE_ANISOTROPY:
    case D3DRENDERSTATE_TRANSLUCENTSORTINDEPENDENT:
        return TRUE;
    default:
        return type >= D3DRENDERSTATE_STIPPLEPATTERN00 && type <= D3DRENDERSTATE_STIPPLEPATTERN31;
    }
}

/* states of the first interfaces are stored, then refused; types past 255 are ignored. */
static HRESULT STDMETHODCALLTYPE dev7_set_render_state(IDirect3DDevice7 *This, D3DRENDERSTATETYPE type, DWORD value)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording) {
        if (legacy_state(type))
            return DdLeave(DDERR_INVALIDPARAMS);
        return DdLeave(append(dev->recording, D3DI_RS, type, value) ? D3D_OK : DDERR_OUTOFMEMORY);
    }
    if ((DWORD)type >= 256 || dev->rs[type] == value)
        return DdLeave(D3D_OK);
    D3dSetRs(dev, type, value);
    return DdLeave(legacy_state(type) ? DDERR_INVALIDPARAMS : D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_render_state(IDirect3DDevice7 *This, D3DRENDERSTATETYPE type,
                                                       LPDWORD value)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (legacy_state(type) || !value)
        return DdLeave(DDERR_INVALIDPARAMS);
    *value = dev->rs[(DWORD)type < 256 ? type : 2];
    return DdLeave(D3D_OK);
}

/* ------------------------------------------------------------------ state blocks */

static HRESULT STDMETHODCALLTYPE dev7_begin_state_block(IDirect3DDevice7 *This)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording)
        return DdLeave(D3DERR_INBEGINSTATEBLOCK);
    D3DBLOCK *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    DWORD handle = b ? new_handle(dev, b) : 0;
    if (!handle) {
        HeapFree(GetProcessHeap(), 0, b);
        return DdLeave(DDERR_OUTOFMEMORY);
    }
    /* the handle is taken, its block complete only at the end. */
    dev->blocks[handle] = nullptr;
    dev->recording = b;
    dev->recording_handle = handle;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_end_state_block(IDirect3DDevice7 *This, LPDWORD handle)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!dev->recording)
        return DdLeave(D3DERR_NOTINBEGINSTATEBLOCK);
    dev->blocks[dev->recording_handle] = dev->recording;
    dev->recording = nullptr;
    if (handle)
        *handle = dev->recording_handle;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_apply_state_block(IDirect3DDevice7 *This, DWORD handle)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording)
        return DdLeave(D3DERR_INBEGINSTATEBLOCK);
    D3DBLOCK *b = block_of(dev, handle);
    if (!b)
        return DdLeave(handle < dev->block_count ? D3D_OK : D3DERR_INVALIDSTATEBLOCK);
    return DdLeave(apply_block(dev, b));
}

static HRESULT STDMETHODCALLTYPE dev7_capture_state_block(IDirect3DDevice7 *This, DWORD handle)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording)
        return DdLeave(D3DERR_INBEGINSTATEBLOCK);
    D3DBLOCK *b = block_of(dev, handle);
    if (!b)
        return DdLeave(D3DERR_INVALIDSTATEBLOCK);
    return DdLeave(capture_block(dev, b));
}

static HRESULT STDMETHODCALLTYPE dev7_delete_state_block(IDirect3DDevice7 *This, DWORD handle)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording)
        return DdLeave(D3DERR_INBEGINSTATEBLOCK);
    D3DBLOCK *b = block_of(dev, handle);
    if (!b)
        return DdLeave(D3DERR_INVALIDSTATEBLOCK);
    free_block(b);
    dev->blocks[handle] = nullptr;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_create_state_block(IDirect3DDevice7 *This, D3DSTATEBLOCKTYPE type,
                                                         LPDWORD handle)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording)
        return DdLeave(D3DERR_INBEGINSTATEBLOCK);
    if ((DWORD)type > D3DSBT_VERTEXSTATE)
        return DdLeave(DDERR_INVALIDPARAMS);
    D3DBLOCK *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b || (type && !predefined(dev, b, type))) {
        free_block(b);
        return DdLeave(DDERR_OUTOFMEMORY);
    }
    DWORD h = new_handle(dev, b);
    if (!h) {
        free_block(b);
        return DdLeave(DDERR_OUTOFMEMORY);
    }
    if (handle)
        *handle = h;
    return DdLeave(D3D_OK);
}

/* ------------------------------------------------------------------ textures */

static HRESULT STDMETHODCALLTYPE dev7_pre_load(IDirect3DDevice7 *This, LPDIRECTDRAWSURFACE7 texture)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    DDSURF *s = DdSurfFromIface(texture);
    if (!s)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!(s->desc.ddsCaps.dwCaps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)))
        return DdLeave(DDERR_INVALIDPARAMS);
    D3dSurfImage(dev, s, DB_TEXTURE);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_texture(IDirect3DDevice7 *This, DWORD stage, LPDIRECTDRAWSURFACE7 *texture)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!texture || stage >= D3D_STAGES)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDSURF *s = dev->textures[stage];
    if (s)
        DdSurfAddRef(s, DdSurfRefOf(s, DDV7));
    *texture = s ? DdSurfIface(s, DDV7) : nullptr;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_set_texture(IDirect3DDevice7 *This, DWORD stage, LPDIRECTDRAWSURFACE7 texture)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (stage >= D3D_STAGES)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDSURF *s = texture ? DdSurfFromIface(texture) : nullptr;
    if (texture && !s)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (s && !(s->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_TEXTURE, stage, 0);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        record_texture(item, s);
        return DdLeave(D3D_OK);
    }
    D3dSetTexture(dev, stage, s);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_texture_stage_state(IDirect3DDevice7 *This, DWORD stage,
                                                              D3DTEXTURESTAGESTATETYPE type, LPDWORD value)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (stage >= D3D_STAGES || (DWORD)type >= 32 || !value)
        return DdLeave(DDERR_INVALIDPARAMS);
    *value = dev->tss[stage][type];
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_set_texture_stage_state(IDirect3DDevice7 *This, DWORD stage,
                                                              D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_TSS, stage, type);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        item->value = value;
        return DdLeave(D3D_OK);
    }
    if (stage >= D3D_STAGES || type < D3DTSS_COLOROP || type > D3DTSS_TEXTURETRANSFORMFLAGS)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (dev->tss[stage][type] != value)
        D3dSetTss(dev, stage, type, value);
    return DdLeave(D3D_OK);
}

/* what the rgb rasterizer can blend, in one pass; a hardware device's driver does everything here. */
static HRESULT STDMETHODCALLTYPE dev7_validate_device(IDirect3DDevice7 *This, LPDWORD passes)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!passes)
        return DdLeave(DDERR_INVALIDPARAMS);
    *passes = 1;
    if (dev->hal)
        return DdLeave(D3D_OK);
    for (DWORD stage = 0; stage < D3D_STAGES; stage++) {
        const DWORD *t = dev->tss[stage];
        DWORD op = t[D3DTSS_COLOROP];
        if (op == D3DTOP_DISABLE)
            break;
        if (!((op >= D3DTOP_SELECTARG1 && op <= D3DTOP_BLENDTEXTUREALPHAPM) || op == D3DTOP_MODULATEALPHA_ADDCOLOR ||
              op == D3DTOP_MODULATECOLOR_ADDALPHA))
            return DdLeave(D3DERR_UNSUPPORTEDCOLOROPERATION);
        DWORD a1 = t[D3DTSS_COLORARG1] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE),
              a2 = t[D3DTSS_COLORARG2] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
        if (a1 != D3DTA_TEXTURE || (a2 != D3DTA_DIFFUSE && a2 != D3DTA_CURRENT && a2 != D3DTA_TFACTOR &&
                                    a2 != D3DTA_SPECULAR))
            return DdLeave(D3DERR_UNSUPPORTEDCOLORARG);
        DWORD aop = t[D3DTSS_ALPHAOP];
        if (aop == D3DTOP_DISABLE)
            continue;
        if (aop < D3DTOP_SELECTARG1 || aop > D3DTOP_BLENDTEXTUREALPHAPM)
            return DdLeave(D3DERR_UNSUPPORTEDALPHAOPERATION);
        a1 = t[D3DTSS_ALPHAARG1] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
        a2 = t[D3DTSS_ALPHAARG2] & ~(D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
        if (a1 != D3DTA_TEXTURE || (a2 != D3DTA_DIFFUSE && a2 != D3DTA_CURRENT && a2 != D3DTA_TFACTOR &&
                                    a2 != D3DTA_SPECULAR))
            return DdLeave(D3DERR_UNSUPPORTEDALPHAARG);
    }
    return DdLeave(D3D_OK);
}

/* a texture's levels copied into another's, through ddraw's blts. */
static HRESULT STDMETHODCALLTYPE dev7_load(IDirect3DDevice7 *This, LPDIRECTDRAWSURFACE7 dst_iface, LPPOINT point,
                                           LPDIRECTDRAWSURFACE7 src_iface, LPRECT rect, DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    DDSURF *dst = DdSurfFromIface(dst_iface), *src = DdSurfFromIface(src_iface);
    if (!dst || !src)
        return DdLeave(DDERR_INVALIDPARAMS);
    RECT r = rect ? *rect : (RECT){0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight};
    POINT at = point ? *point : (POINT){0, 0};
    hr = D3D_OK;
    while (dst && src && SUCCEEDED(hr)) {
        RECT d = {at.x, at.y, at.x + (r.right - r.left), at.y + (r.bottom - r.top)};
        if (d.right > (LONG)dst->desc.dwWidth || d.bottom > (LONG)dst->desc.dwHeight)
            return DdLeave(DDERR_INVALIDPARAMS);
        hr = DdBlt(dst, &d, src, DDV7, &r, DDBLT_WAIT, nullptr, DDV7);
        /* the next levels, at half the size. */
        DDSURF *dn = nullptr, *sn = nullptr;
        for (LIST_ENTRY *e = dst->attached.Flink; e != &dst->attached; e = e->Flink) {
            DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
            if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth < dst->desc.dwWidth)
                dn = a;
        }
        for (LIST_ENTRY *e = src->attached.Flink; e != &src->attached; e = e->Flink) {
            DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
            if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth < src->desc.dwWidth)
                sn = a;
        }
        if (!dn || !sn || r.right - r.left < 2 || r.bottom - r.top < 2)
            break;
        dst = dn;
        src = sn;
        r = (RECT){r.left / 2, r.top / 2, r.right / 2, r.bottom / 2};
        at = (POINT){at.x / 2, at.y / 2};
    }
    (void)flags;
    return DdLeave(SUCCEEDED(hr) ? D3D_OK : hr);
}

static HRESULT STDMETHODCALLTYPE dev7_get_info(IDirect3DDevice7 *This, DWORD id, LPVOID info, DWORD size)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (info && size)
        memset(info, 0, size);
    return DdLeave(id >= D3DDEVINFOID_TEXTUREMANAGER && id <= D3DDEVINFOID_TEXTURING ? S_FALSE : E_FAIL);
}

/* ------------------------------------------------------------------ drawing methods */

static HRESULT STDMETHODCALLTYPE dev7_draw_primitive(IDirect3DDevice7 *This, D3DPRIMITIVETYPE type, DWORD fvf,
                                                     LPVOID vertices, DWORD count, DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    return DdLeave(D3dDraw(dev, type, fvf, vertices, count, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev7_draw_indexed_primitive(IDirect3DDevice7 *This, D3DPRIMITIVETYPE type, DWORD fvf,
                                                             LPVOID vertices, DWORD vertex_count, LPWORD indices,
                                                             DWORD index_count, DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!indices)
        return DdLeave(D3D_OK);
    return DdLeave(D3dDraw(dev, type, fvf, vertices, vertex_count, indices, index_count, flags));
}

static HRESULT STDMETHODCALLTYPE dev7_set_clip_status(IDirect3DDevice7 *This, LPD3DCLIPSTATUS status)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!status || (status->dwFlags & D3DCLIPSTATUS_EXTENTS3))
        return DdLeave(DDERR_INVALIDPARAMS);
    if (status->dwFlags & D3DCLIPSTATUS_STATUS)
        dev->clip_status.dwStatus = status->dwStatus;
    if (status->dwFlags & D3DCLIPSTATUS_EXTENTS2) {
        dev->clip_status.minx = status->minx;
        dev->clip_status.maxx = status->maxx;
        dev->clip_status.miny = status->miny;
        dev->clip_status.maxy = status->maxy;
    }
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_clip_status(IDirect3DDevice7 *This, LPD3DCLIPSTATUS status)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!status)
        return DdLeave(DDERR_INVALIDPARAMS);
    status->dwFlags = D3DCLIPSTATUS_EXTENTS2;
    status->dwStatus = dev->clip_status.dwStatus;
    status->minx = dev->clip_status.minx;
    status->maxx = dev->clip_status.maxx;
    status->miny = dev->clip_status.miny;
    status->maxy = dev->clip_status.maxy;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_draw_primitive_strided(IDirect3DDevice7 *This, D3DPRIMITIVETYPE type,
                                                             DWORD fvf, LPD3DDRAWPRIMITIVESTRIDEDDATA data,
                                                             DWORD count, DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
        return DdLeave(D3DERR_INVALIDVERTEXTYPE);
    if (!data || !count)
        return DdLeave(D3D_OK);
    const BYTE *packed = D3dPackStrided(dev, fvf, data, count);
    if (!packed)
        return DdLeave(DDERR_OUTOFMEMORY);
    return DdLeave(D3dDraw(dev, type, fvf, packed, count, nullptr, 0, flags));
}

static HRESULT STDMETHODCALLTYPE dev7_draw_indexed_primitive_strided(IDirect3DDevice7 *This, D3DPRIMITIVETYPE type,
                                                                     DWORD fvf, LPD3DDRAWPRIMITIVESTRIDEDDATA data,
                                                                     DWORD vertex_count, LPWORD indices,
                                                                     DWORD index_count, DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
        return DdLeave(D3DERR_INVALIDVERTEXTYPE);
    if (!data || !vertex_count || !indices)
        return DdLeave(D3D_OK);
    const BYTE *packed = D3dPackStrided(dev, fvf, data, vertex_count);
    if (!packed)
        return DdLeave(DDERR_OUTOFMEMORY);
    return DdLeave(D3dDraw(dev, type, fvf, packed, vertex_count, indices, index_count, flags));
}

static HRESULT STDMETHODCALLTYPE dev7_draw_primitive_vb(IDirect3DDevice7 *This, D3DPRIMITIVETYPE type,
                                                        LPDIRECT3DVERTEXBUFFER7 buffer, DWORD start, DWORD count,
                                                        DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    D3DVB *vb = D3dVbFromIface7(buffer);
    if (!vb)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (start >= vb->desc.dwNumVertices)
        return DdLeave(D3D_OK);
    count = count < vb->desc.dwNumVertices - start ? count : vb->desc.dwNumVertices - start;
    return DdLeave(D3dDraw(dev, type, vb->desc.dwFVF, vb->data + (SIZE_T)start * vb->stride, count, nullptr, 0,
                           flags));
}

static HRESULT STDMETHODCALLTYPE dev7_draw_indexed_primitive_vb(IDirect3DDevice7 *This, D3DPRIMITIVETYPE type,
                                                                LPDIRECT3DVERTEXBUFFER7 buffer, DWORD start,
                                                                DWORD vertex_count, LPWORD indices,
                                                                DWORD index_count, DWORD flags)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    D3DVB *vb = D3dVbFromIface7(buffer);
    if (!vb)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (!indices || start >= vb->desc.dwNumVertices)
        return DdLeave(D3D_OK);
    vertex_count = vertex_count < vb->desc.dwNumVertices - start ? vertex_count : vb->desc.dwNumVertices - start;
    return DdLeave(D3dDraw(dev, type, vb->desc.dwFVF, vb->data + (SIZE_T)start * vb->stride, vertex_count, indices,
                           index_count, flags));
}

static D3DVECTOR transform_point(const D3DMATRIX *m, float x, float y, float z, float *w)
{
    D3DVECTOR r;
    r.x = x * m->_11 + y * m->_21 + z * m->_31 + m->_41;
    r.y = x * m->_12 + y * m->_22 + z * m->_32 + m->_42;
    r.z = x * m->_13 + y * m->_23 + z * m->_33 + m->_43;
    *w = x * m->_14 + y * m->_24 + z * m->_34 + m->_44;
    return r;
}

/* the cube around each sphere through world, view and projection, its corners' clip codes joined. */
static HRESULT STDMETHODCALLTYPE dev7_compute_sphere_visibility(IDirect3DDevice7 *This, LPD3DVECTOR centers,
                                                                LPD3DVALUE radii, DWORD count, DWORD flags,
                                                                LPDWORD results)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (!centers || !radii || !results)
        return DdLeave(DDERR_INVALIDPARAMS);
    D3DMATRIX wv = D3dMultiply(&dev->transforms[D3DTRANSFORMSTATE_WORLD], &dev->transforms[D3DTRANSFORMSTATE_VIEW]);
    D3DMATRIX all = D3dMultiply(&wv, &dev->transforms[D3DTRANSFORMSTATE_PROJECTION]);
    DWORD planes = dev->rs[D3DRENDERSTATE_CLIPPLANEENABLE];
    for (DWORD i = 0; i < count; i++) {
        DWORD any = 0, every = 0xFFF;
        for (int corner = 0; corner < 8; corner++) {
            float x = centers[i].x + (corner & 1 ? radii[i] : -radii[i]);
            float y = centers[i].y + (corner & 2 ? radii[i] : -radii[i]);
            float z = centers[i].z + (corner & 4 ? radii[i] : -radii[i]);
            float w;
            D3DVECTOR c = transform_point(&all, x, y, z, &w);
            DWORD code = 0;
            if (c.x < -w)
                code |= D3DCLIP_LEFT;
            if (c.x > w)
                code |= D3DCLIP_RIGHT;
            if (c.y > w)
                code |= D3DCLIP_TOP;
            if (c.y < -w)
                code |= D3DCLIP_BOTTOM;
            if (c.z < 0.0f)
                code |= D3DCLIP_FRONT;
            if (c.z > w)
                code |= D3DCLIP_BACK;
            for (DWORD p = 0; p < D3D_CLIP_PLANES; p++) {
                const float *pl = dev->clip_planes[p];
                if ((planes >> p & 1) && pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] * w < 0.0f)
                    code |= D3DCLIP_GEN0 << p;
            }
            any |= code;
            every &= code;
        }
        results[i] = any | every << 12;
    }
    (void)flags;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_set_clip_plane(IDirect3DDevice7 *This, DWORD index, D3DVALUE *plane)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (index >= D3D_CLIP_PLANES || !plane)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (dev->recording) {
        D3DITEM *item = append(dev->recording, D3DI_CLIPPLANE, index, 0);
        if (!item)
            return DdLeave(DDERR_OUTOFMEMORY);
        memcpy(item->plane, plane, sizeof(item->plane));
        return DdLeave(D3D_OK);
    }
    set_clip_plane(dev, index, plane);
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE dev7_get_clip_plane(IDirect3DDevice7 *This, DWORD index, D3DVALUE *plane)
{
    HRESULT hr;
    D3DDEV *dev = enter(This, &hr);
    if (!dev)
        return hr;
    if (index >= D3D_CLIP_PLANES || !plane)
        return DdLeave(DDERR_INVALIDPARAMS);
    memcpy(plane, dev->clip_planes[index], sizeof(dev->clip_planes[index]));
    return DdLeave(D3D_OK);
}

static const IDirect3DDevice7Vtbl dev7_vtbl = {
    dev7_query,
    dev7_add_ref,
    dev7_release,
    dev7_get_caps,
    dev7_enum_texture_formats,
    dev7_begin_scene,
    dev7_end_scene,
    dev7_get_direct3d,
    dev7_set_render_target,
    dev7_get_render_target,
    dev7_clear,
    dev7_set_transform,
    dev7_get_transform,
    dev7_set_viewport,
    dev7_multiply_transform,
    dev7_get_viewport,
    dev7_set_material,
    dev7_get_material,
    dev7_set_light,
    dev7_get_light,
    dev7_set_render_state,
    dev7_get_render_state,
    dev7_begin_state_block,
    dev7_end_state_block,
    dev7_pre_load,
    dev7_draw_primitive,
    dev7_draw_indexed_primitive,
    dev7_set_clip_status,
    dev7_get_clip_status,
    dev7_draw_primitive_strided,
    dev7_draw_indexed_primitive_strided,
    dev7_draw_primitive_vb,
    dev7_draw_indexed_primitive_vb,
    dev7_compute_sphere_visibility,
    dev7_get_texture,
    dev7_set_texture,
    dev7_get_texture_stage_state,
    dev7_set_texture_stage_state,
    dev7_validate_device,
    dev7_apply_state_block,
    dev7_capture_state_block,
    dev7_delete_state_block,
    dev7_create_state_block,
    dev7_load,
    dev7_light_enable,
    dev7_get_light_enable,
    dev7_set_clip_plane,
    dev7_get_clip_plane,
    dev7_get_info,
};
