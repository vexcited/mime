/*
 * the front end of d3dim700 on the cpu, for ProcessVertices: vertices
 * transformed by world, view and projection to the screen, lit in camera
 * space and fogged as direct3d 7 does, their colors and
 * texture coordinates copied into the layout of the destination.
 */
#include "d3d_private.h"

/* msvcrt's, as windows' d3dim700 takes them. */
double __cdecl sqrt(double x);
double __cdecl pow(double x, double y);
double __cdecl exp(double x);
double __cdecl cos(double x);

typedef struct VEC {
    float x, y, z;
} VEC;

static VEC add(VEC a, VEC b)
{
    return (VEC){a.x + b.x, a.y + b.y, a.z + b.z};
}

static VEC sub(VEC a, VEC b)
{
    return (VEC){a.x - b.x, a.y - b.y, a.z - b.z};
}

static VEC scale(VEC a, float s)
{
    return (VEC){a.x * s, a.y * s, a.z * s};
}

static float dot(VEC a, VEC b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static VEC normalize(VEC v)
{
    float l = dot(v, v);
    if (l == 0.0f || l == 1.0f)
        return v;
    return scale(v, 1.0f / (float)sqrt(l));
}

static D3DMATRIX mul(const D3DMATRIX *a, const D3DMATRIX *b)
{
    D3DMATRIX r;
    const float *x = &a->_11, *y = &b->_11;
    float *o = &r._11;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            o[i * 4 + j] = x[i * 4] * y[j] + x[i * 4 + 1] * y[4 + j] + x[i * 4 + 2] * y[8 + j] + x[i * 4 + 3] * y[12 + j];
    return r;
}

/* the inverse of the upper 3x3, transposed: what normals go through; the identity when singular. */
static void normal_matrix(const D3DMATRIX *m, float out[3][3])
{
    float a = m->_11, b = m->_12, c = m->_13, d = m->_21, e = m->_22, f = m->_23, g = m->_31, h = m->_32, i = m->_33;
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det > -1e-20f && det < 1e-20f) {
        memset(out, 0, sizeof(float[3][3]));
        out[0][0] = out[1][1] = out[2][2] = 1.0f;
        return;
    }
    float k = 1.0f / det;
    /* the inverse's transpose is the cofactor matrix over the determinant. */
    out[0][0] = (e * i - f * h) * k;
    out[0][1] = -(d * i - f * g) * k;
    out[0][2] = (d * h - e * g) * k;
    out[1][0] = -(b * i - c * h) * k;
    out[1][1] = (a * i - c * g) * k;
    out[1][2] = -(a * h - b * g) * k;
    out[2][0] = (b * f - c * e) * k;
    out[2][1] = -(a * f - c * d) * k;
    out[2][2] = (a * e - b * d) * k;
}

static VEC point_by(const D3DMATRIX *m, VEC p)
{
    return (VEC){p.x * m->_11 + p.y * m->_21 + p.z * m->_31 + m->_41, p.x * m->_12 + p.y * m->_22 + p.z * m->_32 + m->_42,
                 p.x * m->_13 + p.y * m->_23 + p.z * m->_33 + m->_43};
}

static VEC direction_by(const D3DMATRIX *m, VEC v)
{
    return (VEC){v.x * m->_11 + v.y * m->_21 + v.z * m->_31, v.x * m->_12 + v.y * m->_22 + v.z * m->_32,
                 v.x * m->_13 + v.y * m->_23 + v.z * m->_33};
}

/* a color's channels on the 0 to 255 scale lighting works in. */
static VEC bytes(DWORD c)
{
    return (VEC){(float)(c >> 16 & 0xFF), (float)(c >> 8 & 0xFF), (float)(c & 0xFF)};
}

static VEC value_rgb(const D3DCOLORVALUE *c)
{
    return (VEC){c->r, c->g, c->b};
}

static VEC times(VEC a, VEC b)
{
    return (VEC){a.x * b.x, a.y * b.y, a.z * b.z};
}

static DWORD clamp_byte(float v)
{
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (DWORD)(v + 0.5f);
}

static DWORD pack(VEC c, DWORD alpha)
{
    return alpha << 24 | clamp_byte(c.x) << 16 | clamp_byte(c.y) << 8 | clamp_byte(c.z);
}

static DWORD alpha_of(float a)
{
    return clamp_byte(a * 255.0f);
}

/* where the parts of a vertex of a format lie. */
typedef struct LAYOUT {
    DWORD stride, position, normal, diffuse, specular, tex[8], tex_size[8], sets, weights;
    BOOL has_normal, has_diffuse, has_specular;
} LAYOUT;

