/*
 * 8-bit palettized targets and the ramp device, as d3dim.dll's ramp
 * lighting driver has them, build 26300. direct3d takes the entries of
 * the target's palette the program left free: the rgb devices put a
 * 6x6x6 color cube there and quantize every pixel to it, the ramp device
 * the colors of its material ramps. the backend draws in 32-bit color;
 * the pixels become indices when the cpu reads them.
 *
 * the ramp device is drawn in rgb: a vertex gets the color of the ramp
 * entry its intensity selects, which the backend interpolates. windows
 * interpolates the ramp index instead, the same colors where a ramp is
 * linear; and the per pixel ramp stage of windows 11's rasterizer is not
 * established, so this is the classic model, guessed.
 */
#include "d3d_private.h"

double __cdecl sqrt(double x);
double __cdecl pow(double x, double y);
double __cdecl cos(double x);

enum { PAL_FREE, PAL_KEPT, PAL_RESERVED };

struct D3DPAL8 {
    DDPAL *palette;
    BYTE kind[256];
    WORD refs[256];
    PALETTEENTRY colors[256];
    BOOL dirty;
    BYTE cube[216];
    /* the material whose ramp has its entries, which change the next one asks for its own. */
    D3DMATERIAL ramp;
    float ramp_ambient;
    BOOL ramp_set;
};

static BOOL near_color(const PALETTEENTRY *e, BYTE r, BYTE g, BYTE b)
{
    return (e->peRed & 0xF8) == (r & 0xF8) && (e->peGreen & 0xF8) == (g & 0xF8) && (e->peBlue & 0xF8) == (b & 0xF8);
}