static void layout_of(DWORD fvf, LAYOUT *l)
{
    static const DWORD POSITION[] = {12, 12, 16, 16, 20, 24, 28, 32};
    memset(l, 0, sizeof(*l));
    DWORD at = POSITION[(fvf & D3DFVF_POSITION_MASK) >> 1];
    DWORD type = fvf & D3DFVF_POSITION_MASK;
    l->weights = type >= D3DFVF_XYZB1 ? (type - D3DFVF_XYZB1) / 2 + 1 : 0;
    if (fvf & D3DFVF_NORMAL) {
        l->has_normal = TRUE;
        l->normal = at;
        at += 12;
    }
    if (fvf & D3DFVF_RESERVED1)
        at += 4;
    if (fvf & D3DFVF_DIFFUSE) {
        l->has_diffuse = TRUE;
        l->diffuse = at;
        at += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        l->has_specular = TRUE;
        l->specular = at;
        at += 4;
    }
    l->sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD i = 0; i < l->sets && i < 8; i++) {
        static const DWORD COORDS[] = {8, 12, 16, 4};
        l->tex[i] = at;
        l->tex_size[i] = fvf >> 16 ? COORDS[(fvf >> (16 + 2 * i)) & 3] : 8;
        at += l->tex_size[i];
    }
    l->stride = at;
}

static DWORD read_dword(const BYTE *p, DWORD offset)
{
    DWORD v;
    memcpy(&v, p + offset, 4);
    return v;
}

/* the vertex's color for a material source, or none. */
static BOOL source_color(D3DDEV *dev, DWORD state, const LAYOUT *l, const BYTE *v, DWORD *color)
{
    if (!dev->rs[D3DRENDERSTATE_COLORVERTEX])
        return FALSE;
    DWORD src = dev->rs[state];
    if (src == D3DMCS_COLOR1 && l->has_diffuse) {
        *color = read_dword(v, l->diffuse);
        return TRUE;
    }
    if (src == D3DMCS_COLOR2 && l->has_specular) {
        *color = read_dword(v, l->specular);
        return TRUE;
    }
    return FALSE;
}

typedef struct LIT {
    D3DLIGHT7 light;
    VEC position, direction; /* in camera space, the direction toward the light */
    float cos_theta, cos_phi, spot_scale;
} LIT;

/* one vertex lit by every enabled light, camera space position and normal given. */
static void light_vertex(D3DDEV *dev, const LIT *lights, DWORD count, const LAYOUT *l, const BYTE *v, VEC p, VEC n,
                         BOOL has_normal, DWORD *diffuse_out, DWORD *specular_out)
{
    const D3DMATERIAL7 *m = &dev->material;
    DWORD vc;
    VEC md = source_color(dev, D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, l, v, &vc) ? bytes(vc)
                                                                                 : scale(value_rgb(&m->diffuse), 255.0f);
    DWORD diffuse_alpha = source_color(dev, D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, l, v, &vc) ? vc >> 24
                                                                                             : alpha_of(m->diffuse.a);
    VEC ma = source_color(dev, D3DRENDERSTATE_AMBIENTMATERIALSOURCE, l, v, &vc) ? bytes(vc)
                                                                                : scale(value_rgb(&m->ambient), 255.0f);
    VEC ms = source_color(dev, D3DRENDERSTATE_SPECULARMATERIALSOURCE, l, v, &vc)
                 ? bytes(vc)
                 : scale(value_rgb(&m->specular), 255.0f);
    DWORD specular_alpha = source_color(dev, D3DRENDERSTATE_SPECULARMATERIALSOURCE, l, v, &vc)
                               ? vc >> 24
                               : alpha_of(m->specular.a);
    VEC me = source_color(dev, D3DRENDERSTATE_EMISSIVEMATERIALSOURCE, l, v, &vc)
                 ? bytes(vc)
                 : scale(value_rgb(&m->emissive), 255.0f);
    VEC ambient = scale(bytes(dev->rs[D3DRENDERSTATE_AMBIENT]), 1.0f / 255.0f);
    VEC diffuse = add(times(ambient, ma), me), specular = {};
    BOOL do_specular = dev->rs[D3DRENDERSTATE_SPECULARENABLE] && m->power > 0.001f, any_specular = FALSE;
    BOOL local = dev->rs[D3DRENDERSTATE_LOCALVIEWER] != 0;
    for (DWORD i = 0; i < count; i++) {
        const LIT *lit = &lights[i];
        const D3DLIGHT7 *L = &lit->light;
        VEC cd = value_rgb(&L->dcvDiffuse), ca = value_rgb(&L->dcvAmbient), cs = value_rgb(&L->dcvSpecular);
        BOOL light_ambient = ca.x != 0.0f || ca.y != 0.0f || ca.z != 0.0f;
        float atten = 1.0f;
        VEC dir;
        if (L->dltType == D3DLIGHT_DIRECTIONAL) {
            dir = lit->direction;
        } else {
            VEC d = sub(lit->position, p);
            float dist2 = dot(d, d);
            if (dist2 >= L->dvRange * L->dvRange || dist2 == 0.0f)
                continue;
            float dist = (float)sqrt(dist2);
            float denom = L->dvAttenuation0 + L->dvAttenuation1 * dist + L->dvAttenuation2 * dist2;
            atten = denom == 0.0f ? 3.4028235e38f : 1.0f / denom;
            dir = scale(d, 1.0f / dist);
            if (L->dltType == D3DLIGHT_SPOT) {
                float rho = dot(dir, lit->direction);
                if (rho <= lit->cos_phi)
                    continue;
                if (rho < lit->cos_theta) {
                    float spot = (rho - lit->cos_phi) * lit->spot_scale;
                    if (L->dvFalloff != 1.0f)
                        spot = (float)pow(spot, L->dvFalloff);
                    atten *= spot;
                }
            }
        }
        if (light_ambient)
            diffuse = add(diffuse, scale(times(ma, ca), atten));
        if (!has_normal)
            continue;
        float ndotl = dot(n, dir);
        if (ndotl <= 0.0f)
            continue;
        diffuse = add(diffuse, scale(times(md, cd), atten * ndotl));
        if (!do_specular || (cs.x == 0.0f && cs.y == 0.0f && cs.z == 0.0f))
            continue;
        VEC view = local ? normalize(scale(p, -1.0f)) : (VEC){0.0f, 0.0f, -1.0f};
        VEC h = normalize(add(dir, view));
        float ndoth = dot(n, h);
        if (ndoth <= 0.0f || (float)pow(ndoth, m->power) < 0.001f)
            continue;
        specular = add(specular, scale(times(ms, cs), atten * (float)pow(ndoth > 1.0f ? 1.0f : ndoth, m->power)));
        any_specular = TRUE;
    }
    *diffuse_out = pack(diffuse, diffuse_alpha);
    if (any_specular || dev->rs[D3DRENDERSTATE_SPECULARENABLE])
        *specular_out = pack(specular, specular_alpha);
    else
        *specular_out = l->has_specular ? read_dword(v, l->specular) : 0;
}