/* an entry for a color: one in use close enough, a free one, or the nearest in use when none is free. */
static BYTE allocate(D3DPAL8 *p, BYTE r, BYTE g, BYTE b)
{
    for (DWORD i = 0; i < 256; i++)
        if ((p->kind[i] == PAL_KEPT || p->refs[i]) && near_color(&p->colors[i], r, g, b)) {
            if (p->kind[i] == PAL_FREE)
                p->refs[i]++;
            return (BYTE)i;
        }
    for (DWORD i = 0; i < 256; i++)
        if (p->kind[i] == PAL_FREE && !p->refs[i]) {
            p->colors[i] = (PALETTEENTRY){r, g, b, 0};
            p->refs[i] = 1;
            p->dirty = TRUE;
            return (BYTE)i;
        }
    DWORD best = 0, distance = 0xFFFFFFFF;
    for (DWORD i = 0; i < 256; i++) {
        if (p->kind[i] != PAL_KEPT && !p->refs[i])
            continue;
        int dr = p->colors[i].peRed - r, dg = p->colors[i].peGreen - g, db = p->colors[i].peBlue - b;
        DWORD d = (DWORD)(dr * dr + dg * dg + db * db);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    if (p->kind[best] == PAL_FREE && p->refs[best])
        p->refs[best]++;
    return (BYTE)best;
}

/* the colors taken reach the palette, in the entries still left free there. */
static void flush(D3DPAL8 *p)
{
    if (!p->dirty)
        return;
    p->dirty = FALSE;
    PALETTEENTRY entries[256];
    memcpy(entries, p->palette->entries, sizeof(entries));
    BOOL changed = FALSE;
    for (DWORD i = 0; i < 256 && i < p->palette->count; i++) {
        if ((entries[i].peFlags & 0xC0) || !p->refs[i] || !memcmp(&entries[i], &p->colors[i], sizeof(entries[i])))
            continue;
        entries[i] = p->colors[i];
        changed = TRUE;
    }
    if (changed)
        p->palette->iface.lpVtbl->SetEntries(&p->palette->iface, 0, 0, p->palette->count, entries);
}

static BOOL mono(D3DDEV *dev)
{
    return IsEqualGUID(&dev->guid, &IID_IDirect3DRampDevice);
}

/* the device's target taken: an 8-bit palette gets the cube, or room for ramps. */
void D3dPal8Target(D3DDEV *dev)
{
    DDSURF *s = dev->target;
    BOOL indexed = (s->format.pf.dwFlags & DDPF_PALETTEINDEXED8) && s->palette;
    if (!indexed) {
        HeapFree(GetProcessHeap(), 0, dev->pal8);
        dev->pal8 = nullptr;
        return;
    }
    D3DPAL8 *p = dev->pal8 ? dev->pal8 : HeapAlloc(GetProcessHeap(), 0, sizeof(*p));
    if (!p)
        return;
    memset(p, 0, sizeof(*p));
    dev->pal8 = p;
    p->palette = s->palette;
    for (DWORD i = 0; i < 256; i++) {
        PALETTEENTRY e = i < p->palette->count ? p->palette->entries[i] : (PALETTEENTRY){0, 0, 0, D3DPAL_RESERVED};
        p->kind[i] = !(e.peFlags & 0xC0) ? PAL_FREE : (e.peFlags & 0xC0) == D3DPAL_READONLY ? PAL_KEPT : PAL_RESERVED;
        p->colors[i] = e;
    }
    if (mono(dev)) {
        D3dSurfQuantize(s, nullptr);
        return;
    }
    for (DWORD r = 0; r < 6; r++)
        for (DWORD g = 0; g < 6; g++)
            for (DWORD b = 0; b < 6; b++)
                p->cube[36 * r + 6 * g + b] = allocate(p, (BYTE)(51 * r), (BYTE)(51 * g), (BYTE)(51 * b));
    flush(p);
    D3dSurfQuantize(s, p->cube);
}

void D3dPal8Free(D3DDEV *dev)
{
    HeapFree(GetProcessHeap(), 0, dev->pal8);
    dev->pal8 = nullptr;
}

/* ------------------------------------------------------------------ ramps */

static float min1(float v)
{
    return v > 1.0f ? 1.0f : v;
}

static float ambient_level(D3DDEV *dev)
{
    DWORD c = dev->legacy->light_states[D3DLIGHTSTATE_AMBIENT];
    return (float)(76 * (c >> 16 & 0xFF) + 150 * (c >> 8 & 0xFF) + 29 * (c & 0xFF)) / 65280.0f;
}

static BOOL has_specular(const D3DMATERIAL *m)
{
    return m->dwRampSize > 2 && (m->dcvSpecular.r != 0.0f || m->dcvSpecular.g != 0.0f || m->dcvSpecular.b != 0.0f);
}

/* the color of a ramp's entry: its diffuse part from unlit to lit, then toward the specular peak. */
static DWORD ramp_entry(D3DDEV *dev, const D3DMATERIAL *m, DWORD i)
{
    DWORD n = m->dwRampSize;
    const float *d = &m->dcvDiffuse.r, *am = &m->dcvAmbient.r, *e = &m->dcvEmissive.r, *s = &m->dcvSpecular.r;
    BOOL fog = dev->rs[D3DRENDERSTATE_FOGENABLE] == TRUE;
    DWORD fc = dev->rs[D3DRENDERSTATE_FOGCOLOR];
    float f[3] = {(float)(fc >> 16 & 0xFF) / 255.0f, (float)(fc >> 8 & 0xFF) / 255.0f, (float)(fc & 0xFF) / 255.0f};
    float a = fog || n <= 1 ? 0.0f : ambient_level(dev), out[3];
    DWORD nd = n, ns = 0;
    if (has_specular(m)) {
        nd = 3 * n / 4;
        ns = n / 4;
    }
    for (int c = 0; c < 3; c++) {
        if (n <= 1) {
            out[c] = d[c];
            continue;
        }
        float t = nd > 1 ? (float)(i < nd ? i : nd - 1) / (float)(nd - 1) : 1.0f;
        float lit = fog ? min1(e[c] + t * d[c] + (1.0f - t) * f[c]) : min1(a * am[c] + e[c] + t * (1.0f - a) * d[c]);
        if (i >= nd && ns) {
            float top = fog ? min1(e[c] + d[c]) : min1(a * am[c] + e[c] + (1.0f - a) * d[c]);
            float peak = min1(top + s[c]), u = ns > 1 ? (float)(i - nd) / (float)(ns - 1) : 0.0f;
            lit = (1.0f - u) * top + u * peak;
        }
        out[c] = lit;
    }
    return (DWORD)(out[0] * 255.0f) << 16 | (DWORD)(out[1] * 255.0f) << 8 | (DWORD)(out[2] * 255.0f);
}

/* an 8-bit target holds the colors of the ramp in use. */
static void take_ramp(D3DDEV *dev, const D3DMATERIAL *m)
{
    D3DPAL8 *p = dev->pal8;
    float a = ambient_level(dev);
    if (!p || (p->ramp_set && !memcmp(&p->ramp, m, sizeof(*m)) && p->ramp_ambient == a))
        return;
    p->ramp = *m;
    p->ramp_ambient = a;
    p->ramp_set = TRUE;
    for (DWORD i = 0; i < m->dwRampSize && i < 256; i++) {
        DWORD c = ramp_entry(dev, m, i);
        allocate(p, (BYTE)(c >> 16), (BYTE)(c >> 8), (BYTE)c);
    }
    flush(p);
}

/* the color at a lit and specular intensity of the current material's ramp, gray without one, guessed. */
static DWORD ramp_color(D3DDEV *dev, float lit, float specular)
{
    D3DLEGACY *l = dev->legacy;
    lit = lit < 0.0f ? 0.0f : min1(lit);
    if (!l->light_states[D3DLIGHTSTATE_MATERIAL]) {
        DWORD v = (DWORD)(lit * 255.0f);
        return v << 16 | v << 8 | v;
    }
    const D3DMATERIAL *m = &l->material;
    take_ramp(dev, m);
    if (!m->dwRampSize)
        return 0;
    float v = has_specular(m) ? (1.0f - specular) * (0.75f * lit) + specular : lit;
    DWORD top = m->dwRampSize - 1 > 32767 ? 32767 : m->dwRampSize - 1;
    return ramp_entry(dev, m, (DWORD)(v * (float)top));
}

/* a vertex color of the mono model: its blue channel is the intensity. */
DWORD D3dRampConvert(D3DDEV *dev, DWORD color, DWORD specular)
{
    return (color & 0xFF000000) | ramp_color(dev, (float)(color & 0xFF) / 255.0f, (float)(specular & 0xFF) / 255.0f);
}

/* the background's fill: its ramp's first entry. */
DWORD D3dRampBackground(D3DDEV *dev, const D3DMATERIAL *m)
{
    return m->dwRampSize ? ramp_entry(dev, m, 0) : 0;
}

/*
 * the lights of the current viewport as one gray intensity each, on the
 * mono model's formulas, which keep the second light's attenuation
 * without dividing by the distance or cutting at the range.
 */
void D3dRampLight(D3DDEV *dev, const BYTE *src, DWORD stride, DWORD normal, DWORD count, BYTE *dst,
                  DWORD dst_stride)
{
    D3DLEGACY *l = dev->legacy;
    const D3DMATRIX *w = &dev->transforms[D3DTRANSFORMSTATE_WORLD], *view = &dev->transforms[D3DTRANSFORMSTATE_VIEW];
    D3DMATRIX wv = D3dMultiply(w, view);
    const D3DMATERIAL *m = &l->material;
    BOOL specular_on = has_specular(m) && dev->rs[D3DRENDERSTATE_SPECULARENABLE];
    DWORD alpha = (DWORD)(m->dcvDiffuse.a * 255.0f) << 24;
    for (DWORD i = 0; i < count; i++) {
        const float *v = (const float *)(src + (SIZE_T)i * stride), *n = (const float *)(src + (SIZE_T)i * stride + normal);
        /* camera space, the normal through the world view matrix's rotation, as rigid transforms keep it. */
        float p[3], nn[3];
        for (int k = 0; k < 3; k++) {
            p[k] = v[0] * (&wv._11)[k] + v[1] * (&wv._21)[k] + v[2] * (&wv._31)[k] + (&wv._41)[k];
            nn[k] = n[0] * (&wv._11)[k] + n[1] * (&wv._21)[k] + n[2] * (&wv._31)[k];
        }
        float lit = 0.0f, spec = 0.0f;
        LIST_ENTRY *list = l->current ? &l->current->lights : nullptr;
        for (LIST_ENTRY *e = list ? list->Flink : nullptr; e && e != list; e = e->Flink) {
            D3DLIGHTOBJ *obj = CONTAINING_RECORD(e, D3DLIGHTOBJ, vp_links);
            const D3DLIGHT2 *L = &obj->data;
            BOOL second = L->dwSize == sizeof(D3DLIGHT2);
            if (!obj->set || (second && !(L->dwFlags & D3DLIGHT_ACTIVE)))
                continue;
            float gray = 0.30f * L->dcvColor.r + 0.59f * L->dcvColor.g + 0.11f * L->dcvColor.b;
            float pos[3], dir[3], to[3], factor = 1.0f, ndotl;
            for (int k = 0; k < 3; k++) {
                pos[k] = L->dvPosition.x * (&view->_11)[k] + L->dvPosition.y * (&view->_21)[k] +
                         L->dvPosition.z * (&view->_31)[k] + (&view->_41)[k];
                dir[k] = L->dvDirection.x * (&view->_11)[k] + L->dvDirection.y * (&view->_21)[k] +
                         L->dvDirection.z * (&view->_31)[k];
            }
            if (L->dltType == D3DLIGHT_DIRECTIONAL || L->dltType == D3DLIGHT_PARALLELPOINT) {
                const float *from = L->dltType == D3DLIGHT_DIRECTIONAL ? dir : pos;
                float len = (float)sqrt(from[0] * from[0] + from[1] * from[1] + from[2] * from[2]);
                if (len == 0.0f)
                    continue;
                float sign = L->dltType == D3DLIGHT_DIRECTIONAL ? -1.0f : 1.0f;
                for (int k = 0; k < 3; k++)
                    to[k] = sign * from[k] / len;
                ndotl = nn[0] * to[0] + nn[1] * to[1] + nn[2] * to[2];
            } else {
                for (int k = 0; k < 3; k++)
                    to[k] = pos[k] - p[k];
                float d = (float)sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
                ndotl = nn[0] * to[0] + nn[1] * to[1] + nn[2] * to[2];
                if (d == 0.0f || ndotl <= 0.0f)
                    continue;
                if (second) {
                    float r = (L->dvRange - d) / L->dvRange;
                    factor = L->dvAttenuation0 + L->dvAttenuation1 * r + L->dvAttenuation2 * r * r;
                } else {
                    float a = L->dvAttenuation0 + L->dvAttenuation1 * d + L->dvAttenuation2 * d * d;
                    ndotl /= d;
                    factor = 1.0f / (a > 1.0f ? a : 1.0f);
                }
                if (L->dltType == D3DLIGHT_SPOT) {
                    float dl = (float)sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                    float c = dl == 0.0f ? 0.0f : -(to[0] * dir[0] + to[1] * dir[1] + to[2] * dir[2]) / (d * dl);
                    float umbra = (float)cos(L->dvTheta * 0.5), penumbra = (float)cos(L->dvPhi * 0.5);
                    if (c <= penumbra)
                        continue;
                    if (c < umbra)
                        factor *= 1.0f - (umbra - c) / (umbra - penumbra);
                }
                for (int k = 0; k < 3; k++)
                    to[k] /= d;
            }
            if (ndotl <= 0.0f)
                continue;
            lit += ndotl * factor * gray;
            if (!specular_on)
                continue;
            /* halfway to the eye at the origin. */
            float pl = (float)sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), h[3];
            for (int k = 0; k < 3; k++)
                h[k] = to[k] - (pl != 0.0f ? p[k] / pl : 0.0f);
            float hl = (float)sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            float ndoth = hl == 0.0f ? 0.0f : (nn[0] * h[0] + nn[1] * h[1] + nn[2] * h[2]) / hl;
            if (ndoth > 0.0f)
                spec += factor * gray * (float)pow(ndoth > 1.0f ? 1.0f : ndoth, m->dvPower);
        }
        DWORD out[2] = {alpha | ramp_color(dev, lit, spec > 1.0f ? 1.0f : spec), 0};
        memcpy(dst + (SIZE_T)i * dst_stride, out, sizeof(out));
    }
}