/* the fog factor of a camera space distance, 1 for none. */
static float fog_factor(D3DDEV *dev, float d)
{
    float start = 0.0f, end = 0.0f, density = 0.0f;
    memcpy(&start, &dev->rs[D3DRENDERSTATE_FOGSTART], 4);
    memcpy(&end, &dev->rs[D3DRENDERSTATE_FOGEND], 4);
    memcpy(&density, &dev->rs[D3DRENDERSTATE_FOGDENSITY], 4);
    switch (dev->rs[D3DRENDERSTATE_FOGVERTEXMODE]) {
    case D3DFOG_LINEAR:
        return end == start ? 1.0f : (end - d) / (end - start);
    case D3DFOG_EXP:
        return (float)exp(-density * d);
    case D3DFOG_EXP2:
        return (float)exp(-(density * d) * (density * d));
    default:
        return 1.0f;
    }
}

HRESULT D3dProcess(D3DDEV *dev, const BYTE *src, DWORD src_fvf, BYTE *dst, DWORD dst_fvf, DWORD count, DWORD op,
                   DWORD flags)
{
    LAYOUT in, out;
    layout_of(src_fvf, &in);
    layout_of(dst_fvf, &out);
    BOOL lighting = (op & D3DVOP_LIGHT) != 0;
    if (lighting && (dev->rs[D3DRENDERSTATE_SPECULARENABLE] || dev->rs[D3DRENDERSTATE_FOGENABLE]) &&
        !out.has_specular)
        return D3DERR_INVALIDVERTEXFORMAT;
    if (out.sets < in.sets)
        return D3DERR_INVALIDVERTEXFORMAT;
    D3DMATRIX wv = mul(&dev->transforms[D3DTRANSFORMSTATE_WORLD], &dev->transforms[D3DTRANSFORMSTATE_VIEW]);
    D3DMATRIX all = mul(&wv, &dev->transforms[D3DTRANSFORMSTATE_PROJECTION]);
    float nm[3][3];
    normal_matrix(&wv, nm);
    /* the enabled lights in camera space. */
    LIT stack[16], *lights = stack;
    DWORD lit_count = 0;
    if (lighting) {
        DWORD enabled = 0;
        for (DWORD i = 0; i < dev->light_count; i++)
            enabled += dev->lights[i].enabled;
        if (enabled > RTL_NUMBER_OF(stack))
            lights = HeapAlloc(GetProcessHeap(), 0, enabled * sizeof(*lights));
        if (!lights)
            return DDERR_OUTOFMEMORY;
        const D3DMATRIX *view = &dev->transforms[D3DTRANSFORMSTATE_VIEW];
        for (DWORD i = 0; i < dev->light_count; i++) {
            if (!dev->lights[i].enabled)
                continue;
            LIT *lit = &lights[lit_count++];
            lit->light = dev->lights[i].light;
            VEC pos = {lit->light.dvPosition.x, lit->light.dvPosition.y, lit->light.dvPosition.z};
            VEC dir = {lit->light.dvDirection.x, lit->light.dvDirection.y, lit->light.dvDirection.z};
            lit->position = point_by(view, pos);
            lit->direction = scale(normalize(direction_by(view, dir)), -1.0f);
            lit->cos_theta = (float)cos(lit->light.dvTheta * 0.5);
            lit->cos_phi = (float)cos(lit->light.dvPhi * 0.5);
            lit->spot_scale = lit->cos_theta != lit->cos_phi ? 1.0f / (lit->cos_theta - lit->cos_phi) : 1.0f;
        }
    }
    const D3DVIEWPORT7 *vp = &dev->viewport;
    BOOL vertex_fog = dev->rs[D3DRENDERSTATE_FOGENABLE] && dev->rs[D3DRENDERSTATE_FOGVERTEXMODE] != D3DFOG_NONE;
    BOOL normalize_normals = dev->rs[D3DRENDERSTATE_NORMALIZENORMALS] != 0;
    BOOL copy = !(flags & D3DPV_DONOTCOPYDATA);
    for (DWORD i = 0; i < count; i++) {
        const BYTE *v = src + (SIZE_T)i * in.stride;
        BYTE *o = dst + (SIZE_T)i * out.stride;
        VEC p;
        memcpy(&p, v, sizeof(p));
        float x = p.x * all._11 + p.y * all._21 + p.z * all._31 + all._41;
        float y = p.x * all._12 + p.y * all._22 + p.z * all._32 + all._42;
        float z = p.x * all._13 + p.y * all._23 + p.z * all._33 + all._43;
        float w = p.x * all._14 + p.y * all._24 + p.z * all._34 + all._44;
        float rhw = w != 0.0f ? 1.0f / w : 0.0f;
        float screen[4] = {(float)vp->dwX + (x * rhw + 1.0f) * 0.5f * (float)vp->dwWidth + 1e-6f,
                           (float)vp->dwY + (1.0f - y * rhw) * 0.5f * (float)vp->dwHeight + 1e-6f,
                           vp->dvMinZ + z * rhw * (vp->dvMaxZ - vp->dvMinZ), rhw};
        memcpy(o, screen, sizeof(screen));
        VEC cam = point_by(&wv, p);
        DWORD diffuse = 0xFFFFFFFF, specular = 0;
        if (in.has_diffuse)
            diffuse = read_dword(v, in.diffuse);
        if (in.has_specular)
            specular = read_dword(v, in.specular);
        BOOL wrote_diffuse = copy, wrote_specular = copy;
        if (lighting && (out.has_diffuse || out.has_specular)) {
            VEC n = {};
            if (in.has_normal) {
                VEC raw;
                memcpy(&raw, v + in.normal, sizeof(raw));
                n = (VEC){raw.x * nm[0][0] + raw.y * nm[1][0] + raw.z * nm[2][0],
                          raw.x * nm[0][1] + raw.y * nm[1][1] + raw.z * nm[2][1],
                          raw.x * nm[0][2] + raw.y * nm[1][2] + raw.z * nm[2][2]};
                if (normalize_normals)
                    n = normalize(n);
            }
            light_vertex(dev, lights, lit_count, &in, v, cam, n, in.has_normal, &diffuse, &specular);
            wrote_diffuse = wrote_specular = TRUE;
        }
        if (vertex_fog) {
            float d = dev->rs[D3DRENDERSTATE_RANGEFOGENABLE] ? (float)sqrt(dot(cam, cam)) : cam.z;
            float f = fog_factor(dev, d);
            specular = (specular & 0x00FFFFFF) | clamp_byte((f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f) * 255.0f) << 24;
            wrote_specular = TRUE;
        }
        if (out.has_diffuse && wrote_diffuse)
            memcpy(o + out.diffuse, &diffuse, 4);
        if (out.has_specular && wrote_specular)
            memcpy(o + out.specular, &specular, 4);
        if (copy)
            for (DWORD t = 0; t < in.sets && t < 8; t++)
                memcpy(o + out.tex[t], v + in.tex[t], in.tex_size[t] < out.tex_size[t] ? in.tex_size[t] : out.tex_size[t]);
    }
    if (lights != stack)
        HeapFree(GetProcessHeap(), 0, lights);
    return D3D_OK;
}
